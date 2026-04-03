🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.1 — Compilation avec symboles de débogage (`-g`, DWARF)

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Le problème fondamental : le fossé source ↔ binaire

Quand GCC transforme un fichier `.c` en binaire exécutable, il effectue une série de transformations irréversibles. Les noms de variables locales disparaissent — ils n'ont aucune utilité pour le processeur. Les numéros de lignes du code source sont perdus. Les types des données (`int`, `char *`, `struct Player`) sont réduits à de simples tailles et offsets mémoire. Les paramètres de fonctions deviennent des valeurs dans des registres. À la fin du processus, le binaire ne contient que du code machine et des données brutes : le processeur n'a besoin de rien d'autre.

Ce fossé entre le code source et le binaire est précisément ce qui rend le reverse engineering difficile — et c'est aussi ce qui rend le débogage pénible sans informations supplémentaires. Si vous posez un breakpoint à l'adresse `0x401156` et que GDB vous montre une instruction `cmp eax, 0x2a`, vous ne savez pas que cette instruction correspond à la ligne 47 de `main.c`, que `eax` contient la variable `user_input`, et que `0x2a` est la constante `EXPECTED_VALUE` définie dans un `#define`.

Les **symboles de débogage** existent pour combler ce fossé. Ce sont des métadonnées ajoutées au binaire qui permettent au débogueur de faire le lien entre le monde du code machine et le monde du code source. Elles ne changent pas le code exécuté — les mêmes instructions sont générées avec ou sans symboles — mais elles fournissent à GDB la carte qui lui manque pour naviguer.

## Le flag `-g` : demander à GCC de générer les symboles

La génération de symboles de débogage se fait à la compilation avec le flag `-g` :

```bash
# Sans symboles de débogage
gcc -o programme programme.c

# Avec symboles de débogage
gcc -g -o programme_debug programme.c
```

Ce flag demande à GCC d'inclure dans le binaire ELF des sections supplémentaires contenant les informations de débogage. Le code machine généré reste identique — `-g` n'affecte ni les instructions, ni les optimisations, ni les performances à l'exécution. Il augmente uniquement la taille du fichier sur disque.

On peut le vérifier facilement :

```bash
$ gcc -O0 -o keygenme keygenme.c
$ gcc -O0 -g -o keygenme_debug keygenme.c
$ ls -lh keygenme keygenme_debug
-rwxr-xr-x 1 user user  17K  keygenme
-rwxr-xr-x 1 user user  51K  keygenme_debug
```

Le binaire avec symboles est environ trois fois plus gros, mais les sections `.text` (code) et `.data` (données) sont rigoureusement identiques. L'écart de taille vient entièrement des sections de débogage ajoutées.

### Niveaux de détail : `-g`, `-g1`, `-g2`, `-g3`

Le flag `-g` seul est équivalent à `-g2`, le niveau par défaut. GCC propose plusieurs niveaux de détail :

| Flag | Contenu généré |  
|---|---|  
| `-g0` | Aucun symbole (équivalent à ne pas mettre `-g`) |  
| `-g1` | Informations minimales : noms de fonctions et fonctions externes, suffisant pour les stack traces mais sans variables locales ni numéros de lignes |  
| `-g2` (`-g`) | Niveau standard : noms et types des fonctions, variables locales et paramètres, numéros de lignes, correspondance source ↔ adresse |  
| `-g3` | Niveau maximal : tout `-g2` plus les définitions de macros (`#define`), permettant d'évaluer des macros dans GDB |

En contexte de RE, c'est `-g2` (ou `-g`) qui est le niveau de référence. Le niveau `-g3` est utile quand on débogue son propre code avec beaucoup de macros, mais les binaires que vous rencontrerez en situation réelle n'auront jamais été compilés avec.

### Combiner `-g` avec les optimisations

Un point crucial : **`-g` est compatible avec tous les niveaux d'optimisation**. On peut tout à fait écrire :

```bash
gcc -O2 -g -o programme programme.c
```

Le binaire sera optimisé exactement comme avec `-O2` seul, mais contiendra en plus les symboles de débogage. Cependant, l'expérience de débogage se dégrade significativement avec les optimisations, pour plusieurs raisons :

- **Variables « optimized out ».** Le compilateur peut décider qu'une variable n'a pas besoin d'exister en mémoire : sa valeur est gardée dans un registre, ou elle est complètement éliminée par propagation de constantes. GDB affichera `<optimized out>` quand on tentera de la lire.

- **Instructions réordonnées.** L'exécution ne suit plus l'ordre des lignes du source. Un `step` dans GDB peut sauter de la ligne 12 à la ligne 18, puis revenir à la ligne 14. Les symboles DWARF tentent de suivre ces réarrangements, mais le résultat est souvent déroutant.

- **Fonctions inlinées.** Si le compilateur inline une fonction, elle n'existe plus en tant qu'entité séparée dans le binaire. GDB peut signaler qu'on se trouve « dans » la fonction inlinée grâce aux informations DWARF, mais on ne peut pas poser un breakpoint sur son entrée de la manière habituelle.

C'est pourquoi, pour les binaires d'entraînement de cette formation, nous travaillons d'abord avec `-O0 -g` (aucune optimisation, symboles complets) avant de passer aux variantes optimisées.

## Le format DWARF

Les symboles de débogage ne sont pas stockés dans un format improvisé. GCC utilise le format standard **DWARF** (*Debugging With Attributed Record Formats*), qui est le format de débogage de référence pour les binaires ELF sous Linux. DWARF est actuellement à sa version 5 (DWARF5, publié en 2017), mais GCC génère par défaut du DWARF4 ou DWARF5 selon la version du compilateur.

On peut forcer une version spécifique :

```bash
gcc -g -gdwarf-4 -o programme programme.c   # Force DWARF version 4  
gcc -g -gdwarf-5 -o programme programme.c   # Force DWARF version 5  
```

### Ce que DWARF contient

DWARF est un format riche et structuré. Il organise les informations sous forme d'un arbre d'entrées appelées **DIE** (*Debugging Information Entry*). Chaque DIE décrit un élément du programme source et possède un **tag** (son type) et des **attributs** (ses propriétés). Voici les catégories principales d'informations que DWARF encode :

**Correspondance adresse ↔ ligne source.** Pour chaque adresse dans `.text`, DWARF peut indiquer le fichier source, le numéro de ligne et la colonne correspondants. C'est ce qui permet à GDB d'afficher le code source pendant le débogage et de poser des breakpoints sur des numéros de lignes (`break main.c:42`).

**Description des fonctions.** Pour chaque fonction, DWARF enregistre son nom, son adresse de début et de fin, ses paramètres (noms, types, emplacements — registre ou pile), ses variables locales (mêmes informations), et sa convention d'appel.

**Description des types.** DWARF encode l'intégralité du système de types du programme : types de base (`int`, `char`, `float`), pointeurs, tableaux, structures (`struct`), unions, énumérations, et en C++ les classes avec leurs méthodes, héritage et qualificateurs d'accès. C'est grâce à ces informations que GDB peut afficher une structure complète quand on tape `print *player` au lieu de montrer un bloc d'octets bruts.

**Informations de portée.** DWARF décrit les blocs lexicaux (les `{}` en C), ce qui permet à GDB de savoir qu'une variable `i` déclarée dans un `for` n'est visible que dans cette boucle.

**Emplacements des variables (location expressions).** C'est l'un des aspects les plus sophistiqués de DWARF. L'emplacement d'une variable peut changer au cours de l'exécution d'une fonction : elle peut être dans le registre `rdi` à l'entrée de la fonction, puis être sauvegardée sur la pile à l'offset `rbp-0x10`, puis être déplacée dans `rax` pour servir de valeur de retour. DWARF utilise un mini-langage à pile (*DWARF expressions*) pour décrire ces emplacements variables, et c'est ce langage que GDB interprète pour retrouver la bonne valeur au bon moment.

### Les sections ELF générées par DWARF

Les informations DWARF sont stockées dans des sections ELF dédiées, toutes préfixées par `.debug_`. On peut les lister avec `readelf` :

```bash
$ readelf -S keygenme_debug | grep debug
  [27] .debug_aranges    PROGBITS     0000000000000000  00003041  00000030
  [28] .debug_info       PROGBITS     0000000000000000  00003071  00000198
  [29] .debug_abbrev     PROGBITS     0000000000000000  00003209  000000c7
  [30] .debug_line       PROGBITS     0000000000000000  000032d0  0000008e
  [31] .debug_str        PROGBITS     0000000000000000  0000335e  000000fb
  [32] .debug_line_str   PROGBITS     0000000000000000  00003459  00000032
```

Voici le rôle de chaque section principale :

| Section | Contenu |  
|---|---|  
| `.debug_info` | Le cœur de DWARF : l'arbre de DIE décrivant fonctions, variables, types, portées |  
| `.debug_abbrev` | Table d'abréviations qui compacte `.debug_info` (les tags et attributs fréquents sont encodés par un numéro) |  
| `.debug_line` | La table de correspondance adresse → ligne source (le « line number program ») |  
| `.debug_str` | Pool de chaînes de caractères référencées par `.debug_info` (noms de fonctions, variables, fichiers) |  
| `.debug_aranges` | Index rapide : plages d'adresses → unités de compilation, pour accélérer les recherches |  
| `.debug_loc` | Listes d'emplacements (*location lists*) pour les variables dont l'emplacement change |  
| `.debug_ranges` | Plages d'adresses non contiguës pour les fonctions et portées (utile avec les optimisations) |  
| `.debug_frame` | Informations de déroulement de pile (*call frame information*), utilisées pour reconstruire la pile d'appels |

> 💡 **Note :** La section `.debug_frame` est distincte de `.eh_frame` (vue au chapitre 2). Les deux contiennent des informations de déroulement de pile, mais `.eh_frame` est utilisée par le mécanisme d'exceptions C++ à l'exécution et est toujours présente, même sans `-g`. La section `.debug_frame` est plus détaillée et réservée au débogueur.

### Inspecter les informations DWARF

Plusieurs outils permettent de lire le contenu DWARF d'un binaire. Le plus courant est `readelf` avec le flag `--debug-dump` (ou sa forme abrégée `-w`) :

```bash
# Afficher les DIE de .debug_info
$ readelf --debug-dump=info keygenme_debug
```

La sortie est verbeuse. Voici un extrait simplifié montrant la description d'une fonction `check_key` :

```
 <1><8f>: Abbrev Number: 5 (DW_TAG_subprogram)
    <90>   DW_AT_name        : check_key
    <9a>   DW_AT_decl_file   : 1
    <9b>   DW_AT_decl_line   : 23
    <9c>   DW_AT_type        : <0x62>
    <a0>   DW_AT_low_pc      : 0x401156
    <a8>   DW_AT_high_pc     : 0x4d
    <ac>   DW_AT_frame_base  : 1 byte block: 56    (DW_OP_reg6 (rbp))
 <2><ae>: Abbrev Number: 6 (DW_TAG_formal_parameter)
    <af>   DW_AT_name        : input
    <b5>   DW_AT_decl_line   : 23
    <b6>   DW_AT_type        : <0x7b>
    <ba>   DW_AT_location    : 2 byte block: 91 68  (DW_OP_fbreg: -24)
```

Décortiquons cette sortie :

- `DW_TAG_subprogram` indique une fonction. Son nom (`DW_AT_name`) est `check_key`, déclarée à la ligne 23 (`DW_AT_decl_line`) du premier fichier source.  
- `DW_AT_low_pc` et `DW_AT_high_pc` donnent la plage d'adresses de la fonction : elle commence à `0x401156` et s'étend sur `0x4d` octets (77 octets).  
- `DW_AT_frame_base` indique que le pointeur de frame est dans `rbp` (`DW_OP_reg6`).  
- Le `DW_TAG_formal_parameter` décrit le paramètre `input`. Son emplacement (`DW_AT_location`) est `DW_OP_fbreg: -24`, ce qui signifie « à l'offset -24 par rapport au frame base (`rbp`) », soit `rbp - 0x18`.

Pour la table de correspondance lignes/adresses :

```bash
$ readelf --debug-dump=decodedline keygenme_debug

File name         Line number    Starting address    View    Stmt  
keygenme.c                 23          0x401156               x  
keygenme.c                 24          0x40116a               x  
keygenme.c                 25          0x401172               x  
keygenme.c                 28          0x401183               x  
keygenme.c                 29          0x401190               x  
```

Chaque entrée associe un fichier, un numéro de ligne et une adresse mémoire. C'est exactement ce que GDB utilise quand on tape `break keygenme.c:25` — il consulte cette table pour trouver l'adresse `0x401172` et y poser le breakpoint.

L'outil `objdump` offre une vue alternative avec le flag `-WL` (ou `--dwarf=decodedline`), et l'utilitaire dédié `dwarfdump` (paquet `libdwarf-tools` ou `dwarfdump` selon la distribution) fournit une sortie encore plus détaillée.

## L'impact du stripping sur les symboles

Rappelons la distinction entre deux types de « symboles » qui sont souvent confondus :

**La table de symboles ELF** (`.symtab` / `.dynsym`) contient les noms des fonctions et des variables globales. C'est ce que `nm` affiche et ce que `strip` supprime. Ces symboles permettent à GDB de résoudre `break check_key` en une adresse, mais ils ne contiennent ni les types, ni les variables locales, ni les numéros de lignes.

**Les sections DWARF** (`.debug_*`) contiennent les informations de débogage complètes décrites ci-dessus. Elles sont beaucoup plus riches que la table de symboles.

La commande `strip` supprime **les deux** :

```bash
$ strip keygenme_debug -o keygenme_stripped
$ readelf -S keygenme_stripped | grep -E "symtab|debug"
# (aucune sortie — tout a été supprimé)
```

On peut aussi supprimer uniquement les symboles de débogage en gardant la table de symboles :

```bash
$ strip --strip-debug keygenme_debug -o keygenme_nodebug
$ nm keygenme_nodebug | head
0000000000401156 T check_key    # ← la table de symboles est conservée
```

En pratique, les binaires que vous rencontrerez en RE sont dans l'un de ces trois états :

| État | `.symtab` | `.debug_*` | Commande de compilation |  
|---|---|---|---|  
| Debug complet | ✅ | ✅ | `gcc -g -O0` |  
| Release standard | ✅ | ❌ | `gcc -O2` |  
| Strippé | ❌ | ❌ | `gcc -O2 -s` ou `strip` après compilation |

La majorité des binaires « dans la nature » (logiciels distribués, malwares, firmwares) sont strippés. C'est la situation la plus courante et la plus difficile — nous verrons comment y faire face dans la section 11.4.

## Symboles de débogage séparés

Il existe une pratique courante dans les distributions Linux : les symboles de débogage sont distribués dans des **paquets séparés** (souvent suffixés `-dbg` ou `-dbgsym`). Le binaire installé est strippé pour économiser de l'espace, et les symboles sont disponibles à la demande.

```bash
# Exemple sur Debian/Ubuntu
sudo apt install libc6-dbg       # Symboles de débogage pour la glibc
```

GDB sait charger automatiquement ces symboles s'ils sont installés. Il cherche dans des chemins standardisés comme `/usr/lib/debug/`. On peut aussi charger manuellement un fichier de symboles :

```bash
(gdb) symbol-file /chemin/vers/programme.debug
```

Ou utiliser un fichier de symboles séparé créé avec `objcopy` :

```bash
# Extraire les symboles dans un fichier séparé
$ objcopy --only-keep-debug programme programme.debug

# Stripper le binaire
$ strip programme

# Ajouter un lien vers le fichier de symboles
$ objcopy --add-gnu-debuglink=programme.debug programme
```

Avec cette configuration, GDB chargera automatiquement `programme.debug` quand il ouvrira `programme`, à condition que le fichier soit dans le même répertoire ou dans le chemin de recherche des symboles.

> 💡 **Pour le RE :** cette technique est utile dans l'autre sens. Si vous analysez un binaire provenant d'une distribution Linux (par exemple un démon système), installer le paquet `-dbgsym` correspondant vous donnera un accès complet aux symboles de débogage, transformant une session GDB pénible en une expérience confortable. Pensez-y systématiquement avant de commencer l'analyse.

## Vérifier l'état d'un binaire avant de lancer GDB

Avant d'ouvrir GDB, prenez l'habitude de vérifier rapidement ce que contient le binaire en termes de symboles. Voici la routine :

```bash
# 1. Le binaire a-t-il une table de symboles ?
$ nm programme 2>&1 | head -3
# Si "no symbols" → strippé

# 2. Le binaire contient-il des sections DWARF ?
$ readelf -S programme | grep debug
# Si aucune ligne → pas de symboles de débogage

# 3. Quel format et quelle version de DWARF ?
$ readelf --debug-dump=info programme 2>/dev/null | head -5
# Affiche "Compilation Unit @ offset 0x0:" avec la version DWARF

# 4. Résumé rapide avec file
$ file programme
programme: ELF 64-bit LSB pie executable, x86-64, [...], not stripped
# "not stripped" = table de symboles présente
# "stripped" = table de symboles absente
# (file ne dit rien sur DWARF — il faut readelf pour ça)
```

Cette vérification en quatre commandes prend quelques secondes et conditionne votre approche dans GDB : avec des symboles DWARF, vous travaillerez confortablement avec des noms de fonctions, des numéros de lignes et des types. Sans symboles, vous travaillerez en adresses brutes et en registres — ce qui est tout à fait faisable, mais demande une méthodologie différente (section 11.4).

## Ce que GDB fait avec les informations DWARF

Pour conclure cette section et faire le lien avec la suite, voici concrètement comment GDB exploite chaque catégorie d'information DWARF au quotidien :

| Vous tapez dans GDB... | GDB utilise... |  
|---|---|  
| `break check_key` | `.symtab` pour résoudre le nom en adresse |  
| `break keygenme.c:25` | `.debug_line` pour trouver l'adresse correspondant à la ligne 25 |  
| `next` (avancer d'une ligne source) | `.debug_line` pour calculer jusqu'où avancer |  
| `print input` | `.debug_info` pour trouver le type et l'emplacement de la variable |  
| `print *player` | `.debug_info` pour connaître la structure du type pointé et ses champs |  
| `backtrace` | `.debug_frame` + `.debug_info` pour reconstruire et nommer la pile d'appels |  
| `list` (afficher le source) | `.debug_line` pour la correspondance adresse → fichier:ligne, puis lecture du fichier source sur disque |

Ce dernier point est important : GDB n'inclut pas le code source dans le binaire. La section `.debug_line` contient les chemins vers les fichiers source tels qu'ils existaient au moment de la compilation. Si ces fichiers ne sont pas présents au même emplacement sur votre machine, la commande `list` échouera — mais toutes les autres fonctionnalités (breakpoints par ligne, affichage de variables, backtrace) continueront de fonctionner normalement, car elles ne dépendent que des métadonnées DWARF embarquées dans le binaire.

---

> **À retenir :** Les symboles de débogage DWARF sont la carte qui relie le code machine au code source. Ils ne modifient pas l'exécution du programme, mais transforment radicalement l'expérience de débogage. En RE, vérifier leur présence est la première chose à faire avant d'ouvrir GDB — et quand ils sont absents, il faut adapter sa méthode, pas abandonner.

⏭️ [Commandes GDB fondamentales : `break`, `run`, `next`, `step`, `info`, `x`, `print`](/11-gdb/02-commandes-fondamentales.md)
