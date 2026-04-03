🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.4 — Comprendre les sauts conditionnels (`jz`/`jnz`) dans le contexte du crackme

> 📖 **Rappel** : les sauts conditionnels et inconditionnels x86-64 ont été présentés au chapitre 3 (section 3.4). Cette section se concentre sur leur rôle spécifique dans un crackme et sur les pièges de lecture qu'ils posent au reverse engineer.

---

## Introduction

La section 21.3 a localisé deux sauts conditionnels critiques : un `JZ` dans `main` (après l'appel à `check_license`) et un `JNE` dans `check_license` (après l'appel à `strcmp`). Ces deux instructions de quelques octets sont le mécanisme central de tout crackme : elles matérialisent, en langage machine, la question « la clé est-elle valide ? ».

Comprendre précisément *pourquoi* le processeur prend ou ne prend pas un saut, c'est comprendre la logique du programme. Cette section décortique la mécanique des sauts conditionnels dans le contexte concret de notre keygenme, en remontant du code C jusqu'aux flags du processeur.

---

## Du C aux flags : la chaîne complète

Pour saisir le fonctionnement d'un saut conditionnel, il faut comprendre les trois maillons de la chaîne :

```
Code C (condition logique)
    ↓  compilation GCC
Instruction de test (TEST, CMP)  →  positionne les flags dans RFLAGS
    ↓  immédiatement après
Instruction de saut (Jcc)        →  lit un ou plusieurs flags pour décider
```

Le processeur ne « comprend » pas les conditions C. Il ne sait pas ce qu'est un `if` ou un `==`. Il connaît uniquement des **flags** — des bits individuels dans le registre `RFLAGS` — qui sont positionnés par certaines instructions arithmétiques et logiques, puis lus par les instructions de saut conditionnel.

### Les flags pertinents

Parmi la vingtaine de flags de `RFLAGS`, trois sont omniprésents dans l'analyse de crackmes :

| Flag | Nom | Positionné à 1 quand… |  
|---|---|---|  
| **ZF** | Zero Flag | Le résultat de la dernière opération est zéro |  
| **SF** | Sign Flag | Le résultat de la dernière opération est négatif (bit de poids fort = 1) |  
| **CF** | Carry Flag | Une retenue s'est produite (débordement non signé) |

Pour notre keygenme, seul le **Zero Flag (ZF)** intervient. Les sauts `JZ` et `JNZ` (et leurs alias `JE`/`JNE`) testent exclusivement ce flag.

---

## Premier point de décision : `strcmp` dans `check_license`

### Le code C original

```c
if (strcmp(expected, user_key) == 0) {
    return 1;
}
return 0;
```

La fonction `strcmp` retourne :
- **0** si les deux chaînes sont identiques.  
- Une valeur **positive** si la première chaîne est lexicographiquement supérieure.  
- Une valeur **négative** si la première chaîne est lexicographiquement inférieure.

Le `== 0` du code C teste l'égalité des chaînes. Voyons comment GCC traduit cela.

### La traduction assembleur

```nasm
CALL    strcmp@plt          ; retour dans EAX  
TEST    EAX, EAX           ; EAX AND EAX → positionne ZF  
JNE     .return_zero        ; si ZF = 0 (EAX ≠ 0) → saut vers échec  
```

#### `CALL strcmp@plt`

L'appel à `strcmp` place son résultat dans `EAX` (convention System V AMD64 : la valeur de retour entière est dans `RAX`/`EAX`). Après cet appel :
- Si les chaînes sont identiques : `EAX = 0`.  
- Sinon : `EAX ≠ 0` (valeur positive ou négative).

#### `TEST EAX, EAX`

L'instruction `TEST` effectue un **AND logique** entre ses deux opérandes sans stocker le résultat — elle ne modifie que les flags. Quand les deux opérandes sont le même registre (`TEST EAX, EAX`), l'opération revient à `EAX & EAX`, ce qui donne… `EAX` lui-même. L'intérêt n'est pas dans le résultat (qui est jeté) mais dans l'effet sur les flags :

- Si `EAX = 0` : le résultat du AND est 0 → **ZF = 1**.  
- Si `EAX ≠ 0` : le résultat du AND est non nul → **ZF = 0**.

`TEST EAX, EAX` est l'idiome standard de GCC pour tester si un registre est nul. On le rencontre dans quasiment tout binaire compilé avec GCC. C'est l'équivalent machine de la question « cette valeur est-elle zéro ? ».

> 💡 **Pourquoi `TEST` et pas `CMP EAX, 0` ?** Les deux positionneraient les flags de la même façon. Mais `TEST EAX, EAX` est encodé sur 2 octets (`85 C0`) alors que `CMP EAX, 0` en nécessite 5 (`83 F8 00` ou plus). GCC préfère toujours la forme la plus compacte. Le reverse engineer doit reconnaître les deux formes comme sémantiquement identiques.

#### `JNE .return_zero`

`JNE` (Jump if Not Equal) est un alias de `JNZ` (Jump if Not Zero). Les deux mnémoniques désignent le même opcode (`0x75` pour la forme courte). L'instruction saute si **ZF = 0**.

Combinons :

| `EAX` (retour `strcmp`) | Signification | `TEST EAX, EAX` → ZF | `JNE` pris ? | Conséquence |  
|---|---|---|---|---|  
| `0` | Chaînes identiques | ZF = 1 | **Non** (continue) | → `return 1` (succès) |  
| `≠ 0` | Chaînes différentes | ZF = 0 | **Oui** (saute) | → `return 0` (échec) |

Le saut est pris quand la clé est **mauvaise**. Quand la clé est **bonne**, l'exécution tombe séquentiellement dans le chemin de succès.

---

## Second point de décision : `check_license` dans `main`

### Le code C original

```c
if (check_license(username, user_key)) {
    printf(MSG_OK, username);
    return EXIT_SUCCESS;
} else {
    printf(MSG_FAIL);
    return EXIT_FAILURE;
}
```

En C, la condition `if (check_license(...))` est vraie quand la valeur de retour est **non nulle** (convention C : tout entier non nul est « vrai »).

### La traduction assembleur

```nasm
CALL    check_license       ; retour dans EAX  
TEST    EAX, EAX            ; positionne ZF  
JZ      .label_fail          ; si ZF = 1 (EAX = 0) → saut vers échec  
```

Ici, c'est un `JZ` (Jump if Zero, alias `JE`) — l'inverse du `JNE` dans `check_license`. Le saut est pris quand `EAX = 0`, c'est-à-dire quand `check_license` a retourné 0 (clé invalide).

| `EAX` (retour `check_license`) | Signification | `TEST EAX, EAX` → ZF | `JZ` pris ? | Conséquence |  
|---|---|---|---|---|  
| `1` | Clé valide | ZF = 0 | **Non** (continue) | → `printf(MSG_OK)` |  
| `0` | Clé invalide | ZF = 1 | **Oui** (saute) | → `printf(MSG_FAIL)` |

---

## Les deux sauts face à face

Mettons les deux points de décision côte à côte pour bien visualiser leur articulation :

```
┌──────────────────────────────────────────────────────────┐
│                    check_license()                       │
│                                                          │
│    CALL strcmp@plt                                       │
│    TEST EAX, EAX                                         │
│    JNE  .return_zero     ←── saut pris si clé MAUVAISE   │
│    MOV  EAX, 1           ←── clé bonne : retourne 1      │
│    JMP  .epilogue                                        │
│  .return_zero:                                           │
│    MOV  EAX, 0           ←── clé mauvaise : retourne 0   │
│  .epilogue:                                              │
│    ... (canary check)                                    │
│    RET                                                   │
└──────────────────────────────────────────────────────────┘
                          │
                    retour dans main
                          ▼
┌──────────────────────────────────────────────────────────┐
│                        main()                            │
│                                                          │
│    CALL check_license                                    │
│    TEST EAX, EAX                                         │
│    JZ   .label_fail      ←── saut pris si EAX = 0        │
│    ... printf(MSG_OK)    ←── chemin succès               │
│    JMP  .end                                             │
│  .label_fail:                                            │
│    ... printf(MSG_FAIL)  ←── chemin échec                │
│  .end:                                                   │
│    ...                                                   │
│    RET                                                   │
└──────────────────────────────────────────────────────────┘
```

L'enchaînement est limpide :
1. `strcmp` retourne 0 → le `JNE` n'est **pas** pris → `check_license` retourne 1.  
2. `check_license` retourne 1 → le `JZ` n'est **pas** pris → on affiche le message de succès.

Dans les deux cas, le chemin de **succès** est le chemin **séquentiel** (pas de saut). Le chemin d'**échec** est le chemin où le saut est pris. C'est un pattern extrêmement courant dans les binaires GCC : le compilateur place le cas « normal » ou « attendu » en séquentiel et le cas « exceptionnel » en cible de saut. Connaître cette convention facilite la lecture rapide du désassemblage.

---

## Les alias `JE`/`JNE` vs `JZ`/`JNZ`

En parcourant différents outils, vous rencontrerez indifféremment `JZ` ou `JE`, `JNZ` ou `JNE`. Ce sont des **alias exacts** pour le même opcode :

| Opcode | Alias 1 | Alias 2 | Condition | Flag testé |  
|---|---|---|---|---|  
| `0x74` (court) / `0x0F 0x84` (near) | `JZ` (Jump if Zero) | `JE` (Jump if Equal) | ZF = 1 | ZF |  
| `0x75` (court) / `0x0F 0x85` (near) | `JNZ` (Jump if Not Zero) | `JNE` (Jump if Not Equal) | ZF = 0 | ZF |

Le choix de l'alias dépend du désassembleur :
- **`objdump`** utilise `je`/`jne` par défaut.  
- **Ghidra** utilise `JZ`/`JNZ` dans certaines versions et `JE`/`JNE` dans d'autres.  
- **IDA** utilise `jz`/`jnz`.  
- **Radare2** utilise `je`/`jne`.

Quelle que soit la graphie, le comportement est identique. Le reverse engineer doit associer mentalement les deux formes sans hésitation.

---

## `CMP` vs `TEST` : deux façons de préparer un saut

Notre keygenme utilise `TEST EAX, EAX` avant les sauts, mais on rencontre fréquemment `CMP` dans d'autres binaires. Les deux instructions positionnent les flags, mais pas de la même façon.

### `TEST A, B` — AND logique sans stockage

```nasm
TEST    EAX, EAX     ; calcule EAX & EAX, positionne ZF, jette le résultat
```

Usage principal : **tester si un registre est nul** (quand A = B).

### `CMP A, B` — Soustraction sans stockage

```nasm
CMP     EAX, 0x5     ; calcule EAX - 5, positionne ZF/SF/CF, jette le résultat
```

Usage principal : **comparer deux valeurs**. Après `CMP A, B` :
- `JE` / `JZ` : saut si A = B (résultat de la soustraction = 0 → ZF = 1).  
- `JNE` / `JNZ` : saut si A ≠ B.  
- `JL` / `JNGE` : saut si A < B (comparaison signée).  
- `JG` / `JNLE` : saut si A > B (comparaison signée).  
- `JB` / `JNAE` : saut si A < B (comparaison non signée).  
- `JA` / `JNBE` : saut si A > B (comparaison non signée).

### Dans notre keygenme

GCC utilise `TEST EAX, EAX` parce que la condition C est un simple test de nullité (`== 0` ou valeur booléenne). Si le code C contenait une comparaison contre une constante non nulle (par exemple `if (result == 42)`), GCC utiliserait `CMP EAX, 0x2A` suivi de `JE`/`JNE`.

> 💡 **Règle empirique** : quand on voit `TEST reg, reg` → le code C teste si la variable est nulle ou non nulle. Quand on voit `CMP reg, imm` → le code C compare contre une valeur spécifique. Cette correspondance permet de reconstruire mentalement le `if` original.

---

## Variantes selon le niveau d'optimisation

Le couple `TEST`/`Jcc` que nous avons analysé est typique de `-O0`. Avec des niveaux d'optimisation supérieurs, GCC peut réorganiser le code de façon significative.

### En `-O2` : fusion et inversion

GCC en `-O2` peut fusionner l'appel à `strcmp` et le test dans un code plus compact :

```nasm
CALL    strcmp@plt  
TEST    EAX, EAX  
SETE    AL              ; AL = 1 si ZF=1 (chaînes égales), 0 sinon  
MOVZX   EAX, AL         ; extension en 32 bits  
RET  
```

Ici, le compilateur a remplacé le branchement `JNE` + deux chemins `MOV EAX, 1`/`MOV EAX, 0` par une instruction `SETE` (Set byte if Equal) qui produit directement le résultat 0 ou 1 sans saut. Le code est plus court, sans branchement, et donc plus rapide — mais moins lisible pour le débutant en RE.

L'instruction `SETE` (et ses variantes `SETNE`, `SETL`, `SETG`…) est un pattern fréquent en `-O2`/`-O3`. Elle remplace un embranchement conditionnel par une affectation conditionnelle. Le reverse engineer doit la reconnaître comme l'équivalent de :

```c
return (strcmp(expected, user_key) == 0) ? 1 : 0;
// ou simplement :
return strcmp(expected, user_key) == 0;
```

### En `-O2` : inlining de `check_license`

Si GCC décide d'inliner `check_license` dans `main`, la frontière entre les deux fonctions disparaît. On se retrouve avec un seul bloc de code dans `main` qui enchaîne `compute_hash` → `derive_key` → `format_key` → `strcmp` → `TEST`/`JZ`. La logique est la même, mais il n'y a plus de `CALL check_license` à repérer. On doit alors se fier aux appels restants (`strcmp@plt` n'est pas inlinable car il fait partie de la libc) et aux chaînes de caractères pour localiser le point de décision.

### En `-O3` : CMOV (déplacement conditionnel)

À des niveaux d'optimisation agressifs, GCC peut utiliser `CMOVZ`/`CMOVNZ` (Conditional Move) pour éviter complètement les branchements :

```nasm
CALL    strcmp@plt  
XOR     ECX, ECX         ; ECX = 0  
TEST    EAX, EAX  
MOV     EAX, 0x1  
CMOVNE  EAX, ECX         ; si strcmp ≠ 0, EAX ← 0  
RET  
```

Il n'y a aucun saut conditionnel dans ce code — la valeur de retour est calculée de façon linéaire. C'est plus performant (pas de misprediction de branchement), mais il n'y a plus de `JZ`/`JNE` à inverser pour un patch. On devrait patcher le `CMOVNE` en `NOP` ou modifier la logique autrement. Nous verrons les techniques de patching adaptées en section 21.6.

---

## Piège classique : confondre le sens du saut

Le piège le plus fréquent pour le débutant est de **confondre « le saut mène au succès » avec « le saut mène à l'échec »**. La seule façon de ne pas se tromper est de lire le contexte complet :

1. Identifier ce qui se passe **si le saut est pris** (la cible du saut).  
2. Identifier ce qui se passe **si le saut n'est pas pris** (l'instruction suivante, exécution séquentielle).  
3. Déterminer lequel de ces deux chemins affiche le message de succès.

Ne jamais raisonner sur le mnémonique seul. Un `JNZ` peut mener au succès ou à l'échec — tout dépend de la façon dont le compilateur a agencé le code. Voici deux agencements possibles pour la même condition C :

**Agencement A** (le plus courant avec GCC) :
```nasm
TEST    EAX, EAX  
JZ      .fail           ; saut → échec  
; ... code de succès ...
.fail:
; ... code d'échec ...
```

**Agencement B** (possible avec un autre compilateur ou avec `-O2`) :
```nasm
TEST    EAX, EAX  
JNZ     .success        ; saut → succès  
; ... code d'échec ...
JMP     .end
.success:
; ... code de succès ...
.end:
```

Les deux représentent le même `if (check_license()) { succès } else { échec }`, mais avec des sauts inversés. Seule la lecture du contexte (les instructions après le saut et à la cible du saut) permet de trancher.

---

## Récapitulatif : les opcodes à connaître

Pour préparer les sections suivantes (patching en 21.6, observation en GDB en 21.5), voici les opcodes des sauts conditionnels qui apparaissent dans notre keygenme :

| Mnémonique | Opcode (court, rel8) | Opcode (near, rel32) | Condition | Usage typique |  
|---|---|---|---|---|  
| `JZ` / `JE` | `74 xx` | `0F 84 xx xx xx xx` | ZF = 1 | Test de nullité, égalité |  
| `JNZ` / `JNE` | `75 xx` | `0F 85 xx xx xx xx` | ZF = 0 | Test de non-nullité, inégalité |  
| `JMP` | `EB xx` | `E9 xx xx xx xx` | Inconditionnel | Saut toujours pris |  
| `NOP` | `90` | — | — | Instruction sans effet |

La forme courte (`74 xx`, `75 xx`) encode un déplacement relatif sur 1 octet signé (portée : -128 à +127 octets). La forme near encode un déplacement sur 4 octets signés (portée : ±2 Go). GCC utilise la forme courte quand la cible du saut est proche et la forme near quand elle est distante.

Pour le patching (section 21.6), on retiendra que :
- Changer `74` en `75` (ou inversement) **inverse** la condition du saut.  
- Changer `74 xx` en `EB xx` transforme le saut conditionnel en **saut inconditionnel** (toujours pris).  
- Changer `74 xx` en `90 90` remplace le saut par deux **NOP** (jamais pris — l'exécution continue séquentiellement).

Ces modifications d'un ou deux octets sont la base du patching de crackmes, et elles découlent directement de la compréhension mécanique des sauts conditionnels développée dans cette section.

---

## Synthèse

Les sauts conditionnels sont le point de bascule de tout crackme. Voici ce qu'il faut retenir :

- `TEST reg, reg` positionne ZF = 1 si le registre est nul, ZF = 0 sinon. C'est l'idiome GCC pour tester une valeur booléenne ou un retour de fonction.  
- `JZ`/`JE` saute quand ZF = 1 (valeur nulle, chaînes égales). `JNZ`/`JNE` saute quand ZF = 0 (valeur non nulle, chaînes différentes).  
- Le **sens** du saut (succès ou échec) dépend de l'agencement du code, pas du mnémonique. Toujours lire la cible du saut et le chemin séquentiel pour déterminer lequel est lequel.  
- En `-O2`/`-O3`, les sauts conditionnels peuvent être remplacés par `SETcc` ou `CMOVcc`, éliminant le branchement explicite. Le reverse engineer doit reconnaître ces patterns comme des formes optimisées du même `if`.  
- Les opcodes `74`/`75`/`EB`/`90` sont les clés du patching — on les retrouvera en section 21.6.

La mécanique est comprise. La section suivante (21.5) va la confirmer en temps réel : on posera un breakpoint sur le `strcmp` dans GDB et on observera directement les valeurs dans les registres au moment de la comparaison.

⏭️ [Analyse dynamique : tracer la comparaison avec GDB](/21-keygenme/05-analyse-dynamique-gdb.md)
