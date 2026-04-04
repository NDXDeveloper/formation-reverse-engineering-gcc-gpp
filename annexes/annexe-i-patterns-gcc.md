🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe I — Patterns GCC reconnaissables à l'assembleur (idiomes compilateur)

> 📎 **Fiche de référence** — Cette annexe catalogue les séquences d'instructions x86-64 caractéristiques que GCC génère pour des constructions C/C++ courantes. Reconnaître ces idiomes (*compiler idioms*) accélère considérablement la lecture du désassemblage : au lieu de déchiffrer chaque instruction individuellement, vous identifiez le pattern d'un coup et reconstruisez la construction C correspondante. L'annexe est organisée par type de construction, avec pour chacune le code C d'origine, le code assembleur typique produit par GCC et les explications nécessaires à l'identification.

---

## Conventions

Tous les exemples assembleur sont en **syntaxe Intel** et correspondent à du code compilé par GCC sur x86-64 Linux (System V AMD64 ABI). Les niveaux d'optimisation sont indiqués quand ils influencent le pattern. Les registres utilisés dans les exemples sont illustratifs — GCC peut choisir n'importe quel registre disponible selon le contexte.

Les abréviations suivantes sont utilisées :

| Abréviation | Signification |  
|-------------|---------------|  
| `-O0` | Sans optimisation (mode debug, code le plus lisible) |  
| `-O1` | Optimisation basique |  
| `-O2` | Optimisation standard (défaut pour la production) |  
| `-O3` | Optimisation agressive (vectorisation, déroulage) |

---

## 1 — Mise à zéro d'un registre

### Pattern

```asm
xor    eax, eax
```

### Équivalent C

```c
int x = 0;
// ou : return 0;
// ou : préparation de rax avant un appel variadique (al = 0)
```

### Explication

`xor eax, eax` est l'idiome universel pour mettre un registre à zéro. Il est préféré à `mov eax, 0` car il est encodé en 2 octets au lieu de 5 et il brise les dépendances de données sur les processeurs modernes (le processeur reconnaît le pattern comme un *zeroing idiom*). GCC l'utilise systématiquement à tous les niveaux d'optimisation.

En x86-64, écrire dans un registre 32 bits (`eax`) met automatiquement à zéro les 32 bits supérieurs du registre 64 bits correspondant (`rax`). Donc `xor eax, eax` met tout `rax` à zéro.

Ce pattern a trois contextes d'apparition fréquents : initialiser une variable à zéro, préparer la valeur de retour `return 0`, et positionner `al = 0` avant un appel à une fonction variadique (`printf`, etc.) pour indiquer qu'aucun registre XMM n'est utilisé.

---

## 2 — Division par constante via multiplication magique

### Pattern (division non signée)

```asm
mov    eax, edi           ; eax = dividende  
mov    edx, 0xCCCCCCCD    ; constante magique pour /10  
imul   rax, rdx           ; multiplication 64 bits  
shr    rax, 35            ; décalage pour obtenir le quotient  
```

### Pattern alternatif (division non signée, forme `mul`)

```asm
mov    eax, edi  
mov    ecx, 0xAAAAAAAB    ; constante magique pour /3  
mul    rcx                ; rdx:rax = rax * rcx  
shr    rdx, 1             ; quotient dans rdx  
```

### Pattern (division signée)

```asm
mov    eax, edi  
mov    edx, 0x66666667    ; constante magique signée pour /5  
imul   edx                ; edx:eax = eax * edx (signé)  
sar    edx, 1             ; décalage arithmétique  
mov    eax, edx  
shr    eax, 31            ; extraction du bit de signe  
add    edx, eax           ; correction pour les nombres négatifs  
```

### Équivalent C

```c
unsigned q = x / 10;   // premier pattern  
unsigned q = x / 3;    // deuxième pattern  
int q = x / 5;         // troisième pattern (signé)  
```

### Explication

GCC remplace **toute division entière par une constante connue à la compilation** par une multiplication suivie d'un décalage. L'instruction `div`/`idiv` est extrêmement lente (20–90 cycles selon le processeur) alors que `imul` + `shr` prend 3–4 cycles.

La « constante magique » est l'inverse multiplicatif modulaire du diviseur, calculé par le compilateur. Chaque diviseur a sa propre constante. Voici les plus fréquentes :

| Diviseur | Type | Constante magique | Décalage |  
|----------|------|-------------------|----------|  
| 3 | unsigned | `0xAAAAAAAB` | `shr rdx, 1` |  
| 5 | unsigned | `0xCCCCCCCD` | `shr rax, 34` |  
| 7 | unsigned | `0x24924925` | `shr rdx, 2` (après ajustement) |  
| 10 | unsigned | `0xCCCCCCCD` | `shr rax, 35` |  
| 12 | unsigned | `0xAAAAAAAB` | `shr rdx, 3` |  
| 100 | unsigned | `0x51EB851F` | `shr rdx, 5` |  
| 1000 | unsigned | `0x10624DD3` | `shr rdx, 6` |  
| 3 | signed | `0x55555556` | (pas de décalage supplémentaire) |  
| 5 | signed | `0x66666667` | `sar edx, 1` |  
| 7 | signed | `0x92492493` | `sar edx, 2` (après ajustement) |  
| 10 | signed | `0x66666667` | `sar edx, 2` |

**Comment identifier ce pattern** : si vous voyez un `imul` ou `mul` avec une grande constante hexadécimale qui ne semble pas significative, suivi d'un `shr` ou `sar`, c'est presque certainement une division par constante. Pour retrouver le diviseur, vous pouvez utiliser la calculatrice Python : `hex(round((2**35) / 10))` → `0xCCCCCCCD` (pour la division non signée par 10 avec décalage 35).

Pour la division signée, le pattern est plus complexe car GCC doit corriger l'arrondi vers zéro (la division signée C arrondit vers zéro, contrairement au décalage arithmétique qui arrondit vers moins l'infini). La correction consiste à ajouter le bit de signe du résultat intermédiaire (`shr eax, 31` + `add`).

---

## 3 — Modulo par constante

### Pattern (puissance de 2)

```asm
and    eax, 0x0F          ; x % 16  
and    eax, 0x07          ; x % 8  
and    eax, 0x01          ; x % 2 (test de parité)  
```

### Pattern (non puissance de 2)

GCC calcule d'abord le quotient via multiplication magique (pattern §2), puis reconstruit le reste :

```asm
; x % 10 (unsigned)
mov    eax, edi  
mov    edx, 0xCCCCCCCD  
imul   rax, rdx  
shr    rax, 35            ; quotient q = x / 10  
lea    eax, [rax+rax*4]   ; eax = q * 5  
add    eax, eax           ; eax = q * 10  
sub    edi, eax           ; reste = x - q * 10  
mov    eax, edi  
```

### Équivalent C

```c
unsigned r = x % 16;   // → and  
unsigned r = x % 10;   // → mul magique + sub  
```

### Explication

Pour les puissances de 2, le modulo non signé se réduit à un masque AND : `x % 2^n` est équivalent à `x & (2^n - 1)`. C'est un pattern très courant.

Pour les autres constantes, GCC utilise l'identité `x % d = x - (x / d) * d` : il calcule le quotient par multiplication magique, multiplie le quotient par le diviseur (souvent via `lea` pour les petites constantes), puis soustrait du dividende original.

Pour le modulo signé par une puissance de 2, le pattern est plus complexe car le reste doit avoir le même signe que le dividende :

```asm
; x % 8 (signed)
mov    eax, edi  
cdq                        ; edx = signe de eax (0 ou -1)  
shr    edx, 29             ; edx = 0 ou 7 (biais de correction)  
add    eax, edx            ; ajoute le biais si négatif  
and    eax, 7              ; masque  
sub    eax, edx            ; retire le biais  
```

---

## 4 — Multiplication par petite constante via `lea`

### Patterns

```asm
lea    eax, [rdi+rdi*2]         ; eax = rdi * 3  
lea    eax, [rdi*4]             ; eax = rdi * 4  
lea    eax, [rdi+rdi*4]         ; eax = rdi * 5  
lea    eax, [rdi+rdi*8]         ; eax = rdi * 9  
lea    eax, [rdi+rdi*2]  
add    eax, eax                 ; eax = rdi * 6  (3 * 2)  
lea    eax, [rdi+rdi*2]  
lea    eax, [rax+rax*4]         ; eax = rdi * 15 (3 * 5)  
lea    eax, [rdi+rdi*4]  
add    eax, eax                 ; eax = rdi * 10 (5 * 2)  
```

### Équivalent C

```c
int y = x * 3;  
int y = x * 5;  
int y = x * 10;  
// etc.
```

### Explication

`lea` peut calculer `base + index * scale + displacement` en un seul cycle, avec `scale` limité à 1, 2, 4 ou 8. GCC exploite cette capacité pour remplacer les multiplications par petites constantes (typiquement 2–15) par une ou deux instructions `lea`, parfois combinées avec `add` ou `shl`.

Ce pattern est très fréquent en `-O2` et au-delà. Il est plus rapide qu'un `imul` car `lea` a une latence de 1 cycle contre 3 pour `imul`. De plus, `lea` ne modifie pas les flags, ce qui évite les dépendances de données.

**Comment l'identifier** : un `lea` dont l'opérande mémoire utilise le même registre comme base et comme index (`[rdi+rdi*N]`) est presque toujours une multiplication, pas un accès mémoire.

---

## 5 — Structure `if` / `else`

### Pattern `-O0` (avec frame pointer)

```asm
cmp    dword ptr [rbp-0x4], 5    ; compare x avec 5  
jne    .L_else                    ; si x != 5, saute au else  
; --- bloc then ---
call   do_something  
jmp    .L_end  
.L_else:
; --- bloc else ---
call   do_other
.L_end:
```

### Pattern `-O2` (branchless avec `cmov`)

```asm
cmp    edi, 5  
cmove  eax, ecx      ; si edi == 5, eax = ecx (valeur "then")  
                      ; sinon eax conserve la valeur "else"
```

### Équivalent C

```c
// Version -O0
if (x == 5) {
    do_something();
} else {
    do_other();
}

// Version -O2 (branchless)
int result = (x == 5) ? value_then : value_else;
```

### Explication

Le pattern fondamental du `if`/`else` est un `cmp` suivi d'un saut conditionnel. Le point crucial est que **la condition du saut est l'inverse de la condition du `if` en C** : le saut saute par-dessus le bloc `then` vers le `else`, donc un `if (x == 5)` produit un `jne` (saut si *pas* égal).

À partir de `-O2`, GCC remplace les branchements simples par des instructions `cmovcc` quand les deux branches sont des expressions simples (pas des appels de fonctions). C'est le pattern *branchless* : au lieu de sauter, le processeur calcule les deux valeurs et sélectionne la bonne conditionnellement. C'est plus performant quand la branche est difficile à prédire.

---

## 6 — Boucle `for` / `while`

### Pattern `for` en `-O0`

```asm
mov    dword ptr [rbp-0x4], 0    ; int i = 0  
jmp    .L_cond                    ; saute à la condition  
.L_body:
; --- corps de la boucle ---
add    dword ptr [rbp-0x4], 1    ; i++
.L_cond:
cmp    dword ptr [rbp-0x4], 10   ; i < 10 ?  
jl     .L_body                    ; si oui, recommencer  
```

### Pattern `for` en `-O2` (compteur dans un registre)

```asm
xor    ecx, ecx                  ; i = 0
.L_loop:
; --- corps de la boucle (utilise ecx comme compteur) ---
add    ecx, 1                    ; i++  (ou inc ecx / lea ecx,[rcx+1])  
cmp    ecx, 10                   ; i < 10 ?  
jl     .L_loop  
```

### Pattern `for` inversé (count-down) en `-O2`

```asm
mov    ecx, 10                   ; compteur = 10
.L_loop:
; --- corps ---
sub    ecx, 1                    ; compteur--  
jnz    .L_loop                   ; boucle tant que != 0  
```

### Pattern `while` en `-O2` (condition dupliquée)

```asm
; GCC peut transformer while(cond) en : if(cond) do { ... } while(cond)
test   edi, edi                  ; entrée dans la boucle : x != 0 ?  
je     .L_skip                   ; si x == 0, ne pas entrer  
.L_loop:
; --- corps ---
test   edi, edi  
jne    .L_loop  
.L_skip:
```

### Équivalent C

```c
for (int i = 0; i < 10; i++) { ... }  
while (x != 0) { ... }  
```

### Explication

En `-O0`, GCC place la condition de boucle **en bas** et saute vers elle depuis l'initialisation. Le corps de la boucle est entre le label de corps et le label de condition. Le compteur est stocké en mémoire (pile).

En `-O2`, le compteur est dans un registre, et GCC peut inverser le sens du comptage (count-down) car `sub + jnz` est souvent plus efficace que `add + cmp + jl` (un `sub` qui atteint zéro positionne ZF directement, éliminant le besoin d'un `cmp` séparé).

GCC peut aussi transformer un `while` en `do-while` avec un test de garde initial (*loop rotation*). Ce schéma `if(cond) do { ... } while(cond)` est plus efficace car il place le saut arrière en fin de boucle, ce qui est mieux prédit par le branch predictor.

**Comment identifier une boucle** : cherchez un saut arrière (vers une adresse inférieure). Les sauts en avant sont des `if`/`else`, les sauts en arrière sont des boucles.

---

## 7 — `switch` / `case`

### Pattern : jump table (cas consécutifs)

```asm
; switch (x) avec des cas 0, 1, 2, 3, 4
cmp    edi, 4  
ja     .L_default               ; si x > 4, cas default  
lea    rdx, [rip + .L_jumptable]  
movsxd rax, dword ptr [rdx+rdi*4]  ; lit l'offset relatif  
add    rax, rdx                 ; adresse absolue du cas  
jmp    rax                      ; saut indirect  
```

La jump table est stockée dans `.rodata` et contient des offsets relatifs (4 octets chacun) vers chaque label de `case`.

### Pattern : chaîne de `cmp`/`jz` (cas dispersés)

```asm
; switch (x) avec des cas 1, 50, 100, 999
cmp    edi, 50  
je     .L_case_50  
cmp    edi, 100  
je     .L_case_100  
cmp    edi, 999  
je     .L_case_999  
cmp    edi, 1  
je     .L_case_1  
jmp    .L_default  
```

### Pattern : arbre binaire de comparaisons (cas nombreux et dispersés)

```asm
cmp    edi, 50  
jg     .L_upper_half       ; si x > 50, chercher dans la moitié supérieure  
cmp    edi, 10  
je     .L_case_10  
cmp    edi, 25  
je     .L_case_25  
cmp    edi, 50  
je     .L_case_50  
jmp    .L_default  
.L_upper_half:
cmp    edi, 100  
je     .L_case_100  
; ...
```

### Équivalent C

```c
switch (x) {
    case 0: ...; break;
    case 1: ...; break;
    // ...
}
```

### Explication

GCC choisit la stratégie d'implémentation du `switch` en fonction de la densité et du nombre des `case` :

- **Jump table** : quand les valeurs de `case` sont proches et forment un intervalle dense. C'est le cas le plus efficace (O(1)), reconnaissable par le `lea` vers `.rodata` suivi d'un `jmp` indirect via un index.  
- **Chaîne de comparaisons** : quand il y a peu de `case` (typiquement < 5) ou que les valeurs sont très dispersées. C'est le cas le plus simple à lire.  
- **Arbre binaire** : quand il y a beaucoup de `case` dispersés. GCC génère une série de comparaisons qui partitionne l'espace des valeurs par dichotomie (O(log n)).

**Comment identifier une jump table** : cherchez un `lea` vers `.rodata` suivi de `movsxd` (chargement d'un offset signé 32 bits), `add` et `jmp rax`. La présence d'un `cmp + ja` juste avant (vérification des bornes) confirme le pattern. La taille de la jump table dans `.rodata` (nombre d'entrées × 4 octets) indique le nombre de `case`.

---

## 8 — Appel de fonction et conventions

### Pattern : appel avec ≤ 6 arguments entiers

```asm
mov    edx, 3              ; 3ᵉ argument  
mov    esi, 2              ; 2ᵉ argument  
lea    rdi, [rip+.LC0]     ; 1er argument (adresse de chaîne)  
call   printf@plt  
```

### Pattern : appel variadique (`printf` et famille)

```asm
lea    rdi, [rip+.LC0]     ; format string  
mov    esi, 42             ; premier argument %d  
xor    eax, eax            ; al = 0 (aucun argument flottant)  
call   printf@plt  
```

### Pattern : appel avec arguments flottants

```asm
movsd  xmm0, qword ptr [rip+.LC1]  ; 1er arg double  
mov    edi, 42                       ; 1er arg entier  
mov    eax, 1                        ; al = 1 (un registre xmm utilisé)  
call   mixed_func@plt  
```

### Pattern : sauvegarde de registre caller-saved avant appel

```asm
push   rbx                 ; sauvegarde rbx (callee-saved) dans le prologue  
mov    ebx, edi            ; copie le 1er argument dans rbx  
call   some_func           ; rbx survit au call  
mov    edi, ebx            ; réutilise la valeur sauvegardée  
call   other_func  
pop    rbx                 ; restaure rbx dans l'épilogue  
```

### Explication

Le pattern de sauvegarde dans un registre callee-saved (`rbx`, `r12`–`r15`) est un indice important en RE : il montre qu'une valeur doit survivre à un `call`, ce qui implique qu'elle est réutilisée après. Si vous voyez `mov ebx, edi` en début de fonction, c'est que le premier argument sera nécessaire plus tard. Cela aide à comprendre le flux de données à travers la fonction.

---

## 9 — Prologue et épilogue de fonctions

### Pattern : prologue complet (`-O0`)

```asm
push   rbp  
mov    rbp, rsp  
sub    rsp, 0x20           ; 32 octets de locales  
mov    dword ptr [rbp-0x14], edi   ; sauvegarde du 1er argument sur la pile  
mov    dword ptr [rbp-0x18], esi   ; sauvegarde du 2ᵉ argument  
```

### Pattern : prologue optimisé (`-O2`, sans frame pointer)

```asm
push   rbx                 ; sauvegarde callee-saved  
push   r12                 ; sauvegarde callee-saved  
sub    rsp, 0x18           ; locales + alignement  
```

### Pattern : fonction feuille sans prologue (red zone)

```asm
; Pas de push ni sub rsp !
mov    dword ptr [rsp-0x4], edi   ; stockage direct dans la red zone
; ... calculs ...
ret
```

### Pattern : épilogue avec `leave`

```asm
leave                      ; mov rsp, rbp ; pop rbp  
ret  
```

### Pattern : épilogue optimisé

```asm
add    rsp, 0x18  
pop    r12  
pop    rbx  
ret  
```

### Explication

Le prologue et l'épilogue encadrent chaque fonction. Leur forme donne des informations précieuses :

- **Nombre de `push` callee-saved** → nombre de « variables de registre » utilisées par la fonction (indicateur de complexité)  
- **Taille du `sub rsp`** → espace total pour les variables locales et les arguments de fonctions appelées  
- **Présence de `push rbp` / `mov rbp, rsp`** → frame pointer utilisé (typique de `-O0`, facilite la lecture)  
- **Absence de `sub rsp` avec des accès `[rsp-offset]`** → fonction feuille utilisant la red zone  
- **Ordre des `pop`** → inverse exact de l'ordre des `push` (si ce n'est pas le cas, le binaire est peut-être corrompu ou obfusqué)

---

## 10 — Stack canary (protection contre les buffer overflows)

### Pattern

```asm
; Prologue
mov    rax, qword ptr fs:[0x28]    ; charge le canary depuis le TLS  
mov    qword ptr [rbp-0x8], rax    ; stocke le canary sur la pile  
xor    eax, eax                     ; efface rax (ne pas laisser le canary dans un registre)  

; ... corps de la fonction ...

; Épilogue
mov    rax, qword ptr [rbp-0x8]    ; recharge le canary depuis la pile  
xor    rax, qword ptr fs:[0x28]    ; compare avec la valeur originale  
jne    .L_stack_smash              ; si différent → corruption détectée  
leave  
ret  

.L_stack_smash:
call   __stack_chk_fail@plt        ; ne retourne jamais (abort)
```

### Explication

Ce pattern est généré quand le binaire est compilé avec `-fstack-protector` (activé par défaut dans la plupart des distributions). L'accès à `fs:[0x28]` est le marqueur le plus fiable du stack canary sur la glibc x86-64. Le `xor eax, eax` après le chargement du canary est un nettoyage de sécurité pour ne pas laisser la valeur dans un registre.

En RE, ce pattern vous indique que la fonction manipule un buffer local (c'est ce qui a déclenché la protection). L'appel à `__stack_chk_fail` à la fin de la fonction confirme la présence du canary. Si vous voyez `jne` vers `__stack_chk_fail`, ne confondez pas ce branchement avec une logique métier — c'est purement de la protection.

---

## 11 — Accès aux variables globales en PIE/PIC

### Pattern (variable globale en code PIE)

```asm
lea    rax, [rip+global_var]       ; charge l'adresse de la variable (RIP-relative)  
mov    eax, dword ptr [rax]        ; charge la valeur  
```

Ou, en une seule instruction si la taille est connue :

```asm
mov    eax, dword ptr [rip+global_var]   ; accès RIP-relatif direct
```

### Pattern (chaîne littérale en `.rodata`)

```asm
lea    rdi, [rip+.LC0]            ; adresse de la chaîne dans .rodata  
call   puts@plt  
```

### Explication

En code PIE (*Position-Independent Executable*, défaut sur les distributions modernes), toutes les références à des adresses fixes utilisent l'adressage **RIP-relatif** : l'adresse est exprimée comme un offset par rapport à l'instruction courante (`rip`). C'est ce qui permet au binaire d'être chargé à n'importe quelle adresse (ASLR).

En RE, le pattern `lea reg, [rip+offset]` suivi de `call` indique presque toujours le passage d'une adresse de chaîne ou de variable globale en argument de fonction. `mov reg, [rip+offset]` est le chargement de la *valeur* d'une variable globale. Ghidra et IDA résolvent automatiquement ces offsets en noms de symboles, mais dans `objdump` vous verrez l'offset brut et devrez le calculer manuellement.

---

## 12 — Appel PLT et résolution GOT

### Pattern (appel via PLT)

```asm
call   printf@plt                  ; appel indirect via la PLT
```

Dans la section `.plt`, le stub ressemble à :

```asm
printf@plt:
    jmp    qword ptr [rip+printf@GOTPCREL]  ; saut via la GOT
    push   0x3                               ; index dans la table de relocation
    jmp    .plt_resolve                      ; appel au résolveur dynamique
```

### Pattern (accès GOT en Full RELRO)

```asm
; .plt.got ou .plt.sec (avec CET)
endbr64  
jmp    qword ptr [rip+printf@GOTPCREL]  
```

### Explication

Chaque appel à une fonction de bibliothèque partagée passe par la PLT. Au premier appel, le stub PLT invoque le résolveur dynamique de `ld.so` qui écrit l'adresse réelle de la fonction dans la GOT. Les appels suivants sautent directement à la bonne adresse.

En RE, si vous voyez `call <nom>@plt`, vous savez que c'est un appel à une fonction importée. Le nom après `@plt` identifie la fonction. Sur un binaire strippé, Ghidra et IDA nomment automatiquement ces stubs. Dans `objdump`, vous verrez l'adresse du stub PLT avec le commentaire `<printf@plt>`.

---

## 13 — Expressions booléennes

### Pattern : `return (a == b)`

```asm
cmp    edi, esi  
sete   al               ; al = 1 si edi == esi, 0 sinon  
movzx  eax, al          ; étend à 32 bits (valeur de retour int/bool)  
ret  
```

### Pattern : `return (a < b)` (signé)

```asm
cmp    edi, esi  
setl   al  
movzx  eax, al  
ret  
```

### Pattern : `return (a != 0)` (normalisation booléenne)

```asm
test   edi, edi  
setne  al  
movzx  eax, al  
ret  
```

### Pattern : combinaison `&&` / `||` (court-circuit)

```asm
; if (a > 0 && b < 10)
test   edi, edi  
jle    .L_false          ; a <= 0 → court-circuit, résultat false  
cmp    esi, 10  
jge    .L_false          ; b >= 10 → résultat false  
; ... bloc true ...
```

### Explication

Le triplet `cmp`/`setcc`/`movzx` est le pattern canonique de GCC pour les expressions booléennes retournées ou assignées. `setcc` produit un résultat 8 bits (0 ou 1) dans un registre 8 bits (`al`, `cl`, etc.), et `movzx` l'étend à 32 bits.

Pour les opérateurs `&&` et `||`, GCC implémente l'évaluation court-circuit du C : si le premier opérande de `&&` est faux, le second n'est pas évalué (saut direct au résultat `false`). De même, si le premier opérande de `||` est vrai, le second est ignoré.

---

## 14 — Opérateur ternaire et `min` / `max`

### Pattern : ternaire avec `cmov`

```asm
; result = (a > b) ? a : b    → max(a, b)
cmp    edi, esi  
cmovl  edi, esi          ; si edi < esi, edi = esi  
mov    eax, edi  
ret  
```

### Pattern : `abs()` (valeur absolue)

```asm
; abs(x) pour un int signé
mov    eax, edi  
cdq                       ; edx = signe de eax (0 ou -1)  
xor    eax, edx           ; si négatif : complément à 1  
sub    eax, edx           ; si négatif : +1 (complète le complément à 2)  
ret  
```

### Pattern alternatif `abs()` avec `cmov`

```asm
mov    eax, edi  
neg    eax                ; eax = -edi  
cmovl  eax, edi           ; si le résultat est négatif (edi était positif), restaure edi  
ret  
```

### Explication

Les `cmovcc` sont le signe d'expressions ternaires ou de `min`/`max` en `-O2`. GCC les préfère aux branchements car ils éliminent les erreurs de prédiction de branche.

Le pattern `abs()` via `cdq`/`xor`/`sub` est un idiome classique qui exploite les propriétés du complément à deux : XOR avec -1 donne le complément à 1, et soustraire -1 revient à ajouter 1, ce qui complète la négation.

---

## 15 — Test de bit et manipulation de flags

### Pattern : test d'un bit spécifique

```asm
test   eax, 0x04          ; teste le bit 2  
jnz    .L_bit_set          ; saute si le bit est à 1  
```

### Pattern : extraction de champ de bits

```asm
; unsigned field = (x >> 4) & 0x0F;
mov    eax, edi  
shr    eax, 4  
and    eax, 0x0F  
```

### Pattern : positionnement d'un bit

```asm
; x |= (1 << 3);
or     eax, 0x08
```

### Pattern : effacement d'un bit

```asm
; x &= ~(1 << 3);
and    eax, 0xFFFFFFF7     ; = ~0x08
```

### Explication

Les manipulations de bits sont très courantes dans le code système, réseau et cryptographique. Le pattern `test` + `jnz`/`jz` teste un bit sans modifier la valeur (contrairement à `and` qui modifie la destination). Les masques constants dans `and` et `or` indiquent quels bits sont extraits ou modifiés.

---

## 16 — Accès aux tableaux et indexation

### Pattern : accès à un tableau d'entiers

```asm
mov    eax, dword ptr [rbx+rcx*4]      ; arr[i] (int, 4 octets)  
mov    rax, qword ptr [rbx+rcx*8]      ; arr[i] (long/pointeur, 8 octets)  
movzx  eax, byte ptr [rbx+rcx]         ; arr[i] (char/uint8_t, 1 octet)  
```

### Pattern : accès à un tableau de structures

```asm
; struct S { int a; int b; char c; };  → sizeof(S) = 12 (avec padding)
; accès à arr[i].b
imul   rax, rcx, 12       ; offset = i * sizeof(S) = i * 12  
mov    eax, dword ptr [rbx+rax+4]     ; +4 = offset du champ b dans S  
```

### Pattern : parcours de tableau avec pointeur

```asm
; En -O2, GCC peut transformer for(i=0;i<n;i++) arr[i] en :
mov    rax, rbx            ; ptr = &arr[0]  
lea    rdx, [rbx+rcx*4]   ; end = &arr[n]  
.L_loop:
mov    dword ptr [rax], 0  ; *ptr = 0  
add    rax, 4              ; ptr++  
cmp    rax, rdx            ; ptr < end ?  
jne    .L_loop  
```

### Explication

Le facteur d'échelle dans `[base+index*scale]` révèle la taille des éléments : `*1` = octets/chars, `*2` = shorts, `*4` = ints/floats, `*8` = longs/doubles/pointeurs. Quand le scale factor n'est pas une puissance de 2, GCC utilise un `imul` explicite pour calculer l'offset, ce qui indique une structure de taille non standard.

En `-O2`, GCC transforme souvent les boucles indexées (`arr[i]`) en boucles à pointeur (`*ptr++`) avec une comparaison de pointeur de fin (*pointer-based loop*). C'est plus efficace car cela élimine la multiplication d'index à chaque itération.

---

## 17 — Appels virtuels C++ (vtable dispatch)

### Pattern

```asm
mov    rax, qword ptr [rdi]          ; charge le vptr (premier qword de l'objet)  
call   qword ptr [rax+0x10]          ; appelle la 3ᵉ méthode virtuelle  
```

### Pattern avec vérification `this != nullptr`

```asm
test   rdi, rdi                       ; this == nullptr ?  
je     .L_null_handler  
mov    rax, qword ptr [rdi]           ; vptr  
call   qword ptr [rax+0x08]           ; 2ᵉ méthode virtuelle  
```

### Pattern : dynamic_cast / RTTI

```asm
mov    rsi, qword ptr [rip+typeinfo_for_Derived]  ; RTTI cible  
mov    rdi, rbx                       ; objet source  
call   __dynamic_cast                 ; runtime RTTI check  
test   rax, rax                       ; cast réussi ?  
je     .L_cast_failed  
```

### Explication

L'appel virtuel C++ suit toujours le même schéma en deux étapes : déréférencer `[rdi]` pour obtenir le pointeur de vtable (le vptr est toujours le premier champ d'un objet polymorphe), puis sauter à un offset fixe dans la vtable. L'offset divisé par 8 donne l'index de la méthode virtuelle (0x00 = 1ère, 0x08 = 2ᵉ, 0x10 = 3ᵉ, etc.).

Ce pattern est le marqueur le plus fiable de code C++ orienté objet. Si vous voyez `mov rax, [rdi]; call [rax+offset]`, vous êtes face à un appel virtuel. `rdi` contient `this`, et l'offset dans la vtable identifie la méthode appelée.

---

## 18 — Exceptions C++ (`try` / `catch` / `throw`)

### Pattern : `throw`

```asm
mov    edi, 4                         ; taille de l'objet exception  
call   __cxa_allocate_exception       ; alloue l'objet exception  
mov    dword ptr [rax], 42            ; initialise l'exception (ici un int)  
mov    edx, 0                         ; destructeur (nullptr pour int)  
lea    rsi, [rip+typeinfo_for_int]    ; RTTI du type lancé  
mov    rdi, rax                       ; pointeur vers l'exception  
call   __cxa_throw                    ; lance l'exception (ne retourne pas)  
```

### Pattern : reconnaissance d'un `try`/`catch`

Le code du `try` lui-même n'est pas visible dans les instructions — il est encodé dans la section `.gcc_except_table` sous forme de tables LSDA (*Language Specific Data Area*). En revanche, le code du `catch` est une fonction de *landing pad* appelée par le mécanisme de déroulage de pile :

```asm
.L_landing_pad:
cmp    edx, 1                         ; type de l'exception (index dans la table)  
je     .L_catch_block  
call   _Unwind_Resume                 ; re-lance si ce n'est pas le bon type  

.L_catch_block:
mov    rdi, rax                       ; pointeur vers l'objet exception  
call   __cxa_begin_catch  
; ... code du catch ...
call   __cxa_end_catch
```

### Explication

Les fonctions `__cxa_allocate_exception`, `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch` et `_Unwind_Resume` sont les marqueurs absolus du mécanisme d'exceptions C++. Si vous voyez ces appels dans un binaire, vous savez qu'il utilise des exceptions. La présence de `__cxa_throw` indique un `throw`, `__cxa_begin_catch`/`__cxa_end_catch` encadrent un bloc `catch`, et `_Unwind_Resume` propage une exception non attrapée vers le frame supérieur.

---

## 19 — `std::string` (libstdc++ GCC)

### Pattern : SSO (Small String Optimization) — chaîne courte

```asm
; std::string s = "hello";  (5 chars + nul → tient dans le buffer SSO)
lea    rdi, [rbp-0x20]               ; adresse du std::string sur la pile  
lea    rsi, [rip+.LC0]               ; "hello"  
call   std::string::basic_string(char const*)  
```

### Layout mémoire (libstdc++)

```
; std::string sur la pile (sizeof = 32 octets)
[rbp-0x20]  → pointeur vers les données (ou vers le buffer interne si SSO)
[rbp-0x18]  → taille (length)
[rbp-0x10]  → capacité (si alloué dynamiquement) ou début du buffer SSO
```

### Pattern : accès à `s.size()` et `s.data()`

```asm
mov    rax, qword ptr [rbx+0x08]     ; s.size() = 2ᵉ qword  
mov    rdi, qword ptr [rbx]          ; s.data() = 1er qword (pointeur)  
```

### Explication

`std::string` de libstdc++ (GCC) utilise le *Small String Optimization* : les chaînes de 15 octets ou moins sont stockées directement dans l'objet `string` lui-même (pas d'allocation heap). Au-delà de 15 octets, un buffer est alloué sur le heap. L'objet fait toujours 32 octets sur x86-64.

En RE, un `std::string` apparaît comme une structure de 32 octets avec trois champs de 8 octets. Le premier est un pointeur vers les données, le deuxième est la longueur. Reconnaître ce layout permet d'identifier les manipulations de chaînes C++ dans le code désassemblé.

---

## 20 — Construction et destruction d'objets C++

### Pattern : `new` et `delete`

```asm
; MyClass* p = new MyClass(42);
mov    edi, 24                        ; sizeof(MyClass) = 24  
call   operator new(unsigned long)    ; alloue sur le heap  
mov    rbx, rax                       ; sauvegarde le pointeur  
mov    esi, 42                        ; argument du constructeur  
mov    rdi, rax                       ; this = pointeur alloué  
call   MyClass::MyClass(int)          ; constructeur  

; delete p;
mov    rdi, rbx                       ; this  
call   MyClass::~MyClass()            ; destructeur  
mov    esi, 24                        ; taille (pour le désallocateur)  
mov    rdi, rbx  
call   operator delete(void*, unsigned long)  
```

### Pattern : objet local avec destructeur (RAII)

```asm
; { std::vector<int> v; ... }
; Le destructeur est appelé automatiquement en fin de scope
lea    rdi, [rbp-0x20]               ; this = adresse de v sur la pile  
call   std::vector<int>::~vector()    ; destructeur en fin de scope  
```

### Explication

Le pair `operator new` / constructeur et destructeur / `operator delete` est le schéma fondamental de l'allocation C++. La taille passée à `operator new` (premier argument, dans `edi`) donne directement le `sizeof` de la classe — information précieuse pour reconstruire la structure de la classe.

Les destructeurs d'objets locaux sont appelés systématiquement avant le `ret` ou avant chaque `return` anticipé. Si vous voyez plusieurs appels à un même destructeur dans une fonction, cela correspond aux différents chemins de sortie (chaque `return` appelle les destructeurs des objets locaux en scope).

---

## 21 — Reconnaissance des fonctions standard inlinées

GCC inline automatiquement certaines fonctions de la libc pour les cas simples. Les reconnaître évite de perdre du temps à analyser du code « mystérieux ».

### `memset` inliné (petite taille connue)

```asm
; memset(buf, 0, 32) → mise à zéro de 32 octets
pxor   xmm0, xmm0                    ; xmm0 = 0  
movaps xmmword ptr [rbp-0x30], xmm0  ; 16 octets à 0  
movaps xmmword ptr [rbp-0x20], xmm0  ; 16 octets à 0  
```

Ou avec `rep stosb` :

```asm
xor    eax, eax           ; valeur à écrire = 0  
mov    ecx, 32            ; nombre d'octets  
lea    rdi, [rbp-0x30]    ; destination  
rep    stosb               ; remplit 32 octets avec 0  
```

### `memcpy` inliné

```asm
; memcpy(dst, src, 24) → copie de 24 octets
movdqu xmm0, xmmword ptr [rsi]       ; charge 16 octets de src  
movdqu xmmword ptr [rdi], xmm0       ; écrit 16 octets à dst  
mov    rax, qword ptr [rsi+0x10]     ; charge les 8 octets restants  
mov    qword ptr [rdi+0x10], rax     ; écrit les 8 octets  
```

### `strlen` inliné (rare, cas très simple)

```asm
; strlen de chaîne connue à la compilation
mov    eax, 5              ; GCC calcule strlen("hello") à la compilation
```

### Explication

Quand la taille est connue à la compilation et est petite (typiquement ≤ 64 octets), GCC remplace `memset`, `memcpy`, `memmove` et parfois `strcmp` par des séquences d'instructions inline. Pour les tailles plus grandes, il émet un appel à la fonction libc via PLT. Pour `strlen` sur une chaîne littérale, GCC calcule le résultat entièrement à la compilation et le remplace par une constante.

---

## 22 — Code vectorisé (auto-vectorisation GCC)

### Pattern : boucle scalaire suivie du « tail »

```asm
; Boucle vectorisée (traite 4 éléments par itération)
.L_vector_loop:
movdqu xmm0, xmmword ptr [rdi+rax]   ; charge 4 ints  
paddd  xmm0, xmm1                     ; addition packed  
movdqu xmmword ptr [rsi+rax], xmm0   ; stocke 4 résultats  
add    rax, 16  
cmp    rax, rdx  
jl     .L_vector_loop  

; Tail : traite les éléments restants un par un
.L_scalar_tail:
mov    ecx, dword ptr [rdi+rax]       ; charge 1 int  
add    ecx, ebx                        ; addition scalaire  
mov    dword ptr [rsi+rax], ecx       ; stocke 1 résultat  
add    rax, 4  
cmp    rax, r8  
jl     .L_scalar_tail  
```

### Explication

Quand GCC vectorise une boucle (`-ftree-vectorize`, activé par défaut à `-O2`), il génère deux versions de la boucle : une version SIMD qui traite N éléments en parallèle (N = 4 pour des `int` dans des registres XMM 128 bits), et une version scalaire (*tail*) qui traite les éléments restants quand le nombre total n'est pas un multiple de N.

La version scalaire (tail) est beaucoup plus lisible et contient la même logique que la boucle originale en C. En RE, analysez toujours le tail en premier pour comprendre ce que fait la boucle, puis vérifiez que la version SIMD fait la même chose en parallèle.

---

## 23 — Idiomes divers

### `likely` / `unlikely` (prédiction de branche)

```c
if (__builtin_expect(x == 0, 0)) { error(); }
```

```asm
test   edi, edi  
jne    .L_continue          ; le chemin "normal" est le fall-through  
; bloc d'erreur (placé après le jmp, dans le chemin "froid")
call   error
.L_continue:
```

GCC place le chemin « probable » en fall-through (séquentiel) et le chemin « improbable » après un saut. C'est une optimisation de layout pour le cache d'instructions.

### `__builtin_unreachable()`

```asm
ud2                         ; instruction invalide → crash si atteint
```

GCC insère `ud2` après les points théoriquement inatteignables. En RE, `ud2` signifie « le compilateur a déterminé que ce point ne devrait jamais être atteint ».

### Retour de structure par valeur (hidden pointer)

```asm
; struct BigStruct func();
; L'appelant prépare l'espace et passe le pointeur en rdi :
lea    rdi, [rsp+0x10]     ; espace pour le retour  
call   func                 ; func écrit dans [rdi] et retourne rdi dans rax  
```

Quand le premier argument visible en C devrait être dans `rdi` mais que `rdi` contient un pointeur local de l'appelant, c'est un retour de structure via pointeur caché (voir Annexe B, §4.4).

---

> 📚 **Pour aller plus loin** :  
> - **Annexe A** — [Référence rapide des opcodes x86-64](/annexes/annexe-a-opcodes-x86-64.md) — les instructions individuelles utilisées dans ces patterns.  
> - **Annexe B** — [Conventions d'appel System V AMD64 ABI](/annexes/annexe-b-system-v-abi.md) — la convention qui sous-tend les patterns d'appel et de prologue/épilogue.  
> - **Chapitre 16** — [Comprendre les optimisations du compilateur](/16-optimisations-compilateur/README.md) — couverture pédagogique des transformations GCC qui produisent ces patterns.  
> - **Chapitre 17** — [Reverse Engineering du C++ avec GCC](/17-re-cpp-gcc/README.md) — détail des patterns C++ (vtables, RTTI, exceptions, STL).  
> - **Compiler Explorer** — [https://godbolt.org/](https://godbolt.org/) — outil en ligne indispensable pour observer le code assembleur produit par GCC pour n'importe quel fragment C/C++ en temps réel.

⏭️ [Constantes magiques crypto courantes (AES, SHA, MD5, RC4…)](/annexes/annexe-j-constantes-crypto.md)
