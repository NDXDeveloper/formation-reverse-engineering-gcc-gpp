🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 18.2 — angr — installation et architecture (SimState, SimManager, exploration)

> **Chapitre 18 — Exécution symbolique et solveurs de contraintes**  
> Partie IV — Techniques Avancées de RE

---

## Qu'est-ce qu'angr ?

angr est un framework d'analyse binaire développé par le laboratoire **SecLab** de l'université UC Santa Barbara (UCSB). Écrit en Python, il offre un ensemble cohérent d'outils pour charger un binaire, le désassembler, construire son graphe de flux de contrôle, et surtout l'exécuter symboliquement — le tout depuis un script Python ou un shell interactif.

angr est né dans le monde des CTF (Capture The Flag), où il est devenu l'arme de choix pour résoudre automatiquement des crackmes et des challenges de reverse engineering. Mais ses applications dépassent largement le cadre des compétitions : analyse de vulnérabilités, vérification de propriétés de sécurité, exploration automatique de code, fuzzing guidé par l'exécution symbolique…

Ce qui distingue angr des autres outils d'exécution symbolique (KLEE, Manticore, Triton), c'est sa capacité à travailler directement sur des **binaires compilés** — pas sur du code source, pas sur du bytecode LLVM — et sa flexibilité d'utilisation via une API Python riche et bien documentée.

---

## Installation

### Prérequis

angr nécessite Python **3.10 ou supérieur**. L'installation dans un **environnement virtuel dédié** est quasi obligatoire : angr embarque des dizaines de dépendances (dont ses propres forks de certaines bibliothèques) qui peuvent entrer en conflit avec d'autres paquets Python sur votre système.

### Installation dans un virtualenv

```bash
# Créer un environnement virtuel dédié
python3 -m venv ~/angr-env

# L'activer
source ~/angr-env/bin/activate

# Installer angr (tire toutes les dépendances automatiquement)
pip install angr

# Vérifier l'installation
python3 -c "import angr; print(angr.__version__)"
```

L'installation peut prendre plusieurs minutes : angr compile certaines dépendances C (notamment `unicorn`, le moteur d'émulation CPU, et `z3-solver`). Sur une machine modeste, comptez 5 à 10 minutes.

### Dépendances notables installées automatiquement

angr n'est pas un outil monolithique — c'est un écosystème de bibliothèques développées par la même équipe. Comprendre ces briques vous aidera à naviguer dans la documentation et les messages d'erreur :

| Bibliothèque | Rôle |  
|---|---|  
| **CLE** (*CLE Loads Everything*) | Chargeur de binaires. Lit les formats ELF, PE, Mach-O, les bibliothèques partagées, gère le mappage mémoire et la résolution des imports. Équivalent conceptuel du loader Linux (`ld.so`) vu au chapitre 2. |  
| **archinfo** | Base de données des architectures supportées (x86, x86-64, ARM, MIPS, PPC…). Définit les registres, les tailles de mots, les conventions d'appel. |  
| **pyvex** | Traducteur de code machine vers l'IR (Intermediate Representation) **VEX**, la même représentation intermédiaire que celle utilisée par Valgrind (chapitre 14). Chaque instruction x86-64 est décomposée en micro-opérations VEX. |  
| **claripy** | Bibliothèque de manipulation de bitvectors symboliques et concrets. Interface Python vers le solveur Z3. C'est la couche qui crée les variables symboliques, construit les expressions et interroge le solveur. |  
| **SimEngine** | Moteur d'exécution : interprète les instructions VEX en propageant les valeurs (symboliques ou concrètes) à travers les registres et la mémoire. |  
| **unicorn** | Moteur d'émulation CPU (optionnel). Utilisé pour accélérer l'exécution des portions de code purement concrètes (sans valeurs symboliques). |

### Vérification rapide

Ouvrez un shell Python dans votre virtualenv et testez le chargement du keygenme :

```python
import angr

# Charger le binaire
proj = angr.Project("./keygenme_O0", auto_load_libs=False)

# Informations de base
print(f"Architecture : {proj.arch.name}")  
print(f"Entry point  : {hex(proj.entry)}")  
print(f"Nom du binaire : {proj.filename}")  
```

Si vous voyez `Architecture : AMD64` et une adresse d'entry point sans erreur, l'installation est fonctionnelle.

> 💡 **`auto_load_libs=False`** — Ce paramètre indique à CLE de ne **pas** charger les bibliothèques partagées (libc, ld-linux, etc.). C'est presque toujours ce que vous voulez en exécution symbolique : charger la libc introduirait des milliers de fonctions supplémentaires à explorer, ce qui ferait exploser l'espace des chemins. angr remplace les fonctions de bibliothèque par des **SimProcedures** — des modèles simplifiés écrits en Python qui simulent le comportement des fonctions standards (`strlen`, `strcmp`, `printf`, `malloc`…) sans exécuter leur code machine réel.

---

## Architecture d'angr : vue d'ensemble

Voici comment les différentes briques s'articulent lorsque vous utilisez angr pour résoudre un crackme :

```
  Votre script Python
         │
         ▼
  ┌─────────────────────────────────────────────────────┐
  │                    angr.Project                     │
  │                                                     │
  │   ┌──────────┐    ┌──────────┐     ┌──────────────┐ │
  │   │   CLE    │    │ archinfo │     │   pyvex      │ │
  │   │ (loader) │    │ (archi)  │     │ (x86→VEX IR) │ │
  │   └────┬─────┘    └──────────┘     └──────┬───────┘ │
  │        │                                  │         │
  │        ▼                                  ▼         │
  │   Mémoire mappée              Instructions VEX IR   │
  │                                          │          │
  │                    ┌─────────────────────┐          │
  │                    │     SimEngine       │          │
  │                    │ (exécution symb.)   │          │
  │                    └────────┬────────────┘          │
  │                             │                       │
  │              ┌──────────────┼───────────────┐       │
  │              ▼              ▼               ▼       │
  │        ┌──────────┐   ┌──────────┐    ┌──────────┐  │
  │        │ SimState │   │ SimState │    │ SimState │  │
  │        │ (chemin1)│   │ (chemin2)│    │ (chemin3)│  │
  │        └────┬─────┘   └────┬─────┘    └────┬─────┘  │
  │             │              │               │        │
  │             └──────────────┼───────────────┘        │
  │                            ▼                        │
  │                  ┌──────────────────┐               │
  │                  │SimulationManager │               │
  │                  │ (orchestrateur)  │               │
  │                  └────────┬─────────┘               │
  │                           │                         │
  │                           ▼                         │
  │                     ┌──────────┐                    │
  │                     │ claripy  │                    │
  │                     │ (Z3)     │                    │
  │                     └──────────┘                    │
  └─────────────────────────────────────────────────────┘
```

Détaillons les trois composants centraux que vous manipulerez dans chaque script angr.

---

## Le Project : point d'entrée de toute analyse

Tout commence par la création d'un objet `Project`. C'est l'équivalent de « ouvrir un binaire dans Ghidra » — le binaire est chargé, parsé, et prêt à être analysé.

```python
import angr

proj = angr.Project("./keygenme_O0", auto_load_libs=False)
```

Le `Project` donne accès à :

- **`proj.loader`** — L'objet CLE qui a chargé le binaire. Vous pouvez inspecter les segments, les sections, les symboles importés et exportés, et les bibliothèques liées.  
- **`proj.arch`** — L'architecture détectée. Contient des informations sur les registres (`proj.arch.registers`), la taille des pointeurs, l'endianness, etc.  
- **`proj.entry`** — L'adresse du point d'entrée (`_start`), pas celle de `main`.  
- **`proj.factory`** — La *factory* qui permet de créer des états, des blocs de base, des graphes d'appels et des `SimulationManager`.

### Trouver l'adresse de `main`

Si le binaire contient des symboles (compilé avec `-g`, non strippé), angr peut résoudre `main` directement :

```python
main_addr = proj.loader.find_symbol("main").rebased_addr  
print(f"main() est à l'adresse : {hex(main_addr)}")  
```

Si le binaire est strippé, il faudra trouver l'adresse de `main` par d'autres moyens — par exemple en la repérant dans Ghidra (chapitre 8) ou avec `objdump` (chapitre 7), puis en la passant manuellement à angr.

---

## Le SimState : un snapshot du programme

Un `SimState` (état de simulation) représente l'**état complet** du programme à un instant donné : les valeurs de tous les registres, le contenu de la mémoire, les descripteurs de fichiers ouverts, et surtout l'ensemble des **contraintes** accumulées sur les variables symboliques.

### Créer un état initial

```python
# État démarrant à l'entry point du binaire
state = proj.factory.entry_state()

# État démarrant à une adresse spécifique (par ex. main)
state = proj.factory.blank_state(addr=0x401156)
```

**`entry_state()`** crée un état qui simule le démarrage complet du programme (passage par `_start`, initialisation de la libc, puis appel à `main`). C'est le plus réaliste mais aussi le plus lent.

**`blank_state(addr=...)`** crée un état « vide » positionné à une adresse arbitraire, avec la pile et les registres initialisés à des valeurs symboliques non contraintes. C'est plus rapide mais peut nécessiter un setup manuel (initialiser les arguments, la mémoire…).

### Inspecter un état

Les registres et la mémoire d'un état peuvent contenir des valeurs symboliques ou concrètes :

```python
# Lire la valeur du registre rip (64 bits)
print(state.regs.rip)           # <BV64 0x401156>  (concret)

# Lire 4 octets en mémoire à une adresse donnée
val = state.memory.load(0x404000, 4, endness=proj.arch.memory_endness)  
print(val)                       # Peut être symbolique ou concret  
```

### Accéder au solveur

Chaque état embarque son propre **solveur** via `state.solver`. C'est l'interface vers claripy et Z3 :

```python
# Créer un bitvector symbolique de 64 bits
x = state.solver.BVS("x", 64)

# Ajouter une contrainte
state.solver.add(x > 0x1000)  
state.solver.add(x < 0x2000)  

# Demander une solution concrète
solution = state.solver.eval(x)  
print(f"Solution : {hex(solution)}")  

# Vérifier si les contraintes sont satisfiables
print(state.solver.satisfiable())  # True ou False
```

Ce mécanisme est au cœur de l'exécution symbolique : chaque branchement conditionnel ajoute des contraintes au solveur de l'état, et à la fin on demande au solveur de produire une valeur concrète pour les entrées.

---

## Le SimulationManager : orchestrer l'exploration

Le `SimulationManager` (souvent abrégé `simgr`) est le chef d'orchestre de l'exécution symbolique. Il gère une collection d'états et les fait progresser dans le binaire, en gérant les bifurcations, le filtrage et les stratégies d'exploration.

### Création

```python
simgr = proj.factory.simgr(state)
```

### Les stashes : catégoriser les états

Le `SimulationManager` range les états dans des **stashes** (« réserves ») nommés. Chaque stash est une simple liste Python d'objets `SimState` :

| Stash | Contenu |  
|---|---|  
| **`active`** | Les états en cours d'exploration. À chaque appel à `.step()`, ces états avancent d'un bloc de base. |  
| **`found`** | Les états qui ont atteint une **adresse cible** (définie par vous). C'est ce qui vous intéresse. |  
| **`avoided`** | Les états qui ont atteint une **adresse à éviter** (par ex. `"Access Denied."`). Ils sont écartés de l'exploration. |  
| **`deadended`** | Les états qui ont terminé normalement (appel à `exit`, fin de `main`…). |  
| **`errored`** | Les états qui ont provoqué une erreur interne d'angr (instruction non supportée, accès mémoire impossible…). |  
| **`unsat`** | Les états dont les contraintes sont devenues insatisfiables (chemin impossible). |

Vous pouvez inspecter les stashes à tout moment :

```python
print(f"États actifs : {len(simgr.active)}")  
print(f"États trouvés : {len(simgr.found)}")  
print(f"États évités : {len(simgr.avoided)}")  
```

### Faire avancer l'exploration

La méthode `.step()` fait avancer **tous** les états actifs d'un bloc de base. À chaque branchement conditionnel, un état se dédouble en deux (ou plus) nouveaux états, chacun avec ses contraintes mises à jour :

```python
# Un pas d'exploration
simgr.step()

# L'état initial s'est peut-être divisé
print(f"États actifs après un step : {len(simgr.active)}")
```

Appeler `.step()` en boucle manuellement serait fastidieux. La méthode `.explore()` automatise le processus en prenant des critères d'arrêt.

---

## La méthode `.explore()` : le cœur du workflow

La méthode `explore()` est celle que vous utiliserez le plus souvent. Elle fait avancer l'exploration automatiquement jusqu'à ce qu'un état atteigne une adresse cible ou que tous les chemins soient épuisés :

```python
simgr.explore(
    find=0x40125A,     # Adresse de puts("Access Granted!")
    avoid=0x40126E     # Adresse de puts("Access Denied.")
)
```

### Que fait `explore()` en interne ?

À chaque itération :

1. Chaque état du stash `active` avance d'un bloc de base.  
2. Si un état atteint l'adresse `find`, il est déplacé dans le stash `found`.  
3. Si un état atteint une adresse `avoid`, il est déplacé dans le stash `avoided` et **n'est plus exploré** — c'est un élagage qui réduit considérablement l'espace de recherche.  
4. Si un état atteint un `exit` ou un chemin impossible, il est déplacé dans `deadended` ou `unsat`.  
5. L'exploration continue tant que le stash `active` n'est pas vide et que `found` n'a pas reçu d'état.

### Utiliser des fonctions comme critères

Au lieu d'adresses numériques, vous pouvez passer des **fonctions Python** qui reçoivent un état et retournent `True`/`False`. C'est plus lisible et plus flexible :

```python
simgr.explore(
    find=lambda s: b"Access Granted" in s.posix.dumps(1),
    avoid=lambda s: b"Access Denied" in s.posix.dumps(1)
)
```

Ici, `s.posix.dumps(1)` retourne tout ce que l'état a écrit sur **stdout** (descripteur de fichier 1). On vérifie simplement si le message de succès apparaît dans la sortie. Cette approche a l'avantage de fonctionner même sur un binaire strippé où l'on ne connaît pas les adresses exactes — il suffit de connaître les chaînes affichées.

### Extraire la solution

Une fois qu'un état est dans `found`, on peut interroger son solveur pour obtenir les valeurs concrètes des entrées symboliques :

```python
if simgr.found:
    found_state = simgr.found[0]

    # Si l'entrée est argv[1]
    serial = found_state.solver.eval(argv1_symbolic, cast_to=bytes)
    print(f"Serial valide : {serial}")
else:
    print("Aucune solution trouvée.")
```

Nous verrons le code complet dans la section 18.3.

---

## claripy : manipuler les bitvectors

claripy est la bibliothèque qui sous-tend toute la manipulation symbolique dans angr. Vous l'utiliserez directement quand vous voudrez créer des variables symboliques, poser des contraintes manuellement ou construire des expressions.

### Bitvectors symboliques et concrets

```python
import claripy

# Bitvector symbolique : 64 bits, nommé "serial"
serial_sym = claripy.BVS("serial", 64)

# Bitvector concret : 64 bits, valeur 0xDEADBEEF
magic = claripy.BVV(0xDEADBEEF, 64)
```

Le `S` de `BVS` signifie *Symbolic*, le `V` de `BVV` signifie *Value* (concret).

### Opérations sur les bitvectors

Les bitvectors supportent toutes les opérations que le processeur effectue — ce sont les mêmes opérations que Z3 sait résoudre :

```python
a = claripy.BVS("a", 32)  
b = claripy.BVS("b", 32)  

# Arithmétique
expr1 = a + b  
expr2 = a * 3 + 7  
expr3 = a - claripy.BVV(100, 32)  

# Bit à bit
expr4 = a ^ b  
expr5 = a >> 16          # Shift logique à droite  
expr6 = a & 0xFF         # Masquage des 8 bits bas  

# Comparaisons (retournent des expressions booléennes)
cond1 = a > b            # Non signé par défaut  
cond2 = claripy.SGT(a, b)  # Signé (Signed Greater Than)  
cond3 = a == claripy.BVV(0x1337, 32)  

# Concaténation et extraction
full = claripy.Concat(a, b)           # 64 bits (a || b)  
low_byte = claripy.Extract(7, 0, a)   # 8 bits de poids faible de a  
```

### Contraindre des caractères

Un usage fréquent en RE : contraindre les entrées pour qu'elles soient des caractères ASCII imprimables (ou hexadécimaux, comme dans notre keygenme) :

```python
# Créer un caractère symbolique de 8 bits
c = claripy.BVS("c", 8)

# Contraindre à un caractère hexadécimal [0-9A-Fa-f]
is_digit   = claripy.And(c >= ord('0'), c <= ord('9'))  
is_upper   = claripy.And(c >= ord('A'), c <= ord('F'))  
is_lower   = claripy.And(c >= ord('a'), c <= ord('f'))  
is_hex     = claripy.Or(is_digit, is_upper, is_lower)  
```

Ajouter ces contraintes sur les entrées **avant** de lancer l'exploration réduit drastiquement l'espace de recherche et accélère la résolution.

---

## Les SimProcedures : simuler la libc

Quand angr rencontre un appel à `strlen`, `strcmp`, `printf` ou toute autre fonction de la bibliothèque standard, il ne l'exécute pas réellement (ce serait beaucoup trop complexe symboliquement). À la place, il utilise des **SimProcedures** — des implémentations Python simplifiées de ces fonctions qui savent raisonner sur des arguments symboliques.

Par exemple, la SimProcedure pour `strlen` sait que si on lui passe un pointeur vers un buffer contenant des octets symboliques, le résultat dépend de la position du premier octet nul — et elle encode cette relation comme une contrainte.

angr fournit des SimProcedures pour plusieurs centaines de fonctions standards. Vous pouvez les lister :

```python
# Voir quelles fonctions sont hookées par des SimProcedures
for name, simproc in proj._sim_procedures.items():
    print(f"  {name} → {simproc.__class__.__name__}")
```

Vous pouvez aussi écrire vos propres SimProcedures pour remplacer des fonctions custom du binaire. C'est un outil puissant quand une fonction pose problème à l'exécution symbolique (trop complexe, trop de chemins, appel système non supporté) :

```python
class MySkipFunction(angr.SimProcedure):
    """Remplace une fonction en retournant toujours 0."""
    def run(self):
        return 0

# Hooker la fonction à l'adresse 0x401000
proj.hook(0x401000, MySkipFunction())
```

Nous verrons des cas concrets d'utilisation de hooks dans la section 18.3.

---

## Stratégies d'exploration

Par défaut, `explore()` utilise une stratégie **BFS** (Breadth-First Search) : tous les états actifs avancent d'un pas à chaque itération. Cette stratégie est équitable mais peut être lente si beaucoup de chemins divergent.

angr propose d'autres stratégies via le paramètre `techniques` du `SimulationManager` :

```python
# DFS : explore un chemin en profondeur avant de passer au suivant
simgr = proj.factory.simgr(state)  
simgr.use_technique(angr.exploration_techniques.DFS())  
simgr.explore(find=target, avoid=bad)  
```

Les techniques d'exploration les plus utiles :

| Technique | Comportement | Quand l'utiliser |  
|---|---|---|  
| **BFS** (défaut) | Avance tous les états en parallèle, niveau par niveau. | Bon choix par défaut, exploration équitable. |  
| **DFS** | Explore un seul chemin jusqu'au bout avant de revenir en arrière. | Quand la solution est « profonde » mais peu ramifiée. |  
| **LengthLimiter** | Limite le nombre maximum de blocs de base qu'un état peut traverser. | Empêche les boucles infinies de bloquer l'exploration. |  
| **LoopSeer** | Détecte les boucles et limite le nombre d'itérations autorisées. | Indispensable dès que le binaire contient des boucles. |  
| **Veritesting** | Fusionne des chemins qui se rejoignent (*merging*), réduisant le nombre d'états. | Réduit l'explosion des chemins dans certains cas. |

Vous pouvez combiner plusieurs techniques :

```python
simgr.use_technique(angr.exploration_techniques.DFS())  
simgr.use_technique(angr.exploration_techniques.LengthLimiter(max_length=500))  
```

Le choix de la stratégie d'exploration est souvent un facteur déterminant entre une résolution en 10 secondes et un timeout après 30 minutes. Nous y reviendrons dans la section 18.5 sur les limites de l'exécution symbolique.

---

## Passer des arguments au programme

Notre keygenme attend un argument en ligne de commande (`argv[1]`). Il faut indiquer à angr que cet argument est **symbolique**. Voici comment :

```python
import angr  
import claripy  

proj = angr.Project("./keygenme_O0", auto_load_libs=False)

# Créer un bitvector symbolique de 16 octets (16 chars hex × 8 bits)
serial_len = 16  
serial_chars = [claripy.BVS(f"c{i}", 8) for i in range(serial_len)]  
serial_bvs = claripy.Concat(*serial_chars)  

# Construire argv : [nom_du_programme, serial_symbolique]
# angr attend des arguments sous forme d'objets claripy
state = proj.factory.entry_state(
    args=["./keygenme_O0", serial_bvs]
)

# Contraindre chaque caractère à être hexadécimal
for c in serial_chars:
    is_digit = claripy.And(c >= ord('0'), c <= ord('9'))
    is_upper = claripy.And(c >= ord('A'), c <= ord('F'))
    is_lower = claripy.And(c >= ord('a'), c <= ord('f'))
    state.solver.add(claripy.Or(is_digit, is_upper, is_lower))
```

Ce pattern — créer des caractères symboliques individuels, les concaténer, les contraindre, puis les passer comme argument — est le pattern le plus courant en CTF et en RE avec angr. Vous le retrouverez quasiment à l'identique dans chaque script de résolution.

---

## Récapitulatif de l'API essentielle

Voici les appels que vous utiliserez dans 90% de vos scripts angr :

```python
import angr  
import claripy  

# 1. Charger le binaire
proj = angr.Project("./binaire", auto_load_libs=False)

# 2. Créer des entrées symboliques
sym_input = claripy.BVS("input", N_BITS)

# 3. Créer un état initial
state = proj.factory.entry_state(args=[...])
# ou
state = proj.factory.blank_state(addr=ADDR)

# 4. Ajouter des contraintes sur les entrées
state.solver.add(CONTRAINTE)

# 5. Créer le SimulationManager
simgr = proj.factory.simgr(state)

# 6. Explorer
simgr.explore(find=ADDR_SUCCESS, avoid=ADDR_FAIL)

# 7. Extraire la solution
if simgr.found:
    s = simgr.found[0]
    solution = s.solver.eval(sym_input, cast_to=bytes)
    print(solution)
```

Ces sept étapes constituent le squelette de tout script d'exécution symbolique avec angr. La section 18.3 va les mettre en œuvre sur notre keygenme, du début à la fin.

---

## Points clés à retenir

- angr est un **framework Python** qui charge, désassemble et exécute symboliquement des binaires compilés, sans accès au code source.

- Le **Project** charge le binaire via CLE et donne accès à toutes les analyses.

- Un **SimState** représente l'état complet du programme (registres, mémoire, contraintes) à un point donné de l'exécution.

- Le **SimulationManager** orchestre l'exploration en gérant des stashes d'états (`active`, `found`, `avoided`, `deadended`…).

- **claripy** est la bibliothèque de bitvectors symboliques : `BVS` pour les symboles, `BVV` pour les valeurs concrètes, opérations arithmétiques et bit à bit, contraintes.

- Les **SimProcedures** remplacent les fonctions de bibliothèque (libc) par des modèles Python capables de raisonner symboliquement.

- **`auto_load_libs=False`** est quasi obligatoire pour éviter l'explosion des chemins due au chargement de la libc complète.

- Le choix de la **stratégie d'exploration** (BFS, DFS, LoopSeer…) peut faire la différence entre une résolution rapide et un timeout.

---

> Dans la section suivante (18.3), nous allons assembler toutes ces briques pour **résoudre automatiquement le keygenme** : du chargement du binaire à l'extraction du serial valide, en une vingtaine de lignes de Python.

⏭️ [Résoudre un crackme automatiquement avec angr](/18-execution-symbolique/03-resoudre-crackme-angr.md)
