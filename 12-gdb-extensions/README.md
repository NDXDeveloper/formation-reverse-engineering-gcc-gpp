🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg

> **Partie III — Analyse Dynamique**

---

## Pourquoi améliorer GDB ?

Le chapitre précédent a posé les bases du débogage avec GDB : poser des breakpoints, inspecter la mémoire, parcourir le code pas à pas. Ces fondamentaux sont indispensables, mais quiconque a passé plus de dix minutes dans GDB vanilla le sait : l'interface par défaut est spartiate. Après un `stepi`, le débogueur rend la main avec un simple prompt, sans montrer l'état des registres, le contenu de la pile, ni le désassemblage environnant. Il faut enchaîner manuellement `info registers`, `x/20i $rip`, `x/32gx $rsp` à chaque instruction — un flux de travail lent, répétitif et propice aux erreurs d'inattention.

Ce constat a donné naissance à trois extensions majeures, toutes écrites en Python via l'API GDB que nous avons abordée en section 11.8. Elles partagent un objectif commun — rendre le débogage interactif, visuel et productif — mais divergent par leur philosophie, leur périmètre fonctionnel et leur communauté.

## Les trois extensions en un coup d'œil

**PEDA** (*Python Exploit Development Assistance for GDB*) est la pionnière. Créée par Long Le Dinh en 2012, elle a été la première à afficher automatiquement les registres, la pile et le code désassemblé après chaque instruction. PEDA a posé les conventions visuelles que les deux autres ont reprises et étendues. Elle reste fonctionnelle aujourd'hui, mais son développement a considérablement ralenti, et elle ne prend pas en charge certaines fonctionnalités modernes comme l'analyse avancée de la heap glibc.

**GEF** (*GDB Enhanced Features*, prononcé « jeff ») a été conçu par Hugsy comme une alternative sans dépendance externe. Là où PEDA et pwndbg nécessitent parfois des bibliothèques Python tierces, GEF tient en un seul fichier Python. Cette portabilité en fait un excellent choix pour les environnements contraints : serveurs distants, conteneurs Docker, machines de CTF où l'on ne peut pas installer grand-chose. GEF offre un bon équilibre entre richesse fonctionnelle et légèreté, avec un support multi-architecture (ARM, MIPS, RISC-V…) particulièrement soigné.

**pwndbg** (prononcé « pone-dee-bug ») est le plus riche des trois en termes de fonctionnalités. Maintenu activement par une large communauté, il propose des commandes spécialisées pour l'analyse de la heap (`vis_heap_chunks`, `bins`, `tcachebins`), la recherche de gadgets ROP, le suivi des allocations mémoire, et bien plus. C'est l'extension de choix pour l'exploitation de vulnérabilités et l'analyse de binaires complexes. La contrepartie est une installation un peu plus lourde, avec plusieurs dépendances Python.

## Ce que ces extensions changent concrètement

Le changement le plus immédiat est le **contexte automatique**. À chaque arrêt du programme — breakpoint, `stepi`, `nexti`, watchpoint — l'extension affiche un tableau de bord complet : état des registres avec coloration des valeurs modifiées, portion de la pile avec déréférencement intelligent des pointeurs, désassemblage autour de l'instruction courante, et souvent le code source si les symboles de débogage sont présents. Ce contexte élimine le besoin de taper manuellement des commandes d'inspection après chaque pas.

Au-delà de l'affichage, les trois extensions ajoutent des dizaines de commandes absentes de GDB vanilla. La recherche de patterns cycliques pour calculer des offsets de débordement de buffer, la génération et l'identification de motifs De Bruijn, le déréférencement récursif de pointeurs (« telescope »), la détection automatique des protections du binaire en cours de débogage, ou encore l'extraction de la table GOT et des entrées PLT en un seul coup sont autant de tâches qui prendraient plusieurs commandes enchaînées dans GDB nu.

## Positionnement dans la formation

Ce chapitre se place volontairement après la maîtrise de GDB vanilla (chapitre 11). Comprendre ce que les extensions font « sous le capot » — c'est-à-dire des appels à l'API Python de GDB, des lectures mémoire formatées, des heuristiques sur les structures de la glibc — est essentiel pour ne pas devenir dépendant d'un outil sans en comprendre les limites. Les extensions sont des accélérateurs, pas des substituts à la compréhension.

Dans la suite de la formation, nous utiliserons principalement **GEF** et **pwndbg** pour les cas pratiques des parties V et VI. GEF sera privilégié pour sa portabilité lors du débogage distant (chapitre 11, section `gdbserver`), tandis que pwndbg sera notre outil de prédilection pour l'analyse de la heap et les scénarios d'exploitation dans les chapitres 27 à 29.

## Plan du chapitre

- **12.1** — Installation et comparaison des trois extensions  
- **12.2** — Visualisation de la stack et des registres en temps réel  
- **12.3** — Recherche de gadgets ROP depuis GDB  
- **12.4** — Analyse de heap avec pwndbg (`vis_heap_chunks`, `bins`)  
- **12.5** — Commandes utiles spécifiques à chaque extension  
- **🎯 Checkpoint** — Tracer l'exécution complète de `keygenme_O0` avec GEF, capturer le moment de la comparaison

## Prérequis pour ce chapitre

Ce chapitre suppose acquis l'ensemble du chapitre 11 sur GDB, en particulier :

- La pose de breakpoints et le parcours pas à pas (`stepi`, `nexti`, `finish`)  
- L'inspection des registres (`info registers`) et de la mémoire (`x/`)  
- Les breakpoints conditionnels et les watchpoints (section 11.5)  
- Les bases du scripting Python pour GDB (section 11.8)

Une familiarité avec les conventions d'appel System V AMD64 (chapitre 3, sections 3.5 et 3.6) est également nécessaire pour tirer pleinement parti des affichages de pile et de registres proposés par ces extensions.

## Binaires utilisés

Les binaires d'entraînement de ce chapitre se trouvent dans le répertoire `binaries/ch12-keygenme/`. Le checkpoint utilise `keygenme_O0` (compilé sans optimisation, avec symboles), ce qui permet de se concentrer sur la prise en main des extensions sans être gêné par les transformations du compilateur. Les variantes optimisées et strippées seront exploitées dans les chapitres ultérieurs une fois les extensions maîtrisées.

Pour recompiler les binaires si nécessaire :

```bash
cd binaries/ch12-keygenme/  
make clean && make all  
```

---


⏭️ [Installation et comparaison des trois extensions](/12-gdb-extensions/01-installation-comparaison.md)
