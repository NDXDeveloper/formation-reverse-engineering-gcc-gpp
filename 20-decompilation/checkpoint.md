🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 20

## Produire un `.h` complet pour le binaire `ch20-network`

> **Binaire cible** : `binaries/ch20-network/server_O2_strip`  
> **Temps estimé** : 2 à 3 heures  
> **Outils requis** : Ghidra, RetDec (optionnel), GCC, un éditeur de texte  
> **Prérequis** : avoir lu les sections 20.1 à 20.6

---

## Objectif

Ce checkpoint valide l'ensemble des compétences acquises dans le chapitre 20. Le but est de produire un fichier header C complet (`ch20_network_reconstructed.h`) à partir du binaire strippé du serveur réseau, **sans consulter le code source** fourni dans `binaries/ch20-network/`.

Le header reconstruit doit capturer intégralement le protocole binaire implémenté par le serveur : constantes, structures de messages, énumérations, et signatures des fonctions principales. Il doit être suffisamment correct et complet pour qu'un développeur tiers puisse écrire un client compatible en ne disposant que de ce header.

---

## Le binaire cible

Le serveur réseau est compilé avec `-O2 -s` — optimisé et strippé. Il ne contient ni symboles de debug, ni noms de fonctions locales. Les seuls noms disponibles sont les symboles dynamiques importés depuis la libc (fonctions PLT : `socket`, `bind`, `listen`, `accept`, `send`, `recv`, `memcmp`, `strncmp`, `printf`, etc.).

Le serveur écoute sur un port TCP, accepte des connexions, authentifie les clients via un échange binaire, puis traite des commandes. L'intégralité de ce protocole est à découvrir par décompilation.

---

## Ce que le header doit contenir

### 1. Constantes du protocole

Toutes les valeurs immédiates ayant un rôle identifiable dans le protocole doivent être formalisées en `#define`. Cela inclut au minimum :

- Les magic bytes du header de protocole  
- Le numéro de version du protocole  
- La taille maximale d'un payload  
- La taille du header de protocole  
- La taille des tokens de session  
- La taille des hashes d'authentification  
- Le port d'écoute par défaut

### 2. Énumérations

Les valeurs discrètes utilisées dans les `switch` ou les chaînes de `if/else` doivent être formalisées en `typedef enum`. Cela inclut :

- Les types de messages du protocole (identifiés dans le dispatcher principal du serveur)  
- Les identifiants de commandes (identifiés dans le handler de commandes)

Chaque valeur doit être accompagnée d'un commentaire indiquant son rôle déduit.

### 3. Structures de données

Toutes les structures de messages échangés sur le réseau doivent être reconstruites avec les bons types, les bons offsets, et l'attribut `packed` si nécessaire. Cela inclut au minimum :

- La structure du header de protocole (les premiers octets de chaque trame)  
- La structure du payload d'authentification (requête client)  
- La structure de la réponse d'authentification (réponse serveur)  
- La structure du header de commande (requête client)

Chaque champ doit être commenté avec son offset et sa sémantique.

### 4. Signatures de fonctions

Les fonctions principales du serveur doivent être déclarées avec leurs types de paramètres et de retour. Cela inclut :

- La fonction de calcul de checksum  
- Les fonctions utilitaires de conversion d'endianness (si elles ne sont pas inlinées)  
- La fonction d'envoi de message  
- La fonction de réception de message  
- Le handler d'authentification  
- Le handler de commandes  
- La fonction de génération de token

Chaque signature doit être annotée de l'adresse de la fonction dans le binaire (`FUN_XXXXXXXX` ou adresse hexadécimale).

---

## Critères de validation

Le header est considéré complet et correct s'il satisfait les cinq critères suivants.

### Critère 1 : compilation sans erreur

```bash
echo '#include "ch20_network_reconstructed.h"' > test_header.c  
gcc -Wall -Wextra -Wpedantic -std=c11 -c test_header.c  
```

La compilation ne doit produire **aucune erreur et aucun warning**. Les `#include` système nécessaires (`<stdint.h>`, `<stddef.h>`, etc.) doivent être présents dans le header.

### Critère 2 : tailles des structures correctes

Les tailles et offsets des structures doivent correspondre à ce qui est observé dans le binaire. Un programme de vérification doit confirmer :

```c
#include <stdio.h>
#include <stddef.h>
#include "ch20_network_reconstructed.h"

int main(void) {
    int ok = 1;

    /* Vérifier la taille du header protocole */
    if (sizeof(proto_header_t) != 6) {
        printf("FAIL: sizeof(proto_header_t) = %zu, attendu 6\n",
               sizeof(proto_header_t));
        ok = 0;
    }

    /* Vérifier l'offset du champ type */
    if (offsetof(proto_header_t, type) != 3) {
        printf("FAIL: offsetof(proto_header_t, type) = %zu, attendu 3\n",
               offsetof(proto_header_t, type));
        ok = 0;
    }

    /* Vérifier l'offset du champ payload_len */
    if (offsetof(proto_header_t, payload_len) != 4) {
        printf("FAIL: offsetof(proto_header_t, payload_len) = %zu, attendu 4\n",
               offsetof(proto_header_t, payload_len));
        ok = 0;
    }

    /* Ajouter des vérifications similaires pour les autres structures :
     * auth_req_payload_t, auth_resp_payload_t, cmd_req_header_t */

    if (ok) printf("ALL CHECKS PASSED\n");
    return ok ? 0 : 1;
}
```

Toutes les vérifications doivent passer.

### Critère 3 : exhaustivité des types de messages

L'énumération des types de messages doit couvrir toutes les valeurs traitées par le dispatcher du serveur. Aucune branche du `switch` principal ne doit correspondre à une valeur absente de l'énumération.

### Critère 4 : exhaustivité des commandes

L'énumération des commandes doit couvrir toutes les valeurs traitées par le handler de commandes. Les commandes non documentées (celles qui tombent dans le `default` du `switch`) ne comptent pas — seules les commandes explicitement gérées doivent être listées.

### Critère 5 : utilisabilité pratique

Un développeur lisant uniquement le header (sans accès au binaire ni à Ghidra) doit pouvoir comprendre le protocole suffisamment pour esquisser un client. Cela signifie que les commentaires expliquent la sémantique de chaque champ, que l'endianness des champs multi-octets est documentée, et que le flux d'échanges est décrit au minimum en commentaire d'en-tête.

---

## Indices méthodologiques

Ces indices ne donnent pas la solution mais orientent la démarche. Les consulter progressivement si nécessaire.

### Indice 1 — Point de départ

Importer `server_O2_strip` dans Ghidra et lancer l'analyse automatique complète (toutes les options cochées, y compris Decompiler Parameter ID et Aggressive Instruction Finder). Localiser `main` via le point d'entrée `_start` → `__libc_start_main`. La fonction `main` contient les appels à `socket`, `bind`, `listen` et `accept` — elle est facile à identifier même sans symboles.

### Indice 2 — Le dispatcher

La fonction appelée après `accept` est le handler de client. Elle contient une boucle avec un `switch` sur un octet — c'est le dispatcher de types de messages. Les valeurs du `switch` sont les types de l'énumération à reconstruire.

### Indice 3 — Les magic bytes

La fonction de réception de message commence par lire 6 octets (le header), puis compare les deux premiers octets à des constantes. Ces constantes sont les magic bytes du protocole. La taille de ce premier `recv` donne la taille du header.

### Indice 4 — Les structures d'authentification

Le handler d'authentification appelle `strncmp` et `memcmp` sur le payload reçu. Les offsets de ces appels dans le buffer de payload révèlent la structure du message d'authentification : où se trouve le username, où se trouve le hash, et quelle est la taille de chaque champ.

### Indice 5 — La réponse d'authentification

Après une authentification réussie, le serveur envoie un message de réponse. Le pseudo-code du handler montre les octets écrits dans le buffer de réponse : un octet de statut (0 ou 1) suivi d'un bloc d'octets (le token de session). La taille de ce bloc donne `PROTO_TOKEN_LEN`.

### Indice 6 — Les credentials hardcodés

Le handler d'authentification compare le hash reçu avec un tableau de 32 octets stocké en `.rodata`. Ce tableau et le username de référence (une chaîne dans `.rodata` aussi) sont les credentials hardcodés. Ils ne font pas partie du header à proprement parler, mais les documenter en commentaire est un bonus utile.

### Indice 7 — Croiser avec RetDec

Si une fonction est particulièrement difficile à lire dans Ghidra (par exemple la fonction de génération de token ou le calcul de checksum), lancer RetDec sur le même binaire et comparer le pseudo-code des deux outils. RetDec peut reconstruire la boucle de checksum de manière plus compacte.

---

## Erreurs fréquentes à éviter

- **Oublier `__attribute__((packed))`** sur les structures réseau. Sans cet attribut, GCC insère du padding et les tailles ne correspondent plus.  
- **Confondre l'endianness.** Le champ `payload_len` est stocké en big-endian dans la trame réseau. Le header doit le documenter clairement, et les fonctions utilitaires `read_be16`/`write_be16` doivent être incluses ou déclarées.  
- **Attribuer un mauvais type au champ `success`** de la réponse d'authentification. C'est un `uint8_t` (1 octet), pas un `int` (4 octets). L'erreur décale tous les champs suivants de la structure.  
- **Manquer un type de message.** Le message `DISCONNECT` (`0xFF`) est facile à rater car il peut apparaître comme un cas séparé dans le dispatcher plutôt que dans le `switch` principal.  
- **Déclarer les fonctions inlinées comme des fonctions normales.** Les fonctions utilitaires comme le checksum ou les conversions d'endianness sont souvent inlinées par GCC en `-O2`. Elles n'ont pas d'adresse propre dans le binaire — on les déclare comme `static inline` dans le header.

---

## Livrable attendu

Un unique fichier `ch20_network_reconstructed.h` respectant la structure recommandée en section 20.4 :

```
ch20_network_reconstructed.h
├── Commentaire d'en-tête (binaire source, hash, outils, date)
├── Include guards
├── #include système (<stdint.h>, <stddef.h>)
├── Section 1 : Constantes (#define)
├── Section 2 : Énumérations (typedef enum)
├── Section 3 : Structures (typedef struct)
├── Section 4 : Fonctions utilitaires inline
└── Section 5 : Signatures des fonctions principales
```

---

## Solution

Le corrigé complet est disponible dans `solutions/ch20-checkpoint-solution.h`. Ne le consulter qu'après avoir produit sa propre version et vérifié les critères de validation. Comparer les deux versions pour identifier les divergences et comprendre les choix alternatifs.


⏭️ [Partie V — Cas Pratiques sur Nos Applications](/partie-5-cas-pratiques.md)
