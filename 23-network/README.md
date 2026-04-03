🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 23 — Reverse d'un binaire réseau (client/serveur)

> 📦 **Binaires du chapitre** : `binaries/ch23-network/`  
> Le dossier contient un client et un serveur compilés à plusieurs niveaux d'optimisation (`-O0`, `-O2`, `-O3`) avec et sans symboles. Recompilables via `make` avec le `Makefile` dédié.  
> 📝 **Patterns ImHex** : `hexpat/ch23_protocol.hexpat`

---

## Objectifs du chapitre

Le reverse engineering d'un binaire réseau ajoute une dimension que les programmes purement locaux n'ont pas : le **protocole de communication**. Quand on analyse un keygenme ou un binaire crypto, toute la logique est contenue dans un seul exécutable. Ici, la logique est **répartie entre deux processus** — un client et un serveur — qui échangent des données selon un format que l'on ne connaît pas à l'avance.

L'objectif de ce chapitre est d'apprendre à reconstituer un protocole réseau propriétaire à partir des binaires seuls, sans accès au code source, puis d'écrire un client de remplacement capable de dialoguer avec le serveur original.

À l'issue de ce chapitre, vous serez capable de :

- **Identifier les appels système réseau** (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `close`) dans un binaire, et comprendre la séquence d'établissement de connexion.  
- **Capturer et disséquer le trafic** entre le client et le serveur à l'aide de `strace` côté système et de Wireshark côté réseau, pour obtenir une première vision du protocole.  
- **Reconstruire la machine à états du parseur de paquets** : identifier les magic bytes, les champs de longueur, les types de commandes, les séquences de handshake et les codes de réponse.  
- **Visualiser les trames binaires avec ImHex** et formaliser la structure du protocole en écrivant un pattern `.hexpat` réutilisable.  
- **Rejouer une communication capturée** (replay attack) pour valider votre compréhension du protocole et observer la réaction du serveur.  
- **Écrire un client de remplacement complet** en Python avec `pwntools`, capable de reproduire le handshake, l'authentification et l'échange de commandes avec le serveur.

---

## Contexte : le binaire `ch23-network`

Le binaire d'entraînement de ce chapitre simule un scénario réaliste : un **serveur** écoute sur un port TCP et attend des connexions de **clients** qui doivent s'authentifier puis envoyer des commandes via un protocole binaire custom.

Le protocole n'est documenté nulle part. Il utilise :

- Un **magic byte** en tête de chaque paquet pour identifier le début d'une trame valide.  
- Un **champ de type** qui distingue les différentes commandes (handshake, authentification, requête de données, réponse, erreur).  
- Un **champ de longueur** qui indique la taille du payload qui suit.  
- Un **payload** dont le format varie selon le type de commande.  
- Un mécanisme de **handshake initial** avant toute opération.

Ce type d'architecture se retrouve fréquemment dans les logiciels industriels, les protocoles IoT propriétaires, les jeux en ligne, et les implants réseau analysés en forensic.

---

## Pourquoi le RE réseau est différent

### Deux binaires, une seule logique

Contrairement à un binaire autonome, un programme réseau ne fait rien d'intéressant tout seul. Le client envoie des données que le serveur interprète, et inversement. Le reverse engineer doit donc **analyser les deux côtés** pour comprendre le protocole complet. En pratique, on commence souvent par le côté dont on dispose (parfois on n'a que le client, parfois que le serveur) et on infère le reste.

### L'observation du trafic comme point d'entrée

Avant même de désassembler quoi que ce soit, on peut apprendre énormément en **observant le trafic réseau**. Lancer le client et le serveur sur la même machine tout en capturant les échanges avec Wireshark ou `tcpdump` donne immédiatement une idée de la taille des messages, de leur fréquence, de la présence de patterns récurrents (magic bytes, headers fixes) et de la nature du protocole (requête/réponse, streaming, multiplexé…).

### `strace` révèle la structure des appels

Là où Wireshark montre les données brutes sur le réseau, `strace` montre **comment le programme manipule ces données côté système**. On voit les appels à `send()` et `recv()` avec leurs buffers, leurs tailles, et on peut corréler chaque paquet réseau à l'appel système qui l'a produit. Cette double vision — réseau et système — est la clé pour reconstruire le protocole rapidement.

### Le parseur est la cible principale

Dans un binaire réseau, la fonction la plus intéressante est presque toujours le **parseur de paquets** : la routine qui lit les octets entrants, vérifie les magic bytes, extrait le type et la longueur, puis dispatche vers le handler approprié. C'est cette machine à états qu'il faut reconstruire en priorité. Une fois le parseur compris, le reste du protocole se déduit naturellement.

---

## Outils mobilisés dans ce chapitre

Ce chapitre fait la synthèse de nombreux outils vus dans les parties précédentes, appliqués au contexte réseau :

| Outil | Usage dans ce chapitre |  
|---|---|  
| `strace` | Tracer les appels système réseau (`socket`, `connect`, `send`, `recv`…) |  
| Wireshark / `tcpdump` | Capturer et analyser le trafic réseau entre client et serveur |  
| `strings`, `readelf`, `checksec` | Triage initial des binaires client et serveur |  
| Ghidra | Désassemblage et décompilation du parseur de paquets et de la logique protocolaire |  
| ImHex | Visualisation hexadécimale des trames capturées, écriture d'un `.hexpat` pour le protocole |  
| GDB (+ GEF/pwndbg) | Analyse dynamique, breakpoints sur `send`/`recv`, inspection des buffers en mémoire |  
| `pwntools` | Écriture du client de remplacement en Python |

---

## Méthodologie générale

L'approche suivie dans ce chapitre se décompose en cinq phases qui correspondent aux cinq sections :

1. **Observer** — Lancer les binaires, capturer le trafic avec `strace` et Wireshark, noter les patterns visibles à l'œil nu (section 23.1).  
2. **Comprendre** — Désassembler le parseur de paquets dans Ghidra, reconstruire la machine à états et le format de chaque type de message (section 23.2).  
3. **Formaliser** — Écrire un pattern `.hexpat` dans ImHex qui décode visuellement les trames capturées, confirmant ou corrigeant l'analyse statique (section 23.3).  
4. **Valider** — Rejouer une capture réseau vers le serveur pour vérifier que la compréhension du protocole est correcte (section 23.4).  
5. **Reproduire** — Écrire un client Python autonome capable de dialoguer avec le serveur sans le client original (section 23.5).

Chaque phase alimente la suivante : l'observation guide le désassemblage, le désassemblage guide la formalisation, la formalisation est validée par le replay, et le tout culmine dans l'écriture du client.

---

## Prérequis

Avant d'aborder ce chapitre, assurez-vous d'être à l'aise avec :

- **Chapitre 5** — Outils d'inspection de base (`strace`, `strings`, `readelf`, `checksec`), car ils constituent le point de départ du triage.  
- **Chapitre 6** — ImHex et le langage `.hexpat`, indispensables pour la section 23.3.  
- **Chapitre 8** — Ghidra, utilisé intensivement pour reconstruire le parseur de paquets.  
- **Chapitre 11** — GDB, pour poser des breakpoints sur les fonctions réseau et inspecter les buffers.  
- **Chapitre 11, section 11.9** — `pwntools`, utilisé pour écrire le client final.  
- **Notions réseau de base** : modèle client/serveur TCP, notion de socket, ce que font `bind`, `listen`, `accept`, `connect`. Il n'est pas nécessaire d'être un expert réseau, mais il faut comprendre le cycle de vie d'une connexion TCP.

---

## Plan du chapitre

- **23.1** — [Identifier le protocole custom avec `strace` + Wireshark](/23-network/01-identifier-protocole.md)  
- **23.2** — [RE du parseur de paquets (state machine, champs, magic bytes)](/23-network/02-re-parseur-paquets.md)  
- **23.3** — [Visualiser les trames binaires avec ImHex et écrire un `.hexpat` pour le protocole](/23-network/03-trames-imhex-hexpat.md)  
- **23.4** — [Replay Attack : rejouer une requête capturée](/23-network/04-replay-attack.md)  
- **23.5** — [Écrire un client de remplacement complet avec `pwntools`](/23-network/05-client-pwntools.md)  
- **🎯 Checkpoint** — [Écrire un client Python capable de s'authentifier auprès du serveur sans connaître le code source](/23-network/checkpoint.md)

⏭️ [Identifier le protocole custom avec `strace` + Wireshark](/23-network/01-identifier-protocole.md)
