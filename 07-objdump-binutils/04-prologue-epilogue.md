🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 7.4 — Lecture du prologue/épilogue de fonctions en pratique

> 🔧 **Outils utilisés** : `objdump`, `readelf`  
> 📦 **Binaires** : `keygenme_O0`, `keygenme_O2`, `keygenme_strip` (répertoire `binaries/ch07-keygenme/`)  
> 📝 **Syntaxe** : Intel (via `-M intel`)

---

## Le prologue et l'épilogue : les bornes de chaque fonction

Le chapitre 3 (section 3.5) a introduit la théorie du prologue et de l'épilogue dans le cadre de la convention d'appel System V AMD64. Cette section passe à la pratique : nous allons lire ces séquences **directement dans un listing `objdump`**, apprendre à les repérer instantanément, comprendre leurs variantes selon le niveau d'optimisation, et surtout les utiliser comme outil de navigation dans un binaire — en particulier quand les symboles sont absents.

En RE, le prologue et l'épilogue remplissent deux rôles essentiels :

- **Délimiter les fonctions.** C'est le rôle le plus immédiat. Un prologue marque le début d'une fonction, un épilogue marque sa fin. Sur un binaire strippé, c'est votre premier moyen de segmenter le flux d'instructions en unités logiques.  
- **Révéler la structure interne de la fonction.** La taille de l'allocation sur la pile vous indique combien d'espace est réservé pour les variables locales. La sauvegarde de registres *callee-saved* vous dit quels registres la fonction utilise. La présence ou l'absence du frame pointer vous informe sur le niveau d'optimisation.

---

## Le prologue classique en `-O0`

Commençons par le cas le plus courant et le plus lisible : une fonction compilée sans optimisation.

```bash
objdump -d -M intel keygenme_O0 | less
```

Cherchez le début de n'importe quelle fonction. Vous trouverez systématiquement cette séquence :

```asm
push   rbp                    ; (1) Sauvegarder l'ancien frame pointer  
mov    rbp, rsp               ; (2) Établir le nouveau frame pointer  
sub    rsp, 0x20              ; (3) Allouer de l'espace pour les variables locales  
```

Décortiquons chaque instruction.

### Instruction (1) : `push rbp`

Avant d'établir son propre cadre de pile (*stack frame*), la fonction sauvegarde le frame pointer de la fonction appelante. C'est un contrat : quand la fonction se terminera, elle restaurera `rbp` à sa valeur d'origine, pour que l'appelant retrouve son propre cadre intact.

Après cette instruction, `rsp` a diminué de 8 (la taille d'un registre 64 bits) et l'ancienne valeur de `rbp` se trouve au sommet de la pile.

### Instruction (2) : `mov rbp, rsp`

Le frame pointer pointe maintenant vers le sommet actuel de la pile. À partir de cet instant, `rbp` est « ancré » : il ne bougera plus pendant toute l'exécution de la fonction, même si `rsp` fluctue (appels de fonctions, allocations dynamiques). Toutes les variables locales seront accessibles via des déplacements négatifs par rapport à `rbp` (`[rbp-0x4]`, `[rbp-0x8]`, etc.), et les arguments passés par la pile (s'il y en a) via des déplacements positifs (`[rbp+0x10]`, `[rbp+0x18]`, etc.).

### Instruction (3) : `sub rsp, N`

Le pointeur de pile descend de `N` octets pour réserver l'espace des variables locales. La valeur de `N` vous donne une indication précieuse : une fonction avec `sub rsp, 0x80` (128 octets) a probablement beaucoup de variables locales ou un tableau sur la pile, tandis qu'une fonction avec `sub rsp, 0x10` (16 octets) n'en a que quelques-unes.

Cette instruction est parfois absente pour les fonctions *leaf* (fonctions qui n'appellent aucune autre fonction) très simples, car elles peuvent travailler entièrement dans les registres sans toucher à la pile au-delà de la sauvegarde de `rbp`.

> 💡 **Alignement** : la convention System V AMD64 exige que `rsp` soit aligné sur 16 octets **avant** chaque instruction `call`. GCC ajuste la valeur de `sub rsp, N` pour garantir cet alignement. Vous verrez parfois des allocations qui semblent plus grandes que nécessaire — c'est du padding d'alignement, pas des variables cachées.

### Variante avec sauvegarde de registres callee-saved

Quand la fonction a besoin d'utiliser des registres *callee-saved* (`rbx`, `r12`–`r15`), elle les sauvegarde dans le prologue :

```asm
push   rbp  
mov    rbp, rsp  
push   rbx                    ; sauvegarde de rbx (callee-saved)  
push   r12                    ; sauvegarde de r12 (callee-saved)  
sub    rsp, 0x18              ; allocation des variables locales  
```

L'ordre est significatif : les `push` de registres callee-saved viennent **après** l'établissement du frame pointer et **avant** l'allocation des variables locales. Chaque `push` supplémentaire décale `rsp` de 8 octets, et GCC en tient compte dans le calcul du `sub rsp`.

En RE, compter les `push` de registres callee-saved vous dit combien de registres « lourds » la fonction utilise en interne. Une fonction qui sauvegarde `rbx`, `r12`, `r13` et `r14` est probablement complexe, avec de nombreuses valeurs à conserver à travers des appels de sous-fonctions.

---

## L'épilogue classique en `-O0`

L'épilogue est le miroir exact du prologue. Il défait tout ce que le prologue a construit, dans l'ordre inverse :

```asm
leave                          ; (1) Restaurer rsp et rbp  
ret                            ; (2) Retourner à l'appelant  
```

### Instruction (1) : `leave`

L'instruction `leave` est un raccourci pour deux opérations :

```asm
mov    rsp, rbp                ; rsp revient au niveau du frame pointer  
pop    rbp                     ; restaure l'ancien frame pointer  
```

Elle annule d'un coup l'allocation des variables locales (`sub rsp, N`) et la sauvegarde du frame pointer (`push rbp`). Après `leave`, la pile est dans l'état exact où elle était juste après le `call` qui a invoqué la fonction — c'est-à-dire que le sommet de la pile contient l'adresse de retour.

### Instruction (2) : `ret`

`ret` dépile l'adresse de retour (placée par le `call` de l'appelant) et saute à cette adresse. L'exécution reprend dans la fonction appelante, juste après le `call`.

### Variante avec restauration de registres callee-saved

Si le prologue a sauvegardé des registres, l'épilogue les restaure dans l'**ordre inverse** :

```asm
; Épilogue avec restauration
add    rsp, 0x18              ; libérer les variables locales  
pop    r12                     ; restaurer r12 (inverse du push r12)  
pop    rbx                     ; restaurer rbx (inverse du push rbx)  
pop    rbp                     ; restaurer rbp (inverse du push rbp)  
ret  
```

Notez que dans ce cas, `leave` n'est **pas** utilisé. GCC génère explicitement le `add rsp` suivi des `pop` dans l'ordre inverse des `push` du prologue. La pile est une structure LIFO — le dernier registre sauvegardé est le premier restauré.

### Variante sans `leave` : `pop rbp` + `ret`

Parfois, au lieu de `leave`, GCC génère la séquence équivalente décomposée :

```asm
pop    rbp  
ret  
```

C'est fonctionnellement identique à `leave` + `ret` quand il n'y a pas eu d'allocation `sub rsp` (la pile est déjà au bon niveau). Le processeur les exécute de manière équivalente.

---

## La disposition du stack frame en mémoire

Pour bien lire un prologue, il faut avoir en tête la disposition du cadre de pile après son établissement. Voici ce à quoi ressemble la pile après un prologue complet :

```
Adresses hautes (début de pile)
┌──────────────────────────────────┐
│  Arguments passés par la pile    │  [rbp+0x18], [rbp+0x10]
│  (7ème argument et suivants)     │  (si > 6 arguments)
├──────────────────────────────────┤
│  Adresse de retour               │  [rbp+0x8]
│  (empilée par l'instruction      │
│  call de l'appelant)             │
├──────────────────────────────────┤
│  Ancien rbp sauvegardé           │  [rbp+0x0]  ← rbp pointe ici
├──────────────────────────────────┤
│  Registres callee-saved          │  [rbp-0x8], [rbp-0x10]…
│  (rbx, r12, r13…)                │
├──────────────────────────────────┤
│  Variables locales               │  [rbp-0x14], [rbp-0x18]…
│  (int, char[], pointeurs…)       │
├──────────────────────────────────┤
│  Zone d'alignement / padding     │
├──────────────────────────────────┤
│  (espace libre)                  │  ← rsp pointe ici
└──────────────────────────────────┘
Adresses basses (sommet de pile)
```

Ce schéma est la clé pour interpréter les accès mémoire dans le corps de la fonction :

- `[rbp-0x4]` → première variable locale (typiquement un `int` de 4 octets)  
- `[rbp-0x8]` → deuxième variable locale, ou suite d'une variable de 8 octets  
- `[rbp-0x18]` → un paramètre sauvegardé sur la pile (GCC en `-O0` copie souvent les paramètres depuis les registres vers la pile dès le début de la fonction)  
- `[rbp+0x8]` → adresse de retour (rarement accédée directement, sauf dans du code de sécurité ou de l'exploitation)  
- `[rbp+0x10]` → 7ème argument (si la fonction en a plus de 6)

### Deviner les variables locales depuis le prologue

L'instruction `sub rsp, N` vous donne la taille totale de l'espace local. En scannant le corps de la fonction, les accès `[rbp-X]` vous révèlent les offsets utilisés. En croisant les deux, vous pouvez reconstituer la liste des variables locales :

```asm
sub    rsp, 0x20                       ; 32 octets alloués

; Dans le corps de la fonction :
mov    DWORD PTR [rbp-0x4], 0x0        ; variable locale de 4 octets (int ?)  
mov    DWORD PTR [rbp-0x8], 0x0        ; une autre variable de 4 octets  
mov    QWORD PTR [rbp-0x18], rdi       ; sauvegarde du 1er paramètre (pointeur, 8 octets)  
```

Ici, on peut déduire au moins trois variables locales : deux `int` (4 octets chacun) et un pointeur (8 octets), plus du padding pour l'alignement. Ce n'est pas une reconstruction exacte du code source, mais c'est une approximation exploitable.

---

## Prologues et épilogues en `-O2` : les variantes optimisées

Comme vu en section 7.3, l'optimisation change significativement l'apparence des prologues. Voici les formes que vous rencontrerez en pratique.

### Cas 1 : pas de prologue du tout (*leaf function* optimisée)

Une fonction courte qui n'appelle aucune autre fonction et n'a pas besoin de pile :

```asm
; Début de la fonction (pas de push rbp, pas de sub rsp)
movzx  eax, BYTE PTR [rdi]  
movsx  edx, al  
add    edx, esi  
mov    eax, edx  
ret  
```

Pas de prologue, pas d'épilogue autre que `ret`. La fonction travaille entièrement dans les registres. C'est le cas le plus compact et le plus rapide, mais aussi le plus déroutant quand on cherche des frontières de fonctions dans un binaire strippé — il n'y a aucun marqueur de début.

### Cas 2 : prologue minimal avec sauvegarde de registres

La fonction a besoin de registres callee-saved mais n'alloue pas d'espace sur la pile au-delà :

```asm
push   rbx                    ; sauvegarde de rbx  
mov    rbx, rdi               ; rbx = premier paramètre (conservé à travers les call)  
; ... corps ...
pop    rbx                    ; restauration de rbx  
ret  
```

Pas de `push rbp` / `mov rbp, rsp`. Le frame pointer n'est pas maintenu. Les seuls `push`/`pop` sont ceux des registres callee-saved dont la fonction a besoin. C'est la forme la plus courante en `-O2` pour les fonctions de taille moyenne.

### Cas 3 : prologue avec allocation de pile mais sans frame pointer

La fonction a besoin d'espace sur la pile (par exemple pour un tableau local ou pour sauvegarder plus de valeurs que les registres ne le permettent), mais n'utilise pas `rbp` comme frame pointer :

```asm
sub    rsp, 0x28              ; allocation directe  
mov    QWORD PTR [rsp+0x8], rbx  ; sauvegarde callee-saved sur la pile  
mov    QWORD PTR [rsp], r12       ; idem  
; ... corps : accès via [rsp+N] au lieu de [rbp-N] ...
mov    r12, QWORD PTR [rsp]  
mov    rbx, QWORD PTR [rsp+0x8]  
add    rsp, 0x28  
ret  
```

Les accès aux variables locales se font via **`rsp`** au lieu de `rbp`. C'est plus difficile à lire car `rsp` change à chaque `push`, `pop`, `call` et `sub rsp` — il faut tracer sa valeur mentalement. Les désassembleurs avancés comme Ghidra gèrent cela automatiquement en recalculant la « stack depth » à chaque instruction.

### Cas 4 : frame pointer explicitement maintenu malgré `-O2`

Si vous compilez avec `-O2 -fno-omit-frame-pointer`, GCC maintient le frame pointer même en mode optimisé. C'est le cas de certains projets qui veulent des backtraces fiables en production (le noyau Linux, certains serveurs). Vous retrouvez alors le prologue classique `push rbp` / `mov rbp, rsp`, mais le reste du code est optimisé (registres, réordonnancement, etc.).

Certaines distributions compilent les paquets systèmes avec `-fno-omit-frame-pointer` par défaut (Fedora notamment). Si vous analysez un binaire système et que vous voyez des prologues classiques malgré un code visiblement optimisé, c'est probablement ce flag.

---

## Reconnaître les frontières de fonctions dans un listing strippé

Mettons en pratique ce que nous savons. Sur un binaire strippé, les frontières de fonctions ne sont plus marquées par des labels. Voici un algorithme mental à appliquer en lisant le listing d'`objdump` :

### Étape 1 : chercher les patterns de début

Scannez le listing à la recherche de ces séquences, par ordre de fiabilité :

1. **`push rbp` + `mov rbp, rsp`** — signal très fort d'un début de fonction en `-O0`/`-O1` ou avec `-fno-omit-frame-pointer`.  
2. **`endbr64`** — instruction de contrôle de flux indirect (*Indirect Branch Tracking*, IBT, partie de CET). GCC l'insère au début de chaque fonction quand le binaire est compilé avec `-fcf-protection` (activé par défaut sur les distributions récentes). Si vous voyez `endbr64`, c'est presque certainement le début d'une fonction.  
3. **`push rbx`** ou **`push r12`** en début de séquence (juste après un `ret` précédent) — probable début de fonction optimisée qui sauvegarde des registres callee-saved.  
4. **`sub rsp, N`** juste après un `ret` — probable début de fonction leaf optimisée avec allocation de pile.

### Étape 2 : chercher les patterns de fin

1. **`ret`** (ou `rep ret`, un idiome AMD pour éviter une pénalité de prédiction de branchement) — fin de fonction.  
2. **`leave` + `ret`** — fin de fonction avec frame pointer.  
3. **Séquence de `pop` + `ret`** — fin de fonction avec restauration de registres.

### Étape 3 : corréler avec les `call`

Chaque `call <adresse>` dans le listing vous indique qu'une fonction commence à `<adresse>`. Collectez toutes les cibles de `call` : chacune est un point d'entrée de fonction confirmé.

```bash
# Extraire les adresses cibles de tous les call internes
objdump -d -M intel keygenme_strip | \
    grep -oP 'call\s+\K[0-9a-f]+(?=\s)' | \
    sort -u
```

### Étape 4 : attention aux faux positifs

Certaines séquences ressemblent à des prologues sans en être :

- **`push rbp` au milieu d'une fonction** : rare mais possible dans du code qui manipule `rbp` comme registre général (en `-O2` sans frame pointer, `rbp` peut servir à n'importe quoi).  
- **`ret` qui n'est pas une fin de fonction** : les fonctions avec plusieurs chemins de retour ont plusieurs `ret`. Le premier `ret` rencontré n'est pas nécessairement la fin de la fonction — il peut y avoir d'autres blocs après (branche `else`, gestion d'erreur).  
- **Padding entre fonctions** : GCC insère souvent des octets `nop` (ou `nop DWORD PTR [rax]`, une forme multi-octets de `nop`) entre les fonctions pour les aligner sur des frontières de 16 octets. Ces `nop` apparaissent entre un `ret` et le prochain prologue. Ils ne font partie d'aucune fonction.

```asm
; Fin de la fonction précédente
    11e0:       c3                      ret
; Padding d'alignement
    11e1:       0f 1f 80 00 00 00 00    nop    DWORD PTR [rax+0x0]
; Début de la fonction suivante
    11e8:       f3 0f 1e fa             endbr64
    11ec:       55                      push   rbp
```

Si vous voyez des `nop` multi-octets entre un `ret` et un `push rbp` (ou `endbr64`), c'est du padding — vous venez de confirmer une frontière entre deux fonctions.

---

## `endbr64` : le marqueur moderne de début de fonction

Sur les binaires compilés avec GCC récent sur les distributions actuelles, vous verrez systématiquement `endbr64` (opcode `f3 0f 1e fa`) au tout début de chaque fonction, avant même le `push rbp` :

```asm
0000000000001139 <compute_hash>:
    1139:       f3 0f 1e fa             endbr64
    113d:       55                      push   rbp
    113e:       48 89 e5                mov    rbp, rsp
    ...
```

Cette instruction fait partie de la technologie Intel CET (*Control-flow Enforcement Technology*). Elle indique au processeur que cette adresse est une cible légitime de branchement indirect. Son rôle est purement sécuritaire (empêcher certaines attaques de détournement de flux), mais pour nous en RE, c'est un **marqueur fiable de début de fonction**, encore plus reconnaissable que `push rbp` car la séquence d'octets `f3 0f 1e fa` est toujours identique.

```bash
# Compter les fonctions via endbr64
objdump -d -M intel keygenme_strip | grep -c "endbr64"
```

Ce comptage inclut aussi les entrées de la PLT et quelques stubs du *runtime* C, mais c'est une excellente approximation du nombre de fonctions.

---

## Lire un prologue pour déduire la signature de la fonction

Le prologue et les premières instructions du corps de la fonction vous donnent des indices sur la **signature** de la fonction (nombre et types des paramètres).

Rappel de la convention System V AMD64 : les 6 premiers arguments entiers/pointeurs sont passés dans `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` (dans cet ordre). Les arguments flottants utilisent `xmm0`–`xmm7`. La valeur de retour est dans `rax` (entier) ou `xmm0` (flottant).

En `-O0`, GCC copie systématiquement les paramètres depuis les registres vers la pile au début de la fonction :

```asm
push   rbp  
mov    rbp, rsp  
sub    rsp, 0x20  
mov    QWORD PTR [rbp-0x18], rdi       ; sauvegarde du 1er paramètre (pointeur/entier 64b)  
mov    DWORD PTR [rbp-0x1c], esi       ; sauvegarde du 2ème paramètre (int 32b)  
```

En voyant ces `mov` initiaux, vous pouvez déduire :

- La fonction prend au moins 2 paramètres.  
- Le premier (`rdi`) est un entier 64 bits ou un pointeur (sauvegardé avec `QWORD PTR`).  
- Le second (`esi`) est un entier 32 bits (sauvegardé avec `DWORD PTR`, et c'est `esi` — la partie basse 32 bits de `rsi`).

En `-O2`, les paramètres ne sont pas copiés sur la pile (ils restent dans les registres), mais vous pouvez observer quels registres d'arguments sont **utilisés** dans les premières instructions :

```asm
; Début d'une fonction optimisée
movzx  eax, BYTE PTR [rdi]            ; utilise rdi → 1er paramètre (pointeur)  
test   esi, esi                        ; utilise esi → 2ème paramètre (int)  
```

Si la fonction utilise `rdi`, `rsi` et `rdx` dès ses premières instructions (sans les avoir reçus d'un `call` précédent ni les avoir calculés), elle prend probablement 3 paramètres. C'est une heuristique, pas une certitude — mais elle fonctionne remarquablement bien en pratique.

---

## Reconnaître la valeur de retour

La valeur de retour se lit dans les instructions juste **avant** le `ret` :

```asm
; La fonction retourne un int
mov    eax, DWORD PTR [rbp-0x4]       ; charge la variable locale 'result'  
leave  
ret  

; La fonction retourne un pointeur
mov    rax, QWORD PTR [rbp-0x10]      ; charge un pointeur  
leave  
ret  

; La fonction retourne 0 (succès / false)
xor    eax, eax                        ; eax = 0 (idiome classique)  
ret  

; La fonction retourne 1 (true)
mov    eax, 0x1  
ret  
```

Si une fonction a plusieurs `ret` (plusieurs chemins de retour), examinez chacun d'eux. Souvent, un chemin retourne 0 et l'autre retourne 1 — c'est le pattern d'une fonction de validation (`bool check_something(…)`).

L'instruction `xor eax, eax` juste avant un `ret` est un idiome extrêmement courant qui vaut la peine d'être mémorisé : il met `eax` à zéro en deux octets (plus court et plus rapide que `mov eax, 0`). GCC l'utilise systématiquement.

---

## Résumé

Le prologue et l'épilogue sont les bornes structurelles de chaque fonction. En `-O0`, le prologue canonique `push rbp` / `mov rbp, rsp` / `sub rsp, N` est systématique et facile à repérer. En `-O2`, le frame pointer disparaît souvent, le prologue se réduit à des `push` de registres callee-saved ou à un simple `sub rsp`, et les accès aux variables locales passent par `rsp` au lieu de `rbp`. L'instruction `endbr64`, présente sur les binaires modernes, constitue un marqueur de début de fonction encore plus fiable que `push rbp`. Les premières instructions après le prologue révèlent les paramètres de la fonction (via les registres d'arguments utilisés), et les instructions précédant le `ret` révèlent la valeur de retour. Sur un binaire strippé, combiner la recherche de prologues, d'épilogues, de cibles de `call` et de padding d'alignement permet de reconstituer la carte des fonctions sans aucun symbole.

---


⏭️ [Identifier `main()` et les fonctions C++ (name mangling)](/07-objdump-binutils/05-identifier-main-mangling.md)
