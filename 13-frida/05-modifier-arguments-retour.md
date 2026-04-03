🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 13.5 — Modifier des arguments et valeurs de retour en live

> 🧰 **Outils utilisés** : `frida`, Python 3 + module `frida`  
> 📦 **Binaires utilisés** : `binaries/ch13-keygenme/keygenme_O0`, `binaries/ch14-crypto/crypto_O0`, `binaries/ch13-network/client_O0`  
> 📖 **Prérequis** : [13.3 — Hooking de fonctions C et C++](/13-frida/03-hooking-fonctions-c-cpp.md), [13.4 — Intercepter les appels](/13-frida/04-intercepter-appels.md)

---

## Passer de l'observation à l'intervention

Jusqu'ici, nos hooks Frida étaient des **capteurs passifs** : on lisait les arguments, on inspectait les valeurs de retour, on loguait les données — sans jamais rien changer au comportement du programme. C'est déjà extrêmement puissant pour comprendre un binaire, mais le reverse engineering ne s'arrête pas à la compréhension. On veut souvent **tester des hypothèses** en modifiant le comportement à la volée.

Une fonction de vérification retourne `0` (échec) ? On veut forcer `1` pour voir ce qui se passe ensuite dans le programme. Un appel à `connect` pointe vers un serveur distant inaccessible ? On veut rediriger vers `127.0.0.1`. Le programme lit un fichier de configuration avec des limites ? On veut réécrire les valeurs en mémoire avant qu'elles ne soient traitées.

Avec GDB, ces modifications sont possibles mais manuelles : on pose un breakpoint, on modifie un registre avec `set $rax = 1`, on continue. Avec Frida, elles sont **scriptables et automatiques** — chaque invocation de la fonction est interceptée et modifiée sans intervention humaine, sur toute la durée de l'exécution.

---

## Modifier une valeur de retour avec `retval.replace`

### Le mécanisme

Dans le callback `onLeave`, le paramètre `retval` est un `NativePointer` qui représente la valeur de retour de la fonction (le contenu de `rax` après le `ret`). Frida expose une méthode `.replace()` qui remplace cette valeur avant qu'elle ne soit transmise à l'appelant :

```javascript
Interceptor.attach(addr, {
    onLeave(retval) {
        retval.replace(nouvelle_valeur);
    }
});
```

La fonction originale s'exécute normalement, produit sa valeur de retour, puis le trampoline de sortie appelle `onLeave`, où `retval.replace()` écrase `rax` avec la nouvelle valeur. L'appelant reçoit la valeur modifiée sans savoir qu'elle a été altérée.

> ⚠️ **`retval.replace()`, pas une assignation.** L'écriture `retval = ptr(1)` ne fonctionne pas — elle réassigne la variable locale JavaScript sans affecter le registre. Il faut impérativement utiliser la méthode `.replace()` qui modifie la valeur dans le contexte CPU du processus cible.

### Forcer le retour d'une fonction de vérification

Le scénario le plus classique en RE : une fonction `check_password` retourne `0` (échec) ou `1` (succès). On force le retour à `1` pour contourner la vérification.

```javascript
const base = Process.enumerateModules()[0].base;  
const check_addr = base.add(0x11a9);  // offset depuis Ghidra  

Interceptor.attach(check_addr, {
    onEnter(args) {
        this.input = args[0].readUtf8String();
    },
    onLeave(retval) {
        const original = retval.toInt32();
        if (original === 0) {
            console.log(`check_password("${this.input}") = ${original} → forcé à 1`);
            retval.replace(ptr(1));
        } else {
            console.log(`check_password("${this.input}") = ${original} (déjà OK)`);
        }
    }
});
```

Ce hook est l'équivalent dynamique du patching binaire vu au chapitre 21 (section 21.6), où l'on inversait un `jz` en `jnz` dans ImHex. La différence fondamentale : ici, le binaire sur disque n'est pas modifié, et la modification est conditionnelle — on pourrait ne forcer le retour que pour certains inputs, ou alterner entre le comportement original et le comportement modifié pour observer les conséquences.

### Forcer le retour de fonctions de bibliothèque

Le même pattern fonctionne sur les fonctions de la libc. Exemple : faire croire au programme que `strcmp` retourne toujours `0` (égalité) :

```javascript
Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        this.s1 = args[0].readUtf8String();
        this.s2 = args[1].readUtf8String();
    },
    onLeave(retval) {
        console.log(`strcmp("${this.s1}", "${this.s2}") = ${retval.toInt32()} → forcé à 0`);
        retval.replace(ptr(0));
    }
});
```

> ⚠️ **Effet de bord massif.** Forcer `strcmp` à toujours retourner `0` affecte **tous** les appels à `strcmp` dans le programme, y compris ceux de la libc elle-même (résolution de locales, parsing de configuration interne, etc.). Le programme peut crasher ou se comporter de manière erratique. Il faut toujours filtrer pour ne modifier que les appels pertinents.

### Modification sélective avec filtrage

```javascript
Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        this.s1 = args[0].readUtf8String();
        this.s2 = args[1].readUtf8String();
        // Ne modifier que si un des arguments ressemble à une clé ou un mot de passe
        this.shouldPatch = (this.s1 && this.s1.length > 4 && this.s1.length < 64) &&
                           (this.s2 && this.s2.length > 4 && this.s2.length < 64);
    },
    onLeave(retval) {
        if (this.shouldPatch && retval.toInt32() !== 0) {
            console.log(`[PATCH] strcmp("${this.s1}", "${this.s2}") → forcé à 0`);
            retval.replace(ptr(0));
        }
    }
});
```

Ou mieux encore, filtrer par appelant pour ne cibler que les `strcmp` invoqués par la fonction de vérification :

```javascript
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        this.fromMain = this.returnAddress.compare(modBase) >= 0 &&
                        this.returnAddress.compare(modEnd) < 0;
        if (this.fromMain) {
            this.s1 = args[0].readUtf8String();
            this.s2 = args[1].readUtf8String();
        }
    },
    onLeave(retval) {
        if (this.fromMain) {
            console.log(`[main] strcmp("${this.s1}", "${this.s2}") → forcé à 0`);
            retval.replace(ptr(0));
        }
        // Les strcmp appelés depuis la libc restent intacts
    }
});
```

---

## Modifier les arguments dans `onEnter`

### Réécrire un argument passé par valeur

Les arguments entiers et les pointeurs passés par valeur peuvent être modifiés directement dans le tableau `args` :

```javascript
// open(const char *pathname, int flags, mode_t mode)
Interceptor.attach(Module.findExportByName(null, "open"), {
    onEnter(args) {
        const path = args[0].readUtf8String();

        // Rediriger la lecture d'un fichier de licence vers notre fichier custom
        if (path === "/opt/app/license.dat") {
            const fakePath = Memory.allocUtf8String("/tmp/fake_license.dat");
            args[0] = fakePath;
            console.log(`[REDIRECT] open() : "${path}" → "/tmp/fake_license.dat"`);
        }
    }
});
```

`Memory.allocUtf8String` alloue une nouvelle chaîne dans le heap du processus cible et retourne un `NativePointer` vers cette allocation. En assignant ce pointeur à `args[0]`, on remplace l'argument `pathname` avant que `open` ne l'utilise. La fonction `open` ouvrira `/tmp/fake_license.dat` au lieu de `/opt/app/license.dat`.

> 💡 **Durée de vie de l'allocation.** `Memory.allocUtf8String` alloue dans une zone gérée par Frida. La mémoire reste valide tant que le script est chargé. Pour des hooks appelés un très grand nombre de fois, ces allocations s'accumulent. Si c'est un problème, on peut réutiliser un buffer pré-alloué.

### Réécrire un argument entier

Pour les arguments passés par valeur (entiers, flags, tailles), l'assignation directe dans `args` fonctionne avec `ptr()` :

```javascript
// Forcer le flag O_RDONLY sur tout appel à open
Interceptor.attach(Module.findExportByName(null, "open"), {
    onEnter(args) {
        const path = args[0].readUtf8String();
        const flags = args[1].toInt32();

        if (flags & 0x1) {  // O_WRONLY ou O_RDWR
            console.log(`[PATCH] open("${path}") : flags 0x${flags.toString(16)} → O_RDONLY`);
            args[1] = ptr(0x0);  // O_RDONLY
        }
    }
});
```

Ce hook empêche le programme d'écrire dans des fichiers — toute tentative d'ouverture en écriture est rétrogradée en lecture seule. Utile dans un contexte de sandbox ou pour observer un malware sans lui permettre de modifier le système de fichiers.

### Modifier le contenu d'un buffer pointé

Quand un argument est un pointeur vers un buffer, modifier `args[i]` change le pointeur lui-même (vers quel buffer la fonction va lire). Mais on peut aussi modifier le **contenu** du buffer original, sans changer le pointeur :

```javascript
// send(int sockfd, const void *buf, size_t len, int flags)
Interceptor.attach(Module.findExportByName(null, "send"), {
    onEnter(args) {
        const len = args[2].toInt32();
        const buf = args[1];

        console.log(`send() original (${len} octets) :`);
        console.log(hexdump(buf, { length: Math.min(len, 64) }));

        // Réécrire les 4 premiers octets du buffer
        buf.writeU8(0x41);          // offset 0 : 'A'
        buf.add(1).writeU8(0x42);   // offset 1 : 'B'
        buf.add(2).writeU8(0x43);   // offset 2 : 'C'
        buf.add(3).writeU8(0x44);   // offset 3 : 'D'

        console.log(`send() modifié :`);
        console.log(hexdump(buf, { length: Math.min(len, 64) }));
    }
});
```

La distinction est importante :

- **`args[1] = autrePtr`** — change le pointeur : la fonction lira un buffer complètement différent.  
- **`args[1].writeU8(0x41)`** — change le contenu à l'adresse d'origine : la fonction lira le même buffer, mais son contenu a été altéré.

La première approche est plus sûre (le buffer original reste intact), la seconde est plus simple quand on veut modifier quelques octets.

### Remplacer un buffer entier

Pour remplacer le contenu complet d'un buffer par des données arbitraires :

```javascript
Interceptor.attach(Module.findExportByName(null, "send"), {
    onEnter(args) {
        const originalLen = args[2].toInt32();

        // Préparer un nouveau buffer avec notre contenu
        const payload = [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03];
        const newBuf = Memory.alloc(payload.length);
        newBuf.writeByteArray(payload);

        // Remplacer le pointeur et la taille
        args[1] = newBuf;
        args[2] = ptr(payload.length);

        console.log(`[PATCH] send() : buffer remplacé (${payload.length} octets)`);
    }
});
```

On alloue un nouveau buffer avec `Memory.alloc`, on y écrit le contenu voulu avec `writeByteArray`, puis on redirige le pointeur `args[1]` et on ajuste la taille `args[2]`. La fonction `send` enverra notre payload au lieu des données originales.

---

## Modifier la mémoire du processus : les méthodes `Memory.write*`

Au-delà de la modification d'arguments dans les hooks, Frida permet d'écrire directement dans la mémoire du processus à tout moment. C'est l'équivalent de la commande `set` de GDB, mais scriptable.

### Écriture de types primitifs

```javascript
const addr = ptr("0x7f3a8c001000");

// Écrire un octet
addr.writeU8(0xFF);

// Écrire un entier 32 bits
addr.writeS32(-1);          // signé  
addr.add(4).writeU32(42);   // non signé  

// Écrire un entier 64 bits
addr.writeU64(uint64("0xDEADBEEFCAFEBABE"));

// Écrire un flottant
addr.writeFloat(3.14);  
addr.writeDouble(2.718281828);  

// Écrire un pointeur
addr.writePointer(ptr("0x401000"));
```

### Écriture de chaînes et de buffers

```javascript
// Écrire une chaîne UTF-8 (avec null terminator)
addr.writeUtf8String("Hello from Frida");

// Écrire un tableau d'octets
addr.writeByteArray([0x90, 0x90, 0x90, 0x90]);  // 4 x NOP
```

### Protections mémoire

Toute écriture exige que la page mémoire cible ait la permission d'écriture (`w`). Les sections `.text` (code) et `.rodata` (constantes) sont normalement en lecture seule. Tenter d'y écrire directement provoque un crash (`SIGSEGV`).

Frida fournit `Memory.protect` pour modifier les permissions :

```javascript
const codeAddr = ptr("0x555555555189");

// Rendre la page accessible en lecture + écriture + exécution
Memory.protect(codeAddr, 4096, 'rwx');

// Maintenant on peut écrire dans le code
codeAddr.writeByteArray([0x90, 0x90]);  // remplacer 2 octets par des NOP

// Restaurer les permissions originales
Memory.protect(codeAddr, 4096, 'r-x');
```

> ⚠️ La granularité de `Memory.protect` est la page (4096 octets sur x86-64). Changer les permissions d'une adresse affecte toute la page qui la contient. Restaurez les permissions après modification pour minimiser la surface d'attaque.

---

## Patching binaire en mémoire

La modification de mémoire ouvre la porte au **patching en mémoire** — réécrire des instructions machine directement dans la section `.text`, sans toucher au fichier sur disque. C'est l'équivalent live du patching hexadécimal dans ImHex (chapitre 21, section 21.6).

### Remplacer un saut conditionnel

Scénario classique du crackme : une instruction `jz` (jump if zero, opcode `0x74`) doit être transformée en `jnz` (jump if not zero, opcode `0x75`), ou inversement.

```javascript
const base = Process.enumerateModules()[0].base;

// Adresse du jz à patcher (offset trouvé dans Ghidra)
const jzAddr = base.add(0x1205);

// Vérifier l'opcode actuel
const currentOpcode = jzAddr.readU8();  
console.log(`Opcode actuel @ ${jzAddr} : 0x${currentOpcode.toString(16)}`);  

if (currentOpcode === 0x74) {  // jz (short)
    Memory.protect(jzAddr, 4096, 'rwx');
    jzAddr.writeU8(0x75);  // jnz (short)
    Memory.protect(jzAddr, 4096, 'r-x');
    console.log("[PATCH] jz → jnz");
} else {
    console.log("[!] Opcode inattendu, abandon du patch");
}
```

La vérification de l'opcode avant écriture est une précaution essentielle. Si l'ASLR a décalé les adresses différemment de ce qu'on attendait, ou si le binaire a été mis à jour, on écrirait au mauvais endroit. Toujours valider avant de modifier.

### NOP-er une instruction ou un bloc

Remplacer des instructions par des `NOP` (`0x90`) est une technique classique pour désactiver un bloc de code — une vérification de licence, un appel à `exit`, une boucle de délai :

```javascript
const base = Process.enumerateModules()[0].base;

// NOP-er un call de 5 octets (opcode E8 + 4 octets d'offset)
// Par exemple un call à une fonction de vérification anti-debug
const callAddr = base.add(0x120a);

Memory.protect(callAddr, 4096, 'rwx');  
callAddr.writeByteArray([0x90, 0x90, 0x90, 0x90, 0x90]);  // 5 NOP  
Memory.protect(callAddr, 4096, 'r-x');  

console.log("[PATCH] call anti_debug NOP-é");
```

Un `call rel32` sur x86-64 fait 5 octets (1 pour l'opcode `0xE8`, 4 pour le déplacement relatif). On remplace les 5 octets par 5 `NOP` pour que le processeur les traverse sans rien faire. Le flux d'exécution continue normalement après l'emplacement du `call` disparu.

### `Memory.patchCode` : la méthode propre

Pour le patching de code, Frida offre `Memory.patchCode`, qui gère automatiquement les permissions mémoire et le vidage du cache d'instructions (nécessaire sur certaines architectures) :

```javascript
const base = Process.enumerateModules()[0].base;  
const target = base.add(0x1205);  

Memory.patchCode(target, 1, code => {
    code.putU8(0x75);  // jnz
});
```

`Memory.patchCode` prend l'adresse cible, la taille de la zone à modifier, et un callback qui reçoit un writer. Le writer propose des méthodes comme `putU8`, `putBytes`, etc. Cette approche est plus robuste que la modification manuelle car elle gère les détails de bas niveau (flush du cache d'instructions, atomicité de la modification).

---

## Redirection de flux réseau

Un cas d'usage fréquent en RE de binaires réseau (chapitre 23) : rediriger les connexions vers un serveur que l'on contrôle.

### Modifier l'adresse IP dans `connect`

```javascript
Interceptor.attach(Module.findExportByName(null, "connect"), {
    onEnter(args) {
        const sockaddr = args[1];
        const family = sockaddr.readU16();

        if (family === 2) {  // AF_INET
            const port = (sockaddr.add(2).readU8() << 8) | sockaddr.add(3).readU8();
            const origIp = [
                sockaddr.add(4).readU8(),
                sockaddr.add(5).readU8(),
                sockaddr.add(6).readU8(),
                sockaddr.add(7).readU8()
            ].join('.');

            console.log(`[*] connect() original : ${origIp}:${port}`);

            // Rediriger vers 127.0.0.1
            sockaddr.add(4).writeU8(127);
            sockaddr.add(5).writeU8(0);
            sockaddr.add(6).writeU8(0);
            sockaddr.add(7).writeU8(1);

            console.log(`[REDIRECT] → 127.0.0.1:${port}`);
        }
    }
});
```

Ici, on modifie directement la structure `sockaddr_in` en mémoire, dans le buffer que le programme a préparé. Quand `connect` s'exécute, il utilise l'adresse modifiée. Le programme croit se connecter au serveur distant, mais la connexion aboutit sur `127.0.0.1`.

### Modifier le port

On peut aussi rediriger vers un port différent :

```javascript
// Rediriger le port 443 vers 8080
if (port === 443) {
    // sin_port est en network byte order (big-endian)
    const newPort = 8080;
    sockaddr.add(2).writeU8((newPort >> 8) & 0xFF);  // octet de poids fort
    sockaddr.add(3).writeU8(newPort & 0xFF);          // octet de poids faible
    console.log(`[REDIRECT] port 443 → 8080`);
}
```

Attention à l'ordre des octets : `sin_port` est stocké en big-endian (network byte order), alors que x86-64 est little-endian. On écrit les deux octets séparément dans le bon ordre.

### Application : simuler un serveur C2

Ce pattern de redirection est fondamental pour l'analyse de malware (chapitre 28). Le dropper tente de contacter un serveur C2 distant. En redirigeant `connect` vers `127.0.0.1`, on peut y faire tourner notre propre faux serveur C2 et observer le protocole de communication sans jamais contacter l'infrastructure malveillante réelle.

---

## Modifier les variables d'environnement et les retours de fonctions système

### Faire mentir `getenv`

```c
char *getenv(const char *name);
```

```javascript
Interceptor.attach(Module.findExportByName(null, "getenv"), {
    onEnter(args) {
        this.name = args[0].readUtf8String();
    },
    onLeave(retval) {
        if (this.name === "LICENSE_KEY") {
            const fakeValue = Memory.allocUtf8String("VALID-KEY-12345");
            retval.replace(fakeValue);
            console.log(`[PATCH] getenv("LICENSE_KEY") → "VALID-KEY-12345"`);
        }
    }
});
```

Le programme appelle `getenv("LICENSE_KEY")` pour lire une variable d'environnement. Notre hook intercepte le retour et le remplace par une clé de notre choix, sans avoir besoin de définir la variable réellement.

### Faire mentir `time` et `gettimeofday`

Certains programmes vérifient une date d'expiration ou utilisent le temps comme graine pour un générateur pseudo-aléatoire. Contrôler le temps perçu par le programme est un levier puissant :

```javascript
// time(time_t *tloc)
Interceptor.attach(Module.findExportByName(null, "time"), {
    onLeave(retval) {
        // Figer le temps au 1er janvier 2024 00:00:00 UTC
        const fakeTime = 1704067200;
        retval.replace(ptr(fakeTime));
        console.log(`[PATCH] time() → ${fakeTime} (2024-01-01)`);
    }
});
```

Cela permet de contourner des vérifications d'expiration de licence basées sur le temps, ou de rendre reproductible un comportement qui dépend de `time()` comme seed d'un PRNG.

### Désactiver `ptrace` (anti-anti-debug)

Comme vu en section 13.1, certains programmes appellent `ptrace(PTRACE_TRACEME, ...)` pour détecter un débogueur. On peut neutraliser cette vérification :

```javascript
Interceptor.attach(Module.findExportByName(null, "ptrace"), {
    onEnter(args) {
        this.request = args[0].toInt32();
    },
    onLeave(retval) {
        if (this.request === 0) {  // PTRACE_TRACEME
            retval.replace(ptr(0));  // Simuler un succès
            console.log("[PATCH] ptrace(PTRACE_TRACEME) → 0 (succès simulé)");
        }
    }
});
```

Le programme croit que `ptrace(PTRACE_TRACEME)` a réussi (retour `0`), ce qui signifie qu'aucun débogueur n'est attaché. En réalité, Frida a déjà relâché `ptrace` après l'injection (section 13.1), donc cette vérification échouerait si on ne la neutralisait pas. Ce technique est approfondie au chapitre 19 (section 19.7).

---

## Modifier le contexte CPU directement

Pour les cas où la modification d'arguments et de valeurs de retour ne suffit pas, Frida donne accès au **contexte CPU complet** — tous les registres — via `this.context` dans les callbacks de hook.

### Lire les registres

```javascript
Interceptor.attach(addr, {
    onEnter(args) {
        const ctx = this.context;

        console.log("Registres :");
        console.log(`  rax = ${ctx.rax}`);
        console.log(`  rbx = ${ctx.rbx}`);
        console.log(`  rcx = ${ctx.rcx}`);
        console.log(`  rdx = ${ctx.rdx}`);
        console.log(`  rdi = ${ctx.rdi}`);
        console.log(`  rsi = ${ctx.rsi}`);
        console.log(`  rsp = ${ctx.rsp}`);
        console.log(`  rbp = ${ctx.rbp}`);
        console.log(`  rip = ${ctx.rip}`);
        console.log(`  r8  = ${ctx.r8}`);
        console.log(`  r9  = ${ctx.r9}`);
    }
});
```

### Modifier les registres

Les registres sont en lecture-écriture. On peut modifier n'importe quel registre, y compris le pointeur d'instruction (`rip`) — bien que modifier `rip` soit extrêmement dangereux et rarement nécessaire :

```javascript
Interceptor.attach(addr, {
    onEnter(args) {
        // Modifier rax (par exemple pour changer un compteur)
        this.context.rax = ptr(42);

        // Modifier un flag via un registre
        this.context.rdx = ptr(0);
    }
});
```

La modification de `this.context` dans `onEnter` prend effet **avant** que la fonction ne s'exécute. Dans `onLeave`, elle prend effet au moment du retour à l'appelant. Modifier `rax` dans `onLeave` est fonctionnellement équivalent à `retval.replace()`.

### Cas d'usage : sauter par-dessus un bloc de code

On peut modifier `rip` dans un hook posé au début d'un bloc indésirable pour sauter directement à la fin :

```javascript
const base = Process.enumerateModules()[0].base;  
const antiDebugStart = base.add(0x1300);  // début du bloc anti-debug  
const antiDebugEnd = base.add(0x1350);    // instruction après le bloc  

Interceptor.attach(antiDebugStart, {
    onEnter(args) {
        console.log("[SKIP] Bloc anti-debug sauté");
        this.context.rip = antiDebugEnd;
    }
});
```

> ⚠️ Modifier `rip` est une opération délicate. Si l'adresse de destination n'est pas une instruction valide, ou si l'état de la pile ne correspond pas à ce que le code à destination attend, le programme crashera. Cette technique exige une compréhension précise du code assembleur aux deux extrémités du saut.

---

## Orchestrer les modifications depuis Python

Les exemples précédents montrent des modifications inconditionnelles (forcer à `1`, toujours rediriger). En pratique, on veut souvent décider dynamiquement — depuis le script Python — quelles modifications appliquer. Le canal `send()`/`on_message` est bidirectionnel : l'agent JavaScript peut recevoir des messages du client Python via `recv()`.

### Communication bidirectionnelle

```javascript
// Côté agent (JavaScript)
Interceptor.attach(check_addr, {
    onLeave(retval) {
        const original = retval.toInt32();

        // Demander au client Python si on doit patcher
        send({ event: "check_result", value: original });

        // Attendre la réponse du client
        const op = recv('patch_decision', value => {
            if (value.payload.patch === true) {
                retval.replace(ptr(1));
                console.log("[PATCH] Retour forcé à 1 par décision Python");
            }
        });
        op.wait();  // Bloque jusqu'à réception de la réponse
    }
});
```

```python
# Côté client (Python)
def on_message(message, data):
    if message['type'] == 'send':
        payload = message['payload']
        if payload.get('event') == 'check_result':
            original = payload['value']
            # Logique de décision côté Python
            should_patch = (original == 0)
            script.post({'type': 'patch_decision', 'patch': should_patch})

script.on('message', on_message)
```

`recv()` côté JavaScript bloque l'exécution du hook jusqu'à ce que le client Python envoie un message via `script.post()`. Cela permet une boucle interactive : l'agent signale un événement, Python décide de l'action, l'agent l'exécute.

> ⚠️ `recv().wait()` bloque le thread du processus cible. Si la réponse Python tarde à arriver, le programme est gelé. Pour les fonctions appelées fréquemment, préférez une approche asynchrone où les décisions de patching sont envoyées en amont, stockées dans une variable JavaScript, et consultées de manière non bloquante.

### Approche non bloquante avec configuration pré-envoyée

```javascript
// Côté agent : configuration modifiable à chaud
let patchConfig = {
    forceReturn: false,
    returnValue: 0
};

// Écouter les mises à jour de configuration sans bloquer
recv('update_config', msg => {
    patchConfig = msg.payload;
    console.log("[*] Config mise à jour :", JSON.stringify(patchConfig));
});

Interceptor.attach(check_addr, {
    onLeave(retval) {
        if (patchConfig.forceReturn) {
            retval.replace(ptr(patchConfig.returnValue));
        }
    }
});
```

```python
# Côté Python : envoyer une nouvelle config à tout moment
script.post({'type': 'update_config', 'forceReturn': True, 'returnValue': 1})

# Plus tard, désactiver le patching
script.post({'type': 'update_config', 'forceReturn': False, 'returnValue': 0})
```

Cette approche est non bloquante : le hook consulte la variable `patchConfig` sans attendre de réponse. Le client Python peut mettre à jour la configuration à tout moment, et la modification prend effet dès le prochain appel à la fonction hookée.

---

## Résumé des méthodes de modification

| Ce que vous voulez modifier | Où le faire | Méthode |  
|---|---|---|  
| Valeur de retour d'une fonction | `onLeave` | `retval.replace(ptr(valeur))` |  
| Argument passé par valeur (int, pointeur) | `onEnter` | `args[i] = ptr(valeur)` |  
| Contenu d'un buffer pointé par un argument | `onEnter` | `args[i].writeU8(...)`, `.writeByteArray(...)` |  
| Remplacer un buffer entier | `onEnter` | `args[i] = Memory.alloc(...)` + remplir |  
| Registre CPU arbitraire | `onEnter` ou `onLeave` | `this.context.reg = ptr(valeur)` |  
| Instruction machine dans `.text` | N'importe quand | `Memory.patchCode(addr, size, writer)` |  
| Mémoire arbitraire (data, heap, stack) | N'importe quand | `ptr(addr).writeU32(...)`, etc. |  
| Fonction entière (remplacer sa logique) | Initialisation | `Interceptor.replace(addr, NativeCallback(...))` |

---

## Ce qu'il faut retenir

- **`retval.replace()`** est la méthode pour modifier la valeur de retour — pas l'assignation directe. C'est la technique la plus utilisée pour contourner des vérifications (licence, anti-debug, validation).  
- **`args[i] = ptr()`** remplace un argument par valeur ou un pointeur. **`args[i].write*()`** modifie le contenu du buffer pointé.  
- **Toujours filtrer** les modifications pour ne cibler que les appels pertinents (par appelant, par contenu des arguments). Une modification non filtrée sur `strcmp` ou `malloc` provoque des comportements imprévisibles.  
- **`Memory.patchCode`** permet le patching d'instructions en mémoire (jz→jnz, NOP) sans modifier le fichier sur disque.  
- **`this.context`** donne accès en lecture-écriture à tous les registres CPU, y compris `rip` — à manipuler avec précaution.  
- La communication **bidirectionnelle** Python↔JavaScript (via `send`/`recv`/`post`) permet de piloter les modifications dynamiquement depuis le client Python, en mode bloquant ou non bloquant.

---

> **Prochaine section** : 13.6 — Stalker : tracer toutes les instructions exécutées (code coverage dynamique) — nous aborderons le moteur de traçage instruction par instruction de Frida, un outil unique pour la cartographie exhaustive du code exécuté.

⏭️ [Stalker : tracer toutes les instructions exécutées (code coverage dynamique)](/13-frida/06-stalker-code-coverage.md)
