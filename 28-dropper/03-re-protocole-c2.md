🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 28.3 — RE du protocole C2 custom (commandes, encodage, handshake)

> 📍 **Objectif** — Consolider toutes les observations des sections 28.1 et 28.2 en une **spécification formelle et complète** du protocole C2. À la fin de cette section, vous disposerez d'un document de référence suffisant pour écrire un client ou un serveur compatible — ce qui est précisément l'objectif du checkpoint de ce chapitre.

---

## De l'observation à la spécification

Jusqu'ici, notre approche a été **bottom-up** : on a capturé des octets sur le réseau (`strace`, Wireshark), puis on a observé leur interprétation dans le processus (Frida). On a accumulé des faits isolés — un magic byte ici, un XOR là, un handshake suivi d'un ACK.

Cette section change de perspective. On passe en mode **top-down** : on rassemble tous les faits, on les organise, on comble les lacunes avec le désassemblage statique (Ghidra), et on produit un document de spécification cohérent. C'est exactement le travail d'un analyste malware qui rédige la section « Protocol Analysis » d'un rapport technique.

La méthodologie suit cinq étapes :

1. **Formaliser le format de trame** — Structure de l'en-tête, encodage des champs, contraintes de taille.  
2. **Documenter le handshake** — Séquence d'échanges initiale, contenu de chaque message.  
3. **Cataloguer les commandes** — Pour chaque type de message, documenter le format du body, l'encodage, et le comportement attendu.  
4. **Reconstruire la machine à états** — Quelles transitions sont valides ? Que se passe-t-il en cas d'erreur ?  
5. **Vérifier par le désassemblage** — Confirmer dans Ghidra que la spécification est complète et qu'aucune commande cachée n'a échappé à l'analyse dynamique.

---

## Étape 1 — Format de trame

### En-tête protocolaire

Chaque message du protocole commence par un en-tête fixe de **4 octets** :

```
 Byte 0       Byte 1       Bytes 2–3
┌────────────┬────────────┬────────────────────────┐
│   magic    │    type    │     body_length        │
│   (0xDE)   │   (uint8)  │ (uint16, little-endian)│
└────────────┴────────────┴────────────────────────┘
```

| Champ | Offset | Taille | Type | Description |  
|---|---|---|---|---|  
| `magic` | 0 | 1 octet | `uint8` | Constante `0xDE`. Permet de valider qu'un flux d'octets appartient bien à ce protocole. Si l'octet reçu ne vaut pas `0xDE`, le message est rejeté. |  
| `type` | 1 | 1 octet | `uint8` | Identifiant du type de message. Les plages `0x01–0x0F` sont réservées aux commandes serveur → client ; les plages `0x10–0x1F` aux réponses client → serveur. |  
| `body_length` | 2 | 2 octets | `uint16` | Taille du corps du message en octets, encodée en **little-endian** (octet de poids faible en premier). Valeur maximale observée : 4096 (`0x1000`). Un message sans body a `body_length = 0`. |

Le corps (*body*) suit immédiatement l'en-tête, pour une taille totale de `4 + body_length` octets par message.

### Observations sur le format

**Pas de checksum, pas de numéro de séquence.** Le protocole repose entièrement sur TCP pour la fiabilité et l'ordonnancement. C'est un choix courant dans les protocoles C2 simples : réduire la complexité (et la surface de détection) en déléguant ces aspects à la couche transport.

**Pas de délimiteur de fin de message.** La longueur explicite dans l'en-tête rend les délimiteurs inutiles. Le récepteur lit d'abord 4 octets (l'en-tête), extrait `body_length`, puis lit exactement ce nombre d'octets supplémentaires. C'est un protocole de type *length-prefixed*, par opposition aux protocoles *delimiter-based* comme HTTP/1.1 (qui utilise `\r\n\r\n`).

**Little-endian pour `body_length`.** C'est l'endianness native de x86-64. Le code du dropper utilise directement `memcpy` pour écrire et lire ce champ, sans conversion `htons`/`ntohs`. C'est un détail que l'on repère immédiatement dans le désassemblage : l'absence d'appel à des fonctions de conversion d'endianness sur ce champ (alors que `sin_port` dans `sockaddr_in` utilise bien `htons`).

---

## Étape 2 — Le handshake

### Diagramme de séquence

Le handshake intervient immédiatement après l'établissement de la connexion TCP. C'est un échange simple en deux messages :

```
     Dropper                                C2 Server
        │                                      │
        │──── TCP SYN ────────────────────────►│
        │◄─── TCP SYN-ACK ───────────────────  │
        │──── TCP ACK ────────────────────────►│
        │                                      │
        │   ┌─────────────────────────────┐    │
        │   │ MSG_HANDSHAKE (0x10)        │    │
        │──►│ body: hostname\0 PID\0 ver\0│───►│
        │   └─────────────────────────────┘    │
        │                                      │
        │   ┌─────────────────────────────┐    │
        │   │ MSG_ACK (0x13)              │    │
        │◄──│ body: (optionnel)           │◄───│
        │   └─────────────────────────────┘    │
        │                                      │
        │   ══ Session établie ══              │
        │   Le dropper entre dans la boucle    │
        │   de commandes (command_loop)        │
```

### MSG_HANDSHAKE (0x10) — Client → Serveur

Le premier message est **toujours** un `MSG_HANDSHAKE`. Le body contient trois chaînes ASCII null-terminated concaténées :

```
┌───────────────────┬───┬──────────────┬───┬─────────────────┬───┐
│    hostname       │\0 │   pid_str    │\0 │    version      │\0 │
│  (variable len)   │   │ (ASCII dec.) │   │ (ex: "DRP-1.0") │   │
└───────────────────┴───┴──────────────┴───┴─────────────────┴───┘
```

| Champ | Format | Exemple | Obtention |  
|---|---|---|---|  
| `hostname` | Chaîne ASCII null-terminated | `"analysis-vm"` | `gethostname()` |  
| `pid_str` | PID en ASCII décimal, null-terminated | `"1287"` | `getpid()` converti par `snprintf` |  
| `version` | Identifiant de version, null-terminated | `"DRP-1.0"` | Constante hardcodée `DROPPER_VERSION` |

**body_length** = `strlen(hostname) + 1 + strlen(pid_str) + 1 + strlen(version) + 1`

> 💡 **Point RE** — Le choix d'utiliser des chaînes null-terminated plutôt qu'un format TLV (Type-Length-Value) rend le parsing trivial en C (`strchr` ou simple itération), mais fragile : un hostname contenant un null byte casserait le protocole. Ce genre de fragilité est typique des protocoles C2 écrits rapidement — et peut constituer un vecteur de perturbation (envoyer un hostname malformé pour faire crasher le serveur C2 de l'attaquant).

### MSG_ACK (0x13) — Serveur → Client

Le serveur répond avec un `MSG_ACK`. Le body est **optionnel** — il peut contenir un message texte (ex : `"welcome"`) ou être vide (`body_length = 0`). Le dropper ne vérifie pas le contenu du body de l'ACK, seulement le type (`0x13`).

**Si le serveur ne répond pas** — Le dropper reste bloqué sur `recv_all()` indéfiniment. Il n'y a pas de timeout au niveau applicatif (pas de `SO_RCVTIMEO` positionné sur la socket). Seule une coupure TCP provoquera un retour d'erreur.

**Si le serveur répond avec un type ≠ 0x13** — Le dropper considère que le handshake a échoué, ferme la socket, et tente une reconnexion.

---

## Étape 3 — Catalogue des commandes

### Vue d'ensemble

Le protocole définit deux catégories de messages :

**Commandes Serveur → Client (plage `0x01–0x0F`) :**

| Type | Nom | Body | Encodage XOR | Réponse attendue |  
|---|---|---|---|---|  
| `0x01` | `CMD_PING` | Vide | Non | `MSG_PONG` (0x11) |  
| `0x02` | `CMD_EXEC` | Commande shell | **Oui** | `MSG_RESULT` (0x12) |  
| `0x03` | `CMD_DROP` | Fichier à déposer | **Oui** | `MSG_ACK` (0x13) |  
| `0x04` | `CMD_SLEEP` | Intervalle (uint32) | Non | `MSG_ACK` (0x13) |  
| `0x05` | `CMD_EXIT` | Vide | Non | `MSG_ACK` (0x13) |

**Réponses Client → Serveur (plage `0x10–0x1F`) :**

| Type | Nom | Body | Encodage XOR | Émis en réponse à |  
|---|---|---|---|---|  
| `0x10` | `MSG_HANDSHAKE` | Identification | Non | (initiatif) |  
| `0x11` | `MSG_PONG` | Vide | Non | `CMD_PING` |  
| `0x12` | `MSG_RESULT` | Sortie de commande | **Oui** | `CMD_EXEC` |  
| `0x13` | `MSG_ACK` | Texte optionnel | Non | `CMD_DROP`, `CMD_SLEEP`, `CMD_EXIT` |  
| `0x14` | `MSG_ERROR` | Message d'erreur | Non | Toute commande en erreur |  
| `0x15` | `MSG_BEACON` | Compteur + timestamp | Non | (périodique, initiatif) |

### CMD_PING (0x01) — Keepalive

Le message le plus simple du protocole. L'en-tête suffit, le body est vide (`body_length = 0`).

```
Serveur → Client :  DE 01 00 00  
Client  → Serveur :  DE 11 00 00     (MSG_PONG)  
```

Le PING sert de *heartbeat* depuis le serveur. Il vérifie que le dropper est toujours en vie et que la connexion TCP est fonctionnelle. Le dropper répond immédiatement avec un PONG, qui est lui aussi sans body.

### CMD_EXEC (0x02) — Exécution de commande shell

C'est la commande la plus dangereuse du protocole : elle permet l'exécution arbitraire de commandes sur la machine infectée.

**Format du body (serveur → client) :**

```
┌───────────────────────────────────────┐
│  commande shell (XOR-encodée, 0x5A)   │
│  PAS null-terminated                  │
│  longueur = body_length               │
└───────────────────────────────────────┘
```

Le body contient la commande shell encodée par XOR octet par octet avec la clé `0x5A`. La taille est donnée par `body_length` dans l'en-tête — il n'y a pas de null terminator dans le body encodé (le dropper ajoute le `\0` après décodage).

**Exemple** — Pour envoyer la commande `id` (3 octets) :

```
Texte clair :  69 64          ("id")  
XOR 0x5A    :  33 3E          (0x69⊕0x5A=0x33, 0x64⊕0x5A=0x3E)  

Message complet :  DE 02 02 00 33 3E
                   ── ── ───── ─────
                   │  │    │     └─ body XOR-encodé
                   │  │    └─ body_length = 2 (LE)
                   │  └─ type = CMD_EXEC
                   └─ magic
```

**Format de la réponse MSG_RESULT (0x12) :**

```
┌───────────────────────────────────────┐
│  sortie de la commande                │
│  (XOR-encodée, 0x5A)                  │
│  longueur = body_length               │
└───────────────────────────────────────┘
```

Le résultat (stdout de la commande exécutée via `popen`) est lui aussi encodé en XOR avant envoi. La taille maximale est limitée à `MAX_BODY_SIZE` (4096 octets) ; toute sortie plus longue est tronquée.

**En cas d'erreur** (commande inexistante, `popen` échoue), le dropper renvoie un `MSG_ERROR` (0x14) avec un texte d'erreur en clair (non XOR-encodé) : `"exec_failed"`.

### CMD_DROP (0x03) — Dépôt et exécution de fichier

Cette commande permet au C2 de déposer un fichier sur le système cible et de l'exécuter. C'est le mécanisme de *staging* du dropper — la raison d'être du malware.

**Format du body (serveur → client, XOR-encodé) :**

Après décodage XOR, le body a la structure suivante :

```
┌──────────────┬──────────────────────┬──────────────────────────┐
│ fname_len(1) │   filename           │      payload_data        │
│   uint8      │ (fname_len octets)   │ (body_len-1-fname_len)   │
│              │ PAS null-terminated  │                          │
└──────────────┴──────────────────────┴──────────────────────────┘
```

| Champ | Offset (après XOR) | Taille | Description |  
|---|---|---|---|  
| `fname_len` | 0 | 1 octet | Longueur du nom de fichier |  
| `filename` | 1 | `fname_len` octets | Nom du fichier (sans chemin) |  
| `payload_data` | `1 + fname_len` | le reste | Contenu brut du fichier à écrire |

Le dropper :
1. Décode le body entier avec XOR `0x5A`.  
2. Extrait le nom de fichier.  
3. Écrit `payload_data` dans `/tmp/<filename>`.  
4. Applique `chmod 0755` au fichier déposé.  
5. Exécute le fichier via `system()`.  
6. Renvoie un `MSG_ACK` avec le body `"drop_ok:<exit_code>"`.

**Exemple** — Déposer un script nommé `test.sh` contenant `#!/bin/sh\necho hello\n` :

```
Données en clair (avant XOR) :
  07                              ← fname_len = 7
  74 65 73 74 2e 73 68            ← "test.sh"
  23 21 2f 62 69 6e 2f 73 68 0a  ← "#!/bin/sh\n"
  65 63 68 6f 20 68 65 6c 6c 6f  ← "echo hello"
  0a                              ← "\n"

Après XOR 0x5A sur l'ensemble :
  5d 2e 3f 29 2e 74 29 32 79 7b 75 ...
```

**Validations** — Le dropper vérifie que `body_length ≥ 2` et que `fname_len + 1 < body_length`. Si ces conditions ne sont pas remplies, il renvoie `MSG_ERROR` avec `"too_short"` ou `"bad_fname"`. En cas d'échec d'écriture, il renvoie `"write_fail"`.

> ⚠️ **Point sécurité** — Le nom de fichier n'est **pas** assaini. Une injection de chemin (`../../../etc/cron.d/backdoor`) est théoriquement possible. Dans un vrai malware, ce serait voulu ; dans notre sample pédagogique, c'est un comportement réaliste conservé volontairement pour l'exercice d'analyse.

### CMD_SLEEP (0x04) — Modifier l'intervalle de beacon

Permet au C2 d'ajuster la fréquence des beacons du dropper, typiquement pour réduire le bruit réseau une fois l'implant installé.

**Format du body (pas de XOR) :**

```
┌──────────────────────────────────────┐
│  new_interval (uint32, little-endian)│
│  en secondes                         │
└──────────────────────────────────────┘
```

Le dropper borne la valeur entre 1 et 3600 secondes. Toute valeur hors de cette plage est clampée silencieusement (pas d'erreur).

```
Exemple : intervalle = 30 secondes
  DE 04 04 00    1E 00 00 00
  ── ── ─────    ───────────
  │  │    │         └─ 30 en uint32 LE
  │  │    └─ body_length = 4
  │  └─ CMD_SLEEP
  └─ magic
```

Réponse : `MSG_ACK` avec body `"sleep_ok"`.

### CMD_EXIT (0x05) — Termination

Ordonne au dropper de se terminer proprement.

```
Serveur → Client :  DE 05 00 00  
Client  → Serveur :  DE 13 03 00 62 79 65     (MSG_ACK, body="bye")  
```

Après l'envoi de l'ACK, le dropper met `state.running = 0`, sort de la `command_loop`, ferme la socket, et termine avec `return 0` depuis `main`. Pas de tentative de reconnexion.

### MSG_BEACON (0x15) — Heartbeat périodique (client → serveur)

Le dropper envoie un beacon à intervalles réguliers lorsqu'il n'a pas reçu de commande pendant `beacon_interval` secondes. Le beacon est **initiatif** (envoyé par le client sans sollicitation) et n'attend pas de réponse.

**Format du body (pas de XOR) :**

```
┌───────────────────────┬────────────────────────┐
│  cmd_count (uint32 LE)│  timestamp (uint32 LE) │
│  commandes traitées   │  epoch Unix (sec)      │
└───────────────────────┴────────────────────────┘
```

Le `cmd_count` permet au C2 de suivre l'activité du dropper. Le `timestamp` confirme que le dropper est vivant et donne l'heure locale de la machine infectée.

```
Exemple : 5 commandes traitées, timestamp 1714500000
  DE 15 08 00    05 00 00 00    A0 3E 33 66
  ── ── ─────    ───────────    ───────────
  │  │    │         │               └─ timestamp (LE)
  │  │    │         └─ cmd_count = 5 (LE)
  │  │    └─ body_length = 8
  │  └─ MSG_BEACON
  └─ magic
```

---

## Étape 4 — L'encodage XOR

### Périmètre de l'encodage

L'encodage XOR n'est **pas** appliqué uniformément à tous les messages. Il ne concerne que les bodies des commandes dont le contenu est « sensible » — les commandes shell et les fichiers déposés :

| Message | Body XOR-encodé ? |  
|---|---|  
| `MSG_HANDSHAKE` (0x10) | Non — le hostname et le PID transitent en clair |  
| `CMD_EXEC` (0x02) | **Oui** — la commande shell est encodée |  
| `MSG_RESULT` (0x12) | **Oui** — la sortie de la commande est encodée |  
| `CMD_DROP` (0x03) | **Oui** — le nom de fichier et le payload sont encodés |  
| Tous les autres | Non — ACK, PONG, BEACON, SLEEP, PING, EXIT transitent en clair |

> 💡 **Point RE** — Cette incohérence est typique des malwares réels : l'encodage est ajouté de manière opportuniste sur les canaux les plus « bruyants » (commandes et résultats), mais les métadonnées (handshake, beacons) sont laissées en clair. C'est une aubaine pour l'analyste : les beacons en clair facilitent la détection par signatures réseau (IDS/IPS).

### L'algorithme XOR

L'encodage est un **XOR mono-octet** avec la clé fixe `0x5A`, appliqué sur l'intégralité du body :

```
pour chaque octet b[i] du body :
    b[i] = b[i] ⊕ 0x5A
```

Le XOR étant son propre inverse, la même fonction sert à l'encodage et au décodage. Il n'y a pas de vecteur d'initialisation, pas de compteur, pas de chiffrement par bloc. C'est une **obfuscation**, pas un chiffrement — un analyste avec accès au trafic réseau et connaissant la clé peut décoder instantanément tout le contenu.

### Identifier la clé dans le désassemblage

Dans Ghidra, la fonction `xor_encode` se reconnaît par un pattern caractéristique :

```
; Boucle XOR dans xor_encode (vue Ghidra, syntaxe simplifiée)
LOOP:
    movzx  eax, byte [rdi + rcx]     ; charger l'octet courant
    xor    eax, 0x5a                  ; XOR avec la constante
    mov    byte [rdi + rcx], al       ; réécrire l'octet
    inc    rcx                        ; incrémenter le compteur
    cmp    rcx, rsi                   ; comparer avec la longueur
    jb     LOOP                       ; boucler si pas fini
```

Les éléments qui trahissent l'encodage XOR :

- **Une instruction `xor` avec une constante immédiate** (`0x5A`) dans une boucle. C'est le signal le plus fort.  
- **Un compteur incrémenté de 1** à chaque itération — on traite octet par octet.  
- **Le même buffer en lecture et en écriture** (`rdi` apparaît en source et en destination) — l'opération est *in-place*.  
- **La constante `0x5A` apparaît dans la section `.rodata` ou en immédiat** — cherchable via `Search > For Scalars` dans Ghidra.

> 💡 **Point RE** — En situation réelle, la clé XOR pourrait être dérivée dynamiquement (hash du timestamp, portion de la clé de registre, etc.). La recherche de `xor reg, imm8` dans une boucle reste le pattern de détection le plus fiable, quelle que soit la source de la clé.

---

## Étape 5 — Machine à états du dropper

### Diagramme d'états

Le dropper implémente une machine à états simple avec quatre états :

```
                    ┌──────────────────────────────────┐
                    │                                  │
                    ▼                                  │
             ┌─────────────┐                           │
             │ DISCONNECTED│                           │
             └──────┬──────┘                           │
                    │ connect() réussit                │
                    ▼                                  │
           ┌────────────────┐                          │
           │  HANDSHAKING   │                          │
           └───────┬────────┘                          │
                   │ Envoi HANDSHAKE                   │
                   │ Réception ACK                     │
                   ▼                                   │
           ┌────────────────┐     timeout              │
           │    COMMAND     │────────────┐             │
           │     LOOP       │            │             │
           └───┬──────┬─────┘            │             │
               │      │                  │             │
     CMD reçue │      │ CMD_EXIT         ▼             │
               │      │           ┌────────────┐       │
               ▼      │           │  BEACONING │       │
       ┌────────────┐ │           │  (send     │       │
       │  HANDLING  │ │           │   beacon)  │       │
       │  COMMAND   │ │           └─────┬──────┘       │
       └─────┬──────┘ │                 │              │
             │        │                 │ retour dans  │
             │ retour │                 │ command_loop │
             │ dans   │                 │              │
             │ loop   ▼                 │              │
             │   ┌──────────┐           │              │
             │   │TERMINATED│           │              │
             │   └──────────┘           │              │
             │                          │              │
             └──────────────────────────┘              │
                                                       │
              Erreur réseau / Handshake rejeté         │
              ─────────────────────────────────────────┘
              (reconnexion si retries < MAX_RETRIES)
```

### Transitions détaillées

| État source | Événement | Action | État destination |  
|---|---|---|---|  
| `DISCONNECTED` | `connect()` réussit | — | `HANDSHAKING` |  
| `DISCONNECTED` | `connect()` échoue | `sleep(BEACON_INTERVAL)`, `retries++` | `DISCONNECTED` (si `retries < MAX_RETRIES`) |  
| `DISCONNECTED` | `retries >= MAX_RETRIES` | — | `TERMINATED` |  
| `HANDSHAKING` | Envoi `MSG_HANDSHAKE` + réception `MSG_ACK` | `retries = 0` | `COMMAND_LOOP` |  
| `HANDSHAKING` | Réception type ≠ `MSG_ACK` | `close()`, `retries++` | `DISCONNECTED` |  
| `HANDSHAKING` | Erreur réseau | `close()`, `retries++` | `DISCONNECTED` |  
| `COMMAND_LOOP` | `select()` timeout | Envoi `MSG_BEACON` | `COMMAND_LOOP` |  
| `COMMAND_LOOP` | Réception `CMD_PING` | Envoi `MSG_PONG` | `COMMAND_LOOP` |  
| `COMMAND_LOOP` | Réception `CMD_EXEC` | Décodage XOR, `popen()`, envoi `MSG_RESULT` | `COMMAND_LOOP` |  
| `COMMAND_LOOP` | Réception `CMD_DROP` | Décodage XOR, écriture fichier, `system()`, envoi `MSG_ACK` | `COMMAND_LOOP` |  
| `COMMAND_LOOP` | Réception `CMD_SLEEP` | Mise à jour `beacon_interval`, envoi `MSG_ACK` | `COMMAND_LOOP` |  
| `COMMAND_LOOP` | Réception `CMD_EXIT` | Envoi `MSG_ACK("bye")`, `running = 0` | `TERMINATED` |  
| `COMMAND_LOOP` | Réception type inconnu | Envoi `MSG_ERROR("unknown_cmd")` | `COMMAND_LOOP` |  
| `COMMAND_LOOP` | Erreur réseau | `close()` | `DISCONNECTED` |

### Temporalité

La boucle de commandes repose sur `select()` avec un timeout égal à `beacon_interval` (par défaut 5 secondes). Le dropper ne poll pas activement — il dort entre les commandes, ce qui est plus discret qu'une boucle `recv` en mode bloquant pur car le beacon maintient la connexion vivante même en l'absence de commandes.

---

## Étape 6 — Vérification dans Ghidra

L'analyse dynamique (sections 28.1 et 28.2) nous a permis de reconstruire l'essentiel du protocole, mais il reste un risque : **des commandes non testées pourraient exister**. L'analyse dynamique ne couvre que les chemins effectivement empruntés. Si le C2 n'envoie jamais un certain type de commande pendant nos tests, on ne le verra pas.

Pour s'assurer de la complétude, on examine le `dispatch_command` dans Ghidra.

### Localiser le dispatcher

Deux approches complémentaires :

**Avec symboles (`dropper_O0`)** — Chercher `dispatch_command` dans le Symbol Tree. Double-cliquer pour naviguer vers la fonction.

**Sans symboles (`dropper_O2_strip`)** — Chercher le pattern du `switch/case`. Dans le code désassemblé, un `switch` sur `msg->header.type` se compile typiquement en une table de sauts (*jump table*) ou en une cascade de `cmp` / `je`. On peut localiser cette structure en cherchant les références croisées vers les handlers — par exemple, trouver la chaîne `"unknown_cmd"` (qui survit au stripping car elle est dans `.rodata`), remonter via XREF jusqu'à la branche `default` du `switch`, puis examiner les autres branches.

### Lire le switch dans le décompileur

Le décompileur de Ghidra produit un pseudo-code comme :

```c
int dispatch_command(dropper_state_t *state, proto_message_t *msg)
{
    state->cmd_count++;

    switch (msg->header.type) {
    case 1:     // CMD_PING
        return handle_ping(state);
    case 2:     // CMD_EXEC
        return handle_exec(state, msg->body, msg->header.length);
    case 3:     // CMD_DROP
        return handle_drop(state, msg->body, msg->header.length);
    case 4:     // CMD_SLEEP
        return handle_sleep(state, msg->body, msg->header.length);
    case 5:     // CMD_EXIT
        return handle_exit(state);
    default:
        return send_message(state->sockfd, 0x14, "unknown_cmd", 11);
    }
}
```

On vérifie que les `case` correspondent exactement aux types documentés (`0x01` à `0x05`). S'il existait un `case 6` ou `case 7` inconnu, on le verrait ici. L'absence de cas supplémentaires confirme que notre catalogue de commandes est complet.

> 💡 **Point RE** — Dans un malware réel, le dispatcher pourrait contenir des commandes « dormantes » — implémentées dans le code mais jamais invoquées pendant la période d'observation. L'analyse statique est le seul moyen de les découvrir. C'est pour cela que la combinaison statique + dynamique est fondamentale.

### Vérifier les handlers individuellement

Pour chaque handler (`handle_exec`, `handle_drop`, etc.), on vérifie dans Ghidra :

- **Le format attendu du body** — Correspond-il à notre documentation ?  
- **Les validations effectuées** — Quelles conditions provoquent un `MSG_ERROR` ?  
- **Les effets de bord** — Quels fichiers sont écrits ? Quelles commandes système sont exécutées ?  
- **Les chemins d'erreur** — Le handler peut-il crasher ? (ex : buffer overflow si `body_length > MAX_BODY_SIZE` — vérifier si la borne est correctement appliquée.)

Ce travail de vérification systématique transforme notre spécification « observée » en spécification « vérifiée ».

---

## Spécification récapitulative du protocole

### Résumé en une page

```
Protocole : Custom TCP, little-endian, length-prefixed  
Transport : TCP, port 4444  
Encodage  : XOR mono-octet (clé 0x5A) sur certains bodies  

En-tête (4 octets) :
  [0]     uint8   magic = 0xDE
  [1]     uint8   type
  [2..3]  uint16  body_length (little-endian)

Séquence d'initialisation :
  Client → Serveur : MSG_HANDSHAKE (0x10)
  Serveur → Client : MSG_ACK (0x13)

Commandes serveur → client :
  0x01  PING    body=∅           réponse=PONG(0x11)
  0x02  EXEC    body=cmd^XOR     réponse=RESULT(0x12, sortie^XOR)
  0x03  DROP    body=file^XOR    réponse=ACK(0x13, "drop_ok:N")
  0x04  SLEEP   body=uint32(sec) réponse=ACK(0x13, "sleep_ok")
  0x05  EXIT    body=∅           réponse=ACK(0x13, "bye")

Messages client → serveur (initiatifs) :
  0x10  HANDSHAKE  body=hostname\0+pid\0+version\0  (à la connexion)
  0x15  BEACON     body=uint32(count)+uint32(timestamp)  (périodique)

Erreurs :
  0x14  ERROR   body=texte en clair (non XOR)

Valeur XOR : 0x5A, appliqué octet par octet, in-place  
Périmètre  : bodies de EXEC(0x02), DROP(0x03), RESULT(0x12)  
```

### Format ImHex (`.hexpat`)

Pour les lecteurs qui souhaitent visualiser les trames capturées dans ImHex, le pattern `hexpat/ch23_protocol.hexpat` (protocole du [Chapitre 23](/23-network/README.md)) peut servir de point de départ. La structure du header est similaire (magic + type + longueur), mais les valeurs diffèrent : magic `0xDE` au lieu de celui du Chapitre 23, types de commandes `0x01–0x05` / `0x10–0x15`, et encodage XOR sur certains bodies. L'adaptation en un `ch28_protocol.hexpat` dédié constitue un bon exercice complémentaire.

---

## Corrélation avec les IOC

À ce stade, on peut formaliser les indicateurs de compromission réseau extraits de l'analyse :

| IOC | Type | Valeur | Confiance |  
|---|---|---|---|  
| Adresse C2 | IP | `127.0.0.1` | Haute (hardcodée) |  
| Port C2 | Port TCP | `4444` | Haute (hardcodée) |  
| Magic byte | Signature réseau | Premier octet du payload TCP = `0xDE` | Haute |  
| Beacon pattern | Signature réseau | `DE 15 08 00` suivi de 8 octets, toutes les ~5s | Haute |  
| Handshake pattern | Signature réseau | `DE 10` suivi de chaînes null-terminated contenant `DRP-` | Moyenne (version peut changer) |  
| Clé XOR | Paramètre crypto | `0x5A` | Haute (hardcodée) |

Ces IOC permettent de rédiger des règles de détection (Snort, Suricata, YARA réseau) sans même avoir besoin de comprendre la logique interne du dropper. Une règle Suricata minimale pourrait détecter le beacon :

```
alert tcp any any -> any 4444 (msg:"Dropper beacon detected";
    content:"|DE 15 08 00|"; offset:0; depth:4;
    sid:1000001; rev:1;)
```

---

> **À suivre** — En section 28.4, on met cette spécification en pratique en écrivant un **faux serveur C2 complet** en Python. On enverra chaque commande du catalogue au dropper et on observera son comportement en temps réel, validant définitivement notre compréhension du protocole.

⏭️ [Simuler un serveur C2 pour observer le comportement complet](/28-dropper/04-simuler-serveur-c2.md)
