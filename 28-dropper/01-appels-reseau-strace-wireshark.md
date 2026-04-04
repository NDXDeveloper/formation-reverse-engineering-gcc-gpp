🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 28.1 — Identifier les appels réseau avec `strace` + Wireshark

> 📍 **Objectif** — Avant de plonger dans le désassembleur, on commence par une **observation passive** : que fait ce binaire sur le réseau ? Cette première étape de triage dynamique permet de répondre en quelques minutes aux questions critiques — vers quelle adresse le dropper tente-t-il de se connecter ? Sur quel port ? Quel protocole de transport ? Quel volume de données transite ? — sans jamais toucher au code du binaire.

---

## Pourquoi commencer par là ?

Lorsqu'un analyste reçoit un binaire suspect présentant une activité réseau, sa priorité n'est pas de comprendre l'algorithme interne : c'est d'**identifier les indicateurs de compromission réseau** (IOC) le plus vite possible. Une adresse IP, un port, un motif récurrent dans les paquets — ces éléments permettent de bloquer immédiatement la menace au niveau du pare-feu ou du proxy, avant même que l'analyse complète ne soit terminée.

L'approche est donc double et complémentaire :

- **`strace`** observe le binaire **de l'intérieur** — il intercepte chaque appel système effectué par le processus, y compris `socket`, `connect`, `send`, `recv`, `select`, `close`. On voit exactement ce que le programme *demande* au noyau.  
- **Wireshark** (ou `tcpdump`) observe le réseau **de l'extérieur** — il capture les paquets tels qu'ils transitent réellement sur l'interface. On voit ce qui *passe effectivement sur le fil*, y compris les handshakes TCP, les retransmissions, et les données dans leur contexte protocolaire.

Utilisés ensemble, ces deux outils permettent de corréler les intentions du programme (syscalls) avec leur effet observable (paquets réseau). Un `connect()` qui échoue avec `ECONNREFUSED` dans `strace` correspond à un SYN suivi d'un RST dans Wireshark. Un `send()` de 47 octets dans `strace` correspond à un segment TCP contenant ces mêmes 47 octets dans la capture. Cette corrélation croisée est un réflexe fondamental de l'analyse dynamique.

---

## Préparation de l'environnement

### Rappel : isolation réseau obligatoire

Toute cette section se déroule **dans la VM sandboxée** configurée au [Chapitre 26](/26-lab-securise/README.md). Le réseau doit être en mode **host-only** ou sur un bridge dédié isolé. Le dropper est configuré pour se connecter à `127.0.0.1:4444`, ce qui limite le risque, mais la discipline d'isolation doit rester systématique.

### Prendre un snapshot

Avant de lancer le dropper pour la première fois, prenez un snapshot de votre VM. Si le binaire modifie l'état du système de manière inattendue (écriture de fichiers, modification de configuration), vous pourrez revenir à un état propre instantanément.

### Quel binaire utiliser ?

Pour cette première phase d'observation, le choix de la variante (`_O0`, `_O2`, `_O2_strip`) n'a **aucune importance** : les trois variantes produisent exactement les mêmes appels système et les mêmes paquets réseau. Le comportement observable depuis `strace` et Wireshark est identique, car l'optimisation du compilateur ne modifie pas la logique des appels réseau — seulement leur implémentation interne en assembleur.

Utilisez `dropper_O0` par commodité ; les messages de débogage (`printf`) seront visibles sur la sortie standard et vous aideront à corréler ce que vous voyez dans `strace`.

---

## Phase 1 — Triage rapide avant exécution

Avant de lancer quoi que ce soit, on applique le workflow de triage du [Chapitre 5](/05-outils-inspection-base/07-workflow-triage-rapide.md). Même si on sait que ce binaire est notre sample pédagogique, on maintient la discipline.

### `file` — Nature du binaire

```bash
$ file dropper_O0
dropper_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, with debug_info, not stripped  
```

On confirme qu'il s'agit d'un ELF 64-bit PIE, dynamiquement lié. La mention `with debug_info, not stripped` nous indique que les symboles DWARF sont présents — c'est la variante `_O0`.

### `strings` — Premières chaînes suspectes

```bash
$ strings dropper_O0 | grep -iE '(127\.|connect|send|recv|socket|c2|drop|/tmp|beacon|handshake|cmd|0x)'
```

On cherche des indicateurs d'activité réseau et de comportement suspect. Parmi les chaînes que l'on s'attend à trouver :

- **`127.0.0.1`** — L'adresse IP du C2 (hardcodée).  
- **`4444`** — Visible comme entier, parfois comme chaîne dans les messages de debug.  
- **`DRP-1.0`** — La chaîne de version envoyée dans le handshake.  
- **`/tmp/`** — Le répertoire de dépôt des payloads.  
- Des chaînes comme `Connecting to C2`, `Sending handshake`, `EXEC command`, `DROP`, `beacon` — les messages de debug qui trahissent la logique du programme.  
- Des chaînes de gestion d'erreur : `exec_failed`, `write_fail`, `bad_len`, `unknown_cmd`.

> 💡 **Point RE** — Dans un vrai malware, ces chaînes seraient absentes, chiffrées ou obfusquées. Ici, elles sont en clair car le sample est pédagogique. En situation réelle, `strings` retournerait beaucoup moins d'indices, et il faudrait s'appuyer davantage sur l'analyse dynamique et le désassemblage. Néanmoins, même un malware sophistiqué peut laisser fuiter des chaînes de bibliothèques standard (messages d'erreur de `libc`, chemins de fichiers système).

### `checksec` — Protections du binaire

```bash
$ checksec --file=dropper_O0
```

Le `Makefile` compile avec `-fstack-protector-strong`, `-pie`, `-Wl,-z,relro,-z,now` et `-D_FORTIFY_SOURCE=2`. On s'attend à voir :

- **RELRO** : Full — la GOT est en lecture seule après le chargement.  
- **Stack canary** : Enabled — protection contre les débordements de pile.  
- **NX** : Enabled — la pile n'est pas exécutable.  
- **PIE** : Enabled — le binaire est à adresse de base aléatoire.

Ces protections sont typiques d'un binaire compilé avec une toolchain moderne. Elles n'empêchent pas l'analyse, mais il est bon de les noter dans le rapport.

### `ldd` — Dépendances dynamiques

```bash
$ ldd dropper_O0
```

On vérifie les bibliothèques liées. Le dropper n'utilise que la `libc` standard — pas de bibliothèque réseau spécialisée comme `libcurl` ou `libssl`. Cela signifie que toute la communication passe par les sockets POSIX brutes (`socket`, `connect`, `send`, `recv`), ce qui est cohérent avec un protocole custom non chiffré.

> 💡 **Point RE** — L'absence de `libssl`/`libcrypto` dans les dépendances est une information précieuse : elle indique que le trafic réseau n'est probablement **pas chiffré en TLS**. Les données transitent en clair (ou avec un encodage maison), ce qui rendra la capture Wireshark directement lisible.

---

## Phase 2 — Tracer les appels système avec `strace`

### Lancer le dropper sans serveur C2

La manière la plus simple de commencer est de lancer le dropper **sans aucun serveur en écoute** sur le port 4444. Le dropper va tenter de se connecter, échouer, réessayer, puis abandonner. Ce scénario d'échec est déjà riche en informations.

```bash
$ strace -f -e trace=network,write -o dropper_trace.log ./dropper_O0
```

Décortiquons les options :

- **`-f`** — Suit les processus fils. Si le dropper utilise `fork()` ou `system()`, on ne perdra pas les appels système des enfants.  
- **`-e trace=network,write`** — Filtre pour n'afficher que les appels système réseau (`socket`, `connect`, `bind`, `listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `shutdown`, `setsockopt`, `getsockopt`...) et `write` (pour voir les écritures sur `stdout`/`stderr`). Sans ce filtre, la sortie serait noyée dans des centaines d'appels à `mmap`, `brk`, `fstat`, etc.  
- **`-o dropper_trace.log`** — Redirige la sortie de `strace` dans un fichier pour analyse ultérieure. La sortie de `strace` va sur stderr et celle du programme sur stdout ; les séparer facilite la lecture.

### Lecture de la trace : connexion échouée

Voici ce que l'on observe typiquement dans `dropper_trace.log` lorsque aucun serveur n'écoute :

```
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3  
connect(3, {sa_family=AF_INET, sin_port=htons(4444),  
        sin_addr=inet_addr("127.0.0.1")}, 16) = -1 ECONNREFUSED
close(3)
```

Cette séquence de trois lignes nous apprend déjà beaucoup :

1. **`socket(AF_INET, SOCK_STREAM, IPPROTO_IP)`** — Le dropper crée une socket **TCP** (`SOCK_STREAM`) en IPv4 (`AF_INET`). Le descripteur retourné est `3` (les descripteurs 0, 1, 2 sont stdin, stdout, stderr).

2. **`connect(3, {..., sin_port=htons(4444), sin_addr=inet_addr("127.0.0.1")}, 16)`** — Il tente de se connecter à **127.0.0.1:4444**. L'adresse et le port sont nos premiers **IOC réseau**.

3. **`= -1 ECONNREFUSED`** — La connexion échoue car rien n'écoute sur ce port. Le noyau a renvoyé un RST.

Si le dropper est configuré pour réessayer (ce qui est le cas de notre sample avec `MAX_RETRIES = 3`), on verra cette séquence se répéter, entrecoupée d'appels à `nanosleep` ou `clock_nanosleep` correspondant aux pauses entre les tentatives.

### Lancer le dropper avec un serveur factice

Pour aller plus loin, on a besoin que la connexion **réussisse**. Le plus simple est d'ouvrir un listener `netcat` sur le port 4444 :

```bash
# Terminal 1 : listener minimal
$ nc -lvnp 4444
```

```bash
# Terminal 2 : dropper sous strace
$ strace -f -e trace=network -tt -o dropper_trace_connected.log ./dropper_O0
```

L'option **`-tt`** ajoute des timestamps en microsecondes à chaque appel système, ce qui sera précieux pour corréler avec la capture Wireshark.

Cette fois, la trace montre une connexion réussie :

```
14:23:05.123456 socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
14:23:05.123789 connect(3, {sa_family=AF_INET, sin_port=htons(4444),
                sin_addr=inet_addr("127.0.0.1")}, 16) = 0
```

Le `= 0` à la fin de `connect` confirme le succès. Immédiatement après, on observe le **handshake** :

```
14:23:05.124012 sendto(3, "\336\20\24\0myhost\0...", 24, 0, NULL, 0) = 24
```

Ici, `\336` est `0xDE` en octal — c'est le **magic byte** du protocole. Le type `\20` est `0x10` = `MSG_HANDSHAKE`. Les deux octets suivants `\24\0` encodent la longueur du body en little-endian (`0x0014` = 20 octets). Le body contient le hostname, le PID et la version, séparés par des null bytes.

> 💡 **Point RE** — `strace` affiche les buffers en notation C échappée (octale ou hexadécimale selon les caractères). Les octets non imprimables apparaissent sous forme `\xNN` ou `\NNN`. Pour les buffers longs, `strace` tronque par défaut à 32 octets. L'option **`-s 4096`** augmente cette limite et permet de voir l'intégralité des messages échangés.

### Tracer avec des buffers complets

Pour capturer l'intégralité des données échangées, on augmente la taille des buffers affichés :

```bash
$ strace -f -e trace=network -tt -s 4096 -x \
    -o dropper_trace_full.log ./dropper_O0
```

L'option **`-x`** force l'affichage en hexadécimal (`\xde` au lieu de `\336`), plus lisible pour l'analyse de protocoles binaires.

### Filtrer spécifiquement `send` et `recv`

Si la trace est trop verbeuse, on peut restreindre davantage :

```bash
$ strace -f -e trace=sendto,recvfrom,send,recv,read,write \
    -tt -s 4096 -x -o dropper_io.log ./dropper_O0
```

> 💡 **Subtilité** — Selon la version de la `libc` et les flags de compilation, les fonctions `send()` et `recv()` du code C peuvent être implémentées en interne par les syscalls `sendto()` et `recvfrom()` (avec les arguments d'adresse à `NULL`). C'est pourquoi on inclut les deux variantes dans le filtre. Vérifiez dans votre trace lequel apparaît effectivement.

---

## Phase 3 — Capturer le trafic avec Wireshark

### Capture sur l'interface loopback

Puisque le dropper se connecte à `127.0.0.1`, le trafic passe par l'interface **loopback** (`lo`). C'est un point que les débutants oublient souvent : Wireshark ne capture par défaut que sur les interfaces physiques.

Pour capturer avec `tcpdump` (en ligne de commande, plus léger dans une VM) :

```bash
# Terminal 3 : capture du trafic sur loopback, port 4444
$ sudo tcpdump -i lo -w dropper_capture.pcap port 4444
```

L'option **`-w`** écrit les paquets bruts dans un fichier `.pcap`, que l'on pourra ouvrir dans Wireshark pour une analyse graphique ultérieure. Le filtre **`port 4444`** évite de capturer tout le trafic loopback parasite.

Avec Wireshark en mode graphique, on sélectionne l'interface `Loopback: lo` et on applique le filtre de capture `port 4444`.

### Chronologie d'une session complète

En lançant simultanément `tcpdump` (ou Wireshark), le listener `nc`, puis le dropper sous `strace`, on obtient une capture contenant toute la session. Voici ce que l'on observe dans l'ordre :

**1. Handshake TCP (3-way handshake)**

Les trois premiers paquets sont le handshake TCP classique :

```
SYN        →  dropper → 127.0.0.1:4444  
SYN-ACK    ←  127.0.0.1:4444 → dropper  
ACK        →  dropper → 127.0.0.1:4444  
```

Ceci correspond au `connect()` vu dans `strace`. Le timestamp du SYN dans Wireshark doit correspondre (à quelques microsecondes près) au timestamp du `connect()` dans la trace.

**2. Handshake applicatif**

Immédiatement après l'établissement TCP, le dropper envoie son premier message applicatif. Dans Wireshark, on voit un segment TCP avec un payload commençant par l'octet `0xDE` :

```
PSH-ACK    →  Payload: de 10 14 00 6d 79 68 6f 73 74 00 ...
```

En décomposant :

| Offset | Octets | Signification |  
|---|---|---|  
| `0x00` | `DE` | Magic byte (`PROTO_MAGIC`) |  
| `0x01` | `10` | Type = `0x10` (`MSG_HANDSHAKE`) |  
| `0x02–0x03` | `14 00` | Longueur body = 20 octets (little-endian: `0x0014`) |  
| `0x04+` | `6D 79 68 6F 73 74 00 ...` | Body : `"myhost\0"` + PID + version |

C'est exactement ce que `strace` montrait côté syscall. La corrélation est immédiate.

**3. Silence ou timeout**

Puisque `nc` ne parle pas le protocole du dropper, le serveur factice ne renvoie pas de réponse. Le dropper reste bloqué sur son `recv()`, en attente de l'ACK du handshake. Côté `strace`, on voit :

```
14:23:05.124500 recvfrom(3, ^C  <-- bloqué ici
```

Le `recvfrom` ne retourne jamais. Le dropper est en attente passive. C'est un comportement typique d'un client qui attend une réponse de son serveur.

> 💡 **Point RE** — Cette observation nous apprend quelque chose d'important sur le protocole : le dropper **attend une réponse** après le handshake. Le protocole est donc de type **requête/réponse**, et non un flux unidirectionnel. Cette information guidera la reconstruction du protocole en section 28.3.

---

## Phase 4 — Corrélation `strace` / Wireshark

La puissance de l'approche réside dans la **corrélation entre les deux sources**. Voici comment procéder systématiquement.

### Aligner les timestamps

`strace` avec `-tt` donne des timestamps au format `HH:MM:SS.µµµµµµ`. Wireshark affiche des timestamps relatifs ou absolus (configurable dans `View > Time Display Format > Time of Day`). En alignant les deux, chaque `sendto()` dans `strace` correspond à un segment TCP PSH-ACK dans Wireshark, et chaque `recvfrom()` correspond à la réception d'un segment contenant des données.

### Vérifier la taille des buffers

`strace` indique la valeur de retour de `sendto()` — le nombre d'octets effectivement envoyés. Wireshark indique la taille du payload TCP. Les deux doivent correspondre. Si `strace` montre `sendto(...) = 33` et Wireshark montre un payload TCP de 33 octets, tout est cohérent.

Un écart pourrait indiquer que la `libc` effectue du buffering (rare pour des sockets TCP en mode `SOCK_STREAM` sans `setvbuf` explicite), ou que le noyau a fragmenté l'envoi en plusieurs segments TCP. Dans ce cas, la somme des payloads TCP dans Wireshark doit égaler le retour de `sendto`.

### Construire le tableau de correspondance

Un tableau de ce type est le livrable attendu à la fin de cette phase :

| # | Timestamp | Syscall (`strace`) | Direction | Paquet Wireshark | Taille | Observations |  
|---|---|---|---|---|---|---|  
| 1 | 14:23:05.123 | `socket(AF_INET, SOCK_STREAM, 0) = 3` | — | — | — | Création socket TCP |  
| 2 | 14:23:05.123 | `connect(3, {127.0.0.1:4444}) = 0` | → | SYN → SYN-ACK → ACK | — | Connexion TCP établie |  
| 3 | 14:23:05.124 | `sendto(3, "\xde\x10...", 24) = 24` | → | PSH-ACK, payload 24 oct. | 24 | Handshake (magic=0xDE, type=0x10) |  
| 4 | 14:23:05.124 | `recvfrom(3, ...)` bloqué | ← | (aucune réponse) | — | Attente ACK serveur |

Ce tableau constitue la base de l'analyse protocolaire qui sera approfondie en section 28.3.

---

## Aller plus loin : `strace` avancé

### Tracer les accès fichiers en parallèle

Le dropper ne fait pas que du réseau — il peut écrire des fichiers (commande `CMD_DROP`). On peut capturer **tout** en une seule trace :

```bash
$ strace -f -e trace=network,open,openat,write,close,execve,chmod \
    -tt -s 4096 -x -o dropper_full_trace.log ./dropper_O0
```

Les appels `openat`, `write`, `chmod` et `execve` permettront de voir le dropper déposer un fichier dans `/tmp/` et l'exécuter — ce sera visible quand on lui enverra une commande `CMD_DROP` depuis un vrai faux C2 (section 28.4).

### Compter les appels système

Pour obtenir un résumé statistique plutôt qu'une trace détaillée :

```bash
$ strace -c ./dropper_O0
```

Le flag `-c` produit un tableau récapitulatif en fin d'exécution, indiquant le nombre de fois que chaque syscall a été invoqué, le temps total passé dans chacun, et le pourcentage de temps. C'est utile pour repérer rapidement si un binaire passe l'essentiel de son temps dans des appels réseau (suspect), dans des `read`/`write` de fichiers, ou dans `nanosleep` (attente entre les tentatives).

### `ltrace` en complément

Alors que `strace` trace les appels *système* (interface noyau), `ltrace` trace les appels de *bibliothèque* (fonctions `libc`). Pour un binaire réseau, `ltrace` montrera les appels à `inet_pton`, `htons`, `gethostname`, `popen`, `fwrite`, `chmod` — un niveau d'abstraction légèrement supérieur à celui des syscalls.

```bash
$ ltrace -e 'inet_pton+connect+send+recv+popen+fwrite+chmod' \
    ./dropper_O0
```

> ⚠️ **Limitation** — `ltrace` ne fonctionne pas sur les binaires entièrement statiques, et son support peut être irrégulier sur les binaires PIE avec Full RELRO selon la distribution. Si `ltrace` ne produit rien, ce n'est pas un bug de votre installation — c'est une limitation connue. `strace` reste l'outil de référence pour cette phase.

---

## Ce que l'on sait après cette première phase

À ce stade, sans avoir ouvert le moindre désassembleur, on dispose déjà des informations suivantes :

**IOC réseau identifiés :**
- Adresse C2 : `127.0.0.1` (localhost — en production, ce serait une IP ou un domaine externe)  
- Port : `4444/tcp`  
- Transport : TCP (`SOCK_STREAM`)  
- Pas de chiffrement TLS (absence de `libssl` dans `ldd`)

**Comportement observé :**
- Le dropper initie la connexion (il est **client**, pas serveur)  
- Il envoie un premier message immédiatement après la connexion (handshake)  
- Le message commence par `0xDE` (magic byte) suivi d'un type et d'une longueur  
- Il attend ensuite une réponse du serveur (protocole requête/réponse)  
- En cas d'échec de connexion, il réessaie avec un intervalle de quelques secondes  
- Il abandonne après un nombre fini de tentatives

**Hypothèses à vérifier dans les sections suivantes :**
- Le format `[magic][type][length][body]` semble constant — à confirmer sur davantage de messages.  
- L'encodage XOR visible dans les `strings` (`0x5A`) concerne probablement le body de certains messages — à vérifier en interceptant les buffers avec Frida (section 28.2).  
- Le set de commandes supporté et la machine à états complète restent à reconstruire (section 28.3).

---

## Résumé des commandes clés

| Commande | Rôle |  
|---|---|  
| `strace -f -e trace=network -tt -s 4096 -x -o trace.log ./binary` | Trace complète des appels réseau avec timestamps et buffers hex |  
| `strace -c ./binary` | Statistiques résumées des syscalls |  
| `ltrace -e 'connect+send+recv' ./binary` | Trace des appels libc réseau |  
| `sudo tcpdump -i lo -w capture.pcap port 4444` | Capture du trafic loopback sur le port C2 |  
| `nc -lvnp 4444` | Listener TCP minimal pour accepter la connexion |

---

> **À suivre** — En section 28.2, on passera de l'observation passive à l'**instrumentation active** : Frida nous permettra de hooker `connect`, `send` et `recv` pour intercepter les buffers *avant* et *après* l'encodage XOR, directement dans l'espace mémoire du processus.

⏭️ [Hooker les sockets avec Frida (intercepter `connect`, `send`, `recv`)](/28-dropper/02-hooker-sockets-frida.md)
