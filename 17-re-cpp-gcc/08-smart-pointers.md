🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.8 — Smart Pointers en assembleur : `unique_ptr` vs `shared_ptr` (comptage de références)

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Deux philosophies, deux empreintes binaires

Les smart pointers du C++ moderne (`std::unique_ptr` et `std::shared_ptr`) incarnent deux modèles de gestion de propriété radicalement différents. Cette différence se reflète directement dans le code machine que GCC génère :

- **`unique_ptr`** est une **abstraction à coût zéro** (*zero-cost abstraction*). Dans la grande majorité des cas, le compilateur le réduit à un simple pointeur brut. Il n'y a pas de structure supplémentaire en mémoire, pas d'indirection, pas de compteur. Le code généré est identique (ou quasi identique) à celui d'un `T*` avec un `delete` au bon endroit.

- **`shared_ptr`** implique un **control block** alloué sur le heap, un **compteur de références atomique**, et une mécanique de destruction conditionnelle. Le code généré est significativement plus complexe : opérations atomiques (`lock xadd`, `lock cmpxchg`), indirections via le control block, appels virtuels pour la destruction, et interactions avec `weak_ptr`.

Pour le reverse engineer, reconnaître ces patterns permet d'identifier le modèle de propriété sans accès au code source — une information de conception de haut niveau.

## `std::unique_ptr<T>` : le fantôme bienveillant

### Layout mémoire

```
std::unique_ptr<T> (avec deleter par défaut) :
┌──────────────────────────────────────┐
│  _M_t._M_p  (T*)                     │  offset 0    — pointeur vers l'objet géré
└──────────────────────────────────────┘
sizeof = 8 octets (identique à un T*)
```

C'est tout. Un seul pointeur. Grâce à l'*Empty Base Optimization* (EBO), le deleter par défaut (`std::default_delete<T>`) n'occupe aucun espace car c'est une classe vide. Le `sizeof(unique_ptr<T>)` est exactement `sizeof(T*)`.

> ⚠️ **Exception :** si un deleter custom non-vide est utilisé (`unique_ptr<T, MyDeleter>`), il est stocké dans l'objet et augmente le `sizeof`. Par exemple, un deleter contenant un pointeur de fonction fait passer le `sizeof` à 16 octets. En RE, un « unique_ptr » de 16 octets contient probablement un deleter custom.

### Construction

```cpp
auto config = std::make_unique<Config>("default", 100, true);
```

GCC génère :

```nasm
; 1. Allouer la mémoire
mov    edi, 48                        ; sizeof(Config)  
call   operator new(unsigned long)@plt  
mov    rbx, rax                       ; rbx = pointeur brut  

; 2. Construire l'objet
mov    rdi, rbx                       ; this = mémoire allouée  
lea    rsi, [rip+.LC_default]        ; "default"  
mov    edx, 100                       ; maxShapes  
mov    ecx, 1                         ; verbose = true  
call   Config::Config(std::string const&, int, bool)  

; 3. Stocker le pointeur dans le unique_ptr (sur la pile)
mov    QWORD PTR [rbp-0x20], rbx     ; unique_ptr._M_p = pointeur brut
```

L'étape 3 est un simple `mov`. Il n'y a aucune structure supplémentaire — le `unique_ptr` est littéralement le pointeur brut stocké à un emplacement sur la pile.

> 💡 **En RE :** `std::make_unique<T>(args)` se compile en `operator new(sizeof(T))` + constructeur + stockage du pointeur. C'est **indiscernable** d'un `new T(args)` stocké dans un pointeur brut. La seule différence est que le destructeur du `unique_ptr` (ou le cleanup en cas d'exception) appellera `delete` automatiquement.

### Accès à l'objet géré

```cpp
config->name;         // operator->
(*config).verbose;    // operator*
config.get();         // get()
```

Toutes ces opérations se réduisent à un déréférencement du pointeur brut :

```nasm
; config->name (operator->)
mov    rax, QWORD PTR [rbp-0x20]     ; charger le pointeur brut
; rax pointe directement vers Config, accéder aux membres normalement
lea    rsi, [rax+0x00]               ; &config->name (offset 0 dans Config)
```

Il n'y a aucune indirection supplémentaire par rapport à un `T*`. C'est la « zero-cost abstraction » en action.

### Déplacement (move semantics)

`unique_ptr` ne peut pas être copié, seulement déplacé. Un `std::move` transfère la propriété :

```cpp
auto config2 = std::move(config);
// config est maintenant nullptr
```

```nasm
; std::move sur unique_ptr = transfert du pointeur
mov    rax, QWORD PTR [rbp-0x20]     ; charger config._M_p  
mov    QWORD PTR [rbp-0x28], rax     ; config2._M_p = config._M_p  
mov    QWORD PTR [rbp-0x20], 0       ; config._M_p = nullptr  
```

Trois instructions. Le déplacement d'un `unique_ptr` est une copie du pointeur suivie d'une mise à zéro de la source. Pas d'appel de fonction, pas d'opération atomique.

> 💡 **Pattern RE :** une séquence `mov rax, [src]; mov [dst], rax; mov QWORD PTR [src], 0` est le pattern caractéristique du move d'un `unique_ptr`. Le null-out de la source est la signature qui distingue un move d'une copie.

### Test de nullité

```cpp
if (!config) { /* moved, now null */ }
```

```nasm
cmp    QWORD PTR [rbp-0x20], 0       ; config._M_p == nullptr ?  
je     .L_is_null  
```

Test direct du pointeur, identique à un `if (ptr == nullptr)`.

### Destruction

Le destructeur de `unique_ptr` appelle `delete` sur le pointeur s'il est non nul :

```nasm
; Destructeur de unique_ptr (souvent inliné)
mov    rdi, QWORD PTR [rbp-0x20]     ; charger _M_p  
test   rdi, rdi                       ; nullptr ?  
je     .L_skip_delete  
; Appeler le destructeur de l'objet puis libérer la mémoire
call   Config::~Config()  
mov    rdi, QWORD PTR [rbp-0x20]  
call   operator delete(void*)@plt  
.L_skip_delete:
```

Ou, de manière plus compacte en `-O2` (le destructeur de `Config` est inliné) :

```nasm
mov    rdi, QWORD PTR [rbp-0x20]  
test   rdi, rdi  
je     .L_done  
call   operator delete(void*)@plt     ; Config n'a pas de destructeur non trivial  
.L_done:
```

> 💡 **En RE :** le destructeur du `unique_ptr` est souvent inliné dans la fonction englobante ou dans le landing pad de cleanup. Il se manifeste par un `test rdi, rdi; je .skip; call delete; .skip:` — un test de nullité suivi d'un `delete` conditionnel. C'est indiscernable d'un `if (ptr) delete ptr;` sur un pointeur brut.

### `unique_ptr` avec tableau

```cpp
auto buffer = std::make_unique<char[]>(256);
```

```nasm
mov    edi, 256  
call   operator new[](unsigned long)@plt   ; new[] au lieu de new  
mov    QWORD PTR [rbp-0x30], rax  
```

La différence avec le `unique_ptr` scalaire est l'utilisation de `operator new[]` et `operator delete[]`. Le destructeur appellera `delete[]` au lieu de `delete`. En RE, la distinction `new`/`new[]` identifie le type de `unique_ptr` (scalaire vs tableau).

### Résumé : `unique_ptr` est invisible

En résumé, `unique_ptr` avec le deleter par défaut ne laisse **aucune trace structurelle** spécifique dans le binaire. Il est strictement équivalent à un pointeur brut en termes de code généré. La seule différence est un `delete` automatique dans le destructeur et les landing pads d'exception. En RE, vous ne pouvez pas distinguer un `unique_ptr<T>` d'un `T*` géré manuellement avec `new`/`delete` en examinant uniquement le code machine. Seuls les symboles (quand ils existent) ou les patterns d'utilisation (move systématique, absence de copie) peuvent suggérer l'utilisation de `unique_ptr`.

## `std::shared_ptr<T>` : la machinerie complète

### Layout mémoire

```
std::shared_ptr<T> (sizeof = 16 octets) :
┌──────────────────────────────────────┐
│  _M_ptr (T*)                         │  offset 0    — pointeur vers l'objet géré
├──────────────────────────────────────┤
│  _M_refcount (_Sp_counted_base*)     │  offset 8    — pointeur vers le control block
└──────────────────────────────────────┘
```

Deux pointeurs : un vers l'objet, un vers le **control block** qui gère la durée de vie. Le `sizeof` est toujours 16, quel que soit `T`.

### Le control block

Le control block est le cœur de la mécanique `shared_ptr`. Il est alloué sur le heap et contient les compteurs de références. Sa structure varie selon la manière dont le `shared_ptr` a été créé.

**Cas `std::make_shared<T>(args)` :**

`make_shared` alloue le control block et l'objet `T` dans un **seul bloc mémoire** contigu. Le type du control block est `_Sp_counted_ptr_inplace<T, Alloc>` :

```
_Sp_counted_ptr_inplace<T, Alloc> (allocation unique) :
┌──────────────────────────────────────┐
│  vptr (pointeur vtable)              │  offset 0    — vtable du control block
├──────────────────────────────────────┤
│  _M_use_count  (atomic<int>)         │  offset 8    — références fortes (shared_ptr)
├──────────────────────────────────────┤
│  _M_weak_count (atomic<int>)         │  offset 12   — références faibles + 1
├──────────────────────────────────────┤
│  _M_storage :                        │  offset 16   — (padding/alignement possible)
│    objet T (construit sur place)     │               — l'objet lui-même
└──────────────────────────────────────┘
```

L'objet `T` est inclus directement dans le control block. Un seul appel à `operator new` suffit pour le tout. C'est pourquoi `make_shared` est recommandé : une seule allocation au lieu de deux.

**Cas `shared_ptr<T>(new T(...))` :**

Quand le `shared_ptr` est construit à partir d'un pointeur brut, le control block est alloué séparément. Le type est `_Sp_counted_ptr<T*, Deleter>` :

```
_Sp_counted_ptr<T*> :
┌──────────────────────────────────────┐
│  vptr                                │  offset 0
├──────────────────────────────────────┤
│  _M_use_count  (atomic<int>)         │  offset 8
├──────────────────────────────────────┤
│  _M_weak_count (atomic<int>)         │  offset 12
├──────────────────────────────────────┤
│  _M_ptr (T*)                         │  offset 16   — pointeur vers l'objet (séparé)
└──────────────────────────────────────┘

+ objet T sur le heap (allocation séparée)
```

Dans ce cas, deux allocations ont lieu : une pour l'objet `T`, une pour le control block.

> 💡 **En RE :** avec `make_shared`, vous voyez un seul `operator new` dont la taille est `sizeof(control block) + sizeof(T)`. Avec `shared_ptr(new T)`, vous voyez deux `operator new` : un de `sizeof(T)` puis un plus petit pour le control block. La taille de l'allocation unique est un indice direct : `16 + sizeof(T)` (ou plus avec alignement).

### Le vptr du control block

Le control block possède son propre **vptr** car `_Sp_counted_base` est une classe polymorphe. Le destructeur et la méthode de déallocation sont virtuels :

```
vtable de _Sp_counted_ptr_inplace<Circle, allocator<Circle>> :
  [offset-to-top]  = 0
  [typeinfo]        = &_ZTI...
  [slot 0]          → _M_dispose()    — détruire l'objet T (appeler son destructeur)
  [slot 1]          → _M_destroy()    — détruire et désallouer le control block lui-même
  [slot 2]          → destructor
```

En RE, chaque type `T` utilisé avec `make_shared` génère sa propre classe `_Sp_counted_ptr_inplace<T>` avec sa propre vtable. Cette vtable apparaît dans `.rodata` et ses slots pointent vers des fonctions qui appellent le destructeur de `T`. C'est une source supplémentaire d'information sur les types instanciés.

> 💡 **En RE :** les vtables `_Sp_counted_ptr_inplace<T>` dans `.rodata` révèlent les types utilisés avec `make_shared`. En examinant le slot `_M_dispose` (qui appelle le destructeur de `T`), vous pouvez identifier le type `T` même sur un binaire strippé.

### Construction avec `make_shared`

```cpp
auto sharedCircle = std::make_shared<Circle>(0, 0, 5.0);
```

```nasm
; 1. Allouer le control block + l'objet en un seul bloc
mov    edi, 80                        ; sizeof(control block + Circle) avec alignement  
call   operator new(unsigned long)@plt  
mov    rbx, rax  

; 2. Initialiser le control block
lea    rdx, [rip+_ZTVNSt23_Sp_counted_ptr_inplaceI6CircleSaIS0_EEE+16]  
mov    QWORD PTR [rbx], rdx          ; vptr du control block  
mov    DWORD PTR [rbx+8], 1          ; _M_use_count = 1  
mov    DWORD PTR [rbx+12], 1         ; _M_weak_count = 1  

; 3. Construire Circle dans le storage du control block
lea    rdi, [rbx+16]                  ; this = &control_block->_M_storage
; ... passer les arguments (0, 0, 5.0) dans xmm0, xmm1, xmm2 ...
call   Circle::Circle(double, double, double)

; 4. Initialiser le shared_ptr sur la pile
lea    rax, [rbx+16]                  ; adresse de l'objet Circle  
mov    QWORD PTR [rbp-0x20], rax     ; shared_ptr._M_ptr = &objet  
mov    QWORD PTR [rbp-0x18], rbx     ; shared_ptr._M_refcount = &control_block  
```

Points clés :
- L'allocation unique (`operator new(80)`) couvre le control block et le `Circle`.  
- Les compteurs sont initialisés à 1 (un `shared_ptr` et un weak count implicite).  
- L'adresse de l'objet (`rbx+16`) et l'adresse du control block (`rbx`) sont stockées dans les deux champs du `shared_ptr`.

### Copie : incrémentation atomique

```cpp
auto copy1 = sharedCircle;   // copie → incrémente le compteur
```

```nasm
; Copier le shared_ptr : copier les deux pointeurs + incrémenter use_count
mov    rax, QWORD PTR [rbp-0x20]     ; _M_ptr  
mov    QWORD PTR [rbp-0x30], rax     ; copy._M_ptr = original._M_ptr  

mov    rax, QWORD PTR [rbp-0x18]     ; _M_refcount (control block)  
mov    QWORD PTR [rbp-0x28], rax     ; copy._M_refcount = original._M_refcount  

; Incrémenter le use_count de manière atomique
lock xadd DWORD PTR [rax+8], ecx     ; atomic increment de _M_use_count
; (ecx contient 1 préalablement chargé)
```

L'instruction **`lock xadd`** est la signature du comptage de références atomique. Le préfixe `lock` garantit l'atomicité sur les architectures multiprocesseur. L'offset `+8` depuis le control block est `_M_use_count`.

> 💡 **Pattern RE fondamental :** `lock xadd DWORD PTR [reg+8], ...` ou `lock add DWORD PTR [reg+8], 1` est la signature d'un incrément de `shared_ptr::use_count`. Le `lock` préfixe est le marqueur infaillible des opérations atomiques, et l'offset `+8` depuis un pointeur heap pointe vers `_M_use_count` dans le control block.

En `-O2`, GCC peut utiliser d'autres instructions atomiques :

```nasm
; Variante avec lock add (plus simple, quand l'ancienne valeur n'est pas nécessaire)
lock add DWORD PTR [rax+8], 1

; Variante avec lock cmpxchg (pour certaines opérations conditionnelles)
lock cmpxchg DWORD PTR [rax+8], ecx
```

### Destruction : décrémentation atomique et libération conditionnelle

Le destructeur du `shared_ptr` décrémente le compteur et libère l'objet si le compteur atteint zéro :

```nasm
; Destructeur de shared_ptr (souvent inliné)
shared_ptr_destructor:
    mov    rax, QWORD PTR [rbp-0x18]     ; control block
    test   rax, rax
    je     .L_null_ptr                     ; shared_ptr vide → rien à faire

    ; Décrémenter _M_use_count atomiquement
    mov    ecx, -1
    lock xadd DWORD PTR [rax+8], ecx     ; old_value = _M_use_count; _M_use_count--
    ; ecx contient maintenant l'ancienne valeur

    cmp    ecx, 1                          ; ancienne valeur == 1 ?
    jne    .L_still_alive                  ; non → d'autres shared_ptr existent encore

    ; use_count est tombé à 0 → détruire l'objet
    mov    rdi, rax                        ; this = control block
    mov    rax, QWORD PTR [rdi]           ; vptr du control block
    call   QWORD PTR [rax+0x10]          ; appel virtuel à _M_dispose()
                                           ; → appelle le destructeur de T

    ; Décrémenter _M_weak_count
    mov    rax, QWORD PTR [rbp-0x18]
    mov    ecx, -1
    lock xadd DWORD PTR [rax+12], ecx    ; _M_weak_count--
    cmp    ecx, 1
    jne    .L_weak_alive

    ; weak_count aussi à 0 → détruire le control block
    mov    rdi, rax
    mov    rax, QWORD PTR [rdi]
    call   QWORD PTR [rax+0x18]          ; appel virtuel à _M_destroy()
                                           ; → libère le control block (operator delete)

.L_weak_alive:
.L_still_alive:
.L_null_ptr:
```

Ce destructeur est riche en informations pour le RE :

1. **`lock xadd [rax+8], ecx` avec `ecx = -1`** : décrément atomique de `_M_use_count`. L'ancienne valeur est récupérée dans `ecx`.

2. **`cmp ecx, 1; jne`** : si l'ancienne valeur était 1, le nouveau compteur est 0 → c'est le dernier `shared_ptr`, il faut détruire l'objet.

3. **`call [rax+0x10]`** : appel virtuel à `_M_dispose()` via la vtable du control block. C'est cette fonction qui appelle le destructeur de `T`.

4. **`lock xadd [rax+12], ecx`** : décrément atomique de `_M_weak_count` (offset 12).

5. **`call [rax+0x18]`** : appel virtuel à `_M_destroy()`. Cette fonction libère le control block lui-même.

> 💡 **En RE :** le destructeur du `shared_ptr` est un des patterns les plus verbeux et les plus reconnaissables. Les deux `lock xadd` (offsets 8 et 12), les deux comparaisons avec 1, et les deux appels virtuels via la vtable du control block forment une empreinte unique. Si vous voyez ce pattern, l'objet est un `shared_ptr`.

### Move de `shared_ptr`

Le déplacement d'un `shared_ptr` **ne touche pas aux compteurs** — c'est un simple transfert de pointeurs :

```nasm
; auto sp2 = std::move(sp1);
mov    rax, QWORD PTR [rbp-0x20]     ; sp1._M_ptr  
mov    QWORD PTR [rbp-0x30], rax     ; sp2._M_ptr = sp1._M_ptr  
mov    rax, QWORD PTR [rbp-0x18]     ; sp1._M_refcount  
mov    QWORD PTR [rbp-0x28], rax     ; sp2._M_refcount = sp1._M_refcount  
mov    QWORD PTR [rbp-0x20], 0       ; sp1._M_ptr = nullptr  
mov    QWORD PTR [rbp-0x18], 0       ; sp1._M_refcount = nullptr  
```

Pas de `lock`, pas d'opération atomique. Le move est aussi rapide qu'une copie de deux pointeurs + mise à zéro de la source.

> 💡 **Pattern RE :** la copie de deux QWORD consécutifs suivie de la mise à zéro des sources (quatre `mov`, dont deux à `0`) est le move d'un `shared_ptr`. L'absence d'opérations `lock` le distingue de la copie.

### `shared_ptr::use_count()`

```cpp
std::cout << sharedCircle.use_count() << std::endl;
```

```nasm
mov    rax, QWORD PTR [rbp-0x18]     ; control block  
mov    eax, DWORD PTR [rax+8]        ; charger _M_use_count (lecture non atomique suffit)  
```

La lecture de `use_count()` est une simple lecture mémoire à l'offset 8 du control block. Pas besoin de `lock` pour la lecture sur x86-64 (les lectures alignées sont déjà atomiques sur cette architecture).

## `std::weak_ptr<T>`

### Layout mémoire

```
std::weak_ptr<T> (sizeof = 16 octets) :
┌──────────────────────────────────────┐
│  _M_ptr (T*)                         │  offset 0    — pointeur vers l'objet (peut être invalide)
├──────────────────────────────────────┤
│  _M_refcount (_Sp_counted_base*)     │  offset 8    — pointeur vers le control block
└──────────────────────────────────────┘
```

Le layout est **identique** à celui de `shared_ptr`. La différence est sémantique : la création d'un `weak_ptr` incrémente `_M_weak_count` (offset 12) au lieu de `_M_use_count` (offset 8).

### Création depuis un `shared_ptr`

```cpp
std::weak_ptr<Circle> weakRef = sharedCircle;
```

```nasm
; Copier les pointeurs
mov    rax, QWORD PTR [rbp-0x20]     ; shared._M_ptr  
mov    QWORD PTR [rbp-0x40], rax     ; weak._M_ptr  

mov    rax, QWORD PTR [rbp-0x18]     ; shared._M_refcount  
mov    QWORD PTR [rbp-0x38], rax     ; weak._M_refcount  

; Incrémenter _M_weak_count (pas use_count !)
lock add DWORD PTR [rax+12], 1       ; _M_weak_count++ (offset 12)
```

> 💡 **Pattern RE :** `lock add [rax+12], 1` (offset 12) = incrément du weak count = construction d'un `weak_ptr`. Comparez avec `lock add [rax+8], 1` (offset 8) = incrément du use count = copie d'un `shared_ptr`. L'offset dans le control block distingue les deux opérations.

### `weak_ptr::lock()`

```cpp
if (auto locked = weakRef.lock()) {
    // utiliser locked (qui est un shared_ptr)
}
```

`lock()` tente de promouvoir le `weak_ptr` en `shared_ptr`. Elle échoue si l'objet a déjà été détruit (`use_count == 0`). GCC génère une boucle compare-and-swap :

```nasm
; weak_ptr::lock() — tentative atomique de promotion
    mov    rax, QWORD PTR [rbp-0x38]     ; control block
    test   rax, rax
    je     .L_expired

.L_cas_loop:
    mov    ecx, DWORD PTR [rax+8]        ; charger use_count actuel
    test   ecx, ecx
    je     .L_expired                      ; use_count == 0 → objet détruit

    ; Tenter d'incrémenter atomiquement : CAS(use_count, ecx, ecx+1)
    lea    edx, [rcx+1]                   ; nouvelle valeur = use_count + 1
    lock cmpxchg DWORD PTR [rax+8], edx  ; si [rax+8] == ecx : [rax+8] = edx
    jne    .L_cas_loop                    ; CAS échoué (concurrent) → réessayer

    ; CAS réussi → construire le shared_ptr résultat
    mov    rax, QWORD PTR [rbp-0x40]     ; weak._M_ptr
    mov    QWORD PTR [rbp-0x50], rax     ; locked._M_ptr
    mov    rax, QWORD PTR [rbp-0x38]
    mov    QWORD PTR [rbp-0x48], rax     ; locked._M_refcount
    jmp    .L_lock_done

.L_expired:
    ; L'objet est détruit, retourner un shared_ptr vide
    mov    QWORD PTR [rbp-0x50], 0       ; locked._M_ptr = nullptr
    mov    QWORD PTR [rbp-0x48], 0       ; locked._M_refcount = nullptr

.L_lock_done:
```

> 💡 **Pattern RE :** une boucle `lock cmpxchg` sur l'offset 8 d'un control block, avec un test de zéro comme condition d'abandon, est la signature de `weak_ptr::lock()`. Le CAS (Compare-And-Swap) est nécessaire pour gérer le cas concurrent : un autre thread pourrait détruire le dernier `shared_ptr` entre la lecture et l'incrément.

### `weak_ptr::expired()`

```cpp
bool isExpired = weakRef.expired();
```

```nasm
mov    rax, QWORD PTR [rbp-0x38]     ; control block  
mov    eax, DWORD PTR [rax+8]        ; _M_use_count  
test   eax, eax  
sete   al                             ; al = (use_count == 0)  
```

Simple test de nullité du compteur de références fortes.

## Comparaison récapitulative `unique_ptr` vs `shared_ptr`

| Aspect | `unique_ptr<T>` | `shared_ptr<T>` |  
|--------|-----------------|-----------------|  
| sizeof | 8 (= `T*`) | 16 (ptr + control block) |  
| Control block | **Aucun** | Heap, avec compteurs atomiques |  
| Construction | `new` + stockage du pointeur | `new` + init control block + compteurs |  
| Copie | **Interdite** (erreur de compilation) | Copie + `lock xadd [cb+8]` |  
| Move | Copie du pointeur + null source | Copie des 2 ptrs + null source, **pas de `lock`** |  
| Accès (`->`, `*`) | Déréférencement direct | Déréférencement direct (identique) |  
| Destruction | `test ptr; je skip; delete` | `lock xadd -1; cmp 1; je dispose` |  
| Code généré typique | Identique à un `T*` | ~20 instructions de plus (atomiques) |  
| Reconnaissable en RE ? | **Non** (invisible) | **Oui** (lock, control block, vtable) |

## Impact des optimisations

### `unique_ptr` en `-O2`

En `-O2`, `unique_ptr` est quasi systématiquement réduit à un pointeur brut. Le destructeur est inliné, les move sont optimisés, et le code final est indiscernable d'une gestion manuelle de la mémoire. L'optimiseur peut même éliminer le stockage du pointeur sur la pile si la durée de vie est entièrement contenue dans des registres.

### `shared_ptr` en `-O2`

Les optimisations de `shared_ptr` sont plus limitées car les opérations atomiques sont des barrières de mémoire que le compilateur ne peut pas réordonner librement. Cependant, GCC applique quelques optimisations :

- **Élision de copies** : si un `shared_ptr` temporaire est immédiatement transféré (copy elision, NRVO), l'incrément et le décrément du compteur sont éliminés.  
- **Fusion d'opérations atomiques** : des incréments et décréments consécutifs sur le même control block peuvent être fusionnés (incrémentation de 2 au lieu de deux incrémentations de 1).  
- **Inlining du fast path** : le test `use_count == 1` dans le destructeur est souvent inliné, avec un appel de fonction uniquement pour le cas de destruction effective.

En `-O2`, le pattern du destructeur est souvent plus compact :

```nasm
; Destructeur shared_ptr optimisé
mov    rdi, QWORD PTR [rbp-0x18]     ; control block  
test   rdi, rdi  
je     .L_done  
lock sub DWORD PTR [rdi+8], 1        ; _M_use_count-- (lock sub au lieu de lock xadd)  
jne    .L_done                        ; pas zéro → terminé  
; Zero → appeler la routine de cleanup
call   std::_Sp_counted_base::_M_release()  ; gère dispose + weak_count + destroy
.L_done:
```

Le `lock sub` suivi d'un `jne` (test du zero flag) est la version optimisée du pattern `lock xadd` + `cmp 1`.

> 💡 **En RE (`-O2`) :** le pattern compact `lock sub DWORD [reg+8], 1; jne .skip; call _M_release` est la forme optimisée du destructeur de `shared_ptr`. `_M_release` est une fonction de `libstdc++` qui gère toute la logique de dispose/destroy.

## `shared_ptr` dans les conteneurs

Quand un `shared_ptr` est stocké dans un `std::vector`, chaque opération sur le vector interagit avec les compteurs de références :

- **`push_back`** : copie le `shared_ptr` → `lock xadd` pour incrémenter.  
- **`erase` / `clear`** : détruit le `shared_ptr` → `lock xadd` (ou `lock sub`) pour décrémenter.  
- **Réallocation** : move tous les éléments vers un nouveau buffer → pas de `lock` (move ne touche pas les compteurs).  
- **Destruction du vector** : décrémente le compteur de chaque élément.

En RE, un `std::vector<std::shared_ptr<T>>` se manifeste par :
- Des éléments de 16 octets dans le buffer du vector (facteur d'échelle 4 dans `sar rax, 4` pour `size()`).  
- Des opérations `lock` sur les offsets 8 et 12 des control blocks lors des insertions et suppressions.  
- Des appels à `_M_dispose` / `_M_release` dans le destructeur du vector.

## Aliasing constructor

Le constructeur d'aliasing de `shared_ptr` permet de créer un `shared_ptr` qui pointe vers un sous-objet tout en partageant la propriété avec un `shared_ptr` existant :

```cpp
struct Outer { Inner inner; };  
auto outer = std::make_shared<Outer>();  
std::shared_ptr<Inner> innerPtr(outer, &outer->inner);  // aliasing  
```

En assembleur, le constructeur d'aliasing copie le control block du `shared_ptr` source mais utilise un pointeur différent pour `_M_ptr` :

```nasm
; Aliasing constructor
lea    rax, [rbx+offset_of_inner]     ; adresse du sous-objet  
mov    QWORD PTR [rbp-0x30], rax     ; new_sp._M_ptr = &outer->inner  
mov    rax, QWORD PTR [rbp-0x18]     ; outer_sp._M_refcount  
mov    QWORD PTR [rbp-0x28], rax     ; new_sp._M_refcount = outer_sp._M_refcount  
lock add DWORD PTR [rax+8], 1        ; _M_use_count++  
```

> 💡 **En RE :** si vous voyez un `shared_ptr` dont `_M_ptr` ne pointe pas au début du bloc alloué (l'adresse ne correspond pas à `control_block + 16` pour un `make_shared`), c'est probablement un aliasing constructor. Le pointeur vise un sous-objet de l'objet réellement géré.

## Résumé des patterns à reconnaître

| Pattern assembleur | Signification |  
|--------------------|---------------|  
| Objet de 8 octets = un pointeur, `test; je; delete` | `unique_ptr` (ou pointeur brut avec delete) |  
| Copie d'un pointeur + mise à zéro de la source | Move de `unique_ptr` |  
| Objet de 16 octets = deux pointeurs | `shared_ptr` ou `weak_ptr` |  
| `lock xadd DWORD [reg+8], 1` ou `lock add DWORD [reg+8], 1` | Copie de `shared_ptr` (incrémente use_count) |  
| `lock xadd DWORD [reg+12], 1` ou `lock add DWORD [reg+12], 1` | Construction de `weak_ptr` (incrémente weak_count) |  
| `lock xadd DWORD [reg+8], -1; cmp old, 1; je dispose` | Destructeur de `shared_ptr` |  
| `lock sub DWORD [reg+8], 1; jne skip; call _M_release` | Destructeur `shared_ptr` optimisé (`-O2`) |  
| `lock cmpxchg` en boucle sur `[reg+8]` avec test de zéro | `weak_ptr::lock()` (CAS loop) |  
| `mov eax, [reg+8]; test eax, eax; sete al` | `weak_ptr::expired()` |  
| Copie de deux QWORD + zéro des deux sources | Move de `shared_ptr` (pas de lock) |  
| Allocation unique de taille `16 + sizeof(T)` + init compteurs à 1 | `make_shared<T>()` |  
| Vtable dans `.rodata` avec slots `_M_dispose` / `_M_destroy` | Control block de `shared_ptr` (identifie le type T) |  
| `operator new[]` + stockage dans objet de 8 octets | `make_unique<T[]>(N)` |

---


⏭️ [Coroutines C++20 : reconnaître le frame et le state machine pattern](/17-re-cpp-gcc/09-coroutines-cpp20.md)
