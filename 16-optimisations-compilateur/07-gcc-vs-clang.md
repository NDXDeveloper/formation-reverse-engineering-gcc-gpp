🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 16.7 — Comparaison GCC vs Clang : différences de patterns à l'assembleur

> **Fichiers source associés** : `binaries/ch16-optimisations/opt_levels_demo.c`, `gcc_idioms.c`  
> **Compilation** : `make s16_7` (produit les variantes `_clang_O0` et `_clang_O2`, nécessite `clang`)

---

## Introduction

Jusqu'ici, tout ce chapitre a porté sur GCC. Mais dans le monde réel, le binaire que vous analysez peut aussi bien avoir été compilé avec **Clang/LLVM** — l'autre grand compilateur C/C++ open source. Sur macOS, Clang est le compilateur par défaut. De nombreux projets Linux l'utilisent aussi (le noyau Linux peut être compilé avec Clang depuis la version 4.15, et Android utilise exclusivement Clang depuis 2019). Enfin, des compilateurs commerciaux comme celui d'Intel (ICX) sont désormais basés sur LLVM.

Pour le reverse engineer, identifier le compilateur d'origine d'un binaire est une information tactique précieuse. Elle permet de choisir les bons heuristiques de reconnaissance de patterns, d'anticiper les optimisations appliquées, et d'ajuster les paramètres des outils d'analyse (Ghidra, IDA). Et la meilleure façon d'identifier le compilateur est de reconnaître ses « tics » — les patterns caractéristiques qui diffèrent entre GCC et Clang.

GCC et Clang partagent les mêmes objectifs (traduire du C/C++ en code machine performant) et produisent souvent un assembleur très similaire. Mais leurs chemins d'optimisation internes diffèrent, ce qui crée des divergences subtiles mais reconnaissables. Cette section catalogue les plus importantes.

> 💡 Pour reproduire les comparaisons, Compiler Explorer ([godbolt.org](https://godbolt.org)) est l'outil idéal : entrez le même code C, sélectionnez GCC et Clang côte à côte, et observez les différences.

---

## Comment identifier le compilateur d'un binaire

Avant de détailler les différences de patterns, voici les indices « haut niveau » qui permettent souvent de trancher rapidement.

### Indice 1 — Les chaînes de version dans le binaire

Si le binaire n'est pas strippé, la section `.comment` contient souvent la version du compilateur :

```bash
$ readelf -p .comment build/opt_levels_demo_O2
  [     0]  GCC: (Ubuntu 13.2.0-23ubuntu4) 13.2.0

$ readelf -p .comment build/opt_levels_demo_clang_O2
  [     0]  clang version 18.1.3 (...)
```

C'est le moyen le plus direct, mais il ne fonctionne que sur les binaires non strippés. Un `strip` ou `-s` supprime `.comment`.

### Indice 2 — Les fonctions d'initialisation

GCC et Clang émettent des fonctions d'initialisation légèrement différentes dans les sections `.init` et `.fini`. GCC utilise `__libc_csu_init` et `__libc_csu_fini` (sur les anciens systèmes avec glibc < 2.34), tandis que Clang émet des patterns différents pour l'initialisation des constructeurs globaux.

### Indice 3 — Les conventions de nommage interne

Les symboles internes générés par le compilateur ont des noms différents :

- GCC : `.LC0`, `.LC1` pour les littéraux en `.rodata`, `.L1`, `.L2` pour les labels.  
- Clang : `.Lcmp_true`, `.Lpcrel_hi0`, ou des noms générés par LLVM comme `__unnamed_1`.

Dans un binaire strippé, ces labels internes sont supprimés. Mais dans un binaire avec symboles de debug, ils sont visibles.

### Indice 4 — Les patterns d'assembleur

C'est le cœur de cette section. Certains patterns sont quasi-exclusifs à l'un ou l'autre compilateur. Nous les détaillons ci-dessous.

---

## Différence 1 — Le prologue de fonctions

Le prologue et l'épilogue d'une fonction sont la première chose que le désassembleur montre. GCC et Clang ont des préférences différentes.

### GCC en `-O0`

GCC utilise systématiquement le frame pointer classique `rbp` :

```asm
function:
    push   rbp
    mov    rbp, rsp
    sub    rsp, 0x20                     ; espace pour les variables locales
    ; ...
    leave                                ; équivalent de mov rsp, rbp ; pop rbp
    ret
```

Le `leave` en épilogue est caractéristique de GCC. Il est rarement émis par Clang.

### Clang en `-O0`

Clang utilise aussi `rbp` en `-O0`, mais son prologue est parfois légèrement différent :

```asm
function:
    push   rbp
    mov    rbp, rsp
    sub    rsp, 0x20
    ; ...
    add    rsp, 0x20                     ; restauration explicite de rsp
    pop    rbp                           ; pas de leave
    ret
```

Clang préfère `add rsp, N` + `pop rbp` au lieu de `leave`. Les deux sont fonctionnellement équivalents, mais `leave` est un marqueur quasi-exclusif de GCC.

### En `-O2` — omission du frame pointer

Les deux compilateurs omettent le frame pointer en `-O2` par défaut (`-fomit-frame-pointer`), mais leurs conventions diffèrent pour les fonctions « leaf » (qui n'appellent aucune autre fonction) :

```asm
; GCC — leaf function en O2 : pas de prologue du tout
leaf_function:
    imul   eax, edi, 42
    ret

; Clang — même chose, identique
leaf_function:
    imul   eax, edi, 42
    ret
```

Pour les leaf functions, les deux compilateurs produisent un code identique. Les différences apparaissent dans les fonctions plus complexes.

---

## Différence 2 — Le choix entre `cmov` et branchement

Les deux compilateurs utilisent `cmov` pour éliminer les branchements, mais leurs heuristiques de décision diffèrent.

### Le pattern ternaire simple

```c
int max(int a, int b) {
    return (a > b) ? a : b;
}
```

GCC et Clang produisent le même code — un `cmp` + `cmov`. Pas de différence ici.

### Le pattern avec calcul dans les branches

```c
int f(int x) {
    if (x > 0)
        return x * 3 + 1;
    else
        return x * 2 - 5;
}
```

#### GCC en `-O2`

GCC tend à **garder le branchement** quand les deux branches contiennent du calcul :

```asm
    test   edi, edi
    jle    .L_negative
    lea    eax, [rdi+rdi*2]             ; x * 3
    add    eax, 1                        ; + 1
    ret
.L_negative:
    lea    eax, [rdi+rdi]               ; x * 2
    sub    eax, 5                        ; - 5
    ret
```

Deux chemins séparés, chacun se terminant par `ret`.

#### Clang en `-O2`

Clang est souvent **plus agressif** dans l'utilisation de `cmov`. Il calcule les deux branches puis sélectionne le résultat :

```asm
    lea    eax, [rdi+rdi*2]             ; eax = x * 3
    add    eax, 1                        ; eax = x * 3 + 1  (branche « then »)
    lea    ecx, [rdi+rdi]               ; ecx = x * 2
    sub    ecx, 5                        ; ecx = x * 2 - 5  (branche « else »)
    test   edi, edi
    cmovle eax, ecx                     ; si x <= 0 : eax = ecx
    ret
```

Clang calcule les **deux** résultats, puis choisit avec un `cmov`. C'est plus prévisible pour le pipeline (pas de branchement), mais gaspille du travail si une seule branche est prise.

### Ce que le RE doit retenir

Si vous voyez les deux branches d'un if/else calculées puis sélectionnées par `cmov` — même quand les branches contiennent plusieurs instructions — c'est un indice Clang. GCC est plus conservateur et préfère un branchement quand les branches sont non triviales.

---

## Différence 3 — Division par constante : même résultat, chemin différent

GCC et Clang utilisent tous les deux le magic number pour les divisions par constante, mais les constantes choisies et les séquences d'ajustement peuvent différer.

### Exemple : `x / 7` (signé)

#### GCC

```asm
    mov    eax, edi
    mov    edx, 0x92492493
    imul   edx
    add    edx, edi                      ; correction additive
    sar    edx, 2
    mov    eax, edx
    shr    eax, 31
    add    eax, edx
```

#### Clang

```asm
    mov    eax, edi
    mov    ecx, 0x92492493
    imul   ecx                           ; même magic number
    add    edx, edi                      ; même correction
    sar    edx, 2
    shr    edi, 31                       ; extraction du signe sur le registre original
    add    eax, edx, edi                 ; ou lea eax, [rdx+rdi]
```

Pour `/7`, les deux compilateurs choisissent le même magic number. Mais les registres utilisés et l'ordre des opérations de correction diffèrent. Pour d'autres diviseurs, les magic numbers peuvent être **mathématiquement différents** (il existe souvent plusieurs paires M/S correctes pour un même diviseur), ce qui rend la comparaison plus délicate.

### Ce que le RE doit retenir

Le magic number lui-même n'est pas un indicateur fiable du compilateur — les deux peuvent choisir le même. Ce sont les instructions autour (choix des registres, ordre des corrections, utilisation de `lea` vs `add`) qui portent la signature.

---

## Différence 4 — Gestion des switch

### Jump table : position des données

Les deux compilateurs génèrent des jump tables pour les switch denses, mais la façon dont ils calculent l'adresse cible diffère.

#### GCC — offsets relatifs à la table

```asm
    lea    rax, [rip+.L_table]           ; base = adresse de la table
    movsxd rdx, DWORD PTR [rax+rdi*4]   ; offset = table[index]
    add    rax, rdx                      ; cible = base + offset
    jmp    rax
```

GCC stocke des **offsets relatifs à la table elle-même** (offsets signés 32 bits). L'adresse finale est `table_address + offset`.

#### Clang — offsets relatifs à l'instruction `lea`

```asm
    lea    rcx, [rip+.LJTI0_0]          ; base = adresse de la table
    movsxd rax, DWORD PTR [rcx+4*rdi]   ; offset = table[index]
    add    rax, rcx                      ; cible = base + offset
    jmp    rax
```

Le pattern est très similaire, mais Clang utilise souvent une convention de nommage différente pour la table (`.LJTI0_0` au lieu de `.L_table`) et peut choisir des registres différents.

### Switch sparse : stratégie de recherche

Pour les switch sparse, les deux compilateurs utilisent des arbres de comparaisons, mais Clang tend à produire des arbres **plus balancés** (vrai binary search), tandis que GCC peut produire des cascades linéaires quand le nombre de cases est petit (< 6).

---

## Différence 5 — Déroulage de boucles et vectorisation

C'est l'un des domaines où les différences entre GCC et Clang sont les plus marquées.

### Facteur de déroulage

Pour une même boucle en `-O2`, GCC et Clang choisissent souvent des facteurs de déroulage différents :

```c
void add_arrays(int *dst, const int *src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] += src[i];
}
```

- **GCC `-O2`** : déroulage par 2 ou 4 (conservateur).  
- **Clang `-O2`** : déroulage par 4 ou 8 (plus agressif par défaut).

En `-O3`, les deux deviennent agressifs, mais Clang tend à dérouler davantage.

### Style de la boucle vectorisée

#### GCC — prologue/corps/épilogue classique

GCC suit la structure en trois parties décrite en section 16.3 :

```asm
    ; Test de borne
    cmp    ecx, 3
    jle    .L_scalar
    ; Boucle vectorisée (SSE)
.L_vec:
    movdqu xmm0, [rsi+rax]
    paddd  xmm0, [rdi+rax]
    movdqu [rdi+rax], xmm0
    add    rax, 16
    cmp    eax, edx
    jl     .L_vec
    ; Épilogue scalaire
.L_scalar:
    ; ...
```

#### Clang — boucle vectorisée avec accumulation de pointeurs

Clang utilise souvent un style différent, avec des pointeurs incrémentés au lieu d'un index :

```asm
    ; Clang vectorise avec des pointeurs
    mov    rax, rdi                      ; ptr_dst = dst
    mov    rcx, rsi                      ; ptr_src = src
    mov    edx, esi                      ; compteur

.L_vec:
    movdqu xmm0, [rcx]                  ; charge depuis ptr_src
    movdqu xmm1, [rax]                  ; charge depuis ptr_dst
    paddd  xmm0, xmm1
    movdqu [rax], xmm0                  ; stocke dans ptr_dst
    add    rax, 16                       ; ptr_dst += 16
    add    rcx, 16                       ; ptr_src += 16
    add    edx, -4                       ; compteur -= 4
    jne    .L_vec
```

La différence est subtile mais caractéristique : GCC utilise un **index** (`[rsi+rax]`) tandis que Clang incrémente directement les **pointeurs** (`add rax, 16`). Le résultat fonctionnel est identique, mais le pattern d'adressage est différent.

### Vectorisation des réductions

Pour une boucle d'accumulation (`sum += a[i]`), Clang utilise souvent **plusieurs accumulateurs vectoriels** (2 ou 4 registres `xmm` en parallèle) pour masquer la latence, tandis que GCC utilise un seul accumulateur :

```asm
; Clang — réduction avec 4 accumulateurs
    pxor   xmm0, xmm0                   ; acc0
    pxor   xmm1, xmm1                   ; acc1
    pxor   xmm2, xmm2                   ; acc2
    pxor   xmm3, xmm3                   ; acc3
.L_vec:
    paddd  xmm0, [rdi]
    paddd  xmm1, [rdi+16]
    paddd  xmm2, [rdi+32]
    paddd  xmm3, [rdi+48]
    add    rdi, 64
    ; ...
    ; Réduction finale : xmm0 += xmm1 += xmm2 += xmm3
    paddd  xmm0, xmm1
    paddd  xmm2, xmm3
    paddd  xmm0, xmm2
    ; Puis réduction horizontale
```

Si vous voyez 2 ou 4 `pxor xmm, xmm` initialisant des accumulateurs en début de boucle, suivis de `paddd` vers chacun d'eux, c'est un indice Clang.

---

## Différence 6 — Alignement des boucles et `nop` padding

Les deux compilateurs alignent les cibles de branchement (début de boucle, labels de saut) pour des raisons de performance du cache d'instructions, mais leur stratégie de remplissage diffère.

### GCC — `nop` classiques ou `nop DWORD PTR [rax]`

GCC utilise des instructions `nop` de différentes tailles pour aligner sur 16 octets :

```asm
    nop                                  ; 1 octet
    nop    DWORD PTR [rax+rax*1+0x0]     ; 8 octets (long nop)
    nop    WORD PTR cs:[rax+rax*1+0x0]   ; 10 octets
    ; Alignement atteint
.L_loop:
    ; ...
```

Ces « long nops » sont des instructions qui ne font rien mais occupent un nombre variable d'octets. Ils apparaissent comme des `nop` multi-octets dans `objdump`.

### Clang — `nop` + alignement `.p2align`

Clang utilise une directive `.p2align` qui produit des nops similaires, mais tend à aligner plus agressivement (souvent sur 32 octets au lieu de 16) et utilise parfois des patterns de nop différents :

```asm
    .p2align 5, 0x90                     ; aligne sur 32 octets avec des 0x90 (nop)
.LBB0_1:
    ; boucle
```

### Ce que le RE doit retenir

Une séquence de nops multi-octets avant un label de boucle est du padding d'alignement — ignorez-la lors de l'analyse. La taille d'alignement (16 vs 32 octets) peut être un indice sur le compilateur.

---

## Différence 7 — Chaînes de caractères et constantes en `.rodata`

### Organisation de `.rodata`

GCC et Clang organisent les constantes littérales différemment dans `.rodata` :

- **GCC** : les chaînes de format sont regroupées en début de `.rodata`, dans l'ordre de leur première utilisation. Les constantes flottantes sont séparées.  
- **Clang** : les chaînes et constantes sont parfois entrelacées, et Clang peut fusionner des chaînes qui partagent un suffixe commun (*string merging* plus agressif).

### Fusion de chaînes

```c
printf("Hello, world!\n");  
printf("world!\n");  
```

GCC stocke les deux chaînes séparément dans `.rodata`. Clang peut détecter que `"world!\n"` est un suffixe de `"Hello, world!\n"` et ne stocker que la longue chaîne, avec un pointeur décalé pour la courte :

```
; Clang .rodata
.LC0:  "Hello, world!\n\0"
; "world!\n" pointe vers .LC0 + 7 (pas de duplication)
```

C'est une optimisation de taille qui n'existe pas dans GCC (sauf avec `-fmerge-constants`, qui ne traite que les suffixes exacts).

---

## Différence 8 — Initialisation de variables locales

```c
int data[8] = {0};
```

### GCC

GCC utilise souvent une séquence de `mov QWORD PTR [rsp+offset], 0` ou un `rep stosq` :

```asm
    ; GCC — mise à zéro par paires de qwords
    mov    QWORD PTR [rsp],    0
    mov    QWORD PTR [rsp+8],  0
    mov    QWORD PTR [rsp+16], 0
    mov    QWORD PTR [rsp+24], 0
```

### Clang

Clang préfère souvent les instructions SIMD pour la mise à zéro, même pour des données non-SIMD :

```asm
    ; Clang — mise à zéro via xmm
    xorps  xmm0, xmm0                   ; xmm0 = {0, 0, 0, 0}
    movaps XMMWORD PTR [rsp],    xmm0   ; 16 octets à zéro
    movaps XMMWORD PTR [rsp+16], xmm0   ; 16 octets à zéro
```

`xorps xmm0, xmm0` est la manière canonique de mettre un registre SSE à zéro — c'est l'équivalent SIMD de `xor eax, eax`. Le `movaps` (Move Aligned Packed Single) stocke 16 octets d'un coup.

### Ce que le RE doit retenir

L'utilisation de `xorps xmm0, xmm0` + `movaps` pour initialiser des données scalaires (pas des flottants) est un marqueur Clang. GCC réserve les registres SIMD pour les données SIMD et préfère `mov QWORD PTR, 0` pour les initialisations scalaires.

---

## Différence 9 — Gestion de `abs()` et `min`/`max`

### `abs(x)`

#### GCC (récent)

```asm
    mov    eax, edi
    neg    eax
    cmovs  eax, edi                     ; neg + cmovs
```

#### Clang

```asm
    mov    eax, edi
    sar    eax, 31                       ; masque = signe
    xor    edi, eax                      ; flip si négatif
    sub    edi, eax                      ; +1 si négatif (complément à 2)
    mov    eax, edi
```

Clang préfère systématiquement le pattern arithmétique `sar` + `xor` + `sub`, tandis que GCC récent utilise `neg` + `cmovs`. Les deux sont corrects ; le choix est un marqueur de compilateur.

### `min(a, b)` / `max(a, b)`

Les deux compilateurs utilisent `cmp` + `cmov`, mais l'ordre des opérandes peut différer :

```asm
; GCC — min(a, b)
    cmp    edi, esi
    mov    eax, esi                      ; eax = b (défaut)
    cmovle eax, edi                     ; si a <= b : eax = a

; Clang — min(a, b)
    cmp    edi, esi
    cmovle esi, edi                     ; si a <= b : esi = a (écrase b)
    mov    eax, esi                      ; retourne le résultat
```

GCC initialise le résultat avec une valeur par défaut puis écrase conditionnellement. Clang fait le `cmov` directement sur un opérande puis le copie dans `eax`. Fonctionnellement identique, stylistiquement différent.

---

## Différence 10 — Appels de fonctions et tail calls

### Tail calls

Les deux compilateurs effectuent la tail call optimization, mais Clang est **plus agressif** : il l'applique dans des situations où GCC conserve un `call` + `ret` par prudence. En particulier, Clang effectue plus fréquemment des tail calls quand il y a des réarrangements de paramètres complexes.

### Appels à la libc

Pour un simple `printf("Hello\n")`, GCC remplace parfois l'appel par `puts("Hello")` (suppression du `\n` et changement de fonction), tandis que Clang garde souvent `printf`. Ce remplacement `printf` → `puts` est un marqueur GCC.

```asm
; GCC
    lea    rdi, [rip+.LC0]              ; "Hello" (sans \n)
    call   puts@plt                     ; puts au lieu de printf !

; Clang
    lea    rdi, [rip+.LC0]              ; "Hello\n"
    xor    eax, eax                     ; 0 args flottants (variadic)
    call   printf@plt                   ; printf conservé
```

Le `xor eax, eax` avant un `call printf` est un marqueur de convention d'appel variadic System V AMD64 : `al` doit contenir le nombre de registres SSE utilisés pour les arguments flottants. GCC l'omet parfois quand il sait qu'il n'y a pas d'arguments flottants ; Clang le met systématiquement.

---

## Différence 11 — Rotation de bits

```c
unsigned int rotl(unsigned int x, int n) {
    return (x << n) | (x >> (32 - n));
}
```

Les deux compilateurs reconnaissent le pattern et émettent `rol`. Pas de différence significative ici — c'est un des rares patterns totalement identiques.

Cependant, pour les rotations par une constante dans un contexte plus large (hash, crypto), Clang tend à réordonnancer les rotations de manière plus agressive, ce qui peut changer l'ordre des `rol`/`ror` par rapport au source.

---

## Différence 12 — Fonctions de démarrage (CRT)

Le code de démarrage (*C Runtime*, CRT) visible avant `main()` diffère selon le compilateur et la version de la libc.

### GCC + glibc

Le point d'entrée `_start` appelle `__libc_start_main` avec une signature bien connue. Les fonctions `__libc_csu_init` et `__libc_csu_fini` (sur glibc < 2.34) sont un marqueur fort de GCC.

### Clang + glibc

Clang utilise le même CRT que GCC (fourni par glibc), mais peut émettre des constructeurs/destructeurs globaux différents. La présence de `__libc_csu_init` n'est donc pas un marqueur fiable de GCC si le CRT système est partagé.

### Clang + musl ou une autre libc

Si le binaire utilise musl (fréquent dans les conteneurs Alpine Linux), le CRT est différent de glibc. L'absence de `__libc_csu_init` et la présence de `__init_libc` ou `__libc_start_main_stage2` sont des indices de musl, indépendamment du compilateur.

---

## Tableau récapitulatif des marqueurs

| Pattern | GCC | Clang |  
|---|---|---|  
| Épilogue de fonction | `leave` | `add rsp, N` + `pop rbp` |  
| `printf("Hello\n")` → | `call puts` (optimisé) | `call printf` (conservé) |  
| `xor eax, eax` avant appel variadic | Parfois omis | Systématique |  
| `abs(x)` | `neg` + `cmovs` | `sar 31` + `xor` + `sub` |  
| if/else avec calcul dans les branches | Branchement classique | Calcul des deux + `cmov` |  
| Mise à zéro de tableau local | `mov QWORD [rsp+off], 0` | `xorps xmm0` + `movaps` |  
| Adressage dans boucle vectorisée | Index `[base+rax*4]` | Pointeurs incrémentés |  
| Accumulateurs de réduction SIMD | 1 registre `xmm` | 2–4 registres `xmm` |  
| Déroulage en `-O2` | Conservateur (×2–4) | Agressif (×4–8) |  
| Alignement des boucles | 16 octets | 16 ou 32 octets |  
| Fusion de suffixes de chaînes | Rare | Courant |

---

## Cas ambigus : quand la distinction est impossible

Il est important d'être honnête sur les limites : pour de nombreux patterns, GCC et Clang produisent un assembleur **identique** ou quasi-identique. C'est le cas pour :

- Les magic numbers de division par constante (souvent les mêmes paires M/S).  
- Les jump tables de switch dense (même structure).  
- Les `cmov` pour les ternaires simples.  
- Les instructions SIMD de base (`movdqu`, `paddd`, etc.).  
- Les appels de fonctions standard (convention System V respectée par les deux).  
- Les patterns `test` + `setcc` + `movzx` pour les booléens.

Dans ces cas, l'identification du compilateur doit s'appuyer sur les indices « haut niveau » (section `.comment`, CRT, etc.) plutôt que sur les patterns d'instructions.

De plus, les versions successives de chaque compilateur modifient progressivement leurs patterns. Un GCC 8 émet un `abs()` différent de GCC 13. Un Clang 14 vectorise différemment de Clang 18. Les marqueurs décrits ici sont des tendances générales, pas des règles absolues.

---

## Stratégie pratique pour le RE

Voici une approche en quatre étapes quand vous analysez un binaire d'origine inconnue :

**1. Vérifier `.comment` d'abord.** C'est le test le plus rapide. Si la section existe, elle donne le compilateur et sa version.

```bash
readelf -p .comment binaire_cible 2>/dev/null  
strings binaire_cible | grep -iE 'gcc|clang|llvm'  
```

**2. Regarder le CRT.** Les fonctions d'initialisation et la structure de `_start` sont des indices forts.

**3. Observer les patterns caractéristiques.** Cherchez les marqueurs du tableau récapitulatif : `leave` vs `add rsp` + `pop rbp`, `puts` vs `printf`, mise à zéro scalaire vs SIMD, style d'adressage dans les boucles.

**4. Ne pas surinvestir dans l'identification.** Savoir si c'est GCC ou Clang est utile pour calibrer vos attentes, mais ce n'est pas un prérequis pour l'analyse. Les idiomes fondamentaux (magic numbers, cmov, jump tables, vectorisation) sont les mêmes — seuls les détails d'implémentation varient. Concentrez votre énergie sur la compréhension de la logique du programme plutôt que sur l'identification exacte du compilateur.

---

## Outils pour approfondir

**Compiler Explorer** ([godbolt.org](https://godbolt.org)) — Comparez le même code C avec GCC et Clang côte à côte. C'est le meilleur moyen de construire votre intuition sur les différences de patterns.

**`-fverbose-asm`** — Les deux compilateurs supportent ce flag, qui ajoute des commentaires dans l'assembleur généré. Utile pour comprendre quelle ligne de C correspond à quelles instructions.

```bash
gcc -O2 -S -fverbose-asm -o output_gcc.s source.c  
clang -O2 -S -fverbose-asm -o output_clang.s source.c  
diff output_gcc.s output_clang.s  
```

**`-mllvm -print-after-all`** (Clang uniquement) — Affiche l'IR LLVM après chaque passe d'optimisation. C'est l'équivalent de `-fdump-tree-all` pour GCC. Utile pour comprendre pourquoi Clang a choisi un pattern particulier.

---

> ⏭️ **Section suivante** : [🎯 Checkpoint — identifier 3 optimisations appliquées par GCC sur un binaire `-O2` fourni](/16-optimisations-compilateur/checkpoint.md)

⏭️ [🎯 Checkpoint : identifier 3 optimisations appliquées par GCC sur un binaire `-O2` fourni](/16-optimisations-compilateur/checkpoint.md)
