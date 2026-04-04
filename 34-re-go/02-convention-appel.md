🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 34.2 — Convention d'appel Go (stack-based puis register-based depuis Go 1.17)

> 🐹 *Si vous avez passé des heures à maîtriser la convention System V AMD64 (chapitre 3), préparez-vous à un choc culturel. Go a longtemps utilisé une convention d'appel entièrement basée sur la pile — aucun argument dans les registres, aucune valeur de retour dans `RAX`. Depuis Go 1.17, une ABI registre a été introduite, mais elle diffère encore sensiblement de ce que fait le C. Comprendre ces deux conventions est indispensable pour lire correctement le désassemblage d'un binaire Go.*

---

## Pourquoi Go n'a pas adopté la convention System V

La convention System V AMD64, utilisée par GCC et Clang pour le C/C++ sur Linux, passe les six premiers arguments entiers dans `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9` et la valeur de retour dans `RAX` (chapitre 3, section 3.6). C'est efficace, bien documenté, et universel sur les systèmes Unix 64 bits.

Go a délibérément choisi une autre voie pour plusieurs raisons :

- **Valeurs de retour multiples.** En Go, une fonction peut retourner plusieurs valeurs (`result, err`). La convention System V ne prévoit que `RAX` et `RDX` pour les retours. Go avait besoin d'un mécanisme plus flexible.  
- **Piles extensibles.** Le mécanisme de croissance de pile (section 34.1) nécessite de pouvoir copier l'intégralité d'une pile, y compris les frames d'appel. Si les arguments sont dans des registres, ils ne sont pas sur la pile et ne sont pas copiés automatiquement — cela complique le runtime.  
- **Portabilité du compilateur.** L'équipe Go a privilégié une convention unique et simple sur toutes les architectures, plutôt que d'adapter le compilateur aux particularités de chaque ABI plateforme.  
- **Simplicité du compilateur originel.** Le compilateur Go (à l'origine dérivé de Plan 9) n'avait pas d'allocateur de registres sophistiqué. Tout passer par la pile était plus simple à implémenter.

Ces raisons étaient valides aux débuts du langage, mais les performances ont fini par justifier une migration. En 2021, Go 1.17 a introduit une ABI basée sur les registres — uniquement sur amd64 dans un premier temps, puis étendue à arm64 en Go 1.18.

---

## L'ancienne convention : tout sur la pile (Go < 1.17)

### Principe

Avant Go 1.17, **tous** les arguments et **toutes** les valeurs de retour étaient passés sur la pile. L'appelant empile les arguments de droite à gauche (comme `cdecl`), appelle la fonction, et récupère les valeurs de retour dans les slots réservés sur la pile au-dessus des arguments.

### Schéma de la stack frame

Pour un appel `result, err := maFonction(a, b, c)` :

```
Adresses croissantes ↑

┌─────────────────────────┐
│   err (retour 2)        │  ← RSP + 40  (réservé par l'appelant)
├─────────────────────────┤
│   result (retour 1)     │  ← RSP + 32  (réservé par l'appelant)
├─────────────────────────┤
│   c (argument 3)        │  ← RSP + 24
├─────────────────────────┤
│   b (argument 2)        │  ← RSP + 16
├─────────────────────────┤
│   a (argument 1)        │  ← RSP + 8
├─────────────────────────┤
│   adresse de retour     │  ← RSP (poussée par CALL)
└─────────────────────────┘
```

Notez plusieurs différences majeures avec System V :

- **Pas de `RBP` frame pointer.** Go n'utilise pas de prologue `push rbp; mov rbp, rsp`. Le frame pointer a été optionnellement réintroduit plus tard (Go 1.7, flag `-buildmode`), mais n'est pas systématique. L'absence de `RBP` complique le déroulement de pile (stack unwinding) dans GDB — c'est pour cela que Go fournit ses propres métadonnées de pile via `gopclntab`.  
- **Pas de zone rouge (red zone).** Contrairement à System V qui réserve 128 octets sous `RSP`, Go n'utilise pas de red zone.  
- **L'appelant réserve l'espace pour les retours.** Les valeurs de retour sont écrites par l'appelé directement dans des slots pré-alloués sur la stack frame de l'appelant. C'est ainsi que Go gère nativement les retours multiples.  
- **L'appelant nettoie la pile.** Après le retour, l'appelant ajuste `RSP` pour dépiler les arguments et les retours.

### Exemple assembleur (ancienne ABI)

Code Go :

```go
func add(x int, y int) int {
    return x + y
}

func caller() {
    r := add(10, 20)
    _ = r
}
```

Assembleur généré (Go ≤ 1.16, simplifié) :

```asm
; --- caller ---
; Préambule de vérification de pile omis pour la clarté
SUB     RSP, 24                ; réserve 24 octets : 2 args (16) + 1 retour (8)  
MOV     QWORD PTR [RSP], 10   ; argument x = 10  
MOV     QWORD PTR [RSP+8], 20 ; argument y = 20  
CALL    main.add  
MOV     RAX, [RSP+16]         ; récupère la valeur de retour  
ADD     RSP, 24                ; nettoie la frame  
RET  

; --- add ---
MOV     RAX, [RSP+8]          ; x (premier argument)  
ADD     RAX, [RSP+16]         ; y (second argument)  
MOV     [RSP+24], RAX         ; écrit le retour dans le slot réservé par l'appelant  
RET  
```

Les points à retenir pour le RE :

- Les arguments sont accessibles via des offsets positifs par rapport à `RSP` dans l'appelé (en comptant les 8 octets de l'adresse de retour).  
- La valeur de retour est écrite à un offset encore plus haut — juste au-dessus des arguments.  
- `RAX` **n'est pas** la valeur de retour. Si vous lisez machinalement `RAX` après un `CALL` comme en C, vous obtiendrez n'importe quoi.

> ⚠️ **Piège RE classique** : le décompilateur de Ghidra suppose par défaut la convention System V et interprète `RAX` comme valeur de retour. Sur un binaire Go ancien ABI, le pseudo-code sera **faux**. Il faudra corriger manuellement les signatures de fonctions ou utiliser un plugin Go pour Ghidra.

---

## La nouvelle convention : ABI register-based (Go ≥ 1.17)

### Motivation

L'ABI stack-only était simple mais coûteuse. Chaque appel de fonction impliquait de nombreux accès mémoire pour empiler et dépiler les arguments. Les benchmarks internes de Google montraient un surcoût de 5 à 10 % sur les programmes réels. En 2021, la proposition « register-based calling convention » a été adoptée et déployée dans Go 1.17.

### Registres utilisés

La nouvelle ABI Go définit deux séquences de registres :

**Registres entiers (arguments et retours) :**

| Ordre | Registre |  
|---|---|  
| 1 | `RAX` |  
| 2 | `RBX` |  
| 3 | `RCX` |  
| 4 | `RDI` |  
| 5 | `RSI` |  
| 6 | `R8` |  
| 7 | `R9` |  
| 8 | `R10` |  
| 9 | `R11` |

**Registres flottants (arguments et retours) :**

| Ordre | Registre |  
|---|---|  
| 1 | `X0` |  
| 2 | `X1` |  
| … | … |  
| 15 | `X14` |

Les registres sont assignés dans l'ordre aux arguments de gauche à droite. Les **mêmes séquences** sont utilisées pour les valeurs de retour, en repartant du début. Si les registres sont épuisés, les arguments restants débordent sur la pile (spill).

### Comparaison avec System V AMD64

| Aspect | System V AMD64 (C/C++) | Go ABI register (≥ 1.17) |  
|---|---|---|  
| 1er argument entier | `RDI` | `RAX` |  
| 2e argument entier | `RSI` | `RBX` |  
| 3e argument entier | `RDX` | `RCX` |  
| 4e argument entier | `RCX` | `RDI` |  
| 5e argument entier | `R8` | `RSI` |  
| 6e argument entier | `R9` | `R8` |  
| 1re valeur retour | `RAX` | `RAX` |  
| 2e valeur retour | `RDX` | `RBX` |  
| Retours multiples (> 2) | Non natif | Oui (suite des registres) |  
| Registre goroutine `g` | — | `R14` (réservé) |  
| Frame pointer | `RBP` (optionnel) | `RBP` (optionnel, depuis Go 1.7) |  
| Red zone | 128 octets | Aucune |

Attention : les deux conventions commencent par `RAX` pour le premier retour, ce qui peut donner l'illusion de compatibilité. Mais l'ordre des arguments diffère complètement. Ne supposez jamais que `RDI` contient le premier argument dans du Go.

> 💡 **Astuce RE** : un moyen rapide de distinguer l'ABI : dans l'ancienne convention, la première instruction significative d'une petite fonction accède à `[RSP+8]` pour son premier argument. Dans la nouvelle, elle utilise directement `RAX`.

### Registres réservés

Certains registres sont réservés par le runtime et ne servent jamais à passer des arguments :

| Registre | Usage réservé |  
|---|---|  
| `R14` | Pointeur vers la goroutine courante (`g`) |  
| `RSP` | Stack pointer |  
| `RBP` | Frame pointer (quand activé) |  
| `R12`, `R13` | Réservés (pourraient être utilisés dans de futures versions) |

`R14` est particulièrement important pour le RE : chaque préambule de fonction y accède pour la vérification de pile (`MOV RAX, [R14+0x10]`). C'est un marqueur fiable que vous êtes dans du code Go.

### Exemple assembleur (nouvelle ABI)

Le même code `add` compilé avec Go ≥ 1.17 :

```asm
; --- caller ---
MOV     RAX, 10                ; argument x dans RAX (registre 1)  
MOV     RBX, 20                ; argument y dans RBX (registre 2)  
CALL    main.add  
; RAX contient maintenant la valeur de retour
; (utilisation directe, plus besoin de lire la pile)

; --- add ---
; RAX = x (argument 1)
; RBX = y (argument 2)
ADD     RAX, RBX               ; RAX = x + y  
RET                             ; RAX = valeur de retour  
```

C'est radicalement plus compact. Mais pour un exemple avec retours multiples :

```go
func divide(a, b int) (int, error) { ... }
```

```asm
; Après CALL main.divide :
;   RAX = quotient (retour 1, entier → registre entier 1)
;   RBX = error    (retour 2, interface → registres entiers 2 et 3)
; Note : une interface Go occupe 2 mots (type pointer + data pointer),
; donc error consomme RBX et RCX.
```

### La zone de spill

Même avec l'ABI registre, Go maintient une « zone de spill » sur la pile. Lors de l'entrée dans une fonction, le compilateur peut copier (spill) certains registres d'arguments sur la pile. C'est nécessaire pour :

- les appels de runtime qui pourraient avoir besoin de trouver les arguments sur la pile (GC, stack growth),  
- les situations où le compilateur manque de registres (register pressure),  
- le débogage (les arguments spillés sont visibles dans les stack traces).

En assembleur, vous verrez souvent ce pattern juste après le préambule de vérification de pile :

```asm
; Spill des arguments registres vers la pile
MOV     [RSP+offset1], RAX     ; sauvegarde arg1  
MOV     [RSP+offset2], RBX     ; sauvegarde arg2  
; ... corps de la fonction ...
```

> 💡 **Astuce RE** : ces instructions de spill en début de fonction vous donnent un indice précieux sur le nombre et l'ordre des arguments, même si le décompilateur ne les reconstruit pas correctement. Comptez les `MOV [RSP+...], registre` consécutifs juste après le préambule de pile : chacun correspond probablement à un argument de la fonction.

---

## Le cas des structures et des interfaces

### Passage de structures

Les petites structures dont les champs sont tous de types simples sont « décomposées » et passées champ par champ dans les registres :

```go
type Point struct {
    X int
    Y int
}

func distance(p Point) float64 { ... }
```

Ici, `p.X` ira dans `RAX` et `p.Y` dans `RBX`, comme si la fonction avait deux arguments `int`. En RE, la structure n'est pas visible en tant que telle dans les registres — vous voyez deux entiers indépendants.

Les structures volumineuses ou contenant des types complexes (pointeurs, slices, interfaces) sont passées par pointeur implicite.

### Passage d'interfaces

Une interface Go est un couple de deux pointeurs (16 octets sur amd64) :

```
┌───────────────────┐
│  itable pointer   │  → pointe vers l'itab (table de méthodes + type)
├───────────────────┤
│  data pointer     │  → pointe vers la donnée concrète
└───────────────────┘
```

Quand une interface est passée en argument, elle consomme **deux registres** consécutifs :

```asm
; Appel d'une fonction acceptant une interface Validator (section 34.3)
; RAX = itable pointer
; RBX = data pointer
CALL    main.runValidator
```

C'est un point critique pour le RE : si vous voyez une fonction qui semble recevoir un « pointeur mystérieux » dans `RAX` suivi d'un autre dans `RBX`, pensez à une interface. Le dispatch de méthode passera ensuite par l'itab (voir section 34.3).

Pour les retours d'erreur en Go — pattern `(result, error)` — le même principe s'applique : `error` est une interface et consomme deux registres de retour.

---

## Fonctions variadiques et closures

### Fonctions variadiques

En Go, les fonctions variadiques (`func f(args ...int)`) reçoivent un slice. Le paramètre variadique est transformé par le compilateur en un argument de type `[]int`, qui occupe trois registres ou trois slots de pile :

```asm
; f(1, 2, 3) avec f(args ...int)
; Le compilateur crée un tableau [3]int sur la pile, puis passe un slice :
;   RAX = pointeur vers le tableau
;   RBX = longueur (3)
;   RCX = capacité (3)
CALL    main.f
```

### Closures

Une closure Go est implémentée comme un pointeur vers une structure contenant le pointeur de la fonction et les variables capturées :

```go
func makeAdder(n int) func(int) int {
    return func(x int) int {
        return x + n
    }
}
```

En assembleur, la closure est un bloc mémoire dont le premier champ est le pointeur vers le code de la fonction anonyme, suivi des variables capturées :

```
┌──────────────────┐
│  func pointer    │  → main.makeAdder.func1
├──────────────────┤
│  n (capturé)     │  → valeur ou pointeur selon l'analyse d'échappement
└──────────────────┘
```

À l'appel, le registre `RDX` pointe vers cette structure closure (dans la nouvelle ABI). La fonction anonyme accède aux variables capturées via des déréférencements relatifs à `RDX` :

```asm
; main.makeAdder.func1 :
; RDX = pointeur vers la closure
; RAX = argument x
MOV     RCX, [RDX+8]          ; RCX = n (variable capturée)  
ADD     RAX, RCX               ; return x + n  
RET  
```

> 💡 **Astuce RE** : le symbole d'une closure en Go suit le pattern `package.fonctionEnglobante.funcN` — par exemple `main.makeAdder.func1`. Ce nommage est préservé dans `gopclntab` et vous permet de rattacher chaque closure à sa fonction parent, même dans un binaire strippé.

---

## Identifier la version de l'ABI dans un binaire inconnu

Face à un binaire Go dont vous ne connaissez pas la version du compilateur, voici comment déterminer quelle convention d'appel est utilisée :

### Méthode 1 — Version du compilateur

```bash
strings binaire | grep -oP 'go1\.\d+'
```

Si le résultat est `go1.17` ou supérieur, l'ABI registre est active sur amd64. Sinon, c'est l'ancienne ABI pile.

### Méthode 2 — Inspection de l'assembleur

Prenez une petite fonction identifiable (par exemple `main.main` ou une fonction utilitaire) et observez comment elle accède à ses arguments :

- **Ancienne ABI** : les premiers accès mémoire après le préambule lisent `[RSP+8]`, `[RSP+16]`, etc.  
- **Nouvelle ABI** : le corps de la fonction utilise directement `RAX`, `RBX`, `RCX` sans les charger depuis la pile (hormis les spills éventuels).

### Méthode 3 — Symbole interne `internal/abi`

Les binaires Go ≥ 1.17 contiennent des symboles du package `internal/abi`. Leur présence dans `gopclntab` ou dans les strings est un indicateur fiable :

```bash
strings binaire | grep 'internal/abi'
```

### Méthode 4 — Examen du prologue de `runtime.rt0_go`

Le code de `runtime.rt0_go` diffère entre les versions. Sur les versions récentes, vous verrez des instructions de setup de `R14` comme registre `g` tôt dans le prologue — ce qui n'existe pas avant Go 1.17.

---

## Configurer Ghidra pour l'ABI Go

Par défaut, Ghidra applique la convention System V à toutes les fonctions. Sur un binaire Go, cela produit des signatures de fonctions incorrectes et un pseudo-code trompeur. Quelques ajustements :

### Pour l'ancienne ABI (pile)

1. Modifiez les fonctions analysées pour utiliser une convention custom « stack-only ».  
2. Définissez manuellement les paramètres comme des variables de pile aux offsets corrects.  
3. Marquez la valeur de retour comme étant sur la pile, pas dans `RAX`.

### Pour la nouvelle ABI (registres)

1. La séquence de registres Go (`RAX`, `RBX`, `RCX`, `RDI`, `RSI`, `R8`, `R9`, `R10`, `R11`) ne correspond à aucun preset Ghidra.  
2. Pour les fonctions importantes, éditez manuellement la signature dans le panneau Decompiler : clic droit → *Edit Function Signature*, puis assignez les registres corrects aux paramètres.  
3. Des scripts communautaires (comme ceux du projet `go-re-ghidra` ou `GoReSym`) automatisent partiellement ce travail en appliquant les signatures correctes à partir des métadonnées `gopclntab`.

> 💡 **Astuce RE** : ne cherchez pas à corriger la convention d'appel de *toutes* les fonctions. Concentrez-vous sur les fonctions du package `main.*` et des packages métier. Laisser les fonctions `runtime.*` avec des signatures incorrectes est acceptable — vous n'avez généralement pas besoin de les décompiler proprement.

---

## Récapitulatif comparatif

| Caractéristique | Go ancienne ABI (< 1.17) | Go nouvelle ABI (≥ 1.17) | System V AMD64 (C) |  
|---|---|---|---|  
| Arguments entiers | Pile uniquement | `RAX`, `RBX`, `RCX`, `RDI`, `RSI`, `R8`-`R11` | `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9` |  
| Retours | Pile uniquement | `RAX`, `RBX`, `RCX`, `RDI`, `RSI`, `R8`-`R11` | `RAX`, `RDX` |  
| Retours multiples | Natif (via pile) | Natif (via registres) | Non natif |  
| Frame pointer | Absent (puis optionnel) | Optionnel (`RBP`) | `RBP` (conventionnel) |  
| Registre `g` (goroutine) | TLS | `R14` | — |  
| Red zone | Non | Non | 128 octets |  
| Caller-cleanup | Oui | Oui | Oui |  
| Spill zone | — | Oui (arguments copiés sur pile) | — |

---

## Ce qu'il faut retenir

1. **Ne supposez jamais System V.** Dès que vous identifiez un binaire Go, oubliez `RDI`/`RSI` comme arguments et `RAX` comme unique retour (sauf coïncidence avec la nouvelle ABI pour le premier retour).  
2. **Identifiez la version du compilateur** en premier. La chaîne `go1.XX` dans les strings du binaire détermine l'ABI applicable.  
3. **Les retours multiples sont normaux.** Un `(int, error)` consomme potentiellement 3 registres de retour (un pour l'int, deux pour l'interface error).  
4. **Comptez les spills.** Les `MOV [RSP+...], REG` en début de fonction révèlent le nombre d'arguments.  
5. **Les interfaces consomment deux registres.** Chaque paramètre ou retour de type interface « coûte » deux slots dans la séquence de registres.  
6. **Adaptez Ghidra.** Corrigez manuellement les signatures des fonctions clés, ou utilisez les outils automatisés décrits en section 34.4.

⏭️ [Structures de données Go en mémoire : slices, maps, interfaces, channels](/34-re-go/03-structures-donnees-memoire.md)
