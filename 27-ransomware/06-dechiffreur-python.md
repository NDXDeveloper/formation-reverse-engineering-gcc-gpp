🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 27.6 — Écriture du déchiffreur Python

> 🎯 **Objectif de cette section** : exploiter les paramètres cryptographiques extraits lors de l'analyse (clé, IV, algorithme, format de fichier) pour écrire un script Python capable de restaurer les fichiers chiffrés par le sample. Ce déchiffreur est le livrable technique le plus concret de l'analyse — en contexte d'incident réel, c'est l'outil qui permet aux victimes de récupérer leurs données sans payer de rançon.  
>  
> 📁 Le script final est archivé dans `solutions/ch27-checkpoint-decryptor.py`.

---

## Inventaire de ce que nous savons

Avant d'écrire la moindre ligne de code, rassemblons les faits confirmés par l'analyse statique (27.3) et dynamique (27.5) :

| Paramètre | Valeur | Source de confirmation |  
|---|---|---|  
| Algorithme | AES-256-CBC | XREF vers `EVP_aes_256_cbc()` dans Ghidra + appel observé en dynamique |  
| Clé (32 octets) | `52 45 56 45 ... 32 35 21` (`REVERSE_ENGINEERING_IS_FUN_2025!`) | Argument `rcx` de `EVP_EncryptInit_ex` capturé par GDB et Frida |  
| IV (16 octets) | `DE AD BE EF CA FE BA BE 13 37 42 42 FE ED FA CE` | Argument `r8` de `EVP_EncryptInit_ex` capturé par GDB et Frida |  
| Padding | PKCS#7 (par défaut dans OpenSSL EVP) | Taille ciphertext = prochain multiple de 16 ≥ taille plaintext |  
| Rotation de clé | Aucune — clé et IV identiques pour chaque fichier | Script GDB : 6 appels, mêmes valeurs |  
| Format `.locked` | `[magic 8B][orig_size 8B][ciphertext NB]` | Pattern ImHex + `fwrite` dans Ghidra |  
| Magic header | `RWARE27\0` (8 octets) | Offset 0x00 du fichier `.locked` |  
| Taille originale | `uint64_t` little-endian à l'offset 0x08 | Pattern ImHex + `fwrite(&orig_size, 8, 1, fp)` dans Ghidra |  
| Données chiffrées | À partir de l'offset 0x10 jusqu'à EOF | Analyse ImHex |

Chacune de ces informations va se traduire directement en une constante ou une étape du déchiffreur.

---

## Choix techniques

### Bibliothèque cryptographique

Nous utiliserons le module `cryptography` de Python, qui fournit une API haut niveau pour AES-CBC. C'est la bibliothèque recommandée par la communauté Python pour la cryptographie — elle s'appuie sur OpenSSL en interne, ce qui garantit une compatibilité parfaite avec le chiffrement produit par notre sample.

```bash
pip install cryptography
```

Une alternative serait `pycryptodome` (`from Crypto.Cipher import AES`), tout aussi valide. Nous privilégions `cryptography` pour sa cohérence avec l'écosystème OpenSSL sous-jacent.

### Stratégie de troncature

Le mode AES-CBC avec padding PKCS#7 ajoute entre 1 et 16 octets au plaintext. Après déchiffrement, il faut retirer ce padding pour obtenir le fichier original exact. Deux approches sont possibles :

1. **Laisser la bibliothèque gérer le unpadding** — `cryptography` retire automatiquement le padding PKCS#7 si on utilise le mécanisme de `finalize()` avec un `unpadder`. C'est la méthode canonique.  
2. **Tronquer à la taille originale** — Le header du fichier `.locked` stocke la taille originale à l'offset 0x08. On peut simplement déchiffrer puis tronquer le résultat à cette taille.

Nous implémenterons les deux approches. La première est la bonne pratique cryptographique ; la seconde est un filet de sécurité qui exploite la redondance d'information laissée par l'auteur du sample.

---

## Construction du déchiffreur, étape par étape

### Étape 1 — Définir les constantes extraites de l'analyse

La première traduction directe de nos résultats de RE en code Python :

```python
#!/usr/bin/env python3
"""
Déchiffreur pour le ransomware pédagogique du Chapitre 27.  
Restaure les fichiers .locked chiffrés en AES-256-CBC.  

Paramètres cryptographiques extraits par analyse statique (Ghidra/ImHex)  
et confirmés par analyse dynamique (GDB/Frida).  

Usage :
    python3 decryptor.py                      # déchiffre tout /tmp/test/
    python3 decryptor.py fichier.txt.locked   # déchiffre un fichier spécifique
    python3 decryptor.py --dry-run             # simule sans écrire
"""

import sys  
import os  
import struct  
import argparse  

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes  
from cryptography.hazmat.primitives import padding  
from cryptography.hazmat.backends import default_backend  

# ── Constantes extraites par RE ──────────────────────────────────────────────

# Clé AES-256 (32 octets) — extraite de .rodata, confirmée via $rcx sur
# EVP_EncryptInit_ex (GDB breakpoint + Frida hook)
AES_KEY = bytes([
    0x52, 0x45, 0x56, 0x45, 0x52, 0x53, 0x45, 0x5F,  # REVERSE_
    0x45, 0x4E, 0x47, 0x49, 0x4E, 0x45, 0x45, 0x52,  # ENGINEER
    0x49, 0x4E, 0x47, 0x5F, 0x49, 0x53, 0x5F, 0x46,  # ING_IS_F
    0x55, 0x4E, 0x5F, 0x32, 0x30, 0x32, 0x35, 0x21,  # UN_2025!
])

# IV AES-CBC (16 octets) — extrait de .rodata, confirmé via $r8 sur
# EVP_EncryptInit_ex (GDB breakpoint + Frida hook)
AES_IV = bytes([
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x13, 0x37, 0x42, 0x42, 0xFE, 0xED, 0xFA, 0xCE,
])

# Format du fichier .locked — cartographié avec ImHex (section 27.3)
MAGIC_HEADER = b"RWARE27\x00"      # 8 octets à l'offset 0x00  
HEADER_SIZE  = 16                   # 8 (magic) + 8 (orig_size)  
LOCKED_EXT   = ".locked"  
```

Chaque constante est accompagnée d'un commentaire traçant sa provenance. Cette traçabilité est essentielle : si un collègue reprend le script, il doit pouvoir vérifier chaque valeur dans le rapport d'analyse.

### Étape 2 — Parser le header du fichier `.locked`

Le header fait 16 octets. Nous devons en extraire le magic (pour validation) et la taille originale (pour troncature) :

```python
def parse_locked_header(filepath):
    """
    Lit et valide le header d'un fichier .locked.
    
    Format (little-endian) :
        [0x00 - 0x07]  Magic : "RWARE27\0"
        [0x08 - 0x0F]  Taille originale : uint64_t LE
    
    Retourne (original_size, ciphertext_bytes) ou lève une exception.
    """
    with open(filepath, "rb") as f:
        header = f.read(HEADER_SIZE)
        
        if len(header) < HEADER_SIZE:
            raise ValueError(f"Fichier trop petit ({len(header)} octets) : {filepath}")
        
        # Vérifier le magic
        magic = header[0:8]
        if magic != MAGIC_HEADER:
            raise ValueError(
                f"Magic header invalide : attendu {MAGIC_HEADER!r}, "
                f"obtenu {magic!r} dans {filepath}"
            )
        
        # Extraire la taille originale (uint64_t little-endian)
        original_size = struct.unpack("<Q", header[8:16])[0]
        
        # Lire le reste = données chiffrées
        ciphertext = f.read()
    
    return original_size, ciphertext
```

Le `struct.unpack("<Q", ...)` décode un entier non-signé de 8 octets en little-endian — exactement ce que le sample écrit avec `fwrite(&orig_size, sizeof(uint64_t), 1, fp)` sur une architecture x86-64.

La validation du magic est une mesure de sécurité : si l'utilisateur passe un fichier qui n'a pas été chiffré par notre sample, le déchiffreur refuse de le traiter plutôt que de produire un résultat corrompu.

### Étape 3 — Déchiffrer le payload AES-256-CBC

```python
def decrypt_aes256cbc(ciphertext, key, iv):
    """
    Déchiffre un buffer AES-256-CBC et retire le padding PKCS#7.
    
    Reproduit l'inverse exact de la séquence :
        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)
        EVP_EncryptUpdate(ctx, out, &len, in, in_len)
        EVP_EncryptFinal_ex(ctx, out + len, &len)
    """
    # Vérifications de cohérence
    if len(key) != 32:
        raise ValueError(f"Clé invalide : {len(key)} octets (attendu 32)")
    if len(iv) != 16:
        raise ValueError(f"IV invalide : {len(iv)} octets (attendu 16)")
    if len(ciphertext) == 0:
        raise ValueError("Ciphertext vide")
    if len(ciphertext) % 16 != 0:
        raise ValueError(
            f"Taille du ciphertext ({len(ciphertext)}) non multiple de 16 — "
            "corruption probable"
        )
    
    # Déchiffrement AES-256-CBC
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
    decryptor = cipher.decryptor()
    padded_plaintext = decryptor.update(ciphertext) + decryptor.finalize()
    
    # Retrait du padding PKCS#7
    unpadder = padding.PKCS7(128).unpadder()  # 128 = taille de bloc AES en bits
    plaintext = unpadder.update(padded_plaintext) + unpadder.finalize()
    
    return plaintext
```

Quelques points méritent attention :

**Vérification `len(ciphertext) % 16 != 0`** — Un ciphertext AES-CBC doit toujours être un multiple de la taille de bloc (16 octets). Si ce n'est pas le cas, le fichier est corrompu ou a été tronqué. Mieux vaut échouer proprement que de passer un buffer invalide à la bibliothèque crypto.

**`padding.PKCS7(128).unpadder()`** — Le paramètre `128` est la taille de bloc en **bits** (pas en octets). C'est une source d'erreur classique : AES a un bloc de 16 octets = 128 bits.

**Pourquoi `decryptor.update()` + `decryptor.finalize()` ?** — Cette séquence en deux étapes est le miroir exact de `EVP_DecryptUpdate` + `EVP_DecryptFinal_ex` côté OpenSSL. Le `finalize()` vérifie l'intégrité du padding ; si les données sont corrompues ou que la clé est fausse, il lèvera une exception `ValueError`.

### Étape 4 — Déchiffrer un fichier et restaurer l'original

```python
def decrypt_file(locked_path, dry_run=False):
    """
    Déchiffre un fichier .locked et restaure le fichier original.
    
    Étapes :
        1. Parser le header (magic + taille originale)
        2. Déchiffrer le payload AES-256-CBC
        3. Vérifier la cohérence taille déchiffrée vs taille annoncée
        4. Écrire le fichier restauré (sans l'extension .locked)
    
    Retourne le chemin du fichier restauré.
    """
    print(f"[*] Traitement : {locked_path}")
    
    # 1. Parser le header
    original_size, ciphertext = parse_locked_header(locked_path)
    print(f"    Taille originale annoncée : {original_size} octets")
    print(f"    Taille ciphertext :         {len(ciphertext)} octets")
    
    # 2. Déchiffrer
    try:
        plaintext = decrypt_aes256cbc(ciphertext, AES_KEY, AES_IV)
    except Exception as e:
        print(f"    [!] Échec du déchiffrement : {e}")
        return None
    
    # 3. Vérifier la cohérence
    #    Le unpadding PKCS#7 devrait déjà donner la bonne taille.
    #    La taille annoncée dans le header est une vérification croisée.
    if len(plaintext) != original_size:
        print(f"    [!] Incohérence de taille : déchiffré={len(plaintext)}, "
              f"annoncé={original_size}")
        print(f"    [!] Troncature à la taille annoncée.")
        plaintext = plaintext[:original_size]
    
    # 4. Déterminer le chemin de sortie : retirer .locked
    if locked_path.endswith(LOCKED_EXT):
        output_path = locked_path[:-len(LOCKED_EXT)]
    else:
        output_path = locked_path + ".decrypted"
    
    if dry_run:
        print(f"    [DRY-RUN] Fichier restauré : {output_path} "
              f"({len(plaintext)} octets)")
        return output_path
    
    # Écrire le fichier restauré
    with open(output_path, "wb") as f:
        f.write(plaintext)
    
    print(f"    [✓] Restauré : {output_path} ({len(plaintext)} octets)")
    return output_path
```

**Vérification croisée de taille** — En théorie, le retrait du padding PKCS#7 suffit à retrouver la taille originale. Mais le header stocke cette taille indépendamment, ce qui nous offre un mécanisme de double vérification. Si les deux tailles divergent, c'est un signal d'alarme : soit la clé est incorrecte (le unpadding produira un résultat aléatoire), soit le fichier est corrompu, soit le header a été manipulé. Le script signale l'incohérence et tronque par prudence à la taille annoncée.

**Mode `dry_run`** — Indispensable lors du développement : on peut valider que le parsing et le déchiffrement fonctionnent sans écrire sur le disque. Cela évite d'écraser accidentellement des données dans un lab déjà complexe.

### Étape 5 — Parcourir un répertoire

Pour traiter d'un coup tous les fichiers `.locked` d'une arborescence :

```python
def scan_and_decrypt(directory, dry_run=False):
    """
    Parcourt récursivement un répertoire et déchiffre tous les .locked.
    Retourne le nombre de fichiers restaurés avec succès.
    """
    success = 0
    errors  = 0
    skipped = 0
    
    for root, dirs, files in os.walk(directory):
        for filename in sorted(files):
            filepath = os.path.join(root, filename)
            
            if not filename.endswith(LOCKED_EXT):
                skipped += 1
                continue
            
            try:
                result = decrypt_file(filepath, dry_run=dry_run)
                if result:
                    success += 1
                else:
                    errors += 1
            except Exception as e:
                print(f"    [!] Erreur inattendue sur {filepath} : {e}")
                errors += 1
    
    print(f"\n{'=' * 50}")
    print(f"Résultat : {success} restauré(s), {errors} erreur(s), "
          f"{skipped} ignoré(s)")
    print(f"{'=' * 50}")
    
    return success
```

### Étape 6 — Point d'entrée CLI

```python
def main():
    parser = argparse.ArgumentParser(
        description="Déchiffreur pour le ransomware Ch27 (AES-256-CBC)",
        epilog="Formation Reverse Engineering — Chapitre 27"
    )
    parser.add_argument(
        "target",
        nargs="?",
        default="/tmp/test",
        help="Fichier .locked ou répertoire à traiter (défaut: /tmp/test)"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Simuler le déchiffrement sans écrire de fichier"
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Après déchiffrement, afficher les premiers octets du résultat"
    )
    args = parser.parse_args()
    
    print("=" * 50)
    print("  Déchiffreur — Chapitre 27")
    print(f"  Algorithme : AES-256-CBC")
    print(f"  Clé :        {AES_KEY.decode('ascii')}")
    print(f"  IV :         {AES_IV.hex()}")
    print("=" * 50)
    print()
    
    target = args.target
    
    if os.path.isfile(target):
        # Mode fichier unique
        result = decrypt_file(target, dry_run=args.dry_run)
        
        if result and args.verify and not args.dry_run:
            with open(result, "rb") as f:
                preview = f.read(128)
            print(f"\n    Aperçu ({min(128, len(preview))} premiers octets) :")
            try:
                print(f"    {preview.decode('utf-8', errors='replace')}")
            except Exception:
                print(f"    {preview.hex()}")
    
    elif os.path.isdir(target):
        # Mode répertoire
        scan_and_decrypt(target, dry_run=args.dry_run)
    
    else:
        print(f"[!] Cible introuvable : {target}")
        sys.exit(1)


if __name__ == "__main__":
    main()
```

---

## Script complet assemblé

Les six étapes ci-dessus, assemblées dans l'ordre, forment le script complet `decryptor.py`. Récapitulons sa structure :

```
decryptor.py
│
├── Constantes (AES_KEY, AES_IV, MAGIC_HEADER, HEADER_SIZE)
│
├── parse_locked_header(filepath)
│     → Lit le magic + la taille originale, retourne (size, ciphertext)
│
├── decrypt_aes256cbc(ciphertext, key, iv)
│     → Déchiffre AES-256-CBC + retire le padding PKCS#7
│
├── decrypt_file(locked_path, dry_run)
│     → Orchestre : parse → déchiffre → vérifie → écrit
│
├── scan_and_decrypt(directory, dry_run)
│     → os.walk récursif sur les .locked
│
└── main()
      → CLI argparse : cible, --dry-run, --verify
```

---

## Exécution et validation

### Test sur un fichier unique

```bash
$ python3 decryptor.py /tmp/test/document.txt.locked --verify
==================================================
  Déchiffreur — Chapitre 27
  Algorithme : AES-256-CBC
  Clé :        REVERSE_ENGINEERING_IS_FUN_2025!
  IV :         deadbeefcafebabe13374242feedface
==================================================

[*] Traitement : /tmp/test/document.txt.locked
    Taille originale annoncée : 47 octets
    Taille ciphertext :         48 octets
    [✓] Restauré : /tmp/test/document.txt (47 octets)

    Aperçu (47 premiers octets) :
    Ceci est un document strictement confidentiel.
```

Le contenu restauré correspond exactement au fichier original créé par `make testenv`. Le déchiffrement est fonctionnel.

### Test en mode répertoire complet

```bash
$ python3 decryptor.py /tmp/test/
[*] Traitement : /tmp/test/budget.csv.locked
    Taille originale annoncée : 58 octets
    Taille ciphertext :         64 octets
    [✓] Restauré : /tmp/test/budget.csv (58 octets)
[*] Traitement : /tmp/test/document.txt.locked
    Taille originale annoncée : 47 octets
    Taille ciphertext :         48 octets
    [✓] Restauré : /tmp/test/document.txt (47 octets)
[*] Traitement : /tmp/test/notes.md.locked
    ...
[*] Traitement : /tmp/test/sous-dossier/nested.txt.locked
    ...

==================================================
Résultat : 6 restauré(s), 0 erreur(s), 1 ignoré(s)
==================================================
```

Le fichier ignoré est `README_LOCKED.txt` — la ransom note, qui ne porte pas l'extension `.locked`.

### Validation d'intégrité par hash

Pour une validation rigoureuse, comparons les hash SHA-256 des fichiers originaux (avant chiffrement) avec ceux des fichiers restaurés :

```bash
# Avant chiffrement (à exécuter AVANT le ransomware, ou après make reset)
$ find /tmp/test -type f -exec sha256sum {} \; | sort > /tmp/hashes_before.txt

# Exécuter le ransomware
$ ./ransomware_O0

# Exécuter le déchiffreur
$ python3 decryptor.py /tmp/test/

# Après restauration
$ find /tmp/test -type f ! -name "*.locked" ! -name "README_LOCKED.txt" \
    -exec sha256sum {} \; | sort > /tmp/hashes_after.txt

# Comparer
$ diff /tmp/hashes_before.txt /tmp/hashes_after.txt
# (aucune différence = restauration parfaite)
```

Si `diff` ne produit aucune sortie, les fichiers restaurés sont **identiques bit pour bit** aux originaux. C'est la preuve ultime que le déchiffrement est correct.

---

## Gestion des cas d'erreur

Un déchiffreur robuste doit gérer les situations dégradées qui se présentent en contexte réel :

### Mauvaise clé

Si la clé extraite était incorrecte (hypothèse non confirmée, leurre dans `.rodata`), la bibliothèque `cryptography` lèverait une `ValueError` au moment du retrait du padding PKCS#7. En effet, après un déchiffrement avec la mauvaise clé, le dernier bloc contient des octets pseudo-aléatoires qui ne constituent pas un padding PKCS#7 valide.

Le message d'erreur ressemblerait à :

```
[!] Échec du déchiffrement : Invalid padding bytes.
```

C'est un signal immédiat que la clé ou l'IV sont incorrects. Dans ce cas, il faudrait revenir à l'étape d'analyse dynamique pour recapturer les paramètres.

### Fichier tronqué ou corrompu

Si un fichier `.locked` a été partiellement écrasé ou tronqué (crash disque, copie interrompue), les vérifications en amont détectent le problème :
- Header de moins de 16 octets → `parse_locked_header` refuse le fichier.  
- Ciphertext non multiple de 16 → `decrypt_aes256cbc` refuse le déchiffrement.  
- Taille déchiffrée incohérente → le script signale l'écart.

### Fichier qui n'est pas un `.locked`

La vérification du magic `RWARE27\0` rejette tout fichier qui n'a pas été produit par notre sample, même s'il porte l'extension `.locked` par coïncidence.

---

## Variante minimaliste avec `pwntools`

Pour les étudiants familiers avec `pwntools` ([Chapitre 11, section 11.9](/11-gdb/09-introduction-pwntools.md)), voici une version condensée utilisant `pwnlib.util` pour le formatage, combinée à `pycryptodome` :

```python
#!/usr/bin/env python3
"""Déchiffreur minimal Ch27 — version pwntools + pycryptodome."""

from Crypto.Cipher import AES  
from Crypto.Util.Padding import unpad  
from pwn import *  
import struct, os, sys  

KEY   = b"REVERSE_ENGINEERING_IS_FUN_2025!"  
IV    = bytes.fromhex("deadbeefcafebabe13374242feedface")  
MAGIC = b"RWARE27\x00"  

def decrypt(path):
    data = read(path)                              # pwntools read()
    assert data[:8] == MAGIC, f"Bad magic: {path}"
    orig_size = struct.unpack("<Q", data[8:16])[0]
    ct = data[16:]

    cipher = AES.new(KEY, AES.MODE_CBC, IV)
    pt = unpad(cipher.decrypt(ct), AES.block_size)

    out = path.replace(".locked", "")
    write(out, pt[:orig_size])                     # pwntools write()
    log.success(f"{path} → {out} ({orig_size} B)")

target = sys.argv[1] if len(sys.argv) > 1 else "/tmp/test"  
if os.path.isfile(target):  
    decrypt(target)
else:
    for root, _, files in os.walk(target):
        for f in sorted(files):
            if f.endswith(".locked"):
                decrypt(os.path.join(root, f))
```

Cette version tient en 25 lignes. Elle sacrifie la gestion d'erreur et la verbosité au profit de la concision, mais le résultat est identique. Le choix entre la version complète et la version minimaliste dépend du contexte : la version complète est un livrable professionnel, la version minimaliste est un outil de CTF.

---

## Ce que le déchiffreur révèle sur la faiblesse du sample

Le simple fait qu'un déchiffreur existe et fonctionne expose les faiblesses de conception cryptographique du sample :

**Clé statique** — La même clé est compilée dans le binaire et utilisée pour chaque fichier, sur chaque machine. Quiconque possède le binaire (ou le reverse) possède la clé. En comparaison, un ransomware robuste génère une clé AES aléatoire par exécution (voire par fichier), puis la chiffre avec une clé publique RSA-2048/4096 dont seul l'attaquant détient la clé privée.

**IV statique** — Le même IV est réutilisé pour chaque fichier. Conséquence directe : si deux fichiers commencent par les mêmes 16 premiers octets, les premiers blocs de ciphertext seront identiques. Un analyste pourrait en déduire des informations sur le contenu original sans même déchiffrer (attaque par analyse de patterns). Un ransomware robuste génère un IV aléatoire par fichier et le stocke en en-tête du fichier chiffré.

**Pas de dérivation de clé** — La clé est utilisée brute, sans passer par une fonction de dérivation (PBKDF2, scrypt, Argon2). Ce n'est pas grave ici (la clé fait déjà 256 bits d'entropie structurelle), mais dans un schéma réel basé sur un mot de passe, l'absence de dérivation serait une vulnérabilité critique.

**Clé en clair en mémoire** — La clé est stockée dans `.rodata` et référencée directement. Aucune tentative d'obfuscation en mémoire (XOR avec une valeur runtime, chargement fragmenté, effacement après usage). Les outils comme Frida la capturent trivialement.

Ces faiblesses sont des choix pédagogiques délibérés (voir section 27.1). Mais les identifier explicitement dans le rapport d'analyse (section 27.7) est important : c'est ce qui différencie un rapport descriptif (« voici ce que fait le malware ») d'un rapport analytique (« voici ce que fait le malware, voici pourquoi c'est cassable, voici comment le casser »).

---

## Résumé des livrables

À l'issue de cette section, vous disposez de :

1. **`decryptor.py`** — Script complet avec parsing de header, déchiffrement AES-256-CBC, retrait PKCS#7, vérification croisée de taille, mode dry-run, parcours récursif et CLI argparse.  
2. **Validation par hash** — Procédure de comparaison SHA-256 prouvant la restauration bit-à-bit.  
3. **Variante minimaliste** — Version 25 lignes avec `pwntools` + `pycryptodome` pour les contextes CTF.  
4. **Analyse des faiblesses** — Identification des défauts cryptographiques exploités par le déchiffreur, prête à être intégrée au rapport.

Ces éléments constituent la preuve technique que les fichiers sont récupérables sans payer de rançon — l'information la plus critique que puisse fournir un analyste lors d'un incident ransomware.

⏭️ [Rédiger un rapport d'analyse type (IOC, comportement, recommandations)](/27-ransomware/07-rapport-analyse.md)
