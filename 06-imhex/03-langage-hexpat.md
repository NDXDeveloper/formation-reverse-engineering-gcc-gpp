🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.3 — Le langage de patterns `.hexpat` — syntaxe et types de base

> 🎯 **Objectif de cette section** : Maîtriser les fondements du langage `.hexpat` — types primitifs, structures, énumérations, tableaux, conditionnels et variables spéciales — pour être capable d'écrire des patterns de parsing simples à intermédiaires sur n'importe quel fichier binaire.

---

## Philosophie du langage

Le langage `.hexpat` (pour *hex pattern*) est un langage déclaratif à syntaxe C dont le but est de **décrire la disposition des données dans un fichier binaire**. Quand vous écrivez un pattern, vous ne programmez pas un comportement — vous décrivez une structure. ImHex se charge ensuite de plaquer cette description sur les octets du fichier, d'extraire les valeurs, de les afficher dans l'arbre Pattern Data et de coloriser les régions correspondantes dans la vue hexadécimale.

Si vous savez écrire une `struct` en C, vous savez déjà écrire 80 % d'un pattern `.hexpat`. Le langage reprend volontairement la syntaxe C pour minimiser la courbe d'apprentissage des développeurs et reverse engineers, qui manipulent quotidiennement des structures C. Les différences avec le C portent sur des aspects spécifiques au parsing binaire : placement explicite en mémoire, attributs de formatage, types à taille variable, et quelques constructions absentes du C standard.

---

## Types primitifs

Les types primitifs de `.hexpat` correspondent aux types d'entiers et de flottants que l'on retrouve dans les fichiers binaires. Ils sont nommés de manière explicite, sans ambiguïté sur la taille.

### Entiers

| Type `.hexpat` | Taille | Signé | Équivalent C |  
|---|---|---|---|  
| `u8` | 1 octet | Non | `uint8_t` |  
| `s8` | 1 octet | Oui | `int8_t` |  
| `u16` | 2 octets | Non | `uint16_t` |  
| `s16` | 2 octets | Oui | `int16_t` |  
| `u32` | 4 octets | Non | `uint32_t` |  
| `s32` | 4 octets | Oui | `int32_t` |  
| `u64` | 8 octets | Non | `uint64_t` |  
| `s64` | 8 octets | Oui | `int64_t` |  
| `u128` | 16 octets | Non | `__uint128_t` |  
| `s128` | 16 octets | Oui | `__int128_t` |

### Flottants

| Type `.hexpat` | Taille | Équivalent C |  
|---|---|---|  
| `float` | 4 octets | `float` (IEEE 754) |  
| `double` | 8 octets | `double` (IEEE 754) |

### Caractères et booléens

| Type `.hexpat` | Taille | Description |  
|---|---|---|  
| `char` | 1 octet | Caractère ASCII, affiché comme caractère dans l'arbre |  
| `char16` | 2 octets | Caractère UTF-16 |  
| `bool` | 1 octet | Affiché comme `true` (≠ 0) ou `false` (= 0) |

### Type spécial : `padding`

Le type `padding` consomme des octets sans les afficher dans l'arbre de résultats. Il sert à sauter des zones de remplissage ou des champs réservés que vous ne voulez pas encombrer dans l'affichage :

```cpp
padding[4];   // saute 4 octets sans les afficher
```

---

## Endianness

Par défaut, ImHex interprète les types multi-octets en **little-endian**, ce qui correspond au comportement natif des architectures x86/x86-64 sur lesquelles nous travaillons. Si vous devez parser des données en big-endian (formats réseau, certains formats de fichiers), vous avez deux options.

**Spécifier l'endianness globalement**, en tête de fichier :

```cpp
#pragma endian big
```

**Spécifier l'endianness par type**, avec les préfixes `le` et `be` :

```cpp
be u32 magic;    // ce champ est en big-endian  
le u16 flags;    // celui-ci est explicitement en little-endian  
u32 size;        // celui-ci suit le pragma global (little-endian par défaut)  
```

Dans le contexte de cette formation, la quasi-totalité des binaires ELF x86-64 sont en little-endian. Vous n'aurez besoin du `be` que pour les champs réseau (chapitres 23 et 28) ou les magic numbers qui suivent une convention big-endian.

---

## Variables et placement

### Déclarer une variable

En `.hexpat`, déclarer une variable ne réserve pas de la mémoire comme en C — elle **se plaque sur le fichier** à la position courante du curseur de lecture. Après l'évaluation, le curseur avance de la taille du type.

```cpp
u32 magic;      // lit 4 octets à la position courante, avance de 4  
u16 version;    // lit 2 octets à la nouvelle position, avance de 2  
u32 file_size;  // lit 4 octets, avance de 4  
```

Si le fichier commence par `7f 45 4c 46 02 00 40 00 00 00`, alors après évaluation :

- `magic` vaut `0x464c457f` (les 4 premiers octets en little-endian)  
- `version` vaut `0x0002`  
- `file_size` vaut `0x00000040`

Les variables apparaissent dans l'arbre Pattern Data avec leur nom, leur type, leur offset et leur valeur interprétée.

### Placement explicite avec `@`

L'opérateur `@` permet de placer une variable à un **offset absolu** dans le fichier, indépendamment de la position courante du curseur :

```cpp
u32 magic @ 0x00;          // lit 4 octets à l'offset 0  
u16 e_type @ 0x10;         // lit 2 octets à l'offset 16  
u64 e_entry @ 0x18;        // lit 8 octets à l'offset 24  
```

C'est la syntaxe que vous utiliserez le plus souvent pour les variables isolées et les points d'entrée de vos patterns. À l'intérieur des structures (voir ci-dessous), le placement est séquentiel et automatique — l'opérateur `@` sert alors à placer l'instance de la structure elle-même.

---

## Structures (`struct`)

Les structures sont le mécanisme fondamental pour décrire des blocs de données composites. La syntaxe est quasi identique au C :

```cpp
struct FileHeader {
    u32 magic;
    u16 version;
    u16 flags;
    u32 data_offset;
    u32 data_size;
};
```

À l'intérieur d'une structure, les champs sont lus **séquentiellement** : chaque champ commence là où le précédent se termine. La taille totale de la structure est la somme des tailles de ses champs (ici : 4 + 2 + 2 + 4 + 4 = 16 octets).

Pour instancier une structure sur le fichier, on la déclare comme une variable :

```cpp
FileHeader header @ 0x00;
```

ImHex parse les 16 premiers octets du fichier selon la description de `FileHeader`, et affiche un nœud dépliable dans l'arbre Pattern Data contenant les cinq champs avec leurs valeurs.

### Structures imbriquées

Les structures peuvent contenir d'autres structures, exactement comme en C :

```cpp
struct Timestamp {
    u16 year;
    u8  month;
    u8  day;
    u8  hour;
    u8  minute;
    u8  second;
};

struct FileHeader {
    u32 magic;
    u16 version;
    Timestamp created;
    Timestamp modified;
    u32 data_size;
};

FileHeader header @ 0x00;
```

Dans l'arbre Pattern Data, `header.created` apparaît comme un sous-nœud dépliable contenant les champs `year`, `month`, `day`, etc. La colorisation dans la vue hexadécimale attribue des couleurs distinctes aux différentes structures imbriquées, ce qui donne une lecture visuelle immédiate de l'agencement des données.

---

## Énumérations (`enum`)

Les énumérations associent des **noms symboliques à des valeurs numériques**, exactement comme en C. La différence en `.hexpat` est qu'on doit spécifier le type sous-jacent, car la taille de l'enum détermine le nombre d'octets lus :

```cpp
enum FileType : u16 {
    TEXT     = 0x0001,
    BINARY   = 0x0002,
    ARCHIVE  = 0x0003,
    IMAGE    = 0x0004
};
```

Quand ImHex rencontre un champ de type `FileType`, il lit 2 octets (car le type sous-jacent est `u16`) et affiche le nom symbolique correspondant dans l'arbre. Si les octets valent `03 00` (little-endian), l'arbre affiche `ARCHIVE (3)` plutôt qu'un nombre brut. Cette lisibilité fait toute la différence quand on explore un format inconnu.

Utilisation dans une structure :

```cpp
struct FileHeader {
    u32 magic;
    FileType type;    // lit 2 octets, affiche le nom symbolique
    u32 data_size;
};
```

Si une valeur lue ne correspond à aucun membre de l'enum, ImHex affiche la valeur numérique brute avec un indicateur signalant qu'elle est hors spécification. C'est un signal utile en RE : une valeur non attendue peut indiquer une version inconnue du format, une corruption, ou un champ que vous avez mal identifié.

---

## Tableaux

Les tableaux permettent de parser des **séquences d'éléments de même type**. `.hexpat` supporte trois formes de tableaux.

### Tableau à taille fixe

```cpp
u8 sha256_hash[32];           // 32 octets consécutifs  
u32 section_offsets[16];      // 16 entiers de 4 octets = 64 octets  
```

### Tableau à taille dynamique (référencée par un champ)

C'est la forme la plus courante en RE, car la taille d'un tableau dépend presque toujours d'un champ lu précédemment :

```cpp
struct RecordTable {
    u32 count;
    Record records[count];    // 'count' éléments de type Record
};
```

ImHex évalue `count` en lisant le fichier, puis parse exactement `count` instances de `Record` à la suite. C'est un comportement impossible à reproduire avec un hex editor classique.

### Tableau de caractères (chaînes)

Un tableau de `char` est affiché comme une chaîne dans l'arbre Pattern Data :

```cpp
char name[16];    // affiché comme une chaîne de 16 caractères
```

Pour les chaînes null-terminées dont la longueur n'est pas connue à l'avance, `.hexpat` offre le type `str` avec une syntaxe de terminaison que nous verrons dans les fonctionnalités avancées.

---

## Unions (`union`)

Une union déclare des **interprétations alternatives** d'une même région d'octets, exactement comme en C. Tous les membres commencent au même offset, et la taille de l'union est celle du plus grand membre :

```cpp
union Value {
    u32 as_uint;
    s32 as_int;
    float as_float;
};
```

Cette construction est utile quand un champ peut être interprété de plusieurs façons selon le contexte. Par exemple, dans certains formats, un champ de 4 octets est un entier dans un type de record et un flottant dans un autre. Avec une union, ImHex affiche les deux interprétations et vous choisissez celle qui a du sens.

On peut combiner unions et structures pour modéliser des formats à champs variants :

```cpp
struct TaggedValue {
    u8 type;
    union {
        u32 integer_value;
        float float_value;
        char string_value[4];
    } value;
};
```

---

## Conditions et champs optionnels

Le langage `.hexpat` supporte les instructions `if` / `else` à l'intérieur des structures pour parser des **champs qui n'existent que sous certaines conditions** :

```cpp
struct Packet {
    u8 type;
    u16 length;

    if (type == 0x01) {
        u32 source_ip;
        u32 dest_ip;
    } else if (type == 0x02) {
        char hostname[length];
    }
};
```

Ici, la structure `Packet` a un contenu variable selon la valeur du champ `type`. ImHex évalue la condition au moment du parsing, lit les champs correspondants, et ignore les branches non prises. La taille effective de la structure dépend donc du contenu du fichier — un comportement essentiel pour parser les formats réels, qui sont presque toujours conditionnels.

Les conditions peuvent référencer n'importe quel champ déjà lu dans la structure ou dans une structure parente, ainsi que des variables globales.

---

## Attributs

Les attributs modifient le comportement d'affichage ou de parsing d'une variable. Ils se placent après la déclaration, entre doubles crochets `[[...]]` :

### `[[color]]` — couleur d'affichage

```cpp
u32 magic [[color("FF0000")]];    // surligné en rouge dans la vue hex
```

Vous pouvez spécifier la couleur en hexadécimal RGB. C'est un complément aux couleurs automatiques attribuées par ImHex — utile pour faire ressortir un champ critique (une clé, un checksum, un point de décision).

### `[[name]]` — nom d'affichage

```cpp
u16 e_type [[name("Type ELF")]];
```

Remplace le nom de la variable dans l'arbre Pattern Data par un nom plus lisible. Le nom de la variable dans le code reste inchangé.

### `[[comment]]` — commentaire

```cpp
u32 e_entry [[comment("Point d'entrée du programme")]];
```

Ajoute un commentaire visible dans l'arbre Pattern Data au survol. C'est l'équivalent d'un commentaire de documentation, mais intégré au résultat du parsing.

### `[[format]]` — formatage personnalisé

```cpp
u32 permissions [[format("hex")]];    // affiché en hexadécimal plutôt qu'en décimal
```

Les formats disponibles incluent `hex`, `octal`, `binary`, et vous pouvez définir des fonctions de formatage personnalisées pour des affichages plus élaborés.

### `[[hidden]]` — masquer un champ

```cpp
u16 reserved [[hidden]];
```

Le champ est parsé (le curseur avance) mais n'apparaît pas dans l'arbre Pattern Data. Utile pour les champs réservés ou de padding que vous voulez sauter proprement sans encombrer l'affichage.

---

## Pointeurs

Le langage `.hexpat` permet de déclarer des **pointeurs** — des champs dont la valeur est un offset vers une autre position dans le fichier. La syntaxe utilise l'opérateur `*` comme en C :

```cpp
struct Header {
    u32 magic;
    u32 *name_table : u32;   // pointeur 32 bits vers une structure
};
```

La syntaxe `*name_table : u32` signifie : « lis un `u32` à la position courante, interprète-le comme un offset absolu dans le fichier, et parse la structure pointée à cet offset ». Le type après `:` est le type du pointeur lui-même (sa taille en tant que champ dans la structure parente).

On peut aussi pointer vers des types complexes :

```cpp
struct NameEntry {
    u16 length;
    char name[length];
};

struct Header {
    u32 magic;
    NameEntry *names : u32;    // suit le pointeur vers un NameEntry
};
```

Dans l'arbre Pattern Data, ImHex affiche la valeur du pointeur (l'offset) et rend le contenu pointé dépliable. Dans la vue hexadécimale, la région pointée est colorisée, ce qui crée un lien visuel entre le pointeur et sa cible — extrêmement pratique pour comprendre les références croisées dans un format binaire.

---

## Variables spéciales et fonctions built-in

Le langage `.hexpat` fournit plusieurs **variables spéciales** et **fonctions intégrées** qui donnent accès à des informations sur le contexte de parsing.

### `$` — position courante du curseur

La variable `$` contient l'offset courant du curseur de lecture. Elle est mise à jour automatiquement à chaque champ parsé. On l'utilise principalement dans les conditions et pour le débogage :

```cpp
struct Section {
    u32 offset;
    u32 size;
    // Sauvegarder la position courante pour y revenir
};
```

### `std::mem::size()` — taille du fichier

```cpp
#include <std/mem.pat>

if ($ < std::mem::size()) {
    // il reste des données à parser
}
```

### `std::mem::read_unsigned()` — lire sans avancer le curseur

Permet de lire une valeur à un offset donné sans modifier la position du curseur. Utile pour les conditions de type « regarde ce qu'il y a plus loin avant de décider quoi parser ici » :

```cpp
#include <std/mem.pat>

u8 next_byte = std::mem::read_unsigned($, 1);
```

### La bibliothèque standard (`std::`)

ImHex fournit une bibliothèque standard importable via `#include` qui contient des types, des fonctions et des patterns pré-écrits :

```cpp
#include <std/mem.pat>       // fonctions mémoire (size, read, etc.)
#include <std/io.pat>        // fonctions d'affichage (print, format)
#include <std/string.pat>    // manipulation de chaînes
#include <std/math.pat>      // fonctions mathématiques
```

Ces modules sont installés avec ImHex (ou via le Content Store) et couvrent les besoins courants. Nous utiliserons `std::mem` et `std::io` régulièrement dans les patterns des sections suivantes.

---

## Commentaires et `#include`

Les commentaires suivent la syntaxe C/C++ :

```cpp
// Commentaire sur une ligne

/*
   Commentaire
   sur plusieurs lignes
*/
```

La directive `#include` permet d'importer d'autres fichiers `.hexpat`, que ce soient des modules de la bibliothèque standard ou vos propres fichiers de types partagés :

```cpp
#include <std/mem.pat>              // bibliothèque standard (entre chevrons)
#include "mes_types_communs.hexpat" // fichier local (entre guillemets)
```

Cette mécanique d'inclusion est essentielle pour organiser des patterns complexes en modules réutilisables. Quand vous aurez écrit un pattern pour le header ELF (section 6.4), vous pourrez l'inclure dans d'autres patterns qui analysent des sections spécifiques.

---

## Mettre le tout ensemble : un premier pattern complet

Voici un pattern minimaliste mais complet qui parse un hypothétique format de fichier simple — un header suivi d'une table de records :

```cpp
#include <std/mem.pat>

// Magic number attendu
#define EXPECTED_MAGIC 0x464D5448  // "HTMF" en little-endian

enum RecordType : u8 {
    TEXT   = 0x01,
    NUMBER = 0x02,
    BLOB   = 0x03
};

struct Record {
    RecordType type;
    u16 data_length;

    if (type == RecordType::TEXT) {
        char data[data_length];
    } else if (type == RecordType::NUMBER) {
        u64 value;
        padding[data_length - 8];
    } else {
        u8 raw_data[data_length];
    }
};

struct FileHeader {
    u32 magic [[color("FF4444"), comment("Doit valoir 0x464D5448")]];
    u16 version;
    u16 record_count;
    Record records[record_count];
};

FileHeader file @ 0x00;
```

Ce pattern illustre la plupart des concepts vus dans cette section : types primitifs, enum typée, structure avec champs conditionnels, tableau de taille dynamique, attributs de formatage, et placement explicite avec `@`. En une trentaine de lignes, il décrit un format capable de parser un fichier réel — et ImHex vous en montrera le résultat sous forme d'arbre colorisé, navigable et documenté.

---

## Erreurs courantes et débogage

Quand vous développez un pattern, les erreurs sont inévitables. Voici les plus fréquentes et comment les diagnostiquer.

**« Variable does not fit in file »** — votre pattern tente de lire au-delà de la fin du fichier. Causes habituelles : un champ `count` mal interprété qui produit une valeur gigantesque, ou une structure dont la taille ne correspond pas à la réalité. Vérifiez les valeurs dans le Data Inspector.

**« Unexpected token »** — erreur de syntaxe. Les oublis de point-virgule après une déclaration et les accolades mal appariées sont les causes les plus fréquentes.

**Valeurs incohérentes dans l'arbre** — si les valeurs parsées n'ont aucun sens (des entiers énormes, des chaînes illisibles), vous avez probablement un problème d'**alignement**. Un champ oublié ou de mauvaise taille dans votre structure décale tout ce qui suit. Comparez les offsets affichés dans l'arbre avec ce que vous voyez dans la vue hexadécimale.

**Mauvais endianness** — si un champ `u16` qui devrait valoir `0x0002` affiche `0x0200`, vous avez un problème d'endianness. Ajoutez un `#pragma endian big` ou utilisez les préfixes `be` / `le` sur les champs concernés.

> 💡 **Astuce de débogage** : La fonction `std::print()` (importée via `#include <std/io.pat>`) permet d'afficher des valeurs dans la console de sortie d'ImHex pendant l'évaluation du pattern. C'est l'équivalent d'un `printf` de débogage :  
> ```cpp  
> #include <std/io.pat>  
> u32 count @ 0x08;  
> std::print("count = {}", count);  
> ```

---

## Résumé

Le langage `.hexpat` est un C simplifié et spécialisé pour le parsing binaire. Ses types primitifs (`u8`–`u128`, `float`, `double`, `char`, `bool`) couvrent tous les types de données rencontrés dans les fichiers binaires. Les structures, énumérations, tableaux, unions et conditionnels permettent de décrire des formats allant du plus simple au plus élaboré. L'opérateur `@` place les variables à des offsets absolus, les attributs `[[...]]` contrôlent l'affichage, et les pointeurs suivent les références croisées. Avec ces fondations, vous êtes prêt à écrire le pattern ELF complet de la section 6.4 — un exercice qui mettra en pratique tous les concepts de cette section sur un format que vous connaissez déjà.

---


⏭️ [Écrire un pattern pour visualiser un header ELF depuis zéro](/06-imhex/04-pattern-header-elf.md)
