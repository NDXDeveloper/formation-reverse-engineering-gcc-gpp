🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja

> 📘 **Partie II — Analyse Statique**  
> Prérequis : [Chapitre 7 — objdump et Binutils](/07-objdump-binutils/README.md), [Chapitre 8 — Ghidra](/08-ghidra/README.md)

---

## Pourquoi plusieurs désassembleurs ?

Le chapitre 7 nous a montré les limites d'`objdump` : pas de graphe de flux, pas de décompilation, une analyse linéaire qui se trompe dès qu'elle rencontre des données mêlées au code. Le chapitre 8 a présenté Ghidra comme une solution complète et gratuite à ces problèmes. Alors pourquoi consacrer un chapitre entier à d'autres outils ?

La réponse tient en un mot : **complémentarité**. Dans la pratique du reverse engineering, aucun outil ne domine systématiquement tous les autres sur chaque aspect. IDA reste la référence historique de l'industrie et son analyse initiale des binaires est souvent d'une précision redoutable. Radare2 offre une puissance inégalée en ligne de commande, idéale pour le scripting et l'automatisation. Binary Ninja propose une interface moderne et une API particulièrement bien conçue pour l'analyse programmatique. Chacun a ses forces, ses faiblesses, et ses situations où il excelle.

Un reverse engineer expérimenté ne jure pas par un seul outil. Il connaît les particularités de chacun et choisit celui qui convient le mieux à la tâche du moment — ou les combine. Il n'est pas rare de commencer une analyse dans Ghidra pour profiter du décompileur, de vérifier un point précis dans IDA dont l'heuristique de reconnaissance de fonctions est plus fiable sur certains binaires strippés, puis de scripter une extraction massive avec Radare2.

## Ce que ce chapitre couvre

Ce chapitre vous donne les clés pour prendre en main trois outils majeurs en complément de Ghidra, tous appliqués à des binaires ELF produits par GCC/G++ :

- **IDA Free** — la version gratuite du désassembleur le plus utilisé en industrie. Nous verrons comment importer un binaire, naviguer dans l'interface, et exploiter ses fonctionnalités d'analyse automatique sur un ELF x86-64. IDA Free a des limitations importantes par rapport à la version Pro (pas de décompileur pour x86-64 dans la version gratuite classique, pas de scripting IDAPython complet), mais son moteur d'analyse et sa reconnaissance de fonctions en font un outil précieux même dans sa déclinaison libre.

- **Radare2 et Cutter** — le couteau suisse open source du RE. Radare2 (`r2`) est un framework entièrement en ligne de commande, réputé pour sa courbe d'apprentissage abrupte mais aussi pour sa flexibilité exceptionnelle. Cutter en est l'interface graphique officielle, construite au-dessus de `r2`, qui rend l'outil accessible sans sacrifier la puissance du moteur sous-jacent. Nous couvrirons les commandes essentielles, la navigation, le mode visuel, et surtout le scripting via `r2pipe` qui permet de piloter `r2` depuis Python.

- **Binary Ninja Cloud** — la version gratuite et en ligne de Binary Ninja, un désassembleur plus récent qui s'est imposé par la qualité de son « Intermediate Language » (BNIL) et de son API. Nous verrons comment l'utiliser pour une prise en main rapide et en quoi son approche se distingue des outils précédents.

Le chapitre se conclut par un **comparatif structuré** entre Ghidra, IDA, Radare2 et Binary Ninja, couvrant les fonctionnalités, le modèle de licence, les cas d'usage privilégiés et les critères de choix.

## Positionnement par rapport au chapitre 8

Ce chapitre ne cherche pas à remplacer Ghidra dans votre workflow. Il vise à **élargir votre boîte à outils** et à vous donner suffisamment d'aisance avec chaque alternative pour pouvoir :

- Valider une analyse Ghidra avec un second avis (cross-checking entre décompileurs).  
- Choisir l'outil le plus adapté selon le contexte : script rapide en CLI, analyse collaborative, binaire exotique, contrainte de licence en entreprise.  
- Lire et comprendre les writeups de CTF et les rapports d'analyse qui utilisent IDA ou Radare2, car une grande partie de la littérature existante repose sur ces outils.

## Outils et versions utilisés

| Outil | Version recommandée | Licence | Installation |  
|---|---|---|---|  
| IDA Free | 8.x+ | Gratuit (propriétaire, usage non commercial) | [hex-rays.com/ida-free](https://hex-rays.com/ida-free) |  
| Radare2 | 5.9+ | LGPL v3 | `git clone` + `sys/install.sh` ou package manager |  
| Cutter | 2.3+ | GPL v3 | AppImage / package manager |  
| Binary Ninja Cloud | — | Gratuit (navigateur) | [cloud.binary.ninja](https://cloud.binary.ninja) |

> 💡 Les versions exactes utilisées pour les captures et exemples de ce chapitre sont documentées dans le fichier [`04-environnement-travail/02-installation-outils.md`](/04-environnement-travail/02-installation-outils.md). Si vous avez suivi le chapitre 4 et exécuté `check_env.sh`, Radare2 et Cutter devraient déjà être installés.

## Binaire fil rouge

Tout au long de ce chapitre, nous travaillerons principalement sur le binaire `keygenme_O2_strip` issu du dossier `binaries/ch09-keygenme/`. C'est un binaire compilé avec `-O2` puis strippé avec `strip` : pas de symboles, des fonctions inlinées, du code optimisé. C'est précisément le type de cible où les différences entre désassembleurs deviennent visibles et instructives. Nous utiliserons ponctuellement la version avec symboles (`keygenme_O0`) comme référence pour vérifier nos hypothèses.

## Plan du chapitre

- 9.1 — [IDA Free — workflow de base sur binaire GCC](/09-ida-radare2-binja/01-ida-free-workflow.md)  
- 9.2 — [Radare2 / Cutter — analyse en ligne de commande et GUI](/09-ida-radare2-binja/02-radare2-cutter.md)  
- 9.3 — [`r2` : commandes essentielles (`aaa`, `pdf`, `afl`, `iz`, `iS`, `VV`)](/09-ida-radare2-binja/03-r2-commandes-essentielles.md)  
- 9.4 — [Scripting avec r2pipe (Python)](/09-ida-radare2-binja/04-scripting-r2pipe.md)  
- 9.5 — [Binary Ninja Cloud (version gratuite) — prise en main rapide](/09-ida-radare2-binja/05-binary-ninja-cloud.md)  
- 9.6 — [Comparatif Ghidra vs IDA vs Radare2 vs Binary Ninja](/09-ida-radare2-binja/06-comparatif-outils.md)  
- 🎯 Checkpoint — [Analyser le même binaire dans 2 outils différents, comparer les résultats](/09-ida-radare2-binja/checkpoint.md)

---


⏭️ [IDA Free — workflow de base sur binaire GCC](/09-ida-radare2-binja/01-ida-free-workflow.md)
