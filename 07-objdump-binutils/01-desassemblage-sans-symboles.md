🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 7.1 — Désassemblage d'un binaire compilé sans symboles (`-s`)

> 🔧 **Outils utilisés** : `objdump`, `strip`, `readelf`, `nm`, `file`  
> 📦 **Binaires** : `keygenme_O0` et `keygenme_strip` (répertoire `binaries/ch07-keygenme/`)

---

## Le scénario réaliste : pas de symboles

Quand vous récupérez un binaire « dans la nature » — un exécutable commercial, un firmware extrait, un sample suspect — il y a de fortes chances qu'il ait été **strippé**. Les symboles de débogage (`-g`) n'y sont évidemment pas, et même la table des symboles standard (`.symtab`) a été retirée avec `strip`. Il ne reste que le strict minimum nécessaire à l'exécution : le code machine, les données, et les symboles dynamiques (`.dynsym`) si le binaire est lié dynamiquement.

C'est la situation que nous allons affronter dans cette section. Nous commencerons par comprendre ce que `strip` retire concrètement, puis nous verrons comment `objdump` se comporte face à un binaire dépouillé, et enfin comment naviguer malgré tout dans le listing produit.

---

## Ce que `strip` retire (et ce qu'il laisse)

Avant de désassembler un binaire strippé, comprenons précisément ce qui a disparu. Prenons notre binaire de travail et créons les deux versions si ce n'est pas déjà fait :

```bash
# Compilation avec symboles de débogage
gcc -O0 -g -o keygenme_O0 keygenme.c

# Version strippée
cp keygenme_O0 keygenme_strip  
strip keygenme_strip  
```

Comparons immédiatement la taille des deux fichiers :

```bash
$ ls -l keygenme_O0 keygenme_strip
-rwxr-xr-x 1 user user  20536  keygenme_O0
-rwxr-xr-x 1 user user  14472  keygenme_strip
```

La version strippée est sensiblement plus petite. La différence correspond aux sections de symboles et de débogage qui ont été supprimées. Pour voir exactement ce qui a changé, comparons les sections présentes dans chaque binaire :

```bash
$ readelf -S keygenme_O0 | grep -c '\['
31

$ readelf -S keygenme_strip | grep -c '\['
27
```

Les sections qui disparaissent typiquement après un `strip` sont :

| Section supprimée | Contenu perdu |  
|---|---|  
| `.symtab` | Table complète des symboles (noms de fonctions locales, variables globales…) |  
| `.strtab` | Chaînes associées à `.symtab` (les noms eux-mêmes) |  
| `.debug_info` | Informations DWARF : types, variables, numéros de ligne |  
| `.debug_abbrev` | Abréviations DWARF |  
| `.debug_line` | Correspondance adresse → ligne source |  
| `.debug_str` | Chaînes utilisées par DWARF |  
| `.debug_aranges` | Index des plages d'adresses DWARF |

Ce qui **reste** après le stripping, et c'est crucial :

| Section conservée | Pourquoi elle survit |  
|---|---|  
| `.dynsym` | Nécessaire au *dynamic linker* pour résoudre les symboles importés/exportés à l'exécution |  
| `.dynstr` | Chaînes associées à `.dynsym` |  
| `.plt` / `.got` / `.got.plt` | Mécanisme d'appel des fonctions de bibliothèques partagées (résolution dynamique) |  
| `.text` | Le code exécutable — c'est ce qu'on désassemble |  
| `.rodata` | Les constantes : chaînes littérales, tables de valeurs… |  
| `.data` / `.bss` | Variables globales initialisées / non-initialisées |

La commande `file` confirme la différence :

```bash
$ file keygenme_O0
keygenme_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
for GNU/Linux 3.2.0, with debug_info, not stripped  

$ file keygenme_strip
keygenme_strip: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
for GNU/Linux 3.2.0, stripped  
```

Notez les deux indications clés en fin de ligne : `with debug_info, not stripped` contre simplement `stripped`. La commande `file` vous donne cette information instantanément — c'est l'un des tout premiers réflexes du triage (chapitre 5).

---

## Vérifier la présence ou l'absence de symboles avec `nm`

Avant de désassembler, il est utile de confirmer l'état des symboles avec `nm` :

```bash
$ nm keygenme_O0
0000000000001189 T check_serial
0000000000001139 T compute_hash
00000000000011e2 T main
                 U printf@@GLIBC_2.2.5
                 U puts@@GLIBC_2.2.5
                 U strcmp@@GLIBC_2.2.5
...
```

On voit ici les fonctions définies dans le binaire (`T` = section `.text`) et les fonctions importées (`U` = *undefined*, résolues dynamiquement). Chaque symbole est associé à son adresse virtuelle. C'est une mine d'or pour le RE : on sait immédiatement quelles fonctions existent et où elles se trouvent.

Sur le binaire strippé :

```bash
$ nm keygenme_strip
nm: keygenme_strip: no symbols
```

La table `.symtab` a disparu. Mais les symboles **dynamiques** sont toujours là :

```bash
$ nm -D keygenme_strip
                 w __cxa_finalize
                 w __gmon_start__
                 w _ITM_deregisterTMCloneTable
                 w _ITM_registerTMCloneTable
                 U printf
                 U puts
                 U strcmp
...
```

L'option `-D` interroge `.dynsym` au lieu de `.symtab`. On récupère les noms des fonctions de la libc appelées par le programme (`printf`, `puts`, `strcmp`), mais **aucun nom de fonction locale** — `main`, `check_serial`, `compute_hash` ont tous disparu. Ce sont ces noms-là que le reverse engineer devra reconstituer manuellement.

> 💡 **Point clé** : même sur un binaire strippé, les appels aux bibliothèques partagées restent identifiables grâce à `.dynsym` et au mécanisme PLT/GOT. C'est un point d'ancrage majeur pour le RE. Quand vous voyez un `call` vers `strcmp@plt`, vous savez immédiatement ce que fait cette instruction, et vous pouvez remonter la logique autour.

---

## Désassembler avec `objdump -d`

Passons au désassemblage proprement dit. La commande de base est :

```bash
objdump -d keygenme_O0
```

L'option `-d` (*disassemble*) décode toutes les sections marquées comme contenant du code exécutable — en pratique, `.text`, `.init`, `.fini`, et `.plt`. Le listing produit ressemble à ceci (extrait simplifié) :

```
keygenme_O0:     file format elf64-x86-64


Disassembly of section .init:

0000000000001000 <_init>:
    1000:       f3 0f 1e fa             endbr64
    1004:       48 83 ec 08             sub    $0x8,%rsp
    ...

Disassembly of section .plt:

0000000000001020 <.plt>:
    1020:       ff 35 e2 2f 00 00       pushq  0x2fe2(%rip)
    ...

0000000000001030 <puts@plt>:
    1030:       ff 25 e2 2f 00 00       jmpq   *0x2fe2(%rip)
    ...

0000000000001040 <printf@plt>:
    1040:       ff 25 da 2f 00 00       jmpq   *0x2fda(%rip)
    ...

Disassembly of section .text:

0000000000001060 <_start>:
    1060:       f3 0f 1e fa             endbr64
    1064:       31 ed                   xor    %ebp,%ebp
    ...

0000000000001139 <compute_hash>:
    1139:       55                      push   %rbp
    113a:       48 89 e5                mov    %rsp,%rbp
    ...

0000000000001189 <check_serial>:
    1189:       55                      push   %rbp
    118a:       48 89 e5                mov    %rsp,%rbp
    ...

00000000000011e2 <main>:
    11e2:       55                      push   %rbp
    11e3:       48 89 e5                mov    %rsp,%rbp
    ...
```

### Anatomie d'une ligne

Chaque ligne du listing suit le format :

```
    11e2:       55                      push   %rbp
    ^^^^        ^^                      ^^^^^^^^^^^^^^^^
    │           │                       └─ Instruction décodée (mnémonique + opérandes)
    │           └─ Octets machine (opcodes bruts en hexadécimal)
    └─ Adresse virtuelle (offset dans le binaire)
```

Les trois colonnes sont toujours présentes. L'adresse vous permet de vous repérer, les octets bruts sont utiles pour le patching (chapitre 21), et le mnémonique est ce que vous lisez en premier.

### Les labels entre chevrons

Quand les symboles sont disponibles, `objdump` affiche le nom de chaque fonction comme un **label** entre chevrons (`<compute_hash>:`, `<main>:`…). Ces labels sont inestimables : ils segmentent le listing en blocs logiques et donnent immédiatement le nom de chaque fonction.

À l'intérieur du code, les références aux fonctions utilisent aussi ces labels :

```
    11f5:       e8 3f ff ff ff          callq  1139 <compute_hash>
```

Ici, le `call` pointe vers l'adresse `0x1139`, et `objdump` vous indique entre chevrons qu'il s'agit de `compute_hash`. Sans symboles, vous auriez vu uniquement :

```
    11f5:       e8 3f ff ff ff          callq  1139
```

L'adresse est toujours là, mais le nom a disparu. C'est à vous de déterminer ce que fait la fonction à `0x1139`.

---

## Désassembler un binaire strippé

Exécutons maintenant la même commande sur le binaire strippé :

```bash
objdump -d keygenme_strip
```

Le listing change de manière significative :

```
Disassembly of section .text:

0000000000001060 <.text>:
    1060:       f3 0f 1e fa             endbr64
    1064:       31 ed                   xor    %ebp,%ebp
    1066:       49 89 d1                mov    %rdx,%r9
    ...
    1139:       55                      push   %rbp
    113a:       48 89 e5                mov    %rsp,%rbp
    113d:       48 89 7d e8             mov    %rdi,-0x18(%rbp)
    ...
    1189:       55                      push   %rbp
    118a:       48 89 e5                mov    %rsp,%rbp
    ...
    11e2:       55                      push   %rbp
    11e3:       48 89 e5                mov    %rsp,%rbp
    ...
```

Plusieurs différences sautent aux yeux :

**Un seul label pour toute la section `.text`.** Au lieu de voir `<compute_hash>:`, `<check_serial>:`, `<main>:`, on ne voit qu'un unique `<.text>:` au début. Toute la section est traitée comme un bloc monolithique. Les frontières entre fonctions ne sont plus marquées.

**Les `call` internes perdent leur annotation.** Là où on voyait `callq 1139 <compute_hash>`, on voit désormais simplement `callq 1139`. L'adresse cible est toujours correcte, mais c'est à vous de noter que `0x1139` est le début d'une fonction et de lui donner un nom.

**Les appels à la PLT restent annotés.** Bonne nouvelle : les `call` vers les fonctions de bibliothèques dynamiques conservent leurs labels parce que `.dynsym` n'a pas été supprimée :

```
    1205:       e8 36 fe ff ff          callq  1040 <printf@plt>
```

C'est l'un des rares repères qui subsistent et ils sont précieux. Si vous voyez un `call` vers `strcmp@plt`, vous savez que le code juste avant prépare deux chaînes dans `rdi` et `rsi` pour les comparer. C'est un point de départ solide pour comprendre la logique du programme.

---

## Options essentielles d'`objdump` pour le désassemblage

Voici les options que vous utiliserez le plus fréquemment, au-delà du simple `-d` :

### `-d` vs `-D` : code exécutable vs tout désassembler

L'option `-d` ne désassemble que les sections contenant du code (celles avec le flag `SHF_EXECINSTR`). L'option `-D` (majuscule) désassemble **toutes** les sections, y compris `.data`, `.rodata`, `.got`, etc.

```bash
objdump -D keygenme_strip | head -100
```

En pratique, `-D` est rarement ce que vous voulez : décoder `.rodata` comme des instructions x86 produit du bruit. Mais c'est parfois utile pour examiner le contenu de `.got.plt` ou pour repérer du code caché dans une section de données (technique d'obfuscation).

Préférez `-d` pour le travail quotidien.

### `-M intel` : basculer en syntaxe Intel

Par défaut, `objdump` utilise la syntaxe AT&T, héritée d'Unix. La section 7.2 couvre ce sujet en détail, mais voici l'aperçu :

```bash
# Syntaxe AT&T (par défaut)
objdump -d keygenme_strip
    113a:       48 89 e5                mov    %rsp,%rbp

# Syntaxe Intel
objdump -d -M intel keygenme_strip
    113a:       48 89 e5                mov    rbp,rsp
```

La syntaxe Intel inverse l'ordre des opérandes (destination en premier) et supprime les préfixes `%` et `$`. La majorité de la documentation RE, des cours et des outils (IDA, Ghidra par défaut) utilisent la syntaxe Intel. Nous l'adopterons dans la suite de cette formation.

> 💡 **Astuce** : pour ne pas taper `-M intel` à chaque fois, vous pouvez créer un alias dans votre `~/.bashrc` :  
> ```bash  
> alias objdump='objdump -M intel'  
> ```

### `-j <section>` : désassembler une section spécifique

Si vous ne voulez voir que `.text` sans le bruit de `.init`, `.fini` et `.plt` :

```bash
objdump -d -j .text keygenme_strip
```

Vous pouvez aussi cibler `.plt` pour étudier le mécanisme de résolution dynamique :

```bash
objdump -d -j .plt keygenme_strip
```

### `--start-address` / `--stop-address` : fenêtre d'adresses

Pour zoomer sur une zone précise du binaire — par exemple, la fonction qui commence à `0x1189` et semble se terminer aux alentours de `0x11e1` :

```bash
objdump -d -M intel --start-address=0x1189 --stop-address=0x11e2 keygenme_strip
```

C'est l'équivalent d'un « zoom » : au lieu de chercher dans un listing de plusieurs milliers de lignes, vous isolez exactement la portion qui vous intéresse.

### `-S` : entrelacer code source et assembleur

Si le binaire contient les informations de débogage DWARF (compilé avec `-g`, non strippé), l'option `-S` affiche le code source C original entrelacé avec les instructions assembleur :

```bash
objdump -d -S -M intel keygenme_O0
```

Le résultat ressemble à ceci :

```
int compute_hash(const char *input) {
    1139:       55                      push   rbp
    113a:       48 89 e5                mov    rbp,rsp
    113d:       48 89 7d e8             mov    QWORD PTR [rbp-0x18],rdi
    int hash = 0;
    1141:       c7 45 fc 00 00 00 00    mov    DWORD PTR [rbp-0x4],0x0
    for (int i = 0; input[i] != '\0'; i++) {
    1148:       c7 45 f8 00 00 00 00    mov    DWORD PTR [rbp-0x8],0x0
    114f:       eb 1d                   jmp    116e
```

Cette vue est **extrêmement** précieuse pour l'apprentissage. Elle vous montre exactement quelle instruction correspond à quelle ligne de C. C'est le meilleur moyen de construire votre intuition sur les patterns compilateur GCC. Bien sûr, cette option ne fonctionne que sur un binaire non strippé contenant les infos DWARF — sur un binaire strippé, `-S` se comporte exactement comme `-d`.

> 💡 **Méthode recommandée** : quand vous étudiez un binaire fourni avec ce tutoriel, ouvrez **deux terminaux côte à côte**. Dans le premier, désassemblez la version avec symboles et débogage (`objdump -d -S -M intel keygenme_O0`). Dans le second, désassemblez la version strippée (`objdump -d -M intel keygenme_strip`). Comparez les deux listings. Vous verrez exactement ce que le stripping fait disparaître, et vous apprendrez à reconnaître les patterns sans l'aide des annotations.

### `-r` et `-R` : les relocations

L'option `-r` affiche les relocations des fichiers objets (`.o`), et `-R` celles du binaire final. C'est moins utilisé en RE quotidien, mais peut être utile pour comprendre comment le linker a résolu les adresses. Nous ne les approfondirons pas dans cette section, mais sachez qu'elles existent.

---

## Stratégie face à un binaire strippé : repérer les frontières de fonctions

Le principal défi d'un binaire strippé est que les fonctions ne sont plus délimitées par des labels. Le listing est un flux continu d'instructions. Comment s'y retrouver ?

### 1. Chercher les prologues

En `x86-64`, la grande majorité des fonctions compilées par GCC en `-O0` commencent par le même prologue :

```asm
push   rbp  
mov    rbp, rsp  
sub    rsp, <N>       ; optionnel, allocation de variables locales  
```

Chercher toutes les occurrences de `push rbp` suivi de `mov rbp, rsp` dans le listing vous donne une approximation fiable des débuts de fonctions. Avec `grep` :

```bash
objdump -d -M intel keygenme_strip | grep -n "push   rbp"
```

Chaque ligne trouvée est potentiellement le début d'une fonction. Ce n'est pas infaillible (un `push rbp` peut apparaître dans un autre contexte, et les fonctions optimisées peuvent omettre le *frame pointer*), mais en `-O0`, c'est remarquablement fiable.

### 2. Chercher les épilogues

Symétriquement, les fonctions se terminent par :

```asm
leave              ; équivaut à  mov rsp, rbp  +  pop rbp  
ret  
```

ou parfois :

```asm
pop    rbp  
ret  
```

Un `ret` suivi d'un `push rbp` est un signal très fort de frontière entre deux fonctions.

### 3. Suivre les `call`

Chaque instruction `call <adresse>` vous indique qu'une fonction existe à l'adresse cible. Collectez toutes les cibles de `call` :

```bash
objdump -d -M intel keygenme_strip | grep "call" | grep -v plt
```

Cela vous donne la liste des fonctions internes appelées. En combinant cette liste avec les prologues trouvés au point 1, vous pouvez rapidement reconstituer un répertoire de fonctions, même sans symboles.

### 4. S'appuyer sur les appels PLT

Les appels à `printf@plt`, `strcmp@plt`, `malloc@plt` et consorts sont des indices sémantiques forts. Si vous voyez :

```asm
lea    rdi, [rip+0x...]    ; charge l'adresse d'une chaîne dans rdi  
call   printf@plt  
```

Vous savez que la fonction en cours utilise `printf`, et que le premier argument (dans `rdi`, convention System V) est probablement un pointeur vers une chaîne formatée. Allez consulter `.rodata` pour trouver cette chaîne :

```bash
objdump -s -j .rodata keygenme_strip
```

Ou plus simplement :

```bash
strings keygenme_strip
```

Relier les chaînes aux points du code où elles sont référencées est une technique fondamentale du RE sur binaire strippé. Si vous trouvez une chaîne `"Serial invalide !\n"`, vous savez que la fonction qui la charge est probablement la routine de vérification du serial — et vous venez de localiser votre cible sans avoir eu besoin d'un seul nom de symbole.

---

## L'option `-t` et `--syms` : quand les symboles sont partiels

Il existe une situation intermédiaire : le binaire n'a pas été compilé avec `-g` (pas de DWARF), mais n'a pas non plus été strippé. C'est le cas par défaut quand on compile simplement avec `gcc -o binary source.c` sans option particulière. La table `.symtab` est présente, mais les informations de débogage (types, numéros de ligne, variables locales) ne le sont pas.

Dans ce cas, `objdump -d` affiche bien les labels de fonctions, mais `-S` ne peut pas entrelacer le code source. C'est un entre-deux courant, et `objdump` s'en sort très bien : le désassemblage est clair, les fonctions sont nommées, les `call` sont annotés.

Pour vérifier rapidement l'état des symboles d'un binaire avant de le désassembler :

```bash
# .symtab présente ?
readelf -S keygenme_O0 | grep symtab

# Sections DWARF présentes ?
readelf -S keygenme_O0 | grep debug

# Résumé rapide
file keygenme_O0
```

Ces trois commandes prennent quelques secondes et vous informent immédiatement sur ce que `objdump` sera capable de vous montrer.

---

## Rediriger et filtrer le listing

Sur un binaire de taille raisonnable, le listing d'`objdump` fait quelques centaines à quelques milliers de lignes. Sur un binaire réel (un navigateur web, un serveur…), il peut dépasser le million de lignes. Quelques techniques pour ne pas se noyer :

**Rediriger vers un fichier** pour pouvoir chercher à loisir :

```bash
objdump -d -M intel keygenme_strip > keygenme_strip.asm
```

Vous pouvez ensuite ouvrir ce fichier dans votre éditeur de texte favori et utiliser la recherche intégrée.

**Combiner avec `less`** pour une navigation interactive :

```bash
objdump -d -M intel keygenme_strip | less
```

Dans `less`, tapez `/` suivi d'un terme de recherche (par exemple `/call` ou `/strcmp`) pour sauter directement aux occurrences.

**`grep` avec contexte** pour extraire les zones intéressantes :

```bash
# Voir 10 lignes avant et 20 lignes après chaque appel à strcmp
objdump -d -M intel keygenme_strip | grep -B10 -A20 "strcmp"
```

**Compter les fonctions** (approximation par les prologues) :

```bash
objdump -d -M intel keygenme_strip | grep -c "push   rbp"
```

Ces techniques semblent rudimentaires comparées à l'interface de Ghidra, mais elles sont rapides, scriptables, et parfaitement adaptées à un premier survol du binaire.

---

## Résumé

Le stripping supprime les noms de fonctions locales et les informations de débogage, mais laisse intact le code machine, les données, et les symboles dynamiques. Face à un binaire strippé, `objdump -d` produit un listing linéaire sans labels de fonctions, mais les appels PLT restent annotés. Pour reconstituer la structure du programme, on s'appuie sur les prologues/épilogues pour délimiter les fonctions, sur les cibles des `call` pour en dresser l'inventaire, et sur les appels PLT combinés aux chaînes de `.rodata` pour donner du sens à chaque fonction. Ces techniques manuelles forment la base de tout travail de RE sur binaire inconnu, et restent pertinentes même quand on dispose d'outils plus sophistiqués.

---


⏭️ [Syntaxe AT&T vs Intel — passer de l'une à l'autre (`-M intel`)](/07-objdump-binutils/02-att-vs-intel.md)
