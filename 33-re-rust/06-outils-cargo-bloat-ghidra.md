🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 33.6 — Outils spécifiques : `cargo-bloat`, signatures Ghidra pour la stdlib Rust

> 🧰 Les sections précédentes ont décrit les patterns Rust et les stratégies conceptuelles pour naviguer dans un binaire volumineux. Cette section passe aux outils concrets : ceux du côté développeur qui aident à comprendre la composition d'un binaire, et ceux du côté analyste qui automatisent l'identification de la stdlib pour dégager le code applicatif.

---

## Outils côté développeur (quand on a le code source)

Ces outils sont utiles dans deux scénarios : vous réalisez un audit de sécurité et vous disposez du code source, ou vous compilez un binaire de référence pour préparer des signatures.

### `cargo-bloat` — qui consomme de la place ?

`cargo-bloat` est une extension de `cargo` qui analyse la taille de chaque fonction et de chaque crate dans le binaire final. C'est l'outil de diagnostic le plus direct pour comprendre la composition d'un binaire Rust.

**Installation :**

```bash
$ cargo install cargo-bloat
```

**Analyse par fonction — les plus gros contributeurs :**

```bash
$ cd binaries/ch33-rust/crackme_rust/
$ cargo bloat --release -n 20
```

La sortie ressemble à ceci :

```
 File  .text    Size        Crate Name
 3.8%  8.1%  6.0KiB          std std::rt::lang_start_internal
 2.5%  5.3%  3.9KiB         core core::fmt::write
 2.1%  4.5%  3.3KiB         core core::fmt::Formatter::pad
 1.8%  3.9%  2.9KiB          std std::io::Write::write_fmt
 1.5%  3.2%  2.4KiB         core core::fmt::num::<impl ...>::fmt
 0.9%  1.9%  1.4KiB crackme_rust crackme_rust::ChecksumValidator::validate  (*)
 0.7%  1.5%  1.1KiB crackme_rust crackme_rust::FormatValidator::validate    (*)
 0.5%  1.0%    768B crackme_rust crackme_rust::main                         (*)
 ...
```

Les lignes marquées `(*)` sont les fonctions applicatives — elles représentent une fraction minuscule du total. Les plus gros contributeurs sont systématiquement les fonctions de formatage (`core::fmt`), d'I/O (`std::io`), et d'initialisation du runtime (`std::rt`).

**Analyse par crate — la répartition des dépendances :**

```bash
$ cargo bloat --release --crates
```

```
 File  .text    Size Crate
 45.2% 62.1% 230KiB std
 28.1% 24.3%  90KiB core
  8.5%  7.4%  27KiB alloc
  4.3%  3.7%  14KiB crackme_rust
  2.1%  1.8%   7KiB compiler_builtins
  ...
```

Cette vue montre que la stdlib (`std` + `core` + `alloc`) représente plus de 80% du code, alors que le crate applicatif ne pèse que 4%. Sur un projet avec des dépendances tierces, vous verriez ici la contribution de chaque crate — ce qui révèle les bibliothèques les plus « coûteuses » en taille.

> 💡 **Application RE** : exécuter `cargo bloat --crates` sur le code source d'une cible d'audit vous donne immédiatement la liste des crates embarquées et leur poids relatif. Cela prépare votre analyse : si `ring` représente 15% du binaire, vous savez qu'il y a de la cryptographie significative ; si `serde_json` est présent, il y a de la sérialisation JSON, etc.

### `cargo-bloat` avec filtre de crate

Pour se concentrer sur le code applicatif :

```bash
$ cargo bloat --release --filter crackme_rust -n 30
```

Cette commande ne liste que les fonctions du crate `crackme_rust`, triées par taille décroissante. C'est la cartographie exacte du code que l'analyste devra reverser.

### `twiggy` — analyse du graphe de dépendances de taille

`twiggy` (Mozilla) est un outil complémentaire à `cargo-bloat`. Il analyse le graphe de rétention : pour chaque fonction, il indique pourquoi elle est dans le binaire (qui l'appelle) et combien d'espace elle « retient » (elle-même plus tout ce qu'elle appelle exclusivement).

```bash
$ cargo install twiggy
$ cargo build --release
$ twiggy top target/release/crackme_rust -n 15
$ twiggy dominators target/release/crackme_rust | head -30
```

La commande `dominators` est particulièrement utile : elle montre la hiérarchie de rétention. Si une fonction applicative de 500 octets appelle exclusivement une branche de la stdlib de 20 Ko, le dominateur montre que cette fonction « coûte » 20,5 Ko au total. Pour l'analyste RE, cela révèle quelles fonctions applicatives entraînent les plus grosses portions de stdlib — et donc quels chemins d'exécution seront les plus complexes à analyser.

---

## Outils côté analyste (sans code source)

### `rustfilt` — démanglement en ligne de commande

Couvert en détail en section 33.2, `rustfilt` est l'outil indispensable de toute analyse de binaire Rust non strippé. Rappel des usages essentiels :

```bash
# Démanger un symbole unique
$ echo "_RNvNtCs9g8eSEAj0m_13crackme_rust17ChecksumValidator3new" | rustfilt

# Démanger la sortie complète de nm
$ nm crackme_rust_release | rustfilt > symbols_demangled.txt

# Démanger la sortie d'objdump
$ objdump -d -M intel crackme_rust_release | rustfilt | less

# Lister uniquement les fonctions applicatives
$ nm crackme_rust_release | rustfilt | grep ' T .*crackme_rust::'
```

Le pipe `objdump | rustfilt` transforme un désassemblage illisible (symboles manglés de 80+ caractères) en un listing compréhensible où chaque `call` affiche le nom démanglé de la fonction cible.

### `rustc-demangle` — bibliothèque programmable

Pour les scripts d'analyse automatisée, la bibliothèque `rustc-demangle` (le moteur derrière `rustfilt`) est disponible en Rust et en C. Elle permet d'intégrer le démanglement dans des outils custom :

```python
# En Python, via subprocess
import subprocess

def rust_demangle(symbol):
    result = subprocess.run(
        ['rustfilt'],
        input=symbol,
        capture_output=True,
        text=True
    )
    return result.stdout.strip()
```

Pour un usage intensif (traitement de milliers de symboles), appeler `rustfilt` une seule fois avec tous les symboles en entrée est bien plus performant que de l'appeler en boucle.

---

## Signatures Ghidra pour la stdlib Rust

### Le problème des binaires strippés

Sur un binaire Rust strippé, Ghidra attribue des noms génériques aux fonctions (`FUN_00401230`, `FUN_00401450`, etc.). Parmi les milliers de fonctions, la majorité provient de la stdlib — mais sans signatures, Ghidra ne peut pas les identifier.

Les **signatures de fonctions** (Function ID, ou FIDB dans Ghidra) résolvent ce problème. Le principe : comparer les premiers octets de chaque fonction du binaire cible avec une base de données de fonctions connues (compilées à partir de la stdlib Rust). Quand une correspondance est trouvée, la fonction est automatiquement renommée.

### Projets communautaires de signatures Rust

Plusieurs projets maintiennent des bases de signatures pour la stdlib Rust :

**`rust-std-ghidra-sigs`** — Le projet le plus établi. Il fournit des fichiers FIDB pour Ghidra, générés à partir de la stdlib compilée pour différentes versions de `rustc` et différentes cibles.

```bash
# Cloner le dépôt de signatures
$ git clone https://github.com/<projet>/rust-std-ghidra-sigs.git
```

Les fichiers FIDB sont organisés par version de rustc et par cible :

```
rust-std-ghidra-sigs/
├── 1.75.0/
│   ├── x86_64-unknown-linux-gnu.fidb
│   ├── x86_64-unknown-linux-musl.fidb
│   └── ...
├── 1.76.0/
│   └── ...
└── ...
```

**`sigkit`** (pour IDA) — L'équivalent au format FLIRT pour les utilisateurs d'IDA. Le principe est identique : des signatures pré-calculées à appliquer sur le binaire.

> ⚠️ **Correspondance de version.** L'efficacité des signatures dépend de la correspondance exacte entre la version de `rustc` utilisée pour compiler le binaire cible et celle utilisée pour générer les signatures. Un décalage d'une version mineure peut réduire le taux de reconnaissance de 90% à 50%, car les optimisations LLVM changent subtilement le code généré entre les versions. Identifier la version de `rustc` est donc une étape préliminaire importante (voir plus bas).

### Installer les signatures dans Ghidra

L'installation suit le workflow standard de Ghidra pour les fichiers FIDB :

**Étape 1 — Copier le fichier FIDB dans le répertoire Ghidra :**

```bash
$ cp rust-std-ghidra-sigs/1.76.0/x86_64-unknown-linux-gnu.fidb \
    $GHIDRA_HOME/Features/Base/data/fidb/
```

Alternativement, dans Ghidra : File → Install Extensions → ou placer le `.fidb` dans le dossier utilisateur `~/.ghidra/<version>/fidb/`.

**Étape 2 — Appliquer les signatures lors de l'analyse :**

Lors de l'import d'un binaire, dans le panneau Analysis Options :
1. Assurez-vous que **Function ID** est coché dans la liste des analyseurs.  
2. Cliquez sur le bouton de configuration (icône engrenage) à côté de Function ID.  
3. Vérifiez que le fichier FIDB Rust est listé et activé.  
4. Lancez l'analyse.

**Étape 3 — Vérifier les résultats :**

Après l'analyse, ouvrez le Symbol Table et filtrez par source « Function ID Analyzer ». Les fonctions reconnues apparaissent avec leur nom complet de la stdlib.

```
FUN_00401230  →  core::fmt::write  
FUN_00401450  →  core::panicking::panic_fmt  
FUN_00401890  →  alloc::raw_vec::RawVec<T,A>::reserve_for_push  
FUN_00401a20  →  std::io::stdio::_print  
...
```

Les fonctions non reconnues sont soit du code applicatif, soit des fonctions de la stdlib qui ont divergé (version de `rustc` différente, options de compilation différentes). Dans les deux cas, c'est à l'analyste de trancher — mais le travail est considérablement réduit.

### Générer ses propres signatures

Si aucune signature pré-existante ne correspond à la version de `rustc` de la cible, vous pouvez générer les vôtres. Le processus est le suivant :

**1. Identifier la version de `rustc` de la cible** (voir la section dédiée plus bas).

**2. Compiler la stdlib avec cette version exacte :**

```bash
# Installer la version spécifique de rustc via rustup
$ rustup install 1.76.0
$ rustup default 1.76.0

# Compiler un projet minimal pour produire les .rlib de la stdlib
$ cargo new --bin dummy_project
$ cd dummy_project
$ cargo build --release
```

Les fichiers `.rlib` de la stdlib se trouvent dans le répertoire `~/.rustup/toolchains/1.76.0-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib/`.

**3. Générer le fichier FIDB avec Ghidra :**

Ghidra fournit un outil en mode headless pour créer des fichiers FIDB à partir de bibliothèques :

```bash
$ $GHIDRA_HOME/support/analyzeHeadless /tmp ghidra_project \
    -import ~/.rustup/toolchains/1.76.0-*/lib/rustlib/*/lib/*.rlib \
    -postScript CreateFidDatabase.java \
    -scriptPath $GHIDRA_HOME/Features/Base/ghidra_scripts/
```

Le processus exact peut varier selon la version de Ghidra. Consultez la documentation de Ghidra pour les détails du script `CreateFidDatabase`.

**4. Tester sur le binaire cible** en suivant les étapes d'installation décrites ci-dessus.

---

## Identifier la version de `rustc` dans un binaire

Pour appliquer les bonnes signatures, il faut connaître la version de `rustc` utilisée pour compiler la cible. Plusieurs indices permettent de la déduire.

### La chaîne `.comment`

La section `.comment` de l'ELF contient parfois la version du compilateur :

```bash
$ readelf -p .comment crackme_rust_release

String dump of section '.comment':
  [     0]  rustc version 1.76.0 (07dca489a 2024-02-04)
```

Cette section survit souvent au strip (`strip --strip-all` ne la supprime pas toujours, mais `strip --remove-section=.comment` le fait). C'est le premier endroit à vérifier.

```bash
# Vérifier aussi sur un binaire strippé
$ readelf -p .comment crackme_rust_strip 2>/dev/null
```

### Les chaînes dans `.rodata`

Les messages de panique de la stdlib contiennent des chemins vers le code source de la stdlib, qui incluent parfois la version :

```bash
$ strings crackme_rust_strip | grep -i "rustc\|rust-src\|toolchain"
$ strings crackme_rust_strip | grep "library/std/src"
```

Les chemins de type `/rustc/<commit_hash>/library/core/src/...` contiennent le hash du commit `rustc`. Ce hash peut être recherché dans le dépôt GitHub de Rust pour identifier la version exacte :

```bash
$ strings crackme_rust_strip | grep -oP '/rustc/[a-f0-9]+/' | head -1
/rustc/07dca489ac2d933c78d3c5158e3f43beefeb02ce/

# Rechercher ce hash sur GitHub :
# https://github.com/rust-lang/rust/commit/07dca489ac2d933c78d3c5158e3f43beefeb02ce
# → correspond à rustc 1.76.0
```

### L'entropie et la taille comme indices indirects

La taille du binaire et la proportion des sections varient entre les versions de `rustc` (les optimisations LLVM évoluent). Ce n'est pas une méthode précise, mais combinée avec d'autres indices, elle peut aider à réduire l'éventail des versions candidates.

---

## Radare2 et les binaires Rust

Radare2 dispose de fonctionnalités utiles pour le RE Rust, accessibles en ligne de commande.

### Démanglement automatique

```bash
# Activer le démanglement global
$ r2 -A crackme_rust_release
[0x00008060]> e asm.demangle=true
[0x00008060]> e bin.demangle=true

# Lister les fonctions avec noms démanglés
[0x00008060]> afl~crackme_rust
```

Le filtre `~crackme_rust` après `afl` (analyze functions list) agit comme un `grep` intégré et n'affiche que les fonctions applicatives.

### Signature avec `zignatures`

Radare2 dispose de son propre système de signatures appelé `zignatures` (`z` commands). On peut générer des signatures depuis un binaire de référence et les appliquer sur une cible strippée :

```bash
# Sur le binaire de référence (avec symboles)
$ r2 crackme_rust_release
[0x00008060]> aa
[0x00008060]> zg           # Générer les zignatures de toutes les fonctions
[0x00008060]> zos sigs.z   # Sauvegarder dans un fichier

# Sur le binaire strippé
$ r2 crackme_rust_strip
[0x00008060]> aa
[0x00008060]> zo sigs.z    # Charger les zignatures
[0x00008060]> z/           # Appliquer (chercher les correspondances)
[0x00008060]> afl          # Les fonctions reconnues sont renommées
```

Cette méthode fonctionne bien quand le binaire de référence et la cible ont été compilés avec la même version de `rustc` et les mêmes options. C'est typiquement le cas lors d'un audit où l'on peut recompiler le projet.

---

## Binary Ninja et IDA : support Rust

### Binary Ninja

Binary Ninja reconnaît le mangling Rust v0 nativement dans ses versions récentes. Ses fonctionnalités pertinentes pour le RE Rust incluent le démanglement automatique à l'import, un type system extensible (pour définir `RustStr`, `RustString`, etc.), ainsi qu'un API Python riche qui permet d'automatiser la classification des fonctions par crate. Le plugin communautaire `bn-rust-demangle` comble les lacunes des versions plus anciennes.

### IDA Free / IDA Pro

IDA gère le démanglement Rust v0 depuis la version 7.7. Pour les versions antérieures, le plugin `ida-rust-demangle` remplit cette fonction. Le système FLIRT d'IDA est l'équivalent du FIDB de Ghidra : des fichiers `.sig` pré-calculés permettent de reconnaître la stdlib. Le projet `ida-rust-sig` fournit des signatures FLIRT pour différentes versions de `rustc`.

L'application des signatures FLIRT dans IDA se fait via le menu File → Load File → FLIRT signature file, puis en sélectionnant le fichier `.sig` approprié.

---

## Scripts Ghidra pour l'analyse Rust

Au-delà des signatures, quelques scripts Ghidra (Java ou Python) automatisent des tâches récurrentes de l'analyse Rust.

### Script de classification par crate

Ce script parcourt les symboles démanglés, extrait le nom du crate de chaque fonction, et crée des namespaces correspondants dans Ghidra :

```python
# classify_rust_crates.py — Script Ghidra (Jython)
# Classe les fonctions Rust par crate dans des namespaces Ghidra

from ghidra.program.model.symbol import SourceType

fm = currentProgram.getFunctionManager()  
st = currentProgram.getSymbolTable()  
ns_cache = {}  

for func in fm.getFunctions(True):
    name = func.getName()
    # Les fonctions démanglées contiennent "::" comme séparateur
    if "::" not in name:
        continue
    crate = name.split("::")[0]
    if crate not in ns_cache:
        ns_cache[crate] = st.createNameSpace(
            None, crate, SourceType.ANALYSIS
        )
    func.setParentNamespace(ns_cache[crate])

print("Classified {} crates".format(len(ns_cache)))  
for crate, ns in sorted(ns_cache.items()):  
    count = len([f for f in fm.getFunctions(True)
                 if f.getParentNamespace() == ns])
    print("  {} : {} functions".format(crate, count))
```

Après exécution, le Symbol Tree affiche les fonctions regroupées par crate (`core`, `alloc`, `std`, `crackme_rust`, etc.), ce qui rend la navigation immédiatement plus productive.

### Script d'annotation des panics

Ce script recherche les références aux chaînes de panique dans `.rodata` et annote les fonctions correspondantes :

```python
# annotate_rust_panics.py — Script Ghidra (Jython)
# Ajoute des commentaires aux sites de panique Rust

from ghidra.program.model.listing import CodeUnit

listing = currentProgram.getListing()  
mem = currentProgram.getMemory()  
strings_found = 0  

# Chercher les chaînes de panique dans .rodata
for block in mem.getBlocks():
    if block.getName() != ".rodata":
        continue
    # Parcourir les données définies dans .rodata
    data = listing.getDefinedData(block.getStart(), True)
    while data is not None and block.contains(data.getAddress()):
        val = data.getDefaultValueRepresentation()
        if val and ("panicked at" in val or "unwrap()" in val
                     or "index out of bounds" in val):
            # Trouver les XREF vers cette chaîne
            refs = getReferencesTo(data.getAddress())
            for ref in refs:
                func = getFunctionContaining(ref.getFromAddress())
                if func:
                    listing.setComment(
                        ref.getFromAddress(),
                        CodeUnit.EOL_COMMENT,
                        "RUST PANIC: " + val[:60]
                    )
                    strings_found += 1
        data = listing.getDefinedData(data.getAddress().next(), True)

print("Annotated {} panic sites".format(strings_found))
```

Ces annotations sont visibles dans le Listing et dans le décompilateur, ce qui permet de repérer instantanément les chemins de panique dans le code sans avoir à suivre chaque XREF manuellement.

---

## Workflow intégré : de l'import au code applicatif

Pour conclure ce chapitre, voici le workflow complet recommandé pour analyser un binaire Rust inconnu, de l'import initial jusqu'à l'isolation du code applicatif.

**Phase 1 — Triage (5 minutes)**

```bash
$ file target_binary
$ strings -n 3 target_binary | grep -E '\.rs:|panicked|rustc|RUST'
$ readelf -p .comment target_binary
$ checksec --file=target_binary
$ readelf -S target_binary | wc -l
$ nm target_binary 2>/dev/null | head -5    # Strippé ou non ?
```

Résultat attendu : vous savez que c'est du Rust, si c'est strippé, et quelle version probable de `rustc` a été utilisée.

**Phase 2 — Import et signatures (10 minutes)**

1. Importez le binaire dans Ghidra, activez l'analyse complète.  
2. Activez le Demangler Rust dans les options d'analyse.  
3. Appliquez les signatures FIDB correspondant à la version de `rustc` identifiée.  
4. Vérifiez le taux de reconnaissance dans le Symbol Table.

**Phase 3 — Classification (10 minutes)**

1. Exécutez le script de classification par crate (si le binaire a des symboles).  
2. Si strippé : identifiez le `main` applicatif via le point d'entrée, puis explorez le graphe d'appels en profondeur.  
3. Annotez les fonctions de panique avec le script dédié.

**Phase 4 — Analyse ciblée**

1. Concentrez-vous sur les fonctions applicatives identifiées.  
2. Appliquez les types Ghidra `RustStr`, `RustString`, `PanicLocation` aux emplacements pertinents.  
3. Utilisez les patterns de la section 33.3 pour décoder les constructions `Option`, `Result`, `match`.  
4. Utilisez les patterns de la section 33.4 pour retrouver les chaînes et les comparaisons.

À ce stade, vous travaillez sur un sous-ensemble maîtrisable de fonctions, avec des types annotés et des noms lisibles — les conditions optimales pour une analyse approfondie.

---

> 📌 **Fin du chapitre 33.** Vous disposez maintenant des connaissances et des outils nécessaires pour aborder le reverse engineering de binaires Rust compilés avec la toolchain GNU. Les patterns présentés dans ce chapitre couvrent la grande majorité des constructions que vous rencontrerez en pratique. Pour aller plus loin, le chapitre 34 applique une démarche similaire aux binaires Go — un autre langage à linking statique dont le RE présente ses propres défis.

⏭️ [Chapitre 34 — Reverse Engineering de binaires Go](/34-re-go/README.md)
