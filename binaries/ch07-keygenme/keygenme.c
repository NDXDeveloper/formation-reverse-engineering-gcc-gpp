/*
 * keygenme.c — Binary d'entraînement pour la formation Reverse Engineering
 *
 * Ce programme implémente un système de vérification de serial simple :
 *   1. compute_hash()  — calcule un hash numérique à partir du nom d'utilisateur
 *   2. check_serial()  — compare le serial fourni au hash attendu
 *   3. main()          — orchestre l'ensemble et affiche le résultat
 *
 * Compilé à différents niveaux d'optimisation, ce binaire sert de support
 * pour comparer le code assembleur produit par GCC (-O0, -O2, -O3).
 *
 * Usage : ./keygenme <username> <serial>
 *
 * Licence MIT — usage strictement éducatif.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * compute_hash — Calcule un hash 32 bits à partir d'une chaîne.
 *
 * Algorithme volontairement simple pour être lisible en assembleur :
 *   - Parcourt chaque caractère de la chaîne
 *   - Accumule la valeur ASCII dans un accumulateur
 *   - Applique un décalage et un XOR à chaque itération
 *
 * En -O0, chaque étape est visible comme une instruction distincte.
 * En -O2, GCC optimise les accès mémoire et restructure la boucle.
 * En -O3, la boucle peut être déroulée ou vectorisée.
 */
unsigned int compute_hash(const char *input)
{
    unsigned int hash = 0x5381;
    int i;

    for (i = 0; input[i] != '\0'; i++) {
        hash = (hash << 5) + hash;      /* hash * 33 */
        hash = hash ^ (unsigned char)input[i];
        hash = hash + (unsigned int)i;
    }

    return hash;
}

/*
 * check_serial — Vérifie si le serial correspond au username.
 *
 * Convertit le hash en chaîne hexadécimale via sprintf, puis compare
 * avec le serial fourni par l'utilisateur via strcmp.
 *
 * Points d'intérêt pour le RE :
 *   - L'appel à sprintf@plt est un point d'ancrage sémantique fort.
 *   - L'appel à strcmp@plt révèle immédiatement le mécanisme de validation.
 *   - Le buffer local (64 octets) est visible dans le prologue via sub rsp.
 *   - La valeur de retour (0 ou 1) crée deux chemins de sortie distincts.
 */
int check_serial(const char *username, const char *serial)
{
    unsigned int hash;
    char expected[64];

    hash = compute_hash(username);
    sprintf(expected, "%08x", hash);

    if (strcmp(expected, serial) == 0) {
        return 1;
    } else {
        return 0;
    }
}

/*
 * main — Point d'entrée du programme.
 *
 * Points d'intérêt pour le RE :
 *   - La vérification argc == 3 crée un branchement visible (cmp edi, 3 / jne).
 *   - Les chaînes "Usage:", "Serial valide !", "Serial invalide."
 *     sont stockées dans .rodata et référencées via lea rdi, [rip+...].
 *   - Les deux chemins de retour (return 0 / return 1) créent
 *     les patterns xor eax,eax et mov eax,1 avant le ret.
 */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("Usage: %s <username> <serial>\n", argv[0]);
        printf("Exemple: %s admin 0000abcd\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];
    const char *serial   = argv[2];

    if (check_serial(username, serial)) {
        puts("Serial valide !");
        return 0;
    } else {
        puts("Serial invalide.");
        return 1;
    }
}
