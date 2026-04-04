/*
 * plugin_beta.cpp — Plugin "XOR Cipher Processor"
 *
 * Formation Reverse Engineering — Chapitre 22
 * Licence MIT — Usage strictement éducatif
 *
 * Ce plugin applique un XOR cyclique avec une clé configurable.
 * Par défaut, la clé est 0x42 (un seul octet).
 * L'option "key" permet de définir une clé multi-octets (hex string).
 *
 * Intérêt RE :
 *   - La clé par défaut (0x42) est une constante repérable dans .rodata
 *   - Le XOR cyclique produit un pattern reconnaissable en analyse hexa
 *   - La sortie est affichée en hex (caractères non-imprimables)
 *   - La classe XorCipherProcessor a un membre supplémentaire (tableau de clé)
 *     → le layout mémoire diffère des autres processeurs
 */

#include "processor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static const size_t MAX_KEY_LEN = 32;

class XorCipherProcessor : public Processor {
public:
    XorCipherProcessor(uint32_t id)
        : Processor(id, 50), key_len_(1)
    {
        memset(key_, 0, sizeof(key_));
        key_[0] = 0x42; /* Clé par défaut — constante magique pour le RE */
    }

    ~XorCipherProcessor() override {
        /* Nettoyage sécurisé de la clé en mémoire */
        volatile unsigned char* vk = key_;
        for (size_t i = 0; i < MAX_KEY_LEN; i++)
            vk[i] = 0;
        fprintf(stderr, "[XorCipher #%u] destroyed (key wiped)\n", id_);
    }

    const char* name() const override {
        return "XorCipherProcessor";
    }

    bool configure(const char* key, const char* value) override {
        if (strcmp(key, "key") == 0) {
            return parse_hex_key(value);
        }
        if (strcmp(key, "printable") == 0) {
            printable_output_ = (strcmp(value, "true") == 0);
            return true;
        }
        return false;
    }

    int process(const char* input, size_t in_len,
                char* output, size_t out_cap) override
    {
        if (!enabled_ || !input || !output) return -1;

        if (printable_output_) {
            /* Mode hex : chaque octet XORé → 2 caractères hex */
            size_t max_in = in_len;
            if (max_in * 2 + 1 > out_cap)
                max_in = (out_cap - 1) / 2;

            for (size_t i = 0; i < max_in; i++) {
                unsigned char xored = (unsigned char)input[i] ^ key_[i % key_len_];
                sprintf(output + i * 2, "%02x", xored);
            }
            output[max_in * 2] = '\0';
            bytes_xored_ += max_in;
            return (int)(max_in * 2);
        } else {
            /* Mode brut */
            size_t n = (in_len < out_cap - 1) ? in_len : out_cap - 1;
            for (size_t i = 0; i < n; i++)
                output[i] = input[i] ^ key_[i % key_len_];
            output[n] = '\0';
            bytes_xored_ += n;
            return (int)n;
        }
    }

    const char* status() const override {
        static char buf[256];
        char hex_key[MAX_KEY_LEN * 2 + 1];
        for (size_t i = 0; i < key_len_; i++)
            sprintf(hex_key + i * 2, "%02x", key_[i]);
        hex_key[key_len_ * 2] = '\0';

        snprintf(buf, sizeof(buf),
                 "[XorCipher #%u] xored=%zu key=%s printable=%s",
                 id_, bytes_xored_, hex_key,
                 printable_output_ ? "yes" : "no");
        return buf;
    }

private:
    unsigned char key_[MAX_KEY_LEN];
    size_t        key_len_;
    bool          printable_output_ = true;
    size_t        bytes_xored_      = 0;

    /* Parse une chaîne hexadécimale en clé binaire */
    bool parse_hex_key(const char* hex) {
        size_t len = strlen(hex);
        if (len == 0 || len % 2 != 0 || len / 2 > MAX_KEY_LEN)
            return false;

        key_len_ = len / 2;
        for (size_t i = 0; i < key_len_; i++) {
            unsigned int byte;
            if (sscanf(hex + i * 2, "%2x", &byte) != 1)
                return false;
            key_[i] = (unsigned char)byte;
        }
        return true;
    }
};

/* === Fonctions factory (extern "C") === */

extern "C" {

Processor* create_processor(uint32_t id) {
    fprintf(stderr, "[plugin_beta] creating XorCipherProcessor with id=%u\n", id);
    return new XorCipherProcessor(id);
}

void destroy_processor(Processor* p) {
    fprintf(stderr, "[plugin_beta] destroying processor\n");
    delete p;
}

} /* extern "C" */
