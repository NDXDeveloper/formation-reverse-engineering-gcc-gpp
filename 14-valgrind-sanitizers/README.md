🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 14 — Analyse avec Valgrind et sanitizers

> 🎯 **Objectif du chapitre** : Exploiter les outils d'instrumentation mémoire et les sanitizers du compilateur pour observer le comportement runtime d'un binaire, détecter ses erreurs internes et en déduire sa logique de fonctionnement — le tout sans avoir accès au code source.

---

## Pourquoi ce chapitre dans une formation de Reverse Engineering ?

À première vue, Valgrind et les sanitizers (ASan, UBSan, MSan) sont des outils de **développement** : on les associe au débogage, à la chasse aux fuites mémoire, à la détection de comportements indéfinis. Alors pourquoi leur consacrer un chapitre entier dans un parcours de RE ?

La réponse tient en une phrase : **les erreurs d'un programme révèlent sa structure interne**.

Quand Valgrind signale une lecture non initialisée de 16 octets à l'adresse `0x5204a0`, il nous dit implicitement qu'un buffer de cette taille existe à cet endroit, qu'il est alloué mais pas encore rempli à ce stade de l'exécution, et que le code tente de l'utiliser comme donnée valide. Ce type d'information est de l'or pour le reverse engineer :

- **Les rapports de fuites mémoire dessinent la carte des allocations** — chaque `malloc` non libéré révèle la taille d'une structure, sa durée de vie et la fonction qui l'a créée.  
- **Les accès hors limites trahissent les tailles de buffers** — un overflow de 4 octets sur un bloc de 64 nous dit que le programme manipule probablement une structure de 64 octets et que l'indexation est erronée.  
- **Les lectures de mémoire non initialisée pointent vers les clés, IV et secrets éphémères** — les données cryptographiques sont souvent allouées puis remplies en plusieurs temps, et Valgrind capture précisément cet entre-deux.  
- **Les graphes d'appels de Callgrind exposent l'architecture fonctionnelle** — sans symboles, sans décompilateur, on obtient une cartographie complète de qui appelle qui, combien de fois, et avec quel coût.

En d'autres termes, ces outils nous offrent un **angle d'analyse dynamique complémentaire** à GDB et Frida. Là où GDB nous demande de savoir *où* poser un breakpoint et Frida de savoir *quelle fonction* hooker, Valgrind et les sanitizers travaillent de manière **passive et exhaustive** : ils instrumentent la totalité de l'exécution et remontent tout ce qui est anormal. Le reverse engineer n'a qu'à lire le rapport pour en extraire des hypothèses structurelles sur le binaire.

---

## Positionnement dans la boîte à outils du RE dynamique

Pour situer clairement ces outils par rapport à ce que nous avons déjà couvert :

| Outil | Mode d'action | Ce qu'on observe | Pré-requis |  
|---|---|---|---|  
| **GDB** (ch. 11-12) | Breakpoints, pas-à-pas | Registres, pile, mémoire à un instant *t* | Savoir *où* s'arrêter |  
| **Frida** (ch. 13) | Injection d'agent JS | Appels de fonctions, arguments, retours | Connaître les fonctions cibles |  
| **Valgrind** | Instrumentation binaire complète | Allocations, accès mémoire, fuites, graphe d'appels | Aucun — exécution instrumentée totale |  
| **Sanitizers** | Instrumentation à la compilation | Overflows, UB, lectures non initialisées | Pouvoir recompiler (ou disposer d'un build instrumenté) |

Valgrind ne nécessite **aucune recompilation** du binaire cible. Il fonctionne sur n'importe quel exécutable ELF, strippé ou non, optimisé ou non. C'est un avantage considérable en contexte de RE où l'on n'a généralement pas les sources.

Les sanitizers, eux, nécessitent de recompiler avec des flags spécifiques (`-fsanitize=address`, etc.). Dans le cadre de cette formation, nous disposons des sources de tous les binaires d'entraînement, ce qui nous permet d'explorer les deux approches. En situation réelle, on utilisera principalement Valgrind sur des binaires tiers, et les sanitizers lorsqu'on reconstruit un binaire modifié ou qu'on dispose d'un build de développement.

---

## Ce que nous allons couvrir

Ce chapitre se décompose en quatre sections :

**14.1 — Valgrind / Memcheck** : l'outil phare de la suite Valgrind. Nous verrons comment lancer un binaire sous Memcheck, lire et interpréter les rapports d'erreurs (lectures invalides, fuites, utilisation de mémoire non initialisée), et surtout comment **traduire ces rapports en informations de RE** exploitables — tailles de structures, durée de vie des buffers, flux de données sensibles.

**14.2 — Callgrind + KCachegrind** : le profileur d'appels de Valgrind. Sans disposer de symboles ni de décompilateur, Callgrind produit un graphe d'appels complet avec le nombre d'exécutions de chaque instruction. Couplé à KCachegrind pour la visualisation, c'est un moyen redoutable de **cartographier l'architecture fonctionnelle** d'un binaire inconnu et d'identifier ses hotspots (boucles crypto, parseurs, routines de validation).

**14.3 — AddressSanitizer (ASan), UBSan, MSan** : les sanitizers intégrés à GCC et Clang. Nous verrons comment recompiler nos binaires d'entraînement avec `-fsanitize=address,undefined` et interpréter les rapports produits. L'accent sera mis sur ce que ces rapports révèlent de la logique interne du programme, au-delà du simple diagnostic de bug.

**14.4 — Exploiter les rapports de sanitizers pour comprendre la logique interne** : une section de synthèse où nous appliquerons une méthodologie systématique pour transformer les sorties de Valgrind et des sanitizers en hypothèses de RE vérifiables — reconstruction de structures, identification de buffers de clés, compréhension de la gestion mémoire d'un programme.

---

## Pré-requis pour ce chapitre

Avant de commencer, assurez-vous d'être à l'aise avec :

- **GDB et le débogage dynamique** (chapitres 11–12) — nous ferons régulièrement le lien entre les adresses signalées par Valgrind et leur inspection dans GDB.  
- **Les bases de la gestion mémoire en C/C++** — `malloc`/`free`, pile vs tas, notion de buffer overflow et de lecture hors limites.  
- **La compilation avec GCC** (chapitre 2) — nous recompilerons certains binaires avec des flags spécifiques.

L'installation de Valgrind et des sanitizers a été couverte au chapitre 4. Si ce n'est pas déjà fait, vérifiez que `valgrind --version` retourne une version récente (3.20+) et que votre GCC supporte `-fsanitize=address` (GCC 4.8+ pour ASan, mais préférez GCC 12+ pour un support complet de tous les sanitizers).

---

## Binaires d'entraînement utilisés

Dans ce chapitre, nous travaillerons principalement avec :

- **`ch14-crypto`** (`binaries/ch14-crypto/`) — un binaire de chiffrement qui manipule des clés et des IV en mémoire. C'est une cible idéale pour Valgrind : les allocations de buffers cryptographiques laissent des traces très lisibles dans les rapports Memcheck.  
- **`ch14-keygenme`** (`binaries/ch14-keygenme/`) — notre crackme habituel, utilisé ici pour illustrer Callgrind et la cartographie fonctionnelle.  
- **`ch14-fileformat`** (`binaries/ch14-fileformat/`) — le parseur de format custom, utile pour démontrer comment les sanitizers révèlent la logique de parsing à travers les accès mémoire.

Chaque binaire est disponible à plusieurs niveaux d'optimisation. Pour ce chapitre, nous utiliserons principalement les versions **`-O0`** (plus lisibles dans les rapports Valgrind) et **`-O2`** (pour observer l'impact des optimisations sur les diagnostics).

---

## Conventions utilisées dans ce chapitre

> 💡 **Astuce RE** — Encadrés qui traduisent un diagnostic Valgrind ou sanitizer en information exploitable pour le reverse engineering.

> ⚠️ **Attention** — Points de vigilance sur les faux positifs, les limites d'interprétation ou les pièges courants.

> 🔧 **Commande** — Commandes shell à exécuter, avec les options expliquées.

Les sorties de Valgrind et des sanitizers sont reproduites telles quelles dans des blocs de code, avec des annotations `←` pour pointer les informations clés à extraire.

---

*Passons maintenant à notre premier outil : Valgrind et son module Memcheck.*


⏭️ [Valgrind / Memcheck — fuites mémoire et comportement runtime](/14-valgrind-sanitizers/01-valgrind-memcheck.md)
