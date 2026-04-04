🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 7 — Corrigé du Checkpoint

## Désassembler `keygenme_O0` et `keygenme_O2`, lister les différences clés

> ⚠️ **Spoilers** — Ce fichier contient le corrigé complet du checkpoint du chapitre 7.  
> Les adresses exactes peuvent varier selon votre version de GCC, votre distribution et vos options de liaison. Ce qui compte, ce sont les **observations de fond**, pas les valeurs numériques précises.

---

## 1. Triage initial

### Taille des fichiers

```bash
$ ls -l keygenme_O0 keygenme_O2
-rwxr-xr-x 1 user user  20536  keygenme_O0
-rwxr-xr-x 1 user user  16432  keygenme_O2
```

Le binaire `-O0` est environ 25 % plus volumineux. L'écart provient principalement de la section `.text` plus grande et des sections de débogage si `-g` est inclus.

### Taille de la section `.text`

```bash
$ readelf -S keygenme_O0 | grep '\.text'
  [16] .text             PROGBITS   0000000000001060  00001060
       00000000000001c5  0000000000000000  AX       0     0     16

$ readelf -S keygenme_O2 | grep '\.text'
  [16] .text             PROGBITS   0000000000001060  00001060
       0000000000000142  0000000000000000  AX       0     0     16
```

La section `.text` passe d'environ **0x1c5 (453) octets** en `-O0` à environ **0x142 (322) octets** en `-O2`, soit une réduction d'environ 30 %. Le code optimisé est plus compact.

### Présence des symboles

```bash
$ file keygenme_O0
keygenme_O0: ELF 64-bit LSB pie executable, x86-64, [...], with debug_info, not stripped

$ file keygenme_O2
keygenme_O2: ELF 64-bit LSB pie executable, x86-64, [...], not stripped
```

Les deux binaires conservent leurs symboles (non strippés). Le binaire `-O0` contient en plus les informations de débogage DWARF (`with debug_info`), ce qui est attendu si le `Makefile` utilise `-g` avec `-O0`.

### Nombre approximatif de fonctions utilisateur

```bash
$ nm keygenme_O0 | grep ' T ' | grep -v '_'
0000000000001139 T compute_hash
0000000000001189 T check_serial
00000000000011e2 T main

$ nm keygenme_O2 | grep ' T ' | grep -v '_'
0000000000001060 T compute_hash
0000000000001090 T check_serial
00000000000010e0 T main
```

Les deux binaires contiennent les **3 mêmes fonctions utilisateur** : `compute_hash`, `check_serial`, et `main`. Aucune fonction n'a été inlinée en `-O2` dans ce cas (elles sont toutes les trois toujours présentes comme symboles distincts). Cela peut s'expliquer par le fait que les fonctions sont suffisamment grandes ou qu'elles sont appelées depuis plusieurs endroits, ce qui décourage l'inlining même en `-O2`.

> 💡 **Note** : selon la version de GCC et la taille réelle des fonctions, `compute_hash` pourrait être inlinée dans `check_serial` en `-O2`. Si c'est votre cas, vous ne verrez que 2 fonctions utilisateur — c'est une observation valide à documenter.

---

## 2. Identification de `main()`

### Par les symboles (méthode directe)

Les deux binaires n'étant pas strippés, la localisation est immédiate :

```bash
$ objdump -d -M intel keygenme_O0 | grep '<main>:'
00000000000011e2 <main>:

$ objdump -d -M intel keygenme_O2 | grep '<main>:'
00000000000010e0 <main>:
```

### Par `_start` (vérification de la méthode strippée)

Pour s'entraîner, vérifions via le point d'entrée :

```bash
$ readelf -h keygenme_O0 | grep "Entry point"
  Entry point address:               0x1060
```

En désassemblant `_start` :

```bash
$ objdump -d -M intel --start-address=0x1060 --stop-address=0x1090 keygenme_O0
```

On trouve le `lea rdi, [rip+0x163]` à l'adresse `0x1078` (7 octets). Instruction suivante à `0x107f`. Adresse de `main` = `0x107f + 0x163` = **`0x11e2`** — cohérent avec le symbole.

---

## 3. Comparaison des prologues et épilogues

### `compute_hash`

**En `-O0` :**

```asm
0000000000001139 <compute_hash>:
    1139:       f3 0f 1e fa             endbr64
    113d:       55                      push   rbp
    113e:       48 89 e5                mov    rbp, rsp
    1141:       48 89 7d e8             mov    QWORD PTR [rbp-0x18], rdi
    1145:       c7 45 fc 00 00 00 00    mov    DWORD PTR [rbp-0x4], 0x0
    114c:       c7 45 f8 00 00 00 00    mov    DWORD PTR [rbp-0x8], 0x0
    ...
    1186:       c9                      leave
    1187:       c3                      ret
```

- Prologue complet : `endbr64` + `push rbp` + `mov rbp, rsp`.  
- Pas de `sub rsp` explicite (GCC juge que l'espace sous `rbp` suffit sans ajustement, les variables locales tiennent dans la *red zone* ou l'espace est implicitement réservé).  
- Le paramètre `rdi` est immédiatement sauvegardé sur la pile à `[rbp-0x18]`.  
- Deux variables locales initialisées à 0 : `[rbp-0x4]` (hash) et `[rbp-0x8]` (i).  
- Épilogue : `leave` + `ret`.

**En `-O2` :**

```asm
0000000000001060 <compute_hash>:
    1060:       f3 0f 1e fa             endbr64
    1064:       0f b6 07                movzx  eax, BYTE PTR [rdi]
    1067:       84 c0                   test   al, al
    1069:       74 1b                   je     1086 <compute_hash+0x26>
    ...
    1086:       c3                      ret
```

- **Pas de prologue classique.** Ni `push rbp`, ni `mov rbp, rsp`, ni allocation de pile.  
- Le frame pointer est omis (`-fomit-frame-pointer` actif par défaut en `-O2`).  
- Le paramètre `rdi` n'est **pas** sauvegardé sur la pile — il est utilisé directement dans les registres.  
- La fonction commence immédiatement par le code utile (chargement du premier octet de la chaîne).  
- Épilogue : un simple `ret`, sans `leave` ni `pop rbp`.

> **Observation clé** : le prologue passe de 5 instructions (18 octets) en `-O0` à 0 instruction en `-O2`. L'épilogue passe de 2 instructions à 1.

### `check_serial`

**En `-O0` :**

```asm
0000000000001189 <check_serial>:
    1189:       f3 0f 1e fa             endbr64
    118d:       55                      push   rbp
    118e:       48 89 e5                mov    rbp, rsp
    1191:       48 83 ec 40             sub    rsp, 0x40
    1195:       48 89 7d c8             mov    QWORD PTR [rbp-0x38], rdi
    1199:       48 89 75 c0             mov    QWORD PTR [rbp-0x40], rsi
    ...
    11df:       c9                      leave
    11e0:       c3                      ret
```

- Prologue complet avec `sub rsp, 0x40` (64 octets alloués) — la fonction utilise un buffer local (probablement pour `sprintf`).  
- Les deux paramètres (`rdi` et `rsi`) sont sauvegardés sur la pile.  
- Épilogue : `leave` + `ret`.

**En `-O2` :**

```asm
0000000000001090 <check_serial>:
    1090:       f3 0f 1e fa             endbr64
    1094:       53                      push   rbx
    1095:       48 89 f3                mov    rbx, rsi
    1098:       48 83 ec 30             sub    rsp, 0x30
    ...
    10dc:       48 83 c4 30             add    rsp, 0x30
    10e0:       5b                      pop    rbx
    10e1:       c3                      ret
```

- **Pas de frame pointer** (`rbp` n'est pas sauvegardé ni utilisé).  
- Sauvegarde de `rbx` (callee-saved) car la fonction a besoin d'un registre stable à travers les `call` internes — `rsi` (2ème paramètre) est copié dans `rbx` pour survivre aux appels.  
- Allocation de pile réduite à `0x30` (48 octets) au lieu de `0x40` (64 octets) — moins de padding nécessaire sans le frame pointer.  
- Accès aux variables locales via `[rsp+N]` au lieu de `[rbp-N]`.  
- Épilogue : `add rsp, 0x30` + `pop rbx` + `ret` (pas de `leave`).

### `main`

**En `-O0` :**

- Prologue complet : `endbr64` + `push rbp` + `mov rbp, rsp` + `sub rsp, 0x10`.  
- Sauvegarde de `edi` (argc) et `rsi` (argv) sur la pile.  
- Épilogue : `leave` + `ret`.

**En `-O2` :**

- Prologue minimal ou absent selon que `main` appelle d'autres fonctions (si oui, probable `sub rsp, 0x8` pour l'alignement avant le `call`).  
- Pas de sauvegarde d'`argc`/`argv` sur la pile — utilisation directe des registres.  
- Épilogue : `add rsp` (si allocation) + `ret`.

---

## 4. Différences dans le corps des fonctions

### Focus sur `compute_hash`

C'est la fonction la plus instructive pour observer les optimisations.

#### Accès aux variables : pile vs registres

**`-O0`** — Toutes les variables locales vivent sur la pile :

| Variable | Emplacement `-O0` | Emplacement `-O2` |  
|---|---|---|  
| `input` (paramètre) | `[rbp-0x18]` (copie de `rdi`) | `rdi` (reste dans le registre) |  
| `hash` | `[rbp-0x4]` | `edx` ou `eax` |  
| `i` (compteur) | `[rbp-0x8]` | `rcx` ou déduit de l'avancement du pointeur |

En `-O2`, aucune variable locale ne touche la pile. Tout reste dans les registres.

#### Nombre d'instructions

```bash
$ objdump -d -M intel keygenme_O0 | sed -n '/<compute_hash>:/,/^$/p' | grep '^ ' | wc -l
22

$ objdump -d -M intel keygenme_O2 | sed -n '/<compute_hash>:/,/^$/p' | grep '^ ' | wc -l
13
```

La fonction passe d'environ **22 instructions** en `-O0` à environ **13 instructions** en `-O2` — une réduction de ~40 %.

#### Optimisations concrètes observées

**Optimisation 1 : élimination des store-load inutiles.**

En `-O0`, chaque opération suit le pattern « charger depuis la pile → opérer → ranger sur la pile » :

```asm
; -O0 : hash += (int)input[i]
mov    eax, DWORD PTR [rbp-0x8]       ; charge i  
cdqe  
add    rax, QWORD PTR [rbp-0x18]      ; calcule &input[i]  
movzx  eax, BYTE PTR [rax]            ; charge input[i]  
movsx  eax, al                         ; extension signe  
add    DWORD PTR [rbp-0x4], eax        ; hash += ... (écriture mémoire)  
```

En `-O2`, la même opération :

```asm
; -O2 : hash += (int)input[i]
movsx  eax, BYTE PTR [rdi+rcx]        ; charge input[i] directement (rdi=input, rcx=i)  
add    edx, eax                        ; hash += ... (edx = hash, tout en registres)  
```

Six instructions réduites à deux. Les accès mémoire intermédiaires à la pile ont totalement disparu.

**Optimisation 2 : strength reduction sur la multiplication.**

Si `compute_hash` contient une opération `hash = hash * 8` (ou `hash <<= 3`) :

```asm
; -O0 : multiplication explicite ou shift via la pile
mov    eax, DWORD PTR [rbp-0x4]       ; charge hash  
shl    eax, 3                          ; hash *= 8  
mov    DWORD PTR [rbp-0x4], eax        ; range hash  

; -O2 : shift directement sur le registre
shl    edx, 3                          ; une seule instruction, edx = hash
```

Trois instructions (load-shift-store) deviennent une seule.

**Optimisation 3 : restructuration de la boucle.**

En `-O0`, la boucle `for` suit le pattern canonique avec un saut initial vers le test :

```asm
; -O0
    mov    DWORD PTR [rbp-0x8], 0x0    ; i = 0
    jmp    .test                        ; saut vers le test en fin de boucle
.body:
    ; ... corps ...
    add    DWORD PTR [rbp-0x8], 0x1    ; i++
.test:
    ; charger input[i], comparer avec 0
    jne    .body                        ; si != '\0', continuer
```

En `-O2`, GCC réorganise souvent en *do-while* avec un test de pré-entrée :

```asm
; -O2
    movzx  eax, BYTE PTR [rdi]         ; charger input[0]
    test   al, al                       ; chaîne vide ?
    je     .end                         ; si oui, sauter toute la boucle
.loop:
    ; ... corps (tout en registres) ...
    movzx  eax, BYTE PTR [rdi+rcx]     ; charger le caractère suivant
    test   al, al
    jne    .loop                        ; continuer si != '\0'
.end:
```

Le test est maintenant en **fin de boucle** (structure `do-while`), ce qui économise un saut par rapport au pattern `for` avec test initial. Le compteur `i` peut être remplacé par un incrément direct du pointeur (`rdi+rcx` avec `rcx` incrémenté, ou `rdi` incrémenté directement).

**Optimisation 4 (si observable) : élimination du compteur `i`.**

En `-O2`, GCC peut supprimer la variable `i` et utiliser à la place un **pointeur qui avance** :

```asm
; Au lieu de i++ et input[i], le compilateur fait :
    inc    rdi                          ; pointeur input++
    movzx  eax, BYTE PTR [rdi]         ; charge *input
```

Le compteur `i` n'existe plus en tant que variable — il est implicitement encodé dans la position du pointeur. C'est une optimisation de *strength reduction* appliquée à l'indexation de tableau.

### Focus sur `check_serial`

Observations notables :

- En `-O0`, l'appel à `compute_hash` passe le paramètre via la pile (chargement de `[rbp-0x38]` dans `rdi` avant le `call`). En `-O2`, le paramètre est déjà dans le bon registre ou est transféré directement entre registres.  
- L'appel à `sprintf` et `strcmp` est présent dans les deux versions (ces fonctions de la libc ne sont pas inlinées).  
- En `-O2`, la valeur de retour de `strcmp` peut être directement propagée comme valeur de retour de `check_serial` sans stockage intermédiaire, alors qu'en `-O0`, le résultat est stocké sur la pile puis rechargé dans `eax` avant le `ret`.

---

## 5. Appels de fonctions et PLT

```bash
$ objdump -d -M intel keygenme_O0 | grep 'call' | grep 'plt' | sort -u
    call   1030 <puts@plt>
    call   1040 <printf@plt>
    call   1050 <sprintf@plt>
    call   1060 <strcmp@plt>

$ objdump -d -M intel keygenme_O2 | grep 'call' | grep 'plt' | sort -u
    call   1030 <puts@plt>
    call   1040 <printf@plt>
    call   1050 <sprintf@plt>
    call   1060 <strcmp@plt>
```

Les mêmes fonctions de la libc sont appelées dans les deux versions. L'optimisation n'a pas modifié les dépendances externes — ce qui est logique, car ces fonctions proviennent de bibliothèques partagées et ne sont pas candidates à l'inlining.

Appels internes :

```bash
$ objdump -d -M intel keygenme_O0 | grep 'call' | grep -v 'plt'
    call   1139 <compute_hash>
    call   1189 <check_serial>

$ objdump -d -M intel keygenme_O2 | grep 'call' | grep -v 'plt'
    call   1060 <compute_hash>
    call   1090 <check_serial>
```

Les deux appels internes (`main` → `check_serial` → `compute_hash`) sont préservés dans les deux versions. `compute_hash` n'a pas été inlinée dans `check_serial` en `-O2`. Les adresses diffèrent (le code optimisé est plus compact, les fonctions sont placées à des offsets différents), mais la structure d'appel est identique.

> **Note** : si votre version de GCC a inliné `compute_hash` dans `check_serial` en `-O2`, vous ne verrez pas le `call compute_hash` dans la version optimisée. C'est un résultat tout aussi valide — documentez-le comme une optimisation d'inlining.

---

## 6. Synthèse

Le tableau suivant résume les différences clés :

| Critère | `-O0` | `-O2` |  
|---|---|---|  
| Taille de `.text` | ~453 octets | ~322 octets (−30 %) |  
| Prologue de `compute_hash` | `endbr64` + `push rbp` + `mov rbp,rsp` | `endbr64` uniquement (pas de frame) |  
| Variables locales | Sur la pile (`[rbp-N]`) | Dans les registres |  
| Instructions dans `compute_hash` | ~22 | ~13 (−40 %) |  
| Structure de boucle | `for` avec saut initial vers le test | `do-while` avec pré-test |  
| Accès aux paramètres | Copiés sur la pile dès l'entrée | Restent dans `rdi`, `rsi` |  
| Épilogue | `leave` + `ret` | `ret` (ou `pop` + `ret`) |  
| Appels PLT | Identiques | Identiques |  
| Appels internes | `main` → `check_serial` → `compute_hash` | Idem (ou `compute_hash` inliné) |

**Difficultés supplémentaires en `-O2` sans la version `-O0` :**

Si l'on recevait uniquement le binaire `-O2` sans jamais avoir vu la version `-O0`, les principales difficultés seraient :

- **Identifier les frontières de fonctions** sur un binaire strippé serait plus difficile car le prologue `push rbp` / `mov rbp, rsp` est absent. Il faudrait s'appuyer sur `endbr64`, les cibles de `call`, et les `ret` pour délimiter les fonctions.

- **Reconstituer les variables locales** demanderait de tracer les registres au lieu de simplement lister les accès `[rbp-N]`. Un même registre peut être réutilisé pour des variables différentes au cours de la fonction, rendant l'analyse plus ambiguë.

- **Comprendre la boucle** serait moins immédiat : la structure `do-while` optimisée avec pré-test n'a pas la symétrie familière du `for` canonique. Le compteur `i` ayant potentiellement disparu (remplacé par un incrément de pointeur), il faut reconstituer la sémantique de la boucle à partir du pattern d'accès mémoire.

- **Relier le code au source mental** serait globalement plus lent. En `-O0`, on peut presque lire l'assembleur comme du C. En `-O2`, il faut d'abord comprendre les blocs logiques (« cette séquence calcule un hash en itérant sur une chaîne »), puis seulement reconstruire le pseudo-code. Le processus est inversé : en `-O0`, on lit instruction par instruction et le sens émerge ; en `-O2`, il faut d'abord saisir l'intention globale pour interpréter les instructions individuelles.

Cela dit, le binaire `-O2` reste tout à fait analysable avec les techniques vues dans ce chapitre. Les appels PLT (`strcmp`, `sprintf`) donnent des points d'ancrage sémantiques forts, les chaînes dans `.rodata` révèlent les messages du programme, et la structure d'appel `main` → `check_serial` → `compute_hash` (si préservée) segmente l'analyse en blocs gérables. Le passage à un outil comme Ghidra (chapitre 8) rendrait l'analyse de la version `-O2` significativement plus confortable, notamment grâce au décompilateur et au graphe de flux.

⏭️
