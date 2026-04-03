🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 4.2 — Installation et configuration des outils essentiels (liste versionnée)

> 🎯 **Objectif de cette section** : installer et configurer l'ensemble des outils nécessaires à la formation, de manière ordonnée et vérifiable. À la fin de cette section, votre VM contiendra tout ce qu'il faut pour aborder n'importe quel chapitre.

---

## Stratégie d'installation

Nous procédons en **cinq vagues**, des fondations vers les outils spécialisés. Cet ordre n'est pas arbitraire : chaque vague peut dépendre de la précédente. Par exemple, angr requiert Python 3 et pip (vague 1), et GEF requiert GDB (vague 2).

| Vague | Catégorie | Exemples |  
|---|---|---|  
| 1 | Socle système et langages | GCC, G++, Make, Python 3, pip, Java, Git |  
| 2 | Outils en ligne de commande pour l'inspection et le débogage | binutils, GDB, strace, ltrace, Valgrind, checksec, YARA |  
| 3 | Désassembleurs et éditeurs graphiques | Ghidra, Radare2, Cutter, ImHex, IDA Free |  
| 4 | Frameworks d'instrumentation, fuzzing et exécution symbolique | Frida, AFL++, angr, Z3, pwntools |  
| 5 | Outils complémentaires et optionnels | BinDiff, Wireshark, UPX, binwalk, outils .NET/Rust/Go |

> 💡 **Snapshot avant de commencer.** Si vous avez déjà créé votre VM (section 4.3), prenez un snapshot nommé `pre-install` avant de lancer les installations. En cas de problème, vous pourrez revenir à un état propre sans tout réinstaller le système.

---

## Prérequis : mise à jour du système

Avant toute installation, mettez à jour les dépôts et les paquets existants :

```bash
[vm] sudo apt update && sudo apt upgrade -y
```

Installez ensuite les dépendances de base communes à de nombreux outils :

```bash
[vm] sudo apt install -y \
    build-essential \
    curl \
    wget \
    git \
    unzip \
    pkg-config \
    libssl-dev \
    libffi-dev \
    zlib1g-dev \
    software-properties-common \
    apt-transport-https \
    ca-certificates \
    gnupg
```

---

## Vague 1 — Socle système et langages

### GCC / G++ / Make / Binutils

Le paquet `build-essential` installé ci-dessus fournit déjà GCC, G++, Make et les binutils GNU (dont `as`, `ld`, `objdump`, `readelf`, `nm`, `strings`, `strip`, `objcopy`, `c++filt`). Vérifiez :

```bash
[vm] gcc --version        # attendu : 13.x sur Ubuntu 24.04
[vm] g++ --version
[vm] make --version
[vm] objdump --version
[vm] readelf --version
```

> 📌 **Chapitre concerné** : 2 (chaîne de compilation GNU), puis tout au long de la formation pour compiler les binaires d'entraînement.

### Python 3 et pip

Python est le langage de scripting omniprésent en RE. Nous l'utilisons pour angr, pwntools, Frida, pyelftools, lief, r2pipe, et les scripts personnalisés.

```bash
[vm] sudo apt install -y python3 python3-pip python3-venv python3-dev
```

Créez un **environnement virtuel dédié** à la formation. Cela isole les paquets Python de ceux du système et évite les conflits de versions :

```bash
[vm] python3 -m venv ~/re-venv
[vm] echo 'source ~/re-venv/bin/activate' >> ~/.bashrc
[vm] source ~/re-venv/bin/activate
```

À partir de maintenant, toutes les commandes `pip install` s'exécuteront dans cet environnement virtuel.

```bash
[vm] python3 --version    # attendu : 3.12.x sur Ubuntu 24.04
[vm] pip --version
```

> 📌 **Chapitres concernés** : 11 (scripts GDB Python), 13 (Frida), 15 (fuzzing), 18 (angr/Z3), 21–28 (cas pratiques), 35 (automatisation).

### Java (JDK) — requis par Ghidra

Ghidra nécessite un JDK 17 ou supérieur. OpenJDK fait l'affaire :

```bash
[vm] sudo apt install -y openjdk-21-jdk
[vm] java -version        # attendu : openjdk 21.x
```

> 📌 **Chapitres concernés** : 8 (Ghidra), 9 (comparatif désassembleurs), 10 (BinDiff/Diaphora), 20 (décompilation).

### Git

Déjà installé via les dépendances de base. Vérifiez :

```bash
[vm] git --version
```

Git servira à cloner le dépôt de la formation et, le cas échéant, à récupérer les sources de certains outils.

---

## Vague 2 — Outils CLI d'inspection et de débogage

### GDB (GNU Debugger)

```bash
[vm] sudo apt install -y gdb
[vm] gdb --version        # attendu : 15.x sur Ubuntu 24.04
```

GDB sera enrichi par des extensions (GEF, pwndbg) un peu plus loin. Pour l'instant, nous installons la version de base.

> 📌 **Chapitres concernés** : 11 (GDB fondamental), 12 (extensions), 21–29 (cas pratiques).

### strace / ltrace

```bash
[vm] sudo apt install -y strace ltrace
[vm] strace --version
[vm] ltrace --version
```

> 📌 **Chapitres concernés** : 5 (outils d'inspection de base), 23 (binaire réseau), 26–28 (analyse de malware).

### Valgrind

```bash
[vm] sudo apt install -y valgrind kcachegrind
[vm] valgrind --version   # attendu : 3.22.x+
```

`kcachegrind` est l'interface graphique pour visualiser les profils Callgrind.

> 📌 **Chapitres concernés** : 14 (Valgrind et sanitizers), 24 (extraction de clés crypto en mémoire).

### checksec

`checksec` est un script qui inventorie les protections de sécurité d'un binaire (PIE, NX, canary, RELRO, ASLR).

```bash
[vm] sudo apt install -y checksec
[vm] checksec --version
```

Si le paquet n'est pas disponible ou trop ancien, installez la version upstream :

```bash
[vm] pip install checksec.py
```

> 📌 **Chapitres concernés** : 5 (triage rapide), 19 (anti-reversing), 21 (keygenme), 27–29 (malware).

### YARA

YARA permet d'écrire des règles de pattern matching sur des fichiers binaires.

```bash
[vm] sudo apt install -y yara
[vm] yara --version       # attendu : 4.3.x+
```

Pour l'utilisation depuis Python :

```bash
[vm] pip install yara-python
```

> 📌 **Chapitres concernés** : 6 (ImHex + YARA), 27 (ransomware), 35 (automatisation).

### Utilitaires système complémentaires

Ces outils sont souvent déjà présents mais il vaut mieux s'en assurer :

```bash
[vm] sudo apt install -y \
    file \
    xxd \
    bsdextrautils \
    binwalk \
    tree \
    tmux \
    nasm
```

- `file` : identification de type de fichier (chapitre 5).  
- `xxd` / `hexdump` : dumps hexadécimaux rapides (chapitre 5). `hexdump` est fourni par le paquet `bsdextrautils`.  
- `binwalk` : analyse de firmware et extraction de formats embarqués (chapitre 25).  
- `nasm` : assembleur x86-64, utile pour les expérimentations du chapitre 3.  
- `tmux` : multiplexeur de terminal — indispensable pour les sessions GDB longues.

---

## Vague 3 — Désassembleurs et éditeurs graphiques

### Ghidra

Ghidra n'est pas dans les dépôts apt. Téléchargez-le depuis le dépôt GitHub officiel de la NSA :

```bash
[vm] GHIDRA_VERSION="11.3"
[vm] wget "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_${GHIDRA_VERSION}_build/ghidra_${GHIDRA_VERSION}_PUBLIC_20250108.zip" \
     -O /tmp/ghidra.zip
[vm] sudo unzip /tmp/ghidra.zip -d /opt/
[vm] sudo ln -sf /opt/ghidra_${GHIDRA_VERSION}_PUBLIC /opt/ghidra
[vm] rm /tmp/ghidra.zip
```

> ⚠️ **Vérifiez le numéro de version et la date du build** sur la [page des releases GitHub](https://github.com/NationalSecurityAgency/ghidra/releases) avant de copier cette commande. Les URLs changent à chaque release.

Créez un alias ou un lanceur :

```bash
[vm] echo 'alias ghidra="/opt/ghidra/ghidraRun"' >> ~/.bashrc
[vm] source ~/.bashrc
[vm] ghidra    # devrait lancer l'interface graphique
```

> 📌 **Chapitres concernés** : 8 (prise en main), 9 (comparatif), 10 (diffing), 17 (C++ RE), 20 (décompilation), 22 (OOP), 27 (ransomware).

### Radare2 et Cutter

Radare2 est le couteau suisse en ligne de commande. Cutter est une interface graphique basée sur Rizin (un fork de Radare2), mais reste compatible avec les concepts et la plupart des commandes `r2`. C'est le GUI le plus abouti pour travailler visuellement avec un moteur d'analyse de la famille Radare.

Installez Radare2 depuis les sources pour obtenir la version la plus récente :

```bash
[vm] git clone --depth 1 https://github.com/radareorg/radare2.git /tmp/radare2
[vm] cd /tmp/radare2
[vm] sys/install.sh
[vm] r2 -v               # attendu : radare2 5.9.x+
```

Pour Cutter :

```bash
[vm] sudo apt install -y cutter
```

Si le paquet n'est pas disponible ou trop ancien, téléchargez l'AppImage depuis [cutter.re](https://cutter.re/) :

```bash
[vm] wget "https://github.com/rizinorg/cutter/releases/download/v2.4.0/Cutter-v2.4.0-Linux-x86_64.AppImage" \
     -O ~/tools/Cutter.AppImage
[vm] chmod +x ~/tools/Cutter.AppImage
```

> 📌 **Chapitres concernés** : 9 (Radare2/Cutter), 10 (radiff2 pour le diffing).

### ImHex

ImHex est un éditeur hexadécimal avancé avec support de patterns `.hexpat`, coloration de structures, désassembleur intégré et règles YARA.

Il n'existe pas de PPA officiel pour ImHex. Téléchargez le `.deb` depuis la [page des releases GitHub](https://github.com/WerWolv/ImHex/releases) :

```bash
[vm] IMHEX_VERSION="1.37.4"
[vm] wget "https://github.com/WerWolv/ImHex/releases/download/v${IMHEX_VERSION}/imhex-${IMHEX_VERSION}-Ubuntu-24.04-x86_64.deb" \
     -O /tmp/imhex.deb
[vm] sudo dpkg -i /tmp/imhex.deb
[vm] sudo apt install -f -y    # résout les dépendances manquantes
[vm] rm /tmp/imhex.deb
```

> ⚠️ Vérifiez la version et le nom exact du fichier `.deb` sur la page des releases. Les conventions de nommage changent entre versions.

> 📌 **Chapitres concernés** : 6 (chapitre dédié à ImHex), 21 (patching), 23 (protocole réseau), 24 (format chiffré), 25 (format custom), 27 (ransomware), 29 (unpacking).

### IDA Free (optionnel)

IDA Free est la version gratuite du désassembleur commercial IDA Pro. Elle est limitée (x86-64 uniquement, pas de décompilateur dans les anciennes versions, une cible à la fois), mais reste utile pour le chapitre 9 (comparatif d'outils).

Téléchargez l'installeur depuis [hex-rays.com/ida-free](https://hex-rays.com/ida-free/) et suivez les instructions :

```bash
[vm] chmod +x ida-free-*.run
[vm] ./ida-free-*.run
```

> 📌 **Chapitre concerné** : 9 (IDA Free workflow), 10 (BinDiff depuis IDA).

---

## Vague 4 — Frameworks d'instrumentation, fuzzing et exécution symbolique

> ⚠️ Toutes les commandes `pip install` qui suivent supposent que l'environnement virtuel `re-venv` est activé.

### GDB Extensions : GEF, pwndbg, PEDA

Ces trois extensions enrichissent GDB avec une interface améliorée, un affichage en temps réel des registres et de la stack, et des commandes spécialisées pour le RE et l'exploitation. **Vous n'avez besoin d'en installer qu'une seule à la fois** — elles modifient toutes le fichier `~/.gdbinit` et sont mutuellement exclusives.

Notre recommandation pour cette formation est **GEF** (GDB Enhanced Features), pour sa légèreté et sa compatibilité large :

```bash
[vm] bash -c "$(curl -fsSL https://gef.blah.cat/sh)"
[vm] gdb -q -ex "gef help" -ex quit    # vérifie que GEF se charge
```

Si vous préférez **pwndbg** (meilleur pour l'analyse de heap) :

```bash
[vm] git clone https://github.com/pwndbg/pwndbg ~/tools/pwndbg
[vm] cd ~/tools/pwndbg
[vm] ./setup.sh
```

Et pour **PEDA** :

```bash
[vm] git clone https://github.com/longld/peda.git ~/tools/peda
[vm] echo "source ~/tools/peda/peda.py" > ~/.gdbinit
```

> 💡 **Astuce** : vous pouvez installer les trois et basculer entre elles en modifiant `~/.gdbinit`. Certains utilisateurs maintiennent des fichiers séparés (`~/.gdbinit-gef`, `~/.gdbinit-pwndbg`, `~/.gdbinit-peda`) et utilisent un alias pour charger celui souhaité. Le chapitre 12 détaille cette configuration multi-extensions.

> 📌 **Chapitres concernés** : 12 (chapitre dédié aux extensions GDB), 21–29 (cas pratiques).

### Frida

Frida est un framework d'instrumentation dynamique. Il s'installe via pip (côté host) et nécessite un serveur sur la cible (ici la même machine) :

```bash
[vm] pip install frida-tools frida
[vm] frida --version      # attendu : 16.x+
```

Vérifiez que Frida peut s'attacher à un processus :

```bash
[vm] frida-trace -i "open" /bin/ls    # doit tracer les appels à open()
```

> 📌 **Chapitres concernés** : 13 (chapitre dédié à Frida), 24 (extraction de clés), 27–28 (malware).

### pwntools

pwntools est une bibliothèque Python pour le scripting d'interactions avec des binaires (entrées/sorties, patching, ROP, réseau).

```bash
[vm] pip install pwntools
[vm] python3 -c "from pwn import *; print(pwnlib.version)"
```

> 📌 **Chapitres concernés** : 11 (introduction pwntools), 21 (keygen), 23 (client réseau), 35 (automatisation).

### AFL++

AFL++ est le fuzzer coverage-guided de référence :

```bash
[vm] sudo apt install -y afl++
[vm] afl-fuzz --version
```

Si le paquet n'est pas disponible, compilez depuis les sources :

```bash
[vm] git clone https://github.com/AFLplusplus/AFLplusplus.git ~/tools/AFLplusplus
[vm] cd ~/tools/AFLplusplus
[vm] make distrib
[vm] sudo make install
```

> 📌 **Chapitres concernés** : 15 (chapitre dédié au fuzzing), 25 (fuzzing du parseur de format custom).

### angr et Z3

angr est un framework d'exécution symbolique. Z3 est le solveur SMT qu'il utilise en backend (installé automatiquement comme dépendance).

```bash
[vm] pip install angr
[vm] python3 -c "import angr; print(angr.__version__)"    # attendu : 9.2.x+
```

Pour utiliser Z3 directement (chapitre 18.4) :

```bash
[vm] pip install z3-solver
[vm] python3 -c "import z3; print(z3.get_version_string())"
```

> ⚠️ angr installe un grand nombre de dépendances et peut être lent à compiler sur une VM modeste. Comptez 5 à 15 minutes.

> 📌 **Chapitres concernés** : 18 (chapitre dédié), 21 (résolution automatique du keygenme).

### pyelftools et LIEF

Deux bibliothèques Python pour parser et manipuler des binaires ELF :

```bash
[vm] pip install pyelftools lief
[vm] python3 -c "import elftools; print('pyelftools OK')"
[vm] python3 -c "import lief; print(lief.__version__)"
```

> 📌 **Chapitres concernés** : 35 (automatisation et scripting).

### r2pipe

Interface Python pour piloter Radare2 par script :

```bash
[vm] pip install r2pipe
```

> 📌 **Chapitre concerné** : 9 (scripting r2pipe).

---

## Vague 5 — Outils complémentaires et optionnels

Les outils ci-dessous ne sont pas nécessaires dès le premier chapitre mais interviennent dans des parties spécifiques. Vous pouvez les installer maintenant ou au moment voulu.

### Wireshark et tcpdump

Pour l'analyse réseau (chapitre 23, parties VI) :

```bash
[vm] sudo apt install -y wireshark tcpdump
```

Lors de l'installation, répondez « Oui » à la question sur l'accès non-root aux captures, puis ajoutez votre utilisateur au groupe `wireshark` :

```bash
[vm] sudo usermod -aG wireshark $USER
```

> 📌 **Chapitres concernés** : 23 (protocole réseau), 26 (lab sécurisé), 28 (dropper).

### UPX

UPX est un packer/dépacker de binaires :

```bash
[vm] sudo apt install -y upx-ucl
[vm] upx --version
```

> 📌 **Chapitres concernés** : 19 (packing UPX), 29 (unpacking).

### BinDiff (optionnel)

BinDiff est un outil de diffing binaire de Google. Il s'intègre à Ghidra ou IDA. Téléchargez-le depuis [github.com/google/bindiff/releases](https://github.com/google/bindiff/releases) :

```bash
[vm] wget "https://github.com/google/bindiff/releases/download/v8/bindiff_8_amd64.deb" \
     -O /tmp/bindiff.deb
[vm] sudo dpkg -i /tmp/bindiff.deb
[vm] sudo apt install -f -y
```

> 📌 **Chapitre concerné** : 10 (diffing de binaires).

### Outils de monitoring pour le lab malware (Partie VI)

```bash
[vm] sudo apt install -y auditd inotify-tools sysdig
```

- `auditd` : audit des appels système au niveau noyau.  
- `inotify-tools` : surveillance des modifications du système de fichiers (`inotifywait`).  
- `sysdig` : capture et analyse d'événements système.

> 📌 **Chapitres concernés** : 26 (mise en place du lab), 27–28 (analyse de malware).

### Clang et sanitizers (Partie III–IV)

Clang fournit les sanitizers AddressSanitizer, UBSan et MSan, ainsi que libFuzzer :

```bash
[vm] sudo apt install -y clang llvm
[vm] clang --version
```

> 📌 **Chapitres concernés** : 14 (sanitizers), 15 (libFuzzer), 16 (comparaison GCC vs Clang).

### Outils .NET — Partie VII (optionnel)

Si vous comptez aborder les chapitres 30 à 32 sur le RE .NET :

```bash
[vm] sudo apt install -y dotnet-sdk-8.0
```

Pour ILSpy (en ligne de commande sous Linux) :

```bash
[vm] dotnet tool install --global ilspycmd
```

> 📌 **Chapitres concernés** : 30–32 (RE .NET).

### Toolchains Rust et Go — Partie VIII (optionnel)

Pour compiler les binaires d'entraînement Rust et Go des chapitres 33 et 34 :

```bash
# Rust
[vm] curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
[vm] source ~/.cargo/env
[vm] rustc --version

# Go
[vm] sudo apt install -y golang
[vm] go version
```

> 📌 **Chapitres concernés** : 33 (RE Rust), 34 (RE Go).

---

## Récapitulatif des versions testées

Le tableau ci-dessous liste les versions avec lesquelles cette formation a été testée. Des versions plus récentes fonctionneront dans la grande majorité des cas.

| Outil | Version testée | Installation |  
|---|---|---|  
| GCC / G++ | 13.2 | `apt` (via `build-essential`) |  
| Python 3 | 3.12 | `apt` |  
| Java (OpenJDK) | 21 | `apt` |  
| GDB | 15.0 | `apt` |  
| GEF | 2024.01+ | Script d'installation |  
| pwndbg | 2024.02+ | Git + setup.sh |  
| Ghidra | 11.3 | Téléchargement GitHub |  
| Radare2 | 5.9 | Compilation depuis source |  
| Cutter | 2.4 | `apt` ou AppImage |  
| ImHex | 1.37 | `.deb` depuis GitHub |  
| IDA Free | 9.0 | Téléchargement hex-rays.com |  
| Frida | 16.x | `pip` |  
| pwntools | 4.13 | `pip` |  
| AFL++ | 4.21c | `apt` ou compilation |  
| angr | 9.2 | `pip` |  
| Z3 | 4.13 | `pip` (via `z3-solver`) |  
| Valgrind | 3.22 | `apt` |  
| YARA | 4.3 | `apt` + `pip` (yara-python) |  
| Wireshark | 4.2 | `apt` |  
| UPX | 4.2 | `apt` |  
| BinDiff | 8 | `.deb` depuis GitHub |  
| pyelftools | 0.31 | `pip` |  
| LIEF | 0.15 | `pip` |  
| Clang / LLVM | 18.x | `apt` |  
| binwalk | 2.3 | `apt` |

---

## Organisation sur le disque

Après toutes ces installations, voici l'arborescence recommandée dans le home de votre utilisateur :

```
~/
├── re-venv/                  ← Environnement virtuel Python
├── tools/                    ← Outils installés manuellement
│   ├── pwndbg/               
│   ├── AFLplusplus/          (si compilé depuis source)
│   └── Cutter.AppImage       (si AppImage)
├── formation-re/             ← Dépôt cloné de la formation
│   ├── binaries/
│   ├── scripts/
│   ├── hexpat/
│   ├── yara-rules/
│   └── ...
└── .gdbinit                  ← Chargement de GEF (ou pwndbg/PEDA)
```

Les outils installés via `apt` se retrouvent dans `/usr/bin/` ou `/usr/local/bin/`. Les outils installés dans `/opt/` (comme Ghidra) sont accessibles via des alias dans `~/.bashrc`.

---

## En cas de problème

Quelques réflexes de diagnostic :

- **`command not found`** — Vérifiez que le paquet est installé (`which <outil>` ou `dpkg -l | grep <outil>`), que le `PATH` est correct, et que l'environnement virtuel Python est activé si l'outil est un paquet pip.  
- **Conflit de dépendances pip** — Assurez-vous d'être dans `re-venv`. Si le conflit persiste, essayez `pip install --force-reinstall <paquet>`.  
- **Ghidra ne se lance pas** — Vérifiez la version de Java (`java -version`). Ghidra 11.x exige JDK 17+.  
- **GEF / pwndbg ne se charge pas dans GDB** — Vérifiez le contenu de `~/.gdbinit`. Une seule extension doit être sourcée.  
- **Frida échoue avec « Failed to spawn »** — Vérifiez que vous avez les droits d'accès au processus cible (exécutez avec `sudo` si nécessaire, ou ajustez `ptrace_scope`).  
- **AFL++ : « Hmm, your system is configured to send core dump notifications to an external utility »** — Exécutez `echo core | sudo tee /proc/sys/kernel/core_pattern` avant de lancer le fuzzer.

---

## Résumé

À l'issue de cette section, votre VM contient :

- le **socle de compilation** (GCC, G++, Make, Clang) pour produire et recompiler les binaires d'entraînement ;  
- les **outils d'inspection** en ligne de commande (binutils, strace, ltrace, checksec, YARA, binwalk) pour le triage rapide ;  
- les **désassembleurs et éditeurs** graphiques (Ghidra, Radare2/Cutter, ImHex) pour l'analyse statique approfondie ;  
- les **frameworks dynamiques** (GDB + GEF, Frida, pwntools, Valgrind) pour l'analyse à l'exécution ;  
- les **moteurs d'analyse avancée** (AFL++, angr, Z3) pour le fuzzing et l'exécution symbolique ;  
- un **environnement Python isolé** (`re-venv`) contenant toutes les bibliothèques nécessaires.

Le script `check_env.sh` (section 4.7) validera automatiquement que tout est en place.

---


⏭️ [Création d'une VM sandboxée (VirtualBox / QEMU / UTM pour macOS)](/04-environnement-travail/03-creation-vm.md)
