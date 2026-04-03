🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 16.5 — Optimisations Link-Time (`-flto`) et leurs effets sur le graphe d'appels

> **Fichiers source associés** : `binaries/ch16-optimisations/lto_main.c`, `lto_math.c`, `lto_utils.c` (+ headers)  
> **Compilation** : `make s16_5` (produit 12 variantes : 6 sans LTO, 6 avec `-flto`)  
> **Comparaison rapide** : `make lto_compare`

---

## Introduction

Jusqu'ici, toutes les optimisations que nous avons étudiées opéraient à l'intérieur d'une seule **unité de compilation** — un fichier `.c` compilé en un fichier `.o`. Quand GCC compile `lto_main.c`, il ne voit pas le code de `lto_math.c`. Il sait que `math_square()` existe (grâce à la déclaration dans le header), mais il ne connaît pas son corps. Il ne peut donc pas l'inliner, propager ses constantes, ni éliminer ses branches mortes.

C'est une limitation fondamentale du modèle de compilation séparé hérité du C des années 1970 : chaque fichier `.c` est compilé indépendamment, puis le linker assemble les fichiers `.o` en un exécutable final. Le linker classique (`ld`) ne fait que résoudre des adresses de symboles — il ne touche pas au code machine lui-même.

La **Link-Time Optimization** (`-flto`) change radicalement ce modèle. Avec `-flto`, GCC ne produit pas du code machine dans les fichiers `.o` : il y stocke une représentation intermédiaire (GIMPLE) du programme. Au moment du link, tous les fichiers GIMPLE sont fusionnés et GCC applique ses passes d'optimisation sur le **programme entier**, comme si tous les fichiers `.c` avaient été concaténés en un seul.

Les conséquences pour le reverse engineer sont considérables :

- Des fonctions définies dans des fichiers séparés peuvent être **inlinées cross-module** — elles disparaissent du binaire alors qu'elles auraient survécu sans LTO.  
- Les **constantes se propagent** à travers les frontières de fichiers : une valeur passée en paramètre dans `main.c` peut être substituée directement dans le corps d'une fonction définie dans `math.c`.  
- Le **code mort inter-module** est éliminé : si une fonction exportée dans `utils.c` n'est jamais appelée par aucun autre fichier, elle est supprimée.  
- Le **graphe d'appels** du binaire est radicalement simplifié — ou plutôt aplati, car des niveaux entiers d'indirection disparaissent.

Cette section explore ces transformations avec des comparaisons côte à côte de binaires compilés avec et sans `-flto`.

---

## Comment fonctionne LTO en interne

Pour comprendre les effets de LTO sur le binaire final, il est utile de savoir ce qui se passe dans la chaîne de compilation.

### Compilation classique (sans LTO)

```
lto_main.c  ──→  gcc -O2 -c  ──→  lto_main.o   (code machine x86-64)  
lto_math.c  ──→  gcc -O2 -c  ──→  lto_math.o   (code machine x86-64)  
lto_utils.c ──→  gcc -O2 -c  ──→  lto_utils.o  (code machine x86-64)  

lto_main.o + lto_math.o + lto_utils.o  ──→  ld  ──→  lto_demo_O2
                                              ↑
                                         Résolution de symboles uniquement.
                                         Le code machine n'est pas modifié.
```

Chaque `.o` contient du code machine final. Le linker colle les sections `.text` ensemble et résout les adresses des symboles (`math_square`, `utils_clamp`, etc.) mais ne modifie pas les instructions.

### Compilation avec LTO (`-flto`)

```
lto_main.c  ──→  gcc -O2 -flto -c  ──→  lto_main.o   (GIMPLE IR + bytecode)  
lto_math.c  ──→  gcc -O2 -flto -c  ──→  lto_math.o   (GIMPLE IR + bytecode)  
lto_utils.c ──→  gcc -O2 -flto -c  ──→  lto_utils.o  (GIMPLE IR + bytecode)  

lto_main.o + lto_math.o + lto_utils.o  ──→  gcc -flto (lto1 + ld)  ──→  lto_demo_O2_flto
                                              ↑
                                         1. Fusion des GIMPLE de tous les .o
                                         2. Optimisation globale (inlining,
                                            propagation, DCE, dévirtualisation)
                                         3. Génération du code machine final
                                         4. Linkage classique
```

Les fichiers `.o` produits avec `-flto` contiennent la représentation GIMPLE — l'IR (Intermediate Representation) de GCC, un arbre syntaxique simplifié qui conserve toute la sémantique du programme. Au moment du link, le compilateur (`lto1`) fusionne les IR de tous les fichiers, applique les passes d'optimisation sur l'ensemble, puis génère le code machine final.

Le résultat est un binaire qui a été optimisé comme si tout le code avait été écrit dans un seul fichier — mais avec la modularité de compilation séparée côté développement.

### Vérifier la présence de LTO dans un `.o`

Un fichier `.o` compilé avec `-flto` contient des sections spéciales :

```bash
$ readelf -S lto_math.o | grep lto
  [3] .gnu.lto_.decls   PROGBITS  ...
  [4] .gnu.lto_.symtab  PROGBITS  ...
  [5] .gnu.lto_main.0   PROGBITS  ...
```

Les sections `.gnu.lto_*` contiennent l'IR GIMPLE sérialisée. C'est un indice pour le RE : si vous trouvez ces sections dans un `.o` fourni (par exemple dans un SDK), le développeur utilise LTO.

---

## Effet 1 — Inlining cross-module

C'est l'effet le plus spectaculaire de LTO : des fonctions définies dans un fichier séparé peuvent être inlinées dans l'appelant, exactement comme les fonctions `static` du même fichier.

### Les fonctions triviales disparaissent

Dans `lto_math.c` :

```c
int math_square(int x)
{
    return x * x;
}

int math_cube(int x)
{
    return x * x * x;
}
```

Dans `lto_main.c` :

```c
int sq = math_square(input);  
int cb = math_cube(input);  
```

#### Sans LTO (`-O2`)

`math_square` et `math_cube` sont dans un fichier `.o` séparé. GCC ne voit pas leur corps lors de la compilation de `lto_main.c`. Les appels génèrent des `call` explicites :

```asm
    ; dans main()
    mov    edi, ebx                     ; input
    call   math_square                  ; call via PLT ou direct
    mov    r12d, eax                    ; sq = résultat

    mov    edi, ebx
    call   math_cube
    mov    r13d, eax                    ; cb = résultat
```

Les deux fonctions existent comme symboles dans le binaire :

```bash
$ nm build/lto_demo_O2 | grep ' T '
0000000000401190 T main
0000000000401350 T math_cube
0000000000401340 T math_square
0000000000401380 T math_complex_transform
...
```

#### Avec LTO (`-O2 -flto`)

GCC fusionne les IR des trois fichiers et inline `math_square` et `math_cube` dans `main()` :

```asm
    ; dans main() — math_square inliné
    imul   r12d, ebx, ebx              ; sq = input * input

    ; math_cube inliné
    mov    eax, ebx
    imul   eax, ebx
    imul   r13d, eax, ebx              ; cb = input * input * input
    ; (ou une combinaison lea/imul selon la version de GCC)
```

Les symboles `math_square` et `math_cube` ont disparu :

```bash
$ nm build/lto_demo_O2_flto | grep -E 'math_square|math_cube'
# (rien)
```

### Comparaison du graphe d'appels dans Ghidra

C'est un point crucial pour le RE. Si vous ouvrez les deux binaires dans Ghidra :

**Sans LTO** — le graphe d'appels de `main()` montre des XREF vers `math_square`, `math_cube`, `math_sum_of_powers`, `math_hash`, `math_divide_sum`, `math_complex_transform`, `utils_fill_sequence`, `utils_array_max`, `utils_clamp`, `utils_int_to_hex`, `utils_print_array`. Le graphe est complet et reflète l'architecture modulaire du source.

**Avec LTO** — le graphe de `main()` ne montre que les fonctions qui n'ont pas été inlinées (celles qui étaient trop volumineuses : `math_complex_transform`, `utils_print_array`) plus les appels à la libc (`printf`, `sqrt`). Les fonctions triviales et moyennes ont été absorbées. Le graphe est aplati.

Vérifiez par vous-même avec la cible `lto_compare` du Makefile :

```bash
$ make lto_compare
```

---

## Effet 2 — Propagation de constantes cross-module

Sans LTO, quand `main()` appelle `math_divide_sum(data, 32, 7)`, le compilateur de `lto_math.c` ne sait pas que `divisor` vaut `7` — c'est un paramètre quelconque. Il ne peut pas appliquer l'optimisation du magic number (cf. section 16.6) à la compilation de `lto_math.c` car le diviseur est une variable.

En pratique, le compilateur peut toujours optimiser la division si `divisor` n'est pas connu : il utilise `idiv`. Mais si le diviseur était connu, il pourrait utiliser le magic number beaucoup plus efficace.

### Sans LTO

Dans `math_divide_sum` compilée séparément :

```c
int math_divide_sum(const int *data, int n, int divisor)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += data[i] / divisor;
    }
    return sum;
}
```

```asm
math_divide_sum:
    ; divisor est dans edx — valeur inconnue à la compilation
    ; ...
.L_loop:
    mov    eax, DWORD PTR [rdi+rcx*4]
    cdq
    idiv   esi                          ; division par variable → idiv
    add    r8d, eax
    add    rcx, 1
    cmp    ecx, edx
    jl     .L_loop
```

Le compilateur est contraint d'utiliser `idiv` (20–90 cycles par itération) car il ne connaît pas la valeur du diviseur.

### Avec LTO

GCC voit que `main()` appelle `math_divide_sum(data, 32, 7)` — le troisième argument est la constante `7`. Il propage cette constante dans le corps de la fonction et applique le remplacement par magic number :

```asm
    ; math_divide_sum inliné dans main(), divisor = 7 propagé
.L_loop:
    mov    eax, DWORD PTR [rdi+rcx*4]
    ; Division par 7 via magic number
    mov    edx, 0x92492493
    imul   edx                          ; multiplication par magic number
    add    edx, eax
    sar    edx, 2
    mov    eax, edx
    shr    eax, 31
    add    edx, eax                     ; edx = data[i] / 7
    add    r8d, edx
    add    rcx, 1
    cmp    ecx, 32                      ; n = 32 propagé aussi !
    jl     .L_loop
```

Deux constantes ont été propagées cross-module :

1. `divisor = 7` → le `idiv` est remplacé par le magic number `0x92492493`.  
2. `n = 32` → la borne de boucle est un immédiat, ce qui ouvre la porte au déroulage et à la vectorisation.

### Ce que le RE doit retenir

Quand vous voyez un magic number de division dans le corps de `main()` (ou d'une fonction de haut niveau) d'un binaire LTO, il peut provenir d'une fonction définie dans un tout autre module. Sans LTO, ce même magic number se trouverait dans une fonction séparée avec un `call` pour y accéder. Avec LTO, il apparaît « sorti de nulle part » au milieu de `main()`.

C'est déstabilisant, mais la technique de reconnaissance reste la même : identifier le magic number, retrouver le diviseur (cf. section 16.6), et en déduire la logique originale.

---

## Effet 3 — Élimination de code mort inter-module

Sans LTO, le linker ne peut pas savoir si une fonction exportée sera utilisée par un autre fichier `.o` — il la conserve par précaution. Avec LTO, le compilateur voit le programme entier et peut déterminer qu'une fonction n'est jamais appelée.

### Exemple

Imaginons que `utils_int_to_hex()` n'est appelée nulle part (ou que l'appel est dans une branche `if (0)` éliminée par propagation de constantes). Sans LTO, elle reste dans le binaire. Avec LTO, elle est supprimée.

On peut observer cet effet en comparant les symboles :

```bash
# Sans LTO — toutes les fonctions publiques sont présentes
$ nm build/lto_demo_O2 | grep ' T ' | wc -l
12

# Avec LTO — seules les fonctions réellement appelées survivent
$ nm build/lto_demo_O2_flto | grep ' T ' | wc -l
5
```

Les 7 fonctions manquantes ont été soit inlinées (et leur code fusionné dans `main()`), soit éliminées comme code mort.

### Ce que le RE doit retenir

Un binaire LTO est souvent **plus petit** qu'un binaire non-LTO (malgré l'inlining, qui duplique du code) grâce à l'élimination du code mort. Si vous analysez un binaire et que vous trouvez étonnamment peu de fonctions pour un programme qui semble complexe, LTO est une hypothèse à considérer.

---

## Effet 4 — Constantes magiques déplacées

L'un des effets les plus déroutants de LTO pour le RE est le **déplacement de constantes reconnaissables** d'une fonction vers une autre.

### Le cas de `math_hash`

```c
unsigned int math_hash(const char *str)
{
    unsigned int hash = 0x5F3759DF;  /* Constante reconnaissable */

    while (*str) {
        hash = hash * 31 + (unsigned char)(*str);
        str++;
    }

    hash ^= (hash >> 16);
    hash *= 0x45D9F3B;
    hash ^= (hash >> 16);

    return hash;
}
```

#### Sans LTO

La constante `0x5F3759DF` et le multiplicateur `31` se trouvent dans le corps de `math_hash`, une fonction clairement identifiable par son symbole. Un analyste qui repère ces constantes dans Ghidra peut immédiatement identifier un hash polynomial avec finalisation de type murmurhash.

```bash
$ objdump -d build/lto_demo_O2 | grep '5f3759df'
  401360:   mov    eax, 0x5f3759df       ← dans math_hash
```

#### Avec LTO

`math_hash` est inlinée dans `main()`. Ses constantes apparaissent au milieu du code de `main()`, sans contexte évident :

```bash
$ objdump -d build/lto_demo_O2_flto | grep '5f3759df'
  401234:   mov    eax, 0x5f3759df       ← dans main(), au milieu d'autre code
```

L'analyste qui parcourt `main()` dans Ghidra voit `0x5F3759DF` au milieu d'un long flux d'instructions. Sans connaître cette constante, il n'a pas d'indication immédiate qu'il s'agit d'un hash. Et même s'il la reconnaît, il ne sait pas qu'elle provenait d'une fonction séparée — elle semble faire partie de `main()`.

### Les constantes crypto sont particulièrement affectées

Ce phénomène est critique lors de l'analyse de binaires utilisant de la cryptographie. Les constantes magiques typiques (S-box AES, vecteurs d'initialisation SHA-256, constantes de round) sont normalement regroupées dans une fonction identifiable (`aes_encrypt`, `sha256_update`). Avec LTO, elles peuvent se retrouver dispersées dans les fonctions appelantes.

L'Annexe J du tutoriel liste les constantes crypto courantes. Gardez-la sous la main : la reconnaissance de constantes reste le meilleur outil pour identifier les algorithmes, même quand LTO a dispersé le code.

### Ce que le RE doit retenir

Face à un binaire LTO, la stratégie de « trouver la fonction puis comprendre son corps » ne fonctionne plus pour les fonctions inlinées. Il faut inverser l'approche : **chercher les constantes d'abord** (magic numbers de hash, de crypto, de division), puis reconstituer les frontières logiques des « fonctions fantômes » autour de ces constantes.

---

## Effet 5 — Fonctions volumineuses : elles survivent à LTO

Tout n'est pas inliné avec LTO. Les mêmes heuristiques de taille que pour l'inlining intra-fichier s'appliquent. Les fonctions volumineuses restent des `call` explicites.

```c
long math_complex_transform(const int *data, int n)
{
    /* Trois boucles, des branchements, des calculs statistiques...
     * ~50 gimple statements → trop gros pour l'inlining */
    // ...
}
```

### Avec LTO

```bash
$ nm build/lto_demo_O2_flto | grep math_complex_transform
0000000000401280 T math_complex_transform
```

La fonction survit. Dans `main()`, on voit toujours un `call math_complex_transform`. Cependant, LTO peut tout de même l'optimiser « de l'intérieur » — par exemple en propageant les constantes connues dans `main()` comme arguments de la fonction.

### Ce que le RE doit retenir

La présence d'une fonction comme symbole dans un binaire LTO est un indice de sa taille et de sa complexité. Les fonctions visibles dans un binaire LTO sont les « grosses » fonctions du programme — celles qui méritent une analyse approfondie.

---

## Effet 6 — Dévirtualisation inter-module

LTO peut résoudre certains appels indirects (pointeurs de fonctions) quand la cible est déterminable à la compilation. C'est la **dévirtualisation** — une optimisation particulièrement importante pour le C++ (appels virtuels via vtable) mais qui s'applique aussi au C.

### Exemple en C

```c
/* Dans lto_main.c */
unsigned int h1 = math_hash("hello");
```

Sans LTO, si `math_hash` est appelée via un pointeur de fonction (ce qui n'est pas le cas ici mais illustre le principe), le compilateur de `lto_main.c` ne peut pas résoudre la cible. Avec LTO, il voit l'ensemble du programme et peut déterminer que le pointeur pointe toujours vers `math_hash`.

En C++, l'effet est beaucoup plus marqué. Un appel virtuel `obj->method()` passe par la vtable (un `call [rax+offset]`). Si LTO peut prouver que le type dynamique de `obj` est toujours le même (par exemple parce que l'objet est construit localement et jamais transmis à du code polymorphe), il remplace l'appel indirect par un appel direct — voire inline la méthode.

### Ce que le RE doit retenir

Si vous comparez un binaire C++ avec et sans LTO, vous pouvez observer que certains `call [rax+offset]` (appels via vtable) sont remplacés par des `call direct_function` ou même par du code inliné. C'est la dévirtualisation. Pour le RE, cela signifie que le binaire LTO contient **moins d'appels indirects** — ce qui facilite l'analyse (les cibles sont explicites) mais masque la nature polymorphe du code source.

---

## Identifier un binaire LTO

Comment savoir si un binaire que vous analysez a été compilé avec LTO ? Il n'y a pas d'indicateur explicite dans le binaire final (les sections `.gnu.lto_*` n'existent que dans les `.o` intermédiaires), mais plusieurs indices convergent :

### Indice 1 — Peu de fonctions malgré un programme complexe

Un binaire LTO a typiquement moins de fonctions que son équivalent non-LTO. Si un programme qui devrait avoir des dizaines de modules ne montre que 5–10 fonctions dans `nm` ou Ghidra, LTO est probable.

### Indice 2 — `main()` anormalement longue

Avec LTO, les fonctions de tous les modules peuvent être inlinées dans `main()`. Une fonction `main()` qui fait 500 lignes de décompilation dans Ghidra, avec des constantes hétérogènes (hash, crypto, parsing) mélangées, est caractéristique d'un binaire LTO.

### Indice 3 — Absence de certaines fonctions utilitaires

Dans un programme multi-fichiers sans LTO, les fonctions utilitaires (clamp, min, max, wrappers) existent comme symboles même si elles sont triviales — car le compilateur de l'appelant ne pouvait pas les inliner. Si ces fonctions sont absentes alors que la logique qui les utilise est présente dans le binaire, c'est un signe de LTO.

### Indice 4 — Magic numbers « orphelins »

Des constantes reconnaissables (hash, crypto, CRC) qui apparaissent au milieu de `main()` ou d'une fonction de haut niveau, sans être dans une fonction dédiée, suggèrent un inlining cross-module rendu possible par LTO.

### Indice 5 — Commentaires dans les informations de debug

Si le binaire n'est pas strippé et contient des informations DWARF, les entrées `DW_TAG_inlined_subroutine` peuvent référencer des fonctions définies dans d'autres fichiers source. Cet inlining cross-fichier n'est possible qu'avec LTO (ou avec des fonctions inline dans des headers) :

```bash
$ readelf --debug-dump=info build/lto_demo_O2_flto | grep -B2 'DW_AT_abstract_origin'
# Peut montrer des fonctions de lto_math.c inlinées dans lto_main.c
```

---

## LTO et la taille du binaire

L'effet de LTO sur la taille du binaire est contre-intuitif : il peut **augmenter ou diminuer** la taille selon les cas.

**Facteurs de réduction :**
- Élimination de code mort inter-module (fonctions jamais appelées supprimées).  
- Élimination de prologues/épilogues des fonctions inlinées.  
- Propagation de constantes → simplification de code (ex: `idiv` remplacé par magic number plus court, branches mortes éliminées).

**Facteurs d'augmentation :**
- Inlining cross-module → duplication du corps à chaque site d'appel.  
- Déroulage et vectorisation plus agressifs grâce à la propagation de constantes (bornes de boucles connues).

En pratique, LTO réduit souvent la taille de quelques pourcents pour les programmes de taille moyenne. Pour les gros programmes avec beaucoup de code mort, la réduction peut être significative (5–15%).

```bash
$ make lto_compare
=== Différence de taille ===
  Sans LTO : 21456 octets
  Avec LTO : 19832 octets
```

---

## Compilation en une seule passe vs LTO : quelle différence ?

Une question légitime : si LTO revient à optimiser tout le programme d'un coup, pourquoi ne pas simplement compiler tous les `.c` ensemble ?

```bash
# "LTO du pauvre" — tout dans une seule commande
gcc -O2 -g -o lto_demo_single lto_main.c lto_math.c lto_utils.c -lm
```

En pratique, cette commande **produit un résultat très similaire à LTO** pour un petit projet. GCC concatène les IR des trois fichiers et les optimise ensemble.

La différence apparaît dans les projets réels avec des centaines de fichiers :

- La compilation en une passe recompile **tout** à chaque modification — pas de compilation incrémentale.  
- LTO permet la compilation incrémentale : seuls les `.o` modifiés sont recompilés, mais l'étape de link refait l'optimisation globale.  
- LTO dispose de passes d'optimisation spécifiques (partitionnement du graphe d'appels, résumés inter-procéduraux) qui ne sont pas activées dans la compilation monolithique.

Pour le RE, les deux approches produisent des binaires qui se ressemblent beaucoup — les mêmes fonctions sont inlinées, les mêmes constantes propagées. La distinction est transparente dans le binaire final.

---

## Résumé : avec et sans LTO du point de vue du RE

| Aspect | Sans LTO (`-O2`) | Avec LTO (`-O2 -flto`) |  
|---|---|---|  
| Fonctions triviales cross-module | `call` explicite (symbole visible) | Inlinées, disparues |  
| Fonctions moyennes cross-module | `call` explicite | Inlinées si peu de sites d'appel |  
| Fonctions volumineuses | `call` explicite | `call` explicite (survivent) |  
| Graphe d'appels dans Ghidra | Complet, reflète l'architecture | Aplati, peu de XREF |  
| Division par paramètre constant | `idiv` (diviseur inconnu à la compil.) | Magic number (constante propagée) |  
| Constantes magiques (hash, crypto) | Dans la fonction dédiée | Dispersées dans l'appelant |  
| Code mort inter-module | Conservé par le linker | Éliminé |  
| Taille du binaire | Référence | Souvent légèrement plus petit |  
| Appels indirects (C++ vtable) | `call [reg+offset]` | Potentiellement dévirtualisés |  
| Identification en RE | Nombre « normal » de fonctions | Peu de fonctions, `main()` longue |

---

## Stratégie d'analyse pour les binaires LTO

Quand vous suspectez qu'un binaire a été compilé avec LTO, voici une approche méthodique :

**1. Commencer par les fonctions qui ont survécu.** Ce sont les fonctions volumineuses — elles contiennent la logique complexe du programme. Analysez-les en priorité : elles ont des frontières claires, des XREF exploitables, et leur décompilation dans Ghidra est généralement de bonne qualité.

**2. Chercher les constantes reconnaissables dans `main()`.** Les magic numbers de hash, les constantes crypto, les diviseurs transformés en magic numbers multiplicatifs — ce sont vos repères pour identifier les « fonctions fantômes » inlinées dans `main()`.

**3. Segmenter `main()` en blocs logiques.** Dans Ghidra, utilisez les commentaires et les renommages pour annoter les régions de `main()` qui correspondent à une fonction inlinée. Nommez-les `/* inlined: math_hash */` ou similaire.

**4. Chercher les patterns dupliqués.** Si une même séquence de constantes et d'instructions apparaît plusieurs fois dans `main()`, c'est une fonction inlinée appelée à plusieurs sites. Chaque occurrence est une copie du même corps.

**5. Utiliser Compiler Explorer pour valider.** Si vous avez identifié une bibliothèque ou un algorithme, compilez-le sur [godbolt.org](https://godbolt.org) avec `-O2 -flto` et comparez le pattern assembleur avec ce que vous voyez dans le binaire.

**6. Comparer avec un build sans LTO si possible.** Si vous avez accès aux sources ou à un binaire de debug, la comparaison `nm` avec et sans LTO révèle immédiatement quelles fonctions ont été inlinées.

---


⏭️ [Reconnaître les patterns typiques de GCC (idiomes compilateur)](/16-optimisations-compilateur/06-patterns-idiomes-gcc.md)
