🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.9 — Coroutines C++20 : reconnaître le frame et le state machine pattern

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Ce que les coroutines changent pour le RE

Une fonction classique commence à son point d'entrée, s'exécute jusqu'à son `return`, et ne peut pas être interrompue entre les deux. Une coroutine brise ce modèle : elle peut **suspendre** son exécution à des points arbitraires (`co_await`, `co_yield`), rendre le contrôle à l'appelant, puis **reprendre** plus tard exactement là où elle s'était arrêtée, avec tout son état local intact.

Pour réaliser cette magie, le compilateur applique une transformation profonde. La fonction linéaire du code source est découpée en fragments et réassemblée sous la forme d'une **machine à états**. Les variables locales, qui vivraient normalement sur la pile, sont déplacées dans un **coroutine frame** alloué sur le heap (car la pile de l'appelant peut avoir disparu au moment de la reprise). Chaque point de suspension devient un état numéroté, et la reprise de la coroutine consiste à sauter directement au bon état via un dispatch (switch ou table de sauts).

Le résultat dans le désassemblage est déroutant : une fonction apparemment simple dans le code source se transforme en une ou plusieurs fonctions complexes contenant un dispatch initial sur un index d'état, des sauts vers des blocs de code non contigus, et des accès mémoire systématiques via un pointeur vers le heap au lieu de la pile. Sans connaître les patterns de cette transformation, le code décompilé est quasiment incompréhensible.

> ⚠️ **Support GCC.** Le support des coroutines C++20 dans GCC est disponible depuis GCC 10 (expérimental) et considéré stable à partir de GCC 11. Le code généré a évolué entre les versions. Les patterns décrits ici correspondent à GCC 11–14. Les versions plus récentes pourraient optimiser différemment certains aspects.

> ⚠️ **Binaire d'entraînement.** Le binaire `ch17-oop` de ce chapitre n'inclut pas de coroutines. Les exemples de cette section sont fournis sous forme de code source et de pseudo-assembleur pour illustration. Pour expérimenter, compilez les exemples avec `g++ -std=c++20 -fcoroutines -O0 -g`.

## Rappel rapide du modèle coroutine C++20

Une fonction est une coroutine si son corps contient au moins un des trois opérateurs : `co_await`, `co_yield` ou `co_return`. Le compilateur déduit alors automatiquement le type de la coroutine à partir du type de retour, qui doit fournir un **promise type** imbriqué.

Voici un générateur minimal qui illustre les concepts :

```cpp
#include <coroutine>
#include <iostream>
#include <optional>

struct Generator {
    struct promise_type {
        int current_value;
        bool finished = false;

        Generator get_return_object() {
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { std::terminate(); }
        std::suspend_always yield_value(int value) {
            current_value = value;
            return {};
        }
        void return_void() { finished = true; }
    };

    std::coroutine_handle<promise_type> handle;

    bool next() {
        if (!handle || handle.done()) return false;
        handle.resume();
        return !handle.promise().finished;
    }

    int value() const { return handle.promise().current_value; }

    ~Generator() { if (handle) handle.destroy(); }
};

Generator range(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}
```

Le code de `range()` est linéaire et lisible. Mais ce que GCC en fait est radicalement différent.

## Le coroutine frame

### Allocation et layout

Quand une coroutine est appelée, GCC alloue un **coroutine frame** sur le heap. Ce frame contient tout ce dont la coroutine a besoin pour survivre entre les suspensions :

```
Coroutine frame (alloué sur le heap) :
┌──────────────────────────────────────────────┐
│  resume function pointer (void(*)(frame*))   │  offset 0    — pointeur vers la fonction de reprise
├──────────────────────────────────────────────┤
│  destroy function pointer (void(*)(frame*))  │  offset 8    — pointeur vers la fonction de destruction
├──────────────────────────────────────────────┤
│  promise object (promise_type)               │  offset 16   — l'objet promise
├──────────────────────────────────────────────┤
│  suspension index (int ou short)             │  offset 16 + sizeof(promise)
├──────────────────────────────────────────────┤
│  variables locales de la coroutine :         │
│    paramètres copiés                         │
│    variables automatiques                    │
│    temporaires nécessaires entre suspend     │
├──────────────────────────────────────────────┤
│  (padding / alignement)                      │
└──────────────────────────────────────────────┘
```

Les deux premiers champs sont fondamentaux :

- **`resume`** (offset 0) : pointeur vers la fonction qui reprend l'exécution de la coroutine. Quand on appelle `handle.resume()`, le runtime appelle `frame->resume(frame)`.

- **`destroy`** (offset 8) : pointeur vers la fonction qui détruit le frame et libère ses ressources. Appelée par `handle.destroy()`.

Ces deux pointeurs de fonction sont le mécanisme de dispatch principal. Ils remplacent le vptr/vtable du polymorphisme classique par un mécanisme plus direct.

### Allocation en assembleur

L'appel initial à la coroutine `range(1, 10)` génère :

```nasm
range(int, int):
    ; 1. Allouer le coroutine frame
    mov    edi, 48                        ; taille du frame (varie selon les variables locales)
    call   operator new(unsigned long)@plt
    mov    rbx, rax                       ; rbx = pointeur vers le frame

    ; 2. Initialiser les pointeurs de fonction
    lea    rax, [rip+range.resume]        ; adresse de la fonction de reprise
    mov    QWORD PTR [rbx], rax           ; frame->resume = &range.resume
    lea    rax, [rip+range.destroy]       ; adresse de la fonction de destruction
    mov    QWORD PTR [rbx+8], rax         ; frame->destroy = &range.destroy

    ; 3. Construire le promise object
    lea    rdi, [rbx+16]                  ; this = &frame->promise
    call   Generator::promise_type::promise_type()

    ; 4. Copier les paramètres dans le frame
    mov    DWORD PTR [rbx+36], esi        ; frame->start = start (premier paramètre)
    mov    DWORD PTR [rbx+40], edx        ; frame->end = end (deuxième paramètre)

    ; 5. Initialiser le suspension index
    mov    DWORD PTR [rbx+32], 0          ; frame->index = 0 (état initial)

    ; 6. Appeler promise.get_return_object()
    lea    rdi, [rbx+16]
    call   Generator::promise_type::get_return_object()
    ; rax = le Generator (contient le coroutine_handle)

    ; 7. Appeler promise.initial_suspend()
    ; Si le résultat est suspend_always → la coroutine est suspendue immédiatement
    ; Le contrôle revient à l'appelant avec le Generator
    ret
```

> 💡 **Pattern RE :** une fonction qui commence par `operator new`, stocke deux pointeurs de fonction aux offsets 0 et 8 du bloc alloué, initialise un index d'état, copie ses paramètres dans le bloc heap, puis retourne sans avoir exécuté la logique principale — c'est le **ramp function** d'une coroutine. La logique réelle est dans la fonction de reprise.

### Heap Allocation Elision (HALO)

Le standard C++20 autorise le compilateur à élider l'allocation heap du coroutine frame quand il peut prouver que la durée de vie de la coroutine est contenue dans celle de l'appelant. C'est l'optimisation **HALO** (*Heap Allocation eLision Optimization*).

En pratique, GCC applique HALO de manière limitée. En `-O2`, si la coroutine est entièrement consommée dans une boucle locale et que le compilateur peut déterminer sa durée de vie, le frame peut être placé sur la pile de l'appelant au lieu du heap. Dans ce cas, `operator new` disparaît du code généré.

En RE, l'absence de `operator new` au début d'une coroutine signifie soit HALO, soit que l'allocation a été inlinée/optimisée. Vérifiez si les deux pointeurs de fonction (resume/destroy) sont toujours présents à des offsets fixes d'un objet sur la pile.

## La transformation en machine à états

### Le principe

GCC découpe le corps de la coroutine en **segments** séparés par les points de suspension (`co_await`, `co_yield`). Chaque segment reçoit un numéro d'état. La fonction de reprise (`resume`) commence par un dispatch qui saute au bon segment selon l'index d'état courant.

Pour la coroutine `range()` :

```cpp
Generator range(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;      // point de suspension
    }
}
```

Les états sont :

| Index | Signification |  
|-------|---------------|  
| 0 | Entrée initiale (après `initial_suspend`, premier `resume`) |  
| 1 | Reprise après le `co_yield` |  
| (final) | Coroutine terminée (`final_suspend`) |

### La fonction de reprise (resume)

La fonction `range.resume` contient la machine à états complète :

```nasm
range.resume:                             ; void range.resume(frame*)
    push   rbp
    mov    rbp, rsp
    mov    rbx, rdi                       ; rbx = frame pointer

    ; ---- Dispatch sur l'index d'état ----
    mov    eax, DWORD PTR [rbx+32]        ; charger le suspension index
    cmp    eax, 0
    je     .L_state_0                     ; état 0 : première entrée
    cmp    eax, 1
    je     .L_state_1                     ; état 1 : reprise après co_yield
    ; état invalide ou terminé
    ud2                                    ; unreachable (ou jmp vers cleanup)

; =========================================
; ÉTAT 0 : Première exécution
; =========================================
.L_state_0:
    ; Initialiser i = start
    mov    eax, DWORD PTR [rbx+36]        ; frame->start
    mov    DWORD PTR [rbx+44], eax        ; frame->i = start

.L_loop_check:
    ; Vérifier i < end
    mov    eax, DWORD PTR [rbx+44]        ; frame->i
    cmp    eax, DWORD PTR [rbx+40]        ; frame->end
    jge    .L_loop_done                   ; i >= end → sortir de la boucle

    ; co_yield i → appeler promise.yield_value(i)
    mov    esi, DWORD PTR [rbx+44]        ; i
    lea    rdi, [rbx+16]                  ; &frame->promise
    call   Generator::promise_type::yield_value(int)

    ; Préparer la suspension
    mov    DWORD PTR [rbx+32], 1          ; frame->index = 1 (prochain état)
    ; Retourner → la coroutine est suspendue
    pop    rbp
    ret

; =========================================
; ÉTAT 1 : Reprise après co_yield
; =========================================
.L_state_1:
    ; Incrémenter i (la boucle continue)
    add    DWORD PTR [rbx+44], 1          ; frame->i++
    jmp    .L_loop_check                  ; retour au test de boucle

; =========================================
; FIN : Boucle terminée
; =========================================
.L_loop_done:
    ; co_return implicite (return_void)
    lea    rdi, [rbx+16]
    call   Generator::promise_type::return_void()

    ; final_suspend
    ; Marquer la coroutine comme terminée
    mov    DWORD PTR [rbx+32], -1         ; index = -1 (ou autre valeur sentinelle)
    pop    rbp
    ret
```

### Anatomie du dispatch

Le dispatch initial est le pattern le plus reconnaissable. GCC utilise soit une série de comparaisons (`cmp`/`je`), soit une **table de sauts** (`jmp [table + rax*8]`) quand le nombre d'états est suffisant :

**Dispatch par comparaisons (peu d'états) :**

```nasm
mov    eax, [rbx+offset_index]  
test   eax, eax  
je     .L_state_0  
cmp    eax, 1  
je     .L_state_1  
cmp    eax, 2  
je     .L_state_2  
jmp    .L_invalid  
```

**Dispatch par table de sauts (beaucoup d'états) :**

```nasm
mov    eax, [rbx+offset_index]  
cmp    eax, MAX_STATE  
ja     .L_invalid  
lea    rcx, [rip+.L_jump_table]  
movsxd rax, eax  
jmp    QWORD PTR [rcx+rax*8]  

.L_jump_table:
    .quad  .L_state_0
    .quad  .L_state_1
    .quad  .L_state_2
    .quad  .L_state_3
```

> 💡 **Pattern RE :** une fonction qui commence par charger un entier depuis un pointeur heap (le frame), puis fait un switch/dispatch vers différents blocs de code, est probablement la fonction de reprise d'une coroutine. Le pointeur heap est le coroutine frame, et l'entier est l'index d'état. Ce pattern est analogue au switch d'une machine à états, mais la clé est que le premier argument (`rdi`) est le frame et que le dispatch est la première chose que la fonction fait.

### La fonction de destruction (destroy)

La fonction `range.destroy` a une structure similaire au `resume`, mais au lieu d'exécuter le code de la coroutine, elle détruit les objets locaux selon l'état courant et libère le frame :

```nasm
range.destroy:                            ; void range.destroy(frame*)
    mov    rbx, rdi

    ; Dispatch sur l'index pour déterminer quels objets sont vivants
    mov    eax, DWORD PTR [rbx+32]
    ; ... selon l'état, détruire les objets locaux appropriés ...

    ; Détruire le promise object
    lea    rdi, [rbx+16]
    call   Generator::promise_type::~promise_type()

    ; Libérer le frame
    mov    rdi, rbx
    call   operator delete(void*)@plt
    ret
```

Le dispatch dans `destroy` est nécessaire car les objets vivants dans le frame dépendent du point de suspension où la coroutine a été interrompue. Si la coroutine est détruite à l'état 1, la variable `i` est vivante et les destructeurs de tous les objets locaux actifs à cet état doivent être appelés.

## `coroutine_handle` et ses opérations

### Layout de `coroutine_handle`

```
std::coroutine_handle<promise_type> (sizeof = 8 octets) :
┌──────────────────────────────────────┐
│  _M_fr_ptr (void*)                   │  offset 0    — pointeur vers le coroutine frame
└──────────────────────────────────────┘
```

Un `coroutine_handle` est un simple pointeur brut vers le frame. C'est un wrapper minimal, similaire en esprit à un `unique_ptr` sans gestion automatique de la durée de vie.

### `handle.resume()`

```cpp
handle.resume();
```

```nasm
mov    rax, QWORD PTR [rbp-0x10]     ; rax = handle._M_fr_ptr (le frame)  
mov    rdi, rax                       ; premier argument = frame  
call   QWORD PTR [rax]               ; appel indirect : frame->resume(frame)  
```

L'appel est un **appel indirect via le premier QWORD du frame**. C'est le pointeur de fonction `resume` à l'offset 0.

> 💡 **Pattern RE fondamental :** `mov rdi, rax; call [rax]` où `rax` est un pointeur heap — c'est un `coroutine_handle::resume()`. Le frame est à la fois le `this` (passé dans `rdi`) et la source du pointeur de fonction (chargé depuis l'offset 0 de `rax`). Ce pattern est similaire à un appel virtuel via vptr, sauf que le pointeur de fonction est directement dans l'objet au lieu d'être dans une table séparée.

### `handle.destroy()`

```nasm
mov    rax, QWORD PTR [rbp-0x10]     ; frame  
mov    rdi, rax  
call   QWORD PTR [rax+8]             ; appel indirect : frame->destroy(frame)  
```

Même pattern, mais à l'offset 8 au lieu de 0.

### `handle.done()`

```cpp
if (handle.done()) { /* coroutine terminée */ }
```

GCC implémente `done()` en vérifiant si le pointeur `resume` est nul ou en testant l'index d'état :

```nasm
; Variante 1 : tester le pointeur resume
mov    rax, QWORD PTR [rbp-0x10]     ; frame  
cmp    QWORD PTR [rax], 0            ; frame->resume == nullptr ?  
sete   al                             ; done = (resume == nullptr)  

; Variante 2 : tester l'index d'état
mov    rax, QWORD PTR [rbp-0x10]  
mov    eax, DWORD PTR [rax+32]       ; index d'état  
cmp    eax, -1                        ; état final ?  
sete   al  
```

La variante dépend de la version de GCC et des optimisations. Dans les deux cas, le pattern est un test sur le frame suivi d'une comparaison avec une valeur sentinelle (nullptr ou -1).

### `handle.promise()`

```cpp
auto& promise = handle.promise();
```

```nasm
mov    rax, QWORD PTR [rbp-0x10]     ; frame  
lea    rax, [rax+16]                  ; &frame->promise (offset 16, après les deux pointeurs de fn)  
```

Simple ajout de l'offset du promise object dans le frame.

## `co_await` en détail

`co_await` est l'opérateur le plus général. Il prend un *awaitable* et génère un protocole en trois étapes :

```cpp
co_await expr;
// Équivalent conceptuel :
auto&& awaitable = expr;  
if (!awaitable.await_ready()) {  
    // suspendre la coroutine
    awaitable.await_suspend(handle);
    // ... la coroutine est suspendue ici ...
    // ... reprend quand quelqu'un appelle handle.resume() ...
}
auto result = awaitable.await_resume();
```

### En assembleur

```nasm
; co_await some_awaitable
    ; 1. Appeler await_ready()
    lea    rdi, [rbp-0x28]               ; &awaitable
    call   Awaitable::await_ready()
    test   al, al
    jnz    .L_no_suspend                 ; true → pas besoin de suspendre

    ; 2. Préparer la suspension
    mov    DWORD PTR [rbx+32], 2         ; index = prochain état
    ; Construire le coroutine_handle pour le passer à await_suspend
    mov    QWORD PTR [rbp-0x30], rbx     ; handle._M_fr_ptr = frame

    ; 3. Appeler await_suspend(handle)
    lea    rdi, [rbp-0x28]               ; &awaitable
    lea    rsi, [rbp-0x30]               ; &handle
    call   Awaitable::await_suspend(std::coroutine_handle<>)

    ; 4. Retourner (la coroutine est suspendue)
    pop    rbp
    ret

.L_no_suspend:
    ; Pas de suspension — continuer directement

.L_resume_point:                         ; ← on arrive ici quand resume() est appelé
    ; 5. Appeler await_resume()
    lea    rdi, [rbp-0x28]
    call   Awaitable::await_resume()
    ; Le résultat est dans rax (ou xmm0)
```

> 💡 **Pattern RE :** la séquence `await_ready` → branchement → mise à jour de l'index → `await_suspend` → `ret` est la signature d'un point de suspension `co_await`. Le `ret` au milieu de la logique de la fonction (pas à la fin) est inhabituel pour une fonction normale et trahit la nature de coroutine.

### `co_await suspend_always` et `suspend_never`

Les types les plus courants d'awaitable sont `std::suspend_always` et `std::suspend_never`, qui sont des classes vides avec des méthodes triviales :

- `suspend_always::await_ready()` retourne toujours `false` → la coroutine se suspend toujours.  
- `suspend_never::await_ready()` retourne toujours `true` → la coroutine ne se suspend jamais.

En `-O2`, GCC inline ces appels et élimine le branchement :

```nasm
; co_await suspend_always (optimisé)
; await_ready() = false → toujours suspendre, pas de test
mov    DWORD PTR [rbx+32], N          ; mettre à jour l'index  
pop    rbp  
ret                                    ; suspendre directement  

; co_await suspend_never (optimisé)
; await_ready() = true → jamais suspendre, code éliminé
; rien n'est généré, l'exécution continue
```

## `co_yield` en détail

`co_yield expr` est du sucre syntaxique pour `co_await promise.yield_value(expr)`. GCC le transforme exactement ainsi :

```nasm
; co_yield i
    ; 1. Appeler promise.yield_value(i)
    mov    esi, DWORD PTR [rbx+44]       ; frame->i
    lea    rdi, [rbx+16]                 ; &frame->promise
    call   promise_type::yield_value(int)
    ; retourne un awaitable (suspend_always dans notre cas)

    ; 2. co_await sur le résultat (suspend_always)
    ; await_ready() = false → suspendre
    mov    DWORD PTR [rbx+32], 1         ; index = état suivant
    pop    rbp
    ret                                   ; suspendre
```

> 💡 **En RE :** un appel à une méthode nommée `yield_value` (ou une méthode sur l'objet à l'offset 16 du frame, qui est le promise) suivi d'une suspension est un `co_yield`. La valeur passée en argument est celle que la coroutine produit.

## `co_return` en détail

`co_return expr` appelle `promise.return_value(expr)` (ou `promise.return_void()` sans expression), puis exécute le `final_suspend` :

```nasm
; co_return (void)
    lea    rdi, [rbx+16]                 ; &frame->promise
    call   promise_type::return_void()

    ; final_suspend
    lea    rdi, [rbx+16]
    call   promise_type::final_suspend()
    ; Si suspend_always : marquer comme terminé
    mov    QWORD PTR [rbx], 0            ; frame->resume = nullptr (done)
    mov    DWORD PTR [rbx+32], -1        ; index = état terminal
    pop    rbp
    ret
```

La mise à zéro du pointeur `resume` (ou sa mise à `nullptr`) est la marque de terminaison. Après cela, `handle.done()` retournera `true`.

## Variables locales dans le frame

Les variables locales d'une coroutine qui vivent à travers un point de suspension sont **promouvables au frame** (*frame-promoted*). GCC analyse quelles variables sont vivantes de part et d'autre d'un `co_await`/`co_yield` et les déplace dans le coroutine frame au lieu de les garder sur la pile.

```cpp
Generator range(int start, int end) {
    for (int i = start; i < end; ++i) {  // i vit à travers co_yield
        co_yield i;
    }
}
```

La variable `i` est vivante avant le `co_yield` (elle est calculée) et après (elle est incrémentée). Elle doit donc résider dans le frame :

```
Frame de range() :
  offset 0  : resume function pointer
  offset 8  : destroy function pointer
  offset 16 : promise_type
  offset 32 : suspension index
  offset 36 : start (paramètre copié)
  offset 40 : end (paramètre copié)
  offset 44 : i (variable locale promue)
```

> 💡 **En RE :** dans la fonction de reprise, tous les accès aux variables locales passent par le frame pointer (premier argument, `rdi`, souvent sauvé dans `rbx`). Vous ne verrez quasiment pas d'accès via `rbp` (pile locale) pour les variables de la coroutine. Un accès systématique à `[rbx+offset]` pour lire et écrire les variables, combiné avec le dispatch initial, confirme que la fonction est une coroutine resume.

Les variables qui ne traversent pas de point de suspension (temporaires utilisées uniquement entre deux suspensions) peuvent rester sur la pile de la fonction de reprise. GCC optimise en ne promouvant au frame que le strict nécessaire.

## Différence entre le ramp et le resume

GCC sépare la coroutine en deux (ou trois) fonctions :

| Fonction | Rôle | Quand elle est appelée |  
|----------|------|------------------------|  
| **Ramp** (`range`) | Alloue le frame, initialise, exécute `initial_suspend` | À l'appel initial de la coroutine |  
| **Resume** (`range.resume`) | Contient la machine à états complète | À chaque `handle.resume()` |  
| **Destroy** (`range.destroy`) | Détruit les objets selon l'état, libère le frame | À `handle.destroy()` |

Le ramp est la fonction visible dans la table de symboles sous le nom original. Le resume et le destroy sont des fonctions internes dont les noms contiennent des suffixes spécifiques à GCC.

```bash
$ nm -C coroutine_binary | grep range
0000000000401200 T range(int, int)                    # ramp
0000000000401350 t range(int, int) [clone .resume]    # resume
0000000000401500 t range(int, int) [clone .destroy]   # destroy
```

Le suffixe `[clone .resume]` et `[clone .destroy]` identifie sans ambiguïté les fonctions de coroutine. Sur un binaire strippé, ces noms disparaissent, mais les patterns structurels restent.

> 💡 **En RE :** dans un binaire avec symboles, `[clone .resume]` et `[clone .destroy]` identifient instantanément les coroutines. Dans un binaire strippé, cherchez des paires de fonctions qui prennent un pointeur heap comme unique argument, où la première contient un dispatch sur un entier du heap (resume) et la seconde appelle des destructeurs puis `operator delete` (destroy).

## Reconnaître une coroutine dans un binaire strippé

Sans symboles, voici les indices à combiner :

### Indices structurels du ramp

- Appel à `operator new` en début de fonction.  
- Stockage de **deux pointeurs de fonction** aux offsets 0 et 8 du bloc alloué.  
- Initialisation d'un entier (l'index) à 0 dans le même bloc.  
- Copie des paramètres de la fonction dans le bloc heap.  
- Retour rapide (la fonction est courte : elle alloue, initialise, et retourne).

### Indices structurels du resume

- Unique argument : un pointeur vers le heap (le frame).  
- **Dispatch immédiat** sur un entier lu depuis le frame (l'index d'état) : `mov eax, [rdi+N]; cmp/je` ou table de sauts.  
- Accès systématiques aux variables via le frame pointer (`[rbx+offset]`), peu ou pas d'accès via `rbp`.  
- **`ret` multiples** au milieu de la fonction (un par point de suspension), pas seulement à la fin.  
- Mise à jour de l'index avant chaque `ret` interne (`mov DWORD [rbx+N], new_state`).  
- Absence de prologue/épilogue classique avec allocation de pile pour les variables locales (elles sont dans le frame).

### Indices structurels du destroy

- Même unique argument que le resume (pointeur frame).  
- Dispatch similaire au resume (mais plus simple).  
- Appels à des destructeurs (variables locales du frame).  
- Se termine par `operator delete` (libération du frame).

### Indices croisés

- Le ramp stocke l'adresse du resume et du destroy dans le frame. Les cross-references (XREF) dans Ghidra depuis le ramp vers le resume et le destroy confirment la relation.  
- Le resume et le destroy partagent le même layout de frame (mêmes offsets pour les mêmes données).  
- Le resume peut appeler le destroy (en cas d'exception ou de terminaison).

## Impact des optimisations

### En `-O0`

Le code est fidèle à la transformation standard. Chaque `co_await`, `co_yield`, `co_return` est visible comme une séquence distincte. Les appels aux méthodes du promise (`initial_suspend`, `yield_value`, `final_suspend`, etc.) sont explicites. Le dispatch est un `switch` simple.

### En `-O2` / `-O3`

GCC applique des optimisations significatives :

- **Inlining des méthodes du promise.** Les appels à `await_ready`, `await_suspend`, `await_resume`, `yield_value`, etc. sont inlinés. Le protocole de `co_await` peut se réduire à quelques instructions.

- **Élimination des suspensions triviales.** Si `await_ready()` est constexpr `true` (comme pour `suspend_never`), tout le code de suspension est éliminé.

- **Simplification du dispatch.** Si le nombre d'états est petit et prévisible, GCC peut remplacer le switch par des branchements directs ou même fusionner des états.

- **HALO.** L'allocation heap peut être élidée (le frame est sur la pile de l'appelant).

- **Tail call du resume.** Dans certains cas, le `handle.resume()` peut devenir un tail call, éliminant un niveau de pile.

En `-O2`, la coroutine `range` peut se simplifier au point que la machine à états est à peine reconnaissable — les états sont fusionnés, les appels au promise sont inlinés, et le code ressemble à une boucle ordinaire avec des `ret` inattendus.

> 💡 **Conseil pratique :** comme pour les autres features C++ (vtables, lambdas), analysez toujours d'abord la variante `-O0` pour comprendre la structure, puis passez à `-O2` pour voir ce que les optimisations ont changé.

## Coroutines et exceptions

Si une exception est lancée dans le corps d'une coroutine, elle n'est pas propagée à l'appelant de `resume()`. À la place, le runtime appelle `promise.unhandled_exception()`, qui peut stocker l'exception pour la relancer plus tard :

```cpp
void unhandled_exception() {
    exception_ptr_ = std::current_exception();
}
```

En assembleur, cela se manifeste par un landing pad dans la fonction resume qui, au lieu d'appeler `_Unwind_Resume`, appelle la méthode `unhandled_exception` du promise :

```nasm
; Landing pad dans range.resume
.L_exception_handler:
    mov    rdi, rax                       ; exception object
    call   __cxa_begin_catch@plt
    ; Appeler promise.unhandled_exception()
    lea    rdi, [rbx+16]                 ; &frame->promise
    call   promise_type::unhandled_exception()
    call   __cxa_end_catch@plt
    ; Aller au final_suspend
    jmp    .L_final_suspend
```

> 💡 **En RE :** un landing pad qui appelle une méthode sur l'objet à l'offset 16 du frame (le promise) au lieu de propager l'exception est le handler `unhandled_exception()` d'une coroutine.

## Résumé des patterns à reconnaître

| Pattern assembleur | Signification |  
|--------------------|---------------|  
| `operator new` + stockage de deux ptrs de fonction aux offsets 0 et 8 + init index + retour rapide | Ramp function d'une coroutine |  
| Dispatch immédiat `mov eax, [rdi+N]; cmp; je` + accès via frame pointer + `ret` multiples | Fonction resume d'une coroutine |  
| Même dispatch + destructeurs + `operator delete` | Fonction destroy d'une coroutine |  
| `mov rdi, rax; call [rax]` (appel indirect via offset 0 du frame) | `coroutine_handle::resume()` |  
| `mov rdi, rax; call [rax+8]` (appel indirect via offset 8 du frame) | `coroutine_handle::destroy()` |  
| `cmp QWORD [frame], 0` ou `cmp DWORD [frame+N], -1` | `coroutine_handle::done()` |  
| `lea rax, [frame+16]` | Accès au promise object |  
| Mise à jour d'un entier dans le frame puis `ret` | Point de suspension (mise à jour de l'index d'état) |  
| Symbole `[clone .resume]` / `[clone .destroy]` | Coroutine identifiée par les symboles |  
| Landing pad appelant une méthode sur le promise au lieu de propager | `unhandled_exception()` dans une coroutine |  
| Accès exclusif aux variables via `[rbx+offset]` (heap), pas via `rbp` (pile) | Variables locales promues au coroutine frame |

---


⏭️ [🎯 Checkpoint : reconstruire les classes du binaire `ch17-oop` à partir du désassemblage seul](/17-re-cpp-gcc/checkpoint.md)
