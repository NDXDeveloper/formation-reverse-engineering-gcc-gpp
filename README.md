# Formation Reverse Engineering — Chaîne GNU (GCC/G++)

> **Ce contenu est strictement éducatif et éthique.**  
> Voir [LICENSE](/LICENSE) pour les conditions d'utilisation complètes.

Formation complète au **Reverse Engineering** de binaires natifs compilés avec la chaîne GNU (GCC/G++), enrichie de modules bonus sur les binaires **.NET/C#**, **Rust** et **Go**.

**36 chapitres** · **9 parties** · **~120 heures** de contenu · **20+ binaires d'entraînement** · **Checkpoints avec corrigés**

---

## 🎯 Objectifs

À l'issue de cette formation, vous serez capable de :

- Comprendre la structure interne d'un binaire ELF produit par GCC/G++  
- Mener une analyse statique complète (désassemblage, décompilation, inspection hexadécimale, diffing)  
- Mener une analyse dynamique (débogage GDB, hooking Frida, fuzzing AFL++, exécution symbolique angr/Z3)  
- Reverser du C++ complexe (vtables, RTTI, name mangling, STL, templates, smart pointers)  
- Identifier et contourner les protections courantes (ASLR, PIE, canaries, RELRO, UPX, obfuscation)  
- Analyser du code malveillant en environnement isolé (ransomware, dropper, packing)  
- Appliquer ces techniques sur des binaires .NET/C# (dnSpy, ILSpy, Frida-CLR)  
- Aborder le RE de binaires Rust et Go (name mangling, runtime, structures spécifiques)  
- Automatiser vos workflows RE (scripts Python, Ghidra headless, règles YARA, pipelines CI/CD)

---

## 👥 Public visé

| Profil | Prérequis |  
|:---|:---|  
| Développeur C/C++ souhaitant comprendre ses binaires | Bases du C/C++ |  
| Développeur .NET/C# curieux du RE | Bases du C# + notions de compilation |  
| Développeur Rust/Go confronté à du RE | Bases du langage + notions ELF |  
| Étudiant en cybersécurité | Bases Linux + ligne de commande |  
| Participant CTF débutant/intermédiaire | Aucun prérequis RE |

---

## 📦 Structure du dépôt

```
formation-reverse-engineering-gcc-gpp/
├── README.md                        ← Ce fichier
├── SOMMAIRE.md                      ← Table des matières détaillée (36 chapitres)
├── LICENSE                          ← MIT + disclaimer éthique
├── check_env.sh                     ← Script de vérification de l'environnement
│
├── preface.md                       ← Préface de la formation
├── partie-1-fondamentaux.md         ← Introduction Partie I
├── partie-2-analyse-statique.md     ← Introduction Partie II
├── ...                              ← (une page d'intro par partie)
├── partie-9-ressources.md           ← Introduction Partie IX
│
├── 01-introduction-re/              ← Chapitre 1 — Introduction au RE
│   ├── README.md
│   ├── 01-definition-re.md
│   ├── ...
│   └── checkpoint.md
├── 02-chaine-compilation-gnu/       ← Chapitre 2 — Chaîne de compilation GNU
├── ...                              ← Chapitres 3 à 36 (même structure)
├── 36-ressources-progresser/        ← Chapitre 36 — Ressources pour progresser
│
├── annexes/                         ← Annexes A à K
│   ├── README.md
│   └── ...
│
├── binaries/                        ← Tous les binaires d'entraînement
│   ├── Makefile                     ← `make all` pour tout recompiler
│   ├── ch05-keygenme/               ← Chapitres 5–6 (triage, ImHex)
│   ├── ch06-fileformat/
│   ├── ch08-oop/
│   ├── ch16-optimisations/          ← Chapitre 16 (optimisations GCC)
│   ├── ch17-oop/                    ← Chapitre 17 (C++ RE)
│   ├── ch20-keygenme/               ← Chapitre 20 (décompilation)
│   ├── ch20-network/
│   ├── ch20-oop/
│   ├── ch21-keygenme/               ← Chapitre 21 (cas pratique keygenme)
│   ├── ch22-oop/                    ← Chapitre 22 (cas pratique OOP + plugins)
│   ├── ch23-network/                ← Chapitre 23 (cas pratique réseau)
│   ├── ch24-crypto/                 ← Chapitre 24 (cas pratique crypto)
│   ├── ch25-fileformat/             ← Chapitre 25 (cas pratique format fichier)
│   ├── ch27-ransomware/             ← ⚠️ Sandbox uniquement
│   ├── ch28-dropper/                ← ⚠️ Sandbox uniquement
│   ├── ch29-packed/                 ← Chapitre 29 (packing/unpacking)
│   ├── ch32-dotnet/                 ← Chapitre 32 (.NET LicenseChecker)
│   ├── ch33-rust/                   ← Chapitre 33 (crackme Rust)
│   └── ch34-go/                     ← Chapitre 34 (crackme Go)
│
├── scripts/                         ← Scripts Python utilitaires
│   ├── triage.py                    ← Triage automatique d'un binaire
│   ├── keygen_template.py           ← Template keygen pwntools
│   └── batch_analyze.py             ← Analyse batch Ghidra headless
│
├── hexpat/                          ← Patterns ImHex (.hexpat)
│   ├── elf_header.hexpat            ← Header ELF générique
│   ├── ch06_fileformat.hexpat       ← Format CDB (chapitre 6)
│   ├── ch23_protocol.hexpat         ← Protocole réseau ch23
│   ├── ch24_crypt24.hexpat          ← Format CRYPT24 (chapitre 24)
│   └── ch25_fileformat.hexpat       ← Format CFR (chapitre 25)
│
├── yara-rules/                      ← Règles YARA
│   ├── crypto_constants.yar         ← Détection constantes crypto (AES, SHA, MD5…)
│   └── packer_signatures.yar        ← Signatures de packers (UPX…)
│
└── solutions/                       ← Corrigés des checkpoints (⚠️ spoilers)
    ├── ch01-checkpoint-solution.md
    ├── ch02-checkpoint-solution.md
    ├── ...
    ├── ch21-checkpoint-keygen.py
    ├── ch22-checkpoint-plugin.cpp
    ├── ch23-checkpoint-client.py
    ├── ch24-checkpoint-decrypt.py
    ├── ch25-checkpoint-parser.py
    ├── ch25-checkpoint-solution.hexpat
    ├── ch27-checkpoint-decryptor.py
    ├── ch28-checkpoint-fake-c2.py
    ├── ch34-checkpoint-solution.md
    └── ch35-checkpoint-batch.py
```

---

## 🛠️ Outils utilisés

### Analyse statique

| Outil | Rôle | Gratuit |  
|:---|:---|:---:|  
| `readelf`, `objdump`, `nm` | Inspection ELF / Binutils | ✅ |  
| `checksec` | Inventaire des protections | ✅ |  
| `strace` / `ltrace` | Appels système et bibliothèques | ✅ |  
| **ImHex** | Hex editor avancé + patterns `.hexpat` + YARA | ✅ |  
| **Ghidra** | Désassembleur / décompilateur (NSA) | ✅ |  
| **Radare2 / Cutter** | Analyse CLI + GUI (basé sur Rizin) | ✅ |  
| IDA Free | Désassembleur de référence (version gratuite) | ✅ |  
| Binary Ninja Cloud | Désassembleur moderne (version cloud gratuite) | ✅ |  
| **BinDiff** / Diaphora | Diffing de binaires | ✅ |  
| **RetDec** | Décompilateur statique offline (CLI) | ✅ |

### Analyse dynamique

| Outil | Rôle | Gratuit |  
|:---|:---|:---:|  
| **GDB** + GEF / pwndbg / PEDA | Débogage natif amélioré | ✅ |  
| **Frida** | Instrumentation dynamique + hooking | ✅ |  
| `pwntools` | Scripting d'interactions avec un binaire | ✅ |  
| Valgrind / ASan / UBSan / MSan | Analyse mémoire et comportement runtime | ✅ |  
| **AFL++** / libFuzzer | Fuzzing coverage-guided | ✅ |  
| **angr** | Exécution symbolique | ✅ |  
| **Z3** | Solveur de contraintes (SMT) | ✅ |

### Reverse .NET / C#

| Outil | Rôle | Gratuit |  
|:---|:---|:---:|  
| **dnSpy / dnSpyEx** | Décompilation + débogage .NET intégré | ✅ |  
| **ILSpy** | Décompilation C# open source | ✅ |  
| dotPeek | Décompilation JetBrains | ✅ |  
| de4dot | Désobfuscation d'assemblies .NET | ✅ |  
| Frida-CLR | Hooking de méthodes .NET | ✅ |

---

## 🚀 Démarrage rapide

### 1. Cloner le dépôt

```bash
git clone https://github.com/NDXDeveloper/formation-reverse-engineering-gcc-gpp.git  
cd formation-reverse-engineering-gcc-gpp  
```

### 2. Installer les dépendances essentielles (Debian/Ubuntu/Kali)

```bash
sudo apt update && sudo apt install -y \
    gcc g++ make gdb ltrace strace binutils \
    bsdextrautils checksec valgrind python3-pip binwalk

pip3 install pwntools pyelftools lief frida-tools angr

# AFL++
sudo apt install -y afl++
```

> 💡 Pour Ghidra, ImHex et les outils GUI, consultez le **[Chapitre 4](/04-environnement-travail/README.md)** qui détaille l'installation pas à pas.

### 3. Vérifier l'environnement

```bash
chmod +x check_env.sh
./check_env.sh
```

Ce script vérifie que tous les outils requis sont installés et fonctionnels.

### 4. Compiler tous les binaires d'entraînement

```bash
cd binaries/  
make all  
```

Chaque `Makefile` de chapitre produit plusieurs variantes :

```
*_O0          ← sans optimisation, avec symboles (-O0 -g)
*_O2          ← optimisé -O2, avec symboles
*_O3          ← optimisé -O3, avec symboles
*_strip       ← strippé (sans symboles, -O0 -s)
*_O2_strip    ← optimisé + strippé (cas le plus réaliste)
```

### 5. Commencer la formation

```bash
# Ouvrir le sommaire détaillé
xdg-open SOMMAIRE.md
```

Ou commencez directement par le **[Chapitre 1 — Qu'est-ce que le RE ?](/01-introduction-re/README.md)**

---

## ⚠️ Avertissement — Partie VI (Malware)

Les binaires des chapitres 27 et 28 (`ch27-ransomware/`, `ch28-dropper/`) sont des **prototypes pédagogiques volontairement limités** :

- Le ransomware chiffre uniquement `/tmp/test/` avec une clé AES hardcodée  
- Le dropper communique uniquement sur `127.0.0.1:4444`, sans persistance  
- **Ne jamais les compiler ou exécuter en dehors d'une VM snapshottée et isolée du réseau**

Le **[Chapitre 26](/26-lab-securise/README.md)** détaille la mise en place du lab sécurisé — il doit être complété avant tout travail sur les chapitres 27-29.

---

## 📚 Table des matières

| Partie | Contenu | Chapitres |  
|:---|:---|:---:|  
| **[I](/partie-1-fondamentaux.md)** — Fondamentaux | Intro RE, chaîne GNU, assembleur x86-64, environnement | 1 – 4 |  
| **[II](/partie-2-analyse-statique.md)** — Analyse Statique | Binutils, ImHex, objdump, Ghidra, IDA, Radare2, Binary Ninja, diffing | 5 – 10 |  
| **[III](/partie-3-analyse-dynamique.md)** — Analyse Dynamique | GDB, GEF/pwndbg, Frida, Valgrind/Sanitizers, AFL++/libFuzzer | 11 – 15 |  
| **[IV](/partie-4-techniques-avancees.md)** — Techniques Avancées | Optimisations GCC, C++ RE, exécution symbolique, anti-reversing, décompilation | 16 – 20 |  
| **[V](/partie-5-cas-pratiques.md)** — Cas Pratiques | Keygenme, OOP + plugins, réseau, crypto, format custom | 21 – 25 |  
| **[VI](/partie-6-malware.md)** — Malware (sandbox) | Lab sécurisé, ransomware, dropper, unpacking | 26 – 29 |  
| **[VII](/partie-7-dotnet.md)** — Bonus .NET/C# | RE .NET, ILSpy, dnSpy, Frida-CLR | 30 – 32 |  
| **[VIII](/partie-8-rust-go.md)** — Bonus Rust & Go | Spécificités RE Rust, spécificités RE Go | 33 – 34 |  
| **[IX](/partie-9-ressources.md)** — Ressources | Scripting, automatisation, CTF, lectures, certifications | 35 – 36 |

➡️ **[Table des matières détaillée (SOMMAIRE.md)](/SOMMAIRE.md)**

---

## 🧭 Parcours recommandés

Selon votre profil, vous pouvez suivre la formation de manière linéaire ou ciblée :

| Objectif | Parcours conseillé |  
|:---|:---|  
| **Formation complète** | Parties I → IX dans l'ordre |  
| **Débuter le RE rapidement** | Chapitres 1–5, puis 11, puis 21 (keygenme) |  
| **Se préparer aux CTF** | Chapitres 3, 5, 8, 11, 13, 18, 21 |  
| **Analyse de malware** | Parties I–III, puis Partie VI directement |  
| **RE .NET / C# uniquement** | Chapitre 1, puis Partie VII |  
| **RE Rust / Go** | Chapitres 1–5, 8, 11, puis Partie VIII |

---

## 🎯 Checkpoints

Chaque chapitre (ou groupe de chapitres) se termine par un **checkpoint** : un exercice pratique qui valide les acquis avant de passer à la suite. Les corrigés sont dans `solutions/`.

> ⚠️ Essayez toujours de résoudre le checkpoint par vous-même avant de consulter la solution.

---

## 🤝 Contribuer

Les contributions sont les bienvenues :

- Correction d'erreurs techniques ou typographiques  
- Ajout de variantes de binaires d'entraînement  
- Ajout de patterns `.hexpat` ou de règles YARA  
- Traduction en anglais de chapitres

Merci d'ouvrir une **issue** avant toute pull request majeure.

---

## 📄 Licence

[MIT](/LICENSE) — © 2025-2026 [Nicolas DEOUX / NDXDeveloper]  
Ce contenu est strictement éducatif et éthique. Voir le [disclaimer complet](/LICENSE).
