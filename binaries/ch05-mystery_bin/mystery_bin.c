/*
 * mystery_bin.c — Binaire d'entraînement pour le checkpoint du chapitre 5
 *
 * Formation Reverse Engineering — Applications compilées avec la chaîne GNU
 * Licence MIT — Usage strictement éducatif
 *
 * Ce programme est conçu pour produire des résultats intéressants avec
 * chaque outil du chapitre 5 (file, strings, readelf, nm, ldd, strace,
 * ltrace, checksec). Il ne contient aucune fonctionnalité malveillante.
 *
 * Fonctionnalités :
 *   - Authentification par mot de passe (strcmp visible dans ltrace)
 *   - Lecture d'un fichier de configuration (visible dans strace)
 *   - Chiffrement XOR simple d'un message (constantes visibles dans strings)
 *   - Écriture du résultat dans un fichier (visible dans strace)
 *   - Vérification anti-debug légère (accès /proc visible dans strace)
 *   - Plusieurs fonctions nommées (visibles dans nm)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Constantes et données globales
 * (visibles dans strings / readelf -x .rodata)
 * -------------------------------------------------------------------------- */

#define MAX_INPUT     256
#define XOR_KEY_LEN   16
#define CONFIG_PATH   "/tmp/mystery.conf"
#define OUTPUT_PATH   "/tmp/mystery.out"
#define MAGIC_HEADER  "MYST"
#define VERSION_TAG   "mystery-tool v2.4.1-beta"

static const char *MASTER_PASSWORD = "R3v3rs3M3!2024";

static const unsigned char XOR_KEY[XOR_KEY_LEN] = {
    0x4D, 0x59, 0x53, 0x54, 0x45, 0x52, 0x59, 0x4B,  /* MYSTERYK */
    0x45, 0x59, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35   /* EY012345 */
};

/* Structure du header de fichier de sortie */
typedef struct {
    char     magic[4];       /* "MYST" */
    uint32_t version;        /* 0x00020401 */
    uint32_t data_length;
    uint32_t checksum;
    uint64_t timestamp;
} __attribute__((packed)) MysteryHeader;

/* Variable globale pour le mode verbose */
static int g_verbose = 0;

/* --------------------------------------------------------------------------
 * Fonctions utilitaires
 * -------------------------------------------------------------------------- */

/*
 * compute_checksum — Calcule un checksum simple (somme des octets)
 * (visible dans nm comme symbole T)
 */
static uint32_t compute_checksum(const unsigned char *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
        sum ^= (sum << 3);
    }
    return sum;
}

/*
 * xor_encrypt — Chiffre/déchiffre un buffer avec la clé XOR
 * (visible dans nm, logique de chiffrement pour le RE)
 */
static void xor_encrypt(unsigned char *buf, size_t len,
                         const unsigned char *key, size_t key_len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] ^= key[i % key_len];
    }
}

/*
 * check_debugger — Vérification anti-debug légère
 * Lit /proc/self/status pour détecter un traceur (ptrace)
 * (l'accès au fichier sera visible dans strace)
 */
static int check_debugger(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            int tracer_pid = atoi(line + 10);
            fclose(f);
            if (tracer_pid != 0) {
                fprintf(stderr, "[!] Debugger detected (pid: %d)\n", tracer_pid);
                return 1;
            }
            return 0;
        }
    }

    fclose(f);
    return 0;
}

/* --------------------------------------------------------------------------
 * Fonctions principales
 * -------------------------------------------------------------------------- */

/*
 * authenticate_user — Demande et vérifie le mot de passe
 * (strcmp visible dans ltrace, prompt visible dans strings)
 */
static int authenticate_user(void)
{
    char input[MAX_INPUT];

    printf("=== %s ===\n", VERSION_TAG);
    printf("Enter access password: ");
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) {
        fprintf(stderr, "Error: failed to read input.\n");
        return 0;
    }

    /* Retirer le newline */
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }

    if (strcmp(input, MASTER_PASSWORD) != 0) {
        fprintf(stderr, "Authentication failed. Access denied.\n");
        return 0;
    }

    printf("Authentication successful. Welcome.\n");
    return 1;
}

/*
 * load_config — Tente de charger un fichier de configuration
 * (l'ouverture de fichier sera visible dans strace)
 */
static int load_config(void)
{
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        if (g_verbose) {
            printf("[*] Config file not found: %s (using defaults)\n", CONFIG_PATH);
        }
        return 0;
    }

    char line[256];
    printf("[*] Loading configuration from %s\n", CONFIG_PATH);

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "verbose=", 8) == 0) {
            g_verbose = atoi(line + 8);
        }
        /* D'autres options de config pourraient être parsées ici */
    }

    fclose(f);
    return 1;
}

/*
 * process_message — Chiffre un message et l'écrit dans un fichier
 * (opérations fichier visibles dans strace, structure visible dans xxd)
 */
static int process_message(const char *message)
{
    size_t msg_len = strlen(message);

    /* Allouer un buffer pour le message chiffré */
    unsigned char *encrypted = (unsigned char *)malloc(msg_len);
    if (!encrypted) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        return 0;
    }

    /* Copier et chiffrer */
    memcpy(encrypted, message, msg_len);
    xor_encrypt(encrypted, msg_len, XOR_KEY, XOR_KEY_LEN);

    /* Préparer le header */
    MysteryHeader header;
    memcpy(header.magic, MAGIC_HEADER, 4);
    header.version     = 0x00020401;
    header.data_length = (uint32_t)msg_len;
    header.checksum    = compute_checksum(encrypted, msg_len);
    header.timestamp   = (uint64_t)time(NULL);

    /* Écrire le fichier de sortie */
    FILE *f = fopen(OUTPUT_PATH, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot open output file %s\n", OUTPUT_PATH);
        free(encrypted);
        return 0;
    }

    fwrite(&header, sizeof(MysteryHeader), 1, f);
    fwrite(encrypted, 1, msg_len, f);
    fclose(f);

    printf("[+] Message encrypted and written to %s (%zu bytes)\n",
           OUTPUT_PATH, sizeof(MysteryHeader) + msg_len);

    if (g_verbose) {
        printf("[*] Checksum: 0x%08X\n", header.checksum);
        printf("[*] Timestamp: %lu\n", header.timestamp);
    }

    free(encrypted);
    return 1;
}

/*
 * interactive_mode — Boucle principale d'interaction
 */
static void interactive_mode(void)
{
    char input[MAX_INPUT];

    printf("\nCommands: encrypt <message> | status | quit\n");

    while (1) {
        printf("mystery> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        /* Retirer le newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            printf("Goodbye.\n");
            break;
        } else if (strcmp(input, "status") == 0) {
            printf("[*] Tool: %s\n", VERSION_TAG);
            printf("[*] Config: %s\n", CONFIG_PATH);
            printf("[*] Output: %s\n", OUTPUT_PATH);
            printf("[*] XOR key length: %d bytes\n", XOR_KEY_LEN);
            printf("[*] Verbose: %s\n", g_verbose ? "on" : "off");
        } else if (strncmp(input, "encrypt ", 8) == 0) {
            const char *message = input + 8;
            if (strlen(message) == 0) {
                printf("Usage: encrypt <message>\n");
            } else {
                process_message(message);
            }
        } else if (strlen(input) > 0) {
            printf("Unknown command: '%s'. Try: encrypt, status, quit\n", input);
        }
    }
}

/* --------------------------------------------------------------------------
 * Point d'entrée
 * -------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    /* Vérification anti-debug (légère, contournable) */
    if (check_debugger()) {
        fprintf(stderr, "[!] Exiting due to debugger presence.\n");
        return 2;
    }

    /* Charger la configuration */
    load_config();

    /* Vérifier les arguments */
    if (argc > 1 && strcmp(argv[1], "--verbose") == 0) {
        g_verbose = 1;
    }

    /* Authentification */
    if (!authenticate_user()) {
        return 1;
    }

    /* Mode interactif */
    interactive_mode();

    return 0;
}
