/* ============================================================================
 * Chapitre 16 — Section 16.1
 * opt_levels_demo.c — Impact de -O0, -O1, -O2, -O3, -Os sur le désassemblage
 *
 * Ce fichier rassemble plusieurs patterns C courants dont la traduction
 * assembleur varie drastiquement selon le niveau d'optimisation :
 *
 *   - Arithmétique simple (additions, multiplications, divisions)
 *   - Branchements conditionnels (if/else, switch)
 *   - Boucle d'accumulation
 *   - Appel de fonction auxiliaire
 *   - Accès à un tableau sur la pile
 *
 * Compilez avec :
 *   gcc -O0 -g -o opt_levels_demo_O0 opt_levels_demo.c
 *   gcc -O2 -g -o opt_levels_demo_O2 opt_levels_demo.c
 *
 * Puis comparez :
 *   objdump -d -M intel opt_levels_demo_O0 | grep -A 40 '<compute>:'
 *   objdump -d -M intel opt_levels_demo_O2 | grep -A 40 '<compute>:'
 *
 * Licence MIT — Usage strictement éducatif.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Fonction auxiliaire simple — cible d'inlining potentiel dès -O1.
 * En -O0, elle génère un call explicite.
 * En -O2+, elle est inlinée et peut même être évaluée à la compilation
 * si les arguments sont des constantes.
 * -------------------------------------------------------------------------- */
static int square(int x)
{
    return x * x;
}

/* --------------------------------------------------------------------------
 * Fonction avec branchement conditionnel.
 * Points d'observation :
 *   -O0 : cmp + jle/jg classique, les deux branches sont présentes.
 *   -O1 : le compilateur peut utiliser cmov (conditional move) pour
 *          éliminer le branchement si le corps est simple.
 *   -O2 : propagation de constantes possible si appelée avec valeur connue.
 * -------------------------------------------------------------------------- */
static int clamp(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

/* --------------------------------------------------------------------------
 * Fonction de classification — switch/case.
 * Points d'observation :
 *   -O0 : cascade de cmp/je (comparaisons séquentielles).
 *   -O2 : GCC peut générer une jump table si les cases sont denses,
 *          ou une série de comparaisons réordonnées (binary search).
 *   -Os : préfère la cascade compacte à la jump table.
 * -------------------------------------------------------------------------- */
static const char *classify_grade(int score)
{
    switch (score / 10) {
        case 10:
        case 9:  return "A";
        case 8:  return "B";
        case 7:  return "C";
        case 6:  return "D";
        case 5:  return "E";
        default: return "F";
    }
}

/* --------------------------------------------------------------------------
 * Boucle d'accumulation avec arithmétique.
 * Points d'observation :
 *   -O0 : la variable de boucle et l'accumulateur vivent sur la pile,
 *          avec load/store à chaque itération.
 *   -O1 : les variables passent dans des registres.
 *   -O2 : le compilateur peut réduire la boucle à une formule fermée
 *          (Gauss) ou la dérouler partiellement.
 *   -O3 : déroulage agressif + potentielle vectorisation.
 * -------------------------------------------------------------------------- */
static long sum_of_squares(int n)
{
    long total = 0;
    for (int i = 1; i <= n; i++) {
        total += square(i);
    }
    return total;
}

/* --------------------------------------------------------------------------
 * Manipulation de tableau sur la pile + division.
 * Points d'observation :
 *   -O0 : chaque accès au tableau est un calcul d'offset explicite
 *          depuis rbp. La division génère un idiv.
 *   -O2 : GCC remplace la division par une constante par une
 *          multiplication par l'inverse modulaire ("magic number").
 *          Le tableau peut être partiellement éliminé si les valeurs
 *          sont propagées.
 * -------------------------------------------------------------------------- */
static int compute(int a, int b)
{
    int data[8];

    for (int i = 0; i < 8; i++) {
        data[i] = a * (i + 1) + b;
    }

    int result = 0;
    for (int i = 0; i < 8; i++) {
        result += data[i] / 7;      /* Division par constante → magic number en O2 */
    }

    result += data[3] % 5;          /* Modulo par constante → aussi transformé */

    return result;
}

/* --------------------------------------------------------------------------
 * Fonction avec chaîne de caractères et appel de bibliothèque.
 * Points d'observation :
 *   -O0 : strlen est un call explicite via PLT.
 *   -O2 : si la chaîne est une constante connue, strlen peut être
 *          remplacée par une constante à la compilation. puts peut
 *          remplacer printf quand il n'y a pas de format.
 * -------------------------------------------------------------------------- */
static void print_info(const char *label, int value)
{
    printf("[%s] (len=%zu) = %d\n", label, strlen(label), value);
}

/* --------------------------------------------------------------------------
 * Fonction à plusieurs paramètres — observer le passage dans les registres
 * System V AMD64 (rdi, rsi, rdx, rcx, r8, r9) puis débordement sur la pile.
 * -------------------------------------------------------------------------- */
static int multi_args(int a, int b, int c, int d, int e, int f, int g, int h)
{
    /* Les 6 premiers (a-f) passent par registres, g et h par la pile.
     * En -O0, tout est copié sur la pile dans le prologue.
     * En -O2, les calculs restent dans les registres. */
    return (a + b) * (c - d) + (e ^ f) - (g | h);
}

/* --------------------------------------------------------------------------
 * Point d'entrée — utilise toutes les fonctions pour empêcher GCC
 * de les éliminer comme code mort (dead code elimination).
 * -------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    int input = 42;
    if (argc > 1) {
        input = atoi(argv[1]);
    }

    /* square + sum_of_squares */
    int sq = square(input);
    long sos = sum_of_squares(input);

    /* clamp */
    int clamped = clamp(input, 0, 100);

    /* classify_grade */
    const char *grade = classify_grade(clamped);

    /* compute */
    int comp = compute(input, sq);

    /* multi_args */
    int multi = multi_args(input, sq, clamped, comp,
                           input + 1, sq - 1, clamped + 2, comp - 3);

    /* Affichage des résultats */
    print_info("square", sq);
    print_info("sum_of_squares", (int)sos);
    print_info("clamp", clamped);
    print_info("compute", comp);
    print_info("multi_args", multi);
    printf("Grade: %s\n", grade);

    return 0;
}
