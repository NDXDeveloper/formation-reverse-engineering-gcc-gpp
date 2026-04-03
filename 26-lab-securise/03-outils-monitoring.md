🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 26.3 — Outils de monitoring : `auditd`, `inotifywait`, `tcpdump`, `sysdig`

> **Chapitre 26 — Mise en place d'un lab d'analyse sécurisé**  
> **Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**

---

## Le rôle du monitoring dans l'analyse de malware

En section 26.1, nous avons défini l'isolation comportementale comme le troisième axe du lab : non pas empêcher le malware d'agir, mais **enregistrer chacune de ses actions** pour les analyser ensuite. Les outils de ce chapitre sont les caméras de surveillance de notre aquarium.

Rappelons une distinction importante. Dans les chapitres précédents, nous avons déjà utilisé `strace` et `ltrace` pour tracer les appels système et les appels de bibliothèque d'un processus ciblé. Ces outils restent indispensables et seront largement mobilisés dans les chapitres 27 et 28. Mais ils ont une limite : ils observent **un seul processus** (et éventuellement ses enfants). Or un malware peut lancer des processus indépendants, modifier des fichiers via des mécanismes indirects, ou interagir avec le système d'une manière que `strace` ne capture pas parce que l'activité se produit en dehors de l'arbre de processus tracé.

Les quatre outils présentés ici opèrent à un niveau différent. Ils ne se rattachent pas à un processus particulier : ils surveillent **le système dans son ensemble** — le noyau, le système de fichiers, le réseau. Ensemble, ils forment une couverture complète qui garantit qu'aucune action significative du sample ne passe inaperçue.

| Outil | Ce qu'il observe | Granularité | Mode |  
|---|---|---|---|  
| `auditd` | Appels système au niveau du noyau | Événement par événement | Temps réel + logs |  
| `inotifywait` | Modifications du système de fichiers | Fichier par fichier | Temps réel |  
| `tcpdump` | Trafic réseau brut | Paquet par paquet | Capture `.pcap` |  
| `sysdig` | Tout (syscalls, réseau, fichiers, processus) | Événement par événement | Temps réel + capture |

---

## `auditd` — L'audit du noyau Linux

### Qu'est-ce que `auditd` ?

Le framework d'audit Linux est un mécanisme intégré au noyau qui permet d'enregistrer des événements système de manière fiable et performante. Il se compose de deux parties : un composant noyau (`kauditd`) qui intercepte les événements, et un démon en espace utilisateur (`auditd`) qui reçoit ces événements et les écrit dans un fichier de log.

Ce qui rend `auditd` particulièrement adapté à l'analyse de malware, c'est sa position dans le système. Il opère au niveau du noyau, en amont de toute bibliothèque ou abstraction. Un malware peut contourner la libc en faisant des appels système directs via l'instruction `syscall` — `ltrace` ne verra rien, mais `auditd` capturera l'événement parce qu'il se place au point de passage obligé : l'interface noyau.

### Installation et vérification

`auditd` est normalement déjà installé si vous avez suivi la section 26.2. Vérifions :

```bash
sudo apt install auditd audispd-plugins  
sudo systemctl enable --now auditd  
sudo systemctl status auditd  
```

Le fichier de log principal est `/var/log/audit/audit.log`. Les outils de requête sont `ausearch` (recherche dans les logs) et `aureport` (rapports synthétiques).

### Configurer des règles d'audit pour l'analyse de malware

Par défaut, `auditd` ne surveille presque rien — il faut lui indiquer explicitement quoi observer via des règles. Les règles sont chargées depuis `/etc/audit/rules.d/` ou ajoutées dynamiquement avec `auditctl`.

Pour une session d'analyse de malware, nous voulons surveiller les actions les plus révélatrices du comportement d'un programme hostile. Créons un fichier de règles dédié :

```bash
sudo tee /etc/audit/rules.d/malware-analysis.rules << 'EOF'
# =============================================================
# Règles auditd pour analyse de malware
# Activer AVANT l'exécution du sample, désactiver APRÈS
# =============================================================

# Supprimer les règles existantes pour partir d'une base propre
-D

# Taille du buffer (augmenter si des événements sont perdus)
-b 8192

# -----------------------------------------------------------
# 1. Exécution de programmes (execve)
#    Capture chaque lancement de binaire, y compris les
#    processus enfants et les chaînes d'exécution
# -----------------------------------------------------------
-a always,exit -F arch=b64 -S execve -k exec_monitor

# -----------------------------------------------------------
# 2. Ouverture et création de fichiers
#    Capture les accès fichier en écriture et en création
# -----------------------------------------------------------
-a always,exit -F arch=b64 -S open,openat -F dir=/home -k file_access
-a always,exit -F arch=b64 -S open,openat -F dir=/tmp -k file_access
-a always,exit -F arch=b64 -S open,openat -F dir=/etc -k file_access

# -----------------------------------------------------------
# 3. Connexions réseau (connect, bind, accept)
#    Capture les tentatives de communication réseau
# -----------------------------------------------------------
-a always,exit -F arch=b64 -S connect -k net_connect
-a always,exit -F arch=b64 -S bind -k net_bind
-a always,exit -F arch=b64 -S accept,accept4 -k net_accept

# -----------------------------------------------------------
# 4. Modification des permissions et attributs
#    Un malware qui s'installe modifie souvent les permissions
# -----------------------------------------------------------
-a always,exit -F arch=b64 -S chmod,fchmod,fchmodat -k perm_change
-a always,exit -F arch=b64 -S chown,fchown,fchownat -k owner_change

# -----------------------------------------------------------
# 5. Opérations sur les processus
#    Détection de fork, kill, ptrace (anti-debug ou injection)
# -----------------------------------------------------------
-a always,exit -F arch=b64 -S clone,fork,vfork -k proc_create
-a always,exit -F arch=b64 -S ptrace -k ptrace_use
-a always,exit -F arch=b64 -S kill,tkill,tgkill -k signal_send

# -----------------------------------------------------------
# 6. Suppression et renommage de fichiers
#    Ransomware : suppression des originaux après chiffrement
# -----------------------------------------------------------
-a always,exit -F arch=b64 -S unlink,unlinkat,rename,renameat -k file_delete

# -----------------------------------------------------------
# 7. Modifications de la configuration système
#    Persistance : crontab, services, scripts de démarrage
# -----------------------------------------------------------
-w /etc/crontab -p wa -k persist_cron
-w /etc/cron.d/ -p wa -k persist_cron
-w /var/spool/cron/ -p wa -k persist_cron
-w /etc/systemd/system/ -p wa -k persist_systemd
-w /etc/init.d/ -p wa -k persist_init
-w /home/sample-runner/.bashrc -p wa -k persist_shell
-w /home/sample-runner/.profile -p wa -k persist_shell

# Rendre les règles immuables (optionnel, empêche le malware
# de les désactiver — nécessite un reboot pour les modifier)
# -e 2
EOF
```

Chargez les règles :

```bash
sudo augenrules --load  
sudo auditctl -l    # Vérifier que les règles sont actives  
```

### Anatomie d'un événement audit

Un événement `auditd` est un enregistrement multi-ligne. Voici un exemple typique capturé lors de l'exécution d'un binaire :

```
type=SYSCALL msg=audit(1718450023.456:1284): arch=c000003e syscall=59  
success=yes exit=0 a0=55a3c2f1b0 a1=55a3c2f1e0 a2=55a3c2f210 a3=0  
items=2 ppid=1432 pid=1587 uid=1001 gid=1001 euid=1001  
comm="ransomware_sam" exe="/home/sample-runner/malware-samples/ransomware_sample"  
key="exec_monitor"  
type=EXECVE msg=audit(1718450023.456:1284): argc=1  
a0="/home/sample-runner/malware-samples/ransomware_sample"  
type=PATH msg=audit(1718450023.456:1284): item=0  
name="/home/sample-runner/malware-samples/ransomware_sample"  
inode=262278 dev=fd:01 mode=0100755  
```

Les champs clés à lire sont `syscall` (le numéro de l'appel système — 59 correspond à `execve`), `exe` (le binaire qui a fait l'appel), `pid`/`ppid` (identifiant du processus et de son parent), `uid` (utilisateur), et `key` (la catégorie que nous avons définie dans nos règles). Le champ `key` est particulièrement utile pour filtrer les résultats.

### Requêter les logs avec `ausearch` et `aureport`

Après une session d'analyse, les logs bruts dans `/var/log/audit/audit.log` sont volumineux. `ausearch` permet de filtrer efficacement :

```bash
# Tous les événements liés à l'exécution de programmes
sudo ausearch -k exec_monitor --interpret

# Toutes les connexions réseau tentées
sudo ausearch -k net_connect --interpret

# Tous les fichiers supprimés ou renommés
sudo ausearch -k file_delete --interpret

# Événements générés par un PID spécifique
sudo ausearch -p 1587 --interpret

# Événements depuis une date/heure précise
sudo ausearch -k exec_monitor --start "06/15/2025" "14:30:00" --interpret
```

L'option `--interpret` traduit les valeurs numériques en noms lisibles (numéros de syscall en noms, UID en noms d'utilisateur, etc.).

Pour une vue synthétique, `aureport` génère des rapports par catégorie :

```bash
# Résumé des exécutions de programmes
sudo aureport -x --summary

# Résumé des accès fichiers
sudo aureport -f --summary

# Résumé des événements réseau
sudo aureport --comm --summary

# Chronologie complète
sudo aureport -ts today
```

### Exporter les logs pour analyse hors VM

Avant de restaurer le snapshot, transférez les logs vers l'hôte :

```bash
# Depuis l'hôte
scp analyst@10.66.66.100:/var/log/audit/audit.log ./analyses/session-xxx/
```

---

## `inotifywait` — Surveillance du système de fichiers en temps réel

### Qu'est-ce que `inotifywait` ?

`inotifywait` est un utilitaire du paquet `inotify-tools` qui exploite le mécanisme `inotify` du noyau Linux pour surveiller en temps réel les événements sur le système de fichiers. Contrairement à `auditd` qui capture tous les appels système liés aux fichiers et produit des logs détaillés mais verbeux, `inotifywait` offre une vue ciblée et immédiate : « tel fichier vient d'être créé », « tel répertoire vient d'être modifié ».

Son intérêt principal dans notre contexte est la **détection en temps réel des modifications de fichiers pendant l'exécution du sample**. Pour un ransomware, par exemple, vous verrez défiler en direct la liste des fichiers chiffrés dans l'ordre exact où le malware les traite.

### Installation

```bash
sudo apt install inotify-tools
```

### Utilisation pour l'analyse de malware

La commande suivante surveille récursivement les répertoires les plus intéressants et produit un log horodaté :

```bash
inotifywait -m -r \
  --timefmt '%Y-%m-%d %H:%M:%S' \
  --format '%T %w%f %e' \
  -e create,delete,modify,move,attrib \
  /home/sample-runner/ /tmp/ /etc/ /var/ \
  | tee ~/captures/inotify.log
```

Détail des options :

- `-m` (monitor) — mode continu. Sans cette option, `inotifywait` s'arrête après le premier événement.  
- `-r` — surveillance récursive de tous les sous-répertoires.  
- `--timefmt` et `--format` — format de sortie horodaté et lisible.  
- `-e` — types d'événements à surveiller : création, suppression, modification, déplacement, changement d'attributs.  
- `| tee` — affiche la sortie en temps réel tout en l'enregistrant dans un fichier.

### Exemple de sortie

Voici ce que produit `inotifywait` quand un ransomware chiffre des fichiers dans `/tmp/test/` :

```
2025-06-15 14:32:01 /tmp/test/document.txt MODIFY
2025-06-15 14:32:01 /tmp/test/document.txt.enc CREATE
2025-06-15 14:32:01 /tmp/test/document.txt DELETE
2025-06-15 14:32:01 /tmp/test/photo.jpg MODIFY
2025-06-15 14:32:01 /tmp/test/photo.jpg.enc CREATE
2025-06-15 14:32:01 /tmp/test/photo.jpg DELETE
2025-06-15 14:32:02 /tmp/test/RANSOM_NOTE.txt CREATE
```

La séquence est parlante : pour chaque fichier, le malware le lit (`MODIFY` car il ouvre le fichier), crée la version chiffrée (`.enc`), supprime l'original, puis dépose une note de rançon.

### Limites d'`inotifywait`

`inotifywait` ne voit que les événements sur les répertoires qu'on lui a demandé de surveiller. Si le sample écrit dans un répertoire inattendu (par exemple `/dev/shm` ou un répertoire qu'il crée lui-même à la racine), `inotifywait` ne le capturera pas à moins que ce chemin soit dans la liste. C'est pourquoi `inotifywait` complète `auditd` mais ne le remplace pas : `auditd` voit tous les appels `open`/`write` quel que soit le chemin, tandis qu'`inotifywait` offre une vision plus lisible mais limitée aux chemins spécifiés.

> 💡 **Astuce** — Pour une couverture maximale, vous pouvez surveiller `/` récursivement (`inotifywait -m -r /`), mais préparez-vous à un volume de bruit considérable : chaque écriture de log, chaque accès système générera un événement. Filtrer a posteriori avec `grep` est alors indispensable.

---

## `tcpdump` — Capture réseau sur le bridge isolé

### Rôle dans le lab

`tcpdump` est l'outil de capture réseau en ligne de commande le plus universel. Dans notre lab, il a un rôle précis : capturer l'intégralité du trafic réseau émis par la VM sur le bridge `br-malware` et l'enregistrer dans un fichier `.pcap` pour analyse ultérieure avec Wireshark.

Même si notre réseau est isolé et que les paquets ne mènent nulle part, leur contenu est une mine d'informations. Les tentatives de résolution DNS révèlent les noms de domaine que le malware tente de contacter. Les paquets TCP SYN montrent les adresses IP et les ports des serveurs C2. Le contenu des requêtes HTTP (ou de protocoles customs) révèle le format du protocole de communication. Tout cela sans que le malware n'ait jamais atteint sa destination.

### Deux points de capture possibles

La capture peut se faire à deux endroits, chacun avec ses avantages :

**Sur l'hôte, sur l'interface du bridge** — c'est la méthode recommandée. `tcpdump` tourne sur la machine hôte et écoute l'interface `br-malware`. Le sample ne peut pas détecter la capture ni la perturber, puisque `tcpdump` s'exécute en dehors de la VM.

```bash
# Sur l'hôte — méthode recommandée
sudo tcpdump -i br-malware -w ./analyses/session-xxx/capture.pcap -v
```

**Dans la VM, sur l'interface réseau de la VM** — utile si vous voulez observer le trafic en temps réel depuis la VM, mais le sample pourrait théoriquement détecter le processus `tcpdump` ou tenter de le tuer.

```bash
# Dans la VM — méthode alternative
sudo tcpdump -i enp1s0 -w ~/captures/capture.pcap -v
```

### Options de capture recommandées

```bash
sudo tcpdump -i br-malware \
  -w ./captures/capture_$(date +%Y%m%d_%H%M%S).pcap \
  -s 0 \
  -v \
  --print
```

- `-i br-malware` — interface de capture (le bridge isolé).  
- `-w` — écriture dans un fichier `.pcap` avec horodatage dans le nom.  
- `-s 0` — capturer les paquets entiers (pas de troncature). Indispensable pour analyser le contenu des payloads.  
- `-v` — sortie verbeuse sur la console.  
- `--print` — affiche les paquets en temps réel en plus de les écrire dans le fichier. Utile pour observer l'activité pendant l'exécution.

### Lire une capture rapidement en ligne de commande

Après la session, vous pouvez pré-filtrer la capture avant de l'ouvrir dans Wireshark :

```bash
# Nombre total de paquets capturés
tcpdump -r capture.pcap | wc -l

# Requêtes DNS uniquement (révèle les domaines contactés)
tcpdump -r capture.pcap -n port 53

# Connexions TCP SYN (tentatives de connexion sortante)
tcpdump -r capture.pcap 'tcp[tcpflags] & tcp-syn != 0'

# Trafic HTTP (si le C2 utilise HTTP en clair)
tcpdump -r capture.pcap -A port 80

# Résumé des conversations (IP source → IP destination)
tcpdump -r capture.pcap -n -q | awk '{print $3, $4, $5}' | sort | uniq -c | sort -rn
```

Pour une analyse approfondie, transférez le `.pcap` sur l'hôte et ouvrez-le dans Wireshark. Le chapitre 23 (Reverse d'un binaire réseau) détaille la lecture des captures Wireshark en profondeur.

### Pourquoi capturer sur l'hôte plutôt que dans la VM

Trois raisons justifient la préférence pour la capture côté hôte :

Premièrement, la **sécurité**. Si le sample est un malware qui tente de désactiver les outils de monitoring, il peut tuer le processus `tcpdump` dans la VM. Sur l'hôte, il n'a aucun accès au processus de capture.

Deuxièmement, la **fiabilité**. Si le malware corrompt le système de fichiers de la VM, le fichier `.pcap` stocké dans la VM pourrait être endommagé ou perdu. Sur l'hôte, il est à l'abri.

Troisièmement, la **praticité**. Le fichier `.pcap` est directement accessible sur l'hôte pour analyse avec Wireshark, sans avoir besoin de le transférer depuis la VM avant le rollback du snapshot.

---

## `sysdig` — Visibilité système unifiée

### Qu'est-ce que `sysdig` ?

`sysdig` est un outil de visibilité système qui capture les événements noyau (appels système, activité réseau, opérations fichier, gestion de processus) dans un flux unique et requêtable. On le décrit souvent comme la combinaison de `strace`, `tcpdump` et `lsof` dans un seul outil, avec un langage de filtrage puissant inspiré de celui de Wireshark.

Son avantage principal dans le contexte de l'analyse de malware est la **corrélation**. Là où `auditd` capture les syscalls, `inotifywait` les événements fichiers et `tcpdump` les paquets réseau dans des flux séparés, `sysdig` les réunit dans une chronologie unique. Vous pouvez voir, dans le même flux, le sample ouvrir un fichier, le lire, se connecter à un socket, et écrire des données — le tout avec des timestamps cohérents.

### Installation

`sysdig` nécessite un module noyau ou un pilote eBPF. Sur Debian/Ubuntu :

```bash
# Installation depuis les dépôts officiels de sysdig
curl -s https://s3.amazonaws.com/download.draios.com/stable/install-sysdig | sudo bash
```

Vérifiez l'installation :

```bash
sudo sysdig --version
```

> ⚠️ `sysdig` nécessite les privilèges root pour accéder aux événements noyau. Dans la VM d'analyse, c'est acceptable — l'utilisateur `analyst` utilisera `sudo`.

### Capture et filtrage

#### Mode temps réel

```bash
# Tous les événements système (très verbeux)
sudo sysdig

# Filtrer par nom de processus
sudo sysdig proc.name=ransomware_sample

# Filtrer par utilisateur (le sample tourne sous sample-runner)
sudo sysdig user.name=sample-runner

# Activité fichier uniquement
sudo sysdig "evt.type in (open, openat, read, write, close, unlink, rename)"

# Activité réseau uniquement
sudo sysdig "evt.type in (connect, accept, send, sendto, recv, recvfrom)"

# Combinaison : activité fichier ET réseau du sample
sudo sysdig "user.name=sample-runner and evt.type in (open, openat, write, connect, send)"
```

#### Mode capture (enregistrement pour analyse différée)

Comme `tcpdump`, `sysdig` peut écrire sa capture dans un fichier pour analyse ultérieure :

```bash
# Capturer tous les événements dans un fichier
sudo sysdig -w ~/captures/sysdig_session.scap

# Capturer uniquement les événements de l'utilisateur sample-runner
sudo sysdig -w ~/captures/sysdig_session.scap "user.name=sample-runner"
```

Pour relire la capture après la session :

```bash
# Relire la capture avec les mêmes filtres que sysdig live
sudo sysdig -r ~/captures/sysdig_session.scap "evt.type=connect"

# Relire et formater avec un chisel (voir ci-dessous)
sudo sysdig -r ~/captures/sysdig_session.scap -c topfiles_bytes
```

#### Les chisels : scripts d'analyse prédéfinis

`sysdig` fournit des « chisels » — des scripts d'analyse prédéfinis qui extraient des métriques ciblées. Plusieurs sont directement utiles pour l'analyse de malware :

```bash
# Lister les chisels disponibles
sudo sysdig -cl

# Top des fichiers par volume d'écriture
# (révèle les fichiers que le malware modifie le plus)
sudo sysdig -c topfiles_bytes "user.name=sample-runner"

# Top des connexions réseau
sudo sysdig -c topconns

# Chronologie des processus lancés
# (détecte les fork, exec, chaînes de lancement)
sudo sysdig -c spy_users "user.name=sample-runner"

# Suivre les entrées/sorties d'un processus (équivalent strace enrichi)
sudo sysdig -c echo_fds "proc.name=ransomware_sample"

# Lister tous les fichiers ouverts par le sample
sudo sysdig -c list_login_shells
```

### `sysdig` vs les autres outils : quand l'utiliser

`sysdig` ne remplace pas les trois autres outils — il les complète. Voici comment les articuler :

- **`tcpdump`** reste l'outil de référence pour la capture réseau brute (`.pcap`). `sysdig` capture l'activité réseau au niveau des syscalls, mais ne produit pas de `.pcap` directement exploitable par Wireshark. Pour l'analyse de protocole, `tcpdump` + Wireshark reste imbattable.  
- **`auditd`** est préférable quand vous avez besoin de **logs persistants et certifiables**. Dans un contexte d'incident response professionnel, les logs `auditd` ont une valeur probante que `sysdig` n'a pas (format standardisé, intégrité vérifiable).  
- **`inotifywait`** offre une vue immédiate et lisible des modifications de fichiers, idéale pour observer en temps réel ce que fait un ransomware. Sa sortie est plus facile à lire qu'un flux `sysdig` filtré.  
- **`sysdig`** excelle quand vous avez besoin de **corréler** plusieurs types d'activité dans une même chronologie, ou quand vous voulez faire une analyse exploratoire rapide sans savoir encore exactement quoi chercher.

En pratique, dans les chapitres suivants, nous lancerons systématiquement `tcpdump` (côté hôte) et `inotifywait` + `auditd` (dans la VM) avant chaque exécution de sample. `sysdig` sera utilisé en complément pour des analyses ciblées quand les premières observations orientent vers des comportements spécifiques.

---

## Workflow de monitoring complet

Mettons bout à bout tous les outils pour former le workflow de monitoring standard d'une session d'analyse. Ce workflow est lancé **après** la préparation de la VM (snapshot restauré, réseau isolé, sample copié) et **avant** l'exécution du sample.

### Étape 1 — Ouvrir quatre terminaux

L'approche la plus lisible est de dédier un terminal à chaque outil. Si vous travaillez avec `tmux` ou `screen` (recommandé), créez quatre panneaux.

### Étape 2 — Lancer les captures

**Terminal 1 — `tcpdump` sur l'hôte :**

```bash
# SUR L'HÔTE (pas dans la VM)
ANALYSIS_DIR="./analyses/$(date +%Y%m%d-%H%M%S)"  
mkdir -p "$ANALYSIS_DIR"  
sudo tcpdump -i br-malware -s 0 -w "$ANALYSIS_DIR/capture.pcap" --print  
```

**Terminal 2 — `auditd` dans la VM :**

```bash
# DANS LA VM (ssh analyst@10.66.66.100)
sudo augenrules --load  
sudo ausearch -k exec_monitor --start now --interpret -i  
# (reste en attente des événements)
```

**Terminal 3 — `inotifywait` dans la VM :**

```bash
# DANS LA VM
inotifywait -m -r \
  --timefmt '%Y-%m-%d %H:%M:%S' \
  --format '%T %w%f %e' \
  -e create,delete,modify,move,attrib \
  /home/sample-runner/ /tmp/ /etc/ /var/ \
  | tee ~/captures/inotify.log
```

**Terminal 4 — Terminal d'exécution dans la VM :**

```bash
# DANS LA VM — ce terminal servira à lancer le sample
cd ~/malware-samples/  
sha256sum ransomware_sample    # Vérifier le hash avant exécution  
```

### Étape 3 — Exécuter le sample

Dans le terminal 4 :

```bash
sudo -u sample-runner ./ransomware_sample
```

Les trois autres terminaux affichent en temps réel l'activité observée. Prenez des notes au fur et à mesure : « à T+2s, le sample ouvre /tmp/test/document.txt », « à T+3s, tentative de connexion vers 185.x.x.x:443 », etc.

### Étape 4 — Arrêter les captures et collecter

Après l'exécution (ou quand vous avez observé suffisamment), arrêtez les captures (`Ctrl+C` sur chaque terminal), puis collectez les artefacts :

```bash
# DANS LA VM — copier les logs auditd
sudo cp /var/log/audit/audit.log ~/captures/

# SUR L'HÔTE — récupérer les artefacts de la VM
scp analyst@10.66.66.100:~/captures/* "$ANALYSIS_DIR/"
```

La structure d'artefacts résultante :

```
analyses/20250615-143200/
├── capture.pcap           ← Trafic réseau (tcpdump, capturé sur l'hôte)
├── audit.log              ← Événements noyau (auditd)
├── inotify.log            ← Modifications fichiers (inotifywait)
└── notes.md               ← Vos observations en temps réel
```

---

## Écrire un script de lancement du monitoring

Pour ne pas oublier une étape ni faire une erreur de configuration dans le feu de l'action, automatisez le lancement du monitoring dans la VM :

```bash
#!/bin/bash
# start_monitoring.sh — À exécuter dans la VM avant le sample
# Usage : ./start_monitoring.sh

set -euo pipefail

CAPTURE_DIR=~/captures/$(date +%Y%m%d-%H%M%S)  
mkdir -p "$CAPTURE_DIR"  

echo "[*] Chargement des règles auditd..."  
sudo augenrules --load  

echo "[*] Purge des logs auditd précédents..."  
sudo truncate -s 0 /var/log/audit/audit.log  

echo "[*] Démarrage de inotifywait en arrière-plan..."  
inotifywait -m -r \  
  --timefmt '%Y-%m-%d %H:%M:%S' \
  --format '%T %w%f %e' \
  -e create,delete,modify,move,attrib \
  /home/sample-runner/ /tmp/ /etc/ /var/ \
  > "$CAPTURE_DIR/inotify.log" 2>&1 &
INOTIFY_PID=$!  
echo "    inotifywait PID: $INOTIFY_PID"  

echo "[*] Démarrage de sysdig en arrière-plan..."  
sudo sysdig -w "$CAPTURE_DIR/sysdig.scap" "user.name=sample-runner" &  
SYSDIG_PID=$!  
echo "    sysdig PID: $SYSDIG_PID"  

echo ""  
echo "[+] Monitoring actif. Répertoire de capture : $CAPTURE_DIR"  
echo "    Pour arrêter : kill $INOTIFY_PID && sudo kill $SYSDIG_PID"  
echo "    N'oubliez pas : tcpdump doit tourner sur l'HÔTE (pas ici)"  
echo ""  
echo "    Exécutez le sample quand vous êtes prêt :"  
echo "    sudo -u sample-runner /home/sample-runner/malware-samples/<sample>"  
```

---

## Considérations de performance

Les outils de monitoring consomment des ressources. Quand ils tournent tous simultanément dans une VM à 4 Go de RAM et 2 vCPUs, l'impact n'est pas négligeable.

`auditd` est le plus léger — son composant noyau ajoute une surcharge minime par syscall, et le démon en espace utilisateur écrit séquentiellement dans un fichier. `inotifywait` est également très léger tant qu'il ne surveille pas des dizaines de milliers de fichiers récursivement. `tcpdump` (côté hôte) n'impacte pas du tout les performances de la VM. `sysdig` est le plus gourmand : son pilote noyau capture tous les événements et les transfère en espace utilisateur. Sur un système actif, cela peut représenter un volume d'événements considérable.

Si la VM devient trop lente pour que le sample s'exécute normalement — ce qui pourrait modifier son comportement et fausser l'analyse — allégez la configuration. Commencez par retirer `sysdig` (les informations essentielles sont couvertes par les trois autres outils) et réduisez le périmètre d'`inotifywait` aux seuls répertoires que vous suspectez être ciblés par le sample.

---

> 📌 **À retenir** — Le monitoring est la mémoire de l'analyse. Sans lui, exécuter un sample dans le lab revient à faire une expérience scientifique sans prendre de notes : vous observez sur le moment, mais vous perdez les détails. `auditd` capture le « quoi » (quels syscalls), `inotifywait` montre le « où » (quels fichiers), `tcpdump` enregistre le « avec qui » (quel trafic réseau), et `sysdig` relie le tout dans un « quand » cohérent. Lancez-les **avant** le sample, collectez les artefacts **avant** le rollback, et vous aurez une base solide pour chaque analyse des chapitres suivants.

⏭️ [Captures réseau avec un bridge dédié](/26-lab-securise/04-captures-reseau-bridge.md)
