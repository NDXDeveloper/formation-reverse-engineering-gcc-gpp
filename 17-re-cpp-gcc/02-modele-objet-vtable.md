🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.2 — Modèle objet C++ : vtable, vptr, héritage simple et multiple

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Le problème que résout le dispatch virtuel

Considérons un pointeur `Shape* s` qui peut désigner un `Circle`, un `Rectangle` ou un `Triangle`. Quand le code appelle `s->area()`, le compilateur ne peut pas connaître au moment de la compilation quelle implémentation de `area()` exécuter — cela dépend du type réel de l'objet à l'exécution. Le C++ résout ce problème par le **dispatch virtuel** : un mécanisme d'indirection à travers une table de pointeurs de fonctions.

Pour le reverse engineer, comprendre ce mécanisme est fondamental. Un appel virtuel dans le désassemblage ne ressemble pas à un `call` vers une adresse fixe — c'est un `call` vers une adresse lue dans une table, elle-même pointée par un champ caché de l'objet. Sans reconnaître ce pattern, on ne peut pas savoir quelle fonction est réellement appelée.

## La vtable : structure en mémoire

Pour chaque classe qui contient au moins une méthode virtuelle (y compris le destructeur), GCC génère une **vtable** (*virtual method table*) dans la section `.rodata` du binaire. La vtable est un tableau de pointeurs de fonctions, un par méthode virtuelle, dans l'ordre de leur déclaration dans la hiérarchie de classes.

La vtable complète générée par GCC selon l'Itanium ABI contient en réalité plus que les pointeurs de fonctions. Sa structure est la suivante :

```
Adresse décroissante (offsets négatifs depuis le début "visible")
┌─────────────────────────────────────────────┐
│  offset-to-top (ptrdiff_t)                  │  offset -16
├─────────────────────────────────────────────┤
│  pointeur vers typeinfo (RTTI)              │  offset -8
├═════════════════════════════════════════════╡  ← adresse pointée par le vptr
│  pointeur vers méthode virtuelle #0         │  offset 0
├─────────────────────────────────────────────┤
│  pointeur vers méthode virtuelle #1         │  offset +8
├─────────────────────────────────────────────┤
│  pointeur vers méthode virtuelle #2         │  offset +16
├─────────────────────────────────────────────┤
│  ...                                        │
└─────────────────────────────────────────────┘
```

Les deux champs à offsets négatifs (avant l'adresse que le vptr pointe) sont :

- **`offset-to-top`** : la distance en octets entre le sous-objet courant et le début de l'objet complet. Pour l'héritage simple, c'est toujours 0. Pour l'héritage multiple, les sous-objets secondaires ont un offset négatif non nul. Nous y reviendrons.

- **Pointeur vers typeinfo** : pointe vers la structure `_ZTI...` de la classe (utilisée par `dynamic_cast` et `typeid`). Si le binaire a été compilé avec `-fno-rtti`, ce champ est à `0`.

Les pointeurs de fonctions virtuelles commencent à l'offset 0 depuis l'adresse que le vptr stocke. Ils sont ordonnés selon l'ordre de déclaration des méthodes virtuelles dans la hiérarchie, en partant de la classe de base la plus haute.

## Le vptr : champ caché dans chaque objet

Chaque instance d'une classe polymorphe contient un pointeur caché appelé **vptr** (*virtual table pointer*), que le compilateur place **au tout début de l'objet** (offset 0). Ce vptr pointe vers la vtable correspondant au type réel de l'objet.

Le vptr n'apparaît nulle part dans le code source C++. C'est le compilateur qui l'ajoute, et c'est le constructeur qui l'initialise. Voici ce que cela donne pour la hiérarchie `Shape` / `Circle` :

```
Objet Circle en mémoire :
┌──────────────────────────────────────┐
│  vptr → vtable de Circle             │  offset 0    (8 octets, ajouté par GCC)
├──────────────────────────────────────┤
│  name_ (std::string)                 │  offset 8    (32 octets avec libstdc++)
├──────────────────────────────────────┤
│  x_ (double)                         │  offset 40
├──────────────────────────────────────┤
│  y_ (double)                         │  offset 48
├──────────────────────────────────────┤
│  radius_ (double)                    │  offset 56
└──────────────────────────────────────┘
                                          total : 64 octets
```

Les membres hérités de `Shape` (`name_`, `x_`, `y_`) viennent en premier (après le vptr), suivis des membres propres à `Circle` (`radius_`). Le vptr est toujours à l'offset 0.

> 💡 **En RE :** si vous voyez un accès mémoire à l'offset 0 d'un objet qui charge un pointeur, puis un accès indirect via ce pointeur avec un offset constant, c'est très probablement un appel virtuel via le vptr.

## Héritage simple : vtable et appel virtuel

### La vtable de la classe de base abstraite

Commençons par `Shape`, qui déclare trois méthodes virtuelles (plus le destructeur) :

```cpp
class Shape {
    virtual ~Shape() = default;        // virtuel #0 (D1) et #1 (D0)
    virtual double area() const = 0;   // virtuel #2 (pure)
    virtual double perimeter() const = 0; // virtuel #3 (pure)
    virtual std::string describe() const; // virtuel #4
};
```

La vtable de `Shape` ressemble à ceci :

```
vtable for Shape (_ZTV5Shape) :
  [offset-to-top]   = 0
  [typeinfo]         = &_ZTI5Shape
  [slot 0]           → Shape::~Shape() [D1]          (complete destructor)
  [slot 1]           → Shape::~Shape() [D0]          (deleting destructor)
  [slot 2]           → __cxa_pure_virtual             (area = 0 → pure)
  [slot 3]           → __cxa_pure_virtual             (perimeter = 0 → pure)
  [slot 4]           → Shape::describe() const
```

Les méthodes virtuelles pures pointent vers `__cxa_pure_virtual`, une fonction du runtime C++ qui termine le programme avec une erreur si elle est appelée. On ne devrait jamais l'appeler en pratique puisque `Shape` ne peut pas être instanciée directement.

> 💡 **En RE :** si vous voyez `__cxa_pure_virtual` dans une vtable, vous savez que la classe est abstraite et que le slot correspondant est une méthode virtuelle pure. C'est un marqueur très fiable.

### La vtable d'une classe dérivée

`Circle` hérite de `Shape` et implémente les méthodes pures, plus un override de `describe()` :

```
vtable for Circle (_ZTV6Circle) :
  [offset-to-top]   = 0
  [typeinfo]         = &_ZTI6Circle
  [slot 0]           → Circle::~Circle() [D1]
  [slot 1]           → Circle::~Circle() [D0]
  [slot 2]           → Circle::area() const           ← override
  [slot 3]           → Circle::perimeter() const      ← override
  [slot 4]           → Circle::describe() const       ← override
```

Les slots sont aux **mêmes indices** que dans la vtable de `Shape`. C'est ce qui permet au dispatch virtuel de fonctionner : quel que soit le type réel de l'objet derrière un `Shape*`, le slot 2 est toujours `area()`, le slot 3 est toujours `perimeter()`, etc. Seuls les pointeurs changent.

Pour `Triangle`, qui ne surcharge pas `describe()` :

```
vtable for Triangle (_ZTV8Triangle) :
  [offset-to-top]   = 0
  [typeinfo]         = &_ZTI8Triangle
  [slot 0]           → Triangle::~Triangle() [D1]
  [slot 1]           → Triangle::~Triangle() [D0]
  [slot 2]           → Triangle::area() const
  [slot 3]           → Triangle::perimeter() const
  [slot 4]           → Shape::describe() const        ← hérité, pas surchargé
```

Le slot 4 pointe vers `Shape::describe()` car `Triangle` ne fournit pas sa propre implémentation. En RE, quand un slot d'une vtable de classe dérivée pointe vers une méthode de la classe parente, cela indique l'absence de surcharge.

### Le constructeur initialise le vptr

Le constructeur est l'endroit où le vptr est écrit. Voici une version simplifiée de ce que GCC génère pour `Circle::Circle()` en `-O0` :

```nasm
; Circle::Circle(double x, double y, double r)
; rdi = this, xmm0 = x, xmm1 = y, xmm2 = r
Circle::Circle:
    push   rbp
    mov    rbp, rsp
    sub    rsp, 0x30
    mov    QWORD PTR [rbp-0x8], rdi      ; sauver this
    movsd  QWORD PTR [rbp-0x10], xmm0    ; sauver x
    movsd  QWORD PTR [rbp-0x18], xmm1    ; sauver y
    movsd  QWORD PTR [rbp-0x20], xmm2    ; sauver r

    ; ---- Appel au constructeur parent Shape::Shape() ----
    mov    rdi, QWORD PTR [rbp-0x8]      ; this (même adresse)
    lea    rsi, [rip+.LC_Circle]         ; "Circle"
    movsd  xmm0, QWORD PTR [rbp-0x10]   ; x
    movsd  xmm1, QWORD PTR [rbp-0x18]   ; y
    call   Shape::Shape(std::string const&, double, double)

    ; ---- Écriture du vptr de Circle ----
    mov    rax, QWORD PTR [rbp-0x8]      ; this
    lea    rdx, [rip+_ZTV6Circle+16]     ; &vtable[0] (saute offset-to-top et typeinfo)
    mov    QWORD PTR [rax], rdx          ; this->vptr = &Circle::vtable

    ; ---- Initialisation de radius_ ----
    mov    rax, QWORD PTR [rbp-0x8]      ; this
    movsd  xmm0, QWORD PTR [rbp-0x20]   ; r
    movsd  QWORD PTR [rax+0x38], xmm0   ; this->radius_ = r (offset 56)

    ; ---- Vérification r > 0 (exception si non) ----
    ...
```

Points clés à observer :

1. **Le constructeur parent est appelé en premier.** `Shape::Shape()` initialise le vptr à `&_ZTV5Shape+16` et les membres de `Shape`. Ce vptr sera immédiatement écrasé.

2. **Le vptr est écrasé par la classe dérivée.** L'instruction `mov QWORD PTR [rax], rdx` écrit `&_ZTV6Circle+16` à l'offset 0 de `this`. Le `+16` saute les deux champs à offsets négatifs (offset-to-top et typeinfo pointer, chacun 8 octets).

3. **Chaque niveau de la hiérarchie écrit son propre vptr.** Pendant la construction d'un `Circle`, le vptr pointe d'abord vers la vtable de `Shape` (après l'appel à `Shape::Shape`), puis vers celle de `Circle`. C'est pourquoi appeler une méthode virtuelle dans un constructeur de base appelle la version de la classe de base, pas celle de la classe dérivée — un comportement C++ connu qui se voit très bien en RE.

> 💡 **Pattern RE pour identifier les constructeurs :** cherchez les fonctions qui : (1) reçoivent un pointeur en `rdi` (this), (2) appellent un autre constructeur avec le même `rdi`, (3) écrivent une adresse de vtable à l'offset 0 de `this`, (4) initialisent d'autres champs à des offsets fixes. C'est la signature d'un constructeur de classe dérivée.

### L'appel virtuel en assembleur

L'appel `s->area()` où `s` est un `Shape*` se compile ainsi :

```nasm
; rdi contient s (Shape*)
mov    rax, QWORD PTR [rdi]         ; rax = s->vptr (lecture du vptr à l'offset 0)  
call   QWORD PTR [rax+0x10]         ; appel indirect : vtable[slot 2] = area()  
                                     ; offset 0x10 = 16 = slot 2 × 8 octets
```

C'est le **pattern fondamental du dispatch virtuel**. Deux instructions :

1. Charger le vptr depuis le début de l'objet : `mov rax, [rdi]`  
2. Appeler indirectement via la vtable à un offset fixe : `call [rax+offset]`

L'offset dans la vtable identifie la méthode. Avec la vtable de `Shape` :

| Offset | Slot | Méthode |  
|--------|------|---------|  
| `+0x00` | 0 | `~Shape()` (D1, complete destructor) |  
| `+0x08` | 1 | `~Shape()` (D0, deleting destructor) |  
| `+0x10` | 2 | `area() const` |  
| `+0x18` | 3 | `perimeter() const` |  
| `+0x20` | 4 | `describe() const` |

En voyant `call [rax+0x10]`, on sait que c'est un appel à la troisième méthode virtuelle (slot 2). En consultant la vtable, on identifie `area()`.

> ⚠️ **Attention aux optimisations.** En `-O2`, GCC peut dévirtualiser un appel quand il connaît le type réel au moment de la compilation. Par exemple, si la variable est un `Circle` local (pas un pointeur polymorphe), GCC remplacera l'appel indirect par un `call` direct vers `Circle::area()`. On perd alors le pattern de dispatch virtuel dans le désassemblage, mais on gagne la certitude de la fonction appelée.

### Méthode de reconstruction d'une vtable dans Ghidra

Pour reconstruire une vtable dans Ghidra :

1. **Localiser la vtable.** Cherchez les symboles `_ZTV` dans le Symbol Tree, ou naviguez dans `.rodata` et cherchez des suites de pointeurs vers `.text`. Si le binaire est strippé, cherchez les chaînes `_ZTS` (typeinfo names) et remontez aux structures `_ZTI` adjacentes, puis aux vtables qui les référencent.

2. **Compter les slots.** Depuis l'adresse pointée par le vptr (après offset-to-top et typeinfo), chaque QWORD est un pointeur vers une méthode virtuelle. Comptez les slots jusqu'à la prochaine vtable (reconnaissable par un nouveau offset-to-top, généralement 0 ou une valeur négative).

3. **Identifier chaque slot.** Double-cliquez sur chaque pointeur pour naviguer vers la fonction. Analysez-la pour déterminer ce qu'elle fait. Les deux premiers slots sont presque toujours les destructeurs (D1 et D0).

4. **Créer une structure dans Ghidra.** Menu Data Type Manager → New Structure. Définissez le vptr comme premier champ (type `pointer`), suivi des membres déduits de l'analyse des constructeurs et des accès mémoire.

## Héritage multiple : le cas complexe

L'héritage multiple est le point où le modèle objet C++ devient significativement plus complexe. Quand une classe hérite de plusieurs bases polymorphes, l'objet contient **plusieurs vptr** et la vtable contient **plusieurs sous-tables**.

Prenons notre classe `Canvas` :

```cpp
class Canvas : public Drawable, public Serializable {
    std::string title_;
    std::vector<std::shared_ptr<Shape>> shapes_;
    int z_order_;
    ...
};
```

`Canvas` hérite de `Drawable` (qui a des méthodes virtuelles `draw()` et `zOrder()`) et de `Serializable` (qui a des méthodes virtuelles `serialize()` et `deserialize()`).

### Layout mémoire avec héritage multiple

GCC place les sous-objets des classes de base dans l'ordre de déclaration dans la liste d'héritage :

```
Objet Canvas en mémoire :
┌──────────────────────────────────────────┐
│                                          │
│  Sous-objet Drawable :                   │
│  ┌──────────────────────────────────┐    │
│  │  vptr_1 → vtable Canvas (part 1) │    │  offset 0
│  └──────────────────────────────────┘    │
│                                          │
│  Sous-objet Serializable :               │
│  ┌──────────────────────────────────┐    │
│  │  vptr_2 → vtable Canvas (part 2) │    │  offset 8
│  └──────────────────────────────────┘    │
│                                          │
│  Membres propres de Canvas :             │
│  ┌──────────────────────────────────┐    │
│  │  title_ (std::string)            │    │  offset 16
│  │  shapes_ (std::vector<...>)      │    │  offset 48
│  │  z_order_ (int)                  │    │  offset 72
│  └──────────────────────────────────┘    │
│                                          │
└──────────────────────────────────────────┘
```

L'objet contient **deux vptr** :

- **`vptr_1`** (offset 0) : pointe vers la partie de la vtable de `Canvas` qui correspond à l'interface `Drawable`. C'est aussi la vtable utilisée quand on manipule l'objet via un pointeur `Canvas*` ou `Drawable*`.

- **`vptr_2`** (offset 8) : pointe vers la partie de la vtable de `Canvas` qui correspond à l'interface `Serializable`. C'est la vtable utilisée quand on manipule l'objet via un pointeur `Serializable*`.

### La vtable composite

GCC génère une vtable unique pour `Canvas` qui contient les deux sous-tables consécutivement :

```
vtable for Canvas (_ZTV6Canvas) :

  ═══ Partie 1 : interface Drawable (et Canvas elle-même) ═══
  [offset-to-top]   = 0
  [typeinfo]         = &_ZTI6Canvas
  [slot 0]           → Canvas::~Canvas() [D1]
  [slot 1]           → Canvas::~Canvas() [D0]
  [slot 2]           → Canvas::draw() const
  [slot 3]           → Canvas::zOrder() const
  [slot 4]           → Canvas::serialize() const      ← propre à Canvas
  [slot 5]           → Canvas::deserialize(string const&) ← propre à Canvas

  ═══ Partie 2 : interface Serializable ═══
  [offset-to-top]   = -8                               ← distance vers le début de l'objet
  [typeinfo]         = &_ZTI6Canvas                     ← même typeinfo
  [slot 0]           → thunk to Canvas::~Canvas() [D1]
  [slot 1]           → thunk to Canvas::~Canvas() [D0]
  [slot 2]           → thunk to Canvas::serialize() const
  [slot 3]           → thunk to Canvas::deserialize(string const&)
```

Plusieurs points cruciaux pour le RE :

1. **La première partie** de la vtable sert à la fois pour `Canvas*` et pour `Drawable*`. Elle inclut les méthodes héritées de `Drawable` aux mêmes indices que dans la vtable de `Drawable`, plus les méthodes propres à `Canvas` et celles de `Serializable` (pour un accès direct depuis un `Canvas*`).

2. **La deuxième partie** contient les méthodes de l'interface `Serializable`, mais elles pointent vers des **thunks**, pas vers les méthodes directement.

3. **L'offset-to-top** de la partie 2 est `-8`, ce qui signifie : « pour retrouver le début de l'objet `Canvas` complet depuis le sous-objet `Serializable`, reculez de 8 octets ».

### Thunks : l'ajustement de pointeur

Un **thunk** est un petit fragment de code généré par le compilateur qui ajuste le pointeur `this` avant d'appeler la vraie méthode. Il est nécessaire parce que, quand on appelle une méthode de `Canvas` via un pointeur `Serializable*`, le pointeur `this` pointe vers le sous-objet `Serializable` (offset 8 dans l'objet complet), pas vers le début de l'objet `Canvas`.

Voici à quoi ressemble un thunk en assembleur :

```nasm
; Thunk non-virtuel pour Canvas::serialize(), ajustement -8
_ZThn8_NK6Canvas9serializeEv:
    sub    rdi, 8          ; ajuster this : Serializable* → Canvas*
    jmp    _ZNK6Canvas9serializeEv   ; sauter vers la vraie méthode
```

Le thunk fait exactement deux choses :
1. Soustrait l'offset du sous-objet de `rdi` (this) pour obtenir l'adresse de l'objet complet.  
2. Saute (pas `call`, mais `jmp`) vers l'implémentation réelle.

> 💡 **Pattern RE pour identifier les thunks :** une fonction de 2-3 instructions qui ajuste `rdi` par addition ou soustraction d'une constante puis fait un `jmp` vers une autre fonction. Le symbole manglé commence par `_ZThn` (thunk non-virtuel avec ajustement `n`) ou `_ZTv` (thunk virtuel). En Ghidra, les thunks sont parfois automatiquement reconnus et affichés comme « thunk function ».

### Conversion de pointeur à l'exécution

Quand le code source effectue un cast implicite d'un `Canvas*` vers un `Serializable*`, le compilateur génère un ajustement de pointeur :

```cpp
Canvas canvas("test", 1);  
Serializable* ser = &canvas;   // conversion implicite  
```

En assembleur :

```nasm
; rax = adresse de canvas (Canvas*)
lea    rdx, [rax+8]      ; rdx = Serializable* (décalage de 8 octets)
```

Le pointeur `Serializable*` ne pointe pas au même endroit que le pointeur `Canvas*`. Il pointe 8 octets plus loin, vers le sous-objet `Serializable` et son vptr_2.

Inversement, quand on reconvertit de `Serializable*` vers `Canvas*` (via `dynamic_cast` ou `static_cast`), le compilateur soustrait 8 :

```nasm
; rdi = Serializable*
sub    rdi, 8             ; rdi = Canvas*
```

> ⚠️ **Piège RE fréquent :** quand vous voyez un `add` ou `sub` sur un pointeur d'objet suivi d'un appel ou d'un accès à un vptr, c'est un ajustement d'héritage multiple. Ne le confondez pas avec un accès à un membre — l'indice est que le résultat est utilisé comme `this` (dans `rdi`) pour un appel, ou qu'on charge un vptr (QWORD à l'offset 0) depuis l'adresse ajustée.

### Distinguer héritage simple et multiple en RE

Voici les indices qui signalent un héritage multiple dans un binaire :

| Indice | Ce que ça signifie |  
|--------|--------------------|  
| Un objet contient **deux vptr ou plus** (deux pointeurs de vtable à des offsets différents) | L'objet hérite de plusieurs classes polymorphes |  
| Une vtable contient un **offset-to-top négatif** (valeur non nulle) | C'est la sous-table d'un sous-objet secondaire |  
| Des **thunks** (`sub rdi, N; jmp ...`) apparaissent dans `.text` | Ajustement de `this` pour les méthodes de bases secondaires |  
| Un **ajustement de pointeur** (`add`/`sub` constant) apparaît lors d'un cast entre types | Conversion entre base primaire et base secondaire |  
| Le constructeur écrit **plusieurs vptr** à des offsets différents de `this` | Initialisation de plusieurs sous-objets polymorphes |  
| La **typeinfo** référence un `__vmi_class_type_info` au lieu d'un `__si_class_type_info` | La RTTI encode un héritage multiple (voir section 17.3) |

## Reconstruire une hiérarchie de classes depuis la vtable

En RE, la procédure pour reconstituer la hiérarchie à partir des vtables est la suivante :

**Étape 1 — Lister toutes les vtables.** Utilisez `nm -C binaire | grep 'vtable for'` (avec symboles) ou cherchez les chaînes typeinfo name dans `.rodata` (sans symboles). Chaque vtable = une classe polymorphe.

**Étape 2 — Analyser les slots de chaque vtable.** Notez les pointeurs de fonctions dans chaque slot. Deux vtables qui partagent des pointeurs dans les mêmes slots ont une relation d'héritage.

**Étape 3 — Comparer les vtables deux à deux.** Si la vtable de `B` a les mêmes premiers slots que la vtable de `A` (mêmes pointeurs ou pointeurs vers des surcharges), alors `B` hérite probablement de `A`. Les slots supplémentaires à la fin de la vtable de `B` sont des méthodes virtuelles ajoutées par `B`.

**Étape 4 — Identifier les classes abstraites.** Les vtables qui contiennent des pointeurs vers `__cxa_pure_virtual` correspondent à des classes abstraites.

**Étape 5 — Chercher les thunks et vtables composites.** Si une vtable contient une seconde section avec un offset-to-top négatif, c'est de l'héritage multiple. Les thunks associés indiquent quelles méthodes de la base secondaire sont implémentées.

**Exemple concret avec notre binaire :**

```bash
$ nm -C oop_O0 | grep 'vtable for' | awk '{print $NF}'
vtable for Shape  
vtable for Circle  
vtable for Rectangle  
vtable for Triangle  
vtable for Drawable  
vtable for Serializable  
vtable for Canvas  
```

En examinant les vtables :
- `Circle`, `Rectangle`, `Triangle` ont la même structure de slots que `Shape` → ils héritent de `Shape`.  
- Les vtables de `Circle` et `Rectangle` ont des pointeurs différents aux slots 2-4 mais pas de slots supplémentaires → ils surchargent les méthodes de `Shape` sans en ajouter.  
- La vtable de `Canvas` a une structure composite avec deux parties → héritage multiple de `Drawable` et `Serializable`.  
- La vtable de `Shape` contient `__cxa_pure_virtual` aux slots 2 et 3 → `Shape` est abstraite.

## Visualiser une vtable dans Ghidra

Dans Ghidra, une vtable dans `.rodata` apparaît comme une suite de valeurs 8 octets. Par défaut, Ghidra ne les reconnaît pas forcément comme des pointeurs. Voici comment les rendre lisibles :

1. **Naviguez à l'adresse de la vtable.** Si les symboles sont présents, cherchez `_ZTV` dans le Symbol Tree. Sinon, cherchez dans `.rodata` des suites d'adresses qui pointent vers `.text`.

2. **Typez chaque entrée comme pointeur.** Sélectionnez les 8 premiers octets après l'offset-to-top et le typeinfo, faites clic droit → Data → pointer. Répétez pour chaque slot. Ghidra résoudra automatiquement les adresses vers les noms de fonctions.

3. **Créez un type structure.** Dans le Data Type Manager, créez une structure nommée `vtable_NomClasse` avec des champs de type pointeur-vers-fonction. Appliquez ce type à l'adresse de la vtable.

4. **Vérifiez avec les constructeurs.** Chaque constructeur écrit l'adresse `&vtable + 16` dans le vptr. Double-cliquez sur les cross-references de la vtable pour trouver les constructeurs et confirmer quelle classe utilise quelle vtable.

## Impact des optimisations sur les vtables

Le niveau d'optimisation affecte significativement la façon dont les vtables et le dispatch virtuel apparaissent dans le binaire :

**En `-O0` :** chaque appel virtuel produit fidèlement le pattern `mov rax, [rdi]; call [rax+offset]`. Les constructeurs écrivent explicitement le vptr. Tout est lisible et prévisible.

**En `-O2` / `-O3` :** GCC applique plusieurs transformations :

- **Dévirtualisation.** Quand le compilateur peut prouver le type réel de l'objet (variable locale, résultat de `new`, propagation de type), il remplace l'appel indirect par un appel direct. L'appel virtuel disparaît du désassemblage.

- **Spéculative devirtualization.** GCC peut émettre une comparaison du vptr contre la vtable attendue, et si elle réussit, appeler directement la méthode (fast path) au lieu de passer par l'indirection (slow path). Cela produit un pattern de type :

  ```nasm
  mov    rax, [rdi]                    ; charger vptr
  lea    rdx, [rip+_ZTV6Circle+16]     ; vtable attendue
  cmp    rax, rdx                      ; est-ce un Circle ?
  jne    .slow_path                    ; sinon, dispatch normal
  call   Circle::area                  ; fast path : appel direct
  jmp    .done
  .slow_path:
  call   [rax+0x10]                    ; slow path : dispatch virtuel
  .done:
  ```

- **Inlining de petites méthodes virtuelles.** Si une méthode virtuelle est triviale (accesseur, retour de constante), GCC peut l'inliner après dévirtualisation. La méthode disparaît alors complètement comme appel distinct.

- **Élimination de vtables inutilisées.** Avec `-ffunction-sections` + `--gc-sections` (ou LTO), les vtables de classes jamais instanciées peuvent être éliminées par le linker.

> 💡 **Conseil pratique :** commencez toujours l'analyse avec la variante `-O0` pour comprendre la hiérarchie de classes et le rôle de chaque méthode virtuelle. Puis passez à `-O2` pour voir quelles optimisations GCC a appliquées et comprendre pourquoi certains appels virtuels ont disparu.

## Résumé des patterns à reconnaître

| Pattern assembleur | Signification |  
|--------------------|---------------|  
| `mov rax, [rdi]` puis `call [rax+N]` | Appel virtuel, slot N/8 dans la vtable |  
| Écriture d'une adresse `.rodata` à `[rdi+0]` dans un constructeur | Initialisation du vptr |  
| Écriture de **deux** adresses `.rodata` à `[rdi+0]` et `[rdi+K]` | Constructeur avec héritage multiple (deux vptr) |  
| `sub rdi, N; jmp func` (2-3 instructions) | Thunk d'ajustement pour héritage multiple |  
| `lea rdx, [rdi+N]` suivi d'un stockage ou d'un appel avec `rdx` comme `this` | Conversion de pointeur entre bases (héritage multiple) |  
| Suite de pointeurs vers `.text` dans `.rodata`, précédée d'un 0 et d'un pointeur `_ZTI` | Vtable (offset-to-top = 0, typeinfo, puis slots) |  
| `__cxa_pure_virtual` dans un slot de vtable | Méthode virtuelle pure → classe abstraite |  
| Appel direct là où on attendrait un dispatch indirect | Dévirtualisation par le compilateur (`-O2`+) |

---


⏭️ [RTTI (Run-Time Type Information) et `dynamic_cast`](/17-re-cpp-gcc/03-rtti-dynamic-cast.md)
