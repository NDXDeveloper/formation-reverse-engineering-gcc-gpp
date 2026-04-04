/**
 * binaries/ch13-keygenme/keygenme.c
 *
 * Crackme d'entraînement pour le reverse engineering.
 * Demande une clé de licence et vérifie sa validité.
 *
 * Compilation : voir Makefile
 * Usage :      ./keygenme_O0
 *              ./keygenme_O0 <clé>    (mode non-interactif)
 *
 * Clé valide : GCC-RE-2024-XPRO
 *
 * Architecture de la vérification :
 *   validate_key(input)
 *     ├── strlen(input)                    → vérification de longueur
 *     ├── vérifications format / préfixe   → inline, pas de court-circuit
 *     ├── compute_hash(input, len, ...)    → normalise + calcule checksum
 *     └── check_hash(hash_buf, checksum)   → strcmp avec clé de référence
 *
 *   Toutes les étapes sont exécutées inconditionnellement :
 *   strlen() et strcmp() sont TOUJOURS visibles dans les traces
 *   Frida / strace / ltrace, quel que soit l'input fourni.
 *
 * Traces attendues avec frida-trace (input "AAAA") :
 *   puts("=== KeyGenMe v1.0 ===")
 *   puts("Entrez la clé de licence :")
 *   scanf()
 *   strlen("AAAA")
 *   strcmp("AAAA", "GCC-RE-2024-XPRO")
 *   puts("Clé invalide. Accès refusé.")
 *
 * Traces avec frida-trace -I "keygenme_O0" :
 *   main()
 *      | print_banner()
 *      | read_input()
 *      | validate_key()
 *      |    | compute_hash()
 *      |    | check_hash()
 *      | print_result()
 *
 * Licence MIT — usage strictement éducatif.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════
 * Constantes
 * ═══════════════════════════════════════════════ */

#define KEY_LENGTH        16
#define CHECKSUM_EXPECTED 0x03FC   /* somme ASCII de "GCC-RE-2024-XPRO" = 1020 */
#define XOR_KEY           0x37     /* clé XOR pour l'encodage de la référence   */
#define MAX_INPUT         256

/*
 * Clé de référence encodée en XOR avec XOR_KEY (0x37).
 * Clé en clair : "GCC-RE-2024-XPRO"
 *
 * Calcul :
 *   'G' (0x47) ^ 0x37 = 0x70    'R' (0x52) ^ 0x37 = 0x65
 *   'C' (0x43) ^ 0x37 = 0x74    'E' (0x45) ^ 0x37 = 0x72
 *   '-' (0x2D) ^ 0x37 = 0x1A    '2' (0x32) ^ 0x37 = 0x05
 *   '0' (0x30) ^ 0x37 = 0x07    '4' (0x34) ^ 0x37 = 0x03
 *   'X' (0x58) ^ 0x37 = 0x6F    'P' (0x50) ^ 0x37 = 0x67
 *   'R' (0x52) ^ 0x37 = 0x65    'O' (0x4F) ^ 0x37 = 0x78
 *   '\0'(0x00) ^ 0x37 = 0x37
 */
static const uint8_t encoded_key[] = {
    0x70, 0x74, 0x74, 0x1A,  /* G  C  C  -  */
    0x65, 0x72, 0x1A,        /* R  E  -     */
    0x05, 0x07, 0x05, 0x03,  /* 2  0  2  4  */
    0x1A,                    /* -           */
    0x6F, 0x67, 0x65, 0x78,  /* X  P  R  O  */
    0x37                     /* \0          */
};

/* ═══════════════════════════════════════════════
 * Bannière
 * ═══════════════════════════════════════════════ */

static void print_banner(void) {
    puts("=== KeyGenMe v1.0 ===");
    puts("");
}

/* ═══════════════════════════════════════════════
 * Fonctions de vérification
 * ═══════════════════════════════════════════════ */

/**
 * Décode la clé de référence encodée en XOR.
 * Écrit le résultat dans `out` (KEY_LENGTH + 1 octets).
 */
static void decode_reference_key(char *out) {
    for (int i = 0; i <= KEY_LENGTH; i++) {
        out[i] = (char)(encoded_key[i] ^ XOR_KEY);
    }
}

/**
 * Normalise l'input et calcule un checksum additif.
 *
 * - Copie l'input dans hash_buf (tronqué ou paddé à KEY_LENGTH).
 * - Calcule la somme des codes ASCII et la stocke dans *checksum_out.
 *
 * Le hash_buf contient l'input normalisé en longueur, donc
 * le strcmp() dans check_hash() compare l'input utilisateur
 * (ou sa version tronquée) à la clé de référence.
 *
 * input_len est fourni par l'appelant pour éviter un second
 * appel à strlen() (qui serait visible dans les traces Frida).
 */
static void compute_hash(const char *input, size_t input_len,
                         char *hash_buf, uint16_t *checksum_out) {
    /* Normaliser : copier jusqu'à KEY_LENGTH caractères */
    memset(hash_buf, 0, KEY_LENGTH + 1);
    size_t copy_len = (input_len < KEY_LENGTH) ? input_len : KEY_LENGTH;
    memcpy(hash_buf, input, copy_len);
    hash_buf[KEY_LENGTH] = '\0';

    /* Calculer le checksum additif (somme des codes ASCII) */
    uint16_t sum = 0;
    for (int i = 0; i < KEY_LENGTH; i++) {
        sum += (uint8_t)hash_buf[i];
    }
    *checksum_out = sum;
}

/**
 * Vérifie le hash :
 *   1. Contrôle que le checksum correspond à CHECKSUM_EXPECTED.
 *   2. Décode la clé de référence et la compare via strcmp().
 *
 * Le strcmp() est TOUJOURS exécuté — pas de court-circuit sur
 * le checksum — pour garantir sa visibilité dans les traces.
 *
 * Retourne 1 si tout est valide, 0 sinon.
 */
static int check_hash(const char *hash_buf, uint16_t checksum) {
    int valid = 1;

    /* Vérification du checksum */
    if (checksum != CHECKSUM_EXPECTED) {
        valid = 0;
        /* On continue — pas de court-circuit */
    }

    /* Décoder la clé de référence et comparer via strcmp.
     * C'est CE strcmp que Frida intercepte et qui révèle
     * la clé attendue "GCC-RE-2024-XPRO" en second argument. */
    char reference[KEY_LENGTH + 1];
    decode_reference_key(reference);

    if (strcmp(hash_buf, reference) != 0) {
        valid = 0;
    }

    return valid;
}

/**
 * Fonction principale de validation.
 *
 * Exécute TOUTES les étapes inconditionnellement pour que
 * strlen() et strcmp() soient toujours visibles dans les
 * traces, quel que soit l'input.
 *
 * Retourne 1 si la clé est valide, 0 sinon.
 */
static int validate_key(const char *key) {
    int valid = 1;

    /* ── Étape 1 : longueur ──
     * Un seul appel à strlen — la valeur est réutilisée ensuite. */
    size_t len = strlen(key);
    if (len != KEY_LENGTH) {
        valid = 0;
    }

    /* ── Étape 2 : format XXX-XX-XXXX-XXXX ──
     * Vérifier uniquement si la longueur permet l'accès aux indices.
     * Pas de court-circuit : on met valid à 0 mais on continue. */
    if (len >= KEY_LENGTH) {
        if (key[3] != '-' || key[6] != '-' || key[11] != '-') {
            valid = 0;
        }
    } else {
        valid = 0;
    }

    /* ── Étape 3 : préfixe "GCC" ── */
    if (len >= 3) {
        if (key[0] != 'G' || key[1] != 'C' || key[2] != 'C') {
            valid = 0;
        }
    } else {
        valid = 0;
    }

    /* ── Étape 4 : compute_hash + check_hash ──
     * Toujours exécuté, même si les étapes précédentes ont échoué.
     * compute_hash normalise l'input et calcule le checksum.
     * check_hash compare via strcmp et vérifie le checksum. */
    char hash_buf[KEY_LENGTH + 1];
    uint16_t checksum;

    compute_hash(key, len, hash_buf, &checksum);

    if (!check_hash(hash_buf, checksum)) {
        valid = 0;
    }

    return valid;
}

/* ═══════════════════════════════════════════════
 * Affichage du résultat
 * ═══════════════════════════════════════════════ */

static void print_result(int valid) {
    puts("");
    if (valid) {
        puts("Clé valide ! Accès autorisé.");
    } else {
        puts("Clé invalide. Accès refusé.");
    }
}

/* ═══════════════════════════════════════════════
 * Lecture de l'entrée utilisateur
 * ═══════════════════════════════════════════════ */

/**
 * Lit la clé depuis stdin via scanf.
 * Retourne 0 si OK, -1 en cas d'erreur.
 */
static int read_input(char *buf, size_t bufsize) {
    puts("Entrez la clé de licence :");

    if (scanf("%255s", buf) != 1) {
        return -1;
    }
    buf[bufsize - 1] = '\0';

    return 0;
}

/* ═══════════════════════════════════════════════
 * Point d'entrée
 * ═══════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    char input[MAX_INPUT];

    print_banner();

    if (argc > 1) {
        /* Mode non-interactif : clé passée en argument */
        strncpy(input, argv[1], MAX_INPUT - 1);
        input[MAX_INPUT - 1] = '\0';
    } else {
        /* Mode interactif : lecture depuis stdin */
        if (read_input(input, sizeof(input)) < 0) {
            fprintf(stderr, "Erreur de lecture.\n");
            return 1;
        }
    }

    int result = validate_key(input);
    print_result(result);

    return result ? 0 : 1;
}
