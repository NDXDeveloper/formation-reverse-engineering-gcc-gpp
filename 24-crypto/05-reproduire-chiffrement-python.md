🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 24.5 — Reproduire le schéma de chiffrement en Python

> 🎯 **Objectif de cette section** : assembler toutes les pièces collectées dans les sections précédentes pour écrire un script Python autonome capable de déchiffrer `secret.enc`, puis valider le résultat. C'est l'aboutissement concret de tout le chapitre.

---

## Inventaire : ce qu'on sait

Avant d'écrire la moindre ligne de code, faisons le point sur les informations accumulées au fil des sections.

**De la section 24.1** (identification par constantes) : le binaire utilise AES et SHA-256.

**De la section 24.2** (identification de la bibliothèque) : les routines proviennent d'OpenSSL, via l'API EVP. Le chiffrement est AES-256-CBC.

**De la section 24.3** (extraction mémoire) : on a capturé trois éléments décisifs :
- La passphrase : `r3vers3_m3_1f_y0u_c4n!`  
- Le masque XOR appliqué après le hash SHA-256 (32 octets dans `.rodata`) :
  `DE AD BE EF CA FE BA BE 13 37 42 42 FE ED FA CE 0B AD F0 0D DE AD C0 DE 8B AD F0 0D 0D 15 EA 5E`
- La logique de dérivation : `clé = SHA-256(passphrase) XOR masque`  
- L'IV et la clé finale (capturés au breakpoint sur `EVP_EncryptInit_ex`)

**De la section 24.4** (format du fichier) : la structure complète de `secret.enc` :

| Offset | Taille | Champ | Format |  
|---|---|---|---|  
| 0x00 | 8 | Magic | `"CRYPT24\0"` |  
| 0x08 | 1 | Version majeure | uint8 |  
| 0x09 | 1 | Version mineure | uint8 |  
| 0x0A | 2 | Longueur de l'IV | uint16 LE |  
| 0x0C | 16 | IV | octets bruts |  
| 0x1C | 4 | Taille originale | uint32 LE |  
| 0x20 | … | Ciphertext | AES-256-CBC, PKCS7 |

On a tout. Passons au code.

---

## Approche 1 : déchiffrement avec la clé brute

C'est l'approche la plus directe. On utilise la clé et l'IV capturés par GDB ou Frida (section 24.3), sans se soucier de la dérivation. On parse le fichier `.enc` selon le format documenté en section 24.4, et on déchiffre.

### Installation de la dépendance

```bash
$ pip install pycryptodome
```

`pycryptodome` est la bibliothèque crypto Python de référence. Elle fournit `AES`, `SHA256`, et tous les modes d'opération standard. C'est le pendant Python d'OpenSSL.

### Le script

```python
#!/usr/bin/env python3
"""
decrypt_raw.py — Déchiffrement de secret.enc avec la clé brute.

Approche "force brute mémorielle" : on utilise directement la clé  
capturée par GDB/Frida, sans reproduire la dérivation.  

Usage : python3 decrypt_raw.py secret.enc [output.txt]
"""

import sys  
import struct  
from Crypto.Cipher import AES  
from Crypto.Util.Padding import unpad  

# ── Clé brute capturée par GDB/Frida (section 24.3) ────────────
# Remplacer par les octets réels capturés lors de VOTRE exécution.
# Ceux-ci sont déterministes (même passphrase + même masque = même clé),
# donc ils seront identiques à chaque exécution du binaire.
RAW_KEY = bytes.fromhex(
    "a31f4b728ed05519c73a6188f20dae43"
    "5be9176cd482f03ea156c87d09bb4fe2"
)  # 32 octets — AES-256


def parse_crypt24(filepath):
    """Parse un fichier au format CRYPT24 et retourne ses composants."""

    with open(filepath, "rb") as f:
        data = f.read()

    # ── Magic (8 octets) ────────────────────────────────────────
    magic = data[0x00:0x08]
    if magic != b"CRYPT24\x00":
        raise ValueError(f"Magic invalide : {magic!r} (attendu b'CRYPT24\\x00')")

    # ── Version (2 octets) ──────────────────────────────────────
    version_major = data[0x08]
    version_minor = data[0x09]
    if version_major != 1:
        raise ValueError(f"Version majeure non supportée : {version_major}")

    # ── Longueur de l'IV (uint16 LE) ───────────────────────────
    iv_length = struct.unpack_from("<H", data, 0x0A)[0]
    if iv_length not in (8, 12, 16):
        raise ValueError(f"Longueur d'IV inattendue : {iv_length}")

    # ── IV ──────────────────────────────────────────────────────
    iv = data[0x0C : 0x0C + iv_length]

    # ── Taille originale (uint32 LE) ───────────────────────────
    header_end = 0x0C + iv_length
    original_size = struct.unpack_from("<I", data, header_end)[0]

    # ── Ciphertext ──────────────────────────────────────────────
    ciphertext_offset = header_end + 4
    ciphertext = data[ciphertext_offset:]

    return {
        "version": (version_major, version_minor),
        "iv_length": iv_length,
        "iv": iv,
        "original_size": original_size,
        "ciphertext": ciphertext,
    }


def decrypt_aes256_cbc(ciphertext, key, iv):
    """Déchiffre un buffer AES-256-CBC avec retrait du padding PKCS7."""

    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    plaintext_padded = cipher.decrypt(ciphertext)
    plaintext = unpad(plaintext_padded, AES.block_size)
    return plaintext


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <fichier.enc> [output]")
        sys.exit(1)

    enc_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None

    # 1. Parser le fichier
    print(f"[*] Parsing {enc_path}...")
    parts = parse_crypt24(enc_path)

    print(f"    Version:       {parts['version'][0]}.{parts['version'][1]}")
    print(f"    IV length:     {parts['iv_length']} bytes")
    print(f"    IV:            {parts['iv'].hex()}")
    print(f"    Original size: {parts['original_size']} bytes")
    print(f"    Ciphertext:    {len(parts['ciphertext'])} bytes")

    # 2. Déchiffrer
    print(f"[*] Decrypting with raw key...")
    plaintext = decrypt_aes256_cbc(parts["ciphertext"], RAW_KEY, parts["iv"])

    # 3. Valider la taille
    if len(plaintext) != parts["original_size"]:
        print(f"[!] Warning: decrypted size ({len(plaintext)}) "
              f"!= original_size ({parts['original_size']})")
    else:
        print(f"[+] Size matches: {len(plaintext)} bytes")

    # 4. Afficher ou sauvegarder
    if out_path:
        with open(out_path, "wb") as f:
            f.write(plaintext)
        print(f"[+] Decrypted content written to {out_path}")
    else:
        print(f"\n{'='*60}")
        print(f"Decrypted content:")
        print(f"{'='*60}")
        try:
            print(plaintext.decode("utf-8"))
        except UnicodeDecodeError:
            print(f"(binary data, {len(plaintext)} bytes)")
            print(plaintext[:256].hex())


if __name__ == "__main__":
    main()
```

### Exécution

```bash
$ python3 decrypt_raw.py secret.enc
[*] Parsing secret.enc...
    Version:       1.0
    IV length:     16 bytes
    IV:            9c712eb538f4a06d1c83e752bf4906da
    Original size: 342 bytes
    Ciphertext:    352 bytes
[*] Decrypting with raw key...
[+] Size matches: 342 bytes

============================================================
Decrypted content:
============================================================
=== FICHIER CONFIDENTIEL ===

Projet : Operation Midnight Sun  
Classification : TOP SECRET  
...
```

Le contenu de `secret.txt` est intégralement récupéré. Le fichier a été déchiffré avec succès.

---

## Approche 2 : reproduire la dérivation complète

L'approche 1 fonctionne, mais elle dépend d'une clé capturée pour une exécution précise. Si on veut un outil générique — capable de déchiffrer n'importe quel fichier produit par le binaire, sans avoir besoin de relancer GDB à chaque fois — il faut reproduire la dérivation de clé dans le script Python.

Rappel de la logique reconstruite en section 24.3 :

```
1. Construire la passphrase : "r3vers3_m3_1f_y0u_c4n!"
2. Calculer hash = SHA-256(passphrase)
3. Pour i de 0 à 31 : key[i] = hash[i] XOR masque[i]
```

### Le script avec dérivation

```python
#!/usr/bin/env python3
"""
decrypt_full.py — Déchiffrement de secret.enc avec reproduction
                   complète de la dérivation de clé.

Ce script est autonome : il ne dépend d'aucune donnée capturée  
par GDB/Frida. Toute la logique est reconstruite depuis le RE.  

Usage : python3 decrypt_full.py secret.enc [output.txt]
"""

import sys  
import struct  
import hashlib  
from Crypto.Cipher import AES  
from Crypto.Util.Padding import unpad  

# ── Passphrase hardcodée (reconstruite en section 24.3) ────────
# Trouvée via breakpoint sur build_passphrase() dans GDB.
# Construite en 3 morceaux dans le binaire pour échapper à `strings`.
PASSPHRASE = b"r3vers3_m3_1f_y0u_c4n!"

# ── Masque XOR (extrait de .rodata, section 24.3) ──────────────
# Visible dans Ghidra comme variable globale KEY_MASK[32].
KEY_MASK = bytes([
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x13, 0x37, 0x42, 0x42, 0xFE, 0xED, 0xFA, 0xCE,
    0x0B, 0xAD, 0xF0, 0x0D, 0xDE, 0xAD, 0xC0, 0xDE,
    0x8B, 0xAD, 0xF0, 0x0D, 0x0D, 0x15, 0xEA, 0x5E,
])


def derive_key(passphrase, mask):
    """Reproduit la dérivation de clé du binaire crypto.c."""

    # Étape 1 : SHA-256 de la passphrase
    sha_hash = hashlib.sha256(passphrase).digest()

    # Étape 2 : XOR avec le masque
    key = bytes(h ^ m for h, m in zip(sha_hash, mask))

    return key


def parse_crypt24(filepath):
    """Parse un fichier au format CRYPT24."""

    with open(filepath, "rb") as f:
        data = f.read()

    magic = data[0x00:0x08]
    if magic != b"CRYPT24\x00":
        raise ValueError(f"Magic invalide : {magic!r}")

    version_major = data[0x08]
    version_minor = data[0x09]
    if version_major != 1:
        raise ValueError(f"Version majeure non supportée : {version_major}")

    iv_length = struct.unpack_from("<H", data, 0x0A)[0]
    iv = data[0x0C : 0x0C + iv_length]

    header_end = 0x0C + iv_length
    original_size = struct.unpack_from("<I", data, header_end)[0]

    ciphertext_offset = header_end + 4
    ciphertext = data[ciphertext_offset:]

    return {
        "version": (version_major, version_minor),
        "iv": iv,
        "original_size": original_size,
        "ciphertext": ciphertext,
    }


def decrypt_aes256_cbc(ciphertext, key, iv):
    """Déchiffre en AES-256-CBC avec retrait du padding PKCS7."""

    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    plaintext_padded = cipher.decrypt(ciphertext)
    plaintext = unpad(plaintext_padded, AES.block_size)
    return plaintext


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <fichier.enc> [output]")
        sys.exit(1)

    enc_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None

    # 1. Dériver la clé (reproduit derive_key() du binaire)
    print("[*] Deriving key...")
    key = derive_key(PASSPHRASE, KEY_MASK)
    print(f"    Passphrase:  {PASSPHRASE.decode()}")
    print(f"    SHA-256:     {hashlib.sha256(PASSPHRASE).hexdigest()}")
    print(f"    Key (after XOR mask): {key.hex()}")

    # 2. Parser le fichier .enc
    print(f"[*] Parsing {enc_path}...")
    parts = parse_crypt24(enc_path)
    print(f"    IV:            {parts['iv'].hex()}")
    print(f"    Original size: {parts['original_size']} bytes")
    print(f"    Ciphertext:    {len(parts['ciphertext'])} bytes")

    # 3. Déchiffrer
    print("[*] Decrypting...")
    plaintext = decrypt_aes256_cbc(parts["ciphertext"], key, parts["iv"])

    # 4. Valider
    if len(plaintext) != parts["original_size"]:
        print(f"[!] Warning: size mismatch "
              f"({len(plaintext)} vs {parts['original_size']})")
    else:
        print(f"[+] Size validated: {len(plaintext)} bytes")

    # 5. Résultat
    if out_path:
        with open(out_path, "wb") as f:
            f.write(plaintext)
        print(f"[+] Written to {out_path}")
    else:
        print(f"\n{'='*60}")
        try:
            print(plaintext.decode("utf-8"))
        except UnicodeDecodeError:
            print(f"(binary content, {len(plaintext)} bytes)")
        print(f"{'='*60}")

    # 6. Vérification finale : re-chiffrer et comparer
    print("\n[*] Verification: re-encrypting and comparing...")
    cipher_check = AES.new(key, AES.MODE_CBC, iv=parts["iv"])
    from Crypto.Util.Padding import pad
    re_encrypted = cipher_check.encrypt(pad(plaintext, AES.block_size))
    if re_encrypted == parts["ciphertext"]:
        print("[+] Re-encryption matches — decryption is correct.")
    else:
        print("[!] Re-encryption mismatch — something is wrong.")


if __name__ == "__main__":
    main()
```

### Exécution

```bash
$ python3 decrypt_full.py secret.enc decrypted.txt
[*] Deriving key...
    Passphrase:  r3vers3_m3_1f_y0u_c4n!
    SHA-256:     7db2f59d...  (hash brut de la passphrase)
    Key (after XOR mask): a31f4b728ed05519...
[*] Parsing secret.enc...
    IV:            9c712eb538f4a06d1c83e752bf4906da
    Original size: 342 bytes
    Ciphertext:    352 bytes
[*] Decrypting...
[+] Size validated: 342 bytes
[+] Written to decrypted.txt

[*] Verification: re-encrypting and comparing...
[+] Re-encryption matches — decryption is correct.
```

La clé dérivée par Python correspond exactement à celle capturée par GDB. Le fichier est déchiffré. La re-chiffrée correspond octet par octet au ciphertext original. Le schéma est intégralement reproduit.

---

## Validation croisée : comparaison avec le fichier original

La dernière vérification, la plus satisfaisante, est de comparer le fichier déchiffré avec l'original :

```bash
$ diff secret.txt decrypted.txt && echo "Identiques !" || echo "Différents !"
Identiques !

$ sha256sum secret.txt decrypted.txt
a1b2c3d4...  secret.txt  
a1b2c3d4...  decrypted.txt  
```

Les hash sont identiques. Le reverse engineering est complet.

---

## Retour sur la méthodologie : les deux approches et quand les utiliser

Les deux scripts illustrent deux philosophies de RE crypto qui sont complémentaires.

### Approche « clé brute » (decrypt_raw.py)

On capture la clé en mémoire et on déchiffre directement. C'est rapide, fiable, et ne nécessite pas de comprendre la dérivation.

**Quand l'utiliser** : urgence (incident de sécurité, ransomware à déchiffrer immédiatement), binaire trop obfusqué pour comprendre la dérivation dans un délai raisonnable, ou cas où la clé vient de l'extérieur (saisie utilisateur, reçue par le réseau) et ne peut pas être re-dérivée à partir du binaire seul.

**Limitation** : la clé capturée est valable pour *une* exécution. Si le binaire dérive une clé différente à chaque fois (basée sur un timestamp, un identifiant machine, un sel aléatoire…), il faut capturer la clé à chaque fois. Ce n'est pas un outil autonome.

### Approche « dérivation reproduite » (decrypt_full.py)

On reconstruit intégralement la logique de génération de clé. Le script est autonome : il peut déchiffrer n'importe quel fichier produit par le binaire, sans intervention manuelle.

**Quand l'utiliser** : analyse approfondie, rédaction d'un rapport, création d'un outil de déchiffrement distribuable (pour les victimes d'un ransomware par exemple), ou quand on veut aussi pouvoir *chiffrer* (pour tester, créer des fichiers de test, ou écrire un outil d'interopérabilité).

**Limitation** : nécessite de comprendre la totalité de la chaîne de dérivation, ce qui peut être très coûteux en temps si elle est complexe ou obfusquée.

### En pratique

On commence presque toujours par l'approche clé brute (pour avoir un résultat rapide et confirmer que notre compréhension de l'algorithme et du format est correcte), puis on investit dans la dérivation reproduite si le contexte le justifie.

---

## Pièges courants et dépannage

### « Le déchiffrement produit du bruit »

Causes probables, par ordre de fréquence :

1. **Mauvais offset du ciphertext.** On déchiffre une partie du header ou on saute des octets de ciphertext. Vérifier que l'offset de début correspond exactement à la carte du format (section 24.4).

2. **Mauvais mode d'opération.** CBC, CTR, GCM et ECB ne sont pas interchangeables. Si le binaire utilise CTR et qu'on déchiffre en CBC, le résultat est du bruit. Revérifier l'appel à `EVP_EncryptInit_ex` (section 24.3) : le deuxième argument identifie le mode.

3. **Clé ou IV incorrects.** Un seul octet faux suffit à produire du bruit complet. Comparer octet par octet les valeurs utilisées en Python avec celles capturées en mémoire.

4. **Endianness.** Si la clé est dérivée d'un hash stocké comme un tableau de `uint32_t`, l'endianness du système affecte l'ordre des octets. Python et x86-64 sont tous deux little-endian pour les entiers, mais les fonctions de hash retournent des octets dans l'ordre big-endian (digest order). Vérifier que les conversions sont cohérentes.

### « Le padding est invalide »

`unpad()` lève une exception `ValueError` si les derniers octets du plaintext déchiffré ne forment pas un padding PKCS7 valide. Cela signifie généralement que la clé, l'IV ou le mode est faux — le déchiffrement a « fonctionné » mathématiquement mais a produit du bruit, et le bruit ne ressemble pas à du padding valide.

Conseil de débogage : remplacer temporairement `unpad(...)` par un accès direct au buffer brut et inspecter les derniers 16 octets. S'ils sont aléatoires, le problème est en amont (clé/IV/mode). S'ils ressemblent à du padding mais avec une valeur inattendue, le problème est peut-être un schéma de padding non standard (zéros, ANSI X.923, ISO 10126).

### « Le premier bloc est correct mais le reste est du bruit »

En mode CBC, si l'IV est faux mais la clé est correcte, seul le premier bloc de 16 octets est corrompu — le reste se déchiffre normalement (car chaque bloc suivant dépend du bloc chiffré précédent, pas de l'IV). C'est un symptôme très caractéristique qui pointe directement vers un problème d'IV.

Inversement, si le premier bloc est correct mais que tous les suivants sont du bruit, cela peut indiquer qu'on a accidentellement utilisé le mode ECB (chaque bloc est indépendant) au lieu de CBC, et que le premier bloc correspond par chance.

---

## Aller plus loin : écrire un outil de chiffrement

Si on voulait produire des fichiers `.enc` compatibles avec le binaire (pour du fuzzing, des tests, ou de l'interopérabilité), il suffit d'inverser le processus :

```python
def encrypt_file(input_path, output_path):
    """Chiffre un fichier au format CRYPT24 (compatible avec le binaire)."""

    import os
    from Crypto.Util.Padding import pad

    # Lire le plaintext
    with open(input_path, "rb") as f:
        plaintext = f.read()

    # Dériver la clé
    key = derive_key(PASSPHRASE, KEY_MASK)

    # Générer un IV aléatoire
    iv = os.urandom(16)

    # Chiffrer
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    ciphertext = cipher.encrypt(pad(plaintext, AES.block_size))

    # Écrire le fichier .enc
    with open(output_path, "wb") as f:
        f.write(b"CRYPT24\x00")                          # Magic
        f.write(struct.pack("BB", 1, 0))                  # Version 1.0
        f.write(struct.pack("<H", len(iv)))                # IV length
        f.write(iv)                                        # IV
        f.write(struct.pack("<I", len(plaintext)))         # Original size
        f.write(ciphertext)                                # Ciphertext
```

Un fichier produit par cette fonction est structurellement identique à ceux produits par le binaire C. C'est la preuve ultime que le RE est complet : on a non seulement compris le schéma, mais on l'a reproduit de manière interchangeable.

---

## Récapitulatif du chapitre

Ce chapitre a suivi un fil rouge unique — déchiffrer `secret.enc` — à travers cinq étapes qui forment une méthodologie générale applicable à tout binaire utilisant de la cryptographie :

| Étape | Section | Question | Résultat |  
|---|---|---|---|  
| 1 | 24.1 | Quel algorithme ? | AES-256 + SHA-256 (constantes magiques) |  
| 2 | 24.2 | Quelle implémentation ? | OpenSSL, API EVP (ldd, nm, signatures) |  
| 3 | 24.3 | Où sont les secrets ? | Clé, IV, passphrase (GDB, Frida) |  
| 4 | 24.4 | Comment sont emballées les données ? | Format CRYPT24 documenté (ImHex, .hexpat) |  
| 5 | 24.5 | Peut-on reproduire le schéma ? | Script Python autonome, validation croisée |

Chaque étape alimente la suivante. Sauter une étape, c'est risquer de perdre du temps sur la suivante. Les appliquer dans l'ordre, c'est transformer un binaire opaque en un problème résolu méthodiquement.

> 🎯 **Checkpoint du chapitre** : déchiffrer le fichier `secret.enc` fourni en extrayant la clé du binaire. Vous disposez maintenant de deux approches (clé brute et dérivation reproduite). Pour valider le checkpoint, produisez un script `decrypt.py` fonctionnel et vérifiez que `diff secret.txt decrypted.txt` ne retourne aucune différence.

---


⏭️ [🎯 Checkpoint : déchiffrer le fichier `secret.enc` fourni en extrayant la clé du binaire](/24-crypto/checkpoint.md)
