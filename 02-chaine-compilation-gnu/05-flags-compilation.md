🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.5 — Flags de compilation et leur impact sur le RE (`-O0` à `-O3`, `-g`, `-s`, `-fPIC`, `-pie`)

> 🎯 **Objectif de cette section** : Comprendre comment les principaux flags de GCC transforment le binaire produit, et savoir évaluer la difficulté d'une analyse RE en fonction des options de compilation utilisées.

---

## Pourquoi les flags de compilation importent en RE

Face à un binaire inconnu, l'une des premières questions du reverse engineer est : **dans quelles conditions ce programme a-t-il été compilé ?** La réponse influence radicalement la stratégie d'analyse :

- Un binaire compilé en `-O0 -g` (debug, sans optimisation) est presque transparent — le désassemblage correspond ligne à ligne au source, et les noms de variables sont disponibles.  
- Le même programme compilé en `-O2 -s` (optimisé et strippé) est un tout autre défi — fonctions inlinées, variables en registres, pas de noms de symboles.

Les flags de compilation agissent comme des curseurs sur un spectre qui va de « presque le code source » à « puzzle binaire opaque ». Les connaître vous permet d'anticiper ce que vous allez trouver dans le binaire et d'adapter vos outils en conséquence.

Pour illustrer concrètement chaque flag, nous utiliserons notre `hello.c` fil conducteur et comparerons les binaires produits.

## Flags d'optimisation : `-O0`, `-O1`, `-O2`, `-O3`, `-Os`

### `-O0` — Aucune optimisation (défaut)

C'est le niveau par défaut quand aucun flag `-O` n'est spécifié. Le compilateur produit du code machine qui suit fidèlement la structure du code source, sans aucune tentative d'amélioration des performances.

**Caractéristiques du code produit :**

- Chaque variable locale est stockée sur la pile à une adresse fixe. Le compilateur effectue des allers-retours constants entre les registres et la pile (*spilling*), même quand la valeur vient d'être chargée.  
- Chaque appel de fonction est un véritable `call` — aucune fonction n'est inlinée.  
- Les structures de contrôle (`if`, `for`, `while`, `switch`) sont traduites mécaniquement en comparaisons et sauts, dans un ordre prévisible.  
- Le code mort (jamais exécuté) est conservé.  
- Le prologue et l'épilogue de chaque fonction suivent le schéma standard (`push rbp; mov rbp, rsp; ... leave; ret`), ce qui rend les frontières de fonctions triviales à repérer.

**Impact RE** : c'est le scénario idéal. La correspondance entre le source et l'assembleur est quasi directe. Le décompilateur de Ghidra produit un pseudo-code très proche du C original. C'est le niveau que nous utiliserons dans la plupart des exercices de cette formation (variantes `_O0`).

```bash
gcc -O0 -o hello_O0 hello.c
```

### `-O1` — Optimisations conservatrices

Le compilateur active un premier lot d'optimisations qui améliorent les performances sans augmenter excessivement le temps de compilation ni transformer radicalement le code.

**Optimisations typiques activées :**

- Propagation de constantes : si le compilateur peut prouver qu'une variable vaut toujours `42`, il remplace la variable par la constante.  
- Élimination du code mort : les branches `if` dont la condition est toujours fausse sont supprimées.  
- Allocation de registres améliorée : les variables locales sont maintenues dans les registres au lieu d'être systématiquement stockées sur la pile.  
- Réduction de la force (*strength reduction*) : une multiplication par une puissance de 2 est remplacée par un décalage à gauche (`shl`).  
- Élimination des sous-expressions communes (CSE) : si `a + b` est calculé deux fois, le résultat est réutilisé.

**Impact RE** : le code est plus compact et plus fluide, mais reste globalement lisible. Les fonctions conservent leur identité (pas d'inlining à ce niveau). Le principal changement visible est la disparition des accès mémoire redondants — les variables « vivent » dans les registres plutôt que sur la pile, ce qui rend le suivi des valeurs un peu moins immédiat.

```bash
gcc -O1 -o hello_O1 hello.c
```

### `-O2` — Optimisations standard (production)

C'est le niveau d'optimisation le plus courant pour les builds de production. Il active la grande majorité des optimisations qui ne sacrifient pas la taille du code au profit de la vitesse.

**Optimisations supplémentaires par rapport à `-O1` :**

- **Inlining de fonctions** : les petites fonctions sont intégrées dans l'appelant. Notre fonction `check()`, qui ne fait qu'appeler `strcmp` et comparer le résultat, a de bonnes chances d'être inlinée dans `main()` — elle disparaît alors en tant que fonction distincte.  
- **Ordonnancement d'instructions** (*instruction scheduling*) : le compilateur réordonne les instructions pour exploiter le pipeline du processeur et masquer les latences mémoire.  
- **Optimisation des boucles** : déroulage partiel (*loop unrolling*), déplacement des invariants de boucle, etc.  
- **Tail call optimization** : un appel en position terminale est transformé en saut, économisant un frame de pile (détaillé au Chapitre 16, section 16.4).  
- **Peephole optimizations** : des séquences d'instructions courtes sont remplacées par des séquences équivalentes plus efficaces.

**Impact RE** : c'est la frontière à partir de laquelle l'analyse se complique notablement. Les fonctions peuvent disparaître (inlining), les variables n'ont plus de correspondance stable avec des emplacements mémoire, et l'ordre des instructions ne reflète plus la structure logique du source. Le décompilateur de Ghidra produit encore du pseudo-code exploitable, mais il nécessite davantage de travail d'interprétation de la part de l'analyste.

```bash
gcc -O2 -o hello_O2 hello.c
```

### `-O3` — Optimisations agressives

Ce niveau active des optimisations supplémentaires qui favorisent la vitesse au détriment potentiel de la taille du binaire.

**Optimisations supplémentaires par rapport à `-O2` :**

- **Vectorisation automatique** : les boucles opérant sur des tableaux sont transformées en instructions SIMD (SSE/AVX), qui traitent plusieurs éléments en parallèle. En assembleur, cela se traduit par des instructions opérant sur les registres `xmm0`–`xmm15` ou `ymm0`–`ymm15` (Chapitre 3, section 3.9).  
- **Déroulage de boucles agressif** : le corps de la boucle est dupliqué plusieurs fois pour réduire le coût des branchements.  
- **Inlining encore plus agressif** : des fonctions plus grandes sont candidates à l'inlining.  
- **Transformations spéculatives** : le compilateur peut dupliquer du code pour optimiser les chemins les plus probables.

**Impact RE** : le code devient significativement plus difficile à lire. Les boucles déroulées produisent de longues séquences d'instructions répétitives. Les instructions SIMD sont opaques si l'on ne connaît pas leur sémantique. Le graphe de flot de contrôle (CFG) peut devenir complexe à cause des duplications de chemins. Pour notre petit `hello.c`, la différence avec `-O2` est minime — l'impact de `-O3` se fait surtout sentir sur du code avec des boucles de calcul intensif.

```bash
gcc -O3 -o hello_O3 hello.c
```

### `-Os` — Optimisation en taille

Ce flag active les mêmes optimisations que `-O2` mais **désactive** celles qui augmentent la taille du code (déroulage de boucles, inlining agressif, alignement généreux des fonctions). L'objectif est de produire le binaire le plus compact possible.

**Impact RE** : paradoxalement, un binaire `-Os` peut être plus facile à analyser qu'un binaire `-O2`, car les fonctions sont moins souvent inlinées (l'inlining augmente la taille) et les boucles ne sont pas déroulées. Le code est compact mais structuré.

```bash
gcc -Os -o hello_Os hello.c
```

### Comparaison concrète sur notre fil conducteur

Compilons `hello.c` avec chaque niveau et comparons :

```bash
for opt in O0 O1 O2 O3 Os; do
    gcc -$opt -o hello_$opt hello.c
done  
ls -l hello_O*  
```

| Variante | Taille binaire (indicative) | `check()` visible ? | Instructions dans `main` (approx.) |  
|---|---|---|---|  
| `hello_O0` | ~16 Ko | Oui, fonction distincte | ~35–40 |  
| `hello_O1` | ~16 Ko | Oui, mais épurée | ~25–30 |  
| `hello_O2` | ~16 Ko | Souvent inlinée | ~20–25 |  
| `hello_O3` | ~16 Ko | Inlinée | ~20–25 |  
| `hello_Os` | ~16 Ko | Parfois préservée | ~20–25 |

Les différences de taille sont faibles sur un programme aussi petit. Sur un projet réel de plusieurs milliers de lignes, les écarts deviennent significatifs — un binaire `-O3` peut être 20 à 50 % plus gros qu'un binaire `-Os`.

Pour observer les différences dans le code assembleur :

```bash
# Comparer le désassemblage de main() entre O0 et O2
objdump -d -j .text hello_O0 | grep -A 50 '<main>'  > main_O0.txt  
objdump -d -j .text hello_O2 | grep -A 50 '<main>'  > main_O2.txt  
diff --color main_O0.txt main_O2.txt  
```

> 💡 **Astuce RE** : Pour deviner le niveau d'optimisation d'un binaire inconnu, cherchez des indices. La présence systématique de prologues `push rbp; mov rbp, rsp` dans toutes les fonctions suggère `-O0` ou `-O1`. L'absence de frame pointer (utilisation directe de `rsp`) et des fonctions très compactes suggèrent `-O2` ou supérieur. La présence d'instructions SIMD (`movaps`, `paddd`, `vaddps`…) dans des boucles simples est un indice de `-O3`. La section `.comment` révèle parfois la ligne de commande exacte (certaines distributions la préservent).

## Flag de débogage : `-g`

Le flag `-g` ordonne à GCC de générer des **informations de débogage** au format DWARF et de les embarquer dans le binaire (ou dans un fichier séparé avec `-gsplit-dwarf`).

```bash
gcc -O0 -g -o hello_debug hello.c
```

### Ce que `-g` ajoute au binaire

Les informations DWARF sont stockées dans des sections dédiées :

| Section | Contenu |  
|---|---|  
| `.debug_info` | Descriptions des types, variables, fonctions, paramètres, portées |  
| `.debug_abbrev` | Abréviations des descriptions DWARF |  
| `.debug_line` | Correspondance adresse machine ↔ ligne du fichier source |  
| `.debug_str` | Chaînes de caractères utilisées dans les infos de débogage |  
| `.debug_aranges` | Plages d'adresses couvertes par chaque unité de compilation |  
| `.debug_frame` | Informations de déroulement de pile (plus détaillées que `.eh_frame`) |  
| `.debug_loc` | Localisation des variables (dans quel registre ou à quel offset de pile) |  
| `.debug_ranges` | Plages d'adresses discontinues (pour le code optimisé) |

L'impact sur la taille est considérable :

```bash
ls -lh hello_O0 hello_debug
# hello_O0:    ~16 Ko
# hello_debug: ~30-50 Ko   (parfois 2-3x plus gros)
```

### Impact sur le RE

Les informations DWARF sont un trésor pour le reverse engineer :

- **Noms de fonctions, paramètres et variables locales** : au lieu de voir `rbp-8`, vous voyez `input`. Au lieu de `func_1234`, vous voyez `check`.  
- **Types complets** : les `struct`, `enum`, `typedef`, `class` sont décrits avec tous leurs champs, tailles et alignements. Ghidra et GDB les importent et les appliquent automatiquement.  
- **Correspondance source-assembleur** : chaque instruction machine est mappée à une ligne précise du fichier source. GDB utilise cette correspondance pour le débogage source-level (`list`, `next`, `step`).  
- **Informations de portée** : les variables sont associées à leur portée (bloc, fonction), ce qui permet de savoir quand une variable est « vivante ».

```bash
# Voir les informations DWARF
readelf --debug-dump=info hello_debug | head -80

# Voir la correspondance ligne-adresse
readelf --debug-dump=decodedline hello_debug
```

> 💡 **En RE** : En analyse réelle, les informations de débogage sont presque toujours absentes — les builds de production n'incluent pas `-g`. Cependant, il arrive de les trouver dans des cas spécifiques : firmwares embarqués compilés à la hâte, builds de développement fuitées, paquets de debug des distributions Linux (`*-dbgsym`, `*-debuginfo`), ou via des serveurs `debuginfod` publics. Vérifiez systématiquement avec `readelf -S binaire | grep debug` — une bonne surprise est toujours possible.

### Combinaison `-g` et optimisations

Les flags `-g` et `-O` ne sont pas mutuellement exclusifs. Il est parfaitement valide de compiler avec `-O2 -g` :

```bash
gcc -O2 -g -o hello_O2_debug hello.c
```

Le binaire contiendra du code optimisé *et* des informations DWARF. Cependant, les informations de débogage seront moins précises qu'en `-O0 -g` : les variables inlinées ou maintenues en registres sont plus difficiles à localiser, et la correspondance ligne-adresse peut présenter des sauts inhabituels (le code d'une ligne N peut être mélangé avec celui de la ligne N+3 après réordonnancement).

GDB signale ces situations avec des messages du type « optimized out » quand vous tentez d'afficher une variable qui n'existe plus en tant que telle dans le code machine.

## Flag de stripping : `-s`

Le flag `-s` demande au linker de **supprimer toutes les tables de symboles non dynamiques** du binaire final. C'est l'équivalent d'exécuter la commande `strip` après la compilation.

```bash
gcc -O0 -s -o hello_stripped hello.c
```

### Ce que `-s` supprime

| Élément | Avant `-s` | Après `-s` |  
|---|---|---|  
| `.symtab` (symboles complets) | ✅ Présent | ❌ Supprimé |  
| `.strtab` (noms des symboles) | ✅ Présent | ❌ Supprimé |  
| Sections `.debug_*` | ✅ Si `-g` | ❌ Supprimées |  
| `.dynsym` (symboles dynamiques) | ✅ Présent | ✅ **Préservé** |  
| `.dynstr` (noms dynamiques) | ✅ Présent | ✅ **Préservé** |

La distinction est cruciale : les symboles dynamiques (`.dynsym`) **ne sont jamais supprimés** par `strip` ou `-s`, car ils sont indispensables au fonctionnement du loader. C'est pourquoi les noms des fonctions importées (`strcmp`, `printf`, `malloc`…) restent toujours visibles.

### Impact sur le RE

Le stripping est l'**obstacle le plus courant** en RE. Sans `.symtab`, vous perdez :

- Les noms de toutes les fonctions internes (l'auteur les a nommées `check`, `validate_license`, `decrypt_config`… mais vous ne voyez que des adresses).  
- Les noms des variables globales.  
- La taille des fonctions (Ghidra et les autres outils doivent la recalculer par analyse du flux de contrôle).

Ce que vous conservez malgré le stripping :

- Les noms des fonctions importées (via `.dynsym`).  
- Les chaînes littérales dans `.rodata`.  
- La structure du code machine (les instructions ne changent pas).  
- Les informations de `.eh_frame` (utiles pour la détection des frontières de fonctions).

```bash
# Comparer les symboles avant et après stripping
readelf -s hello_O0 | grep FUNC | wc -l       # ex: 35 fonctions  
readelf -s hello_stripped                       # (vide)  
readelf --dyn-syms hello_stripped | grep FUNC   # strcmp, printf, puts... toujours là  
```

> 💡 **Astuce RE** : L'outil `strip` offre un contrôle plus fin que le flag `-s`. L'option `strip --strip-unneeded` supprime uniquement les symboles non nécessaires à la relocation, en préservant les symboles de fonctions globales. C'est un juste milieu que certains projets utilisent. Avec `strip --only-keep-debug`, vous pouvez extraire les informations de débogage dans un fichier séparé — technique utilisée par les distributions Linux pour leurs paquets `*-dbgsym`.

## Flags de positionnement du code : `-fPIC` et `-pie`

Ces deux flags concernent la **position-independence** du code — sa capacité à fonctionner correctement quelle que soit l'adresse mémoire à laquelle il est chargé.

### `-fPIC` — Position-Independent Code

Ce flag demande au compilateur de générer du code qui ne contient **aucune adresse absolue codée en dur**. Toutes les références aux données et aux fonctions passent par des indirections relatives (via le registre `rip` en x86-64, via la GOT pour les symboles externes).

```bash
gcc -fPIC -shared -o libhello.so hello_lib.c
```

`-fPIC` est **obligatoire** pour compiler des bibliothèques partagées (`.so`). Sans lui, le code contiendrait des adresses absolues qui ne fonctionneraient que si la bibliothèque est chargée à une adresse précise — ce qui est incompatible avec le chargement dynamique (plusieurs `.so` partagent le même espace d'adressage).

**Impact sur l'assembleur** : au lieu d'accéder à une variable globale via une adresse absolue, le code utilise un accès relatif au compteur de programme :

```asm
# Sans -fPIC (adresse absolue — rare en x86-64 moderne)
mov    eax, DWORD PTR [0x404028]

# Avec -fPIC (relatif à rip)
mov    eax, DWORD PTR [rip+0x2f1a]    # adresse calculée relativement à rip
```

En x86-64, le mode d'adressage relatif à `rip` est natif et efficace, ce qui rend le surcoût de `-fPIC` négligeable sur cette architecture (contrairement à x86-32 où un registre supplémentaire devait être sacrifié).

### `-pie` — Position-Independent Executable

Ce flag demande au linker de produire un **exécutable position-independent**, c'est-à-dire un programme qui peut être chargé à n'importe quelle adresse en mémoire. C'est le prérequis technique pour l'**ASLR** (Address Space Layout Randomization — section 2.8).

```bash
gcc -pie -o hello_pie hello.c
```

Depuis GCC 6+ et les distributions Linux modernes, `-pie` est activé **par défaut**. Votre `gcc hello.c -o hello` produit déjà un binaire PIE. Vous pouvez le vérifier :

```bash
readelf -h hello | grep Type
# Type: DYN (Position-Independent Executable file)

# Pour désactiver PIE explicitement :
gcc -no-pie -o hello_nopie hello.c  
readelf -h hello_nopie | grep Type  
# Type: EXEC (Executable file)
```

**Impact RE** : dans un binaire PIE (`ET_DYN`), toutes les adresses affichées par le désassembleur sont des **offsets relatifs à la base de chargement**, pas des adresses absolues. À chaque exécution, l'ASLR attribue une base différente. Cela signifie que :

- Les adresses dans `objdump` ou Ghidra commencent typiquement à `0x1000` (offset relatif), pas à `0x401000` (adresse absolue typique d'un `ET_EXEC`).  
- Pour poser un breakpoint dans GDB sur un binaire PIE, vous devez soit utiliser un nom de fonction (`break main`), soit calculer l'adresse runtime (`break *($base + 0x1234)`). Les extensions GDB comme GEF et pwndbg gèrent cela automatiquement (Chapitre 12).  
- Les XREF (cross-references) dans les outils de RE fonctionnent sur les offsets relatifs et restent cohérentes.

| Caractéristique | Non-PIE (`ET_EXEC`) | PIE (`ET_DYN`) |  
|---|---|---|  
| Adresse de base | Fixe (ex: `0x400000`) | Aléatoire (ASLR) |  
| Adresses dans le désassemblage | Absolues | Offsets relatifs à la base |  
| ASLR du code | ❌ Impossible | ✅ Activé |  
| Breakpoints GDB sur adresse | Simples | Nécessitent calcul ou nom de symbole |  
| Code machine | Peut contenir des adresses absolues | Entièrement relatif à `rip` |

> 💡 **En RE** : Quand vous analysez un binaire avec `checksec` (Chapitre 5, section 5.6), la ligne « PIE: enabled » vous indique un binaire position-independent. Si PIE est désactivé, les adresses sont fixes d'une exécution à l'autre, ce qui simplifie le débogage dynamique mais affaiblit la sécurité.

## Récapitulatif : matrice flags × impact RE

Le tableau suivant synthétise l'impact de chaque flag sur les aspects clés de l'analyse RE :

| Flag | Lisibilité du code | Symboles | Infos de débogage | Taille binaire | Sécurité |  
|---|---|---|---|---|---|  
| `-O0` | ★★★★★ Excellente | Inchangés | Inchangées | Base | Aucune optimisation |  
| `-O1` | ★★★★ Bonne | Inchangés | Inchangées | ≈ Base | — |  
| `-O2` | ★★★ Moyenne | Inchangés | Inchangées | ≈ Base | — |  
| `-O3` | ★★ Difficile | Inchangés | Inchangées | ↑ Plus gros | — |  
| `-Os` | ★★★ Moyenne | Inchangés | Inchangées | ↓ Plus petit | — |  
| `-g` | Inchangée | Inchangés | ✅ DWARF complet | ↑↑ Beaucoup plus gros | — |  
| `-s` | Inchangée | ❌ Strippés | ❌ Supprimées | ↓ Plus petit | Obfuscation légère |  
| `-fPIC` | Légèrement changée | Inchangés | Inchangées | ≈ Base | Prérequis `.so` |  
| `-pie` | Adresses relatives | Inchangés | Inchangées | ≈ Base | ✅ Permet l'ASLR |

Les combinaisons les plus fréquentes en pratique :

| Contexte | Flags typiques | Difficulté RE |  
|---|---|---|  
| Build debug développeur | `-O0 -g` | ★ Triviale |  
| Build release standard | `-O2 -s -pie` | ★★★★ Élevée |  
| Build release hardened | `-O2 -s -pie -fstack-protector-strong` | ★★★★★ Maximale |  
| Bibliothèque partagée | `-O2 -fPIC -shared` | ★★★ Moyenne (symboles exportés visibles) |  
| Firmware embarqué | `-Os -static` | ★★★★ Élevée (statique, pas de symboles dynamiques) |

## Autres flags utiles à connaître

Quelques flags supplémentaires que vous rencontrerez en analyse RE :

### `-fstack-protector` et variantes

Active la protection par **stack canary** : une valeur sentinelle est placée sur la pile entre les variables locales et l'adresse de retour. Si un buffer overflow écrase le canary, le programme détecte la corruption et appelle `__stack_chk_fail` avant de retourner. En assembleur, vous reconnaîtrez ce pattern par un accès à `fs:0x28` (la valeur du canary dans le TLS) en début de fonction et une comparaison avant le `ret`.

Les variantes sont `-fstack-protector` (fonctions avec buffers `char` uniquement), `-fstack-protector-strong` (plus large, recommandé) et `-fstack-protector-all` (toutes les fonctions).

Approfondi au Chapitre 19, section 19.5.

### `-fno-omit-frame-pointer`

Force le compilateur à conserver le **frame pointer** (`rbp`) dans chaque fonction, même avec optimisations. Sans ce flag (comportement par défaut en `-O1` et supérieur), le compilateur peut utiliser `rbp` comme registre général, ce qui rend la pile plus difficile à lire dans GDB.

Ce flag est souvent activé dans les builds de profiling (il facilite le *stack unwinding* des profilers comme `perf`).

### `-flto` — Link-Time Optimization

Active l'**optimisation inter-modules** : au lieu d'optimiser chaque fichier `.c` indépendamment, le compilateur conserve une représentation intermédiaire dans les fichiers `.o` et optimise l'ensemble au moment de l'édition de liens. Cela permet d'inliner des fonctions définies dans des fichiers différents et d'éliminer du code mort à l'échelle du programme entier.

**Impact RE** : les frontières entre modules sources disparaissent complètement. Le binaire est encore plus optimisé (et donc plus difficile à analyser) qu'avec `-O2` seul. Approfondi au Chapitre 16, section 16.5.

### `-static`

Produit un binaire **statiquement lié** : tout le code des bibliothèques est copié dans l'exécutable. Le binaire est autonome mais beaucoup plus gros, et les fonctions de la libc ne sont plus identifiables par la PLT (puisqu'il n'y a pas de PLT). Les signatures de fonctions (FLIRT, Ghidra) deviennent indispensables (Chapitre 20, section 20.5).

## Comment déterminer les flags d'un binaire inconnu

Le binaire ne contient généralement pas un enregistrement explicite de la ligne de commande `gcc` utilisée. Mais plusieurs indices permettent de les deviner :

| Indice | Commande / Outil | Ce que ça révèle |  
|---|---|---|  
| Section `.comment` | `readelf -p .comment binaire` | Version de GCC (parfois la ligne complète) |  
| Présence de `.debug_*` | `readelf -S binaire \| grep debug` | Compilé avec `-g` |  
| `file` dit « stripped » | `file binaire` | Compilé avec `-s` ou strippé après coup |  
| Type `DYN` vs `EXEC` | `readelf -h binaire \| grep Type` | PIE activé ou non |  
| Prologues `push rbp` systématiques | Désassemblage | `-O0` ou `-fno-omit-frame-pointer` |  
| Absence de frame pointer (`rbp`) | Désassemblage | `-O1` ou supérieur |  
| Instructions SIMD dans les boucles | Désassemblage | `-O3` probable |  
| Appels à `__stack_chk_fail` | `objdump -d \| grep stack_chk` | `-fstack-protector` |  
| `checksec` | `checksec --file=binaire` | PIE, NX, canary, RELRO |  
| Taille anormalement grande | `ls -lh binaire` | `-static` ou `-g` ou `-O3` |

---

> 📖 **Nous savons désormais comment les flags de compilation façonnent le binaire.** Parmi tous les artefacts que `-g` peut produire, les informations DWARF méritent un traitement à part tant elles sont précieuses quand elles sont présentes. C'est l'objet de la section suivante.  
>  
> → 2.6 — Comprendre les fichiers de symboles DWARF

⏭️ [Comprendre les fichiers de symboles DWARF](/02-chaine-compilation-gnu/06-symboles-dwarf.md)
