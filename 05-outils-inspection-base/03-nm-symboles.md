🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 5.3 — `nm` et `objdump -t` — inspection des tables de symboles

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Introduction

À la section précédente, `readelf -S` nous a montré l'existence de sections nommées `.symtab` et `.dynsym`. Ces sections sont les **tables de symboles** du binaire — des structures de données qui associent des noms lisibles par un humain (noms de fonctions, de variables globales, de labels) à des adresses en mémoire.

Les symboles sont le lien entre le monde du code source (où l'on raisonne en termes de `main`, `check_license`, `user_input`) et le monde du binaire (où il n'y a que des adresses comme `0x1189` ou `0x11f5`). Quand les symboles sont présents, le travail de reverse engineering est considérablement facilité : on sait immédiatement quelles fonctions existent, comment elles s'appellent, et où elles se trouvent.

Cette section présente les outils qui permettent d'extraire et d'interpréter ces tables de symboles : `nm`, l'outil spécialisé des GNU Binutils, et `objdump -t` / `readelf -s`, leurs alternatives complémentaires.

---

## Les deux tables de symboles d'un ELF

Un binaire ELF dynamiquement lié contient potentiellement **deux** tables de symboles distinctes, qui servent des objectifs différents :

### `.symtab` — la table de symboles complète

Cette table contient **tous** les symboles connus au moment du linking : fonctions locales, fonctions globales, fonctions importées, variables globales, variables statiques, labels de début de section, et même certains symboles internes du compilateur. C'est la table la plus riche et la plus utile pour le reverse engineer.

Elle est stockée dans la section `.symtab`, accompagnée de sa table de chaînes `.strtab` qui contient les noms sous forme de chaînes C (terminées par `\0`).

**Point crucial** : `.symtab` n'est **pas nécessaire à l'exécution** du programme. Elle est là uniquement pour le débogage et l'analyse. C'est pourquoi la commande `strip` la supprime sans affecter le fonctionnement du binaire. Sur un binaire strippé, `.symtab` et `.strtab` sont absentes.

### `.dynsym` — la table de symboles dynamiques

Cette table contient uniquement les symboles nécessaires au **linking dynamique** au runtime : les fonctions importées depuis des bibliothèques partagées (comme `printf`, `strcmp`, `malloc`) et les fonctions exportées par le binaire (s'il s'agit d'une bibliothèque partagée `.so`).

Elle est stockée dans la section `.dynsym`, accompagnée de `.dynstr` pour les noms.

**Point crucial** : `.dynsym` est **indispensable à l'exécution**. Sans elle, le loader dynamique ne saurait pas quelles fonctions résoudre. C'est pourquoi elle **survit au `strip`**. Même sur un binaire complètement strippé, on peut toujours lire les noms des fonctions importées depuis les bibliothèques partagées.

### Résumé des deux tables

| Table | Section | Contenu | Survit au `strip` ? | Nécessaire au runtime ? |  
|---|---|---|---|---|  
| `.symtab` | `.symtab` + `.strtab` | Tous les symboles (fonctions locales, globales, variables…) | Non | Non |  
| `.dynsym` | `.dynsym` + `.dynstr` | Symboles dynamiques (imports/exports) uniquement | Oui | Oui |

---

## `nm` — lister les symboles d'un binaire

### Utilisation de base

`nm` est l'outil canonique pour lister les symboles d'un binaire ELF. Par défaut, il affiche le contenu de `.symtab` :

```bash
$ nm keygenme_O0
                 U __cxa_finalize@GLIBC_2.2.5
                 U __libc_start_main@GLIBC_2.34
                 U printf@GLIBC_2.2.5
                 U puts@GLIBC_2.2.5
                 U strcmp@GLIBC_2.2.5
                 U strlen@GLIBC_2.2.5
0000000000004010 B __bss_start
0000000000004010 b completed.0
0000000000004000 D __data_start
0000000000004000 W data_start
00000000000010c0 T _start
0000000000001189 T main
00000000000011f5 T check_license
0000000000001280 T generate_expected_key
0000000000002000 R _IO_stdin_used
[...]
```

Chaque ligne suit le format : **adresse — type — nom**. Les symboles sans adresse (marqués par des espaces) sont des symboles **undefined** — ils seront résolus au runtime par le loader dynamique.

### Les types de symboles : décoder la lettre centrale

La lettre entre l'adresse et le nom encode le **type** du symbole. C'est l'information la plus dense et la plus importante de la sortie de `nm`. Voici les types que vous rencontrerez le plus fréquemment :

| Lettre | Signification | Section typique | Interprétation pour le RE |  
|---|---|---|---|  
| `T` | **Text** (code) — symbole global | `.text` | Fonction globale définie dans le binaire. C'est ce qu'on cherche en priorité. |  
| `t` | **Text** (code) — symbole local | `.text` | Fonction statique (`static` en C) ou fonction interne. Visible uniquement dans le fichier objet d'origine. |  
| `U` | **Undefined** | *(aucune)* | Symbole importé, non défini dans le binaire. Sera résolu par le loader dynamique. |  
| `D` | **Data** — variable initialisée, globale | `.data` | Variable globale avec une valeur initiale. |  
| `d` | **Data** — variable initialisée, locale | `.data` | Variable statique initialisée. |  
| `B` | **BSS** — variable non initialisée, globale | `.bss` | Variable globale sans valeur initiale (mise à zéro au chargement). |  
| `b` | **BSS** — variable non initialisée, locale | `.bss` | Variable statique non initialisée. |  
| `R` | **Read-only data** — globale | `.rodata` | Constante globale (chaîne, table de lookup…). |  
| `r` | **Read-only data** — locale | `.rodata` | Constante locale ou statique. |  
| `W` / `w` | **Weak** symbol | variable | Symbole faible — peut être remplacé par un symbole fort du même nom. Fréquent avec les constructeurs/destructeurs C++ et les initialiseurs. |  
| `A` | **Absolute** | *(aucune)* | Valeur fixe, non liée à une section. Rare dans les binaires utilisateurs. |

La convention majuscule/minuscule encode la **visibilité** : une lettre majuscule (`T`, `D`, `B`, `R`) indique un symbole **global** (visible de l'extérieur), une lettre minuscule (`t`, `d`, `b`, `r`) indique un symbole **local** (visible uniquement dans l'unité de compilation d'origine).

### Interpréter la sortie : ce que les symboles nous apprennent

Reprenons la sortie de `nm` sur notre crackme et voyons ce qu'on peut en déduire, sans avoir lu une seule ligne de code :

```
                 U strcmp@GLIBC_2.2.5       # Le programme compare des chaînes
                 U strlen@GLIBC_2.2.5       # Il mesure la longueur d'une chaîne
                 U printf@GLIBC_2.2.5       # Il affiche du texte formaté
                 U puts@GLIBC_2.2.5         # Il affiche des lignes de texte
0000000000001189 T main                     # Le point d'entrée logique du programme
00000000000011f5 T check_license            # ← Une fonction de vérification de licence !
0000000000001280 T generate_expected_key    # ← Une fonction qui génère la clé attendue !
```

Sans aucun désassemblage, on sait déjà :

1. Le programme a une fonction `main` à l'adresse `0x1189`.  
2. Il contient une fonction `check_license` — c'est probablement la routine de vérification à analyser.  
3. Il contient une fonction `generate_expected_key` — elle calcule vraisemblablement la clé correcte.  
4. Il utilise `strcmp` — la comparaison de la clé saisie avec la clé attendue passe probablement par cette fonction.  
5. Il utilise `strlen` — il vérifie probablement la longueur de l'input.

On a essentiellement reconstruit l'architecture du programme uniquement à partir des noms de symboles. C'est la puissance des symboles — et c'est aussi la raison pour laquelle les développeurs qui veulent protéger leurs binaires les strippent.

### Options essentielles de `nm`

```bash
# Afficher les symboles dynamiques (.dynsym) au lieu de .symtab
# Indispensable sur un binaire strippé
$ nm -D keygenme_O2_strip
                 U __cxa_finalize@GLIBC_2.2.5
                 U __libc_start_main@GLIBC_2.34
                 U printf@GLIBC_2.2.5
                 U puts@GLIBC_2.2.5
                 U strcmp@GLIBC_2.2.5
                 U strlen@GLIBC_2.2.5
```

Sur le binaire strippé, `nm` sans option échoue (ou affiche « no symbols »), car `.symtab` a été supprimée. Avec `-D`, on interroge `.dynsym` et on retrouve les fonctions importées. Les fonctions locales (`main`, `check_license`, `generate_expected_key`) ont disparu — il faudra les retrouver par d'autres moyens (désassemblage, analyse du graphe d'appels).

```bash
# Trier par adresse (au lieu de l'ordre alphabétique par défaut)
# Utile pour visualiser l'agencement des fonctions en mémoire
$ nm -n keygenme_O0
[...]
0000000000001189 T main
00000000000011f5 T check_license
0000000000001280 T generate_expected_key
00000000000012c0 T __libc_csu_init
[...]
```

Le tri par adresse (`-n` ou `--numeric-sort`) révèle **l'ordre physique des fonctions** dans la section `.text`. On voit que `main` est à `0x1189`, `check_license` commence juste après à `0x11f5`, et `generate_expected_key` suit à `0x1280`. En soustrayant les adresses, on peut estimer la taille de chaque fonction : `check_license` fait environ `0x1280 - 0x11f5 = 0x8B` = 139 octets, ce qui correspond à une fonction relativement courte.

```bash
# Afficher la taille des symboles
$ nm -S keygenme_O0 | grep ' T '
0000000000001189 000000000000006c T main
00000000000011f5 000000000000008b T check_license
0000000000001280 0000000000000035 T generate_expected_key
```

L'option `-S` (ou `--print-size`) affiche la taille de chaque symbole dans la deuxième colonne. `main` fait `0x6c` = 108 octets, `check_license` fait `0x8b` = 139 octets, `generate_expected_key` fait `0x35` = 53 octets. Ces tailles sont des indicateurs utiles : une fonction de 53 octets est très courte (peut-être un simple calcul ou une transformation), tandis qu'une fonction de plusieurs centaines d'octets contient probablement de la logique complexe avec des branchements.

```bash
# Filtrer uniquement les symboles undefined (imports)
$ nm -u keygenme_O0
                 U __cxa_finalize@GLIBC_2.2.5
                 U __libc_start_main@GLIBC_2.34
                 U printf@GLIBC_2.2.5
                 U puts@GLIBC_2.2.5
                 U strcmp@GLIBC_2.2.5
                 U strlen@GLIBC_2.2.5

# Filtrer uniquement les symboles définis (fonctions et données locales)
$ nm --defined-only keygenme_O0

# Filtrer uniquement les symboles globaux (exportés)
$ nm -g keygenme_O0

# Démanger les noms C++ (voir chapitre 7, section 7.6)
$ nm -C programme_cpp
```

L'option `-C` (ou `--demangle`) est essentielle pour les binaires C++. Les noms C++ sont **manglés** par le compilateur pour encoder la signature complète (namespace, classe, types de paramètres) dans un seul identifiant. Par exemple, `_ZN7MyClass10processKeyENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE` deviendra après démangling `MyClass::processKey(std::string)`. Sans `-C`, les tables de symboles C++ sont pratiquement illisibles.

### Récapitulatif des options `nm`

| Option | Effet | Cas d'usage |  
|---|---|---|  
| *(aucune)* | Affiche `.symtab` | Premier réflexe sur un binaire non strippé |  
| `-D` | Affiche `.dynsym` | Seule option sur un binaire strippé |  
| `-n` | Tri par adresse | Visualiser l'ordre des fonctions en mémoire |  
| `-S` | Affiche les tailles | Estimer la complexité des fonctions |  
| `-u` | Symboles undefined uniquement | Lister les imports (fonctions de bibliothèques) |  
| `--defined-only` | Symboles définis uniquement | Lister les fonctions et données locales |  
| `-g` | Symboles globaux uniquement | Lister les fonctions et variables exportées |  
| `-C` | Démangling C++ | Rendre lisibles les noms C++ manglés |

---

## `objdump -t` et `readelf -s` — alternatives pour l'inspection de symboles

### `objdump -t` — la table de symboles complète

```bash
$ objdump -t keygenme_O0

keygenme_O0:     file format elf64-x86-64

SYMBOL TABLE:
0000000000000000 l    df *ABS*  0000000000000000 Scrt1.o
000000000000038c l     O .note.ABI-tag  0000000000000020 __abi_tag
0000000000000000 l    df *ABS*  0000000000000000 crtstuff.c
00000000000010f0 l     F .text  0000000000000000 deregister_tm_clones
0000000000001120 l     F .text  0000000000000000 register_tm_clones
0000000000001160 l     F .text  0000000000000000 __do_global_dtors_aux
0000000000004010 l     O .bss   0000000000000001 completed.0
[...]
0000000000000000 l    df *ABS*  0000000000000000 keygenme.c
0000000000000000       F *UND*  0000000000000000 __cxa_finalize@GLIBC_2.2.5
0000000000000000       F *UND*  0000000000000000 printf@GLIBC_2.2.5
0000000000000000       F *UND*  0000000000000000 strcmp@GLIBC_2.2.5
0000000000000000       F *UND*  0000000000000000 strlen@GLIBC_2.2.5
0000000000000000       F *UND*  0000000000000000 puts@GLIBC_2.2.5
0000000000001189 g     F .text  000000000000006c main
00000000000011f5 g     F .text  000000000000008b check_license
0000000000001280 g     F .text  0000000000000035 generate_expected_key
[...]
```

`objdump -t` affiche plus de détails que `nm` : le format inclut des colonnes pour les flags (`l` = local, `g` = global, `F` = fonction, `O` = objet, `df` = debug/filename), la section d'appartenance, et la taille.

Un élément particulièrement intéressant apparaît ici : les lignes avec le flag `df` et la section `*ABS*` portent les **noms de fichiers sources**. On peut lire `Scrt1.o`, `crtstuff.c`, et `keygenme.c` — le nom du fichier source original. Ce détail peut sembler anodin, mais dans un vrai cas de RE, connaître le nom du fichier source aide à comprendre l'organisation du code, surtout dans un projet multi-fichiers.

```bash
# Équivalent de nm -D : uniquement les symboles dynamiques
$ objdump -T keygenme_O0

keygenme_O0:     file format elf64-x86-64

DYNAMIC SYMBOL TABLE:
0000000000000000      DF *UND*  0000000000000000 GLIBC_2.2.5  __cxa_finalize
0000000000000000      DF *UND*  0000000000000000 GLIBC_2.34   __libc_start_main
0000000000000000      DF *UND*  0000000000000000 GLIBC_2.2.5  printf
0000000000000000      DF *UND*  0000000000000000 GLIBC_2.2.5  puts
0000000000000000      DF *UND*  0000000000000000 GLIBC_2.2.5  strcmp
0000000000000000      DF *UND*  0000000000000000 GLIBC_2.2.5  strlen
```

`objdump -T` (majuscule) est l'équivalent de `nm -D`. La sortie inclut en plus la **version de la GLIBC** requise pour chaque symbole. On voit que la plupart des fonctions nécessitent `GLIBC_2.2.5` (très ancienne), mais `__libc_start_main` requiert `GLIBC_2.34` — une version beaucoup plus récente. Cette information peut être pertinente pour déterminer la compatibilité du binaire avec un système cible donné.

### `readelf -s` — la sortie la plus détaillée

```bash
$ readelf -s keygenme_O0

Symbol table '.dynsym' contains 10 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __cxa_finalize@GLIBC_2.2.5
     2: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __libc_start_main@GLIBC_2.34
     3: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND printf@GLIBC_2.2.5
     4: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND puts@GLIBC_2.2.5
     5: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND strcmp@GLIBC_2.2.5
     6: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND strlen@GLIBC_2.2.5
     [...]

Symbol table '.symtab' contains 43 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     [...]
    26: 0000000000001189   108 FUNC    GLOBAL DEFAULT   14 main
    27: 00000000000011f5   139 FUNC    GLOBAL DEFAULT   14 check_license
    28: 0000000000001280    53 FUNC    GLOBAL DEFAULT   14 generate_expected_key
     [...]
```

`readelf -s` est la commande la plus explicite. Chaque champ est nommé dans l'en-tête de colonne, ce qui la rend auto-documentée. Elle affiche séparément `.dynsym` et `.symtab`, avec les informations suivantes :

**`Type`** — `FUNC` (fonction), `OBJECT` (variable/donnée), `NOTYPE` (pas de type défini), `SECTION` (début d'une section), `FILE` (nom du fichier source).

**`Bind`** — `LOCAL` (visible uniquement dans le fichier objet d'origine), `GLOBAL` (visible partout), `WEAK` (peut être remplacé par un symbole global du même nom).

**`Vis`** — la visibilité ELF : `DEFAULT` (visible normalement), `HIDDEN` (non exporté par une bibliothèque partagée même s'il est global), `PROTECTED`, `INTERNAL`. Dans la majorité des cas, c'est `DEFAULT`.

**`Ndx`** — l'index de la section contenant le symbole. `UND` signifie undefined (symbole importé). Un numéro comme `14` renvoie à une section — dans notre cas, la section 14 est `.text`, ce qui confirme que `main`, `check_license` et `generate_expected_key` sont bien du code exécutable.

### Comparaison des trois outils sur les symboles

| Aspect | `nm` | `objdump -t` / `-T` | `readelf -s` |  
|---|---|---|---|  
| Format de sortie | Compact (3 colonnes) | Détaillé (flags, section, taille) | Très détaillé (colonnes nommées) |  
| `.symtab` | Par défaut | `-t` | `-s` (affiche les deux tables) |  
| `.dynsym` | `-D` | `-T` | `-s` (affiche les deux tables) |  
| Taille des symboles | `-S` | Incluse par défaut | Incluse par défaut |  
| Noms de fichiers sources | Non affiché | Affiché (flag `df`) | Affiché (type `FILE`) |  
| Démangling C++ | `-C` | `-C` | Non intégré (piper vers `c++filt`) |  
| Scriptabilité | Excellente (sortie simple) | Bonne | Bonne (colonnes fixes) |  
| Gestion ELF malformés | Via BFD | Via BFD | Parsing direct ELF (plus robuste) |

---

## Techniques pratiques avec les symboles

### Lister uniquement les fonctions du programme

En combinant `nm` avec `grep`, on peut isoler rapidement les fonctions qui appartiennent au code du programme, en éliminant le bruit des symboles internes du compilateur et du runtime :

```bash
# Toutes les fonctions globales définies dans .text
$ nm -n keygenme_O0 | grep ' T '
00000000000010c0 T _start
0000000000001189 T main
00000000000011f5 T check_license
0000000000001280 T generate_expected_key
00000000000012c0 T __libc_csu_init
0000000000001330 T __libc_csu_fini

# Pareil, mais en excluant les fonctions du runtime C
$ nm -n keygenme_O0 | grep ' T ' | grep -v -E '(_start|_init|_fini|__libc|_IO_)'
0000000000001189 T main
00000000000011f5 T check_license
0000000000001280 T generate_expected_key
```

En deux commandes, on a isolé les trois fonctions qui constituent la logique métier du programme. Sur un binaire plus complexe avec des dizaines ou des centaines de fonctions, ce filtrage est indispensable pour ne pas se noyer dans le bruit.

### Chercher un symbole spécifique

Quand on suspecte qu'un binaire utilise une fonction particulière — par exemple une fonction de chiffrement — on peut chercher directement :

```bash
# Chercher des fonctions liées au chiffrement
$ nm keygenme_O0 | grep -iE '(crypt|aes|sha|md5|encrypt|decrypt)'

# Chercher des fonctions réseau
$ nm keygenme_O0 | grep -iE '(socket|connect|send|recv|bind|listen|accept)'

# Chercher des fonctions de manipulation de fichiers
$ nm keygenme_O0 | grep -iE '(fopen|fread|fwrite|open|read|write|mmap)'
```

L'absence de résultat est elle-même une information : si aucun symbole réseau n'apparaît, le binaire ne fait probablement pas de communication réseau (ou alors via des appels système directs, ce que `strace` révélera à la section 5.5).

### Comparer les symboles entre deux versions d'un binaire

En combinant `nm` avec `diff` ou `comm`, on peut identifier rapidement les fonctions ajoutées, supprimées ou modifiées entre deux versions :

```bash
# Extraire les fonctions de chaque version
$ nm --defined-only -n keygenme_v1 | grep ' T ' > /tmp/syms_v1.txt
$ nm --defined-only -n keygenme_v2 | grep ' T ' > /tmp/syms_v2.txt

# Comparer
$ diff /tmp/syms_v1.txt /tmp/syms_v2.txt
```

Cette technique est un premier niveau de diffing binaire, bien avant l'utilisation d'outils spécialisés comme BinDiff (chapitre 10).

---

## Ce qui se passe sur un binaire strippé

Résumé de la situation sur un binaire strippé, un scénario que vous rencontrerez très fréquemment en conditions réelles :

```bash
$ nm keygenme_O2_strip
nm: keygenme_O2_strip: no symbols

$ nm -D keygenme_O2_strip
                 U __cxa_finalize@GLIBC_2.2.5
                 U __libc_start_main@GLIBC_2.34
                 U printf@GLIBC_2.2.5
                 U puts@GLIBC_2.2.5
                 U strcmp@GLIBC_2.2.5
                 U strlen@GLIBC_2.2.5
```

`.symtab` est absente : `nm` sans option ne trouve rien. Avec `-D`, on accède à `.dynsym` et on voit les fonctions importées, mais les fonctions locales (`main`, `check_license`, `generate_expected_key`) ont entièrement disparu. On sait que le programme utilise `strcmp` et `strlen`, mais on ne sait plus où ni comment.

C'est là que le travail de reverse engineering commence véritablement : retrouver ces fonctions par l'analyse du code machine, les nommer manuellement dans un désassembleur, et reconstruire la logique du programme. Les symboles dynamiques restants sont des indices précieux — ils nous disent quoi chercher, même s'ils ne nous disent plus où.

Pour localiser `main` dans un binaire strippé, une technique classique consiste à chercher l'appel à `__libc_start_main` dans le code de `_start` : le troisième argument passé à `__libc_start_main` est l'adresse de `main`. Nous verrons cette technique en détail au chapitre 7 (section 7.5).

---

## Ce qu'il faut retenir pour la suite

- **`nm` est le premier outil de reconnaissance fonctionnelle**. Avant de désassembler, listez les symboles. Les noms de fonctions sont le meilleur indice sur l'architecture d'un programme.  
- **Majuscule = global, minuscule = local** dans les types `nm`. `T` = fonction globale dans `.text`, `U` = undefined (import), `B`/`D`/`R` = données dans `.bss`/`.data`/`.rodata`.  
- **`nm -D` est votre plan B** sur un binaire strippé. Les symboles dynamiques survivent au `strip` et révèlent les bibliothèques utilisées.  
- **`nm -n`** trie par adresse et montre l'agencement physique des fonctions — utile pour estimer leurs tailles et comprendre le layout du code.  
- **`nm -C`** est indispensable en C++ — sans démangling, les symboles C++ sont illisibles.  
- Les **symboles importés** (`U`) sont des indices fonctionnels : `strcmp` = comparaison de chaînes, `socket` = réseau, `EVP_EncryptInit` = chiffrement OpenSSL. Apprenez à les reconnaître.  
- Sur un binaire strippé, la perte de `.symtab` signifie que toute la richesse des noms de fonctions locales a disparu. Il reste `.dynsym` et les chaînes de `.rodata` — les deux fils d'Ariane que nous exploiterons avec le désassembleur.

---


⏭️ [`ldd` et `ldconfig` — dépendances dynamiques et résolution](/05-outils-inspection-base/04-ldd-ldconfig.md)
