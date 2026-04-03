🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.5 — Parser une structure C/C++ maison directement dans le binaire

> 🎯 **Objectif de cette section** : Apprendre à identifier des structures C/C++ compilées dans un binaire ELF, à en déduire la disposition mémoire (layout) sans disposer du code source, et à écrire des patterns `.hexpat` pour les visualiser — en tenant compte du padding, de l'alignement et des particularités de GCC.

> 📦 **Binaire de test** : `binaries/ch22-oop/oop_O0` (compilé avec symboles) puis `binaries/ch22-oop/oop_O0_strip` (sans symboles)

---

## Le problème : des structures sans code source

En section 6.4, nous avons parsé le header ELF — un format parfaitement documenté dont la spécification donne le type, la taille et l'offset de chaque champ. Dans la vraie vie du reverse engineering, la situation est rarement aussi confortable. Le binaire que vous analysez contient des structures définies par le développeur — une `struct Config`, une `class Player`, un `struct PacketHeader` — dont vous n'avez ni la documentation ni le code source.

Ces structures existent pourtant bel et bien dans le binaire. Elles sont stockées dans les sections `.data` (variables globales initialisées), `.bss` (variables globales non initialisées), `.rodata` (constantes), ou sur la pile et le tas à l'exécution. Le compilateur a traduit la définition C/C++ en un agencement d'octets en mémoire, en ajoutant éventuellement du padding pour respecter les contraintes d'alignement de l'architecture. Notre travail consiste à retrouver cet agencement et à le décrire dans un pattern `.hexpat`.

---

## Rappel : comment GCC organise une structure en mémoire

Avant de parser quoi que ce soit dans ImHex, il faut comprendre les règles que GCC applique quand il traduit une `struct` C en octets. Ces règles sont dictées par l'**ABI System V AMD64** que nous avons vue au chapitre 3.

### Alignement naturel

Chaque type primitif a un **alignement naturel** égal à sa taille (jusqu'à un maximum de 8 octets sur x86-64) :

| Type C | Taille | Alignement |  
|---|---|---|  
| `char`, `uint8_t` | 1 octet | 1 octet |  
| `short`, `uint16_t` | 2 octets | 2 octets |  
| `int`, `uint32_t`, `float` | 4 octets | 4 octets |  
| `long`, `uint64_t`, `double`, pointeurs | 8 octets | 8 octets |

Un champ de type `int` doit commencer à une adresse multiple de 4. Un champ `double` doit commencer à une adresse multiple de 8. Si le champ précédent se termine à une adresse qui ne respecte pas cette contrainte, le compilateur insère des **octets de padding** entre les deux champs pour réaligner.

### Padding interne

Prenons un exemple concret. Supposons que le développeur a écrit :

```c
struct PlayerInfo {
    uint8_t  level;       // 1 octet
    uint32_t score;       // 4 octets
    uint8_t  health;      // 1 octet
    uint64_t unique_id;   // 8 octets
};
```

En mémoire, GCC ne place pas ces champs de manière contiguë. Voici la disposition réelle :

```
Offset  Contenu               Taille
0x00    level (uint8_t)       1 octet
0x01    --- padding ---       3 octets  (aligner score sur 4)
0x04    score (uint32_t)      4 octets
0x08    health (uint8_t)      1 octet
0x09    --- padding ---       7 octets  (aligner unique_id sur 8)
0x10    unique_id (uint64_t)  8 octets
0x18    (fin)                 Total : 24 octets
```

La structure occupe 24 octets alors que les données utiles n'en représentent que 14. Les 10 octets restants sont du padding invisible au niveau du code source C mais bien réel dans le binaire. Si votre pattern `.hexpat` ne tient pas compte de ce padding, tous les champs après le premier trou seront décalés et les valeurs parsées seront fausses.

### Padding de fin (tail padding)

GCC ajoute aussi du padding à la **fin** de la structure pour que la taille totale soit un multiple de l'alignement du membre le plus grand. Cela garantit que les tableaux de structures respectent l'alignement de chaque élément.

Dans notre exemple, le membre le plus large est `unique_id` (alignement 8). La structure se termine à l'offset `0x18` (24), qui est déjà un multiple de 8 — pas de padding de fin nécessaire. Mais considérons cette variante :

```c
struct CompactInfo {
    uint32_t score;      // 4 octets
    uint8_t  level;      // 1 octet
};
```

Les données utiles occupent 5 octets, mais le membre le plus large est `score` (alignement 4). La taille totale de la structure est arrondie à 8 octets (prochain multiple de 4), avec 3 octets de padding après `level`.

### L'attribut `__attribute__((packed))`

Certains développeurs utilisent `__attribute__((packed))` pour supprimer tout padding et forcer une disposition compacte. C'est courant dans les structures de protocoles réseau et les formats de fichiers. Une structure packée occupe exactement la somme des tailles de ses membres, sans aucun trou. En `.hexpat`, cela se traduit simplement par une structure sans `padding[]` entre les champs.

Quand vous analysez un binaire inconnu, vous ne savez pas a priori si une structure est packée ou non. Le Data Inspector et la vue hexadécimale d'ImHex vous permettront de trancher : si les valeurs parsées ont du sens sans padding, la structure est probablement packée ; si elles deviennent cohérentes uniquement en ajoutant des trous aux points d'alignement, elle ne l'est pas.

---

## Méthode : retrouver le layout d'une structure inconnue

Retrouver la disposition mémoire d'une structure dont on n'a pas le code source est un processus itératif qui combine plusieurs sources d'information. Voici la démarche générale.

### Étape 1 — Localiser la structure dans le binaire

Avant de parser, il faut savoir **où** la structure se trouve dans le fichier. Plusieurs indices permettent de la localiser.

**Les chaînes de caractères.** Si la structure contient des chaînes ou des pointeurs vers `.rodata`, la commande `strings` ou le panneau Strings d'ImHex peuvent révéler des textes lisibles dont l'offset pointe vers la zone de données qui nous intéresse.

**Le désassemblage.** En analysant le code dans Ghidra ou objdump (chapitres 7–8), vous voyez les instructions qui accèdent à la structure : des `mov` avec des offsets relatifs à un registre de base. Ces offsets vous donnent les positions relatives des champs. Par exemple, `mov eax, [rbx+0x10]` indique qu'un champ de 4 octets se trouve à l'offset `0x10` dans la structure pointée par `rbx`.

**Les sections `.data` et `.rodata`.** Les variables globales initialisées se trouvent dans `.data`, les constantes dans `.rodata`. Le Section Header Table (que nous avons parsé en 6.4) donne les offsets et tailles de ces sections dans le fichier. Vous pouvez naviguer directement à ces offsets dans ImHex.

**Le décompilateur.** Si vous avez déjà importé le binaire dans Ghidra (chapitre 8), le décompilateur fournit une reconstitution approximative des structures sous forme de pseudo-code C. Cette reconstitution est imparfaite mais donne un point de départ solide — types probables, nombre de champs, taille approximative.

### Étape 2 — Formuler une hypothèse de layout

À partir des indices récoltés, vous formulez une première hypothèse sur la structure : combien de champs, quels types, dans quel ordre. Vous écrivez le pattern `.hexpat` correspondant.

À ce stade, ne cherchez pas la perfection. Commencez par les champs dont vous êtes sûr (le magic number s'il y en a un, les entiers dont la valeur est reconnaissable, les pointeurs dont l'adresse est plausible) et laissez des zones d'incertitude marquées par des `u8 unknown_XX[N]` temporaires.

### Étape 3 — Évaluer, vérifier, ajuster

Évaluez le pattern dans ImHex et examinez les valeurs parsées. Les questions à se poser :

- Les entiers ont-ils des valeurs plausibles ? Un `uint32_t` qui vaut `0x00000003` pour un compteur, c'est plausible. Un `uint32_t` qui vaut `0x7F454C46`, c'est un magic number ELF, pas un compteur.  
- Les pointeurs pointent-ils vers des zones existantes du fichier ou de l'espace d'adressage ? Un pointeur `0x00404020` dans un binaire dont `.data` commence à `0x00404000` est plausible. Un pointeur `0xCCCCCCCC` est du padding non initialisé.  
- Y a-t-il des « trous » avec des octets nuls entre des champs significatifs ? Ce sont probablement des octets de padding d'alignement.  
- Les chaînes sont-elles lisibles et terminées par un null byte ?

Ajustez votre pattern, réévaluez, et recommencez jusqu'à ce que toutes les valeurs soient cohérentes.

---

## Cas concret : parser une structure globale dans `.data`

Mettons cette méthode en pratique sur un scénario réaliste. Supposons que lors de l'analyse du binaire `ch22-oop`, le désassemblage et les chaînes nous ont permis d'identifier une variable globale dans la section `.data` qui semble être une structure de configuration. Le Section Header Table nous donne l'offset de `.data` dans le fichier.

Après exploration dans ImHex, nous observons le bloc suivant à l'offset de cette variable (les valeurs sont fictives mais réalistes) :

```
00 00 80 3F 00 00 00 00  03 00 00 00 00 00 00 00
E8 03 00 00 00 00 00 00  01 00 00 00 00 00 00 00
48 65 6C 6C 6F 00 00 00  00 00 00 00 00 00 00 00
```

Analysons ces 48 octets en utilisant le Data Inspector.

En posant le curseur sur l'offset `0x00`, le Data Inspector nous montre que `float` = `1.0` (les octets `00 00 80 3F` sont la représentation IEEE 754 little-endian de `1.0`). C'est un indice fort : le premier champ est probablement un `float`.

Les octets `0x04`–`0x07` sont nuls — padding probable pour aligner le champ suivant sur 8 octets.

À l'offset `0x08`, `uint32_t` = `3`. Valeur plausible pour un compteur ou un identifiant.

Les octets `0x0C`–`0x0F` sont nuls — encore du padding, cette fois pour aligner un champ 64 bits.

À l'offset `0x10`, `uint32_t` = `1000` (`0x03E8`). Suivi de padding nul jusqu'à `0x17`.

À l'offset `0x18`, `uint32_t` = `1`. Un booléen ou un flag ?

Enfin, à l'offset `0x20`, on lit `Hello` suivi de zéros — une chaîne C null-terminée dans un buffer de 16 octets.

En croisant ces observations avec le code désassemblé (où nous voyons des accès à `[base+0x00]`, `[base+0x08]`, `[base+0x10]`, `[base+0x18]`, `[base+0x20]`), nous formulons cette hypothèse :

```cpp
struct AppConfig {
    float  scale;            // 0x00 : 4 octets
    padding[4];              // 0x04 : alignement sur 8
    u32    max_retries;      // 0x08 : 4 octets
    padding[4];              // 0x0C : alignement sur 8
    u32    timeout_ms;       // 0x10 : 4 octets
    padding[4];              // 0x14 : alignement sur 8
    u32    verbose;          // 0x18 : 4 octets (booléen)
    padding[4];              // 0x1C : alignement sur 8
    char   label[16];        // 0x20 : chaîne fixe
};                           // Taille totale : 48 octets

AppConfig config @ 0x...;    // remplacer par l'offset réel dans .data
```

Évaluons ce pattern. Dans l'arbre Pattern Data, nous voyons :

```
config
├── scale        = 1.0
├── max_retries  = 3
├── timeout_ms   = 1000
├── verbose      = 1
└── label        = "Hello"
```

Les valeurs sont cohérentes, les types ont du sens, et la taille totale (48 octets) correspond au bloc de données observé. Le pattern est validé.

> 💡 **Pourquoi autant de padding ?** Vous remarquez que chaque champ de 4 octets est suivi de 4 octets de padding, comme si le compilateur alignait tout sur 8 octets. C'est un comportement fréquent quand la structure contient au moins un membre de 8 octets (un pointeur ou un `uint64_t`), ou quand le compilateur choisit un alignement conservateur. Ici, l'alignement global de la structure est probablement dicté par un membre 64 bits que nous n'avons pas vu (peut-être supprimé lors d'une refactorisation), ou par un pragma d'alignement explicite.

---

## Structures C++ : données membres et vtable pointer

Quand vous parsez un objet C++ plutôt qu'une structure C, un élément supplémentaire entre en jeu : le **pointeur de vtable** (`vptr`). Pour toute classe qui contient au moins une méthode virtuelle, GCC ajoute un pointeur implicite au début de l'objet. Ce pointeur occupe 8 octets (sur x86-64) et pointe vers la vtable de la classe dans `.rodata`.

Concrètement, si le code source déclare :

```cpp
class Enemy {  
public:  
    virtual void update();
    virtual void render();
    uint32_t hp;
    float    speed;
    uint64_t id;
};
```

La disposition en mémoire de l'objet est :

```
Offset  Contenu                Taille
0x00    vptr (→ vtable)        8 octets    ← ajouté par le compilateur
0x08    hp (uint32_t)          4 octets
0x0C    speed (float)          4 octets
0x10    id (uint64_t)          8 octets
0x18    (fin)                  Total : 24 octets
```

Le pattern `.hexpat` correspondant :

```cpp
struct Enemy {
    u64   vptr         [[format("hex"), comment("Pointeur vers la vtable")]];
    u32   hp;
    float speed;
    u64   id           [[format("hex")]];
};
```

Le `vptr` est un pointeur vers une **adresse virtuelle** (pas un offset dans le fichier). Sa valeur sera une adresse dans la plage de `.rodata`, typiquement quelque chose comme `0x00403D50`. Si vous voyez une valeur de 8 octets au début d'un objet qui pointe vers `.rodata`, c'est un indice très fort que vous avez affaire à un objet C++ polymorphe. Nous approfondirons l'analyse des vtables au chapitre 17.

### Héritage simple

En cas d'héritage, les données membres de la classe parente sont placées **avant** celles de la classe dérivée. Le `vptr` est hérité (et potentiellement mis à jour pour pointer vers la vtable de la classe dérivée) :

```cpp
// C++ : class Boss : public Enemy { uint32_t phase; };

struct Boss {
    // --- membres hérités de Enemy ---
    u64   vptr         [[format("hex"), comment("vtable de Boss")]];
    u32   hp;
    float speed;
    u64   id           [[format("hex")]];
    // --- membres propres à Boss ---
    u32   phase;
    padding[4];        // alignement de fin (taille multiple de 8)
};
```

Cette disposition en « préfixe parental » est la raison pour laquelle un pointeur `Enemy*` peut pointer vers un objet `Boss` en mémoire — les premiers octets ont la même structure. En `.hexpat`, on peut modéliser cela de manière plus élégante avec l'imbrication de structures :

```cpp
struct Enemy {
    u64   vptr [[format("hex")]];
    u32   hp;
    float speed;
    u64   id   [[format("hex")]];
};

struct Boss {
    Enemy base;          // héritage = inclusion du parent en tête
    u32   phase;
    padding[4];
};
```

---

## Tableaux de structures en `.data` et `.rodata`

Les structures ne vivent pas toujours seules. Il est très fréquent de trouver des **tableaux de structures** dans `.data` (tableau global mutable) ou `.rodata` (tableau constant, comme une table de correspondance). Un pattern `.hexpat` parse ces tableaux très naturellement :

```cpp
struct LevelEntry {
    u32  level_id;
    u32  enemy_count;
    float difficulty;
    padding[4];
};

// Si on sait qu'il y a 10 niveaux :
LevelEntry levels[10] @ 0x...;
```

Quand le nombre d'éléments n'est pas connu à l'avance, deux approches sont possibles. Si un compteur existe quelque part dans le binaire (un champ `num_levels` dans une structure de configuration, par exemple), on s'y réfère directement :

```cpp
AppConfig config @ 0x...;  
LevelEntry levels[config.max_retries] @ 0x...;  // si max_retries est le compteur  
```

Si aucun compteur n'est disponible, on peut calculer le nombre d'éléments à partir de la taille connue de la zone de données. Par exemple, si la section `.rodata` contient un bloc de 160 octets à partir d'un offset donné et que chaque `LevelEntry` fait 16 octets, on peut écrire :

```cpp
#include <std/mem.pat>

u32 block_size = 160;  // déterminé par analyse  
LevelEntry levels[block_size / sizeof(LevelEntry)] @ 0x...;  
```

---

## Pièges courants et stratégies de contournement

### Les zéros ne sont pas toujours du padding

Quand vous voyez des octets nuls entre deux champs identifiés, la tentation est forte de les déclarer comme `padding`. Mais un zéro peut aussi être une **valeur légitime** : un compteur à zéro, un booléen `false`, un entier non initialisé. Croisez toujours avec le désassemblage : si le code lit explicitement une valeur à cet offset (un `mov` ou un `cmp`), ce n'est pas du padding.

### L'alignement de GCC vs Clang vs MSVC

Les règles d'alignement décrites dans cette section s'appliquent à **GCC et Clang sur x86-64 Linux** (ABI System V). Si vous analysez un binaire compilé avec MSVC (Windows), les règles de padding diffèrent légèrement, notamment pour les structures contenant des `long double` ou pour l'alignement des bitfields. Dans le cadre de cette formation, nous restons sur GCC/Linux, mais gardez cette nuance en tête si vous croisez des binaires cross-compilés avec MinGW.

### Les structures avec bitfields

Les bitfields C (`uint32_t flags : 3;`) sont compilés en masques de bits à l'intérieur d'un mot machine. GCC peut regrouper plusieurs bitfields dans un même entier ou les répartir sur plusieurs, selon des règles complexes qui dépendent des types et de l'ordre de déclaration. En `.hexpat`, le type `bitfield` (vu en section 6.4) permet de modéliser ces cas, mais il faut d'abord déterminer la taille du mot machine sous-jacent par observation.

### Les unions et les variants

Quand un même bloc d'octets semble avoir des interprétations différentes selon les instances (parfois c'est un entier, parfois une chaîne, parfois un pointeur), vous avez probablement affaire à une `union` C ou à un champ variant. Le type `union` de `.hexpat` combiné avec un `if` conditionnel sur un champ discriminant permet de modéliser ces cas :

```cpp
struct TaggedValue {
    u8 tag;
    padding[7];
    union {
        u64   as_integer;
        double as_float;
        char   as_string[8];
    } value;
};
```

---

## Workflow récapitulatif

Voici la démarche complète pour parser une structure inconnue dans un binaire, résumée en sept étapes :

1. **Localiser** la zone de données dans le fichier (via `readelf`, Ghidra, ou les Section Headers parsés en 6.4).  
2. **Explorer** visuellement dans ImHex : déplacer le curseur, observer le Data Inspector, repérer les valeurs reconnaissables.  
3. **Croiser** avec le désassemblage : identifier les accès mémoire (`mov`, `lea`) qui révèlent les offsets des champs.  
4. **Formuler** une hypothèse de layout : types, tailles, padding.  
5. **Écrire** le pattern `.hexpat` correspondant.  
6. **Évaluer** et vérifier : les valeurs parsées sont-elles cohérentes ? Les types ont-ils du sens dans le contexte ?  
7. **Itérer** : ajuster le pattern, ajouter des enums et des commentaires, raffiner les noms de champs.

Ce workflow est itératif et converge progressivement. Les premières itérations produisent un pattern approximatif avec des champs `unknown`. Les itérations suivantes, enrichies par le désassemblage et les tests dynamiques (chapitres 11–13), affinent la compréhension jusqu'à un pattern complet et documenté.

---

## Résumé

Parser des structures C/C++ dans un binaire sans code source est l'un des usages les plus puissants d'ImHex en reverse engineering. La clé est de comprendre les règles de padding et d'alignement de GCC sur x86-64 : alignement naturel des types, padding interne entre champs, padding de fin pour les tableaux. Pour les objets C++, le `vptr` en tête de l'objet est un marqueur reconnaissable qui signale un objet polymorphe. La démarche est toujours itérative — localiser, explorer, formuler une hypothèse, écrire le pattern, évaluer, ajuster — et s'enrichit au fil de l'analyse en combinant les informations d'ImHex avec celles du désassembleur et du débogueur. Le pattern `.hexpat` produit capture cette compréhension de manière pérenne et partageable.

---


⏭️ [Colorisation, annotations et bookmarks de régions binaires](/06-imhex/06-colorisation-annotations.md)
