🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.4 — Écrire un pattern pour visualiser un header ELF depuis zéro

> 🎯 **Objectif de cette section** : Construire pas à pas un pattern `.hexpat` qui parse le header principal d'un fichier ELF 64 bits, les Program Headers et les Section Headers, en s'appuyant sur la spécification ELF et les concepts du chapitre 2. Le pattern final sera réutilisable dans les chapitres suivants.

> 📁 **Fichier produit** : `hexpat/elf_header.hexpat`  
> 📦 **Binaire de test** : n'importe quel ELF 64 bits de `binaries/` — par exemple `binaries/ch21-keygenme/keygenme_O0`

---

## Pourquoi construire ce pattern à la main ?

ImHex fournit déjà un pattern ELF dans son Content Store. Alors pourquoi en écrire un nous-mêmes ? Pour trois raisons.

Premièrement, le format ELF est un **terrain d'entraînement idéal** pour le langage `.hexpat`. Il contient des types primitifs variés (entiers de 8 à 64 bits), des énumérations (type de fichier, architecture, endianness), des tableaux de taille dynamique (Program Headers, Section Headers), et des pointeurs vers d'autres régions du fichier (offsets de sections, table de chaînes). C'est un condensé de tout ce que nous avons vu en section 6.3.

Deuxièmement, écrire le pattern nous oblige à **relire la spécification ELF** avec un regard neuf. Au chapitre 2, nous avons vu les sections ELF du point de vue de leur rôle (`.text` pour le code, `.data` pour les données…). Ici, nous les voyons du point de vue de leur **encodage binaire** — quels octets, à quels offsets, avec quel endianness. C'est exactement le changement de perspective que le RE exige.

Troisièmement, le pattern que nous produirons sera **adapté à nos besoins**. Le pattern du Content Store est générique et couvre tous les cas de figure (ELF 32 et 64 bits, toutes les architectures). Le nôtre sera ciblé sur les ELF 64 bits x86-64 que nous manipulons dans cette formation, avec des commentaires en français et des attributs de formatage adaptés à notre workflow.

---

## Rappel : structure d'un fichier ELF 64 bits

Avant d'écrire la moindre ligne de `.hexpat`, rappelons l'organisation générale d'un fichier ELF telle que vue au chapitre 2. Un ELF se compose de trois couches de métadonnées :

**Le ELF Header** (`Elf64_Ehdr`) — 64 octets fixes à l'offset 0 du fichier. C'est le point d'entrée : il identifie le fichier comme ELF, spécifie l'architecture, l'endianness, le type (exécutable, shared object, relocatable…), et contient les offsets vers les deux tables suivantes.

**La Program Header Table** — un tableau de `Elf64_Phdr` (56 octets chacun) décrivant les **segments** que le loader (`ld.so`) doit charger en mémoire. L'offset de cette table et le nombre d'entrées sont donnés par le ELF Header.

**La Section Header Table** — un tableau de `Elf64_Shdr` (64 octets chacun) décrivant les **sections** du fichier (`.text`, `.data`, `.rodata`, `.bss`, etc.). Là encore, l'offset et le nombre d'entrées viennent du ELF Header.

Notre pattern va parser ces trois couches dans cet ordre, en suivant la chaîne de pointeurs du ELF Header vers les deux tables.

---

## Étape 1 : le magic number et l'identification

Ouvrons un fichier ELF dans ImHex et regardons les 16 premiers octets à l'offset `0x00`. Ce bloc s'appelle `e_ident` — le champ d'identification ELF. C'est un tableau de 16 octets qui contient le magic number et les paramètres fondamentaux du fichier.

Commençons notre pattern par ce bloc :

```cpp
#include <std/io.pat>

// === e_ident : les 16 premiers octets ===

enum EI_CLASS : u8 {
    ELFCLASSNONE = 0,
    ELFCLASS32   = 1,    // ELF 32 bits
    ELFCLASS64   = 2     // ELF 64 bits
};

enum EI_DATA : u8 {
    ELFDATANONE = 0,
    ELFDATA2LSB = 1,     // Little-endian
    ELFDATA2MSB = 2      // Big-endian
};

enum EI_OSABI : u8 {
    ELFOSABI_NONE    = 0x00,   // UNIX System V
    ELFOSABI_HPUX    = 0x01,
    ELFOSABI_NETBSD  = 0x02,
    ELFOSABI_GNU     = 0x03,   // alias ELFOSABI_LINUX
    ELFOSABI_SOLARIS = 0x06,
    ELFOSABI_FREEBSD = 0x09
};

struct ElfIdent {
    char     magic[4]         [[comment("Doit valoir 0x7f 'E' 'L' 'F'")]];
    EI_CLASS file_class       [[comment("32 ou 64 bits")]];
    EI_DATA  data_encoding    [[comment("Endianness")]];
    u8       version          [[comment("Toujours 1 pour EV_CURRENT")]];
    EI_OSABI os_abi           [[comment("OS/ABI cible")]];
    u8       abi_version;
    padding[7]                [[comment("Octets de padding réservés")]];
};
```

Quelques points à noter.

Le magic number est déclaré comme `char[4]` plutôt que `u32`. C'est un choix délibéré : les 4 octets `7f 45 4c 46` sont mieux lus comme les caractères `\x7f`, `E`, `L`, `F` que comme un entier. ImHex affichera la chaîne dans l'arbre, ce qui est immédiatement lisible.

Les champs `file_class` et `data_encoding` utilisent des enums typées sur `u8`. Dans l'arbre Pattern Data, au lieu de voir `2` et `1`, vous verrez `ELFCLASS64` et `ELFDATA2LSB` — un gain de lisibilité considérable.

Les 7 derniers octets de `e_ident` sont du padding réservé par la spécification. On utilise `padding[7]` pour les sauter proprement sans encombrer l'arbre.

Évaluez ce pattern avec `F5`. Vous devriez voir un nœud `ElfIdent` dépliable dans Pattern Data, avec chaque champ nommé et interprété. Dans la vue hexadécimale, les 16 premiers octets sont colorisés. C'est un bon début.

---

## Étape 2 : le ELF Header complet (`Elf64_Ehdr`)

Les 48 octets suivants (offsets `0x10` à `0x3F`) constituent le reste du ELF Header. Ils spécifient le type de fichier, l'architecture, le point d'entrée et les pointeurs vers les tables de headers. Ajoutons les enums nécessaires puis la structure complète :

```cpp
enum ET_Type : u16 {
    ET_NONE   = 0x0000,   // Aucun
    ET_REL    = 0x0001,   // Relocatable (.o)
    ET_EXEC   = 0x0002,   // Exécutable
    ET_DYN    = 0x0003,   // Shared object (.so) ou PIE
    ET_CORE   = 0x0004    // Core dump
};

enum EM_Machine : u16 {
    EM_NONE    = 0,
    EM_386     = 3,       // Intel 80386
    EM_ARM     = 40,      // ARM
    EM_X86_64  = 62,      // AMD x86-64
    EM_AARCH64 = 183,     // ARM AARCH64
    EM_RISCV   = 243      // RISC-V
};

struct Elf64_Ehdr {
    ElfIdent  e_ident                [[comment("Identification ELF (16 octets)")]];
    ET_Type   e_type                 [[comment("Type de fichier ELF")]];
    EM_Machine e_machine             [[comment("Architecture cible")]];
    u32       e_version              [[comment("Version ELF (1 = current)")]];
    u64       e_entry                [[format("hex"), comment("Point d'entrée (adresse virtuelle)")]];
    u64       e_phoff                [[format("hex"), comment("Offset de la Program Header Table")]];
    u64       e_shoff                [[format("hex"), comment("Offset de la Section Header Table")]];
    u32       e_flags                [[format("hex"), comment("Flags spécifiques au processeur")]];
    u16       e_ehsize               [[comment("Taille de ce header (64 octets pour ELF64)")]];
    u16       e_phentsize            [[comment("Taille d'une entrée Program Header")]];
    u16       e_phnum                [[comment("Nombre d'entrées Program Header")]];
    u16       e_shentsize            [[comment("Taille d'une entrée Section Header")]];
    u16       e_shnum                [[comment("Nombre d'entrées Section Header")]];
    u16       e_shstrndx             [[comment("Index de la section .shstrtab")]];
};

Elf64_Ehdr elf_header @ 0x00;
```

Évaluez à nouveau. L'arbre montre maintenant l'intégralité du ELF Header sur les 64 premiers octets. Vérifions quelques valeurs en les comparant avec la sortie de `readelf` :

```bash
readelf -h binaries/ch21-keygenme/keygenme_O0
```

Le champ `e_entry` dans ImHex doit correspondre au « Entry point address » de `readelf`. Le champ `e_phoff` doit correspondre au « Start of program headers ». Le champ `e_shoff` doit correspondre au « Start of section headers ». Si les valeurs coïncident, votre pattern est correct.

Notez l'utilisation de `[[format("hex")]]` sur les champs d'adresses et d'offsets. Sans cet attribut, ImHex afficherait ces valeurs en décimal, ce qui est peu naturel pour des adresses mémoire et des offsets dans un fichier. En hexadécimal, une adresse comme `0x401040` est immédiatement reconnaissable.

---

## Étape 3 : la Program Header Table (`Elf64_Phdr`)

Le ELF Header nous donne l'offset de la Program Header Table (`e_phoff`) et le nombre d'entrées (`e_phnum`). Nous pouvons maintenant parser cette table. Chaque entrée est un `Elf64_Phdr` de 56 octets :

```cpp
enum PT_Type : u32 {
    PT_NULL    = 0,        // Entrée inutilisée
    PT_LOAD    = 1,        // Segment chargeable en mémoire
    PT_DYNAMIC = 2,        // Informations de linking dynamique
    PT_INTERP  = 3,        // Chemin vers l'interpréteur (ld.so)
    PT_NOTE    = 4,        // Informations auxiliaires
    PT_SHLIB   = 5,        // Réservé
    PT_PHDR    = 6,        // Entrée pour la table elle-même
    PT_TLS     = 7,        // Thread-Local Storage
    PT_GNU_EH_FRAME = 0x6474E550,
    PT_GNU_STACK    = 0x6474E551,
    PT_GNU_RELRO    = 0x6474E552,
    PT_GNU_PROPERTY = 0x6474E553
};

bitfield PF_Flags {
    execute : 1;
    write   : 1;
    read    : 1;
    padding : 29;
};

struct Elf64_Phdr {
    PT_Type   p_type     [[comment("Type de segment")]];
    PF_Flags  p_flags    [[comment("Permissions : RWX")]];
    u64       p_offset   [[format("hex"), comment("Offset du segment dans le fichier")]];
    u64       p_vaddr    [[format("hex"), comment("Adresse virtuelle en mémoire")]];
    u64       p_paddr    [[format("hex"), comment("Adresse physique (souvent = vaddr)")]];
    u64       p_filesz   [[format("hex"), comment("Taille du segment dans le fichier")]];
    u64       p_memsz    [[format("hex"), comment("Taille du segment en mémoire")]];
    u64       p_align    [[format("hex"), comment("Alignement requis")]];
};
```

Ici, nous introduisons une nouvelle construction : le **bitfield**. Le champ `p_flags` est un masque de bits sur 32 bits où le bit 0 indique l'exécution, le bit 1 l'écriture et le bit 2 la lecture. Plutôt que de déclarer un simple `u32` et de laisser le lecteur décoder mentalement le masque, le `bitfield` décompose les bits individuellement. Dans l'arbre Pattern Data, ImHex affichera `execute = 1`, `write = 0`, `read = 1` — vous voyez immédiatement que le segment est `R-X` (lisible et exécutable, non inscriptible), ce qui est typique d'un segment `.text`.

Pour instancier la table, nous utilisons l'offset et le compteur lus dans le ELF Header :

```cpp
Elf64_Phdr program_headers[elf_header.e_phnum] @ elf_header.e_phoff;
```

Cette ligne dit : « parse `e_phnum` structures `Elf64_Phdr` consécutives, en commençant à l'offset `e_phoff` dans le fichier ». C'est la puissance du placement dynamique avec `@` combiné aux tableaux de taille variable — en une seule ligne, nous parsons toute la Program Header Table.

Évaluez et comparez avec `readelf -l` (qui affiche les Program Headers). Le nombre de segments, leurs types et leurs permissions doivent correspondre.

---

## Étape 4 : la Section Header Table (`Elf64_Shdr`)

Procédons de la même façon pour la Section Header Table. Chaque entrée est un `Elf64_Shdr` de 64 octets :

```cpp
enum SHT_Type : u32 {
    SHT_NULL          = 0,
    SHT_PROGBITS      = 1,     // Code ou données du programme
    SHT_SYMTAB        = 2,     // Table des symboles
    SHT_STRTAB        = 3,     // Table de chaînes
    SHT_RELA          = 4,     // Relocations avec addend
    SHT_HASH          = 5,     // Table de hachage des symboles
    SHT_DYNAMIC       = 6,     // Informations de linking dynamique
    SHT_NOTE          = 7,     // Notes
    SHT_NOBITS        = 8,     // Section sans données dans le fichier (.bss)
    SHT_REL           = 9,     // Relocations sans addend
    SHT_DYNSYM        = 11,    // Table des symboles dynamiques
    SHT_INIT_ARRAY    = 14,    // Tableau de constructeurs
    SHT_FINI_ARRAY    = 15,    // Tableau de destructeurs
    SHT_GNU_HASH      = 0x6FFFFFF6,
    SHT_GNU_VERSYM    = 0x6FFFFFFF,
    SHT_GNU_VERNEED   = 0x6FFFFFFE
};

bitfield SHF_Flags {
    write     : 1;
    alloc     : 1;
    execinstr : 1;
    padding   : 1;
    merge     : 1;
    strings   : 1;
    info_link : 1;
    link_order: 1;
    padding2  : 24;
};

struct Elf64_Shdr {
    u32       sh_name       [[comment("Index dans .shstrtab (nom de la section)")]];
    SHT_Type  sh_type       [[comment("Type de section")]];
    SHF_Flags sh_flags      [[comment("Flags : Write, Alloc, Exec...")]];
    u64       sh_addr       [[format("hex"), comment("Adresse virtuelle si chargée")]];
    u64       sh_offset     [[format("hex"), comment("Offset dans le fichier")]];
    u64       sh_size       [[format("hex"), comment("Taille de la section")]];
    u32       sh_link       [[comment("Index de section liée (dépend du type)")]];
    u32       sh_info       [[comment("Information additionnelle (dépend du type)")]];
    u64       sh_addralign  [[comment("Contrainte d'alignement")]];
    u64       sh_entsize    [[comment("Taille d'une entrée si la section est une table")]];
};

Elf64_Shdr section_headers[elf_header.e_shnum] @ elf_header.e_shoff;
```

Évaluez et comparez avec `readelf -S`. Le nombre de sections, leurs types et leurs tailles doivent correspondre. Vous remarquerez que le champ `sh_name` affiche un entier plutôt qu'un nom de section lisible. C'est normal : `sh_name` est un **index dans la table de chaînes** `.shstrtab`, pas le nom lui-même. Résoudre ces noms nécessite de suivre le pointeur `e_shstrndx` du ELF Header vers la section `.shstrtab` puis de lire la chaîne à l'offset indiqué. C'est faisable en `.hexpat` avec des fonctions avancées, mais cela dépasse le cadre de cette section introductive. Pour l'instant, `readelf -S` reste le meilleur outil pour voir les noms de sections — notre pattern se concentre sur la structure binaire brute.

---

## Le pattern complet assemblé

Regroupons tout dans un fichier unique et cohérent. C'est ce fichier que vous sauvegarderez dans `hexpat/elf_header.hexpat` :

```cpp
// ============================================================
// elf_header.hexpat — Pattern ELF 64 bits pour ImHex
// Formation Reverse Engineering — Chapitre 6
// ============================================================

#include <std/io.pat>

// ────────────────────────────────────────────
//  Enums : e_ident
// ────────────────────────────────────────────

enum EI_CLASS : u8 {
    ELFCLASSNONE = 0,
    ELFCLASS32   = 1,
    ELFCLASS64   = 2
};

enum EI_DATA : u8 {
    ELFDATANONE = 0,
    ELFDATA2LSB = 1,
    ELFDATA2MSB = 2
};

enum EI_OSABI : u8 {
    ELFOSABI_NONE    = 0x00,
    ELFOSABI_HPUX    = 0x01,
    ELFOSABI_NETBSD  = 0x02,
    ELFOSABI_GNU     = 0x03,
    ELFOSABI_SOLARIS = 0x06,
    ELFOSABI_FREEBSD = 0x09
};

// ────────────────────────────────────────────
//  Enums : ELF Header
// ────────────────────────────────────────────

enum ET_Type : u16 {
    ET_NONE = 0x0000,
    ET_REL  = 0x0001,
    ET_EXEC = 0x0002,
    ET_DYN  = 0x0003,
    ET_CORE = 0x0004
};

enum EM_Machine : u16 {
    EM_NONE    = 0,
    EM_386     = 3,
    EM_ARM     = 40,
    EM_X86_64  = 62,
    EM_AARCH64 = 183,
    EM_RISCV   = 243
};

// ────────────────────────────────────────────
//  Enums & bitfields : Program Headers
// ────────────────────────────────────────────

enum PT_Type : u32 {
    PT_NULL         = 0,
    PT_LOAD         = 1,
    PT_DYNAMIC      = 2,
    PT_INTERP       = 3,
    PT_NOTE         = 4,
    PT_SHLIB        = 5,
    PT_PHDR         = 6,
    PT_TLS          = 7,
    PT_GNU_EH_FRAME = 0x6474E550,
    PT_GNU_STACK    = 0x6474E551,
    PT_GNU_RELRO    = 0x6474E552,
    PT_GNU_PROPERTY = 0x6474E553
};

bitfield PF_Flags {
    execute : 1;
    write   : 1;
    read    : 1;
    padding : 29;
};

// ────────────────────────────────────────────
//  Enums & bitfields : Section Headers
// ────────────────────────────────────────────

enum SHT_Type : u32 {
    SHT_NULL       = 0,
    SHT_PROGBITS   = 1,
    SHT_SYMTAB     = 2,
    SHT_STRTAB     = 3,
    SHT_RELA       = 4,
    SHT_HASH       = 5,
    SHT_DYNAMIC    = 6,
    SHT_NOTE       = 7,
    SHT_NOBITS     = 8,
    SHT_REL        = 9,
    SHT_DYNSYM     = 11,
    SHT_INIT_ARRAY = 14,
    SHT_FINI_ARRAY = 15,
    SHT_GNU_HASH    = 0x6FFFFFF6,
    SHT_GNU_VERSYM  = 0x6FFFFFFF,
    SHT_GNU_VERNEED = 0x6FFFFFFE
};

bitfield SHF_Flags {
    write      : 1;
    alloc      : 1;
    execinstr  : 1;
    padding    : 1;
    merge      : 1;
    strings    : 1;
    info_link  : 1;
    link_order : 1;
    padding2   : 24;
};

// ────────────────────────────────────────────
//  Structures
// ────────────────────────────────────────────

struct ElfIdent {
    char     magic[4]      [[comment("0x7f 'E' 'L' 'F'")]];
    EI_CLASS file_class    [[comment("32 ou 64 bits")]];
    EI_DATA  data_encoding [[comment("Endianness")]];
    u8       version       [[comment("EV_CURRENT = 1")]];
    EI_OSABI os_abi;
    u8       abi_version;
    padding[7];
};

struct Elf64_Ehdr {
    ElfIdent   e_ident;
    ET_Type    e_type        [[comment("Type de fichier ELF")]];
    EM_Machine e_machine     [[comment("Architecture cible")]];
    u32        e_version     [[comment("Version ELF")]];
    u64        e_entry       [[format("hex"), comment("Point d'entrée")]];
    u64        e_phoff       [[format("hex"), comment("Offset Program Header Table")]];
    u64        e_shoff       [[format("hex"), comment("Offset Section Header Table")]];
    u32        e_flags       [[format("hex")]];
    u16        e_ehsize      [[comment("Taille de ce header")]];
    u16        e_phentsize   [[comment("Taille d'un Program Header")]];
    u16        e_phnum       [[comment("Nombre de Program Headers")]];
    u16        e_shentsize   [[comment("Taille d'un Section Header")]];
    u16        e_shnum       [[comment("Nombre de Section Headers")]];
    u16        e_shstrndx    [[comment("Index de .shstrtab")]];
};

struct Elf64_Phdr {
    PT_Type  p_type    [[comment("Type de segment")]];
    PF_Flags p_flags   [[comment("Permissions RWX")]];
    u64      p_offset  [[format("hex"), comment("Offset dans le fichier")]];
    u64      p_vaddr   [[format("hex"), comment("Adresse virtuelle")]];
    u64      p_paddr   [[format("hex"), comment("Adresse physique")]];
    u64      p_filesz  [[format("hex"), comment("Taille dans le fichier")]];
    u64      p_memsz   [[format("hex"), comment("Taille en mémoire")]];
    u64      p_align   [[format("hex"), comment("Alignement")]];
};

struct Elf64_Shdr {
    u32       sh_name      [[comment("Index dans .shstrtab")]];
    SHT_Type  sh_type      [[comment("Type de section")]];
    SHF_Flags sh_flags     [[comment("Flags : W, A, X...")]];
    u64       sh_addr      [[format("hex"), comment("Adresse virtuelle")]];
    u64       sh_offset    [[format("hex"), comment("Offset dans le fichier")]];
    u64       sh_size      [[format("hex"), comment("Taille")]];
    u32       sh_link;
    u32       sh_info;
    u64       sh_addralign [[comment("Alignement")]];
    u64       sh_entsize   [[comment("Taille d'entrée si table")]];
};

// ────────────────────────────────────────────
//  Instanciation sur le fichier
// ────────────────────────────────────────────

Elf64_Ehdr elf_header @ 0x00;

Elf64_Phdr program_headers[elf_header.e_phnum] @ elf_header.e_phoff;

Elf64_Shdr section_headers[elf_header.e_shnum] @ elf_header.e_shoff;
```

Trois lignes d'instanciation suffisent pour parser l'intégralité des métadonnées structurelles d'un ELF. Le ELF Header est placé à l'offset 0 (c'est toujours le cas par spécification), puis les deux tables sont placées aux offsets que le header lui-même nous fournit. Le tout pèse environ 150 lignes, commentaires inclus.

---

## Vérification croisée avec `readelf`

Une fois le pattern évalué dans ImHex, prenez l'habitude de **vérifier systématiquement** les valeurs parsées contre un outil de référence. Cette discipline vous évitera de construire des patterns incorrects qui semblent « fonctionner » par coïncidence.

Voici les trois commandes `readelf` à lancer en parallèle et les champs à comparer :

**ELF Header** — `readelf -h <binaire>` :

- « Class » doit correspondre à `e_ident.file_class`  
- « Type » doit correspondre à `e_type`  
- « Machine » doit correspondre à `e_machine`  
- « Entry point address » doit correspondre à `e_entry`  
- « Number of program headers » doit correspondre à `e_phnum`  
- « Number of section headers » doit correspondre à `e_shnum`

**Program Headers** — `readelf -l <binaire>` :

- Le nombre de segments doit correspondre à la taille du tableau `program_headers`  
- Les types (LOAD, DYNAMIC, INTERP…) doivent correspondre aux `p_type` parsés  
- Les offsets et tailles doivent correspondre aux `p_offset` et `p_filesz`

**Section Headers** — `readelf -S <binaire>` :

- Le nombre de sections doit correspondre à la taille du tableau `section_headers`  
- Les types (PROGBITS, STRTAB, SYMTAB…) doivent correspondre aux `sh_type` parsés  
- Les offsets et tailles doivent correspondre aux `sh_offset` et `sh_size`

Si une valeur ne coïncide pas, le problème vient presque toujours d'un **décalage d'alignement** dans votre pattern — un champ oublié ou de mauvaise taille qui décale tous les champs suivants. Revenez à la spécification ELF et comptez les octets.

---

## Ce que nous n'avons pas parsé (et pourquoi)

Notre pattern couvre les trois couches de métadonnées structurelles d'un ELF, mais il ne parse pas le **contenu** des sections elles-mêmes. Nous n'avons pas, par exemple, parsé les entrées de la table des symboles (`.symtab`), les entrées de relocation (`.rela.text`), ni le contenu de la section `.dynamic`.

C'est un choix délibéré. Chacune de ces structures mériterait son propre développement, et certaines (la table des symboles, les relocations) sont mieux explorées avec des outils dédiés comme `readelf -s` ou Ghidra. Notre pattern a un objectif précis : donner une vue structurée et navigable des **métadonnées de haut niveau** d'un ELF, celles qui répondent aux questions « quel type de fichier est-ce ? », « quels segments seront chargés en mémoire ? » et « quelles sections contient-il ? ».

Pour aller plus loin, vous pourriez enrichir ce pattern en parsant la table de chaînes `.shstrtab` pour résoudre les noms de sections, ou en ajoutant des structures conditionnelles pour parser le contenu des sections selon leur type. Le pattern ELF du Content Store d'ImHex illustre ces techniques avancées et constitue une bonne référence si vous souhaitez étendre votre pattern.

---

## Ce que cette construction nous enseigne sur le RE

Au-delà du pattern lui-même, cet exercice illustre une démarche fondamentale du reverse engineering : **suivre la chaîne de pointeurs**. Le ELF Header est le point d'entrée. Il contient des offsets (`e_phoff`, `e_shoff`) et des compteurs (`e_phnum`, `e_shnum`) qui nous dirigent vers les tables suivantes. Chaque Section Header contient à son tour un offset (`sh_offset`) et une taille (`sh_size`) qui pointent vers le contenu réel de la section.

Cette logique de « header → pointeur → données → sous-pointeur → sous-données » se retrouve dans pratiquement tous les formats binaires. Un protocole réseau a un header avec un champ de longueur qui indique la taille du payload. Un exécutable PE a un DOS Header qui pointe vers un PE Header qui pointe vers des Section Headers. Un format de fichier propriétaire a un magic number suivi d'une table des matières qui pointe vers des blocs de données.

Quand vous reverserez un format inconnu aux chapitres 23 et 25, vous chercherez exactement ce même schéma : identifier le point d'entrée, trouver les pointeurs, suivre les chaînes. Le pattern ELF que nous venons de construire est un modèle que vous pourrez adapter à n'importe quel format.

---

## Résumé

Nous avons construit un pattern `.hexpat` complet pour le format ELF 64 bits en quatre étapes : identification (`e_ident`), ELF Header (`Elf64_Ehdr`), Program Headers (`Elf64_Phdr`) et Section Headers (`Elf64_Shdr`). Le pattern utilise des enums pour les valeurs symboliques, des bitfields pour les masques de permissions, des attributs `[[format("hex")]]` et `[[comment(...)]]` pour la lisibilité, et le placement dynamique avec `@` pour suivre les pointeurs du header vers les tables. Le résultat est un fichier de 150 lignes qui transforme un blob hexadécimal en une carte navigable et documentée — exactement le type d'outil que nous construirons pour chaque format binaire rencontré dans la suite de cette formation.

---


⏭️ [Parser une structure C/C++ maison directement dans le binaire](/06-imhex/05-parser-structure-custom.md)
