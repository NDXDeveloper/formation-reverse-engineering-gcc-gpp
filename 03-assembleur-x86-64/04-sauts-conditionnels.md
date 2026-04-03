🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.4 — Sauts conditionnels et inconditionnels : `jmp`, `jz`/`jnz`, `jl`, `jge`, `jle`, `ja`…

> 🎯 **Objectif de cette section** : maîtriser les instructions de saut qui traduisent *tout* le flux de contrôle du C (`if`, `else`, `while`, `for`, `switch`, `break`, `continue`…) en assembleur. Savoir les reconnaître et reconstruire mentalement les structures de contrôle d'origine.

---

## Le mécanisme fondamental

En C, le flux de contrôle est exprimé par des mots-clés structurés : `if`, `else`, `while`, `for`, `switch`. En assembleur, **il n'existe qu'un seul mécanisme** : le saut (*jump*). Toutes les structures de contrôle du C se réduisent à des combinaisons de sauts conditionnels et inconditionnels.

Le principe, déjà effleuré en sections 3.1 et 3.3, se résume en deux temps :

1. Une instruction positionne les flags (`cmp`, `test`, ou toute opération arithmétique/logique).  
2. Une instruction de saut **lit les flags** et modifie `rip` (saute) si la condition est remplie.

Si la condition n'est pas remplie, l'exécution continue simplement à l'instruction suivante — `rip` avance normalement.

---

## `jmp` — saut inconditionnel

```
jmp  cible
```

`jmp` transfère l'exécution à l'adresse cible **sans aucune condition**. Il ne consulte pas les flags. C'est l'équivalent direct d'un `goto` en C.

**Forme directe** (la plus courante) :

```asm
jmp     .loop_start         ; saute à un label dans la même fonction  
jmp     0x40115a            ; saute à une adresse absolue  
```

**Forme indirecte** (via registre ou mémoire) :

```asm
jmp     rax                 ; saute à l'adresse contenue dans rax  
jmp     qword [rax+rcx*8]  ; saute via une table (switch/jump table)  
```

En RE, vous verrez `jmp` dans quatre contextes principaux :

- **Boucles** : le saut arrière qui ramène au début de la boucle.  
- **`else`** : le saut qui contourne le bloc `then` pour atteindre le bloc `else` (ou la fin du `if`).  
- **`break` / `continue`** : dans les boucles et les `switch`.  
- **Jump tables** : l'implémentation optimisée de `switch` par GCC (`jmp qword [table + index*8]`).

### Encodage : short jump vs near jump

Le désassembleur affiche toujours l'adresse cible résolue, mais dans l'encodage binaire, le saut est relatif à `rip` :

- **Short jump** (`jmp rel8`) : offset sur 1 octet signé → portée de -128 à +127 octets. Encodage total : 2 octets.  
- **Near jump** (`jmp rel32`) : offset sur 4 octets signés → portée de ±2 Go. Encodage total : 5 octets.

Cette distinction a un impact direct en **binary patching** (chapitre 21) : remplacer un saut par un autre nécessite de respecter la taille de l'encodage original, sinon on décale toutes les instructions suivantes.

---

## Les sauts conditionnels — table de référence complète

Les sauts conditionnels testent un ou plusieurs flags de `RFLAGS`. Chaque mnémonique correspond à une condition précise. La plupart ont des **synonymes** — deux noms différents pour le même opcode — qui reflètent simplement un contexte de lecture différent (« zéro » vs « égal », « inférieur » vs « pas supérieur ou égal »).

### Sauts pour comparaisons signées

Ces sauts sont utilisés après un `cmp` entre valeurs signées (`int`, `long`, `char` signé) :

| Mnémonique | Synonyme | Condition | Flags testés | Équivalent C |  
|---|---|---|---|---|  
| `je` | `jz` | Égal / Zéro | ZF = 1 | `a == b` |  
| `jne` | `jnz` | Non égal / Non zéro | ZF = 0 | `a != b` |  
| `jl` | `jnge` | Inférieur (*Less*) | SF ≠ OF | `a < b` |  
| `jle` | `jng` | Inférieur ou égal (*Less or Equal*) | ZF = 1 ou SF ≠ OF | `a <= b` |  
| `jg` | `jnle` | Supérieur (*Greater*) | ZF = 0 et SF = OF | `a > b` |  
| `jge` | `jnl` | Supérieur ou égal (*Greater or Equal*) | SF = OF | `a >= b` |

### Sauts pour comparaisons non signées

Ces sauts sont utilisés après un `cmp` entre valeurs non signées (`unsigned int`, pointeurs, `size_t`) :

| Mnémonique | Synonyme | Condition | Flags testés | Équivalent C |  
|---|---|---|---|---|  
| `je` | `jz` | Égal / Zéro | ZF = 1 | `a == b` |  
| `jne` | `jnz` | Non égal / Non zéro | ZF = 0 | `a != b` |  
| `jb` | `jnae`, `jc` | Inférieur (*Below*) | CF = 1 | `a < b` |  
| `jbe` | `jna` | Inférieur ou égal (*Below or Equal*) | CF = 1 ou ZF = 1 | `a <= b` |  
| `ja` | `jnbe` | Supérieur (*Above*) | CF = 0 et ZF = 0 | `a > b` |  
| `jae` | `jnb`, `jnc` | Supérieur ou égal (*Above or Equal*) | CF = 0 | `a >= b` |

### Sauts sur flag individuel

| Mnémonique | Condition | Usage typique |  
|---|---|---|  
| `js` | SF = 1 (résultat négatif) | Test de signe après arithmétique |  
| `jns` | SF = 0 (résultat positif ou nul) | Test de signe |  
| `jo` | OF = 1 (overflow signé) | Vérification de débordement |  
| `jno` | OF = 0 (pas d'overflow) | — |  
| `jp` / `jpe` | PF = 1 (parité paire) | Rare en code applicatif |  
| `jnp` / `jpo` | PF = 0 (parité impaire) | Rare en code applicatif |

### Le moyen mnémotechnique

La terminologie x86 utilise deux familles de mots pour distinguer signé et non signé :

- **Signé** → vocabulaire de grandeur : *Less* (inférieur), *Greater* (supérieur) → `jl`, `jg`, `jle`, `jge`  
- **Non signé** → vocabulaire de position : *Below* (en dessous), *Above* (au-dessus) → `jb`, `ja`, `jbe`, `jae`  
- **Égalité** → partagée : *Equal* (égal), *Zero* (zéro) → `je`/`jz`, `jne`/`jnz`

> 💡 **Pour le RE** : la distinction `jl`/`jg` (signé) vs `jb`/`ja` (non signé) vous donne directement la signedness des variables comparées. Si GCC génère un `jb` après un `cmp`, les deux opérandes sont traités comme non signés. C'est un indice de type aussi fiable que la distinction `shr` vs `sar` vue en section 3.3.

---

## Condition inversée : le réflexe GCC

Un point qui déroute souvent les débutants en RE : **GCC inverse presque toujours la condition par rapport au code C source**. La raison est purement logique — le compilateur génère le code de manière séquentielle et utilise le saut pour *contourner* le bloc qui ne doit pas être exécuté.

```c
// Code C
if (x == 42) {
    do_something();
}
// suite...
```

Le réflexe serait d'attendre un `je` (Jump if Equal) vers `do_something`. Mais GCC fait l'inverse :

```asm
cmp     eax, 0x2a          ; compare x avec 42  
jne     .after_if           ; si x != 42, SAUTE AU-DELÀ du bloc if  
call    do_something        ; ce code n'est atteint que si x == 42  
.after_if:
; suite...
```

Le compilateur place le code du bloc `then` immédiatement après le `cmp`, et utilise un saut avec la condition **inversée** pour le contourner si la condition est fausse. Résultat :

| Condition C | Saut GCC (pour contourner le bloc) |  
|---|---|  
| `if (a == b)` | `jne` |  
| `if (a != b)` | `je` |  
| `if (a < b)` | `jge` |  
| `if (a >= b)` | `jl` |  
| `if (a > b)` | `jle` |  
| `if (a <= b)` | `jg` |

C'est systématique en `-O0`. Avec les optimisations activées, GCC peut réorganiser le code différemment (placer le cas le plus probable en fallthrough), mais le principe d'inversion reste très courant.

> ⚠️ **Règle de lecture** : quand vous rencontrez un saut conditionnel en RE, demandez-vous toujours **« ce saut contourne quel bloc ? »** plutôt que **« ce saut va vers quel bloc ? »**. C'est en identifiant ce qui est *contourné* que vous reconstruisez la condition originale.

---

## Reconnaître les structures de contrôle

### `if` simple

```c
if (a > 0) {
    result = 1;
}
```

```asm
cmp     eax, 0  
jle     .end_if           ; si a <= 0, contourne le bloc  
mov     dword [rbp-0x4], 1 ; result = 1  
.end_if:
```

Le pattern est : `cmp` → saut conditionnel (condition inversée) vers un label après le bloc → corps du `if` → label.

### `if` / `else`

```c
if (a > 0) {
    result = 1;
} else {
    result = -1;
}
```

```asm
cmp     eax, 0  
jle     .else_block        ; si a <= 0, va au else  
mov     dword [rbp-0x4], 1 ; result = 1 (bloc then)  
jmp     .end_if            ; saute par-dessus le else  
.else_block:
mov     dword [rbp-0x4], 0xffffffff  ; result = -1 (bloc else)
.end_if:
```

Le `jmp` inconditionnel entre le bloc `then` et le bloc `else` est la signature caractéristique du `if`/`else`. Sans lui, l'exécution « tomberait » dans le `else` après avoir exécuté le `then`.

Le pattern complet est : `cmp` → saut conditionnel vers `.else` → bloc then → `jmp` vers `.end` → bloc else → `.end`.

### `if` / `else if` / `else` (chaîne de conditions)

```c
if (x == 1) {
    a();
} else if (x == 2) {
    b();
} else {
    c();
}
```

```asm
cmp     eax, 1  
jne     .elif_2  
call    a  
jmp     .end  
.elif_2:
cmp     eax, 2  
jne     .else_block  
call    b  
jmp     .end  
.else_block:
call    c
.end:
```

Chaque condition est un couple `cmp` + saut vers la condition suivante, avec un `jmp` inconditionnel à la fin de chaque bloc pour rejoindre la sortie. C'est une cascade linéaire reconnaissable dans le graphe de flux de contrôle.

### Boucle `while`

```c
while (i < 10) {
    process(i);
    i++;
}
```

GCC en `-O0` génère typiquement une structure avec le test au début :

```asm
.loop_test:
    cmp     dword [rbp-0x4], 9     ; compare i avec 9
    jg      .loop_end               ; si i > 9, sort de la boucle
    ; corps de la boucle
    mov     edi, dword [rbp-0x4]
    call    process
    add     dword [rbp-0x4], 1      ; i++
    jmp     .loop_test              ; retourne au test
.loop_end:
```

En mode optimisé (`-O1` et plus), GCC préfère souvent placer le test **à la fin** de la boucle, avec un saut initial vers le test :

```asm
    jmp     .loop_test              ; saute directement au test
.loop_body:
    ; corps de la boucle
    mov     edi, eax
    call    process
    add     ebx, 1                  ; i++
.loop_test:
    cmp     ebx, 9
    jle     .loop_body              ; si i <= 9, retourne au corps
```

Cette seconde forme est plus performante car le saut arrière (`jle .loop_body`) est le saut « normal » de la boucle — et les processeurs modernes prédisent les sauts arrière comme *pris* par défaut, ce qui correspond au cas le plus fréquent (les boucles itèrent généralement plusieurs fois).

> 💡 **Pour le RE** : la signature d'une boucle en assembleur est un **saut arrière** (vers une adresse inférieure à l'adresse courante). Quand vous repérez un `jXX` dont la cible est *avant* l'instruction courante, vous êtes à la fin d'une boucle. Le `cmp` ou `test` qui précède ce saut est la condition de continuation.

### Boucle `for`

```c
for (int i = 0; i < n; i++) {
    arr[i] = 0;
}
```

```asm
    ; initialisation : i = 0
    mov     dword [rbp-0x4], 0
    jmp     .for_test
.for_body:
    ; corps : arr[i] = 0
    mov     eax, dword [rbp-0x4]       ; eax = i
    cdqe                                 ; extension signée → rax
    mov     dword [rbp-0x30+rax*4], 0   ; arr[i] = 0
    ; incrémentation : i++
    add     dword [rbp-0x4], 1
.for_test:
    ; test : i < n
    mov     eax, dword [rbp-0x4]
    cmp     eax, dword [rbp-0x8]        ; compare i avec n
    jl      .for_body                    ; si i < n, retourne au corps
```

La boucle `for` se distingue de la boucle `while` par la présence d'un bloc d'**incrémentation** clairement séparé juste avant le test. En pratique, les deux produisent des structures très similaires en assembleur.

### Boucle `do…while`

```c
do {
    process(i);
    i++;
} while (i < 10);
```

```asm
.loop_body:
    mov     edi, dword [rbp-0x4]
    call    process
    add     dword [rbp-0x4], 1
    cmp     dword [rbp-0x4], 9
    jle     .loop_body              ; retourne au début si i <= 9
```

C'est la forme la plus compacte : pas de saut initial, le test est à la fin, un seul saut conditionnel. C'est aussi la raison pour laquelle GCC optimisé transforme souvent les `while` et `for` en `do…while` précédés d'un test de garde — cela élimine un `jmp` inconditionnel par boucle.

### Résumé visuel des patterns de boucle

```
while (test en haut, -O0)      while (test en bas, -O1+)      do...while
──────────────────────────     ──────────────────────────     ──────────────────

  ┌──────────────┐               jmp ─────┐                    ┌──────────────┐
  │              ▼                        ▼                    │              ▼
  │          ┌──────┐              ┌──────────────┐            │         ┌─────────┐
  │          │ TEST │              │    CORPS     │            │         │  CORPS  │
  │          └──┬───┘              └──────┬───────┘            │         └────┬────┘
  │        vrai │  faux                   │                    │              │
  │        ┌────┘└────┐            ┌──────▼───────┐            │         ┌────▼────┐
  │        ▼          ▼            │    TEST      │            │         │  TEST   │
  │   ┌─────────┐  sortie          └──┬───────┬───┘            │         └──┬───┬──┘
  │   │  CORPS  │                 vrai│       │faux            │       vrai │   │ faux
  │   └────┬────┘                     │       ▼                └────────────┘   ▼
  └────────┘                     ┌────┘    sortie                            sortie
                                 │
                                 └──► (retour au CORPS)
```

---

## Le `switch` et les jump tables

Les instructions `switch` produisent deux patterns distincts selon le nombre de `case` et la densité des valeurs.

### Peu de `case` : cascade de `cmp`/`je`

Pour un `switch` avec peu de cas (typiquement moins de 5-6), GCC génère une chaîne de comparaisons, identique à une série de `if`/`else if` :

```c
switch (cmd) {
    case 1: handle_start(); break;
    case 2: handle_stop();  break;
    case 3: handle_reset(); break;
    default: handle_error();
}
```

```asm
cmp     eax, 1  
je      .case_1  
cmp     eax, 2  
je      .case_2  
cmp     eax, 3  
je      .case_3  
jmp     .default            ; aucun case → default  

.case_1:
    call    handle_start
    jmp     .end_switch
.case_2:
    call    handle_stop
    jmp     .end_switch
.case_3:
    call    handle_reset
    jmp     .end_switch
.default:
    call    handle_error
.end_switch:
```

Chaque `break` se traduit par un `jmp .end_switch`. L'absence de `break` (fallthrough) se traduirait par l'absence de ce `jmp` — le code « tomberait » dans le case suivant.

### Beaucoup de `case` denses : la jump table

Quand les valeurs sont suffisamment nombreuses et proches les unes des autres, GCC génère une **table de sauts** (*jump table*) — un tableau d'adresses indexé par la valeur du `switch`. C'est nettement plus performant qu'une cascade de comparaisons, car l'accès est en O(1).

```c
switch (opcode) {
    case 0: op_nop();    break;
    case 1: op_load();   break;
    case 2: op_store();  break;
    case 3: op_add();    break;
    case 4: op_sub();    break;
    case 5: op_mul();    break;
    case 6: op_div();    break;
    case 7: op_halt();   break;
    default: op_invalid();
}
```

```asm
    ; vérification de la borne (opcode <= 7)
    cmp     edi, 7
    ja      .default                  ; si opcode > 7 (non signé), → default

    ; saut via la table
    lea     rdx, [rip+.jump_table]    ; rdx = adresse de la table
    movsxd  rax, dword [rdx+rdi*4]   ; lit l'offset 32 bits relatif
    add     rax, rdx                  ; calcule l'adresse absolue
    jmp     rax                       ; saute au case correspondant

; ... code des case_0 à case_7 ...

.default:
    call    op_invalid

; Table en .rodata (données, pas du code)
.jump_table:
    .long   .case_0 - .jump_table
    .long   .case_1 - .jump_table
    .long   .case_2 - .jump_table
    ; ... etc.
```

Les caractéristiques d'une jump table dans le désassemblage :

1. Un **`cmp` + `ja`** initial qui vérifie que la valeur est dans les bornes (le `ja` est non signé, donc une valeur négative est aussi rejetée car interprétée comme un très grand nombre non signé).  
2. Un **`jmp` indirect** via un calcul d'adresse impliquant un registre index et un facteur d'échelle (`*4` ou `*8`).  
3. Un **bloc de données** dans `.rodata` contenant les offsets ou adresses de chaque case.

> 💡 **Pour le RE** : Ghidra et IDA reconnaissent généralement les jump tables automatiquement et affichent les cas du `switch` de manière structurée. Mais dans `objdump` ou en analyse manuelle, il faut repérer le `jmp` indirect et aller lire la table dans `.rodata` pour identifier les cibles. La section 3.8 revient sur l'adressage RIP-relatif utilisé dans ce contexte.

### Valeurs éparses : recherche binaire ou table indexée

Quand les valeurs du `switch` sont nombreuses mais éparses (par exemple `case 10`, `case 200`, `case 5000`), GCC peut générer une **recherche par arbre binaire** — une cascade de `cmp` organisée pour bissecter l'espace des valeurs, avec une profondeur logarithmique. On reconnaît ce pattern à la structure « en diamant » du graphe de flux de contrôle, où chaque nœud compare avec une valeur médiane et branche à gauche ou à droite.

---

## Sauts conditionnels sans `cmp` : exploiter les flags d'une opération précédente

GCC n'insère pas toujours un `cmp` ou un `test` avant un saut conditionnel. Si l'instruction **précédente** a déjà positionné les flags de manière exploitable, le compilateur enchaîne directement le saut :

```asm
sub     eax, 1              ; eax-- (met à jour ZF si le résultat est 0)  
jz      .reached_zero        ; si le résultat de sub est 0, saute  

add     ecx, edx             ; ecx += edx (met à jour SF)  
js      .negative            ; si le résultat est négatif, saute  

and     eax, 0x3             ; eax &= 3 (met à jour ZF)  
jnz     .not_aligned         ; si eax & 3 != 0, pas aligné sur 4  
```

Ce pattern est plus courant en code optimisé (`-O1` et plus) car le compilateur fusionne le test avec l'opération. En `-O0`, GCC est plus conservateur et insère souvent un `cmp` ou `test` explicite même après un `sub` ou `add`.

> 💡 **Pour le RE** : si vous voyez un saut conditionnel sans `cmp`/`test` immédiatement avant, regardez l'instruction précédente — c'est elle qui a positionné les flags. L'enchaînement `sub eax, 1` / `jz` est une forme compacte de `eax--; if (eax == 0)`.

---

## L'instruction `set`XX — stocker une condition dans un octet

Les instructions `setXX` sont les cousines des `jXX` : au lieu de sauter, elles **stockent** le résultat de la condition (0 ou 1) dans un registre 8 bits :

```asm
cmp     eax, ebx  
setl    al              ; al = 1 si eax < ebx (signé), 0 sinon  
movzx   eax, al         ; étend à 32 bits (eax = 0 ou 1)  
```

Chaque `jXX` a son `setXX` correspondant : `setz`, `setnz`, `setl`, `setg`, `setb`, `seta`, etc.

GCC utilise `setXX` pour traduire les expressions booléennes qui sont **stockées** dans une variable plutôt qu'utilisées dans un `if` :

```c
int is_positive = (x > 0);    // →  test edi, edi / setg al / movzx eax, al  
int are_equal = (a == b);     // →  cmp edi, esi / sete al / movzx eax, al  
return x < y;                 // →  cmp edi, esi / setl al / movzx eax, al  
```

> 💡 **Pour le RE** : quand vous voyez un `setXX` suivi d'un `movzx`, c'est le stockage d'une condition booléenne dans un `int`. Traduisez-le directement en `variable = (condition)`.

---

## Reconnaître les opérateurs logiques `&&` et `||`

Les opérateurs logiques C avec court-circuit produisent des patterns caractéristiques en assembleur.

### `&&` (ET logique avec court-circuit)

```c
if (a > 0 && b > 0) {
    action();
}
```

```asm
cmp     eax, 0  
jle     .skip           ; si a <= 0, court-circuit → saute (pas besoin de tester b)  
cmp     ecx, 0  
jle     .skip           ; si b <= 0, saute  
call    action  
.skip:
```

Chaque condition est un `cmp` + saut vers la même cible (la sortie). Dès qu'une condition est fausse, on court-circuite. Les deux sauts vont au **même label** — c'est la signature du `&&`.

### `||` (OU logique avec court-circuit)

```c
if (a > 0 || b > 0) {
    action();
}
```

```asm
cmp     eax, 0  
jg      .do_action      ; si a > 0, court-circuit → condition vraie  
cmp     ecx, 0  
jle     .skip           ; si b <= 0 (et a <= 0), aucune condition vraie  
.do_action:
call    action
.skip:
```

Ici, la première condition vraie suffit — le premier saut va directement au **corps** du `if`. La seconde condition est le dernier recours, avec la logique inversée habituelle.

> 💡 **Pour le RE** : deux sauts conditionnels consécutifs vers **le même label** → `&&`. Un saut conditionnel vers le **corps** suivi d'un saut conditionnel vers la **sortie** → `||`. Ce pattern est fiable et fréquent.

---

## L'opérateur ternaire et les `cmovXX`

Le cas simple de l'opérateur ternaire `? :` a déjà été vu en section 3.2 avec `cmovXX`. Mais quand l'expression est plus complexe (appels de fonctions dans les branches, effets de bord), GCC revient aux sauts classiques :

```c
// Cas simple → cmov (pas de saut)
int min = (a < b) ? a : b;

// Cas complexe → sauts classiques
char *msg = valid ? compute_msg() : get_default();
```

```asm
; Cas complexe : les fonctions doivent être appelées conditionnellement
test    edi, edi  
jz      .use_default  
call    compute_msg  
jmp     .done  
.use_default:
call    get_default
.done:
; rax contient le résultat
```

---

## Flux de contrôle et graphes dans les désassembleurs

Les outils de RE modernes affichent le flux de contrôle sous forme de **graphe** où chaque bloc d'instructions linéaires (*basic block*) est un nœud, et chaque saut est une arête. Cette représentation rend les structures de contrôle immédiatement visuelles :

- Un **`if`/`else`** forme un losange (diamant) : un nœud qui bifurque en deux chemins qui reconvergent.  
- Une **boucle** forme un cycle : une arête qui remonte vers un nœud précédent.  
- Un **`switch`** avec jump table forme une étoile : un nœud central avec de multiples arêtes sortantes vers les case.  
- Un **`break`** est une arête qui sort du cycle de la boucle.  
- Un **`continue`** est une arête qui remonte directement au test de la boucle.

```
    if / else                   boucle while                switch (cascade)
  ─────────────               ──────────────              ──────────────────

      ┌─────┐                    ┌─────┐                  ┌──────┐
      │ TEST│                    │ TEST│◄──┐              │ cmp 1│
      └──┬──┘                    └──┬──┘   │              └──┬───┘
     vrai│ faux               vrai│ faux   │              == │  !=
     ┌───┘└───┐               ┌───┘└───┐   │            ┌───┘└───┐
     ▼        ▼               ▼        ▼   │            ▼   ┌──────┐
  ┌──────┐┌──────┐        ┌──────┐  sortie │         ┌────┐ │ cmp 2│
  │ THEN ││ ELSE │        │ BODY │         │         │ c1 │ └──┬───┘
  └──┬───┘└──┬───┘        └──┬───┘         │         └──┬─┘ == │  !=
     └───┬───┘               └─────────────┘            │   ┌──┘└───┐
         ▼                                              │   ▼    ┌──────┐
       suite                                            │ ┌────┐ │ cmp 3│
                                                        │ │ c2 │ └──┬───┘
                                                        │ └──┬─┘ == │  !=
                                                        │    │   ┌──┘└───┐
                                                        │    │   ▼       ▼
                                                        │    │ ┌────┐ ┌─────┐
                                                        │    │ │ c3 │ │ def │
                                                        │    │ └──┬─┘ └──┬──┘
                                                        └────┴────┴──────┘
                                                                 ▼
                                                               suite
```

Dans Ghidra, cette vue est le **Function Graph** (`Window → Function Graph`). Dans IDA, c'est la vue **Graph** (`Espace`). Dans Cutter/Radare2, c'est la commande `VV`. Ces vues graphiques sont souvent plus lisibles qu'un listing linéaire pour reconstituer le flux de contrôle — elles rendent les patterns décrits dans cette section immédiatement identifiables d'un coup d'œil.

---

## Ce qu'il faut retenir pour la suite

1. **`jmp`** est le seul saut inconditionnel — il traduit les `goto`, les `break`, les `else`, et les retours en début de boucle.  
2. **Les sauts conditionnels se lisent en inversant la condition** : GCC utilise `jne` pour implémenter un `if (a == b)`, parce que le saut *contourne* le bloc `then`.  
3. **Signé vs non signé** est trahi par le choix du mnémonique : `jl`/`jg` = signé, `jb`/`ja` = non signé.  
4. **Un saut arrière** (vers une adresse inférieure) signale presque toujours une **boucle**.  
5. **Deux sauts vers le même label** = `&&`, **un saut vers le corps + un saut vers la sortie** = `||`.  
6. **`setXX`** stocke une condition booléenne dans un registre au lieu de sauter — c'est la traduction de `variable = (condition)`.  
7. **Les jump tables** (`jmp` indirect + table en `.rodata`) sont l'implémentation optimisée des `switch` à nombreux cas denses.  
8. Les **vues graphiques** des désassembleurs (Ghidra Function Graph, IDA Graph view) rendent ces patterns visuellement évidents.

---

> ⏭️ **Prochaine section** : [3.5 — La pile : prologue, épilogue et conventions d'appel System V AMD64](/03-assembleur-x86-64/05-pile-prologue-epilogue.md)

⏭️ [La pile : prologue, épilogue et conventions d'appel System V AMD64](/03-assembleur-x86-64/05-pile-prologue-epilogue.md)
