🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.7 — Résolution automatique avec angr

> 📖 **Rappel** : les principes de l'exécution symbolique, l'architecture d'angr (SimState, SimManager, exploration) et ses limites sont présentés au chapitre 18. Cette section suppose que vous avez déjà installé angr et exécuté au moins un script simple. Si ce n'est pas le cas, reportez-vous aux sections 18.1 à 18.3.

---

## Introduction

Les sections précédentes ont suivi un chemin manuel : analyse statique pour comprendre la structure, analyse dynamique pour observer l'exécution, patching pour contourner la vérification. À chaque étape, c'est le reverse engineer qui pilote, qui déduit, qui décide.

L'exécution symbolique renverse la perspective. Au lieu de comprendre l'algorithme pour trouver la clé, on laisse un **solveur de contraintes** explorer tous les chemins possibles du programme et découvrir automatiquement l'entrée qui mène au chemin de succès. L'outil n'a pas besoin de savoir ce que fait `compute_hash` ni comment `derive_key` transforme le hash — il modélise les instructions machine une par une et construit un système d'équations que le solveur (Z3) résout.

angr est le framework d'exécution symbolique le plus utilisé en RE. Il combine un moteur d'exécution symbolique (VEX IR, basé sur Valgrind), un solveur SMT (Z3 de Microsoft Research) et une suite d'outils d'analyse binaire (chargeur ELF, simulation de syscalls, modèles de libc).

Dans cette section, nous allons écrire un script Python qui trouve automatiquement la clé valide pour un username donné, sans comprendre l'algorithme de hachage. Nous commencerons par la variante `keygenme_O0` (avec symboles), puis nous adapterons le script pour `keygenme_O2_strip` (optimisé et strippé).

---

## Le principe : find et avoid

L'utilisation d'angr sur un crackme repose sur un concept simple :

- **find** : l'adresse (ou la condition) qui correspond au chemin de succès. On veut qu'angr trouve une entrée qui mène le programme à cette adresse.  
- **avoid** : les adresses qui correspondent aux chemins d'échec. On veut qu'angr abandonne tout chemin qui atteint ces adresses, pour ne pas gaspiller de temps d'exploration.

Sur notre keygenme, d'après les sections 21.1 et 21.3 :

- **find** → l'adresse du `printf(MSG_OK)` (le message `"[+] Valid license!"`)  
- **avoid** → l'adresse du `printf(MSG_FAIL)` (le message `"[-] Invalid license."`) et l'adresse du `printf(MSG_ERR_LEN)` (le message d'erreur sur la longueur du username)

angr explore l'espace des états du programme : à chaque branchement conditionnel, il crée deux états (un pour chaque branche), propage les contraintes symboliques, et élimine les états qui atteignent une adresse `avoid`. Quand un état atteint une adresse `find`, angr demande à Z3 de résoudre les contraintes accumulées — le résultat est une valeur concrète des entrées symboliques qui satisfait toutes les conditions.

---

## Étape 1 — Identifier les adresses cibles

### Avec symboles (`keygenme_O0`)

On récupère les adresses directement depuis `objdump` ou Ghidra. On cherche les instructions qui référencent les chaînes de succès et d'échec :

```bash
$ objdump -d -M intel keygenme_O0 | grep -n "lea.*Valid\|lea.*Invalid\|lea.*must be"
```

Cette commande ne fonctionne pas directement (les chaînes sont référencées par adresse, pas par contenu). On utilise plutôt l'approche par chaînes dans le binaire :

```bash
# Trouver l'adresse de la chaîne de succès dans .rodata
$ strings -t x keygenme_O0 | grep "Valid license"
   20c0 [+] Valid license! Welcome, %s.

# Trouver les XREF vers cette adresse dans le code
$ objdump -d -M intel keygenme_O0 | grep "20c0"
    15e6:   48 8d 05 d3 0a 00 00    lea    rax,[rip+0xad3]  # 20c0
```

L'adresse `0x15e6` est le `LEA` qui charge la chaîne de succès dans `main`. C'est notre adresse **find**.

De la même façon, on identifie les adresses **avoid** :

```bash
# Message d'échec
$ strings -t x keygenme_O0 | grep "Invalid license"
   21c8 [-] Invalid license. Try again.

$ objdump -d -M intel keygenme_O0 | grep "21c8"
    1601:   48 8d 05 c0 0b 00 00    lea    rax,[rip+0xbc0]  # 21c8

# Message d'erreur longueur
$ strings -t x keygenme_O0 | grep "must be between"
   2140 [-] Username must be between 3 and 31 characters.

$ objdump -d -M intel keygenme_O0 | grep "2190"
    1575:   48 8d 05 14 0c 00 00    lea    rax,[rip+0xc14]  # 2190
```

Récapitulatif :

| Rôle | Adresse (offset) | Instruction |  
|---|---|---|  
| **find** (succès) | `0x15e6` | `LEA RAX, [MSG_OK]` |  
| **avoid** (échec) | `0x1601` | `LEA RAX, [MSG_FAIL]` |  
| **avoid** (erreur longueur) | `0x1575` | `LEA RAX, [MSG_ERR_LEN]` |

> 💡 **Adresses PIE** : angr charge les binaires PIE avec une base par défaut de `0x400000`. Les offsets trouvés par `objdump` doivent être ajoutés à cette base. Ainsi `0x15e6` devient `0x4015e6` dans angr. Alternativement, on peut utiliser les fonctionnalités de recherche de chaînes d'angr pour ne pas se soucier des adresses du tout (voir plus loin).

---

## Étape 2 — Écrire le script angr (version basique)

Voici un premier script, volontairement simple et commenté, qui résout le keygenme pour un username fixé :

```python
#!/usr/bin/env python3
"""
solve_keygenme.py — Résolution automatique du keygenme avec angr.

Usage : python3 solve_keygenme.py
"""

import angr  
import claripy  
import sys  

# ── Configuration ────────────────────────────────────────────
BINARY = "./keygenme_O0"  
USERNAME = b"Alice"  

# Adresses cibles (offset fichier + base angr 0x400000 pour PIE)
BASE = 0x400000  
ADDR_SUCCESS = BASE + 0x15e6   # LEA RAX, [MSG_OK]  
ADDR_FAIL    = BASE + 0x1601   # LEA RAX, [MSG_FAIL]  
ADDR_ERR_LEN = BASE + 0x1575   # LEA RAX, [MSG_ERR_LEN]  

# ── Chargement du binaire ────────────────────────────────────
proj = angr.Project(BINARY, auto_load_libs=False)

# ── État initial ─────────────────────────────────────────────
# On crée un état au point d'entrée du programme.
state = proj.factory.entry_state(
    stdin=angr.SimFile("/dev/stdin", content=angr.SimFileBase.ALL_BYTES),
)

# ── Préparer l'entrée simulée (stdin) ───────────────────────
# Le programme lit d'abord le username, puis la clé.
# On fournit le username en concret et la clé en symbolique.
#
# Format de stdin : "Alice\nXXXX-XXXX-XXXX-XXXX\n"
#
# La clé fait 19 caractères (XXXX-XXXX-XXXX-XXXX) + newline.

KEY_LEN = 19  
key_chars = [claripy.BVS(f"key_{i}", 8) for i in range(KEY_LEN)]  

# Contraindre chaque caractère à être un caractère hexadécimal
# majuscule ou un tiret, selon sa position dans le format.
for i, c in enumerate(key_chars):
    if i in (4, 9, 14):
        # Positions des tirets
        state.solver.add(c == ord('-'))
    else:
        # Caractères hexadécimaux majuscules : 0-9 ou A-F
        state.solver.add(claripy.Or(
            claripy.And(c >= ord('0'), c <= ord('9')),
            claripy.And(c >= ord('A'), c <= ord('F')),
        ))

# Construire le contenu de stdin
stdin_content = claripy.Concat(
    claripy.BVV(USERNAME + b"\n"),   # username (concret)
    *key_chars,                       # clé (symbolique)
    claripy.BVV(b"\n"),              # newline final
)

state = proj.factory.entry_state(
    stdin=angr.SimFile("/dev/stdin", content=stdin_content),
)

# Réappliquer les contraintes sur le nouvel état
for i, c in enumerate(key_chars):
    if i in (4, 9, 14):
        state.solver.add(c == ord('-'))
    else:
        state.solver.add(claripy.Or(
            claripy.And(c >= ord('0'), c <= ord('9')),
            claripy.And(c >= ord('A'), c <= ord('F')),
        ))

# ── Exploration ──────────────────────────────────────────────
simgr = proj.factory.simulation_manager(state)

print(f"[*] Exploration en cours pour username = '{USERNAME.decode()}'...")  
simgr.explore(  
    find=ADDR_SUCCESS,
    avoid=[ADDR_FAIL, ADDR_ERR_LEN],
)

# ── Résultat ─────────────────────────────────────────────────
if simgr.found:
    found_state = simgr.found[0]
    # Extraire la valeur concrète de chaque octet de la clé
    solution = bytes(
        found_state.solver.eval(c, cast_to=int) for c in key_chars
    )
    print(f"[+] Clé trouvée : {solution.decode()}")
else:
    print("[-] Aucune solution trouvée.")
    sys.exit(1)
```

### Exécution

```bash
$ python3 solve_keygenme.py
[*] Exploration en cours pour username = 'Alice'...
[+] Clé trouvée : DCEB-0DFC-B51F-3428
```

angr a trouvé la clé valide sans que nous ayons eu à comprendre l'algorithme de hachage.

---

## Anatomie du script

Décortiquons les choix techniques du script pour comprendre pourquoi chaque élément est nécessaire.

### `angr.Project(BINARY, auto_load_libs=False)`

Le `Project` est le point d'entrée d'angr. Il charge le binaire ELF, le désassemble et construit sa représentation interne (VEX IR).

Le paramètre `auto_load_libs=False` indique à angr de **ne pas charger la libc** réelle. À la place, angr utilise ses propres modèles (SimProcedures) pour simuler les fonctions standard (`printf`, `strcmp`, `strlen`, `fgets`…). Ces modèles sont des implémentations simplifiées qui comprennent la sémantique symbolique — par exemple, le SimProcedure de `strcmp` sait comparer deux chaînes dont l'une est symbolique et produire les contraintes correspondantes.

Charger la libc réelle (`auto_load_libs=True`) augmenterait considérablement la complexité de l'exploration sans bénéfice, car angr devrait explorer le code interne de la libc (des milliers de fonctions, des boucles complexes) au lieu de les simuler directement.

### Variables symboliques avec `claripy.BVS`

`claripy` est le module d'angr pour manipuler les expressions symboliques. `BVS("name", 8)` crée une **variable symbolique** de 8 bits (un octet) — un placeholder qui représente « n'importe quelle valeur possible sur 8 bits ».

Chaque caractère de la clé est une variable symbolique indépendante. angr va propager ces variables à travers toutes les instructions du programme : quand `compute_hash` additionne un caractère à l'accumulateur, angr enregistre l'addition symbolique ; quand `strcmp` compare la clé formatée avec l'entrée utilisateur, angr enregistre l'égalité comme contrainte.

### Contraintes de format

On ajoute des contraintes manuelles pour restreindre l'espace de recherche :

```python
state.solver.add(c == ord('-'))          # tirets aux positions 4, 9, 14  
state.solver.add(claripy.And(c >= ord('0'), c <= ord('9')))  # chiffres  
state.solver.add(claripy.And(c >= ord('A'), c <= ord('F')))  # lettres hex  
```

Sans ces contraintes, angr explorerait aussi des clés contenant des caractères non hexadécimaux. Le solveur finirait par trouver une solution, mais le temps d'exploration serait beaucoup plus long. Contraindre le format est une optimisation qui exploite notre connaissance du triage (section 21.1 : le format est `XXXX-XXXX-XXXX-XXXX` avec des caractères hexadécimaux).

### `simgr.explore(find=..., avoid=...)`

Le Simulation Manager gère la collection d'états actifs. La méthode `explore` :

1. Prend l'état initial et exécute symboliquement les instructions une par une.  
2. À chaque branchement conditionnel (comme notre `JNE` après `strcmp`), elle crée deux copies de l'état : une pour chaque branche, avec les contraintes correspondantes ajoutées.  
3. Les états qui atteignent une adresse `avoid` sont déplacés vers le stash `avoided` et ne sont plus explorés.  
4. Quand un état atteint une adresse `find`, il est déplacé vers le stash `found` et l'exploration s'arrête.

### Extraction de la solution

```python
found_state.solver.eval(c, cast_to=int)
```

L'état trouvé (`found_state`) contient toutes les contraintes accumulées le long du chemin de succès. La méthode `solver.eval(variable)` demande à Z3 de trouver une valeur concrète pour la variable symbolique qui satisfait toutes les contraintes. Le résultat est la clé valide.

---

## Étape 3 — Version améliorée avec recherche par chaînes

La version basique utilise des adresses codées en dur, ce qui la rend fragile (les adresses changent si on recompile le binaire). Une version plus robuste utilise la recherche de chaînes dans le binaire pour trouver automatiquement les adresses cibles :

```python
#!/usr/bin/env python3
"""
solve_keygenme_robust.py — Version robuste avec détection  
automatique des adresses via les chaînes du binaire.  
"""

import angr  
import claripy  
import sys  

BINARY = "./keygenme_O0"  
USERNAME = b"Alice"  
KEY_LEN = 19  

# ── Chargement ───────────────────────────────────────────────
proj = angr.Project(BINARY, auto_load_libs=False)

# ── Recherche automatique des adresses ───────────────────────
# Parcourir le binaire pour trouver les références aux chaînes
# de succès et d'échec, sans coder les adresses en dur.

cfg = proj.analyses.CFGFast()

def find_addr_referencing(string_needle):
    """Trouve l'adresse d'une instruction qui référence
    une chaîne contenant string_needle."""
    for addr, func in proj.kb.functions.items():
        try:
            # Décompiler la fonction pour chercher les constantes string
            block_addrs = list(func.block_addrs)
            for baddr in block_addrs:
                block = proj.factory.block(baddr)
                for const in block.vex.all_constants:
                    val = const.value
                    try:
                        mem = proj.loader.memory.load(val, 60)
                        if string_needle in mem:
                            return baddr
                    except Exception:
                        continue
        except Exception:
            continue
    return None

addr_success = find_addr_referencing(b"Valid license")  
addr_fail    = find_addr_referencing(b"Invalid license")  
addr_err_len = find_addr_referencing(b"must be between")  

if not addr_success or not addr_fail:
    print("[-] Impossible de trouver les adresses cibles.")
    sys.exit(1)

avoid_addrs = [addr_fail]  
if addr_err_len:  
    avoid_addrs.append(addr_err_len)

print(f"[*] find  = {hex(addr_success)}")  
print(f"[*] avoid = {[hex(a) for a in avoid_addrs]}")  

# ── Variables symboliques ────────────────────────────────────
key_chars = [claripy.BVS(f"k{i}", 8) for i in range(KEY_LEN)]

stdin_content = claripy.Concat(
    claripy.BVV(USERNAME + b"\n"),
    *key_chars,
    claripy.BVV(b"\n"),
)

state = proj.factory.entry_state(
    stdin=angr.SimFile("/dev/stdin", content=stdin_content),
)

for i, c in enumerate(key_chars):
    if i in (4, 9, 14):
        state.solver.add(c == ord('-'))
    else:
        state.solver.add(claripy.Or(
            claripy.And(c >= ord('0'), c <= ord('9')),
            claripy.And(c >= ord('A'), c <= ord('F')),
        ))

# ── Exploration ──────────────────────────────────────────────
simgr = proj.factory.simulation_manager(state)  
print(f"[*] Exploration pour '{USERNAME.decode()}'...")  

simgr.explore(find=addr_success, avoid=avoid_addrs)

if simgr.found:
    solution = bytes(
        simgr.found[0].solver.eval(c, cast_to=int) for c in key_chars
    )
    print(f"[+] Clé : {solution.decode()}")
else:
    print("[-] Aucune solution.")
    sys.exit(1)
```

Cette version fonctionne sur toutes les variantes du keygenme (y compris les strippées) tant que les chaînes de succès/échec sont présentes en clair dans le binaire.

---

## Étape 4 — Adapter pour la variante strippée et optimisée

### `keygenme_O2_strip`

Le script robuste fonctionne directement : angr ne se soucie pas des symboles (il travaille sur le code machine) et les chaînes en `.rodata` survivent au stripping. Il suffit de changer la variable `BINARY` :

```python
BINARY = "./keygenme_O2_strip"
```

L'exploration prend un peu plus de temps car le code optimisé en `-O2` produit des chemins plus compacts avec moins de variables intermédiaires sur la pile, ce qui modifie la structure du graphe d'états. Mais le solveur Z3 trouve la solution de la même manière.

### Points d'attention sur les binaires optimisés

En `-O2`/`-O3`, le compilateur peut :

- **Inliner `check_license`** dans `main`. Pour angr, c'est transparent — il explore le flux d'instructions sans se soucier des frontières de fonctions.  
- **Remplacer `strcmp` par une comparaison optimisée** (par exemple `memcmp` inliné, ou une boucle de comparaison déroulée). Si angr ne reconnaît pas le pattern comme un appel à `strcmp`, il l'exécutera symboliquement instruction par instruction. C'est plus lent mais fonctionne.  
- **Utiliser des registres SIMD** pour des opérations de copie ou de comparaison de chaînes. angr supporte une partie des instructions SSE/AVX, mais certaines peuvent provoquer des erreurs. Si c'est le cas, on peut demander à angr d'utiliser un modèle simplifié avec l'option `add_options={angr.options.ZERO_FILL_UNCONSTRAINED_REGISTERS}`.

---

## Étape 5 — Approche alternative : démarrer au milieu

Si l'exploration depuis `entry_state` est trop lente (ce qui arrive sur des binaires plus complexes), on peut démarrer l'exécution symbolique à mi-chemin — directement à l'entrée de `check_license`, en contournant toute la phase de lecture d'entrée.

```python
#!/usr/bin/env python3
"""
solve_keygenme_targeted.py — Exploration ciblée depuis  
l'entrée de check_license.  
"""

import angr  
import claripy  
import sys  

BINARY = "./keygenme_O0"  
USERNAME = b"Alice"  
KEY_LEN = 19  

proj = angr.Project(BINARY, auto_load_libs=False)

# ── Adresses (offsets + base 0x400000) ───────────────────────
BASE = 0x400000  
ADDR_CHECK_LICENSE = BASE + 0x13d1  
ADDR_STRCMP_RET_1  = BASE + 0x143e   # MOV EAX, 1 (succès)  
ADDR_STRCMP_RET_0  = BASE + 0x1445   # MOV EAX, 0 (échec)  

# ── Variables symboliques pour la clé ────────────────────────
key_chars = [claripy.BVS(f"k{i}", 8) for i in range(KEY_LEN)]  
key_bvv = claripy.Concat(*key_chars, claripy.BVV(b"\0"))  

# ── Construire un état à l'entrée de check_license ──────────
state = proj.factory.call_state(
    ADDR_CHECK_LICENSE,
    angr.PointerWrapper(USERNAME + b"\0"),  # RDI = username
    angr.PointerWrapper(key_bvv),           # RSI = user_key
)

# Contraintes de format
for i, c in enumerate(key_chars):
    if i in (4, 9, 14):
        state.solver.add(c == ord('-'))
    else:
        state.solver.add(claripy.Or(
            claripy.And(c >= ord('0'), c <= ord('9')),
            claripy.And(c >= ord('A'), c <= ord('F')),
        ))

# ── Exploration ──────────────────────────────────────────────
simgr = proj.factory.simulation_manager(state)  
print("[*] Exploration ciblée depuis check_license...")  

simgr.explore(
    find=ADDR_STRCMP_RET_1,
    avoid=[ADDR_STRCMP_RET_0],
)

if simgr.found:
    solution = bytes(
        simgr.found[0].solver.eval(c, cast_to=int) for c in key_chars
    )
    print(f"[+] Clé : {solution.decode()}")
else:
    print("[-] Aucune solution.")
    sys.exit(1)
```

### `call_state` vs `entry_state`

La différence est fondamentale :

| Méthode | Point de départ | Entrées | Usage |  
|---|---|---|---|  
| `entry_state()` | Point d'entrée du programme (`_start`) | stdin simulé | Exploration complète, proche du comportement réel |  
| `call_state(addr, arg1, arg2, ...)` | Adresse arbitraire | Arguments passés directement dans les registres | Exploration ciblée, plus rapide, mais demande de connaître la signature de la fonction |

`call_state` est beaucoup plus rapide car angr n'a pas à traverser toute la phase d'initialisation (CRT, `main` avant `check_license`, lectures de stdin). En contrepartie, il faut fournir manuellement les arguments dans le bon format — ce qui nécessite d'avoir compris la signature de la fonction cible grâce à l'analyse statique (section 21.3).

---

## Comprendre ce qu'angr fait en interne

Pour démystifier l'exécution symbolique, traçons mentalement ce que fait angr sur notre keygenme. L'exploration depuis `check_license` traverse les étapes suivantes :

**1. `compute_hash(username)`** — Le username est concret (`"Alice"`), donc angr calcule le hash concrètement, sans symboles. Le résultat est un entier 32 bits concret.

**2. `derive_key(hash, groups)`** — Le hash est concret, donc les 4 groupes de 16 bits sont calculés concrètement. Pas de symboles ici non plus.

**3. `format_key(groups, expected)`** — `snprintf` (via le SimProcedure d'angr) écrit la chaîne formatée. Le buffer `expected` contient maintenant une chaîne concrète (par exemple `"DCEB-0DFC-B51F-3428"`).

**4. `strcmp(expected, user_key)`** — C'est ici que la magie opère. `expected` est concret, mais `user_key` est **symbolique** (composé de nos `key_chars`). Le SimProcedure de `strcmp` compare les deux chaînes caractère par caractère et génère des contraintes :

```
key_chars[0]  == 'D'  
key_chars[1]  == 'C'  
key_chars[2]  == 'E'  
key_chars[3]  == 'B'  
key_chars[4]  == '-'  
...
key_chars[18] == '8'
```

**5. Branchement** — Après `strcmp`, le `TEST`/`JNE` crée deux branches. La branche « succès » (find) porte les contraintes d'égalité ci-dessus. La branche « échec » (avoid) porte la négation. angr conserve la branche succès et l'évalue avec Z3.

**6. Z3 résout** — Les contraintes sont triviales (chaque caractère est fixé à une valeur concrète). Z3 retourne la solution instantanément.

### Pourquoi c'est si rapide ici

Notre keygenme est un cas favorable pour l'exécution symbolique :

- Le username est concret → pas d'explosion combinatoire dans `compute_hash`.  
- Les opérations de hachage ne dépendent pas de l'entrée symbolique (la clé).  
- Le seul point où la clé symbolique intervient est le `strcmp` final.  
- Le `strcmp` génère des contraintes linéaires (égalité caractère par caractère).

Sur un keygenme où la clé serait transformée *avant* comparaison (par exemple, un XOR entre la clé saisie et un masque, suivi d'un `strcmp` sur le résultat), angr devrait propager les symboles à travers la transformation, générant des contraintes plus complexes — mais Z3 sait les résoudre efficacement tant que les opérations restent arithmétiques/logiques.

---

## Limites et quand angr échoue

L'exécution symbolique n'est pas une solution miracle. Elle a des limites structurelles qu'il est important de connaître :

### Explosion de chemins

Chaque branchement conditionnel qui dépend d'une valeur symbolique double le nombre d'états. Une boucle de N itérations sur un buffer symbolique peut créer 2^N états. Sur notre keygenme, ce n'est pas un problème (la boucle de hachage parcourt le username concret, pas la clé symbolique), mais sur un binaire où la clé est parcourue dans une boucle avec des conditions dépendant de chaque caractère, l'explosion est réelle.

**Parade** : utiliser `call_state` pour démarrer après les boucles problématiques, ou ajouter des contraintes pour réduire l'espace de recherche.

### Appels système et I/O

angr simule un sous-ensemble des syscalls Linux. Les programmes qui utilisent des fichiers, des sockets réseau, des threads ou des mécanismes IPC complexes peuvent provoquer des erreurs de simulation.

**Parade** : hooker les fonctions problématiques avec des SimProcedures custom, ou utiliser `call_state` pour contourner la phase d'I/O.

### Fonctions crypto complexes

Les fonctions de chiffrement modernes (AES, SHA-256…) impliquent des S-boxes (tables de substitution) indexées par des valeurs symboliques. Chaque accès à la table génère 256 branches possibles — l'explosion de chemins est immédiate.

**Parade** : extraire la clé par d'autres moyens (GDB, Frida — chapitre 24) ou modéliser la fonction crypto comme une boîte noire en fournissant sa spécification à Z3 manuellement (chapitre 18, section 4).

### Temps d'exploration

Même sans explosion de chemins, l'exécution symbolique est intrinsèquement lente car chaque instruction est interprétée (pas exécutée nativement). Sur notre petit keygenme, l'exploration prend quelques secondes. Sur un binaire de 10 Mo, elle peut prendre des heures.

---

## Synthèse

L'exécution symbolique avec angr offre une approche complémentaire au RE manuel :

| Approche | Comprend l'algorithme ? | Produit une clé ? | Effort humain | Temps machine |  
|---|---|---|---|---|  
| Patching (21.6) | Non | Non (bypass) | Moyen | Nul |  
| angr (21.7) | Non | **Oui** | Faible (script) | Secondes à minutes |  
| Keygen manuel (21.8) | **Oui** | **Oui** | Élevé | Nul |

angr est particulièrement puissant quand :
- On veut une clé valide rapidement sans comprendre l'algorithme en détail.  
- L'algorithme est complexe et difficile à reconstruire manuellement.  
- On a plusieurs variantes d'un même binaire à résoudre (le script est réutilisable).

Mais angr ne remplace pas la compréhension. Pour écrire un **keygen** — un programme qui génère des clés valides pour n'importe quel username à la demande — il faut comprendre et reproduire l'algorithme. C'est l'objectif de la section suivante (21.8).

⏭️ [Écriture d'un keygen en Python avec `pwntools`](/21-keygenme/08-keygen-pwntools.md)
