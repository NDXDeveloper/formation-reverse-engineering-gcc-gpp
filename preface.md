🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Préface

---

## Objectifs du tutoriel et public visé

Ce tutoriel est né d'un constat simple : les ressources francophones sur le reverse engineering de binaires compilés avec la chaîne GNU sont rares, dispersées, et souvent soit trop théoriques, soit trop orientées vers un outil unique sans vision d'ensemble. L'objectif de cette formation est de combler ce manque en proposant un parcours structuré, progressif et résolument pratique.

À l'issue de ce tutoriel, vous serez capable de :

- **Comprendre** ce qu'un compilateur comme GCC ou G++ produit réellement, depuis le code source C/C++ jusqu'au binaire ELF chargé en mémoire.  
- **Inspecter** un binaire inconnu de manière méthodique, en combinant outils en ligne de commande, éditeurs hexadécimaux avancés et désassembleurs professionnels.  
- **Analyser statiquement** du code désassemblé et décompilé, y compris sur des binaires optimisés ou strippés.  
- **Analyser dynamiquement** un programme en cours d'exécution à l'aide de débogueurs, d'instrumentation et de fuzzing.  
- **Reconnaître** les patterns générés par GCC pour les constructions C++ (vtables, exceptions, STL, templates, smart pointers), ainsi que les effets des différents niveaux d'optimisation.  
- **Contourner** les protections courantes (stripping, packing, obfuscation de flux de contrôle, détection de débogueur).  
- **Appliquer** ces compétences sur des cas concrets : crackmes, applications réseau, binaires chiffrés, formats de fichiers custom et samples malveillants en environnement isolé.  
- **Automatiser** vos analyses grâce au scripting Python, aux API des outils (Ghidra headless, r2pipe, GDB Python, Frida) et aux règles YARA.

### À qui s'adresse cette formation ?

Cette formation est conçue pour des développeurs, étudiants en informatique ou professionnels de la sécurité qui souhaitent acquérir ou approfondir des compétences en reverse engineering sur des binaires natifs Linux. Plus précisément :

- **Développeurs C/C++** curieux de comprendre ce que le compilateur fait de leur code, ou confrontés à des bugs qui ne s'expliquent qu'au niveau assembleur.  
- **Étudiants en sécurité informatique** qui veulent aller au-delà des cours théoriques et mettre les mains dans le désassemblage.  
- **Participants à des CTF** (Capture The Flag) cherchant à structurer leur approche des challenges de type « reversing ».  
- **Analystes SOC ou malware juniors** souhaitant acquérir les bases de l'analyse de binaires ELF avant de se spécialiser.  
- **Ingénieurs logiciel** travaillant sur l'interopérabilité, le débogage avancé ou l'audit de bibliothèques tierces sans code source.

Cette formation ne s'adresse **pas** aux débutants complets en programmation. Elle suppose que vous savez déjà écrire et compiler du code, et que vous êtes à l'aise dans un terminal Linux. Les sections qui suivent détaillent précisément les prérequis attendus.

---

## Prérequis recommandés

Pour tirer le meilleur parti de cette formation, un socle de connaissances préalables est nécessaire. Vous n'avez pas besoin d'être expert dans chacun de ces domaines — un niveau intermédiaire suffit — mais une absence totale dans l'un d'entre eux risque de rendre certains chapitres difficiles à suivre.

### Programmation C (intermédiaire)

Vous devez être à l'aise avec les concepts suivants :

- Les types de base, les pointeurs et l'arithmétique de pointeurs.  
- Les structures (`struct`), les unions, les énumérations.  
- L'allocation dynamique (`malloc`, `free`) et la notion de pile vs tas.  
- Les chaînes de caractères C (tableaux de `char` terminés par `\0`).  
- La compilation avec `gcc` : flags courants (`-o`, `-Wall`, `-g`, `-O2`).  
- La lecture d'un `Makefile` simple.

### Programmation C++ (notions)

Plusieurs chapitres (notamment la Partie IV et les cas pratiques) portent sur des binaires C++. Un niveau de base est suffisant :

- Classes, héritage, polymorphisme (méthodes virtuelles).  
- Notions de templates et de la STL (`vector`, `string`, `map`).  
- Compréhension générale de la compilation C++ avec `g++`.

> 💡 Si vous ne connaissez pas le C++, les parties I à III et la majorité des cas pratiques en C restent parfaitement accessibles. Vous pourrez revenir aux chapitres C++ plus tard.

### Ligne de commande Linux (intermédiaire)

Le reverse engineering sous Linux repose massivement sur des outils en ligne de commande. Vous devez savoir :

- Naviguer dans un système de fichiers, manipuler des fichiers et des permissions.  
- Utiliser un éditeur de texte en terminal (`vim`, `nano`, ou équivalent).  
- Lancer des programmes, rediriger des flux (`stdin`, `stdout`, `stderr`), utiliser des pipes.  
- Installer des paquets avec `apt` (ou le gestionnaire de votre distribution).  
- Travailler dans un environnement multi-terminal ou avec `tmux`/`screen`.

### Notions de mémoire et d'architecture

Vous n'avez pas besoin d'un cours complet d'architecture des ordinateurs, mais les concepts suivants doivent vous être familiers :

- La distinction entre mémoire vive (RAM) et stockage.  
- La notion d'adresse mémoire et de représentation hexadécimale.  
- Les concepts de pile (stack) et de tas (heap) au niveau du système d'exploitation.  
- L'idée qu'un processeur exécute des instructions machine séquentiellement.

> 💡 Le chapitre 3 (assembleur x86-64) reprend ces notions depuis les bases dans le contexte du RE. Même si vos connaissances sont rouillées, vous pourrez suivre à condition d'y consacrer du temps.

### Ce qui n'est **pas** un prérequis

- **L'assembleur x86-64** : le chapitre 3 l'enseigne de zéro, orienté RE.  
- **Les outils de RE** (Ghidra, GDB, Frida, etc.) : chaque outil est introduit progressivement avec des instructions d'installation et de prise en main.  
- **L'analyse de malware** : la Partie VI part de zéro, avec la mise en place d'un lab sécurisé avant toute manipulation.  
- **Les langages Rust, Go ou C#** : les parties bonus (VII et VIII) sont autonomes et n'exigent aucune expérience préalable dans ces langages.

---

## Comment utiliser ce tutoriel

Cette formation est conçue pour fonctionner selon deux modes de lecture, en fonction de votre profil et de vos objectifs.

### Parcours linéaire (recommandé pour les débutants en RE)

Si vous découvrez le reverse engineering ou que vous souhaitez consolider vos bases, suivez les parties dans l'ordre, du chapitre 1 au chapitre 36. La progression est pensée pour que chaque chapitre s'appuie sur les précédents :

1. **Partie I** pose les fondations : concepts, chaîne de compilation, assembleur, environnement.  
2. **Partie II** introduit l'analyse statique avec des outils de complexité croissante.  
3. **Partie III** ajoute l'analyse dynamique (débogage, instrumentation, fuzzing).  
4. **Partie IV** approfondit les techniques avancées (optimisations, C++, exécution symbolique, anti-reversing).  
5. **Partie V** met tout en pratique sur des applications concrètes.  
6. **Parties VI à IX** élargissent vers l'analyse de malware, les langages modernes et l'automatisation.

Chaque chapitre se termine par un **checkpoint** — un mini-exercice qui valide les acquis avant de passer à la suite. Ne sautez pas les checkpoints : ils sont calibrés pour révéler rapidement les points à revoir.

### Accès par besoin (pour les profils expérimentés)

Si vous avez déjà une expérience en RE et que vous cherchez à approfondir un sujet spécifique, chaque chapitre est rédigé pour être aussi autonome que possible. Quelques exemples de parcours ciblés :

- **« Je connais les bases mais je galère sur les binaires C++ optimisés »** → Chapitres 16 (optimisations compilateur) et 17 (RE du C++ avec GCC), puis le cas pratique du chapitre 22.  
- **« Je veux apprendre Ghidra »** → Chapitre 8, puis les cas pratiques des chapitres 21 à 25 qui l'utilisent abondamment.  
- **« Je veux me lancer dans l'analyse de malware Linux »** → Chapitre 26 (lab sécurisé), puis chapitres 27 à 29.  
- **« Je dois analyser des binaires Go/Rust au travail »** → Directement les chapitres 33 ou 34, avec un retour ponctuel vers la Partie I si nécessaire.  
- **« Je veux automatiser mes analyses »** → Chapitre 35, en piochant dans les chapitres d'outils (8, 9, 11, 13) pour les API de scripting.

> 💡 Les dépendances entre chapitres sont signalées en début de chaque fichier quand elles existent. Si un chapitre suppose la maîtrise d'un outil ou d'un concept vu plus tôt, un renvoi explicite vous l'indiquera.

### Les binaires d'entraînement

Tous les binaires manipulés dans cette formation sont fournis dans le répertoire `binaries/`, accompagnés de leur code source et d'un `Makefile`. Vous pouvez les recompiler à tout moment avec `make all` depuis la racine du dépôt, ou individuellement dans chaque sous-répertoire. Chaque binaire existe en plusieurs variantes (niveaux d'optimisation, avec ou sans symboles) pour que vous puissiez constater l'impact de ces paramètres sur l'analyse.

Aucun binaire malveillant réel n'est distribué. Les samples de la Partie VI sont entièrement conçus par nos soins, à des fins pédagogiques, et ne fonctionnent que dans l'environnement contrôlé décrit au chapitre 26.

---

## Conventions typographiques

Tout au long de cette formation, les conventions suivantes sont utilisées pour faciliter la lecture et le repérage de l'information.

### Mise en forme du texte

- `code inline` — noms de commandes, fonctions, fichiers, registres, opcodes, flags de compilation et extraits de code courts. Exemple : la commande `readelf -S` affiche les sections d'un binaire ELF.  
- **Gras** — termes définis pour la première fois, concepts clés introduits dans une section, et noms d'outils lors de leur première mention. Exemple : **Ghidra** est un framework de RE open source développé par la NSA.  
- *Italique* — termes en anglais conservés tels quels lorsqu'il n'existe pas de traduction établie en français (ou que la traduction serait source de confusion). Exemple : le *name mangling* est le mécanisme de renommage des symboles C++.

### Blocs de code

Les blocs de code utilisent la coloration syntaxique adaptée au langage concerné :

```bash
readelf -h ./keygenme_O0
```

Au chapitre 4 (environnement de travail), les commandes sont préfixées par `[vm]` (à exécuter dans la VM) ou `[hôte]` (à exécuter sur la machine hôte) pour éviter toute confusion :

```bash
[vm] sudo apt install -y gdb
[hôte] virsh snapshot-create-as RE-Lab tools-ready
```

Quand un extrait assembleur est présenté, la syntaxe Intel est utilisée par défaut (sauf mention contraire), conformément à la convention la plus répandue dans les outils de RE :

```asm
mov    rdi, rax  
call   strcmp@plt  
test   eax, eax  
jnz    0x401234  
```

### Pictogrammes

Les encadrés suivants sont utilisés pour attirer l'attention sur des informations particulières :

> 💡 **Astuce** — Un conseil pratique, un raccourci ou une technique qui peut vous faire gagner du temps.

> 📖 **Rappel** — Un retour sur un concept vu dans un chapitre précédent, avec un renvoi vers la section concernée.

> ⚠️ **Attention** — Un piège courant, une erreur fréquente ou un point de vigilance à ne pas négliger.

> 🔬 **Approfondissement** — Un détail technique avancé, optionnel en première lecture, destiné aux lecteurs qui souhaitent aller plus loin.

> 🎯 **Checkpoint** — Le mini-exercice de validation en fin de chapitre. Faites-le avant de passer à la suite.

---

## Remerciements

Un projet de cette envergure ne naît pas dans le vide. Cette formation s'appuie sur le travail de nombreux auteurs, chercheurs et communautés qui partagent généreusement leurs connaissances.

### Ouvrages et ressources de référence

- **"Reverse Engineering for Beginners"** de Dennis Yurichev (également connu sous le titre *"Understanding Assembly Language"*) — une référence incontournable et librement accessible, dont l'approche par l'exemple a profondément influencé la pédagogie de cette formation.  
- **"Practical Binary Analysis"** de Dennis Andriesse — pour son traitement rigoureux de l'analyse de binaires ELF et de l'instrumentation.  
- **"The IDA Pro Book"** de Chris Eagle — bien que centré sur IDA, les méthodologies d'analyse qui y sont décrites sont universelles.  
- **"Hacking: The Art of Exploitation"** de Jon Erickson — pour son approche pédagogique de l'assembleur x86 et de la mémoire.  
- La documentation officielle de **Ghidra**, **Radare2**, **Frida**, **angr** et **GDB** — des projets open source sans lesquels cette discipline serait inaccessible au plus grand nombre.

### Communautés et plateformes

- **root-me.org**, **crackmes.one**, **pwnable.kr**, **picoCTF** et **Hack The Box** — des plateformes d'entraînement qui permettent de mettre en pratique les compétences de RE dans un cadre ludique et légal.  
- Les communautés **r/ReverseEngineering**, **r/netsec** et les villages RE des conférences **DEF CON** et **REcon** — pour le partage continu de techniques, d'outils et de write-ups.  
- Le projet **PoC||GTFO** — pour avoir démontré que la rigueur technique et la créativité ne sont pas incompatibles.

### Outils open source

Cette formation repose presque intégralement sur des outils libres et gratuits. Un remerciement particulier aux équipes derrière GCC, GDB, Ghidra, Radare2, Frida, ImHex, angr, AFL++, pwntools, YARA, ILSpy et Binary Ninja Cloud. L'accessibilité de ces outils a démocratisé une discipline autrefois réservée à quelques spécialistes disposant de licences coûteuses.

### Responsabilité et éthique

Ce tutoriel est publié sous **licence MIT** et a une vocation strictement éducative. Toutes les techniques présentées ici doivent être utilisées dans un cadre légal : audit de sécurité autorisé, recherche de vulnérabilités dans vos propres logiciels, compétitions CTF, interopérabilité, ou apprentissage sur des binaires que vous avez compilés vous-même. Le reverse engineering de logiciels propriétaires sans autorisation peut constituer une infraction selon les juridictions (DMCA aux États-Unis, directive EUCD en Europe, entre autres). Le chapitre 1 détaille le cadre légal applicable.

Les samples de la Partie VI (analyse de code malveillant) sont des créations originales à des fins pédagogiques. Aucun malware réel n'est distribué, et leur exécution est strictement limitée à l'environnement sandboxé décrit au chapitre 26.

---

*Bonne exploration. Le binaire ne ment jamais — il faut simplement apprendre à le lire.*

⏭️ [Partie I — Fondamentaux & Environnement](/partie-1-fondamentaux.md)
