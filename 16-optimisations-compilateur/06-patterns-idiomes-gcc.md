🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 16.6 — Reconnaître les patterns typiques de GCC (idiomes compilateur)

> **Fichier source associé** : `binaries/ch16-optimisations/gcc_idioms.c`  
> **Compilation** : `make s16_6` (produit 6 variantes dans `build/`)

---

## Introduction

Chaque compilateur a ses « tics » — des séquences d'instructions caractéristiques qu'il émet systématiquement pour traduire certaines constructions C. Ces séquences sont appelées **idiomes compilateur** (*compiler idioms*). Les reconnaître instantanément est l'une des compétences les plus précieuses en reverse engineering : au lieu de décortiquer chaque instruction une par une, vous identifiez le pattern d'un coup et reconstituez l'opération C originale.

Les sections précédentes ont déjà montré certains de ces idiomes en contexte (le magic number de division en 16.1, le `cmov` en 16.1, le déroulage en 16.3). Cette section les rassemble tous dans un catalogue systématique, enrichi de patterns supplémentaires que vous rencontrerez fréquemment en RE.

Chaque idiome est présenté avec le code C d'origine, l'assembleur `-O0` (pour référence) et l'assembleur `-O2` (le pattern à reconnaître). Les exemples sont tirés de `gcc_idioms.c`.

---

## Idiome 1 — Division par constante : le magic number

C'est l'idiome le plus célèbre et le plus déroutant la première fois qu'on le rencontre. GCC remplace toute division par une constante connue par une multiplication suivie d'un décalage.

### Pourquoi

L'instruction `idiv` (division signée) est l'une des plus lentes du jeu x86-64 : entre 20 et 90 cycles selon le CPU et la taille des opérandes. À titre de comparaison, un `imul` prend 3 cycles. Remplacer une division par une multiplication est donc un gain considérable.

### Le principe mathématique

Pour calculer `x / N` avec N connu à la compilation, on utilise l'identité :

```
x / N  ≈  (x * M) >> S
```

où `M` (le *magic number*) et `S` (le *shift*) sont précalculés par le compilateur. `M` est l'inverse multiplicatif de `N` modulo 2^32 (ou 2^64), ajusté pour que le décalage compense l'erreur d'arrondi.

La référence pour ces calculs est le chapitre 10 de *Hacker's Delight* (Henry S. Warren Jr.).

### Exemple : division signée par 7

```c
int idiom_div_by_constant(int x)
{
    int a = x / 3;
    int b = x / 7;
    int c = x / 10;
    int d = x / 100;
    int e = x / 127;
    return a + b + c + d + e;
}
```

#### En `-O0`

```asm
    ; x / 7
    mov    eax, DWORD PTR [rbp-0x4]
    cdq                                  ; extension de signe → edx:eax
    mov    ecx, 7
    idiv   ecx                           ; eax = quotient, edx = reste
```

Simple et lisible : `idiv` avec le diviseur en registre.

#### En `-O2`

```asm
    ; x / 7 — magic number
    mov    eax, edi                      ; eax = x
    mov    edx, 0x92492493               ; magic number M pour /7
    imul   edx                           ; edx:eax = x * M (64 bits)
    add    edx, edi                      ; correction : edx += x
    sar    edx, 2                        ; shift arithmétique de 2
    mov    eax, edx
    shr    eax, 31                       ; extraction du bit de signe
    add    edx, eax                      ; correction pour les négatifs
    ; edx = x / 7
```

Le pattern se décompose en étapes :

1. **`imul edx` par le magic number** — multiplication 32×32→64 bits. Le résultat utile est dans `edx` (les 32 bits de poids fort du produit 64 bits).  
2. **Correction additive** (`add edx, edi`) — nécessaire pour certains diviseurs, pas pour tous.  
3. **`sar edx, S`** — décalage arithmétique vers la droite, qui achève la « division ».  
4. **Correction du signe** (`shr eax, 31` + `add`) — pour que le résultat soit correct pour les nombres négatifs (le décalage arithmétique arrondit vers -∞, alors que la division C arrondit vers zéro).

### Table des magic numbers courants

| Diviseur | Magic number (signé, 32 bits) | Shift | Correction additive |  
|---|---|---|---|  
| 3 | `0x55555556` | 0 | Non |  
| 5 | `0x66666667` | 1 | Non |  
| 7 | `0x92492493` | 2 | Oui (`add edx, edi`) |  
| 9 | `0x38E38E39` | 1 | Non |  
| 10 | `0x66666667` | 2 | Non |  
| 12 | `0x2AAAAAAB` | 1 | Non |  
| 100 | `0x51EB851F` | 5 | Non |  
| 127 | `0x02040811` | varie | Selon GCC |  
| 1000 | `0x10624DD3` | 6 | Non |

> 💡 Vous n'avez pas besoin de mémoriser cette table. L'important est de reconnaître le **pattern** : un `imul` par une grande constante hexadécimale suivie de `sar` = division par constante. Pour retrouver le diviseur, vous pouvez utiliser la formule inverse ou simplement tester : `0x66666667 * 42` >> 34 donne bien `42 / 10 = 4`.

### Division non signée

Pour les `unsigned int`, le pattern est légèrement différent : pas de correction de signe, et le magic number peut être différent.

```c
unsigned int idiom_udiv_by_constant(unsigned int x)
{
    return x / 10;
}
```

```asm
    ; unsigned x / 10
    mov    eax, edi
    mov    edx, 0xCCCCCCCD               ; magic number unsigned pour /10
    mul    edx                           ; mul (non signé) au lieu de imul
    shr    edx, 3                        ; shr (non signé) au lieu de sar
    ; edx = x / 10
```

Différences clés : `mul` au lieu de `imul`, `shr` au lieu de `sar`, et pas de correction de signe. Le magic number est aussi différent (`0xCCCCCCCD` vs `0x66666667`).

### Comment retrouver le diviseur en RE

Face à un magic number inconnu dans un binaire, deux approches :

**Approche empirique** — Testez quelques valeurs. Si `(x * MAGIC) >> S` donne `x / N` pour plusieurs `x`, vous avez trouvé `N`.

**Approche calculatoire** — Le diviseur N vérifie : `N ≈ 2^(32 + S) / M` (pour les non signés) ou une formule similaire avec correction pour les signés.

**Outil** — Le plugin Ghidra reconnaît automatiquement les magic numbers de division et affiche `x / N` dans le décompilateur. C'est l'une des raisons pour lesquelles le décompilateur est si utile en RE.

---

## Idiome 2 — Modulo par puissance de 2 : AND bitmask

```c
unsigned int idiom_umod_power_of_2(unsigned int x)
{
    return x % 8;
}
```

### En `-O2` (non signé)

```asm
    ; unsigned x % 8
    and    eax, 7                        ; masque les 3 bits de poids faible
    ; eax = x % 8
```

Pour un entier non signé, `x % 2^n` est strictement équivalent à `x & (2^n - 1)`. GCC applique cette substitution systématiquement. C'est l'idiome le plus simple à reconnaître : un `and` par `0x7`, `0xF`, `0x1F`, `0x3F`, `0xFF`, etc.

### En `-O2` (signé)

```c
int idiom_mod_power_of_2(int x)
{
    return x % 8;
}
```

Pour les entiers signés, le modulo C a le signe du dividende (`-13 % 8 = -5`), ce qui nécessite une correction :

```asm
    ; signed x % 8
    mov    eax, edi
    cdq                                  ; edx = 0 si x >= 0, 0xFFFFFFFF si x < 0
    ; Alternative : sar eax, 31 → même effet
    and    edx, 7                        ; masque = 7 si x < 0, 0 si x >= 0
    add    eax, edx                      ; ajuste pour les négatifs
    and    eax, 7                        ; masque final
    sub    eax, edx                      ; restaure le signe
    ; eax = x % 8 (signé)
```

Le pattern signé est plus long mais reste reconnaissable par la présence de deux `and` par la même constante avec un `cdq` ou `sar eax, 31` au milieu.

---

## Idiome 3 — Modulo par constante non-puissance de 2

```c
int idiom_mod_non_pow2(int x)
{
    return x % 7;
}
```

GCC calcule le modulo en deux étapes : d'abord la division `x / 7` par magic number (idiome 1), puis la soustraction `x - (x/7) * 7`.

### En `-O2`

```asm
    ; x % 7
    ; Étape 1 : calculer x / 7 (magic number)
    mov    eax, edi
    mov    edx, 0x92492493
    imul   edx
    add    edx, edi
    sar    edx, 2
    mov    eax, edx
    shr    eax, 31
    add    edx, eax                      ; edx = x / 7

    ; Étape 2 : x - (x/7) * 7
    lea    eax, [rdx+rdx*2]             ; eax = (x/7) * 3
    lea    eax, [rdx+rax*2]             ; eax = (x/7) + (x/7)*3*2 = (x/7) * 7
    ; (GCC utilise lea pour multiplier par 7 sans imul)
    sub    edi, eax                      ; edi = x - (x/7) * 7 = x % 7
```

Le modulo produit donc le pattern du magic number **plus** une multiplication par le diviseur (souvent via `lea`) et une soustraction finale.

---

## Idiome 4 — Multiplication par constante : lea / shl / add

GCC évite l'instruction `imul` quand il peut exprimer la multiplication par des combinaisons de `lea`, `shl` et `add`, qui sont plus rapides sur certains CPU.

```c
int idiom_mul_by_constant(int x)
{
    int a = x * 2;
    int b = x * 3;
    int c = x * 5;
    int d = x * 7;
    int e = x * 9;
    int f = x * 10;
    int g = x * 15;
    int h = x * 100;
    return a + b + c + d + e + f + g + h;
}
```

### Patterns en `-O2`

L'instruction `lea` (Load Effective Address) peut calculer `base + index * {1, 2, 4, 8}` en un seul cycle. GCC l'exploite pour les petits multiplicateurs :

```asm
    ; x * 2
    add    eax, eax                      ; ou : shl eax, 1
                                         ; ou : lea eax, [rdi+rdi]

    ; x * 3
    lea    eax, [rdi+rdi*2]             ; eax = x + x*2 = x*3

    ; x * 5
    lea    eax, [rdi+rdi*4]             ; eax = x + x*4 = x*5

    ; x * 7
    lea    eax, [rdi+rdi*2]             ; eax = x*3
    lea    eax, [rdi+rax*2]             ; eax = x + x*3*2 = x*7
    ; Variante : lea eax, [rdi*8] ; sub eax, edi  → x*8 - x = x*7

    ; x * 9
    lea    eax, [rdi+rdi*8]             ; eax = x + x*8 = x*9

    ; x * 10
    lea    eax, [rdi+rdi*4]             ; eax = x*5
    add    eax, eax                      ; eax = x*10

    ; x * 15
    lea    eax, [rdi+rdi*4]             ; eax = x*5
    lea    eax, [rax+rax*2]             ; eax = x*5 * 3 = x*15

    ; x * 100
    lea    eax, [rdi+rdi*4]             ; eax = x*5
    lea    eax, [rax+rax*4]             ; eax = x*25
    shl    eax, 2                        ; eax = x*100
```

### Tableau des multiplicateurs courants

| Multiplicateur | Pattern GCC | Logique |  
|---|---|---|  
| 2 | `add eax, eax` ou `shl eax, 1` | x + x ou x << 1 |  
| 3 | `lea [rdi+rdi*2]` | x + 2x |  
| 4 | `shl eax, 2` | x << 2 |  
| 5 | `lea [rdi+rdi*4]` | x + 4x |  
| 6 | `lea [rdi+rdi*2]` + `add eax, eax` | 3x × 2 |  
| 7 | `lea [rdi*8]` + `sub eax, edi` | 8x − x |  
| 8 | `shl eax, 3` | x << 3 |  
| 9 | `lea [rdi+rdi*8]` | x + 8x |  
| 10 | `lea [rdi+rdi*4]` + `add eax, eax` | 5x × 2 |

Pour les multiplicateurs plus grands ou qui ne se décomposent pas facilement, GCC utilise `imul eax, edi, constante` — un `imul` direct avec immédiat, qui reste rapide (3 cycles).

### Ce que le RE doit retenir

Quand vous voyez un `lea` avec un facteur d'échelle (1, 2, 4, 8) suivi d'un autre `lea` ou d'un `add`/`shl`, c'est une multiplication par constante décomposée. Recombinez les facteurs mentalement : `lea [rdi+rdi*4]` = ×5, `lea [rax+rax*2]` = résultat précédent × 3, etc.

---

## Idiome 5 — Conditional move (cmov) : le branchement éliminé

```c
int idiom_cmov_max(int a, int b)
{
    return (a > b) ? a : b;
}

int idiom_cmov_abs(int x)
{
    return (x < 0) ? -x : x;
}
```

### En `-O2`

```asm
idiom_cmov_max:
    cmp    edi, esi                      ; compare a et b
    mov    eax, esi                      ; eax = b (valeur par défaut)
    cmovg  eax, edi                     ; si a > b : eax = a
    ret

idiom_cmov_abs:
    mov    eax, edi                      ; eax = x
    neg    eax                           ; eax = -x
    cmovs  eax, edi                     ; si -x est négatif (x était positif) : eax = x
    ret
    ; Variante : test edi, edi ; cmovns au lieu de neg+cmovs
```

Le `cmov` (Conditional MOVe) effectue un `mov` conditionnel sans branchement. Le CPU évalue les **deux** valeurs possibles, puis choisit laquelle affecter au registre destination en fonction des flags.

### Variantes de cmov

| Instruction | Condition | Usage typique |  
|---|---|---|  
| `cmovg` / `cmovl` | > / < (signé) | `max(a, b)`, `min(a, b)` signés |  
| `cmova` / `cmovb` | > / < (non signé) | `max(a, b)`, `min(a, b)` non signés |  
| `cmove` / `cmovne` | == / != | Sélection basée sur l'égalité |  
| `cmovs` / `cmovns` | signe positif/négatif | `abs(x)` |  
| `cmovge` / `cmovle` | >= / <= | Clamp, saturation |

### Ce que le RE doit retenir

Un `cmp` + `cmovCC` est un if/else simple sans branchement. L'opérande destination du `cmov` est la valeur choisie si la condition est vraie ; le registre destination avant le `cmov` contient la valeur « else ». Reconstituez le ternaire : `result = (condition) ? valeur_cmov : valeur_précédente`.

---

## Idiome 6 — Test de bit : `test` + `setcc` / `jcc`

```c
int idiom_test_bit(int flags)
{
    int result = 0;
    if (flags & 0x01) result += 1;
    if (flags & 0x04) result += 10;
    if (flags & 0x80) result += 100;
    return result;
}
```

### En `-O2`

```asm
    xor    eax, eax                      ; result = 0
    test   dil, 1                        ; teste le bit 0
    jz     .L_skip1
    mov    eax, 1                        ; result = 1
.L_skip1:
    test   dil, 4                        ; teste le bit 2
    jz     .L_skip2
    add    eax, 10                       ; result += 10
.L_skip2:
    test   dil, 0x80                     ; teste le bit 7
    jz     .L_skip3
    add    eax, 100                      ; result += 100
.L_skip3:
    ret
```

L'instruction `test` effectue un AND logique sans stocker le résultat — elle ne modifie que les flags. `test reg, masque` suivi de `jz` est le pattern standard pour « si le bit N est positionné ».

Variante avec `setcc` quand le résultat est booléen :

```asm
    ; flag = (x & 4) != 0
    test   edi, 4
    setnz  al                            ; al = 1 si bit 2 positionné, 0 sinon
    movzx  eax, al                       ; extension 8→32 bits
```

### Ce que le RE doit retenir

Le pattern `test reg, constante_puissance_de_2` suivi de `jz`/`jnz` ou `setz`/`setnz` correspond à un test de bit dans le code source. La constante du `test` indique quel bit est testé : `1` = bit 0, `2` = bit 1, `4` = bit 2, `0x80` = bit 7, `0x100` = bit 8, etc. C'est un pattern omniprésent dans le code qui manipule des flags, des bitmasks, des registres matériel ou des permissions.

---

## Idiome 7 — Normalisation booléenne : `!!x`

```c
int idiom_bool_normalize(int x)
{
    return !!x;    /* Convertit toute valeur non-nulle en 1 */
}

int idiom_bool_from_compare(int a, int b)
{
    return (a == b);
}
```

### En `-O2`

```asm
idiom_bool_normalize:
    test   edi, edi                      ; x == 0 ?
    setne  al                            ; al = 1 si x != 0, 0 sinon
    movzx  eax, al                       ; extension 8→32 bits
    ret

idiom_bool_from_compare:
    cmp    edi, esi                      ; a == b ?
    sete   al                            ; al = 1 si a == b
    movzx  eax, al                       ; extension 8→32 bits
    ret
```

Le pattern `test`/`cmp` + `setCC` + `movzx` est la façon standard de GCC pour produire un résultat booléen 0 ou 1 à partir d'une condition. Les variantes courantes :

| Code C | Instructions |  
|---|---|  
| `!!x` | `test edi, edi` + `setne al` + `movzx` |  
| `x == 0` | `test edi, edi` + `sete al` + `movzx` |  
| `a == b` | `cmp edi, esi` + `sete al` + `movzx` |  
| `a < b` | `cmp edi, esi` + `setl al` + `movzx` |  
| `a >= b` | `cmp edi, esi` + `setge al` + `movzx` |

---

## Idiome 8 — Switch dense : la jump table

```c
const char *idiom_switch_dense(int day)
{
    switch (day) {
        case 0: return "Lundi";
        case 1: return "Mardi";
        case 2: return "Mercredi";
        case 3: return "Jeudi";
        case 4: return "Vendredi";
        case 5: return "Samedi";
        case 6: return "Dimanche";
        default: return "Inconnu";
    }
}
```

### En `-O2`

```asm
idiom_switch_dense:
    cmp    edi, 6
    ja     .L_default                    ; si day > 6 → "Inconnu"

    ; Jump table
    lea    rax, [rip+.L_jumptable]
    movsxd rdx, DWORD PTR [rax+rdi*4]   ; charge l'offset relatif
    add    rax, rdx
    jmp    rax                           ; saut vers le case correspondant

.L_case_0:
    lea    rax, [rip+.LC_lundi]          ; "Lundi"
    ret
.L_case_1:
    lea    rax, [rip+.LC_mardi]          ; "Mardi"
    ret
; ... etc.

.L_default:
    lea    rax, [rip+.LC_inconnu]
    ret

; Dans .rodata :
.L_jumptable:
    .long  .L_case_0 - .L_jumptable     ; offset relatif vers case 0
    .long  .L_case_1 - .L_jumptable     ; offset relatif vers case 1
    .long  .L_case_2 - .L_jumptable
    ; ... etc.
```

La jump table est un tableau d'offsets relatifs stocké dans `.rodata`. Le pattern de reconnaissance est le suivant, dans cet ordre :

1. `cmp edi, N` + `ja .L_default` — vérification des bornes.  
2. `lea rax, [rip+.L_jumptable]` — chargement de la base de la table.  
3. `movsxd rdx, [rax+rdi*4]` — lecture de l'offset indexé par la valeur du switch.  
4. `add rax, rdx` — calcul de l'adresse cible.  
5. `jmp rax` — saut indirect.

GCC génère une jump table quand les valeurs de case sont **denses** (peu de trous entre elles). Le seuil dépend de la version de GCC, mais en général, si plus de ~75% des valeurs dans la plage min–max ont un case, GCC préfère la jump table.

---

## Idiome 9 — Switch sparse : arbre de comparaisons

```c
const char *idiom_switch_sparse(int code)
{
    switch (code) {
        case 1:    return "START";
        case 7:    return "PAUSE";
        case 42:   return "ANSWER";
        case 100:  return "PERCENT";
        case 255:  return "MAX_BYTE";
        case 1000: return "KILO";
        default:   return "UNKNOWN";
    }
}
```

Les valeurs de case sont éloignées les unes des autres — une jump table de 1000 entrées pour 6 cases serait gaspilleuse.

### En `-O2`

GCC génère un **arbre binaire de comparaisons** — une sorte de recherche dichotomique :

```asm
idiom_switch_sparse:
    cmp    edi, 42
    je     .L_answer                     ; case 42
    jg     .L_upper_half                 ; code > 42 → chercher dans {100, 255, 1000}

    ; code < 42
    cmp    edi, 1
    je     .L_start                      ; case 1
    cmp    edi, 7
    je     .L_pause                      ; case 7
    jmp    .L_unknown                    ; default

.L_upper_half:
    cmp    edi, 100
    je     .L_percent                    ; case 100
    cmp    edi, 255
    je     .L_max_byte                   ; case 255
    cmp    edi, 1000
    je     .L_kilo                       ; case 1000
    jmp    .L_unknown                    ; default
```

GCC choisit une valeur pivot (ici 42) et divise les cases en deux groupes. Chaque groupe peut être subdivisé récursivement si nécessaire. Le nombre de comparaisons est O(log N) au lieu de O(N) pour une cascade linéaire.

### Ce que le RE doit retenir

Si vous voyez une cascade de `cmp`/`je` organisée en arbre (avec un `jg` ou `jl` initial qui sépare deux groupes), c'est un switch sparse. Si vous voyez un `lea` + `movsxd` + `jmp rax`, c'est un switch dense (jump table). La frontière entre les deux dépend de la densité des cases.

Ghidra reconstruit les deux patterns en switch/case dans le décompilateur.

---

## Idiome 10 — Rotation de bits : `rol` / `ror`

Le langage C n'a pas d'opérateur de rotation de bits. Les développeurs écrivent le pattern classique :

```c
unsigned int idiom_rotate_left(unsigned int x, int n)
{
    return (x << n) | (x >> (32 - n));
}

unsigned int idiom_rotate_left_13(unsigned int x)
{
    return (x << 13) | (x >> 19);
}
```

### En `-O0`

Le pattern est traduit littéralement — deux shifts et un OR :

```asm
    ; (x << n) | (x >> (32 - n))
    mov    eax, DWORD PTR [rbp-0x4]
    mov    ecx, DWORD PTR [rbp-0x8]
    shl    eax, cl                       ; x << n
    mov    edx, 32
    sub    edx, DWORD PTR [rbp-0x8]
    mov    ecx, edx
    mov    edx, DWORD PTR [rbp-0x4]
    shr    edx, cl                       ; x >> (32-n)
    or     eax, edx                      ; résultat
```

### En `-O2`

GCC **reconnaît le pattern** et le remplace par une seule instruction `rol` :

```asm
idiom_rotate_left:
    mov    eax, edi
    mov    ecx, esi
    rol    eax, cl                       ; rotation gauche de n bits
    ret

idiom_rotate_left_13:
    mov    eax, edi
    rol    eax, 13                       ; rotation gauche de 13 bits (immédiat)
    ret
```

Deux shifts et un OR deviennent un seul `rol`. Pour la rotation droite, `(x >> n) | (x << (32 - n))` est reconnu comme `ror`.

### Ce que le RE doit retenir

Les rotations sont omniprésentes dans les algorithmes cryptographiques (SHA-256, ChaCha20, RC5, MD5…) et les fonctions de hash. Si vous voyez un `rol` ou `ror` dans un binaire, c'est presque certainement un algorithme qui utilise des rotations de bits — cherchez dans l'Annexe J les constantes de rotation connues pour identifier l'algorithme.

Si vous analysez un binaire compilé en `-O0` et que vous voyez le pattern `shl` + `shr` + `or`, reconnaissez-le comme une rotation même sans le `rol`.

---

## Idiome 11 — Valeur absolue sans branchement

```c
int idiom_cmov_abs(int x)
{
    return (x < 0) ? -x : x;
}
```

### En `-O2`

GCC produit typiquement une des deux variantes suivantes.

**Variante `neg` + `cmov`** (la plus courante sur les GCC récents) :

```asm
    mov    eax, edi                      ; eax = x
    neg    eax                           ; eax = -x (positionne SF si x > 0)
    cmovs  eax, edi                     ; si -x < 0 (i.e. x > 0) : eax = x
    ret
```

**Variante arithmétique** `sar` + `xor` + `sub` (vue dans les anciens GCC ou en écrivant le pattern manuellement) :

```c
int idiom_abs_manual(int x)
{
    int mask = x >> 31;
    return (x ^ mask) - mask;
}
```

```asm
    mov    eax, edi
    sar    eax, 31                       ; eax = 0x00000000 si x >= 0
                                         ;      = 0xFFFFFFFF si x < 0
    xor    edi, eax                      ; si x < 0 : inverse tous les bits (complément à 1)
    sub    edi, eax                      ; si x < 0 : ajoute 1 (complément à 2 = -x)
                                         ; si x >= 0 : pas d'effet (xor 0, sub 0)
    mov    eax, edi
    ret
```

Le `sar eax, 31` produit un masque : tout à 0 si positif, tout à 1 si négatif. Le `xor` + `sub` avec ce masque est l'équivalent sans branchement de la négation conditionnelle.

### Ce que le RE doit retenir

Les deux patterns (`neg` + `cmovs` et `sar 31` + `xor` + `sub`) calculent `abs(x)`. Le premier est plus lisible ; le second apparaît souvent dans le code cryptographique ou DSP où les branchements sont proscrits.

---

## Idiome 12 — Min / Max sans branchement

```c
int idiom_min(int a, int b) { return (a < b) ? a : b; }  
int idiom_max(int a, int b) { return (a > b) ? a : b; }  
unsigned int idiom_umin(unsigned int a, unsigned int b) { return (a < b) ? a : b; }  
```

### En `-O2`

```asm
idiom_min:
    cmp    edi, esi
    mov    eax, esi
    cmovl  eax, edi                     ; si a < b : eax = a, sinon eax = b
    ret

idiom_max:
    cmp    edi, esi
    mov    eax, esi
    cmovg  eax, edi                     ; si a > b : eax = a, sinon eax = b
    ret

idiom_umin:
    cmp    edi, esi
    mov    eax, esi
    cmovb  eax, edi                     ; cmovb = "below" (comparaison non signée)
    ret
```

La différence entre `cmovl` (signé) et `cmovb` (non signé) est un indice précieux en RE : elle révèle si les variables sont traitées comme `int` ou `unsigned int` dans le code source.

---

## Idiome 13 — Initialisation de structure : `rep stosq` ou séquence de `mov`

```c
Record r;  
memset(&r, 0, sizeof(r));  
r.id = id;  
r.type = 1;  
r.value = 3.14159;  
r.flags = 0x0F;  
```

### En `-O2`

Pour les structures de taille modérée, GCC émet une séquence de `mov` immédiats :

```asm
    ; Mise à zéro + initialisation des champs
    mov    DWORD PTR [rsp],    edi       ; r.id = id
    mov    DWORD PTR [rsp+4],  1         ; r.type = 1
    movsd  xmm0, QWORD PTR [rip+.LC_pi] ; charge 3.14159 depuis .rodata
    movsd  QWORD PTR [rsp+8], xmm0      ; r.value = 3.14159
    ; ... mise à zéro des champs name[32] ...
    mov    DWORD PTR [rsp+48], 0x0F      ; r.flags = 0x0F
    mov    DWORD PTR [rsp+52], 0         ; r.padding = 0
```

Pour les structures plus grandes (> ~128 octets), GCC utilise `rep stosq` — une instruction qui remplit un bloc mémoire en écrivant `rcx` fois la valeur dans `rax` :

```asm
    ; Mise à zéro d'une grande structure (ou tableau)
    lea    rdi, [rsp+offset]             ; adresse destination
    xor    eax, eax                      ; valeur = 0
    mov    ecx, N                        ; nombre de qwords (8 octets)
    rep    stosq                          ; remplit N × 8 octets avec 0
```

### Ce que le RE doit retenir

Une séquence de `mov DWORD/QWORD PTR [rsp+offsets], constantes` est une initialisation de structure. Les offsets révèlent la disposition (layout) des champs : `[rsp+0]` est le premier champ, `[rsp+4]` le deuxième si le premier fait 4 octets, etc. Vous pouvez reconstruire la structure en notant les offsets et les tailles des stores.

Le `rep stosq` indique un `memset` (souvent à zéro). Il peut provenir d'un appel `memset()` explicite ou d'une boucle de mise à zéro reconnue par le compilateur (cf. section 16.3).

---

## Idiome 14 — Comparaison de chaînes courtes inline

```c
int idiom_strcmp_known(const char *input)
{
    if (strcmp(input, "OK") == 0)     return 1;
    if (strcmp(input, "FAIL") == 0)   return 2;
    if (strcmp(input, "ERROR") == 0)  return 3;
    return 0;
}
```

### En `-O2`

Quand la chaîne de comparaison est courte et connue à la compilation, GCC peut inliner le `strcmp` en chargeant la chaîne comme un entier et en faisant une comparaison numérique :

```asm
    ; strcmp(input, "OK") == 0
    ; "OK" = 0x4B4F en little-endian (2 octets + null)
    movzx  eax, WORD PTR [rdi]          ; charge 2 octets de input
    cmp    ax, 0x4B4F                    ; compare avec "OK"
    jne    .L_not_ok
    cmp    BYTE PTR [rdi+2], 0          ; vérifie le null terminator
    je     .L_return_1

.L_not_ok:
    ; strcmp(input, "FAIL") == 0
    ; "FAIL" = 0x4C494146 en little-endian (4 octets)
    mov    eax, DWORD PTR [rdi]          ; charge 4 octets
    cmp    eax, 0x4C494146               ; compare avec "FAIL"
    jne    .L_not_fail
    cmp    BYTE PTR [rdi+4], 0          ; null terminator
    je     .L_return_2
```

Au lieu d'appeler `strcmp@plt` (qui boucle octet par octet), GCC charge 2 ou 4 octets d'un coup et les compare comme un entier. Le `cmp BYTE PTR [rdi+N], 0` vérifie que la chaîne se termine bien là (pas de continuation après le match).

### Ce que le RE doit retenir

Quand vous voyez un `cmp eax, 0x4C494146` dans un binaire, ne cherchez pas un magic number de division ou de hash — c'est probablement une comparaison de chaîne inline. Convertissez la constante en ASCII (attention à l'endianness little-endian) : `0x4C494146` → octets `46 41 49 4C` → « FAIL ».

Ce pattern est très courant dans les parseurs de commandes, les vérificateurs de mot de passe simples, et les machines à états de protocoles réseau.

---

## Idiome 15 — Population count (popcount) : `popcnt`

```c
int idiom_popcount(unsigned int x)
{
    return __builtin_popcount(x);
}
```

### Avec `-mpopcnt` (CPU moderne)

```asm
    popcnt eax, edi                      ; compte le nombre de bits à 1
    ret
```

Une seule instruction. `popcnt` est disponible depuis Nehalem (2008) sur Intel et Barcelona (2007) sur AMD.

### Sans `-mpopcnt` (fallback logiciel)

GCC émet le calcul de Hamming weight avec les constantes magiques caractéristiques :

```asm
    ; Hamming weight — constantes reconnaissables
    mov    eax, edi
    shr    eax, 1
    and    eax, 0x55555555               ; masque : bits pairs
    sub    edi, eax
    mov    eax, edi
    shr    eax, 2
    and    eax, 0x33333333               ; masque : paires de bits
    and    edi, 0x33333333
    add    edi, eax
    mov    eax, edi
    shr    eax, 4
    add    eax, edi
    and    eax, 0x0F0F0F0F               ; masque : nibbles
    imul   eax, eax, 0x01010101          ; somme horizontale via multiplication
    shr    eax, 24                       ; résultat dans les 8 bits de poids fort
```

Les constantes `0x55555555`, `0x33333333`, `0x0F0F0F0F` et `0x01010101` sont la signature du Hamming weight. Si vous les voyez ensemble dans un binaire, c'est un comptage de bits.

### Ce que le RE doit retenir

L'instruction `popcnt` ou la séquence de constantes `0x55555555` / `0x33333333` / `0x0F0F0F0F` identifient un comptage de bits. Ce pattern apparaît dans les implémentations de sets en bitmap, les calculs de distance de Hamming, les algorithmes de compression, et certaines fonctions de hash.

---

## Idiome 16 — Extension de signe et de zéro : `movsx` / `movzx`

```c
int idiom_sign_extend(char c) { return (int)c; }  
int idiom_zero_extend(unsigned char c) { return (int)c; }  
```

### En `-O2`

```asm
idiom_sign_extend:
    movsx  eax, dil                      ; extension de signe 8→32 bits
    ret                                   ; 0xFE → 0xFFFFFFFE (-2)

idiom_zero_extend:
    movzx  eax, dil                      ; extension de zéro 8→32 bits
    ret                                   ; 0xFE → 0x000000FE (254)
```

| Instruction | Signification | Cas d'usage |  
|---|---|---|  
| `movsx eax, dil` | Sign-extend 8→32 | Cast `char` → `int` |  
| `movsx eax, di` | Sign-extend 16→32 | Cast `short` → `int` |  
| `movsxd rax, edi` | Sign-extend 32→64 | Cast `int` → `long` |  
| `movzx eax, dil` | Zero-extend 8→32 | Cast `unsigned char` → `int` |  
| `movzx eax, di` | Zero-extend 16→32 | Cast `unsigned short` → `int` |

Le choix entre `movsx` et `movzx` révèle si le type source est signé ou non signé dans le code C — c'est une information de typage précieuse en RE.

### Ce que le RE doit retenir

`movsx` = type source signé (`char`, `short`, `int`). `movzx` = type source non signé (`unsigned char`, `unsigned short`). Un `movsxd rax, edi` indique une conversion `int` → `long` (32→64 bits), souvent vue avant un accès indexé à un tableau en 64 bits.

---

## Récapitulatif : carte de référence rapide

| Pattern assembleur | Idiome | Code C correspondant |  
|---|---|---|  
| `imul edx, MAGIC` + `sar edx, S` + correction signe | Division par constante | `x / N` |  
| `and eax, (2^n - 1)` | Modulo puissance de 2 | `x % 2^n` (unsigned) |  
| magic number + `imul` + `sub` | Modulo non-puissance de 2 | `x % N` |  
| `lea [rdi+rdi*{2,4,8}]` | Multiplication par petite constante | `x * {3,5,9}` |  
| `cmp` + `cmovCC` | Branchement éliminé | `(cond) ? a : b`, `min`, `max`, `abs` |  
| `test reg, masque` + `jz`/`setnz` | Test de bit | `if (flags & BIT)` |  
| `test`/`cmp` + `setCC` + `movzx` | Normalisation booléenne | `!!x`, `a == b`, `a < b` |  
| `lea` + `movsxd [table+idx*4]` + `jmp rax` | Jump table (switch dense) | `switch (x) { case 0..N: }` |  
| Arbre de `cmp`/`je`/`jg` | Switch sparse | `switch (x) { case 1, 42, 1000: }` |  
| `rol eax, N` / `ror eax, N` | Rotation de bits | `(x << n) \| (x >> (32-n))` |  
| `neg` + `cmovs` ou `sar 31` + `xor` + `sub` | Valeur absolue | `abs(x)` |  
| `cmp` + `cmovl`/`cmovg` | Min / Max | `min(a,b)`, `max(a,b)` |  
| séquence `mov [rsp+off], imm` | Initialisation de structure | `struct s = {...}` |  
| `cmp eax, 0xASCII` + `cmp byte [rdi+N], 0` | strcmp inline | `strcmp(s, "court")` |  
| `popcnt` ou `0x55555555`/`0x33333333` | Comptage de bits | `__builtin_popcount(x)` |  
| `movsx` / `movzx` | Extension de type | Cast `char→int`, `short→int` |

---

## Conseils pour la pratique quotidienne du RE

**Construisez votre mémoire musculaire.** Plus vous analysez de binaires, plus ces patterns deviennent automatiques. Les premiers `0x92492493` que vous rencontrez sont mystérieux ; après en avoir vu dix, vous les identifiez instantanément comme une division par 7.

**Laissez le décompilateur faire le travail quand c'est possible.** Ghidra reconnaît la plupart de ces idiomes et les traduit en C lisible dans le décompilateur. Mais le décompilateur peut se tromper — la lecture directe du désassemblage reste la compétence fondamentale.

**Attention à la version de GCC.** Les patterns exacts varient entre les versions de GCC. Un `abs()` peut être `neg` + `cmovs` sur GCC 12 et `sar` + `xor` + `sub` sur GCC 8. Le code source est le même, mais l'assembleur diffère. Compiler Explorer ([godbolt.org](https://godbolt.org)) permet de vérifier le pattern pour une version spécifique.

**Documentez vos découvertes.** L'Annexe I du tutoriel fournit un tableau de référence étendu des patterns GCC. Enrichissez-le avec vos propres observations au fil de vos analyses.

---


⏭️ [Comparaison GCC vs Clang : différences de patterns à l'assembleur](/16-optimisations-compilateur/07-gcc-vs-clang.md)
