🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe K — Glossaire du Reverse Engineering

> 📎 **Fiche de référence** — Ce glossaire définit les termes techniques utilisés dans l'ensemble de la formation. Chaque entrée renvoie vers le chapitre où le concept est introduit pour la première fois. Les termes sont classés par ordre alphabétique. Les acronymes sont listés sous leur forme abrégée, avec le développement complet dans la définition.

---

## A

**ABI** (*Application Binary Interface*) — Ensemble de conventions qui régissent l'interface binaire entre un programme compilé et le système d'exploitation ou les bibliothèques : conventions d'appel, layout des structures, tailles des types, gestion des exceptions. Sur Linux x86-64, l'ABI de référence est System V AMD64 ABI. *(Chapitres 2, 3 — Annexe B)*

**Adresse virtuelle** (*Virtual Address*, VA) — Adresse vue par le processus en cours d'exécution, par opposition à l'adresse physique en RAM. Chaque processus dispose de son propre espace d'adresses virtuelles, géré par la MMU du processeur. *(Chapitre 2.8)*

**AFL++** (*American Fuzzy Lop plus plus*) — Fuzzer coverage-guided qui génère automatiquement des entrées pour découvrir des crashs et des chemins d'exécution dans un binaire. Supporte l'instrumentation à la compilation et le mode QEMU pour les binaires sans source. *(Chapitre 15.2)*

**Analyse dynamique** — Technique de RE qui consiste à exécuter le programme cible et à observer son comportement en temps réel : mémoire, registres, appels système, trafic réseau. Complémentaire de l'analyse statique. *(Chapitre 1.4)*

**Analyse statique** — Technique de RE qui consiste à examiner le binaire sans l'exécuter : désassemblage, décompilation, inspection des sections, des chaînes et des symboles. *(Chapitre 1.4)*

**angr** — Framework Python d'exécution symbolique capable d'explorer automatiquement les chemins d'exécution d'un binaire et de résoudre des contraintes sur les entrées. Utilise Z3 en interne. *(Chapitre 18.2)*

**Anti-debugging** — Ensemble de techniques utilisées par un programme pour détecter la présence d'un débogueur et modifier son comportement en conséquence (crash, faux résultats, boucle infinie). Exemples : vérification de `ptrace`, timing checks avec `rdtsc`, lecture de `/proc/self/status`. *(Chapitre 19.7)*

**ASan** (*AddressSanitizer*) — Instrumentation de compilation (GCC/Clang, `-fsanitize=address`) qui détecte les erreurs mémoire à l'exécution : buffer overflows, use-after-free, double-free, memory leaks. *(Chapitre 14.3)*

**ASLR** (*Address Space Layout Randomization*) — Protection du système d'exploitation qui randomise les adresses de base de la pile, du heap, des bibliothèques partagées et (en PIE) du binaire lui-même à chaque exécution, rendant l'exploitation de vulnérabilités plus difficile. *(Chapitre 2.8)*

**Assembly** (.NET) — Dans l'écosystème .NET, unité de déploiement et de versioning contenant du bytecode CIL, des métadonnées et des ressources. Correspond à un fichier `.exe` ou `.dll` .NET. Ne pas confondre avec le code assembleur x86. *(Chapitre 30.2)*

---

## B

**Base address** — Adresse à laquelle un binaire ou une bibliothèque est chargé en mémoire. En mode PIE/ASLR, cette adresse est randomisée à chaque exécution. L'adresse de base par défaut pour un exécutable ELF non-PIE est typiquement `0x400000`. *(Chapitre 2.8)*

**Basic block** (*bloc de base*) — Séquence d'instructions contiguës sans branchement interne : l'exécution entre par le début et sort par la fin (un saut, un appel ou un retour). Les graphes de flux de contrôle (CFG) sont construits à partir de basic blocks. *(Chapitre 8.3)*

**Binutils** — Collection d'outils GNU pour la manipulation de fichiers binaires : `as` (assembleur), `ld` (linker), `objdump`, `readelf`, `nm`, `objcopy`, `strip`, `c++filt`, `ar`. *(Chapitres 5, 7)*

**Bloc de base** — Voir *Basic block*.

**Breakpoint** — Point d'arrêt placé dans un programme débogué. Le débogueur remplace temporairement l'instruction à l'adresse ciblée par l'opcode `int 3` (`0xCC`), ce qui provoque un trap quand l'exécution atteint ce point. Peut être logiciel (mémoire) ou matériel (registres DR0–DR3). *(Chapitre 11.2)*

**BSS** (*Block Started by Symbol*) — Section ELF (`.bss`) contenant les variables globales et statiques initialisées à zéro. N'occupe pas d'espace dans le fichier binaire — l'espace est alloué et mis à zéro par le loader au chargement. *(Chapitre 2.4 — Annexe F)*

---

## C

**Callee-saved register** (*registre préservé par l'appelé*) — Registre dont la valeur doit être restaurée par une fonction avant de retourner à son appelant. Sur System V AMD64 : `rbx`, `rbp`, `rsp`, `r12`–`r15`. Aussi appelé *non-volatile register*. *(Chapitre 3.5 — Annexe B)*

**Caller-saved register** (*registre non préservé par l'appelant*) — Registre qui peut être librement écrasé par une fonction appelée. L'appelant doit sauvegarder sa valeur s'il en a encore besoin après le `call`. Sur System V AMD64 : `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11`. Aussi appelé *volatile register*. *(Chapitre 3.5 — Annexe B)*

**Canary** (*stack canary*, *stack protector*) — Valeur sentinelle placée sur la pile entre les variables locales et l'adresse de retour pour détecter les buffer overflows. Sur GCC x86-64 avec glibc, le canary est lu depuis `fs:[0x28]` (TLS). Activé par `-fstack-protector`. *(Chapitre 19.5 — Annexe I, §10)*

**Catchpoint** — Dans GDB, point d'arrêt déclenché par un événement système spécifique (appel système, fork, exec, chargement de bibliothèque, signal, exception C++) plutôt que par l'atteinte d'une adresse. *(Chapitre 11.6)*

**CFG** (*Control Flow Graph*) — Graphe orienté représentant le flux d'exécution d'une fonction : les nœuds sont les blocs de base, les arêtes sont les sauts et les fall-throughs. Affiché par Ghidra (Function Graph), Radare2 (`VV`) et Cutter. *(Chapitre 8.3)*

**CIL** (*Common Intermediate Language*) — Bytecode de la plateforme .NET dans lequel sont compilés les langages C#, F# et VB.NET. Équivalent du code machine pour le runtime .NET (CLR). Le CIL est compilé en code natif par le JIT au moment de l'exécution. *(Chapitre 30.1)*

**COMDAT** — Mécanisme ELF permettant au linker de fusionner des sections dupliquées (typiquement les instanciations de templates C++) : seule une copie est conservée dans le binaire final. *(Chapitre 17.6)*

**Control Flow Flattening** — Technique d'obfuscation qui transforme le flux de contrôle naturel d'une fonction (if/else, boucles) en un grand `switch` dans une boucle, rendant le CFG illisible. Chaque bloc de base original devient un `case` du switch. *(Chapitre 19.3)*

**Convention d'appel** (*calling convention*) — Ensemble de règles définissant comment les arguments sont passés à une fonction, comment la valeur de retour est transmise, et quels registres doivent être préservés. Sur Linux x86-64 : System V AMD64 ABI. *(Chapitre 3.5 — Annexe B)*

**Core dump** — Fichier produit par le système d'exploitation quand un processus crash (SIGSEGV, SIGABRT, etc.), contenant l'image mémoire complète du processus au moment du crash. Analysable avec GDB (`gdb -c core ./binary`). *(Chapitre 11)*

**Coverage** (*code coverage*) — Mesure de la proportion du code d'un programme qui a été exécutée pendant un test ou un fuzzing. Le coverage-guided fuzzing utilise cette mesure pour guider la génération de nouvelles entrées vers les chemins non encore explorés. *(Chapitre 15.5)*

**Crackme** — Programme conçu comme un défi de reverse engineering : l'objectif est de trouver un mot de passe, une clé de série ou de contourner une vérification. Utilisé à des fins éducatives et dans les CTF. *(Chapitre 21)*

**Cross-reference** (*XREF*) — Lien dans l'analyse statique indiquant qu'une adresse (fonction, variable, chaîne) est référencée depuis une autre adresse. Deux types : *xref to* (qui référence cette adresse) et *xref from* (que référence cette adresse). Outil fondamental de navigation dans le désassemblage. *(Chapitre 8.7)*

**CTF** (*Capture The Flag*) — Compétition de sécurité informatique où les participants résolvent des défis (RE, exploitation, crypto, web, forensics) pour obtenir des « flags » (chaînes de validation). *(Chapitre 1.3)*

---

## D

**de4dot** — Déobfuscateur open source pour les assemblies .NET. Détecte automatiquement l'obfuscateur utilisé et applique les transformations inverses : restauration des noms, déchiffrement des chaînes, simplification du flux de contrôle. *(Chapitre 31.5 — Annexe H)*

**Décompilateur** — Outil qui transforme du code machine (ou du bytecode) en pseudo-code dans un langage de haut niveau (C, C#). Exemples : décompilateur Ghidra, Hex-Rays (IDA), ILSpy (.NET). Le résultat est une approximation du code source original, jamais une reconstitution exacte pour les binaires natifs. *(Chapitre 20)*

**Démangler** (*demangle*) — Opération de décodage des noms de symboles C++ transformés par le *name mangling*. L'outil `c++filt` (GNU) convertit par exemple `_ZN5MyApp4mainEi` en `MyApp::main(int)`. *(Chapitre 7.6)*

**Désassembleur** — Outil qui transforme le code machine binaire en instructions assembleur lisibles par un humain. Deux approches : désassemblage linéaire (parcourt séquentiellement) et désassemblage récursif (suit les branchements). Exemples : `objdump`, Ghidra, IDA, Radare2. *(Chapitres 7, 8, 9)*

**Diffing** (*binary diffing*) — Technique de comparaison de deux versions d'un même binaire pour identifier les fonctions modifiées, ajoutées ou supprimées. Utilisé pour l'analyse de correctifs de sécurité (patch diffing). Outils : BinDiff, Diaphora, `radiff2`. *(Chapitre 10)*

**dnSpy** / **dnSpyEx** — Décompilateur, débogueur et éditeur .NET tout-en-un. Permet de poser des breakpoints sur du code C# décompilé, d'éditer le code IL ou C# directement et de sauvegarder les modifications. dnSpyEx est le fork communautaire activement maintenu. *(Chapitre 31.2 — Annexe H)*

**Dropper** — Programme malveillant dont la fonction est de déposer (écrire sur le disque) et d'exécuter un second programme malveillant (le payload). Peut télécharger le payload depuis un serveur C2 ou le contenir chiffré dans ses propres données. *(Chapitre 28)*

**DWARF** — Format standard de données de débogage embarquées dans les binaires ELF (sections `.debug_*`). Contient la correspondance instructions ↔ lignes source, les types des variables, les scopes et les informations de déroulage de pile. Généré par `-g`. *(Chapitre 2.6)*

**Dynamic linking** (*liaison dynamique*) — Mécanisme par lequel un binaire résout ses dépendances vers des bibliothèques partagées (`.so`) au moment du chargement ou de l'exécution, plutôt qu'à la compilation. Géré par le loader `ld.so`. *(Chapitre 2.7)*

---

## E

**ELF** (*Executable and Linkable Format*) — Format de fichier binaire standard sur Linux et la plupart des systèmes Unix. Contient du code machine, des données et des métadonnées organisés en sections (vue du linker) et segments (vue du loader). *(Chapitre 2.3 — Annexe F)*

**Endianness** — Ordre dans lequel les octets d'une valeur multi-octets sont stockés en mémoire. **Little-endian** (x86, ARM en mode LE) : l'octet de poids faible est à l'adresse la plus basse. **Big-endian** (réseau, SPARC, PowerPC) : l'octet de poids fort est à l'adresse la plus basse. *(Chapitre 2.3)*

**Entry point** (*point d'entrée*) — Adresse de la première instruction exécutée par un programme après le chargement. Sur un ELF, le champ `e_entry` du header pointe typiquement vers `_start` (et non vers `main`). `_start` appelle `__libc_start_main` qui à son tour appelle `main`. *(Chapitre 2.4)*

**Épilogue** (*function epilogue*) — Séquence d'instructions en fin de fonction qui restaure les registres sauvegardés, libère l'espace de pile et retourne à l'appelant. Forme typique : `add rsp, N` / `pop` registres / `ret`. Avec frame pointer : `leave` / `ret`. *(Chapitre 3.5 — Annexe I, §9)*

**Exécution symbolique** — Technique d'analyse qui exécute un programme en traitant les entrées comme des variables symboliques (inconnues) plutôt que des valeurs concrètes. Permet d'explorer tous les chemins d'exécution et de résoudre les contraintes sur les entrées pour atteindre un point précis du programme. *(Chapitre 18.1)*

---

## F

**Fall-through** — Dans le désassemblage, l'exécution séquentielle qui « tombe » de l'instruction courante à la suivante sans saut. Quand un saut conditionnel n'est pas pris, l'exécution continue en fall-through. Dans une structure `if`/`else`, le fall-through correspond typiquement au bloc `then`. *(Chapitre 3.4 — Annexe I, §5)*

**Flag register** — Voir *RFLAGS*.

**FLIRT** (*Fast Library Identification and Recognition Technology*) — Technologie IDA qui identifie les fonctions de bibliothèques standard (libc, libstdc++, etc.) dans un binaire strippé en comparant les octets de code à une base de signatures. Ghidra offre une fonctionnalité similaire (*Function ID*). *(Chapitre 20.5)*

**Frame pointer** — Registre (`rbp` sur x86-64) qui pointe vers la base du frame de pile de la fonction courante, fournissant un point de référence fixe pour accéder aux variables locales et aux arguments. Omis par défaut en `-O1` et au-delà (`-fomit-frame-pointer`). *(Chapitre 3.5 — Annexe B)*

**Frida** — Framework d'instrumentation dynamique multi-plateforme. Permet d'injecter du code JavaScript dans un processus en cours d'exécution pour hooker des fonctions, modifier des arguments et des valeurs de retour, et tracer l'exécution. *(Chapitre 13)*

**Full RELRO** — Mode de protection où la totalité de la GOT (y compris `.got.plt`) est remappée en lecture seule après la résolution initiale de tous les symboles. Activé par `-z now`. Empêche les attaques de type GOT overwrite mais impose la résolution de tous les symboles au démarrage (pas de lazy binding). *(Chapitre 19.6 — Annexe F)*

**Fuzzing** — Technique de test automatisé qui génère des entrées semi-aléatoires pour un programme afin de découvrir des crashs, des erreurs mémoire et des comportements inattendus. En RE, le fuzzing aide à comprendre la logique de parsing d'un binaire en révélant les chemins d'exécution déclenchés par différentes entrées. *(Chapitre 15)*

---

## G

**GDB** (*GNU Debugger*) — Débogueur en ligne de commande de référence sous Linux. Permet de contrôler l'exécution d'un programme pas à pas, d'inspecter la mémoire, les registres et la pile, et de scripter l'analyse via Python. *(Chapitre 11 — Annexe C)*

**GEF** (*GDB Enhanced Features*) — Extension mono-fichier pour GDB ajoutant un affichage contextuel enrichi, des commandes de recherche mémoire, d'analyse de heap et de recherche de gadgets ROP. *(Chapitre 12 — Annexe C)*

**Ghidra** — Suite de reverse engineering open source développée par la NSA. Inclut un désassembleur, un décompilateur, un éditeur de types, un système de scripts Java/Python et un mode headless pour l'analyse batch. *(Chapitre 8)*

**GOT** (*Global Offset Table*) — Table en mémoire contenant les adresses résolues des fonctions importées et des variables globales. Remplie par le loader dynamique (`ld.so`) au chargement ou au premier appel (lazy binding). Cible classique des attaques d'exploitation (GOT overwrite). *(Chapitre 2.9 — Annexe F)*

**GOT overwrite** — Technique d'exploitation qui consiste à écrire une adresse contrôlée par l'attaquant dans une entrée de la GOT pour détourner un appel de fonction. Bloquée par Full RELRO. *(Chapitre 19.6)*

---

## H

**Headless mode** — Mode d'exécution d'un outil sans interface graphique, pilotable par script. Le headless mode de Ghidra (`analyzeHeadless`) permet l'analyse automatisée de binaires en batch. *(Chapitre 8.9)*

**Heap** — Zone de mémoire allouée dynamiquement par `malloc`/`new` pendant l'exécution d'un programme. Sur Linux, le heap est géré par l'allocateur glibc (ptmalloc2). Son analyse est essentielle pour comprendre le comportement runtime d'un programme. *(Chapitre 12.4)*

**Hidden pointer** (*pointeur caché*) — Mécanisme de l'ABI System V AMD64 pour le retour de structures de plus de 16 octets : l'appelant alloue l'espace et passe un pointeur vers cet espace en premier argument (`rdi`), décalant tous les arguments visibles d'un cran. *(Annexe B, §4.4 — Annexe I, §23)*

**Hooking** — Technique qui intercepte l'appel à une fonction pour exécuter du code personnalisé avant, après ou à la place de la fonction originale. Implémentable via Frida (runtime), `LD_PRELOAD` (chargement), patching de la PLT/GOT ou réécriture d'instructions. *(Chapitre 13.3)*

---

## I

**IDA** (*Interactive DisAssembler*) — Désassembleur interactif commercial de référence (Hex-Rays). IDA Free est la version gratuite limitée. IDA Pro inclut le décompilateur Hex-Rays, le support multi-architecture et un SDK de plugins. *(Chapitre 9.1)*

**ILSpy** — Décompilateur .NET open source qui reconstruit du code C# à partir du bytecode CIL. Multi-plateforme (Windows, Linux, macOS via Avalonia). Son moteur de décompilation (ICSharpCode.Decompiler) est aussi disponible comme bibliothèque NuGet. *(Chapitre 31.1 — Annexe H)*

**ImHex** — Éditeur hexadécimal avancé avec langage de patterns (`.hexpat`), visualisation structurée, bookmarks, diff binaire et intégration YARA. Outil central de la formation pour la cartographie de formats binaires. *(Chapitre 6 — Annexe E)*

**Inlining** (*function inlining*) — Optimisation du compilateur qui remplace un appel de fonction par le corps de la fonction directement au site d'appel, éliminant le coût du `call`/`ret`. Rend le RE plus difficile car la fonction « disparaît » du binaire en tant qu'entité séparée. *(Chapitre 16.2)*

**Instrumentation** — Modification d'un programme pour y ajouter du code d'observation (logging, comptage, traçage) sans altérer sa logique fonctionnelle. Peut être statique (à la compilation : ASan, AFL++) ou dynamique (à l'exécution : Frida, Valgrind). *(Chapitres 13, 14, 15)*

**IOC** (*Indicator of Compromise*) — Élément technique permettant de détecter la présence d'un malware ou d'une compromission : hash de fichier, adresse IP de serveur C2, chaîne caractéristique, nom de mutex, clé de registre. *(Chapitre 27.7)*

**Itanium ABI** — Standard de name mangling C++ utilisé par GCC, Clang et la plupart des compilateurs non-MSVC. Définit comment les noms de fonctions C++ (avec namespaces, paramètres, templates) sont encodés en symboles binaires. *(Chapitre 17.1)*

---

## J

**JIT** (*Just-In-Time compilation*) — Technique de compilation qui convertit le bytecode (CIL, Java bytecode) en code machine natif au moment de l'exécution plutôt qu'à l'avance. Utilisée par le runtime .NET (CLR) et la JVM. *(Chapitre 30.1)*

**Jump table** — Table d'adresses (ou d'offsets relatifs) stockée dans `.rodata`, utilisée par GCC pour implémenter les instructions `switch` avec des `case` consécutifs. L'index du `switch` sert d'index dans la table, et un saut indirect (`jmp`) atteint le bon `case` en O(1). *(Annexe I, §7)*

---

## K

**Keygen** (*key generator*) — Programme qui génère des clés de série valides pour un logiciel, en reproduisant l'algorithme de validation extrait par reverse engineering. En contexte éducatif (crackme), écrire un keygen démontre la compréhension complète de l'algorithme de vérification. *(Chapitre 21.8)*

---

## L

**Lazy binding** — Mécanisme de résolution différée des symboles dynamiques : les fonctions importées ne sont résolues qu'au moment de leur premier appel, via le stub PLT et le résolveur de `ld.so`. Désactivé par Full RELRO (`-z now`). *(Chapitre 2.9)*

**`LD_PRELOAD`** — Variable d'environnement Linux qui force le chargement d'une bibliothèque partagée avant toutes les autres, permettant de surcharger (hooker) des fonctions sans modifier le binaire. *(Chapitre 22.4)*

**`ld.so`** (*dynamic linker/loader*) — Programme responsable du chargement des bibliothèques partagées et de la résolution des symboles dynamiques au démarrage d'un binaire ELF. Son chemin est spécifié dans la section `.interp` (typiquement `/lib64/ld-linux-x86-64.so.2`). *(Chapitre 2.7)*

**LIEF** — Bibliothèque Python/C++ pour le parsing et la modification de binaires ELF, PE et Mach-O. Permet de lire les headers, sections, symboles, et de modifier le binaire (ajouter des sections, changer l'entry point, modifier les imports). *(Chapitre 35.1)*

**Little-endian** — Voir *Endianness*.

**LSDA** (*Language Specific Data Area*) — Table dans la section `.gcc_except_table` qui décrit les régions de code couvertes par des blocs `try`/`catch` C++, les types d'exceptions attrapées et les actions de nettoyage. *(Chapitre 17.4)*

---

## M

**Magic bytes** (*magic number*) — Séquence d'octets fixe au début d'un fichier qui identifie son format. Exemples : `\x7fELF` pour ELF, `MZ` pour PE, `PK` pour ZIP, `%PDF` pour PDF. *(Chapitre 5.1)*

**Malware** — Logiciel malveillant conçu pour nuire : virus, ransomware, trojan, dropper, rootkit, spyware. L'analyse de malware est l'un des cas d'usage principaux du RE. *(Partie VI, chapitres 26–29)*

**Mangling** — Voir *Name mangling*.

**Memcheck** — Outil de Valgrind qui détecte les erreurs mémoire à l'exécution : fuites, accès hors bornes, utilisation de mémoire non initialisée, double-free. Fonctionne sur des binaires compilés sans instrumentation spéciale. *(Chapitre 14.1)*

---

## N

**Name mangling** (*décoration de nom*) — Transformation des noms de fonctions et méthodes C++ en symboles uniques encodant le namespace, la classe, le nom, les types de paramètres et les qualificateurs. Nécessaire car le C++ autorise la surcharge de fonctions (même nom, paramètres différents). Décodable avec `c++filt`. *(Chapitre 17.1)*

**NativeAOT** — Technologie de compilation .NET qui produit un binaire natif sans bytecode CIL. Le binaire résultant est un exécutable ELF ou PE standard, analysable uniquement avec les outils de RE natifs (Ghidra, IDA). *(Chapitre 30.5 — Annexe H)*

**NOP** (*No Operation*) — Instruction qui ne fait rien (`0x90` sur x86). Utilisée pour le padding d'alignement entre fonctions, le patching (remplacement d'instructions indésirables par des NOP) et certaines techniques de NOP sled en exploitation. Les NOP multi-octets (`0x0F 0x1F ...`) sont des variantes d'alignement. *(Annexe A, §11)*

**NX** (*No-eXecute*, aussi appelé DEP/*Data Execution Prevention*) — Protection qui marque les pages de données (pile, heap) comme non-exécutables. Le processeur lève une exception si du code tente de s'exécuter dans ces pages. *(Chapitre 19.5)*

---

## O

**Obfuscation** — Ensemble de transformations appliquées à un programme pour rendre son reverse engineering plus difficile sans modifier sa fonctionnalité. Techniques courantes : renommage de symboles, control flow flattening, insertion de code mort, chiffrement de chaînes. *(Chapitre 19.3)*

**`objdump`** — Outil GNU Binutils de désassemblage et d'inspection de fichiers binaires ELF. Effectue un désassemblage linéaire (contrairement au désassemblage récursif de Ghidra/IDA). *(Chapitre 7)*

**Opcode** (*operation code*) — Encodage binaire d'une instruction machine. Par exemple, `0x90` est l'opcode de `nop`, `0xCC` est `int 3`, `0xC3` est `ret`. Un opcode peut faire 1 à 15 octets sur x86-64. *(Chapitre 3 — Annexe A)*

---

## P

**Packer** — Outil qui compresse et/ou chiffre un binaire en l'enveloppant dans un stub de décompression. À l'exécution, le stub se décompresse en mémoire et transfère le contrôle au code original. UPX est le packer le plus courant. Utilisé pour réduire la taille et gêner l'analyse statique. *(Chapitre 19.2)*

**Partial RELRO** — Mode de protection par défaut de GCC où la section `.got` est en lecture seule après le chargement mais `.got.plt` reste modifiable (nécessaire pour le lazy binding). Moins sûr que Full RELRO. *(Chapitre 19.6)*

**Patching** — Modification directe des octets d'un binaire pour en altérer le comportement. Exemple classique : inverser un saut conditionnel (`jz` → `jnz`, `0x74` → `0x75`) pour contourner une vérification. Réalisable avec ImHex, `r2 -w` ou en script avec LIEF/pwntools. *(Chapitres 6, 21.6)*

**PIE** (*Position-Independent Executable*) — Binaire compilé de manière à pouvoir être chargé à n'importe quelle adresse mémoire. Toutes les références internes utilisent l'adressage relatif (`rip`-relative). Activé par défaut sur les distributions Linux modernes. Prérequis pour que l'ASLR s'applique au binaire lui-même. *(Chapitre 2.5)*

**PLT** (*Procedure Linkage Table*) — Section ELF (`.plt`) contenant des trampolines de saut indirect pour les appels aux fonctions de bibliothèques partagées. Chaque entrée PLT redirige vers l'adresse stockée dans la GOT correspondante. *(Chapitre 2.9 — Annexe F)*

**Prologue** (*function prologue*) — Séquence d'instructions en début de fonction qui prépare le frame de pile : sauvegarde du frame pointer, allocation de l'espace pour les variables locales, sauvegarde des registres callee-saved. Forme typique en `-O0` : `push rbp` / `mov rbp, rsp` / `sub rsp, N`. *(Chapitre 3.5 — Annexe I, §9)*

**pwndbg** — Extension GDB orientée exploitation avec visualisation avancée du heap glibc (`vis_heap_chunks`), émulation de code, navigation par type d'instruction (`nextcall`, `nextret`) et outils de génération de patterns. *(Chapitre 12 — Annexe C)*

**pwntools** — Framework Python pour le développement d'exploits et l'interaction automatisée avec des binaires : gestion des I/O (tubes), assemblage/désassemblage, patching de fichiers, communication réseau. *(Chapitre 11.9)*

---

## R

**r2pipe** — Bibliothèque Python officielle pour piloter Radare2 de manière programmatique. Envoie des commandes r2 et récupère les résultats en texte ou en JSON. *(Chapitre 9.4)*

**Radare2** (*r2*) — Framework de reverse engineering en ligne de commande. Fournit le désassemblage, le débogage, le patching, la recherche de patterns et le scripting. Son interface repose sur des commandes courtes et composables. *(Chapitre 9.2 — Annexe D)*

**Ransomware** — Malware qui chiffre les fichiers de la victime et exige une rançon en échange de la clé de déchiffrement. L'analyse RE vise à identifier l'algorithme de chiffrement, extraire la clé et écrire un déchiffreur. *(Chapitre 27)*

**Red zone** — Zone de 128 octets en dessous de `rsp` (adresses `[rsp-1]` à `[rsp-128]`) que les fonctions feuilles de l'ABI System V AMD64 peuvent utiliser sans ajuster `rsp`. Garantie de ne pas être écrasée par les interruptions. N'existe pas en mode noyau. *(Annexe B, §6.3)*

**Register** (*registre*) — Emplacement de stockage rapide à l'intérieur du processeur. Sur x86-64 : 16 registres généraux 64 bits (`rax`–`r15`), 16 registres SSE 128 bits (`xmm0`–`xmm15`), le pointeur d'instruction (`rip`) et le registre de flags (`RFLAGS`). *(Chapitre 3.1 — Annexe A)*

**RELRO** (*RELocation Read-Only*) — Protection qui rend certaines sections de la GOT en lecture seule après le chargement, empêchant leur modification par un attaquant. Deux modes : Partial RELRO (défaut) et Full RELRO (`-z now`). *(Chapitre 19.6)*

**Reverse debugging** — Fonctionnalité de GDB permettant d'exécuter un programme en arrière (instruction par instruction) pour revenir à un état précédent. Très lent mais utile pour comprendre comment un état a été atteint. *(Chapitre 11 — Annexe C, §2.3)*

**RFLAGS** — Registre de flags du processeur x86-64 contenant les bits de condition mis à jour par les instructions arithmétiques et logiques. Les flags les plus utilisés en RE : ZF (Zero Flag), SF (Sign Flag), CF (Carry Flag), OF (Overflow Flag). *(Chapitre 3.1 — Annexe A)*

**RIP-relative addressing** — Mode d'adressage x86-64 où l'adresse est exprimée comme un offset par rapport à l'instruction courante (`rip`). Utilisé systématiquement en code PIE/PIC pour les accès aux données globales et aux chaînes. Exemple : `lea rdi, [rip+0x2a3e]`. *(Annexe I, §11)*

**ROP** (*Return-Oriented Programming*) — Technique d'exploitation qui enchaîne des séquences d'instructions existantes dans le binaire (gadgets terminés par `ret`) pour exécuter du code arbitraire sans injecter de nouveau code, contournant ainsi la protection NX. *(Chapitre 12.3)*

**RTTI** (*Run-Time Type Information*) — Informations de type embarquées dans les binaires C++ qui utilisent le polymorphisme (`virtual`, `dynamic_cast`, `typeid`). Contiennent les noms de classes et la hiérarchie d'héritage. Utiles en RE pour reconstruire la structure de classes. *(Chapitre 17.3)*

---

## S

**S-box** (*Substitution box*) — Table de substitution utilisée dans les algorithmes cryptographiques pour introduire de la confusion (non-linéarité). La S-box AES (256 octets) est la constante crypto la plus recherchée en RE. *(Chapitre 24.1 — Annexe J)*

**Sandbox** — Environnement isolé (VM, conteneur) dans lequel un programme potentiellement malveillant est exécuté sans risque pour le système hôte. L'isolation réseau et les snapshots sont essentiels. *(Chapitre 26)*

**Section** (ELF) — Division logique d'un fichier ELF nommée et typée. Chaque section contient un type de données spécifique (code dans `.text`, constantes dans `.rodata`, symboles dans `.symtab`). Vue du linker et des outils d'analyse statique, par opposition aux segments (vue du loader). *(Chapitre 2.4 — Annexe F)*

**Segment** (ELF) — Regroupement d'une ou plusieurs sections avec des permissions mémoire communes, décrit par un *program header*. Le loader du noyau utilise les segments pour mapper le binaire en mémoire via `mmap`. *(Chapitre 2.4 — Annexe F)*

**SIMD** (*Single Instruction, Multiple Data*) — Instructions qui opèrent simultanément sur plusieurs données (vecteurs) en une seule opération. Sur x86-64 : SSE (128 bits, registres `xmm`), AVX (256 bits, registres `ymm`), AVX-512 (512 bits, registres `zmm`). GCC auto-vectorise les boucles en SIMD à partir de `-O2`. *(Chapitre 3.9 — Annexe A, §12–§13)*

**SMT solver** (*Satisfiability Modulo Theories*) — Solveur logique capable de déterminer si un ensemble de contraintes mathématiques a une solution, et si oui, d'en fournir une. Z3 (Microsoft) est le SMT solver de référence en RE, utilisé par angr pour résoudre les contraintes d'exécution symbolique. *(Chapitre 18.4)*

**SSO** (*Small String Optimization*) — Optimisation de `std::string` (libstdc++) qui stocke les chaînes courtes (≤ 15 octets) directement dans l'objet `string` lui-même, évitant une allocation heap. *(Chapitre 17.5 — Annexe I, §19)*

**Stack** (*pile*) — Zone de mémoire LIFO utilisée pour stocker les adresses de retour, les variables locales, les arguments excédentaires et les registres sauvegardés. Croît vers les adresses basses sur x86-64. Le registre `rsp` pointe vers le sommet (adresse la plus basse). *(Chapitre 3.5)*

**Stack unwinding** (*déroulage de pile*) — Processus de remontée de la pile d'appels pour restaurer les frames précédentes, typiquement lors de la propagation d'une exception C++ ou de la génération d'une backtrace. Utilise les informations de la section `.eh_frame`. *(Chapitre 17.4 — Annexe F)*

**Stalker** — Module de Frida qui trace toutes les instructions exécutées par un thread, fournissant une couverture de code dynamique complète. Utile pour identifier les chemins d'exécution empruntés avec un input donné. *(Chapitre 13.6)*

**Static linking** (*liaison statique*) — Intégration du code des bibliothèques directement dans le binaire à la compilation, produisant un exécutable autonome sans dépendances `.so`. Le binaire est plus gros mais portable. *(Chapitre 2.3)*

**`strace`** — Outil Linux qui trace les appels système effectués par un processus. Affiche chaque syscall avec ses arguments et sa valeur de retour. Premier réflexe pour comprendre le comportement I/O et réseau d'un binaire. *(Chapitre 5.5)*

**Stripping** — Suppression des informations de débogage et de la table de symboles complète (`.symtab`, `.strtab`, `.debug_*`) d'un binaire ELF via la commande `strip`. Les symboles dynamiques (`.dynsym`) survivent au stripping. *(Chapitre 19.1)*

**`syscall`** — Instruction x86-64 qui effectue un appel système Linux. Le numéro du syscall est dans `rax`, les arguments dans `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`. Le résultat revient dans `rax`. Écrase `rcx` et `r11`. *(Chapitre 3.8 — Annexe A, §11 — Annexe B, §8)*

---

## T

**Tail call optimization** (*optimisation d'appel terminal*) — Optimisation du compilateur qui remplace un `call` + `ret` en fin de fonction par un simple `jmp`, réutilisant le frame de pile de l'appelant. Économise une frame de pile et un `ret`. *(Chapitre 16.4)*

**TLS** (*Thread-Local Storage*) — Mécanisme permettant à chaque thread d'avoir sa propre copie d'une variable globale. Sur x86-64 avec glibc, les données TLS sont accédées via le registre de segment `fs`. Le stack canary est stocké en TLS à l'offset `fs:0x28`. *(Annexe F)*

---

## U

**Unpacking** — Processus de restauration d'un binaire compressé/chiffré par un packer à son état original. Peut être statique (outil dédié : `upx -d`) ou dynamique (exécuter le binaire, attendre la décompression en mémoire, puis dumper la mémoire avec GDB). *(Chapitre 29)*

**UPX** (*Ultimate Packer for eXecutables*) — Packer open source qui compresse les binaires ELF et PE. Facilement détecté (signature UPX dans les headers) et décompressé (`upx -d`). *(Chapitre 19.2)*

---

## V

**Valgrind** — Framework d'instrumentation binaire qui exécute un programme dans une machine virtuelle pour détecter les erreurs mémoire (Memcheck), profiler l'exécution (Callgrind) et analyser le comportement runtime. Fonctionne sans recompilation. *(Chapitre 14)*

**VMA** (*Virtual Memory Address*) — Adresse dans l'espace d'adressage virtuel du processus. C'est l'adresse que vous voyez dans le désassemblage et dans les registres pendant le débogage. *(Chapitre 2.8)*

**Vtable** (*virtual method table*) — Table de pointeurs de fonctions associée à chaque classe C++ polymorphe. Chaque objet contient un pointeur vers la vtable de sa classe (le vptr). Les appels virtuels passent par la vtable pour déterminer quelle méthode appeler (dispatch dynamique). *(Chapitre 17.2 — Annexe I, §17)*

---

## W

**Watchpoint** — Breakpoint sur données : arrête l'exécution quand une adresse mémoire est lue, écrite ou modifiée. Les watchpoints matériels utilisent les registres debug du processeur (DR0–DR3, limités à 4 simultanés). Les watchpoints logiciels sont plus lents (exécution pas à pas interne). *(Chapitre 11.5 — Annexe C)*

---

## X

**XREF** — Voir *Cross-reference*.

---

## Y

**YARA** — Langage et outil de détection de patterns binaires par règles. Chaque règle décrit une combinaison de chaînes, séquences d'octets et conditions logiques. Utilisé pour scanner des fichiers à la recherche de signatures de malware, de constantes crypto ou de packers. Intégré dans ImHex. *(Chapitres 6.10, 27.4, 35.4 — Annexe J)*

---

## Z

**Z3** — Solveur SMT open source développé par Microsoft Research. Utilisé en RE pour résoudre les systèmes de contraintes extraits de l'analyse d'un binaire (vérifications de clé, conditions de branchement complexes). angr utilise Z3 en interne. *(Chapitre 18.4)*

**Zero Flag** (*ZF*) — Bit du registre RFLAGS positionné à 1 quand le résultat d'une opération arithmétique ou logique est zéro. C'est le flag le plus consulté en RE : `jz`/`je` saute si ZF=1, `jnz`/`jne` saute si ZF=0. `test reg, reg` positionne ZF si le registre est nul. *(Chapitre 3.1 — Annexe A)*

---

> 📚 **Retour aux annexes** :  
> - [README des annexes](/annexes/README.md) — index de toutes les annexes.  
> - [Annexe A](/annexes/annexe-a-opcodes-x86-64.md) — Opcodes x86-64.  
> - [Annexe B](/annexes/annexe-b-system-v-abi.md) — Conventions d'appel System V.  
> - [Annexe F](/annexes/annexe-f-sections-elf.md) — Sections ELF.  
> - [Annexe I](/annexes/annexe-i-patterns-gcc.md) — Patterns GCC.

⏭️
