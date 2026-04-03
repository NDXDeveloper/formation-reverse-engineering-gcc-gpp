🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 20.4 — Reconstruire un fichier `.h` depuis un binaire (types, structs, API)

> 📘 **Chapitre 20 — Décompilation et reconstruction du code source**  
> **Partie IV — Techniques Avancées de RE**

---

## Le header comme livrable de RE

Dans les sections précédentes, on a appris à obtenir du pseudo-code depuis un binaire avec Ghidra et RetDec. Ce pseudo-code est utile pour comprendre la logique, mais il reste enfermé dans l'outil de décompilation. La question naturelle qui suit est : **que produit-on concrètement à la fin d'une analyse ?**

L'un des livrables les plus utiles du reverse engineering est un fichier `.h` — un header C/C++ qui capture tout ce que l'analyste a découvert sur les interfaces du binaire. Ce fichier documente les structures de données, les signatures de fonctions, les constantes, et les énumérations reconstruites depuis le désassemblage et la décompilation. C'est un artefact tangible qui sert plusieurs objectifs :

**Documenter l'analyse.** Le header est un résumé structuré et lisible de tout le travail de retypage et de renommage effectué dans Ghidra. Il peut être relu par un collègue, versionné dans un dépôt Git, et servir de référence pour des analyses futures sur des versions ultérieures du même binaire.

**Écrire du code qui interagit avec le binaire.** Si l'objectif est d'écrire un client compatible avec le serveur analysé (chapitre 23), un plugin pour l'application C++ (chapitre 22), ou un keygen (chapitre 21), le header fournit les définitions nécessaires pour que ce code compile et soit compatible avec les structures du binaire.

**Alimenter Ghidra lui-même.** Le header reconstruit peut être réimporté dans Ghidra via *File → Parse C Source* pour enrichir le Data Type Manager, ce qui améliore en retour la qualité du pseudo-code dans tout le projet.

Cette section détaille la méthode pour construire ce header pas à pas, en utilisant nos trois binaires d'entraînement comme exemples.

---

## Méthodologie générale

Reconstruire un header depuis un binaire n'est pas un processus automatique — c'est un travail itératif qui se construit au fil de l'analyse. On ne s'assoit pas devant un binaire en se disant « je vais maintenant produire le header ». On le construit progressivement, en extrayant et en formalisant les découvertes faites dans Ghidra.

La méthode suit un ordre naturel : d'abord les constantes et les énumérations (les éléments les plus simples à identifier), puis les structures de données (qui nécessitent une analyse des accès mémoire), puis les signatures de fonctions (qui dépendent des types précédemment définis), et enfin les relations entre ces éléments (graphe d'appels, dépendances entre structures).

### Phase 1 : constantes et magic numbers

Les constantes sont les premières informations exploitables dans un binaire, car elles apparaissent en clair dans le code machine — ce sont des valeurs immédiates dans les instructions ou des données dans `.rodata`.

**Où les trouver.** Dans Ghidra, la recherche de scalaires (*Search → For Scalars*) liste toutes les valeurs immédiates du binaire. Les valeurs récurrentes ou reconnaissables méritent d'être nommées. La commande `strings` et la vue *Defined Strings* de Ghidra révèlent les chaînes de caractères, dont certaines sont des identifiants de protocole, des messages d'erreur ou des noms de configuration.

**Comment les extraire.** On cherche les patterns suivants dans le pseudo-code et le désassemblage :

Les **magic numbers** apparaissent dans les comparaisons et les initialisations. Dans `keygenme_O2_strip`, le pseudo-code de la fonction de dérivation contient la valeur `0xdeadbeef` utilisée comme seed initial. Dans `server_O2_strip`, les octets `0xc0` et `0xfe` apparaissent dans la fonction de réception comme test de validité du header. Ces valeurs deviennent des `#define` dans le header.

Les **tailles et limites** apparaissent comme bornes de boucles, arguments de `malloc`, ou paramètres de `memcmp`/`memcpy`. Un `memcmp(buf, expected, 0x20)` indique une comparaison sur 32 octets — c'est probablement la taille d'un hash. Un `recv(fd, buf, 0x400, 0)` indique un buffer de 1024 octets. Ces valeurs deviennent aussi des `#define`.

Les **valeurs d'énumération** apparaissent dans les `switch` et les chaînes de `if/else`. Dans le serveur réseau, le pseudo-code du dispatcher principal teste `type == 1`, `type == 2`, etc. En croisant avec les chaînes de caractères des messages d'erreur et les réponses envoyées, on peut déduire la sémantique de chaque valeur.

**Ce que ça donne dans le header.** Après analyse de `server_O2_strip`, on pourrait écrire :

```c
/* Constantes du protocole — extraites de server_O2_strip */
#define PROTO_MAGIC_0       0xC0
#define PROTO_MAGIC_1       0xFE
#define PROTO_VERSION       0x01
#define PROTO_MAX_PAYLOAD   1024    /* 0x400 */
#define PROTO_HEADER_SIZE   6
#define PROTO_TOKEN_LEN     16     /* 0x10 */
#define PROTO_HASH_LEN      32     /* 0x20 */
#define DEFAULT_PORT        4337   /* 0x10F1 */
```

À ce stade, on ne sait pas encore comment ces constantes s'appellent dans le source original. Les noms choisis sont des déductions raisonnables basées sur le contexte d'utilisation. C'est parfaitement normal — le header reconstruit documente la compréhension de l'analyste, pas le code source exact.

### Phase 2 : structures de données

Les structures sont au cœur du header reconstruit. C'est aussi la partie la plus technique, car les structures n'existent plus explicitement dans le binaire — elles se manifestent uniquement par les patterns d'accès mémoire.

**Identifier une structure.** Le signal principal est un pointeur utilisé avec plusieurs offsets différents. Quand le pseudo-code montre :

```c
*(uint8_t *)(param_1 + 0)  = 0xc0;
*(uint8_t *)(param_1 + 1)  = 0xfe;
*(uint8_t *)(param_1 + 2)  = 0x01;
*(uint8_t *)(param_1 + 3)  = type;
*(uint16_t *)(param_1 + 4) = payload_len;
```

On reconnaît un accès séquentiel à des champs contigus. Les offsets `0, 1, 2, 3, 4` et les tailles d'accès (`uint8_t` pour les 4 premiers, `uint16_t` pour le dernier) dessinent la structure en mémoire.

**Déterminer les types des champs.** Le type de chaque champ se déduit de la manière dont il est utilisé :

Un champ comparé avec une constante connue (`== 0xC0`) est probablement un `uint8_t` de type magic byte. Un champ passé à `strlen` ou `printf("%s", ...)` est un `char[]` ou `char *`. Un champ utilisé comme argument de taille dans `memcpy` ou `malloc` est un entier (sa largeur dépend de la taille de l'instruction d'accès). Un champ passé à `memcmp` avec une longueur de 32 est probablement un `uint8_t[32]` de type hash.

**Déterminer l'alignement et le packing.** GCC aligne les champs des structures par défaut selon les règles de l'ABI. Un `uint32_t` à l'offset 5 (non aligné sur 4) indiquerait une structure `__attribute__((packed))`. On peut vérifier en regardant si le compilateur utilise des accès mémoire non alignés (load/store byte-par-byte) ou des accès directs (load/store 32 bits).

**Gérer les trous (padding).** Si un champ `uint8_t` est à l'offset 3 et le champ suivant `uint32_t` est à l'offset 8, il y a un trou de padding de 4 octets entre les deux (offsets 4–7). GCC insère ce padding pour aligner le `uint32_t` sur une frontière de 4 octets. Le header doit soit ajouter un champ de padding explicite, soit utiliser `__attribute__((packed))` si la structure originale était packed.

**Ce que ça donne dans le header.** En analysant la fonction `recv_message` de `server_O2_strip`, on reconstruit :

```c
/* Header du protocole réseau — 6 octets, packed */
typedef struct __attribute__((packed)) {
    uint8_t  magic[2];       /* offset 0x00 — attendu: {0xC0, 0xFE} */
    uint8_t  version;        /* offset 0x02 — attendu: 0x01 */
    uint8_t  type;           /* offset 0x03 — type de message */
    uint16_t payload_len;    /* offset 0x04 — big-endian */
} proto_header_t;
```

Les commentaires d'offset sont essentiels : ils permettent de vérifier la cohérence de la structure avec le désassemblage et facilitent la relecture.

### Phase 3 : énumérations

Les énumérations émergent naturellement de l'analyse des `switch` et des tests de valeurs discrètes. Dans le dispatcher du serveur, on observe un `switch` sur le champ `type` du header, avec des cas pour les valeurs `1`, `2`, `3`, `4`, `5`, `6`, et `0xff`. En croisant avec le comportement de chaque cas (envoi d'un hash, envoi d'un token, traitement d'une commande, etc.), on reconstruit la sémantique :

```c
/* Types de messages — déduits du switch dans handle_client() */
typedef enum {
    MSG_AUTH_REQ    = 0x01,   /* client -> serveur : authentification */
    MSG_AUTH_RESP   = 0x02,   /* serveur -> client : réponse auth */
    MSG_CMD_REQ     = 0x03,   /* client -> serveur : commande */
    MSG_CMD_RESP    = 0x04,   /* serveur -> client : réponse commande */
    MSG_PING        = 0x05,   /* keepalive */
    MSG_PONG        = 0x06,   /* réponse keepalive */
    MSG_DISCONNECT  = 0xFF    /* fin de session */
} msg_type_t;
```

Les noms sont des conjectures basées sur le comportement observé. Si le binaire contient des chaînes de debug comme `"Auth OK"` ou `"Unknown command"`, elles confirment les hypothèses. Sinon, les noms restent des conventions de l'analyste — le header les documente avec des commentaires explicatifs.

### Phase 4 : signatures de fonctions

Une fois les types et les structures définis, les signatures de fonctions deviennent exprimables proprement. On les extrait depuis le pseudo-code de Ghidra en combinant plusieurs sources d'information :

**Nombre de paramètres.** Déterminé par les registres utilisés en entrée de la fonction selon la convention System V AMD64 : `rdi` (1er), `rsi` (2e), `rdx` (3e), `rcx` (4e), `r8` (5e), `r9` (6e). Au-delà, les paramètres passent par la pile. Ghidra détecte cela automatiquement dans la plupart des cas.

**Types des paramètres.** Déduits de l'utilisation dans le corps de la fonction. Un paramètre passé à `strlen` est un `const char *`. Un paramètre utilisé comme descripteur de fichier dans `send`/`recv` est un `int`. Un paramètre accédé avec les offsets de `proto_header_t` est un `proto_header_t *` (ou `uint8_t *` si la structure n'est pas encore reconstruite).

**Type de retour.** Déterminé par l'utilisation de `rax`/`eax` après l'appel. Si la valeur de retour est testée avec `test eax, eax` suivi d'un saut conditionnel, c'est un `int` utilisé comme booléen ou code d'erreur. Si elle est passée à `free` ou utilisée comme pointeur, c'est un `void *` ou un type pointeur spécifique.

**Convention d'appel et qualificateurs.** Pour les fonctions qui ne sont pas exportées (pas de symbole dynamique), on peut ajouter `static`. Pour les paramètres qui ne sont pas modifiés dans le corps de la fonction, `const` est approprié.

**Ce que ça donne dans le header :**

```c
/* ============================================================
 * API du protocole — signatures reconstruites depuis
 * server_O2_strip, fonctions identifiées par analyse Ghidra
 * ============================================================ */

/* Calcul du checksum XOR sur un buffer — FUN_00401120 */
static inline uint8_t proto_checksum(const uint8_t *data, size_t len);

/* Lecture big-endian 16 bits — inlinée, non présente comme symbole */
static inline uint16_t read_be16(const uint8_t *p);

/* Écriture big-endian 16 bits — inlinée, non présente comme symbole */
static inline void write_be16(uint8_t *p, uint16_t val);

/* Envoi d'un message complet (header + payload + checksum)
 * Retourne 0 en cas de succès, -1 en cas d'erreur.
 * FUN_00401250 */
static int send_message(int fd, uint8_t type,
                        const uint8_t *payload, uint16_t payload_len);

/* Réception d'un message complet avec vérification du checksum.
 * Retourne 0 en cas de succès, -1 en cas d'erreur.
 * type, payload et payload_len sont des paramètres de sortie.
 * FUN_00401340 */
static int recv_message(int fd, uint8_t *type,
                        uint8_t *payload, uint16_t *payload_len);

/* Gestion de l'authentification côté serveur — FUN_00401500 */
static void handle_auth(int fd, const uint8_t *payload,
                        uint16_t payload_len);

/* Dispatch des commandes après authentification — FUN_00401620 */
static void handle_cmd(int fd, const uint8_t *payload,
                       uint16_t payload_len);
```

Chaque signature est annotée de l'adresse de la fonction dans le binaire (`FUN_XXXXXXXX`). Cette traçabilité permet de retrouver la source de chaque déclaration dans Ghidra.

### Phase 5 : assemblage et organisation du header

Le header final doit être structuré pour être lisible et utilisable. Voici la convention recommandée :

```c
/*
 * ch20_network_reconstructed.h
 *
 * Header reconstruit par analyse du binaire server_O2_strip
 * Formation RE — Chapitre 20, exercice de décompilation
 *
 * Ce fichier documente les structures et l'API du protocole
 * réseau custom identifié dans le serveur TCP sur port 4337.
 *
 * Outil principal : Ghidra 11.x
 * Outil secondaire : RetDec 5.0
 * Binaire source : server_O2_strip (SHA256: ...)
 */

#ifndef CH20_NETWORK_RECONSTRUCTED_H
#define CH20_NETWORK_RECONSTRUCTED_H

#include <stdint.h>
#include <stddef.h>

/* ---- Section 1 : Constantes ---- */
/* ... #define ... */

/* ---- Section 2 : Énumérations ---- */
/* ... typedef enum ... */

/* ---- Section 3 : Structures ---- */
/* ... typedef struct ... */

/* ---- Section 4 : Signatures de fonctions ---- */
/* ... prototypes ... */

#endif /* CH20_NETWORK_RECONSTRUCTED_H */
```

L'en-tête du fichier contient les métadonnées de l'analyse : quel binaire a été analysé (avec un hash pour le retrouver sans ambiguïté), quels outils ont été utilisés, et quelle est la date de l'analyse. Les include guards et les includes système rendent le header directement utilisable dans du code C.

---

## Cas pratique : le binaire keygenme_O2_strip

Appliquons la méthode sur un binaire qui ne fournit aucune aide — ni symboles, ni DWARF. Voici les étapes concrètes.

### Identification des constantes

Après le triage initial (`strings`, `file`, `checksec`) et l'import dans Ghidra, on navigue vers la fonction `main` (identifiée via le point d'entrée `_start` → `__libc_start_main`). Le pseudo-code révèle :

- La valeur `0xdeadbeef` assignée à un champ avant un appel de calcul. C'est une seed.  
- Le littéral `3` dans une comparaison de longueur de chaîne. C'est la longueur minimale du username.  
- Le littéral `0x10` (16) dans une boucle de comparaison. C'est la taille de la clé.  
- Le littéral `4` comme borne de boucle dans la fonction de dérivation. C'est le nombre de rounds.  
- La valeur `0x01000193` dans la boucle de hashing. C'est la constante FNV prime, un indice important sur l'algorithme utilisé.

```c
#define MAGIC_SEED    0xDEADBEEF
#define KEY_LEN       16
#define MAX_USER      64    /* déduit de la taille du buffer fgets */
#define ROUND_COUNT   4
#define FNV_PRIME     0x01000193
```

### Reconstruction de la structure principale

La fonction `main` alloue un bloc sur la pile dont les différents offsets sont utilisés pour stocker le username (offset 0x00, accédé par `fgets` avec une taille de 64), la clé attendue (offset 0x40, écrite par la fonction de dérivation, 16 octets) et la seed (offset 0x50, initialisée à `0xdeadbeef`). On en déduit :

```c
/* Structure de contexte de licence — reconstruite depuis main()
 * Taille totale : 0x54 (84 octets) */
typedef struct {
    char     username[64];     /* offset 0x00 — buffer fgets */
    uint8_t  expected_key[16]; /* offset 0x40 — clé dérivée */
    uint32_t seed;             /* offset 0x50 — initialisé à 0xDEADBEEF */
} license_ctx_t;
```

Pour valider cette reconstruction, on vérifie que `sizeof(license_ctx_t)` correspond à la taille allouée sur la pile (visible dans le prologue de `main` par l'instruction `sub rsp, ...`), en tenant compte de l'alignement.

### Extraction des signatures

Les fonctions internes de `keygenme_O2_strip` ne portent pas de nom. On les identifie par leur rôle et on leur attribue un nom dans le header :

```c
/* Dérivation de la clé depuis le username et la seed.
 * Itère ROUND_COUNT fois un hash custom (XOR + rotation + FNV).
 * FUN_00401200 — appelée depuis main() après saisie du username */
void derive_key(const char *username, uint32_t seed, uint8_t *out_key);

/* Parsing de la saisie utilisateur au format XXXXXXXX-XXXXXXXX-...
 * Retourne 0 si le format est valide, -1 sinon.
 * FUN_00401350 — appelée depuis main() après saisie de la clé */
int parse_key_input(const char *input, uint8_t *out_key);

/* Comparaison en temps constant de deux buffers de KEY_LEN octets.
 * Retourne 1 si identiques, 0 sinon.
 * FUN_00401400 — appelée depuis main() pour la vérification finale */
int verify_key(const uint8_t *expected, const uint8_t *provided);
```

---

## Cas spécifique : le C++ et oop_O2_strip

Le binaire C++ ajoute des complexités propres à la reconstruction du header. La notion même de « fichier `.h` » prend un sens légèrement différent : on reconstruit des déclarations de classes avec leurs méthodes virtuelles, plutôt que de simples structures C et fonctions libres.

### Reconstruire une classe depuis la vtable

Dans Ghidra, après avoir identifié la vtable de `Device` (une série de pointeurs de fonctions dans `.rodata`, référencée par le constructeur), on peut reconstituer la déclaration de la classe. L'ordre des pointeurs dans la vtable donne l'ordre des méthodes virtuelles :

```cpp
/* Classe de base abstraite — reconstruite depuis la vtable
 * à l'adresse 0x404a00 dans oop_O2_strip
 *
 * Layout vtable (offsets depuis le vptr de l'objet) :
 *   +0x00  -> ~Device() D1 (complete object destructor)
 *   +0x08  -> ~Device() D0 (deleting destructor)
 *   +0x10  -> type_name() const -> std::string
 *   +0x18  -> initialize()
 *   +0x20  -> process()
 *   +0x28  -> status_report() const -> std::string
 */
class Device {  
public:  
    virtual ~Device();
    virtual std::string type_name() const = 0;
    virtual void        initialize()       = 0;
    virtual void        process()          = 0;
    virtual std::string status_report() const;

    /* Accesseurs non virtuels — identifiés par appels directs (non vtable) */
    const std::string &name() const;
    uint32_t           id()   const;
    bool               is_active() const;

protected:
    void set_active(bool state);

private:
    /* Layout mémoire de l'objet (hors vptr) :
     * +0x08  std::string name_    (taille dépend de l'implémentation)
     * +0x28  uint32_t    id_      (offset vérifié par accès dans accesseur)
     * +0x2c  bool        active_  (1 octet + padding) */
    std::string name_;
    uint32_t    id_;
    bool        active_;
};
```

Les offsets des membres sont obtenus en analysant les accesseurs non virtuels (les fonctions qui lisent un champ à un offset fixe depuis `this`). L'accesseur `id()` fait `return *(uint32_t *)(this + 0x28)`, ce qui place `id_` à l'offset 0x28. La taille de `std::string` avant ce champ (0x28 - 0x08 = 0x20 = 32 octets) est cohérente avec l'implémentation de libstdc++ où `std::string` contient un pointeur, une taille et un buffer SSO.

### Les classes filles

Pour `Sensor` et `Actuator`, on répète le processus : identifier leur vtable respective, comparer avec celle de `Device` pour trouver les méthodes overridées, et analyser leurs constructeurs pour déduire les membres supplémentaires.

```cpp
/* Classe Sensor — hérite de Device
 * Vtable à 0x404a40
 * Membres supplémentaires à partir de l'offset 0x30 :
 *   +0x30  double   min_range_
 *   +0x38  double   max_range_
 *   +0x40  double   last_value_
 *   +0x48  uint32_t read_count_
 */
class Sensor : public Device {  
public:  
    Sensor(const std::string &name, uint32_t id,
           double min_range, double max_range);

    std::string type_name() const override;
    void        initialize() override;
    void        process() override;
    std::string status_report() const override;

    double last_value() const;

private:
    double   min_range_;
    double   max_range_;
    double   last_value_;
    uint32_t read_count_;
};
```

### L'interface plugin

Le système de plugins utilise `dlopen`/`dlsym` pour charger des fonctions exportées en convention C. L'analyse de la fonction `load_plugin` (identifiable par ses appels à `dlopen`, `dlsym`, `dlerror`) révèle les noms de symboles recherchés : les chaînes `"plugin_name"` et `"plugin_run"` dans `.rodata`. Le header documente cette interface :

```cpp
/* Interface plugin — convention C pour le chargement dynamique.
 * Un plugin .so valide doit exporter ces deux symboles. */
extern "C" {
    /* Retourne le nom du plugin (chaîne statique) */
    const char *plugin_name(void);

    /* Point d'entrée du plugin — reçoit un pointeur vers le DeviceManager */
    void plugin_run(DeviceManager *mgr);
}
```

Ce header est directement utilisable pour écrire un plugin compatible sans disposer du code source de l'application — ce qui est exactement l'objectif du checkpoint du chapitre 22.

---

## Validation du header reconstruit

Un header reconstruit n'a de valeur que s'il est correct. Plusieurs techniques de validation sont possibles.

### Test de compilation

Le header doit compiler sans erreur quand il est inclus dans un fichier C/C++ vide :

```bash
echo '#include "ch20_network_reconstructed.h"' > test.c  
gcc -c -Wall -Wextra -std=c11 test.c  
```

Si le compilateur émet des avertissements ou des erreurs, le header contient des incohérences de types ou des dépendances manquantes.

### Test de taille des structures

On peut écrire un petit programme qui vérifie que les tailles et les offsets correspondent à ce qu'on observe dans le binaire :

```c
#include <stdio.h>
#include <stddef.h>
#include "ch20_network_reconstructed.h"

int main(void) {
    printf("sizeof(proto_header_t) = %zu (attendu: 6)\n",
           sizeof(proto_header_t));
    printf("offsetof(proto_header_t, type) = %zu (attendu: 3)\n",
           offsetof(proto_header_t, type));
    printf("offsetof(proto_header_t, payload_len) = %zu (attendu: 4)\n",
           offsetof(proto_header_t, payload_len));
    printf("sizeof(auth_req_payload_t) = %zu (attendu: 64)\n",
           sizeof(auth_req_payload_t));
    return 0;
}
```

Si les tailles ne correspondent pas, c'est qu'un champ est mal typé, qu'il manque du padding, ou que l'attribut `packed` est manquant.

### Test fonctionnel

La validation la plus forte est d'écrire du code qui utilise le header pour interagir avec le binaire. Pour le binaire réseau, écrire un client minimal qui utilise les structures et les constantes du header pour s'authentifier auprès du serveur prouve que la reconstruction est correcte. Si le serveur accepte la connexion et répond correctement, les structures sont exactes.

---

## Erreurs courantes et comment les éviter

### Confondre l'ordre des champs avec l'ordre d'accès

Le pseudo-code de Ghidra montre les accès mémoire dans l'ordre d'exécution, pas nécessairement dans l'ordre des champs de la structure. Si une fonction remplit d'abord le champ à l'offset 0x10, puis celui à l'offset 0x00, l'analyste peut être tenté de placer le premier champ écrit en premier dans la structure. Il faut toujours se baser sur les **offsets numériques**, pas sur l'ordre d'apparition dans le pseudo-code.

### Ignorer le padding d'alignement

Sur x86-64, GCC aligne les `uint32_t` sur 4 octets et les `uint64_t`/`double`/pointeurs sur 8 octets par défaut. Oublier ce padding conduit à des offsets décalés pour tous les champs suivants. En cas de doute, on vérifie l'offset de chaque champ individuellement dans le désassemblage plutôt que de se fier au calcul cumulatif.

### Confondre taille d'un type et taille d'un accès

Un champ `bool` (1 octet) peut être lu par une instruction `movzx eax, byte ptr [...]` mais aussi par un `mov eax, [...]` qui charge 4 octets (les 3 octets supérieurs étant ignorés ou du padding). Le type réel est déterminé par la sémantique d'utilisation, pas par la largeur de l'instruction d'accès.

### Oublier l'endianness des champs réseau

Pour les structures de protocole réseau, les champs multi-octets sont souvent en big-endian (network byte order), alors que x86-64 est little-endian. Le pseudo-code montre des appels à `htons`/`ntohs` (ou des shifts manuels équivalents) qui sont le signal d'une conversion d'endianness. Le header doit documenter clairement quelle convention est utilisée pour chaque champ, comme on l'a fait avec `/* big-endian */` dans `proto_header_t`.

### Ne pas documenter le niveau de confiance

Toutes les reconstructions ne se valent pas. Un champ dont le type est confirmé par trois accès différents et un appel de bibliothèque connue est quasi certain. Un champ déduit d'un seul accès ambigu est hypothétique. Le header gagne à annoter le niveau de confiance :

```c
    uint32_t seed;             /* offset 0x50 — CONFIRMÉ: init 0xDEADBEEF,
                                  passé en param_2 à derive_key */
    uint8_t  unknown_0x54[4];  /* offset 0x54 — INCERTAIN: accédé une seule
                                  fois, rôle non déterminé */
```

---

## Réimporter le header dans Ghidra

Une fois le header finalisé, on peut le réimporter dans Ghidra pour boucler la boucle et améliorer le pseudo-code de l'ensemble du projet.

La procédure est la suivante : dans le Data Type Manager, clic droit → *Open/Create* → sélectionner *Parse C Source*. Dans la fenêtre qui apparaît, ajouter le fichier `.h` dans la liste des fichiers source, configurer les options de parsing (standard C11 ou C++17 selon le cas), et lancer le parsing. Les types définis dans le header sont importés dans le Data Type Manager et peuvent être appliqués aux variables et aux paramètres dans le pseudo-code.

L'effet est immédiat : les accès avec offsets numériques se transforment en accès par nom de champ, les `undefined4` deviennent des `uint32_t`, et le pseudo-code gagne considérablement en lisibilité. C'est le cycle vertueux de la décompilation : plus on formalise les types, plus le pseudo-code est lisible, plus on peut affiner les types.

---


⏭️ [Identifier des bibliothèques tierces embarquées (FLIRT / signatures Ghidra)](/20-decompilation/05-flirt-signatures.md)
