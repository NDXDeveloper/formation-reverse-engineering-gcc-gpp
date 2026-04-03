🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 23.1 — Identifier le protocole custom avec `strace` + Wireshark

> 🎯 **Objectif de cette section** : avant même d'ouvrir un désassembleur, obtenir une première cartographie du protocole réseau en observant le trafic en temps réel. À la fin de cette section, vous aurez identifié le port utilisé, la séquence d'échanges, la taille des messages, et repéré les premiers patterns récurrents (magic bytes, headers fixes).

---

## Pourquoi commencer par l'observation

Quand on reçoit deux binaires — un client et un serveur — le réflexe naturel est d'ouvrir Ghidra immédiatement. C'est une erreur stratégique. Le désassemblage d'un parseur réseau sans contexte est un travail ingrat : on se retrouve face à des dizaines de `recv()` et de comparaisons d'octets sans savoir ce qu'on cherche.

L'approche inverse est bien plus productive : **lancer les deux binaires, observer ce qui se passe sur le réseau et dans les appels système, puis seulement après ouvrir le désassembleur avec des hypothèses à vérifier.** C'est la différence entre explorer un labyrinthe à l'aveugle et y entrer avec une carte approximative.

Cette phase d'observation repose sur deux outils complémentaires :

- **`strace`** observe le programme **de l'intérieur** : quels appels système il fait, dans quel ordre, avec quels arguments et quelles données.  
- **Wireshark** observe le trafic **de l'extérieur** : ce qui transite réellement sur le réseau, octet par octet, avec le timing de chaque paquet.

Croiser les deux vues donne une compréhension rapide du protocole avant toute analyse statique.

---

## Phase 1 — Triage rapide des deux binaires

Avant de lancer quoi que ce soit, on applique le workflow de triage du chapitre 5 sur les deux binaires. Cela prend deux minutes et donne déjà des informations précieuses.

### `file` — nature des binaires

```bash
$ file server
server: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, not stripped  

$ file client
client: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, not stripped  
```

On confirme : deux ELF 64 bits, liés dynamiquement, PIE activé. Le fait qu'ils soient `not stripped` (dans la variante `-O0 -g`) facilitera l'analyse. En conditions réelles, on commence toujours par la variante la plus facile avant de s'attaquer aux versions strippées.

### `strings` — indices textuels

```bash
$ strings server | grep -iE "port|listen|bind|auth|error|welcome|password|key|secret"
```

Les chaînes de caractères révèlent souvent le vocabulaire du protocole : messages d'erreur, noms de commandes, prompts d'authentification. On cherche en particulier :

- Des **messages d'erreur** comme `"Invalid command"`, `"Auth failed"`, `"Bad magic"` — ils trahissent la logique de validation du parseur.  
- Des **numéros de port** en dur dans les chaînes de format (`"Listening on port %d"`).  
- Des **noms de commandes** en clair (`"HELLO"`, `"AUTH"`, `"DATA"`, `"QUIT"`) si le protocole utilise des commandes textuelles ou mixtes.  
- Des **chaînes de bienvenue** ou de handshake (`"Welcome"`, `"Server ready"`).

On fait la même chose côté client :

```bash
$ strings client | grep -iE "connect|send|recv|server|auth|login|user|pass"
```

> 💡 **Astuce** : `strings -t x` affiche l'offset hexadécimal de chaque chaîne. Notez les offsets intéressants : ils seront des points d'entrée précieux dans Ghidra via les cross-references (chapitre 8, section 8.7).

### `checksec` — protections actives

```bash
$ checksec --file=server
$ checksec --file=client
```

On note les protections (PIE, RELRO, canary, NX) pour anticiper les contraintes de l'analyse dynamique. Pour ce chapitre, l'intérêt principal est de vérifier si PIE est activé — ce qui affectera les adresses de breakpoints dans GDB plus tard.

### `ldd` — dépendances

```bash
$ ldd server
$ ldd client
```

On cherche des bibliothèques réseau ou crypto particulières. Une dépendance à `libssl` ou `libcrypto` indiquerait du chiffrement TLS, ce qui compliquerait considérablement la capture réseau. Une dépendance à `libz` pourrait indiquer de la compression. Pour notre binaire d'entraînement, on s'attend à ne voir que la libc — le protocole est implémenté directement avec les sockets POSIX.

---

## Phase 2 — Capture réseau avec `strace`

### Lancer le serveur sous `strace`

On démarre le serveur en traçant les appels système liés au réseau et aux entrées/sorties :

```bash
$ strace -f -e trace=network,read,write -x -s 256 -o server_trace.log ./server
```

Décortiquons les options :

- **`-f`** : suit les processus fils. Indispensable si le serveur utilise `fork()` pour gérer les connexions (modèle classique des serveurs concurrents).  
- **`-e trace=network,read,write`** : filtre uniquement les appels système réseau (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `shutdown`, `close`…) et les opérations de lecture/écriture (`read`, `write`, souvent utilisées à la place de `send`/`recv` sur des sockets TCP).  
- **`-x`** : affiche les données binaires en hexadécimal plutôt qu'en ASCII échappé. Crucial pour un protocole binaire.  
- **`-s 256`** : augmente la taille maximale des chaînes affichées (défaut : 32). Pour des paquets réseau, 256 est un bon compromis ; augmentez à 1024 si les messages sont longs.  
- **`-o server_trace.log`** : redirige la sortie dans un fichier pour analyse ultérieure.

### Lancer le client sous `strace`

Dans un second terminal :

```bash
$ strace -f -e trace=network,read,write -x -s 256 -o client_trace.log ./client 127.0.0.1
```

Le client se connecte au serveur et effectue sa séquence complète (handshake, authentification, commandes). Une fois terminé, on dispose de deux fichiers de trace complets.

### Lire la trace du serveur

Voici un exemple annoté de ce qu'on pourrait observer dans `server_trace.log` :

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3  
setsockopt(3, SOL_SOCKET, SO_REUSEADDR, [1], 4) = 0  
bind(3, {sa_family=AF_INET, sin_port=htons(4444), sin_addr=inet_addr("0.0.0.0")}, 16) = 0  
listen(3, 5)                            = 0  
accept(3, {sa_family=AF_INET, sin_port=htons(54321), sin_addr=inet_addr("127.0.0.1")}, [16]) = 4  
```

Ces cinq premières lignes sont la séquence classique d'un serveur TCP. On en extrait immédiatement :

- **Le port d'écoute** : `4444` (visible dans le `bind`).  
- **Le protocole de transport** : TCP (`SOCK_STREAM`).  
- **Le modèle de connexion** : le serveur fait un `accept`, donc c'est un serveur à connexion unique ou concurrent (vérifier la présence de `fork` ensuite).

La suite est la partie intéressante — les échanges de données :

```
recv(4, "\xc0\x01\x00\x08", 4, 0)                             = 4  
recv(4, "HELLO\x00\x00\x00", 8, 0)                            = 8  
send(4, "\xc0\x81\x00\x0f", 4, 0)                             = 4  
send(4, "WELCOME\xa3\x7b\x01\xf9\x8c\x22\xd4\x5e", 15, 0)   = 15  
recv(4, "\xc0\x02\x00\x12", 4, 0)                             = 4  
recv(4, "\x05admin\x0b\xd0\x48\x62\x8c\xfe\x11\x84\x1e\xd0\x08\x20", 18, 0) = 18  
send(4, "\xc0\x82\x00\x02", 4, 0)                             = 4  
send(4, "\x00\x01", 2, 0)                                      = 2  
```

Le premier pattern qui saute aux yeux : **chaque échange commence par un `recv` (ou `send`) de exactement 4 octets**, suivi d'un second appel dont la taille varie. Le serveur lit donc un **en-tête fixe de 4 octets**, puis un **payload de taille variable** déterminée par l'en-tête. C'est le pattern classique d'un protocole TLV (Type-Length-Value).

En examinant les en-têtes de 4 octets, on voit :

- Le premier octet est toujours **`\xc0`** — c'est probablement le **magic byte**.  
- Le deuxième octet varie : `\x01`, `\x81`, `\x02`, `\x82` — les valeurs `\x8x` semblent être les réponses (bit 7 mis à 1 = réponse ?).  
- Les octets 3–4 (`\x00\x08`, `\x00\x0f`, `\x00\x12`, `\x00\x02`) ressemblent à un **champ de longueur** en big-endian. On vérifie : `\x00\x08` = 8, et le `recv` suivant lit exactement 8 octets. `\x00\x0f` = 15, et le `send` suivant envoie 15 octets. La correspondance est parfaite.  
- Les payloads contiennent des chaînes lisibles (`HELLO`, `WELCOME`, `admin`) entrecoupées de données binaires opaques.

> 📝 **Note** : à ce stade, ce ne sont que des **hypothèses**. On les notera soigneusement pour les confirmer ou les invalider lors du désassemblage (section 23.2).

### Lire la trace du client

La trace du client donne le même échange vu depuis l'autre côté. L'intérêt est de **corréler les `write` du client avec les `read` du serveur** (et inversement) pour vérifier qu'il n'y a pas de fragmentation ou de buffering qui décalerait les données.

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3  
connect(3, {sa_family=AF_INET, sin_port=htons(4444), sin_addr=inet_addr("127.0.0.1")}, 16) = 0  
send(3, "\xc0\x01\x00\x08", 4, 0)                             = 4  
send(3, "HELLO\x00\x00\x00", 8, 0)                            = 8  
recv(3, "\xc0\x81\x00\x0f", 4, 0)                             = 4  
recv(3, "WELCOME\xa3\x7b\x01\xf9\x8c\x22\xd4\x5e", 15, 0)   = 15  
send(3, "\xc0\x02\x00\x12", 4, 0)                             = 4  
send(3, "\x05admin\x0b\xd0\x48\x62\x8c\xfe\x11\x84\x1e\xd0\x08\x20", 18, 0) = 18  
recv(3, "\xc0\x82\x00\x02", 4, 0)                             = 4  
recv(3, "\x00\x01", 2, 0)                                      = 2  
```

On retrouve le même pattern header/payload. La correspondance avec la trace serveur est directe : le `send` de 4+8 octets du client correspond aux deux `recv` de 4 puis 8 octets du serveur. On confirme que le protocole est **synchrone requête/réponse** : le client envoie un message, attend la réponse, puis envoie le message suivant.

### Extraire un chronogramme des échanges

À partir des deux traces, on peut déjà construire un chronogramme du protocole :

```
Client                          Serveur
  |                                |
  |--- [C0 01 ...] HELLO -------->|     Handshake request
  |<-- [C0 81 ...] WELCOME -------|     Handshake response
  |                                |
  |--- [C0 02 ...] AUTH ---------->|     Authentication request
  |<-- [C0 82 ...] OK ------------|     Authentication response
  |                                |
  |--- [C0 03 ...] CMD ---------->|     Command request
  |<-- [C0 83 ...] DATA ----------|     Command response
  |                                |
  |--- [C0 04 ...] QUIT --------->|     Disconnect
  |<-- [C0 84 ...] BYE -----------|     Disconnect ack
  |                                |
```

Ce schéma, aussi approximatif soit-il, sera un guide précieux pour le désassemblage.

---

## Phase 3 — Capture réseau avec Wireshark

### Capture sur l'interface loopback

Puisque le client et le serveur tournent sur la même machine, le trafic passe par l'interface **loopback** (`lo`). On lance Wireshark (ou `tcpdump`) sur cette interface :

```bash
# Avec tcpdump (capture brute pour analyse ultérieure dans Wireshark) :
$ sudo tcpdump -i lo -w ch23_capture.pcap port 4444

# Ou directement dans Wireshark :
# Menu Capture → choisir l'interface "Loopback: lo" → filtre de capture : "port 4444"
```

On relance ensuite le client pour générer du trafic, puis on arrête la capture.

### Première lecture dans Wireshark

En ouvrant la capture, on voit la séquence TCP classique :

1. **Three-way handshake** : `SYN` → `SYN-ACK` → `ACK` (Wireshark les affiche en gris/noir).  
2. **Échanges de données** : les paquets `PSH-ACK` contiennent les données applicatives — c'est ce qui nous intéresse.  
3. **Fermeture** : `FIN-ACK` → `FIN-ACK` → `ACK`.

On applique un filtre d'affichage pour ne garder que les données applicatives :

```
tcp.port == 4444 && tcp.len > 0
```

### Identifier les patterns dans le payload

En cliquant sur chaque paquet `PSH-ACK`, le panneau inférieur de Wireshark affiche le payload hexadécimal. On retrouve les mêmes octets que dans la trace `strace` — ce qui est rassurant et confirme qu'il n'y a pas de couche intermédiaire (TLS, compression) entre l'application et le réseau.

Les avantages de Wireshark par rapport à `strace` pour cette phase :

- **Vision temporelle** : la colonne `Time` montre le délai entre chaque paquet. Un délai important entre la requête d'authentification et la réponse pourrait indiquer un traitement coûteux côté serveur (hachage de mot de passe, accès fichier…).  
- **Vision par flux** : le menu `Analyze → Follow → TCP Stream` reconstitue l'intégralité de la conversation en continu, alternant les données du client (en rouge) et du serveur (en bleu). C'est la vue la plus lisible pour comprendre le protocole d'un coup d'œil.  
- **Statistiques** : le menu `Statistics → Conversations` donne le nombre total d'octets échangés dans chaque direction, et `Statistics → I/O Graphs` montre le profil temporel du trafic.  
- **Détection automatique** : si le protocole ressemble à un protocole connu (HTTP, DNS, TLS…), Wireshark le décodera automatiquement. L'absence de décodage confirme qu'on est bien face à un protocole custom.

### Follow TCP Stream — la vue qui résume tout

La fonctionnalité `Follow TCP Stream` est particulièrement utile. En basculant l'affichage en **"Hex Dump"**, on obtient la conversation complète avec l'alternance client/serveur clairement marquée :

```
→ 00000000  c0 01 00 08 48 45 4c 4c  4f 00 00 00             ....HELL O...
← 00000000  c0 81 00 0f 57 45 4c 43  4f 4d 45 a3 7b 01 f9 8c ....WELC OME.{...
← 00000010  22 d4 5e                                          ".^
→ 0000000C  c0 02 00 12 05 61 64 6d  69 6e 0b d0 48 62 8c fe .....adm in..Hb..
→ 0000001C  11 84 1e d0 08 20                                 ..... 
← 00000013  c0 82 00 02 00 01                                 ......
```

Les flèches `→` indiquent les données envoyées par le client, `←` celles envoyées par le serveur. On note que le nom d'utilisateur `"admin"` est visible en clair dans le payload AUTH, mais le mot de passe qui suit (après l'octet `\x0b`) est une séquence d'octets opaques — un indice que le mot de passe subit une transformation avant envoi. Cette vue est idéale pour repérer visuellement les structures de paquets.

> 💡 **Astuce** : on peut sauvegarder ce flux brut depuis Wireshark (`Show data as: Raw`, puis `Save as…`) pour l'ouvrir ensuite dans ImHex et y appliquer un pattern `.hexpat` (section 23.3).

---

## Phase 4 — Synthèse des observations

### Construire un tableau d'hypothèses

À ce stade, sans avoir ouvert un seul désassembleur, on peut formuler un ensemble d'hypothèses structurées sur le protocole :

**Structure de l'en-tête (4 octets) — hypothèse :**

| Offset | Taille | Hypothèse | Valeurs observées |  
|--------|--------|-----------|-------------------|  
| 0 | 1 octet | Magic byte | Toujours `0xC0` |  
| 1 | 1 octet | Type de commande | `0x01`=HELLO, `0x02`=AUTH, `0x03`=CMD, `0x04`=QUIT |  
| 2–3 | 2 octets | Longueur du payload (big-endian) | Cohérent avec les tailles observées |

**Types de messages — hypothèse :**

| Type | Direction | Nom supposé | Payload observé |  
|------|-----------|-------------|-----------------|  
| `0x01` | Client → Serveur | Handshake request | Chaîne `"HELLO"` + padding |  
| `0x81` | Serveur → Client | Handshake response | Chaîne `"WELCOME"` + 8 octets (challenge ?) |  
| `0x02` | Client → Serveur | Auth request | Longueur + username + longueur + password |  
| `0x82` | Serveur → Client | Auth response | 2 octets (status code ?) |  
| `0x03` | Client → Serveur | Command request | À déterminer |  
| `0x83` | Serveur → Client | Command response | À déterminer |  
| `0x04` | Client → Serveur | Disconnect | Payload vide ou minimal |  
| `0x84` | Serveur → Client | Disconnect ack | Payload vide ou minimal |

**Observations complémentaires :**

- Le bit 7 du champ type semble distinguer les requêtes (`0x0_`) des réponses (`0x8_`). C'est un pattern fréquent dans les protocoles binaires (on le retrouve dans RADIUS, ASN.1 BER, et d'autres).  
- Le payload d'authentification contient la chaîne `"admin"` en clair (lisible dans le strace), précédée de l'octet `\x05` (5 — la longueur de `"admin"`). Cela suggère un format **length-prefixed strings** : un octet de longueur suivi de la chaîne. Le reste du payload AUTH est constitué d'octets binaires opaques — probablement le mot de passe, mais sous une forme transformée (chiffré ? hashé ? XOR-é ?). On ne voit pas de mot de passe en clair.  
- Les 8 octets après `"WELCOME"` dans la réponse de handshake pourraient être un **challenge** ou un **nonce** utilisé dans l'authentification — ce qui expliquerait pourquoi le mot de passe n'est pas lisible. Il faudra vérifier si ces octets changent d'une session à l'autre.

### Vérifier la variabilité du challenge

Pour tester l'hypothèse du challenge/nonce, on relance le client plusieurs fois et on compare les réponses de handshake :

```bash
# Relancer 3 fois en capturant à chaque fois
for i in 1 2 3; do
    strace -e trace=read -x -s 256 ./client 127.0.0.1 2>&1 | grep "WELCOME"
done
```

Si les 8 octets après `"WELCOME"` changent à chaque connexion, c'est bien un nonce ou un challenge. Si ils sont identiques, c'est une valeur fixe (identifiant de session, version du protocole, ou simplement du padding). Cette distinction est importante : un challenge variable signifie que l'authentification n'est pas un simple envoi de mot de passe en clair, et qu'un replay naïf de la séquence d'authentification pourrait échouer.

---

## Phase 5 — Préparer la suite

Les observations de cette section ont produit trois livrables concrets :

1. **Les fichiers de traces `strace`** (`server_trace.log`, `client_trace.log`) — ils serviront de référence tout au long de l'analyse.  
2. **La capture Wireshark** (`ch23_capture.pcap`) — elle sera réouverte dans les sections suivantes pour valider les hypothèses et, dans la section 23.3, exportée en raw pour analyse dans ImHex.  
3. **Le tableau d'hypothèses** sur la structure du protocole — c'est la carte avec laquelle on entrera dans le désassembleur à la section 23.2.

L'étape suivante consiste à ouvrir le binaire serveur dans Ghidra et à localiser le parseur de paquets pour confirmer (ou invalider) chacune de ces hypothèses. On sait désormais exactement quoi chercher : une fonction qui lit 4 octets d'en-tête, vérifie le magic byte `0xC0`, extrait le type et la longueur, puis dispatche selon le type. C'est un point d'entrée bien plus précis que "désassembler le binaire et voir ce qu'il se passe".

---

## Pièges courants

### Fragmentation TCP

TCP est un protocole de flux, pas de messages. Rien ne garantit qu'un `send()` de 22 octets côté client arrive en un seul `recv()` de 22 octets côté serveur. Le noyau peut fragmenter ou regrouper les données. En pratique, sur `localhost` avec des petits messages, la fragmentation est rare. Mais en conditions réelles (réseau lent, paquets volumineux), un seul message applicatif peut arriver en plusieurs `recv()`. Il faut en être conscient en lisant les traces : si un `read()` retourne moins d'octets que prévu, ce n'est pas un bug du protocole, c'est TCP qui fait son travail.

### `send`/`recv` vs `read`/`write`

Sur un socket TCP sous Linux, `read()` et `recv()` (sans flags) sont fonctionnellement identiques, de même que `write()` et `send()`. Il ne faut pas être surpris de voir l'un ou l'autre dans les traces `strace` — c'est un choix de l'auteur du programme, pas une différence de comportement. Le filtre `strace` `-e trace=network,read,write` couvre les deux cas.

### Protocole chiffré ou compressé

Si le payload dans Wireshark est totalement opaque (entropie élevée, aucune chaîne lisible, aucun pattern visible), il est probable que le protocole utilise du chiffrement ou de la compression. Dans ce cas, `strace` reste utile (les données sont chiffrées *avant* `send()` et déchiffrées *après* `recv()`, donc on peut potentiellement intercepter les données en clair en posant des breakpoints sur les fonctions de chiffrement). Mais la capture réseau seule ne suffira pas.

Pour notre binaire d'entraînement, le protocole est en clair — les chaînes `"HELLO"`, `"WELCOME"`, `"admin"` sont directement visibles. En conditions réelles, ce n'est pas toujours le cas.

### Serveurs multi-processus ou multi-threads

Si le serveur utilise `fork()` pour chaque connexion, le flag `-f` de `strace` est indispensable : sans lui, on ne voit que le processus parent (qui ne fait que `accept`) et on manque tout le traitement de la connexion dans le processus fils. Si le serveur utilise des threads, `-f` les suit également (les threads sont des LWP sous Linux). La trace indiquera le PID/TID de chaque appel, ce qui permet de distinguer les flux.

---

## Récapitulatif de la section

| Étape | Outil | Ce qu'on obtient |  
|-------|-------|------------------|  
| Triage des binaires | `file`, `strings`, `checksec`, `ldd` | Nature des binaires, indices textuels, protections, dépendances |  
| Trace système côté serveur | `strace -f -e trace=network,read,write` | Séquence d'appels, buffers en hexa, tailles des messages |  
| Trace système côté client | `strace` (mêmes options) | Vue miroir, corrélation des échanges |  
| Capture réseau | Wireshark / `tcpdump` sur loopback | Paquets bruts, timing, Follow TCP Stream |  
| Synthèse | Analyse manuelle | Tableau d'hypothèses : magic byte, types, longueurs, séquence |

À la fin de cette phase, on dispose d'une **compréhension macroscopique du protocole** : combien de messages sont échangés, dans quel ordre, quelle est leur taille approximative, et quels patterns structurels on peut y repérer. La section suivante (**23.2**) plongera dans le désassemblage pour reconstruire précisément la machine à états du parseur de paquets.

⏭️ [RE du parseur de paquets (state machine, champs, magic bytes)](/23-network/02-re-parseur-paquets.md)
