// ============================================================
// elf_header.hexpat — Pattern ELF 64 bits pour ImHex
// Formation Reverse Engineering — Chapitre 6
//
// Parse les trois couches de métadonnées structurelles d'un
// fichier ELF 64 bits :
//   1. ELF Header (Elf64_Ehdr) — 64 octets à l'offset 0
//   2. Program Header Table (Elf64_Phdr[]) — segments
//   3. Section Header Table (Elf64_Shdr[]) — sections
//
// Référence : ELF specification (Tool Interface Standard),
//             System V ABI AMD64 Architecture Supplement
//
// Vérification croisée :
//   readelf -h <binaire>   → comparer avec elf_header
//   readelf -l <binaire>   → comparer avec program_headers
//   readelf -S <binaire>   → comparer avec section_headers
//
// Limitations :
//   - ELF 64 bits uniquement (pas de support ELF 32 bits)
//   - Ne parse pas le contenu des sections (.symtab,
//     .strtab, .rela, .dynamic, etc.)
//   - Ne résout pas les noms de sections (sh_name reste un
//     index numérique dans .shstrtab)
// ============================================================

#include <std/io.pat>

// ────────────────────────────────────────────
//  Enums : e_ident (16 premiers octets)
// ────────────────────────────────────────────

enum EI_CLASS : u8 {
    ELFCLASSNONE = 0,        // Invalide
    ELFCLASS32   = 1,        // ELF 32 bits
    ELFCLASS64   = 2         // ELF 64 bits
};

enum EI_DATA : u8 {
    ELFDATANONE = 0,         // Invalide
    ELFDATA2LSB = 1,         // Little-endian
    ELFDATA2MSB = 2          // Big-endian
};

enum EI_OSABI : u8 {
    ELFOSABI_NONE    = 0x00, // UNIX System V (valeur par défaut)
    ELFOSABI_HPUX    = 0x01, // HP-UX
    ELFOSABI_NETBSD  = 0x02, // NetBSD
    ELFOSABI_GNU     = 0x03, // GNU/Linux (alias ELFOSABI_LINUX)
    ELFOSABI_SOLARIS = 0x06, // Solaris
    ELFOSABI_FREEBSD = 0x09  // FreeBSD
};

// ────────────────────────────────────────────
//  Enums : ELF Header
// ────────────────────────────────────────────

enum ET_Type : u16 {
    ET_NONE = 0x0000,        // Aucun type
    ET_REL  = 0x0001,        // Fichier relocatable (.o)
    ET_EXEC = 0x0002,        // Exécutable (adresses fixes)
    ET_DYN  = 0x0003,        // Shared object (.so) ou PIE
    ET_CORE = 0x0004         // Core dump
};

enum EM_Machine : u16 {
    EM_NONE    = 0,          // Aucune architecture
    EM_386     = 3,          // Intel 80386
    EM_ARM     = 40,         // ARM
    EM_X86_64  = 62,         // AMD x86-64
    EM_AARCH64 = 183,        // ARM AARCH64
    EM_RISCV   = 243         // RISC-V
};

// ────────────────────────────────────────────
//  Enums : Program Headers (segments)
// ────────────────────────────────────────────

enum PT_Type : u32 {
    PT_NULL         = 0,           // Entrée inutilisée
    PT_LOAD         = 1,           // Segment chargeable en mémoire
    PT_DYNAMIC      = 2,           // Informations de linking dynamique
    PT_INTERP       = 3,           // Chemin vers l'interpréteur (ld.so)
    PT_NOTE         = 4,           // Informations auxiliaires
    PT_SHLIB        = 5,           // Réservé (non utilisé)
    PT_PHDR         = 6,           // Entrée décrivant la table elle-même
    PT_TLS          = 7,           // Thread-Local Storage
    PT_GNU_EH_FRAME = 0x6474E550,  // Frame d'exception GNU
    PT_GNU_STACK    = 0x6474E551,  // Permissions de la pile GNU
    PT_GNU_RELRO    = 0x6474E552,  // Read-only après relocation
    PT_GNU_PROPERTY = 0x6474E553   // Propriétés GNU
};

// Permissions des segments — masque de bits sur 32 bits.
// Bit 0 = Execute, Bit 1 = Write, Bit 2 = Read.
// Exemples courants :
//   R-X (5) = code exécutable (.text)
//   RW- (6) = données inscriptibles (.data, .bss)
//   R-- (4) = données en lecture seule (.rodata)
bitfield PF_Flags {
    execute : 1;
    write   : 1;
    read    : 1;
    padding : 29;
};

// ────────────────────────────────────────────
//  Enums : Section Headers (sections)
// ────────────────────────────────────────────

enum SHT_Type : u32 {
    SHT_NULL       = 0,            // Entrée inutilisée (index 0)
    SHT_PROGBITS   = 1,            // Code ou données du programme
    SHT_SYMTAB     = 2,            // Table des symboles (complète)
    SHT_STRTAB     = 3,            // Table de chaînes
    SHT_RELA       = 4,            // Relocations avec addend explicite
    SHT_HASH       = 5,            // Table de hachage des symboles
    SHT_DYNAMIC    = 6,            // Informations de linking dynamique
    SHT_NOTE       = 7,            // Notes (build-id, ABI, etc.)
    SHT_NOBITS     = 8,            // Section sans données dans le fichier (.bss)
    SHT_REL        = 9,            // Relocations sans addend
    SHT_DYNSYM     = 11,           // Table des symboles dynamiques
    SHT_INIT_ARRAY = 14,           // Tableau de pointeurs de constructeurs
    SHT_FINI_ARRAY = 15,           // Tableau de pointeurs de destructeurs
    SHT_GNU_HASH    = 0x6FFFFFF6,  // Table de hachage GNU (plus rapide que SHT_HASH)
    SHT_GNU_VERSYM  = 0x6FFFFFFF,  // Symboles de versionnement
    SHT_GNU_VERNEED = 0x6FFFFFFE   // Dépendances de versions requises
};

// Flags de section — masque de bits sur 32 bits.
// Les trois premiers bits sont les plus courants :
//   SHF_WRITE (1)     = section inscriptible en mémoire
//   SHF_ALLOC (2)     = section chargée en mémoire à l'exécution
//   SHF_EXECINSTR (4) = section contenant du code exécutable
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
//  Structure : ELF Identification (e_ident)
// ────────────────────────────────────────────
// Les 16 premiers octets de tout fichier ELF. Contient le
// magic number (7f 45 4c 46 = "\x7fELF"), la classe (32/64),
// l'endianness, la version et l'OS/ABI cible.

struct ElfIdent {
    char     magic[4]      [[color("FF4444"),
                             comment("Magic ELF : 0x7F 'E' 'L' 'F'")]];
    EI_CLASS file_class    [[comment("Classe : ELFCLASS32 (1) ou ELFCLASS64 (2)")]];
    EI_DATA  data_encoding [[comment("Endianness : LSB (1) = little, MSB (2) = big")]];
    u8       version       [[comment("Version ELF : toujours EV_CURRENT (1)")]];
    EI_OSABI os_abi        [[comment("OS/ABI cible (NONE = System V générique)")]];
    u8       abi_version   [[comment("Version de l'ABI (généralement 0)")]];
    padding[7]             [[comment("Padding réservé (EI_PAD, 7 octets nuls)")]];
};

// ────────────────────────────────────────────
//  Structure : ELF Header (Elf64_Ehdr)
// ────────────────────────────────────────────
// 64 octets à l'offset 0 du fichier. Point d'entrée de
// toute l'arborescence de métadonnées ELF.
//
// Les champs e_phoff / e_phnum pointent vers la Program
// Header Table (segments). Les champs e_shoff / e_shnum
// pointent vers la Section Header Table (sections).

struct Elf64_Ehdr {
    ElfIdent   e_ident     [[comment("Bloc d'identification ELF (16 octets)")]];
    ET_Type    e_type      [[comment("Type de fichier ELF")]];
    EM_Machine e_machine   [[comment("Architecture cible du processeur")]];
    u32        e_version   [[comment("Version ELF (1 = EV_CURRENT)")]];
    u64        e_entry     [[format("hex"),
                             comment("Adresse virtuelle du point d'entrée (_start)")]];
    u64        e_phoff     [[format("hex"),
                             comment("Offset (dans le fichier) de la Program Header Table")]];
    u64        e_shoff     [[format("hex"),
                             comment("Offset (dans le fichier) de la Section Header Table")]];
    u32        e_flags     [[format("hex"),
                             comment("Flags spécifiques au processeur (souvent 0 sur x86-64)")]];
    u16        e_ehsize    [[comment("Taille de ce header ELF (64 octets pour ELF64)")]];
    u16        e_phentsize [[comment("Taille d'une entrée Program Header (56 octets)")]];
    u16        e_phnum     [[comment("Nombre d'entrées dans la Program Header Table")]];
    u16        e_shentsize [[comment("Taille d'une entrée Section Header (64 octets)")]];
    u16        e_shnum     [[comment("Nombre d'entrées dans la Section Header Table")]];
    u16        e_shstrndx  [[comment("Index du Section Header de .shstrtab (table des noms de sections)")]];
};

// ────────────────────────────────────────────
//  Structure : Program Header (Elf64_Phdr)
// ────────────────────────────────────────────
// 56 octets par entrée. Décrit un segment que le loader
// (ld.so) doit charger en mémoire.
//
// Champs clés pour le RE :
//   p_type   — type de segment (LOAD, DYNAMIC, INTERP…)
//   p_flags  — permissions RWX
//   p_vaddr  — adresse virtuelle de chargement
//   p_offset — offset du segment dans le fichier
//   p_filesz — taille dans le fichier (peut être < p_memsz)
//   p_memsz  — taille en mémoire (si > filesz, le delta est du .bss)

struct Elf64_Phdr {
    PT_Type  p_type   [[comment("Type de segment")]];
    PF_Flags p_flags  [[comment("Permissions : bits R(4) W(2) X(1)")]];
    u64      p_offset [[format("hex"),
                        comment("Offset du début du segment dans le fichier")]];
    u64      p_vaddr  [[format("hex"),
                        comment("Adresse virtuelle de chargement en mémoire")]];
    u64      p_paddr  [[format("hex"),
                        comment("Adresse physique (= vaddr sur la plupart des systèmes)")]];
    u64      p_filesz [[format("hex"),
                        comment("Taille du segment dans le fichier")]];
    u64      p_memsz  [[format("hex"),
                        comment("Taille du segment en mémoire (>= filesz)")]];
    u64      p_align  [[format("hex"),
                        comment("Contrainte d'alignement (puissance de 2)")]];
};

// ────────────────────────────────────────────
//  Structure : Section Header (Elf64_Shdr)
// ────────────────────────────────────────────
// 64 octets par entrée. Décrit une section du fichier ELF.
//
// Note : le champ sh_name est un index (en octets) dans la
// table de chaînes .shstrtab, PAS le nom de la section
// lui-même. Pour résoudre les noms, il faudrait parser
// .shstrtab et lire la chaîne à l'offset sh_name — ce que
// ce pattern ne fait pas (voir « Limitations » en en-tête).
//
// Utiliser `readelf -S` pour voir les noms résolus.

struct Elf64_Shdr {
    u32       sh_name      [[comment("Index dans .shstrtab (utiliser readelf -S pour le nom)")]];
    SHT_Type  sh_type      [[comment("Type de section")]];
    SHF_Flags sh_flags     [[comment("Flags : W(rite), A(lloc), X(ecinstr)")]];
    u64       sh_addr      [[format("hex"),
                             comment("Adresse virtuelle si la section est chargée (sinon 0)")]];
    u64       sh_offset    [[format("hex"),
                             comment("Offset de la section dans le fichier")]];
    u64       sh_size      [[format("hex"),
                             comment("Taille de la section en octets")]];
    u32       sh_link      [[comment("Index de section liée (sémantique dépend du type)")]];
    u32       sh_info      [[comment("Info additionnelle (sémantique dépend du type)")]];
    u64       sh_addralign [[comment("Contrainte d'alignement (0 ou 1 = pas de contrainte)")]];
    u64       sh_entsize   [[comment("Taille d'une entrée si la section est une table (sinon 0)")]];
};

// ────────────────────────────────────────────
//  Instanciation sur le fichier
// ────────────────────────────────────────────
// Trois lignes suffisent pour parser l'intégralité des
// métadonnées structurelles d'un ELF 64 bits :
//
//   1. Le ELF Header est toujours à l'offset 0.
//   2. La Program Header Table est à l'offset e_phoff,
//      avec e_phnum entrées.
//   3. La Section Header Table est à l'offset e_shoff,
//      avec e_shnum entrées.

Elf64_Ehdr elf_header @ 0x00;

Elf64_Phdr program_headers[elf_header.e_phnum] @ elf_header.e_phoff;

Elf64_Shdr section_headers[elf_header.e_shnum] @ elf_header.e_shoff;

// ────────────────────────────────────────────
//  Informations de débogage
// ────────────────────────────────────────────

std::print("=== ELF Header Summary ===");
std::print("  Class       : {}", elf_header.e_ident.file_class);
std::print("  Encoding    : {}", elf_header.e_ident.data_encoding);
std::print("  Type        : {}", elf_header.e_type);
std::print("  Machine     : {}", elf_header.e_machine);
std::print("  Entry point : 0x{:X}", elf_header.e_entry);
std::print("  PH count    : {}", elf_header.e_phnum);
std::print("  SH count    : {}", elf_header.e_shnum);
std::print("  File size   : 0x{:X} ({} bytes)", std::mem::size(), std::mem::size());
