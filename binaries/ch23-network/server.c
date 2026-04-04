/*
 * ch23-network — server.c
 * Serveur TCP avec protocole binaire custom
 * Formation Reverse Engineering — Chapitre 23
 *
 * Licence MIT — Usage strictement éducatif
 *
 * Protocole :
 *   Header : [magic:1][type:1][payload_len:2 BE]
 *   Magic  : 0xC0
 *   Types  : 0x01=HELLO, 0x02=AUTH, 0x03=CMD, 0x04=QUIT
 *            Réponses = type | 0x80, erreur = 0xFF
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/random.h>

/* ═══════════════════════════════════════════
 *  Constantes du protocole
 * ═══════════════════════════════════════════ */

#define PROTO_MAGIC       0xC0
#define PROTO_PORT        4444
#define HEADER_SIZE       4
#define MAX_PAYLOAD       4096
#define CHALLENGE_LEN     8
#define MAX_AUTH_RETRIES   3

/* Types de messages */
#define MSG_HELLO_REQ     0x01
#define MSG_AUTH_REQ      0x02
#define MSG_CMD_REQ       0x03
#define MSG_QUIT_REQ      0x04
#define MSG_HELLO_RESP    0x81
#define MSG_AUTH_RESP     0x82
#define MSG_CMD_RESP      0x83
#define MSG_QUIT_RESP     0x84
#define MSG_ERROR         0xFF

/* Codes d'erreur */
#define ERR_BAD_MAGIC     0x01
#define ERR_BAD_TYPE      0x02
#define ERR_WRONG_STATE   0x03
#define ERR_AUTH_FAIL     0x04
#define ERR_BAD_CMD       0x05
#define ERR_PAYLOAD_TOO_LARGE 0x06

/* États de la session */
#define STATE_CONNECTED     0
#define STATE_HELLO_DONE    1
#define STATE_AUTHENTICATED 2
#define STATE_DISCONNECTED  3

/* Commandes disponibles */
#define CMD_PING          0x01
#define CMD_LIST          0x02
#define CMD_READ          0x03
#define CMD_INFO          0x04

/* Codes de status */
#define STATUS_OK         0x01
#define STATUS_FAIL       0x00

/* ═══════════════════════════════════════════
 *  Structures
 * ═══════════════════════════════════════════ */

typedef struct {
    uint8_t  magic;
    uint8_t  msg_type;
    uint16_t payload_len;  /* big-endian sur le réseau */
} proto_header_t;

typedef struct {
    int      socket_fd;
    int      state;
    uint8_t  challenge[CHALLENGE_LEN];
    char     username[64];
    int      auth_retries;
} session_t;

/* ═══════════════════════════════════════════
 *  Base de données utilisateurs (hardcodée)
 * ═══════════════════════════════════════════ */

typedef struct {
    const char *username;
    const char *password;
} user_entry_t;

static const user_entry_t user_db[] = {
    { "admin",    "s3cur3P@ss!"  },
    { "analyst",  "r3v3rs3M3"    },
    { "guest",    "guest123"     },
    { NULL, NULL }
};

/* ═══════════════════════════════════════════
 *  Fichiers virtuels (pour les commandes)
 * ═══════════════════════════════════════════ */

typedef struct {
    const char *name;
    const char *content;
} vfile_t;

static const vfile_t file_db[] = {
    { "readme.txt",   "Welcome to the secret server.\n"
                      "Access level: CLASSIFIED\n" },
    { "notes.txt",    "TODO: rotate encryption keys\n"
                      "TODO: fix auth bypass in v2.1\n" },
    { "config.dat",   "port=4444\nmax_conn=16\nlog_level=2\n" },
    { "flag.txt",     "FLAG{pr0t0c0l_r3v3rs3d_succ3ssfully}\n" },
    { NULL, NULL }
};

/* ═══════════════════════════════════════════
 *  Fonctions réseau utilitaires
 * ═══════════════════════════════════════════ */

/* Lire exactement n octets depuis un socket */
static int recv_exact(int fd, void *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t r = recv(fd, (uint8_t *)buf + total, n - total, 0);
        if (r <= 0)
            return -1;
        total += (size_t)r;
    }
    return 0;
}

/* Envoyer exactement n octets */
static int send_all(int fd, const void *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t w = send(fd, (const uint8_t *)buf + total, n - total, 0);
        if (w <= 0)
            return -1;
        total += (size_t)w;
    }
    return 0;
}

/* ═══════════════════════════════════════════
 *  Fonctions du protocole
 * ═══════════════════════════════════════════ */

/* Construire et envoyer un message protocolaire */
static int proto_send(int fd, uint8_t msg_type,
                      const uint8_t *payload, uint16_t payload_len)
{
    uint8_t header[HEADER_SIZE];
    header[0] = PROTO_MAGIC;
    header[1] = msg_type;
    header[2] = (payload_len >> 8) & 0xFF;  /* big-endian */
    header[3] = payload_len & 0xFF;

    if (send_all(fd, header, HEADER_SIZE) < 0)
        return -1;

    if (payload_len > 0 && payload != NULL) {
        if (send_all(fd, payload, payload_len) < 0)
            return -1;
    }

    return 0;
}

/* Envoyer un message d'erreur */
static int proto_send_error(int fd, uint8_t error_code, const char *msg)
{
    uint8_t buf[256];
    size_t msg_len = msg ? strlen(msg) : 0;

    if (msg_len > 254) msg_len = 254;

    buf[0] = error_code;
    if (msg_len > 0)
        memcpy(buf + 1, msg, msg_len);

    return proto_send(fd, MSG_ERROR, buf, (uint16_t)(1 + msg_len));
}

/* Recevoir un message protocolaire (header + payload) */
static int proto_recv(int fd, uint8_t *msg_type,
                      uint8_t *payload, uint16_t *payload_len)
{
    uint8_t header[HEADER_SIZE];

    if (recv_exact(fd, header, HEADER_SIZE) < 0)
        return -1;

    if (header[0] != PROTO_MAGIC) {
        proto_send_error(fd, ERR_BAD_MAGIC, "Bad magic byte");
        return -1;
    }

    *msg_type    = header[1];
    *payload_len = ((uint16_t)header[2] << 8) | (uint16_t)header[3];

    if (*payload_len > MAX_PAYLOAD) {
        proto_send_error(fd, ERR_PAYLOAD_TOO_LARGE, "Payload too large");
        return -1;
    }

    if (*payload_len > 0) {
        if (recv_exact(fd, payload, *payload_len) < 0)
            return -1;
    }

    return 0;
}

/* ═══════════════════════════════════════════
 *  Crypto légère (XOR avec challenge)
 * ═══════════════════════════════════════════ */

/*
 * Le mot de passe est XOR-é avec le challenge côté client
 * avant envoi. Le serveur applique le même XOR pour récupérer
 * le mot de passe en clair et le comparer.
 *
 * C'est volontairement faible — l'objectif est pédagogique :
 * les étudiants doivent découvrir ce mécanisme par RE.
 */
static void xor_with_challenge(uint8_t *data, size_t len,
                               const uint8_t *challenge)
{
    for (size_t i = 0; i < len; i++) {
        data[i] ^= challenge[i % CHALLENGE_LEN];
    }
}

/* ═══════════════════════════════════════════
 *  Handlers de messages
 * ═══════════════════════════════════════════ */

static int handle_hello(session_t *sess, const uint8_t *payload,
                        uint16_t payload_len)
{
    /* Vérifier l'état */
    if (sess->state != STATE_CONNECTED) {
        proto_send_error(sess->socket_fd, ERR_WRONG_STATE,
                         "Unexpected HELLO");
        return -1;
    }

    /* Vérifier le contenu du HELLO */
    if (payload_len < 5 || memcmp(payload, "HELLO", 5) != 0) {
        proto_send_error(sess->socket_fd, ERR_BAD_TYPE,
                         "Invalid HELLO payload");
        return -1;
    }

    /* Générer le challenge */
    if (getrandom(sess->challenge, CHALLENGE_LEN, 0) != CHALLENGE_LEN) {
        /* Fallback si getrandom échoue */
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (int i = 0; i < CHALLENGE_LEN; i++)
            sess->challenge[i] = (uint8_t)(rand() & 0xFF);
    }

    /* Construire la réponse HELLO : "WELCOME" + challenge */
    uint8_t resp[7 + CHALLENGE_LEN];
    memcpy(resp, "WELCOME", 7);
    memcpy(resp + 7, sess->challenge, CHALLENGE_LEN);

    if (proto_send(sess->socket_fd, MSG_HELLO_RESP,
                   resp, (uint16_t)sizeof(resp)) < 0)
        return -1;

    sess->state = STATE_HELLO_DONE;
    printf("[*] Handshake completed, challenge sent.\n");
    return 0;
}

static int handle_auth(session_t *sess, const uint8_t *payload,
                       uint16_t payload_len)
{
    /* Vérifier l'état */
    if (sess->state != STATE_HELLO_DONE) {
        proto_send_error(sess->socket_fd, ERR_WRONG_STATE,
                         "Complete handshake first");
        return -1;
    }

    if (payload_len < 4) {  /* minimum : 1+1 + 1+1 */
        proto_send_error(sess->socket_fd, ERR_AUTH_FAIL,
                         "Auth payload too short");
        return -1;
    }

    /* Extraire username (length-prefixed) */
    uint8_t user_len = payload[0];
    if (1 + user_len >= payload_len) {
        proto_send_error(sess->socket_fd, ERR_AUTH_FAIL,
                         "Bad username length");
        return -1;
    }
    const uint8_t *username = payload + 1;

    /* Extraire password (length-prefixed, XOR-é avec le challenge) */
    size_t pass_offset = 1 + user_len;
    uint8_t pass_len = payload[pass_offset];
    if (pass_offset + 1 + pass_len > payload_len) {
        proto_send_error(sess->socket_fd, ERR_AUTH_FAIL,
                         "Bad password length");
        return -1;
    }

    /* Copier et dé-XOR-er le mot de passe */
    uint8_t password[256];
    memcpy(password, payload + pass_offset + 1, pass_len);
    xor_with_challenge(password, pass_len, sess->challenge);

    /* Chercher l'utilisateur dans la base */
    int authenticated = 0;
    for (int i = 0; user_db[i].username != NULL; i++) {
        if (user_len == strlen(user_db[i].username) &&
            memcmp(username, user_db[i].username, user_len) == 0 &&
            pass_len == strlen(user_db[i].password) &&
            memcmp(password, user_db[i].password, pass_len) == 0)
        {
            authenticated = 1;
            memcpy(sess->username, user_db[i].username, user_len);
            sess->username[user_len] = '\0';
            break;
        }
    }

    /* Effacer le mot de passe de la mémoire */
    memset(password, 0, sizeof(password));

    /* Construire la réponse AUTH */
    uint8_t resp[2];
    resp[0] = 0x00;  /* réservé */

    if (authenticated) {
        resp[1] = STATUS_OK;
        sess->state = STATE_AUTHENTICATED;
        printf("[+] User '%s' authenticated successfully.\n",
               sess->username);
    } else {
        resp[1] = STATUS_FAIL;
        sess->auth_retries++;
        printf("[-] Authentication failed (attempt %d/%d).\n",
               sess->auth_retries, MAX_AUTH_RETRIES);

        if (sess->auth_retries >= MAX_AUTH_RETRIES) {
            proto_send(sess->socket_fd, MSG_AUTH_RESP, resp, 2);
            proto_send_error(sess->socket_fd, ERR_AUTH_FAIL,
                             "Too many failed attempts");
            return -1;
        }
    }

    return proto_send(sess->socket_fd, MSG_AUTH_RESP, resp, 2);
}

static int handle_cmd_ping(session_t *sess)
{
    const char *pong = "PONG";
    uint8_t resp[1 + 4];
    resp[0] = STATUS_OK;
    memcpy(resp + 1, pong, 4);
    return proto_send(sess->socket_fd, MSG_CMD_RESP, resp, 5);
}

static int handle_cmd_list(session_t *sess)
{
    /* Construire la liste des fichiers disponibles */
    uint8_t resp[MAX_PAYLOAD];
    size_t offset = 0;

    resp[offset++] = STATUS_OK;

    /* Nombre de fichiers */
    int count = 0;
    for (int i = 0; file_db[i].name != NULL; i++)
        count++;
    resp[offset++] = (uint8_t)count;

    /* Pour chaque fichier : index(1) + name_len(1) + name */
    for (int i = 0; file_db[i].name != NULL; i++) {
        size_t nlen = strlen(file_db[i].name);
        if (offset + 2 + nlen > MAX_PAYLOAD) break;

        resp[offset++] = (uint8_t)i;
        resp[offset++] = (uint8_t)nlen;
        memcpy(resp + offset, file_db[i].name, nlen);
        offset += nlen;
    }

    return proto_send(sess->socket_fd, MSG_CMD_RESP,
                      resp, (uint16_t)offset);
}

static int handle_cmd_read(session_t *sess, const uint8_t *args,
                           uint16_t args_len)
{
    if (args_len < 1) {
        uint8_t resp[2] = { STATUS_FAIL, ERR_BAD_CMD };
        return proto_send(sess->socket_fd, MSG_CMD_RESP, resp, 2);
    }

    uint8_t file_index = args[0];

    /* Vérifier que l'index est valide */
    int count = 0;
    for (int i = 0; file_db[i].name != NULL; i++)
        count++;

    if (file_index >= count) {
        uint8_t resp[2] = { STATUS_FAIL, ERR_BAD_CMD };
        return proto_send(sess->socket_fd, MSG_CMD_RESP, resp, 2);
    }

    /* Envoyer le contenu du fichier */
    const char *content = file_db[file_index].content;
    size_t clen = strlen(content);
    uint8_t resp[MAX_PAYLOAD];
    resp[0] = STATUS_OK;

    if (clen + 1 > MAX_PAYLOAD) clen = MAX_PAYLOAD - 1;
    memcpy(resp + 1, content, clen);

    printf("[*] User '%s' reading file '%s'\n",
           sess->username, file_db[file_index].name);

    return proto_send(sess->socket_fd, MSG_CMD_RESP,
                      resp, (uint16_t)(1 + clen));
}

static int handle_cmd_info(session_t *sess)
{
    /* Retourner des informations sur le serveur */
    uint8_t resp[MAX_PAYLOAD];
    resp[0] = STATUS_OK;

    const char *info = "ch23-network server v1.0\n"
                       "Protocol: custom binary\n"
                       "Build: GCC " __VERSION__ "\n";
    size_t ilen = strlen(info);
    memcpy(resp + 1, info, ilen);

    return proto_send(sess->socket_fd, MSG_CMD_RESP,
                      resp, (uint16_t)(1 + ilen));
}

static int handle_command(session_t *sess, const uint8_t *payload,
                          uint16_t payload_len)
{
    /* Vérifier l'état */
    if (sess->state != STATE_AUTHENTICATED) {
        proto_send_error(sess->socket_fd, ERR_WRONG_STATE,
                         "Authenticate first");
        return -1;
    }

    if (payload_len < 1) {
        proto_send_error(sess->socket_fd, ERR_BAD_CMD,
                         "Empty command");
        return -1;
    }

    uint8_t command_id = payload[0];
    const uint8_t *args = payload + 1;
    uint16_t args_len = payload_len - 1;

    switch (command_id) {
    case CMD_PING:
        return handle_cmd_ping(sess);
    case CMD_LIST:
        return handle_cmd_list(sess);
    case CMD_READ:
        return handle_cmd_read(sess, args, args_len);
    case CMD_INFO:
        return handle_cmd_info(sess);
    default:
        printf("[-] Unknown command: 0x%02X\n", command_id);
        proto_send_error(sess->socket_fd, ERR_BAD_CMD,
                         "Unknown command ID");
        return 0;  /* non fatal */
    }
}

static int handle_quit(session_t *sess)
{
    const char *bye = "BYE";
    uint8_t resp[3];
    memcpy(resp, bye, 3);

    proto_send(sess->socket_fd, MSG_QUIT_RESP, resp, 3);

    sess->state = STATE_DISCONNECTED;
    printf("[*] Client '%s' disconnected gracefully.\n",
           sess->username[0] ? sess->username : "(unauthenticated)");
    return 0;
}

/* ═══════════════════════════════════════════
 *  Boucle de traitement d'un client
 * ═══════════════════════════════════════════ */

static void handle_client(int client_fd, struct sockaddr_in *client_addr)
{
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, addr_str, sizeof(addr_str));
    printf("[+] New connection from %s:%d\n",
           addr_str, ntohs(client_addr->sin_port));

    session_t sess;
    memset(&sess, 0, sizeof(sess));
    sess.socket_fd = client_fd;
    sess.state = STATE_CONNECTED;

    uint8_t payload[MAX_PAYLOAD];
    uint8_t msg_type;
    uint16_t payload_len;

    while (sess.state != STATE_DISCONNECTED) {
        if (proto_recv(client_fd, &msg_type, payload, &payload_len) < 0) {
            printf("[-] Connection lost or protocol error.\n");
            break;
        }

        int result;
        switch (msg_type) {
        case MSG_HELLO_REQ:
            result = handle_hello(&sess, payload, payload_len);
            break;
        case MSG_AUTH_REQ:
            result = handle_auth(&sess, payload, payload_len);
            break;
        case MSG_CMD_REQ:
            result = handle_command(&sess, payload, payload_len);
            break;
        case MSG_QUIT_REQ:
            result = handle_quit(&sess);
            break;
        default:
            printf("[-] Unknown message type: 0x%02X\n", msg_type);
            proto_send_error(client_fd, ERR_BAD_TYPE,
                             "Unknown message type");
            result = 0;  /* non fatal */
            break;
        }

        if (result < 0)
            break;
    }

    close(client_fd);
    printf("[*] Connection closed.\n\n");
}

/* ═══════════════════════════════════════════
 *  Point d'entrée
 * ═══════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    uint16_t port = PROTO_PORT;

    if (argc >= 2) {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) port = PROTO_PORT;
    }

    /* Créer le socket serveur */
    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("╔══════════════════════════════════════╗\n");
    printf("║   ch23-network server v1.0           ║\n");
    printf("║   Listening on port %-5d            ║\n", port);
    printf("╚══════════════════════════════════════╝\n\n");

    /* Boucle principale — un client à la fois (monothread) */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        handle_client(client_fd, &client_addr);
    }

    close(server_fd);
    return 0;
}
