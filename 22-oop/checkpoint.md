🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Écrire un plugin `.so` compatible qui s'intègre dans l'application sans les sources

> **Objectif** : valider l'ensemble des compétences du chapitre 22 en produisant un plugin fonctionnel pour `oop_O2_strip` — le binaire optimisé et strippé, sans accès aux fichiers sources.  
> **Livrable** : un fichier `plugin_gamma.so` qui est découvert, chargé et exécuté par le pipeline au même titre que `plugin_alpha.so` et `plugin_beta.so`.  
> **Temps estimé** : 45–90 minutes selon le niveau de confort avec les outils.

---

## Contexte

Vous travaillez uniquement à partir du binaire `oop_O2_strip` et des deux plugins compilés (`plugin_alpha.so`, `plugin_beta.so`). Vous n'avez pas accès au code source, ni au header `processor.h`. Votre mission est de reconstruire suffisamment d'informations sur l'interface `Processor` pour écrire un plugin tiers qui s'intègre dans le pipeline sans erreur.

Ce checkpoint mobilise les quatre sections du chapitre :

- **22.1** — Reconstruire la hiérarchie de classes et le layout mémoire de `Processor`.  
- **22.2** — Identifier le contrat de plugin (`dlopen`/`dlsym`, symboles factory, convention d'instanciation).  
- **22.3** — Comprendre le dispatch virtuel pour implémenter les méthodes virtuelles dans le bon ordre.  
- **22.4** — Utiliser `LD_PRELOAD` ou d'autres techniques dynamiques pour valider les hypothèses en cours de route.

---

## Phase 1 — Reconstituer le contrat d'interface du plugin

La première étape consiste à répondre à trois questions sans ouvrir le moindre éditeur de code :

1. **Quels symboles le binaire hôte attend-il d'un plugin ?**  
2. **Quelle est la signature de chaque symbole factory ?**  
3. **Quelle interface l'objet retourné doit-il implémenter ?**

### 1.1 — Identifier les symboles factory

Les chaînes passées à `dlsym` sont stockées en clair dans `.rodata`. Même sur un binaire strippé, elles sont accessibles :

```bash
$ strings oop_O2_strip | grep -iE 'create|destroy|processor|plugin'
```

Vous devriez retrouver les deux noms de symboles : `create_processor` et `destroy_processor`. Confirmez avec `ltrace` :

```bash
$ ltrace -e dlsym ./oop_O2_strip -p ./plugins "test" 2>&1 | grep dlsym
```

Les appels `dlsym(handle, "create_processor")` et `dlsym(handle, "destroy_processor")` confirment les noms exacts.

### 1.2 — Déterminer les signatures des factory

Analysez un plugin existant pour retrouver les prototypes. `plugin_alpha.so` a ses symboles :

```bash
$ nm -CD plugins/plugin_alpha.so | grep -E 'create|destroy'
0000000000001200 T create_processor
0000000000001280 T destroy_processor
```

Ouvrez `create_processor` dans Ghidra (ou `objdump -d -M intel`) et observez :

- **L'argument** : un seul paramètre arrive dans `rdi` (convention System V). Le code le passe au constructeur de la classe — c'est un entier (l'ID). Le type est `uint32_t` d'après l'usage (stocké sur 32 bits dans l'objet).  
- **La valeur de retour** : `rax` contient le résultat de `operator new` suivi du constructeur. C'est un pointeur vers un objet — un `Processor*`.

Pour `destroy_processor` : un seul argument dans `rdi` (le pointeur vers l'objet), aucune valeur de retour. Le corps appelle le destructeur puis `operator delete`.

Signatures reconstituées :

```c
extern "C" Processor* create_processor(uint32_t id);  
extern "C" void destroy_processor(Processor* p);  
```

### 1.3 — Reconstruire l'interface `Processor`

C'est l'étape la plus substantielle. Vous devez retrouver la vtable de `Processor` et le layout mémoire des objets.

**Depuis les chaînes RTTI** de `oop_O2_strip` :

```bash
$ strings oop_O2_strip | grep -E '^[0-9]+[A-Z]'
```

Vous identifiez `9Processor`, `19UpperCaseProcessor`, `16ReverseProcessor`. L'entrée `__cxa_pure_virtual` dans la vtable de `Processor` vous confirme les méthodes virtuelles pures.

**Depuis la vtable d'un plugin** — ouvrez la vtable de `Rot13Processor` dans `plugin_alpha.so` (technique de la section 22.1). Comptez les entrées :

| Index | Offset | Méthode (déduite de l'analyse) |  
|-------|--------|-------------------------------|  
| 0 | `+0x00` | Destructeur (complete) |  
| 1 | `+0x08` | Destructeur (deleting) |  
| 2 | `+0x10` | `name()` — retourne `const char*` |  
| 3 | `+0x18` | `configure()` — prend 2 `const char*`, retourne `bool` |  
| 4 | `+0x20` | `process()` — prend `const char*`, `size_t`, `char*`, `size_t`, retourne `int` |  
| 5 | `+0x28` | `status()` — retourne `const char*` |

Confirmez en croisant avec la vtable de `XorCipherProcessor` dans `plugin_beta.so` — même nombre d'entrées, mêmes offsets, mêmes conventions d'appel.

**Depuis le layout mémoire** — examinez les accès `[rdi+offset]` dans les méthodes de chaque plugin et dans les classes internes du binaire hôte. Les offsets communs à toutes les classes vous donnent les champs hérités de `Processor` :

| Offset | Taille | Champ déduit |  
|--------|--------|-------------|  
| `+0x00` | 8 | vptr |  
| `+0x08` | 4 | id (uint32_t) |  
| `+0x0C` | 4 | priority (int) |  
| `+0x10` | 1 | enabled (bool) |  
| `+0x11`–`+0x17` | 7 | padding |

La taille de base est donc 0x18 (24 octets). Chaque classe dérivée ajoute ses propres champs après `enabled_`. Attention : un `bool` en sous-classe peut se placer directement à `+0x11` (après `enabled_` à `+0x10`) sans padding intermédiaire, car les `bool` n'exigent qu'un alignement de 1 octet.

---

## Phase 2 — Écrire le header reconstitué

À partir des informations de la phase 1, rédigez un header minimal qui vous permettra de compiler un plugin compatible :

```cpp
/* processor_reconstructed.h
 *
 * Header reconstitué par reverse engineering de oop_O2_strip
 * et des plugins plugin_alpha.so / plugin_beta.so.
 *
 * Aucun accès au code source original.
 */

#ifndef PROCESSOR_RECONSTRUCTED_H
#define PROCESSOR_RECONSTRUCTED_H

#include <cstddef>
#include <cstdint>

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

    uint32_t id() const { return id_; }
    int priority() const { return priority_; }
    bool enabled() const { return enabled_; }
    void set_enabled(bool e) { enabled_ = e; }

protected:
    uint32_t id_;
    int      priority_;
    bool     enabled_;
};

#endif
```

> ⚠️ **L'ordre des méthodes virtuelles est critique.** La vtable est construite dans l'ordre de déclaration. Si vous déclarez `configure()` avant `name()`, votre vtable sera incompatible et le programme crashera en appelant la mauvaise méthode via le mauvais slot. C'est ici que votre analyse de la section 22.1 (reconstruction des vtables) est directement mise à l'épreuve.

### Vérification du layout

Compilez un programme de test minimal pour vérifier que votre header produit le même layout mémoire que le binaire original :

```cpp
// check_layout.cpp
#include "processor_reconstructed.h"
#include <cstdio>
#include <cstddef>

class DummyProcessor : public Processor {  
public:  
    DummyProcessor() : Processor(0, 0) {}
    const char* name() const override { return "dummy"; }
    bool configure(const char*, const char*) override { return false; }
    int process(const char*, size_t, char*, size_t) override { return 0; }
    const char* status() const override { return "ok"; }
};

int main() {
    printf("sizeof(Processor)      = %zu\n", sizeof(Processor));
    printf("sizeof(DummyProcessor) = %zu\n", sizeof(DummyProcessor));

    DummyProcessor d;
    char* base = (char*)&d;
    printf("offset vptr     = %zu\n", (size_t)0);  /* toujours 0 */
    printf("offset id_      = %zu\n", offsetof(DummyProcessor, id_));
    printf("offset priority = %zu\n", offsetof(DummyProcessor, priority_));
    printf("offset enabled  = %zu\n", offsetof(DummyProcessor, enabled_));
    return 0;
}
```

```bash
$ g++ -std=c++17 -O2 -o check_layout check_layout.cpp
$ ./check_layout
sizeof(Processor)      = 24  
sizeof(DummyProcessor) = 24  
offset id_      = 8  
offset priority = 12  
offset enabled  = 16  
```

Si ces valeurs correspondent à ce que vous avez observé dans le binaire (section 22.1 et phase 1), votre header est correct. Si une valeur diffère, revoyez l'ordre des champs ou le type d'un membre — un `int64_t` au lieu d'un `int` décalerait tout ce qui suit.

---

## Phase 3 — Implémenter le plugin

Avec le header vérifié, écrivez votre plugin. L'implémentation du processeur lui-même est libre — ce qui compte pour la compatibilité est le respect de l'interface et la convention de plugin. Voici un exemple de processeur « LeetSpeak » qui transforme le texte en langage l33t :

```cpp
/* plugin_gamma.cpp
 *
 * Plugin « LeetSpeak » pour ch22-oop.
 * Compilé à partir d'un header reconstitué, sans accès aux sources originales.
 *
 * Compile :
 *   g++ -shared -fPIC -std=c++17 -O2 -o plugin_gamma.so plugin_gamma.cpp
 */

#include "processor_reconstructed.h"

#include <cstdio>
#include <cstring>

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

/* ── Factory extern "C" : contrat identifié en phase 1 ── */

extern "C" {

Processor* create_processor(uint32_t id) {
    fprintf(stderr, "[plugin_gamma] creating LeetSpeakProcessor id=%u\n", id);
    return new LeetSpeakProcessor(id);
}

void destroy_processor(Processor* p) {
    fprintf(stderr, "[plugin_gamma] destroying processor\n");
    delete p;
}

}
```

### Compilation

```bash
$ g++ -shared -fPIC -std=c++17 -O2 -o plugins/plugin_gamma.so plugin_gamma.cpp
```

Le flag `-fPIC` est obligatoire pour un shared object. L'option `-std=c++17` assure la compatibilité avec le binaire hôte (compilé avec le même standard). Le flag `-O2` n'est pas fonctionnellement nécessaire mais produit un code comparable aux autres plugins pour l'analyse.

> ⚠️ **`-rdynamic` n'est pas nécessaire ici.** Ce flag concerne l'exécutable hôte (pour exporter ses symboles vers les plugins). Le plugin lui-même n'a pas besoin de rendre ses symboles internes visibles à l'hôte — seules les deux fonctions `extern "C"` doivent être exportées, ce qui est le comportement par défaut.

---

## Phase 4 — Vérification avant exécution

Avant de lancer le binaire, effectuez une série de vérifications statiques pour maximiser les chances de réussite du premier coup.

### 4.1 — Vérifier les symboles exportés

```bash
$ nm -CD plugins/plugin_gamma.so | grep -E 'create|destroy'
0000000000001200 T create_processor
0000000000001260 T destroy_processor
```

Les deux symboles sont présents, exportés (`T`), et non manglés (pas de `_Z` — le `extern "C"` fonctionne). Si vous voyez un symbole manglé comme `_Z16create_processorj`, c'est que le `extern "C"` est manquant ou mal positionné.

### 4.2 — Vérifier la RTTI et la vtable

```bash
$ nm -C plugins/plugin_gamma.so | grep -E 'vtable|typeinfo'
```

Vous devriez voir :

```
0000000000003d00 V vtable for LeetSpeakProcessor
0000000000003d50 V typeinfo for LeetSpeakProcessor
0000000000003d68 V typeinfo name for LeetSpeakProcessor
                 U typeinfo for Processor
                 U vtable for __cxa_pure_virtual
```

Le `U` (undefined) devant `typeinfo for Processor` est normal — ce symbole sera résolu au chargement par le linker dynamique, depuis l'exécutable hôte (qui l'exporte grâce à `-rdynamic`). Si ce symbole n'apparaît pas comme undefined, c'est que votre `LeetSpeakProcessor` ne reconnaît pas `Processor` comme parent — vérifiez l'héritage.

### 4.3 — Comparer la structure de la vtable

Examinez la vtable de votre plugin et comparez-la avec celle de `plugin_alpha.so` :

```bash
$ objdump -d -M intel --section=.text plugins/plugin_gamma.so | head -100
```

Ou dans Ghidra, vérifiez que votre vtable a le même nombre d'entrées et dans le même ordre que celle de `Rot13Processor`. Les deux premières entrées doivent être les deux variantes du destructeur, suivies de `name`, `configure`, `process`, `status`.

### 4.4 — Vérifier la compatibilité ABI

```bash
$ readelf -h plugins/plugin_gamma.so | grep -E 'Class|Machine'
  Class:                             ELF64
  Machine:                           Advanced Micro Devices X86-64

$ readelf -h oop_O2_strip | grep -E 'Class|Machine'
  Class:                             ELF64
  Machine:                           Advanced Micro Devices X86-64
```

Les deux doivent correspondre : même classe (ELF64), même architecture (x86-64).

---

## Phase 5 — Exécution et validation

### 5.1 — Premier lancement

```bash
$ ./oop_O2_strip -p ./plugins "Hello World from RE"
```

Si tout est correct, la sortie doit montrer votre processeur dans le pipeline :

```
[Pipeline] loading plugin: ./plugins/plugin_gamma.so
[plugin_gamma] creating LeetSpeakProcessor id=3
[Pipeline] loaded: ./plugins/plugin_gamma.so (name=LeetSpeakProcessor, id=3, priority=40)
...
=== Pipeline Start ===
Input: "Hello World from RE"

[STEP] UpperCaseProcessor        → "HELLO WORLD FROM RE"
[STEP] ReverseProcessor          → "ER MORF DLROW OLLEH"
[STEP] Rot13Processor            → "RE ZBES QYJBJ BYYRU"
[STEP] LeetSpeakProcessor        → "R3 Z835 QYJ8J 8YYRU"
[STEP] XorCipherProcessor        → "..."

Output: "..."
=== Pipeline End ===

--- Status ---
  [UpperCase #0] processed=19 skip_digits=no
  [Reverse #0] chunks=1 word_mode=no
  [ROT13 #1] rotated=19 half=no
  [LeetSpeak #3] converted=19 aggressive=no
  [XorCipher #2] xored=19 key=42 printable=yes
--------------
```

Votre plugin apparaît entre `Rot13Processor` (priorité 30) et `XorCipherProcessor` (priorité 50), conformément à sa priorité de 40. Le tri par priorité — que vous avez observé dans le dispatch virtuel (section 22.3) — place votre processeur au bon endroit dans la chaîne.

### 5.2 — Diagnostic en cas d'échec

**Segfault au chargement** — La vtable est probablement incompatible. Causes fréquentes :

- Méthodes virtuelles déclarées dans le mauvais ordre → les slots de vtable sont décalés → l'hôte appelle `name()` mais tombe sur `configure()`.  
- Destructeur non virtuel → le slot 0 de la vtable ne contient pas le destructeur attendu.  
- Champ manquant ou mal dimensionné dans la classe de base → les offsets des membres sont décalés → l'hôte lit `priority_` là où se trouve `enabled_`.

Pour diagnostiquer, lancez sous GDB :

```bash
$ gdb -q --args ./oop_O2_strip -p ./plugins "test"
(gdb) run
```

Au segfault, examinez le backtrace (`bt`), l'instruction fautive (`x/i $rip`), et les registres. Si le crash est sur un `call [rax+0xNN]`, comparez l'offset avec votre tableau de vtable — un offset inattendu révèle un décalage.

**Plugin chargé mais pas de sortie** — Vérifiez que `process()` retourne bien un nombre d'octets positif (pas 0, pas -1). Le pipeline interprète une valeur négative comme une erreur et s'arrête. Vérifiez aussi que `enabled_` est initialisé à `true` dans le constructeur — s'il est `false`, le pipeline saute votre processeur.

**« missing symbols »** — Le message `[Pipeline] missing symbols in plugin_gamma.so` indique que `dlsym` n'a pas trouvé `create_processor` ou `destroy_processor`. Vérifiez avec `nm -CD` que les symboles sont bien exportés et non manglés.

### 5.3 — Validation dynamique avec `LD_PRELOAD`

Utilisez la bibliothèque d'interception `operator new` (section 22.4) pour vérifier que votre objet est bien alloué avec la taille attendue :

```bash
$ LD_PRELOAD=./preload_new.so ./oop_O2_strip -p ./plugins "test"
```

Si votre `LeetSpeakProcessor` a un `sizeof` de 40 octets (24 hérités + `bool` + padding + `size_t`), vous devriez voir :

```
[PRELOAD:new] size=40   → 0x...  (UpperCase/Reverse-sized)
[PRELOAD:new] size=40   → 0x...  (UpperCase/Reverse-sized)
[PRELOAD:new] size=40   → 0x...  (LeetSpeak — même taille)
[PRELOAD:new] size=48   → 0x...  (Rot13-sized)
[PRELOAD:new] size=80   → 0x...  (XorCipher-sized)
```

---

## Critères de validation

Le checkpoint est considéré comme réussi quand les conditions suivantes sont toutes remplies :

| # | Critère | Comment vérifier |  
|---|---------|-----------------|  
| 1 | Le plugin est compilé sans accès au header original `processor.h` | Le header utilisé est un fichier reconstitué, documentant les choix de RE |  
| 2 | `plugin_gamma.so` exporte les deux symboles factory non manglés | `nm -CD plugin_gamma.so` montre `create_processor` et `destroy_processor` |  
| 3 | Le binaire `oop_O2_strip` charge le plugin sans erreur | Pas de message `dlopen error` ni `missing symbols` |  
| 4 | Le processeur apparaît dans le pipeline et produit une sortie | La ligne `[STEP] LeetSpeakProcessor → "..."` est visible |  
| 5 | Le processeur respecte l'ordre de priorité | Il s'insère au bon endroit dans la chaîne de traitement |  
| 6 | Le programme se termine proprement (pas de segfault, pas de leak) | Le destructeur est appelé, `valgrind` ne rapporte pas de fuite |  
| 7 | `name()` et `status()` retournent des chaînes cohérentes | Les chaînes apparaissent dans la section `--- Status ---` |

---

## Ce que ce checkpoint valide

En produisant un plugin fonctionnel à partir d'un binaire strippé, vous avez démontré que vous savez :

- **Reconstruire une interface C++ abstraite** à partir des vtables, de la RTTI et des accès mémoire observés — sans code source, sans documentation (22.1).  
- **Identifier le contrat d'un système de plugins** en analysant les appels `dlopen`/`dlsym` et les chaînes associées (22.2).  
- **Comprendre le dispatch virtuel** suffisamment pour implémenter les méthodes dans le bon ordre et produire une vtable compatible (22.3).  
- **Utiliser les techniques dynamiques** (`LD_PRELOAD`, GDB, `ltrace`) pour valider vos hypothèses et diagnostiquer les problèmes (22.4).

Ce workflow — observer, reconstituer, implémenter, vérifier — est représentatif du travail de reverse engineering en conditions réelles : interopérabilité avec des systèmes fermés, développement de plugins pour des applications propriétaires, ou analyse de malwares modulaires.

---


⏭️ [Chapitre 23 — Reverse d'un binaire réseau (client/serveur)](/23-network/README.md)
