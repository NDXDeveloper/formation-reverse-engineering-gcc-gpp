🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Déployer le lab et vérifier l'isolation réseau

> **Chapitre 26 — Mise en place d'un lab d'analyse sécurisé**  
> **Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**

---

## Objectif

Ce checkpoint valide que votre lab d'analyse est opérationnel et correctement isolé **avant** d'aborder les chapitres 27, 28 et 29. Il ne s'agit pas encore d'analyser un sample — il s'agit de vérifier que l'environnement dans lequel vous le ferez est fiable. C'est la dernière porte à franchir avant de manipuler du code hostile.

À la fin de ce checkpoint, vous devez avoir un lab fonctionnel qui passe l'intégralité des vérifications décrites ci-dessous. Si un seul point échoue, identifiez la cause et corrigez-la avant de poursuivre. Les chapitres suivants **présupposent** que ce checkpoint est validé.

---

## Ce que vous devez avoir en place

Le tableau suivant récapitule chaque composant attendu, la section du chapitre qui le détaille, et le critère de validation.

| Composant | Section de référence | Critère de validation |  
|---|---|---|  
| QEMU/KVM fonctionnel | 26.2 | `kvm-ok` retourne un message de succès |  
| VM `malware-lab` opérationnelle | 26.2 | `virsh list --all` montre la VM |  
| Système Debian/Ubuntu minimal installé | 26.2 | Connexion SSH fonctionnelle vers la VM |  
| Outils RE installés dans la VM | 26.2 | GDB, strace, Frida, pwntools disponibles |  
| VM durcie | 26.2 | Pas de shared folders, pas de `spice-vdagent`, utilisateur `sample-runner` créé |  
| Snapshot `clean-base` | 26.2 | `virsh snapshot-list malware-lab` le montre |  
| Réseau `isolated-malware` actif | 26.2 / 26.4 | `virsh net-list --all` le montre actif |  
| Bridge `br-malware` configuré | 26.4 | `ip addr show br-malware` montre `10.66.66.1/24` |  
| Règles `iptables` de blocage | 26.2 | `iptables -L FORWARD` montre les règles DROP sur `br-malware` |  
| `auditd` installé et configurable | 26.3 | `systemctl status auditd` actif, `auditctl -l` charge des règles |  
| `inotify-tools` installé | 26.3 | `which inotifywait` retourne un chemin |  
| `tcpdump` disponible sur l'hôte | 26.3 / 26.4 | `sudo tcpdump -i br-malware -c 1` ne produit pas d'erreur |  
| `sysdig` installé dans la VM | 26.3 | `sudo sysdig --version` retourne une version |  
| Scripts d'automatisation en place | 26.2 / 26.3 | `prepare_analysis.sh`, `cleanup_analysis.sh`, `start_monitoring.sh` exécutables |

---

## Protocole de vérification

Suivez ce protocole dans l'ordre. Chaque étape dépend du succès de la précédente.

### Phase 1 — Vérification de l'infrastructure de virtualisation

Sur la machine **hôte**, exécutez :

```bash
# KVM est fonctionnel
kvm-ok

# libvirtd tourne
systemctl is-active libvirtd

# La VM existe
virsh list --all | grep malware-lab

# Le snapshot clean-base existe
virsh snapshot-list malware-lab | grep clean-base

# Le réseau isolé est actif
virsh net-list --all | grep isolated-malware
```

**Résultat attendu** : les cinq commandes retournent des résultats positifs. La VM peut être dans l'état `shut off` ou `running`. Le réseau `isolated-malware` doit être `active`.

### Phase 2 — Vérification de l'isolation réseau

Démarrez la VM et basculez-la sur le réseau isolé :

```bash
# Sur l'hôte
virsh start malware-lab
# Attendre le boot complet

# Basculer vers le réseau isolé
virsh detach-interface malware-lab network --current 2>/dev/null || true  
virsh attach-interface malware-lab network isolated-malware --current  
```

Connectez-vous à la VM et exécutez les tests :

```bash
# Dans la VM (ssh analyst@10.66.66.100 ou via virsh console)

# Renouveler le bail DHCP
sudo dhclient -r && sudo dhclient

# Test 1 : la VM a une adresse sur le bon sous-réseau
ip addr show | grep "10.66.66"
# Attendu : une adresse en 10.66.66.x/24

# Test 2 : la VM peut joindre l'hôte sur le bridge
ping -c 3 10.66.66.1
# Attendu : 3 paquets reçus, 0% de perte

# Test 3 : la VM ne peut PAS joindre Internet
ping -c 3 -W 2 8.8.8.8
# Attendu : 100% de perte (timeout)

# Test 4 : la VM ne peut PAS joindre Internet (seconde IP)
ping -c 3 -W 2 1.1.1.1
# Attendu : 100% de perte (timeout)

# Test 5 : la VM ne peut PAS résoudre de noms publics
host example.com
# Attendu : échec de résolution (timeout ou SERVFAIL)

# Test 6 : pas de route par défaut
ip route
# Attendu : uniquement "10.66.66.0/24 dev enp1s0 ..."
# PAS de ligne "default via ..."

# Test 7 : pas de téléchargement possible
curl -m 5 http://example.com
# Attendu : timeout ou erreur de connexion
```

**Résultat attendu** : les tests 1 et 2 réussissent, les tests 3 à 7 échouent. C'est exactement le comportement souhaité : la VM communique avec l'hôte via le bridge, mais elle ne peut atteindre rien au-delà.

Vérifiez également les règles `iptables` sur l'**hôte** :

```bash
# Sur l'hôte
sudo iptables -L FORWARD -v -n | grep br-malware
# Attendu : au moins une règle DROP en entrée et/ou sortie sur br-malware
```

### Phase 3 — Vérification du monitoring

Toujours dans la VM (sur le réseau isolé), vérifiez que chaque outil de monitoring fonctionne.

**`auditd`** :

```bash
# Charger les règles d'analyse
sudo augenrules --load  
sudo auditctl -l  
# Attendu : la liste des règles définies en section 26.3 s'affiche
# (exec_monitor, file_access, net_connect, etc.)

# Générer un événement de test
touch /tmp/test_audit_file

# Vérifier la capture
sudo ausearch -k file_access --interpret | tail -5
# Attendu : un événement montrant l'ouverture/création de /tmp/test_audit_file

# Nettoyage
rm /tmp/test_audit_file
```

**`inotifywait`** :

```bash
# Lancer inotifywait en arrière-plan sur /tmp/
inotifywait -m -r /tmp/ --format '%T %w%f %e' --timefmt '%H:%M:%S' &  
INOTIFY_PID=$!  

# Générer un événement de test
echo "test" > /tmp/test_inotify_file

# Attendu : inotifywait affiche une ligne comme :
# 14:35:22 /tmp/test_inotify_file CREATE
# 14:35:22 /tmp/test_inotify_file MODIFY

# Nettoyage
kill $INOTIFY_PID  
rm /tmp/test_inotify_file  
```

**`tcpdump`** (sur l'**hôte**) :

```bash
# Sur l'hôte — lancer une capture de 10 secondes
sudo timeout 10 tcpdump -i br-malware -c 5 --print 2>&1

# Pendant ce temps, dans la VM, générer du trafic :
ping -c 3 10.66.66.1

# Attendu : tcpdump sur l'hôte affiche les paquets ICMP (ping)
# entre 10.66.66.100 et 10.66.66.1
```

**`sysdig`** :

```bash
# Dans la VM — capture rapide de 5 secondes
sudo timeout 5 sysdig "evt.type=open and proc.name=bash" 2>/dev/null | head -10
# Attendu : des événements montrant les appels open() faits par bash
# (Même s'il n'y a que quelques lignes ou aucune, l'absence d'erreur
#  confirme que sysdig fonctionne)
```

### Phase 4 — Vérification du durcissement de la VM

```bash
# Pas de dossier partagé monté
mount | grep -iE '(vboxsf|vmhgfs|9p|shared)'
# Attendu : aucun résultat

# spice-vdagent n'est pas installé ou n'est pas actif
pgrep spice-vdagent
# Attendu : aucun PID retourné

# L'utilisateur sample-runner existe
id sample-runner
# Attendu : uid et gid affichés

# Le répertoire de sample-runner est protégé
ls -ld /home/sample-runner/
# Attendu : permissions drwx------ (700)

# Le répertoire de travail pour les samples existe
ls -ld ~/malware-samples/
# Attendu : le répertoire existe avec permissions 700
```

### Phase 5 — Vérification du cycle snapshot complet

Cette phase simule un cycle d'analyse complet sans sample réel. Elle vérifie que les snapshots fonctionnent et que le rollback restaure un état propre.

```bash
# Sur l'hôte — prendre un snapshot de test
virsh snapshot-create-as malware-lab \
  --name "test-checkpoint" \
  --description "Snapshot de vérification du checkpoint ch26"

# Dans la VM — créer un fichier marqueur
ssh analyst@10.66.66.100 'echo "Ce fichier prouve que le snapshot fonctionne" > ~/marqueur.txt'  
ssh analyst@10.66.66.100 'cat ~/marqueur.txt'  
# Attendu : le contenu du fichier s'affiche

# Sur l'hôte — restaurer le snapshot
virsh snapshot-revert malware-lab --snapshotname "test-checkpoint"

# Relancer la VM (le revert sur un snapshot offline arrête la VM)
virsh start malware-lab
# Attendre le boot...

# Vérifier que le marqueur a disparu
ssh analyst@10.66.66.100 'cat ~/marqueur.txt 2>&1'
# Attendu : "No such file or directory"
# Le fichier n'existe plus : le rollback a fonctionné.

# Nettoyer le snapshot de test
virsh snapshot-delete malware-lab --snapshotname "test-checkpoint"
```

**Résultat attendu** : le fichier `marqueur.txt` existe avant le rollback et a disparu après. C'est la preuve que le mécanisme de snapshot fonctionne correctement et que tout état créé après un snapshot peut être effacé proprement.

---

## Grille de validation

Parcourez cette grille et cochez chaque point. **Tous les points doivent être validés** pour considérer le checkpoint comme réussi.

```
INFRASTRUCTURE
  □  kvm-ok confirme le support de la virtualisation
  □  La VM malware-lab existe et démarre
  □  Le snapshot clean-base existe et est restaurable
  □  Le réseau isolated-malware est actif

ISOLATION RÉSEAU
  □  La VM obtient une IP en 10.66.66.x
  □  La VM peut joindre l'hôte (10.66.66.1)
  □  ping 8.8.8.8 échoue depuis la VM
  □  ping 1.1.1.1 échoue depuis la VM
  □  host example.com échoue depuis la VM
  □  ip route ne montre aucune route par défaut
  □  iptables sur l'hôte montre les règles DROP sur br-malware

MONITORING
  □  auditd est actif, les règles se chargent, un événement de test est capturé
  □  inotifywait détecte la création d'un fichier de test
  □  tcpdump sur l'hôte capture le trafic du bridge
  □  sysdig s'exécute sans erreur

DURCISSEMENT
  □  Aucun dossier partagé monté
  □  spice-vdagent absent ou inactif
  □  L'utilisateur sample-runner existe
  □  /home/sample-runner/ est en permissions 700
  □  ~/malware-samples/ existe

SNAPSHOTS
  □  Un snapshot de test peut être créé
  □  Un fichier créé après le snapshot disparaît après rollback
  □  Le snapshot de test est nettoyé
```

---

## En cas d'échec

Si un ou plusieurs points échouent, le tableau suivant oriente vers la section à relire et les causes les plus fréquentes.

| Symptôme | Cause probable | Section à relire |  
|---|---|---|  
| `kvm-ok` échoue | VT-x/AMD-V désactivé dans le BIOS | 26.2 (Vérifier le support) |  
| La VM ne démarre pas | Image qcow2 corrompue ou chemin incorrect | 26.2 (Création de l'image) |  
| Pas de snapshot `clean-base` | Oublié lors de l'installation initiale | 26.2 (Le snapshot de référence) |  
| `ping 8.8.8.8` réussit depuis la VM | VM encore sur le réseau `default` (NAT) | 26.2 (Basculer entre les réseaux) |  
| `ping 10.66.66.1` échoue | DHCP non renouvelé ou bridge mal configuré | 26.4 (Anatomie du bridge) |  
| `host example.com` réussit | `dnsmasq` résout vers INetSim — acceptable si intentionnel | 26.4 (Répondeur DNS) |  
| `auditctl -l` ne montre aucune règle | Fichier de règles absent ou non chargé | 26.3 (Configurer des règles auditd) |  
| `inotifywait` : command not found | `inotify-tools` non installé | 26.3 (Installation) |  
| `tcpdump -i br-malware` échoue | Bridge non créé ou nom différent | 26.4 (Vérifier la topologie) |  
| `sysdig` échoue | Module noyau non chargé ou installation incomplète | 26.3 (Installation sysdig) |  
| Le rollback ne supprime pas le fichier | Snapshot pris après la création du fichier | 26.2 (Gestion des snapshots) |

---

## Ce que valide ce checkpoint

En complétant ce checkpoint avec succès, vous avez démontré que :

- Vous maîtrisez la création et la gestion d'une VM d'analyse avec QEMU/KVM.  
- Votre réseau isolé est réellement étanche — aucun trafic ne peut quitter le bridge `br-malware`.  
- Vos outils de monitoring fonctionnent et sont capables de capturer l'activité système, fichier et réseau.  
- Votre VM est durcie conformément au principe du moindre privilège.  
- Le mécanisme de snapshot vous garantit une réversibilité totale.

Votre lab est prêt. Le chapitre 27 y introduira le premier sample hostile : un ransomware ELF compilé avec GCC, qui chiffrera des fichiers dans `/tmp/test/` avec une clé AES hardcodée. Tout ce que vous avez construit dans ce chapitre — l'isolation, le monitoring, les snapshots, les règles d'or — sera mobilisé pour l'observer, le comprendre, et finalement écrire un déchiffreur.

---

> 📌 **Règle de progression** — Ne passez au chapitre 27 que si **tous** les points de la grille de validation sont cochés. Un lab partiellement fonctionnel est un lab qui donne un faux sentiment de sécurité — c'est pire qu'un lab inexistant, parce qu'on y exécute des samples en croyant être protégé.

⏭️ [Chapitre 27 — Analyse d'un ransomware Linux ELF (auto-compilé GCC)](/27-ransomware/README.md)
