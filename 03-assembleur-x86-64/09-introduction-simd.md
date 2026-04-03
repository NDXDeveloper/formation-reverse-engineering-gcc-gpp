🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.9 — Introduction aux instructions SIMD (SSE/AVX) — les reconnaître sans les craindre

> 🎯 **Objectif de cette section** : savoir identifier les instructions SIMD quand elles apparaissent dans le désassemblage, comprendre pourquoi elles sont là, et être capable de les traverser sans blocage lors de l'analyse. Il ne s'agit pas de maîtriser la programmation SIMD — seulement de ne pas paniquer quand GCC en génère.

---

## Pourquoi cette section existe

Vous êtes en train d'analyser un binaire compilé avec `-O2`. Tout se passe bien — `mov`, `cmp`, `jne`, `call` — et soudain vous tombez sur ceci :

```asm
movdqu  xmm0, xmmword [rdi]  
movdqu  xmm1, xmmword [rsi]  
pcmpeqb xmm0, xmm1  
pmovmskb eax, xmm0  
cmp     eax, 0xffff  
jne     .not_equal  
```

Première réaction : ces mnémoniques sont incompréhensibles. Deuxième réaction : panique.

Cette section est là pour que la deuxième réaction n'arrive pas. Les instructions SIMD ne sont pas rares dans les binaires modernes — GCC en génère abondamment dès `-O2`, et la libc elle-même en est truffée (les implémentations optimisées de `memcpy`, `strcmp`, `strlen`…). Il faut savoir les reconnaître, les survoler intelligemment, et ne creuser que quand c'est nécessaire.

---

## SIMD en une phrase

**SIMD** (*Single Instruction, Multiple Data*) permet d'appliquer une même opération à **plusieurs données en parallèle** avec une seule instruction. Au lieu d'additionner deux entiers un par un, une instruction SIMD additionne 4, 8 ou 16 paires d'entiers simultanément.

```
Scalaire (une opération à la fois) :        SIMD (4 opérations en parallèle) :

  a₁ + b₁ = c₁                              ┌ a₁ ┐   ┌ b₁ ┐   ┌ c₁ ┐
  a₂ + b₂ = c₂                              │ a₂ │ + │ b₂ │ = │ c₂ │
  a₃ + b₃ = c₃                              │ a₃ │   │ b₃ │   │ c₃ │
  a₄ + b₄ = c₄                              └ a₄ ┘   └ b₄ ┘   └ c₄ ┘

  4 instructions                              1 instruction
```

C'est le mécanisme qui rend les opérations sur les tableaux, les chaînes, les buffers et les calculs numériques beaucoup plus rapides.

---

## Les générations SIMD sur x86-64

L'architecture x86-64 a accumulé plusieurs générations d'extensions SIMD au fil des décennies. Chaque génération ajoute des registres plus larges et de nouvelles instructions :

| Extension | Année | Largeur registres | Registres | Introduit par |  
|---|---|---|---|---|  
| **SSE** | 1999 | 128 bits | `xmm0`–`xmm7` | Intel Pentium III |  
| **SSE2** | 2001 | 128 bits | `xmm0`–`xmm7` | Intel Pentium 4 |  
| **SSE3/SSSE3** | 2004/2006 | 128 bits | `xmm0`–`xmm7` | Intel Prescott / Core 2 |  
| **SSE4.1/4.2** | 2008 | 128 bits | `xmm0`–`xmm7` | Intel Penryn / Nehalem |  
| **AVX** | 2011 | 256 bits | `ymm0`–`ymm15` | Intel Sandy Bridge |  
| **AVX2** | 2013 | 256 bits | `ymm0`–`ymm15` | Intel Haswell |  
| **AVX-512** | 2016 | 512 bits | `zmm0`–`zmm31` | Intel Xeon Phi / Skylake-X |

En mode x86-64, **SSE2 est garanti** — il fait partie de la spécification AMD64 de base. Cela signifie que tout binaire x86-64 peut utiliser les registres `xmm0`–`xmm15` et les instructions SSE2 sans vérification préalable. C'est pourquoi GCC utilise SSE2 comme base par défaut, même sans flag spécifique.

> 💡 **Pour le RE** : la grande majorité du SIMD que vous rencontrerez dans des binaires standards est du **SSE/SSE2**, parfois SSE4.2 (pour les opérations sur les chaînes). AVX et AVX-512 apparaissent dans du code scientifique, du traitement multimédia ou des bibliothèques numériques (BLAS, FFT, codecs…).

---

## Les registres SIMD

### Registres `xmm` (128 bits — SSE)

Les registres `xmm0` à `xmm15` font 128 bits (16 octets). Un même registre peut contenir différentes interprétations des données :

```
xmm0 (128 bits) peut être vu comme :
┌────────────────────────────────────────────────────────────┐
│                   1 valeur 128 bits                        │  (entier 128 bits)
├─────────────────────────────┬──────────────────────────────┤
│       double (64 bits)      │       double (64 bits)       │  (2 × double)
├──────────────┬──────────────┬──────────────┬───────────────┤
│  float 32b   │  float 32b   │  float 32b   │  float 32b    │  (4 × float)
├───────┬──────┬───────┬──────┬───────┬──────┬───────┬───────┤
│ int32 │ int32│ int32 │ int32│  ...  │      │       │       │  (4 × int32)
├──┬──┬─┴─┬──┬─┴─┬──┬──┴──┬───┴──┬──┬─┴─┬──┬─┴─┬──┬──┴──┬────┤
│b │b │ b │b │ b │b │  b  │ b    │b │ b │b │ b │b │  b  │ b  │  (16 × byte)
└──┴──┴───┴──┴───┴──┴─────┴──────┴──┴───┴──┴───┴──┴─────┴────┘
```

C'est l'interprétation choisie par l'instruction qui détermine comment les 128 bits sont découpés. Le registre lui-même ne « sait » pas s'il contient 2 doubles ou 16 octets.

### Registres `ymm` (256 bits — AVX)

Les registres `ymm0` à `ymm15` sont les extensions 256 bits des `xmm`. La moitié basse d'un `ymm` **est** le `xmm` correspondant :

```
ymm0 :  [ 256 bits                                                    ]
         [ moitié haute (128 bits)  ][ moitié basse = xmm0 (128 bits) ]
```

### Registres `zmm` (512 bits — AVX-512)

Le même principe étendu à 512 bits. Rare dans le code courant.

### Rôle dans la convention System V AMD64

Comme vu en section 3.5, les registres `xmm0`–`xmm7` sont utilisés pour **passer les arguments flottants** et `xmm0` pour la **valeur de retour flottante**. Ces usages sont du SSE scalaire (une seule valeur par registre), pas du SIMD vectoriel — mais ils utilisent les mêmes registres physiques.

---

## Reconnaître les instructions SIMD dans le désassemblage

### Le système de nommage

Les mnémoniques SIMD suivent des conventions de nommage qui, une fois comprises, permettent de deviner le rôle d'une instruction sans la connaître par cœur :

**Préfixes de type de données :**

| Préfixe / Suffixe | Signification | Exemple |  
|---|---|---|  
| `ss` | *Scalar Single* — 1 float 32 bits | `addss`, `movss` |  
| `sd` | *Scalar Double* — 1 double 64 bits | `addsd`, `movsd` |  
| `ps` | *Packed Single* — 4 floats 32 bits en parallèle | `addps`, `mulps` |  
| `pd` | *Packed Double* — 2 doubles 64 bits en parallèle | `addpd`, `mulpd` |  
| `b` | Bytes (8 bits) | `paddb`, `pcmpeqb` |  
| `w` | Words (16 bits) | `paddw`, `pcmpeqw` |  
| `d` | Doublewords (32 bits) | `paddd`, `pcmpeqd` |  
| `q` | Quadwords (64 bits) | `paddq` |  
| `dq` / `dqu` | Double-quadword (128 bits) | `movdqa`, `movdqu` |

**Préfixes d'opération :**

| Préfixe | Signification | Exemples |  
|---|---|---|  
| `p` | *Packed integer* | `paddb`, `pcmpeqb`, `pmovmskb` |  
| `v` | *VEX encoding* (AVX) | `vaddps`, `vmovdqu`, `vpxor` |

Le préfixe `v` est le signal immédiat d'une instruction AVX (ou supérieur). Les instructions AVX ont la même sémantique que leurs équivalents SSE, mais avec un encodage VEX qui permet des opérandes 3-registres et l'utilisation des registres `ymm` 256 bits :

```asm
addps   xmm0, xmm1           ; SSE : xmm0 = xmm0 + xmm1 (2 opérandes)  
vaddps  ymm0, ymm1, ymm2     ; AVX : ymm0 = ymm1 + ymm2 (3 opérandes, 256 bits)  
```

### Les catégories d'instructions SIMD

Sans chercher l'exhaustivité, les instructions SIMD se regroupent en familles reconnaissables :

**Déplacement de données :**

```asm
movaps  xmm0, [rdi]          ; charge 128 bits alignés (float packed)  
movups  xmm0, [rdi]          ; charge 128 bits non alignés (float packed)  
movdqa  xmm0, [rdi]          ; charge 128 bits alignés (entiers)  
movdqu  xmm0, [rdi]          ; charge 128 bits non alignés (entiers)  
movss   xmm0, [rdi]          ; charge un seul float (32 bits)  
movsd   xmm0, [rdi]          ; charge un seul double (64 bits)  
```

La distinction **aligné** (`a` = *aligned*) vs **non aligné** (`u` = *unaligned*) est historiquement importante : les versions alignées exigent que l'adresse mémoire soit un multiple de 16 octets, sinon elles déclenchent un crash (`SIGSEGV`). Les versions non alignées sont plus tolérantes mais étaient plus lentes sur les anciens processeurs. Sur les processeurs modernes (depuis Nehalem/Sandy Bridge), la différence de performance est négligeable, et GCC utilise de plus en plus les versions non alignées.

**Arithmétique :**

```asm
addps   xmm0, xmm1       ; 4 additions float en parallèle  
mulpd   xmm0, xmm1       ; 2 multiplications double en parallèle  
paddb   xmm0, xmm1       ; 16 additions d'octets en parallèle  
psubd   xmm0, xmm1       ; 4 soustractions d'entiers 32 bits en parallèle  
```

**Comparaison :**

```asm
pcmpeqb xmm0, xmm1       ; compare 16 paires d'octets → 0xFF si égal, 0x00 sinon  
cmpps   xmm0, xmm1, 0    ; compare 4 paires de floats (0 = equal)  
```

**Logique :**

```asm
pxor    xmm0, xmm0       ; mise à zéro d'un registre xmm (idiome SSE)  
pand    xmm0, xmm1       ; AND de 128 bits  
por     xmm0, xmm1       ; OR de 128 bits  
```

> 💡 **Pour le RE** : `pxor xmm0, xmm0` est l'équivalent SIMD de `xor eax, eax` — c'est la mise à zéro standard d'un registre SSE. Vous le verrez constamment.

**Réarrangement et extraction :**

```asm
pshufd  xmm0, xmm1, 0xff    ; redistribue les 4 doublewords selon un masque  
punpcklbw xmm0, xmm1        ; entrelace les octets de deux registres  
pmovmskb eax, xmm0           ; extrait le bit de poids fort de chaque octet → masque 16 bits  
```

**Conversion :**

```asm
cvtsi2sd  xmm0, eax        ; convertit int → double  
cvttsd2si eax, xmm0        ; convertit double → int (troncature)  
cvtss2sd  xmm0, xmm0       ; convertit float → double  
```

Ces instructions de conversion apparaissent dans le code arithmétique mixte (calculs impliquant à la fois des entiers et des flottants).

---

## Pourquoi GCC génère du SIMD dans votre code

Même si votre code C n'utilise aucune fonction SIMD explicite, GCC en génère dans quatre situations courantes :

### 1. Arithmétique flottante scalaire

En x86-64, **toute l'arithmétique flottante** passe par les registres SSE. L'ancienne unité x87 (`st0`–`st7`) n'est presque plus utilisée. Un simple `a + b` sur des `double` génère :

```c
double add(double a, double b) {
    return a + b;
}
```

```asm
addsd   xmm0, xmm1       ; a dans xmm0, b dans xmm1, résultat dans xmm0  
ret  
```

Ce n'est pas du SIMD au sens « opérations parallèles » — c'est du SSE scalaire. Mais les instructions et les registres sont les mêmes. C'est le cas le plus fréquent dans du code ordinaire.

### 2. Auto-vectorisation des boucles (`-O2` / `-O3`)

GCC analyse les boucles et, quand c'est possible, remplace les opérations scalaires par des opérations vectorielles. C'est l'**auto-vectorisation** :

```c
void add_arrays(float *a, const float *b, int n) {
    for (int i = 0; i < n; i++)
        a[i] += b[i];
}
```

En `-O3`, GCC peut générer :

```asm
; Boucle vectorisée — traite 4 floats par itération
.loop:
    movups  xmm0, [rdi+rax]     ; charge 4 floats de a[]
    movups  xmm1, [rsi+rax]     ; charge 4 floats de b[]
    addps   xmm0, xmm1           ; 4 additions en parallèle
    movups  [rdi+rax], xmm0      ; stocke les 4 résultats
    add     rax, 16               ; avance de 16 octets (4 × 4)
    cmp     rax, rcx
    jl      .loop

; Boucle de nettoyage — traite les éléments restants un par un
.cleanup:
    movss   xmm0, [rdi+rax]
    addss   xmm0, [rsi+rax]
    movss   [rdi+rax], xmm0
    add     rax, 4
    ; ...
```

Le pattern typique est une **boucle principale** avec des instructions `ps`/`pd` (packed, parallèle) qui avance par blocs de 16 octets (ou 32 avec AVX), suivie d'une **boucle de nettoyage** (*cleanup loop* ou *epilog*) qui traite les éléments restants en scalaire (`ss`/`sd`).

> 💡 **Pour le RE** : si vous voyez une boucle avec des `movups`/`addps`/`movups` qui avance par pas de 16, suivie d'une petite boucle avec `movss`/`addss` qui avance par pas de 4, c'est une boucle auto-vectorisée. La logique est la même que la version scalaire — elle va juste 4× plus vite.

### 3. Opérations sur les chaînes et les buffers (libc)

Les implémentations optimisées de `memcpy`, `memset`, `strcmp`, `strlen` dans la glibc utilisent massivement les instructions SIMD pour traiter 16 ou 32 octets à la fois. Si vous entrez dans le code de ces fonctions (en analyse dynamique avec GDB, ou dans un binaire statique), vous verrez du SIMD intensif.

Un `strcmp` optimisé, par exemple, charge 16 octets de chaque chaîne dans des registres `xmm`, les compare en bloc avec `pcmpeqb`, et utilise `pmovmskb` pour extraire un masque de bits indiquant les positions différentes.

### 4. Instructions SSE4.2 pour les chaînes

SSE4.2 a introduit des instructions spécialement conçues pour le traitement de chaînes, que la glibc utilise quand le processeur les supporte :

```asm
pcmpistri xmm0, [rdi], 0x18    ; compare des chaînes implicitement terminées par NUL  
pcmpistrm xmm0, [rdi], 0x40    ; idem mais retourne un masque dans xmm0  
```

Ces instructions sont puissantes mais complexes (le byte immédiat encode de multiples options). En RE, sachez simplement qu'elles apparaissent dans les fonctions de manipulation de chaînes optimisées.

---

## Stratégie de lecture du SIMD en RE

La plupart du temps, vous n'avez **pas besoin** de comprendre chaque instruction SIMD en détail. Voici la stratégie recommandée :

### Niveau 1 — Identifier et contourner (suffisant dans 80% des cas)

Quand vous rencontrez un bloc de SIMD :

1. **Identifiez le contexte** : est-ce dans une boucle de calcul, dans une fonction de la libc, dans un `memcpy` ?  
2. **Cherchez la version scalaire** : souvent, juste après la boucle vectorisée, il y a une boucle scalaire de nettoyage qui fait la même chose instruction par instruction — elle est bien plus facile à lire.  
3. **Résumez le bloc** par un commentaire de haut niveau : `// copie vectorisée de 16 octets`, `// comparaison parallèle de chaînes`, `// addition de 4 floats`.  
4. **Passez au bloc suivant** — le SIMD est rarement la logique intéressante en RE applicatif.

### Niveau 2 — Comprendre la logique vectorielle (quand c'est nécessaire)

Si le SIMD **est** la logique intéressante (crypto vectorisée, parseur SIMD, traitement d'image), vous devez le décoder :

1. **Identifiez le type de données** via les suffixes (`ps` = float, `pd` = double, `b` = bytes, `d` = int32…).  
2. **Suivez les données registre par registre** — imaginez chaque registre `xmm` comme un petit tableau.  
3. **Utilisez un brouillon** : dessinez les registres sous forme de boîtes découpées en éléments et tracez les opérations.  
4. **Consultez la référence Intel** : le *Intel Intrinsics Guide* (en ligne) donne une description visuelle de chaque instruction avec des diagrammes.

### Niveau 3 — Identifier les algorithmes connus

Certains algorithmes ont des implémentations SIMD reconnaissables par leurs constantes et leurs séquences d'instructions :

- **AES-NI** : `aesenc`, `aesdec`, `aeskeygenassist` — instructions dédiées au chiffrement AES, intégrées au processeur depuis 2010. Si vous voyez ces mnémoniques, le code fait du AES matériel.  
- **CRC32** : `crc32` — instruction SSE4.2 dédiée.  
- **SHA** : `sha256rnds2`, `sha256msg1` — instructions SHA Extensions.  
- **CLMUL** : `pclmulqdq` — multiplication sans retenue, utilisée en GCM (mode de chiffrement authentifié).

> 💡 **Pour le RE crypto** : la présence d'instructions AES-NI (`aesenc`, `aesdec`) identifie immédiatement l'algorithme de chiffrement — pas besoin de reconnaître les constantes S-box manuellement. C'est un raccourci majeur par rapport au RE d'implémentations AES purement logicielles. Le chapitre 24 couvre l'identification des routines crypto en détail.

---

## Idiomes SIMD courants générés par GCC

Quelques patterns que vous rencontrerez fréquemment et leur signification :

### Mise à zéro d'un registre SSE

```asm
pxor    xmm0, xmm0           ; SSE : xmm0 = 0  
vpxor   xmm0, xmm0, xmm0    ; AVX : xmm0 = 0 (encodage VEX)  
vxorps  ymm0, ymm0, ymm0    ; AVX : ymm0 = 0 (256 bits)  
```

Équivalent de `xor eax, eax` pour les registres scalaires.

### Conversion entier ↔ flottant

```asm
cvtsi2sd  xmm0, eax          ; (int)eax → (double)xmm0  
cvttsd2si eax, xmm0          ; (double)xmm0 → (int)eax (troncature)  
```

Apparaît chaque fois que du code C mélange `int` et `double` dans une expression.

### Copie de blocs mémoire (memcpy/memset inliné)

```asm
; GCC inline un petit memcpy avec des instructions SSE
movdqu  xmm0, [rsi]  
movdqu  [rdi], xmm0          ; copie 16 octets  
movdqu  xmm0, [rsi+0x10]  
movdqu  [rdi+0x10], xmm0     ; copie 16 octets de plus  
```

Quand la taille est connue à la compilation et assez petite, GCC remplace l'appel à `memcpy` par une série de `movdqu` inline. C'est plus rapide qu'un appel de fonction pour les petites copies.

### Broadcast d'une valeur (AVX)

```asm
vbroadcastss ymm0, [rdi]     ; copie un float dans les 8 positions du ymm  
vpbroadcastd ymm0, xmm0      ; copie un int32 dans les 8 positions du ymm  
```

Prépare un registre où toutes les positions contiennent la même valeur, typiquement avant une opération parallèle (comparer un tableau à une constante, remplir un buffer…).

### Comparaison de chaînes optimisée

```asm
; strcmp optimisé (pattern simplifié)
movdqu  xmm0, [rdi]              ; charge 16 octets de la chaîne 1  
movdqu  xmm1, [rsi]              ; charge 16 octets de la chaîne 2  
pcmpeqb xmm0, xmm1               ; compare octet par octet : 0xFF si égal, 0x00 sinon  
pmovmskb eax, xmm0                ; extrait 1 bit par octet → masque 16 bits dans eax  
cmp     eax, 0xffff               ; 0xFFFF = tous les 16 octets sont égaux  
jne     .differ                    ; si non, il y a une différence  
```

`pmovmskb` (*Packed Move Mask Byte*) est l'instruction clé : elle extrait le bit de poids fort de chaque octet du registre `xmm` et les concatène dans un registre 32 bits. C'est le pont entre le monde SIMD (128 bits) et le monde scalaire (conditions, sauts).

---

## SIMD et décompilateurs

Les décompilateurs modernes gèrent le SIMD de manière variable :

- **Ghidra** : reconnaît les instructions SIMD de base et les affiche dans le décompilateur, mais le résultat est souvent difficile à lire — il utilise des types vectoriels peu familiers et des casts complexes. La vue assembleur est parfois plus claire pour le SIMD.  
- **IDA + Hex-Rays** : meilleure gestion avec des types intrinsèques (`__m128i`, `_mm_add_ps`…), mais le résultat reste verbeux.  
- **Binary Ninja** : support comparable à Ghidra.

En pratique, pour le SIMD, la vue assembleur annotée manuellement reste souvent l'approche la plus productive. Le décompilateur est utile pour le code scalaire autour du SIMD, mais les blocs vectoriels eux-mêmes méritent une lecture directe.

---

## Ce qu'il faut retenir pour la suite

1. **Le SIMD est normal dans les binaires modernes** — GCC en génère pour l'arithmétique flottante, l'auto-vectorisation des boucles, et les copies mémoire optimisées. Ce n'est pas exotique.  
2. **Les registres `xmm0`–`xmm15`** (128 bits) sont omniprésents : pour les flottants scalaires (convention d'appel) et pour le SIMD. Les registres `ymm` (256 bits AVX) et `zmm` (512 bits AVX-512) sont plus rares.  
3. **Les suffixes révèlent le type** : `ss` = 1 float, `sd` = 1 double, `ps` = 4 floats parallèles, `pd` = 2 doubles parallèles, `b`/`w`/`d`/`q` = entiers de taille variable.  
4. **Le préfixe `v`** = encodage AVX. `addps` est SSE, `vaddps` est AVX — la sémantique est la même.  
5. **`pxor xmm, xmm`** = mise à zéro. **`pmovmskb`** = pont entre SIMD et scalaire. **`cvtsi2sd`/`cvttsd2si`** = conversion int↔double.  
6. **Stratégie en RE** : dans 80% des cas, identifiez le contexte, résumez le bloc SIMD en un commentaire de haut niveau, et passez au bloc suivant. Ne décodez instruction par instruction que si le SIMD *est* la logique qui vous intéresse.  
7. **AES-NI** (`aesenc`, `aesdec`) et **CRC32** identifient instantanément l'algorithme — c'est un raccourci puissant en RE crypto.

---


⏭️ [🎯 Checkpoint : annoter manuellement un désassemblage réel (fourni)](/03-assembleur-x86-64/checkpoint.md)
