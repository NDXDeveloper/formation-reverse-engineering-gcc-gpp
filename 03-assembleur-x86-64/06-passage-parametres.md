🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.6 — Passage des paramètres : `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` puis la pile

> 🎯 **Objectif de cette section** : savoir identifier, dans le code désassemblé, les arguments passés à chaque appel de fonction — un réflexe fondamental du reverse engineering. La section 3.5 a posé les règles de la convention System V AMD64. Ici, on passe à la pratique : comment ces règles se manifestent concrètement dans le désassemblage GCC, quels patterns observer, et quels pièges éviter.

---

## Le réflexe « lecture des arguments »

En RE, chaque `call` que vous croisez pose la même question immédiate : **« quels arguments cette fonction reçoit-elle ? »**. La réponse se trouve dans les instructions qui **précèdent** le `call` — ce sont les instructions de préparation des arguments.

La méthode est toujours la même :

1. Repérer le `call`.  
2. Remonter instruction par instruction pour identifier les écritures dans `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` (et `xmm0`–`xmm7` pour les flottants).  
3. Vérifier s'il y a des `push` ou des `mov [rsp+X], ...` pour les arguments au-delà du sixième.

```asm
; Quel appel est-ce ?
mov     edx, 0xa             ; 3e argument = 10  
lea     rsi, [rip+0x2f1a]    ; 2e argument = adresse d'une chaîne  
mov     rdi, rbx              ; 1er argument = valeur de rbx (un pointeur ?)  
call    some_function  
```

En trois lignes, on sait que `some_function` reçoit trois arguments : un pointeur dans `rdi`, une chaîne dans `rsi`, et l'entier 10 dans `rdx`.

---

## Récapitulatif de l'ordre des registres

Pour les arguments entiers et pointeurs :

```
Argument :   1er     2e      3e      4e      5e      6e      7e+  
Registre :   rdi     rsi     rdx     rcx     r8      r9      pile  
```

Pour les arguments flottants (`float`, `double`) :

```
Argument :   1er     2e      3e      4e      5e      6e      7e      8e      9e+  
Registre :   xmm0    xmm1    xmm2    xmm3    xmm4    xmm5    xmm6    xmm7    pile  
```

Les compteurs entiers et flottants sont **indépendants**. Une fonction `f(int a, double b, int c)` utilise `edi` pour `a`, `xmm0` pour `b`, et `esi` pour `c` — pas `rdx`.

---

## Exemples pratiques détaillés

### Appel simple : `puts(msg)`

```c
puts("Hello, world!");
```

```asm
lea     rdi, [rip+0x2e5a]    ; rdi = adresse de "Hello, world!" dans .rodata  
call    puts@plt  
```

Un seul argument, un pointeur vers une chaîne. Le `lea` avec adressage RIP-relatif est le pattern standard pour charger l'adresse d'une chaîne littérale.

### Appel avec plusieurs types : `printf(fmt, n, x)`

```c
printf("n = %d, x = %f\n", n, x);
```

```asm
lea     rdi, [rip+0x1b3f]        ; 1er arg (entier #1) : format string  
mov     esi, dword [rbp-0x4]     ; 2e arg (entier #2)  : n  
movsd   xmm0, qword [rbp-0x10]  ; 1er arg flottant     : x  
mov     eax, 1                    ; nombre de registres SSE utilisés (variadique)  
call    printf@plt  
```

Observez comment les compteurs sont indépendants : `rdi` reçoit le 1er argument entier, `rsi` le 2e argument entier, et `xmm0` le 1er argument flottant. Le `mov eax, 1` est le compteur SSE spécifique aux fonctions variadiques (cf. section 3.5).

### Appel avec 6 arguments : `mmap`

```c
void *p = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
```

```asm
xor     r9d, r9d              ; 6e arg : offset = 0  
mov     r8d, 0xffffffff       ; 5e arg : fd = -1  
mov     ecx, 0x22             ; 4e arg : MAP_PRIVATE|MAP_ANONYMOUS (0x22)  
mov     edx, 0x3              ; 3e arg : PROT_READ|PROT_WRITE (0x3)  
mov     esi, 0x1000           ; 2e arg : length = 4096  
xor     edi, edi              ; 1er arg : addr = NULL  
call    mmap@plt  
```

Les six registres d'arguments sont utilisés. Notez que GCC utilise `xor edi, edi` pour passer `NULL` (0) et `xor r9d, r9d` pour passer 0 — l'idiome de mise à zéro vu en section 3.3.

> 💡 **Pour le RE** : quand vous connaissez le prototype de la fonction appelée (parce que c'est une fonction de la libc, un syscall wrapper, ou parce que vous l'avez déjà analysée), vous pouvez **nommer** chaque registre d'argument. C'est le premier pas de l'annotation du désassemblage.

### Appel avec plus de 6 arguments : arguments sur la pile

```c
long result = func7(10, 20, 30, 40, 50, 60, 70);
```

```asm
; 7e argument → pile
mov     dword [rsp], 0x46        ; [rsp] = 70 (7e argument, au sommet de pile)  
mov     r9d, 0x3c                ; 6e arg = 60  
mov     r8d, 0x32                ; 5e arg = 50  
mov     ecx, 0x28                ; 4e arg = 40  
mov     edx, 0x1e                ; 3e arg = 30  
mov     esi, 0x14                ; 2e arg = 20  
mov     edi, 0xa                 ; 1er arg = 10  
call    func7  
```

Quand l'espace pour les arguments pile a été réservé dans le prologue (via le `sub rsp`), GCC utilise `mov [rsp+offset], valeur` plutôt que `push`. Cela évite de modifier `rsp` entre les arguments.

S'il y a plusieurs arguments sur la pile, ils sont disposés à des offsets croissants depuis `rsp` :

```asm
mov     qword [rsp+0x8], 80     ; 8e argument  
mov     qword [rsp], 70         ; 7e argument (au sommet)  
; ... registres pour les 6 premiers ...
call    func8
```

> 💡 **Pour le RE** : les arguments sur la pile sont moins intuitifs à repérer que les arguments registres, car les `mov [rsp+X], ...` peuvent se confondre avec des écritures dans des variables locales. La clé est la **proximité avec le `call`** : si un `mov [rsp+X]` apparaît dans le bloc de préparation d'arguments juste avant un `call`, c'est probablement un argument pile, pas une variable locale.

---

## Passage de structures par valeur

Quand une structure est passée par valeur, le comportement dépend de sa taille et de sa composition :

### Petites structures (≤ 16 octets)

Les structures de 16 octets ou moins qui contiennent des types entiers sont décomposées et passées dans des registres :

```c
typedef struct {
    int x;
    int y;
} Point;

void draw(Point p, int color);
```

```asm
; draw((Point){10, 20}, 0xFF0000)
; La structure Point (8 octets) tient dans un seul registre 64 bits
mov     rdi, 0x0000001400000000a  ; rdi = {x=10, y=20} packés en 64 bits
                                   ; (10 dans les 32 bits bas, 20 dans les 32 bits hauts)
mov     esi, 0xff0000             ; 2e arg : color  
call    draw  
```

En pratique, GCC peut utiliser un ou deux registres selon la taille et le contenu :

| Taille struct | Contenu | Registres utilisés |  
|---|---|---|  
| ≤ 8 octets, entiers | `int`, `short`, `char`, pointeurs | 1 registre (`rdi`) |  
| 9–16 octets, entiers | deux champs de 8 octets | 2 registres (`rdi` + `rsi`) |  
| ≤ 16 octets, mixte | entiers + flottants | 1 entier (`rdi`) + 1 SSE (`xmm0`) |

### Grandes structures (> 16 octets)

Les structures de plus de 16 octets sont passées **par copie sur la pile**. L'appelant copie la structure entière sur la pile avant le `call` :

```c
typedef struct {
    char name[32];
    int id;
    double score;
} Record;

void process(Record r);
```

```asm
; Copie de la structure sur la pile avant l'appel
sub     rsp, 0x30                    ; réserve l'espace  
mov     rax, qword [rbp-0x38]       ; copie les 8 premiers octets  
mov     qword [rsp], rax  
mov     rax, qword [rbp-0x30]       ; copie les 8 suivants  
mov     qword [rsp+0x8], rax  
; ... copie continue ...
call    process
```

> ⚠️ **Pour le RE** : une série de `mov` qui copie des blocs de 8 octets de `[rbp-X]` vers `[rsp+Y]` juste avant un `call` est la signature du passage d'une grosse structure par valeur. Si vous voyez ce pattern, la fonction reçoit une structure, pas une série d'arguments indépendants. GCC peut aussi utiliser `rep movsb` ou `rep movsq` pour les copies volumineuses.

### Le cas courant : passage par pointeur

En pratique, les programmeurs C passent les grosses structures par pointeur (`const struct *`), ce qui se réduit à un simple passage de pointeur dans un registre :

```c
void process(const Record *r);
```

```asm
lea     rdi, [rbp-0x38]     ; rdi = adresse de la structure locale  
call    process  
```

C'est la forme la plus fréquente en code réel. Un `lea rdi, [rbp-X]` suivi d'un `call` signifie presque toujours « passe l'adresse d'une variable locale ».

---

## Reconnaître les arguments dans du code optimisé

En `-O0`, la préparation des arguments est un bloc d'instructions linéaire et prévisible juste avant le `call`. Avec les optimisations (`-O1` et au-delà), les choses se compliquent.

### Propagation des arguments

GCC peut propager les arguments d'une fonction directement dans les registres d'appel de la fonction suivante, sans passer par la pile :

```c
void wrapper(int a, int b) {
    inner(a, b, 0);
}
```

En `-O0` :

```asm
; Sauvegarde les arguments sur la pile, puis les recharge
wrapper:
    push    rbp
    mov     rbp, rsp
    mov     dword [rbp-0x4], edi     ; sauvegarde a
    mov     dword [rbp-0x8], esi     ; sauvegarde b
    mov     edx, 0                    ; 3e arg = 0
    mov     esi, dword [rbp-0x8]     ; recharge b → 2e arg
    mov     edi, dword [rbp-0x4]     ; recharge a → 1er arg
    call    inner
    ; ...
```

En `-O2` :

```asm
; Les arguments restent dans leurs registres d'origine
wrapper:
    xor     edx, edx         ; 3e arg = 0
    jmp     inner             ; tail call — rdi et rsi sont déjà en place !
```

Le code optimisé est beaucoup plus compact : `rdi` et `rsi` contiennent déjà `a` et `b` (puisque `wrapper` les a reçus dans ces mêmes registres), donc GCC se contente d'ajouter le troisième argument et fait un *tail call* (`jmp` au lieu de `call` — cf. chapitre 16).

> 💡 **Pour le RE** : en code optimisé, les arguments ne sont pas toujours visiblement écrits dans les registres juste avant le `call`. Si une fonction reçoit `x` dans `rdi` et appelle immédiatement `inner(x, ...)`, le `rdi` n'est jamais réécrit — il est déjà en place. Il faut alors remonter plus loin dans le code (voire jusqu'au début de la fonction) pour comprendre d'où vient la valeur.

### Entrelacement des préparations d'arguments

GCC optimisé peut **entrelacer** la préparation d'arguments de plusieurs appels ou mélanger calculs et chargements d'arguments :

```asm
mov     ebx, edi              ; sauvegarde le 1er arg dans rbx (callee-saved)  
lea     rdi, [rip+0x1234]    ; prépare le 1er arg du PREMIER call  
mov     esi, ebx              ; prépare le 2e arg du PREMIER call  
call    printf@plt  

mov     edi, ebx              ; prépare le 1er arg du SECOND call  
call    process  
```

Ici, `ebx` sert de « variable temporaire » callee-saved pour conserver la valeur originale de `edi` à travers le premier `call`. C'est un pattern classique d'optimisation.

### Arguments calculés

L'argument peut être le résultat d'un calcul complexe, pas une simple copie :

```c
process(a * 3 + offset, ptr->field);
```

```asm
lea     edi, [rax+rax*2]         ; edi = a * 3  
add     edi, ecx                  ; edi = a * 3 + offset (1er argument)  
mov     esi, dword [rdx+0x10]    ; esi = ptr->field      (2e argument)  
call    process  
```

Le premier argument n'est pas un simple chargement mais le résultat d'un `lea` + `add`. C'est courant en code optimisé et il faut suivre la chaîne de calculs pour comprendre la valeur réelle passée.

---

## Cas particulier : les fonctions de la libc

Les fonctions de la bibliothèque standard C sont celles que vous rencontrerez le plus souvent dans un binaire. Connaître leur prototype permet d'identifier instantanément les arguments :

### `memcpy` / `memmove`

```c
void *memcpy(void *dest, const void *src, size_t n);
```

```asm
mov     edx, 0x40              ; 3e arg : n = 64 octets  
lea     rsi, [rbp-0x60]        ; 2e arg : src = adresse locale  
mov     rdi, rbx                ; 1er arg : dest = pointeur dans rbx  
call    memcpy@plt  
```

`rdi` = destination, `rsi` = source, `rdx` = taille. L'ordre destination-source-taille est le même qu'en C.

### `strcmp` / `strncmp`

```c
int strcmp(const char *s1, const char *s2);
```

```asm
lea     rsi, [rip+0x1a2b]     ; 2e arg : chaîne constante "password"  
mov     rdi, rax                ; 1er arg : input utilisateur  
call    strcmp@plt  
test    eax, eax                ; vérifie si retour == 0 (chaînes égales)  
je      .success  
```

Ce pattern (`strcmp` suivi de `test eax, eax` / `je` ou `jne`) est la cible classique du reverse engineering de crackmes et de vérifications de mots de passe — on y revient en détail au chapitre 21.

### `malloc` / `calloc` / `free`

```c
void *malloc(size_t size);  
void *calloc(size_t nmemb, size_t size);  
void free(void *ptr);  
```

```asm
; malloc(256)
mov     edi, 0x100          ; 1er arg : taille = 256  
call    malloc@plt  
mov     rbx, rax             ; sauvegarde le pointeur retourné  

; calloc(10, sizeof(int))
mov     esi, 4               ; 2e arg : taille d'un élément = 4  
mov     edi, 0xa             ; 1er arg : nombre d'éléments = 10  
call    calloc@plt  

; free(ptr)
mov     rdi, rbx             ; 1er arg : pointeur à libérer  
call    free@plt  
```

### `open` / `read` / `write` / `close`

```c
int open(const char *pathname, int flags, mode_t mode);  
ssize_t read(int fd, void *buf, size_t count);  
ssize_t write(int fd, const void *buf, size_t count);  
```

```asm
; open("config.dat", O_RDONLY)
xor     esi, esi               ; 2e arg : flags = O_RDONLY (0)  
lea     rdi, [rip+0x2345]     ; 1er arg : pathname  
call    open@plt  
mov     ebx, eax               ; sauvegarde le fd retourné  

; read(fd, buf, 1024)
mov     edx, 0x400             ; 3e arg : count = 1024  
lea     rsi, [rbp-0x420]      ; 2e arg : buf (buffer local)  
mov     edi, ebx               ; 1er arg : fd  
call    read@plt  
```

> 💡 **Pour le RE** : constituer progressivement une « carte mentale » des prototypes de la libc est l'un des investissements les plus rentables. Quand vous voyez un `call` vers une fonction de la libc, les arguments décodés instantanément vous donnent le contexte : quelle chaîne est comparée, quel fichier est ouvert, combien d'octets sont lus, à quelle adresse on copie…

---

## Arguments et fonctions C++ : le pointeur `this`

En C++ compilé par GCC (ABI Itanium), les méthodes non statiques d'une classe reçoivent un **premier argument implicite** : le pointeur `this`, passé dans `rdi`.

```cpp
class Player {  
public:  
    void set_health(int hp);
    int get_health() const;
};

player.set_health(100);  
player.get_health();  
```

```asm
; player.set_health(100)
mov     esi, 0x64            ; 2e arg apparent = 100 (hp)  
mov     rdi, rbx              ; 1er arg = &player (this)  
call    _ZN6Player10set_healthEi  

; player.get_health()
mov     rdi, rbx              ; seul arg = &player (this)  
call    _ZNK6Player10get_healthEv  
```

Tous les arguments explicites de la méthode sont **décalés d'un rang** par rapport au prototype C++ :

| Paramètre C++ | Registre réel |  
|---|---|  
| `this` (implicite) | `rdi` |  
| 1er paramètre explicite | `rsi` |  
| 2e paramètre explicite | `rdx` |  
| 3e paramètre explicite | `rcx` |  
| … | … |

> ⚠️ **Pour le RE** : c'est un piège classique. Si vous analysez une méthode C++ et que vous identifiez ses paramètres, souvenez-vous que `rdi` est `this` — le premier « vrai » paramètre est dans `rsi`. Le chapitre 17 traite en profondeur le modèle objet C++ et le name mangling.

---

## Quand les arguments ne sont plus dans les registres

Plusieurs situations font que les arguments reçus dans les registres finissent sur la pile au début de la fonction :

### Le « spill » en `-O0`

En `-O0`, GCC **sauvegarde systématiquement** tous les arguments reçus par registre sur la pile, même s'il pourrait les garder en registre :

```asm
func:
    push    rbp
    mov     rbp, rsp
    mov     dword [rbp-0x14], edi    ; spill arg 1
    mov     dword [rbp-0x18], esi    ; spill arg 2
    mov     dword [rbp-0x1c], edx    ; spill arg 3
    ; ... le corps utilise [rbp-0x14] etc. plutôt que edi/esi/edx
```

C'est un comportement de débogage : en gardant tout sur la pile, GDB peut toujours inspecter les arguments même après que les registres ont été réutilisés. En `-O1+`, ces spills disparaissent et les arguments restent en registre aussi longtemps que possible.

> 💡 **Pour le RE** : en `-O0`, les premières instructions après le prologue sont souvent des `mov [rbp-X], rdi/rsi/rdx/...`. C'est le spill des arguments. En les comptant, vous obtenez directement le **nombre de paramètres** de la fonction et leur ordre. C'est l'un des avantages du RE sur un binaire non optimisé.

### La sauvegarde avant un `call` interne

Même en code optimisé, si une fonction doit conserver un argument à travers un `call` interne (puisque `rdi`, `rsi` etc. sont caller-saved), elle le sauvegarde soit dans un registre callee-saved, soit sur la pile :

```asm
; Sauvegarde dans un callee-saved
func:
    push    rbx
    mov     ebx, edi          ; sauvegarde arg1 dans rbx (callee-saved)
    ; ... 
    call    helper             ; rdi est écrasé, mais ebx survit
    mov     edi, ebx           ; restaure arg1 pour usage ultérieur
```

```asm
; Sauvegarde sur la pile
func:
    sub     rsp, 0x18
    mov     dword [rsp+0xc], edi    ; sauvegarde arg1 sur la pile
    ; ...
    call    helper
    mov     edi, dword [rsp+0xc]    ; recharge arg1
```

---

## Déduire le prototype d'une fonction inconnue

L'un des exercices centraux du RE est de reconstruire le prototype d'une fonction dont on n'a pas le code source. Voici la méthode systématique :

### Étape 1 — Identifier le nombre d'arguments

Analyser **tous les sites d'appel** (*call sites*) de la fonction (via les cross-references / XREF dans Ghidra ou IDA). Pour chaque appel, noter quels registres sont écrits dans le bloc de préparation :

```
Appel site 1 :  rdi, rsi, edx écrits    → au moins 3 arguments  
Appel site 2 :  rdi, rsi, edx écrits    → confirme 3 arguments  
Appel site 3 :  rdi, rsi écrits seuls   → possible 2 arguments ? Vérifier rdx  
```

Si `rdx` n'est pas explicitement écrit au site 3, il peut conserver une valeur résiduelle — mais si la fonction utilise réellement `rdx`, GCC garantit qu'il est initialisé. Il faut alors regarder le corps de la fonction pour voir si `rdx`/`edx` est lu.

### Étape 2 — Identifier les types d'arguments

Observer la taille des registres utilisés et les opérations effectuées sur les arguments :

| Observation | Type probable |  
|---|---|  
| `mov edi, ...` (32 bits) | `int` ou `unsigned int` |  
| `mov rdi, ...` (64 bits) | pointeur, `long`, `size_t` |  
| `lea rdi, [rip+...]` | pointeur vers donnée globale / chaîne |  
| `lea rdi, [rbp-X]` | pointeur vers variable locale |  
| `movsd xmm0, ...` | `double` |  
| `movss xmm0, ...` | `float` |  
| `movzx edi, byte [...]` | `unsigned char` promu en `int` |  
| `movsx edi, byte [...]` | `char` (signé) promu en `int` |

### Étape 3 — Identifier le type de retour

Observer comment l'appelant utilise `rax`/`eax`/`xmm0` après le `call` :

```asm
call    mystery_func  
test    eax, eax         ; compare le retour avec 0 → retourne int (ou bool)  
je      .error  

call    mystery_func  
mov     rbx, rax          ; sauvegarde un pointeur → retourne void* ou char*  
mov     rdi, rax  
call    strlen@plt         ; passe le retour à strlen → c'est une chaîne !  

call    mystery_func       ; pas d'utilisation de rax après → retourne void  
mov     edi, 0  
call    exit@plt  
```

### Étape 4 — Synthétiser le prototype

En combinant les informations :

```
Arguments :  rdi = pointeur (char*), esi = int, edx = int  
Retour :     eax testé comme booléen, puis chaîne passée à puts  

→ Prototype reconstitué :
   int mystery_func(const char *input, int param1, int param2);
```

> 💡 **Pour le RE** : Ghidra effectue cette analyse automatiquement et propose un prototype inféré dans sa fenêtre « Decompiler ». Mais le résultat n'est pas toujours correct — savoir faire cette analyse manuellement est indispensable pour valider ou corriger les inférences de l'outil.

---

## Différence avec la convention Windows x64 (Microsoft ABI)

Si vous analysez un binaire Windows compilé avec MinGW (GCC pour Windows), la convention d'appel est **différente** :

| Aspect | System V AMD64 (Linux) | Microsoft x64 (Windows) |  
|---|---|---|  
| Registres d'arguments | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` | `rcx`, `rdx`, `r8`, `r9` |  
| Nombre d'args registres | 6 | 4 |  
| *Shadow space* | Non | Oui (32 octets réservés par l'appelant) |  
| Red zone | 128 octets | Aucune |  
| Registres callee-saved | `rbx`, `rbp`, `r12`–`r15` | `rbx`, `rbp`, `rdi`, `rsi`, `r12`–`r15` |  
| Compteur SSE variadique | `al` | Non |

Les deux différences les plus visibles dans le désassemblage :

1. **L'ordre des registres** : sous Windows, le premier argument est dans `rcx` (pas `rdi`). Si vous voyez `mov ecx, ...` comme premier argument, vous analysez probablement un binaire Windows.  
2. **Le shadow space** : sous Windows, l'appelant réserve toujours 32 octets sur la pile avant chaque `call`, même si la fonction appelée n'utilise pas ces octets. Cela produit un `sub rsp, 0x20` (ou plus) systématique avant chaque `call`.

Ce tutoriel se concentre sur la convention System V AMD64 (Linux), mais cette différence est importante à connaître si vous analysez un binaire PE compilé avec MinGW — les flags de compilation GCC sont les mêmes, mais la convention d'appel change.

---

## Ce qu'il faut retenir pour la suite

1. **Lire les arguments = remonter depuis le `call`** en identifiant les écritures dans `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` (entiers) et `xmm0`–`xmm7` (flottants).  
2. **En `-O0`**, les arguments sont systématiquement spilled sur la pile en début de fonction — en les comptant, on obtient le nombre de paramètres.  
3. **En code optimisé**, les arguments peuvent rester dans leurs registres d'origine, être propagés directement, ou être sauvegardés dans des registres callee-saved (`rbx`, `r12`…) plutôt que sur la pile.  
4. **En C++**, `rdi` est toujours `this` pour les méthodes non statiques — tous les paramètres explicites sont décalés d'un rang.  
5. **La taille du registre révèle le type** : `edi` = `int`, `rdi` = pointeur, `xmm0` via `movsd` = `double`, `xmm0` via `movss` = `float`.  
6. **Les prototypes de la libc** sont votre meilleur allié : connaître `strcmp(rdi, rsi)`, `memcpy(rdi, rsi, rdx)`, `open(rdi, esi, edx)` accélère considérablement l'analyse.  
7. **Sous Windows** (MinGW), la convention change : `rcx`, `rdx`, `r8`, `r9` au lieu de `rdi`, `rsi`, `rdx`, `rcx` — et le shadow space de 32 octets est omniprésent.

---


⏭️ [Lire un listing assembleur sans paniquer : méthode pratique en 5 étapes](/03-assembleur-x86-64/07-methode-lecture-asm.md)
