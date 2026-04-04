/*
 * ch25_fileformat.hexpat — Pattern ImHex pour le format CFR v2
 *
 * Solution du checkpoint du chapitre 25.
 * Licence MIT — Usage strictement éducatif.
 *
 * Applique sur : demo.cfr, packed_noxor.cfr, packed_xor.cfr
 * Produit une colorisation complète sans octet orphelin.
 *
 * Format CFR v2 :
 *   [Header 32 octets]
 *   [Record 0 .. N-1]  (taille variable chacun)
 *   [Footer 12 octets]  (optionnel, si flags.has_footer == 1)
 */

#pragma endian little
#pragma pattern_limit 131072

import std.mem;

// ═══════════════════════════════════════════════════════════
//  Constantes
// ═══════════════════════════════════════════════════════════

#define HDR_MAGIC "CFRM"
#define FTR_MAGIC "CRFE"
#define HEADER_SIZE 32
#define FOOTER_SIZE 12

// ═══════════════════════════════════════════════════════════
//  Enums & Bitfields
// ═══════════════════════════════════════════════════════════

enum RecordType : u8 {
    TEXT   = 0x01,
    BINARY = 0x02,
    META   = 0x03
    // Valeurs > 0x03 acceptées par le parseur (traitées comme BINARY opaque)
};

bitfield HeaderFlags {
    xor_enabled : 1;    // bit 0 — données XOR avec clé rotative {5A, 3C, 96, F1}
    has_footer  : 1;    // bit 1 — footer CRFE présent en fin de fichier
    reserved    : 14;   // bits 2-15 — réservés (doivent être à 0)
};

// ═══════════════════════════════════════════════════════════
//  Header (32 octets fixes)
//
//  Offset  Taille  Champ
//  0x00    4       magic           "CFRM"
//  0x04    2       version         0x0002
//  0x06    2       flags           bitfield (XOR, FOOTER)
//  0x08    4       num_records     nombre de records
//  0x0C    4       timestamp       UNIX epoch (secondes)
//  0x10    4       header_crc      CRC-32 des octets [0x00..0x0F]
//  0x14    8       author          ASCII, null-padded
//  0x1C    4       data_len_xor    XOR de tous les data_len
// ═══════════════════════════════════════════════════════════

struct CFRHeader {
    char        magic[4]
        [[comment("Identifiant du format, doit être \"CFRM\" (0x4346524D)")]];

    u16         version
        [[comment("Version du format (ce pattern couvre v2 = 0x0002)")]];

    HeaderFlags flags
        [[comment("Bit 0: XOR sur les données / Bit 1: footer présent")]];

    u32         num_records
        [[comment("Nombre d'enregistrements (max 1024)")]];

    u32         timestamp
        [[comment("Date de création (UNIX epoch, secondes)")]];

    u32         header_crc
        [[comment("CRC-32 (poly 0xEDB88320, init 0xFFFFFFFF) des octets [0x00..0x0F]")]];

    char        author[8]
        [[comment("Auteur de l'archive, ASCII, complété par des 0x00")]];

    u32         data_len_xor
        [[comment("XOR de tous les champs data_len des records")]];
};

// ═══════════════════════════════════════════════════════════
//  Record (taille variable)
//
//  Layout de chaque record :
//    [RecordHeader  8 octets]
//    [name          name_len octets, ASCII, jamais XOR-é]
//    [data          data_len octets, XOR si flags.xor_enabled]
//    [crc16         2 octets, CRC-16/CCITT init=0x1D0F]
//
//  Le CRC-16 est calculé sur (name || data_ORIGINAL),
//  c'est-à-dire sur les données AVANT la transformation XOR.
// ═══════════════════════════════════════════════════════════

struct RecordHeader {
    RecordType type
        [[comment("0x01=TEXT, 0x02=BINARY, 0x03=META")]];

    u8 flags
        [[comment("Réservé, devrait être 0x00 (ignoré par le parseur)")]];

    u16 name_len
        [[comment("Longueur du nom en octets (peut être 0)")]];

    u32 data_len
        [[comment("Longueur du payload en octets (peut être 0)")]];
};

struct Record {
    RecordHeader rh;

    char name[rh.name_len]
        [[comment("Nom ASCII, NON null-terminé, NON soumis au XOR")]];

    u8 data[rh.data_len]
        [[comment("Payload. Si header.flags.xor_enabled: XOR clé rotative {5A,3C,96,F1}. Sinon: données en clair.")]];

    u16 crc16
        [[comment("CRC-16 (poly 0x1021, init 0x1D0F, pas de XOR final) calculé sur name||data_original (avant XOR)")]];
};

// ═══════════════════════════════════════════════════════════
//  Footer (12 octets, optionnel)
//
//  Présent uniquement si header.flags.has_footer == 1.
//  Situé immédiatement après le dernier record.
//
//  Offset  Taille  Champ
//  +0x00   4       magic           "CRFE"
//  +0x04   4       total_size      taille totale du fichier (header+records+footer)
//  +0x08   4       global_crc      CRC-32 de tous les octets avant le footer
// ═══════════════════════════════════════════════════════════

struct CFRFooter {
    char magic[4]
        [[comment("Identifiant du footer, doit être \"CRFE\" (0x43524645)")]];

    u32 total_size
        [[comment("Taille totale du fichier en octets (doit == taille réelle)")]];

    u32 global_crc
        [[comment("CRC-32 (même algo que header_crc) de tout ce qui précède le footer")]];
};

// ═══════════════════════════════════════════════════════════
//  Instanciation
//
//  Le header est placé à l'offset 0.
//  Les records suivent immédiatement (offset 0x20 = 32).
//  Le footer est placé en fin de fichier si le flag est actif.
//
//  Vérification de couverture :
//    header (32) + Σ records + footer (12 si présent) == taille du fichier
// ═══════════════════════════════════════════════════════════

CFRHeader header @ 0x00;

Record records[header.num_records] @ HEADER_SIZE;

if (header.flags.has_footer) {
    CFRFooter footer @ (std::mem::size() - FOOTER_SIZE);
}
