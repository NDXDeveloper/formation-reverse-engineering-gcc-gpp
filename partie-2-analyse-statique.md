🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Partie II — Analyse Statique

L'analyse statique consiste à examiner un binaire **sans jamais l'exécuter** : on travaille sur le fichier tel qu'il est sur disque, on lit ses headers, ses sections, ses chaînes de caractères, son code désassemblé. C'est la première étape de tout Reverse Engineering — celle qui permet de formuler des hypothèses solides avant de poser le moindre breakpoint. Cette partie vous emmène du premier contact brut avec un binaire inconnu (`file`, `strings`) jusqu'à la reconstruction de structures complexes dans Ghidra, en passant par l'analyse hexadécimale fine avec ImHex et la comparaison de versions avec BinDiff.

---

## 🎯 Objectifs de cette partie

À l'issue de ces six chapitres, vous serez capable de :

1. **Réaliser un triage complet d'un binaire inconnu en moins de 5 minutes** — identifier son format, ses dépendances, ses protections, ses chaînes lisibles et ses appels système probables.  
2. **Écrire des patterns ImHex (`.hexpat`)** pour visualiser et annoter des structures binaires arbitraires : headers ELF, protocoles custom, formats de fichiers propriétaires.  
3. **Désassembler et lire le code machine** produit par GCC à différents niveaux d'optimisation, en syntaxe Intel comme AT&T, et démêler le name mangling C++ avec `c++filt`.  
4. **Naviguer efficacement dans Ghidra** : utiliser le decompiler, tracer les cross-references, reconstruire des structures C/C++ (vtables, structs, enums), et automatiser des tâches avec des scripts Java ou Python.  
5. **Utiliser au moins deux désassembleurs** parmi Ghidra, IDA Free, Radare2 et Binary Ninja, et choisir le bon outil selon le contexte.  
6. **Comparer deux versions d'un même binaire** pour identifier précisément les fonctions modifiées — compétence directement applicable à l'analyse de patchs de sécurité.

---

## 📋 Chapitres

| N° | Titre | Description | Lien |  
|----|-------|-------------|------|  
| 5 | Outils d'inspection binaire de base | `file`, `strings`, `xxd`, `readelf`, `objdump`, `nm`, `ldd`, `strace`, `ltrace`, `checksec` — triage rapide et premiers réflexes face à un binaire inconnu. | [Chapitre 5](/05-outils-inspection-base/README.md) |  
| 6 | ImHex : analyse hexadécimale avancée | Éditeur hex nouvelle génération : langage de patterns `.hexpat`, parsing de headers ELF et structures custom, colorisation, diff binaire, règles YARA. | [Chapitre 6](/06-imhex/README.md) |  
| 7 | Désassemblage avec objdump et Binutils | Désassemblage en ligne de commande, syntaxe AT&T vs Intel, impact des optimisations GCC, prologues/épilogues, name mangling C++ et `c++filt`. | [Chapitre 7](/07-objdump-binutils/README.md) |  
| 8 | Désassemblage avancé avec Ghidra | Import ELF, CodeBrowser, decompiler, renommage et typage, vtables et RTTI GCC, cross-references, scripts Java/Python, mode headless. | [Chapitre 8](/08-ghidra/README.md) |  
| 9 | IDA Free, Radare2 et Binary Ninja | Workflows alternatifs : IDA Free sur binaire GCC, Radare2/Cutter en CLI et GUI, scripting r2pipe, Binary Ninja Cloud, comparatif détaillé des quatre outils. | [Chapitre 9](/09-ida-radare2-binja/README.md) |  
| 10 | Diffing de binaires | Comparer deux versions d'un binaire : BinDiff (Google), Diaphora (open source), `radiff2` en CLI. Cas pratique d'identification d'un patch de vulnérabilité. | [Chapitre 10](/10-diffing-binaires/README.md) |

---

## 🛠️ Outils couverts

- **`file`** — identification du type de fichier (ELF, PE, script, données).  
- **`strings`** — extraction des chaînes ASCII/Unicode lisibles dans un binaire.  
- **`xxd` / `hexdump`** — dump hexadécimal brut en ligne de commande.  
- **`readelf`** — inspection détaillée des headers, sections et segments ELF.  
- **`objdump`** — désassemblage et affichage des tables de symboles.  
- **`nm`** — listing des symboles (fonctions, variables globales) d'un binaire ou d'un `.o`.  
- **`ldd`** — affichage des dépendances dynamiques (bibliothèques partagées).  
- **`strace`** — trace des appels système (syscalls) à l'exécution.  
- **`ltrace`** — trace des appels de bibliothèques dynamiques (libc, etc.).  
- **`checksec`** — inventaire des protections binaires (PIE, NX, canary, RELRO, ASLR).  
- **`c++filt`** — démanglement des symboles C++ (Itanium ABI).  
- **ImHex** — éditeur hexadécimal avancé avec langage de patterns, YARA, diff et désassembleur intégré.  
- **Ghidra** — suite de RE de la NSA : désassembleur, decompiler, analyse de types, scripting.  
- **IDA Free** — désassembleur interactif de référence (version gratuite).  
- **Radare2 / Cutter** — framework de RE en CLI (r2) et GUI (Cutter), scriptable via r2pipe.  
- **Binary Ninja** — désassembleur moderne avec IL intermédiaire (version Cloud gratuite).  
- **BinDiff** — diffing de binaires par Google, intégré à Ghidra et IDA.  
- **Diaphora** — plugin open source de diffing pour Ghidra et IDA.  
- **`radiff2`** — diffing en ligne de commande via Radare2.

---

## ⏱️ Durée estimée

**~18-25 heures** pour un développeur ayant complété la Partie I.

Le chapitre 5 (outils CLI) se parcourt rapidement si vous avez l'habitude du terminal (~2h). Le chapitre 6 (ImHex) demande de la pratique sur les patterns `.hexpat` (~3-4h). Les chapitres 7 à 9 constituent le cœur de cette partie : prévoyez ~4h pour `objdump`/Binutils, ~5-6h pour Ghidra (l'outil le plus complet à maîtriser), et ~3h pour le tour d'horizon IDA/r2/BinJa. Le chapitre 10 (diffing) est plus court (~2h) mais très concret.

---

## 📌 Prérequis

Avoir complété la **[Partie I — Fondamentaux & Environnement](/partie-1-fondamentaux.md)**, ou disposer des connaissances équivalentes :

- Comprendre la structure d'un binaire ELF (headers, sections `.text`/`.data`/`.rodata`/`.plt`/`.got`).  
- Savoir lire un listing assembleur x86-64 de base (registres, `mov`, `call`, `cmp`, `jz`, prologue/épilogue).  
- Connaître la convention d'appel System V AMD64 (passage des arguments dans `rdi`, `rsi`, `rdx`…).  
- Disposer d'un environnement de travail fonctionnel avec tous les outils installés (`check_env.sh` au vert).

---

## ⬅️ Partie précédente

← [**Partie I — Fondamentaux & Environnement**](/partie-1-fondamentaux.md)

## ➡️ Partie suivante

Une fois l'analyse statique maîtrisée, vous passerez à l'analyse dynamique : exécuter le binaire sous contrôle avec GDB, instrumenter ses fonctions avec Frida, détecter ses failles avec Valgrind et AFL++.

→ [**Partie III — Analyse Dynamique**](/partie-3-analyse-dynamique.md)

⏭️ [Chapitre 5 — Outils d'inspection binaire de base](/05-outils-inspection-base/README.md)
