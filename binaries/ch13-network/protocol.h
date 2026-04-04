/**
 * binaries/ch13-network/protocol.h
 *
 * Définition du protocole custom "GCRP" (GCC-RE Protocol)
 * utilisé entre le client et le serveur d'entraînement.
 *
 * Format d'un paquet :
 * ┌──────────┬─────────┬──────────┬──────────────────────┐
 * │ magic(4) │ type(1) │ len(2)   │ payload(0..1024)     │
 * └──────────┴─────────┴──────────┴──────────────────────┘
 *
 * - magic   : 4 octets, toujours "GCRP" (0x47 0x43 0x52 0x50)
 * - type    : 1 octet, identifiant du type de message
 * - len     : 2 octets, big-endian, taille du payload
 * - payload : 0 à 1024 octets, contenu variable selon le type
 *
 * Licence MIT — usage strictement éducatif.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* ═══════════════════════════════════════════════
 * Constantes du protocole
 * ═══════════════════════════════════════════════ */

#define GCRP_MAGIC        "GCRP"
#define GCRP_MAGIC_U32    0x50524347   /* "GCRP" en little-endian x86 */
#define GCRP_HEADER_SIZE  7            /* magic(4) + type(1) + len(2) */
#define GCRP_MAX_PAYLOAD  1024
#define GCRP_PORT         4444
#define GCRP_AUTH_KEY     0xA5         /* clé XOR pour l'encodage du token */

/* ═══════════════════════════════════════════════
 * Types de messages
 * ═══════════════════════════════════════════════ */

#define MSG_AUTH_REQ      0x01   /* Client → Serveur : demande d'authentification   */
#define MSG_AUTH_RESP     0x02   /* Serveur → Client : réponse d'authentification    */
#define MSG_PING          0x03   /* Client → Serveur : heartbeat                     */
#define MSG_PONG          0x04   /* Serveur → Client : réponse heartbeat             */
#define MSG_DATA_REQ      0x05   /* Client → Serveur : demande de données            */
#define MSG_DATA_RESP     0x06   /* Serveur → Client : réponse avec données          */
#define MSG_GOODBYE       0x07   /* Client → Serveur : fin de session                */
#define MSG_ERROR         0xFF   /* Serveur → Client : erreur                        */

/* ═══════════════════════════════════════════════
 * Codes de résultat d'authentification
 * ═══════════════════════════════════════════════ */

#define AUTH_OK           0x00
#define AUTH_FAILED       0x01
#define AUTH_EXPIRED      0x02

/* ═══════════════════════════════════════════════
 * Structures du protocole
 * ═══════════════════════════════════════════════ */

#pragma pack(push, 1)

/**
 * En-tête commun à tous les paquets GCRP.
 */
typedef struct {
    char     magic[4];      /* "GCRP"                              */
    uint8_t  type;          /* MSG_AUTH_REQ, MSG_PING, etc.        */
    uint16_t payload_len;   /* big-endian, taille du payload       */
} gcrp_header_t;

/**
 * Payload d'une demande d'authentification (MSG_AUTH_REQ).
 * Le token est le username XOR-é octet par octet avec GCRP_AUTH_KEY.
 */
typedef struct {
    char    username[32];   /* nom d'utilisateur, null-terminé     */
    uint8_t token[32];      /* username XOR GCRP_AUTH_KEY           */
    uint32_t timestamp;     /* horodatage Unix (little-endian)     */
} auth_req_payload_t;

/**
 * Payload d'une réponse d'authentification (MSG_AUTH_RESP).
 */
typedef struct {
    uint8_t  result;        /* AUTH_OK, AUTH_FAILED, AUTH_EXPIRED   */
    uint32_t session_id;    /* identifiant de session (si AUTH_OK)  */
    char     message[64];   /* message lisible                     */
} auth_resp_payload_t;

/**
 * Payload d'un ping (MSG_PING).
 */
typedef struct {
    uint32_t seq;           /* numéro de séquence                  */
    uint32_t timestamp;     /* horodatage Unix                     */
} ping_payload_t;

/**
 * Payload d'un pong (MSG_PONG).
 */
typedef struct {
    uint32_t seq;           /* numéro de séquence (écho du ping)   */
    uint32_t server_time;   /* horodatage serveur                  */
} pong_payload_t;

/**
 * Payload d'une demande de données (MSG_DATA_REQ).
 */
typedef struct {
    uint32_t session_id;    /* identifiant de session               */
    uint8_t  resource_id;   /* identifiant de la ressource demandée */
} data_req_payload_t;

/**
 * Payload d'une réponse de données (MSG_DATA_RESP).
 */
typedef struct {
    uint8_t  resource_id;
    uint16_t data_len;      /* big-endian                          */
    char     data[512];     /* contenu de la ressource             */
} data_resp_payload_t;

#pragma pack(pop)

/* ═══════════════════════════════════════════════
 * Helpers inline
 * ═══════════════════════════════════════════════ */

/**
 * Encode un uint16 en big-endian dans un buffer.
 */
static inline void put_u16_be(uint8_t *dst, uint16_t val) {
    dst[0] = (val >> 8) & 0xFF;
    dst[1] = val & 0xFF;
}

/**
 * Décode un uint16 big-endian depuis un buffer.
 */
static inline uint16_t get_u16_be(const uint8_t *src) {
    return ((uint16_t)src[0] << 8) | (uint16_t)src[1];
}

/**
 * Encode le token d'authentification : XOR du username avec la clé.
 */
static inline void encode_token(const char *username, uint8_t *token, size_t len) {
    for (size_t i = 0; i < len; i++) {
        token[i] = (uint8_t)username[i] ^ GCRP_AUTH_KEY;
    }
}

/**
 * Vérifie le token d'authentification.
 * Retourne 1 si valide, 0 sinon.
 */
static inline int verify_token(const char *username, const uint8_t *token, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (((uint8_t)username[i] ^ GCRP_AUTH_KEY) != token[i]) {
            return 0;
        }
    }
    return 1;
}

#endif /* PROTOCOL_H */
