🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 16.4 — Tail call optimization et son impact sur la pile

> **Fichier source associé** : `binaries/ch16-optimisations/tail_call.c`  
> **Compilation** : `make s16_4` (produit 6 variantes dans `build/`)

---

## Introduction

Les sections précédentes ont montré comment GCC transforme le *contenu* des fonctions (inlining, déroulage, vectorisation). La tail call optimization (TCO) transforme quelque chose de plus fondamental : la **relation entre les fonctions** — la manière dont elles s'appellent et dont la pile d'exécution évolue.

Le principe est simple : quand le dernier acte d'une fonction avant de retourner est un appel à une autre fonction (ou à elle-même), le `call` + `ret` peut être remplacé par un simple `jmp`. Le frame de pile de l'appelant est réutilisé par l'appelé, au lieu d'en empiler un nouveau.

Les conséquences pour le reverse engineer sont profondes :

- Une **récursion** peut se transformer en **boucle** — il n'y a plus de `call` récursif visible dans le binaire.  
- La **backtrace GDB** est tronquée : les frames intermédiaires n'existent plus sur la pile, donc `bt` ne les montre pas.  
- Deux fonctions en **récursion mutuelle** (A appelle B qui appelle A) peuvent devenir une seule boucle — les deux fonctions semblent fusionnées.  
- Un `call [rax]` (appel indirect) peut devenir un `jmp [rax]`, ce qui change la signature du pattern en RE.

Cette section explore chaque scénario en détail, avec le désassemblage commenté et les pièges à éviter lors de l'analyse.

---

## Qu'est-ce qu'un tail call ?

Un appel est en **position terminale** (*tail position*) quand c'est la toute dernière opération avant le `return`. Aucun calcul, aucune transformation, aucune opération ne s'applique au résultat de l'appel — il est retourné tel quel à l'appelant.

### Ce qui EST un tail call

```c
return other_function(x, y);      /* Tail call — le résultat est retourné directement */  
return self(n - 1, acc * n);      /* Tail recursion — appel récursif en position terminale */  
```

### Ce qui N'EST PAS un tail call

```c
return n * factorial(n - 1);      /* PAS un tail call — multiplication APRÈS l'appel */  
return 1 + process(data);         /* PAS un tail call — addition APRÈS l'appel */  

int result = compute(x);  
log(result);                      /* PAS un tail call — du code s'exécute après compute() */  
return result;  
```

La distinction est subtile mais cruciale. Dans le premier cas non-tail, le résultat de `factorial(n - 1)` doit revenir dans le frame courant pour être multiplié par `n`. Le frame ne peut donc pas être libéré avant le retour de l'appel récursif. Dans le cas d'un vrai tail call, le frame n'a plus aucune utilité après l'appel — il peut être recyclé.

### Ce que fait le compilateur

En `-O0`, la TCO est **toujours désactivée**. Chaque appel génère un `call` avec un nouveau frame.

À partir de `-O1`, GCC commence à appliquer la TCO. En `-O2`, elle couvre la plupart des cas. En `-O3`, le comportement est identique à `-O2` pour la TCO (il n'y a pas de « TCO plus agressive » — soit elle s'applique, soit elle ne s'applique pas).

La transformation concrète dans le binaire :

```
AVANT (sans TCO) :              APRÈS (avec TCO) :

call target_function            ; Mise à jour des paramètres dans les registres
; ... ret de target revient ici  jmp target_function
ret                              ; Le ret de target revient directement
                                 ; à NOTRE appelant
```

Le `call` + `ret` est remplacé par un `jmp`. Puisque `jmp` ne pousse pas d'adresse de retour sur la pile, le `ret` de la fonction cible retournera directement à l'appelant de la fonction courante — un niveau au-dessus dans la chaîne d'appels.

---

## Scénario 1 — Récursion terminale : la factorielle avec accumulateur

La récursion terminale (*tail recursion*) est le cas le plus emblématique de la TCO. C'est aussi le plus transformateur : une récursion de profondeur N est convertie en une boucle plate, sans aucune croissance de pile.

```c
static long factorial_tail(int n, long accumulator)
{
    if (n <= 1)
        return accumulator;
    return factorial_tail(n - 1, accumulator * n);
}

long factorial(int n)
{
    return factorial_tail(n, 1);
}
```

L'appel récursif `return factorial_tail(n - 1, accumulator * n)` est en position terminale : rien ne se passe entre le retour de l'appel récursif et le `return` de la fonction courante. L'accumulateur transporte le résultat intermédiaire « vers le bas » au lieu de le reconstruire « en remontant ».

### En `-O0` — récursion classique

```asm
factorial_tail:
    push   rbp
    mov    rbp, rsp
    sub    rsp, 0x10
    mov    DWORD PTR [rbp-0x4], edi      ; n sur la pile
    mov    QWORD PTR [rbp-0x10], rsi     ; accumulator sur la pile

    ; if (n <= 1)
    cmp    DWORD PTR [rbp-0x4], 1
    jg     .L_recurse
    mov    rax, QWORD PTR [rbp-0x10]     ; return accumulator
    jmp    .L_end

.L_recurse:
    ; accumulator * n
    mov    eax, DWORD PTR [rbp-0x4]
    cdqe
    imul   rax, QWORD PTR [rbp-0x10]     ; rax = accumulator * n

    ; factorial_tail(n - 1, accumulator * n)
    mov    rsi, rax                       ; 2e param = accumulator * n
    mov    edi, DWORD PTR [rbp-0x4]
    sub    edi, 1                         ; 1er param = n - 1
    call   factorial_tail                 ; APPEL RÉCURSIF

.L_end:
    leave
    ret
```

Chaque appel récursif empile un nouveau frame. Pour `factorial(20)`, il y a 20 frames sur la pile, chacun occupant ~32 octets. La backtrace GDB montre les 20 niveaux :

```
(gdb) bt
#0  factorial_tail (n=1, accumulator=2432902008176640000) at tail_call.c:6
#1  factorial_tail (n=2, accumulator=1216451004088320000) at tail_call.c:8
#2  factorial_tail (n=3, accumulator=405483668029440000) at tail_call.c:8
...
#19 factorial_tail (n=20, accumulator=1) at tail_call.c:8
#20 factorial (n=20) at tail_call.c:13
#21 main (argc=1, argv=0x7fffffffde18) at tail_call.c:150
```

### En `-O2` — transformation en boucle

GCC reconnaît le pattern de récursion terminale et le transforme en boucle. Le `call factorial_tail` est remplacé par un `jmp` vers le début de la fonction (ou, plus souvent, par une restructuration complète en boucle `while`) :

```asm
factorial_tail:
    ; Pas de prologue complet — leaf function ou prologue minimal
    mov    rax, rsi                       ; rax = accumulator
    cmp    edi, 1
    jle    .L_done                        ; if (n <= 1) return accumulator

.L_loop:
    ; Corps de la "boucle" — anciennement le cas récursif
    movsxd rdx, edi                      ; rdx = n (étendu 64 bits)
    imul   rax, rdx                      ; accumulator *= n
    sub    edi, 1                         ; n--
    cmp    edi, 1
    jg     .L_loop                        ; while (n > 1)

.L_done:
    ret                                   ; return accumulator (dans rax)
```

La transformation est radicale :

- Le `call factorial_tail` a **complètement disparu**. Il n'y a plus d'appel récursif — c'est une boucle `jg .L_loop`.  
- Les paramètres `n` et `accumulator` sont mis à jour **dans les registres** (`edi` et `rax`) au lieu d'être passés via un nouveau frame.  
- La pile ne croît pas du tout. Pour `factorial(20)`, il y a **un seul frame** sur la pile, quelle que soit la profondeur.  
- La backtrace GDB ne montre qu'un seul niveau :

```
(gdb) bt
#0  factorial_tail (n=1, accumulator=2432902008176640000) at tail_call.c:6
#1  factorial (n=20) at tail_call.c:13
#2  main (argc=1, argv=0x7fffffffde18) at tail_call.c:150
```

Les 18 frames intermédiaires ont disparu. C'est l'effet le plus visible de la TCO pour un analyste qui débogue avec GDB.

### Comment le reconnaître en RE

Le pattern de la tail recursion optimisée est facile à confondre avec une simple boucle `while` écrite dans le source. Les deux produisent exactement le même assembleur. Voici les indices pour distinguer une tail recursion transformée :

1. **Les registres de paramètres sont réinitialisés avant le `jmp`/`jg` en arrière.** Dans une boucle classique, le compteur est incrémenté. Dans une tail recursion transformée, les paramètres sont recalculés selon la signature de la fonction récursive : `edi` reçoit `n - 1`, `rax` reçoit `accumulator * n`.

2. **La structure des paramètres correspond à une signature de fonction.** Si le « corps de boucle » manipule exactement les registres `edi` et `rsi` (les deux premiers paramètres System V), et que la condition de sortie de boucle correspond à un cas de base récursif (`n <= 1`), il est probable que le source était récursif.

3. **En pratique, la distinction est souvent sans importance pour le RE.** Que le source ait été une récursion terminale ou une boucle while, le comportement est identique. L'important est de comprendre l'algorithme, pas la forme syntaxique originale.

---

## Scénario 2 — Récursion NON terminale : le `call` survit

```c
static long factorial_notail(int n)
{
    if (n <= 1)
        return 1;
    return n * factorial_notail(n - 1);
}
```

La multiplication par `n` s'effectue **après** le retour de l'appel récursif. Le résultat de `factorial_notail(n - 1)` doit revenir dans le frame courant pour être multiplié. Le frame ne peut pas être libéré — la TCO est impossible.

### En `-O2`

```asm
factorial_notail:
    push   rbx                           ; sauvegarde de registre (callee-saved)
    mov    ebx, edi                      ; ebx = n (sauvegardé pour après le call)

    cmp    edi, 1
    jle    .L_base

    lea    edi, [rbx-1]                  ; edi = n - 1
    call   factorial_notail              ; APPEL RÉCURSIF (call, pas jmp !)

    movsxd rbx, ebx
    imul   rax, rbx                      ; rax = n * résultat_récursif
    pop    rbx
    ret

.L_base:
    mov    eax, 1                        ; return 1
    pop    rbx
    ret
```

Le `call factorial_notail` est toujours présent. GCC ne peut pas le transformer en `jmp`, car il doit récupérer le résultat dans `rax` pour le multiplier par `n` (conservé dans `ebx`, un registre callee-saved).

Notez le `push rbx` au début : GCC a besoin de préserver `n` pendant l'appel récursif, donc il utilise `ebx` (callee-saved) et le sauvegarde sur la pile. C'est un pattern classique de récursion non terminale.

### La leçon pour le RE

Si vous voyez un `call` vers la fonction elle-même suivi d'opérations sur le résultat (`imul rax, rbx` ici), c'est une **récursion non terminale**. La présence d'un `push` de registre callee-saved (`rbx`, `r12`–`r15`) qui préserve un paramètre pour l'utiliser après le `call` est le signe que la TCO n'a pas pu s'appliquer.

À l'inverse, si vous voyez un `jmp` vers le début de la fonction (ou une boucle) avec seulement des mises à jour de registres de paramètres, c'est une récursion terminale optimisée — ou une boucle native.

---

## Scénario 3 — Récursion mutuelle : `is_even` / `is_odd`

La TCO ne se limite pas à l'auto-récursion. Elle s'applique aussi quand une fonction appelle **une autre fonction** en position terminale. Le cas le plus intéressant est la récursion mutuelle.

```c
static int is_even(unsigned int n)
{
    if (n == 0) return 1;
    return is_odd(n - 1);
}

static int is_odd(unsigned int n)
{
    if (n == 0) return 0;
    return is_even(n - 1);
}
```

Chaque fonction appelle l'autre en position terminale. Sans TCO, un appel `is_even(1000000)` empilerait un million de frames et causerait un stack overflow.

### En `-O0` — stack overflow garanti

```asm
is_even:
    push   rbp
    mov    rbp, rsp
    mov    DWORD PTR [rbp-0x4], edi
    cmp    DWORD PTR [rbp-0x4], 0
    jne    .L_not_zero
    mov    eax, 1
    jmp    .L_end
.L_not_zero:
    mov    edi, DWORD PTR [rbp-0x4]
    sub    edi, 1
    call   is_odd                         ; call (pas jmp)
.L_end:
    pop    rbp
    ret
```

Pour `is_even(1000000)`, la pile croît de ~32 octets par appel × 1 000 000 = ~32 Mo. Sur un système avec une limite de pile de 8 Mo (valeur par défaut de `ulimit -s`), c'est un stack overflow assuré.

### En `-O2` — fusion en une seule boucle

GCC applique la TCO aux deux fonctions. Mieux encore, il peut les **fusionner** en une seule boucle puisque les deux fonctions ont la même structure (décrémenter `n` et alterner) :

```asm
is_even:
    ; GCC fusionne is_even et is_odd
    ; Le résultat est : is_even(n) = (n % 2 == 0)
    ; Mais sans cette simplification, la version TCO ressemble à :

    test   edi, edi
    je     .L_return_1

.L_loop:
    sub    edi, 1                        ; n--
    je     .L_return_0                   ; if (n == 0) return 0  (is_odd base case)
    sub    edi, 1                        ; n--
    jne    .L_loop                       ; if (n != 0) continue  (is_even base case)

.L_return_1:
    mov    eax, 1
    ret

.L_return_0:
    xor    eax, eax
    ret
```

Les deux `call` mutuels ont été remplacés par une boucle qui décrémente `n` de 2 à chaque tour (une fois pour `is_odd`, une fois pour `is_even`). Les deux fonctions ont été **fusionnées** en une seule boucle.

Dans certaines versions de GCC, le compilateur va encore plus loin et reconnaît que toute cette récursion mutuelle est équivalente à `n % 2 == 0` :

```asm
is_even:
    ; Version ultra-optimisée
    mov    eax, edi
    and    eax, 1                        ; n & 1
    xor    eax, 1                        ; inverse le bit (even = !odd)
    ret
```

Trois instructions. Un million de niveaux de récursion réduits à un `and` + `xor`.

### Ce que le RE doit retenir

La récursion mutuelle optimisée par TCO produit des patterns surprenants : deux fonctions qui dans le source s'appellent alternativement peuvent se retrouver fusionnées en une seule boucle dans le binaire. Si le symbole `is_odd` a disparu et que `is_even` contient une boucle qui décrémente de 2, l'analyste peut ne pas suspecter qu'il y avait deux fonctions à l'origine.

Un indice : si la boucle a **deux** conditions de sortie distinctes avec des valeurs de retour différentes (`return 0` et `return 1`), et que le compteur est décrémenté deux fois par tour, c'est potentiellement une récursion mutuelle fusionnée.

---

## Scénario 4 — Ce qui bloque la TCO

La TCO échoue dès que le frame de la fonction courante doit rester actif après l'appel. Voici les cas les plus courants.

### Travail après l'appel

```c
static int sum_recursive(int n)
{
    if (n <= 0) return 0;
    return n + sum_recursive(n - 1);   /* n + ... empêche la TCO */
}
```

L'addition de `n` après le retour de l'appel récursif bloque la TCO. Le frame doit rester actif pour stocker `n`. En `-O2`, GCC peut cependant transformer cette récursion en itération par d'autres moyens (accumulation implicite), mais ce n'est pas de la TCO à proprement parler.

Comparons avec la version tail-recursive :

```c
static int sum_tail(int n, int acc)
{
    if (n <= 0) return acc;
    return sum_tail(n - 1, acc + n);   /* Tail call valide */
}
```

### En `-O2` — comparaison côte à côte

`sum_recursive` :

```asm
sum_recursive:
    ; GCC peut transformer en itération par analyse de récurrence,
    ; mais le mécanisme est différent de la TCO.
    ; Version typique : accumulation dans un registre.
    test   edi, edi
    jle    .L_zero
    xor    eax, eax                      ; acc = 0
.L_loop:
    add    eax, edi                      ; acc += n
    sub    edi, 1                        ; n--
    jnz    .L_loop                       ; while (n != 0)
    ret
.L_zero:
    xor    eax, eax
    ret
```

`sum_tail` :

```asm
sum_tail:
    ; TCO directe — les paramètres sont mis à jour et on boucle
    test   edi, edi
    jle    .L_return_acc
.L_loop:
    add    esi, edi                      ; acc += n
    sub    edi, 1                        ; n--
    jnz    .L_loop
.L_return_acc:
    mov    eax, esi                      ; return acc
    ret
```

Les deux versions produisent un assembleur presque identique. La différence est dans le **mécanisme** : `sum_tail` est transformée par TCO (remplacement du `call` par un `jmp`), tandis que `sum_recursive` est transformée par une passe d'analyse de récurrence différente. Le résultat pour le RE est le même — une boucle — mais le fait que GCC puisse « sauver » `sum_recursive` ne signifie pas que la TCO s'est appliquée.

### Buffer local sur la pile

```c
static int process_with_buffer(int n, int threshold)
{
    int buffer[64];
    buffer[n % 64] = n;

    if (n <= 0) return buffer[0];

    if (n > threshold)
        return process_with_buffer(n - 2, threshold);
    else
        return process_with_buffer(n - 1, threshold);
}
```

Malgré les appels en position terminale, le tableau `buffer[64]` alloué sur la pile empêche la TCO dans certains cas. Le frame doit rester actif pour que `buffer` existe tant que la fonction est en cours d'exécution. Si le compilateur ne peut pas prouver que `buffer` n'est plus utilisé après le point d'appel récursif, il conserve le frame.

En pratique, GCC est parfois assez intelligent pour réaliser que `buffer` n'est pas accédé après l'appel récursif et applique tout de même la TCO. Mais c'est un cas limite dont le résultat dépend de la version de GCC et du niveau d'optimisation.

### Ce que le RE doit retenir

Si vous voyez un `call` récursif dans un binaire `-O2` (au lieu du `jmp` attendu pour une TCO), cherchez ce qui bloque l'optimisation : une opération après le `call` (multiplication, addition, transformation du résultat), un tableau local, ou un `push`/`pop` de registre callee-saved qui indique que la fonction a besoin de restaurer un état après le retour de l'appel.

---

## Scénario 5 — Tail call vers une autre fonction

La TCO ne s'applique pas seulement à l'auto-récursion. Tout appel en position terminale vers **n'importe quelle fonction** peut être transformé en `jmp`.

```c
typedef long (*transform_fn)(long, int);

static long apply_transform(transform_fn fn, long initial, int steps)
{
    return fn(initial, steps);   /* Tail call indirect */
}
```

### En `-O0`

```asm
apply_transform:
    push   rbp
    mov    rbp, rsp
    mov    QWORD PTR [rbp-0x8], rdi      ; fn
    mov    QWORD PTR [rbp-0x10], rsi     ; initial
    mov    DWORD PTR [rbp-0x14], edx     ; steps

    mov    esi, DWORD PTR [rbp-0x14]     ; 2e param = steps
    mov    rdi, QWORD PTR [rbp-0x10]     ; 1er param = initial
    mov    rax, QWORD PTR [rbp-0x8]      ; rax = fn
    call   rax                           ; call indirect
    pop    rbp
    ret
```

`call rax` + `ret` : l'appel indirect classique.

### En `-O2`

```asm
apply_transform:
    ; Réarrangement des paramètres pour le tail call
    mov    rax, rdi                      ; rax = fn
    mov    rdi, rsi                      ; 1er param = initial (était en rsi)
    mov    esi, edx                      ; 2e param = steps (était en edx)
    jmp    rax                           ; TAIL CALL — jmp au lieu de call
```

Le `call rax` est devenu un `jmp rax`. Il n'y a pas de `push rbp`, pas de `ret` — la fonction `apply_transform` ne crée même pas de frame de pile. Elle se contente de réarranger les paramètres (puisque `fn`, `initial` et `steps` ne sont pas dans les registres attendus par la fonction cible) puis saute directement.

Le `ret` de la fonction cible (`double_it` ou `triple_it`) retournera directement à l'appelant de `apply_transform` — c'est-à-dire `main()`.

### Le pattern `jmp` en fin de fonction

C'est un pattern extrêmement courant dans les binaires optimisés, même en dehors de la récursion. Chaque fois qu'une fonction se termine par `return autre_fonction(...)`, GCC émet un `jmp` au lieu d'un `call` + `ret`. Voici des exemples fréquents :

```asm
; Wrapper qui ajoute un paramètre
wrapper:
    mov    edx, 42                       ; ajoute un 3e paramètre
    jmp    real_function                 ; tail call direct

; Dispatch — l'appelant choisit la cible
dispatch:
    cmp    edi, 1
    je     .L_handler_a
    cmp    edi, 2
    je     .L_handler_b
    jmp    default_handler               ; tail call vers le default

.L_handler_a:
    jmp    handler_a                     ; tail call
.L_handler_b:
    jmp    handler_b                     ; tail call
```

### Ce que le RE doit retenir

Un `jmp` vers une **autre fonction** (pas un label local) en fin de fonction est un tail call. Ne le confondez pas avec un `jmp` vers un label interne (qui est un branchement conditionnel ou une boucle). La distinction :

- `jmp .L_loop` → branchement local (label dans la même fonction)  
- `jmp printf@plt` → tail call vers `printf`  
- `jmp rax` → tail call indirect

Si vous voyez une « fonction » dans Ghidra qui ne se termine pas par `ret` mais par un `jmp` vers une autre fonction, c'est un tail call. Ghidra gère généralement bien ce cas et montre la relation dans le décompilateur, mais certains désassembleurs moins sophistiqués peuvent mal délimiter les frontières de fonctions.

---

## Scénario 6 — Exemples algorithmiques classiques

Deux algorithmes bien connus sont naturellement tail-recursive et illustrent parfaitement la TCO en pratique.

### GCD (algorithme d'Euclide)

```c
static int gcd(int a, int b)
{
    if (b == 0) return a;
    return gcd(b, a % b);
}
```

#### En `-O2`

```asm
gcd:
    test   esi, esi
    je     .L_done                       ; if (b == 0) return a

.L_loop:
    mov    eax, edi                      ; eax = a
    cdq                                  ; extension de signe
    idiv   esi                           ; eax = a/b, edx = a%b
    mov    edi, esi                      ; a = b
    mov    esi, edx                      ; b = a % b
    test   esi, esi
    jne    .L_loop                       ; while (b != 0)

.L_done:
    mov    eax, edi                      ; return a
    ret
```

Le `call gcd` récursif a été remplacé par la boucle `jne .L_loop`. Les paramètres sont mis à jour dans `edi` (a = b) et `esi` (b = a % b) — exactement les registres de paramètres System V, puisque ce sont les mêmes que ceux de l'appel récursif d'origine.

Le `idiv` est conservé ici car le modulo dépend du runtime (pas d'une constante), donc GCC ne peut pas appliquer le magic number.

### Exponentiation modulaire rapide

```c
static long mod_pow_tail(long base, int exp, long mod, long acc)
{
    if (exp == 0) return acc;
    if (exp % 2 == 1)
        return mod_pow_tail(base, exp - 1, mod, (acc * base) % mod);
    else
        return mod_pow_tail((base * base) % mod, exp / 2, mod, acc);
}
```

#### En `-O2`

```asm
mod_pow_tail:
    ; rdi = base, esi = exp, rdx = mod, rcx = acc
    test   esi, esi
    je     .L_return_acc

.L_loop:
    test   esi, 1                        ; exp impair ?
    jz     .L_even

    ; Cas impair : acc = (acc * base) % mod
    mov    rax, rcx
    imul   rax, rdi                      ; rax = acc * base
    cqo
    idiv   rdx                           ; rdx:rax / mod → reste dans rdx
    ; ... (rcx = rdx, le nouveau acc)
    sub    esi, 1                        ; exp--

    test   esi, esi
    je     .L_return_acc
    jmp    .L_even_entry

.L_even:
.L_even_entry:
    ; Cas pair : base = (base * base) % mod, exp /= 2
    mov    rax, rdi
    imul   rax, rdi                      ; rax = base * base
    cqo
    idiv   rdx                           ; reste dans rdx
    ; ... (rdi = rdx, la nouvelle base)
    shr    esi, 1                        ; exp /= 2

    test   esi, esi
    jne    .L_loop

.L_return_acc:
    mov    rax, rcx                      ; return acc
    ret
```

Les deux branches de l'appel récursif (pair et impair) sont fusionnées en une seule boucle avec un `test esi, 1` pour choisir le chemin. Les quatre paramètres (`base`, `exp`, `mod`, `acc`) sont mis à jour dans les registres à chaque tour de boucle. L'algorithme d'exponentiation rapide O(log n), écrit de manière récursive dans le source, devient une boucle itérative dans le binaire.

---

## Impact sur le débogage avec GDB

La TCO a un impact direct sur l'expérience de débogage, et cette connaissance est utile au reverse engineer qui utilise GDB pour l'analyse dynamique.

### Backtrace tronquée

C'est l'effet le plus visible. Quand la TCO s'applique, les frames intermédiaires n'existent plus sur la pile :

```
# En -O0 (pas de TCO) :
(gdb) bt
#0  factorial_tail (n=1, accumulator=2432902008176640000)
#1  factorial_tail (n=2, accumulator=1216451004088320000)
#2  factorial_tail (n=3, accumulator=405483668029440000)
...
#19 factorial_tail (n=20, accumulator=1)
#20 factorial (n=20)
#21 main ()

# En -O2 (TCO activée) :
(gdb) bt
#0  factorial_tail (n=7, accumulator=????)
#1  factorial (n=20)
#2  main ()
```

En `-O2`, la backtrace ne montre qu'un seul frame pour `factorial_tail`, quel que soit le niveau de récursion actuel. Les 19 frames intermédiaires n'ont jamais existé sur la pile.

### Breakpoints et stepping

Poser un breakpoint sur une fonction optimisée par TCO fonctionne, mais le comportement peut sembler étrange :

```
(gdb) b factorial_tail
Breakpoint 1 at 0x401234
(gdb) r 10
Breakpoint 1, factorial_tail ()
(gdb) c
Breakpoint 1, factorial_tail ()   ← même breakpoint, tour de boucle suivant
(gdb) c
Breakpoint 1, factorial_tail ()
```

Le breakpoint est touché à chaque itération de la boucle (puisque le `jmp` revient au début de la fonction). En `-O0`, le breakpoint ne serait touché qu'une fois par frame (au premier appel), puis il faudrait `step` pour entrer dans les appels récursifs.

### Conseil : utiliser le binaire `-O0 -g` pour comprendre, le binaire `-O2` pour valider

En situation de RE, compilez le même source (si disponible) en `-O0 -g` pour comprendre la logique avec GDB (backtrace complète, variables sur la pile), puis validez votre compréhension sur le binaire `-O2` (ou strippé). C'est exactement l'approche du Makefile fourni, qui produit les deux variantes.

---

## Résumé des patterns de la TCO en RE

| Ce que vous voyez | Ce que c'est | Ce qui était dans le source |  
|---|---|---|  
| `jmp` vers le début de la fonction elle-même, paramètres mis à jour dans les registres | Tail recursion optimisée | `return f(new_params);` |  
| Boucle avec mise à jour de registres de paramètres System V (`edi`, `esi`, `edx`, `ecx`) | Idem — indistinguable d'une boucle native | `return f(...)` ou `while(...)` |  
| `jmp other_function` en fin de fonction (pas de `ret`) | Tail call vers une autre fonction | `return other_function(...)` |  
| `jmp rax` / `jmp [reg+offset]` en fin de fonction | Tail call indirect | `return fn_ptr(...)` |  
| `call` récursif suivi d'un `imul`/`add` sur `rax` | Récursion NON terminale (TCO impossible) | `return n * f(n-1)` |  
| `push rbx` + `call` récursif + usage de `ebx` après le `call` | Récursion non terminale avec sauvegarde callee-saved | Idem — le compilateur préserve un état post-appel |  
| Boucle qui décrémente de 2, deux conditions de sortie | Récursion mutuelle fusionnée | `A() → B() → A()` |  
| Backtrace GDB à un seul frame pour une récursion profonde | TCO appliquée | Tail recursion |

---

## En résumé

La tail call optimization est une transformation élégante qui élimine le coût en pile des appels en position terminale. Pour le reverse engineer, son impact principal est la **disparition des frames de pile intermédiaires** : une récursion se transforme en boucle, deux fonctions mutuellement récursives fusionnent, et la backtrace GDB ne reflète plus l'historique des appels.

La règle pratique est simple : si une fonction se termine par un `jmp` (vers elle-même, vers une autre fonction, ou via un registre) au lieu d'un `call` + `ret`, c'est un tail call. Et si vous voyez une boucle dont les « variables » correspondent exactement aux registres de paramètres System V (`rdi`, `rsi`, `rdx`, `rcx`), envisagez l'hypothèse qu'il s'agit d'une récursion terminale transformée.

---


⏭️ [Optimisations Link-Time (`-flto`) et leurs effets sur le graphe d'appels](/16-optimisations-compilateur/05-link-time-optimization.md)
