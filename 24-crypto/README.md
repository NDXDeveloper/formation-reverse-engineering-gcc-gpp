🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 24 — Reverse d'un binaire avec chiffrement

> 📦 **Binaire d'entraînement** : `binaries/ch24-crypto/`  
> Compilable via `make` (produit plusieurs variantes : `crypto_O0`, `crypto_O2`, `crypto_O2_strip`, `crypto_static`).  
> Le fichier `secret.txt.enc` est généré automatiquement par `make`.  
> 📝 **Pattern ImHex** : `hexpat/ch24_crypt24.hexpat`

---

## Pourquoi un chapitre dédié au chiffrement ?

La cryptographie est omniprésente dans les binaires modernes : protection de données utilisateur, vérification de licences, communications réseau chiffrées, ransomwares, DRM. Quand on tombe sur un binaire qui manipule des données chiffrées, le RE prend une dimension supplémentaire : il ne suffit plus de comprendre *ce que fait* le programme, il faut aussi comprendre *comment il protège* (ou cache) ses données, et surtout retrouver les éléments secrets — clés, vecteurs d'initialisation, paramètres — qui permettent de reproduire ou d'inverser le processus.

Ce chapitre ne vise pas à enseigner la cryptographie en tant que telle. L'objectif est de développer les réflexes et les techniques qui permettent, face à un binaire inconnu, de répondre à trois questions fondamentales :

1. **Quel algorithme est utilisé ?** — Le binaire embarque-t-il une implémentation connue (AES, ChaCha20, RSA…) ou un schéma custom ?  
2. **Où sont les secrets ?** — La clé est-elle hardcodée, dérivée d'un input utilisateur, lue depuis un fichier, reçue par le réseau ?  
3. **Comment reproduire le schéma ?** — Une fois l'algorithme et les paramètres identifiés, peut-on écrire un outil indépendant (en Python, par exemple) qui chiffre ou déchiffre les mêmes données ?

## Ce que ce chapitre couvre

Le binaire `ch24-crypto` est une application C compilée avec GCC qui chiffre un fichier en utilisant une combinaison d'algorithmes standards. La clé de chiffrement est dérivée en interne selon une logique qu'il faudra reconstituer. Le fichier `secret.enc` produit par le binaire servira de cible : à la fin du chapitre, vous serez capable de le déchiffrer sans disposer du code source.

Le parcours suit une progression naturelle, de l'identification à la reproduction :

- **Section 24.1** — Identifier les routines cryptographiques en repérant les constantes magiques caractéristiques (S-box AES, vecteurs d'initialisation SHA-256, tables de permutation…) dans le binaire, que ce soit via `strings`, Ghidra, ou des règles YARA.  
- **Section 24.2** — Déterminer si le binaire utilise une bibliothèque crypto connue (OpenSSL, libsodium, mbedTLS…) ou une implémentation maison, et adapter la stratégie d'analyse en conséquence.  
- **Section 24.3** — Extraire les clés et IV directement depuis la mémoire du processus en cours d'exécution, à l'aide de GDB et de Frida — parce que même un schéma complexe de dérivation de clé finit toujours par produire une clé en clair quelque part en RAM.  
- **Section 24.4** — Visualiser la structure du fichier chiffré avec ImHex : repérer les headers, les blocs, les métadonnées en clair, le padding, et comprendre le format de sortie du binaire.  
- **Section 24.5** — Assembler toutes les pièces pour reproduire le schéma de chiffrement complet en Python et déchiffrer `secret.enc`.

## Prérequis

Ce chapitre mobilise la quasi-totalité des outils vus précédemment. Avant de commencer, assurez-vous d'être à l'aise avec :

- **Analyse statique** (Partie II) — Vous utiliserez Ghidra pour naviguer dans le code décompilé, repérer les constantes et reconstruire les structures. ImHex servira à inspecter le format du fichier chiffré. Les commandes `strings` et `readelf` seront utiles en phase de triage.  
- **Analyse dynamique** (Partie III) — GDB sera indispensable pour poser des breakpoints aux moments clés (dérivation de clé, appels de chiffrement) et inspecter les buffers en mémoire. Frida permettra de hooker les fonctions crypto pour capturer les paramètres à la volée.  
- **Notions de cryptographie** — Pas besoin d'être cryptographe, mais vous devez connaître la différence entre chiffrement symétrique et asymétrique, savoir ce qu'est un mode de chiffrement par blocs (ECB, CBC, CTR…), et comprendre le rôle d'un IV et d'une fonction de dérivation de clé (KDF). Si ces termes sont flous, un rappel rapide est proposé ci-dessous.

## Rappel express : vocabulaire crypto minimal

Pour aborder ce chapitre sans ambiguïté, voici les concepts qui reviendront constamment.

**Chiffrement symétrique** — Un seul secret (la *clé*) sert à chiffrer et déchiffrer. Les algorithmes les plus courants dans les binaires sont AES (128/256 bits), ChaCha20, et — dans du code legacy ou custom — RC4, DES, Blowfish. C'est ce type de chiffrement que notre binaire `ch24-crypto` utilise.

**Mode d'opération** — Un algorithme par blocs comme AES ne chiffre que 16 octets à la fois. Le *mode* définit comment enchaîner les blocs pour chiffrer un message arbitrairement long. ECB (chaque bloc indépendant, pas de diffusion — à éviter) ; CBC (chaque bloc XORé avec le précédent, nécessite un IV) ; CTR (transforme le chiffrement par blocs en chiffrement par flux). Reconnaître le mode est crucial car il détermine les paramètres nécessaires au déchiffrement.

**IV (Initialization Vector)** — Valeur aléatoire ou unique utilisée pour que deux chiffrements du même message avec la même clé produisent des résultats différents. L'IV n'est pas secret : il est souvent stocké en clair au début du fichier chiffré.

**KDF (Key Derivation Function)** — Fonction qui transforme un mot de passe (ou un secret brut) en une clé de taille fixe adaptée à l'algorithme. PBKDF2, scrypt, Argon2, ou un simple hash (SHA-256 du mot de passe) sont des approches courantes. Dans les binaires, la KDF est souvent le maillon le plus intéressant à reverser, car c'est elle qui fait le lien entre un input observable (mot de passe utilisateur, valeur hardcodée…) et la clé réellement utilisée.

**Constantes magiques** — Chaque algorithme crypto repose sur des constantes mathématiques spécifiques : la S-box d'AES (256 octets commençant par `0x63, 0x7c, 0x77, 0x7b…`), les vecteurs d'initialisation de SHA-256 (`0x6a09e667, 0xbb67ae85…`), les constantes de round de SHA-256, la table de substitution de DES… Ces constantes sont des signatures infaillibles pour identifier un algorithme dans un binaire, même strippé et optimisé.

## Stratégie générale face à un binaire « crypto »

Avant de plonger dans les sections, voici la démarche mentale à adopter. Elle s'applique bien au-delà de notre binaire d'entraînement.

**Étape 1 — Triage classique.** Comme pour tout binaire, on commence par `file`, `strings`, `checksec`, `readelf`. On cherche des indices directs : noms de fonctions crypto dans la table des symboles (`AES_encrypt`, `EVP_CipherInit`, `SHA256_Update`…), chaînes révélatrices (`"AES-256-CBC"`, `"PBKDF2"`…), ou bibliothèques dynamiques liées (`libcrypto.so`, `libsodium.so`).

**Étape 2 — Identification de l'algorithme.** Si le triage ne suffit pas (binaire strippé, crypto embarquée statiquement, implémentation custom), on passe à la recherche de constantes magiques. C'est souvent le levier le plus fiable : un développeur peut renommer ses fonctions, stripper ses symboles, obfusquer son flux de contrôle — mais il ne peut pas modifier les constantes mathématiques d'AES sans casser l'algorithme.

**Étape 3 — Localisation des secrets.** On identifie où la clé est générée ou stockée. Analyse statique (XREF vers les constantes crypto, remontée vers les fonctions appelantes) complétée par analyse dynamique (breakpoint sur la routine de chiffrement, inspection des arguments).

**Étape 4 — Compréhension du format de sortie.** On examine le fichier chiffré avec ImHex pour comprendre sa structure : y a-t-il un header ? L'IV est-il stocké en préfixe ? Y a-t-il un MAC (code d'authentification) ? Quel padding est utilisé ?

**Étape 5 — Reproduction et validation.** On écrit un script Python qui implémente le même schéma et on vérifie qu'on peut déchiffrer `secret.enc`.

## Plan du chapitre

| Section | Sujet | Outils principaux |  
|---|---|---|  
| 24.1 | Identifier les routines crypto par les constantes magiques | `strings`, Ghidra, YARA, `binwalk` |  
| 24.2 | Identifier les bibliothèques crypto embarquées | `ldd`, `nm`, Ghidra FLIRT/signatures |  
| 24.3 | Extraire clés et IV depuis la mémoire | GDB, Frida |  
| 24.4 | Visualiser le format du fichier chiffré | ImHex, `.hexpat` |  
| 24.5 | Reproduire le schéma de chiffrement en Python | Python, `pycryptodome` |

> 🎯 **Checkpoint de fin de chapitre** : déchiffrer le fichier `secret.enc` fourni en extrayant la clé du binaire, sans accès au code source.

---

*Prêt ? On commence par la chasse aux constantes magiques.*

⏭️ [Identifier les routines crypto (constantes magiques : AES S-box, SHA256 IV…)](/24-crypto/01-identifier-routines-crypto.md)
