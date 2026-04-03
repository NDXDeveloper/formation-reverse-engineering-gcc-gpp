🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.2 — Instructions essentielles : `mov`, `push`/`pop`, `call`/`ret`, `lea`

> 🎯 **Objectif de cette section** : maîtriser les instructions que l'on rencontre dans pratiquement *chaque fonction* d'un binaire compilé par GCC. Ces six instructions (et leurs variantes) constituent le squelette de tout programme désassemblé — les comprendre, c'est déjà être capable de lire la majorité du code.

---

## Vue d'ensemble

Si l'on analysait statistiquement le code produit par GCC, on constaterait qu'une poignée d'instructions revient de manière écrasante. Avant de parler d'arithmétique (section 3.3) ou de sauts conditionnels (section 3.4), il faut d'abord comprendre les instructions qui **déplacent des données**, **gèrent la pile** et **organisent les appels de fonctions** — car elles forment le tissu conjonctif entre toutes les autres.

| Instruction | Rôle résumé |  
|---|---|  
| `mov` | Copie une valeur d'une source vers une destination |  
| `push` | Empile une valeur (décrémente `rsp`, puis écrit) |  
| `pop` | Dépile une valeur (lit, puis incrémente `rsp`) |  
| `call` | Appelle une fonction (empile `rip`, puis saute) |  
| `ret` | Retourne de fonction (dépile `rip`) |  
| `lea` | Calcule une adresse *sans* accéder à la mémoire |

---

## `mov` — le couteau suisse du déplacement de données

`mov` est, de très loin, l'instruction la plus fréquente dans tout binaire x86-64. Son rôle est simple : **copier** une valeur de la source vers la destination. Malgré son nom (*move*), c'est bien une copie — la source n'est pas effacée.

### Syntaxe Intel (utilisée dans ce tutoriel)

```
mov  destination, source
```

La destination est toujours à gauche, la source à droite. C'est l'inverse de la syntaxe AT&T (cf. encadré plus bas).

### Les formes courantes de `mov`

En x86-64, `mov` peut opérer entre registres, entre un registre et la mémoire, ou avec une valeur immédiate. Voici les formes que vous croiserez constamment :

**Registre ← Registre**

```asm
mov     rbp, rsp          ; copie rsp dans rbp (début de prologue)  
mov     rdi, rax           ; prépare le 1er argument d'un call  
```

C'est la forme la plus rapide — tout reste dans le processeur, aucun accès mémoire.

**Registre ← Valeur immédiate**

```asm
mov     eax, 0             ; met eax (et donc rax) à zéro  
mov     edi, 0x1           ; charge la constante 1 dans edi (1er argument = 1)  
mov     rax, 0x400580      ; charge une adresse 64 bits dans rax  
```

> 💡 **Pour le RE** : quand vous voyez `mov edi, <constante>` juste avant un `call`, c'est le premier argument entier de la fonction appelée. La constante vous donne directement la valeur du paramètre.

**Registre ← Mémoire** (lecture)

```asm
mov     eax, dword [rbp-0x4]      ; lit un int (32 bits) depuis la pile  
mov     rax, qword [rip+0x2a3f]   ; lit un pointeur (64 bits) depuis .data/.bss  
mov     al, byte [rdi]             ; lit un octet (char) pointé par rdi  
```

Les crochets `[ ]` indiquent un **accès mémoire** (déréférencement). L'expression entre crochets est l'adresse effective. Les préfixes `dword`, `qword`, `byte` précisent la taille de l'accès (respectivement 4, 8, 1 octets) — ils sont parfois omis par le désassembleur quand la taille est évidente d'après le registre utilisé.

**Mémoire ← Registre** (écriture)

```asm
mov     dword [rbp-0x4], eax      ; écrit eax dans une variable locale (int)  
mov     qword [rbp-0x10], rdi     ; sauvegarde un pointeur sur la pile  
mov     byte [rax+rcx], dl        ; écrit un octet dans un tableau  
```

**Mémoire ← Valeur immédiate**

```asm
mov     dword [rbp-0x4], 0x0      ; initialise une variable locale à 0  
mov     qword [rbp-0x8], 0x0      ; initialise un pointeur à NULL  
```

### Ce que `mov` ne peut PAS faire

Une règle fondamentale du x86-64 : **`mov` ne peut pas transférer directement d'une adresse mémoire à une autre**. Pour copier une valeur d'un emplacement mémoire à un autre, il faut passer par un registre intermédiaire :

```asm
; Copier la variable à [rbp-0x8] vers [rbp-0x4]
mov     eax, dword [rbp-0x8]     ; mémoire → registre  
mov     dword [rbp-0x4], eax     ; registre → mémoire  
```

C'est un pattern très fréquent dans le code `-O0`, où chaque variable C vit sur la pile et le compilateur fait des allers-retours constants entre la pile et les registres.

### Correspondance avec le C

```c
int x = 42;          // →  mov  dword [rbp-0x4], 0x2a  
int y = x;           // →  mov  eax, dword [rbp-0x4]  
                     //    mov  dword [rbp-0x8], eax
char c = buf[i];     // →  mov  al, byte [rax+rcx]
*ptr = value;        // →  mov  dword [rax], edx
```

### Les variantes de `mov` à connaître

Au-delà du `mov` de base, plusieurs variantes apparaissent régulièrement dans le code GCC :

**`movzx`** — *Move with Zero Extension*

```asm
movzx   eax, byte [rdi]     ; lit un octet, étend à 32 bits avec des zéros  
movzx   ecx, word [rbp-0x2] ; lit 16 bits, étend à 32 bits avec des zéros  
```

`movzx` est la traduction typique d'un accès à un `unsigned char` ou un `unsigned short` qui est ensuite utilisé dans une expression `int`. L'extension par zéros garantit que les bits supérieurs sont propres.

**`movsx` / `movsxd`** — *Move with Sign Extension*

```asm
movsx   eax, byte [rdi]     ; lit un octet signé, étend à 32 bits en préservant le signe  
movsxd  rax, dword [rbp-0x4] ; lit un int (32 bits), étend à 64 bits signé  
```

`movsx` fait la même chose que `movzx`, mais en propageant le bit de signe. C'est la traduction d'un `char` (signé) ou d'un `short` (signé) promu en `int`. `movsxd` est spécifique au mode 64 bits et étend un `int` 32 bits en un `long` 64 bits signé — on le voit souvent quand un `int` est utilisé comme index dans un tableau de pointeurs.

**`cmovXX`** — *Conditional Move*

```asm
cmp     eax, ebx  
cmovl   eax, ecx       ; si eax < ebx (signé), alors eax = ecx  
```

Les `cmov` sont des `mov` conditionnels : le transfert ne s'effectue que si la condition (basée sur les flags, comme pour les sauts) est vraie. GCC les utilise pour éviter les branchements sur des `if` simples avec affectation — c'est plus performant car cela évite les erreurs de prédiction de branche. On les reconnaît au suffixe de condition : `cmovz`, `cmovnz`, `cmovl`, `cmovge`, `cmova`, etc.

```c
// Code C
int min = (a < b) ? a : b;
```

```asm
; GCC avec -O2 peut générer :
mov     eax, edi          ; eax = a  
cmp     edi, esi          ; compare a et b  
cmovg   eax, esi          ; si a > b, eax = b  
; résultat : eax = min(a, b)
```

> 💡 **Pour le RE** : quand vous voyez un `cmovXX` là où vous attendiez un `jXX` + `mov`, c'est que le compilateur a optimisé un opérateur ternaire `? :` ou un `if` simple avec affectation. La logique est identique, mais le flux de contrôle est linéaire — pas de saut.

---

## `push` et `pop` — la gestion de la pile

La pile (*stack*) est une zone de mémoire gérée par le processeur via le registre `rsp`. Elle fonctionne selon le principe LIFO (*Last In, First Out*) et croît vers les adresses basses sur x86-64.

### `push` — empiler une valeur

```
push  source
```

`push` effectue deux opérations atomiques :

1. **Décrémente `rsp`** de 8 octets (en mode 64 bits).  
2. **Écrit** la valeur source à l'adresse pointée par le nouveau `rsp`.

```asm
push    rbp          ; rsp -= 8, puis [rsp] = rbp  
push    rbx          ; rsp -= 8, puis [rsp] = rbx  
push    0x42         ; rsp -= 8, puis [rsp] = 0x42  
```

En termes d'effet, `push rbp` est équivalent à :

```asm
sub     rsp, 8  
mov     qword [rsp], rbp  
```

Mais `push` est plus compact (un seul opcode) et le processeur l'optimise en interne.

### `pop` — dépiler une valeur

```
pop  destination
```

`pop` fait l'inverse :

1. **Lit** la valeur à l'adresse pointée par `rsp`.  
2. **Incrémente `rsp`** de 8 octets.

```asm
pop     rbx          ; rbx = [rsp], puis rsp += 8  
pop     rbp          ; rbp = [rsp], puis rsp += 8  
```

### Rôle de `push`/`pop` dans le code GCC

Vous les verrez systématiquement dans trois contextes :

**1. Prologue de fonction — sauvegarde des registres callee-saved**

```asm
push    rbp              ; sauvegarde l'ancien base pointer  
push    rbx              ; sauvegarde rbx (callee-saved, va être utilisé)  
push    r12              ; sauvegarde r12 (callee-saved, va être utilisé)  
```

Le nombre de `push` en début de fonction vous dit combien de registres callee-saved la fonction utilise — c'est un indicateur de sa complexité.

**2. Épilogue de fonction — restauration (en ordre inverse)**

```asm
pop     r12              ; restaure r12  
pop     rbx              ; restaure rbx  
pop     rbp              ; restaure rbp  
ret  
```

L'ordre des `pop` est **strictement inverse** de l'ordre des `push` — c'est la nature LIFO de la pile. Si l'ordre ne correspond pas, il y a un problème (ou une obfuscation intentionnelle).

**3. Alignement de la pile**

Parfois, GCC insère un `push` supplémentaire uniquement pour aligner `rsp` sur 16 octets (exigence de la convention System V AMD64 avant un `call`). Vous verrez par exemple un `push rax` dont la valeur n'est jamais utilisée — il ne sert qu'à décrémenter `rsp` de 8.

> 💡 **Pour le RE** : compter les `push` en début de fonction et les `pop` en fin est un premier réflexe de validation. Si les nombres ne correspondent pas, cela mérite investigation : stack frame inhabituelle, optimisation agressive, ou code obfusqué.

---

## `call` et `ret` — les appels de fonctions

Les appels de fonctions sont le mécanisme structurant de tout programme compilé. Comprendre `call` et `ret` est indispensable pour suivre le flux d'exécution dans un désassembleur.

### `call` — appel de fonction

```
call  cible
```

`call` effectue deux opérations :

1. **Empile l'adresse de retour** : `rsp -= 8`, puis `[rsp] = rip` (l'adresse de l'instruction *suivant* le `call`).  
2. **Saute** à l'adresse cible : `rip = cible`.

L'adresse de retour empilée est ce qui permet à `ret` de savoir où reprendre l'exécution après la fonction appelée.

**Forme directe** — l'adresse de la cible est encodée dans l'instruction :

```asm
call    0x401150                ; appel direct à une adresse fixe  
call    my_function             ; le désassembleur affiche le nom si les symboles existent  
call    printf@plt              ; appel via la PLT (bibliothèque dynamique)  
```

**Forme indirecte** — l'adresse est lue depuis un registre ou la mémoire :

```asm
call    rax                     ; appel indirect via registre  
call    qword [rbx+0x18]       ; appel indirect via mémoire (pointeur de fonction)  
call    qword [rax]             ; dispatch virtuel C++ via vtable  
```

> 💡 **Pour le RE** : la distinction est cruciale. Un `call` direct vous donne immédiatement la cible — vous pouvez naviguer vers la fonction appelée dans votre désassembleur. Un `call` indirect nécessite une analyse dynamique ou contextuelle pour résoudre la cible. En C++, `call qword [rax+offset]` est le pattern caractéristique de l'appel de méthode virtuelle via la vtable (détaillé au chapitre 17).

### `ret` — retour de fonction

```
ret
```

`ret` effectue l'inverse de la partie « empilage » de `call` :

1. **Dépile l'adresse de retour** : `rip = [rsp]`, puis `rsp += 8`.

L'exécution reprend à l'instruction qui suit le `call` d'origine.

Occasionnellement, vous verrez une variante `ret imm16` (par exemple `ret 0x8`) qui dépile l'adresse de retour puis ajoute la valeur immédiate à `rsp`. C'est rare dans du code System V AMD64 (plutôt utilisé dans les conventions Windows `__stdcall`), mais cela peut apparaître dans du code assembleur inline ou des fonctions avec des conventions non standard.

### Visualiser un appel complet

Voici le cycle complet d'un appel de fonction, vu du côté de la pile :

```
AVANT le call :
    rsp →  ┌──────────────────┐
           │  (données)       │
           └──────────────────┘

APRÈS le call (entrée dans la fonction appelée) :
    rsp →  ┌──────────────────┐
           │ adresse retour   │  ← empilée par call
           ├──────────────────┤
           │  (données)       │
           └──────────────────┘

APRÈS le prologue (push rbp / mov rbp, rsp / sub rsp, N) :
    rsp →  ┌──────────────────┐
           │ variables locales│  ← espace réservé par sub rsp, N
           ├──────────────────┤
    rbp →  │ ancien rbp       │  ← sauvé par push rbp
           ├──────────────────┤
           │ adresse retour   │  ← empilée par call
           ├──────────────────┤
           │  (données appelant)
           └──────────────────┘

APRÈS ret (retour à l'appelant) :
    rsp →  ┌──────────────────┐
           │  (données)       │  ← rsp restauré, on est revenu
           └──────────────────┘
```

---

## `lea` — l'instruction la plus mal comprise

`lea` (*Load Effective Address*) est une instruction qui **calcule une adresse sans accéder à la mémoire**. C'est probablement l'instruction qui déroute le plus les débutants en RE, parce que sa syntaxe ressemble à un accès mémoire — mais elle ne lit ni n'écrit rien en mémoire.

### Syntaxe

```
lea  destination, [expression]
```

L'expression entre crochets est calculée selon les mêmes règles de mode d'adressage que `mov` — mais au lieu de lire la mémoire à l'adresse résultante, `lea` place **l'adresse elle-même** dans le registre destination.

### Comparaison directe avec `mov`

```asm
mov     rax, [rbp-0x10]     ; lit la valeur EN MÉMOIRE à l'adresse rbp-0x10
                              ; → rax = contenu de la mémoire

lea     rax, [rbp-0x10]     ; calcule l'adresse rbp-0x10, la met dans rax
                              ; → rax = rbp - 0x10 (aucun accès mémoire)
```

C'est la même syntaxe avec les crochets, mais le comportement est fondamentalement différent. `mov` déréférence, `lea` calcule.

### Les trois usages de `lea` en code GCC

**Usage 1 — Calculer l'adresse d'une variable locale** (équivalent de l'opérateur `&` en C)

```c
// Code C
int x = 42;  
scanf("%d", &x);    // passe l'adresse de x  
```

```asm
; Assembleur
mov     dword [rbp-0x4], 0x2a    ; x = 42  
lea     rsi, [rbp-0x4]            ; rsi = &x (adresse de x sur la pile)  
lea     rdi, [rip+0x1234]         ; rdi = adresse de la chaîne "%d"  
call    scanf@plt  
```

Ici, `lea rsi, [rbp-0x4]` met dans `rsi` l'adresse de la variable `x`, pas sa valeur. C'est exactement l'opérateur `&` du C.

**Usage 2 — Charger l'adresse d'une donnée globale / chaîne** (adressage RIP-relatif)

```asm
lea     rdi, [rip+0x2e5a]        ; rdi = adresse d'une chaîne dans .rodata  
call    puts@plt  
```

Ce pattern est omniprésent dans les binaires PIE (*Position Independent Executable*), qui est le mode par défaut de GCC sur les distributions modernes. Toutes les références aux données globales et aux chaînes littérales passent par un `lea` avec adressage relatif à `rip`. Dans Ghidra ou IDA, le désassembleur résout souvent cet offset et affiche directement la chaîne :

```asm
lea     rdi, [rip+0x2e5a]    ; "Hello, world!"
```

**Usage 3 — Arithmétique déguisée** (le détournement le plus déroutant)

GCC utilise fréquemment `lea` pour effectuer des **additions et multiplications simples en une seule instruction**, en exploitant le mécanisme de calcul d'adresse du processeur :

```asm
lea     eax, [rdi+rsi]           ; eax = rdi + rsi        (addition)  
lea     eax, [rdi+rdi*2]         ; eax = rdi * 3          (multiplication par 3)  
lea     eax, [rdi*4+0x5]         ; eax = rdi * 4 + 5      (scale + offset)  
lea     eax, [rdi+rsi*8+0xa]     ; eax = rdi + rsi*8 + 10 (combinaison)  
```

Cet usage n'a **rien à voir avec des adresses** — c'est de l'arithmétique pure. GCC choisit `lea` plutôt qu'une séquence `add` + `imul` parce que `lea` peut combiner une addition, un décalage (multiplication par 1, 2, 4 ou 8) et un offset immédiat en une seule instruction.

```c
// Code C
int index = row * 3 + col;
```

```asm
; GCC -O2
lea     eax, [rdi+rdi*2]     ; eax = row * 3  
add     eax, esi              ; eax = row * 3 + col  
```

Ou encore plus compact si les registres sont bien placés :

```asm
lea     eax, [rsi+rdi*2+rdi]  ; eax = col + row*2 + row = col + row*3
```

> 💡 **Pour le RE** : quand vous voyez un `lea` dont l'expression entre crochets implique des multiplications (`*2`, `*4`, `*8`) ou des additions de registres, ne cherchez pas un accès mémoire — c'est de l'arithmétique optimisée par le compilateur. Traduisez simplement l'expression telle quelle.

### Le mode d'adressage x86-64 en un coup d'œil

L'expression entre crochets, que ce soit dans un `mov` ou un `lea`, suit la forme générale :

```
[base + index * scale + displacement]
```

Où :

- **base** : un registre quelconque (souvent `rbp`, `rsp`, `rax`…)  
- **index** : un registre quelconque sauf `rsp`  
- **scale** : un facteur multiplicatif, uniquement **1, 2, 4 ou 8**  
- **displacement** : une constante immédiate signée (8 ou 32 bits)

Tous les composants sont optionnels. Voici des exemples concrets et leur signification en termes C :

| Assembleur | Calcul d'adresse | Correspondance C typique |  
|---|---|---|  
| `[rbp-0x4]` | `rbp - 4` | Variable locale `int` |  
| `[rdi]` | `rdi` | Déréférencement `*ptr` |  
| `[rdi+0x10]` | `rdi + 16` | Champ de structure `ptr->field` |  
| `[rdi+rsi*4]` | `rdi + rsi*4` | Tableau d'`int` : `arr[i]` |  
| `[rdi+rsi*8]` | `rdi + rsi*8` | Tableau de pointeurs : `ptrs[i]` |  
| `[rip+0x2345]` | `rip + 0x2345` | Donnée globale / chaîne littérale |

Le facteur *scale* (1, 2, 4, 8) correspond exactement à la taille de l'élément dans un tableau : 1 pour `char`, 2 pour `short`, 4 pour `int`/`float`, 8 pour `long`/`double`/pointeur. C'est un indice précieux pour deviner le type des données manipulées.

---

## Syntaxe AT&T vs syntaxe Intel — l'essentiel

Deux syntaxes coexistent dans l'écosystème GNU. Par défaut, `objdump` et GAS (l'assembleur GNU) utilisent la syntaxe AT&T, tandis que la plupart des outils de RE (Ghidra, IDA, Binary Ninja) utilisent la syntaxe Intel. Voici les différences principales :

| Caractéristique | Intel (ce tutoriel) | AT&T (`objdump` par défaut) |  
|---|---|---|  
| Ordre des opérandes | `mov dest, src` | `mov src, dest` |  
| Préfixe registres | aucun (`rax`) | `%` (`%rax`) |  
| Préfixe immédiats | aucun (`0x42`) | `$` (`$0x42`) |  
| Suffixe de taille | mot-clé (`dword`, `qword`) | suffixe d'instruction (`l`, `q`) |  
| Accès mémoire | `[rbp-0x4]` | `-0x4(%rbp)` |

Le même code dans les deux syntaxes :

```asm
; Intel
mov     dword [rbp-0x4], 0x2a  
lea     rdi, [rip+0x1234]  
call    puts@plt  
```

```asm
# AT&T
movl    $0x2a, -0x4(%rbp)  
lea     0x1234(%rip), %rdi  
call    puts@plt  
```

Pour forcer `objdump` en syntaxe Intel :

```bash
objdump -d -M intel ./mon_binaire
```

Ce tutoriel utilise exclusivement la syntaxe Intel. Le chapitre 7 revient en détail sur les deux syntaxes et la conversion de l'une à l'autre.

---

## `nop` — l'instruction qui ne fait rien (mais qui compte)

Vous croiserez régulièrement des instructions `nop` (*No Operation*) dans le code désassemblé. Elles n'effectuent aucune opération et ne modifient aucun registre ni flag.

```asm
nop                          ; 1 octet — l'opcode 0x90  
nop dword [rax]              ; multi-octets (padding)  
nop word [rax+rax+0x0]       ; variante longue (padding)  
```

GCC insère des `nop` pour **aligner** le début de fonctions ou de boucles sur des frontières de 16 octets, ce qui améliore les performances du cache d'instructions et de la prédiction de branche. Ce n'est jamais de la logique applicative — en RE, on peut les ignorer sans perdre d'information.

> 💡 **Pour le RE** : si vous voyez des séquences de `nop` inhabituellement longues à l'intérieur d'une fonction (pas juste en padding entre fonctions), cela peut être le signe d'un **patch** appliqué au binaire — les opcodes originaux ont été remplacés par des `nop` pour désactiver une instruction ou un branchement. C'est une technique classique de binary patching que l'on verra au chapitre 21.

---

## Assembler le tout : lire un prologue et un épilogue complets

Maintenant que chaque instruction est comprise individuellement, observons comment elles s'enchaînent dans une vraie fonction compilée par GCC en `-O0` :

```c
// Code C source
#include <stdio.h>

void greet(const char *name) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Hello, %s!", name);
    puts(buf);
}
```

```asm
; Désassemblage GCC -O0 (syntaxe Intel, simplifié)

greet:
    ; === PROLOGUE ===
    push    rbp                      ; sauvegarde l'ancien base pointer
    mov     rbp, rsp                 ; établit le nouveau base pointer
    sub     rsp, 0x50                ; réserve 80 octets sur la pile (buf + alignement)
    
    ; sauvegarde de l'argument name (passé dans rdi)
    mov     qword [rbp-0x48], rdi    ; stocke name sur la pile

    ; === CORPS ===
    ; appel à snprintf(buf, 64, "Hello, %s!", name)
    mov     rcx, qword [rbp-0x48]    ; rcx = name         (4ᵉ argument)
    lea     rdx, [rip+0xf2a]         ; rdx = "Hello, %s!" (3ᵉ argument)
    mov     esi, 0x40                ; esi = 64            (2ᵉ argument)
    lea     rdi, [rbp-0x40]          ; rdi = &buf[0]       (1ᵉʳ argument — adresse de buf)
    call    snprintf@plt

    ; appel à puts(buf)
    lea     rdi, [rbp-0x40]          ; rdi = &buf[0]       (1ᵉʳ argument)
    call    puts@plt

    ; === ÉPILOGUE ===
    leave                            ; équivalent de : mov rsp, rbp / pop rbp
    ret                              ; retourne à l'appelant
```

Chaque instruction de cet exemple a été couverte dans cette section :

- `push rbp` / `mov rbp, rsp` / `sub rsp, N` — le prologue classique.  
- `mov` dans ses variantes registre-mémoire pour sauvegarder et charger des valeurs.  
- `lea` pour calculer l'adresse de `buf` (variable locale) et l'adresse de la chaîne de format.  
- `call` pour les appels de fonctions via la PLT.  
- `leave` + `ret` — l'épilogue. `leave` est un raccourci pour `mov rsp, rbp` suivi de `pop rbp` ; il défait en une instruction ce que le prologue a construit en deux.

---

## Ce qu'il faut retenir pour la suite

1. **`mov`** est partout — apprenez à lire rapidement ses formes (registre ← mémoire, mémoire ← registre, registre ← immédiat) et les préfixes de taille (`byte`, `dword`, `qword`).  
2. **`push`/`pop`** encadrent les fonctions (prologue/épilogue) et sauvegardent les registres callee-saved — leur comptage vérifie la cohérence d'une stack frame.  
3. **`call`/`ret`** structurent le flux d'exécution — un `call` direct donne la cible, un `call` indirect nécessite une résolution (pointeur de fonction, vtable, PLT).  
4. **`lea`** ne touche jamais la mémoire — c'est soit le calcul d'une adresse (`&x` en C), soit de l'arithmétique optimisée (`a + b*4 + c`).  
5. **Les crochets `[ ]` signifient toujours un accès mémoire**, sauf dans un `lea` où ils délimitent le calcul de l'adresse effective.  
6. **La syntaxe Intel** (dest, src) est le standard de ce tutoriel et de la majorité des outils de RE.

---


⏭️ [Arithmétique et logique : `add`, `sub`, `imul`, `xor`, `shl`/`shr`, `test`, `cmp`](/03-assembleur-x86-64/03-arithmetique-logique.md)
