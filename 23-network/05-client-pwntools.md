🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 23.5 — Écrire un client de remplacement complet avec `pwntools`

> 🎯 **Objectif de cette section** : transformer toute la connaissance accumulée depuis la section 23.1 en un client Python autonome capable de dialoguer avec le serveur sans le binaire client original. Ce client implémente le protocole de zéro à partir de la spécification reconstruite par reverse engineering. À la fin de cette section, vous disposerez d'un outil réutilisable, scriptable, et extensible qui remplace intégralement le client C d'origine.

---

## Du replay au client autonome

À la section 23.4, le replay adaptatif a prouvé que notre compréhension du protocole est complète. Mais un script de replay reste tributaire des messages capturés : il les rejoue (éventuellement en les adaptant), sans pouvoir envoyer de nouvelles commandes ni interagir librement avec le serveur.

Un **client de remplacement** va plus loin. Il implémente le protocole en tant que bibliothèque : chaque type de message est une fonction Python qui construit le paquet de zéro, l'envoie, lit la réponse, et retourne le résultat sous une forme exploitable. On peut ensuite composer ces fonctions pour écrire des scénarios arbitraires — tests automatisés, fuzzing ciblé, exploration de commandes non documentées, ou extraction de données.

On utilise `pwntools` pour plusieurs raisons :

- **Gestion robuste des sockets TCP** : la classe `remote()` gère les connexions, les timeouts, et les erreurs de manière concise.  
- **Packing/unpacking binaire** : les fonctions `p16()`, `u16()`, `p8()` etc. avec contrôle de l'endianness simplifient la construction et le parsing des paquets.  
- **Logging intégré** : `log.info()`, `log.success()`, `log.error()` produisent une sortie structurée et colorée qui facilite le débogage.  
- **Intégration GDB** : en cas de besoin, `pwntools` peut attacher GDB au processus distant via `gdb.attach()`.  
- **Familiarité CTF** : `pwntools` est l'outil standard dans la communauté RE/CTF, le code produit ici sera directement réutilisable dans d'autres contextes.

---

## Architecture du client

On structure le client en trois couches, du plus bas niveau au plus haut :

```
┌─────────────────────────────────────────────┐
│  Couche 3 — Scénarios (main, scripts)       │
│  Compose les opérations pour des workflows  │
│  complets : session interactive, extraction │
│  de fichiers, bruteforce, fuzzing…          │
├─────────────────────────────────────────────┤
│  Couche 2 — Opérations protocolaires        │
│  do_handshake(), do_auth(), do_command(),   │
│  do_quit() — une fonction par phase du      │
│  protocole                                  │
├─────────────────────────────────────────────┤
│  Couche 1 — Transport (send/recv binaire)   │
│  proto_send(), proto_recv() — sérialisation │
│  et désérialisation des paquets             │
└─────────────────────────────────────────────┘
```

Cette séparation en couches est une bonne pratique générale quand on écrit un client RE : elle rend le code testable, lisible, et facilement adaptable si le protocole évolue (par exemple entre deux versions du binaire).

---

## Couche 1 — Transport

La couche transport encapsule la sérialisation des paquets. Elle ne connaît pas la sémantique des messages (HELLO, AUTH, CMD…) — elle sait uniquement construire un paquet avec un header de 4 octets et un payload de taille variable, et parser l'inverse à la réception.

```python
#!/usr/bin/env python3
"""
ch23_client.py  
Client de remplacement pour le protocole ch23-network.  
Reconstruit par reverse engineering — Formation RE, Chapitre 23.  
"""

from pwn import *

# ═══════════════════════════════════════════
#  Constantes du protocole
# ═══════════════════════════════════════════

PROTO_MAGIC    = 0xC0  
HEADER_SIZE    = 4  
CHALLENGE_LEN  = 8  

# Types de messages
MSG_HELLO_REQ  = 0x01  
MSG_AUTH_REQ   = 0x02  
MSG_CMD_REQ    = 0x03  
MSG_QUIT_REQ   = 0x04  
MSG_HELLO_RESP = 0x81  
MSG_AUTH_RESP  = 0x82  
MSG_CMD_RESP   = 0x83  
MSG_QUIT_RESP  = 0x84  
MSG_ERROR      = 0xFF  

# Commandes
CMD_PING       = 0x01  
CMD_LIST       = 0x02  
CMD_READ       = 0x03  
CMD_INFO       = 0x04  

# Status
STATUS_OK      = 0x01  
STATUS_FAIL    = 0x00  

# Codes d'erreur
ERR_BAD_MAGIC      = 0x01  
ERR_BAD_TYPE       = 0x02  
ERR_WRONG_STATE    = 0x03  
ERR_AUTH_FAIL      = 0x04  
ERR_BAD_CMD        = 0x05  

# Noms lisibles pour le logging
MSG_NAMES = {
    0x01: "HELLO_REQ",   0x81: "HELLO_RESP",
    0x02: "AUTH_REQ",    0x82: "AUTH_RESP",
    0x03: "CMD_REQ",     0x83: "CMD_RESP",
    0x04: "QUIT_REQ",    0x84: "QUIT_RESP",
    0xFF: "ERROR",
}

CMD_NAMES = {
    0x01: "PING", 0x02: "LIST", 0x03: "READ", 0x04: "INFO",
}
```

Les constantes sont extraites directement du désassemblage (section 23.2). On les regroupe en tête de fichier pour faciliter la mise à jour si une nouvelle version du binaire modifie les valeurs.

### Fonctions de transport

```python
# ═══════════════════════════════════════════
#  Couche 1 — Transport
# ═══════════════════════════════════════════

def proto_send(r, msg_type, payload=b""):
    """
    Envoyer un message protocolaire.
    
    Format sur le réseau :
        [0xC0][msg_type:1][payload_len:2 BE][payload:N]
    
    Args:
        r:        connexion pwntools (remote)
        msg_type: type de message (uint8)
        payload:  données du payload (bytes)
    """
    payload_len = len(payload)
    header = bytes([
        PROTO_MAGIC,
        msg_type,
        (payload_len >> 8) & 0xFF,   # big-endian high byte
        payload_len & 0xFF,           # big-endian low byte
    ])
    
    pkt = header + payload
    
    log.debug(f"TX [{MSG_NAMES.get(msg_type, hex(msg_type))}] "
              f"{payload_len} bytes payload")
    
    r.send(pkt)


def proto_recv(r):
    """
    Recevoir un message protocolaire.
    
    Returns:
        (msg_type, payload) — le type de message et les données brutes.
        
    Raises:
        EOFError si la connexion est fermée.
        Exception si le magic byte est invalide.
    """
    header = r.recvn(HEADER_SIZE)
    
    magic     = header[0]
    msg_type  = header[1]
    payload_len = (header[2] << 8) | header[3]
    
    if magic != PROTO_MAGIC:
        log.error(f"Bad magic byte: 0x{magic:02X} (expected 0x{PROTO_MAGIC:02X})")
        raise Exception("Protocol desync — bad magic byte")
    
    payload = r.recvn(payload_len) if payload_len > 0 else b""
    
    log.debug(f"RX [{MSG_NAMES.get(msg_type, hex(msg_type))}] "
              f"{payload_len} bytes payload")
    
    return msg_type, payload
```

Quelques points importants sur cette implémentation :

- **`r.recvn(n)`** lit exactement `n` octets, en bouclant si nécessaire. C'est l'équivalent `pwntools` de la fonction `recv_exact()` qu'on a vue dans le code C du serveur. Contrairement à `r.recv(n)` qui retourne *jusqu'à* `n` octets, `recvn` garantit que le buffer est complet. C'est indispensable pour un protocole binaire où chaque octet compte.

- **Le big-endian est géré manuellement** avec des shifts et des masques, exactement comme dans le code C du serveur. On pourrait aussi utiliser `struct.pack(">H", payload_len)` ou `p16(payload_len, endian='big')` de `pwntools` — les deux approches sont équivalentes.

- **Le magic byte est vérifié côté client.** Même si c'est nous qui écrivons le client, valider le magic byte en réception protège contre les décalages de flux (un octet perdu ou dupliqué par un bug réseau ferait dériver tout le parsing). C'est une bonne pratique de programmation défensive.

---

## Couche 2 — Opérations protocolaires

Chaque phase du protocole (handshake, authentification, commande, déconnexion) devient une fonction Python qui encapsule l'envoi de la requête, la réception de la réponse, et l'interprétation du résultat.

### Handshake

```python
# ═══════════════════════════════════════════
#  Couche 2 — Opérations protocolaires
# ═══════════════════════════════════════════

def do_handshake(r):
    """
    Effectuer le handshake HELLO.
    
    Envoie : [HELLO_REQ] "HELLO" + 3 octets de padding
    Attend : [HELLO_RESP] "WELCOME" + challenge (8 octets)
    
    Returns:
        Le challenge de 8 octets (bytes), nécessaire pour l'authentification.
    """
    # Construire le payload HELLO
    payload = b"HELLO" + b"\x00" * 3
    
    proto_send(r, MSG_HELLO_REQ, payload)
    
    msg_type, resp = proto_recv(r)
    
    # Gérer les erreurs
    if msg_type == MSG_ERROR:
        err_code = resp[0] if resp else 0
        err_msg  = resp[1:].decode("utf-8", errors="replace") if len(resp) > 1 else ""
        log.error(f"Server error on HELLO: [{err_code:#x}] {err_msg}")
        raise Exception("Handshake failed")
    
    if msg_type != MSG_HELLO_RESP:
        log.error(f"Unexpected response type: {msg_type:#x}")
        raise Exception("Handshake failed — unexpected response")
    
    # Parser la réponse : "WELCOME" (7 octets) + challenge (8 octets)
    if len(resp) < 7 + CHALLENGE_LEN:
        log.error(f"HELLO response too short: {len(resp)} bytes")
        raise Exception("Handshake failed — truncated response")
    
    banner    = resp[:7]
    challenge = resp[7:7 + CHALLENGE_LEN]
    
    if banner != b"WELCOME":
        log.warning(f"Unexpected banner: {banner}")
    
    log.success(f"Handshake OK — challenge: {challenge.hex()}")
    
    return challenge
```

### Authentification

```python
def xor_with_challenge(data, challenge):
    """XOR cyclique de data avec le challenge de 8 octets."""
    return bytes(
        d ^ challenge[i % CHALLENGE_LEN]
        for i, d in enumerate(data)
    )


def do_auth(r, username, password, challenge):
    """
    Effectuer l'authentification.
    
    Le mot de passe est XOR-é avec le challenge avant envoi.
    
    Payload AUTH :
        [user_len:1][username:N][pass_len:1][password_xored:M]
    
    Args:
        r:         connexion pwntools
        username:  identifiant (str)
        password:  mot de passe en clair (str)
        challenge: challenge reçu au handshake (bytes, 8 octets)
    
    Returns:
        True si l'authentification a réussi, False sinon.
    """
    user_bytes = username.encode("utf-8")
    pass_bytes = password.encode("utf-8")
    
    # XOR du mot de passe avec le challenge
    pass_xored = xor_with_challenge(pass_bytes, challenge)
    
    # Construire le payload : length-prefixed strings
    payload = (
        bytes([len(user_bytes)]) +
        user_bytes +
        bytes([len(pass_xored)]) +
        pass_xored
    )
    
    proto_send(r, MSG_AUTH_REQ, payload)
    
    msg_type, resp = proto_recv(r)
    
    if msg_type == MSG_ERROR:
        err_code = resp[0] if resp else 0
        err_msg  = resp[1:].decode("utf-8", errors="replace") if len(resp) > 1 else ""
        log.error(f"Server error on AUTH: [{err_code:#x}] {err_msg}")
        return False
    
    if msg_type != MSG_AUTH_RESP or len(resp) < 2:
        log.error(f"Unexpected AUTH response: type={msg_type:#x} len={len(resp)}")
        return False
    
    reserved = resp[0]
    status   = resp[1]
    
    if status == STATUS_OK:
        log.success(f"Authenticated as '{username}'")
        return True
    else:
        log.failure(f"Authentication failed (status={status:#x})")
        return False
```

Le XOR avec le challenge est implémenté exactement comme dans le code C du client (section 23.2). La correspondance octet pour octet entre notre implémentation Python et le code C original est vérifiable en capturant les deux avec Wireshark et en comparant les paquets.

### Commandes

```python
def do_command(r, cmd_id, args=b""):
    """
    Envoyer une commande et recevoir la réponse.
    
    Payload CMD_REQ :
        [command_id:1][args:N]
    
    Payload CMD_RESP :
        [status:1][data:N]
    
    Args:
        r:      connexion pwntools
        cmd_id: identifiant de commande (CMD_PING, CMD_LIST, etc.)
        args:   arguments de la commande (bytes)
    
    Returns:
        (status, data) — le code de status et les données de réponse.
    """
    cmd_name = CMD_NAMES.get(cmd_id, f"0x{cmd_id:02x}")
    
    payload = bytes([cmd_id]) + args
    proto_send(r, MSG_CMD_REQ, payload)
    
    msg_type, resp = proto_recv(r)
    
    if msg_type == MSG_ERROR:
        err_code = resp[0] if resp else 0
        err_msg  = resp[1:].decode("utf-8", errors="replace") if len(resp) > 1 else ""
        log.error(f"Server error on CMD {cmd_name}: [{err_code:#x}] {err_msg}")
        return STATUS_FAIL, b""
    
    if msg_type != MSG_CMD_RESP or len(resp) < 1:
        log.error(f"Unexpected CMD response: type={msg_type:#x}")
        return STATUS_FAIL, b""
    
    status = resp[0]
    data   = resp[1:] if len(resp) > 1 else b""
    
    if status == STATUS_OK:
        log.debug(f"CMD {cmd_name} OK — {len(data)} bytes data")
    else:
        log.warning(f"CMD {cmd_name} failed — status={status:#x}")
    
    return status, data


def do_ping(r):
    """Envoyer un PING et vérifier le PONG."""
    status, data = do_command(r, CMD_PING)
    if status == STATUS_OK and data == b"PONG":
        log.success("PING → PONG")
        return True
    return False


def do_list(r):
    """
    Lister les fichiers disponibles.
    
    Format de la réponse LIST :
        [count:1] puis pour chaque fichier :
        [index:1][name_len:1][name:N]
    
    Returns:
        Liste de tuples (index, nom_du_fichier).
    """
    status, data = do_command(r, CMD_LIST)
    
    if status != STATUS_OK or len(data) < 1:
        return []
    
    count  = data[0]
    files  = []
    offset = 1
    
    for _ in range(count):
        if offset + 2 > len(data):
            break
        
        file_index = data[offset]
        name_len   = data[offset + 1]
        offset += 2
        
        if offset + name_len > len(data):
            break
        
        name = data[offset : offset + name_len].decode("utf-8", errors="replace")
        offset += name_len
        
        files.append((file_index, name))
    
    return files


def do_read(r, file_index):
    """
    Lire le contenu d'un fichier par son index.
    
    Returns:
        Le contenu du fichier (str), ou None en cas d'erreur.
    """
    status, data = do_command(r, CMD_READ, bytes([file_index]))
    
    if status == STATUS_OK and data:
        return data.decode("utf-8", errors="replace")
    return None


def do_info(r):
    """
    Récupérer les informations du serveur.
    
    Returns:
        Chaîne d'information (str), ou None en cas d'erreur.
    """
    status, data = do_command(r, CMD_INFO)
    
    if status == STATUS_OK and data:
        return data.decode("utf-8", errors="replace")
    return None


def do_quit(r):
    """
    Envoyer la commande QUIT et recevoir l'acquittement.
    
    Returns:
        True si le serveur a répondu BYE.
    """
    proto_send(r, MSG_QUIT_REQ)
    
    msg_type, resp = proto_recv(r)
    
    if msg_type == MSG_QUIT_RESP and resp[:3] == b"BYE":
        log.info("Server acknowledged disconnect (BYE)")
        return True
    
    return False
```

Chaque fonction de la couche 2 suit le même schéma : construire le payload, appeler `proto_send`, appeler `proto_recv`, interpréter la réponse, retourner un résultat propre. Les fonctions spécialisées (`do_ping`, `do_list`, `do_read`, `do_info`) sont des raccourcis qui appellent `do_command` avec le bon `cmd_id` et parsent le format de réponse spécifique à chaque commande.

---

## Couche 3 — Scénarios

La couche 3 compose les opérations protocolaires en workflows complets. C'est ici qu'on retrouve la logique de haut niveau : établir une session complète, extraire tous les fichiers, ou mener des actions spécifiques.

### Session complète avec extraction de fichiers

```python
# ═══════════════════════════════════════════
#  Couche 3 — Scénarios
# ═══════════════════════════════════════════

def full_session(host, port, username, password):
    """
    Exécuter une session complète :
    handshake → auth → list → read all → quit.
    """
    r = remote(host, port)
    
    try:
        # ── Handshake ──
        log.info("Phase 1: Handshake")
        challenge = do_handshake(r)
        
        # ── Authentification ──
        log.info("Phase 2: Authentication")
        if not do_auth(r, username, password, challenge):
            log.error("Authentication failed — aborting.")
            r.close()
            return False
        
        # ── Informations serveur ──
        log.info("Phase 3: Server info")
        info = do_info(r)
        if info:
            log.info(f"Server info:\n{info}")
        
        # ── Ping ──
        do_ping(r)
        
        # ── Liste des fichiers ──
        log.info("Phase 4: File listing")
        files = do_list(r)
        
        if files:
            log.success(f"Found {len(files)} files:")
            for idx, name in files:
                log.info(f"  [{idx}] {name}")
            
            # ── Lecture de chaque fichier ──
            log.info("Phase 5: Reading all files")
            for idx, name in files:
                content = do_read(r, idx)
                if content:
                    log.success(f"── {name} ──")
                    print(content, end="")
                    if not content.endswith("\n"):
                        print()
        
        # ── Déconnexion ──
        log.info("Phase 6: Disconnect")
        do_quit(r)
        
        log.success("Session completed successfully.")
        return True
    
    except EOFError:
        log.error("Connection closed unexpectedly.")
        return False
    except Exception as e:
        log.error(f"Session error: {e}")
        return False
    finally:
        r.close()
```

### Point d'entrée

```python
# ═══════════════════════════════════════════
#  Point d'entrée
# ═══════════════════════════════════════════

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(
        description="ch23-network — Client de remplacement (pwntools)",
        epilog="Reconstruit par RE — Formation Reverse Engineering, Ch.23"
    )
    parser.add_argument("host", nargs="?", default="127.0.0.1",
                        help="Adresse du serveur (défaut: 127.0.0.1)")
    parser.add_argument("-p", "--port", type=int, default=4444,
                        help="Port TCP (défaut: 4444)")
    parser.add_argument("-u", "--user", default="admin",
                        help="Username (défaut: admin)")
    parser.add_argument("-P", "--password", default="s3cur3P@ss!",
                        help="Password (défaut: s3cur3P@ss!)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Activer le logging DEBUG")
    
    args = parser.parse_args()
    
    if args.verbose:
        context.log_level = "debug"
    else:
        context.log_level = "info"
    
    full_session(args.host, args.port, args.user, args.password)
```

---

## Exécution et validation

### Test nominal

```bash
# Terminal 1 : lancer le serveur
$ ./build/server_O0

# Terminal 2 : lancer le client Python
$ python3 ch23_client.py 127.0.0.1 -p 4444 -u admin -P 's3cur3P@ss!'
[+] Opening connection to 127.0.0.1 on port 4444: Done
[*] Phase 1: Handshake
[+] Handshake OK — challenge: b73a9e21f0884c17
[*] Phase 2: Authentication
[+] Authenticated as 'admin'
[*] Phase 3: Server info
[*] Server info:
    ch23-network server v1.0
    Protocol: custom binary
    Build: GCC 13.2.0
[+] PING → PONG
[*] Phase 4: File listing
[+] Found 4 files:
[*]   [0] readme.txt
[*]   [1] notes.txt
[*]   [2] config.dat
[*]   [3] flag.txt
[*] Phase 5: Reading all files
[+] ── readme.txt ──
Welcome to the secret server.  
Access level: CLASSIFIED  
[+] ── notes.txt ──
TODO: rotate encryption keys  
TODO: fix auth bypass in v2.1  
[+] ── config.dat ──
port=4444  
max_conn=16  
log_level=2  
[+] ── flag.txt ──
FLAG{pr0t0c0l_r3v3rs3d_succ3ssfully}
[*] Phase 6: Disconnect
[*] Server acknowledged disconnect (BYE)
[+] Session completed successfully.
[*] Closed connection to 127.0.0.1 port 4444
```

Le client Python produit exactement le même résultat que le client C original. Le flag est extrait.

### Validation croisée avec Wireshark

Pour prouver que notre client est protocolairement identique au client original, on capture les deux sessions avec Wireshark et on compare :

```bash
# Capture de la session du client C
$ sudo tcpdump -i lo -w session_c.pcap port 4444 &
$ ./build/client_O0 127.0.0.1
$ kill %1

# Capture de la session du client Python
$ sudo tcpdump -i lo -w session_py.pcap port 4444 &
$ python3 ch23_client.py
$ kill %1
```

En ouvrant les deux captures dans Wireshark et en comparant les "Follow TCP Stream", on vérifie que :

- Les headers sont identiques (même magic, mêmes types, mêmes longueurs).  
- Le payload HELLO est identique (`"HELLO"` + 3 octets de padding).  
- Le payload AUTH a la même structure (mêmes longueurs de username/password), seul le XOR diffère (car le challenge diffère entre les deux sessions).  
- Les payloads CMD sont identiques.  
- Le QUIT est identique.

Si un octet diffère de manière inattendue, c'est qu'il reste une incompréhension dans la spécification. On retourne alors au désassemblage (section 23.2) pour corriger.

---

## Utilisation avancée du client comme bibliothèque

L'architecture en couches du client permet de l'importer comme module Python dans d'autres scripts. On ne le lance plus via `main()` mais on utilise les fonctions directement.

### Exemple : extraction ciblée d'un fichier

```python
from ch23_client import *

context.log_level = "warning"   # réduire le bruit

r = remote("127.0.0.1", 4444)

challenge = do_handshake(r)  
do_auth(r, "analyst", "r3v3rs3M3", challenge)  

# Lire uniquement flag.txt (index 3)
content = do_read(r, 3)  
print(content)  

do_quit(r)  
r.close()  
```

### Exemple : tester tous les comptes connus

```python
from ch23_client import *

context.log_level = "error"

credentials = [
    ("admin",   "s3cur3P@ss!"),
    ("analyst", "r3v3rs3M3"),
    ("guest",   "guest123"),
    ("root",    "toor"),          # devrait échouer
    ("admin",   "wrongpass"),     # devrait échouer
]

for username, password in credentials:
    try:
        r = remote("127.0.0.1", 4444)
        challenge = do_handshake(r)
        success = do_auth(r, username, password, challenge)
        
        status = "✓" if success else "✗"
        print(f"  {status}  {username}:{password}")
        
        if success:
            do_quit(r)
        r.close()
    except Exception:
        print(f"  ✗  {username}:{password} (connection error)")
```

```
  ✓  admin:s3cur3P@ss!
  ✓  analyst:r3v3rs3M3
  ✓  guest:guest123
  ✗  root:toor
  ✗  admin:wrongpass
```

### Exemple : bruteforce de mots de passe

Le challenge change à chaque connexion, mais notre client le gère nativement. On peut donc écrire un bruteforce qui teste des mots de passe depuis une wordlist :

```python
from ch23_client import *

context.log_level = "error"

def try_login(host, port, username, password):
    """Tente une connexion et retourne True si AUTH réussit."""
    try:
        r = remote(host, port)
        challenge = do_handshake(r)
        result = do_auth(r, username, password, challenge)
        r.close()
        return result
    except Exception:
        return False

# Charger une wordlist
with open("/usr/share/wordlists/rockyou.txt", "r",
          errors="ignore") as f:
    passwords = [line.strip() for line in f][:1000]  # limiter pour le test

target_user = "admin"  
log.info(f"Bruteforcing '{target_user}' with {len(passwords)} passwords...")  

for i, pwd in enumerate(passwords):
    if try_login("127.0.0.1", 4444, target_user, pwd):
        log.success(f"Found password: {target_user}:{pwd}")
        break
    if (i + 1) % 100 == 0:
        log.info(f"  Tested {i+1}/{len(passwords)}...")
else:
    log.failure("Password not found in wordlist.")
```

> ⚠️ **Note** : le serveur implémente une limite de 3 tentatives par session (`MAX_AUTH_RETRIES`). Notre bruteforce crée une **nouvelle connexion** pour chaque tentative, ce qui contourne cette protection puisque le compteur est lié à la session. En conditions réelles, un serveur pourrait implémenter un rate-limiting par IP ou un délai croissant entre les tentatives — ce que notre serveur pédagogique ne fait pas volontairement.

### Exemple : exploration de commandes non documentées

Pendant le désassemblage, on a vu que le dispatch utilise un `switch` avec les commandes `0x01` à `0x04`. Que se passe-t-il si on envoie un `cmd_id` non documenté ? Notre client permet de tester facilement :

```python
from ch23_client import *

r = remote("127.0.0.1", 4444)  
challenge = do_handshake(r)  
do_auth(r, "admin", "s3cur3P@ss!", challenge)  

# Tester des commandes non documentées
for cmd_id in range(0x00, 0x10):
    try:
        status, data = do_command(r, cmd_id, b"\x00")
        result = data.decode("utf-8", errors="replace") if data else "(empty)"
        print(f"  CMD 0x{cmd_id:02X}: status={status:#x} data={result[:40]}")
    except Exception as e:
        print(f"  CMD 0x{cmd_id:02X}: error — {e}")
        # Reconnecter si la session a été coupée
        r.close()
        r = remote("127.0.0.1", 4444)
        challenge = do_handshake(r)
        do_auth(r, "admin", "s3cur3P@ss!", challenge)

r.close()
```

Ce type d'exploration est un classique du RE : on teste les limites du parseur pour découvrir des fonctionnalités cachées, des erreurs de gestion des cas limites, ou des comportements inattendus qui pourraient constituer des vulnérabilités.

---

## Debugging du client avec Wireshark et GDB

### Quand le client ne fonctionne pas

Si une commande échoue de manière inattendue, la méthodologie de débogage est la suivante :

1. **Activer le mode verbose** (`-v` ou `context.log_level = "debug"`) pour voir chaque paquet envoyé et reçu.

2. **Capturer avec Wireshark** pendant l'exécution du client Python, puis comparer le trafic avec une capture du client C original. La différence se trouve presque toujours dans un champ de payload mal construit.

3. **Poser un breakpoint GDB sur le handler côté serveur.** Par exemple, si le AUTH échoue, on lance le serveur sous GDB avec un breakpoint sur `handle_auth` (ou sur l'adresse équivalente si strippé), puis on exécute le client Python. Au breakpoint, on inspecte le buffer `payload` pour vérifier que les données reçues correspondent à ce que le client a envoyé :

```bash
$ gdb ./build/server_O0
(gdb) break handle_auth
(gdb) run
# ... dans un autre terminal, lancer le client Python
# ... GDB s'arrête sur handle_auth
(gdb) x/32bx payload      # examiner les 32 premiers octets du payload
(gdb) p payload_len        # vérifier la longueur
(gdb) p sess->challenge    # vérifier le challenge
```

4. **Comparer octet par octet** le payload reçu par le serveur avec celui que le client Python prétend avoir envoyé. Si un décalage apparaît, c'est généralement un problème de construction du payload : un champ de longueur calculé avec ou sans le terminateur null, un padding manquant, ou une endianness inversée.

### Erreur fréquente : oublier le padding du HELLO

Le payload HELLO du client C original fait 8 octets : `"HELLO"` (5 octets) + 3 octets de padding (`\x00\x00\x00`). Si le client Python envoie uniquement `b"HELLO"` (5 octets) avec un `payload_len` de 5, le serveur peut ne pas vérifier la taille exacte et accepter quand même — ou peut rejeter le paquet si le handler teste `payload_len < 8`. Ce type de détail n'est visible que dans le désassemblage, pas dans la capture réseau (où le padding se confond avec des octets nuls).

### Erreur fréquente : endianness du champ longueur

Si le `payload_len` est encodé en little-endian par erreur dans `proto_send`, un payload de 8 octets sera annoncé comme `0x0800` (2048) au lieu de `0x0008` (8). Le serveur tentera de lire 2048 octets de payload, ne les recevra jamais, et la connexion restera bloquée jusqu'au timeout. Ce bug est immédiatement visible dans Wireshark : les octets 2 et 3 de l'en-tête sont inversés par rapport à la capture de référence.

---

## Récapitulatif de la section

| Couche | Contenu | Rôle |  
|--------|---------|------|  
| 1 — Transport | `proto_send()`, `proto_recv()` | Sérialiser/désérialiser les paquets (magic, type, longueur, payload) |  
| 2 — Opérations | `do_handshake()`, `do_auth()`, `do_command()`, `do_quit()` + spécialisations | Encapsuler chaque phase du protocole |  
| 3 — Scénarios | `full_session()`, scripts d'exploration, bruteforce… | Composer les opérations en workflows complets |

Le client de remplacement produit dans cette section est le **livrable final du chapitre**. Il prouve que le protocole a été intégralement compris : chaque champ, chaque mécanisme (challenge XOR), chaque transition d'état est implémenté en Python, validé par une session complète avec le serveur, et vérifié par capture Wireshark. Le fichier `ch23_client.py` est aussi un **outil opérationnel** réutilisable pour l'exploration, le test, et l'automatisation d'interactions avec le serveur.

Le checkpoint du chapitre demandera d'utiliser ces techniques pour écrire un client capable de s'authentifier auprès du serveur et d'extraire le flag — exactement ce que ce script accomplit.

⏭️ [🎯 Checkpoint : écrire un client Python capable de s'authentifier auprès du serveur sans connaître le code source](/23-network/checkpoint.md)
