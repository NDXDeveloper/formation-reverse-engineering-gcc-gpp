🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 15.5 — Coverage-guided fuzzing : lire les cartes de couverture (`afl-cov`, `lcov`)

> 🔗 **Prérequis** : Section 15.2 (AFL++, bitmap de couverture), Section 15.4 (analyse des crashs), Chapitre 8 (navigation dans Ghidra)

---

## La couverture comme outil de RE

Dans les sections précédentes, nous avons exploité les **crashs** produits par le fuzzer pour comprendre les chemins d'exécution qui échouent. Mais la majorité des inputs ne crashent pas — ils traversent le parseur, empruntent des branches, et se terminent normalement. Ces exécutions « silencieuses » sont tout aussi informatives que les crashs, à condition de pouvoir observer **quels chemins elles ont parcourus**.

C'est exactement ce que mesure la couverture de code. Pendant le fuzzing, chaque input déclenche l'exécution de certains blocs de base et de certaines transitions entre blocs. En accumulant ces observations sur l'ensemble de la campagne, on obtient une **carte** du binaire qui distingue :

- **Le code couvert** — les portions effectivement exécutées par au moins un input du corpus. Ce sont les chemins que le fuzzer a su atteindre.  
- **Le code non couvert** — les portions jamais exécutées. Soit du code mort (jamais atteignable), soit des chemins protégés par des conditions que le fuzzer n'a pas satisfaites.

Pour un reverse engineer, cette distinction est une boussole. Le code couvert est « compris » — on sait quels inputs le déclenchent, on peut le tracer dans GDB. Le code non couvert est la terra incognita qui mérite une attention manuelle : pourquoi le fuzzer n'y est-il pas arrivé ? Quelle condition bloque ? Est-ce un checksum, une signature, une dépendance temporelle ?

Cette section explique comment extraire, visualiser et interpréter les données de couverture produites par AFL++ et libFuzzer, en utilisant `afl-cov`, `lcov`, `genhtml` et `afl-showmap`.

---

## Les deux niveaux de couverture

Avant de plonger dans les outils, distinguons deux métriques de couverture qui n'apportent pas la même information :

### Couverture par lignes (line coverage)

La métrique la plus intuitive : quelles lignes du code source ont été exécutées ? C'est ce que produisent `gcov` et `lcov` à partir de l'instrumentation GCC (`-fprofile-arcs -ftest-coverage` ou `--coverage`). Le résultat est un rapport HTML où chaque ligne source est annotée en vert (exécutée) ou rouge (jamais exécutée), avec un compteur d'exécutions.

**Avantage** : lisible immédiatement, corrélation directe avec le code source.  
**Limite** : nécessite les sources. En RE pur sur un binaire sans sources, cette métrique n'est pas directement disponible (mais on peut l'approximer en superposant la couverture binaire au pseudo-code Ghidra).  

### Couverture par edges (edge coverage)

C'est la métrique native d'AFL++ et de libFuzzer. Un *edge* est une transition d'un bloc de base vers un autre — par exemple, le saut de la condition `if (version == 2)` vers le bloc qui traite la version 2. La bitmap d'AFL++ (cf. section 15.2) enregistre exactement ces edges.

**Avantage** : disponible même sans sources, plus fine que la couverture par lignes (deux chemins qui passent par les mêmes lignes mais dans un ordre différent génèrent des edges distincts).  
**Limite** : moins lisible brut — les edges sont des paires d'adresses, pas des lignes source.  

En pratique, on utilise **les deux** : la couverture par edges pendant le fuzzing (c'est ce qui guide les mutations d'AFL++), et la couverture par lignes après la campagne (pour visualiser le résultat dans un rapport HTML lisible).

---

## `afl-showmap` : la bitmap brute

L'outil le plus bas niveau pour examiner la couverture est `afl-showmap`. Il exécute le binaire instrumenté avec un input donné et affiche la bitmap résultante — la liste des edges traversés.

### Visualiser la couverture d'un seul input

```bash
$ afl-showmap -o /dev/stdout -- ./simple_parser_afl out/default/queue/id:000003,...
```

Sortie (extrait) :

```
000247:1
003891:1
007142:1
012088:3
018923:1
024510:1
031847:2
```

Chaque ligne est une paire `edge_id:hit_count`. L'identifiant de l'edge est un hash des deux blocs de base connectés (cf. la formule `hash(bloc_précédent XOR bloc_courant)` expliquée en section 15.2). Le compteur indique combien de fois cette transition a été empruntée pendant l'exécution.

### Comparer la couverture de deux inputs

```bash
$ afl-showmap -o map_seed1.txt -- ./simple_parser_afl out/default/queue/id:000000,...
$ afl-showmap -o map_seed3.txt -- ./simple_parser_afl out/default/queue/id:000003,...
$ diff map_seed1.txt map_seed3.txt
```

Le diff montre les edges présents dans un input mais pas dans l'autre. Si `id:000003` couvre des edges que `id:000000` ne couvre pas, cela signifie que la mutation qui a produit `id:000003` a ouvert un nouveau chemin — et on peut examiner les deux inputs avec `xxd` pour identifier quel octet modifié a provoqué ce changement de chemin.

### Couverture cumulée du corpus entier

Pour obtenir la couverture totale de la campagne (l'union de tous les edges atteints par tous les inputs) :

```bash
$ afl-showmap -C -i out/default/queue/ -o total_coverage.txt \
    -- ./simple_parser_afl @@
```

Le flag `-C` active le mode cumulatif : `afl-showmap` exécute chaque input du corpus et fusionne les bitmaps. Le fichier `total_coverage.txt` contient l'ensemble des edges atteints pendant toute la campagne, avec les compteurs cumulés.

Le nombre de lignes dans ce fichier est le nombre total d'edges couverts :

```bash
$ wc -l total_coverage.txt
47
```

Ce chiffre, comparé au nombre total d'edges du programme (estimable avec `afl-showmap` sur un corpus qui exerce tous les chemins, ou via le compteur d'instrumentation affiché à la compilation), donne un **pourcentage de couverture au niveau binaire**.

---

## `gcov` et `lcov` : couverture au niveau source

Pour obtenir un rapport de couverture lisible, ligne par ligne, sur le code source, on utilise la chaîne `gcov` → `lcov` → `genhtml`. Cette chaîne est indépendante d'AFL++ — elle repose sur l'instrumentation de couverture de GCC (`--coverage`).

### Principe

1. On compile le binaire avec `--coverage` (en plus de l'instrumentation AFL++ si on veut combiner les deux).  
2. On exécute le binaire avec chaque input du corpus produit par le fuzzer.  
3. GCC génère des fichiers `.gcda` contenant les compteurs d'exécution par ligne.  
4. `lcov` agrège ces compteurs en un fichier `.info`.  
5. `genhtml` transforme le `.info` en un rapport HTML navigable.

### Compilation avec couverture GCC

```bash
$ gcc --coverage -O0 -g -o simple_parser_gcov simple_parser.c
```

Le flag `--coverage` est un raccourci pour `-fprofile-arcs -ftest-coverage`. Il ajoute l'instrumentation de comptage des lignes et génère un fichier `.gcno` (notes de couverture statiques) à côté de chaque fichier objet.

> ⚠️ **Attention** — Ce binaire n'est **pas** instrumenté pour AFL++ (pas compilé avec `afl-gcc`). C'est un binaire séparé, dédié à la mesure de couverture. On l'utilise *après* la campagne de fuzzing pour rejouer les inputs découverts par AFL++ et mesurer quelle fraction du code source ils couvrent. Les deux compilations (AFL++ et gcov) coexistent sans problème.

> 💡 **Pourquoi `-O0` ?** — Les optimisations réordonnent et fusionnent les lignes de code, ce qui rend la couverture par lignes difficile à interpréter. Un rapport `lcov` sur un binaire `-O2` montrera des lignes « non couvertes » qui ont en réalité été inlinées ou éliminées par l'optimiseur. Pour une couverture lisible, compilez toujours en `-O0`.

### Rejouer le corpus sur le binaire gcov

```bash
# Réinitialiser les compteurs (important si on relance)
$ lcov --directory . --zerocounters

# Rejouer chaque input du corpus AFL++
$ for input in out/default/queue/id:*; do
    ./simple_parser_gcov "$input" 2>/dev/null
  done

# On peut aussi rejouer les crashs pour voir quelles lignes ils traversent
$ for crash in out/default/crashes/id:*; do
    ./simple_parser_gcov "$crash" 2>/dev/null
  done
```

Chaque exécution met à jour le fichier `.gcda` correspondant, accumulant les compteurs de couverture. Après avoir rejoué tout le corpus, les fichiers `.gcda` contiennent la couverture totale de la campagne.

### Générer le rapport avec lcov et genhtml

```bash
# Capturer les données de couverture
$ lcov --directory . --capture --output-file coverage.info

# (Optionnel) Filtrer les fichiers système et les headers
$ lcov --remove coverage.info '/usr/*' --output-file coverage_filtered.info

# Générer le rapport HTML
$ genhtml coverage_filtered.info --output-directory coverage_report/
```

Le rapport est maintenant consultable dans un navigateur :

```bash
$ firefox coverage_report/index.html
```

### Lire le rapport lcov

Le rapport HTML de `genhtml` se présente en trois niveaux de navigation :

**Vue d'ensemble (index).** Un tableau listant chaque fichier source avec ses pourcentages de couverture par lignes et par fonctions. Par exemple :

```
Filename                      Lines     Functions
─────────────────────────────────────────────────
simple_parser.c               78.3%     100.0%
```

78.3% des lignes ont été exécutées au moins une fois, et toutes les fonctions ont été atteintes (mais pas forcément intégralement).

**Vue par fichier.** En cliquant sur un fichier, on voit le code source avec un code couleur :

- **Vert** (avec compteur) — ligne exécutée. Le compteur indique combien de fois.  
- **Rouge** — ligne jamais exécutée.  
- **Blanc/gris** — ligne non exécutable (commentaires, accolades, déclarations).

**Vue par ligne.** Le compteur en marge gauche permet d'identifier les « lignes chaudes » (exécutées des milliers de fois, typiquement le corps d'une boucle de parsing) et les « lignes froides » (exécutées une seule fois ou jamais).

### Interpréter la couverture pour le RE

Les lignes rouges (non couvertes) sont les plus intéressantes. Chaque zone rouge est une question à poser :

**Zone rouge dans un `if` rarement vrai.** Le fuzzer n'a pas trouvé d'input qui satisfait cette condition. Regarder la condition dans Ghidra : est-ce un checksum ? Un magic number secondaire ? Un champ de taille avec une contrainte précise ? C'est un candidat pour une analyse manuelle avec Z3 ou angr (Chapitre 18).

**Zone rouge dans un gestionnaire d'erreur.** Le code de traitement des erreurs est souvent non couvert parce que le fuzzer produit des inputs qui soit passent les validations, soit échouent très tôt. Les gestionnaires d'erreurs intermédiaires (erreur de décodage au milieu d'un payload, par exemple) sont plus difficiles à atteindre.

**Zone rouge dans une branche de version inconnue.** Si le parseur supporte les versions 1, 2 et 3, mais que le fuzzer n'a produit que des inputs v1 et v2, le code v3 est rouge. Ajouter un seed `RE\x03...` au corpus et relancer le fuzzing.

**Grande zone verte avec un îlot rouge.** Un bloc de code couvert qui contient une ligne non couverte indique une micro-condition à l'intérieur d'un chemin globalement atteint. Le fuzzer passe par cette zone mais ne déclenche pas cette condition spécifique — souvent une vérification de valeur extrême ou un cas limite.

---

## `afl-cov` : automatiser le pipeline couverture

`afl-cov` est un outil dédié qui automatise le pipeline `gcov` → `lcov` → `genhtml` en l'intégrant directement avec la sortie d'AFL++. Il peut fonctionner en temps réel pendant le fuzzing ou en post-traitement.

### Installation

```bash
$ git clone https://github.com/mrash/afl-cov.git
$ cd afl-cov
# afl-cov est un script Python, pas de compilation nécessaire
$ ./afl-cov --help
```

Dépendances : `lcov`, `genhtml`, `gcov` (installés via `sudo apt install lcov`).

### Utilisation en post-traitement

Après une campagne AFL++ terminée :

```bash
$ ./afl-cov/afl-cov \
    -d out/default \
    -e "./simple_parser_gcov AFL_FILE" \
    -c . \
    --coverage-cmd "lcov --directory . --capture --output-file cov.info" \
    --genhtml-cmd "genhtml cov.info --output-directory cov_html" \
    --overwrite
```

Les options principales :

| Option | Rôle |  
|--------|------|  
| `-d out/default` | Répertoire de sortie AFL++ à analyser |  
| `-e "./simple_parser_gcov AFL_FILE"` | Commande d'exécution du binaire gcov ; `AFL_FILE` est remplacé par chaque input |  
| `-c .` | Répertoire contenant le code source (pour la corrélation lignes/couverture) |  
| `--overwrite` | Écraser les résultats précédents |

`afl-cov` rejoue automatiquement chaque input du corpus et chaque crash sur le binaire gcov, accumule la couverture, et produit le rapport HTML final.

### Utilisation en temps réel (mode live)

Pour surveiller la couverture pendant que le fuzzer tourne :

```bash
# Terminal 1 : lancer AFL++
$ afl-fuzz -i in -o out -- ./simple_parser_afl @@

# Terminal 2 : lancer afl-cov en mode live
$ ./afl-cov/afl-cov \
    -d out/default \
    -e "./simple_parser_gcov AFL_FILE" \
    -c . \
    --live
```

Le flag `--live` fait tourner `afl-cov` en boucle : il détecte les nouveaux inputs ajoutés par AFL++ au corpus et met à jour le rapport de couverture incrémentalement. On peut rafraîchir la page HTML dans le navigateur pour voir la couverture évoluer en temps réel.

### Sortie d'`afl-cov`

`afl-cov` produit plusieurs fichiers dans le répertoire de sortie :

```
out/default/cov/
├── web/                    ← Rapport HTML genhtml (navigable)
│   ├── index.html
│   └── ...
├── id-delta-cov/           ← Couverture incrémentale par input
│   ├── id:000003,...       ← Nouvelles lignes couvertes par cet input
│   └── ...
├── zero-cov/               ← Fonctions avec 0% de couverture
└── cov-final.info          ← Données lcov agrégées
```

Le répertoire `id-delta-cov/` est particulièrement utile : pour chaque input du corpus, il liste les lignes source **nouvellement couvertes** par cet input (par rapport aux inputs précédents). En le parcourant dans l'ordre chronologique, on observe comment le fuzzer a progressivement « déverrouillé » de nouvelles zones du code.

Le fichier `zero-cov/` liste les fonctions **jamais atteintes**. En contexte RE, c'est un guide direct : ces fonctions méritent une analyse manuelle — pourquoi le fuzzer ne les atteint-il pas ? Sont-elles appelées uniquement via un chemin que le fuzzer n'a pas su emprunter ?

---

## Couverture libFuzzer : `-print_coverage` et SanitizerCoverage

libFuzzer dispose de ses propres mécanismes de couverture, indépendants de `gcov`.

### Rapport de couverture intégré

```bash
$ ./fuzz_parse_input -print_coverage=1 -runs=100000 corpus_parse/
```

À la fin de l'exécution, libFuzzer affiche un résumé des fonctions et des edges couverts :

```
COVERAGE:
  COVERED_FUNC: parse_input         (7/12 edges)
  COVERED_FUNC: validate_header     (4/4 edges)
  UNCOVERED_FUNC: decode_payload_v3
```

Ce résumé est moins détaillé qu'un rapport `lcov`, mais il donne immédiatement les fonctions non couvertes — directement exploitable pour orienter l'analyse.

### Exporter la couverture avec SanitizerCoverage

Pour une couverture plus fine, on peut compiler avec les flags SanitizerCoverage de Clang et exporter les données en format brut :

```bash
$ clang -fsanitize=fuzzer,address -fsanitize-coverage=trace-pc-guard,pc-table \
    -g -O1 -o fuzz_parse_input fuzz_parse_input.c parse_input.c
```

Les données de couverture brutes (adresses PC couvertes) peuvent ensuite être converties en rapport `lcov` via des scripts comme `sancov` :

```bash
# Après le fuzzing, les fichiers .sancov sont générés
$ sancov -symbolize fuzz_parse_input *.sancov > coverage_symbolized.txt
```

En pratique, pour une visualisation complète au niveau source, la chaîne `gcov` + `lcov` + `genhtml` décrite plus haut reste plus ergonomique. La couverture SanitizerCoverage est surtout utile pour des analyses programmatiques (scripts de triage, intégration CI/CD).

---

## Superposer la couverture au désassemblage Ghidra

Quand les sources ne sont pas disponibles (ou quand on veut corréler la couverture avec le désassemblage plutôt qu'avec les sources), on peut superposer les données de couverture binaire au graphe de fonctions dans Ghidra.

### Exporter les adresses couvertes

À partir de la bitmap AFL++ ou de la sortie `afl-showmap`, on peut extraire les adresses des blocs de base couverts. L'approche la plus directe utilise `afl-showmap` avec le mode verbose :

```bash
$ AFL_DEBUG=1 afl-showmap -o /dev/null -- ./simple_parser_afl some_input.bin 2>&1 \
    | grep "edge" > edges_covered.txt
```

Alternativement, avec un build SanitizerCoverage, les adresses PC sont directement disponibles dans les fichiers `.sancov`.

### Script Ghidra pour coloriser la couverture

Un script Ghidra (Java ou Python) peut lire la liste des adresses couvertes et coloriser les blocs correspondants dans le listing ou le Function Graph. La logique de base :

```python
# Script Ghidra Python (simplifié)
# Charger la liste des adresses couvertes
covered = set()  
with open("/path/to/covered_addresses.txt") as f:  
    for line in f:
        addr = int(line.strip(), 16)
        covered.add(addr)

# Coloriser les blocs couverts en vert, les non-couverts en rouge
from ghidra.program.model.address import AddressSet  
from java.awt import Color  

listing = currentProgram.getListing()  
for func in currentProgram.getFunctionManager().getFunctions(True):  
    for block in func.getBody().getAddressRanges():
        start = block.getMinAddress().getOffset()
        if start in covered:
            setBackgroundColor(block.getMinAddress(), Color.GREEN)
        else:
            setBackgroundColor(block.getMinAddress(), Color.RED)
```

> 💡 **En pratique** — Des scripts Ghidra plus sophistiqués existent dans la communauté (par exemple `lighthouse` pour IDA/Binja, ou des adaptations pour Ghidra). L'important est le principe : la couverture du fuzzer devient une **surcouche visuelle** sur le désassemblage, transformant Ghidra en un outil de navigation orienté par les données dynamiques.

Le résultat visuel est saisissant : dans le Function Graph de Ghidra, les blocs verts sont ceux que le fuzzer a atteints, les blocs rouges sont les zones inexplorées. On identifie en un coup d'œil :

- Les branches du parseur que le fuzzer a su emprunter.  
- Les branches bloquées — souvent gardées par une condition précise (checksum, signature, valeur magique secondaire).  
- Le code mort — des blocs qui ne sont atteignables par aucun chemin depuis le point d'entrée.

---

## Interpréter les zones non couvertes : stratégies d'action

La couverture n'est pas un objectif en soi — c'est un **diagnostic**. Chaque zone non couverte est une question, et la réponse détermine l'action suivante.

### Condition bloquante identifiable

Si le bloc non couvert est gardé par une condition que l'on peut lire dans Ghidra (par exemple `if (checksum(data) == data[offset_X])`), les options sont :

- **Ajouter un seed valide** : si on comprend le calcul, fabriquer manuellement un input qui satisfait la condition et l'ajouter au corpus.  
- **Écrire un harness intelligent** : modifier le harness libFuzzer pour calculer le checksum automatiquement avant d'appeler la fonction cible (cf. section 15.3, harness avancés).  
- **Utiliser l'exécution symbolique** : alimenter angr ou Z3 avec la contrainte extraite et lui demander de générer un input satisfaisant (cf. Chapitre 18).

### Dépendance d'état externe

Si le bloc non couvert dépend d'un état que le fuzzer ne contrôle pas (horloge système, variable d'environnement, fichier de configuration externe), les options sont :

- **Stubber la dépendance** : dans le harness, remplacer l'appel à `time()` ou `getenv()` par une valeur fixe ou contrôlée par l'input.  
- **Utiliser `LD_PRELOAD`** : interposer une bibliothèque qui intercepte les appels système et retourne des valeurs déterministes (cf. Chapitre 22, section 22.4).

### Code véritablement mort

Si aucun chemin ne mène au bloc non couvert (pas de XREF dans Ghidra), c'est du code mort — résidu d'une version précédente, code conditionné à une plateforme différente, ou fonction de débogage désactivée. Le noter comme tel et passer à autre chose.

### Fonctions entièrement non couvertes

Si une fonction entière n'a jamais été atteinte, vérifier dans Ghidra d'où elle est appelée (XREF). Si elle est appelée uniquement depuis une autre fonction elle-même non couverte, remonter la chaîne jusqu'à trouver le point de blocage. Souvent, une seule condition bloquante en amont « ferme » l'accès à tout un sous-arbre de fonctions.

---

## Mesurer la progression : quand arrêter le fuzzing

La couverture permet de prendre une décision rationnelle sur la durée du fuzzing. Les indicateurs clés :

**La couverture stagne.** Si `afl-cov` en mode live ne montre plus de nouvelles lignes couvertes depuis 30 minutes (ou plusieurs heures sur un programme complexe), le fuzzer a vraisemblablement épuisé les chemins atteignables avec sa stratégie actuelle. Options : enrichir le dictionnaire (section 15.6), ajouter des seeds manuels basés sur les zones rouges, ou modifier le harness.

**Le pourcentage de couverture est satisfaisant.** Ce seuil dépend du contexte. Pour un parseur simple, 80-90% de couverture lignes est atteignable. Pour un programme complexe avec de nombreuses branches conditionnelles, 50-60% est déjà un bon résultat pour le fuzzer seul. Les 20-40% restants relèvent typiquement de l'analyse manuelle ou de l'exécution symbolique.

**Les zones non couvertes sont identifiées et comprises.** Si toutes les zones rouges ont été examinées dans Ghidra et classées (condition bloquante connue, code mort, dépendance externe), on sait exactement ce que le fuzzer ne peut pas atteindre et pourquoi. La campagne a rempli son rôle.

**Le ratio crashs / couverture nouvelle diminue.** Si les derniers pourcents de couverture ne produisent plus de crashs, le rendement marginal du fuzzing diminue. Cela ne signifie pas qu'il n'y a plus de bugs — juste que les bugs restants sont dans les zones non couvertes, qui nécessitent une autre approche.

---

## En résumé

La couverture de code est le pont entre le fuzzing automatisé et l'analyse manuelle :

- **`afl-showmap`** donne la bitmap brute des edges — utile pour comparer des inputs individuels et mesurer la couverture au niveau binaire.  
- **`gcov` + `lcov` + `genhtml`** produisent un rapport HTML ligne par ligne — le format le plus lisible pour identifier les zones couvertes et non couvertes dans le code source.  
- **`afl-cov`** automatise le pipeline et peut fonctionner en temps réel pendant le fuzzing.  
- **La superposition dans Ghidra** transforme les données de couverture en surcouche visuelle sur le désassemblage — indispensable quand les sources ne sont pas disponibles.  
- **L'interprétation des zones non couvertes** oriente directement les actions suivantes : seeds manuels, harness intelligent, exécution symbolique, ou classification en code mort.

La couverture nous dit *où* le fuzzer est allé et *où* il n'est pas allé. La section suivante aborde comment l'aider à aller plus loin, en optimisant le **corpus** et en lui fournissant des **dictionnaires** adaptés au format cible.

---


⏭️ [Corpus management et dictionnaires custom](/15-fuzzing/06-corpus-dictionnaires.md)
