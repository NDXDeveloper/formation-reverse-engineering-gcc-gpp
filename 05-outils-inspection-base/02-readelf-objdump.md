🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 5.2 — `readelf` et `objdump` — anatomie d'un ELF (headers, sections, segments)

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Introduction

À la section précédente, `file` nous a appris que notre binaire est un ELF 64 bits x86-64, et `xxd` nous a permis de lire manuellement les premiers octets du header. Mais parser un ELF à la main, octet par octet, serait un exercice de patience déraisonnable. C'est là qu'interviennent `readelf` et `objdump` — deux outils des GNU Binutils qui savent **parser la structure interne d'un ELF** et en présenter le contenu de manière lisible.

Ces deux outils se recouvrent partiellement, mais ont des philosophies différentes :

- **`readelf`** est un outil de **dissection structurelle**. Il affiche les headers, les sections, les segments, les tables de symboles, les relocations, les notes, les informations DWARF — bref, toute la **métadonnée** d'un ELF. Il ne désassemble pas le code.  
- **`objdump`** est un outil plus polyvalent qui peut **à la fois** afficher les métadonnées ELF et **désassembler** le code machine. Son volet désassemblage sera approfondi au chapitre 7 ; ici, nous nous concentrons sur ses capacités d'inspection structurelle, en complément de `readelf`.

Avant de plonger dans les commandes, rappelons brièvement l'architecture d'un fichier ELF, introduite au chapitre 2.

---

## Rappel : la double vue d'un fichier ELF

Un fichier ELF peut être lu selon deux perspectives complémentaires, chacune servant un objectif différent :

**La vue "linking" (sections)** est utilisée par le linker (`ld`) au moment de la compilation. Elle découpe le fichier en **sections** nommées (`.text`, `.data`, `.rodata`, `.bss`, `.symtab`, `.strtab`…), chacune ayant un rôle précis. Cette vue est décrite par le **Section Header Table** (SHT), situé en fin de fichier.

**La vue "execution" (segments)** est utilisée par le loader (`ld.so`) au moment du chargement en mémoire. Elle regroupe les sections en **segments** (aussi appelés *program headers*) qui définissent les régions à mapper en mémoire avec leurs permissions (lecture, écriture, exécution). Cette vue est décrite par le **Program Header Table** (PHT), situé juste après le ELF header.

Un segment contient généralement plusieurs sections. Par exemple, un segment `LOAD` avec les permissions `R+X` (lisible et exécutable) contiendra typiquement les sections `.text`, `.plt`, `.init` et `.fini`. Un segment `LOAD` avec les permissions `R+W` (lisible et inscriptible) contiendra `.data`, `.bss` et `.got`.

Le **ELF header** est le point de départ absolu. Situé aux 64 premiers octets du fichier (en ELF 64 bits), il contient les informations fondamentales et les offsets vers les deux tables de headers (PHT et SHT).

`readelf` et `objdump` permettent d'explorer ces trois niveaux : ELF header, program headers (segments) et section headers (sections).

---

## `readelf` — le scalpel de l'analyse ELF

`readelf` fait partie des GNU Binutils. Contrairement à `objdump`, il ne dépend pas de la bibliothèque BFD (*Binary File Descriptor*) et parse directement le format ELF. Cela le rend plus fiable pour les ELF malformés ou non-standard.

### Le ELF header : `-h`

```bash
$ readelf -h keygenme_O0
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Position-Independent Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0x10c0
  Start of program headers:          64 (bytes into file)
  Start of section headers:          14808 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         13
  Size of section headers:           64 (bytes)
  Number of section headers:         31
  Section header string table index: 30
```

C'est exactement l'information que nous avions extraite manuellement avec `xxd` à la section 5.1, mais présentée de manière lisible. Passons en revue les champs les plus importants pour le RE :

**`Entry point address: 0x10c0`** — c'est l'adresse de la première instruction exécutée par le programme. Sur un binaire compilé avec GCC, ce n'est **pas** `main()`. C'est `_start`, un petit stub fourni par la libc (via `crt1.o`) qui initialise l'environnement, puis appelle `__libc_start_main`, qui à son tour appelle `main()`. Connaître l'entry point permet de poser un premier breakpoint dans GDB avant même d'avoir identifié `main`.

**`Type: DYN`** — le type `DYN` (shared object) est utilisé à la fois pour les bibliothèques partagées `.so` et pour les exécutables PIE. La distinction se fait par la présence d'un point d'entrée non nul et d'un `INTERP` segment. Avant GCC 8/9, les exécutables classiques avaient le type `EXEC` ; aujourd'hui, `-pie` est activé par défaut et le type est `DYN`.

**`Start of program headers: 64`** et **`Start of section headers: 14808`** — les offsets dans le fichier vers les deux tables de headers. Le PHT commence immédiatement après le ELF header (64 octets = taille du ELF header en 64 bits). Le SHT est en fin de fichier.

**`Number of section headers: 31`** — le binaire contient 31 sections. Un binaire strippé en aura beaucoup moins (les sections `.symtab`, `.strtab`, `.debug_*` seront absentes).

**`Section header string table index: 30`** — l'index de la section qui contient les noms des sections eux-mêmes (`.shstrtab`). C'est grâce à cette section que `readelf` peut afficher `.text` au lieu de « section numéro 14 ».

### Les sections : `-S`

```bash
$ readelf -S keygenme_O0
There are 31 section headers, starting at offset 0x39d8:

Section Headers:
  [Nr] Name              Type             Address           Offset    Size              EntSize          Flags  Link  Info  Align
  [ 0]                   NULL             0000000000000000  00000000  0000000000000000  0000000000000000           0     0     0
  [ 1] .interp           PROGBITS         0000000000000318  00000318  000000000000001c  0000000000000000   A       0     0     1
  [ 2] .note.gnu.pr[...] NOTE             0000000000000338  00000338  0000000000000030  0000000000000000   A       0     0     8
  [ 3] .note.gnu.bu[...] NOTE             0000000000000368  00000368  0000000000000024  0000000000000000   A       0     0     4
  [ 4] .note.ABI-tag     NOTE             000000000000038c  0000038c  0000000000000020  0000000000000000   A       0     0     4
  [ 5] .gnu.hash         GNU_HASH         00000000000003b0  000003b0  0000000000000024  0000000000000000   A       6     0     8
  [ 6] .dynsym           DYNSYM           00000000000003d8  000003d8  00000000000000f0  0000000000000018   A       7     1     8
  [ 7] .dynstr           STRTAB           00000000000004c8  000004c8  00000000000000ad  0000000000000000   A       0     0     1
  [...]
  [14] .text             PROGBITS         00000000000010c0  000010c0  0000000000000225  0000000000000000  AX       0     0    16
  [15] .fini             PROGBITS         00000000000012e8  000012e8  000000000000000d  0000000000000000  AX       0     0     4
  [16] .rodata           PROGBITS         0000000000002000  00002000  00000000000000f5  0000000000000000   A       0     0     8
  [...]
  [23] .data             PROGBITS         0000000000004000  00003000  0000000000000010  0000000000000000  WA       0     0     8
  [24] .bss              NOBITS           0000000000004010  00003010  0000000000000008  0000000000000000  WA       0     0     1
  [25] .comment          PROGBITS         0000000000000000  00003010  000000000000002c  0000000000000001  MS       0     0     1
  [26] .symtab           SYMTAB           0000000000000000  00003040  0000000000000408  0000000000000018          27    18     8
  [27] .strtab           STRTAB           0000000000000000  00003448  0000000000000242  0000000000000000           0     0     1
  [...]
```

La sortie est dense, mais chaque colonne a un sens précis :

**`Name`** — le nom de la section. Les sections essentielles pour le RE ont été présentées au chapitre 2 (section 2.4). Rappelons les plus importantes :

| Section | Contenu | Intérêt pour le RE |  
|---|---|---|  
| `.text` | Le code machine exécutable | C'est ici que se trouvent les instructions à désassembler. |  
| `.rodata` | Les données en lecture seule (chaînes, constantes) | Les chaînes trouvées par `strings` résident principalement ici. |  
| `.data` | Les variables globales et statiques initialisées | Peut contenir des clés, des tables de lookup, des configurations. |  
| `.bss` | Les variables globales non initialisées | Réservée en mémoire mais n'occupe pas d'espace dans le fichier. |  
| `.plt` / `.got` | Procédure Linkage Table / Global Offset Table | Mécanisme de résolution des appels à des fonctions de bibliothèques partagées (voir chapitre 2, section 2.9). |  
| `.symtab` | Table complète des symboles | Noms de toutes les fonctions et variables — disparaît après `strip`. |  
| `.dynsym` | Table des symboles dynamiques | Noms des fonctions importées/exportées — survit au `strip`. |  
| `.dynstr` | Table des chaînes dynamiques | Les noms référencés par `.dynsym`. |  
| `.strtab` | Table des chaînes de symboles | Les noms référencés par `.symtab` — disparaît après `strip`. |  
| `.comment` | Commentaires du compilateur | Contient la version de GCC (celle que `strings` avait trouvée). |

**`Type`** — le type de la section. `PROGBITS` signifie que la section contient des données définies par le programme (code ou données). `NOBITS` signifie que la section occupe de l'espace en mémoire mais pas dans le fichier (typique de `.bss`). `DYNSYM` et `SYMTAB` sont des tables de symboles. `STRTAB` est une table de chaînes.

**`Address`** — l'adresse virtuelle à laquelle la section sera chargée en mémoire. Pour un binaire PIE, ces adresses sont relatives et seront décalées par l'ASLR au chargement.

**`Offset`** — la position de la section dans le fichier sur disque. C'est cet offset que vous utiliseriez avec `xxd -s` pour examiner le contenu brut de la section.

**`Size`** — la taille de la section en octets.

**`Flags`** — les attributs de la section. Les flags les plus courants sont :

| Flag | Signification |  
|---|---|  
| `A` | **Alloc** — la section occupe de l'espace en mémoire au runtime. |  
| `X` | **eXecute** — la section contient du code exécutable. |  
| `W` | **Write** — la section est inscriptible en mémoire. |  
| `S` | **Strings** — la section contient des chaînes terminées par un octet nul. |  
| `M` | **Merge** — la section peut être fusionnée pour éliminer les doublons. |

La combinaison des flags révèle la nature de la section. `AX` (alloc + execute) = code exécutable (`.text`). `A` seul = données en lecture seule (`.rodata`). `WA` (write + alloc) = données inscriptibles (`.data`, `.bss`, `.got`).

### Comparer les sections d'un binaire normal et d'un binaire strippé

```bash
$ readelf -S keygenme_O0 | grep -c '\['
32
$ readelf -S keygenme_O2_strip | grep -c '\['
29
```

Le binaire strippé a moins de sections. Les sections qui disparaissent sont principalement `.symtab` et `.strtab` — les tables de symboles non dynamiques. On peut le vérifier :

```bash
$ readelf -S keygenme_O0 | grep -E '\.symtab|\.strtab'
  [26] .symtab           SYMTAB           [...]
  [27] .strtab           STRTAB           [...]

$ readelf -S keygenme_O2_strip | grep -E '\.symtab|\.strtab'
(aucun résultat)
```

C'est la confirmation structurelle de ce que `file` nous avait annoncé avec `stripped` vs `not stripped`. En revanche, `.dynsym` et `.dynstr` sont toujours présentes, car elles sont nécessaires au runtime pour la résolution des bibliothèques partagées. C'est pourquoi, même sur un binaire strippé, on peut encore voir les noms des fonctions importées comme `printf` ou `strcmp`.

### Les segments (program headers) : `-l`

```bash
$ readelf -l keygenme_O0

Elf file type is DYN (Position-Independent Executable file)  
Entry point 0x10c0  
There are 13 program headers, starting at offset 64  

Program Headers:
  Type           Offset             VirtAddr           PhysAddr           FileSiz            MemSiz              Flags  Align
  PHDR           0x0000000000000040 0x0000000000000040 0x0000000000000040 0x00000000000002d8 0x00000000000002d8  R      0x8
  INTERP         0x0000000000000318 0x0000000000000318 0x0000000000000318 0x000000000000001c 0x000000000000001c  R      0x1
      [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
  LOAD           0x0000000000000000 0x0000000000000000 0x0000000000000000 0x0000000000000628 0x0000000000000628  R      0x1000
  LOAD           0x0000000000001000 0x0000000000001000 0x0000000000001000 0x00000000000002f5 0x00000000000002f5  R E    0x1000
  LOAD           0x0000000000002000 0x0000000000002000 0x0000000000002000 0x0000000000000174 0x0000000000000174  R      0x1000
  LOAD           0x0000000000002db8 0x0000000000003db8 0x0000000000003db8 0x0000000000000258 0x0000000000000260  RW     0x1000
  DYNAMIC        0x0000000000002dc8 0x0000000000003dc8 0x0000000000003dc8 0x00000000000001f0 0x00000000000001f0  RW     0x8
  NOTE           0x0000000000000338 0x0000000000000338 0x0000000000000338 0x0000000000000030 0x0000000000000030  R      0x8
  [...]
  GNU_STACK      0x0000000000000000 0x0000000000000000 0x0000000000000000 0x0000000000000000 0x0000000000000000  RW     0x10
  GNU_RELRO      0x0000000000002db8 0x0000000000003db8 0x0000000000003db8 0x0000000000000248 0x0000000000000248  R      0x1

 Section to Segment mapping:
  Segment Sections...
   00
   01     .interp
   02     .interp .note.gnu.property .note.gnu.build-id .note.ABI-tag .gnu.hash .dynsym .dynstr .gnu.version .gnu.version_r .rela.dyn .rela.plt
   03     .init .plt .plt.got .plt.sec .text .fini
   04     .rodata .eh_frame_hdr .eh_frame
   05     .init_array .fini_array .dynamic .got .data .bss
   06     .dynamic
   [...]
```

La sortie se décompose en deux parties. La première liste les **program headers** (segments), la seconde montre le **mapping sections → segments**.

Les types de segments les plus importants :

**`PHDR`** — décrit le program header table lui-même. Nécessaire pour que le loader sache où trouver les autres segments.

**`INTERP`** — contient le chemin du loader dynamique (`/lib64/ld-linux-x86-64.so.2`). Ce segment n'existe que dans les exécutables dynamiquement liés. Son absence indiquerait un binaire statiquement lié.

**`LOAD`** — ce sont les segments effectivement chargés en mémoire par le loader. C'est ici que la colonne **Flags** est cruciale :

| Flags | Permissions | Contenu typique |  
|---|---|---|  
| `R` | Lecture seule | Headers ELF, métadonnées |  
| `R E` | Lecture + exécution | Code (`.text`, `.plt`, `.init`, `.fini`) |  
| `R` (second) | Lecture seule | Données constantes (`.rodata`, `.eh_frame`) |  
| `RW` | Lecture + écriture | Données modifiables (`.data`, `.bss`, `.got`) |

Remarquez qu'aucun segment n'est à la fois inscriptible **et** exécutable (`RWE`). C'est la protection **NX** (No-eXecute) en action : le code n'est pas modifiable, les données ne sont pas exécutables. Cette séparation complique considérablement l'exploitation de vulnérabilités comme les buffer overflows (voir chapitre 19).

**`DYNAMIC`** — contient la structure `dynamic` qui liste les bibliothèques partagées nécessaires, les offsets des tables de symboles dynamiques, les informations de relocation, etc. C'est la « table des matières » pour le loader dynamique.

**`GNU_STACK`** — indique les permissions de la pile. Le flag `RW` (sans `E`) confirme que la pile n'est pas exécutable, une autre couche de la protection NX.

**`GNU_RELRO`** — marque une région de mémoire qui sera rendue en lecture seule après les relocations initiales. C'est le mécanisme RELRO (Relocation Read-Only). Nous y reviendrons à la section 5.6 avec `checksec`.

Le **mapping sections → segments** en bas de la sortie est particulièrement instructif. Il montre comment les sections sont regroupées. Le segment 03, avec les flags `R E`, contient `.init`, `.plt`, `.plt.got`, `.plt.sec`, `.text` et `.fini` — tout le code exécutable. Le segment 05, avec les flags `RW`, contient `.init_array`, `.fini_array`, `.dynamic`, `.got`, `.data` et `.bss` — toutes les données modifiables.

### Les informations dynamiques : `-d`

```bash
$ readelf -d keygenme_O0

Dynamic section at offset 0x2dc8 contains 27 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000c (INIT)               0x1000
 0x000000000000000d (FINI)               0x12e8
 0x0000000000000019 (INIT_ARRAY)         0x3db8
 0x000000000000001b (INIT_ARRAYSZ)       8 (bytes)
 0x000000000000001a (FINI_ARRAY)         0x3dc0
 0x000000000000001c (FINI_ARRAYSZ)       8 (bytes)
 0x000000006ffffef5 (GNU_HASH)           0x3b0
 0x0000000000000005 (STRTAB)             0x4c8
 0x0000000000000006 (SYMTAB)             0x3d8
 0x000000000000000a (STRSZ)              173 (bytes)
 0x000000000000000b (SYMENT)             24 (bytes)
 0x0000000000000015 (DEBUG)              0x0
 0x0000000000000003 (PLTGOT)             0x3fb8
 0x0000000000000002 (PLTRELSZ)           72 (bytes)
 0x0000000000000014 (PLTREL)             RELA
 0x0000000000000017 (JMPREL)             0x5d8
 [...]
 0x000000006ffffff0 (VERSYM)             0x576
 0x000000006ffffffe (VERNEED)            0x598
 0x000000006fffffff (VERNEEDNUM)         1
 0x0000000000000000 (NULL)               0x0
```

L'entrée la plus importante ici est **`NEEDED`** : elle indique que le binaire dépend de `libc.so.6`. C'est la bibliothèque C standard. Un binaire avec de nombreuses entrées `NEEDED` utilise plusieurs bibliothèques partagées — chacune est un indice sur les fonctionnalités du programme (réseau, crypto, GUI, etc.).

Les entrées `PLTGOT`, `JMPREL` et `PLTRELSZ` fournissent les adresses et tailles des tables PLT/GOT, essentielles pour comprendre le mécanisme de résolution dynamique (chapitre 2, section 2.9).

### Les notes ELF : `-n`

```bash
$ readelf -n keygenme_O0

Displaying notes found in: .note.gnu.property
  Owner                Data size    Description
  GNU                  0x00000020   NT_GNU_PROPERTY_TYPE_0
      Properties: x86 feature: IBT, SHSTK

Displaying notes found in: .note.gnu.build-id
  Owner                Data size    Description
  GNU                  0x00000014   NT_GNU_BUILD_ID (unique build ID bitstring)
    Build ID: a3f5...c4e2

Displaying notes found in: .note.ABI-tag
  Owner                Data size    Description
  GNU                  0x00000010   NT_GNU_ABI_TAG (ABI version tag)
    OS: Linux, ABI: 3.2.0
```

Les notes contiennent des métadonnées utiles. Le **Build ID** est un hash unique identifiant cette compilation précise — il permet de retrouver les symboles de débogage correspondants dans un serveur de debug symbols. Les **propriétés x86** (`IBT`, `SHSTK`) indiquent que le binaire a été compilé avec le support d'Intel CET (Control-flow Enforcement Technology), une protection matérielle contre les attaques ROP.

### Récapitulatif des options `readelf` essentielles

| Option | Affiche | Usage typique |  
|---|---|---|  
| `-h` | ELF header | Architecture, type, entry point, tailles des headers |  
| `-S` | Section headers | Liste des sections, leurs flags, offsets et tailles |  
| `-l` | Program headers (segments) | Mapping mémoire, permissions, vue du loader |  
| `-s` | Tables de symboles | Noms de fonctions et variables (voir section 5.3) |  
| `-d` | Section `.dynamic` | Dépendances, adresses PLT/GOT |  
| `-r` | Relocations | Entrées de relocation (utile pour comprendre PLT/GOT) |  
| `-n` | Notes | Build ID, ABI tag, propriétés matérielles |  
| `-a` | Tout | Équivalent de toutes les options combinées |  
| `-W` | Mode large (wide) | Évite la troncature des lignes longues |  
| `-x <section>` | Dump hex d'une section | Examiner le contenu brut d'une section spécifique |

L'option `-W` mérite une mention particulière : sans elle, `readelf` tronque les lignes longues pour tenir dans un terminal de 80 colonnes, ce qui rend la sortie illisible sur les champs d'adresse 64 bits. Prenez l'habitude de toujours utiliser `-W` :

```bash
$ readelf -SW keygenme_O0
```

### Dumper le contenu brut d'une section

`readelf -x` permet de dumper une section spécifique en hexadécimal, une alternative ciblée à `xxd` :

```bash
$ readelf -x .rodata keygenme_O0

Hex dump of section '.rodata':
  0x00002000 01000200 00000000 456e7465 7220796f ........Enter yo
  0x00002010 7572206c 6963656e 7365206b 65793a20 ur license key:
  0x00002020 00496e76 616c6964 206b6579 20666f72 .Invalid key for
  0x00002030 6d61742e 20457870 65637465 643a2058 mat. Expected: X
  [...]
```

On retrouve dans `.rodata` les chaînes que `strings` avait détectées. Cette fois, on les voit **dans leur contexte structurel** — elles appartiennent à la section des données en lecture seule, ce qui confirme qu'elles sont des constantes compilées, pas des artefacts aléatoires.

---

## `objdump` — la perspective complémentaire

`objdump` peut afficher une grande partie des informations que `readelf` fournit, mais avec une présentation et une granularité parfois différentes. Son point fort est le **désassemblage** (chapitre 7), mais ses capacités d'inspection structurelle méritent d'être connues.

### Headers du fichier : `-f`

```bash
$ objdump -f keygenme_O0

keygenme_O0:     file format elf64-x86-64  
architecture: i386:x86-64, flags 0x00000150:  
HAS_SYMS, DYNAMIC, D_PAGED  
start address 0x00000000000010c0  
```

La sortie est plus compacte que `readelf -h`. L'information clé est dans les **flags BFD** :

- `HAS_SYMS` — le binaire contient des symboles (il n'est pas strippé).  
- `DYNAMIC` — le binaire est dynamiquement lié.  
- `D_PAGED` — le binaire utilise la pagination mémoire (standard pour les exécutables).

Sur un binaire strippé, `HAS_SYMS` sera remplacé par une absence de ce flag, ce qui confirme la perte des symboles.

### Headers de sections : `-h`

```bash
$ objdump -h keygenme_O0

keygenme_O0:     file format elf64-x86-64

Sections:  
Idx Name          Size      VMA               LMA               File off  Algn  
  0 .interp       0000001c  0000000000000318  0000000000000318  00000318  2**0
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  [...]
 13 .text         00000225  00000000000010c0  00000000000010c0  000010c0  2**4
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
 15 .rodata       000000f5  0000000000002000  0000000000002000  00002000  2**3
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
 [...]
 22 .data         00000010  0000000000004000  0000000000004000  00003000  2**3
                  CONTENTS, ALLOC, LOAD, DATA
 23 .bss          00000008  0000000000004010  0000000000004010  00003010  2**0
                  ALLOC
```

Par rapport à `readelf -S`, `objdump -h` présente les attributs sous forme de mots-clés descriptifs au lieu de flags à une lettre. `CONTENTS, ALLOC, LOAD, READONLY, CODE` est plus lisible que `AX` une fois qu'on connaît la correspondance. Cela dit, `readelf -S` affiche des champs supplémentaires (comme `EntSize`, `Link`, `Info`) qui peuvent être utiles pour l'analyse approfondie des tables de symboles et de relocations.

### Contenu d'une section : `-s -j`

```bash
$ objdump -s -j .rodata keygenme_O0

keygenme_O0:     file format elf64-x86-64

Contents of section .rodata:
 2000 01000200 00000000 456e7465 7220796f  ........Enter yo
 2010 7572206c 6963656e 7365206b 65793a20  ur license key:
 [...]
```

Fonctionnellement équivalent à `readelf -x .rodata`, avec un format de sortie légèrement différent. L'option `-j` sélectionne la section, `-s` active le mode dump.

### Headers privés (ELF-specific) : `-p`

```bash
$ objdump -p keygenme_O0
```

Cette commande affiche les program headers et la section `.dynamic` — un mélange de `readelf -l` et `readelf -d`. L'information est la même, mais le format diffère. En pratique, `readelf` est généralement préféré pour l'inspection structurelle pure, car sa sortie est plus facilement parsable et plus détaillée.

---

## `readelf` vs `objdump` : quand utiliser lequel ?

Les deux outils se recouvrent beaucoup, et le choix est parfois une question de préférence. Voici cependant quelques principes directeurs :

**Préférez `readelf`** pour l'inspection structurelle pure : headers, sections, segments, tables de symboles, relocations, notes, informations DWARF. Sa sortie est plus complète, plus prévisible, et ne dépend pas de la bibliothèque BFD. Il gère mieux les ELF malformés ou non-standard — un avantage important pour l'analyse de malware.

**Préférez `objdump`** quand vous avez besoin de **désassembler** le code (chapitre 7) ou quand vous voulez une vue rapide et compacte des headers (`-f`). `objdump` est aussi l'outil de choix pour comparer des listings assembleur entre différents niveaux d'optimisation, grâce à ses options de formatage du désassemblage.

**Utilisez les deux** quand un outil donne un résultat ambigu. Avoir deux sources indépendantes qui parsent le même fichier avec des implémentations différentes renforce la confiance dans les résultats. Sur un ELF intentionnellement malformé (une technique anti-RE, voir chapitre 19), les divergences entre `readelf` et `objdump` sont elles-mêmes une source d'information.

---

## Lecture rapide : les réflexes à adopter

En pratique, lors d'un triage (section 5.7), on n'exécute pas toutes les options de `readelf` en séquence. Voici les commandes que le reverse engineer lance en priorité, et dans quel ordre :

```bash
# 1. Vue d'ensemble rapide : type, architecture, entry point
$ readelf -h keygenme_O0

# 2. Liste des sections : le binaire est-il strippé ?
#    Y a-t-il des sections inhabituelles ?
$ readelf -SW keygenme_O0

# 3. Dépendances : quelles bibliothèques sont liées ?
$ readelf -d keygenme_O0 | grep NEEDED

# 4. Segments : quelles permissions mémoire ?
#    NX est-il actif ? (pas de segment RWE)
$ readelf -lW keygenme_O0

# 5. Si non strippé : symboles (section 5.3)
$ readelf -s keygenme_O0
```

Cette séquence prend moins de 30 secondes et fournit un portrait structurel complet du binaire. Combinée aux résultats de `file` et `strings` (section 5.1), elle constitue la majorité du triage statique de premier niveau.

---

## Ce qu'il faut retenir pour la suite

- **`readelf -h`** est votre premier réflexe après `file`. Il confirme et détaille les informations de base : entry point, type de binaire, nombre de sections.  
- **`readelf -S`** révèle la structure interne du binaire. La présence ou l'absence de `.symtab` confirme si le binaire est strippé. Des noms de sections non-standard peuvent indiquer un packer ou un obfuscateur.  
- **`readelf -l`** montre comment le binaire sera chargé en mémoire. Les permissions des segments (`R`, `RE`, `RW`) sont directement liées aux protections de sécurité (NX, RELRO).  
- **`readelf -d | grep NEEDED`** liste les bibliothèques partagées nécessaires — chaque entrée est un indice fonctionnel (réseau ? crypto ? GUI ?).  
- **`objdump`** complète `readelf` et excelle pour le désassemblage, que nous aborderons au chapitre 7.  
- La distinction entre **sections** (vue linking) et **segments** (vue exécution) est fondamentale pour comprendre comment le code sur disque devient un processus en mémoire.

---


⏭️ [`nm` et `objdump -t` — inspection des tables de symboles](/05-outils-inspection-base/03-nm-symboles.md)
