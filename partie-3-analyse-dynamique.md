🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Partie III — Analyse Dynamique

L'analyse statique vous donne une vue du binaire tel qu'il est sur disque — l'analyse dynamique vous montre ce qu'il **fait réellement** quand il tourne. En plaçant des breakpoints, en hookant des fonctions, en traçant les allocations mémoire ou en bombardant un parseur d'inputs malformés, vous observez le comportement concret du programme : quelles branches sont prises, quelles données transitent, quels buffers débordent. Les deux approches sont complémentaires — l'analyse statique formule les hypothèses, l'analyse dynamique les confirme ou les invalide.

---

## 🎯 Objectifs de cette partie

À l'issue de ces cinq chapitres, vous serez capable de :

1. **Déboguer n'importe quel binaire ELF avec GDB** — y compris strippé et sans symboles — en utilisant breakpoints conditionnels, watchpoints, catchpoints et la Python API pour automatiser vos sessions.  
2. **Exploiter les extensions GDB (GEF, pwndbg, PEDA)** pour visualiser la pile, les registres et le heap en temps réel, et rechercher des gadgets ROP directement depuis le débogueur.  
3. **Instrumenter un processus en cours d'exécution avec Frida** : hooker des fonctions C/C++, intercepter et modifier des arguments ou des valeurs de retour à la volée, et tracer la couverture de code avec Stalker.  
4. **Détecter des bugs mémoire et profiler l'exécution** avec Valgrind (Memcheck, Callgrind) et les sanitizers GCC (`-fsanitize=address,undefined,memory`), puis exploiter ces rapports pour comprendre la logique interne du programme.  
5. **Fuzzer un binaire compilé avec GCC** via AFL++ ou libFuzzer, analyser les crashs obtenus, et utiliser les cartes de couverture pour cartographier les chemins d'exécution du programme.  
6. **Combiner analyse statique et dynamique** dans un workflow itératif : identifier une fonction suspecte dans Ghidra, poser un breakpoint ciblé dans GDB, hooker ses entrées/sorties avec Frida, puis valider avec du fuzzing.

---

## 📋 Chapitres

| N° | Titre | Description | Lien |  
|----|-------|-------------|------|  
| 11 | Débogage avec GDB | Compilation avec symboles DWARF, commandes fondamentales, inspection pile/registres/mémoire, binaire strippé, breakpoints conditionnels, watchpoints, catchpoints, `gdbserver`, GDB Python API, introduction à `pwntools`. | [Chapitre 11](/11-gdb/README.md) |  
| 12 | GDB amélioré : PEDA, GEF, pwndbg | Installation et comparaison des trois extensions, visualisation stack/registres en temps réel, recherche de gadgets ROP, analyse de heap avec `vis_heap_chunks`. | [Chapitre 12](/12-gdb-extensions/README.md) |  
| 13 | Instrumentation dynamique avec Frida | Architecture agent JS, modes d'injection (spawn vs attach), hooking de fonctions C/C++, interception de `malloc`/`free`/`open`/customs, modification d'arguments et retours en live, Stalker pour le code coverage. | [Chapitre 13](/13-frida/README.md) |  
| 14 | Analyse avec Valgrind et sanitizers | Memcheck (fuites et accès invalides), Callgrind + KCachegrind (profiling et graphe d'appels), ASan, UBSan, MSan — exploiter les rapports pour comprendre la logique interne. | [Chapitre 14](/14-valgrind-sanitizers/README.md) |  
| 15 | Fuzzing pour le Reverse Engineering | AFL++ (instrumentation et premier run), libFuzzer (fuzzing in-process), coverage-guided fuzzing, gestion de corpus et dictionnaires, analyse de crashs pour comprendre la logique de parsing. | [Chapitre 15](/15-fuzzing/README.md) |

---

## 🛠️ Outils couverts

- **GDB** — débogueur GNU, outil central de l'analyse dynamique sous Linux.  
- **GEF** (GDB Enhanced Features) — extension GDB orientée exploitation et RE, la plus activement maintenue.  
- **pwndbg** — extension GDB spécialisée heap/exploitation, commandes `vis_heap_chunks`, `bins`, `arena`.  
- **PEDA** (Python Exploit Development Assistance) — extension GDB historique, recherche de patterns et gadgets.  
- **`gdbserver`** — débogage distant (cible sur une machine, GDB sur une autre).  
- **`pwntools`** — framework Python pour l'interaction automatisée avec des binaires (I/O, patching, exploitation).  
- **Frida** — instrumentation dynamique multi-plateforme par injection d'un agent JavaScript dans le processus cible.  
- **`frida-trace`** — tracing rapide de fonctions sans écrire de script complet.  
- **Valgrind / Memcheck** — détection de fuites mémoire, lectures non initialisées, accès hors bornes.  
- **Callgrind + KCachegrind** — profiling d'exécution et visualisation du graphe d'appels.  
- **AddressSanitizer (ASan)** — détection de buffer overflows et use-after-free à la compilation (`-fsanitize=address`).  
- **UndefinedBehaviorSanitizer (UBSan)** — détection de comportements indéfinis (`-fsanitize=undefined`).  
- **MemorySanitizer (MSan)** — détection de lectures de mémoire non initialisée (`-fsanitize=memory`).  
- **AFL++** — fuzzer coverage-guided de référence, fork amélioré d'AFL.  
- **libFuzzer** — fuzzer in-process intégré à LLVM/Clang, compatible avec les sanitizers.  
- **`afl-cov` / `lcov`** — visualisation de la couverture de code atteinte par le fuzzing.

---

## ⚠️ Précautions

L'analyse dynamique implique d'**exécuter le binaire**. Si vous travaillez sur un programme dont vous ne maîtrisez pas le comportement — ce qui est le cas de tout binaire en cours de RE — appliquez systématiquement ces règles :

- Travaillez **exclusivement dans une VM isolée** (configurée au chapitre 4). Jamais sur votre machine hôte.  
- Réseau de la VM en **host-only ou déconnecté**. Un binaire qui ouvre des sockets ne doit pas atteindre Internet.  
- Prenez un **snapshot avant chaque exécution**. Si le binaire altère le filesystem, vous pouvez revenir en arrière en un clic.  
- Les binaires des chapitres 27-29 (malware pédagogique) exigent une isolation renforcée — la Partie VI détaille la mise en place d'un lab sécurisé dédié.

---

## ⏱️ Durée estimée

**~20-28 heures** pour un développeur ayant complété les Parties I et II.

Le chapitre 11 (GDB) est le plus long de cette partie (~6-8h) : c'est l'outil que vous utiliserez le plus dans votre pratique quotidienne du RE, et la maîtrise de la Python API demande du temps. Le chapitre 12 (extensions GDB) est un complément plus rapide (~2-3h). Le chapitre 13 (Frida) nécessite de se familiariser avec l'API JavaScript de Frida (~4-5h). Les chapitres 14 (Valgrind/sanitizers, ~3h) et 15 (fuzzing, ~4-5h) sont plus autonomes et peuvent être abordés dans l'ordre qui vous convient.

---

## 📌 Prérequis

Avoir complété la **[Partie I — Fondamentaux & Environnement](/partie-1-fondamentaux.md)** et la **[Partie II — Analyse Statique](/partie-2-analyse-statique.md)**, ou disposer des connaissances équivalentes :

- Lire un désassemblage x86-64 et identifier les structures de contrôle (boucles, conditions, appels de fonctions).  
- Naviguer dans un binaire ELF avec `readelf`, `objdump` ou Ghidra.  
- Connaître les sections ELF clés et le mécanisme PLT/GOT.  
- Disposer d'une VM de travail opérationnelle avec les outils installés.

L'analyse dynamique s'appuie constamment sur ce que l'analyse statique a révélé : vous poserez vos breakpoints sur les fonctions identifiées dans Ghidra, vous hookerez avec Frida les appels repérés dans `objdump`, vous fuzzerez les parseurs dont vous avez lu le code dans le decompiler.

---

## ⬅️ Partie précédente

← [**Partie II — Analyse Statique**](/partie-2-analyse-statique.md)

## ➡️ Partie suivante

L'analyse statique et dynamique maîtrisées, vous aborderez les techniques avancées : comprendre les optimisations du compilateur, reverser du C++ (vtables, STL, templates), résoudre des crackmes avec l'exécution symbolique, et contourner les protections anti-reversing.

→ [**Partie IV — Techniques Avancées de RE**](/partie-4-techniques-avancees.md)

⏭️ [Chapitre 11 — Débogage avec GDB](/11-gdb/README.md)
