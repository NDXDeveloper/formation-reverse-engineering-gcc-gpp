🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 23.3 — Visualiser les trames binaires avec ImHex et écrire un `.hexpat` pour le protocole

> 🎯 **Objectif de cette section** : exploiter les captures réseau de la section 23.1 et la spécification reconstruite à la section 23.2 pour écrire un pattern `.hexpat` capable de décoder automatiquement les trames du protocole custom dans ImHex. À la fin de cette section, vous disposerez d'un fichier `ch23_protocol.hexpat` qui colorise et annote chaque champ de chaque message, transformant un flux d'octets bruts en une lecture structurée du protocole.

---

## Pourquoi ImHex à cette étape

On dispose maintenant de deux sources d'information complémentaires :

- Les **captures réseau** (fichier `.pcap` ou export brut depuis Wireshark) qui contiennent les données réelles échangées entre client et serveur.  
- La **spécification du protocole** reconstruite par le désassemblage, qui décrit le format attendu de chaque champ.

ImHex fait le pont entre les deux. En écrivant un pattern `.hexpat`, on applique la spécification sur les données réelles et on **vérifie visuellement** que chaque octet tombe au bon endroit. Si un champ est mal aligné ou qu'une longueur est incohérente, la colorisation le révèle immédiatement. C'est un outil de validation puissant : une erreur dans la spécification qui passerait inaperçue en lisant du pseudo-code devient évidente quand les couleurs ne correspondent pas aux données.

Au-delà de la validation, le `.hexpat` est aussi un **livrable réutilisable**. Il pourra servir à décoder de nouvelles captures sans rouvrir Ghidra, à former d'autres analystes au protocole, ou à débugger le client de remplacement qu'on écrira à la section 23.5.

---

## Préparer les données à analyser

### Exporter le flux TCP brut depuis Wireshark

ImHex travaille sur des fichiers binaires bruts. Il faut donc extraire le payload applicatif de la capture `.pcap`, en éliminant les en-têtes Ethernet, IP et TCP.

Dans Wireshark :

1. Ouvrir `ch23_capture.pcap`.  
2. Appliquer le filtre `tcp.port == 4444 && tcp.len > 0`.  
3. Clic droit sur le premier paquet de la conversation → **Follow → TCP Stream**.  
4. Dans la fenêtre qui s'ouvre, choisir **Show data as: Raw**.  
5. Cliquer sur **Save as…** → enregistrer sous `ch23_stream.bin`.

Ce fichier contient l'intégralité du flux TCP reconstitué : tous les messages du client et du serveur concaténés dans l'ordre, sans les en-têtes réseau. C'est exactement ce que le parseur du serveur voit après la couche TCP.

> ⚠️ **Attention** : le "Follow TCP Stream" de Wireshark concatène les deux directions dans un seul flux. Par défaut, il alterne les données client (en rouge) et serveur (en bleu) dans la vue texte, mais l'export raw les mélange. Pour un `.hexpat` propre, il est souvent préférable d'exporter **chaque direction séparément** en utilisant le filtre directionnel dans la fenêtre Follow Stream (bouton radio "Client → Server" puis "Server → Client") et de sauver deux fichiers distincts. On peut aussi exporter le flux complet et gérer l'alternance dans le pattern — c'est plus complexe mais plus réaliste.

### Alternative : enregistrer depuis `strace`

Si l'on a tracé les échanges avec `strace -x -s 1024`, on peut extraire les buffers manuellement. C'est fastidieux mais utile quand on ne dispose pas d'une capture Wireshark. Un petit script Python fait le travail :

```python
import re, sys

with open(sys.argv[1]) as f:
    for line in f:
        # Chercher les lignes write() ou send() avec des données hex
        m = re.search(r'(?:write|send)\(\d+, "(.*?)", \d+\)', line)
        if m:
            raw = m.group(1)
            # Convertir les séquences \x.. en octets
            data = bytes.fromhex(
                re.sub(r'\\x([0-9a-f]{2})', r'\1',
                re.sub(r'[^\\x]|\\[^x]', '', raw) if False else
                raw.replace('\\x', ''))
            )
            sys.stdout.buffer.write(data)
```

On redirige la sortie dans un fichier binaire qu'on ouvre dans ImHex.

### Ouvrir le fichier dans ImHex

On lance ImHex et on ouvre `ch23_stream.bin`. L'éditeur hexadécimal affiche les octets bruts. Si l'export est correct, on doit retrouver visuellement le magic byte `0xC0` à intervalles réguliers — chaque occurrence marque le début d'un nouveau message.

Avant d'écrire le moindre pattern, un repérage visuel rapide est utile :

- **`Ctrl+F`** → rechercher la séquence `C0 01` pour localiser le HELLO Request.  
- **`Ctrl+F`** → rechercher `C0 81` pour le HELLO Response.  
- Vérifier que les distances entre les magic bytes correspondent aux longueurs attendues (4 octets d'en-tête + payload_len).

Ce repérage confirme que les données sont correctement alignées et prêtes pour le pattern.

---

## Écrire le pattern `.hexpat` — étape par étape

Le langage `.hexpat` d'ImHex est conçu pour décrire des structures binaires de manière déclarative. On va construire le pattern de manière incrémentale, en validant chaque ajout sur les données réelles.

### Étape 1 — L'en-tête seul

On commence par le minimum : décoder un seul en-tête de 4 octets.

```hexpat
#pragma endian big   // le protocole utilise le big-endian pour les champs multi-octets

import std.io;

enum MsgType : u8 {
    HELLO_REQ    = 0x01,
    AUTH_REQ     = 0x02,
    CMD_REQ      = 0x03,
    QUIT_REQ     = 0x04,
    HELLO_RESP   = 0x81,
    AUTH_RESP    = 0x82,
    CMD_RESP     = 0x83,
    QUIT_RESP    = 0x84,
    ERROR        = 0xFF
};

struct ProtoHeader {
    u8      magic;
    MsgType msg_type;
    u16     payload_len;
};

ProtoHeader header @ 0x00;
```

On sauvegarde ce fichier sous `ch23_protocol.hexpat` et on l'applique dans ImHex via le **Pattern Editor** (panneau latéral gauche → coller le code → clic sur le bouton ▶ "Evaluate").

Résultat immédiat : les 4 premiers octets du fichier sont décodés et colorisés. Le champ `magic` apparaît avec sa valeur `0xC0`, le champ `msg_type` affiche `HELLO_REQ (0x01)` grâce à l'enum, et `payload_len` affiche la longueur en décimal. Si tout correspond, on passe à l'étape suivante.

> 💡 **Astuce ImHex** : le pragma `#pragma endian big` s'applique globalement. Si certains champs étaient en little-endian, on pourrait utiliser le type `le u16` ponctuellement pour les surcharger. Ici, le protocole est entièrement big-endian donc le pragma global suffit.

### Étape 2 — Payloads typés

L'en-tête seul n'est pas suffisant : il faut aussi décoder le payload qui suit, et son format dépend du `msg_type`. C'est là que le pattern devient intéressant — on utilise les capacités conditionnelles du langage `.hexpat`.

```hexpat
struct LPString {
    u8   len;
    char value[len];
};

struct HelloPayload {
    char  identifier[5];    // "HELLO"
    u8    padding[parent.header.payload_len - 5];
};

struct WelcomePayload {
    char  banner[7];        // "WELCOME"
    u8    challenge[parent.header.payload_len - 7];
};

struct AuthRequestPayload {
    LPString username;
    LPString password;
};

struct AuthResponsePayload {
    u8 reserved;
    u8 status;
};

struct GenericPayload {
    u8 data[parent.header.payload_len];
};
```

Chaque structure de payload correspond à un type de message. La structure `LPString` (Length-Prefixed String) capture le pattern identifié à la section 23.2 : un octet de longueur suivi des données.

Le point clé est l'utilisation de `parent.header.payload_len` pour dimensionner les tableaux dynamiquement. Le langage `.hexpat` permet de référencer des champs déjà parsés dans la même structure (ou la structure parente) pour calculer des tailles à la volée.

### Étape 3 — Le message complet avec dispatch

On assemble le tout dans une structure `ProtoMessage` qui combine l'en-tête et le payload approprié :

```hexpat
struct ProtoMessage {
    ProtoHeader header;
    
    if (header.payload_len > 0) {
        match (header.msg_type) {
            (MsgType::HELLO_REQ):    HelloPayload        payload;
            (MsgType::HELLO_RESP):   WelcomePayload      payload;
            (MsgType::AUTH_REQ):     AuthRequestPayload   payload;
            (MsgType::AUTH_RESP):    AuthResponsePayload  payload;
            (_):                     GenericPayload       payload;
        }
    }
};
```

Le `match` est l'équivalent `.hexpat` du `switch` : il sélectionne la structure de payload en fonction du type de message. Le cas par défaut `(_)` utilise un `GenericPayload` qui lit les octets bruts sans interprétation — utile pour les types qu'on n'a pas encore analysés en détail.

> ⚠️ **Piège** : si la taille calculée d'un payload typé (par exemple `AuthRequestPayload`) ne correspond pas exactement à `payload_len`, ImHex affichera une erreur ou un décalage dans la colorisation. C'est précisément l'intérêt de cette étape : tout décalage signale une erreur dans la spécification qu'il faut corriger avant d'aller plus loin.

### Étape 4 — La séquence complète de messages

Un seul `ProtoMessage` ne suffit pas : le fichier contient une séquence de messages. On déclare un tableau de messages qui consomme tout le fichier :

```hexpat
ProtoMessage messages[while($ < std::mem::size())] @ 0x00;
```

La syntaxe `[while($ < std::mem::size())]` demande à ImHex de continuer à instancier des `ProtoMessage` tant que le curseur de lecture (`$`) n'a pas atteint la fin du fichier. Chaque message est lu séquentiellement : l'en-tête donne la taille du payload, le payload est lu, puis le curseur avance au message suivant.

Si le fichier contient le flux complet (client + serveur mélangés, comme dans un export "Follow TCP Stream"), le pattern décode alternativement les requêtes et les réponses. La colorisation par type rend la conversation parfaitement lisible.

### Étape 5 — Attributs de visualisation

Le langage `.hexpat` permet d'ajouter des attributs de couleur et des commentaires pour enrichir la visualisation :

```hexpat
struct ProtoHeader {
    u8      magic       [[color("FF6B6B")]];  // rouge — repère visuel immédiat
    MsgType msg_type    [[color("4ECDC4")]];  // turquoise
    u16     payload_len [[color("45B7D1")]];  // bleu
} [[format("format_header")]];

fn format_header(ProtoHeader h) {
    return std::format("Type: {} | Payload: {} bytes", h.msg_type, h.payload_len);
};
```

L'attribut `[[color(...)]]` assigne une couleur hexadécimale RGB à chaque champ. L'attribut `[[format(...)]]` associe une fonction de formatage qui contrôle ce qui s'affiche dans le panneau **Data Inspector** et dans les tooltips quand on survole un en-tête.

On peut aussi ajouter des couleurs aux payloads pour distinguer visuellement les requêtes des réponses :

```hexpat
struct HelloPayload {
    char identifier[5]  [[color("A8E6CF")]];  // vert clair
    u8   padding[parent.header.payload_len - 5] [[color("808080")]];
};

struct AuthRequestPayload {
    LPString username [[color("FFD93D")]];    // jaune
    LPString password [[color("FF6B6B")]];    // rouge — attire l'œil sur les credentials
};
```

Le choix des couleurs n'est pas anodin : on utilise des conventions intuitives. Le rouge pour les données sensibles (magic byte, mots de passe), le vert pour les données informatives, le gris pour le padding. Ces conventions aident à repérer immédiatement les zones d'intérêt dans un flux volumineux.

---

## Le pattern complet assemblé

Voici le pattern `.hexpat` complet, prêt à être chargé dans ImHex :

```hexpat
/*!
 * ch23_protocol.hexpat
 * Pattern ImHex pour le protocole custom ch23-network
 * Reconstruit par reverse engineering (sections 23.1 et 23.2)
 */

#pragma endian big
#pragma pattern_limit 1024

import std.io;  
import std.mem;  

// ═══════════════════════════════════
//  Constantes et enums
// ═══════════════════════════════════

#define PROTO_MAGIC 0xC0

enum MsgType : u8 {
    HELLO_REQ    = 0x01,
    AUTH_REQ     = 0x02,
    CMD_REQ      = 0x03,
    QUIT_REQ     = 0x04,
    HELLO_RESP   = 0x81,
    AUTH_RESP    = 0x82,
    CMD_RESP     = 0x83,
    QUIT_RESP    = 0x84,
    ERROR        = 0xFF
};

enum AuthStatus : u8 {
    AUTH_FAIL = 0x00,
    AUTH_OK   = 0x01
};

// ═══════════════════════════════════
//  Structures de base
// ═══════════════════════════════════

struct LPString {
    u8   len                           [[color("AAAAAA")]];
    char value[len]                    [[color("FFD93D")]];
} [[format("format_lpstring")]];

fn format_lpstring(LPString s) {
    return std::format("\"{}\" ({} bytes)", s.value, s.len);
};

// ═══════════════════════════════════
//  En-tête du protocole
// ═══════════════════════════════════

struct ProtoHeader {
    u8      magic                      [[color("FF6B6B")]];
    MsgType msg_type                   [[color("4ECDC4")]];
    u16     payload_len                [[color("45B7D1")]];
} [[static, format("format_header")]];

fn format_header(ProtoHeader h) {
    return std::format("[0x{:02X}] {} — {} bytes",
                       h.magic, h.msg_type, h.payload_len);
};

// ═══════════════════════════════════
//  Payloads par type de message
// ═══════════════════════════════════

struct HelloPayload {
    char identifier[5]                 [[color("A8E6CF")]];
    
    if (parent.header.payload_len > 5) {
        u8 padding[parent.header.payload_len - 5]
                                       [[color("808080")]];
    }
};

struct WelcomePayload {
    char banner[7]                     [[color("A8E6CF")]];
    u8   challenge[parent.header.payload_len - 7]
                                       [[color("C9B1FF")]];
};

struct AuthRequestPayload {
    LPString username;
    LPString password;
};

struct AuthResponsePayload {
    u8         reserved                [[color("808080")]];
    AuthStatus status                  [[color("FF6B6B")]];
};

struct CmdRequestPayload {
    u8 command_id                      [[color("4ECDC4")]];
    
    if (parent.header.payload_len > 1) {
        u8 args[parent.header.payload_len - 1]
                                       [[color("FFD93D")]];
    }
};

struct CmdResponsePayload {
    u8 status_code                     [[color("4ECDC4")]];
    
    if (parent.header.payload_len > 1) {
        u8 data[parent.header.payload_len - 1]
                                       [[color("A8E6CF")]];
    }
};

struct GenericPayload {
    u8 data[parent.header.payload_len] [[color("CCCCCC")]];
};

// ═══════════════════════════════════
//  Message complet (header + payload)
// ═══════════════════════════════════

struct ProtoMessage {
    ProtoHeader header;
    
    // Validation du magic byte
    std::assert(header.magic == PROTO_MAGIC,
                "Invalid magic byte — stream may be misaligned");
    
    if (header.payload_len > 0) {
        match (header.msg_type) {
            (MsgType::HELLO_REQ):    HelloPayload        payload;
            (MsgType::HELLO_RESP):   WelcomePayload      payload;
            (MsgType::AUTH_REQ):     AuthRequestPayload   payload;
            (MsgType::AUTH_RESP):    AuthResponsePayload  payload;
            (MsgType::CMD_REQ):      CmdRequestPayload    payload;
            (MsgType::CMD_RESP):     CmdResponsePayload   payload;
            (_):                     GenericPayload       payload;
        }
    }
} [[format("format_message")]];

fn format_message(ProtoMessage m) {
    return std::format("{} — {} bytes payload",
                       m.header.msg_type, m.header.payload_len);
};

// ═══════════════════════════════════
//  Point d'entrée — flux complet
// ═══════════════════════════════════

ProtoMessage messages[while($ < std::mem::size())] @ 0x00;
```

---

## Lire le résultat dans ImHex

### Le panneau Pattern Data

Une fois le pattern évalué (bouton ▶), le panneau **Pattern Data** (en bas ou sur le côté) affiche une arborescence hiérarchique :

```
▼ messages [6 entries]
  ▼ [0] ProtoMessage — HELLO_REQ — 8 bytes payload
    ▼ header
        magic      = 0xC0
        msg_type   = HELLO_REQ (0x01)
        payload_len = 8
    ▼ payload (HelloPayload)
        identifier = "HELLO"
        padding    = [00 00 00]
  ▼ [1] ProtoMessage — HELLO_RESP — 15 bytes payload
    ▼ header
        magic      = 0xC0
        msg_type   = HELLO_RESP (0x81)
        payload_len = 15
    ▼ payload (WelcomePayload)
        banner    = "WELCOME"
        challenge = [A3 7B 01 F9 8C 22 D4 5E]
  ▼ [2] ProtoMessage — AUTH_REQ — 18 bytes payload
    ...
```

Chaque entrée est cliquable : en cliquant sur un champ, ImHex surligne les octets correspondants dans la vue hexadécimale et positionne le curseur sur leur offset. Inversement, en cliquant sur un octet dans la vue hex, le panneau Pattern Data met en évidence le champ auquel il appartient.

### La vue hexadécimale colorisée

L'effet le plus spectaculaire est la **colorisation de la vue hex**. Chaque champ du protocole est coloré selon les attributs `[[color(...)]]` définis dans le pattern. On voit immédiatement, au premier coup d'œil :

- Les **magic bytes** en rouge, espacés régulièrement dans le flux.  
- Les **types de messages** en turquoise, alternant entre requêtes (`01`, `02`, `03`…) et réponses (`81`, `82`, `83`…).  
- Les **longueurs** en bleu.  
- Les **chaînes** (usernames, banners) en jaune.  
- Le **challenge** en violet.  
- Le **padding** et les champs réservés en gris.

Ce rendu visuel transforme un flux d'octets opaque en une carte structurée du protocole. Les erreurs d'alignement deviennent évidentes : si un champ jaune (chaîne) empiète sur une zone qui devrait être rouge (magic byte), c'est que la spécification est incorrecte quelque part.

### Les bookmarks pour annoter

En complément du pattern, on peut utiliser les **Bookmarks** d'ImHex pour annoter des régions spécifiques :

- Marquer le **challenge** de chaque session avec un bookmark nommé `"Session 1 — challenge"`.  
- Marquer les **credentials** avec un bookmark `"AUTH — admin/password"`.  
- Marquer les **anomalies** si certains octets ne correspondent pas à la spécification.

Les bookmarks sont sauvegardés avec le projet ImHex et persistent entre les sessions — utile pour documenter une analyse de longue durée.

---

## Techniques de débogage du pattern

Écrire un `.hexpat` qui fonctionne du premier coup est rare. Voici les erreurs les plus fréquentes et comment les diagnostiquer.

### Désalignement du flux

**Symptôme** : le premier message se décode correctement, mais le deuxième affiche un magic byte invalide.

**Cause probable** : la taille du payload calculée ne correspond pas à la taille réelle. Le curseur de lecture avance trop ou trop peu, et le message suivant est lu à partir d'un offset incorrect.

**Diagnostic** : vérifier manuellement dans la vue hex que le `payload_len` du premier message correspond bien au nombre d'octets entre la fin de l'en-tête et le prochain `0xC0`. Si le décompte ne colle pas, le problème est soit dans la spécification (longueur mal interprétée — endianness inversée par exemple), soit dans l'export du flux (données manquantes ou dupliquées).

**Correction** : si l'endianness est en cause, remplacer le pragma `#pragma endian big` par `#pragma endian little` (ou inversement) et réévaluer. Si le problème est dans l'export, refaire la capture.

### Payload trop court pour la structure

**Symptôme** : ImHex affiche une erreur `"Array index out of range"` ou `"Pattern extends past end of data"` sur un payload.

**Cause probable** : la structure du payload (par exemple `AuthRequestPayload`) lit plus d'octets que `payload_len` ne l'indique. Cela arrive quand le format des chaînes length-prefixed a été mal interprété (par exemple, l'octet de longueur inclut ou non le terminateur null, ou la longueur est sur 2 octets et non 1).

**Diagnostic** : remplacer temporairement le payload typé par un `GenericPayload` (qui lit exactement `payload_len` octets sans interprétation) et examiner les octets bruts pour vérifier le format réel.

### Le `match` ne sélectionne pas le bon payload

**Symptôme** : un message AUTH est décodé avec la structure `GenericPayload` au lieu de `AuthRequestPayload`.

**Cause probable** : la valeur du `msg_type` ne correspond à aucun cas du `match`. Vérifier que les valeurs dans l'enum `MsgType` correspondent exactement aux octets observés dans le flux.

**Diagnostic** : ajouter temporairement un `std::print(...)` dans le pattern pour afficher la valeur brute du type :

```hexpat
std::print("msg_type = 0x{:02X}", header.msg_type);
```

La sortie apparaît dans la console d'ImHex (panneau **Console** ou **Log**).

---

## Gérer les cas avancés

### Flux bidirectionnel mélangé

Si le fichier contient les deux directions du flux (client et serveur) concaténées par le "Follow TCP Stream" de Wireshark, le pattern ci-dessus fonctionne directement car chaque message — qu'il soit une requête ou une réponse — commence par le même magic byte et suit le même format d'en-tête. Le `match` sur `msg_type` sélectionne automatiquement la bonne structure de payload.

En revanche, si l'on veut **distinguer visuellement** les messages client des messages serveur, on peut ajouter un attribut de couleur conditionnel sur le `ProtoMessage` lui-même :

```hexpat
fn is_response(MsgType t) {
    return (u8(t) & 0x80) != 0;
};

// Dans ProtoMessage, après le header :
if (is_response(header.msg_type)) {
    // Réponse serveur — fond bleu clair via bookmark automatique
    std::print("[SERVER] {} at offset 0x{:X}", header.msg_type, $);
} else {
    std::print("[CLIENT] {} at offset 0x{:X}", header.msg_type, $);
}
```

### Protocole avec champ de longueur incluant l'en-tête

Certains protocoles comptent la longueur totale du message (en-tête inclus) plutôt que la longueur du seul payload. Si c'est le cas, il faut ajuster la lecture du payload :

```hexpat
// Si payload_len inclut les 4 octets d'en-tête :
u8 data[header.payload_len - 4];

// Si payload_len est la longueur du payload seul :
u8 data[header.payload_len];
```

C'est une source d'erreur très fréquente. En cas de doute, on revient aux données : on prend un message dont on connaît le contenu exact (le HELLO avec 8 octets de payload), on vérifie si le champ longueur vaut `8` (payload seul) ou `12` (payload + en-tête de 4 octets), et on ajuste.

### Messages fragmentés ou concaténés

Si le flux brut a été exporté depuis une capture où TCP a concaténé plusieurs messages applicatifs dans un seul segment (Nagle), ou au contraire fragmenté un message en plusieurs segments, l'export "Follow TCP Stream" de Wireshark reconstitue le flux dans l'ordre. Mais un export paquet par paquet (`File → Export Packet Bytes`) ne le fait pas. On obtient alors des morceaux de messages qui ne s'alignent pas sur les frontières protocolaires.

**Solution** : toujours utiliser "Follow TCP Stream" pour l'export, jamais l'export brut de paquets individuels.

---

## Valider le pattern sur plusieurs captures

Un bon pattern ne doit pas fonctionner uniquement sur une capture. Pour s'assurer de sa robustesse, on le teste sur plusieurs scénarios :

- **Authentification réussie** — le cas nominal, déjà couvert.  
- **Authentification échouée** — le serveur renvoie un `AUTH_RESP` avec `status = AUTH_FAIL`. Le pattern doit décoder correctement ce cas (même structure, valeur différente).  
- **Commandes variées** — si le protocole supporte plusieurs types de commandes (`CMD_REQ` avec différents `command_id`), on capture une session utilisant chaque type et on vérifie le décodage.  
- **Erreurs protocolaires** — envoyer un magic byte invalide ou un type inconnu et observer la réponse d'erreur (`MSG_ERROR = 0xFF`). Le pattern `GenericPayload` en cas par défaut doit absorber ces cas sans planter.  
- **Sessions multiples** — concaténer plusieurs sessions dans le même fichier (possible avec `cat session1.bin session2.bin > multi.bin`) pour vérifier que le pattern boucle correctement.

Chaque test qui passe renforce la confiance dans la spécification. Chaque test qui échoue révèle un cas non couvert et enrichit le pattern.

---

## Récapitulatif de la section

| Étape | Action | Résultat |  
|-------|--------|----------|  
| Préparer les données | Export "Follow TCP Stream" depuis Wireshark en raw | Fichier `.bin` contenant le flux applicatif brut |  
| En-tête seul | `ProtoHeader` avec magic, type, longueur | Validation des 4 premiers octets sur le flux |  
| Payloads typés | Structures par type de message + `match` | Décodage complet de chaque message |  
| Séquence complète | Tableau `messages[while(...)]` | Tout le flux décodé d'un coup |  
| Attributs visuels | `[[color(...)]]` + `[[format(...)]]` | Colorisation et annotations lisibles |  
| Validation | Tests sur captures variées (succès, échec, erreurs) | Confiance dans la spécification du protocole |

Le pattern `.hexpat` produit dans cette section est un **artefact permanent** de l'analyse. Il encode la connaissance du protocole sous une forme exécutable et visuellement vérifiable. À la section suivante (**23.4**), on passera du passif à l'actif en rejouant une communication capturée vers le serveur réel pour tester nos hypothèses en conditions dynamiques.

⏭️ [Replay Attack : rejouer une requête capturée](/23-network/04-replay-attack.md)
