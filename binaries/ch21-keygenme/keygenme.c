/*
 * keygenme.c — Crackme pédagogique pour la formation Reverse Engineering GNU
 *
 * Chapitre 21 — Reverse d'un programme C simple (keygenme)
 *
 * Description :
 *   Le programme demande un nom d'utilisateur et une clé de licence.
 *   La clé valide est dérivée du nom via un algorithme simple mais
 *   non trivial, conçu pour être intéressant à reverser :
 *     1. Calcul d'un hash sur le nom d'utilisateur (additions, XOR, rotations)
 *     2. Dérivation de 4 groupes hexadécimaux depuis le hash
 *     3. Formatage attendu : XXXX-XXXX-XXXX-XXXX
 *
 * Compilation : voir le Makefile associé (produit 5 variantes).
 *
 * Licence MIT — Usage strictement éducatif.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ──────────────────────────────────────────────
 * Constantes volontairement visibles via strings(1)
 * pour guider l'apprenant lors du triage initial.
 * ────────────────────────────────────────────── */
static const char BANNER[]    = "=== KeyGenMe v1.0 — RE Training ===";
static const char PROMPT_USER[] = "Enter username: ";
static const char PROMPT_KEY[]  = "Enter license key (XXXX-XXXX-XXXX-XXXX): ";
static const char MSG_OK[]      = "[+] Valid license! Welcome, %s.\n";
static const char MSG_FAIL[]    = "[-] Invalid license. Try again.\n";
static const char MSG_ERR_LEN[] = "[-] Username must be between 3 and 31 characters.\n";

#define USERNAME_MAX  32
#define KEY_LEN       19  /* XXXX-XXXX-XXXX-XXXX = 4*4 + 3 tirets */

#define HASH_SEED     0x5A3C6E2D
#define HASH_MUL      0x1003F
#define HASH_XOR      0xDEADBEEF

/* ──────────────────────────────────────────────
 * rotate_left — Rotation à gauche sur 32 bits.
 * Visible comme un pattern (shl + shr + or) en ASM.
 * ────────────────────────────────────────────── */
static uint32_t rotate_left(uint32_t value, unsigned int count)
{
    count &= 31;
    return (value << count) | (value >> (32 - count));
}

/* ──────────────────────────────────────────────
 * compute_hash — Fonction de hachage du username.
 *
 * Points d'intérêt pour le RE :
 *   - Constante HASH_SEED repérable dans .rodata / imm32
 *   - Boucle sur chaque caractère (pattern classique)
 *   - Mélange : multiplication, XOR, rotation
 * ────────────────────────────────────────────── */
static uint32_t compute_hash(const char *username)
{
    uint32_t h = HASH_SEED;
    size_t len = strlen(username);

    for (size_t i = 0; i < len; i++) {
        h += (uint32_t)username[i];
        h *= HASH_MUL;
        h = rotate_left(h, (unsigned int)(username[i] & 0x0F));
        h ^= HASH_XOR;
    }

    /* Avalanche final pour diffuser les bits */
    h ^= (h >> 16);
    h *= 0x45D9F3B;
    h ^= (h >> 16);

    return h;
}

/* ──────────────────────────────────────────────
 * derive_key — Dérive 4 groupes de 16 bits depuis le hash.
 *
 * Chaque groupe subit une transformation différente
 * pour rendre l'algorithme plus intéressant à reverser.
 * ────────────────────────────────────────────── */
static void derive_key(uint32_t hash, uint16_t groups[4])
{
    groups[0] = (uint16_t)((hash & 0xFFFF) ^ 0xA5A5);
    groups[1] = (uint16_t)(((hash >> 16) & 0xFFFF) ^ 0x5A5A);
    groups[2] = (uint16_t)((rotate_left(hash, 7) & 0xFFFF) ^ 0x1234);
    groups[3] = (uint16_t)((rotate_left(hash, 13) & 0xFFFF) ^ 0xFEDC);
}

/* ──────────────────────────────────────────────
 * format_key — Formate la clé en "XXXX-XXXX-XXXX-XXXX".
 * Le buffer doit faire au moins KEY_LEN + 1 octets.
 * ────────────────────────────────────────────── */
static void format_key(const uint16_t groups[4], char *out)
{
    snprintf(out, KEY_LEN + 1, "%04X-%04X-%04X-%04X",
             groups[0], groups[1], groups[2], groups[3]);
}

/* ──────────────────────────────────────────────
 * check_license — Point central de la vérification.
 *
 * C'est cette fonction que l'apprenant doit localiser.
 * Le strcmp final est le point de décision clé :
 *   - jz  → clé valide
 *   - jnz → clé invalide
 *
 * Retourne 1 si la clé est valide, 0 sinon.
 * ────────────────────────────────────────────── */
static int check_license(const char *username, const char *user_key)
{
    uint32_t hash;
    uint16_t groups[4];
    char expected[KEY_LEN + 1];

    hash = compute_hash(username);
    derive_key(hash, groups);
    format_key(groups, expected);

    /* ── Point de décision : strcmp ── */
    if (strcmp(expected, user_key) == 0) {
        return 1;
    }
    return 0;
}

/* ──────────────────────────────────────────────
 * read_line — Lecture sécurisée d'une ligne (sans newline).
 * ────────────────────────────────────────────── */
static int read_line(char *buf, size_t size)
{
    if (fgets(buf, (int)size, stdin) == NULL)
        return -1;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';

    return 0;
}

/* ──────────────────────────────────────────────
 * main — Point d'entrée.
 * ────────────────────────────────────────────── */
int main(void)
{
    char username[USERNAME_MAX];
    char user_key[KEY_LEN + 2]; /* +2 pour le \n et le \0 */

    printf("%s\n\n", BANNER);

    /* Lecture du nom d'utilisateur */
    printf("%s", PROMPT_USER);
    if (read_line(username, sizeof(username)) != 0)
        return EXIT_FAILURE;

    size_t ulen = strlen(username);
    if (ulen < 3 || ulen >= USERNAME_MAX) {
        printf("%s", MSG_ERR_LEN);
        return EXIT_FAILURE;
    }

    /* Lecture de la clé de licence */
    printf("%s", PROMPT_KEY);
    if (read_line(user_key, sizeof(user_key)) != 0)
        return EXIT_FAILURE;

    /* Vérification */
    if (check_license(username, user_key)) {
        printf(MSG_OK, username);
        return EXIT_SUCCESS;
    } else {
        printf("%s", MSG_FAIL);
        return EXIT_FAILURE;
    }
}
