🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe B — Conventions d'appel System V AMD64 ABI (tableau récapitulatif)

> 📎 **Fiche de référence** — Cette annexe détaille la convention d'appel utilisée par **tous** les binaires ELF x86-64 sous Linux (et la plupart des Unix : macOS, FreeBSD, Solaris). C'est la convention que GCC, G++, Clang et Rust appliquent par défaut. La maîtriser est indispensable pour lire n'importe quel désassemblage : sans elle, vous ne pouvez pas savoir quel registre contient quel argument, quelle fonction est responsable de sauvegarder quoi, ni comment interpréter la pile.

---

## 1 — Vue d'ensemble

La convention d'appel System V AMD64 (formellement définie dans le document *System V Application Binary Interface — AMD64 Architecture Processor Supplement*) régit trois aspects fondamentaux de chaque appel de fonction :

- **Comment les arguments sont transmis** de l'appelant (*caller*) à l'appelé (*callee*)  
- **Comment la valeur de retour** est renvoyée à l'appelant  
- **Quels registres** doivent être préservés par l'appelé et lesquels peuvent être écrasés librement

Contrairement à la convention x86 32 bits (où presque tout passait par la pile), System V AMD64 privilégie massivement les **registres** pour le passage d'arguments. C'est ce qui rend le code x86-64 plus compact et plus rapide, mais aussi ce qui exige du reverse engineer qu'il connaisse précisément l'assignation des registres.

---

## 2 — Passage des arguments entiers et pointeurs

Les six premiers arguments de type entier, pointeur ou référence sont passés dans des registres dédiés, dans un ordre fixe. Au-delà de six, les arguments supplémentaires sont empilés sur la pile.

### 2.1 — Registres d'arguments entiers

| Position | Registre | Exemples de types C |  
|----------|----------|---------------------|  
| 1er argument | `rdi` | `int`, `long`, `size_t`, `char *`, `void *`, `struct *` |  
| 2ᵉ argument | `rsi` | idem |  
| 3ᵉ argument | `rdx` | idem |  
| 4ᵉ argument | `rcx` | idem |  
| 5ᵉ argument | `r8` | idem |  
| 6ᵉ argument | `r9` | idem |  
| 7ᵉ et suivants | pile (en ordre gauche→droite, adresses croissantes depuis `rsp`) | idem |

> 💡 **Moyen mnémotechnique** : **D**iane **S**'il **D**anse **C**e **8**→**9** — `rDi`, `rSi`, `rDx`, `rCx`, `r8`, `r9`. Il existe de nombreuses variantes de ce type de phrases mnémoniques ; choisissez celle qui fonctionne pour vous, car cet ordre est la clé de voûte de toute lecture d'assembleur x86-64.

### 2.2 — Arguments sur la pile

Lorsqu'une fonction a plus de six arguments entiers, les arguments excédentaires sont poussés sur la pile **de droite à gauche** (le 7ᵉ argument se retrouve à l'adresse la plus basse, juste au-dessus de l'adresse de retour). Après le `call`, le layout de la pile vu par l'appelé est :

```
              ┌───────────────────────┐  adresses hautes
              │     9ᵉ argument       │  [rsp + 0x20]
              ├───────────────────────┤
              │     8ᵉ argument       │  [rsp + 0x18]
              ├───────────────────────┤
              │     7ᵉ argument       │  [rsp + 0x10]
              ├───────────────────────┤
              │  adresse de retour    │  [rsp + 0x08]   ← poussée par call
              ├───────────────────────┤
              │    (frame locale)     │  [rsp]           ← sommet de pile
              └───────────────────────┘  adresses basses
```

> ⚠️ **Attention** : les offsets ci-dessus sont ceux **à l'entrée de la fonction**, avant que le prologue n'ajuste `rsp`. Après un `push rbp` / `sub rsp, N`, tous les offsets se décalent.

En pratique, les fonctions à plus de 6 arguments entiers sont peu fréquentes en code C/C++ idiomatique, mais elles existent (certaines fonctions de la libc comme `mmap` en ont 6 exactement, et des fonctions custom ou générées par des frameworks peuvent en avoir davantage).

### 2.3 — Particularités du comptage d'arguments

Le comptage des arguments suit les types **après promotion** :

- Un `char` ou `short` passé à une fonction est promu en `int` (32 bits) mais occupe quand même un registre 64 bits complet. GCC peut utiliser `edi` (32 bits) au lieu de `rdi` (64 bits) pour charger la valeur, puisque l'écriture dans un registre 32 bits met automatiquement à zéro les 32 bits supérieurs.  
- Un `bool` occupe un registre entier (typiquement via `edi`), avec la valeur 0 ou 1.  
- Les `enum` sont traités comme des entiers de la taille sous-jacente (généralement `int`).

---

## 3 — Passage des arguments flottants

Les arguments de type `float`, `double` et `long double` (80 bits, x87) suivent un chemin séparé.

### 3.1 — Registres d'arguments flottants

| Position | Registre | Types |  
|----------|----------|-------|  
| 1er argument flottant | `xmm0` | `float`, `double` |  
| 2ᵉ argument flottant | `xmm1` | idem |  
| 3ᵉ argument flottant | `xmm2` | idem |  
| 4ᵉ argument flottant | `xmm3` | idem |  
| 5ᵉ argument flottant | `xmm4` | idem |  
| 6ᵉ argument flottant | `xmm5` | idem |  
| 7ᵉ argument flottant | `xmm6` | idem |  
| 8ᵉ argument flottant | `xmm7` | idem |  
| 9ᵉ et suivants | pile | idem |

### 3.2 — Compteurs séparés

Un point crucial : les compteurs d'arguments entiers et flottants sont **indépendants**. Chaque type puise dans sa propre banque de registres sans affecter l'autre. Considérons la signature suivante :

```c
void draw(int x, double opacity, int y, float scale, const char *label);
```

L'assignation des registres sera :

| Argument | Type | Registre |  
|----------|------|----------|  
| `x` | `int` | `edi` (1er entier) |  
| `opacity` | `double` | `xmm0` (1er flottant) |  
| `y` | `int` | `esi` (2ᵉ entier) |  
| `scale` | `float` | `xmm1` (2ᵉ flottant) |  
| `label` | `const char *` | `rdx` (3ᵉ entier) |

Les positions des arguments dans la signature C déterminent l'ordre logique, mais chaque argument « pioche » dans le prochain registre disponible de sa catégorie (entier ou flottant). Il n'y a pas de « trou » laissé dans les registres entiers pour un argument flottant.

### 3.3 — Le registre `al` et les fonctions variadiques

Pour les fonctions à arguments variables (`printf`, `scanf`, et toute fonction déclarée avec `...`), l'ABI exige que **`al` contienne le nombre de registres SSE utilisés** pour les arguments (de 0 à 8). C'est pourquoi vous verrez souvent avant un `call printf` :

```asm
xor     eax, eax          ; al = 0 (aucun argument flottant)  
call    printf@plt  
```

Ou, si un `double` est passé :

```asm
mov     eax, 1            ; al = 1 (un argument flottant dans xmm0)  
call    printf@plt  
```

Ce `xor eax, eax` ou `mov eax, N` juste avant un `call` est un **marqueur fiable de fonction variadique** en RE. Si vous le voyez, vous savez que la fonction appelée accepte un nombre variable d'arguments.

> ⚠️ Pour les fonctions **non variadiques**, la valeur de `al` n'est pas spécifiée et n'a pas besoin d'être positionnée par l'appelant. GCC peut quand même émettre `xor eax, eax` avant certains appels non variadiques pour des raisons de performance (éviter les dépendances sur les bits supérieurs de `rax`), ce qui peut induire en erreur. En cas de doute, vérifiez le prototype de la fonction.

---

## 4 — Valeurs de retour

### 4.1 — Retour entier

| Registre | Usage |  
|----------|-------|  
| `rax` | Valeur de retour entière principale (jusqu'à 64 bits) |  
| `rdx` | Seconde moitié si le retour fait 128 bits (`__int128`, ou certaines structures de 2 × 64 bits) |

La très grande majorité des fonctions C retournent dans `rax` uniquement. Le retour via la paire `rdx:rax` est rare en dehors de l'arithmétique 128 bits ou de structures compactes à deux champs 64 bits.

### 4.2 — Retour flottant

| Registre | Usage |  
|----------|-------|  
| `xmm0` | Valeur de retour `float` ou `double` |  
| `xmm1` | Seconde partie (structures contenant 2 flottants) |

### 4.3 — Retour de `void`

Les fonctions `void` ne placent rien dans `rax`. Cependant, le contenu de `rax` après un `call` vers une fonction `void` est **indéfini** — il contient une valeur résiduelle. En RE, ne déduisez pas de signification d'un usage de `rax` après un appel à une fonction que vous avez identifiée comme `void`.

### 4.4 — Retour de structures

Le passage et le retour de structures constituent le cas le plus complexe de l'ABI. Le comportement dépend de la taille et de la composition de la structure :

| Taille / Composition | Mécanisme de retour |  
|----------------------|---------------------|  
| ≤ 8 octets, types entiers uniquement | `rax` |  
| 9–16 octets, types entiers uniquement | `rax` + `rdx` |  
| ≤ 8 octets, types flottants uniquement | `xmm0` |  
| 9–16 octets, types flottants uniquement | `xmm0` + `xmm1` |  
| 8 octets entier + 8 octets flottant (ou inversement) | `rax` + `xmm0` (ou `xmm0` + `rax`) |  
| > 16 octets, ou contient des champs non triviaux | Via pointeur caché (voir ci-dessous) |

**Le pointeur caché (*hidden pointer*)** : quand une structure est trop grande pour tenir dans les registres de retour, l'appelant alloue de l'espace sur sa propre pile et passe un **pointeur caché** en premier argument (dans `rdi`). Ce pointeur est invisible dans le code source C mais bien réel dans l'assembleur. L'appelé écrit le résultat à l'adresse pointée par `rdi` et retourne cette même adresse dans `rax`.

Ce mécanisme est un piège classique en RE : si vous voyez une fonction prendre un argument `rdi` qui ressemble à un pointeur local de l'appelant, et que cette fonction écrit abondamment à `[rdi]`, `[rdi+8]`, etc., c'est très probablement un retour de structure via pointeur caché. Le nombre effectif de « vrais » arguments est alors décalé d'un cran (le 1er argument visible en C est en réalité dans `rsi`, pas dans `rdi`).

---

## 5 — Classification des registres : caller-saved vs callee-saved

C'est l'un des tableaux les plus importants de toute la formation. Lors de chaque appel de fonction, certains registres sont **garantis préservés** par l'appelé (callee-saved, aussi appelés *non-volatile*) et d'autres peuvent être **écrasés librement** par l'appelé (caller-saved, aussi appelés *volatile*).

### 5.1 — Tableau complet

| Registre | Catégorie | Préservé après un `call` ? | Usage principal |  
|----------|-----------|---------------------------|-----------------|  
| `rax` | Caller-saved | Non | Valeur de retour, résultat `mul`/`div` |  
| `rbx` | **Callee-saved** | **Oui** | Usage général |  
| `rcx` | Caller-saved | Non | 4ᵉ argument, compteur pour `rep`/shifts |  
| `rdx` | Caller-saved | Non | 3ᵉ argument, extension retour, `mul`/`div` |  
| `rsi` | Caller-saved | Non | 2ᵉ argument, source pour `rep movs` |  
| `rdi` | Caller-saved | Non | 1er argument, destination pour `rep movs/stos` |  
| `rbp` | **Callee-saved** | **Oui** | Frame pointer (si utilisé) ou usage général |  
| `rsp` | **Callee-saved** | **Oui** | Stack pointer (doit toujours être restauré) |  
| `r8` | Caller-saved | Non | 5ᵉ argument |  
| `r9` | Caller-saved | Non | 6ᵉ argument |  
| `r10` | Caller-saved | Non | Usage temporaire, 4ᵉ argument syscall |  
| `r11` | Caller-saved | Non | Usage temporaire, écrasé par `syscall` |  
| `r12` | **Callee-saved** | **Oui** | Usage général |  
| `r13` | **Callee-saved** | **Oui** | Usage général |  
| `r14` | **Callee-saved** | **Oui** | Usage général |  
| `r15` | **Callee-saved** | **Oui** | Usage général |  
| `rip` | — | (modifié par `call`/`ret`) | Instruction pointer |  
| `RFLAGS` | Caller-saved | Non | Flags (condition codes) |  
| `xmm0–xmm7` | Caller-saved | Non | Arguments flottants et valeur de retour |  
| `xmm8–xmm15` | Caller-saved | Non | Temporaires flottants |  
| `x87 FPU stack` | Caller-saved | Non | Registres FPU x87 (usage rare en x86-64) |  
| `MMX` | Caller-saved | Non | Registres MMX (héritage, très rare) |

### 5.2 — Résumé visuel

```
╔══════════════════════════════════════════════════════════════╗
║  CALLEE-SAVED (préservés par la fonction appelée)            ║
║  rbx, rbp, rsp, r12, r13, r14, r15                           ║
║                                                              ║
║  → Si vous voyez push rbx / push r12 dans un prologue,       ║
║    c'est la sauvegarde de ces registres avant utilisation.   ║
╠══════════════════════════════════════════════════════════════╣
║  CALLER-SAVED (peuvent être écrasés par tout appel)          ║
║  rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11                   ║
║  xmm0–xmm15, RFLAGS                                          ║
║                                                              ║
║  → Après un call, ces registres contiennent des valeurs      ║
║    potentiellement différentes. Ne supposez pas qu'ils       ║
║    conservent leur valeur d'avant l'appel.                   ║
╚══════════════════════════════════════════════════════════════╝
```

### 5.3 — Implications en RE

La distinction caller-saved / callee-saved a des conséquences directes sur votre lecture du désassemblage :

**Prologues et épilogues** — Les registres callee-saved utilisés par une fonction sont sauvegardés dans le prologue (`push rbx`, `push r12`, etc.) et restaurés dans l'épilogue (`pop r12`, `pop rbx`). En comptant les `push` callee-saved dans le prologue, vous savez quels registres la fonction utilise comme « variables locales de registre » (GCC alloue les variables fréquemment utilisées dans ces registres pour éviter les accès mémoire).

**Identification du nombre de variables locales de registre** — Si une fonction commence par `push rbx` / `push r12` / `push r13`, elle utilise trois registres callee-saved en plus de ses registres temporaires. Cela indique souvent une fonction de complexité moyenne avec 3 à 5 variables significatives.

**Suivi des valeurs à travers les `call`** — Si une valeur est dans `rbx` et qu'un `call` intervient, la valeur de `rbx` est toujours intacte après le retour (car `rbx` est callee-saved). En revanche, si une valeur était dans `rdi` avant le `call`, elle est potentiellement perdue. C'est pourquoi GCC déplace souvent un argument (`rdi`) vers un registre callee-saved (`rbx`) au début d'une fonction qui fait plusieurs appels : la valeur survivra à tous les `call`.

---

## 6 — Prologue et épilogue standard

### 6.1 — Prologue avec frame pointer (`-O0` ou `-fno-omit-frame-pointer`)

```asm
push    rbp              ; sauvegarde l'ancien frame pointer (callee-saved)  
mov     rbp, rsp         ; rbp = nouveau frame pointer  
sub     rsp, 0x30        ; alloue 48 octets de variables locales  
```

Avec un frame pointer, les variables locales sont accessibles via `[rbp - offset]` et les arguments empilés (s'il y en a) via `[rbp + offset]` (au-dessus de l'adresse de retour).

```
              ┌───────────────────────┐  adresses hautes
              │  arguments empilés    │  [rbp + 0x18], [rbp + 0x10]
              ├───────────────────────┤
              │  adresse de retour    │  [rbp + 0x08]
              ├───────────────────────┤
              │  ancien rbp (sauvé)   │  [rbp]        ← rbp pointe ici
              ├───────────────────────┤
              │  variable locale 1    │  [rbp - 0x08]
              ├───────────────────────┤
              │  variable locale 2    │  [rbp - 0x10]
              ├───────────────────────┤
              │        ...            │
              ├───────────────────────┤
              │  variable locale N    │  [rbp - 0x30] ← rsp pointe ici
              └───────────────────────┘  adresses basses
```

Épilogue correspondant :

```asm
leave                    ; équivaut à : mov rsp, rbp ; pop rbp  
ret                      ; pop rip (retour à l'appelant)  
```

Ou la forme décomposée :

```asm
mov     rsp, rbp         ; libère les variables locales  
pop     rbp              ; restaure l'ancien frame pointer  
ret  
```

### 6.2 — Prologue sans frame pointer (`-O1` et au-delà avec `-fomit-frame-pointer`)

À partir de `-O1`, GCC omet le frame pointer par défaut. `rbp` est alors utilisé comme registre callee-saved ordinaire, et toutes les variables locales sont référencées relativement à `rsp` :

```asm
push    rbx              ; sauvegarde d'un registre callee-saved  
push    r12              ; sauvegarde d'un autre  
sub     rsp, 0x28        ; alloue de l'espace pour les locales + alignement  
```

Épilogue correspondant :

```asm
add     rsp, 0x28        ; libère les locales  
pop     r12              ; restaure  
pop     rbx              ; restaure  
ret  
```

Sans frame pointer, les offsets par rapport à `rsp` changent chaque fois que quelque chose est poussé ou retiré de la pile (par exemple pour préparer un appel). C'est ce qui rend le code optimisé plus difficile à lire manuellement que le code `-O0`. Les décompilateurs comme Ghidra gèrent bien cette complexité en traquant les ajustements de `rsp`, mais en lecture manuelle, il faut compter chaque `push`, `pop` et `sub rsp` pour maintenir le bon offset.

### 6.3 — Le Red Zone

L'ABI System V AMD64 définit une **red zone** (*zone rouge*) de 128 octets en dessous de `rsp` (adresses `[rsp-1]` à `[rsp-128]`) que les fonctions feuilles (*leaf functions* — fonctions qui n'appellent aucune autre fonction) peuvent utiliser **sans ajuster `rsp`** :

```asm
; Fonction feuille simple — pas de sub rsp !
mov     dword ptr [rsp-0x4], edi    ; stocke le 1er argument dans la red zone  
mov     eax, dword ptr [rsp-0x4]    ; le recharge  
ret  
```

La red zone est garantie de ne pas être écrasée par les interruptions ou les signaux (le noyau en tient compte). Elle permet aux fonctions feuilles simples d'éviter le coût du prologue/épilogue, ce qui accélère les appels de petites fonctions.

En RE, la red zone se manifeste par des accès à `[rsp - offset]` **sans** `sub rsp` préalable. Si vous voyez cela dans une fonction qui ne contient aucun `call`, c'est un usage normal de la red zone. Si vous le voyez dans une fonction qui appelle d'autres fonctions, c'est un bug de compilation (extrêmement rare) ou votre analyse de la pile est incorrecte.

> ⚠️ La red zone n'existe **pas** dans le code noyau (`-mno-red-zone`) ni dans les gestionnaires de signaux de bas niveau, car les interruptions pourraient écraser cette zone. C'est une distinction importante si vous analysez du code kernel.

---

## 7 — Alignement de la pile

L'ABI exige que la pile soit alignée sur **16 octets au moment du `call`**. Cela signifie que `rsp` doit être un multiple de 16 juste avant l'exécution de l'instruction `call`. Après le `call` (qui pousse 8 octets d'adresse de retour), `rsp` vaut donc `16n + 8` à l'entrée de la fonction appelée.

C'est pourquoi les prologues ajustent souvent `rsp` par des multiples de 16 (en tenant compte des `push` déjà effectués). Si une fonction fait un `push rbp` (8 octets), `rsp` est de nouveau aligné sur 16 et le `sub rsp` qui suit utilise un multiple de 16. Si elle fait trois `push` (24 octets = 16 + 8), le `sub rsp` devra compenser les 8 octets d'excédent.

En RE, un `sub rsp, 0x8` isolé (sans variable locale apparente) est souvent un **pur padding d'alignement**. GCC l'insère pour garantir l'alignement de 16 octets avant le prochain `call`. Ne cherchez pas de variable locale correspondante : il n'y en a pas.

L'alignement à 16 octets est nécessaire pour les instructions SSE (`movaps`, etc.) qui exigent des opérandes mémoire alignés. Un défaut d'alignement provoque un `SIGSEGV` sur `movaps` — c'est d'ailleurs un bug courant en exploitation.

---

## 8 — Comparaison avec la convention syscall Linux

Les appels système Linux x86-64 utilisent une convention légèrement différente de la convention d'appel de fonctions. Les confondre est une erreur classique.

| Aspect | Appel de fonction (System V) | Appel système (`syscall`) |  
|--------|------------------------------|---------------------------|  
| Numéro / cible | Adresse dans l'instruction `call` | Numéro du syscall dans `rax` |  
| 1er argument | `rdi` | `rdi` |  
| 2ᵉ argument | `rsi` | `rsi` |  
| 3ᵉ argument | `rdx` | `rdx` |  
| 4ᵉ argument | **`rcx`** | **`r10`** |  
| 5ᵉ argument | `r8` | `r8` |  
| 6ᵉ argument | `r9` | `r9` |  
| Valeur de retour | `rax` | `rax` (négatif = erreur = `-errno`) |  
| Registres écrasés | Tous les caller-saved | **`rcx`** et **`r11`** uniquement |  
| Arguments flottants | `xmm0`–`xmm7` | Non supporté (pas d'arguments flottants via syscall) |

Les trois différences à retenir sont les suivantes. Premièrement, le 4ᵉ argument : `rcx` pour les fonctions, `r10` pour les syscalls. Cette différence existe parce que l'instruction `syscall` écrase `rcx` (elle y sauvegarde `rip`) et `r11` (elle y sauvegarde `RFLAGS`), donc `rcx` ne peut pas transporter un argument. Deuxièmement, les registres écrasés : un `syscall` ne détruit que `rcx` et `r11`, alors qu'un `call` de fonction peut potentiellement écraser tous les registres caller-saved. Troisièmement, les erreurs : un retour négatif dans `rax` après un `syscall` indique une erreur, la valeur étant l'opposé du code `errno` (par exemple, `rax = -2` signifie `ENOENT`).

---

## 9 — Passage de structures par valeur

Le passage de structures en argument suit des règles de classification assez complexes. Voici le résumé pratique :

### 9.1 — Structures petites (≤ 16 octets)

Les structures de 16 octets ou moins sont décomposées en « eightbytes » (blocs de 8 octets) et chaque bloc est classé indépendamment :

| Contenu du bloc | Classe | Registre utilisé |  
|-----------------|--------|-------------------|  
| Entiers, pointeurs, booléens | INTEGER | Prochain registre entier disponible (`rdi`, `rsi`, …) |  
| `float`, `double` | SSE | Prochain registre SSE disponible (`xmm0`, `xmm1`, …) |  
| Mélange entier + flottant dans le même bloc de 8 octets | Dépend des règles de fusion | Voir la spécification complète |

Exemple avec une structure simple :

```c
struct Point { int x; int y; };  // 8 octets total → 1 eightbyte → rdi

void move(struct Point p, int dx);
// p passe dans rdi (x dans les 32 bits bas, y dans les 32 bits hauts)
// dx passe dans esi
```

Exemple avec une structure de 16 octets :

```c
struct Rect { long x; long y; };  // 16 octets → 2 eightbytes → rdi + rsi

void draw(struct Rect r, int color);
// r.x passe dans rdi, r.y passe dans rsi
// color passe dans edx (3ᵉ registre entier)
```

### 9.2 — Structures grandes (> 16 octets)

Les structures de plus de 16 octets sont passées **par pointeur invisible** : l'appelant copie la structure sur sa pile et passe un pointeur vers cette copie dans le prochain registre entier disponible. L'appelé travaille avec une copie indépendante.

En RE, cela se manifeste par un `lea rdi, [rsp+offset]` avant le `call` : l'appelant charge l'adresse d'un espace local dans `rdi`, ce qui ressemble à un passage par référence alors que le code C passe par valeur.

### 9.3 — Structures avec constructeur/destructeur (C++)

En C++, les structures et classes avec un constructeur de copie non trivial, un destructeur non trivial, ou des membres virtuels sont **toujours** passées par pointeur invisible, quelle que soit leur taille. C'est parce que l'ABI ne peut pas garantir que la copie bit à bit implicite du passage par registre respecte la sémantique du constructeur de copie.

---

## 10 — Convention C++ : `this` et fonctions membres

En C++ compilé avec GCC/G++, les fonctions membres non statiques reçoivent un **pointeur `this` implicite** comme premier argument, dans `rdi` :

```cpp
class Widget {  
public:  
    void resize(int w, int h);
};

// Équivalent en termes d'ABI :
// void Widget::resize(Widget* this, int w, int h);
// this → rdi, w → esi, h → edx
```

Cela signifie que tous les arguments « visibles » sont décalés d'un cran par rapport à ce qu'on attendrait en C :

| Argument visible C++ | Registre réel |  
|----------------------|---------------|  
| `this` (implicite) | `rdi` |  
| 1er argument | `rsi` |  
| 2ᵉ argument | `rdx` |  
| 3ᵉ argument | `rcx` |  
| 4ᵉ argument | `r8` |  
| 5ᵉ argument | `r9` |

Pour les **fonctions virtuelles**, le mécanisme est le même mais l'appel passe par la vtable :

```asm
mov     rax, qword ptr [rdi]        ; charge le vptr (premier qword de l'objet)  
call    qword ptr [rax + 0x10]       ; appelle la 3ᵉ entrée de la vtable (offset / 8 = index)  
```

L'offset dans la vtable donne directement l'index de la méthode virtuelle (chaque entrée fait 8 octets en x86-64) : offset `0x00` = 1ʳᵉ méthode virtuelle, `0x08` = 2ᵉ, `0x10` = 3ᵉ, etc. Reconnaître ce pattern (déréférencement de `[rdi]` suivi d'un `call [rax + constante]`) est l'un des indices les plus fiables pour identifier un appel de méthode virtuelle C++.

---

## 11 — Récapitulatif synthétique en une page

```
╔══════════════════════════════════════════════════════════════════╗
║                  SYSTEM V AMD64 — AIDE-MÉMOIRE                   ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                  ║
║  ARGUMENTS ENTIERS :  rdi, rsi, rdx, rcx, r8, r9  → pile         ║
║  ARGUMENTS FLOTTANTS: xmm0–xmm7                   → pile         ║
║  (les compteurs entier et flottant sont indépendants)            ║
║                                                                  ║
║  RETOUR :  rax (+rdx si 128 bits)  |  xmm0 (+xmm1 si besoin)     ║
║                                                                  ║
║  CALLEE-SAVED :  rbx, rbp, rsp, r12, r13, r14, r15               ║
║  CALLER-SAVED :  rax, rcx, rdx, rsi, rdi, r8–r11, xmm0–xmm15     ║
║                                                                  ║
║  PILE : alignée 16 octets au call  |  Red zone : 128 octets      ║
║                                                                  ║
║  VARIADIQUES : al = nombre de registres xmm utilisés             ║
║                                                                  ║
║  SYSCALL : comme ci-dessus SAUF 4ᵉ arg = r10 (pas rcx)           ║
║            → écrase rcx et r11                                   ║
║                                                                  ║
║  C++ : this = rdi (1er arg implicite)                            ║
║        vtable call : mov rax,[rdi] ; call [rax+offset]           ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
```

---

> 📚 **Pour aller plus loin** :  
> - **Annexe A** — [Référence rapide des opcodes x86-64](/annexes/annexe-a-opcodes-x86-64.md) — les instructions elles-mêmes.  
> - **Annexe I** — [Patterns GCC reconnaissables à l'assembleur](/annexes/annexe-i-patterns-gcc.md) — les idiomes compilateur qui découlent de ces conventions.  
> - **Chapitre 3, section 3.5–3.6** — [La pile et le passage des paramètres](/03-assembleur-x86-64/05-pile-prologue-epilogue.md) — couverture pédagogique progressive avec exemples commentés.  
> - **Document de référence** : *System V Application Binary Interface — AMD64 Architecture Processor Supplement* (disponible sur [gitlab.com/x86-psABIs](https://gitlab.com/x86-psABIs/x86-64-ABI)) — la spécification officielle complète.

⏭️ [Cheat sheet GDB / GEF / pwndbg](/annexes/annexe-c-cheatsheet-gdb.md)
