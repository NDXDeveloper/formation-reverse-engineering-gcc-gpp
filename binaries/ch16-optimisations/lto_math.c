/* ============================================================================
 * Chapitre 16 — Section 16.5
 * lto_math.c — Fonctions mathématiques pour la démo LTO
 *
 * Ce fichier est compilé SÉPARÉMENT de lto_main.c et lto_utils.c.
 *
 * Sans LTO (-flto) :
 *   Chaque fichier .c est compilé en un .o indépendant. Le compilateur
 *   ne peut pas voir au-delà des frontières de fichier. Les fonctions
 *   définies ici ne peuvent PAS être inlinées dans lto_main.c, même si
 *   elles sont triviales. Chaque appel génère un call via le linker.
 *
 * Avec LTO (-flto) :
 *   GCC conserve une représentation intermédiaire (GIMPLE) dans les .o.
 *   Au moment du link, il fusionne tous les fichiers et peut :
 *     - Inliner ces fonctions dans main() (cross-module inlining)
 *     - Propager les constantes à travers les fichiers
 *     - Éliminer le code mort inter-module
 *     - Dévirtualiser des appels indirects
 *
 * Pour le RE :
 *   - Sans LTO, ces fonctions apparaissent comme des symboles distincts.
 *   - Avec LTO, elles peuvent disparaître entièrement du binaire.
 *   → Le graphe d'appels est radicalement différent.
 *
 * Licence MIT — Usage strictement éducatif.
 * ============================================================================ */

#include "lto_math.h"

/* --------------------------------------------------------------------------
 * Fonction triviale — sera inlinée cross-module avec -flto -O2.
 * Sans LTO, elle génère toujours un call.
 * -------------------------------------------------------------------------- */
int math_square(int x)
{
    return x * x;
}

/* --------------------------------------------------------------------------
 * Fonction triviale — même chose.
 * -------------------------------------------------------------------------- */
int math_cube(int x)
{
    return x * x * x;
}

/* --------------------------------------------------------------------------
 * Fonction de taille moyenne — inlinée avec LTO si peu de sites d'appel.
 *
 * Contient une boucle et un branchement, ce qui la rend "moyenne"
 * du point de vue des heuristiques d'inlining.
 * -------------------------------------------------------------------------- */
long math_sum_of_powers(int n, int power)
{
    long total = 0;
    for (int i = 1; i <= n; i++) {
        long val = 1;
        for (int p = 0; p < power; p++) {
            val *= i;
        }
        total += val;
    }
    return total;
}

/* --------------------------------------------------------------------------
 * Fonction avec constantes magiques pour le RE.
 *
 * Le hash polynomial utilise des constantes (31, 0x5F3759DF) reconnaissables
 * dans le désassemblage. Avec LTO, ces constantes apparaissent directement
 * dans le corps de main() (ou de l'appelant), ce qui peut dérouter.
 *
 * Sans LTO : on voit un call math_hash → on regarde le corps → on repère
 *   la constante 31 et le pattern de hash polynomial.
 * Avec LTO : la constante 31 apparaît dans main() sans contexte évident.
 * -------------------------------------------------------------------------- */
unsigned int math_hash(const char *str)
{
    unsigned int hash = 0x5F3759DF;  /* Constante reconnaissable */

    while (*str) {
        hash = hash * 31 + (unsigned char)(*str);
        str++;
    }

    /* Finalisation — bit mixing */
    hash ^= (hash >> 16);
    hash *= 0x45D9F3B;
    hash ^= (hash >> 16);

    return hash;
}

/* --------------------------------------------------------------------------
 * Fonction avec division par constante — le magic number.
 *
 * Sans LTO : la division par 7 génère un magic number dans math_divide_sum.
 * Avec LTO : le magic number apparaît dans l'appelant, mélangé à son code.
 * -------------------------------------------------------------------------- */
int math_divide_sum(const int *data, int n, int divisor)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += data[i] / divisor;
    }
    return sum;
}

/* --------------------------------------------------------------------------
 * Fonction qui ne sera PAS inlinée même avec LTO — trop volumineuse.
 *
 * Le seuil d'inlining de GCC est basé sur le nombre de "gimple statements".
 * Cette fonction est délibérément longue pour rester comme un call.
 * -------------------------------------------------------------------------- */
long math_complex_transform(const int *data, int n)
{
    long result = 0;
    long running_min = data[0];
    long running_max = data[0];
    long running_avg = 0;

    /* Première passe : statistiques */
    for (int i = 0; i < n; i++) {
        if (data[i] < running_min) running_min = data[i];
        if (data[i] > running_max) running_max = data[i];
        running_avg += data[i];
    }
    running_avg /= (n > 0 ? n : 1);

    /* Deuxième passe : transformation */
    long range = running_max - running_min;
    if (range == 0) range = 1;

    for (int i = 0; i < n; i++) {
        long normalized = ((data[i] - running_min) * 1000) / range;
        long deviation = data[i] - running_avg;
        result += normalized * normalized + deviation;
    }

    /* Troisième passe : checksum rotatif */
    unsigned int checksum = 0xDEADBEEF;
    for (int i = 0; i < n; i++) {
        checksum ^= (unsigned int)(data[i] * 0x9E3779B9);
        checksum = (checksum << 7) | (checksum >> 25);
    }

    return result + (long)checksum;
}
