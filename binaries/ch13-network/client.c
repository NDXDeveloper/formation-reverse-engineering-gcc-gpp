/**
 * binaries/ch13-network/client.c
 *
 * Client d'entraînement utilisant le protocole custom GCRP.
 * Se connecte au serveur, s'authentifie, envoie des pings,
 * demande des ressources, puis se déconnecte.
 *
 * Compilation : voir Makefile
 * Usage :      ./client_O0 [ip] [port] [username]
 *
 * Licence MIT — usage strictement éducatif.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "protocol.h"

/* ═══════════════════════════════════════════════
 * Envoi / réception de paquets GCRP
 * ═══════════════════════════════════════════════ */

static int send_all(int sock, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = send(sock, p, remaining, 0);
        if (n <= 0) {
            perror("send");
            return -1;
        }
        p += n;
        remaining -= n;
    }
    return 0;
}

static int recv_all(int sock, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = recv(sock, p, remaining, 0);
        if (n <= 0) {
            if (n == 0) fprintf(stderr, "[!] Connexion fermée par le serveur.\n");
            else perror("recv");
            return -1;
        }
        p += n;
        remaining -= n;
    }
    return 0;
}

static int send_packet(int sock, uint8_t type,
                       const void *payload, uint16_t payload_len) {
    gcrp_header_t hdr;
    memcpy(hdr.magic, GCRP_MAGIC, 4);
    hdr.type = type;
    put_u16_be((uint8_t *)&hdr.payload_len, payload_len);

    if (send_all(sock, &hdr, GCRP_HEADER_SIZE) < 0) return -1;
    if (payload_len > 0 && payload) {
        if (send_all(sock, payload, payload_len) < 0) return -1;
    }
    return 0;
}

static int recv_packet(int sock, uint8_t *type,
                       void *payload, uint16_t *payload_len) {
    gcrp_header_t hdr;
    if (recv_all(sock, &hdr, GCRP_HEADER_SIZE) < 0) return -1;

    if (memcmp(hdr.magic, GCRP_MAGIC, 4) != 0) {
        fprintf(stderr, "[!] Réponse invalide (magic incorrect).\n");
        return -1;
    }

    *type = hdr.type;
    *payload_len = get_u16_be((const uint8_t *)&hdr.payload_len);

    if (*payload_len > GCRP_MAX_PAYLOAD) {
        fprintf(stderr, "[!] Payload trop grand : %u\n", *payload_len);
        return -1;
    }

    if (*payload_len > 0) {
        if (recv_all(sock, payload, *payload_len) < 0) return -1;
    }

    return 0;
}

/* ═══════════════════════════════════════════════
 * Logique du client
 * ═══════════════════════════════════════════════ */

/**
 * Phase 1 : Authentification.
 * Construit le token XOR et envoie MSG_AUTH_REQ.
 * Retourne le session_id en cas de succès, 0 sinon.
 */
static uint32_t do_authenticate(int sock, const char *username) {
    auth_req_payload_t req;
    memset(&req, 0, sizeof(req));

    strncpy(req.username, username, sizeof(req.username) - 1);
    size_t ulen = strlen(req.username);

    /* Encoder le token : XOR de chaque octet du username avec la clé */
    encode_token(req.username, req.token, ulen);

    req.timestamp = (uint32_t)time(NULL);

    printf("[*] Envoi AUTH_REQ : user=\"%s\"\n", req.username);

    if (send_packet(sock, MSG_AUTH_REQ, &req, sizeof(req)) < 0) {
        return 0;
    }

    /* Recevoir la réponse */
    uint8_t type;
    uint16_t resp_len;
    uint8_t resp_buf[GCRP_MAX_PAYLOAD];

    if (recv_packet(sock, &type, resp_buf, &resp_len) < 0) {
        return 0;
    }

    if (type != MSG_AUTH_RESP) {
        fprintf(stderr, "[!] Réponse inattendue (type 0x%02x).\n", type);
        return 0;
    }

    const auth_resp_payload_t *resp = (const auth_resp_payload_t *)resp_buf;

    if (resp->result == AUTH_OK) {
        printf("[+] Authentifié ! Session ID : %08X\n", resp->session_id);
        printf("    Message : %s\n", resp->message);
        return resp->session_id;
    } else {
        printf("[-] Authentification échouée (code %u).\n", resp->result);
        printf("    Message : %s\n", resp->message);
        return 0;
    }
}

/**
 * Phase 2 : Envoi de pings heartbeat.
 */
static int do_ping(int sock, uint32_t seq) {
    ping_payload_t ping;
    ping.seq = seq;
    ping.timestamp = (uint32_t)time(NULL);

    printf("[*] Envoi PING seq=%u\n", seq);

    if (send_packet(sock, MSG_PING, &ping, sizeof(ping)) < 0) {
        return -1;
    }

    uint8_t type;
    uint16_t resp_len;
    uint8_t resp_buf[GCRP_MAX_PAYLOAD];

    if (recv_packet(sock, &type, resp_buf, &resp_len) < 0) {
        return -1;
    }

    if (type == MSG_PONG) {
        const pong_payload_t *pong = (const pong_payload_t *)resp_buf;
        printf("[+] PONG reçu : seq=%u server_time=%u\n",
               pong->seq, pong->server_time);
    } else {
        fprintf(stderr, "[!] Réponse inattendue au PING (type 0x%02x).\n", type);
    }

    return 0;
}

/**
 * Phase 3 : Demande de ressources.
 */
static int do_request_data(int sock, uint32_t session_id, uint8_t resource_id) {
    data_req_payload_t req;
    req.session_id = session_id;
    req.resource_id = resource_id;

    printf("[*] Envoi DATA_REQ : session=%08X resource=%u\n",
           session_id, resource_id);

    if (send_packet(sock, MSG_DATA_REQ, &req, sizeof(req)) < 0) {
        return -1;
    }

    uint8_t type;
    uint16_t resp_len;
    uint8_t resp_buf[GCRP_MAX_PAYLOAD];

    if (recv_packet(sock, &type, resp_buf, &resp_len) < 0) {
        return -1;
    }

    if (type == MSG_DATA_RESP) {
        const data_resp_payload_t *resp = (const data_resp_payload_t *)resp_buf;
        uint16_t data_len = get_u16_be((const uint8_t *)&resp->data_len);
        printf("[+] DATA_RESP : resource=%u len=%u\n", resp->resource_id, data_len);
        printf("    Contenu : \"%.*s\"\n", data_len, resp->data);
    } else if (type == MSG_ERROR) {
        printf("[-] Erreur serveur : \"%.*s\"\n", resp_len, (char *)resp_buf);
    } else {
        fprintf(stderr, "[!] Réponse inattendue (type 0x%02x).\n", type);
    }

    return 0;
}

/**
 * Phase 4 : Déconnexion propre.
 */
static int do_goodbye(int sock) {
    printf("[*] Envoi GOODBYE.\n");
    return send_packet(sock, MSG_GOODBYE, NULL, 0);
}

/* ═══════════════════════════════════════════════
 * Point d'entrée
 * ═══════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    const char *server_ip = "127.0.0.1";
    uint16_t port = GCRP_PORT;
    const char *username = "admin";

    if (argc > 1) server_ip = argv[1];
    if (argc > 2) port = (uint16_t)atoi(argv[2]);
    if (argc > 3) username = argv[3];

    printf("=== GCRP Client v1.0 ===\n");
    printf("[*] Connexion à %s:%u en tant que \"%s\"...\n",
           server_ip, port, username);

    /* Créer le socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    /* Se connecter au serveur */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[!] Adresse IP invalide : %s\n", server_ip);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("[+] Connecté !\n\n");

    /* ── Phase 1 : Authentification ── */
    uint32_t session_id = do_authenticate(sock, username);
    if (session_id == 0) {
        fprintf(stderr, "[-] Impossible de s'authentifier. Abandon.\n");
        close(sock);
        return 1;
    }
    printf("\n");

    /* ── Phase 2 : Pings heartbeat ── */
    for (uint32_t i = 1; i <= 3; i++) {
        do_ping(sock, i);
        usleep(200000);  /* 200ms entre chaque ping */
    }
    printf("\n");

    /* ── Phase 3 : Demande de ressources ── */
    for (uint8_t r = 0; r < 4; r++) {
        do_request_data(sock, session_id, r);
        usleep(100000);  /* 100ms entre chaque requête */
    }

    /* Demander une ressource inexistante (test d'erreur) */
    do_request_data(sock, session_id, 99);
    printf("\n");

    /* ── Phase 4 : Déconnexion ── */
    do_goodbye(sock);

    close(sock);
    printf("[*] Déconnecté.\n");

    return 0;
}
