🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.7 — Lambda, closures et captures en assembleur

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Ce que le compilateur fait d'une lambda

Une lambda C++ n'est pas une fonction au sens classique. Le standard définit une lambda comme un objet d'un **type closure unique et anonyme**, généré par le compilateur. Ce type closure est une classe (au sens `struct`) qui :

1. Possède un **`operator()` const** (ou non-const si la lambda est `mutable`) dont le corps est le code de la lambda.  
2. Contient comme **membres** toutes les variables capturées (par valeur ou par référence).  
3. Dispose de constructeurs et destructeurs implicites pour initialiser et nettoyer les captures.

Autrement dit, GCC transforme chaque expression lambda en une classe anonyme. Le code de la lambda devient une méthode de cette classe. Les variables capturées deviennent des champs. L'objet closure est instancié (souvent sur la pile) au point où la lambda est définie.

Pour le reverse engineer, cela signifie que chaque lambda se manifeste dans le binaire comme :

- Une **fonction** dans `.text` (l'`operator()`) avec un nom manglé contenant un identifiant anonyme.  
- Un **objet** sur la pile ou dans un registre, dont la taille dépend des captures.  
- Du code d'initialisation au point de définition, qui copie les valeurs capturées (ou les pointeurs/références) dans l'objet closure.

## Anatomie d'une lambda sans capture

La lambda la plus simple est celle sans capture :

```cpp
auto printSeparator = []() {
    std::cout << "------------------------" << std::endl;
};
printSeparator();
```

### Ce que GCC génère

GCC crée une classe anonyme vide (aucun membre) avec un `operator()` :

```cpp
// Équivalent conceptuel généré par GCC :
struct __lambda_printSeparator {
    void operator()() const {
        std::cout << "------------------------" << std::endl;
    }
};
```

Comme la classe n'a aucun membre, son `sizeof` est 1 (taille minimale d'un objet C++). L'objet closure n'occupe quasiment aucun espace et le compilateur l'optimise souvent entièrement.

### Le symbole manglé

Le nom de l'`operator()` d'une lambda sans capture dans GCC suit un schéma comme :

```
_ZZ20demonstrateLambdasRKSt6vectorISt10shared_ptrI5ShapeESaIS2_EEENKUlvE_clEv
```

Démanglement :

```
demonstrateLambdas(std::vector<std::shared_ptr<Shape>> const&)::{lambda()#1}::operator()() const
```

La structure du nom est :

- `_ZZ` : symbole local à une fonction (la fonction englobante).  
- `demonstrateLambdas(...)` : la fonction englobante.  
- `{lambda()#1}` : la première lambda sans argument définie dans cette fonction.  
- `::operator()() const` : l'opérateur d'appel.

Le `#1`, `#2`, `#3` identifie les lambdas dans l'ordre de leur apparition dans la fonction. Ce numéro est le seul moyen de distinguer les lambdas dans les symboles.

> 💡 **En RE :** quand vous voyez `{lambda` dans un symbole démanglé, c'est le corps d'une lambda. Le nom de la fonction englobante et le numéro de lambda vous situent dans le code source. Sur un binaire strippé, ces symboles disparaissent, mais la structure du code (petite fonction appelée indirectement ou inlinée) reste reconnaissable.

### Conversion en pointeur de fonction

Une lambda sans capture peut être convertie implicitement en pointeur de fonction. GCC génère pour cela un opérateur de conversion et une fonction statique thunk :

```cpp
void (*fp)() = printSeparator;  // conversion implicite  
fp();                            // appel via pointeur de fonction  
```

GCC génère :

```
{lambda()#1}::operator void (*)()() const    — opérateur de conversion
{lambda()#1}::_FUN()                         — fonction statique (le thunk)
```

La fonction `_FUN` est une fonction statique ordinaire (sans `this`) qui contient le même code que l'`operator()`. C'est elle que le pointeur de fonction désigne.

> 💡 **En RE :** si vous voyez un symbole contenant `_FUN` associé à une lambda, c'est le thunk de conversion en pointeur de fonction. Sa présence indique que la lambda a été utilisée comme pointeur de fonction quelque part dans le code.

## Capture par valeur

### La mécanique

Quand une lambda capture une variable par valeur, la variable est **copiée** dans l'objet closure au moment de la définition :

```cpp
double minArea = 10.0;  
auto isLargeShape = [minArea](const std::shared_ptr<Shape>& s) {  
    return s->area() > minArea;
};
```

### La classe closure générée

```cpp
// Équivalent conceptuel :
struct __lambda_isLargeShape {
    double minArea;    // capture par valeur → membre copié

    bool operator()(const std::shared_ptr<Shape>& s) const {
        return s->area() > this->minArea;
    }
};
```

La classe contient un champ `double minArea` (8 octets). Le `sizeof` de la closure est 8.

### En assembleur

**Initialisation de la closure (au point de définition) :**

```nasm
; minArea est dans xmm0 ou sur la pile
; La closure est construite sur la pile
movsd  QWORD PTR [rbp-0x30], xmm0    ; copier minArea dans l'objet closure
; [rbp-0x30] est maintenant l'objet closure (8 octets)
```

**Appel de l'`operator()` :**

```nasm
; rdi = pointeur vers la closure (this)
; rsi = pointeur vers le shared_ptr<Shape> (premier argument)
isLargeShape::operator()() const:
    push   rbp
    mov    rbp, rsp
    mov    QWORD PTR [rbp-8], rdi     ; sauver this (closure)
    mov    QWORD PTR [rbp-16], rsi    ; sauver l'argument

    ; Appel virtuel : s->area()
    mov    rax, QWORD PTR [rsi]       ; charger le pointeur Shape* depuis shared_ptr
    mov    rcx, QWORD PTR [rax]       ; charger le vptr
    mov    rdi, rax                   ; this = Shape*
    call   QWORD PTR [rcx+0x10]      ; vtable[2] = area()

    ; Comparer avec la capture minArea
    mov    rdi, QWORD PTR [rbp-8]     ; recharger this (closure)
    ucomisd xmm0, QWORD PTR [rdi]    ; comparer area() avec this->minArea (offset 0)
    seta   al                          ; al = (area > minArea)
    ret
```

Le point clé est l'accès `QWORD PTR [rdi]` dans l'`operator()` : c'est la lecture du champ `minArea` capturé, à l'offset 0 de la closure. Le `rdi` initial est le pointeur `this` vers l'objet closure, exactement comme pour n'importe quelle méthode de classe.

> 💡 **Pattern RE :** dans l'`operator()` d'une lambda, les accès à `[rdi + offset]` sont des lectures des variables capturées. L'offset identifie quelle capture est lue. Si l'`operator()` n'accède jamais à `[rdi]`, la lambda n'a probablement pas de captures (ou elles ont été optimisées).

## Capture par référence

### La mécanique

Quand une lambda capture par référence, la closure stocke un **pointeur** vers la variable originale, pas une copie :

```cpp
double totalArea = 0.0;  
int count = 0;  
auto accumulate = [&totalArea, &count](const std::shared_ptr<Shape>& s) {  
    totalArea += s->area();
    count++;
};
```

### La classe closure générée

```cpp
// Équivalent conceptuel :
struct __lambda_accumulate {
    double* totalArea;   // capture par référence → pointeur
    int*    count;       // capture par référence → pointeur

    void operator()(const std::shared_ptr<Shape>& s) const {
        *this->totalArea += s->area();
        (*this->count)++;
    }
};
```

Chaque capture par référence ajoute un pointeur (8 octets) à la closure. Le `sizeof` est ici 16 (deux pointeurs).

### En assembleur

**Initialisation de la closure :**

```nasm
; totalArea est à [rbp-0x40], count à [rbp-0x44]
lea    rax, [rbp-0x40]               ; adresse de totalArea  
mov    QWORD PTR [rbp-0x60], rax     ; closure.totalArea = &totalArea  
lea    rax, [rbp-0x44]               ; adresse de count  
mov    QWORD PTR [rbp-0x58], rax     ; closure.count = &count  
; La closure à [rbp-0x60] contient deux pointeurs
```

**Dans l'`operator()` :**

```nasm
accumulate::operator()() const:
    ; rdi = this (closure), rsi = argument (shared_ptr)
    ; ... appel à s->area(), résultat dans xmm0 ...

    ; *totalArea += area
    mov    rax, QWORD PTR [rdi]       ; rax = this->totalArea (le pointeur, offset 0)
    addsd  xmm0, QWORD PTR [rax]     ; xmm0 = *totalArea + area
    movsd  QWORD PTR [rax], xmm0     ; *totalArea = résultat

    ; (*count)++
    mov    rax, QWORD PTR [rdi+8]    ; rax = this->count (le pointeur, offset 8)
    add    DWORD PTR [rax], 1         ; (*count)++
    ret
```

La différence avec la capture par valeur est visible : le code charge d'abord le **pointeur** depuis la closure (`[rdi]`), puis déréférence ce pointeur pour accéder à la variable (`[rax]`). C'est une double indirection.

> 💡 **Pattern RE :** dans l'`operator()` d'une lambda, une capture par valeur = un accès direct `[rdi+offset]`. Une capture par référence = deux niveaux d'indirection : `mov rax, [rdi+offset]; op [rax]`. Cette distinction permet de reconstruire le mode de capture de chaque variable.

## Capture mixte (valeur + référence)

```cpp
std::string prefix = ">> ";  
std::vector<std::string> descriptions;  
auto describeAndCollect = [prefix, &descriptions](const std::shared_ptr<Shape>& s) {  
    std::string desc = prefix + s->describe();
    descriptions.push_back(desc);
    std::cout << desc << std::endl;
};
```

### La classe closure générée

```cpp
struct __lambda_describeAndCollect {
    std::string                prefix;        // par valeur → copie (32 octets)
    std::vector<std::string>*  descriptions;  // par référence → pointeur (8 octets)

    void operator()(const std::shared_ptr<Shape>& s) const {
        // ...
    }
};
// sizeof = 40 (32 + 8, potentiellement aligné)
```

### Layout mémoire de la closure

```
Objet closure sur la pile :
┌──────────────────────────────────────┐
│  prefix (std::string, 32 octets)     │  offset 0    — capture par valeur
│    _M_p                              │    +0
│    _M_string_length                  │    +8
│    _M_local_buf[16]                  │    +16
├──────────────────────────────────────┤
│  descriptions (std::vector<...>*)    │  offset 32   — capture par référence
└──────────────────────────────────────┘
                                          sizeof = 40
```

> 💡 **En RE :** la taille totale de la closure donne un indice sur le nombre et le type des captures. Une closure de 8 octets = une capture simple (un `double`, un `int`, ou un pointeur). Une closure de 40 octets avec un `std::string` (32 octets) + un pointeur (8 octets) = une `string` par valeur et quelque chose par référence. Croisez avec les accès dans l'`operator()` pour confirmer.

L'initialisation de cette closure implique la **copie** du `std::string` (appel au constructeur de copie) et le stockage du pointeur :

```nasm
; Construire la closure sur la pile
; 1. Copier prefix (std::string par valeur) → appel au copy constructor
lea    rdi, [rbp-0x60]               ; destination = offset 0 de la closure  
lea    rsi, [rbp-0x30]               ; source = variable prefix locale  
call   std::string::basic_string(std::string const&)  

; 2. Stocker le pointeur vers descriptions
lea    rax, [rbp-0x80]               ; adresse de descriptions  
mov    QWORD PTR [rbp-0x28], rax     ; closure.descriptions = &descriptions (offset 32)  
```

La présence d'un appel à un constructeur de copie lors de l'initialisation de la closure confirme une capture par valeur d'un type non trivial. Inversement, un simple `lea` + `mov` (stockage d'adresse) confirme une capture par référence.

## Capture par copie intégrale (`[=]`)

```cpp
auto formatShape = [=](const auto& s) -> std::string {
    return prefix + s->name() + " (area >= " +
           std::to_string(minArea) + "? " +
           (s->area() >= minArea ? "yes" : "no") + ")";
};
```

Le `[=]` capture **toutes les variables utilisées** par valeur. GCC n'inclut dans la closure que les variables effectivement référencées dans le corps de la lambda — pas toutes les variables du scope englobant.

Dans cet exemple, `prefix` (`std::string`, 32 octets) et `minArea` (`double`, 8 octets) sont capturés. La closure fait 40 octets (32 + 8).

> ⚠️ **En RE :** avec `[=]` ou `[&]`, vous ne savez pas à l'avance quelles variables sont capturées. Seule l'analyse du code de l'`operator()` (quels offsets de la closure sont lus) et de l'initialisation (quelles variables sont copiées/référencées) révèle les captures effectives.

## Capture par référence intégrale (`[&]`)

```cpp
auto accumulate = [&](const std::shared_ptr<Shape>& s) {
    totalArea += s->area();
    count++;
};
```

La closure contient un pointeur pour chaque variable capturée. Avec `[&]`, GCC peut optimiser en ne stockant qu'un seul pointeur vers le frame de la pile de la fonction englobante, et accéder aux variables via des offsets depuis ce pointeur. Mais en pratique (surtout en `-O0`), GCC stocke un pointeur séparé par variable.

## Lambda générique (C++14 `auto`)

```cpp
auto formatShape = [=](const auto& s) -> std::string { ... };
```

Le paramètre `auto` fait de la lambda un **template implicite**. GCC génère un `operator()` template, et chaque appel avec un type d'argument différent produit une instanciation distincte :

```
{lambda(auto:1 const&)#5}::operator()<std::shared_ptr<Shape>>(std::shared_ptr<Shape> const&) const
{lambda(auto:1 const&)#5}::operator()<std::shared_ptr<Circle>>(std::shared_ptr<Circle> const&) const
```

En RE, si vous voyez plusieurs `operator()` pour la même lambda (même `#N`) avec des types de paramètres différents, c'est une lambda générique instanciée avec plusieurs types.

> 💡 **En RE :** les lambdas génériques combinent les complications des lambdas (classe anonyme, captures) et des templates (instanciations multiples, explosion de symboles). Dans un binaire strippé, cherchez des fonctions structurellement identiques qui diffèrent uniquement par les tailles d'accès ou les appels aux méthodes des paramètres — c'est le pattern d'une lambda template instanciée avec des types différents.

## Lambda `mutable`

Par défaut, l'`operator()` d'une lambda est `const` — la lambda ne peut pas modifier ses captures par valeur. Le mot-clé `mutable` supprime cette restriction :

```cpp
int counter = 0;  
auto increment = [counter]() mutable {  
    return ++counter;  // modifie la copie locale
};
```

En assembleur, la seule différence est que le `this` passé à l'`operator()` est non-const, ce qui se manifeste dans le mangling :

```
Sans mutable : {lambda()#1}::operator()() const    — K dans le mangling (NKUl...)  
Avec mutable : {lambda()#1}::operator()()          — pas de K (NUl...)  
```

Dans le code, l'`operator()` écrit dans la closure (`mov [rdi+offset], value`) au lieu de seulement la lire. En `-O0`, GCC peut même passer `this` dans un registre non-const. En pratique, la différence est subtile dans le désassemblage, mais le mangling la trahit clairement.

## Lambdas et `std::function`

Quand une lambda est stockée dans un `std::function`, un mécanisme de **type erasure** entre en jeu. La lambda (de type anonyme) doit être encapsulée dans un objet polymorphe que `std::function` peut manipuler uniformément.

```cpp
std::function<void(const std::string&, const std::shared_ptr<Shape>&)> callback;  
callback = [](const std::string& key, const std::shared_ptr<Shape>& s) {  
    std::cout << key << ": " << s->describe() << std::endl;
};
```

GCC génère pour cela un **manager function** et un **invoker function** qui encapsulent la lambda :

```
std::_Function_handler<void(string const&, shared_ptr<Shape> const&),
                       {lambda(...)}>::_M_invoke(...)
std::_Function_handler<void(string const&, shared_ptr<Shape> const&),
                       {lambda(...)}>::_M_manager(...)
```

Le layout de `std::function` est :

```
std::function<R(Args...)> (sizeof = 32 octets) :
┌──────────────────────────────────────┐
│  _M_functor (union, 16 octets)       │  offset 0    — stockage inline ou pointeur heap
├──────────────────────────────────────┤
│  _M_invoker (function pointer)       │  offset 16   — pointeur vers l'invoker
├──────────────────────────────────────┤
│  _M_manager (function pointer)       │  offset 24   — pointeur vers le manager
└──────────────────────────────────────┘
```

Le champ `_M_functor` stocke la closure directement si elle fait 16 octets ou moins (Small Buffer Optimization, analogue au SSO de `std::string`). Sinon, la closure est allouée sur le heap et `_M_functor` contient un pointeur vers elle.

**Appel de `std::function` en assembleur :**

```nasm
; Appeler callback(key, shape)
; rdi = pointeur vers std::function, rsi = key, rdx = shape
mov    rax, QWORD PTR [rdi+16]       ; _M_invoker (pointeur de fonction)  
call   rax                            ; appel indirect → _M_invoke  
```

> 💡 **Pattern RE :** un `call [rdi+16]` où `rdi` pointe vers un objet de 32 octets est très probablement un appel de `std::function`. L'indirection via `_M_invoker` masque la lambda réelle. Pour trouver le code de la lambda, suivez le pointeur `_M_invoker` — il pointe vers la fonction `_M_invoke`, qui elle-même appelle l'`operator()` de la closure.

Le type erasure de `std::function` ajoute une couche d'indirection qui rend le suivi du flux de contrôle plus difficile. En RE, si vous voyez beaucoup de `_Function_handler::_M_invoke`, c'est que le code utilise intensivement `std::function` (ce qui est courant avec les callbacks, les event handlers et les patterns Observer).

## Lambdas dans les algorithmes STL

Les lambdas sont très souvent passées aux algorithmes STL. En `-O2`, GCC inlinera typiquement à la fois l'algorithme et la lambda, ce qui fait disparaître les deux :

```cpp
auto largeCount = std::count_if(shapes.begin(), shapes.end(), isLargeShape);
```

**En `-O0` :** GCC génère un appel explicite à `std::count_if`, qui reçoit un pointeur vers la closure. L'`operator()` est appelé une fois par élément.

**En `-O2` :** GCC inline `std::count_if` et inline l'`operator()` de la lambda. Le résultat est une simple boucle qui compare chaque `area()` avec `minArea` directement, sans aucune trace visible de la lambda ou de l'algorithme :

```nasm
; std::count_if inliné avec la lambda inlinée
    mov    rbx, QWORD PTR [rdi]          ; _M_start du vector
    mov    r12, QWORD PTR [rdi+8]        ; _M_finish
    xor    r13d, r13d                     ; compteur = 0
.L_loop:
    cmp    rbx, r12
    je     .L_done
    ; Charger l'élément (shared_ptr<Shape>) et appeler area()
    mov    rax, QWORD PTR [rbx]          ; Shape*
    mov    rcx, QWORD PTR [rax]          ; vptr
    mov    rdi, rax
    call   QWORD PTR [rcx+0x10]         ; area() — appel virtuel non dévirtualisable

    ; Comparer avec minArea (valeur capturée, maintenant constante immédiate)
    ucomisd xmm0, QWORD PTR [rip+.LC_10_0]  ; area > 10.0 ?
    seta   al
    movzx  eax, al
    add    r13, rax                       ; compteur += (area > 10.0)

    add    rbx, 16                        ; avancer de sizeof(shared_ptr)
    jmp    .L_loop
.L_done:
    ; r13 = nombre d'éléments avec area > 10.0
```

Remarquez que la capture `minArea` (qui valait 10.0) est devenue une **constante immédiate** dans le code (`[rip+.LC_10_0]`). La closure a entièrement disparu — la valeur capturée a été propagée comme constante par l'optimiseur. C'est l'avantage de l'inlining des lambdas : GCC peut propager les captures comme des constantes quand leurs valeurs sont connues au moment de la compilation.

> 💡 **En RE (`-O2`) :** quand une lambda est inlinée, elle ne laisse aucune trace structurelle dans le binaire. Le code de la lambda est fusionné avec la boucle de l'algorithme. La seule façon de reconnaître l'ancienne lambda est de comprendre la logique : « cette boucle itère sur un vector et compare quelque chose avec une constante, c'est probablement un `count_if` avec un prédicat ».

## Capture de `this`

Dans une méthode de classe, une lambda peut capturer `this` pour accéder aux membres de l'objet :

```cpp
class Canvas {
    void draw() const {
        auto drawShape = [this](const std::shared_ptr<Shape>& s) {
            std::cout << title_ << ": " << s->describe() << std::endl;
        };
        std::for_each(shapes_.begin(), shapes_.end(), drawShape);
    }
};
```

La closure capture `this` comme un pointeur :

```
Closure de drawShape :
┌──────────────────────────────────────┐
│  this (Canvas*)                      │  offset 0   — 8 octets
└──────────────────────────────────────┘
```

Dans l'`operator()`, l'accès à `title_` passe par deux niveaux d'indirection :

```nasm
; rdi = closure (this de la lambda)
mov    rax, QWORD PTR [rdi]          ; rax = closure->this (Canvas*)
; Accéder à Canvas::title_ (std::string à un certain offset de Canvas)
lea    rsi, [rax+0x10]               ; rsi = &(canvas->title_)
```

> 💡 **En RE :** une closure qui stocke un pointeur unique et l'utilise pour accéder à plusieurs champs via des offsets variés est une lambda qui capture `this`. Les offsets correspondent aux membres de la classe englobante.

En C++17, `[*this]` capture l'objet par **copie** au lieu de capturer le pointeur. La closure contient alors une copie complète de l'objet (potentiellement gros). En RE, cela se manifeste par un appel au constructeur de copie de la classe englobante lors de l'initialisation de la closure.

## Immediately Invoked Lambda Expression (IILE)

Un pattern courant en C++ moderne est d'utiliser une lambda immédiatement invoquée pour initialiser une variable complexe :

```cpp
const auto config = [&]() {
    Config c("default", 100, true);
    if (argc > 2) c.maxShapes = std::stoi(argv[2]);
    return c;
}();
```

En `-O0`, GCC génère la closure, l'appelle immédiatement, puis la détruit. En `-O2`, la lambda est entièrement inlinée : le code de la lambda se retrouve directement au site d'appel, et la closure n'existe jamais en tant qu'objet. L'IILE est alors indiscernable d'un bloc de code ordinaire dans le désassemblage.

## Résumé des patterns à reconnaître

| Pattern | Signification |  
|---------|---------------|  
| Symbole contenant `{lambda` ou `UlvE` / `UlRK...E` dans le mangling | `operator()` d'une lambda |  
| Symbole contenant `_FUN` associé à une lambda | Thunk de conversion en pointeur de fonction (lambda sans capture) |  
| Petite fonction recevant `this` (rdi) et accédant à `[rdi+offset]` | `operator()` d'une lambda avec captures |  
| Accès direct `[rdi+offset]` dans l'operator() | Capture par valeur (lecture directe du membre) |  
| Double indirection `mov rax, [rdi+offset]; op [rax]` | Capture par référence (pointeur puis déréférencement) |  
| `lea` + `mov` (stockage d'adresse dans la closure) | Initialisation d'une capture par référence |  
| Appel à un constructeur de copie lors de l'initialisation de la closure | Capture par valeur d'un type non trivial (string, vector...) |  
| `call [rdi+16]` sur un objet de 32 octets | Appel de `std::function` (invoker via _M_invoker) |  
| `_Function_handler::_M_invoke` / `_M_manager` | Type erasure pour une lambda dans un `std::function` |  
| Lambda disparue en `-O2`, logique fusionnée dans une boucle | Lambda inlinée par l'optimiseur |  
| Constante immédiate remplaçant une capture par valeur en `-O2` | Propagation de constante après inlining de la lambda |  
| Closure avec un seul pointeur accédant à des offsets variés | Lambda capturant `this` |  
| Plusieurs `operator()` pour le même `#N` avec des types différents | Lambda générique (`auto` parameter) instanciée plusieurs fois |

---


⏭️ [Smart Pointers en assembleur : `unique_ptr` vs `shared_ptr` (comptage de références)](/17-re-cpp-gcc/08-smart-pointers.md)
