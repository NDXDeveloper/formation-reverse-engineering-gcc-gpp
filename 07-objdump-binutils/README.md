🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 7 — Désassemblage avec objdump et Binutils

> 📦 **Binaires du chapitre** : `binaries/ch07-keygenme/` — les variantes `keygenme_O0`, `keygenme_O2`, `keygenme_O3`, `keygenme_strip` et `keygenme_O2_strip` sont utilisées tout au long de ce chapitre.  
> 🔧 **Outils requis** : `objdump`, `readelf`, `nm`, `c++filt` (paquet `binutils`), GCC/G++.  
> ⏱️ **Durée estimée** : 2 à 3 heures.

---

## Objectifs du chapitre

Au chapitre 5, nous avons fait connaissance avec `objdump` dans le cadre du triage rapide d'un binaire : afficher les headers, lister les sections, jeter un œil aux symboles. C'était un premier contact — on grattait la surface. Dans ce chapitre, on passe à l'étape suivante : **utiliser `objdump` comme un véritable outil de désassemblage**, capable de transformer les octets de la section `.text` en instructions assembleur lisibles.

À l'issue de ce chapitre, vous serez capable de :

- Désassembler un binaire ELF complet avec `objdump`, y compris lorsque les symboles ont été retirés avec `strip`.  
- Basculer entre la syntaxe AT&T (par défaut sous GNU) et la syntaxe Intel, et comprendre pourquoi cette dernière est souvent préférée en RE.  
- Comparer visuellement le code assembleur produit par GCC à différents niveaux d'optimisation (`-O0`, `-O2`, `-O3`) et interpréter les transformations appliquées par le compilateur.  
- Lire et annoter un prologue et un épilogue de fonction directement dans la sortie d'`objdump`.  
- Retrouver la fonction `main()` dans un binaire strippé et identifier des fonctions C++ malgré le *name mangling*.  
- Utiliser `c++filt` pour rendre les symboles C++ lisibles.  
- Reconnaître les limites d'`objdump` en tant que désassembleur linéaire, et comprendre pourquoi des outils comme Ghidra, IDA ou Radare2 deviennent indispensables sur des binaires complexes.

---

## Pourquoi `objdump` avant Ghidra ?

La question mérite d'être posée. Si Ghidra offre un décompilateur, un graphe de flux, des cross-references et une interface graphique riche (tout cela sera couvert au chapitre 8), pourquoi s'attarder sur un outil en ligne de commande qui produit un listing brut ?

Plusieurs raisons :

**`objdump` est universel et léger.** Il fait partie du paquet `binutils`, installé par défaut sur pratiquement toutes les distributions Linux. Pas de JVM à lancer, pas de projet à créer, pas d'analyse automatique à attendre. Un simple `objdump -d ./binary` et vous avez votre listing en une seconde. Sur un serveur distant accessible uniquement en SSH, sur une machine embarquée, ou dans un conteneur minimal, `objdump` est souvent le seul outil disponible.

**Il force la lecture directe de l'assembleur.** Quand on débute en RE, il est tentant de se reposer immédiatement sur le pseudo-code C généré par un décompilateur. C'est confortable, mais c'est aussi trompeur : le décompilateur fait des hypothèses, simplifie des constructions, et parfois se trompe. Travailler d'abord avec `objdump` vous oblige à lire les instructions une par une, à tracer les sauts manuellement, à comprendre ce que fait réellement le processeur. C'est un exercice fondamental qui rend ensuite l'utilisation de Ghidra beaucoup plus efficace, parce que vous saurez repérer les erreurs du décompilateur.

**Il sert de référence de comparaison.** Quand Ghidra ou IDA produisent un résultat surprenant, revenir au listing brut d'`objdump` permet de vérifier ce que contient réellement le binaire, sans couche d'interprétation. C'est votre « vérité terrain ».

En résumé, `objdump` n'est pas un outil du passé qu'on survole avant de passer aux choses sérieuses. C'est un outil que vous utiliserez encore régulièrement, même après avoir maîtrisé Ghidra.

---

## Rappel : la famille Binutils

`objdump` ne travaille pas seul. Il fait partie de la suite **GNU Binutils**, un ensemble d'outils en ligne de commande conçus pour manipuler les fichiers objets et les binaires. Vous en avez déjà croisé plusieurs au chapitre 5. Voici ceux qui interviennent dans ce chapitre :

| Outil | Rôle principal dans ce chapitre |  
|---|---|  
| `objdump` | Désassemblage, affichage des sections et headers |  
| `readelf` | Inspection fine des structures ELF (complémentaire à `objdump`) |  
| `nm` | Listing des symboles — repérer les fonctions avant de désassembler |  
| `c++filt` | Démanglement des symboles C++ (Itanium ABI) |  
| `strip` | Retrait des symboles — pour créer nos propres binaires strippés de test |  
| `objcopy` | Manipulation de sections (extraction, ajout) — mentionné en complément |

Ces outils partagent la même bibliothèque sous-jacente (`libbfd` — *Binary File Descriptor*) et comprennent nativement le format ELF. Ils fonctionnent aussi sur d'autres formats (PE, Mach-O dans une certaine mesure), mais c'est sur ELF qu'ils sont le plus à l'aise — ce qui tombe bien, puisque c'est notre cible tout au long de cette formation.

---

## Désassemblage linéaire vs. désassemblage récursif

Avant de plonger dans les commandes, il est essentiel de comprendre **comment** `objdump` désassemble, car cela conditionne ce qu'il peut et ne peut pas faire.

`objdump` utilise un **désassemblage linéaire** (*linear sweep*). Le principe est simple : il part du début de la section `.text` (ou de l'adresse que vous lui indiquez) et décode les octets séquentiellement, instruction après instruction, jusqu'à la fin de la section. Il ne suit pas les sauts, ne reconstruit pas le graphe de contrôle, et ne distingue pas le code des données qui pourraient être intercalées dans `.text`.

À l'inverse, les désassembleurs comme Ghidra, IDA et Radare2 utilisent un **désassemblage récursif** (*recursive descent*). Ils partent du point d'entrée, suivent les branchements (`call`, `jmp`, `jz`…), et ne désassemblent que les chemins effectivement atteignables. Cela leur permet de gérer correctement les données insérées au milieu du code, les *jump tables* du `switch`, et les fonctions indirectement appelées via des pointeurs.

Cette différence a des conséquences concrètes :

- **Données interprétées comme du code** : si le compilateur place une table de constantes ou une *jump table* dans `.text`, `objdump` tentera de les décoder comme des instructions. Vous verrez alors des séquences d'instructions absurdes — c'est un signal d'alerte classique.  
- **Désynchronisation** : les instructions x86-64 ont une longueur variable (de 1 à 15 octets). Si `objdump` « rate » le début d'une instruction (par exemple après avoir traversé des données), il peut se désynchroniser et produire des instructions fantaisistes sur plusieurs lignes avant de retomber sur ses pieds.  
- **Pas de graphe de flux** : `objdump` ne vous montrera jamais un graphe de fonctions ou de blocs de base. C'est du texte linéaire, point.

Ces limites ne rendent pas `objdump` inutile — loin de là. Sur un binaire compilé proprement avec GCC sans obfuscation, le désassemblage linéaire fonctionne remarquablement bien, parce que GCC sépare clairement le code et les données dans des sections distinctes. Les problèmes apparaissent surtout avec des binaires obfusqués, packés, ou compilés avec des options exotiques. Nous y reviendrons en détail à la section 7.7.

---

## Prérequis pour ce chapitre

Ce chapitre suppose que vous maîtrisez :

- Les **bases de l'assembleur x86-64** vues au chapitre 3 : registres, instructions courantes (`mov`, `call`, `ret`, `cmp`, `jz`…), conventions d'appel System V AMD64, notions de pile (prologue/épilogue).  
- Les **sections ELF** vues au chapitre 2 : au minimum `.text`, `.data`, `.rodata`, `.plt`, `.got` et leur rôle respectif.  
- L'utilisation basique de `readelf`, `nm` et `strings` vue au chapitre 5.

Si l'un de ces points vous semble flou, prenez le temps de relire les sections concernées avant de continuer. Tout ce qui suit repose sur ces fondations.

---

## Organisation du chapitre

Le chapitre suit une progression naturelle, du cas le plus simple au plus complexe :

- **7.1** — On commence par désassembler un binaire **sans symboles**, le cas le plus courant en RE réel, et on apprend à s'y retrouver malgré l'absence de noms de fonctions.  
- **7.2** — On aborde la question de la **syntaxe AT&T vs Intel**, avec un passage pratique de l'une à l'autre.  
- **7.3** — On compare le désassemblage d'un même programme compilé **avec différents niveaux d'optimisation** GCC (`-O0`, `-O2`, `-O3`), pour apprendre à reconnaître les transformations du compilateur.  
- **7.4** — On se concentre sur la **lecture du prologue et de l'épilogue** des fonctions dans un listing `objdump`, un réflexe fondamental pour délimiter les fonctions.  
- **7.5** — On apprend à **localiser `main()`** même sans symboles, et à repérer les fonctions C++ malgré le *name mangling*.  
- **7.6** — On découvre `c++filt`, l'outil de **démanglement** de la suite Binutils.  
- **7.7** — On termine par un tour d'horizon honnête des **limitations d'`objdump`**, pour comprendre quand il est temps de passer à un désassembleur récursif.

Chaque section s'appuie sur les binaires `keygenme` fournis dans `binaries/ch07-keygenme/`. Si vous ne les avez pas encore compilés, lancez `make` dans ce répertoire (ou `make all` à la racine de `binaries/`).

---

> 💡 **Conseil** : ouvrez un terminal à côté pendant votre lecture. Ce chapitre est conçu pour être suivi les mains sur le clavier. Chaque commande montrée est reproductible sur les binaires fournis — n'hésitez pas à expérimenter au-delà des exemples.

---

## Sections du chapitre

- 7.1 [Désassemblage d'un binaire compilé sans symboles (`-s`)](/07-objdump-binutils/01-desassemblage-sans-symboles.md)  
- 7.2 [Syntaxe AT&T vs Intel — passer de l'une à l'autre (`-M intel`)](/07-objdump-binutils/02-att-vs-intel.md)  
- 7.3 [Comparaison avec/sans optimisations GCC (`-O0` vs `-O2` vs `-O3`)](/07-objdump-binutils/03-comparaison-optimisations.md)  
- 7.4 [Lecture du prologue/épilogue de fonctions en pratique](/07-objdump-binutils/04-prologue-epilogue.md)  
- 7.5 [Identifier `main()` et les fonctions C++ (name mangling)](/07-objdump-binutils/05-identifier-main-mangling.md)  
- 7.6 [`c++filt` — démanglement des symboles C++](/07-objdump-binutils/06-cppfilt-demanglement.md)  
- 7.7 [Limitations d'`objdump` : pourquoi un vrai désassembleur est nécessaire](/07-objdump-binutils/07-limitations-objdump.md)  
- [**🎯 Checkpoint** : désassembler `keygenme_O0` et `keygenme_O2`, lister les différences clés](/07-objdump-binutils/checkpoint.md)

⏭️ [Désassemblage d'un binaire compilé sans symboles (`-s`)](/07-objdump-binutils/01-desassemblage-sans-symboles.md)
