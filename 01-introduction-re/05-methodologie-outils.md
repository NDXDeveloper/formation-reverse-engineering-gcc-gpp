🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 1.5 — Vue d'ensemble de la méthodologie et des outils du tuto

> **Chapitre 1 — Introduction au Reverse Engineering**  
> 📦 Aucun prérequis technique — section de lecture.  
> 📖 Cette section présente le workflow global et les outils. L'installation et la configuration sont traitées au [chapitre 4](/04-environnement-travail/README.md).

---

## Une méthodologie avant des outils

L'une des erreurs les plus fréquentes chez les débutants en reverse engineering est de se précipiter sur un outil — ouvrir Ghidra, charger le binaire, et commencer à lire du pseudo-code décompilé — sans avoir d'abord établi une stratégie. Le résultat est souvent une navigation erratique dans un océan de fonctions, sans savoir quoi chercher ni comment hiérarchiser l'information.

Le RE efficace repose sur une **méthodologie structurée** : une séquence de phases, chacune avec ses objectifs et ses outils, qui permet de passer progressivement de l'ignorance totale à une compréhension suffisante du binaire pour atteindre l'objectif fixé (comprendre un algorithme, trouver une vulnérabilité, extraire un protocole, etc.).

Cette formation enseigne une méthodologie en **cinq phases**, qui constitue le fil conducteur de toutes les analyses pratiques des Parties II à VI. Ces phases ne sont pas rigidement séquentielles — comme vu en section 1.4, le RE est un processus itératif — mais elles fournissent un cadre de travail qui évite de tourner en rond.

---

## Les cinq phases de l'analyse

### Phase 1 — Triage et reconnaissance

**Objectif** : en moins de cinq minutes, obtenir un profil du binaire sans lire une seule instruction assembleur.

C'est la phase de premier contact. Vous venez de recevoir un fichier binaire — peut-être dans le cadre d'un CTF, d'un audit, ou d'une analyse de malware. Avant toute chose, vous devez répondre à un ensemble de questions fondamentales :

- **Quel type de fichier ?** ELF, PE, Mach-O, script, archive ? La commande `file` répond en une seconde.  
- **Quelle architecture ?** x86-64, x86-32, ARM, MIPS ? `file` ou `readelf -h` vous le disent.  
- **Lié statiquement ou dynamiquement ?** `ldd` liste les dépendances dynamiques. Un binaire sans dépendance est probablement lié statiquement — ce qui change considérablement l'approche d'analyse.  
- **Strippé ou non ?** `file` indique « not stripped » ou « stripped ». `nm` confirme la présence ou l'absence de symboles.  
- **Quelles chaînes de caractères sont embarquées ?** `strings` extrait les séquences de caractères imprimables. Les messages d'erreur, les URLs, les chemins de fichiers, les noms de fonctions de bibliothèques et les constantes textuelles sont souvent les premiers indices de la fonctionnalité du programme.  
- **Quelles protections sont activées ?** `checksec` dresse un inventaire rapide : PIE, NX, canary, RELRO (partiel ou complet), ASLR. Ces informations conditionnent la stratégie d'analyse dynamique.  
- **Quelle est la structure interne ?** `readelf -S` liste les sections ELF. La présence ou l'absence de certaines sections (`.debug_info`, `.plt`, `.got`, `.eh_frame`) donne des indications sur le mode de compilation et les fonctionnalités du programme.

À l'issue du triage, vous disposez d'une **fiche d'identité** du binaire. Vous ne savez pas encore ce qu'il fait, mais vous savez à quoi vous avez affaire, et vous avez identifié les premières pistes à explorer.

**Outils principaux** : `file`, `strings`, `readelf`, `objdump -f`, `nm`, `ldd`, `checksec`, `xxd`/`hexdump`.

**Chapitre de référence** : [5 — Outils d'inspection binaire de base](/05-outils-inspection-base/README.md), et en particulier [5.7 — Workflow « triage rapide »](/05-outils-inspection-base/07-workflow-triage-rapide.md).

---

### Phase 2 — Analyse statique approfondie

**Objectif** : comprendre la structure et la logique du programme par l'examen du code désassemblé et décompilé, sans l'exécuter.

C'est la phase la plus longue et la plus intellectuellement exigeante. Vous chargez le binaire dans un désassembleur/décompileur et vous commencez à reconstruire sa logique.

Le travail procède généralement du général vers le particulier :

**Identifier les points d'entrée** — La fonction `main` est le point de départ naturel pour un programme C. Dans un binaire C++, le constructeur global (`__libc_csu_init`) peut également mériter un examen. Pour un binaire strippé, l'entry point ELF (`_start`) mène à `__libc_start_main`, dont le premier argument est l'adresse de `main`. Les désassembleurs comme Ghidra identifient généralement `main` automatiquement, même sur un binaire strippé.

**Cartographier les fonctions** — Parcourir la liste des fonctions identifiées par le désassembleur, repérer celles qui portent des noms significatifs (si le binaire n'est pas strippé), identifier les fonctions importées depuis les bibliothèques partagées (`printf`, `strcmp`, `malloc`, `send`, `recv`, `AES_encrypt`…). Les imports sont des indices majeurs sur les capacités du programme.

**Suivre le flux de contrôle** — À partir de `main`, suivre les appels de fonctions en profondeur. Les graphes de flux de contrôle (CFG) et les références croisées (XREF) sont vos outils de navigation principaux. Ghidra, IDA et Radare2 offrent tous des vues graphiques du CFG qui facilitent la compréhension des branches conditionnelles et des boucles.

**Renommer et annoter** — Au fur et à mesure de la compréhension, renommer les fonctions, les variables et les types dans le désassembleur. `FUN_00401280` devient `verify_password`. `DAT_00404060` devient `expected_hash`. Cette étape est cruciale : elle transforme un listing opaque en un document lisible, et elle capitalise votre travail pour les prochaines sessions d'analyse.

**Analyser les données** — Examiner les sections de données avec ImHex. Appliquer des patterns `.hexpat` pour visualiser des structures binaires : headers de fichiers custom, tables de configuration, buffers chiffrés. Identifier les constantes magiques qui trahissent l'utilisation d'algorithmes connus (les constantes de l'AES S-box, les valeurs initiales de SHA-256, etc.).

**Outils principaux** : Ghidra (désassemblage, décompilation, XREF, scripting), IDA Free, Radare2/Cutter, Binary Ninja Cloud, ImHex, `objdump -d -M intel`, `c++filt`, BinDiff/Diaphora (diffing).

**Chapitres de référence** : [6 — ImHex](/06-imhex/README.md), [7 — objdump et Binutils](/07-objdump-binutils/README.md), [8 — Ghidra](/08-ghidra/README.md), [9 — IDA, Radare2, Binary Ninja](/09-ida-radare2-binja/README.md), [10 — Diffing de binaires](/10-diffing-binaires/README.md).

---

### Phase 3 — Analyse dynamique ciblée

**Objectif** : valider les hypothèses formulées pendant l'analyse statique en observant le programme en cours d'exécution, et obtenir les valeurs concrètes inaccessibles à l'analyse statique.

L'analyse dynamique n'est pas une exploration à l'aveugle — elle est **guidée par l'analyse statique**. Vous savez déjà, grâce à la phase 2, quelles fonctions vous intéressent, quels branchements sont critiques, quelles zones mémoire contiennent les données sensibles. La phase 3 consiste à vérifier et compléter cette compréhension par l'observation directe.

**Tracer le comportement global** — Avant de sortir GDB, une première exécution supervisée par `strace` et `ltrace` donne une vue d'ensemble du comportement runtime : fichiers ouverts, sockets créées, processus fils lancés, signaux reçus, appels de bibliothèque effectués. C'est un complément rapide au triage statique.

**Déboguer les zones d'intérêt** — Poser des breakpoints sur les fonctions identifiées en phase 2, exécuter le programme, inspecter les registres et la mémoire aux points critiques. Les extensions GDB comme GEF ou pwndbg rendent cette étape beaucoup plus visuelle en affichant en permanence l'état des registres, de la pile et du code environnant.

**Instrumenter sans modifier le binaire** — Frida permet d'intercepter les appels de fonctions, de lire et modifier les arguments et les valeurs de retour, de tracer le flux d'exécution — le tout via des scripts JavaScript injectés dans le processus, sans toucher au binaire sur le disque. C'est un outil particulièrement puissant pour le RE de protocoles réseau et de schémas de chiffrement.

**Explorer par fuzzing** — Quand la surface d'entrée du programme est large ou mal comprise, le fuzzing avec AFL++ ou libFuzzer permet de découvrir des chemins d'exécution inattendus, des crashes révélateurs, et des comportements aux limites qui éclairent la logique de parsing.

**Outils principaux** : GDB (avec GEF, pwndbg ou PEDA), `strace`, `ltrace`, Frida, Valgrind/Memcheck, AFL++, libFuzzer, `pwntools`.

**Chapitres de référence** : [11 — GDB](/11-gdb/README.md), [12 — GDB amélioré](/12-gdb-extensions/README.md), [13 — Frida](/13-frida/README.md), [14 — Valgrind et sanitizers](/14-valgrind-sanitizers/README.md), [15 — Fuzzing](/15-fuzzing/README.md).

---

### Phase 4 — Techniques avancées (si nécessaire)

**Objectif** : surmonter les obstacles que les phases 2 et 3 ne suffisent pas à lever — optimisations agressives, constructions C++ complexes, protections anti-RE, exécution symbolique.

Tous les binaires ne nécessitent pas cette phase. Un programme compilé en `-O0` avec symboles se laisse analyser confortablement avec les techniques des phases 2 et 3. Mais un binaire compilé en `-O3`, strippé, obfusqué ou packé oppose une résistance qui exige des techniques supplémentaires.

**Reconnaître les effets des optimisations** — Le compilateur réorganise le code de manière parfois radicale : inlining, déroulage de boucles, tail call optimization, vectorisation SIMD. Reconnaître ces transformations évite de perdre du temps à analyser du code qui n'a pas d'équivalent direct dans le source original.

**Analyser le C++ au niveau binaire** — Les constructions C++ (vtables, RTTI, exceptions, templates, smart pointers, STL) génèrent des patterns assembleur spécifiques que l'analyste doit apprendre à reconnaître. Sans cette connaissance, un binaire C++ optimisé est un mur opaque.

**Contourner les protections anti-RE** — Stripping, packing (UPX et packers custom), obfuscation de flux de contrôle (control flow flattening), détection de débogueur (`ptrace`, timing checks), contre-mesures aux breakpoints. Chaque protection a ses techniques de contournement.

**Recourir à l'exécution symbolique** — Pour certains problèmes (résolution de crackmes, exploration systématique de branches), l'exécution symbolique avec angr ou la modélisation manuelle de contraintes avec Z3 permet d'obtenir des résultats que l'analyse manuelle mettrait des heures à produire.

**Outils principaux** : Ghidra (reconstruction de types C++), angr, Z3, `checksec`, UPX (unpacking), GDB (contournement anti-debug).

**Chapitres de référence** : [16 — Optimisations du compilateur](/16-optimisations-compilateur/README.md), [17 — RE du C++ avec GCC](/17-re-cpp-gcc/README.md), [18 — Exécution symbolique](/18-execution-symbolique/README.md), [19 — Anti-reversing](/19-anti-reversing/README.md), [20 — Décompilation et reconstruction](/20-decompilation/README.md).

---

### Phase 5 — Exploitation des résultats

**Objectif** : produire un livrable concret à partir de la compréhension acquise — keygen, client de remplacement, déchiffreur, rapport d'analyse, spécification de format, patch.

Le RE n'est complet que lorsque la compréhension est **matérialisée** en quelque chose d'exploitable. Selon l'objectif initial de l'analyse, ce livrable peut prendre différentes formes :

- **Un keygen** — Un script Python qui génère des clés valides en reproduisant l'algorithme de vérification identifié (chapitre 21).  
- **Un client ou serveur de remplacement** — Un programme qui implémente le protocole réseau reversé, capable de communiquer avec le binaire d'origine (chapitre 23).  
- **Un déchiffreur** — Un outil qui reproduit le schéma de chiffrement identifié pour déchiffrer des données protégées (chapitre 24).  
- **Un parser de format** — Un programme qui lit et écrit des fichiers au format propriétaire identifié (chapitre 25).  
- **Un rapport d'analyse** — Un document structuré décrivant les capacités, les IOC, le protocole C2 et les recommandations, dans le cas de l'analyse de malware (chapitre 27).  
- **Une spécification** — Un document technique décrivant un protocole ou un format de fichier identifié, réutilisable par d'autres développeurs (chapitre 25).  
- **Un patch binaire** — Une modification directe du binaire pour corriger un comportement ou contourner une vérification (chapitre 21).  
- **Un plugin compatible** — Un module `.so` développé à partir de la compréhension de l'interface de plugins d'une application (chapitre 22).

**Outils principaux** : Python, `pwntools`, ImHex (patching), `lief`/`pyelftools` (modification d'ELF), YARA (détection), scripts Ghidra headless (automatisation).

**Chapitres de référence** : [21–25 — Cas pratiques](/partie-5-cas-pratiques.md), [27–28 — Analyse de malware](/partie-6-malware.md), [35 — Automatisation et scripting](/35-automatisation-scripting/README.md).

---

## Vision d'ensemble du workflow

Les cinq phases forment un entonnoir : on part d'une vue large et grossière (triage), on affine progressivement la compréhension (analyse statique, puis dynamique), on mobilise des techniques spécialisées si nécessaire (phase 4), et on matérialise le résultat (phase 5). Le tout est itératif — les phases 2 et 3, en particulier, s'alimentent mutuellement en boucle.

```
 ┌─────────────────────────────────────┐
 │  Phase 1 — Triage & reconnaissance  │  file, strings, readelf, checksec
 └──────────────────┬──────────────────┘
                    ▼
 ┌─────────────────────────────────────┐
 │  Phase 2 — Analyse statique         │  Ghidra, ImHex, objdump, YARA
 └──────────────────┬──────────────────┘
                    ▼
            ┌───────────────┐
            │  Hypothèses   │
            └───────┬───────┘
                    ▼
 ┌─────────────────────────────────────┐
 │  Phase 3 — Analyse dynamique        │  GDB, Frida, strace, AFL++
 └──────────────────┬──────────────────┘
                    │
          ┌─────────┴─────────┐
          │  Validation /     │
          │  Nouvelles pistes │
          └─────────┬─────────┘
                    │
         ┌──── Suffisant ? ────┐
         │                     │
        Non                   Oui
         │                     │
         ▼                     ▼
 ┌──────────────────┐  ┌──────────────────────────┐
 │  Phase 4 —       │  │  Phase 5 — Exploitation  │
 │  Avancé          │  │  des résultats           │
 │  (anti-RE, C++,  │  │  (keygen, rapport,       │
 │   symex, optim.) │  │   parser, patch…)        │
 └────────┬─────────┘  └──────────────────────────┘
          │
          └──────► Retour Phase 2 ou 3
```

> 💡 **Ce schéma est un guide, pas un carcan.** En pratique, un analyste expérimenté peut sauter des phases, revenir en arrière, ou mener plusieurs phases en parallèle. L'important est d'avoir un cadre mental qui structure la démarche et évite de se perdre.

---

## Panorama des outils de la formation

Le tableau ci-dessous récapitule tous les outils utilisés dans cette formation, classés par catégorie. Chaque outil est introduit dans un chapitre dédié avec des instructions d'installation et de prise en main. Aucune installation n'est requise à ce stade — le chapitre 4 et le script `check_env.sh` s'en chargent.

### Inspection et triage

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| `file` | Identification du type de fichier | GPL | CLI | 5 |  
| `strings` | Extraction de chaînes de caractères | GPL | CLI | 5 |  
| `xxd` / `hexdump` | Dump hexadécimal brut | GPL | CLI | 5 |  
| `readelf` | Inspection des headers et sections ELF | GPL | CLI | 5 |  
| `objdump` | Désassemblage et inspection de binaires | GPL | CLI | 5, 7 |  
| `nm` | Listing des symboles | GPL | CLI | 5 |  
| `ldd` | Dépendances dynamiques | GPL | CLI | 5 |  
| `checksec` | Inventaire des protections binaires | GPL | CLI | 5 |  
| `c++filt` | Démanglement de symboles C++ | GPL | CLI | 7 |  
| `binwalk` | Analyse de firmwares et fichiers composites | MIT | CLI | 25 |

### Analyse hexadécimale

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| ImHex | Éditeur hexadécimal avancé avec patterns `.hexpat`, YARA, diff | GPL-2.0 | GUI | 6 |

### Désassemblage et décompilation

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| Ghidra | Framework de RE complet (NSA) : désassemblage, décompilation, scripting | Apache 2.0 | GUI + headless | 8 |  
| IDA Free | Désassembleur interactif (version gratuite) | Freeware | GUI | 9 |  
| Radare2 / Cutter | Framework de RE en ligne de commande (+ GUI Cutter) | LGPL-3.0 | CLI + GUI | 9 |  
| Binary Ninja Cloud | Désassembleur/décompileur en ligne (version gratuite) | Freeware | Web | 9 |  
| RetDec | Décompileur statique offline (Avast) | MIT | CLI | 20 |

### Diffing de binaires

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| BinDiff | Comparaison de binaires (Google) | Apache 2.0 | GUI (plugin Ghidra/IDA) | 10 |  
| Diaphora | Plugin open source de diffing pour Ghidra/IDA | GPL | Plugin | 10 |  
| `radiff2` | Diffing en ligne de commande (Radare2) | LGPL-3.0 | CLI | 10 |

### Débogage

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| GDB | Débogueur GNU — l'outil fondamental | GPL | CLI | 11 |  
| GEF | Extension GDB : visualisation, exploitation | MIT | CLI (plugin GDB) | 12 |  
| pwndbg | Extension GDB : heap analysis, exploitation | MIT | CLI (plugin GDB) | 12 |  
| PEDA | Extension GDB : exploitation | BSD | CLI (plugin GDB) | 12 |  
| `gdbserver` | Débogage distant | GPL | CLI | 11 |

### Instrumentation dynamique

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| Frida | Instrumentation dynamique par injection JS | wxWindows | CLI + scripting | 13 |  
| `strace` | Traçage des appels système | BSD | CLI | 5 |  
| `ltrace` | Traçage des appels de bibliothèque | GPL | CLI | 5 |

### Analyse mémoire et profiling

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| Valgrind (Memcheck) | Détection de fuites et erreurs mémoire | GPL | CLI | 14 |  
| Callgrind + KCachegrind | Profiling et graphe d'appels | GPL | CLI + GUI | 14 |  
| ASan / UBSan / MSan | Sanitizers GCC/Clang (compilation avec `-fsanitize`) | Apache 2.0 | Intégré | 14 |

### Fuzzing

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| AFL++ | Fuzzer coverage-guided (mutation + instrumentation) | Apache 2.0 | CLI | 15 |  
| libFuzzer | Fuzzer in-process intégré à Clang | Apache 2.0 | Intégré | 15 |

### Exécution symbolique et solveurs

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| angr | Framework d'exécution symbolique Python | BSD | Python API | 18 |  
| Z3 | Solveur SMT (Microsoft Research) | MIT | Python API | 18 |

### Scripting et automatisation

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| `pwntools` | Framework Python pour le CTF et l'exploitation | MIT | Python | 11, 21, 23 |  
| `pyelftools` | Parsing d'ELF en Python | Domaine public | Python | 35 |  
| `lief` | Parsing et modification de binaires (ELF, PE, Mach-O) | Apache 2.0 | Python / C++ | 35 |  
| YARA | Détection de patterns par règles | BSD | CLI + Python | 6, 27, 35 |  
| Ghidra headless | Analyse automatisée en mode batch | Apache 2.0 | CLI + Java/Python | 8, 35 |  
| r2pipe | Scripting Radare2 via pipe Python | LGPL-3.0 | Python | 9 |  
| GDB Python API | Scripting GDB en Python | GPL | Python | 11 |

### Outils .NET (Partie VII — bonus)

| Outil | Description | Licence | Interface | Chapitre |  
|---|---|---|---|---|  
| ILSpy | Décompileur C# open source | MIT | GUI | 31 |  
| dnSpy / dnSpyEx | Décompileur + débogueur .NET | GPL | GUI | 31, 32 |  
| dotPeek | Décompileur .NET (JetBrains) | Freeware | GUI | 31 |  
| de4dot | Déobfuscateur .NET | GPL | CLI | 31 |

---

## Pourquoi ces outils et pas d'autres ?

La sélection des outils de cette formation répond à trois critères :

**Accessibilité** — La quasi-totalité des outils sont gratuits et open source. Les rares exceptions (IDA Free, Binary Ninja Cloud, dotPeek) sont disponibles en version gratuite. L'objectif est qu'aucun obstacle financier ne bloque l'apprentissage.

**Complémentarité** — Chaque outil a un rôle précis dans le workflow. Il n'y a pas de redondance inutile, mais il y a une couverture volontaire de plusieurs désassembleurs (Ghidra, IDA, Radare2, Binary Ninja) pour que vous puissiez choisir celui qui convient le mieux à votre workflow une fois la formation terminée. Le chapitre 9 propose un comparatif détaillé.

**Pertinence professionnelle** — Ce sont les outils utilisés par les professionnels du RE dans l'industrie. Ghidra est devenu un standard de facto depuis sa publication par la NSA en 2019. GDB avec GEF/pwndbg est l'environnement de débogage dominant sous Linux. Frida est l'outil d'instrumentation dynamique de référence. AFL++ est le fuzzer le plus utilisé. Maîtriser ces outils, c'est être immédiatement opérationnel dans un contexte professionnel.

> 💡 **Vous n'avez pas besoin de maîtriser tous ces outils.** La formation les présente tous, mais en pratique, la plupart des analystes développent une affinité avec un sous-ensemble d'outils qu'ils utilisent au quotidien, et n'en sortent que lorsqu'un problème spécifique l'exige. L'important est de savoir que chaque outil existe, ce qu'il fait, et quand il est pertinent de le mobiliser.

---

> 📖 **À retenir** — Le RE suit une méthodologie en cinq phases : triage, analyse statique, analyse dynamique, techniques avancées (si nécessaire), et exploitation des résultats. Ce workflow est itératif — les phases 2 et 3 s'alimentent mutuellement en boucle. Chaque phase a ses outils dédiés, tous gratuits ou open source, couverts par un chapitre spécifique de la formation. L'installation et la configuration de ces outils sont traitées au chapitre 4.

---


⏭️ [Taxonomie des cibles : binaire natif, bytecode, firmware — où se situe ce tuto](/01-introduction-re/06-taxonomie-cibles.md)
