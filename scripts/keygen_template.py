#!/usr/bin/env python3
"""
keygen_template.py — Template de keygen pwntools réutilisable
Formation Reverse Engineering — Applications compilées avec la chaîne GNU

Template à copier et adapter pour chaque nouveau crackme.
Stratégie en deux phases :
  Phase 1 — Extraction : lancer le binaire sous GDB, poser un breakpoint
            sur la fonction de comparaison (strcmp, memcmp, etc.),
            envoyer un input bidon, et lire la valeur attendue dans les
            registres au moment de la comparaison.
  Phase 2 — Vérification : relancer le binaire normalement avec la
            valeur extraite et confirmer que l'input est accepté.

Ce template est pré-rempli pour keygenme_O0 (chapitre 21) à titre
d'exemple. Les sections marquées "ADAPTER" doivent être modifiées
pour chaque nouveau binaire cible.

Usage :
  python3 keygen_template.py                          # défaut : keygenme_O0
  python3 keygen_template.py ./keygenme_O2_strip alice
  python3 keygen_template.py --remote 127.0.0.1 4444  # mode réseau

Dépendances :
  pip install pwntools

Licence MIT — Usage strictement éducatif.
"""

from pwn import *
import re
import sys

# ═══════════════════════════════════════════════════════════════
#  CONFIGURATION — ADAPTER pour chaque crackme
# ═══════════════════════════════════════════════════════════════

# Chemin vers le binaire cible
BINARY = "./keygenme_O0"

# Architecture (affecte p32/p64, asm, etc.)
context.arch = "amd64"
context.os = "linux"

# Niveau de log pwntools ('debug' pour voir les échanges bruts)
context.log_level = "warn"

# ── Prompts du binaire (chaînes à attendre avant d'envoyer) ──
# Trouvées via `strings` ou lors du triage initial.
PROMPT_USERNAME = b"Enter username: "
PROMPT_KEY      = b"XXXX-XXXX-XXXX-XXXX): "

# ── Marqueurs de succès / échec dans la sortie ──
SUCCESS_MARKER = b"Valid license"
FAILURE_MARKER = b"Invalid license"

# ── Fonction de comparaison à intercepter ──
# C'est ici que le binaire compare l'input utilisateur à la valeur attendue.
# Pour un strcmp : arg1 (RDI) = attendu, arg2 (RSI) = saisie utilisateur.
# Pour un memcmp : pareil, avec arg3 (RDX) = longueur.
COMPARE_FUNC = "strcmp"

# ── Registre contenant la valeur attendue ──
# System V AMD64 ABI : RDI = 1er argument, RSI = 2ème.
# Dans check_license() de keygenme.c : strcmp(expected, user_key)
# → RDI contient expected (la clé calculée).
EXPECTED_REG = "$rdi"

# ── Format attendu de la clé (regex) ──
# Utilisé pour extraire la valeur depuis la sortie GDB.
# keygenme : XXXX-XXXX-XXXX-XXXX (hex majuscules)
KEY_REGEX = r'([0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4})'

# ── Input bidon envoyé pendant l'extraction ──
DUMMY_KEY = "AAAA-BBBB-CCCC-DDDD"


# ═══════════════════════════════════════════════════════════════
#  Phase 1 — Extraction via GDB
# ═══════════════════════════════════════════════════════════════

def extract_key(binary, username):
    """Lance le binaire sous GDB et extrait la valeur attendue.

    Retourne la clé (str) ou None si l'extraction échoue.
    """
    # Script GDB injecté au lancement :
    # 1. Breakpoint sur la fonction de comparaison
    # 2. Quand atteint : afficher le registre contenant la valeur attendue
    # 3. Continuer (le binaire termine normalement)
    gdb_script = f'''
        set pagination off
        set confirm off
        break {COMPARE_FUNC}
        commands
            silent
            printf "KEYDUMP:%s\\n", (char*){EXPECTED_REG}
            continue
        end
        continue
    '''

    log.info(f"Extraction pour username='{username}'")

    io = gdb.debug(binary, gdb_script, level='warn')

    try:
        io.recvuntil(PROMPT_USERNAME, timeout=10)
        io.sendline(username.encode())

        io.recvuntil(PROMPT_KEY, timeout=10)
        io.sendline(DUMMY_KEY.encode())

        # Lire toute la sortie (programme + GDB)
        output = io.recvall(timeout=10).decode(errors='replace')
    except EOFError:
        output = ""
    finally:
        io.close()

    # Extraire la clé depuis le préfixe KEYDUMP:
    match = re.search(r'KEYDUMP:' + KEY_REGEX, output)
    if match:
        key = match.group(1)
        log.success(f"Clé extraite : {key}")
        return key

    log.error("Extraction échouée — clé non trouvée dans la sortie GDB")
    log.debug(f"Sortie brute :\n{output}")
    return None


# ═══════════════════════════════════════════════════════════════
#  Phase 2 — Vérification
# ═══════════════════════════════════════════════════════════════

def verify_key(target, username, key):
    """Vérifie qu'un couple username/key est accepté par le binaire.

    `target` est soit un chemin (str) pour un process local,
    soit un tuple (host, port) pour une connexion réseau.

    Retourne True si la clé est acceptée, False sinon.
    """
    if isinstance(target, tuple):
        host, port = target
        io = remote(host, port)
    else:
        io = process(target)

    try:
        io.recvuntil(PROMPT_USERNAME, timeout=10)
        io.sendline(username.encode())

        io.recvuntil(PROMPT_KEY, timeout=10)
        io.sendline(key.encode())

        response = io.recvall(timeout=5)
    except EOFError:
        response = b""
    finally:
        io.close()

    if SUCCESS_MARKER in response:
        log.success("Clé ACCEPTÉE")
        return True
    elif FAILURE_MARKER in response:
        log.error("Clé REJETÉE")
        return False
    else:
        log.warning("Réponse inattendue — ni succès ni échec détecté")
        log.debug(f"Réponse brute : {response}")
        return False


# ═══════════════════════════════════════════════════════════════
#  Keygen complet (extraction + vérification)
# ═══════════════════════════════════════════════════════════════

def keygen(binary, username):
    """Workflow complet : extraire la clé puis la vérifier.

    Retourne la clé (str) si le keygen fonctionne, None sinon.
    """
    key = extract_key(binary, username)
    if key is None:
        return None

    if verify_key(binary, username, key):
        return key
    else:
        log.error("La clé extraite a été rejetée — vérifier la logique")
        return None


# ═══════════════════════════════════════════════════════════════
#  Mode batch : générer des clés pour plusieurs usernames
# ═══════════════════════════════════════════════════════════════

def batch_keygen(binary, usernames):
    """Génère des clés pour une liste de noms d'utilisateur."""
    results = {}
    for username in usernames:
        log.info(f"--- {username} ---")
        key = keygen(binary, username)
        results[username] = key
        if key:
            print(f"{username} : {key}")
        else:
            print(f"{username} : ÉCHEC")
    return results


# ═══════════════════════════════════════════════════════════════
#  Point d'entrée
# ═══════════════════════════════════════════════════════════════

def main():
    # Parsing d'arguments simple (sans argparse pour rester léger)
    args = sys.argv[1:]

    # Mode réseau : --remote HOST PORT
    if "--remote" in args:
        idx = args.index("--remote")
        host = args[idx + 1]
        port = int(args[idx + 2])
        username = args[idx + 3] if len(args) > idx + 3 else "student"
        # En mode réseau, pas d'extraction GDB possible
        # L'utilisateur doit fournir la clé ou adapter le script
        log.error("Mode réseau : extraction GDB non disponible.")
        log.info("Adaptez extract_key() pour votre protocole, "
                 "ou fournissez la clé manuellement.")
        return

    # Mode local
    binary = args[0] if len(args) >= 1 else BINARY
    username = args[1] if len(args) >= 2 else "student"

    # Mode batch si plusieurs usernames fournis
    if len(args) > 2:
        usernames = args[1:]
        batch_keygen(binary, usernames)
    else:
        key = keygen(binary, username)
        if key:
            print(f"\n{'='*40}")
            print(f"  Username : {username}")
            print(f"  Key      : {key}")
            print(f"{'='*40}\n")
            sys.exit(0)
        else:
            sys.exit(1)


if __name__ == "__main__":
    main()
