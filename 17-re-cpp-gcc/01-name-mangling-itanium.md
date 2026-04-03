🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.1 — Name mangling — règles Itanium ABI et démanglement

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Pourquoi le name mangling existe

En C, chaque fonction a un nom unique dans l'espace global. La fonction `connect` dans un fichier objet correspond exactement au symbole `connect` dans la table de symboles. Le linker n'a qu'à faire correspondre les noms.

Le C++ rend cette simplicité impossible. Le langage autorise la **surcharge** (plusieurs fonctions portant le même nom mais avec des paramètres différents), les **namespaces** (plusieurs fonctions portant le même nom dans des espaces de noms distincts), les **méthodes de classes** (le même nom de fonction dans des classes différentes), et les **templates** (la même fonction instanciée avec des types différents). Toutes ces entités doivent pourtant coexister dans une table de symboles plate au format ELF, où chaque symbole doit être unique.

Le **name mangling** (ou *name decoration*) est le mécanisme par lequel le compilateur C++ encode dans le nom du symbole toutes les informations nécessaires pour le rendre unique : le namespace, la classe, le nom de la fonction, les types de ses paramètres, les qualificateurs (`const`, `volatile`), et les paramètres de template. Le résultat est une chaîne opaque pour un humain mais parfaitement décodable par les outils.

Pour le reverse engineer, le name mangling est à la fois un obstacle et une mine d'or. Un obstacle parce que les symboles bruts sont illisibles. Une mine d'or parce qu'un symbole manglé contient **plus d'informations que le nom de fonction original** — il encode la signature complète, ce qui permet de reconstruire les prototypes sans accès au code source.

## L'Itanium C++ ABI

GCC, Clang et la plupart des compilateurs C++ sur Linux/macOS/FreeBSD suivent l'**Itanium C++ ABI**, un standard qui définit (entre autres) les règles de name mangling. Ce standard a été initialement conçu pour le processeur Intel Itanium (IA-64), mais il est devenu le standard de facto sur toutes les plateformes Unix, indépendamment de l'architecture processeur.

> ⚠️ **MSVC (Microsoft Visual C++) utilise un schéma de mangling complètement différent.** Les règles décrites ici ne s'appliquent pas aux binaires PE compilés avec MSVC. Cependant, si vous utilisez MinGW (GCC ciblant Windows), le mangling Itanium s'applique bien, même sur un binaire PE.

La spécification complète est publique et disponible dans le document *Itanium C++ ABI* (https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling). Ce qui suit est un sous-ensemble pratique couvrant les cas les plus fréquents en reverse engineering.

## Anatomie d'un symbole manglé

Tout symbole manglé selon l'Itanium ABI commence par le préfixe `_Z`. C'est le marqueur universel : si un symbole dans `nm` ou `objdump` commence par `_Z`, c'est un symbole C++ manglé. Si ce n'est pas le cas, c'est soit un symbole C, soit un symbole C++ déclaré `extern "C"`.

La structure générale d'un symbole manglé est :

```
_Z <encoding>
```

L'encoding se décompose en deux parties : un **nom qualifié** (qui inclut les namespaces et classes) et une **signature de types** (les paramètres de la fonction).

### Fonctions libres (non-membres)

Pour une fonction simple sans namespace ni classe, l'encoding est :

```
_Z <longueur_nom> <nom> <types_paramètres>
```

La longueur du nom est encodée en décimal, suivie du nom lui-même, suivi des codes de types des paramètres.

**Exemple — `int compute(int x, double y)` :**

```
_Z7computeid
│ │       ││
│ │       │└─ d = double (deuxième paramètre)
│ │       └── i = int (premier paramètre)
│ └───────── compute (7 caractères)
└─────────── préfixe C++ manglé
```

Le type de retour **n'est pas encodé** dans le mangling des fonctions ordinaires (il l'est uniquement pour les instances de templates, voir plus bas).

### Codes des types de base

Voici les codes des types fondamentaux les plus courants :

| Code | Type C++ | Remarque |  
|------|----------|----------|  
| `v` | `void` | Fonction sans paramètre |  
| `b` | `bool` | |  
| `c` | `char` | |  
| `h` | `unsigned char` | |  
| `s` | `short` | |  
| `t` | `unsigned short` | |  
| `i` | `int` | Le plus fréquent |  
| `j` | `unsigned int` | |  
| `l` | `long` | |  
| `m` | `unsigned long` | |  
| `x` | `long long` | |  
| `y` | `unsigned long long` | |  
| `f` | `float` | |  
| `d` | `double` | |  
| `e` | `long double` | |  
| `z` | `...` (ellipsis) | Fonctions variadiques |

Les types composés utilisent des préfixes :

| Préfixe | Signification | Exemple |  
|---------|---------------|---------|  
| `P` | Pointeur vers | `Pi` = `int*` |  
| `R` | Référence lvalue | `Ri` = `int&` |  
| `O` | Référence rvalue (C++11) | `Oi` = `int&&` |  
| `K` | `const` | `Ki` = `const int` |  
| `V` | `volatile` | `Vi` = `volatile int` |

Ces préfixes se combinent. Un `const int*` (pointeur vers const int) se code `PKi`, tandis qu'un `int* const` (pointeur const vers int) se code `KPi`. L'ordre de lecture est de droite à gauche, exactement comme la déclaration C++ se lit.

**Exemples de combinaisons :**

```
PKc        → const char*            (très fréquent : chaînes C)  
PRKi       → const int&             (passage par référence constante)  
PPi        → int**                  (double pointeur)  
PFviE      → void(*)(int)           (pointeur de fonction : void(int))  
```

### Noms qualifiés (namespaces et classes)

Quand une fonction appartient à un namespace ou à une classe, le nom est enveloppé dans un bloc `N...E` (pour *nested name*) :

```
_Z N <qualificateur1> <qualificateur2> ... <nom_fonction> E <types>
```

Chaque qualificateur est encodé comme `<longueur><nom>`.

**Exemple — `MyApp::Network::Client::connect(int port)` :**

```
_ZN5MyApp7Network6Client7connectEi
  │ │     │       │      │       ││
  │ │     │       │      │       │└ i = int
  │ │     │       │      │       └─ E = fin du nom qualifié
  │ │     │       │      └──────── connect (7 chars)
  │ │     │       └─────────────── Client (6 chars)
  │ │     └─────────────────────── Network (7 chars)
  │ └───────────────────────────── MyApp (5 chars)
  └─────────────────────────────── N = début nom qualifié (nested)
```

### Méthodes const et qualificateurs

Une méthode `const` (le `const` sur `this`) est marquée par un `K` après le `N` :

```cpp
void Shape::describe() const;
```

```
_ZNK5Shape8describeEv
   ││                │
   │└ K = méthode const
   └─ N = nested
```

### Constructeurs et destructeurs

Les constructeurs et destructeurs ont des encodings spéciaux au lieu du nom de la fonction :

| Code | Signification |  
|------|---------------|  
| `C1` | Constructeur complet (*complete object constructor*) |  
| `C2` | Constructeur de base (*base object constructor*) |  
| `C3` | Constructeur d'allocation (*allocating constructor*, rare) |  
| `D0` | Destructeur avec `delete` (*deleting destructor*) |  
| `D1` | Destructeur complet (*complete object destructor*) |  
| `D2` | Destructeur de base (*base object destructor*) |

En pratique, GCC génère souvent **deux versions** du constructeur (C1 et C2) et **deux ou trois versions** du destructeur (D0, D1, D2). La différence concerne le comportement avec l'héritage virtuel : C1/D1 gèrent les sous-objets virtuels, C2/D2 non. Quand il n'y a pas d'héritage virtuel, GCC les fusionne généralement en une seule implémentation, mais les deux symboles existent.

**Exemple — constructeur de `Circle` :**

```
_ZN6CircleC1Eddd
   │      ││ │││
   │      ││ ││└ d = double (troisième param : r)
   │      ││ │└─ d = double (deuxième param : y)
   │      ││ └── d = double (premier param : x)
   │      │└──── 1 = complete object constructor
   │      └───── C = constructor
   └──────────── Circle (6 chars)
```

> 💡 **En RE :** quand vous voyez `C1` et `C2` pour la même classe avec un contenu identique, c'est normal. Concentrez-vous sur un seul des deux. Quand vous voyez `D0`, c'est le destructeur appelé via `delete` — il appelle le destructeur D1 puis libère la mémoire.

### Opérateurs surchargés

Les opérateurs utilisent des codes spéciaux commençant par un suffixe en deux lettres :

| Code | Opérateur | Code | Opérateur |  
|------|-----------|------|-----------|  
| `nw` | `new` | `dl` | `delete` |  
| `na` | `new[]` | `da` | `delete[]` |  
| `pl` | `+` | `mi` | `-` |  
| `ml` | `*` | `dv` | `/` |  
| `rm` | `%` | `an` | `&` (bitwise) |  
| `or` | `\|` | `eo` | `^` |  
| `aS` | `=` | `pL` | `+=` |  
| `mI` | `-=` | `mL` | `*=` |  
| `eq` | `==` | `ne` | `!=` |  
| `lt` | `<` | `gt` | `>` |  
| `le` | `<=` | `ge` | `>=` |  
| `ls` | `<<` | `rs` | `>>` |  
| `cl` | `()` | `ix` | `[]` |

**Exemple — `bool Shape::operator==(const Shape& other) const` :**

```
_ZNK5ShapeeqERKS_
         ││  │││
         ││  ││└ S_ = substitution (réfère à Shape, déjà mentionné)
         ││  │└─ K = const
         ││  └── R = référence
         │└───── eq = operator==
         └────── K = méthode const
```

Le `S_` ici est un mécanisme de **substitution** : pour éviter de répéter les types déjà encodés, l'ABI utilise des codes abrégés. `S_` désigne la première substitution candidate (ici, `Shape`). Les substitutions suivantes sont `S0_`, `S1_`, etc.

### Templates

Les instanciations de templates sont encodées avec `I...E` encadrant les arguments de template :

```
_Z <nom> I <args_template> E <types_params>
```

**Exemple — `void process<int, double>(int, double)` :**

```
_Z7processIidEvi d
         │ ││ │││ │
         │ ││ ││└ d = param double
         │ ││ │└─ i = param int
         │ ││ └── v = void (type retour, présent pour les templates)
         │ │└──── E = fin des args template
         │ └───── id = <int, double>
         └─────── I = début des args template
```

> 💡 **Attention :** pour les fonctions template, le type de retour **est** encodé dans le symbole (contrairement aux fonctions ordinaires). C'est une source fréquente de confusion lors du décodage manuel.

**Exemple avec notre `Registry` :**

La méthode `Registry<std::string, int>::add(const std::string&, const int&)` produit un symbole comme :

```
_ZN8RegistryINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEiE3addERKS5_RKi
```

Ce symbole est long, mais il encode complètement : la classe template `Registry`, ses paramètres (`std::string` et `int`), le nom de la méthode `add`, et les types des paramètres. En pratique, personne ne décode cela à la main — c'est le travail des outils.

### Substitutions standard

L'Itanium ABI définit des abréviations pour les types de la bibliothèque standard les plus courants :

| Code | Signification |  
|------|---------------|  
| `St` | `std::` |  
| `Sa` | `std::allocator` |  
| `Sb` | `std::basic_string` |  
| `Ss` | `std::basic_string<char, std::char_traits<char>, std::allocator<char>>` (= `std::string`) |  
| `Si` | `std::basic_istream<char, std::char_traits<char>>` |  
| `So` | `std::basic_ostream<char, std::char_traits<char>>` (= `std::ostream`) |  
| `Sd` | `std::basic_iostream<char, std::char_traits<char>>` |

> ⚠️ **Note GCC ≥ 5 (ABI C++11) :** GCC utilise `NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE` au lieu de `Ss` pour `std::string` depuis le passage à la nouvelle ABI. Vous verrez `__cxx11` dans les symboles — c'est le signe de la nouvelle ABI. L'ancienne ABI (GCC 4.x) utilisait `Ss`.

## Outils de démanglement

### `c++filt`

L'outil standard de la toolchain GNU pour démangler les symboles :

```bash
$ echo '_ZN6CircleC1Eddd' | c++filt
Circle::Circle(double, double, double)

$ echo '_ZNK5Shape8describeEv' | c++filt
Shape::describe() const

$ echo '_ZN8RegistryINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEiE3addERKS5_RKi' | c++filt
Registry<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, int>::add(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, int const&)
```

`c++filt` peut aussi filtrer un flux complet. Combinez-le avec `nm` ou `objdump` :

```bash
# Lister tous les symboles démanglés d'un binaire
$ nm oop_O0 | c++filt

# Désassembler avec noms démanglés
$ objdump -d -M intel oop_O0 | c++filt

# Chercher les symboles d'une classe spécifique
$ nm oop_O0 | c++filt | grep 'Circle::'
```

### `nm` avec l'option `-C`

`nm` intègre un démanglement automatique via le flag `-C` (ou `--demangle`) :

```bash
$ nm -C oop_O0 | grep 'Circle'
0000000000401a2c T Circle::Circle(double, double, double)
0000000000401a2c T Circle::Circle(double, double, double)
0000000000401b14 T Circle::area() const
0000000000401b38 T Circle::perimeter() const
0000000000401b64 T Circle::describe() const
0000000000401c98 T Circle::radius() const
```

On voit ici les deux symboles constructeur (C1 et C2) qui pointent vers la même adresse — GCC les a fusionnés.

### `objdump` avec l'option `-C`

```bash
$ objdump -d -C -M intel oop_O0 | head -30
```

Le flag `-C` active le démanglement dans le listing de désassemblage. Les `call` affichent alors le nom complet de la fonction appelée au lieu du symbole manglé.

### Démanglement dans Ghidra

Ghidra démantle automatiquement les symboles Itanium lors de l'import d'un binaire ELF. Les noms démanglés apparaissent dans le Symbol Tree, le Listing et le Decompiler. Pour voir le nom manglé original, cliquez droit sur un symbole et cherchez les propriétés ou consultez la vue « Defined Strings ».

Si le démanglement automatique ne fonctionne pas (par exemple après un import avec des options non standard), vous pouvez le déclencher manuellement :

1. Menu **Analysis** → **One Shot** → **Demangler GNU**.  
2. Ou via un script Ghidra qui appelle `DemanglerCmd`.

### Démanglement dans Radare2

```bash
# Activer le démanglement (activé par défaut)
$ r2 -A oop_O0
[0x00401080]> e bin.demangle = true

# Lister les fonctions avec noms démanglés
[0x00401080]> afl

# Désassembler une fonction avec nom démanglé
[0x00401080]> pdf @ sym.Circle::area
```

### Démanglement programmatique en Python

Pour le scripting RE, la bibliothèque `cxxfilt` (wrapper Python) ou `subprocess` avec `c++filt` :

```python
# Avec la bibliothèque cxxfilt (pip install cxxfilt)
import cxxfilt

mangled = '_ZN6CircleC1Eddd'  
print(cxxfilt.demangle(mangled))  
# → Circle::Circle(double, double, double)
```

```python
# Avec subprocess (pas de dépendance externe)
import subprocess

def demangle(symbol):
    result = subprocess.run(
        ['c++filt', symbol],
        capture_output=True, text=True
    )
    return result.stdout.strip()

print(demangle('_ZNK5Shape8describeEv'))
# → Shape::describe() const
```

## Lire le mangling à la main : méthode pratique

Même avec les outils, il est utile de savoir décoder les cas simples à la main — par exemple quand vous êtes dans GDB sans `c++filt` sous la main, ou quand vous analysez un symbole tronqué. Voici une procédure en 5 étapes :

**Étape 1 — Vérifier le préfixe.** Le symbole commence-t-il par `_Z` ? Si non, ce n'est pas un symbole manglé Itanium. S'il commence par `_ZTV`, c'est une vtable. `_ZTI`, c'est une typeinfo (RTTI). `_ZTS`, un type name string.

**Étape 2 — Identifier le nom qualifié.** Après `_Z`, cherchez `N` (début de nom qualifié). Lisez les paires longueur+nom successives jusqu'au `E`. Si `K` apparaît juste après `N`, c'est une méthode `const`. S'il n'y a pas de `N`, c'est une fonction libre : lisez directement longueur+nom.

**Étape 3 — Identifier le constructeur/destructeur.** Si le dernier composant du nom est `C1`, `C2`, `D0`, `D1`, `D2`, c'est un constructeur ou destructeur.

**Étape 4 — Décoder les types des paramètres.** Après le `E` (ou après le nom pour une fonction libre), chaque caractère ou groupe de caractères encode un type de paramètre. Utilisez la table des codes de types de base. Attention aux préfixes `P`, `R`, `K` qui se combinent.

**Étape 5 — Gérer les substitutions.** `S_`, `S0_`, `S1_`… référencent des types déjà mentionnés. `St` = `std::`, `Ss` = `std::string`, etc.

**Exemple de décodage complet :**

Symbole : `_ZN7MyClass7processERKSsi`

```
_Z              → symbole manglé
  N             → début nom qualifié
    7MyClass    → "MyClass" (7 chars)
    7process    → "process" (7 chars)
  E             → fin nom qualifié
    R           → référence vers...
     K          → const...
      Ss        → std::string
    i           → int

Résultat : MyClass::process(const std::string&, int)
```

## Symboles spéciaux à reconnaître

Certains préfixes manglés identifient des structures internes du C++ que le reverse engineer rencontrera fréquemment :

| Préfixe | Signification | Où le trouver |  
|---------|---------------|---------------|  
| `_ZTV` | **vtable** de la classe | `.rodata` — table de pointeurs de fonctions virtuelles |  
| `_ZTI` | **typeinfo** de la classe | `.rodata` — structure RTTI |  
| `_ZTS` | **typeinfo name** (chaîne) | `.rodata` — nom de la classe en clair |  
| `_ZTT` | **VTT** (virtual table table) | `.rodata` — héritage virtuel |  
| `_ZThn` | **thunk non-virtuel** | `.text` — ajustement de pointeur pour héritage multiple |  
| `_ZTv` | **thunk virtuel** | `.text` — ajustement pour héritage virtuel |  
| `_ZGV` | **guard variable** | `.bss` — protection pour l'initialisation de variables statiques locales |  
| `_ZGVN` | **guard variable (nested)** | `.bss` — idem dans un namespace/classe |

**Exemples concrets issus du binaire `oop_O0` :**

```bash
$ nm oop_O0 | grep '_ZTV'
0000000000403d00 V _ZTV6Circle         # vtable for Circle
0000000000403d60 V _ZTV9Rectangle      # vtable for Rectangle
0000000000403dc0 V _ZTV8Triangle       # vtable for Triangle
0000000000403c40 V _ZTV5Shape          # vtable for Shape
0000000000403e20 V _ZTV6Canvas         # vtable for Canvas
0000000000403ea0 V _ZTV8Drawable       # vtable for Drawable
0000000000403ec0 V _ZTV12Serializable  # vtable for Serializable

$ nm oop_O0 | grep '_ZTI'
0000000000403f20 V _ZTI6Circle         # typeinfo for Circle
0000000000403f38 V _ZTI9Rectangle      # typeinfo for Rectangle
0000000000403f50 V _ZTI8Triangle       # typeinfo for Triangle
0000000000403f10 V _ZTI5Shape          # typeinfo for Shape
0000000000403f68 V _ZTI6Canvas         # typeinfo for Canvas

$ nm oop_O0 | grep '_ZTS'
0000000000403f00 V _ZTS6Circle         # typeinfo name → "6Circle"
0000000000403ef8 V _ZTS5Shape          # typeinfo name → "5Shape"
```

> 💡 **Astuce RE :** les chaînes `_ZTS` contiennent le nom de la classe en clair (sous forme manglée mais très lisible). Même sur un binaire strippé, si la RTTI n'a pas été désactivée (`-fno-rtti`), ces chaînes sont présentes dans `.rodata` et permettent de retrouver les noms de toutes les classes polymorphes. C'est souvent le premier réflexe face à un binaire C++ inconnu :  
>  
> ```bash  
> $ strings oop_O2_strip | grep -E '^[0-9]+[A-Z]'  
> 6Circle  
> 9Rectangle  
> 8Triangle  
> 5Shape  
> 6Canvas  
> 8Drawable  
> 12Serializable  
> 12AppException  
> 10ParseError  
> 12NetworkError  
> ```  
>  
> Ces chaînes suivent le format `<longueur><nom>` du mangling Itanium — elles sont directement les noms des classes.

## Cas particuliers à connaître

### Fonctions `extern "C"`

Une fonction déclarée `extern "C"` en C++ **n'est pas manglée**. Son symbole dans le binaire est identique à celui qu'aurait un compilateur C. C'est le mécanisme standard pour l'interopérabilité C/C++ :

```cpp
extern "C" void my_callback(int x);  // symbole : my_callback (pas de _Z)
```

En RE, si vous voyez un mix de symboles `_Z...` et de symboles sans préfixe dans un binaire C++, les seconds sont probablement des fonctions exportées pour une API C ou des callbacks.

### Variables globales et statiques de classe

Les variables globales et les membres statiques de classe sont aussi manglés :

```cpp
int MyClass::instanceCount;  // → _ZN7MyClass13instanceCountE
```

Le `E` final marque la fin du nom qualifié, et il n'y a pas de types de paramètres (ce n'est pas une fonction).

### Fonctions dans des namespaces anonymes

GCC utilise un namespace spécial interne pour les namespaces anonymes, typiquement encodé comme `_ZN12_GLOBAL__N_1...`. Si vous voyez `_GLOBAL__N_1` dans un symbole démanglé, c'est une entité à liaison interne (l'équivalent de `static` au niveau fichier).

### Symboles manglés tronqués

Dans un binaire strippé, vous ne verrez pas les symboles locaux. Mais les symboles référencés dynamiquement (exports, PLT) restent présents. Un symbole peut aussi être tronqué dans une sortie `strings` — commencez toujours le décodage par la gauche et arrêtez-vous quand la chaîne est coupée.

## Impact du stripping sur les symboles C++

Le comportement du stripping (`strip` ou `-s`) sur les symboles C++ dépend du type de symbole :

| Type de symbole | Après `strip` | Pourquoi |  
|-----------------|---------------|----------|  
| Fonctions locales (méthodes privées, etc.) | **Supprimé** | Pas nécessaire au runtime |  
| Fonctions exportées dynamiquement | **Conservé** | Nécessaire pour le linker dynamique |  
| Symboles PLT (`func@plt`) | **Conservé** | Résolution dynamique |  
| vtables (`_ZTV...`) | **Conservé** | Nécessaire au runtime (résolution virtuelle) |  
| typeinfo (`_ZTI...`, `_ZTS...`) | **Conservé*** | Nécessaire pour RTTI et exceptions |  
| Guard variables (`_ZGV...`) | **Conservé** | Nécessaire au runtime |

*\*Les typeinfo sont conservées sauf si le binaire a été compilé avec `-fno-rtti` ET que les exceptions ne les utilisent pas.*

Conséquence pratique : **même un binaire C++ strippé conserve souvent les vtables et les typeinfo**, ce qui donne au reverse engineer les noms de toutes les classes polymorphes et la structure de leurs tables de fonctions virtuelles. C'est une différence majeure avec le C, où le stripping supprime effectivement toute l'information nominative.

```bash
# Comparer le nombre de symboles avant/après strip
$ nm oop_O0 | wc -l
1847
$ nm oop_O0_strip 2>/dev/null | wc -l
0

# Mais les symboles dynamiques survivent
$ nm -D oop_O0_strip | c++filt | grep -c '.'
287
```

---


⏭️ [Modèle objet C++ : vtable, vptr, héritage simple et multiple](/17-re-cpp-gcc/02-modele-objet-vtable.md)
