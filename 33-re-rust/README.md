🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 33 — Reverse Engineering de binaires Rust

> 🦀 *Rust produit des binaires natifs ELF qui passent par le linker GNU, mais leur reverse engineering est une expérience radicalement différente de celle d'un binaire C ou C++. Ce chapitre vous donne les clés pour ne pas vous noyer dans un océan de symboles et de code généré.*

---

## Pourquoi un chapitre dédié à Rust ?

À première vue, un binaire Rust ressemble à n'importe quel exécutable ELF : même format, mêmes sections `.text`, `.data`, `.rodata`, même linker `ld` en bout de chaîne. On pourrait donc penser que les techniques vues dans les parties précédentes suffisent. En pratique, ouvrir un binaire Rust dans Ghidra ou Radare2 pour la première fois est souvent un choc.

Là où un programme C compilé avec GCC produit un binaire de quelques dizaines de kilo-octets avec une poignée de fonctions clairement identifiables, un « Hello, World! » en Rust pèse couramment **plusieurs mégaoctets** et contient **des milliers de fonctions**. La raison est simple : Rust lie statiquement sa bibliothèque standard par défaut, et le compilateur génère une quantité considérable de code pour ses abstractions à coût zéro — abstractions qui, au niveau assembleur, ont bel et bien un coût en volume de code à analyser.

Plusieurs caractéristiques de Rust rendent son RE spécifique :

**Un name mangling distinct du C++.** Rust utilise son propre schéma de décoration des symboles (le format « v0 » stabilisé depuis 2021), différent du mangling Itanium du C++. Les outils classiques comme `c++filt` ne fonctionnent pas ; il faut `rustfilt` ou des démangleurs intégrés aux désassembleurs récents. Les noms démanglés sont souvent longs et incluent le chemin complet du crate, du module et des paramètres génériques.

**Des patterns de code récurrents et reconnaissables.** Le système de types de Rust — avec `Option<T>`, `Result<T, E>`, le pattern matching exhaustif et la gestion des `panic!` — se traduit en assembleur par des motifs répétitifs. Un `unwrap()` génère systématiquement un branchement vers du code de panique. Un `match` sur un `enum` produit des tables de sauts ou des cascades de comparaisons avec des layouts mémoire prévisibles. Apprendre à reconnaître ces idiomes accélère considérablement l'analyse.

**Une gestion des chaînes de caractères sans terminateur nul.** Les `&str` de Rust sont des *fat pointers* composés d'un pointeur et d'une longueur, sans le `\0` final du C. Cela signifie que l'outil `strings` rate fréquemment des chaînes ou les découpe mal. La commande `strings` reste utile pour un premier triage, mais il faut être conscient de cette limitation et savoir où chercher les structures `(ptr, len)` en mémoire.

**Des binaires volumineux avec linking statique.** Par défaut, `cargo build` en mode release produit un binaire qui embarque l'intégralité de la bibliothèque standard utilisée, plus toutes les dépendances (crates). Le graphe d'appels est massif, et distinguer le code applicatif du code de la stdlib ou des crates tierces devient un défi majeur. Des outils comme `cargo-bloat` (côté développeur) et les signatures de fonctions connues dans Ghidra aident à filtrer le bruit.

**L'absence d'héritage et de vtables classiques.** Contrairement au C++, Rust n'a pas d'héritage de classes ni de vtables au sens traditionnel. Le polymorphisme passe par les *trait objects* (`dyn Trait`), qui utilisent des *fat pointers* contenant un pointeur vers les données et un pointeur vers une vtable de trait. Le layout est différent de celui du C++ et demande une approche spécifique pour reconstruire les types dynamiques.

---

## Ce que vous allez apprendre

Ce chapitre couvre les aspects essentiels du reverse engineering de binaires Rust compilés avec la toolchain GNU :

- Comment Rust interagit avec la toolchain GNU lors de la compilation et du linking, et ce que cela implique pour les symboles et les sections ELF.  
- Le fonctionnement du name mangling Rust et les outils pour décoder les symboles.  
- Les patterns assembleur caractéristiques de `Option`, `Result`, `match` et des panics, afin de les reconnaître sans hésiter dans un listing.  
- La représentation mémoire des chaînes Rust (`&str` et `String`) et les pièges que cela pose pour les outils d'analyse classiques.  
- Pourquoi les binaires Rust sont si volumineux et comment isoler le code applicatif du code de bibliothèque.  
- Les outils spécialisés (`rustfilt`, `cargo-bloat`, signatures Ghidra pour la stdlib Rust) qui rendent l'analyse praticable.

---

## Prérequis pour ce chapitre

Ce chapitre suppose que vous maîtrisez les fondamentaux couverts dans les parties précédentes :

- L'analyse statique avec Ghidra ou un désassembleur équivalent (chapitres 8–9).  
- La lecture d'assembleur x86-64 et les conventions d'appel System V AMD64 (chapitre 3).  
- La structure d'un binaire ELF : sections, symboles, linking dynamique vs statique (chapitre 2).  
- L'utilisation des outils de triage (`file`, `strings`, `readelf`, `checksec`) pour un premier contact avec un binaire inconnu (chapitre 5).

Une connaissance du langage Rust n'est pas strictement nécessaire, mais avoir écrit quelques programmes simples en Rust (manipulation de `Option`, `Result`, `String`, itérateurs) facilitera grandement la compréhension des patterns assembleur présentés.

---

## Binaire d'entraînement

Le binaire utilisé tout au long de ce chapitre est `crackme_rust`, situé dans `binaries/ch33-rust/`. Le code source Rust est fourni dans `binaries/ch33-rust/crackme_rust/src/main.rs` avec son `Cargo.toml`.

Le `Makefile` associé permet de produire plusieurs variantes :

| Variante | Commande | Description |  
|---|---|---|  
| Debug avec symboles | `make debug` | Binaire non optimisé, symboles complets — idéal pour apprendre les patterns |  
| Release | `make release` | Optimisé (`-O3` équivalent), symboles présents |  
| Release strippé | `make release-strip` | Optimisé et strippé — le cas réaliste |

Commencez par compiler toutes les variantes et observez déjà la différence de taille entre elles : c'est votre premier indice sur ce qui rend le RE Rust particulier.

---

## Plan du chapitre

- **33.1** — Spécificités de compilation Rust avec la toolchain GNU (linking, symboles)  
- **33.2** — Name mangling Rust vs C++ : décoder les symboles  
- **33.3** — Reconnaître les patterns Rust : `Option`, `Result`, `match`, panics  
- **33.4** — Strings en Rust : `&str` vs `String` en mémoire (pas de null terminator)  
- **33.5** — Bibliothèques embarquées et taille des binaires (tout est linké statiquement)  
- **33.6** — Outils spécifiques : `cargo-bloat`, signatures Ghidra pour la stdlib Rust

---

> **Commencez par la section 33.1** pour comprendre comment `rustc` s'appuie sur la toolchain GNU et ce que cela change pour l'analyste.

⏭️ [Spécificités de compilation Rust avec la toolchain GNU (linking, symboles)](/33-re-rust/01-compilation-toolchain-gnu.md)
