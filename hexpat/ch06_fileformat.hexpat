// ============================================================
// ch06_fileformat.hexpat
// Pattern ImHex pour le format .cdb (Custom Database)
// Formation Reverse Engineering — Chapitre 6, section 6.11
//
// Ce pattern parse les fichiers sample.cdb produits par le
// binaire ch06-fileformat.
//
// Structure du format :
//   FileHeader      (32 octets)  — magic "CDB2", version, compteurs
//   FieldDescriptor[] (N × 32 o) — description de chaque champ
//   Padding          (variable)  — alignement sur 32 octets
//   Record[]         (N × 68 o)  — données de la base
//   Footer           (12 octets) — magic "CDB2", compteurs de validation
//
// Usage : ouvrir sample.cdb dans ImHex, charger ce pattern.
// ============================================================

#include <std/io.pat>

// ────────────────────────────────────────────
//  Enums
// ────────────────────────────────────────────

enum FieldType : u16 {
    STRING  = 0x0001,
    TEXT    = 0x0002,
    INTEGER = 0x0003
};

// ────────────────────────────────────────────
//  Structure : CDB_Header (32 octets)
// ────────────────────────────────────────────

struct CDB_Header {
    char   magic[4]        [[color("FF4444"),
                             comment("Magic 'CDB2' — identifie le format")]];
    u16    version         [[comment("Version majeure du format")]];
    u16    sub_version     [[comment("Version mineure du format")]];
    u32    record_count    [[comment("Nombre de records dans la base")]];
    u32    data_offset     [[format("hex"),
                             comment("Offset des données (records) depuis le début du fichier")]];
    u32    unknown_10      [[comment("Champ réservé (observé : toujours 1)")]];
    u32    field_count     [[comment("Nombre de descripteurs de champs")]];
    u8     reserved[8]     [[comment("Octets réservés (toujours nuls)")]];
};

// ────────────────────────────────────────────
//  Structure : CDB_FieldDescriptor (32 octets)
// ────────────────────────────────────────────

struct CDB_FieldDescriptor {
    u16       field_id     [[comment("Identifiant numérique du champ")]];
    FieldType field_type   [[comment("Type de données du champ")]];
    char      field_name[20] [[comment("Nom du champ (null-terminé)")]];
    u32       max_size     [[comment("Taille maximale de la valeur du champ")]];
    u32       flags        [[format("hex"),
                             comment("Flags du champ (observé : 0x01 = indexé)")]];
};

// ────────────────────────────────────────────
//  Structure : CDB_Record (68 octets)
// ────────────────────────────────────────────

struct CDB_Record {
    u32  record_id     [[comment("Identifiant unique du record (auto-incrémenté)")]];
    char name[20]      [[color("44FF44"),
                         comment("Champ 'name' — chaîne de 20 octets max")]];
    char description[40] [[color("44AAFF"),
                           comment("Champ 'description' — chaîne de 40 octets max")]];
    u32  value         [[comment("Champ 'value' — entier 32 bits")]];
};

// ────────────────────────────────────────────
//  Structure : CDB_Footer (12 octets)
// ────────────────────────────────────────────

struct CDB_Footer {
    char magic[4]          [[color("FF4444"),
                             comment("Magic de fin — doit correspondre au magic du header")]];
    u32  record_count      [[comment("Nombre de records (validation croisée avec le header)")]];
    u32  data_end_offset   [[format("hex"),
                             comment("Offset de fin des données")]];
};

// ────────────────────────────────────────────
//  Instanciation sur le fichier
// ────────────────────────────────────────────

CDB_Header header @ 0x00;

CDB_FieldDescriptor field_descriptors[header.field_count] @ sizeof(CDB_Header);

// Padding entre les descripteurs et les données
u8 padding[header.data_offset - sizeof(CDB_Header) - header.field_count * sizeof(CDB_FieldDescriptor)]
    @ sizeof(CDB_Header) + header.field_count * sizeof(CDB_FieldDescriptor)
    [[comment("Padding d'alignement avant les données")]];

CDB_Record records[header.record_count] @ header.data_offset;

CDB_Footer footer @ header.data_offset + header.record_count * sizeof(CDB_Record);

// ────────────────────────────────────────────
//  Informations de débogage
// ────────────────────────────────────────────

std::print("=== CDB File Summary ===");
std::print("  Magic       : {}", header.magic);
std::print("  Version     : {}.{}", header.version, header.sub_version);
std::print("  Fields      : {}", header.field_count);
std::print("  Records     : {}", header.record_count);
std::print("  Data offset : 0x{:X}", header.data_offset);
