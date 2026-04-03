# Formation Reverse Engineering — Applications compilées avec la chaîne GNU

> 📄 **Licence MIT** — Ce contenu est strictement éducatif et éthique.  
> 📁 Tous les binaires d'entraînement sont dans `binaries/`, recompilables via `make` avec le `Makefile` fourni.  
> Chaque dossier de chapitre contient les sources `.c` / `.cpp` et un `Makefile` dédié pour reproduire chaque binaire à différents niveaux d'optimisation (`-O0` à `-O3`), avec et sans symboles.  
> 🎯 Chaque chapitre se termine par un **checkpoint** (mini-exercice) pour valider les acquis avant de passer à la suite.

---

## [Préface](/preface.md)

- [Objectifs du tutoriel et public visé](/preface.md#objectifs-du-tutoriel-et-public-visé)  
- [Prérequis recommandés (C/C++ intermédiaire, Linux CLI, notions de mémoire)](/preface.md#prérequis-recommandés)  
- [Comment utiliser ce tutoriel : parcours linéaire vs. accès par besoin](/preface.md#comment-utiliser-ce-tutoriel)  
- [Conventions typographiques et pictogrammes utilisés](/preface.md#conventions-typographiques)  
- [Remerciements et ressources ayant inspiré ce projet](/preface.md#remerciements)

---

## **[Partie I — Fondamentaux & Environnement](/partie-1-fondamentaux.md)**

### [Chapitre 1 — Introduction au Reverse Engineering](/01-introduction-re/README.md)

- 1.1 [Définition et objectifs du RE](/01-introduction-re/01-definition-objectifs.md)  
- 1.2 [Cadre légal et éthique (licences, lois CFAA / EUCD / DMCA)](/01-introduction-re/02-cadre-legal-ethique.md)  
- 1.3 [Cas d'usage légitimes : audit de sécurité, CTF, débogage avancé, interopérabilité](/01-introduction-re/03-cas-usage-legitimes.md)  
- 1.4 [Différence entre RE statique et RE dynamique](/01-introduction-re/04-statique-vs-dynamique.md)  
- 1.5 [Vue d'ensemble de la méthodologie et des outils du tuto](/01-introduction-re/05-methodologie-outils.md)  
- 1.6 [Taxonomie des cibles : binaire natif, bytecode, firmware — où se situe ce tuto](/01-introduction-re/06-taxonomie-cibles.md)  
- [**🎯 Checkpoint** : classer 5 scénarios donnés en « statique » ou « dynamique »](/01-introduction-re/checkpoint.md)

### [Chapitre 2 — La chaîne de compilation GNU](/02-chaine-compilation-gnu/README.md)

- 2.1 [Architecture de GCC/G++ : préprocesseur → compilateur → assembleur → linker](/02-chaine-compilation-gnu/01-architecture-gcc.md)  
- 2.2 [Phases de compilation et fichiers intermédiaires (`.i`, `.s`, `.o`)](/02-chaine-compilation-gnu/02-phases-compilation.md)  
- 2.3 [Formats binaires : ELF (Linux), PE (Windows via MinGW), Mach-O (macOS)](/02-chaine-compilation-gnu/03-formats-binaires.md)  
- 2.4 [Sections ELF clés : `.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`, `.init`, `.fini`](/02-chaine-compilation-gnu/04-sections-elf.md)  
- 2.5 [Flags de compilation et leur impact sur le RE (`-O0` à `-O3`, `-g`, `-s`, `-fPIC`, `-pie`)](/02-chaine-compilation-gnu/05-flags-compilation.md)  
- 2.6 [Comprendre les fichiers de symboles DWARF](/02-chaine-compilation-gnu/06-symboles-dwarf.md)  
- 2.7 [Le Loader Linux (`ld.so`) : du fichier ELF au processus en mémoire](/02-chaine-compilation-gnu/07-loader-linux.md)  
- 2.8 [Mappage des segments, ASLR et adresses virtuelles : pourquoi les adresses bougent](/02-chaine-compilation-gnu/08-segments-aslr.md)  
- 2.9 [Résolution dynamique des symboles : PLT/GOT en détail (lazy binding)](/02-chaine-compilation-gnu/09-plt-got-lazy-binding.md)  
- [**🎯 Checkpoint** : compiler un même `hello.c` avec `-O0 -g` puis `-O2 -s`, comparer les tailles et sections avec `readelf`](/02-chaine-compilation-gnu/checkpoint.md)

### [Chapitre 3 — Bases de l'assembleur x86-64 pour le RE](/03-assembleur-x86-64/README.md)

- 3.1 [Registres généraux, pointeurs et flags (`rax`, `rsp`, `rbp`, `rip`, `RFLAGS`…)](/03-assembleur-x86-64/01-registres-pointeurs-flags.md)  
- 3.2 [Instructions essentielles : `mov`, `push`/`pop`, `call`/`ret`, `lea`](/03-assembleur-x86-64/02-instructions-essentielles.md)  
- 3.3 [Arithmétique et logique : `add`, `sub`, `imul`, `xor`, `shl`/`shr`, `test`, `cmp`](/03-assembleur-x86-64/03-arithmetique-logique.md)  
- 3.4 [Sauts conditionnels et inconditionnels : `jmp`, `jz`/`jnz`, `jl`, `jge`, `jle`, `ja`…](/03-assembleur-x86-64/04-sauts-conditionnels.md)  
- 3.5 [La pile : prologue, épilogue et conventions d'appel System V AMD64](/03-assembleur-x86-64/05-pile-prologue-epilogue.md)  
- 3.6 [Passage des paramètres : `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` puis la pile](/03-assembleur-x86-64/06-passage-parametres.md)  
- 3.7 [Lire un listing assembleur sans paniquer : méthode pratique en 5 étapes](/03-assembleur-x86-64/07-methode-lecture-asm.md)  
- 3.8 [Différence entre appel de bibliothèque (`call printf@plt`) et syscall direct (`syscall`)](/03-assembleur-x86-64/08-call-plt-vs-syscall.md)  
- 3.9 [Introduction aux instructions SIMD (SSE/AVX) — les reconnaître sans les craindre](/03-assembleur-x86-64/09-introduction-simd.md)  
- [**🎯 Checkpoint** : annoter manuellement un désassemblage réel (fourni)](/03-assembleur-x86-64/checkpoint.md)

### [Chapitre 4 — Mise en place de l'environnement de travail](/04-environnement-travail/README.md)

- 4.1 [Distribution Linux recommandée (Ubuntu/Debian/Kali)](/04-environnement-travail/01-distribution-linux.md)  
- 4.2 [Installation et configuration des outils essentiels (liste versionnée)](/04-environnement-travail/02-installation-outils.md)  
- 4.3 [Création d'une VM sandboxée (VirtualBox / QEMU / UTM pour macOS)](/04-environnement-travail/03-creation-vm.md)  
- 4.4 [Configuration réseau de la VM : NAT, host-only, isolation](/04-environnement-travail/04-configuration-reseau-vm.md)  
- 4.5 [Structure du dépôt : organisation de `binaries/` et des `Makefile` par chapitre](/04-environnement-travail/05-structure-depot.md)  
- 4.6 [Compiler tous les binaires d'entraînement en une commande (`make all`)](/04-environnement-travail/06-compiler-binaires.md)  
- 4.7 [Vérifier l'installation : script `check_env.sh` fourni](/04-environnement-travail/07-verifier-installation.md)  
- [**🎯 Checkpoint** : exécuter `check_env.sh` — tous les outils doivent être au vert](/04-environnement-travail/checkpoint.md)

---

## **[Partie II — Analyse Statique](/partie-2-analyse-statique.md)**

### [Chapitre 5 — Outils d'inspection binaire de base](/05-outils-inspection-base/README.md)

- 5.1 [`file`, `strings`, `xxd` / `hexdump` — premier contact avec un binaire inconnu](/05-outils-inspection-base/01-file-strings-xxd.md)  
- 5.2 [`readelf` et `objdump` — anatomie d'un ELF (headers, sections, segments)](/05-outils-inspection-base/02-readelf-objdump.md)  
- 5.3 [`nm` et `objdump -t` — inspection des tables de symboles](/05-outils-inspection-base/03-nm-symboles.md)  
- 5.4 [`ldd` et `ldconfig` — dépendances dynamiques et résolution](/05-outils-inspection-base/04-ldd-ldconfig.md)  
- 5.5 [`strace` / `ltrace` — appels système et appels de bibliothèques (syscall vs libc)](/05-outils-inspection-base/05-strace-ltrace.md)  
- 5.6 [`checksec` — inventaire des protections d'un binaire (ASLR, PIE, NX, canary, RELRO)](/05-outils-inspection-base/06-checksec.md)  
- 5.7 [Workflow « triage rapide » : la routine des 5 premières minutes face à un binaire](/05-outils-inspection-base/07-workflow-triage-rapide.md)  
- [**🎯 Checkpoint** : réaliser un triage complet du binaire `mystery_bin` fourni, rédiger un rapport d'une page](/05-outils-inspection-base/checkpoint.md)

### [Chapitre 6 — ImHex : analyse hexadécimale avancée](/06-imhex/README.md)

- 6.1 [Pourquoi ImHex dépasse le simple hex editor](/06-imhex/01-pourquoi-imhex.md)  
- 6.2 [Installation et tour de l'interface (Pattern Editor, Data Inspector, Bookmarks, Diff)](/06-imhex/02-installation-interface.md)  
- 6.3 [Le langage de patterns `.hexpat` — syntaxe et types de base](/06-imhex/03-langage-hexpat.md)  
- 6.4 [Écrire un pattern pour visualiser un header ELF depuis zéro](/06-imhex/04-pattern-header-elf.md)  
- 6.5 [Parser une structure C/C++ maison directement dans le binaire](/06-imhex/05-parser-structure-custom.md)  
- 6.6 [Colorisation, annotations et bookmarks de régions binaires](/06-imhex/06-colorisation-annotations.md)  
- 6.7 [Comparaison de deux versions d'un même binaire GCC (diff)](/06-imhex/07-comparaison-diff.md)  
- 6.8 [Recherche de magic bytes, chaînes encodées et séquences d'opcodes](/06-imhex/08-recherche-magic-bytes.md)  
- 6.9 [Intégration avec le désassembleur intégré d'ImHex](/06-imhex/09-desassembleur-integre.md)  
- 6.10 [Appliquer des règles YARA depuis ImHex (pont vers l'analyse malware)](/06-imhex/10-regles-yara.md)  
- 6.11 [Cas pratique : cartographier un format de fichier custom avec `.hexpat`](/06-imhex/11-cas-pratique-format-custom.md)  
- [**🎯 Checkpoint** : écrire un `.hexpat` complet pour le format `ch23-fileformat`](/06-imhex/checkpoint.md)

### [Chapitre 7 — Désassemblage avec objdump et Binutils](/07-objdump-binutils/README.md)

- 7.1 [Désassemblage d'un binaire compilé sans symboles (`-s`)](/07-objdump-binutils/01-desassemblage-sans-symboles.md)  
- 7.2 [Syntaxe AT&T vs Intel — passer de l'une à l'autre (`-M intel`)](/07-objdump-binutils/02-att-vs-intel.md)  
- 7.3 [Comparaison avec/sans optimisations GCC (`-O0` vs `-O2` vs `-O3`)](/07-objdump-binutils/03-comparaison-optimisations.md)  
- 7.4 [Lecture du prologue/épilogue de fonctions en pratique](/07-objdump-binutils/04-prologue-epilogue.md)  
- 7.5 [Identifier `main()` et les fonctions C++ (name mangling)](/07-objdump-binutils/05-identifier-main-mangling.md)  
- 7.6 [`c++filt` — démanglement des symboles C++](/07-objdump-binutils/06-cppfilt-demanglement.md)  
- 7.7 [Limitations d'`objdump` : pourquoi un vrai désassembleur est nécessaire](/07-objdump-binutils/07-limitations-objdump.md)  
- [**🎯 Checkpoint** : désassembler `keygenme_O0` et `keygenme_O2`, lister les différences clés](/07-objdump-binutils/checkpoint.md)

### [Chapitre 8 — Désassemblage avancé avec Ghidra](/08-ghidra/README.md)

- 8.1 [Installation et prise en main de Ghidra (NSA)](/08-ghidra/01-installation-prise-en-main.md)  
- 8.2 [Import d'un binaire ELF — analyse automatique et options](/08-ghidra/02-import-elf-analyse.md)  
- 8.3 [Navigation dans le CodeBrowser : Listing, Decompiler, Symbol Tree, Function Graph](/08-ghidra/03-navigation-codebrowser.md)  
- 8.4 [Renommage de fonctions et variables, ajout de commentaires, création de types](/08-ghidra/04-renommage-commentaires-types.md)  
- 8.5 [Reconnaître les structures GCC : vtables C++, RTTI, exceptions](/08-ghidra/05-structures-gcc-vtables-rtti.md)  
- 8.6 [Reconstruire des structures de données (`struct`, `class`, `enum`)](/08-ghidra/06-reconstruire-structures.md)  
- 8.7 [Cross-references (XREF) : tracer l'usage d'une fonction ou d'une donnée](/08-ghidra/07-cross-references-xref.md)  
- 8.8 [Scripts Ghidra en Java/Python pour automatiser l'analyse](/08-ghidra/08-scripts-java-python.md)  
- 8.9 [Ghidra en mode headless pour le traitement batch](/08-ghidra/09-headless-mode-batch.md)  
- [**🎯 Checkpoint** : importer `ch08-oop` dans Ghidra, reconstruire la hiérarchie de classes](/08-ghidra/checkpoint.md)

### [Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja](/09-ida-radare2-binja/README.md)

- 9.1 [IDA Free — workflow de base sur binaire GCC](/09-ida-radare2-binja/01-ida-free-workflow.md)  
- 9.2 [Radare2 / Cutter — analyse en ligne de commande et GUI](/09-ida-radare2-binja/02-radare2-cutter.md)  
- 9.3 [`r2` : commandes essentielles (`aaa`, `pdf`, `afl`, `iz`, `iS`, `VV`)](/09-ida-radare2-binja/03-r2-commandes-essentielles.md)  
- 9.4 [Scripting avec r2pipe (Python)](/09-ida-radare2-binja/04-scripting-r2pipe.md)  
- 9.5 [Binary Ninja Cloud (version gratuite) — prise en main rapide](/09-ida-radare2-binja/05-binary-ninja-cloud.md)  
- 9.6 [Comparatif Ghidra vs IDA vs Radare2 vs Binary Ninja (fonctionnalités, prix, cas d'usage)](/09-ida-radare2-binja/06-comparatif-outils.md)  
- [**🎯 Checkpoint** : analyser le même binaire dans 2 outils différents, comparer les résultats du decompiler](/09-ida-radare2-binja/checkpoint.md)

### [Chapitre 10 — Diffing de binaires](/10-diffing-binaires/README.md)

- 10.1 [Pourquoi comparer deux versions d'un même binaire (analyse de patch, détection de vuln)](/10-diffing-binaires/01-pourquoi-diffing.md)  
- 10.2 [BinDiff (Google) — installation, import depuis Ghidra/IDA, lecture du résultat](/10-diffing-binaires/02-bindiff.md)  
- 10.3 [Diaphora — plugin Ghidra/IDA open source pour le diffing](/10-diffing-binaires/03-diaphora.md)  
- 10.4 [`radiff2` — diffing en ligne de commande avec Radare2](/10-diffing-binaires/04-radiff2.md)  
- 10.5 [Cas pratique : identifier une correction de vulnérabilité entre deux versions d'un binaire](/10-diffing-binaires/05-cas-pratique-patch-vuln.md)  
- [**🎯 Checkpoint** : comparer `keygenme_v1` et `keygenme_v2`, identifier la fonction modifiée](/10-diffing-binaires/checkpoint.md)

---

## **[Partie III — Analyse Dynamique](/partie-3-analyse-dynamique.md)**

### [Chapitre 11 — Débogage avec GDB](/11-gdb/README.md)

- 11.1 [Compilation avec symboles de débogage (`-g`, DWARF)](/11-gdb/01-compilation-symboles-debug.md)  
- 11.2 [Commandes GDB fondamentales : `break`, `run`, `next`, `step`, `info`, `x`, `print`](/11-gdb/02-commandes-fondamentales.md)  
- 11.3 [Inspecter la pile, les registres, la mémoire (format et tailles)](/11-gdb/03-inspecter-pile-registres-memoire.md)  
- 11.4 [GDB sur un binaire strippé — travailler sans symboles](/11-gdb/04-gdb-binaire-strippe.md)  
- 11.5 [Breakpoints conditionnels et watchpoints (mémoire et registres)](/11-gdb/05-breakpoints-conditionnels-watchpoints.md)  
- 11.6 [Catchpoints : intercepter les `fork`, `exec`, `syscall`, signaux](/11-gdb/06-catchpoints.md)  
- 11.7 [Remote debugging avec `gdbserver` (debugging sur cible distante)](/11-gdb/07-remote-debugging-gdbserver.md)  
- 11.8 [GDB Python API — scripting et automatisation](/11-gdb/08-gdb-python-api.md)  
- 11.9 [Introduction à `pwntools` pour automatiser les interactions avec un binaire](/11-gdb/09-introduction-pwntools.md)  
- [**🎯 Checkpoint** : écrire un script GDB Python qui dumpe automatiquement les arguments de chaque appel à `strcmp`](/11-gdb/checkpoint.md)

### [Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg](/12-gdb-extensions/README.md)

- 12.1 [Installation et comparaison des trois extensions](/12-gdb-extensions/01-installation-comparaison.md)  
- 12.2 [Visualisation de la stack et des registres en temps réel](/12-gdb-extensions/02-visualisation-stack-registres.md)  
- 12.3 [Recherche de gadgets ROP depuis GDB](/12-gdb-extensions/03-recherche-gadgets-rop.md)  
- 12.4 [Analyse de heap avec pwndbg (`vis_heap_chunks`, `bins`)](/12-gdb-extensions/04-analyse-heap-pwndbg.md)  
- 12.5 [Commandes utiles spécifiques à chaque extension](/12-gdb-extensions/05-commandes-specifiques.md)  
- [**🎯 Checkpoint** : tracer l'exécution complète de `keygenme_O0` avec GEF, capturer le moment de la comparaison](/12-gdb-extensions/checkpoint.md)

### [Chapitre 13 — Instrumentation dynamique avec Frida](/13-frida/README.md)

- 13.1 [Architecture de Frida — agent JS injecté dans le processus cible](/13-frida/01-architecture-frida.md)  
- 13.2 [Modes d'injection : `frida`, `frida-trace`, spawn vs attach](/13-frida/02-modes-injection.md)  
- 13.3 [Hooking de fonctions C et C++ à la volée](/13-frida/03-hooking-fonctions-c-cpp.md)  
- 13.4 [Intercepter les appels à `malloc`, `free`, `open`, fonctions customs](/13-frida/04-intercepter-appels.md)  
- 13.5 [Modifier des arguments et valeurs de retour en live](/13-frida/05-modifier-arguments-retour.md)  
- 13.6 [Stalker : tracer toutes les instructions exécutées (code coverage dynamique)](/13-frida/06-stalker-code-coverage.md)  
- 13.7 [Cas pratique : contourner une vérification de licence](/13-frida/07-cas-pratique-licence.md)  
- [**🎯 Checkpoint** : écrire un script Frida qui logue tous les appels à `send()` avec leurs buffers](/13-frida/checkpoint.md)

### [Chapitre 14 — Analyse avec Valgrind et sanitizers](/14-valgrind-sanitizers/README.md)

- 14.1 [Valgrind / Memcheck — fuites mémoire et comportement runtime](/14-valgrind-sanitizers/01-valgrind-memcheck.md)  
- 14.2 [Callgrind + KCachegrind — profiling et graphe d'appels](/14-valgrind-sanitizers/02-callgrind-kcachegrind.md)  
- 14.3 [AddressSanitizer (ASan), UBSan, MSan — compiler avec `-fsanitize`](/14-valgrind-sanitizers/03-asan-ubsan-msan.md)  
- 14.4 [Exploiter les rapports de sanitizers pour comprendre la logique interne](/14-valgrind-sanitizers/04-exploiter-rapports.md)  
- [**🎯 Checkpoint** : lancer Valgrind sur `ch14-crypto`, identifier les buffers de clés en mémoire](/14-valgrind-sanitizers/checkpoint.md)

### [Chapitre 15 — Fuzzing pour le Reverse Engineering](/15-fuzzing/README.md)

- 15.1 [Pourquoi le fuzzing est un outil de RE à part entière](/15-fuzzing/01-pourquoi-fuzzing-re.md)  
- 15.2 [AFL++ — installation, instrumentation et premier run sur une appli GCC](/15-fuzzing/02-afl-plus-plus.md)  
- 15.3 [libFuzzer — fuzzing in-process avec sanitizers](/15-fuzzing/03-libfuzzer.md)  
- 15.4 [Analyser les crashs pour comprendre la logique de parsing](/15-fuzzing/04-analyser-crashs.md)  
- 15.5 [Coverage-guided fuzzing : lire les cartes de couverture (`afl-cov`, `lcov`)](/15-fuzzing/05-coverage-guided.md)  
- 15.6 [Corpus management et dictionnaires custom](/15-fuzzing/06-corpus-dictionnaires.md)  
- 15.7 [Cas pratique : découvrir des chemins cachés dans un parseur binaire](/15-fuzzing/07-cas-pratique-parseur.md)  
- [**🎯 Checkpoint** : fuzzer `ch15-fileformat` avec AFL++, trouver au moins 2 crashs et les analyser](/15-fuzzing/checkpoint.md)

---

## **[Partie IV — Techniques Avancées de RE](/partie-4-techniques-avancees.md)**

### [Chapitre 16 — Comprendre les optimisations du compilateur](/16-optimisations-compilateur/README.md)

- 16.1 [Impact de `-O1`, `-O2`, `-O3`, `-Os` sur le code désassemblé](/16-optimisations-compilateur/01-impact-niveaux-optimisation.md)  
- 16.2 [Inlining de fonctions : quand la fonction disparaît du binaire](/16-optimisations-compilateur/02-inlining.md)  
- 16.3 [Déroulage de boucles et vectorisation (SIMD/SSE/AVX)](/16-optimisations-compilateur/03-deroulage-vectorisation.md)  
- 16.4 [Tail call optimization et son impact sur la pile](/16-optimisations-compilateur/04-tail-call-optimization.md)  
- 16.5 [Optimisations Link-Time (`-flto`) et leurs effets sur le graphe d'appels](/16-optimisations-compilateur/05-link-time-optimization.md)  
- 16.6 [Reconnaître les patterns typiques de GCC (idiomes compilateur)](/16-optimisations-compilateur/06-patterns-idiomes-gcc.md)  
- 16.7 [Comparaison GCC vs Clang : différences de patterns à l'assembleur](/16-optimisations-compilateur/07-gcc-vs-clang.md)  
- [**🎯 Checkpoint** : identifier 3 optimisations appliquées par GCC sur un binaire `-O2` fourni](/16-optimisations-compilateur/checkpoint.md)

### [Chapitre 17 — Reverse Engineering du C++ avec GCC](/17-re-cpp-gcc/README.md)

- 17.1 [Name mangling — règles Itanium ABI et démanglement](/17-re-cpp-gcc/01-name-mangling-itanium.md)  
- 17.2 [Modèle objet C++ : vtable, vptr, héritage simple et multiple](/17-re-cpp-gcc/02-modele-objet-vtable.md)  
- 17.3 [RTTI (Run-Time Type Information) et `dynamic_cast`](/17-re-cpp-gcc/03-rtti-dynamic-cast.md)  
- 17.4 [Gestion des exceptions (`.eh_frame`, `.gcc_except_table`, `__cxa_throw`)](/17-re-cpp-gcc/04-gestion-exceptions.md)  
- 17.5 [STL internals : `std::vector`, `std::string`, `std::map`, `std::unordered_map` en mémoire](/17-re-cpp-gcc/05-stl-internals.md)  
- 17.6 [Templates : instanciations et explosion de symboles](/17-re-cpp-gcc/06-templates-instanciations.md)  
- 17.7 [Lambda, closures et captures en assembleur](/17-re-cpp-gcc/07-lambda-closures.md)  
- 17.8 [Smart Pointers en assembleur : `unique_ptr` vs `shared_ptr` (comptage de références)](/17-re-cpp-gcc/08-smart-pointers.md)  
- 17.9 [Coroutines C++20 : reconnaître le frame et le state machine pattern](/17-re-cpp-gcc/09-coroutines-cpp20.md)  
- [**🎯 Checkpoint** : reconstruire les classes du binaire `ch17-oop` à partir du désassemblage seul](/17-re-cpp-gcc/checkpoint.md)

### [Chapitre 18 — Exécution symbolique et solveurs de contraintes](/18-execution-symbolique/README.md)

- 18.1 [Principes de l'exécution symbolique : traiter les inputs comme des symboles](/18-execution-symbolique/01-principes-execution-symbolique.md)  
- 18.2 [angr — installation et architecture (SimState, SimManager, exploration)](/18-execution-symbolique/02-angr-installation-architecture.md)  
- 18.3 [Résoudre un crackme automatiquement avec angr](/18-execution-symbolique/03-resoudre-crackme-angr.md)  
- 18.4 [Z3 Theorem Prover — modéliser des contraintes extraites manuellement](/18-execution-symbolique/04-z3-theorem-prover.md)  
- 18.5 [Limites : explosion de chemins, boucles, appels système](/18-execution-symbolique/05-limites-explosion-chemins.md)  
- 18.6 [Combinaison avec le RE manuel : quand utiliser l'exécution symbolique](/18-execution-symbolique/06-combinaison-re-manuel.md)  
- [**🎯 Checkpoint** : résoudre `keygenme_O2_strip` avec angr en moins de 30 lignes Python](/18-execution-symbolique/checkpoint.md)

### [Chapitre 19 — Anti-reversing et protections compilateur](/19-anti-reversing/README.md)

- 19.1 [Stripping (`strip`) et détection](/19-anti-reversing/01-stripping-detection.md)  
- 19.2 [Packing avec UPX — détecter et décompresser](/19-anti-reversing/02-packing-upx.md)  
- 19.3 [Obfuscation de flux de contrôle (Control Flow Flattening, bogus control flow)](/19-anti-reversing/03-obfuscation-flux-controle.md)  
- 19.4 [Obfuscation via LLVM (Hikari, O-LLVM) — reconnaître les patterns](/19-anti-reversing/04-obfuscation-llvm.md)  
- 19.5 [Stack canaries (`-fstack-protector`), ASLR, PIE, NX](/19-anti-reversing/05-canaries-aslr-pie-nx.md)  
- 19.6 [RELRO : Partial vs Full et impact sur la table GOT/PLT](/19-anti-reversing/06-relro-got-plt.md)  
- 19.7 [Techniques de détection de débogueur (`ptrace`, timing checks, `/proc/self/status`)](/19-anti-reversing/07-detection-debogueur.md)  
- 19.8 [Contre-mesures aux breakpoints (self-modifying code, int3 scanning)](/19-anti-reversing/08-contre-mesures-breakpoints.md)  
- 19.9 [Inspecter l'ensemble des protections avec `checksec` avant toute analyse](/19-anti-reversing/09-checksec-audit-complet.md)  
- [**🎯 Checkpoint** : identifier toutes les protections du binaire `anti_reverse_all_checks`, les contourner une par une](/19-anti-reversing/checkpoint.md)

### [Chapitre 20 — Décompilation et reconstruction du code source](/20-decompilation/README.md)

- 20.1 [Limites de la décompilation automatique (pourquoi le résultat n'est jamais parfait)](/20-decompilation/01-limites-decompilation.md)  
- 20.2 [Ghidra Decompiler — qualité selon le niveau d'optimisation](/20-decompilation/02-ghidra-decompiler.md)  
- 20.3 [RetDec (Avast) — décompilation statique offline](/20-decompilation/03-retdec.md)  
- 20.4 [Reconstruire un fichier `.h` depuis un binaire (types, structs, API)](/20-decompilation/04-reconstruire-header.md)  
- 20.5 [Identifier des bibliothèques tierces embarquées (FLIRT / signatures Ghidra)](/20-decompilation/05-flirt-signatures.md)  
- 20.6 [Exporter et nettoyer le pseudo-code pour produire un code recompilable](/20-decompilation/06-exporter-pseudo-code.md)  
- [**🎯 Checkpoint** : produire un `.h` complet pour le binaire `ch20-network`](/20-decompilation/checkpoint.md)

---

## **[Partie V — Cas Pratiques sur Nos Applications](/partie-5-cas-pratiques.md)**

> 💡 Chaque chapitre mobilise les outils vus en Parties II–IV. Les binaires sont fournis à plusieurs niveaux d'optimisation et avec/sans symboles.

### [Chapitre 21 — Reverse d'un programme C simple (keygenme)](/21-keygenme/README.md)

- 21.1 [Analyse statique complète du binaire (triage, strings, sections)](/21-keygenme/01-analyse-statique.md)  
- 21.2 [Inventaire des protections avec `checksec`](/21-keygenme/02-checksec-protections.md)  
- 21.3 [Localisation de la routine de vérification (approche top-down)](/21-keygenme/03-localisation-routine.md)  
- 21.4 [Comprendre les sauts conditionnels (`jz`/`jnz`) dans le contexte du crackme](/21-keygenme/04-sauts-conditionnels-crackme.md)  
- 21.5 [Analyse dynamique : tracer la comparaison avec GDB](/21-keygenme/05-analyse-dynamique-gdb.md)  
- 21.6 [Patching binaire : inverser un saut directement dans le binaire (avec ImHex)](/21-keygenme/06-patching-imhex.md)  
- 21.7 [Résolution automatique avec angr](/21-keygenme/07-resolution-angr.md)  
- 21.8 [Écriture d'un keygen en Python avec `pwntools`](/21-keygenme/08-keygen-pwntools.md)  
- [**🎯 Checkpoint** : produire un keygen fonctionnel pour les 3 variantes du binaire](/21-keygenme/checkpoint.md)

### [Chapitre 22 — Reverse d'une application C++ orientée objet](/22-oop/README.md)

- 22.1 [Reconstruction de la hiérarchie de classes et des vtables](/22-oop/01-reconstruction-classes-vtables.md)  
- 22.2 [RE d'un système de plugins (chargement dynamique `.so` via `dlopen`/`dlsym`)](/22-oop/02-systeme-plugins-dlopen.md)  
- 22.3 [Comprendre le dispatch virtuel : de la vtable à l'appel de méthode](/22-oop/03-dispatch-virtuel.md)  
- 22.4 [Patcher un comportement via `LD_PRELOAD`](/22-oop/04-patcher-ld-preload.md)  
- [**🎯 Checkpoint** : écrire un plugin `.so` compatible qui s'intègre dans l'application sans les sources](/22-oop/checkpoint.md)

### [Chapitre 23 — Reverse d'un binaire réseau (client/serveur)](/23-network/README.md)

- 23.1 [Identifier le protocole custom avec `strace` + Wireshark](/23-network/01-identifier-protocole.md)  
- 23.2 [RE du parseur de paquets (state machine, champs, magic bytes)](/23-network/02-re-parseur-paquets.md)  
- 23.3 [Visualiser les trames binaires avec ImHex et écrire un `.hexpat` pour le protocole](/23-network/03-trames-imhex-hexpat.md)  
- 23.4 [Replay Attack : rejouer une requête capturée](/23-network/04-replay-attack.md)  
- 23.5 [Écrire un client de remplacement complet avec `pwntools`](/23-network/05-client-pwntools.md)  
- [**🎯 Checkpoint** : écrire un client Python capable de s'authentifier auprès du serveur sans connaître le code source](/23-network/checkpoint.md)

### [Chapitre 24 — Reverse d'un binaire avec chiffrement](/24-crypto/README.md)

- 24.1 [Identifier les routines crypto (constantes magiques : AES S-box, SHA256 IV…)](/24-crypto/01-identifier-routines-crypto.md)  
- 24.2 [Identifier les bibliothèques crypto embarquées (OpenSSL, libsodium, custom)](/24-crypto/02-identifier-libs-crypto.md)  
- 24.3 [Extraire clés et IV depuis la mémoire avec GDB/Frida](/24-crypto/03-extraire-cles-iv.md)  
- 24.4 [Visualiser le format chiffré et les structures avec ImHex](/24-crypto/04-visualiser-format-imhex.md)  
- 24.5 [Reproduire le schéma de chiffrement en Python](/24-crypto/05-reproduire-chiffrement-python.md)  
- [**🎯 Checkpoint** : déchiffrer le fichier `secret.enc` fourni en extrayant la clé du binaire](/24-crypto/checkpoint.md)

### [Chapitre 25 — Reverse d'un format de fichier custom](/25-fileformat/README.md)

- 25.1 [Identifier la structure générale avec `file`, `strings` et `binwalk`](/25-fileformat/01-identifier-structure.md)  
- 25.2 [Cartographier les champs avec ImHex et un pattern `.hexpat` itératif](/25-fileformat/02-cartographier-imhex-hexpat.md)  
- 25.3 [Confirmer l'interprétation avec AFL++ (fuzzing du parser)](/25-fileformat/03-confirmer-afl-fuzzing.md)  
- 25.4 [Écrire un parser/sérialiseur Python indépendant](/25-fileformat/04-parser-python.md)  
- 25.5 [Documenter le format (produire une spécification)](/25-fileformat/05-documenter-specification.md)  
- [**🎯 Checkpoint** : produire un parser Python + un `.hexpat` + une spec du format](/25-fileformat/checkpoint.md)

---

## **[Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)](/partie-6-malware.md)**

> ⚠️ **Tous les samples de cette partie sont créés par nous, à des fins pédagogiques uniquement.** Aucun malware réel n'est distribué.

### [Chapitre 26 — Mise en place d'un lab d'analyse sécurisé](/26-lab-securise/README.md)

- 26.1 [Principes d'isolation : pourquoi et comment](/26-lab-securise/01-principes-isolation.md)  
- 26.2 [VM dédiée avec QEMU/KVM — snapshots et réseau isolé](/26-lab-securise/02-vm-qemu-kvm.md)  
- 26.3 [Outils de monitoring : `auditd`, `inotifywait`, `tcpdump`, `sysdig`](/26-lab-securise/03-outils-monitoring.md)  
- 26.4 [Captures réseau avec un bridge dédié](/26-lab-securise/04-captures-reseau-bridge.md)  
- 26.5 [Règles d'or : ne jamais exécuter hors sandbox, ne jamais connecter au réseau réel](/26-lab-securise/05-regles-or.md)  
- [**🎯 Checkpoint** : déployer le lab et vérifier l'isolation réseau](/26-lab-securise/checkpoint.md)

### [Chapitre 27 — Analyse d'un ransomware Linux ELF (auto-compilé GCC)](/27-ransomware/README.md)

- 27.1 [Conception du sample : chiffrement AES sur `/tmp/test`, clé hardcodée](/27-ransomware/01-conception-sample.md)  
- 27.2 [Triage rapide : `file`, `strings`, `checksec`, premières hypothèses](/27-ransomware/02-triage-rapide.md)  
- 27.3 [Analyse statique : Ghidra + ImHex (repérer les constantes AES, flux de chiffrement)](/27-ransomware/03-analyse-statique-ghidra-imhex.md)  
- 27.4 [Identifier les règles YARA correspondantes depuis ImHex](/27-ransomware/04-regles-yara.md)  
- 27.5 [Analyse dynamique : GDB + Frida (extraire la clé en mémoire)](/27-ransomware/05-analyse-dynamique-gdb-frida.md)  
- 27.6 [Écriture du déchiffreur Python](/27-ransomware/06-dechiffreur-python.md)  
- 27.7 [Rédiger un rapport d'analyse type (IOC, comportement, recommandations)](/27-ransomware/07-rapport-analyse.md)  
- [**🎯 Checkpoint** : déchiffrer les fichiers et produire un rapport complet](/27-ransomware/checkpoint.md)

### [Chapitre 28 — Analyse d'un dropper ELF avec communication réseau](/28-dropper/README.md)

- 28.1 [Identifier les appels réseau avec `strace` + Wireshark](/28-dropper/01-appels-reseau-strace-wireshark.md)  
- 28.2 [Hooker les sockets avec Frida (intercepter `connect`, `send`, `recv`)](/28-dropper/02-hooker-sockets-frida.md)  
- 28.3 [RE du protocole C2 custom (commandes, encodage, handshake)](/28-dropper/03-re-protocole-c2.md)  
- 28.4 [Simuler un serveur C2 pour observer le comportement complet](/28-dropper/04-simuler-serveur-c2.md)  
- [**🎯 Checkpoint** : écrire un faux serveur C2 qui contrôle le dropper](/28-dropper/checkpoint.md)

### [Chapitre 29 — Détection de packing, unpack et reconstruction](/29-unpacking/README.md)

- 29.1 [Identifier UPX et packers custom avec `checksec` + ImHex + entropie](/29-unpacking/01-identifier-packers.md)  
- 29.2 [Unpacking statique (UPX) et dynamique (dump mémoire avec GDB)](/29-unpacking/02-unpacking-statique-dynamique.md)  
- 29.3 [Reconstruire l'ELF original : fixer les headers, sections et entry point](/29-unpacking/03-reconstruire-elf.md)  
- 29.4 [Réanalyser le binaire unpacké](/29-unpacking/04-reanalyser-binaire.md)  
- [**🎯 Checkpoint** : unpacker `ch27-packed`, reconstruire l'ELF et retrouver la logique originale](/29-unpacking/checkpoint.md)

---

## **[Partie VII — Bonus : RE sur Binaires .NET / C#](/partie-7-dotnet.md)**

> 🔗 *Pont direct avec le développement C# — les mêmes concepts de RE s'appliquent sur le bytecode CIL/.NET.*

### [Chapitre 30 — Introduction au RE .NET](/30-introduction-re-dotnet/README.md)

- 30.1 [Différences fondamentales : bytecode CIL vs code natif x86-64](/30-introduction-re-dotnet/01-cil-vs-natif.md)  
- 30.2 [Structure d'un assembly .NET : metadata, PE headers, sections CIL](/30-introduction-re-dotnet/02-structure-assembly-dotnet.md)  
- 30.3 [Obfuscateurs courants : ConfuserEx, Dotfuscator, SmartAssembly](/30-introduction-re-dotnet/03-obfuscateurs-courants.md)  
- 30.4 [Inspecter un assembly avec `file`, `strings` et ImHex (headers PE/.NET)](/30-introduction-re-dotnet/04-inspecter-assembly-imhex.md)  
- 30.5 [NativeAOT et ReadyToRun : quand le C# devient du code natif](/30-introduction-re-dotnet/05-nativeaot-readytorun.md)

### [Chapitre 31 — Décompilation d'assemblies .NET](/31-decompilation-dotnet/README.md)

- 31.1 [ILSpy — décompilation C# open source](/31-decompilation-dotnet/01-ilspy.md)  
- 31.2 [dnSpy / dnSpyEx — décompilation + débogage intégré (breakpoints sur C# décompilé)](/31-decompilation-dotnet/02-dnspy-dnspyex.md)  
- 31.3 [dotPeek (JetBrains) — navigation et export de sources](/31-decompilation-dotnet/03-dotpeek.md)  
- 31.4 [Comparatif ILSpy vs dnSpy vs dotPeek](/31-decompilation-dotnet/04-comparatif-outils.md)  
- 31.5 [Décompiler malgré l'obfuscation : de4dot et techniques de contournement](/31-decompilation-dotnet/05-de4dot-contournement.md)

### [Chapitre 32 — Analyse dynamique et hooking .NET](/32-analyse-dynamique-dotnet/README.md)

- 32.1 [Déboguer un assembly avec dnSpy sans les sources](/32-analyse-dynamique-dotnet/01-debug-dnspy-sans-sources.md)  
- 32.2 [Hooking de méthodes .NET avec Frida (`frida-clr`)](/32-analyse-dynamique-dotnet/02-hooking-frida-clr.md)  
- 32.3 [Intercepter des appels P/Invoke (pont .NET → bibliothèques natives GCC)](/32-analyse-dynamique-dotnet/03-pinvoke-interception.md)  
- 32.4 [Patcher un assembly .NET à la volée (modifier l'IL avec dnSpy)](/32-analyse-dynamique-dotnet/04-patcher-il-dnspy.md)  
- 32.5 [Cas pratique : contourner une vérification de licence C#](/32-analyse-dynamique-dotnet/05-cas-pratique-licence-csharp.md)  
- [**🎯 Checkpoint** : patcher et keygenner l'application .NET fournie](/32-analyse-dynamique-dotnet/checkpoint.md)

---

## **[Partie VIII — Bonus : RE de binaires Rust et Go](/partie-8-rust-go.md)**

> 🦀🐹 *Ces langages utilisent la toolchain GNU (linker) et produisent des binaires ELF natifs. Leur RE présente des défis spécifiques.*

### [Chapitre 33 — Reverse Engineering de binaires Rust](/33-re-rust/README.md)

- 33.1 [Spécificités de compilation Rust avec la toolchain GNU (linking, symboles)](/33-re-rust/01-compilation-toolchain-gnu.md)  
- 33.2 [Name mangling Rust vs C++ : décoder les symboles](/33-re-rust/02-name-mangling-rust.md)  
- 33.3 [Reconnaître les patterns Rust : `Option`, `Result`, `match`, panics](/33-re-rust/03-patterns-option-result-match.md)  
- 33.4 [Strings en Rust : `&str` vs `String` en mémoire (pas de null terminator)](/33-re-rust/04-strings-rust-memoire.md)  
- 33.5 [Bibliothèques embarquées et taille des binaires (tout est linké statiquement)](/33-re-rust/05-bibliotheques-taille-binaires.md)  
- 33.6 [Outils spécifiques : `cargo-bloat`, signatures Ghidra pour la stdlib Rust](/33-re-rust/06-outils-cargo-bloat-ghidra.md)

### [Chapitre 34 — Reverse Engineering de binaires Go](/34-re-go/README.md)

- 34.1 [Spécificités du runtime Go : goroutines, scheduler, GC](/34-re-go/01-runtime-goroutines-gc.md)  
- 34.2 [Convention d'appel Go (stack-based puis register-based depuis Go 1.17)](/34-re-go/02-convention-appel.md)  
- 34.3 [Structures de données Go en mémoire : slices, maps, interfaces, channels](/34-re-go/03-structures-donnees-memoire.md)  
- 34.4 [Récupérer les noms de fonctions : `gopclntab` et `go_parser` pour Ghidra/IDA](/34-re-go/04-gopclntab-go-parser.md)  
- 34.5 [Strings en Go : structure `(ptr, len)` et implications pour `strings`](/34-re-go/05-strings-go-ptr-len.md)  
- 34.6 [Stripped Go binaries : retrouver les symboles via les structures internes](/34-re-go/06-stripped-go-symboles.md)  
- [**🎯 Checkpoint** : analyser un binaire Go strippé, retrouver les fonctions et reconstruire la logique](/34-re-go/checkpoint.md)

---

## **[Partie IX — Ressources & Automatisation](/partie-9-ressources.md)**

### [Chapitre 35 — Automatisation et scripting](/35-automatisation-scripting/README.md)

- 35.1 [Scripts Python avec `pyelftools` et `lief` (parsing et modification d'ELF)](/35-automatisation-scripting/01-pyelftools-lief.md)  
- 35.2 [Automatiser Ghidra en headless mode (analyse batch de N binaires)](/35-automatisation-scripting/02-ghidra-headless-batch.md)  
- 35.3 [Scripting RE avec `pwntools` (interactions, patching, exploitation)](/35-automatisation-scripting/03-scripting-pwntools.md)  
- 35.4 [Écrire des règles YARA pour détecter des patterns dans une collection de binaires](/35-automatisation-scripting/04-regles-yara.md)  
- 35.5 [Intégration dans un pipeline CI/CD pour audit de régression binaire](/35-automatisation-scripting/05-pipeline-ci-cd.md)  
- 35.6 [Construire son propre toolkit RE : organiser ses scripts et snippets](/35-automatisation-scripting/06-construire-toolkit.md)  
- [**🎯 Checkpoint** : écrire un script qui analyse automatiquement un répertoire de binaires et produit un rapport JSON](/35-automatisation-scripting/checkpoint.md)

### [Chapitre 36 — Ressources pour progresser](/36-ressources-progresser/README.md)

- 36.1 [CTF orientés RE : pwnable.kr, crackmes.one, root-me.org, picoCTF, Hack The Box](/36-ressources-progresser/01-ctf-orientes-re.md)  
- 36.2 [Lectures recommandées (livres, papers, blogs)](/36-ressources-progresser/02-lectures-recommandees.md)  
- 36.3 [Communautés et conférences (REcon, DEF CON RE Village, PoC||GTFO, r/ReverseEngineering)](/36-ressources-progresser/03-communautes-conferences.md)  
- 36.4 [Parcours de certification : GREM (SANS), OSED (OffSec)](/36-ressources-progresser/04-certifications.md)  
- 36.5 [Construire son portfolio RE : documenter ses analyses](/36-ressources-progresser/05-construire-portfolio.md)

---

## [Annexes](/annexes/README.md)

- **Annexe A** — [Référence rapide des opcodes x86-64 fréquents en RE](/annexes/annexe-a-opcodes-x86-64.md)  
- **Annexe B** — [Conventions d'appel System V AMD64 ABI (tableau récapitulatif)](/annexes/annexe-b-system-v-abi.md)  
- **Annexe C** — [Cheat sheet GDB / GEF / pwndbg](/annexes/annexe-c-cheatsheet-gdb.md)  
- **Annexe D** — [Cheat sheet Radare2 / Cutter](/annexes/annexe-d-cheatsheet-radare2.md)  
- **Annexe E** — [Cheat sheet ImHex : syntaxe `.hexpat` de référence](/annexes/annexe-e-cheatsheet-imhex.md)  
- **Annexe F** — [Table des sections ELF et leurs rôles](/annexes/annexe-f-sections-elf.md)  
- **Annexe G** — [Comparatif des outils natifs (outil / usage / gratuit / CLI ou GUI)](/annexes/annexe-g-comparatif-outils-natifs.md)  
- **Annexe H** — [Comparatif des outils .NET (ILSpy / dnSpy / dotPeek / de4dot)](/annexes/annexe-h-comparatif-outils-dotnet.md)  
- **Annexe I** — [Patterns GCC reconnaissables à l'assembleur (idiomes compilateur)](/annexes/annexe-i-patterns-gcc.md)  
- **Annexe J** — [Constantes magiques crypto courantes (AES, SHA, MD5, RC4…)](/annexes/annexe-j-constantes-crypto.md)  
- **Annexe K** — [Glossaire du Reverse Engineering](/annexes/annexe-k-glossaire.md)

---

## Structure du dépôt

```
formation-reverse-engineering-gcc-gpp/
│
├── README.md                              ← Ce fichier (présentation + liens)
├── LICENSE                                ← MIT + disclaimer éthique (FR/EN)
├── check_env.sh                           ← Script de vérification de l'environnement
├── preface.md                             ← Préface du tutoriel
│
├── partie-1-fondamentaux.md               ← Introduction Partie I
├── partie-2-analyse-statique.md           ← Introduction Partie II
├── partie-3-analyse-dynamique.md          ← Introduction Partie III
├── partie-4-techniques-avancees.md        ← Introduction Partie IV
├── partie-5-cas-pratiques.md              ← Introduction Partie V
├── partie-6-malware.md                    ← Introduction Partie VI
├── partie-7-dotnet.md                     ← Introduction Partie VII
├── partie-8-rust-go.md                    ← Introduction Partie VIII
├── partie-9-ressources.md                 ← Introduction Partie IX
│
│
│   ════════════════════════════════════════
│   PARTIE I — FONDAMENTAUX & ENVIRONNEMENT
│   ════════════════════════════════════════
│
├── 01-introduction-re/
│   ├── README.md
│   ├── 01-definition-objectifs.md
│   ├── 02-cadre-legal-ethique.md
│   ├── 03-cas-usage-legitimes.md
│   ├── 04-statique-vs-dynamique.md
│   ├── 05-methodologie-outils.md
│   ├── 06-taxonomie-cibles.md
│   └── checkpoint.md
│
├── 02-chaine-compilation-gnu/
│   ├── README.md
│   ├── 01-architecture-gcc.md
│   ├── 02-phases-compilation.md
│   ├── 03-formats-binaires.md
│   ├── 04-sections-elf.md
│   ├── 05-flags-compilation.md
│   ├── 06-symboles-dwarf.md
│   ├── 07-loader-linux.md
│   ├── 08-segments-aslr.md
│   ├── 09-plt-got-lazy-binding.md
│   └── checkpoint.md
│
├── 03-assembleur-x86-64/
│   ├── README.md
│   ├── 01-registres-pointeurs-flags.md
│   ├── 02-instructions-essentielles.md
│   ├── 03-arithmetique-logique.md
│   ├── 04-sauts-conditionnels.md
│   ├── 05-pile-prologue-epilogue.md
│   ├── 06-passage-parametres.md
│   ├── 07-methode-lecture-asm.md
│   ├── 08-call-plt-vs-syscall.md
│   ├── 09-introduction-simd.md
│   └── checkpoint.md
│
├── 04-environnement-travail/
│   ├── README.md
│   ├── 01-distribution-linux.md
│   ├── 02-installation-outils.md
│   ├── 03-creation-vm.md
│   ├── 04-configuration-reseau-vm.md
│   ├── 05-structure-depot.md
│   ├── 06-compiler-binaires.md
│   ├── 07-verifier-installation.md
│   └── checkpoint.md
│
│
│   ════════════════════════════
│   PARTIE II — ANALYSE STATIQUE
│   ════════════════════════════
│
├── 05-outils-inspection-base/
│   ├── README.md
│   ├── 01-file-strings-xxd.md
│   ├── 02-readelf-objdump.md
│   ├── 03-nm-symboles.md
│   ├── 04-ldd-ldconfig.md
│   ├── 05-strace-ltrace.md
│   ├── 06-checksec.md
│   ├── 07-workflow-triage-rapide.md
│   └── checkpoint.md
│
├── 06-imhex/
│   ├── README.md
│   ├── 01-pourquoi-imhex.md
│   ├── 02-installation-interface.md
│   ├── 03-langage-hexpat.md
│   ├── 04-pattern-header-elf.md
│   ├── 05-parser-structure-custom.md
│   ├── 06-colorisation-annotations.md
│   ├── 07-comparaison-diff.md
│   ├── 08-recherche-magic-bytes.md
│   ├── 09-desassembleur-integre.md
│   ├── 10-regles-yara.md
│   ├── 11-cas-pratique-format-custom.md
│   └── checkpoint.md
│
├── 07-objdump-binutils/
│   ├── README.md
│   ├── 01-desassemblage-sans-symboles.md
│   ├── 02-att-vs-intel.md
│   ├── 03-comparaison-optimisations.md
│   ├── 04-prologue-epilogue.md
│   ├── 05-identifier-main-mangling.md
│   ├── 06-cppfilt-demanglement.md
│   ├── 07-limitations-objdump.md
│   └── checkpoint.md
│
├── 08-ghidra/
│   ├── README.md
│   ├── 01-installation-prise-en-main.md
│   ├── 02-import-elf-analyse.md
│   ├── 03-navigation-codebrowser.md
│   ├── 04-renommage-commentaires-types.md
│   ├── 05-structures-gcc-vtables-rtti.md
│   ├── 06-reconstruire-structures.md
│   ├── 07-cross-references-xref.md
│   ├── 08-scripts-java-python.md
│   ├── 09-headless-mode-batch.md
│   └── checkpoint.md
│
├── 09-ida-radare2-binja/
│   ├── README.md
│   ├── 01-ida-free-workflow.md
│   ├── 02-radare2-cutter.md
│   ├── 03-r2-commandes-essentielles.md
│   ├── 04-scripting-r2pipe.md
│   ├── 05-binary-ninja-cloud.md
│   ├── 06-comparatif-outils.md
│   └── checkpoint.md
│
├── 10-diffing-binaires/
│   ├── README.md
│   ├── 01-pourquoi-diffing.md
│   ├── 02-bindiff.md
│   ├── 03-diaphora.md
│   ├── 04-radiff2.md
│   ├── 05-cas-pratique-patch-vuln.md
│   └── checkpoint.md
│
│
│   ═══════════════════════════════
│   PARTIE III — ANALYSE DYNAMIQUE
│   ═══════════════════════════════
│
├── 11-gdb/
│   ├── README.md
│   ├── 01-compilation-symboles-debug.md
│   ├── 02-commandes-fondamentales.md
│   ├── 03-inspecter-pile-registres-memoire.md
│   ├── 04-gdb-binaire-strippe.md
│   ├── 05-breakpoints-conditionnels-watchpoints.md
│   ├── 06-catchpoints.md
│   ├── 07-remote-debugging-gdbserver.md
│   ├── 08-gdb-python-api.md
│   ├── 09-introduction-pwntools.md
│   └── checkpoint.md
│
├── 12-gdb-extensions/
│   ├── README.md
│   ├── 01-installation-comparaison.md
│   ├── 02-visualisation-stack-registres.md
│   ├── 03-recherche-gadgets-rop.md
│   ├── 04-analyse-heap-pwndbg.md
│   ├── 05-commandes-specifiques.md
│   └── checkpoint.md
│
├── 13-frida/
│   ├── README.md
│   ├── 01-architecture-frida.md
│   ├── 02-modes-injection.md
│   ├── 03-hooking-fonctions-c-cpp.md
│   ├── 04-intercepter-appels.md
│   ├── 05-modifier-arguments-retour.md
│   ├── 06-stalker-code-coverage.md
│   ├── 07-cas-pratique-licence.md
│   └── checkpoint.md
│
├── 14-valgrind-sanitizers/
│   ├── README.md
│   ├── 01-valgrind-memcheck.md
│   ├── 02-callgrind-kcachegrind.md
│   ├── 03-asan-ubsan-msan.md
│   ├── 04-exploiter-rapports.md
│   └── checkpoint.md
│
├── 15-fuzzing/
│   ├── README.md
│   ├── 01-pourquoi-fuzzing-re.md
│   ├── 02-afl-plus-plus.md
│   ├── 03-libfuzzer.md
│   ├── 04-analyser-crashs.md
│   ├── 05-coverage-guided.md
│   ├── 06-corpus-dictionnaires.md
│   ├── 07-cas-pratique-parseur.md
│   └── checkpoint.md
│
│
│   ══════════════════════════════════════
│   PARTIE IV — TECHNIQUES AVANCÉES DE RE
│   ══════════════════════════════════════
│
├── 16-optimisations-compilateur/
│   ├── README.md
│   ├── 01-impact-niveaux-optimisation.md
│   ├── 02-inlining.md
│   ├── 03-deroulage-vectorisation.md
│   ├── 04-tail-call-optimization.md
│   ├── 05-link-time-optimization.md
│   ├── 06-patterns-idiomes-gcc.md
│   ├── 07-gcc-vs-clang.md
│   └── checkpoint.md
│
├── 17-re-cpp-gcc/
│   ├── README.md
│   ├── 01-name-mangling-itanium.md
│   ├── 02-modele-objet-vtable.md
│   ├── 03-rtti-dynamic-cast.md
│   ├── 04-gestion-exceptions.md
│   ├── 05-stl-internals.md
│   ├── 06-templates-instanciations.md
│   ├── 07-lambda-closures.md
│   ├── 08-smart-pointers.md
│   ├── 09-coroutines-cpp20.md
│   └── checkpoint.md
│
├── 18-execution-symbolique/
│   ├── README.md
│   ├── 01-principes-execution-symbolique.md
│   ├── 02-angr-installation-architecture.md
│   ├── 03-resoudre-crackme-angr.md
│   ├── 04-z3-theorem-prover.md
│   ├── 05-limites-explosion-chemins.md
│   ├── 06-combinaison-re-manuel.md
│   └── checkpoint.md
│
├── 19-anti-reversing/
│   ├── README.md
│   ├── 01-stripping-detection.md
│   ├── 02-packing-upx.md
│   ├── 03-obfuscation-flux-controle.md
│   ├── 04-obfuscation-llvm.md
│   ├── 05-canaries-aslr-pie-nx.md
│   ├── 06-relro-got-plt.md
│   ├── 07-detection-debogueur.md
│   ├── 08-contre-mesures-breakpoints.md
│   ├── 09-checksec-audit-complet.md
│   └── checkpoint.md
│
├── 20-decompilation/
│   ├── README.md
│   ├── 01-limites-decompilation.md
│   ├── 02-ghidra-decompiler.md
│   ├── 03-retdec.md
│   ├── 04-reconstruire-header.md
│   ├── 05-flirt-signatures.md
│   ├── 06-exporter-pseudo-code.md
│   └── checkpoint.md
│
│
│   ══════════════════════════════════════════════
│   PARTIE V — CAS PRATIQUES SUR NOS APPLICATIONS
│   ══════════════════════════════════════════════
│
├── 21-keygenme/
│   ├── README.md
│   ├── 01-analyse-statique.md
│   ├── 02-checksec-protections.md
│   ├── 03-localisation-routine.md
│   ├── 04-sauts-conditionnels-crackme.md
│   ├── 05-analyse-dynamique-gdb.md
│   ├── 06-patching-imhex.md
│   ├── 07-resolution-angr.md
│   ├── 08-keygen-pwntools.md
│   └── checkpoint.md
│
├── 22-oop/
│   ├── README.md
│   ├── 01-reconstruction-classes-vtables.md
│   ├── 02-systeme-plugins-dlopen.md
│   ├── 03-dispatch-virtuel.md
│   ├── 04-patcher-ld-preload.md
│   └── checkpoint.md
│
├── 23-network/
│   ├── README.md
│   ├── 01-identifier-protocole.md
│   ├── 02-re-parseur-paquets.md
│   ├── 03-trames-imhex-hexpat.md
│   ├── 04-replay-attack.md
│   ├── 05-client-pwntools.md
│   └── checkpoint.md
│
├── 24-crypto/
│   ├── README.md
│   ├── 01-identifier-routines-crypto.md
│   ├── 02-identifier-libs-crypto.md
│   ├── 03-extraire-cles-iv.md
│   ├── 04-visualiser-format-imhex.md
│   ├── 05-reproduire-chiffrement-python.md
│   └── checkpoint.md
│
├── 25-fileformat/
│   ├── README.md
│   ├── 01-identifier-structure.md
│   ├── 02-cartographier-imhex-hexpat.md
│   ├── 03-confirmer-afl-fuzzing.md
│   ├── 04-parser-python.md
│   ├── 05-documenter-specification.md
│   └── checkpoint.md
│
│
│   ══════════════════════════════════════════════════════════
│   PARTIE VI — ANALYSE DE CODE MALVEILLANT (ENV. CONTRÔLÉ)
│   ══════════════════════════════════════════════════════════
│
├── 26-lab-securise/
│   ├── README.md
│   ├── 01-principes-isolation.md
│   ├── 02-vm-qemu-kvm.md
│   ├── 03-outils-monitoring.md
│   ├── 04-captures-reseau-bridge.md
│   ├── 05-regles-or.md
│   └── checkpoint.md
│
├── 27-ransomware/
│   ├── README.md
│   ├── 01-conception-sample.md
│   ├── 02-triage-rapide.md
│   ├── 03-analyse-statique-ghidra-imhex.md
│   ├── 04-regles-yara.md
│   ├── 05-analyse-dynamique-gdb-frida.md
│   ├── 06-dechiffreur-python.md
│   ├── 07-rapport-analyse.md
│   └── checkpoint.md
│
├── 28-dropper/
│   ├── README.md
│   ├── 01-appels-reseau-strace-wireshark.md
│   ├── 02-hooker-sockets-frida.md
│   ├── 03-re-protocole-c2.md
│   ├── 04-simuler-serveur-c2.md
│   └── checkpoint.md
│
├── 29-unpacking/
│   ├── README.md
│   ├── 01-identifier-packers.md
│   ├── 02-unpacking-statique-dynamique.md
│   ├── 03-reconstruire-elf.md
│   ├── 04-reanalyser-binaire.md
│   └── checkpoint.md
│
│
│   ══════════════════════════════════════════
│   PARTIE VII — BONUS : RE BINAIRES .NET/C#
│   ══════════════════════════════════════════
│
├── 30-introduction-re-dotnet/
│   ├── README.md
│   ├── 01-cil-vs-natif.md
│   ├── 02-structure-assembly-dotnet.md
│   ├── 03-obfuscateurs-courants.md
│   ├── 04-inspecter-assembly-imhex.md
│   └── 05-nativeaot-readytorun.md
│
├── 31-decompilation-dotnet/
│   ├── README.md
│   ├── 01-ilspy.md
│   ├── 02-dnspy-dnspyex.md
│   ├── 03-dotpeek.md
│   ├── 04-comparatif-outils.md
│   └── 05-de4dot-contournement.md
│
├── 32-analyse-dynamique-dotnet/
│   ├── README.md
│   ├── 01-debug-dnspy-sans-sources.md
│   ├── 02-hooking-frida-clr.md
│   ├── 03-pinvoke-interception.md
│   ├── 04-patcher-il-dnspy.md
│   ├── 05-cas-pratique-licence-csharp.md
│   └── checkpoint.md
│
│
│   ═══════════════════════════════════════════════
│   PARTIE VIII — BONUS : RE BINAIRES RUST ET GO
│   ═══════════════════════════════════════════════
│
├── 33-re-rust/
│   ├── README.md
│   ├── 01-compilation-toolchain-gnu.md
│   ├── 02-name-mangling-rust.md
│   ├── 03-patterns-option-result-match.md
│   ├── 04-strings-rust-memoire.md
│   ├── 05-bibliotheques-taille-binaires.md
│   └── 06-outils-cargo-bloat-ghidra.md
│
├── 34-re-go/
│   ├── README.md
│   ├── 01-runtime-goroutines-gc.md
│   ├── 02-convention-appel.md
│   ├── 03-structures-donnees-memoire.md
│   ├── 04-gopclntab-go-parser.md
│   ├── 05-strings-go-ptr-len.md
│   ├── 06-stripped-go-symboles.md
│   └── checkpoint.md
│
│
│   ═══════════════════════════════════════════
│   PARTIE IX — RESSOURCES & AUTOMATISATION
│   ═══════════════════════════════════════════
│
├── 35-automatisation-scripting/
│   ├── README.md
│   ├── 01-pyelftools-lief.md
│   ├── 02-ghidra-headless-batch.md
│   ├── 03-scripting-pwntools.md
│   ├── 04-regles-yara.md
│   ├── 05-pipeline-ci-cd.md
│   ├── 06-construire-toolkit.md
│   └── checkpoint.md
│
├── 36-ressources-progresser/
│   ├── README.md
│   ├── 01-ctf-orientes-re.md
│   ├── 02-lectures-recommandees.md
│   ├── 03-communautes-conferences.md
│   ├── 04-certifications.md
│   └── 05-construire-portfolio.md
│
│
│   ══════════
│   ANNEXES
│   ══════════
│
├── annexes/
│   ├── README.md
│   ├── annexe-a-opcodes-x86-64.md
│   ├── annexe-b-system-v-abi.md
│   ├── annexe-c-cheatsheet-gdb.md
│   ├── annexe-d-cheatsheet-radare2.md
│   ├── annexe-e-cheatsheet-imhex.md
│   ├── annexe-f-sections-elf.md
│   ├── annexe-g-comparatif-outils-natifs.md
│   ├── annexe-h-comparatif-outils-dotnet.md
│   ├── annexe-i-patterns-gcc.md
│   ├── annexe-j-constantes-crypto.md
│   └── annexe-k-glossaire.md
│
│
│   ═══════════════════════════════════════════
│   BINAIRES D'ENTRAÎNEMENT & RESSOURCES
│   ═══════════════════════════════════════════
│
├── binaries/                              ← Tous les binaires d'entraînement
│   ├── Makefile                           ← `make all` pour tout recompiler
│   ├── ch02-hello/
│   │   ├── hello.c
│   │   └── Makefile
│   ├── ch03-checkpoint/
│   │   ├── count_lowercase.c
│   │   └── Makefile
│   ├── ch05-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch05-mystery_bin/
│   │   ├── mystery_bin.c
│   │   └── Makefile
│   ├── ch06-fileformat/
│   │   ├── fileformat.c
│   │   └── Makefile
│   ├── ch07-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch08-oop/
│   │   ├── oop.cpp
│   │   └── Makefile
│   ├── ch09-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch10-keygenme/
│   │   ├── keygenme_v1.c
│   │   ├── keygenme_v2.c
│   │   └── Makefile
│   ├── ch11-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch12-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch13-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch13-network/
│   │   ├── client.c
│   │   ├── server.c
│   │   ├── protocol.h
│   │   └── Makefile
│   ├── ch14-crypto/
│   │   ├── crypto.c
│   │   └── Makefile
│   ├── ch14-fileformat/
│   │   ├── fileformat.c
│   │   └── Makefile
│   ├── ch14-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch15-fileformat/
│   │   ├── fileformat.c
│   │   ├── fuzz_fileformat.c
│   │   └── Makefile
│   ├── ch15-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch16-optimisations/
│   │   ├── gcc_idioms.c
│   │   ├── inlining_demo.c
│   │   ├── loop_unroll_vec.c
│   │   ├── lto_main.c
│   │   ├── lto_math.c
│   │   ├── lto_math.h
│   │   ├── lto_utils.c
│   │   ├── lto_utils.h
│   │   ├── opt_levels_demo.c
│   │   ├── tail_call.c
│   │   └── Makefile
│   ├── ch17-oop/
│   │   ├── oop.cpp
│   │   └── Makefile
│   ├── ch18-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch19-anti-reversing/
│   │   ├── anti_reverse.c
│   │   ├── vuln_demo.c
│   │   └── Makefile
│   ├── ch20-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch20-network/
│   │   ├── client.c
│   │   ├── server.c
│   │   ├── protocol.h
│   │   └── Makefile
│   ├── ch20-oop/
│   │   ├── oop.cpp
│   │   └── Makefile
│   ├── ch21-keygenme/
│   │   ├── keygenme.c
│   │   └── Makefile
│   ├── ch22-oop/
│   │   ├── oop.cpp
│   │   ├── plugin_alpha.cpp
│   │   ├── plugin_beta.cpp
│   │   ├── processor.h
│   │   └── Makefile
│   ├── ch23-network/
│   │   ├── client.c
│   │   ├── server.c
│   │   └── Makefile
│   ├── ch24-crypto/
│   │   ├── crypto.c
│   │   └── Makefile
│   ├── ch25-fileformat/
│   │   ├── fileformat.c
│   │   └── Makefile
│   ├── ch27-ransomware/                   ← ⚠️ Sandbox uniquement
│   │   ├── ransomware_sample.c
│   │   └── Makefile
│   ├── ch28-dropper/                      ← ⚠️ Sandbox uniquement
│   │   ├── dropper_sample.c
│   │   └── Makefile
│   ├── ch29-packed/
│   │   ├── packed_sample.c
│   │   └── Makefile
│   ├── ch32-dotnet/
│   │   ├── LicenseChecker/
│   │   │   ├── LicenseChecker.csproj
│   │   │   ├── LicenseValidator.cs
│   │   │   ├── NativeBridge.cs
│   │   │   └── Program.cs
│   │   ├── native/
│   │   │   └── native_check.c
│   │   └── Makefile
│   ├── ch33-rust/
│   │   ├── crackme_rust/
│   │   │   ├── src/
│   │   │   │   └── main.rs
│   │   │   └── Cargo.toml
│   │   └── Makefile
│   └── ch34-go/
│       ├── crackme_go/
│       │   ├── main.go
│       │   └── go.mod
│       └── Makefile
│
├── scripts/                               ← Scripts Python utilitaires
│   ├── triage.py                          ← Triage automatique d'un binaire
│   ├── keygen_template.py                 ← Template keygen pwntools
│   └── batch_analyze.py                   ← Analyse batch Ghidra headless
│
├── hexpat/                                ← Patterns ImHex (.hexpat)
│   ├── elf_header.hexpat
│   ├── ch25_fileformat.hexpat
│   └── ch23_protocol.hexpat
│
├── yara-rules/                            ← Règles YARA du tuto
│   ├── crypto_constants.yar
│   └── packer_signatures.yar
│
└── solutions/                             ← Corrigés des checkpoints (⚠️ spoilers)
    ├── ch01-checkpoint-solution.md
    ├── ch02-checkpoint-solution.md
    ├── ch03-checkpoint-solution.md
    ├── ch04-checkpoint-solution.md
    ├── ch05-checkpoint-solution.md
    ├── ch06-checkpoint-solution.hexpat
    ├── ch07-checkpoint-solution.md
    ├── ch08-checkpoint-solution.md
    ├── ch09-checkpoint-solution.md
    ├── ch10-checkpoint-solution.md
    ├── ch11-checkpoint-solution.py
    ├── ch12-checkpoint-solution.md
    ├── ch13-checkpoint-solution.js
    ├── ch13-checkpoint-solution.py
    ├── ch14-checkpoint-solution.md
    ├── ch15-checkpoint-solution.md
    ├── ch16-checkpoint-solution.md
    ├── ch17-checkpoint-solution.md
    ├── ch18-checkpoint-solution.py
    ├── ch19-checkpoint-solution.md
    ├── ch20-checkpoint-solution.h
    ├── ch21-checkpoint-keygen.py
    ├── ch22-checkpoint-plugin.cpp
    ├── ch23-checkpoint-client.py
    ├── ch24-checkpoint-decrypt.py
    ├── ch25-checkpoint-parser.py
    ├── ch25-checkpoint-solution.hexpat
    ├── ch26-checkpoint-solution.md
    ├── ch27-checkpoint-decryptor.py
    ├── ch27-checkpoint-solution.md
    ├── ch28-checkpoint-fake-c2.py
    ├── ch29-checkpoint-solution.md
    ├── ch32-checkpoint-solution.md
    ├── ch34-checkpoint-solution.md
    └── ch35-checkpoint-batch.py
``` 
