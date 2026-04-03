🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.11 — Cas pratique : cartographier un format de fichier custom avec `.hexpat`

> 🎯 **Objectif de cette section** : Mettre en œuvre l'ensemble des compétences acquises dans ce chapitre — exploration visuelle, Data Inspector, bookmarks, patterns `.hexpat`, recherche, entropie, désassembleur et YARA — dans un scénario réaliste de reverse engineering d'un format de fichier propriétaire dont aucune documentation n'existe.

> 📦 **Binaire de test** : `binaries/ch06-fileformat/fileformat_O0`  
> 📁 **Fichier de données** : un fichier `.cdb` (custom database) produit par ce binaire  
> 📁 **Pattern produit** : `hexpat/ch06_fileformat.hexpat`

---

## Le scénario

Vous êtes chargé d'analyser une application compilée avec GCC qui stocke ses données dans un format de fichier propriétaire portant l'extension `.cdb` (custom database). Vous disposez du binaire (`fileformat_O0`) et d'un fichier de données (`sample.cdb`) produit par ce programme. Vous n'avez ni le code source, ni la documentation du format. Votre objectif est de **comprendre la structure du fichier `.cdb`** suffisamment pour pouvoir le lire, le modifier, et éventuellement écrire un parser indépendant en Python (ce que nous ferons au chapitre 25).

Ce scénario est courant en reverse engineering : un logiciel propriétaire utilise un format de stockage non documenté, et vous devez le comprendre — pour l'interopérabilité, l'audit de sécurité, la migration de données, ou simplement la curiosité technique.

Dans cette section, nous allons dérouler l'analyse complète, étape par étape, en utilisant exclusivement ImHex et les techniques vues dans ce chapitre. C'est un exercice intégrateur qui mobilise les sections 6.1 à 6.10.

---

## Phase 1 — Reconnaissance initiale

### Triage en ligne de commande

Avant d'ouvrir ImHex, appliquons le workflow de triage rapide du chapitre 5 sur le fichier de données :

```bash
file sample.cdb
# sample.cdb: data

strings sample.cdb
# (quelques chaînes lisibles apparaissent : des noms, des descriptions...)

xxd sample.cdb | head -5
# 00000000: 4344 4232 0200 0100 0500 0000 8000 0000  CDB2............
# 00000010: 0100 0000 0300 0000 0000 0000 0000 0000  ................
# 00000020: ...
```

Premiers enseignements. La commande `file` ne reconnaît pas le format — c'est attendu pour un format propriétaire. La commande `strings` révèle des chaînes lisibles, ce qui signifie que le fichier n'est pas chiffré ni compressé dans sa totalité. Et `xxd` nous montre les premiers octets : `43 44 42 32` — la chaîne ASCII `CDB2`. C'est notre **magic number**.

### Ouverture dans ImHex et premier regard

Ouvrons `sample.cdb` dans ImHex. Voici ce que nous observons dans la vue hexadécimale :

- Les 4 premiers octets sont `43 44 42 32` (`CDB2`). Magic number confirmé.  
- Les octets suivants montrent de petits entiers entrecoupés de zéros — probable structure de header avec des champs courts et du padding ou des compteurs.  
- Plus loin dans le fichier, on aperçoit des blocs de texte lisible (les chaînes vues par `strings`) entrecoupés de données binaires structurées.  
- Aucune zone ne semble être de l'aléatoire pur — pas de bloc chiffré évident à première vue.

### Analyse d'entropie

Ouvrons **View → Information** pour voir le profil d'entropie. Le graphe montre une entropie modérée et relativement uniforme (autour de 4–5 bits/octet) sur l'ensemble du fichier, avec quelques creux localisés. Ce profil est caractéristique d'un fichier de données structurées contenant du texte et des entiers — pas de compression ni de chiffrement. Cette observation confirme que le fichier est analysable directement, sans étape de décompression ou déchiffrement préalable.

### Bookmarks exploratoires

Posons nos premiers bookmarks avant d'aller plus loin :

- **Offset 0x00, 4 octets** → bookmark `Magic number "CDB2"` (couleur orange).  
- **Offset 0x04, ~28 octets** → bookmark `Header (structure inconnue)` (couleur orange). Nous ajusterons la taille quand nous aurons compris le header.  
- Les premières zones de texte lisible → bookmark `Zone de données textuelles` (couleur jaune).

Ces bookmarks exploratoires sont temporaires. Ils capturent notre compréhension initiale et serviront de points de navigation pendant la suite de l'analyse.

---

## Phase 2 — Décryptage du header

### Exploration avec le Data Inspector

Plaçons le curseur sur chaque groupe d'octets du début du fichier et observons le Data Inspector.

**Offset 0x00** — `43 44 42 32` : le Data Inspector montre `char[4] = "CDB2"`. Magic number, 4 octets.

**Offset 0x04** — `02 00` : `uint16_t = 2`. Probablement un numéro de version (le magic dit « CDB2 », ce champ dit version 2 — cohérent).

**Offset 0x06** — `01 00` : `uint16_t = 1`. Trop tôt pour savoir ce que c'est. Notons « champ inconnu, valeur 1 ».

**Offset 0x08** — `05 00 00 00` : `uint32_t = 5`. Un compteur ? Le fichier contient peut-être 5 enregistrements. Hypothèse à vérifier.

**Offset 0x0C** — `80 00 00 00` : `uint32_t = 128` (0x80). Une taille ? Un offset ? Si c'est un offset, les données commenceraient à l'offset 128 dans le fichier. Si c'est une taille, le bloc de données fait 128 octets. Notons les deux hypothèses.

**Offset 0x10** — `01 00 00 00` : `uint32_t = 1`. Une valeur ou un flag.

**Offset 0x14** — `03 00 00 00` : `uint32_t = 3`. Un autre compteur ?

**Offset 0x18** — 8 octets nuls. Padding de fin de header, ou champs réservés.

À ce stade, nous formulons une première hypothèse de header :

```
Offset  Taille  Hypothèse
0x00    4       Magic "CDB2"
0x04    2       Version (2)
0x06    2       Inconnu (1) — sous-version ? flags ?
0x08    4       Nombre d'enregistrements (5)
0x0C    4       Offset ou taille (128)
0x10    4       Inconnu (1)
0x14    4       Inconnu (3) — nombre de champs par record ?
0x18    8       Réservé / padding (zéros)
Total : 32 octets (0x20)
```

### Premier pattern `.hexpat`

Traduisons cette hypothèse en pattern :

```cpp
struct CDB_Header {
    char magic[4]       [[comment("Doit valoir 'CDB2'")]];
    u16  version        [[comment("Version du format")]];
    u16  unknown_06     [[comment("Sous-version ou flags ?")]];
    u32  record_count   [[comment("Nombre d'enregistrements")]];
    u32  data_offset    [[comment("Offset vers la zone de données ?")]];
    u32  unknown_10     [[comment("À déterminer")]];
    u32  field_count    [[comment("Nombre de champs par record ?")]];
    padding[8]          [[comment("Réservé")]];
};

CDB_Header header @ 0x00;
```

Évaluons (`F5`). L'arbre Pattern Data affiche :

```
header
├── magic        = "CDB2"
├── version      = 2
├── unknown_06   = 1
├── record_count = 5
├── data_offset  = 128
├── unknown_10   = 1
└── field_count  = 3
```

Les valeurs sont plausibles. L'hypothèse `data_offset = 128` est testable : naviguons à l'offset `0x80` (128) dans la vue hexadécimale. Si nous y trouvons le début de données structurées (et pas le milieu d'un bloc), l'hypothèse est renforcée.

---

## Phase 3 — La zone entre le header et les données

### Identifier la table intermédiaire

Entre le header (32 octets, offsets 0x00–0x1F) et l'offset 0x80 (supposé début des données), il y a 96 octets (offsets 0x20–0x7F). Que contiennent-ils ?

Explorons cette zone avec le Data Inspector en déplaçant le curseur. On observe un pattern répétitif : des blocs de taille apparemment fixe qui se succèdent. Chaque bloc semble contenir un petit entier suivi d'une chaîne courte puis d'octets nuls de padding.

Posons un bookmark `Table intermédiaire (0x20–0x9F, 128 octets)` (couleur verte) et examinons de plus près.

En observant les blocs, nous identifions une structure régulière de 24 octets :

```
Offset 0x20 : 01 00  "name\0"  (padding)  01 00 00 00  
Offset 0x38 : 02 00  "description\0"  (padding)  02 00 00 00  
Offset 0x50 : 03 00  "value\0"  (padding)  03 00 00 00  
```

Trois blocs de 24 octets = 72 octets. Mais la zone fait 128 octets. Il reste 56 octets. Peut-être que nos blocs font une taille différente, ou que la zone contient autre chose après les trois descripteurs.

Revenons au header : `field_count = 3`. Nous avions émis l'hypothèse que c'est le nombre de champs par record. Et ici nous trouvons exactement 3 blocs qui ressemblent à des **descripteurs de champs** (un identifiant, un nom, un type). L'hypothèse se renforce.

Ajustons notre observation. En comptant précisément avec ImHex (sélection d'un bloc, lecture de la taille dans la barre de statut), nous mesurons que chaque descripteur fait effectivement 32 octets, pas 24. Avec le Data Inspector :

```
Offset  Taille  Contenu
+0x00   2       ID du champ (u16)
+0x02   2       Type du champ (u16) — 1=string, 2=string, 3=integer ?
+0x04   20      Nom du champ (char[20], null-terminé, paddé de zéros)
+0x18   4       Taille max du champ en octets (u32)
+0x1C   4       Flags ou réservé (u32)
Total : 32 octets
```

Trois descripteurs de 32 octets = 96 octets (0x60). De 0x20 à 0x80, cela fait 96 octets. Les octets restants de 0x80 à 0x9F (32 octets) pourraient être un autre bloc ou du padding jusqu'à `data_offset`.

Les descripteurs occupent exactement 96 octets (3 × 32), ce qui amène directement à l'offset 0x80 (32 + 96 = 128). Comme 128 est déjà un multiple de 32, aucun padding d'alignement n'est nécessaire : `data_offset = 0x80` tombe pile après le dernier descripteur.

### Pattern mis à jour

```cpp
enum FieldType : u16 {
    STRING  = 0x0001,
    TEXT    = 0x0002,
    INTEGER = 0x0003
};

struct FieldDescriptor {
    u16       field_id      [[comment("Identifiant unique du champ")]];
    FieldType field_type    [[comment("Type de données")]];
    char      field_name[20] [[comment("Nom du champ, null-terminé")]];
    u32       max_size      [[comment("Taille maximale en octets")]];
    u32       flags         [[format("hex")]];
};

struct CDB_Header {
    char magic[4]       [[color("FF8844"), comment("Magic 'CDB2'")]];
    u16  version;
    u16  sub_version;
    u32  record_count   [[comment("Nombre d'enregistrements")]];
    u32  data_offset    [[format("hex"), comment("Offset de la zone de données")]];
    u32  unknown_10;
    u32  field_count    [[comment("Nombre de descripteurs de champs")]];
    padding[8];
};

CDB_Header header @ 0x00;  
FieldDescriptor fields[header.field_count] @ 0x20;  
```

Évaluons. L'arbre montre maintenant le header **et** les trois descripteurs de champs avec leurs noms (`name`, `description`, `value`), leurs types et leurs tailles max. La vue hexadécimale est colorisée sur les 32 premiers octets (header) et les 96 suivants (descripteurs). La zone de padding 0x80–0x9F reste non colorisée — c'est normal, nous l'avons identifiée comme du padding.

Mettons à jour nos bookmarks : remplaçons le bookmark `Table intermédiaire` par un bookmark plus précis `Descripteurs de champs (3 × 32 octets)`, et ajoutons un bookmark `Padding d'alignement (0x80–0x9F)` en gris.

---

## Phase 4 — La zone de données (les records)

### Explorer les records

Naviguons à l'offset `0x80` (la valeur de `data_offset`). C'est ici que les enregistrements devraient commencer. Le header nous dit qu'il y en a 5 (`record_count`), et les descripteurs nous disent que chaque record a 3 champs : `name` (string), `description` (text) et `value` (integer).

Observons les octets à partir de 0x80 avec le Data Inspector :

**Offset 0x80** — Les premiers octets ressemblent à un petit header de record : un entier suivi de données. En balayant avec le curseur, nous identifions un pattern :

```
Offset  Observation
0x80    u32 = 1 (identifiant de record ?)
0xA4    Chaîne lisible "Alpha\0" suivie de padding
0xB8    Chaîne plus longue "First entry in the database\0"
0xE0    u32 = 42 (une valeur entière)
0xE4    u32 = 2 (début du record suivant ?)
```

Le record semble avoir une taille fixe. Mesurons : de 0x80 (début du record 1) à 0xC4 (début supposé du record 2), il y a 68 octets. Vérifions en cherchant le troisième record : si les records font 68 octets, le record 3 devrait commencer à `0x80 + 2×68 = 0x80 + 0x88 = 0x108`. Naviguons à 0x108 et vérifions si un identifiant de record (valeur 3) s'y trouve.

Si la vérification échoue, nous ajustons. Si elle réussit, nous avons trouvé la taille des records. En l'occurrence, supposons que la vérification confirme une taille de 68 octets par record.

### Déduire la structure d'un record

Croisons avec les descripteurs de champs :

- Champ `name` : type STRING, `max_size = 20`. → Probablement `char[20]`.  
- Champ `description` : type TEXT, `max_size = 40`. → Probablement `char[40]`.  
- Champ `value` : type INTEGER, `max_size = 4`. → Probablement `u32`.

Total des données : 20 + 40 + 4 = 64 octets. Plus un identifiant de 4 octets en tête = 68 octets. La taille correspond.

### Pattern pour les records

```cpp
struct CDB_Record {
    u32  record_id   [[comment("Identifiant séquentiel")]];
    char name[20]    [[color("FFEE55"), comment("Champ 'name'")]];
    char description[40] [[color("FFEE55"), comment("Champ 'description'")]];
    u32  value       [[comment("Champ 'value'")]];
};
```

Instancions le tableau de records :

```cpp
CDB_Record records[header.record_count] @ header.data_offset;
```

Évaluons. L'arbre montre 5 records, chacun avec son identifiant, son nom, sa description et sa valeur. Les chaînes sont lisibles, les identifiants sont séquentiels (1, 2, 3, 4, 5), les valeurs entières sont plausibles. La vue hexadécimale est maintenant colorisée sur la quasi-totalité du fichier.

---

## Phase 5 — Vérification et découverte du footer

### Vérifier la couverture

Regardons si notre pattern couvre l'intégralité du fichier. Le header fait 32 octets, les descripteurs 96, le padding 32, et les 5 records font 5 × 68 = 340 octets. Total : 32 + 96 + 32 + 340 = 500 octets (0x1F4).

Quelle est la taille du fichier ? ImHex l'affiche dans la barre de statut ou dans **View → Information**. Supposons que le fichier fait 512 octets (0x200). Il reste 12 octets non couverts (offsets 0x1F4–0x1FF).

Naviguons à 0x1F4. Le Data Inspector montre :

```
Offset 0x1F4 : u32 = 0x4F454643  → char[4] = "CFEO" → inversé : "OEFC"
```

Intéressant — cela ressemble à un **magic de fin** (footer magic). En little-endian, les octets `43 46 45 4F` se lisent comme la chaîne `CFEO`. Vérifions : si le magic de début est `CDB2`, le magic de fin pourrait être `2BDC` inversé, ou un identifiant distinct. Quoi qu'il en soit, un magic en fin de fichier est un pattern classique qui permet de vérifier l'intégrité du fichier (le fichier n'a pas été tronqué).

Les octets suivants :

```
Offset 0x1F8 : u32 = 5           → record_count (redondance de vérification)  
Offset 0x1FC : u32 = 0x1F4       → taille des données ou offset du footer  
```

C'est un **footer** de 12 octets :

```cpp
struct CDB_Footer {
    char magic[4]        [[color("FF8844"), comment("Magic de fin")]];
    u32  record_count    [[comment("Copie du nombre de records (vérification)")]];
    u32  data_end_offset [[format("hex"), comment("Offset de fin des données")]];
};

CDB_Footer footer @ 0x1F4;
```

Le fichier est maintenant entièrement couvert par notre pattern.

---

## Phase 6 — Le pattern complet assemblé

Regroupons tout dans un fichier unique :

```cpp
// ============================================================
// ch06_fileformat.hexpat — Format de fichier custom .cdb
// Formation Reverse Engineering — Chapitre 6 (cas pratique)
// ============================================================

#include <std/io.pat>

// ─── Enums ───

enum FieldType : u16 {
    STRING  = 0x0001,
    TEXT    = 0x0002,
    INTEGER = 0x0003
};

// ─── Structures ───

struct CDB_Header {
    char magic[4]       [[color("FF8844"), comment("Magic 'CDB2'")]];
    u16  version        [[comment("Version majeure")]];
    u16  sub_version    [[comment("Version mineure")]];
    u32  record_count   [[comment("Nombre d'enregistrements")]];
    u32  data_offset    [[format("hex"), comment("Offset de la zone de données")]];
    u32  unknown_10     [[comment("À déterminer (toujours 1 ?)")]];
    u32  field_count    [[comment("Nombre de descripteurs de champs")]];
    padding[8]          [[comment("Réservé")]];
};

struct FieldDescriptor {
    u16       field_id    [[comment("Identifiant du champ")]];
    FieldType field_type  [[comment("Type de données")]];
    char      field_name[20] [[comment("Nom du champ")]];
    u32       max_size    [[comment("Taille max en octets")]];
    u32       flags       [[format("hex"), comment("Flags")]];
};

struct CDB_Record {
    u32  record_id      [[comment("ID séquentiel")]];
    char name[20]       [[color("FFEE55"), comment("Champ 'name'")]];
    char description[40][[color("AADDFF"), comment("Champ 'description'")]];
    u32  value          [[comment("Champ 'value'")]];
};

struct CDB_Footer {
    char magic[4]        [[color("FF8844"), comment("Magic de fin")]];
    u32  record_count    [[comment("Vérification du nombre de records")]];
    u32  data_end_offset [[format("hex"), comment("Offset fin de données")]];
};

// ─── Instanciation ───

CDB_Header      header  @ 0x00;  
FieldDescriptor fields[header.field_count] @ 0x20;  
CDB_Record      records[header.record_count] @ header.data_offset;  
CDB_Footer      footer  @ addressof(records) + sizeof(records);  
```

Quelques points à noter sur les dernières lignes.

La position du footer est calculée dynamiquement : `addressof(records)` donne l'offset de début du tableau de records, et `sizeof(records)` donne sa taille totale. Le footer se trouve donc juste après le dernier record. Cette approche est plus robuste que de hardcoder l'offset `0x1F4` — si le fichier contient un nombre différent de records, le footer sera quand même trouvé.

Le champ `unknown_10` du header reste non résolu. C'est normal — en RE, il est fréquent de terminer une analyse avec quelques champs qui résistent à l'interprétation. Nous les avons documentés avec un `[[comment]]` qui signale l'incertitude. L'analyse dynamique du binaire avec GDB (chapitre 11) ou Frida (chapitre 13) pourra éventuellement lever l'ambiguïté en observant comment le programme lit et utilise ce champ.

---

## Phase 7 — Documentation finale

### Bookmarks finaux

Mettons à jour nos bookmarks pour refléter la compréhension finale :

| Bookmark | Offset | Taille | Couleur | Commentaire |  
|---|---|---|---|---|  
| Header CDB2 | 0x00 | 32 | Orange | Version 2.1, 5 records, 3 champs |  
| Descripteurs de champs | 0x20 | 96 | Vert | 3 descripteurs × 32 octets |  
| Padding d'alignement | 0x80 | 32 | Gris | Zéros jusqu'à data_offset |  
| Zone de données (records) | 0x80 | 340 | Jaune | 5 records × 68 octets |  
| Footer CDB2 | 0x1F4 | 12 | Orange | Magic + vérification d'intégrité |

### Sauvegarde du projet

Sauvegardons le tout via **File → Save Project** sous le nom `sample_cdb_analysis.hexproj`. Le projet contient le pattern chargé, les bookmarks et la disposition de l'interface. Sauvegardons aussi le pattern dans `hexpat/ch06_fileformat.hexpat` .

### Scan YARA

En complément, lançons un scan YARA rapide avec `crypto_constants.yar` pour vérifier qu'aucune constante crypto ne se cache dans le fichier. Résultat : aucun match. Le format `.cdb` ne contient pas de chiffrement — cohérent avec le profil d'entropie observé en phase 1.

---

## Bilan méthodologique

Récapitulons la démarche que nous avons suivie et les outils ImHex mobilisés à chaque étape :

| Phase | Action | Outils ImHex |  
|---|---|---|  
| 1 — Reconnaissance | Triage CLI, premier regard, entropie | Vue hex, View → Information |  
| 2 — Header | Exploration octet par octet, hypothèse de structure | Data Inspector, premier `.hexpat` |  
| 3 — Table intermédiaire | Identification d'un pattern répétitif, mesure de taille | Data Inspector, sélection + barre de statut |  
| 4 — Records | Croisement descripteurs/données, validation de la taille | Pattern `.hexpat` avec tableau dynamique |  
| 5 — Footer | Couverture du fichier, découverte d'une structure inattendue | Navigation, Data Inspector |  
| 6 — Assemblage | Pattern complet, placement dynamique | Pattern Editor, évaluation |  
| 7 — Documentation | Bookmarks, sauvegarde, scan YARA | Bookmarks, Project, YARA |

Cette démarche est **transférable** à n'importe quel format de fichier inconnu. Les étapes et leur ordre peuvent varier — parfois vous identifierez les records avant le header, parfois le footer sera votre premier indice — mais le cycle fondamental reste le même : observer, formuler une hypothèse, l'encoder en `.hexpat`, évaluer, ajuster, documenter.

Le fait que nous ayons réalisé cette analyse **sans jamais ouvrir le binaire `fileformat_O0` dans un désassembleur** est significatif. Pour comprendre un format de fichier, le fichier de données lui-même est souvent la meilleure source d'information. Le binaire qui le produit sera utile dans un second temps — au chapitre 25, nous croiserons notre pattern `.hexpat` avec le désassemblage du parser dans Ghidra pour confirmer les champs restés ambigus et comprendre la logique de sérialisation/désérialisation.

---

## Résumé

Ce cas pratique a démontré le workflow complet de cartographie d'un format de fichier inconnu dans ImHex : reconnaissance initiale (entropie, magic bytes, bookmarks exploratoires), exploration progressive avec le Data Inspector, construction itérative d'un pattern `.hexpat` en commençant par le header puis en suivant les pointeurs vers les structures internes, vérification de la couverture totale du fichier, et documentation finale avec bookmarks et sauvegarde en projet. Le pattern produit — une soixantaine de lignes — transforme un blob hexadécimal opaque en une structure navigable, colorisée et documentée. C'est ce pattern qui servira de base au chapitre 25 pour écrire un parser Python indépendant et produire une spécification formelle du format.

---


⏭️ [🎯 Checkpoint : écrire un `.hexpat` complet pour le format `ch23-fileformat`](/06-imhex/checkpoint.md)
