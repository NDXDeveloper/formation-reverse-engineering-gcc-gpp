/*
 * solutions/ch22-checkpoint-plugin.cpp
 *
 * ⚠️  SPOILER — Corrigé du checkpoint du chapitre 22
 *
 * Formation Reverse Engineering — Licence MIT
 *
 * Ce fichier contient :
 *   1. Le header reconstitué (processor_reconstructed.h) inline
 *   2. L'implémentation du plugin LeetSpeakProcessor
 *   3. Les fonctions factory extern "C"
 *   4. Un programme de vérification du layout mémoire (en fin de fichier,
 *      compilable séparément avec -DCHECK_LAYOUT)
 *
 * ═══════════════════════════════════════════════════════════════════
 *  DÉMARCHE DE RÉSOLUTION COMPLÈTE
 * ═══════════════════════════════════════════════════════════════════
 *
 * ── Étape A : identifier les symboles factory ──────────────────
 *
 *   $ strings oop_O2_strip | grep -iE 'create|destroy|plugin'
 *     create_processor
 *     destroy_processor
 *     ./plugins
 *     .so
 *     [Pipeline] loading plugin: %s
 *     [Pipeline] missing symbols in %s
 *
 *   → L'hôte attend deux symboles extern "C" :
 *       Processor* create_processor(uint32_t id);
 *       void       destroy_processor(Processor* p);
 *
 *   Confirmation avec ltrace :
 *   $ ltrace -e dlsym ./oop_O2_strip -p ./plugins "test" 2>&1
 *     dlsym(0x..., "create_processor")  = 0x...
 *     dlsym(0x..., "destroy_processor") = 0x...
 *
 * ── Étape B : reconstruire la hiérarchie de classes ────────────
 *
 *   $ strings oop_O2_strip | grep -E '^[0-9]+[A-Z]'
 *     9Processor
 *     19UpperCaseProcessor
 *     16ReverseProcessor
 *     8Pipeline
 *
 *   $ strings plugins/plugin_alpha.so | grep -E '^[0-9]+[A-Z]'
 *     15Rot13Processor
 *
 *   $ strings plugins/plugin_beta.so | grep -E '^[0-9]+[A-Z]'
 *     19XorCipherProcessor
 *
 *   $ nm -C plugins/plugin_alpha.so | grep typeinfo
 *     ... V typeinfo for Rot13Processor
 *     ... U typeinfo for Processor           ← parent = Processor
 *
 *   Hiérarchie :
 *     Processor (abstraite)
 *     ├── UpperCaseProcessor
 *     ├── ReverseProcessor
 *     ├── Rot13Processor       (plugin_alpha.so)
 *     └── XorCipherProcessor   (plugin_beta.so)
 *
 * ── Étape C : reconstruire la vtable ───────────────────────────
 *
 *   Méthode : examiner la vtable de Rot13Processor dans Ghidra.
 *   Localisation : nm -C plugin_alpha.so | grep vtable
 *     → vtable for Rot13Processor à une adresse dans .data.rel.ro
 *
 *   Le vptr pointe après offset-to-top et typeinfo. Entrées :
 *
 *   Index  Offset  Symbole (si dispo)                   Déduit de
 *   ─────  ──────  ───────────────────────────────────   ──────────────────
 *   [0]    +0x00   ~Rot13Processor() (complete dtor)     tous les plugins
 *   [1]    +0x08   ~Rot13Processor() (deleting dtor)     tous les plugins
 *   [2]    +0x10   Rot13Processor::name()                retourne "Rot13Processor"
 *   [3]    +0x18   Rot13Processor::configure()           compare "half_rot"
 *   [4]    +0x20   Rot13Processor::process()             cœur du traitement
 *   [5]    +0x28   Rot13Processor::status()              retourne chaîne statut
 *
 *   Vérification croisée :
 *   - La vtable de UpperCaseProcessor (dans oop_O0) a les mêmes 6 slots.
 *   - La vtable de Processor a __cxa_pure_virtual aux slots 2–5
 *     → confirme que name/configure/process/status sont virtuels purs.
 *   - Le slot 0/1 de Processor pointe vers un vrai destructeur
 *     → le destructeur est virtuel mais pas pur.
 *
 *   Confirmation des signatures via le désassemblage :
 *   - name()      : pas d'argument autre que this (rdi), retourne ptr (rax)
 *   - configure() : this(rdi), key(rsi), value(rdx), retourne bool (eax)
 *   - process()   : this(rdi), input(rsi), in_len(rdx), output(rcx),
 *                    out_cap(r8), retourne int (eax)
 *   - status()    : pas d'argument autre que this (rdi), retourne ptr (rax)
 *
 * ── Étape D : reconstruire le layout mémoire ──────────────────
 *
 *   Analyse des accès [rdi+offset] dans les méthodes de Processor
 *   (observés dans UpperCaseProcessor et ReverseProcessor de oop_O0) :
 *
 *   Instruction observée dans configure() :
 *     ; Pas d'accès spécifique aux champs Processor dans configure()
 *
 *   Instruction observée dans process() :
 *     movzx  eax, BYTE PTR [rdi+0x10]    ; test enabled_
 *     test   al, al
 *     je     .skip                         ; if (!enabled_) return -1;
 *
 *   Instruction observée dans le constructeur UpperCaseProcessor :
 *     lea    rax, [rip + vtable + 0x10]
 *     mov    QWORD PTR [rdi], rax          ; vptr       @ +0x00
 *     mov    DWORD PTR [rdi+0x08], esi     ; id_        @ +0x08
 *     mov    DWORD PTR [rdi+0x0C], 0x0A    ; priority_  @ +0x0C (10)
 *     mov    BYTE  PTR [rdi+0x10], 0x01    ; enabled_   @ +0x10 (true)
 *
 *   Dans UpperCaseProcessor::process(), accès spécifiques :
 *     movzx  eax, BYTE PTR [rdi+0x11]     ; skip_digits_ @ +0x11
 *
 *   Dans UpperCaseProcessor::status() :
 *     mov    rsi, QWORD PTR [rdi+0x18]    ; bytes_processed_ @ +0x18
 *
 *   Layout reconstitué de Processor (classe de base) :
 *     +0x00  vptr          (8 octets)
 *     +0x08  id_           (4 octets, uint32_t)
 *     +0x0C  priority_     (4 octets, int)
 *     +0x10  enabled_      (1 octet, bool)
 *     +0x11  (padding)     (7 octets)
 *     Total : 0x18 = 24 octets
 *
 *   Layout reconstitué de UpperCaseProcessor :
 *     +0x00–0x17  hérité de Processor  (24 octets)
 *       → +0x10   enabled_  (bool, 1 octet)
 *     +0x11       skip_digits_         (1 octet, bool — placé sans padding après enabled_)
 *     +0x12       (padding)            (6 octets)
 *     +0x18       bytes_processed_     (8 octets, size_t)
 *     Total : 0x20 = 32 octets   ← confirmé par operator new(0x20)
 *
 *   Layout reconstitué de Rot13Processor (plugin_alpha.so) :
 *     Observé dans create_processor → operator new(0x28) → 40 octets
 *     +0x00–0x17  hérité de Processor
 *       → +0x10   enabled_  (bool)
 *     +0x11       half_rot_            (1 octet, bool — après enabled_ sans padding)
 *     +0x12       (padding)            (6 octets)
 *     +0x18       total_rotated_       (8 octets, size_t)
 *     +0x20       call_count_          (8 octets, size_t)
 *     Total : 0x28 = 40 octets  ← confirmé par operator new(0x28)
 *
 *   Layout reconstitué de XorCipherProcessor (plugin_beta.so) :
 *     Observé dans create_processor → operator new(0x50) → 80 octets
 *     +0x00–0x17  hérité de Processor
 *       → +0x10   enabled_  (bool)
 *     +0x11       key_[32]             (32 octets, unsigned char[] — pas d'alignement requis)
 *     +0x31       (padding)            (7 octets, alignement pour size_t)
 *     +0x38       key_len_             (8 octets, size_t)
 *     +0x40       printable_output_    (1 octet, bool)
 *     +0x41       (padding)            (7 octets)
 *     +0x48       bytes_xored_         (8 octets, size_t)
 *     Total : 0x50 = 80 octets  ← confirmé
 *
 * ── Étape E : vérifier le contrat de discovery ────────────────
 *
 *   L'hôte scanne un répertoire et filtre par ".so" :
 *   $ strace -e openat ./oop_O2_strip -p ./plugins "test" 2>&1 | grep plugin
 *     openat(AT_FDCWD, "./plugins", O_RDONLY|O_DIRECTORY) = 3
 *     openat(AT_FDCWD, "./plugins/plugin_alpha.so", ...) = 4
 *     openat(AT_FDCWD, "./plugins/plugin_beta.so", ...)  = 4
 *
 *   → Il suffit de placer notre .so dans le répertoire ./plugins/.
 *   → Le nom du fichier doit se terminer par ".so".
 *
 * ── Étape F : compiler, vérifier, exécuter ────────────────────
 *
 *   Compilation :
 *   $ g++ -shared -fPIC -std=c++17 -O2 \
 *         -o plugins/plugin_gamma.so ch22-checkpoint-plugin.cpp
 *
 *   Vérification des symboles :
 *   $ nm -CD plugins/plugin_gamma.so | grep -E 'T (create|destroy)'
 *     ... T create_processor
 *     ... T destroy_processor
 *
 *   Vérification RTTI :
 *   $ nm -C plugins/plugin_gamma.so | grep typeinfo
 *     ... V typeinfo for LeetSpeakProcessor
 *     ... U typeinfo for Processor             ← OK : parent en U
 *
 *   Exécution :
 *   $ ./oop_O2_strip -p ./plugins "Hello World from RE"
 *     [STEP] UpperCaseProcessor → "HELLO WORLD FROM RE"
 *     [STEP] ReverseProcessor   → "ER MORF DLROW OLLEH"
 *     [STEP] Rot13Processor     → "..."
 *     [STEP] LeetSpeakProcessor → "..."           ← Notre plugin
 *     [STEP] XorCipherProcessor → "..."
 *
 * ═══════════════════════════════════════════════════════════════════
 */

/* ====================================================================
 * PARTIE 1 : Header reconstitué
 * ====================================================================
 *
 * Ce header est le produit direct du reverse engineering.
 * Il a été reconstitué sans accès au fichier processor.h original.
 *
 * Points critiques à respecter :
 *   - L'ordre des méthodes virtuelles détermine la vtable.
 *   - L'ordre et le type des champs déterminent le layout mémoire.
 *   - Le destructeur doit être virtuel (sinon la vtable est décalée).
 *   - Les champs protected permettent l'accès depuis les classes dérivées.
 * ==================================================================== */

#ifndef CHECK_LAYOUT  /* Pas inclus en mode vérification du layout */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

class Processor {
public:
    Processor(uint32_t id, int priority)
        : id_(id), priority_(priority), enabled_(true) {}

    /* Slot vtable [0] et [1] — destructeur virtuel (complete + deleting) */
    virtual ~Processor() {}

    /* Slot vtable [2] — retourne le nom lisible du processeur */
    virtual const char* name() const = 0;

    /* Slot vtable [3] — configure via paire clé/valeur */
    virtual bool configure(const char* key, const char* value) = 0;

    /* Slot vtable [4] — traitement principal */
    virtual int process(const char* input, size_t in_len,
                        char* output, size_t out_cap) = 0;

    /* Slot vtable [5] — chaîne de statut */
    virtual const char* status() const = 0;

    /* Accesseurs non-virtuels (souvent inlinés en -O2) */
    uint32_t id() const { return id_; }
    int priority() const { return priority_; }
    bool enabled() const { return enabled_; }
    void set_enabled(bool e) { enabled_ = e; }

protected:
    uint32_t id_;        /* +0x08 */
    int      priority_;  /* +0x0C */
    bool     enabled_;   /* +0x10 */
    /* padding +0x11 à +0x17, total struct = 0x18 (24 octets) */
};


/* ====================================================================
 * PARTIE 2 : Implémentation du plugin
 * ====================================================================
 *
 * LeetSpeakProcessor — transforme le texte en « l33t sp34k ».
 *
 * Layout mémoire de LeetSpeakProcessor :
 *   +0x00–0x17  hérité de Processor  (24 octets)
 *     → +0x10   enabled_  (bool)
 *   +0x11       aggressive_          (1 octet, bool — après enabled_ sans padding)
 *   +0x12       (padding)            (6 octets)
 *   +0x18       chars_converted_     (8 octets, size_t)
 *   Total : 0x20 = 32 octets
 *
 * Priorité choisie : 40
 *   → S'insère entre Rot13Processor (30) et XorCipherProcessor (50)
 *   → Vérifié par l'ordre d'apparition dans la sortie du pipeline
 * ==================================================================== */

class LeetSpeakProcessor : public Processor {
public:
    LeetSpeakProcessor(uint32_t id)
        : Processor(id, 40), aggressive_(false), chars_converted_(0) {}

    ~LeetSpeakProcessor() override {
        fprintf(stderr, "[LeetSpeak #%u] destroyed\n", id_);
    }

    const char* name() const override {
        return "LeetSpeakProcessor";
    }

    bool configure(const char* key, const char* value) override {
        if (strcmp(key, "aggressive") == 0) {
            aggressive_ = (strcmp(value, "true") == 0);
            return true;
        }
        return false;
    }

    int process(const char* input, size_t in_len,
                char* output, size_t out_cap) override
    {
        if (!enabled_ || !input || !output) return -1;

        size_t n = (in_len < out_cap - 1) ? in_len : out_cap - 1;

        for (size_t i = 0; i < n; i++) {
            output[i] = to_leet(input[i]);
        }
        output[n] = '\0';
        chars_converted_ += n;
        return (int)n;
    }

    const char* status() const override {
        static char buf[128];
        snprintf(buf, sizeof(buf),
                 "[LeetSpeak #%u] converted=%zu aggressive=%s",
                 id_, chars_converted_, aggressive_ ? "yes" : "no");
        return buf;
    }

private:
    bool   aggressive_;
    size_t chars_converted_;

    char to_leet(char c) const {
        switch (c) {
            case 'A': case 'a': return '4';
            case 'E': case 'e': return '3';
            case 'I': case 'i': return '1';
            case 'O': case 'o': return '0';
            case 'S': case 's': return '5';
            case 'T': case 't': return '7';
            default:
                if (aggressive_) {
                    switch (c) {
                        case 'B': case 'b': return '8';
                        case 'G': case 'g': return '9';
                        case 'L': case 'l': return '1';
                        default: return c;
                    }
                }
                return c;
        }
    }
};


/* ====================================================================
 * PARTIE 3 : Fonctions factory (extern "C")
 * ====================================================================
 *
 * Ces deux fonctions constituent le contrat entre l'hôte et le plugin.
 *
 * Identifiées par :
 *   $ strings oop_O2_strip | grep -E 'create_processor|destroy_processor'
 *   $ ltrace -e dlsym ./oop_O2_strip -p ./plugins "test"
 *
 * Le extern "C" est indispensable — sans lui, les symboles sont manglés
 * (ex: _Z16create_processorj) et dlsym ne les trouve pas.
 * ==================================================================== */

extern "C" {

Processor* create_processor(uint32_t id) {
    fprintf(stderr, "[plugin_gamma] creating LeetSpeakProcessor id=%u\n", id);
    return new LeetSpeakProcessor(id);
}

void destroy_processor(Processor* p) {
    fprintf(stderr, "[plugin_gamma] destroying processor\n");
    delete p;
}

} /* extern "C" */


#else /* CHECK_LAYOUT — Programme de vérification du layout */


/* ====================================================================
 * PARTIE 4 : Vérification du layout mémoire
 * ====================================================================
 *
 * Compiler séparément :
 *   g++ -std=c++17 -O2 -DCHECK_LAYOUT \
 *       -o check_layout ch22-checkpoint-plugin.cpp
 *
 * Exécuter :
 *   ./check_layout
 *
 * Sortie attendue (doit correspondre aux offsets observés dans le binaire) :
 *   sizeof(Processor)          = 24   (0x18)
 *   sizeof(LeetSpeakProcessor) = 32   (0x20)
 *   offset id_                 = 8    (0x08)
 *   offset priority_           = 12   (0x0C)
 *   offset enabled_            = 16   (0x10)
 * ==================================================================== */

#include <cstddef>
#include <cstdint>
#include <cstdio>

class Processor {
public:
    Processor(uint32_t id, int priority)
        : id_(id), priority_(priority), enabled_(true) {}
    virtual ~Processor() {}
    virtual const char* name() const = 0;
    virtual bool configure(const char* key, const char* value) = 0;
    virtual int process(const char* input, size_t in_len,
                        char* output, size_t out_cap) = 0;
    virtual const char* status() const = 0;

    /* Rendus publics uniquement pour offsetof() dans ce test */
    uint32_t id_;
    int      priority_;
    bool     enabled_;
};

class LeetSpeakProcessor : public Processor {
public:
    LeetSpeakProcessor() : Processor(0, 0), aggressive_(false), cc_(0) {}
    const char* name() const override { return ""; }
    bool configure(const char*, const char*) override { return false; }
    int process(const char*, size_t, char*, size_t) override { return 0; }
    const char* status() const override { return ""; }

    bool   aggressive_;
    size_t cc_;
};

int main() {
    printf("sizeof(Processor)          = %zu  (0x%02zx)\n",
           sizeof(Processor), sizeof(Processor));
    printf("sizeof(LeetSpeakProcessor) = %zu  (0x%02zx)\n",
           sizeof(LeetSpeakProcessor), sizeof(LeetSpeakProcessor));
    printf("offset id_                 = %zu  (0x%02zx)\n",
           offsetof(Processor, id_), offsetof(Processor, id_));
    printf("offset priority_           = %zu  (0x%02zx)\n",
           offsetof(Processor, priority_), offsetof(Processor, priority_));
    printf("offset enabled_            = %zu  (0x%02zx)\n",
           offsetof(Processor, enabled_), offsetof(Processor, enabled_));
    printf("offset aggressive_         = %zu  (0x%02zx)\n",
           offsetof(LeetSpeakProcessor, aggressive_),
           offsetof(LeetSpeakProcessor, aggressive_));
    printf("offset chars_converted_    = %zu  (0x%02zx)\n",
           offsetof(LeetSpeakProcessor, cc_),
           offsetof(LeetSpeakProcessor, cc_));

    /* Vérifications automatiques */
    int errors = 0;

    if (sizeof(Processor) != 24) {
        printf("FAIL: sizeof(Processor) should be 24\n");
        errors++;
    }
    if (offsetof(Processor, id_) != 8) {
        printf("FAIL: id_ should be at offset 8\n");
        errors++;
    }
    if (offsetof(Processor, priority_) != 12) {
        printf("FAIL: priority_ should be at offset 12\n");
        errors++;
    }
    if (offsetof(Processor, enabled_) != 16) {
        printf("FAIL: enabled_ should be at offset 16\n");
        errors++;
    }

    if (errors == 0)
        printf("\nAll layout checks PASSED.\n");
    else
        printf("\n%d layout check(s) FAILED.\n", errors);

    return errors;
}

#endif /* CHECK_LAYOUT */
