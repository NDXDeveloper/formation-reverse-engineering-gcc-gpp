🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 33.5 — Bibliothèques embarquées et taille des binaires (tout est linké statiquement)

> 📦 L'un des chocs les plus courants en ouvrant un binaire Rust dans un désassembleur est le nombre de fonctions : des milliers, voire des dizaines de milliers, pour un programme qui semble trivial. Cette masse de code provient de la stdlib et des crates tierces, embarquées statiquement dans le binaire. Pour l'analyste RE, le défi n'est pas de lire tout ce code — c'est de l'ignorer intelligemment pour se concentrer sur la logique applicative.

---

## Pourquoi Rust lie tout statiquement

### Le modèle de distribution Rust

En C, la bibliothèque standard (`glibc`) est une bibliothèque partagée (`.so`) installée sur chaque système Linux. Un programme C la référence dynamiquement via la PLT/GOT et ne l'embarque pas dans son binaire. Résultat : un « Hello, World! » C compilé dynamiquement pèse quelques kilo-octets.

Rust fait le choix inverse. La stdlib Rust (`libstd`, `libcore`, `liballoc`, etc.) est liée **statiquement** par défaut. Il n'existe pas de `libstd-rust.so` installé sur les systèmes — chaque binaire Rust emporte sa propre copie de la stdlib.

Les raisons de ce choix sont multiples. La stdlib Rust évolue rapidement entre les versions du compilateur, et les garanties de stabilité ABI n'existent pas entre les versions de `rustc`. Deux programmes compilés avec des versions différentes de `rustc` ne pourraient pas partager la même `.so` sans risque d'incompatibilité. Le linking statique garantit que chaque binaire est autonome et fonctionne sur n'importe quel système Linux disposant d'une libc compatible.

### Les crates tierces : le même principe

L'écosystème Rust repose sur `crates.io`, un registre de bibliothèques (crates) que `cargo` télécharge et compile automatiquement. Chaque crate est compilée en une bibliothèque statique (`.rlib`) et linkée dans le binaire final. Un projet Rust typique dépend de dizaines, voire de centaines de crates — et chacune est embarquée.

Notre crackme n'utilise aucune crate tierce (uniquement la stdlib), mais un projet réel comme un serveur web ou un outil CLI pourrait tirer des centaines de dépendances transitives. Un simple serveur HTTP avec `tokio` et `axum` peut produire un binaire de 10 à 20 Mo contenant le code de 200+ crates.

---

## Anatomie de la taille d'un binaire Rust

### Décomposition par composant

Reprenons les trois variantes de notre crackme pour quantifier la répartition :

```bash
$ cd binaries/ch33-rust/
$ make all
```

| Variante | Taille | Fonctions (FUNC) | Commentaire |  
|---|---|---|---|  
| `crackme_rust_debug` | ~15 Mo | ~25 000 | Debug complet, DWARF, rien d'éliminé |  
| `crackme_rust_release` | ~4,3 Mo | ~8 000 | Optimisé, code mort partiellement éliminé |  
| `crackme_rust_strip` | ~406 Ko | ? (strippé) | LTO + strip + panic=abort |

Le passage de 15 Mo à 4 Mo s'explique principalement par l'élimination du code mort (dead code elimination) que LLVM réalise en `-O3` : les fonctions de la stdlib qui ne sont pas appelées (directement ou transitivement) sont supprimées. Le passage de 4 Mo à 400 Ko s'explique par le LTO, qui permet à LLVM de voir l'ensemble du programme comme une seule unité de compilation et d'éliminer encore plus de code inutilisé, combiné à `panic = "abort"` qui supprime tout le mécanisme d'unwinding.

### Visualiser la répartition avec `bloaty`

L'outil `bloaty` (Google) permet de décomposer la taille d'un binaire par section, par symbole, ou par fichier source compilé :

```bash
$ bloaty crackme_rust_release -d sections
    FILE SIZE        VM SIZE
 --------------  --------------
  41.2%  1.72Mi   48.5%  1.72Mi    .text
  20.3%   870Ki   23.9%   870Ki    .rodata
  15.1%   647Ki    0.0%       0    .symtab
  10.2%   438Ki    0.0%       0    .strtab
   5.8%   249Ki    6.8%   249Ki    .eh_frame
   ...
```

Observations clés : `.text` (le code exécutable) représente environ la moitié de la taille. `.rodata` (données en lecture seule : chaînes, tables de traits, constantes) représente un cinquième. `.symtab` et `.strtab` (tables de symboles) pèsent ensemble un quart du binaire — et disparaissent au strip. `.eh_frame` (tables d'unwinding) pèse environ 6% et disparaît avec `panic = "abort"`.

On peut aussi décomposer par symbole pour identifier les plus gros contributeurs :

```bash
$ bloaty crackme_rust_release -d symbols -n 20
```

Les fonctions les plus volumineuses sont presque toujours issues de la stdlib : le formateur (`core::fmt`), l'allocateur, la gestion des paniques, le code de hashage pour `HashMap`, les implémentations de `Display` et `Debug`. Le code applicatif ne représente qu'une infime fraction du total.

---

## Le problème pour l'analyste RE : le rapport signal/bruit

### Des milliers de fonctions, une poignée d'intéressantes

Sur le binaire release non strippé, on peut compter les fonctions :

```bash
$ nm crackme_rust_release | grep ' T \| t ' | wc -l
    4721

$ nm crackme_rust_release | rustfilt | grep 'crackme_rust::' | wc -l
    18
```

Sur 4 721 fonctions dans `.text`, seulement **18 appartiennent à notre code applicatif**. Le reste provient de `core`, `alloc`, `std`, et du runtime Rust. Le rapport signal/bruit est d'environ **1 pour 260**.

Sur un binaire strippé, cette distinction disparaît : toutes les fonctions sont anonymes. L'analyste se retrouve face à des milliers de fonctions indifférenciées, dont la grande majorité est du code de bibliothèque qu'il n'a aucun intérêt à analyser.

### L'effet sur le décompilateur

Le décompilateur (Ghidra, IDA) tente de décompiler chaque fonction. Sur un binaire Rust, cela produit des milliers de fonctions décompilées, la plupart sans intérêt pour l'analyse. Le graphe d'appels devient un enchevêtrement illisible, et le Symbol Tree de Ghidra se transforme en une liste interminable.

Le problème est aggravé par le fait que le code de la stdlib est souvent plus complexe que le code applicatif : la gestion du formatage (`core::fmt`), l'allocateur, le mécanisme de panique, les implémentations de traits pour les types standard — tout cela génère du pseudo-code dense et difficile à lire, qui noie l'analyste s'il ne fait pas le tri.

---

## Stratégie 1 : séparer le code applicatif par les symboles

C'est l'approche la plus efficace quand le binaire n'est pas strippé. Toutes les fonctions applicatives partagent le même préfixe de crate dans leur nom manglé.

### Filtrer avec `nm` et `rustfilt`

```bash
# Lister uniquement les fonctions applicatives
$ nm crackme_rust_release | rustfilt | grep 'crackme_rust::'

# Lister les crates présentes dans le binaire
$ nm crackme_rust_release | rustfilt | grep '::' | \
    sed 's/^\([^ ]* \)\?[^ ]* //' | cut -d: -f1 | sort -u
```

La deuxième commande produit la liste des crates embarquées :

```
alloc  
core  
crackme_rust  
std  
std_detect  
```

Notre crackme n'a pas de dépendances tierces, mais sur un projet réel vous verriez ici la liste complète : `serde`, `tokio`, `clap`, `regex`, etc. Cette liste est en elle-même une information précieuse : elle révèle les bibliothèques utilisées par l'application, ce qui donne des indices sur sa fonctionnalité (réseau, crypto, parsing, etc.).

### Annoter dans Ghidra

Dans Ghidra, après l'analyse automatique et le démanglement, vous pouvez exploiter le Symbol Tree pour filtrer :

1. Ouvrez le **Symbol Table** (Window → Symbol Table).  
2. Filtrez par le nom du crate applicatif (recherche textuelle).  
3. Sélectionnez toutes les fonctions applicatives et assignez-leur un label ou un namespace dédié.  
4. Les fonctions restantes (stdlib) peuvent être regroupées dans un namespace « stdlib » pour les réduire visuellement.

Certains analystes vont plus loin et colorient les fonctions dans le Function Graph : vert pour le code applicatif, gris pour la stdlib. Cela permet de voir immédiatement les zones intéressantes dans le graphe d'appels.

---

## Stratégie 2 : identifier la stdlib par les adresses

Même sans symboles, le code de la stdlib et le code applicatif tendent à occuper des plages d'adresses distinctes dans `.text`. Le linker place les fichiers objet dans l'ordre où il les reçoit, et le code applicatif est généralement linké avant la stdlib.

### Localiser le `main` applicatif

Le point d'entrée ELF (`_start`) appelle `__libc_start_main`, qui finit par appeler le `main` du programme. En Rust, ce `main` est un wrapper généré par le compilateur qui initialise le runtime puis appelle votre `fn main()`.

```bash
# Trouver le point d'entrée
$ readelf -h crackme_rust_strip | grep "Entry point"
  Entry point address:               0x8060

# Dans GDB, tracer jusqu'au main applicatif
(gdb) break *0x8060
(gdb) run
(gdb) si 50    # Avancer pas à pas jusqu'à l'appel à main
```

Une fois le `main` applicatif localisé, son adresse sert de point d'ancrage. Les fonctions proches en adresse sont probablement aussi du code applicatif (car elles proviennent du même fichier objet). Les fonctions à des adresses très différentes sont probablement de la stdlib.

### Utiliser la carte mémoire

Les fichiers `.map` produits par le linker (activables avec `-C link-args=-Wl,-Map=output.map` dans `RUSTFLAGS`) donnent le placement exact de chaque section objet dans le binaire final. C'est la méthode la plus précise pour délimiter les zones de code.

---

## Stratégie 3 : les signatures de fonctions connues

C'est l'approche reine pour les binaires strippés. L'idée est de reconnaître automatiquement les fonctions de la stdlib en comparant leurs octets avec une base de signatures pré-calculée. Nous détaillerons les outils en section 33.6, mais voici le principe.

Des projets communautaires compilent la stdlib Rust pour chaque version de `rustc` et chaque cible, puis extraient des signatures (hash des premiers octets de chaque fonction). Ces signatures sont distribuées sous forme de fichiers compatibles avec Ghidra (FIDB) ou IDA (FLIRT).

L'application de ces signatures sur un binaire strippé permet de nommer automatiquement des centaines à des milliers de fonctions. Le code applicatif est ce qui reste après ce nettoyage — les fonctions que la base de signatures n'a pas reconnues.

L'efficacité dépend de la correspondance exacte entre la version de `rustc` et la cible utilisées pour compiler le binaire et celles utilisées pour générer les signatures. Un décalage de version peut réduire significativement le taux de reconnaissance.

---

## Stratégie 4 : approche par les XREF depuis `main`

Plutôt que d'essayer de classifier toutes les fonctions, une approche plus pragmatique consiste à partir du `main` applicatif et à suivre les appels en profondeur.

### Construire le graphe d'appels applicatif

1. Localisez le `main` applicatif (voir stratégie 2).  
2. Dans Ghidra, ouvrez le **Function Call Graph** (Window → Function Call Graph) centré sur `main`.  
3. Parcourez le graphe niveau par niveau. Les premiers niveaux d'appels sont presque toujours du code applicatif.  
4. Quand vous atteignez des fonctions qui ressemblent à de la stdlib (formatage, allocation, I/O), arrêtez la descente dans cette branche.

Cette méthode « top-down » est souvent la plus efficace en pratique : au lieu de trier 5 000 fonctions, vous n'en examinez que quelques dizaines en suivant le fil de l'exécution.

### Reconnaître les fonctions stdlib sans les nommer

Même sans signatures, certaines fonctions de la stdlib se reconnaissent par leur structure :

**Les fonctions de formatage** (`core::fmt::*`) prennent des structures `fmt::Arguments` complexes en paramètre (plusieurs pointeurs vers `.rodata`) et appellent de nombreuses sous-fonctions. Elles sont volumineuses et hautement ramifiées.

**Les fonctions d'allocation** (`__rust_alloc`, `__rust_dealloc`) sont de courts wrappers qui appellent directement `malloc`/`free` via la PLT. Elles font moins de 10 instructions.

**Les fonctions de panique** sont identifiables par les chaînes de `.rodata` qu'elles référencent (voir section 33.3).

**Le code de hashage** (pour `HashMap`) contient des constantes magiques reconnaissables (SipHash utilise des constantes d'initialisation spécifiques).

**Le code d'I/O** (`std::io::*`) passe par des syscalls ou des appels libc (`write`, `read`) visibles dans la PLT/GOT.

En combinant ces heuristiques, un analyste expérimenté peut classifier les principales fonctions de la stdlib sans base de signatures, simplement en reconnaissant leurs motifs structurels.

---

## L'impact du LTO sur la séparation des crates

Le Link-Time Optimization (LTO) transforme radicalement la structure du binaire. Sans LTO, chaque crate est compilée séparément et ses fonctions forment un bloc relativement continu dans `.text`. Les frontières entre crates sont préservées, ce qui facilite la séparation.

Avec LTO (activé dans notre profil `release-strip`), LLVM fusionne toutes les crates en une seule unité de compilation avant d'optimiser. Les conséquences pour le RE sont importantes.

**L'inlining cross-crate.** Des fonctions de la stdlib peuvent être inlinées dans le code applicatif, et inversement. Le code applicatif et le code de la stdlib se retrouvent mélangés au sein des mêmes fonctions. Une fonction « applicative » peut contenir des dizaines de lignes de code provenant de `core::fmt` ou `alloc::vec`.

**La réorganisation des fonctions.** LLVM peut réordonner les fonctions pour optimiser la localité du cache d'instructions. Les fonctions applicatives ne forment plus un bloc contigu — elles sont dispersées au milieu des fonctions stdlib.

**L'élimination agressive.** Le LTO permet à LLVM de voir quelles fonctions de la stdlib sont réellement utilisées et d'éliminer le reste. Le binaire est plus petit, mais les fonctions restantes sont souvent des fragments partiels (fonctions partiellement inlinées, spécialisées pour un seul site d'appel).

> ⚠️ **Conséquence pratique** : sur un binaire compilé avec LTO et strippé, les stratégies de séparation par adresses (stratégie 2) et par signatures (stratégie 3) deviennent moins efficaces. L'approche top-down depuis `main` (stratégie 4) reste la plus fiable, car elle suit le flux d'exécution réel au lieu de chercher des frontières structurelles qui n'existent plus.

---

## Comparaison avec d'autres langages

Pour mettre en perspective le problème de taille et de bruit, voici une comparaison avec les autres langages natifs que l'on peut rencontrer en RE :

| Langage | Linking de la stdlib | Taille « Hello World » (release, Linux x86-64) | Fonctions dans le binaire | Difficulté de séparation |  
|---|---|---|---|---|  
| C (GCC) | Dynamique (glibc) | ~16 Ko | ~20 | Triviale (presque tout est applicatif) |  
| C++ (GCC) | Dynamique (libstdc++) | ~17 Ko | ~30 | Facile (templates instanciées identifiables) |  
| Rust | Statique (stdlib) | ~400 Ko – 4 Mo | ~4 000 – 8 000 | Difficile (stdlib mêlée au code applicatif) |  
| Go | Statique (runtime + stdlib) | ~1,8 Mo | ~6 000 | Moyen (`gopclntab` aide, voir ch. 34) |

Rust et Go partagent le même modèle de linking statique et souffrent du même problème de bruit. La différence principale est que Go conserve les noms de fonctions dans sa table `gopclntab` même après strip, alors que Rust ne dispose pas d'un tel mécanisme. Sur un binaire strippé, le RE Rust est objectivement plus difficile que le RE Go.

---

## Réduire la taille à la source (perspective développeur)

Cette section s'adresse à l'analyste qui a accès au code source (cas d'un audit) ou qui veut comprendre pourquoi un binaire cible est inhabituellement petit ou grand.

Les développeurs Rust disposent de plusieurs leviers pour réduire la taille de leurs binaires :

| Technique | Impact typique | Effet sur le RE |  
|---|---|---|  
| `panic = "abort"` | -30% à -50% | Supprime le code d'unwinding et réduit les chaînes de panique |  
| `lto = true` | -20% à -60% | Élimine le code mort cross-crate, mais brouille les frontières |  
| `codegen-units = 1` | -5% à -15% | Meilleures optimisations globales |  
| `strip = true` | -15% à -25% | Supprime symboles et debug info |  
| `opt-level = "z"` | -10% à -20% | Optimise pour la taille plutôt que la vitesse |  
| `cargo-bloat` → refactoring | Variable | Réduit les dépendances, donc le nombre de crates embarquées |

Un binaire Rust inhabituellement petit (< 200 Ko) a probablement été compilé avec toutes ces options activées, voire avec `#![no_std]` (pas de stdlib du tout, uniquement `core`). Dans ce cas, le binaire contient peu de code de bibliothèque et l'analyse est paradoxalement plus simple — mais le code applicatif doit réimplémenter ce que la stdlib fournit habituellement (allocation, I/O, formatage), ce qui peut le rendre plus complexe.

---

## Résumé pour l'analyste

Face à un binaire Rust volumineux, la démarche recommandée est la suivante :

1. **Estimer le ratio signal/bruit.** Si le binaire n'est pas strippé, comptez les fonctions applicatives vs le total. Si strippé, la taille brute donne un ordre de grandeur : un binaire de 5 Mo contient probablement moins de 1% de code applicatif.

2. **Choisir la stratégie de séparation adaptée.** Non strippé → filtrer par nom de crate (stratégie 1). Strippé sans LTO → signatures + séparation par adresses (stratégies 2 et 3). Strippé avec LTO → approche top-down depuis `main` (stratégie 4).

3. **Ne pas essayer de tout comprendre.** La stdlib Rust est un code connu et documenté. Quand vous identifiez une fonction comme étant de la stdlib (par son nom, sa signature, ou sa structure), annotez-la et passez à autre chose. Votre temps d'analyse doit se concentrer sur le code applicatif.

4. **Exploiter les indices indirects.** La liste des crates embarquées (via les symboles ou les chaînes) révèle les dépendances du projet : `serde` = sérialisation, `reqwest` = HTTP, `ring` = cryptographie, `clap` = parsing d'arguments CLI, `tokio` = async runtime. Ces indices orientent l'analyse avant même de lire l'assembleur.

---

> **Section suivante : 33.6 — Outils spécifiques : `cargo-bloat`, signatures Ghidra pour la stdlib Rust** — nous passerons en revue les outils concrets qui automatisent la séparation du code applicatif et de la stdlib, et leur utilisation pas à pas.

⏭️ [Outils spécifiques : `cargo-bloat`, signatures Ghidra pour la stdlib Rust](/33-re-rust/06-outils-cargo-bloat-ghidra.md)
