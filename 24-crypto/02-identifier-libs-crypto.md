🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 24.2 — Identifier les bibliothèques crypto embarquées (OpenSSL, libsodium, custom)

> 🎯 **Objectif de cette section** : déterminer si les routines crypto détectées en 24.1 proviennent d'une bibliothèque connue ou d'une implémentation maison, et comprendre pourquoi cette distinction change fondamentalement la stratégie d'analyse.

---

## Pourquoi cette question est cruciale

La section précédente nous a appris à identifier *quel algorithme* un binaire utilise. Mais savoir qu'un binaire fait de l'AES-256 ne suffit pas pour le reverser efficacement. Il faut aussi savoir *comment* cet AES est implémenté, car cela détermine toute la suite de l'analyse.

**Si le binaire utilise OpenSSL**, l'API est documentée publiquement. On connaît les signatures de fonctions, l'ordre des paramètres, les structures de données internes (`EVP_CIPHER_CTX`, `EVP_MD_CTX`…). On peut lire le code source d'OpenSSL pour comprendre exactement ce que fait chaque fonction, poser des breakpoints pertinents, et même utiliser les signatures FLIRT/Ghidra pour renommer automatiquement des centaines de fonctions dans un binaire strippé. Le RE se transforme en exercice de reconnaissance de patterns connus.

**Si le binaire utilise une implémentation custom**, rien de tout cela ne s'applique. Les structures sont inconnues, les fonctions ne correspondent à aucune signature, et il faut reconstruire la logique manuellement. Le temps d'analyse passe de quelques heures à potentiellement plusieurs jours.

La distinction entre ces deux cas est donc un multiplicateur d'efficacité. Investir 15 minutes à identifier la bibliothèque peut en économiser des dizaines par la suite.

---

## Cas 1 : Liaison dynamique — le cas simple

Quand un binaire est lié dynamiquement à une bibliothèque crypto, l'identification est quasi immédiate.

### `ldd` — lister les dépendances

```bash
$ ldd crypto_O0
    linux-vdso.so.1 (0x00007ffd...)
    libcrypto.so.3 => /lib/x86_64-linux-gnu/libcrypto.so.3 (0x00007f...)
    libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f...)
    /lib64/ld-linux-x86-64.so.2 (0x00007f...)
```

La présence de `libcrypto.so` (OpenSSL), `libsodium.so`, `libgcrypt.so`, `libmbedcrypto.so`, ou `libwolfssl.so` est un diagnostic instantané. Voici les bibliothèques les plus courantes et leurs `.so` :

| Bibliothèque | Fichier `.so` typique | Indices supplémentaires |  
|---|---|---|  
| OpenSSL | `libcrypto.so`, `libssl.so` | Fonctions `EVP_*`, `SHA*`, `AES_*`, `RSA_*` |  
| LibreSSL | `libcrypto.so` (fork OpenSSL) | API quasi identique à OpenSSL |  
| libsodium (NaCl) | `libsodium.so` | Fonctions `crypto_secretbox_*`, `crypto_box_*` |  
| libgcrypt (GnuPG) | `libgcrypt.so` | Fonctions `gcry_cipher_*`, `gcry_md_*` |  
| mbedTLS (ARM) | `libmbedcrypto.so`, `libmbedtls.so` | Fonctions `mbedtls_aes_*`, `mbedtls_sha256_*` |  
| wolfSSL | `libwolfssl.so` | Fonctions `wc_AesSetKey`, `wc_Sha256*` |  
| Botan | `libbotan-*.so` | Namespace C++ `Botan::` |  
| Nettle (GnuTLS) | `libnettle.so`, `libhogweed.so` | Fonctions `nettle_aes*`, `nettle_sha256*` |

### `nm -D` — symboles dynamiques

Même sur un binaire strippé, les **symboles dynamiques** (ceux importés depuis les `.so`) restent visibles — ils sont nécessaires au linker dynamique pour résoudre les adresses au chargement :

```bash
$ nm -D crypto_O2_strip | grep -i evp
                 U EVP_aes_256_cbc
                 U EVP_CIPHER_CTX_free
                 U EVP_CIPHER_CTX_new
                 U EVP_EncryptFinal_ex
                 U EVP_EncryptInit_ex
                 U EVP_EncryptUpdate
```

Le `U` signifie « undefined » — le symbole est importé depuis une bibliothèque externe. C'est une mine d'or : on connaît non seulement la bibliothèque mais les fonctions exactes appelées. Pour OpenSSL, ces noms de fonctions permettent d'aller directement lire la documentation de l'API `EVP` et de comprendre le flux de chiffrement sans même ouvrir le désassembleur.

### `readelf --dynamic` — alternative robuste

Si `ldd` refuse de fonctionner (binaire d'une autre architecture, ou précaution contre l'exécution), `readelf` donne la même information sans exécuter le binaire :

```bash
$ readelf --dynamic crypto_O2_strip | grep NEEDED
 0x0000000000000001 (NEEDED)  Shared library: [libcrypto.so.3]
 0x0000000000000001 (NEEDED)  Shared library: [libc.so.6]
```

### `objdump -T` — table des symboles dynamiques avec démanglement

Pour les binaires C++ qui utilisent une bibliothèque crypto C++ (Botan, Crypto++), les symboles sont manglés. `objdump -TC` (avec démanglement) est plus lisible que `nm` :

```bash
$ objdump -TC binary_cpp | grep -i cipher
0000000000000000      DF *UND*  ... Botan::Cipher_Mode::create(...)
```

---

## Cas 2 : Liaison statique — le défi principal

Quand un binaire est lié statiquement (`-static`), toutes les fonctions de la bibliothèque sont copiées dans le binaire. `ldd` ne montre rien (ou indique « not a dynamic executable »), et si le binaire est en plus strippé, les noms de fonctions disparaissent. C'est le cas le plus courant en RE réaliste : malwares, firmware, binaires IoT, applications Go et Rust embarquant leurs dépendances.

### Étape 1 : confirmer la liaison statique

```bash
$ file crypto_static
crypto_static: ELF 64-bit LSB executable, x86-64, ...  
statically linked, ...  

$ ldd crypto_static
    not a dynamic executable
```

### Étape 2 : `strings` ciblé — les chaînes internes de la bibliothèque

Les bibliothèques crypto contiennent des chaînes internes (messages d'erreur, noms d'algorithme, informations de version) qui survivent au stripping car elles sont dans `.rodata`, pas dans la table de symboles.

**OpenSSL** est particulièrement bavard :

```bash
$ strings crypto_static | grep -i openssl
OpenSSL 3.0.2 15 Mar 2022
...

$ strings crypto_static | grep -i "aes-"
aes-128-cbc  
aes-128-ecb  
aes-256-cbc  
aes-256-gcm  
...
```

OpenSSL embarque une table interne de tous les algorithmes supportés, avec leurs noms en ASCII. Même dans un binaire strippé et lié statiquement, cette table reste accessible.

Voici les empreintes `strings` typiques par bibliothèque :

**OpenSSL / LibreSSL** :
```
OpenSSL X.Y.Z ...  
EVP_CipherInit_ex  
aes-256-cbc  
SHA256  
PKCS7 padding  
```

**libsodium** :
```
libsodium  
sodium_init  
crypto_secretbox_xsalsa20poly1305  
```

**mbedTLS** :
```
MBEDTLS_ERR_AES_INVALID_KEY_LENGTH  
mbedtls_aes_crypt_cbc  
```

**wolfSSL** :
```
wolfSSL  
wolfCrypt  
wc_AesSetKey failed  
```

**Absence de chaînes identifiables** → implémentation custom probable. C'est le signal pour passer en analyse structurelle (voir plus loin).

### Étape 3 : signatures FLIRT / Ghidra — renommer automatiquement les fonctions

C'est la technique la plus puissante pour traiter un binaire statique strippé. L'idée est la suivante : les fonctions d'une bibliothèque compilée avec les mêmes options (version, compilateur, architecture) produisent toujours les mêmes séquences d'octets en début de fonction. En créant une base de signatures à partir de la bibliothèque compilée, on peut ensuite scanner un binaire inconnu et réassocier chaque fonction à son nom d'origine.

#### FLIRT (Fast Library Identification and Recognition Technology) — IDA

FLIRT est le format de signatures historique, développé par Hex-Rays pour IDA. Le processus est le suivant :

1. Compiler la bibliothèque cible dans les mêmes conditions que le binaire analysé (même version, même architecture, même compilateur).  
2. Extraire les signatures avec `pelf` (pour les `.a` statiques) ou `sigmake` pour produire un fichier `.sig`.  
3. Appliquer le fichier `.sig` dans IDA : les fonctions reconnues sont automatiquement renommées.

Des collections de signatures FLIRT pré-générées existent pour les versions courantes d'OpenSSL, glibc, libcrypto, etc. La communauté en maintient sur GitHub (par exemple le projet `sig-database`).

#### Function ID (FID) — Ghidra

Ghidra dispose de son propre système de signatures, appelé **Function ID** (FID). Il fonctionne sur le même principe mais avec un format différent :

1. Dans Ghidra, ouvrir **Analysis → One Shot → Function ID** ou activer l'analyseur FID lors de l'import.  
2. Ghidra compare les fonctions du binaire contre sa base de données FID intégrée.  
3. Les fonctions matchées sont renommées dans le Symbol Tree.

Ghidra est livré avec des bases FID pour les bibliothèques standards (glibc, libstdc++…). Pour OpenSSL ou d'autres bibliothèques spécifiques, il faut créer ses propres bases FID. Le processus :

1. Compiler `libcrypto.a` dans la version ciblée.  
2. Importer la `.a` dans un projet Ghidra dédié, laisser l'analyse tourner.  
3. Utiliser **Tools → Function ID → Create new empty FidDb**, puis **Populate FidDb from programs** pour générer la base.  
4. Appliquer cette base sur le binaire cible.

C'est un investissement initial, mais sur un cas réel (malware lié statiquement à OpenSSL 1.1.1), cette technique peut renommer des centaines de fonctions d'un coup et transformer un désassemblage opaque en quelque chose de navigable.

#### Signatures Radare2

Radare2 dispose de son propre mécanisme via la commande `zg` (génération de signatures) et `z/` (application). Le framework `r2-zignatures` permet d'importer des signatures FLIRT ou de créer des signatures Radare2 natives :

```bash
# Appliquer un fichier de signatures sur le binaire chargé
[0x00401000]> zo openssl_3.0.2_x64.z
[0x00401000]> z/
# => fonctions reconnues renommées
```

### Étape 4 : heuristiques structurelles — quand les signatures ne matchent pas

Les signatures ne fonctionnent que si la version, le compilateur et les options de compilation correspondent assez précisément. Si le match est partiel ou nul, on peut encore identifier la bibliothèque par des caractéristiques structurelles.

**Taille et nombre de fonctions crypto** : une implémentation OpenSSL complète liée statiquement ajoute des milliers de fonctions au binaire. Si `afl` (list functions) dans Radare2 ou la Function List dans Ghidra montre 3000+ fonctions dans un binaire qui devrait être simple, c'est un indice fort de bibliothèque embarquée.

**Graphe d'appels autour des constantes** : à partir des constantes identifiées en 24.1 (S-box AES, IV SHA-256), remonter les XREF dans Ghidra. Si la fonction qui accède à la S-box est appelée par une cascade de fonctions intermédiaires avec une structure régulière (fonctions de round, key schedule, wrapper EVP…), c'est le signe d'une bibliothèque bien architecturée, pas d'un copier-coller minimal.

**Structures de données internes** : OpenSSL utilise des structures comme `EVP_CIPHER_CTX` (plusieurs centaines d'octets) avec des pointeurs de fonctions (dispatch par algorithme). Si le décompilateur Ghidra montre des accès à une grosse structure avec des appels indirects via des champs de la structure, c'est un pattern classique d'API crypto à dispatch.

---

## Cas 3 : Implémentation custom — les signaux d'alerte

Quand aucune bibliothèque connue n'est identifiée, on est face à l'un de ces scénarios :

**Implémentation custom d'un algorithme standard** — Le développeur a codé (ou copié) sa propre version d'AES, SHA-256, etc. Les constantes magiques sont présentes (détectées en 24.1) mais l'organisation du code ne correspond à aucune bibliothèque connue. C'est courant dans le monde embarqué, les malwares, et les projets qui veulent éviter une dépendance externe.

**Algorithme entièrement custom** — Le développeur a inventé son propre schéma. Pas de constantes standard, pas de pattern reconnaissable. C'est le cas le plus difficile et, paradoxalement, souvent le moins robuste cryptographiquement — la crypto maison est rarement solide.

### Comment les reconnaître

Plusieurs indices convergent vers une implémentation custom :

- Aucune chaîne interne de bibliothèque connue dans `strings`.  
- Les constantes crypto standard sont présentes mais isolées (pas entourées des centaines de fonctions d'une vraie bibliothèque).  
- Le graphe d'appels autour des constantes est court : 2-3 fonctions, pas une hiérarchie profonde.  
- Les fonctions crypto sont petites et compactes (quelques dizaines de lignes dans le décompilateur) plutôt que les implémentations optimisées et volumineuses des bibliothèques.  
- Le code mélange la logique métier et la crypto dans la même fonction, au lieu du découpage propre en couches d'une API comme EVP.

### Stratégie d'analyse

Face à du custom, la démarche change :

1. **Si les constantes standard sont présentes** : on sait quel algorithme est implémenté. On peut comparer le code décompilé avec une implémentation de référence (par exemple, l'implémentation AES de référence de NIST ou le RFC de SHA-256) pour vérifier qu'il n'y a pas de variantes ou d'erreurs. On vérifie notamment le key schedule, le nombre de rounds, et le mode d'opération.

2. **Si aucune constante standard n'est trouvée** : il faut analyser le code structurellement. On cherche les boucles à compteur fixe (indice de rounds), les opérations XOR massives (substitution, mélange), les rotations de bits, les accès à des tableaux de taille caractéristique (256 = substitution, 16 = taille de bloc…). L'analyse dynamique avec Frida (section 24.3) devient alors indispensable pour observer les données en transit.

---

## Application sur nos binaires

### `crypto_O0` (dynamique, non strippé)

C'est le cas d'école du diagnostic instantané :

```bash
$ ldd crypto_O0 | grep crypto
    libcrypto.so.3 => /lib/x86_64-linux-gnu/libcrypto.so.3

$ nm -D crypto_O0 | grep -c "EVP\|SHA\|AES\|RAND"
8
```

Verdict en 10 secondes : OpenSSL, API EVP, AES-256-CBC, SHA-256, RAND_bytes.

### `crypto_O2_strip` (dynamique, strippé)

Les symboles locaux ont disparu, mais les symboles dynamiques sont intacts :

```bash
$ nm crypto_O2_strip
nm: crypto_O2_strip: no symbols

$ nm -D crypto_O2_strip | grep EVP
                 U EVP_aes_256_cbc
                 U EVP_CIPHER_CTX_free
                 ...
```

Même verdict. Le stripping ne masque pas les imports dynamiques.

### `crypto_static` (statique, non strippé)

Plus de `ldd`, mais `strings` et les constantes font le travail :

```bash
$ ldd crypto_static
    not a dynamic executable

$ strings crypto_static | grep -c -i openssl
12

$ strings crypto_static | grep "aes-256"
aes-256-cbc  
aes-256-cfb  
...
```

Les chaînes internes d'OpenSSL trahissent la bibliothèque. Si on avait strippé le binaire en plus, ces chaînes seraient toujours là.

### Cas hypothétique : `crypto_static` strippé + `strings` nettoyé

Si un adversaire prenait la peine de supprimer aussi les chaînes internes (ce qui est rare mais possible), il resterait :

1. Les constantes magiques (identifiées en 24.1) : elles prouvent AES + SHA-256.  
2. La taille du binaire : un binaire de 2+ Mo pour un programme simple suggère une grosse bibliothèque embarquée.  
3. Les signatures FLIRT/FID : si on dispose de la bonne version compilée d'OpenSSL.  
4. L'analyse structurelle du graphe d'appels : la profondeur et la régularité de l'architecture EVP.

Même dans ce cas extrême, l'identification reste possible avec un effort raisonnable.

---

## Tableau de synthèse : arbre de décision

```
Le binaire est-il lié dynamiquement à une .so crypto ?
├── OUI → ldd + nm -D → identification immédiate
│         → Lire la doc de l'API, poser des breakpoints sur les fonctions connues
│
└── NON (statique ou pas de .so crypto)
    │
    ├── strings révèle des chaînes internes de bibliothèque connue ?
    │   ├── OUI → Bibliothèque identifiée
    │   │         → Créer/appliquer des signatures FLIRT/FID pour renommer les fonctions
    │   │
    │   └── NON
    │       │
    │       ├── Constantes crypto standard trouvées (section 24.1) ?
    │       │   ├── OUI → Algorithme standard, implémentation custom ou micro-bibliothèque
    │       │   │         → Comparer le décompilé avec l'implémentation de référence
    │       │   │
    │       │   └── NON → Crypto entièrement custom
    │       │             → Analyse structurelle + dynamique obligatoire
    │       │
    │       └── Signatures FLIRT/FID matchent ?
    │           ├── OUI → Bibliothèque identifiée malgré l'absence de chaînes
    │           └── NON → Confirmer custom, analyser manuellement
```

---

## Récapitulatif

| Technique | Fonctionne sur | Effort | Ce qu'elle révèle |  
|---|---|---|---|  
| `ldd` | Binaire dynamique | Nul | Nom de la `.so` |  
| `nm -D` | Binaire dynamique (même strippé) | Nul | Fonctions importées exactes |  
| `readelf --dynamic` | Binaire dynamique, sans exécution | Nul | Dépendances sans risque |  
| `strings` ciblé | Statique ou dynamique | Faible | Bibliothèque par ses chaînes internes |  
| Signatures FLIRT | Statique strippé (IDA) | Moyen | Renommage massif de fonctions |  
| Function ID (FID) | Statique strippé (Ghidra) | Moyen | Renommage massif de fonctions |  
| Analyse structurelle | Tout binaire | Élevé | Custom vs bibliothèque, architecture du code |

L'identification de la bibliothèque crypto est un investissement de temps modeste qui oriente toute la suite de l'analyse. Une fois cette étape franchie, on sait exactement quelles fonctions cibler pour extraire les secrets — ce qui est précisément l'objet de la section suivante.

---


⏭️ [Extraire clés et IV depuis la mémoire avec GDB/Frida](/24-crypto/03-extraire-cles-iv.md)
