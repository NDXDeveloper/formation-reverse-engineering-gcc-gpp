🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 1.6 — Taxonomie des cibles : binaire natif, bytecode, firmware — où se situe ce tuto

> **Chapitre 1 — Introduction au Reverse Engineering**  
> 📦 Aucun prérequis technique — section de lecture.  
> 📖 Cette section situe le périmètre de la formation dans le paysage global du reverse engineering.

---

## Un terme, de nombreuses cibles

Le terme « reverse engineering » est utilisé pour désigner des activités qui, bien qu'elles partagent un objectif commun (comprendre le fonctionnement d'un système sans sa documentation), portent sur des cibles très différentes en nature, en complexité et en outillage. Reverser un binaire ELF x86-64 compilé avec GCC, reverser une application Android en bytecode Dalvik, reverser le firmware d'un routeur MIPS et reverser un protocole réseau capturé avec Wireshark sont quatre activités qui relèvent toutes du RE — mais qui exigent des compétences, des outils et des approches distincts.

Cette section dresse une carte du territoire. Elle décrit les principales catégories de cibles rencontrées en RE, explique en quoi elles diffèrent, et situe précisément le périmètre couvert par cette formation.

---

## Les grandes catégories de cibles

### Binaires natifs

Un **binaire natif** est un fichier exécutable contenant du code machine directement exécutable par un processeur. Le compilateur (GCC, Clang, MSVC, etc.) a transformé le code source en une séquence d'instructions spécifiques à une architecture matérielle donnée. Le binaire est étroitement lié à deux paramètres : l'**architecture du processeur** (x86-64, x86-32, ARM, AArch64, MIPS, RISC-V, PowerPC…) et le **système d'exploitation** cible, qui détermine le format du fichier exécutable et les conventions d'appel.

Les trois formats de binaires natifs les plus courants sont :

**ELF (Executable and Linkable Format)** — Le format standard sous Linux, les BSD et la plupart des systèmes Unix. C'est le format sur lequel cette formation se concentre. Un fichier ELF contient des headers qui décrivent sa structure, des sections (`.text` pour le code, `.data` et `.rodata` pour les données, `.bss` pour les données non initialisées, `.plt`/`.got` pour la résolution dynamique des symboles, etc.) et des segments qui définissent comment le loader doit mapper le fichier en mémoire. Le chapitre 2 détaille en profondeur la structure ELF.

**PE (Portable Executable)** — Le format natif de Windows. Sa structure est différente de celle d'ELF (headers DOS et PE, sections `.text`, `.rdata`, `.data`, Import Address Table, Export Directory), mais les principes sont comparables. Le RE de binaires PE est un domaine à part entière, dominant dans l'analyse de malware Windows. Cette formation ne couvre pas le format PE directement, mais les techniques d'analyse (désassemblage, débogage, instrumentation) sont largement transposables.

**Mach-O (Mach Object)** — Le format natif de macOS et iOS. Utilisé par les binaires compilés avec Xcode/Clang pour les plateformes Apple. Sa structure comprend des *load commands* qui jouent un rôle analogue aux headers de programme ELF. Le RE de binaires Mach-O est pertinent pour l'analyse d'applications macOS et iOS, mais reste une niche comparée à ELF et PE.

**Caractéristiques du RE de binaires natifs :**

- Le code analysé est du **code machine brut** — des instructions directement exécutées par le processeur. Il n'y a pas de couche d'abstraction intermédiaire.  
- La **perte d'information** à la compilation est maximale : types, noms de variables, structure du code source — tout est perdu ou fortement dégradé (cf. section 1.1).  
- L'analyse repose sur le **désassemblage** (traduction du code machine en assembleur) et la **décompilation** (reconstruction d'un pseudo-code de haut niveau). La qualité de ces opérations dépend de l'architecture, du compilateur, du niveau d'optimisation et des protections éventuelles.  
- Les outils doivent connaître l'**architecture cible** : un désassembleur x86-64 ne peut pas analyser un binaire ARM, et inversement. Les outils modernes (Ghidra, Radare2, Binary Ninja) supportent de nombreuses architectures ; d'autres (IDA Free) sont plus limités.

> 💡 **C'est la catégorie centrale de cette formation.** Les Parties I à VI portent exclusivement sur des binaires natifs ELF x86-64 compilés avec GCC/G++.

---

### Bytecode managé

Le **bytecode** est un code intermédiaire, compilé non pas pour un processeur physique mais pour une **machine virtuelle** logicielle. Le programme source est compilé en bytecode, et ce bytecode est exécuté par un runtime qui l'interprète ou le compile à la volée (JIT — *Just-In-Time compilation*).

Les deux écosystèmes de bytecode les plus répandus sont :

**Java / JVM (Java Virtual Machine)** — Le compilateur `javac` transforme du code Java en bytecode JVM, stocké dans des fichiers `.class` regroupés en archives `.jar`. La JVM exécute ce bytecode sur n'importe quelle plateforme supportée. D'autres langages ciblent également la JVM : Kotlin, Scala, Groovy, Clojure.

**.NET / CIL (Common Intermediate Language)** — Le compilateur C# (ou VB.NET, F#) produit du bytecode CIL (anciennement MSIL), stocké dans des *assemblies* (fichiers `.dll` ou `.exe` au format PE avec des métadonnées .NET). Le CLR (*Common Language Runtime*) exécute ce bytecode, soit en l'interprétant, soit en le compilant JIT.

**Android / Dalvik-ART** — Les applications Android sont compilées en bytecode Dalvik (fichiers `.dex`), exécuté par le runtime ART (*Android Runtime*). Depuis Android 5.0, ART compile le bytecode en code natif à l'installation (AOT — *Ahead-Of-Time*), mais l'analyse porte généralement sur le bytecode `.dex` ou sur le code Java/Kotlin décompilé.

**Caractéristiques du RE de bytecode :**

- La **perte d'information est beaucoup moindre** que pour les binaires natifs. Le bytecode conserve les noms de classes, de méthodes et de champs (sauf si un obfuscateur les a renommés). Les types sont explicites. La structure du programme (héritage, interfaces, exceptions) est préservée dans les métadonnées.  
- La **décompilation est nettement plus fiable**. Des outils comme JD-GUI, CFR ou Procyon (pour Java), ILSpy ou dnSpy (pour .NET) et JADX (pour Android) produisent un code source reconstruit souvent très proche de l'original — parfois même compilable directement.  
- Les techniques d'**obfuscation** sont le principal obstacle : renommage de symboles (classes, méthodes, variables remplacées par `a`, `b`, `c`), chiffrement de chaînes, *control flow flattening*, *string encryption*, réflexion dynamique. Les outils de déobfuscation (de4dot pour .NET, divers deobfuscators pour Java) permettent de contrer partiellement ces protections.  
- L'analyse est **moins dépendante de l'architecture matérielle** puisque le bytecode est portable. Un analyste n'a pas besoin de maîtriser un jeu d'instructions spécifique à un processeur — il doit en revanche comprendre le modèle d'exécution de la machine virtuelle cible.

> 💡 **Couverture dans cette formation** — La Partie VII (chapitres 30 à 32) propose un parcours bonus sur le RE de binaires .NET/C#, couvrant la décompilation avec ILSpy et dnSpy, le hooking avec Frida, et le patching de bytecode CIL. Le RE Java/Android n'est pas couvert.

---

### Firmware et systèmes embarqués

Un **firmware** est un logiciel intégré dans un composant matériel : routeur, caméra IP, automate industriel, contrôleur automobile, implant médical, objet connecté (IoT). Le firmware est généralement flashé dans une mémoire non volatile (flash NOR/NAND, EEPROM) et exécuté par un microprocesseur ou microcontrôleur embarqué.

**Caractéristiques du RE de firmware :**

- L'**extraction** du firmware est souvent la première difficulté. Selon le matériel, il peut être obtenu par téléchargement sur le site du fabricant (cas le plus simple), par dump de la puce flash via un programmateur SPI/JTAG, par interception d'une mise à jour OTA (*Over-The-Air*), ou par exploitation d'une interface de debug.  
- Le firmware est souvent une **image complète** contenant un système de fichiers (SquashFS, CramFS, JFFS2), un noyau Linux (ou un RTOS propriétaire), des bibliothèques partagées et des exécutables applicatifs. L'outil `binwalk` est le couteau suisse de l'extraction et de l'identification des composants d'une image firmware.  
- Les **architectures** rencontrées sont variées : ARM (le plus courant dans l'IoT et le mobile), MIPS (routeurs, points d'accès), AArch64, PowerPC, Xtensa (ESP32), AVR et Cortex-M (microcontrôleurs). L'analyste doit maîtriser — ou au minimum pouvoir lire — le jeu d'instructions de l'architecture cible.  
- Le débogage nécessite souvent du **matériel spécialisé** : sondes JTAG/SWD, adaptateurs série UART, analyseurs logiques. L'émulation avec QEMU est une alternative qui permet d'exécuter le firmware (ou des composants isolés) sur une machine de développement sans le matériel d'origine.  
- Les **vulnérabilités** sont fréquentes et souvent critiques : mots de passe par défaut, services réseau non authentifiés, absence de chiffrement des communications, clés privées embarquées en clair, commandes de debug non retirées en production.

> 💡 **Couverture dans cette formation** — Le RE de firmware n'est pas couvert directement. Cependant, les compétences acquises sur les binaires ELF (analyse statique, désassemblage, débogage) sont directement transposables aux composants applicatifs extraits d'un firmware Linux. Si le firmware contient des exécutables ARM ELF, par exemple, Ghidra les analyse avec les mêmes techniques que celles enseignées ici — seul le jeu d'instructions change.

---

### Protocoles réseau et formats de fichiers

Le RE ne porte pas toujours sur du code exécutable. Deux catégories de cibles non exécutables méritent d'être mentionnées :

**Protocoles réseau** — Comprendre un protocole de communication non documenté en capturant et analysant le trafic réseau. L'analyste observe les échanges entre un client et un serveur, identifie la structure des messages (headers, champs, délimiteurs, encodages), reconstitue la machine à états du protocole, et peut écrire une implémentation indépendante. Les outils clés sont Wireshark (capture et dissection de paquets), `strace` (traçage des appels socket côté applicatif) et le RE du binaire client ou serveur pour comprendre comment les messages sont construits et interprétés.

**Formats de fichiers** — Comprendre la structure d'un format de fichier propriétaire non documenté. L'analyste examine des fichiers d'exemple avec un éditeur hexadécimal, identifie les magic bytes, les champs fixes et variables, les offsets, les tables d'index, les sections compressées ou chiffrées, et reconstitue la spécification du format. ImHex avec ses patterns `.hexpat` est l'outil central de cette activité.

> 💡 **Couverture dans cette formation** — Les deux sujets sont couverts par des cas pratiques dédiés. Le chapitre 23 traite le RE d'un protocole réseau custom (identification, dissection, écriture d'un client de remplacement). Le chapitre 25 traite le RE d'un format de fichier custom (cartographie, fuzzing du parser, écriture d'un parser indépendant, documentation du format).

---

### Code source obfusqué (JavaScript, PHP, Python…)

Bien qu'il ne s'agisse pas de « reverse engineering » au sens traditionnel du terme, l'analyse de **code source volontairement obfusqué** dans des langages interprétés (JavaScript minifié/obfusqué, PHP encodé, Python compilé en `.pyc`) est une activité apparentée qui utilise certaines techniques similaires.

Un JavaScript obfusqué avec des outils comme *javascript-obfuscator* remplace les noms de variables par des séquences illisibles, encode les chaînes de caractères en hexadécimal, insère du code mort et transforme les structures de contrôle. L'analyste doit reconnaître les patterns d'obfuscation, décoder les chaînes, simplifier le flux de contrôle — des opérations conceptuellement proches du RE de bytecode managé.

> 💡 **Couverture dans cette formation** — Ce sujet n'est pas couvert. Il relève davantage de l'analyse web et de la sécurité applicative que du RE de binaires. Cependant, les principes fondamentaux (comprendre un flux de contrôle transformé, identifier des patterns d'obfuscation, reconstruire la logique originale) sont les mêmes.

---

### Hardware et circuits électroniques

À l'extrémité du spectre, le RE de **matériel** (*hardware reverse engineering*) consiste à analyser un circuit électronique — carte, puce, ASIC, FPGA — pour en comprendre le fonctionnement. Cela peut aller du relevé du schéma d'une carte PCB (identification des composants, traçage des pistes) jusqu'à l'imagerie de couches d'un circuit intégré par microscopie pour en reconstruire la netlist.

Le hardware RE est une discipline à part entière, qui exige des compétences en électronique, des outils de mesure (oscilloscope, analyseur logique, station de soudure) et parfois des équipements coûteux (microscope électronique, station FIB).

> 💡 **Couverture dans cette formation** — Le hardware RE n'est pas couvert. Il est mentionné ici uniquement pour compléter la taxonomie et montrer l'étendue du domaine.

---

## Où se situe cette formation

Le tableau ci-dessous résume la couverture de chaque catégorie de cibles dans cette formation :

| Catégorie de cible | Couverture | Parties |  
|---|---|---|  
| Binaires natifs ELF x86-64 (GCC/G++) | **Couverture complète** — c'est le cœur de la formation | I à VI |  
| Binaires natifs ELF — autres architectures (ARM, MIPS) | Non couvert directement, mais les techniques et outils (Ghidra, GDB) sont transposables | — |  
| Binaires natifs PE (Windows) | Non couvert, mais la méthodologie est applicable | — |  
| Bytecode .NET / C# | **Couverture bonus** — décompilation, hooking, patching | VII (ch. 30–32) |  
| Bytecode Java / Android | Non couvert | — |  
| Binaires Rust (ELF) | **Couverture bonus** — spécificités de compilation et d'analyse | VIII (ch. 33) |  
| Binaires Go (ELF) | **Couverture bonus** — spécificités du runtime et des symboles | VIII (ch. 34) |  
| Protocoles réseau custom | **Couvert** — cas pratique complet | V (ch. 23) |  
| Formats de fichiers custom | **Couvert** — cas pratique complet | V (ch. 25) |  
| Firmware / systèmes embarqués | Non couvert directement | — |  
| Code source obfusqué (JS, PHP, Python) | Non couvert | — |  
| Hardware / circuits | Non couvert | — |

### Pourquoi ce périmètre ?

Le choix de se concentrer sur les **binaires natifs ELF x86-64 compilés avec GCC/G++** est délibéré :

**La profondeur plutôt que la largeur.** Couvrir toutes les architectures, tous les formats et tous les langages de manière superficielle n'aurait pas permis d'atteindre le niveau de détail nécessaire pour être réellement opérationnel. En se concentrant sur une cible unique, la formation peut explorer chaque aspect en profondeur : la structure ELF, les conventions d'appel System V, les patterns spécifiques de GCC, les constructions C++ selon l'ABI Itanium, les optimisations à chaque niveau.

**La cible la plus représentative.** x86-64 est l'architecture dominante sur les serveurs, les postes de travail et une grande partie de l'infrastructure cloud. Linux est omniprésent dans les serveurs, les conteneurs et les systèmes embarqués haut de gamme. GCC est le compilateur par défaut sur la plupart des distributions Linux. Maîtriser le RE de binaires ELF x86-64 GCC, c'est être capable d'analyser une proportion significative du logiciel déployé dans le monde.

**La transférabilité des compétences.** Les concepts et la méthodologie enseignés ici — lire de l'assembleur, comprendre un graphe de flux de contrôle, poser un breakpoint au bon endroit, reconnaître les patterns d'un compilateur, combiner analyse statique et dynamique — se transposent directement aux autres architectures et formats. Un analyste qui maîtrise le RE de binaires ELF x86-64 peut se mettre au RE de binaires PE Windows ou de firmware ARM en quelques semaines d'adaptation, parce que les principes fondamentaux sont les mêmes. Seuls les détails changent : le jeu d'instructions, le format du binaire, les conventions d'appel, les outils spécifiques.

**Les bonus pour élargir.** Les Parties VII et VIII offrent des points d'entrée vers le bytecode .NET et les binaires Rust/Go. Ces parties ne visent pas l'exhaustivité — elles montrent les spécificités de chaque cible par rapport au socle ELF/C/C++ acquis dans les parties principales, et fournissent les clés pour approfondir de manière autonome.

---

> 📖 **À retenir** — Le reverse engineering couvre un spectre très large de cibles : binaires natifs, bytecode managé, firmware, protocoles, formats de fichiers, matériel. Cette formation se concentre sur les binaires natifs ELF x86-64 compilés avec GCC/G++, avec des extensions vers .NET, Rust et Go. Ce choix privilégie la profondeur sur la largeur, tout en enseignant une méthodologie et des compétences directement transposables aux autres catégories de cibles.

---


⏭️ [🎯 Checkpoint : classer 5 scénarios donnés en « statique » ou « dynamique »](/01-introduction-re/checkpoint.md)
