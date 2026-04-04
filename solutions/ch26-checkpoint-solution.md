🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 26
# Déployer le lab et vérifier l'isolation réseau

> ⚠️ **Spoiler** — Ce fichier contient le corrigé complet du checkpoint du chapitre 26. Essayez de réaliser le déploiement par vous-même avant de consulter cette solution.

---

## Vue d'ensemble

Ce corrigé présente l'intégralité des commandes à exécuter, dans l'ordre, pour déployer le lab depuis zéro et valider chaque point de la grille du checkpoint. Chaque commande est accompagnée de la sortie attendue, afin que vous puissiez comparer avec vos propres résultats et identifier précisément l'origine d'un éventuel écart.

Le corrigé est découpé en deux parties : le **déploiement** (si vous n'avez pas encore construit le lab) et la **vérification** (si le lab est déjà en place et que vous souhaitez uniquement valider le checkpoint).

---

## Partie A — Déploiement complet du lab

### A.1 — Prérequis sur l'hôte

```bash
# Vérifier le support de la virtualisation matérielle
grep -Ec '(vmx|svm)' /proc/cpuinfo
# Attendu : un nombre > 0 (nombre de cœurs avec support VT-x/AMD-V)

# Installer les paquets nécessaires
sudo apt update  
sudo apt install -y qemu-system-x86 qemu-utils libvirt-daemon-system \  
                    libvirt-clients virtinst virt-manager bridge-utils \
                    cpu-checker wireshark tshark

# Vérifier KVM
sudo modprobe kvm  
sudo modprobe kvm_intel  # ou kvm_amd selon votre CPU  
kvm-ok  
# Attendu :
# INFO: /dev/kvm exists
# KVM acceleration can be used

# Ajouter l'utilisateur au groupe libvirt
sudo usermod -aG libvirt $(whoami)  
newgrp libvirt  

# Démarrer libvirtd
sudo systemctl enable --now libvirtd  
systemctl is-active libvirtd  
# Attendu : active
```

### A.2 — Création de la VM

```bash
# Créer l'image disque
mkdir -p ~/malware-lab  
cd ~/malware-lab  
qemu-img create -f qcow2 malware-lab.qcow2 30G  
# Attendu :
# Formatting 'malware-lab.qcow2', fmt=qcow2 size=32212254720 ...

# Vérifier la taille réelle sur le disque (allocation dynamique)
ls -lh malware-lab.qcow2
# Attendu : quelques centaines de Ko seulement

# Télécharger l'ISO Debian (adapter la version si nécessaire)
wget https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-12.7.0-amd64-netinst.iso

# Lancer l'installation
virt-install \
  --name malware-lab \
  --ram 4096 \
  --vcpus 2 \
  --disk path=./malware-lab.qcow2,format=qcow2 \
  --cdrom debian-12.7.0-amd64-netinst.iso \
  --os-variant debian12 \
  --network network=default \
  --graphics spice \
  --video virtio \
  --boot uefi \
  --noautoconsole
# Attendu : "Domain installation still in progress..."

# Ouvrir la console graphique pour terminer l'installation
virt-manager &
```

**Choix pendant l'installation Debian :**

- Langue : selon votre préférence (n'affecte pas le lab).  
- Partitionnement : disque entier, tout dans une seule partition.  
- Logiciels à installer : décocher tout **sauf** « utilitaires standard du système » et « serveur SSH ».  
- Créer un utilisateur : `analyst` (mot de passe de votre choix).  
- Mot de passe root : définir un mot de passe (nécessaire pour `sudo` initial).

### A.3 — Configuration post-installation dans la VM

Une fois Debian installé et la VM redémarrée, connectez-vous en SSH depuis l'hôte (le réseau NAT par défaut le permet via l'IP attribuée par le DHCP de `virbr0`) :

```bash
# Trouver l'IP de la VM
virsh domifaddr malware-lab
# Attendu : une adresse en 192.168.122.x

# Se connecter
ssh analyst@192.168.122.xxx
```

Dans la VM :

```bash
# Passer en root pour la configuration initiale
su -

# Ajouter analyst au groupe sudo
usermod -aG sudo analyst  
exit  

# Reconnecter en tant qu'analyst avec sudo disponible
exit  
ssh analyst@192.168.122.xxx  

# Installer les outils RE et de monitoring
sudo apt update && sudo apt upgrade -y  
sudo apt install -y build-essential gdb strace ltrace \  
                    python3 python3-pip python3-venv \
                    tcpdump inotify-tools auditd \
                    wget curl git unzip file binutils \
                    net-tools nmap xxd dnsutils

# Installer sysdig
curl -s https://s3.amazonaws.com/download.draios.com/stable/install-sysdig | sudo bash

# Installer Frida et pwntools dans un venv
python3 -m venv ~/re-venv  
source ~/re-venv/bin/activate  
pip install frida-tools pwntools  
deactivate  

# Installer GEF (extension GDB)
bash -c "$(curl -fsSL https://gef.blah.cat/sh)"

# Créer l'utilisateur sample-runner
sudo useradd -m -s /bin/bash sample-runner  
sudo chmod 700 /home/sample-runner  

# Créer les répertoires de travail
mkdir -p ~/malware-samples ~/captures  
chmod 700 ~/malware-samples  
```

### A.4 — Durcissement de la VM

```bash
# Vérifier que spice-vdagent n'est PAS installé
dpkg -l | grep spice-vdagent
# Attendu : aucun résultat (non installé sur une Debian minimale)
# Si présent :
# sudo apt remove --purge spice-vdagent

# Vérifier qu'aucun dossier partagé n'est monté
mount | grep -iE '(vboxsf|vmhgfs|9p|shared)'
# Attendu : aucun résultat
```

Sur l'**hôte**, éditer la configuration de la VM pour retirer les périphériques inutiles :

```bash
virsh shutdown malware-lab  
virsh edit malware-lab  
```

Dans l'éditeur XML, vérifier et supprimer si présents :

- Les blocs `<filesystem>` (dossiers partagés).  
- Les blocs `<redirdev>` (redirection USB).  
- Les blocs `<channel>` liés à `spicevmc` autres que le canal principal.

Sauvegarder et quitter l'éditeur.

### A.5 — Création des règles auditd

Dans la VM :

```bash
sudo tee /etc/audit/rules.d/malware-analysis.rules << 'EOF'
-D
-b 8192

# Exécution de programmes
-a always,exit -F arch=b64 -S execve -k exec_monitor

# Ouverture/création de fichiers
-a always,exit -F arch=b64 -S open,openat -F dir=/home -k file_access
-a always,exit -F arch=b64 -S open,openat -F dir=/tmp -k file_access
-a always,exit -F arch=b64 -S open,openat -F dir=/etc -k file_access

# Connexions réseau
-a always,exit -F arch=b64 -S connect -k net_connect
-a always,exit -F arch=b64 -S bind -k net_bind
-a always,exit -F arch=b64 -S accept,accept4 -k net_accept

# Permissions
-a always,exit -F arch=b64 -S chmod,fchmod,fchmodat -k perm_change
-a always,exit -F arch=b64 -S chown,fchown,fchownat -k owner_change

# Processus
-a always,exit -F arch=b64 -S clone,fork,vfork -k proc_create
-a always,exit -F arch=b64 -S ptrace -k ptrace_use
-a always,exit -F arch=b64 -S kill,tkill,tgkill -k signal_send

# Suppression/renommage de fichiers
-a always,exit -F arch=b64 -S unlink,unlinkat,rename,renameat -k file_delete

# Persistance
-w /etc/crontab -p wa -k persist_cron
-w /etc/cron.d/ -p wa -k persist_cron
-w /var/spool/cron/ -p wa -k persist_cron
-w /etc/systemd/system/ -p wa -k persist_systemd
-w /etc/init.d/ -p wa -k persist_init
-w /home/sample-runner/.bashrc -p wa -k persist_shell
-w /home/sample-runner/.profile -p wa -k persist_shell
EOF

sudo augenrules --load  
sudo auditctl -l  
# Attendu : la liste complète des règles s'affiche (une vingtaine de lignes)
```

### A.6 — Snapshot clean-base

```bash
# Sur l'hôte — arrêter proprement la VM
virsh shutdown malware-lab

# Attendre l'arrêt complet
virsh list --all | grep malware-lab
# Attendu : "shut off"

# Prendre le snapshot de référence
virsh snapshot-create-as malware-lab \
  --name "clean-base" \
  --description "Debian 12 + outils RE + monitoring + durci. Aucun sample." \
  --atomic

# Vérifier
virsh snapshot-list malware-lab
# Attendu :
#  Name          Creation Time               State
# ---------------------------------------------------
#  clean-base    2025-06-15 14:00:00 +0200   shutoff
```

### A.7 — Réseau isolé et règles iptables

Sur l'**hôte** :

```bash
# Créer le réseau isolé
cat > /tmp/isolated-malware.xml << 'EOF'
<network>
  <name>isolated-malware</name>
  <bridge name="br-malware" stp="on" delay="0"/>
  <ip address="10.66.66.1" netmask="255.255.255.0">
    <dhcp>
      <range start="10.66.66.100" end="10.66.66.200"/>
    </dhcp>
  </ip>
</network>
EOF

virsh net-define /tmp/isolated-malware.xml  
virsh net-start isolated-malware  
virsh net-autostart isolated-malware  

# Vérifier
virsh net-list --all | grep isolated-malware
# Attendu : isolated-malware   active   yes   ...

ip addr show br-malware
# Attendu : inet 10.66.66.1/24 ...

# Ajouter les règles iptables de blocage
# Identifier l'interface réseau externe
DEFAULT_IF=$(ip route show default | awk '{print $5}' | head -1)  
echo "Interface externe détectée : $DEFAULT_IF"  

sudo iptables -I FORWARD -i br-malware -o "$DEFAULT_IF" -j DROP  
sudo iptables -I FORWARD -i "$DEFAULT_IF" -o br-malware -j DROP  
sudo iptables -I FORWARD -i br-malware ! -o br-malware -j DROP  

# Persister les règles
sudo apt install -y iptables-persistent  
sudo netfilter-persistent save  

# Vérifier
sudo iptables -L FORWARD -v -n | grep br-malware
# Attendu : 2-3 règles DROP mentionnant br-malware
```

### A.8 — Scripts d'automatisation

Sur l'**hôte**, créez les scripts dans le répertoire du lab :

```bash
cd ~/malware-lab  
mkdir -p scripts analyses  
```

**`scripts/prepare_analysis.sh`** :

```bash
cat > scripts/prepare_analysis.sh << 'SCRIPT'
#!/bin/bash
set -euo pipefail

SAMPLE_NAME="${1:?Usage: $0 <nom-du-sample>}"  
VM_NAME="malware-lab"  
TIMESTAMP=$(date +%Y%m%d-%H%M%S)  
SNAPSHOT_NAME="pre-exec-${SAMPLE_NAME}-${TIMESTAMP}"  

echo "[*] Restauration du snapshot clean-base..."  
virsh snapshot-revert "$VM_NAME" --snapshotname "clean-base"  

echo "[*] Démarrage de la VM..."  
virsh start "$VM_NAME"  

echo "[*] Attente du boot (30s)..."  
sleep 30  

echo "[*] Bascule vers le réseau isolé..."  
virsh detach-interface "$VM_NAME" network --current 2>/dev/null || true  
virsh attach-interface "$VM_NAME" network isolated-malware --current  

echo "[*] Attente DHCP (10s)..."  
sleep 10  

echo "[*] Prise du snapshot pré-exécution : $SNAPSHOT_NAME"  
virsh snapshot-create-as "$VM_NAME" \  
  --name "$SNAPSHOT_NAME" \
  --description "Avant exécution de $SAMPLE_NAME"

echo ""  
echo "[+] VM prête pour l'analyse de : $SAMPLE_NAME"  
echo "    Snapshot : $SNAPSHOT_NAME"  
echo "    Réseau   : isolated-malware (10.66.66.0/24)"  
SCRIPT  
chmod +x scripts/prepare_analysis.sh  
```

**`scripts/cleanup_analysis.sh`** :

```bash
cat > scripts/cleanup_analysis.sh << 'SCRIPT'
#!/bin/bash
set -euo pipefail

SAMPLE_NAME="${1:?Usage: $0 <nom-du-sample>}"  
VM_NAME="malware-lab"  
TIMESTAMP=$(date +%Y%m%d-%H%M%S)  
OUTPUT_DIR="./analyses/${SAMPLE_NAME}-${TIMESTAMP}"  
VM_IP="10.66.66.100"  

mkdir -p "$OUTPUT_DIR"

echo "[*] Collecte des artefacts depuis la VM..."  
scp "analyst@${VM_IP}:~/captures/*" "$OUTPUT_DIR/" 2>/dev/null || echo "    Pas de captures"  
scp "analyst@${VM_IP}:/var/log/audit/audit.log" "$OUTPUT_DIR/" 2>/dev/null || echo "    Pas de log audit"  

echo "[*] Artefacts sauvegardés dans : $OUTPUT_DIR"  
ls -la "$OUTPUT_DIR/"  

echo "[*] Restauration du snapshot clean-base..."  
virsh snapshot-revert "$VM_NAME" --snapshotname "clean-base"  

echo "[+] Nettoyage terminé."  
SCRIPT  
chmod +x scripts/cleanup_analysis.sh  
```

**`scripts/start_monitoring.sh`** (à copier dans la VM) :

```bash
cat > scripts/start_monitoring.sh << 'VMSCRIPT'
#!/bin/bash
set -euo pipefail

echo "=== Vérification isolation réseau ==="  
if ip route | grep -q "^default"; then  
    echo "[FAIL] Route par défaut détectée — NE PAS EXÉCUTER"
    exit 1
fi  
if ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1; then  
    echo "[FAIL] Internet accessible — NE PAS EXÉCUTER"
    exit 1
fi  
echo "[OK] Isolation réseau confirmée"  
echo ""  

CAPTURE_DIR=~/captures/$(date +%Y%m%d-%H%M%S)  
mkdir -p "$CAPTURE_DIR"  

echo "[*] Chargement des règles auditd..."  
sudo augenrules --load  
sudo truncate -s 0 /var/log/audit/audit.log  

echo "[*] Démarrage de inotifywait..."  
inotifywait -m -r \  
  --timefmt '%Y-%m-%d %H:%M:%S' \
  --format '%T %w%f %e' \
  -e create,delete,modify,move,attrib \
  /home/sample-runner/ /tmp/ /etc/ /var/ \
  > "$CAPTURE_DIR/inotify.log" 2>&1 &
INOTIFY_PID=$!

echo "[*] Démarrage de sysdig..."  
sudo sysdig -w "$CAPTURE_DIR/sysdig.scap" "user.name=sample-runner" &  
SYSDIG_PID=$!  

echo ""  
echo "[+] Monitoring actif. Captures dans : $CAPTURE_DIR"  
echo "    inotifywait PID : $INOTIFY_PID"  
echo "    sysdig PID      : $SYSDIG_PID"  
echo ""  
echo "    Pour arrêter : kill $INOTIFY_PID && sudo kill $SYSDIG_PID"  
echo "    N'oubliez pas : tcpdump doit tourner sur l'HÔTE"  
echo ""  
echo "    Exécutez le sample :"  
echo "    sudo -u sample-runner ~/malware-samples/<sample>"  
VMSCRIPT  
chmod +x scripts/start_monitoring.sh  
```

---

## Partie B — Vérification complète (protocole du checkpoint)

Cette partie exécute exactement le protocole de vérification décrit dans le checkpoint. Si vous avez suivi la partie A, chaque commande doit produire le résultat attendu.

### B.1 — Infrastructure de virtualisation

```bash
kvm-ok
# ✅ Attendu :
# INFO: /dev/kvm exists
# KVM acceleration can be used

systemctl is-active libvirtd
# ✅ Attendu : active

virsh list --all | grep malware-lab
# ✅ Attendu :
#  -   malware-lab   shut off
# (ou "running" si la VM est démarrée)

virsh snapshot-list malware-lab | grep clean-base
# ✅ Attendu :
#  clean-base   2025-06-15T14:00:00+02:00   shutoff

virsh net-list --all | grep isolated-malware
# ✅ Attendu :
#  isolated-malware   active   yes   ...
```

**Résultat : 5/5 ✅**

### B.2 — Isolation réseau

```bash
# Démarrer et basculer sur le réseau isolé
virsh start malware-lab  
sleep 30  
virsh detach-interface malware-lab network --current 2>/dev/null || true  
virsh attach-interface malware-lab network isolated-malware --current  
sleep 10  

# Se connecter à la VM
ssh analyst@10.66.66.100
# (Si la connexion échoue, la VM n'a peut-être pas encore obtenu son IP.
#  Utilisez virsh console malware-lab et lancez sudo dhclient)
```

Dans la VM :

```bash
sudo dhclient -r && sudo dhclient

# Test 1 : adresse IP correcte
ip addr show | grep "10.66.66"
# ✅ Attendu : inet 10.66.66.100/24 (ou .101, .102... selon le DHCP)

# Test 2 : communication avec l'hôte
ping -c 3 10.66.66.1
# ✅ Attendu :
# 3 packets transmitted, 3 received, 0% packet loss

# Test 3 : Internet inaccessible (IP Google DNS)
ping -c 3 -W 2 8.8.8.8
# ✅ Attendu :
# 3 packets transmitted, 0 received, 100% packet loss

# Test 4 : Internet inaccessible (IP Cloudflare)
ping -c 3 -W 2 1.1.1.1
# ✅ Attendu :
# 3 packets transmitted, 0 received, 100% packet loss

# Test 5 : résolution DNS publique impossible
host example.com
# ✅ Attendu :
# ;; connection timed out; no servers could be reached
# (ou Host example.com not found si dnsmasq tourne sans résolution universelle)

# Test 6 : pas de route par défaut
ip route
# ✅ Attendu :
# 10.66.66.0/24 dev enp1s0 proto kernel scope link src 10.66.66.100
# (PAS de ligne "default via ...")

# Test 7 : téléchargement impossible
curl -m 5 http://example.com
# ✅ Attendu :
# curl: (28) Connection timed out
# ou curl: (6) Could not resolve host: example.com
```

Sur l'**hôte** :

```bash
sudo iptables -L FORWARD -v -n | grep br-malware
# ✅ Attendu : lignes contenant DROP et br-malware
# Exemple :
#     0     0 DROP   all  --  br-malware !br-malware  0.0.0.0/0   0.0.0.0/0
#     0     0 DROP   all  --  br-malware eth0         0.0.0.0/0   0.0.0.0/0
#     0     0 DROP   all  --  eth0       br-malware   0.0.0.0/0   0.0.0.0/0
```

**Résultat : 8/8 ✅**

### B.3 — Monitoring

Dans la VM :

```bash
# --- auditd ---
sudo systemctl is-active auditd
# ✅ Attendu : active

sudo augenrules --load  
sudo auditctl -l | head -5  
# ✅ Attendu : des règles s'affichent
# -a always,exit -F arch=b64 -S execve -F key=exec_monitor
# -a always,exit -F arch=b64 -S open,openat -F dir=/tmp -F key=file_access
# ...

touch /tmp/test_audit_verify  
sudo ausearch -k file_access -ts recent --interpret 2>/dev/null | grep test_audit_verify  
# ✅ Attendu : au moins une ligne mentionnant test_audit_verify
rm /tmp/test_audit_verify

# --- inotifywait ---
which inotifywait
# ✅ Attendu : /usr/bin/inotifywait

inotifywait -m -r /tmp/ --format '%T %w%f %e' --timefmt '%H:%M:%S' &  
INOTIFY_PID=$!  
sleep 1  
echo "test" > /tmp/test_inotify_verify  
# ✅ Attendu : une ou plusieurs lignes s'affichent immédiatement :
# 14:35:22 /tmp/test_inotify_verify CREATE
# 14:35:22 /tmp/test_inotify_verify MODIFY
kill $INOTIFY_PID 2>/dev/null  
rm /tmp/test_inotify_verify  

# --- sysdig ---
sudo sysdig --version
# ✅ Attendu : un numéro de version (ex: sysdig version 0.35.1)

sudo timeout 3 sysdig "evt.type=open and proc.name=bash" 2>/dev/null | head -3
# ✅ Attendu : quelques lignes d'événements ou rien (pas d'erreur)
```

Sur l'**hôte** :

```bash
# --- tcpdump ---
# Terminal 1 (hôte) : lancer tcpdump
sudo timeout 15 tcpdump -i br-malware -c 10 --print &  
TCPDUMP_PID=$!  

# Terminal 2 (VM) : générer du trafic
ssh analyst@10.66.66.100 'ping -c 5 10.66.66.1'

# ✅ Attendu dans la sortie tcpdump :
# 14:36:01.123456 IP 10.66.66.100 > 10.66.66.1: ICMP echo request ...
# 14:36:01.123789 IP 10.66.66.1 > 10.66.66.100: ICMP echo reply ...
```

**Résultat : 4/4 ✅**

### B.4 — Durcissement

Dans la VM :

```bash
# Pas de dossier partagé
mount | grep -iE '(vboxsf|vmhgfs|9p|shared)'
# ✅ Attendu : aucune sortie

# spice-vdagent absent
pgrep spice-vdagent
# ✅ Attendu : aucun PID (code retour 1)

# Utilisateur sample-runner
id sample-runner
# ✅ Attendu : uid=1001(sample-runner) gid=1001(sample-runner) groups=1001(sample-runner)
# (les numéros uid/gid peuvent varier)

# Permissions /home/sample-runner
ls -ld /home/sample-runner/
# ✅ Attendu : drwx------ 2 sample-runner sample-runner ... /home/sample-runner/

# Répertoire malware-samples
ls -ld ~/malware-samples/
# ✅ Attendu : drwx------ 2 analyst analyst ... /home/analyst/malware-samples/
```

**Résultat : 5/5 ✅**

### B.5 — Cycle snapshot complet

Sur l'**hôte** :

```bash
# Créer un snapshot de test
virsh snapshot-create-as malware-lab \
  --name "test-checkpoint-solution" \
  --description "Snapshot de test pour le corrigé du checkpoint ch26"
# ✅ Attendu : Domain snapshot test-checkpoint-solution created

# Créer un fichier marqueur dans la VM
ssh analyst@10.66.66.100 'echo "MARQUEUR_CHECKPOINT_CH26" > ~/marqueur.txt'

# Vérifier que le fichier existe
ssh analyst@10.66.66.100 'cat ~/marqueur.txt'
# ✅ Attendu : MARQUEUR_CHECKPOINT_CH26

# Restaurer le snapshot (la VM est arrêtée par le revert)
virsh snapshot-revert malware-lab --snapshotname "test-checkpoint-solution"

# Redémarrer la VM
virsh start malware-lab  
sleep 30  

# Rebasculer sur le réseau isolé pour pouvoir se reconnecter
virsh detach-interface malware-lab network --current 2>/dev/null || true  
virsh attach-interface malware-lab network isolated-malware --current  
sleep 10  

# Vérifier que le marqueur a DISPARU
ssh analyst@10.66.66.100 'cat ~/marqueur.txt 2>&1'
# ✅ Attendu :
# cat: /home/analyst/marqueur.txt: No such file or directory

# Nettoyer le snapshot de test
virsh snapshot-delete malware-lab --snapshotname "test-checkpoint-solution"
# ✅ Attendu : Domain snapshot test-checkpoint-solution deleted
```

**Résultat : 3/3 ✅**

---

## Grille de validation complétée

```
INFRASTRUCTURE
  ✅  kvm-ok confirme le support de la virtualisation
  ✅  La VM malware-lab existe et démarre
  ✅  Le snapshot clean-base existe et est restaurable
  ✅  Le réseau isolated-malware est actif

ISOLATION RÉSEAU
  ✅  La VM obtient une IP en 10.66.66.x
  ✅  La VM peut joindre l'hôte (10.66.66.1)
  ✅  ping 8.8.8.8 échoue depuis la VM
  ✅  ping 1.1.1.1 échoue depuis la VM
  ✅  host example.com échoue depuis la VM
  ✅  ip route ne montre aucune route par défaut
  ✅  iptables sur l'hôte montre les règles DROP sur br-malware

MONITORING
  ✅  auditd est actif, les règles se chargent, un événement test est capturé
  ✅  inotifywait détecte la création d'un fichier de test
  ✅  tcpdump sur l'hôte capture le trafic du bridge
  ✅  sysdig s'exécute sans erreur

DURCISSEMENT
  ✅  Aucun dossier partagé monté
  ✅  spice-vdagent absent ou inactif
  ✅  L'utilisateur sample-runner existe
  ✅  /home/sample-runner/ est en permissions 700
  ✅  ~/malware-samples/ existe

SNAPSHOTS
  ✅  Un snapshot de test peut être créé
  ✅  Un fichier créé après le snapshot disparaît après rollback
  ✅  Le snapshot de test est nettoyé

TOTAL : 22/22 ✅ — Checkpoint validé.
```

---

## Problèmes fréquents et solutions

### « `ping 8.8.8.8` réussit depuis la VM »

C'est le problème le plus courant et le plus critique. Causes possibles :

1. **La VM est encore sur le réseau `default`.** Vérifiez avec `virsh domiflist malware-lab`. Si la colonne « Source » indique `default`, basculez :
   ```bash
   virsh detach-interface malware-lab network --current
   virsh attach-interface malware-lab network isolated-malware --current
   ```

2. **Le forwarding IP est activé sur l'hôte.** Vérifiez avec `cat /proc/sys/net/ipv4/ip_forward`. Si la valeur est `1`, les règles iptables doivent bloquer le forwarding depuis `br-malware`. Revérifiez les règles iptables.

3. **Les règles iptables ne sont pas chargées.** Après un redémarrage de l'hôte, les règles peuvent disparaître si `iptables-persistent` n'a pas été installé correctement. Relancez `sudo netfilter-persistent reload`.

### « SSH vers 10.66.66.100 échoue »

1. **La VM n'a pas obtenu d'IP.** Connectez-vous via `virsh console malware-lab` et lancez `sudo dhclient`.

2. **L'IP est différente.** Le DHCP peut attribuer `.101`, `.102`, etc. Vérifiez depuis la console de la VM avec `ip addr show`.

3. **Le service SSH n'est pas installé.** Depuis la console : `sudo apt install openssh-server`.

### « `auditctl -l` affiche "No rules" »

Le fichier de règles existe mais n'a pas été chargé. Exécutez :
```bash
sudo augenrules --load
```

Si l'erreur persiste, vérifiez la syntaxe du fichier :
```bash
sudo auditctl -R /etc/audit/rules.d/malware-analysis.rules
```

Les erreurs de syntaxe seront affichées avec le numéro de ligne.

### « Le snapshot revert ne semble pas fonctionner »

Si le fichier marqueur existe encore après le revert, vérifiez que vous avez bien restauré le bon snapshot (celui pris **avant** la création du fichier). Vérifiez avec :
```bash
virsh snapshot-list malware-lab
```

Assurez-vous que le snapshot `test-checkpoint-solution` a bien été créé avant l'écriture du fichier `marqueur.txt`.

---

## État final du lab

Après validation du checkpoint, l'architecture de votre lab est la suivante :

```
~/malware-lab/
├── malware-lab.qcow2              ← Image disque de la VM (avec snapshot clean-base intégré)
├── scripts/
│   ├── prepare_analysis.sh        ← Prépare une session d'analyse
│   ├── cleanup_analysis.sh        ← Collecte les artefacts et rollback
│   └── start_monitoring.sh        ← À copier dans la VM, lance le monitoring
└── analyses/                      ← Répertoire des artefacts (vide pour l'instant)
```

La VM `malware-lab` contient :

```
/home/analyst/
├── malware-samples/               ← Répertoire pour les samples (vide, prêt)
├── captures/                      ← Répertoire pour les logs de monitoring
└── re-venv/                       ← Environnement Python (Frida, pwntools)

/home/sample-runner/                ← Utilisateur d'exécution des samples (drwx------)

/etc/audit/rules.d/
└── malware-analysis.rules          ← Règles auditd prêtes à être chargées
```

Vous êtes prêt pour le chapitre 27.

⏭️
