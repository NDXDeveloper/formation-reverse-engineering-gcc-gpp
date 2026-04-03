🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 4.3 — Création d'une VM sandboxée (VirtualBox / QEMU / UTM pour macOS)

> 🎯 **Objectif de cette section** : créer une machine virtuelle isolée qui servira de laboratoire de reverse engineering tout au long de la formation. À la fin de cette section, vous disposerez d'une VM fonctionnelle, avec un système de snapshots configuré pour pouvoir revenir à un état propre à tout moment.

---

## Pourquoi une VM et pas directement l'hôte ?

Travailler dans une machine virtuelle n'est pas un caprice d'organisation. C'est une **mesure de sécurité fondamentale** et un outil de productivité pour le reverse engineer.

### Isolation et sécurité

Le reverse engineering implique d'exécuter des binaires dont on ne maîtrise pas le comportement. Même les binaires d'entraînement de cette formation — que nous avons écrits nous-mêmes — seront exécutés dans des conditions inhabituelles : injection de code avec Frida, fuzzing avec des entrées aléatoires, exécution de samples simulant un ransomware ou un dropper (Partie VI). Une erreur de manipulation, un binaire qui se comporte de façon inattendue sous instrumentation, et votre système hôte pourrait être affecté.

La VM agit comme un **conteneur jetable**. Si quelque chose tourne mal, le dommage reste confiné à la VM. Un snapshot restauré en quelques secondes, et vous repartez de zéro.

### Reproductibilité

Un environnement virtualisé est identique d'une session à l'autre. Pas de mise à jour système surprise qui casse une dépendance, pas de conflit avec un logiciel installé pour un autre projet. Lorsque les instructions de ce tutoriel disent « lancez cette commande », le résultat sera le même pour tout le monde.

### Snapshots : votre filet de sécurité

Les snapshots sont des instantanés de l'état complet de la VM (disque, mémoire, configuration) à un instant donné. Nous en créerons plusieurs au fil de la mise en place :

| Snapshot | Quand le prendre | Usage |  
|---|---|---|  
| `base-install` | Après installation du système, avant les outils | Repartir de zéro si l'installation des outils échoue |  
| `tools-ready` | Après la section 4.2 (tous les outils installés) | État de référence pour la formation |  
| `pre-malware` | Avant d'aborder la Partie VI | Isoler les expérimentations malware |

> 💡 **Règle d'or** : prenez un snapshot *avant* chaque opération risquée ou irréversible. Restaurer un snapshot prend 10 secondes. Réinstaller un système prend une heure.

---

## Choix de l'hyperviseur

Le choix de l'hyperviseur dépend de votre système hôte et de vos contraintes matérielles. Voici les trois options que nous documentons.

### VirtualBox — le choix multiplateforme

**Pour qui** : utilisateurs Windows, macOS (Intel) ou Linux qui veulent une solution gratuite, graphique et simple à prendre en main.

**Avantages :**
- Gratuit et open source (licence GPLv3).  
- Disponible sur Windows, macOS (Intel et Apple Silicon via bêta), Linux.  
- Interface graphique intuitive pour gérer les VM et les snapshots.  
- Large communauté et documentation abondante.  
- Les Guest Additions améliorent l'intégration (redimensionnement d'écran, dossiers partagés, presse-papiers partagé).

**Inconvénients :**
- Performances inférieures à QEMU/KVM sur Linux (pas d'accélération KVM native).  
- Sur macOS Apple Silicon, le support x86-64 est expérimental et lent (émulation, pas de virtualisation native).

**Version recommandée** : VirtualBox 7.1+.

### QEMU/KVM — la performance native sous Linux

**Pour qui** : utilisateurs Linux qui veulent les meilleures performances possibles.

**Avantages :**
- Performances quasi-natives grâce à l'accélération KVM (le CPU exécute directement le code de la VM sans traduction).  
- Très flexible : pilotable en ligne de commande ou via l'interface graphique `virt-manager`.  
- Parfait pour les configurations headless ou automatisées.  
- Supporte les snapshots, les réseaux virtuels, le passthrough de périphériques.

**Inconvénients :**
- Réservé à Linux (KVM est un module du noyau Linux).  
- Configuration initiale plus technique que VirtualBox.  
- `virt-manager` est fonctionnel mais moins ergonomique que l'interface VirtualBox.

**Paquets requis** : `qemu-system-x86`, `libvirt-daemon-system`, `virt-manager`, `ovmf`.

### UTM — la solution pour macOS Apple Silicon

**Pour qui** : utilisateurs de Mac M1/M2/M3/M4 qui ont besoin d'émuler l'architecture x86-64.

**Avantages :**
- Interface graphique native macOS, élégante et simple.  
- Basé sur QEMU en backend, donc bénéficie de sa maturité.  
- Supporte la **virtualisation ARM64** (quasi-native, très rapide) et l'**émulation x86-64** (plus lent, mais fonctionnel).  
- Disponible gratuitement sur GitHub (payant sur l'App Store, même logiciel).  
- Gestion intégrée des snapshots.

**Inconvénients :**
- L'émulation x86-64 est significativement plus lente que la virtualisation native. Comptez un facteur 3 à 5 de ralentissement par rapport à du natif. C'est utilisable pour l'apprentissage, mais les sessions de fuzzing lourdes (chapitre 15) seront pénalisées.  
- Moins de documentation communautaire que VirtualBox.

**Point critique — architecture** : les binaires de cette formation sont compilés pour **x86-64**. Sur un Mac Apple Silicon, vous avez deux options :

1. **Émulation x86-64** (recommandée pour cette formation) : UTM utilise QEMU en mode émulation TCG. L'ensemble de l'OS invité et des binaires tourne en x86-64. Tout fonctionne, mais c'est plus lent.  
2. **Virtualisation ARM64** : rapide, mais les binaires x86-64 ne s'exécuteront pas nativement. Vous devrez installer `qemu-user-static` dans la VM pour émuler x86-64 au niveau utilisateur — une configuration plus complexe et parfois incompatible avec certains outils (GDB, Frida, ptrace).

> 📌 **Notre recommandation pour Apple Silicon** : utilisez UTM en mode **émulation x86-64**. Les performances sont suffisantes pour l'apprentissage. La compatibilité totale avec les outils et les binaires vaut le compromis de vitesse.

---

## Guide pas-à-pas : VirtualBox

### 1. Installation de VirtualBox

Téléchargez l'installeur depuis [virtualbox.org/wiki/Downloads](https://www.virtualbox.org/wiki/Downloads) pour votre système hôte. Installez-le en suivant les instructions par défaut.

Sur un hôte Linux, vous pouvez aussi passer par apt :

```bash
[hôte] sudo apt install -y virtualbox virtualbox-ext-pack
```

Vérifiez que la virtualisation matérielle est activée dans le BIOS/UEFI de votre machine (VT-x pour Intel, AMD-V pour AMD). Sans cela, VirtualBox fonctionnera en mode émulation logicielle, beaucoup plus lent.

### 2. Création de la VM

Ouvrez VirtualBox et cliquez sur **Nouvelle**. Paramétrez comme suit :

| Paramètre | Valeur recommandée |  
|---|---|  
| Nom | `RE-Lab` |  
| Type | Linux |  
| Version | Ubuntu (64-bit) |  
| Mémoire vive | **4096 Mo** minimum — **8192 Mo** recommandé |  
| Processeurs | **2 vCPU** minimum — **4 vCPU** recommandé |  
| Disque dur | Créer un disque virtuel, **60 Go**, dynamiquement alloué |  
| Contrôleur graphique | VMSVGA |  
| Vidéo mémoire | 128 Mo |

> 💡 **Pourquoi 60 Go ?** Ghidra consomme beaucoup d'espace pour ses projets d'analyse (bases de données, index). AFL++ génère des corpus de fuzzing volumineux. 40 Go est un minimum, 60 Go est confortable. Le disque étant dynamiquement alloué, il ne consommera sur l'hôte que l'espace réellement utilisé.

### 3. Installation du système

Insérez l'ISO Ubuntu 24.04 LTS dans le lecteur virtuel (paramètre **Stockage** → contrôleur IDE → disque optique), puis démarrez la VM.

Pendant l'installation d'Ubuntu :

- **Partitionnement** : utilisez le schéma par défaut (disque entier, LVM si proposé). Pas besoin de complexité ici.  
- **Nom d'utilisateur** : `re` (ou tout autre nom — les commandes du tutoriel utilisent `$USER`).  
- **Nom de la machine** : `re-lab`.  
- **Paquets additionnels** : installez le serveur OpenSSH si proposé (utile pour se connecter en SSH depuis l'hôte).

Une fois l'installation terminée, redémarrez et retirez l'ISO du lecteur virtuel.

### 4. Guest Additions

Les Guest Additions améliorent considérablement l'expérience :

- Redimensionnement dynamique de l'écran de la VM.  
- Presse-papiers partagé entre hôte et VM (indispensable pour copier des commandes).  
- Dossiers partagés entre hôte et VM.  
- Meilleure gestion de la souris.

```bash
[vm] sudo apt install -y virtualbox-guest-utils virtualbox-guest-x11
[vm] sudo reboot
```

Après le redémarrage, activez dans le menu VirtualBox : **Périphériques → Presse-papiers partagé → Bidirectionnel**.

### 5. Premier snapshot

Le système est installé, les Guest Additions sont actives. Prenez votre premier snapshot :

**Machine → Prendre un instantané** → nommez-le `base-install`.

C'est votre point de retour le plus basique. Si quoi que ce soit se passe mal dans les étapes suivantes, vous pourrez restaurer cet état.

---

## Guide pas-à-pas : QEMU/KVM

### 1. Installation

```bash
[hôte] sudo apt install -y \
    qemu-system-x86 \
    qemu-utils \
    libvirt-daemon-system \
    libvirt-clients \
    virt-manager \
    bridge-utils \
    ovmf
```

Ajoutez votre utilisateur aux groupes nécessaires :

```bash
[hôte] sudo usermod -aG libvirt,kvm $USER
```

Déconnectez-vous puis reconnectez-vous pour que les groupes prennent effet.

Vérifiez que KVM est disponible :

```bash
[hôte] kvm-ok
# attendu : "INFO: /dev/kvm exists" et "KVM acceleration can be used"
```

### 2. Création de la VM avec virt-manager

Lancez `virt-manager` (interface graphique). Cliquez sur **Créer une nouvelle machine virtuelle** :

- **Méthode d'installation** : média d'installation local (ISO).  
- **ISO** : sélectionnez l'ISO Ubuntu 24.04 LTS.  
- **Mémoire** : 4096 Mo minimum, 8192 Mo recommandé.  
- **CPU** : 2 minimum, 4 recommandé.  
- **Stockage** : créez un disque de 60 Go (format qcow2, qui est automatiquement alloué dynamiquement).  
- **Réseau** : NAT par défaut (virbr0). Nous ajusterons en section 4.4.

Cochez **Personnaliser la configuration avant l'installation** pour vérifier :
- **Firmware** : UEFI (OVMF) de préférence, BIOS sinon.  
- **Chipset** : Q35 (plus moderne que i440FX).  
- **Vidéo** : QXL ou Virtio (meilleures performances graphiques que VGA).

Lancez l'installation et suivez les mêmes étapes que pour VirtualBox.

### 3. Installation de l'agent SPICE (équivalent des Guest Additions)

```bash
[vm] sudo apt install -y spice-vdagent qemu-guest-agent
[vm] sudo systemctl enable --now spice-vdagent qemu-guest-agent
```

Cela active le redimensionnement d'écran dynamique et le presse-papiers partagé.

### 4. Création en ligne de commande (alternative sans GUI)

Pour ceux qui préfèrent la ligne de commande, voici l'équivalent complet :

```bash
# Création du disque
[hôte] qemu-img create -f qcow2 ~/vms/re-lab.qcow2 60G

# Lancement de l'installation
[hôte] qemu-system-x86_64 \
    -enable-kvm \
    -m 8192 \
    -smp 4 \
    -cpu host \
    -drive file=~/vms/re-lab.qcow2,format=qcow2,if=virtio \
    -cdrom ~/iso/ubuntu-24.04-desktop-amd64.iso \
    -boot d \
    -net nic,model=virtio \
    -net user,hostfwd=tcp::2222-:22 \
    -vga qxl \
    -display sdl
```

Après l'installation, retirez le paramètre `-cdrom` et `-boot d` pour les démarrages suivants.

### 5. Snapshots avec QEMU

Avec `virt-manager` : clic droit sur la VM → **Instantanés** → **Créer**.

En ligne de commande (VM éteinte) :

```bash
[hôte] qemu-img snapshot -c base-install ~/vms/re-lab.qcow2
```

Pour lister les snapshots :

```bash
[hôte] qemu-img snapshot -l ~/vms/re-lab.qcow2
```

Pour restaurer :

```bash
[hôte] qemu-img snapshot -a base-install ~/vms/re-lab.qcow2
```

Avec libvirt (VM allumée ou éteinte) :

```bash
[hôte] virsh snapshot-create-as RE-Lab base-install --description "Après installation du système"
[hôte] virsh snapshot-list RE-Lab
[hôte] virsh snapshot-revert RE-Lab base-install
```

---

## Guide pas-à-pas : UTM (macOS Apple Silicon)

### 1. Installation d'UTM

Téléchargez UTM depuis [github.com/utmapp/UTM/releases](https://github.com/utmapp/UTM/releases) (gratuit) ou depuis le Mac App Store (payant, même logiciel).

Glissez `UTM.app` dans votre dossier Applications.

### 2. Création de la VM en émulation x86-64

Ouvrez UTM et cliquez sur **Créer une nouvelle machine virtuelle** :

- **Type** : sélectionnez **Émuler** (et non pas « Virtualiser »).  
- **Système d'exploitation** : Linux.  
- **Architecture** : **x86_64**. C'est le point crucial — ne sélectionnez pas ARM64.  
- **Système** : laissez les valeurs par défaut (QEMU machine type `q35`).

Paramètres matériels :

| Paramètre | Valeur recommandée |  
|---|---|  
| Mémoire | **8192 Mo** (l'émulation consomme plus de RAM) |  
| CPU | **4 cœurs** (UTM traduira les instructions x86 via TCG) |  
| Backend d'accélération | TCG (sélectionné automatiquement en mode émulation) |

Stockage :

- Créez un nouveau disque de **60 Go** (format qcow2).

Réseau :

- Mode **Partagé** (équivalent du NAT VirtualBox). Suffisant pour télécharger des paquets.

### 3. Installation du système

Ajoutez l'ISO Ubuntu 24.04 LTS comme lecteur CD/DVD amovible dans les paramètres de la VM (onglet **Lecteurs**), puis démarrez.

> ⚠️ **L'installation sera plus lente qu'avec VirtualBox/QEMU-KVM.** L'émulation x86-64 sur Apple Silicon est fonctionnelle mais sensiblement plus lente. Comptez 30 à 60 minutes pour l'installation complète d'Ubuntu Desktop. Soyez patient — une fois installé, l'utilisation quotidienne est tout à fait viable.

> 💡 **Astuce performance** : si la lenteur du bureau GNOME est gênante, installez un bureau plus léger après l'installation :  
> ```bash  
> [vm] sudo apt install -y xfce4 xfce4-goodies  
> ```  
> Puis sélectionnez « Session Xfce » sur l'écran de connexion.

### 4. Agent SPICE

Comme pour QEMU/KVM :

```bash
[vm] sudo apt install -y spice-vdagent
[vm] sudo systemctl enable --now spice-vdagent
```

### 5. Snapshots dans UTM

Dans la fenêtre de la VM, cliquez sur l'icône d'appareil photo (ou **Menu → Prendre un instantané**). Nommez le snapshot `base-install`.

Pour restaurer : ouvrez la liste des snapshots, sélectionnez celui souhaité, cliquez sur **Restaurer**.

### 6. Performances attendues et optimisations

L'émulation TCG impose un overhead significatif. Voici quelques repères pour calibrer vos attentes :

| Opération | Temps natif (KVM) | Temps émulé (UTM/TCG) | Facteur |  
|---|---|---|---|  
| Compilation d'un petit projet C | ~2 s | ~8 s | ×4 |  
| Lancement de Ghidra + analyse auto | ~15 s | ~50 s | ×3 |  
| Session GDB interactive | Temps réel | Quasi-temps réel | ×1.5 |  
| Fuzzing AFL++ (execs/sec) | ~2000 | ~300–500 | ×4–6 |

Ces chiffres sont indicatifs et varient selon le modèle de Mac et la charge de travail. Pour l'apprentissage, c'est amplement suffisant. Pour du fuzzing intensif sur des projets réels, un poste x86-64 sera nettement préférable.

**Optimisations possibles :**
- Allouez le **maximum de RAM** que votre Mac peut offrir à la VM (la mémoire unifiée des puces Apple gère bien le partage).  
- Utilisez un **bureau léger** (Xfce, LXQt) plutôt que GNOME.  
- Pour les tâches purement CLI (GDB, strace, scripts), connectez-vous en **SSH** depuis le terminal macOS plutôt que d'utiliser l'affichage graphique émulé.  
- Désactivez les effets visuels dans les préférences du bureau.

---

## Accès SSH à la VM (tous hyperviseurs)

Quel que soit l'hyperviseur, un accès SSH depuis l'hôte est extrêmement pratique : il vous permet de travailler dans votre terminal habituel, d'utiliser votre éditeur de texte préféré et de transférer des fichiers avec `scp`.

Assurez-vous que le serveur SSH est installé dans la VM :

```bash
[vm] sudo apt install -y openssh-server
[vm] sudo systemctl enable --now ssh
```

La méthode de connexion dépend de la configuration réseau :

- **VirtualBox (NAT avec port forwarding)** : ajoutez une règle dans **Paramètres → Réseau → Avancé → Redirection de ports** : protocole TCP, port hôte 2222, port invité 22. Puis :
  ```bash
  [hôte] ssh -p 2222 re@127.0.0.1
  ```

- **QEMU/KVM (NAT via virbr0)** : la VM reçoit une IP sur le réseau 192.168.122.0/24. Trouvez-la avec `ip a` dans la VM, puis :
  ```bash
  [hôte] ssh re@192.168.122.xxx
  ```

- **UTM (mode partagé)** : fonctionne comme le NAT QEMU. Trouvez l'IP avec `ip a` dans la VM.

- **Mode host-only** (tous hyperviseurs) : après configuration en section 4.4, la VM sera accessible sur un réseau privé dédié.

> 💡 **Astuce** : ajoutez une entrée dans `~/.ssh/config` sur l'hôte pour simplifier la connexion :  
> ```  
> Host re-lab  
>     HostName 127.0.0.1  
>     Port 2222  
>     User re  
> ```  
> Vous pourrez alors simplement taper `ssh re-lab`.

---

## Dossiers partagés entre hôte et VM

Les dossiers partagés permettent de transférer facilement des fichiers (binaires, scripts, notes) entre l'hôte et la VM sans passer par SSH/SCP.

### VirtualBox

1. Dans **Paramètres → Dossiers partagés**, ajoutez un dossier de l'hôte (par ex. `~/shared-re`).  
2. Cochez **Montage automatique** et **Permanent**.  
3. Dans la VM :
   ```bash
   [vm] sudo usermod -aG vboxsf $USER
   [vm] # Déconnexion/reconnexion nécessaire
   ```
   Le dossier sera accessible dans `/media/sf_<nom>`.

### QEMU/KVM (virtio-fs ou 9p)

Avec `virt-manager`, ajoutez un périphérique de type **Filesystem** pointant vers un répertoire de l'hôte. Dans la VM :

```bash
[vm] sudo mount -t 9p -o trans=virtio shared /mnt/shared
```

Pour un montage automatique au démarrage, ajoutez dans `/etc/fstab` :

```
shared /mnt/shared 9p trans=virtio,version=9p2000.L,rw 0 0
```

### UTM

UTM propose le partage via VirtFS. Dans les paramètres de la VM, onglet **Partage**, ajoutez un répertoire. Le montage dans la VM est identique à celui de QEMU (mount 9p).

> ⚠️ **Partie VI (malware)** : lors de l'analyse de code malveillant, **désactivez les dossiers partagés**. Un malware dans la VM pourrait théoriquement écrire sur le système de fichiers partagé et affecter l'hôte. Ce risque est faible avec nos samples pédagogiques, mais l'hygiène de sécurité exige de supprimer ce vecteur.

---

## Checklist post-création

Avant de passer à la suite, vérifiez que votre VM coche toutes les cases :

- [ ] Le système Ubuntu 24.04 LTS démarre correctement.  
- [ ] L'écran se redimensionne dynamiquement (Guest Additions / SPICE agent installé).  
- [ ] Le presse-papiers partagé fonctionne (copier un texte sur l'hôte, coller dans la VM).  
- [ ] L'accès SSH depuis l'hôte fonctionne (`ssh re-lab`).  
- [ ] Internet est accessible dans la VM (`ping 8.8.8.8` et `curl https://example.com`).  
- [ ] Un snapshot `base-install` a été créé.  
- [ ] La VM dispose d'au moins 4 Go de RAM, 2 vCPU et 60 Go de disque.

Si tout est au vert, vous êtes prêt à installer les outils (section 4.2, si ce n'est pas déjà fait) puis à configurer le réseau pour les phases d'analyse (section 4.4).

---

## Résumé

- **VirtualBox** est le choix le plus simple et le plus portable — c'est notre recommandation par défaut si vous êtes sur Windows ou macOS Intel.  
- **QEMU/KVM** offre les meilleures performances sur un hôte Linux grâce à l'accélération matérielle KVM.  
- **UTM** est la meilleure option sur macOS Apple Silicon, en mode **émulation x86-64** — plus lent mais pleinement compatible avec les binaires de la formation.  
- Quel que soit l'hyperviseur, l'essentiel est de travailler dans un environnement **isolé**, **snapshottable** et **reproductible**. La VM est votre filet de sécurité.

---


⏭️ [Configuration réseau de la VM : NAT, host-only, isolation](/04-environnement-travail/04-configuration-reseau-vm.md)
