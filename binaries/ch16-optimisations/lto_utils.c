/* ============================================================================
 * Chapitre 16 — Section 16.5
 * lto_utils.c — Fonctions utilitaires pour la démo LTO
 *
 * Fonctions simples d'affichage et de manipulation de tableaux.
 * Avec LTO, les fonctions triviales (utils_clamp, utils_array_max)
 * seront inlinées dans lto_main.c même si elles sont dans un fichier
 * séparé. Sans LTO, elles restent comme des calls.
 *
 * Licence MIT — Usage strictement éducatif.
 * ============================================================================ */

#include <stdio.h>
#include <string.h>
#include "lto_utils.h"

/* --------------------------------------------------------------------------
 * Affichage d'un tableau — ne sera PAS inlinée (I/O, trop volumineuse).
 * Sert à empêcher l'élimination de code mort.
 * -------------------------------------------------------------------------- */
void utils_print_array(const char *label, const int *data, int n)
{
    printf("[%s] ", label);
    int display = (n > 10) ? 10 : n;
    for (int i = 0; i < display; i++) {
        printf("%d ", data[i]);
    }
    if (n > 10) printf("... (%d total)", n);
    printf("\n");
}

/* --------------------------------------------------------------------------
 * Remplissage séquentiel — triviale, inlinée cross-module avec LTO.
 * -------------------------------------------------------------------------- */
void utils_fill_sequence(int *data, int n, int start, int step)
{
    for (int i = 0; i < n; i++) {
        data[i] = start + i * step;
    }
}

/* --------------------------------------------------------------------------
 * Clamp — très triviale (3 comparaisons).
 * Sera inlinée partout avec LTO. Sans LTO : call utils_clamp via PLT/GOT
 * ou call direct selon le linkage.
 *
 * En -O2, même sans LTO, GCC peut utiliser cmov pour cette fonction
 * (dans son propre fichier). Avec LTO, le cmov apparaît directement
 * dans main().
 * -------------------------------------------------------------------------- */
int utils_clamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/* --------------------------------------------------------------------------
 * Maximum d'un tableau — boucle simple, inlinable avec LTO.
 *
 * Intérêt RE : sans LTO, on voit un call utils_array_max + sa boucle.
 * Avec LTO, la boucle est fusionnée dans l'appelant, potentiellement
 * combinée avec d'autres boucles (loop fusion).
 * -------------------------------------------------------------------------- */
int utils_array_max(const int *data, int n)
{
    int max = data[0];
    for (int i = 1; i < n; i++) {
        if (data[i] > max)
            max = data[i];
    }
    return max;
}

/* --------------------------------------------------------------------------
 * Conversion int → hex dans un buffer.
 * Taille moyenne, inlinée avec LTO seulement si un seul site d'appel.
 *
 * Contient une boucle à itérations fixes (8 nibbles pour un int 32 bits)
 * que GCC peut dérouler complètement.
 * -------------------------------------------------------------------------- */
char *utils_int_to_hex(int value, char *buf, int bufsize)
{
    static const char hex_chars[] = "0123456789ABCDEF";

    if (bufsize < 11) {  /* "0x" + 8 hex digits + '\0' */
        buf[0] = '\0';
        return buf;
    }

    buf[0] = '0';
    buf[1] = 'x';

    unsigned int uval = (unsigned int)value;
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex_chars[(uval >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';

    return buf;
}
