🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 27.5 — Analyse dynamique : GDB + Frida (extraire la clé en mémoire)

> 🔬 **Objectif de cette section** : confirmer de manière irréfutable les hypothèses posées lors de l'analyse statique en observant le programme **en cours d'exécution**. Nous allons intercepter les arguments passés à `EVP_EncryptInit_ex` au moment précis de l'appel, capturer la clé et l'IV tels qu'ils sont réellement utilisés par OpenSSL, et tracer le flux de chiffrement fichier par fichier.  
>  
> ⚠️ **Rappel impératif** : toute exécution du sample doit se faire dans la VM sandboxée du [Chapitre 26](/26-lab-securise/README.md). Prenez un snapshot **avant** chaque lancement. Vérifiez que le réseau est isolé (`ip link show` — pas d'interface vers l'hôte).

---

## Pourquoi l'analyse dynamique est nécessaire

L'analyse statique dans Ghidra (section 27.3) a identifié la clé et l'IV dans `.rodata`, et les XREF montrent qu'ils sont passés à `EVP_EncryptInit_ex`. Alors pourquoi ne pas s'arrêter là ?

Parce que l'analyse statique montre ce que le code **pourrait** faire, pas ce qu'il **fait réellement**. Plusieurs scénarios invalideraient nos conclusions statiques sans que le désassemblage ne le révèle facilement : la clé dans `.rodata` pourrait être un leurre, copiée dans un buffer puis transformée par un XOR ou une dérivation avant utilisation. Une condition runtime (variable d'environnement, argument, date système) pourrait sélectionner une clé alternative. Le compilateur en `-O2` pourrait avoir réorganisé le code d'une manière que le décompilateur interprète incorrectement.

L'analyse dynamique apporte la **preuve par l'observation directe** : nous verrons les octets exacts que le programme passe à OpenSSL, au cycle d'horloge où il le fait.

---

## Préparation de l'environnement

### Restaurer un état propre

```bash
# Dans la VM sandboxée
make reset        # Recrée /tmp/test/ avec les fichiers de test  
ls -la /tmp/test/ # Vérifier que les fichiers sont présents et non chiffrés  
```

### Choisir la variante

Nous travaillerons avec les deux variantes en parallèle :

- **`ransomware_O0`** (debug) — Pour la première prise en main avec GDB. Les symboles DWARF permettent de poser des breakpoints par nom de fonction et d'inspecter les variables locales.  
- **`ransomware_O2_strip`** (strippée) — Pour la démonstration réaliste. On posera les breakpoints sur les fonctions de la bibliothèque partagée, qui restent accessibles même sans symboles.

### Désactiver ASLR (optionnel mais recommandé pour l'apprentissage)

L'ASLR randomise les adresses à chaque exécution, ce qui complique le suivi entre les sessions GDB. Pour des raisons pédagogiques, nous le désactivons temporairement :

```bash
# Désactiver (nécessite root)
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

# À réactiver après l'analyse
echo 2 | sudo tee /proc/sys/kernel/randomize_va_space
```

> 💡 En situation réelle, on ne désactive pas l'ASLR — on travaille en offsets relatifs ou on utilise les capacités de GDB/GEF à résoudre les adresses dynamiquement. Ici, c'est un confort d'apprentissage.

---

## Partie A — Extraction avec GDB

### Approche sur la variante debug (`ransomware_O0`)

#### Lancer GDB

```bash
$ gdb -q ./ransomware_O0
Reading symbols from ./ransomware_O0...
(gdb)
```

GDB charge les symboles DWARF. Toutes les fonctions internes sont accessibles par nom.

#### Poser un breakpoint sur la routine de chiffrement

Notre cible principale est la fonction `aes256cbc_encrypt`, qui encapsule les appels EVP. Posons un breakpoint à son entrée :

```gdb
(gdb) break aes256cbc_encrypt
Breakpoint 1 at 0x...: file ransomware_sample.c, line ...
```

Pour capturer le moment exact où la clé est transmise à OpenSSL, posons également un breakpoint sur l'appel à la bibliothèque :

```gdb
(gdb) break EVP_EncryptInit_ex
Breakpoint 2 at 0x... (in /lib/x86_64-linux-gnu/libcrypto.so.3)
```

#### Exécuter et intercepter

```gdb
(gdb) run
Starting program: /home/user/binaries/ch27-ransomware/ransomware_O0
==============================================
  Formation RE — Chapitre 27
  Sample pedagogique — NE PAS DISTRIBUER
  Cible : /tmp/test
==============================================
[*] Parcours de /tmp/test ...
[+] Chiffrement : /tmp/test/document.txt

Breakpoint 1, aes256cbc_encrypt (in=0x..., in_len=47, out=0x..., out_len=0x...)
    at ransomware_sample.c:...
```

Le programme s'arrête à l'entrée de `aes256cbc_encrypt`, au moment de chiffrer le premier fichier. Les arguments sont visibles car les symboles DWARF incluent les noms des paramètres.

#### Inspecter les arguments de `aes256cbc_encrypt`

```gdb
(gdb) print in_len
$1 = 47

(gdb) x/47c in
0x...:  "Ceci est un document strictement confidentiel.\n"
```

On voit le contenu du fichier `document.txt` en clair, prêt à être chiffré. Continuons jusqu'au breakpoint sur `EVP_EncryptInit_ex` :

```gdb
(gdb) continue
Continuing.

Breakpoint 2, EVP_EncryptInit_ex (ctx=0x..., type=0x..., impl=0x0,
    key=0x..., iv=0x...)
```

#### Capturer la clé et l'IV

C'est le moment clé de l'analyse. Les arguments de `EVP_EncryptInit_ex` suivent la convention d'appel System V AMD64 (vue au [Chapitre 3, section 3.6](/03-assembleur-x86-64/06-passage-parametres.md)) :

| Registre | Paramètre | Signification |  
|---|---|---|  
| `rdi` | `ctx` | Contexte EVP (opaque) |  
| `rsi` | `type` | Pointeur vers la structure `EVP_CIPHER` (AES-256-CBC) |  
| `rdx` | `impl` | Engine (NULL = implémentation par défaut) |  
| `rcx` | `key` | Pointeur vers la clé de chiffrement |  
| `r8`  | `iv` | Pointeur vers le vecteur d'initialisation |

Extrayons la clé (32 octets pointés par `rcx`) et l'IV (16 octets pointés par `r8`) :

```gdb
(gdb) # Clé AES-256 — 32 octets depuis le registre rcx
(gdb) x/32xb $rcx
0x...: 0x52  0x45  0x56  0x45  0x52  0x53  0x45  0x5f
0x...: 0x45  0x4e  0x47  0x49  0x4e  0x45  0x45  0x52
0x...: 0x49  0x4e  0x47  0x5f  0x49  0x53  0x5f  0x46
0x...: 0x55  0x4e  0x5f  0x32  0x30  0x32  0x35  0x21

(gdb) # Afficher en ASCII pour confirmation
(gdb) x/s $rcx
0x...: "REVERSE_ENGINEERING_IS_FUN_2025!"

(gdb) # IV AES — 16 octets depuis le registre r8
(gdb) x/16xb $r8
0x...: 0xde  0xad  0xbe  0xef  0xca  0xfe  0xba  0xbe
0x...: 0x13  0x37  0x42  0x42  0xfe  0xed  0xfa  0xce
```

**Confirmation définitive** : la clé passée à OpenSSL au runtime est exactement celle identifiée dans `.rodata` lors de l'analyse statique. L'IV est identique à notre hypothèse H4. Aucune transformation, aucun leurre, aucune dérivation.

#### Inspecter le résultat après chiffrement

Continuons l'exécution jusqu'à la sortie de `aes256cbc_encrypt` pour voir le ciphertext :

```gdb
(gdb) finish
Run till exit from #0  EVP_EncryptInit_ex ...
(gdb) finish
Run till exit from #0  aes256cbc_encrypt ...  
aes256cbc_encrypt returned 0        ← succès  

(gdb) # out_len contient la taille du ciphertext
(gdb) print *out_len
$2 = 48
```

Le fichier original faisait 47 octets. Le ciphertext fait 48 octets : 47 octets de données + 1 octet de padding PKCS#7, arrondis au prochain multiple de 16 (48 = 3 × 16). Ce résultat est cohérent avec le mode CBC et confirme le padding.

### Approche sur la variante strippée (`ransomware_O2_strip`)

Sur un binaire sans symboles, la stratégie est différente. On ne peut pas écrire `break aes256cbc_encrypt` car cette fonction n'existe pas dans la table des symboles. En revanche, les **fonctions importées** (bibliothèques partagées) restent accessibles.

```bash
$ gdb -q ./ransomware_O2_strip
(No debugging symbols found in ./ransomware_O2_strip)
(gdb)
```

#### Breakpoint sur la fonction de bibliothèque

```gdb
(gdb) break EVP_EncryptInit_ex
Function "EVP_EncryptInit_ex" not defined.  
Make breakpoint pending on future shared library load? (y or [n]) y  
Breakpoint 1 (EVP_EncryptInit_ex) pending.  
(gdb) run
```

Le breakpoint est marqué *pending* car la bibliothèque `libcrypto.so` n'est pas encore chargée. GDB le résoudra automatiquement quand le loader dynamique la mappera en mémoire.

```gdb
[*] Parcours de /tmp/test ...
[+] Chiffrement : /tmp/test/document.txt

Breakpoint 1, 0x00007ffff7... in EVP_EncryptInit_ex () from /lib/.../libcrypto.so.3
```

L'extraction est alors identique : `x/32xb $rcx` pour la clé, `x/16xb $r8` pour l'IV. Le fait que le binaire soit strippé n'empêche pas d'intercepter les appels aux bibliothèques dynamiques — c'est l'une des faiblesses structurelles du linkage dynamique du point de vue d'un auteur de malware.

#### Examiner plusieurs fichiers

Pour vérifier que la même clé et le même IV sont utilisés pour chaque fichier (ce qui est attendu puisqu'ils sont statiques), on peut définir un breakpoint avec commandes automatiques :

```gdb
(gdb) break EVP_EncryptInit_ex
(gdb) commands 1
    silent
    printf "=== EVP_EncryptInit_ex called ===\n"
    printf "Key: "
    x/32xb $rcx
    printf "IV:  "
    x/16xb $r8
    continue
end
(gdb) run
```

GDB affichera automatiquement la clé et l'IV à chaque appel, puis reprendra l'exécution. Sur notre sample, chaque invocation montrera les mêmes valeurs — confirmant l'absence de rotation de clé entre les fichiers. Cette vérification est importante : un ransomware plus sophistiqué pourrait générer une clé unique par fichier.

### Script GDB Python pour l'extraction automatique

Pour formaliser l'extraction, voici un script GDB Python qui capture et exporte les paramètres cryptographiques :

```python
# extract_crypto_params.py — Script GDB Python
# Usage : gdb -q -x extract_crypto_params.py ./ransomware_O2_strip

import gdb  
import json  

results = []

class EvpBreakpoint(gdb.Breakpoint):
    """Breakpoint sur EVP_EncryptInit_ex qui capture clé et IV."""
    
    def __init__(self):
        super().__init__("EVP_EncryptInit_ex", gdb.BP_BREAKPOINT)
        self.call_count = 0
    
    def stop(self):
        self.call_count += 1
        frame = gdb.newest_frame()
        
        # Lire rcx (key) et r8 (iv) — convention System V AMD64
        key_addr = int(gdb.parse_and_eval("$rcx"))
        iv_addr  = int(gdb.parse_and_eval("$r8"))
        
        # Lire les octets en mémoire
        inferior = gdb.selected_inferior()
        key_bytes = inferior.read_memory(key_addr, 32).tobytes()
        iv_bytes  = inferior.read_memory(iv_addr, 16).tobytes()
        
        entry = {
            "call_number": self.call_count,
            "key_hex": key_bytes.hex(),
            "key_ascii": key_bytes.decode("ascii", errors="replace"),
            "iv_hex": iv_bytes.hex(),
        }
        results.append(entry)
        
        print(f"[*] Call #{self.call_count}")
        print(f"    Key : {key_bytes.hex()}")
        print(f"    IV  : {iv_bytes.hex()}")
        
        # Ne pas arrêter l'exécution — continuer automatiquement
        return False

# Poser le breakpoint
bp = EvpBreakpoint()

# Exécuter le programme
gdb.execute("run")

# Exporter les résultats après la fin de l'exécution
with open("/tmp/crypto_params.json", "w") as f:
    json.dump(results, f, indent=2)

print(f"\n[✓] {len(results)} appels capturés → /tmp/crypto_params.json")
```

Exécution :

```bash
$ gdb -q -batch -x extract_crypto_params.py ./ransomware_O2_strip
[*] Call #1
    Key : 524556455253455f454e47494e454552494e475f49535f46554e5f3230323521
    IV  : deadbeefcafebabe13374242feedface
[*] Call #2
    Key : 524556455253455f454e47494e454552494e475f49535f46554e5f3230323521
    IV  : deadbeefcafebabe13374242feedface
...
[✓] 6 appels capturés → /tmp/crypto_params.json
```

Le flag `-batch` fait tourner GDB en mode non-interactif. Le fichier JSON produit est exploitable directement par le déchiffreur Python (section 27.6) et constitue une pièce du rapport (section 27.7).

---

## Partie B — Extraction avec Frida

Frida offre une approche complémentaire à GDB. Là où GDB interrompt l'exécution et inspecte l'état (paradigme breakpoint), Frida **injecte du code JavaScript** dans le processus cible et intercepte les appels en live, sans interrompre le flux (paradigme hook). Le programme s'exécute à vitesse quasi-native, et les données sont capturées au passage.

### Pourquoi utiliser Frida en complément de GDB

La distinction n'est pas seulement technique, elle est méthodologique :

| Aspect | GDB | Frida |  
|---|---|---|  
| Paradigme | Stop-and-inspect | Hook-and-log |  
| Impact sur l'exécution | Interrompt le programme | Quasi-transparent |  
| Sortie | Manuelle ou scriptée | Logging structuré automatique |  
| Modification runtime | Possible mais laborieuse | Native (modifier arguments, retours) |  
| Anti-debug | Détectable via `ptrace` | Plus discret (injection en userspace) |  
| Courbe d'apprentissage | CLI complexe | JavaScript familier |

Pour notre sample (sans anti-debug), les deux approches aboutissent au même résultat. L'intérêt de Frida ici est pédagogique : montrer une deuxième méthode, et préparer le terrain pour le [Chapitre 28](/28-dropper/README.md) où le hooking Frida sera indispensable pour intercepter les communications réseau en live.

### Script Frida : hooker `EVP_EncryptInit_ex`

```javascript
// hook_evp.js — Script Frida pour intercepter les appels EVP
// Usage : frida -f ./ransomware_O2_strip -l hook_evp.js --no-pause

const CYAN  = "\x1b[36m";  
const GREEN = "\x1b[32m";  
const RESET = "\x1b[0m";  

// Résoudre l'adresse de EVP_EncryptInit_ex dans libcrypto
const evpInit = Module.findExportByName("libcrypto.so.3", "EVP_EncryptInit_ex");

if (evpInit) {
    console.log(`[*] EVP_EncryptInit_ex trouvée à ${evpInit}`);

    Interceptor.attach(evpInit, {
        onEnter: function (args) {
            // Signature : EVP_EncryptInit_ex(ctx, type, impl, key, iv)
            //               args[0] args[1] args[2] args[3] args[4]
            
            const keyPtr = args[3];
            const ivPtr  = args[4];

            // Lire la clé (32 octets) et l'IV (16 octets)
            const keyBytes = keyPtr.readByteArray(32);
            const ivBytes  = ivPtr.readByteArray(16);

            console.log(`\n${GREEN}=== EVP_EncryptInit_ex called ===${RESET}`);

            // Afficher la clé en hex
            console.log(`${CYAN}Key (32 bytes):${RESET}`);
            console.log(hexdump(keyPtr, { length: 32, ansi: true }));

            // Tenter un affichage ASCII de la clé
            try {
                const keyStr = keyPtr.readUtf8String(32);
                console.log(`Key (ASCII): ${keyStr}`);
            } catch (e) {
                console.log("Key (ASCII): [non-printable]");
            }

            // Afficher l'IV en hex
            console.log(`${CYAN}IV (16 bytes):${RESET}`);
            console.log(hexdump(ivPtr, { length: 16, ansi: true }));

            // Sauvegarder pour onLeave si besoin
            this.keyPtr = keyPtr;
            this.ivPtr  = ivPtr;
        },

        onLeave: function (retval) {
            console.log(`Return value: ${retval} (1 = success)`);
        }
    });
} else {
    // Fallback : chercher dans libcrypto sans numéro de version
    const libs = Process.enumerateModules();
    const cryptoLib = libs.find(m => m.name.includes("libcrypto"));
    if (cryptoLib) {
        console.log(`[!] libcrypto trouvée : ${cryptoLib.name}`);
        console.log("[!] Adaptez le nom dans Module.findExportByName()");
    } else {
        console.log("[!] libcrypto non trouvée dans le processus");
    }
}
```

#### Exécution

```bash
$ frida -f ./ransomware_O2_strip -l hook_evp.js --no-pause
```

Le flag `-f` indique à Frida de **spawner** le processus (le lancer lui-même), ce qui garantit que le hook est en place avant la première instruction du programme. Le flag `--no-pause` laisse le programme s'exécuter immédiatement après l'injection.

Sortie (pour chaque fichier chiffré) :

```
[*] EVP_EncryptInit_ex trouvée à 0x7f...

=== EVP_EncryptInit_ex called ===
Key (32 bytes):
           0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
00000000  52 45 56 45 52 53 45 5f 45 4e 47 49 4e 45 45 52  REVERSE_ENGINEER
00000010  49 4e 47 5f 49 53 5f 46 55 4e 5f 32 30 32 35 21  ING_IS_FUN_2025!
Key (ASCII): REVERSE_ENGINEERING_IS_FUN_2025!  
IV (16 bytes):  
           0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
00000000  de ad be ef ca fe ba be 13 37 42 42 fe ed fa ce  .........7BB....
Return value: 0x1 (1 = success)
```

Le `hexdump` intégré de Frida produit une sortie formatée avec ASCII, plus lisible que la sortie brute de GDB.

### Script Frida étendu : capturer aussi le plaintext et le ciphertext

Pour une analyse complète, nous pouvons également hooker `EVP_EncryptUpdate` pour capturer les données avant et après chiffrement :

```javascript
// hook_evp_full.js — Capture complète du flux de chiffrement
// Usage : frida -f ./ransomware_O2_strip -l hook_evp_full.js --no-pause

let fileCount = 0;

// --- Hook EVP_EncryptInit_ex ---
const evpInit = Module.findExportByName("libcrypto.so.3", "EVP_EncryptInit_ex");  
Interceptor.attach(evpInit, {  
    onEnter: function (args) {
        fileCount++;
        console.log(`\n[${"=".repeat(50)}]`);
        console.log(`[*] Fichier #${fileCount}`);

        console.log("[*] Key:");
        console.log(hexdump(args[3], { length: 32, ansi: true }));
        console.log("[*] IV:");
        console.log(hexdump(args[4], { length: 16, ansi: true }));
    }
});

// --- Hook EVP_EncryptUpdate ---
const evpUpdate = Module.findExportByName("libcrypto.so.3", "EVP_EncryptUpdate");  
Interceptor.attach(evpUpdate, {  
    onEnter: function (args) {
        // args: ctx, out, out_len_ptr, in, in_len
        this.outPtr    = args[1];
        this.outLenPtr = args[2];
        const inPtr    = args[3];
        const inLen    = args[4].toInt32();

        console.log(`[*] EncryptUpdate — plaintext (${inLen} bytes):`);
        if (inLen <= 256) {
            console.log(hexdump(inPtr, { length: inLen, ansi: true }));
        } else {
            console.log(hexdump(inPtr, { length: 256, ansi: true }));
            console.log(`    ... (${inLen - 256} bytes supplémentaires)`);
        }
    },
    onLeave: function (retval) {
        const outLen = this.outLenPtr.readS32();
        console.log(`[*] EncryptUpdate — ciphertext (${outLen} bytes):`);
        if (outLen <= 256) {
            console.log(hexdump(this.outPtr, { length: outLen, ansi: true }));
        } else {
            console.log(hexdump(this.outPtr, { length: 256, ansi: true }));
            console.log(`    ... (${outLen - 256} bytes supplémentaires)`);
        }
    }
});

// --- Hook EVP_EncryptFinal_ex ---
const evpFinal = Module.findExportByName("libcrypto.so.3", "EVP_EncryptFinal_ex");  
Interceptor.attach(evpFinal, {  
    onEnter: function (args) {
        this.outPtr    = args[1];
        this.outLenPtr = args[2];
    },
    onLeave: function (retval) {
        const outLen = this.outLenPtr.readS32();
        console.log(`[*] EncryptFinal — padding block (${outLen} bytes):`);
        if (outLen > 0) {
            console.log(hexdump(this.outPtr, { length: outLen, ansi: true }));
        }
    }
});
```

Ce script fournit une **trace complète** du flux de chiffrement : pour chaque fichier, on voit le plaintext entrer dans `EncryptUpdate`, le ciphertext en sortir, et le bloc de padding produit par `EncryptFinal`. C'est l'équivalent d'une radiographie en temps réel du processus de chiffrement.

### Hooker `unlink` pour tracer les suppressions

Un script complémentaire peut intercepter les appels à `unlink` pour confirmer quels fichiers sont supprimés :

```javascript
// hook_unlink.js — Tracer les suppressions de fichiers
const unlinkAddr = Module.findExportByName(null, "unlink");

Interceptor.attach(unlinkAddr, {
    onEnter: function (args) {
        const path = args[0].readUtf8String();
        console.log(`[!] unlink("${path}")`);
    },
    onLeave: function (retval) {
        console.log(`    → retour : ${retval} (0 = succès)`);
    }
});
```

Sortie :

```
[!] unlink("/tmp/test/document.txt")
    → retour : 0x0 (0 = succès)
[!] unlink("/tmp/test/notes.md")
    → retour : 0x0 (0 = succès)
...
```

Cette trace confirme le comportement destructeur et fournit la **liste exacte des fichiers supprimés** — information directement exploitable pour le rapport d'incident.

---

## Vérification croisée : `ltrace`

Avant de conclure l'analyse dynamique, un rapide passage par `ltrace` offre une vue synthétique de tous les appels de bibliothèque, sans écrire de script :

```bash
$ ltrace -e 'EVP_*+opendir+readdir+unlink+fopen+fwrite' ./ransomware_O0 2>&1 | head -40
```

Le flag `-e` filtre les appels par nom. La sortie mêle chronologiquement les appels EVP, les opérations de fichier et les suppressions, offrant une vue chronologique condensée du comportement complet. C'est un outil de confirmation rapide, moins puissant que GDB ou Frida mais immédiat.

> 💡 `ltrace` ne fonctionne pas toujours correctement sur les binaires PIE modernes avec certaines versions de la libc. Si la sortie est vide ou incohérente, préférez `strace` pour les appels système ou les scripts Frida.

---

## Vérification complémentaire avec `strace`

`strace` trace les **appels système** (niveau noyau) plutôt que les appels de bibliothèque. Il confirme les opérations de fichier du point de vue du système d'exploitation :

```bash
$ strace -f -e trace=openat,read,write,unlink,getdents64 ./ransomware_O2_strip 2>&1 | grep /tmp/test
```

On y verra les `openat` sur les fichiers de `/tmp/test/`, les `read` de leur contenu, les `write` des fichiers `.locked`, et les `unlink` des originaux. Cette trace est une source d'IOC comportementaux pour le rapport : elle prouve, au niveau syscall, que le programme lit, écrit et supprime des fichiers dans l'arborescence cible.

`strace` confirme également l'hypothèse H7 (absence de communication réseau) : aucun appel `socket`, `connect`, `sendto` ou `recvfrom` n'apparaît dans la trace complète.

```bash
$ strace -f -e trace=network ./ransomware_O2_strip 2>&1
# (aucune sortie relative au réseau)
```

---

## Synthèse : tableau des confirmations dynamiques

| Hypothèse | Statut avant dynamique | Statut après dynamique | Preuve dynamique |  
|---|---|---|---|  
| H2 — AES-256-CBC | Confirmée (statique) | **Confirmée (dynamique)** | `EVP_aes_256_cbc` appelé, retour non-NULL passé à `EncryptInit` |  
| H3 — Clé = `REVERSE_...2025!` | Confirmée (statique) | **Confirmée définitivement** | 32 octets lus dans `$rcx` au moment de l'appel `EVP_EncryptInit_ex` |  
| H4 — IV = `DEADBEEF...FEEDFACE` | Confirmée (statique) | **Confirmée définitivement** | 16 octets lus dans `$r8` au moment de l'appel |  
| H6 — Suppression des originaux | Confirmée (statique) | **Confirmée (dynamique)** | `unlink()` tracé par Frida et `strace` sur chaque fichier |  
| H7 — Pas de réseau | Renforcée (statique) | **Confirmée (dynamique)** | `strace -e trace=network` muet |  
| H8 — Pas d'anti-debug | Renforcée (statique) | **Confirmée (dynamique)** | GDB et Frida fonctionnent sans obstruction |  
| N1 — Fichier lu intégralement en mémoire | Observation statique | **Confirmée (dynamique)** | Plaintext complet visible dans `EncryptUpdate` en un seul appel |  
| N5 — Clé/IV identiques pour chaque fichier | Observation statique | **Confirmée (dynamique)** | Script GDB/Frida : mêmes valeurs sur les 6 appels |

Toutes les hypothèses sont désormais au statut de **faits confirmés**. La clé et l'IV sont les données d'entrée nécessaires et suffisantes pour écrire le déchiffreur (section 27.6).

---

## Données exportées pour la suite

L'analyse dynamique produit trois artefacts exploitables :

1. **`/tmp/crypto_params.json`** — Export du script GDB Python : clé, IV, nombre d'appels. Entrée du déchiffreur Python.  
2. **Logs Frida** — Traces complètes des appels EVP avec hexdumps du plaintext et du ciphertext. Preuves pour le rapport.  
3. **Traces `strace`** — Appels système confirmant le comportement fichier et l'absence de réseau. IOC comportementaux pour le rapport.

Ces éléments, combinés aux résultats de l'analyse statique (graphe d'appels Ghidra, pattern ImHex, règles YARA), constituent le dossier complet à partir duquel nous allons maintenant écrire le déchiffreur et rédiger le rapport final.

⏭️ [Écriture du déchiffreur Python](/27-ransomware/06-dechiffreur-python.md)
