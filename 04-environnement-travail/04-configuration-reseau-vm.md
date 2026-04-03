🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 4.4 — Configuration réseau de la VM : NAT, host-only, isolation

> 🎯 **Objectif de cette section** : comprendre les différents modes réseau disponibles pour votre VM, savoir lequel utiliser selon la phase de travail, et configurer les interfaces nécessaires pour pouvoir basculer d'un mode à l'autre en quelques secondes.

---

## Le réseau, un vecteur à maîtriser

Le réseau est à la fois un outil et un risque en reverse engineering. Un outil, parce que certains binaires d'entraînement communiquent sur le réseau (chapitre 23 — client/serveur, chapitre 28 — dropper avec protocole C2). Un risque, parce qu'un binaire malveillant exécuté dans la VM pourrait tenter de contacter un serveur externe, d'exfiltrer des données, ou de se propager.

La stratégie est simple : **on donne à la VM exactement le niveau d'accès réseau dont elle a besoin, et pas un octet de plus.** Pour cela, nous configurons plusieurs interfaces réseau et basculons entre elles selon le contexte.

---

## Les trois modes réseau

### NAT — accès à Internet, pas d'accès direct depuis l'hôte

En mode NAT (Network Address Translation), la VM accède à Internet à travers l'hôte, qui agit comme un routeur. La VM obtient une adresse IP privée (typiquement 10.0.2.x pour VirtualBox, 192.168.122.x pour QEMU/KVM) et peut naviguer sur le web, télécharger des paquets, cloner des dépôts Git.

En revanche, l'hôte et le réseau local ne peuvent pas initier de connexion vers la VM — sauf si vous configurez des règles de redirection de ports (port forwarding), comme nous l'avons fait pour SSH en section 4.3.

```
┌─────────────┐       NAT          ┌──────────────┐       ┌──────────┐
│     VM      │ ──── (10.0.2.x) ──→│    Hôte      │ ────→ │ Internet │
│  RE-Lab     │ ←── réponses ──────│  (routeur)   │ ←──── │          │
└─────────────┘                    └──────────────┘       └──────────┘
                                          ↑
                            Pas de connexion entrante
                            (sauf port forwarding SSH)
```

**Quand l'utiliser :**

- Pendant l'installation du système et des outils (sections 4.2–4.3) — vous avez besoin d'`apt`, `pip`, `wget`.  
- Lorsque vous travaillez sur des chapitres qui n'impliquent pas d'exécution de binaires suspects.  
- Pour mettre à jour la VM ponctuellement.

**Risque résiduel** : si vous exécutez un binaire malveillant en mode NAT, ce binaire a accès à Internet. Il pourrait théoriquement contacter un serveur de commande, télécharger un payload ou exfiltrer des données. C'est exactement ce que nous voulons éviter pour la Partie VI.

### Host-only — communication hôte ↔ VM, pas d'Internet

En mode host-only (réseau privé hôte), la VM et l'hôte sont connectés sur un réseau privé virtuel. La VM peut communiquer avec l'hôte (et d'autres VM sur le même réseau host-only), mais elle n'a **aucun accès à Internet**.

```
┌─────────────┐    host-only     ┌──────────────┐
│     VM      │ ← 192.168.56.x ─→│    Hôte      │       ✗ Internet
│  RE-Lab     │                  │              │
└─────────────┘                  └──────────────┘
       ↕
  Autres VM sur
  le même réseau
```

**Quand l'utiliser :**

- Pendant les chapitres d'analyse dynamique (Partie III) lorsque vous exécutez et instrumentez des binaires d'entraînement.  
- Pour les exercices réseau du chapitre 23 : le client et le serveur tournent dans la VM (ou entre deux VM), le trafic reste confiné.  
- Lorsque vous souhaitez capturer le trafic réseau de la VM avec Wireshark ou `tcpdump` sur l'hôte.

**Avantage clé** : un binaire exécuté dans la VM ne peut pas atteindre Internet. Même s'il tente de se connecter à un serveur C2, la connexion échouera silencieusement ou sera refusée.

### Réseau interne / isolé — aucune communication avec l'hôte ni Internet

En mode réseau interne (VirtualBox) ou réseau isolé (QEMU/KVM), la VM ne peut communiquer qu'avec d'autres VM rattachées au même réseau virtuel. L'hôte lui-même n'est pas joignable.

```
┌─────────────┐   réseau isolé   ┌─────────────┐
│    VM 1     │ ← 10.99.0.x ───→ │    VM 2     │       ✗ Internet
│  (malware)  │                  │  (monitor)  │       ✗ Hôte
└─────────────┘                  └─────────────┘
```

**Quand l'utiliser :**

- Partie VI (analyse de code malveillant) — c'est le mode le plus sûr pour exécuter les samples ransomware et dropper.  
- Chapitre 28 (dropper avec communication réseau) : vous pouvez simuler un faux serveur C2 sur une seconde VM connectée au même réseau isolé, tout en gardant l'ensemble coupé du monde extérieur.

**C'est le niveau d'isolation maximum.** Même si la VM est compromise, le malware ne peut atteindre ni l'hôte, ni Internet, ni le réseau local.

---

## Tableau récapitulatif

| Mode | Internet | Accès hôte → VM | Accès VM → hôte | Accès inter-VM | Usage principal |  
|---|---|---|---|---|---|  
| **NAT** | Oui | Via port forwarding | Non | Non | Installation, mises à jour |  
| **Host-only** | Non | Oui (réseau privé) | Oui (réseau privé) | Oui (même réseau) | Analyse dynamique, exercices réseau |  
| **Isolé / interne** | Non | Non | Non | Oui (même réseau) | Analyse de malware (Partie VI) |

---

## Configuration par hyperviseur

### VirtualBox

VirtualBox permet d'attacher jusqu'à quatre adaptateurs réseau à une VM. Nous en configurons deux pour pouvoir basculer facilement :

**Adaptateur 1 — NAT (activé par défaut)**

Dans **Paramètres → Réseau → Adaptateur 1** :

- Mode d'accès réseau : **NAT**  
- Type d'adaptateur : Intel PRO/1000 MT Desktop (ou Virtio pour de meilleures performances si les drivers sont installés dans la VM)

Ce sera l'interface utilisée pour les installations et mises à jour. Vous pouvez la désactiver (décocher « Activer la carte réseau ») lorsque vous n'avez pas besoin d'Internet.

> 💡 **Règle de port forwarding SSH** (si pas déjà fait en section 4.3) : dans **Avancé → Redirection de ports**, ajoutez une règle TCP port hôte 2222 → port invité 22.

**Adaptateur 2 — Host-only**

Avant de configurer l'adaptateur, créez le réseau host-only dans VirtualBox :

1. **Fichier → Gestionnaire de réseau hôte** (ou **Outils → Réseau** dans les versions récentes).  
2. Cliquez sur **Créer**. Un réseau `vboxnet0` apparaît avec une plage par défaut (typiquement 192.168.56.0/24).  
3. Vérifiez que le **serveur DHCP** est activé (onglet Serveur DHCP) — cela simplifie l'attribution d'adresses IP dans la VM.

Puis dans **Paramètres → Réseau → Adaptateur 2** :

- Cochez **Activer la carte réseau**.  
- Mode d'accès réseau : **Réseau privé hôte (Host-Only)**.  
- Nom : `vboxnet0`.

**Réseau interne (pour la Partie VI)**

Lorsque vous aborderez l'analyse de malware, vous remplacerez temporairement le mode host-only par un **réseau interne** :

- Mode d'accès réseau : **Réseau interne**.  
- Nom : `malware-lab` (nommez-le comme vous voulez — les VM sur le même nom de réseau interne se verront).

Ce réseau n'a pas de serveur DHCP par défaut. Vous devrez configurer les adresses IP manuellement dans les VM (voir plus bas).

### QEMU/KVM (avec libvirt)

Libvirt gère les réseaux virtuels via des fichiers XML. Trois réseaux nous intéressent.

**Réseau `default` (NAT) — déjà présent**

Libvirt crée automatiquement un réseau `default` en mode NAT (interface `virbr0`, plage 192.168.122.0/24). Vérifiez qu'il est actif :

```bash
[hôte] virsh net-list --all
# Devrait afficher : default   active   yes   yes
```

S'il n'est pas actif :

```bash
[hôte] virsh net-start default
[hôte] virsh net-autostart default
```

**Réseau host-only — à créer**

Créez un fichier de définition `hostonly.xml` :

```xml
<network>
  <name>hostonly</name>
  <bridge name="virbr1" />
  <ip address="192.168.100.1" netmask="255.255.255.0">
    <dhcp>
      <range start="192.168.100.100" end="192.168.100.200" />
    </dhcp>
  </ip>
</network>
```

Notez l'absence de balise `<forward>` — c'est ce qui empêche le trafic de sortir vers l'extérieur.

```bash
[hôte] virsh net-define hostonly.xml
[hôte] virsh net-start hostonly
[hôte] virsh net-autostart hostonly
```

Ajoutez une seconde interface réseau à la VM via `virt-manager` : **Ajouter un matériel → Réseau → Source réseau : hostonly**.

**Réseau isolé — à créer pour la Partie VI**

```xml
<network>
  <name>isolated-malware</name>
  <bridge name="virbr2" />
  <ip address="10.99.0.1" netmask="255.255.255.0">
    <dhcp>
      <range start="10.99.0.100" end="10.99.0.200" />
    </dhcp>
  </ip>
</network>
```

Même principe — pas de `<forward>`, donc aucune route vers l'extérieur. Les VM connectées à ce réseau ne pourront communiquer qu'entre elles.

```bash
[hôte] virsh net-define isolated-malware.xml
[hôte] virsh net-start isolated-malware
```

> 💡 Nous n'activons pas `net-autostart` pour le réseau isolé : vous le démarrerez manuellement lorsque vous en aurez besoin (Partie VI).

### UTM (macOS)

UTM propose les modes réseau dans les paramètres de chaque VM, onglet **Réseau** :

- **Partagé (Shared Network)** : équivalent du NAT. La VM accède à Internet via l'hôte. C'est le mode par défaut.  
- **Host Only** : réseau privé entre l'hôte et la VM, sans accès Internet.  
- **Aucun (None)** : aucune connectivité réseau. La VM est complètement isolée.

UTM ne supporte pas nativement les réseaux internes multi-VM comme VirtualBox. Pour simuler un réseau isolé entre deux VM, vous pouvez utiliser un réseau host-only partagé et ajouter des règles de pare-feu macOS pour bloquer le forwarding, ou simplement mettre les deux VM en mode host-only (elles se verront sur le même sous-réseau).

**Configuration recommandée pour cette formation :**

| Phase | Mode UTM |  
|---|---|  
| Installation, mises à jour | Shared Network |  
| Chapitres 1–25 (apprentissage) | Shared Network ou Host Only |  
| Partie VI (malware) | Host Only strict ou None |

---

## Configuration des interfaces dans la VM

Avec deux adaptateurs réseau configurés, la VM verra deux interfaces (typiquement `enp0s3` et `enp0s8` sous VirtualBox, ou `ens3` et `ens4` sous QEMU/KVM). Ubuntu utilise Netplan pour la configuration réseau.

Vérifiez les interfaces détectées :

```bash
[vm] ip link show
```

Si la seconde interface (host-only) n'a pas d'adresse IP, c'est que le DHCP n'a pas été activé sur celle-ci. Éditez la configuration Netplan :

```bash
[vm] sudo nano /etc/netplan/01-netcfg.yaml
```

Exemple de configuration pour deux interfaces, la première en DHCP (NAT) et la seconde en DHCP (host-only) :

```yaml
network:
  version: 2
  ethernets:
    enp0s3:
      dhcp4: true
    enp0s8:
      dhcp4: true
```

Appliquez la configuration :

```bash
[vm] sudo netplan apply
[vm] ip addr show
```

Vous devriez voir une adresse IP sur chaque interface — par exemple 10.0.2.15 (NAT) et 192.168.56.101 (host-only).

> 💡 **Astuce** : les noms d'interfaces (`enp0s3`, `ens3`, `eth0`…) varient selon l'hyperviseur et le type d'adaptateur virtuel. Utilisez `ip link show` pour identifier les vôtres.

---

## Basculer entre les modes réseau

La bascule entre les modes se fait **sans redémarrer la VM** dans la plupart des cas.

### Méthode rapide : activer/désactiver les interfaces dans la VM

Pour couper l'accès Internet tout en gardant le réseau host-only :

```bash
[vm] sudo ip link set enp0s3 down    # désactive l'interface NAT
```

Pour le réactiver :

```bash
[vm] sudo ip link set enp0s3 up
[vm] sudo dhclient enp0s3             # redemande une IP via DHCP
```

### Méthode côté hyperviseur

**VirtualBox** : dans le menu de la VM en cours d'exécution, **Périphériques → Réseau → Adaptateur 1 → Connecter/Déconnecter le câble réseau**. L'effet est instantané — c'est comme débrancher un câble Ethernet virtuel.

**QEMU/KVM (virsh)** :

```bash
[hôte] virsh domif-setlink RE-Lab enp0s3 down    # déconnecte l'interface NAT
[hôte] virsh domif-setlink RE-Lab enp0s3 up      # reconnecte
```

**UTM** : changez le mode réseau dans les paramètres de la VM. Un redémarrage de la VM peut être nécessaire.

---

## Workflow réseau recommandé par phase de la formation

| Phase | Interface NAT | Interface Host-only | Réseau isolé | Justification |  
|---|---|---|---|---|  
| **Installation** (4.2–4.3) | Active | Inactive | — | Besoin d'Internet pour `apt`/`pip`/téléchargements |  
| **Parties I–II** (chap. 1–10) | Active | Active | — | Pas de risque, analyse statique uniquement |  
| **Partie III** (chap. 11–15) | **Désactivée** | Active | — | Exécution et instrumentation de binaires — couper Internet par précaution |  
| **Parties IV–V** (chap. 16–25) | Au cas par cas | Active | — | Désactiver NAT pendant l'exécution de binaires |  
| **Partie VI** (chap. 26–29) | **Désactivée** | **Désactivée** | **Active** | Isolation maximale pour l'analyse de malware |  
| **Parties VII–IX** (chap. 30–36) | Active | Active | — | Pas de risque significatif |

> 📌 **Règle simple** : si vous êtes sur le point d'exécuter un binaire et que vous n'avez pas besoin d'Internet pour cet exercice, **coupez l'interface NAT**. C'est un geste de 5 secondes qui élimine une catégorie entière de risques.

---

## Vérification de l'isolation

Après avoir configuré vos interfaces, vérifiez que le comportement est conforme à vos attentes.

**Test d'accès Internet :**

```bash
[vm] ping -c 3 8.8.8.8
```

- Si l'interface NAT est active → réponses reçues.  
- Si l'interface NAT est désactivée → `Network is unreachable` ou pas de réponse. C'est le comportement attendu.

**Test de connectivité hôte ↔ VM (host-only) :**

```bash
# Depuis la VM, pingez l'hôte (adresse du réseau host-only)
[vm] ping -c 3 192.168.56.1

# Depuis l'hôte, pingez la VM
[hôte] ping -c 3 192.168.56.101
```

**Test d'isolation complète (réseau interne/isolé) :**

```bash
[vm] ping -c 3 8.8.8.8            # doit échouer
[vm] ping -c 3 192.168.56.1       # doit échouer (hôte non joignable)
[vm] ping -c 3 10.99.0.101        # doit réussir (autre VM sur le même réseau isolé)
```

---

## Capture de trafic réseau

Pouvoir capturer le trafic réseau de la VM est essentiel pour le chapitre 23 (protocole custom) et la Partie VI (analyse de malware).

### Depuis la VM (le plus simple)

```bash
[vm] sudo tcpdump -i enp0s8 -w /tmp/capture.pcap
```

Ouvrez ensuite `/tmp/capture.pcap` dans Wireshark (dans la VM) ou transférez-le vers l'hôte via le dossier partagé ou SCP.

### Depuis l'hôte (sur l'interface bridge)

Sur un hôte Linux avec QEMU/KVM, vous pouvez capturer directement sur le bridge virtuel :

```bash
[hôte] sudo tcpdump -i virbr1 -w ~/capture-hostonly.pcap
```

Avec VirtualBox, la capture est possible via la commande `VBoxManage` :

```bash
[hôte] VBoxManage modifyvm "RE-Lab" --nictrace2 on --nictracefile2 ~/capture.pcap
```

> 📌 **Chapitres concernés** : 23 (analyse de protocole réseau avec Wireshark), 26 (lab sécurisé), 28 (dropper réseau).

---

## Résumé

- Configurez **deux interfaces réseau** sur votre VM : une NAT (Internet) et une host-only (réseau privé).  
- **Basculez entre les modes** selon la phase de travail en activant/désactivant les interfaces — pas besoin de redémarrer.  
- Pour la **Partie VI (malware)**, utilisez un réseau isolé/interne sans aucun accès à l'hôte ni à Internet.  
- La règle d'or : **coupez l'interface NAT avant d'exécuter un binaire suspect.** Restaurer Internet prend 5 secondes ; réparer une exfiltration de données est autrement plus compliqué.  
- Vérifiez systématiquement votre isolation avec des `ping` avant de commencer une session d'analyse dynamique.

---


⏭️ [Structure du dépôt : organisation de `binaries/` et des `Makefile` par chapitre](/04-environnement-travail/05-structure-depot.md)
