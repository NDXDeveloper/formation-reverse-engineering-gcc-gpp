/*!
 * hexpat/ch23_protocol.hexpat
 * Pattern ImHex pour le protocole custom ch23-network
 *
 * Formation Reverse Engineering — Chapitre 23
 * Reconstruit par reverse engineering des binaires server et client.
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │  Usage                                                  │
 * │                                                         │
 * │  1. Capturer une session avec Wireshark :               │
 * │     - Filtre : tcp.port == 4444                         │
 * │     - Analyze → Follow → TCP Stream                     │
 * │     - Show data as: Raw → Save as: ch23_stream.bin      │
 * │                                                         │
 * │  2. Ouvrir ch23_stream.bin dans ImHex                   │
 * │                                                         │
 * │  3. Charger ce fichier dans le Pattern Editor (▶)       │
 * │                                                         │
 * │  Résultat : chaque message est décodé, colorisé et      │
 * │  annoté dans la vue hexadécimale et le panneau          │
 * │  Pattern Data.                                          │
 * └─────────────────────────────────────────────────────────┘
 *
 * Protocole reconstruit :
 *
 *   Header (4 octets, commun à tous les messages) :
 *     [magic:1 = 0xC0] [msg_type:1] [payload_len:2 big-endian]
 *
 *   Convention msg_type :
 *     bit 7 = 0 → requête  (client → serveur) : 0x01–0x04
 *     bit 7 = 1 → réponse  (serveur → client) : 0x81–0x84
 *     0xFF      → erreur serveur
 *
 *   Séquence protocolaire :
 *     HELLO_REQ  (0x01) → HELLO_RESP (0x81)  + challenge 8 octets
 *     AUTH_REQ   (0x02) → AUTH_RESP  (0x82)  password XOR-é avec challenge
 *     CMD_REQ    (0x03) → CMD_RESP   (0x83)  commandes : PING/LIST/READ/INFO
 *     QUIT_REQ   (0x04) → QUIT_RESP  (0x84)  déconnexion propre
 *
 *   Commandes (premier octet du payload CMD_REQ) :
 *     0x01 = PING   → réponse "PONG"
 *     0x02 = LIST   → liste des fichiers [count][idx,nlen,name]*
 *     0x03 = READ   → contenu d'un fichier par index
 *     0x04 = INFO   → informations serveur
 *
 * Fichiers associés :
 *   - binaries/ch23-network/server.c   — code source du serveur
 *   - binaries/ch23-network/client.c   — code source du client
 *   - 23-network/03-trames-imhex-hexpat.md — section du tutoriel
 *
 * Licence MIT — Usage strictement éducatif.
 */

#pragma endian big
#pragma pattern_limit 1024

import std.io;
import std.mem;

// ═══════════════════════════════════════════
//  Constantes
// ═══════════════════════════════════════════

#define PROTO_MAGIC    0xC0
#define CHALLENGE_LEN  8

// ═══════════════════════════════════════════
//  Enums
// ═══════════════════════════════════════════

enum MsgType : u8 {
    HELLO_REQ    = 0x01,
    AUTH_REQ     = 0x02,
    CMD_REQ      = 0x03,
    QUIT_REQ     = 0x04,
    HELLO_RESP   = 0x81,
    AUTH_RESP    = 0x82,
    CMD_RESP     = 0x83,
    QUIT_RESP    = 0x84,
    ERROR        = 0xFF
};

enum AuthStatus : u8 {
    AUTH_FAIL = 0x00,
    AUTH_OK   = 0x01
};

enum CmdId : u8 {
    CMD_PING = 0x01,
    CMD_LIST = 0x02,
    CMD_READ = 0x03,
    CMD_INFO = 0x04
};

enum ErrCode : u8 {
    ERR_BAD_MAGIC          = 0x01,
    ERR_BAD_TYPE           = 0x02,
    ERR_WRONG_STATE        = 0x03,
    ERR_AUTH_FAIL          = 0x04,
    ERR_BAD_CMD            = 0x05,
    ERR_PAYLOAD_TOO_LARGE  = 0x06
};

// ═══════════════════════════════════════════
//  Structures de base
// ═══════════════════════════════════════════

/*!
 * Chaîne préfixée par sa longueur (1 octet).
 * Utilisée dans le payload AUTH pour username et password.
 *
 * Format : [len:1][data:len]
 */
struct LPString {
    u8   len                           [[color("AAAAAA")]];
    char value[len]                    [[color("FFD93D")]];
} [[format("format_lpstring")]];

fn format_lpstring(LPString s) {
    return std::format("\"{}\" ({} bytes)", s.value, s.len);
};

/*!
 * Entrée de fichier dans la réponse LIST.
 *
 * Format : [index:1][name_len:1][name:name_len]
 */
struct FileEntry {
    u8   index                         [[color("45B7D1")]];
    u8   name_len                      [[color("AAAAAA")]];
    char name[name_len]                [[color("A8E6CF")]];
} [[format("format_file_entry")]];

fn format_file_entry(FileEntry f) {
    return std::format("[{}] \"{}\"", f.index, f.name);
};

// ═══════════════════════════════════════════
//  En-tête du protocole (4 octets fixes)
// ═══════════════════════════════════════════

/*!
 * Header commun à tous les messages.
 *
 *  Offset  Taille  Champ
 *  0       1       Magic byte (toujours 0xC0)
 *  1       1       Type de message (voir enum MsgType)
 *  2       2       Longueur du payload en big-endian
 */
struct ProtoHeader {
    u8      magic                      [[color("FF6B6B")]];
    MsgType msg_type                   [[color("4ECDC4")]];
    u16     payload_len                [[color("45B7D1")]];
} [[static, format("format_header")]];

fn format_header(ProtoHeader h) {
    return std::format("[0x{:02X}] {} — {} bytes",
                       h.magic, h.msg_type, h.payload_len);
};

// ═══════════════════════════════════════════
//  Payloads — Requêtes (client → serveur)
// ═══════════════════════════════════════════

/*!
 * HELLO Request (type 0x01)
 *
 * Payload : "HELLO" (5 octets) + padding (3 octets de zéros)
 * Total payload_len = 8
 */
struct HelloPayload {
    char identifier[5]                 [[color("A8E6CF")]];

    if (parent.header.payload_len > 5) {
        u8 padding[parent.header.payload_len - 5]
                                       [[color("808080")]];
    }
};

/*!
 * AUTH Request (type 0x02)
 *
 * Payload : deux chaînes length-prefixed :
 *   [user_len:1][username:N][pass_len:1][password_xored:M]
 *
 * Le mot de passe est XOR-é avec le challenge (8 octets)
 * reçu dans le HELLO_RESP. XOR cyclique : pass[i] ^= challenge[i % 8].
 */
struct AuthRequestPayload {
    LPString username;
    LPString password;                 // XOR-é avec le challenge !
};

/*!
 * CMD Request (type 0x03)
 *
 * Payload : [command_id:1][args:N]
 *   command_id : PING=0x01, LIST=0x02, READ=0x03, INFO=0x04
 *   args       : dépend de la commande (ex: READ → [file_index:1])
 */
struct CmdRequestPayload {
    CmdId command_id                   [[color("4ECDC4")]];

    if (parent.header.payload_len > 1) {
        u8 args[parent.header.payload_len - 1]
                                       [[color("FFD93D")]];
    }
};

// ═══════════════════════════════════════════
//  Payloads — Réponses (serveur → client)
// ═══════════════════════════════════════════

/*!
 * HELLO Response (type 0x81)
 *
 * Payload : "WELCOME" (7 octets) + challenge (8 octets aléatoires)
 * Total payload_len = 15 (0x0F)
 *
 * Le challenge est généré par getrandom() côté serveur et change
 * à chaque connexion. Il est utilisé pour XOR-er le mot de passe
 * dans le AUTH_REQ.
 */
struct WelcomePayload {
    char banner[7]                     [[color("A8E6CF")]];
    u8   challenge[CHALLENGE_LEN]      [[color("C9B1FF")]];

    // Absorber d'éventuels octets supplémentaires si payload_len > 15
    if (parent.header.payload_len > 7 + CHALLENGE_LEN) {
        u8 extra[parent.header.payload_len - 7 - CHALLENGE_LEN]
                                       [[color("808080")]];
    }
};

/*!
 * AUTH Response (type 0x82)
 *
 * Payload (2 octets) :
 *   [reserved:1 = 0x00][status:1]
 *   status : 0x00 = AUTH_FAIL, 0x01 = AUTH_OK
 */
struct AuthResponsePayload {
    u8         reserved                [[color("808080")]];
    AuthStatus status                  [[color("FF6B6B")]];
};

/*!
 * CMD Response (type 0x83)
 *
 * Payload : [status:1][data:N]
 *   status = 0x01 (OK) ou 0x00 (FAIL)
 *   data   = dépend de la commande :
 *     PING → "PONG" (4 octets)
 *     LIST → [count:1][FileEntry]*
 *     READ → contenu du fichier (texte brut)
 *     INFO → informations serveur (texte brut)
 */
struct CmdResponsePayload {
    u8 status_code                     [[color("4ECDC4")]];

    if (parent.header.payload_len > 1) {
        u8 data[parent.header.payload_len - 1]
                                       [[color("A8E6CF")]];
    }
};

/*!
 * QUIT Response (type 0x84)
 *
 * Payload : "BYE" (3 octets)
 */
struct QuitResponsePayload {
    char message[parent.header.payload_len]
                                       [[color("A8E6CF")]];
};

/*!
 * ERROR (type 0xFF)
 *
 * Payload : [error_code:1][error_message:N]
 *   error_code : voir enum ErrCode
 *   error_message : texte ASCII décrivant l'erreur
 */
struct ErrorPayload {
    ErrCode error_code                 [[color("FF6B6B")]];

    if (parent.header.payload_len > 1) {
        char error_message[parent.header.payload_len - 1]
                                       [[color("FFAAAA")]];
    }
};

/*!
 * Payload générique pour les types non reconnus.
 * Lit les octets bruts sans interprétation.
 */
struct GenericPayload {
    u8 data[parent.header.payload_len] [[color("CCCCCC")]];
};

// ═══════════════════════════════════════════
//  Message complet (header + payload)
// ═══════════════════════════════════════════

struct ProtoMessage {
    ProtoHeader header;

    // Validation du magic byte — signale un désalignement du flux
    std::assert(header.magic == PROTO_MAGIC,
                "Invalid magic byte — stream may be misaligned");

    if (header.payload_len > 0) {
        match (header.msg_type) {
            // Requêtes (client → serveur)
            (MsgType::HELLO_REQ):    HelloPayload          payload;
            (MsgType::AUTH_REQ):     AuthRequestPayload    payload;
            (MsgType::CMD_REQ):      CmdRequestPayload     payload;
            // QUIT_REQ a un payload vide (payload_len == 0),
            // donc cette branche ne sera jamais atteinte — c'est normal.
            (MsgType::QUIT_REQ):     GenericPayload        payload;

            // Réponses (serveur → client)
            (MsgType::HELLO_RESP):   WelcomePayload        payload;
            (MsgType::AUTH_RESP):    AuthResponsePayload   payload;
            (MsgType::CMD_RESP):     CmdResponsePayload    payload;
            (MsgType::QUIT_RESP):    QuitResponsePayload   payload;

            // Erreur
            (MsgType::ERROR):        ErrorPayload          payload;

            // Type inconnu — fallback générique
            (_):                     GenericPayload        payload;
        }
    }
} [[format("format_message")]];

fn format_message(ProtoMessage m) {
    // Indiquer la direction pour faciliter la lecture
    str direction;
    if (u8(m.header.msg_type) == 0xFF) {
        direction = "ERR";
    } else if ((u8(m.header.msg_type) & 0x80) != 0) {
        direction = "S→C";
    } else {
        direction = "C→S";
    }

    return std::format("[{}] {} — {} bytes payload",
                       direction, m.header.msg_type,
                       m.header.payload_len);
};

// ═══════════════════════════════════════════
//  Point d'entrée — décoder tout le flux
// ═══════════════════════════════════════════

ProtoMessage messages[while($ < std::mem::size())] @ 0x00;
