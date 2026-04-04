🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 29.3 — Reconstruire l'ELF original : fixer les headers, sections et entry point

> 🎯 **Objectif de cette section** — Transformer les dumps mémoire bruts obtenus en section 29.2 en un **fichier ELF valide** que l'on pourra importer dans Ghidra, radare2 ou tout autre outil d'analyse statique. On couvrira la reconstruction manuelle (pour comprendre chaque champ) et la reconstruction assistée par script Python (pour automatiser le processus).

---

## Pourquoi les dumps bruts ne suffisent pas

À la fin de la section 29.2, on dispose de fichiers binaires (`code.bin`, `data_ro.bin`, `data_rw.bin`) contenant le contenu exact des régions mémoire du processus décompressé. On a vérifié avec `strings` et `objdump -D` que le code machine est bien présent et cohérent. Pourquoi ne peut-on pas simplement ouvrir ces fichiers dans Ghidra ?

Le problème est que ces dumps sont des **images mémoire plates** — des séquences d'octets sans aucune métadonnée structurelle. Or un désassembleur comme Ghidra a besoin de savoir où commence le code, où sont les données, quel est le point d'entrée, quelles sont les adresses virtuelles associées à chaque octet, et comment les sections s'articulent entre elles. Toutes ces informations sont normalement portées par le **header ELF** et la **table des sections** (section header table), qui sont absents du dump brut.

On peut certes importer un dump brut dans Ghidra en tant que « Raw Binary » en spécifiant manuellement l'architecture (x86-64), l'adresse de base et le point d'entrée. Mais le résultat est médiocre : Ghidra ne sait pas distinguer le code des données, ne peut pas identifier les chaînes dans `.rodata`, et ne reconstruit pas les cross-references correctement. Un ELF bien formé produit une analyse incomparablement meilleure.

La reconstruction consiste donc à **réemballer** les dumps dans un conteneur ELF en recréant les métadonnées que le packer avait supprimées ou remplacées.

---

## Anatomie rapide d'un ELF : ce qu'il faut reconstruire

Rappelons la structure d'un fichier ELF (détaillée au chapitre 2, sections 2.3–2.4). Un ELF se compose de trois couches de métadonnées :

Le **ELF header** occupe les 64 premiers octets du fichier (en 64 bits). Il contient le magic (`\x7fELF`), la classe (32/64 bits), l'endianness, le type de fichier (`ET_EXEC` ou `ET_DYN`), l'architecture (`EM_X86_64`), le point d'entrée (`e_entry`), et les offsets vers les deux tables qui suivent.

La **program header table** (PHT) décrit les **segments** — les régions contiguës du fichier qui seront mappées en mémoire par le loader. Chaque entrée `Phdr` spécifie le type du segment (`PT_LOAD`, `PT_NOTE`…), les offsets fichier et mémoire, les tailles sur disque et en mémoire, et les flags de permission (`PF_R`, `PF_W`, `PF_X`). C'est la PHT que le noyau Linux utilise pour charger le binaire ; elle est **obligatoire** pour l'exécution.

La **section header table** (SHT) décrit les **sections** — un découpage logique plus fin (`.text`, `.rodata`, `.data`, `.bss`…) utilisé par les outils d'analyse mais **pas par le loader**. C'est pour cela qu'UPX peut supprimer la SHT sans empêcher l'exécution du binaire. Mais c'est justement la SHT qui rend Ghidra efficace : sans elle, le désassembleur ne peut pas distinguer le code des données et perd une grande partie de sa capacité d'analyse automatique.

Notre objectif de reconstruction est donc triple : recréer un ELF header correct, construire une PHT fonctionnelle, et — autant que possible — recréer une SHT avec au minimum les sections `.text`, `.rodata`, `.data` et `.bss`.

---

## Étape 1 — Collecter les informations depuis GDB

Avant de commencer la reconstruction, on rassemble toutes les informations nécessaires. On reprend la session GDB de la section 29.2, arrêtée à l'OEP :

### Adresse de l'OEP

C'est la valeur de `rip` au moment où le breakpoint matériel a été atteint :

```
gef➤ print/x $rip
$1 = 0x401000
```

Cette adresse deviendra le champ `e_entry` de notre ELF reconstruit.

### Carte mémoire complète

```
gef➤ vmmap  
Start              End                Offset Perm  Path  
0x0000000000400000 0x0000000000401000 0x0000 r--p  packed_sample_upx_tampered
0x0000000000401000 0x0000000000404000 0x1000 r-xp  packed_sample_upx_tampered
0x0000000000404000 0x0000000000405000 0x4000 r--p  packed_sample_upx_tampered
0x0000000000405000 0x0000000000406000 0x5000 rw-p  packed_sample_upx_tampered
0x0000000000406000 0x0000000000407000 0x0000 rw-p  [heap]
...
```

On en déduit quatre régions pertinentes issues du binaire (on ignore le heap, la stack et les mappings de la libc/vdso) :

| Adresse début | Adresse fin | Taille   | Permissions | Rôle probable       |  
|---------------|-------------|----------|-------------|---------------------|  
| `0x400000`    | `0x401000`  | `0x1000` | `r--`       | ELF header + PHT    |  
| `0x401000`    | `0x404000`  | `0x3000` | `r-x`       | `.text` (code)      |  
| `0x404000`    | `0x405000`  | `0x1000` | `r--`       | `.rodata` (données lecture seule) |  
| `0x405000`    | `0x406000`  | `0x1000` | `rw-`       | `.data` + `.bss`    |

### Dumps des régions

Si ce n'est pas déjà fait, on dumpe chaque région :

```
gef➤ dump binary memory region_hdr.bin  0x400000 0x401000  
gef➤ dump binary memory region_text.bin 0x401000 0x404000  
gef➤ dump binary memory region_ro.bin   0x404000 0x405000  
gef➤ dump binary memory region_rw.bin   0x405000 0x406000  
```

---

## Étape 2 — Reconstruction manuelle avec un éditeur hexadécimal

Cette approche est fastidieuse mais formatrice. Elle permet de comprendre précisément chaque octet du header ELF. On l'utilisera ici pour reconstruire un ELF minimal, puis on automatisera avec Python.

### 2a — Assembler un fichier plat

On commence par concaténer les dumps dans l'ordre des adresses virtuelles :

```
$ cat region_hdr.bin region_text.bin region_ro.bin region_rw.bin > flat_dump.bin
$ ls -la flat_dump.bin
-rw-r--r-- 1 user user 24576 ... flat_dump.bin
```

Le fichier fait `0x6000` octets (6 pages de 4 Ko). La bonne nouvelle est que `region_hdr.bin` contient potentiellement déjà un ELF header — celui que le stub UPX a laissé en mémoire. Vérifions :

```
$ xxd flat_dump.bin | head -4
00000000: 7f45 4c46 0201 0103 0000 0000 0000 0000  .ELF............
00000010: 0200 3e00 0100 0000 e813 4000 0000 0000  ..>.......@.....
00000020: 4000 0000 0000 0000 0000 0000 0000 0000  @...............
00000030: 0000 0000 4000 3800 0300 0000 0000 0000  ....@.8.........
```

Le magic `\x7fELF` est présent. Cependant, ce header reflète l'état du binaire **packé** — le point d'entrée (`e_entry`) pointe vers le stub, pas vers l'OEP, et la PHT décrit les segments du fichier packé, pas du code décompressé. Il faut corriger plusieurs champs.

### 2b — Corriger le ELF header

Ouvrons `flat_dump.bin` dans ImHex. On peut charger le pattern `elf_header.hexpat` du dépôt (chapitre 6, section 6.4) pour visualiser les champs. Les corrections à appliquer sont les suivantes.

**`e_entry` (offset `0x18`, 8 octets, little-endian)** — Le point d'entrée. On remplace la valeur actuelle (adresse du stub) par l'OEP observé dans GDB. Si l'OEP est `0x401000` :

```
Offset 0x18 : 00 10 40 00 00 00 00 00
```

**`e_phoff` (offset `0x20`, 8 octets)** — L'offset de la program header table dans le fichier. Habituellement situé juste après le ELF header, à l'offset `0x40` (64 en décimal, soit la taille du ELF header en 64 bits). On vérifie que la valeur est correcte ; si le fichier packé a déplacé la PHT, on la corrige.

**`e_shoff` (offset `0x28`, 8 octets)** — L'offset de la section header table. Sur un binaire packé par UPX, ce champ vaut souvent `0` (table absente). On le laissera à `0` pour l'instant et on recréera la SHT à l'étape 3.

**`e_phnum` (offset `0x38`, 2 octets)** — Le nombre d'entrées dans la PHT. On le mettra à jour après avoir reconstruit les program headers.

**`e_shnum` (offset `0x3C`, 2 octets)** — Le nombre d'entrées dans la SHT. On le fixe à `0` si l'on ne reconstruit pas la SHT, ou au nombre de sections créées sinon.

### 2c — Reconstruire la program header table

La PHT doit décrire les segments tels qu'ils existent dans notre fichier reconstruit. Chaque entrée `Phdr` fait 56 octets en ELF 64 bits et contient les champs suivants :

```
Offset  Taille  Champ       Description
0x00    4       p_type      Type de segment (1 = PT_LOAD)
0x04    4       p_flags     Permissions (PF_R=4, PF_W=2, PF_X=1)
0x08    8       p_offset    Offset dans le fichier
0x10    8       p_vaddr     Adresse virtuelle
0x18    8       p_paddr     Adresse physique (= p_vaddr en pratique)
0x20    8       p_filesz    Taille dans le fichier
0x28    8       p_memsz     Taille en mémoire
0x30    8       p_align     Alignement (typiquement 0x1000)
```

Pour notre binaire, on crée trois segments `PT_LOAD` correspondant aux trois régions utiles (on peut fusionner le header et le code en un segment si l'on préfère, ou les séparer pour plus de précision) :

**Segment 1 — Code (`r-x`)** :
- `p_type` = `1` (`PT_LOAD`)  
- `p_flags` = `5` (`PF_R | PF_X`)  
- `p_offset` = `0x0000` (début du fichier — inclut le header)  
- `p_vaddr` = `0x400000`  
- `p_filesz` = `0x4000`  
- `p_memsz` = `0x4000`  
- `p_align` = `0x1000`

**Segment 2 — Données lecture seule (`r--`)** :
- `p_flags` = `4` (`PF_R`)  
- `p_offset` = `0x4000`  
- `p_vaddr` = `0x404000`  
- `p_filesz` = `0x1000`  
- `p_memsz` = `0x1000`

**Segment 3 — Données lecture-écriture (`rw-`)** :
- `p_flags` = `6` (`PF_R | PF_W`)  
- `p_offset` = `0x5000`  
- `p_vaddr` = `0x405000`  
- `p_filesz` = `0x1000`  
- `p_memsz` = `0x2000` (plus grand que `p_filesz` si `.bss` existe — la différence sera remplie de zéros par le loader)

On écrit ces entrées à l'offset `0x40` dans le fichier (juste après le ELF header) et on met à jour `e_phnum` = `3`.

---

## Étape 3 — Recréer une section header table (optionnel mais recommandé)

La SHT n'est pas nécessaire à l'exécution mais elle transforme radicalement la qualité de l'analyse dans Ghidra. Sans SHT, Ghidra traite tout le segment `r-x` comme un bloc monolithique de code. Avec une SHT, il peut distinguer `.text` de `.init` et de `.fini`, identifier `.rodata` séparément, et traiter `.bss` correctement.

### Principe

La SHT est un tableau de structures `Shdr` (64 octets chacune en ELF 64 bits) placé quelque part dans le fichier (souvent à la fin). Chaque entrée décrit une section avec son nom, son type, ses flags, son adresse virtuelle, son offset dans le fichier et sa taille.

Les noms des sections sont stockés dans une section spéciale de type `SHT_STRTAB` appelée `.shstrtab`, référencée par le champ `e_shstrndx` du ELF header.

### Sections minimales à recréer

Pour une analyse correcte dans Ghidra, on recommande de recréer au minimum les sections suivantes :

| Section     | Type          | Flags       | Adresse    | Contenu                          |  
|-------------|---------------|-------------|------------|----------------------------------|  
| *(null)*    | `SHT_NULL`    | —           | `0`        | Entrée obligatoire (index 0)     |  
| `.text`     | `SHT_PROGBITS`| `AX`        | `0x401000` | Code exécutable principal        |  
| `.rodata`   | `SHT_PROGBITS`| `A`         | `0x404000` | Données en lecture seule         |  
| `.data`     | `SHT_PROGBITS`| `WA`        | `0x405000` | Données initialisées             |  
| `.bss`      | `SHT_NOBITS`  | `WA`        | `0x406000` | Données non initialisées         |  
| `.shstrtab` | `SHT_STRTAB`  | —           | —          | Table des noms de sections       |

Les flags sont les suivants : `A` = `SHF_ALLOC` (la section occupe de la mémoire à l'exécution), `W` = `SHF_WRITE`, `X` = `SHF_EXECINSTR`.

La construction de la `.shstrtab` consiste simplement à concaténer les noms de sections séparés par des octets nuls :

```
\0.text\0.rodata\0.data\0.bss\0.shstrtab\0
```

Chaque entrée `Shdr` référence le nom de sa section par un offset dans cette table de chaînes.

### Emplacement de la SHT dans le fichier

On ajoute la `.shstrtab` et la SHT à la fin du fichier :

1. Écrire la `.shstrtab` à la fin du fichier actuel. Noter son offset.  
2. Écrire les 6 entrées `Shdr` (6 × 64 = 384 octets) immédiatement après. Noter l'offset de début.  
3. Mettre à jour le ELF header : `e_shoff` = offset de la SHT, `e_shnum` = 6, `e_shstrndx` = 5 (index de `.shstrtab`).

---

## Étape 4 — Reconstruction automatisée avec `lief` (Python)

La reconstruction manuelle est un exercice formateur, mais en situation réelle on utilise un script. La bibliothèque **LIEF** (Library to Instrument Executable Formats, vue au chapitre 35) permet de manipuler les structures ELF de manière programmatique.

Voici la logique du script de reconstruction. On crée un ELF vide, on y insère les segments et les sections à partir des dumps mémoire, et on fixe le point d'entrée :

```python
import lief

# --- Paramètres extraits de GDB ---
OEP        = 0x401000  
BASE_ADDR  = 0x400000  

regions = [
    # (fichier_dump,  vaddr,     perm,         section_name)
    ("region_hdr.bin",  0x400000, "r--",        None),
    ("region_text.bin", 0x401000, "r-x",        ".text"),
    ("region_ro.bin",   0x404000, "r--",        ".rodata"),
    ("region_rw.bin",   0x405000, "rw-",        ".data"),
]

# --- Créer le binaire ELF ---
elf = lief.ELF.Binary("reconstructed", lief.ELF.ELF_CLASS.CLASS64)  
elf.header.entrypoint = OEP  
elf.header.file_type  = lief.ELF.E_TYPE.EXECUTABLE  

for dump_path, vaddr, perms, sec_name in regions:
    data = open(dump_path, "rb").read()

    # Créer le segment PT_LOAD
    seg = lief.ELF.Segment()
    seg.type            = lief.ELF.SEGMENT_TYPES.LOAD
    seg.flags           = 0
    if "r" in perms: seg.flags |= lief.ELF.SEGMENT_FLAGS.R
    if "w" in perms: seg.flags |= lief.ELF.SEGMENT_FLAGS.W
    if "x" in perms: seg.flags |= lief.ELF.SEGMENT_FLAGS.X
    seg.virtual_address = vaddr
    seg.physical_address= vaddr
    seg.alignment       = 0x1000
    seg.content         = list(data)
    elf.add(seg)

    # Créer la section correspondante
    if sec_name:
        sec = lief.ELF.Section(sec_name)
        sec.content         = list(data)
        sec.virtual_address = vaddr
        sec.alignment       = 0x10

        if "x" in perms:
            sec.type  = lief.ELF.SECTION_TYPES.PROGBITS
            sec.flags = (lief.ELF.SECTION_FLAGS.ALLOC |
                         lief.ELF.SECTION_FLAGS.EXECINSTR)
        elif "w" in perms:
            sec.type  = lief.ELF.SECTION_TYPES.PROGBITS
            sec.flags = (lief.ELF.SECTION_FLAGS.ALLOC |
                         lief.ELF.SECTION_FLAGS.WRITE)
        else:
            sec.type  = lief.ELF.SECTION_TYPES.PROGBITS
            sec.flags = lief.ELF.SECTION_FLAGS.ALLOC

        elf.add(sec, loaded=True)

# --- Écrire le résultat ---
elf.write("packed_sample_reconstructed")  
print("[+] ELF reconstruit : packed_sample_reconstructed")  
```

> ⚠️ **Avertissement** — LIEF prend en charge la cohérence interne du fichier ELF (offsets, alignements, tables de chaînes), ce qui élimine la majorité des erreurs de calcul manuel. Cependant, le résultat peut différer légèrement du binaire original selon la version de LIEF et les heuristiques d'alignement utilisées. Le but n'est pas de reproduire le binaire original octet pour octet, mais d'obtenir un ELF suffisamment correct pour l'analyse statique.

### Alternative : `pyelftools` en mode écriture

La bibliothèque `pyelftools` est principalement conçue pour la lecture, pas l'écriture. Pour la reconstruction, LIEF est le choix recommandé. Si l'on préfère rester dans un écosystème minimal, on peut aussi construire le fichier ELF octet par octet avec le module `struct` de Python, en encodant manuellement chaque champ des headers — c'est essentiellement l'automatisation de la procédure manuelle décrite à l'étape 2, mais en script plutôt qu'en éditeur hexadécimal.

---

## Étape 5 — Validation de l'ELF reconstruit

Une fois le fichier produit, on effectue une série de vérifications pour s'assurer de sa validité.

### Vérification structurelle avec `readelf`

```
$ readelf -h packed_sample_reconstructed
```

On vérifie que le magic est correct, que le type est `EXEC`, que l'architecture est `Advanced Micro Devices X86-64`, que le point d'entrée correspond à l'OEP, et que les offsets de la PHT et de la SHT sont cohérents.

```
$ readelf -l packed_sample_reconstructed
```

On vérifie que les segments `LOAD` couvrent les bonnes plages d'adresses avec les bonnes permissions.

```
$ readelf -S packed_sample_reconstructed
```

On vérifie la présence des sections `.text`, `.rodata`, `.data` avec les bons types et flags.

### Vérification du contenu avec `strings` et `objdump`

```
$ strings packed_sample_reconstructed | grep FLAG
FLAG{unp4ck3d_and_r3c0nstruct3d}

$ objdump -d packed_sample_reconstructed | head -30
```

`objdump -d` doit produire un désassemblage cohérent à partir de l'adresse `0x401000`, avec des fonctions reconnaissables (prologues, appels à des adresses de la PLT, etc.).

### Test d'exécution (optionnel, en sandbox)

Si l'on veut vérifier que le binaire reconstruit est fonctionnel, on peut tenter de l'exécuter dans la VM sandboxée. Cela ne fonctionnera que si les imports dynamiques sont correctement résolus, ce qui n'est pas garanti après une reconstruction par dump mémoire. L'objectif premier de la reconstruction est l'**analysabilité statique**, pas la ré-exécutabilité.

### Import dans Ghidra

Le test final est l'import dans Ghidra. On ouvre le fichier reconstruit avec les paramètres par défaut et on lance l'auto-analyse. Les points à vérifier sont les suivants : Ghidra reconnaît-il correctement le point d'entrée ? Identifie-t-il la fonction `main` ? Les chaînes de `.rodata` apparaissent-elles dans le Listing et le Decompiler ? Les cross-references entre le code et les données sont-elles résolues ?

Si Ghidra produit un décompilé lisible des fonctions du programme (on devrait reconnaître `check_license_key`, `xor_decode`, `compute_checksum`…), la reconstruction est réussie.

---

## Cas particuliers et difficultés courantes

### Le point d'entrée ne pointe pas vers `main`

Dans un binaire GCC classique, `e_entry` pointe vers `_start`, qui appelle `__libc_start_main`, qui appelle `main`. Le code CRT (`_start`, `__libc_csu_init`, `__libc_csu_fini`) est normalement inclus dans la section `.text`. Si le dump mémoire ne couvre pas cette zone (par exemple parce que le CRT a été fourni par le stub et non par le programme original), le point d'entrée reconstruit peut ne pas correspondre à `_start`. Dans ce cas, on pointe `e_entry` directement vers `main` (identifiable par ses appels à `printf`, `fgets`, etc.) et on accepte que le binaire ne soit pas réellement exécutable mais parfaitement analysable.

### Imports dynamiques et PLT/GOT

Si le programme original utilisait des bibliothèques partagées (ce qui est le cas de notre `packed_sample`), le code décompressé contient des appels `call` vers des entrées PLT qui redirigent vers la GOT. En mémoire au moment du dump, la GOT peut déjà contenir les adresses résolues (si le lazy binding a été effectué) ou encore les adresses de la routine de résolution PLT.

La reconstruction fidèle de la PLT/GOT et des sections `.dynamic`, `.dynsym`, `.dynstr` est un travail considérable qui dépasse le cadre de cette section. En pratique, on a deux options. La première est de s'en passer pour l'analyse statique : Ghidra verra les `call` vers des adresses fixes et l'analyste pourra les annoter manuellement en identifiant les fonctions libc concernées (par les constantes passées en argument, le nombre de paramètres, etc.). La seconde est de récupérer ces sections depuis le binaire packé lui-même — certains packers (dont UPX) laissent les sections `.dynamic` et `.dynsym` relativement intactes dans le fichier sur disque, car le stub en a besoin pour la résolution post-décompression.

### Binaires PIE (Position-Independent Executable)

Si le binaire original a été compilé avec `-pie`, toutes les adresses dans le dump mémoire sont relatives à une base choisie aléatoirement par le loader (ASLR). Le ELF reconstruit devra utiliser le type `ET_DYN` (et non `ET_EXEC`) et les adresses virtuelles devront être ajustées en soustrayant la base de chargement. On peut retrouver cette base en comparant l'adresse de `_start` dans le dump avec l'offset de `_start` dans le fichier packé (qui est un offset relatif à la base).

Dans notre cas, le Makefile compile avec `-no-pie`, ce qui simplifie considérablement la reconstruction : les adresses virtuelles dans le dump correspondent directement aux adresses dans le fichier ELF.

### Sections `.bss` — données non initialisées

La section `.bss` n'occupe pas d'espace dans le fichier ELF (sa taille sur disque est nulle) mais réserve de l'espace en mémoire (rempli de zéros par le loader). En mémoire, `.bss` se trouve immédiatement après `.data`, dans la même page `rw-`. Le dump mémoire de la région `rw-` contient donc à la fois `.data` et `.bss`.

Pour séparer les deux dans l'ELF reconstruit, il faut déterminer où se termine `.data` et où commence `.bss`. Sans symboles ni SHT originale, la frontière est impossible à déterminer avec certitude. L'heuristique courante est de chercher une longue séquence de zéros en fin de région `rw-` — ces zéros sont probablement le `.bss`. En cas de doute, on peut simplement inclure toute la région dans `.data` (taille fichier = taille mémoire, pas de `.bss` explicite). L'analyse statique n'en souffrira pas de manière significative.

---

> 📌 **Point clé à retenir** — La reconstruction d'un ELF à partir de dumps mémoire est un processus **itératif**. On commence par un ELF minimal (header + un seul segment `LOAD`), on l'importe dans Ghidra, on évalue la qualité de l'analyse, puis on affine en ajoutant des sections et en corrigeant les métadonnées. La perfection n'est pas l'objectif : un ELF « suffisamment bon » pour que Ghidra produise un décompilé lisible est un ELF réussi.

⏭️ [Réanalyser le binaire unpacké](/29-unpacking/04-reanalyser-binaire.md)
