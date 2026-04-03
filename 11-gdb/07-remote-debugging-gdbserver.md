🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.7 — Remote debugging avec `gdbserver` (debugging sur cible distante)

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Pourquoi déboguer à distance

Jusqu'ici, GDB et le binaire analysé tournaient sur la même machine. C'est la configuration la plus simple, mais elle ne correspond pas toujours à la réalité du RE. Plusieurs situations imposent — ou recommandent fortement — de séparer le débogueur de la cible :

**Analyse de malware en sandbox.** Le principe fondamental de l'analyse de code malveillant (Partie VI) est l'isolation : le binaire suspect s'exécute dans une VM sandboxée, déconnectée du réseau réel. On ne veut pas installer un environnement GDB complet dans cette VM, ni y transférer nos scripts et notes d'analyse. Le débogage distant permet de piloter l'exécution depuis la machine hôte, en toute sécurité, avec tout notre confort d'outils.

**Cible aux ressources limitées.** Un système embarqué, un routeur, un IoT ne dispose généralement pas de la mémoire ou de l'espace disque pour héberger GDB (qui pèse plusieurs dizaines de Mo avec ses dépendances). `gdbserver`, en revanche, est un exécutable minimaliste — quelques centaines de Ko, aucune dépendance lourde — conçu pour tourner sur des cibles contraintes.

**Séparation des privilèges.** On peut vouloir exécuter le binaire en tant que root dans la VM (parce qu'il nécessite des privilèges) tout en pilotant l'analyse depuis un utilisateur non privilégié sur l'hôte. Le débogage distant découple les privilèges du débogueur de ceux du processus débogué.

**Confort de travail.** La VM d'analyse a un écran limité, pas d'interface graphique, un clavier différent. Travailler depuis notre machine hôte avec notre terminal configuré, nos scripts, nos fichiers `.gdb` et notre Ghidra ouvert à côté est incomparablement plus productif.

Le protocole qui rend tout cela possible est le **GDB Remote Serial Protocol** (RSP), un protocole texte simple sur TCP que GDB parle nativement. Côté cible, `gdbserver` est le serveur qui parle ce protocole et contrôle le processus débogué.

## Architecture du débogage distant

Le schéma est le suivant :

```
┌──────────────────────────┐         TCP/IP           ┌─────────────────────────┐
│      MACHINE HÔTE        │                          │     CIBLE (VM / device) │
│                          │                          │                         │
│  ┌────────────────────┐  │    GDB Remote Protocol   │  ┌──────────────────┐   │
│  │       GDB          │◄─┼──────────────────────────┼─►│   gdbserver      │   │
│  │  (client complet)  │  │       port 1234          │  │  (stub minimal)  │   │
│  └────────────────────┘  │                          │  └────────┬─────────┘   │
│                          │                          │           │ ptrace      │
│  - Symboles / DWARF      │                          │  ┌────────▼─────────┐   │
│  - Scripts .gdb          │                          │  │  Binaire cible   │   │
│  - Ghidra à côté         │                          │  │  (processus)     │   │
│                          │                          │  └──────────────────┘   │
└──────────────────────────┘                          └─────────────────────────┘
```

Le point essentiel : **le binaire n'a pas besoin d'être présent sur la machine hôte pour être débogué**. Seul `gdbserver` et le binaire doivent être sur la cible. Cependant, pour que GDB affiche les symboles, les noms de fonctions et le code source, il faut lui fournir une copie locale du binaire (ou au minimum un fichier de symboles).

`gdbserver` est un programme léger qui :
- Lance le binaire cible (ou s'attache à un processus existant).  
- Contrôle son exécution via `ptrace` (comme GDB le ferait localement).  
- Traduit les commandes GDB reçues via TCP en opérations `ptrace`.  
- Renvoie les résultats (état des registres, contenu mémoire) au client GDB.

Il ne fait aucune analyse, ne lit aucun symbole, ne désassemble rien. Toute l'intelligence est côté client GDB.

## Mise en place : le cas standard

### Côté cible : lancer `gdbserver`

`gdbserver` s'utilise de deux manières : lancer un nouveau processus ou s'attacher à un processus existant.

**Lancer un nouveau processus :**

```bash
# Sur la cible (VM)
$ gdbserver :1234 ./keygenme_O2_strip
Process ./keygenme_O2_strip created; pid = 4567  
Listening on port 1234  
```

`gdbserver` lance le binaire, le met immédiatement en pause (avant même l'exécution de `_start`), et écoute les connexions GDB sur le port TCP 1234. Le `:1234` est un raccourci pour `0.0.0.0:1234` — écoute sur toutes les interfaces.

On peut passer des arguments au programme :

```bash
$ gdbserver :1234 ./keygenme_O2_strip "MA-CLE-TEST"
```

Et rediriger l'entrée standard :

```bash
$ gdbserver :1234 ./keygenme_O2_strip < input.txt
```

**S'attacher à un processus existant :**

```bash
# Le programme tourne déjà avec le PID 4567
$ gdbserver --attach :1234 4567
Attached; pid = 4567  
Listening on port 1234  
```

Le processus est immédiatement mis en pause. C'est utile pour déboguer un démon ou un programme déjà en cours d'exécution sans le relancer.

### Côté hôte : se connecter depuis GDB

```bash
# Sur l'hôte
$ gdb -q ./keygenme_O2_strip
(gdb) target remote 192.168.56.10:1234
Remote debugging using 192.168.56.10:1234
0x00007ffff7fe4c40 in _start () from /lib64/ld-linux-x86-64.so.2
```

La commande `target remote` établit la connexion. L'adresse `192.168.56.10` est l'IP de la VM cible (typiquement une interface host-only, voir chapitre 4, section 4.4). GDB affiche le point d'arrêt initial — le programme est en pause sur la première instruction du loader.

On passe le binaire local (`./keygenme_O2_strip`) à GDB pour qu'il en lise les symboles et les sections. Si le binaire local contient des symboles DWARF (version debug) alors que la cible exécute la version strippée, on bénéficie du meilleur des deux mondes : les symboles de la version debug appliqués à l'exécution de la version strippée.

```bash
# Version debug locale, version strippée sur la cible
$ gdb -q ./keygenme_O2_debug
(gdb) target remote 192.168.56.10:1234
```

### `target remote` vs `target extended-remote`

GDB propose deux modes de connexion :

| Mode | Comportement |  
|---|---|  
| `target remote` | Connexion simple. Quand le programme termine (ou qu'on fait `kill`), la session se ferme. Il faut relancer `gdbserver` sur la cible pour recommencer. |  
| `target extended-remote` | Connexion persistante. On peut relancer le programme avec `run` sans relancer `gdbserver`. Le serveur reste actif entre les exécutions. |

Pour le RE itératif (on relance souvent le binaire avec des entrées différentes), `extended-remote` est nettement plus pratique :

```bash
# Côté cible
$ gdbserver --multi :1234
Listening on port 1234

# Côté hôte
$ gdb -q ./keygenme_O2_strip
(gdb) target extended-remote 192.168.56.10:1234
(gdb) set remote exec-file /home/user/keygenme_O2_strip
(gdb) run
...
(gdb) run "AUTRE-CLE"    # On peut relancer sans toucher à la cible
```

Le flag `--multi` côté `gdbserver` active le mode multi-session. La commande `set remote exec-file` indique le chemin du binaire **sur la cible** (qui peut différer du chemin local).

## Configuration réseau pour le RE

### Topologie recommandée

La configuration réseau idéale pour le débogage distant en contexte de RE (surtout pour l'analyse de malware) utilise un réseau **host-only** :

```
┌─────────────────┐     host-only (192.168.56.0/24)    ┌─────────────────┐
│   Machine hôte  │◄──────────────────────────────────►│   VM sandbox    │
│  192.168.56.1   │    (pas d'accès Internet)          │  192.168.56.10  │
│                 │                                    │                 │
│  GDB client     │                                    │  gdbserver      │
│  Ghidra         │                                    │  binaire cible  │
└─────────────────┘                                    └─────────────────┘
```

Le réseau host-only permet la communication entre l'hôte et la VM sans donner à la VM un accès à Internet. C'est le strict minimum de connectivité nécessaire pour le débogage distant, et ça empêche un malware analysé de communiquer avec l'extérieur.

La mise en place de ce réseau est détaillée au chapitre 4 (section 4.4) et au chapitre 26 pour les labs d'analyse de malware.

### Sécuriser la connexion

Le protocole GDB RSP n'offre **aucun chiffrement ni authentification**. Quiconque peut se connecter au port de `gdbserver` obtient un contrôle total sur le processus débogué. Sur un réseau host-only isolé, ce n'est pas un problème. Sur tout autre réseau, il faut tunneliser la connexion :

```bash
# Sur l'hôte : tunnel SSH vers la cible
$ ssh -L 1234:localhost:1234 user@192.168.56.10

# Puis dans GDB, se connecter au tunnel local
(gdb) target remote localhost:1234
```

Le port 1234 local est redirigé via SSH vers le port 1234 de la cible. La connexion est chiffrée et authentifiée.

Alternativement, on peut limiter `gdbserver` à écouter uniquement sur localhost et utiliser SSH pour y accéder :

```bash
# Sur la cible
$ gdbserver localhost:1234 ./binaire

# Depuis l'hôte, via SSH
$ ssh -L 1234:localhost:1234 user@cible
```

### Connexion via un pipe (sans réseau)

Pour les cas où même un réseau host-only n'est pas souhaitable, GDB peut se connecter via un pipe stdin/stdout à travers SSH :

```
(gdb) target remote | ssh user@192.168.56.10 gdbserver - ./binaire
```

Le `-` comme adresse indique à `gdbserver` de communiquer via stdin/stdout au lieu d'un socket TCP. GDB pipe sa communication à travers SSH. Aucun port réseau n'est ouvert sur la cible.

## Commandes spécifiques au mode distant

La plupart des commandes GDB fonctionnent de manière transparente en mode distant. Quelques commandes et réglages sont spécifiques :

### Charger les symboles de bibliothèques partagées

En mode distant, GDB ne trouve pas automatiquement les bibliothèques partagées de la cible (elles sont sur le système de fichiers de la VM, pas sur l'hôte). Si les bibliothèques de la cible diffèrent de celles de l'hôte (version de glibc différente, par exemple), les symboles seront incorrects.

On configure un répertoire local contenant les copies des bibliothèques de la cible :

```
(gdb) set sysroot /home/user/target-sysroot/
```

Le répertoire `target-sysroot/` reproduit la structure de la cible :

```
target-sysroot/
├── lib/
│   └── x86_64-linux-gnu/
│       ├── libc.so.6
│       ├── libpthread.so.0
│       └── ld-linux-x86-64.so.2
└── usr/
    └── lib/
        └── ...
```

Si les bibliothèques sont identiques entre hôte et cible (même distribution, même version), `set sysroot /` utilise les bibliothèques locales directement. Pour ignorer complètement les bibliothèques partagées :

```
(gdb) set sysroot
(gdb) set auto-solib-add off
```

### Transférer des fichiers

GDB peut télécharger des fichiers depuis la cible via le protocole distant :

```
(gdb) remote get /proc/self/maps /tmp/target_maps.txt
(gdb) remote get /etc/passwd /tmp/target_passwd.txt
```

Et envoyer des fichiers vers la cible :

```
(gdb) remote put /home/user/payload.bin /tmp/payload.bin
```

C'est utile pour récupérer des dumps mémoire, des fichiers de configuration, ou tout artefact produit par le binaire pendant l'analyse.

### Détacher et reconnecter

Pour se détacher de la cible sans tuer le processus :

```
(gdb) detach
Detaching from program: /home/user/keygenme, process 4567
```

Le processus continue à tourner sur la cible. En mode `extended-remote`, on peut s'y rattacher :

```
(gdb) target extended-remote 192.168.56.10:1234
(gdb) attach 4567
```

Pour tuer le processus distant :

```
(gdb) kill
```

Et pour déconnecter GDB du serveur sans affecter le processus :

```
(gdb) disconnect
```

### Rediriger l'entrée/sortie du programme

Par défaut, l'entrée et la sortie standard du programme débogué passent par le terminal de `gdbserver` sur la cible, pas par le terminal de GDB sur l'hôte. Si le programme attend une saisie clavier, il faut la taper **dans le terminal de la cible**.

Pour rediriger l'I/O vers l'hôte, on utilise le protocole de fichier distant de GDB :

```
(gdb) set remote exec-file /home/user/keygenme
(gdb) set inferior-tty /dev/pts/3
```

En pratique, la solution la plus simple est de préparer les entrées dans un fichier et de rediriger stdin sur la cible :

```bash
# Sur la cible
$ echo "TEST-KEY" > /tmp/input.txt
$ gdbserver :1234 ./keygenme < /tmp/input.txt
```

Ou d'utiliser `pwntools` côté hôte pour interagir programmatiquement avec le processus distant (section 11.9).

## Débogage distant de processus multi-threadés

Les programmes multi-threadés fonctionnent en mode distant, mais avec quelques particularités :

```
(gdb) info threads
  Id   Target Id                    Frame
* 1    Thread 4567.4567 "keygenme"  0x00007ffff7e62123 in read ()
  2    Thread 4567.4568 "keygenme"  0x00007ffff7e63456 in nanosleep ()
  3    Thread 4567.4569 "keygenme"  0x00007ffff7e62789 in poll ()
```

On bascule entre les threads comme en local :

```
(gdb) thread 2
[Switching to thread 2 (Thread 4567.4568)]
(gdb) backtrace
...
```

Un réglage important pour les programmes multi-threadés en mode distant :

```
(gdb) set non-stop on
```

En mode **non-stop**, quand un thread atteint un breakpoint, seul ce thread est arrêté — les autres continuent de tourner. C'est plus fidèle au comportement réel et évite les deadlocks artificiels causés par l'arrêt simultané de tous les threads. Le mode par défaut (`set non-stop off`) arrête tous les threads quand l'un d'eux touche un breakpoint.

## Automatiser avec un fichier de commandes

Pour un workflow de RE itératif, on rassemble toute la configuration dans un fichier :

```bash
# remote_analysis.gdb — session de débogage distant

# Connexion
target extended-remote 192.168.56.10:1234  
set remote exec-file /home/user/keygenme_O2_strip  

# Symboles
set sysroot /home/user/target-sysroot/

# Configuration
set disassembly-flavor intel  
set pagination off  
set follow-fork-mode child  
set detach-on-fork off  

# Annotations (adresses identifiées dans Ghidra)
set $main       = 0x401190  
set $check_key  = 0x401140  

# Breakpoints
break *$main  
break *$check_key  
break strcmp  

# Affichages
display/x $rax  
display/x $rdi  
display/6i $rip  

# Anti-anti-debug : neutraliser ptrace
catch syscall ptrace  
commands  
  silent
  set $rax = 0
  continue
end

# Lancer
run
```

Lancement :

```bash
$ gdb -q -x remote_analysis.gdb ./keygenme_O2_strip
```

Ce fichier unique contient la connexion, les symboles, les annotations, les breakpoints, les contournements anti-débogage et le lancement. On peut le versionner, le partager avec un collègue, et reproduire exactement la même session d'analyse.

## Alternatives et cas particuliers

### Débogage via QEMU user-mode

Pour les binaires d'une architecture différente (ARM, MIPS, RISC-V), QEMU en mode utilisateur (*user-mode emulation*) peut émuler le binaire et exposer un stub GDB :

```bash
# Émuler un binaire ARM et écouter sur le port 1234
$ qemu-arm -g 1234 ./binaire_arm
```

Côté hôte, on utilise un GDB multiarch :

```bash
$ gdb-multiarch ./binaire_arm
(gdb) target remote localhost:1234
```

C'est le principal moyen de déboguer des binaires d'architectures exotiques sans disposer du matériel physique. Le binaire s'exécute instruction par instruction dans l'émulateur, avec un accès complet aux registres et à la mémoire.

### Débogage via QEMU system-mode

Pour déboguer un noyau complet ou un firmware, QEMU en mode système expose aussi un stub GDB :

```bash
$ qemu-system-x86_64 -s -S -hda disk.img
# -s : écoute GDB sur le port 1234
# -S : démarre en pause (attend la connexion GDB)
```

```
(gdb) target remote localhost:1234
```

On débogue alors le **noyau** ou le code bare-metal, pas un processus utilisateur. C'est un usage avancé, hors du périmètre de ce tutoriel, mais le mécanisme de connexion est identique.

### `gdbserver` sans installation

Si la cible n'a pas `gdbserver` installé et qu'on ne peut pas (ou ne veut pas) l'installer, on peut transférer un binaire statique précompilé :

```bash
# Compiler gdbserver statiquement (sur une machine de build)
$ apt source gdb
$ cd gdb-*/gdb/gdbserver/
$ ./configure --host=x86_64-linux-gnu --enable-static
$ make LDFLAGS=-static
$ file gdbserver
gdbserver: ELF 64-bit LSB executable, statically linked, ...

# Transférer sur la cible
$ scp gdbserver user@cible:/tmp/
```

Le binaire statique n'a aucune dépendance et fonctionne sur n'importe quelle distribution Linux de la même architecture.

Sur les distributions courantes, `gdbserver` est souvent disponible dans un paquet séparé :

```bash
$ sudo apt install gdbserver          # Debian/Ubuntu
$ sudo dnf install gdb-gdbserver      # Fedora/RHEL
```

## Diagnostic des problèmes courants

| Symptôme | Cause probable | Solution |  
|---|---|---|  
| `Connection refused` | `gdbserver` n'est pas lancé ou écoute sur un autre port | Vérifier le processus et le port sur la cible (`ss -tlnp`) |  
| `Connection timed out` | Pare-feu, mauvaise IP, réseau non configuré | Tester la connectivité avec `ping` et `nc -z <ip> <port>` |  
| `Remote 'g' packet reply is too long` | Architecture 32/64 bits différente entre GDB et la cible | Utiliser `set architecture i386` ou le bon GDB (multiarch) |  
| Symboles absents ou incorrects | Sysroot non configuré ou bibliothèques différentes | Configurer `set sysroot` avec les bibliothèques de la cible |  
| Le programme semble bloqué | L'I/O est sur le terminal de la cible, pas de l'hôte | Rediriger stdin depuis un fichier ou utiliser le terminal de la cible |  
| `Cannot access memory` | Les adresses ASLR diffèrent de celles attendues | Recalculer la base (section 11.4) ou désactiver ASLR sur la cible |

Pour activer les logs du protocole en cas de problème de communication :

```
(gdb) set debug remote 1
```

GDB affichera chaque paquet envoyé et reçu, ce qui permet de diagnostiquer les échanges défaillants.

---

> **À retenir :** Le débogage distant avec `gdbserver` sépare le confort d'analyse (côté hôte) de l'exécution du binaire (côté cible). C'est une nécessité pour l'analyse de malware en sandbox et pour les cibles embarquées, mais c'est aussi un gain de productivité pour toute analyse où l'on veut garder ses outils, scripts et notes sur la machine principale. La mise en place est simple — `gdbserver :1234 ./binaire` d'un côté, `target remote <ip>:1234` de l'autre — et 95 % des commandes GDB fonctionnent de manière transparente. Le seul effort supplémentaire est la gestion des symboles et des bibliothèques partagées via `set sysroot`.

⏭️ [GDB Python API — scripting et automatisation](/11-gdb/08-gdb-python-api.md)
