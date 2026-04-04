#!/usr/bin/env python3
"""
solutions/ch13-checkpoint-solution.py

Client Python d'orchestration — Checkpoint Chapitre 13
Spawne le binaire cible, charge l'agent Frida, collecte les
appels à send() et exporte les résultats dans capture.json
et capture.bin.

Usage :
    python3 ch13-checkpoint-solution.py ./binaries/ch13-network/client_O0

    # Avec des arguments pour le binaire cible :
    python3 ch13-checkpoint-solution.py ./binaries/ch13-network/client_O0 -- 127.0.0.1 4444

    # Avec filtrage sur un file descriptor spécifique :
    python3 ch13-checkpoint-solution.py ./binaries/ch13-network/client_O0 --filter-fd 3
"""
import frida
import sys
import os
import json
import time
import argparse
from pathlib import Path

# ═══════════════════════════════════════════════════════════
# CONFIGURATION ET ARGUMENTS
# ═══════════════════════════════════════════════════════════

def parse_args():
    parser = argparse.ArgumentParser(
        description="Frida send() logger — Checkpoint Ch.13"
    )
    parser.add_argument(
        "binary",
        help="Chemin vers le binaire cible"
    )
    parser.add_argument(
        "binary_args",
        nargs="*",
        help="Arguments à passer au binaire cible"
    )
    parser.add_argument(
        "--filter-fd", type=int, default=-1,
        help="Ne logger que ce file descriptor (-1 = tous)"
    )
    parser.add_argument(
        "--output-dir", type=str, default=".",
        help="Répertoire de sortie pour capture.json et capture.bin"
    )
    parser.add_argument(
        "--no-write-hook", action="store_true",
        help="Désactiver le hook sur write()"
    )
    return parser.parse_args()


# ═══════════════════════════════════════════════════════════
# CLASSE PRINCIPALE
# ═══════════════════════════════════════════════════════════

class SendLogger:
    """Orchestre la session Frida et collecte les résultats."""

    def __init__(self, binary, binary_args, filter_fd, output_dir, hook_write):
        self.binary = binary
        self.binary_args = binary_args
        self.filter_fd = filter_fd
        self.output_dir = Path(output_dir)
        self.hook_write = hook_write

        # Données collectées
        self.events = []        # métadonnées JSON de chaque appel
        self.buffers = []       # buffers binaires bruts (bytes)
        self.total_bytes = 0    # octets capturés au total

        # État de la session
        self.session = None
        self.script = None
        self.pid = None
        self.running = True

    def load_agent(self):
        """Charge le code JavaScript de l'agent depuis le fichier."""
        agent_path = Path(__file__).parent / "ch13-checkpoint-solution.js"
        if not agent_path.exists():
            # Fallback : chercher à côté du binaire ou dans le CWD
            agent_path = Path("ch13-checkpoint-solution.js")
        if not agent_path.exists():
            print(f"[!] Agent JS introuvable. Cherché dans :")
            print(f"    - {Path(__file__).parent / 'ch13-checkpoint-solution.js'}")
            print(f"    - {Path('ch13-checkpoint-solution.js').resolve()}")
            sys.exit(1)

        code = agent_path.read_text(encoding="utf-8")

        # Injecter la configuration dans le code agent
        # (remplacer les valeurs par défaut de CONFIG)
        code = code.replace(
            "filterFd: -1,",
            f"filterFd: {self.filter_fd},",
        )
        code = code.replace(
            "hookWrite: true,",
            f"hookWrite: {'true' if self.hook_write else 'false'},",
        )

        return code

    def on_message(self, message, data):
        """
        Callback déclenché par chaque send() côté agent JS.

        - message : dict avec 'type' et 'payload' (le JSON)
        - data    : bytes ou None (le buffer binaire brut)
        """
        if message["type"] == "send":
            payload = message["payload"]
            event_type = payload.get("event")

            if event_type == "connect":
                self._handle_connect(payload)

            elif event_type in ("send", "write"):
                self._handle_send(payload, data)

            elif event_type == "close":
                self._handle_close(payload)

        elif message["type"] == "error":
            print(f"\n[ERREUR AGENT] {message.get('stack', message)}")

    def _handle_connect(self, payload):
        """Affiche et enregistre un événement connect()."""
        result = payload["result"]
        status = "OK" if result == 0 else f"ERREUR ({result})"
        ip = payload.get("ip", "?")
        port = payload.get("port", "?")

        print(f"\n┌─ CONNECT fd {payload['fd']} → {ip}:{port} [{status}]")
        self.events.append(payload)

    def _handle_send(self, payload, data):
        """Affiche et enregistre un événement send()/write()."""
        seq = payload["seq"]
        ts = payload["timestamp_ms"]
        fd = payload["fd"]
        requested = payload["requested_len"]
        sent = payload["bytes_sent"]
        func = payload["event"].upper()
        dest_ip = payload.get("dest_ip")
        dest_port = payload.get("dest_port")

        dest = f" → {dest_ip}:{dest_port}" if dest_ip else ""
        status = f"{sent}/{requested} octets" if sent >= 0 else "ERREUR"

        print(f"│ [{func} #{seq:03d}] +{ts:>6d}ms | fd {fd}{dest} | {status}")

        if data:
            # Afficher un aperçu hex des 48 premiers octets
            preview = data[:48]
            hex_str = " ".join(f"{b:02x}" for b in preview)
            ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in preview)
            suffix = "..." if len(data) > 48 else ""
            print(f"│   hex: {hex_str}{suffix}")
            print(f"│   asc: {ascii_str}{suffix}")

            self.buffers.append(data)
            self.total_bytes += len(data)

        # Enregistrer les métadonnées (sans le buffer binaire)
        payload_copy = dict(payload)
        payload_copy["buffer_size"] = len(data) if data else 0
        self.events.append(payload_copy)

    def _handle_close(self, payload):
        """Affiche et enregistre un événement close()."""
        ip = payload.get("ip", "?")
        port = payload.get("port", "?")
        print(f"└─ CLOSE fd {payload['fd']} ({ip}:{port})")
        self.events.append(payload)

    def on_detached(self, reason):
        """Callback quand le processus se termine."""
        print(f"\n[*] Session détachée : {reason}")
        self.running = False

    def export_results(self):
        """Sauvegarde capture.json et capture.bin."""
        self.output_dir.mkdir(parents=True, exist_ok=True)

        # ── capture.json ──
        json_path = self.output_dir / "capture.json"
        export_data = {
            "binary": self.binary,
            "binary_args": self.binary_args,
            "filter_fd": self.filter_fd,
            "total_send_calls": sum(
                1 for e in self.events if e.get("event") in ("send", "write")
            ),
            "total_bytes_captured": self.total_bytes,
            "events": self.events,
        }
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(export_data, f, indent=2, ensure_ascii=False)
        print(f"[+] Métadonnées exportées → {json_path}")

        # ── capture.bin ──
        bin_path = self.output_dir / "capture.bin"
        with open(bin_path, "wb") as f:
            for buf in self.buffers:
                f.write(buf)
        print(f"[+] Buffers bruts exportés → {bin_path} ({self.total_bytes} octets)")

    def run(self):
        """Point d'entrée principal."""
        # Vérifier que le binaire existe
        if not Path(self.binary).exists():
            print(f"[!] Binaire introuvable : {self.binary}")
            sys.exit(1)

        # Charger l'agent
        agent_code = self.load_agent()

        # Spawner le processus
        spawn_args = [self.binary] + self.binary_args
        print(f"[*] Spawn : {' '.join(spawn_args)}")
        self.pid = frida.spawn(spawn_args)
        self.session = frida.attach(self.pid)
        self.session.on("detached", self.on_detached)

        # Charger le script agent
        self.script = self.session.create_script(agent_code)
        self.script.on("message", self.on_message)
        self.script.load()

        # Reprendre l'exécution du processus
        frida.resume(self.pid)
        print(f"[*] Processus lancé (PID {self.pid}). Ctrl+C pour arrêter.\n")

        # Boucle d'attente
        try:
            while self.running:
                time.sleep(0.5)
        except KeyboardInterrupt:
            print("\n[*] Interruption utilisateur.")

        # Nettoyage et export
        print(f"\n{'═' * 50}")
        print(f"  Résumé de la capture")
        print(f"{'═' * 50}")

        send_count = sum(
            1 for e in self.events if e.get("event") in ("send", "write")
        )
        connect_count = sum(
            1 for e in self.events if e.get("event") == "connect"
        )
        print(f"  Connexions capturées  : {connect_count}")
        print(f"  Appels send()/write() : {send_count}")
        print(f"  Octets totaux         : {self.total_bytes}")
        print(f"{'═' * 50}\n")

        self.export_results()

        # Détacher proprement
        try:
            self.session.detach()
        except Exception:
            pass


# ═══════════════════════════════════════════════════════════
# POINT D'ENTRÉE
# ═══════════════════════════════════════════════════════════

def main():
    args = parse_args()

    logger = SendLogger(
        binary=args.binary,
        binary_args=args.binary_args,
        filter_fd=args.filter_fd,
        output_dir=args.output_dir,
        hook_write=not args.no_write_hook,
    )
    logger.run()


if __name__ == "__main__":
    main()
