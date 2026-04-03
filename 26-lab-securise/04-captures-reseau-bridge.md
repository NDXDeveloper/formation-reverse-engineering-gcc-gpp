🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 26.4 — Captures réseau avec un bridge dédié

> **Chapitre 26 — Mise en place d'un lab d'analyse sécurisé**  
> **Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**

---

## Objectif de cette section

En section 26.2, nous avons créé le réseau isolé `isolated-malware` et son bridge `br-malware`. En section 26.3, nous avons vu comment lancer `tcpdump` pour capturer le trafic. Cette section va plus loin. Elle détaille l'architecture réseau du bridge, explique comment la configurer pour maximiser la visibilité sur l'activité du malware, et présente les techniques d'analyse des captures qui seront indispensables dans les chapitres 27 (ransomware) et 28 (dropper avec communication C2).

Capturer du trafic réseau sur un réseau isolé peut sembler paradoxal — rien ne sort, rien n'entre, à quoi bon ? En réalité, les paquets que le malware **tente** d'émettre sont aussi révélateurs que ceux qu'il réussit à envoyer. Une requête DNS vers `evil-c2.example.com` qui reste sans réponse trahit le domaine C2. Un SYN vers `185.x.x.x:4443` qui expire révèle l'adresse et le port du serveur de commande. Un paquet UDP contenant un blob binaire encodé dévoile le format du protocole d'exfiltration. Toutes ces informations sont capturables sur un réseau qui ne mène nulle part.

---

## Anatomie du bridge `br-malware`

Un bridge Linux est un commutateur réseau virtuel qui opère au niveau 2 (couche liaison de données). Il relie entre elles les interfaces réseau virtuelles qui y sont raccordées, exactement comme un switch physique relie des câbles Ethernet. Quand libvirt crée le réseau `isolated-malware`, il met en place l'architecture suivante :

```
                    MACHINE HÔTE
    ┌──────────────────────────────────────────┐
    │                                          │
    │   br-malware (10.66.66.1/24)             │
    │   ├── vnet0 ←──── interface virtuelle    │
    │   │                rattachée à la VM     │
    │   │                                      │
    │   └── (aucune interface physique)        │
    │                                          │
    │   La VM voit une interface enp1s0        │
    │   qui obtient 10.66.66.100 via DHCP      │
    │                                          │
    │   Pas de masquerade, pas de forwarding,  │
    │   pas de route par défaut sur ce bridge  │
    │                                          │
    └──────────────────────────────────────────┘
```

L'hôte possède une adresse IP sur le bridge (`10.66.66.1`) parce que libvirt le configure pour servir de serveur DHCP (via `dnsmasq`). Cette adresse permet la communication bidirectionnelle hôte ↔ VM (pour `scp`, `ssh`), mais **aucune route** ne relie le bridge à l'interface réseau externe de l'hôte (`eth0`, `wlan0`…). Les paquets envoyés par la VM vers une adresse hors du sous-réseau `10.66.66.0/24` arrivent sur le bridge, n'ont nulle part où aller, et sont silencieusement abandonnés par la pile réseau de l'hôte.

C'est précisément ce comportement que nous exploitons : le bridge est un cul-de-sac réseau, mais un cul-de-sac sur lequel nous avons un micro.

### Vérifier la topologie du bridge

```bash
# Voir les interfaces rattachées au bridge
bridge link show br-malware

# Ou avec ip
ip link show master br-malware

# Voir la configuration IP du bridge
ip addr show br-malware
```

Vous devriez voir `br-malware` avec l'adresse `10.66.66.1/24` et une interface `vnetX` rattachée (l'interface virtuelle côté hôte du câble réseau virtuel de la VM).

### Vérifier l'absence de routage

```bash
# Vérifier qu'aucune route ne sort du bridge
ip route show dev br-malware
# Attendu : uniquement "10.66.66.0/24 proto kernel scope link src 10.66.66.1"

# Vérifier que le forwarding est bloqué par iptables
sudo iptables -L FORWARD -v -n | grep br-malware
# Attendu : règles DROP en entrée et sortie
```

Si `ip route` montre une route par défaut ou une route vers un autre sous-réseau via `br-malware`, il y a un problème de configuration. Ne poursuivez pas avant de l'avoir corrigé.

---

## Point de capture : hôte vs VM

Nous avons abordé cette question en section 26.3. Approfondissons ici les implications techniques de chaque approche.

### Capture sur l'hôte (recommandé)

Quand `tcpdump` écoute l'interface `br-malware` sur l'hôte, il se place au niveau du bridge lui-même. Il voit **tout le trafic** qui transite par le bridge : les paquets émis par la VM, les paquets émis par l'hôte vers la VM (réponses DHCP, par exemple), et les paquets ARP de résolution d'adresse.

```bash
# Capture complète sur le bridge
sudo tcpdump -i br-malware -s 0 -w capture.pcap --print
```

Avantages :

- **Invisibilité** — le sample ne peut pas détecter la capture. Il n'y a aucun processus `tcpdump` dans la VM, aucun fichier `.pcap` en cours d'écriture dans le système de fichiers invité, aucune interface en mode promiscuous visible depuis l'intérieur.  
- **Intégrité** — même si le malware corrompt la VM (efface des fichiers, kernel panic), le fichier `.pcap` est en sécurité sur l'hôte.  
- **Pas de surcharge dans la VM** — les ressources CPU et I/O de la VM restent entièrement disponibles pour le sample et les autres outils de monitoring.

Inconvénient :

- Le trafic interne à la VM (loopback `127.0.0.1`) n'est pas visible sur le bridge. Si le sample communique avec un autre processus via localhost, cette communication n'apparaîtra pas dans la capture. Pour ce cas de figure, une capture complémentaire dans la VM sur l'interface `lo` peut être nécessaire.

### Capture dans la VM (complémentaire)

Utile dans deux situations spécifiques : capturer le trafic loopback, ou observer le trafic du point de vue exact du sample (avant toute transformation par le bridge).

```bash
# Dans la VM — capture loopback
sudo tcpdump -i lo -s 0 -w ~/captures/loopback.pcap --print

# Dans la VM — capture de l'interface réseau
sudo tcpdump -i enp1s0 -s 0 -w ~/captures/vm_capture.pcap --print
```

Cette capture est vulnérable aux actions du malware (suppression, corruption), et elle crée un processus `tcpdump` détectable. Réservez-la aux cas où le loopback est pertinent.

---

## Capturer intelligemment : filtres de capture vs filtres d'affichage

`tcpdump` permet de filtrer le trafic à deux niveaux. Comprendre la différence est essentiel pour ne pas perdre d'informations critiques.

### Filtres de capture (BPF)

Les filtres de capture sont appliqués **au moment de l'enregistrement**. Les paquets qui ne correspondent pas au filtre ne sont jamais écrits dans le fichier `.pcap`. Ce qui est filtré est **perdu définitivement**.

```bash
# Capturer uniquement le trafic TCP
sudo tcpdump -i br-malware -s 0 -w capture_tcp.pcap tcp

# Capturer uniquement le trafic DNS
sudo tcpdump -i br-malware -s 0 -w capture_dns.pcap port 53

# Capturer uniquement le trafic provenant de la VM
sudo tcpdump -i br-malware -s 0 -w capture_vm.pcap src host 10.66.66.100
```

**Règle pour l'analyse de malware : ne filtrez pas à la capture.** Vous ne savez pas encore quel protocole ou quel port le sample utilisera. Un C2 peut communiquer sur le port 443, 8080, 53, ou un port totalement exotique. Capturez tout (`-s 0`, pas de filtre BPF) et filtrez ensuite à l'analyse.

La seule exception raisonnable est d'exclure le trafic SSH si vous êtes connecté à la VM via SSH à travers le bridge, car ce trafic d'administration polluerait la capture :

```bash
# Capturer tout SAUF le SSH d'administration
sudo tcpdump -i br-malware -s 0 -w capture.pcap 'not port 22'
```

### Filtres d'affichage (post-capture)

Les filtres d'affichage s'appliquent à la relecture du fichier `.pcap`. Le fichier original reste intact — vous filtrez uniquement ce que vous voyez.

Avec `tcpdump` en relecture :

```bash
# Relire en filtrant les requêtes DNS
tcpdump -r capture.pcap -n port 53

# Relire en filtrant le trafic HTTP
tcpdump -r capture.pcap -A port 80

# Relire en filtrant les SYN (tentatives de connexion)
tcpdump -r capture.pcap 'tcp[tcpflags] & tcp-syn != 0 and tcp[tcpflags] & tcp-ack == 0'
```

Avec Wireshark (filtres d'affichage, syntaxe différente) :

```
dns  
http  
tcp.flags.syn == 1 && tcp.flags.ack == 0  
ip.dst != 10.66.66.1  
tcp.port == 4443  
frame contains "RANSOM"  
```

Wireshark offre une ergonomie incomparable pour l'analyse de protocoles complexes. Transférez toujours le `.pcap` sur l'hôte pour l'examiner dans Wireshark une fois la capture terminée.

---

## Simuler un environnement réseau réaliste (optionnel)

Certains malwares ne révèlent leur comportement complet que s'ils réussissent à communiquer. Un dropper qui ne reçoit pas de réponse de son serveur C2 peut rester dormant. Un ransomware qui doit récupérer sa clé de chiffrement depuis un serveur ne chiffrera rien si la connexion échoue. Dans ces cas, un réseau complètement muet peut limiter l'observabilité.

Deux techniques permettent de simuler un environnement réseau minimal tout en maintenant l'isolation.

### Technique 1 — Répondeur DNS avec `dnsmasq`

Le serveur `dnsmasq` fourni par libvirt pour le DHCP peut aussi servir de résolveur DNS. Configuré correctement, il peut répondre à toutes les requêtes DNS en renvoyant une adresse IP sous notre contrôle (typiquement l'adresse de l'hôte sur le bridge, `10.66.66.1`).

Modifiez la configuration du réseau isolé pour activer le DNS avec résolution universelle :

```bash
# Créer un fichier de configuration dnsmasq additionnel
sudo mkdir -p /etc/dnsmasq.d  
sudo tee /etc/dnsmasq.d/malware-lab.conf << 'EOF'  
# Répondre à TOUTES les requêtes DNS avec l'IP de l'hôte sur le bridge
# Le malware croit résoudre ses domaines C2, mais tout pointe vers nous
address=/#/10.66.66.1  
EOF  
```

Relancez le réseau libvirt pour appliquer :

```bash
virsh net-destroy isolated-malware  
virsh net-start isolated-malware  
```

Désormais, quand le malware fait une requête DNS pour `evil-c2.example.com`, il reçoit `10.66.66.1` comme réponse. Il tentera ensuite de se connecter à cette adresse — où nous pouvons avoir un faux serveur en écoute.

> ⚠️ **Cette technique rend le réseau « semi-isolé »** — la VM peut résoudre des noms et se connecter à l'hôte. Les données ne quittent toujours pas le bridge, mais le malware dispose d'un interlocuteur réseau. Activez cette technique uniquement quand le comportement du sample l'exige (chapitre 28 notamment), et désactivez-la pour les analyses où l'isolation totale est préférée (chapitre 27).

### Technique 2 — `INetSim` : simulateur de services Internet

`INetSim` est un outil conçu spécifiquement pour l'analyse de malware. Il simule une dizaine de services Internet courants (HTTP, HTTPS, DNS, FTP, SMTP, IRC…) sur une seule machine. Le malware croit communiquer avec de vrais serveurs, alors qu'il parle à un simulateur qui enregistre chaque échange.

Installation sur l'hôte (ou dans une VM de services dédiée) :

```bash
sudo apt install inetsim
```

Configuration pour écouter sur le bridge :

```bash
sudo tee /etc/inetsim/inetsim.conf << 'EOF'
# Écouter uniquement sur l'interface du bridge
service_bind_address 10.66.66.1

# DNS : résoudre tout vers nous
dns_default_ip 10.66.66.1

# Services à activer
start_service dns  
start_service http  
start_service https  
start_service smtp  
start_service ftp  

# Répertoire de logging
report_dir /var/log/inetsim/report

# Répertoire pour les fichiers servis en HTTP/FTP
data_dir /var/lib/inetsim  
EOF  
```

Lancement :

```bash
sudo inetsim
```

`INetSim` va répondre aux requêtes HTTP avec une page par défaut, accepter les connexions SMTP et enregistrer les emails que le malware tente d'envoyer, servir des fichiers bidons en FTP, etc. Chaque interaction est journalisée dans `/var/log/inetsim/report/`.

> 💡 **Dans le cadre de cette formation**, `INetSim` sera utilisé principalement au chapitre 28 (dropper avec communication C2) pour permettre au sample de compléter son handshake et révéler son comportement complet. Au chapitre 27 (ransomware), le sample n'a pas besoin de réseau pour fonctionner — l'isolation totale est donc maintenue.

### Technique 3 — Faux serveur C2 artisanal

Pour les cas où `INetSim` ne suffit pas (protocole custom non-HTTP), on peut écrire un faux serveur en Python qui répond au protocole spécifique du malware. C'est précisément l'objet du chapitre 28 (section 28.4 — Simuler un serveur C2). À ce stade, retenez simplement que le bridge isolé permet de faire tourner un serveur sur `10.66.66.1` qui sera accessible par la VM sans qu'aucun trafic ne quitte la machine physique.

---

## Rotation et dimensionnement des captures

Une capture `tcpdump` sans filtre ni limite de taille peut croître rapidement, surtout si le malware génère du trafic en boucle (beacon C2 périodique, scan réseau). Sur une session de 30 minutes, un sample bavard peut produire plusieurs centaines de Mo de `.pcap`.

### Rotation par taille de fichier

`tcpdump` peut découper la capture en fichiers successifs quand un seuil de taille est atteint :

```bash
# Rotation tous les 100 Mo, conservation de 10 fichiers maximum
sudo tcpdump -i br-malware -s 0 \
  -w capture_%Y%m%d_%H%M%S.pcap \
  -C 100 \
  -W 10 \
  --print
```

- `-C 100` — crée un nouveau fichier tous les 100 Mo (la valeur est en millions d'octets).  
- `-W 10` — conserve au maximum 10 fichiers (les plus anciens sont écrasés).

### Rotation par durée

```bash
# Rotation toutes les 5 minutes
sudo tcpdump -i br-malware -s 0 \
  -w capture_%Y%m%d_%H%M%S.pcap \
  -G 300 \
  --print
```

- `-G 300` — crée un nouveau fichier toutes les 300 secondes (5 minutes).

### Estimation du volume

Pour dimensionner vos captures, quelques repères :

| Comportement du sample | Volume estimé (30 min) |  
|---|---|  
| Pas d'activité réseau (ransomware local) | < 1 Ko (uniquement ARP/DHCP) |  
| Beacon C2 périodique (toutes les 30 s) | 1 – 10 Mo |  
| Scan réseau actif (SYN sur plage IP) | 50 – 500 Mo |  
| Exfiltration de données (upload continu) | 100 Mo – plusieurs Go |

Pour les sessions courtes et nos samples pédagogiques, la capture monolithique (un seul fichier, pas de rotation) est largement suffisante. La rotation devient pertinente lors d'analyses prolongées ou sur des samples très bavards.

---

## Analyser une capture : méthodologie en 5 étapes

Une fois la capture terminée et récupérée sur l'hôte, l'analyse suit un processus structuré. Ce processus sera appliqué concrètement dans les chapitres 27 et 28.

### Étape 1 — Vue d'ensemble statistique

Avant d'ouvrir Wireshark, prenez la mesure de la capture :

```bash
# Statistiques générales
capinfos capture.pcap

# Nombre de paquets
tcpdump -r capture.pcap | wc -l

# Répartition par protocole (avec tshark, le Wireshark CLI)
tshark -r capture.pcap -q -z io,phs
```

La commande `tshark -z io,phs` (Protocol Hierarchy Statistics) affiche la répartition du trafic par protocole. C'est l'équivalent du menu Statistics → Protocol Hierarchy dans Wireshark. Cette vue vous indique immédiatement si le sample a généré du trafic DNS, HTTP, TCP brut, UDP, ICMP, etc.

### Étape 2 — Identifier les endpoints

Quelles adresses IP le sample a-t-il tenté de contacter ?

```bash
# Conversations IP (source → destination)
tshark -r capture.pcap -q -z conv,ip

# Endpoints uniques
tshark -r capture.pcap -q -z endpoints,ip
```

Dans notre lab, la VM a l'adresse `10.66.66.100` et l'hôte `10.66.66.1`. Toute adresse de destination qui n'est **ni l'une ni l'autre** est une adresse que le malware a tenté de joindre à l'extérieur — c'est un IOC (Indicator of Compromise) potentiel.

### Étape 3 — Extraire les requêtes DNS

Les requêtes DNS sont souvent le premier geste réseau d'un malware : résoudre le nom de domaine de son serveur C2.

```bash
# Lister toutes les requêtes DNS
tshark -r capture.pcap -Y "dns.flags.response == 0" \
  -T fields -e dns.qry.name | sort -u
```

Cette commande extrait les noms de domaine que le sample a tenté de résoudre. Chacun de ces noms est un IOC de premier ordre.

### Étape 4 — Examiner les connexions TCP

```bash
# Lister les SYN sortants (tentatives de connexion initiées par la VM)
tshark -r capture.pcap -Y "tcp.flags.syn == 1 && tcp.flags.ack == 0 && ip.src == 10.66.66.100" \
  -T fields -e ip.dst -e tcp.dstport | sort -u
```

Chaque couple `(IP destination, port destination)` représente un service que le malware a tenté de joindre. Les ports courants donnent des indications : 443 (HTTPS/TLS), 80 (HTTP), 4444 (Metasploit par défaut), 8443, 8080 (proxies/C2), 53 (DNS ou DNS tunneling), 6667 (IRC).

### Étape 5 — Examiner les payloads

Pour les connexions qui ont abouti (dans le cas où `INetSim` ou un faux C2 répondait), extrayez le contenu échangé :

```bash
# Suivre un flux TCP spécifique (stream index 0, 1, 2…)
tshark -r capture.pcap -q -z "follow,tcp,ascii,0"

# Exporter tous les objets HTTP récupérés par le sample
tshark -r capture.pcap --export-objects http,./http_objects/

# Rechercher une chaîne dans les payloads
tshark -r capture.pcap -Y 'frame contains "HELLO"' -V
```

Pour les protocoles customs (binaires, non-HTTP), la commande `follow,tcp,raw` exporte le contenu brut en hexadécimal. Ce dump peut ensuite être chargé dans ImHex pour appliquer un pattern `.hexpat` — c'est exactement le workflow que nous suivrons au chapitre 23 (Reverse d'un binaire réseau) et au chapitre 28 (protocole C2).

---

## Intégration avec Wireshark sur l'hôte

Pour l'analyse approfondie des captures, Wireshark offre des fonctionnalités que la ligne de commande ne peut pas égaler : suivi visuel des flux, décodage automatique de centaines de protocoles, graphes temporels, reconstruction de sessions.

### Installation sur l'hôte

```bash
# Sur l'hôte (pas dans la VM)
sudo apt install wireshark tshark
```

### Profil dédié à l'analyse de malware

Créez un profil Wireshark dédié (Edit → Configuration Profiles → New) avec les colonnes suivantes, optimisées pour l'analyse de trafic malveillant :

| Colonne | Champ | Utilité |  
|---|---|---|  
| Time | `frame.time_relative` | Temps écoulé depuis le début de la capture |  
| Source | `ip.src` | IP source |  
| Destination | `ip.dst` | IP destination |  
| Protocol | `_ws.col.Protocol` | Protocole détecté |  
| Dst Port | `tcp.dstport` ou `udp.dstport` | Port de destination |  
| Length | `frame.len` | Taille du paquet |  
| Info | `_ws.col.Info` | Résumé du paquet |

Ajoutez des règles de coloration pour mettre en évidence :

- En rouge : le trafic DNS vers des domaines inconnus.  
- En orange : les SYN sans réponse (connexions échouées — le sample tente de joindre un C2 inaccessible).  
- En vert : les flux TCP établis (le sample communique avec `INetSim` ou un faux C2).

### Ouvrir la capture

```bash
wireshark ./analyses/session-xxx/capture.pcap &
```

---

## Automatiser l'extraction d'IOC depuis une capture

Après plusieurs analyses, le processus d'extraction d'IOC (Indicators of Compromise) depuis un fichier `.pcap` devient répétitif. Le script suivant automatise les étapes 2 à 4 de notre méthodologie :

```bash
#!/bin/bash
# extract_ioc.sh — Extrait les IOC réseau d'une capture pcap
# Usage : ./extract_ioc.sh capture.pcap

set -euo pipefail

PCAP="${1:?Usage: $0 <fichier.pcap>}"  
VM_IP="10.66.66.100"  
HOST_IP="10.66.66.1"  
OUTPUT="${PCAP%.pcap}_ioc.txt"  

echo "=== IOC extraits de : $PCAP ===" > "$OUTPUT"  
echo "Date : $(date)" >> "$OUTPUT"  
echo "" >> "$OUTPUT"  

# Domaines DNS requêtés
echo "--- Domaines DNS requêtés ---" >> "$OUTPUT"  
tshark -r "$PCAP" -Y "dns.flags.response == 0" \  
  -T fields -e dns.qry.name 2>/dev/null | sort -u >> "$OUTPUT"
echo "" >> "$OUTPUT"

# Adresses IP contactées (hors hôte et VM)
echo "--- Adresses IP de destination (hors lab) ---" >> "$OUTPUT"  
tshark -r "$PCAP" -Y "ip.src == $VM_IP && ip.dst != $HOST_IP" \  
  -T fields -e ip.dst 2>/dev/null | sort -u >> "$OUTPUT"
echo "" >> "$OUTPUT"

# Ports de destination
echo "--- Ports TCP de destination ---" >> "$OUTPUT"  
tshark -r "$PCAP" \  
  -Y "tcp.flags.syn == 1 && tcp.flags.ack == 0 && ip.src == $VM_IP" \
  -T fields -e ip.dst -e tcp.dstport 2>/dev/null | sort -u >> "$OUTPUT"
echo "" >> "$OUTPUT"

echo "--- Ports UDP de destination ---" >> "$OUTPUT"  
tshark -r "$PCAP" -Y "ip.src == $VM_IP && udp" \  
  -T fields -e ip.dst -e udp.dstport 2>/dev/null | sort -u >> "$OUTPUT"
echo "" >> "$OUTPUT"

# Statistiques
echo "--- Statistiques ---" >> "$OUTPUT"  
echo "Paquets totaux : $(tshark -r "$PCAP" 2>/dev/null | wc -l)" >> "$OUTPUT"  
echo "Durée capture  : $(capinfos -u "$PCAP" 2>/dev/null | grep 'Capture duration' | awk -F: '{print $2}')" >> "$OUTPUT"  

echo "[+] IOC extraits dans : $OUTPUT"  
cat "$OUTPUT"  
```

Ce script produit un fichier texte récapitulatif qui peut être joint au rapport d'analyse (chapitre 27, section 27.7).

---

## Scénarios réseau par type de sample

Les trois chapitres suivants présentent des samples aux profils réseau très différents. Voici comment adapter la configuration du bridge à chaque cas :

### Chapitre 27 — Ransomware (activité réseau minimale)

Le ransomware fourni chiffre des fichiers locaux avec une clé hardcodée. Il n'a pas besoin de réseau pour fonctionner. La configuration idéale est l'isolation totale :

- `dnsmasq` en mode DHCP uniquement (pas de résolution DNS universelle).  
- Pas d'`INetSim`.  
- `tcpdump` tourne quand même pour vérifier l'absence de trafic — ou pour détecter un comportement réseau inattendu qui enrichirait l'analyse.

### Chapitre 28 — Dropper avec communication C2

Le dropper tente de contacter un serveur de commande pour recevoir des instructions. Sans réponse, il reste dormant ou n'exécute qu'un comportement partiel. Pour observer son comportement complet :

- Activer la résolution DNS universelle (tout résout vers `10.66.66.1`).  
- Lancer `INetSim` sur l'hôte, ou mieux : écrire un faux serveur C2 adapté au protocole spécifique du sample (c'est l'objet de la section 28.4).  
- `tcpdump` capture l'intégralité des échanges entre le dropper et le faux C2.

### Chapitre 29 — Binaire packé

Le sample packé ne génère normalement aucune activité réseau (le packing est une protection, pas un comportement). L'isolation totale est appropriée. `tcpdump` sert de filet de sécurité pour confirmer l'absence de trafic.

---

## Résumé du pipeline de capture

```
AVANT l'exécution du sample
│
├─ 1. Vérifier l'isolation : ping 8.8.8.8 doit échouer depuis la VM
├─ 2. Optionnel : activer INetSim / dnsmasq universel si le sample a besoin de réseau
├─ 3. Lancer tcpdump sur l'hôte : sudo tcpdump -i br-malware -s 0 -w capture.pcap
│
PENDANT l'exécution
│
├─ 4. Observer tcpdump --print en temps réel (noms DNS, SYN, payloads)
│
APRÈS l'exécution
│
├─ 5. Arrêter tcpdump (Ctrl+C)
├─ 6. Vérification rapide : capinfos + tcpdump -r (vue d'ensemble)
├─ 7. Extraction d'IOC : ./extract_ioc.sh capture.pcap
├─ 8. Analyse approfondie : Wireshark sur l'hôte
└─ 9. Archiver le .pcap avec les autres artefacts de la session
```

---

> 📌 **À retenir** — Le bridge `br-malware` est un piège à paquets. Le malware émet librement, ses paquets sont capturés intégralement, mais ils ne vont nulle part. Quand le sample a besoin d'un interlocuteur réseau pour révéler son comportement, `INetSim` ou un faux serveur C2 sur `10.66.66.1` joue le rôle du monde extérieur sans jamais ouvrir une brèche dans l'isolation. La capture `.pcap` qui en résulte est un artefact aussi précieux que les logs `auditd` ou les observations `inotifywait` — archivez-la systématiquement, elle sera la base de l'analyse de protocole et de l'extraction d'IOC.

⏭️ [Règles d'or : ne jamais exécuter hors sandbox, ne jamais connecter au réseau réel](/26-lab-securise/05-regles-or.md)
