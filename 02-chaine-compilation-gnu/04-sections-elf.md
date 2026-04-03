🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.4 — Sections ELF clés : `.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`, `.init`, `.fini`

> 🎯 **Objectif de cette section** : Connaître le rôle de chaque section importante d'un binaire ELF, savoir quel type d'information y chercher lors d'une analyse RE, et comprendre comment le linker organise ces sections en segments chargeables en mémoire.

---

## Le concept de section dans ELF

En section 2.3, nous avons vu qu'un fichier ELF offre deux vues : la vue en **sections** (pour le linker et les outils d'analyse) et la vue en **segments** (pour le loader). Ici, nous nous concentrons sur les sections — ce sont elles que vous manipulerez quotidiennement dans Ghidra, objdump, readelf ou Radare2.

Une section est une **zone contiguë du fichier** identifiée par un nom, un type, des flags et une taille. Chaque section a un rôle précis : contenir du code exécutable, des données en lecture seule, des données modifiables, des métadonnées de liaison, des informations de débogage, etc. Le nom d'une section est une convention (rien n'empêche techniquement de renommer `.text` en `.moncode`), mais les outils et le loader s'appuient sur ces conventions.

Pour lister les sections de notre `hello` :

```bash
readelf -S hello
```

Sortie typique (simplifiée — un binaire ELF dynamique courant contient entre 25 et 35 sections) :

| Nr | Nom | Type | Flags | Rôle résumé |  
|---|---|---|---|---|  
| 0 | *(null)* | NULL | — | Entrée obligatoire à index 0 |  
| 1 | `.interp` | PROGBITS | A | Chemin du loader dynamique |  
| 2 | `.note.gnu.build-id` | NOTE | A | Identifiant unique du build |  
| 3 | `.gnu.hash` | GNU_HASH | A | Table de hachage pour les symboles dynamiques |  
| 4 | `.dynsym` | DYNSYM | A | Table des symboles dynamiques |  
| 5 | `.dynstr` | STRTAB | A | Noms des symboles dynamiques |  
| 6 | `.rela.dyn` | RELA | A | Relocations pour les données |  
| 7 | `.rela.plt` | RELA | AI | Relocations pour les appels PLT |  
| 8 | `.init` | PROGBITS | AX | Code d'initialisation |  
| 9 | `.plt` | PROGBITS | AX | Procedure Linkage Table |  
| 10 | `.text` | PROGBITS | AX | Code exécutable principal |  
| 11 | `.fini` | PROGBITS | AX | Code de finalisation |  
| 12 | `.rodata` | PROGBITS | A | Données en lecture seule |  
| 13 | `.eh_frame_hdr` | PROGBITS | A | Index des frames d'exception |  
| 14 | `.eh_frame` | PROGBITS | A | Informations de déroulement de pile |  
| 15 | `.init_array` | INIT_ARRAY | WA | Pointeurs vers les constructeurs |  
| 16 | `.fini_array` | FINI_ARRAY | WA | Pointeurs vers les destructeurs |  
| 17 | `.dynamic` | DYNAMIC | WA | Table d'informations pour le loader |  
| 18 | `.got` | PROGBITS | WA | Global Offset Table (données) |  
| 19 | `.got.plt` | PROGBITS | WA | Global Offset Table (fonctions PLT) |  
| 20 | `.data` | PROGBITS | WA | Données initialisées modifiables |  
| 21 | `.bss` | NOBITS | WA | Données non initialisées |  
| 22 | `.comment` | PROGBITS | MS | Version du compilateur |  
| 23 | `.symtab` | SYMTAB | — | Table de symboles complète |  
| 24 | `.strtab` | STRTAB | — | Noms de la table de symboles |  
| 25 | `.shstrtab` | STRTAB | — | Noms des sections |

Les **flags** décrivent les propriétés de chaque section :

| Flag | Lettre | Signification |  
|---|---|---|  
| `SHF_ALLOC` | `A` | La section occupe de la mémoire à l'exécution |  
| `SHF_WRITE` | `W` | La section est modifiable en mémoire |  
| `SHF_EXECINSTR` | `X` | La section contient du code exécutable |  
| `SHF_MERGE` | `M` | Les entrées peuvent être fusionnées (déduplication) |  
| `SHF_STRINGS` | `S` | La section contient des chaînes terminées par `\0` |  
| `SHF_INFO_LINK` | `I` | Le champ `sh_info` référence une autre section |

La combinaison des flags traduit directement les **permissions mémoire** que le loader appliquera. Par exemple, `AX` (alloc + exec) signifie que la zone sera chargée en mémoire avec les droits lecture + exécution, mais **pas** écriture. C'est le principe **W⊕X** (*Write XOR Execute*) : une zone mémoire est soit inscriptible, soit exécutable, jamais les deux — une protection fondamentale contre l'injection de code (section 2.8 et Chapitre 19).

## Les sections de code

### `.text` — Le code exécutable

C'est la section la plus importante pour le reverse engineer. Elle contient l'intégralité du **code machine** du programme : toutes les fonctions compilées, y compris `main()`, les fonctions de l'utilisateur, et les fonctions inlinées.

**Flags** : `AX` (Alloc + Exec) — chargée en mémoire, exécutable, non inscriptible.

Quand vous ouvrez un binaire dans un désassembleur, c'est le contenu de `.text` qui est affiché en priorité. Le point d'entrée du programme (`e_entry` dans l'en-tête ELF) pointe vers une adresse dans `.text` — généralement la fonction `_start` du code CRT, et non directement vers `main()`.

```bash
# Désassembler uniquement la section .text
objdump -d -j .text hello
```

> 💡 **En RE** : Si un binaire est strippé, les limites entre les fonctions dans `.text` ne sont plus marquées par des symboles. Le désassembleur doit alors recourir à des heuristiques pour identifier les débuts de fonctions (recherche de prologues `push rbp; mov rbp, rsp`, analyse du graphe de flot de contrôle…). C'est l'une des raisons pour lesquelles un outil comme Ghidra est supérieur à `objdump` pour l'analyse de binaires strippés (Chapitre 7, section 7.7).

### `.plt` — Procedure Linkage Table

La PLT contient de petits morceaux de code (des *stubs*) qui servent de trampoline pour les appels aux fonctions de bibliothèques dynamiques. Chaque fonction importée (par exemple `strcmp`, `printf`, `puts`) possède un stub PLT dédié.

**Flags** : `AX` (Alloc + Exec).

Quand votre code appelle `strcmp`, le `call` pointe en réalité vers `strcmp@plt` dans la section `.plt`. Ce stub effectue un saut indirect via la GOT (`.got.plt`) pour atteindre la véritable implémentation de `strcmp` dans `libc.so`. Ce mécanisme de double indirection est la base du **lazy binding** — nous le détaillerons en section 2.9.

```bash
# Voir le contenu de la PLT
objdump -d -j .plt hello
```

Sortie typique pour le stub de `strcmp` :

```
Disassembly of section .plt:

0000000000001020 <strcmp@plt>:
    1020:   ff 25 e2 2f 00 00    jmp    *0x2fe2(%rip)    # 4008 <strcmp@GLIBC_2.2.5>
    1026:   68 00 00 00 00       push   $0x0
    102b:   e9 e0 ff ff ff       jmp    1010 <_init+0x10>
```

> 💡 **En RE** : La PLT est une mine d'or. Même dans un binaire strippé, les noms des fonctions importées sont préservés dans les symboles dynamiques (`.dynsym`). Quand vous voyez `call 0x1020` dans `.text` et que `0x1020` correspond à `strcmp@plt`, vous savez immédiatement que le code appelle `strcmp`. C'est souvent le premier repère fiable dans un binaire inconnu.

### `.init` et `.fini` — Initialisation et finalisation

Ces deux sections contiennent du code exécuté respectivement **avant** et **après** `main()` :

- **`.init`** : code d'initialisation exécuté par le loader dynamique avant que `main()` soit appelé. Il est généré par le linker à partir du CRT (C Runtime).  
- **`.fini`** : code de finalisation exécuté après le retour de `main()` (ou après un appel à `exit()`).

**Flags** : `AX` (Alloc + Exec).

Deux sections complémentaires, **`.init_array`** et **`.fini_array`**, contiennent des tableaux de pointeurs de fonctions. Chaque pointeur dans `.init_array` désigne une fonction constructeur à appeler avant `main()`, et chaque pointeur dans `.fini_array` désigne un destructeur à appeler après `main()`. En C, vous pouvez déclarer de telles fonctions avec l'attribut GCC `__attribute__((constructor))` et `__attribute__((destructor))`. En C++, les constructeurs de variables globales sont enregistrés dans `.init_array`.

```bash
# Voir le contenu de .init_array (tableau de pointeurs)
objdump -s -j .init_array hello
```

> 💡 **En RE** : Les sections `.init_array` et `.fini_array` sont des cibles d'intérêt dans l'analyse de malware. Un code malveillant peut enregistrer une fonction constructeur pour exécuter du code *avant* `main()`, ce qui peut passer inaperçu si l'analyste se concentre uniquement sur `main()`. Vérifiez systématiquement ces sections (Chapitres 27–28).

## Les sections de données

### `.rodata` — Données en lecture seule

Cette section contient toutes les données constantes du programme : chaînes littérales, constantes numériques, tables de correspondance (`switch/case` compilés en jump tables), et toute donnée marquée `const` en C/C++.

**Flags** : `A` (Alloc, sans Write ni Exec) — chargée en mémoire, lecture seule.

Pour notre `hello.c`, cette section contient les chaînes `"RE-101"`, `"Usage: %s <mot de passe>\n"`, `"Accès autorisé."` et `"Accès refusé."` :

```bash
# Afficher le contenu brut de .rodata
objdump -s -j .rodata hello
```

> 💡 **En RE** : La section `.rodata` est la première à examiner après `.text`. Les chaînes littérales qu'elle contient sont souvent les meilleurs indices sur le comportement du programme : messages d'erreur, noms de fichiers, URLs, clés de registre, format strings `printf`, messages de logs… L'outil `strings` (Chapitre 5) extrait justement ces données. Les cross-references (XREF) dans Ghidra permettent ensuite de remonter de chaque chaîne vers le code qui la référence (Chapitre 8, section 8.7).

### `.data` — Données initialisées modifiables

Cette section contient les **variables globales et statiques initialisées** avec une valeur non nulle. Par exemple :

```c
int counter = 42;                   // → .data  
static char key[] = "secret123";    // → .data  
```

**Flags** : `WA` (Write + Alloc) — chargée en mémoire, inscriptible.

La section `.data` occupe de l'espace à la fois dans le fichier et en mémoire, car les valeurs initiales doivent être stockées quelque part.

> 💡 **En RE** : Les variables globales dans `.data` sont souvent des indicateurs d'état du programme (flags, compteurs, pointeurs vers des buffers). Dans un binaire avec symboles, elles portent un nom. Dans un binaire strippé, elles apparaissent comme des adresses brutes dans la plage mémoire correspondant à `.data`. Les watchpoints de GDB (Chapitre 11, section 11.5) sont particulièrement utiles pour surveiller les modifications de ces variables pendant l'exécution.

### `.bss` — Données non initialisées (ou initialisées à zéro)

Le nom `.bss` est historique (*Block Started by Symbol*, issu d'un assembleur des années 1950). Cette section contient les **variables globales et statiques initialisées à zéro** ou non explicitement initialisées :

```c
int buffer[1024];           // → .bss (implicitement zéro)  
static int state = 0;       // → .bss (explicitement zéro)  
```

**Flags** : `WA` (Write + Alloc).

**Type** : `NOBITS` — c'est la particularité de `.bss`. Contrairement à `.data`, cette section **n'occupe aucun espace dans le fichier** sur disque. Son type `NOBITS` indique au loader qu'il doit allouer la zone mémoire correspondante et la remplir de zéros au chargement. Cela permet d'économiser de l'espace disque : un tableau de 1 Mo initialisé à zéro ne coûte que quelques octets de métadonnées dans le fichier ELF.

```bash
# Vérifier que .bss est de type NOBITS et ne prend pas de place dans le fichier
readelf -S hello | grep bss
#  [21] .bss     NOBITS    0000000000004020  00003020  00000008  ...  WA  0  0  1
#                                                      ^^^^^^^^
#                                            Taille en mémoire (8 octets ici)
# Mais l'offset fichier et la taille fichier montrent qu'elle ne consomme rien sur disque.
```

> 💡 **En RE** : La taille de `.bss` peut révéler la présence de buffers importants (buffers de communication, zones de déchiffrement, caches). Un `.bss` anormalement grand dans un petit binaire mérite investigation.

## Les sections de liaison dynamique

### `.got` et `.got.plt` — Global Offset Table

La GOT est un tableau d'adresses situé dans une zone **inscriptible** de la mémoire. Chaque entrée correspond à un symbole externe (fonction ou variable) dont l'adresse réelle n'est connue qu'au moment du chargement.

- **`.got`** contient les adresses des variables globales importées depuis des bibliothèques partagées.  
- **`.got.plt`** contient les adresses des fonctions importées, remplies progressivement par le mécanisme de lazy binding.

**Flags** : `WA` (Write + Alloc) — c'est ce caractère inscriptible qui rend la GOT stratégique en exploitation (GOT overwrite — Chapitre 19, section 19.6).

Au démarrage, les entrées de `.got.plt` ne contiennent pas les adresses réelles des fonctions. Elles pointent initialement vers le stub PLT correspondant, qui déclenche la résolution via le loader dynamique lors du premier appel. Après résolution, l'adresse réelle est écrite dans la GOT et les appels suivants sont directs. C'est le **lazy binding**, détaillé en section 2.9.

```bash
# Afficher les entrées de la GOT
objdump -R hello
# ou
readelf -r hello
```

> 💡 **En RE** : Lire la GOT pendant l'exécution avec GDB permet de voir quelles fonctions de bibliothèque ont déjà été résolues et à quelles adresses. La commande `x/10gx <adresse_got>` (Chapitre 11) affiche le contenu de la table. En exploitation, écraser une entrée GOT pour rediriger un appel vers un code arbitraire est une technique classique — c'est pourquoi la protection Full RELRO (section 2.9 et Chapitre 19) rend la GOT en lecture seule après la résolution initiale.

### `.dynsym` et `.dynstr` — Symboles dynamiques

Ces sections forment la table de symboles nécessaire au **runtime** pour la résolution dynamique :

- **`.dynsym`** contient les entrées de symboles (nom, type, binding, section, valeur).  
- **`.dynstr`** contient les chaînes de caractères (noms des symboles) référencées par `.dynsym`.

**Flags** : `A` (Alloc) — elles doivent être en mémoire car le loader en a besoin.

La différence avec `.symtab`/`.strtab` (voir plus bas) est fondamentale : **`.dynsym` survit au stripping**. Quand vous exécutez `strip` sur un binaire, `.symtab` et `.strtab` sont supprimées, mais `.dynsym` et `.dynstr` sont préservées car elles sont indispensables au fonctionnement du programme. C'est pourquoi, même dans un binaire strippé, vous pouvez toujours voir les noms des fonctions importées depuis les bibliothèques partagées.

```bash
# Symboles dynamiques (survivent au stripping)
readelf --dyn-syms hello

# Table complète (disparaît après strip)
readelf -s hello
```

### `.dynamic` — Table dynamique

Cette section contient un tableau de paires clé-valeur (`tag` + `value`) qui fournit au loader toutes les informations nécessaires à la liaison dynamique : noms des bibliothèques partagées requises (`NEEDED`), adresses des tables de symboles et de hachage, flags de liaison, etc.

```bash
readelf -d hello
```

Sortie simplifiée :

| Tag | Valeur | Signification |  
|---|---|---|  
| `NEEDED` | `libc.so.6` | Bibliothèque requise |  
| `INIT` | `0x1000` | Adresse de `.init` |  
| `FINI` | `0x1234` | Adresse de `.fini` |  
| `PLTGOT` | `0x3fe8` | Adresse de `.got.plt` |  
| `SYMTAB` | `0x3c8` | Adresse de `.dynsym` |  
| `STRTAB` | `0x488` | Adresse de `.dynstr` |  
| `BIND_NOW` | — | Désactive le lazy binding (Full RELRO) |

> 💡 **En RE** : Consulter `.dynamic` est l'un des premiers réflexes du triage rapide (Chapitre 5, section 5.7). L'entrée `NEEDED` liste les bibliothèques partagées requises, ce qui révèle les dépendances du programme. La présence de `BIND_NOW` ou de `FLAGS_1` avec `NOW` indique un Full RELRO — un indice sur le niveau de hardening du binaire.

### `.interp` — Chemin du loader

Cette petite section contient une unique chaîne : le chemin absolu de l'interpréteur dynamique (le loader). Sur un système x86-64 standard :

```bash
readelf -p .interp hello
#   /lib64/ld-linux-x86-64.so.2
```

Le noyau Linux lit cette section pour savoir quel programme charger en premier. Ce n'est pas votre programme qui est exécuté directement — c'est le loader qui est lancé, et c'est lui qui charge et prépare votre programme (section 2.7).

## Les sections de métadonnées et débogage

### `.symtab` et `.strtab` — Table de symboles complète

Ces sections constituent la table de symboles « complète » du binaire — celle qui contient les noms de **toutes** les fonctions et variables, y compris les fonctions internes (`static`), les labels, les fichiers sources, etc.

**Flags** : aucun (`A` absent) — ces sections ne sont **pas** chargées en mémoire. Elles n'existent que dans le fichier sur disque pour les outils d'analyse et de débogage.

C'est précisément pour cette raison que `strip` peut les supprimer sans affecter l'exécution du programme. La différence entre un binaire « not stripped » et un binaire « stripped » réside essentiellement dans la présence ou l'absence de `.symtab` et `.strtab` :

```bash
# Avant stripping
file hello
# hello: ELF 64-bit ... not stripped
readelf -s hello | wc -l
# 65 (par exemple)

strip hello

# Après stripping
file hello
# hello: ELF 64-bit ... stripped
readelf -s hello
# (aucune sortie — .symtab a disparu)

# Mais les symboles dynamiques sont toujours là :
readelf --dyn-syms hello
# strcmp, printf, puts... toujours visibles
```

### `.eh_frame` et `.eh_frame_hdr` — Déroulement de pile

Ces sections contiennent les informations de **stack unwinding** au format DWARF (même en C, même sans `-g`). Elles permettent au runtime de « dérouler » la pile d'appels, ce qui est nécessaire pour la gestion des exceptions C++ (`throw`/`catch`), les outils de profiling, et les débogueurs (`backtrace` dans GDB).

**Flags** : `A` (Alloc) — elles doivent être en mémoire car le runtime en a besoin pour le *stack unwinding*.

> 💡 **En RE** : Les informations de `.eh_frame` sont exploitables pour retrouver les limites des fonctions dans un binaire strippé. Certains outils d'analyse (dont Ghidra) s'en servent pour améliorer la détection des fonctions. La section `.eh_frame` est aussi liée à la gestion des exceptions C++ que nous aborderons au Chapitre 17, section 17.4.

### `.comment` — Version du compilateur

Cette section contient une chaîne identifiant le compilateur et sa version :

```bash
readelf -p .comment hello
#   GCC: (Ubuntu 13.2.0-23ubuntu4) 13.2.0
```

**Flags** : `MS` (Merge + Strings) — non chargée en mémoire.

> 💡 **En RE** : Connaître la version exacte du compilateur permet de rechercher les patterns et idiomes spécifiques à cette version (Chapitre 16), de reproduire la compilation dans des conditions similaires, et d'identifier les éventuelles vulnérabilités connues du compilateur lui-même.

### `.note.gnu.build-id` — Identifiant de build

Cette section contient un hash unique (généralement SHA1) calculé à partir du contenu du binaire. Il sert de « fingerprint » :

```bash
readelf -n hello
#   Build ID: 3a1b...f42c (hex SHA1)
```

Ce build ID permet de faire le lien entre un binaire et ses fichiers de débogage séparés (`.debug` ou `debuginfod`), ce qui est utile quand les informations DWARF sont fournies dans un paquet séparé (courant dans les distributions Linux).

## Cartographie mentale : sections et permissions mémoire

Quand le loader charge un binaire, il regroupe les sections en **segments** selon leurs permissions. La correspondance sections → permissions → segment forme un modèle mental essentiel :

| Permissions mémoire | Sections concernées | Segment |  
|---|---|---|  
| **R-X** (lecture + exécution) | `.text`, `.plt`, `.init`, `.fini`, `.rodata`, `.eh_frame` | `LOAD` #1 (texte) |  
| **RW-** (lecture + écriture) | `.data`, `.bss`, `.got`, `.got.plt`, `.dynamic`, `.init_array`, `.fini_array` | `LOAD` #2 (données) |  
| **Pas chargées** | `.symtab`, `.strtab`, `.shstrtab`, `.comment` | Aucun segment |

> ⚠️ **Note** : La frontière exacte entre les segments et les sections qu'ils contiennent peut varier selon la version du linker et les options de compilation. Le schéma ci-dessus est le cas courant. Utilisez `readelf -l` pour voir le mappage réel (section 2.7).

Notez que `.rodata` est placée dans le segment exécutable (R-X) avec `.text`. Cela peut sembler surprenant — des données en lecture seule dans un segment exécutable ? C'est une conséquence du fait que le linker regroupe par permissions minimales : `.rodata` n'a besoin que de la lecture, et le segment R-X la fournit. Certaines configurations modernes de linker (avec l'option `-z separate-code`) créent un segment dédié R-- (lecture seule, non exécutable) pour `.rodata`, améliorant ainsi la sécurité.

## Inspecter les sections en pratique

Voici les commandes les plus utiles, récapitulées pour référence rapide :

| Objectif | Commande |  
|---|---|  
| Lister toutes les sections | `readelf -S hello` |  
| Contenu hexadécimal d'une section | `objdump -s -j .rodata hello` |  
| Désassembler une section de code | `objdump -d -j .text hello` |  
| Afficher les chaînes d'une section | `readelf -p .rodata hello` |  
| Voir les symboles dynamiques | `readelf --dyn-syms hello` |  
| Voir les relocations | `readelf -r hello` |  
| Voir la table dynamique | `readelf -d hello` |  
| Informations de build | `readelf -p .comment hello` |  
| Build ID | `readelf -n hello` |  
| Mappage sections → segments | `readelf -l hello` |

> 💡 **Conseil** : Au Chapitre 5, nous formaliserons ces commandes dans un **workflow de triage rapide** — une routine systématique des 5 premières minutes face à un binaire inconnu. Les sections que vous venez d'apprendre à identifier constitueront la colonne vertébrale de ce workflow.

---

> 📖 **Nous savons maintenant ce que contient un binaire ELF, section par section.** La question suivante est : comment influence-t-on ce contenu au moment de la compilation ? Dans la section suivante, nous examinerons les flags de GCC les plus importants pour le RE et leur impact concret sur les sections et le code produit.  
>  
> → 2.5 — Flags de compilation et leur impact sur le RE (`-O0` à `-O3`, `-g`, `-s`, `-fPIC`, `-pie`)

⏭️ [Flags de compilation et leur impact sur le RE (`-O0` à `-O3`, `-g`, `-s`, `-fPIC`, `-pie`)](/02-chaine-compilation-gnu/05-flags-compilation.md)
