🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 8 — Désassemblage avancé avec Ghidra

> **Partie II — Analyse Statique**

---

## Pourquoi ce chapitre ?

Les chapitres précédents vous ont permis de réaliser un premier contact avec le désassemblage à l'aide d'`objdump` et des outils Binutils. Vous avez appris à lire un listing assembleur, à distinguer les syntaxes AT&T et Intel, et à repérer des structures de base comme les prologues de fonctions ou le name mangling C++. Mais vous avez aussi constaté les limites de ces outils en ligne de commande : pas de navigation interactive, pas de décompilation, pas de typage automatique, pas de graphes de flux de contrôle. Pour aller plus loin dans l'analyse statique, il faut un véritable environnement de rétro-ingénierie.

C'est exactement ce que propose **Ghidra**.

## Ghidra en quelques mots

Ghidra est un framework d'analyse de binaires développé par la NSA (National Security Agency), rendu public et open source en 2019 sous licence Apache 2.0. Sa publication a profondément transformé le paysage du reverse engineering en offrant gratuitement un ensemble de fonctionnalités qui étaient jusqu'alors réservées à des outils commerciaux coûteux comme IDA Pro.

Ghidra n'est pas un simple désassembleur. C'est une suite complète qui intègre :

- un **désassembleur** multi-architecture (x86, x86-64, ARM, MIPS, PowerPC, et bien d'autres) ;  
- un **décompileur** capable de produire du pseudo-code C à partir du code machine, sans nécessiter de plugin additionnel ;  
- un **système de typage** permettant de définir et d'appliquer des structures, des énumérations et des types personnalisés ;  
- un **moteur de cross-references** (XREF) pour tracer l'usage de chaque fonction, variable ou constante à travers le binaire ;  
- un **éditeur de graphes** qui visualise le flux de contrôle d'une fonction sous forme de blocs basiques reliés par des arêtes ;  
- un **système de scripts** en Java et Python (via Jython) pour automatiser des tâches répétitives ;  
- un **mode headless** permettant de lancer des analyses en batch sur un grand nombre de binaires, sans interface graphique.

## Ce que vous allez apprendre

Ce chapitre vous guide pas à pas dans la maîtrise de Ghidra appliquée aux binaires ELF produits par GCC/G++. Vous apprendrez à :

1. **Installer Ghidra** et configurer votre environnement de travail, y compris la gestion des versions de Java et la structure des projets Ghidra.  
2. **Importer et analyser un binaire ELF**, en comprenant les options d'analyse automatique proposées par Ghidra et leur impact sur les résultats.  
3. **Naviguer efficacement dans le CodeBrowser**, l'interface centrale de Ghidra, en exploitant ses différentes vues : le listing assembleur, le décompileur, l'arbre de symboles et le graphe de fonctions.  
4. **Annoter un binaire** en renommant fonctions et variables, en ajoutant des commentaires, et en créant des types personnalisés pour rendre le désassemblage lisible et maintenable.  
5. **Reconnaître les structures spécifiques à GCC** dans un binaire C++ : vtables, RTTI (Run-Time Type Information), et tables d'exceptions.  
6. **Reconstruire des structures de données** (`struct`, `class`, `enum`) à partir du code désassemblé, en utilisant le Data Type Manager de Ghidra.  
7. **Exploiter les cross-references** (XREF) pour remonter les chaînes d'appels et comprendre comment une donnée ou une fonction est utilisée dans l'ensemble du programme.  
8. **Écrire des scripts Ghidra** en Java ou en Python pour automatiser des tâches d'analyse courantes : renommer des fonctions en masse, extraire des chaînes, appliquer des signatures.  
9. **Utiliser le mode headless** pour intégrer Ghidra dans un workflow d'analyse automatisé ou pour traiter un lot de binaires sans interaction manuelle.

## Positionnement dans le parcours

Ce chapitre suppose que vous maîtrisez les notions abordées dans les chapitres précédents :

- **Chapitre 2** — La chaîne de compilation GNU : vous devez comprendre comment un fichier source devient un binaire ELF, ce que sont les sections (`.text`, `.data`, `.rodata`, `.plt`, `.got`), et ce que signifie un binaire compilé avec ou sans symboles.  
- **Chapitre 3** — Assembleur x86-64 : vous devez savoir lire les instructions de base, comprendre les registres, les conventions d'appel System V AMD64, et interpréter les prologues/épilogues de fonctions.  
- **Chapitre 5** — Outils d'inspection de base : les outils de triage (`file`, `strings`, `readelf`, `nm`, `checksec`) doivent faire partie de votre routine.  
- **Chapitre 7** — Désassemblage avec `objdump` : vous devez avoir pratiqué le désassemblage en ligne de commande et ressenti les limitations qui motivent le passage à un outil graphique.

Les compétences acquises ici seront directement réutilisées dans :

- **Chapitre 9** — où vous comparerez Ghidra avec IDA Free, Radare2 et Binary Ninja ;  
- **Chapitre 10** — pour le diffing de binaires avec BinDiff et Diaphora, qui s'intègrent à Ghidra ;  
- **Chapitre 17** — pour le reverse engineering approfondi du C++ compilé avec GCC ;  
- **Chapitre 20** — pour la décompilation et la reconstruction de code source ;  
- **Partie V** (Chapitres 21 à 25) — où Ghidra sera votre outil principal d'analyse statique sur chaque cas pratique.

## Binaires d'entraînement

Les binaires utilisés dans ce chapitre se trouvent dans le répertoire `binaries/` du dépôt. Ce chapitre s'appuie principalement sur le binaire `ch08-oop` (application C++ orientée objet), fourni en plusieurs variantes :

| Variante | Optimisation | Symboles | Usage dans ce chapitre |  
|---|---|---|---|  
| `ch08-oop_O0` | `-O0` | Oui | Découverte de l'interface, première analyse |  
| `ch08-oop_O0_strip` | `-O0` | Non (`-s`) | Travailler sans symboles |  
| `ch08-oop_O2` | `-O2` | Oui | Observer l'impact des optimisations sur le décompileur |  
| `ch08-oop_O2_strip` | `-O2` | Non (`-s`) | Conditions réalistes d'analyse |

Pour compiler ces binaires :

```bash
cd binaries/ch08-oop/  
make all  
```

Vous utiliserez également le binaire `ch21-keygenme` pour certains exemples plus simples en C pur, ainsi que le binaire `mystery_bin` du Chapitre 5 si vous souhaitez reprendre votre triage initial dans Ghidra.

## Organisation du chapitre

| Section | Titre | Objectif |  
|---|---|---|  
| 8.1 | Installation et prise en main de Ghidra | Installer, configurer, créer un premier projet |  
| 8.2 | Import d'un binaire ELF — analyse automatique et options | Comprendre ce que fait l'auto-analyse |  
| 8.3 | Navigation dans le CodeBrowser | Maîtriser les vues Listing, Decompiler, Symbol Tree, Function Graph |  
| 8.4 | Renommage, commentaires et types | Annoter un binaire pour le rendre lisible |  
| 8.5 | Structures GCC : vtables, RTTI, exceptions | Reconnaître les artefacts C++ de GCC |  
| 8.6 | Reconstruire des structures de données | Créer des `struct`, `class`, `enum` dans Ghidra |  
| 8.7 | Cross-references (XREF) | Tracer l'usage de fonctions et données |  
| 8.8 | Scripts Ghidra (Java/Python) | Automatiser l'analyse |  
| 8.9 | Mode headless et traitement batch | Ghidra sans interface graphique |  
| 🎯 Checkpoint | Importer `ch20-oop`, reconstruire la hiérarchie de classes | Valider les acquis du chapitre |

---

> **💡 Conseil** — Ghidra est un outil riche dont l'interface peut sembler intimidante au premier abord. Ne cherchez pas à tout maîtriser d'un coup. Ce chapitre est conçu pour une progression incrémentale : chaque section construit sur la précédente. Prenez le temps de manipuler l'outil en parallèle de la lecture. Le reverse engineering est avant tout une discipline pratique.

---


⏭️ [Installation et prise en main de Ghidra (NSA)](/08-ghidra/01-installation-prise-en-main.md)
