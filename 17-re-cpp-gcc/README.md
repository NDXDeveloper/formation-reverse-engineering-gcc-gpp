🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 17 — Reverse Engineering du C++ avec GCC

> **Partie IV — Techniques Avancées de RE**

---

## Pourquoi le C++ est un défi majeur en Reverse Engineering

Le C, malgré sa proximité avec la machine, produit un assembleur relativement prévisible : les fonctions ont des noms lisibles, les structures de données se traduisent en accès mémoire à offsets fixes, et le flux de contrôle reste fidèle au code source. Le C++ change radicalement la donne. Le compilateur génère une quantité considérable de code et de métadonnées invisibles au développeur — code que le reverse engineer doit pourtant comprendre pour reconstruire la logique originale.

Quand GCC compile du C++, il ne se contente pas de traduire des instructions : il orchestre un modèle objet complet. Chaque classe polymorphe se voit attribuer une **vtable** (table de pointeurs de fonctions virtuelles) et chaque instance embarque un **vptr** caché qui pointe vers cette table. L'héritage multiple complexifie le layout mémoire des objets avec des ajustements de pointeurs (`thunks`). Les noms de fonctions sont **manglés** selon les règles de l'Itanium ABI pour encoder les namespaces, les types de paramètres et les qualificateurs — transformant un simple `MyClass::process(int)` en une chaîne cryptique comme `_ZN7MyClass7processEi`. Le mécanisme d'exceptions repose sur des sections dédiées (`.eh_frame`, `.gcc_except_table`) et des fonctions runtime (`__cxa_throw`, `__cxa_begin_catch`) qui génèrent un flux de contrôle invisible dans le désassemblage classique.

Et ce n'est que la partie émergée. Les templates provoquent une **explosion combinatoire de symboles** : chaque instanciation avec un type différent génère une copie complète du code. Les conteneurs de la STL (`std::vector`, `std::string`, `std::map`…) ont des layouts mémoire internes spécifiques qu'il faut connaître pour interpréter correctement les accès mémoire. Les lambdas sont compilées en classes anonymes avec un `operator()`. Les smart pointers ajoutent des couches d'indirection et de comptage de références. Les coroutines C++20 transforment une fonction apparemment linéaire en machine à états.

Tout cela signifie qu'un binaire C++ optimisé et strippé peut être **plusieurs ordres de grandeur plus difficile à analyser** qu'un binaire C équivalent en termes de fonctionnalités.

## Ce que ce chapitre couvre

Ce chapitre est le plus dense de la formation. Il décompose systématiquement chaque mécanisme C++ tel que GCC le compile, en montrant à chaque fois :

- **Ce que le développeur écrit** (le code source C++).  
- **Ce que GCC produit** (l'assembleur x86-64 et les structures de données générées).  
- **Comment le reconnaître** dans un désassembleur (Ghidra, objdump, radare2) sans avoir accès au code source.  
- **Comment le reconstruire** pour retrouver une représentation haut niveau exploitable.

Voici les sujets abordés dans l'ordre :

1. **Name mangling** — Les règles de l'Itanium ABI utilisées par GCC pour encoder les identifiants C++, et les outils pour les décoder (`c++filt`, démanglement automatique dans Ghidra).

2. **Modèle objet : vtables et vptr** — Comment GCC implémente le polymorphisme, le layout mémoire d'un objet avec héritage simple puis multiple, et comment identifier les appels virtuels dans le désassemblage.

3. **RTTI et `dynamic_cast`** — Les structures de type runtime générées par GCC, leur emplacement dans le binaire, et comment elles permettent de reconstruire la hiérarchie de classes.

4. **Gestion des exceptions** — Le mécanisme zero-cost exception handling, les sections `.eh_frame` et `.gcc_except_table`, et les fonctions `__cxa_*` du runtime.

5. **STL internals** — Le layout mémoire des conteneurs les plus courants (`vector`, `string`, `map`, `unordered_map`) tel que GCC/libstdc++ les implémente.

6. **Templates** — Comment reconnaître les instanciations multiples, l'impact sur la taille du binaire et la table de symboles.

7. **Lambdas et closures** — La transformation en classes anonymes, la capture de variables par valeur et par référence, et leur représentation en assembleur.

8. **Smart pointers** — Les différences entre `unique_ptr` (zero-cost abstraction) et `shared_ptr` (comptage de références atomique) dans le code généré.

9. **Coroutines C++20** — Le pattern de machine à états généré par GCC, le frame de coroutine et les points de suspension.

## Prérequis pour ce chapitre

Ce chapitre s'appuie fortement sur les acquis des chapitres précédents. Avant de le commencer, assurez-vous d'être à l'aise avec :

- **Chapitre 3** — Assembleur x86-64 : registres, conventions d'appel System V AMD64, lecture d'un listing assembleur. Vous devez pouvoir lire un prologue/épilogue de fonction et suivre le passage de paramètres via `rdi`, `rsi`, `rdx`… sans hésitation.

- **Chapitre 7** — Désassemblage avec objdump : syntaxe Intel, comparaison de niveaux d'optimisation, identification de `main()`. Nous utiliserons intensivement `objdump -d -M intel` pour examiner le code généré.

- **Chapitre 8** — Ghidra : navigation dans le CodeBrowser, décompileur, renommage de fonctions, reconstruction de structures. Ghidra sera notre outil principal pour visualiser les vtables et les structures de données C++.

- **Chapitre 16** — Optimisations du compilateur : inlining, déroulage de boucles, tail call optimization. Comprendre les optimisations de GCC est indispensable car elles transforment profondément le code C++ (l'inlining supprime des appels virtuels, les templates sont optimisés de manière agressive, etc.).

Une connaissance intermédiaire du C++ côté développeur est également nécessaire. Vous n'avez pas besoin de maîtriser les subtilités du standard, mais vous devez comprendre les concepts de classe, héritage, polymorphisme, templates, exceptions et STL au niveau d'un développeur qui les utilise au quotidien.

## Binaire d'entraînement

Le binaire principal de ce chapitre est `ch17-oop`, situé dans `binaries/ch17-oop/`. Il s'agit d'une application C++ orientée objet compilée avec GCC, disponible en plusieurs variantes :

| Variante | Flags de compilation | Usage |  
|---|---|---|  
| `ch17-oop_O0` | `-O0 -g` | Apprentissage — code non optimisé, symboles complets |  
| `ch17-oop_O0_strip` | `-O0 -s` | Intermédiaire — non optimisé mais sans symboles |  
| `ch17-oop_O2` | `-O2 -g` | Réaliste — optimisé avec symboles pour vérification |  
| `ch17-oop_O2_strip` | `-O2 -s` | Conditions réelles — optimisé et strippé |  
| `ch17-oop_O3` | `-O3 -g` | Optimisation agressive — avec symboles |  
| `ch17-oop_O3_strip` | `-O3 -s` | Défi maximal — optimisé agressivement et strippé |

La progression recommandée est de commencer chaque section avec la variante `ch17-oop_O0` pour comprendre le mécanisme, puis de passer à `ch17-oop_O2_strip` pour apprendre à le reconnaître dans des conditions réalistes.

Ce binaire contient volontairement :

- Une hiérarchie de classes avec héritage simple et multiple.  
- Des méthodes virtuelles et virtuelles pures (classes abstraites).  
- De la RTTI activée (pas de `-fno-rtti`).  
- Des exceptions avec `try`/`catch` et types d'exceptions personnalisés.  
- Des conteneurs STL variés (`std::vector`, `std::string`, `std::map`).  
- Des templates instanciés avec plusieurs types.  
- Des lambdas avec différents modes de capture.  
- Des smart pointers (`std::unique_ptr`, `std::shared_ptr`).

## Méthodologie recommandée

Face à un binaire C++ inconnu, l'approche recommandée se décompose en phases :

**Phase 1 — Démanglement et cartographie.** Commencez toujours par récupérer le maximum d'informations depuis les symboles (s'ils existent) ou depuis la RTTI. Démanglez les noms, listez les classes identifiables, et construisez une première ébauche de la hiérarchie.

**Phase 2 — Reconstruction des vtables.** Localisez les vtables dans `.rodata`, identifiez les méthodes virtuelles et leur ordre. Chaque vtable vous donne la liste complète des méthodes virtuelles d'une classe, y compris celles héritées.

**Phase 3 — Layout mémoire des objets.** Déterminez la taille et le layout de chaque classe en analysant les constructeurs (ils initialisent le vptr et les membres) et les accès mémoire récurrents via les offsets depuis `this` (premier paramètre, dans `rdi`).

**Phase 4 — Flux de contrôle et logique métier.** Une fois la structure des classes comprise, concentrez-vous sur la logique : que font les méthodes, comment interagissent les objets, quel est le flux d'exécution global.

Ce chapitre suit cette progression en traitant chaque mécanisme individuellement avant de les combiner dans le checkpoint final.

---

## Sections du chapitre

- 17.1 [Name mangling — règles Itanium ABI et démanglement](/17-re-cpp-gcc/01-name-mangling-itanium.md)  
- 17.2 [Modèle objet C++ : vtable, vptr, héritage simple et multiple](/17-re-cpp-gcc/02-modele-objet-vtable.md)  
- 17.3 [RTTI (Run-Time Type Information) et `dynamic_cast`](/17-re-cpp-gcc/03-rtti-dynamic-cast.md)  
- 17.4 [Gestion des exceptions (`.eh_frame`, `.gcc_except_table`, `__cxa_throw`)](/17-re-cpp-gcc/04-gestion-exceptions.md)  
- 17.5 [STL internals : `std::vector`, `std::string`, `std::map`, `std::unordered_map` en mémoire](/17-re-cpp-gcc/05-stl-internals.md)  
- 17.6 [Templates : instanciations et explosion de symboles](/17-re-cpp-gcc/06-templates-instanciations.md)  
- 17.7 [Lambda, closures et captures en assembleur](/17-re-cpp-gcc/07-lambda-closures.md)  
- 17.8 [Smart Pointers en assembleur : `unique_ptr` vs `shared_ptr` (comptage de références)](/17-re-cpp-gcc/08-smart-pointers.md)  
- 17.9 [Coroutines C++20 : reconnaître le frame et le state machine pattern](/17-re-cpp-gcc/09-coroutines-cpp20.md)

---

**🎯 Checkpoint** : [Reconstruire les classes du binaire `ch17-oop` à partir du désassemblage seul](/17-re-cpp-gcc/checkpoint.md)

---


⏭️ [Name mangling — règles Itanium ABI et démanglement](/17-re-cpp-gcc/01-name-mangling-itanium.md)
