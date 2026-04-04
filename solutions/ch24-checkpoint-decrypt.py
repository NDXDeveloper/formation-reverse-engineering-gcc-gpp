#!/usr/bin/env python3
"""
ch24-checkpoint-decrypt.py — Corrigé du Checkpoint du Chapitre 24
Formation Reverse Engineering — Applications compilées avec la chaîne GNU

Déchiffre un fichier au format CRYPT24 produit par le binaire ch24-crypto.

Ce script reproduit intégralement la logique de dérivation de clé du binaire :
    1. Passphrase hardcodée : "r3vers3_m3_1f_y0u_c4n!"
       (construite par morceaux dans build_passphrase() pour échapper à `strings`)
    2. Hash SHA-256 de la passphrase
    3. XOR du hash avec un masque de 32 octets (KEY_MASK, stocké dans .rodata)
    4. Résultat = clé AES-256

Le fichier .enc est structuré selon le format CRYPT24 :
    [0x00..0x07]  Magic         "CRYPT24\0"           (8 octets)
    [0x08]        Version maj.  0x01                   (1 octet)
    [0x09]        Version min.  0x00                   (1 octet)
    [0x0A..0x0B]  IV length     0x0010 (16)            (uint16 LE)
    [0x0C..0x1B]  IV            (16 octets aléatoires)
    [0x1C..0x1F]  Orig. size    (taille du plaintext)  (uint32 LE)
    [0x20..EOF]   Ciphertext    (AES-256-CBC, PKCS7)

Prérequis : pip install pycryptodome

Usage :
    python3 ch24-checkpoint-decrypt.py <fichier.enc> [fichier_sortie]

Exemples :
    python3 ch24-checkpoint-decrypt.py secret.enc
    python3 ch24-checkpoint-decrypt.py secret.enc decrypted.txt

Validation :
    diff secret.txt decrypted.txt   # aucune sortie = succès

Licence MIT — Usage strictement éducatif et éthique.
"""

import sys
import struct
import hashlib
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad


# ============================================================================
# Données reconstruites par Reverse Engineering
# ============================================================================

# Passphrase — Section 24.3
# Trouvée via : breakpoint GDB sur build_passphrase(), puis x/s sur le buffer
# de sortie au retour de la fonction.
# Dans le binaire, elle est construite en 3 morceaux (part1 + part2 + part3)
# concaténés par strcat() pour ne pas apparaître en clair dans `strings`.
PASSPHRASE = b"r3vers3_m3_1f_y0u_c4n!"

# Masque XOR — Section 24.3
# Trouvé via : variable globale KEY_MASK dans .rodata, repérée dans Ghidra
# par XREF depuis la boucle XOR dans derive_key().
# Premiers octets reconnaissables : 0xDEADBEEF 0xCAFEBABE (valeurs sentinelles
# classiques, probablement choisies par le développeur pour le debug).
KEY_MASK = bytes([
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x13, 0x37, 0x42, 0x42, 0xFE, 0xED, 0xFA, 0xCE,
    0x0B, 0xAD, 0xF0, 0x0D, 0xDE, 0xAD, 0xC0, 0xDE,
    0x8B, 0xAD, 0xF0, 0x0D, 0x0D, 0x15, 0xEA, 0x5E,
])

# Algorithme identifié — Sections 24.1 et 24.2
# - AES-256-CBC : confirmé par EVP_aes_256_cbc dans nm -D, et par la
#   S-box AES trouvée dans crypto_static via scan de constantes magiques.
# - SHA-256 : confirmé par le symbole SHA256 dans nm -D, et par les IV
#   SHA-256 (0x6A09E667...) trouvés dans crypto_static.
# - OpenSSL : confirmé par ldd (libcrypto.so.3) et strings ("OpenSSL ...").
ALGORITHM = "AES-256-CBC"
HASH_ALGO = "SHA-256"
KEY_LEN = 32   # AES-256
IV_LEN = 16    # AES block size
BLOCK_SIZE = 16


# ============================================================================
# Dérivation de clé — reproduit derive_key() du binaire
# ============================================================================

def derive_key(passphrase: bytes, mask: bytes) -> bytes:
    """
    Reproduit la fonction derive_key() de crypto.c :
        1. sha_hash = SHA-256(passphrase)
        2. key[i] = sha_hash[i] ^ mask[i]  pour i in 0..31

    Identifiée via :
        - GDB : break derive_key (sur crypto_O0), step through
        - Ghidra : XREF depuis les constantes SHA-256 → fonction appelante
          → boucle XOR avec KEY_MASK
    """
    sha_hash = hashlib.sha256(passphrase).digest()
    key = bytes(h ^ m for h, m in zip(sha_hash, mask))
    return key


# ============================================================================
# Parsing du format CRYPT24 — reconstruit en section 24.4
# ============================================================================

MAGIC = b"CRYPT24\x00"

def parse_crypt24(filepath: str) -> dict:
    """
    Parse un fichier au format CRYPT24.

    Structure identifiée via :
        - ImHex : inspection visuelle des premiers octets
        - Data Inspector : confirmation uint16 LE pour iv_length,
          uint32 LE pour original_size
        - Analyse d'entropie : transition nette à l'offset 0x20
        - Pattern .hexpat : validation automatique des assertions

    Retourne un dict avec les champs du header et le ciphertext.
    """
    with open(filepath, "rb") as f:
        data = f.read()

    # Vérifications minimales de taille
    if len(data) < 0x20:
        raise ValueError(
            f"Fichier trop court ({len(data)} octets, minimum 32 pour le header)"
        )

    # ── Magic (0x00, 8 octets) ──────────────────────────────────
    magic = data[0x00:0x08]
    if magic != MAGIC:
        raise ValueError(
            f"Magic invalide : {magic!r} (attendu {MAGIC!r})\n"
            f"Ce fichier n'est pas au format CRYPT24."
        )

    # ── Version (0x08, 2 octets) ────────────────────────────────
    version_major = data[0x08]
    version_minor = data[0x09]
    if version_major != 1:
        raise ValueError(
            f"Version majeure {version_major} non supportée "
            f"(ce script supporte uniquement la version 1.x)"
        )

    # ── Longueur de l'IV (0x0A, uint16 LE) ─────────────────────
    iv_length = struct.unpack_from("<H", data, 0x0A)[0]
    if iv_length not in (8, 12, 16):
        raise ValueError(
            f"Longueur d'IV inattendue : {iv_length} "
            f"(attendu 8, 12 ou 16 octets)"
        )

    # ── IV (0x0C, iv_length octets) ─────────────────────────────
    iv_start = 0x0C
    iv_end = iv_start + iv_length
    iv = data[iv_start:iv_end]

    # ── Taille originale (après IV, uint32 LE) ──────────────────
    orig_size_offset = iv_end
    original_size = struct.unpack_from("<I", data, orig_size_offset)[0]

    # ── Ciphertext (après original_size, jusqu'à EOF) ───────────
    ct_offset = orig_size_offset + 4
    ciphertext = data[ct_offset:]

    # Validation : le ciphertext doit être un multiple de BLOCK_SIZE
    if len(ciphertext) == 0:
        raise ValueError("Ciphertext vide")
    if len(ciphertext) % BLOCK_SIZE != 0:
        raise ValueError(
            f"Taille du ciphertext ({len(ciphertext)}) n'est pas un "
            f"multiple de {BLOCK_SIZE} — fichier corrompu ou mauvais format"
        )

    return {
        "version": (version_major, version_minor),
        "iv_length": iv_length,
        "iv": iv,
        "original_size": original_size,
        "ciphertext": ciphertext,
        "ct_offset": ct_offset,
    }


# ============================================================================
# Déchiffrement AES-256-CBC
# ============================================================================

def decrypt_aes256_cbc(ciphertext: bytes, key: bytes, iv: bytes) -> bytes:
    """
    Déchiffre un buffer AES-256-CBC et retire le padding PKCS7.

    Lève Crypto.Util.Padding.PaddingError si le padding est invalide,
    ce qui indique généralement une clé ou un IV incorrect.
    """
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    plaintext_padded = cipher.decrypt(ciphertext)
    plaintext = unpad(plaintext_padded, BLOCK_SIZE)
    return plaintext


# ============================================================================
# Vérification par re-chiffrement (round-trip)
# ============================================================================

def verify_roundtrip(plaintext: bytes, key: bytes, iv: bytes,
                     expected_ct: bytes) -> bool:
    """
    Re-chiffre le plaintext avec les mêmes paramètres et vérifie que
    le ciphertext obtenu est identique à l'original.

    C'est la preuve la plus forte que le RE est correct : si le
    round-trip fonctionne, on a reproduit exactement le comportement
    du binaire.
    """
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    re_encrypted = cipher.encrypt(pad(plaintext, BLOCK_SIZE))
    return re_encrypted == expected_ct


# ============================================================================
# Point d'entrée
# ============================================================================

def main():
    # ── Arguments ───────────────────────────────────────────────
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <fichier.enc> [fichier_sortie]")
        print()
        print("Déchiffre un fichier au format CRYPT24 produit par ch24-crypto.")
        print("Si aucun fichier de sortie n'est spécifié, affiche le contenu.")
        sys.exit(1)

    enc_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None

    print(f"[*] Checkpoint Chapitre 24 — Déchiffrement de {enc_path}")
    print(f"    Algorithme : {ALGORITHM}")
    print(f"    Hash KDF   : {HASH_ALGO}")
    print()

    # ── Étape 1 : Dériver la clé ───────────────────────────────
    print("[1/5] Dérivation de la clé...")
    key = derive_key(PASSPHRASE, KEY_MASK)
    sha_hex = hashlib.sha256(PASSPHRASE).hexdigest()

    print(f"      Passphrase      : {PASSPHRASE.decode()}")
    print(f"      SHA-256(phrase) : {sha_hex}")
    print(f"      Masque XOR      : {KEY_MASK.hex()}")
    print(f"      Clé dérivée     : {key.hex()}")
    print()

    # ── Étape 2 : Parser le fichier .enc ───────────────────────
    print(f"[2/5] Parsing de {enc_path}...")
    try:
        parts = parse_crypt24(enc_path)
    except (ValueError, FileNotFoundError) as e:
        print(f"      ERREUR : {e}")
        sys.exit(1)

    print(f"      Version         : "
          f"{parts['version'][0]}.{parts['version'][1]}")
    print(f"      IV ({parts['iv_length']} octets)   : {parts['iv'].hex()}")
    print(f"      Taille originale: {parts['original_size']} octets")
    print(f"      Ciphertext      : {len(parts['ciphertext'])} octets "
          f"(début à offset 0x{parts['ct_offset']:02X})")
    padding_size = len(parts["ciphertext"]) - parts["original_size"]
    print(f"      Padding attendu : {padding_size} octets (PKCS7)")
    print()

    # ── Étape 3 : Déchiffrer ──────────────────────────────────
    print("[3/5] Déchiffrement AES-256-CBC...")
    try:
        plaintext = decrypt_aes256_cbc(
            parts["ciphertext"], key, parts["iv"]
        )
    except ValueError as e:
        print(f"      ERREUR de padding : {e}")
        print()
        print("      Causes probables :")
        print("        - Clé incorrecte (vérifier la passphrase et le masque)")
        print("        - IV incorrect (vérifier le parsing du header)")
        print("        - Mauvais mode (vérifier que c'est bien CBC)")
        print("        - Fichier corrompu")
        sys.exit(1)

    print(f"      Déchiffré : {len(plaintext)} octets")
    print()

    # ── Étape 4 : Valider ─────────────────────────────────────
    print("[4/5] Validation...")

    # 4a. Vérifier la taille
    size_ok = len(plaintext) == parts["original_size"]
    if size_ok:
        print(f"      [OK] Taille : {len(plaintext)} octets "
              f"== original_size ({parts['original_size']})")
    else:
        print(f"      [!!] Taille : {len(plaintext)} octets "
              f"!= original_size ({parts['original_size']})")

    # 4b. Vérifier le round-trip
    roundtrip_ok = verify_roundtrip(
        plaintext, key, parts["iv"], parts["ciphertext"]
    )
    if roundtrip_ok:
        print(f"      [OK] Round-trip : re-chiffrement identique au ciphertext")
    else:
        print(f"      [!!] Round-trip : le re-chiffrement ne correspond pas")

    print()

    # ── Étape 5 : Résultat ────────────────────────────────────
    print("[5/5] Résultat")

    if out_path:
        with open(out_path, "wb") as f:
            f.write(plaintext)
        print(f"      Écrit dans : {out_path}")
        print()
        print(f"      Validation finale :")
        print(f"        $ diff secret.txt {out_path}")
        print(f"        (aucune sortie = succès)")
    else:
        print()
        print("=" * 64)
        try:
            print(plaintext.decode("utf-8"), end="")
        except UnicodeDecodeError:
            print(f"(données binaires, {len(plaintext)} octets)")
            # Afficher les 256 premiers octets en hex
            preview = plaintext[:256]
            for i in range(0, len(preview), 16):
                chunk = preview[i:i+16]
                hex_part = " ".join(f"{b:02x}" for b in chunk)
                ascii_part = "".join(
                    chr(b) if 32 <= b < 127 else "." for b in chunk
                )
                print(f"  {i:04x}  {hex_part:<48s}  {ascii_part}")
            if len(plaintext) > 256:
                print(f"  ... ({len(plaintext) - 256} octets restants)")
        print("=" * 64)

    # ── Résumé ────────────────────────────────────────────────
    print()
    all_ok = size_ok and roundtrip_ok
    if all_ok:
        print("[+] CHECKPOINT RÉUSSI")
        print("    Le fichier a été déchiffré avec succès.")
        print("    La dérivation de clé et le format sont correctement reproduits.")
    else:
        print("[-] CHECKPOINT INCOMPLET")
        print("    Le déchiffrement a produit un résultat mais les validations")
        print("    ont échoué. Revérifier la clé, l'IV et le parsing du format.")

    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
