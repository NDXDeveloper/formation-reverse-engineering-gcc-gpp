🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.5 — La pile : prologue, épilogue et conventions d'appel System V AMD64

> 🎯 **Objectif de cette section** : comprendre le fonctionnement de la pile (*stack*) en x86-64, savoir décoder les prologues et épilogues de fonctions générés par GCC, et maîtriser les règles de la convention d'appel System V AMD64 qui gouvernent la manière dont les fonctions communiquent entre elles sous Linux.

---

## La pile x86-64 : principes fondamentaux

La pile est une zone de mémoire utilisée par le processeur pour stocker temporairement des données durant l'exécution d'un programme. Elle sert à trois usages fondamentaux :

1. **Sauvegarder l'adresse de retour** lors d'un `call` (section 3.2).  
2. **Stocker les variables locales** des fonctions.  
3. **Sauvegarder les registres** callee-saved que la fonction souhaite utiliser.

### Croissance vers le bas

Sur x86-64, la pile croît vers les **adresses basses**. Cela signifie que chaque `push` **décrémente** `rsp`, et chaque `pop` **incrémente** `rsp`. C'est contre-intuitif au premier abord, mais c'est une convention matérielle héritée des premières architectures x86.

```
Adresses hautes (ex: 0x7fffffffe000)
    ┌─────────────────────────────┐
    │                             │  ← fond de la pile (début du programme)
    │    frames des fonctions     │
    │    appelantes               │
    │         ...                 │
    │─────────────────────────────│
    │    frame de la fonction     │
    │    courante                 │
    │                             │  ← rsp pointe ici (sommet)
    └─────────────────────────────┘
Adresses basses (ex: 0x7fffffffdf00)

    « Empiler » = aller vers le bas (rsp diminue)
    « Dépiler » = aller vers le haut (rsp augmente)
```

### Le registre `rsp` comme ancre

Le registre `rsp` pointe **toujours** sur le dernier élément empilé (le sommet de la pile). Toute opération qui touche la pile passe par `rsp` :

| Opération | Effet sur `rsp` | Effet en mémoire |  
|---|---|---|  
| `push rax` | `rsp -= 8` | écrit `rax` à `[rsp]` |  
| `pop rax` | `rsp += 8` | lit `[rsp]` dans `rax` |  
| `sub rsp, 0x20` | `rsp -= 32` | réserve 32 octets |  
| `add rsp, 0x20` | `rsp += 32` | libère 32 octets |  
| `call func` | `rsp -= 8` | empile `rip` (adresse de retour) |  
| `ret` | `rsp += 8` | dépile dans `rip` |

---

## La stack frame : anatomie complète

Chaque appel de fonction crée une **stack frame** (ou *cadre de pile*) — une zone contiguë sur la pile qui contient tout ce dont la fonction a besoin. Voici le layout complet d'une stack frame en convention System V AMD64, du cas le plus général (avec frame pointer, registres sauvegardés et arguments sur la pile) :

```
Adresses hautes
┌─────────────────────────────────────┐
│           ...                       │  frame de l'appelant
│─────────────────────────────────────│
│  argument 8 (si > 6 args entiers)   │  [rbp+0x18]
│  argument 7                         │  [rbp+0x10]
│─────────────────────────────────────│
│  adresse de retour                  │  [rbp+0x08]  ← empilée par call
│─────────────────────────────────────│
│  ancien rbp (sauvegardé)            │  [rbp]       ← push rbp
│═════════════════════════════════════│  ← rbp pointe ici après mov rbp, rsp
│  registres callee-saved             │  [rbp-0x08]  ← push rbx, push r12...
│  (rbx, r12, r13, r14, r15)          │
│─────────────────────────────────────│
│                                     │
│  variables locales                  │  [rbp-0x10], [rbp-0x14]...
│  (int, tableaux, structs...)        │
│                                     │
│─────────────────────────────────────│
│  espace d'alignement (si nécessaire)│
│─────────────────────────────────────│
│  zone d'arguments pour les call     │  ← espace pour les args pile
│  sortants (si la fonction appelle   │    des fonctions appelées
│  d'autres fonctions)                │
│─────────────────────────────────────│  ← rsp pointe ici (sommet)
Adresses basses
```

En pratique, la plupart des fonctions n'ont pas d'arguments au-delà du sixième (ils passent par les registres, cf. section 3.6), et les fonctions simples n'ont que quelques variables locales. La stack frame est alors beaucoup plus compacte.

---

## Le prologue de fonction

Le prologue est la séquence d'instructions au tout début d'une fonction qui **construit** la stack frame. C'est le premier code que vous voyez quand vous naviguez vers une fonction dans un désassembleur.

### Prologue classique avec frame pointer (`-O0`)

```asm
push    rbp             ; 1. sauvegarde l'ancien base pointer  
mov     rbp, rsp        ; 2. établit le nouveau base pointer  
sub     rsp, 0x30       ; 3. réserve 48 octets pour les variables locales  
```

**Étape 1 — `push rbp`** : sauvegarde la valeur de `rbp` de la fonction appelante. Cela permet de restaurer l'ancien frame pointer en fin de fonction, ce qui « remonte » la chaîne des stack frames.

**Étape 2 — `mov rbp, rsp`** : fixe `rbp` au sommet actuel de la pile. À partir de cet instant, `rbp` sert de référence stable : les variables locales sont à des offsets négatifs (`[rbp-0x4]`, `[rbp-0x8]`…), et l'adresse de retour est à `[rbp+0x8]`.

**Étape 3 — `sub rsp, N`** : réserve de l'espace sur la pile pour les variables locales et l'alignement. La valeur de `N` vous indique la quantité d'espace local utilisé.

### Prologue avec sauvegarde de registres callee-saved

Quand la fonction utilise des registres callee-saved (`rbx`, `r12`–`r15`), ils sont sauvegardés après (ou parfois avant) l'établissement du frame pointer :

```asm
push    rbp  
mov     rbp, rsp  
push    rbx             ; sauvegarde rbx (sera utilisé dans le corps)  
push    r12             ; sauvegarde r12 (sera utilisé dans le corps)  
sub     rsp, 0x20       ; réserve l'espace local  
```

Chaque `push` supplémentaire décrémente `rsp` de 8 octets. L'espace réservé par `sub rsp, N` tient compte de ces `push` pour que le total soit aligné correctement.

> 💡 **Pour le RE** : le nombre de `push` de registres callee-saved est un indice sur la complexité de la fonction. Une fonction avec `push rbx` / `push r12` / `push r13` / `push r14` / `push r15` utilise 5 registres callee-saved — elle a probablement de nombreuses variables qui doivent survivre à travers des appels de fonctions internes.

### Prologue avec stack canary

Sur les distributions modernes, GCC compile par défaut avec `-fstack-protector-strong`, ce qui insère un *stack canary* — une valeur sentinelle placée entre les variables locales et l'adresse de retour pour détecter les buffer overflows :

```asm
push    rbp  
mov     rbp, rsp  
sub     rsp, 0x40  
mov     rax, qword [fs:0x28]       ; lit le canary depuis le TLS  
mov     qword [rbp-0x8], rax       ; place le canary sur la pile  
xor     eax, eax                    ; efface rax (bonne pratique sécurité)  
```

L'accès `[fs:0x28]` est la signature visuelle immédiate du stack canary. Quand vous le voyez dans un prologue, vous savez que la fonction est protégée et que l'épilogue contiendra la vérification correspondante.

### Prologue sans frame pointer (`-O1` et au-delà)

Avec les optimisations activées, GCC utilise par défaut `-fomit-frame-pointer`. Le prologue est plus court — pas de `push rbp` ni de `mov rbp, rsp` :

```asm
sub     rsp, 0x28           ; réserve l'espace directement
; ou simplement :
push    rbx                  ; sauvegarde un callee-saved, ajuste rsp de 8  
sub     rsp, 0x20            ; réserve le reste  
```

Sans frame pointer, **tous les accès aux variables locales utilisent des offsets relatifs à `rsp`** au lieu de `rbp` :

```asm
; Avec frame pointer (-O0)
mov     eax, dword [rbp-0x4]       ; variable locale via rbp (stable)

; Sans frame pointer (-O1+)
mov     eax, dword [rsp+0x1c]      ; même variable, mais via rsp
```

La difficulté en RE est que les offsets par rapport à `rsp` **changent** à chaque `push`, `pop` ou `sub rsp` au sein de la fonction, alors que les offsets par rapport à `rbp` restent fixes. Ghidra et IDA gèrent cette complexité automatiquement en traçant la valeur de `rsp` tout au long de la fonction (*stack pointer tracking*), mais en analyse manuelle avec `objdump`, c'est plus exigeant.

---

## L'épilogue de fonction

L'épilogue **défait** ce que le prologue a construit, dans l'ordre inverse, puis retourne à l'appelant.

### Épilogue classique avec frame pointer

```asm
; === vérification du canary (si -fstack-protector) ===
mov     rax, qword [rbp-0x8]       ; relit le canary depuis la pile  
xor     rax, qword [fs:0x28]       ; compare avec la valeur originale  
jne     .stack_chk_fail             ; si différent → corruption détectée !  

; === restauration ===
add     rsp, 0x20                   ; libère l'espace local (ou implicite via leave)  
pop     r12                         ; restaure r12 (ordre inverse du prologue)  
pop     rbx                         ; restaure rbx  
pop     rbp                         ; restaure l'ancien base pointer  
ret                                  ; dépile l'adresse de retour, saute  
```

Ou, plus fréquemment, avec l'instruction `leave` :

```asm
leave                               ; équivalent de : mov rsp, rbp / pop rbp  
ret  
```

`leave` est un raccourci qui combine la restauration de `rsp` et le `pop rbp` en une seule instruction. C'est la forme que GCC utilise le plus souvent en `-O0`.

### Épilogue sans frame pointer

```asm
add     rsp, 0x20           ; libère l'espace local  
pop     rbx                  ; restaure rbx  
ret  
```

Sans frame pointer, pas de `leave` ni de `pop rbp` — il suffit de remonter `rsp` et de restaurer les registres callee-saved.

### La valeur de retour

Avant l'épilogue, la fonction place sa valeur de retour dans `rax` (ou `eax` pour un `int`) :

```asm
mov     eax, dword [rbp-0x4]    ; charge le résultat
; ... épilogue ...
ret                               ; l'appelant récupère la valeur dans eax/rax
```

Pour les valeurs flottantes (`float`, `double`), le retour se fait via `xmm0`. Pour les structures de petite taille (≤ 16 octets), GCC peut les retourner dans `rax` + `rdx`. Les structures plus grandes sont retournées via un pointeur caché passé en premier argument (cf. plus bas dans cette section).

---

## Alignement de la pile sur 16 octets

La convention System V AMD64 impose une règle d'alignement stricte :

> **Au moment d'un `call`, `rsp` doit être aligné sur 16 octets.**

Plus précisément, juste *avant* l'exécution du `call`, `rsp` doit être un multiple de 16. Comme `call` empile 8 octets (l'adresse de retour), cela signifie qu'au point d'entrée de la fonction appelée, `rsp` vaut un **multiple de 16 + 8** (c'est-à-dire `rsp mod 16 == 8`).

Le prologue doit donc s'assurer que `rsp` redevient un multiple de 16 avant tout `call` sortant. GCC gère cela automatiquement en ajustant la taille du `sub rsp, N` ou en insérant un `push` supplémentaire.

### Pourquoi 16 octets ?

Les instructions SSE/AVX exigent que certains accès mémoire soient alignés sur 16 octets (instructions `movaps`, `movdqa`…). Comme la libc et d'autres bibliothèques utilisent ces instructions, violer l'alignement provoque un crash immédiat (`SIGSEGV` ou `SIGBUS`).

### Ce que ça donne en pratique

```asm
; Fonction qui appelle printf — prologue ajusté pour l'alignement
push    rbp                 ; rsp -= 8 → rsp mod 16 == 0  
mov     rbp, rsp  
sub     rsp, 0x10           ; 16 octets → rsp reste aligné sur 16  
; ... préparation des arguments ...
call    printf@plt          ; rsp est bien aligné avant le call
```

Si le nombre de `push` + la taille du `sub rsp` ne donne pas un total multiple de 16, GCC insère un padding :

```asm
push    rbp                 ; rsp -= 8  
push    rbx                 ; rsp -= 8 → rsp mod 16 == 0 (2 × 8 = 16)  
sub     rsp, 0x18           ; 24 octets → rsp -= 24 → total = 8+8+24 = 40  
                            ; 40 mod 16 = 8... pas aligné !
                            ; GCC aurait plutôt fait sub rsp, 0x20 (32 octets)
                            ; pour atteindre un total de 48 (multiple de 16)
```

> 💡 **Pour le RE** : si vous voyez un `sub rsp` dont la valeur semble plus grande que nécessaire pour les variables locales, le surplus est probablement du **padding d'alignement**. Ne cherchez pas de variable locale cachée dans ces octets excédentaires.

---

## La convention System V AMD64 en détail

La convention d'appel (*calling convention*) est l'ensemble des règles qui définissent comment les fonctions se passent des arguments, renvoient des résultats, et partagent les registres. Sous Linux x86-64, c'est la **System V AMD64 ABI** qui s'applique à tout code compilé par GCC (et Clang, Rust, Go via le linker GNU…).

### Passage des arguments entiers et pointeurs

Les **six premiers arguments** entiers ou pointeurs sont passés via les registres, dans cet ordre :

| Argument | Registre |  
|---|---|  
| 1er | `rdi` |  
| 2e | `rsi` |  
| 3e | `rdx` |  
| 4e | `rcx` |  
| 5e | `r8` |  
| 6e | `r9` |

À partir du **7e argument**, les valeurs sont empilées sur la pile, de droite à gauche (le 7e argument est au sommet).

```c
// Code C
long result = func(10, 20, 30, 40, 50, 60, 70, 80);
```

```asm
; Préparation des arguments
push    80                  ; 8e argument → pile  
push    70                  ; 7e argument → pile  
mov     r9d, 60             ; 6e argument → r9  
mov     r8d, 50             ; 5e argument → r8  
mov     ecx, 40             ; 4e argument → rcx  
mov     edx, 30             ; 3e argument → rdx  
mov     esi, 20             ; 2e argument → rsi  
mov     edi, 10             ; 1er argument → rdi  
call    func  
add     rsp, 16             ; nettoie les 2 arguments empilés (2 × 8 octets)  
```

> 💡 **Pour le RE** : repérer les `mov edi, ...` / `mov esi, ...` / `mov edx, ...` juste avant un `call` vous donne directement les arguments de la fonction. C'est un réflexe fondamental pour comprendre ce que fait un appel.

### Passage des arguments flottants

Les arguments `float` et `double` utilisent les registres SSE :

| Argument flottant | Registre |  
|---|---|  
| 1er | `xmm0` |  
| 2e | `xmm1` |  
| 3e | `xmm2` |  
| 4e | `xmm3` |  
| 5e | `xmm4` |  
| 6e | `xmm5` |  
| 7e | `xmm6` |  
| 8e | `xmm7` |

Les arguments entiers et flottants sont comptés **indépendamment**. Un appel `func(int a, double b, int c, double d)` utilise `rdi` pour `a`, `xmm0` pour `b`, `rsi` pour `c`, et `xmm1` pour `d`.

### Fonctions variadiques (`printf`, `scanf`…)

Pour les fonctions à nombre variable d'arguments (déclarées avec `...`), la convention impose que **`al`** (l'octet bas de `rax`) contienne le **nombre de registres SSE utilisés** pour passer des arguments flottants. C'est un pattern très reconnaissable :

```asm
; printf("x = %d, y = %f\n", x, y)
lea     rdi, [rip+0x1234]      ; 1er arg : format string  
mov     esi, ebx                ; 2e arg : x (entier → rsi)  
movsd   xmm0, qword [rbp-0x10] ; 3e arg : y (double → xmm0)  
mov     eax, 1                  ; 1 registre SSE utilisé (xmm0)  
call    printf@plt  
```

Si aucun argument flottant n'est passé, vous verrez :

```asm
xor     eax, eax           ; 0 registres SSE utilisés  
call    printf@plt  
```

> 💡 **Pour le RE** : un `xor eax, eax` ou `mov eax, N` juste avant un `call` à une fonction connue comme `printf`, `scanf`, `sprintf`… est le compteur d'arguments SSE, pas un argument réel. C'est spécifique aux fonctions variadiques — les fonctions à prototype fixe ne l'utilisent pas.

### Valeur de retour

| Type de retour | Registre(s) |  
|---|---|  
| Entier, pointeur (≤ 64 bits) | `rax` |  
| Entier 128 bits | `rax` (bas) + `rdx` (haut) |  
| `float`, `double` | `xmm0` |  
| Petite struct (≤ 16 octets) | `rax` et/ou `rdx` et/ou `xmm0`/`xmm1` |  
| Grande struct (> 16 octets) | Pointeur caché en 1er argument (`rdi`) |

Le dernier cas est le plus surprenant : quand une fonction retourne une structure de plus de 16 octets, l'appelant réserve l'espace sur sa propre pile et passe un pointeur vers cet espace comme **premier argument caché** dans `rdi`. Tous les autres arguments sont décalés d'un rang. La fonction écrit le résultat à l'adresse pointée par `rdi` et retourne cette même adresse dans `rax`.

```c
// Code C
typedef struct { char data[64]; } BigStruct;  
BigStruct make_big(int x);  

BigStruct s = make_big(42);
```

```asm
; L'appelant
lea     rdi, [rbp-0x50]     ; rdi = &s (espace réservé sur la pile de l'appelant)  
mov     esi, 42              ; esi = x (décalé au 2e argument !)  
call    make_big  
; rax pointe vers [rbp-0x50], qui contient maintenant le résultat
```

> ⚠️ **Pour le RE** : si vous analysez une fonction et que le premier argument (`rdi`) semble être un pointeur vers lequel la fonction écrit abondamment sans jamais le lire, c'est probablement le **pointeur de retour caché** pour une grosse structure. Cela décale tous les arguments apparents d'un rang — l'argument que vous pensiez être le premier est en fait le deuxième.

### Registres callee-saved vs caller-saved (récapitulatif)

| Catégorie | Registres | Qui sauvegarde ? |  
|---|---|---|  
| **Caller-saved** (volatils) | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | L'**appelant** doit sauvegarder ces registres avant un `call` s'il veut conserver leur valeur |  
| **Callee-saved** (non volatils) | `rbx`, `rbp`, `r12`–`r15` | La **fonction appelée** doit les restaurer avant de retourner |  
| **Pointeur de pile** | `rsp` | Restauré implicitement par l'épilogue |

En termes concrets pour le RE :

- Si une fonction utilise `rbx` dans son corps, elle **doit** faire `push rbx` dans le prologue et `pop rbx` dans l'épilogue — sinon elle viole l'ABI et corrompt l'état de l'appelant.  
- Si une fonction a besoin qu'une valeur survive à un `call` interne, elle la stocke dans un registre callee-saved (ce qui coûte un `push`/`pop`) ou sur la pile (ce qui coûte un `mov` mémoire).

---

## La Red Zone — 128 octets gratuits sous `rsp`

La System V AMD64 ABI définit une **red zone** : une zone de 128 octets **sous** `rsp` (aux adresses `rsp-1` à `rsp-128`) que la fonction peut utiliser librement **sans décrémenter `rsp`**.

```asm
; Fonction feuille utilisant la red zone (pas de sub rsp)
mov     dword [rsp-0x4], edi      ; stocke le 1er argument sous rsp  
mov     dword [rsp-0x8], esi      ; stocke le 2e argument  
; ... calculs utilisant [rsp-0x4] et [rsp-0x8] ...
ret
```

La red zone n'est exploitable que par les **fonctions feuilles** — celles qui n'appellent aucune autre fonction. Un `call` écraserait la red zone en empilant l'adresse de retour. De même, un signal ou une interruption peut écraser la red zone (sauf que le noyau Linux respecte cette convention pour les signaux).

Avantage : pas besoin de `sub rsp` / `add rsp`, ce qui économise des instructions et rend le prologue/épilogue inexistant. GCC exploite la red zone en `-O1` et au-delà pour les petites fonctions feuilles.

> 💡 **Pour le RE** : si vous voyez une fonction qui accède à `[rsp-0x...]` sans avoir fait de `sub rsp` préalable, ce n'est pas un bug — c'est l'utilisation de la red zone. Ces fonctions n'ont souvent **aucun prologue ni épilogue** visible, juste du code utile suivi d'un `ret`.

> ⚠️ **Attention** : la red zone n'existe **pas** dans la convention Windows x64 (Microsoft ABI), ni en mode noyau Linux. Si vous analysez un driver ou un module kernel, les accès sous `rsp` sans `sub rsp` sont effectivement suspects.

---

## Variantes de prologues selon le contexte

En pratique, vous ne verrez pas toujours le prologue « scolaire » `push rbp` / `mov rbp, rsp` / `sub rsp, N`. Voici les variantes courantes et comment les reconnaître :

### Fonction feuille triviale (red zone, pas de prologue)

```asm
; int add(int a, int b) { return a + b; }
add:
    lea     eax, [rdi+rsi]
    ret
```

Aucun prologue, aucun épilogue. La fonction est si simple qu'elle n'a besoin ni de pile ni de sauvegarde.

### Fonction feuille avec variables locales (red zone)

```asm
; Utilise la red zone — pas de sub rsp
process:
    mov     dword [rsp-0x4], edi
    mov     dword [rsp-0x8], esi
    ; ... calculs ...
    mov     eax, dword [rsp-0x4]
    ret
```

### Fonction avec frame pointer (`-O0` ou `-fno-omit-frame-pointer`)

```asm
func:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 0x20
    ; ... corps avec accès [rbp-X] ...
    leave
    ret
```

C'est la forme « classique » enseignée dans les cours d'architecture. Les variables locales sont à des offsets négatifs fixes par rapport à `rbp`.

### Fonction sans frame pointer (`-O1` et au-delà)

```asm
func:
    sub     rsp, 0x28
    ; ... corps avec accès [rsp+X] ...
    add     rsp, 0x28
    ret
```

Ou avec sauvegarde de callee-saved :

```asm
func:
    push    rbx
    push    r12
    sub     rsp, 0x18
    ; ... corps ...
    add     rsp, 0x18
    pop     r12
    pop     rbx
    ret
```

### Prologue de `main()` — alignement spécial

Le prologue de `main()` contient souvent un alignement explicite de `rsp` sur 16 octets, car `main` est appelé par le *C runtime* (`__libc_start_main`) et GCC veut garantir l'alignement indépendamment du contexte :

```asm
main:
    push    rbp
    mov     rbp, rsp
    and     rsp, 0xfffffffffffffff0   ; aligne rsp sur 16 octets
    sub     rsp, 0x20
    ; ...
```

L'instruction `and rsp, -16` (qui s'affiche `and rsp, 0xfffffffffffffff0`) est la signature du prologue de `main()`.

---

## Reconstruire la stack frame en RE

L'un des exercices les plus courants en reverse engineering est de **reconstruire le layout de la stack frame** — c'est-à-dire de déterminer quelles variables locales sont stockées à quels offsets, et de leur attribuer un nom et un type.

### Méthode avec frame pointer (`rbp`)

Quand le frame pointer est présent, la tâche est relativement simple. Chaque offset négatif par rapport à `rbp` est une variable locale :

```asm
mov     dword [rbp-0x4], 0          ; variable à rbp-4, 4 octets → int  
mov     qword [rbp-0x10], rdi       ; variable à rbp-16, 8 octets → pointeur  
mov     byte [rbp-0x11], 0x41       ; variable à rbp-17, 1 octet → char  
lea     rax, [rbp-0x50]             ; adresse de rbp-80 → tableau/buffer  
```

On peut en déduire :

```
Offset      Taille    Type probable    Nom suggéré  
rbp-0x04    4         int              var_4 (ou compteur, index…)  
rbp-0x10    8         pointeur         var_10 (ou ptr, buf…)  
rbp-0x11    1         char             var_11  
rbp-0x50    64        char[64]         buffer (car lea prend l'adresse)  
```

### Méthode sans frame pointer (`rsp`)

Sans frame pointer, les offsets sont relatifs à `rsp` et varient au fil de la fonction. Ghidra et IDA normalisent ces offsets automatiquement. En analyse manuelle, il faut tracer la valeur de `rsp` instruction par instruction :

```asm
sub     rsp, 0x28                    ; rsp -= 40  
mov     dword [rsp+0x1c], edi        ; → offset réel depuis base = 0x1c  
mov     dword [rsp+0x18], esi        ; → offset réel = 0x18  
; ...
call    some_func                     ; rsp ne change pas (caller clean-up)
; ...
mov     eax, dword [rsp+0x1c]        ; relit la même variable  
add     rsp, 0x28                    ; rsp += 40  
ret  
```

> 💡 **Pour le RE** : dans Ghidra, la fenêtre « Decompiler » effectue ce travail automatiquement et vous présente des variables locales nommées avec leurs types inférés. C'est l'un des grands avantages d'un outil de décompilation par rapport au listing assembleur brut.

---

## Visualiser la chaîne des stack frames

Les stack frames forment une **liste chaînée** via les valeurs sauvegardées de `rbp`. Chaque frame contient, à l'adresse `[rbp]`, la valeur de `rbp` de la frame précédente, et à `[rbp+8]`, l'adresse de retour :

```
   ┌──────────────────────────────────┐
   │         frame de main()          │
   │  [rbp_main+0x08] = adr retour    │
   │  [rbp_main]      = rbp initial   │◄── rbp_main
   │  variables locales de main       │
   ├──────────────────────────────────┤
   │         frame de func_a()        │
   │  [rbp_a+0x08] = adr retour       │
   │  [rbp_a]      = rbp_main ────────│──► pointe vers rbp_main
   │  variables locales de func_a     │◄── rbp_a
   ├──────────────────────────────────┤
   │         frame de func_b()        │
   │  [rbp_b+0x08] = adr retour       │
   │  [rbp_b]      = rbp_a ───────────│──► pointe vers rbp_a
   │  variables locales de func_b     │◄── rbp_b (rbp courant)
   └──────────────────────────────────┘
                                        ← rsp (sommet de pile)
```

C'est cette chaîne que GDB parcourt quand vous tapez `backtrace` — il suit les pointeurs `rbp` de frame en frame pour reconstruire la pile d'appels. Sans frame pointer (code optimisé), GDB a besoin des informations DWARF (`.eh_frame`) pour dérouler la pile, ce qui est plus lent et parfois moins fiable sur un binaire strippé.

---

## Ce qu'il faut retenir pour la suite

1. **La pile croît vers le bas** — `push` décrémente `rsp`, `pop` l'incrémente.  
2. **Le prologue** construit la stack frame (`push rbp` / `mov rbp, rsp` / `sub rsp, N`), **l'épilogue** la détruit (`leave` / `ret` ou `add rsp, N` / `pop` / `ret`).  
3. **Avec frame pointer** (`-O0`) : variables locales à `[rbp-X]`, arguments pile à `[rbp+0x10]` et au-delà. **Sans frame pointer** (`-O1+`) : tout est relatif à `rsp`, les offsets varient.  
4. **L'alignement sur 16 octets** est obligatoire avant chaque `call` — c'est la cause des `sub rsp` apparemment surdimensionnés et des `push` « inutiles ».  
5. **La red zone** (128 octets sous `rsp`) permet aux fonctions feuilles d'éviter tout prologue.  
6. **System V AMD64 ABI** : 6 arguments entiers par registres (`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`), retour dans `rax`, flottants via `xmm0`–`xmm7`. Les arguments au-delà du 6e vont sur la pile.  
7. **Pour les fonctions variadiques**, `al` contient le nombre de registres SSE utilisés — c'est le `xor eax, eax` ou `mov eax, N` mystérieux avant les `call printf`.  
8. **Les grosses structures** sont retournées via un pointeur caché en `rdi`, ce qui décale tous les arguments d'un rang.

---


⏭️ [Passage des paramètres : `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` puis la pile](/03-assembleur-x86-64/06-passage-parametres.md)
