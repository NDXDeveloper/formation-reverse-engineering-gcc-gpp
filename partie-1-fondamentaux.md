🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Partie I — Fondamentaux & Environnement

Avant de lancer Ghidra, de poser un breakpoint dans GDB ou de hooker une fonction avec Frida, il faut maîtriser ce qui se passe **entre votre code source et le binaire qui tourne en mémoire**. Cette première partie pose les bases sans lesquelles toute analyse — statique ou dynamique — se résume à naviguer à l'aveugle dans un flux d'octets. Vous y apprendrez ce qu'est réellement un binaire ELF, comment le compilateur transforme votre C/C++ en instructions machine, et comment lire ces instructions sans paniquer.

---

## 🎯 Objectifs de cette partie

À l'issue de ces quatre chapitres, vous serez capable de :

1. **Définir le périmètre du Reverse Engineering** et distinguer clairement analyse statique et analyse dynamique, en connaissant le cadre légal applicable (CFAA, EUCD, DMCA).  
2. **Décrire chaque étape de la chaîne de compilation GNU** — du préprocesseur au linker — et expliquer l'impact des flags (`-O0` à `-O3`, `-g`, `-s`, `-fPIC`, `-pie`) sur le binaire produit.  
3. **Lire et interpréter un listing assembleur x86-64** : identifier les registres, les instructions courantes, le prologue/épilogue de fonctions, la convention d'appel System V AMD64, et appliquer une méthode structurée en 5 étapes pour annoter un désassemblage inconnu.  
4. **Naviguer dans la structure d'un binaire ELF** : localiser les sections clés (`.text`, `.data`, `.rodata`, `.plt`, `.got`), comprendre le rôle du loader `ld.so`, et expliquer le mécanisme PLT/GOT de résolution dynamique.  
5. **Disposer d'un environnement de travail opérationnel** : VM isolée, outils installés et vérifiés, binaires d'entraînement compilés et prêts à l'analyse.  
6. **Compiler un même programme à différents niveaux d'optimisation** et observer concrètement les différences de taille, de sections et de désassemblage avec `readelf` et `objdump`.

---

## 📋 Chapitres

| N° | Titre | Description | Lien |  
|----|-------|-------------|------|  
| 1 | Introduction au Reverse Engineering | Définition et objectifs du RE, cadre légal et éthique (CFAA, EUCD/DMCA, directive 2009/24/CE), cas d'usage légitimes (audit, CTF, interopérabilité, malware), distinction statique vs dynamique, méthodologie et outils, taxonomie des cibles. | [Chapitre 1](/01-introduction-re/README.md) |  
| 2 | La chaîne de compilation GNU | Architecture GCC (4 phases), fichiers intermédiaires (`.i`, `.s`, `.o`), formats binaires (ELF, PE, Mach-O), sections ELF clés, flags de compilation (`-O0`→`-O3`, `-g`, `-s`, `-pie`), symboles DWARF, loader `ld.so`, segments et ASLR, résolution PLT/GOT (lazy binding). | [Chapitre 2](/02-chaine-compilation-gnu/README.md) |  
| 3 | Bases de l'assembleur x86-64 pour le RE | Registres (généraux, `rsp`, `rbp`, `rip`, `RFLAGS`), instructions essentielles (`mov`, `lea`, `push`/`pop`, `call`/`ret`), arithmétique et logique, sauts conditionnels (signés vs non signés), pile et conventions d'appel System V AMD64, passage des paramètres (`rdi`→`r9`), méthode de lecture en 5 étapes, `call@plt` vs `syscall`, introduction SIMD (SSE/AVX). | [Chapitre 3](/03-assembleur-x86-64/README.md) |  
| 4 | Mise en place de l'environnement de travail | Distribution Linux (Ubuntu LTS/Debian/Kali), installation des outils en 5 vagues (socle, CLI, désassembleurs, frameworks, complémentaires), VM sandboxée (VirtualBox/QEMU/UTM), configuration réseau (NAT/host-only/isolé), structure du dépôt, compilation des binaires (`make all`), vérification avec `check_env.sh`. | [Chapitre 4](/04-environnement-travail/README.md) |

---

## ⏱️ Durée estimée

**~10-15 heures** pour un développeur ayant une pratique intermédiaire du C/C++ et une familiarité de base avec le terminal Linux.

Le chapitre 2 (chaîne de compilation, ELF, PLT/GOT) et le chapitre 3 (assembleur x86-64) représentent le gros du temps d'apprentissage. Si vous êtes déjà à l'aise avec l'assembleur, vous pouvez survoler le chapitre 3 et vous concentrer sur la méthode de lecture (section 3.7) et les conventions d'appel (sections 3.5-3.6). Le chapitre 4 est essentiellement pratique : comptez 1 à 2 heures pour monter la VM et vérifier l'installation.

---

## 📌 Prérequis

- **C/C++ intermédiaire** — Vous savez écrire, compiler et déboguer un programme de quelques centaines de lignes. Vous comprenez les pointeurs, l'allocation mémoire (`malloc`/`free`), les structures et les bases de la compilation séparée.  
- **Terminal Linux** — Vous êtes à l'aise avec la navigation dans le filesystem, l'édition de fichiers, l'utilisation de `make`, et les opérations courantes en ligne de commande (`grep`, `find`, pipes, redirections).  
- **Notions de mémoire** — Vous avez une idée générale de ce que sont la pile (stack), le tas (heap), et l'espace d'adressage d'un processus. Pas besoin de maîtriser les détails : c'est précisément ce que cette partie va consolider.  
- **Virtualisation** — Vous savez installer et lancer une machine virtuelle (VirtualBox, QEMU ou UTM). La configuration détaillée est couverte au chapitre 4.

> 💡 Si un de ces points vous semble flou, ce n'est pas bloquant — les chapitres 2 et 3 reprennent ces notions en profondeur. En revanche, un minimum de pratique en C et en terminal Linux est nécessaire pour suivre les exercices.

---

## ➡️ Partie suivante

Une fois votre environnement en place et les fondamentaux assimilés, vous passerez aux outils d'analyse statique : inspection binaire, désassemblage avec `objdump` et Ghidra, analyse hexadécimale avec ImHex, et diffing de binaires.

→ [**Partie II — Analyse Statique**](/partie-2-analyse-statique.md)

⏭️ [Chapitre 1 — Introduction au Reverse Engineering](/01-introduction-re/README.md)
