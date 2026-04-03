🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 14.2 — Callgrind + KCachegrind — profiling et graphe d'appels

> 🎯 **Objectif de cette section** : Utiliser Callgrind pour produire un graphe d'appels complet et un profil d'exécution instruction par instruction d'un binaire inconnu, puis exploiter KCachegrind pour visualiser l'architecture fonctionnelle du programme — identification des hotspots, des boucles critiques et de la hiérarchie d'appels — le tout sans disposer du code source ni des symboles.

---

## Callgrind dans la boîte à outils RE

En section 14.1, nous avons vu comment Memcheck révèle la structure interne d'un programme à travers ses erreurs mémoire. Callgrind adopte une approche complémentaire : au lieu de chercher des erreurs, il **compte**. Il compte chaque instruction exécutée, chaque appel de fonction, chaque relation appelant → appelé, et produit un profil exhaustif de l'exécution.

En développement, Callgrind sert à optimiser les performances — on identifie les fonctions les plus coûteuses pour les réécrire. En reverse engineering, les mêmes données nous servent à un tout autre objectif : **cartographier l'architecture fonctionnelle d'un binaire inconnu**.

Concrètement, Callgrind nous donne :

- **Le graphe d'appels complet** — quelle fonction appelle quelle autre, combien de fois, et dans quel contexte. C'est l'équivalent dynamique des cross-references (XREF) de Ghidra, mais avec l'avantage de ne montrer que les chemins réellement exécutés pour un input donné.  
- **Le coût en instructions de chaque fonction** — une fonction qui exécute 50 000 instructions sur un total de 200 000 représente 25 % de l'exécution. Si c'est un programme de chiffrement, cette fonction est probablement la routine crypto principale.  
- **La localisation des boucles** — une fonction appelée une seule fois mais qui exécute des millions d'instructions contient forcément des boucles internes. La répartition du coût entre les lignes (ou plutôt les adresses) de cette fonction révèle la structure de ces boucles.  
- **Les bibliothèques impliquées** — Callgrind profile aussi les appels vers les bibliothèques partagées. On voit directement si le programme passe 80 % de son temps dans `libcrypto.so` (chiffrement OpenSSL), `libz.so` (compression zlib) ou dans son propre code.

---

## Fonctionnement de Callgrind

Comme Memcheck, Callgrind s'appuie sur le moteur d'instrumentation de Valgrind. Mais au lieu de maintenir une shadow memory, il insère des compteurs à chaque instruction et à chaque site d'appel (`call`/`ret`).

### Ce que Callgrind mesure

Callgrind mesure principalement les **événements d'exécution d'instructions** (Ir — *Instruction reads*). Par défaut, chaque instruction x86-64 exécutée incrémente un compteur. Le résultat est un comptage exact du nombre d'instructions exécutées par le programme, ventilé par :

- **Fonction** — le total d'instructions exécutées dans le corps de la fonction (coût *self*) et le total incluant les fonctions qu'elle appelle (coût *inclusive*).  
- **Ligne source / adresse** — si des symboles de débogage sont disponibles, le coût est ventilé par ligne source. Sinon, il est ventilé par adresse d'instruction — ce qui est parfaitement exploitable en RE.  
- **Arc d'appel** — pour chaque paire (appelant, appelé), le nombre d'appels et le coût total transféré.

Optionnellement, Callgrind peut aussi simuler le comportement du cache (L1, L2, LL) et du prédicteur de branchement, ce qui ajoute les événements de cache miss et de mauvaise prédiction. En RE, ces métriques supplémentaires sont rarement nécessaires — le comptage d'instructions suffit largement pour nos besoins.

### Coût d'exécution

Callgrind est **plus lourd que Memcheck** : le ralentissement est d'un facteur **20 à 100x** environ, contre 10–50x pour Memcheck. Le comptage instruction par instruction est intrinsèquement coûteux car il nécessite une instrumentation à grain plus fin. Pour un programme de chiffrement de quelques secondes, l'analyse prendra quelques minutes — c'est acceptable.

---

## Lancement d'une analyse Callgrind

### Commande de base

```bash
valgrind --tool=callgrind ./mon_binaire arg1 arg2
```

Callgrind produit un fichier de sortie nommé `callgrind.out.<pid>` dans le répertoire courant, où `<pid>` est le PID du processus analysé.

### Options recommandées pour le RE

```bash
valgrind \
    --tool=callgrind \
    --callgrind-out-file=callgrind_ch21.out \
    --collect-jumps=yes \
    --collect-systime=nsec \
    ./ch14-keygenme_O0 ABCD-1234-EFGH
```

Détaillons chaque option :

**`--callgrind-out-file=callgrind_ch21.out`** — Nomme explicitement le fichier de sortie. Sans cette option, le nom inclut le PID qui change à chaque exécution, ce qui complique les comparaisons entre runs.

**`--collect-jumps=yes`** — Active la collecte des sauts conditionnels et inconditionnels. Pour chaque branchement, Callgrind enregistre combien de fois il a été pris et combien de fois il n'a pas été pris. C'est une information précieuse en RE : un branchement pris 0 fois sur 1000 exécutions est probablement un chemin d'erreur ou un cas limite rarement atteint. Un branchement pris exactement 16 fois dans une boucle nous indique la taille d'un bloc traité (16 octets = bloc AES, par exemple).

**`--collect-systime=nsec`** — Mesure le temps passé dans les appels système en nanosecondes. Cela permet de distinguer le temps CPU réel du temps passé en attente d'I/O (lecture fichier, communication réseau). En RE, un programme qui passe 95 % de son temps dans `read()` et `write()` a un profil très différent d'un programme qui passe 95 % dans du calcul interne.

### Options supplémentaires utiles

**`--separate-callers=N`** — Par défaut, Callgrind agrège les coûts par fonction, quel que soit le chemin d'appel. Avec `--separate-callers=3`, il distingue les contextes d'appel sur 3 niveaux de profondeur. Si la fonction `process_block` est appelée à la fois par `encrypt` et `decrypt`, on verra deux entrées séparées avec leurs coûts respectifs. En RE, cela permet de comprendre dans quel contexte une fonction est utilisée.

```bash
valgrind --tool=callgrind --separate-callers=3 ./mon_binaire
```

**`--toggle-collect=<function>`** — Limite la collecte de profiling à une fonction spécifique et ses descendantes. Si on a déjà identifié qu'une fonction à l'adresse `0x401B00` est intéressante (par exemple via Memcheck), on peut concentrer l'analyse dessus :

```bash
valgrind --tool=callgrind \
    --collect-atstart=no \
    --toggle-collect=0x401B00 \
    ./mon_binaire
```

Avec `--collect-atstart=no`, la collecte est désactivée au démarrage. Elle ne s'active que lorsque l'exécution entre dans la fonction `0x401B00`, et se désactive quand elle en sort. Le profil résultant ne contient que l'activité de cette fonction et de tout ce qu'elle appelle.

> 💡 **Astuce RE** — `--toggle-collect` est extrêmement utile quand le programme fait beaucoup d'initialisation (chargement de bibliothèques, parsing de config) avant d'arriver à la partie intéressante. On isole la routine cible et on obtient un profil propre et lisible.

---

## Contrôle dynamique de la collecte avec `callgrind_control`

Callgrind offre un outil compagnon, `callgrind_control`, qui permet de contrôler la collecte **pendant l'exécution** du programme, sans l'arrêter.

### Commandes principales

```bash
# Lister les processus Callgrind en cours
callgrind_control -l

# Activer/désactiver la collecte
callgrind_control -i on      # activer l'instrumentation  
callgrind_control -i off     # désactiver l'instrumentation  

# Forcer l'écriture du profil (dump intermédiaire)
callgrind_control -d

# Remettre les compteurs à zéro
callgrind_control -z
```

### Scénario RE typique

Supposons qu'on analyse un binaire interactif qui attend un input utilisateur, puis effectue un traitement :

```bash
# Terminal 1 : lancer le programme sous Callgrind, instrumentation désactivée
valgrind --tool=callgrind --collect-atstart=no --callgrind-out-file=profile.out ./ch21-keygenme_O0
```

```bash
# Terminal 2 : quand le programme attend l'input
callgrind_control -i on         # activer la collecte juste avant de saisir l'input
```

On saisit alors l'input dans le terminal 1 (par exemple la clé `ABCD-1234-EFGH`). Le programme traite l'input.

```bash
# Terminal 2 : après le traitement
callgrind_control -d             # dumper le profil  
callgrind_control -i off         # désactiver la collecte  
```

Le fichier `profile.out` ne contient maintenant que le profil de la phase de vérification de la clé, sans le bruit de l'initialisation ni de l'affichage. C'est un profil chirurgical de la routine qui nous intéresse.

---

## Lecture du fichier Callgrind en ligne de commande

Avant de passer à KCachegrind (interface graphique), voyons comment exploiter le fichier Callgrind en ligne de commande. C'est utile sur un serveur distant sans environnement graphique, ou pour des analyses scriptées.

### `callgrind_annotate` — le lecteur de base

```bash
callgrind_annotate callgrind_ch21.out
```

Cet outil produit un rapport textuel trié par coût décroissant :

```
--------------------------------------------------------------------------------
Profile data file 'callgrind_ch21.out' (creator: callgrind-3.22.0)
--------------------------------------------------------------------------------
I refs:      1,247,893

--------------------------------------------------------------------------------
         Ir
--------------------------------------------------------------------------------
    487,231  ???:0x00401080 [/path/to/ch21-keygenme_O0]    ← 39% du total
    312,456  ???:0x004010E0 [/path/to/ch21-keygenme_O0]    ← 25% du total
    198,712  /build/glibc/.../strcmp.S:strcmp [/usr/lib/libc.so.6]
     87,334  ???:0x00401150 [/path/to/ch21-keygenme_O0]
     52,891  /build/glibc/.../printf.c:printf [/usr/lib/libc.so.6]
    ...
```

Ce que ce rapport nous dit immédiatement :

- Le programme a exécuté **1 247 893 instructions** au total pour cet input.  
- La fonction à l'adresse `0x00401080` consomme **39 %** de l'exécution (487 231 instructions). C'est le hotspot principal — probablement la routine de transformation/hashing de la clé.  
- La fonction à `0x004010E0` consomme **25 %** — potentiellement la boucle de vérification ou une routine de dérivation.  
- `strcmp` de la libc est appelée et consomme **16 %** — c'est la comparaison finale entre la clé dérivée et la valeur attendue.  
- `printf` est présent mais ne consomme que **4 %** — affichage du résultat (succès/échec).

> 💡 **Astuce RE** — La présence de `strcmp` dans le profil d'un crackme est un signal fort : la vérification se fait par comparaison de chaînes. L'adresse de l'appelant de `strcmp` (visible dans le graphe d'appels) est la fonction de vérification principale. Ce seul indice peut suffire à localiser le point de patching.

### Annotation par adresse

Pour un binaire sans symboles, on peut demander l'annotation au niveau des adresses individuelles :

```bash
callgrind_annotate --auto=yes --inclusive=yes callgrind_ch21.out
```

L'option `--inclusive=yes` affiche le coût inclusif (la fonction + tout ce qu'elle appelle) en plus du coût self. La différence entre les deux est révélatrice :

- Si le coût self ≈ coût inclusif → la fonction fait son travail elle-même, elle ne délègue pas. C'est typique d'une boucle de calcul (hashing, chiffrement).  
- Si le coût self << coût inclusif → la fonction est un dispatcher ou un orchestrateur. Elle appelle d'autres fonctions qui font le vrai travail. C'est typique d'un `main()` ou d'une fonction de contrôle de flux.

### Filtrer par objet (binaire vs bibliothèques)

```bash
callgrind_annotate --include=./ch21-keygenme_O0 callgrind_ch21.out
```

Cette commande limite l'affichage aux fonctions du binaire cible, en excluant la libc, libstdc++ et les autres bibliothèques. On obtient un profil concentré sur le code de l'application.

---

## Visualisation avec KCachegrind

KCachegrind est l'outil de visualisation graphique pour les fichiers Callgrind. C'est ici que l'analyse prend toute sa dimension pour le RE : le graphe d'appels devient visuel, navigable, et les hotspots sautent aux yeux.

### Installation

```bash
# Debian / Ubuntu
sudo apt install kcachegrind

# Alternative Qt (sans dépendances KDE)
sudo apt install qcachegrind
```

`qcachegrind` est une version utilisant uniquement Qt, sans les dépendances du bureau KDE. Fonctionnellement identique pour nos besoins.

### Ouverture d'un fichier

```bash
kcachegrind callgrind_ch21.out
```

L'interface se divise en plusieurs panneaux. Voyons les plus utiles en contexte de RE.

### Le panneau Flat Profile (liste des fonctions)

Le panneau de gauche liste toutes les fonctions triées par coût. Pour un binaire strippé, les noms apparaissent sous la forme `0x00401080` — les adresses brutes. Deux colonnes principales :

- **Self** — le coût propre de la fonction (instructions exécutées dans son corps uniquement).  
- **Incl.** — le coût inclusif (instructions dans son corps + dans toutes les fonctions qu'elle appelle).

En cliquant sur une fonction, les autres panneaux se mettent à jour pour afficher ses détails.

> 💡 **Astuce RE** — Dans KCachegrind, faites un clic droit sur une fonction et sélectionnez « Rename Function ». Vous pouvez la renommer en quelque chose de lisible (`check_key`, `derive_hash`, `main`). Ces renommages sont conservés dans la session et rendent le graphe d'appels immédiatement compréhensible. C'est le même réflexe que le renommage dans Ghidra, mais appliqué au profil dynamique.

### Le graphe d'appels (Call Graph)

C'est le panneau le plus précieux pour le RE. Accessible via l'onglet **Call Graph** dans le panneau de droite, il affiche une représentation graphique des relations appelant → appelé.

Chaque nœud est une fonction, avec :

- Son nom (ou adresse).  
- Son coût inclusif en pourcentage.  
- La couleur du nœud reflète son coût relatif (rouge = chaud, bleu = froid).

Chaque arc (flèche) entre deux nœuds indique :

- La direction de l'appel (de l'appelant vers l'appelé).  
- Le nombre d'appels.  
- Le coût transféré.

Pour un crackme typique, le graphe d'appels ressemblera à quelque chose comme :

```
[0x4012E8]  ──(1x)──►  [0x401080]  ──(256x)──►  [0x4011A0]
  main?                  check_key?               transform_char?
  Incl: 100%             Incl: 64%                Self: 39%
     │
     └──(1x)──►  [0x4010E0]  ──(1x)──►  [strcmp@plt]
                  compare?                 Self: 16%
                  Incl: 41%
```

Ce graphe nous révèle immédiatement la structure du programme :

- `0x4012E8` est l'orchestrateur (main ou équivalent) — coût self faible, coût inclusif 100 %.  
- `0x401080` est appelé une seule fois et appelle `0x4011A0` exactement **256 fois** — c'est une boucle qui traite chaque caractère ou chaque octet d'un bloc. Le nombre 256 est caractéristique d'une table de substitution (S-box) ou d'un traitement caractère par caractère.  
- `0x4011A0` est le hotspot computationnel — elle fait le vrai calcul (transformation, hashing).  
- `0x4010E0` appelle `strcmp` — c'est la comparaison finale.

> 💡 **Astuce RE** — Les **nombres d'appels** sur les arcs du graphe sont des indices structurels majeurs. 256 appels = probablement une itération sur 256 valeurs (table, charset). 16 appels = potentiellement 16 rounds (AES). 64 appels = itération sur des blocs de 64 octets (SHA-256). Ces nombres ne mentent pas — ils viennent du comptage exact de l'exécution.

### Le panneau Callers / Callees

En sélectionnant une fonction dans la liste, les onglets **Callers** et **Callees** affichent respectivement qui l'appelle et qui elle appelle, avec les coûts associés. C'est l'équivalent dynamique des XREF de Ghidra.

La différence fondamentale : les XREF de Ghidra montrent tous les appels **possibles** dans le code, tandis que Callgrind montre les appels **réellement effectués** pour un input donné. Un appel de fonction qui existe dans le code mais qui se trouve derrière un `if` jamais pris n'apparaîtra pas dans le profil Callgrind.

Cette propriété est à double tranchant :

- **Avantage** : le graphe est plus simple et plus lisible, car il ne contient que les chemins exercés.  
- **Inconvénient** : un chemin de code important (gestion d'erreur, branche alternative) peut être invisible si l'input choisi ne le déclenche pas.

C'est pourquoi on lance souvent Callgrind **plusieurs fois** avec des inputs différents : un input valide, un input invalide, un input vide, un input très long. La comparaison des profils révèle les branches conditionnelles du programme.

### Le panneau Source / Assembly

Si le binaire contient des symboles de débogage (compilé avec `-g`), KCachegrind affiche le code source annoté avec les compteurs instruction par instruction. Pour un binaire strippé, il affiche le désassemblage annoté — chaque instruction avec son compteur d'exécution.

C'est une vue extrêmement puissante en RE : on voit non seulement le désassemblage, mais aussi **combien de fois chaque instruction a été exécutée**. Une instruction `jnz` exécutée 255 fois avec le saut pris, et 1 fois sans le saut, nous dit que la boucle fait 256 itérations et que la condition de sortie est atteinte à la 256ᵉ.

---

## Méthodologie d'analyse RE avec Callgrind

Voici une méthode structurée pour exploiter Callgrind dans un workflow de reverse engineering.

### Étape 1 — Profil avec un input « normal »

On lance une première exécution avec un input typique :

```bash
valgrind --tool=callgrind \
    --callgrind-out-file=profile_normal.out \
    --collect-jumps=yes \
    ./ch21-keygenme_O0 ABCD-1234-EFGH
```

On ouvre le résultat dans KCachegrind et on note :

- Le graphe d'appels global.  
- Les 5 fonctions les plus coûteuses (adresses + pourcentages).  
- Les nombres d'appels sur les arcs significatifs.

### Étape 2 — Profil avec un input « différent »

On relance avec un input différent pour observer les variations :

```bash
valgrind --tool=callgrind \
    --callgrind-out-file=profile_alt.out \
    --collect-jumps=yes \
    ./ch21-keygenme_O0 XXXX-0000-YYYY
```

On compare les deux profils. Les questions à se poser :

- **Le graphe d'appels a-t-il la même forme ?** Si oui, le programme suit le même chemin quelle que soit la clé → la validation est probablement séquentielle. Si non, il y a des branches conditionnelles dépendant de l'input.  
- **Les nombres d'appels ont-ils changé ?** Si `0x4011A0` est appelée 256 fois dans les deux cas, le nombre d'itérations est fixe. S'il change, il dépend de la longueur ou du contenu de l'input.  
- **Le coût total a-t-il changé ?** Un coût total quasi-identique pour deux inputs signifie que le programme fait toujours le même travail (pas de short-circuit sur les premiers caractères).

### Étape 3 — Profil avec un input « limite »

On teste un cas extrême — input vide, input très long, caractères spéciaux :

```bash
valgrind --tool=callgrind \
    --callgrind-out-file=profile_empty.out \
    --collect-jumps=yes \
    ./ch21-keygenme_O0 ""
```

Ce profil révèle le chemin de gestion d'erreur : quelles fonctions sont appelées quand l'input est invalide d'emblée, et quelles fonctions sont *absentes* par rapport au profil normal. Les fonctions absentes sont celles qui traitent réellement l'input — elles n'ont pas été atteintes car le programme a rejeté l'input en amont.

### Étape 4 — Comparaison dans KCachegrind

KCachegrind permet de charger plusieurs profils simultanément via le menu **File → Add**. L'interface affiche alors les coûts de chaque profil côte à côte pour chaque fonction, ce qui facilite la comparaison visuelle.

On peut aussi comparer en ligne de commande :

```bash
callgrind_annotate profile_normal.out > annotated_normal.txt  
callgrind_annotate profile_alt.out > annotated_alt.txt  
diff annotated_normal.txt annotated_alt.txt  
```

### Étape 5 — Report vers Ghidra

Les adresses identifiées comme intéressantes dans Callgrind sont directement utilisables dans Ghidra. On établit une correspondance :

| Adresse Callgrind | Coût | Rôle hypothétique | Nom Ghidra proposé |  
|---|---|---|---|  
| `0x4012E8` | Incl: 100 %, Self: 2 % | Point d'entrée / main | `main` |  
| `0x401080` | Incl: 64 %, Self: 25 % | Routine de hashing | `hash_key` |  
| `0x4011A0` | Self: 39 % | Transformation unitaire | `transform_byte` |  
| `0x4010E0` | Incl: 41 %, Self: 25 % | Comparaison | `verify_result` |

On ouvre Ghidra, on navigue à chaque adresse (`G` → adresse), et on renomme les fonctions avec les noms hypothétiques. L'analyse statique qui suit est considérablement accélérée : au lieu de partir d'un désassemblage anonyme, on a déjà une carte fonctionnelle annotée.

---

## Cas d'analyse : identifier une routine crypto par son profil

Les routines cryptographiques ont des **signatures de profiling** caractéristiques que Callgrind révèle sans ambiguïté. Voici les patterns les plus courants.

### AES (Advanced Encryption Standard)

- Une fonction appelée exactement **10, 12 ou 14 fois** dans une boucle → les rounds AES (10 pour AES-128, 12 pour AES-192, 14 pour AES-256).  
- À l'intérieur de chaque round, des sous-fonctions appelées **16 fois** (16 octets = taille du bloc AES) ou **4 fois** (4 colonnes dans le state).  
- Un hotspot contenant des accès mémoire intensifs à une table de 256 entrées → la S-box de substitution.

### SHA-256

- Une fonction de compression appelée **N fois** où N dépend de la taille de l'input (un appel par bloc de 64 octets).  
- À l'intérieur de chaque appel, une boucle exécutée exactement **64 fois** → les 64 rounds de SHA-256.  
- Un coût dominé par des opérations arithmétiques (rotations, XOR, additions) et des accès à une table de 64 constantes.

### RC4

- Une phase d'initialisation avec une boucle de **256 itérations** exactes → le Key Scheduling Algorithm (KSA).  
- Une phase de chiffrement avec une boucle de **N itérations** (N = taille du plaintext) → le Pseudo-Random Generation Algorithm (PRGA).  
- Très peu d'appels de sous-fonctions — RC4 est un algorithme compact qui tient dans une seule fonction.

### bcrypt / PBKDF2

- Une fonction de hashing appelée un **très grand nombre de fois** (milliers à centaines de milliers) → le facteur de coût / nombre d'itérations.  
- Un coût total disproportionné par rapport à la taille de l'input → signe d'une dérivation de clé volontairement lente.

> 💡 **Astuce RE** — Quand vous voyez une boucle avec un nombre d'itérations fixe et « rond » (10, 16, 64, 256, 1024, 4096...), notez-le. Ces nombres sont rarement arbitraires : ils correspondent presque toujours à des constantes d'un algorithme connu. Croisez-les avec l'Annexe J (constantes magiques crypto) pour identifier l'algorithme.

---

## Callgrind sur un binaire strippé et optimisé

Jusqu'ici, nos exemples utilisaient des binaires `-O0` pour la lisibilité. En pratique, les binaires rencontrés en RE sont souvent compilés avec `-O2` ou `-O3`, et strippés. Callgrind fonctionne toujours, mais l'interprétation est différente.

### L'impact de l'inlining

Avec `-O2`, GCC peut inliner les petites fonctions. Une fonction `transform_byte` appelée 256 fois en `-O0` sera potentiellement intégrée dans le corps de `hash_key` en `-O2`. Conséquence dans Callgrind :

- En `-O0` : on voit deux fonctions distinctes, avec un arc de 256 appels.  
- En `-O2` : on ne voit qu'une seule fonction, avec un coût self beaucoup plus élevé. Les 256 itérations sont toujours là (visibles dans le compteur de la boucle), mais l'arc d'appel a disparu.

Le graphe d'appels est donc **plus plat** avec les optimisations. Moins de nœuds, mais chaque nœud est plus gros. C'est un inconvénient pour la compréhension de la hiérarchie fonctionnelle, mais un avantage pour identifier les hotspots computationnels : tout est concentré dans peu de fonctions.

### Le tail call optimization

Avec `-O2`, GCC remplace parfois un `call` + `ret` par un simple `jmp` (tail call optimization, cf. chapitre 16). Callgrind ne voit pas de `call` dans ce cas et ne crée pas d'arc d'appel. La fonction appelée apparaît comme faisant partie de l'appelant.

Pour détecter cette situation, on compare le graphe Callgrind avec les XREF de Ghidra : si Ghidra montre un appel (`call`) que Callgrind ne voit pas, c'est probablement un inlining ou un tail call.

### La stratégie multi-optimisation

Quand c'est possible (binaires d'entraînement de cette formation, CTF où les sources sont reconstruites), la stratégie la plus efficace est de **profiler les deux versions** :

```bash
# Version -O0 : graphe d'appels détaillé
valgrind --tool=callgrind --callgrind-out-file=profile_O0.out ./keygenme_O0 ABCD

# Version -O2 : profil réaliste
valgrind --tool=callgrind --callgrind-out-file=profile_O2.out ./keygenme_O2 ABCD
```

On utilise le profil `-O0` pour comprendre la structure fonctionnelle (graphe d'appels riche), puis le profil `-O2` pour identifier les hotspots dans le binaire tel qu'il est réellement distribué. Les adresses `-O0` ne correspondent pas directement aux adresses `-O2`, mais les patterns de coût (proportions, nombres d'itérations) sont transposables.

---

## Format du fichier Callgrind et exploitation scriptée

Le fichier produit par Callgrind est un fichier texte avec un format documenté, exploitable par des scripts Python.

### Structure du format

```
# callgrind format
version: 1  
creator: callgrind-3.22.0  
pid: 12345  
cmd: ./ch21-keygenme_O0 ABCD-1234-EFGH  

positions: line  
events: Ir  
summary: 1247893  

ob=./ch21-keygenme_O0  
fl=(1) ???  
fn=(1) 0x00401080  

0x00401080 3
0x00401084 256
0x00401088 256
0x0040108c 512
...

cfn=(2) 0x004011A0  
calls=256 0x004011A0  
0x00401094 256
```

Les champs clés :

- **`ob=`** — l'objet (binaire ou bibliothèque).  
- **`fn=`** — la fonction en cours.  
- **`0x... N`** — l'adresse de l'instruction et son nombre d'exécutions.  
- **`cfn=`** — la fonction appelée (callee function).  
- **`calls=N`** — le nombre d'appels vers cette fonction.

### Script Python d'extraction

Voici un exemple de script qui extrait les fonctions triées par coût depuis un fichier Callgrind :

```python
#!/usr/bin/env python3
"""Extracteur de fonctions depuis un fichier Callgrind."""

import re  
import sys  
from collections import defaultdict  

def parse_callgrind(filepath):
    functions = defaultdict(int)
    current_fn = None

    with open(filepath) as f:
        for line in f:
            line = line.strip()
            # Nouvelle fonction
            m = re.match(r'fn=\(\d+\)\s+(.*)', line)
            if m:
                current_fn = m.group(1)
                continue
            # Ligne de coût : adresse + compteur
            m = re.match(r'(0x[0-9a-fA-F]+|\d+)\s+(\d+)', line)
            if m and current_fn:
                functions[current_fn] += int(m.group(2))

    return functions

if __name__ == '__main__':
    funcs = parse_callgrind(sys.argv[1])
    total = sum(funcs.values())

    print(f"{'Fonction':<30} {'Coût':>12} {'%':>8}")
    print("-" * 52)
    for fn, cost in sorted(funcs.items(), key=lambda x: -x[1])[:20]:
        pct = (cost / total) * 100 if total > 0 else 0
        print(f"{fn:<30} {cost:>12,} {pct:>7.1f}%")
```

Ce script produit un tableau trié des 20 fonctions les plus coûteuses, avec leurs adresses et pourcentages. On peut le connecter à d'autres outils (Ghidra headless, r2pipe) pour automatiser le renommage des fonctions.

---

## Limites de Callgrind en contexte de RE

**Couverture dépendante de l'input.** Comme tout outil d'analyse dynamique, Callgrind ne voit que les chemins exercés par l'input fourni. Une fonction jamais appelée est invisible. C'est pourquoi la stratégie multi-input (étapes 1–3 de la méthodologie) est importante.

**Pas de distinction entre données.** Callgrind compte les instructions, pas les données. Il ne sait pas si une boucle de 256 itérations itère sur une clé, un message ou une table de substitution. Pour cette information, il faut croiser avec Memcheck (section 14.1) ou GDB (chapitre 11).

**Pas de mesure du temps réel.** Le comptage d'instructions est déterministe (même résultat à chaque exécution), ce qui est un avantage pour la reproductibilité. Mais une instruction `div` coûte beaucoup plus de cycles CPU qu'un `mov`, et Callgrind les compte de la même manière. Le profil reflète la complexité algorithmique, pas le temps réel.

**Programmes multi-threadés.** Callgrind gère les threads mais les sérialise — un seul thread s'exécute à la fois. Le profil est valide en termes de comptage d'instructions, mais les problèmes de concurrence et les performances réelles des programmes parallèles ne sont pas reflétés.

---

## Résumé : ce que Callgrind + KCachegrind nous apprennent en RE

| Information Callgrind | Utilité RE |  
|---|---|  
| Graphe d'appels complet | Architecture fonctionnelle du programme |  
| Coût self vs inclusif | Distinction code de calcul vs orchestrateur |  
| Nombres d'appels sur les arcs | Nombre d'itérations → identification d'algorithmes |  
| Hotspots (fonctions les plus coûteuses) | Localisation des routines crypto / parsing |  
| Compteurs de sauts conditionnels | Conditions de boucle, branches prises/non prises |  
| Comparaison multi-inputs | Branches dépendant de l'input, chemins d'erreur |  
| Répartition binaire vs bibliothèques | Part de code propre vs code tiers |

Callgrind et KCachegrind sont les outils de **cartographie fonctionnelle** du RE dynamique. Là où Memcheck nous donne le « quoi » (quels buffers, quelles tailles), Callgrind nous donne le « comment » (quelles fonctions, dans quel ordre, combien de fois). Combinés, ils fournissent une vision structurelle du programme qui rivalise avec l'analyse statique — et qui la complète en ne montrant que les chemins réellement exécutés.

---


⏭️ [AddressSanitizer (ASan), UBSan, MSan — compiler avec `-fsanitize`](/14-valgrind-sanitizers/03-asan-ubsan-msan.md)
