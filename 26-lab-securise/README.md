🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 26 — Mise en place d'un lab d'analyse sécurisé

> **Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**

---

## Pourquoi un chapitre entier sur le lab ?

Jusqu'ici, les binaires que nous avons manipulés étaient inoffensifs : des crackmes, des programmes réseau maîtrisés, des parseurs de fichiers. Leur exécution sur votre machine de travail ne posait aucun risque réel. Avec cette sixième partie, la donne change radicalement. Nous allons analyser des programmes dont le **comportement est volontairement destructeur** — chiffrement de fichiers, communication avec un serveur de commande et contrôle (C2), injection de code. Même si tous les samples fournis dans cette formation sont créés par nos soins et strictement pédagogiques, ils reproduisent fidèlement les mécanismes de vrais malwares. Les exécuter sans précaution reviendrait à manipuler un réactif chimique dangereux sur la table de la cuisine.

L'analyse de code malveillant ne commence donc pas par l'ouverture de Ghidra ou le lancement de GDB. Elle commence par la **construction d'un environnement isolé, reproductible et jetable** dans lequel vous pouvez observer le comportement d'un binaire hostile sans mettre en péril vos données, votre réseau local, ni les machines de votre entourage.

Ce chapitre pose les fondations de cet environnement. Sans lui, aucun des chapitres suivants (27, 28, 29) ne doit être abordé.

---

## Ce que vous allez apprendre

Ce chapitre couvre l'ensemble du processus de mise en place d'un laboratoire d'analyse sécurisé, depuis la philosophie d'isolation jusqu'à la vérification concrète que votre environnement est bien étanche. Vous y découvrirez les principes fondamentaux qui justifient chaque couche de protection, la création d'une machine virtuelle dédiée avec QEMU/KVM configurée pour le snapshoting et le rollback rapide, l'installation et la configuration des outils de monitoring nécessaires à l'observation comportementale (`auditd`, `inotifywait`, `tcpdump`, `sysdig`), la mise en place d'un réseau isolé via un bridge dédié pour capturer le trafic sans jamais l'exposer à l'extérieur, et enfin les règles de discipline strictes qui doivent accompagner toute session d'analyse de code malveillant.

> 🔧 **Note sur les binaires d'entraînement** — Contrairement aux autres chapitres de cette formation, le chapitre 26 ne comporte **aucun binaire à compiler ni à analyser**. C'est un chapitre d'infrastructure : il construit l'environnement dans lequel les samples des chapitres suivants seront exécutés. Il n'y a pas de dossier `binaries/ch26-*/` dans le dépôt. Les premiers binaires à manipuler dans ce lab seront ceux du chapitre 27 (`binaries/ch27-ransomware/`), du chapitre 28 (`binaries/ch28-dropper/`) et du chapitre 29 (`binaries/ch29-packed/`). Le snapshot `clean-base` doit impérativement être pris **avant** l'introduction de tout sample dans la VM.

---

## Prérequis

Avant d'entamer ce chapitre, vous devez être à l'aise avec les éléments suivants :

- **Administration Linux de base** — gestion de paquets, configuration réseau, édition de fichiers système. Le chapitre 4 (Environnement de travail) couvre ces fondamentaux.  
- **Virtualisation** — vous avez déjà créé et utilisé une VM au chapitre 4. Ici, nous allons plus loin dans la configuration, notamment sur l'aspect réseau et les snapshots.  
- **Les outils des parties II et III** — `strace`, `ltrace`, GDB, Frida, Ghidra. Le lab que nous construisons est le théâtre dans lequel tous ces outils seront déployés sur des cibles hostiles.  
- **Connaissances réseau minimales** — comprendre ce qu'est une interface réseau, un bridge, une règle `iptables` basique. Rien d'expert, mais il faut pouvoir lire une sortie de `ip addr` sans être perdu.

---

## Architecture du lab

Le schéma général du laboratoire repose sur un principe simple : **la machine d'analyse n'est jamais la machine hôte**. Voici l'architecture cible que nous allons construire dans les sections qui suivent :

```
┌─────────────────────────────────────────────────────────┐
│                    MACHINE HÔTE                         │
│                                                         │
│   Rôle : piloter la VM, stocker les snapshots,          │
│          rédiger les rapports.                          │
│   Règle : AUCUN sample n'est jamais exécuté ici.        │
│                                                         │
│   ┌───────────────────────────────────────────────┐     │
│   │          VM D'ANALYSE (QEMU/KVM)              │     │
│   │                                               │     │
│   │  - Système invité : Debian/Ubuntu minimal     │     │
│   │  - Outils RE installés (GDB, Ghidra, Frida…)  │     │
│   │  - Samples copiés dans /tmp/malware/          │     │
│   │  - Snapshots avant chaque exécution           │     │
│   │                                               │     │
│   │  Réseau : bridge isolé (br-malware)           │     │
│   │           ├─ pas de route vers Internet       │     │
│   │           ├─ pas de route vers le LAN         │     │
│   │           └─ tcpdump capture tout le trafic   │     │
│   └───────────────────────────────────────────────┘     │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

Trois couches de sécurité se superposent :

1. **Isolation d'exécution** — le code tourne dans une VM, pas sur l'hôte. Si le sample corrompt le système invité, un rollback de snapshot suffit à revenir à l'état propre en quelques secondes.  
2. **Isolation réseau** — la VM est connectée à un bridge virtuel qui ne route vers rien. Le malware peut tenter d'appeler un C2, d'exfiltrer des données ou de scanner le réseau : ses paquets ne quitteront jamais le bridge. Pendant ce temps, `tcpdump` les capture intégralement pour analyse ultérieure.  
3. **Isolation comportementale** — des outils de monitoring (`auditd`, `inotifywait`, `sysdig`) observent en temps réel les actions du sample : fichiers créés ou modifiés, appels système, processus enfants lancés. C'est cette couche qui transforme le lab en véritable observatoire.

---

## Différence avec le chapitre 4

Au chapitre 4, nous avions déjà mis en place une VM pour l'environnement de travail. Pourquoi ne pas simplement réutiliser cette VM ? Pour deux raisons fondamentales.

La première est une question de **propreté analytique**. Votre VM de travail contient vos projets, vos configurations Ghidra, vos scripts, vos notes. Un sample malveillant qui chiffre `/home` ou qui modifie des bibliothèques partagées pourrait compromettre tout ce travail. La VM d'analyse doit être **jetable** : on la restaure à un état propre avant chaque session, sans rien regretter.

La seconde est une question de **rigueur réseau**. La VM du chapitre 4 avait probablement un accès NAT ou host-only avec passerelle — ce qui est parfait pour installer des paquets et suivre le tutoriel. Mais pour l'analyse de malware, le moindre accès réseau vers l'extérieur est un vecteur de propagation ou d'exfiltration. Ici, le réseau est volontairement coupé, et cette coupure est vérifiée avant chaque analyse.

---

## Outils spécifiques à cette partie

En plus des outils déjà installés dans les parties précédentes, ce chapitre introduit plusieurs utilitaires dédiés à l'observation comportementale :

- **`auditd`** — le framework d'audit du noyau Linux. Il permet de tracer des événements système précis (ouverture de fichiers, exécution de binaires, changements de permissions) avec une granularité fine et une faible empreinte sur les performances.  
- **`inotifywait`** (du paquet `inotify-tools`) — surveillance en temps réel des événements sur le système de fichiers. Quand un sample crée, modifie ou supprime un fichier, `inotifywait` le signale immédiatement.  
- **`tcpdump`** — capture de paquets réseau bruts sur le bridge isolé. Les fichiers `.pcap` produits seront ensuite analysés avec Wireshark sur la machine hôte.  
- **`sysdig`** — outil de visibilité système qui combine les capacités de `strace`, `tcpdump` et `lsof` dans une interface unifiée avec un langage de filtrage puissant. Particulièrement utile pour corréler activité réseau et activité fichier.

Nous installerons et configurerons chacun de ces outils dans les sections dédiées.

---

## Plan du chapitre

- **26.1** — Principes d'isolation : pourquoi et comment  
- **26.2** — VM dédiée avec QEMU/KVM — snapshots et réseau isolé  
- **26.3** — Outils de monitoring : `auditd`, `inotifywait`, `tcpdump`, `sysdig`  
- **26.4** — Captures réseau avec un bridge dédié  
- **26.5** — Règles d'or : ne jamais exécuter hors sandbox, ne jamais connecter au réseau réel  
- **🎯 Checkpoint** — Déployer le lab et vérifier l'isolation réseau

---

## Et après ? Les binaires des chapitres 27 à 29

Une fois ce chapitre validé, le lab est prêt à accueillir ses premiers samples. Chaque chapitre suivant de cette partie VI fournit un binaire compilé avec GCC, accompagné de ses sources et d'un `Makefile` dédié :

| Chapitre | Binaire | Source | Comportement |  
|---|---|---|---|  
| **27** — Ransomware | `ransomware_sample` | `binaries/ch27-ransomware/` | Chiffre les fichiers de `/tmp/test/` avec AES, clé hardcodée |  
| **28** — Dropper | `dropper_sample` | `binaries/ch28-dropper/` | Contacte un serveur C2, reçoit des commandes, exfiltre des données |  
| **29** — Packed | `packed_sample` | `binaries/ch29-packed/` | Binaire packé avec UPX, logique cachée à extraire après unpacking |

Ces binaires seront compilés sur votre machine hôte (ou dans la VM de travail du chapitre 4) via `make`, puis **transférés** dans la VM d'analyse par `scp` — jamais exécutés hors du lab. Le workflow complet (compilation → transfert → snapshot → monitoring → exécution → collecte → rollback) sera détaillé pas à pas dès le chapitre 27.

> ⚠️ **Rappel important** — Tous ces samples sont créés par nos soins, compilés avec GCC à partir de sources fournies. Aucun malware réel n'est distribué avec cette formation. Cependant, ces samples reproduisent des comportements réellement malveillants (chiffrement de fichiers, communication C2, packing). Traitez-les avec la même rigueur que s'il s'agissait de vrais échantillons : c'est à la fois une mesure de sécurité et un réflexe professionnel à acquérir dès maintenant.

⏭️ [Principes d'isolation : pourquoi et comment](/26-lab-securise/01-principes-isolation.md)
