/* ============================================================================
 * Chapitre 16 — Section 16.2
 * inlining_demo.c — Inlining de fonctions : quand la fonction disparaît
 *
 * Ce fichier illustre les différents scénarios d'inlining par GCC :
 *
 *   1. Fonction triviale (getter/setter) — inlinée dès -O1
 *   2. Fonction moyenne — inlinée à partir de -O2
 *   3. Fonction volumineuse — rarement inlinée sauf __attribute__((always_inline))
 *   4. Fonction récursive — jamais inlinée (sauf déroulement partiel en -O3)
 *   5. Fonction appelée via pointeur — jamais inlinée (appel indirect)
 *   6. Chaîne d'inlining (A appelle B qui appelle C) — propagation
 *   7. Impact sur le graphe d'appels vu dans Ghidra
 *
 * Observations clés pour le RE :
 *   - En -O0 : chaque fonction génère un symbole, un prologue/épilogue,
 *     un call explicite. Le graphe d'appels est complet.
 *   - En -O2 : les petites fonctions disparaissent du binaire. Leur code
 *     est fusionné dans l'appelant. Le graphe d'appels de Ghidra est
 *     incomplet — les fonctions inlinées n'apparaissent plus comme XREF.
 *   - En -O3 : inlining encore plus agressif, y compris des fonctions
 *     de taille moyenne appelées plusieurs fois (duplication de code).
 *
 * Compilez et comparez :
 *   objdump -d -M intel build/inlining_demo_O0 | grep '<.*>:'
 *   objdump -d -M intel build/inlining_demo_O2 | grep '<.*>:'
 *   → Compter le nombre de fonctions visibles dans chaque cas.
 *
 * Licence MIT — Usage strictement éducatif.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * 1. Fonctions triviales — inlinées dès -O1
 *
 * Ces fonctions sont si courtes que le coût du call/ret dépasse le coût
 * du corps. GCC les inline systématiquement dès -O1.
 * En -O0, elles génèrent un vrai call avec prologue push rbp / mov rbp, rsp.
 * ========================================================================== */

typedef struct {
    int x;
    int y;
    int z;
} Vec3;

/* Getter — une seule instruction utile (mov) */
static int vec3_get_x(const Vec3 *v)
{
    return v->x;
}

/* Setter — un store + un return */
static void vec3_set_x(Vec3 *v, int val)
{
    v->x = val;
}

/* Calcul trivial — sera réduit à un lea ou add */
static int vec3_length_squared(const Vec3 *v)
{
    return v->x * v->x + v->y * v->y + v->z * v->z;
}

/* ==========================================================================
 * 2. Fonction de taille moyenne — inlinée en -O2 si appelée peu de fois
 *
 * GCC utilise une heuristique basée sur la taille estimée du corps
 * (en "gimple statements") et le nombre de sites d'appel.
 * Une fonction appelée une seule fois est presque toujours inlinée en -O2.
 * ========================================================================== */

static int transform_value(int input, int factor, int offset)
{
    int result = input;

    /* Quelques opérations — pas trivial, mais pas énorme */
    result = result * factor;
    result = result + offset;

    if (result < 0)
        result = -result;

    result = result % 1000;

    if (result > 500)
        result = 1000 - result;

    return result;
}

/* ==========================================================================
 * 3. Fonction volumineuse — résiste à l'inlining en -O2
 *
 * Cette fonction est délibérément longue. GCC ne l'inline pas en -O2
 * car le surcoût en taille de code dépasse le gain.
 * On peut forcer l'inlining avec __attribute__((always_inline)).
 *
 * Pour le RE : une fonction qui reste visible est plus facile à analyser.
 * ========================================================================== */

static int heavy_computation(const int *data, int len)
{
    int acc = 0;
    int min_val = data[0];
    int max_val = data[0];

    for (int i = 0; i < len; i++) {
        acc += data[i] * data[i];
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }

    int range = max_val - min_val;
    if (range == 0) range = 1;

    int normalized = 0;
    for (int i = 0; i < len; i++) {
        normalized += ((data[i] - min_val) * 100) / range;
    }

    int checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum ^= (data[i] << (i % 8));
        checksum = (checksum >> 3) | (checksum << 29);  /* rotate right 3 */
    }

    return acc + normalized + checksum;
}

/* ==========================================================================
 * 4. Fonction récursive — PAS inlinée
 *
 * L'inlining d'une fonction récursive est théoriquement impossible
 * (profondeur inconnue à la compilation). GCC ne l'inline jamais
 * directement, mais peut dérouler les premiers niveaux en -O3.
 * ========================================================================== */

static int fibonacci(int n)
{
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

/* Version itérative pour comparaison — sera inlinée en -O2 si appelée
 * une seule fois. */
static int fibonacci_iter(int n)
{
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

/* ==========================================================================
 * 5. Appel indirect via pointeur de fonction — JAMAIS inliné
 *
 * Le compilateur ne peut pas inliner un appel dont la cible est
 * déterminée à l'exécution. C'est le même mécanisme qui rend le
 * dispatch virtuel C++ (via vtable) opaque pour l'optimiseur.
 *
 * Pour le RE : un call [rax] ou call [rbx+offset] signale un appel
 * indirect — cherchez d'où vient le pointeur.
 * ========================================================================== */

typedef int (*operation_fn)(int, int);

static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }

static int apply_operation(operation_fn op, int a, int b)
{
    /* call [registre] — jamais inliné */
    return op(a, b);
}

/* ==========================================================================
 * 6. Chaîne d'inlining — A appelle B qui appelle C
 *
 * En -O2, C est inliné dans B, puis B+C est inliné dans A.
 * Résultat : dans le binaire, seul A existe, avec tout le code fusionné.
 * Le graphe d'appels perd deux niveaux.
 * ========================================================================== */

static int step_c(int x)
{
    return x * 3 + 1;
}

static int step_b(int x)
{
    int tmp = step_c(x);
    return tmp + step_c(tmp);
}

static int step_a(int x)
{
    return step_b(x) + step_b(x + 1);
}

/* ==========================================================================
 * 7. Fonctions avec __attribute__ — contrôle explicite
 *
 * noinline : force GCC à NE PAS inliner (utile pour le debug/profiling).
 * always_inline : force l'inlining même si GCC le juge non rentable.
 * ========================================================================== */

__attribute__((noinline))
static int forced_noinline(int x)
{
    return x * x + x + 1;
}

/* always_inline doit être static inline pour fonctionner */
__attribute__((always_inline))
static inline int forced_inline(int x)
{
    /* Ce code sera dupliqué à chaque site d'appel, même s'il est gros */
    int result = x;
    for (int i = 0; i < 10; i++) {
        result = (result * 31) ^ (result >> 3);
    }
    return result;
}

/* ==========================================================================
 * Point d'entrée
 * ========================================================================== */

int main(int argc, char *argv[])
{
    int input = 10;
    if (argc > 1)
        input = atoi(argv[1]);

    /* --- 1. Fonctions triviales (inlinées dès -O1) --- */
    Vec3 v = { input, input + 1, input + 2 };
    vec3_set_x(&v, input * 2);
    int vx = vec3_get_x(&v);
    int vlen = vec3_length_squared(&v);
    printf("Vec3: x=%d, len²=%d\n", vx, vlen);

    /* --- 2. Fonction moyenne (inlinée en -O2 si un seul site d'appel) --- */
    int transformed = transform_value(input, 7, -13);
    printf("Transformed: %d\n", transformed);

    /* --- 3. Fonction volumineuse (reste un call même en -O2) --- */
    int data[] = { input, input*2, input*3, input+5, input-3,
                   input*4, input+7, input-1 };
    int heavy = heavy_computation(data, 8);
    printf("Heavy: %d\n", heavy);

    /* --- 4. Récursion vs itération --- */
    int fib_r = fibonacci(input % 25);       /* Garde un call récursif */
    int fib_i = fibonacci_iter(input % 90);  /* Peut être inlinée */
    printf("Fib recursive(%d)=%d, iterative(%d)=%d\n",
           input % 25, fib_r, input % 90, fib_i);

    /* --- 5. Appel indirect (jamais inliné) --- */
    operation_fn ops[] = { op_add, op_sub, op_mul };
    int op_result = apply_operation(ops[input % 3], input, input + 5);
    printf("Indirect call result: %d\n", op_result);

    /* --- 6. Chaîne d'inlining A → B → C --- */
    int chain = step_a(input);
    printf("Chain A->B->C: %d\n", chain);

    /* --- 7. Attributs noinline / always_inline --- */
    int ni = forced_noinline(input);        /* Toujours un call */
    int fi = forced_inline(input);          /* Toujours inliné */
    int fi2 = forced_inline(input + 1);     /* Dupliqué ici aussi */
    printf("noinline=%d, always_inline=%d, %d\n", ni, fi, fi2);

    return 0;
}
