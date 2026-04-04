/* ============================================================================
 * Chapitre 16 — Section 16.5
 * lto_main.c — Point d'entrée de la démo Link-Time Optimization
 *
 * Ce fichier utilise les fonctions de lto_math.c et lto_utils.c.
 *
 * Compilation et comparaison :
 *
 *   SANS LTO :
 *     gcc -O2 -g -o build/lto_demo_O2 lto_main.c lto_math.c lto_utils.c -lm
 *
 *   AVEC LTO :
 *     gcc -O2 -g -flto -o build/lto_demo_O2_flto lto_main.c lto_math.c lto_utils.c -lm
 *
 * Analyse recommandée — comparer les symboles visibles :
 *
 *   nm build/lto_demo_O2      | grep ' T '    → toutes les fonctions présentes
 *   nm build/lto_demo_O2_flto | grep ' T '    → fonctions triviales disparues
 *
 * Comparer le graphe d'appels dans Ghidra :
 *   → Sans LTO : main() appelle math_square, math_cube, utils_clamp, etc.
 *   → Avec LTO : main() contient le code inliné, moins de XREF.
 *
 * Comparer la taille :
 *   ls -la build/lto_demo_O2 build/lto_demo_O2_flto
 *   → LTO produit souvent un binaire légèrement plus petit (dead code
 *     elimination inter-module) ou plus gros (inlining cross-module).
 *
 * Licence MIT — Usage strictement éducatif.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "lto_math.h"
#include "lto_utils.h"

#define DATA_SIZE 32

int main(int argc, char *argv[])
{
    int input = 8;
    if (argc > 1)
        input = atoi(argv[1]);

    /* ----- Utilisation des fonctions triviales de lto_math ----- */

    /* Sans LTO : call math_square (symbole visible dans nm).
     * Avec LTO : inliné → imul ou lea dans le corps de main. */
    int sq = math_square(input);
    int cb = math_cube(input);
    printf("square(%d) = %d\n", input, sq);
    printf("cube(%d)   = %d\n", input, cb);

    /* ----- Fonctions de taille moyenne ----- */

    /* sum_of_powers : boucle imbriquée.
     * Sans LTO : call math_sum_of_powers.
     * Avec LTO : potentiellement inlinée si un seul site d'appel. */
    long sop2 = math_sum_of_powers(input, 2);  /* Somme des carrés */
    long sop3 = math_sum_of_powers(input, 3);  /* Somme des cubes */
    printf("sum_of_squares(%d) = %ld\n", input, sop2);
    printf("sum_of_cubes(%d)   = %ld\n", input, sop3);

    /* ----- Hash — constantes reconnaissables ----- */

    /* La constante 0x5F3759DF et le multiplicateur 31 de math_hash
     * apparaissent dans le corps de main() avec LTO.
     * Sans LTO, il faut suivre le call pour les trouver. */
    unsigned int h1 = math_hash("hello");
    unsigned int h2 = math_hash("reverse engineering");
    printf("hash('hello')      = 0x%08X\n", h1);
    printf("hash('reverse eng')= 0x%08X\n", h2);

    /* ----- Utilisation des fonctions de lto_utils ----- */

    int data[DATA_SIZE];

    /* utils_fill_sequence : triviale, inlinée avec LTO.
     * Sans LTO : call utils_fill_sequence. */
    utils_fill_sequence(data, DATA_SIZE, input, 3);

    /* utils_array_max : boucle simple, inlinée avec LTO. */
    int mx = utils_array_max(data, DATA_SIZE);
    printf("array_max = %d\n", mx);

    /* utils_clamp : très triviale, toujours inlinée avec LTO.
     * Sans LTO : call utils_clamp → on voit le cmp/cmov dedans.
     * Avec LTO : cmp/cmov directement dans main(). */
    int clamped = utils_clamp(sq, 0, 10000);
    printf("clamp(%d, 0, 10000) = %d\n", sq, clamped);

    /* utils_int_to_hex : taille moyenne. */
    char hexbuf[16];
    utils_int_to_hex(sq, hexbuf, sizeof(hexbuf));
    printf("hex(%d) = %s\n", sq, hexbuf);

    /* ----- Division par constante à travers les fichiers ----- */

    /* Sans LTO : call math_divide_sum. Le magic number de la division
     *   par 7 est dans math_divide_sum.
     * Avec LTO : le magic number se retrouve dans main(). */
    int dsum = math_divide_sum(data, DATA_SIZE, 7);
    printf("divide_sum(/7) = %d\n", dsum);

    /* ----- Fonction volumineuse — reste un call même avec LTO ----- */

    long ct = math_complex_transform(data, DATA_SIZE);
    printf("complex_transform = %ld\n", ct);

    /* ----- Affichage final (empêche dead code elimination) ----- */

    utils_print_array("data", data, DATA_SIZE);

    /* Utiliser sqrt pour justifier -lm et montrer un appel de lib
     * qui n'est JAMAIS inliné (bibliothèque partagée). */
    double root = sqrt((double)sq);
    printf("sqrt(%d) = %.4f\n", sq, root);

    return 0;
}
