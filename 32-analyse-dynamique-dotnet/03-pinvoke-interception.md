🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 32.3 — Intercepter des appels P/Invoke (pont .NET → bibliothèques natives GCC)

> 📁 **Fichiers utilisés** : `binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64/LicenseChecker.dll`, `binaries/ch32-dotnet/native/libnative_check.so`  
> 🔧 **Outils** : Frida, GDB/GEF, objdump, nm, strace/ltrace, dnSpy  
> 📖 **Prérequis** : [Chapitre 5 — Outils d'inspection](/05-outils-inspection-base/README.md), [Chapitre 11 — GDB](/11-gdb/README.md), [Chapitre 13 — Frida](/13-frida/README.md), [Section 32.2](/32-analyse-dynamique-dotnet/02-hooking-frida-clr.md)

---

## Le pont managé–natif : pourquoi c'est un point chaud du RE

Les applications .NET ne vivent pas dans un univers clos. Tôt ou tard, le code managé a besoin d'interagir avec le monde extérieur : appeler une bibliothèque cryptographique écrite en C, invoquer une API système non exposée par le framework, ou — comme dans notre `LicenseChecker` — déléguer une partie de la logique de validation à une bibliothèque native compilée avec GCC.

Le mécanisme qui rend cela possible s'appelle **P/Invoke** (Platform Invocation Services). C'est un système de marshalling qui permet à une méthode C# d'appeler une fonction exportée par une bibliothèque partagée native (`.so` sous Linux, `.dll` sous Windows). Le CLR se charge de convertir les types managés en types natifs, d'épingler les objets en mémoire pour que le GC ne les déplace pas pendant l'appel, de passer les arguments selon la convention d'appel native, puis de reconvertir la valeur de retour en type managé.

Pour le reverse engineer, les appels P/Invoke sont des points d'intérêt majeurs pour trois raisons. D'abord, ils marquent les frontières de confiance : le code qui traverse le pont managé–natif est souvent du code sensible (vérifications de licence, crypto, interactions système). Ensuite, ils constituent une surface d'interception double : on peut les hooker côté managé (avant la traversée) ou côté natif (après la traversée). Enfin, ils nécessitent de maîtriser les deux mondes — les outils .NET vus en section 32.1–32.2 et les outils natifs des chapitres 5 à 15.

Notre `LicenseChecker` incarne parfaitement ce scénario. Les segments A et C de la clé sont calculés en C# pur, mais le segment B dépend de `compute_native_hash()` et le segment D dépend partiellement de `compute_checksum()` — deux fonctions exportées par `libnative_check.so`, une bibliothèque compilée avec GCC. Le script keygen Frida de la section 32.2 était incomplet précisément parce qu'il ne capturait pas les valeurs calculées côté natif. Cette section comble cette lacune.

## Anatomie d'un appel P/Invoke

Avant d'intercepter, comprenons ce qui se passe quand le CLR exécute un appel P/Invoke. Prenons l'appel à `ComputeNativeHash` dans notre `NativeBridge.cs` :

```csharp
[DllImport("libnative_check.so", CallingConvention = CallingConvention.Cdecl,
           EntryPoint = "compute_native_hash")]
public static extern uint ComputeNativeHash(byte[] data, int length);
```

L'attribut `[DllImport]` indique au CLR trois choses : le nom de la bibliothèque à charger (`libnative_check.so`), le nom de la fonction native à appeler (`compute_native_hash`), et la convention d'appel à utiliser (`Cdecl`). Quand le code C# appelle `NativeBridge.ComputeNativeHash(data, data.Length)`, la séquence suivante s'exécute dans le runtime :

**Résolution de la bibliothèque.** Au premier appel, le CLR charge `libnative_check.so` en utilisant le mécanisme de chargement dynamique du système (`dlopen` sous Linux, `LoadLibrary` sous Windows). La bibliothèque est recherchée dans le répertoire de l'application, puis dans les chemins standard (`LD_LIBRARY_PATH`, `/usr/lib`, etc.). Si la bibliothèque n'est pas trouvée, une `DllNotFoundException` est levée.

**Résolution du symbole.** Le CLR recherche le symbole `compute_native_hash` dans la bibliothèque chargée (`dlsym` sous Linux, `GetProcAddress` sous Windows). Si le symbole n'existe pas, une `EntryPointNotFoundException` est levée.

**Marshalling des arguments.** Le CLR convertit les arguments managés en types natifs. Le `byte[] data` est un tableau managé — le CLR l'épingle en mémoire (pour empêcher le GC de le déplacer) et passe un pointeur vers son contenu au code natif. Le `int length` est un type blittable (sa représentation mémoire est identique en managé et en natif) et est passé directement.

**Appel natif.** Le CLR effectue l'appel à la fonction native en suivant la convention Cdecl : les arguments sont passés via les registres `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` (System V AMD64 ABI, exactement comme vu au chapitre 3). L'exécution quitte le monde managé et entre dans le code machine de `libnative_check.so`.

**Retour et dé-marshalling.** La valeur de retour (`uint32_t` en C) est lue depuis le registre `rax` et convertie en `uint` C#. Les objets épinglés sont libérés. L'exécution reprend dans le monde managé.

Cette séquence offre de multiples points d'interception. On peut intervenir avant le marshalling (côté CLR, avec Frida CLR ou dnSpy), après le marshalling mais avant l'appel natif (sur le stub P/Invoke), au niveau de la fonction native elle-même (avec Frida natif ou GDB), ou au retour (pour modifier la valeur renvoyée au monde managé).

## Étape 1 — Identifier les appels P/Invoke en analyse statique

Avant toute instrumentation dynamique, on commence par inventorier les appels P/Invoke présents dans l'assembly. Plusieurs approches complémentaires permettent de le faire.

### Depuis dnSpy

En ouvrant `LicenseChecker.dll` dans dnSpy et en naviguant vers la classe `NativeBridge`, on voit immédiatement les déclarations `[DllImport]`. dnSpy les décompile fidèlement, y compris les attributs de marshalling. On identifie trois fonctions P/Invoke : `ComputeNativeHash`, `ComputeChecksum` et `VerifyIntegrity`, toutes pointant vers `libnative_check.so`.

On peut aussi utiliser la fonctionnalité **Analyze** de dnSpy (clic droit → Analyze sur une méthode P/Invoke) pour trouver tous les sites d'appel dans le code managé. Pour `ComputeNativeHash`, l'analyse révèle qu'elle est appelée depuis `LicenseValidator.CheckSegmentB()`. Pour `ComputeChecksum`, depuis `LicenseValidator.ComputeFinalChecksum()`. Pour `VerifyIntegrity`, aucun site d'appel — elle est déclarée mais jamais utilisée dans le flux principal (c'est notre cible d'exercice bonus).

### Depuis la ligne de commande

Pour un triage rapide sans GUI, on peut utiliser `monodis` (si Mono est installé) ou `ildasm` pour lister les imports natifs :

```bash
# Avec monodis (Mono)
monodis --implmap LicenseChecker.dll
```

La sortie listera les méthodes P/Invoke avec leur bibliothèque cible et leur point d'entrée natif. C'est l'équivalent .NET de `ldd` + `nm` pour les binaires natifs.

### Côté natif : inspecter libnative_check.so

La bibliothèque native est un `.so` ELF classique, analysable avec tous les outils de la partie II. On applique le workflow de triage du chapitre 5 :

```bash
# Type du fichier
file native/libnative_check.so

# Symboles exportés
nm -D native/libnative_check.so
# → T compute_native_hash
# → T compute_checksum
# → T verify_integrity

# Chaînes de caractères
strings native/libnative_check.so
# → NATIVERE   (le salt natif !)

# Désassemblage des fonctions exportées
objdump -d -M intel native/libnative_check.so | less
```

La commande `nm -D` confirme que les trois fonctions sont bien exportées (type `T` = texte/code, visibilité globale). La commande `strings` révèle le salt `"NATIVERE"` — un indice que le reverse engineer attentif repèrera et comparera avec le salt C# `"REV3RSE!"`. Le désassemblage avec `objdump` donne accès au code machine des fonctions, analysable avec les techniques du chapitre 7.

Pour une analyse plus approfondie, on importerait `libnative_check.so` dans Ghidra (chapitre 8), qui produirait un décompilé C lisible des trois fonctions. C'est la démarche à suivre pour comprendre l'algorithme de hash natif dans le cadre d'un keygen complet.

## Étape 2 — Tracer les appels P/Invoke avec strace et ltrace

Avant de sortir Frida, les outils de traçage système offrent un premier aperçu des interactions natives. Ce sont les mêmes outils que ceux vus au chapitre 5 — ils fonctionnent identiquement sur un processus .NET, puisque le code natif est exécuté dans le même espace d'adressage.

```bash
# Tracer les appels système (chargement de la bibliothèque)
strace -f -e trace=openat ./LicenseChecker 2>&1 | grep native
```

> ⚠️ **Attention au retour anticipé.** La méthode `Validate()` est séquentielle : si le segment A est incorrect, elle retourne immédiatement sans jamais appeler `CheckSegmentB`, et le CLR ne charge jamais `libnative_check.so`. Pour que `strace` capture le `openat` de la bibliothèque, il faut fournir une clé dont le **segment A est correct** (obtenu au préalable via dnSpy en §32.1 ou via le hook Frida en §32.2). Par exemple, si le segment A pour `alice` est `7B3F` :

```bash
strace -f -e trace=openat ./LicenseChecker 2>&1 <<< "alice
7B3F-0000-0000-0000" | grep native
# → openat(AT_FDCWD, "./libnative_check.so", O_RDONLY|O_CLOEXEC) = 3
```

Le filtre sur `openat` montre le moment où le CLR charge la bibliothèque native. Le descripteur de fichier retourné (ici `3`) confirme que le chargement a réussi. Avec une clé dont le segment A est faux, cette ligne n'apparaîtrait pas.

```bash
# Tracer les appels de bibliothèques partagées
# (segment A correct pour atteindre le code natif)
ltrace -e 'compute_*' -f ./LicenseChecker <<< "alice
7B3F-0000-0000-0000"
# → compute_native_hash(0x7f..., 5) = 0x<valeur_segment_B>
```

La commande `ltrace` avec un filtre sur `compute_*` capture les appels aux fonctions de `libnative_check.so`, avec leurs arguments et valeurs de retour. On voit directement la valeur retournée par `compute_native_hash` — c'est le segment B attendu. En revanche, `compute_checksum` n'apparaît pas ici : la validation échoue au segment B (on a mis `0000`) et retourne avant d'atteindre `ComputeFinalChecksum`. Pour capturer `compute_checksum`, il faudrait fournir une clé avec les segments A **et** B corrects.

> ⚠️ `ltrace` peut être instable avec les processus .NET modernes en raison de la complexité du runtime CoreCLR. Si `ltrace` plante ou ne capture rien, on passe directement à Frida.

## Étape 3 — Interception native avec Frida

C'est l'approche la plus puissante et la plus fiable. On utilise les capacités natives de Frida (les mêmes que celles du chapitre 13) pour hooker directement les fonctions exportées par `libnative_check.so`. Le bridge CLR n'est pas nécessaire ici — on travaille au niveau du code machine.

### Hooker `compute_native_hash` pour capturer le segment B

```javascript
// hook_native_hash.js
// Intercepter compute_native_hash() dans libnative_check.so
//
// Usage :
//   frida -f ./LicenseChecker -l hook_native_hash.js
//   (ou frida -p <PID> -l hook_native_hash.js)

"use strict";

function hookNativeHash() {
    // Chercher la fonction exportée dans libnative_check.so
    const addr = Module.findExportByName("libnative_check.so",
                                         "compute_native_hash");
    if (!addr) {
        console.log("[-] compute_native_hash introuvable.");
        console.log("    La bibliothèque est-elle chargée ?");
        console.log("    (L'appel P/Invoke déclenche le chargement au premier appel)");
        return;
    }

    console.log(`[+] compute_native_hash @ ${addr}`);

    Interceptor.attach(addr, {
        onEnter: function (args) {
            // Signature C : uint32_t compute_native_hash(
            //                    const uint8_t *data, int length)
            //
            // System V AMD64 ABI :
            //   args[0] = rdi = pointeur vers data (username UTF-8)
            //   args[1] = rsi = length

            this.dataPtr = args[0];
            this.length  = args[1].toInt32();

            // Lire le contenu du buffer (le username en minuscules)
            const username = Memory.readUtf8String(this.dataPtr, this.length);
            console.log(`\n[+] compute_native_hash() appelé`);
            console.log(`    data    = "${username}"`);
            console.log(`    length  = ${this.length}`);

            // Bonus : dumper les octets bruts du buffer
            const bytes = Memory.readByteArray(this.dataPtr, this.length);
            console.log(`    hex     = ${hexdump(bytes, { length: this.length })}`);
        },

        onLeave: function (retval) {
            // La valeur de retour est un uint32_t dans rax.
            // Seuls les 16 bits bas sont utilisés par le code C#.
            const raw   = retval.toUInt32();
            const seg_b = raw & 0xFFFF;
            const hex   = seg_b.toString(16).toUpperCase().padStart(4, "0");

            console.log(`    retour  = 0x${raw.toString(16)} (brut)`);
            console.log(`    ↳ Segment B attendu : ${hex}`);

            // Stocker pour usage dans d'autres hooks
            this.segB = seg_b;
        }
    });

    console.log("[+] Hook installé sur compute_native_hash()");
}

// Attendre que la bibliothèque soit chargée par le CLR.
// Le chargement se fait au premier appel P/Invoke (lazy loading).
// Si on est en mode spawn, la lib n'est pas encore chargée au démarrage.

function waitForLibAndHook() {
    const mod = Process.findModuleByName("libnative_check.so");
    if (mod) {
        hookNativeHash();
    } else {
        console.log("[*] libnative_check.so pas encore chargée, surveillance...");

        // Surveiller le chargement de nouveaux modules
        const interval = setInterval(() => {
            if (Process.findModuleByName("libnative_check.so")) {
                clearInterval(interval);
                console.log("[+] libnative_check.so détectée !");
                hookNativeHash();
            }
        }, 50);
    }
}

waitForLibAndHook();
```

Quelques points méritent attention.

**Le chargement paresseux.** Le CLR charge `libnative_check.so` au premier appel P/Invoke, pas au démarrage du processus. Si on attache Frida avant ce premier appel, la bibliothèque n'est pas encore en mémoire et `Module.findExportByName` retourne `null`. Le script ci-dessus gère cette situation avec un polling périodique qui attend l'apparition du module.

**La lecture du buffer.** L'argument `data` est un pointeur vers le contenu du tableau `byte[]` C#. Le CLR a épinglé le tableau en mémoire avant l'appel — le pointeur est donc valide pendant toute la durée de l'exécution native. On peut le lire avec `Memory.readUtf8String` (si le contenu est une chaîne UTF-8) ou avec `Memory.readByteArray` (pour un dump brut).

**La convention d'appel.** C'est exactement la System V AMD64 ABI vue au chapitre 3 : premier argument dans `rdi` (args[0]), deuxième dans `rsi` (args[1]), valeur de retour dans `rax`. Rien de spécifique à .NET ici — une fois que l'exécution a traversé le pont P/Invoke, c'est du code natif standard.

### Hooker `compute_checksum` pour capturer la partie native du segment D

```javascript
// hook_checksum_native.js
// Intercepter compute_checksum() dans libnative_check.so

"use strict";

function hookChecksum() {
    const addr = Module.findExportByName("libnative_check.so",
                                         "compute_checksum");
    if (!addr) {
        console.log("[-] compute_checksum introuvable.");
        return;
    }

    console.log(`[+] compute_checksum @ ${addr}`);

    Interceptor.attach(addr, {
        onEnter: function (args) {
            // uint32_t compute_checksum(uint32_t seg_a,
            //                           uint32_t seg_b,
            //                           uint32_t seg_c)
            this.segA = args[0].toUInt32() & 0xFFFF;
            this.segB = args[1].toUInt32() & 0xFFFF;
            this.segC = args[2].toUInt32() & 0xFFFF;

            const fmtHex = (v) => v.toString(16).toUpperCase().padStart(4, "0");

            console.log(`\n[+] compute_checksum() appelé`);
            console.log(`    seg_a = 0x${fmtHex(this.segA)}`);
            console.log(`    seg_b = 0x${fmtHex(this.segB)}`);
            console.log(`    seg_c = 0x${fmtHex(this.segC)}`);
        },

        onLeave: function (retval) {
            const val = retval.toUInt32() & 0xFFFF;
            const hex = val.toString(16).toUpperCase().padStart(4, "0");
            console.log(`    retour (partie native du seg D) = 0x${hex}`);
        }
    });

    console.log("[+] Hook installé sur compute_checksum()");
}

// Même pattern de chargement différé
const interval = setInterval(() => {
    if (Process.findModuleByName("libnative_check.so")) {
        clearInterval(interval);
        hookChecksum();
    }
}, 50);
```

## Étape 4 — Script combiné : le keygen complet

À la section 32.2, notre keygen Frida était incomplet : il capturait le segment A (calculé côté CLR) mais pas le segment B (calculé côté natif). De plus, la méthode `Validate()` originale est séquentielle avec retour anticipé — si le segment A est incorrect, elle retourne immédiatement sans jamais appeler les méthodes de calcul des segments suivants. Avec une clé bidon, seul `ComputeUserHash` est atteint.

La solution : le hook sur `Validate()` appelle **directement** chaque méthode de calcul dans le bon ordre, en leur passant les bonnes valeurs, au lieu de déléguer à l'implémentation originale. Côté natif, les hooks sur `compute_native_hash` et `compute_checksum` se déclenchent de manière synchrone pendant ces appels et capturent les valeurs natives.

```javascript
// keygen_complete.js
// Keygen complet combinant hooking CLR et hooking natif
//
// Usage :
//   frida -f ./LicenseChecker --runtime=clr -l keygen_complete.js
//   Entrer un username quelconque et une clé bidon (ex: 0000-0000-0000-0000)

"use strict";

const seg     = { A: null, B: null, C: null, D: null };  
const fmtHex  = (v) => v !== null  
    ? (v >>> 0).toString(16).toUpperCase().padStart(4, "0")
    : "????";

// ═══════════════════════════════════════════════
//  HOOKS NATIFS — segment B et partie native de D
// ═══════════════════════════════════════════════

function installNativeHooks() {
    // ── compute_native_hash → segment B ──
    const hashAddr = Module.findExportByName(
        "libnative_check.so", "compute_native_hash");

    if (hashAddr) {
        Interceptor.attach(hashAddr, {
            onLeave: function (retval) {
                seg.B = retval.toUInt32() & 0xFFFF;
                console.log(`  [natif] compute_native_hash → 0x${fmtHex(seg.B)}  (segment B)`);
            }
        });
        console.log("[+] Hook natif installé : compute_native_hash");
    }

    // ── compute_checksum → partie native du segment D ──
    const chkAddr = Module.findExportByName(
        "libnative_check.so", "compute_checksum");

    if (chkAddr) {
        Interceptor.attach(chkAddr, {
            onLeave: function (retval) {
                const nativePart = retval.toUInt32() & 0xFFFF;
                console.log(`  [natif] compute_checksum → 0x${fmtHex(nativePart)}  (checksum natif)`);
            }
        });
        console.log("[+] Hook natif installé : compute_checksum");
    }
}

// ═══════════════════════════════════════════════
//  HOOKS CLR — appels directs depuis Validate
// ═══════════════════════════════════════════════

function installCLRHooks() {
    const klass = CLR.classes["LicenseChecker.LicenseValidator"];
    if (!klass) {
        console.log("[-] LicenseValidator introuvable côté CLR");
        return;
    }

    // ── Hook principal : Validate ──
    // L'original Validate() retourne dès le premier check raté.
    // On court-circuite ce flux en appelant directement chaque
    // méthode de calcul avec les bonnes valeurs.

    klass.methods["Validate"].implementation = function (username, licenseKey) {
        console.log(`\n[+] Validate("${username}", "${licenseKey}")`);
        seg.A = seg.B = seg.C = seg.D = null;

        // Segment A — appel direct à ComputeUserHash (C# pur)
        seg.A = this.ComputeUserHash(username) & 0xFFFF;
        console.log(`  [CLR] Segment A = 0x${fmtHex(seg.A)}`);

        // Segment B — déclencher CheckSegmentB pour provoquer
        // l'appel P/Invoke à compute_native_hash.
        // Le hook natif (ci-dessus) capture seg.B de manière
        // synchrone pendant cet appel.
        try {
            this.CheckSegmentB(username, 0);
        } catch (e) {
            console.log(`  [!] CheckSegmentB exception : ${e}`);
        }

        // Segment C — appel avec les vrais A et B
        if (seg.B !== null) {
            seg.C = this.ComputeCrossXor(seg.A, seg.B) & 0xFFFF;
            console.log(`  [CLR] Segment C = 0x${fmtHex(seg.C)}`);
        }

        // Segment D — appel avec les vrais A, B, C
        if (seg.B !== null && seg.C !== null) {
            seg.D = this.ComputeFinalChecksum(
                seg.A, seg.B, seg.C, username) & 0xFFFF;
            console.log(`  [CLR] Segment D = 0x${fmtHex(seg.D)}`);
        }

        // Affichage de la clé
        console.log("\n╔═════════════════════════════════════════════╗");
        console.log("║      KEYGEN COMPLET — CLR + NATIF           ║");
        console.log("╠═════════════════════════════════════════════╣");
        console.log(`║  Username  : ${username.padEnd(28)}║`);
        console.log(`║  Segment A : ${fmtHex(seg.A).padEnd(28)}║`);
        console.log(`║  Segment B : ${fmtHex(seg.B).padEnd(28)}║`);
        console.log(`║  Segment C : ${fmtHex(seg.C).padEnd(28)}║`);
        console.log(`║  Segment D : ${fmtHex(seg.D).padEnd(28)}║`);
        console.log("║                                             ║");

        if (seg.A !== null && seg.B !== null &&
            seg.C !== null && seg.D !== null) {
            const fullKey = `${fmtHex(seg.A)}-${fmtHex(seg.B)}`
                          + `-${fmtHex(seg.C)}-${fmtHex(seg.D)}`;
            console.log(`║  CLÉ VALIDE : ${fullKey.padEnd(28)}║`);
        } else {
            console.log("║  ⚠ Capture incomplète (lib native ?)     ║");
        }
        console.log("╚═════════════════════════════════════════════╝\n");

        // Appeler l'original (il échouera, mais le programme
        // affichera son message normalement).
        return this.Validate(username, licenseKey);
    };

    console.log("[+] Hooks CLR installés");
}

// ═══════════════════════════════════════════════
//  ORCHESTRATION
// ═══════════════════════════════════════════════

// Les hooks natifs doivent attendre le chargement de libnative_check.so.
// Les hooks CLR doivent attendre le chargement de l'assembly.
// On surveille les deux en parallèle.

let nativeReady = false;  
let clrReady    = false;  

const poll = setInterval(() => {
    if (!nativeReady && Process.findModuleByName("libnative_check.so")) {
        installNativeHooks();
        nativeReady = true;
    }

    if (!clrReady && CLR && CLR.assemblies &&
        CLR.assemblies["LicenseChecker"]) {
        installCLRHooks();
        clrReady = true;
    }

    if (nativeReady && clrReady) {
        clearInterval(poll);
        console.log("\n[+] Tous les hooks sont en place. Entrez un username.\n");
    }
}, 100);
```

Ce script illustre la puissance de la combinaison CLR + natif. Le hook sur `Validate()` appelle directement chaque méthode de calcul dans l'ordre — `ComputeUserHash`, `CheckSegmentB`, `ComputeCrossXor`, `ComputeFinalChecksum` — en leur passant les valeurs correctes (et non celles de la clé bidon). Les hooks natifs se déclenchent de manière synchrone pendant les appels P/Invoke traversés par `CheckSegmentB` et `ComputeFinalChecksum`, capturant le segment B et le checksum natif. Le résultat est un keygen fonctionnel en une seule passe, qui n'a nécessité aucune compréhension des algorithmes internes.

## Étape 5 — Interception avec GDB

Frida n'est pas le seul outil capable d'intercepter les appels P/Invoke côté natif. GDB, avec les extensions GEF ou pwndbg (chapitre 12), fonctionne tout aussi bien sur un processus .NET.

### Attacher GDB à un processus .NET

```bash
# Lancer l'application
cd binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64  
LD_LIBRARY_PATH=. ./LicenseChecker &  
PID=$!  

# Attacher GDB
gdb -p $PID
```

GDB s'attache au processus. On verra un grand nombre de threads (le runtime CoreCLR crée des threads pour le GC, le JIT, le finaliseur, etc.). Le thread principal est généralement bloqué en attente d'entrée (sur `read` ou `fgets`).

### Poser un breakpoint sur la fonction native

```gdb
# Charger les symboles de libnative_check.so (si elle est déjà chargée)
info sharedlibrary
# → libnative_check.so devrait apparaître dans la liste

# Breakpoint sur la fonction exportée
break compute_native_hash  
break compute_checksum  

# Reprendre l'exécution
continue
```

À ce stade, le programme attend que l'utilisateur entre un username et une clé. Il faut fournir une clé dont le **segment A est correct** pour que `Validate()` dépasse le premier check et atteigne `CheckSegmentB` — c'est là que le P/Invoke appelle `compute_native_hash`. Si le segment A pour `alice` est `7B3F`, on tape `alice` et `7B3F-0000-0000-0000` dans le terminal de l'application. Le breakpoint sur `compute_native_hash` se déclenche.

> 💡 Si on entre une clé avec un segment A incorrect, `Validate()` retourne avant d'appeler `CheckSegmentB` et les breakpoints natifs ne se déclenchent jamais. C'est la conséquence directe du flux séquentiel avec retour anticipé du code C#.

```gdb
# On est arrêté à l'entrée de compute_native_hash.
# Inspecter les arguments (System V AMD64 ABI) :
#   rdi = pointeur vers data
#   rsi = length

info registers rdi rsi

# Lire le contenu du buffer (le username en minuscules)
x/s $rdi
# → "alice"

# Afficher la longueur
print (int)$rsi
# → 5

# Exécuter jusqu'au retour de la fonction
finish

# Lire la valeur de retour (dans rax)
print/x $rax
# → 0x<segment B attendu>
```

La commande `finish` exécute la fonction jusqu'à son `ret` et s'arrête immédiatement après. La valeur dans `rax` est le hash natif — le segment B attendu. C'est la même information que celle capturée par le hook Frida, obtenue ici avec GDB.

> 💡 Si `libnative_check.so` n'est pas encore chargée au moment de l'attachement (parce que l'utilisateur n'a pas encore soumis de clé), GDB ne pourra pas résoudre le symbole `compute_native_hash`. On peut alors utiliser un *catchpoint* sur `dlopen` pour détecter le chargement :  
>  
> ```gdb  
> catch syscall openat  
> continue  
> # Soumettre une clé dans l'application...  
> # GDB s'arrête sur l'openat de libnative_check.so  
> # Maintenant la bibliothèque est chargée, on peut poser le breakpoint  
> break compute_native_hash  
> continue  
> ```

## Le marshalling en détail : ce qu'il faut savoir

Quand on intercepte un appel P/Invoke côté natif, les arguments qu'on observe sont le résultat du marshalling effectué par le CLR. Comprendre ce marshalling est essentiel pour interpréter correctement les valeurs observées dans GDB ou Frida.

### Types blittables

Certains types ont une représentation mémoire identique en C# et en C. Le CLR les passe directement, sans transformation. C'est le cas de `int`, `uint`, `long`, `double`, `byte`, et des pointeurs (`IntPtr`). Dans notre `LicenseChecker`, les arguments `uint segA`, `uint segB`, `uint segC` de `compute_checksum` sont blittables : les valeurs dans les registres `rdi`, `rsi`, `rdx` sont directement les entiers C# sans aucune conversion.

### Tableaux

Un `byte[]` C# est un objet managé avec un header d'objet, un champ de longueur, puis les données. Le CLR ne passe pas l'objet entier au code natif — il passe un pointeur vers la zone de données (en sautant le header et la longueur). Le tableau est épinglé en mémoire pendant l'appel. C'est pourquoi, dans notre hook sur `compute_native_hash`, `args[0]` pointe directement vers les octets du username, pas vers l'objet `byte[]` complet.

### Chaînes de caractères

Le marshalling des chaînes dépend de l'attribut `[MarshalAs]`. Dans notre `NativeBridge`, la méthode `VerifyIntegrity` déclare son paramètre `username` avec `[MarshalAs(UnmanagedType.LPStr)]`, ce qui indique une chaîne C null-terminée en ANSI (8 bits). Le CLR alloue un buffer temporaire, copie la chaîne C# (qui est en UTF-16 en interne) en ANSI, ajoute le terminateur `\0`, et passe un pointeur vers ce buffer. Côté Frida, on lit cette chaîne avec `Memory.readUtf8String(args[0])`.

Sans `[MarshalAs]`, le marshalling par défaut dépend de la plateforme : `LPStr` (ANSI) sous Windows avec .NET Framework, `LPUTF8Str` (UTF-8) avec .NET Core/5+. C'est un piège classique en RE : la même déclaration P/Invoke peut produire un marshalling différent selon le runtime.

### Structures

Pour les types `struct` passés par valeur, le CLR reproduit la disposition mémoire en respectant les alignements spécifiés par `[StructLayout]`. Les structures blittables sont passées directement ; les autres sont marshalées dans un buffer temporaire. Notre `LicenseChecker` n'utilise pas de structures P/Invoke, mais c'est un pattern fréquent dans les applications réelles (structures Win32 comme `RECT`, `POINT`, `SECURITY_ATTRIBUTES`, etc.).

## Cas courants en RE : ce qu'on trouve derrière les P/Invoke

Au-delà de notre exercice pédagogique, les appels P/Invoke dans les applications réelles pointent généralement vers un nombre limité de catégories de bibliothèques. Les reconnaître accélère l'analyse.

**Vérifications de licence déléguées.** C'est notre cas de figure. Le développeur place la logique sensible dans une bibliothèque native pour la rendre plus difficile à décompiler (pas de métadonnées .NET). En pratique, cette stratégie offre peu de protection supplémentaire : la bibliothèque native reste analysable avec les outils des parties II à V, et le pont P/Invoke crée un point d'interception commode.

**Bibliothèques cryptographiques.** Des appels vers OpenSSL (`libssl.so`), libsodium, ou une bibliothèque crypto custom. Les arguments sont des buffers (clés, plaintexts, ciphertexts) et des tailles. L'interception permet d'extraire les clés en transit — la même technique que celle du chapitre 24.

**API système non exposées.** Des appels directs vers `libc` (`open`, `read`, `write`, `mmap`, `ptrace`) ou vers des API spécifiques au noyau. Souvent liés à des mécanismes anti-debug ou à des vérifications d'intégrité.

**Code legacy.** Des bibliothèques C/C++ anciennes enveloppées dans une interface .NET. Le P/Invoke est le mécanisme d'interopérabilité, pas une mesure de protection. L'interception est triviale.

## Résumé du workflow d'interception P/Invoke

Pour synthétiser la démarche complète face à un appel P/Invoke inconnu :

On commence par **identifier** les déclarations `[DllImport]` dans l'assembly .NET (dnSpy, `monodis`, ILSpy). On note le nom de la bibliothèque, le point d'entrée, la convention d'appel, et les attributs de marshalling.

On **inspecte** ensuite la bibliothèque native avec les outils classiques (`file`, `nm`, `strings`, `objdump`, Ghidra). On localise les fonctions cibles, on comprend leur signature C réelle, et on analyse leur logique si nécessaire.

On **intercepte** les appels — côté managé avec Frida CLR (section 32.2), côté natif avec Frida `Interceptor` ou GDB. L'interception côté natif est plus fiable et donne accès aux arguments après marshalling (c'est-à-dire tels que le code C les reçoit).

On **corrèle** les deux côtés. Les arguments observés côté natif doivent correspondre aux valeurs passées côté managé (après marshalling). Si une divergence apparaît, c'est un indice que le marshalling fait quelque chose d'inattendu — un `[MarshalAs]` custom, une transformation de chaîne, une structure marshalée différemment de ce qu'on attendait.

On **exploite** enfin les résultats : capture de valeurs sensibles (clés, hash, tokens), bypass de vérifications (en modifiant la valeur de retour), ou remplacement complet de la fonction native (en redirigeant l'appel vers notre propre implémentation avec `Interceptor.replace` ou `LD_PRELOAD`, comme vu au chapitre 22).

---


⏭️ [Patcher un assembly .NET à la volée (modifier l'IL avec dnSpy)](/32-analyse-dynamique-dotnet/04-patcher-il-dnspy.md)
