/*
 * Formation Reverse Engineering — Chapitre 3 — Checkpoint
 *
 * count_lowercase.c
 *
 * Programme simple servant de support au checkpoint du chapitre 3.
 * L'objectif est de désassembler la fonction count_lowercase() compilée
 * en -O0 et de l'annoter manuellement en appliquant la méthode en 5 étapes.
 *
 * Compilation : voir le Makefile fourni.
 *   make all       → produit les variantes -O0, -O2, -O0 strippé
 *   make clean     → supprime les binaires générés
 *
 * Usage :
 *   ./count_lowercase_O0 <chaîne>
 *   Affiche le nombre de lettres minuscules ('a'–'z') dans la chaîne.
 *
 * Exemple :
 *   $ ./count_lowercase_O0 "Hello World 123"
 *   Lowercase count: 7
 *
 * Licence MIT — Usage strictement éducatif.
 */

#include <stdio.h>
#include <string.h>

/*
 * count_lowercase — Compte les lettres minuscules ASCII dans un buffer.
 *
 * @param str  Pointeur vers le buffer de caractères à analyser.
 * @param len  Nombre de caractères à examiner dans le buffer.
 * @return     Le nombre de caractères dans l'intervalle 'a'–'z'.
 *
 * C'est cette fonction que l'étudiant doit retrouver par reverse engineering
 * à partir du désassemblage fourni dans le checkpoint.
 */
int count_lowercase(const char *str, int len) {
    int count = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            count++;
        }
    }
    return count;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <string>\n", argv[0]);
        return 1;
    }

    const char *input = argv[1];
    int length = (int)strlen(input);
    int result = count_lowercase(input, length);

    printf("Lowercase count: %d\n", result);
    return 0;
}
