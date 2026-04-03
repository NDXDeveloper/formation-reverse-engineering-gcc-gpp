🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 24.1 — Identifier les routines crypto (constantes magiques : AES S-box, SHA256 IV…)

> 🎯 **Objectif de cette section** : face à un binaire inconnu, être capable de déterminer rapidement quels algorithmes cryptographiques il utilise — même s'il est strippé, optimisé, et ne lie aucune bibliothèque dynamique identifiable.

---

## Le problème : comment un binaire « cache » sa crypto

Quand un développeur utilise de la cryptographie dans son programme, il peut le faire de plusieurs manières, de la plus visible à la plus discrète :

**Cas favorable** — Le binaire est lié dynamiquement à une bibliothèque crypto connue (`libcrypto.so` d'OpenSSL, `libsodium.so`, `libgcrypt.so`…). Un simple `ldd` ou `nm` révèle les fonctions utilisées. On verra ce cas en détail dans la section 24.2.

**Cas intermédiaire** — Le binaire est lié statiquement. Les fonctions crypto sont embarquées dans le binaire lui-même. Les symboles peuvent être présents (si non strippé) ou absents. Plus de `ldd` pour nous aider.

**Cas défavorable** — Le développeur a intégré sa propre implémentation d'un algorithme standard (copié-collé depuis GitHub, implémentation maison…), ou utilise un algorithme crypto entièrement custom. Le binaire est strippé. Aucun nom de fonction, aucun nom de bibliothèque ne trahit quoi que ce soit.

Dans les deux derniers cas, il existe un levier extrêmement fiable : **les constantes mathématiques**. Chaque algorithme cryptographique repose sur des valeurs numériques spécifiques, définies par la norme ou le papier de recherche original. Ces constantes sont des empreintes digitales uniques : un développeur peut renommer ses fonctions, obfusquer son flux de contrôle, passer en `-O3 -s` — mais il ne peut pas modifier les constantes d'AES sans casser l'algorithme.

---

## Les constantes magiques crypto : catalogue des plus courantes

### AES (Rijndael)

AES utilise une **S-box** (Substitution box) de 256 octets qui effectue une substitution non linéaire lors de chaque round. Cette table est absolument caractéristique :

```
63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76
CA 82 C9 7D FA 59 47 F0 AD D4 A2 AF 9C A4 72 C0  
B7 FD 93 26 36 3F F7 CC 34 A5 E5 F1 71 D8 31 15  
...
```

Les 4 premiers octets `63 7C 77 7B` sont la signature la plus cherchée en RE crypto. On trouve également la **S-box inverse** (utilisée pour le déchiffrement), qui commence par `52 09 6A D5`, ainsi que les **tables de Rijndael pré-calculées** (`Te0`, `Te1`, `Te2`, `Te3` pour les implémentations optimisées T-table), qui sont des tableaux de 256 mots de 32 bits chacun. Ces T-tables sont encore plus volumineuses (4 × 1024 octets) et donc encore plus faciles à repérer dans le binaire.

**Où les trouver** : section `.rodata` (données constantes en lecture seule), parfois `.data` si l'implémentation les déclare en mutable, ou directement inlinées dans `.text` pour les implémentations bitsliced.

### SHA-256

SHA-256 utilise deux jeux de constantes :

**Vecteurs d'initialisation (H0–H7)** — 8 mots de 32 bits dérivés des racines carrées des 8 premiers nombres premiers :

```
6A09E667  BB67AE85  3C6EF372  A54FF53A
510E527F  9B05688C  1F83D9AB  5BE0CD19
```

**Constantes de round (K0–K63)** — 64 mots de 32 bits dérivés des racines cubiques des 64 premiers nombres premiers :

```
428A2F98  71374491  B5C0FBCF  E9B5DBA5
3956C25B  59F111F1  923F82A4  AB1C5ED5
...
```

Le premier mot `0x428A2F98` est la signature classique de SHA-256 en RE. La présence de ces 64 constantes de 32 bits dans `.rodata` est un signal quasi certain.

**Variantes** : SHA-224 partage les mêmes constantes de round mais des IV différents (`C1059ED8…`). SHA-512 et SHA-384 utilisent des constantes de 64 bits (`0x6A09E667F3BCC908…` pour SHA-512).

### MD5

MD5 utilise 64 constantes de 32 bits dérivées de la fonction sinus, dont les premières sont :

```
D76AA478  E8C7B756  242070DB  C1BDCEEE  
F57C0FAF  4787C62A  A8304613  FD469501  
...
```

Le mot `0xD76AA478` est la signature typique. En pratique, MD5 est de moins en moins utilisé pour du chiffrement mais reste courant pour du hashing (vérification d'intégrité, empreintes).

### SHA-1

SHA-1 utilise 4 constantes de round :

```
5A827999  6ED9EBA1  8F1BBCDC  CA62C1D6
```

Et des IV (`67452301 EFCDAB89 98BADCFE 10325476 C3D2E1F0`) qui sont partiellement partagés avec MD5 — attention aux faux positifs si vous cherchez uniquement les IV.

### ChaCha20 / Salsa20

ChaCha20 utilise une constante de 16 octets en ASCII : `"expand 32-byte k"` (pour les clés de 256 bits) ou `"expand 16-byte k"` (128 bits). Cette chaîne est directement repérable avec `strings`. C'est l'un des rares algorithmes crypto où une simple recherche textuelle suffit.

### RC4

RC4 n'utilise pas de constantes statiques caractéristiques (sa S-box est initialisée dynamiquement à partir de la clé). En revanche, l'algorithme KSA (Key Scheduling Algorithm) a un pattern reconnaissable dans le désassemblage : une boucle d'initialisation de 256 itérations qui remplit un tableau de 0 à 255, suivie d'une boucle de permutation. C'est un cas où le pattern structurel remplace la constante magique.

### DES / 3DES

DES utilise plusieurs tables de permutation et de substitution. Les plus identifiables sont les **8 S-boxes** de 64 octets chacune, dont la première commence par :

```
0E 04 0D 01 02 0F 0B 08 03 0A 06 0C 05 09 00 07
```

On trouve aussi les tables de permutation initiale (IP) et finale (FP). DES est considéré obsolète mais reste présent dans du code legacy.

### Blowfish

Blowfish est initialisé avec les décimales de pi encodées en hexadécimal. Les premières valeurs de la P-array sont :

```
243F6A88  85A308D3  13198A2E  03707344
A4093822  299F31D0  082EFA98  EC4E6C89
```

Le mot `0x243F6A88` est une signature fiable (ce sont les bits fractionnaires de pi).

### RSA

RSA n'a pas de constantes magiques fixes comme les algorithmes symétriques. En revanche, on repère RSA par la présence de fonctions de manipulation d'entiers de grande taille (big number / bignum) et par les structures ASN.1/DER des clés. L'OID `1.2.840.113549.1.1.1` (encodé `06 09 2A 86 48 86 F7 0D 01 01 01`) est la signature d'une clé RSA au format PKCS.

---

## Méthode 1 : `strings` — le filet à mailles larges

La première chose à faire face à un binaire suspect de faire de la crypto est de lancer `strings` avec un filtre intelligent. On ne cherche pas les constantes brutes ici (elles sont binaires, pas ASCII), mais les indices textuels :

```bash
$ strings crypto_O2_strip | grep -iE 'aes|sha|md5|crypt|cipher|key|iv|salt|hmac|pbkdf|hash|encrypt|decrypt|openssl|libcrypto|gcrypt|sodium'
```

Sur notre binaire `crypto_O0` (non strippé, lié dynamiquement), cette commande révèle immédiatement les noms de fonctions OpenSSL puisque les symboles dynamiques sont visibles. Mais sur `crypto_O2_strip`, c'est plus discret : on peut encore trouver des chaînes comme des messages d'erreur internes d'OpenSSL, des noms d'algorithme embarqués dans la bibliothèque, ou dans notre cas le magic `CRYPT24` du format de fichier.

Pour les algorithmes embarqués statiquement ou les implémentations custom, `strings` ne suffit généralement pas. Il faut passer aux octets bruts.

## Méthode 2 : `grep` binaire sur les constantes connues

On peut chercher directement les premiers octets d'une constante connue dans le binaire. La commande `grep` avec l'option `-c` (count) sur un dump hexadécimal est une approche rapide :

```bash
# Chercher la S-box AES (premiers octets)
$ xxd crypto_O2_strip | grep "637c 777b"

# Chercher le premier IV SHA-256 (little-endian sur x86 : 67 E6 09 6A)
$ xxd crypto_O2_strip | grep "67e6 096a"
```

> ⚠️ **Attention à l'endianness.** Les constantes sont documentées en big-endian dans les spécifications, mais sur x86-64 elles sont stockées en little-endian en mémoire. Le premier IV de SHA-256 est `0x6A09E667` dans la spec, mais apparaît comme `67 E6 09 6A` dans le binaire. C'est un piège classique qui fait perdre du temps.

Pour une recherche plus systématique, on peut utiliser un petit script Python :

```python
#!/usr/bin/env python3
"""Quick scan for well-known crypto constants in a binary."""

import sys

SIGNATURES = {
    "AES S-box": bytes([0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5]),
    "AES Inv S-box": bytes([0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38]),
    "SHA-256 IV (LE)": bytes.fromhex("67e6096a85ae67bb72f36e3c3af54fa5"),
    "SHA-256 K[0] (LE)": bytes.fromhex("982f8a4228e9f04b"),
    "MD5 T[0] (LE)": bytes.fromhex("78a46ad7"),
    "Blowfish P (LE)": bytes.fromhex("886a3f24"),
    "ChaCha20 sigma": b"expand 32-byte k",
    "DES S-box 1": bytes([0x0E, 0x04, 0x0D, 0x01, 0x02, 0x0F, 0x0B, 0x08]),
}

def scan(filepath):
    with open(filepath, "rb") as f:
        data = f.read()
    for name, sig in SIGNATURES.items():
        offset = data.find(sig)
        if offset != -1:
            print(f"  [+] {name:25s} found at offset 0x{offset:08X}")
        else:
            print(f"  [ ] {name:25s} not found")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <binary>")
        sys.exit(1)
    print(f"Scanning {sys.argv[1]}...")
    scan(sys.argv[1])
```

En l'exécutant sur notre binaire lié dynamiquement (`crypto_O0`), les constantes AES et SHA-256 ne seront **pas** trouvées dans le binaire lui-même — elles résident dans `libcrypto.so`. En revanche, sur `crypto_static` (lié statiquement), elles apparaîtront clairement.

## Méthode 3 : `binwalk` — analyse d'entropie et signatures

`binwalk` est connu pour l'analyse de firmware, mais son moteur de signatures et son analyse d'entropie sont très utiles pour la détection crypto :

```bash
# Recherche de signatures crypto connues
$ binwalk -R crypto_static

# Analyse d'entropie : les données chiffrées ont une entropie proche de 1.0
$ binwalk -E secret.enc
```

L'analyse d'entropie sur `secret.enc` montrera un profil caractéristique : un header à basse entropie (magic, version, IV structuré) suivi d'un bloc à très haute entropie (les données chiffrées). Ce profil « marche d'escalier » est un indicateur classique d'un fichier contenant des données chiffrées ou compressées.

Sur le binaire lui-même, une zone de haute entropie dans la section `.rodata` ou `.data` peut trahir des tables de substitution ou des données embarquées chiffrées.

## Méthode 4 : Ghidra — recherche de constantes dans le désassemblage

Ghidra est l'outil le plus puissant pour cette tâche car il permet non seulement de trouver les constantes mais aussi de remonter aux fonctions qui les utilisent via les cross-references.

### Recherche par octets (Scalar Search)

Dans le CodeBrowser, la commande **Search → For Scalars** permet de chercher une valeur numérique dans tout le binaire. Pour trouver les constantes SHA-256 :

1. Ouvrir **Search → For Scalars**  
2. Entrer `0x6A09E667` (premier IV SHA-256)  
3. Ghidra liste toutes les occurrences — dans les instructions (`mov`, chargement de constante) et dans les données

Chaque résultat est cliquable. Un double-clic vous positionne dans le listing, et à partir de là, les XREF (cross-references) montrent quelles fonctions accèdent à cette constante.

### Recherche par octets bruts (Memory Search)

Pour chercher une séquence d'octets (comme la S-box AES) :

1. **Search → Memory** (ou `S`)  
2. Sélectionner **Hex** comme format  
3. Entrer les premiers octets : `63 7C 77 7B F2 6B 6F C5`  
4. Ghidra indique l'adresse exacte dans la section de données

Une fois la S-box localisée, on fait clic droit → **References → Find References to** pour voir quelles fonctions y accèdent. Ces fonctions sont, par définition, des fonctions AES (ou au minimum des fonctions qui manipulent la S-box d'AES).

### Analyse structurelle

Au-delà des constantes isolées, Ghidra permet de reconnaître la *structure* d'un algorithme crypto dans le décompilateur. Par exemple, une implémentation AES typique compilée avec GCC présente un pattern reconnaissable : une boucle de 10 rounds (AES-128), 12 rounds (AES-192), ou 14 rounds (AES-256), chaque round comportant des accès indexés aux tables de substitution, des XOR avec les sous-clés de round, et des opérations de mélange (ShiftRows, MixColumns). Même sans les noms de fonctions, cette structure est caractéristique.

Le décompilateur affichera typiquement quelque chose qui ressemble à des accès indexés dans un grand tableau (`sbox[state[i]]`), des rotations (`>> 8`, `<< 24`), et des XOR en cascade — le tout dans une boucle comptée. Ce pattern est un signal fort même quand les constantes sont dispersées ou accédées indirectement.

## Méthode 5 : règles YARA — détection automatisée

YARA est l'outil de référence pour la détection de patterns dans les fichiers binaires. Écrire (ou utiliser) des règles YARA qui ciblent les constantes crypto permet d'automatiser l'identification sur de larges collections de binaires.

Voici un exemple de règle pour détecter AES :

```yara
rule AES_SBox
{
    meta:
        description = "Detects AES S-box (Rijndael forward substitution table)"
        author      = "Formation RE"

    strings:
        $sbox = {
            63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76
            CA 82 C9 7D FA 59 47 F0 AD D4 A2 AF 9C A4 72 C0
        }

    condition:
        $sbox
}

rule SHA256_Constants
{
    meta:
        description = "Detects SHA-256 round constants (first 8 values)"

    strings:
        // Big-endian (documentation order)
        $k_be = { 42 8A 2F 98 71 37 44 91 B5 C0 FB CF E9 B5 DB A5 }
        // Little-endian (x86/x64 in-memory order)
        $k_le = { 98 2F 8A 42 91 44 37 71 CF FB C0 B5 A5 DB B5 E9 }

    condition:
        $k_be or $k_le
}
```

On exécute avec :

```bash
$ yara crypto_rules.yar crypto_static
AES_SBox crypto_static  
SHA256_Constants crypto_static  
```

Le dépôt `yara-rules/crypto_constants.yar` fourni avec la formation contient un jeu complet de règles couvrant les algorithmes les plus courants. La communauté maintient également des collections de règles YARA crypto, notamment dans le projet **Yara-Rules** sur GitHub.

> 💡 **ImHex intègre un moteur YARA** (cf. section 6.10). Vous pouvez appliquer ces mêmes règles directement depuis ImHex lors de l'inspection hexadécimale du binaire, ce qui permet de visualiser immédiatement le contexte autour de chaque match.

## Application sur notre binaire `crypto_O0`

Mettons en pratique sur le binaire du chapitre. Commençons par le triage classique :

```bash
$ file crypto_O0
crypto_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, with debug_info, not stripped  
```

Le binaire est lié dynamiquement et non strippé : c'est le cas favorable. Un rapide `nm` confirme la présence de symboles OpenSSL :

```bash
$ nm -D crypto_O0 | grep -i -E 'sha|aes|evp|rand'
                 U EVP_aes_256_cbc
                 U EVP_CIPHER_block_size
                 U EVP_CIPHER_CTX_free
                 U EVP_CIPHER_CTX_new
                 U EVP_EncryptFinal_ex
                 U EVP_EncryptInit_ex
                 U EVP_EncryptUpdate
                 U RAND_bytes
                 U SHA256
```

On sait déjà beaucoup de choses : AES-256-CBC, SHA-256, génération d'aléa avec `RAND_bytes`. Les constantes AES et SHA-256 ne sont pas dans le binaire lui-même mais dans `libcrypto.so` (puisqu'il est lié dynamiquement).

Basculons sur `crypto_static` pour un exercice plus réaliste :

```bash
$ file crypto_static
crypto_static: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux),  
statically linked, BuildID[sha1]=..., for GNU/Linux 3.2.0, not stripped  
```

Statiquement lié. Même si les symboles sont présents ici (non strippé), imaginons qu'on les ignore. Lançons notre script de scan :

```
Scanning crypto_static...
  [+] AES S-box                found at offset 0x001A3F40
  [+] AES Inv S-box            found at offset 0x001A4040
  [+] SHA-256 IV (LE)          found at offset 0x0019B260
  [+] SHA-256 K[0] (LE)        found at offset 0x0019B280
  [ ] MD5 T[0] (LE)            not found
  [ ] Blowfish P (LE)          not found
  [ ] ChaCha20 sigma           not found
  [ ] DES S-box 1              not found
```

Résultat net : AES et SHA-256 confirmés, aucun autre algorithme détecté. On peut maintenant ouvrir le binaire dans Ghidra, naviguer jusqu'à l'offset `0x001A3F40`, et remonter les XREF pour trouver les fonctions qui utilisent la S-box — ce seront les fonctions de chiffrement AES.

## Ce qu'on cherche vs. ce qu'on ne trouve pas

Il est important de garder à l'esprit les limites de la détection par constantes :

**Ce que cette méthode détecte bien** : tous les algorithmes standards qui reposent sur des tables ou des constantes pré-calculées — AES, SHA-*, MD5, DES, Blowfish, Whirlpool, et beaucoup d'autres. C'est la grande majorité des cas.

**Ce que cette méthode détecte mal** : les algorithmes qui n'utilisent pas de constantes statiques caractéristiques, comme RC4 (table dynamique), certains chiffrements par flux (XOR simple, Vernam), ou les constructions entièrement custom basées sur des opérations arithmétiques sans tables. Pour ces cas, il faut analyser la *structure* du code (patterns de boucles, opérations arithmétiques récurrentes, taille des blocs manipulés) plutôt que chercher des constantes.

**Piège courant** : les implémentations qui calculent les tables à la volée au lieu de les stocker en dur. Certaines implémentations AES génèrent la S-box au runtime à partir du polynôme générateur dans GF(2⁸). Dans ce cas, la S-box n'existe pas dans le binaire statique — elle n'apparaît qu'en mémoire une fois le programme lancé. C'est un cas où l'analyse dynamique (section 24.3) prend le relais.

---

## Récapitulatif

À ce stade, vous disposez d'un arsenal de techniques pour répondre à la première question fondamentale : *quel algorithme est utilisé ?*

| Méthode | Effort | Fiabilité | Cas d'usage |  
|---|---|---|---|  
| `strings` + `grep` | Minimal | Faible (indicatif) | Premier filtre rapide, chaînes ASCII |  
| `xxd` + `grep` hex | Faible | Moyenne | Recherche ciblée d'une constante connue |  
| Script Python de scan | Faible | Bonne | Scan systématique multi-algorithmes |  
| `binwalk` entropie | Faible | Moyenne | Détecter la *présence* de crypto, pas l'algorithme |  
| Ghidra Scalar Search | Moyen | Excellente | Identification + remontée XREF |  
| Règles YARA | Faible (si règles prêtes) | Excellente | Détection batch, automatisation |

La section suivante s'attaque à un problème complémentaire : quand les constantes sont identifiées, comment déterminer si elles proviennent d'une bibliothèque connue (OpenSSL, libsodium…) ou d'une implémentation maison — et pourquoi cette distinction change radicalement la stratégie de RE.

---


⏭️ [Identifier les bibliothèques crypto embarquées (OpenSSL, libsodium, custom)](/24-crypto/02-identifier-libs-crypto.md)
