🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe J — Constantes magiques crypto courantes (AES, SHA, MD5, RC4…)

> 📎 **Fiche de référence** — Cette annexe regroupe les valeurs hexadécimales caractéristiques des algorithmes cryptographiques les plus répandus. Lorsque vous tombez sur une séquence d'octets suspecte dans `.rodata`, `.data` ou dans un dump mémoire, cette table vous permet de l'identifier rapidement. Elle est particulièrement utile dans le contexte des chapitres 24 (reverse de binaire avec chiffrement) et 27 (analyse de ransomware).

---

## Pourquoi chercher des constantes crypto ?

La très grande majorité des algorithmes cryptographiques reposent sur des **constantes mathématiques prédéfinies** : tables de substitution (S-boxes), vecteurs d'initialisation (IV), constantes de tour (*round constants*), valeurs initiales de hash. Ces constantes sont fixées par les spécifications de l'algorithme et sont identiques dans toutes les implémentations conformes, qu'elles proviennent d'OpenSSL, de libsodium, de mbedTLS ou d'une implémentation custom.

Cette propriété en fait des **empreintes digitales fiables** : si vous trouvez les 16 premiers octets de la S-box AES dans un binaire, vous savez avec une quasi-certitude que le binaire utilise AES. Peu importe que le code soit obfusqué, strippé ou compilé avec `-O3` — les constantes ne changent pas.

Le workflow typique est le suivant : vous cherchez les constantes de cette annexe dans le binaire (avec `strings`, ImHex, YARA ou un script Python), puis vous localisez le code qui les référence via les cross-références (xrefs). Ce code est la routine crypto, et à partir de là vous pouvez remonter aux clés, IV et données traitées.

### Méthodes de recherche

Plusieurs outils permettent de chercher ces constantes dans un binaire :

| Méthode | Commande / Outil | Contexte |  
|---------|-------------------|----------|  
| ImHex | `Edit → Find → Hex Pattern` | Recherche visuelle dans la vue hex |  
| Radare2 | `/x 637c777b` | Recherche d'octets dans le fichier |  
| YARA | Règle avec condition `{ 63 7C 77 7B ... }` | Scan automatisé de fichiers/répertoires |  
| Python | `data.find(b'\x63\x7c\x77\x7b')` | Script de triage |  
| GDB | `find 0x400000, 0x500000, 0x637c777b` | Recherche en mémoire (runtime) |  
| `grep -c` | `xxd binary \| grep "637c 777b"` | Recherche rapide en shell |  
| Ghidra | `Search → Memory → Hex` | Recherche dans l'analyse statique |

> ⚠️ **Endianness** : les constantes sont listées dans cette annexe dans l'ordre dans lequel elles apparaissent en mémoire (octet par octet). Sur x86-64 (little-endian), les valeurs multi-octets lues comme des `uint32_t` apparaissent en ordre inversé dans les registres. Par exemple, les 4 premiers octets de la S-box AES (`63 7C 77 7B`) seront lus comme le dword `0x7B777C63` par un `mov eax, [addr]`. Gardez cela à l'esprit quand vous cherchez des constantes dans le désassemblage plutôt que dans la vue hex.

---

## 1 — AES (Advanced Encryption Standard / Rijndael)

AES est l'algorithme de chiffrement symétrique le plus utilisé au monde. Ses constantes sont les plus fréquemment recherchées en RE de binaires.

### 1.1 — S-box (table de substitution)

La S-box AES est un tableau de 256 octets utilisé dans l'opération SubBytes de chaque tour de chiffrement. C'est la constante la plus reconnaissable d'AES.

**Premiers octets (signature de détection)** :

```
63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76
CA 82 C9 7D FA 59 47 F0 AD D4 A2 AF 9C A4 72 C0  
B7 FD 93 26 36 3F F7 CC 34 A5 E5 F1 71 D8 31 15  
```

Les 4 premiers octets (`63 7C 77 7B`) sont la signature minimale suffisante pour identifier AES avec une très haute confiance. Si vous cherchez un pattern plus court, `63 7C 77 7B F2 6B 6F C5` (8 octets) élimine pratiquement tout risque de faux positif.

### 1.2 — S-box inverse

La S-box inverse est utilisée pour le déchiffrement (opération InvSubBytes). Sa présence dans un binaire indique que celui-ci implémente le déchiffrement AES (et pas seulement le chiffrement).

**Premiers octets** :

```
52 09 6A D5 30 36 A5 38 BF 40 A3 9E 81 F3 D7 FB
7C E3 39 82 9B 2F FF 87 34 8E 43 44 C4 DE E9 CB
```

### 1.3 — Rcon (Round Constants)

Les constantes de tour sont utilisées dans l'expansion de clé (*key schedule*). C'est un petit tableau (10–14 entrées selon la taille de clé) :

```
01 00 00 00
02 00 00 00
04 00 00 00
08 00 00 00
10 00 00 00
20 00 00 00
40 00 00 00
80 00 00 00
1B 00 00 00
36 00 00 00
```

En format dword little-endian : `0x00000001`, `0x00000002`, `0x00000004`, ..., `0x0000001B`, `0x00000036`.

### 1.4 — Tables T précalculées

Les implémentations AES optimisées (dites *T-table implementation*) précalculent quatre tables de 256 dwords chacune (T0, T1, T2, T3) qui combinent SubBytes, ShiftRows et MixColumns en un seul lookup. Chaque table fait 1024 octets.

**Premiers dwords de T0** (little-endian) :

```
C66363A5 F87C7C84 EE777799 F67B7B8D  
FFF2F20D D66B6BBD DE6F6FB1 91C5C554  
```

La présence de quatre tables consécutives de 1 Ko chacune (total : 4 Ko) dans `.rodata` est un indice fort d'une implémentation AES par T-tables.

### 1.5 — AES-NI (instructions matérielles)

Sur les processeurs modernes, AES peut être implémenté via les instructions matérielles AES-NI. Dans ce cas, **aucune S-box ni T-table n'apparaît dans le binaire** — les constantes sont intégrées dans le microcode du processeur. Les instructions à chercher sont :

| Instruction | Description |  
|-------------|-------------|  
| `aesenc` | Un tour de chiffrement AES |  
| `aesenclast` | Dernier tour de chiffrement |  
| `aesdec` | Un tour de déchiffrement |  
| `aesdeclast` | Dernier tour de déchiffrement |  
| `aeskeygenassist` | Expansion de clé |  
| `aesimc` | Conversion de clé pour le déchiffrement |

Si vous voyez ces instructions dans le désassemblage, le binaire utilise AES matériel. Les clés de tour seront dans des registres XMM et non dans des tables mémoire, ce qui rend l'extraction de clé plus complexe (il faut les capturer en dynamique avec GDB ou Frida).

---

## 2 — SHA-256

SHA-256 est la fonction de hachage la plus utilisée dans la famille SHA-2.

### 2.1 — Valeurs initiales (H0–H7)

Ce sont les 8 valeurs 32 bits qui initialisent l'état du hash. Elles correspondent aux parties fractionnaires des racines carrées des 8 premiers nombres premiers.

```
6A09E667 BB67AE85 3C6EF372 A54FF53A
510E527F 9B05688C 1F83D9AB 5BE0CD19
```

En mémoire little-endian (octets) :

```
67 E6 09 6A  85 AE 67 BB  72 F3 6E 3C  3A F5 4F A5
7F 52 0E 51  8C 68 05 9B  AB D9 83 1F  19 CD E0 5B
```

### 2.2 — Constantes de tour (K0–K63)

SHA-256 utilise 64 constantes de 32 bits (parties fractionnaires des racines cubiques des 64 premiers nombres premiers). Ces 256 octets (64 × 4) sont une signature très fiable.

**Premiers dwords** :

```
428A2F98 71374491 B5C0FBCF E9B5DBA5
3956C25B 59F111F1 923F82A4 AB1C5ED5
D807AA98 12835B01 243185BE 550C7DC3
72BE5D74 80DEB1FE 9BDC06A7 C19BF174
```

La séquence `428A2F98 71374491 B5C0FBCF E9B5DBA5` est la signature minimale recommandée. En mémoire little-endian : `98 2F 8A 42  91 44 37 71  CF FB C0 B5  A5 DB B5 E9`.

---

## 3 — SHA-1

SHA-1 est obsolète pour la sécurité mais encore largement présent dans le code existant (Git, certificats anciens, vérification d'intégrité non sécuritaire).

### 3.1 — Valeurs initiales (H0–H4)

```
67452301 EFCDAB89 98BADCFE 10325476 C3D2E1F0
```

En mémoire little-endian :

```
01 23 45 67  89 AB CD EF  FE DC BA 98  76 54 32 10  F0 E1 D2 C3
```

> 💡 Les 4 premières valeurs (`67452301 EFCDAB89 98BADCFE 10325476`) sont **partagées avec MD5**. Seule la 5ᵉ valeur (`C3D2E1F0`) distingue SHA-1 de MD5. Si vous trouvez les 4 premières sans la 5ᵉ, c'est probablement MD5. Si les 5 sont présentes, c'est SHA-1.

### 3.2 — Constantes de tour

SHA-1 utilise 4 constantes de 32 bits, chacune utilisée pendant 20 tours :

| Tours | Constante | Hexadécimal LE |  
|-------|-----------|----------------|  
| 0–19 | `5A827999` | `99 79 82 5A` |  
| 20–39 | `6ED9EBA1` | `A1 EB D9 6E` |  
| 40–59 | `8F1BBCDC` | `DC BC 1B 8F` |  
| 60–79 | `CA62C1D6` | `D6 C1 62 CA` |

---

## 4 — SHA-512

### 4.1 — Valeurs initiales

SHA-512 utilise 8 valeurs de 64 bits :

```
6A09E667F3BCC908  BB67AE8584CAA73B
3C6EF372FE94F82B  A54FF53A5F1D36F1
510E527FADE682D1  9B05688C2B3E6C1F
1F83D9ABFB41BD6B  5BE0CD19137E2179
```

Les 32 bits de poids fort de chaque valeur sont identiques à ceux de SHA-256 (`6A09E667`, `BB67AE85`, etc.). La présence de valeurs 64 bits (au lieu de 32 bits) distingue SHA-512 de SHA-256.

### 4.2 — Constantes de tour

SHA-512 utilise 80 constantes de 64 bits. Les premières :

```
428A2F98D728AE22  7137449123EF65CD
B5C0FBCFEC4D3B2F  E9B5DBA58189DBBC
```

Même remarque : les 32 bits de poids fort sont identiques à SHA-256.

---

## 5 — MD5

### 5.1 — Valeurs initiales

```
67452301 EFCDAB89 98BADCFE 10325476
```

En mémoire little-endian :

```
01 23 45 67  89 AB CD EF  FE DC BA 98  76 54 32 10
```

Ces valeurs sont partagées avec SHA-1 (voir §3.1). Si vous trouvez exactement ces 4 valeurs sans la 5ᵉ de SHA-1, c'est MD5.

### 5.2 — Constantes T (table de sinus)

MD5 utilise 64 constantes de 32 bits dérivées de la fonction sinus : `T[i] = floor(2^32 × abs(sin(i+1)))`. Ces constantes sont très distinctives.

**Premiers dwords** :

```
D76AA478 E8C7B756 242070DB C1BDCEEE  
F57C0FAF 4787C62A A8304613 FD469501  
698098D8 8B44F7AF FFFF5BB1 895CD7BE
6B901122 FD987193 A679438E 49B40821
```

La séquence `D76AA478 E8C7B756 242070DB C1BDCEEE` est la signature minimale de MD5. En mémoire little-endian : `78 A4 6A D7  56 B7 C7 E8  DB 70 20 24  EE CE BD C1`.

---

## 6 — SHA-3 / Keccak

### 6.1 — Constantes de tour (Round Constants)

SHA-3 (Keccak) utilise 24 constantes de 64 bits pour ses 24 tours. Contrairement aux fonctions SHA-2, Keccak n'a pas de valeurs initiales — l'état est initialisé à zéro.

**Premières constantes** :

```
0000000000000001  0000000000008082
800000000000808A  8000000080008000
000000000000808B  0000000080000001
8000000080008081  8000000000008009
000000000000008A  0000000000000088
```

La séquence `0000000000000001 0000000000008082 800000000000808A` est la signature de Keccak/SHA-3.

### 6.2 — Table de rotation (ρ offsets)

```
0  1  62 28 27
36 44 6  55 20
3  10 43 25 39
41 45 15 21 8
18 2  61 56 14
```

Cette table 5×5 de petits entiers peut être stockée sous différentes formes en mémoire selon l'implémentation.

---

## 7 — HMAC et dérivation de clés

HMAC n'a pas de constantes propres — il utilise les constantes de la fonction de hachage sous-jacente (SHA-256, SHA-1, MD5, etc.). Cependant, deux valeurs de padding sont caractéristiques de toute implémentation HMAC :

| Valeur | Rôle | Octets |  
|--------|------|--------|  
| `ipad` | Inner padding | `0x36` répété (bloc de 64 octets : `36 36 36 36 ...`) |  
| `opad` | Outer padding | `0x5C` répété (bloc de 64 octets : `5C 5C 5C 5C ...`) |

La présence de blocs de `0x36` et `0x5C` répétés sur 64 octets (ou 128 octets pour SHA-512) à proximité de constantes SHA ou MD5 indique HMAC.

Pour PBKDF2 et HKDF, il n'y a pas de constantes supplémentaires au-delà de celles de HMAC et du hash sous-jacent. HKDF utilise cependant la chaîne fixe `"expand"` dans certaines implémentations (comme dans TLS 1.3 : `"tls13 "` suivi d'un label).

---

## 8 — ChaCha20

### 8.1 — Constante d'expansion

ChaCha20 initialise son état avec la constante ASCII `"expand 32-byte k"` (pour les clés de 256 bits) :

```
65 78 70 61  6E 64 20 33  32 2D 62 79  74 65 20 6B
```

En dwords little-endian :

```
61707865  3320646E  79622D32  6B206574
```

Cette chaîne ASCII est la signature la plus fiable de ChaCha20 (et de Salsa20, qui utilise la même constante). Elle apparaît en clair dans `.rodata` et est facilement repérable avec `strings`.

Pour les clés de 128 bits, la constante est `"expand 16-byte k"` :

```
65 78 70 61  6E 64 20 31  36 2D 62 79  74 65 20 6B
```

---

## 9 — Salsa20

Salsa20 utilise les mêmes constantes que ChaCha20 (`"expand 32-byte k"` et `"expand 16-byte k"`). La différence entre les deux algorithmes est dans l'ordre des opérations internes (quarter round), pas dans les constantes. Si vous trouvez cette constante, vérifiez la structure de la fonction pour distinguer ChaCha20 de Salsa20.

---

## 10 — RC4

RC4 n'utilise aucune constante prédéfinie — son état interne est une permutation de 256 octets (S-box de 0x00 à 0xFF) initialisée à partir de la clé. Cependant, l'**initialisation de la S-box** est reconnaissable :

### 10.1 — Pattern d'initialisation

```asm
; for (i = 0; i < 256; i++) S[i] = i;
xor    ecx, ecx
.L_init:
mov    byte ptr [rdi+rcx], cl     ; S[i] = i  
add    ecx, 1  
cmp    ecx, 256  
jl     .L_init  
```

### 10.2 — Pattern du KSA (Key Scheduling Algorithm)

```asm
; j = 0
; for (i = 0; i < 256; i++) {
;     j = (j + S[i] + key[i % keylen]) % 256;
;     swap(S[i], S[j]);
; }
```

Le pattern reconnaissable de RC4 est la boucle de 256 itérations avec des `swap` d'octets indexés par `i` et `j`, où `j` est accumulé modulo 256 (`and ej, 0xFF` ou `movzx`). L'absence de constantes magiques fait de RC4 l'un des algorithmes les plus difficiles à identifier par les constantes seules — il faut reconnaître le pattern de code.

---

## 11 — Blowfish / Twofish

### 11.1 — Blowfish — S-boxes initiales

Blowfish utilise 4 S-boxes de 256 dwords chacune (4 Ko au total), initialisées avec les décimales de π.

**Premiers dwords de la S-box 0** :

```
D1310BA6 98DFB5AC 2FFD72DB D01ADFB7  
B8E1AFED 6A267E96 BA7C9045 F12C7F99  
```

**Premiers dwords du P-array (sous-clés initiales)** :

```
243F6A88 85A308D3 13198A2E 03707344
A4093822 299F31D0 082EFA98 EC4E6C89
```

La valeur `243F6A88` correspond aux décimales hexadécimales de π. C'est la signature de Blowfish.

### 11.2 — Twofish

Twofish utilise des S-boxes dérivées de la clé (pas de constantes fixes pour les S-boxes), mais ses sous-constantes de génération de clé incluent des valeurs dérivées des constantes RS et MDS. Twofish est plus difficile à identifier par constantes que Blowfish.

---

## 12 — DES / 3DES

DES utilise plusieurs tables de constantes fixes. Bien que DES soit obsolète, il reste présent dans du code hérité.

### 12.1 — Tables de permutation

DES utilise des tables de permutation initiale (IP), de permutation finale (FP), et des tables d'expansion (E) et de permutation (P). Ces tables contiennent des petits entiers (1–64) et ne sont pas très distinctives individuellement.

### 12.2 — S-boxes DES

DES utilise 8 S-boxes de 64 entrées de 4 bits chacune. Les premiers octets de la S-box 1 :

```
14 04 0D 01 02 0F 0B 08 03 0A 06 0C 05 09 00 07
00 0F 07 04 0E 02 0D 01 0A 06 0C 0B 09 05 03 08
```

---

## 13 — RSA

RSA en lui-même n'a pas de constantes magiques au sens habituel. Cependant, les implémentations RSA sont identifiables par :

**L'exposant public standard** : la valeur `0x10001` (65537) est utilisée comme exposant public dans la quasi-totalité des clés RSA. En mémoire : `01 00 01 00` (en 32 bits LE) ou `01 00 01` (en 3 octets big-endian dans les structures ASN.1).

**Les marqueurs ASN.1/DER des clés** : les clés RSA encodées au format PEM/DER contiennent des séquences ASN.1 reconnaissables :

| Séquence | Signification |  
|----------|---------------|  
| `30 82` | Début de SEQUENCE (structure ASN.1) |  
| `02 01 00` | INTEGER = 0 (version de la clé privée PKCS#1) |  
| `06 09 2A 86 48 86 F7 0D 01 01 01` | OID `1.2.840.113549.1.1.1` = rsaEncryption |  
| `06 09 2A 86 48 86 F7 0D 01 01 0B` | OID `1.2.840.113549.1.1.11` = sha256WithRSAEncryption |

---

## 14 — Courbes elliptiques (ECC)

### 14.1 — Paramètres de courbes NIST

Les courbes NIST standard utilisent des constantes de générateur (point de base) et d'ordre. Les paramètres de P-256 (secp256r1) sont les plus courants :

**Coordonnée X du point de base P-256** :

```
6B17D1F2 E12C4247 F8BCE6E5 63A440F2 77037D81 2DEB33A0 F4A13945 D898C296
```

**Coordonnée Y du point de base P-256** :

```
4FE342E2 FE1A7F9B 8EE7EB4A 7C0F9E16 2BCE3357 6B315ECE CBB64068 37BF51F5
```

### 14.2 — Curve25519

Curve25519 utilise la constante de base `9` (le point de base est `x = 9`). Ce n'est pas une séquence distinctive. Les implémentations sont identifiables par la constante `121665` (le coefficient `d` de la courbe Edwards25519 : `d = -121665/121666`) et par le nombre premier `2^255 - 19` :

```
7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFED
```

En pratique, Curve25519 est souvent identifié par la présence de la bibliothèque `libsodium` ou par les noms de fonctions `crypto_box`, `crypto_scalarmult`, `X25519` dans les symboles.

---

## 15 — CRC-32

CRC-32 utilise une table de lookup de 256 dwords (1024 octets) dérivée du polynôme générateur.

### 15.1 — CRC-32 standard (polynôme `0xEDB88320`, réfléchi)

**Premiers dwords de la table** :

```
00000000 77073096 EE0E612C 990951BA
076DC419 706AF48F E963A535 9E6495A3
```

La séquence `00000000 77073096 EE0E612C 990951BA` est la signature du CRC-32 standard (utilisé par zlib, gzip, PNG, etc.).

### 15.2 — CRC-32C (Castagnoli, polynôme `0x82F63B78`)

**Premiers dwords** :

```
00000000 F26B8303 E13B70F7 1350F3F4
C79A971F 35F1141C 26A1E7E8 D4CA64EB
```

CRC-32C est utilisé par iSCSI, Btrfs, et certains protocoles réseau.

---

## 16 — Base64

L'alphabet Base64 standard est une chaîne ASCII reconnaissable :

```
ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/
```

En hexadécimal :

```
41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50
51 52 53 54 55 56 57 58 59 5A 61 62 63 64 65 66
67 68 69 6A 6B 6C 6D 6E 6F 70 71 72 73 74 75 76
77 78 79 7A 30 31 32 33 34 35 36 37 38 39 2B 2F
```

Cet alphabet est directement visible avec `strings`. La variante URL-safe utilise `-_` au lieu de `+/`.

Base64 n'est pas du chiffrement mais il est omniprésent dans les binaires qui manipulent des données encodées (tokens, cookies, configurations, données sérialisées). Sa présence indique souvent un encodage/décodage de données et peut mener à des clés ou des secrets encodés.

---

## 17 — Table récapitulative de détection rapide

Ce tableau résume les signatures minimales à chercher pour chaque algorithme. Quatre à huit octets suffisent généralement pour identifier l'algorithme avec confiance.

| Algorithme | Signature minimale (hex) | Taille | Localisation typique |  
|------------|--------------------------|--------|----------------------|  
| **AES** (S-box) | `63 7C 77 7B F2 6B 6F C5` | 8 octets | `.rodata` ou `.data` |  
| **AES** (S-box inverse) | `52 09 6A D5 30 36 A5 38` | 8 octets | `.rodata` ou `.data` |  
| **AES** (T-table T0) | `A5 63 63 C6 84 7C 7C F8` | 8 octets (LE dwords) | `.rodata` |  
| **SHA-256** (H init) | `67 E6 09 6A 85 AE 67 BB` | 8 octets (LE) | `.rodata` |  
| **SHA-256** (K) | `98 2F 8A 42 91 44 37 71` | 8 octets (LE) | `.rodata` |  
| **SHA-1** (H init) | `01 23 45 67 89 AB CD EF FE DC BA 98 76 54 32 10 F0 E1 D2 C3` | 20 octets (LE) | `.rodata` |  
| **MD5** (H init) | `01 23 45 67 89 AB CD EF FE DC BA 98 76 54 32 10` | 16 octets (LE) | `.rodata` |  
| **MD5** (T sinus) | `78 A4 6A D7 56 B7 C7 E8` | 8 octets (LE) | `.rodata` |  
| **SHA-3/Keccak** (RC) | `01 00 00 00 00 00 00 00 82 80 00 00 00 00 00 00` | 16 octets | `.rodata` |  
| **ChaCha20/Salsa20** | `65 78 70 61 6E 64 20 33` (`"expand 3"`) | 8 octets (ASCII) | `.rodata` |  
| **Blowfish** (P-array) | `88 6A 3F 24 D3 08 A3 85` | 8 octets (LE) | `.rodata` |  
| **CRC-32** (table) | `00 00 00 00 96 30 07 77` | 8 octets (LE) | `.rodata` |  
| **RSA** (exposant public) | `01 00 01` ou `00 01 00 01` | 3–4 octets | `.data`, `.rodata` |  
| **HMAC** (ipad) | `36 36 36 36 36 36 36 36` | 8 octets | `.rodata` ou dynamique |  
| **Base64** (alphabet) | `41 42 43 44 45 46 47 48` (`"ABCDEFGH"`) | 8 octets (ASCII) | `.rodata` |

---

## 18 — Règle YARA générique pour la détection crypto

Voici un squelette de règle YARA qui combine plusieurs signatures. Vous pouvez l'adapter et l'étendre pour vos besoins d'analyse :

```yara
rule Crypto_Constants {
    meta:
        description = "Détecte les constantes crypto courantes dans un binaire"
        author = "Formation RE GCC"

    strings:
        // AES
        $aes_sbox     = { 63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76 }
        $aes_inv_sbox = { 52 09 6A D5 30 36 A5 38 BF 40 A3 9E 81 F3 D7 FB }

        // SHA-256 initial values (little-endian)
        $sha256_h = { 67 E6 09 6A 85 AE 67 BB 72 F3 6E 3C 3A F5 4F A5 }

        // SHA-256 round constants (little-endian)
        $sha256_k = { 98 2F 8A 42 91 44 37 71 CF FB C0 B5 A5 DB B5 E9 }

        // MD5 sine table (little-endian)
        $md5_t = { 78 A4 6A D7 56 B7 C7 E8 DB 70 20 24 EE CE BD C1 }

        // MD5 / SHA-1 initial values (little-endian)
        $md5_sha1_h = { 01 23 45 67 89 AB CD EF FE DC BA 98 76 54 32 10 }

        // ChaCha20 / Salsa20
        $chacha = "expand 32-byte k"
        $chacha16 = "expand 16-byte k"

        // Blowfish P-array (little-endian)
        $blowfish_p = { 88 6A 3F 24 D3 08 A3 85 2E 8A 19 13 44 73 70 03 }

        // CRC-32 table
        $crc32 = { 00 00 00 00 96 30 07 77 2C 61 0E EE BA 51 09 99 }

    condition:
        any of them
}
```

Cette règle peut être utilisée directement avec YARA en ligne de commande (`yara crypto.yar ./binary`), intégrée dans ImHex (chapitre 6.10), ou déployée dans un pipeline d'analyse automatisé (chapitre 35.4).

---

## 19 — Stratégie d'identification en l'absence de constantes

Certains algorithmes n'ont pas de constantes distinctives facilement recherchables. Voici comment les aborder :

**RC4** — Pas de constantes. Cherchez le pattern de code : boucle de 256 itérations initialisant un tableau séquentiel (`S[i] = i`), suivie d'une boucle KSA avec swaps et accumulation modulo 256.

**XOR simple** — Pas de constantes. Cherchez les instructions `xor` sur des blocs de données avec un pattern répétitif. L'analyse d'entropie dans ImHex peut révéler un XOR naïf (entropie élevée mais uniforme).

**Algorithmes custom** — Les implémentations crypto maison n'ont par définition pas de constantes cataloguées. Cherchez les indicateurs indirects : boucles avec beaucoup d'opérations XOR, rotations (`rol`/`ror`), structures de données de 16/32/64 octets (tailles de blocs courantes), et noms de fonctions ou chaînes suggérant du chiffrement (`encrypt`, `decrypt`, `key`, `iv`, `cipher`, `hash`).

**Bibliothèques connues** — Si le binaire est linké dynamiquement avec OpenSSL, libsodium, mbedTLS ou une autre bibliothèque crypto, les noms de fonctions importées (`EVP_EncryptInit`, `crypto_aead_xchacha20poly1305_ietf_encrypt`, etc.) identifient directement les algorithmes sans avoir besoin de chercher les constantes. Vérifiez les imports avec `ii` (r2) ou `readelf -s --dyn-syms`.

---

> 📚 **Pour aller plus loin** :  
> - **Chapitre 24** — [Reverse d'un binaire avec chiffrement](/24-crypto/README.md) — identification et extraction de clés crypto.  
> - **Chapitre 27** — [Analyse d'un ransomware Linux ELF](/27-ransomware/README.md) — cas pratique de détection AES et extraction de clé.  
> - **Annexe E** — [Cheat sheet ImHex](/annexes/annexe-e-cheatsheet-imhex.md) — recherche de magic bytes et patterns dans la vue hexadécimale.  
> - **Annexe I** — [Patterns GCC reconnaissables](/annexes/annexe-i-patterns-gcc.md) — les idiomes de code qui entourent les constantes crypto (boucles, S-box lookups).  
> - **Chapitre 35, section 35.4** — [Écrire des règles YARA](/35-automatisation-scripting/04-regles-yara.md) — automatiser la détection avec YARA.  
> - **Findcrypt** — Plugin Ghidra/IDA qui automatise la détection de constantes crypto (utilise une base similaire à cette annexe).  
> - **hashID / hash-identifier** — Outils en ligne de commande pour identifier un hash à partir de sa longueur et de son format.

⏭️ [Glossaire du Reverse Engineering](/annexes/annexe-k-glossaire.md)
