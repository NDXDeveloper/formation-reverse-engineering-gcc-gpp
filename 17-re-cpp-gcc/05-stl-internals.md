🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.5 — STL internals : `std::vector`, `std::string`, `std::map`, `std::unordered_map` en mémoire

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Pourquoi connaître le layout interne de la STL

Quand un reverse engineer rencontre un accès mémoire de la forme `mov rax, [rdi+8]` dans un binaire C++, la question immédiate est : « que représente l'offset 8 ? ». Si `rdi` pointe vers un `std::vector`, l'offset 8 correspond au pointeur de fin (`_M_finish`). Si `rdi` pointe vers un `std::string`, l'offset 8 est la taille de la chaîne (`_M_string_length`). Sans connaître les layouts internes, chaque accès mémoire est une devinette.

La bibliothèque standard C++ (STL) implémentée dans `libstdc++` (fournie avec GCC) utilise des structures de données internes stables et documentées. Leur layout ne change pas entre les versions mineures de GCC, et les changements majeurs (comme le passage à la nouvelle ABI `__cxx11` pour `std::string` dans GCC 5) sont rares et bien identifiés. Connaître ces layouts par cœur permet de décoder instantanément des patterns mémoire récurrents dans tout binaire C++ compilé avec GCC.

> ⚠️ **Ces layouts sont spécifiques à `libstdc++` (GCC).** Clang utilise `libc++` par défaut sur macOS, dont les layouts diffèrent. MSVC utilise sa propre implémentation de la STL. Ce chapitre se concentre exclusivement sur `libstdc++`, la bibliothèque standard fournie avec GCC sur Linux.

## `std::vector<T>`

`std::vector` est le conteneur le plus fréquent en C++. Son layout interne est d'une simplicité trompeuse : trois pointeurs, rien de plus.

### Layout mémoire

```
std::vector<T> (sizeof = 24 octets sur x86-64) :
┌──────────────────────────────────────┐
│  _M_start   (T*)                     │  offset 0    — début du buffer alloué
├──────────────────────────────────────┤
│  _M_finish  (T*)                     │  offset 8    — un-past-the-end (premier élément invalide)
├──────────────────────────────────────┤
│  _M_end_of_storage (T*)              │  offset 16   — fin de la capacité allouée
└──────────────────────────────────────┘
```

Les trois pointeurs définissent tout :

- **`_M_start`** : pointe vers le premier élément. Correspond à `vec.data()` et `vec.begin()`.  
- **`_M_finish`** : pointe juste après le dernier élément. Correspond à `vec.end()`.  
- **`_M_end_of_storage`** : pointe vers la fin du buffer alloué sur le heap.

Les métriques du vector se déduisent des trois pointeurs :

| Propriété | Calcul | Code C++ |  
|-----------|--------|----------|  
| Nombre d'éléments | `(_M_finish - _M_start) / sizeof(T)` | `vec.size()` |  
| Capacité | `(_M_end_of_storage - _M_start) / sizeof(T)` | `vec.capacity()` |  
| Vide ? | `_M_start == _M_finish` | `vec.empty()` |  
| Accès à l'élément i | `_M_start + i * sizeof(T)` | `vec[i]` |

Le buffer des éléments est alloué séparément sur le heap. Un vector vide a les trois pointeurs à `nullptr` (ou à une même adresse sentinelle).

### Patterns assembleur caractéristiques

**`vec.size()` :**

```nasm
mov    rax, QWORD PTR [rdi+8]        ; _M_finish  
sub    rax, QWORD PTR [rdi]          ; _M_finish - _M_start = taille en octets  
sar    rax, 3                         ; diviser par 8 si sizeof(T) == 8 (ex: pointeur)  
; rax = nombre d'éléments
```

Le `sar` (shift arithmetic right) ou `shr` sert de division par `sizeof(T)`. La valeur du décalage est un indice direct sur la taille des éléments :

| Décalage | sizeof(T) | Type probable |  
|----------|-----------|---------------|  
| 0 | 1 | `char`, `uint8_t`, `bool` |  
| 1 | 2 | `short`, `int16_t` |  
| 2 | 4 | `int`, `float`, `uint32_t` |  
| 3 | 8 | `long`, `double`, `pointer`, `size_t` |  
| 4 | 16 | Petite structure, `__int128` |  
| 5 | 32 | `std::string` (nouvelle ABI) |

> 💡 **En RE :** quand vous voyez `sub rax, rcx; sar rax, N` après le chargement de deux QWORD adjacents d'un même objet, c'est presque certainement un `vec.size()`. Le décalage N vous donne `sizeof(T) = 2^N`, ce qui peut suffire à identifier le type des éléments.

**`vec[i]` (accès indexé) :**

```nasm
mov    rax, QWORD PTR [rdi]          ; _M_start (base du buffer)  
mov    rax, QWORD PTR [rax+rcx*8]    ; _M_start[i] avec sizeof(T) == 8  
```

L'accès indexé est un simple déréférencement avec un facteur d'échelle. Le facteur (ici `*8`) est `sizeof(T)`.

**`vec.push_back(val)` :**

```nasm
mov    rax, QWORD PTR [rdi+8]        ; _M_finish  
cmp    rax, QWORD PTR [rdi+16]       ; _M_finish vs _M_end_of_storage  
je     .L_realloc                     ; si égaux, buffer plein → réallouer  

; Espace disponible : placer l'élément à _M_finish
mov    QWORD PTR [rax], rsi          ; stocker la valeur  
add    QWORD PTR [rdi+8], 8          ; _M_finish += sizeof(T)  
jmp    .L_done  

.L_realloc:
; Appeler la réallocation (complexe : nouveau buffer, copie, libération)
call   std::vector<T>::_M_realloc_insert(...)
.L_done:
```

Le pattern `cmp [rdi+8], [rdi+16]` suivi d'un `je` vers une fonction de réallocation est la signature de `push_back`. La comparaison `_M_finish == _M_end_of_storage` teste si la capacité est épuisée.

**Itération `for (auto& elem : vec)` :**

```nasm
mov    rbx, QWORD PTR [rdi]          ; rbx = _M_start (itérateur begin)  
mov    r12, QWORD PTR [rdi+8]        ; r12 = _M_finish (itérateur end)  
.L_loop:
cmp    rbx, r12                       ; begin == end ?  
je     .L_end  
; ... utiliser rbx comme pointeur vers l'élément courant ...
add    rbx, 8                         ; avancer de sizeof(T)  
jmp    .L_loop  
.L_end:
```

Le range-for se compile en une boucle entre `_M_start` et `_M_finish` avec un incrément de `sizeof(T)`.

## `std::string` (nouvelle ABI `__cxx11`, GCC ≥ 5)

Depuis GCC 5, `std::string` utilise la **nouvelle ABI** identifiable par le namespace `std::__cxx11::basic_string`. Le changement majeur par rapport à l'ancienne ABI est le passage du modèle COW (*Copy-On-Write*) à un modèle avec **Small String Optimization (SSO)**.

### Layout mémoire

```
std::__cxx11::basic_string<char> (sizeof = 32 octets sur x86-64) :
┌──────────────────────────────────────┐
│  _M_dataplus._M_p  (char*)           │  offset 0    — pointeur vers les données
├──────────────────────────────────────┤
│  _M_string_length  (size_t)          │  offset 8    — longueur de la chaîne
├──────────────────────────────────────┤
│  _M_local_buf[16]  /  _M_allocated   │  offset 16   — buffer local SSO (16 octets)
│  capacity                            │               ou capacité allouée
└──────────────────────────────────────┘
```

Les trois zones :

- **`_M_dataplus._M_p`** (offset 0) : pointeur vers les données de la chaîne. Pour les chaînes courtes (≤ 15 caractères + null terminator), ce pointeur pointe vers `_M_local_buf` à l'offset 16 du même objet. Pour les chaînes longues, il pointe vers un buffer alloué sur le heap.

- **`_M_string_length`** (offset 8) : longueur de la chaîne en octets (sans le null terminator). Correspond à `str.size()` et `str.length()`.

- **`_M_local_buf` / capacité** (offset 16) : union de 16 octets. En mode SSO (chaîne courte), ces 16 octets contiennent directement les caractères de la chaîne. En mode heap (chaîne longue), le premier `size_t` (8 octets) contient la capacité allouée.

### Small String Optimization (SSO)

Le SSO est une optimisation cruciale à reconnaître en RE. L'idée est d'éviter une allocation heap pour les petites chaînes en stockant les données directement dans l'objet `std::string` lui-même.

**Chaîne courte (≤ 15 chars) — mode SSO :**

```
Objet string sur la pile :
┌──────────────────────────────────────┐
│  _M_p = adresse de [offset 16]  ───┐ │  offset 0    — pointe vers soi-même
├──────────────────────────────────┐ │ │
│  _M_string_length = 5            │ │ │  offset 8
├──────────────────────────────────┤ │ │
│  'H' 'e' 'l' 'l' 'o' '\0' ... ←┘ │  offset 16   — données inline
│  (padding jusqu'à 16 octets)         │
└──────────────────────────────────────┘
```

Le pointeur `_M_p` pointe vers l'intérieur de l'objet lui-même (offset 16). Aucune allocation heap n'a lieu.

**Chaîne longue (> 15 chars) — mode heap :**

```
Objet string sur la pile :           Buffer sur le heap :
┌─────────────────────────────┐      ┌────────────────────────────┐
│  _M_p ──────────────────────┼──→   │  'T' 'h' 'i' 's' ' ' 'i'   │
├─────────────────────────────┤      │  's' ' ' 'a' ' ' 'l' 'o'   │
│  _M_string_length = 25      │      │  'n' 'g' ' ' 's' 't' 'r'   │
├─────────────────────────────┤      │  'i' 'n' 'g' '!' '\0'      │
│  _M_allocated_capacity = 30 │      └────────────────────────────┘
│  (8 octets, reste padding)  │
└─────────────────────────────┘
```

Le pointeur `_M_p` pointe vers le heap. La capacité allouée est stockée à l'offset 16.

### Distinguer SSO et heap en RE

Le test que le code fait pour déterminer le mode est :

```nasm
; rdi = pointeur vers std::string
mov    rax, QWORD PTR [rdi]          ; _M_p  
lea    rdx, [rdi+16]                 ; adresse de _M_local_buf  
cmp    rax, rdx                      ; _M_p == &_M_local_buf ?  
je     .L_short_string                ; oui → SSO, pas de free nécessaire  
; Non → chaîne longue, le buffer est sur le heap
mov    rdi, rax  
call   operator delete(void*)        ; libérer le buffer heap  
```

> 💡 **Pattern RE fondamental :** `lea rdx, [rdi+16]; cmp [rdi], rdx` est la signature du test SSO dans le destructeur ou dans les opérations de réallocation de `std::string`. Si vous le voyez, l'objet à `rdi` est un `std::string` de la nouvelle ABI. Ce pattern est extrêmement fréquent — vous le verrez dans tout binaire C++ compilé avec GCC ≥ 5.

### Le constructeur de `std::string`

Le constructeur initialise le pointeur SSO :

```nasm
; Construction d'un std::string à partir d'un const char*
; rdi = this (string à construire), rsi = source (const char*)
std::string::basic_string(const char*):
    lea    rax, [rdi+16]              ; adresse du buffer local
    mov    QWORD PTR [rdi], rax       ; _M_p = &_M_local_buf (mode SSO initial)
    ; ... calculer la longueur, copier les données ...
    ; si longueur > 15 : allouer sur le heap et changer _M_p
```

La première action est toujours `lea rax, [rdi+16]; mov [rdi], rax` — initialiser `_M_p` pour pointer vers le buffer local. Si la chaîne est trop longue, une allocation suit et `_M_p` est mis à jour.

### Le destructeur de `std::string`

```nasm
std::string::~basic_string():
    mov    rax, QWORD PTR [rdi]       ; _M_p
    lea    rdx, [rdi+16]              ; &_M_local_buf
    cmp    rax, rdx                   ; SSO ?
    je     .L_done                    ; oui → rien à libérer
    mov    rdi, rax                   ; non → libérer le buffer heap
    call   operator delete(void*)
.L_done:
    ret
```

Ce destructeur est l'un des plus fréquents dans tout binaire C++. Vous le verrez dans les landing pads de cleanup (section 17.4), dans les destructeurs de toute classe contenant un `std::string`, et dans les épillogues de fonctions ayant des `std::string` locaux.

### Ancienne ABI (GCC < 5, ou `-D_GLIBCXX_USE_CXX11_ABI=0`)

L'ancienne ABI utilise un modèle COW (*Copy-On-Write*) avec un layout différent :

```
Ancienne ABI std::string (sizeof = 8 octets) :
┌──────────────────────────────────────┐
│  _M_p (char*) → buffer partagé       │  offset 0    — seul champ visible
└──────────────────────────────────────┘

Buffer partagé sur le heap :
┌──────────────────────────────────────┐
│  _M_length   (size_t)                │  offset -24 depuis _M_p
├──────────────────────────────────────┤
│  _M_capacity (size_t)                │  offset -16
├──────────────────────────────────────┤
│  _M_refcount (atomic<int>)           │  offset -8   (compteur COW)
├══════════════════════════════════════╡
│  données de la chaîne + '\0'         │  offset 0 ← _M_p pointe ici
└──────────────────────────────────────┘
```

L'objet `std::string` ne fait que 8 octets (un seul pointeur). La longueur, la capacité et le compteur de références sont stockés *avant* les données de la chaîne, à des offsets négatifs depuis `_M_p`. Le compteur de références permet le COW : plusieurs `std::string` peuvent partager le même buffer, et une copie n'est faite que lors d'une modification.

> 💡 **En RE :** si un `std::string` ne fait que 8 octets et que vous voyez des accès à `[_M_p - 24]`, `[_M_p - 16]`, `[_M_p - 8]`, c'est l'ancienne ABI COW. Si l'objet fait 32 octets avec le pattern `lea rdx, [rdi+16]; cmp [rdi], rdx`, c'est la nouvelle ABI SSO. La présence de `__cxx11` dans les symboles manglés confirme la nouvelle ABI.

## `std::map<K, V>`

`std::map` est implémenté comme un **arbre rouge-noir** (*red-black tree*). C'est une structure à nœuds alloués individuellement sur le heap, ce qui la rend plus complexe à parcourir en RE qu'un `vector`.

### Layout de l'objet `std::map`

```
std::map<K, V> (sizeof = 48 octets sur x86-64) :
┌──────────────────────────────────────┐
│  _M_key_compare (comparateur)        │  offset 0    — objet foncteur (souvent vide : 0 ou 1 octet)
├──────────────────────────────────────┤
│  _M_header :                         │
│  ┌──────────────────────────────┐    │
│  │  _M_color (int/enum)         │    │  offset 8    — couleur du nœud header (RED)
│  ├──────────────────────────────┤    │
│  │  _M_parent (node*)           │    │  offset 16   — racine de l'arbre
│  ├──────────────────────────────┤    │
│  │  _M_left (node*)             │    │  offset 24   — nœud le plus à gauche (begin)
│  ├──────────────────────────────┤    │
│  │  _M_right (node*)            │    │  offset 32   — nœud le plus à droite (rbegin)
│  └──────────────────────────────┘    │
├──────────────────────────────────────┤
│  _M_node_count (size_t)              │  offset 40   — nombre d'éléments
└──────────────────────────────────────┘
```

Le `_M_header` est un nœud sentinelle qui ne contient pas de données. Il sert de point d'ancrage pour l'arbre : son `_M_parent` pointe vers la racine, son `_M_left` vers le plus petit élément (utilisé par `begin()`), et son `_M_right` vers le plus grand (utilisé par `rbegin()`).

> ⚠️ **Le sizeof de 48 octets** peut varier légèrement selon que le comparateur est un objet vide (cas de `std::less<K>`, qui bénéficie de l'*empty base optimization*) ou un objet avec état. En pratique, avec le comparateur par défaut, la taille est quasi toujours 48 octets.

### Layout d'un nœud

Chaque nœud de l'arbre est alloué individuellement sur le heap :

```
_Rb_tree_node<pair<const K, V>> :
┌──────────────────────────────────────┐
│  _M_color  (int)                     │  offset 0    — RED (0) ou BLACK (1)
├──────────────────────────────────────┤
│  _M_parent (_Rb_tree_node_base*)     │  offset 8    — nœud parent
├──────────────────────────────────────┤
│  _M_left   (_Rb_tree_node_base*)     │  offset 16   — enfant gauche
├──────────────────────────────────────┤
│  _M_right  (_Rb_tree_node_base*)     │  offset 24   — enfant droit
├══════════════════════════════════════╡
│  _M_value_field :                    │  offset 32   — début des données
│    first  (const K)                  │               — la clé
│    second (V)                        │               — la valeur
└──────────────────────────────────────┘
```

Les 32 premiers octets constituent la base du nœud (couleur, parent, gauche, droite). Les données (`std::pair<const K, V>`) commencent à l'offset 32.

### Patterns assembleur caractéristiques

**`map.size()` :**

```nasm
mov    rax, QWORD PTR [rdi+40]       ; _M_node_count
```

Un simple chargement à l'offset 40. Pas de calcul nécessaire.

**`map.find(key)` et navigation dans l'arbre :**

```nasm
; Recherche dans l'arbre rouge-noir
; rdi = map*, rsi = clé à chercher
mov    rax, QWORD PTR [rdi+16]       ; _M_parent = racine de l'arbre  
lea    rdx, [rdi+8]                   ; adresse du header (sentinelle = end())  
.L_search:
    test   rax, rax
    je     .L_not_found
    ; Comparer la clé du nœud courant avec la clé cherchée
    mov    rcx, QWORD PTR [rax+32]    ; nœud->first (la clé, offset 32)
    cmp    rcx, rsi
    jl     .L_go_right
    jg     .L_go_left
    ; Trouvé
    jmp    .L_found
.L_go_left:
    mov    rax, QWORD PTR [rax+16]    ; nœud->_M_left
    jmp    .L_search
.L_go_right:
    mov    rax, QWORD PTR [rax+24]    ; nœud->_M_right
    jmp    .L_search
```

> 💡 **Pattern RE :** une boucle qui charge alternativement `[rax+16]` (gauche) ou `[rax+24]` (droite) selon le résultat d'une comparaison, avec un test de nullité comme condition d'arrêt, est une recherche dans un arbre rouge-noir. L'accès aux données se fait à l'offset 32 du nœud.

**Itération (`for (auto& [k, v] : map)`) :**

L'itération dans un `std::map` utilise le successeur in-order. Le pattern est plus complexe que celui du `vector` car il doit naviguer dans l'arbre :

```nasm
; Avancer l'itérateur : trouver le successeur in-order
; rdi = nœud courant
mov    rax, QWORD PTR [rdi+24]       ; rax = _M_right  
test   rax, rax  
je     .L_no_right_child  
; Descendre à gauche dans le sous-arbre droit
.L_leftmost:
    mov    rdx, QWORD PTR [rax+16]    ; rdx = _M_left
    test   rdx, rdx
    je     .L_found_next
    mov    rax, rdx
    jmp    .L_leftmost

.L_no_right_child:
; Remonter tant qu'on vient de la droite
    mov    rax, QWORD PTR [rdi+8]     ; parent
    ; ... logique de remontée ...
```

Ce pattern est caractéristique de l'incrément d'un itérateur d'arbre binaire. La fonction `_Rb_tree_increment` de `libstdc++` est souvent appelée directement.

## `std::unordered_map<K, V>`

`std::unordered_map` est une **table de hachage** avec chaînage par listes. Son layout est significativement plus complexe que celui de `std::map`.

### Layout de l'objet `std::unordered_map`

En interne, `libstdc++` implémente `std::unordered_map` via `_Hashtable`. Le layout simplifié est :

```
std::unordered_map<K, V> (sizeof = 56 octets sur x86-64) :
┌──────────────────────────────────────┐
│  _M_bucket_count (size_t)            │  offset 0    — nombre de buckets
├──────────────────────────────────────┤
│  _M_buckets (__node_base**)          │  offset 8    — tableau de pointeurs vers les listes
├──────────────────────────────────────┤
│  _M_bbegin._M_node :                 │
│  ┌──────────────────────────────┐    │
│  │  _M_nxt (__node_base*)       │    │  offset 16   — tête de la liste chaînée globale
│  └──────────────────────────────┘    │
├──────────────────────────────────────┤
│  _M_element_count (size_t)           │  offset 24   — nombre d'éléments
├──────────────────────────────────────┤
│  _M_rehash_policy :                  │
│  ┌──────────────────────────────┐    │
│  │  _M_max_load_factor (float)  │    │  offset 32
│  ├──────────────────────────────┤    │
│  │  _M_next_resize (size_t)     │    │  offset 40   (peut varier avec l'alignement)
│  └──────────────────────────────┘    │
├──────────────────────────────────────┤
│  _M_single_bucket (__node_base*)     │  offset 48   — bucket unique (optimisation pour 1 bucket)
└──────────────────────────────────────┘
```

> ⚠️ **Les offsets exacts peuvent varier** selon la version de `libstdc++` et les paramètres de template (hash, equality, allocator). Le layout ci-dessus est celui des versions GCC 7 à 14 avec les paramètres par défaut. Vérifiez toujours en inspectant les constructeurs du binaire analysé.

### Layout d'un nœud

```
_Hash_node<pair<const K, V>> :
┌──────────────────────────────────────┐
│  _M_nxt (__node_base*)               │  offset 0    — prochain nœud dans la liste
├──────────────────────────────────────┤
│  _M_hash (size_t)                    │  offset 8    — hash précalculé (si cached)
├──────────────────────────────────────┤
│  _M_v :                              │  offset 16   — début des données
│    first  (const K)                  │               — la clé
│    second (V)                        │               — la valeur
└──────────────────────────────────────┘
```

> 💡 **Note :** le champ `_M_hash` n'est présent que si le hash est caché (*cached*), ce qui est le cas par défaut quand la fonction de hash n'est pas triviale. Pour des clés de type entier avec le hash par défaut, `libstdc++` peut omettre ce champ.

### Patterns assembleur caractéristiques

**`umap.size()` :**

```nasm
mov    rax, QWORD PTR [rdi+24]       ; _M_element_count
```

**Recherche par clé (`umap.find(key)` / `umap[key]`) :**

```nasm
; 1. Calculer le hash de la clé
mov    rdi, rsi                       ; clé  
call   std::hash<K>::operator()(K)    ; ou inline : instructions de hachage  
; rax = hash

; 2. Trouver le bucket
xor    edx, edx  
div    QWORD PTR [rdi]               ; hash % _M_bucket_count  
; rdx = index du bucket

; 3. Charger la tête de liste du bucket
mov    rax, QWORD PTR [rdi+8]        ; _M_buckets  
mov    rax, QWORD PTR [rax+rdx*8]    ; _M_buckets[index]  

; 4. Parcourir la liste chaînée
.L_chain:
    test   rax, rax
    je     .L_not_found
    cmp    QWORD PTR [rax+16], rsi    ; comparer la clé du nœud
    je     .L_found
    mov    rax, QWORD PTR [rax]       ; _M_nxt
    jmp    .L_chain
```

> 💡 **Pattern RE :** un calcul de hash suivi d'un `div` par une valeur chargée depuis l'objet, puis un parcours de liste chaînée (boucle avec `[rax] → rax`), est caractéristique d'un accès à `std::unordered_map`. La présence d'un appel à une fonction de hash (ou d'instructions de hachage inline comme les `imul` + shifts du hash FNV ou MurmurHash) confirme l'identification.

## `std::shared_ptr<T>` (rappel structural)

Bien que traité en détail dans la section 17.8, le layout de `std::shared_ptr` mérite un aperçu ici car il apparaît constamment dans les conteneurs STL (par exemple `std::vector<std::shared_ptr<Shape>>`).

### Layout mémoire

```
std::shared_ptr<T> (sizeof = 16 octets) :
┌──────────────────────────────────────┐
│  _M_ptr (T*)                         │  offset 0    — pointeur vers l'objet géré
├──────────────────────────────────────┤
│  _M_refcount (_Sp_counted_base*)     │  offset 8    — pointeur vers le control block
└──────────────────────────────────────┘

Control block (_Sp_counted_base) sur le heap :
┌──────────────────────────────────────┐
│  vptr                                │  offset 0    — vtable du control block
├──────────────────────────────────────┤
│  _M_use_count  (atomic<int>)         │  offset 8    — compteur de références fortes
├──────────────────────────────────────┤
│  _M_weak_count (atomic<int>)         │  offset 12   — compteur de références faibles + 1
├──────────────────────────────────────┤
│  (données spécifiques au type de     │  offset 16+
│   control block : deleter, objet     │
│   pour make_shared, etc.)            │
└──────────────────────────────────────┘
```

Un `std::vector<std::shared_ptr<Shape>>` a ses éléments de 16 octets chacun dans le buffer du vector. Le `sar rax, 4` (division par 16) dans le calcul de `size()` est la signature d'un vector de shared_ptr.

## Identifier les conteneurs STL dans un binaire strippé

Sur un binaire strippé, il n'y a plus de symboles pour identifier les types. Voici une méthode systématique pour reconnaître les conteneurs STL :

### Par la taille de l'objet

| sizeof | Conteneur probable |  
|--------|--------------------|  
| 8 | `std::string` (ancienne ABI), pointeur brut, `std::unique_ptr` |  
| 16 | `std::shared_ptr`, `std::weak_ptr`, `std::array<T,N>` (petit) |  
| 24 | `std::vector` |  
| 32 | `std::string` (nouvelle ABI `__cxx11`) |  
| 48 | `std::map`, `std::set`, `std::multimap`, `std::multiset` |  
| 56 | `std::unordered_map`, `std::unordered_set` |

Quand un constructeur alloue un objet ou initialise un membre, et que vous pouvez déterminer la taille utilisée (par l'allocation ou par les offsets des membres adjacents), cette table vous donne un point de départ.

### Par les patterns d'accès

| Pattern observé | Conteneur |  
|-----------------|-----------|  
| Trois pointeurs adjacents, `sub` + `sar` pour le size | `std::vector` |  
| `lea rdx, [rdi+16]; cmp [rdi], rdx` (test SSO) | `std::string` (`__cxx11`) |  
| Navigation arborescente gauche/droite avec couleur | `std::map` / `std::set` |  
| Hash + modulo + parcours de liste chaînée | `std::unordered_map` / `std::unordered_set` |  
| Deux pointeurs (T* + control block*), accès atomiques | `std::shared_ptr` |

### Par les fonctions de `libstdc++` appelées

Même dans un binaire strippé, les appels aux fonctions de `libstdc++.so` via la PLT restent visibles avec leurs symboles manglés. Certaines fonctions identifient sans ambiguïté le conteneur :

| Symbole PLT | Conteneur |  
|-------------|-----------|  
| `_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE...` | `std::string` |  
| `_ZSt18_Rb_tree_incrementPKSt18_Rb_tree_node_base` | `std::map` / `std::set` (incrément itérateur) |  
| `_ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base` | `std::map` / `std::set` (décrément itérateur) |  
| `_ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_` | `std::map` / `std::set` (insertion) |  
| `_ZNSt10_HashtableI...` | `std::unordered_map` / `std::unordered_set` |

```bash
# Lister les symboles STL dans la PLT
$ objdump -d -j .plt oop_O2_strip | c++filt | grep 'std::'
```

### Par les chaînes d'erreur

`libstdc++` inclut des chaînes d'erreur caractéristiques qui survivent dans le binaire :

```bash
$ strings oop_O2_strip | grep -i 'vector\|map\|string\|hash'
vector::_M_realloc_insert  
basic_string::_M_construct null not valid  
basic_string::_M_create  
```

La présence de `vector::_M_realloc_insert` dans les chaînes est une preuve directe de l'utilisation de `std::vector`.

## Reconstruction dans Ghidra

Pour reconstruire un conteneur STL dans Ghidra :

1. **Identifiez le conteneur** en utilisant les techniques ci-dessus (taille, patterns d'accès, symboles PLT).

2. **Créez la structure dans le Data Type Manager.** Par exemple, pour `std::vector<int>` :

   ```
   struct vector_int {
       int* _M_start;          // offset 0
       int* _M_finish;         // offset 8
       int* _M_end_of_storage; // offset 16
   };
   ```

3. **Appliquez le type** aux variables locales et aux membres de classes dans le décompileur. Le pseudo-code devient immédiatement plus lisible quand `param_1->field_0x8 - param_1->field_0x0` se transforme en `vec->_M_finish - vec->_M_start`.

4. **Pour les conteneurs imbriqués** (ex : `std::vector<std::string>`), créez d'abord la structure interne (`string_cxx11`), puis utilisez-la comme type d'élément dans le vector. Ghidra calculera automatiquement les offsets corrects.

> 💡 **Conseil pratique :** créez une bibliothèque de types STL dans Ghidra (un fichier `.gdt`) que vous réutiliserez d'un projet à l'autre. Les layouts de `libstdc++` ne changent pas souvent — une fois les structures créées, elles sont valables pour tous les binaires GCC de la même génération ABI.

## Résumé des layouts

| Conteneur | sizeof | Champs clés (offsets) | Signature en RE |  
|-----------|--------|----------------------|-----------------|  
| `std::vector<T>` | 24 | start(0), finish(8), end_storage(16) | 3 ptrs, `sub`+`sar` pour size |  
| `std::string` (cxx11) | 32 | ptr(0), length(8), local_buf(16) | `lea [rdi+16]; cmp [rdi], rdx` (SSO) |  
| `std::string` (old) | 8 | ptr(0), metadata à offsets négatifs | Accès `[ptr-24]`, `[ptr-8]` |  
| `std::map<K,V>` | 48 | parent(16), left(24), right(32), count(40) | Navigation arborescente G/D |  
| `std::unordered_map<K,V>` | 56 | bucket_count(0), buckets(8), count(24) | Hash + modulo + liste chaînée |  
| `std::shared_ptr<T>` | 16 | ptr(0), control_block(8) | `lock xadd` (compteur atomique) |

---


⏭️ [Templates : instanciations et explosion de symboles](/17-re-cpp-gcc/06-templates-instanciations.md)
