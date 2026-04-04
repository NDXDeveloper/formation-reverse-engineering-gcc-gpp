/*
 * keygenme.c — Crackme d'entraînement pour la formation Reverse Engineering
 *
 * Chapitre concerné :
 *   - Chapitre 18 : Résolution par exécution symbolique (angr / Z3)
 *
 * Compilation : voir Makefile (produit plusieurs variantes O0/O2/O3/strip)
 *
 * Usage : ./keygenme <serial>
 *         Le serial est une chaîne de 16 caractères hexadécimaux (64 bits).
 *         Exemple : ./keygenme 4A5F...
 *
 * Licence MIT — Usage strictement éducatif.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Constantes utilisées dans la routine de vérification.
 * En RE, ces valeurs apparaissent comme des immédiats dans le désassemblage
 * et constituent un point d'accroche pour l'analyste.
 * -------------------------------------------------------------------------- */

#define SERIAL_LEN      16    /* 16 caractères hex = 8 octets = 64 bits */

#define MAGIC_A         0x5A3CE7F1U
#define MAGIC_B         0x1F4B8C2DU
#define MAGIC_C         0xDEAD1337U
#define MAGIC_D         0x8BADF00DU

#define EXPECTED_HIGH   0xA11C3514U
#define EXPECTED_LOW    0xF00DCAFEU

/* --------------------------------------------------------------------------
 * Fonctions de transformation — volontairement non triviales pour encourager
 * l'usage de l'exécution symbolique plutôt que la résolution manuelle.
 * -------------------------------------------------------------------------- */

/*
 * Mélange de bits inspiré des fonctions de hachage.
 * Objectif pédagogique : montrer que quelques lignes de C produisent
 * un assembleur dense et difficile à inverser mentalement.
 */
static uint32_t mix32(uint32_t v, uint32_t seed)
{
    v ^= seed;
    v  = ((v >> 16) ^ v) * 0x45D9F3BU;
    v  = ((v >> 16) ^ v) * 0x45D9F3BU;
    v  = (v >> 16) ^ v;
    return v;
}

/*
 * Feistel-like round : applique 4 tours sur deux moitiés 32 bits.
 * Le réseau de Feistel est un classique de la crypto ; ici on l'utilise
 * à des fins pédagogiques pour créer une dépendance croisée entre
 * la partie haute et la partie basse du serial.
 */
static void feistel4(uint32_t *left, uint32_t *right)
{
    uint32_t l = *left;
    uint32_t r = *right;
    uint32_t tmp;

    /* Tour 1 */
    tmp = r;
    r   = l ^ mix32(r, MAGIC_A);
    l   = tmp;

    /* Tour 2 */
    tmp = r;
    r   = l ^ mix32(r, MAGIC_B);
    l   = tmp;

    /* Tour 3 */
    tmp = r;
    r   = l ^ mix32(r, MAGIC_C);
    l   = tmp;

    /* Tour 4 */
    tmp = r;
    r   = l ^ mix32(r, MAGIC_D);
    l   = tmp;

    *left  = l;
    *right = r;
}

/* --------------------------------------------------------------------------
 * Routine de vérification principale.
 * Retourne 1 si le serial est valide, 0 sinon.
 *
 * En RE, c'est cette fonction qu'il faut localiser et comprendre.
 * Avec angr, on peut la résoudre sans même la lire.
 * -------------------------------------------------------------------------- */
static int check_serial(const char *serial)
{
    uint32_t high, low;
    char buf[9];

    if (strlen(serial) != SERIAL_LEN)
        return 0;

    /* Vérifier que tous les caractères sont hexadécimaux valides */
    for (int i = 0; i < SERIAL_LEN; i++) {
        char c = serial[i];
        int valid = (c >= '0' && c <= '9') ||
                    (c >= 'A' && c <= 'F') ||
                    (c >= 'a' && c <= 'f');
        if (!valid)
            return 0;
    }

    /* Découper le serial en deux moitiés de 32 bits */
    memcpy(buf, serial, 8);
    buf[8] = '\0';
    high = (uint32_t)strtoul(buf, NULL, 16);

    memcpy(buf, serial + 8, 8);
    buf[8] = '\0';
    low = (uint32_t)strtoul(buf, NULL, 16);

    /* Appliquer le réseau de Feistel */
    feistel4(&high, &low);

    /* Vérification finale */
    if (high == EXPECTED_HIGH && low == EXPECTED_LOW)
        return 1;

    return 0;
}

/* --------------------------------------------------------------------------
 * Point d'entrée — messages distincts pour faciliter le ciblage avec angr
 * (on cherche l'adresse de "Access Granted" et on évite "Access Denied").
 * -------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: %s <serial>\n", argv[0]);
        printf("  Le serial est une chaîne de %d caractères hexadécimaux.\n",
               SERIAL_LEN);
        return 1;
    }

    if (check_serial(argv[1])) {
        puts("Access Granted!");
        return 0;
    } else {
        puts("Access Denied.");
        return 1;
    }
}
