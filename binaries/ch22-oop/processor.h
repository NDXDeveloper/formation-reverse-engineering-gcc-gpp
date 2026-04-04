/*
 * processor.h — Interface commune pour le système de traitement de données
 *
 * Formation Reverse Engineering — Chapitre 22
 * Licence MIT — Usage strictement éducatif
 *
 * Cette interface définit le contrat que tout processeur (interne ou plugin)
 * doit respecter. En RE, c'est cette interface qu'il faudra reconstruire
 * à partir des vtables et des appels virtuels.
 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <cstddef>
#include <cstdint>

/* --------------------------------------------------------------------
 * Classe de base abstraite : Processor
 *
 * Layout mémoire (GCC / Itanium ABI, x86-64) :
 *   +0x00  vptr          → pointe vers la vtable dans .rodata
 *   +0x08  id_           → uint32_t, identifiant unique (4 octets)
 *   +0x0C  priority_     → int, priorité d'exécution (4 octets)
 *   +0x10  enabled_      → bool (1 octet)
 *   +0x11  (padding 7B pour alignement struct à 8)
 *   Total : 0x18 (24 octets)
 *
 * Vtable (depuis le vptr, GCC Itanium ABI) :
 *   [0] → ~Processor() D1       (complete object destructor)
 *   [1] → ~Processor() D0       (deleting destructor)
 *   [2] → name()
 *   [3] → configure()
 *   [4] → process()
 *   [5] → status()
 * -------------------------------------------------------------------- */
class Processor {
public:
    Processor(uint32_t id, int priority)
        : id_(id), priority_(priority), enabled_(true) {}

    virtual ~Processor() {}

    /* Retourne le nom lisible du processeur */
    virtual const char* name() const = 0;

    /* Configure le processeur avec une paire clé/valeur */
    virtual bool configure(const char* key, const char* value) = 0;

    /* Traite un buffer d'entrée, écrit dans le buffer de sortie.
     * Retourne le nombre d'octets écrits, ou -1 en cas d'erreur. */
    virtual int process(const char* input, size_t in_len,
                        char* output, size_t out_cap) = 0;

    /* Retourne une chaîne de statut (pour le logging) */
    virtual const char* status() const = 0;

    /* Accesseurs non-virtuels */
    uint32_t id() const { return id_; }
    int priority() const { return priority_; }
    bool enabled() const { return enabled_; }
    void set_enabled(bool e) { enabled_ = e; }

protected:
    uint32_t id_;
    int      priority_;
    bool     enabled_;
};

/* --------------------------------------------------------------------
 * Convention de plugin :
 *
 * Chaque plugin .so doit exporter deux fonctions C :
 *   - Processor* create_processor(uint32_t id)
 *   - void destroy_processor(Processor* p)
 *
 * Le symbole est extern "C" pour éviter le name mangling.
 * -------------------------------------------------------------------- */
typedef Processor* (*create_func_t)(uint32_t id);
typedef void       (*destroy_func_t)(Processor*);

#define PLUGIN_CREATE_SYMBOL  "create_processor"
#define PLUGIN_DESTROY_SYMBOL "destroy_processor"

#endif /* PROCESSOR_H */
