/*
 * ch20_network_reconstructed.h
 *
 * Header reconstruit par décompilation du binaire server_O2_strip
 * Formation Reverse Engineering — Chapitre 20, Checkpoint
 *
 * Binaire source : server_O2_strip
 *                  SHA256: (à compléter avec le hash de votre binaire)
 * Outil principal : Ghidra 11.x
 * Outil secondaire : RetDec 5.0
 *
 * ====================================================================
 * PROTOCOLE RÉSEAU CUSTOM — VUE D'ENSEMBLE
 * ====================================================================
 *
 * Transport : TCP, port 4337
 * Modèle   : client/serveur, 1 client à la fois (accept bloquant)
 *
 * Format d'une trame :
 *
 *   +--------+--------+---------+------+------------------+---------+
 *   | magic  | magic  | version | type | payload_len (BE) | payload | chksum |
 *   | 0xC0   | 0xFE   |  0x01   | 1B   |     2B           |  N B    |  1B    |
 *   +--------+--------+---------+------+------------------+---------+
 *   |<------------- HEADER (6 octets) ------------->|      |         |
 *
 * Checksum : XOR de tous les octets du header + payload.
 *
 * Flux typique :
 *   1. Client se connecte en TCP
 *   2. Client envoie AUTH_REQ (username + hash du mot de passe)
 *   3. Serveur répond AUTH_RESP (succès/échec + token de session)
 *   4. Client envoie CMD_REQ (token + commande + arguments)
 *   5. Serveur répond CMD_RESP (données)
 *   6. Répéter 4-5 autant que nécessaire
 *   7. Client envoie DISCONNECT ou ferme la connexion
 *
 * Le serveur supporte aussi PING/PONG pour le keepalive.
 *
 * ====================================================================
 * NOTES D'ANALYSE
 * ====================================================================
 *
 * - Les credentials sont hardcodés dans .rodata :
 *     username = "admin"
 *     password hash = tableau de 32 octets à l'adresse 0x4040XX
 *   Le hash est calculé côté client par un algorithme basé sur FNV-1a
 *   itéré 32 fois (un octet de sortie par round). Mot de passe
 *   original non retrouvé par analyse statique seule.
 *
 * - Le token de session est généré par un LCG (Linear Congruential
 *   Generator) avec seed fixe 0xCAFEBABE et paramètres classiques
 *   glibc (a=1103515245, c=12345). Le token est donc déterministe
 *   et prédictible — vulnérabilité intentionnelle.
 *
 * - Le champ payload_len est en big-endian (network byte order).
 *   Les fonctions de conversion sont inlinées par GCC en -O2 et
 *   apparaissent comme des shifts/OR dans le pseudo-code.
 *
 * ====================================================================
 */

#ifndef CH20_NETWORK_RECONSTRUCTED_H
#define CH20_NETWORK_RECONSTRUCTED_H

#include <stdint.h>
#include <stddef.h>

/* ====================================================================
 * Section 1 : Constantes du protocole
 *
 * Sources :
 *   - Magic bytes   : comparaison dans recv_message (FUN_00401340)
 *   - Version       : comparaison dans recv_message
 *   - Tailles       : arguments de memcmp, recv, boucles
 *   - Port          : argument de htons() dans main()
 * ==================================================================== */

/* Magic bytes — premiers octets de chaque trame, vérifiés à la réception.
 * Identifiés par la comparaison :
 *   if (header[0] != 0xC0 || header[1] != 0xFE) return -1;
 * dans la fonction recv_message. */
#define PROTO_MAGIC_0       0xC0
#define PROTO_MAGIC_1       0xFE

/* Version du protocole — troisième octet du header.
 * Identifié par la comparaison :
 *   if (header[2] != 0x01) { fprintf(stderr, "Version non supportee..."); }
 */
#define PROTO_VERSION       0x01

/* Taille maximale du payload — déduite du test :
 *   if (payload_len > 0x400) return -1;
 * dans recv_message, et de la taille du buffer local (1024 octets). */
#define PROTO_MAX_PAYLOAD   1024

/* Taille du header de protocole — nombre d'octets lus par le premier
 * recv_all() dans recv_message : recv_all(fd, hdr, 6). */
#define PROTO_HEADER_SIZE   6

/* Taille du token de session — déduite de :
 *   - la boucle de génération : for (i = 0; i < 16; i++)
 *   - l'argument de memcmp dans handle_cmd : memcmp(token, session, 16)
 *   - la taille du champ token dans auth_resp : 16 octets après success */
#define PROTO_TOKEN_LEN     16

/* Taille du hash d'authentification — déduite de :
 *   - l'argument de memcmp dans handle_auth : memcmp(hash, valid_hash, 32)
 *   - la taille du tableau VALID_HASH en .rodata : 32 octets */
#define PROTO_HASH_LEN      32

/* Port d'écoute par défaut — argument de htons() dans main().
 * htons(0x10F1) = htons(4337). */
#define DEFAULT_PORT        4337

/* ====================================================================
 * Section 2 : Énumérations
 * ==================================================================== */

/*
 * Types de messages — identifiés dans le switch du dispatcher
 * principal (handle_client / FUN_00401780).
 *
 * Le switch teste le champ type (offset 3 du header) :
 *   case 0x01 → appelle handle_auth     → AUTH_REQ
 *   case 0x03 → appelle handle_cmd      → CMD_REQ
 *   case 0x05 → envoie réponse type 6   → PING (répondu par PONG)
 *   case 0xFF → printf("deconnecte") + return → DISCONNECT
 *   default   → fprintf(stderr, "Type inconnu") → ignoré
 *
 * Les types 0x02, 0x04, 0x06 ne sont pas dans le switch car ce sont
 * des messages ENVOYÉS par le serveur (réponses), pas reçus.
 * Leur valeur est déduite de l'argument 'type' passé à send_message
 * dans les handlers correspondants.
 */
typedef enum {
    MSG_AUTH_REQ    = 0x01,   /* client → serveur : requête d'authentification */
    MSG_AUTH_RESP   = 0x02,   /* serveur → client : réponse d'authentification */
    MSG_CMD_REQ     = 0x03,   /* client → serveur : requête de commande */
    MSG_CMD_RESP    = 0x04,   /* serveur → client : réponse de commande */
    MSG_PING        = 0x05,   /* client → serveur : keepalive */
    MSG_PONG        = 0x06,   /* serveur → client : réponse keepalive */
    MSG_DISCONNECT  = 0xFF    /* client → serveur : fin de session */
} msg_type_t;

/*
 * Identifiants de commandes — identifiés dans le switch du handler
 * de commandes (handle_cmd / FUN_00401620).
 *
 * Le switch teste le champ cmd_id (offset 16 dans le payload CMD_REQ,
 * soit juste après le token de 16 octets) :
 *   case 0x10 → lit arg_len, renvoie le payload tel quel → ECHO
 *   case 0x03 → envoie une chaîne d'info serveur        → GET_INFO
 *   case 0x01 → envoie une liste de noms de fichiers     → LIST_FILES
 *   case 0x02 → envoie le contenu d'un fichier (FLAG)    → READ_FILE
 *   default   → envoie "ERR:UNKNOWN_CMD"                 → non géré
 *
 * Les chaînes de réponse en .rodata ("config.dat\nusers.db\n...",
 * "FLAG{...}", "KeyGenMe Training Server v1.0...") confirment la
 * sémantique de chaque commande.
 */
typedef enum {
    CMD_LIST_FILES  = 0x01,   /* lister les fichiers disponibles */
    CMD_READ_FILE   = 0x02,   /* lire le contenu d'un fichier */
    CMD_GET_INFO    = 0x03,   /* obtenir les informations du serveur */
    CMD_ECHO        = 0x10    /* écho : renvoie les arguments tels quels */
} cmd_id_t;

/* ====================================================================
 * Section 3 : Structures de données
 *
 * Toutes les structures réseau sont packed (pas de padding) —
 * confirmé par les offsets d'accès consécutifs dans le pseudo-code
 * et par la taille totale des recv/send.
 * ==================================================================== */

/*
 * Header du protocole — 6 octets, packed.
 *
 * Identifié dans recv_message (FUN_00401340) :
 *   recv_all(fd, hdr_buf, 6);
 *   if (hdr_buf[0] != 0xC0 || hdr_buf[1] != 0xFE) → magic
 *   if (hdr_buf[2] != 0x01)                        → version
 *   type = hdr_buf[3];                              → type
 *   plen = (hdr_buf[4] << 8) | hdr_buf[5];         → payload_len (BE)
 *
 * Layout vérifié :
 *   offset 0x00 : magic[0]      (1 octet)
 *   offset 0x01 : magic[1]      (1 octet)
 *   offset 0x02 : version       (1 octet)
 *   offset 0x03 : type          (1 octet)
 *   offset 0x04 : payload_len   (2 octets, big-endian)
 *   total : 6 octets ✓
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic[2];       /* offset 0x00 — attendu : {0xC0, 0xFE} */
    uint8_t  version;        /* offset 0x02 — attendu : 0x01 */
    uint8_t  type;           /* offset 0x03 — msg_type_t */
    uint16_t payload_len;    /* offset 0x04 — big-endian ! */
} proto_header_t;

/*
 * Payload AUTH_REQ — envoyé par le client pour s'authentifier.
 *
 * Identifié dans handle_auth (FUN_00401500) :
 *   strncmp(payload + 0x00, VALID_USER, 32)  → username, 32 octets
 *   memcmp(payload + 0x20, VALID_HASH, 32)   → hash, 32 octets
 *
 * L'offset 0x20 = 32 confirme que username fait 32 octets (pas 64).
 * Le champ username est comparé avec strncmp, donc c'est une chaîne
 * terminée par NUL (ou tronquée à 32 caractères).
 *
 * Layout vérifié :
 *   offset 0x00 : username[32]       (32 octets)
 *   offset 0x20 : password_hash[32]  (32 octets)
 *   total : 64 octets ✓
 *   Confirmé par le test : if (plen < 64) → rejet
 */
typedef struct __attribute__((packed)) {
    char    username[32];                  /* offset 0x00 — chaîne NUL-terminée */
    uint8_t password_hash[PROTO_HASH_LEN]; /* offset 0x20 — hash du mot de passe */
} auth_req_payload_t;

/*
 * Payload AUTH_RESP — envoyé par le serveur en réponse à AUTH_REQ.
 *
 * Identifié dans handle_auth (FUN_00401500), branche succès :
 *   resp_buf[0] = 1;                           → success (1 octet)
 *   memcpy(resp_buf + 1, session_token, 16);   → token (16 octets)
 *   send_message(fd, 0x02, resp_buf, 17);      → taille totale = 17
 *
 * Branche échec :
 *   resp_buf[0] = 0;
 *   send_message(fd, 0x02, resp_buf, 17);      → même taille, token = 0
 *
 * Layout vérifié :
 *   offset 0x00 : success         (1 octet : 0x00=fail, 0x01=ok)
 *   offset 0x01 : token[16]       (16 octets, valide ssi success==1)
 *   total : 17 octets ✓
 */
typedef struct __attribute__((packed)) {
    uint8_t success;                   /* offset 0x00 — 0x00=échec, 0x01=succès */
    uint8_t token[PROTO_TOKEN_LEN];    /* offset 0x01 — token de session (si succès) */
} auth_resp_payload_t;

/*
 * Header du payload CMD_REQ — envoyé par le client pour une commande.
 *
 * Identifié dans handle_cmd (FUN_00401620) :
 *   memcmp(payload + 0x00, session_token, 16)  → token, 16 octets
 *   cmd_id = payload[0x10];                     → cmd_id, 1 octet
 *   arg_len = (payload[0x11] << 8) | payload[0x12]; → arg_len, 2 oct BE
 *
 * Les arguments de la commande suivent immédiatement ce header dans
 * le payload, sur arg_len octets.
 *
 * Layout vérifié :
 *   offset 0x00 : token[16]     (16 octets)
 *   offset 0x10 : cmd_id        (1 octet — cmd_id_t)
 *   offset 0x11 : arg_len       (2 octets, big-endian)
 *   total header : 19 octets, suivi de arg_len octets d'arguments
 */
typedef struct __attribute__((packed)) {
    uint8_t  token[PROTO_TOKEN_LEN];  /* offset 0x00 — token de session */
    uint8_t  cmd_id;                  /* offset 0x10 — cmd_id_t */
    uint16_t arg_len;                 /* offset 0x11 — big-endian */
    /* suivi de arg_len octets d'arguments */
} cmd_req_header_t;

/* ====================================================================
 * Section 4 : Fonctions utilitaires inline
 *
 * Ces fonctions sont inlinées par GCC en -O2 et n'ont pas d'adresse
 * propre dans le binaire. Elles sont reconstruites depuis les patterns
 * d'instructions récurrents dans send_message et recv_message.
 * ==================================================================== */

/*
 * Checksum XOR — identifié dans send_message et recv_message.
 *
 * Le pattern assembleur est une boucle :
 *   xor al, [rsi + rcx]  ;  inc rcx  ;  cmp rcx, rdx  ;  jb loop
 * Le résultat est un XOR cumulatif de tous les octets du buffer.
 *
 * Utilisé pour calculer le checksum de la trame complète
 * (header + payload), stocké dans le dernier octet de la trame.
 */
static inline uint8_t proto_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum ^= data[i];
    return sum;
}

/*
 * Lecture big-endian 16 bits — identifié par le pattern :
 *   movzx eax, byte ptr [rdi]
 *   shl   eax, 8
 *   movzx ecx, byte ptr [rdi+1]
 *   or    eax, ecx
 * dans recv_message (extraction de payload_len) et handle_cmd
 * (extraction de arg_len).
 */
static inline uint16_t read_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

/*
 * Écriture big-endian 16 bits — identifié par le pattern :
 *   mov byte ptr [rdi], ah     (ou shr eax, 8 + mov)
 *   mov byte ptr [rdi+1], al
 * dans send_message (écriture de payload_len dans le header).
 */
static inline void write_be16(uint8_t *p, uint16_t val)
{
    p[0] = (uint8_t)(val >> 8);
    p[1] = (uint8_t)(val & 0xFF);
}

/* ====================================================================
 * Section 5 : Signatures des fonctions principales
 *
 * Les adresses sont celles observées dans server_O2_strip.
 * Elles varieront si le binaire est recompilé.
 *
 * Convention : les fonctions sont listées dans l'ordre d'appel
 * depuis main() → handle_client() → handlers spécifiques.
 * ==================================================================== */

/*
 * send_all — envoi complet d'un buffer sur un socket.
 * Boucle sur send() jusqu'à ce que tous les octets soient envoyés.
 * Retourne le nombre d'octets envoyés, ou -1 en cas d'erreur.
 *
 * Identifiée par : boucle while autour de send(), paramètres (fd, buf, len).
 * FUN_00401120
 */
/* ssize_t send_all(int fd, const uint8_t *buf, size_t len); */

/*
 * recv_all — réception complète d'exactement len octets.
 * Boucle sur recv() jusqu'à ce que le buffer soit rempli.
 * Retourne le nombre d'octets reçus, ou -1 en cas d'erreur/déconnexion.
 *
 * Identifiée par : boucle while autour de recv(), même pattern que send_all.
 * FUN_00401180
 */
/* ssize_t recv_all(int fd, uint8_t *buf, size_t len); */

/*
 * send_message — envoi d'un message protocole complet.
 *
 * Construit une trame : header (6 octets) + payload + checksum (1 octet).
 * Écrit les magic bytes, la version, le type et payload_len (BE) dans
 * le header, copie le payload, calcule le checksum XOR, et envoie
 * le tout via send_all.
 *
 * Retourne 0 en cas de succès, -1 en cas d'erreur.
 *
 * Identifiée par : écriture de 0xC0/0xFE en début de buffer,
 * appel à proto_checksum (inliné), puis send_all.
 * FUN_00401250
 */
int send_message(int fd, uint8_t type,
                 const uint8_t *payload, uint16_t payload_len);

/*
 * recv_message — réception d'un message protocole complet.
 *
 * Lit le header (6 octets) via recv_all, vérifie les magic bytes et
 * la version, extrait le type et payload_len. Si payload_len > 0,
 * lit le payload. Lit le checksum final (1 octet) et le vérifie.
 *
 * Paramètres de sortie : type, payload (buffer fourni par l'appelant),
 * payload_len.
 * Retourne 0 en cas de succès, -1 en cas d'erreur.
 *
 * Identifiée par : recv_all(fd, hdr, 6), comparaisons magic/version,
 * extraction BE de payload_len, second recv_all conditionnel.
 * FUN_00401340
 */
int recv_message(int fd, uint8_t *type,
                 uint8_t *payload, uint16_t *payload_len);

/*
 * generate_token — génération du token de session.
 *
 * Utilise un LCG (Linear Congruential Generator) :
 *   seed = 0xCAFEBABE
 *   boucle 16 fois : seed = seed * 1103515245 + 12345
 *                    token[i] = (seed >> 16) & 0xFF
 *
 * Le seed est FIXE — le token est identique à chaque connexion.
 * Vulnérabilité intentionnelle pour l'exercice de RE.
 *
 * Identifiée par : constante 0xCAFEBABE, multiplicateur 1103515245
 * (LCG classique glibc), boucle de 16 itérations.
 * FUN_004014a0
 */
void generate_token(uint8_t *token);

/*
 * handle_auth — traitement d'une requête d'authentification.
 *
 * Vérifie que le payload est assez long (>= 64 octets).
 * Compare le username avec le credential hardcodé via strncmp.
 * Compare le hash avec le hash hardcodé via memcmp (32 octets).
 * Si les deux correspondent :
 *   - génère un token via generate_token()
 *   - envoie AUTH_RESP avec success=1 et le token
 * Sinon :
 *   - envoie AUTH_RESP avec success=0
 *
 * Identifiée par : appels strncmp + memcmp sur le payload,
 * références aux données .rodata (username + hash), appel à
 * generate_token, envoi d'un message de type 0x02.
 * FUN_00401500
 */
void handle_auth(int fd, const uint8_t *payload, uint16_t payload_len);

/*
 * handle_cmd — traitement d'une requête de commande.
 *
 * Vérifie que le payload est assez long (>= 19 octets, taille de
 * cmd_req_header_t). Vérifie le token de session via memcmp.
 * Si le token est invalide, envoie "ERR:NOT_AUTH".
 * Sinon, dispatche selon cmd_id :
 *   CMD_ECHO (0x10)       → renvoie les arguments tels quels
 *   CMD_GET_INFO (0x03)   → renvoie la chaîne d'identification serveur
 *   CMD_LIST_FILES (0x01) → renvoie une liste de noms de fichiers
 *   CMD_READ_FILE (0x02)  → renvoie un contenu de fichier (contient le FLAG)
 *   default               → renvoie "ERR:UNKNOWN_CMD"
 *
 * Identifiée par : memcmp sur les 16 premiers octets du payload
 * (token), switch sur l'octet à l'offset 16 (cmd_id), envoi de
 * messages de type 0x04.
 * FUN_00401620
 */
void handle_cmd(int fd, const uint8_t *payload, uint16_t payload_len);

/*
 * handle_client — boucle principale de gestion d'un client connecté.
 *
 * Remet l'état d'authentification à zéro, puis boucle sur
 * recv_message. Dispatche selon le type de message :
 *   MSG_AUTH_REQ (0x01)   → handle_auth
 *   MSG_CMD_REQ (0x03)    → handle_cmd
 *   MSG_PING (0x05)       → envoie MSG_PONG (0x06) sans payload
 *   MSG_DISCONNECT (0xFF) → return (fin propre)
 *   default               → log "Type inconnu" et continue
 *
 * Identifiée par : boucle while autour de recv_message, switch
 * sur type, appels aux handlers ci-dessus.
 * FUN_00401780
 */
void handle_client(int client_fd);

/* ====================================================================
 * Données hardcodées notables (en .rodata)
 *
 * Ces données ne font pas partie de l'API mais sont documentées
 * ici pour référence car elles sont nécessaires à l'écriture d'un
 * client fonctionnel.
 * ==================================================================== */

/*
 * Username valide : "admin" (chaîne dans .rodata)
 *
 * Hash valide du mot de passe (32 octets dans .rodata) :
 *   0xa7, 0x5f, 0x5a, 0x35, 0x8f, 0xc1, 0xbc, 0x8e,
 *   0xab, 0xa8, 0x4f, 0xd3, 0xc7, 0x6d, 0xb6, 0x8e,
 *   0xa1, 0xe6, 0xab, 0x71, 0xef, 0x77, 0x5c, 0x2f,
 *   0x82, 0x04, 0xf8, 0xcd, 0xbc, 0x07, 0x47, 0x2d
 *
 * L'algorithme de hashing du mot de passe n'est pas implémenté
 * côté serveur — seul le hash final est comparé. Pour s'authentifier,
 * un client peut envoyer directement ces 32 octets sans connaître
 * le mot de passe original.
 *
 * Chaînes de réponse identifiées en .rodata :
 *   - "KeyGenMe Training Server v1.0 -- RE Formation GCC" (CMD_GET_INFO)
 *   - "config.dat\nusers.db\nsecret.key\nlog.txt"         (CMD_LIST_FILES)
 *   - "FLAG{pr0t0c0l_r3v3rs3d_gcc}\n"                     (CMD_READ_FILE)
 *   - "ERR:NOT_AUTH"                                        (token invalide)
 *   - "ERR:UNKNOWN_CMD"                                     (commande inconnue)
 */

#endif /* CH20_NETWORK_RECONSTRUCTED_H */
