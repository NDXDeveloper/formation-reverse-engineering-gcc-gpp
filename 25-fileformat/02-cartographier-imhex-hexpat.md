🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 25.2 — Cartographier les champs avec ImHex et un pattern `.hexpat` itératif

> 🎯 **Objectif de cette section** : construire pas à pas un pattern `.hexpat` complet pour le format CFR, en alternant observation hexadécimale et hypothèses structurelles. À la fin, chaque octet de l'archive sera identifié et colorisé dans ImHex.

---

## L'approche itérative

Écrire un pattern `.hexpat` pour un format inconnu ne se fait jamais en une passe. C'est un processus de va-et-vient entre trois activités :

1. **Observer** les octets bruts dans la vue hexadécimale.  
2. **Formuler une hypothèse** sur la signification d'un groupe d'octets.  
3. **Écrire le fragment de pattern** correspondant, l'appliquer, et vérifier si la colorisation produite est cohérente avec le reste du fichier.

Si le pattern colore correctement une zone, l'hypothèse est validée — on passe à la zone suivante. Si la colorisation déborde ou ne correspond pas aux données visibles, l'hypothèse est fausse — on la corrige. C'est exactement la même méthode scientifique que dans le reste du RE, mais appliquée aux données plutôt qu'au code.

Ouvrons `demo.cfr` dans ImHex et commençons.

---

## Passe 1 — Le header

### Observation brute

En section 25.1, on a déterminé que le header commence à l'offset `0x00` et fait probablement 32 octets. Observons-les dans ImHex avec le Data Inspector activé (panneau latéral qui affiche la valeur de l'octet ou du groupe sélectionné dans différents types : `uint8`, `uint16 LE`, `uint32 LE`, `float`, `char[]`…).

```
Offset    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
00000000  43 46 52 4D 02 00 02 00 04 00 00 00 XX XX XX XX
00000010  XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX
```

Décortiquons ce qu'on voit, en lisant de gauche à droite :

| Offset | Octets | Data Inspector (LE) | Interprétation probable |  
|--------|--------|---------------------|------------------------|  
| `0x00` | `43 46 52 4D` | ASCII `"CFRM"` | Magic bytes — confirmé |  
| `0x04` | `02 00` | `uint16 = 2` | Numéro de version ? |  
| `0x06` | `02 00` | `uint16 = 2` | Flags ? (valeur 2 = bit 1 actif) |  
| `0x08` | `04 00 00 00` | `uint32 = 4` | Nombre d'enregistrements ? (demo.cfr en contient 4) |  
| `0x0C` | `XX XX XX XX` | `uint32` = grand nombre | Timestamp UNIX ? |  
| `0x10` | `XX XX XX XX` | `uint32` | Inconnu — checksum ? |  
| `0x14` | Texte ASCII | Chaîne lisible | Nom d'auteur ? |  
| `0x1C` | `XX XX XX XX` | `uint32` | Inconnu — "reserved" ? |

Plusieurs indices convergent déjà. La valeur `4` en offset `0x08` correspond au nombre d'enregistrements qu'on a comptés avec `strings` en section 25.1 (quatre noms de fichiers). La valeur `2` en offset `0x04` correspond au `version=2` qu'on avait vu dans les métadonnées. La valeur `2` en offset `0x06` s'interprète bien comme un champ de flags à un bit actif (le bit 1).

Le champ en `0x0C` : si on convertit la valeur en date (`date -d @<valeur>`), on obtient une date récente cohérente avec le moment de génération du fichier. C'est un timestamp UNIX.

Le champ en `0x14` : on y lit le nom d'utilisateur du système (8 caractères, complété par des zéros). C'est le champ `author` qu'on avait deviné via `strings`.

### Premier fragment de pattern

On peut maintenant écrire une première ébauche du header :

```hexpat
#pragma endian little

import std.io;

struct CFRHeader {
    char magic[4];        // 0x00 — "CFRM"
    u16  version;         // 0x04 — format version
    u16  flags;           // 0x06 — bitfield
    u32  num_records;     // 0x08 — nombre d'enregistrements
    u32  timestamp;       // 0x0C — UNIX timestamp
    u32  header_crc;      // 0x10 — CRC (à identifier)
    char author[8];       // 0x14 — auteur, null-padded
    u8   reserved[4];     // 0x1C — usage inconnu
};

CFRHeader header @ 0x00;
```

Appliquons ce pattern dans ImHex (*Pattern Editor → coller → exécuter*). Si tout va bien, les 32 premiers octets se colorisent et les noms de champs apparaissent dans le panneau *Pattern Data*. On peut alors vérifier que les valeurs affichées sont cohérentes :

- `magic` = `"CFRM"` ✓  
- `version` = `2` ✓  
- `flags` = `2` (binaire : `0b10` → bit 1 actif) ✓  
- `num_records` = `4` ✓  
- `timestamp` = valeur UNIX plausible ✓  
- `author` = nom d'utilisateur ✓

### Valider sur les autres archives

Ouvrons `packed_noxor.cfr` avec le même pattern. On devrait observer :

- `flags` = `2` (binaire : `0b10` → seul le bit 1 actif, comme `demo.cfr`)  
- `num_records` = `3`

Et pour `packed_xor.cfr` :

- `flags` = `3` (binaire : `0b11` → bits 0 et 1 actifs)  
- `num_records` = `3`

Le bit 0 est actif **uniquement** dans `packed_xor.cfr` — l'archive où les données textuelles sont illisibles avec `strings` (section 25.1). C'est donc le **flag XOR**. Le bit 1 est actif dans les trois archives — c'est probablement le **flag footer** (puisqu'on a identifié le magic `CRFE` à la fin de chaque fichier).

Enrichissons notre pattern avec des constantes nommées :

```hexpat
bitfield Flags {
    xor_enabled : 1;     // bit 0 — données XOR-obfusquées
    has_footer  : 1;     // bit 1 — footer présent en fin de fichier
    padding     : 14;
};
```

Et remplaçons `u16 flags;` par `Flags flags;` dans la structure.

### Le mystère du champ `reserved`

Le champ à l'offset `0x1C` (4 octets) contient une valeur non nulle, mais son rôle n'est pas encore clair. On le garde tel quel avec un commentaire `// usage inconnu` et on y reviendra. Dans le processus itératif, il est normal de laisser temporairement des zones annotées comme « inconnues » — elles se clarifient souvent quand on comprend mieux le reste du format.

> 💡 **Astuce ImHex** : utilisez la fonctionnalité *Bookmarks* pour marquer les zones dont la signification reste incertaine. Créez un bookmark « reserved — à investiguer » sur les octets `0x1C–0x1F`. Quand vous y reviendrez, le signet sera là pour vous rappeler la question ouverte.

---

## Passe 2 — Le premier enregistrement

### Observation

À l'offset `0x20` (juste après le header de 32 octets), on observe :

```
Offset    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
00000020  01 00 0C 00 40 00 00 00 67 72 65 65 74 69 6E 67  ....@...greeting
00000030  2E 74 78 74 48 65 6C 6C 6F 20 66 72 6F 6D 20 74  .txtHello from t
00000040  68 65 20 43 46 52 20 61 72 63 68 69 76 65 20 66  he CFR archive f
```

Décomposons l'en-tête du record :

| Offset | Octets | Valeur LE | Hypothèse |  
|--------|--------|-----------|-----------|  
| `0x20` | `01` | `uint8 = 1` | Type d'enregistrement ? (1 = TEXT ?) |  
| `0x21` | `00` | `uint8 = 0` | Flags par enregistrement ? Padding ? |  
| `0x22` | `0C 00` | `uint16 = 12` | Longueur du nom ? (`"greeting.txt"` fait 12 caractères) |  
| `0x24` | `40 00 00 00` | `uint32 = 64` | Longueur des données ? |  
| `0x28` | `67 72 65 65…` | ASCII | Le nom : `"greeting.txt"` |

Le champ `uint16 = 12` en `0x22` correspond exactement à la longueur de `"greeting.txt"`. La chaîne commence juste après, à l'offset `0x28`. C'est cohérent avec un en-tête d'enregistrement de **8 octets** (1 + 1 + 2 + 4), suivi du nom de longueur variable.

Après le nom (`0x28` + 12 = `0x34`), on trouve les données. Ici, `demo.cfr` n'ayant pas le flag XOR actif (bit 0 = 0), les données sont en clair — on peut lire directement `"Hello from t"` à partir de l'offset `0x34` dans le dump hexadécimal ci-dessus. Si la longueur des données est 64 octets, elles s'étendent de `0x34` à `0x73` inclus.

Regardons au-delà de la zone de données, à l'offset `0x74` : on devrait trouver soit le CRC-16 de l'enregistrement (2 octets), soit le début de l'enregistrement suivant.

```
00000074  XX XX 02 00 08 00 18 00 00 00 64 61 74 61 2E 62  ..........data.b
```

On lit `02` à l'offset `0x76` — un nouvel octet de type (`2` = BINARY ?), précédé de 2 octets (`XX XX` à `0x74–0x75`) qui pourraient être le CRC-16 de l'enregistrement précédent. Puis `00` (flags), `08 00` (uint16 = 8, longueur du nom `"data.bin"`), `18 00 00 00` (uint32 = 24, longueur des données).

Le pattern se confirme : chaque enregistrement est structuré comme **en-tête (8 octets) + nom (variable) + données (variable) + CRC-16 (2 octets)**.

> 📝 **Note** : si on ouvrait `packed_xor.cfr` à ce stade, on verrait les mêmes en-têtes de records (mêmes noms, mêmes longueurs), mais les zones de données contiendraient des octets non lisibles — puisque le flag XOR est actif dans cette archive. On reviendra sur cette transformation en passe 4.

### Deuxième fragment de pattern

```hexpat
enum RecordType : u8 {
    TEXT   = 0x01,
    BINARY = 0x02,
    META   = 0x03
};

struct RecordHeader {
    RecordType type;      // 0x00 — type de contenu
    u8         flags;     // 0x01 — flags par enregistrement
    u16        name_len;  // 0x02 — longueur du nom
    u32        data_len;  // 0x04 — longueur des données
};

struct Record {
    RecordHeader rh;
    char  name[rh.name_len];
    u8    data[rh.data_len];
    u16   crc16;
};
```

Ajoutons cela au pattern principal :

```hexpat
CFRHeader header @ 0x00;  
Record records[header.num_records] @ 0x20;  
```

En appliquant ce pattern, ImHex devrait coloriser l'intégralité des enregistrements. Si le tableau `records` se déploie correctement et que le dernier enregistrement se termine juste avant le magic `CRFE`, on a validé la structure des records.

### Vérification par la cohérence des types

Parcourons les 4 enregistrements de `demo.cfr` dans le panneau *Pattern Data* :

| # | `type` | `name_len` | `name` | `data_len` |  
|---|--------|------------|--------|------------|  
| 0 | TEXT (1) | 12 | `greeting.txt` | 64 |  
| 1 | BINARY (2) | 8 | `data.bin` | 24 |  
| 2 | META (3) | 12 | `version.meta` | 35 |  
| 3 | TEXT (1) | 9 | `notes.txt` | 116 |

Quatre enregistrements, trois types différents, des longueurs de noms qui correspondent aux chaînes visibles — tout est cohérent. Puisque le flag XOR n'est pas actif dans `demo.cfr`, les données des enregistrements TEXT et META sont directement lisibles dans la vue hexadécimale.

---

## Passe 3 — Le footer

### Observation

Le magic `CRFE` se situe en fin de fichier. La taille de `demo.cfr` est 364 octets, donc le footer commence à l'offset `364 - 12 = 352` (`0x160`) si notre estimation de 12 octets est juste. Vérifions :

```
Offset           Octets
0x160       43 52 46 45 XX XX XX XX YY YY YY YY
            C  R  F  E  ........    ........
```

| Offset relatif | Octets | Valeur LE | Hypothèse |  
|---|---|---|---|  
| `+0x00` | `43 52 46 45` | ASCII `"CRFE"` | Magic footer |  
| `+0x04` | `XX XX XX XX` | `uint32` | Taille totale du fichier ? |  
| `+0x08` | `YY YY YY YY` | `uint32` | CRC-32 global ? |

Le champ à `+0x04` : convertissons la valeur. Si on obtient exactement 364, l'hypothèse « taille totale » est confirmée. C'est un pattern classique dans les formats d'archive — stocker la taille totale dans le footer permet de détecter les fichiers tronqués.

Le champ à `+0x08` est probablement un CRC-32 global calculé sur l'ensemble du fichier *avant* le footer (c'est-à-dire sur les `364 - 12 = 352` premiers octets).

### Fragment de pattern

```hexpat
struct CFRFooter {
    char magic[4];        // "CRFE"
    u32  total_size;      // taille totale du fichier
    u32  global_crc;      // CRC-32 de tout ce qui précède
};
```

Pour placer le footer correctement, il faut le positionner juste après le dernier enregistrement. En `.hexpat`, on peut utiliser le curseur implicite (le pattern se place séquentiellement après les structures précédentes) :

```hexpat
CFRHeader header @ 0x00;  
Record records[header.num_records] @ 0x20;  
// Le footer suit immédiatement les records si le flag est actif
```

Ou bien le placer explicitement à la fin :

```hexpat
// Placement à la fin du fichier (taille - 12 octets)
CFRFooter footer @ (std::mem::size() - 12);
```

La deuxième approche est plus robuste : elle fonctionne même si notre calcul des tailles d'enregistrements a un léger décalage. Si le footer se colorise correctement à cette position (magic `CRFE` lisible, `total_size` = taille du fichier), c'est une double validation : le footer est bien là **et** notre calcul séquentiel des records est correct.

### Vérification croisée

Si le footer est à l'offset `F`, alors les enregistrements occupent les octets de `0x20` à `F - 1`. On peut vérifier que la somme `32 (header) + taille des records + 12 (footer) = taille du fichier`. Si l'égalité tient, notre cartographie est complète — chaque octet du fichier est attribué à une structure.

---

## Passe 4 — Comprendre la transformation XOR

Ouvrons maintenant `packed_noxor.cfr` et `packed_xor.cfr` côte à côte dans ImHex (fonctionnalité *Diff View* ou simplement deux onglets). Appliquons le même pattern aux deux fichiers.

Les en-têtes de records sont identiques dans les deux fichiers (mêmes noms, mêmes longueurs). Seules les zones `data[]` diffèrent. Comparons octet par octet la zone de données du premier enregistrement :

```
packed_noxor.cfr (data) : 54 68 69 73 20 69 73 20 61 20 70 6C ...  
packed_xor.cfr   (data) : 0E 54 FF 82 7A 55 EB D1 3B 0C E6 9D ...  
```

La première ligne est du texte ASCII lisible (`"This is a "…`). La deuxième est transformée. Calculons le XOR entre les deux :

```
0x54 ^ 0x0E = 0x5A
0x68 ^ 0x54 = 0x3C
0x69 ^ 0xFF = 0x96
0x73 ^ 0x82 = 0xF1
0x20 ^ 0x7A = 0x5A    ← le motif se répète
0x69 ^ 0x55 = 0x3C
0x73 ^ 0xEB = 0x96    (attention au détail du calcul réel)
0x20 ^ 0xD1 = 0xF1
```

La clé XOR est `5A 3C 96 F1`, et elle se répète tous les 4 octets. C'est un **XOR rotatif à clé fixe de 4 octets**. On peut le vérifier sur l'intégralité des données : chaque octet `data_xor[i]` est égal à `data_plain[i] ^ key[i % 4]`.

On confirme aussi ce qu'on avait observé en section 25.1 : `demo.cfr` (flags = `0x0002`, bit 0 = 0) stocke les données en clair, tandis que `packed_xor.cfr` (flags = `0x0003`, bit 0 = 1) les transforme. Le bit 0 du header contrôle bien l'activation du XOR.

> 💡 **Astuce ImHex** : ImHex possède un outil intégré de XOR dans le *Data Processor* (panneau de traitement de données). Vous pouvez sélectionner une zone, appliquer un XOR avec la clé `5A3C96F1`, et vérifier que le résultat correspond au texte en clair.

Enrichissons notre pattern pour gérer la transformation :

```hexpat
// On ne peut pas "déchiffrer" dans un .hexpat standard,
// mais on peut annoter la zone et documenter la clé.

struct Record {
    RecordHeader rh;
    char  name[rh.name_len];

    // Si le flag XOR du header est actif, ces octets sont
    // XOR avec la clé rotative {0x5A, 0x3C, 0x96, 0xF1}
    u8    data[rh.data_len] [[comment("XOR key: 5A 3C 96 F1 si flag bit 0")]];

    u16   crc16;
};
```

> 📝 **Note importante** : le CRC-16 qui suit les données est-il calculé sur les données *avant* ou *après* le XOR ? C'est une question cruciale. On peut le déterminer empiriquement : calculer le CRC-16 sur les données en clair (dans l'archive non-XOR) et vérifier si la valeur correspond au CRC-16 stocké dans l'archive XOR pour le même enregistrement. Si les CRC sont identiques entre les deux archives pour le même enregistrement, alors le CRC est calculé sur les données **avant** XOR (données originales). Sinon, il est calculé sur les données **après** XOR (données telles que stockées).  
>  
> Cette distinction est fondamentale pour écrire un parser correct — elle détermine l'ordre des opérations lors de la validation.

---

## Passe 5 — Identifier le CRC-16

On sait qu'un CRC-16 de 2 octets termine chaque enregistrement. Mais quel CRC-16 exactement ? Il existe de nombreuses variantes (CRC-16/CCITT, CRC-16/XMODEM, CRC-16/IBM, CRC-16/ARC…), qui diffèrent par le polynôme, la valeur initiale et la réflexion des bits.

### Approche empirique

Prenons l'enregistrement le plus simple dans `packed_noxor.cfr` (pas de XOR à gérer). On connaît les octets du nom et des données en clair. On extrait le CRC-16 stocké. On pourrait tout aussi bien utiliser `demo.cfr`, qui n'a pas non plus le flag XOR actif.

Ensuite, on essaie les variantes CRC-16 courantes sur la concaténation `nom + données` avec un outil comme `reveng` (CRC RevEng) ou un script Python :

```python
# Script de force brute des variantes CRC-16 connues
import crcmod

data = nom_bytes + payload_bytes  
stored_crc = 0xXXXX  # valeur lue dans le fichier  

# Tester CRC-16/CCITT avec init=0x1D0F
crc_func = crcmod.mkCrcFun(0x11021, initCrc=0x1D0F, xorOut=0x0000)  
computed = crc_func(data)  
print(f"CCITT init=0x1D0F : {computed:#06x}  {'MATCH' if computed == stored_crc else ''}")  
```

> 💡 **Le Data Inspector d'ImHex peut aussi aider.** Si vous sélectionnez exactement les octets sur lesquels le CRC devrait être calculé (nom + données), certaines versions d'ImHex affichent des checksums dans le panneau latéral. Cela ne couvre pas toutes les variantes, mais peut confirmer rapidement une hypothèse.

Quand la variante est identifiée (ici : CRC-16/CCITT avec polynôme `0x1021` et valeur initiale `0x1D0F`), ajoutons un commentaire dans le pattern :

```hexpat
u16 crc16 [[comment("CRC-16/CCITT poly=0x1021 init=0x1D0F sur name+data")]];
```

---

## Passe 6 — Retour sur le header : CRC et `reserved`

### Le CRC-32 du header

Le champ `header_crc` à l'offset `0x10` est un CRC-32. Mais sur quels octets est-il calculé ? Il ne peut pas se couvrir lui-même (problème de poule et d'œuf). L'approche standard est de mettre ce champ à zéro avant de calculer le CRC.

Hypothèse : le CRC-32 est calculé sur les 16 premiers octets du header (offsets `0x00` à `0x0F`), c'est-à-dire `magic + version + flags + num_records + timestamp`, le champ `header_crc` étant exclu du calcul car il vient juste après.

Vérification : extraire les 16 premiers octets, calculer le CRC-32 standard (polynôme `0xEDB88320`, init `0xFFFFFFFF`, XOR final `0xFFFFFFFF`), et comparer avec la valeur stockée.

```bash
$ python3 -c "
import struct, binascii  
with open('demo.cfr', 'rb') as f:  
    data = f.read(32)
# CRC-32 des 16 premiers octets
crc = binascii.crc32(data[:16]) & 0xFFFFFFFF  
stored = struct.unpack_from('<I', data, 0x10)[0]  
print(f'Computed: {crc:#010x}')  
print(f'Stored:   {stored:#010x}')  
print('MATCH' if crc == stored else 'MISMATCH')  
"
```

Si ça ne matche pas directement, il faut tester une variante : peut-être que le CRC est calculé sur les 16 premiers octets avec `header_crc` mis à zéro. C'est un exercice de tâtonnement classique en reverse de formats.

### Le champ `reserved`

Revenons au champ de 4 octets en `0x1C`. On peut maintenant formuler une hypothèse en croisant les informations de nos trois archives. Calculons la valeur `reserved` et les `data_len` de chaque enregistrement :

Pour `demo.cfr` : data_len des 4 records = 64, 24, 35, 116.

Essayons quelques opérations :
- Somme : 64 + 24 + 35 + 116 = 239 → comparer avec `reserved`  
- XOR : 64 ^ 24 ^ 35 ^ 116 → comparer avec `reserved`

Si le XOR de toutes les `data_len` correspond à la valeur stockée, on a trouvé : c'est un **checksum léger** qui permet de vérifier rapidement la cohérence des tailles sans recalculer tous les CRC. Vérifions sur les trois archives pour confirmer.

Mettons à jour le pattern :

```hexpat
struct CFRHeader {
    char   magic[4];
    u16    version;
    Flags  flags;
    u32    num_records;
    u32    timestamp;
    u32    header_crc;      // CRC-32 des 16 premiers octets
    char   author[8];       // null-padded
    u32    data_len_xor;    // XOR de tous les data_len (vérification)
};
```

---

## Pattern `.hexpat` complet

Voici le pattern consolidé après nos six passes. Il peut être appliqué sur n'importe quelle archive CFR :

```hexpat
#pragma endian little
#pragma pattern_limit 65536

import std.io;  
import std.mem;  

// ───────────────────────────────────────
//  Constants
// ───────────────────────────────────────

#define HEADER_MAGIC "CFRM"
#define FOOTER_MAGIC "CRFE"

// ───────────────────────────────────────
//  Enums & Bitfields
// ───────────────────────────────────────

enum RecordType : u8 {
    TEXT   = 0x01,
    BINARY = 0x02,
    META   = 0x03
};

bitfield HeaderFlags {
    xor_enabled : 1;     // bit 0 — données XOR avec clé {5A, 3C, 96, F1}
    has_footer  : 1;     // bit 1 — footer CRFE présent
    padding     : 14;
};

// ───────────────────────────────────────
//  Header (32 octets)
// ───────────────────────────────────────

struct CFRHeader {
    char        magic[4];       // 0x00 — "CFRM"
    u16         version;        // 0x04 — version du format (attendu : 2)
    HeaderFlags flags;          // 0x06 — bitfield
    u32         num_records;    // 0x08 — nombre d'enregistrements
    u32         timestamp;      // 0x0C — date de création (UNIX epoch)
    u32         header_crc;     // 0x10 — CRC-32 des octets [0x00..0x0F]
    char        author[8];      // 0x14 — auteur, null-padded
    u32         data_len_xor;   // 0x1C — XOR de tous les data_len
};

// ───────────────────────────────────────
//  Record (taille variable)
//
//  Layout :
//    RecordHeader  (8 octets)
//    name          (name_len octets, ASCII, non transformé)
//    data          (data_len octets, XOR si flag actif)
//    crc16         (2 octets, CRC-16/CCITT init=0x1D0F
//                   calculé sur name + data AVANT XOR)
// ───────────────────────────────────────

struct RecordHeader {
    RecordType type;        // 0x00
    u8         flags;       // 0x01 — réservé (toujours 0)
    u16        name_len;    // 0x02
    u32        data_len;    // 0x04
};

struct Record {
    RecordHeader rh;
    char name[rh.name_len];
    u8   data[rh.data_len]
        [[comment("XOR clé rotative {5A,3C,96,F1} si header.flags.xor_enabled")]];
    u16  crc16
        [[comment("CRC-16/CCITT poly=0x1021 init=0x1D0F sur name||data_original")]];
};

// ───────────────────────────────────────
//  Footer (12 octets, optionnel)
// ───────────────────────────────────────

struct CFRFooter {
    char magic[4];       // "CRFE"
    u32  total_size;     // taille totale du fichier en octets
    u32  global_crc;     // CRC-32 de tout ce qui précède le footer
};

// ───────────────────────────────────────
//  Instanciation
// ───────────────────────────────────────

CFRHeader header @ 0x00;  
Record    records[header.num_records] @ 0x20;  

// Footer conditionnel en fin de fichier
if (header.flags.has_footer) {
    CFRFooter footer @ (std::mem::size() - 12);
}
```

### Résultat dans ImHex

Une fois appliqué, ce pattern produit une colorisation complète du fichier :

- Le **header** (32 octets) est colorisé en un bloc, avec chaque champ identifiable dans le panneau *Pattern Data*.  
- Chaque **enregistrement** est colorisé individuellement avec ses sous-champs (type, flags, name_len, data_len, nom, données, CRC).  
- Le **footer** (12 octets) est colorisé à la fin du fichier.

Il ne reste aucun octet non attribué entre le header et le footer — c'est la meilleure preuve que notre cartographie est correcte.

---

## Leçons méthodologiques

Ce processus en six passes illustre plusieurs principes généraux du reverse de formats :

**Commencer par ce qu'on voit.** Les magic bytes et les chaînes ASCII sont des points d'ancrage gratuits. Ils permettent de segmenter le fichier avant même de comprendre les champs numériques. Dans `demo.cfr`, les données en clair nous ont permis de vérifier visuellement la délimitation de chaque record.

**Valider sur plusieurs fichiers.** Un seul fichier d'exemple ne suffit jamais. Les valeurs fixes (magic, version) se confirment par leur constance entre fichiers. Les valeurs variables (timestamps, CRC) se confirment par leur variation cohérente. C'est la comparaison entre `packed_noxor.cfr` et `packed_xor.cfr` qui nous a permis de comprendre le XOR, et la comparaison des flags entre les trois archives qui a révélé la signification de chaque bit.

**Laisser des inconnues.** Il est tentant de vouloir tout comprendre avant d'avancer, mais c'est contre-productif. Le champ `reserved` n'a été élucidé qu'en passe 6, une fois qu'on disposait de toutes les `data_len`. Accepter les zones d'ombre temporaires et y revenir avec plus de contexte est la marque d'un reverse engineer efficace.

**Vérifier les CRC sur données connues.** Pour identifier une variante de CRC, il faut disposer à la fois des données en clair et du CRC stocké. Les archives sans XOR (`demo.cfr` et `packed_noxor.cfr`) sont précieuses pour cela — elles éliminent une variable de l'équation.

**Le pattern `.hexpat` est un document vivant.** Il ne se fige pas à la fin de cette section. Le fuzzing (section 25.3) peut révéler des cas limites (enregistrement de taille zéro, nom vide, flags non nuls sur un record) qui nécessitent des ajustements au pattern. L'écriture du parser Python (section 25.4) peut aussi mettre en lumière des ambiguïtés que le pattern ne couvrait pas.

---



⏭️ [Confirmer l'interprétation avec AFL++ (fuzzing du parser)](/25-fileformat/03-confirmer-afl-fuzzing.md)
