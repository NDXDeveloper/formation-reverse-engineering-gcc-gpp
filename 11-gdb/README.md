🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 11 — Débogage avec GDB

> **Partie III — Analyse Dynamique**

---

## Introduction

Les chapitres précédents nous ont appris à lire un binaire sans jamais l'exécuter : désassemblage, inspection des sections ELF, analyse hexadécimale, cross-references dans Ghidra. Cette approche statique est puissante, mais elle atteint ses limites dès que le comportement du programme dépend de données calculées à l'exécution — une valeur dérivée d'une entrée utilisateur, un pointeur résolu dynamiquement, un état interne modifié au fil des appels. Pour comprendre ce qui se passe *vraiment*, il faut observer le programme pendant qu'il tourne. C'est le rôle de l'analyse dynamique, et son outil fondamental sous Linux est **GDB** (GNU Debugger).

GDB existe depuis 1986. Il fait partie intégrante de la chaîne GNU que nous avons étudiée au chapitre 2, et il est installé par défaut sur la quasi-totalité des distributions Linux. Derrière son interface en ligne de commande austère se cache un débogueur d'une profondeur remarquable : il permet de poser des points d'arrêt sur n'importe quelle adresse, d'inspecter la mémoire octet par octet, de modifier des registres à la volée, de tracer les appels système, et même d'être entièrement scripté en Python. Pour le reverse engineer, GDB est l'équivalent d'un microscope : là où Ghidra montre la structure d'ensemble, GDB révèle l'état exact du programme à un instant donné.

## Pourquoi GDB est incontournable en RE

En analyse statique, on raisonne sur ce que le code *pourrait* faire. En analyse dynamique avec GDB, on observe ce qu'il *fait* réellement, avec des valeurs concrètes dans les registres et en mémoire. Cette différence est fondamentale dans plusieurs situations courantes :

- **Valeurs calculées à l'exécution.** Un algorithme de vérification de clé peut combiner l'entrée utilisateur avec des constantes, des opérations XOR en cascade, des rotations de bits. En statique, il faut reconstruire mentalement chaque étape. Avec GDB, on pose un breakpoint juste avant la comparaison finale et on lit directement la valeur attendue dans un registre.

- **Résolution de pointeurs et d'adresses.** Dans un binaire PIE avec ASLR activé, les adresses absolues changent à chaque exécution. GDB résout tout cela automatiquement : il affiche les adresses effectives, suit les indirections à travers les vtables C++, et permet de naviguer dans la heap comme dans la pile.

- **Code obfusqué ou auto-modifiant.** Certains binaires modifient leur propre code en mémoire avant de l'exécuter, ou déchiffrent des portions de code à la volée. L'analyse statique ne voit que le code chiffré. GDB permet d'attendre que le déchiffrement soit terminé, puis d'examiner le code réel.

- **Exploration de bibliothèques tierces.** Quand un binaire appelle des fonctions de bibliothèques partagées (OpenSSL, zlib, etc.), GDB permet de suivre l'exécution jusque dans ces bibliothèques, d'inspecter les arguments passés et les valeurs retournées, sans avoir besoin d'en lire le code source.

## Ce que couvre ce chapitre

Ce chapitre est le plus dense de la Partie III, car GDB sera notre compagnon constant pour tout le reste de la formation. Nous allons construire les compétences de manière progressive :

Nous commencerons par la **compilation avec symboles de débogage** (`-g` et le format DWARF), afin de comprendre ce que GDB sait — ou ne sait pas — sur le binaire qu'il analyse. Nous verrons ensuite les **commandes fondamentales** : poser des breakpoints, avancer instruction par instruction, inspecter les registres et la mémoire. Ces bases acquises, nous aborderons le cas réaliste d'un **binaire strippé** — sans aucun symbole — et les techniques pour s'y retrouver malgré tout.

La seconde moitié du chapitre monte en puissance avec les **breakpoints conditionnels et watchpoints**, qui permettent de ne s'arrêter que lorsqu'une condition précise est remplie (une variable atteint une certaine valeur, une zone mémoire est modifiée). Nous verrons les **catchpoints**, qui interceptent des événements système comme les `fork`, `exec` ou les signaux. Nous couvrirons le **débogage distant** avec `gdbserver`, indispensable pour analyser un binaire dans une VM sandboxée depuis le confort de notre machine hôte.

Enfin, nous découvrirons deux extensions majeures de GDB : son **API Python**, qui permet d'automatiser des tâches d'analyse complexes via des scripts, et **pwntools**, la bibliothèque Python de référence pour interagir programmatiquement avec un binaire — envoyer des entrées, lire des sorties, et piloter GDB en parallèle.

## Prérequis pour ce chapitre

Ce chapitre s'appuie directement sur les connaissances construites dans les chapitres précédents :

- **Chapitre 2** — La chaîne de compilation GNU : comprendre comment un fichier source devient un binaire ELF, et le rôle des sections `.text`, `.data`, `.plt`/`.got`.  
- **Chapitre 3** — Assembleur x86-64 : lire les instructions, connaître les registres (`rax`, `rdi`, `rsp`, `rbp`, `rip`), comprendre la pile et les conventions d'appel System V AMD64.  
- **Chapitre 5** — Outils d'inspection : `file`, `readelf`, `checksec` pour le triage initial avant de lancer GDB.  
- **Chapitre 7 ou 8** — Désassemblage avec `objdump` ou Ghidra : savoir localiser une fonction d'intérêt dans le désassemblage statique avant de poser un breakpoint.

Si la lecture d'un listing assembleur de 20 lignes vous met encore mal à l'aise, prenez le temps de revoir la section 3.7 (*Lire un listing assembleur sans paniquer*) avant de continuer.

## Outils nécessaires

Tous ces outils doivent être fonctionnels dans votre environnement. Si vous avez suivi le chapitre 4 et que `check_env.sh` est au vert, tout est en place.

| Outil | Version minimale | Rôle dans ce chapitre |  
|---|---|---|  
| `gdb` | 12.x+ | Débogueur principal |  
| `gcc` / `g++` | 12.x+ | Compilation des binaires d'entraînement avec `-g` |  
| `gdbserver` | (inclus avec GDB) | Débogage distant (section 11.7) |  
| Python 3 | 3.10+ | Scripts GDB Python (section 11.8) et pwntools (section 11.9) |  
| `pwntools` | 4.x+ | Automatisation des interactions (section 11.9) |

## Conseil de lecture

GDB s'apprend en le pratiquant. Chaque section de ce chapitre contient des commandes à taper et des sorties à observer. Gardez un terminal ouvert avec GDB à côté de votre lecture : reproduisez chaque exemple sur les binaires fournis dans `binaries/ch11-keygenme/`. Le binaire `keygenme_O0` (compilé sans optimisation, avec symboles) est le compagnon idéal pour les premières sections ; nous passerons aux variantes strippées et optimisées au fur et à mesure.

---

## Plan du chapitre

- 11.1 [Compilation avec symboles de débogage (`-g`, DWARF)](/11-gdb/01-compilation-symboles-debug.md)  
- 11.2 [Commandes GDB fondamentales : `break`, `run`, `next`, `step`, `info`, `x`, `print`](/11-gdb/02-commandes-fondamentales.md)  
- 11.3 [Inspecter la pile, les registres, la mémoire (format et tailles)](/11-gdb/03-inspecter-pile-registres-memoire.md)  
- 11.4 [GDB sur un binaire strippé — travailler sans symboles](/11-gdb/04-gdb-binaire-strippe.md)  
- 11.5 [Breakpoints conditionnels et watchpoints (mémoire et registres)](/11-gdb/05-breakpoints-conditionnels-watchpoints.md)  
- 11.6 [Catchpoints : intercepter les `fork`, `exec`, `syscall`, signaux](/11-gdb/06-catchpoints.md)  
- 11.7 [Remote debugging avec `gdbserver` (debugging sur cible distante)](/11-gdb/07-remote-debugging-gdbserver.md)  
- 11.8 [GDB Python API — scripting et automatisation](/11-gdb/08-gdb-python-api.md)  
- 11.9 [Introduction à `pwntools` pour automatiser les interactions avec un binaire](/11-gdb/09-introduction-pwntools.md)  
- 🎯 [Checkpoint : écrire un script GDB Python qui dumpe automatiquement les arguments de chaque appel à `strcmp`](/11-gdb/checkpoint.md)

⏭️ [Compilation avec symboles de débogage (`-g`, DWARF)](/11-gdb/01-compilation-symboles-debug.md)
