/*
 * keygenme.c — Binaire d'entraînement pour la formation RE
 *
 * Crackme simple avec vérification de clé par strcmp.
 * Utilisé dès le chapitre 11 (GDB) .
 *
 * Compilation : voir le Makefile associé.
 *
 * Usage :
 *   ./keygenme_O0
 *   Enter your key: VALID-KEY-2025
 *   Correct!
 *
 * Licence MIT — usage strictement éducatif.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ──────────────────────────────────────────────
 * Constantes et données globales
 * ────────────────────────────────────────────── */

/* Clé attendue, stockée en clair dans .rodata.
 * En RE, elle sera visible avec `strings` ou dans Ghidra.
 * Les chapitres ultérieurs montrent des variantes plus résistantes. */
static const char EXPECTED_KEY[] = "VALID-KEY-2025";

/* Variable globale observable avec un watchpoint (section 11.5).
 * 0 = verrouillé, 1 = déverrouillé. */
int access_granted = 0;

/* ──────────────────────────────────────────────
 * Fonctions
 * ────────────────────────────────────────────── */

/*
 * transform_input — Nettoie l'entrée utilisateur.
 *
 * Retire le retour à la ligne final laissé par fgets.
 * Fonction simple, utile pour observer le prologue/épilogue
 * et la convention d'appel (rdi = pointeur vers le buffer).
 */
void transform_input(char *input)
{
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
}

/*
 * check_key — Vérifie si la clé fournie est correcte.
 *
 * C'est LA fonction cible du reverse engineer :
 *   - Son premier argument (rdi) pointe vers l'entrée utilisateur.
 *   - Elle appelle strcmp, dont les arguments (rdi, rsi) révèlent
 *     la clé attendue quand on pose un breakpoint.
 *   - Elle retourne 1 (succès) ou 0 (échec) dans rax.
 *
 * En -O0, la structure est lisible : prologue, appel à strcmp,
 * test du retour, saut conditionnel, épilogue.
 * En -O2, le compilateur peut inliner strcmp ou réordonner le code.
 */
int check_key(const char *input)
{
    if (strcmp(input, EXPECTED_KEY) == 0) {
        return 1;
    }
    return 0;
}

/*
 * main — Point d'entrée du programme.
 *
 * Repères pour GDB (chapitre 11) :
 *   - Localisable via __libc_start_main (section 11.4).
 *   - Le buffer input[64] est sur la pile (section 11.3).
 *   - fgets lit depuis stdin → rediriger avec run < input.txt
 *     ou automatiser avec pwntools (section 11.9).
 *   - Le saut conditionnel après check_key (jz/jnz) est la cible
 *     du patching binaire .
 */
int main(int argc, char *argv[])
{
    char input[64];

    printf("Enter your key: ");
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) {
        fprintf(stderr, "Error reading input.\n");
        return 1;
    }

    transform_input(input);

    if (check_key(input)) {
        access_granted = 1;
        printf("Correct!\n");
        return 0;
    } else {
        printf("Wrong key!\n");
        return 1;
    }
}
