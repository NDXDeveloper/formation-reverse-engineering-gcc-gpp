/*
 * vuln_demo.c — Binaire d'entraînement Chapitre 19
 * Formation Reverse Engineering — Chaîne GNU
 *
 * Ce programme contient volontairement un buffer overflow
 * pour observer l'effet des protections compilateur :
 *
 *   - Stack canary (-fstack-protector / -fstack-protector-all)
 *   - NX (pile non exécutable)
 *   - PIE (Position Independent Executable)
 *   - RELRO (Partial vs Full)
 *
 * Compilé avec différentes combinaisons de flags via le Makefile,
 * il permet de constater visuellement (avec checksec, GDB, readelf)
 * l'impact de chaque protection.
 *
 * ⚠️ Ce binaire est VOLONTAIREMENT vulnérable.
 *     Usage strictement éducatif — Licence MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════
 * Table de fonctions via pointeurs (GOT-relevant)
 *
 * Ces pointeurs de fonctions stockés en .data sont
 * intéressants pour démontrer l'impact de RELRO :
 * - Partial RELRO : GOT writable après le démarrage
 * - Full RELRO : GOT en lecture seule après résolution
 * ═══════════════════════════════════════════ */

typedef void (*action_fn)(const char *);

static void greet(const char *name)
{
    printf("Bonjour, %s !\n", name);
}

static void farewell(const char *name)
{
    printf("Au revoir, %s !\n", name);
}

/* Table de dispatch — stockée dans .data */
static action_fn actions[] = { greet, farewell };

/* ═══════════════════════════════════════════
 * Fonction vulnérable — buffer overflow
 *
 * Le buffer local fait 64 octets mais strcpy ne
 * vérifie pas la taille. Avec un stack canary activé,
 * le programme détectera l'overflow et appellera
 * __stack_chk_fail. Sans canary, l'overflow écrasera
 * silencieusement l'adresse de retour.
 * ═══════════════════════════════════════════ */
static void process_input(const char *data)
{
    char buffer[64];

    /* ⚠️ VULNÉRABILITÉ INTENTIONNELLE
     * strcpy ne vérifie pas la taille de data.
     * Si strlen(data) > 63, overflow de buffer. */
    strcpy(buffer, data);

    printf("Traitement : %s\n", buffer);

    /* Utilisation de la table de dispatch pour
     * démontrer les appels via pointeurs de fonction */
    if (strlen(buffer) > 0) {
        actions[0](buffer);
    }
}

/* ═══════════════════════════════════════════
 * Fonction avec variable sur la pile
 *
 * Permet d'observer la disposition mémoire de la pile
 * et la position du canary avec GDB/GEF.
 * ═══════════════════════════════════════════ */
static int authenticate(void)
{
    char username[32];
    char password[32];
    int auth_flag = 0;

    printf("Nom d'utilisateur : ");
    fflush(stdout);

    if (!fgets(username, sizeof(username), stdin))
        return 0;

    printf("Mot de passe : ");
    fflush(stdout);

    if (!fgets(password, sizeof(password), stdin))
        return 0;

    /* Retirer les newlines */
    username[strcspn(username, "\n")] = '\0';
    password[strcspn(password, "\n")] = '\0';

    /* Vérification simpliste.
     * Le point intéressant est la disposition de auth_flag
     * par rapport aux buffers sur la pile. */
    if (strcmp(username, "admin") == 0 &&
        strcmp(password, "s3cur3") == 0) {
        auth_flag = 1;
    }

    return auth_flag;
}

/* Forward declaration nécessaire pour print_address_info() */
int main(int argc, char *argv[]);

/* ═══════════════════════════════════════════
 * Affichage d'informations sur les adresses
 *
 * Permet de visualiser l'effet d'ASLR et PIE :
 * les adresses changent à chaque exécution si
 * PIE + ASLR sont actifs.
 * ═══════════════════════════════════════════ */
static void print_address_info(void)
{
    int stack_var = 42;
    static int data_var = 100;
    char *heap_var = malloc(16);

    printf("\n--- Informations d'adressage ---\n");
    printf("  main()       @ %p  (.text)\n", (void *)main);
    printf("  greet()      @ %p  (.text)\n", (void *)greet);
    printf("  actions[]    @ %p  (.data)\n", (void *)actions);
    printf("  data_var     @ %p  (.data)\n", (void *)&data_var);
    printf("  stack_var    @ %p  (pile)\n",  (void *)&stack_var);
    printf("  heap_var     @ %p  (tas)\n",   (void *)heap_var);
    printf("--------------------------------\n\n");

    free(heap_var);
}

/* ═══════════════════════════════════════════
 * Point d'entrée
 * ═══════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    printf("=== vuln_demo — Chapitre 19 ===\n");
    printf("Démonstration des protections compilateur\n\n");

    print_address_info();

    if (argc > 1) {
        /* Mode direct : passer l'entrée en argument
         * pour déclencher facilement l'overflow */
        printf("[mode argument] Traitement de argv[1]...\n");
        process_input(argv[1]);
    } else {
        /* Mode interactif : authentification */
        if (authenticate()) {
            printf(">>> Authentification réussie.\n");
            actions[0]("utilisateur authentifié");
        } else {
            printf(">>> Authentification échouée.\n");
            actions[1]("intrus");
        }
    }

    return 0;
}
