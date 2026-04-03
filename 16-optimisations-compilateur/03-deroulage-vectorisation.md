🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 16.3 — Déroulage de boucles et vectorisation (SIMD/SSE/AVX)

> **Fichier source associé** : `binaries/ch16-optimisations/loop_unroll_vec.c`  
> **Compilation** : `make s16_3` (produit 7 variantes dans `build/`, incluant `_O3_avx2`)

---

## Introduction

Les boucles sont le cœur de la plupart des programmes : parcours de tableaux, accumulations, transformations de données, algorithmes de tri, parseurs de protocoles. Elles sont aussi la cible privilégiée des optimisations les plus agressives de GCC, car c'est dans les boucles que se concentre le temps d'exécution.

Deux familles de transformations s'appliquent aux boucles à partir de `-O2` :

- Le **déroulage** (*loop unrolling*) : répliquer le corps de la boucle N fois pour réduire le nombre d'itérations et le coût des comparaisons/sauts.  
- La **vectorisation** (*auto-vectorization*) : regrouper plusieurs itérations scalaires en une seule opération SIMD, en exploitant les registres 128 bits (SSE, `xmm0`–`xmm15`) ou 256 bits (AVX, `ymm0`–`ymm15`) du processeur.

Ces deux transformations produisent un assembleur qui ne ressemble plus du tout à la boucle C d'origine. Le corps de la boucle peut être multiplié par 4 ou 8, des instructions que vous n'avez jamais vues dans du code `-O0` apparaissent (`paddd`, `vpmulld`, `vmovdqu`…), et la structure de contrôle se fragmente en trois sous-boucles (prologue, corps principal vectorisé, épilogue).

Pour le reverse engineer, comprendre ces transformations est indispensable pour analyser tout binaire compilé en `-O2` ou `-O3` qui manipule des tableaux — c'est-à-dire la quasi-totalité des programmes réels.

---

## Rappel : les registres SIMD

Avant de plonger dans le désassemblage, un bref rappel de l'architecture SIMD x86-64 est nécessaire. Le chapitre 3 (section 3.9) les introduit ; nous allons ici les rencontrer en contexte.

### SSE (Streaming SIMD Extensions)

SSE utilise les registres `xmm0` à `xmm15`, chacun faisant 128 bits. Un registre `xmm` peut contenir simultanément 4 entiers 32 bits (ou 2 entiers 64 bits, 4 flottants simple précision, 2 double précision). Les instructions SSE opèrent sur les 4 valeurs en parallèle.

Les instructions SSE les plus fréquentes en RE de boucles sur des entiers 32 bits :

| Instruction | Signification | Équivalent scalaire |  
|---|---|---|  
| `movdqa xmm0, [mem]` | Charge 4 int alignés | `mov eax, [mem]` × 4 |  
| `movdqu xmm0, [mem]` | Charge 4 int non alignés | idem, sans contrainte d'alignement |  
| `paddd xmm0, xmm1` | Addition parallèle 4 × 32 bits | `add eax, ecx` × 4 |  
| `psubd xmm0, xmm1` | Soustraction parallèle | `sub eax, ecx` × 4 |  
| `pmulld xmm0, xmm1` | Multiplication parallèle (SSE4.1) | `imul eax, ecx` × 4 |  
| `pxor xmm0, xmm1` | XOR parallèle | `xor eax, ecx` × 4 |  
| `pcmpgtd xmm0, xmm1` | Comparaison > parallèle (masque) | `cmp eax, ecx` × 4 |  
| `movdqa [mem], xmm0` | Stocke 4 int alignés | `mov [mem], eax` × 4 |

### AVX / AVX2

AVX étend les registres à 256 bits (`ymm0`–`ymm15`), permettant de traiter 8 entiers 32 bits simultanément. Les instructions AVX sont préfixées par `v` : `vpaddd`, `vpmulld`, `vmovdqu`, etc.

AVX2 (introduit avec Haswell en 2013) ajoute le support des opérations entières 256 bits. Sans AVX2, les `ymm` ne servent qu'aux flottants.

Pour le RE, la présence d'instructions `vmov...`, `vpadd...`, `vpmul...` avec des registres `ymm` indique un binaire compilé avec `-mavx2` (ou `-march=native` sur un CPU compatible). C'est un indice sur l'environnement de compilation.

---

## Le déroulage de boucles (loop unrolling)

Le déroulage est la transformation la plus simple à comprendre : au lieu d'exécuter le corps de la boucle une fois par itération avec un `cmp` + `jmp` à chaque tour, GCC réplique le corps N fois et n'effectue le test qu'une fois tous les N tours.

### Pourquoi dérouler ?

Chaque itération de boucle comporte un coût fixe incompressible :

1. Incrémenter le compteur (`add edx, 1`).  
2. Comparer avec la borne (`cmp edx, ecx`).  
3. Effectuer le saut conditionnel (`jle .L_loop`).

Sur un CPU moderne, ces trois instructions prennent ~1 cycle, mais le branchement conditionnel peut causer un *branch misprediction* (~15 cycles de pénalité). En déroulant par 4, on divise le nombre de branchements par 4.

De plus, le déroulage ouvre des opportunités pour d'autres optimisations : le CPU peut exécuter les instructions de deux itérations en parallèle (instruction-level parallelism), et le compilateur peut réutiliser des valeurs communes entre itérations adjacentes.

### Anatomie d'une boucle déroulée

Prenons la fonction `vec_add` de notre fichier source :

```c
static void vec_add(int *dst, const int *a, const int *b, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = a[i] + b[i];
    }
}
```

#### En `-O0` — la boucle littérale

```asm
vec_add:
    push   rbp
    mov    rbp, rsp
    mov    QWORD PTR [rbp-0x8], rdi      ; dst
    mov    QWORD PTR [rbp-0x10], rsi     ; a
    mov    QWORD PTR [rbp-0x18], rdx     ; b
    mov    DWORD PTR [rbp-0x1c], ecx     ; n

    mov    DWORD PTR [rbp-0x20], 0       ; i = 0

.L_check:
    mov    eax, DWORD PTR [rbp-0x20]
    cmp    eax, DWORD PTR [rbp-0x1c]
    jge    .L_end

    ; Corps : dst[i] = a[i] + b[i]
    mov    eax, DWORD PTR [rbp-0x20]     ; charge i
    cdqe
    lea    rdx, [rax*4]
    mov    rax, QWORD PTR [rbp-0x10]     ; charge a
    add    rax, rdx
    mov    ecx, DWORD PTR [rax]          ; ecx = a[i]

    mov    eax, DWORD PTR [rbp-0x20]     ; charge i (encore !)
    cdqe
    lea    rdx, [rax*4]
    mov    rax, QWORD PTR [rbp-0x18]     ; charge b
    add    rax, rdx
    mov    eax, DWORD PTR [rax]          ; eax = b[i]

    add    ecx, eax                       ; ecx = a[i] + b[i]

    mov    eax, DWORD PTR [rbp-0x20]     ; charge i (une 3e fois !)
    cdqe
    lea    rdx, [rax*4]
    mov    rax, QWORD PTR [rbp-0x8]      ; charge dst
    add    rax, rdx
    mov    DWORD PTR [rax], ecx          ; dst[i] = résultat

    add    DWORD PTR [rbp-0x20], 1       ; i++
    jmp    .L_check

.L_end:
    pop    rbp
    ret
```

Le compteur `i` est rechargé depuis la pile **trois fois** par itération (pour calculer `a[i]`, `b[i]`, `dst[i]`). C'est le « pire cas » en termes de performances, mais c'est parfaitement lisible pour le RE : on voit immédiatement la structure de la boucle et le calcul effectué.

#### En `-O2` — déroulage sans vectorisation

En `-O2`, GCC optimise la boucle avec des registres et peut la dérouler partiellement (typiquement par 2 ou 4), mais la vectorisation n'est pas toujours activée (elle dépend de `-ftree-vectorize`, actif par défaut en `-O2` depuis GCC 12, mais pas dans les versions antérieures) :

```asm
    ; Boucle déroulée par 2
    ; rdi = dst, rsi = a, rdx = b, ecx = n
    xor    eax, eax                       ; i = 0
    test   ecx, ecx
    jle    .L_done

.L_loop:
    ; Itération i
    mov    r8d, DWORD PTR [rsi+rax*4]    ; r8d = a[i]
    add    r8d, DWORD PTR [rdx+rax*4]    ; r8d += b[i]
    mov    DWORD PTR [rdi+rax*4], r8d    ; dst[i] = résultat

    ; Itération i+1 (déroulée)
    mov    r8d, DWORD PTR [rsi+rax*4+4]  ; r8d = a[i+1]
    add    r8d, DWORD PTR [rdx+rax*4+4]  ; r8d += b[i+1]
    mov    DWORD PTR [rdi+rax*4+4], r8d  ; dst[i+1] = résultat

    add    rax, 2                         ; i += 2
    cmp    eax, ecx
    jl     .L_loop

    ; Épilogue : si n est impair, traiter le dernier élément
    ; ...

.L_done:
```

La signature du déroulage est claire : le compteur `rax` est incrémenté de 2 au lieu de 1, et le corps contient deux groupes d'instructions identiques avec des offsets décalés de 4 octets (`+0` et `+4`).

Notez l'**épilogue** : quand `n` n'est pas un multiple du facteur de déroulage, une petite boucle ou une séquence linéaire traite les éléments restants. En `-O0`, il n'y a pas d'épilogue — la boucle scalaire traite tout élément par élément.

---

## La vectorisation automatique

La vectorisation est un niveau au-dessus du déroulage : au lieu de traiter les itérations séquentiellement (même déroulées), le compilateur les exécute **en parallèle** en utilisant les registres SIMD.

### Conditions pour la vectorisation

GCC ne vectorise une boucle que si **toutes** ces conditions sont remplies :

**1. Pas de dépendance inter-itérations.** Si l'itération `i` dépend du résultat de l'itération `i-1`, les itérations ne peuvent pas être exécutées en parallèle. C'est la raison la plus courante d'échec de vectorisation.

**2. Opérations supportées par le jeu SIMD.** L'opération dans le corps de la boucle doit avoir un équivalent SIMD. L'addition, la soustraction, la multiplication, le XOR, les comparaisons sont supportés. La division entière, les appels de fonctions arbitraires, les accès mémoire non séquentiels ne le sont pas (ou difficilement).

**3. Accès mémoire séquentiels.** Les données doivent être accédées de manière contiguë (`a[i]`, `a[i+1]`, `a[i+2]`, ...). Un accès strided (`a[i*stride]`) complique la vectorisation, et un accès indirect (`a[index[i]]`) l'empêche.

**4. Pas d'aliasing non résolu.** Le compilateur doit être sûr que les zones mémoire en lecture et écriture ne se chevauchent pas. Le mot-clé `restrict` (C99) fournit cette garantie au compilateur.

### `vec_add` en `-O3` — vectorisation SSE

Reprenons `vec_add` compilée en `-O3` (ou `-O2` avec `-ftree-vectorize`) :

```asm
vec_add:                                  ; inlinée dans main() en pratique
    ; Vérification : n >= 4 ?
    cmp    ecx, 3
    jle    .L_scalar                      ; trop petit → boucle scalaire

    ; Boucle vectorisée — 4 int par itération (SSE 128 bits)
    xor    eax, eax                       ; i = 0

.L_vec_loop:
    movdqu xmm0, XMMWORD PTR [rsi+rax]   ; xmm0 = a[i..i+3]  (4 entiers)
    movdqu xmm1, XMMWORD PTR [rdx+rax]   ; xmm1 = b[i..i+3]
    paddd  xmm0, xmm1                    ; xmm0 = a[i..i+3] + b[i..i+3]
    movdqu XMMWORD PTR [rdi+rax], xmm0   ; dst[i..i+3] = résultat
    add    rax, 16                        ; i += 4 (4 int × 4 octets = 16)
    cmp    eax, r8d                       ; r8d = n arrondi au multiple de 4
    jl     .L_vec_loop

    ; Épilogue : traiter les 0–3 éléments restants (scalaire)
.L_epilogue:
    ; ... boucle scalaire pour le reste ...

.L_scalar:
    ; Boucle scalaire classique pour n < 4
    ; ...
```

C'est ici que le désassemblage change radicalement d'apparence. Décortiquons les éléments clés.

**`movdqu xmm0, [rsi+rax]`** — Charge 128 bits (4 entiers de 32 bits) depuis le tableau `a` dans le registre `xmm0`. L'instruction `movdqu` (Move Double Quadword Unaligned) fonctionne même si l'adresse n'est pas alignée sur 16 octets. Si GCC peut prouver l'alignement, il utilise `movdqa` (Aligned), qui est légèrement plus rapide sur les anciens CPU.

**`paddd xmm0, xmm1`** — Addition parallèle de 4 entiers 32 bits : `xmm0[0] += xmm1[0]`, `xmm0[1] += xmm1[1]`, `xmm0[2] += xmm1[2]`, `xmm0[3] += xmm1[3]`. Une seule instruction remplace 4 `add` scalaires.

**`add rax, 16`** — Le compteur avance de 16 octets (= 4 entiers × 4 octets/entier) à chaque tour.

**L'épilogue** — Si `n` n'est pas un multiple de 4, les 1 à 3 éléments restants sont traités par une petite boucle scalaire. C'est la partie souvent la plus confuse dans le désassemblage, car elle crée un deuxième chemin de code après la boucle principale.

### `vec_add` en `-O3 -mavx2` — vectorisation AVX

Avec `-mavx2`, les registres passent à 256 bits (`ymm`), traitant 8 entiers par itération :

```asm
.L_vec_loop_avx:
    vmovdqu ymm0, YMMWORD PTR [rsi+rax]  ; ymm0 = a[i..i+7]  (8 entiers)
    vpaddd  ymm0, ymm0, [rdx+rax]        ; ymm0 += b[i..i+7]
    vmovdqu YMMWORD PTR [rdi+rax], ymm0  ; dst[i..i+7] = résultat
    add     rax, 32                       ; i += 8 (8 int × 4 octets = 32)
    cmp     eax, r8d
    jl      .L_vec_loop_avx
```

Le pattern est identique à SSE, mais avec `ymm` au lieu de `xmm`, `vmovdqu` au lieu de `movdqu`, `vpaddd` au lieu de `paddd`, et un pas de 32 au lieu de 16.

Pour le RE, la différence SSE vs AVX se reconnaît immédiatement aux préfixes (`v` pour AVX) et aux noms de registres (`ymm` vs `xmm`). C'est un indice sur le CPU cible et les flags de compilation.

---

## La structure en trois parties d'une boucle vectorisée

Un pattern récurrent dans le désassemblage des boucles vectorisées en `-O3` est la structure en trois parties. C'est l'un des patterns les plus importants à reconnaître en RE.

### 1. Le prologue (alignement)

Si les données ne sont pas alignées sur la taille du registre SIMD (16 octets pour SSE, 32 pour AVX), GCC peut ajouter une boucle de prologue qui traite les premiers éléments en scalaire jusqu'à atteindre une adresse alignée.

```asm
    ; Prologue — traiter les éléments jusqu'à l'alignement
    test   rdi, 0xF                      ; dst aligné sur 16 octets ?
    jz     .L_main_loop                  ; oui → skip le prologue
.L_prologue:
    mov    eax, DWORD PTR [rsi+rcx*4]
    add    eax, DWORD PTR [rdx+rcx*4]
    mov    DWORD PTR [rdi+rcx*4], eax
    add    rcx, 1
    test   rdi, 0xF                      ; rétest l'alignement
    jnz    .L_prologue
```

En pratique, le prologue est souvent absent si GCC utilise `movdqu` (non aligné), ce qui est le cas courant sur les CPU modernes (Haswell et ultérieurs) où le coût d'un accès non aligné est négligeable.

### 2. Le corps principal (vectorisé)

C'est la boucle SIMD que nous avons vue ci-dessus : `movdqu` + opération SIMD + `movdqu` + incrément de 16/32.

### 3. L'épilogue (reste)

Après la boucle principale, les éléments restants (quand `n` n'est pas un multiple du facteur de vectorisation) sont traités en scalaire :

```asm
    ; Épilogue — éléments restants
    cmp    eax, ecx                      ; il reste des éléments ?
    jge    .L_done
.L_epilogue:
    mov    r8d, DWORD PTR [rsi+rax*4]
    add    r8d, DWORD PTR [rdx+rax*4]
    mov    DWORD PTR [rdi+rax*4], r8d
    add    eax, 1
    cmp    eax, ecx
    jl     .L_epilogue
.L_done:
```

Cette structure prologue/corps/épilogue est souvent ce qui rend le désassemblage d'une boucle `-O3` intimidant au premier abord : une boucle de 3 lignes en C se transforme en 30–50 lignes d'assembleur avec trois sous-boucles et des branchements entre elles. Mais une fois le pattern reconnu, la lecture devient systématique : identifier la boucle principale (celle avec les `xmm`/`ymm`), comprendre l'opération SIMD, ignorer le prologue et l'épilogue.

---

## Réduction (accumulation) : le cas du dot product

Les boucles avec un accumulateur unique (somme, produit, min, max) posent un défi particulier pour la vectorisation : les itérations ne sont pas indépendantes puisque chacune lit et écrit le même accumulateur.

```c
static long dot_product(const int *a, const int *b, int n)
{
    long sum = 0;
    for (int i = 0; i < n; i++) {
        sum += (long)a[i] * (long)b[i];
    }
    return sum;
}
```

### La solution : accumulateur vectoriel + réduction horizontale

GCC vectorise cette boucle en utilisant un **accumulateur vectoriel** — un registre `xmm` qui contient 2 (ou 4) sommes partielles en parallèle. À la fin de la boucle, les sommes partielles sont additionnées en une seule valeur par une **réduction horizontale**.

```asm
    ; Corps vectorisé (simplifié)
    pxor     xmm2, xmm2                  ; accumulateur vectoriel = {0, 0}

.L_vec_loop:
    movdqu   xmm0, XMMWORD PTR [rsi+rax] ; charge a[i..i+3]
    movdqu   xmm1, XMMWORD PTR [rdx+rax] ; charge b[i..i+3]

    ; Multiplication avec extension 32→64 bits
    ; (les opérations exactes dépendent de la version GCC)
    pmuludq  xmm0, xmm1                  ; multiplication 32×32→64 (2 résultats)
    paddq    xmm2, xmm0                  ; accumulateur += résultats

    add      rax, 16
    cmp      eax, r8d
    jl       .L_vec_loop

    ; Réduction horizontale — sommer les 2 éléments de xmm2
    movhlps  xmm0, xmm2                  ; xmm0 = partie haute de xmm2
    paddq    xmm2, xmm0                  ; xmm2[0] = somme totale
    movq     rax, xmm2                   ; résultat scalaire dans rax
```

Les instructions de réduction horizontale varient selon le type de données. Pour les entiers 32 bits, on voit souvent une séquence `pshufd` + `paddd` (ou `phaddd` si SSSE3 est disponible). Pour les 64 bits, `movhlps` + `paddq`.

### Ce que le RE doit retenir

Si vous voyez un registre `xmm` initialisé à zéro (`pxor xmm, xmm`), puis accumulé dans une boucle (`paddq` ou `paddd`), suivi d'une séquence de `pshufd`/`movhlps` + additions, c'est une **réduction vectorisée**. Le code C original contient un accumulateur (`sum += ...`).

---

## Déroulage complet d'une boucle à bornes constantes

Quand le nombre d'itérations est connu à la compilation et qu'il est petit, GCC peut **dérouler complètement** la boucle — aucune instruction de contrôle (`cmp`, `jmp`) ne subsiste.

```c
static void fixed_size_init(int arr[16])
{
    for (int i = 0; i < 16; i++) {
        arr[i] = i * i + 1;
    }
}
```

### En `-O0`

Boucle classique avec compteur sur la pile, 16 itérations avec `cmp`/`jge`/`jmp`.

### En `-O2` / `-O3`

GCC évalue `i * i + 1` pour chaque `i` de 0 à 15 à la compilation et produit une séquence de stores immédiats :

```asm
    ; Déroulage complet — aucune boucle
    mov    DWORD PTR [rdi],    1         ; arr[0]  = 0*0+1 = 1
    mov    DWORD PTR [rdi+4],  2         ; arr[1]  = 1*1+1 = 2
    mov    DWORD PTR [rdi+8],  5         ; arr[2]  = 2*2+1 = 5
    mov    DWORD PTR [rdi+12], 10        ; arr[3]  = 3*3+1 = 10
    mov    DWORD PTR [rdi+16], 17        ; arr[4]  = 4*4+1 = 17
    mov    DWORD PTR [rdi+20], 26        ; arr[5]  = 5*5+1 = 26
    ; ... etc. jusqu'à arr[15]
    mov    DWORD PTR [rdi+60], 226       ; arr[15] = 15*15+1 = 226
```

Ou, si les valeurs tiennent dans un registre SIMD, GCC peut charger un vecteur de constantes depuis `.rodata` et faire un seul `movdqa` pour 4 valeurs à la fois :

```asm
    ; Variante vectorisée du déroulage complet
    movdqa xmm0, XMMWORD PTR [rip+.LC0] ; {1, 2, 5, 10}
    movdqa XMMWORD PTR [rdi], xmm0
    movdqa xmm0, XMMWORD PTR [rip+.LC1] ; {17, 26, 37, 50}
    movdqa XMMWORD PTR [rdi+16], xmm0
    ; ... etc. (4 movdqa pour 16 entiers)
```

### Ce que le RE doit retenir

Quand vous voyez une longue séquence de `mov DWORD PTR [reg+offset], constante` sans aucune boucle autour, vous êtes face à un déroulage complet. Les constantes stockées sont les valeurs précalculées — elles contiennent souvent la clé pour comprendre ce que faisait la boucle originale. Essayez de trouver la formule qui relie les constantes à leur index : ici, la suite `1, 2, 5, 10, 17, 26, 37, 50, ...` correspond à `i² + 1`.

---

## Boucle non vectorisable : dépendance inter-itérations

```c
static void dependent_loop(int *data, int n)
{
    for (int i = 1; i < n; i++) {
        data[i] = data[i - 1] * 3 + data[i];
    }
}
```

Ici, `data[i]` dépend de `data[i-1]` — le résultat de l'itération précédente est nécessaire pour calculer l'itération courante. Les itérations ne peuvent pas être exécutées en parallèle.

### En `-O2` et `-O3`

GCC détecte la dépendance et **renonce à la vectorisation**. Il peut tout de même dérouler la boucle partiellement, mais chaque itération reste séquentielle :

```asm
.L_loop:
    mov    eax, DWORD PTR [rdi+rcx*4-4]  ; eax = data[i-1]
    lea    eax, [rax+rax*2]              ; eax = data[i-1] * 3
    add    eax, DWORD PTR [rdi+rcx*4]    ; eax += data[i]
    mov    DWORD PTR [rdi+rcx*4], eax    ; data[i] = résultat
    add    rcx, 1
    cmp    rcx, rdx
    jl     .L_loop
```

Remarquez : aucune instruction `xmm`/`ymm`, malgré le `-O3`. La boucle est scalaire, avec une seule itération par tour.

La multiplication par 3 est exprimée avec `lea eax, [rax+rax*2]` au lieu de `imul eax, eax, 3` — c'est un idiome GCC courant (cf. section 16.6).

### Ce que le RE doit retenir

Si une boucle en `-O3` n'utilise que des registres scalaires (`eax`, `ecx`, etc.) sans aucun registre SIMD, c'est un indice fort que le compilateur a détecté une **dépendance entre itérations**. Cherchez le pattern : la valeur calculée à l'itération `i` est immédiatement utilisée comme source à l'itération `i+1`, typiquement via un décalage d'offset (`[rdi+rcx*4-4]` pour `data[i-1]` vs `[rdi+rcx*4]` pour `data[i]`).

---

## Le problème de l'aliasing et le mot-clé `restrict`

L'aliasing est la situation où deux pointeurs peuvent désigner la même zone mémoire. Quand GCC ne peut pas prouver l'absence d'aliasing, il doit être conservateur.

```c
/* Sans restrict — aliasing possible */
static void vec_add_alias(int *dst, const int *src, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = dst[i] + src[i];
    }
}

/* Avec restrict — pas d'aliasing garanti */
static void vec_add_noalias(int * restrict dst, const int * restrict src, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = dst[i] + src[i];
    }
}
```

### Sans `restrict` — test d'aliasing au runtime

GCC ne sait pas si `dst` et `src` se chevauchent. Si les zones se chevauchent (par exemple `dst = src + 1`), la vectorisation donnerait un résultat incorrect. GCC génère donc **deux versions** de la boucle : une version vectorisée et une version scalaire, avec un test au runtime pour choisir :

```asm
vec_add_alias:
    ; Test d'aliasing au runtime
    lea    rax, [rdi+rcx*4]              ; fin de dst
    cmp    rax, rsi                       ; dst_end > src ?
    jbe    .L_vectorized                  ; pas de chevauchement → vectoriser

    lea    rax, [rsi+rcx*4]              ; fin de src
    cmp    rax, rdi                       ; src_end > dst ?
    jbe    .L_vectorized                  ; pas de chevauchement → vectoriser

    jmp    .L_scalar                      ; chevauchement → scalaire

.L_vectorized:
    ; Boucle SIMD (movdqu + paddd + movdqu)
    ; ...

.L_scalar:
    ; Boucle classique un élément à la fois
    ; ...
```

Ce test d'aliasing est un pattern reconnaissable : deux `lea` qui calculent les adresses de fin, suivis de `cmp` croisés entre les deux zones mémoire, avec un branchement vers la version vectorisée ou scalaire.

### Avec `restrict` — vectorisation directe

```asm
vec_add_noalias:
    ; Pas de test d'aliasing — restrict garantit la non-interférence
    ; Directement la boucle SIMD
.L_vec_loop:
    movdqu xmm0, XMMWORD PTR [rdi+rax]
    movdqu xmm1, XMMWORD PTR [rsi+rax]
    paddd  xmm0, xmm1
    movdqu XMMWORD PTR [rdi+rax], xmm0
    add    rax, 16
    cmp    eax, edx
    jl     .L_vec_loop
```

Le test a disparu — le compilateur fait confiance au `restrict`.

### Ce que le RE doit retenir

Si vous voyez un « double chemin » dans une fonction (deux versions de la même boucle, une avec des `xmm` et une sans, précédées d'un test sur les adresses), c'est un **test d'aliasing runtime**. C'est un indice que le code source n'utilisait pas `restrict` et que le compilateur a dû gérer les deux cas.

---

## Flottants et `-ffast-math`

La vectorisation des réductions flottantes est un cas particulier intéressant.

```c
static float float_sum(const float *data, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += data[i];
    }
    return sum;
}
```

### Sans `-ffast-math` : pas de vectorisation de la réduction

L'addition flottante IEEE 754 n'est **pas associative** : `(a + b) + c` peut donner un résultat différent de `a + (b + c)` à cause des arrondis. Or, la vectorisation d'une somme revient à changer l'ordre d'évaluation : au lieu de `sum += a[0]; sum += a[1]; sum += a[2]; sum += a[3];`, on calcule `sum0 += a[0]; sum1 += a[1]; sum2 += a[2]; sum3 += a[3]; sum = sum0+sum1+sum2+sum3;`.

Par défaut, GCC respecte strictement la sémantique IEEE 754 et ne vectorise pas cette boucle. Le résultat en `-O3` est une boucle scalaire utilisant `addss` (Add Scalar Single) :

```asm
.L_loop:
    addss  xmm0, DWORD PTR [rdi+rax*4]  ; addition scalaire flottante
    add    rax, 1
    cmp    eax, ecx
    jl     .L_loop
```

`addss` opère sur un seul flottant dans le coin bas du registre `xmm0`. Les 96 bits supérieurs sont ignorés — c'est du SIMD gaspillé.

### Avec `-ffast-math` : vectorisation autorisée

Avec `-ffast-math`, GCC a la permission de réassocier les additions flottantes :

```asm
    ; Accumulateur vectoriel — 4 sommes partielles
    xorps  xmm1, xmm1                   ; {0, 0, 0, 0}

.L_vec_loop:
    movups xmm0, XMMWORD PTR [rdi+rax]  ; 4 floats
    addps  xmm1, xmm0                   ; 4 additions parallèles
    add    rax, 16
    cmp    eax, edx
    jl     .L_vec_loop

    ; Réduction horizontale
    movhlps xmm0, xmm1                  ; échange haut et bas
    addps   xmm1, xmm0                  ; somme croisée
    movss   xmm0, xmm1
    shufps  xmm1, xmm1, 0x55
    addss   xmm0, xmm1                  ; somme finale
```

Les instructions `addps` (Add Packed Single) et `movups` (Move Unaligned Packed Single) sont les équivalents flottants de `paddd` et `movdqu`. La réduction horizontale utilise `movhlps` + `addps` + `shufps` + `addss` pour sommer les 4 éléments du registre.

### Ce que le RE doit retenir

Si une boucle sur des flottants en `-O3` utilise `addss` (scalaire) au lieu de `addps` (parallèle), le binaire a probablement été compilé **sans** `-ffast-math`. C'est une information utile : le développeur se souciait de la précision numérique IEEE 754. À l'inverse, `addps` dans une réduction indique `-ffast-math` — le résultat peut différer légèrement de la version séquentielle.

---

## Reconnaissance de memset/memcpy par le compilateur

GCC reconnaît certains patterns de boucle comme étant fonctionnellement équivalents à `memset` ou `memcpy` et les remplace par des appels à ces fonctions optimisées de la libc (ou par des instructions inline `rep stosb` / `rep movsb`).

```c
static void zero_fill(int *arr, int n)
{
    for (int i = 0; i < n; i++) {
        arr[i] = 0;
    }
}

static void copy_array(int *dst, const int *src, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}
```

### En `-O0`

Boucles explicites avec `mov DWORD PTR [rdi+rax*4], 0` par itération.

### En `-O2`

```asm
zero_fill:
    ; GCC remplace la boucle par un call memset
    movsxd rdx, esi                      ; rdx = n
    shl    rdx, 2                        ; rdx = n * 4 (taille en octets)
    xor    esi, esi                      ; valeur = 0
    jmp    memset@plt                    ; tail call vers memset

copy_array:
    ; GCC remplace la boucle par un call memcpy
    movsxd rdx, edx                      ; rdx = n
    shl    rdx, 2                        ; rdx = n * 4
    mov    rax, rdi                      ; sauvegarde dst pour le retour
    jmp    memcpy@plt                    ; tail call vers memcpy
```

La boucle entière a été remplacée par un seul `jmp` (tail call) vers `memset` ou `memcpy`. Le calcul de taille `n * 4` (un `int` fait 4 octets) est le seul vestige de la boucle originale.

### Ce que le RE doit retenir

Quand vous voyez un `call memset` ou `call memcpy` dans un binaire optimisé, il est possible qu'il n'y ait **aucun appel explicite** à `memset`/`memcpy` dans le code source. GCC a reconnu le pattern de la boucle et l'a remplacé automatiquement. Inversement, si vous reconstruisez le code source, vous pouvez choisir d'écrire un `memset` direct — le résultat compilé sera identique.

Ce phénomène explique aussi pourquoi certains binaires optimisés font plus d'appels à `memset`/`memcpy` que ce que le source laisse supposer.

---

## Strength reduction dans les boucles

La *strength reduction* remplace une opération coûteuse qui dépend du compteur de boucle par une opération moins coûteuse incrémentale.

```c
static void strided_write(int *data, int n, int stride, int value)
{
    for (int i = 0; i < n; i++) {
        data[i * stride] = value + i;
    }
}
```

### En `-O0`

L'index `i * stride` est calculé par un `imul` à chaque itération :

```asm
.L_loop:
    mov    eax, DWORD PTR [rbp-0x24]     ; i
    imul   eax, DWORD PTR [rbp-0x1c]     ; i * stride
    cdqe
    lea    rdx, [rax*4]
    mov    rax, QWORD PTR [rbp-0x8]      ; data
    add    rax, rdx
    ; ...
    mov    DWORD PTR [rax], ecx          ; data[i*stride] = value + i
```

### En `-O2`

GCC transforme la multiplication en addition cumulative. Au lieu de recalculer `i * stride` à chaque tour, il maintient un pointeur qui avance de `stride * 4` octets à chaque itération :

```asm
    ; Précalcul : stride_bytes = stride * 4
    movsxd rax, ecx                      ; rax = stride
    shl    rax, 2                        ; rax = stride * 4 (en octets)

.L_loop:
    mov    DWORD PTR [rdi], esi          ; *ptr = value + i
    add    esi, 1                        ; value + i → value + i + 1
    add    rdi, rax                      ; ptr += stride_bytes
    cmp    esi, edx
    jl     .L_loop
```

Le `imul` a disparu, remplacé par un `add rdi, rax`. La multiplication (coûteuse : 3 cycles) est remplacée par une addition (1 cycle). C'est la strength reduction.

### Ce que le RE doit retenir

Quand vous voyez un pointeur qui avance par pas fixes dans une boucle (`add rdi, constante` ou `add rdi, registre`), sans aucune multiplication visible, c'est probablement un accès indexé strided dont la multiplication a été réduite en addition. Le pas d'avancement (`rax` dans l'exemple) correspond à `stride * sizeof(element)`.

---

## Résumé : reconnaître les transformations de boucle en RE

| Ce que vous voyez dans le désassemblage | Transformation GCC | Ce qui était dans le source |  
|---|---|---|  
| `add rcx, 2` (ou 4, 8) au lieu de `add rcx, 1` dans la boucle | Déroulage partiel | Boucle `for(i=0; i<n; i++)` avec corps dupliqué |  
| Corps de boucle dupliqué N fois, compteur incrémenté de N | Déroulage partiel (facteur N) | Idem |  
| Séquence de `mov [reg+off], constante` sans boucle | Déroulage complet | Boucle à bornes constantes et petites |  
| `movdqu xmm + paddd xmm + movdqu xmm`, compteur += 16 | Vectorisation SSE (4 × int32) | Boucle sur un tableau d'entiers |  
| `vmovdqu ymm + vpaddd ymm`, compteur += 32 | Vectorisation AVX (8 × int32) | Idem, avec `-mavx2` |  
| `pxor xmm, xmm` + accumulation SIMD + réduction `pshufd`/`phaddd` | Réduction vectorisée | Boucle `sum += ...` |  
| Trois sous-boucles (prologue scalaire + corps SIMD + épilogue scalaire) | Boucle vectorisée avec gestion des restes | Boucle simple dont n n'est pas multiple du facteur SIMD |  
| Test `lea`/`cmp` sur deux adresses avant la boucle, deux versions | Test d'aliasing runtime | Boucle sur deux pointeurs sans `restrict` |  
| `addss` au lieu de `addps` dans une boucle de réduction float | Pas de `-ffast-math` | `sum += float_array[i]` |  
| `call memset@plt` ou `jmp memcpy@plt` | Reconnaissance de pattern | Boucle de mise à zéro ou de copie |  
| `add rdi, reg` sans `imul` dans une boucle | Strength reduction | `data[i * stride]` |  
| Aucun registre SIMD malgré `-O3` | Dépendance inter-itérations | `data[i] = f(data[i-1])` |

---

## Conseil pratique : utiliser `-fopt-info` pour comprendre ce que GCC fait

Si vous avez accès au code source (ou si vous faites des expériences), GCC fournit un flag très utile pour comprendre ses décisions :

```bash
gcc -O3 -fopt-info-vec-optimized -o test loop_unroll_vec.c
```

GCC affiche les boucles qu'il a vectorisées :

```
loop_unroll_vec.c:42:5: optimized: loop vectorized using 16 byte vectors  
loop_unroll_vec.c:62:5: optimized: loop vectorized using 16 byte vectors  
```

Et avec `-fopt-info-vec-missed`, il explique pourquoi certaines boucles ne sont **pas** vectorisées :

```
loop_unroll_vec.c:95:5: missed: not vectorized: complicated access pattern  
loop_unroll_vec.c:115:5: missed: not vectorized: possible dependence  
```

Ces messages sont précieux pour comprendre la logique interne du compilateur — même si en situation de RE, vous n'y avez évidemment pas accès. L'objectif est de construire votre intuition sur ce qui est vectorisable et ce qui ne l'est pas.

---


⏭️ [Tail call optimization et son impact sur la pile](/16-optimisations-compilateur/04-tail-call-optimization.md)
