🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 22.4 — Patcher un comportement via `LD_PRELOAD`

> 🛠️ **Outils utilisés** : `LD_PRELOAD`, GCC/G++, `nm`, `ltrace`, GDB, `readelf`  
> 📦 **Binaires** : `oop_O0`, `oop_O2`, `plugins/plugin_alpha.so`  
> 📚 **Prérequis** : Sections 22.1–22.3, Chapitre 2.9 (PLT/GOT), Chapitre 5.4 (`ldd`), Chapitre 13 (Frida — pour comparaison)

---

## Introduction

Jusqu'ici, nous avons analysé le binaire `ch22-oop` sans le modifier. Nous avons reconstruit la hiérarchie de classes, compris le mécanisme de plugins, et décodé le dispatch virtuel. Mais le reverse engineering ne se limite pas à l'observation — il inclut aussi l'**expérimentation active** : modifier un comportement pour valider une hypothèse, contourner une vérification, ou explorer un chemin d'exécution autrement inaccessible.

La technique `LD_PRELOAD` permet exactement cela. En injectant une bibliothèque partagée personnalisée **avant** les autres dans l'ordre de résolution du linker dynamique, vous pouvez **remplacer n'importe quelle fonction** exportée par une bibliothèque, y compris la libc, sans toucher au binaire cible. Le programme original ne sait pas qu'il utilise votre version de la fonction — le remplacement est transparent.

Cette technique se situe à la frontière entre l'analyse dynamique (chapitre 13 — Frida) et le patching binaire (chapitre 21.6). Elle est plus légère que Frida (pas de runtime JS à injecter) et plus réversible que le patching (le binaire n'est jamais modifié). C'est un outil du quotidien pour le reverse engineer, le développeur système et l'analyste de sécurité.

---

## Comment fonctionne `LD_PRELOAD`

### Le mécanisme de résolution des symboles

Quand le linker dynamique (`ld.so`) charge un programme, il résout les symboles importés (comme `printf`, `malloc`, `dlopen`) en cherchant dans les bibliothèques partagées dans un ordre précis :

1. **`LD_PRELOAD`** — les bibliothèques listées dans cette variable d'environnement, chargées en premier.  
2. **Les dépendances directes** du binaire (listées dans les entrées `DT_NEEDED` de l'en-tête ELF dynamique).  
3. **Les dépendances des dépendances** (résolution récursive).

La règle fondamentale est : **le premier symbole trouvé gagne**. Si votre bibliothèque `LD_PRELOAD` exporte un symbole `strcmp`, c'est votre version qui sera utilisée par le programme, pas celle de la libc. La libc est toujours chargée, mais son `strcmp` est « masqué » par le vôtre pour tous les appels qui passent par la PLT.

### Ce que `LD_PRELOAD` peut intercepter

- **Les fonctions de la libc** : `strcmp`, `strlen`, `malloc`, `free`, `open`, `read`, `write`, `printf`, `time`, `rand`…  
- **Les fonctions de `libdl`** : `dlopen`, `dlsym`, `dlclose`.  
- **Les fonctions de `libstdc++`** : `operator new`, `operator delete`, `__cxa_throw`…  
- **Les fonctions C exportées par le binaire lui-même** (si compilé avec `-rdynamic`).  
- **Les symboles `extern "C"` des plugins** : `create_processor`, `destroy_processor`.

### Ce que `LD_PRELOAD` ne peut pas intercepter

- **Les appels internes** qui ne passent pas par la PLT. Si une fonction appelle une autre fonction du même binaire (ou de la même `.so`) et que l'appel est résolu statiquement au link time, `LD_PRELOAD` ne peut pas l'intercepter. C'est souvent le cas en `-O2` avec les fonctions `static` ou celles résolues via la GOT locale.  
- **Les appels système directs** (`syscall`). Un programme qui appelle `write` via la libc peut être intercepté ; un programme qui exécute directement `syscall(1, fd, buf, len)` ne peut pas l'être.  
- **Les méthodes C++ manglées** — en théorie, c'est possible (le symbol mangling produit un nom exportable), mais en pratique, les méthodes C++ internes ne sont presque jamais résolues via la PLT. Elles sont appelées directement, sans passer par le linker dynamique.  
- **Les appels virtuels intra-module** — le dispatch passe par la vtable en mémoire, pas par le linker. `LD_PRELOAD` ne peut pas remplacer une entrée de vtable.

> 💡 **Résumé** : `LD_PRELOAD` intercepte ce qui passe par la PLT/GOT — c'est-à-dire les symboles résolus dynamiquement par `ld.so`. Pour intercepter du code interne (y compris les méthodes virtuelles), il faut des techniques complémentaires : Frida (hooking en mémoire), patching binaire, ou manipulation de la vtable à l'exécution.

---

## Cas pratique 1 — Intercepter `strcmp` pour tracer les comparaisons

### Scénario

Vous analysez un binaire qui vérifie un mot de passe ou une licence via `strcmp`. Plutôt que de poser des breakpoints manuels dans GDB, vous voulez **logger automatiquement** chaque appel à `strcmp` avec ses deux arguments.

Ce scénario est directement applicable à notre binaire `oop_O0` : la méthode `configure()` de chaque processeur utilise `strcmp` pour comparer les noms de clés (comme `"skip_digits"`, `"word_mode"`, `"half_rot"`). Intercepter `strcmp` révèle instantanément les options de configuration acceptées.

### La bibliothèque d'interception

```c
/* preload_strcmp.c
 *
 * Compile :
 *   gcc -shared -fPIC -o preload_strcmp.so preload_strcmp.c -ldl
 *
 * Usage :
 *   LD_PRELOAD=./preload_strcmp.so ./oop_O0 -s "Hello World"
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* Pointeur vers le strcmp original (résolu au premier appel) */
static int (*real_strcmp)(const char*, const char*) = NULL;

/* Notre version de strcmp — appelée à la place de l'originale */
int strcmp(const char* s1, const char* s2) {
    /* Résoudre paresseusement le vrai strcmp */
    if (!real_strcmp) {
        real_strcmp = (int (*)(const char*, const char*))dlsym(RTLD_NEXT, "strcmp");
    }

    /* Logger l'appel */
    fprintf(stderr, "[PRELOAD] strcmp(\"%s\", \"%s\")", s1, s2);

    /* Appeler le vrai strcmp */
    int result = real_strcmp(s1, s2);

    fprintf(stderr, " → %d\n", result);
    return result;
}
```

### Points clés du code

**`RTLD_NEXT`** — C'est la constante magique qui fait tout fonctionner. Passée à `dlsym`, elle signifie « cherche ce symbole dans la **prochaine** bibliothèque dans l'ordre de résolution, en sautant la bibliothèque courante ». C'est ainsi que notre `strcmp` peut appeler le vrai `strcmp` de la libc sans créer de récursion infinie.

**`_GNU_SOURCE`** — Nécessaire pour que `RTLD_NEXT` soit défini dans `<dlfcn.h>`. Sans cette macro, la compilation échoue.

**Résolution paresseuse** — Le pointeur `real_strcmp` est initialisé au premier appel via `dlsym(RTLD_NEXT, "strcmp")`. Cela évite les problèmes d'ordre d'initialisation au chargement de la bibliothèque.

**`fprintf(stderr, ...)`** — On écrit sur `stderr` et non `stdout` pour ne pas polluer la sortie standard du programme. Si le programme redirige `stdout` vers un fichier ou un pipe, nos logs restent visibles dans le terminal.

### Compilation et exécution

```bash
$ gcc -shared -fPIC -o preload_strcmp.so preload_strcmp.c -ldl

$ LD_PRELOAD=./preload_strcmp.so ./oop_O0 -s "Hello World"
```

Sortie sur `stderr` :

```
[PRELOAD] strcmp("skip_digits", "skip_digits") → 0
[PRELOAD] strcmp("skip_digits", "word_mode") → -4
[PRELOAD] strcmp("word_mode", "word_mode") → 0
...
```

Le résultat `→ 0` indique une correspondance. Vous voyez immédiatement quelles clés de configuration sont comparées, et par déduction, quelles options chaque processeur supporte.

---

## Cas pratique 2 — Intercepter `dlopen` pour contrôler le chargement de plugins

### Scénario

Vous voulez comprendre comment le programme réagit si un plugin est absent, corrompu, ou remplacé par un autre. Plutôt que de manipuler le système de fichiers, vous interceptez `dlopen` pour filtrer, rediriger ou bloquer le chargement.

### La bibliothèque d'interception

```c
/* preload_dlopen.c
 *
 * Compile :
 *   gcc -shared -fPIC -o preload_dlopen.so preload_dlopen.c -ldl
 *
 * Usage :
 *   LD_PRELOAD=./preload_dlopen.so ./oop_O0 -p ./plugins "Hello"
 *
 * Contrôle via variables d'environnement :
 *   BLOCK_PLUGIN=plugin_beta.so   → bloque le chargement de ce plugin
 *   REDIRECT_PLUGIN=alpha:beta    → redirige alpha vers beta
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* (*real_dlopen)(const char*, int) = NULL;

static void init_real_dlopen(void) {
    if (!real_dlopen) {
        real_dlopen = (void* (*)(const char*, int))dlsym(RTLD_NEXT, "dlopen");
    }
}

void* dlopen(const char* filename, int flags) {
    init_real_dlopen();

    fprintf(stderr, "[PRELOAD:dlopen] request: \"%s\" flags=%d\n",
            filename ? filename : "(null)", flags);

    if (!filename) {
        return real_dlopen(filename, flags);
    }

    /* ── Blocage conditionnel ── */
    const char* blocked = getenv("BLOCK_PLUGIN");
    if (blocked && strstr(filename, blocked)) {
        fprintf(stderr, "[PRELOAD:dlopen] BLOCKED: %s\n", filename);
        return NULL;  /* Simule un échec de chargement */
    }

    /* ── Redirection conditionnelle ── */
    const char* redirect = getenv("REDIRECT_PLUGIN");
    if (redirect) {
        /* Format: "pattern_source:pattern_dest" */
        char buf[256];
        strncpy(buf, redirect, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* sep = strchr(buf, ':');
        if (sep) {
            *sep = '\0';
            const char* from = buf;
            const char* to = sep + 1;
            if (strstr(filename, from)) {
                /* Construire le nouveau chemin */
                static char new_path[512];
                strncpy(new_path, filename, sizeof(new_path) - 1);
                char* pos = strstr(new_path, from);
                if (pos) {
                    /* Remplacement simple (même longueur approximative) */
                    size_t prefix_len = (size_t)(pos - new_path);
                    snprintf(new_path + prefix_len,
                             sizeof(new_path) - prefix_len,
                             "%s%s", to, pos + strlen(from));
                    fprintf(stderr, "[PRELOAD:dlopen] REDIRECT: %s → %s\n",
                            filename, new_path);
                    filename = new_path;
                }
            }
        }
    }

    /* ── Appel réel ── */
    void* handle = real_dlopen(filename, flags);
    fprintf(stderr, "[PRELOAD:dlopen] result: %p %s\n",
            handle, handle ? "OK" : dlerror());
    return handle;
}
```

### Scénarios d'utilisation

**Bloquer un plugin** pour observer le comportement du pipeline sans lui :

```bash
$ BLOCK_PLUGIN=plugin_beta.so \
  LD_PRELOAD=./preload_dlopen.so ./oop_O0 -p ./plugins "Hello"
```

```
[PRELOAD:dlopen] request: "./plugins/plugin_alpha.so" flags=2
[PRELOAD:dlopen] result: 0x5555557a4000 OK
[PRELOAD:dlopen] request: "./plugins/plugin_beta.so" flags=2
[PRELOAD:dlopen] BLOCKED: ./plugins/plugin_beta.so
[Pipeline] dlopen error: (null)
```

Le plugin beta est bloqué. Vous pouvez observer comment le pipeline gère l'absence d'un plugin — est-ce qu'il crashe, est-ce qu'il continue avec les processeurs restants, est-ce qu'il affiche un message d'erreur explicite ?

**Rediriger un plugin** pour voir ce qui se passe si le même plugin est chargé deux fois sous des noms différents :

```bash
$ REDIRECT_PLUGIN=beta:alpha \
  LD_PRELOAD=./preload_dlopen.so ./oop_O0 -p ./plugins "Hello"
```

```
[PRELOAD:dlopen] request: "./plugins/plugin_alpha.so" flags=2
[PRELOAD:dlopen] result: 0x5555557a4000 OK
[PRELOAD:dlopen] request: "./plugins/plugin_beta.so" flags=2
[PRELOAD:dlopen] REDIRECT: ./plugins/plugin_beta.so → ./plugins/plugin_alpha.so
[PRELOAD:dlopen] result: 0x5555557a4000 OK
```

Le handle retourné est le même (`0x5555557a4000`) car `dlopen` retourne le handle existant si la bibliothèque est déjà chargée. Le pipeline aura deux instances de `Rot13Processor` au lieu d'une instance de chaque. Ce genre d'expérimentation est précieux pour comprendre comment le programme gère les cas limites.

---

## Cas pratique 3 — Intercepter `time` et `rand` pour rendre l'exécution déterministe

### Scénario

De nombreux binaires utilisent `time()` comme seed pour `srand()`, ou appellent `rand()` pour des décisions internes (choix de serveur C2, délai avant exécution, génération de clé…). En interceptant ces fonctions, vous rendez l'exécution parfaitement reproductible — indispensable pour comparer des runs dans GDB ou pour l'analyse de malware.

### La bibliothèque

```c
/* preload_deterministic.c
 *
 * Compile :
 *   gcc -shared -fPIC -o preload_deterministic.so preload_deterministic.c -ldl
 *
 * Usage :
 *   LD_PRELOAD=./preload_deterministic.so ./target
 *   FAKE_TIME=1700000000 LD_PRELOAD=./preload_deterministic.so ./target
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ── time() figé ── */
time_t time(time_t* tloc) {
    const char* env = getenv("FAKE_TIME");
    time_t fake = env ? (time_t)atol(env) : 1700000000; /* 14 nov 2023 */

    if (tloc) *tloc = fake;
    fprintf(stderr, "[PRELOAD] time() → %ld (fixed)\n", (long)fake);
    return fake;
}

/* ── rand() déterministe ── */
static unsigned int call_count = 0;

int rand(void) {
    /* Séquence déterministe simple : toujours les mêmes valeurs */
    unsigned int val = (call_count * 1103515245 + 12345) & 0x7fffffff;
    call_count++;
    fprintf(stderr, "[PRELOAD] rand() → %u (call #%u)\n", val, call_count);
    return (int)val;
}

/* ── srand() neutralisé ── */
void srand(unsigned int seed) {
    fprintf(stderr, "[PRELOAD] srand(%u) → ignored\n", seed);
    /* Ne fait rien — on veut garder notre séquence déterministe */
}
```

Cette technique est directement applicable au chapitre 27 (analyse de ransomware) et au chapitre 28 (analyse de dropper) où le comportement dépend souvent du temps et de l'aléatoire.

---

## Cas pratique 4 — Intercepter `operator new` pour tracer les allocations d'objets C++

### Scénario

Vous voulez savoir **quels objets sont créés**, **quand**, et **de quelle taille**. En C++, toute allocation dynamique d'objet passe par `operator new` (sauf surcharge de classe). En l'interceptant, vous obtenez un journal de toutes les constructions d'objets.

### La bibliothèque

```cpp
/* preload_new.cpp — ATTENTION : compiler en C++
 *
 * Compile :
 *   g++ -shared -fPIC -o preload_new.so preload_new.cpp -ldl
 *
 * Usage :
 *   LD_PRELOAD=./preload_new.so ./oop_O0 -p ./plugins "Hello"
 */

#define _GNU_SOURCE
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <new>

/* Résoudre l'operator new original */
static void* (*real_new)(size_t) = nullptr;

static void init_real_new() {
    if (!real_new) {
        real_new = (void* (*)(size_t))dlsym(RTLD_NEXT, "_Znwm");
        /* _Znwm = mangled name of operator new(unsigned long) sur x86-64 */
    }
}

/* Remplacer operator new(size_t) */
void* operator new(size_t size) {
    init_real_new();

    void* ptr = real_new(size);

    fprintf(stderr, "[PRELOAD:new] size=%-4zu → %p", size, ptr);

    /* Heuristique : les tailles connues de nos classes */
    switch (size) {
        case 24:  fprintf(stderr, "  (Processor-sized)");       break;
        case 40:  fprintf(stderr, "  (UpperCase/Reverse-sized)"); break;
        case 48:  fprintf(stderr, "  (Rot13-sized)");           break;
        case 80:  fprintf(stderr, "  (XorCipher-sized)");       break;
    }

    fprintf(stderr, "\n");
    return ptr;
}

/* Remplacer operator delete(void*) */
void operator delete(void* ptr) noexcept {
    fprintf(stderr, "[PRELOAD:delete] %p\n", ptr);

    void (*real_delete)(void*) =
        (void (*)(void*))dlsym(RTLD_NEXT, "_ZdlPv");
    if (real_delete) real_delete(ptr);
}

/* Variante avec taille (C++14) */
void operator delete(void* ptr, size_t size) noexcept {
    fprintf(stderr, "[PRELOAD:delete] %p size=%zu\n", ptr, size);

    void (*real_delete)(void*, size_t) =
        (void (*)(void*, size_t))dlsym(RTLD_NEXT, "_ZdlPvm");
    if (real_delete)
        real_delete(ptr, size);
    else {
        void (*fallback)(void*) =
            (void (*)(void*))dlsym(RTLD_NEXT, "_ZdlPv");
        if (fallback) fallback(ptr);
    }
}
```

### Sortie

```bash
$ LD_PRELOAD=./preload_new.so ./oop_O0 -p ./plugins "Hello RE"
```

```
[PRELOAD:new] size=40   → 0x5555558070f0  (UpperCase/Reverse-sized)
[PRELOAD:new] size=40   → 0x555555807130  (UpperCase/Reverse-sized)
[PRELOAD:new] size=48   → 0x555555808010  (Rot13-sized)
[PRELOAD:new] size=80   → 0x555555808050  (XorCipher-sized)
...
[PRELOAD:delete] 0x555555808050 size=80
[PRELOAD:delete] 0x555555808010 size=48
[PRELOAD:delete] 0x555555807130 size=40
[PRELOAD:delete] 0x5555558070f0 size=40
```

Vous voyez l'ordre de création (UpperCase, Reverse, Rot13, XorCipher) et de destruction (ordre inverse — le `Pipeline` détruit les plugins avant les processeurs internes). Les tailles d'allocation confirment vos estimations de `sizeof` pour chaque classe, reconstruites en section 22.1.

> 💡 **Le symbole manglé `_Znwm`** : c'est le name mangling Itanium pour `operator new(unsigned long)`. Sur les plateformes 32 bits, c'est `_Znwj` (`j` = `unsigned int`). Vous pouvez retrouver ces noms avec `echo "_Znwm" | c++filt` → `operator new(unsigned long)`.

---

## Combiner plusieurs interceptions dans une seule bibliothèque

En pratique, vous regrouperez souvent plusieurs interceptions dans une seule bibliothèque pour obtenir un tableau complet du comportement :

```c
/* preload_full.c — Interception combinée pour le RE de ch22-oop
 *
 * Compile :
 *   gcc -shared -fPIC -o preload_full.so preload_full.c -ldl
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* ── strcmp : tracer les comparaisons de configuration ── */
int strcmp(const char* s1, const char* s2) {
    static int (*real)(const char*, const char*) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "strcmp");
    int r = real(s1, s2);
    if (r == 0)  /* Ne logger que les matches pour réduire le bruit */
        fprintf(stderr, "[CMP] \"%s\" == \"%s\"\n", s1, s2);
    return r;
}

/* ── dlopen : tracer les plugins chargés ── */
void* dlopen(const char* path, int flags) {
    static void* (*real)(const char*, int) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "dlopen");
    fprintf(stderr, "[DL] dlopen(\"%s\")\n", path ? path : "NULL");
    void* h = real(path, flags);
    fprintf(stderr, "[DL] → %p\n", h);
    return h;
}

/* ── dlsym : tracer les résolutions de symboles ── */
void* dlsym(void* handle, const char* symbol) {
    /* Attention : on ne peut pas utiliser dlsym(RTLD_NEXT, "dlsym")
     * depuis dlsym elle-même — récursion infinie.
     * Solution : utiliser __libc_dlsym ou résoudre au chargement. */
    static void* (*real)(void*, const char*) = NULL;
    if (!real) {
        /* Accès direct via l'ABI GNU interne */
        void* libdl = __libc_dlopen_mode("libdl.so.2", 0x80000002);
        if (libdl)
            real = __libc_dlsym(libdl, "dlsym");
    }
    if (!real) {
        fprintf(stderr, "[DL] FATAL: cannot resolve real dlsym\n");
        return NULL;
    }

    fprintf(stderr, "[DL] dlsym(%p, \"%s\")\n", handle, symbol);
    void* r = real(handle, symbol);
    fprintf(stderr, "[DL] → %p\n", r);
    return r;
}
```

> ⚠️ **Le piège de `dlsym` dans `dlsym`** : intercepter `dlsym` est délicat car `RTLD_NEXT` passe lui-même par `dlsym`. Le code ci-dessus contourne le problème avec `__libc_dlsym`, une fonction interne de la glibc. Cette approche fonctionne sur Linux/glibc mais n'est pas portable. Une alternative plus robuste est de résoudre le pointeur dans une fonction constructeur `__attribute__((constructor))` au chargement de la bibliothèque, avant tout appel.

---

## `LD_PRELOAD` vs Frida vs patching binaire

Ces trois techniques modifient le comportement d'un programme sans recompilation. Leurs domaines d'application se recoupent mais ne sont pas identiques.

**`LD_PRELOAD`** fonctionne au niveau du linker dynamique. Il intercepte les symboles résolus via PLT/GOT — principalement les fonctions de bibliothèques partagées. C'est la technique la plus simple à mettre en œuvre (un fichier `.c`, une ligne de compilation, une variable d'environnement) et la plus légère en overhead. Elle ne nécessite aucun outil spécial, fonctionne sur n'importe quel Linux, et ne laisse aucune trace dans le binaire.

**Frida** fonctionne au niveau de la mémoire du processus. Il peut hooker n'importe quelle adresse — y compris les fonctions internes, les méthodes virtuelles, les entrées de vtable, et les instructions individuelles. C'est plus puissant que `LD_PRELOAD` mais nécessite le runtime Frida, un script JavaScript, et un overhead plus important. Frida est le bon choix quand vous devez intercepter du code interne qui ne passe pas par la PLT.

**Le patching binaire** modifie le fichier ELF lui-même : inversion d'un saut conditionnel, remplacement d'un `call`, NOP-ification d'une vérification. C'est permanent (le fichier est modifié) et précis (vous changez exactement les octets voulus), mais fragile (un offset incorrect corrompt le binaire) et non réversible sans backup. Le patching est le bon choix quand vous devez produire un binaire modifié redistribuable.

| Critère | `LD_PRELOAD` | Frida | Patching binaire |  
|---------|-------------|-------|-----------------|  
| Portée | Symboles PLT/GOT | Toute adresse mémoire | Tout octet du fichier |  
| Complexité | Faible (C + gcc) | Moyenne (JS + runtime) | Variable (hex editor → scripts) |  
| Réversibilité | Totale (variable d'env.) | Totale (détacher l'agent) | Nécessite backup |  
| Overhead | Quasi nul | Modéré | Aucun |  
| Méthodes virtuelles C++ | Non (pas dans la PLT) | Oui (hook par adresse) | Oui (modifier la vtable ou le call) |  
| Persistance | Par session (env. var.) | Par session (agent) | Permanente (fichier modifié) |  
| Binaire statique | Non | Oui | Oui |

En RE quotidien, la bonne pratique est de commencer par `LD_PRELOAD` pour les interceptions simples (libc, libdl), de passer à Frida quand il faut hooker du code interne ou des vtables, et de recourir au patching uniquement quand un binaire modifié est nécessaire.

---

## Protections et contre-mesures

`LD_PRELOAD` est une technique puissante, mais certains binaires cherchent à s'en protéger.

### Binaires setuid/setgid

Le linker dynamique **ignore** `LD_PRELOAD` pour les binaires setuid ou setgid. C'est une protection de sécurité du noyau Linux : permettre à un utilisateur non-root d'injecter du code dans un binaire privilégié serait une élévation de privilèges triviale.

```bash
$ ls -l /usr/bin/passwd
-rwsr-xr-x 1 root root 68208 ... /usr/bin/passwd
                                    ↑ bit setuid
$ LD_PRELOAD=./preload_strcmp.so /usr/bin/passwd
# → LD_PRELOAD est silencieusement ignoré
```

### Détection par le programme

Un programme peut détecter la présence de `LD_PRELOAD` de plusieurs manières :

- **Lire la variable d'environnement** : `getenv("LD_PRELOAD")`. Trivial à contourner en interceptant `getenv` elle-même.  
- **Lire `/proc/self/environ`** : accès direct au bloc d'environnement du processus. Plus difficile à contourner avec `LD_PRELOAD` seul.  
- **Lire `/proc/self/maps`** : liste les bibliothèques mappées en mémoire. Votre `.so` preloaded y apparaît avec son chemin complet.  
- **Comparer les adresses de fonctions** : le programme peut vérifier que l'adresse de `strcmp` tombe dans la plage d'adresses attendue de la libc. Si elle tombe ailleurs, une interception est détectée.

### Contournement des détections

Pour la plupart des cas de RE éducatif et de CTF, ces détections ne sont pas présentes. Si vous les rencontrez, Frida offre des mécanismes plus furtifs (injection dans le processus sans variable d'environnement), et le patching binaire peut neutraliser les vérifications directement dans le code.

### Full RELRO

Un binaire compilé avec Full RELRO (`-Wl,-z,relro,-z,now`) résout **tous** les symboles au chargement et marque la GOT en lecture seule. Cela n'empêche pas `LD_PRELOAD` de fonctionner (l'interposition se fait avant l'écriture de la GOT), mais empêche la modification de la GOT après le chargement — une technique complémentaire parfois combinée avec `LD_PRELOAD`.

---

## Écrire une bibliothèque `LD_PRELOAD` proprement

En vous basant sur les exemples de cette section, voici les bonnes pratiques à suivre systématiquement.

**Toujours résoudre la fonction originale via `RTLD_NEXT`.** Ne réimplémentez jamais la fonction vous-même (sauf pour la bloquer intentionnellement). Appelez toujours l'originale après votre instrumentation pour que le programme se comporte normalement.

**Logger sur `stderr`, pas `stdout`.** La sortie standard est souvent redirigée ou parsée par le programme. `stderr` reste disponible pour vos messages de diagnostic.

**Gérer la résolution paresseuse.** Initialisez les pointeurs vers les fonctions originales au premier appel (ou dans un `__attribute__((constructor))`), pas en global. L'ordre d'initialisation des bibliothèques au chargement n'est pas garanti.

**Compiler avec `-fPIC` et `-shared`.** C'est obligatoire pour produire un shared object compatible avec `LD_PRELOAD`. Oublier `-fPIC` produit des erreurs de relocation au chargement.

**Utiliser `__attribute__((constructor))` pour le setup initial.** Si vous avez besoin d'ouvrir un fichier de log ou d'initialiser un état global, utilisez une fonction constructeur plutôt que de le faire au premier appel intercepté :

```c
__attribute__((constructor))
static void preload_init(void) {
    fprintf(stderr, "[PRELOAD] Library loaded, PID=%d\n", getpid());
}

__attribute__((destructor))
static void preload_fini(void) {
    fprintf(stderr, "[PRELOAD] Library unloaded\n");
}
```

**Garder la bibliothèque aussi légère que possible.** Chaque appel intercepté ajoute un overhead. Si vous interceptez `malloc`, chaque allocation du programme passe par votre code — y compris celles de `fprintf` dans votre interception. Attention aux récursions infinies (`malloc` → `fprintf` → `malloc` → …). Utilisez `write(2, ...)` au lieu de `fprintf(stderr, ...)` dans les interceptions de fonctions d'allocation mémoire.

---

## Résumé

`LD_PRELOAD` est un outil de RE à la fois simple et puissant. En injectant une bibliothèque partagée avant les autres dans l'ordre de résolution du linker, vous pouvez intercepter, logger, modifier ou bloquer n'importe quel appel de bibliothèque sans toucher au binaire cible.

Dans le contexte de notre binaire `ch22-oop`, cette technique nous a permis de tracer les comparaisons de configuration (`strcmp`), de contrôler le chargement des plugins (`dlopen`), et de surveiller les allocations d'objets C++ (`operator new`). Combinée avec les techniques des sections précédentes — analyse des vtables, compréhension du dispatch virtuel, traçage des plugins — elle complète votre boîte à outils pour le reverse engineering d'applications C++ orientées objet.

Au checkpoint de ce chapitre, vous mettrez en pratique l'ensemble de ces connaissances en écrivant un plugin `.so` compatible qui s'intègre dans l'application sans les sources — la boucle est bouclée : du reverse engineering à la création de code interopérable.

---


⏭️ [🎯 Checkpoint : écrire un plugin `.so` compatible qui s'intègre dans l'application sans les sources](/22-oop/checkpoint.md)
