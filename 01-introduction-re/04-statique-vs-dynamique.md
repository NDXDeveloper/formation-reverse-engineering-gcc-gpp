🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 1.4 — Différence entre RE statique et RE dynamique

> **Chapitre 1 — Introduction au Reverse Engineering**  
> 📦 Aucun prérequis technique — section de lecture.  
> 📖 Les concepts introduits ici seront développés en profondeur dans les Parties II (analyse statique) et III (analyse dynamique).

---

## Deux familles d'approches, un seul objectif

Face à un binaire inconnu, le reverse engineer dispose de deux grandes familles de techniques pour en comprendre le fonctionnement. Ces deux approches se distinguent par un critère simple : **le programme est-il exécuté pendant l'analyse, ou non ?**

L'**analyse statique** consiste à examiner le binaire *sans l'exécuter*. On travaille sur le fichier tel qu'il est stocké sur le disque : sa structure, son contenu hexadécimal, son code désassemblé, son pseudo-code décompilé, ses chaînes de caractères, ses métadonnées.

L'**analyse dynamique** consiste à observer le programme *pendant son exécution*. On le lance (dans un environnement contrôlé) et on observe ce qu'il fait : quelles instructions il exécute, quelles valeurs passent dans les registres, quels fichiers il ouvre, quelles connexions réseau il établit, comment il réagit à différentes entrées.

Ces deux approches ne sont pas concurrentes — elles sont **complémentaires**. Chacune révèle des informations que l'autre ne peut pas fournir facilement. Un reverse engineer compétent maîtrise les deux et passe de l'une à l'autre en permanence au cours d'une même analyse.

---

## L'analyse statique

### Le principe

L'analyse statique traite le binaire comme un document à lire. On l'ouvre, on en examine la structure, on désassemble le code machine en instructions assembleur lisibles, et on tente de reconstruire la logique du programme par la lecture et le raisonnement.

L'analogie la plus directe serait celle d'un mécanicien qui étudie les plans d'un moteur — ou, à défaut de plans, qui démonte le moteur pièce par pièce et examine chaque composant — sans jamais le faire tourner.

### Ce que l'analyse statique permet de faire

**Obtenir une vue d'ensemble du binaire** — Avant même de lire une seule instruction assembleur, les outils d'inspection de base révèlent une quantité considérable d'informations : le format du fichier (ELF, PE, Mach-O), l'architecture cible (x86-64, ARM, MIPS), les sections et segments du binaire, les bibliothèques liées dynamiquement, les symboles exportés et importés, les chaînes de caractères embarquées, les protections activées (PIE, NX, canary, RELRO). C'est la phase de **triage** — les cinq premières minutes face à un binaire inconnu.

**Désassembler le code machine** — Le désassemblage transforme les octets bruts de la section `.text` en instructions assembleur lisibles. Un outil comme `objdump` produit un listing linéaire ; des désassembleurs plus sophistiqués comme Ghidra, IDA ou Radare2 effectuent une analyse récursive qui reconstruit le graphe de flux de contrôle (CFG — *Control Flow Graph*), identifie les fonctions, résout les références croisées et reconnaît certains patterns du compilateur.

**Décompiler vers un pseudo-code de haut niveau** — Les décompileurs (Ghidra Decompiler, Hex-Rays dans IDA, RetDec) vont un cran plus loin que le désassemblage : ils tentent de reconstruire un pseudo-code ressemblant à du C à partir des instructions assembleur. Le résultat n'est jamais parfait (les noms de variables sont perdus, les types sont approximatifs, les structures de contrôle sont parfois mal reconstruites), mais il accélère considérablement la compréhension de la logique globale.

**Analyser les données embarquées** — Un binaire ne contient pas que du code. Les sections `.data`, `.rodata` et `.bss` contiennent des constantes, des chaînes de caractères, des tables de valeurs, des structures initialisées. L'analyse hexadécimale avec un outil comme ImHex permet de visualiser ces données, d'y appliquer des patterns de décodage et d'identifier des éléments significatifs : constantes cryptographiques (S-box AES, IV SHA-256), tables de dispatch, formats de messages, clés codées en dur.

**Comparer deux versions d'un binaire** — Le *binary diffing* est une technique purement statique qui consiste à comparer deux versions d'un même programme pour identifier les fonctions modifiées, ajoutées ou supprimées. C'est un outil central pour l'analyse de patches de sécurité.

**Rechercher des patterns connus** — Les règles YARA permettent de scanner un binaire à la recherche de séquences d'octets, de chaînes ou de conditions structurelles qui correspondent à des signatures connues : familles de malware, bibliothèques cryptographiques embarquées, packers, signatures de compilateurs.

### Les limites de l'analyse statique

L'analyse statique est puissante mais elle bute sur plusieurs obstacles :

**Le code auto-modifiant et le packing** — Si un programme modifie son propre code en mémoire au moment de l'exécution (unpacking, déchiffrement de sections), l'analyse statique ne voit que le code chiffré ou compressé sur le disque — pas le code réel qui sera exécuté. Le binaire tel qu'il apparaît sur le disque n'est alors qu'une enveloppe qui masque le véritable programme.

**Les valeurs connues uniquement à l'exécution** — L'analyse statique ne peut pas déterminer la valeur concrète d'une variable qui dépend d'une entrée utilisateur, d'un fichier de configuration, d'une réponse réseau, de l'heure système ou de toute autre donnée externe. Elle peut identifier que « cette fonction compare l'entrée utilisateur avec une valeur dérivée d'un calcul sur `rdx` », mais elle ne peut pas toujours déterminer quelle valeur `rdx` contiendra réellement.

**L'appel indirect et le dispatch dynamique** — Quand le code exécute un `call rax` (appel indirect) ou résout une méthode virtuelle C++ via une vtable, l'analyse statique doit raisonner sur toutes les valeurs possibles de `rax` ou sur toutes les classes qui pourraient être pointées. En pratique, résoudre ces indirections de manière exhaustive est souvent impossible sans exécuter le programme.

**L'obfuscation de flux de contrôle** — Les techniques d'obfuscation comme le *control flow flattening* ou le *bogus control flow* transforment un graphe de flux lisible en un labyrinthe de blocs basiques reliés par un dispatcher central. L'analyse statique reste techniquement possible, mais le temps nécessaire explose.

**L'explosion combinatoire** — Un programme de taille réelle contient des milliers de fonctions, des millions d'instructions et des chemins d'exécution innombrables. L'analyse statique exhaustive d'un tel programme est un travail de plusieurs semaines ou mois. En pratique, l'analyste doit cibler les zones d'intérêt — et c'est souvent l'analyse dynamique qui lui indique *où* concentrer son effort.

### Les outils de cette formation pour l'analyse statique

| Outil | Rôle | Chapitres |  
|---|---|---|  
| `file`, `strings`, `xxd` | Triage rapide | 5 |  
| `readelf`, `objdump`, `nm` | Inspection de la structure ELF | 5, 7 |  
| `checksec` | Inventaire des protections | 5 |  
| ImHex | Analyse hexadécimale avancée, patterns `.hexpat` | 6 |  
| Ghidra | Désassemblage, décompilation, analyse de structures | 8 |  
| IDA Free, Radare2, Binary Ninja | Désassemblage et décompilation alternatifs | 9 |  
| BinDiff, Diaphora, `radiff2` | Diffing de binaires | 10 |  
| YARA | Détection de patterns et signatures | 6, 35 |

---

## L'analyse dynamique

### Le principe

L'analyse dynamique consiste à exécuter le programme et à observer son comportement en temps réel. On travaille avec le programme vivant : on le lance, on lui fournit des entrées, on place des points d'arrêt, on inspecte la mémoire, on intercepte les appels système, on modifie des valeurs à la volée.

Pour reprendre l'analogie du moteur : cette fois, le mécanicien fait tourner le moteur et observe son fonctionnement — il écoute les bruits, mesure les températures, vérifie les pressions, teste les réactions à différents régimes.

### Ce que l'analyse dynamique permet de faire

**Observer le comportement réel du programme** — L'analyse dynamique montre ce que le programme fait *effectivement* pour un ensemble d'entrées donné, sans ambiguïté. Elle ne montre pas ce que le programme *pourrait* faire dans d'autres circonstances (c'est une limitation), mais ce qu'elle montre est certain.

**Résoudre les valeurs concrètes** — Les registres, les variables en mémoire, les arguments de fonctions, les valeurs de retour — tout est observable pendant l'exécution. Ce qui était un `mov rdi, [rbp-0x18]` abstrait dans l'analyse statique devient « `rdi` vaut `0x7fffffffde30`, qui pointe vers la chaîne `"admin123"` ». Cette résolution concrète est souvent le moment décisif d'une analyse.

**Tracer les appels système et les appels de bibliothèque** — Les outils `strace` et `ltrace` permettent de capturer tous les appels système et les appels aux bibliothèques partagées effectués par le programme. Sans lire une seule instruction assembleur, vous pouvez savoir que le programme ouvre le fichier `/etc/shadow`, envoie un paquet UDP vers l'adresse `192.168.1.42:4444`, ou alloue 65 536 octets de mémoire.

**Déboguer pas à pas** — Un débogueur comme GDB permet d'exécuter le programme instruction par instruction, d'inspecter l'état complet de la machine (registres, pile, tas, flags) à chaque étape, et de poser des conditions d'arrêt sophistiquées (breakpoints conditionnels, watchpoints sur des zones mémoire, catchpoints sur des événements système).

**Modifier l'exécution en cours** — L'analyse dynamique ne se limite pas à l'observation passive. Avec GDB, vous pouvez modifier la valeur d'un registre ou d'une variable en mémoire pour forcer le programme à prendre une branche différente. Avec Frida, vous pouvez intercepter un appel de fonction, modifier ses arguments ou sa valeur de retour, et injecter du code JavaScript dans le processus cible — le tout sans modifier le binaire sur le disque.

**Analyser le code réellement exécuté après unpacking** — Quand un binaire est packé ou chiffré, l'analyse dynamique permet d'attendre que le code se décompresse ou se déchiffre en mémoire, puis de le capturer (dumper) dans son état déchiffré pour l'analyser statiquement. C'est la technique standard pour traiter les binaires packés.

**Explorer les chemins d'exécution par fuzzing** — Le fuzzing consiste à bombarder le programme avec des entrées générées aléatoirement ou par mutation, guidées par la couverture de code. Ce n'est pas une analyse manuelle, mais c'est une forme d'analyse dynamique automatisée qui explore des chemins d'exécution que l'analyste n'aurait peut-être jamais empruntés manuellement, et qui révèle des comportements inattendus (crashes, hangs, consommation mémoire anormale).

### Les limites de l'analyse dynamique

**La couverture partielle** — L'analyse dynamique n'observe que les chemins d'exécution effectivement empruntés lors du test. Si le programme contient une porte dérobée activée uniquement quand l'entrée est `"xK9#mZ$2"` un mardi à 3 h du matin, l'analyse dynamique ne la trouvera probablement pas — sauf si le fuzzer a de la chance ou si l'analyste sait quoi chercher.

**L'environnement d'exécution** — Le programme doit être exécuté, ce qui suppose un environnement compatible : le bon système d'exploitation, les bonnes bibliothèques, le bon matériel (ou un émulateur). Analyser dynamiquement un binaire ARM sur une machine x86-64 nécessite un émulateur comme QEMU, ce qui ajoute de la complexité et des limitations.

**Le risque d'exécution** — Exécuter un binaire inconnu comporte des risques. Si le binaire est un malware, il peut endommager le système, exfiltrer des données ou se propager sur le réseau. C'est pourquoi l'analyse dynamique de code potentiellement malveillant doit **toujours** se faire dans un environnement isolé (VM sandboxée, réseau coupé). Le chapitre 26 détaille la mise en place d'un tel lab.

**La détection d'analyse** — Certains programmes intègrent des techniques de détection de débogueur ou d'environnement d'analyse : vérification de la présence de `ptrace`, mesure de timing pour détecter le pas-à-pas, inspection de `/proc/self/status`, détection de machines virtuelles. Ces techniques compliquent l'analyse dynamique et nécessitent des contre-mesures spécifiques (traitées au chapitre 19).

**Le coût en temps pour les programmes complexes** — Déboguer un programme pas à pas est un processus lent. Un programme qui effectue un million d'itérations avant d'atteindre la zone d'intérêt rend le pas-à-pas impraticable. L'analyste doit alors combiner breakpoints intelligents, conditions d'arrêt, et connaissance préalable du code (obtenue par analyse statique) pour cibler directement les zones pertinentes.

### Les outils de cette formation pour l'analyse dynamique

| Outil | Rôle | Chapitres |  
|---|---|---|  
| `strace`, `ltrace` | Traçage des appels système et bibliothèque | 5 |  
| GDB | Débogage pas à pas, inspection mémoire | 11 |  
| GEF / pwndbg / PEDA | Extensions GDB (visualisation, heap, gadgets ROP) | 12 |  
| Frida | Instrumentation dynamique, hooking de fonctions | 13 |  
| Valgrind, sanitizers | Détection de bugs mémoire, profiling | 14 |  
| AFL++, libFuzzer | Fuzzing guidé par couverture | 15 |  
| angr | Exécution symbolique (hybride statique/dynamique) | 18 |

---

## Complémentarité en pratique

La force du reverse engineering réside dans la capacité à combiner les deux approches de manière fluide. En pratique, une analyse suit rarement un chemin purement statique ou purement dynamique. Le workflow typique ressemble plutôt à une boucle itérative :

### Le cycle statique → dynamique → statique

**1. Triage statique** — Premiers réflexes face au binaire : `file` pour identifier le format, `strings` pour repérer des chaînes de caractères révélatrices, `readelf` pour comprendre la structure, `checksec` pour inventorier les protections. En quelques minutes, vous avez une première idée de ce à quoi vous avez affaire.

**2. Analyse statique ciblée** — Vous ouvrez le binaire dans Ghidra ou un autre désassembleur. Vous localisez `main`, vous identifiez les fonctions clés, vous lisez le pseudo-code décompilé. Vous formez des hypothèses : « cette fonction semble vérifier un mot de passe en le comparant avec un hash SHA-256 », « ce bloc semble déchiffrer un buffer avec XOR ».

**3. Validation dynamique** — Vous lancez le programme dans GDB pour vérifier vos hypothèses. Vous posez un breakpoint sur la fonction suspecte, vous exécutez le programme avec une entrée connue, et vous observez les valeurs concrètes. L'hypothèse se confirme, se nuance ou s'effondre.

**4. Retour à l'analyse statique** — Les observations dynamiques orientent la suite de l'analyse statique. Vous savez maintenant que la fonction à l'adresse `0x401280` est la routine de vérification — vous pouvez concentrer votre lecture sur cette fonction et ses appelantes, tracer les références croisées, comprendre comment l'entrée utilisateur arrive jusqu'à la comparaison.

**5. Approfondissement dynamique** — Vous revenez à GDB ou Frida pour explorer un aspect précis : intercepter les arguments de `strcmp`, modifier une valeur de retour pour forcer une branche, dumper un buffer déchiffré en mémoire.

Ce cycle se répète autant de fois que nécessaire. Chaque itération apporte de nouvelles informations qui affinent la compréhension du programme.

### Un exemple concret

Imaginons un programme qui demande un mot de passe et affiche « Access Granted » ou « Access Denied ».

L'**analyse statique** vous permet de localiser les chaînes `"Access Granted"` et `"Access Denied"` dans la section `.rodata`, de remonter les références croisées pour trouver la fonction qui les utilise, et de lire le code désassemblé de cette fonction pour identifier la logique de comparaison. Vous voyez un `call` vers une fonction interne, un `test eax, eax`, puis un `jnz` qui saute vers le message d'erreur. Vous avez identifié la structure de la vérification, mais vous ne savez pas encore quel mot de passe est attendu — la valeur de comparaison est peut-être calculée dynamiquement.

L'**analyse dynamique** vous permet de poser un breakpoint juste avant la comparaison, de lancer le programme avec un mot de passe quelconque, et d'inspecter les registres et la mémoire à ce point précis. Vous voyez la valeur attendue en clair dans un registre ou sur la pile. Le mot de passe est trouvé.

Aucune des deux approches n'aurait suffi seule de manière optimale. L'analyse statique seule aurait pu fonctionner, mais aurait exigé de comprendre en détail l'algorithme de dérivation de la clé — un travail potentiellement long. L'analyse dynamique seule aurait pu fonctionner aussi, mais sans savoir où poser le breakpoint, l'analyste aurait tâtonné. C'est la combinaison des deux qui produit un résultat rapide et fiable.

---

## Le cas particulier de l'exécution symbolique

L'**exécution symbolique** (chapitre 18) mérite une mention à part, car elle se situe à la frontière entre les deux approches. Le principe est de « simuler » l'exécution du programme en remplaçant les entrées concrètes par des **variables symboliques** — des inconnues mathématiques. Au lieu de calculer `x + 5 = 12` avec `x = 7`, l'exécution symbolique propage le symbole `x` à travers le programme et accumule des **contraintes** à chaque branchement conditionnel.

Quand un chemin d'exécution atteint un point d'intérêt (la branche « Access Granted », par exemple), l'outil passe les contraintes accumulées à un **solveur de contraintes** (Z3, typiquement) qui calcule une valeur concrète de l'entrée satisfaisant toutes les contraintes — c'est-à-dire un mot de passe valide.

L'exécution symbolique n'exécute pas le programme au sens classique du terme (elle ne produit pas d'appels système réels), mais elle ne se contente pas non plus de lire le code statiquement (elle explore les chemins d'exécution). C'est une technique **hybride** qui combine le raisonnement statique sur le code avec l'exploration systématique des chemins — et elle est redoutablement efficace sur certains types de problèmes, notamment les crackmes et les vérifications de licences.

> 🔬 **Approfondissement** — L'exécution symbolique a des limites bien connues, notamment l'**explosion de chemins** (le nombre de chemins possibles croît exponentiellement avec la taille du programme) et la difficulté à modéliser les appels système et les interactions avec l'environnement. Le chapitre 18 détaille ces limites et les stratégies pour les contourner.

---

## Récapitulatif

| | Analyse statique | Analyse dynamique |  
|---|---|---|  
| **Exécution du binaire** | Non | Oui |  
| **Ce qu'elle observe** | Le binaire sur le disque : code, données, structure | Le programme en cours d'exécution : registres, mémoire, comportement |  
| **Couverture** | Totale en théorie (tout le code est visible) | Partielle (seuls les chemins empruntés sont observés) |  
| **Valeurs des variables** | Inconnues ou à déduire par raisonnement | Concrètes, observables directement |  
| **Code packé/chiffré** | Invisible (vu sous forme chiffrée) | Visible après décompression en mémoire |  
| **Risque d'exécution** | Aucun | Réel (malware, effets de bord) — nécessite un sandbox |  
| **Appels indirects** | Difficiles à résoudre | Résolus naturellement par l'exécution |  
| **Anti-analyse** | Obfuscation de flux, faux graphes | Détection de débogueur, timing checks |  
| **Outils principaux (cette formation)** | `readelf`, Ghidra, ImHex, YARA | GDB, Frida, `strace`, AFL++ |

---

> 📖 **À retenir** — L'analyse statique et l'analyse dynamique sont les deux piliers complémentaires du reverse engineering. L'analyse statique offre une vue exhaustive mais abstraite du binaire. L'analyse dynamique offre des observations concrètes mais partielles. Le reverse engineer efficace combine les deux dans un cycle itératif : observer statiquement pour formuler des hypothèses, exécuter dynamiquement pour les valider, puis revenir à l'analyse statique avec les informations obtenues. C'est cette alternance que cette formation enseigne.

---


⏭️ [Vue d'ensemble de la méthodologie et des outils du tuto](/01-introduction-re/05-methodologie-outils.md)
