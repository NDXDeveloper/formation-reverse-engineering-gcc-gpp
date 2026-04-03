🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 13.4 — Intercepter les appels à `malloc`, `free`, `open`, fonctions customs

> 🧰 **Outils utilisés** : `frida`, Python 3 + module `frida`  
> 📦 **Binaires utilisés** : `binaries/ch13-keygenme/keygenme_O0`, `binaries/ch14-crypto/crypto_O0`, `binaries/ch23-network/server_O0`  
> 📖 **Prérequis** : [13.3 — Hooking de fonctions C et C++](/13-frida/03-hooking-fonctions-c-cpp.md)

---

## De la théorie à la pratique terrain

La section 13.3 a posé les fondations : `Interceptor.attach`, résolution de symboles, lecture d'arguments, filtrage. Cette section les applique à trois familles de fonctions que le reverse engineer intercepte constamment — les allocations mémoire (`malloc`/`free`), les opérations fichier et réseau (`open`, `read`, `write`, `send`, `recv`), et les fonctions applicatives propres au binaire analysé. Chaque famille présente des défis spécifiques et des patterns de hooking éprouvés.

---

## Intercepter les allocations mémoire : `malloc` et `free`

### Pourquoi tracer les allocations ?

En RE, tracer les allocations mémoire répond à plusieurs questions fondamentales. Quelle quantité de mémoire le programme utilise-t-il, et comment cette consommation évolue-t-elle au fil du temps ? Des buffers sont-ils alloués pour stocker des données sensibles (clés crypto, mots de passe déchiffrés, tokens) ? Y a-t-il des fuites mémoire qui pourraient révéler une logique interne défaillante ? Quel est le cycle de vie d'une structure de données — quand est-elle créée, remplie, consommée, libérée ?

Avec GDB, répondre à ces questions exige des breakpoints conditionnels sur `malloc` et `free`, une inspection manuelle des registres à chaque arrêt, et une patience considérable. Avec Frida, on automatise l'ensemble et on observe le flux d'allocations en temps réel, sans interrompre le programme.

### Hook basique sur `malloc`

Rappelons la signature :

```c
void *malloc(size_t size);
```

Un seul argument (`size`, passé dans `rdi`), et la valeur de retour est le pointeur vers la zone allouée.

```javascript
Interceptor.attach(Module.findExportByName(null, "malloc"), {
    onEnter(args) {
        this.size = args[0].toInt32();
    },
    onLeave(retval) {
        if (this.size > 0) {
            console.log(`malloc(${this.size}) = ${retval}`);
        }
    }
});
```

> ⚠️ **Attention au volume.** Un programme typique effectue des milliers d'appels à `malloc` par seconde — chaque `printf`, chaque manipulation de `std::string`, chaque opération interne de la libc déclenche des allocations. Sans filtrage, la sortie est inexploitable. Les sections suivantes montrent comment réduire le bruit.

### Filtrer par taille

Souvent, les allocations intéressantes ont des tailles caractéristiques. Un buffer AES-256 fait 32 octets. Un buffer de lecture réseau fait typiquement 1024, 4096 ou 8192 octets. On peut filtrer par taille :

```javascript
Interceptor.attach(Module.findExportByName(null, "malloc"), {
    onEnter(args) {
        this.size = args[0].toInt32();
    },
    onLeave(retval) {
        // Ne logger que les allocations entre 16 et 256 octets
        // (plage typique pour des clés crypto, tokens, petites structures)
        if (this.size >= 16 && this.size <= 256) {
            console.log(`malloc(${this.size}) = ${retval}`);
        }
    }
});
```

### Filtrer par appelant

Technique plus puissante : ne capturer que les `malloc` appelés depuis le binaire principal, en ignorant les allocations internes de la libc et des bibliothèques.

```javascript
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

Interceptor.attach(Module.findExportByName(null, "malloc"), {
    onEnter(args) {
        this.size = args[0].toInt32();
        this.fromMain = this.returnAddress.compare(modBase) >= 0 &&
                        this.returnAddress.compare(modEnd) < 0;
    },
    onLeave(retval) {
        if (this.fromMain) {
            console.log(`[main] malloc(${this.size}) = ${retval}`);
            console.log(`  appelé depuis ${DebugSymbol.fromAddress(this.returnAddress)}`);
        }
    }
});
```

Ce pattern réduit drastiquement le bruit. On ne voit plus que les allocations initiées par le code du binaire analysé — celles qui reflètent sa logique interne.

### Hook sur `free` et corrélation

```c
void free(void *ptr);
```

```javascript
Interceptor.attach(Module.findExportByName(null, "free"), {
    onEnter(args) {
        const ptr = args[0];
        if (!ptr.isNull()) {
            console.log(`free(${ptr})`);
        }
    }
});
```

L'intérêt de hooker `free` seul est limité. La puissance apparaît quand on **corrèle** les allocations et les libérations pour suivre le cycle de vie des buffers :

```javascript
const allocations = new Map();

Interceptor.attach(Module.findExportByName(null, "malloc"), {
    onEnter(args) {
        this.size = args[0].toInt32();
    },
    onLeave(retval) {
        if (this.size >= 16 && !retval.isNull()) {
            allocations.set(retval.toString(), {
                size: this.size,
                caller: DebugSymbol.fromAddress(this.returnAddress).toString(),
                time: Date.now()
            });
        }
    }
});

Interceptor.attach(Module.findExportByName(null, "free"), {
    onEnter(args) {
        const key = args[0].toString();
        if (allocations.has(key)) {
            const info = allocations.get(key);
            const lifetime = Date.now() - info.time;
            console.log(`free(${key}) — était malloc(${info.size}) `
                      + `depuis ${info.caller}, vivant ${lifetime}ms`);
            allocations.delete(key);
        }
    }
});

// Périodiquement, afficher les allocations non libérées
setInterval(() => {
    if (allocations.size > 0) {
        console.log(`\n[*] ${allocations.size} allocations en vol :`);
        allocations.forEach((info, ptr) => {
            console.log(`  ${ptr} : ${info.size} octets depuis ${info.caller}`);
        });
    }
}, 5000);
```

Ce script construit un tracker d'allocations en temps réel. Chaque `malloc` enregistre le pointeur retourné, la taille, l'appelant et l'horodatage. Chaque `free` retrouve l'allocation correspondante et affiche sa durée de vie. Les allocations qui ne sont jamais libérées (les fuites, ou les buffers persistants) restent dans la map et sont affichées périodiquement.

En contexte de RE crypto (chapitre 24), ce tracker révèle les buffers alloués pour stocker les clés et les IV — ils ont une taille caractéristique (16, 24 ou 32 octets pour AES) et sont souvent alloués par une fonction identifiable (`init_cipher`, `generate_key`…).

### `calloc` et `realloc`

Pour une couverture complète, il faut aussi hooker `calloc` et `realloc` :

```c
void *calloc(size_t nmemb, size_t size);  
void *realloc(void *ptr, size_t size);  
```

```javascript
Interceptor.attach(Module.findExportByName(null, "calloc"), {
    onEnter(args) {
        this.nmemb = args[0].toInt32();
        this.size = args[1].toInt32();
    },
    onLeave(retval) {
        const total = this.nmemb * this.size;
        console.log(`calloc(${this.nmemb}, ${this.size}) [${total} octets] = ${retval}`);
    }
});

Interceptor.attach(Module.findExportByName(null, "realloc"), {
    onEnter(args) {
        this.oldPtr = args[0];
        this.newSize = args[1].toInt32();
    },
    onLeave(retval) {
        console.log(`realloc(${this.oldPtr}, ${this.newSize}) = ${retval}`);
    }
});
```

### Lire le contenu d'un buffer après allocation

`malloc` retourne un pointeur vers de la mémoire non initialisée. Le contenu intéressant n'y sera écrit que plus tard, par le code applicatif. Lire le buffer dans le `onLeave` de `malloc` ne donne donc rien d'utile.

La stratégie est de noter l'adresse et la taille lors du `malloc`, puis de lire le contenu au moment opportun — dans le `onEnter` d'une fonction qui consomme ce buffer (par exemple `send`, `write`, ou une fonction crypto), ou à un breakpoint logique plus avancé dans l'exécution. Nous verrons cette technique en détail dans les exemples qui suivent.

---

## Intercepter les opérations fichier : `open`, `read`, `write`, `close`

### Tracer les accès fichier

Observer quels fichiers un programme ouvre, lit et écrit est l'un des premiers réflexes du reverse engineer. `strace` le fait passivement (chapitre 5, section 5.5), mais Frida permet d'aller plus loin : filtrer, modifier les arguments, lire les buffers, corréler les opérations.

### Hook sur `open` / `openat`

Sous Linux moderne, `openat` est l'appel système réel pour la quasi-totalité des ouvertures de fichier. La fonction `open` de la libc est souvent un wrapper autour d'`openat`. Pour être exhaustif, hookez les deux :

```javascript
// open(const char *pathname, int flags, mode_t mode)
Interceptor.attach(Module.findExportByName(null, "open"), {
    onEnter(args) {
        this.path = args[0].readUtf8String();
        this.flags = args[1].toInt32();
    },
    onLeave(retval) {
        const fd = retval.toInt32();
        console.log(`open("${this.path}", 0x${this.flags.toString(16)}) = fd ${fd}`);
    }
});

// openat(int dirfd, const char *pathname, int flags, mode_t mode)
Interceptor.attach(Module.findExportByName(null, "openat"), {
    onEnter(args) {
        this.dirfd = args[0].toInt32();
        this.path = args[1].readUtf8String();
        this.flags = args[2].toInt32();
    },
    onLeave(retval) {
        const fd = retval.toInt32();
        console.log(`openat(${this.dirfd}, "${this.path}", 0x${this.flags.toString(16)}) = fd ${fd}`);
    }
});
```

### Corréler les file descriptors

Le pattern le plus puissant pour le traçage fichier consiste à maintenir une table de correspondance entre file descriptors et chemins, puis à utiliser cette table dans les hooks de `read`/`write` :

```javascript
const fdMap = new Map();

Interceptor.attach(Module.findExportByName(null, "open"), {
    onEnter(args) {
        this.path = args[0].readUtf8String();
    },
    onLeave(retval) {
        const fd = retval.toInt32();
        if (fd >= 0) {
            fdMap.set(fd, this.path);
        }
    }
});

Interceptor.attach(Module.findExportByName(null, "close"), {
    onEnter(args) {
        const fd = args[0].toInt32();
        if (fdMap.has(fd)) {
            console.log(`close(fd ${fd}) → "${fdMap.get(fd)}"`);
            fdMap.delete(fd);
        }
    }
});

// read(int fd, void *buf, size_t count)
Interceptor.attach(Module.findExportByName(null, "read"), {
    onEnter(args) {
        this.fd = args[0].toInt32();
        this.buf = args[1];
        this.count = args[2].toInt32();
    },
    onLeave(retval) {
        const bytesRead = retval.toInt32();
        if (bytesRead > 0 && fdMap.has(this.fd)) {
            const path = fdMap.get(this.fd);
            console.log(`read(fd ${this.fd} → "${path}", ${bytesRead} octets)`);
            // Dumper les premiers octets lus
            const preview = this.buf.readByteArray(Math.min(bytesRead, 64));
            console.log("  données :", preview);
        }
    }
});

// write(int fd, const void *buf, size_t count)
Interceptor.attach(Module.findExportByName(null, "write"), {
    onEnter(args) {
        this.fd = args[0].toInt32();
        this.buf = args[1];
        this.count = args[2].toInt32();
    },
    onLeave(retval) {
        const bytesWritten = retval.toInt32();
        if (bytesWritten > 0 && fdMap.has(this.fd)) {
            const path = fdMap.get(this.fd);
            console.log(`write(fd ${this.fd} → "${path}", ${bytesWritten} octets)`);
            const preview = this.buf.readByteArray(Math.min(bytesWritten, 64));
            console.log("  données :", preview);
        }
    }
});
```

Ce script construit une vue complète des I/O fichier du programme : quel fichier est ouvert, quelles données y sont lues ou écrites, et quand il est fermé. En contexte de RE crypto (chapitre 24), on voit apparaître les lectures du fichier chiffré et les écritures du fichier déchiffré — avec leur contenu.

Notez que pour `read`, le buffer est lu dans `onLeave` (après que la fonction l'a rempli), tandis que pour `write`, le buffer peut être lu dès `onEnter` (il contient déjà les données à écrire). Ici, on lit dans `onLeave` dans les deux cas pour avoir accès au nombre réel d'octets transférés via `retval`.

### Décoder les flags `open`

Les flags de `open` sont des masques de bits. Pour les rendre lisibles :

```javascript
function decodeOpenFlags(flags) {
    const names = [];
    const O_RDONLY = 0x0, O_WRONLY = 0x1, O_RDWR = 0x2;
    const O_CREAT = 0x40, O_TRUNC = 0x200, O_APPEND = 0x400;

    const access = flags & 0x3;
    if (access === O_RDONLY) names.push("O_RDONLY");
    else if (access === O_WRONLY) names.push("O_WRONLY");
    else if (access === O_RDWR) names.push("O_RDWR");

    if (flags & O_CREAT) names.push("O_CREAT");
    if (flags & O_TRUNC) names.push("O_TRUNC");
    if (flags & O_APPEND) names.push("O_APPEND");

    return names.join(" | ");
}
```

---

## Intercepter les opérations réseau : `connect`, `send`, `recv`

Le traçage réseau est central pour le reverse de protocoles (chapitre 23). `strace` montre les appels système bruts, mais Frida permet de décoder les structures et de corréler les échanges.

### Hook sur `connect`

```c
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```

La difficulté ici est de parser la structure `sockaddr`, qui varie selon la famille d'adresse (IPv4, IPv6, Unix) :

```javascript
Interceptor.attach(Module.findExportByName(null, "connect"), {
    onEnter(args) {
        this.sockfd = args[0].toInt32();
        const sockaddr = args[1];
        const family = sockaddr.readU16();  // sa_family, 2 premiers octets

        if (family === 2) { // AF_INET (IPv4)
            // struct sockaddr_in : family(2) + port(2) + addr(4) + zero(8)
            const port = (sockaddr.add(2).readU8() << 8) | sockaddr.add(3).readU8();
            const ip = [
                sockaddr.add(4).readU8(),
                sockaddr.add(5).readU8(),
                sockaddr.add(6).readU8(),
                sockaddr.add(7).readU8()
            ].join('.');

            this.target = `${ip}:${port}`;
        } else if (family === 1) { // AF_UNIX
            this.target = sockaddr.add(2).readUtf8String();
        } else {
            this.target = `famille=${family}`;
        }
    },
    onLeave(retval) {
        const result = retval.toInt32();
        console.log(`connect(fd ${this.sockfd}, ${this.target}) = ${result}`);
    }
});
```

Notez le parsing manuel du port réseau : `sockaddr_in.sin_port` est en **network byte order** (big-endian), tandis que x86-64 est little-endian. On lit les deux octets individuellement et on les recombine dans le bon ordre.

### Hook sur `send` et `recv`

```c
ssize_t send(int sockfd, const void *buf, size_t len, int flags);  
ssize_t recv(int sockfd, void *buf, size_t len, int flags);  
```

```javascript
Interceptor.attach(Module.findExportByName(null, "send"), {
    onEnter(args) {
        this.sockfd = args[0].toInt32();
        this.buf = args[1];
        this.len = args[2].toInt32();
    },
    onLeave(retval) {
        const sent = retval.toInt32();
        if (sent > 0) {
            console.log(`\n>>> send(fd ${this.sockfd}, ${sent} octets)`);
            console.log(hexdump(this.buf, { length: Math.min(sent, 128) }));
        }
    }
});

Interceptor.attach(Module.findExportByName(null, "recv"), {
    onEnter(args) {
        this.sockfd = args[0].toInt32();
        this.buf = args[1];
        this.len = args[2].toInt32();
    },
    onLeave(retval) {
        const received = retval.toInt32();
        if (received > 0) {
            console.log(`\n<<< recv(fd ${this.sockfd}, ${received} octets)`);
            console.log(hexdump(this.buf, { length: Math.min(received, 128) }));
        }
    }
});
```

La fonction `hexdump` est un utilitaire intégré à Frida qui produit un affichage hexadécimal classique avec offsets et représentation ASCII — exactement le format de `xxd` (chapitre 5, section 5.1) :

```
              0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  0123456789ABCDEF
00000000  48 45 4c 4c 4f 00 01 02 03 04 05 06 07 08 09 0a  HELLO...........
00000010  6b 65 79 3d 41 42 43 44 45 46                    key=ABCDEF
```

### Envoyer les données binaires au client Python

Pour les protocoles binaires (chapitre 23), les données capturées doivent souvent être analysées côté Python plutôt qu'affichées en console. Le second paramètre de `send()` permet de transmettre un buffer binaire brut :

```javascript
Interceptor.attach(Module.findExportByName(null, "send"), {
    onEnter(args) {
        this.buf = args[1];
        this.len = args[2].toInt32();
    },
    onLeave(retval) {
        const sent = retval.toInt32();
        if (sent > 0) {
            // Envoyer le JSON + le buffer binaire brut
            send(
                { event: "send", fd: this.sockfd, length: sent },
                this.buf.readByteArray(sent)
            );
        }
    }
});
```

Côté Python :

```python
def on_message(message, data):
    if message['type'] == 'send':
        payload = message['payload']
        if payload.get('event') == 'send':
            print(f"[>>>] {payload['length']} octets sur fd {payload['fd']}")
            # data contient les octets bruts (type bytes)
            with open("capture_send.bin", "ab") as f:
                f.write(data)
            # Ou analyser le protocole directement
            parse_protocol(data)
```

Le paramètre `data` dans le callback Python est un objet `bytes` contenant exactement les octets envoyés par le second argument de `send()` côté JavaScript. Ce mécanisme évite l'encodage base64 du buffer dans le JSON, ce qui est crucial pour les transferts volumineux.

---

## Intercepter des fonctions applicatives (customs)

Au-delà des fonctions de bibliothèque standard, les cibles les plus intéressantes en RE sont les fonctions propres au binaire — une fonction `validate_license`, `decrypt_buffer`, `parse_packet`, ou `authenticate`. Ce sont elles qui encapsulent la logique métier.

### Trouver les fonctions candidates

La première étape est d'identifier les fonctions à hooker. Plusieurs approches se combinent :

**Depuis l'analyse statique (Ghidra, objdump).** Vous avez identifié dans Ghidra une fonction à l'offset `0x11a9` qui prend une chaîne en argument et retourne un `int`. Le décompilateur suggère qu'elle effectue une vérification.

```javascript
const base = Process.enumerateModules()[0].base;  
const validate_addr = base.add(0x11a9);  

Interceptor.attach(validate_addr, {
    onEnter(args) {
        console.log("validate() appelée");
        console.log("  arg0 (rdi) :", args[0].readUtf8String());
        console.log("  arg1 (rsi) :", args[1].toInt32());
    },
    onLeave(retval) {
        console.log("  retour :", retval.toInt32());
    }
});
```

**Depuis les imports du binaire.** Les imports (section 13.3) révèlent les fonctions de bibliothèque appelées, mais les fonctions internes n'y figurent pas. En revanche, on peut partir d'un import et remonter : « qui appelle `strcmp` dans le binaire principal ? » La backtrace (section 13.3) donne la réponse, et l'adresse de retour identifie la fonction appelante.

**Depuis `frida-trace` avec enumération exhaustive.** On peut tracer toutes les fonctions du binaire principal d'un coup :

```bash
frida-trace -f ./keygenme_O0 -I "keygenme_O0"
```

L'option `-I` (majuscule) inclut toutes les fonctions du module spécifié. La sortie montre l'ordre d'appel et permet d'identifier rapidement les fonctions appelées lors d'une action spécifique (saisie du mot de passe, par exemple).

### Hooker une fonction dont on ignore la signature

En RE, on ne connaît pas toujours la signature exacte de la fonction cible. Le décompilateur de Ghidra donne une estimation, mais elle peut être incorrecte — surtout en présence d'optimisations. La stratégie est de hooker d'abord en mode exploratoire, en inspectant les registres bruts :

```javascript
const base = Process.enumerateModules()[0].base;  
const mystery_func = base.add(0x1250);  

Interceptor.attach(mystery_func, {
    onEnter(args) {
        console.log("mystery_func() appelée");
        console.log("  rdi :", args[0]);
        console.log("  rsi :", args[1]);
        console.log("  rdx :", args[2]);
        console.log("  rcx :", args[3]);

        // Tenter de lire chaque argument comme différents types
        try { console.log("  rdi comme string :", args[0].readUtf8String()); } catch(e) {}
        try { console.log("  rdi comme int    :", args[0].toInt32()); } catch(e) {}
        try { console.log("  rsi comme string :", args[1].readUtf8String()); } catch(e) {}
        try { console.log("  rsi comme int    :", args[1].toInt32()); } catch(e) {}

        // Dump mémoire autour du premier argument (si c'est un pointeur)
        try {
            console.log("  mémoire @ rdi :");
            console.log(hexdump(args[0], { length: 64 }));
        } catch(e) {}
    },
    onLeave(retval) {
        console.log("  retour :", retval);
        console.log("  retour comme int :", retval.toInt32());
        try { console.log("  retour comme string :", retval.readUtf8String()); } catch(e) {}
    }
});
```

Les `try/catch` multiples semblent inélégants, mais c'est une technique de sondage efficace. Si `args[0]` est un entier (par exemple un file descriptor), `readUtf8String()` lèvera une exception (l'adresse `0x3` n'est pas un pointeur valide), mais `toInt32()` fonctionnera. Si c'est un pointeur vers une chaîne, `readUtf8String()` retournera la chaîne. En observant les résultats, on déduit progressivement la signature.

### Hooker une fonction qui manipule des structures

Quand un argument est un pointeur vers une structure, il faut lire les champs individuellement en connaissant le layout mémoire (reconstruit via Ghidra, chapitre 8 section 8.6) :

```c
// Structure reconstruite depuis Ghidra
typedef struct {
    uint32_t magic;      // offset 0x00
    uint16_t version;    // offset 0x04
    uint16_t flags;      // offset 0x06
    uint32_t data_size;  // offset 0x08
    char     name[32];   // offset 0x0c
} PacketHeader;
```

```javascript
Interceptor.attach(parse_packet_addr, {
    onEnter(args) {
        const hdr = args[0];  // pointeur vers PacketHeader

        const magic     = hdr.readU32();
        const version   = hdr.add(0x04).readU16();
        const flags     = hdr.add(0x06).readU16();
        const dataSize  = hdr.add(0x08).readU32();
        const name      = hdr.add(0x0c).readUtf8String();

        console.log(`parse_packet() :`);
        console.log(`  magic    : 0x${magic.toString(16)}`);
        console.log(`  version  : ${version}`);
        console.log(`  flags    : 0x${flags.toString(16)}`);
        console.log(`  dataSize : ${dataSize}`);
        console.log(`  name     : "${name}"`);

        // Si on veut aussi voir les données brutes après le header
        if (dataSize > 0 && dataSize < 4096) {
            const dataPtr = hdr.add(0x2c); // 0x0c + 32 (taille de name)
            console.log("  données :");
            console.log(hexdump(dataPtr, { length: Math.min(dataSize, 128) }));
        }
    }
});
```

Cette lecture champ par champ est l'équivalent dynamique de l'écriture d'un pattern `.hexpat` pour ImHex (chapitre 6). La différence est qu'ici, on voit les valeurs réelles au runtime, avec les adresses mémoire effectives et le contenu dynamique.

### Combiner hooks multiples pour reconstruire un flux

Un cas fréquent en RE : on veut comprendre le flux de données à travers plusieurs fonctions — par exemple, comment une entrée utilisateur est transformée, chiffrée, puis envoyée sur le réseau. La stratégie consiste à poser des hooks sur chaque étape et à corréler les données via des identifiants communs (pointeurs, file descriptors, taille de buffer) :

```javascript
// Étape 1 : l'utilisateur saisit un mot de passe
Interceptor.attach(Module.findExportByName(null, "fgets"), {
    onEnter(args) {
        this.buf = args[0];
    },
    onLeave(retval) {
        if (!retval.isNull()) {
            const input = this.buf.readUtf8String().trim();
            console.log(`[1] Saisie utilisateur : "${input}"`);
        }
    }
});

// Étape 2 : le programme transforme l'entrée (fonction custom)
const transform_addr = Process.enumerateModules()[0].base.add(0x1340);  
Interceptor.attach(transform_addr, {  
    onEnter(args) {
        this.inputBuf = args[0];
        this.outputBuf = args[1];
        this.len = args[2].toInt32();
        console.log(`[2] transform() : input de ${this.len} octets`);
        console.log(hexdump(this.inputBuf, { length: this.len }));
    },
    onLeave(retval) {
        console.log(`[2] transform() : output`);
        console.log(hexdump(this.outputBuf, { length: this.len }));
    }
});

// Étape 3 : le résultat est envoyé sur le réseau
Interceptor.attach(Module.findExportByName(null, "send"), {
    onEnter(args) {
        const len = args[2].toInt32();
        console.log(`[3] send() : ${len} octets`);
        console.log(hexdump(args[1], { length: Math.min(len, 128) }));
    }
});
```

Ce montage de hooks successifs est la technique fondamentale de l'analyse dynamique avec Frida. On pose des sondes à chaque point de transformation des données, et on observe comment les octets évoluent d'une étape à l'autre. C'est l'analogue dynamique du suivi de cross-references dans Ghidra (chapitre 8, section 8.7), mais avec les vraies données.

---

## `Memory.scan` : chercher des patterns en mémoire

En complément des hooks sur fonctions, Frida permet de scanner la mémoire du processus à la recherche de patterns d'octets. C'est particulièrement utile pour localiser des constantes crypto (chapitre 24), des chaînes déchiffrées en mémoire, ou des structures de données spécifiques.

```javascript
// Chercher la constante AES S-box (premiers octets : 63 7c 77 7b)
const sboxSignature = "63 7c 77 7b f2 6b 6f c5";

Process.enumerateRanges('r--').forEach(range => {
    Memory.scan(range.base, range.size, sboxSignature, {
        onMatch(address, size) {
            console.log(`[!] AES S-box trouvée @ ${address}`);
            console.log(hexdump(address, { length: 64 }));
        },
        onComplete() {}
    });
});
```

`Process.enumerateRanges('r--')` retourne toutes les plages mémoire lisibles. Le pattern est exprimé en hexadécimal avec des espaces entre les octets. On peut utiliser des wildcards avec `??` pour les octets variables :

```javascript
// Chercher un magic number suivi de 2 octets quelconques puis d'un flag
const pattern = "de ad ?? ?? 01";
```

---

## Bonnes pratiques et pièges courants

### La réentrance

Un hook sur `malloc` qui appelle `console.log` déclenche potentiellement un `malloc` interne (pour formater la chaîne), ce qui déclenche à nouveau le hook, et ainsi de suite — récursion infinie. En pratique, Frida gère ce cas en détectant la réentrance et en désactivant le hook pendant l'exécution du callback. Mais dans certains scénarios complexes (hooks sur plusieurs fonctions qui s'appellent mutuellement), la réentrance peut causer des comportements inattendus.

Si vous observez des résultats étranges ou des crashs avec des hooks sur des fonctions très bas niveau (`malloc`, `free`, `mmap`), un guard de réentrance explicite peut aider :

```javascript
let insideHook = false;

Interceptor.attach(Module.findExportByName(null, "malloc"), {
    onEnter(args) {
        if (insideHook) return;
        insideHook = true;

        this.size = args[0].toInt32();
        this.active = true;

        insideHook = false;
    },
    onLeave(retval) {
        if (!this.active) return;
        console.log(`malloc(${this.size}) = ${retval}`);
    }
});
```

> ⚠️ Ce guard n'est pas thread-safe dans un programme multi-threadé. Pour une protection robuste, il faudrait un guard par thread (via `Process.getCurrentThreadId()`). En pratique, pour nos binaires d'entraînement mono-threadés, le guard simple suffit.

### Performances

Chaque hook ajoute un surcoût. Pour des fonctions appelées très fréquemment (`malloc`, `free`, `strlen`), ce surcoût peut ralentir sensiblement le programme. Quelques règles :

- **Filtrez tôt.** Vérifiez le critère de filtrage (taille, appelant, contenu) le plus tôt possible dans `onEnter`, et utilisez un `return` rapide pour les cas non intéressants.  
- **Minimisez le travail dans le hook.** Envoyez les données brutes au client Python via `send()` et faites l'analyse lourde côté Python plutôt que dans le JavaScript de l'agent.  
- **Utilisez `console.log` avec parcimonie.** `send()` avec un callback Python est plus performant que `console.log` pour les volumes importants.  
- **Détachez les hooks devenus inutiles.** Si vous avez capturé l'information recherchée, appelez `listener.detach()` pour supprimer le hook et restaurer les performances normales.

---

## Ce qu'il faut retenir

- **`malloc`/`free`** : hooker les deux et corréler les pointeurs permet de suivre le cycle de vie des allocations. Filtrer par taille et par appelant élimine le bruit des allocations internes de la libc.  
- **`open`/`read`/`write`** : maintenir une map `fd → chemin` donne une vue complète des I/O fichier. Lire les buffers dans `onLeave` de `read` (après remplissage) et dans `onEnter` de `write` (avant envoi).  
- **`connect`/`send`/`recv`** : parser `sockaddr` manuellement pour l'IP et le port, utiliser `hexdump` pour visualiser les données réseau, et `send()` avec un buffer binaire pour transmettre les captures au client Python.  
- **Fonctions applicatives** : combiner l'analyse statique (Ghidra) pour trouver les offsets, le sondage exploratoire (`try/catch` sur les types) pour deviner la signature, et la lecture de structures champ par champ pour les arguments complexes.  
- **`Memory.scan`** : recherche de patterns d'octets en mémoire pour localiser des constantes, clés, ou structures.  
- **Réentrance et performance** : attention aux hooks sur des fonctions très fréquentes, filtrer tôt, envoyer les données brutes au client Python pour l'analyse lourde.

---

> **Prochaine section** : 13.5 — Modifier des arguments et valeurs de retour en live — nous passerons de l'observation à l'intervention, en apprenant à réécrire les données pendant l'exécution du programme.

⏭️ [Modifier des arguments et valeurs de retour en live](/13-frida/05-modifier-arguments-retour.md)
