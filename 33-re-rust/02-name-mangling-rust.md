🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 33.2 — Name mangling Rust vs C++ : décoder les symboles

> 🏷️ Le name mangling est la transformation que le compilateur applique aux noms de fonctions et de types pour les rendre uniques dans la table des symboles. Rust et C++ utilisent deux schémas radicalement différents. Les reconnaître au premier coup d'œil et savoir les décoder est l'un des premiers réflexes à acquérir face à un binaire non strippé.

---

## Pourquoi le name mangling existe

Un binaire ELF ne peut pas contenir deux symboles portant le même nom. Or, dans un langage comme Rust ou C++, il est courant d'avoir plusieurs fonctions portant le même nom « humain » : méthodes dans différents modules, fonctions génériques instanciées avec différents types, implémentations de traits pour différents types, etc.

Le compilateur résout ce problème en encodant dans le nom du symbole toute l'information nécessaire pour le rendre unique : le chemin du module (crate, sous-module), le nom du type, le nom de la méthode, les paramètres génériques, et un hash de désambiguïsation.

Pour l'analyste RE, le symbole manglé est une mine d'information — **à condition de savoir le lire**.

---

## Le mangling C++ (Itanium ABI) — rappel

Comme vu au chapitre 17.1, les compilateurs C++ conformes à l'ABI Itanium (GCC, Clang) utilisent un schéma de mangling qui commence par le préfixe `_Z`. Voici un rappel rapide :

```
Symbole manglé :  _ZN7MyClass6methodEi
                  ││ │       │      ││
                  │╰─┤       │      │╰── type du paramètre : int
                  │  │       │      ╰─── E = fin de la partie qualifiée
                  │  │       ╰────────── "method" (6 caractères)
                  │  ╰────────────────── "MyClass" (7 caractères)
                  ╰───────────────────── _ZN = symbole C++ dans un namespace

Démanglé :        MyClass::method(int)
```

Les caractéristiques du mangling Itanium que l'analyste RE reconnaît instantanément :

- Préfixe `_Z` (ou `_ZN` pour les noms qualifiés).  
- Les noms sont encodés en « longueur-préfixée » : le chiffre indique le nombre de caractères qui suivent.  
- Les types de paramètres sont encodés par des lettres : `i` = `int`, `d` = `double`, `Ss` = `std::string`, etc.  
- L'outil `c++filt` les décode parfaitement.

---

## Le mangling Rust — le format « v0 »

Rust a utilisé plusieurs schémas de mangling au fil de son évolution. Le schéma actuel, appelé **« v0 »** (ou « RFC 2603 »), est stable depuis Rust 1.57 (fin 2021). C'est celui que l'on rencontre dans tous les binaires Rust récents.

### Reconnaître un symbole Rust

Le critère est simple : **un symbole Rust v0 commence par `_R`**.

```
_RNvNtCs9g8eSEAj0m_13crackme_rust17ChecksumValidator3new
^^
Préfixe Rust v0
```

Ce préfixe `_R` est l'équivalent du `_Z` du C++. Dès que vous voyez `_R` dans une table de symboles, vous savez que c'est du Rust.

> ⚠️ **Attention au mangling legacy.** Les binaires compilés avec des versions de Rust antérieures à 1.57 (ou avec l'option `-C symbol-mangling-version=legacy`) utilisent un ancien schéma qui ressemble superficiellement au mangling C++ : les symboles commencent par `_ZN` et contiennent un hash de 17 caractères hexadécimaux à la fin (préfixé par `h`). Ce format est de plus en plus rare, mais il peut tromper un analyste qui le confondrait avec du C++ Itanium. L'indice de différenciation est justement ce suffixe `h` suivi d'un hash hexadécimal de longueur fixe.

### Anatomie d'un symbole Rust v0

Décortiquons un symbole complet tiré de notre crackme :

```
_RNvNtCs9g8eSEAj0m_13crackme_rust17ChecksumValidator3new
```

Le décodage se lit de gauche à droite :

```
_R                         → Préfixe : symbole Rust v0
  N                        → Namespace path (début du chemin qualifié)
   v                       → "value" namespace (fonction, constante)
    Nt                     → Nested dans un "type" namespace
      Cs9g8eSEAj0m_       → Crate hash (identifiant unique du crate)
        13crackme_rust     → Nom du crate : "crackme_rust" (13 caractères)
          17ChecksumValidator → Nom du type : "ChecksumValidator" (17 chars)
            3new           → Nom de la fonction : "new" (3 chars)
```

Le résultat démanglé est :

```
crackme_rust::ChecksumValidator::new
```

### Les préfixes de namespace

Le mangling v0 distingue deux types de namespaces, encodés par une lettre minuscule après `N` :

| Lettre | Namespace | Signification |  
|---|---|---|  
| `v` | Value | Fonctions, constantes, variables statiques |  
| `t` | Type | Types (`struct`, `enum`, `trait`, `impl`) |

Cette distinction est utile en RE : elle vous indique immédiatement si un symbole est une fonction ou un type, avant même de le démanger.

### Les types génériques

Quand une fonction Rust est générique, les paramètres de type concrets sont encodés dans le symbole manglé. Par exemple, une instanciation de `Vec<u32>::push` pourrait apparaître comme :

```
_RNvMs_NtCs...5alloc3vec8Vec$u20$u32$GT$4push
```

Les types sont encodés avec des marqueurs spéciaux : `$u20$` pour un espace, `$GT$` pour `>`, `$LT$` pour `<`, `$RF$` pour `&`, etc. Le symbole démanglé donne :

```
<alloc::vec::Vec<u32>>::push
```

Cette information est précieuse : elle vous dit exactement quel type concret est manipulé, ce qui aide à reconstruire les structures de données du programme.

### Le hash de crate

Chaque crate compilé se voit attribuer un hash unique (la séquence `Cs9g8eSEAj0m_` dans notre exemple). Ce hash garantit l'unicité même si deux crates portent le même nom. Pour l'analyste RE, ce hash permet de **regrouper toutes les fonctions d'un même crate** en filtrant sur un préfixe commun, ce qui est très utile pour séparer le code applicatif du code de la stdlib.

---

## Décoder les symboles en pratique

### `rustfilt` — le `c++filt` de Rust

L'outil de référence pour démanger les symboles Rust est **`rustfilt`**, installable via `cargo` :

```bash
$ cargo install rustfilt
```

Son usage est identique à `c++filt` — il lit sur l'entrée standard et remplace les symboles manglés par leur forme lisible :

```bash
$ echo "_RNvNtCs9g8eSEAj0m_13crackme_rust17ChecksumValidator3new" | rustfilt
crackme_rust::ChecksumValidator::new
```

On peut l'utiliser en pipe avec `nm`, `objdump` ou n'importe quel outil qui affiche des symboles :

```bash
$ nm crackme_rust_release | rustfilt | grep crackme_rust
```

Cette commande affiche tous les symboles applicatifs du crackme sous forme démanglée, ce qui donne une cartographie immédiate du programme.

### `nm` avec démanglement intégré

Les versions récentes de `nm` (binutils ≥ 2.36) reconnaissent le mangling Rust v0 nativement via l'option `--demangle` (ou `-C`) :

```bash
$ nm -C crackme_rust_release | grep crackme_rust
```

Selon la version de binutils, le résultat peut être partiel ou complet. Si le démanglement semble incomplet, passez par `rustfilt` qui est toujours à jour avec les évolutions du format.

### `c++filt` — ne fonctionne PAS pour le Rust v0

C'est un piège classique. L'outil `c++filt` ne comprend que le mangling Itanium C++. Appliqué à un symbole Rust v0, il le laisse inchangé :

```bash
$ echo "_RNvNtCs9g8eSEAj0m_13crackme_rust17ChecksumValidator3new" | c++filt
_RNvNtCs9g8eSEAj0m_13crackme_rust17ChecksumValidator3new
```

Le symbole ressort tel quel — `c++filt` ne sait pas quoi en faire. En revanche, `c++filt` fonctionne sur les symboles Rust au format **legacy** (ceux en `_ZN...h<hash>`) puisqu'ils empruntent la syntaxe Itanium. Mais le résultat est alors trompeur car les conventions de nommage internes ne sont pas les mêmes.

> 💡 **Règle simple** : `_Z` → `c++filt` ; `_R` → `rustfilt`.

### Démanglement dans les désassembleurs

Les désassembleurs modernes gèrent le mangling Rust v0 à des degrés divers :

**Ghidra** (≥ 10.2) reconnaît et démangle automatiquement les symboles Rust v0 lors de l'import. Les noms de fonctions apparaissent sous leur forme lisible dans le Symbol Tree et le Listing. Si le démanglement ne se fait pas automatiquement, vérifiez que l'option « Demangle Rust » est activée dans les options d'analyse (Analysis → One Shot → Demangler Rust).

**IDA** (≥ 7.7 / IDA Free récent) supporte le démanglement Rust dans ses versions récentes. Les versions plus anciennes laissent les symboles manglés, auquel cas un script IDAPython appelant `rustfilt` en sous-processus permet de combler le manque.

**Radare2 / Cutter** supporte le démanglement Rust via la commande `iDr` (list demangled Rust symbols) ou en activant le démanglement global avec `e asm.demangle=true`. Le support s'est amélioré progressivement au fil des versions.

**Binary Ninja** gère le mangling Rust v0 nativement dans ses versions récentes.

---

## Comparaison côte à côte : Rust v0 vs C++ Itanium

Le tableau suivant met en parallèle les deux schémas pour des constructions analogues. Cette comparaison aide à développer le réflexe de reconnaissance.

| Concept | C++ (Itanium) | Rust (v0) |  
|---|---|---|  
| **Préfixe** | `_Z` | `_R` |  
| **Fonction libre** | `_Z3fooi` → `foo(int)` | `_RNvCs..._7mycrate3foo` → `mycrate::foo` |  
| **Méthode** | `_ZN7MyClass3barEv` → `MyClass::bar()` | `_RNvNtCs..._7mycrate7MyClass3bar` → `mycrate::MyClass::bar` |  
| **Namespace imbriqué** | `_ZN2ns7MyClass3barEi` → `ns::MyClass::bar(int)` | `_RNvNtNtCs..._7mycrate2ns7MyClass3bar` → `mycrate::ns::MyClass::bar` |  
| **Template / Générique** | `_Z3fooIiEvT_` → `void foo<int>(int)` | Types encodés dans le chemin avec `$LT$`, `$GT$` |  
| **Encodage des types** | Lettres compactes (`i`, `d`, `Ss`…) | Pas d'encodage des types de paramètres dans le symbole |  
| **Hash / Désambiguïsation** | Non (sauf ABI tag) | Hash de crate systématique |  
| **Outil de démanglement** | `c++filt` | `rustfilt` |

Une différence fondamentale : le mangling C++ encode les **types des paramètres** de la fonction (pour distinguer les surcharges), alors que le mangling Rust ne le fait pas. Rust n'a pas de surcharge de fonctions au sens du C++ — deux fonctions dans le même scope ne peuvent pas avoir le même nom avec des signatures différentes. L'unicité est garantie par le chemin de module et le hash de crate.

---

## Exploiter les symboles démanglés pour le RE

Sur un binaire Rust non strippé, les symboles démanglés fournissent une cartographie quasi complète du programme. Voici comment en tirer le maximum.

### Séparer le code applicatif de la stdlib

Le hash de crate dans chaque symbole permet de filtrer par provenance. Toutes les fonctions de notre crackme partagent le même hash de crate :

```bash
$ nm crackme_rust_release | rustfilt | grep '^[0-9a-f]* T' | \
    grep 'crackme_rust::' | head -15
```

À l'inverse, les fonctions de la stdlib commencent par des préfixes connus :

```bash
$ nm crackme_rust_release | rustfilt | grep -E '(core|std|alloc)::' | wc -l
```

Ce simple filtrage sépare les quelques dizaines de fonctions applicatives des milliers de fonctions de la stdlib, ce qui réduit drastiquement la surface d'analyse.

### Reconstruire l'architecture du programme

Les symboles démanglés révèlent directement la structure modulaire :

```
crackme_rust::main  
crackme_rust::print_banner  
crackme_rust::usage_and_exit  
crackme_rust::determine_license  
crackme_rust::ValidationPipeline::new  
crackme_rust::ValidationPipeline::add  
crackme_rust::ValidationPipeline::run  
crackme_rust::PrefixValidator::validate      (via <PrefixValidator as Validator>::validate)  
crackme_rust::FormatValidator::validate       (via <FormatValidator as Validator>::validate)  
crackme_rust::ChecksumValidator::new  
crackme_rust::ChecksumValidator::validate     (via <ChecksumValidator as Validator>::validate)  
crackme_rust::LicenseType::max_features  
```

En une commande, on obtient la liste de toutes les fonctions, leurs modules d'appartenance, et les relations d'implémentation de trait (les symboles `<Type as Trait>::method` indiquent explicitement quel type implémente quel trait). C'est l'équivalent d'un diagramme de classes, extrait directement du binaire.

### Identifier les instanciations de génériques

Les symboles des fonctions génériques instanciées contiennent les types concrets. En cherchant les instanciations de types standard, on peut déduire quels types l'application utilise :

```bash
$ nm crackme_rust_release | rustfilt | grep 'Vec<' | sort -u
```

Cette commande liste toutes les instanciations de `Vec` dans le binaire. Si vous voyez `Vec<Box<dyn crackme_rust::Validator>>`, vous savez que le programme stocke des validateurs polymorphes dans un vecteur — une information structurelle de haut niveau obtenue sans lire une seule instruction assembleur.

### Retrouver les implémentations de trait

Les symboles des vtables de trait (voir section 33.3 pour le détail des trait objects) apparaissent avec des noms explicites :

```bash
$ nm crackme_rust_release | rustfilt | grep 'vtable'
```

Chaque vtable listée correspond à une implémentation concrète d'un trait pour un type donné. C'est l'équivalent direct des vtables C++ du chapitre 17.2, mais avec un nommage qui encode explicitement la relation `Type → Trait`.

---

## Que faire quand le binaire est strippé ?

Sur un binaire strippé, les symboles manglés ont disparu. Mais tout n'est pas perdu. Voici les stratégies de repli, classées par efficacité :

**1. Les chaînes de panique.** Comme vu en section 33.1, les messages de panique contiennent les chemins source (`src/main.rs:42:5`). En croisant le numéro de ligne avec l'adresse du code qui référence cette chaîne (via les XREF dans Ghidra), on peut attribuer un nom approximatif à chaque fonction.

**2. Les signatures de fonctions connues.** Des projets communautaires fournissent des bases de signatures pour la stdlib Rust, utilisables dans Ghidra (format FIDB) ou IDA (format FLIRT). Nous les détaillerons en section 33.6. Appliquer ces signatures permet de nommer automatiquement des centaines de fonctions de la stdlib, ce qui dégage le code applicatif.

**3. La comparaison avec un binaire de référence.** Si vous pouvez compiler un programme Rust avec la même version de `rustc` et la même cible, vous obtenez un binaire de référence dont les fonctions stdlib sont au même offset relatif. Les outils de diffing binaire (chapitre 10) permettent alors de transférer les noms des fonctions identifiées.

**4. Les patterns structurels.** Même sans nom, les fonctions Rust ont des signatures reconnaissables au niveau assembleur (section 33.3). Un `unwrap` génère toujours le même motif : test du discriminant suivi d'un branchement vers un appel de panique. Un analyste entraîné finit par reconnaître ces constructions à vue.

---

## Résumé : les réflexes à acquérir

| Situation | Action |  
|---|---|  
| Vous voyez `_R` dans `nm` | C'est du Rust v0 → utilisez `rustfilt` |  
| Vous voyez `_ZN...h<16 hex>` | C'est du Rust legacy → `c++filt` fonctionne partiellement, préférez `rustfilt` |  
| Vous voyez `_ZN...` sans hash `h` | C'est du C++ Itanium classique → `c++filt` |  
| Binaire non strippé | `nm -C` ou `nm \| rustfilt` pour cartographier le programme en 30 secondes |  
| Binaire strippé | Chaînes de panique + signatures stdlib + diffing pour reconstruire les noms |  
| Dans Ghidra | Vérifiez que « Demangler Rust » est activé dans les options d'analyse |  
| Recherche ciblée | Filtrez par nom de crate pour isoler le code applicatif |

---

> **Section suivante : 33.3 — Reconnaître les patterns Rust : `Option`, `Result`, `match`, panics** — nous passons du niveau symboles au niveau instructions pour identifier les constructions idiomatiques de Rust dans le désassemblage.

⏭️ [Reconnaître les patterns Rust : `Option`, `Result`, `match`, panics](/33-re-rust/03-patterns-option-result-match.md)
