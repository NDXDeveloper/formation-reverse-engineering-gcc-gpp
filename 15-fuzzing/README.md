🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 15 — Fuzzing pour le Reverse Engineering

> 📦 **Binaires utilisés dans ce chapitre** : `binaries/ch15-keygenme/` (premier run AFL++, sections 15.2–15.4), `binaries/ch15-fileformat/` (cas pratique complet, section 15.7)  
> 🛠️ **Outils principaux** : AFL++, libFuzzer, `afl-cov`, `lcov`, GCC avec `-fsanitize`, `afl-tmin`, `afl-cmin`  
> ⏱️ **Durée estimée** : 4 à 5 heures (lecture + expérimentation)

---

## Pourquoi ce chapitre ?

Dans les chapitres précédents, nous avons analysé des binaires en les lisant (analyse statique) ou en les exécutant pas à pas (analyse dynamique avec GDB, Frida, Valgrind). Ces deux approches reposent sur une même prémisse : **c'est le reverse engineer qui décide quoi regarder et quoi tester**. Cela fonctionne bien quand on a une hypothèse précise — tracer un appel à `strcmp`, poser un breakpoint sur une routine de vérification, inspecter un buffer en mémoire.

Mais que faire quand on ne sait pas encore *où* regarder ? Quand le binaire expose un parseur complexe avec des dizaines de branches conditionnelles, et qu'on ignore quels inputs déclenchent quels chemins ? C'est exactement le problème que le **fuzzing** résout.

Le fuzzing consiste à bombarder un programme avec des entrées générées automatiquement — souvent aléatoires ou semi-aléatoires — et à observer ce qui se passe. Un crash ? On vient de découvrir un chemin d'exécution intéressant. Une nouvelle branche couverte ? Le fuzzer adapte ses mutations pour l'explorer davantage. En quelques heures de fuzzing, on peut cartographier des portions entières de la logique interne d'un binaire que des jours d'analyse manuelle n'auraient pas révélées.

Ce chapitre positionne le fuzzing non pas comme un outil de test qualité ou de recherche de vulnérabilités (même s'il excelle dans ces rôles), mais comme un **outil de reverse engineering à part entière** — un moyen d'explorer un binaire de l'extérieur pour en comprendre la logique de l'intérieur.

---

## Ce que vous allez apprendre

À l'issue de ce chapitre, vous serez capable de :

- Expliquer en quoi le fuzzing complète l'analyse statique et dynamique dans une démarche de RE.  
- Installer et configurer **AFL++** pour instrumenter un binaire compilé avec GCC.  
- Utiliser **libFuzzer** pour du fuzzing in-process couplé aux sanitizers (ASan, UBSan).  
- Analyser les crashs produits par un fuzzer pour en extraire des informations sur la logique interne du programme (chemins de parsing, validation d'entrées, gestion d'erreurs).  
- Lire et interpréter une **carte de couverture** pour identifier les zones du binaire atteintes et celles qui restent inexplorées.  
- Gérer un **corpus** d'entrées et créer des **dictionnaires** adaptés au format cible pour accélérer la découverte.  
- Appliquer ces techniques sur un cas concret : le parseur de format de fichier custom fourni dans `binaries/ch25-fileformat/`.

---

## Prérequis

Ce chapitre s'appuie sur des notions vues dans les chapitres précédents :

- **Chapitre 2** — Chaîne de compilation GNU : vous devez être à l'aise avec les flags GCC (`-O0`, `-O2`, `-g`, `-fsanitize`) et savoir recompiler un binaire depuis les sources fournies.  
- **Chapitre 5** — Outils d'inspection de base : le workflow de triage rapide (`file`, `strings`, `checksec`) est le point de départ avant toute session de fuzzing.  
- **Chapitre 14** — Valgrind et sanitizers : la compréhension d'AddressSanitizer (ASan) et UndefinedBehaviorSanitizer (UBSan) est essentielle, car les fuzzers modernes s'appuient massivement sur ces sanitizers pour détecter les bugs au-delà des simples crashes.

Une familiarité avec la ligne de commande Linux et un environnement de travail fonctionnel (cf. Chapitre 4) sont indispensables. Les sessions de fuzzing peuvent être gourmandes en ressources — prévoyez au minimum 2 cœurs CPU et 4 Go de RAM disponibles pour votre VM.

---

## Plan du chapitre

| Section | Titre | Contenu |  
|---------|-------|---------|  
| 15.1 | Pourquoi le fuzzing est un outil de RE à part entière | Positionnement du fuzzing dans la méthodologie RE, complémentarité avec statique/dynamique |  
| 15.2 | AFL++ — installation, instrumentation et premier run | Compilation instrumentée avec `afl-gcc`/`afl-clang-fast`, configuration, premier run sur `ch15-keygenme` |  
| 15.3 | libFuzzer — fuzzing in-process avec sanitizers | Écriture d'un harness, compilation avec `-fsanitize=fuzzer`, couplage ASan/UBSan |  
| 15.4 | Analyser les crashs pour comprendre la logique de parsing | Trier les crashs, les reproduire, les interpréter comme indices sur la structure interne ; exploitation du corpus sans crashs |  
| 15.5 | Coverage-guided fuzzing : lire les cartes de couverture | Utilisation d'`afl-cov` et `lcov` pour visualiser la couverture et guider l'analyse |  
| 15.6 | Corpus management et dictionnaires custom | `afl-cmin`, `afl-tmin`, création de dictionnaires à partir de `strings` et de l'analyse statique |  
| 15.7 | Cas pratique : découvrir des chemins cachés dans un parseur binaire | Application complète sur `ch25-fileformat` : du corpus initial au rapport de couverture |

---

## Positionnement dans la formation

```
Partie III — Analyse Dynamique

  Chapitre 11 — GDB                        ← Exécution contrôlée, pas à pas
  Chapitre 12 — GDB amélioré (GEF/pwndbg)  ← Visualisation enrichie
  Chapitre 13 — Frida                       ← Instrumentation et hooking live
  Chapitre 14 — Valgrind & sanitizers       ← Détection de bugs mémoire
  ▶ Chapitre 15 — Fuzzing                   ← Exploration automatisée    ◀ VOUS ÊTES ICI
```

Le fuzzing ferme la boucle de l'analyse dynamique. Là où GDB et Frida vous donnent un **microscope** pour examiner un chemin d'exécution précis, le fuzzer vous donne un **radar** qui balaie l'ensemble de la surface d'entrée du programme. Les crashs qu'il découvre deviennent ensuite des points d'entrée pour une analyse ciblée avec les outils des chapitres 11 à 14.

---

## Conventions utilisées dans ce chapitre

Les commandes shell sont préfixées par `$` (utilisateur normal) ou `#` (root). Les sessions de fuzzing longues sont représentées par des extraits de la sortie d'AFL++ avec des annotations. Les fichiers de crash sont référencés par leur chemin dans le répertoire de sortie standard d'AFL++ (`out/crashes/`).

> 💡 **Astuce** — Encadré vert : conseil pratique pour gagner du temps ou éviter un piège courant.

> ⚠️ **Attention** — Encadré orange : point de vigilance (ressources système, faux positifs, interprétation erronée).

> 🔗 **Lien** — Renvoi vers un autre chapitre ou une annexe de la formation.

⏭️ [Pourquoi le fuzzing est un outil de RE à part entière](/15-fuzzing/01-pourquoi-fuzzing-re.md)
