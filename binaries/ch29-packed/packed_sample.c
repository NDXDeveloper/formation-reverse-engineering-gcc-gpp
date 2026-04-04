/*
 * packed_sample.c — Binaire d'entraînement pour le Chapitre 29
 * Formation Reverse Engineering — Chaîne GNU
 *
 * Description :
 *   Programme volontairement riche en chaînes de caractères, constantes
 *   reconnaissables et logique de vérification. Une fois packé avec UPX,
 *   toutes ces informations disparaissent de l'analyse statique.
 *   L'objectif de l'exercice est de :
 *     1. Détecter que le binaire est packé
 *     2. Le décompresser (statiquement ou dynamiquement)
 *     3. Reconstruire un ELF analysable
 *     4. Retrouver la logique ci-dessous
 *
 * Compilation : voir Makefile associé
 * Licence MIT — Usage strictement éducatif
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ──────────────────────────────────────────────
 * Constantes reconnaissables en analyse statique
 * (visibles avec `strings` sur le binaire non packé)
 * ────────────────────────────────────────────── */

#define BANNER \
    "╔══════════════════════════════════════╗\n" \
    "║   Ch29 — PackedSample v1.0          ║\n" \
    "║   Formation RE — Chaîne GNU         ║\n" \
    "╚══════════════════════════════════════╝\n"

#define SECRET_FLAG   "FLAG{unp4ck3d_and_r3c0nstruct3d}"
#define AUTHOR_TAG    "Auteur: Formation-RE-GNU"
#define BUILD_MARKER  "BUILD:ch29-packed-2025"

/* Marqueur volontairement placé dans .rodata pour l'exercice ImHex */
static const char g_watermark[] = "<<< WATERMARK:PACKED_SAMPLE_ORIGINAL >>>";

/* ──────────────────────────────────────────────
 * Constantes « magiques » simulant une routine
 * crypto (reconnaissables dans un hex dump)
 * Ici : les 16 premiers octets de la S-box AES
 * ────────────────────────────────────────────── */
static const uint8_t g_fake_sbox[16] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5,
    0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76
};

/* ──────────────────────────────────────────────
 * Clé XOR embarquée (8 octets)
 * ────────────────────────────────────────────── */
static const uint8_t g_xor_key[8] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE
};

/* ──────────────────────────────────────────────
 * Message chiffré par XOR avec g_xor_key
 * Texte clair : "SUCCESS!" (8 octets)
 *   'S'^0xDE=0x8D  'U'^0xAD=0xF8  'C'^0xBE=0xFD
 *   'C'^0xEF=0xAC  'E'^0xCA=0x8F  'S'^0xFE=0xAD
 *   'S'^0xBA=0xE9  '!'^0xBE=0x9F
 * ────────────────────────────────────────────── */
static const uint8_t g_encrypted_msg[8] = {
    0x8D, 0xF8, 0xFD, 0xAC, 0x8F, 0xAD, 0xE9, 0x9F
};

/* ──────────────────────────────────────────────
 * Fonctions utilitaires
 * ────────────────────────────────────────────── */

/*
 * xor_decode — Déchiffre un buffer avec une clé XOR cyclique.
 *   dst : buffer de sortie (doit être alloué par l'appelant, +1 pour '\0')
 *   src : données chiffrées
 *   len : taille des données
 *   key : clé XOR
 *   klen: taille de la clé
 */
static void xor_decode(char *dst, const uint8_t *src, size_t len,
                        const uint8_t *key, size_t klen)
{
    for (size_t i = 0; i < len; i++) {
        dst[i] = (char)(src[i] ^ key[i % klen]);
    }
    dst[len] = '\0';
}

/*
 * compute_checksum — Calcule un checksum simple sur un buffer.
 *   Utilisé pour la vérification de la clé utilisateur.
 *   Algorithme : somme pondérée des octets (poids = position + 1),
 *   réduite modulo 0xFFFF.
 */
static uint32_t compute_checksum(const char *buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += (uint32_t)((unsigned char)buf[i]) * (uint32_t)(i + 1);
    }
    return sum & 0xFFFF;
}

/*
 * check_license_key — Vérifie si la clé de licence est valide.
 *
 *   Format attendu : "RE29-XXXX" où XXXX est un nombre hexadécimal
 *   tel que le checksum du préfixe "RE29-" vaut XXXX (en hexa).
 *
 *   Checksum("RE29-") :
 *     'R'*1 + 'E'*2 + '2'*3 + '9'*4 + '-'*5
 *     = 82 + 138 + 150 + 228 + 225 = 823
 *     = 0x0337
 *
 *   Donc la clé valide est : RE29-0337
 */
static int check_license_key(const char *key)
{
    const char *prefix = "RE29-";
    size_t prefix_len = strlen(prefix);

    /* Vérifier le préfixe */
    if (strlen(key) != 9) {
        return 0;
    }
    if (strncmp(key, prefix, prefix_len) != 0) {
        return 0;
    }

    /* Extraire la partie hexadécimale */
    const char *hex_part = key + prefix_len;
    char *endptr = NULL;
    unsigned long user_val = strtoul(hex_part, &endptr, 16);

    if (endptr == NULL || *endptr != '\0') {
        return 0;  /* Caractères non hexadécimaux */
    }

    /* Calculer le checksum attendu */
    uint32_t expected = compute_checksum(prefix, prefix_len);

    return (user_val == expected);
}

/* ──────────────────────────────────────────────
 * Affichage d'informations de débogage
 * (utile après unpacking pour vérifier la
 * reconstruction)
 * ────────────────────────────────────────────── */
static void print_debug_info(void)
{
    printf("[DEBUG] Auteur       : %s\n", AUTHOR_TAG);
    printf("[DEBUG] Build        : %s\n", BUILD_MARKER);
    printf("[DEBUG] Watermark    : %s\n", g_watermark);
    printf("[DEBUG] Fake S-box[0]: 0x%02X\n", g_fake_sbox[0]);
    printf("[DEBUG] Fake S-box[1]: 0x%02X\n", g_fake_sbox[1]);
    printf("[DEBUG] XOR key[0..3]: %02X %02X %02X %02X\n",
           g_xor_key[0], g_xor_key[1], g_xor_key[2], g_xor_key[3]);
}

/* ──────────────────────────────────────────────
 * Point d'entrée
 * ────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    printf("%s\n", BANNER);

    /* Mode debug : afficher les métadonnées internes */
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        print_debug_info();
        printf("\n");
    }

    /* Demander la clé de licence */
    printf("[*] Entrez votre clé de licence (format RE29-XXXX) : ");
    fflush(stdout);

    char input[64];
    if (fgets(input, sizeof(input), stdin) == NULL) {
        fprintf(stderr, "[!] Erreur de lecture.\n");
        return EXIT_FAILURE;
    }

    /* Retirer le saut de ligne */
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
        len--;
    }

    /* Vérification */
    if (check_license_key(input)) {
        /* Déchiffrer et afficher le message de succès */
        char decoded[9];
        xor_decode(decoded, g_encrypted_msg, sizeof(g_encrypted_msg),
                   g_xor_key, sizeof(g_xor_key));

        printf("\n[+] Clé valide ! Message déchiffré : %s\n", decoded);
        printf("[+] Flag : %s\n", SECRET_FLAG);
        printf("[+] Bravo, vous avez retrouvé la logique après unpacking !\n");
    } else {
        printf("\n[-] Clé invalide.\n");
        printf("[-] Indice : analysez la fonction check_license_key...\n");
        printf("[-] ... mais pour cela, il faut d'abord unpacker le binaire.\n");
    }

    return EXIT_SUCCESS;
}
