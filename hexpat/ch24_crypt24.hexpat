// ch24_crypt24.hexpat — Pattern ImHex pour le format CRYPT24
// Formation Reverse Engineering — Chapitre 24
//
// Ce pattern parse les fichiers .enc produits par le binaire ch24-crypto.
//
// Structure du format :
//   [0x00..0x07]  Magic         "CRYPT24\0"           (8 octets)
//   [0x08]        Version maj.  0x01                   (1 octet)
//   [0x09]        Version min.  0x00                   (1 octet)
//   [0x0A..0x0B]  IV length     0x0010 (16)            (uint16 LE)
//   [0x0C..0x1B]  IV            (16 octets aléatoires)
//   [0x1C..0x1F]  Orig. size    (taille du plaintext)  (uint32 LE)
//   [0x20..EOF]   Ciphertext    (AES-256-CBC, PKCS7)
//
// Usage : ouvrir un fichier .enc dans ImHex, charger ce pattern.
//
// Licence MIT — Usage strictement éducatif.

#pragma description "CRYPT24 encrypted file format"
#pragma magic [ 43 52 59 50 54 32 34 00 ]  // "CRYPT24\0"
#pragma endian little

import std.io;
import std.mem;

// ── Types de base ──────────────────────────────────────────────

struct Magic {
    char value[8];
} [[static, color("4A90D9")]];

struct Version {
    u8 major;
    u8 minor;
} [[static, color("50C878")]];

struct IVLength {
    u16 length;
} [[static, color("F5A623")]];

struct IV {
    u8 bytes[parent.iv_length.length];
} [[color("E74C3C")]];

struct OriginalSize {
    u32 size;
} [[static, color("9B59B6")]];

struct CipherData {
    u8 data[std::mem::size() - $];
} [[color("7F8C8D")]];

// ── Structure principale ───────────────────────────────────────

struct Crypt24File {
    Magic       magic;          // 0x00 : "CRYPT24\0" (8 bytes)
    Version     version;        // 0x08 : version majeure.mineure (2 bytes)
    IVLength    iv_length;      // 0x0A : longueur de l'IV (2 bytes)
    IV          iv;             // 0x0C : IV (iv_length.length bytes)
    OriginalSize orig_size;     // 0x1C : taille originale (4 bytes)
    CipherData  ciphertext;     // 0x20 : données chiffrées (reste du fichier)
};

// ── Point d'entrée ─────────────────────────────────────────────

Crypt24File file @ 0x00;

// ── Validations ────────────────────────────────────────────────

std::assert(
    file.magic.value == "CRYPT24\0",
    "Invalid magic: expected CRYPT24"
);

std::assert(
    file.version.major == 1,
    "Unsupported major version"
);

std::assert(
    file.iv_length.length == 8 ||
    file.iv_length.length == 12 ||
    file.iv_length.length == 16,
    "Unexpected IV length"
);

std::assert(
    std::mem::size() - 0x20 != 0,
    "Empty ciphertext"
);

// ── Affichage informatif ───────────────────────────────────────

std::print("=== CRYPT24 File Analysis ===");
std::print("Version:       {}.{}", file.version.major, file.version.minor);
std::print("IV length:     {} bytes", file.iv_length.length);
std::print("Original size: {} bytes", file.orig_size.size);
std::print("Cipher length: {} bytes",
           std::mem::size() - 0x20);
std::print("Padding bytes: {} bytes",
           (std::mem::size() - 0x20) - file.orig_size.size);
