🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 16.2 — Inlining de fonctions : quand la fonction disparaît du binaire

> **Fichier source associé** : `binaries/ch16-optimisations/inlining_demo.c`  
> **Compilation** : `make s16_2` (produit 6 variantes dans `build/`)

---

## Introduction

Dans la section précédente, nous avons vu que la fonction `square()` disparaissait du binaire en `-O2`. Ce phénomène — l'**inlining** — est probablement la transformation du compilateur qui a le plus d'impact sur le travail du reverse engineer.

L'inlining consiste à remplacer un appel de fonction (`call`) par une copie du corps de la fonction directement dans l'appelant. Le résultat est un binaire où certaines fonctions n'existent plus comme entités indépendantes : pas de symbole, pas de prologue, pas d'adresse à laquelle poser un breakpoint. Leur code est fusionné dans celui de l'appelant, parfois transformé au-delà de toute reconnaissance après interaction avec les autres passes d'optimisation.

Pour le reverse engineer, l'inlining a des conséquences directes :

- Le **graphe d'appels** reconstruit par Ghidra ou IDA est incomplet. Des fonctions qui existaient dans le source n'apparaissent nulle part.  
- La fonction `main()` (ou une autre fonction de haut niveau) devient **anormalement longue** — elle contient le code de dizaines de sous-fonctions fusionnées.  
- Les **cross-references** (XREF) vers les fonctions inlinées n'existent pas, puisque l'appel a été supprimé.  
- La **backtrace GDB** ne montre pas les fonctions inlinées dans la pile d'appels (sauf si les informations DWARF sont présentes et que GDB sait les exploiter).

Cette section explore les règles qui gouvernent l'inlining dans GCC, les scénarios où il se produit ou non, et les techniques pour le détecter et le « défaire » mentalement lors de l'analyse.

---

## Comment GCC décide d'inliner une fonction

L'inlining n'est pas un choix binaire. GCC utilise un ensemble d'heuristiques pour décider si le bénéfice (élimination du coût du `call`/`ret`, ouverture de nouvelles opportunités d'optimisation) justifie le coût (augmentation de la taille du code, pression sur le cache d'instructions).

### Les critères principaux

**La taille estimée du corps de la fonction.** GCC mesure la complexité d'une fonction en « gimple statements » — une représentation intermédiaire interne. Une fonction de moins de ~40 statements est considérée « petite » et éligible à l'inlining en `-O2`. Le seuil exact est contrôlé par le paramètre `--param max-inline-insns-auto` (valeur par défaut : 40 en `-O2`).

**Le nombre de sites d'appel.** Une fonction appelée une seule fois est presque toujours inlinée en `-O2`, quelle que soit sa taille — car l'inlining ne duplique pas de code dans ce cas. À l'inverse, une fonction appelée 50 fois ne sera inlinée que si elle est très petite, pour éviter l'explosion de la taille du `.text`.

**Le linkage de la fonction.** Seules les fonctions dont la définition est visible au moment de la compilation peuvent être inlinées. Concrètement :

- Les fonctions `static` définies dans le même fichier `.c` sont les premières candidates.  
- Les fonctions dans d'autres unités de compilation ne peuvent être inlinées qu'avec `-flto` (Link-Time Optimization, cf. section 16.5).  
- Les fonctions de bibliothèques partagées (`.so`) ne sont **jamais** inlinées — le compilateur ne voit pas leur code.

**Le niveau d'optimisation.** L'inlining est désactivé en `-O0`, conservateur en `-O1`, standard en `-O2`, et agressif en `-O3`. En `-Os`, l'inlining est plus restrictif qu'en `-O2` car la duplication de code augmente la taille du binaire.

### Le rôle des attributs

Le développeur peut influencer les décisions d'inlining avec des attributs GCC :

- `__attribute__((always_inline))` + `static inline` : force l'inlining, même si GCC le juge non rentable. Le corps est dupliqué à chaque site d'appel, y compris en `-O0` (à condition que l'optimisation minimale soit activée).  
- `__attribute__((noinline))` : interdit l'inlining. La fonction reste un `call` explicite à tous les niveaux d'optimisation. C'est utile pour le profilage et le débogage.  
- `inline` (mot-clé C99/C++) : c'est une **suggestion**, pas un ordre. GCC est libre de l'ignorer. En pratique, le mot-clé `inline` seul a peu d'effet sur les décisions de GCC en `-O2` — les heuristiques automatiques sont plus déterminantes.

---

## Scénario 1 — Fonctions triviales : inlinées dès `-O1`

Les fonctions les plus courtes — getters, setters, calculs d'une seule expression — sont systématiquement inlinées dès le premier niveau d'optimisation.

```c
typedef struct {
    int x;
    int y;
    int z;
} Vec3;

static int vec3_get_x(const Vec3 *v)
{
    return v->x;
}

static void vec3_set_x(Vec3 *v, int val)
{
    v->x = val;
}

static int vec3_length_squared(const Vec3 *v)
{
    return v->x * v->x + v->y * v->y + v->z * v->z;
}
```

### En `-O0`

Chaque fonction existe comme un symbole indépendant avec son propre frame de pile :

```asm
vec3_get_x:
    push   rbp
    mov    rbp, rsp
    mov    QWORD PTR [rbp-0x8], rdi     ; sauvegarde du pointeur v
    mov    rax, QWORD PTR [rbp-0x8]
    mov    eax, DWORD PTR [rax]          ; lecture de v->x
    pop    rbp
    ret
```

L'appel depuis `main()` :

```asm
    ; int vx = vec3_get_x(&v);
    lea    rdi, [rbp-0x20]               ; adresse de v
    call   vec3_get_x
    mov    DWORD PTR [rbp-0x24], eax     ; stocke vx sur la pile
```

Six instructions de « gestion » (prologue, sauvegarde, restauration, épilogue) pour une seule instruction utile (`mov eax, [rax]`). Le ratio signal/bruit est catastrophique.

### En `-O1` et au-delà

Les trois fonctions disparaissent. L'appel `vec3_get_x(&v)` est remplacé par un accès mémoire direct :

```asm
    ; int vx = vec3_get_x(&v) — inliné
    mov    eax, DWORD PTR [rsp+0x10]     ; lecture directe de v.x
```

Et `vec3_length_squared(&v)` :

```asm
    ; vec3_length_squared inliné
    mov    eax, DWORD PTR [rsp+0x10]     ; v.x
    imul   eax, eax                       ; x*x
    mov    edx, DWORD PTR [rsp+0x14]     ; v.y
    imul   edx, edx                       ; y*y
    add    eax, edx                       ; x*x + y*y
    mov    edx, DWORD PTR [rsp+0x18]     ; v.z
    imul   edx, edx                       ; z*z
    add    eax, edx                       ; x*x + y*y + z*z
```

Le `call` est remplacé par les opérations elles-mêmes. GCC peut ensuite appliquer d'autres optimisations sur ce code inliné : si la valeur de `v.x` est déjà dans un registre (par exemple après le `vec3_set_x`), il élimine le load redondant.

### Ce que le RE doit retenir

Si vous analysez un binaire optimisé et que vous voyez des accès directs à des champs de structure (`[rsp+offset]`) sans `call` préalable, il est probable qu'un getter/setter a été inliné. Cherchez les patterns d'accès à des offsets fixes depuis un même pointeur de base — c'est souvent le signe d'une structure dont les accesseurs ont été absorbés.

---

## Scénario 2 — Fonction de taille moyenne : l'inlining dépend du contexte

```c
static int transform_value(int input, int factor, int offset)
{
    int result = input;
    result = result * factor;
    result = result + offset;
    if (result < 0)
        result = -result;
    result = result % 1000;
    if (result > 500)
        result = 1000 - result;
    return result;
}
```

Cette fonction fait une dizaine de « gimple statements ». Elle est dans la zone grise de l'inlining : assez petite pour être inlinée si elle est appelée peu de fois, mais assez grosse pour que GCC hésite si elle est appelée partout.

### Un seul site d'appel → inlinée en `-O2`

Si `transform_value` n'est appelée qu'une seule fois dans le programme, GCC l'inline systématiquement en `-O2`. La raison est simple : l'inlining ne duplique pas de code quand il n'y a qu'un seul appelant. Le binaire est même potentiellement plus petit, car le prologue/épilogue de la fonction disparaît.

Après inlining, le code se retrouve intégré dans `main()`, et GCC peut appliquer des optimisations supplémentaires qui n'auraient pas été possibles sans inlining. Par exemple, si `factor` vaut 7 (constante connue dans `main()`), GCC peut propager cette constante et remplacer `imul` par une combinaison `lea` + `shl`.

### Plusieurs sites d'appel → seuil de taille

Si vous appelez `transform_value` à 10 endroits différents, GCC duplique le corps 10 fois. Pour une fonction de cette taille, le coût en taille de code l'emporte : GCC laisse un `call` classique.

Le seuil n'est pas absolu — il dépend de la taille du corps, du nombre de sites, et du flag `--param max-inline-insns-auto`. En `-O3`, le seuil est relevé et des fonctions plus grosses sont inlinées même avec plusieurs appelants.

### Comment vérifier

```bash
# Compter les symboles en O0 vs O2
nm build/inlining_demo_O0 | grep 'transform_value'
# → t transform_value   (symbole local présent)

nm build/inlining_demo_O2 | grep 'transform_value'
# → (rien — la fonction a été inlinée)
```

Si `transform_value` apparaît dans `nm` en `-O2`, c'est qu'elle n'a pas été inlinée (trop d'appelants ou trop volumineuse). Si elle n'apparaît pas, elle est inlinée.

---

## Scénario 3 — Fonction volumineuse : résiste à l'inlining

```c
static int heavy_computation(const int *data, int len)
{
    int acc = 0;
    int min_val = data[0];
    int max_val = data[0];

    for (int i = 0; i < len; i++) {
        acc += data[i] * data[i];
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }

    int range = max_val - min_val;
    if (range == 0) range = 1;

    int normalized = 0;
    for (int i = 0; i < len; i++) {
        normalized += ((data[i] - min_val) * 100) / range;
    }

    int checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum ^= (data[i] << (i % 8));
        checksum = (checksum >> 3) | (checksum << 29);
    }

    return acc + normalized + checksum;
}
```

Cette fonction contient trois boucles, des branchements, et un volume de code significatif. Même en `-O3`, GCC la garde comme une fonction séparée avec un `call` explicite.

### En `-O2`

```bash
nm build/inlining_demo_O2 | grep 'heavy_computation'
# → t heavy_computation   (toujours présente !)
```

La fonction apparaît comme un symbole local `t` — elle a son propre prologue, son épilogue, et `main()` l'appelle avec un `call`.

```asm
    ; dans main()
    lea    rdi, [rsp+0x30]              ; data[]
    mov    esi, 8                        ; len = 8
    call   heavy_computation
```

C'est une bonne nouvelle pour le RE : les fonctions volumineuses restent des entités identifiables dans le binaire, même optimisé. On peut les analyser séparément, poser des breakpoints dessus, et les nommer dans Ghidra.

### La leçon pour le RE

En règle générale, si une fonction fait plus de 30–50 instructions en `-O0`, elle survivra à l'inlining. Ce sont les « petites » fonctions (getters, wrappers, utilitaires d'une ligne) qui disparaissent. Les fonctions avec des boucles, du code conditionnel complexe, ou des appels à d'autres fonctions résistent.

---

## Scénario 4 — Récursion : jamais inlinée directement

```c
static int fibonacci(int n)
{
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}
```

L'inlining d'une fonction récursive est impossible en général : la profondeur de récursion n'est pas connue à la compilation, donc le compilateur ne peut pas « dérouler » tous les niveaux.

### Ce que GCC fait réellement

En `-O2`, `fibonacci` reste une fonction récursive avec deux `call fibonacci` dans son corps. Cependant, GCC peut appliquer une optimisation subtile en `-O3` : le **déroulement partiel de la récursion**. Il inline un ou deux niveaux de l'appel récursif, créant une version « dépliée » qui réduit le nombre d'appels réels.

Concrètement, GCC peut transformer ceci :

```c
// Conceptuellement, un niveau de déroulement :
return fibonacci(n - 1) + fibonacci(n - 2);
// ↓ inline fibonacci(n-1) :
// = (fibonacci(n-2) + fibonacci(n-3)) + fibonacci(n-2);
```

Dans le désassemblage, cela se manifeste par un corps de fonction plus gros avec des `call fibonacci` mais à des profondeurs décalées. Le pattern reste reconnaissable : une fonction qui s'appelle elle-même est toujours récursive, même si le compilateur a déroulé un niveau.

### Comparaison avec la version itérative

```c
static int fibonacci_iter(int n)
{
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}
```

`fibonacci_iter` n'est pas récursive — c'est une boucle simple. En `-O2`, elle est inlinée dans `main()` (un seul site d'appel, taille raisonnable), et la boucle est optimisée avec des registres :

```asm
    ; fibonacci_iter inliné
    cmp    edi, 1
    jle    .L_base_case
    xor    eax, eax                     ; a = 0
    mov    ecx, 1                       ; b = 1
    mov    edx, 2                       ; i = 2
.L_fib_loop:
    lea    esi, [rax+rcx]              ; tmp = a + b
    mov    eax, ecx                     ; a = b
    mov    ecx, esi                     ; b = tmp
    add    edx, 1                       ; i++
    cmp    edx, edi
    jle    .L_fib_loop
    ; ecx = résultat
```

Le contraste est frappant : la version itérative est compacte et entièrement dans des registres, tandis que la version récursive conserve des `call` et une croissance de pile proportionnelle à `n`.

### Ce que le RE doit retenir

En rencontrant un `call` vers la fonction elle-même dans un binaire optimisé, vous avez confirmé une récursion — le compilateur n'a pas pu l'éliminer. Si la récursion était terminale (tail recursion), elle a pu être transformée en boucle par la tail call optimization (section 16.4), et dans ce cas il n'y a plus de `call` récursif visible.

---

## Scénario 5 — Appel indirect : l'inlining est impossible

```c
typedef int (*operation_fn)(int, int);

static int op_add(int a, int b) { return a + b; }  
static int op_sub(int a, int b) { return a - b; }  
static int op_mul(int a, int b) { return a * b; }  

static int apply_operation(operation_fn op, int a, int b)
{
    return op(a, b);    /* appel indirect */
}
```

Quand une fonction est appelée via un pointeur, le compilateur ne sait pas, au moment de la compilation, quelle fonction sera exécutée. Il ne peut donc pas copier un corps spécifique — l'inlining est impossible.

### En `-O2`

L'appel indirect reste un `call` via un registre :

```asm
    ; apply_operation — l'appel indirect subsiste
    call   rax                          ; appel via pointeur de fonction
```

Ou, si `apply_operation` elle-même est inlinée dans `main()` :

```asm
    ; dans main() — apply_operation inliné mais l'appel indirect reste
    mov    rax, QWORD PTR [rsp+rbx*8]   ; charge ops[input % 3]
    mov    edi, r12d                     ; a = input
    lea    esi, [r12+5]                  ; b = input + 5
    call   rax                           ; appel indirect
```

Le `call rax` est la signature d'un appel indirect. En C++, les appels virtuels (via vtable) produisent exactement le même pattern : `call [registre + offset]`.

### La dévirtualisation

Il existe un cas particulier : si GCC peut **prouver** quelle fonction sera appelée malgré le pointeur (par exemple, si le pointeur est initialisé avec une constante juste avant l'appel), il peut « dévirtualiser » l'appel et inliner la fonction cible. C'est rare sans LTO, mais possible :

```c
operation_fn op = op_add;   // constante connue  
int result = op(3, 4);      // GCC peut inliner op_add  
```

En `-O2`, GCC peut transformer ceci en un simple `lea eax, [3+4]` → `mov eax, 7`. Mais si le pointeur vient d'un tableau ou d'une condition, la dévirtualisation échoue.

### Ce que le RE doit retenir

Un `call rax` ou `call [reg+offset]` dans un binaire optimisé est un **appel indirect** — le compilateur n'a pas pu résoudre la cible. Votre travail de RE est de remonter la chaîne pour trouver d'où vient la valeur du registre :

1. Cherchez le dernier `mov` ou `lea` qui écrit dans le registre utilisé par le `call`.  
2. Remontez jusqu'à la source : un tableau de pointeurs de fonctions, une vtable, un callback passé en paramètre.  
3. Identifiez les cibles possibles — ce sont les fonctions dont l'adresse est stockée dans ce tableau ou cette vtable.

---

## Scénario 6 — Chaîne d'inlining : A → B → C

Quand une fonction A appelle B qui appelle C, et que B et C sont toutes deux éligibles à l'inlining, GCC les fusionne en cascade. Le résultat est que le corps de C se retrouve directement dans A, sans aucune trace de B.

```c
static int step_c(int x)
{
    return x * 3 + 1;
}

static int step_b(int x)
{
    int tmp = step_c(x);
    return tmp + step_c(tmp);
}

static int step_a(int x)
{
    return step_b(x) + step_b(x + 1);
}
```

### En `-O0`

Le graphe d'appels est complet — trois fonctions, trois niveaux :

```
main() → step_a() → step_b() → step_c()
```

On peut poser un breakpoint sur chacune, observer les arguments, remonter la pile. Le graphe de Ghidra montre toutes les XREF.

### En `-O2`

Les trois fonctions sont fusionnées. `step_c` est inlinée dans `step_b`, puis `step_b` (qui contient maintenant le code de `step_c`) est inlinée dans `step_a`, qui est elle-même inlinée dans `main()`.

Le résultat dans `main()` :

```asm
    ; step_a(input) — tout inliné
    ; step_c(x) = x * 3 + 1

    ; step_b(x) = step_c(x) + step_c(step_c(x))
    ; = (x*3+1) + ((x*3+1)*3+1)
    ; = (x*3+1) + (x*9+3+1)
    ; = (x*3+1) + (x*9+4)
    ; = x*12 + 5

    ; step_a(x) = step_b(x) + step_b(x+1)
    ; = (x*12+5) + ((x+1)*12+5)
    ; = x*12+5 + x*12+12+5
    ; = x*24 + 22

    ; GCC peut réduire tout ça à :
    lea    eax, [rdi*8]                 ; eax = x * 8
    lea    eax, [rax+rdi*2]            ; eax = x * 8 + x * 2 = x * 10
    lea    eax, [rax+rdi*2]            ; eax = x * 10 + x * 2 = x * 12
    ; ... ou une autre combinaison de lea pour atteindre x*24+22
```

Selon la version de GCC, le résultat peut être encore plus simple : GCC évalue l'expression algébrique complète et produit la formule fermée `x * 24 + 22` en quelques instructions.

### Impact sur le graphe d'appels

```bash
# En O0 — 4 fonctions visibles
nm build/inlining_demo_O0 | grep -E 'step_[abc]|main'
# → t step_a
# → t step_b
# → t step_c
# → T main

# En O2 — seul main survit
nm build/inlining_demo_O2 | grep -E 'step_[abc]|main'
# → T main
```

Dans Ghidra, le graphe d'appels de `main()` en `-O2` ne montre aucune référence à `step_a`, `step_b` ou `step_c`. Deux niveaux d'abstraction entiers ont disparu.

### Ce que le RE doit retenir

Quand vous voyez une fonction `main()` anormalement longue dans un binaire optimisé, avec des séquences de calcul arithmétique qui ne semblent pas correspondre à une logique évidente, envisagez l'hypothèse d'une chaîne d'inlining. Le compilateur a pu fusionner 3, 4 ou 5 niveaux de fonctions et simplifier le résultat algébriquement.

Pour reconstituer les couches, cherchez des « blocs logiques » dans le code : des groupes d'instructions qui calculent un résultat intermédiaire réutilisé ensuite. Chaque bloc était potentiellement une fonction séparée dans le source.

---

## Scénario 7 — Contrôle explicite : `noinline` et `always_inline`

```c
__attribute__((noinline))
static int forced_noinline(int x)
{
    return x * x + x + 1;
}

__attribute__((always_inline))
static inline int forced_inline(int x)
{
    int result = x;
    for (int i = 0; i < 10; i++) {
        result = (result * 31) ^ (result >> 3);
    }
    return result;
}
```

### `noinline` — la fonction survit à tout prix

Même en `-O3`, `forced_noinline` reste un `call` explicite :

```asm
    ; dans main()
    mov    edi, r12d
    call   forced_noinline              ; call explicite, même en O3
```

```bash
nm build/inlining_demo_O3 | grep forced_noinline
# → t forced_noinline   (toujours présente)
```

C'est un outil précieux pour le développeur qui veut garder des points d'ancrage dans le binaire (pour le profiling, par exemple). Pour le RE, une fonction `noinline` est identifiable par le fait qu'elle existe comme symbole alors que des fonctions équivalentes ou plus grosses ont été inlinées — c'est un indice que le développeur a explicitement marqué la fonction.

### `always_inline` — duplication forcée

`forced_inline` est dupliquée à **chaque** site d'appel, peu importe la taille du code. Si elle est appelée 3 fois, son corps apparaît 3 fois dans le binaire :

```asm
    ; Premier appel : forced_inline(input)
    mov    eax, r12d
    ; Boucle déroulée (10 itérations → 10x le corps)
    imul   ecx, eax, 31
    mov    edx, eax
    sar    edx, 3
    xor    ecx, edx                     ; result = (result*31) ^ (result>>3)
    imul   eax, ecx, 31
    mov    edx, ecx
    sar    edx, 3
    xor    eax, edx
    ; ... (8 itérations de plus)
    ; résultat dans eax

    ; ... plus loin dans main() ...

    ; Deuxième appel : forced_inline(input + 1)
    lea    eax, [r12+1]
    ; Même séquence dupliquée intégralement
    imul   ecx, eax, 31
    mov    edx, eax
    sar    edx, 3
    xor    ecx, edx
    ; ... etc.
```

Le code de la boucle (10 itérations × quelques instructions) est dupliqué à chaque site d'appel. En `-O3`, la boucle est de plus complètement déroulée, ce qui produit une longue séquence linéaire sans aucun `jmp`.

### Ce que le RE doit retenir

Si vous voyez le **même pattern d'instructions** dupliqué à plusieurs endroits dans un binaire optimisé, c'est probablement une fonction `always_inline` (ou une macro C, qui a un effet similaire). Cherchez les séquences répétées : mêmes opcodes, mêmes constantes (`31`, `>> 3`), même structure — seuls les registres d'entrée changent.

Ce pattern est aussi fréquent dans les implémentations inline de primitives cryptographiques (les « rounds » de SHA-256, AES, etc.) et dans les macros de type `MIN()` / `MAX()`.

---

## Inlining et informations DWARF

Un aspect souvent méconnu : même quand une fonction est inlinée, les informations de débogage DWARF (si le binaire est compilé avec `-g`) peuvent conserver une trace de l'inlining. La section `.debug_info` contient des entrées `DW_TAG_inlined_subroutine` qui indiquent :

- Le nom de la fonction inlinée.  
- Le fichier source et le numéro de ligne de l'appel original.  
- La plage d'adresses dans le binaire où le code inliné se trouve.

GDB exploite ces informations. Quand vous steppez dans du code optimisé avec `-O2 -g`, GDB peut afficher le nom de la fonction inlinée dans la backtrace :

```
#0  0x00401234 in main ()
    inlined from vec3_get_x at inlining_demo.c:52
```

Ghidra peut aussi lire les DWARF et annoter les régions inlinées dans le décompilateur — à condition que le binaire n'ait pas été strippé.

Sur un binaire strippé (`-s`), toutes ces informations disparaissent. Il ne reste que le code brut, et c'est à vous de reconstituer les fonctions inlinées par analyse des patterns.

---

## Résumé : quand une fonction est-elle inlinée ?

| Caractéristique de la fonction | `-O0` | `-O1` | `-O2` | `-O3` | `-Os` |  
|---|---|---|---|---|---|  
| Triviale (1–3 instructions utiles) | Non | Oui | Oui | Oui | Oui |  
| Taille moyenne, 1 site d'appel | Non | Possible | Oui | Oui | Possible |  
| Taille moyenne, N sites d'appel | Non | Non | Possible | Oui | Non |  
| Volumineuse (boucles, branches) | Non | Non | Non | Rarement | Non |  
| Récursive | Non | Non | Non | Partiel (1–2 niveaux) | Non |  
| Appel indirect (pointeur de fn) | Non | Non | Non | Non | Non |  
| `__attribute__((always_inline))` | Non* | Oui | Oui | Oui | Oui |  
| `__attribute__((noinline))` | Non | Non | Non | Non | Non |

*\* `always_inline` peut fonctionner en `-O0` si la fonction est aussi déclarée `static inline` et que GCC a le minimum de support nécessaire, mais le comportement varie selon les versions.*

---

## Techniques de détection de l'inlining en RE

Voici une méthodologie pratique pour identifier les fonctions inlinées dans un binaire que vous analysez :

**1. Comparer le nombre de fonctions avec un binaire `-O0` de référence.** Si vous avez accès au binaire non optimisé (ou si vous pouvez le recompiler), comparez le nombre de symboles `T`/`t` dans `nm`. Les fonctions manquantes en `-O2` ont été inlinées.

**2. Chercher les blocs de code « déconnectés » dans `main()`.** Dans Ghidra, si `main()` fait 300 lignes de décompilation alors qu'un programme similaire devrait en faire 30, l'essentiel est du code inliné. Identifiez les « blocs logiques » — séquences qui calculent un résultat intermédiaire — et nommez-les comme des sous-fonctions.

**3. Repérer les constantes récurrentes.** Si la même constante magique (ex: `0x5F3759DF`, `31`, `0x9E3779B9`) apparaît à plusieurs endroits dans le binaire, c'est probablement une fonction utilitaire (hash, checksum) qui a été inlinée à chaque site d'appel. Chaque occurrence est une copie du même corps.

**4. Chercher les patterns de code dupliqués.** Avec `objdump -d`, cherchez des séquences d'opcodes identiques qui apparaissent à différentes adresses. Si la même séquence de 10 instructions (mêmes opcodes, mêmes constantes, registres différents) apparaît 3 fois dans `main()`, c'est une fonction inlinée appelée 3 fois.

**5. Utiliser les informations DWARF si disponibles.** Sur un binaire avec symboles (`-g`), exécutez :

```bash
readelf --debug-dump=info build/inlining_demo_O2 | grep -A 2 'DW_TAG_inlined_subroutine'
```

Cela liste toutes les fonctions inlinées avec leur nom original et leur plage d'adresses.

**6. Exploiter Compiler Explorer pour vérifier des hypothèses.** Si vous suspectez qu'un bloc de code est une fonction standard inlinée (ex: `strlen`, `abs`, un hash connu), tapez la fonction candidate sur [godbolt.org](https://godbolt.org) avec le même compilateur et niveau d'optimisation. Comparez le pattern assembleur produit avec ce que vous voyez dans le binaire.

---


⏭️ [Déroulage de boucles et vectorisation (SIMD/SSE/AVX)](/16-optimisations-compilateur/03-deroulage-vectorisation.md)
