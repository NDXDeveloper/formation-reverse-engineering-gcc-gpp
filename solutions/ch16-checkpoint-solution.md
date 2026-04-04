🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 16

> ⚠️ **Spoilers** — Ne lisez cette solution qu'après avoir tenté le checkpoint par vous-même.

---

## Environnement de référence

- GCC 13.2.0 (Ubuntu 24.04)  
- x86-64, Linux 6.8  
- Binaires compilés via `make s16_1`

Les adresses et détails exacts varient selon votre version de GCC. Ce corrigé décrit les **patterns structurels** — si vous avez identifié le même type d'optimisation avec des adresses ou registres différents, c'est correct.

---

## Étape préliminaire — Comparaison des symboles

```bash
$ nm build/opt_levels_demo_O0 | grep ' t ' | sort
000000000040116a t clamp
0000000000401199 t classify_grade
00000000004011fd t compute
0000000000401288 t multi_args
00000000004012d5 t print_info
0000000000401150 t square
000000000040117a t sum_of_squares

$ nm build/opt_levels_demo_O2 | grep ' t ' | sort
(rien, ou seulement 1-2 fonctions survivantes)
```

**Constat** : en `-O0`, 7 fonctions `static` sont visibles comme symboles locaux (`t`). En `-O2`, la plupart (voire toutes) ont disparu — elles ont été **inlinées** dans `main()`.

C'est déjà une première optimisation identifiable sans même regarder le désassemblage.

---

## Optimisation 1 : Inlining de `square()`

### Localisation

Fonction `square()` et ses sites d'appel dans `main()`.

### En `-O0`

`square()` existe comme fonction indépendante avec son propre prologue :

```asm
square:
    push   rbp
    mov    rbp, rsp
    mov    DWORD PTR [rbp-0x4], edi
    mov    eax, DWORD PTR [rbp-0x4]
    imul   eax, DWORD PTR [rbp-0x4]
    pop    rbp
    ret
```

L'appel dans `main()` :

```asm
    mov    edi, DWORD PTR [rbp-0x14]     ; charge input
    call   square
    mov    DWORD PTR [rbp-0x18], eax     ; stocke sq
```

### En `-O2`

Le symbole `square` n'existe plus. À l'endroit où `main()` appelait `square(input)`, on trouve simplement :

```asm
    imul   ebx, ebx                      ; sq = input * input
```

Une seule instruction remplace 7 instructions (prologue + corps + épilogue) plus le coût du `call`/`ret`.

### Indice de reconnaissance

L'absence du symbole `square` dans `nm` et l'absence de `call square` dans le désassemblage de `main()`. La présence d'un `imul reg, reg` isolé (auto-multiplication) correspond au corps de `square()`.

---

## Optimisation 2 : Division par constante → magic number dans `compute()`

### Localisation

Fonction `compute()`, boucle contenant `data[i] / 7`.

### En `-O0`

La division utilise l'instruction `idiv` :

```asm
    mov    eax, DWORD PTR [rbp+rax*4-0x30]   ; charge data[i]
    cdq                                       ; extension de signe
    mov    ecx, 7
    idiv   ecx                                ; eax = data[i] / 7
    add    DWORD PTR [rbp-0x34], eax          ; result += quotient
```

Le diviseur `7` est explicitement visible dans le `mov ecx, 7`.

### En `-O2`

La division est remplacée par une multiplication par le magic number `0x92492493` :

```asm
    mov    eax, DWORD PTR [rsp+rsi*4+...]
    mov    edx, 0x92492493                    ; magic number pour /7
    imul   edx
    add    edx, eax                           ; correction additive
    sar    edx, 2                             ; shift arithmétique
    mov    eax, edx
    shr    eax, 31                            ; extraction bit de signe
    add    edx, eax                           ; correction négatifs
```

Le même pattern se retrouve pour `data[3] % 5` (modulo), qui utilise le magic number de la division par 5 (`0x66666667`) suivi d'une multiplication inverse et d'une soustraction.

### Indice de reconnaissance

La présence de `imul` par une grande constante hexadécimale (`0x92492493`) suivie de `sar` est le marqueur infaillible de la division par constante. L'absence totale d'instruction `idiv` dans le binaire `-O2` confirme que toutes les divisions par constante ont été transformées.

---

## Optimisation 3 : Branchement → `cmov` dans `clamp()`

### Localisation

Fonction `clamp()` (inlinée dans `main()` en `-O2`).

### En `-O0`

Deux branchements conditionnels, trois chemins de sortie :

```asm
clamp:
    push   rbp
    mov    rbp, rsp
    mov    DWORD PTR [rbp-0x4], edi
    mov    DWORD PTR [rbp-0x8], esi
    mov    DWORD PTR [rbp-0xc], edx

    mov    eax, DWORD PTR [rbp-0x4]
    cmp    eax, DWORD PTR [rbp-0x8]
    jge    .L_not_low
    mov    eax, DWORD PTR [rbp-0x8]      ; return low
    jmp    .L_end

.L_not_low:
    mov    eax, DWORD PTR [rbp-0x4]
    cmp    eax, DWORD PTR [rbp-0xc]
    jle    .L_not_high
    mov    eax, DWORD PTR [rbp-0xc]      ; return high
    jmp    .L_end

.L_not_high:
    mov    eax, DWORD PTR [rbp-0x4]      ; return value

.L_end:
    pop    rbp
    ret
```

Deux `cmp` + `jge`/`jle`, trois `jmp`, et chaque variable est rechargée depuis la pile.

### En `-O2`

Les deux branchements sont remplacés par deux `cmov` :

```asm
    ; clamp inliné dans main()
    cmp    edi, esi
    cmovl  edi, esi                      ; if (value < low) value = low
    cmp    edi, edx
    cmovg  edi, edx                      ; if (value > high) value = high
```

Quatre instructions, zéro branchement, zéro accès pile.

### Indice de reconnaissance

La séquence `cmp` + `cmovl` suivie de `cmp` + `cmovg` sur le même registre est le pattern exact d'un clamp (saturation entre deux bornes). L'absence de tout `jmp`/`jge`/`jle` pour cette logique confirme l'utilisation de `cmov`.

---

## Optimisation 4 : Jump table dans `classify_grade()`

### Localisation

Fonction `classify_grade()`, le switch sur `score / 10`.

### En `-O0`

Cascade linéaire de comparaisons :

```asm
classify_grade:
    ; ... calcul de score / 10 via idiv ...
    cmp    DWORD PTR [rbp-0x8], 10
    je     .L_case_A
    cmp    DWORD PTR [rbp-0x8], 9
    je     .L_case_A
    cmp    DWORD PTR [rbp-0x8], 8
    je     .L_case_B
    cmp    DWORD PTR [rbp-0x8], 7
    je     .L_case_C
    cmp    DWORD PTR [rbp-0x8], 6
    je     .L_case_D
    cmp    DWORD PTR [rbp-0x8], 5
    je     .L_case_E
    jmp    .L_default
```

Sept comparaisons séquentielles, une par case.

### En `-O2`

Le switch est transformé en jump table avec saut indirect :

```asm
    ; score / 10 via magic number (0x66666667)
    ; ...

    ; Vérification des bornes
    sub    edx, 5                         ; normalisation (case 5 → index 0)
    cmp    edx, 5
    ja     .L_default                     ; hors bornes → default

    ; Saut via la table
    lea    rax, [rip+.L_jumptable]
    movsxd rdx, DWORD PTR [rax+rdx*4]
    add    rax, rdx
    jmp    rax
```

De plus, le `score / 10` qui utilisait `idiv` en `-O0` est lui-même transformé en magic number (`0x66666667`) — c'est une **deuxième optimisation** imbriquée dans la même fonction.

### Indice de reconnaissance

Le pattern `lea` + `movsxd [base+index*4]` + `add` + `jmp rax` est la signature de la jump table. Le `cmp` + `ja` qui précède est la vérification des bornes (protection contre un index hors table). La soustraction initiale (`sub edx, 5`) normalise les cases pour que l'index commence à 0.

---

## Optimisation 5 : Allocation registre (variables sur la pile → registres)

### Localisation

Toutes les fonctions, mais particulièrement visible dans `main()` et dans les boucles.

### En `-O0`

Chaque variable locale est stockée sur la pile. Chaque utilisation génère un load, chaque modification un store :

```asm
    ; int input = 42;
    mov    DWORD PTR [rbp-0x14], 42

    ; sq = square(input)
    mov    edi, DWORD PTR [rbp-0x14]     ; load input
    call   square
    mov    DWORD PTR [rbp-0x18], eax     ; store sq

    ; clamped = clamp(input, 0, 100)
    mov    edx, 100
    mov    esi, 0
    mov    edi, DWORD PTR [rbp-0x14]     ; re-load input (encore !)
    call   clamp
    mov    DWORD PTR [rbp-0x1c], eax     ; store clamped
```

La variable `input` est rechargée depuis `[rbp-0x14]` à chaque utilisation, même si sa valeur n'a pas changé.

### En `-O2`

Les variables vivent dans des registres callee-saved (`ebx`, `r12d`, `r13d`, etc.) pour toute la durée de `main()` :

```asm
    ; input dans ebx pour toute la fonction
    mov    ebx, eax                       ; ebx = input (depuis atoi ou 42)

    ; sq = input * input (square inliné)
    imul   r12d, ebx, ebx                ; r12d = sq = input²

    ; clamped = clamp(input, 0, 100) (clamp inliné)
    mov    eax, ebx
    ; ... cmov pour le clamp ...
    mov    r13d, eax                      ; r13d = clamped
```

`input` est lu une seule fois et reste dans `ebx`. `sq` reste dans `r12d`. `clamped` reste dans `r13d`. Aucun accès à la pile pour ces variables.

### Indice de reconnaissance

L'absence quasi-totale de `mov DWORD PTR [rbp-0x...], ...` dans le corps de `main()` en `-O2`. L'utilisation intensive de registres callee-saved (`ebx`, `r12d`–`r15d`) qui sont préservés par les `push`/`pop` en début et fin de `main()`.

---

## Optimisation 6 : Propagation de constantes pour `strlen`

### Localisation

Fonction `print_info()`, appels avec des chaînes littérales.

### En `-O0`

Chaque appel à `print_info("square", sq)` génère un `call strlen@plt` :

```asm
    ; strlen(label)
    mov    rdi, QWORD PTR [rbp-0x8]      ; label = "square"
    call   strlen@plt                     ; appel dynamique
    ; ... utilise le résultat dans printf ...
```

### En `-O2`

GCC sait que `label` pointe vers `"square"` (constante connue à la compilation) et évalue `strlen("square") = 6` au moment de la compilation :

```asm
    ; print_info("square", sq) inliné
    mov    edx, 6                         ; strlen("square") = 6, évalué statiquement
    lea    rsi, [rip+.LC_square]          ; "square"
    mov    ecx, r12d                      ; value = sq
    lea    rdi, [rip+.LC_fmt]             ; format string
    xor    eax, eax
    call   printf@plt
```

Le `call strlen` a complètement disparu, remplacé par l'immédiat `6`.

### Indice de reconnaissance

L'absence de `call strlen@plt` dans le binaire `-O2` pour les appels avec des chaînes littérales. La présence d'un `mov edx, N` (où N correspond à la longueur de la chaîne) juste avant le `call printf`.

Pour vérifier : comptez les lettres de chaque chaîne littérale et vérifiez que la constante dans le `mov` correspond.

---

## Optimisation 7 : Inlining de `sum_of_squares()` et suppression de l'appel à `square()`

### Localisation

Boucle de `sum_of_squares()`, inlinée dans `main()`.

### En `-O0`

Deux niveaux d'appel : `main()` → `sum_of_squares()` → `square()` :

```asm
    ; Dans sum_of_squares :
.L_loop:
    mov    edi, DWORD PTR [rbp-0xc]      ; i
    call   square                         ; appel à square(i)
    cdqe
    add    QWORD PTR [rbp-0x8], rax      ; total += result
    add    DWORD PTR [rbp-0xc], 1        ; i++
    ; ...
    jmp    .L_loop
```

### En `-O2`

Les deux fonctions sont inlinées en cascade dans `main()`. La boucle entière se réduit à :

```asm
    ; sum_of_squares inliné, square() inliné dedans
    xor    eax, eax                       ; total = 0
    mov    edx, 1                         ; i = 1
.L_sos_loop:
    mov    ecx, edx
    imul   ecx, edx                      ; ecx = i * i (square inliné)
    movsxd rcx, ecx
    add    rax, rcx                      ; total += i*i
    add    edx, 1                        ; i++
    cmp    edx, ebx                      ; i <= n ?
    jle    .L_sos_loop
```

Deux niveaux de `call` ont été éliminés. Le corps de `square(i)` — un simple `imul ecx, edx` — apparaît directement dans la boucle.

### Indice de reconnaissance

L'absence de `call sum_of_squares` ET de `call square` dans `main()`. La présence d'une boucle avec `imul reg, reg` (auto-multiplication) et un accumulateur — c'est la signature de la somme des carrés avec `square()` inliné.

---

## Optimisation 8 : Multiplication par constante via `lea`

### Localisation

Divers endroits dans `compute()` et `multi_args()`, inlinés dans `main()`.

### En `-O0`

Les multiplications utilisent `imul` :

```asm
    ; a * (i + 1) dans compute()
    mov    eax, DWORD PTR [rbp-0x24]     ; i
    add    eax, 1                        ; i + 1
    imul   eax, DWORD PTR [rbp-0x14]    ; * a
```

### En `-O2`

Pour les petits multiplicateurs, GCC utilise `lea` :

```asm
    ; Exemples dans le code inliné
    lea    eax, [rdi+rdi*2]              ; x * 3
    lea    eax, [rdi+rdi*4]              ; x * 5
```

### Indice de reconnaissance

Un `lea` avec un facteur d'échelle (`*2`, `*4`, `*8`) qui n'est pas utilisé comme calcul d'adresse (pas de déréférencement `[...]` ensuite) est une multiplication déguisée.

---

## Optimisation 9 (bonus) : Remplacement de `printf` par `puts`

### Localisation

L'appel `printf("Grade: %s\n", grade)` à la fin de `main()`.

### En `-O0`

```asm
    lea    rdi, [rip+.LC_grade_fmt]      ; "Grade: %s\n"
    mov    rsi, rax                       ; grade
    call   printf@plt
```

### En `-O2`

Si GCC détecte un `printf` dont le format est une simple chaîne sans spécificateur (par exemple `printf("Hello\n")`), il le remplace par `puts`. Ce remplacement ne s'applique pas à tous les `printf` du programme (ceux avec `%d`, `%s`, etc. restent des `printf`), mais il peut s'appliquer à certains cas simples.

### Indice de reconnaissance

La présence d'un `call puts@plt` dans le binaire `-O2` là où le source ne contient que des `printf`. Vérifiable en comparant les appels de bibliothèque entre les deux versions :

```bash
objdump -d build/opt_levels_demo_O0 | grep 'call.*@plt' | sort -u  
objdump -d build/opt_levels_demo_O2 | grep 'call.*@plt' | sort -u  
```

---

## Résumé des optimisations identifiées

| # | Optimisation | Section du chapitre | Difficulté |  
|---|---|---|---|  
| 1 | Inlining de `square()` | 16.2 | Facile |  
| 2 | Division par constante → magic number | 16.6, idiome 1 | Moyenne |  
| 3 | Branchement → `cmov` dans `clamp()` | 16.6, idiome 5 | Facile |  
| 4 | Switch → jump table | 16.6, idiome 8 | Moyenne |  
| 5 | Allocation registre | 16.1 | Facile |  
| 6 | `strlen` résolu à la compilation | 16.1 | Moyenne |  
| 7 | Inlining en cascade (`sum_of_squares` + `square`) | 16.2 | Moyenne |  
| 8 | Multiplication via `lea` | 16.6, idiome 4 | Facile |  
| 9 | `printf` → `puts` | 16.7 | Facile |

Si vous en avez trouvé **3 ou plus**, vous avez validé le checkpoint. Si vous avez trouvé les 9 (ou d'autres non listées ici, comme le déroulage de la boucle dans `compute()`), vous maîtrisez le sujet.

---


⏭️
