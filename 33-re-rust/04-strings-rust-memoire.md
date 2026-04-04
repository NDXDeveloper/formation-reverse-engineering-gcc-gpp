🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 33.4 — Strings en Rust : `&str` vs `String` en mémoire (pas de null terminator)

> 🔤 Les chaînes de caractères sont souvent le premier point d'entrée d'une analyse RE : messages d'erreur, clés de configuration, URLs, labels d'interface… En Rust, leur représentation mémoire diffère fondamentalement de celle du C. Un analyste qui applique ses réflexes C aux chaînes Rust manquera des données, en découpera d'autres incorrectement, et perdra un temps précieux. Cette section explique exactement ce qui change et comment adapter vos outils.

---

## Le modèle C : rappel

En C, une chaîne est un pointeur vers une séquence d'octets terminée par un octet nul (`\0`). C'est le modèle que tous les outils classiques (`strings`, `gdb`, `x/s`) attendent :

```
Pointeur ──▶ [ 'H' 'e' 'l' 'l' 'o' '\0' ]
              0x00  0x01 0x02 0x03 0x04 0x05
```

La longueur n'est pas stockée : elle est déduite en parcourant les octets jusqu'au `\0`. Ce modèle est simple mais fragile (buffer overflows) et interdit les `\0` dans le contenu.

---

## Le modèle Rust : fat pointers et longueur explicite

Rust utilise deux types principaux pour les chaînes, et aucun des deux ne recourt au terminateur nul.

### `&str` — la référence de chaîne (fat pointer)

Un `&str` est un **fat pointer** de 16 octets composé de deux champs :

```
┌──────────────────┬──────────────────┐
│  ptr  (8 octets) │  len  (8 octets) │
│  pointeur vers   │  longueur en     │
│  les données     │  octets (pas de  │
│  UTF-8           │  \0 à la fin)    │
└──────────────────┴──────────────────┘
        Taille totale : 16 octets
```

Les données pointées sont une séquence d'octets UTF-8 **sans terminateur nul** :

```
ptr ──▶ [ 'H' 'e' 'l' 'l' 'o' ]    ← pas de \0
         0x00 0x01 0x02 0x03 0x04

len = 5
```

Quand un `&str` est passé en argument à une fonction, il occupe **deux registres** selon la convention System V AMD64 :

```nasm
; Passage de &str en argument : occupe 2 registres consécutifs
    lea     rdi, [rip + .Lstr_data]    ; ptr → premier registre
    mov     rsi, 5                      ; len → deuxième registre
    call    some_function_taking_str
```

C'est un pattern extrêmement courant dans le code Rust : un `lea` suivi d'un `mov` d'une constante immédiate, les deux ciblant des registres d'arguments consécutifs (`rdi`/`rsi`, `rsi`/`rdx`, `rdx`/`rcx`, etc.). Ce couple `(adresse, longueur)` est la signature d'un passage de `&str`.

### `String` — la chaîne allouée sur le tas

Un `String` est une structure de 24 octets sur la pile, composée de trois champs :

```
┌──────────────────┬──────────────────┬──────────────────┐
│  ptr  (8 octets) │  len  (8 octets) │  cap  (8 octets) │
│  pointeur vers   │  longueur        │  capacité allouée│
│  le buffer heap  │  actuelle        │  sur le tas      │
└──────────────────┴──────────────────┴──────────────────┘
        Taille totale : 24 octets (sur la pile)
```

Le buffer pointé par `ptr` est alloué sur le tas (heap) via l'allocateur global. Comme pour `&str`, il ne contient **pas de `\0` terminal**. La différence avec `&str` est le champ `cap` (capacité), qui indique la taille totale du buffer alloué — `len` pouvant être inférieure à `cap` si la chaîne a été créée avec une réserve.

En mémoire sur la pile, un `String` ressemble exactement à un `Vec<u8>` — c'est d'ailleurs son implémentation interne. Si vous savez reconnaître un `Vec` (section 33.3), vous savez reconnaître un `String`.

```nasm
; String stocké sur la pile à [rbp-0x28]
; [rbp-0x28] = ptr (vers le heap)
; [rbp-0x20] = len
; [rbp-0x18] = cap

    mov     rdi, qword [rbp-0x28]     ; ptr
    mov     rsi, qword [rbp-0x20]     ; len
    ; Passe le &str implicite (ptr, len) à une fonction
    call    some_function
```

### Relation entre `&str` et `String`

Un `String` peut être converti en `&str` à coût zéro (c'est un simple emprunt du `ptr` et du `len`, sans copier `cap`). En RE, cela signifie qu'un `String` de 24 octets sur la pile peut « devenir » un `&str` de 16 octets en ignorant le troisième champ. Quand une fonction prend un `&str` en paramètre et qu'on lui passe un `String`, le compilateur extrait juste les deux premiers champs.

---

## Les littéraux de chaînes dans `.rodata`

Les chaînes littérales en Rust (`"Hello"`, `"RUST-"`, les messages d'erreur…) sont stockées dans la section `.rodata` du binaire, exactement comme en C. Mais leur référencement diffère.

### En C

```c
const char *msg = "Hello";
// .rodata : 48 65 6C 6C 6F 00      ← 6 octets, avec le \0
```

Le compilateur C stocke la chaîne avec son `\0` final. Le code la référence par un simple pointeur.

### En Rust

```rust
let msg: &str = "Hello";
// .rodata : 48 65 6C 6C 6F          ← 5 octets, SANS \0
// Ailleurs (code ou .rodata) : pointeur vers "Hello" + longueur 5
```

Le compilateur Rust stocke la chaîne **sans `\0`**. La longueur est encodée soit comme une constante immédiate dans le code (`mov rsi, 5`), soit dans une structure de référence en `.rodata`.

> ⚠️ **La nuance LLVM.** En pratique, LLVM ajoute parfois un `\0` après les littéraux de chaînes dans `.rodata` — non pas parce que Rust l'exige, mais parce que LLVM réutilise son infrastructure C et que ce `\0` supplémentaire ne coûte qu'un octet. Cela signifie que `strings` peut quand même trouver ces chaînes. Mais **ce comportement n'est pas garanti** : il dépend de la version de LLVM, des optimisations, et de la manière dont les chaînes sont fusionnées en mémoire. Ne vous y fiez pas.

### Concaténation de littéraux en `.rodata`

Quand plusieurs `&str` littéraux sont stockés dans `.rodata`, LLVM peut les placer de manière contiguë sans aucun séparateur :

```
Offset 0x1000: 52 55 53 54 2D         "RUST-"  
Offset 0x1005: 52 75 73 74 43 72 61   "RustCrackMe-v3.3"  
               63 6B 4D 65 2D 76 33
               2E 33
Offset 0x1016: 50 72 65 66 69 78 43   "PrefixCheck"
               68 65 63 6B
```

Vu par `strings`, cela pourrait produire une seule longue chaîne sans rapport avec la réalité, car les données s'enchaînent sans `\0` entre elles. C'est un piège classique du RE Rust.

---

## Impact sur l'outil `strings`

L'outil `strings` recherche les séquences d'au moins N octets (4 par défaut) composées de caractères imprimables, terminées par un `\0` ou une fin de section. Son comportement face aux chaînes Rust est imprévisible :

**Cas favorable** : LLVM a inséré un `\0` après la chaîne, ou la chaîne est suivie d'un alignement qui contient des octets nuls. `strings` la trouve correctement.

**Cas défavorable — fusion** : deux chaînes sont contiguës sans `\0` intermédiaire. `strings` les fusionne en une seule chaîne plus longue, dénuée de sens.

**Cas défavorable — troncature** : une chaîne contient des octets non-ASCII (UTF-8 multibyte : accents, emojis, caractères CJK). `strings` avec ses paramètres par défaut peut la tronquer au premier octet non imprimable.

**Cas défavorable — invisibilité** : une chaîne courte (< 4 octets) sans `\0` adjacent n'est pas détectée du tout.

### Améliorer les résultats de `strings`

Quelques options atténuent ces problèmes :

```bash
# Réduire la longueur minimale pour capturer les chaînes courtes
$ strings -n 2 crackme_rust_release | head -30

# Forcer l'encodage UTF-8 sur toute la section .rodata
$ strings -e S crackme_rust_release

# Extraire uniquement la section .rodata
$ objcopy -O binary -j .rodata crackme_rust_release rodata.bin
$ strings rodata.bin
```

Même avec ces ajustements, `strings` reste un outil de triage imparfait pour les binaires Rust. Pour une extraction fiable, il faut croiser avec l'analyse dans le désassembleur.

---

## Reconnaître les `&str` dans le désassembleur

### Pattern de passage en argument

Le pattern le plus fréquent est le passage d'un `&str` littéral comme argument de fonction. En syntaxe Intel :

```nasm
    lea     rdi, [rip + 0x1a3f]       ; Pointeur vers les données dans .rodata
    mov     esi, 0x11                  ; Longueur = 17 (0x11)
    call    core::fmt::write           ; Ou toute autre fonction prenant un &str
```

Le `lea` charge l'adresse des données (le `ptr` du fat pointer), et le `mov` charge la longueur dans le registre suivant. Ce couple est la signature d'un `&str`.

Pour retrouver le contenu de la chaîne dans Ghidra, suivez l'adresse du `lea` : elle pointe dans `.rodata` vers exactement `0x11` (17) octets de données UTF-8. Ghidra n'affichera pas forcément la chaîne automatiquement (puisqu'il n'y a pas de `\0`), mais vous pouvez la lire manuellement en sélectionnant les 17 octets à cette adresse.

> 💡 **Créer un type `&str` dans Ghidra.** Définissez une structure `RustStr` de 16 octets avec un champ `ptr` (pointer) et un champ `len` (ulong). Appliquez ce type aux emplacements sur la pile ou dans `.rodata` où vous identifiez des fat pointers. Le décompilateur affichera alors `rust_str.ptr` et `rust_str.len` au lieu de valeurs brutes.

### Pattern de chaîne en `.rodata` avec structure de référence

Parfois, le compilateur crée une structure de référence dans `.rodata` qui contient le fat pointer complet :

```nasm
; Au lieu de deux instructions lea/mov, une seule structure est chargée
    lea     rax, [rip + .Lref_str]     ; Pointe vers la structure (ptr, len) en .rodata
    mov     rdi, qword [rax]           ; ptr
    mov     rsi, qword [rax+8]         ; len
```

Dans `.rodata`, la structure ressemble à ceci (vue hexadécimale) :

```
.Lref_str:
    .quad   .Lstr_data        ; 8 octets : adresse des données
    .quad   17                ; 8 octets : longueur
```

Ce pattern est fréquent pour les chaînes passées aux macros `format!`, `println!`, `panic!` — elles utilisent des structures `fmt::Arguments` complexes qui référencent les littéraux via des fat pointers en `.rodata`.

### Pattern de comparaison de `&str`

La comparaison de deux `&str` en Rust (`==`) ne se fait pas avec `strcmp` (qui cherche un `\0`). Le compilateur émet un code qui compare d'abord les longueurs, puis le contenu :

```nasm
    ; Comparaison de deux &str : (rdi, rsi) vs (rdx, rcx)
    ; rdi = ptr1, rsi = len1, rdx = ptr2, rcx = len2

    cmp     rsi, rcx                   ; Comparer les longueurs
    jne     .not_equal                 ; Si longueurs différentes → pas égal

    ; Longueurs identiques : comparer le contenu octet par octet
    mov     rdi, rdi                   ; ptr1 (déjà en place)
    mov     rsi, rdx                   ; ptr2
    mov     rdx, rcx                   ; len (commun aux deux)
    call    memcmp                     ; ou bcmp — comparaison brute
    test    eax, eax
    jnz     .not_equal

.equal:
    ; ...

.not_equal:
    ; ...
```

Le point important : la comparaison passe par **`memcmp`** (ou `bcmp`), pas par `strcmp`. C'est un indice supplémentaire que vous analysez du Rust. Si vous voyez un `cmp` de longueurs suivi d'un `call memcmp`, c'est presque certainement une comparaison de `&str` ou de slices `&[u8]`.

> 🔑 **Implication pour le RE de notre crackme** : la vérification du préfixe `"RUST-"` dans `PrefixValidator::validate` utilise `starts_with`, qui se ramène à un `memcmp` des N premiers octets (où N est la longueur du préfixe). En posant un breakpoint sur `memcmp` dans GDB et en inspectant les arguments, vous pouvez capturer le préfixe attendu sans même lire le désassemblage.

---

## Reconnaître les `String` dans le désassembleur

### Allocation d'un `String`

La création d'un `String` passe par l'allocateur. Un `String::new()` initialise un triplet `(ptr, len, cap)` avec des valeurs nulles (pas d'allocation tant que la chaîne est vide) :

```nasm
    ; String::new() — chaîne vide, pas d'allocation
    mov     qword [rbp-0x28], 1        ; ptr = dangling pointer (convention Rust pour cap=0)
    mov     qword [rbp-0x20], 0        ; len = 0
    mov     qword [rbp-0x18], 0        ; cap = 0
```

> 💡 Le `ptr` d'un `String` ou `Vec` vide n'est pas `NULL` (`0x0`) en Rust — c'est un pointeur « dangling » intentionnel, souvent `0x1` ou l'alignement du type. C'est une différence avec le C où un vecteur vide aurait typiquement un pointeur nul. Si vous voyez un pointeur initialisé à `1` dans un triplet `(ptr, len, cap)`, c'est un `String` ou `Vec` vide.

Un `String::from("Hello")` ou un `"Hello".to_string()` déclenche une allocation sur le tas :

```nasm
    ; Allouer un buffer de 5 octets sur le heap
    mov     edi, 5                      ; taille demandée
    mov     esi, 1                      ; alignement (1 pour u8)
    call    __rust_alloc                ; Appel à l'allocateur global
    test    rax, rax                    ; Vérifier si l'allocation a réussi
    je      .alloc_failed               ; → panique OOM

    ; Copier "Hello" dans le buffer alloué
    mov     rdi, rax                    ; destination = buffer heap
    lea     rsi, [rip + .Lstr_hello]   ; source = littéral en .rodata
    mov     edx, 5                      ; 5 octets
    call    memcpy

    ; Initialiser le triplet String sur la pile
    mov     qword [rbp-0x28], rax      ; ptr = buffer heap
    mov     qword [rbp-0x20], 5        ; len = 5
    mov     qword [rbp-0x18], 5        ; cap = 5
```

Le pattern : `__rust_alloc` (ou `__rust_alloc_zeroed`) suivi de `memcpy` depuis `.rodata`, puis stockage d'un triplet `(ptr, len, cap)` sur la pile. C'est la signature d'une construction de `String` depuis un littéral.

### Destruction d'un `String` (Drop)

Quand un `String` sort du scope, Rust insère automatiquement un appel au destructeur qui libère le buffer heap :

```nasm
    ; Drop de String — libération du buffer
    mov     rdi, qword [rbp-0x28]     ; ptr
    mov     rsi, qword [rbp-0x18]     ; cap (pas len — on libère la capacité totale)
    mov     edx, 1                     ; alignement
    call    __rust_dealloc
```

L'appel à `__rust_dealloc` (wrapper autour de `free`) avec `cap` comme taille est la signature du `Drop` d'un `String` ou `Vec`. Notez que c'est `cap` qui est passé, pas `len` — on libère toute la mémoire allouée, pas seulement la portion utilisée.

---

## Les fonctions d'allocateur Rust

L'allocateur global de Rust expose un ensemble de fonctions reconnaissables dans le binaire, même strippé. Elles remplacent les `malloc`/`free` du C :

| Fonction Rust | Équivalent C | Signature assembleur |  
|---|---|---|  
| `__rust_alloc` | `malloc` | `rdi` = taille, `rsi` = alignement → `rax` = pointeur |  
| `__rust_alloc_zeroed` | `calloc` | Idem, mais mémoire initialisée à zéro |  
| `__rust_dealloc` | `free` | `rdi` = pointeur, `rsi` = taille, `rdx` = alignement |  
| `__rust_realloc` | `realloc` | `rdi` = pointeur, `rsi` = ancienne taille, `rdx` = alignement, `rcx` = nouvelle taille |

La différence clé avec le C : **l'allocateur Rust reçoit toujours la taille et l'alignement**, alors que `malloc`/`free` en C se contentent du pointeur (la taille est gérée en interne par l'allocateur). Si vous voyez un appel de type « allocation » qui prend un argument d'alignement en plus de la taille, c'est l'allocateur Rust.

Sur un binaire non strippé, ces fonctions sont nommées explicitement. Sur un binaire strippé, elles délèguent à `malloc`/`free` (via la libc) et apparaissent comme de simples wrappers autour des fonctions C.

---

## Chaînes Rust et interopérabilité C (FFI)

Quand du code Rust appelle des fonctions C (via FFI), il doit convertir ses `&str` en chaînes C terminées par `\0`. Le type `CString` de la stdlib Rust alloue un buffer avec un `\0` final :

```rust
use std::ffi::CString;  
let c_str = CString::new("Hello").unwrap();  
unsafe { libc_function(c_str.as_ptr()); }  
```

En assembleur, la création d'un `CString` est reconnaissable : allocation de `len + 1` octets, copie du contenu, écriture d'un `\0` à la fin :

```nasm
    ; CString::new("Hello")
    mov     edi, 6                      ; 5 + 1 pour le \0
    mov     esi, 1
    call    __rust_alloc
    ; ... copie de "Hello" ...
    mov     byte [rax+5], 0             ; Ajout du \0 terminal
```

Ce `mov byte [rax+len], 0` est le marqueur d'une conversion vers une chaîne C. Si vous le voyez, c'est que le code Rust s'interface avec une bibliothèque C à cet endroit — ce qui peut révéler des appels FFI intéressants.

> 💡 L'inverse existe aussi : `CStr::from_ptr` convertit un pointeur C (`*const c_char`) en une référence Rust. Ce code appelle `strlen` pour déterminer la longueur, puis construit un fat pointer `(ptr, len)`. Si vous voyez un `call strlen` dans du code Rust, c'est un point d'interopérabilité avec du C.

---

## Stratégies pratiques pour l'analyste

### Retrouver les chaînes dans un binaire Rust strippé

La méthode la plus fiable combine plusieurs approches :

**Étape 1 — Triage avec `strings`.** Lancez `strings` normalement pour capturer les chaînes qui se trouvent isolées ou suivies d'un `\0`. Cela couvre les messages de panique, les chemins source, et une partie des littéraux applicatifs.

```bash
$ strings -n 3 crackme_rust_strip > strings_output.txt
```

**Étape 2 — Chercher les patterns `lea`/`mov` dans le code.** Dans Ghidra, recherchez les instructions de type `lea reg, [rip + offset]` suivies de `mov reg, imm`. Le `imm` est la longueur. L'offset pointe vers les données dans `.rodata`. Cette recherche identifie les `&str` que `strings` a manqués.

**Étape 3 — Repérer les structures de formatage.** Les macros `format!`, `println!`, `eprintln!` créent des structures `fmt::Arguments` en `.rodata` qui contiennent des tableaux de fat pointers vers les fragments de la chaîne de formatage. Ces structures sont reconnaissables par leur taille régulière et leurs pointeurs internes vers `.rodata`.

**Étape 4 — Tracer les `memcmp` en dynamique.** Si vous avez un moyen d'exécuter le binaire (GDB, Frida), posez un breakpoint sur `memcmp` et `bcmp`. Chaque appel révèle les chaînes comparées avec leurs longueurs exactes — pas besoin de `\0`.

```bash
# Dans GDB :
(gdb) break memcmp
(gdb) run test_user RUST-0000-0000-0000-0000
# À chaque hit, inspectez rdi (ptr1), rsi (ptr2), rdx (len)
(gdb) x/s $rdi
(gdb) x/Ns $rsi    # où N = $rdx
```

### Définir des types dans Ghidra

Créez ces structures dans le Data Type Manager de Ghidra pour faciliter l'annotation :

```c
// Fat pointer &str
struct RustStr {
    char *ptr;       // Pointeur vers les données UTF-8
    ulong len;       // Longueur en octets
};

// String (chaîne allouée)
struct RustString {
    char *ptr;       // Pointeur vers le buffer heap
    ulong len;       // Longueur actuelle
    ulong cap;       // Capacité allouée
};

// Localisation de panique (vu en 33.3)
struct PanicLocation {
    struct RustStr file;  // Nom du fichier source
    uint line;            // Numéro de ligne
    uint col;             // Numéro de colonne
};
```

Appliquer ces types aux emplacements identifiés sur la pile ou dans `.rodata` transforme le pseudo-code du décompilateur : au lieu de manipulations brutes de pointeurs et d'entiers, vous verrez des accès à `str.ptr`, `str.len`, `string.cap` — ce qui rend la logique immédiatement lisible.

---

## Résumé des différences C vs Rust pour les chaînes

| Aspect | C (`char *`) | Rust `&str` | Rust `String` |  
|---|---|---|---|  
| **Représentation** | Pointeur simple (8 octets) | Fat pointer (16 octets) | Triplet (24 octets) |  
| **Terminateur** | `\0` obligatoire | Aucun | Aucun |  
| **Longueur** | Calculée par `strlen` | Stockée dans le fat pointer | Stockée dans la structure |  
| **Stockage des données** | Stack, heap ou `.rodata` | `.rodata` (littéraux) | Heap |  
| **Comparaison** | `strcmp` (cherche `\0`) | `memcmp` (longueur explicite) | `memcmp` (via déréf en `&str`) |  
| **Outil `strings`** | Fiable | Partiel — résultats incomplets | Partiel — données sur le heap |  
| **Allocation** | `malloc` / `free` | Pas d'allocation (emprunt) | `__rust_alloc` / `__rust_dealloc` |  
| **Passage en argument** | 1 registre (`rdi`) | 2 registres (`rdi` + `rsi`) | Pointeur vers la structure |

---

> **Section suivante : 33.5 — Bibliothèques embarquées et taille des binaires (tout est linké statiquement)** — nous verrons pourquoi les binaires Rust sont si volumineux et comment isoler le code applicatif du bruit de la stdlib pour concentrer votre analyse.

⏭️ [Bibliothèques embarquées et taille des binaires (tout est linké statiquement)](/33-re-rust/05-bibliotheques-taille-binaires.md)
