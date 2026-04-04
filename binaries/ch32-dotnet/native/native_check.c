/*
 * native_check.c — Bibliothèque native pour LicenseChecker (Chapitre 32)
 *
 * Compilée avec GCC en shared library, appelée depuis C# via P/Invoke.
 *
 * Points d'intérêt RE :
 *   - Symboles exportés visibles avec nm / objdump / readelf
 *   - Algorithmes analysables avec Ghidra, GDB ou Frida (côté natif)
 *   - Salt différent du côté C# → il faut reverser les deux côtés
 *
 * Compilation :
 *   gcc -shared -fPIC -O2 -Wall -o libnative_check.so native_check.c
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ── Constantes ────────────────────────────────────────────────────────
 *
 * Le salt natif est DIFFÉRENT du salt managé ("REV3RSE!").
 * C'est un piège classique : l'étudiant qui ne reverse que le côté C#
 * obtiendra un segment B incorrect.
 */

static const uint8_t NATIVE_SALT[] = {
    0x4E, 0x41, 0x54, 0x49, 0x56, 0x45, 0x52, 0x45
};
/* ASCII : "NATIVERE" */

static const uint32_t FNV_OFFSET = 0x811C9DC5u;
static const uint32_t FNV_PRIME  = 0x01000193u;


/* ──────────────────────────────────────────────────────────────────────
 * compute_native_hash()
 *
 * Hash FNV-1a du buffer d'entrée, salé avec NATIVE_SALT[].
 *
 * Algorithme :
 *   1. Initialiser hash = FNV_OFFSET
 *   2. Pour chaque octet de data : hash = (hash XOR octet) * FNV_PRIME
 *   3. Pour chaque octet de NATIVE_SALT : idem
 *   4. Fold-XOR 32→16 bits : (hash >> 16) ^ (hash & 0xFFFF)
 *   5. Retour masqué sur 16 bits
 *
 * Paramètres :
 *   data   — buffer d'octets (username encodé UTF-8, en minuscules)
 *   length — taille du buffer
 *
 * Retour : hash 16 bits dans un uint32_t
 * ────────────────────────────────────────────────────────────────────── */

uint32_t compute_native_hash(const uint8_t *data, int length)
{
    uint32_t hash = FNV_OFFSET;

    /* Hash des données utilisateur */
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint32_t)data[i];
        hash *= FNV_PRIME;
    }

    /* Hash du salt natif */
    for (int i = 0; i < (int)sizeof(NATIVE_SALT); i++)
    {
        hash ^= (uint32_t)NATIVE_SALT[i];
        hash *= FNV_PRIME;
    }

    /* Fold-XOR 32→16 */
    uint32_t folded = (hash >> 16) ^ (hash & 0xFFFFu);
    return folded & 0xFFFFu;
}


/* ──────────────────────────────────────────────────────────────────────
 * compute_checksum()
 *
 * Checksum combinant les segments A, B, C de la clé.
 * Appelé depuis C# pour produire la partie native du segment D.
 *
 * Algorithme :
 *   1. val = segA
 *   2. Rotation gauche 3 bits (16 bits), XOR segB
 *   3. Rotation droite 7 bits (16 bits), XOR segC
 *   4. val = (val * 0x5BD1) & 0xFFFF
 *   5. val ^= 0x1337
 *
 * Retour : checksum 16 bits dans un uint32_t
 * ────────────────────────────────────────────────────────────────────── */

uint32_t compute_checksum(uint32_t seg_a, uint32_t seg_b, uint32_t seg_c)
{
    uint32_t val = seg_a & 0xFFFFu;

    /* Rotation gauche 3 bits (sur 16 bits) + XOR seg_b */
    val = ((val << 3) | (val >> 13)) & 0xFFFFu;
    val ^= seg_b & 0xFFFFu;

    /* Rotation droite 7 bits (sur 16 bits) + XOR seg_c */
    val = ((val >> 7) | (val << 9)) & 0xFFFFu;
    val ^= seg_c & 0xFFFFu;

    /* Mélange multiplicatif final */
    val = (val * 0x5BD1u) & 0xFFFFu;
    val ^= 0x1337u;

    return val & 0xFFFFu;
}


/* ──────────────────────────────────────────────────────────────────────
 * verify_integrity()
 *
 * Vérification d'intégrité côté natif (exercice uniquement).
 * NON appelée dans le flux principal de LicenseValidator.cs.
 * Présente comme cible de hooking Frida (§32.2 / §32.3).
 *
 * Paramètres :
 *   username       — nom d'utilisateur (ASCII/UTF-8)
 *   seg_a..seg_d   — les 4 segments de la clé
 *
 * Retour : 1 si valide, 0 sinon
 * ────────────────────────────────────────────────────────────────────── */

int verify_integrity(const char *username,
                     uint32_t seg_a, uint32_t seg_b,
                     uint32_t seg_c, uint32_t seg_d)
{
    if (username == NULL)
        return 0;

    size_t len = strlen(username);
    if (len == 0 || len > 256)
        return 0;

    /* Conversion en minuscules (ASCII uniquement) */
    uint8_t lower[256];
    for (size_t i = 0; i < len; i++)
    {
        uint8_t c = (uint8_t)username[i];
        if (c >= 'A' && c <= 'Z')
            c += 0x20;
        lower[i] = c;
    }

    /* Vérifier le segment B (hash natif) */
    uint32_t expected_b = compute_native_hash(lower, (int)len);
    if ((seg_b & 0xFFFFu) != expected_b)
        return 0;

    /* Vérifier que le segment D est non nul */
    /* (vérification partielle : le segment D complet dépend
     *  aussi de la partie managée qu'on n'a pas ici) */
    if (seg_d == 0)
        return 0;

    /* Vérifier la cohérence du checksum natif */
    uint32_t chk = compute_checksum(seg_a & 0xFFFFu,
                                    seg_b & 0xFFFFu,
                                    seg_c & 0xFFFFu);
    (void)chk;

    return 1;
}
