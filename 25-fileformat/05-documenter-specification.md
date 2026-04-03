🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 25.5 — Documenter le format (produire une spécification)

> 🎯 **Objectif de cette section** : transformer toutes les connaissances accumulées au fil du chapitre en un document de spécification autonome. Ce document doit permettre à un développeur tiers d'implémenter un parser/sérialiseur CFR complet sans jamais toucher au binaire original ni lire notre code Python.

---

## Pourquoi une spécification formelle ?

On dispose déjà d'un pattern `.hexpat` commenté et d'un parser Python fonctionnel. Pourquoi investir du temps supplémentaire dans un document texte ?

Parce que le code et le pattern décrivent *comment* traiter le format, pas *ce qu'il est*. Un développeur qui lirait notre `cfr_parser.py` devrait reconstituer mentalement la spécification à partir des appels `struct.unpack`, des constantes éparpillées et de la logique de contrôle. C'est exactement le travail de reverse engineering qu'on veut éviter à la personne suivante.

Une bonne spécification est **déclarative** : elle décrit la structure des données indépendamment de toute implémentation. Elle répond à des questions que le code ne pose pas explicitement — « que se passe-t-il si ce champ vaut zéro ? », « cet alignement est-il garanti ? », « quelle version du format cette spec couvre-t-elle ? ». Elle est aussi le livrable le plus utile en contexte professionnel : un rapport d'audit de format, une documentation d'interopérabilité, ou une contribution à un projet d'archivage numérique prennent la forme d'un document, pas d'un script.

---

## Anatomie d'une bonne spécification de format

Les spécifications de formats binaires les plus respectées (PNG, ELF, ZIP, Protocol Buffers encoding…) partagent une structure commune. On va s'en inspirer pour produire un document complet, même si notre format CFR est infiniment plus simple.

Les sections essentielles sont :

1. **Résumé et portée** — ce que le format fait, ce que le document couvre, le numéro de version.  
2. **Conventions et notations** — endianness, unités, notation des tailles, terminologie.  
3. **Vue d'ensemble de la structure** — un diagramme ou un schéma montrant l'agencement global (header → records → footer).  
4. **Description champ par champ** — chaque structure détaillée avec offset, taille, type, valeurs possibles, contraintes.  
5. **Algorithmes** — les CRC, la transformation XOR, et tout processus non trivial.  
6. **Contraintes de validation** — les invariants qu'un fichier conforme doit respecter.  
7. **Cas limites et comportements optionnels** — footer absent, records vides, types inconnus.  
8. **Historique des versions** — si plusieurs versions du format existent.  
9. **Annexes** — exemples hexadécimaux annotés, implémentation de référence.

---

## Conventions de rédaction

Avant de rédiger, établissons quelques conventions qui rendront le document sans ambiguïté.

### Le vocabulaire RFC 2119

Les spécifications techniques utilisent conventionnellement les mots-clés définis par la RFC 2119 pour exprimer les niveaux d'obligation :

- **MUST** / **DOIT** — obligation absolue. Un fichier qui viole cette règle n'est pas conforme.  
- **SHOULD** / **DEVRAIT** — recommandation forte. La violation est possible mais doit être justifiée.  
- **MAY** / **PEUT** — comportement optionnel. Un parser conforme doit tolérer la présence ou l'absence de cette caractéristique.

Par exemple : « Le champ `magic` DOIT contenir la valeur `0x4346524D`. » ne laisse aucune place à l'interprétation. « Le champ `record.flags` DEVRAIT être mis à zéro par les producteurs » signifie qu'un parser doit tolérer des valeurs non nulles.

### La notation des octets

On adopte les conventions suivantes :

- Les offsets sont en hexadécimal, préfixés `0x` : `0x00`, `0x10`, `0x1C`.  
- Les valeurs constantes sont en hexadécimal : `0x4346524D`, `0xEDB88320`.  
- Les tailles sont en octets sauf mention contraire.  
- Les types entiers suivent la notation `uint16_le` (entier non signé, 16 bits, little-endian).  
- Les chaînes sont encodées en ASCII, **non** null-terminées sauf mention contraire.

---

## La spécification CFR

Ce qui suit est le document de spécification complet tel qu'on le produirait comme livrable final du reverse. Dans un contexte réel, ce document serait un fichier séparé (par exemple `CFR_FORMAT_SPEC.md`), versionné et maintenu indépendamment du code.

---

### 1. Résumé

Le format CFR (*Custom Format Records*) est un format d'archive binaire permettant de stocker plusieurs enregistrements nommés dans un fichier unique. Chaque enregistrement possède un type (texte, binaire ou métadonnées), un nom de longueur variable et un payload de longueur variable. Le format supporte optionnellement une obfuscation XOR des données et inclut des mécanismes d'intégrité à trois niveaux (header, record, global).

Ce document décrit la version **2** (`0x0002`) du format.

### 2. Conventions

| Élément | Convention |  
|---------|------------|  
| Ordre des octets | Little-endian pour tous les champs multi-octets |  
| Encodage des chaînes | ASCII, non null-terminées (la longueur est explicite) |  
| Alignement | Aucun. Les champs sont contigus sans padding entre structures |  
| Offsets | Hexadécimal, relatifs au début de la structure décrite |  
| Mots-clés | DOIT, DEVRAIT, PEUT — au sens de la RFC 2119 |

### 3. Vue d'ensemble

Un fichier CFR est composé de trois parties consécutives :

```
┌─────────────────────────────────┐  offset 0x00
│           HEADER (32 octets)    │
├─────────────────────────────────┤  offset 0x20
│         RECORD 0                │
│  ┌────────────────────────────┐ │
│  │ Record Header   (8 octets) │ │
│  │ Name     (name_len octets) │ │
│  │ Data     (data_len octets) │ │
│  │ CRC-16          (2 octets) │ │
│  └────────────────────────────┘ │
├─────────────────────────────────┤
│         RECORD 1                │
│             ...                 │
├─────────────────────────────────┤
│         RECORD N-1              │
├─────────────────────────────────┤
│     FOOTER (12 octets)          │  ← optionnel
│     (présent si flags.bit1 = 1) │
└─────────────────────────────────┘
```

Les records se succèdent sans padding entre eux. Le footer, s'il est présent, suit immédiatement le dernier record.

### 4. Header

**Taille fixe** : 32 octets.

| Offset | Taille | Type | Nom | Description |  
|--------|--------|------|-----|-------------|  
| `0x00` | 4 | `char[4]` | `magic` | DOIT valoir `"CFRM"` (`0x43 0x46 0x52 0x4D`). Identifie le format. |  
| `0x04` | 2 | `uint16_le` | `version` | Version du format. Ce document décrit la version `0x0002`. |  
| `0x06` | 2 | `uint16_le` | `flags` | Champ de bits (cf. section 4.1). |  
| `0x08` | 4 | `uint32_le` | `num_records` | Nombre d'enregistrements dans l'archive. DOIT être ≤ 1024. |  
| `0x0C` | 4 | `uint32_le` | `timestamp` | Date de création de l'archive, en secondes depuis l'epoch UNIX (1er janvier 1970 00:00:00 UTC). |  
| `0x10` | 4 | `uint32_le` | `header_crc` | CRC-32 des octets `[0x00..0x0F]` (les 16 premiers octets du header). Cf. section 7.1. |  
| `0x14` | 8 | `char[8]` | `author` | Identifiant de l'auteur. Complété à droite par des octets nuls (`0x00`) si inférieur à 8 caractères. |  
| `0x1C` | 4 | `uint32_le` | `data_len_xor` | XOR de tous les champs `data_len` des records de l'archive. Cf. section 7.4. |

#### 4.1 Champ `flags`

| Bit | Nom | Description |  
|-----|-----|-------------|  
| 0 | `XOR_ENABLED` | Si actif (1), les données de chaque record sont obfusquées par XOR rotatif (cf. section 7.3). Les noms ne sont PAS transformés. |  
| 1 | `HAS_FOOTER` | Si actif (1), un footer de 12 octets est présent en fin de fichier (cf. section 6). |  
| 2–15 | Réservés | DOIVENT être mis à zéro par les producteurs. Les parseurs DEVRAIENT les ignorer. |

### 5. Record

Chaque record se compose de quatre parties consécutives :

#### 5.1 Record Header

**Taille fixe** : 8 octets.

| Offset | Taille | Type | Nom | Description |  
|--------|--------|------|-----|-------------|  
| `0x00` | 1 | `uint8` | `type` | Type de contenu (cf. section 5.2). |  
| `0x01` | 1 | `uint8` | `flags` | Réservé. DEVRAIT être `0x00`. Les parseurs DOIVENT l'ignorer. |  
| `0x02` | 2 | `uint16_le` | `name_len` | Longueur du nom en octets. PEUT être zéro. |  
| `0x04` | 4 | `uint32_le` | `data_len` | Longueur du payload en octets. PEUT être zéro. |

#### 5.2 Types de record

| Valeur | Nom | Sémantique |  
|--------|-----|------------|  
| `0x01` | `TEXT` | Contenu textuel (UTF-8 ou ASCII). |  
| `0x02` | `BINARY` | Données binaires arbitraires. |  
| `0x03` | `META` | Métadonnées au format `clé=valeur`, une paire par ligne (`\n`). |

Les parseurs DEVRAIENT accepter les valeurs de type inconnues sans erreur et traiter le payload comme des données binaires opaques.

#### 5.3 Name

- Taille : `name_len` octets.  
- Encodage : ASCII.  
- N'est PAS null-terminé.  
- N'est JAMAIS soumis à la transformation XOR, même si le flag `XOR_ENABLED` est actif.  
- PEUT être vide (`name_len = 0`).

#### 5.4 Data

- Taille : `data_len` octets.  
- Si le flag `XOR_ENABLED` du header est actif et `data_len > 0`, les octets stockés sont le résultat de la transformation XOR (cf. section 7.3) appliquée aux données originales.  
- Si le flag `XOR_ENABLED` est inactif, les données sont stockées en clair.  
- PEUT être vide (`data_len = 0`), auquel cas aucune transformation n'est appliquée.

#### 5.5 CRC-16 du record

- Taille : 2 octets (`uint16_le`).  
- Algorithme : CRC-16/CCITT (cf. section 7.2).  
- Entrée du calcul : concaténation `name || data_original`, où `data_original` désigne les données **avant** la transformation XOR.  
- Ce CRC protège à la fois le nom et le contenu de l'enregistrement.

**Ordre des opérations pour un producteur** :

1. Calculer `crc16 = CRC-16(name || data_original)`.  
2. Si `XOR_ENABLED` : transformer `data_stored = XOR(data_original)`.  
3. Écrire : `record_header || name || data_stored || crc16`.

**Ordre des opérations pour un parseur** :

1. Lire `record_header`, `name`, `data_stored`, `crc16`.  
2. Si `XOR_ENABLED` : restaurer `data_original = XOR(data_stored)`.  
3. Vérifier : `CRC-16(name || data_original) == crc16`.

### 6. Footer

**Présence** : uniquement si `flags.HAS_FOOTER = 1`.

**Taille fixe** : 12 octets.

**Position** : immédiatement après le dernier record.

| Offset | Taille | Type | Nom | Description |  
|--------|--------|------|-----|-------------|  
| `0x00` | 4 | `char[4]` | `magic` | DOIT valoir `"CRFE"` (`0x43 0x52 0x46 0x45`). |  
| `0x04` | 4 | `uint32_le` | `total_size` | Taille totale du fichier en octets (header + records + footer). |  
| `0x08` | 4 | `uint32_le` | `global_crc` | CRC-32 de tous les octets précédant le footer (offsets `[0x00..total_size - 13]`). Cf. section 7.1. |

Le footer permet de détecter les fichiers tronqués (via `total_size`) et la corruption globale (via `global_crc`).

### 7. Algorithmes

#### 7.1 CRC-32

Utilisé pour `header_crc` et `global_crc`.

| Paramètre | Valeur |  
|-----------|--------|  
| Polynôme | `0xEDB88320` (forme réfléchie de `0x04C11DB7`) |  
| Valeur initiale | `0xFFFFFFFF` |  
| XOR final | `0xFFFFFFFF` |  
| Réflexion des bits d'entrée | Oui |  
| Réflexion du CRC final | Oui |

C'est la variante CRC-32/ISO-HDLC, identique à celle utilisée par `zlib`, `gzip`, `binascii.crc32()` en Python, et l'ethernet FCS.

**Pseudo-code** :

```
function crc32(data):
    crc ← 0xFFFFFFFF
    for each byte b in data:
        crc ← crc XOR b
        repeat 8 times:
            if crc AND 1:
                crc ← (crc >> 1) XOR 0xEDB88320
            else:
                crc ← crc >> 1
    return crc XOR 0xFFFFFFFF
```

**Portée par champ** :

| Champ | Octets couverts |  
|-------|-----------------|  
| `header_crc` | Octets `[0x00..0x0F]` du header (16 premiers octets : magic, version, flags, num_records, timestamp) |  
| `global_crc` | Tous les octets du fichier précédant le footer (header + totalité des records) |

#### 7.2 CRC-16

Utilisé pour le `crc16` de chaque record.

| Paramètre | Valeur |  
|-----------|--------|  
| Polynôme | `0x1021` |  
| Valeur initiale | `0x1D0F` |  
| XOR final | `0x0000` (pas de XOR final) |  
| Réflexion des bits d'entrée | Non |  
| Réflexion du CRC final | Non |

Cette variante diffère du CRC-16/CCITT-FALSE standard uniquement par sa valeur initiale (`0x1D0F` au lieu de `0xFFFF`). Elle correspond au CRC-16/AUG-CCITT défini dans le catalogue de Greg Cook.

**Pseudo-code** :

```
function crc16(data):
    crc ← 0x1D0F
    for each byte b in data:
        crc ← crc XOR (b << 8)
        repeat 8 times:
            if crc AND 0x8000:
                crc ← (crc << 1) XOR 0x1021
            else:
                crc ← crc << 1
            crc ← crc AND 0xFFFF
    return crc
```

#### 7.3 Transformation XOR

Appliquée aux données des records lorsque `flags.XOR_ENABLED = 1`.

| Paramètre | Valeur |  
|-----------|--------|  
| Clé | `0x5A 0x3C 0x96 0xF1` (4 octets, fixe) |  
| Mode | Rotatif (l'octet `data[i]` est XOR-é avec `key[i mod 4]`) |

La transformation est involutive : appliquer la fonction deux fois produit les données originales.

**Pseudo-code** :

```
KEY = [0x5A, 0x3C, 0x96, 0xF1]

function xor_transform(data):
    result ← copy of data
    for i from 0 to length(data) - 1:
        result[i] ← data[i] XOR KEY[i mod 4]
    return result
```

**Remarques** :

- La transformation n'est PAS appliquée aux noms des records.  
- Si `data_len = 0`, aucune transformation n'est nécessaire.  
- L'index du XOR est réinitialisé à zéro au début de chaque record (la clé ne « continue » pas d'un record à l'autre).

#### 7.4 Vérification `data_len_xor`

Le champ `data_len_xor` du header est le XOR de tous les champs `data_len` des records de l'archive :

```
data_len_xor = record[0].data_len XOR record[1].data_len XOR ... XOR record[N-1].data_len
```

Ce champ offre une vérification rapide de la cohérence des tailles sans nécessiter la lecture complète des données. Si un seul `data_len` est corrompu, le XOR global sera invalide.

### 8. Contraintes de validation

Un fichier CFR DOIT respecter les invariants suivants pour être considéré conforme :

| # | Invariant | Conséquence en cas de violation |  
|---|-----------|-------------------------------|  
| V1 | `header.magic == "CFRM"` | Rejet immédiat (pas un fichier CFR). |  
| V2 | `header.num_records ≤ 1024` | Rejet (protection contre l'allocation excessive). |  
| V3 | `header.header_crc == CRC-32(header[0x00..0x0F])` | Le header est corrompu. |  
| V4 | Pour chaque record : `name_len + data_len` ne DOIT PAS dépasser la taille restante du fichier. | Record tronqué. |  
| V5 | Pour chaque record : `CRC-16(name \|\| data_original) == stored_crc16` | Les données du record sont corrompues. |  
| V6 | `header.data_len_xor == XOR(data_len[0], ..., data_len[N-1])` | Incohérence des tailles de records. |  
| V7 | Si `HAS_FOOTER` : `footer.magic == "CRFE"` | Footer absent ou corrompu. |  
| V8 | Si `HAS_FOOTER` : `footer.total_size == taille réelle du fichier` | Fichier tronqué ou étendu. |  
| V9 | Si `HAS_FOOTER` : `footer.global_crc == CRC-32(fichier[0..total_size - 13])` | Corruption globale. |

Un parseur strict DOIT vérifier tous ces invariants. Un parseur tolérant PEUT ignorer les violations V3, V6 et V9 avec un avertissement.

### 9. Cas limites

| Cas | Comportement attendu |  
|-----|---------------------|  
| `num_records = 0` | Archive vide. Le fichier contient uniquement le header (et éventuellement le footer). Valide. |  
| `name_len = 0` | Record avec un nom vide. Le champ `name` est absent (0 octets). Le CRC-16 est calculé sur les données seules. Valide. |  
| `data_len = 0` | Record sans données. Le champ `data` est absent (0 octets). Aucune transformation XOR. Le CRC-16 est calculé sur le nom seul. Valide. |  
| `name_len = 0` ET `data_len = 0` | Record entièrement vide. Le CRC-16 est calculé sur un buffer de 0 octets : `CRC-16("") = 0x1D0F`. Valide. |  
| `type` inconnu (> `0x03`) | Le parseur DEVRAIT accepter le record et traiter le payload comme binaire opaque. |  
| `flags.HAS_FOOTER = 0` | Le fichier se termine après le dernier record. Aucun footer n'est présent ni attendu. |  
| `flags.HAS_FOOTER = 1` mais fichier tronqué | Le parseur DOIT signaler une erreur. |  
| `header.version ≠ 0x0002` | Le parseur PEUT tenter de lire le fichier mais DEVRAIT émettre un avertissement. |

### 10. Historique des versions

| Version | Identifiant | Changements |  
|---------|-------------|-------------|  
| 1 | `0x0001` | Version initiale (non documentée, supposée obsolète). |  
| 2 | `0x0002` | Version décrite dans ce document. Ajout du footer optionnel, du champ `data_len_xor`, et de la transformation XOR. |

### 11. Annexe — Exemple annoté

Voici les 48 premiers octets de l'archive `demo.cfr`, annotés champ par champ :

```
Offset   Hex                                      Champ
───────  ───────────────────────────────────────   ─────────────────────────
                          HEADER
0x00     43 46 52 4D                               magic = "CFRM"
0x04     02 00                                     version = 2
0x06     02 00                                     flags = 0x0002
                                                     bit 0 (XOR_ENABLED) = 0
                                                     bit 1 (HAS_FOOTER)  = 1
0x08     04 00 00 00                               num_records = 4
0x0C     XX XX XX XX                               timestamp (variable)
0x10     XX XX XX XX                               header_crc
0x14     XX XX XX XX XX XX XX XX                   author (8 octets)
0x1C     XX XX XX XX                               data_len_xor

                       RECORD 0
0x20     01                                        type = TEXT (0x01)
0x21     00                                        flags = 0x00
0x22     0C 00                                     name_len = 12
0x24     40 00 00 00                               data_len = 64
0x28     67 72 65 65 74 69 6E 67 2E 74 78 74      name = "greeting.txt"
0x34     [64 octets de données en clair]           data (XOR inactif dans demo.cfr)
0x74     XX XX                                     crc16
                                                   (sur "greeting.txt" || data)

                       RECORD 1
0x76     02                                        type = BINARY (0x02)
0x77     00                                        flags = 0x00
0x78     08 00                                     name_len = 8
0x7A     18 00 00 00                               data_len = 24
0x7E     64 61 74 61 2E 62 69 6E                   name = "data.bin"
0x86     [24 octets de données binaires]           data
0x9E     XX XX                                     crc16
         ...
```

---

## Bonnes pratiques de rédaction

Quelques réflexions tirées de ce travail de documentation, applicables à tout reverse de format.

**Préciser ce qui n'est pas dit.** Le silence d'une spécification est ambigu. Si les noms ne sont pas null-terminés, dites-le explicitement — un lecteur habitué au C pourrait supposer le contraire. Si l'alignement est absent, dites-le aussi. Les « Notes » et « Remarques » en italique dans les sections d'algorithme servent à lever ces ambiguïtés.

**Séparer la structure des algorithmes.** La section 5.5 décrit *où* se trouve le CRC-16 et *sur quoi* il porte. La section 7.2 décrit *comment* le calculer. Cette séparation permet à un lecteur pressé de comprendre la structure sans se noyer dans les détails algorithmiques, et à un implémenteur de retrouver rapidement le pseudo-code dont il a besoin.

**Documenter l'ordre des opérations.** Pour le CRC-16 et le XOR, l'ordre est critique et contre-intuitif (le CRC porte sur les données *avant* XOR, pas après). La section 5.5 détaille l'ordre pour le producteur *et* pour le parseur, parce que les deux points de vue ne sont pas symétriques et qu'une erreur dans l'un ou l'autre produit des résultats différents.

**Inclure les cas limites.** La section 9 est souvent la plus utile pour un implémenteur. C'est là que se cachent les bugs : un `data_len = 0` qui transforme un XOR en no-op, un `name_len = 0` qui produit un CRC calculé sur un buffer vide. Ces cas sont rarement couverts par les exemples mais systématiquement rencontrés en production.

**Versionner le document.** Le format peut évoluer. Indiquer clairement quelle version est décrite (ici : version 2, identifiant `0x0002`) permet de maintenir plusieurs versions de la spec sans confusion.

**Fournir un exemple annoté.** Un dump hexadécimal annoté vaut mille mots. Il permet au lecteur de vérifier sa compréhension en suivant les octets du doigt. C'est aussi le premier test d'un nouvel implémenteur : parser l'exemple à la main et vérifier que chaque champ est lu correctement.

---

## Les trois livrables du chapitre

Avec cette spécification, les trois livrables du chapitre sont complets :

| Livrable | Fichier | Rôle |  
|----------|---------|------|  
| Pattern ImHex | `hexpat/ch25_fileformat.hexpat` | Visualisation et inspection interactive des archives CFR dans ImHex. Colorisation de chaque champ, annotations et commentaires. |  
| Parser Python | `scripts/cfr_parser.py` | Lecture, écriture, validation et round-trip programmatique. CLI autonome. |  
| Spécification | `docs/CFR_FORMAT_SPEC.md` | Document autonome décrivant le format indépendamment de toute implémentation. Permet à un tiers de créer son propre parser sans accès au binaire. |

Ces trois livrables se complètent : le pattern permet l'exploration visuelle, le parser prouve la compréhension par le code, et la spécification pérennise le savoir. Si le binaire original disparaît demain, la spécification et le parser suffisent à recréer un outil compatible.

---


⏭️ [🎯 Checkpoint : produire un parser Python + un `.hexpat` + une spec du format](/25-fileformat/checkpoint.md)
