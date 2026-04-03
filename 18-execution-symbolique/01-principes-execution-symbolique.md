🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 18.1 — Principes de l'exécution symbolique : traiter les inputs comme des symboles

> **Chapitre 18 — Exécution symbolique et solveurs de contraintes**  
> Partie IV — Techniques Avancées de RE

---

## Le problème posé par l'exécution concrète

Revenons un instant sur ce que vous faites depuis le début de cette formation quand vous analysez dynamiquement un binaire. Avec GDB (chapitre 11), vous lancez le programme avec une **entrée concrète** — par exemple `./keygenme AAAA1111BBBB2222` — et vous observez ce qui se passe : quels registres prennent quelles valeurs, quel branchement est emprunté, où le programme décide de rejeter votre entrée.

Cette approche fonctionne, mais elle a une limitation fondamentale : **vous n'explorez qu'un seul chemin à la fois**. Si votre entrée échoue au premier `if`, vous n'avez aucune information sur ce qui se passerait si ce `if` était satisfait. Vous devez alors modifier votre entrée, relancer, observer à nouveau, et ainsi de suite. C'est un processus essentiellement manuel, guidé par votre intuition et votre compréhension du code.

Considérons un cas concret. Voici un fragment simplifié de la logique de vérification de notre keygenme :

```c
uint32_t high = parse_hex(serial, 0, 8);  
uint32_t low  = parse_hex(serial, 8, 8);  

feistel4(&high, &low);

if (high == 0xA11C3514 && low == 0xF00DCAFE) {
    puts("Access Granted!");
} else {
    puts("Access Denied.");
}
```

Pour trouver le bon serial par exécution concrète, il faudrait théoriquement essayer les 2⁶⁴ combinaisons possibles d'entrées sur 16 caractères hexadécimaux. Même à un milliard de tentatives par seconde, cela prendrait environ 585 ans. Le bruteforce n'est pas une option.

L'exécution symbolique propose une approche radicalement différente.

---

## L'idée centrale : des symboles à la place des valeurs

Au lieu de dire « `high` vaut `0x41414141` » (la valeur concrète correspondant à `"AAAA"`), l'exécution symbolique dit :

> « `high` vaut **α**, où **α** est un entier non signé de 32 bits dont on ne connaît pas encore la valeur. »

De la même manière, `low` devient **β**. Ces deux inconnues sont des **variables symboliques** — exactement comme les variables *x* et *y* que vous manipuliez en algèbre au lycée.

À partir de là, le moteur d'exécution symbolique ne calcule plus des résultats numériques : il construit des **expressions symboliques**. Quand le programme exécute `high ^= 0x5A3CE7F1`, le moteur ne produit pas un nombre, il produit l'expression **α ⊕ 0x5A3CE7F1**. Quand le programme exécute ensuite un shift et une multiplication, l'expression se complexifie, mais elle reste une formule en fonction de **α** et **β**.

Cette idée simple a des conséquences profondes : à la fin de l'exécution, le moteur n'a pas « une réponse », il a **un système d'équations** qui décrit toutes les transformations subies par les entrées.

---

## Exécution symbolique pas à pas

Prenons un exemple volontairement minimaliste pour comprendre le mécanisme. Oubliez le keygenme un instant et considérez cette fonction :

```c
int check(int x) {
    int y = x * 3 + 7;
    if (y > 100) {
        if (x % 2 == 0) {
            return 1;  // SUCCESS
        }
        return 0;      // FAIL path A
    }
    return 0;          // FAIL path B
}
```

### Étape 1 — Initialisation de l'état symbolique

Le moteur crée un **état initial** où `x` n'est pas un nombre mais un symbole **α** (un entier signé de 32 bits).

```
État initial :
  x = α          (symbolique, 32 bits signé)
  Contraintes :  ∅  (aucune contrainte pour l'instant)
```

### Étape 2 — Exécution de `y = x * 3 + 7`

Le moteur évalue l'instruction symboliquement :

```
  y = α × 3 + 7
```

Il ne connaît pas la valeur de `y`, mais il connaît sa **relation** avec `α`.

### Étape 3 — Le premier branchement : `if (y > 100)`

C'est ici que l'exécution symbolique diverge fondamentalement de l'exécution concrète. Au lieu de prendre **un** côté du branchement, le moteur explore **les deux** en créant deux états distincts :

```
État A (branche THEN) :
  x = α
  y = α × 3 + 7
  Contraintes : { α × 3 + 7 > 100 }

État B (branche ELSE) :
  x = α
  y = α × 3 + 7
  Contraintes : { α × 3 + 7 ≤ 100 }
```

Chaque état transporte avec lui l'**ensemble des contraintes** accumulées le long de son chemin. On appelle cet ensemble le *path constraint* (contrainte de chemin).

### Étape 4 — Le deuxième branchement : `if (x % 2 == 0)`

L'état A atteint un nouveau branchement et se dédouble à nouveau :

```
État A1 (SUCCESS) :
  Contraintes : { α × 3 + 7 > 100,  α mod 2 = 0 }

État A2 (FAIL path A) :
  Contraintes : { α × 3 + 7 > 100,  α mod 2 ≠ 0 }
```

L'état B, lui, a atteint `return 0` directement et ne se divise plus.

### Étape 5 — Interroger le solveur

Nous avons maintenant trois ensembles de contraintes, un par chemin terminal. Si notre objectif est d'atteindre `return 1` (SUCCESS), on soumet les contraintes de l'état A1 à un **solveur SMT** :

```
Trouver α ∈ Z₃₂ tel que :
  α × 3 + 7 > 100
  α mod 2 = 0
```

La première contrainte se simplifie en **α > 31**. Combinée avec la parité, le solveur peut proposer par exemple **α = 32**. On vérifie : `32 × 3 + 7 = 103 > 100` ✓, `32 mod 2 = 0` ✓. C'est une entrée valide.

Le solveur a trouvé **la** valeur (ou plutôt **une** valeur parmi celles possibles) qui mène au chemin désiré, sans jamais exécuter le programme avec une entrée concrète.

---

## L'arbre d'exécution symbolique

Ce processus de bifurcation à chaque branchement produit une structure que l'on appelle l'**arbre d'exécution symbolique** (Symbolic Execution Tree). Chaque nœud interne correspond à un branchement conditionnel, et chaque feuille correspond à un chemin terminal du programme (sortie, crash, boucle infinie…).

```
                          check(α)
                             │
                        y = α×3 + 7
                             │
                    ┌────────┴────────┐
                    │                 │
              y > 100 ?          y ≤ 100 ?
           (α×3+7 > 100)     (α×3+7 ≤ 100)
                    │                 │
            ┌───────┴──────┐      return 0
            │              │     (FAIL path B)
       α mod 2 = 0   α mod 2 ≠ 0
            │              │
        return 1       return 0
        (SUCCESS)     (FAIL path A)
```

Dans notre exemple à deux branchements, l'arbre a 3 feuilles. C'est gérable. Mais imaginez un programme avec 50 branchements successifs : l'arbre peut théoriquement avoir **2⁵⁰ feuilles** (plus d'un million de milliards). C'est le problème de l'explosion des chemins, que nous aborderons en détail dans la section 18.5.

---

## Vocabulaire de référence

Avant d'aller plus loin, fixons les termes que vous rencontrerez dans la documentation d'angr, dans les articles de recherche et dans la communauté RE :

**Variable symbolique** (*symbolic variable*) — Une inconnue représentant une entrée du programme. Dans angr, on la crée avec `claripy.BVS("nom", taille_en_bits)` — un **bitvector symbolique**. La taille en bits est cruciale : un `uint32_t` est un bitvector de 32 bits, un `char` est un bitvector de 8 bits.

**Valeur concrète** (*concrete value*) — Le contraire d'une valeur symbolique : un nombre fixe, comme `0x41` ou `42`. Dans angr, `claripy.BVV(0x41, 8)` crée un bitvector **concret** de 8 bits valant `0x41`.

**État symbolique** (*symbolic state*) — Un snapshot de l'ensemble du contexte du programme à un point donné : les registres (certains symboliques, d'autres concrets), la mémoire (idem), et l'ensemble des contraintes de chemin accumulées. Dans angr, c'est un objet `SimState`.

**Contrainte de chemin** (*path constraint*) — La conjonction (ET logique) de toutes les conditions rencontrées le long d'un chemin d'exécution. Chaque branchement ajoute soit la condition elle-même (branche THEN), soit sa négation (branche ELSE). Le solveur doit satisfaire **toutes** les contraintes simultanément.

**Solveur SMT** (*Satisfiability Modulo Theories*) — Un programme capable de déterminer si un ensemble de contraintes sur des bitvectors, des tableaux, de l'arithmétique… admet une solution, et si oui, d'en fournir une. **Z3** (Microsoft Research) est le solveur SMT de référence et celui utilisé par angr en interne.

**Satisfiable / Unsatisfiable** — Un ensemble de contraintes est *satisfiable* (SAT) s'il existe au moins une affectation des variables symboliques qui rend toutes les contraintes vraies. Il est *unsatisfiable* (UNSAT) si aucune affectation ne fonctionne — ce qui signifie que le chemin correspondant est **impossible** (dead path).

**Exploration** (*exploration*) — Le processus par lequel le moteur d'exécution symbolique parcourt l'arbre des chemins, décide quels états faire progresser, et applique éventuellement des stratégies pour élaguer les branches inutiles. Dans angr, c'est le `SimulationManager` qui orchestre l'exploration.

---

## De la théorie au reverse engineering

Replaçons tout cela dans le contexte du reverse engineering d'un binaire compilé avec GCC.

### On n'a pas le code source

L'exemple précédent partait du code C pour construire l'arbre. En pratique, le moteur d'exécution symbolique travaille directement sur le **binaire** — sur les instructions machine, pas sur le code source. angr, par exemple, traduit le binaire en une représentation intermédiaire (VEX IR, le même IR que celui de Valgrind) puis exécute cette IR symboliquement. Cela signifie que l'exécution symbolique fonctionne sans les sources, sans les symboles, et même sur des binaires strippés et optimisés.

### Les branchements sont des instructions machine

Un `if` en C devient un `cmp` suivi d'un `jz` ou `jnz` en x86-64. Pour le moteur symbolique, c'est exactement la même chose : l'instruction `cmp rax, 0xA11C3514` suivie de `jne fail` crée une bifurcation avec deux contraintes miroir :

- Branche THEN : la valeur symbolique dans `rax` est égale à `0xA11C3514`.  
- Branche ELSE : la valeur symbolique dans `rax` est différente de `0xA11C3514`.

### L'objectif est défini par des adresses

En exécution symbolique appliquée au RE, on ne dit pas « je veux atteindre `return 1` ». On dit **« je veux atteindre l'adresse `0x401234` »** (celle du `puts("Access Granted!")`) **et éviter l'adresse `0x401250`** (celle du `puts("Access Denied.")`). Le moteur explore l'arbre en cherchant un chemin qui mène à l'adresse cible sans passer par les adresses à éviter. Une fois ce chemin trouvé, il collecte ses contraintes et interroge le solveur.

C'est exactement ce que nous ferons dans la section 18.3 avec angr sur notre keygenme.

### Le solveur fait le travail inverse

Le point essentiel est celui-ci : vous n'avez **pas besoin de comprendre** la routine de vérification dans le détail pour la résoudre. Le réseau de Feistel à 4 tours de notre keygenme, avec ses shifts, ses multiplications et ses XOR chaînés, produit un assembleur dense et difficile à inverser mentalement. Mais le moteur symbolique se contente de **propager** les expressions à travers chaque instruction, et le solveur SMT résout le système d'équations résultant. Là où l'analyste humain devrait inverser la fonction `mix32` puis chaque tour de Feistel en raisonnant à l'envers, le solveur explore l'espace des solutions en quelques secondes.

C'est ce qui fait de l'exécution symbolique un outil aussi puissant pour le reverse engineering : elle **court-circuite la compréhension** du code. Vous n'avez besoin que de savoir *où* se trouve la condition de succès, pas *comment* elle fonctionne.

---

## Exécution symbolique, concolique et DSE

Il existe plusieurs variantes de l'exécution symbolique. Il est utile de les distinguer car vous les rencontrerez dans la littérature et dans les options de certains outils.

### Exécution symbolique pure (Static Symbolic Execution)

C'est ce que nous avons décrit jusqu'ici : toutes les entrées sont symboliques dès le départ, et le moteur explore l'arbre des chemins de manière exhaustive. C'est l'approche la plus puissante en théorie, mais aussi la plus sujette à l'explosion des chemins.

### Exécution concolique (Concolic Execution)

Le terme « concolique » est un mot-valise formé de **conc**ret et symb**olique**. L'idée est de combiner les deux : on lance le programme avec une **entrée concrète** (par exemple, un serial aléatoire) et on enregistre le chemin emprunté. En parallèle, on maintient les contraintes symboliques le long de ce chemin. Puis, on **nie** une des contraintes pour forcer l'exploration d'un chemin alternatif, et on relance avec la nouvelle entrée concrète produite par le solveur.

L'avantage est que l'exécution concrète permet de résoudre naturellement les interactions avec l'environnement (appels système, bibliothèques) là où l'exécution purement symbolique devrait les modéliser. L'inconvénient est que l'exploration est moins systématique.

L'outil de référence historique pour l'exécution concolique est **SAGE** (Microsoft), utilisé en interne pour le fuzzing de logiciels Windows. **KLEE** (basé sur LLVM) est un autre outil académique majeur qui combine les deux approches.

### Dynamic Symbolic Execution (DSE)

C'est le terme générique qui englobe les deux approches précédentes quand elles opèrent sur un programme en cours d'exécution (ou simulé). angr fait de la DSE : il simule l'exécution du binaire dans son propre moteur (SimEngine) tout en maintenant l'état symbolique. Il peut basculer entre exécution concrète et symbolique selon les besoins, ce qui lui donne une grande flexibilité.

---

## Ce que le solveur SMT sait résoudre

Le solveur SMT qui travaille en coulisses (Z3, dans le cas d'angr via la bibliothèque `claripy`) est capable de raisonner sur :

- L'**arithmétique des bitvectors** : addition, soustraction, multiplication, division, modulo — le tout sur des entiers de taille fixe (8, 16, 32, 64 bits), avec gestion du débordement (*overflow*) exactement comme le fait le processeur.  
- Les **opérations bit à bit** : AND, OR, XOR, NOT, shifts logiques et arithmétiques, rotations.  
- Les **comparaisons** : égalité, inégalité, supérieur/inférieur (signés et non signés).  
- Les **tableaux** (théorie des tableaux) : lecture et écriture à des indices symboliques dans un tableau, ce qui modélise les accès mémoire avec des pointeurs symboliques.  
- La **concaténation et l'extraction** de bits : prendre les 8 bits de poids faible d'un bitvector de 32 bits, concaténer deux bitvectors de 32 bits en un de 64, etc.

C'est précisément l'ensemble des opérations que l'on retrouve dans un programme compilé. Chaque instruction x86-64 se traduit en une ou plusieurs opérations sur des bitvectors, et le solveur sait les résoudre.

En revanche, le solveur ne sait **pas** raisonner efficacement sur les fonctions de hachage cryptographiques conçues pour être à sens unique (SHA-256, etc.), ni sur l'arithmétique en virgule flottante complexe (bien que Z3 ait un support partiel des flottants). Ce sont des cas où l'exécution symbolique atteint ses limites, ce que nous détaillerons dans la section 18.5.

---

## Récapitulatif visuel du processus

Pour fixer les idées, voici le flux complet de l'exécution symbolique appliquée au RE d'un binaire :

```
  ┌──────────────────┐
  │  Binaire ELF     │    Entrée : le fichier compilé par GCC
  │  (keygenme_O2)   │
  └────────┬─────────┘
           │
           ▼
  ┌───────────────────────┐
  │  Chargement (Loader)  │    Le moteur charge le binaire, résout les
  │  + Traduction en IR   │    imports, traduit le code machine en IR
  └────────┬──────────────┘
           │
           ▼
  ┌───────────────────────────┐
  │  Création de l'état       │    Les entrées (argv, stdin...) sont
  │  symbolique initial       │    remplacées par des variables symboliques
  └────────┬──────────────────┘
           │
           ▼
  ┌───────────────────────────┐
  │  Exploration de l'arbre   │    Le moteur exécute symboliquement,
  │  des chemins              │    bifurque à chaque branchement,
  │  (SimulationManager)      │    accumule les contraintes
  └────────┬──────────────────┘
           │
           ▼
  ┌───────────────────────────┐
  │  Chemin cible atteint ?   │──── Non ──→ Continuer l'exploration
  │  (adresse "find")         │             ou déclarer l'échec
  └────────┬──────────────────┘
           │ Oui
           ▼
  ┌───────────────────────────┐
  │  Résolution des           │    Le solveur SMT (Z3) cherche une
  │  contraintes de chemin    │    affectation des variables symboliques
  └────────┬──────────────────┘    qui satisfait TOUTES les contraintes
           │
           ▼
  ┌───────────────────────────┐
  │  Solution concrète        │    → Le serial valide, par exemple
  │  (valeurs des entrées)    │      "4F2A8B1D73E590C6"
  └───────────────────────────┘
```

---

## Points clés à retenir

- L'exécution symbolique remplace les entrées par des **variables mathématiques** et propage des **expressions** au lieu de calculer des valeurs.

- À chaque branchement conditionnel, l'exécution se **dédouble** : un chemin ajoute la condition comme contrainte, l'autre ajoute sa négation.

- Un **solveur SMT** (Z3) détermine s'il existe des valeurs concrètes satisfaisant l'ensemble des contraintes d'un chemin donné.

- En RE, on définit un objectif par une **adresse à atteindre** et des **adresses à éviter**, sans avoir besoin de comprendre le détail de la logique intermédiaire.

- L'exécution symbolique opère sur le **binaire** directement, pas sur le code source. Elle fonctionne donc sur des binaires strippés, optimisés et sans symboles.

- L'**explosion des chemins** est le principal ennemi de cette technique : chaque branchement double potentiellement le nombre d'états à explorer.

---

>  Dans la section suivante (18.2), nous passerons de la théorie à la pratique en installant **angr** et en explorant son architecture : `SimState`, `SimulationManager`, stratégies d'exploration et la bibliothèque `claripy` pour manipuler les bitvectors symboliques.

⏭️ [angr — installation et architecture (SimState, SimManager, exploration)](/18-execution-symbolique/02-angr-installation-architecture.md)
