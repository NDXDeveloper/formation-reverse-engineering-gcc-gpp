🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 20 — Décompilation et reconstruction du code source

> 📘 **Partie IV — Techniques Avancées de RE**  
>  
> **Prérequis** : Chapitres 7–9 (désassemblage avec objdump, Ghidra, IDA/Radare2/Binary Ninja), Chapitre 16 (optimisations compilateur), Chapitre 17 (RE du C++ avec GCC).

---

## Pourquoi ce chapitre ?

Le désassemblage produit une représentation instruction par instruction du code machine. C'est fidèle, mais difficile à raisonner : une fonction de 30 lignes en C peut engendrer plus de 200 lignes d'assembleur, sans compter les artefacts introduits par le compilateur — prologues, épilogues, registres intermédiaires, optimisations arithmétiques. La décompilation franchit une étape supplémentaire en tentant de reconstruire un pseudo-code de haut niveau à partir de ce flux d'instructions. Au lieu de lire des `mov`, `cmp` et `jnz`, on retrouve des `if`, des boucles `while` et des appels de fonctions avec des paramètres nommés.

Ce passage du désassemblage à la décompilation change radicalement la productivité de l'analyste. Un binaire de plusieurs milliers de fonctions devient navigable. Les structures de contrôle redeviennent lisibles. Les types de données peuvent être devinés, puis affinés manuellement. Mais cette lisibilité a un prix : le décompilateur fait des choix, des suppositions, et parfois des erreurs. Comprendre ce qu'il produit — et surtout ce qu'il ne peut pas produire — est une compétence fondamentale en reverse engineering.

## Ce que couvre ce chapitre

Ce chapitre aborde la décompilation sous un angle résolument pratique, appliqué à des binaires ELF compilés avec GCC/G++.

On commence par examiner **les limites intrinsèques de la décompilation automatique** : pourquoi le pseudo-code généré n'est jamais strictement équivalent au code source original, quelles informations sont irrémédiablement perdues lors de la compilation, et comment le niveau d'optimisation influence la qualité du résultat. Cette prise de conscience est essentielle pour éviter de traiter la sortie d'un décompilateur comme une vérité absolue.

On passe ensuite à la **pratique avec le décompilateur de Ghidra**, l'outil central de cette formation. On y analyse la qualité du pseudo-code produit selon différents niveaux d'optimisation (`-O0` à `-O3`), et on apprend à guider le décompilateur en renommant, retypant et restructurant les données pour obtenir un résultat exploitable. On aborde également **RetDec**, le décompilateur open source d'Avast, comme alternative offline et statique.

La seconde moitié du chapitre se concentre sur la **reconstruction concrète d'artefacts exploitables** à partir du pseudo-code. On apprend à produire un fichier `.h` synthétique qui capture les types, structures et signatures de fonctions découverts dans le binaire — un livrable tangible qui documente le travail de RE et peut servir de base pour écrire du code interagissant avec le binaire analysé. On aborde ensuite l'**identification de bibliothèques tierces embarquées** via les signatures FLIRT et les Function ID de Ghidra, une technique qui permet d'éliminer d'un coup des centaines de fonctions de l'analyse en les reconnaissant comme appartenant à une bibliothèque connue. Enfin, on traite l'**export et le nettoyage du pseudo-code** pour tendre vers un résultat recompilable — un objectif rarement atteint à 100 %, mais dont la poursuite structure l'analyse.

## Position dans le parcours

Ce chapitre ferme la Partie IV sur les techniques avancées. Il s'appuie sur tout ce qui précède : le désassemblage (chapitres 7–9), la compréhension des optimisations (chapitre 16), le reverse engineering des constructions C++ (chapitre 17), et la connaissance des protections anti-reversing (chapitre 19). Il prépare directement la Partie V, où l'on appliquera la décompilation sur des cas pratiques complets — le keygenme (chapitre 21), l'application orientée objet (chapitre 22), le binaire réseau (chapitre 23) et le binaire chiffré (chapitre 24).

## Outils utilisés dans ce chapitre

| Outil | Rôle dans ce chapitre |  
|---|---|  
| **Ghidra** (Decompiler) | Décompilation interactive, renommage, retypage, export du pseudo-code |  
| **RetDec** | Décompilation statique offline, comparaison avec Ghidra |  
| **FLIRT / Function ID** | Identification de bibliothèques tierces embarquées (signatures) |  
| **GCC/G++** | Compilation des binaires d'entraînement à différents niveaux d'optimisation |  
| **c++filt** | Démanglement des symboles C++ dans le pseudo-code |  
| **diff** | Comparaison entre pseudo-code décompilé et source original |

## Binaires d'entraînement

Les binaires utilisés dans ce chapitre proviennent du dossier `binaries/` et sont compilés à plusieurs niveaux d'optimisation via le `Makefile` dédié :

- `ch20-keygenme` — programme C simple, idéal pour observer l'impact des optimisations sur la décompilation.  
- `ch20-oop` — application C++ avec classes, vtables et héritage, pour tester la reconstruction de structures complexes.  
- `ch20-network` — binaire client/serveur, pour s'exercer à produire un `.h` à partir de la décompilation.

Chaque binaire est disponible en variantes `-O0`, `-O2`, `-O3`, avec et sans symboles (`-g` / `-s`), ce qui permet de comparer directement l'effet de chaque configuration sur la sortie du décompilateur.

## Plan du chapitre

- **20.1** — Limites de la décompilation automatique (pourquoi le résultat n'est jamais parfait)  
- **20.2** — Ghidra Decompiler — qualité selon le niveau d'optimisation  
- **20.3** — RetDec (Avast) — décompilation statique offline  
- **20.4** — Reconstruire un fichier `.h` depuis un binaire (types, structs, API)  
- **20.5** — Identifier des bibliothèques tierces embarquées (FLIRT / signatures Ghidra)  
- **20.6** — Exporter et nettoyer le pseudo-code pour produire un code recompilable

> **🎯 Checkpoint** : produire un `.h` complet pour le binaire `ch20-network` (le serveur réseau strippé).

---


⏭️ [Limites de la décompilation automatique (pourquoi le résultat n'est jamais parfait)](/20-decompilation/01-limites-decompilation.md)
