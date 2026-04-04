#!/usr/bin/env python3
"""
solutions/ch23-checkpoint-client.py
Solution du checkpoint — Chapitre 23

Client de remplacement pour le protocole ch23-network,
reconstruit intégralement par reverse engineering à partir
des binaires strippés server_O2_strip et client_O2_strip.

⚠️  SPOILER — Consulter uniquement après avoir tenté le checkpoint.

Méthodologie suivie :
  1. Triage : file, strings, checksec, ldd sur les deux binaires.
  2. Observation : strace + Wireshark sur une session client/serveur.
  3. Hypothèses : magic byte 0xC0, header 4 octets, types 0x01-0x04
     (requêtes) et 0x81-0x84 (réponses), payload_len en big-endian.
  4. Désassemblage : Ghidra sur server_O2_strip — localisation du
     parseur via XREF sur recv/read, reconstruction des handlers,
     identification du XOR challenge dans handle_auth.
  5. Validation ImHex : pattern .hexpat sur l'export TCP brut.
  6. Replay : replay naïf (AUTH échoue), inversion du XOR pour
     récupérer le mot de passe, replay adaptatif (succès).
  7. Client autonome : ce fichier.

Credentials découvertes (strings + GDB breakpoint sur memcmp) :
  admin    / s3cur3P@ss!
  analyst  / r3v3rs3M3
  guest    / guest123

Usage :
  python3 ch23-checkpoint-client.py [host] [-p PORT] [-u USER] [-P PASS]
  python3 ch23-checkpoint-client.py 127.0.0.1 -p 4444
  python3 ch23-checkpoint-client.py 127.0.0.1 -u guest -P guest123
  python3 ch23-checkpoint-client.py 127.0.0.1 -v          # mode debug

Licence MIT — Usage strictement éducatif.
"""

from pwn import *
import argparse
import sys

# ═══════════════════════════════════════════════════════════════
#  Constantes du protocole (reconstruites par RE)
# ═══════════════════════════════════════════════════════════════
#
#  Header (4 octets) :
#    [magic:1] [msg_type:1] [payload_len:2 big-endian]
#
#  Convention du champ msg_type :
#    bit 7 = 0 → requête (client → serveur)
#    bit 7 = 1 → réponse (serveur → client)
#    0xFF      → erreur serveur
#
#  Machine à états :
#    CONNECTED ──HELLO──▶ HELLO_DONE ──AUTH──▶ AUTHENTICATED
#    AUTHENTICATED ──CMD──▶ AUTHENTICATED (boucle)
#    * ──QUIT──▶ DISCONNECTED
#
#  Authentification :
#    Le mot de passe est XOR-é avec le challenge (8 octets)
#    reçu dans la réponse HELLO, avant envoi dans le payload AUTH.
#    XOR cyclique : password[i] ^= challenge[i % 8]
# ═══════════════════════════════════════════════════════════════

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

# Commandes (payload[0] du CMD_REQ)
CMD_PING  = 0x01
CMD_LIST  = 0x02
CMD_READ  = 0x03
CMD_INFO  = 0x04

# Status de réponse
STATUS_OK   = 0x01
STATUS_FAIL = 0x00

# Noms pour le logging
MSG_NAMES = {
    0x01: "HELLO_REQ",  0x81: "HELLO_RESP",
    0x02: "AUTH_REQ",   0x82: "AUTH_RESP",
    0x03: "CMD_REQ",    0x83: "CMD_RESP",
    0x04: "QUIT_REQ",   0x84: "QUIT_RESP",
    0xFF: "ERROR",
}

CMD_NAMES = {
    0x01: "PING", 0x02: "LIST", 0x03: "READ", 0x04: "INFO",
}

ERR_NAMES = {
    0x01: "BAD_MAGIC",
    0x02: "BAD_TYPE",
    0x03: "WRONG_STATE",
    0x04: "AUTH_FAIL",
    0x05: "BAD_CMD",
    0x06: "PAYLOAD_TOO_LARGE",
}


# ═══════════════════════════════════════════════════════════════
#  Couche 1 — Transport
#
#  Sérialisation/désérialisation des paquets.
#  Ne connaît pas la sémantique des messages.
# ═══════════════════════════════════════════════════════════════

def proto_send(r, msg_type, payload=b""):
    """
    Construire et envoyer un message protocolaire.

    Format sur le réseau :
        [0xC0] [msg_type:1] [payload_len:2 BE] [payload:N]
    """
    plen = len(payload)
    header = bytes([
        PROTO_MAGIC,
        msg_type,
        (plen >> 8) & 0xFF,
        plen & 0xFF,
    ])
    r.send(header + payload)
    log.debug(f"TX → {MSG_NAMES.get(msg_type, f'0x{msg_type:02X}')}"
              f" | {plen} bytes payload"
              f" | {(header + payload[:16]).hex()}"
              f"{'...' if plen > 16 else ''}")


def proto_recv(r):
    """
    Recevoir et parser un message protocolaire.

    Returns:
        (msg_type, payload)

    Raises:
        EOFError      — connexion fermée
        ProtocolError — magic byte invalide
    """
    header = r.recvn(HEADER_SIZE)

    magic    = header[0]
    msg_type = header[1]
    plen     = (header[2] << 8) | header[3]

    if magic != PROTO_MAGIC:
        raise ProtocolError(
            f"Bad magic: 0x{magic:02X} (expected 0x{PROTO_MAGIC:02X})"
        )

    payload = r.recvn(plen) if plen > 0 else b""

    log.debug(f"RX ← {MSG_NAMES.get(msg_type, f'0x{msg_type:02X}')}"
              f" | {plen} bytes payload"
              f" | {(header + payload[:16]).hex()}"
              f"{'...' if plen > 16 else ''}")

    return msg_type, payload


class ProtocolError(Exception):
    """Erreur de niveau protocole (magic invalide, désync…)."""
    pass


class ServerError(Exception):
    """Erreur renvoyée explicitement par le serveur (MSG_ERROR)."""
    def __init__(self, code, message):
        self.code = code
        self.message = message
        super().__init__(
            f"Server error [{ERR_NAMES.get(code, f'0x{code:02X}')}]: "
            f"{message}"
        )


def check_server_error(msg_type, payload):
    """
    Vérifier si la réponse est un MSG_ERROR et lever une exception
    si c'est le cas. Appelé par les fonctions de couche 2.
    """
    if msg_type == MSG_ERROR:
        code = payload[0] if payload else 0x00
        text = payload[1:].decode("utf-8", errors="replace") \
               if len(payload) > 1 else "(no message)"
        raise ServerError(code, text)


# ═══════════════════════════════════════════════════════════════
#  Couche 2 — Opérations protocolaires
#
#  Une fonction par phase du protocole.
#  Construit les payloads, interprète les réponses.
# ═══════════════════════════════════════════════════════════════

def xor_with_challenge(data, challenge):
    """
    XOR cyclique de data avec le challenge de 8 octets.

    Découvert par RE dans la fonction d'authentification du serveur :
    une boucle qui itère sur chaque octet du mot de passe et le XOR
    avec challenge[i % CHALLENGE_LEN]. Le même code est présent côté
    client (pour encoder) et côté serveur (pour décoder).
    """
    return bytes(d ^ challenge[i % CHALLENGE_LEN]
                 for i, d in enumerate(data))


def do_handshake(r):
    """
    Phase 1 : Handshake HELLO.

    Envoie :  [HELLO_REQ] "HELLO" + 3 octets padding (total 8)
    Reçoit :  [HELLO_RESP] "WELCOME" (7) + challenge (8)

    Le serveur vérifie :
      - state == CONNECTED (sinon ERR_WRONG_STATE)
      - payload commence par "HELLO" (sinon ERR_BAD_TYPE)

    Le challenge est un nonce aléatoire de 8 octets généré par
    getrandom() côté serveur. Il change à chaque connexion.

    Returns:
        bytes — le challenge de 8 octets.
    """
    payload = b"HELLO" + b"\x00" * 3
    proto_send(r, MSG_HELLO_REQ, payload)

    msg_type, resp = proto_recv(r)
    check_server_error(msg_type, resp)

    if msg_type != MSG_HELLO_RESP:
        raise ProtocolError(
            f"Expected HELLO_RESP (0x{MSG_HELLO_RESP:02X}), "
            f"got 0x{msg_type:02X}"
        )

    if len(resp) < 7 + CHALLENGE_LEN:
        raise ProtocolError(
            f"HELLO_RESP payload too short: {len(resp)} bytes "
            f"(need {7 + CHALLENGE_LEN})"
        )

    banner    = resp[:7]
    challenge = resp[7:7 + CHALLENGE_LEN]

    if banner != b"WELCOME":
        log.warning(f"Unexpected banner: {banner!r} (expected b'WELCOME')")

    log.success(f"Handshake OK — challenge: {challenge.hex()}")
    return challenge


def do_auth(r, username, password, challenge):
    """
    Phase 2 : Authentification.

    Le mot de passe est XOR-é avec le challenge avant envoi.
    C'est cette protection qui empêche le replay naïf de la
    séquence AUTH (le challenge change à chaque session).

    Payload AUTH (length-prefixed strings) :
        [user_len:1] [username:N] [pass_len:1] [password_xored:M]

    Réponse AUTH :
        [reserved:1] [status:1]
        status = 0x01 (OK) ou 0x00 (FAIL)

    Le serveur vérifie :
      - state == HELLO_DONE (sinon ERR_WRONG_STATE)
      - Format du payload (longueurs cohérentes)
      - Credentials : XOR-décode le password, compare avec la base
        interne (3 comptes hardcodés).
      - Max 3 tentatives par session (compteur dans la session).

    Returns:
        True si authentifié, False sinon.
    """
    user_bytes  = username.encode("utf-8")
    pass_bytes  = password.encode("utf-8")
    pass_xored  = xor_with_challenge(pass_bytes, challenge)

    payload = (
        bytes([len(user_bytes)]) + user_bytes +
        bytes([len(pass_xored)]) + pass_xored
    )

    proto_send(r, MSG_AUTH_REQ, payload)

    msg_type, resp = proto_recv(r)

    # Gérer MSG_ERROR (ex: trop de tentatives)
    if msg_type == MSG_ERROR:
        code = resp[0] if resp else 0
        text = resp[1:].decode("utf-8", errors="replace") \
               if len(resp) > 1 else ""
        log.failure(f"Auth error [{ERR_NAMES.get(code, hex(code))}]: {text}")
        return False

    if msg_type != MSG_AUTH_RESP or len(resp) < 2:
        raise ProtocolError(
            f"Unexpected AUTH response: type=0x{msg_type:02X} "
            f"len={len(resp)}"
        )

    status = resp[1]

    if status == STATUS_OK:
        log.success(f"Authenticated as '{username}'")
        return True
    else:
        log.failure(f"Authentication failed for '{username}' "
                    f"(status=0x{status:02X})")
        return False


def do_command(r, cmd_id, args=b""):
    """
    Phase 3 : Envoyer une commande.

    Payload CMD_REQ :
        [command_id:1] [args:N]

    Payload CMD_RESP :
        [status:1] [data:N]

    Le serveur vérifie :
      - state == AUTHENTICATED (sinon ERR_WRONG_STATE)
      - command_id valide (sinon ERR_BAD_CMD, non fatal)

    Returns:
        (status, data) — code de status et données brutes.
    """
    payload = bytes([cmd_id]) + args
    proto_send(r, MSG_CMD_REQ, payload)

    msg_type, resp = proto_recv(r)
    check_server_error(msg_type, resp)

    if msg_type != MSG_CMD_RESP or len(resp) < 1:
        raise ProtocolError(
            f"Unexpected CMD response: type=0x{msg_type:02X}"
        )

    return resp[0], resp[1:]


def do_ping(r):
    """CMD PING → attend PONG."""
    status, data = do_command(r, CMD_PING)
    ok = (status == STATUS_OK and data == b"PONG")
    if ok:
        log.success("PING → PONG")
    else:
        log.warning(f"PING unexpected: status={status:#x} data={data!r}")
    return ok


def do_info(r):
    """CMD INFO → informations serveur (texte)."""
    status, data = do_command(r, CMD_INFO)
    if status == STATUS_OK:
        return data.decode("utf-8", errors="replace")
    return None


def do_list(r):
    """
    CMD LIST → liste des fichiers disponibles.

    Format de la réponse (après le status byte) :
        [count:1]
        puis pour chaque fichier :
        [index:1] [name_len:1] [name:N]

    Returns:
        Liste de tuples (index, filename).
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

        file_idx = data[offset]
        name_len = data[offset + 1]
        offset  += 2

        if offset + name_len > len(data):
            break

        name = data[offset:offset + name_len].decode(
            "utf-8", errors="replace"
        )
        offset += name_len
        files.append((file_idx, name))

    return files


def do_read(r, file_index):
    """
    CMD READ → contenu d'un fichier par son index.

    Payload CMD_REQ : [CMD_READ] [file_index:1]
    Réponse : [STATUS_OK] [contenu:N]

    Returns:
        Contenu du fichier (str), ou None en cas d'erreur.
    """
    status, data = do_command(r, CMD_READ, bytes([file_index]))
    if status == STATUS_OK and data:
        return data.decode("utf-8", errors="replace")
    return None


def do_quit(r):
    """
    Phase 4 : Déconnexion propre.

    Envoie :  [QUIT_REQ] (payload vide)
    Reçoit :  [QUIT_RESP] "BYE"

    Returns:
        True si le serveur a acquitté avec BYE.
    """
    proto_send(r, MSG_QUIT_REQ)

    msg_type, resp = proto_recv(r)

    if msg_type == MSG_QUIT_RESP and resp[:3] == b"BYE":
        log.info("Disconnected (server sent BYE)")
        return True

    log.warning(f"Unexpected QUIT response: type=0x{msg_type:02X} "
                f"data={resp!r}")
    return False


# ═══════════════════════════════════════════════════════════════
#  Couche 3 — Scénarios
# ═══════════════════════════════════════════════════════════════

def full_session(host, port, username, password):
    """
    Session complète : handshake → auth → info → ping →
    list → read all files → quit.

    C'est le scénario demandé par le checkpoint.
    """
    r = remote(host, port)

    try:
        # ── Phase 1 : Handshake ──
        log.info("═" * 50)
        log.info("Phase 1 — Handshake")
        log.info("═" * 50)
        challenge = do_handshake(r)

        # ── Phase 2 : Authentification ──
        log.info("═" * 50)
        log.info("Phase 2 — Authentication")
        log.info("═" * 50)
        if not do_auth(r, username, password, challenge):
            log.error("Authentication failed. Aborting.")
            r.close()
            return False

        # ── Phase 3 : Commandes ──
        log.info("═" * 50)
        log.info("Phase 3 — Commands")
        log.info("═" * 50)

        # PING
        do_ping(r)

        # INFO
        info = do_info(r)
        if info:
            log.info("Server info:")
            for line in info.strip().split("\n"):
                log.info(f"  {line}")

        # LIST
        files = do_list(r)
        if files:
            log.success(f"Available files ({len(files)}):")
            for idx, name in files:
                log.info(f"  [{idx}] {name}")
        else:
            log.warning("No files returned by LIST.")

        # READ all files
        log.info("═" * 50)
        log.info("Phase 4 — Reading all files")
        log.info("═" * 50)

        flag_found = False
        for idx, name in files:
            content = do_read(r, idx)
            if content:
                log.success(f"── {name} ──")
                for line in content.strip().split("\n"):
                    print(f"    {line}")

                    # Détecter le flag
                    if "FLAG{" in line:
                        flag_found = True
                        log.success(f"🚩 FLAG FOUND: {line.strip()}")
            else:
                log.warning(f"Could not read file [{idx}] {name}")

        if not flag_found:
            log.warning("No FLAG{...} found in any file.")

        # ── Phase 5 : Déconnexion ──
        log.info("═" * 50)
        log.info("Phase 5 — Disconnect")
        log.info("═" * 50)
        do_quit(r)

        log.success("Session completed successfully.")
        return True

    except ServerError as e:
        log.error(f"Server error: {e}")
        return False
    except ProtocolError as e:
        log.error(f"Protocol error: {e}")
        return False
    except EOFError:
        log.error("Connection closed unexpectedly.")
        return False
    except Exception as e:
        log.error(f"Unexpected error: {e}")
        return False
    finally:
        r.close()


def test_all_credentials(host, port):
    """
    Bonus : tester les 3 comptes découverts par RE.
    Vérifie que chacun peut s'authentifier et exécuter des commandes.
    """
    # Credentials extraites par :
    #   1. strings server_O2_strip | grep -i pass
    #   2. Breakpoint GDB sur memcmp dans handle_auth
    #   3. Lecture des chaînes adjacentes en mémoire
    credentials = [
        ("admin",   "s3cur3P@ss!"),
        ("analyst", "r3v3rs3M3"),
        ("guest",   "guest123"),
    ]

    log.info("Testing all discovered credentials...")
    results = []

    for username, password in credentials:
        try:
            r = remote(host, port, level="error")
            challenge = do_handshake(r)
            success = do_auth(r, username, password, challenge)

            if success:
                # Vérifier qu'on peut exécuter une commande
                status, _ = do_command(r, CMD_PING)
                cmd_ok = (status == STATUS_OK)
                do_quit(r)
            else:
                cmd_ok = False

            r.close()
            results.append((username, password, success, cmd_ok))

        except Exception as e:
            results.append((username, password, False, False))

    # Afficher le bilan
    print()
    log.info("Credentials test results:")
    log.info(f"  {'User':<12} {'Password':<16} {'Auth':<8} {'CMD':<8}")
    log.info(f"  {'─'*12} {'─'*16} {'─'*8} {'─'*8}")
    for user, pwd, auth_ok, cmd_ok in results:
        auth_str = "✓ OK" if auth_ok else "✗ FAIL"
        cmd_str  = "✓ OK" if cmd_ok else "✗ FAIL"
        log.info(f"  {user:<12} {pwd:<16} {auth_str:<8} {cmd_str:<8}")

    all_ok = all(auth and cmd for _, _, auth, cmd in results)
    if all_ok:
        log.success("All credentials validated.")
    else:
        log.failure("Some credentials failed.")

    return all_ok


# ═══════════════════════════════════════════════════════════════
#  Point d'entrée
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description=(
            "ch23-network checkpoint solution — "
            "Client de remplacement reconstruit par RE"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            Exemples :
              %(prog)s 127.0.0.1
              %(prog)s 127.0.0.1 -p 4444 -u admin -P 's3cur3P@ss!'
              %(prog)s 127.0.0.1 -u guest -P guest123
              %(prog)s 127.0.0.1 --test-all
              %(prog)s 127.0.0.1 -v          # mode debug
        """)
    )

    parser.add_argument(
        "host", nargs="?", default="127.0.0.1",
        help="Adresse du serveur (défaut: 127.0.0.1)"
    )
    parser.add_argument(
        "-p", "--port", type=int, default=4444,
        help="Port TCP (défaut: 4444)"
    )
    parser.add_argument(
        "-u", "--user", default="admin",
        help="Username (défaut: admin)"
    )
    parser.add_argument(
        "-P", "--password", default="s3cur3P@ss!",
        help="Password (défaut: s3cur3P@ss!)"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Activer le logging DEBUG (affiche chaque paquet)"
    )
    parser.add_argument(
        "--test-all", action="store_true",
        help="Bonus : tester les 3 comptes découverts par RE"
    )

    args = parser.parse_args()

    context.log_level = "debug" if args.verbose else "info"

    print()
    log.info("╔══════════════════════════════════════════╗")
    log.info("║  ch23-network — Checkpoint Solution      ║")
    log.info("║  Client reconstruit par RE               ║")
    log.info(f"║  Target: {args.host}:{args.port:<25}║")
    log.info("╚══════════════════════════════════════════╝")
    print()

    if args.test_all:
        success = test_all_credentials(args.host, args.port)
    else:
        success = full_session(
            args.host, args.port,
            args.user, args.password
        )

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    import textwrap
    main()
