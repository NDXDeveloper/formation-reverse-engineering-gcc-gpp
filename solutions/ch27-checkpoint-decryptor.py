#!/usr/bin/env python3
"""
============================================================================
 Formation Reverse Engineering — Chapitre 27
 CORRIGÉ : Déchiffreur pour le ransomware pédagogique Ch27
============================================================================

 Paramètres cryptographiques extraits par :
   - Analyse statique (Ghidra) : XREF vers EVP_EncryptInit_ex,
     clé et IV dans .rodata
   - Analyse dynamique (GDB/Frida) : capture de $rcx (clé) et $r8 (IV)
     au moment de l'appel EVP_EncryptInit_ex

 Format des fichiers .locked (cartographié avec ImHex) :
   [0x00 - 0x07]  Magic : "RWARE27\0"
   [0x08 - 0x0F]  Taille originale : uint64_t little-endian
   [0x10 - EOF ]  Ciphertext AES-256-CBC (padding PKCS#7 inclus)

 Dépendance :
   pip install cryptography

 Usage :
   python3 ch27-checkpoint-decryptor.py                        # /tmp/test/
   python3 ch27-checkpoint-decryptor.py /chemin/vers/dossier
   python3 ch27-checkpoint-decryptor.py fichier.txt.locked
   python3 ch27-checkpoint-decryptor.py --dry-run
   python3 ch27-checkpoint-decryptor.py --verify

 Licence : MIT — usage éducatif uniquement
============================================================================
"""

import sys
import os
import struct
import hashlib
import argparse

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding
from cryptography.hazmat.backends import default_backend


# ═══════════════════════════════════════════════════════════════════════════
#  Constantes extraites par Reverse Engineering
# ═══════════════════════════════════════════════════════════════════════════

# Clé AES-256 (32 octets)
# Source : .rodata du binaire, confirmée via registre $rcx sur
#          EVP_EncryptInit_ex (GDB breakpoint + Frida hook)
# Valeur ASCII : REVERSE_ENGINEERING_IS_FUN_2025!
AES_KEY = bytes([
    0x52, 0x45, 0x56, 0x45, 0x52, 0x53, 0x45, 0x5F,  # REVERSE_
    0x45, 0x4E, 0x47, 0x49, 0x4E, 0x45, 0x45, 0x52,  # ENGINEER
    0x49, 0x4E, 0x47, 0x5F, 0x49, 0x53, 0x5F, 0x46,  # ING_IS_F
    0x55, 0x4E, 0x5F, 0x32, 0x30, 0x32, 0x35, 0x21,  # UN_2025!
])

# IV AES-CBC (16 octets)
# Source : .rodata du binaire, confirmé via registre $r8 sur
#          EVP_EncryptInit_ex (GDB breakpoint + Frida hook)
AES_IV = bytes([
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x13, 0x37, 0x42, 0x42, 0xFE, 0xED, 0xFA, 0xCE,
])

# Format du fichier .locked (cartographié avec ImHex)
MAGIC_HEADER = b"RWARE27\x00"   # 8 octets, offset 0x00
HEADER_SIZE  = 16                # 8 (magic) + 8 (orig_size)
LOCKED_EXT   = ".locked"


# ═══════════════════════════════════════════════════════════════════════════
#  Parsing du header .locked
# ═══════════════════════════════════════════════════════════════════════════

def parse_locked_header(filepath):
    """
    Lit et valide le header d'un fichier .locked.

    Retourne :
        (original_size, ciphertext) — taille originale et données chiffrées

    Lève ValueError si :
        - Le fichier est trop petit (< 16 octets)
        - Le magic header ne correspond pas
        - La taille annoncée est incohérente
    """
    with open(filepath, "rb") as f:
        header = f.read(HEADER_SIZE)

        if len(header) < HEADER_SIZE:
            raise ValueError(
                f"Fichier trop petit ({len(header)} octets, "
                f"minimum {HEADER_SIZE}) : {filepath}"
            )

        # Vérifier le magic (8 premiers octets)
        magic = header[0:8]
        if magic != MAGIC_HEADER:
            raise ValueError(
                f"Magic header invalide dans {filepath} : "
                f"attendu {MAGIC_HEADER!r}, obtenu {magic!r}"
            )

        # Extraire la taille originale (uint64_t little-endian, offset 0x08)
        original_size = struct.unpack("<Q", header[8:16])[0]

        # Vérification de cohérence basique
        if original_size == 0:
            raise ValueError(f"Taille originale nulle dans {filepath}")

        # Lire le ciphertext (tout ce qui suit le header)
        ciphertext = f.read()

    # Le ciphertext doit être un multiple de 16 (taille de bloc AES)
    if len(ciphertext) == 0:
        raise ValueError(f"Aucune donnée chiffrée dans {filepath}")

    if len(ciphertext) % 16 != 0:
        raise ValueError(
            f"Taille du ciphertext ({len(ciphertext)} octets) non multiple "
            f"de 16 dans {filepath} — fichier probablement corrompu"
        )

    return original_size, ciphertext


# ═══════════════════════════════════════════════════════════════════════════
#  Déchiffrement AES-256-CBC
# ═══════════════════════════════════════════════════════════════════════════

def decrypt_aes256cbc(ciphertext, key, iv):
    """
    Déchiffre un buffer AES-256-CBC et retire le padding PKCS#7.

    C'est l'opération inverse de la séquence OpenSSL observée dans le sample :
        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)
        EVP_EncryptUpdate(ctx, out, &len, in, in_len)
        EVP_EncryptFinal_ex(ctx, out + len, &len)

    Retourne le plaintext sans padding.
    Lève ValueError si la clé/IV est incorrecte (padding invalide).
    """
    # Déchiffrement
    cipher = Cipher(
        algorithms.AES(key),
        modes.CBC(iv),
        backend=default_backend()
    )
    decryptor = cipher.decryptor()
    padded_plaintext = decryptor.update(ciphertext) + decryptor.finalize()

    # Retrait du padding PKCS#7
    # 128 = taille de bloc AES en BITS (pas en octets)
    unpadder = padding.PKCS7(128).unpadder()
    plaintext = unpadder.update(padded_plaintext) + unpadder.finalize()

    return plaintext


# ═══════════════════════════════════════════════════════════════════════════
#  Déchiffrement d'un fichier complet
# ═══════════════════════════════════════════════════════════════════════════

def decrypt_file(locked_path, dry_run=False, verify=False):
    """
    Déchiffre un fichier .locked et restaure le fichier original.

    Étapes :
        1. Parser le header (magic + taille originale)
        2. Déchiffrer le payload AES-256-CBC
        3. Vérifier la cohérence taille déchiffrée vs taille annoncée
        4. Écrire le fichier restauré (sans l'extension .locked)

    Retourne le chemin du fichier restauré, ou None en cas d'échec.
    """
    print(f"[*] {locked_path}")

    # ── 1. Parser le header ──
    try:
        original_size, ciphertext = parse_locked_header(locked_path)
    except ValueError as e:
        print(f"    [!] Header invalide : {e}")
        return None

    print(f"    Taille originale : {original_size} octets")
    print(f"    Ciphertext :       {len(ciphertext)} octets")

    # ── 2. Déchiffrer ──
    try:
        plaintext = decrypt_aes256cbc(ciphertext, AES_KEY, AES_IV)
    except ValueError as e:
        print(f"    [!] Échec déchiffrement : {e}")
        print(f"    [!] La clé ou l'IV sont probablement incorrects.")
        return None
    except Exception as e:
        print(f"    [!] Erreur inattendue : {e}")
        return None

    # ── 3. Vérification croisée de taille ──
    if len(plaintext) != original_size:
        print(
            f"    [!] Incohérence de taille : "
            f"déchiffré={len(plaintext)}, annoncé={original_size}"
        )
        # Tronquer à la taille annoncée comme filet de sécurité
        plaintext = plaintext[:original_size]
        print(f"    [!] Troncature appliquée à {original_size} octets")

    # ── 4. Déterminer le chemin de sortie ──
    if locked_path.endswith(LOCKED_EXT):
        output_path = locked_path[:-len(LOCKED_EXT)]
    else:
        output_path = locked_path + ".decrypted"

    # ── Mode dry-run : ne pas écrire ──
    if dry_run:
        print(f"    [DRY-RUN] → {output_path} ({len(plaintext)} octets)")
        return output_path

    # ── Écrire le fichier restauré ──
    with open(output_path, "wb") as f:
        f.write(plaintext)

    # ── Hash SHA-256 du fichier restauré ──
    sha256 = hashlib.sha256(plaintext).hexdigest()
    print(f"    [✓] → {output_path} ({len(plaintext)} octets)")
    print(f"        SHA-256 : {sha256}")

    # ── Aperçu si demandé ──
    if verify:
        preview_len = min(80, len(plaintext))
        try:
            preview = plaintext[:preview_len].decode("utf-8", errors="replace")
            print(f"        Aperçu : {preview!r}")
        except Exception:
            print(f"        Aperçu : {plaintext[:preview_len].hex()}")

    return output_path


# ═══════════════════════════════════════════════════════════════════════════
#  Parcours récursif d'un répertoire
# ═══════════════════════════════════════════════════════════════════════════

def scan_and_decrypt(directory, dry_run=False, verify=False):
    """
    Parcourt récursivement un répertoire et déchiffre tous les .locked.
    Retourne (success_count, error_count).
    """
    success = 0
    errors  = 0
    skipped = 0

    for root, _dirs, files in os.walk(directory):
        for filename in sorted(files):
            filepath = os.path.join(root, filename)

            if not filename.endswith(LOCKED_EXT):
                skipped += 1
                continue

            result = decrypt_file(filepath, dry_run=dry_run, verify=verify)
            if result:
                success += 1
            else:
                errors += 1

    print()
    print("=" * 55)
    print(f"  Résultat : {success} restauré(s), "
          f"{errors} erreur(s), {skipped} ignoré(s)")
    print("=" * 55)

    return success, errors


# ═══════════════════════════════════════════════════════════════════════════
#  Point d'entrée
# ═══════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="Déchiffreur pour le ransomware Ch27 (AES-256-CBC)",
        epilog="Formation Reverse Engineering — Chapitre 27 — Corrigé"
    )
    parser.add_argument(
        "target",
        nargs="?",
        default="/tmp/test",
        help="Fichier .locked ou répertoire à traiter (défaut : /tmp/test)"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Simuler sans écrire de fichier"
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Afficher un aperçu du contenu déchiffré"
    )
    args = parser.parse_args()

    # Bannière
    print()
    print("=" * 55)
    print("  Déchiffreur Ch27 — Corrigé du checkpoint")
    print(f"  Algorithme : AES-256-CBC")
    print(f"  Clé :  {AES_KEY.decode('ascii')}")
    print(f"  IV :   {AES_IV.hex()}")
    print("=" * 55)
    print()

    target = args.target

    if os.path.isfile(target):
        result = decrypt_file(target, dry_run=args.dry_run, verify=args.verify)
        sys.exit(0 if result else 1)

    elif os.path.isdir(target):
        success, errors = scan_and_decrypt(
            target, dry_run=args.dry_run, verify=args.verify
        )
        sys.exit(0 if errors == 0 else 1)

    else:
        print(f"[!] Cible introuvable : {target}")
        sys.exit(1)


if __name__ == "__main__":
    main()
