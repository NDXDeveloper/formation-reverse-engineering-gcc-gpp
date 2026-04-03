🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 13 — Instrumentation dynamique avec Frida

> 📦 **Binaires utilisés** : `binaries/ch13-keygenme/`, `binaries/ch13-network/`, `binaries/ch14-crypto/`  
> 🧰 **Outils requis** : `frida`, `frida-tools`, `frida-trace`, Python 3 avec le module `frida`  
> 📖 **Prérequis** : [Chapitre 11 — GDB](/11-gdb/README.md), [Chapitre 12 — GDB amélioré](/12-gdb-extensions/README.md), notions de JavaScript

---

## Pourquoi ce chapitre ?

Dans les chapitres 11 et 12, nous avons exploré le débogage classique avec GDB et ses extensions. GDB est un outil extraordinairement puissant, mais il impose une contrainte fondamentale : l'exécution du programme cible est **interrompue** à chaque breakpoint. On travaille en mode « stop-and-inspect » — on arrête le processus, on inspecte son état, on reprend. Cette approche devient laborieuse dès qu'on veut observer un grand nombre d'appels de fonctions, modifier des valeurs de retour à la volée sur des centaines d'invocations, ou interagir avec un programme sans que celui-ci ne perçoive la moindre interruption.

Frida change radicalement la donne. Au lieu de suspendre le processus, Frida **injecte un agent JavaScript directement dans l'espace mémoire du programme cible**, pendant qu'il tourne. Cet agent peut lire et écrire la mémoire, intercepter des appels de fonctions, remplacer des arguments, modifier des valeurs de retour — le tout **sans jamais arrêter l'exécution**. Le programme continue de fonctionner normalement, sans savoir qu'un observateur invisible réécrit les règles du jeu en temps réel.

Si GDB est un microscope qui exige l'immobilité de l'échantillon, Frida est une caméra embarquée qui filme en continu.

---

## Ce que Frida apporte au reverse engineer

Les cas d'usage de Frida en RE sont nombreux, mais ils gravitent autour de quelques scénarios récurrents :

**Observer sans perturber.** On veut savoir quelles fonctions sont appelées, avec quels arguments, et ce qu'elles retournent — sans altérer le comportement du programme. C'est l'équivalent dynamique d'une analyse de cross-references dans Ghidra, mais avec les vraies valeurs d'exécution au lieu de suppositions statiques.

**Modifier le comportement en live.** Une fonction de vérification de licence renvoie `0` (échec) ? On peut forcer le retour à `1` sans toucher au binaire sur disque. Un appel à `connect()` pointe vers un serveur distant ? On peut rediriger les arguments vers `127.0.0.1`. Frida permet de tester des hypothèses de RE instantanément, sans cycle de patching–recompilation–relancement.

**Tracer l'exécution à grande échelle.** Avec le moteur Stalker, Frida peut instrumenter chaque instruction exécutée par un thread donné. On obtient ainsi une couverture de code dynamique complète — une information précieuse pour comprendre quels chemins sont réellement empruntés, quelles branches restent mortes, et comment le programme réagit à différents inputs.

**Automatiser des interactions complexes.** Combiné avec Python côté contrôleur, Frida permet de scripter des scénarios complets : injecter des données, observer la réaction du programme, ajuster les inputs en conséquence. On est à mi-chemin entre le débogage et le fuzzing guidé.

---

## Frida dans notre méthodologie

Ce chapitre s'inscrit dans la progression logique de la Partie III (Analyse Dynamique). Jusqu'ici, notre boîte à outils dynamique comprenait :

- **`strace` / `ltrace`** (chapitre 5) — observation passive des appels système et bibliothèque, sans aucun contrôle sur le processus.  
- **GDB** (chapitres 11–12) — contrôle total mais intrusif, avec arrêt de l'exécution à chaque point d'inspection.

Frida se positionne entre les deux : plus puissant que `strace` (on peut modifier le comportement, pas seulement l'observer), et moins intrusif que GDB (pas d'arrêt de l'exécution). Dans les chapitres suivants de la Partie V (Cas Pratiques), nous utiliserons Frida de manière intensive — pour extraire des clés de chiffrement depuis la mémoire (chapitre 24), intercepter des communications réseau (chapitre 23), et contourner des protections anti-debug qui rendraient GDB inutilisable (chapitre 19).

---

## Portée et limites

Frida est un framework multiplateforme qui fonctionne sur Linux, Windows, macOS, Android et iOS. Dans le cadre de cette formation, nous nous concentrons exclusivement sur **Linux x86-64** avec des binaires ELF compilés par GCC/G++, en cohérence avec le périmètre du tutoriel. Les concepts et les API JavaScript présentés ici sont toutefois transposables tels quels aux autres plateformes — seuls les détails de l'injection et les noms de bibliothèques changent.

Il est important de garder en tête que Frida n'est pas un outil furtif par nature. Un programme déterminé à détecter l'instrumentation peut y parvenir (présence de `frida-agent` en mémoire, threads supplémentaires, timing anomalies). Nous aborderons brièvement ces aspects de détection ici, et le chapitre 19 (Anti-reversing) approfondira les techniques de détection de débogueur et d'instrumentation.

---

## Plan du chapitre

- **13.1** — [Architecture de Frida — agent JS injecté dans le processus cible](/13-frida/01-architecture-frida.md)  
- **13.2** — [Modes d'injection : `frida`, `frida-trace`, spawn vs attach](/13-frida/02-modes-injection.md)  
- **13.3** — [Hooking de fonctions C et C++ à la volée](/13-frida/03-hooking-fonctions-c-cpp.md)  
- **13.4** — [Intercepter les appels à `malloc`, `free`, `open`, fonctions customs](/13-frida/04-intercepter-appels.md)  
- **13.5** — [Modifier des arguments et valeurs de retour en live](/13-frida/05-modifier-arguments-retour.md)  
- **13.6** — [Stalker : tracer toutes les instructions exécutées (code coverage dynamique)](/13-frida/06-stalker-code-coverage.md)  
- **13.7** — [Cas pratique : contourner une vérification de licence](/13-frida/07-cas-pratique-licence.md)  
- **🎯 Checkpoint** — [Écrire un script Frida qui logue tous les appels à `send()` avec leurs buffers](/13-frida/checkpoint.md)

---

## Installation rapide

Avant d'attaquer la section 13.1, assurez-vous que Frida est installé et fonctionnel :

```bash
# Installation via pip (Python 3)
pip install frida-tools frida

# Vérification de la version
frida --version

# Test rapide : lister les processus locaux
frida-ps
```

Si `frida-ps` affiche la liste des processus en cours, l'environnement est prêt. En cas de problème de permissions, l'exécution en `sudo` peut être nécessaire pour l'injection dans des processus appartenant à d'autres utilisateurs — nous détaillerons les implications dans la section 13.1.

> 💡 Le script `check_env.sh` fourni à la racine du dépôt vérifie automatiquement la présence de Frida parmi les autres outils requis.

⏭️ [Architecture de Frida — agent JS injecté dans le processus cible](/13-frida/01-architecture-frida.md)
