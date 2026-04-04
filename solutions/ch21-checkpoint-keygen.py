#!/usr/bin/env python3
"""
ch21-checkpoint-keygen.py — Corrigé du checkpoint du chapitre 21.

Ce script remplit les 5 critères du checkpoint :
  1. Keygen autonome (ne dépend pas du binaire pour calculer la clé)
  2. Clés valides sur keygenme_O0
  3. Clés valides sur keygenme_O2
  4. Clés valides sur keygenme_O2_strip
  5. Validation automatisée via pwntools

Usage :
  Générer une clé :
    python3 ch21-checkpoint-keygen.py Alice

  Valider le keygen contre les 3 variantes :
    python3 ch21-checkpoint-keygen.py --validate

  Valider contre les 5 variantes :
    python3 ch21-checkpoint-keygen.py --validate-all

Licence MIT — Usage strictement éducatif.
"""

import sys
import os
import random
import string

# ═══════════════════════════════════════════════════════════════
# PARTIE 1 — KEYGEN AUTONOME
#
# Fonctions reconstruites depuis le désassemblage du keygenme.
# Chaque fonction correspond à son équivalent dans le binaire,
# identifié via l'analyse statique dans Ghidra (section 21.3)
# et vérifié dynamiquement dans GDB (section 21.5).
# ═══════════════════════════════════════════════════════════════

def rotate_left_32(value: int, count: int) -> int:
    """
    Rotation à gauche sur 32 bits.

    Correspond à la fonction `rotate_left` dans le binaire.
    En assembleur x86-64, GCC émet soit un ROL direct, soit le
    pattern classique : SHL + SHR + OR.

    Le masquage count &= 31 protège contre le cas count == 0
    (qui provoquerait un shift de 32 bits — comportement indéfini
    en C sur un uint32_t, mais traité correctement ici).
    """
    count &= 31
    return ((value << count) | (value >> (32 - count))) & 0xFFFFFFFF


def compute_hash(username: bytes) -> int:
    """
    Reproduit la fonction compute_hash du keygenme.

    Constantes identifiées dans le désassemblage :
      - SEED  = 0x5A3C6E2D  (valeur initiale de h, MOV imm32)
      - MUL   = 0x1003F     (multiplicateur, IMUL imm32)
      - XOR   = 0xDEADBEEF  (masque XOR, XOR imm32)
      - 0x45D9F3B           (multiplicateur d'avalanche final)

    Points d'attention pour la traduction :
      - Chaque opération arithmétique est masquée à 32 bits
        (& 0xFFFFFFFF) pour simuler le comportement uint32_t du C.
      - La rotation utilise (byte & 0x0F) comme compteur,
        extrayant les 4 bits de poids faible du caractère courant.
    """
    SEED = 0x5A3C6E2D
    MUL  = 0x1003F
    XOR  = 0xDEADBEEF

    h = SEED

    for byte in username:
        h = (h + byte) & 0xFFFFFFFF
        h = (h * MUL) & 0xFFFFFFFF
        h = rotate_left_32(h, byte & 0x0F)
        h ^= XOR

    # Avalanche final — diffuse les bits de poids fort vers les
    # bits de poids faible pour améliorer la distribution.
    # Pattern reconnaissable : XOR-shift + multiplication + XOR-shift.
    h ^= (h >> 16)
    h = (h * 0x45D9F3B) & 0xFFFFFFFF
    h ^= (h >> 16)

    return h


def derive_key(hash_val: int) -> list:
    """
    Dérive 4 groupes de 16 bits depuis le hash.

    Correspond à la fonction `derive_key` dans le binaire.
    Chaque groupe combine une extraction de 16 bits (masquage
    direct ou rotation + masquage) avec un XOR par une constante.

    Constantes XOR identifiées dans le désassemblage :
      - groups[0] : 0xA5A5
      - groups[1] : 0x5A5A
      - groups[2] : 0x1234
      - groups[3] : 0xFEDC

    Les rotations de 7 et 13 bits pour les groupes 2 et 3
    sont visibles comme des ROL imm8 ou des paires SHL/SHR/OR
    avec les constantes 7 et 13 en opérande immédiat.
    """
    groups = [
        (hash_val & 0xFFFF) ^ 0xA5A5,
        ((hash_val >> 16) & 0xFFFF) ^ 0x5A5A,
        (rotate_left_32(hash_val, 7) & 0xFFFF) ^ 0x1234,
        (rotate_left_32(hash_val, 13) & 0xFFFF) ^ 0xFEDC,
    ]
    return groups


def format_key(groups: list) -> str:
    """
    Formate les 4 groupes en clé XXXX-XXXX-XXXX-XXXX.

    Correspond à la fonction `format_key` dans le binaire,
    qui appelle snprintf avec le format "%04X-%04X-%04X-%04X"
    (chaîne identifiée dès le triage avec strings, section 21.1).
    """
    return "{:04X}-{:04X}-{:04X}-{:04X}".format(*groups)


def keygen(username: str) -> str:
    """
    Génère une clé de licence valide pour le username donné.

    Reproduit la chaîne complète :
      compute_hash(username) → derive_key(hash) → format_key(groups)

    C'est exactement ce que fait check_license dans le binaire,
    à l'exception du strcmp final (le keygen produit la clé
    attendue, il n'a pas besoin de comparer).
    """
    h = compute_hash(username.encode("ascii"))
    groups = derive_key(h)
    return format_key(groups)


# ═══════════════════════════════════════════════════════════════
# PARTIE 2 — VALIDATION AUTOMATISÉE AVEC PWNTOOLS
#
# Soumet les clés générées aux binaires et vérifie qu'elles
# sont acceptées. Utilise pwntools pour l'interaction.
# ═══════════════════════════════════════════════════════════════

def find_binaries_dir() -> str:
    """
    Cherche le répertoire contenant les binaires du keygenme.
    Essaie plusieurs chemins relatifs courants.
    """
    candidates = [
        ".",
        "./binaries/ch21-keygenme",
        "../binaries/ch21-keygenme",
        "../../binaries/ch21-keygenme",
    ]
    for d in candidates:
        if os.path.isfile(os.path.join(d, "keygenme_O0")):
            return d
    return "."


def validate_single(binary_path: str, username: str, key: str) -> bool:
    """
    Soumet un username et une clé au binaire, retourne True si
    le message de succès est détecté dans la sortie.
    """
    from pwn import process, context
    context.log_level = "error"

    try:
        p = process(binary_path)
        p.recvuntil(b"Enter username: ")
        p.sendline(username.encode())
        p.recvuntil(b": ")  # fin du prompt de la clé
        p.sendline(key.encode())
        response = p.recvall(timeout=3).decode(errors="replace")
        p.close()
        return "Valid license" in response
    except Exception as e:
        print(f"    [!] Erreur sur {binary_path}: {e}")
        return False


def generate_test_usernames(count: int = 10) -> list:
    """
    Génère une liste de usernames de test variés.
    Inclut des cas limites (longueur min/max) et des cas normaux.
    """
    # Cas fixes couvrant différentes longueurs et caractères
    fixed = [
        "Alice",
        "Bob",
        "X1z",                       # longueur minimale (3)
        "ReverseEngineer",
        "user_2024",
        "AAAAAAA",                   # caractères répétés
        "aZ9",                       # mix min/maj/chiffre, longueur 3
        "ThisIsALongerUsername12345", # 25 caractères
    ]

    # Cas aléatoires pour compléter
    rand_count = max(0, count - len(fixed))
    charset = string.ascii_letters + string.digits
    for _ in range(rand_count):
        length = random.randint(3, 31)
        name = "".join(random.choices(charset, k=length))
        fixed.append(name)

    return fixed[:count]


def run_validation(binaries: dict, num_tests: int = 10):
    """
    Exécute la validation complète du keygen.

    Args:
        binaries: dict {label: path} des binaires à tester.
        num_tests: nombre de usernames à tester.
    """
    usernames = generate_test_usernames(num_tests)
    labels = list(binaries.keys())

    # ── En-tête ──────────────────────────────────────────────
    print()
    print("══════════════════════════════════════════════════════════════")
    print("  Checkpoint 21 — Validation du keygen")
    print("══════════════════════════════════════════════════════════════")
    print()

    # Vérifier que les binaires existent
    missing = [l for l, p in binaries.items() if not os.path.isfile(p)]
    if missing:
        print(f"  [!] Binaires introuvables : {', '.join(missing)}")
        print(f"      Vérifiez le chemin ou compilez avec 'make'.")
        sys.exit(1)

    # ── Colonnes ─────────────────────────────────────────────
    col_user = 22
    col_key  = 24
    col_bin  = 6

    header_bins = "".join(f"{l:>{col_bin}}" for l in labels)
    print(f"  {'Username':<{col_user}}{'Clé générée':<{col_key}}{header_bins}")
    print(f"  {'─' * (col_user + col_key + col_bin * len(labels))}")

    # ── Tests ────────────────────────────────────────────────
    total = 0
    passed = 0
    failures = []

    for username in usernames:
        key = keygen(username)
        results = {}

        for label, path in binaries.items():
            ok = validate_single(path, username, key)
            results[label] = ok
            total += 1
            if ok:
                passed += 1
            else:
                failures.append((username, key, label))

        icons = "".join(
            f"{'  ✅' if results[l] else '  ❌':>{col_bin}}" for l in labels
        )

        # Tronquer le username et la clé pour l'affichage
        udisp = username if len(username) <= col_user - 2 else username[:col_user - 4] + "…"
        print(f"  {udisp:<{col_user}}{key:<{col_key}}{icons}")

    # ── Résumé ───────────────────────────────────────────────
    print()
    print(f"  Résultat : {passed}/{total} validations réussies.")

    if failures:
        print()
        print("  Échecs détaillés :")
        for username, key, label in failures:
            print(f"    - username='{username}' key='{key}' binaire={label}")
        print()
        print("  ❌ Checkpoint échoué.")
        print()
        print("  Pistes de diagnostic :")
        print("    1. Vérifier compute_hash : comparer avec GDB (break après compute_hash, print $eax)")
        print("    2. Vérifier derive_key : comparer les 4 groupes avec GDB (x/4hx sur le tableau)")
        print("    3. Vérifier format_key : comparer avec GDB (x/s $rdi avant strcmp)")
        print("    4. Vérifier l'interaction pwntools : sendline envoie-t-il un \\n parasite ?")
        sys.exit(1)
    else:
        print()
        print("  ✅ Checkpoint réussi.")
        print()


# ═══════════════════════════════════════════════════════════════
# PARTIE 3 — POINT D'ENTRÉE
# ═══════════════════════════════════════════════════════════════

def print_usage():
    print(f"Usage :")
    print(f"  {sys.argv[0]} <username>          Générer une clé")
    print(f"  {sys.argv[0]} --validate          Valider contre O0, O2, O2_strip")
    print(f"  {sys.argv[0]} --validate-all      Valider contre les 5 variantes")
    print(f"  {sys.argv[0]} --hash <username>   Afficher le hash intermédiaire (debug)")


def main():
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    arg = sys.argv[1]

    # ── Mode validation (3 variantes requises par le checkpoint) ──
    if arg == "--validate":
        d = find_binaries_dir()
        binaries = {
            "O0":  os.path.join(d, "keygenme_O0"),
            "O2":  os.path.join(d, "keygenme_O2"),
            "O2s": os.path.join(d, "keygenme_O2_strip"),
        }
        run_validation(binaries, num_tests=10)

    # ── Mode validation (5 variantes, pour aller plus loin) ──────
    elif arg == "--validate-all":
        d = find_binaries_dir()
        binaries = {
            "O0":    os.path.join(d, "keygenme_O0"),
            "O2":    os.path.join(d, "keygenme_O2"),
            "O3":    os.path.join(d, "keygenme_O3"),
            "strip": os.path.join(d, "keygenme_strip"),
            "O2s":   os.path.join(d, "keygenme_O2_strip"),
        }
        run_validation(binaries, num_tests=10)

    # ── Mode debug : afficher le hash intermédiaire ──────────────
    elif arg == "--hash":
        if len(sys.argv) < 3:
            print("Usage : --hash <username>")
            sys.exit(1)
        username = sys.argv[2]
        h = compute_hash(username.encode("ascii"))
        groups = derive_key(h)
        key = format_key(groups)
        print(f"  Username  : {username}")
        print(f"  Hash      : 0x{h:08X}")
        print(f"  Groups    : [0x{groups[0]:04X}, 0x{groups[1]:04X}, "
              f"0x{groups[2]:04X}, 0x{groups[3]:04X}]")
        print(f"  License   : {key}")

    # ── Mode keygen simple ───────────────────────────────────────
    else:
        username = arg

        if len(username) < 3 or len(username) > 31:
            print("[-] Le username doit faire entre 3 et 31 caractères.")
            sys.exit(1)

        key = keygen(username)
        print(f"[+] Username : {username}")
        print(f"[+] License  : {key}")


if __name__ == "__main__":
    main()
