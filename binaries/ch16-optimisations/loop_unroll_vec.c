/* ============================================================================
 * Chapitre 16 — Section 16.3
 * loop_unroll_vec.c — Déroulage de boucles et vectorisation (SIMD/SSE/AVX)
 *
 * Ce fichier contient des boucles conçues pour mettre en évidence :
 *
 *   1. Le déroulage simple (loop unrolling) — la boucle est répliquée N fois
 *   2. La vectorisation automatique (auto-vectorization) — les itérations
 *      sont regroupées pour utiliser des registres SIMD (xmm/ymm)
 *   3. Les boucles que GCC ne peut PAS vectoriser (dépendances, aliasing)
 *   4. Le peeling et le code de gestion du « reste » (tail/epilogue)
 *
 * Points d'observation :
 *
 *   -O0 : une boucle = un cmp + jle + corps + jmp. Littéral.
 *   -O1 : variables dans les registres, pas de déroulage.
 *   -O2 : déroulage partiel (typiquement 2x ou 4x), vectorisation basique.
 *   -O3 : déroulage agressif + vectorisation SSE/AVX, boucle de
 *          « prologue » pour l'alignement, boucle principale vectorisée,
 *          boucle « épilogue » pour les éléments restants.
 *   -Os : PAS de déroulage (augmente la taille), vectorisation minimale.
 *
 * Pour forcer AVX2 (registres ymm 256 bits) :
 *   gcc -O3 -mavx2 -g -o loop_unroll_vec_O3 loop_unroll_vec.c
 *
 * Analyse recommandée :
 *   objdump -d -M intel build/loop_unroll_vec_O3 | less
 *   → Chercher les instructions : movdqa, paddd, pmulld (SSE)
 *      ou vpaddd, vpmulld, vmovdqu (AVX)
 *
 * Licence MIT — Usage strictement éducatif.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE 1024

/* Empêcher GCC d'optimiser les résultats inutilisés */
static void consume(const void *ptr, size_t size)
{
    /* Force le compilateur à considérer la mémoire comme lue.
     * En -O2+, cette fonction est inlinée mais la barrière reste. */
    __asm__ volatile("" : : "r"(ptr), "r"(size) : "memory");
}

/* ==========================================================================
 * 1. Boucle vectorisable — addition élément par élément
 *
 * Pattern idéal pour la vectorisation : pas de dépendance entre itérations,
 * opération uniforme sur des données contiguës.
 *
 *   -O0 : boucle scalaire, add un int à la fois.
 *   -O2 : peut vectoriser avec movdqu + paddd (4 int en SSE).
 *   -O3 -mavx2 : vpaddd sur ymm (8 int simultanément).
 *
 * Dans le désassemblage, cherchez :
 *   - movdqu xmm0, [rdi+rax*4]   ← charge 4 entiers (128 bits)
 *   - paddd  xmm0, xmm1           ← addition parallèle de 4 entiers
 *   - vmovdqu ymm0, [rdi+rax*4]  ← charge 8 entiers (256 bits, AVX)
 * ========================================================================== */

static void vec_add(int *dst, const int *a, const int *b, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = a[i] + b[i];
    }
}

/* ==========================================================================
 * 2. Boucle vectorisable — multiplication + accumulation (dot product)
 *
 * Réduction (un accumulateur unique) : GCC doit prouver que la réduction
 * est associative et commutative (vrai pour les entiers, pas toujours
 * pour les flottants sans -ffast-math).
 *
 *   -O2 : peut vectoriser avec accumulateur vectoriel, puis réduction
 *          horizontale à la fin (phaddd ou séquence de shuffles).
 *   -O3 : utilise plusieurs accumulateurs pour masquer la latence.
 * ========================================================================== */

static long dot_product(const int *a, const int *b, int n)
{
    long sum = 0;
    for (int i = 0; i < n; i++) {
        sum += (long)a[i] * (long)b[i];
    }
    return sum;
}

/* ==========================================================================
 * 3. Boucle avec déroulage visible — compteur connu à la compilation
 *
 * Quand le nombre d'itérations est connu et petit, GCC peut dérouler
 * complètement la boucle (pas de cmp/jmp du tout).
 *
 *   -O0 : boucle classique, 16 itérations.
 *   -O2 : déroulage partiel (ex: 4 itérations par tour).
 *   -O3 : déroulage complet possible (16 instructions séquentielles).
 * ========================================================================== */

static void fixed_size_init(int arr[16])
{
    for (int i = 0; i < 16; i++) {
        arr[i] = i * i + 1;
    }
}

/* ==========================================================================
 * 4. Boucle NON vectorisable — dépendance inter-itérations
 *
 * Chaque itération dépend du résultat de la précédente. Impossible de
 * paralléliser. GCC peut toujours dérouler, mais pas vectoriser.
 *
 * Pour le RE : si vous ne voyez PAS d'instructions SIMD dans une boucle
 * O3, c'est probablement à cause d'une dépendance.
 * ========================================================================== */

static void dependent_loop(int *data, int n)
{
    for (int i = 1; i < n; i++) {
        data[i] = data[i - 1] * 3 + data[i];
    }
}

/* ==========================================================================
 * 5. Boucle avec aliasing potentiel — restrict nécessaire
 *
 * Sans le mot-clé restrict, GCC ne peut pas prouver que dst et src
 * ne se chevauchent pas en mémoire. Il doit donc rester conservateur.
 *
 * vec_add_alias  : sans restrict → vectorisation possible mais avec tests.
 * vec_add_noalias: avec restrict → vectorisation directe.
 *
 * Comparez le désassemblage des deux en -O2.
 * ========================================================================== */

static void vec_add_alias(int *dst, const int *src, int n)
{
    /* GCC ne sait pas si dst et src se chevauchent.
     * Il peut générer deux versions : une vectorisée et une scalaire,
     * avec un test d'aliasing au runtime. */
    for (int i = 0; i < n; i++) {
        dst[i] = dst[i] + src[i];
    }
}

static void vec_add_noalias(int * restrict dst, const int * restrict src, int n)
{
    /* Avec restrict, GCC a la garantie qu'il n'y a pas d'aliasing.
     * Vectorisation directe, sans test runtime. */
    for (int i = 0; i < n; i++) {
        dst[i] = dst[i] + src[i];
    }
}

/* ==========================================================================
 * 6. Boucle sur des flottants — -ffast-math change tout
 *
 * L'addition flottante N'EST PAS associative (arrondis IEEE 754).
 * Sans -ffast-math, GCC ne vectorise pas les réductions flottantes.
 * On peut le vérifier en comparant -O3 et -O3 -ffast-math.
 *
 * Note : ce fichier est compilé sans -ffast-math par défaut.
 * Pour tester : gcc -O3 -ffast-math -g -o test_fastmath loop_unroll_vec.c
 * ========================================================================== */

static float float_sum(const float *data, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += data[i];
    }
    return sum;
}

/* ==========================================================================
 * 7. Boucle memset/memcpy — reconnaissance par le compilateur
 *
 * GCC reconnaît certains patterns de boucle comme des memset ou memcpy
 * déguisés et les remplace par des appels à la libc optimisée (ou des
 * instructions rep stosb / rep movsb).
 *
 *   -O0 : boucle explicite.
 *   -O2 : remplacé par un call à memset/memcpy.
 *
 * Pour le RE : si vous voyez un memset là où le source n'en a pas,
 * c'est cette optimisation.
 * ========================================================================== */

static void zero_fill(int *arr, int n)
{
    for (int i = 0; i < n; i++) {
        arr[i] = 0;
    }
}

static void copy_array(int *dst, const int *src, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/* ==========================================================================
 * 8. Strength reduction — transformation de boucle
 *
 * GCC transforme les multiplications dépendant de l'index de boucle
 * en additions successives (strength reduction).
 *   Ex : data[i * stride] → on incrémente un pointeur de stride à chaque tour.
 *
 *   -O0 : imul explicite à chaque itération.
 *   -O2 : le imul disparaît, remplacé par un add sur le pointeur.
 * ========================================================================== */

static void strided_write(int *data, int n, int stride, int value)
{
    for (int i = 0; i < n; i++) {
        data[i * stride] = value + i;
    }
}

/* ==========================================================================
 * Point d'entrée
 * ========================================================================== */

int main(int argc, char *argv[])
{
    int n = ARRAY_SIZE;
    if (argc > 1)
        n = atoi(argv[1]);
    if (n <= 0 || n > ARRAY_SIZE)
        n = ARRAY_SIZE;

    /* Allocation des tableaux sur la pile */
    int a[ARRAY_SIZE], b[ARRAY_SIZE], dst[ARRAY_SIZE];
    float fdata[ARRAY_SIZE];

    /* Initialisation avec des valeurs déterministes */
    for (int i = 0; i < ARRAY_SIZE; i++) {
        a[i] = i + 1;
        b[i] = (i * 7 + 3) % 100;
        fdata[i] = (float)(i * 0.1);
    }

    /* 1. Addition vectorisable */
    vec_add(dst, a, b, n);
    consume(dst, sizeof(dst));

    /* 2. Dot product (réduction) */
    long dp = dot_product(a, b, n);
    printf("Dot product: %ld\n", dp);

    /* 3. Taille fixe — déroulage complet */
    int fixed[16];
    fixed_size_init(fixed);
    consume(fixed, sizeof(fixed));

    /* 4. Dépendance inter-itérations — non vectorisable */
    memcpy(dst, a, (size_t)n * sizeof(int));
    dependent_loop(dst, n);
    consume(dst, sizeof(dst));

    /* 5. Aliasing : avec et sans restrict */
    memcpy(dst, a, (size_t)n * sizeof(int));
    vec_add_alias(dst, b, n);

    memcpy(dst, a, (size_t)n * sizeof(int));
    vec_add_noalias(dst, b, n);
    consume(dst, sizeof(dst));

    /* 6. Flottants — vectorisation conditionnelle */
    float fs = float_sum(fdata, n);
    printf("Float sum: %f\n", fs);

    /* 7. Reconnaissance memset/memcpy */
    zero_fill(dst, n);
    copy_array(dst, a, n);
    consume(dst, sizeof(dst));

    /* 8. Strength reduction */
    memset(dst, 0, sizeof(dst));
    strided_write(dst, n / 4, 4, 42);
    consume(dst, sizeof(dst));

    /* Utiliser fixed pour empêcher l'élimination */
    int sum = 0;
    for (int i = 0; i < 16; i++) sum += fixed[i];
    printf("Fixed sum: %d, dst[0]=%d\n", sum, dst[0]);

    return 0;
}
