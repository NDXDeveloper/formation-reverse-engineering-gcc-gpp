/*
 * keygenme.c — Binaire d'entraînement pour la formation Reverse Engineering.
 *
 * Licence MIT — Usage strictement éducatif.
 *
 * Ce programme lit une clé utilisateur, lui applique une transformation
 * (XOR + rotation), puis compare le résultat avec une chaîne attendue.
 *
 * L'objectif du reverse engineer est de :
 *   1. Comprendre la transformation appliquée (transform_key).
 *   2. Retrouver la clé valide, soit par analyse statique, soit par
 *      résolution symbolique (angr/Z3), soit en écrivant un keygen.
 *
 * Compilation : voir le Makefile associé.
 */

#include <stdio.h>
#include <string.h>

/* Clé XOR utilisée pour la transformation — volontairement simple
 * pour un premier exercice de RE. */
#define XOR_KEY  0x2A
#define ROT_AMT  3

/* Chaîne attendue après transformation.
 * En clair, la clé valide est celle qui, après transform_key(),
 * produit exactement cette chaîne. */
static const char *expected = "s3cr3t_k3y";

/*
 * transform_key — Transforme la clé utilisateur sur place.
 *
 * Pour chaque caractère :
 *   1. XOR avec XOR_KEY (0x2A)
 *   2. Rotation circulaire à gauche de ROT_AMT bits (sur 8 bits)
 *
 * La transformation est inversible, ce qui permet d'écrire un keygen.
 */
void transform_key(char *key)
{
    size_t i;
    unsigned char c;

    for (i = 0; key[i] != '\0'; i++) {
        c = (unsigned char)key[i];
        c ^= XOR_KEY;
        c = (c << ROT_AMT) | (c >> (8 - ROT_AMT));
        key[i] = (char)c;
    }
}

/*
 * check_key — Compare la clé transformée avec la valeur attendue.
 * Retourne 1 si la clé est valide, 0 sinon.
 */
int check_key(const char *transformed)
{
    return strcmp(transformed, expected) == 0;
}

int main(void)
{
    char input[26];  /* 25 caractères max + '\0' */

    printf("Enter key: ");
    fflush(stdout);

    if (scanf("%25s", input) != 1) {
        fprintf(stderr, "Read error\n");
        return 1;
    }

    transform_key(input);

    if (check_key(input)) {
        puts("Access granted");
    } else {
        puts("Wrong key");
    }

    return 0;
}
