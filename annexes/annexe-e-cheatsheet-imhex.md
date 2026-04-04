🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe E — Cheat sheet ImHex : syntaxe `.hexpat` de référence

> 📎 **Fiche de référence** — Cette annexe documente la syntaxe du langage de patterns `.hexpat` propre à ImHex. Ce langage permet de décrire la structure d'un fichier binaire pour que ImHex le visualise avec des couleurs, des annotations et une décomposition hiérarchique. Elle couvre les types, les structures, les attributs, le contrôle de flux, les fonctions intégrées et les idiomes courants pour le RE de binaires ELF et de formats custom.

---

## Qu'est-ce qu'un fichier `.hexpat` ?

Un fichier `.hexpat` (*hex pattern*) est un script écrit dans le langage de patterns d'ImHex. Il décrit comment interpréter une séquence d'octets bruts en structures de données typées et nommées. Quand vous chargez un `.hexpat` dans ImHex, le logiciel colore automatiquement les régions correspondantes dans la vue hexadécimale, et affiche l'arborescence des champs dans le panneau *Pattern Data*.

Le langage ressemble volontairement au C, avec des extensions spécifiques pour la manipulation binaire. Si vous savez écrire une `struct` en C, vous savez déjà écrire l'essentiel d'un `.hexpat`.

Le workflow typique est itératif : vous écrivez un pattern minimal, vous observez le résultat dans ImHex, vous affinez les types et les tailles, et vous recommencez jusqu'à avoir cartographié l'intégralité du format. C'est exactement le processus décrit dans les chapitres 6 et 25.

---

## 1 — Types de base

### 1.1 — Types entiers

| Type `.hexpat` | Taille | Équivalent C | Signé ? |  
|----------------|--------|--------------|---------|  
| `u8` | 1 octet | `uint8_t` | Non |  
| `u16` | 2 octets | `uint16_t` | Non |  
| `u32` | 4 octets | `uint32_t` | Non |  
| `u64` | 8 octets | `uint64_t` | Non |  
| `u128` | 16 octets | `__uint128_t` | Non |  
| `s8` | 1 octet | `int8_t` | Oui |  
| `s16` | 2 octets | `int16_t` | Oui |  
| `s32` | 4 octets | `int32_t` | Oui |  
| `s64` | 8 octets | `int64_t` | Oui |  
| `s128` | 16 octets | `__int128_t` | Oui |

### 1.2 — Types flottants

| Type `.hexpat` | Taille | Équivalent C |  
|----------------|--------|--------------|  
| `float` | 4 octets | `float` (IEEE 754 simple précision) |  
| `double` | 8 octets | `double` (IEEE 754 double précision) |

### 1.3 — Types caractère et booléen

| Type `.hexpat` | Taille | Description |  
|----------------|--------|-------------|  
| `char` | 1 octet | Caractère ASCII (affiché comme caractère dans le panneau) |  
| `char16` | 2 octets | Caractère UTF-16 |  
| `bool` | 1 octet | Booléen (0 = false, non-zéro = true) |

### 1.4 — Type spécial `padding`

| Type `.hexpat` | Description |  
|----------------|-------------|  
| `padding[N]` | Avance de N octets sans les afficher dans le panneau Pattern Data |

`padding` est extrêmement utile pour sauter les octets de remplissage (padding d'alignement) dans les structures, sans polluer l'arborescence avec des champs sans signification.

---

## 2 — Endianness

Par défaut, ImHex interprète les valeurs multi-octets en **little-endian** (l'ordre natif de x86). Vous pouvez changer l'endianness globalement ou localement.

### 2.1 — Endianness globale

```cpp
#pragma endian little    // Défaut — little-endian pour tout le fichier
#pragma endian big       // Big-endian pour tout le fichier
```

### 2.2 — Endianness locale (par type)

```cpp
le u32 magic;            // Little-endian explicite  
be u32 network_field;    // Big-endian explicite  
```

Les préfixes `le` et `be` s'appliquent à une seule déclaration et prennent le pas sur le `#pragma` global. C'est indispensable pour les formats qui mélangent les deux ordres d'octets (protocoles réseau avec des headers big-endian et des payloads little-endian, par exemple).

---

## 3 — Variables et placement

### 3.1 — Déclaration de variables

Les variables dans un `.hexpat` sont **placées dans le fichier** à la position courante du curseur de lecture. Chaque déclaration avance automatiquement le curseur de la taille du type.

```cpp
u32 magic;          // Lit 4 octets à la position courante, avance de 4  
u16 version;        // Lit 2 octets à la nouvelle position, avance de 2  
u8  flags;          // Lit 1 octet, avance de 1  
```

### 3.2 — Placement explicite avec `@`

L'opérateur `@` place une variable à une adresse absolue dans le fichier, sans modifier la position courante du curseur pour les déclarations suivantes :

```cpp
u32 magic @ 0x00;           // Lit 4 octets à l'offset 0x00  
u16 version @ 0x04;         // Lit 2 octets à l'offset 0x04  
u32 data_offset @ 0x08;     // Lit 4 octets à l'offset 0x08  
```

### 3.3 — Position courante : `$`

La variable spéciale `$` représente la position courante du curseur de lecture dans le fichier. Elle est très utile pour des calculs d'offset et des assertions :

```cpp
u32 header_start = $;       // Sauvegarde la position courante
// ... déclarations ...
u32 header_size = $ - header_start;  // Calcule la taille lue
```

### 3.4 — Placement relatif avec `addressof`

La fonction intégrée `addressof(variable)` retourne l'adresse de début d'une variable déjà déclarée :

```cpp
u32 data_offset;  
u8  data[16] @ addressof(data_offset) + data_offset;  
// Place le tableau 'data' à l'adresse calculée
```

---

## 4 — Tableaux

### 4.1 — Tableaux de taille fixe

```cpp
u8  raw_bytes[16];           // 16 octets bruts  
u32 table[8];                // 8 dwords (32 octets au total)  
char signature[4];           // 4 caractères ASCII (ex : "ELF\x7f")  
```

### 4.2 — Tableaux de taille variable (runtime)

La taille d'un tableau peut être une expression calculée à partir de champs lus précédemment :

```cpp
u32 count;  
u32 entries[count];          // 'count' entrées de 4 octets chacune  
```

```cpp
u16 name_length;  
char name[name_length];      // Chaîne de longueur variable  
```

### 4.3 — Tableaux de taille illimitée avec sentinelle

L'opérateur `while` permet de lire des éléments jusqu'à ce qu'une condition soit remplie. C'est utile pour les listes terminées par un marqueur (sentinel value) :

```cpp
// Lit des u32 jusqu'à rencontrer la valeur 0
u32 entries[while(std::mem::read_unsigned($, 4) != 0x00)];
```

### 4.4 — Tableaux de chaînes C (null-terminated)

Pour les chaînes terminées par un octet nul, utilisez le type `str` fourni par la bibliothèque standard :

```cpp
#include <std/string.pat>

std::string::NullString null_terminated_string @ 0x100;
```

Ou, de manière plus simple avec le type intégré `char[]` et un terminateur nul :

```cpp
char my_string[] @ 0x100;   // Lit jusqu'au premier \0
```

Quand un tableau de `char` est déclaré sans taille explicite, ImHex le lit jusqu'au premier octet nul rencontré.

---

## 5 — Structures (`struct`)

Les structures sont le cœur du langage `.hexpat`. Elles groupent des champs séquentiels en une unité logique nommée, exactement comme en C.

### 5.1 — Déclaration de base

```cpp
struct FileHeader {
    char     magic[4];       // 4 octets : signature du format
    u16      version;        // 2 octets : numéro de version
    u16      flags;          // 2 octets : drapeaux
    u32      entry_count;    // 4 octets : nombre d'entrées
    u32      data_offset;    // 4 octets : offset vers les données
};

FileHeader header @ 0x00;   // Instancie la structure à l'offset 0
```

### 5.2 — Structures imbriquées

```cpp
struct Vec2 {
    float x;
    float y;
};

struct Entity {
    u32   id;
    Vec2  position;
    Vec2  velocity;
    u8    type;
};

Entity entities[10] @ 0x100;  // 10 entités consécutives
```

### 5.3 — Structures avec taille dynamique

Les champs d'une structure peuvent dépendre de valeurs lues précédemment dans la même structure :

```cpp
struct Chunk {
    u32  chunk_type;
    u32  chunk_size;
    u8   data[chunk_size];    // Taille lue dynamiquement
    u32  checksum;
};
```

### 5.4 — Structures avec héritage

Le langage supporte l'héritage simple, similaire au C++ :

```cpp
struct Base {
    u32 type;
    u32 size;
};

struct ExtendedHeader : Base {
    u16 flags;
    u16 version;
    // Les champs type et size de Base sont inclus au début
};
```

---

## 6 — Unions (`union`)

Une union superpose plusieurs interprétations au même emplacement mémoire. Tous les champs d'une union commencent à la même adresse.

```cpp
union Value {
    u32   as_uint;
    s32   as_int;
    float as_float;
    u8    as_bytes[4];
};

Value val @ 0x100;
// Les 4 mêmes octets sont visibles sous 4 interprétations différentes
```

Les unions sont particulièrement utiles pour les champs dont l'interprétation dépend d'un type discriminant lu ailleurs :

```cpp
struct TaggedValue {
    u8 type;
    union {
        u32   integer_val;
        float float_val;
        char  string_val[4];
    } value;
};
```

---

## 7 — Énumérations (`enum`)

### 7.1 — Syntaxe de base

```cpp
enum FileType : u16 {
    EXECUTABLE = 0x0001,
    SHARED_LIB = 0x0002,
    OBJECT     = 0x0003,
    CORE_DUMP  = 0x0004
};

FileType type @ 0x10;   // Affiché par nom si la valeur correspond
```

Le type sous-jacent (ici `u16`) détermine la taille en octets de l'énumération. ImHex affiche le nom symbolique si la valeur correspond à l'un des membres ; sinon, la valeur brute est affichée.

### 7.2 — Utilisation dans les conditions

Les valeurs d'enum peuvent être utilisées dans les expressions conditionnelles (`if`, `match`) pour piloter le parsing :

```cpp
enum SectionType : u32 {
    TEXT   = 0x01,
    DATA   = 0x02,
    BSS    = 0x03,
    CUSTOM = 0xFF
};

struct Section {
    SectionType type;
    u32 size;

    if (type == SectionType::TEXT || type == SectionType::DATA) {
        u8 content[size];
    } else if (type == SectionType::BSS) {
        padding[size];        // BSS n'a pas de contenu dans le fichier
    }
};
```

---

## 8 — Bitfields

Les bitfields permettent de décomposer un entier en champs de bits individuels, ce qui est indispensable pour parser les flags, les registres de contrôle et les champs compactés.

### 8.1 — Syntaxe

```cpp
bitfield ElfFlags {
    executable : 1;
    writable   : 1;
    readable   : 1;
    reserved   : 29;
};

ElfFlags flags @ 0x20;
```

Chaque champ spécifie son nombre de bits. La somme des bits doit correspondre à la taille du type sous-jacent (ici 32 bits implicitement, car 1+1+1+29 = 32).

### 8.2 — Bitfields dans des structures

```cpp
bitfield TcpFlags {
    fin : 1;
    syn : 1;
    rst : 1;
    psh : 1;
    ack : 1;
    urg : 1;
    ece : 1;
    cwr : 1;
};

struct TcpHeader {
    be u16     src_port;
    be u16     dst_port;
    be u32     seq_number;
    be u32     ack_number;
    u8         data_offset_reserved;
    TcpFlags   flags;
    be u16     window_size;
    be u16     checksum;
    be u16     urgent_pointer;
};
```

### 8.3 — Padding dans les bitfields

Utilisez `padding` pour sauter des bits réservés sans leur donner de nom :

```cpp
bitfield StatusRegister {
    carry     : 1;
    zero      : 1;
    sign      : 1;
    overflow  : 1;
    padding   : 4;       // 4 bits réservés, ignorés dans l'affichage
};
```

---

## 9 — Contrôle de flux conditionnel

### 9.1 — `if` / `else`

Le contrôle de flux conditionnel à l'intérieur d'une structure permet de parser différemment selon les valeurs lues :

```cpp
struct Record {
    u8 type;
    u32 size;

    if (type == 0x01) {
        u32 integer_data;
    } else if (type == 0x02) {
        float float_data;
    } else if (type == 0x03) {
        char string_data[size];
    } else {
        u8 raw_data[size];
    }
};
```

### 9.2 — `match` (pattern matching)

L'instruction `match` est une alternative plus lisible à une chaîne de `if`/`else` quand on compare une seule valeur :

```cpp
struct Packet {
    u8 opcode;
    u16 length;

    match (opcode) {
        (0x01): u8 payload_a[length];
        (0x02): u16 payload_b[length / 2];
        (0x03): {
            u32 sub_type;
            u8  payload_c[length - 4];
        }
        (_): u8 unknown_payload[length];   // _ = cas par défaut
    }
};
```

### 9.3 — Boucles

Le langage supporte les boucles `for` et `while` à l'intérieur des structures pour parser des séquences répétitives dont la structure n'est pas un simple tableau homogène :

```cpp
struct FileFormat {
    u32 magic;
    u32 num_chunks;

    // Boucle for pour parser N chunks de taille variable
    for (u32 i = 0, i < num_chunks, i = i + 1) {
        Chunk chunk;
    }
};
```

> ⚠️ **Attention à la syntaxe** : les boucles `for` dans `.hexpat` utilisent des **virgules** (`,`) comme séparateurs entre les clauses d'initialisation, de condition et d'incrémentation, et non des points-virgules (`;`) comme en C.

```cpp
struct NullTerminatedList {
    // Boucle while pour lire jusqu'à un marqueur
    u32 entry;
    while (entry != 0x00000000) {
        u32 entry;
    }
};
```

---

## 10 — Fonctions personnalisées

### 10.1 — Syntaxe

```cpp
fn calculate_checksum(u32 offset, u32 size) {
    u32 sum = 0;
    for (u32 i = 0, i < size, i = i + 1) {
        sum = sum + std::mem::read_unsigned(offset + i, 1);
    }
    return sum & 0xFF;
};
```

Les fonctions peuvent retourner une valeur et accepter des paramètres typés. Elles sont utiles pour factoriser des calculs réutilisés dans plusieurs structures (checksums, offsets relatifs, décodages).

### 10.2 — Fonctions dans les assertions et les attributs

```cpp
fn is_valid_magic(u32 value) {
    return value == 0x7F454C46;  // "\x7fELF"
};

struct ElfHeader {
    u32 magic;
    std::assert(is_valid_magic(magic), "Magic invalide : ce n'est pas un ELF");
    // ... suite du header
};
```

---

## 11 — Attributs

Les attributs modifient le comportement d'affichage ou de parsing d'une variable. Ils se placent après la déclaration, entre doubles crochets `[[...]]`.

### 11.1 — Attributs d'affichage

| Attribut | Description | Exemple |  
|----------|-------------|---------|  
| `[[color("RRGGBB")]]` | Couleur personnalisée pour le champ dans la vue hex | `u32 magic [[color("FF0000")]];` |  
| `[[name("label")]]` | Nom d'affichage alternatif (remplace le nom de variable) | `u32 e_type [[name("Type ELF")]];` |  
| `[[comment("text")]]` | Ajoute un commentaire affiché dans le panneau Pattern Data | `u16 version [[comment("Doit être 2")]];` |  
| `[[format("fmt")]]` | Format d'affichage personnalisé pour la valeur | `u32 addr [[format("0x{:08X}")]];` |  
| `[[hidden]]` | Masque le champ dans le panneau Pattern Data (mais le lit quand même) | `u8 reserved[4] [[hidden]];` |  
| `[[sealed]]` | Empêche le dépliage des sous-champs (structure affichée sur une seule ligne) | `Vec2 pos [[sealed]];` |  
| `[[single_color]]` | Applique une seule couleur à toute la structure (pas de couleurs alternées) | `struct Block { ... } [[single_color]];` |  
| `[[highlight_hidden]]` | Colore le champ dans la vue hex même s'il est `[[hidden]]` | `padding[4] [[highlight_hidden]];` |  
| `[[inline]]` | Affiche les champs de la sous-structure au même niveau que le parent | `Vec2 pos [[inline]];` |

### 11.2 — Attributs de transformation

| Attribut | Description | Exemple |  
|----------|-------------|---------|  
| `[[transform("fn")]]` | Applique une fonction de transformation à la valeur affichée | Voir ci-dessous |  
| `[[pointer_base("fn")]]` | Définit la base pour les champs pointeurs (adresse de base) | `u32 offset [[pointer_base("base_addr")]];` |

L'attribut `[[transform]]` est puissant pour afficher une valeur décodée sans modifier les données sous-jacentes :

```cpp
fn decode_xor(u8 value) {
    return value ^ 0xAA;
};

u8 encoded_byte [[transform("decode_xor")]];
// La vue hex montre l'octet brut, le panneau Pattern Data montre la valeur décodée
```

### 11.3 — Attributs de contrôle du parsing

| Attribut | Description | Exemple |  
|----------|-------------|---------|  
| `[[static]]` | Le champ est évalué à la compilation (constante) | `u32 MAGIC = 0x7F454C46 [[static]];` |  
| `[[no_unique_address]]` | Le champ ne fait pas avancer le curseur (se superpose au précédent) | `u32 alt_view [[no_unique_address]];` |

### 11.4 — Combinaison d'attributs

Plusieurs attributs peuvent être combinés sur un même champ, séparés par des virgules :

```cpp
u32 magic [[color("FF6600"), name("Magic Number"), comment("0x7F454C46 = ELF")]];
```

---

## 12 — Directives de préprocesseur

| Directive | Description | Exemple |  
|-----------|-------------|---------|  
| `#include <file>` | Inclut un fichier de la bibliothèque standard | `#include <std/mem.pat>` |  
| `#include "file.hexpat"` | Inclut un fichier local | `#include "my_types.hexpat"` |  
| `#define NAME value` | Définit une constante de préprocesseur | `#define HEADER_SIZE 64` |  
| `#pragma endian big` | Définit l'endianness globale | `#pragma endian big` |  
| `#pragma base_address 0x400000` | Définit l'adresse de base pour les offsets | `#pragma base_address 0x400000` |  
| `#pragma pattern_limit N` | Augmente la limite de patterns (défaut : 2 millions) | `#pragma pattern_limit 5000000` |  
| `#pragma array_limit N` | Augmente la limite de taille des tableaux | `#pragma array_limit 100000` |

> 💡 Si ImHex affiche une erreur « pattern limit reached » sur un gros fichier, augmentez `#pragma pattern_limit`. Le défaut de 2 millions de patterns est suffisant pour la plupart des formats, mais les ELF avec de grosses tables de symboles peuvent dépasser cette limite.

---

## 13 — Bibliothèque standard (`std::`)

ImHex fournit une bibliothèque standard de fonctions et types accessibles via `#include`. Voici les modules les plus utiles pour le RE.

### 13.1 — `std::mem` — Accès mémoire

```cpp
#include <std/mem.pat>
```

| Fonction | Description |  
|----------|-------------|  
| `std::mem::read_unsigned(offset, size)` | Lit un entier non signé de `size` octets à `offset` |  
| `std::mem::read_signed(offset, size)` | Lit un entier signé |  
| `std::mem::read_string(offset, length)` | Lit une chaîne de `length` octets |  
| `std::mem::find_sequence(offset, bytes...)` | Cherche une séquence d'octets à partir de `offset` |  
| `std::mem::find_string(offset, string)` | Cherche une chaîne à partir de `offset` |  
| `std::mem::size()` | Taille totale du fichier chargé |  
| `std::mem::base_address()` | Adresse de base du fichier |

### 13.2 — `std::string` — Types chaîne

```cpp
#include <std/string.pat>
```

| Type | Description |  
|------|-------------|  
| `std::string::NullString` | Chaîne C null-terminated (taille variable) |  
| `std::string::SizedString<N>` | Chaîne de taille fixe N octets |

### 13.3 — `std::assert` — Assertions et validation

```cpp
#include <std/core.pat>
```

| Fonction | Description |  
|----------|-------------|  
| `std::assert(condition, message)` | Arrête le parsing avec un message d'erreur si la condition est fausse |  
| `std::assert_warn(condition, message)` | Affiche un avertissement sans arrêter le parsing |  
| `std::print("format", args...)` | Affiche un message dans la console ImHex |

Exemple d'utilisation :

```cpp
struct ElfHeader {
    u32 magic;
    std::assert(magic == 0x464C457F,
        "Magic number invalide : attendu 0x7F454C46 (ELF)");

    u8 class;
    std::assert(class == 1 || class == 2,
        "Classe ELF invalide : attendu 1 (32-bit) ou 2 (64-bit)");

    // Si on arrive ici, les assertions sont passées
    u8 data;
    u8 version;
    u8 os_abi;
};
```

### 13.4 — `std::math` — Fonctions mathématiques

```cpp
#include <std/math.pat>
```

| Fonction | Description |  
|----------|-------------|  
| `std::math::min(a, b)` | Minimum de deux valeurs |  
| `std::math::max(a, b)` | Maximum de deux valeurs |  
| `std::math::abs(x)` | Valeur absolue |  
| `std::math::ceil(x)` | Arrondi au supérieur |  
| `std::math::floor(x)` | Arrondi à l'inférieur |  
| `std::math::log2(x)` | Logarithme base 2 |

### 13.5 — `type::` — Types étendus

```cpp
#include <type/magic.pat>
#include <type/guid.pat>
#include <type/ip.pat>
#include <type/time.pat>
```

| Type | Description |  
|------|-------------|  
| `type::Magic<"ELF">` | Vérifie automatiquement que les octets correspondent à la chaîne attendue |  
| `type::GUID` | Parse et affiche un GUID/UUID (16 octets) |  
| `type::IP4Address` | Adresse IPv4 (4 octets, affichée en notation pointée) |  
| `type::IP6Address` | Adresse IPv6 (16 octets) |  
| `type::time32_t` | Timestamp Unix 32 bits (affiché en date lisible) |  
| `type::time64_t` | Timestamp Unix 64 bits |

---

## 14 — Opérateurs

### 14.1 — Opérateurs arithmétiques et logiques

| Opérateur | Description |  
|-----------|-------------|  
| `+`, `-`, `*`, `/`, `%` | Arithmétique |  
| `&`, `\|`, `^`, `~` | ET, OU, XOR, NOT bit à bit |  
| `<<`, `>>` | Décalages bit à bit |  
| `&&`, `\|\|`, `!` | ET, OU, NOT logiques |  
| `==`, `!=`, `<`, `>`, `<=`, `>=` | Comparaisons |

### 14.2 — Opérateurs spécifiques `.hexpat`

| Opérateur | Description | Exemple |  
|-----------|-------------|---------|  
| `@` | Placement à une adresse | `u32 x @ 0x100;` |  
| `$` | Position courante du curseur | `u32 here = $;` |  
| `addressof(var)` | Adresse de début d'une variable | `addressof(header)` |  
| `sizeof(type_or_var)` | Taille en octets d'un type ou d'une variable | `sizeof(u32)` → 4 |  
| `parent` | Référence à la structure parente englobante | `parent.size` |

---

## 15 — Namespaces

Les namespaces permettent d'organiser les types et d'éviter les collisions de noms dans les grands patterns :

```cpp
namespace elf {
    enum Class : u8 {
        ELFCLASS32 = 1,
        ELFCLASS64 = 2
    };

    struct Ident {
        char     magic[4];
        Class    class;
        u8       data;
        u8       version;
        u8       os_abi;
        padding[8];
    };
};

namespace custom_format {
    struct Header {
        u32 magic;
        u16 version;
    };
};

elf::Ident elf_ident @ 0x00;  
custom_format::Header custom_header @ 0x200;  
```

---

## 16 — Patterns courants pour le RE

### 16.1 — Parser un header ELF 64-bit

```cpp
#include <std/core.pat>

enum ElfClass : u8 {
    ELFCLASS32 = 1,
    ELFCLASS64 = 2
};

enum ElfData : u8 {
    ELFDATA2LSB = 1,  // Little-endian
    ELFDATA2MSB = 2   // Big-endian
};

enum ElfType : u16 {
    ET_NONE   = 0,
    ET_REL    = 1,
    ET_EXEC   = 2,
    ET_DYN    = 3,
    ET_CORE   = 4
};

enum ElfMachine : u16 {
    EM_386     = 3,
    EM_ARM     = 40,
    EM_X86_64  = 62,
    EM_AARCH64 = 183
};

struct ElfIdent {
    char       magic[4]    [[comment("Doit être 0x7F 'E' 'L' 'F'")]];
    ElfClass   class;
    ElfData    data;
    u8         version;
    u8         os_abi;
    padding[8]             [[comment("Padding EI_ABIVERSION + réservés")]];
};

struct Elf64Header {
    ElfIdent    ident       [[color("FF6600")]];
    ElfType     type        [[color("0066FF")]];
    ElfMachine  machine;
    u32         version;
    u64         entry       [[comment("Point d'entrée"), format("0x{:016X}")]];
    u64         ph_offset   [[comment("Offset de la Program Header Table")]];
    u64         sh_offset   [[comment("Offset de la Section Header Table")]];
    u32         flags;
    u16         eh_size     [[comment("Taille de cet en-tête")]];
    u16         ph_entry_size;
    u16         ph_num      [[comment("Nombre de program headers")]];
    u16         sh_entry_size;
    u16         sh_num      [[comment("Nombre de section headers")]];
    u16         sh_strndx   [[comment("Index de la section .shstrtab")]];
};

Elf64Header elf_header @ 0x00;
```

### 16.2 — Parser un protocole réseau custom (TLV)

Le pattern TLV (*Type-Length-Value*) est un idiome extrêmement courant dans les protocoles binaires :

```cpp
#pragma endian big     // Les protocoles réseau sont souvent big-endian

enum MessageType : u8 {
    HANDSHAKE  = 0x01,
    AUTH       = 0x02,
    DATA       = 0x03,
    HEARTBEAT  = 0x04,
    DISCONNECT = 0xFF
};

struct TLVMessage {
    MessageType type    [[color("FF0000")]];
    be u16      length  [[color("00FF00")]];

    match (type) {
        (MessageType::HANDSHAKE): {
            u8  protocol_version;
            u32 client_id;
        }
        (MessageType::AUTH): {
            u8   username_len;
            char username[username_len];
            u8   token[32];
        }
        (MessageType::DATA): {
            u8 payload[length];
        }
        (MessageType::HEARTBEAT): {
            u32 timestamp;
            u32 seq_number;
        }
        (_): {
            u8 raw[length];
        }
    }
};

// Parser tout le fichier comme une séquence de messages TLV
TLVMessage messages[while($ < std::mem::size())] @ 0x00;
```

### 16.3 — Format avec table d'offsets (indirect)

Beaucoup de formats binaires utilisent une table d'offsets dans le header qui pointe vers les données réelles :

```cpp
struct EntryHeader {
    u32 name_offset;
    u32 data_offset;
    u32 data_size;
    u16 type;
    u16 flags;
};

struct FileFormat {
    char magic[8]        [[comment("Signature du format")]];
    u32  version;
    u32  entry_count;
    u32  string_table_offset;

    // Table des entrées (séquentielle après le header)
    EntryHeader entries[entry_count];

    // Les données pointées par les offsets ne sont PAS séquentielles
    // On y accède via le placement @
};

FileFormat file_header @ 0x00;

// Accéder aux données d'une entrée spécifique :
// u8 first_entry_data[file_header.entries[0].data_size]
//     @ file_header.entries[0].data_offset;
```

### 16.4 — Décodage XOR simple

```cpp
fn xor_decode(u8 value) {
    return value ^ 0x37;
};

struct ObfuscatedString {
    u8 length;
    u8 data[length] [[transform("xor_decode"),
                       comment("XOR 0x37 pour décoder")]];
};

ObfuscatedString secret @ 0x200;
```

### 16.5 — Constantes magiques crypto (AES S-box partielle)

```cpp
// Vérifier la présence de la S-box AES dans un binaire
// Les 16 premiers octets de la S-box AES sont :
// 63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76

fn check_aes_sbox(u32 offset) {
    return std::mem::read_unsigned(offset, 1) == 0x63 &&
           std::mem::read_unsigned(offset + 1, 1) == 0x7C &&
           std::mem::read_unsigned(offset + 2, 1) == 0x77 &&
           std::mem::read_unsigned(offset + 3, 1) == 0x7B;
};

// Si vous connaissez l'offset de la S-box :
u8 aes_sbox[256] @ 0x402000 [[color("FF00FF"), comment("AES S-box")]];
```

---

## 17 — Débogage d'un pattern `.hexpat`

Quand un pattern ne fonctionne pas comme prévu, voici les techniques de débogage les plus utiles.

**Utiliser `std::print`** pour afficher des valeurs intermédiaires dans la console ImHex :

```cpp
#include <std/io.pat>

struct Debug {
    u32 magic;
    std::print("Magic lu : 0x{:08X} à l'offset {}", magic, $ - 4);

    u32 size;
    std::print("Size : {} octets", size);
};
```

**Vérifier la position du curseur** avec `$` à chaque étape critique :

```cpp
struct MyStruct {
    u32 field_a;
    std::print("Position après field_a : 0x{:X}", $);
    u16 field_b;
    std::print("Position après field_b : 0x{:X}", $);
};
```

**Utiliser `std::assert`** pour valider les invariants au fur et à mesure :

```cpp
struct Chunk {
    u32 size;
    std::assert(size < 0x10000000, "Taille de chunk suspecte (> 256 Mo)");
    std::assert($ + size <= std::mem::size(), "Chunk dépasse la fin du fichier");
    u8 data[size];
};
```

**Commencer minimal** : parsez d'abord uniquement le header avec des types de base, vérifiez que les valeurs sont cohérentes, puis ajoutez les structures secondaires une par une. N'essayez jamais de parser un format complet du premier coup.

**Consulter le panneau Console** dans ImHex (View → Console) : les erreurs de parsing et les messages de `std::print` y sont affichés.

---

## 18 — Aide-mémoire : syntaxe condensée

```
╔══════════════════════════════════════════════════════════════════╗
║                   .HEXPAT — SYNTAXE RAPIDE                       ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                  ║
║  TYPES DE BASE                                                   ║
║  u8 u16 u32 u64 u128    s8 s16 s32 s64 s128                      ║
║  float double   char char16   bool   padding[N]                  ║
║                                                                  ║
║  ENDIANNESS                                                      ║
║  #pragma endian big/little   |   be u32 x;   le u16 y;           ║
║                                                                  ║
║  PLACEMENT                                                       ║
║  Type name @ 0xADDR;        $ = position courante                ║
║  addressof(var)              sizeof(type)                        ║
║                                                                  ║
║  STRUCTURE           UNION              ENUM                     ║
║  struct S {          union U {          enum E : u16 {           ║
║    u32 a;              u32 as_int;        A = 0x01,              ║
║    u16 b;              float as_f;        B = 0x02               ║
║  };                  };                 };                       ║
║                                                                  ║
║  BITFIELD                                                        ║
║  bitfield Flags {                                                ║
║    read : 1;  write : 1;  exec : 1;  padding : 5;                ║
║  };                                                              ║
║                                                                  ║
║  TABLEAUX                                                        ║
║  u8 data[16];             // taille fixe                         ║
║  u8 data[count];          // taille variable                     ║
║  char str[];              // null-terminated                     ║
║  T arr[while(cond)];      // sentinelle                          ║
║                                                                  ║
║  CONTRÔLE DE FLUX (dans struct)                                  ║
║  if (expr) { ... } else { ... }                                  ║
║  match (val) { (0x01): ...; (_): ...; }                          ║
║  for (init, cond, incr) { ... }    ← virgules, pas ;             ║
║                                                                  ║
║  ATTRIBUTS                                                       ║
║  [[color("RRGGBB")]]    [[name("label")]]    [[hidden]]          ║
║  [[comment("text")]]    [[transform("fn")]]  [[sealed]]          ║
║  [[format("0x{:X}")]]   [[inline]]           [[no_unique_addr]]  ║
║                                                                  ║
║  BIBLIOTHÈQUE STANDARD                                           ║
║  #include <std/mem.pat>     std::mem::read_unsigned(off, sz)     ║
║  #include <std/core.pat>    std::assert(cond, msg)               ║
║  #include <std/io.pat>      std::print("fmt", args)              ║
║  #include <std/string.pat>  std::string::NullString              ║
║                                                                  ║
║  PRAGMAS                                                         ║
║  #pragma endian big/little                                       ║
║  #pragma base_address 0x400000                                   ║
║  #pragma pattern_limit 5000000                                   ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
```

---

> 📚 **Pour aller plus loin** :  
> - **Chapitre 6** — [ImHex : analyse hexadécimale avancée](/06-imhex/README.md) — couverture pédagogique progressive d'ImHex et du langage `.hexpat`.  
> - **Chapitre 25** — [Reverse d'un format de fichier custom](/25-fileformat/README.md) — cas pratique complet de cartographie d'un format inconnu avec `.hexpat`.  
> - **Annexe F** — [Table des sections ELF et leurs rôles](/annexes/annexe-f-sections-elf.md) — référence complémentaire pour écrire des patterns ELF.  
> - **Annexe J** — [Constantes magiques crypto courantes](/annexes/annexe-j-constantes-crypto.md) — les séquences d'octets à chercher avec ImHex dans un binaire crypto.  
> - **Documentation ImHex** — [https://docs.werwolv.net/pattern-language/](https://docs.werwolv.net/pattern-language/) — référence officielle complète du langage de patterns.  
> - **Dépôt de patterns communautaires** — [https://github.com/WerWolv/ImHex-Patterns](https://github.com/WerWolv/ImHex-Patterns) — collection de `.hexpat` prêts à l'emploi pour de nombreux formats.

⏭️ [Table des sections ELF et leurs rôles](/annexes/annexe-f-sections-elf.md)
