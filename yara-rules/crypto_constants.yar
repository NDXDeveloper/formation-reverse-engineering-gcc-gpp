/*
 * crypto_constants.yar — Détection de constantes cryptographiques embarquées
 * Formation Reverse Engineering — Applications compilées avec la chaîne GNU
 *
 * Ce fichier contient des règles YARA pour détecter la présence de
 * constantes cryptographiques connues dans des binaires ELF compilés
 * avec GCC/G++. Il couvre :
 *   - Les constantes AES (S-box, round constants)
 *   - Les constantes SHA-1 / SHA-256 / SHA-512 (valeurs initiales, K)
 *   - Les constantes MD5 (table T)
 *   - Les constantes RC4 / ChaCha20 reconnaissables
 *   - Les marqueurs spécifiques aux binaires d'entraînement (ch21, ch24, ch25, ch27)
 *
 * Usage :
 *   yara -r crypto_constants.yar binaries/
 *   yara -s crypto_constants.yar crypto_O0        # afficher les offsets
 *
 * Licence MIT — Usage strictement éducatif.
 */

import "elf"

/* ============================================================
 *  AES — Advanced Encryption Standard
 * ============================================================ */

rule AES_SBox_Embedded
{
    meta:
        description = "Binary contains the AES forward S-box (first 32 bytes)"
        category    = "crypto"
        algorithm   = "AES"
        reference   = "FIPS 197, Section 5.1.1"

    strings:
        // Première ligne de la S-box AES (16 octets)
        $sbox_row0 = {
            63 7C 77 7B F2 6B 6F C5
            30 01 67 2B FE D7 AB 76
        }

        // Deuxième ligne (confirmation, réduit les faux positifs)
        $sbox_row1 = {
            CA 82 C9 7D FA 59 47 F0
            AD D4 A2 AF 9C A4 72 C0
        }

        // S-box inverse (première ligne) — présente si le binaire déchiffre aussi
        $inv_sbox_row0 = {
            52 09 6A D5 30 36 A5 38
            BF 40 A3 9E 81 F3 D7 FB
        }

    condition:
        $sbox_row0 and $sbox_row1
}

rule AES_Round_Constants
{
    meta:
        description = "Binary contains AES round constants (Rcon)"
        category    = "crypto"
        algorithm   = "AES"

    strings:
        // Rcon[1..10] — utilisés dans le key schedule
        // Souvent stockés comme un tableau de 10 octets ou de 10 uint32
        $rcon_bytes = { 01 02 04 08 10 20 40 80 1B 36 }

        // Variante 32 bits (chaque Rcon dans un mot de 32 bits, LE)
        $rcon_word0 = { 01 00 00 00 }
        $rcon_word1 = { 02 00 00 00 }

    condition:
        $rcon_bytes or ($rcon_word0 and $rcon_word1)
}

/* ============================================================
 *  SHA-256
 * ============================================================ */

rule SHA256_Constants
{
    meta:
        description = "Binary contains SHA-256 initial hash values or round constants"
        category    = "crypto"
        algorithm   = "SHA-256"
        reference   = "FIPS 180-4, Section 5.3.3 / 4.2.2"

    strings:
        // Valeurs initiales H0..H7 (big-endian, tel que dans la spec)
        $h0_be = { 6A 09 E6 67 }
        $h1_be = { BB 67 AE 85 }
        $h2_be = { 3C 6E F3 72 }
        $h3_be = { A5 4F F5 3A }
        $h4_be = { 51 0E 52 7F }
        $h5_be = { 9B 05 68 8C }
        $h6_be = { 1F 83 D9 AB }
        $h7_be = { 5B E0 CD 19 }

        // Valeurs initiales en little-endian (certaines implémentations
        // les stockent ainsi sur x86)
        $h0_le = { 67 E6 09 6A }
        $h1_le = { 85 AE 67 BB }
        $h2_le = { 72 F3 6E 3C }
        $h3_le = { 3A F5 4F A5 }

        // Premières constantes K[0..3] (big-endian)
        $k0 = { 42 8A 2F 98 }
        $k1 = { 71 37 44 91 }
        $k2 = { B5 C0 FB CF }
        $k3 = { E9 B5 DB A5 }

    condition:
        // Au moins 4 valeurs initiales OU au moins 3 constantes K
        (4 of ($h0_be, $h1_be, $h2_be, $h3_be, $h4_be, $h5_be, $h6_be, $h7_be)) or
        (4 of ($h0_le, $h1_le, $h2_le, $h3_le)) or
        (3 of ($k0, $k1, $k2, $k3))
}

/* ============================================================
 *  SHA-1
 * ============================================================ */

rule SHA1_Constants
{
    meta:
        description = "Binary contains SHA-1 initial hash values"
        category    = "crypto"
        algorithm   = "SHA-1"
        reference   = "FIPS 180-4, Section 5.3.1"

    strings:
        // H0..H4 (big-endian)
        $h0 = { 67 45 23 01 }
        $h1 = { EF CD AB 89 }
        $h2 = { 98 BA DC FE }
        $h3 = { 10 32 54 76 }
        $h4 = { C3 D2 E1 F0 }

        // Constantes K de SHA-1
        $k1 = { 5A 82 79 99 }
        $k2 = { 6E D9 EB A1 }
        $k3 = { 8F 1B BC DC }
        $k4 = { CA 62 C1 D6 }

    condition:
        (3 of ($h0, $h1, $h2, $h3, $h4)) or
        (3 of ($k1, $k2, $k3, $k4))
}

/* ============================================================
 *  SHA-512
 * ============================================================ */

rule SHA512_Constants
{
    meta:
        description = "Binary contains SHA-512 initial hash values"
        category    = "crypto"
        algorithm   = "SHA-512"
        reference   = "FIPS 180-4, Section 5.3.5"

    strings:
        // H0..H3 (big-endian, 64 bits chacun)
        $h0 = { 6A 09 E6 67 F3 BC C9 08 }
        $h1 = { BB 67 AE 85 84 CA A7 3B }
        $h2 = { 3C 6E F3 72 FE 94 F8 2B }
        $h3 = { A5 4F F5 3A 5F 1D 36 F1 }

    condition:
        3 of them
}

/* ============================================================
 *  MD5
 * ============================================================ */

rule MD5_Constants
{
    meta:
        description = "Binary contains MD5 initial values or T table constants"
        category    = "crypto"
        algorithm   = "MD5"
        reference   = "RFC 1321"

    strings:
        // Valeurs initiales (little-endian sur x86)
        $iv_a = { 01 23 45 67 }
        $iv_b = { 89 AB CD EF }
        $iv_c = { FE DC BA 98 }
        $iv_d = { 76 54 32 10 }

        // Premières entrées de la table T (sin-based constants, LE)
        $t1  = { 78 A4 6A D7 }   // T[1]  = 0xD76AA478
        $t2  = { 56 B7 C7 E8 }   // T[2]  = 0xE8C7B756
        $t3  = { DB 70 24 24 }   // T[3]  = 0x242070DB
        $t4  = { EE CE BD C1 }   // T[4]  = 0xC1BDCEEE

    condition:
        (3 of ($iv_a, $iv_b, $iv_c, $iv_d)) or
        (3 of ($t1, $t2, $t3, $t4))
}

/* ============================================================
 *  RC4 — détection indirecte
 * ============================================================ */

rule RC4_KSA_Pattern
{
    meta:
        description = "Potential RC4 Key Scheduling Algorithm (256-byte identity permutation init)"
        category    = "crypto"
        algorithm   = "RC4"
        note        = "Détection heuristique — la boucle KSA initialise un tableau 0..255"

    strings:
        // Séquence d'initialisation S[0..15] = 0x00..0x0F (souvent dans .data ou stack)
        // Trop générique seul, doit être combiné avec d'autres indicateurs
        $ksa_init = {
            00 01 02 03 04 05 06 07
            08 09 0A 0B 0C 0D 0E 0F
            10 11 12 13 14 15 16 17
            18 19 1A 1B 1C 1D 1E 1F
        }

    condition:
        $ksa_init and filesize < 10MB
}

/* ============================================================
 *  ChaCha20 / Salsa20
 * ============================================================ */

rule ChaCha20_Salsa20_Constants
{
    meta:
        description = "Binary contains ChaCha20 or Salsa20 'expand' constants"
        category    = "crypto"
        algorithm   = "ChaCha20/Salsa20"

    strings:
        // "expand 32-byte k" — constante de setup ChaCha20/Salsa20 (clé 256 bits)
        $expand_32 = "expand 32-byte k"

        // "expand 16-byte k" — variante clé 128 bits
        $expand_16 = "expand 16-byte k"

        // Constantes sigma en little-endian (4 mots de 32 bits)
        $sigma = { 61 70 78 65 33 20 64 6E 79 62 2D 32 6B 20 65 74 }

    condition:
        any of them
}

/* ============================================================
 *  Marqueurs spécifiques : binaires d'entraînement (ch21, ch24, ch27)
 * ============================================================ */

rule KeyGenMe_Hash_Constants
{
    meta:
        description = "Contains hash constants from the keygenme training binary (Ch21)"
        category    = "training"
        chapter     = "21"

    strings:
        // HASH_SEED = 0x5A3C6E2D (little-endian)
        $hash_seed = { 2D 6E 3C 5A }

        // HASH_XOR = 0xDEADBEEF (little-endian)
        $hash_xor  = { EF BE AD DE }

        // HASH_MUL = 0x1003F (little-endian, 32 bits)
        $hash_mul  = { 3F 00 01 00 }

        // Constante d'avalanche = 0x45D9F3B = 0x045D9F3B (little-endian)
        $avalanche = { 3B 9F 5D 04 }

        // Constantes de dérivation
        $derive_xor_a = { A5 A5 }    // XOR groupe 0
        $derive_xor_b = { 5A 5A }    // XOR groupe 1
        $derive_xor_c = { 34 12 }    // 0x1234 XOR groupe 2 (LE)
        $derive_xor_d = { DC FE }    // 0xFEDC XOR groupe 3 (LE)

        // Chaînes caractéristiques
        $banner    = "KeyGenMe v1.0"
        $prompt    = "XXXX-XXXX-XXXX-XXXX"

    condition:
        // Constantes algorithmiques + au moins une chaîne
        (3 of ($hash_seed, $hash_xor, $hash_mul, $avalanche)) and
        (1 of ($banner, $prompt))
}

rule CRYPT24_Key_Derivation
{
    meta:
        description = "Contains key derivation markers from crypto training binary (Ch24)"
        category    = "training"
        chapter     = "24"

    strings:
        // Masque XOR appliqué après SHA-256 (16 premiers octets)
        $key_mask_head = {
            DE AD BE EF CA FE BA BE
            13 37 42 42 FE ED FA CE
        }

        // Masque XOR complet (32 octets)
        $key_mask_full = {
            DE AD BE EF CA FE BA BE
            13 37 42 42 FE ED FA CE
            0B AD F0 0D DE AD C0 DE
            8B AD F0 0D 0D 15 EA 5E
        }

        // Fragments de la passphrase (tableaux const char dans .rodata)
        $pp_part1 = "r3vers3_"
        $pp_part2 = "m3_1f_"
        $pp_part3 = "y0u_c4n!"

        // Magic du format de sortie
        $crypt24_magic = "CRYPT24"

        // Chaîne de feedback
        $derived_msg = "Derived key"

    condition:
        ($key_mask_head and $crypt24_magic) or
        ($key_mask_full) or
        (2 of ($pp_part1, $pp_part2, $pp_part3) and $crypt24_magic)
}

rule CRYPT24_Encrypted_File
{
    meta:
        description = "File encrypted with the CRYPT24 format (Ch24 output)"
        category    = "training"
        chapter     = "24"
        filetype    = "data"

    strings:
        $magic = "CRYPT24"

    condition:
        // Magic au début du fichier, version 1.0 aux offsets 0x08-0x09
        $magic at 0 and
        uint8(0x08) == 0x01 and     // version majeure
        uint8(0x09) == 0x00 and     // version mineure
        uint16(0x0A) == 0x0010 and  // IV length = 16
        filesize > 0x20             // header (32 octets) + au moins 1 octet de données
}

/* ============================================================
 *  Règle composite : tout binaire contenant du crypto
 * ============================================================ */

rule Contains_Any_Crypto_Constant
{
    meta:
        description = "Umbrella rule: binary contains at least one known crypto constant"
        category    = "crypto"
        note        = "Useful for triage — triggers if any specific crypto rule matches"

    condition:
        AES_SBox_Embedded or
        AES_Round_Constants or
        SHA256_Constants or
        SHA1_Constants or
        SHA512_Constants or
        MD5_Constants or
        ChaCha20_Salsa20_Constants or
        CRYPT24_Key_Derivation
}
