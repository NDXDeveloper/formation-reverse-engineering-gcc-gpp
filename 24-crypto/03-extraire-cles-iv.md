🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 24.3 — Extraire clés et IV depuis la mémoire avec GDB/Frida

> 🎯 **Objectif de cette section** : intercepter les clés de chiffrement, les IV et les buffers de données en clair au moment précis où ils transitent en mémoire, en combinant débogage classique (GDB) et instrumentation dynamique (Frida).

---

## Le principe fondamental

Aussi complexe que soit le schéma de dérivation d'une clé — passphrase hardcodée, KDF multi-passes, obfuscation XOR en cascade — il y a un moment incontournable : **la clé finale, en clair, doit être passée à la fonction de chiffrement**. C'est une contrainte architecturale absolue. L'algorithme de chiffrement a besoin de la clé sous forme de tableau d'octets pour fonctionner. Ce moment de vérité existe toujours, et c'est là qu'on intervient.

Le même raisonnement s'applique à l'IV, au plaintext avant chiffrement, et au ciphertext après déchiffrement. À un instant donné de l'exécution, toutes ces valeurs existent en mémoire simultanément. Le RE dynamique consiste à figer cet instant et à tout capturer.

Cette approche présente un avantage considérable par rapport à l'analyse statique pure : on n'a pas besoin de comprendre intégralement la logique de dérivation pour obtenir la clé. Si le binaire dérive sa clé via 47 étapes d'obfuscation imbriquées, on peut ignorer ces 47 étapes et simplement lire le résultat final. Comprendre la dérivation reste utile (pour écrire un keygen, par exemple), mais pour le déchiffrement immédiat d'un fichier, la clé brute suffit.

---

## Préparation : identifier les points d'interception

Avant de lancer GDB ou Frida, il faut savoir **où** poser ses sondes. Le travail des sections 24.1 et 24.2 a identifié l'algorithme (AES-256-CBC) et la bibliothèque (OpenSSL, API EVP). On sait donc quelles fonctions sont appelées et quels paramètres elles reçoivent.

### Cibles pour OpenSSL (API EVP)

La fonction centrale est `EVP_EncryptInit_ex`. Sa signature (simplifiée) est :

```c
int EVP_EncryptInit_ex(EVP_CIPHER_CTX *ctx,
                       const EVP_CIPHER *type,  // ex: EVP_aes_256_cbc()
                       ENGINE *impl,            // généralement NULL
                       const unsigned char *key, // ← 32 octets pour AES-256
                       const unsigned char *iv); // ← 16 octets pour CBC
```

Les paramètres `key` et `iv` sont des pointeurs vers les buffers en clair. Sur x86-64 avec la convention d'appel System V, ils sont passés dans les registres :

| Paramètre | Registre | Taille des données pointées |  
|---|---|---|  
| `ctx` | `rdi` | — (structure opaque) |  
| `type` | `rsi` | — (pointeur vers descripteur EVP) |  
| `impl` | `rdx` | — (généralement `NULL`) |  
| `key` | `rcx` | 32 octets (AES-256) |  
| `iv` | `r8` | 16 octets (AES-CBC) |

C'est le point d'interception idéal : au moment de l'appel, `rcx` pointe vers la clé et `r8` pointe vers l'IV.

D'autres points sont également intéressants selon ce qu'on cherche :

| Fonction | Ce qu'on capture | Registre clé |  
|---|---|---|  
| `EVP_EncryptInit_ex` | Clé + IV au moment de l'initialisation | `rcx` (key), `r8` (iv) |  
| `EVP_EncryptUpdate` | Plaintext avant chiffrement | `rcx` (in), `r8` (inl = taille) |  
| `EVP_EncryptFinal_ex` | Dernier bloc + padding | `rsi` (out) |  
| `SHA256` | Donnée hashée + hash résultant | `rdi` (data), `rdx` (md_out) |  
| `RAND_bytes` | IV ou nonce généré | `rdi` (buf), `rsi` (num) |  
| `derive_key` (notre fonction) | Clé après dérivation complète | Stack / registres locaux |

Pour un binaire utilisant une autre bibliothèque, les noms de fonctions changent mais le principe reste identique : on cible la fonction d'initialisation du chiffrement et on lit les paramètres.

---

## Méthode 1 : GDB — le scalpel chirurgical

GDB permet de figer l'exécution à l'instruction exacte et d'inspecter la mémoire octet par octet. C'est l'approche la plus précise et la plus contrôlée.

### Breakpoint sur `EVP_EncryptInit_ex`

On lance le binaire sous GDB avec le fichier à chiffrer en argument :

```bash
$ gdb -q ./crypto_O0
(gdb) break EVP_EncryptInit_ex
Breakpoint 1 at 0x... (EVP_EncryptInit_ex)
(gdb) run secret.txt
```

Le programme s'arrête au moment de l'appel. On inspecte les registres pour retrouver les pointeurs vers la clé et l'IV :

```bash
(gdb) info registers rcx r8
rcx            0x7fffffffd9e0      # pointeur vers la clé  
r8             0x7fffffffda10      # pointeur vers l'IV  
```

On lit ensuite les contenus pointés :

```bash
# Clé AES-256 : 32 octets à partir de l'adresse dans rcx
(gdb) x/32bx $rcx
0x7fffffffd9e0: 0xa3  0x1f  0x4b  0x72  0x8e  0xd0  0x55  0x19
0x7fffffffd9e8: 0xc7  0x3a  0x61  0x88  0xf2  0x0d  0xae  0x43
0x7fffffffd9f0: 0x5b  0xe9  0x17  0x6c  0xd4  0x82  0xf0  0x3e
0x7fffffffd9f8: 0xa1  0x56  0xc8  0x7d  0x09  0xbb  0x4f  0xe2

# IV AES-CBC : 16 octets à partir de l'adresse dans r8
(gdb) x/16bx $r8
0x7fffffffda10: 0x9c  0x71  0x2e  0xb5  0x38  0xf4  0xa0  0x6d
0x7fffffffda18: 0x1c  0x83  0xe7  0x52  0xbf  0x49  0x06  0xda
```

On a la clé et l'IV. C'est terminé pour le déchiffrement. Mais allons plus loin.

### Dumper la clé dans un fichier

Pour une utilisation programmatique (script de déchiffrement Python), on peut dumper directement les octets dans un fichier :

```bash
# Dumper la clé (32 octets) dans un fichier
(gdb) dump binary memory /tmp/key.bin $rcx ($rcx + 32)

# Dumper l'IV (16 octets)
(gdb) dump binary memory /tmp/iv.bin $r8 ($r8 + 16)
```

On peut vérifier le résultat immédiatement :

```bash
$ xxd /tmp/key.bin
00000000: a31f 4b72 8ed0 5519 c73a 6188 f20d ae43  ..Kr..U..:a....C
00000010: 5be9 176c d482 f03e a156 c87d 09bb 4fe2  [..l...>.V.}..O.

$ xxd /tmp/iv.bin
00000000: 9c71 2eb5 38f4 a06d 1c83 e752 bf49 06da  .q..8..m...R.I..
```

### Breakpoint sur `derive_key` — comprendre la dérivation

Si on veut aussi comprendre *comment* la clé est construite (pas seulement la capturer), on pose un breakpoint sur la fonction de dérivation interne :

```bash
(gdb) break derive_key
Breakpoint 2 at 0x... : file crypto.c, line 73.
(gdb) run secret.txt
```

On peut ensuite exécuter pas à pas (`next`, `step`) et observer chaque étape :

```bash
# Après l'appel à build_passphrase() — lire la passphrase en mémoire
(gdb) next
(gdb) x/s $rbp-0x50
0x7fffffffd990: "r3vers3_m3_1f_y0u_c4n!"

# Après SHA256() — lire le hash
(gdb) next
(gdb) x/32bx $rbp-0x30
0x7fffffffd9b0: 0x7d  0xb2  0xf5  0x9d  ...  # SHA-256 de la passphrase

# Après la boucle XOR — lire la clé finale
(gdb) next  # (sortie de la boucle)
(gdb) x/32bx <adresse de out_key>
```

On reconstruit ainsi toute la chaîne : passphrase → SHA-256 → XOR avec le masque → clé AES finale.

### Automatiser avec un script GDB Python

Pour les cas où on veut capturer les clés sans intervention manuelle (exécution en batch, ou capture sur un binaire qui chiffre plusieurs fichiers), un script GDB Python est la solution :

```python
# extract_crypto_params.py — Script GDB pour capturer clé + IV
# Usage : gdb -q -x extract_crypto_params.py ./crypto_O0

import gdb

class CryptoBreakpoint(gdb.Breakpoint):
    """Breakpoint sur EVP_EncryptInit_ex qui dumpe la clé et l'IV."""

    def __init__(self):
        super().__init__("EVP_EncryptInit_ex", gdb.BP_BREAKPOINT)
        self.silent = True  # pas de message standard de GDB
        self.count = 0

    def stop(self):
        self.count += 1
        inferior = gdb.selected_inferior()

        # Lire les registres (System V AMD64 : key=rcx, iv=r8)
        rcx = int(gdb.parse_and_eval("$rcx"))
        r8  = int(gdb.parse_and_eval("$r8"))

        # Lire la clé (32 octets)
        key_bytes = inferior.read_memory(rcx, 32)
        key_hex = key_bytes.tobytes().hex()

        # Lire l'IV (16 octets) — r8 peut être NULL si pas d'IV
        if r8 != 0:
            iv_bytes = inferior.read_memory(r8, 16)
            iv_hex = iv_bytes.tobytes().hex()
        else:
            iv_hex = "(null — no IV)"

        print(f"\n{'='*60}")
        print(f"[*] EVP_EncryptInit_ex call #{self.count}")
        print(f"[*] Key (32 bytes): {key_hex}")
        print(f"[*] IV  (16 bytes): {iv_hex}")
        print(f"{'='*60}\n")

        # Sauvegarder dans des fichiers
        with open(f"/tmp/key_{self.count}.bin", "wb") as f:
            f.write(key_bytes.tobytes())
        with open(f"/tmp/iv_{self.count}.bin", "wb") as f:
            f.write(iv_bytes.tobytes() if r8 != 0 else b"")

        print(f"[+] Saved to /tmp/key_{self.count}.bin"
              f" and /tmp/iv_{self.count}.bin")

        return False  # False = ne pas stopper, continuer l'exécution

# Installation du breakpoint
CryptoBreakpoint()

# Lancer le programme
gdb.execute("run secret.txt")
```

On lance avec :

```bash
$ gdb -q -batch -x extract_crypto_params.py ./crypto_O0
```

Le script capture silencieusement chaque appel à `EVP_EncryptInit_ex`, dumpe la clé et l'IV dans des fichiers, et laisse le programme s'exécuter normalement.

### GDB sur un binaire strippé

Sur `crypto_O2_strip`, les symboles locaux (`derive_key`, `build_passphrase`) ont disparu. Mais les symboles dynamiques (`EVP_EncryptInit_ex`) sont toujours accessibles car ils viennent de `libcrypto.so` :

```bash
$ gdb -q ./crypto_O2_strip
(gdb) break EVP_EncryptInit_ex
Breakpoint 1 at 0x... (in /lib/x86_64-linux-gnu/libcrypto.so.3)
(gdb) run secret.txt
```

Le breakpoint fonctionne. Les registres contiennent les mêmes valeurs. Le stripping ne protège pas les points d'interception situés dans les bibliothèques dynamiques.

Pour le binaire statique strippé, il n'y a plus de symbole `EVP_EncryptInit_ex`. Il faut alors trouver l'adresse de la fonction manuellement :

1. Identifier l'adresse via les constantes crypto (XREF depuis la S-box AES dans Ghidra, comme vu en 24.1–24.2).  
2. Remonter le graphe d'appels jusqu'à la fonction d'initialisation.  
3. Poser un breakpoint sur l'adresse trouvée : `break *0x00401a3c`.

C'est plus laborieux mais le résultat est le même.

---

## Méthode 2 : Frida — l'instrumentation en vol

Frida offre une approche complémentaire à GDB. Là où GDB fige l'exécution et attend nos commandes, Frida injecte du code JavaScript dans le processus cible, intercepte les appels en temps réel, et rapporte les résultats sans interrompre l'exécution. C'est particulièrement adapté quand le binaire effectue de nombreuses opérations crypto (chiffrement de plusieurs fichiers, communication réseau chiffrée en boucle…) et qu'on veut tout capturer d'un coup.

### Hooking de `EVP_EncryptInit_ex`

Le script Frida suivant intercepte chaque appel à `EVP_EncryptInit_ex` et affiche la clé et l'IV :

```javascript
// hook_crypto.js — Script Frida pour capturer les paramètres crypto
// Usage : frida -l hook_crypto.js -f ./crypto_O0 -- secret.txt

// Résoudre l'adresse de EVP_EncryptInit_ex dans libcrypto
const EVP_EncryptInit_ex = Module.findExportByName("libcrypto.so.3",
                                                    "EVP_EncryptInit_ex");
if (!EVP_EncryptInit_ex) {
    // Essayer sans numéro de version
    const alt = Module.findExportByName("libcrypto.so",
                                        "EVP_EncryptInit_ex");
    if (!alt) {
        console.log("[-] EVP_EncryptInit_ex not found");
    }
}

if (EVP_EncryptInit_ex) {
    Interceptor.attach(EVP_EncryptInit_ex, {

        onEnter: function(args) {
            // args[0] = ctx, args[1] = type, args[2] = impl
            // args[3] = key, args[4] = iv

            const keyPtr = args[3];
            const ivPtr  = args[4];

            console.log("\n" + "=".repeat(60));
            console.log("[*] EVP_EncryptInit_ex called");

            // Lire la clé (32 octets pour AES-256)
            if (!keyPtr.isNull()) {
                const keyBuf = keyPtr.readByteArray(32);
                console.log("[*] Key (32 bytes):");
                console.log(hexdump(keyBuf, { ansi: true }));
            } else {
                console.log("[*] Key: NULL (reusing previous key)");
            }

            // Lire l'IV (16 octets)
            if (!ivPtr.isNull()) {
                const ivBuf = ivPtr.readByteArray(16);
                console.log("[*] IV (16 bytes):");
                console.log(hexdump(ivBuf, { ansi: true }));
            } else {
                console.log("[*] IV: NULL (reusing previous IV)");
            }

            console.log("=".repeat(60));
        }
    });

    console.log("[+] Hooked EVP_EncryptInit_ex at " + EVP_EncryptInit_ex);
}
```

On lance avec `frida` en mode spawn (Frida démarre le processus) :

```bash
$ frida -l hook_crypto.js -f ./crypto_O0 -- secret.txt
```

La sortie affiche la clé et l'IV en hexdump lisible dès que `EVP_EncryptInit_ex` est appelé, puis le programme continue normalement et produit `secret.enc`.

### Hooking élargi : capturer le plaintext et le ciphertext

Pour voir les données elles-mêmes, on hooker aussi `EVP_EncryptUpdate` :

```javascript
const EVP_EncryptUpdate = Module.findExportByName("libcrypto.so.3",
                                                   "EVP_EncryptUpdate");

if (EVP_EncryptUpdate) {
    Interceptor.attach(EVP_EncryptUpdate, {

        onEnter: function(args) {
            // int EVP_EncryptUpdate(ctx, out, outl, in, inl)
            // args[3] = in (plaintext), args[4] = inl (taille)
            this.inPtr = args[3];
            this.inLen = args[4].toInt32();
            this.outPtr = args[1];
            this.outlPtr = args[2];

            console.log("\n[*] EVP_EncryptUpdate — plaintext ("
                        + this.inLen + " bytes):");
            if (this.inLen > 0 && this.inLen < 4096) {
                console.log(hexdump(this.inPtr.readByteArray(this.inLen),
                            { ansi: true }));
            }
        },

        onLeave: function(retval) {
            // Après l'appel : lire la taille écrite
            const written = this.outlPtr.readInt();
            if (written > 0) {
                console.log("[*] EVP_EncryptUpdate — ciphertext ("
                            + written + " bytes):");
                console.log(hexdump(this.outPtr.readByteArray(written),
                            { ansi: true }));
            }
        }
    });

    console.log("[+] Hooked EVP_EncryptUpdate at " + EVP_EncryptUpdate);
}
```

Avec ce hook, on voit le plaintext entrer et le ciphertext sortir. C'est un moyen de validation indépendant : on pourra vérifier que notre script de déchiffrement Python (section 24.5) produit exactement le même plaintext.

### Hooking de `derive_key` — capturer la passphrase et le hash

Pour les fonctions internes du binaire (pas d'une bibliothèque externe), Frida peut les localiser de deux manières :

**Si les symboles sont présents** (`crypto_O0`) :

```javascript
const derive_key = DebugSymbol.getFunctionByName("derive_key");  
console.log("[*] derive_key at " + derive_key);  
```

**Si le binaire est strippé** — on utilise l'adresse trouvée via l'analyse statique (Ghidra) :

```javascript
const base = Module.findBaseAddress("crypto_O2_strip");
// Adresse relative trouvée dans Ghidra, par exemple 0x1340
const derive_key = base.add(0x1340);
```

On peut ensuite hooker cette fonction pour capturer la passphrase et la clé dérivée :

```javascript
Interceptor.attach(derive_key, {

    onEnter: function(args) {
        // derive_key(unsigned char *out_key)
        // args[0] = pointeur vers le buffer de sortie (32 octets)
        this.outKeyPtr = args[0];
        console.log("[*] derive_key() called, output buffer at "
                    + this.outKeyPtr);
    },

    onLeave: function(retval) {
        // Au retour, le buffer contient la clé dérivée
        console.log("[*] derive_key() returned — derived key:");
        console.log(hexdump(this.outKeyPtr.readByteArray(32),
                    { ansi: true }));
    }
});
```

Le `onLeave` est crucial ici : c'est au retour de la fonction que le buffer contient la clé finale. Si on le lisait dans `onEnter`, le buffer serait encore vide ou contiendrait des données résiduelles.

### Frida sur un binaire lié statiquement

Quand la crypto est liée statiquement, `Module.findExportByName("libcrypto.so.3", ...)` ne trouvera rien (il n'y a pas de `libcrypto.so.3`). Il faut chercher la fonction directement dans le binaire :

```javascript
// Chercher par nom dans le binaire principal (si symboles présents)
const func = Module.findExportByName(null, "EVP_EncryptInit_ex");

// Ou par adresse (si strippé) — adresse trouvée via Ghidra
const base = Process.enumerateModules()[0].base;  
const func = base.add(0x00045a20);  // adresse relative dans le binaire  
```

Le principe est le même : une fois l'adresse connue, `Interceptor.attach` fonctionne de manière identique.

---

## Méthode 3 : Frida + `frida-trace` — la voie rapide

Pour un diagnostic initial sans écrire de script, `frida-trace` génère automatiquement des stubs de hooking :

```bash
$ frida-trace -f ./crypto_O0 -i "EVP_*" -- secret.txt
```

Frida-trace crée un fichier de handler JavaScript pour chaque fonction matchée dans `__handlers__/libcrypto.so.3/`. On peut éditer ces handlers pour ajouter l'affichage des paramètres. C'est un bon point de départ quand on ne connaît pas encore précisément quelles fonctions sont intéressantes : on hooker large (`EVP_*`, `SHA*`, `AES_*`) et on observe ce qui passe.

---

## GDB vs Frida : quand utiliser quoi

Les deux outils sont complémentaires. Le choix dépend du contexte :

| Critère | GDB | Frida |  
|---|---|---|  
| **Précision** | Instruction par instruction | Fonction par fonction |  
| **Interruption** | Fige le programme | Continue l'exécution |  
| **Exploration** | Idéal pour comprendre la logique pas à pas | Idéal pour capturer des données en masse |  
| **Automatisation** | Script GDB Python | Script JavaScript, r2pipe |  
| **Anti-debug** | Détectable (`ptrace`) | Plus discret (injection) |  
| **Binaire strippé** | Breakpoint par adresse | Idem, mais hook plus ergonomique |  
| **Multi-appels** | Fastidieux manuellement | Naturel (hook persistant) |

**Recommandation pratique** : commencer par GDB pour comprendre le flux et valider les hypothèses sur un seul appel, puis basculer sur Frida pour capturer tout en production ou automatiser l'extraction.

---

## Précautions et pièges courants

**Le timing compte.** Si le binaire efface la clé de la mémoire après usage (`memset(key, 0, KEY_LEN)` — comme notre `crypto.c` le fait), il faut capturer la clé *avant* le nettoyage. Le breakpoint doit être sur `EVP_EncryptInit_ex` (la clé y est encore vivante), pas après le retour de la fonction wrapper qui fait le `memset`.

**ASLR et adresses.** Les adresses absolues changent à chaque exécution à cause de l'ASLR. Pour les breakpoints par adresse sur un binaire PIE, il faut soit désactiver l'ASLR temporairement (`echo 0 | sudo tee /proc/sys/kernel/randomize_va_space`), soit utiliser des offsets relatifs à la base du module (Frida gère cela nativement avec `Module.findBaseAddress`).

**Endianness.** Les octets lus en mémoire sur x86-64 sont en little-endian. Si l'algorithme attend la clé comme un tableau d'octets (ce qui est le cas d'AES), il n'y a pas de conversion à faire — les octets sont dans le bon ordre. En revanche, si on compare avec des constantes 32/64 bits (valeurs SHA-256 par exemple), il faut penser à l'inversion.

**`EVP_EncryptInit_ex` peut être appelé avec `key = NULL`.** OpenSSL permet de séparer l'initialisation en deux appels : un premier pour définir le cipher, un second pour fournir la clé et l'IV. Il faut gérer le cas `NULL` dans les scripts (notre script Frida ci-dessus le fait).

**Détection de débogueur.** Certains binaires (malwares, logiciels protégés) détectent la présence d'un débogueur via `ptrace(PTRACE_TRACEME)`, vérification de `/proc/self/status`, ou timing checks. Les contournements sont traités en détail au chapitre 19 (section 19.7). En résumé : `LD_PRELOAD` d'un stub `ptrace`, patching du check, ou utilisation de Frida (qui n'utilise pas `ptrace` sur Linux par défaut quand on utilise le mode spawn avec `frida-gadget`).

---

## Application complète sur `crypto_O0`

Récapitulons le flux complet d'extraction sur notre binaire d'entraînement :

**1. Identification préalable** (sections 24.1 et 24.2) : AES-256-CBC via OpenSSL, SHA-256 pour la dérivation.

**2. Capture GDB de la clé et de l'IV** :
```bash
$ gdb -q ./crypto_O0
(gdb) break EVP_EncryptInit_ex
(gdb) run secret.txt
(gdb) dump binary memory /tmp/key.bin $rcx ($rcx + 32)
(gdb) dump binary memory /tmp/iv.bin $r8 ($r8 + 16)
(gdb) continue
```

**3. Capture GDB de la passphrase** (pour comprendre la dérivation) :
```bash
(gdb) break build_passphrase
(gdb) run secret.txt
(gdb) finish
(gdb) x/s <adresse du buffer out>
"r3vers3_m3_1f_y0u_c4n!"
```

**4. Validation croisée avec Frida** :
```bash
$ frida -l hook_crypto.js -f ./crypto_O0 -- secret.txt
```

On vérifie que les valeurs capturées par Frida correspondent exactement à celles de GDB. Si c'est le cas, on a une confiance élevée dans nos données.

**5. Résultat** : on dispose de la clé AES-256 (32 octets), de l'IV (16 octets), et on connaît la passphrase source (`r3vers3_m3_1f_y0u_c4n!`) ainsi que la logique de dérivation (SHA-256 → XOR masque). Toutes les pièces sont en main pour la section 24.5 (reproduction en Python).

Mais avant cela, la section suivante examine de plus près le fichier `secret.enc` lui-même, pour comprendre comment les données chiffrées sont structurées et emballées.

---


⏭️ [Visualiser le format chiffré et les structures avec ImHex](/24-crypto/04-visualiser-format-imhex.md)
