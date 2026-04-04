/*
 * fuzz_fileformat.c — Harness libFuzzer pour le parseur CSTM
 *
 * Appelle directement les fonctions de parsing de fileformat.c
 * (rendues non-static par la macro FUZZ_TARGET).
 *
 * Compilation (les deux fichiers dans la même commande) :
 *   clang -DFUZZ_TARGET -fsanitize=fuzzer,address,undefined -g -O1 \
 *       -o fuzz_fileformat fuzz_fileformat.c fileformat.c
 *
 * Usage :
 *   mkdir corpus && echo -ne 'CSTM\x01\x00\x00\x00' > corpus/seed.bin
 *   ./fuzz_fileformat corpus/
 *
 * Licence MIT — Usage strictement éducatif.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════
 *  Structures et constantes reproduites depuis fileformat.c
 * ══════════════════════════════════════════════════════════ */

#define MAGIC           "CSTM"
#define MAGIC_SIZE      4
#define HEADER_SIZE     8
#define MAX_VERSION     3

typedef struct {
    char     magic[MAGIC_SIZE];
    uint8_t  version;
    uint8_t  flags;
    uint16_t section_count;
} __attribute__((packed)) FileHeader;

/* ══════════════════════════════════════════════════════════
 *  Prototypes des fonctions de fileformat.c
 *  (non-static quand compilé avec -DFUZZ_TARGET)
 * ══════════════════════════════════════════════════════════ */

int parse_header(const uint8_t *file_data, size_t file_size,
                 FileHeader *header);
int verify_signature(const uint8_t *file_data, size_t file_size,
                     const FileHeader *header);
int parse_sections(const uint8_t *file_data, size_t file_size,
                   const FileHeader *header);
int validate_checksum(const uint8_t *file_data, size_t file_size,
                      const FileHeader *header);

/* ══════════════════════════════════════════════════════════
 *  Harness libFuzzer — point d'entrée
 * ══════════════════════════════════════════════════════════ */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Rejet rapide des inputs trop petits (évite du bruit) */
    if (size < HEADER_SIZE) {
        return 0;
    }

    /* Limiter la taille pour garder le fuzzing rapide */
    if (size > 1024 * 1024) {
        return 0;
    }

    /*
     * Suppression de stdout pour éviter de noyer le terminal.
     * En production on utiliserait freopen("/dev/null", "w", stdout)
     * une seule fois dans LLVMFuzzerInitialize, mais ici on garde
     * le code simple et auto-contenu.
     */
    FILE *devnull = fopen("/dev/null", "w");
    FILE *saved_stdout = stdout;
    if (devnull) {
        stdout = devnull;
    }

    /* ── Reproduire la logique de main() in-process ── */

    FileHeader header;

    if (parse_header(data, size, &header) != 0) {
        goto cleanup;
    }

    /* Vérification de signature pour la version 3 */
    if (header.version == 3) {
        if (verify_signature(data, size, &header) != 0) {
            goto cleanup;
        }
    }

    /* Parsing des sections */
    if (header.section_count > 0) {
        parse_sections(data, size, &header);
    }

    /* Validation du checksum si le flag est activé */
    if (header.flags & 0x01) {
        validate_checksum(data, size, &header);
    }

cleanup:
    /* Restaurer stdout */
    if (devnull) {
        stdout = saved_stdout;
        fclose(devnull);
    }

    return 0;
}

/*
 * LLVMFuzzerInitialize — Hook d'initialisation optionnel.
 * Appelé une seule fois avant la première itération.
 * Peut être utilisé pour parser les arguments du harness.
 */
int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;

    /* Supprimer stderr pour les messages d'erreur du parseur
     * (optionnel — décommenter si la sortie est trop verbeuse) */
    /* freopen("/dev/null", "w", stderr); */

    return 0;
}
