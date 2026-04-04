🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 33.1 — Spécificités de compilation Rust avec la toolchain GNU (linking, symboles)

> 🦀 Rust dispose de son propre compilateur (`rustc`) et de son propre gestionnaire de build (`cargo`), mais en bout de chaîne, c'est le **linker GNU `ld`** qui assemble le binaire final sur Linux. Comprendre cette articulation est essentiel pour l'analyste RE, car elle détermine ce qu'on retrouve — et ce qu'on ne retrouve pas — dans le binaire ELF produit.

---

## Vue d'ensemble de la chaîne de compilation Rust

Quand vous lancez `cargo build` ou `rustc`, voici ce qui se passe sous le capot :

```
              ┌──────────────────────────────────────────────────────┐
              │                    Compilateur Rust                  │
              │                                                      │
  main.rs ───▶│  Parsing ──▶ HIR ──▶ MIR ──▶ LLVM IR ──▶ Code objet  │
              │                                (.o / .rlib)          │
              └──────────────────────┬───────────────────────────────┘
                                     │
                                     ▼
              ┌──────────────────────────────────────────────────────┐
              │              Linker GNU (ld / cc)                    │
              │                                                      │
              │  .o applicatif                                       │
              │  + .rlib stdlib Rust (libstd, libcore, liballoc…)    │──▶ ELF exécutable
              │  + .a / .so bibliothèques C (libc, libpthread…)      │
              │  + crt0 / crti / crtn (C runtime startup)            │
              └──────────────────────────────────────────────────────┘
```

Deux points essentiels ressortent de ce schéma :

1. **`rustc` ne produit pas directement un exécutable.** Il génère du code objet (`.o`) via le backend LLVM, puis délègue le linking au linker système — par défaut `cc` (qui invoque `ld` en interne) sur les distributions GNU/Linux.

2. **Le binaire final passe par le C runtime.** Même un programme Rust « pur » est linké avec `crt0.o`, `crti.o`, `crtn.o` et la `libc` système. Le véritable point d'entrée ELF n'est pas `main()` mais `_start`, qui appelle `__libc_start_main`, qui finit par appeler le `main` Rust (après l'initialisation du runtime Rust). Ce détail est identique au C classique, et l'analyste RE retrouve les mêmes patterns au tout début de l'exécution.

---

## Rust et LLVM : pourquoi ce n'est pas GCC

Contrairement aux binaires C/C++ traités dans les chapitres précédents (compilés directement par GCC), Rust utilise **LLVM** comme backend de génération de code. Le compilateur `rustc` transforme le code source en une représentation intermédiaire LLVM (LLVM IR), puis LLVM produit le code machine.

Cela a des conséquences directes sur ce que l'analyste observe dans le désassemblage :

**Les idiomes assembleur diffèrent de ceux de GCC.** LLVM et GCC n'émettent pas le même code machine pour les mêmes opérations. Par exemple, LLVM a tendance à utiliser `cmov` (conditional move) plus agressivement que GCC, à organiser les blocs de base différemment, et à produire des prologues/épilogues de fonctions avec des séquences légèrement distinctes. Si vous êtes habitué aux patterns GCC du chapitre 16, attendez-vous à des différences subtiles mais perceptibles.

**Les optimisations sont celles de LLVM, pas de GCC.** Les passes d'optimisation (inlining, déroulage de boucles, vectorisation, élimination de code mort) sont appliquées par LLVM. Le résultat optimisé ressemble à ce que produirait Clang (le compilateur C/C++ de LLVM) plutôt que GCC. Si vous avez déjà comparé GCC et Clang comme au chapitre 16.7, vous retrouverez les mêmes « saveurs » LLVM dans les binaires Rust.

**Mais le linker reste GNU.** C'est le point de convergence avec la chaîne GNU : une fois le code objet produit par LLVM, c'est bien `ld` (ou `gold`, ou `mold` si configuré) qui effectue le linking. Les sections ELF, les tables de symboles, la résolution PLT/GOT, le RELRO — tout cela est identique à un binaire C linké avec la toolchain GNU. L'analyste peut donc appliquer les mêmes techniques d'inspection ELF vues au chapitre 2.

> 💡 **Pour l'analyste RE**, cette distinction signifie que vos outils d'inspection ELF (`readelf`, `objdump`, `checksec`) fonctionnent exactement comme sur un binaire C, mais que les patterns au niveau instruction seront « LLVM-flavored » plutôt que « GCC-flavored ».

---

## Le linking en détail : qu'est-ce qui entre dans le binaire ?

### Linking statique de la stdlib Rust

Par défaut, Rust lie **statiquement** sa bibliothèque standard. Cela inclut :

- **`libcore`** — types primitifs, `Option`, `Result`, itérateurs, traits fondamentaux.  
- **`liballoc`** — allocateur mémoire, `Box`, `Vec`, `String`, `Rc`, `Arc`.  
- **`libstd`** — I/O, réseau, threads, filesystem, `HashMap`, et tout ce qui dépend de l'OS.  
- **`libpanic_unwind`** (ou `libpanic_abort`)** — mécanisme de panique.

Tout ce code se retrouve **embarqué dans le binaire final**. C'est pourquoi un « Hello, World! » en Rust pèse plusieurs mégaoctets alors que son équivalent C tient en quelques kilo-octets.

Vérifions avec notre crackme :

```bash
$ cd binaries/ch33-rust/
$ make debug release release-strip

$ ls -lh crackme_rust_*
-rwxr-xr-x 1 user user  15M  crackme_rust_debug
-rwxr-xr-x 1 user user 4.3M  crackme_rust_release
-rwxr-xr-x 1 user user 406K  crackme_rust_strip
```

> ⚠️ Les tailles exactes varient selon la version de `rustc` et la plateforme, mais les ordres de grandeur sont représentatifs.

Le binaire debug fait **~15 Mo** pour un programme d'une centaine de lignes. Le release optimisé descend à **~4 Mo** grâce à l'élimination du code mort par LLVM. Le strippé avec LTO et `panic=abort` passe sous le mégaoctet, car le LTO permet à LLVM d'éliminer encore plus de fonctions inutilisées et `panic=abort` supprime tout le mécanisme d'unwinding.

### Linking dynamique de la libc

Bien que la stdlib Rust soit linkée statiquement, la **libc système** (`glibc` ou `musl`) reste linkée **dynamiquement** par défaut. On peut le vérifier :

```bash
$ ldd crackme_rust_release
    linux-vdso.so.1 (0x00007ffc...)
    libgcc_s.so.1 => /lib/x86_64-linux-gnu/libgcc_s.so.1 (0x00007f...)
    libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f...)
    /lib64/ld-linux-x86-64.so.2 (0x00007f...)

$ ldd crackme_rust_debug
    # Même liste — la libc est toujours dynamique
```

On retrouve les dépendances classiques : `libc.so.6`, `libgcc_s.so.1` (pour le stack unwinding), et le loader `ld-linux-x86-64.so.2`. L'analyste retrouve donc les mécanismes PLT/GOT habituels pour les appels à la libc (`write`, `read`, `mmap`, `pthread_*`, etc.).

> 💡 Il est possible de produire un binaire Rust **entièrement statique** en compilant avec la cible `x86_64-unknown-linux-musl` (libc musl linkée statiquement). Dans ce cas, `ldd` affichera « not a dynamic executable » et il n'y aura ni PLT ni GOT — uniquement des syscalls directs. C'est un cas que l'on rencontre dans certains outils en ligne de commande distribués comme binaires uniques.

---

## Anatomie ELF d'un binaire Rust

Les sections ELF d'un binaire Rust sont les mêmes que celles d'un binaire C/C++. Rien de spécifique à Rust au niveau du format — c'est le contenu qui diffère.

```bash
$ readelf -S crackme_rust_release | grep -E '^\s+\[' | head -20
```

On retrouve les sections habituelles : `.text`, `.rodata`, `.data`, `.bss`, `.plt`, `.got`, `.got.plt`, `.eh_frame`, `.eh_frame_hdr`, `.symtab`, `.strtab`, `.dynstr`, `.dynsym`.

Quelques observations spécifiques :

**`.rodata` est massive.** Rust y stocke une quantité importante de données en lecture seule : les littéraux de chaînes (avec leurs longueurs), les messages de panique, les informations de formatage (`fmt::Arguments`), les tables de traits, et les données RTTI-like utilisées par le mécanisme de panique. Sur notre crackme en release, `.rodata` représente une part significative du binaire.

```bash
$ readelf -S crackme_rust_release | grep rodata
  [17] .rodata           PROGBITS    ...   0001a3c0  000000  ...
```

**`.eh_frame` est volumineuse quand `panic = "unwind"`.** Le mécanisme d'unwinding de Rust (identique dans son principe à celui du C++) repose sur les tables `.eh_frame` pour dérouler la pile en cas de panique. Avec le profil `release-strip` configuré en `panic = "abort"`, cette section est quasi absente, car le programme appelle directement `abort()` en cas de panique au lieu de dérouler la pile.

```bash
$ size crackme_rust_release crackme_rust_strip
   text    data     bss     dec     hex filename
 387234    9520     280  397034   60e6a crackme_rust_release
  78642    4896     280   83818   4776a crackme_rust_strip
```

La différence de taille du segment `text` entre les deux variantes illustre l'impact combiné du LTO et de `panic=abort`.

**Pas de section spécifique à Rust.** Contrairement à Go (qui possède des sections dédiées comme `gopclntab`, voir chapitre 34), Rust ne crée aucune section custom dans l'ELF. L'analyste n'a pas de « marqueur structurel » indiquant qu'il s'agit d'un binaire Rust — il doit le déduire du contenu (name mangling, messages de panique, patterns de code).

---

## Les symboles : une mine d'or quand ils sont présents

### Binaire non strippé

Sur un binaire Rust non strippé, la table des symboles est exceptionnellement riche. Le name mangling Rust (détaillé en section 33.2) encode le chemin complet de chaque fonction : crate, module, type, méthode, et paramètres génériques.

```bash
$ nm crackme_rust_release | wc -l
    8234

$ nm crackme_rust_release | grep 'T ' | head -10
```

Plus de 8 000 symboles pour un programme d'une centaine de lignes — la grande majorité provient de la stdlib. Parmi eux, on trouve nos fonctions applicatives :

```bash
$ nm crackme_rust_release | grep crackme_rust
```

Les symboles sont sous forme manglée. Par exemple, la fonction `ChecksumValidator::new` apparaît sous une forme comme :

```
_RNvMNtCs...crackme_rust...ChecksumValidator3new
```

Le préfixe `_R` identifie immédiatement un symbole Rust (format de mangling « v0 »). Nous détaillerons le décodage en section 33.2.

### Binaire strippé

Sur le binaire strippé, la situation change radicalement :

```bash
$ nm crackme_rust_strip
nm: crackme_rust_strip: no symbols

$ nm -D crackme_rust_strip | wc -l
    42
```

La table `.symtab` a été supprimée. Il ne reste que les symboles dynamiques (`.dynsym`), qui correspondent aux fonctions importées de la libc. Toute la logique applicative et la stdlib sont devenues anonymes.

C'est le scénario réaliste : en dehors du monde open source, les binaires Rust distribués sont presque toujours strippés. L'analyste doit alors s'appuyer sur d'autres indices pour reconstruire la structure du programme.

### Ce qui survit au strip

Même sur un binaire strippé, certains éléments restent exploitables :

**Les chaînes de caractères.** Les messages de panique contiennent systématiquement le chemin source (`src/main.rs:42:5`) et le message d'erreur. Ces chaînes survivent au strip car elles sont des données dans `.rodata`, pas des symboles.

```bash
$ strings crackme_rust_strip | grep -E '\.rs:'
src/main.rs:87:42  
src/main.rs:103:17  
library/core/src/fmt/mod.rs:...  
library/core/src/panicking.rs:...  
```

Ces chemins révèlent non seulement que c'est un binaire Rust, mais aussi la structure des modules et les numéros de ligne du code source. C'est un indice extrêmement précieux que l'on ne retrouve pas dans un binaire C strippé (sauf compilation avec `-g`, ce qui est rare en production).

> ⚠️ Avec `panic = "abort"` et `strip = true`, certains messages de panique sont néanmoins réduits ou éliminés. Le profil `release-strip` de notre `Cargo.toml` utilise `panic = "abort"`, ce qui supprime une partie de ces chaînes. En pratique, beaucoup de projets Rust gardent `panic = "unwind"` même en release, ce qui laisse davantage de traces.

**Les chaînes de la libc et du runtime.** Les appels à la libc passent par la PLT et les noms des fonctions importées restent visibles dans `.dynsym` :

```bash
$ readelf -d crackme_rust_strip | grep NEEDED
 0x0000000000000001 (NEEDED)    Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)    Shared library: [libc.so.6]
```

**Les constantes dans `.rodata`.** Les littéraux de chaînes applicatifs (nos messages d'erreur, le préfixe `RUST-`, la bannière, etc.) sont toujours présents :

```bash
$ strings crackme_rust_strip | grep -i "rust"
RUST-  
RustCrackMe-v3.3  
```

---

## Identifier un binaire Rust sans symboles

Quand on tombe sur un binaire ELF inconnu et strippé, comment savoir qu'il a été compilé en Rust ? Plusieurs heuristiques convergent :

**Les messages de panique de la stdlib.** La présence de chaînes contenant `panicked at`, `unwrap()`, `called \`Option::unwrap()\` on a \`None\` value`, ou des chemins de type `library/core/src/` ou `library/std/src/` est un marqueur quasi certain.

```bash
$ strings crackme_rust_strip | grep -c "panick"
```

**Les chemins source `.rs`.** Même strippé, le binaire contient souvent des chemins comme `src/main.rs`, `src/lib.rs`, ou des chemins de crates (`/home/user/.cargo/registry/src/...`). Le suffixe `.rs` et la structure `src/` sont caractéristiques.

**La taille anormalement élevée.** Un programme simple qui pèse plusieurs mégaoctets sans dépendances dynamiques exotiques suggère un linking statique massif — typique de Rust (et de Go, mais Go a d'autres marqueurs, voir chapitre 34).

**La présence de `libgcc_s.so.1` dans les dépendances.** Cette bibliothèque est nécessaire pour le stack unwinding et est quasi systématiquement présente sur les binaires Rust compilés avec `panic = "unwind"`.

**L'absence de symboles C++ manglés.** Si le binaire est volumineux, contient des messages de panique avec des chemins `.rs`, mais aucun symbole Itanium (`_Z...`), c'est très probablement du Rust et non du C++.

---

## Impact des options de compilation sur le RE

Le tableau suivant résume l'effet des principales options de compilation Rust sur la difficulté du reverse engineering. Ces options sont contrôlées via `Cargo.toml` (profils) ou via des flags passés directement à `rustc`.

| Option | Effet sur le binaire | Impact RE |  
|---|---|---|  
| `opt-level = 0` | Pas d'optimisation, code 1:1 avec le source | Lisible, correspondance directe avec le source Rust |  
| `opt-level = 3` | Inlining agressif, vectorisation, élimination | Fonctions fusionnées, flux de contrôle réorganisé |  
| `debug = true` | Informations DWARF complètes | Noms de variables, types, numéros de ligne dans GDB |  
| `debug = false` | Pas de DWARF | Perte des noms de variables et de la correspondance source |  
| `strip = true` | Suppression de `.symtab` et `.strtab` | Plus de noms de fonctions, analyse purement structurelle |  
| `lto = true` | Link-Time Optimization (inter-crate) | Frontières entre crates effacées, inlining cross-crate |  
| `panic = "unwind"` | Mécanisme d'unwinding complet | `.eh_frame` volumineuse, messages de panique riches |  
| `panic = "abort"` | `abort()` direct en cas de panique | Binaire plus petit, moins de chaînes de panique |  
| `codegen-units = 1` | Un seul thread de codegen | Meilleures optimisations globales, plus d'inlining |

La combinaison la plus difficile à reverser est celle de notre profil `release-strip` : `opt-level = 3` + `lto = true` + `strip = true` + `panic = "abort"` + `codegen-units = 1`. C'est aussi la combinaison la plus courante en production.

---

## La commande `cargo` et le linker : voir ce qui se passe

Pour observer la chaîne de compilation complète, y compris l'invocation du linker, on peut demander à `cargo` d'être verbeux :

```bash
$ cd binaries/ch33-rust/crackme_rust/
$ cargo build --release -v 2>&1 | tail -5
```

La dernière ligne de la sortie verbose montre l'appel au linker avec tous ses arguments. On y voit typiquement :

```
cc -m64 [...] crackme_rust.o [...] -lgcc_s -lutil -lrt -lpthread -lm -ldl -lc [...]
```

C'est un appel à `cc` (wrapper pour `gcc`) qui invoque le linker GNU en lui passant les fichiers objet Rust et les bibliothèques système. On retrouve `-lpthread` (threads POSIX, utilisés par le runtime Rust), `-ldl` (chargement dynamique), et `-lc` (libc).

> 💡 Cette commande est utile pour comprendre exactement quelles bibliothèques sont liées dynamiquement au binaire. En modifiant le linker (via la variable `RUSTFLAGS="-C linker=..."` ou via `.cargo/config.toml`), on peut aussi utiliser `mold` ou `lld` à la place de `ld` — ce qui ne change pas le format ELF mais peut affecter légèrement le layout des sections.

---

## Résumé pour l'analyste RE

Quand vous ouvrez un binaire Rust dans votre désassembleur :

- **Le format ELF est standard.** Tous les outils d'inspection (`readelf`, `objdump`, `checksec`, `nm`, `ldd`) fonctionnent normalement. Il n'y a rien de « magique » dans un binaire Rust au niveau du format.

- **Le volume de code est massif.** Ne soyez pas surpris de trouver des milliers de fonctions alors que le programme semble simple. La grande majorité provient de la stdlib Rust linkée statiquement. Votre priorité est d'isoler le code applicatif — nous verrons comment en section 33.5 et 33.6.

- **Les patterns assembleur sont LLVM, pas GCC.** Si vous connaissez les idiomes GCC, attendez-vous à des différences. En revanche, si vous avez déjà analysé des binaires Clang, vous serez en terrain familier.

- **Les symboles sont votre meilleur allié.** Quand ils sont présents, les symboles Rust encodent une quantité d'information considérable (chemin complet, types génériques). Quand ils sont absents, les chaînes dans `.rodata` (messages de panique, chemins source) prennent le relais comme premier indice structurel.

- **Le point d'entrée est classique.** `_start` → `__libc_start_main` → `main` Rust. Le runtime Rust initialise l'allocateur et le mécanisme de panique avant d'appeler votre `fn main()`, mais cette initialisation est un code connu et identifiable avec les signatures de la stdlib.

---

> **Section suivante : 33.2 — Name mangling Rust vs C++ : décoder les symboles** — nous plongerons dans le format de mangling « v0 » de Rust, les outils pour le décoder, et comment exploiter les symboles démanglés pour accélérer l'analyse.

⏭️ [Name mangling Rust vs C++ : décoder les symboles](/33-re-rust/02-name-mangling-rust.md)
