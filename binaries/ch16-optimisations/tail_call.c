/* ============================================================================
 * Chapitre 16 — Section 16.4
 * tail_call.c — Tail call optimization et son impact sur la pile
 *
 * La tail call optimization (TCO) transforme un appel de fonction en
 * position terminale (le dernier acte avant le return) en un simple jmp.
 * Conséquences :
 *   - Pas de nouveau frame sur la pile → pas de croissance de pile
 *   - Le call/ret disparaît → la backtrace GDB est tronquée
 *   - Une récursion terminale devient une boucle
 *
 * Ce fichier illustre :
 *   1. Récursion terminale (tail recursion) → transformée en boucle
 *   2. Récursion NON terminale → reste un call récursif
 *   3. Tail call vers une autre fonction (mutual recursion)
 *   4. Tail call empêché par du travail après l'appel
 *   5. Tail call empêché par des destructeurs / cleanup
 *   6. Impact sur la backtrace GDB
 *
 * Points d'observation :
 *   -O0 : JAMAIS de TCO. Chaque appel = call + nouveau frame.
 *   -O1 : TCO activée pour les cas simples (tail recursion directe).
 *   -O2 : TCO plus agressive, inclut les tail calls vers d'autres fonctions.
 *   -O3 : idem -O2 pour la TCO (pas de gain supplémentaire).
 *
 * Test clé : comparer la pile avec GDB
 *   gdb ./build/tail_call_O0 -ex 'b factorial_tail' -ex 'r 10' -ex 'bt'
 *   gdb ./build/tail_call_O2 -ex 'b factorial_tail' -ex 'r 10' -ex 'bt'
 *   → En O0, la backtrace montre N frames. En O2, un seul frame.
 *
 * Licence MIT — Usage strictement éducatif.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>

/* ==========================================================================
 * 1. Récursion terminale — factorielle avec accumulateur
 *
 * L'appel récursif est la dernière instruction : c'est un tail call.
 *
 *   -O0 : call factorial_tail à chaque niveau → N frames sur la pile.
 *   -O2 : le call est remplacé par un jmp au début de la fonction.
 *          Les paramètres sont mis à jour dans les registres, puis
 *          jmp factorial_tail (ou jmp au label du début).
 *          → 1 seul frame, la récursion devient une boucle.
 * ========================================================================== */

static long factorial_tail(int n, long accumulator)
{
    if (n <= 1)
        return accumulator;

    /* Tail position : rien après cet appel */
    return factorial_tail(n - 1, accumulator * n);
}

long factorial(int n)
{
    return factorial_tail(n, 1);
}

/* ==========================================================================
 * 2. Récursion NON terminale — factorielle classique
 *
 * Ici, le résultat de l'appel récursif est multiplié APRÈS le retour.
 * Ce n'est PAS un tail call. Le compilateur NE PEUT PAS optimiser.
 *
 *   -O0 et -O2 : call factorial_notail dans les deux cas.
 *   (GCC peut cependant transformer en itératif via d'autres passes,
 *    mais ce n'est pas de la TCO à proprement parler.)
 * ========================================================================== */

static long factorial_notail(int n)
{
    if (n <= 1)
        return 1;

    /* PAS en tail position : multiplication après le retour */
    return n * factorial_notail(n - 1);
}

/* ==========================================================================
 * 3. Tail call vers une autre fonction (mutual/indirect tail call)
 *
 * is_even et is_odd s'appellent mutuellement en position terminale.
 *
 *   -O0 : deux fonctions avec calls mutuels → stack overflow si n grand.
 *   -O2 : les calls sont remplacés par des jmp → pas de croissance pile.
 *          GCC peut même fusionner les deux en une seule boucle.
 * ========================================================================== */

/* Déclarations anticipées pour la récursion mutuelle */
static int is_odd(unsigned int n);

static int is_even(unsigned int n)
{
    if (n == 0) return 1;
    return is_odd(n - 1);    /* Tail call vers is_odd */
}

static int is_odd(unsigned int n)
{
    if (n == 0) return 0;
    return is_even(n - 1);   /* Tail call vers is_even */
}

/* ==========================================================================
 * 4. Tail call EMPÊCHÉ — travail après l'appel
 *
 * Ces fonctions ressemblent à des tail calls mais n'en sont pas,
 * parce qu'il y a du travail après l'appel récursif.
 * ========================================================================== */

/* Addition après l'appel → pas un tail call */
static int sum_recursive(int n)
{
    if (n <= 0) return 0;
    return n + sum_recursive(n - 1);  /* n + ... empêche la TCO */
}

/* Version tail-recursive avec accumulateur — pour comparaison */
static int sum_tail(int n, int acc)
{
    if (n <= 0) return acc;
    return sum_tail(n - 1, acc + n);  /* Tail call valide */
}

/* ==========================================================================
 * 5. Tail call empêché par la pile locale
 *
 * Si la fonction a un tableau local ou un objet qui nécessite un
 * cleanup (ex: variable-length array, alloca, ou en C++ un destructeur),
 * la TCO est bloquée car le frame doit rester actif pour le cleanup.
 * ========================================================================== */

static int process_with_buffer(int n, int threshold)
{
    /* Ce buffer local empêche la TCO car le frame doit rester
     * pour le désallouer (en théorie ; GCC peut parfois contourner). */
    int buffer[64];

    buffer[n % 64] = n;

    if (n <= 0)
        return buffer[0];

    if (n > threshold)
        return process_with_buffer(n - 2, threshold);  /* Tail position... */
    else
        return process_with_buffer(n - 1, threshold);  /* ...mais buffer bloque */
}

/* ==========================================================================
 * 6. Tail call via pointeur de fonction
 *
 * La TCO fonctionne aussi avec les appels indirects en position terminale,
 * bien que ce soit moins fréquemment optimisé.
 *
 *   -O2 : GCC peut émettre un jmp [rax] au lieu de call [rax] + ret.
 * ========================================================================== */

typedef long (*transform_fn)(long, int);

static long double_it(long val, int steps)
{
    if (steps <= 0) return val;
    return double_it(val * 2, steps - 1);
}

static long triple_it(long val, int steps)
{
    if (steps <= 0) return val;
    return triple_it(val * 3, steps - 1);
}

static long apply_transform(transform_fn fn, long initial, int steps)
{
    /* Tail call indirect — jmp [registre] en -O2 */
    return fn(initial, steps);
}

/* ==========================================================================
 * 7. Exemple de détection en RE : reconnaître une TCO dans le binaire
 *
 * Signature d'un tail call dans le désassemblage :
 *   - La fonction se termine par un jmp vers elle-même (ou une autre fn)
 *     au lieu d'un call + ret.
 *   - Pas de push rbp / sub rsp au point de retour.
 *   - La backtrace GDB ne montre qu'un seul frame même après N récursions.
 *
 * Piège RE : on peut confondre une TCO avec une boucle while écrite
 * dans le source. Seule l'analyse de la structure (paramètres réinitialisés
 * + jmp en arrière) permet de distinguer.
 * ========================================================================== */

/* GCD (algorithme d'Euclide) — naturellement tail recursive */
static int gcd(int a, int b)
{
    if (b == 0) return a;
    return gcd(b, a % b);   /* Tail call */
}

/* Exponentiation modulaire rapide — tail recursive avec accumulateur */
static long mod_pow_tail(long base, int exp, long mod, long acc)
{
    if (exp == 0) return acc;

    if (exp % 2 == 1)
        return mod_pow_tail(base, exp - 1, mod, (acc * base) % mod);
    else
        return mod_pow_tail((base * base) % mod, exp / 2, mod, acc);
}

static long mod_pow(long base, int exp, long mod)
{
    return mod_pow_tail(base % mod, exp, mod, 1);
}

/* ==========================================================================
 * Point d'entrée
 * ========================================================================== */

int main(int argc, char *argv[])
{
    int input = 12;
    if (argc > 1)
        input = atoi(argv[1]);

    /* 1. Tail recursive factorial */
    long fact_t = factorial(input);
    printf("factorial_tail(%d) = %ld\n", input, fact_t);

    /* 2. Non-tail recursive factorial */
    long fact_nt = factorial_notail(input);
    printf("factorial_notail(%d) = %ld\n", input, fact_nt);

    /* 3. Mutual tail calls */
    printf("is_even(%d) = %d\n", input, is_even((unsigned)input));
    printf("is_odd(%d)  = %d\n", input, is_odd((unsigned)input));

    /* 4. Non-tail vs tail sum */
    int s1 = sum_recursive(input);
    int s2 = sum_tail(input, 0);
    printf("sum_recursive(%d) = %d\n", input, s1);
    printf("sum_tail(%d)      = %d\n", input, s2);

    /* 5. Buffer empêche TCO */
    int pb = process_with_buffer(input, 5);
    printf("process_with_buffer(%d, 5) = %d\n", input, pb);

    /* 6. Tail call indirect */
    transform_fn fn = (input % 2 == 0) ? double_it : triple_it;
    long tr = apply_transform(fn, 1, input);
    printf("apply_transform(1, %d) = %ld\n", input, tr);

    /* 7. GCD et modular exponentiation */
    int g = gcd(input * 7, input * 3);
    long mp = mod_pow(input, 13, 1000000007L);
    printf("gcd(%d, %d) = %d\n", input * 7, input * 3, g);
    printf("mod_pow(%d, 13, 1e9+7) = %ld\n", input, mp);

    return 0;
}
