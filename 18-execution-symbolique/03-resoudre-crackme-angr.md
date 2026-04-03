🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 18.3 — Résoudre un crackme automatiquement avec angr

> **Chapitre 18 — Exécution symbolique et solveurs de contraintes**  
> Partie IV — Techniques Avancées de RE

---

## Objectif de cette section

Nous allons résoudre le keygenme compilé avec GCC en utilisant uniquement angr — sans lire le code source, sans comprendre le réseau de Feistel, sans inverser manuellement la moindre opération. Le script final fera une vingtaine de lignes.

Nous procéderons en trois passes successives sur le même binaire, chacune illustrant une approche différente :

1. **Passe 1** — Résolution de `keygenme_O0` (avec symboles) en utilisant les adresses trouvées dans Ghidra.  
2. **Passe 2** — Résolution du même binaire en utilisant la sortie stdout comme critère (sans même ouvrir un désassembleur).  
3. **Passe 3** — Résolution de `keygenme_O2_strip` (optimisé, strippé) pour montrer que la méthode fonctionne aussi dans des conditions réalistes.

---

## Passe 1 — Résolution par adresses (keygenme_O0)

### Étape préliminaire : trouver les adresses cibles

Avant d'écrire le script angr, il faut connaître deux adresses :

- L'adresse de l'instruction qui mène au message de succès (`"Access Granted!"`).  
- L'adresse de l'instruction qui mène au message d'échec (`"Access Denied."`).

Plusieurs méthodes sont possibles. La plus rapide avec les outils vus dans les chapitres précédents :

```bash
$ objdump -d keygenme_O0 -M intel | grep -A2 "Access"
```

Ou avec `strings` combiné à `objdump` pour localiser les références :

```bash
# Trouver l'offset de la chaîne "Access Granted"
$ strings -t x keygenme_O0 | grep "Access"
  2004 Access Granted!
  2014 Access Denied.

# Chercher les références à ces chaînes dans le désassemblage
$ objdump -d keygenme_O0 -M intel | grep "2004"
```

Vous pouvez aussi ouvrir le binaire dans Ghidra (chapitre 8). Repérez la fonction `main`, identifiez le branchement `if/else` final, et notez les adresses des deux appels à `puts`.

> 💡 **Les adresses exactes dépendent de votre compilation.** Les valeurs utilisées dans cette section sont des exemples. Vous devez les remplacer par celles de **votre** binaire. C'est un réflexe fondamental en RE : ne jamais supposer qu'une adresse est fixe d'un build à l'autre.

Supposons que l'analyse donne :

- `0x40125a` — adresse du `call puts` pour `"Access Granted!"`.  
- `0x40126e` — adresse du `call puts` pour `"Access Denied."`.

### Le script complet

```python
#!/usr/bin/env python3
"""
solve_keygenme_v1.py — Résolution du keygenme par adresses  
Chapitre 18.3 — Passe 1  
"""

import angr  
import claripy  

# ---------- 1. Charger le binaire ----------
proj = angr.Project("./keygenme_O0", auto_load_libs=False)

# ---------- 2. Créer l'entrée symbolique ----------
# Le serial est une chaîne de 16 caractères hexadécimaux.
# On crée 16 bitvectors symboliques de 8 bits (un par caractère).
SERIAL_LEN = 16  
serial_chars = [claripy.BVS(f"c{i}", 8) for i in range(SERIAL_LEN)]  
serial_bvs = claripy.Concat(*serial_chars)  

# ---------- 3. Créer l'état initial ----------
state = proj.factory.entry_state(
    args=["./keygenme_O0", serial_bvs]
)

# ---------- 4. Contraindre les caractères ----------
# Chaque caractère doit être un chiffre hexadécimal valide [0-9A-Fa-f].
for c in serial_chars:
    digit = claripy.And(c >= ord('0'), c <= ord('9'))
    upper = claripy.And(c >= ord('A'), c <= ord('F'))
    lower = claripy.And(c >= ord('a'), c <= ord('f'))
    state.solver.add(claripy.Or(digit, upper, lower))

# ---------- 5. Lancer l'exploration ----------
simgr = proj.factory.simgr(state)  
simgr.explore(  
    find=0x40125a,      # puts("Access Granted!")
    avoid=0x40126e       # puts("Access Denied.")
)

# ---------- 6. Extraire la solution ----------
if simgr.found:
    found_state = simgr.found[0]
    solution = found_state.solver.eval(serial_bvs, cast_to=bytes)
    print(f"Serial trouvé : {solution.decode()}")
else:
    print("Aucune solution trouvée.")
```

### Exécution

```bash
$ source ~/angr-env/bin/activate
$ python3 solve_keygenme_v1.py
Serial trouvé : 7f3a1b9e5c82d046
```

Le serial affiché est une solution valide. Vérifions immédiatement :

```bash
$ ./keygenme_O0 7f3a1b9e5c82d046
Access Granted!
```

Le solveur a trouvé **une** solution parmi les éventuelles multiples solutions possibles. Si vous relancez le script, Z3 pourrait renvoyer une solution différente — c'est normal, le solveur n'est pas déterministe dans le choix de la solution quand plusieurs existent.

### Ce qui s'est passé en coulisses

Détaillons le déroulement interne, car c'est essentiel pour savoir diagnostiquer un script qui ne fonctionne pas :

**Chargement (CLE)** — Le binaire ELF est chargé en mémoire virtuelle. Les sections `.text`, `.data`, `.rodata` sont mappées à leurs adresses. Les fonctions importées (`puts`, `strlen`, `strtoul`, `memcpy`…) sont remplacées par des SimProcedures.

**Création de l'état** — `entry_state()` positionne l'exécution à `_start` et prépare la pile avec `argc=2`, `argv[0]="./keygenme_O0"` (concret) et `argv[1]=serial_bvs` (symbolique). Les 16 caractères symboliques sont écrits en mémoire à l'adresse pointée par `argv[1]`, suivis d'un octet nul.

**Exploration** — Le `SimulationManager` fait avancer l'état à travers `_start`, puis `__libc_start_main` (SimProcedure), puis `main`. Dans `main`, le programme vérifie `argc`, appelle `check_serial` avec `argv[1]`, qui à son tour appelle `strlen`, `strtoul`, effectue les opérations du Feistel, et arrive au branchement final.

À chaque instruction arithmétique, angr ne calcule pas un nombre — il construit une expression symbolique. Quand `feistel4` effectue `v ^= seed; v = ((v >> 16) ^ v) * 0x45D9F3B; ...`, le moteur produit une expression imbriquée de la forme :

```
(((((α ⊕ 0x5A3CE7F1) >> 16) ⊕ (α ⊕ 0x5A3CE7F1)) × 0x45D9F3B) >> 16) ⊕ ...
```

…en fonction des caractères symboliques d'entrée.

**Branchement final** — Quand l'exécution atteint `cmp` suivi de `jne`, le moteur bifurque :

- Un état prend la branche « égal » avec la contrainte `expression_high == 0xA11C3514`.  
- L'autre prend la branche « différent » avec la contrainte `expression_high != 0xA11C3514`.

Le second état hérite d'une adresse `avoid` — il est immédiatement écarté. Le premier continue jusqu'à la deuxième comparaison (`expression_low == 0xF00DCAFE`), se dédouble à nouveau, et l'état survivant atteint l'adresse `find`.

**Résolution** — Le solveur Z3 reçoit l'ensemble des contraintes accumulées (les contraintes hexadécimales sur chaque caractère + les contraintes de chemin issues des branchements) et trouve une affectation des 16 caractères qui satisfait tout.

---

## Passe 2 — Résolution par stdout (sans désassembleur)

La passe 1 nécessitait de trouver les adresses cibles dans le binaire. On peut s'en passer complètement en utilisant la **sortie standard** comme critère.

### Le script

```python
#!/usr/bin/env python3
"""
solve_keygenme_v2.py — Résolution par contenu de stdout  
Chapitre 18.3 — Passe 2  
"""

import angr  
import claripy  

proj = angr.Project("./keygenme_O0", auto_load_libs=False)

SERIAL_LEN = 16  
serial_chars = [claripy.BVS(f"c{i}", 8) for i in range(SERIAL_LEN)]  
serial_bvs = claripy.Concat(*serial_chars)  

state = proj.factory.entry_state(
    args=["./keygenme_O0", serial_bvs]
)

for c in serial_chars:
    digit = claripy.And(c >= ord('0'), c <= ord('9'))
    upper = claripy.And(c >= ord('A'), c <= ord('F'))
    lower = claripy.And(c >= ord('a'), c <= ord('f'))
    state.solver.add(claripy.Or(digit, upper, lower))

simgr = proj.factory.simgr(state)

# Critères basés sur stdout au lieu d'adresses
simgr.explore(
    find=lambda s: b"Access Granted" in s.posix.dumps(1),
    avoid=lambda s: b"Access Denied" in s.posix.dumps(1)
)

if simgr.found:
    found_state = simgr.found[0]
    solution = found_state.solver.eval(serial_bvs, cast_to=bytes)
    print(f"Serial trouvé : {solution.decode()}")
else:
    print("Aucune solution trouvée.")
```

La seule différence se situe dans l'appel à `explore()` : les adresses numériques sont remplacées par des fonctions lambda qui inspectent le contenu de stdout via `s.posix.dumps(1)`.

### Avantages et inconvénients de cette approche

**Avantages :**

- Aucun besoin d'ouvrir un désassembleur. Il suffit de connaître les chaînes affichées, que `strings` révèle en quelques secondes.  
- Fonctionne quel que soit le niveau d'optimisation ou l'état des symboles.  
- Plus robuste face aux changements d'adresses entre compilations.

**Inconvénients :**

- Plus lent. À chaque `step()`, angr doit vérifier le contenu de stdout pour **chaque** état actif, ce qui implique des appels au solveur supplémentaires.  
- Le critère lambda est évalué après chaque bloc de base, ce qui ajoute un overhead par rapport à une simple comparaison d'adresse.  
- Ne fonctionne pas si le programme n'affiche rien de distinctif (par exemple, un programme qui retourne juste un code de sortie différent).

En pratique, sur notre keygenme, la différence de temps est négligeable (quelques secondes de plus). Sur des binaires plus complexes, l'approche par adresses sera souvent préférable.

---

## Passe 3 — Binaire optimisé et strippé (keygenme_O2_strip)

C'est le vrai test. Le binaire `keygenme_O2_strip` est compilé avec `-O2` (optimisations agressives) et strippé (aucun symbole). C'est le scénario que vous rencontrerez le plus souvent en RE réel.

### Ce qui change avec -O2

Avant d'écrire le script, comprenons ce que GCC a fait au code (chapitre 16) :

- **Inlining** — Les fonctions `mix32`, `feistel4` et potentiellement `check_serial` sont inlinées dans `main`. Elles n'existent plus comme fonctions séparées dans le binaire.  
- **Réordonnancement des instructions** — L'ordre des opérations peut différer du code source.  
- **Optimisation des registres** — Moins d'accès mémoire, plus de valeurs conservées dans les registres.  
- **Déroulage partiel** — Les 4 tours du Feistel peuvent être déroulés.

Pour un analyste humain, tout cela rend le binaire significativement plus difficile à lire. Pour angr, **ça ne change presque rien** — le moteur symbolique exécute les instructions une par une, quelle que soit leur organisation. L'inlining ne modifie pas la sémantique, il modifie juste la structure du code.

### Ce qui change avec le stripping

Sans symboles, angr ne peut pas résoudre `main` par son nom. Deux options :

**Option A** — Trouver l'adresse de `main` manuellement. Le moyen le plus rapide sur un binaire ELF x86-64 est de regarder l'argument passé à `__libc_start_main` par `_start` :

```bash
$ objdump -d keygenme_O2_strip -M intel | head -30
```

L'instruction `lea rdi, [rip+0x...]` juste avant `call __libc_start_main` charge l'adresse de `main` dans `rdi`. Notez cette adresse.

**Option B** — Ne pas chercher `main` du tout et partir de l'entry point. C'est ce que fait `entry_state()` par défaut, et c'est suffisant pour notre cas.

### Le script

```python
#!/usr/bin/env python3
"""
solve_keygenme_v3.py — Résolution du binaire strippé -O2  
Chapitre 18.3 — Passe 3  
"""

import angr  
import claripy  

proj = angr.Project("./keygenme_O2_strip", auto_load_libs=False)

SERIAL_LEN = 16  
serial_chars = [claripy.BVS(f"c{i}", 8) for i in range(SERIAL_LEN)]  
serial_bvs = claripy.Concat(*serial_chars)  

state = proj.factory.entry_state(
    args=["./keygenme_O2_strip", serial_bvs]
)

for c in serial_chars:
    digit = claripy.And(c >= ord('0'), c <= ord('9'))
    upper = claripy.And(c >= ord('A'), c <= ord('F'))
    lower = claripy.And(c >= ord('a'), c <= ord('f'))
    state.solver.add(claripy.Or(digit, upper, lower))

simgr = proj.factory.simgr(state)

simgr.explore(
    find=lambda s: b"Access Granted" in s.posix.dumps(1),
    avoid=lambda s: b"Access Denied" in s.posix.dumps(1)
)

if simgr.found:
    found_state = simgr.found[0]
    solution = found_state.solver.eval(serial_bvs, cast_to=bytes)
    print(f"Serial trouvé : {solution.decode()}")
else:
    print("Aucune solution trouvée.")
```

Vous remarquerez que le script est **quasiment identique** à la passe 2. Seul le nom du fichier binaire change. C'est précisément le point : l'exécution symbolique est largement indifférente au niveau d'optimisation et à la présence ou non de symboles. Le solveur résout les mêmes contraintes, simplement encodées différemment dans le binaire.

### Vérification

```bash
$ python3 solve_keygenme_v3.py
Serial trouvé : 7f3a1b9e5c82d046

$ ./keygenme_O2_strip 7f3a1b9e5c82d046
Access Granted!
```

Le même serial fonctionne sur toutes les variantes — ce qui est logique puisque la sémantique du programme est identique quel que soit le niveau d'optimisation.

---

## Obtenir plusieurs solutions

Z3 renvoie par défaut **une** solution. Mais il en existe potentiellement d'autres. Pour en obtenir plusieurs, on peut demander au solveur d'évaluer l'expression symbolique à répétition en excluant les solutions déjà trouvées :

```python
if simgr.found:
    s = simgr.found[0]

    print("Solutions trouvées :")
    for i in range(5):
        try:
            sol = s.solver.eval(serial_bvs, cast_to=bytes)
            print(f"  [{i+1}] {sol.decode()}")
            # Exclure cette solution pour en trouver une autre
            s.solver.add(serial_bvs != s.solver.BVV(sol, SERIAL_LEN * 8))
        except angr.errors.SimUnsatError:
            print(f"  Seulement {i} solution(s) existante(s).")
            break
```

Sur notre keygenme, le réseau de Feistel est une bijection (chaque couple `(high, low)` en entrée produit un unique couple en sortie). La paire `(EXPECTED_HIGH, EXPECTED_LOW)` n'a donc qu'une seule préimage possible sur les 32 bits de chaque moitié. En revanche, le mapping des caractères hexadécimaux vers les valeurs numériques n'est pas injectif (`'a'` et `'A'` représentent le même chiffre hex), ce qui peut produire plusieurs serials valides si le binaire accepte les deux casses.

---

## Quand angr n'y arrive pas : techniques de déblocage

Tout ne se passe pas toujours aussi bien. Voici les situations les plus fréquentes et comment y répondre.

### L'exploration ne termine pas

Le symptôme : le script tourne depuis 10 minutes, la mémoire grimpe, et `simgr.active` contient des milliers d'états.

**Causes probables et remèdes :**

**Boucles non bornées** — Si le binaire contient une boucle dont la condition de sortie dépend d'une valeur symbolique, angr peut la dérouler indéfiniment. Solution : limiter les itérations avec `LoopSeer` :

```python
simgr.use_technique(angr.exploration_techniques.LoopSeer(
    cfg=proj.analyses.CFGFast(),
    bound=5   # max 5 itérations par boucle
))
```

**Trop de branchements** — Le nombre d'états explose exponentiellement. Solution : ajouter davantage de contraintes sur les entrées pour élaguer l'espace, ou utiliser `avoid` plus agressivement pour écarter les chemins non pertinents.

**Fonctions de bibliothèque problématiques** — Certaines fonctions de la libc sont mal modélisées par les SimProcedures. `strtoul` en particulier peut poser problème car elle gère de nombreux cas (bases différentes, espaces initiaux, gestion d'erreurs). Solution : hooker la fonction avec une SimProcedure simplifiée :

```python
class SimpleStrtoul(angr.SimProcedure):
    """Version simplifiée de strtoul pour l'exécution symbolique."""
    def run(self, str_ptr, endptr, base):
        # Lire 8 caractères depuis le pointeur
        str_data = self.state.memory.load(str_ptr, 8)
        # Retourner une valeur symbolique non contrainte de 32 bits
        result = self.state.solver.BVS("strtoul_result", 64)
        return result

proj.hook_symbol("strtoul", SimpleStrtoul())
```

> ⚠️ Hooker une fonction par une version simplifiée fait perdre de la précision : le solveur aura moins de contraintes et pourrait proposer des solutions invalides. C'est un compromis entre complétude et performance. Testez toujours la solution trouvée sur le vrai binaire.

### angr trouve une solution mais elle ne fonctionne pas

Le symptôme : le script affiche un serial, mais `./keygenme <serial>` donne `"Access Denied."`.

**Causes probables :**

- **SimProcedure imprécise** — Le modèle simplifié d'une fonction de la libc ne capture pas exactement son comportement réel. Par exemple, `strtoul` en mode symbolique peut ne pas modéliser correctement la conversion hexadécimale. Solution : vérifier les contraintes avec `found_state.solver.constraints` et comparer avec le comportement réel observé dans GDB.

- **Contraintes manquantes sur les entrées** — Si vous n'avez pas contraint les caractères à être hexadécimaux, le solveur peut proposer des caractères non-hex qui passent la vérification symbolique mais échouent dans la conversion réelle.

- **Problème d'encodage ou de null byte** — Le serial peut contenir un octet nul (`\x00`) qui tronque la chaîne C. Solution : ajouter `state.solver.add(c != 0)` pour chaque caractère.

### angr crashe ou lève une exception

Les messages d'erreur d'angr peuvent être obscurs. Les plus courants :

- **`SimUnsatError`** — Les contraintes sont devenues insatisfiables. Un des chemins est impossible. Ce n'est généralement pas un problème — l'état est simplement écarté.  
- **`AngrError: ... unsupported syscall`** — Le binaire effectue un appel système que angr ne sait pas modéliser. Solution : hooker la zone de code concernée.  
- **`ClaripyZeroDivisionError`** — Le programme effectue une division par une valeur qui pourrait être zéro symboliquement. Solution : ajouter une contrainte pour exclure le zéro.

---

## Démarrer l'exploration à mi-chemin

Parfois, exécuter le binaire depuis `_start` est trop coûteux : l'initialisation de la libc, le parsing des arguments, les vérifications de format du serial — tout cela génère des chemins inutiles avant d'atteindre la partie intéressante.

Une technique puissante consiste à démarrer l'exploration directement à l'entrée de la fonction de vérification, en injectant les entrées symboliques dans les registres ou la mémoire :

```python
# Supposons que check_serial commence à 0x401180
# et attend un pointeur vers le serial dans rdi (convention System V)

import angr  
import claripy  

proj = angr.Project("./keygenme_O0", auto_load_libs=False)

# Créer un état à l'entrée de check_serial
state = proj.factory.blank_state(addr=0x401180)

# Allouer un buffer pour le serial dans la mémoire symbolique
SERIAL_ADDR = 0x500000  # Adresse arbitraire dans un espace libre  
SERIAL_LEN = 16  

serial_chars = [claripy.BVS(f"c{i}", 8) for i in range(SERIAL_LEN)]  
for i, c in enumerate(serial_chars):  
    state.memory.store(SERIAL_ADDR + i, c)
# Null terminator
state.memory.store(SERIAL_ADDR + SERIAL_LEN, claripy.BVV(0, 8))

# Passer le pointeur dans rdi (premier argument, convention System V)
state.regs.rdi = SERIAL_ADDR

# Contraintes hexadécimales
for c in serial_chars:
    digit = claripy.And(c >= ord('0'), c <= ord('9'))
    upper = claripy.And(c >= ord('A'), c <= ord('F'))
    lower = claripy.And(c >= ord('a'), c <= ord('f'))
    state.solver.add(claripy.Or(digit, upper, lower))

simgr = proj.factory.simgr(state)

# Ici, find/avoid sont les adresses DANS check_serial
# (le return 1 vs return 0)
simgr.explore(
    find=0x4011f5,    # adresse du `mov eax, 1` (return 1)
    avoid=0x4011e0     # adresse du `xor eax, eax`  (return 0)
)

if simgr.found:
    s = simgr.found[0]
    serial = bytes(s.solver.eval(c) for c in serial_chars)
    print(f"Serial trouvé : {serial.decode()}")
```

Cette approche est **beaucoup plus rapide** car elle élimine toute l'exploration du code avant `check_serial`. Elle nécessite en revanche une analyse statique préalable pour déterminer l'adresse de la fonction, ses arguments et la convention d'appel — exactement le genre de travail fait avec Ghidra au chapitre 8.

---

## Comparer les temps de résolution

Le tableau ci-dessous donne un ordre de grandeur des temps de résolution sur une machine typique (4 cœurs, 16 Go RAM). Les valeurs exactes dépendent de votre hardware et de la version d'angr :

| Variante | Méthode | Temps approximatif |  
|---|---|---|  
| `keygenme_O0` | Par adresses, depuis `entry_state` | ~15–30 secondes |  
| `keygenme_O0` | Par stdout, depuis `entry_state` | ~20–45 secondes |  
| `keygenme_O0` | Depuis `check_serial` (`blank_state`) | ~3–8 secondes |  
| `keygenme_O2_strip` | Par stdout, depuis `entry_state` | ~20–60 secondes |  
| `keygenme_O3_strip` | Par stdout, depuis `entry_state` | ~25–90 secondes |

Observations :

- Démarrer à mi-chemin (`blank_state` à l'entrée de la fonction cible) divise le temps par un facteur 3 à 10.  
- Le passage de `-O0` à `-O2`/`-O3` n'a qu'un impact modéré sur le temps de résolution. L'inlining produit un chemin plus long mais pas plus ramifié.  
- La méthode par stdout est légèrement plus lente à cause de l'évaluation des lambdas à chaque step, mais la différence reste faible sur un binaire de cette taille.

---

## Anatomie d'un script angr robuste

En synthèse, voici le template que vous pouvez réutiliser et adapter pour n'importe quel crackme :

```python
#!/usr/bin/env python3
"""
Template de résolution de crackme avec angr.
À adapter : nom du binaire, taille de l'entrée, contraintes, adresses.
"""

import angr  
import claripy  
import sys  
import logging  

# Réduire la verbosité d'angr (décommenter pour déboguer)
# logging.getLogger("angr").setLevel(logging.DEBUG)
logging.getLogger("angr").setLevel(logging.WARNING)

# ========== PARAMÈTRES À ADAPTER ==========
BINARY    = "./keygenme_O2_strip"  
INPUT_LEN = 16                       # Longueur de l'entrée en caractères  

# Critères d'exploration (choisir UNE des deux méthodes) :
# Méthode A — par adresses :
# FIND_ADDR  = 0x40125a
# AVOID_ADDR = 0x40126e
# Méthode B — par stdout :
FIND_STR  = b"Access Granted"  
AVOID_STR = b"Access Denied"  
# ===========================================

def main():
    proj = angr.Project(BINARY, auto_load_libs=False)

    # Entrée symbolique
    chars = [claripy.BVS(f"c{i}", 8) for i in range(INPUT_LEN)]
    sym_input = claripy.Concat(*chars)

    state = proj.factory.entry_state(args=[BINARY, sym_input])

    # Contraintes sur les caractères (adapter selon le format attendu)
    for c in chars:
        # Ici : hexadécimal [0-9A-Fa-f]
        state.solver.add(claripy.Or(
            claripy.And(c >= ord('0'), c <= ord('9')),
            claripy.And(c >= ord('A'), c <= ord('F')),
            claripy.And(c >= ord('a'), c <= ord('f'))
        ))

    simgr = proj.factory.simgr(state)

    # Exploration
    simgr.explore(
        find=lambda s: FIND_STR in s.posix.dumps(1),
        avoid=lambda s: AVOID_STR in s.posix.dumps(1)
    )

    # Résultat
    if simgr.found:
        s = simgr.found[0]
        solution = s.solver.eval(sym_input, cast_to=bytes)
        print(f"[+] Solution : {solution.decode()}")
        print(f"[*] stdout   : {s.posix.dumps(1).decode().strip()}")
    else:
        print("[-] Aucune solution trouvée.")
        print(f"    active={len(simgr.active)} "
              f"deadended={len(simgr.deadended)} "
              f"avoided={len(simgr.avoided)} "
              f"errored={len(simgr.errored)}")
        sys.exit(1)

if __name__ == "__main__":
    main()
```

Le bloc final qui affiche l'état des stashes en cas d'échec est un réflexe de diagnostic essentiel. Si `active` est vide et `found` aussi, cela signifie que tous les chemins ont été explorés sans trouver la cible — probablement un problème de critère ou de contraintes. Si `active` contient des milliers d'états, c'est une explosion de chemins — il faut élaguer davantage ou utiliser une stratégie d'exploration différente.

---

## Points clés à retenir

- Un script angr de résolution de crackme suit toujours le même squelette en 6 étapes : charger → symboliser → contraindre → explorer → extraire → vérifier.

- Deux méthodes de ciblage : par **adresses** (plus rapide, nécessite un désassembleur) ou par **contenu de stdout** (plus portable, légèrement plus lent).

- Le script est **quasiment identique** pour un binaire `-O0` avec symboles et un binaire `-O3` strippé. L'exécution symbolique est largement indifférente aux optimisations du compilateur.

- Démarrer à **mi-chemin** (`blank_state` à l'entrée de la fonction cible) accélère considérablement la résolution en éliminant le code d'initialisation.

- Toujours **vérifier la solution** sur le vrai binaire. Les SimProcedures sont des approximations, et le solveur ne garantit la correction que dans le modèle d'angr, pas dans le monde réel.

- En cas d'échec, examiner les **stashes** (`active`, `deadended`, `avoided`, `errored`) pour diagnostiquer le problème.

---

> Dans la section suivante (18.4), nous quitterons angr pour travailler directement avec **Z3** : modéliser manuellement des contraintes extraites lors d'une analyse statique et les résoudre sans moteur d'exécution symbolique.

⏭️ [Z3 Theorem Prover — modéliser des contraintes extraites manuellement](/18-execution-symbolique/04-z3-theorem-prover.md)
