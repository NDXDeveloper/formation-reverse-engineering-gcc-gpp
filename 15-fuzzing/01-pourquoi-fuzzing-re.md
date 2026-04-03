🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 15.1 — Pourquoi le fuzzing est un outil de RE à part entière

> 🔗 **Prérequis** : Chapitre 1 (différence RE statique / dynamique), Chapitre 5 (workflow de triage), Chapitre 14 (sanitizers)

---

## Le problème : l'angle mort de l'analyste

Reprenons la méthodologie que nous avons construite depuis le début de cette formation. Face à un binaire inconnu, l'approche classique suit deux axes complémentaires :

**L'analyse statique** (Parties II et IV) consiste à lire le binaire sans l'exécuter. On inspecte les sections ELF, on parcourt le désassemblage dans Ghidra, on reconstruit les structures de données, on renomme les fonctions. C'est un travail de lecture — méthodique, précis, mais entièrement guidé par l'intuition et l'expérience du reverse engineer. On choisit de regarder `main()`, puis on suit les appels, on repère un `strcmp`, on remonte vers la routine de vérification. Le risque ? **Passer à côté d'un chemin d'exécution qu'on n'a pas pensé à explorer.** Un parseur avec 47 branches conditionnelles ne révèle pas ses secrets à la simple lecture — surtout compilé en `-O2` avec des fonctions inlinées.

**L'analyse dynamique** (Chapitres 11 à 14) exécute le binaire et observe son comportement. On pose des breakpoints dans GDB, on hooke des fonctions avec Frida, on trace les appels système avec `strace`. C'est plus concret que la lecture statique, mais cela reste **dirigé par l'analyste** : on choisit quels inputs fournir, où poser les breakpoints, quelles fonctions hooker. Si on ne devine pas le bon input, on n'atteint pas le bon chemin. On observe un sous-ensemble du comportement du programme — celui qu'on a su provoquer.

Entre ces deux approches, il existe un angle mort considérable : **tous les chemins d'exécution que l'analyste n'a ni lus ni déclenchés**. C'est précisément dans cet angle mort que se cachent les logiques de parsing les plus complexes, les gestionnaires d'erreurs rarement atteints, les fonctionnalités non documentées, et — dans un contexte de sécurité — les vulnérabilités.

---

## Le fuzzing comme exploration automatisée

Le fuzzing comble cet angle mort en inversant la logique : au lieu que l'analyste choisisse les inputs, **c'est le programme lui-même qui guide l'exploration**, via un mécanisme de rétroaction appelé *coverage feedback* (retour de couverture).

Le principe est simple dans son concept :

1. On fournit au fuzzer un ou plusieurs **inputs initiaux** (le *corpus de départ* ou *seed corpus*).  
2. Le fuzzer **mute** ces inputs — il change des octets, en insère, en supprime, en recombine.  
3. Chaque input muté est envoyé au programme cible.  
4. Le fuzzer observe si cet input a **déclenché un nouveau chemin** dans le code (une branche jamais prise, un bloc de base jamais atteint).  
5. Si oui, l'input est **conservé** dans le corpus et servira de base pour de futures mutations.  
6. Si le programme **crashe**, l'input est sauvegardé séparément pour analyse.  
7. Le cycle recommence, des milliers de fois par seconde.

Ce mécanisme de *coverage-guided fuzzing* (fuzzing guidé par la couverture) signifie que le fuzzer **apprend** progressivement la structure attendue par le programme. Sans aucune connaissance préalable du format d'entrée, il finit par produire des inputs qui passent les premières validations, atteignent des couches de parsing plus profondes, et révèlent des comportements que l'analyste n'aurait pas soupçonnés.

> 💡 **Analogie** — Imaginez un labyrinthe dont vous ne connaissez pas le plan. L'analyse statique, c'est regarder le labyrinthe d'en haut sans y entrer — utile, mais certains couloirs sont masqués. L'analyse dynamique, c'est y entrer avec une lampe torche en choisissant les embranchements — on explore ce qu'on décide d'explorer. Le fuzzing, c'est envoyer des milliers de robots qui prennent chacun des chemins différents et vous rapportent ce qu'ils ont trouvé.

---

## Ce que le fuzzing révèle à un reverse engineer

Le fuzzing n'est pas seulement un outil de détection de bugs. Pour un reverse engineer, chaque résultat produit par le fuzzer est une **source d'information** sur le binaire :

### Les crashs comme indices structurels

Un crash n'est pas juste un bug à corriger — c'est une **fenêtre ouverte sur la logique interne** du programme. Quand le fuzzer produit un input qui déclenche un segfault dans une fonction située à l'offset `0x4012a0`, on apprend plusieurs choses d'un coup :

- Cette fonction est **atteignable** depuis l'entrée du programme (ce qui n'était peut-être pas évident à la lecture du graphe d'appels).  
- L'input qui a provoqué le crash nous indique **quel chemin de parsing** mène à cette fonction.  
- La nature du crash (lecture hors limites, déréférencement nul, overflow de pile) nous renseigne sur **la façon dont cette fonction manipule les données**.

En combinant cette information avec Ghidra ou GDB, on peut reconstruire la chaîne complète : de l'entrée du programme jusqu'au point de crash, en passant par chaque validation, chaque branchement, chaque transformation des données. Un seul crash bien analysé peut débloquer la compréhension d'un module entier.

### La couverture comme carte du binaire

Les fuzzers modernes comme AFL++ maintiennent une **carte de couverture** (*coverage bitmap*) qui enregistre quels blocs de base ont été exécutés et quelles transitions entre blocs ont été observées. Cette carte est une mine d'or pour le RE :

- **Les zones couvertes** correspondent aux chemins que le fuzzer a su atteindre. On peut les superposer au graphe de fonctions dans Ghidra pour visualiser quelles parties du code sont effectivement utilisées pour le traitement des entrées.  
- **Les zones non couvertes** sont tout aussi intéressantes : elles indiquent soit du code mort, soit des chemins protégés par des conditions que le fuzzer n'a pas su satisfaire — ce qui oriente l'analyse manuelle vers ces conditions précises.

Nous verrons en section 15.5 comment extraire et visualiser ces cartes de couverture avec `afl-cov` et `lcov`.

### Les inputs survivants comme spécification implicite

Le corpus que le fuzzer accumule au fil du temps constitue, de fait, une **collection d'inputs valides** (ou presque valides) qui exercent différents chemins dans le programme. En examinant ces inputs, on peut reconstituer des fragments de la spécification du format d'entrée :

- Quels *magic bytes* sont attendus en début de fichier ?  
- Quels champs ont des contraintes de taille ou de valeur ?  
- Quelles combinaisons de flags activent quelles branches ?

Cette approche « par l'extérieur » est particulièrement précieuse quand le format est propriétaire et non documenté — exactement le scénario du Chapitre 25.

---

## Complémentarité avec les autres approches

Le fuzzing ne remplace ni l'analyse statique ni l'analyse dynamique. Il les **alimente**. Voici comment les trois approches s'articulent dans une méthodologie de RE complète :

**Statique → Fuzzing.** L'analyse statique dans Ghidra permet d'identifier les fonctions de parsing, les routines de validation, et les formats attendus. Ces informations servent à construire un **corpus initial pertinent** et un **dictionnaire** de tokens pour le fuzzer (section 15.6). Plus le corpus de départ est proche du format réel, plus vite le fuzzer atteindra les couches profondes du parseur.

**Fuzzing → Statique.** Les crashs et la carte de couverture produits par le fuzzer orientent le retour à l'analyse statique. Au lieu de lire le binaire de façon linéaire, on sait désormais **quelles fonctions méritent une attention particulière** — celles qui crashent, celles qui traitent des branches rarement atteintes, celles qui n'ont pas été couvertes du tout.

**Fuzzing → Dynamique.** Chaque crash produit un **input reproductible** qu'on peut rejouer dans GDB pour observer exactement ce qui se passe au moment du bug. Le workflow typique est : le fuzzer trouve le crash, GDB (avec GEF ou pwndbg) permet de l'analyser instruction par instruction, et Frida peut hooker les fonctions traversées pour capturer les transformations de données en live.

**Dynamique → Fuzzing.** Les observations faites sous GDB ou Frida permettent d'affiner la stratégie de fuzzing. Si on découvre qu'une certaine valeur de checksum est attendue à l'offset 8 du fichier, on peut modifier le harness de fuzzing pour calculer ce checksum automatiquement — ce qui débloque l'accès à des couches de parsing que le fuzzer seul n'aurait jamais atteintes.

```
                    ┌──────────────────────┐
                    │   Analyse Statique   │
                    │  (Ghidra, objdump)   │
                    └──────┬───────▲───────┘
                           │       │
           corpus initial, │       │ fonctions à
             dictionnaire  │       │ examiner
                           │       │
                    ┌──────▼───────┴───────┐
                    │       Fuzzing        │
                    │   (AFL++, libFuzzer) │
                    └──────┬───────▲───────┘
                           │       │
              crashs,      │       │ harness affiné,
              inputs       │       │ checksum fixé
                           │       │
                    ┌──────▼───────┴───────┐
                    │  Analyse Dynamique   │
                    │  (GDB, Frida, strace)│
                    └──────────────────────┘
```

Ce cycle itératif — statique, fuzzing, dynamique, puis retour — est au cœur de la méthodologie moderne de reverse engineering sur des binaires complexes.

---

## Fuzzing « black-box » vs « grey-box » vs « white-box »

Il est utile de distinguer trois niveaux de fuzzing, car le contexte de RE détermine lequel est applicable :

**Black-box fuzzing.** On ne dispose d'aucune instrumentation du binaire. On lui envoie des inputs et on observe uniquement s'il crashe ou non — via le code de retour, `strace`, ou un monitoring externe. C'est l'approche la plus limitée, mais c'est parfois la seule option quand on n'a pas les sources et qu'on ne peut pas instrumenter le binaire (firmware embarqué, binaire protégé). La découverte est lente car le fuzzer n'a aucun retour sur la couverture interne.

**Grey-box fuzzing.** On instrumente le binaire à la compilation (avec `afl-gcc` ou `afl-clang-fast`) ou au runtime (via QEMU, Frida, DynamoRIO) pour obtenir un retour de couverture. Le fuzzer sait quels chemins chaque input explore, et adapte ses mutations en conséquence. C'est le mode dominant aujourd'hui — AFL++ et libFuzzer fonctionnent ainsi. C'est **l'approche que nous utiliserons tout au long de ce chapitre**, car les binaires de la formation sont fournis avec leurs sources et peuvent être recompilés avec instrumentation.

**White-box fuzzing.** On utilise l'exécution symbolique (cf. Chapitre 18 — angr, Z3) pour générer des inputs qui satisfont exactement les contraintes de chaque branche. C'est théoriquement optimal, mais l'explosion combinatoire des chemins le rend impraticable sur des binaires de taille réelle — sauf pour des fonctions isolées. En pratique, on le combine souvent avec le grey-box : le fuzzer explore largement, et l'exécution symbolique résout ponctuellement les contraintes bloquantes.

> 💡 **En contexte RE** — Quand vous avez les sources (ou pouvez recompiler), privilégiez toujours le grey-box avec instrumentation à la compilation : c'est de loin le plus efficace. Si vous travaillez sur un binaire que vous ne pouvez pas recompiler, AFL++ propose un mode QEMU (`-Q`) qui instrumente au runtime — plus lent, mais fonctionnel.

---

## Quand utiliser le fuzzing dans un workflow RE

Le fuzzing n'est pas toujours la bonne réponse. Voici les situations où il apporte le plus de valeur :

**Le fuzzing excelle quand :**

- Le binaire **traite des entrées structurées** (fichiers, paquets réseau, messages sérialisés). Les parseurs sont des cibles idéales car ils contiennent de nombreuses branches conditionnelles que le fuzzer peut explorer.  
- La logique de parsing est **complexe ou obscure** — trop de branches pour les lire toutes manuellement, trop de combinaisons pour les tester à la main.  
- On cherche à **cartographier rapidement la surface d'entrée** d'un binaire inconnu avant de plonger dans l'analyse détaillée.  
- On veut **valider des hypothèses** issues de l'analyse statique : « cette fonction semble gérer les entrées de type X — est-ce que le fuzzer la confirme ? »

**Le fuzzing est moins pertinent quand :**

- Le binaire est **purement interactif** (interface graphique sans entrée fichier) — le fuzzing de GUI est un domaine à part, hors scope de ce chapitre.  
- La logique cible dépend d'un **état externe complexe** (base de données, réseau multi-parties, horloge système) difficile à reproduire dans un harness de fuzzing.  
- Le programme a un **temps de démarrage très long** — le fuzzing repose sur l'exécution de milliers d'inputs par seconde ; si chaque exécution prend plusieurs secondes, le rendement sera faible.  
- On comprend déjà bien le binaire et on cherche un détail précis — dans ce cas, GDB ou Frida sont plus adaptés que le balayage large du fuzzer.

---

## En résumé

Le fuzzing transforme le reverse engineering d'un exercice purement humain en une **collaboration entre l'analyste et l'automatisation**. Le reverse engineer apporte la compréhension structurelle, l'intuition, et la capacité d'interprétation. Le fuzzer apporte l'exhaustivité, la vitesse d'exploration, et la capacité à atteindre des chemins que l'humain n'aurait pas envisagés. Ensemble, ils couvrent bien plus de terrain que l'un ou l'autre séparément.

Dans les sections suivantes, nous passons à la pratique : installation d'AFL++ (15.2), écriture d'un harness libFuzzer (15.3), puis exploitation des résultats pour faire avancer notre compréhension du binaire cible.

---


⏭️ [AFL++ — installation, instrumentation et premier run sur une appli GCC](/15-fuzzing/02-afl-plus-plus.md)
