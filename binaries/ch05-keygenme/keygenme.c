/*
 * keygenme.c — Crackme d'entraînement
 *
 * Formation Reverse Engineering — Applications compilées avec la chaîne GNU
 * Licence MIT — Usage strictement éducatif
 *
 * Ce programme est utilisé dès le chapitre 5 (triage, strings, nm, ltrace)
 *
 * Fonctionnement :
 *   1. Demande une clé de licence au format XXXX-XXXX-XXXX-XXXX
 *   2. Génère la clé attendue à partir d'un seed hardcodé
 *   3. Compare l'entrée avec la clé attendue via strcmp
 *   4. Affiche "Access granted" ou "Access denied"
 *
 * Points d'intérêt pédagogiques :
 *   - Mot de passe seed en clair dans .rodata ("SuperSecret123")
 *   - strcmp visible dans ltrace avec les deux arguments
 *   - Fonctions nommées visibles dans nm (main, check_license, generate_expected_key)
 *   - Format de clé visible dans strings
 *   - Plusieurs niveaux d'optimisation compilables via le Makefile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Constantes
 * -------------------------------------------------------------------------- */

#define KEY_FORMAT_LEN   19   /* XXXX-XXXX-XXXX-XXXX = 4+1+4+1+4+1+4 = 19 */
#define MAX_INPUT        256
#define NUM_GROUPS       4
#define GROUP_LEN        4

/*
 * Seed utilisé pour générer la clé attendue.
 * Volontairement visible dans strings pour que l'apprenant le repère
 * lors du triage (section 5.1).
 */
static const char *LICENSE_SEED = "SuperSecret123";

/*
 * Alphabet pour la génération de la clé.
 * Produit des clés alphanumériques majuscules + chiffres.
 */
static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define ALPHABET_SIZE (sizeof(ALPHABET) - 1)  /* 36 */

/* --------------------------------------------------------------------------
 * Fonctions
 * -------------------------------------------------------------------------- */

/*
 * generate_expected_key — Génère la clé attendue à partir du seed
 *
 * Algorithme :
 *   - Hash simple du seed (somme pondérée des caractères)
 *   - Utilise le hash pour indexer dans l'alphabet
 *   - Produit 4 groupes de 4 caractères séparés par des tirets
 *
 * La clé résultante avec le seed "SuperSecret123" est : K3Y9-AX7F-QW2M-PL8N
 *
 * Paramètres :
 *   seed   — chaîne utilisée comme graine
 *   output — buffer de sortie (doit pouvoir contenir au moins 20 octets)
 */
void generate_expected_key(const char *seed, char *output)
{
    unsigned int hash = 0;
    size_t seed_len = strlen(seed);

    /* Calcul d'un hash simple à partir du seed */
    for (size_t i = 0; i < seed_len; i++) {
        hash += (unsigned char)seed[i] * (unsigned int)(i + 7);
        hash ^= (hash << 5) | (hash >> 27);
        hash += 0x9E3779B9;   /* Constante du golden ratio (Knuth) */
    }

    /* Génération des 4 groupes de 4 caractères */
    int pos = 0;
    for (int group = 0; group < NUM_GROUPS; group++) {
        for (int ch = 0; ch < GROUP_LEN; ch++) {
            /* Dériver un index dans l'alphabet */
            hash ^= (hash << 13);
            hash ^= (hash >> 17);
            hash ^= (hash << 5);
            output[pos++] = ALPHABET[hash % ALPHABET_SIZE];
        }
        if (group < NUM_GROUPS - 1) {
            output[pos++] = '-';
        }
    }
    output[pos] = '\0';
}

/*
 * check_license — Vérifie la clé de licence saisie par l'utilisateur
 *
 * Étapes :
 *   1. Vérifie la longueur (doit être exactement 19 caractères)
 *   2. Vérifie le format (tirets aux positions 4, 9, 14)
 *   3. Génère la clé attendue
 *   4. Compare avec strcmp
 *
 * Retourne : 1 si la clé est valide, 0 sinon.
 */
int check_license(const char *user_key)
{
    /* Vérification de la longueur */
    if (strlen(user_key) != KEY_FORMAT_LEN) {
        printf("Invalid key format. Expected: XXXX-XXXX-XXXX-XXXX\n");
        return 0;
    }

    /* Vérification des tirets aux bonnes positions */
    if (user_key[4] != '-' || user_key[9] != '-' || user_key[14] != '-') {
        printf("Invalid key format. Expected: XXXX-XXXX-XXXX-XXXX\n");
        return 0;
    }

    /* Générer la clé attendue */
    char expected[MAX_INPUT];
    generate_expected_key(LICENSE_SEED, expected);

    /* Comparaison — c'est ce strcmp que ltrace révélera */
    printf("Checking key...\n");
    if (strcmp(user_key, expected) == 0) {
        return 1;
    }

    return 0;
}

/*
 * main — Point d'entrée du programme
 */
int main(int argc, char *argv[])
{
    char input[MAX_INPUT];

    (void)argc;
    (void)argv;

    /* Prompt */
    printf("Enter your license key: ");
    fflush(stdout);

    /* Lire l'entrée utilisateur */
    if (fgets(input, sizeof(input), stdin) == NULL) {
        fprintf(stderr, "Error: failed to read input.\n");
        return 1;
    }

    /* Retirer le newline */
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }

    /* Vérifier la licence */
    if (check_license(input)) {
        puts("Access granted! Welcome.");
        return 0;
    } else {
        puts("Access denied. Invalid key.");
        return 1;
    }
}
