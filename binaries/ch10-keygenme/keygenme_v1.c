/*
 * keygenme_v1.c — Version vulnérable
 * Formation Reverse Engineering — Chapitre 10 (Diffing de binaires)
 *
 * Ce programme vérifie un serial saisi par l'utilisateur.
 * VULNÉRABILITÉ : check_serial() ne valide pas la longueur de l'entrée
 * avant de la transmettre à transform(), qui utilise un buffer de taille
 * fixe. Une entrée trop longue provoque un buffer overflow ; une entrée
 * trop courte provoque un accès hors limites en lecture.
 *
 * Compilez avec le Makefile fourni ou manuellement :
 *   gcc -O0 -g -o keygenme_v1 keygenme_v1.c
 *
 * Licence MIT — Usage strictement éducatif et éthique.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  transform() — transforme un serial en une valeur numérique         */
/*                                                                     */
/*  ATTENTION : utilise un buffer interne de 64 octets. Si input       */
/*  dépasse cette taille, il y a dépassement de buffer (vulnérable     */
/*  dans v1, corrigé dans v2 par une vérification dans check_serial).  */
/* ------------------------------------------------------------------ */
int transform(const char *input)
{
    char buf[64];
    int  acc = 0;
    int  i;

    /*
     * Copie l'entrée dans un buffer local SANS vérification de taille.
     * C'est ici que le overflow se produit si input > 64 octets.
     * Note : on utilise strcpy volontairement pour illustrer la vuln.
     */
    strcpy(buf, input);  /* CWE-120 : Buffer Copy without Checking Size */

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
/*  VERSION v1 (vulnérable) : pas de vérification de longueur.         */
/*  L'entrée est transmise directement à transform().                  */
/* ------------------------------------------------------------------ */
int check_serial(const char *input)
{
    int transformed;

    /* PAS de vérification de longueur — c'est la vulnérabilité */

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
