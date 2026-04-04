/*
 * keygenme.c — Crackme / Keygenme d'entraînement
 *
 * Binaire d'entraînement pour le chapitre 15 (Fuzzing) .
 *
 * Format de clé valide :  RENG-XXXX-XXXX-XXXX
 *   - Préfixe fixe : "RENG-"
 *   - 3 groupes de 4 caractères hexadécimaux majuscules, séparés par '-'
 *   - Longueur totale : 19 caractères
 *   - Contraintes mathématiques entre les groupes :
 *       • groupe1 XOR groupe2 == 0xBEEF
 *       • groupe3 == (groupe1 + groupe2) & 0xFFFF
 *
 * Exemple de clé valide :  RENG-1234-ACDB-BF01
 *   groupe1 = 0x1234
 *   groupe2 = 0xACDB  → 0x1234 XOR 0xACDB = 0xBEEF  ✓
 *   groupe3 = 0xBF0F  → (0x1234 + 0xACDB) & 0xFFFF = 0xBF0F  ✓
 *   (Note : cet exemple est illustratif — vérifiez les calculs)
 *
 * La routine de validation comporte plusieurs étapes/branches,
 * ce qui la rend intéressante pour le fuzzing (section 15.2)
 * et pour l'analyse statique/dynamique (chapitre 21).
 *
 * Licence MIT — Usage strictement éducatif.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

/* ═══════════════════════════════════════════
 *  Constantes
 * ═══════════════════════════════════════════ */

#define KEY_LENGTH      19          /* "RENG-XXXX-XXXX-XXXX" */
#define PREFIX          "RENG-"
#define PREFIX_LEN      5
#define GROUP_LEN       4
#define SEPARATOR       '-'
#define XOR_SECRET      0xBEEFu
#define MAX_INPUT       256

/* ═══════════════════════════════════════════
 *  Fonctions utilitaires
 * ═══════════════════════════════════════════ */

/*
 * is_hex_group — Vérifie que `s` contient exactement `len`
 * caractères hexadécimaux majuscules (0-9, A-F).
 */
static int is_hex_group(const char *s, int len)
{
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

/*
 * hex_to_u16 — Convertit une chaîne de 4 caractères hex en uint16_t.
 * Suppose que la chaîne a déjà été validée par is_hex_group.
 */
static uint16_t hex_to_u16(const char *s)
{
    uint16_t val = 0;
    for (int i = 0; i < GROUP_LEN; i++) {
        val <<= 4;
        char c = s[i];
        if (c >= '0' && c <= '9')
            val |= (uint16_t)(c - '0');
        else if (c >= 'A' && c <= 'F')
            val |= (uint16_t)(c - 'A' + 10);
    }
    return val;
}

/* ═══════════════════════════════════════════
 *  Validation de la clé — étape par étape
 *
 *  Chaque étape est une branche distincte que le
 *  fuzzer peut découvrir progressivement.
 * ═══════════════════════════════════════════ */

/*
 * validate_key — Routine principale de validation.
 *
 * Retourne :
 *   0  = clé valide
 *  -1  = clé invalide (différentes raisons selon l'étape échouée)
 */
static int validate_key(const char *key)
{
    /* ── Étape 1 : vérification de la longueur ── */
    size_t len = strlen(key);
    if (len != KEY_LENGTH) {
        fprintf(stderr, "Error: invalid key length (expected %d, got %zu)\n",
                KEY_LENGTH, len);
        return -1;
    }

    /* ── Étape 2 : vérification du préfixe "RENG-" ── */
    if (strncmp(key, PREFIX, PREFIX_LEN) != 0) {
        fprintf(stderr, "Error: invalid prefix\n");
        return -1;
    }

    /* ── Étape 3 : vérification des séparateurs ── */
    /*    Positions attendues : 4, 9, 14 (index 0-based)    */
    if (key[4] != SEPARATOR || key[9] != SEPARATOR || key[14] != SEPARATOR) {
        fprintf(stderr, "Error: invalid separator positions\n");
        return -1;
    }

    /* ── Étape 4 : extraction et validation des groupes ── */
    const char *group1_str = key + PREFIX_LEN;       /* offset 5, "XXXX" */
    const char *group2_str = key + PREFIX_LEN + 5;   /* offset 10, "XXXX" */
    const char *group3_str = key + PREFIX_LEN + 10;  /* offset 15, "XXXX" */

    if (!is_hex_group(group1_str, GROUP_LEN)) {
        fprintf(stderr, "Error: group 1 contains non-hex characters\n");
        return -1;
    }
    if (!is_hex_group(group2_str, GROUP_LEN)) {
        fprintf(stderr, "Error: group 2 contains non-hex characters\n");
        return -1;
    }
    if (!is_hex_group(group3_str, GROUP_LEN)) {
        fprintf(stderr, "Error: group 3 contains non-hex characters\n");
        return -1;
    }

    /* ── Étape 5 : conversion en valeurs numériques ── */
    uint16_t g1 = hex_to_u16(group1_str);
    uint16_t g2 = hex_to_u16(group2_str);
    uint16_t g3 = hex_to_u16(group3_str);

    /* ── Étape 6 : contrainte XOR entre groupe 1 et groupe 2 ── */
    /*    g1 XOR g2 doit valoir 0xBEEF                           */
    uint16_t xor_result = g1 ^ g2;
    if (xor_result != XOR_SECRET) {
        fprintf(stderr, "Error: XOR check failed (0x%04X ^ 0x%04X = 0x%04X, expected 0x%04X)\n",
                g1, g2, xor_result, XOR_SECRET);
        return -1;
    }

    /* ── Étape 7 : contrainte somme pour le groupe 3 ── */
    /*    g3 doit valoir (g1 + g2) & 0xFFFF               */
    uint16_t expected_g3 = (g1 + g2) & 0xFFFF;
    if (g3 != expected_g3) {
        fprintf(stderr, "Error: checksum failed (expected 0x%04X, got 0x%04X)\n",
                expected_g3, g3);
        return -1;
    }

    /* ── Étape 8 (bonus) : contrainte supplémentaire ── */
    /*    Le groupe 1 ne doit pas être nul                */
    if (g1 == 0x0000) {
        fprintf(stderr, "Error: group 1 must not be zero\n");
        return -1;
    }

    /* ══════════════════ CLÉ VALIDE ══════════════════ */
    return 0;
}

/* ═══════════════════════════════════════════
 *  Fonctions d'affichage
 * ═══════════════════════════════════════════ */

static void print_banner(void)
{
    printf("╔══════════════════════════════════════╗\n");
    printf("║   RENG KeygenMe — Training Binary   ║\n");
    printf("║   Format: RENG-XXXX-XXXX-XXXX       ║\n");
    printf("╚══════════════════════════════════════╝\n\n");
}

static void print_success(const char *key)
{
    printf("✓ Key accepted: %s\n", key);
    printf("  Congratulations! The key is valid.\n");
}

static void print_failure(void)
{
    printf("✗ Key rejected.\n");
}

/* ═══════════════════════════════════════════
 *  Point d'entrée
 * ═══════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    char input[MAX_INPUT];

    if (argc >= 2) {
        /* Mode argument : clé passée via argv[1] */
        strncpy(input, argv[1], MAX_INPUT - 1);
        input[MAX_INPUT - 1] = '\0';
    } else {
        /* Mode interactif : lire depuis stdin */
        print_banner();
        printf("Enter your key: ");
        fflush(stdout);

        if (!fgets(input, MAX_INPUT, stdin)) {
            fprintf(stderr, "Error: failed to read input\n");
            return 1;
        }

        /* Retirer le newline final */
        size_t slen = strlen(input);
        if (slen > 0 && input[slen - 1] == '\n') {
            input[slen - 1] = '\0';
        }
    }

    /* ── Validation ── */
    int result = validate_key(input);

    if (result == 0) {
        print_success(input);
        return 0;
    } else {
        print_failure();
        return 1;
    }
}
