🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 34.3 — Structures de données Go en mémoire : slices, maps, interfaces, channels

> 🐹 *En C, un tableau est un pointeur, une chaîne est un pointeur null-terminé, et une « interface » n'existe pas au niveau du langage. En Go, chaque structure de données fondamentale porte des métadonnées en mémoire — longueur, capacité, pointeur de type, compteur interne. Pour le reverse engineer, ces structures ont des layouts fixes et reconnaissables qui, une fois maîtrisés, permettent de reconstruire rapidement la logique d'un programme Go à partir du désassemblage.*

---

## Préambule : tailles et alignement sur amd64

Tout au long de cette section, les tailles et offsets sont donnés pour l'architecture **amd64** (Linux 64 bits), qui est la cible principale de cette formation. Sur amd64 :

- un pointeur occupe 8 octets,  
- un `int` Go occupe 8 octets (Go fixe la taille de `int` à la taille du mot machine),  
- l'alignement naturel est de 8 octets pour les types de 8 octets.

Sur arm64, les tailles sont identiques. Sur les architectures 32 bits, divisez par deux.

---

## Slices

Le slice est la structure de données la plus omniprésente en Go. Chaque `[]byte`, `[]int`, `[]string` et même les arguments variadiques (section 34.2) sont des slices.

### Layout mémoire

Un slice est un **header de 24 octets** composé de trois champs :

```
Offset   Taille   Champ       Description
──────   ──────   ─────       ───────────
+0x00    8        ptr         Pointeur vers le premier élément du tableau sous-jacent
+0x08    8        len         Nombre d'éléments actuellement dans le slice
+0x10    8        cap         Capacité totale du tableau sous-jacent
```

Représentation schématique :

```
         Slice header (24 octets)                    Backing array
       ┌──────────┬──────┬──────┐               ┌───┬───┬───┬───┬───┐
       │   ptr  ──┼──────┼──────┼─────────────► │ 0 │ 1 │ 2 │ . │ . │
       ├──────────┤      │      │               └───┴───┴───┴───┴───┘
       │   len=3  │      │      │                 ◄── len ──►
       ├──────────┤      │      │                 ◄────── cap ──────►
       │   cap=5  │      │      │
       └──────────┴──────┴──────┘
```

### Ce que vous verrez en assembleur

Quand un slice est passé en argument (nouvelle ABI ≥ 1.17), il consomme **trois registres** consécutifs :

```asm
; Appel de process(data []byte)
; RAX = ptr   (pointeur vers le backing array)
; RBX = len   (longueur)
; RCX = cap   (capacité)
CALL    main.process
```

Avec l'ancienne ABI (< 1.17), ces trois valeurs occupent trois slots consécutifs de 8 octets sur la pile.

L'accès à un élément `data[i]` produit typiquement :

```asm
; Accès à data[i] dans une boucle
; RAX = ptr, RBX = len, RCX = index courant
CMP     RCX, RBX               ; bounds check : i < len ?  
JAE     runtime.panicIndex      ; si i >= len → panic  
MOVZX   EDX, BYTE PTR [RAX+RCX] ; charge data[i]  
```

Le `CMP` + `JAE` avant chaque accès est le **bounds checking** de Go. C'est un pattern extrêmement fréquent et immédiatement reconnaissable. Dans une boucle serrée, vous le verrez à chaque itération (sauf si le compilateur a pu prouver que l'index est toujours valide et l'a éliminé — ce qui arrive avec les optimisations).

> 💡 **Astuce RE** : le pattern `CMP reg, reg; JAE runtime.panicIndex` est un marqueur fiable d'un accès à un slice ou un tableau. En le repérant, vous savez que le registre comparé contient un index et l'autre contient la longueur. Cela vous aide à reconstruire les boucles.

### Slice vs tableau (array)

Un tableau Go (`[5]int`) est une valeur de taille fixe, allouée en place (sur la pile ou dans une struct). Il n'a **pas** de header — c'est juste `5 × 8 = 40` octets contigus. Les slices, en revanche, sont toujours des headers de 24 octets pointant vers un tableau.

En RE, la distinction se fait par l'usage : si vous voyez trois registres (ptr, len, cap) ou trois mots consécutifs sur la pile, c'est un slice. Si vous voyez un accès direct à un bloc de taille fixe sans header, c'est un tableau.

### `append` et la croissance

L'opération `append` peut déclencher une réallocation si `len == cap`. Le compilateur génère un appel à `runtime.growslice` :

```asm
; s = append(s, elem)
CMP     RBX, RCX               ; len == cap ?  
JNE     .no_grow                ; non → ajout direct  
; Besoin de croître
CALL    runtime.growslice       ; alloue un nouveau backing array
; RAX = nouveau ptr, RBX = nouvelle len, RCX = nouvelle cap
.no_grow:
; Écrit l'élément à la position len, incrémente len
```

> 💡 **Astuce RE** : un `CALL runtime.growslice` vous indique qu'un slice est en train d'être construit dynamiquement — typiquement dans une boucle d'accumulation de données. C'est souvent un indice que la fonction parse ou collecte des éléments.

---

## Maps

Les maps Go (`map[K]V`) sont des tables de hachage implémentées par le runtime. Leur structure interne est significativement plus complexe que les slices.

### Layout mémoire : `runtime.hmap`

Une variable de type `map` en Go est un **pointeur** vers une structure `runtime.hmap` :

```
runtime.hmap (simplifié, amd64)  
Offset   Taille   Champ         Description  
──────   ──────   ─────         ───────────
+0x00    8        count          Nombre d'éléments dans la map
+0x08    1        flags          État interne (iterator, writing, etc.)
+0x09    1        B              Log2 du nombre de buckets (2^B buckets)
+0x0A    2        (padding)
+0x0C    4        noverflow      Nombre approximatif de buckets overflow
+0x10    8        hash0          Seed aléatoire pour la fonction de hachage
+0x18    8        buckets        Pointeur vers le tableau de buckets courant
+0x20    8        oldbuckets     Pointeur vers les anciens buckets (pendant un rehash)
+0x28    8        nevacuate      Compteur de progression de l'évacuation
+0x30    8        extra          Pointeur vers overflow buckets pré-alloués
```

Chaque bucket contient 8 paires clé-valeur organisées ainsi :

```
bmap (bucket, simplifié)
┌──────────────────────────────┐
│  tophash [8]uint8            │  ← 8 octets : hash haut de chaque clé
├──────────────────────────────┤
│  keys [8]KeyType             │  ← 8 × taille(K) octets
├──────────────────────────────┤
│  values [8]ValueType         │  ← 8 × taille(V) octets
├──────────────────────────────┤
│  overflow *bmap              │  ← pointeur vers le bucket overflow suivant
└──────────────────────────────┘
```

Les clés et valeurs sont groupées séparément (toutes les clés ensemble, puis toutes les valeurs) plutôt qu'entrelacées. C'est une optimisation d'alignement mémoire qui réduit le padding.

### Ce que vous verrez en assembleur

La création d'une map génère un appel à `runtime.makemap` :

```asm
; m := make(map[string]int)
; Le compilateur passe un pointeur vers le type descriptor
LEA     RAX, [type.map[string]int]   ; descripteur de type  
XOR     EBX, EBX                      ; hint = 0 (taille initiale)  
CALL    runtime.makemap  
; RAX = pointeur vers hmap
```

L'accès en lecture (`v := m[key]`) passe par `runtime.mapaccess1` ou `runtime.mapaccess2` (la variante `2` retourne en plus un booléen `ok`) :

```asm
; v, ok := m[key]
LEA     RAX, [type.map[string]int]  
MOV     RBX, [adresse_de_m]           ; pointeur hmap  
LEA     RCX, [clé]                    ; pointeur vers la clé  
CALL    runtime.mapaccess2_faststr    ; variante optimisée pour les clés string  
; RAX = pointeur vers la valeur (ou vers une valeur zéro si absente)
; RBX = bool (ok)
```

L'écriture (`m[key] = value`) utilise `runtime.mapassign` :

```asm
LEA     RAX, [type.map[string]int]  
MOV     RBX, [adresse_de_m]  
LEA     RCX, [clé]  
CALL    runtime.mapassign_faststr  
; RAX = pointeur vers le slot où écrire la valeur
MOV     QWORD PTR [RAX], valeur       ; écrit la valeur dans le slot
```

La suppression (`delete(m, key)`) passe par `runtime.mapdelete`.

> 💡 **Astuce RE** : les variantes `_fast32`, `_fast64`, `_faststr` des fonctions map sont des versions optimisées pour les clés de types courants. Le suffixe vous révèle le type de clé : `faststr` → `string`, `fast64` → `int64` ou `uint64`, `fast32` → `int32` ou `uint32`. C'est une information de typage gratuite.

### Itération sur une map

La boucle `for k, v := range m` se traduit par une paire d'appels :

```asm
CALL    runtime.mapiterinit    ; initialise un itérateur (structure hiter sur la pile)
.loop:
; Vérifier si l'itérateur a terminé
CMP     QWORD PTR [RSP+offset_key], 0   ; pointeur de clé == nil ?  
JEQ     .done  
; ... traiter la clé et la valeur ...
CALL    runtime.mapiternext    ; avance au prochain élément  
JMP     .loop  
.done:
```

La structure `hiter` (l'itérateur) fait environ 120 octets et est allouée sur la pile de l'appelant. Le pattern `mapiterinit` + boucle `mapiternext` est caractéristique d'un `range` sur une map.

### Dumper une map depuis GDB

Pour inspecter une map en analyse dynamique, il faut naviguer la structure manuellement :

1. Récupérer le pointeur `hmap` (registre ou pile selon le contexte).  
2. Lire le champ `count` à l'offset `+0x00` pour connaître le nombre d'éléments.  
3. Lire `B` à l'offset `+0x09` pour calculer le nombre de buckets (`1 << B`).  
4. Lire `buckets` à l'offset `+0x18` pour obtenir le début du tableau de buckets.  
5. Parcourir chaque bucket en lisant les `tophash`, puis les clés et valeurs.

> 💡 **Astuce RE** : un raccourci en dynamique consiste à poser un breakpoint sur `runtime.mapaccess1` ou `runtime.mapassign` et observer les arguments. Vous obtiendrez le type descriptor (qui révèle les types K et V) et la clé recherchée ou insérée — sans devoir parser la structure interne.

---

## Interfaces

Les interfaces sont le mécanisme central du polymorphisme en Go. En mémoire, elles prennent deux formes selon que l'interface est vide ou non.

### Interface non-vide : `runtime.iface`

Une interface déclarant au moins une méthode (par exemple `Validator` dans notre crackme) est représentée par une `iface` :

```
runtime.iface (16 octets)  
Offset   Taille   Champ   Description  
──────   ──────   ─────   ───────────
+0x00    8        tab     Pointeur vers l'itab (table d'interface)
+0x08    8        data    Pointeur vers la valeur concrète
```

L'`itab` est la clé du dispatch dynamique. Elle contient la table de méthodes virtuelles pour un couple (type concret, type interface) donné :

```
runtime.itab (simplifié)  
Offset   Taille   Champ       Description  
──────   ──────   ─────       ───────────
+0x00    8        inter       Pointeur vers le type descriptor de l'interface
+0x08    8        _type       Pointeur vers le type descriptor du type concret
+0x10    4        hash        Hash du type concret (accélère les type assertions)
+0x14    4        (padding)
+0x18    8        fun[0]      Pointeur vers la 1ère méthode du type concret
+0x20    8        fun[1]      Pointeur vers la 2ème méthode (si applicable)
...      ...      fun[N]      ...
```

Le tableau `fun` contient les pointeurs de fonctions dans le même ordre que les méthodes déclarées dans l'interface. C'est l'équivalent Go de la vtable C++, mais avec une différence majeure : en C++, la vtable est associée au type concret, tandis que l'itab Go est spécifique au couple (type concret, interface).

### Dispatch dynamique en assembleur

Quand le code appelle une méthode sur une interface :

```go
var v Validator = &ChecksumValidator{...}  
v.Validate(group, index)  
```

Le compilateur génère :

```asm
; RAX = itab pointer (iface.tab)
; RBX = data pointer (iface.data)
; --- Chargement du pointeur de méthode depuis l'itab ---
MOV     RCX, [RAX+0x18]       ; RCX = itab.fun[0] (adresse de Validate)
; --- Préparation des arguments ---
MOV     RAX, RBX               ; arg1 = receiver (data pointer)
; ... autres arguments dans les registres suivants ...
CALL    RCX                    ; appel indirect via le pointeur de méthode
```

Le pattern `MOV reg, [itab+offset]; CALL reg` est la signature du dispatch d'interface Go. L'offset dans l'itab dépend de l'index de la méthode : `+0x18` pour la première, `+0x20` pour la deuxième, etc.

> 💡 **Astuce RE** : quand vous voyez un `CALL reg` (appel indirect) avec le registre chargé depuis une structure à offset fixe, il s'agit presque certainement d'un dispatch d'interface. Pour savoir quelle méthode est appelée, trouvez l'itab en mémoire et lisez le pointeur de fonction correspondant — il vous mènera à l'implémentation concrète.

### Interface vide : `runtime.eface`

L'interface vide `interface{}` (ou `any` depuis Go 1.18) n'a pas besoin d'itab puisqu'elle ne déclare aucune méthode. Sa représentation est plus simple :

```
runtime.eface (16 octets)  
Offset   Taille   Champ    Description  
──────   ──────   ─────    ───────────
+0x00    8        _type    Pointeur vers le type descriptor du type concret
+0x08    8        data     Pointeur vers la valeur concrète
```

La différence avec `iface` : le premier champ pointe directement vers le type descriptor au lieu d'une itab. En assembleur, les conversions vers `interface{}` passent par `runtime.convT` (ou ses variantes `convT64`, `convTstring`, etc.) plutôt que par `runtime.convI` (qui construit une itab).

### Type assertions et type switches

L'assertion de type `v.(ConcreteType)` se traduit par une comparaison du hash de type stocké dans l'itab :

```asm
; val, ok := v.(ConcreteType)
MOV     RAX, [interface_tab]         ; charger l'itab  
MOV     ECX, [RAX+0x10]             ; charger itab.hash  
CMP     ECX, hash_du_type_attendu   ; comparer avec le hash connu à la compilation  
JNE     .not_match  
; ... extraire la valeur ...
```

Le `type switch` produit une cascade de comparaisons de hash similaires.

> 💡 **Astuce RE** : les constantes de hash dans les comparaisons de type assertions sont calculées à la compilation. Si vous retrouvez les mêmes constantes dans les `runtime._type` listés dans `.rodata`, vous pouvez identifier les types concrets testés.

---

## Channels

Les channels sont le mécanisme de communication entre goroutines. En mémoire, un channel est un pointeur vers une structure `runtime.hchan`.

### Layout mémoire : `runtime.hchan`

```
runtime.hchan (simplifié, amd64)  
Offset   Taille   Champ       Description  
──────   ──────   ─────       ───────────
+0x00    8        qcount      Nombre d'éléments actuellement dans le buffer
+0x08    8        dataqsiz    Taille du buffer circulaire (0 = unbuffered)
+0x10    8        buf         Pointeur vers le buffer circulaire
+0x18    8        elemsize    Taille d'un élément
+0x20    4        closed      Flag : le channel est-il fermé ?
+0x24    4        (padding)
+0x28    8        elemtype    Pointeur vers le type descriptor des éléments
+0x30    8        sendx       Index d'écriture dans le buffer circulaire
+0x38    8        recvx       Index de lecture dans le buffer circulaire
+0x40    8        recvq       File d'attente des goroutines en lecture (waitq)
+0x48    8        sendq       File d'attente des goroutines en écriture (waitq)
+0x50    8        lock        Mutex interne (runtime.mutex)
```

### Channels unbuffered vs buffered

La distinction se fait par le champ `dataqsiz` :

- **Unbuffered** (`make(chan int)`) : `dataqsiz = 0`, `buf` est nil. Chaque envoi bloque jusqu'à ce qu'un récepteur soit prêt.  
- **Buffered** (`make(chan int, 10)`) : `dataqsiz = 10`, `buf` pointe vers un tableau circulaire de 10 éléments.

### Ce que vous verrez en assembleur

La création d'un channel :

```asm
; ch := make(chan int, 4)
LEA     RAX, [type.chan int]    ; type descriptor  
MOV     RBX, 4                  ; taille du buffer  
CALL    runtime.makechan  
; RAX = pointeur vers hchan
```

L'envoi sur un channel (`ch <- value`) :

```asm
; ch <- value
MOV     RAX, [adresse_ch]       ; pointeur hchan  
LEA     RBX, [valeur]           ; pointeur vers la valeur à envoyer  
CALL    runtime.chansend1  
```

La réception depuis un channel (`value := <-ch`) :

```asm
; value := <-ch
MOV     RAX, [adresse_ch]       ; pointeur hchan  
LEA     RBX, [destination]      ; pointeur où écrire la valeur reçue  
CALL    runtime.chanrecv1  
```

Le `select` (multiplexage de channels) produit un appel à `runtime.selectgo`, qui prend un tableau de `scase` (select cases) décrivant chaque branche :

```asm
LEA     RAX, [tableau_scase]  
MOV     RBX, nombre_de_cas  
CALL    runtime.selectgo  
; RAX = index du cas sélectionné
```

> 💡 **Astuce RE** : les appels `runtime.chansend1` et `runtime.chanrecv1` vous révèlent les points de synchronisation entre goroutines. En posant des breakpoints sur ces fonctions et en inspectant le pointeur `hchan`, vous pouvez tracer les flux de données inter-goroutines. Lisez `hchan.elemtype` pour connaître le type transmis, et `hchan.qcount` pour savoir combien d'éléments sont en attente dans le buffer.

### Fermeture d'un channel

La fermeture (`close(ch)`) passe par `runtime.closechan`. Le champ `closed` à l'offset `+0x20` passe à une valeur non nulle.

---

## Strings

Les strings Go ne sont **pas** null-terminées. Cette différence fondamentale avec le C a des conséquences directes en RE.

### Layout mémoire : `runtime.stringHeader`

```
String header (16 octets)  
Offset   Taille   Champ   Description  
──────   ──────   ─────   ───────────
+0x00    8        ptr     Pointeur vers les données UTF-8 (non null-terminées)
+0x08    8        len     Longueur en octets
```

C'est le même layout qu'un slice, mais **sans le champ `cap`** — les strings Go sont immuables, elles ne peuvent pas croître.

### Conséquences pour le RE

1. **`strings` (la commande) manque des chaînes.** L'utilitaire `strings` cherche des séquences d'octets imprimables terminées par un null ou de longueur minimale. Les strings Go, stockées bout à bout sans null entre elles dans `.rodata`, forment une longue séquence continue. `strings` peut les fusionner en une seule chaîne géante ou les découper incorrectement.

2. **Les comparaisons de strings ne sont pas des `strcmp`.** En Go, la comparaison de deux strings se fait d'abord par longueur, puis par `memcmp` :

```asm
; Comparaison de deux strings s1 et s2
CMP     RCX, RDI               ; comparer les longueurs  
JNE     .not_equal              ; si len(s1) != len(s2) → pas égales  
; Longueurs identiques → comparer le contenu
MOV     RDI, RAX               ; ptr1  
MOV     RSI, RBX               ; ptr2  
MOV     RDX, RCX               ; len  
CALL    runtime.memequal  
; ou inline : REPE CMPSB
```

3. **Les string literals sont concaténées dans `.rodata`.** Le compilateur Go stocke toutes les constantes string dans une zone contiguë de `.rodata`. Chaque utilisation référence un offset et une longueur dans cette zone. La même séquence d'octets peut être partagée entre plusieurs strings (interning partiel).

> 💡 **Astuce RE** : pour extraire les strings Go correctement, ne vous fiez pas à la commande `strings`. Utilisez plutôt les métadonnées de `gopclntab` ou les structures `runtime.stringHeader` référencées dans le code. En section 34.5, nous verrons des techniques dédiées à l'extraction propre des chaînes Go.

### Strings dans les arguments (nouvelle ABI)

Puisqu'un string est un header de 16 octets (ptr + len), il consomme **deux registres** consécutifs quand il est passé en argument :

```asm
; Appel de process(s string)
; RAX = ptr (pointeur vers les données UTF-8)
; RBX = len (longueur)
CALL    main.process
```

C'est le même comportement que pour les interfaces (2 mots → 2 registres). Gardez cela en tête quand vous comptez les arguments d'une fonction.

---

## Structures (structs)

### Layout mémoire

Les structs Go suivent les mêmes règles de padding et d'alignement que le C :

```go
type Header struct {
    Magic   uint32   // +0x00, 4 octets
    Version uint8    // +0x04, 1 octet
    // padding 3 octets pour aligner le champ suivant
    Length  uint64   // +0x08, 8 octets
    Flags   uint16   // +0x10, 2 octets
    // padding 6 octets pour aligner la struct sur 8 octets
}
// Taille totale : 24 octets
```

Le compilateur Go ne réordonne **jamais** les champs — l'ordre en mémoire est garanti identique à l'ordre de déclaration. C'est une propriété précieuse pour le RE : si vous pouvez retrouver la définition source (via les métadonnées de type du runtime, voir ci-dessous), le mapping avec la mémoire est direct.

### Métadonnées de type : `runtime._type`

Go embarque des descripteurs de type dans le binaire pour le GC, la reflection et les interfaces. Chaque type a un `runtime._type` :

```
runtime._type (simplifié)  
Offset   Taille   Champ       Description  
──────   ──────   ─────       ───────────
+0x00    8        size        Taille du type en octets
+0x08    8        ptrdata     Taille de la zone contenant des pointeurs (pour le GC)
+0x10    4        hash        Hash du type (utilisé par les interfaces)
+0x14    1        tflag       Flags de type
+0x15    1        align       Alignement
+0x16    1        fieldAlign  Alignement des champs dans une struct
+0x17    1        kind        Genre du type (bool, int, slice, map, struct, etc.)
+0x18    8        equal       Pointeur vers la fonction d'égalité
+0x20    8        gcdata      Bitmap des pointeurs pour le GC
+0x28    4        str         Offset vers le nom du type (dans une table de strings)
+0x2C    4        ptrToThis   Offset vers le type *T
```

Le champ `kind` à l'offset `+0x17` est une constante parmi :

| Valeur | Type Go |  
|---|---|  
| 1 | `bool` |  
| 2 | `int` |  
| 3 | `int8` |  
| ... | ... |  
| 17 | `array` |  
| 18 | `chan` |  
| 19 | `func` |  
| 20 | `interface` |  
| 21 | `map` |  
| 22 | `ptr` |  
| 23 | `slice` |  
| 24 | `string` |  
| 25 | `struct` |

Pour les types composites (struct, map, slice, etc.), des descripteurs étendus suivent le `_type` de base. Par exemple, un `structType` ajoute la liste des champs avec leurs noms, types et offsets.

> 💡 **Astuce RE** : les descripteurs `runtime._type` survivent au stripping. Ils sont indispensables au runtime (GC, reflection, interfaces) et ne peuvent pas être supprimés sans casser le programme. En les parsant, vous pouvez reconstruire les définitions de types du programme — noms, champs, tailles. C'est une source d'information que le C ne vous donnera jamais.

---

## Récapitulatif visuel

```
              Taille
Type          header    Champs du header               Pattern en assembleur
────────────  ────────  ─────────────────────────────  ────────────────────────────────
slice         24 oct.   ptr, len, cap                  3 registres ou 3 slots pile  
string        16 oct.   ptr, len                       2 registres ou 2 slots pile  
interface     16 oct.   itab/type, data                2 registres ; CALL [itab+0x18]  
map           8 oct.    *hmap (pointeur seul)          1 registre ; CALL runtime.mapaccess*  
channel       8 oct.    *hchan (pointeur seul)         1 registre ; CALL runtime.chansend1/chanrecv1  
struct        variable  champs alignés, ordre source   accès par offsets fixes  
```

---

## Ce qu'il faut retenir

1. **Comptez les registres.** Un argument slice consomme 3 registres, un string ou une interface en consomme 2, un pointeur (map, channel) en consomme 1. Ce comptage est votre outil principal pour reconstruire les signatures.  
2. **Reconnaissez les fonctions runtime.** `runtime.makeslice`, `runtime.growslice`, `runtime.makemap`, `runtime.mapaccess*`, `runtime.makechan`, `runtime.chansend1`, `runtime.chanrecv1` — chaque appel vous dit quel type de structure est manipulé.  
3. **Les bounds checks sont vos amis.** Le pattern `CMP; JAE runtime.panicIndex` vous révèle les accès à des slices et tableaux, et vous donne la longueur et l'index dans les registres.  
4. **Les suffixes `_fast*` révèlent les types.** `mapaccess1_fast64` → clé `int64`, `mapassign_faststr` → clé `string`.  
5. **Les descripteurs `runtime._type` sont une mine d'or.** Ils survivent au stripping et permettent de reconstruire les types du programme. Nous les exploiterons en détail dans les sections 34.4 et 34.6.  
6. **Les strings ne sont pas null-terminées.** Ne faites pas confiance à la commande `strings` — les techniques spécifiques de la section 34.5 sont indispensables.

⏭️ [Récupérer les noms de fonctions : `gopclntab` et `go_parser` pour Ghidra/IDA](/34-re-go/04-gopclntab-go-parser.md)
