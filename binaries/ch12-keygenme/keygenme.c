/*
 * keygenme.c — Binaire d'entraînement pour la formation Reverse Engineering
 *
 * Crackme pédagogique simple : le programme demande un mot de passe
 * et le compare à une valeur attendue via strcmp.
 *
 * Compilé à différents niveaux d'optimisation et avec/sans symboles
 * pour servir de support au chapitre 12.
 *
 * Licence MIT — Usage strictement éducatif.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ───────────────────────────────────────────────────────────
 * Mot de passe attendu — stocké en .rodata comme constante.
 * En RE, on le retrouve via `strings`, Ghidra, ou en
 * interceptant strcmp avec GDB/GEF/pwndbg/Frida.
 * ─────────────────────────────────────────────────────────── */
static const char *EXPECTED_PASSWORD = "s3cr3t_k3y";

/* ───────────────────────────────────────────────────────────
 * strip_newline — retire le '\n' final laissé par fgets.
 *
 * Pattern classique : strlen + remplacement. Ce traitement
 * est visible en RE via l'import de strlen dans la GOT et
 * le write d'un '\0' dans le buffer avant l'appel à strcmp.
 * ─────────────────────────────────────────────────────────── */
static void strip_newline(char *str)
{
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = '\0';
}

/* ───────────────────────────────────────────────────────────
 * check_password — routine de vérification.
 *
 * En RE, cette fonction est identifiable par :
 *   - son nom (si le binaire n'est pas strippé)
 *   - la cross-reference vers strcmp@plt
 *   - le pattern test eax, eax → jne/je après le call strcmp
 *
 * Retourne 1 si le mot de passe est correct, 0 sinon.
 * ─────────────────────────────────────────────────────────── */
static int check_password(const char *input)
{
    if (strcmp(input, EXPECTED_PASSWORD) == 0) {
        puts("Mot de passe correct !");
        return 1;
    } else {
        puts("Mot de passe incorrect.");
        return 0;
    }
}

/* ───────────────────────────────────────────────────────────
 * main — point d'entrée.
 *
 * Flux linéaire : affichage du prompt → lecture via fgets →
 * nettoyage du '\n' → appel à check_password → code de retour.
 *
 * Le buffer de 64 octets est volontairement surdimensionné
 * par rapport au mot de passe attendu. Dans une variante
 * d'exploitation (hors scope de ce crackme), on pourrait
 * étudier un débordement — ici le but est uniquement le RE
 * de la logique de vérification.
 * ─────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    char buffer[64];

    printf("=== KeyGenMe v1.0 ===\n");
    printf("Entrez le mot de passe : ");

    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        fprintf(stderr, "Erreur de lecture.\n");
        return EXIT_FAILURE;
    }

    strip_newline(buffer);

    if (check_password(buffer))
        return EXIT_SUCCESS;
    else
        return EXIT_FAILURE;
}
