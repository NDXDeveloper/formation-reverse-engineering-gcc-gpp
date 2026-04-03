🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 23.2 — RE du parseur de paquets (state machine, champs, magic bytes)

> 🎯 **Objectif de cette section** : ouvrir le binaire serveur dans Ghidra et reconstruire intégralement le parseur de paquets — la fonction qui lit les octets entrants, valide le magic byte, extrait le type et la longueur, puis dispatche vers le handler de chaque commande. À la fin de cette section, vous aurez une compréhension précise du format de chaque type de message et de la machine à états du protocole.

---

## Stratégie d'analyse

À la section 23.1, on a formulé des hypothèses sur le protocole en observant le trafic. On entre maintenant dans le désassembleur avec un objectif clair : **localiser le parseur de paquets et confirmer (ou corriger) chaque hypothèse**.

L'approche est top-down en trois temps :

1. **Trouver le point d'entrée réseau** — localiser les appels à `recv()`/`read()` dans le binaire serveur.  
2. **Remonter au parseur** — identifier la fonction qui traite les données reçues, en suivant le buffer depuis `recv()` jusqu'à la logique de décision.  
3. **Reconstruire la machine à états** — comprendre l'enchaînement des états du protocole (handshake → authentification → commandes → déconnexion) et le format exact de chaque message.

On travaille sur le binaire serveur plutôt que le client, parce que c'est le serveur qui **valide** le protocole. Le client construit les messages, mais le serveur les vérifie — et c'est dans la vérification qu'on trouve les règles exactes du format.

---

## Étape 1 — Localiser les fonctions réseau dans Ghidra

### Import et analyse automatique

On importe le binaire `server` (variante `-O0 -g` pour commencer) dans Ghidra et on lance l'analyse automatique avec les options par défaut. Si le binaire n'est pas strippé, le Symbol Tree se remplit immédiatement avec les noms de fonctions.

### Chercher les appels réseau par les imports

Dans le Symbol Tree, on ouvre la catégorie **Imports** (ou **External Functions**) et on cherche les fonctions de la libc liées aux sockets :

- `socket`  
- `bind`  
- `listen`  
- `accept`  
- `recv` / `read`  
- `send` / `write`  
- `close`

Chacune de ces fonctions apparaît comme un symbole externe résolu via la PLT. En double-cliquant sur `recv` (ou `read`), Ghidra affiche le thunk PLT. L'intérêt n'est pas le thunk lui-même, mais ses **cross-references** (XREF).

### Remonter via les cross-references

On fait un clic droit sur `recv` → **References → Find references to** (ou raccourci `Ctrl+Shift+F` dans Ghidra). La liste des XREF montre toutes les fonctions qui appellent `recv`. Dans un serveur simple, on en trouve typiquement deux ou trois :

- Un appel dans la **boucle de réception principale** — c'est celui qui nous intéresse.  
- Éventuellement un appel dans une fonction utilitaire de type `recv_all` ou `recv_exact` qui boucle jusqu'à avoir reçu `n` octets.  
- Parfois un appel dans le handshake initial, séparé de la boucle principale.

On navigue vers chaque XREF et on identifie celle qui se trouve dans une boucle — c'est la boucle de traitement du protocole.

### Alternative : chercher par les chaînes

Si le binaire est strippé et qu'on n'a pas les noms de fonctions importées directement visibles, on utilise les chaînes identifiées à la section 23.1. Par exemple, si `strings` avait révélé `"Bad magic"` ou `"Invalid command"`, on cherche cette chaîne dans Ghidra (**Search → For Strings** ou **Defined Strings** dans la fenêtre), puis on remonte aux fonctions qui la référencent. Un message d'erreur `"Bad magic byte"` sera forcément proche du code qui vérifie le magic byte — c'est un raccourci très efficace vers le parseur.

---

## Étape 2 — Anatomie du parseur de paquets

Une fois la fonction de parsing localisée, on la décompile dans Ghidra. Le pseudo-code brut est souvent verbeux et utilise des noms de variables génériques (`iVar1`, `local_28`, `param_1`…). Le travail consiste à le rendre lisible en renommant et retypant progressivement.

### Structure typique d'un parseur réseau

Avant de plonger dans le code spécifique, voici le squelette que l'on retrouve dans la grande majorité des parseurs de protocoles binaires TCP :

```
┌─────────────────────────────────────────────┐
│  1. Lire N octets (header fixe)             │
│     → recv(fd, header_buf, HEADER_SIZE, 0)  │
├─────────────────────────────────────────────┤
│  2. Valider le magic byte                   │
│     → if (header_buf[0] != MAGIC) → erreur  │
├─────────────────────────────────────────────┤
│  3. Extraire le type de commande            │
│     → cmd_type = header_buf[1]              │
├─────────────────────────────────────────────┤
│  4. Extraire la longueur du payload         │
│     → payload_len = (header_buf[2] << 8)    │
│                    | header_buf[3]          │
├─────────────────────────────────────────────┤
│  5. Lire le payload                         │
│     → recv(fd, payload_buf, payload_len, 0) │
├─────────────────────────────────────────────┤
│  6. Dispatcher selon le type                │
│     → switch(cmd_type) {                    │
│         case 0x01: handle_hello(...)        │
│         case 0x02: handle_auth(...)         │
│         case 0x03: handle_cmd(...)          │
│         case 0x04: handle_quit(...)         │
│       }                                     │
└─────────────────────────────────────────────┘
```

C'est ce squelette qu'on va retrouver — sous une forme plus ou moins directe selon le niveau d'optimisation — dans le désassemblage.

### Identifier le magic byte dans le désassemblage

La vérification du magic byte est généralement le premier test conditionnel après la lecture de l'en-tête. En assembleur x86-64, elle ressemble à ceci :

```asm
; Lire le premier octet du buffer dans un registre
movzx  eax, BYTE PTR [rbp-0x28]      ; header_buf[0]  
cmp    al, 0xC0                        ; comparer avec le magic byte  
jne    .bad_magic                      ; si différent → erreur  
```

Dans le décompilateur Ghidra, ça donne quelque chose comme :

```c
if (header_buf[0] != 0xc0) {
    puts("Bad magic byte");
    return -1;
}
```

On confirme l'hypothèse de la section 23.1 : le magic byte est bien `0xC0`. On renomme la constante dans Ghidra en créant un `#define` ou un `enum` :

```
Clic droit sur 0xc0 → Set Equate → "PROTO_MAGIC"
```

> 💡 **Astuce Ghidra** : les equates permettent de remplacer les constantes numériques par des noms symboliques partout dans le listing. C'est un gain de lisibilité considérable quand le même magic byte apparaît à plusieurs endroits (envoi et réception).

### Extraire le champ type

Juste après la validation du magic byte, le parseur extrait le type de commande. En `-O0`, c'est direct :

```c
cmd_type = header_buf[1];
```

On vérifie que les valeurs correspondent à nos observations : `0x01`, `0x02`, `0x03`, `0x04` pour les requêtes client. Le dispatch se fait typiquement par un `switch` ou une cascade de `if/else if`.

Dans le décompilateur Ghidra à `-O0`, un `switch` apparaît clairement :

```c
switch (cmd_type) {  
case 1:  
    handle_hello(client_fd, payload_buf, payload_len);
    break;
case 2:
    handle_auth(client_fd, payload_buf, payload_len);
    break;
case 3:
    handle_command(client_fd, payload_buf, payload_len);
    break;
case 4:
    handle_quit(client_fd);
    break;
default:
    send_error(client_fd, 0xff);
    break;
}
```

Les noms `handle_hello`, `handle_auth`, etc. ne seront visibles que si le binaire a ses symboles. Sur un binaire strippé, Ghidra affichera `FUN_00401a30`, `FUN_00401b80`, etc. — on les renommera manuellement au fur et à mesure de l'analyse.

> 📝 **Convention de renommage** : adoptez un préfixe cohérent dès le début. Par exemple `proto_handle_hello`, `proto_handle_auth`, `proto_parse_header`, `proto_send_response`. Cela rend le Function Call Graph beaucoup plus lisible.

### Extraire le champ longueur

Le champ longueur est sur deux octets. La manière dont il est lu révèle l'**endianness** du protocole. Deux patterns assembleur fréquents :

**Big-endian (network byte order)** — le plus courant dans les protocoles réseau :

```c
payload_len = (header_buf[2] << 8) | header_buf[3];
```

En assembleur, on reconnaît le `shl` de 8 bits suivi d'un `or` :

```asm
movzx  eax, BYTE PTR [rbp-0x26]      ; header_buf[2]  
shl    eax, 8  
movzx  edx, BYTE PTR [rbp-0x25]      ; header_buf[3]  
or     eax, edx  
```

**Avec `ntohs()`** — si l'auteur a utilisé la macro standard :

```c
payload_len = ntohs(*(uint16_t *)(header_buf + 2));
```

En assembleur, `ntohs` se traduit par une instruction `bswap` (sur 32 bits suivie d'un shift) ou `ror`/`xchg` sur les deux octets. Ghidra reconnaît souvent ce pattern et affiche directement `ntohs()` ou `__bswap_16()` dans le décompilateur.

**Little-endian** — plus rare dans les protocoles réseau, mais possible :

```c
payload_len = *(uint16_t *)(header_buf + 2);  // lecture directe, LE natif x86
```

On vérifie avec nos captures : si le paquet HELLO contenait `\x00\x08` aux octets 2–3 et que le payload faisait 8 octets, alors c'est bien du big-endian (`0x0008` = 8). En little-endian, `\x00\x08` se lirait comme `0x0800` = 2048, ce qui serait incohérent avec les tailles observées.

---

## Étape 3 — Reconstruire chaque handler

Le dispatch étant identifié, on descend dans chaque handler pour reconstruire le format exact du payload de chaque type de message.

### Handler HELLO (`0x01` / `0x81`)

Le handler du handshake est généralement simple. Côté serveur, il :

1. Vérifie éventuellement le contenu du payload (la chaîne `"HELLO"` ou un identifiant de version du protocole).  
2. Génère une réponse contenant un message de bienvenue et, potentiellement, un **challenge** (nonce aléatoire).

Dans le décompilateur, on cherche :

- Un appel à `memcmp()` ou `strcmp()` comparant le payload avec une chaîne fixe.  
- Un appel à `rand()`, `random()`, `/dev/urandom` ou `getrandom()` pour générer le challenge.  
- La construction du paquet de réponse : header avec magic `0xC0`, type `0x81`, longueur, puis payload.

```c
// Pseudo-code reconstruit (après renommage)
void proto_handle_hello(int client_fd, uint8_t *payload, uint16_t len) {
    if (memcmp(payload, "HELLO", 5) != 0) {
        proto_send_error(client_fd, ERR_BAD_HELLO);
        return;
    }
    
    uint8_t challenge[8];
    getrandom(challenge, 8, 0);               // génère un nonce de 8 octets
    memcpy(session->challenge, challenge, 8);  // stocke pour vérification future
    
    uint8_t response[4 + 7 + 8];              // header + "WELCOME" + challenge
    response[0] = PROTO_MAGIC;                 // 0xC0
    response[1] = MSG_HELLO_RESP;              // 0x81
    response[2] = 0x00;                        // longueur high byte
    response[3] = 0x0F;                        // longueur low byte (15)
    memcpy(response + 4, "WELCOME", 7);
    memcpy(response + 11, challenge, 8);
    
    send(client_fd, response, sizeof(response), 0);
    session->state = STATE_HELLO_DONE;
}
```

Plusieurs informations cruciales émergent :

- Le challenge est bien **aléatoire** (appel à `getrandom`), ce qui confirme qu'il change à chaque session.  
- La session possède un **état** (`session->state`), mis à jour après chaque étape réussie. C'est le cœur de la machine à états.  
- Le format du payload de réponse est : 7 octets de texte `"WELCOME"` + 8 octets de challenge, pour un total de 15 (`0x0F`) octets.

### Handler AUTH (`0x02` / `0x82`)

Le handler d'authentification est le plus riche en logique. On y cherche :

- Comment le **username** et le **password** sont extraits du payload.  
- Si le challenge du handshake est utilisé (hash du mot de passe avec le nonce ?).  
- Comment la réponse de succès/échec est construite.

Le format d'extraction des chaînes length-prefixed se reconnaît dans le désassemblage par un pattern récurrent :

```asm
; Extraire la longueur du username
movzx  eax, BYTE PTR [rdi]           ; premier octet = longueur du username  
movzx  ecx, al                        ; ecx = username_len  
lea    rsi, [rdi+1]                   ; rsi pointe sur le début du username  

; Avancer dans le buffer
add    rdi, rcx                       ; skip le username  
inc    rdi                            ; skip l'octet de longueur suivant  

; Extraire la longueur du password
movzx  eax, BYTE PTR [rdi]           ; longueur du password
```

En pseudo-code reconstruit :

```c
void proto_handle_auth(int client_fd, uint8_t *payload, uint16_t len) {
    // Vérifier l'état de la session
    if (session->state != STATE_HELLO_DONE) {
        proto_send_error(client_fd, ERR_WRONG_STATE);
        return;
    }
    
    // Extraire username (length-prefixed)
    uint8_t user_len = payload[0];
    char *username = (char *)(payload + 1);
    
    // Extraire password (length-prefixed)
    uint8_t pass_len = payload[1 + user_len];
    
    // Copier le password et le dé-XOR-er avec le challenge
    uint8_t password[256];
    memcpy(password, payload + 2 + user_len, pass_len);
    xor_with_challenge(password, pass_len, session->challenge);
    
    // Vérifier les credentials
    uint8_t status;
    if (check_credentials(username, user_len, password, pass_len)) {
        status = AUTH_OK;                 // 0x01
        session->state = STATE_AUTHENTICATED;
    } else {
        status = AUTH_FAIL;               // 0x00
    }
    
    // Construire et envoyer la réponse
    uint8_t response[6] = { PROTO_MAGIC, MSG_AUTH_RESP, 0x00, 0x02, 
                            0x00, status };
    send(client_fd, response, 6, 0);
}
```

Le format du payload AUTH est maintenant clair :

```
[user_len: 1 octet][username: user_len octets][pass_len: 1 octet][password: pass_len octets]
```

On note aussi que la réponse AUTH fait 6 octets : 4 d'en-tête + 2 de payload (un octet inconnu `0x00` et un octet de status `0x01` pour succès). L'octet inconnu pourrait être un code d'erreur détaillé, un compteur de tentatives restantes, ou un padding — il faudra tester avec des credentials incorrects pour observer si ces valeurs changent.

> 💡 **Méthode** : pour chaque champ dont le rôle n'est pas clair, on fait varier les entrées et on observe la sortie. C'est la combinaison analyse statique + analyse dynamique qui donne les réponses les plus fiables.

### La fonction `xor_with_challenge` — découvrir la protection du mot de passe

Le pseudo-code ci-dessus contient un appel à `xor_with_challenge` avant la comparaison des credentials. Dans le désassemblage, cette fonction se repère facilement : c'est une **petite boucle** qui itère sur chaque octet du mot de passe et le combine avec le challenge.

En assembleur x86-64, le pattern typique d'un XOR cyclique ressemble à ceci :

```asm
; xor_with_challenge(password, pass_len, challenge)
; rdi = password, rsi = pass_len, rdx = challenge
    xor    ecx, ecx                    ; i = 0
.loop:
    cmp    rcx, rsi                    ; i < pass_len ?
    jge    .done
    mov    rax, rcx
    xor    edx, edx
    div    r8                          ; i % CHALLENGE_LEN (ou AND 0x7 si len=8)
    movzx  eax, BYTE PTR [rdx+rax]    ; challenge[i % 8]
    xor    BYTE PTR [rdi+rcx], al      ; password[i] ^= challenge[i % 8]
    inc    rcx
    jmp    .loop
.done:
```

> 💡 **Pattern reconnaissable** : une boucle avec un `xor` byte-par-byte et un modulo (ou un `and` avec une puissance de 2 moins 1) sur l'index est le signe classique d'un XOR cyclique avec une clé. Si la taille de la clé est une puissance de 2 (ici 8 = 2³), GCC optimise le `div` en `and reg, 0x7`, ce qui est encore plus caractéristique.

Dans le décompilateur Ghidra, la boucle apparaît de manière plus lisible :

```c
void xor_with_challenge(uint8_t *data, size_t len, uint8_t *challenge) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= challenge[i % 8];
    }
}
```

C'est le mécanisme central de protection du mot de passe :

1. Lors du handshake, le serveur génère un **challenge aléatoire** de 8 octets.  
2. Le client **XOR le mot de passe** avec ce challenge avant de l'envoyer.  
3. Le serveur **applique le même XOR** pour retrouver le mot de passe en clair, puis le compare avec la base interne.

Ce mécanisme empêche le replay naïf de l'authentification (puisque le challenge change à chaque session), mais il est **cryptographiquement faible** : un attaquant qui capture le handshake complet (challenge en clair + mot de passe XOR-é) peut retrouver le mot de passe par simple XOR inverse. On exploitera cette faiblesse à la section 23.4.

### La fonction `check_credentials` — comparaison en clair

Après le XOR-décodage, le mot de passe est en clair en mémoire. La vérification des credentials est alors une simple comparaison :

```c
int check_credentials(char *user, uint8_t ulen, char *pass, uint8_t plen) {
    for (int i = 0; user_db[i].username != NULL; i++) {
        if (ulen == strlen(user_db[i].username) &&
            memcmp(user, user_db[i].username, ulen) == 0 &&
            plen == strlen(user_db[i].password) &&
            memcmp(pass, user_db[i].password, plen) == 0)
        {
            return 1;
        }
    }
    return 0;
}
```

La boucle `for` avec un tableau de structures terminé par un pointeur NULL est un pattern GCC classique pour les tables de données statiques. Les appels à `memcmp` et `strlen` sont facilement identifiables dans le désassemblage (ce sont des fonctions importées de la libc, visibles dans les XREF).

> 💡 **Extraction des credentials** : en posant un breakpoint GDB sur `memcmp` pendant une tentative d'authentification, on peut observer les arguments de chaque comparaison et ainsi lire les noms d'utilisateurs et mots de passe stockés en clair dans le binaire. Alternativement, `strings` sur le binaire peut révéler des chaînes adjacentes qui ressemblent à des credentials — les mots de passe sont stockés en clair dans la section `.rodata`.

### Handler COMMAND (`0x03` / `0x83`) et Handler QUIT (`0x04` / `0x84`)

On applique la même méthode pour chaque handler restant. Le handler COMMAND est souvent celui qui contient la logique métier (lecture de fichiers, exécution d'actions, requêtes de données). Le handler QUIT est généralement trivial (nettoyer la session, envoyer un acquittement, fermer le socket).

Pour chaque handler, on documente :

- Le **format exact du payload** (champ par champ, avec tailles et encodage).  
- Les **conditions de validation** (vérifications de longueur, de valeurs autorisées).  
- Le **format de la réponse** correspondante.  
- Les **transitions d'état** de la machine à états.

---

## Étape 4 — Reconstruire la machine à états

### Identifier la variable d'état

La plupart des serveurs de protocole maintiennent un **état de session** qui détermine quelles commandes sont acceptées à un instant donné. On ne peut pas envoyer une commande sans s'être authentifié, ni s'authentifier sans avoir fait le handshake.

Dans le désassemblage, cet état se manifeste par :

- Une **structure de session** allouée à chaque connexion (sur la pile ou sur le heap via `malloc`).  
- Un **champ entier** dans cette structure, comparé au début de chaque handler.  
- Des **vérifications de type** `if (session->state != EXPECTED_STATE) → erreur`.

En parcourant les handlers, on peut reconstituer l'ensemble des états et des transitions :

```
               ┌──────────────┐
               │  CONNECTED   │  (état initial après accept)
               └──────┬───────┘
                      │ recv HELLO (0x01)
                      ▼
               ┌──────────────┐
               │ HELLO_DONE   │  (handshake complété)
               └──────┬───────┘
                      │ recv AUTH (0x02) + credentials valides
                      ▼
               ┌──────────────┐
          ┌───▶│AUTHENTICATED │◀───┐  (prêt pour les commandes)
          │    └──────┬───────┘    │
          │           │            │
          │    recv CMD (0x03)     │
          │           │            │
          │           ▼            │
          │    traitement +        │
          │    envoi réponse ──────┘
          │
          │    recv QUIT (0x04)
          │           │
          │           ▼
          │    ┌──────────────┐
          │    │ DISCONNECTED │  (session terminée)
          │    └──────────────┘
          │
          │    AUTH échoué → reste en HELLO_DONE
          │    (possibilité de retenter)
          └────────────────────────────────────
```

### Vérifier avec GDB

Pour confirmer la machine à états, on peut poser un **watchpoint** sur le champ d'état de la session dans GDB :

```bash
$ gdb ./server
(gdb) break accept
(gdb) run
# ... accept retourne avec fd = 4
(gdb) # Identifier l'adresse de la structure session
(gdb) # (visible dans le code après accept, souvent allouée sur la pile ou via malloc)
(gdb) watch *((int*)0x7ffff...)    # adresse du champ state
(gdb) continue
```

Chaque fois que l'état change, GDB interrompt l'exécution et affiche l'ancienne et la nouvelle valeur. On voit ainsi les transitions défiler en temps réel pendant qu'on fait tourner le client dans un autre terminal.

Avec GEF ou pwndbg, la commande `watch` est enrichie d'un contexte visuel qui montre les registres et la pile à chaque transition — ce qui permet de corréler immédiatement la transition avec la commande qui l'a provoquée.

---

## Étape 5 — Reconstruire les structures de données

### Définir l'en-tête du protocole dans Ghidra

Maintenant que le format de l'en-tête est confirmé, on le formalise en créant un type structuré dans le Data Type Manager de Ghidra :

```c
struct proto_header {
    uint8_t  magic;        // offset 0 — toujours 0xC0
    uint8_t  msg_type;     // offset 1 — type de commande
    uint16_t payload_len;  // offset 2 — longueur du payload (big-endian)
};
```

Pour créer cette structure dans Ghidra : **Data Type Manager → clic droit → New → Structure**, puis ajouter les champs un par un avec les bons types et tailles.

Une fois la structure créée, on la réapplique dans le décompilateur. Au lieu de :

```c
if (local_28[0] != 0xc0) { ... }  
iVar1 = (int)local_28[1];  
uVar2 = ((uint)local_28[2] << 8) | (uint)local_28[3];  
```

On obtient, après retypage du buffer en `struct proto_header *` :

```c
if (hdr->magic != PROTO_MAGIC) { ... }  
cmd_type = hdr->msg_type;  
payload_len = ntohs(hdr->payload_len);  
```

La lisibilité est transformée. On fait de même pour la structure de session :

```c
struct client_session {
    int      socket_fd;
    int      state;           // 0=CONNECTED, 1=HELLO_DONE, 2=AUTHENTICATED
    uint8_t  challenge[8];    // nonce du handshake
    char     username[64];    // username authentifié
};
```

### Définir les constantes comme un `enum`

Les valeurs des types de messages et des états sont plus lisibles sous forme d'enum :

```c
enum msg_type : uint8_t {
    MSG_HELLO_REQ    = 0x01,
    MSG_AUTH_REQ     = 0x02,
    MSG_CMD_REQ      = 0x03,
    MSG_QUIT_REQ     = 0x04,
    MSG_HELLO_RESP   = 0x81,
    MSG_AUTH_RESP    = 0x82,
    MSG_CMD_RESP     = 0x83,
    MSG_QUIT_RESP    = 0x84,
    MSG_ERROR        = 0xFF
};

enum session_state : int {
    STATE_CONNECTED     = 0,
    STATE_HELLO_DONE    = 1,
    STATE_AUTHENTICATED = 2,
    STATE_DISCONNECTED  = 3
};
```

Dans Ghidra, on crée ces enums via **Data Type Manager → New → Enum**, puis on les applique sur les variables et constantes correspondantes dans le décompilateur. Chaque `0x01` se transforme en `MSG_HELLO_REQ`, chaque `2` en `STATE_AUTHENTICATED` — le pseudo-code devient quasiment du code source lisible.

---

## Étape 6 — Documenter le format du protocole

À ce stade, on a toutes les informations pour produire une **spécification informelle** du protocole. Ce document sera la référence pour l'écriture du pattern ImHex (section 23.3) et du client Python (section 23.5).

### Format de l'en-tête (commun à tous les messages)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Magic     |   Msg Type    |         Payload Length        |
|     (0xC0)    |               |         (big-endian)          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                     Payload (variable)                        |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Convention du champ Msg Type

Le bit 7 encode la direction :

- **Bit 7 = 0** (`0x01`–`0x7F`) : requête client → serveur.  
- **Bit 7 = 1** (`0x81`–`0xFF`) : réponse serveur → client.  
- Le type de réponse est toujours `type_requête | 0x80`.  
- La valeur spéciale `0xFF` est réservée aux erreurs.

### Payloads par type de message

**HELLO Request (`0x01`)** :

```
+-------+-------+-------+-------+-------+-------+-------+-------+
| 'H'   | 'E'   | 'L'   | 'L'   | 'O'   | 0x00  | 0x00  | 0x00  |
+-------+-------+-------+-------+-------+-------+-------+-------+
  Identifiant fixe "HELLO"          Padding / réservé
```

**HELLO Response (`0x81`)** :

```
+-------+-------+-------+-------+-------+-------+-------+
| 'W'   | 'E'   | 'L'   | 'C'   | 'O'   | 'M'   | 'E'   |
+-------+-------+-------+-------+-------+-------+-------+
|          Challenge (8 octets, aléatoire)              |
+-------+-------+-------+-------+-------+-------+-------+
```

**AUTH Request (`0x02`)** :

```
+----------+-------------------+----------+-------------------+
| user_len | username          | pass_len | password          |
| (1 octet)| (user_len octets) | (1 octet)| (pass_len octets) |
+----------+-------------------+----------+-------------------+
```

**AUTH Response (`0x82`)** :

```
+----------+----------+
| reserved | status   |
| (1 octet)| (1 octet)|
+----------+----------+
  0x00       0x01 = OK, 0x00 = FAIL
```

### Séquence protocolaire complète

```
1. Client → Serveur : HELLO Request
2. Serveur → Client : HELLO Response (+ challenge)
3. Client → Serveur : AUTH Request (username + password/hash)
4. Serveur → Client : AUTH Response (status)
   - Si AUTH_FAIL → retour à l'étape 3 (retry) ou déconnexion
   - Si AUTH_OK → passage en mode commande
5. Client → Serveur : CMD Request (N fois)
6. Serveur → Client : CMD Response (N fois)
7. Client → Serveur : QUIT Request
8. Serveur → Client : QUIT Response
9. Fermeture TCP
```

---

## Cas des binaires optimisés et strippés

Tout ce qui précède suppose un binaire `-O0` avec symboles, où la correspondance entre le code source et le désassemblage est quasi directe. En pratique, les binaires d'entraînement sont aussi fournis en `-O2` et strippés. Voici les principales différences :

### Effet de `-O2` sur le parseur

- **Inlining** : les petits handlers peuvent être inlinés dans la boucle principale. Au lieu de voir `call handle_hello`, on voit le code du handler directement dans le `switch`. Le dispatch reste visible, mais les "fonctions" disparaissent du Function Call Graph.  
- **Réorganisation des branches** : GCC place les cas les plus fréquents en premier et peut transformer le `switch` en table de sauts ou en arbre binaire de comparaisons. Le pattern `cmp/je` en cascade est remplacé par un `jmp [rax*8 + table]` — plus efficace mais moins lisible.  
- **Optimisation des lectures** : au lieu de lire le header octet par octet, le compilateur peut faire un `mov eax, DWORD PTR [rdi]` qui lit les 4 octets d'un coup, puis extraire chaque champ par masquage (`and`, `shr`). Le résultat est fonctionnellement identique mais la correspondance avec les champs de la structure est moins évidente.

### Effet du stripping

Sans symboles, les noms de fonctions disparaissent. On se retrouve avec `FUN_00401230` au lieu de `proto_handle_auth`. La stratégie d'analyse reste la même — on entre par les chaînes de caractères et les appels à `recv`/`send` — mais le travail de renommage est entièrement à la charge de l'analyste.

L'approche recommandée est de commencer par la variante `-O0 -g`, de bien comprendre le protocole, puis de vérifier que les mêmes structures se retrouvent dans la variante optimisée. Cela entraîne l'œil à reconnaître les patterns d'optimisation GCC (chapitre 16) appliqués au contexte réseau.

---

## Récapitulatif de la section

| Étape | Action | Résultat |  
|-------|--------|----------|  
| 1. Localiser les fonctions réseau | XREF sur `recv`/`read` dans Ghidra | Identification de la boucle de réception principale |  
| 2. Anatomie du parseur | Décompilation + renommage | Structure de l'en-tête confirmée : magic, type, longueur |  
| 3. Reconstruire les handlers | Descente dans chaque `case` du dispatch | Format exact du payload pour chaque type de message |  
| 4. Machine à états | Identification de la variable d'état + watchpoint GDB | Diagramme de transitions CONNECTED → HELLO_DONE → AUTHENTICATED |  
| 5. Structures de données | Création de `struct` et `enum` dans Ghidra | Pseudo-code lisible, proche du code source original |  
| 6. Documentation | Spécification informelle du protocole | Référence pour ImHex (23.3) et le client Python (23.5) |

On dispose maintenant d'une compréhension complète du protocole : format de l'en-tête, format de chaque payload, machine à états, et mécanisme d'authentification. La section suivante (**23.3**) va formaliser visuellement cette compréhension en écrivant un pattern `.hexpat` pour ImHex, capable de décoder automatiquement les trames capturées.

⏭️ [Visualiser les trames binaires avec ImHex et écrire un `.hexpat` pour le protocole](/23-network/03-trames-imhex-hexpat.md)
