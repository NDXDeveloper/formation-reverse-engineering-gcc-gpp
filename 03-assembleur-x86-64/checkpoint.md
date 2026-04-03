🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 3

## Annoter manuellement un désassemblage réel

> **But** : valider que vous maîtrisez les acquis du chapitre 3 en appliquant la méthode de lecture en 5 étapes (section 3.7) sur un listing assembleur réel. Ce checkpoint couvre les registres, les instructions essentielles, l'arithmétique, les sauts conditionnels, le prologue/épilogue, le passage de paramètres et la reconstruction en pseudo-code C.

---

## Consignes

Le listing ci-dessous est le désassemblage d'une fonction inconnue, compilée par GCC en `-O0` pour x86-64 Linux (syntaxe Intel). Le binaire a été strippé — vous ne disposez d'aucun nom de fonction ni de variable.

Votre travail consiste à :

1. **Délimiter** la fonction (prologue, épilogue, nombre d'arguments).  
2. **Structurer** le flux de contrôle (identifier les blocs, les sauts, les boucles, les branchements).  
3. **Caractériser** les opérations remarquables (appels, constantes, patterns connus).  
4. **Annoter** chaque instruction ou groupe d'instructions avec un commentaire expliquant son rôle.  
5. **Reformuler** la logique en pseudo-code C.

---

## Listing à analyser

```asm
; Fonction inconnue — GCC -O0, x86-64 Linux, syntaxe Intel, non-PIE

0x401176:  push    rbp
0x401177:  mov     rbp, rsp
0x40117a:  mov     qword [rbp-0x18], rdi
0x40117e:  mov     dword [rbp-0x1c], esi
0x401181:  mov     dword [rbp-0x8], 0x0
0x401188:  mov     dword [rbp-0x4], 0x0
0x40118f:  jmp     0x4011c1
0x401191:  mov     eax, dword [rbp-0x4]
0x401194:  movsxd  rdx, eax
0x401197:  mov     rax, qword [rbp-0x18]
0x40119b:  add     rax, rdx
0x40119e:  movzx   eax, byte [rax]
0x4011a1:  cmp     al, 0x60
0x4011a3:  jle     0x4011bd
0x4011a5:  mov     eax, dword [rbp-0x4]
0x4011a8:  movsxd  rdx, eax
0x4011ab:  mov     rax, qword [rbp-0x18]
0x4011af:  add     rax, rdx
0x4011b2:  movzx   eax, byte [rax]
0x4011b5:  cmp     al, 0x7a
0x4011b7:  jg      0x4011bd
0x4011b9:  add     dword [rbp-0x8], 0x1
0x4011bd:  add     dword [rbp-0x4], 0x1
0x4011c1:  mov     eax, dword [rbp-0x4]
0x4011c4:  cmp     eax, dword [rbp-0x1c]
0x4011c7:  jl      0x401191
0x4011c9:  mov     eax, dword [rbp-0x8]
0x4011cc:  pop     rbp
0x4011cd:  ret
```

---

## Grille d'auto-évaluation

Avant de consulter le corrigé, vérifiez que vous avez identifié chacun des éléments suivants :

| Élément | Question | ✓ |  
|---|---|---|  
| **Prologue** | Quelles instructions constituent le prologue ? Y a-t-il un frame pointer ? | ☐ |  
| **Arguments** | Combien de paramètres la fonction reçoit-elle ? Quels sont leurs types probables ? | ☐ |  
| **Variables locales** | Combien de variables locales sont déclarées ? À quels offsets `rbp` ? | ☐ |  
| **Boucle** | Y a-t-il une boucle ? De quel type (for, while, do…while) ? Quelle est sa borne ? | ☐ |  
| **Condition interne** | Quelle condition est testée dans le corps de la boucle ? Que protège-t-elle ? | ☐ |  
| **Constantes** | Quelles constantes apparaissent ? Quelle est leur signification ? | ☐ |  
| **Valeur de retour** | Que retourne la fonction ? Dans quel registre ? | ☐ |  
| **Pseudo-code** | Pouvez-vous réécrire la logique complète en C ? | ☐ |

---

## Corrigé détaillé

> ⚠️ **Essayez de faire l'exercice avant de lire la suite.** Le corrigé est là pour valider votre analyse, pas pour la remplacer.

### Étape 1 — Délimiter

**Prologue** (lignes `0x401176`–`0x401177`) :

```asm
0x401176:  push    rbp                 ; sauvegarde l'ancien base pointer
0x401177:  mov     rbp, rsp            ; établit le frame pointer
```

Prologue classique avec frame pointer, sans `sub rsp` — les variables locales tiennent dans la red zone ou l'espace au-dessus de `rbp`. Pas de sauvegarde de registres callee-saved (pas de `push rbx`, `push r12`…) → fonction simple. Pas de stack canary (pas de `mov rax, [fs:0x28]`).

**Épilogue** (lignes `0x4011cc`–`0x4011cd`) :

```asm
0x4011cc:  pop     rbp                 ; restaure l'ancien base pointer
0x4011cd:  ret                          ; retourne à l'appelant
```

Pas de `leave` ici, mais un simple `pop rbp` (équivalent puisqu'il n'y a pas eu de `sub rsp`).

**Arguments** — les spills en début de fonction révèlent les paramètres :

```asm
0x40117a:  mov     qword [rbp-0x18], rdi    ; 1er arg, 64 bits → pointeur
0x40117e:  mov     dword [rbp-0x1c], esi    ; 2e arg, 32 bits → int
```

Deux paramètres : un pointeur (64 bits, dans `rdi`) et un entier (32 bits, dans `esi`).

### Étape 2 — Structurer

Identifions les sauts :

| Adresse | Instruction | Cible | Direction |  
|---|---|---|---|  
| `0x40118f` | `jmp 0x4011c1` | `0x4011c1` | Vers le bas → saut vers le test de boucle |  
| `0x4011a3` | `jle 0x4011bd` | `0x4011bd` | Vers le bas → contournement (condition fausse) |  
| `0x4011b7` | `jg 0x4011bd` | `0x4011bd` | Vers le bas → contournement (condition fausse) |  
| `0x4011c7` | `jl 0x401191` | `0x401191` | **Vers le haut** → **boucle** |

Le `jl` vers le haut à `0x4011c7` confirme une boucle. Le `jmp 0x4011c1` initial saute directement au test de la boucle → c'est le pattern **boucle `for`/`while` avec test en bas** (cf. section 3.4).

Blocs identifiés :

```
Bloc A [0x401176–0x40118f] : prologue + spill arguments + init variables + jmp vers test  
Bloc B [0x401191–0x4011b9] : corps de la boucle (chargement str[i] + double condition)  
Bloc C [0x4011bd–0x4011c1] : incrémentation du compteur de boucle i  
Bloc D [0x4011c1–0x4011c7] : test de boucle (cmp + jl retour au corps)  
Bloc E [0x4011c9–0x4011cd] : chargement du résultat + épilogue + ret  
```

Schéma du flux :

```
        ┌──────────┐
        │  Bloc A  │  prologue + init
        └────┬─────┘
             │ jmp
             ▼
        ┌─────────────┐
  ┌────►│  Bloc D     │  test : i < len ?
  │     └──┬───────┬──┘
  │   vrai │       │ faux
  │        ▼       │
  │  ┌──────────┐  │
  │  │  Bloc B  │  │
  │  │  corps   │  │
  │  └────┬─────┘  │
  │       ▼        │
  │  ┌──────────┐  │
  │  │  Bloc C  │  │
  │  │  i++     │  │
  │  └────┬─────┘  │
  │       │        │
  └───────┘        │
                   ▼
            ┌──────────┐
            │  Bloc E  │  return result
            └──────────┘
```

### Étape 3 — Caractériser

**Aucun `call`** → la fonction n'appelle rien, elle est autonome.

**Constantes remarquables** :

| Valeur hex | Décimal | ASCII | Signification |  
|---|---|---|---|  
| `0x60` | 96 | `` ` `` | Caractère juste avant `'a'` (97) dans la table ASCII |  
| `0x7a` | 122 | `z` | Lettre `'z'` minuscule |  
| `0x0` | 0 | — | Initialisation à zéro |  
| `0x1` | 1 | — | Incrémentation |

Les constantes `0x60` et `0x7a` délimitent l'intervalle des **lettres minuscules** dans la table ASCII : un caractère `c` est une lettre minuscule si `c > 0x60 && c <= 0x7a`, c'est-à-dire `c >= 'a' && c <= 'z'`. Notons que la comparaison utilise `jle` (inférieur ou égal) avec `0x60`, ce qui signifie « saute si `al <= 0x60` », soit « continue si `al > 0x60` ». Combiné avec `jg` sur `0x7a` (« saute si `al > 0x7a` »), la condition pour ne PAS sauter est `0x60 < al <= 0x7a`, soit `'a' <= al <= 'z'`.

**Opérations** :

- `movsxd rdx, eax` → extension signée d'un index `int` vers 64 bits (pour l'arithmétique de pointeur).  
- `movzx eax, byte [rax]` → lecture d'un **octet non signé** (caractère) depuis un buffer pointé.  
- `add dword [rbp-0x8], 0x1` → incrémentation d'un compteur (la variable qui sera retournée).

**Hypothèse** : la fonction compte le nombre de lettres minuscules dans une chaîne (ou un buffer de longueur donnée).

### Étape 4 — Annoter

```asm
; === PROLOGUE ===
0x401176:  push    rbp                          ; sauvegarde le frame pointer
0x401177:  mov     rbp, rsp                     ; établit le nouveau frame pointer

; === SPILL DES ARGUMENTS ===
0x40117a:  mov     qword [rbp-0x18], rdi        ; arg1 → str (pointeur, char*)
0x40117e:  mov     dword [rbp-0x1c], esi        ; arg2 → len (entier, int)

; === INITIALISATION DES VARIABLES LOCALES ===
0x401181:  mov     dword [rbp-0x8], 0x0         ; count = 0 (compteur de minuscules)
0x401188:  mov     dword [rbp-0x4], 0x0         ; i = 0 (index de boucle)
0x40118f:  jmp     0x4011c1                     ; saut vers le test de boucle

; === CORPS DE LA BOUCLE — 1ère condition (str[i] >= 'a') ===
0x401191:  mov     eax, dword [rbp-0x4]         ; eax = i
0x401194:  movsxd  rdx, eax                     ; rdx = (long)i (extension signée)
0x401197:  mov     rax, qword [rbp-0x18]        ; rax = str
0x40119b:  add     rax, rdx                     ; rax = str + i  (adresse de str[i])
0x40119e:  movzx   eax, byte [rax]              ; eax = (unsigned char)str[i]
0x4011a1:  cmp     al, 0x60                     ; compare str[i] avec 96 ('a' - 1)
0x4011a3:  jle     0x4011bd                     ; si str[i] <= 96 → pas minuscule, skip

; === CORPS DE LA BOUCLE — 2e condition (str[i] <= 'z') ===
; GCC -O0 recharge str[i] intégralement (pas de réutilisation des registres)
0x4011a5:  mov     eax, dword [rbp-0x4]         ; eax = i (rechargé depuis la pile)
0x4011a8:  movsxd  rdx, eax                     ; rdx = (long)i
0x4011ab:  mov     rax, qword [rbp-0x18]        ; rax = str (rechargé depuis la pile)
0x4011af:  add     rax, rdx                     ; rax = str + i
0x4011b2:  movzx   eax, byte [rax]              ; eax = (unsigned char)str[i]
0x4011b5:  cmp     al, 0x7a                     ; compare str[i] avec 122 ('z')
0x4011b7:  jg      0x4011bd                     ; si str[i] > 'z' → pas minuscule, skip

; --- les deux conditions sont vraies : c'est une minuscule ---
0x4011b9:  add     dword [rbp-0x8], 0x1         ; count++

; === INCRÉMENTATION ===
0x4011bd:  add     dword [rbp-0x4], 0x1         ; i++

; === TEST DE BOUCLE ===
0x4011c1:  mov     eax, dword [rbp-0x4]         ; eax = i
0x4011c4:  cmp     eax, dword [rbp-0x1c]        ; compare i avec len
0x4011c7:  jl      0x401191                     ; si i < len → retour au corps

; === RETOUR ===
0x4011c9:  mov     eax, dword [rbp-0x8]         ; eax = count (valeur de retour)
0x4011cc:  pop     rbp                           ; restaure le frame pointer
0x4011cd:  ret                                    ; retourne count
```

**Carte des variables :**

| Offset | Taille | Registre d'origine | Nom | Type | Rôle |  
|---|---|---|---|---|---|  
| `[rbp-0x18]` | 8 octets | `rdi` | `str` | `const char *` | Pointeur vers le buffer à analyser |  
| `[rbp-0x1c]` | 4 octets | `esi` | `len` | `int` | Longueur du buffer |  
| `[rbp-0x08]` | 4 octets | — | `count` | `int` | Compteur de lettres minuscules |  
| `[rbp-0x04]` | 4 octets | — | `i` | `int` | Index de boucle |

### Étape 5 — Reformuler en pseudo-code C

```c
int count_lowercase(const char *str, int len) {
    int count = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] > 0x60 && str[i] <= 0x7a) {  // 'a' <= str[i] <= 'z'
            count++;
        }
    }
    return count;
}
```

Ou, en version plus idiomatique :

```c
int count_lowercase(const char *str, int len) {
    int count = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            count++;
        }
    }
    return count;
}
```

### Points d'attention dans ce listing

**La double condition comme traduction du `&&`** (section 3.4) : le `if (c >= 'a' && c <= 'z')` en C produit deux `cmp`/`jXX` consécutifs qui sautent tous les deux vers la **même cible** (`0x4011bb`). Dès que l'une des conditions est fausse, on court-circuite — c'est la signature du ET logique.

**L'inversion des conditions par GCC** : en C on écrit `>= 'a'`, mais GCC compare avec `0x60` (= `'a' - 1`) et utilise `jle` (saute si inférieur ou égal). La condition « continue » est `al > 0x60`, ce qui équivaut à `al >= 0x61` soit `al >= 'a'`. De même, `jg 0x7a` saute si `al > 'z'`, donc la condition « continue » est `al <= 'z'`.

**Le `movzx` (section 3.2)** : `movzx eax, byte [rax]` lit un octet et l'étend à 32 bits avec des zéros. C'est la lecture d'un `unsigned char` (ou d'un `char` traité comme non signé pour la comparaison de plage). Le fait que GCC utilise `movzx` (et non `movsx`) combiné avec `jle`/`jg` (sauts signés) nous dit que les valeurs comparées sont dans la plage 0–127 (ASCII standard), où la distinction signé/non signé n'a pas d'impact.

**Le rechargement intégral de `str[i]` pour la 2e condition** (lignes `0x4011a5`–`0x4011b2`) : GCC en `-O0` recalcule entièrement `str[i]` depuis la pile — recharge `i`, étend en 64 bits, recharge `str`, recalcule `str + i`, relit l'octet — au lieu de réutiliser la valeur déjà présente dans `eax`. C'est le comportement typique de `-O0` : chaque sous-expression C est compilée indépendamment, sans réutilisation de valeurs en registre. Avec `-O1` ou plus, cette séquence entière serait éliminée au profit d'une simple comparaison sur le registre déjà chargé.

---

## Critères de validation

Vous pouvez considérer ce checkpoint comme réussi si vous avez :

- ☑ Identifié correctement le prologue, l'épilogue et les deux arguments (`char *` + `int`).  
- ☑ Repéré la boucle `for` avec test en bas et le saut arrière `jl` à `0x4011c7`.  
- ☑ Compris que les deux `cmp`/`jXX` internes forment un `&&` (court-circuit vers la même cible).  
- ☑ Traduit les constantes `0x60` et `0x7a` en bornes de l'alphabet minuscule ASCII.  
- ☑ Produit un pseudo-code C fonctionnellement correct (même si les noms diffèrent).

Si certains points vous ont posé problème, relisez la section correspondante du chapitre avant de passer à la suite.

---

> ✅ **Chapitre 3 terminé.** Vous disposez maintenant des bases d'assembleur x86-64 nécessaires pour aborder les outils de RE des parties II et III.  
>  
> 

⏭️ [Chapitre 4 — Mise en place de l'environnement de travail](/04-environnement-travail/README.md)
