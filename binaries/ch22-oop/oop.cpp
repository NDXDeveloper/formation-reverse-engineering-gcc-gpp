/*
 * oop.cpp — Système de traitement de données modulaire (application principale)
 *
 * Formation Reverse Engineering — Chapitre 22
 * Licence MIT — Usage strictement éducatif
 *
 * Architecture :
 *   - Deux processeurs internes (UpperCase, Reverse)
 *   - Chargement dynamique de plugins .so via dlopen/dlsym
 *   - Pipeline : les processeurs s'enchaînent par ordre de priorité
 *   - Toute la logique passe par des pointeurs Processor* (dispatch virtuel)
 *
 * Compilation : voir Makefile (produit oop_O0, oop_O2, oop_O2_strip)
 */

#include "processor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <vector>

/* ====================================================================
 * Classe dérivée : UpperCaseProcessor
 *
 * Convertit tout le texte en majuscules.
 * Supporte une option "skip_digits" pour ignorer les chiffres.
 * ==================================================================== */
class UpperCaseProcessor : public Processor {
public:
    UpperCaseProcessor(uint32_t id)
        : Processor(id, 10), skip_digits_(false) {}

    ~UpperCaseProcessor() override {
        fprintf(stderr, "[UpperCase #%u] destroyed\n", id_);
    }

    const char* name() const override {
        return "UpperCaseProcessor";
    }

    bool configure(const char* key, const char* value) override {
        if (strcmp(key, "skip_digits") == 0) {
            skip_digits_ = (strcmp(value, "true") == 0);
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
            if (skip_digits_ && isdigit((unsigned char)input[i]))
                output[i] = input[i];
            else
                output[i] = toupper((unsigned char)input[i]);
        }
        output[n] = '\0';
        bytes_processed_ += n;
        return (int)n;
    }

    const char* status() const override {
        static char buf[128];
        snprintf(buf, sizeof(buf),
                 "[UpperCase #%u] processed=%zu skip_digits=%s",
                 id_, bytes_processed_, skip_digits_ ? "yes" : "no");
        return buf;
    }

private:
    bool   skip_digits_;
    size_t bytes_processed_ = 0;
};

/* ====================================================================
 * Classe dérivée : ReverseProcessor
 *
 * Inverse l'ordre des caractères du buffer.
 * Supporte une option "word_mode" pour inverser mot par mot.
 * ==================================================================== */
class ReverseProcessor : public Processor {
public:
    ReverseProcessor(uint32_t id)
        : Processor(id, 20), word_mode_(false) {}

    ~ReverseProcessor() override {
        fprintf(stderr, "[Reverse #%u] destroyed\n", id_);
    }

    const char* name() const override {
        return "ReverseProcessor";
    }

    bool configure(const char* key, const char* value) override {
        if (strcmp(key, "word_mode") == 0) {
            word_mode_ = (strcmp(value, "true") == 0);
            return true;
        }
        return false;
    }

    int process(const char* input, size_t in_len,
                char* output, size_t out_cap) override
    {
        if (!enabled_ || !input || !output) return -1;
        size_t n = (in_len < out_cap - 1) ? in_len : out_cap - 1;

        if (word_mode_) {
            /* Inverse chaque mot individuellement */
            memcpy(output, input, n);
            output[n] = '\0';
            char* start = output;
            for (char* p = output; ; p++) {
                if (*p == ' ' || *p == '\0') {
                    reverse_range(start, p);
                    if (*p == '\0') break;
                    start = p + 1;
                }
            }
        } else {
            /* Inverse tout le buffer */
            for (size_t i = 0; i < n; i++)
                output[i] = input[n - 1 - i];
            output[n] = '\0';
        }
        chunks_processed_++;
        return (int)n;
    }

    const char* status() const override {
        static char buf[128];
        snprintf(buf, sizeof(buf),
                 "[Reverse #%u] chunks=%zu word_mode=%s",
                 id_, chunks_processed_, word_mode_ ? "yes" : "no");
        return buf;
    }

private:
    bool   word_mode_;
    size_t chunks_processed_ = 0;

    static void reverse_range(char* begin, char* end) {
        end--;
        while (begin < end) {
            char tmp = *begin;
            *begin = *end;
            *end = tmp;
            begin++;
            end--;
        }
    }
};

/* ====================================================================
 * PluginHandle — Gestion du cycle de vie d'un plugin chargé
 * ==================================================================== */
struct PluginHandle {
    void*          dl_handle;
    create_func_t  create;
    destroy_func_t destroy;
    Processor*     instance;
    char           path[256];
};

/* ====================================================================
 * Pipeline — Orchestre les processeurs par ordre de priorité
 * ==================================================================== */
class Pipeline {
public:
    Pipeline() : next_id_(1) {}

    virtual ~Pipeline() {
        /* Destruction des processeurs internes */
        for (Processor* p : internal_) {
            fprintf(stderr, "[Pipeline] deleting internal: %s\n", p->name());
            delete p;
        }
        /* Destruction des plugins */
        for (PluginHandle& ph : plugins_) {
            fprintf(stderr, "[Pipeline] unloading plugin: %s\n", ph.path);
            if (ph.instance && ph.destroy)
                ph.destroy(ph.instance);
            if (ph.dl_handle)
                dlclose(ph.dl_handle);
        }
    }

    /* Ajoute un processeur interne */
    void add_internal(Processor* p) {
        internal_.push_back(p);
        all_.push_back(p);
    }

    /* Charge un plugin depuis un fichier .so */
    bool load_plugin(const char* path) {
        fprintf(stderr, "[Pipeline] loading plugin: %s\n", path);

        void* handle = dlopen(path, RTLD_NOW);
        if (!handle) {
            fprintf(stderr, "[Pipeline] dlopen error: %s\n", dlerror());
            return false;
        }

        /* Résolution des symboles factory */
        create_func_t  cfn = (create_func_t)dlsym(handle, PLUGIN_CREATE_SYMBOL);
        destroy_func_t dfn = (destroy_func_t)dlsym(handle, PLUGIN_DESTROY_SYMBOL);

        if (!cfn || !dfn) {
            fprintf(stderr, "[Pipeline] missing symbols in %s\n", path);
            dlclose(handle);
            return false;
        }

        /* Instanciation du processeur via la factory */
        Processor* inst = cfn(next_id_++);
        if (!inst) {
            fprintf(stderr, "[Pipeline] create_processor returned NULL\n");
            dlclose(handle);
            return false;
        }

        fprintf(stderr, "[Pipeline] loaded: %s (name=%s, id=%u, priority=%d)\n",
                path, inst->name(), inst->id(), inst->priority());

        PluginHandle ph;
        ph.dl_handle = handle;
        ph.create    = cfn;
        ph.destroy   = dfn;
        ph.instance  = inst;
        strncpy(ph.path, path, sizeof(ph.path) - 1);
        ph.path[sizeof(ph.path) - 1] = '\0';

        plugins_.push_back(ph);
        all_.push_back(inst);
        return true;
    }

    /* Charge tous les .so d'un répertoire */
    int load_plugins_from_dir(const char* dir) {
        DIR* d = opendir(dir);
        if (!d) {
            fprintf(stderr, "[Pipeline] cannot open plugin dir: %s\n", dir);
            return 0;
        }
        int count = 0;
        struct dirent* entry;
        while ((entry = readdir(d)) != NULL) {
            size_t len = strlen(entry->d_name);
            if (len > 3 && strcmp(entry->d_name + len - 3, ".so") == 0) {
                char fullpath[512];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, entry->d_name);
                if (load_plugin(fullpath))
                    count++;
            }
        }
        closedir(d);
        return count;
    }

    /* Exécute le pipeline : enchaîne tous les processeurs triés par priorité */
    int execute(const char* input) {
        if (all_.empty()) {
            fprintf(stderr, "[Pipeline] no processors loaded\n");
            return -1;
        }

        /* Tri par priorité croissante */
        std::sort(all_.begin(), all_.end(),
                  [](const Processor* a, const Processor* b) {
                      return a->priority() < b->priority();
                  });

        char buf_a[4096], buf_b[4096];
        const char* current_in = input;
        size_t current_len = strlen(input);
        char* current_out = buf_a;
        char* alternate   = buf_b;

        printf("=== Pipeline Start ===\n");
        printf("Input: \"%s\"\n\n", input);

        for (Processor* p : all_) {
            if (!p->enabled()) {
                printf("[SKIP] %s (#%u) — disabled\n", p->name(), p->id());
                continue;
            }

            int written = p->process(current_in, current_len,
                                     current_out, sizeof(buf_a));
            if (written < 0) {
                fprintf(stderr, "[ERROR] %s (#%u) returned error\n",
                        p->name(), p->id());
                return -1;
            }

            printf("[STEP] %-25s → \"%s\"\n", p->name(), current_out);

            /* Swap des buffers pour l'étape suivante */
            current_in  = current_out;
            current_len = (size_t)written;
            char* tmp   = current_out;
            current_out = alternate;
            alternate   = tmp;
        }

        printf("\nOutput: \"%s\"\n", current_in);
        printf("=== Pipeline End ===\n\n");

        /* Affichage du statut de chaque processeur */
        printf("--- Status ---\n");
        for (const Processor* p : all_) {
            printf("  %s\n", p->status());
        }
        printf("--------------\n");

        return 0;
    }

private:
    std::vector<Processor*>    internal_;
    std::vector<PluginHandle>  plugins_;
    std::vector<Processor*>    all_;
    uint32_t                   next_id_;
};

/* ====================================================================
 * Point d'entrée
 * ==================================================================== */
static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options] <text>\n"
        "Options:\n"
        "  -p <dir>     Plugin directory (default: ./plugins)\n"
        "  -d           Disable built-in ReverseProcessor\n"
        "  -w           Enable word_mode on ReverseProcessor\n"
        "  -s           Enable skip_digits on UpperCaseProcessor\n"
        "  -h           Show this help\n",
        prog);
}

int main(int argc, char* argv[]) {
    const char* plugin_dir     = "./plugins";
    bool disable_reverse       = false;
    bool word_mode             = false;
    bool skip_digits           = false;

    /* Parsing des arguments (manuel, pour garder le binaire simple) */
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            plugin_dir = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0) {
            disable_reverse = true;
        } else if (strcmp(argv[i], "-w") == 0) {
            word_mode = true;
        } else if (strcmp(argv[i], "-s") == 0) {
            skip_digits = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        i++;
    }

    if (i >= argc) {
        fprintf(stderr, "Error: no input text provided\n");
        usage(argv[0]);
        return 1;
    }
    const char* input_text = argv[i];

    /* Construction du pipeline */
    Pipeline pipeline;

    /* Processeurs internes */
    UpperCaseProcessor* upper = new UpperCaseProcessor(0);
    if (skip_digits)
        upper->configure("skip_digits", "true");
    pipeline.add_internal(upper);

    ReverseProcessor* rev = new ReverseProcessor(0);
    if (word_mode)
        rev->configure("word_mode", "true");
    if (disable_reverse)
        rev->set_enabled(false);
    pipeline.add_internal(rev);

    /* Chargement des plugins */
    int loaded = pipeline.load_plugins_from_dir(plugin_dir);
    fprintf(stderr, "[main] %d plugin(s) loaded from %s\n", loaded, plugin_dir);

    /* Exécution */
    int ret = pipeline.execute(input_text);
    return (ret == 0) ? 0 : 1;
}
