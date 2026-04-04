// ============================================================
// ch06-checkpoint-solution.hexpat
// Solution du checkpoint du chapitre 6
// Format : fichier .pkt (capture de protocole custom ch23)
// Formation Reverse Engineering — Applications compilées GCC
// ============================================================
//
// DESCRIPTION DU FORMAT
// =====================
// Le fichier .pkt est un format de capture propriétaire produit
// par le binaire ch06-fileformat. Il stocke une séquence de
// paquets issus du protocole réseau custom du chapitre 23.
//
// Structure générale :
//
//   ┌──────────────────────────┐  offset 0x00
//   │   FileHeader (32 octets) │
//   ├──────────────────────────┤  offset 0x20
//   │   SessionInfo (48 octets)│
//   ├──────────────────────────┤  offset 0x50
//   │   Packet #1              │
//   │     PacketHeader (20 o.) │
//   │     Payload (variable)   │
//   ├──────────────────────────┤
//   │   Packet #2              │
//   │     ...                  │
//   ├──────────────────────────┤
//   │   ...                    │
//   ├──────────────────────────┤
//   │   FileFooter (16 octets) │
//   └──────────────────────────┘
//
// CONVENTIONS
// ===========
// - Les champs réseau (adresses IP, ports, numéros de séquence)
//   sont en big-endian, conformément à la convention réseau.
// - Les champs de métadonnées du format de fichier lui-même
//   (offsets, tailles, compteurs de structure) sont en
//   little-endian, conformément à la convention x86.
// - Les timestamps sont des Unix timestamps 64 bits (secondes
//   depuis epoch) en little-endian.
//
// ============================================================

#include <std/io.pat>
#include <std/mem.pat>

// ────────────────────────────────────────────
//  Enums
// ────────────────────────────────────────────

// Type de paquet dans le protocole custom ch23.
// Identifié par observation des valeurs récurrentes dans le
// champ packet_type des PacketHeader : les valeurs 0x01 à 0x06
// apparaissent dans les captures, d'autres pourraient exister.
enum PacketType : u8 {
    HANDSHAKE_INIT   = 0x01,   // Client → Serveur : initiation de connexion
    HANDSHAKE_ACK    = 0x02,   // Serveur → Client : accusé de réception
    AUTH_REQUEST     = 0x03,   // Client → Serveur : envoi des credentials
    AUTH_RESPONSE    = 0x04,   // Serveur → Client : résultat de l'auth
    DATA_TRANSFER    = 0x05,   // Bidirectionnel : transfert de données
    DISCONNECT       = 0x06    // Bidirectionnel : fermeture de connexion
};

// Direction du paquet — déduite de l'observation : 0x01 = client
// vers serveur, 0x02 = serveur vers client.
enum PacketDirection : u8 {
    CLIENT_TO_SERVER = 0x01,
    SERVER_TO_CLIENT = 0x02
};

// Status de la réponse d'authentification (dans le payload
// des paquets AUTH_RESPONSE).
enum AuthStatus : u8 {
    AUTH_SUCCESS     = 0x00,
    AUTH_FAILURE     = 0x01,
    AUTH_EXPIRED     = 0x02,
    AUTH_BANNED      = 0x03
};

// Flags de paquet — masque de bits sur 1 octet.
// Identifiés par croisement avec les différents paquets :
// le bit 0 semble indiquer la compression du payload,
// le bit 1 la présence d'un checksum en fin de payload.
bitfield PacketFlags {
    compressed   : 1 [[comment("Payload compressé (non rencontré dans sample.pkt)")]];
    has_checksum : 1 [[comment("Un CRC32 de 4 octets suit le payload")]];
    encrypted    : 1 [[comment("Payload chiffré (non rencontré dans sample.pkt)")]];
    fragmented   : 1 [[comment("Paquet fragmenté (non rencontré)")]];
    reserved     : 4;
};

// Version du protocole — encodée sur 2 octets.
// Observé : major=1, minor=0 dans tous les échantillons.
enum ProtoVersion : u16 {
    V1_0 = 0x0100
};

// ────────────────────────────────────────────
//  Structure : FileHeader (32 octets)
// ────────────────────────────────────────────
// Les 32 premiers octets du fichier .pkt identifient le format,
// sa version, et fournissent les compteurs globaux nécessaires
// au parsing du reste du fichier.
//
// Identification :
//   - Offset 0x00 : magic "PKT\x00" (50 4B 54 00)
//   - Offset 0x04 : version du format de fichier (u16 LE)
//
// Compteurs et offsets :
//   - packet_count  : nombre total de paquets dans le fichier
//   - total_length  : taille totale du fichier en octets
//   - data_offset   : offset du premier paquet (après SessionInfo)
//   - footer_offset : offset du FileFooter en fin de fichier

struct FileHeader {
    char    magic[4]        [[color("FF8844"),
                              comment("Magic 'PKT\\0' — identifie le format .pkt")]];
    u16     format_version  [[comment("Version du format de fichier (1 = actuelle)")]];
    u16     proto_version   [[comment("Version du protocole capturé (0x0100 = v1.0)")]];
    u32     packet_count    [[comment("Nombre total de paquets dans la capture")]];
    u32     total_length    [[format("hex"),
                              comment("Taille totale du fichier (doit correspondre à la taille réelle)")]];
    u32     data_offset     [[format("hex"),
                              comment("Offset du premier PacketHeader")]];
    u32     footer_offset   [[format("hex"),
                              comment("Offset du FileFooter")]];
    u64     capture_start   [[comment("Timestamp Unix du début de la capture")]];
};

// ────────────────────────────────────────────
//  Structure : SessionInfo (48 octets)
// ────────────────────────────────────────────
// Métadonnées de la session réseau capturée.
// Placée immédiatement après le FileHeader.
//
// Les adresses IP et les ports sont en big-endian (convention
// réseau). Les autres champs sont en little-endian.
//
// Note : les adresses IPv4 sont stockées comme 4 octets bruts
// plutôt que comme un u32, car leur représentation naturelle
// est dotted-decimal (ex: 192.168.1.10), pas un entier.

struct IPv4Address {
    u8 octet_1;
    u8 octet_2;
    u8 octet_3;
    u8 octet_4;
};

struct SessionInfo {
    IPv4Address client_ip     [[color("AADDFF"),
                                comment("Adresse IP du client")]];
    be u16      client_port   [[comment("Port source du client (big-endian)")]];
    padding[2]                [[comment("Alignement sur 4 octets")]];
    IPv4Address server_ip     [[color("AADDFF"),
                                comment("Adresse IP du serveur")]];
    be u16      server_port   [[comment("Port du serveur (big-endian)")]];
    padding[2]                [[comment("Alignement sur 4 octets")]];
    u64         session_id    [[format("hex"),
                                comment("Identifiant unique de la session")]];
    u64         capture_end   [[comment("Timestamp Unix de fin de capture")]];
    char        capture_host[16] [[comment("Hostname de la machine de capture, null-terminé")]];
};

// ────────────────────────────────────────────
//  Structure : PacketHeader (20 octets)
// ────────────────────────────────────────────
// Chaque paquet capturé est précédé d'un header de 20 octets
// qui décrit son type, sa direction, sa taille et ses
// métadonnées de séquençage.
//
// Le champ payload_length détermine le nombre d'octets de
// données qui suivent immédiatement ce header. La taille
// totale d'un paquet est donc : 20 + payload_length
// (+ 4 octets de CRC si le flag has_checksum est actif).

struct PacketHeader {
    be u32          sequence_num   [[comment("Numéro de séquence (big-endian, convention protocole)")]];
    PacketType      packet_type    [[color("CC88FF"),
                                     comment("Type de paquet (voir enum PacketType)")]];
    PacketDirection direction      [[comment("Sens du paquet")]];
    PacketFlags     flags          [[comment("Flags : compression, checksum, chiffrement")]];
    padding[1]                     [[comment("Alignement")]];
    u32             payload_length [[comment("Taille du payload en octets (sans le CRC)")]];
    u32             timestamp_delta [[comment("Delta en ms depuis capture_start")]];
};

// ────────────────────────────────────────────
//  Structures de payload par type de paquet
// ────────────────────────────────────────────
// Le contenu du payload dépend du packet_type. Les structures
// ci-dessous modélisent les formats observés dans sample.pkt.
// Pour les types non observés ou les payloads de taille
// variable non structurée, on tombe sur le cas par défaut
// (tableau de u8 bruts).

// Payload HANDSHAKE_INIT (Client → Serveur)
// Le client envoie sa version de protocole et un nonce
// aléatoire de 16 octets pour l'échange initial.
struct PayloadHandshakeInit {
    be u16 proto_version   [[comment("Version du protocole demandée par le client")]];
    u8     nonce[16]       [[color("66DD66"),
                             comment("Nonce aléatoire (16 octets) pour le handshake")]];
    padding[2]             [[comment("Alignement")]];
};

// Payload HANDSHAKE_ACK (Serveur → Client)
// Le serveur confirme la version et renvoie son propre nonce.
struct PayloadHandshakeAck {
    be u16 proto_version   [[comment("Version acceptée par le serveur")]];
    u8     server_nonce[16] [[color("66DD66"),
                              comment("Nonce serveur (16 octets)")]];
    be u32 session_token   [[format("hex"),
                             comment("Token de session attribué par le serveur")]];
};

// Payload AUTH_REQUEST (Client → Serveur)
// Le client envoie un nom d'utilisateur et un hash de mot
// de passe (SHA-256, 32 octets).
struct PayloadAuthRequest {
    char username[32]     [[color("FFEE55"),
                            comment("Nom d'utilisateur, null-terminé, paddé à 32 octets")]];
    u8   password_hash[32] [[color("FF6666"),
                             comment("SHA-256 du mot de passe (32 octets)")]];
};

// Payload AUTH_RESPONSE (Serveur → Client)
// Le serveur renvoie le résultat de l'authentification.
struct PayloadAuthResponse {
    AuthStatus status     [[comment("Résultat de l'authentification")]];
    padding[3]            [[comment("Alignement sur 4 octets")]];
    be u32 user_id        [[comment("ID utilisateur attribué (0 si échec)")]];
    char   message[32]    [[color("FFEE55"),
                            comment("Message textuel ('Welcome', 'Access denied'...)")]];
};

// Payload DATA_TRANSFER (bidirectionnel)
// Transfert de données applicatives. Le contenu est un blob
// opaque dont la sémantique dépend de l'application.
// On parse les premiers champs structurés identifiés, le
// reste est un buffer brut.
struct PayloadDataTransfer {
    be u16 data_type      [[comment("Sous-type de données applicatives")]];
    be u16 fragment_id    [[comment("ID de fragment (0 si non fragmenté)")]];
    // Le reste du payload est un buffer de données brut.
    // Sa taille est : payload_length - 4 (les 4 octets ci-dessus).
    // Note : on ne peut pas calculer cette taille directement ici
    // car on n'a pas accès au parent. On utilise le parsing
    // conditionnel dans la structure Packet à la place.
};

// Payload DISCONNECT (bidirectionnel)
// Simple notification de déconnexion avec un code de raison.
struct PayloadDisconnect {
    be u16 reason_code    [[comment("Code de raison : 0=normal, 1=timeout, 2=error")]];
    char   reason_text[30] [[color("FFEE55"),
                             comment("Description textuelle de la raison")]];
};

// ────────────────────────────────────────────
//  Structure : Packet (taille variable)
// ────────────────────────────────────────────
// Un paquet complet = PacketHeader + Payload + CRC optionnel.
//
// Le payload est parsé conditionnellement selon le packet_type.
// Pour les types dont la structure interne est connue, on
// utilise la structure typée correspondante. Pour les types
// inconnus ou les payloads dont la taille ne correspond pas
// à une structure fixe, on parse en octets bruts.
//
// Si le flag has_checksum est actif, un CRC32 de 4 octets
// suit immédiatement le payload.

struct Packet {
    PacketHeader hdr;

    // --- Payload conditionnel selon le type de paquet ---

    if (hdr.packet_type == PacketType::HANDSHAKE_INIT &&
        hdr.payload_length == 20) {
        PayloadHandshakeInit payload;

    } else if (hdr.packet_type == PacketType::HANDSHAKE_ACK &&
               hdr.payload_length == 22) {
        PayloadHandshakeAck payload;

    } else if (hdr.packet_type == PacketType::AUTH_REQUEST &&
               hdr.payload_length == 64) {
        PayloadAuthRequest payload;

    } else if (hdr.packet_type == PacketType::AUTH_RESPONSE &&
               hdr.payload_length == 40) {
        PayloadAuthResponse payload;

    } else if (hdr.packet_type == PacketType::DATA_TRANSFER &&
               hdr.payload_length >= 4) {
        // Pour DATA_TRANSFER, on parse le sous-header de 4 octets
        // puis le reste en octets bruts.
        PayloadDataTransfer data_header;
        u8 data_body[hdr.payload_length - 4]
            [[color("BBBBBB"), comment("Données applicatives brutes")]];

    } else if (hdr.packet_type == PacketType::DISCONNECT &&
               hdr.payload_length == 32) {
        PayloadDisconnect payload;

    } else {
        // Type inconnu ou taille inattendue — parsing brut
        u8 raw_payload[hdr.payload_length]
            [[color("BBBBBB"), comment("Payload brut (type non reconnu)")]];
    }

    // --- CRC32 optionnel ---
    if (hdr.flags.has_checksum == 1) {
        u32 crc32 [[format("hex"), color("66DD66"),
                     comment("CRC32 du payload (vérification d'intégrité)")]];
    }
};

// ────────────────────────────────────────────
//  Structure : FileFooter (16 octets)
// ────────────────────────────────────────────
// Le footer clôt le fichier et fournit des champs de
// vérification d'intégrité : le nombre de paquets (doit
// correspondre à FileHeader.packet_count), le nombre total
// d'octets de payload capturés, et un magic de fin.

struct FileFooter {
    u32  total_payload_bytes [[format("hex"),
                               comment("Somme des payload_length de tous les paquets")]];
    u32  packet_count_check  [[comment("Doit correspondre à FileHeader.packet_count")]];
    u32  file_crc32          [[format("hex"),
                               comment("CRC32 de tout le fichier hors footer (vérification globale)")]];
    char magic[4]            [[color("FF8844"),
                               comment("Magic de fin 'TKP\\0' (inverse de 'PKT\\0')")]];
};

// ────────────────────────────────────────────
//  Instanciation sur le fichier
// ────────────────────────────────────────────

// 1. Header global à l'offset 0
FileHeader file_header @ 0x00;

// 2. Informations de session juste après le header (offset 0x20)
SessionInfo session @ sizeof(file_header);

// 3. Tableau de paquets — placé à l'offset indiqué par le header.
//    Chaque Packet a une taille variable (header + payload + CRC
//    optionnel), mais ImHex gère les tableaux de structures à
//    taille variable : il parse chaque élément séquentiellement,
//    chacun avançant le curseur de sa propre taille.
Packet packets[file_header.packet_count] @ file_header.data_offset;

// 4. Footer — placé à l'offset indiqué par le header.
FileFooter file_footer @ file_header.footer_offset;

// ────────────────────────────────────────────
//  Assertions de cohérence (optionnel)
// ────────────────────────────────────────────
// Ces vérifications produisent un warning dans la console
// ImHex si les valeurs ne correspondent pas. Elles ne bloquent
// pas le parsing mais signalent un fichier potentiellement
// corrompu ou une erreur dans le pattern.

std::print("=== Vérifications de cohérence ===");
std::print("  Taille du fichier     : 0x{:X}", std::mem::size());
std::print("  total_length (header) : 0x{:X}", file_header.total_length);
std::print("  packet_count (header) : {}", file_header.packet_count);
std::print("  packet_count (footer) : {}", file_footer.packet_count_check);

if (file_header.total_length != std::mem::size()) {
    std::print("  [!] ATTENTION : total_length ne correspond pas a la taille du fichier");
}

if (file_header.packet_count != file_footer.packet_count_check) {
    std::print("  [!] ATTENTION : packet_count header/footer incoherents");
}

std::print("=== Parsing termine avec succes ===");
