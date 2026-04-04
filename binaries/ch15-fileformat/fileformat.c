/*
 * fileformat.c — Parseur de format de fichier custom (CSTM)
 *
 * Binaire d'entraînement pour les chapitres 15 (Fuzzing) et 25 (Reverse de format).
 * Contient des bugs intentionnels destinés à être découverts par fuzzing.
 *
 * Format CSTM :
 *   [HEADER 8 octets] [SECTION_DESC 8 octets + PAYLOAD N octets] × count [CHECKSUM 4 octets]
 *
 * Licence MIT — Usage strictement éducatif.
 */

/*
 * Guard pour la compilation avec libFuzzer :
 * Quand FUZZ_TARGET est défini, main() est exclu et les fonctions
 * de parsing sont non-static, appelables depuis le harness.
 *
 *   gcc -o fileformat fileformat.c                          → binaire normal
 *   clang -DFUZZ_TARGET -fsanitize=fuzzer,address ...       → build libFuzzer
 */
#ifdef FUZZ_TARGET
  #define MAYBE_STATIC
#else
  #define MAYBE_STATIC static
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ═══════════════════════════════════════════
 *  Constantes et structures
 * ═══════════════════════════════════════════ */

#define MAGIC           "CSTM"
#define MAGIC_SIZE      4
#define HEADER_SIZE     8
#define SECTION_DESC_SIZE 8

#define TYPE_DATA       0x01
#define TYPE_INDEX      0x02
#define TYPE_META       0x03
#define FLAG_COMPRESSED 0x80
#define TYPE_MASK       0x7F

#define MAX_SECTIONS    256
#define INDEX_TABLE_SIZE 64
#define MAX_VERSION     3

/* ── Structures du format ── */

typedef struct {
    char     magic[MAGIC_SIZE];
    uint8_t  version;
    uint8_t  flags;
    uint16_t section_count;
} __attribute__((packed)) FileHeader;

typedef struct {
    uint8_t  type;
    uint8_t  reserved[3];
    uint32_t length;
} __attribute__((packed)) SectionDescriptor;

/* ── Table interne pour les sections INDEX ── */

static const char *g_index_labels[INDEX_TABLE_SIZE] = {
    "entry_0",  "entry_1",  "entry_2",  "entry_3",
    "entry_4",  "entry_5",  "entry_6",  "entry_7",
    "entry_8",  "entry_9",  "entry_10", "entry_11",
    "entry_12", "entry_13", "entry_14", "entry_15",
    "entry_16", "entry_17", "entry_18", "entry_19",
    "entry_20", "entry_21", "entry_22", "entry_23",
    "entry_24", "entry_25", "entry_26", "entry_27",
    "entry_28", "entry_29", "entry_30", "entry_31",
    "entry_32", "entry_33", "entry_34", "entry_35",
    "entry_36", "entry_37", "entry_38", "entry_39",
    "entry_40", "entry_41", "entry_42", "entry_43",
    "entry_44", "entry_45", "entry_46", "entry_47",
    "entry_48", "entry_49", "entry_50", "entry_51",
    "entry_52", "entry_53", "entry_54", "entry_55",
    "entry_56", "entry_57", "entry_58", "entry_59",
    "entry_60", "entry_61", "entry_62", "entry_63",
};

/* ── Déclarations anticipées ── */

MAYBE_STATIC int process_data_section(const uint8_t *data, uint32_t len);
MAYBE_STATIC int process_index_section(const uint8_t *data, uint32_t len);
MAYBE_STATIC int process_meta_section(const uint8_t *data, uint32_t len);
MAYBE_STATIC int decompress_section(const uint8_t *compressed, uint32_t comp_len,
                                    uint8_t **out_data, uint32_t *out_len);

/* ═══════════════════════════════════════════
 *  Fonctions de traitement des sections
 * ═══════════════════════════════════════════ */

/*
 * decode_section — Décode le payload brut d'une section.
 *
 * BUG INTENTIONNEL (A) : la longueur déclarée dans le descripteur n'est
 * pas validée par rapport à la taille réelle du fichier. Un champ length
 * malveillant provoque un heap-buffer-overflow en lecture.
 */
MAYBE_STATIC int decode_section(const uint8_t *file_data, size_t file_size,
                                uint32_t offset, uint32_t length, uint8_t type)
{
    (void)file_size; /* BUG : file_size ignoré intentionnellement */

    printf("Decoding section at offset %u, length %u\n", offset, length);

    if (length == 0) {
        return 0;
    }

    /* Allocation d'un buffer de travail pour le payload */
    uint8_t *buf = (uint8_t *)malloc(length);
    if (!buf) {
        fprintf(stderr, "Error: malloc failed for section payload\n");
        return -1;
    }

    /*
     * >>> BUG A : lecture de `length` octets sans vérifier offset+length <= file_size
     * Un descripteur avec length=1000 dans un fichier de 30 octets provoque
     * un heap-buffer-overflow (READ of size `length`).
     */
    memcpy(buf, file_data + offset, length);

    int result = 0;
    uint8_t base_type = type & TYPE_MASK;

    /* Dispatch selon le type de section */
    switch (base_type) {
    case TYPE_DATA:
        result = process_data_section(buf, length);
        break;
    case TYPE_INDEX:
        result = process_index_section(buf, length);
        break;
    case TYPE_META:
        result = process_meta_section(buf, length);
        break;
    default:
        fprintf(stderr, "Error: unknown section type 0x%02x\n", base_type);
        result = -1;
        break;
    }

    free(buf);
    return result;
}

/* ── process_data_section ── */

MAYBE_STATIC int process_data_section(const uint8_t *section_data, uint32_t section_len)
{
    printf("Section type: DATA\n");

    if (section_len < 4) {
        printf("  DATA section: raw payload (%u bytes)\n", section_len);
        return 0;
    }

    /* Les 4 premiers octets sont un tag de sous-type */
    uint32_t data_tag = *(const uint32_t *)section_data;
    printf("  DATA tag: 0x%08x\n", data_tag);

    /* Traitement basique : compter les octets non nuls */
    uint32_t nonzero = 0;
    for (uint32_t i = 4; i < section_len; i++) {
        if (section_data[i] != 0x00) {
            nonzero++;
        }
    }
    printf("  DATA payload: %u non-zero bytes out of %u\n", nonzero, section_len - 4);

    return 0;
}

/* ── process_index_section ── */

/*
 * BUG INTENTIONNEL (B) : les entrées d'index lues depuis le payload sont
 * utilisées directement comme indices dans g_index_labels[] sans vérification
 * de bornes. Une valeur >= INDEX_TABLE_SIZE provoque un SEGV.
 */
MAYBE_STATIC int process_index_section(const uint8_t *section_data, uint32_t section_len)
{
    printf("Section type: INDEX\n");

    if (section_len < 4) {
        fprintf(stderr, "Error: INDEX section too small\n");
        return -1;
    }

    uint32_t entry_count = section_len / sizeof(uint32_t);
    const uint32_t *entries = (const uint32_t *)section_data;

    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t index_entry = entries[i];

        /*
         * >>> BUG B : pas de vérification `index_entry < INDEX_TABLE_SIZE`
         * Une valeur comme 0xFF (255) dépasse la table de 64 entrées
         * et provoque un accès mémoire invalide (SEGV).
         */
        const char *label = g_index_labels[index_entry];
        printf("  INDEX[%u] = %u -> %s\n", i, index_entry, label);
    }

    return 0;
}

/* ── process_meta_section ── */

MAYBE_STATIC int process_meta_section(const uint8_t *section_data, uint32_t section_len)
{
    printf("Section type: META\n");

    if (section_len < 2) {
        fprintf(stderr, "Error: META section too small\n");
        return -1;
    }

    /* Premier octet : nombre de paires clé-valeur */
    uint8_t kv_count = section_data[0];
    /* Deuxième octet : taille max d'une clé */
    uint8_t key_max_len = section_data[1];

    printf("  META: %u key-value pairs, max key length %u\n", kv_count, key_max_len);

    uint32_t pos = 2;
    for (uint8_t i = 0; i < kv_count && pos < section_len; i++) {
        if (pos + 1 >= section_len) break;
        uint8_t klen = section_data[pos++];
        if (pos + klen > section_len) break;

        /* Afficher la clé (tronquée si nécessaire) */
        char key_buf[256];
        uint8_t print_len = (klen < 255) ? klen : 255;
        memcpy(key_buf, section_data + pos, print_len);
        key_buf[print_len] = '\0';
        pos += klen;

        if (pos + 4 > section_len) break;
        uint32_t value = *(const uint32_t *)(section_data + pos);
        pos += 4;

        printf("  META[%u]: \"%s\" = 0x%08x\n", i, key_buf, value);
    }

    return 0;
}

/* ═══════════════════════════════════════════
 *  Décompression (jamais atteinte par le fuzzer naïf)
 * ═══════════════════════════════════════════ */

/*
 * decompress_section — Décompresse un payload de section.
 * Appelée uniquement quand le bit FLAG_COMPRESSED (0x80) est activé
 * dans le champ type du descripteur de section.
 *
 * Implémente un schéma RLE (Run-Length Encoding) simple :
 *   [taille décompressée (4B LE)] puis [count (1B)] [value (1B)] répété
 */
MAYBE_STATIC int decompress_section(const uint8_t *compressed, uint32_t comp_len,
                                    uint8_t **out_data, uint32_t *out_len)
{
    printf("  Decompressing section (RLE, %u compressed bytes)...\n", comp_len);

    if (comp_len < 4) {
        fprintf(stderr, "Error: compressed section too small\n");
        return -1;
    }

    /* Les 4 premiers octets sont la taille décompressée (uint32_t LE) */
    uint32_t decompressed_size = *(const uint32_t *)compressed;

    if (decompressed_size > 1024 * 1024) { /* 1 Mo max */
        fprintf(stderr, "Error: decompressed size too large (%u)\n", decompressed_size);
        return -1;
    }

    uint8_t *output = (uint8_t *)malloc(decompressed_size);
    if (!output) {
        fprintf(stderr, "Error: malloc failed for decompression\n");
        return -1;
    }

    uint32_t src_pos = 4;
    uint32_t dst_pos = 0;

    while (src_pos + 1 < comp_len && dst_pos < decompressed_size) {
        uint8_t count = compressed[src_pos++];
        uint8_t value = compressed[src_pos++];

        for (uint8_t j = 0; j < count && dst_pos < decompressed_size; j++) {
            output[dst_pos++] = value;
        }
    }

    /* Remplir le reste avec des zéros si la décompression est incomplète */
    while (dst_pos < decompressed_size) {
        output[dst_pos++] = 0x00;
    }

    *out_data = output;
    *out_len = decompressed_size;

    printf("  Decompressed: %u -> %u bytes\n", comp_len, decompressed_size);
    return 0;
}

/* ═══════════════════════════════════════════
 *  Checksum
 * ═══════════════════════════════════════════ */

/*
 * validate_checksum — Vérifie le checksum du fichier.
 *
 * Le checksum est un XOR rotatif sur tous les octets du fichier sauf
 * les 4 derniers (qui contiennent le checksum attendu).
 *
 * BUG INTENTIONNEL (C) : le buffer `work_buf` est alloué sur la pile
 * avec une taille fixe prévue pour MAX_SECTIONS (256) sections max.
 * Mais la boucle utilise `section_count` directement comme borne,
 * sans vérifier qu'il ne dépasse pas MAX_SECTIONS. Un section_count
 * supérieur à MAX_SECTIONS provoque un stack-buffer-overflow en écriture.
 */
MAYBE_STATIC int validate_checksum(const uint8_t *file_data, size_t file_size,
                                   const FileHeader *header)
{
    if (file_size < HEADER_SIZE + 4) {
        /* Pas assez de données pour contenir un checksum */
        return 0; /* Checksum optionnel pour les petits fichiers */
    }

    /*
     * >>> BUG C : work_buf dimensionné pour MAX_SECTIONS (256), mais indexé
     * par section_count qui peut aller jusqu'à 65535 (uint16_t).
     * Quand section_count > 256, l'écriture dépasse work_buf → stack overflow.
     * Le fix correct serait : if (header->section_count > MAX_SECTIONS) return -1;
     */
    uint8_t work_buf[MAX_SECTIONS * 4]; /* 256 * 4 = 1024 octets sur la pile */

    /* Initialiser le buffer de travail */
    memset(work_buf, 0, sizeof(work_buf));

    /* Calculer le checksum : XOR rotatif */
    size_t checksum_data_len = file_size - 4;
    uint32_t computed = 0;

    for (size_t i = 0; i < checksum_data_len; i++) {
        computed ^= ((uint32_t)file_data[i]) << ((i % 4) * 8);
        /* Accumuler dans le buffer de travail par section */
        if (i >= HEADER_SIZE && header->section_count > 0) {
            /*
             * section_idx peut être [0, section_count-1]
             * Si section_count > MAX_SECTIONS, l'index dépasse work_buf
             */
            size_t section_idx = (i - HEADER_SIZE) % header->section_count;
            size_t buf_idx = section_idx * 4 + (i % 4);
            work_buf[buf_idx] ^= file_data[i]; /* <<< OVERFLOW ICI */
        }
    }

    /* Lire le checksum attendu (4 derniers octets du fichier) */
    uint32_t expected = *(const uint32_t *)(file_data + file_size - 4);

    if (computed != expected) {
        fprintf(stderr, "Checksum mismatch: expected 0x%08x, got 0x%08x\n",
                expected, computed);
        return -1;
    }

    printf("Checksum OK (0x%08x)\n", computed);
    return 0;
}

/* ═══════════════════════════════════════════
 *  Signature (version 3 uniquement)
 * ═══════════════════════════════════════════ */

/*
 * verify_signature — Vérifie une signature pour les fichiers version 3.
 * Jamais atteinte par le fuzzer si aucun seed version 3 n'est fourni.
 */
MAYBE_STATIC int verify_signature(const uint8_t *file_data, size_t file_size,
                                  const FileHeader *header)
{
    (void)header;

    printf("Verifying signature (version 3)...\n");

    if (file_size < HEADER_SIZE + 32) {
        fprintf(stderr, "Error: file too small for signature block\n");
        return -1;
    }

    /*
     * La signature est un bloc de 16 octets situé juste après le header,
     * avant les descripteurs de section. Elle doit commencer par "SIG\x00".
     */
    const uint8_t *sig_block = file_data + HEADER_SIZE;

    if (memcmp(sig_block, "SIG\x00", 4) != 0) {
        fprintf(stderr, "Error: invalid signature marker\n");
        return -1;
    }

    /* Vérification simplifiée : les octets 4..15 du bloc signature
     * doivent avoir un XOR cumulé égal à 0x42 */
    uint8_t sig_check = 0;
    for (int i = 4; i < 16; i++) {
        sig_check ^= sig_block[i];
    }

    if (sig_check != 0x42) {
        fprintf(stderr, "Error: signature verification failed (0x%02x != 0x42)\n",
                sig_check);
        return -1;
    }

    printf("Signature OK\n");
    return 0;
}

/* ═══════════════════════════════════════════
 *  Parsing principal
 * ═══════════════════════════════════════════ */

MAYBE_STATIC int parse_header(const uint8_t *file_data, size_t file_size,
                              FileHeader *header)
{
    if (file_size < HEADER_SIZE) {
        fprintf(stderr, "Error: file too small\n");
        return -1;
    }

    memcpy(header, file_data, HEADER_SIZE);

    if (memcmp(header->magic, MAGIC, MAGIC_SIZE) != 0) {
        fprintf(stderr, "Error: invalid magic\n");
        return -1;
    }

    if (header->version == 0 || header->version > MAX_VERSION) {
        fprintf(stderr, "Error: unsupported version %d\n", header->version);
        return -1;
    }

    printf("Parsing header...\n");
    printf("  Magic:   %.4s\n", header->magic);
    printf("  Version: %u\n", header->version);
    printf("  Flags:   0x%02x\n", header->flags);
    printf("  Sections: %u\n", header->section_count);

    return 0;
}

MAYBE_STATIC int parse_sections(const uint8_t *file_data, size_t file_size,
                                const FileHeader *header)
{
    uint32_t offset;

    /* Pour la version 3, le bloc signature (16 octets) suit le header */
    if (header->version == 3) {
        offset = HEADER_SIZE + 16; /* signature block */
    } else {
        offset = HEADER_SIZE;
    }

    for (uint16_t i = 0; i < header->section_count; i++) {
        /* Vérifier qu'on peut lire le descripteur de section */
        if (offset + SECTION_DESC_SIZE > file_size) {
            fprintf(stderr, "Error: unexpected end of file at section %u descriptor\n", i);
            return -1;
        }

        SectionDescriptor desc;
        memcpy(&desc, file_data + offset, SECTION_DESC_SIZE);
        offset += SECTION_DESC_SIZE;

        uint8_t raw_type = desc.type;
        uint8_t is_compressed = (raw_type & FLAG_COMPRESSED) != 0;
        uint8_t base_type = raw_type & TYPE_MASK;

        printf("Section %u: type=0x%02x (base=0x%02x, compressed=%d), length=%u\n",
               i, raw_type, base_type, is_compressed, desc.length);

        if (is_compressed) {
            /* Chemin de décompression — rarement atteint */
            uint8_t *decompressed = NULL;
            uint32_t decomp_len = 0;

            if (decompress_section(file_data + offset, desc.length,
                                   &decompressed, &decomp_len) != 0) {
                fprintf(stderr, "Error: decompression failed for section %u\n", i);
                offset += desc.length;
                continue;
            }

            /* Décoder la section décompressée */
            int result = 0;
            switch (base_type) {
            case TYPE_DATA:
                result = process_data_section(decompressed, decomp_len);
                break;
            case TYPE_INDEX:
                result = process_index_section(decompressed, decomp_len);
                break;
            case TYPE_META:
                result = process_meta_section(decompressed, decomp_len);
                break;
            default:
                fprintf(stderr, "Error: unknown section type 0x%02x\n", base_type);
                result = -1;
                break;
            }

            free(decompressed);
            if (result != 0) {
                offset += desc.length;
                continue;
            }
        } else {
            /*
             * Chemin normal : decode_section lit directement depuis file_data.
             * C'est ici que le BUG A se manifeste (length non validé).
             */
            if (decode_section(file_data, file_size,
                               offset, desc.length, raw_type) != 0) {
                fprintf(stderr, "Error: failed to decode section %u\n", i);
            }
        }

        offset += desc.length;
    }

    return 0;
}

/* ═══════════════════════════════════════════
 *  Point d'entrée (exclu en mode libFuzzer)
 * ═══════════════════════════════════════════ */

#ifndef FUZZ_TARGET

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    /* ── Lecture du fichier ── */
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open file\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long raw_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (raw_size <= 0 || raw_size > 10 * 1024 * 1024) { /* 10 Mo max */
        fprintf(stderr, "Error: file too small\n");
        fclose(f);
        return 1;
    }
    size_t file_size = (size_t)raw_size;

    uint8_t *file_data = (uint8_t *)malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Error: malloc failed\n");
        fclose(f);
        return 1;
    }

    if (fread(file_data, 1, file_size, f) != file_size) {
        fprintf(stderr, "Error: read failed\n");
        free(file_data);
        fclose(f);
        return 1;
    }
    fclose(f);

    /* ── Parsing ── */
    FileHeader header;

    if (parse_header(file_data, file_size, &header) != 0) {
        free(file_data);
        return 1;
    }

    /* Vérification de signature pour la version 3 */
    if (header.version == 3) {
        if (verify_signature(file_data, file_size, &header) != 0) {
            free(file_data);
            return 1;
        }
    }

    /* Parsing des sections */
    if (header.section_count > 0) {
        if (parse_sections(file_data, file_size, &header) != 0) {
            fprintf(stderr, "Error: section parsing failed\n");
            free(file_data);
            return 1;
        }
    }

    /* Validation du checksum (après le décodage, pas avant) */
    if (header.flags & 0x01) {
        /* Le bit 0 des flags indique la présence d'un checksum */
        if (validate_checksum(file_data, file_size, &header) != 0) {
            fprintf(stderr, "Warning: checksum validation failed\n");
            /* On continue malgré l'échec — le RE peut observer le comportement */
        }
    }

    printf("Processing complete: %u sections parsed\n", header.section_count);

    free(file_data);
    return 0;
}

#endif /* FUZZ_TARGET */
