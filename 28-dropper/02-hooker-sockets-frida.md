🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 28.2 — Hooker les sockets avec Frida (intercepter `connect`, `send`, `recv`)

> 📍 **Objectif** — Passer de l'observation passive (`strace` + Wireshark) à l'**instrumentation active**. Avec Frida, on injecte du code JavaScript dans le processus du dropper pour intercepter chaque appel aux fonctions réseau de la `libc`. On voit les buffers dans leur intégralité, on peut les décoder à la volée, et on peut même modifier les arguments ou les valeurs de retour — le tout sans patcher ni recompiler le binaire.

---

## Pourquoi Frida plutôt que `strace` ?

`strace` est excellent pour un premier aperçu, mais il présente plusieurs limites dans le cadre d'une analyse approfondie de protocole :

- **Niveau d'abstraction** — `strace` opère au niveau des syscalls (`sendto`, `recvfrom`). On ne voit pas les appels de plus haut niveau (`connect`, `send`, `recv` de la `libc`) avec leurs arguments typés. Surtout, on ne peut pas intercepter des fonctions internes du binaire.  
- **Buffers tronqués** — Même avec `-s 4096`, `strace` impose une limite sur la taille des buffers affichés. Frida n'a pas cette limitation : on accède à toute la mémoire du processus.  
- **Pas de modification** — `strace` est en lecture seule. Il ne permet pas de modifier un argument avant l'appel ou de falsifier une valeur de retour. Frida permet les deux.  
- **Pas de décodage contextuel** — Si les données sont encodées (XOR, base64, compression), `strace` montre les octets bruts. Avec Frida, on peut appeler la fonction de décodage du binaire lui-même, ou implémenter le décodage dans le script JavaScript.  
- **Corrélation avec le code** — Frida permet de hooker non seulement les fonctions `libc`, mais aussi les fonctions internes du binaire (par adresse). On peut ainsi hooker `xor_encode`, `dispatch_command`, `perform_handshake` — des points que `strace` ne voit pas du tout.

En résumé, `strace` répond à « *que se passe-t-il sur le réseau ?* » ; Frida répond à « *que se passe-t-il dans le programme quand il communique ?* ».

---

## Rappel : architecture de Frida

> Cette section suppose que vous maîtrisez les bases de Frida vues au [Chapitre 13](/13-frida/README.md). On en rappelle ici les principes essentiels appliqués au contexte réseau.

Frida fonctionne en injectant un **agent JavaScript** dans l'espace mémoire du processus cible. Cet agent s'exécute dans un runtime V8 (ou QuickJS) embarqué et communique avec le script Python de contrôle via un canal de messages.

L'API centrale pour l'interception est **`Interceptor.attach(target, callbacks)`** :

- **`target`** — L'adresse de la fonction à hooker. Pour les fonctions `libc`, on utilise `Module.getExportByName(null, "connect")` qui résout le symbole dans toutes les bibliothèques chargées.  
- **`callbacks.onEnter(args)`** — Appelé **avant** l'exécution de la fonction originale. On a accès aux arguments via `args[0]`, `args[1]`, etc.  
- **`callbacks.onLeave(retval)`** — Appelé **après** l'exécution. On a accès à la valeur de retour et on peut la modifier.

Pour les fonctions réseau, les arguments suivent les signatures POSIX standard. On les décode en utilisant les types `NativePointer` de Frida et les méthodes de lecture mémoire (`readByteArray`, `readUtf8String`, `readU16`, etc.).

---

## Mise en place : le listener et le dropper

Pour que le dropper aille au-delà de la phase de connexion, il faut qu'un serveur accepte la connexion et réponde au handshake. On utilise un script Python minimal qui parle juste assez du protocole pour maintenir la conversation :

```python
#!/usr/bin/env python3
"""mini_c2.py — Serveur C2 minimal pour tester les hooks Frida.
Accepte la connexion, acquitte le handshake, puis envoie un PING."""

import socket, struct, time

MAGIC = 0xDE  
MSG_ACK   = 0x13  
CMD_PING  = 0x01  
CMD_EXIT  = 0x05  

def make_msg(msg_type, body=b""):
    hdr = struct.pack("<BBH", MAGIC, msg_type, len(body))
    return hdr + body

def recv_msg(sock):
    hdr = sock.recv(4)
    if len(hdr) < 4:
        return None, None, None
    magic, mtype, length = struct.unpack("<BBH", hdr)
    body = sock.recv(length) if length > 0 else b""
    return magic, mtype, body

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 4444))
    srv.listen(1)
    print("[C2] Listening on 127.0.0.1:4444")
    conn, addr = srv.accept()
    print(f"[C2] Connection from {addr}")

    # Recevoir le handshake
    magic, mtype, body = recv_msg(conn)
    print(f"[C2] Handshake: magic=0x{magic:02X} type=0x{mtype:02X} "
          f"body={body}")

    # Acquitter
    conn.sendall(make_msg(MSG_ACK, b"welcome"))
    print("[C2] Sent ACK")

    time.sleep(1)

    # Envoyer un PING
    conn.sendall(make_msg(CMD_PING))
    print("[C2] Sent PING")

    # Attendre le PONG
    magic, mtype, body = recv_msg(conn)
    print(f"[C2] Response: type=0x{mtype:02X}")

    time.sleep(2)

    # Terminer proprement
    conn.sendall(make_msg(CMD_EXIT))
    print("[C2] Sent EXIT")
    time.sleep(1)
    conn.close()
```

Ce mini-C2 sera remplacé par un serveur plus complet en section 28.4. Pour l'instant, il suffit pour générer du trafic bidirectionnel que Frida pourra intercepter.

**Workflow de lancement (trois terminaux) :**

```
Terminal 1 :  python3 mini_c2.py  
Terminal 2 :  frida -l hook_network.js -f ./dropper_O0  
Terminal 3 :  (optionnel) sudo tcpdump -i lo -w cap.pcap port 4444  
```

---

## Hook 1 — `connect` : où le dropper se connecte-t-il ?

Le premier hook cible la fonction `connect()` de la `libc`. Sa signature POSIX est :

```c
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```

L'argument `addr` pointe vers une structure `sockaddr_in` (pour IPv4) dont le layout en mémoire est :

```
Offset  Taille  Champ
0x00    2       sa_family  (AF_INET = 2)
0x02    2       sin_port   (big-endian, réseau)
0x04    4       sin_addr   (big-endian, réseau)
```

Le script Frida pour hooker `connect` et extraire l'adresse de destination :

```javascript
// hook_connect.js — Intercepte connect() et affiche l'adresse cible

Interceptor.attach(Module.getExportByName(null, "connect"), {
    onEnter(args) {
        this.sockfd = args[0].toInt32();
        const sockaddr = args[1];
        const family = sockaddr.readU16();

        if (family === 2) { // AF_INET
            // sin_port est en network byte order (big-endian)
            const portRaw = sockaddr.add(2).readU16();
            const port = ((portRaw & 0xFF) << 8) | ((portRaw >> 8) & 0xFF);

            // sin_addr : 4 octets en network byte order
            const addrRaw = sockaddr.add(4).readU32();
            const ip = [
                addrRaw & 0xFF,
                (addrRaw >> 8) & 0xFF,
                (addrRaw >> 16) & 0xFF,
                (addrRaw >> 24) & 0xFF
            ].join(".");

            this.target = `${ip}:${port}`;
            console.log(`[connect] fd=${this.sockfd} → ${this.target}`);
        }
    },

    onLeave(retval) {
        const ret = retval.toInt32();
        const status = ret === 0 ? "SUCCESS" : `FAILED (${ret})`;
        console.log(`[connect] fd=${this.sockfd} → ${this.target} : ${status}`);
    }
});
```

**Sortie attendue :**

```
[connect] fd=3 → 127.0.0.1:4444
[connect] fd=3 → 127.0.0.1:4444 : SUCCESS
```

> 💡 **Point RE** — Le parsing manuel de `sockaddr_in` dans Frida est un exercice qui renforce la compréhension du layout mémoire des structures réseau. En situation réelle, on retrouvera exactement ce parsing dans Ghidra quand on reconstruira les types au [Chapitre 20](/20-decompilation/04-reconstruire-header.md).

---

## Hook 2 — `send` : que dit le dropper au C2 ?

La fonction `send()` de la `libc` a la signature suivante :

```c
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
```

Le hook capture le buffer envoyé et le décompose selon le format protocolaire identifié en section 28.1 :

```javascript
// hook_send.js — Intercepte send() et décode le header protocolaire

const PROTO_MAGIC = 0xDE;  
const MSG_TYPES = {  
    0x10: "HANDSHAKE", 0x11: "PONG",  0x12: "RESULT",
    0x13: "ACK",       0x14: "ERROR", 0x15: "BEACON"
};

Interceptor.attach(Module.getExportByName(null, "send"), {
    onEnter(args) {
        this.sockfd = args[0].toInt32();
        this.buf    = args[1];
        this.len    = args[2].toInt32();
        this.flags  = args[3].toInt32();

        if (this.len < 4) return;

        const magic = this.buf.readU8();
        if (magic !== PROTO_MAGIC) {
            console.log(`[send] fd=${this.sockfd} len=${this.len} (non-protocol data)`);
            return;
        }

        const msgType  = this.buf.add(1).readU8();
        const bodyLen  = this.buf.add(2).readU16(); // little-endian (x86-64 natif)
        const typeName = MSG_TYPES[msgType] || `UNKNOWN(0x${msgType.toString(16)})`;

        console.log(`[send] fd=${this.sockfd} | magic=0xDE | type=${typeName} (0x${msgType.toString(16)}) | body_len=${bodyLen}`);

        // Dump hex du body
        if (bodyLen > 0 && this.len >= 4 + bodyLen) {
            const body = this.buf.add(4).readByteArray(bodyLen);
            console.log("  body (hex): " + hexdump(body, { header: false, ansi: true }));
        }
    },

    onLeave(retval) {
        const sent = retval.toInt32();
        if (sent < 0) {
            console.log(`[send] fd=${this.sockfd} FAILED (returned ${sent})`);
        }
    }
});
```

**Sortie attendue lors du handshake :**

```
[send] fd=3 | magic=0xDE | type=HANDSHAKE (0x10) | body_len=20
  body (hex):           0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
             00000000  6d 79 68 6f 73 74 00 31  32 33 34 00 44 52 50 2d  myhost.1234.DRP-
             00000010  31 2e 30 00                                        1.0.
```

On voit clairement le hostname (`myhost`), le PID (`1234`) et la version (`DRP-1.0`), séparés par des null bytes. Ce sont les trois champs du handshake, lisibles en clair car le handshake n'est **pas** encodé en XOR (seuls les commandes `CMD_EXEC` et `CMD_DROP` le sont).

---

## Hook 3 — `recv` : que dit le C2 au dropper ?

La signature de `recv()` :

```c
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
```

Pour `recv`, le buffer est rempli **après** l'appel. On doit donc lire les données dans `onLeave`, une fois que le noyau a écrit dans le buffer :

```javascript
// hook_recv.js — Intercepte recv() et décode les commandes du C2

const CMD_TYPES = {
    0x01: "PING", 0x02: "EXEC", 0x03: "DROP",
    0x04: "SLEEP", 0x05: "EXIT"
};
const XOR_KEY = 0x5A;

function xorDecode(bytes) {
    const decoded = new Uint8Array(bytes);
    for (let i = 0; i < decoded.length; i++) {
        decoded[i] ^= XOR_KEY;
    }
    return decoded.buffer;
}

Interceptor.attach(Module.getExportByName(null, "recv"), {
    onEnter(args) {
        this.sockfd = args[0].toInt32();
        this.buf    = args[1];
        this.size   = args[2].toInt32();
    },

    onLeave(retval) {
        const received = retval.toInt32();
        if (received <= 0) return;

        // On ne décode que si on a au moins un header complet
        if (received < 4) {
            console.log(`[recv] fd=${this.sockfd} ${received} bytes (partial header)`);
            return;
        }

        const magic = this.buf.readU8();
        if (magic !== 0xDE) {
            console.log(`[recv] fd=${this.sockfd} ${received} bytes (non-protocol)`);
            return;
        }

        const msgType = this.buf.add(1).readU8();
        const bodyLen = this.buf.add(2).readU16();
        const typeName = CMD_TYPES[msgType] || `RESP(0x${msgType.toString(16)})`;

        console.log(`[recv] fd=${this.sockfd} | magic=0xDE | type=${typeName} (0x${msgType.toString(16)}) | body_len=${bodyLen}`);

        if (bodyLen > 0 && received >= 4 + bodyLen) {
            const bodyRaw = this.buf.add(4).readByteArray(bodyLen);
            console.log("  body (raw hex):");
            console.log(hexdump(bodyRaw, { header: false, ansi: true }));

            // Tentative de décodage XOR pour les commandes EXEC et DROP
            if (msgType === 0x02 || msgType === 0x03) {
                const decoded = xorDecode(bodyRaw);
                console.log("  body (XOR-decoded):");
                console.log(hexdump(decoded, { header: false, ansi: true }));
            }
        }
    }
});
```

**Points importants dans ce hook :**

- La lecture se fait dans **`onLeave`** — c'est la différence fondamentale avec le hook de `send` où on lit dans `onEnter`. Le buffer de `recv` est vide à l'entrée de la fonction ; c'est le noyau qui le remplit pendant l'exécution.  
- Le **décodage XOR** est implémenté directement dans le hook. Pour `CMD_EXEC` (type `0x02`) et `CMD_DROP` (type `0x03`), le body est encodé avec la clé `0x5A`. Le hook affiche à la fois la version brute et la version décodée, ce qui permet de vérifier immédiatement si notre hypothèse sur l'encodage est correcte.  
- La vérification du **magic byte** (`0xDE`) filtre les données non-protocolaires (par exemple, du trafic réseau parasite si le dropper ouvrait d'autres sockets).

---

## Script complet : tout assembler

En pratique, on combine les trois hooks dans un seul fichier JavaScript. Voici le script unifié, enrichi de quelques fonctionnalités supplémentaires :

```javascript
// hook_network.js — Script Frida complet pour l'analyse réseau du dropper
// Usage : frida -l hook_network.js -f ./dropper_O0

"use strict";

const PROTO_MAGIC = 0xDE;  
const XOR_KEY     = 0x5A;  

// Tables de correspondance type → nom lisible
const CLIENT_MSG = {
    0x10: "HANDSHAKE", 0x11: "PONG",   0x12: "RESULT",
    0x13: "ACK",       0x14: "ERROR",  0x15: "BEACON"
};
const SERVER_CMD = {
    0x01: "PING",  0x02: "EXEC",  0x03: "DROP",
    0x04: "SLEEP", 0x05: "EXIT"
};
// Merge pour lookup générique
const ALL_TYPES = Object.assign({}, CLIENT_MSG, SERVER_CMD);

// ─── Utilitaires ────────────────────────────────────────────

function xorBuf(arrayBuf) {
    const u8 = new Uint8Array(arrayBuf);
    const out = new Uint8Array(u8.length);
    for (let i = 0; i < u8.length; i++) out[i] = u8[i] ^ XOR_KEY;
    return out.buffer;
}

function parseAddr(sockaddrPtr) {
    const family = sockaddrPtr.readU16();
    if (family !== 2) return null; // AF_INET uniquement
    const portRaw = sockaddrPtr.add(2).readU16();
    const port = ((portRaw & 0xFF) << 8) | ((portRaw >> 8) & 0xFF);
    const raw = sockaddrPtr.add(4).readU32();
    const ip = [raw & 0xFF, (raw >> 8) & 0xFF,
                (raw >> 16) & 0xFF, (raw >> 24) & 0xFF].join(".");
    return { ip, port };
}

function decodeProtoHeader(ptr, len) {
    if (len < 4) return null;
    const magic   = ptr.readU8();
    if (magic !== PROTO_MAGIC) return null;
    const msgType = ptr.add(1).readU8();
    const bodyLen = ptr.add(2).readU16();
    const name    = ALL_TYPES[msgType] || `0x${msgType.toString(16)}`;
    return { magic, msgType, bodyLen, name };
}

function logBody(ptr, hdr, direction) {
    if (hdr.bodyLen === 0) return;
    const body = ptr.add(4).readByteArray(hdr.bodyLen);
    console.log(`  [${direction}] body (raw):`);
    console.log(hexdump(body, { header: false, ansi: true }));

    // Décodage XOR pour EXEC, DROP, et RESULT
    if (hdr.msgType === 0x02 || hdr.msgType === 0x03 ||
        hdr.msgType === 0x12) {
        const decoded = xorBuf(body);
        console.log(`  [${direction}] body (XOR 0x5A decoded):`);
        console.log(hexdump(decoded, { header: false, ansi: true }));
    }
}

// ─── Hook : connect() ──────────────────────────────────────

Interceptor.attach(Module.getExportByName(null, "connect"), {
    onEnter(args) {
        this.fd   = args[0].toInt32();
        this.addr = parseAddr(args[1]);
    },
    onLeave(retval) {
        if (!this.addr) return;
        const ok = retval.toInt32() === 0 ? "OK" : "FAIL";
        console.log(`\n[connect] fd=${this.fd} → ${this.addr.ip}:${this.addr.port} [${ok}]`);
    }
});

// ─── Hook : send() ─────────────────────────────────────────

Interceptor.attach(Module.getExportByName(null, "send"), {
    onEnter(args) {
        const fd  = args[0].toInt32();
        const buf = args[1];
        const len = args[2].toInt32();

        const hdr = decodeProtoHeader(buf, len);
        if (hdr) {
            console.log(`\n[send >>>] fd=${fd} | ${hdr.name} (0x${hdr.msgType.toString(16)}) | body=${hdr.bodyLen}B | total=${len}B`);
            logBody(buf, hdr, "send");
        } else {
            console.log(`\n[send >>>] fd=${fd} | raw ${len}B`);
        }
    }
});

// ─── Hook : recv() ─────────────────────────────────────────

Interceptor.attach(Module.getExportByName(null, "recv"), {
    onEnter(args) {
        this.fd  = args[0].toInt32();
        this.buf = args[1];
    },
    onLeave(retval) {
        const received = retval.toInt32();
        if (received <= 0) return;

        const hdr = decodeProtoHeader(this.buf, received);
        if (hdr) {
            console.log(`\n[recv <<<] fd=${this.fd} | ${hdr.name} (0x${hdr.msgType.toString(16)}) | body=${hdr.bodyLen}B | total=${received}B`);
            logBody(this.buf, hdr, "recv");
        } else {
            console.log(`\n[recv <<<] fd=${this.fd} | raw ${received}B`);
        }
    }
});

// ─── Hook : close() ────────────────────────────────────────

Interceptor.attach(Module.getExportByName(null, "close"), {
    onEnter(args) {
        const fd = args[0].toInt32();
        if (fd > 2) { // ignorer stdin/stdout/stderr
            console.log(`\n[close] fd=${fd}`);
        }
    }
});

console.log("=== hook_network.js loaded ===");  
console.log("Hooks active: connect, send, recv, close");  
```

### Lancement

```bash
$ frida -l hook_network.js -f ./dropper_O0 --no-pause
```

L'option **`-f`** lance le binaire via Frida (*spawn mode*), ce qui garantit que les hooks sont en place **avant** le premier appel à `connect`. Sans cette option (mode *attach*), on risquerait de rater la connexion initiale et le handshake.

L'option **`--no-pause`** reprend immédiatement l'exécution du processus après l'injection de l'agent. Sans elle, Frida met le processus en pause au point d'entrée, et il faut taper `%resume` dans la console Frida pour continuer.

---

## Lecture de la sortie : session complète annotée

Voici une session typique avec `mini_c2.py` en écoute, annotée pour servir de référence :

```
=== hook_network.js loaded ===
Hooks active: connect, send, recv, close

[connect] fd=3 → 127.0.0.1:4444 [OK]              ← connexion TCP établie

[send >>>] fd=3 | HANDSHAKE (0x10) | body=20B | total=24B
  [send] body (raw):                                ← handshake en clair
             00000000  6d 79 68 6f 73 74 00 31  32 33 34 00 44 52 50 2d  myhost.1234.DRP-
             00000010  31 2e 30 00                                        1.0.

[recv <<<] fd=3 | ACK (0x13) | body=7B | total=11B  ← le C2 acquitte
  [recv] body (raw):
             00000000  77 65 6c 63 6f 6d 65                               welcome

[recv <<<] fd=3 | PING (0x01) | body=0B | total=4B  ← PING sans body

[send >>>] fd=3 | PONG (0x11) | body=0B | total=4B  ← réponse PONG

[recv <<<] fd=3 | EXIT (0x05) | body=0B | total=4B  ← ordre de terminaison

[send >>>] fd=3 | ACK (0x13) | body=3B | total=7B   ← acquittement "bye"
  [send] body (raw):
             00000000  62 79 65                                            bye

[close] fd=3                                         ← fermeture socket
```

Chaque ligne correspond exactement à ce que `strace` montrait, mais avec une lisibilité incomparablement supérieure : les types de messages sont nommés, les bodies sont décodés, et la direction (envoi/réception) est explicite.

> ⚠️ **Précision** — Les lignes `[send >>>]` reflètent fidèlement ce que le hook voit (un seul `send()` par message, grâce au buffer unifié de `send_message`). Les lignes `[recv <<<]` sont une **vue simplifiée** : en réalité, pour les messages avec body (comme ACK/7B), le hook `recv` se déclenche deux fois (4B header + 7B body séparément). Les messages sans body (PING, EXIT, PONG) ne nécessitent qu'un seul `recv` et s'affichent correctement. Pour obtenir la vue unifiée montrée ici, il faut hooker la fonction interne `recv_message()` plutôt que le `recv` de la libc — voir la section « Pièges courants » plus bas.

---

## Aller plus loin : hooks sur les fonctions internes

L'avantage majeur de Frida sur `strace` est la possibilité de hooker les fonctions **internes** du binaire, pas seulement celles de la `libc`. Si le binaire possède des symboles (variante `_O0`), on peut résoudre les fonctions par nom :

### Hooker `xor_encode` — observer l'encodage en action

> ⚠️ **Attention** — Dans le code source du dropper, `xor_encode` est déclarée `static`. Les fonctions `static` ne sont **pas** des exports dynamiques : elles n'apparaissent pas dans `.dynsym` et `Module.getExportByName()` ne les trouvera pas, même sur un binaire compilé avec `-g`. En revanche, les symboles de débogage DWARF les référencent dans `.symtab`. Frida expose cette table via **`DebugSymbol.getFunctionByName()`**, qui fonctionne sur les binaires non strippés.

```javascript
// hook_xor.js — Intercepte la fonction xor_encode() interne du dropper
//
// xor_encode est `static` → pas dans .dynsym → on utilise DebugSymbol
// qui lit .symtab / DWARF (disponible sur _O0 et _O2, pas sur _strip)

const xorEncode = DebugSymbol.getFunctionByName("xor_encode");

if (!xorEncode.isNull()) {
    Interceptor.attach(xorEncode, {
        onEnter(args) {
            this.buf = args[0];
            this.len = args[1].toInt32();

            // Lire le buffer AVANT le XOR
            console.log(`\n[xor_encode] BEFORE — ${this.len} bytes:`);
            console.log(hexdump(this.buf.readByteArray(this.len),
                        { header: false, ansi: true }));
        },
        onLeave(retval) {
            // Lire le buffer APRÈS le XOR
            console.log(`[xor_encode] AFTER  — ${this.len} bytes:`);
            console.log(hexdump(this.buf.readByteArray(this.len),
                        { header: false, ansi: true }));
        }
    });
    console.log("[+] Hooked xor_encode at " + xorEncode);
} else {
    console.log("[-] xor_encode not found in debug symbols (stripped binary?)");
    console.log("    → Use Ghidra to find the offset, then hook by address.");
}
```

Ce hook montre le buffer **avant** et **après** l'application du XOR, ce qui confirme définitivement la clé utilisée et le périmètre de l'encodage. On voit en clair la commande shell reçue (avant XOR) puis sa version encodée (après XOR, juste avant l'envoi).

> 💡 **Point RE** — Quand on passera à la variante strippée (`_O2_strip`), les symboles de débogage sont absents : ni `.symtab` ni DWARF. `DebugSymbol.getFunctionByName()` retournera un pointeur nul. Il faudra d'abord identifier l'adresse de `xor_encode` dans Ghidra (par exemple via la constante `0x5A` dans le désassemblage), puis fournir cette adresse directement à Frida :  
>  
> ```javascript  
> const base = Module.getBaseAddress("dropper_O2_strip");  
> const xorEncode = base.add(0x1a30); // offset trouvé dans Ghidra  
> Interceptor.attach(xorEncode, { ... });  
> ```

### Hooker `dispatch_command` — voir la machine à états

Même remarque que pour `xor_encode` : `dispatch_command` est `static`, on utilise donc `DebugSymbol` :

```javascript
// hook_dispatch.js — Intercepte dispatch_command() pour loguer chaque commande

const dispatch = DebugSymbol.getFunctionByName("dispatch_command");

if (!dispatch.isNull()) {
    Interceptor.attach(dispatch, {
        onEnter(args) {
            // args[0] = dropper_state_t*, args[1] = proto_message_t*
            const msgPtr = args[1];
            const msgType = msgPtr.add(1).readU8();    // offset du champ type
            const bodyLen = msgPtr.add(2).readU16();   // offset du champ length
            const typeName = {
                0x01: "PING", 0x02: "EXEC", 0x03: "DROP",
                0x04: "SLEEP", 0x05: "EXIT"
            }[msgType] || "??";

            console.log(`\n[dispatch] command=${typeName} (0x${msgType.toString(16)}) body_len=${bodyLen}`);
        },
        onLeave(retval) {
            console.log(`[dispatch] returned ${retval.toInt32()}`);
        }
    });
    console.log("[+] Hooked dispatch_command at " + dispatch);
}
```

En hookant le dispatcher, on observe la **machine à états** du dropper depuis l'intérieur : chaque commande qui entre, la valeur de retour du handler, et l'enchaînement des commandes. C'est un complément idéal aux hooks réseau : on corrèle « ce qui arrive sur la socket » avec « ce que le dropper en fait ».

---

## Gérer le cas du binaire strippé

Sur la variante `dropper_O2_strip`, les symboles de débogage (`.symtab`, DWARF) ont été supprimés par `strip`. `DebugSymbol.getFunctionByName()` retourne un pointeur nul pour les fonctions internes (`xor_encode`, `dispatch_command`, `perform_handshake`…). Les hooks sur les fonctions `libc` (`connect`, `send`, `recv`) continuent de fonctionner normalement puisque ces symboles résident dans les bibliothèques partagées, pas dans le binaire.

Pour hooker les fonctions internes d'un binaire strippé, la démarche est la suivante :

1. **Identifier la fonction dans Ghidra** — Chercher la constante XOR `0x5A`, les chaînes de format, les patterns de `switch/case` du dispatcher. Ghidra attribuera des noms automatiques comme `FUN_00101a30`.

2. **Calculer l'offset depuis la base du module** — Dans Ghidra, l'adresse affichée est l'adresse *dans l'image* (avant relocation). L'offset est `adresse_ghidra - base_image_ghidra`. Pour un PIE, la base image dans Ghidra est typiquement `0x00100000`.

3. **Appliquer l'offset à la base réelle en mémoire** — Dans Frida, `Module.getBaseAddress("dropper_O2_strip")` retourne la base réelle (après ASLR). On additionne l'offset.

```javascript
// Exemple pour un binaire strippé PIE
const mod  = Process.getModuleByName("dropper_O2_strip");  
const base = mod.base;  

// Offsets trouvés dans Ghidra (base image = 0x100000)
const OFF_XOR_ENCODE = 0x1a30;      // FUN_00101a30  
const OFF_DISPATCH   = 0x1d80;      // FUN_00101d80  

Interceptor.attach(base.add(OFF_XOR_ENCODE), {
    onEnter(args) {
        console.log("[xor_encode] called");
        // ... même logique que précédemment
    }
});
```

> 💡 **Point RE** — Cette technique de « pont Ghidra → Frida par offset » est fondamentale en analyse de malware. On identifie les fonctions intéressantes en statique, puis on les instrumente en dynamique. Les deux approches se renforcent mutuellement.

---

## Pièges courants et solutions

### Le dropper utilise `sendto`/`recvfrom` au lieu de `send`/`recv`

Selon la version de la `libc` et le niveau d'optimisation, les appels C `send()` et `recv()` peuvent être implémentés en interne comme `sendto()` et `recvfrom()` avec les arguments d'adresse à `NULL`. Si vos hooks `send`/`recv` ne se déclenchent pas, ajoutez des hooks sur `sendto` et `recvfrom` :

```javascript
Interceptor.attach(Module.getExportByName(null, "sendto"), {
    onEnter(args) {
        // args : sockfd, buf, len, flags, dest_addr, addrlen
        // Pour TCP, dest_addr est NULL — même logique que send()
        // ...
    }
});
```

Le problème inverse peut aussi se produire (vos hooks `sendto` ne se déclenchent pas car la `libc` utilise directement `send`). La solution robuste est de **hooker les deux** et de dédupliquer dans votre logique si nécessaire.

### Le hook `recv` voit deux appels par message protocolaire

La fonction interne `recv_message()` du dropper appelle `recv_all()` **deux fois** pour chaque message : une première fois pour le **header** (4 octets) afin de connaître la taille du body, puis une seconde fois pour le **body** (N octets). Chaque `recv_all()` appelle `recv()` en boucle, ce qui signifie que le hook Frida sur `recv` se déclenche **au moins deux fois** par message protocolaire.

En pratique sur une socket locale (`127.0.0.1`), chaque `recv_all()` aboutit généralement en un seul appel `recv()` (le noyau a assez de données en buffer). Le hook voit donc :

1. **Premier `recv`** → 4 octets (header) — le hook parse le magic, le type et la longueur, mais le body n'est pas encore dans le buffer.  
2. **Deuxième `recv`** → N octets (body) — le hook voit des données brutes sans magic byte et les affiche comme « non-protocol ».

C'est pourquoi l'exemple de sortie annotée dans cette section (et le script unifié `hook_network.js`) correspond à un scénario **simplifié**. En réalité, les messages reçus apparaissent en deux fragments dans la sortie du hook.

Pour reconstituer les messages complets, trois approches :

- **Hooker `recv_message()` directement** — C'est la fonction interne du dropper qui retourne un message complet (header + body) dans une structure `proto_message_t` contiguë. Avec `DebugSymbol.getFunctionByName("recv_message")` sur la variante `_O0`, ou par offset sur la variante strippée. C'est l'approche la plus fiable.  
- **Accumuler dans le hook** — Maintenir un buffer d'accumulation dans le script JavaScript et reconstituer les messages en parsant progressivement les headers. Plus complexe mais fonctionne sur n'importe quel binaire.  
- **Accepter les fragments** — Les hooks sur `send`/`recv` libc restent utiles pour confirmer les tailles et les timestamps, même s'ils ne donnent pas la vue « un message = une ligne ».

> 💡 **Pourquoi le hook `send` n'a pas ce problème** — La fonction `send_message()` du dropper assemble le header et le body dans un **buffer unique** avant d'appeler `send_all()`. Il n'y a donc qu'un seul appel `send()` par message protocolaire, et le hook Frida voit la totalité du message d'un coup.

### Frida crash avec `Process.getModuleByName` introuvable

Si le binaire est lancé en mode spawn (`-f`) et que l'agent tente d'accéder au module avant que le loader ne l'ait chargé, on obtient une erreur. Utiliser `--no-pause` et placer les hooks dans le corps principal du script (pas dans un callback asynchrone) résout généralement le problème. Si le souci persiste, on peut envelopper l'initialisation dans un `setTimeout` :

```javascript
setTimeout(function() {
    // hooks ici
}, 100);
```

---

## Ce que l'on sait après cette phase

Les hooks Frida nous ont permis de confirmer et d'enrichir les observations de la section 28.1 :

| Élément | Source 28.1 (strace/Wireshark) | Confirmé/Enrichi par Frida (28.2) |  
|---|---|---|  
| Adresse C2 | `127.0.0.1:4444` | Confirmé via hook `connect` |  
| Magic byte | `0xDE` (octet brut dans les paquets) | Confirmé — constant sur tous les messages |  
| Format header | `[magic][type][length][body]` | Confirmé — structure `packed` de 4 octets |  
| Handshake | Body = hostname + PID + version | Confirmé — visible en clair dans le hook `send` |  
| Encodage XOR | Hypothèse basée sur `strings` | **Confirmé** : clé `0x5A`, appliqué sur EXEC et DROP uniquement |  
| Machine à états | Inconnue | **Identifiée** via hook `dispatch_command` : 5 commandes |

La section suivante (28.3) utilisera toutes ces données pour **formaliser le protocole C2** : format exact de chaque commande, diagramme de séquence, machine à états complète, et spécification suffisante pour écrire un client ou un serveur compatible.

---

> **À suivre** — En section 28.3, on consolide toutes nos observations en une **spécification complète du protocole C2** : format des messages, diagramme de séquence du handshake, table des commandes, et machine à états du dropper.

⏭️ [RE du protocole C2 custom (commandes, encodage, handshake)](/28-dropper/03-re-protocole-c2.md)
