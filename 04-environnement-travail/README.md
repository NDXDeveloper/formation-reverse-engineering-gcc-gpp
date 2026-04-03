🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 4 — Mise en place de l'environnement de travail

> 🔧 **Objectif du chapitre** : construire un environnement de reverse engineering fonctionnel, reproductible et isolé, prêt à accueillir tous les exercices de la formation.

---

## Pourquoi un chapitre entier sur l'environnement ?

Le reverse engineering repose sur une constellation d'outils — désassembleurs, débogueurs, éditeurs hexadécimaux, frameworks d'instrumentation, fuzzers — qui interagissent les uns avec les autres et avec le système d'exploitation hôte. Un outil mal installé, une version incompatible ou un binaire exécuté sans isolation peut transformer une session d'apprentissage en cauchemar de debugging… ou en incident de sécurité.

Ce chapitre existe pour éviter ces écueils. En le suivant, vous obtiendrez :

- une **machine virtuelle dédiée**, isolée de votre système principal, dans laquelle vous pourrez exécuter des binaires inconnus sans risque ;  
- un **ensemble d'outils versionnés et testés**, installés de manière cohérente, afin que tous les exemples de la formation fonctionnent sans surprise ;  
- une **structure de projet claire**, avec les binaires d'entraînement compilables en une seule commande ;  
- un **script de vérification** (`check_env.sh`) qui valide que tout est en place avant de commencer.

> 💡 **Conseil** : même si vous pratiquez déjà le RE et que certains outils sont déjà installés sur votre machine, prenez le temps de monter la VM décrite ici. Travailler dans un environnement standardisé vous évitera les divergences de versions et vous permettra de suivre chaque chapitre à l'identique.

---

## Ce que nous allons mettre en place

Le chapitre couvre sept étapes, chacune dans sa propre section :

1. **Choix de la distribution Linux** — Pourquoi nous recommandons Ubuntu LTS (ou Debian/Kali selon votre profil), et les critères qui guident ce choix : disponibilité des paquets, compatibilité avec les outils RE, documentation communautaire.

2. **Installation des outils essentiels** — La liste complète et versionnée de tout ce dont nous aurons besoin au fil de la formation : GCC/G++, GDB (+ extensions GEF/pwndbg), Ghidra, Radare2, ImHex, Frida, AFL++, angr, pwntools, Valgrind, binutils, et les utilitaires complémentaires. Chaque outil est accompagné de sa méthode d'installation recommandée.

3. **Création d'une VM sandboxée** — Guide pas-à-pas pour monter une machine virtuelle avec VirtualBox, QEMU/KVM ou UTM (macOS Apple Silicon). Nous y configurons les snapshots pour pouvoir revenir à un état propre à tout moment.

4. **Configuration réseau de la VM** — Comment paramétrer le réseau en mode NAT pour l'installation des paquets, puis basculer en host-only ou en réseau isolé pour les chapitres d'analyse dynamique et d'analyse de malware (Partie VI).

5. **Structure du dépôt** — Présentation de l'arborescence `binaries/`, des `Makefile` par chapitre, et de la logique d'organisation des sources, patterns ImHex, règles YARA et scripts utilitaires.

6. **Compilation des binaires d'entraînement** — Un simple `make all` depuis le répertoire `binaries/` produit toutes les variantes nécessaires : niveaux d'optimisation `-O0` à `-O3`, avec et sans symboles de débogage, avec et sans stripping. Nous expliquons ce que chaque cible du Makefile génère et pourquoi.

7. **Vérification de l'installation** — Le script `check_env.sh` parcourt la liste des outils attendus, vérifie leurs versions, contrôle que les binaires d'entraînement sont compilés et que la VM est correctement configurée. Tout doit être au vert avant de passer à la suite.

---

## Prérequis avant de commencer

Avant d'attaquer ce chapitre, assurez-vous de disposer de :

- **Un ordinateur hôte** avec au moins 8 Go de RAM (16 Go recommandés), 40 Go d'espace disque libre et un processeur supportant la virtualisation matérielle (VT-x / AMD-V). Sur macOS Apple Silicon, UTM avec l'émulation x86-64 est l'option retenue.  
- **Un hyperviseur installé** : VirtualBox (toutes plateformes), QEMU/KVM (Linux), ou UTM (macOS). Si vous n'en avez pas encore, la section 4.3 vous guidera.  
- **Les connaissances des chapitres 1 à 3** : vous devez comprendre ce qu'est un fichier ELF, à quoi servent les sections `.text` et `.data`, et être capable de lire un listing assembleur basique. Si ce n'est pas le cas, revenez aux chapitres précédents — l'environnement technique ne compensera pas les lacunes conceptuelles.

---

## Architecture cible

Le schéma suivant résume l'environnement que nous allons construire :

```
┌─────────────────────────────────────────────────────┐
│                   Machine hôte                      │
│  (Windows / macOS / Linux — votre OS quotidien)     │
│                                                     │
│  ┌───────────────────────────────────────────────┐  │
│  │         VM RE Lab (Ubuntu LTS x86-64)         │  │
│  │                                               │  │
│  │  ┌─────────────┐  ┌────────────────────────┐  │  │
│  │  │  Outils RE  │  │  Dépôt de la formation │  │  │
│  │  │             │  │                        │  │  │
│  │  │  GDB + GEF  │  │  binaries/             │  │  │
│  │  │  Ghidra     │  │  scripts/              │  │  │
│  │  │  Radare2    │  │  hexpat/               │  │  │
│  │  │  ImHex      │  │  yara-rules/           │  │  │
│  │  │  Frida      │  │  solutions/            │  │  │
│  │  │  AFL++      │  │                        │  │  │
│  │  │  angr       │  │  Makefile              │  │  │
│  │  │  pwntools   │  │  check_env.sh          │  │  │
│  │  │  Valgrind   │  │                        │  │  │
│  │  │  ...        │  │                        │  │  │
│  │  └─────────────┘  └────────────────────────┘  │  │
│  │                                               │  │
│  │  Réseau : NAT (install) → Host-only (analyse) │  │
│  │  Snapshots : baseline, post-install, clean    │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

L'idée directrice est simple : **tout le travail de RE se fait dans la VM, jamais sur l'hôte**. Cette séparation protège votre système principal et garantit un environnement reproductible. Si quelque chose tourne mal — un binaire piégé, une dépendance cassée, une manipulation hasardeuse — il suffit de restaurer un snapshot.

---

## Conventions pour ce chapitre

- Les commandes à exécuter **sur la machine hôte** sont préfixées par `[hôte]`.  
- Les commandes à exécuter **dans la VM** sont préfixées par `[vm]`.  
- Les chemins sont relatifs à la racine du dépôt de la formation, sauf indication contraire.  
- Les versions indiquées sont celles testées au moment de la rédaction. Des versions plus récentes fonctionneront dans la grande majorité des cas, mais en cas de comportement inattendu, revenir à la version documentée est la première chose à essayer.

---

## Plan du chapitre

| Section | Contenu |  
|---|---|  
| 4.1 | Distribution Linux recommandée (Ubuntu/Debian/Kali) |  
| 4.2 | Installation et configuration des outils essentiels |  
| 4.3 | Création d'une VM sandboxée (VirtualBox / QEMU / UTM) |  
| 4.4 | Configuration réseau de la VM : NAT, host-only, isolation |  
| 4.5 | Structure du dépôt : organisation de `binaries/` et des `Makefile` |  
| 4.6 | Compiler tous les binaires d'entraînement (`make all`) |  
| 4.7 | Vérifier l'installation : script `check_env.sh` |  
| **🎯 Checkpoint** | Exécuter `check_env.sh` — tous les outils doivent être au vert |

---


⏭️ [Distribution Linux recommandée (Ubuntu/Debian/Kali)](/04-environnement-travail/01-distribution-linux.md)
