🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.6 — Templates : instanciations et explosion de symboles

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Le problème des templates en RE

Les templates C++ sont un mécanisme de compilation — ils n'existent pas au runtime. Le compilateur génère une copie complète du code pour chaque combinaison de paramètres de template utilisée dans le programme. Un simple `std::vector` instancié avec trois types différents (`int`, `double`, `std::string`) produit trois jeux complets de fonctions dans le binaire : trois `push_back`, trois `operator[]`, trois destructeurs, trois réallocations, etc.

Pour le reverse engineer, les conséquences sont multiples. La table de symboles explose en taille, souvent dominée par des instanciations STL. Le binaire contient du code quasi-identique dupliqué pour chaque instanciation, rendant la navigation confuse. Les symboles manglés deviennent très longs et difficiles à lire, même démanglés. Et sur un binaire strippé, on perd complètement l'information sur les paramètres de template — il faut la reconstruire en analysant les accès mémoire et les tailles d'éléments.

## Comment GCC instancie les templates

### Le mécanisme d'instanciation implicite

Quand le compilateur rencontre une utilisation de template avec des arguments concrets, il génère (« instancie ») le code spécialisé pour ces arguments. Ce processus s'appelle l'**instanciation implicite** :

```cpp
Registry<std::string, std::shared_ptr<Shape>> shapeRegistry("shapes");  
Registry<int, std::string> idRegistry("ids");  
```

Ces deux lignes déclenchent la génération de deux familles complètes de fonctions :

```
Registry<std::string, std::shared_ptr<Shape>>::Registry(std::string const&)  
Registry<std::string, std::shared_ptr<Shape>>::add(std::string const&, std::shared_ptr<Shape> const&)  
Registry<std::string, std::shared_ptr<Shape>>::get(std::string const&) const  
Registry<std::string, std::shared_ptr<Shape>>::contains(std::string const&) const  
Registry<std::string, std::shared_ptr<Shape>>::size() const  
Registry<std::string, std::shared_ptr<Shape>>::forEach(std::function<...>) const  

Registry<int, std::string>::Registry(std::string const&)  
Registry<int, std::string>::add(int const&, std::string const&)  
Registry<int, std::string>::get(int const&) const  
Registry<int, std::string>::contains(int const&) const  
Registry<int, std::string>::size() const  
Registry<int, std::string>::forEach(std::function<...>) const  
```

Chaque instanciation produit un ensemble complet de fonctions, avec du code machine distinct car les types ont des tailles, des alignements et des opérations différentes.

### Instanciation dans chaque unité de traduction

En C++, les templates sont définis dans les headers. Chaque fichier `.cpp` qui inclut un header et utilise un template génère ses propres instanciations. Si trois fichiers source utilisent `std::vector<int>`, le compilateur produit trois copies de toutes les fonctions de `std::vector<int>`, une dans chaque fichier objet `.o`.

Le linker élimine les doublons grâce au mécanisme de **COMDAT** : les instanciations de template sont émises en tant que symboles *weak* dans des sections COMDAT (`.text._ZN...`, une section par fonction), et le linker ne conserve qu'une seule copie de chaque symbole identique. C'est pourquoi les instanciations de template apparaissent comme symboles de type `W` (weak) dans la sortie de `nm` :

```bash
$ nm oop_O0 | c++filt | grep 'Registry.*add'
0000000000402a10 W Registry<std::__cxx11::basic_string<char, ...>, std::shared_ptr<Shape>>::add(...)
0000000000402e80 W Registry<int, std::__cxx11::basic_string<char, ...>>::add(...)
```

Le `W` indique un symbole weak — le linker a conservé cette copie, mais d'autres fichiers objet auraient pu fournir la même.

> 💡 **En RE :** les symboles `W` (weak) dans `nm` sont presque toujours des instanciations de templates ou des fonctions inline. Leur volume domine souvent la table de symboles d'un binaire C++. Filtrez-les pour vous concentrer sur le code applicatif : `nm -C binaire | grep ' T '` n'affiche que les symboles globaux forts (fonctions non-template définies explicitement).

## L'explosion de symboles en pratique

### Mesurer l'ampleur

Sur notre binaire d'entraînement, comparons le nombre de symboles :

```bash
# Total des symboles définis
$ nm oop_O0 | wc -l
1847

# Symboles faibles (principalement des instanciations de templates)
$ nm oop_O0 | grep ' W ' | wc -l
1203

# Symboles forts (code applicatif + libstdc++ embarqué)
$ nm oop_O0 | grep ' T ' | wc -l
312
```

Dans cet exemple, **65% des symboles** sont des instanciations weak. Et ce ratio est modeste — un projet réel utilisant la STL intensivement, Boost, ou des bibliothèques template-heavy peut atteindre 90% de symboles weak.

### Sources principales d'explosion

Les instanciations les plus volumineuses proviennent presque toujours de la STL :

```bash
$ nm -C oop_O0 | grep ' W ' | sed 's/.*W //' | cut -d'(' -f1 | sort | uniq -c | sort -rn | head -15
     47  std::__cxx11::basic_string<char, ...>::
     38  std::vector<std::shared_ptr<Shape>, ...>::
     31  std::_Rb_tree<std::__cxx11::basic_string<char, ...>, ...>::
     24  std::shared_ptr<Shape>::
     19  std::_Hashtable<std::__cxx11::basic_string<char, ...>, ...>::
     16  Registry<std::__cxx11::basic_string<char, ...>, std::shared_ptr<Shape>>::
     14  Registry<int, std::__cxx11::basic_string<char, ...>>::
     12  std::_Sp_counted_ptr_inplace<Circle, ...>::
     ...
```

`std::string` seul génère des dizaines de fonctions instanciées (constructeurs pour différents cas, opérateurs, `append`, `assign`, `compare`, itérateurs...). Multiplié par les conteneurs qui l'utilisent, le volume devient considérable.

### Instanciations récursives

Certaines instanciations de templates en déclenchent d'autres en cascade. Par exemple, `std::map<std::string, std::shared_ptr<Shape>>` instancie :
- `std::_Rb_tree<std::string, std::pair<const std::string, std::shared_ptr<Shape>>, ...>`  
- qui instancie `std::_Rb_tree_node<std::pair<const std::string, std::shared_ptr<Shape>>>`  
- qui instancie des itérateurs, des allocateurs, etc.  
- `std::pair<const std::string, std::shared_ptr<Shape>>` instancie ses constructeurs, opérateurs de comparaison, etc.

Chaque niveau de la hiérarchie de templates génère son propre ensemble de fonctions. Le résultat est une arborescence d'instanciations dont le volume total peut surprendre.

## Reconnaître les instanciations dans le désassemblage

### Avec les symboles : lire le mangling

Les paramètres de template sont encodés entre `I` et `E` dans le symbole manglé (voir section 17.1). Un symbole comme :

```
_ZN8RegistryINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt10shared_ptrI5ShapeEE3addERKS5_RKSA_
```

se démantle en :

```
Registry<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>,
         std::shared_ptr<Shape>>::add(
             std::__cxx11::basic_string<char, ...> const&,
             std::shared_ptr<Shape> const&)
```

Les paramètres de template (`std::string` et `std::shared_ptr<Shape>`) sont entièrement encodés dans le nom, ce qui donne accès aux types exacts même sans le code source.

En pratique, ces symboles sont tellement longs que les outils les tronquent souvent. Utilisez `c++filt` en mode pipe pour un rendu complet, ou `nm --no-demangle` pour voir la forme brute et la décoder manuellement si nécessaire.

### Sans les symboles : identifier par le code

Sur un binaire strippé, les paramètres de template ne sont plus visibles. Il faut les reconstituer en analysant le code généré. Voici les indices principaux.

**La taille des éléments.** Comme vu en section 17.5, le facteur d'échelle dans les calculs de `size()` d'un `std::vector` révèle le `sizeof(T)` :

```nasm
; vector::size() pour vector<shared_ptr<Shape>>
mov    rax, [rdi+8]       ; _M_finish  
sub    rax, [rdi]         ; - _M_start  
sar    rax, 4             ; / 16 → sizeof(shared_ptr) = 16  
```

Le décalage `sar rax, 4` (division par 16) indique que les éléments font 16 octets, ce qui correspond à `std::shared_ptr` (deux pointeurs de 8 octets).

**Les opérations sur les éléments.** Le code des fonctions template utilise les opérations spécifiques au type instancié. Par exemple, `Registry<int, std::string>::add` comparera les clés avec une simple instruction `cmp`, tandis que `Registry<std::string, ...>::add` appellera `std::string::compare` ou `operator<` pour les chaînes. Le type des opérations de comparaison, copie, et destruction révèle le type de template.

**Les destructeurs dans les cleanup landing pads.** Quand un conteneur est détruit, il appelle le destructeur de chaque élément. Si les éléments sont des `std::string`, vous verrez le pattern SSO caractéristique (`lea rdx, [rdi+16]; cmp [rdi], rdx`). Si ce sont des `shared_ptr`, vous verrez le décrément atomique du compteur de références.

**Les fonctions de hachage et de comparaison.** Pour `std::unordered_map`, la fonction de hash instanciée est spécifique au type de clé. `std::hash<int>` est trivial (souvent l'identité), tandis que `std::hash<std::string>` appelle une fonction de hachage sur les caractères (MurmurHash, FNV, ou similaire selon la version de libstdc++).

### Reconnaître deux instanciations du même template

Quand le même template est instancié avec des types différents, le code généré a la même **structure** (mêmes branchements, même séquence logique) mais des **détails** différents (tailles d'accès, fonctions appelées, registres utilisés). En RE, deux fonctions qui se ressemblent structurellement mais diffèrent dans les tailles d'accès mémoire et les fonctions appelées sont probablement deux instanciations du même template.

Par exemple, `Registry<string, shared_ptr<Shape>>::add` et `Registry<int, string>::add` auront :
- La même logique : vérifier si la clé existe (`entries_.count(key)`), lancer une exception si oui, insérer sinon.  
- Des appels différents : l'un appellera `std::map<string, ...>::count`, l'autre `std::map<int, ...>::count`.  
- Des tailles de copie différentes : les clés `string` (32 octets) vs `int` (4 octets).

Ce pattern de « même structure, détails différents » est la signature des instanciations de templates dans un binaire strippé.

## Les templates et les optimisations

### Inlining des fonctions template

GCC est particulièrement agressif pour inliner les fonctions template, surtout en `-O2` et `-O3`. Les raisons sont multiples :
- Les fonctions template sont définies dans les headers, donc visibles dans chaque unité de traduction.  
- Beaucoup de fonctions template sont petites (accesseurs, wrappers, adaptateurs).  
- L'inlining élimine le coût d'un appel de fonction tout en permettant des optimisations supplémentaires (propagation de constantes, élimination de code mort).

En `-O2`, il est courant qu'une instanciation de template n'apparaisse **pas du tout** comme fonction distincte dans le binaire — son code a été inliné dans tous les sites d'appel. C'est un problème en RE car on perd la correspondance entre les symboles et les fonctions réelles dans `.text`.

```bash
# En -O0, chaque instanciation est une fonction distincte
$ nm -C oop_O0 | grep 'Registry.*contains' | wc -l
2

# En -O2, certaines disparaissent (inlinées)
$ nm -C oop_O2 | grep 'Registry.*contains' | wc -l
0
```

> 💡 **En RE :** si vous ne trouvez pas une méthode de template attendue dans la table de symboles d'un binaire `-O2`, elle a probablement été inlinée. Cherchez son code (la logique qu'elle devrait contenir) directement dans les fonctions appelantes.

### Fusion d'instanciations identiques (ICF)

Quand deux instanciations de template produisent un **code machine identique**, le linker peut les fusionner en une seule copie. C'est l'**Identical Code Folding** (ICF), activé par `--icf=all` dans `ld.gold` et `lld`, ou partiellement par `ld.bfd` avec les sections COMDAT.

Par exemple, `std::vector<int*>::push_back` et `std::vector<double*>::push_back` produisent exactement le même code machine car les deux types sont des pointeurs de 8 octets avec la même sémantique de copie. Avec ICF, une seule copie subsiste dans le binaire, et les deux symboles pointent vers la même adresse.

```bash
# Deux symboles, même adresse → ICF
$ nm oop_O2 | c++filt | grep 'vector.*push_back'
0000000000403a20 W std::vector<int*, ...>::push_back(int* const&)
0000000000403a20 W std::vector<double*, ...>::push_back(double* const&)
```

> ⚠️ **Piège RE :** avec ICF, deux fonctions portant des noms différents et des types de paramètres différents partagent la même adresse. Si vous posez un breakpoint dans GDB sur l'une, vous interceptez aussi les appels à l'autre. Si vous renommez la fonction dans Ghidra, le nouveau nom s'applique à toutes les instanciations fusionnées. Soyez conscient qu'une seule fonction peut servir plusieurs types.

### Spécialisation et son impact

Le C++ permet de **spécialiser** un template pour un type particulier, en fournissant une implémentation différente :

```cpp
template<>  
class Registry<int, int> {  
    // Implémentation complètement différente
};
```

En RE, une spécialisation se manifeste par une instanciation dont le code est structurellement différent des autres instanciations du même template. Si `Registry<string, Shape*>::add` et `Registry<int, string>::add` se ressemblent structurellement, mais que `Registry<int, int>::add` a une logique complètement différente, cette dernière est probablement une spécialisation.

La bibliothèque standard utilise intensivement les spécialisations internes. Par exemple, `std::hash` est spécialisé pour chaque type de base (`int`, `long`, `double`, `string`, `pointer`...), et chaque spécialisation a un code de hachage différent.

## La STL : la principale source de templates en RE

En pratique, la grande majorité des instanciations de templates rencontrées en RE proviennent de la STL, pas du code applicatif. Voici les familles les plus volumineuses.

### `std::basic_string` et ses dépendances

`std::string` est techniquement `std::basic_string<char, std::char_traits<char>, std::allocator<char>>`. Chaque opération (`append`, `assign`, `compare`, `find`, `substr`, itérateurs, constructeurs de copie et de déplacement, etc.) est une instanciation distincte. Un binaire typique contient facilement 30 à 50 fonctions instanciées liées à `std::string`.

Si le programme utilise `std::wstring` (caractères larges), c'est un deuxième jeu complet d'instanciations avec `wchar_t`.

### `std::vector` et ses allocateurs

Chaque `std::vector<T>` pour un type `T` distinct génère un jeu complet de fonctions. Les plus coûteuses en volume de code sont :
- `_M_realloc_insert` (réallocation lors d'un push_back)  
- `_M_fill_assign` et `_M_range_insert` (insertion de plages)  
- Les constructeurs de copie et de déplacement

### `std::map` / `std::set` (arbre rouge-noir)

L'implémentation `_Rb_tree` est commune à `std::map`, `std::set`, `std::multimap` et `std::multiset`. Mais les instanciations sont distinctes pour chaque combinaison de types clé/valeur car les comparaisons et les constructeurs d'éléments diffèrent.

### `std::shared_ptr` et le control block

`std::make_shared<T>(args...)` instancie `_Sp_counted_ptr_inplace<T, allocator<T>, ...>`, un control block qui contient l'objet `T` directement (pour éviter une allocation séparée). Chaque type `T` utilisé avec `make_shared` produit un control block distinct avec sa propre vtable, son propre destructeur, et sa propre logique de déallocation.

### `std::function`

`std::function<R(Args...)>` est un wrapper polymorphe qui peut encapsuler n'importe quel callable (fonction, lambda, foncteur). En interne, il utilise du type erasure, ce qui génère des classes de management template distinctes pour chaque type de callable stocké. Si une `std::function<void(int)>` est initialisée avec trois lambdas différentes dans le programme, chaque lambda produit ses propres instanciations de management.

## Stratégies RE face à l'explosion de templates

### Trier et filtrer

La première stratégie est de **trier les symboles par pertinence** :

```bash
# Symboles applicatifs (non STL, non template standard)
$ nm -C oop_O0 | grep ' [TW] ' | grep -v 'std::' | grep -v '__gnu_cxx' | grep -v '__cxa'

# Instanciations de templates applicatifs uniquement
$ nm -C oop_O0 | grep ' W ' | grep -v 'std::' | grep -v 'basic_string'
```

L'idée est de séparer le « bruit STL » du code template applicatif que vous cherchez réellement à comprendre.

### Identifier les patterns plutôt que les instances

Plutôt que d'analyser chaque instanciation individuellement, identifiez le **pattern commun**. Analysez une seule instanciation en détail (de préférence celle avec le type le plus simple, comme `int`), comprenez la logique, puis appliquez cette compréhension aux autres instanciations en adaptant les tailles et les types.

### Utiliser les signatures Ghidra

Ghidra dispose d'un mécanisme de **Function ID** (FID) et de signatures qui peut automatiquement identifier les fonctions de `libstdc++`. Quand Ghidra reconnaît `std::vector<int>::push_back`, il la nomme automatiquement, ce qui élimine une grande partie du bruit. Vérifiez que les signatures de `libstdc++` sont chargées :

1. Dans Ghidra, menu **Analysis** → **Auto Analyze** → vérifiez que « Function ID » est activé.  
2. Si les fonctions STL ne sont pas reconnues, importez des fichiers de signatures `.fidb` pour votre version de `libstdc++`.

### Ignorer les détails internes de la STL

En RE, vous n'avez généralement pas besoin de comprendre le fonctionnement interne de `std::vector::_M_realloc_insert`. Ce qui importe est de reconnaître que la fonction appelante effectue un `push_back` sur un vector, pas les détails de la réallocation. Identifiez le conteneur, identifiez l'opération, et passez à la logique applicative.

L'exception est quand une vulnérabilité ou un comportement spécifique se trouve dans l'utilisation du conteneur (out-of-bounds, use-after-free, iterator invalidation). Dans ce cas, la connaissance des layouts internes (section 17.5) devient essentielle.

## Instanciation explicite et `extern template`

Certains projets utilisent l'**instanciation explicite** pour contrôler où et comment les templates sont instanciés :

```cpp
// Dans un .cpp : force l'instanciation ici
template class Registry<int, std::string>;

// Dans un header : empêche l'instanciation automatique ailleurs
extern template class Registry<int, std::string>;
```

En RE, les instanciations explicites apparaissent comme des symboles forts (`T` dans `nm`) au lieu de symboles weak (`W`). Cela indique que le développeur a intentionnellement contrôlé l'instanciation, probablement pour réduire le temps de compilation ou la taille du binaire.

```bash
# Symboles forts = instanciations explicites
$ nm -C oop_O0 | grep ' T .*Registry'
0000000000402a10 T Registry<int, std::string>::add(...)

# Symboles faibles = instanciations implicites
$ nm -C oop_O0 | grep ' W .*Registry'
0000000000402e80 W Registry<std::string, std::shared_ptr<Shape>>::add(...)
```

## Templates variadiques et expression fold

Les templates variadiques (C++11) et les expressions fold (C++17) méritent une mention car ils produisent des patterns de code caractéristiques.

Un template variadique comme :

```cpp
template<typename... Args>  
void log(const char* fmt, Args... args) {  
    printf(fmt, args...);
}
```

instancié avec `log("x=%d y=%f", 42, 3.14)` produit une seule instanciation concrète `log<int, double>(const char*, int, double)`. Le code généré est identique à un appel direct à `printf` — le mécanisme variadique est entièrement résolu à la compilation.

En RE, les templates variadiques se reconnaissent par la présence de nombreuses instanciations du même template avec des listes de types de longueurs différentes :

```
log<int>(const char*, int)  
log<int, double>(const char*, int, double)  
log<int, double, std::string const&>(const char*, int, double, std::string const&)  
```

Chaque combinaison d'arguments utilisée dans le programme génère sa propre instanciation.

## `if constexpr` et spécialisation à la compilation

Le C++17 `if constexpr` génère un code où seule la branche valide est compilée :

```cpp
template<typename T>  
void process(T val) {  
    if constexpr (std::is_integral_v<T>) {
        // code pour les entiers — seul ce code existe pour T=int
    } else {
        // code pour les autres types — seul ce code existe pour T=double
    }
}
```

En RE, deux instanciations du même template peuvent avoir un code **très différent** si elles utilisent `if constexpr`. Contrairement aux instanciations normales où la structure est identique, `if constexpr` produit des fonctions dont le corps varie fondamentalement selon le type. Ne confondez pas cela avec une spécialisation explicite — le mécanisme est différent mais le résultat en RE est similaire.

## Résumé des patterns à reconnaître

| Pattern | Signification |  
|---------|---------------|  
| Symboles `W` (weak) dans `nm` | Instanciations implicites de templates |  
| Symboles `T` (global) avec types template | Instanciations explicites |  
| Deux fonctions à la même adresse avec des noms différents | ICF (Identical Code Folding) — instanciations avec types de même taille |  
| Code structurellement identique avec des tailles d'accès différentes | Deux instanciations du même template avec des types différents |  
| `I...E` dans un symbole manglé | Paramètres de template (section 17.1) |  
| `__cxx11` dans les symboles | Nouvelle ABI pour `std::string` et conteneurs associés (GCC ≥ 5) |  
| Fonctions attendues absentes en `-O2` | Template inliné par le compilateur |  
| 60–90% des symboles liés à `std::` | Normal — la STL domine le volume de templates |  
| Même template, code radicalement différent selon l'instanciation | `if constexpr` ou spécialisation de template |

---


⏭️ [Lambda, closures et captures en assembleur](/17-re-cpp-gcc/07-lambda-closures.md)
