🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 18.4 — Z3 Theorem Prover — modéliser des contraintes extraites manuellement

> **Chapitre 18 — Exécution symbolique et solveurs de contraintes**  
> Partie IV — Techniques Avancées de RE

---

## Pourquoi utiliser Z3 directement ?

Dans la section 18.3, angr a fait tout le travail : charger le binaire, propager les expressions symboliques, bifurquer aux branchements, et interroger le solveur. Vous n'avez jamais touché à Z3 directement — angr s'en chargeait via claripy.

Mais angr n'est pas toujours la bonne réponse. Il y a des situations où l'exécution symbolique automatique échoue ou n'est tout simplement pas adaptée :

- Le binaire est **trop gros** ou trop ramifié pour qu'angr l'explore en un temps raisonnable.  
- Le code d'intérêt ne représente qu'une **petite fraction** du programme, noyée dans des milliers de fonctions sans rapport.  
- Vous avez déjà **compris la logique** grâce à une analyse statique dans Ghidra (chapitre 8) et vous voulez simplement résoudre le système d'équations que vous avez extrait.  
- Le binaire utilise des **anti-reversing** (chapitre 19) qui perturbent angr : code auto-modifiant, détection de débogueur, obfuscation de flux de contrôle.  
- Vous travaillez sur un **fragment de code** isolé — une routine crypto, un algorithme de vérification de licence — et non sur un programme complet.

Dans tous ces cas, l'approche est différente : vous analysez le binaire manuellement (ou avec un décompileur), vous **extrayez les contraintes** sous forme d'équations mathématiques, puis vous les soumettez directement à Z3. C'est un travail hybride — moitié reverse engineering classique, moitié modélisation mathématique — et c'est souvent la méthode la plus efficace face à des binaires complexes.

---

## Z3 en bref

Z3 est un **solveur SMT** (Satisfiability Modulo Theories) développé par Microsoft Research. « SMT » signifie qu'il sait vérifier la satisfiabilité de formules logiques dans le cadre de théories mathématiques spécifiques : arithmétique des entiers bornés (bitvectors), tableaux, nombres réels, etc.

En termes simples : vous lui décrivez un système de contraintes (« `x` XOR `y` doit valoir `0x1337`, et `x` multiplié par 3 doit être inférieur à `0xFFFF` »), et il vous dit si une solution existe. Si oui, il vous la donne.

### Installation

Z3 est déjà installé si vous avez installé angr (il est tiré comme dépendance). Sinon :

```bash
pip install z3-solver
```

> ⚠️ Le paquet PyPI s'appelle `z3-solver`, pas `z3`. Un paquet nommé `z3` existe mais c'est un projet différent et sans rapport.

### Premier contact

```python
from z3 import *

# Déclarer deux entiers symboliques
x = Int("x")  
y = Int("y")  

# Créer un solveur
s = Solver()

# Ajouter des contraintes
s.add(x + y == 42)  
s.add(x - y == 10)  

# Résoudre
if s.check() == sat:
    m = s.model()
    print(f"x = {m[x]}")   # x = 26
    print(f"y = {m[y]}")   # y = 16
else:
    print("Pas de solution")
```

C'est de l'algèbre élémentaire, mais le mécanisme est exactement le même pour des contraintes sur des bitvectors de 64 bits avec des opérations XOR, shift et multiplication — simplement, vous ne pourriez pas les résoudre à la main.

---

## Bitvectors : le type fondamental pour le RE

En reverse engineering, vous ne travaillez presque jamais avec des entiers mathématiques abstraits (`Int`). Vous travaillez avec des registres de taille fixe — 8, 16, 32 ou 64 bits — où l'arithmétique **déborde** (*wraps around*). Un `uint32_t` qui vaut `0xFFFFFFFF` et auquel on ajoute `1` donne `0x00000000`, pas `0x100000000`.

Z3 modélise ce comportement avec les **bitvectors** (`BitVec`), qui reproduisent exactement l'arithmétique du processeur :

```python
from z3 import *

# Bitvector de 32 bits nommé "x"
x = BitVec("x", 32)

# Bitvector constant
magic = BitVecVal(0xDEADBEEF, 32)

# Arithmétique modulaire (mod 2^32 implicite)
expr = x + BitVecVal(1, 32)   # Overflow géré comme sur le CPU

# Opérations bit à bit
expr2 = x ^ magic              # XOR  
expr3 = LShR(x, 16)           # Logical Shift Right  
expr4 = x << 3                 # Shift Left  
expr5 = x & 0xFF              # AND (masquage)  
```

### Signé vs non signé

En assembleur x86-64, la même séquence de bits peut être interprétée comme signée ou non signée — c'est l'instruction qui détermine l'interprétation (`ja` vs `jg`, `shr` vs `sar`…). Z3 reproduit cette distinction :

```python
x = BitVec("x", 32)

# Comparaisons non signées (par défaut)
UGT(x, 100)    # Unsigned Greater Than  
ULT(x, 100)    # Unsigned Less Than  
UGE(x, 100)    # Unsigned Greater or Equal  
ULE(x, 100)    # Unsigned Less or Equal  

# Comparaisons signées
x > 100         # Signé (opérateur Python natif)  
x < 100         # Signé  

# Shift
LShR(x, 4)     # Logical Shift Right (non signé, insère des 0)  
x >> 4          # Arithmetic Shift Right (signé, propage le bit de signe)  
```

La distinction `LShR` (logique) vs `>>` (arithmétique) est un piège classique. En C, `>>` sur un `unsigned` est logique, sur un `signed` est arithmétique. En Z3, `>>` est **toujours arithmétique**. Quand vous traduisez du désassemblage, vérifiez si l'instruction est `shr` (logique → `LShR`) ou `sar` (arithmétique → `>>`).

---

## Workflow : du désassemblage aux contraintes Z3

Le processus complet se décompose en quatre étapes :

```
  Binaire ELF
       │
       ▼
  ┌───────────────────────┐
  │  Analyse statique     │    Ghidra, objdump, IDA...
  │  (décompilation)      │    → Comprendre la logique
  └──────────┬────────────┘
             │
             ▼
  ┌───────────────────────┐
  │  Extraction des       │    Identifier les opérations sur
  │  contraintes          │    les entrées et la condition finale
  └──────────┬────────────┘
             │
             ▼
  ┌───────────────────────┐
  │  Modélisation Z3      │    Traduire chaque opération en
  │  (script Python)      │    expression Z3 sur des BitVec
  └──────────┬────────────┘
             │
             ▼
  ┌───────────────────────┐
  │  Résolution           │    s.check() → s.model()
  │  + Vérification       │    Tester la solution sur le binaire
  └───────────────────────┘
```

Appliquons cela à notre keygenme. Supposons que vous avez ouvert `keygenme_O2_strip` dans Ghidra et que le décompileur vous montre quelque chose comme ceci (noms de variables ajustés manuellement après renommage dans Ghidra) :

```c
// Pseudo-code Ghidra (nettoyé)
uint32_t high = parse_hex_8chars(serial);  
uint32_t low  = parse_hex_8chars(serial + 8);  

// Tour 1
uint32_t tmp = low;  
uint32_t v = low ^ 0x5a3ce7f1;  
v = ((v >> 16) ^ v) * 0x45d9f3b;  
v = ((v >> 16) ^ v) * 0x45d9f3b;  
v = (v >> 16) ^ v;  
low = high ^ v;  
high = tmp;  

// Tour 2
tmp = low;  
v = low ^ 0x1f4b8c2d;  
v = ((v >> 16) ^ v) * 0x45d9f3b;  
v = ((v >> 16) ^ v) * 0x45d9f3b;  
v = (v >> 16) ^ v;  
low = high ^ v;  
high = tmp;  

// Tour 3 (même pattern, seed = 0xdead1337)
// Tour 4 (même pattern, seed = 0x8badf00d)

if (high == 0xa11c3514 && low == 0xf00dcafe) {
    puts("Access Granted!");
}
```

Vous n'avez pas besoin du code source pour arriver à ce pseudo-code — c'est exactement ce que Ghidra produit (chapitre 8), moyennant un peu de renommage et de nettoyage.

---

## Modélisation complète du keygenme en Z3

Traduisons ce pseudo-code en contraintes Z3. Chaque opération C devient son équivalent Z3, en respectant scrupuleusement les tailles de bitvectors et les types de shifts :

```python
#!/usr/bin/env python3
"""
solve_keygenme_z3.py — Résolution du keygenme avec Z3 seul  
Chapitre 18.4  

Les contraintes sont extraites manuellement depuis le pseudo-code  
produit par Ghidra (ou tout autre décompileur).  
"""

from z3 import *

# ================================================================
# 1. Déclarer les inconnues : les deux moitiés 32 bits du serial
# ================================================================

# high et low AVANT le réseau de Feistel (= les valeurs d'entrée)
high_in = BitVec("high_in", 32)  
low_in  = BitVec("low_in", 32)  

# ================================================================
# 2. Modéliser la fonction mix32
# ================================================================

def mix32(v, seed):
    """Reproduction exacte de mix32 en Z3."""
    v = v ^ seed
    v = (LShR(v, 16) ^ v) * BitVecVal(0x45d9f3b, 32)
    v = (LShR(v, 16) ^ v) * BitVecVal(0x45d9f3b, 32)
    v = LShR(v, 16) ^ v
    return v

# ================================================================
# 3. Modéliser le réseau de Feistel à 4 tours
# ================================================================

MAGIC_A = BitVecVal(0x5a3ce7f1, 32)  
MAGIC_B = BitVecVal(0x1f4b8c2d, 32)  
MAGIC_C = BitVecVal(0xdead1337, 32)  
MAGIC_D = BitVecVal(0x8badf00d, 32)  

def feistel4(high, low):
    """4 tours de Feistel — traduit instruction par instruction."""

    # Tour 1
    tmp = low
    low = high ^ mix32(low, MAGIC_A)
    high = tmp

    # Tour 2
    tmp = low
    low = high ^ mix32(low, MAGIC_B)
    high = tmp

    # Tour 3
    tmp = low
    low = high ^ mix32(low, MAGIC_C)
    high = tmp

    # Tour 4
    tmp = low
    low = high ^ mix32(low, MAGIC_D)
    high = tmp

    return high, low

# ================================================================
# 4. Appliquer la transformation et poser la contrainte finale
# ================================================================

high_out, low_out = feistel4(high_in, low_in)

s = Solver()

# La condition de succès extraite du binaire
s.add(high_out == BitVecVal(0xa11c3514, 32))  
s.add(low_out  == BitVecVal(0xf00dcafe, 32))  

# ================================================================
# 5. Résoudre
# ================================================================

if s.check() == sat:
    m = s.model()
    h = m[high_in].as_long()
    l = m[low_in].as_long()
    serial = f"{h:08x}{l:08x}"
    print(f"[+] high_in = 0x{h:08x}")
    print(f"[+] low_in  = 0x{l:08x}")
    print(f"[+] Serial  = {serial}")
else:
    print("[-] Aucune solution (UNSAT)")
```

### Exécution

```bash
$ python3 solve_keygenme_z3.py
[+] high_in = 0x7f3a1b9e
[+] low_in  = 0x5c82d046
[+] Serial  = 7f3a1b9e5c82d046

$ ./keygenme_O2_strip 7f3a1b9e5c82d046
Access Granted!
```

Le résultat est identique à celui obtenu avec angr — et c'est logique, puisque les contraintes sont les mêmes. Mais la résolution est **quasi instantanée** (quelques millisecondes contre plusieurs dizaines de secondes pour angr), car Z3 n'a pas eu à charger le binaire, à simuler l'exécution, ni à gérer des SimProcedures. Il a résolu directement le système d'équations.

---

## Comparer les deux approches

| Critère | angr (section 18.3) | Z3 direct (cette section) |  
|---|---|---|  
| **Effort humain** | Minimal — il suffit de connaître les chaînes de succès/échec | Significatif — il faut comprendre et traduire la logique |  
| **Temps de résolution** | Secondes à minutes | Millisecondes |  
| **Connaissance du binaire requise** | Presque aucune | Bonne compréhension de la routine cible |  
| **Robustesse face aux gros binaires** | Peut exploser en chemins | N'est pas affecté par la taille du binaire |  
| **Risque d'erreur** | Faible (angr traduit fidèlement le binaire) | Réel (erreur de traduction humaine possible) |  
| **Réutilisabilité** | Le même script marche sur d'autres crackmes similaires | Le script est spécifique à ce binaire |

En pratique, les deux approches sont **complémentaires**. angr est idéal pour un premier essai rapide. Si angr échoue ou est trop lent, on passe à Z3 en extrayant les contraintes manuellement depuis le décompileur.

---

## Techniques de modélisation courantes

Au-delà du keygenme, voici les patterns Z3 que vous rencontrerez le plus souvent en RE.

### Modéliser une table de lookup (S-box, substitution)

Beaucoup d'algorithmes crypto utilisent des tables de substitution. En assembleur, cela se traduit par un accès indexé en mémoire (`movzx eax, byte [rsi + rax]`). En Z3, on utilise un `Array` ou une cascade de `If` :

```python
# Table de substitution (extraite du binaire avec Ghidra ou un script)
sbox = [0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
        # ... 256 entrées au total
       ]

def lookup_sbox(index):
    """Modélise sbox[index] pour un index symbolique 8 bits."""
    result = BitVecVal(sbox[0], 8)
    for i in range(1, 256):
        result = If(index == i, BitVecVal(sbox[i], 8), result)
    return result

x = BitVec("x", 8)  
y = lookup_sbox(x)  
```

La cascade de `If` est verbeuse mais le solveur l'optimise efficacement. Pour des tables plus grandes, l'approche `Array` de Z3 est plus adaptée :

```python
# Avec un Array Z3
SBox = Array("SBox", BitVecSort(8), BitVecSort(8))

s = Solver()
# Contraindre chaque entrée du tableau
for i in range(256):
    s.add(Select(SBox, BitVecVal(i, 8)) == BitVecVal(sbox[i], 8))

# Utiliser le tableau
x = BitVec("x", 8)  
y = Select(SBox, x)  
```

### Modéliser une boucle à bornes connues

Si une boucle itère un nombre fixe de fois (ce qui est fréquent dans les algorithmes crypto — nombre de tours fixe), il suffit de la **dérouler** en Z3 :

```python
# Boucle : for (int i = 0; i < 4; i++) { state ^= keys[i]; state = rotate(state); }
state = BitVec("input", 32)  
keys = [0x11111111, 0x22222222, 0x33333333, 0x44444444]  

for i in range(4):
    state = state ^ BitVecVal(keys[i], 32)
    state = RotateLeft(state, 7)

# state contient maintenant une expression symbolique fonction de "input"
```

Le déroulage est trivial en Python — c'est une boucle Python qui construit une expression Z3 de plus en plus complexe. Le solveur n'a aucun problème avec la profondeur de l'expression.

### Modéliser des contraintes sur des chaînes

Si le serial est une chaîne ASCII avec des contraintes de format (lettres uniquement, chiffres uniquement, alphanumériques…), on crée un bitvector de 8 bits par caractère :

```python
# Serial de 8 caractères alphanumériques
serial = [BitVec(f"c{i}", 8) for i in range(8)]

s = Solver()  
for c in serial:  
    is_digit = And(c >= 0x30, c <= 0x39)       # '0'-'9'
    is_upper = And(c >= 0x41, c <= 0x5a)       # 'A'-'Z'
    is_lower = And(c >= 0x61, c <= 0x7a)       # 'a'-'z'
    s.add(Or(is_digit, is_upper, is_lower))
```

### Modéliser une comparaison `strcmp` / `memcmp`

Quand le binaire compare le résultat d'une transformation avec une chaîne fixe, c'est une contrainte d'égalité octet par octet :

```python
# Le binaire compare le résultat transformé avec "VALID_KEY"
expected = b"VALID_KEY"  
transformed = [some_function(serial[i]) for i in range(len(expected))]  

for i, byte_val in enumerate(expected):
    s.add(transformed[i] == byte_val)
```

### Modéliser un CRC ou un checksum

Les checksums sont courants dans les routines de vérification. Un CRC-32 classique se modélise par déroulage de la boucle bit par bit, en utilisant le polynôme comme constante :

```python
POLY = BitVecVal(0xEDB88320, 32)

def crc32_z3(data_bytes, length):
    """CRC-32 symbolique sur une liste de BitVec(8)."""
    crc = BitVecVal(0xFFFFFFFF, 32)
    for i in range(length):
        crc = crc ^ ZeroExt(24, data_bytes[i])  # Étendre 8 bits → 32 bits
        for _ in range(8):
            mask = If(crc & 1 == BitVecVal(1, 32),
                      BitVecVal(0xFFFFFFFF, 32),
                      BitVecVal(0, 32))
            crc = LShR(crc, 1) ^ (POLY & mask)
    return crc ^ BitVecVal(0xFFFFFFFF, 32)
```

> ⚠️ Ce CRC-32 symbolique fonctionne mais peut être lent à résoudre pour des entrées longues. Chaque bit de chaque octet ajoute un niveau de `If` imbriqué. Sur une entrée de 16 octets (128 bits × 8 itérations internes = 1024 `If` imbriqués), Z3 s'en sort en quelques secondes. Sur une entrée de 1024 octets, il pourrait timeout.

---

## Pièges courants et comment les éviter

### Piège n°1 : oublier la taille des bitvectors

Chaque `BitVec` a une taille fixe. Les opérations entre bitvectors de tailles différentes provoquent une erreur immédiate :

```python
a = BitVec("a", 32)  
b = BitVec("b", 16)  

# ERREUR : tailles incompatibles
# result = a + b

# Correct : étendre b à 32 bits avant l'opération
result = a + ZeroExt(16, b)    # Extension non signée (ajoute des 0)  
result = a + SignExt(16, b)    # Extension signée (propage le bit de signe)  
```

En assembleur x86-64, les instructions comme `movzx` (zero-extend) et `movsx` (sign-extend) font exactement cela. Quand vous voyez `movzx eax, byte [rbx]` dans le désassemblage, c'est un `ZeroExt(24, byte_value)` en Z3.

### Piège n°2 : confondre shift logique et arithmétique

C'est le piège le plus fréquent et le plus silencieux — le script tourne, le solveur renvoie une solution, mais elle est fausse :

```python
x = BitVec("x", 32)

# En C : (unsigned)x >> 16  →  shr dans l'assembleur
result_unsigned = LShR(x, 16)      # ✓ Correct

# En C : (signed)x >> 16    →  sar dans l'assembleur
result_signed = x >> 16              # ✓ Correct

# ERREUR SILENCIEUSE : utiliser >> pour un shift non signé
# Si x = 0x80000000, LShR donne 0x00008000
# mais >> donne 0xFFFF8000 (propage le bit de signe)
```

**Règle simple** : quand le désassemblage montre `shr`, utilisez `LShR`. Quand il montre `sar`, utilisez `>>`.

### Piège n°3 : oublier la sémantique modulaire de la multiplication

En C, la multiplication de deux `uint32_t` tronque le résultat à 32 bits (les 32 bits de poids fort sont perdus). Z3 fait la même chose avec les bitvectors — c'est le comportement par défaut, ce qui est correct. Mais si le binaire utilise `mul` (multiplication étendue, qui produit un résultat de 64 bits dans `rdx:rax`), il faut modéliser l'extension :

```python
a = BitVec("a", 32)  
b = BitVec("b", 32)  

# Multiplication tronquée (32 bits) — imul reg32, reg32
result_low = a * b

# Multiplication étendue (64 bits) — mul reg32 → rdx:rax
a_ext = ZeroExt(32, a)   # 32 → 64 bits  
b_ext = ZeroExt(32, b)  
full = a_ext * b_ext  
result_rdx = Extract(63, 32, full)   # 32 bits de poids fort  
result_rax = Extract(31, 0, full)    # 32 bits de poids faible  
```

### Piège n°4 : ne pas vérifier la solution

Z3 résout les contraintes **que vous lui avez données**. Si votre modélisation est incorrecte (un shift dans le mauvais sens, une constante mal recopiée, un tour de boucle oublié), Z3 produira une solution qui satisfait votre modèle erroné mais qui échouera sur le vrai binaire.

La vérification est triviale et non négociable :

```bash
$ ./keygenme_O2_strip <solution_z3>
```

Si le résultat est `"Access Denied."`, votre modèle contient une erreur. Comparez-le instruction par instruction avec le désassemblage.

---

## Z3 en mode interactif pour le RE exploratoire

Vous n'êtes pas obligé d'écrire un script complet d'emblée. Z3 s'utilise très bien en mode interactif dans un shell Python, en parallèle d'une session Ghidra. Le workflow typique :

1. Ouvrez le binaire dans Ghidra.  
2. Ouvrez un terminal Python avec Z3.  
3. Lisez le décompileur Ghidra bloc par bloc.  
4. Traduisez chaque bloc en Z3 dans le terminal.  
5. Testez des hypothèses au fur et à mesure.

```python
>>> from z3 import *
>>> x = BitVec("x", 32)

# "Hmm, Ghidra montre v = ((v >> 16) ^ v) * 0x45d9f3b..."
# Essayons avec une valeur connue pour vérifier ma traduction

>>> concrete = BitVecVal(0x12345678, 32)
>>> v = concrete
>>> v = (LShR(v, 16) ^ v) * BitVecVal(0x45d9f3b, 32)
>>> simplify(v)
2494104013

>>> hex(2494104013)
'0x94a2b2cd'
```

Vous pouvez valider cette valeur en la comparant à ce que GDB affiche quand vous exécutez le binaire avec l'entrée `0x12345678`. Si les deux correspondent, votre traduction est correcte. Sinon, il y a une erreur à trouver.

Ce ping-pong entre Ghidra, Z3 et GDB est le cœur du workflow hybride que nous recommandons dans la section 18.6.

---

## Aller plus loin avec Z3 : fonctionnalités avancées

### Optimisation : trouver le minimum ou maximum

Z3 peut non seulement trouver **une** solution, mais aussi **optimiser** une variable sous contraintes. Cela peut servir à trouver le plus petit serial valide, ou à déterminer les bornes d'un paramètre :

```python
from z3 import *

x = BitVec("x", 32)  
o = Optimize()    # Au lieu de Solver()  

o.add(x * 3 + 7 > 100)  
o.add(x % 2 == 0)  

# Minimiser x
o.minimize(x)

if o.check() == sat:
    print(f"Plus petit x : {o.model()[x]}")
```

### Énumérer toutes les solutions

Pour obtenir **toutes** les solutions (utile quand on veut tous les serials valides) :

```python
s = Solver()  
s.add(high_out == BitVecVal(0xa11c3514, 32))  
s.add(low_out  == BitVecVal(0xf00dcafe, 32))  

solutions = []  
while s.check() == sat:  
    m = s.model()
    h = m[high_in].as_long()
    l = m[low_in].as_long()
    solutions.append((h, l))
    # Exclure cette solution
    s.add(Or(high_in != m[high_in], low_in != m[low_in]))

print(f"{len(solutions)} solution(s) trouvée(s)")  
for h, l in solutions:  
    print(f"  {h:08x}{l:08x}")
```

Sur notre keygenme, il n'y a qu'une seule solution car le réseau de Feistel est une bijection sur les entiers 32 bits.

### Prouver qu'il n'y a pas de solution

Parfois, l'objectif n'est pas de trouver une solution mais de **prouver** qu'aucun input ne peut satisfaire certaines conditions — par exemple, prouver qu'un chemin de code est mort (*dead code*). Si `s.check()` retourne `unsat`, c'est une preuve formelle qu'aucune entrée ne peut atteindre ce chemin :

```python
s = Solver()  
s.add(contraintes_du_chemin)  

if s.check() == unsat:
    print("Ce chemin est impossible — dead code confirmé.")
```

C'est un usage avancé mais puissant pour l'analyse de vulnérabilités (chapitre 10 — diffing de binaires).

---

## Récapitulatif : quand utiliser Z3 vs angr

```
                    Connaître la logique ?
                    ┌───── Oui ──────┐
                    │                │
                    ▼                │
            ┌──────────────┐         │
            │  Z3 direct   │         │
            │  (rapide,    │         │
            │   précis)    │         │
            └──────────────┘         │
                                     │
                    ┌──── Non ───────┘
                    │
                    ▼
            ┌──────────────┐     Timeout ?
            │  angr        │────── Oui ──→ Extraire les contraintes
            │  (automatique│                manuellement → Z3
            │   complet)   │
            └──────────────┘
                    │
                    Non
                    │
                    ▼
                Solution ✓
```

La règle empirique est simple : commencez par angr. S'il réussit, vous avez terminé en 5 minutes. S'il échoue (timeout, explosion de chemins), ouvrez Ghidra, comprenez la logique, et modélisez les contraintes dans Z3. C'est plus de travail humain, mais le solveur répondra en millisecondes.

---

## Points clés à retenir

- Z3 est un **solveur SMT** qui résout des systèmes de contraintes sur des bitvectors — exactement le type d'opérations que l'on trouve dans un binaire compilé.

- L'approche Z3 directe nécessite d'**extraire manuellement** les contraintes depuis le décompileur, ce qui demande une bonne compréhension de la logique du binaire.

- Les **bitvectors** (`BitVec`) sont le type fondamental : ils reproduisent l'arithmétique modulaire du processeur, avec gestion des débordements, des shifts et des opérations bit à bit.

- Les pièges principaux sont les confusions entre **shifts logiques et arithmétiques** (`LShR` vs `>>`), les **tailles de bitvectors** incompatibles, et les **erreurs de traduction** depuis le désassemblage.

- Z3 résout les contraintes en **millisecondes** là où angr peut prendre des minutes, mais au prix d'un travail humain de modélisation.

- Toujours **vérifier la solution** sur le vrai binaire — Z3 résout votre modèle, pas le programme.

- Z3 et angr sont **complémentaires** : angr pour l'automatisation, Z3 pour la précision chirurgicale.

---

> Dans la section suivante (18.5), nous examinerons les **limites fondamentales** de l'exécution symbolique : explosion des chemins, boucles dépendant d'entrées symboliques, appels système, et les stratégies pour repousser ces limites.

⏭️ [Limites : explosion de chemins, boucles, appels système](/18-execution-symbolique/05-limites-explosion-chemins.md)
