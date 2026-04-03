🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 18 — Exécution symbolique et solveurs de contraintes

> **Partie IV — Techniques Avancées de RE**

---

## Pourquoi ce chapitre ?

Dans les chapitres précédents, vous avez appris à lire du désassemblage, à naviguer dans un décompileur, à poser des breakpoints et à tracer l'exécution d'un binaire instruction par instruction. Ces approches — statique et dynamique — reposent sur un dénominateur commun : **c'est vous qui raisonnez** sur les chemins d'exécution possibles, les contraintes sur les entrées et les conditions de branchement. Face à un crackme avec une routine de vérification de 15 lignes, c'est tout à fait faisable à la main. Face à une cascade de transformations arithmétiques imbriquées sur 200 lignes d'assembleur optimisé, ça ne l'est plus.

L'exécution symbolique renverse le problème : au lieu de fournir une entrée concrète et d'observer ce qui se passe, on laisse les entrées **indéterminées** — on les traite comme des variables mathématiques — et on demande à un moteur d'exploration de parcourir les chemins du programme en accumulant des **contraintes** sur ces variables. À la fin, un solveur de contraintes (typiquement un SMT solver comme Z3) répond à la question : *« Existe-t-il une valeur d'entrée qui satisfait toutes ces contraintes et qui mène à tel point du programme ? »*

En d'autres termes, l'exécution symbolique transforme un problème de reverse engineering en un **problème de satisfaction de contraintes**, et délègue la résolution à une machine.

---

## Ce que vous allez apprendre

Ce chapitre couvre les fondements théoriques et la mise en pratique de l'exécution symbolique appliquée au reverse engineering de binaires compilés avec GCC. Vous y découvrirez :

- Les **principes fondamentaux** de l'exécution symbolique : variables symboliques, états symboliques, exploration d'arbres de chemins et collecte de contraintes le long de chaque branche.

- **angr**, le framework d'analyse binaire en Python le plus utilisé dans la communauté RE et CTF. Vous apprendrez son architecture interne (`SimState`, `SimulationManager`, stratégies d'exploration) et comment l'utiliser pour résoudre automatiquement un crackme compilé avec GCC — du binaire brut à la solution, sans jamais lire une ligne d'assembleur.

- **Z3**, le solveur SMT développé par Microsoft Research, utilisé comme backend par angr mais également exploitable de façon autonome. Vous verrez comment modéliser manuellement des contraintes extraites lors d'une analyse statique et demander à Z3 de les résoudre.

- Les **limites fondamentales** de l'approche : explosion combinatoire des chemins, difficulté à modéliser les boucles non bornées, les appels système et les interactions avec l'environnement.

- Quand et comment **combiner** l'exécution symbolique avec le reverse engineering manuel pour tirer le meilleur des deux mondes.

---

## Positionnement dans la formation

Ce chapitre se situe à la croisée de l'analyse statique et de l'analyse dynamique. Il suppose que vous êtes à l'aise avec :

- La **lecture de désassemblage x86-64** et la compréhension des branchements conditionnels (chapitres 3 et 7).  
- L'utilisation d'un **décompileur** comme Ghidra pour comprendre la logique d'un binaire (chapitre 8).  
- Les bases du **débogage avec GDB** pour valider des hypothèses sur un binaire en cours d'exécution (chapitre 11).  
- Les notions de **protections binaires** (stripping, PIE, ASLR) qui influencent la configuration d'angr (chapitre 19, mais les bases vues au chapitre 5 avec `checksec` suffisent ici).

Les techniques apprises dans ce chapitre seront directement mobilisées dans les cas pratiques de la Partie V, notamment au **chapitre 21** (résolution automatique du keygenme avec angr) et au **chapitre 24** (extraction de contraintes sur des routines cryptographiques).

---

## Outils utilisés dans ce chapitre

| Outil | Rôle | Installation |  
|-------|------|--------------|  
| **angr** | Framework d'exécution symbolique sur binaires | `pip install angr` |  
| **Z3** (z3-solver) | Solveur SMT autonome, également backend d'angr | `pip install z3-solver` |  
| **Python 3.10+** | Langage de scripting pour angr et Z3 | Pré-installé sur la VM du chapitre 4 |  
| **Ghidra** | Décompileur pour l'extraction manuelle de contraintes | Installé au chapitre 8 |  
| **GDB + GEF** | Validation dynamique des solutions trouvées | Installé aux chapitres 11–12 |

> 💡 **Note sur les versions :** angr évolue rapidement. Ce chapitre a été rédigé et testé avec angr **9.2.x**. Si vous utilisez une version plus récente, consultez la [documentation officielle](https://docs.angr.io/) en cas de changement d'API. L'installation dans un **virtualenv dédié** est fortement recommandée, car angr embarque de nombreuses dépendances qui peuvent entrer en conflit avec d'autres paquets Python.

---

## Binaires d'entraînement

Les binaires utilisés dans ce chapitre se trouvent dans le répertoire `binaries/` du dépôt. Vous travaillerez principalement avec les variantes du keygenme :

```
binaries/ch18-keygenme/
├── keygenme.c          ← Source (ne pas regarder avant d'avoir essayé !)
├── Makefile
├── keygenme_O0         ← -O0, avec symboles  (apprentissage)
├── keygenme_O0_strip   ← -O0, sans symboles  (RE sans symboles)
├── keygenme_O2         ← -O2, avec symboles  (optimisations)
├── keygenme_O2_strip   ← -O2, sans symboles  (checkpoint ch.18)
├── keygenme_O3         ← -O3, avec symboles  (vectorisation)
└── keygenme_O3_strip   ← -O3, sans symboles  (défi avancé)
```

Les 6 variantes sont produites par `make all`. La progression au sein du chapitre suit une difficulté croissante : on commence par résoudre `keygenme_O0` (le plus lisible), puis on s'attaque aux versions optimisées et strippées pour voir comment angr gère — ou non — la complexité supplémentaire.

---

## Plan du chapitre

- **18.1** — Principes de l'exécution symbolique : traiter les inputs comme des symboles  
- **18.2** — angr — installation et architecture (SimState, SimManager, exploration)  
- **18.3** — Résoudre un crackme automatiquement avec angr  
- **18.4** — Z3 Theorem Prover — modéliser des contraintes extraites manuellement  
- **18.5** — Limites : explosion de chemins, boucles, appels système  
- **18.6** — Combinaison avec le RE manuel : quand utiliser l'exécution symbolique  
- **🎯 Checkpoint** — Résoudre `keygenme_O2_strip` avec angr en moins de 30 lignes Python

---

> **Prêt ?** Commençons par comprendre ce que signifie concrètement « traiter une entrée comme un symbole » dans la section 18.1.

⏭️ [Principes de l'exécution symbolique : traiter les inputs comme des symboles](/18-execution-symbolique/01-principes-execution-symbolique.md)
