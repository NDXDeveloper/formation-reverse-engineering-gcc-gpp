🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.1 — Registres généraux, pointeurs et flags (`rax`, `rsp`, `rbp`, `rip`, `RFLAGS`…)

> 🎯 **Objectif de cette section** : connaître les registres du x86-64 que l'on rencontre constamment dans le code désassemblé, comprendre leur rôle respectif, et savoir pourquoi un même registre peut apparaître sous plusieurs noms (`rax`, `eax`, `ax`, `al`).

---

## Qu'est-ce qu'un registre ?

Un registre est une zone de stockage minuscule et ultra-rapide, directement intégrée dans le processeur. Là où un accès à la RAM prend des dizaines de nanosecondes, un accès à un registre se fait en un cycle d'horloge — c'est l'espace de travail immédiat du CPU.

Quand le compilateur traduit votre code C en assembleur, il s'efforce de garder les variables les plus utilisées dans des registres plutôt qu'en mémoire (sur la pile). C'est l'une des raisons pour lesquelles le code désassemblé est truffé de noms de registres : ce sont, en quelque sorte, les « variables locales » du processeur.

En x86-64, les registres à usage général font **64 bits** (8 octets), ce qui permet de manipuler directement des adresses mémoire et des entiers 64 bits.

---

## Les registres à usage général

L'architecture x86-64 dispose de **16 registres à usage général** (contre 8 en x86 32 bits). Voici la liste complète, avec le rôle que chacun joue *par convention* dans le code généré par GCC sous Linux (convention System V AMD64 ABI — détaillée en section 3.5 et 3.6) :

| Registre 64 bits | Rôle conventionnel | Sauvegardé par l'appelé ? |  
|---|---|---|  
| `rax` | Valeur de retour d'une fonction | Non |  
| `rbx` | Usage général (base, historique) | **Oui** |  
| `rcx` | 4ᵉ argument entier d'une fonction | Non |  
| `rdx` | 3ᵉ argument entier d'une fonction | Non |  
| `rsi` | 2ᵉ argument entier (« source index ») | Non |  
| `rdi` | 1ᵉʳ argument entier (« destination index ») | Non |  
| `rbp` | Pointeur de base de la stack frame | **Oui** |  
| `rsp` | Pointeur de sommet de pile | **Oui** (implicitement) |  
| `r8` | 5ᵉ argument entier | Non |  
| `r9` | 6ᵉ argument entier | Non |  
| `r10` | Usage général (temporaire) | Non |  
| `r11` | Usage général (temporaire) | Non |  
| `r12` | Usage général | **Oui** |  
| `r13` | Usage général | **Oui** |  
| `r14` | Usage général | **Oui** |  
| `r15` | Usage général | **Oui** |

La colonne « sauvegardé par l'appelé » (*callee-saved*) est capitale pour le RE : si une fonction utilise `rbx`, `r12`–`r15` ou `rbp`, elle **doit** les sauvegarder en début de fonction (généralement par un `push`) et les restaurer avant de retourner (par un `pop`). Vous verrez ces `push`/`pop` systématiquement dans les prologues et épilogues — ce n'est pas du bruit, c'est la convention ABI en action.

À l'inverse, les registres *caller-saved* (`rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11`) peuvent être écrasés librement par n'importe quel `call`. Si une fonction a besoin de conserver leur valeur à travers un appel, c'est à elle de les sauvegarder avant.

> 💡 **Pour le RE** : quand vous voyez une série de `push rbx` / `push r12` / `push r13` en début de fonction, cela vous indique immédiatement que la fonction utilise ces registres pour stocker des valeurs qui doivent survivre à des appels de sous-fonctions. C'est un indice sur la complexité de la fonction.

---

## Sous-registres : pourquoi `rax`, `eax`, `ax`, `al` ?

L'architecture x86-64 est rétrocompatible avec toutes les générations précédentes (x86 32 bits, 16 bits, et même 8 bits). Chaque registre 64 bits est donc accessible par tranches, via des noms différents :

```
Bits :  63                           31              15      7      0
        ┌────────────────────────────┬───────────────┬───────┬──────┐
  rax   │         (bits 63-32)       │     eax       │  ax   │      │
        │                            │               ├───────┤      │
        │                            │               │  ah   │  al  │
        └────────────────────────────┴───────────────┴───────┴──────┘

  rax  = 64 bits complets
  eax  = 32 bits inférieurs (bits 31-0)
  ax   = 16 bits inférieurs (bits 15-0)
  ah   = bits 15-8 (octet haut de ax)
  al   = bits 7-0  (octet bas)
```

Le même principe s'applique à tous les registres classiques :

| 64 bits | 32 bits | 16 bits | 8 bits haut | 8 bits bas |  
|---|---|---|---|---|  
| `rax` | `eax` | `ax` | `ah` | `al` |  
| `rbx` | `ebx` | `bx` | `bh` | `bl` |  
| `rcx` | `ecx` | `cx` | `ch` | `cl` |  
| `rdx` | `edx` | `dx` | `dh` | `dl` |  
| `rsi` | `esi` | `si` | — | `sil` |  
| `rdi` | `edi` | `di` | — | `dil` |  
| `rbp` | `ebp` | `bp` | — | `bpl` |  
| `rsp` | `esp` | `sp` | — | `spl` |

Pour les registres `r8`–`r15`, la nomenclature est plus régulière :

| 64 bits | 32 bits | 16 bits | 8 bits bas |  
|---|---|---|---|  
| `r8` | `r8d` | `r8w` | `r8b` |  
| `r9` | `r9d` | `r9w` | `r9b` |  
| … | … | … | … |  
| `r15` | `r15d` | `r15w` | `r15b` |

### Pourquoi c'est important pour le RE

Le choix du nom de registre dans le désassemblage **vous révèle la taille du type manipulé** :

```asm
mov     eax, 0x1          ; opération 32 bits → probablement un int  
mov     al, byte [rdi]    ; opération 8 bits  → probablement un char / octet  
mov     rax, [rbp-0x10]   ; opération 64 bits → probablement un pointeur (ou un long)  
```

C'est un réflexe essentiel : quand vous lisez du code désassemblé et que vous voyez `eax` plutôt que `rax`, vous savez immédiatement que la variable sous-jacente est un entier 32 bits (comme un `int` en C sous Linux x86-64), pas un pointeur.

> ⚠️ **Piège classique** : écrire dans un sous-registre 32 bits (`eax`) met automatiquement à zéro les 32 bits supérieurs du registre 64 bits correspondant (`rax`). Ce comportement est spécifique au mode 64 bits et fait partie de la spécification AMD64. En revanche, écrire dans un sous-registre 8 ou 16 bits (`al`, `ax`) ne touche **pas** aux bits supérieurs. GCC exploite cette règle : un `mov eax, 0` est un moyen efficace de mettre `rax` à zéro (et en pratique, GCC préfère souvent `xor eax, eax` qui est encore plus court — on y reviendra en section 3.3).

---

## Les registres à rôle spécial

Trois registres ne sont pas interchangeables avec les autres : le processeur leur attribue un rôle matériel spécifique.

### `rip` — le pointeur d'instruction (*Instruction Pointer*)

`rip` contient l'adresse de la **prochaine instruction** à exécuter. On ne le manipule jamais directement avec un `mov` — il avance automatiquement après chaque instruction, et il est modifié implicitement par les instructions de saut (`jmp`, `jz`, `call`, `ret`…).

En RE, `rip` est omniprésent :

- Dans **GDB**, quand vous faites un `break *0x401234`, vous posez un breakpoint à une valeur future de `rip`.  
- Dans **Ghidra**, la colonne d'adresses à gauche du listing correspond aux valeurs successives de `rip`.  
- L'**adressage relatif à `rip`** (RIP-relative addressing) est la norme en x86-64 pour accéder aux données globales et aux chaînes de caractères :

```asm
lea     rdi, [rip+0x2e5a]    ; charge l'adresse d'une chaîne .rodata  
call    puts@plt  
```

Cette instruction `lea` ne charge pas la valeur *à* l'adresse `rip+0x2e5a` — elle calcule l'adresse elle-même et la place dans `rdi`. On verra cela en détail en section 3.2.

### `rsp` — le pointeur de pile (*Stack Pointer*)

`rsp` pointe toujours vers le **sommet de la pile** (l'adresse la plus basse de la stack frame courante, car la pile grandit vers les adresses basses sur x86-64). Il est modifié implicitement par `push`, `pop`, `call` et `ret`, et explicitement par des instructions comme `sub rsp, 0x20` (réservation d'espace local dans le prologue de fonction).

```
Adresses hautes
    ┌──────────────────┐
    │   ...            │
    │  arguments pile  │  ← au-delà du 6ᵉ argument
    │  adresse retour  │  ← empilée par call
    │  ancien rbp      │  ← sauvé par le prologue
    │  variables       │
    │  locales         │
    │                  │  ← rsp pointe ici (sommet)
    └──────────────────┘
Adresses basses
```

> 💡 **Pour le RE** : si vous voyez `mov eax, [rsp+0x8]` ou `mov [rsp+0x10], rdi`, la fonction accède à ses variables locales ou aux arguments passés via la pile. La valeur de l'offset par rapport à `rsp` vous aide à reconstruire le layout de la stack frame.

### `rbp` — le pointeur de base de la frame (*Base Pointer*)

En mode non optimisé (`-O0`), GCC utilise `rbp` comme **ancre fixe** de la stack frame courante. Le prologue classique d'une fonction ressemble à ceci :

```asm
push    rbp             ; sauvegarde l'ancien rbp  
mov     rbp, rsp        ; rbp = nouveau base pointer  
sub     rsp, 0x20       ; réserve 32 octets pour les variables locales  
```

Avec ce schéma, les variables locales sont accessibles via des offsets négatifs par rapport à `rbp` (`[rbp-0x4]`, `[rbp-0x8]`, etc.) et les arguments passés via la pile (s'il y en a) sont accessibles via des offsets positifs (`[rbp+0x10]`, `[rbp+0x18]`, etc.).

> ⚠️ **Frame pointer omission** : avec les optimisations activées (`-O1` et au-delà), GCC active par défaut `-fomit-frame-pointer`. Le registre `rbp` est alors libéré pour servir de registre à usage général, et tous les accès à la pile se font relativement à `rsp`. C'est plus efficace (un registre supplémentaire disponible), mais rend la lecture du désassemblage un peu plus exigeante, car les offsets par rapport à `rsp` changent à chaque `push` ou `sub rsp`. C'est un des effets des optimisations que l'on détaillera au chapitre 16.

---

## Le registre `RFLAGS` — les drapeaux du processeur

`RFLAGS` est un registre spécial qui n'apparaît jamais par son nom dans le code assembleur, mais dont les **bits individuels** (appelés *flags* ou *drapeaux*) contrôlent le comportement des sauts conditionnels. C'est le mécanisme fondamental qui traduit les `if`, `else`, `while` et `for` du C en assembleur.

Le principe est simple et omniprésent :

1. Une instruction de **comparaison** ou d'**arithmétique** (`cmp`, `test`, `sub`, `add`…) modifie les flags.  
2. Une instruction de **saut conditionnel** (`jz`, `jnz`, `jl`, `jge`…) lit les flags et décide de sauter ou non.

### Les flags essentiels pour le RE

Sur les 64 bits de `RFLAGS`, seuls quelques drapeaux apparaissent régulièrement dans le code produit par GCC :

| Flag | Nom | Bit | Signification | Mis à 1 quand… |  
|---|---|---|---|---|  
| **ZF** | Zero Flag | 6 | Le résultat est zéro | `cmp a, b` avec `a == b`, ou `sub` / `test` dont le résultat est 0 |  
| **SF** | Sign Flag | 7 | Le résultat est négatif (bit de poids fort = 1) | Le résultat interprété en signé est négatif |  
| **CF** | Carry Flag | 0 | Retenue en arithmétique non signée | Débordement sur une opération non signée |  
| **OF** | Overflow Flag | 11 | Débordement en arithmétique signée | Le résultat signé ne tient pas dans le registre |  
| **PF** | Parity Flag | 2 | Parité de l'octet bas du résultat | Nombre pair de bits à 1 dans l'octet bas |

En pratique, **ZF** et **SF** couvrent la majorité des cas que vous rencontrerez en RE de code applicatif compilé par GCC. **CF** et **OF** interviennent surtout dans les comparaisons non signées et les vérifications de débordement.

### Le duo `cmp` + saut conditionnel

La paire `cmp` / saut conditionnel est le pattern le plus fréquent dans tout binaire compilé. Voici comment elle fonctionne :

```c
// Code C
if (x == 42) {
    do_something();
}
```

```asm
; Assembleur généré par GCC (-O0)
cmp     dword [rbp-0x4], 0x2a   ; compare x avec 42  
jne     .skip                    ; si x != 42, saute au-delà  
call    do_something             ; sinon, exécute l'appel  
.skip:
```

L'instruction `cmp` effectue une soustraction (`x - 42`) sans stocker le résultat, mais en mettant à jour les flags. Si `x == 42`, la soustraction donne 0, donc **ZF = 1**. L'instruction `jne` (*Jump if Not Equal*) saute si **ZF = 0** — donc elle ne saute pas, et l'appel à `do_something` est exécuté.

> 💡 **Pour le RE** : vous n'avez pas besoin de mémoriser la table de correspondance entre chaque instruction de saut et les flags qu'elle teste. Ce qui compte, c'est de lire le `cmp` qui précède et de comprendre la condition en langage naturel : `jne` = « saute si pas égal », `jl` = « saute si inférieur (signé) », `ja` = « saute si supérieur (non signé) ». La section 3.4 dresse la table complète.

### Le duo `test` + saut conditionnel

L'autre pattern très courant est `test reg, reg`, qui effectue un ET logique sans stocker le résultat :

```c
// Code C
if (ptr != NULL) {
    use(ptr);
}
```

```asm
; Assembleur généré par GCC
test    rax, rax          ; ET logique de rax avec lui-même  
jz      .skip             ; saute si rax == 0 (ZF = 1)  
mov     rdi, rax  
call    use  
.skip:
```

`test rax, rax` est l'idiome standard de GCC pour vérifier si un registre est nul. Le résultat de `rax AND rax` est `rax` lui-même — si cette valeur est zéro, ZF est mis à 1. C'est plus compact et plus rapide qu'un `cmp rax, 0`.

---

## Les registres de segments — un vestige à connaître

Vous croiserez parfois des références à des registres de segment (`cs`, `ds`, `ss`, `es`, `fs`, `gs`) dans le désassemblage. En mode 64 bits, `cs`, `ds`, `ss` et `es` ne jouent plus de rôle de segmentation mémoire — le modèle mémoire est plat (*flat*).

En revanche, **`fs` et `gs`** restent utilisés :

- Sous **Linux**, `fs` pointe vers le Thread Local Storage (TLS). Vous verrez fréquemment des accès comme `mov rax, qword [fs:0x28]` — c'est la lecture du **stack canary** (valeur sentinelle anti-buffer-overflow), un pattern que l'on retrouve dans pratiquement tout binaire compilé avec `-fstack-protector` :

```asm
; Prologue typique avec stack canary
mov     rax, qword [fs:0x28]     ; lit le canary depuis le TLS  
mov     qword [rbp-0x8], rax     ; le place sur la pile  

; ... corps de la fonction ...

; Épilogue — vérification du canary
mov     rax, qword [rbp-0x8]     ; relit le canary depuis la pile  
xor     rax, qword [fs:0x28]     ; compare avec la valeur originale  
jne     .stack_chk_fail           ; si différent → corruption détectée  
```

Ce pattern est détaillé au chapitre 19 (anti-reversing et protections), mais il est utile de le reconnaître dès maintenant pour ne pas le confondre avec de la logique applicative.

---

## Les registres flottants et vectoriels — aperçu rapide

L'architecture x86-64 dispose aussi de registres dédiés aux calculs en virgule flottante et aux opérations vectorielles (SIMD). On ne les couvre pas en détail ici (la section 3.9 y est consacrée), mais il est utile de connaître leur existence pour ne pas être surpris en les croisant.

**Registres SSE/AVX** : `xmm0`–`xmm15` (128 bits), étendus en `ymm0`–`ymm15` (256 bits) avec AVX. En convention System V AMD64, les arguments flottants (`float`, `double`) sont passés via `xmm0`–`xmm7`, et la valeur de retour flottante est dans `xmm0`.

```c
// Code C
double add_doubles(double a, double b) {
    return a + b;
}
```

```asm
; Assembleur — a dans xmm0, b dans xmm1
addsd   xmm0, xmm1       ; addition double précision  
ret                        ; résultat dans xmm0  
```

**Registres x87 FPU** (`st0`–`st7`) : l'ancienne unité de calcul flottant à pile, héritée du 8087. On les croise rarement dans du code GCC moderne en 64 bits (GCC préfère SSE), mais ils peuvent apparaître avec certaines options ou dans du code 32 bits.

---

## Récapitulatif visuel

```
╔════════════════════════════════════════════════════════════════════╗
║                   REGISTRES x86-64 POUR LE RE                      ║
╠════════════════════════════════════════════════════════════════════╣
║                                                                    ║
║  USAGE GÉNÉRAL (64 bits)           RÔLE CONVENTION System V        ║
║  ┌─────┐                                                           ║
║  │ rax │  Valeur de retour                                         ║
║  │ rbx │  Callee-saved (usage général)                             ║
║  │ rcx │  4ᵉ argument                                              ║
║  │ rdx │  3ᵉ argument (+ partie haute retour 128 bits)             ║
║  │ rsi │  2ᵉ argument                                              ║
║  │ rdi │  1ᵉʳ argument                                             ║
║  │ rbp │  Base pointer (si frame pointer actif)                    ║
║  │ rsp │  Stack pointer (sommet de pile)                           ║
║  │ r8  │  5ᵉ argument                                              ║
║  │ r9  │  6ᵉ argument                                              ║
║  │r10  │  Temporaire                                               ║
║  │r11  │  Temporaire                                               ║
║  │r12  │  Callee-saved                                             ║
║  │r13  │  Callee-saved                                             ║
║  │r14  │  Callee-saved                                             ║
║  │r15  │  Callee-saved                                             ║
║  └─────┘                                                           ║
║                                                                    ║
║  POINTEUR D'INSTRUCTION                                            ║
║  ┌─────┐                                                           ║
║  │ rip │  Adresse de la prochaine instruction                      ║
║  └─────┘                                                           ║
║                                                                    ║
║  FLAGS                                                             ║
║  ┌────────┐                                                        ║
║  │RFLAGS  │  ZF (zéro), SF (signe), CF (retenue), OF (overflow)    ║
║  └────────┘                                                        ║
║                                                                    ║
║  FLOTTANTS / SIMD                                                  ║
║  ┌──────────────┐                                                  ║
║  │ xmm0–xmm15  │  128 bits SSE (args float : xmm0–xmm7)            ║
║  │ ymm0–ymm15  │  256 bits AVX (extension de xmm)                  ║
║  └──────────────┘                                                  ║
║                                                                    ║
╚════════════════════════════════════════════════════════════════════╝
```

---

## Ce qu'il faut retenir pour la suite

Cinq réflexes à développer en lisant du code désassemblé :

1. **La taille du nom de registre indique la taille de la donnée** : `rax` → 64 bits (pointeur ou long), `eax` → 32 bits (int), `ax` → 16 bits (short), `al` → 8 bits (char/byte).  
2. **`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`** sont les six registres d'arguments entiers — quand vous les voyez écrits juste avant un `call`, ce sont les paramètres.  
3. **`rax`** après un `call` contient la valeur de retour.  
4. **`rsp`** et **`rbp`** délimitent la stack frame — les accès `[rbp-X]` ou `[rsp+X]` touchent les variables locales.  
5. **`cmp`/`test`** modifient les flags, et le **saut conditionnel** qui suit immédiatement les exploite — c'est la traduction directe de vos `if` / `while` / `for`.

---


⏭️ [Instructions essentielles : `mov`, `push`/`pop`, `call`/`ret`, `lea`](/03-assembleur-x86-64/02-instructions-essentielles.md)
