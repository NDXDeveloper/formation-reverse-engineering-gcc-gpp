🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Importer `ch08-oop` dans Ghidra, reconstruire la hiérarchie de classes

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Objectif

Ce checkpoint valide l'ensemble des compétences acquises dans le Chapitre 8 en les mobilisant sur un cas concret : l'analyse complète d'un binaire C++ orienté objet dans Ghidra. Le binaire cible est `ch08-oop_O0` (compilé sans optimisation, avec symboles), fourni dans `binaries/ch08-oop/`.

L'objectif final est de produire un **document de synthèse** (Markdown ou texte) qui reconstruit la hiérarchie de classes du programme : les noms des classes, leurs relations d'héritage, leurs méthodes virtuelles, et la structure de leurs champs. Ce document doit être produit uniquement à partir de l'analyse dans Ghidra — sans consulter le code source.

> 💡 Le code source `oop.cpp` est disponible dans le même répertoire pour vérification **a posteriori**. Ne le consultez qu'après avoir terminé votre reconstruction pour évaluer la précision de votre travail.

---

## Compétences évaluées

Ce checkpoint couvre les sections suivantes du chapitre :

| Section | Compétence mobilisée |  
|---|---|  
| 8.1 | Créer un projet dédié, importer le binaire correctement |  
| 8.2 | Lancer l'analyse automatique avec les options appropriées pour un binaire C++ |  
| 8.3 | Naviguer dans le CodeBrowser : localiser `main`, explorer les fonctions via le Symbol Tree, lire le Decompiler, utiliser le Function Graph |  
| 8.4 | Renommer les fonctions et variables, ajouter des commentaires, modifier les signatures de fonctions |  
| 8.5 | Identifier et interpréter les vtables, exploiter le RTTI pour retrouver les noms de classes et les relations d'héritage, reconnaître les patterns d'exceptions |  
| 8.6 | Reconstruire les structures de données des classes dans le Data Type Manager |  
| 8.7 | Utiliser les cross-references pour tracer les appels de méthodes, relier les vtables aux constructeurs, remonter depuis les chaînes de caractères |  
| 8.8 | *(Optionnel)* Écrire un script qui automatise l'extraction de la hiérarchie |  
| 8.9 | *(Optionnel)* Réaliser l'import et l'analyse via `analyzeHeadless` |

---

## Binaire cible

```bash
cd binaries/ch08-oop/  
make all  
```

Travaillez sur la variante **`ch08-oop_O0`** pour ce checkpoint. C'est le cas le plus favorable (pas d'optimisation, symboles présents), ce qui vous permet de valider votre méthodologie sans la complexité supplémentaire du stripping ou de l'inlining.

Si vous souhaitez aller plus loin, répétez l'analyse sur `ch08-oop_O2_strip` (optimisé et strippé) pour mesurer la perte d'information et adapter votre approche.

---

## Livrables attendus

### 1. Diagramme de hiérarchie de classes

Un schéma textuel ou graphique montrant les classes identifiées et leurs relations d'héritage. Format suggéré :

```
ClasseBase
├── ClasseDerivee1
└── ClasseDerivee2
    └── ClasseDerivee3
```

Pour chaque relation d'héritage, indiquez comment vous l'avez identifiée (RTTI, vtable partagée, appel de constructeur parent).

### 2. Description de chaque classe

Pour chaque classe identifiée, documentez :

- **Nom** — tel qu'extrait du RTTI ou des symboles.  
- **Taille totale** — déduite de l'argument de `operator new` ou de l'analyse des champs.  
- **Vtable** — adresse de la vtable, nombre d'entrées, liste des méthodes virtuelles avec leur nom (ou un nom attribué par vous si le binaire était strippé).  
- **Champs de données** — offset, type, nom attribué et justification. Distinguez les champs hérités des champs propres à la classe.  
- **Constructeur(s) et destructeur(s)** — adresses identifiées, indices qui ont permis leur identification (initialisation du vptr, appel au constructeur parent, appel à `operator new`).  
- **Classe abstraite ou concrète** — et comment vous l'avez déterminé (présence de `__cxa_pure_virtual` dans la vtable, ou instanciation directe observée).

### 3. Structures Ghidra

Créez dans le Data Type Manager de Ghidra une structure pour chaque classe identifiée, avec les champs nommés et typés. Appliquez ces structures aux paramètres `this` des méthodes correspondantes. Le pseudo-code du Decompiler doit refléter vos annotations (accès par noms de champs plutôt que par offsets bruts).

### 4. *(Optionnel)* Script d'extraction

Écrivez un script Python Ghidra qui automatise l'extraction de la hiérarchie : il parcourt les structures RTTI, suit les pointeurs de classes parentes, et produit un résumé sur la Console ou dans un fichier JSON.

---

## Méthodologie recommandée

Le checkpoint ne prescrit pas une procédure unique — l'objectif est justement que vous construisiez votre propre workflow en combinant les techniques du chapitre. Cependant, voici un rappel des points d'entrée les plus efficaces pour ce type d'analyse.

### Points d'entrée recommandés

**Le Symbol Tree** est votre meilleur allié sur un binaire avec symboles. Les fonctions sont organisées par namespace et par classe, ce qui révèle immédiatement la structure du programme. Parcourez les namespaces pour identifier les classes et leurs méthodes.

**Le RTTI** fournit les noms de classes et les liens d'héritage. Cherchez les chaînes de typeinfo names dans Defined Strings ou les labels `typeinfo for *` dans le Symbol Tree. Suivez les pointeurs `__base_type` pour reconstruire les chaînes d'héritage.

**Les vtables** listent les méthodes virtuelles de chaque classe. Comparez les vtables parent et enfant pour identifier quelles méthodes sont surchargées (pointeurs différents) et lesquelles sont héritées (pointeurs identiques).

**Les constructeurs** sont le meilleur endroit pour observer la taille totale d'un objet (argument de `operator new`) et le layout de ses champs (initialisations séquentielles). L'écriture du vptr à `[this + 0]` confirme l'adresse de la vtable.

**Les cross-references** permettent de relier tous ces éléments : XREF `(*)` vers une vtable pour trouver les constructeurs, XREF `(c)` vers un constructeur pour trouver les sites d'instanciation, XREF `(r)` vers une chaîne pour trouver le code qui l'utilise.

### Pièges courants

**Confondre les deux destructeurs** — Rappel de la section 8.5 : GCC génère un *complete object destructor* (D1) et un *deleting destructor* (D0) pour chaque classe avec un destructeur virtuel. Ce sont deux entrées distinctes dans la vtable, pas deux destructeurs différents dans le code source.

**Oublier le vptr dans le layout** — Le vptr est le premier champ de tout objet polymorphe (offset 0, 8 octets). Il n'apparaît pas dans le code source mais il est bien présent dans le binaire. Votre structure doit l'inclure.

**Confondre champs hérités et champs propres** — Les champs de la classe parente sont présents dans l'objet de la classe dérivée, aux mêmes offsets. Si `Animal` a un champ `name` à l'offset 0x10 et que `Dog` hérite de `Animal`, alors `Dog` a aussi un champ à l'offset 0x10 — c'est le même champ hérité, pas un champ propre à `Dog`.

**Négliger le padding** — L'alignement des champs peut créer des trous dans la structure. Si vous observez un « saut » d'offset entre deux champs (par exemple, un champ `char` à l'offset 0x08 et le champ suivant à l'offset 0x10), il y a du padding intercalé. La taille totale de la structure peut être supérieure à la somme des tailles de ses champs.

---

## Critères de validation

Votre reconstruction est réussie si :

- ✅ Toutes les classes du programme sont identifiées et nommées.  
- ✅ Les relations d'héritage sont correctes et documentées avec les preuves (RTTI, vtable, constructeur).  
- ✅ Les méthodes virtuelles de chaque classe sont listées et correctement attribuées (surchargée vs héritée).  
- ✅ Les structures dans le Data Type Manager de Ghidra reflètent le layout mémoire réel, avec des champs nommés et typés.  
- ✅ Le pseudo-code du Decompiler pour les méthodes principales utilise les noms de champs plutôt que les offsets bruts, grâce à l'application de vos structures.  
- ✅ Le document de synthèse est clair, structuré, et traçable (chaque affirmation est reliée à un indice observable dans Ghidra).

Comparez ensuite votre reconstruction avec le code source `oop.cpp` pour évaluer votre précision. Les écarts les plus courants concernent les noms de champs (impossibles à retrouver sans symboles DWARF) et l'ordre exact des méthodes non-virtuelles (qui n'apparaissent pas dans les vtables).

---

## Pour aller plus loin

Si vous avez complété le checkpoint sur `ch08-oop_O0` et souhaitez approfondir, voici deux extensions :

**Extension 1 — Binaire strippé.** Répétez l'analyse sur `ch08-oop_O2_strip`. Les symboles de fonctions sont absents, mais le RTTI est toujours présent (sauf compilation avec `-fno-rtti`). Votre workflow devra s'appuyer davantage sur le RTTI et les patterns structurels que sur le Symbol Tree. Comparez le temps et la difficulté avec la version non-strippée.

**Extension 2 — Script d'extraction automatique.** Écrivez un script Python Ghidra qui parcourt automatiquement toutes les structures RTTI du binaire, reconstruit l'arbre d'héritage, et produit un rapport formaté. Ce script pourra être réutilisé sur n'importe quel binaire C++ analysé dans Ghidra. Le Chapitre 35 vous donnera des techniques supplémentaires pour industrialiser ce type de script.

---


⏭️ [Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja](/09-ida-radare2-binja/README.md)
