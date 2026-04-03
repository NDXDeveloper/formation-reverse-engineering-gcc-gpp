🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 16.1 — Impact de `-O1`, `-O2`, `-O3`, `-Os` sur le code désassemblé

> **Fichier source associé** : `binaries/ch16-optimisations/opt_levels_demo.c`  
> **Compilation** : `make s16_1` (produit 6 variantes dans `build/`)

---

## Introduction

Quand vous lancez `gcc -O0 programme.c`, le compilateur traduit votre code C en assembleur de la façon la plus littérale possible : chaque variable vit sur la pile, chaque opération génère les instructions correspondantes dans l'ordre où vous les avez écrites, et chaque appel de fonction produit un `call` explicite. C'est un assembleur « lisible » — presque une traduction mot à mot.

Dès que vous montez d'un cran avec `-O1`, puis `-O2`, `-O3` ou `-Os`, GCC active progressivement des centaines de passes d'optimisation qui transforment ce code en profondeur. Le résultat est plus rapide (ou plus compact), mais sa structure peut devenir méconnaissable par rapport au source original.

Pour le reverse engineer, comprendre ce que fait chaque niveau d'optimisation est une compétence fondamentale. Elle permet de répondre à la question qui se pose face à chaque binaire inconnu : *ce que je vois dans le désassemblage reflète-t-il la logique du développeur, ou une transformation du compilateur ?*

---

## Vue d'ensemble des niveaux d'optimisation

Avant d'entrer dans le désassemblage, il est essentiel de comprendre ce que chaque flag active. GCC regroupe ses passes d'optimisation en niveaux cumulatifs — chaque niveau inclut toutes les passes du niveau précédent, plus des passes supplémentaires.

### `-O0` — Aucune optimisation

C'est le niveau par défaut. GCC ne fait aucune transformation :

- Chaque variable locale est allouée sur la pile (dans le frame de la fonction).  
- Chaque lecture de variable produit un `mov` depuis la pile, chaque écriture un `mov` vers la pile — même si la valeur vient d'être calculée dans un registre.  
- Chaque appel de fonction produit un `call` explicite, même pour les fonctions `static` triviales.  
- Les branchements suivent exactement la structure if/else du source.  
- Aucune réorganisation du code, aucune élimination de code mort.

C'est le niveau idéal pour l'apprentissage du RE car la correspondance source → assembleur est directe. C'est aussi le niveau utilisé avec `-g` pour le débogage, car les breakpoints et le stepping ligne par ligne fonctionnent de manière prévisible.

### `-O1` — Optimisations conservatrices

GCC commence à transformer le code, mais reste prudent :

- **Allocation registre** : les variables locales fréquemment utilisées sont placées dans des registres au lieu de la pile. Les load/store inutiles disparaissent.  
- **Propagation de constantes** : si une variable vaut toujours 42, GCC remplace son usage par l'immédiat 42.  
- **Élimination de code mort** : le code dont le résultat n'est jamais utilisé est supprimé.  
- **Simplification algébrique** : `x * 1` → `x`, `x + 0` → `x`, `x * 2` → `x + x` ou `shl`.  
- **Fusion de branches** simples.  
- **Inlining des fonctions triviales** marquées `static` et appelées une seule fois.

Le code reste relativement fidèle à la structure du source, mais les variables vivent dans des registres, ce qui rend le suivi dans GDB un peu moins direct.

### `-O2` — Optimisations standard (le cas de production)

C'est le niveau le plus courant dans les binaires que vous rencontrerez en RE. Il ajoute à `-O1` :

- **Inlining plus agressif** : les fonctions `static` de taille raisonnable sont inlinées, même si elles sont appelées plusieurs fois.  
- **Déroulage partiel de boucles** : une boucle de N itérations peut être transformée en une boucle de N/2 itérations traitant 2 éléments à chaque tour.  
- **Réordonnancement des instructions** : GCC réarrange les instructions pour maximiser le parallélisme du pipeline CPU. L'ordre de l'assembleur ne correspond plus à l'ordre des lignes du source.  
- **Remplacement de divisions par des multiplications** : `x / 7` devient une multiplication par un « magic number » suivie d'un shift (détail dans la section 16.6).  
- **Conditional moves** (`cmov`) : les branchements simples (type ternaire `a > b ? a : b`) sont remplacés par des instructions `cmov` qui évitent les branch mispredictions.  
- **Peephole optimizations** : remplacement de séquences d'instructions par des équivalents plus courts (`lea` au lieu de `add` + `mov`, etc.).  
- **Tail call optimization** : un appel en position terminale est remplacé par un `jmp` (détail en section 16.4).  
- **Élimination de sous-expressions communes** (CSE) : si `a * b` est calculé deux fois, GCC le calcule une seule fois et réutilise le résultat.

### `-O3` — Optimisations agressives

Ajoute à `-O2` des transformations qui augmentent la taille du code en échange de performances :

- **Vectorisation automatique** : les boucles sur des tableaux sont transformées pour utiliser les instructions SIMD (SSE, AVX). Au lieu de traiter un `int` par itération, GCC traite 4 (SSE, 128 bits) ou 8 (AVX, 256 bits) en parallèle.  
- **Déroulage de boucles agressif** : les boucles sont déroulées davantage qu'en `-O2`.  
- **Inlining encore plus agressif** : le seuil de taille pour l'inlining est relevé.  
- **Clonage de fonctions** : une même fonction peut être dupliquée avec des spécialisations différentes selon le contexte d'appel.  
- **Interchangement de boucles** (loop interchange), **fusion de boucles**, **distribution de boucles**.

Le code `-O3` est souvent nettement plus gros que le code `-O2`, et sa structure peut diverger considérablement du source. Pour le RE, c'est le niveau le plus exigeant à analyser.

### `-Os` — Optimisation en taille

Active les mêmes passes qu'`-O2`, **sauf celles qui augmentent la taille du code** :

- Pas de déroulage de boucles.  
- Pas de vectorisation (les instructions SIMD ajoutent du code de prologue/épilogue).  
- Inlining plus conservateur (le coût en taille de la duplication dépasse le gain).  
- Préférence pour les boucles compactes et les appels de fonction.  
- Utilisation de `rep stosb` / `rep movsb` plutôt que des séquences de `mov` déroulées.

On retrouve `-Os` dans les firmwares embarqués, les bootloaders, et certains binaires où la taille du segment `.text` est critique. Pour le RE, `-Os` produit un code plus proche de `-O1` en termes de lisibilité, tout en utilisant les mêmes transformations algébriques que `-O2` (magic numbers pour les divisions, `cmov`, etc.).

---

## Comparaisons concrètes sur `opt_levels_demo.c`

Tout au long de cette section, les exemples de désassemblage sont en syntaxe Intel (obtenue avec `objdump -d -M intel`). Les adresses et offsets exacts peuvent varier selon votre version de GCC ; ce sont les **patterns structurels** qui importent.

> 💡 **Reproduire chez vous** : après `make s16_1`, utilisez la commande utilitaire du Makefile :  
> ```bash  
> make disasm_compare BIN=opt_levels_demo  
> ```

### Cas 1 — Fonction arithmétique simple : `square()`

La fonction `square()` est un cas d'école — un seul calcul, un seul paramètre :

```c
static int square(int x)
{
    return x * x;
}
```

#### En `-O0`

La fonction existe comme un symbole à part entière, avec son propre prologue et épilogue :

```asm
square:
    push   rbp
    mov    rbp, rsp
    mov    DWORD PTR [rbp-0x4], edi    ; sauvegarde x sur la pile
    mov    eax, DWORD PTR [rbp-0x4]    ; recharge x depuis la pile
    imul   eax, DWORD PTR [rbp-0x4]    ; eax = x * x (lecture pile)
    pop    rbp
    ret
```

Le paramètre `x` arrive dans `edi` (convention System V), est copié sur la pile, puis rechargé depuis la pile pour la multiplication. C'est absurde en termes de performance, mais c'est ce que produit `-O0` : un aller-retour pile systématique pour chaque variable.

Quand `main()` appelle `square(input)`, on voit un `call square` explicite :

```asm
    ; dans main(), appel de square(input)
    mov    edi, DWORD PTR [rbp-0x14]   ; charge input depuis la pile
    call   square                       ; appel explicite
    mov    DWORD PTR [rbp-0x18], eax   ; stocke le résultat sur la pile
```

#### En `-O2`

La fonction `square()` **disparaît complètement** du binaire. GCC l'inline dans chaque site d'appel. À l'endroit où `main()` appelait `square(input)`, on trouve simplement :

```asm
    ; dans main() — square(input) inliné
    imul   ebx, ebx                    ; ebx = input * input (1 instruction)
```

Pas de `call`, pas de prologue, pas d'accès pile. La variable `input` est déjà dans `ebx` (allocation registre), et le résultat `sq` reste aussi dans un registre pour être réutilisé immédiatement.

Si vous cherchez le symbole `square` dans la table des symboles (`nm build/opt_levels_demo_O2`), il n'existe plus. Pour le reverse engineer, la fonction a été **absorbée** — il faut la reconstituer mentalement.

#### Ce qu'il faut retenir

En `-O0`, chaque fonction, même triviale, est un `call` avec prologue/épilogue. En `-O2`, les fonctions triviales `static` sont systématiquement inlinées. Si vous analysez un binaire optimisé et que vous ne trouvez pas de fonction `square` dans Ghidra, c'est normal — elle n'existe plus comme entité indépendante.

---

### Cas 2 — Branchement conditionnel : `clamp()`

```c
static int clamp(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}
```

#### En `-O0`

La structure du code source est reproduite fidèlement avec deux comparaisons et deux sauts conditionnels :

```asm
clamp:
    push   rbp
    mov    rbp, rsp
    mov    DWORD PTR [rbp-0x4], edi     ; value
    mov    DWORD PTR [rbp-0x8], esi     ; low
    mov    DWORD PTR [rbp-0xc], edx     ; high

    ; if (value < low)
    mov    eax, DWORD PTR [rbp-0x4]
    cmp    eax, DWORD PTR [rbp-0x8]
    jge    .L_not_low                   ; saute si value >= low
    mov    eax, DWORD PTR [rbp-0x8]     ; return low
    jmp    .L_end

.L_not_low:
    ; if (value > high)
    mov    eax, DWORD PTR [rbp-0x4]
    cmp    eax, DWORD PTR [rbp-0xc]
    jle    .L_not_high                  ; saute si value <= high
    mov    eax, DWORD PTR [rbp-0xc]     ; return high
    jmp    .L_end

.L_not_high:
    mov    eax, DWORD PTR [rbp-0x4]     ; return value

.L_end:
    pop    rbp
    ret
```

On lit le code assembleur presque comme du C — deux `cmp` + `jge`/`jle`, trois chemins de sortie. Chaque accès à une variable passe par la pile.

#### En `-O2`

GCC remplace les branchements par deux instructions `cmov` — des **conditional moves** qui évitent tout saut :

```asm
    ; clamp inliné dans main()
    ; edi = value, esi = low, edx = high  (ou déjà dans des registres)
    cmp    edi, esi
    cmovl  edi, esi        ; if (value < low) value = low
    cmp    edi, edx
    cmovg  edi, edx        ; if (value > high) value = high
    ; edi contient le résultat
```

Quatre instructions, zéro branchement. C'est plus rapide car le CPU n'a pas à prédire de branche, et c'est aussi plus compact.

#### Comment le reconnaître en RE

Quand vous voyez une séquence `cmp` + `cmovCC` dans un binaire optimisé, vous pouvez la « dé-optimiser » mentalement en un if/else simple. Les variantes courantes sont `cmovl` / `cmovg` (signé), `cmovb` / `cmova` (non signé), `cmove` / `cmovne` (égalité).

Un piège fréquent : le `cmov` calcule les **deux** valeurs possibles avant la condition. Si l'une des branches a un effet de bord (appel de fonction, écriture mémoire), GCC ne peut pas utiliser `cmov` et reste sur un branchement classique.

---

### Cas 3 — Switch/case : `classify_grade()`

```c
static const char *classify_grade(int score)
{
    switch (score / 10) {
        case 10:
        case 9:  return "A";
        case 8:  return "B";
        case 7:  return "C";
        case 6:  return "D";
        case 5:  return "E";
        default: return "F";
    }
}
```

#### En `-O0`

GCC génère une **cascade de comparaisons** — une séquence linéaire de `cmp` + `je` (jump if equal) :

```asm
classify_grade:
    push   rbp
    mov    rbp, rsp
    mov    DWORD PTR [rbp-0x4], edi

    ; Calcul de score / 10 (via idiv)
    mov    eax, DWORD PTR [rbp-0x4]
    cdq
    mov    ecx, 10
    idiv   ecx                          ; eax = score / 10
    mov    DWORD PTR [rbp-0x8], eax

    ; Cascade de comparaisons
    cmp    DWORD PTR [rbp-0x8], 10
    je     .L_case_A
    cmp    DWORD PTR [rbp-0x8], 9
    je     .L_case_A
    cmp    DWORD PTR [rbp-0x8], 8
    je     .L_case_B
    cmp    DWORD PTR [rbp-0x8], 7
    je     .L_case_C
    ; ... etc.
    jmp    .L_default

.L_case_A:
    lea    rax, [rip+str_A]             ; "A"
    jmp    .L_end
; ... etc.
```

Chaque `case` produit un `cmp` + `je`. Le compilateur les teste dans l'ordre. C'est simple à lire mais inefficace pour un grand nombre de cases.

#### En `-O2`

GCC applique deux optimisations simultanées :

1. **La division par 10** est remplacée par une multiplication par le magic number (cf. section 16.6).  
2. **Le switch est transformé en jump table** — un tableau de pointeurs stocké dans `.rodata`.

```asm
    ; Division par 10 via magic number
    mov    eax, edi
    mov    edx, 0x66666667              ; magic number pour /10
    imul   edx
    sar    edx, 2                       ; edx = score / 10
    mov    eax, edi
    sar    eax, 31
    sub    edx, eax                     ; correction pour les négatifs

    ; Vérification des bornes de la jump table
    sub    edx, 5                       ; normalise : case 5 → index 0
    cmp    edx, 5
    ja     .L_default                   ; hors bornes → "F"

    ; Saut indirect via la table
    lea    rax, [rip+.L_jumptable]
    movsxd rdx, DWORD PTR [rax+rdx*4]  ; charge l'offset depuis la table
    add    rax, rdx
    jmp    rax                          ; saut vers le bon case
```

La jump table est un bloc de données dans `.rodata` contenant les offsets relatifs de chaque case. Au lieu de N comparaisons, le CPU fait un seul accès mémoire indexé + un saut indirect. La complexité passe de O(N) à O(1).

#### Comment le reconnaître en RE

Le pattern de la jump table est l'un des plus importants à reconnaître :

1. Un `cmp` + `ja` qui vérifie les bornes (protection contre un index hors table).  
2. Un `lea` qui charge l'adresse de base de la table.  
3. Un accès indexé `[base + index*4]` (ou `*8` en 64 bits).  
4. Un `jmp rax` — le saut indirect.

Ghidra reconnaît automatiquement les jump tables et reconstruit le switch dans le décompilateur. Mais si le binaire est obfusqué ou si la table est déplacée, il faut savoir la repérer manuellement en cherchant le pattern `lea` + `movsxd` + `add` + `jmp reg`.

En `-Os`, GCC préfère souvent **garder la cascade de comparaisons** plutôt que la jump table, car la table occupe de l'espace dans `.rodata`. C'est un indice pour identifier le flag d'optimisation utilisé.

---

### Cas 4 — Boucle d'accumulation : `sum_of_squares()`

```c
static long sum_of_squares(int n)
{
    long total = 0;
    for (int i = 1; i <= n; i++) {
        total += square(i);
    }
    return total;
}
```

#### En `-O0`

La boucle est traduite littéralement : compteur sur la pile, accumulateur sur la pile, appel `call square` à chaque itération.

```asm
sum_of_squares:
    push   rbp
    mov    rbp, rsp
    sub    rsp, 0x18
    mov    DWORD PTR [rbp-0x14], edi    ; n sur la pile

    ; total = 0
    mov    QWORD PTR [rbp-0x8], 0

    ; i = 1
    mov    DWORD PTR [rbp-0xc], 1

.L_loop_check:
    ; i <= n ?
    mov    eax, DWORD PTR [rbp-0xc]
    cmp    eax, DWORD PTR [rbp-0x14]
    jg     .L_loop_end

    ; call square(i)
    mov    edi, DWORD PTR [rbp-0xc]     ; charge i depuis la pile
    call   square
    cdqe                                ; extension 32→64 bits
    add    QWORD PTR [rbp-0x8], rax     ; total += result

    ; i++
    add    DWORD PTR [rbp-0xc], 1

    jmp    .L_loop_check

.L_loop_end:
    mov    rax, QWORD PTR [rbp-0x8]     ; return total
    leave
    ret
```

Chaque itération fait deux accès pile (lire `i`, écrire `total`) et un `call` vers `square`. C'est verbeux mais parfaitement lisible.

#### En `-O2`

GCC applique plusieurs transformations en cascade :

1. `square(i)` est inlinée — le `call` disparaît, remplacé par un `imul`.  
2. `i` et `total` vivent dans des registres — aucun accès pile.  
3. La boucle peut être partiellement déroulée (2 itérations par tour).

```asm
    ; sum_of_squares inliné dans main()
    ; ecx = n (déjà dans un registre)
    xor    eax, eax                     ; total = 0
    test   ecx, ecx
    jle    .L_done                      ; si n <= 0, skip
    mov    edx, 1                       ; i = 1

.L_loop:
    mov    esi, edx
    imul   esi, edx                     ; esi = i * i  (square inliné)
    movsxd rsi, esi
    add    rax, rsi                     ; total += i*i
    add    edx, 1                       ; i++
    cmp    edx, ecx
    jle    .L_loop                      ; boucle

.L_done:
    ; rax = total (résultat)
```

La boucle est réduite à 5 instructions utiles par itération : `imul`, `movsxd`, `add`, `add`, `cmp`+`jle`. Pas d'accès mémoire, tout dans les registres.

#### En `-O3`

En plus de ce que fait `-O2`, GCC peut :

- **Dérouler la boucle** (traiter 2 ou 4 itérations par tour).  
- **Vectoriser** si le corps le permet (ici, l'accumulation `long` et le `imul` rendent la vectorisation difficile, donc GCC se contente souvent du déroulage).

Le déroulage produit un code du type :

```asm
.L_loop_unrolled:
    ; Itération i
    mov    esi, edx
    imul   esi, edx
    movsxd rsi, esi
    add    rax, rsi

    ; Itération i+1 (déroulée)
    lea    esi, [edx+1]
    imul   esi, esi
    movsxd rsi, esi
    add    rax, rsi

    add    edx, 2                       ; i += 2
    cmp    edx, ecx
    jle    .L_loop_unrolled

    ; + boucle "épilogue" pour le reste si n est impair
```

Le nombre d'itérations de la boucle est divisé par 2, au prix d'un corps de boucle doublé. On reconnaît un déroulage au fait que le compteur est incrémenté de 2 (ou 4, 8…) au lieu de 1.

---

### Cas 5 — Division par constante et magic numbers : `compute()`

```c
static int compute(int a, int b)
{
    int data[8];
    for (int i = 0; i < 8; i++)
        data[i] = a * (i + 1) + b;

    int result = 0;
    for (int i = 0; i < 8; i++)
        result += data[i] / 7;

    result += data[3] % 5;
    return result;
}
```

#### En `-O0`

La division par 7 utilise l'instruction `idiv` — une vraie division matérielle :

```asm
    ; result += data[i] / 7
    mov    eax, DWORD PTR [rbp+rax*4-0x30]  ; charge data[i]
    cdq                                      ; extension de signe → edx:eax
    mov    ecx, 7
    idiv   ecx                               ; eax = quotient, edx = reste
    add    DWORD PTR [rbp-0x34], eax         ; result += quotient
```

L'instruction `idiv` est l'une des plus lentes du jeu d'instructions x86 — elle prend entre 20 et 90 cycles selon le CPU. C'est pourquoi GCC la remplace dès `-O1`.

#### En `-O2`

GCC remplace `x / 7` par une multiplication par le « magic number » `0x92492493` (ou une variante selon le signe) suivie d'un décalage :

```asm
    ; data[i] / 7  via magic number
    mov    eax, DWORD PTR [rsp+rsi*4]       ; charge data[i]
    mov    edx, 0x92492493                   ; magic number pour /7
    imul   edx                               ; edx:eax = x * magic
    ; edx contient les bits de poids fort du produit
    add    edx, eax                          ; correction (spécifique à /7)
    sar    edx, 2                            ; shift arithmétique
    mov    ecx, edx
    shr    ecx, 31                           ; extraction du bit de signe
    add    edx, ecx                          ; correction finale pour négatifs
    ; edx = x / 7
```

Ce pattern est expliqué en détail dans la section 16.6. L'idée clé pour le RE : quand vous voyez un `imul` par une constante hexadécimale improbable suivi de `sar`, vous êtes face à une division par constante transformée. Le diviseur original peut être retrouvé à partir du magic number.

De même, `data[3] % 5` est transformé en : calcul de `x / 5` par magic number, puis `x - (x/5) * 5`.

---

### Cas 6 — Passage de paramètres : `multi_args()`

```c
static int multi_args(int a, int b, int c, int d, int e, int f, int g, int h)
{
    return (a + b) * (c - d) + (e ^ f) - (g | h);
}
```

#### En `-O0`

La convention System V AMD64 passe les 6 premiers paramètres entiers dans `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`. Les suivants passent par la pile. En `-O0`, **tous** sont recopiés sur la pile dans le prologue :

```asm
multi_args:
    push   rbp
    mov    rbp, rsp

    ; Sauvegarde des 6 paramètres registres sur la pile
    mov    DWORD PTR [rbp-0x4],  edi    ; a
    mov    DWORD PTR [rbp-0x8],  esi    ; b
    mov    DWORD PTR [rbp-0xc],  edx    ; c
    mov    DWORD PTR [rbp-0x10], ecx    ; d
    mov    DWORD PTR [rbp-0x14], r8d    ; e
    mov    DWORD PTR [rbp-0x18], r9d    ; f
    ; g et h sont déjà sur la pile (au-dessus de l'adresse de retour)
    ; g = [rbp+0x10], h = [rbp+0x18]

    ; (a + b)
    mov    eax, DWORD PTR [rbp-0x4]
    add    eax, DWORD PTR [rbp-0x8]
    ; (c - d)
    mov    edx, DWORD PTR [rbp-0xc]
    sub    edx, DWORD PTR [rbp-0x10]
    ; (a+b) * (c-d)
    imul   eax, edx
    ; ... etc.
```

#### En `-O2`

Si la fonction est inlinée, tout le calcul se fait dans les registres sans aucun accès pile. Si elle n'est pas inlinée (par exemple si elle était `__attribute__((noinline))`), les paramètres restent dans leurs registres d'arrivée sans être recopiés sur la pile :

```asm
multi_args:
    ; Pas de prologue — leaf function optimisée
    lea    eax, [rdi+rsi]          ; eax = a + b
    mov    r10d, edx
    sub    r10d, ecx               ; r10d = c - d
    imul   eax, r10d               ; eax = (a+b) * (c-d)
    xor    r8d, r9d                ; r8d = e ^ f
    add    eax, r8d                ; eax += (e ^ f)
    mov    r10d, DWORD PTR [rsp+0x8]
    or     r10d, DWORD PTR [rsp+0x10]  ; r10d = g | h (depuis la pile)
    sub    eax, r10d               ; eax -= (g | h)
    ret
```

Notez que `g` et `h` (les 7e et 8e paramètres) sont toujours lus depuis la pile — la convention d'appel l'impose. Mais les 6 premiers ne sont jamais sauvegardés.

---

### Cas 7 — Appel de bibliothèque : `print_info()`

```c
static void print_info(const char *label, int value)
{
    printf("[%s] (len=%zu) = %d\n", label, strlen(label), value);
}
```

#### En `-O0`

L'appel à `strlen` génère un `call strlen@plt` — un appel via la PLT (Procedure Linkage Table) vers la bibliothèque C :

```asm
    ; strlen(label)
    mov    rdi, QWORD PTR [rbp-0x8]    ; charge label
    call   strlen@plt                   ; appel dynamique via PLT

    ; printf(format, label, strlen_result, value)
    mov    rcx, QWORD PTR [rbp-0x8]    ; label → rsi
    mov    rdx, rax                     ; strlen result → rdx
    mov    r8d, DWORD PTR [rbp-0xc]    ; value → rcx (après décalage)
    lea    rdi, [rip+.LC_fmt]          ; format string → rdi
    call   printf@plt
```

#### En `-O2`

Si `label` est une constante connue à la compilation (ce qui est le cas dans nos appels comme `print_info("square", sq)`), GCC **évalue `strlen` à la compilation** et la remplace par une constante :

```asm
    ; print_info("square", sq) — inliné
    ; strlen("square") = 6 — évalué à la compilation
    mov    edx, 6                       ; strlen résolu en constante !
    lea    rsi, [rip+.LC_square]        ; "square"
    mov    ecx, ebx                     ; value (déjà dans un registre)
    lea    rdi, [rip+.LC_fmt]           ; format string
    xor    eax, eax                     ; variadic: 0 args flottants
    call   printf@plt
```

Le `call strlen` a complètement disparu — remplacé par un `mov edx, 6`. C'est la **propagation de constantes inter-procédurale** : GCC sait que le premier argument est `"square"`, que `strlen("square")` vaut 6, et il substitue directement.

> ⚠️ Attention : cette optimisation ne fonctionne que si la chaîne est connue à la compilation. Si `label` provient d'une entrée utilisateur, `strlen` reste un `call strlen@plt` même en `-O2`.

---

## Résumé comparatif

Le tableau ci-dessous synthétise les différences clés observables dans le désassemblage selon le niveau d'optimisation :

| Aspect | `-O0` | `-O1` | `-O2` | `-O3` | `-Os` |  
|---|---|---|---|---|---|  
| Variables locales | Pile | Registres | Registres | Registres | Registres |  
| Fonctions `static` triviales | `call` explicite | Inlinées (1 site) | Inlinées (multi-sites) | Inlinées agressif | Inlinées (conservateur) |  
| Branchements if/else | `cmp` + `jcc` | `cmp` + `jcc` | `cmov` si possible | `cmov` | `cmov` si possible |  
| switch dense | Cascade `cmp`+`je` | Cascade ou jump table | Jump table | Jump table | Cascade (compacte) |  
| Division par constante | `idiv` | Magic number | Magic number | Magic number | Magic number |  
| Boucles | Littérale, pile | Registres | Déroulage partiel | Déroulage + vectorisation SIMD | Pas de déroulage |  
| Taille du `.text` | Grande (verbeux) | Moyenne | Moyenne-grande | Grande (code dupliqué) | Petite |  
| Lisibilité RE | Excellente | Bonne | Moyenne | Difficile | Moyenne |

---

## Impact sur la taille du binaire

Compilez `opt_levels_demo.c` aux différents niveaux et comparez les tailles :

```bash
$ make s16_1
$ ls -lhS build/opt_levels_demo_*
```

Résultat typique (GCC 13, x86-64, Linux) :

```
build/opt_levels_demo_O0         ~21 Ko   (le plus gros — verbeux, symboles DWARF)  
build/opt_levels_demo_O3         ~18 Ko   (code déroulé/dupliqué + DWARF)  
build/opt_levels_demo_O2         ~17 Ko   (bon compromis)  
build/opt_levels_demo_O1         ~17 Ko  
build/opt_levels_demo_Os         ~16 Ko   (le plus compact avec symboles)  
build/opt_levels_demo_O2_strip   ~14 Ko   (strippé — sans DWARF ni symboles)  
```

L'essentiel de la différence entre `-O0` et les autres vient de l'élimination des accès pile inutiles et de l'inlining qui supprime les prologues/épilogues. Le strip (`-s`) retire les tables de symboles et les informations DWARF, ce qui réduit encore la taille de façon significative.

Utilisez `readelf -S` pour comparer la taille de la section `.text` (le code exécutable) indépendamment des métadonnées :

```bash
readelf -S build/opt_levels_demo_O0 | grep '\.text'  
readelf -S build/opt_levels_demo_O2 | grep '\.text'  
```

---

## Impact sur le nombre de fonctions visibles

Un indicateur simple mais révélateur est le nombre de symboles de type `T` (fonctions dans `.text`) visibles avec `nm` :

```bash
$ nm build/opt_levels_demo_O0 | grep ' t \| T ' | wc -l
  12      ← main + toutes les fonctions static

$ nm build/opt_levels_demo_O2 | grep ' t \| T ' | wc -l
  3       ← main + quelques fonctions non inlinées
```

En `-O0`, chaque fonction `static` apparaît avec un symbole local (`t` minuscule). En `-O2`, les fonctions inlinées disparaissent — il ne reste que celles qui étaient trop volumineuses pour être inlinées et `main`.

Pour le RE sur un binaire strippé (`_O2_strip`), `nm` ne montre plus rien. Il faut alors utiliser Ghidra ou un autre désassembleur pour reconstruire les frontières de fonctions par analyse heuristique des prologues/épilogues.

---

## Conseils pratiques pour le RE

Voici les réflexes à adopter face à un binaire dont vous ne connaissez pas le niveau d'optimisation :

**1. Vérifier la présence de `idiv` par des constantes.** Si vous voyez des `idiv ecx` avec `ecx = 7` (ou toute autre constante), le binaire est probablement en `-O0`. À partir de `-O1`, GCC remplace systématiquement ces divisions par des magic numbers.

**2. Vérifier la présence d'instructions `cmov`.** Les `cmov` sont quasi-absents en `-O0` et omniprésents à partir de `-O2`. Leur présence est un indicateur fiable d'optimisation.

**3. Compter les fonctions.** Un binaire avec beaucoup de petites fonctions (prologues `push rbp` / `mov rbp, rsp` partout) est typique de `-O0`. Un binaire où `main()` est une longue séquence monolithique suggère un inlining agressif (`-O2` ou plus).

**4. Chercher les jump tables.** La présence de jump tables dans `.rodata` indique au minimum `-O2`. En `-Os`, les switch sont souvent laissés en cascades de comparaisons.

**5. Chercher les instructions SIMD.** Des instructions `movdqa`, `paddd`, `pmulld` (SSE) ou `vpaddd`, `vmovdqu` (AVX) dans le corps de boucles indiquent `-O3` — ou `-O2` avec `-ftree-vectorize` explicite.

**6. Observer la structure des boucles.** Un compteur incrémenté de 1 à chaque tour est typique de `-O0` ou `-O1`. Un compteur incrémenté de 2, 4 ou 8 indique un déroulage (`-O2` ou `-O3`). La présence d'une boucle « épilogue » (qui traite les éléments restants après la boucle principale) est caractéristique de la vectorisation.

**7. Regarder les accès mémoire dans les fonctions.** Si les premiers `mov` du corps d'une fonction copient les registres de paramètres (`edi`, `esi`, `edx`, `ecx`) sur la pile puis les rechargent immédiatement, c'est du `-O0`. En `-O2`, les paramètres restent dans les registres.

---


⏭️ [Inlining de fonctions : quand la fonction disparaît du binaire](/16-optimisations-compilateur/02-inlining.md)
