/* hello.c — Fil conducteur du Chapitre 2
 *
 * Programme volontairement simple mais suffisamment riche pour illustrer
 * chaque étape de la chaîne de compilation GNU :
 *
 *   - Le préprocesseur remplace la macro SECRET (section 2.1, 2.2)
 *   - Le compilateur transforme check() en instructions machine (section 2.1)
 *   - Le linker résout strcmp/printf depuis la libc via PLT/GOT (section 2.9)
 *   - Le loader met tout en mémoire au moment de l'exécution (section 2.7)
 *
 * Compilé à différents niveaux d'optimisation et avec/sans symboles
 * pour observer l'impact des flags de compilation (section 2.5).
 *
 * Licence MIT — Ce contenu est strictement éducatif et éthique.
 */

#include <stdio.h>
#include <string.h>

#define SECRET "RE-101"

int check(const char *input) {
    return strcmp(input, SECRET) == 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <mot de passe>\n", argv[0]);
        return 1;
    }
    if (check(argv[1])) {
        printf("Accès autorisé.\n");
    } else {
        printf("Accès refusé.\n");
    }
    return 0;
}
