🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe G — Comparatif des outils natifs (outil / usage / gratuit / CLI ou GUI)

> 📎 **Fiche de référence** — Cette annexe synthétise l'ensemble des outils présentés dans la formation pour le reverse engineering de binaires natifs (ELF x86-64). Pour chaque outil, elle indique sa catégorie d'usage, son interface (ligne de commande ou graphique), sa licence, les chapitres où il est couvert et une appréciation de son niveau de difficulté. Elle vous aide à choisir l'outil adapté à votre besoin sans avoir à relire les chapitres de présentation.

---

## Comment lire ce comparatif

Les outils sont regroupés par catégorie fonctionnelle. Les colonnes sont les suivantes :

| Colonne | Signification |  
|---------|---------------|  
| **Outil** | Nom de l'outil |  
| **Usage principal** | Ce pour quoi l'outil est le plus utilisé en RE |  
| **Interface** | CLI (ligne de commande), GUI (interface graphique), ou les deux |  
| **Gratuit** | Oui = entièrement gratuit, Freemium = version gratuite limitée, Payant = licence commerciale |  
| **Licence** | Type de licence open source ou propriétaire |  
| **OS** | Systèmes d'exploitation supportés |  
| **Chapitres** | Chapitres de la formation où l'outil est couvert |  
| **Difficulté** | Courbe d'apprentissage estimée (★ facile → ★★★ avancé) |

---

## 1 — Inspection et triage de binaires

Ces outils sont les premiers que vous utilisez face à un binaire inconnu. Ils répondent aux questions de base : quel est ce fichier, que contient-il, comment est-il protégé ?

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| `file` | Identifier le type d'un fichier (ELF, PE, script, données) | CLI | Oui | BSD | Linux, macOS | 5.1 | ★ |  
| `strings` | Extraire les chaînes imprimables d'un binaire | CLI | Oui | GPL (GNU) | Linux, macOS | 5.1 | ★ |  
| `xxd` | Dump hexadécimal compact | CLI | Oui | GPL (Vim) | Linux, macOS | 5.1 | ★ |  
| `hexdump` | Dump hexadécimal configurable | CLI | Oui | BSD | Linux, macOS | 5.1 | ★ |  
| `readelf` | Inspection des headers, sections, segments, symboles et relocations ELF | CLI | Oui | GPL (GNU Binutils) | Linux | 2.4, 5.2 | ★★ |  
| `objdump` | Désassemblage basique et inspection des sections ELF | CLI | Oui | GPL (GNU Binutils) | Linux | 5.2, 7.1–7.7 | ★★ |  
| `nm` | Lister les symboles d'un binaire (fonctions, variables, types) | CLI | Oui | GPL (GNU Binutils) | Linux | 5.3 | ★ |  
| `c++filt` | Démanglement des symboles C++ (Itanium ABI) | CLI | Oui | GPL (GNU) | Linux | 7.6, 17.1 | ★ |  
| `ldd` | Lister les dépendances de bibliothèques partagées | CLI | Oui | GPL (glibc) | Linux | 5.4 | ★ |  
| `ldconfig` | Gérer le cache des bibliothèques partagées | CLI | Oui | GPL (glibc) | Linux | 5.4 | ★ |  
| `checksec` | Inventaire des protections binaires (ASLR, PIE, NX, canary, RELRO, Fortify) | CLI | Oui | Apache 2.0 | Linux | 5.6, 19.9 | ★ |  
| `binwalk` | Détection de formats embarqués, signatures et entropie | CLI | Oui | MIT | Linux, macOS | 25.1 | ★ |  
| `rabin2` | Analyse de métadonnées binaires (suite Radare2) | CLI | Oui | LGPL3 | Linux, macOS, Windows | 9.2 | ★★ |

### Quand utiliser quel outil de triage ?

Le workflow de triage recommandé (chapitre 5.7) enchaîne ces outils dans un ordre logique. `file` identifie le format, `strings` révèle les chaînes significatives, `readelf -S` montre la structure des sections, `readelf -d` et `ldd` identifient les dépendances, `checksec` inventorie les protections et `nm` ou `objdump -t` listent les symboles disponibles. Ce pipeline de 5 minutes est la base de toute analyse.

Pour un triage rapide en une seule commande, `rabin2 -I` de la suite Radare2 fournit un résumé qui combine les informations de `file`, `checksec` et `readelf` en une seule sortie.

---

## 2 — Analyse hexadécimale

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **ImHex** | Éditeur hexadécimal avancé avec langage de patterns `.hexpat`, visualisation structurée, bookmarks, diff, YARA | GUI | Oui | GPL2 | Linux, macOS, Windows | 6.1–6.11, 21.6, 24.4, 25.2 | ★★ |

ImHex est dans une catégorie à part. Il dépasse de très loin les éditeurs hexadécimaux classiques grâce à son langage de patterns (voir Annexe E) qui permet de visualiser les structures binaires avec des couleurs et une arborescence, de comparer deux versions d'un binaire, de rechercher des magic bytes et d'appliquer des règles YARA. C'est l'outil de choix pour la cartographie de formats de fichiers et le patching précis.

Les éditeurs hexadécimaux alternatifs comme `HxD` (Windows), `010 Editor` (payant, multi-plateforme) et `Hex Fiend` (macOS) peuvent dépanner mais n'offrent pas le langage de patterns qui fait la force d'ImHex pour le RE.

---

## 3 — Désassembleurs et décompilateurs

C'est la catégorie reine du RE. Les désassembleurs transforment le code machine en assembleur lisible, et les décompilateurs tentent de reconstruire du pseudo-code C à partir de l'assembleur.

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **Ghidra** | Désassembleur + décompilateur complet, analyse automatique, scripts Java/Python, mode headless | GUI (+ headless CLI) | Oui | Apache 2.0 (NSA) | Linux, macOS, Windows | 8.1–8.9, 20.2 | ★★ |  
| **IDA Free** | Désassembleur interactif de référence industrielle (version gratuite limitée au x86-64 cloud decompiler) | GUI | Freemium | Propriétaire (Hex-Rays) | Linux, macOS, Windows | 9.1 | ★★ |  
| **IDA Pro** | Version complète d'IDA avec décompilateur local, multi-architecture, SDK plugins | GUI | Payant | Propriétaire (Hex-Rays) | Linux, macOS, Windows | 9.1 | ★★★ |  
| **Radare2** | Framework de RE en ligne de commande : désassemblage, débogage, patching, scripting | CLI | Oui | LGPL3 | Linux, macOS, Windows | 9.2–9.4 | ★★★ |  
| **Cutter** | Interface graphique de Radare2 avec décompilateur intégré | GUI | Oui | GPL3 | Linux, macOS, Windows | 9.2 | ★★ |  
| **Binary Ninja** | Désassembleur + décompilateur moderne avec IL intermédiaire puissant | GUI | Freemium (Cloud gratuit) | Propriétaire (Vector 35) | Linux, macOS, Windows | 9.5 | ★★ |  
| **RetDec** | Décompilateur statique standalone (offline, pas d'interface interactive) | CLI | Oui | MIT (Avast) | Linux, macOS, Windows | 20.3 | ★★ |  
| `objdump -d` | Désassemblage basique (linéaire, pas d'analyse de flux) | CLI | Oui | GPL (GNU Binutils) | Linux | 7.1–7.7 | ★ |

### Quel désassembleur choisir ?

Le choix du désassembleur dépend de votre budget, de votre confort avec la ligne de commande et de la complexité du binaire cible.

**Ghidra** est le choix par défaut recommandé dans cette formation. Il est entièrement gratuit, son décompilateur est de bonne qualité (comparable à IDA sur beaucoup de binaires), il supporte le scripting Java et Python, et le mode headless permet l'automatisation batch. Ses inconvénients sont une interface Java parfois lente et un temps d'analyse initial long sur les gros binaires.

**IDA Pro** reste la référence industrielle. Son décompilateur Hex-Rays est généralement considéré comme le meilleur du marché, notamment sur le code optimisé et le C++. Son SDK permet un écosystème de plugins très riche (BinDiff, Diaphora, FLIRT, etc.). Son prix élevé (plusieurs milliers d'euros) le réserve aux professionnels. La version IDA Free est un bon compromis pour s'initier à l'interface IDA.

**Radare2** est l'outil des power users qui préfèrent la ligne de commande. Sa force est la composabilité des commandes, le scripting via r2pipe, et le fait qu'il fonctionne sur n'importe quel terminal (SSH sur un serveur distant, par exemple). Sa courbe d'apprentissage est la plus raide de tous les outils listés ici. **Cutter** atténue cette difficulté en fournissant une GUI, et le plugin r2ghidra lui donne accès au décompilateur de Ghidra.

**Binary Ninja** est un outil moderne qui se distingue par ses représentations intermédiaires (LLIL, MLIL, HLIL) permettant des analyses programmatiques sophistiquées. La version Cloud gratuite est suffisante pour découvrir l'outil. La version commerciale est moins chère qu'IDA Pro.

**RetDec** est utile en complément : vous pouvez lui passer un binaire et obtenir un pseudo-code C sans interface interactive. La qualité de décompilation est inférieure à Ghidra et IDA, mais il peut servir de « second avis » rapide.

**`objdump`** est toujours disponible sur n'importe quel système Linux et ne nécessite aucune installation. Il est parfait pour un désassemblage rapide d'une fonction, mais il ne fait que du désassemblage linéaire (pas de détection de fonctions, pas de xrefs, pas de décompilation). C'est le point de départ avant de dégainer un outil plus lourd.

---

## 4 — Débogueurs

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **GDB** | Débogueur natif de référence Linux : breakpoints, inspection mémoire, scripting Python | CLI | Oui | GPL (GNU) | Linux, macOS | 11.1–11.9 | ★★ |  
| **GEF** | Extension GDB : contexte enrichi, checksec, heap, gadgets ROP, patterns | CLI (extension GDB) | Oui | MIT | Linux | 12.1–12.5 | ★★ |  
| **pwndbg** | Extension GDB : visualisation heap avancée, émulation, telescope, navigation | CLI (extension GDB) | Oui | MIT | Linux | 12.1–12.5 | ★★ |  
| **PEDA** | Extension GDB : plus ancienne, contexte coloré, recherche de patterns | CLI (extension GDB) | Oui | BSD | Linux | 12.1 | ★★ |  
| **r2 (debug)** | Débogueur intégré à Radare2 (mode `-d`) | CLI | Oui | LGPL3 | Linux, macOS, Windows | 9.2 | ★★★ |

### GDB vs extensions : lequel installer ?

GDB natif est indispensable — c'est la fondation. Les extensions (GEF, pwndbg, PEDA) s'installent par-dessus et ne modifient pas GDB lui-même. Vous n'en activez qu'**une seule à la fois** dans votre `.gdbinit`.

**GEF** est recommandé pour les débutants et les analystes RE. Il est le plus facile à installer (un seul fichier Python), a une documentation claire, et son jeu de commandes couvre bien le RE et l'exploitation basique.

**pwndbg** est recommandé pour l'exploitation et l'analyse de heap. Ses commandes `vis_heap_chunks`, `bins`, `tcachebins` et `emulate` sont supérieures à celles de GEF pour le travail sur le heap glibc. Il est aussi le plus activement maintenu.

**PEDA** est l'extension historique. Elle reste fonctionnelle mais est moins activement développée que GEF et pwndbg. À choisir si vous travaillez dans un environnement contraint où installer les dépendances de GEF/pwndbg est difficile.

Le tableau de correspondance entre GEF et pwndbg est dans l'Annexe C, §13.

---

## 5 — Instrumentation et traçage dynamique

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| `strace` | Tracer les appels système d'un processus | CLI | Oui | BSD/GPL | Linux | 5.5, 23.1 | ★ |  
| `ltrace` | Tracer les appels aux fonctions de bibliothèques partagées | CLI | Oui | GPL | Linux | 5.5 | ★ |  
| **Frida** | Instrumentation dynamique : hooking de fonctions, modification d'arguments/retours en live, stalker (coverage) | CLI + scripting JS | Oui | wxWindows | Linux, macOS, Windows, Android, iOS | 13.1–13.7 | ★★★ |  
| **pwntools** | Framework Python pour l'interaction automatisée avec des binaires (I/O, patching, exploitation) | CLI (lib Python) | Oui | MIT | Linux | 11.9, 21.8, 23.5, 35.3 | ★★ |

### Quand utiliser strace/ltrace vs Frida ?

`strace` et `ltrace` sont des outils passifs qui ne modifient pas le comportement du programme — ils se contentent de lister les appels. Ils sont parfaits pour un premier aperçu de ce que fait un binaire : quels fichiers il ouvre, quelles connexions réseau il établit, quelles fonctions de la libc il appelle. Aucune installation particulière n'est nécessaire, ils sont disponibles sur tout système Linux.

Frida est un outil actif qui peut **modifier** le comportement du programme en temps réel : changer les arguments d'une fonction, remplacer sa valeur de retour, injecter du code JavaScript dans le processus cible. C'est beaucoup plus puissant mais aussi plus complexe. Frida est l'outil de choix quand vous devez contourner une vérification (licence, anti-debug) ou quand vous avez besoin de logger des informations internes au programme (buffers mémoire, clés crypto avant chiffrement).

pwntools n'est pas un outil de traçage à proprement parler mais un framework d'interaction. Il automatise les échanges stdin/stdout avec un binaire, le patching de fichiers, la construction d'exploits et la communication réseau. C'est le couteau suisse de l'exploitation et du scripting RE en Python.

---

## 6 — Analyse mémoire et profiling

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **Valgrind (Memcheck)** | Détection de fuites mémoire, accès invalides, utilisation de mémoire non initialisée | CLI | Oui | GPL | Linux, macOS | 14.1 | ★★ |  
| **Callgrind** (+ KCachegrind) | Profiling d'exécution, graphe d'appels, comptage d'instructions | CLI + GUI (KCachegrind) | Oui | GPL | Linux | 14.2 | ★★ |  
| **AddressSanitizer (ASan)** | Détection de buffer overflows, use-after-free, memory leaks (compile-time) | CLI (flag GCC/Clang) | Oui | Partie de GCC/Clang | Linux, macOS | 14.3 | ★ |  
| **UBSan** | Détection de comportements indéfinis (signed overflow, null deref, shift) | CLI (flag GCC/Clang) | Oui | Partie de GCC/Clang | Linux, macOS | 14.3 | ★ |  
| **MSan** | Détection d'utilisation de mémoire non initialisée (Clang uniquement) | CLI (flag Clang) | Oui | Partie de Clang | Linux | 14.3 | ★ |

### Quand utiliser Valgrind vs les sanitizers ?

Valgrind est un outil d'analyse *post-compilation* : vous l'utilisez sur un binaire déjà compilé, sans le recompiler. C'est son avantage principal en RE, car vous n'avez pas toujours accès au code source. Il fonctionne par émulation (le binaire tourne dans une machine virtuelle Valgrind), ce qui le rend environ 20 à 50 fois plus lent que l'exécution native. Malgré cette lenteur, Valgrind est irremplaçable pour analyser le comportement mémoire d'un binaire dont vous n'avez pas le source.

Les sanitizers (ASan, UBSan, MSan) sont des instrumentations à la *compilation* : vous recompilez le code source avec `-fsanitize=address` (ou `undefined`, `memory`). Ils sont beaucoup plus rapides que Valgrind (ralentissement de 2x seulement pour ASan) et détectent certains bugs que Valgrind manque (et inversement). Ils ne sont utilisables que quand vous avez accès au code source ou que vous pouvez recompiler le binaire.

En RE, Valgrind est l'outil par défaut. Les sanitizers entrent en jeu quand vous avez reconstruit un code source partiel et que vous voulez le tester, ou quand vous travaillez sur les binaires d'entraînement de cette formation (dont le source est fourni).

---

## 7 — Fuzzing

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **AFL++** | Fuzzing coverage-guided avec instrumentation compile-time ou QEMU | CLI | Oui | Apache 2.0 | Linux | 15.2, 25.3 | ★★ |  
| **libFuzzer** | Fuzzing in-process intégré à Clang/LLVM | CLI (intégré au binaire) | Oui | Apache 2.0 (LLVM) | Linux, macOS | 15.3 | ★★ |

AFL++ est le fuzzer de référence pour le RE car il supporte le mode QEMU (`afl-fuzz -Q`) qui permet de fuzzer un binaire **sans le recompiler**. Cela le rend directement applicable aux binaires cibles dont vous n'avez pas le source. libFuzzer nécessite de recompiler le code source avec Clang et offre des performances supérieures mais exige l'accès au source.

---

## 8 — Exécution symbolique et solveurs

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **angr** | Exécution symbolique de binaires : exploration de chemins, résolution automatique de contraintes | CLI (lib Python) | Oui | BSD | Linux, macOS | 18.2–18.6, 21.7 | ★★★ |  
| **Z3** | Solveur SMT : résolution de contraintes logiques et arithmétiques extraites manuellement | CLI (lib Python/C++) | Oui | MIT (Microsoft) | Linux, macOS, Windows | 18.4 | ★★★ |

angr et Z3 sont les deux faces de l'exécution symbolique en RE. angr est un framework complet qui charge un binaire, explore ses chemins d'exécution et utilise Z3 en interne pour résoudre les contraintes. Vous l'utilisez quand vous voulez résoudre un crackme ou trouver un input qui atteint un point précis du programme, de manière semi-automatique.

Z3 seul est utile quand vous avez manuellement extrait les contraintes d'un algorithme (par lecture du désassemblage) et que vous voulez trouver une solution. C'est plus précis mais demande plus de travail manuel que angr.

---

## 9 — Diffing de binaires

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **BinDiff** | Comparaison structurelle de deux binaires (fonctions matchées, graphes de différences) | GUI (plugin Ghidra/IDA) | Oui | Propriétaire (Google) | Linux, macOS, Windows | 10.2 | ★★ |  
| **Diaphora** | Diffing de binaires open source (plugin Ghidra/IDA) | GUI (plugin) | Oui | AGPL | Linux, macOS, Windows | 10.3 | ★★ |  
| `radiff2` | Diffing en ligne de commande (suite Radare2) | CLI | Oui | LGPL3 | Linux, macOS, Windows | 10.4 | ★★ |

BinDiff (anciennement Zynamics, racheté par Google) est l'outil de diffing le plus mature. Il compare deux binaires au niveau des fonctions, des blocs de base et des graphes de flux, et produit une visualisation claire des différences. Il fonctionne comme plugin pour Ghidra ou IDA : vous exportez les analyses des deux binaires, puis BinDiff les compare.

Diaphora offre des fonctionnalités similaires en open source. Son avantage est sa capacité à détecter des fonctions similaires même quand elles ont été significativement modifiées (heuristiques de matching avancées).

`radiff2` est plus limité mais fonctionne entièrement en CLI, ce qui le rend scriptable et utilisable dans des pipelines automatisés.

---

## 10 — Parsing et manipulation programmatique d'ELF

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **pyelftools** | Parsing de fichiers ELF et DWARF en Python (lecture seule) | Lib Python | Oui | Domaine public | Linux, macOS | 35.1 | ★★ |  
| **LIEF** | Parsing et modification de binaires ELF, PE, Mach-O en Python/C++ | Lib Python/C++ | Oui | Apache 2.0 | Linux, macOS, Windows | 35.1 | ★★ |  
| **r2pipe** | Interface Python vers Radare2 (envoie des commandes, récupère les résultats) | Lib Python | Oui | LGPL3 | Linux, macOS, Windows | 9.4, 35.3 | ★★ |

pyelftools est la bibliothèque de choix pour lire un ELF en Python : extraire les headers, les sections, les symboles, les informations DWARF. Elle est en lecture seule — vous ne pouvez pas modifier le binaire avec.

LIEF va plus loin : il peut lire **et modifier** un binaire (ajouter/supprimer des sections, changer l'entry point, modifier les imports). C'est l'outil idéal pour le patching programmatique et l'instrumentation statique. Il supporte ELF, PE et Mach-O, ce qui le rend polyvalent.

r2pipe n'est pas un parser ELF mais une interface vers l'ensemble des capacités de Radare2 depuis Python. Son avantage est d'exploiter toute l'analyse de r2 (fonctions, xrefs, décompilation) plutôt que de reimplémenter un parser.

---

## 11 — Détection et analyse de menaces

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **YARA** | Détection de patterns binaires par règles (signatures de malware, constantes crypto, packers) | CLI (+ intégration ImHex) | Oui | Apache 2.0 (VirusTotal) | Linux, macOS, Windows | 6.10, 27.4, 35.4 | ★★ |  
| **UPX** | Packer/unpacker de binaires ELF et PE | CLI | Oui | GPL | Linux, macOS, Windows | 19.2, 29.1 | ★ |

YARA est le standard de l'industrie pour écrire des signatures de détection. Vous décrivez un pattern (séquence d'octets, chaînes, conditions logiques) et YARA le cherche dans un fichier ou un ensemble de fichiers. ImHex intègre un moteur YARA directement dans son interface (chapitre 6.10).

UPX est à la fois un outil de compression de binaires et un outil de RE : `upx -d` décompresse un binaire packé avec UPX. C'est souvent la première étape face à un binaire packé, avant de tenter des techniques d'unpacking plus avancées.

---

## 12 — Analyse réseau

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **Wireshark** | Capture et analyse de trafic réseau avec dissecteurs de protocoles | GUI (+`tshark` CLI) | Oui | GPL2 | Linux, macOS, Windows | 23.1 | ★★ |  
| `tcpdump` | Capture de paquets réseau en ligne de commande | CLI | Oui | BSD | Linux, macOS | 26.3 | ★★ |

Wireshark n'est pas un outil de RE à proprement parler, mais il est indispensable pour l'analyse de binaires réseau (chapitre 23). Quand vous analysez un client/serveur custom, la capture réseau Wireshark combinée à `strace` sur les appels socket est le meilleur moyen d'identifier le protocole.

---

## 13 — Sandbox et monitoring

| Outil | Usage principal | Interface | Gratuit | Licence | OS | Chapitres | Difficulté |  
|-------|----------------|-----------|---------|---------|-----|-----------|------------|  
| **QEMU** | Émulation système complet (VM pour sandbox d'analyse) ou émulation user-mode (pour le fuzzing AFL++) | CLI | Oui | GPL2 | Linux, macOS | 4.3, 26.2 | ★★ |  
| **VirtualBox** | Virtualisation de bureau (VM d'analyse avec snapshots) | GUI | Oui | GPL2 | Linux, macOS, Windows | 4.3 | ★ |  
| `auditd` | Monitoring des événements système (accès fichiers, exécutions, réseau) | CLI | Oui | GPL | Linux | 26.3 | ★★ |  
| `inotifywait` | Surveiller les modifications du système de fichiers en temps réel | CLI | Oui | GPL | Linux | 26.3 | ★ |  
| `sysdig` | Capture et filtrage d'événements système (alternative avancée à strace + auditd) | CLI | Oui (OSS) | Apache 2.0 | Linux | 26.3 | ★★ |

---

## 14 — Outils complémentaires de la suite Radare2

La suite Radare2 inclut plusieurs outils satellites couverts dans l'Annexe D. Voici un récapitulatif pour le comparatif :

| Outil | Usage principal | Interface | Chapitres |  
|-------|----------------|-----------|-----------|  
| `rasm2` | Assembleur/désassembleur en ligne de commande | CLI | 9.2 |  
| `rahash2` | Calcul de hash, entropie, encodage/décodage | CLI | 9.2 |  
| `radiff2` | Diffing de binaires | CLI | 10.4 |  
| `rafind2` | Recherche de patterns dans des fichiers | CLI | 9.2 |  
| `ragg2` | Générateur de patterns De Bruijn et shellcode | CLI | 9.2 |  
| `rarun2` | Lanceur d'exécution avec environnement contrôlé | CLI | 9.2 |  
| `rax2` | Convertisseur de bases et calculatrice | CLI | 9.2 |

---

## 15 — Tableau récapitulatif par cas d'usage

Ce tableau vous oriente directement vers le bon outil en fonction de ce que vous cherchez à faire.

| Besoin | Outil recommandé | Alternative |  
|--------|------------------|-------------|  
| Identifier un fichier inconnu | `file` + `strings` + `readelf` | `rabin2 -I` |  
| Vérifier les protections d'un binaire | `checksec` | `rabin2 -I`, GEF `checksec` |  
| Désassembler rapidement une fonction | `objdump -d -M intel` | `r2 -A` → `pdf` |  
| Analyse statique complète | **Ghidra** | IDA Pro, Binary Ninja |  
| Décompiler en pseudo-code C | **Ghidra** (décompilateur intégré) | IDA Pro (Hex-Rays), Cutter + r2ghidra |  
| Déboguer un binaire pas à pas | **GDB** + GEF/pwndbg | r2 en mode debug |  
| Tracer les appels système | `strace` | `sysdig` |  
| Tracer les appels de bibliothèque | `ltrace` | Frida |  
| Hooker des fonctions en live | **Frida** | `LD_PRELOAD`, GDB scripting |  
| Analyser le heap glibc | **pwndbg** (`vis_heap_chunks`) | GEF (`heap bins`) |  
| Visualiser un format binaire | **ImHex** (patterns `.hexpat`) | 010 Editor |  
| Comparer deux versions d'un binaire | **BinDiff** | Diaphora, `radiff2` |  
| Résoudre un crackme automatiquement | **angr** | Z3 (contraintes manuelles) |  
| Fuzzer un binaire sans le source | **AFL++** (mode QEMU) | — |  
| Fuzzer avec le source | **AFL++** (instrumentation) ou **libFuzzer** | — |  
| Détecter un packer / le retirer | `checksec` + **UPX** (`upx -d`) | ImHex (entropie), GDB (dump mémoire) |  
| Scanner des patterns (malware, crypto) | **YARA** | ImHex (recherche), `rafind2` |  
| Patcher un binaire sur le disque | **ImHex** ou `r2 -w` | LIEF (Python), `pwntools` |  
| Automatiser l'analyse de N binaires | **Ghidra headless** + Python | r2pipe + scripts Python |  
| Parser/modifier un ELF en Python | **LIEF** | pyelftools (lecture seule) |  
| Capturer du trafic réseau | **Wireshark** / `tcpdump` | `sysdig` |  
| Créer une sandbox d'analyse | **QEMU**/KVM ou **VirtualBox** | UTM (macOS) |

---

## 16 — Budget : construire un lab RE entièrement gratuit

L'intégralité de cette formation peut être suivie avec des outils **100 % gratuits**. Voici le toolkit recommandé pour un budget de zéro euro :

| Catégorie | Outil gratuit | Remplacement payant (si budget disponible) |  
|-----------|---------------|---------------------------------------------|  
| Désassembleur + décompilateur | Ghidra | IDA Pro |  
| Désassembleur CLI | Radare2 / objdump | — |  
| GUI pour r2 | Cutter + r2ghidra | Binary Ninja |  
| Débogueur | GDB + GEF ou pwndbg | — |  
| Éditeur hexadécimal | ImHex | 010 Editor |  
| Instrumentation dynamique | Frida | — |  
| Fuzzer | AFL++ | — |  
| Exécution symbolique | angr + Z3 | — |  
| Diffing | Diaphora | BinDiff (gratuit aussi) |  
| Scripting | pwntools + LIEF + pyelftools + r2pipe | — |  
| Virtualisation | QEMU / VirtualBox | VMware Workstation Pro |  
| Réseau | Wireshark + tcpdump | — |  
| Détection de patterns | YARA | — |

Tous ces outils sont couverts dans la formation et les commandes essentielles sont documentées dans les Annexes C, D et E.

---

> 📚 **Pour aller plus loin** :  
> - **Annexe H** — [Comparatif des outils .NET](/annexes/annexe-h-comparatif-outils-dotnet.md) — le même comparatif pour l'écosystème .NET/C#.  
> - **Annexe C** — [Cheat sheet GDB / GEF / pwndbg](/annexes/annexe-c-cheatsheet-gdb.md) — commandes détaillées du débogueur.  
> - **Annexe D** — [Cheat sheet Radare2 / Cutter](/annexes/annexe-d-cheatsheet-radare2.md) — commandes détaillées de r2.  
> - **Annexe E** — [Cheat sheet ImHex](/annexes/annexe-e-cheatsheet-imhex.md) — syntaxe `.hexpat` de référence.  
> - **Chapitre 4** — [Mise en place de l'environnement de travail](/04-environnement-travail/README.md) — installation et configuration de tous ces outils.  
> - **Chapitre 9, section 9.6** — [Comparatif Ghidra vs IDA vs Radare2 vs Binary Ninja](/09-ida-radare2-binja/06-comparatif-outils.md) — analyse détaillée des 4 désassembleurs majeurs.

⏭️ [Comparatif des outils .NET (ILSpy / dnSpy / dotPeek / de4dot)](/annexes/annexe-h-comparatif-outils-dotnet.md)
