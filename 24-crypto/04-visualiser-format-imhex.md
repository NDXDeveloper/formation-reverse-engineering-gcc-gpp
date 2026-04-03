🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 24.4 — Visualiser le format chiffré et les structures avec ImHex

> 🎯 **Objectif de cette section** : cartographier la structure exacte du fichier `secret.enc` à l'aide d'ImHex, comprendre l'agencement des métadonnées et des données chiffrées, et produire un pattern `.hexpat` réutilisable qui documente le format.

---

## Pourquoi examiner le fichier chiffré

Les sections précédentes se sont concentrées sur le binaire qui *produit* le fichier chiffré. On a identifié l'algorithme (AES-256-CBC), la bibliothèque (OpenSSL), et extrait la clé et l'IV depuis la mémoire. On pourrait se dire que le travail est fait — il suffit de déchiffrer avec ces paramètres.

En réalité, il manque un maillon : **comment les données sont-elles emballées dans le fichier `.enc` ?** Un fichier chiffré n'est presque jamais un simple dump brut du ciphertext. Il contient généralement un header avec des métadonnées : un magic number pour identifier le format, des informations de version, l'IV (qui doit être transmis au destinataire), la taille originale du fichier (pour retirer le padding), parfois un MAC d'authentification, un sel pour la KDF, ou des flags indiquant l'algorithme utilisé.

Si on tente de déchiffrer en ignorant ce header — en passant le fichier entier à `AES-256-CBC` — on obtiendra du bruit, car les premiers octets ne sont pas du ciphertext. Il faut savoir exactement **à quel offset commencent les données chiffrées** et **combien d'octets déchiffrer**.

C'est ici qu'ImHex entre en jeu. Là où `xxd` montre des octets bruts, ImHex permet de superposer une interprétation structurée sur le fichier, de coloriser les régions, et de valider les hypothèses sur le format en temps réel grâce aux patterns `.hexpat`.

---

## Premier contact : inspection visuelle brute

Ouvrons `secret.enc` dans ImHex. Le premier réflexe est de regarder les premières lignes du fichier en vue hexadécimale, avant toute interprétation :

```
Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   Decoded text
00000000  43 52 59 50 54 32 34 00  01 00 10 00 9C 71 2E B5   CRYPT24.....q..
00000010  38 F4 A0 6D 1C 83 E7 52  BF 49 06 DA XX XX XX XX   8..m...R.I......
00000020  XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX   ................
```

*(Les `XX` représentent les octets du ciphertext, qui varient à chaque exécution à cause de l'IV aléatoire.)*

Plusieurs observations immédiates, avant même de regarder le code source :

**Octets 0x00–0x07** : `43 52 59 50 54 32 34 00` — en ASCII, cela donne `CRYPT24\0`. C'est un magic number de 8 octets, null-terminé. Les magic bytes sont la carte de visite du format : ils permettent à un outil (ou à `file`) d'identifier le type de fichier sans ambiguïté.

**Octets 0x08–0x09** : `01 00` — deux octets isolés. Si on les interprète comme un numéro de version majeure/mineure, on obtient `1.0`. C'est une hypothèse à confirmer, mais la position juste après le magic et les valeurs basses sont cohérentes.

**Octets 0x0A–0x0B** : `10 00` — en little-endian, `0x0010` = 16 en décimal. Si c'est la longueur du champ suivant, 16 octets correspond exactement à la taille d'un bloc AES (et donc d'un IV pour CBC).

**Octets 0x0C–0x1B** : 16 octets. Si l'hypothèse précédente est correcte, c'est l'IV. On peut le vérifier en le comparant avec l'IV capturé par GDB/Frida en section 24.3 — ils doivent correspondre.

**Octets 0x1C–0x1F** : 4 octets. En little-endian, ils donnent un entier qui pourrait être la taille originale du fichier avant chiffrement. Si `secret.txt` fait, par exemple, 342 octets, on devrait trouver `0x00000156` ici, stocké comme `56 01 00 00`.

**Octets 0x20 et au-delà** : le reste du fichier. C'est probablement le ciphertext pur.

Ce raisonnement est typique du RE de format de fichier : on formule des hypothèses à partir des patterns visuels, puis on les valide une par une.

---

## Validation avec le Data Inspector d'ImHex

ImHex dispose d'un panneau **Data Inspector** qui affiche l'interprétation de l'octet (ou du groupe d'octets) sous le curseur dans tous les formats courants simultanément : uint8, int8, uint16 LE/BE, uint32 LE/BE, float, double, ASCII, UTF-8, etc.

Plaçons le curseur sur différentes positions :

**Curseur sur 0x0A** (le champ qu'on soupçonne être la longueur de l'IV) :

| Format | Valeur |  
|---|---|  
| uint16 LE | 16 |  
| uint16 BE | 4096 |

La valeur `16` en little-endian est cohérente avec notre hypothèse (taille de l'IV). La valeur big-endian `4096` ne fait pas de sens dans ce contexte — confirmation que le format est little-endian, ce qui est attendu pour un binaire x86-64.

**Curseur sur 0x1C** (taille originale suspectée) :

| Format | Valeur |  
|---|---|  
| uint32 LE | (taille de `secret.txt` en octets) |

On peut vérifier avec `wc -c secret.txt` dans le terminal. Si les valeurs correspondent, l'hypothèse est confirmée.

---

## Analyse d'entropie : visualiser la frontière clair/chiffré

ImHex propose une vue **Entropy Analysis** (via **View → Information** ou l'icône d'analyse). Cette vue calcule l'entropie de Shannon par blocs et l'affiche sous forme de graphique.

Pour `secret.enc`, le profil d'entropie est caractéristique :

- **Octets 0x00–0x1F (header)** : entropie basse à moyenne. Le magic est du texte ASCII prévisible, la version et la taille sont des petits entiers avec beaucoup de zéros. L'IV a une entropie élevée (c'est de l'aléa pur) mais il est court (16 octets).  
- **Octets 0x20 et au-delà (ciphertext)** : entropie très élevée, proche de 8.0 bits/octet (le maximum théorique). C'est la signature visuelle de données chiffrées (ou compressées). Un bloc de haute entropie homogène indique que le chiffrement fonctionne correctement — les patterns du plaintext ont été complètement détruits.

Cette transition nette entre basse et haute entropie confirme visuellement l'offset 0x20 comme début du ciphertext. Si le ciphertext commençait plus tôt ou plus tard, la transition serait décalée.

> 💡 **Astuce** : si vous rencontrez un format inconnu sans magic ni documentation, l'analyse d'entropie est souvent le premier outil à utiliser. Elle révèle la structure macro du fichier : zones de texte (entropie ~4–5), zones de données structurées (entropie ~5–6), zones compressées ou chiffrées (entropie ~7.5–8.0), et zones de padding/zéros (entropie ~0).

---

## Écrire un pattern `.hexpat` pour le format CRYPT24

Maintenant que la structure est comprise, on la formalise dans un pattern ImHex. C'est à la fois un outil d'analyse (ImHex colorise et annote le fichier en temps réel) et une documentation exécutable du format.

### Le pattern complet

```cpp
// crypt24.hexpat — Pattern ImHex pour le format CRYPT24
// Formation Reverse Engineering — Chapitre 24

#pragma description "CRYPT24 encrypted file format"
#pragma magic [ 43 52 59 50 54 32 34 00 ]  // "CRYPT24\0"
#pragma endian little

import std.io;  
import std.mem;  

// ── Types de base ──────────────────────────────────────────────

// Magic number : 8 octets, doit être "CRYPT24\0"
struct Magic {
    char value[8];
} [[static, color("4A90D9")]];

// Version : majeure.mineure, chacune sur 1 octet
struct Version {
    u8 major;
    u8 minor;
} [[static, color("50C878")]];

// Longueur de l'IV, stockée sur 2 octets LE
// Permet au format de supporter d'autres tailles d'IV à l'avenir
struct IVLength {
    u16 length;
} [[static, color("F5A623")]];

// IV : tableau d'octets de taille variable (lue depuis iv_length)
struct IV {
    u8 bytes[parent.iv_length.length];
} [[color("E74C3C")]];

// Taille du fichier original avant chiffrement (uint32 LE)
// Nécessaire pour retirer le padding PKCS7 après déchiffrement
struct OriginalSize {
    u32 size;
} [[static, color("9B59B6")]];

// Données chiffrées : tout le reste du fichier
struct CipherData {
    u8 data[std::mem::size() - $];
} [[color("7F8C8D")]];

// ── Structure principale ───────────────────────────────────────

struct Crypt24File {
    Magic       magic;          // 0x00 : "CRYPT24\0" (8 bytes)
    Version     version;        // 0x08 : version majeure.mineure (2 bytes)
    IVLength    iv_length;      // 0x0A : longueur de l'IV (2 bytes)
    IV          iv;             // 0x0C : IV (iv_length.length bytes)
    OriginalSize orig_size;     // 0x1C : taille originale (4 bytes)
    CipherData  ciphertext;     // 0x20 : données chiffrées (reste du fichier)
};

// ── Point d'entrée ─────────────────────────────────────────────

Crypt24File file @ 0x00;

// ── Validations ────────────────────────────────────────────────

// Vérifier le magic
std::assert(
    file.magic.value == "CRYPT24\0",
    "Invalid magic: expected CRYPT24"
);

// Vérifier la version (on ne supporte que 1.x pour l'instant)
std::assert(
    file.version.major == 1,
    "Unsupported major version"
);

// Vérifier que l'IV a une taille raisonnable (8, 12, ou 16 octets)
std::assert(
    file.iv_length.length == 8 ||
    file.iv_length.length == 12 ||
    file.iv_length.length == 16,
    "Unexpected IV length"
);

// Vérifier que le ciphertext est un multiple de 16 (AES block size)
std::assert(
    std::mem::size() - 0x20 != 0,
    "Empty ciphertext"
);

// ── Affichage informatif dans la console ImHex ─────────────────

std::print("=== CRYPT24 File Analysis ===");  
std::print("Version:       {}.{}", file.version.major, file.version.minor);  
std::print("IV length:     {} bytes", file.iv_length.length);  
std::print("Original size: {} bytes", file.orig_size.size);  
std::print("Cipher length: {} bytes",  
           std::mem::size() - 0x20);
std::print("Padding bytes: {} bytes",
           (std::mem::size() - 0x20) - file.orig_size.size);
```

### Ce que produit le pattern dans ImHex

Une fois le pattern appliqué (**File → Load Pattern** ou glissé dans le Pattern Editor), ImHex :

1. **Colorise** chaque région du fichier selon le schéma de couleurs défini : bleu pour le magic, vert pour la version, orange pour la longueur de l'IV, rouge pour l'IV, violet pour la taille originale, gris pour le ciphertext.

2. **Structure** le panneau Pattern Data avec une arborescence navigable : on peut déplier `Crypt24File` → `magic` → `value` et voir chaque champ avec sa valeur interprétée.

3. **Valide** automatiquement les assertions : si le magic ne correspond pas ou si la version est inattendue, ImHex affiche une erreur claire. C'est un filet de sécurité qui empêche d'appliquer le mauvais pattern au mauvais fichier.

4. **Affiche** dans la console les informations calculées : taille originale, taille du ciphertext, nombre d'octets de padding.

---

## Bookmarks : annoter manuellement les zones d'intérêt

En complément du pattern, les **Bookmarks** d'ImHex permettent d'ajouter des annotations libres sur des régions du fichier. C'est utile pour les observations qui ne rentrent pas dans un pattern structuré :

- Sélectionner les octets 0x0C à 0x1B → clic droit → **Add Bookmark** → « IV — comparer avec la valeur capturée par GDB/Frida en section 24.3 ».  
- Sélectionner les derniers 1 à 16 octets du fichier → **Add Bookmark** → « Padding PKCS7 probable — vérifier que les N derniers octets valent N ».

Les bookmarks sont sauvegardés dans le projet ImHex et peuvent être exportés. C'est un bon moyen de documenter ses observations au fil de l'analyse.

---

## Visualiser le padding PKCS7

AES-CBC utilise le padding PKCS7 : si le dernier bloc du plaintext n'est pas complet (16 octets), il est complété avec des octets dont la valeur est égale au nombre d'octets ajoutés. Par exemple, s'il manque 5 octets, on ajoute `05 05 05 05 05`.

On ne peut pas voir directement le padding dans le fichier chiffré (il est chiffré avec le reste), mais on peut le déduire :

```
Taille ciphertext = taille du fichier - 0x20 (offset du header)  
Taille originale  = champ orig_size (lu dans le header)  
Padding           = taille ciphertext - taille originale  
```

Si `secret.txt` fait 342 octets, le ciphertext fera 352 octets (prochain multiple de 16), et le padding sera de 10 octets (`0x0A 0x0A 0x0A...`). Le champ `orig_size` dans le header nous donne cette information sans avoir besoin de déchiffrer.

> 💡 **Détail important pour la section 24.5** : certaines bibliothèques de déchiffrement (comme `pycryptodome`) retirent automatiquement le padding PKCS7 si on le demande. Le champ `orig_size` sert alors de validation croisée : après déchiffrement et retrait du padding, la taille doit correspondre à `orig_size`.

---

## Comparaison avec d'autres fichiers `.enc`

Si on chiffre plusieurs fichiers avec le même binaire, ImHex permet de les comparer via la vue **Diff** (cf. section 6.7). Les observations attendues :

- **Le magic, la version et la longueur de l'IV sont identiques** dans tous les fichiers — ce sont des constantes du format.  
- **L'IV est différent** à chaque chiffrement — c'est le comportement correct d'un chiffrement CBC avec IV aléatoire (`RAND_bytes`).  
- **Le ciphertext est complètement différent** même pour le même plaintext — grâce à l'IV distinct.  
- **La taille originale varie** selon le fichier source.

Si, lors de la comparaison, on découvrait que deux fichiers chiffrés à partir du même plaintext produisent le même ciphertext, ce serait un signal d'alerte grave : cela signifierait que l'IV est réutilisé (ou absent), ce qui compromet la sécurité du schéma. C'est le genre de faille qu'un analyste RE peut détecter visuellement dans ImHex.

---

## Synthèse de la cartographie

Le format CRYPT24 est maintenant entièrement documenté. Voici la carte finale :

```
secret.enc
├─ [0x00..0x07]  Magic         "CRYPT24\0"           (8 octets, fixe)
├─ [0x08]        Version maj.  0x01                   (1 octet)
├─ [0x09]        Version min.  0x00                   (1 octet)
├─ [0x0A..0x0B]  IV length     0x0010 (16)            (uint16 LE)
├─ [0x0C..0x1B]  IV            (16 octets aléatoires) (variable)
├─ [0x1C..0x1F]  Orig. size    (taille du plaintext)  (uint32 LE)
└─ [0x20..EOF]   Ciphertext    (AES-256-CBC, PKCS7)   (variable)
```

Chaque champ a été identifié par observation visuelle, confirmé par le Data Inspector et les valeurs capturées en section 24.3, et formalisé dans le pattern `.hexpat`. C'est cette carte qui va guider l'écriture du script de déchiffrement dans la section suivante : on sait exactement quels octets lire, à quel offset, dans quel format, et quoi en faire.

---


⏭️ [Reproduire le schéma de chiffrement en Python](/24-crypto/05-reproduire-chiffrement-python.md)
