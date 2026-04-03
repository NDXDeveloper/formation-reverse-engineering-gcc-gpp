🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 26.2 — VM dédiée avec QEMU/KVM — snapshots et réseau isolé

> **Chapitre 26 — Mise en place d'un lab d'analyse sécurisé**  
> **Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**

---

## Pourquoi QEMU/KVM

Au chapitre 4, nous avions présenté plusieurs solutions de virtualisation (VirtualBox, QEMU, UTM). Pour le lab d'analyse de malware, cette formation recommande **QEMU/KVM** comme hyperviseur principal, pour plusieurs raisons.

KVM (Kernel-based Virtual Machine) est intégré directement dans le noyau Linux depuis la version 2.6.20. Ce n'est pas un logiciel tiers installé par-dessus le système : c'est un module noyau qui exploite les extensions de virtualisation matérielle du processeur (Intel VT-x ou AMD-V) pour exécuter le code invité quasi nativement. Les performances sont proches du bare-metal, ce qui est appréciable quand on lance Ghidra ou un fuzzer à l'intérieur de la VM.

QEMU fournit l'émulation des périphériques (disque, réseau, écran, USB…) et l'interface utilisateur. Couplé à KVM, il forme un duo mature, audité, largement utilisé dans l'industrie (c'est la base de la virtualisation chez la plupart des fournisseurs cloud), et entièrement open source. Pour notre usage, trois propriétés sont déterminantes :

- **Snapshots internes au format qcow2** — le format d'image disque qcow2 supporte nativement les snapshots. Pas besoin de copier un fichier de 20 Go pour sauvegarder l'état de la VM : le snapshot enregistre uniquement le delta par rapport à l'état précédent, ce qui le rend quasi instantané.  
- **Contrôle réseau fin** — QEMU permet de créer des interfaces réseau virtuelles rattachées à des bridges Linux standard. On peut configurer l'isolation réseau avec les outils natifs du système (`ip`, `bridge`, `iptables`) sans dépendre d'une couche d'abstraction propriétaire.  
- **Pilotage en ligne de commande** — tout est scriptable. Créer une VM, prendre un snapshot, restaurer un état, démarrer une analyse : tout peut être encapsulé dans des scripts shell reproductibles, ce qui s'inscrit dans notre exigence de reproductibilité (section 26.1).

> 💡 **Si vous êtes sur macOS ou Windows** — UTM (macOS, basé sur QEMU) et VirtualBox (multiplateforme) sont des alternatives viables. Les principes de ce chapitre (snapshots, réseau isolé, moindre privilège) s'appliquent de la même manière. Les commandes spécifiques différeront, mais la logique reste identique. L'annexe de ce chapitre fournit les équivalences VirtualBox pour les commandes QEMU présentées ici.

---

## Vérifier le support de la virtualisation matérielle

Avant toute installation, vérifions que le processeur supporte la virtualisation matérielle et que le module KVM est chargé.

```bash
# Vérifier les flags CPU (intel: vmx, amd: svm)
grep -Ec '(vmx|svm)' /proc/cpuinfo
```

Si la commande retourne `0`, la virtualisation matérielle est soit absente du processeur, soit désactivée dans le BIOS/UEFI. Dans ce cas, accédez aux paramètres du firmware de votre machine et activez « Intel VT-x » ou « AMD-V » / « SVM Mode » selon votre processeur.

```bash
# Vérifier que le module KVM est chargé
lsmod | grep kvm
```

Vous devriez voir `kvm` et `kvm_intel` (ou `kvm_amd`) dans la liste. Si ce n'est pas le cas :

```bash
sudo modprobe kvm  
sudo modprobe kvm_intel   # ou kvm_amd  
```

Pour un diagnostic complet, le paquet `cpu-checker` fournit la commande `kvm-ok` :

```bash
sudo apt install cpu-checker  
kvm-ok  
```

La réponse attendue est : `INFO: /dev/kvm exists` suivi de `KVM acceleration can be used`.

---

## Installation des paquets

Sur Debian/Ubuntu, l'installation se fait en une commande :

```bash
sudo apt update  
sudo apt install qemu-system-x86 qemu-utils libvirt-daemon-system \  
                 libvirt-clients virtinst virt-manager bridge-utils
```

Détail des paquets :

- `qemu-system-x86` — l'émulateur QEMU pour l'architecture x86-64.  
- `qemu-utils` — utilitaires pour manipuler les images disque (`qemu-img`).  
- `libvirt-daemon-system` et `libvirt-clients` — la couche de gestion libvirt, qui fournit une API unifiée pour piloter QEMU/KVM. Nous l'utiliserons principalement via `virsh`.  
- `virtinst` — outils d'installation de VM en ligne de commande (`virt-install`).  
- `virt-manager` — interface graphique facultative mais utile pour les premières prises en main.  
- `bridge-utils` — utilitaires de gestion de bridges réseau (`brctl`), complétés par la commande `ip` moderne.

Ajoutez votre utilisateur au groupe `libvirt` pour éviter de travailler en root :

```bash
sudo usermod -aG libvirt $(whoami)  
newgrp libvirt  
```

Vérifiez que le service libvirt est actif :

```bash
sudo systemctl enable --now libvirtd  
systemctl status libvirtd  
```

---

## Création de l'image disque qcow2

Le format **qcow2** (QEMU Copy-On-Write version 2) est le choix naturel pour notre lab. Il offre trois propriétés essentielles : l'allocation dynamique (le fichier ne prend sur le disque hôte que l'espace réellement utilisé par la VM), la prise en charge native des snapshots, et la compression optionnelle.

Créons une image de 30 Go pour notre VM d'analyse :

```bash
qemu-img create -f qcow2 malware-lab.qcow2 30G
```

Cette commande crée un fichier qui ne pèse que quelques centaines de Ko sur le disque. Les 30 Go représentent la taille maximale que la VM pourra utiliser — l'espace sera alloué au fur et à mesure des écritures par le système invité.

> 💡 **Pourquoi 30 Go ?** C'est un compromis. Le système Debian minimal avec nos outils RE occupe environ 8-10 Go. Le reste laisse de la marge pour les samples, les captures `tcpdump`, les exports Ghidra et les fichiers temporaires d'analyse. Si l'espace vient à manquer, il est possible d'agrandir l'image ultérieurement avec `qemu-img resize`.

---

## Installation du système invité

Nous installons un système Debian (ou Ubuntu Server) minimal. L'objectif est d'avoir un système léger, sans environnement de bureau superflu, sur lequel nous installerons uniquement les outils nécessaires à l'analyse.

Téléchargez l'ISO d'installation :

```bash
# Exemple avec Debian 12 (Bookworm) netinst
wget https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-12.*-amd64-netinst.iso
```

> ⚠️ **Important** — Le téléchargement de l'ISO et l'installation des paquets dans la VM nécessitent un accès réseau temporaire. Ce réseau sera **coupé définitivement** avant la première exécution de sample. L'accès réseau durant l'installation n'est pas une violation de nos principes d'isolation : aucun code hostile n'est encore présent dans la VM à ce stade.

Lancez l'installation avec `virt-install` :

```bash
virt-install \
  --name malware-lab \
  --ram 4096 \
  --vcpus 2 \
  --disk path=./malware-lab.qcow2,format=qcow2 \
  --cdrom debian-12.*-amd64-netinst.iso \
  --os-variant debian12 \
  --network network=default \
  --graphics spice \
  --video virtio \
  --boot uefi \
  --noautoconsole
```

Détail des paramètres importants :

- `--ram 4096` — 4 Go de RAM. Suffisant pour GDB et Frida. Si vous prévoyez d'utiliser Ghidra à l'intérieur de la VM (et non sur l'hôte), montez à 8 Go.  
- `--vcpus 2` — deux cœurs virtuels. Les outils de monitoring et le sample doivent pouvoir tourner en parallèle sans se bloquer mutuellement.  
- `--network network=default` — réseau NAT par défaut de libvirt, **utilisé uniquement pendant l'installation**. Nous le remplacerons par un bridge isolé avant toute analyse.  
- `--boot uefi` — démarrage UEFI. C'est la configuration standard des systèmes modernes, et certains malwares vérifient le mode de boot.

Ouvrez la console graphique pour terminer l'installation :

```bash
virt-manager &
# Ou en console :
virsh console malware-lab
```

Durant l'installation Debian, choisissez les options suivantes :

- Partitionnement : disque entier, tout dans une seule partition (simplicité).  
- Logiciels : décochez tout sauf « utilitaires standard du système » et « serveur SSH ». Pas d'environnement de bureau.  
- Créez un utilisateur non-root nommé `analyst`.

---

## Installation des outils RE dans la VM

Une fois le système installé et démarré, connectez-vous en SSH (le réseau NAT par défaut le permet) et installez les outils nécessaires :

```bash
# Outils de base
sudo apt update && sudo apt upgrade -y  
sudo apt install -y build-essential gdb strace ltrace \  
                    python3 python3-pip python3-venv \
                    tcpdump inotify-tools auditd sysdig \
                    wget curl git unzip file binutils \
                    net-tools nmap hexdump xxd

# Frida
python3 -m venv ~/re-venv  
source ~/re-venv/bin/activate  
pip install frida-tools pwntools  

# GEF (extension GDB)
bash -c "$(curl -fsSL https://gef.blah.cat/sh)"
```

Pour Ghidra, deux approches sont possibles. Si vous prévoyez de l'utiliser depuis la VM (nécessite plus de RAM et un serveur X ou un accès VNC), installez-le dans la VM. Sinon — et c'est l'approche recommandée — utilisez Ghidra sur l'hôte pour l'analyse statique, et réservez la VM à l'analyse dynamique (GDB, Frida, strace, monitoring). Cette séparation est naturelle : l'analyse statique ne nécessite pas d'exécuter le sample, donc pas besoin d'isolation.

> 💡 **Transférer un binaire de la VM vers l'hôte pour l'analyse statique** — Utilisez `scp` via le réseau host-only (que nous configurerons plus bas), ou montez temporairement un répertoire partagé le temps du transfert. Le sample n'a jamais besoin d'être exécuté sur l'hôte : on ne copie que pour le charger dans Ghidra.

Créez le répertoire de travail pour les samples :

```bash
mkdir -p ~/malware-samples  
chmod 700 ~/malware-samples  
```

---

## Durcissement de la VM

Avant de prendre le snapshot de référence, appliquons les mesures de durcissement qui réduisent la surface d'attaque conformément au principe du moindre privilège (section 26.1).

### Désactiver les fonctionnalités superflues

Arrêtez la VM et modifiez sa configuration avec `virsh edit` :

```bash
virsh shutdown malware-lab  
virsh edit malware-lab  
```

Dans le XML de la VM, vérifiez ou modifiez les points suivants :

**Supprimer les périphériques inutiles.** Retirez les sections `<channel>` relatives au partage de fichiers SPICE, le périphérique `<redirdev>` (redirection USB), et le `<filesystem>` si présent (dossier partagé) :

```xml
<!-- SUPPRIMER si présent : dossier partagé -->
<!-- <filesystem type='mount' accessmode='mapped'>
  <source dir='/home/user/shared'/>
  <target dir='shared'/>
</filesystem> -->

<!-- SUPPRIMER si présent : redirection USB -->
<!-- <redirdev bus='usb' type='spicevmc'/> -->
```

**Désactiver le presse-papier partagé.** Si un canal SPICE `com.redhat.spice.0` est défini, vérifiez qu'aucun agent n'est installé dans la VM (`spice-vdagent`). Le plus simple est de ne pas installer ce paquet.

**Limiter les contrôleurs USB.** Si la VM n'a pas besoin d'USB (notre cas), retirez le contrôleur USB ou laissez-le vide sans périphérique attaché.

### Créer un utilisateur dédié à l'exécution des samples

Dans la VM, nous allons séparer l'utilisateur `analyst` (qui lance les outils d'analyse) d'un utilisateur encore plus restreint qui exécutera les samples :

```bash
sudo useradd -m -s /bin/bash sample-runner  
sudo chmod 700 /home/sample-runner  
```

L'idée est de lancer les samples sous `sample-runner` (via `sudo -u sample-runner`) tandis que les outils de monitoring tournent sous `analyst` ou `root`. Cela ajoute une couche de séparation des privilèges à l'intérieur même de la VM : si le sample tente d'accéder aux fichiers d'`analyst` (notes, scripts, configurations GDB), les permissions Unix l'en empêchent.

---

## Gestion des snapshots

Les snapshots sont la pierre angulaire de la réversibilité. Avec le format qcow2, QEMU stocke les snapshots à l'intérieur même du fichier image sous forme de couches copy-on-write. Créer un snapshot est quasi instantané et ne coûte que l'espace des blocs modifiés après le snapshot.

### Le snapshot de référence (golden image)

Après l'installation du système, des outils et le durcissement — mais **avant toute copie de sample dans la VM** — prenez le snapshot de référence :

```bash
# VM arrêtée (snapshot offline — le plus fiable)
virsh snapshot-create-as malware-lab \
  --name "clean-base" \
  --description "Debian 12 + outils RE, durci, aucun sample présent" \
  --atomic
```

Ce snapshot est votre **point de retour garanti**. Quoi qu'il arrive dans la VM après ce point, vous pouvez toujours revenir à cet état propre.

Vérifiez sa création :

```bash
virsh snapshot-list malware-lab
```

### Workflow d'analyse avec snapshots

Chaque session d'analyse d'un sample suit le même workflow en trois étapes :

```
1. RESTAURER le snapshot clean-base
   └─ virsh snapshot-revert malware-lab --snapshotname "clean-base"

2. PRÉPARER la session
   ├─ Démarrer la VM
   ├─ Copier le sample dans ~/malware-samples/
   ├─ Activer le monitoring (auditd, tcpdump, inotifywait)
   ├─ Prendre un snapshot pré-exécution nommé
   │   └─ virsh snapshot-create-as malware-lab \
   │        --name "pre-exec-ch27-ransomware-$(date +%Y%m%d-%H%M)"
   └─ Vérifier l'isolation réseau (voir section 26.4)

3. EXÉCUTER et OBSERVER
   ├─ Lancer le sample sous l'utilisateur sample-runner
   ├─ Observer avec les outils de monitoring
   ├─ Collecter les artefacts (pcap, logs auditd, fichiers modifiés)
   └─ Optionnel : prendre un snapshot post-exécution si l'état
      de la VM compromise est intéressant à conserver pour analyse
```

Après la session, on revient au snapshot `clean-base` pour la prochaine analyse. Les artefacts collectés (fichiers `.pcap`, logs) sont transférés vers l'hôte via `scp` avant le rollback.

### Commandes de référence pour les snapshots

```bash
# Lister les snapshots
virsh snapshot-list malware-lab

# Créer un snapshot (VM en cours d'exécution — snapshot live)
virsh snapshot-create-as malware-lab \
  --name "nom-descriptif" \
  --description "Description de l'état"

# Créer un snapshot (VM arrêtée — snapshot offline, plus fiable)
virsh shutdown malware-lab  
virsh snapshot-create-as malware-lab --name "nom-descriptif" --atomic  

# Restaurer un snapshot
virsh snapshot-revert malware-lab --snapshotname "clean-base"

# Supprimer un snapshot devenu inutile
virsh snapshot-delete malware-lab --snapshotname "nom-à-supprimer"

# Voir les détails d'un snapshot
virsh snapshot-info malware-lab --snapshotname "clean-base"
```

> 💡 **Snapshots live vs offline** — Un snapshot live capture l'état complet de la VM en cours d'exécution (mémoire RAM incluse). Il est plus lourd mais permet de reprendre exactement là où on s'est arrêté. Un snapshot offline (VM arrêtée) ne capture que l'état du disque. Pour le snapshot `clean-base`, le mode offline est préférable : il est plus petit, plus rapide à restaurer, et il n'y a aucun état mémoire à conserver à ce stade.

---

## Configuration réseau : du NAT à l'isolation

C'est l'étape la plus critique de toute la mise en place. Nous allons créer une configuration réseau qui permet deux modes de fonctionnement mutuellement exclusifs :

- **Mode maintenance** — la VM a accès au réseau (NAT) pour installer ou mettre à jour des paquets. Aucun sample n'est présent ni exécuté dans ce mode.  
- **Mode analyse** — la VM est connectée à un bridge isolé sans aucune route vers l'extérieur. C'est le seul mode dans lequel un sample peut être exécuté.

### Créer le réseau isolé avec libvirt

Libvirt gère les réseaux virtuels via des fichiers XML. Créons un réseau nommé `isolated-malware` :

```bash
cat > /tmp/isolated-malware.xml << 'EOF'
<network>
  <name>isolated-malware</name>
  <bridge name="br-malware" stp="on" delay="0"/>
  <ip address="10.66.66.1" netmask="255.255.255.0">
    <dhcp>
      <range start="10.66.66.100" end="10.66.66.200"/>
    </dhcp>
  </ip>
  <!-- PAS de <forward> : c'est ce qui rend le réseau isolé.
       Sans élément <forward>, libvirt ne crée aucune règle
       iptables de NAT ni de routage vers l'extérieur. -->
</network>
EOF

virsh net-define /tmp/isolated-malware.xml  
virsh net-start isolated-malware  
virsh net-autostart isolated-malware  
```

L'absence de balise `<forward>` est la clé. Comparons avec le réseau `default` de libvirt :

```xml
<!-- Réseau "default" (NAT vers l'extérieur — DANGEREUX pour l'analyse) -->
<network>
  <name>default</name>
  <forward mode='nat'/>          <!-- ← Cette ligne donne accès à Internet -->
  <bridge name='virbr0'/>
  <ip address='192.168.122.1' netmask='255.255.255.0'>
    <dhcp>...</dhcp>
  </ip>
</network>

<!-- Réseau "isolated-malware" (aucune sortie — SÛR pour l'analyse) -->
<network>
  <name>isolated-malware</name>
  <!-- Pas de <forward> : rien ne sort -->
  <bridge name='br-malware'/>
  <ip address='10.66.66.1' netmask='255.255.255.0'>
    <dhcp>...</dhcp>
  </ip>
</network>
```

### Basculer la VM entre les deux réseaux

Pour passer du mode maintenance au mode analyse (et inversement), on modifie l'interface réseau de la VM :

```bash
# Passer en mode analyse (réseau isolé)
virsh detach-interface malware-lab network --current  
virsh attach-interface malware-lab network isolated-malware --current  

# Passer en mode maintenance (NAT — jamais avec un sample présent)
virsh detach-interface malware-lab network --current  
virsh attach-interface malware-lab network default --current  
```

Dans la VM, redémarrez le client DHCP pour obtenir une adresse sur le nouveau réseau :

```bash
sudo dhclient -r && sudo dhclient  
ip addr show  
```

En mode analyse, la VM obtiendra une adresse en `10.66.66.x`. Elle pourra communiquer avec l'hôte sur `10.66.66.1` (utile pour `scp`), mais **aucune route ne mène au-delà**.

### Verrouiller la configuration avec iptables sur l'hôte

Par précaution supplémentaire, ajoutons des règles `iptables` sur l'hôte qui bloquent explicitement tout forwarding depuis le bridge `br-malware` :

```bash
# Bloquer tout forwarding depuis/vers br-malware
sudo iptables -I FORWARD -i br-malware -o eth0 -j DROP  
sudo iptables -I FORWARD -i eth0 -o br-malware -j DROP  

# Bloquer aussi vers les autres interfaces (wlan0, etc.)
sudo iptables -I FORWARD -i br-malware ! -o br-malware -j DROP
```

> ⚠️ Remplacez `eth0` par le nom réel de votre interface réseau externe (`ip route show default` pour l'identifier).

Ces règles sont une **ceinture de sécurité supplémentaire**. Même si une erreur de configuration libvirt créait une route involontaire, `iptables` bloquerait le trafic au niveau du noyau de l'hôte.

Pour persister ces règles entre les redémarrages :

```bash
sudo apt install iptables-persistent  
sudo netfilter-persistent save  
```

---

## Automatiser le workflow avec des scripts

Tout ce que nous avons fait manuellement jusqu'ici peut (et devrait) être encapsulé dans des scripts. Voici la structure recommandée :

### Script de préparation de session

```bash
#!/bin/bash
# prepare_analysis.sh — Prépare la VM pour une session d'analyse
# Usage : ./prepare_analysis.sh <nom-du-sample>

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

echo "[*] Prise du snapshot pré-exécution : $SNAPSHOT_NAME"  
virsh snapshot-create-as "$VM_NAME" \  
  --name "$SNAPSHOT_NAME" \
  --description "Avant exécution de $SAMPLE_NAME"

echo "[+] VM prête pour l'analyse de : $SAMPLE_NAME"  
echo "    Snapshot : $SNAPSHOT_NAME"  
echo "    Réseau   : isolated-malware (10.66.66.0/24)"  
echo ""  
echo "    Prochaines étapes :"  
echo "    1. ssh analyst@10.66.66.100"  
echo "    2. Copier le sample : scp $SAMPLE_NAME analyst@10.66.66.100:~/malware-samples/"  
echo "    3. Démarrer le monitoring (voir section 26.3)"  
echo "    4. Exécuter le sample"  
```

### Script de nettoyage post-analyse

```bash
#!/bin/bash
# cleanup_analysis.sh — Collecte les artefacts et restaure l'état propre
# Usage : ./cleanup_analysis.sh <nom-du-sample>

set -euo pipefail

SAMPLE_NAME="${1:?Usage: $0 <nom-du-sample>}"  
VM_NAME="malware-lab"  
TIMESTAMP=$(date +%Y%m%d-%H%M%S)  
OUTPUT_DIR="./analyses/${SAMPLE_NAME}-${TIMESTAMP}"  

mkdir -p "$OUTPUT_DIR"

echo "[*] Collecte des artefacts depuis la VM..."  
VM_IP="10.66.66.100"  

scp "analyst@${VM_IP}:~/captures/*.pcap" "$OUTPUT_DIR/" 2>/dev/null || echo "    Pas de pcap"  
scp "analyst@${VM_IP}:~/captures/audit.log" "$OUTPUT_DIR/" 2>/dev/null || echo "    Pas de log audit"  
scp "analyst@${VM_IP}:~/captures/inotify.log" "$OUTPUT_DIR/" 2>/dev/null || echo "    Pas de log inotify"  

echo "[*] Artefacts sauvegardés dans : $OUTPUT_DIR"

echo "[*] Restauration du snapshot clean-base..."  
virsh snapshot-revert "$VM_NAME" --snapshotname "clean-base"  

echo "[+] Nettoyage terminé. La VM est revenue à l'état propre."
```

Ces scripts sont des points de départ. Adaptez-les à votre workflow et enrichissez-les au fil des analyses.

---

## Vérifications post-installation

Avant de considérer le lab comme opérationnel, vérifiez les points suivants :

```bash
# 1. La VM démarre correctement
virsh start malware-lab  
virsh list --all    # État : "running"  

# 2. Le snapshot clean-base existe et est restaurable
virsh snapshot-list malware-lab  
virsh snapshot-revert malware-lab --snapshotname "clean-base"  

# 3. Le réseau isolé est actif
virsh net-list --all
# "isolated-malware" doit être "active"

# 4. En mode analyse, la VM n'a PAS accès à Internet
# (Depuis la VM, après bascule sur isolated-malware) :
ping -c 3 8.8.8.8          # Doit échouer (timeout)  
ping -c 3 1.1.1.1          # Doit échouer (timeout)  
curl -m 5 http://example.com  # Doit échouer (timeout)  

# 5. En mode analyse, la VM peut joindre l'hôte
ping -c 3 10.66.66.1       # Doit réussir (communication hôte ↔ VM)

# 6. L'hôte ne forwarde PAS les paquets du bridge
sudo iptables -L FORWARD -v | grep br-malware
# Les règles DROP doivent être présentes
```

La vérification n°4 est la plus importante. Si `ping 8.8.8.8` réussit depuis la VM en mode analyse, **le lab n'est pas isolé**. Ne poursuivez pas tant que ce point n'est pas résolu.

---

## Récapitulatif de l'architecture réseau

```
MACHINE HÔTE
│
├── eth0 (ou wlan0) ─────────── Internet / LAN
│     │
│     │  iptables : DROP tout forwarding depuis br-malware
│     │
├── virbr0 (192.168.122.0/24) ── Réseau "default" (NAT)
│     │                            └─ Mode maintenance uniquement
│     │
├── br-malware (10.66.66.0/24) ── Réseau "isolated-malware"
│     │                              ├─ Pas de <forward>
│     │                              ├─ Pas de NAT
│     │                              ├─ Pas de route externe
│     │                              └─ tcpdump écoute ici
│     │
│     └── VM malware-lab
│           └─ 10.66.66.100 (DHCP)
│
└── Règle absolue : la VM n'est JAMAIS sur virbr0
    quand un sample est présent dans ~/malware-samples/
```

---

> 📌 **À retenir** — La VM est un outil jetable. Traitez-la comme un gant en latex : on l'enfile propre, on manipule le sample, puis on la jette (rollback) et on en prend une neuve (snapshot). Si vous hésitez à restaurer le snapshot parce que vous avez « des trucs importants dans la VM », c'est le signe que votre séparation hôte/VM n'est pas assez stricte. Les notes, rapports et scripts d'analyse vivent sur l'hôte. La VM ne contient que ce qui peut être détruit sans regret.

⏭️ [Outils de monitoring : `auditd`, `inotifywait`, `tcpdump`, `sysdig`](/26-lab-securise/03-outils-monitoring.md)
