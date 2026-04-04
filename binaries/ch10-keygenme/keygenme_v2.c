/*
 * keygenme_v2.c — Version corrigée
 * Formation Reverse Engineering — Chapitre 10 (Diffing de binaires)
 *
 * Ce programme vérifie un serial saisi par l'utilisateur.
 * CORRECTION : check_serial() valide désormais la longueur de l'entrée
 * (entre 4 et 32 caractères inclus) avant de la transmettre à transform().
 *
 * La fonction transform() reste inchangée par rapport à v1 — seule la
 * validation en amont a été ajoutée. C'est un pattern de correction
 * fréquent : on protège l'appelant plutôt que de modifier le code
 * vulnérable lui-même.
 *
 * Compilez avec le Makefile fourni ou manuellement :
 *   gcc -O0 -g -o keygenme_v2 keygenme_v2.c
 *
 * Licence MIT — Usage strictement éducatif et éthique.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERIAL_MIN_LEN   4
#define SERIAL_MAX_LEN  32

/* ------------------------------------------------------------------ */
/*  transform() — transforme un serial en une valeur numérique         */
/*                                                                     */
/*  INCHANGÉE par rapport à v1. Le buffer interne de 64 octets est     */
/*  toujours présent, mais il est désormais protégé par la validation   */
/*  de longueur dans check_serial().                                   */
/* ------------------------------------------------------------------ */
int transform(const char *input)
{
    char buf[64];
    int  acc = 0;
    int  i;

    /*
     * Copie l'entrée dans un buffer local.
     * Toujours strcpy (non sécurisé), mais la longueur est désormais
     * garantie <= 32 par check_serial(), donc pas de débordement.
     */
    strcpy(buf, input);

    /* Algorithme de hachage simple (à des fins pédagogiques) */
    for (i = 0; buf[i] != '\0'; i++) {
        acc = (acc * 31 + (unsigned char)buf[i]) & 0xFFFF;
    }

    /* Mélange supplémentaire basé sur la longueur */
    acc ^= (i * 0x1337) & 0xFFFF;

    return acc;
}

/* ------------------------------------------------------------------ */
/*  check_serial() — vérifie si le serial est valide                   */
/*                                                                     */
/*  VERSION v2 (corrigée) : validation de longueur ajoutée.            */
/*  L'entrée doit faire entre SERIAL_MIN_LEN et SERIAL_MAX_LEN        */
/*  caractères pour être acceptée.                                     */
/* ------------------------------------------------------------------ */
int check_serial(const char *input)
{
    size_t len;
    int    transformed;

    /* CORRECTION : vérification de la longueur avant tout traitement */
    len = strlen(input);

    if (len < SERIAL_MIN_LEN || len > SERIAL_MAX_LEN) {
        puts("Access denied.");
        return 0;
    }

    transformed = transform(input);

    if (transformed == 0x5A42) {
        puts("Access granted!");
        return 1;
    }

    puts("Access denied.");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  usage() — affiche l'aide                                           */
/* ------------------------------------------------------------------ */
void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <serial>\n", progname);
    fprintf(stderr, "  Checks whether the provided serial is valid.\n");
}

/* ------------------------------------------------------------------ */
/*  main()                                                             */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    return check_serial(argv[1]) ? EXIT_SUCCESS : EXIT_FAILURE;
}
