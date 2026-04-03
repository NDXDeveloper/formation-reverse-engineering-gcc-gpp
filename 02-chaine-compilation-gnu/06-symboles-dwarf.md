🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.6 — Comprendre les fichiers de symboles DWARF

> 🎯 **Objectif de cette section** : Comprendre la structure et le contenu du format DWARF, savoir extraire et exploiter les informations de débogage quand elles sont disponibles, et connaître les situations où un reverse engineer peut en bénéficier même sur un binaire de production.

---

## Qu'est-ce que DWARF ?

DWARF (*Debugging With Attributed Record Formats*) est le format standard d'informations de débogage pour les systèmes Unix-like. Il est produit par GCC (et Clang) lorsque le flag `-g` est utilisé, et consommé par les débogueurs (GDB), les profilers (`perf`, `valgrind`), les outils de stack unwinding, et les désassembleurs/décompilateurs (Ghidra, IDA).

Le nom DWARF est un clin d'œil à ELF (les elfes et les nains de la mythologie fantastique). Le format en est actuellement à sa version 5 (DWARF5, publiée en 2017), mais GCC produit par défaut du DWARF4 ou DWARF5 selon la version. Vous pouvez forcer une version avec `-gdwarf-4` ou `-gdwarf-5`.

DWARF répond à une question fondamentale : **comment relier le code machine (adresses, registres, offsets de pile) au code source (fichiers, lignes, fonctions, variables, types) ?** Cette correspondance est exactement l'inverse du travail du reverse engineer — c'est pourquoi les informations DWARF, quand elles sont présentes, sont un raccourci extraordinaire.

## Où sont stockées les informations DWARF ?

Les informations DWARF sont réparties dans plusieurs sections du fichier ELF, toutes préfixées par `.debug_`. En section 2.5, nous les avons listées brièvement. Détaillons maintenant le rôle de chacune :

| Section | Rôle | Consommateur principal |  
|---|---|---|  
| `.debug_info` | Descriptions structurées de tout le programme : fonctions, variables, types, portées, paramètres | GDB, Ghidra, IDA |  
| `.debug_abbrev` | Définitions des « formulaires » utilisés dans `.debug_info` (système de compression) | Parser DWARF interne |  
| `.debug_line` | Table de correspondance adresse machine ↔ fichier source + numéro de ligne | GDB (`list`), profilers |  
| `.debug_str` | Pool de chaînes de caractères (noms de fonctions, variables, types, fichiers) | Référencé par `.debug_info` |  
| `.debug_loc` | Listes de localisation : où se trouve une variable à chaque point du programme | GDB (`print variable`) |  
| `.debug_ranges` | Plages d'adresses discontinues associées à une entité (fonctions optimisées, scopes) | GDB, analyseurs |  
| `.debug_frame` | Informations de déroulement de pile (CFA — Call Frame Address) | GDB (`backtrace`), unwinders |  
| `.debug_aranges` | Index accéléré : plages d'adresses → unités de compilation | Recherche rapide |  
| `.debug_types` | Descriptions de types (DWARF4, fusionné dans `.debug_info` en DWARF5) | GDB, Ghidra |  
| `.debug_macro` | Définitions de macros préprocesseur (si `-g3` est utilisé) | GDB (`macro expand`) |  
| `.debug_str_offsets` | Table d'offsets dans `.debug_str` (DWARF5, optimisation d'accès) | Parser DWARF interne |  
| `.debug_line_str` | Pool de chaînes spécifique à `.debug_line` (DWARF5) | Parser DWARF interne |  
| `.debug_addr` | Table d'adresses partagée (DWARF5, réduction de la redondance) | Parser DWARF interne |

Toutes ces sections ont le flag `A` absent — elles ne sont **pas chargées en mémoire** à l'exécution. Elles n'existent que dans le fichier sur disque. C'est pourquoi les supprimer (avec `strip` ou `-s`) ne modifie en rien le comportement du programme.

Pour vérifier rapidement la présence d'informations DWARF :

```bash
readelf -S hello_debug | grep debug
```

Si aucune section `.debug_*` n'apparaît, le binaire a été compilé sans `-g` ou a été strippé.

## La structure de `.debug_info` : les DIE

Le cœur de DWARF est la section `.debug_info`, organisée en une hiérarchie d'entrées appelées **DIE** (*Debugging Information Entry*). Chaque DIE représente une entité du programme source et possède un **tag** (son type) et une liste d'**attributs** (ses propriétés).

### Tags courants

| Tag | Représente | Exemple |  
|---|---|---|  
| `DW_TAG_compile_unit` | Une unité de compilation (un fichier `.c`) | `hello.c` |  
| `DW_TAG_subprogram` | Une fonction | `check`, `main` |  
| `DW_TAG_formal_parameter` | Un paramètre de fonction | `input`, `argc`, `argv` |  
| `DW_TAG_variable` | Une variable (locale ou globale) | `counter` |  
| `DW_TAG_base_type` | Un type primitif | `int`, `char`, `long` |  
| `DW_TAG_pointer_type` | Un type pointeur | `const char *` |  
| `DW_TAG_structure_type` | Une structure (`struct`) | `struct sockaddr` |  
| `DW_TAG_class_type` | Une classe C++ | `class MyClass` |  
| `DW_TAG_enumeration_type` | Un type énuméré | `enum Color` |  
| `DW_TAG_typedef` | Un alias de type | `typedef unsigned int uint32_t` |  
| `DW_TAG_array_type` | Un type tableau | `int buffer[1024]` |  
| `DW_TAG_lexical_block` | Un bloc de portée (`{ ... }`) | Bloc interne d'un `if` |  
| `DW_TAG_inlined_subroutine` | Une fonction qui a été inlinée | `check` inliné dans `main` |

### Attributs courants

| Attribut | Signification | Exemple de valeur |  
|---|---|---|  
| `DW_AT_name` | Nom de l'entité | `"check"` |  
| `DW_AT_type` | Référence vers le DIE décrivant le type | `→ DIE #0x4a (int)` |  
| `DW_AT_low_pc` | Adresse de début en mémoire | `0x1149` |  
| `DW_AT_high_pc` | Adresse de fin (ou taille) | `0x1172` (ou taille: `41`) |  
| `DW_AT_decl_file` | Fichier source de déclaration | `hello.c` |  
| `DW_AT_decl_line` | Ligne de déclaration | `6` |  
| `DW_AT_location` | Localisation de la variable (registre, pile, expression) | `DW_OP_fbreg -24` (pile, rbp-24) |  
| `DW_AT_encoding` | Encodage d'un type primitif | `DW_ATE_signed` (entier signé) |  
| `DW_AT_byte_size` | Taille en octets | `4` (pour un `int`) |  
| `DW_AT_comp_dir` | Répertoire de compilation | `/home/user/project` |  
| `DW_AT_producer` | Compilateur utilisé | `GNU C17 13.2.0 -O0 -g` |  
| `DW_AT_inline` | Indication d'inlining | `DW_INL_inlined` |

### Hiérarchie des DIE

Les DIE sont organisés en arbre. Chaque DIE peut contenir des DIE enfants, formant une structure qui reflète l'imbrication du code source :

```
DW_TAG_compile_unit ("hello.c")
├── DW_TAG_base_type ("int", 4 bytes, signed)
├── DW_TAG_base_type ("char", 1 byte, signed)
├── DW_TAG_pointer_type (→ const char)
├── DW_TAG_subprogram ("check")
│   ├── DW_TAG_formal_parameter ("input", type: const char *)
│   │       DW_AT_location: DW_OP_fbreg -24   ← sur la pile, rbp-24
│   └── DW_TAG_variable (variable temporaire du retour strcmp)
├── DW_TAG_subprogram ("main")
│   ├── DW_TAG_formal_parameter ("argc", type: int)
│   │       DW_AT_location: DW_OP_fbreg -20   ← sur la pile, rbp-20
│   ├── DW_TAG_formal_parameter ("argv", type: char **)
│   │       DW_AT_location: DW_OP_fbreg -32   ← sur la pile, rbp-32
│   └── DW_TAG_lexical_block
│       └── ...
```

Cette hiérarchie est exactement ce que Ghidra et GDB exploitent pour afficher les noms de fonctions, les types de paramètres et les valeurs de variables pendant le débogage.

## Explorer les données DWARF en pratique

### Avec `readelf`

L'outil `readelf` fournit plusieurs options pour explorer les sections DWARF :

```bash
# Vue complète de .debug_info (verbeux — peut être très long)
readelf --debug-dump=info hello_debug

# Table de correspondance ligne ↔ adresse (décodée)
readelf --debug-dump=decodedline hello_debug

# Informations de frame (déroulement de pile)
readelf --debug-dump=frames hello_debug

# Plages d'adresses
readelf --debug-dump=aranges hello_debug
```

La sortie de `--debug-dump=info` pour notre fonction `check()` ressemble à ceci (simplifiée) :

```
 <1><0x80>: Abbrev Number: 5 (DW_TAG_subprogram)
    <0x81>   DW_AT_external    : 1
    <0x82>   DW_AT_name        : check
    <0x87>   DW_AT_decl_file   : 1 (hello.c)
    <0x88>   DW_AT_decl_line   : 6
    <0x89>   DW_AT_type        : <0x4a> (int)
    <0x8d>   DW_AT_low_pc      : 0x1149
    <0x95>   DW_AT_high_pc     : 0x29 (taille)
    <0x99>   DW_AT_frame_base  : 1 byte block: 56 (DW_OP_reg6 (rbp))
 <2><0x9b>: Abbrev Number: 6 (DW_TAG_formal_parameter)
    <0x9c>   DW_AT_name        : input
    <0x a2>  DW_AT_decl_line   : 6
    <0xa3>   DW_AT_type        : <0x3b> (const char *)
    <0xa7>   DW_AT_location    : 2 byte block: 91 58 (DW_OP_fbreg -24)
```

On y lit directement que la fonction `check` est définie à la ligne 6 de `hello.c`, commence à l'adresse `0x1149`, fait `0x29` (41) octets, retourne un `int`, et que son paramètre `input` (de type `const char *`) est stocké sur la pile à `rbp - 24`.

### Avec `objdump`

L'outil `objdump` peut mélanger le code source avec le désassemblage quand les informations DWARF sont présentes :

```bash
objdump -d -S hello_debug
```

Le flag `-S` intercale les lignes de code source entre les instructions assembleur :

```
0000000000001149 <check>:
int check(const char *input) {
    1149:   55                      push   %rbp
    114a:   48 89 e5                mov    %rsp,%rbp
    114d:   48 83 ec 10             sub    $0x10,%rsp
    1151:   48 89 7d f8             mov    %rdi,-0x8(%rbp)
    return strcmp(input, SECRET) == 0;
    1155:   48 8b 45 f8             mov    -0x8(%rbp),%rax
    1159:   48 8d 15 a4 0e 00 00    lea    0xea4(%rip),%rdx
    1160:   48 89 d6                mov    %rdx,%rsi
    1163:   48 89 c7                mov    %rax,%rdi
    1166:   e8 c5 fe ff ff          call   1030 <strcmp@plt>
    116b:   85 c0                   test   %eax,%eax
    116d:   0f 94 c0                sete   %al
    1170:   0f b6 c0                movzbl %al,%eax
}
    1173:   c9                      leave
    1174:   c3                      ret
```

Cette vue intercalée est un outil d'apprentissage précieux pour comprendre la correspondance C → assembleur. C'est aussi un raccourci pour analyser un binaire quand vous disposez à la fois du source et du binaire (audit de sécurité, vérification de compilation).

### Avec `dwarfdump` et `eu-readelf`

Pour des explorations plus avancées, deux outils spécialisés complètent `readelf` :

- **`dwarfdump`** (paquet `libdwarf-tools` ou `dwarfdump`) : outil dédié à l'analyse DWARF, avec des options de filtrage et un format de sortie plus lisible que `readelf`.  
- **`eu-readelf`** (paquet `elfutils`) : version alternative de `readelf` développée par Red Hat, souvent plus performante sur les gros fichiers DWARF et supportant mieux DWARF5.

```bash
# Avec dwarfdump
dwarfdump --name=check hello_debug      # Chercher tout ce qui concerne "check"  
dwarfdump --print-lines hello_debug     # Table des lignes  

# Avec eu-readelf (elfutils)
eu-readelf --debug-dump=info hello_debug
```

## La table des lignes (`.debug_line`)

La table des lignes est l'une des sections DWARF les plus directement utiles. Elle établit une correspondance bidirectionnelle entre chaque adresse machine et le fichier source + numéro de ligne qui a généré cette instruction.

```bash
readelf --debug-dump=decodedline hello_debug
```

Sortie simplifiée :

| Adresse | Fichier | Ligne | Colonne | Flags |  
|---|---|---|---|---|  
| `0x1149` | `hello.c` | 6 | 0 | `is_stmt` |  
| `0x1155` | `hello.c` | 7 | 0 | `is_stmt` |  
| `0x1175` | `hello.c` | 10 | 0 | `is_stmt` |  
| `0x1188` | `hello.c` | 11 | 0 | `is_stmt` |  
| `0x1197` | `hello.c` | 13 | 0 | `is_stmt` |  
| `0x11a3` | `hello.c` | 14 | 0 | `is_stmt` |  
| `0x11a8` | `hello.c` | 16 | 0 | `is_stmt` |

Le flag `is_stmt` (*is statement*) indique que l'adresse correspond au début d'une instruction source (par opposition à du code intermédiaire généré par le compilateur). Les débogueurs utilisent ce flag pour placer les breakpoints de manière pertinente quand l'utilisateur demande « break à la ligne 7 ».

Avec des optimisations (`-O2 -g`), cette table devient plus complexe : une même ligne source peut correspondre à des adresses non contiguës (le compilateur a réordonné les instructions), et une même adresse peut être associée à plusieurs lignes (code fusionné).

## Les expressions de localisation (`DW_AT_location`)

L'un des aspects les plus sophistiqués de DWARF est le système de **localisation des variables**. À chaque point du programme, une variable peut se trouver à des endroits différents : dans un registre, sur la pile, dans une zone mémoire, ou même n'exister que partiellement.

### Localisation simple

Pour un binaire compilé en `-O0`, les localisations sont stables et simples :

```
DW_AT_location: DW_OP_fbreg -24    → Variable à rbp - 24 (sur la pile)  
DW_AT_location: DW_OP_reg0         → Variable dans rax  
DW_AT_location: DW_OP_addr 0x4020  → Variable à l'adresse globale 0x4020  
```

Les opérations `DW_OP_*` forment un petit langage à pile (*stack machine*) qui permet de décrire des localisations arbitrairement complexes. Les cas les plus courants sont `DW_OP_fbreg` (offset par rapport au frame base, généralement `rbp`) et `DW_OP_reg*` (dans un registre).

### Listes de localisation (code optimisé)

Avec des optimisations, une variable peut changer d'emplacement au cours de l'exécution de la fonction. DWARF utilise alors des **listes de localisation** dans la section `.debug_loc` :

```
Variable "input" :
  [0x1149, 0x1151) → DW_OP_reg5 (rdi)        ← dans le registre rdi à l'entrée
  [0x1151, 0x1166) → DW_OP_fbreg -24          ← puis sauvegardée sur la pile
  [0x1166, 0x1174) → <optimized out>          ← plus accessible après le call
```

Cette liste se lit ainsi : entre les adresses `0x1149` et `0x1151`, la variable `input` est dans le registre `rdi` (c'est le premier paramètre, conformément à la convention System V AMD64). Puis elle est copiée sur la pile. Après l'appel à `strcmp`, elle n'est plus nécessaire et le compilateur a réutilisé son emplacement.

C'est la raison du fameux message GDB « `<optimized out>`» : le débogueur consulte la liste de localisation et constate qu'à l'adresse courante du programme, la variable n'a plus d'emplacement défini.

> 💡 **En RE** : Les listes de localisation sont un outil avancé mais puissant. Elles révèlent exactement dans quel registre ou à quel offset de pile se trouve chaque variable à chaque point du programme. Si vous analysez un binaire `-O2 -g`, ces informations vous permettent de suivre les valeurs à travers les optimisations du compilateur.

## DWARF et les outils de RE

### GDB

GDB est le consommateur principal des informations DWARF. En présence de DWARF, GDB peut :

- Afficher le code source (`list`), poser des breakpoints par nom de fonction ou numéro de ligne (`break check`, `break hello.c:7`).  
- Afficher les variables par leur nom (`print input`, `info locals`).  
- Afficher les types complets (`ptype struct sockaddr`, `whatis variable`).  
- Produire un backtrace lisible avec noms de fonctions et numéros de ligne.  
- Naviguer dans les frames d'appel (`frame`, `up`, `down`) avec contexte source.

Sans DWARF, GDB fonctionne toujours mais en mode « brut » : pas de noms, pas de types, pas de source — uniquement des adresses, des registres et du code machine. Le Chapitre 11 couvre les deux modes de travail.

### Ghidra

Ghidra importe les informations DWARF lors de l'analyse initiale d'un binaire. Si DWARF est présent, Ghidra applique automatiquement les noms de fonctions, les types de paramètres, les noms de variables locales et les définitions de structures au code décompilé. Le résultat est un pseudo-code remarquablement proche du source original.

Même en l'absence de DWARF dans le binaire cible, vous pouvez exploiter DWARF indirectement : si le binaire utilise une bibliothèque open source (par exemple OpenSSL), vous pouvez compiler cette bibliothèque avec `-g`, extraire les informations de type (structures, enums, typedefs), et les importer dans votre projet Ghidra sous forme de fichiers de types. Ce workflow est couvert au Chapitre 8.

### Valgrind et les sanitizers

Valgrind (Chapitre 14) et les sanitizers comme AddressSanitizer (`-fsanitize=address`) utilisent les informations DWARF pour afficher des messages d'erreur lisibles : nom de la variable corrompue, ligne source de l'allocation, pile d'appels avec noms de fonctions. Sans DWARF, les rapports contiennent uniquement des adresses hexadécimales.

## Informations DWARF séparées

Les informations DWARF sont volumineuses — elles peuvent multiplier la taille du binaire par 3 à 10. Pour les builds de production, il est courant de **séparer** les informations de débogage du binaire exécutable.

### Fichiers debug séparés

GCC et les outils GNU permettent d'extraire les informations DWARF dans un fichier séparé :

```bash
# 1. Compiler avec -g
gcc -O2 -g -o hello hello.c

# 2. Extraire les infos de debug dans un fichier séparé
objcopy --only-keep-debug hello hello.debug

# 3. Stripper le binaire principal
strip hello

# 4. Ajouter un lien vers le fichier debug (via build-id ou section .gnu_debuglink)
objcopy --add-gnu-debuglink=hello.debug hello
```

Après cette opération, `hello` est strippé (compact) mais GDB trouvera automatiquement `hello.debug` et chargera les informations DWARF. Le lien se fait soit via le **build-id** (section `.note.gnu.build-id`), soit via la section `.gnu_debuglink` qui contient le nom du fichier debug et un CRC de vérification.

### Paquets debug des distributions Linux

Les distributions Linux utilisent exactement ce mécanisme. Pour chaque paquet binaire, un paquet `-dbgsym` ou `-debuginfo` séparé contient les fichiers `.debug` :

```bash
# Debian/Ubuntu — installer les symboles de debug de la libc
sudo apt install libc6-dbg

# Fedora/RHEL — installer les debuginfo
sudo dnf debuginfo-install glibc
```

Les fichiers sont installés dans `/usr/lib/debug/` avec une arborescence miroir. GDB les trouve automatiquement.

### `debuginfod` — Serveur de debug à la demande

Le projet `debuginfod` (intégré à `elfutils`) va plus loin : au lieu d'installer des paquets, GDB télécharge les informations de débogage **à la volée** depuis un serveur HTTP en utilisant le build-id comme clé de recherche. Plusieurs distributions (Fedora, Ubuntu, Arch, Debian) opèrent des serveurs `debuginfod` publics.

```bash
# Activer debuginfod dans GDB (souvent activé par défaut)
export DEBUGINFOD_URLS="https://debuginfod.ubuntu.com"

# GDB téléchargera automatiquement les symboles manquants
gdb ./hello
```

> 💡 **En RE** : Les serveurs `debuginfod` sont une ressource souvent négligée. Si votre binaire cible utilise des bibliothèques système standard, `debuginfod` peut vous fournir les informations de débogage de ces bibliothèques gratuitement et automatiquement. Cela facilite considérablement la compréhension des appels de bibliothèque dans GDB. Pensez à vérifier si un serveur `debuginfod` est disponible pour la distribution cible.

## Niveaux de détail avec `-g`

Le flag `-g` accepte des niveaux de détail qui contrôlent la quantité d'informations DWARF générées :

| Flag | Informations générées | Cas d'usage |  
|---|---|---|  
| `-g0` | Aucune information de débogage | Équivalent à ne pas mettre `-g` |  
| `-g1` | Minimal : tables de lignes et informations de backtrace, pas de variables locales ni de types | Builds de production avec backtrace lisible |  
| `-g` ou `-g2` | Standard : tout sauf les macros | Développement et débogage normal |  
| `-g3` | Complet : ajoute les définitions de macros préprocesseur (`#define`) | Débogage avancé, analyse de macros |

Le niveau `-g3` est particulièrement intéressant pour le RE : il préserve les noms et valeurs des macros préprocesseur, qui sont normalement perdues dès la phase de préprocessing (section 2.1). Dans GDB, la commande `macro expand NOM_MACRO` affiche alors la valeur de la macro.

```bash
gcc -O0 -g3 -o hello_g3 hello.c
```

En pratique, `-g3` est rarement utilisé en production. Mais si vous avez accès au système de build (audit interne), demander une recompilation avec `-g3` peut vous faire gagner un temps considérable.

## `-gsplit-dwarf` — Informations de débogage dans des fichiers `.dwo`

Pour les gros projets, GCC propose l'option `-gsplit-dwarf` qui sépare les informations DWARF dès la compilation en fichiers `.dwo` (*DWARF Object*) :

```bash
gcc -O0 -g -gsplit-dwarf -o hello hello.c  
ls hello*.dwo  
# hello.dwo
```

Le binaire principal contient uniquement un squelette DWARF (section `.debug_info` minimale avec des références vers le `.dwo`), tandis que le gros des données est dans le fichier `.dwo`. Cela accélère l'édition de liens sur les gros projets car le linker n'a pas à traiter les gigaoctets de données DWARF.

GDB retrouve les fichiers `.dwo` automatiquement grâce au chemin enregistré dans l'attribut `DW_AT_GNU_dwo_name`.

## Résumé : ce que DWARF apporte au reverse engineer

| Information DWARF | Ce qu'elle remplace | Gain en RE |  
|---|---|---|  
| Noms de fonctions (`DW_TAG_subprogram`) | Analyse des prologues et XREF | Identification instantanée |  
| Noms de paramètres et variables | Suivi manuel des registres/offsets | Lecture directe |  
| Types complets (struct, class, enum) | Reconstruction manuelle des layouts mémoire | Des heures économisées |  
| Correspondance ligne-adresse | Déduction de la logique par lecture d'assembleur | Navigation source ↔ machine |  
| Localisations de variables | Traçage dynamique avec GDB | `print variable` au lieu de `x/gx $rbp-0x18` |  
| Informations d'inlining | Deviner quelles fonctions ont été inlinées | Cartographie complète du code |  
| Chemin de compilation, flags | Inspection de `.comment` et heuristiques | Contexte de build exact |  
| Macros (`-g3`) | Irrécupérables sans source | Retrouver les constantes nommées |

**La règle d'or** : vérifiez toujours si des informations DWARF sont disponibles avant de commencer une analyse manuelle. Quelques secondes de vérification peuvent vous épargner des heures de travail :

```bash
# Vérification rapide en une commande
readelf -S binaire | grep -c '\.debug_'
# Si > 0 : jackpot — DWARF est présent
# Si 0 : vérifiez si un fichier .debug ou un paquet -dbgsym existe
```

---

> 📖 **Nous savons désormais ce que le compilateur met dans le binaire et comment les informations de débogage peuvent aider le RE.** Mais un binaire ne sert à rien tant qu'il n'est pas chargé en mémoire pour être exécuté. C'est le travail du loader Linux, que nous allons découvrir dans la section suivante.  
>  
> → 2.7 — Le Loader Linux (`ld.so`) : du fichier ELF au processus en mémoire

⏭️ [Le Loader Linux (`ld.so`) : du fichier ELF au processus en mémoire](/02-chaine-compilation-gnu/07-loader-linux.md)
