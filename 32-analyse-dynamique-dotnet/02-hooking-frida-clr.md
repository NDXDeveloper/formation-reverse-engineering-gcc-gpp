🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 32.2 — Hooking de méthodes .NET avec Frida (`frida-clr`)

> 📁 **Fichiers utilisés** : `binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64/LicenseChecker.dll`  
> 🔧 **Outils** : Frida, frida-tools, frida-clr  
> 📖 **Prérequis** : [Chapitre 13 — Instrumentation dynamique avec Frida](/13-frida/README.md), [Section 32.1](/32-analyse-dynamique-dotnet/01-debug-dnspy-sans-sources.md)

---

## De Frida natif à Frida CLR

Au chapitre 13, vous avez appris à utiliser Frida pour instrumenter des binaires natifs : hooker des fonctions C/C++ par leur adresse ou leur symbole, intercepter `malloc`, `free`, `open`, modifier des arguments et des valeurs de retour à la volée. Le moteur est le même ici — un agent JavaScript injecté dans le processus cible — mais la surface d'intervention change radicalement.

Quand on instrumente un binaire natif, on travaille au niveau des adresses mémoire et des conventions d'appel machine. Pour hooker une fonction, on a besoin de son adresse (obtenue via `Module.findExportByName` ou calculée à partir d'un offset dans Ghidra). Les arguments sont des registres ou des emplacements sur la pile, qu'on lit et écrit avec `args[0]`, `args[1]`, etc. C'est bas niveau, direct, et parfois fragile.

Avec un processus .NET, on fait face à une couche supplémentaire : le runtime CLR. Le code C# est compilé en bytecode CIL, qui est ensuite traduit en code natif par le compilateur JIT au moment de l'exécution. Les méthodes .NET ne sont pas de simples fonctions à des adresses fixes — elles sont des objets managés que le runtime connaît par leur token de métadonnées, leur classe, leur signature. Frida propose un module dédié, `CLR` (aussi appelé `frida-clr`), qui expose ces objets managés à l'agent JavaScript et permet de les hooker à un niveau sémantique : par nom de classe et nom de méthode, pas par adresse brute.

## Architecture de l'instrumentation CLR

Pour comprendre ce que fait `frida-clr`, il faut visualiser les couches en jeu quand on attache Frida à un processus .NET :

```
┌───────────────────────────────────────────────┐
│  Agent JavaScript Frida (injecté)             │
│                                               │
│   CLR bridge : accès aux types, méthodes,     │
│   champs du monde managé via les API du       │
│   runtime (ICorProfiler / metadata API)       │
│                                               │
├───────────────────────────────────────────────┤
│  Runtime CLR / CoreCLR                        │
│   ┌─────────────┐  ┌──────────────────────┐  │
│   │ JIT Compiler │  │ Garbage Collector    │  │
│   └──────┬──────┘  └──────────────────────┘  │
│          │                                    │
│          ▼                                    │
│   Code natif généré (en mémoire)              │
├───────────────────────────────────────────────┤
│  Code natif P/Invoke (libnative_check.so)     │
│  → hookable via Interceptor classique         │
└───────────────────────────────────────────────┘
```

L'agent Frida opère en deux modes simultanés. Il peut interagir avec le monde managé via le bridge CLR — c'est ce qui nous intéresse dans cette section. Et il peut toujours interagir avec le code natif via `Interceptor` et `NativeFunction`, exactement comme au chapitre 13 — c'est ce qui sera exploité à la section 32.3 pour les appels P/Invoke.

Le bridge CLR fonctionne en s'appuyant sur les API de profilage et de métadonnées du runtime .NET. Il énumère les assemblies chargés, parcourt leurs tables de types, résout les méthodes, et peut installer des hooks en manipulant les stubs JIT ou en utilisant les mécanismes de profilage. Le résultat : depuis JavaScript, on peut écrire `CLR.classes['LicenseChecker.LicenseValidator'].methods['Validate']` et obtenir un handle sur la méthode managée.

## Installation et vérification

Frida et ses bindings Python s'installent comme au chapitre 13 :

```bash
pip install frida-tools frida
```

Pour vérifier que le support CLR est disponible, on peut lancer un processus .NET simple et tenter d'y attacher Frida :

```bash
# Lancer l'application en arrière-plan
cd binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64  
LD_LIBRARY_PATH=. ./LicenseChecker &  
APP_PID=$!  

# Tester l'attachement
frida -p $APP_PID -l /dev/null --runtime=clr
```

Si l'attachement réussit et que la console Frida s'ouvre, le support CLR est opérationnel. Le flag `--runtime=clr` indique à Frida d'activer le bridge CLR plutôt que de travailler uniquement au niveau natif.

> ⚠️ **Note sur la compatibilité** : le support CLR de Frida est plus mature sur Windows (.NET Framework / .NET Core) que sur Linux (CoreCLR). Sous Linux, certaines fonctionnalités peuvent être limitées selon la version de Frida et du runtime .NET. Si le bridge CLR n'est pas disponible, une approche alternative est présentée en fin de section : hooker les méthodes après compilation JIT, au niveau natif.

## Enumérer les classes et méthodes

La première étape, avant tout hooking, est d'explorer le paysage managé du processus cible. Le bridge CLR permet de lister les assemblies chargés, puis de parcourir leurs types et méthodes.

```javascript
// enum_assemblies.js
// Lister les assemblies et leurs types principaux

"use strict";

function enumManagedTypes() {
    const dominated = CLR.assemblies;

    for (const name in dominated) {
        const assembly = dominated[name];
        console.log(`\n[Assembly] ${name}`);

        const types = assembly.classes;
        for (const typeName in types) {
            console.log(`  [Type] ${typeName}`);
            const klass = types[typeName];

            // Lister les méthodes
            const methods = klass.methods;
            for (const methodName in methods) {
                console.log(`    [Method] ${methodName}`);
            }
        }
    }
}

setTimeout(enumManagedTypes, 500);
```

Sur notre `LicenseChecker`, ce script révèle la structure que dnSpy nous montrait en statique, mais cette fois depuis l'intérieur du processus en cours d'exécution :

```
[Assembly] LicenseChecker
  [Type] LicenseChecker.Program
    [Method] Main
  [Type] LicenseChecker.LicenseValidator
    [Method] Validate
    [Method] ValidateStructure
    [Method] ComputeUserHash
    [Method] CheckSegmentB
    [Method] ComputeCrossXor
    [Method] ComputeFinalChecksum
    [Method] DeriveLicenseLevel
  [Type] LicenseChecker.ValidationResult
    [Method] get_IsValid
    [Method] set_IsValid
    [Method] get_FailureReason
    [Method] set_FailureReason
    ...
  [Type] LicenseChecker.NativeBridge
    [Method] ComputeNativeHash
    [Method] ComputeChecksum
    [Method] VerifyIntegrity
```

Cette énumération confirme que le bridge CLR a accès à l'ensemble des métadonnées managées. On peut maintenant cibler précisément les méthodes à hooker.

## Hooker une méthode managée

Le hooking CLR s'effectue en obtenant une référence à la méthode cible, puis en attachant un callback qui sera invoqué avant et/ou après chaque appel. La syntaxe diffère de `Interceptor.attach` (qui travaille sur des adresses natives) mais le principe est identique.

### Capturer les arguments de `Validate()`

Notre première cible est la méthode `Validate(string username, string licenseKey)` de la classe `LicenseValidator`. On veut intercepter chaque appel pour logger les arguments — le username et la clé fournis par l'utilisateur.

```javascript
// hook_validate.js
// Intercepter LicenseValidator.Validate() pour logger les entrées

"use strict";

function hookValidate() {
    const LicenseValidator = CLR.classes[
        "LicenseChecker.LicenseValidator"
    ];

    if (!LicenseValidator) {
        console.log("[-] Classe LicenseValidator introuvable.");
        console.log("    L'assembly est-il chargé ?");
        return;
    }

    const validate = LicenseValidator.methods["Validate"];

    validate.implementation = function (username, licenseKey) {
        console.log("\n══════════════════════════════════════");
        console.log("[+] LicenseValidator.Validate() appelé");
        console.log(`    username   = "${username}"`);
        console.log(`    licenseKey = "${licenseKey}"`);
        console.log("══════════════════════════════════════");

        // Appeler la méthode originale
        const result = this.Validate(username, licenseKey);

        // Logger le résultat
        console.log(`\n[+] Résultat :`);
        console.log(`    IsValid       = ${result.IsValid}`);
        console.log(`    FailureReason = "${result.FailureReason}"`);
        console.log(`    LicenseLevel  = "${result.LicenseLevel}"`);

        return result;
    };

    console.log("[+] Hook installé sur LicenseValidator.Validate()");
}

setTimeout(hookValidate, 500);
```

Le pattern `validate.implementation = function(...) { ... }` remplace l'implémentation de la méthode par notre callback. À l'intérieur, `this.Validate(username, licenseKey)` appelle l'implémentation originale — c'est l'équivalent managé du pattern `onEnter` / `onLeave` que vous connaissez de `Interceptor.attach`, mais combiné en un seul wrapper.

### Intercepter `ComputeUserHash()` pour extraire le segment A

Allons plus loin. On veut capturer la valeur de retour de `ComputeUserHash()` — c'est la valeur attendue pour le segment A.

```javascript
// hook_compute_hash.js
// Intercepter ComputeUserHash() pour capturer le segment A attendu

"use strict";

function hookComputeUserHash() {
    const klass = CLR.classes["LicenseChecker.LicenseValidator"];
    const method = klass.methods["ComputeUserHash"];

    method.implementation = function (username) {
        const result = this.ComputeUserHash(username);

        // Convertir en hex 4 caractères (format du segment)
        const hex = (result >>> 0).toString(16).toUpperCase().padStart(4, "0");

        console.log(`[+] ComputeUserHash("${username}") → 0x${hex}`);
        console.log(`    ↳ Segment A attendu : ${hex}`);

        return result;
    };

    console.log("[+] Hook installé sur ComputeUserHash()");
}

setTimeout(hookComputeUserHash, 500);
```

En lançant `LicenseChecker` avec ce script actif et en entrant `alice` comme username, on verra dans la console Frida le segment A calculé — sans avoir touché à un débogueur, sans avoir compris l'algorithme FNV-1a, et sans avoir modifié le binaire.

### Intercepter `ComputeCrossXor()` pour extraire le segment C

Le même pattern s'applique pour le segment C, en hookant `ComputeCrossXor()` :

```javascript
// hook_cross_xor.js
// Intercepter ComputeCrossXor() pour capturer le segment C attendu

"use strict";

function hookCrossXor() {
    const klass = CLR.classes["LicenseChecker.LicenseValidator"];
    const method = klass.methods["ComputeCrossXor"];

    method.implementation = function (segA, segB) {
        const result = this.ComputeCrossXor(segA, segB);

        const hexA = (segA >>> 0).toString(16).toUpperCase().padStart(4, "0");
        const hexB = (segB >>> 0).toString(16).toUpperCase().padStart(4, "0");
        const hexC = (result >>> 0).toString(16).toUpperCase().padStart(4, "0");

        console.log(`[+] ComputeCrossXor(0x${hexA}, 0x${hexB}) → 0x${hexC}`);
        console.log(`    ↳ Segment C attendu : ${hexC}`);

        return result;
    };

    console.log("[+] Hook installé sur ComputeCrossXor()");
}

setTimeout(hookCrossXor, 500);
```

## Modifier des valeurs de retour

Le hooking ne se limite pas à l'observation. On peut modifier les valeurs de retour pour altérer le comportement du programme sans toucher au binaire sur disque. C'est l'équivalent Frida de la modification de variables dans dnSpy — mais automatisable et reproductible.

### Forcer la validation à réussir

L'approche la plus brutale consiste à remplacer l'implémentation de `Validate()` par une version qui retourne toujours un résultat positif :

```javascript
// bypass_validate.js
// Forcer Validate() à toujours retourner IsValid = true

"use strict";

function bypassValidation() {
    const LicenseValidator = CLR.classes[
        "LicenseChecker.LicenseValidator"
    ];
    const ValidationResult = CLR.classes[
        "LicenseChecker.ValidationResult"
    ];

    LicenseValidator.methods["Validate"].implementation =
        function (username, licenseKey) {
            console.log(`[+] Validate() intercepté — bypass activé`);
            console.log(`    username = "${username}"`);

            // Créer un ValidationResult forgé
            const fakeResult = ValidationResult.$new();
            fakeResult.IsValid = true;
            fakeResult.FailureReason = "";
            fakeResult.LicenseLevel = "Enterprise";
            fakeResult.ExpirationInfo = "Perpétuelle (forgée par Frida)";

            return fakeResult;
        };

    console.log("[+] Bypass installé — toute licence sera acceptée");
}

setTimeout(bypassValidation, 500);
```

Ce script crée une instance de `ValidationResult` via `$new()` (l'équivalent Frida de `new ValidationResult()`), assigne les propriétés souhaitées, et retourne cet objet forgé à la place du résultat réel. L'application affichera « Licence valide ! Bienvenue. » quelle que soit la clé entrée.

> 💡 Le `$new()` est spécifique au bridge CLR de Frida. Il permet d'instancier des objets .NET depuis JavaScript. Les propriétés sont accessibles directement par leur nom, grâce aux métadonnées qui fournissent à Frida la correspondance entre les noms C# et les offsets mémoire.

### Approche chirurgicale : modifier un seul check

Plutôt que de bypasser toute la validation, on peut cibler un seul check. Par exemple, si on a déjà les segments A, B et C corrects mais que le segment D pose problème, on peut hooker uniquement `ComputeFinalChecksum` pour qu'il retourne la valeur qu'on a entrée :

```javascript
// hook_checksum.js
// Forcer ComputeFinalChecksum à retourner la valeur du segment D fourni

"use strict";

function hookFinalChecksum() {
    const klass = CLR.classes["LicenseChecker.LicenseValidator"];

    // On a besoin d'accéder au segment D entré par l'utilisateur.
    // Stratégie : hooker Validate() pour capturer licenseKey,
    // puis hooker ComputeFinalChecksum pour retourner la valeur
    // du segment D tel que saisi.

    let capturedSegD = null;

    klass.methods["Validate"].implementation =
        function (username, licenseKey) {
            // Extraire le segment D de la clé saisie
            const parts = licenseKey.trim().toUpperCase().split("-");
            if (parts.length === 4) {
                capturedSegD = parseInt(parts[3], 16);
                console.log(`[+] Segment D saisi : 0x${parts[3]}`);
            }
            return this.Validate(username, licenseKey);
        };

    klass.methods["ComputeFinalChecksum"].implementation =
        function (segA, segB, segC, username) {
            const realValue = this.ComputeFinalChecksum(
                segA, segB, segC, username
            );
            const realHex = (realValue >>> 0)
                .toString(16).toUpperCase().padStart(4, "0");

            console.log(`[+] ComputeFinalChecksum réel = 0x${realHex}`);

            if (capturedSegD !== null) {
                console.log(`[+] → Remplacé par 0x${
                    capturedSegD.toString(16).toUpperCase().padStart(4, "0")
                } (segment D saisi)`);
                return capturedSegD;
            }
            return realValue;
        };

    console.log("[+] Hooks installés (Validate + ComputeFinalChecksum)");
}

setTimeout(hookFinalChecksum, 500);
```

Cette approche est plus subtile : le programme exécute toute sa logique normalement, sauf au moment précis du checksum final où on substitue la valeur attendue par celle que l'utilisateur a saisie. C'est l'équivalent d'un patch ciblé, mais sans modification du binaire.

## Script combiné : keygen automatique par hooking

En combinant plusieurs hooks, on peut construire un script qui laisse l'application calculer elle-même tous les segments, les capture, et affiche la clé complète valide. L'idée est de lancer l'application avec un username donné et une clé bidon, puis de lire les valeurs attendues.

Le point subtil est le suivant : la méthode `Validate()` originale est séquentielle avec retour anticipé — si le segment A est incorrect, elle retourne immédiatement sans jamais appeler `ComputeCrossXor` ni `ComputeFinalChecksum`. Avec une clé bidon, seul `ComputeUserHash` serait atteint. Pour capturer les quatre segments en une seule passe, notre hook sur `Validate()` doit **appeler directement** les méthodes de calcul dans l'ordre, en leur passant les bonnes valeurs, au lieu de déléguer à l'implémentation originale.

```javascript
// keygen_frida.js
// Keygen automatique par interception des calculs internes
//
// Usage :
//   frida -f ./LicenseChecker --runtime=clr -l keygen_frida.js
//   (entrer un username quelconque et une clé bidon AAAA-AAAA-AAAA-AAAA)

"use strict";

function installKeygen() {
    const klass = CLR.classes["LicenseChecker.LicenseValidator"];
    const segments = { A: null, B: null, C: null, D: null };

    const fmt = (v) => v !== null
        ? (v >>> 0).toString(16).toUpperCase().padStart(4, "0")
        : "????";

    // ── Hooks de logging sur les sous-méthodes (optionnels) ──
    // Ces hooks ne font que logger — la capture est pilotée
    // depuis le hook Validate ci-dessous.

    klass.methods["ComputeUserHash"].implementation =
        function (username) {
            const val = this.ComputeUserHash(username);
            console.log(`  [CLR] ComputeUserHash("${username}") → 0x${fmt(val & 0xFFFF)}`);
            return val;
        };

    klass.methods["ComputeCrossXor"].implementation =
        function (segA, segB) {
            const val = this.ComputeCrossXor(segA, segB);
            console.log(`  [CLR] ComputeCrossXor(0x${fmt(segA)}, 0x${fmt(segB)}) → 0x${fmt(val & 0xFFFF)}`);
            return val;
        };

    klass.methods["ComputeFinalChecksum"].implementation =
        function (segA, segB, segC, username) {
            const val = this.ComputeFinalChecksum(segA, segB, segC, username);
            console.log(`  [CLR] ComputeFinalChecksum → 0x${fmt(val & 0xFFFF)}`);
            return val;
        };

    // ── Hook principal : Validate ──
    // Au lieu de laisser l'original s'exécuter (il retournerait
    // dès le premier check raté), on appelle chaque méthode de
    // calcul directement avec les bonnes valeurs.

    klass.methods["Validate"].implementation =
        function (username, licenseKey) {
            console.log(`\n[+] Validate("${username}", "${licenseKey}")`);
            segments.A = segments.B = segments.C = segments.D = null;

            // Segment A — appel direct
            segments.A = this.ComputeUserHash(username) & 0xFFFF;

            // Segment B — déclencher CheckSegmentB pour provoquer
            // l'appel P/Invoke à compute_native_hash. Si un hook natif
            // est installé (§32.3), il capturera seg.B de manière
            // synchrone pendant cet appel. Sans hook natif, seg.B
            // restera null — CheckSegmentB retourne un bool, pas le hash.
            // On passe 0 comme segmentB bidon — peu importe, on veut
            // juste que compute_native_hash soit appelé côté natif.
            try {
                // CheckSegmentB appelle NativeBridge.ComputeNativeHash
                // en interne et compare avec segmentB. On ne peut pas
                // lire 'expected' directement depuis CLR — il faut le
                // hooker côté natif (§32.3) pour capturer seg.B.
                this.CheckSegmentB(username, 0);
            } catch (e) {
                console.log(`  [!] CheckSegmentB exception : ${e}`);
            }

            // Segment C — appel avec les vrais A et B
            // (seg.B est capturé par le hook natif si installé)
            if (segments.B !== null) {
                segments.C = this.ComputeCrossXor(
                    segments.A, segments.B) & 0xFFFF;
            }

            // Segment D — appel avec les vrais A, B, C
            if (segments.B !== null && segments.C !== null) {
                segments.D = this.ComputeFinalChecksum(
                    segments.A, segments.B, segments.C, username) & 0xFFFF;
            }

            // Affichage
            console.log("\n╔══════════════════════════════════════════╗");
            console.log("║         KEYGEN FRIDA — RÉSULTATS         ║");
            console.log("╠══════════════════════════════════════════╣");
            console.log(`║  Username  : ${username.padEnd(25)}║`);
            console.log(`║  Segment A : ${fmt(segments.A).padEnd(25)}║`);
            console.log(`║  Segment B : ${fmt(segments.B).padEnd(25)}║`);
            console.log(`║  Segment C : ${fmt(segments.C).padEnd(25)}║`);
            console.log(`║  Segment D : ${fmt(segments.D).padEnd(25)}║`);
            console.log("║                                          ║");

            if (segments.A !== null && segments.B !== null &&
                segments.C !== null && segments.D !== null) {
                const key = `${fmt(segments.A)}-${fmt(segments.B)}`
                          + `-${fmt(segments.C)}-${fmt(segments.D)}`;
                console.log(`║  CLÉ VALIDE : ${key.padEnd(23)}║`);
            } else {
                console.log("║  ⚠ Segment B manquant — installer le   ║");
                console.log("║    hook natif (voir §32.3)              ║");
            }

            console.log("╚══════════════════════════════════════════╝\n");

            // Appeler l'original pour que le programme affiche son message.
            // (Il échouera au segment A, mais on a déjà nos valeurs.)
            return this.Validate(username, licenseKey);
        };

    console.log("[+] Keygen Frida installé — lancez la validation.");
}

setTimeout(installKeygen, 1000);
```

Ce script illustre un pattern fondamental en RE dynamique : on ne reverse pas l'algorithme, on appelle les fonctions de calcul directement et on capture leurs résultats. Le hook sur `Validate()` court-circuite le flux linéaire de l'original (qui retournerait dès le premier check raté) en appelant chaque sous-méthode avec les bonnes valeurs.

On notera que le segment B ne peut pas être capturé par le bridge CLR seul : il est calculé par la bibliothèque native `libnative_check.so`, et la valeur intermédiaire `expected` dans `CheckSegmentB` n'est pas directement accessible. Pour compléter le keygen, il faut ajouter un hook natif sur `compute_native_hash` (section 32.3). C'est précisément la difficulté ajoutée par l'architecture P/Invoke de notre `LicenseChecker`.

## Approche alternative : hooking post-JIT au niveau natif

Si le bridge CLR n'est pas disponible ou fonctionne mal sur votre plateforme, il existe une approche de repli qui utilise les capacités d'interception native de Frida sur le code généré par le JIT.

Le principe est le suivant. Quand le CLR exécute une méthode .NET pour la première fois, le compilateur JIT la traduit en code machine natif et stocke le résultat en mémoire. Cette version native de la méthode a une adresse fixe (tant que l'application tourne). Si on peut trouver cette adresse, on peut hooker la méthode avec `Interceptor.attach` — exactement comme une fonction C native.

Pour trouver l'adresse JIT-compilée d'une méthode, plusieurs techniques existent.

**Utiliser les symboles du runtime.** CoreCLR exporte des fonctions internes pour résoudre les méthodes. On peut appeler `clr_jit_compileMethod` ou inspecter les structures internes du runtime. C'est fragile et dépend de la version de CoreCLR.

**Forcer la compilation JIT puis scanner la mémoire.** On déclenche un appel à la méthode cible (en envoyant une entrée quelconque), puis on cherche en mémoire le prologue caractéristique de la méthode compilée. Les prologues natifs générés par le JIT suivent des patterns reconnaissables (`push rbp; mov rbp, rsp` ou `sub rsp, N`).

**Utiliser `frida-trace` avec des patterns.** On peut tracer les appels aux fonctions du JIT pour identifier les adresses de compilation.

```javascript
// hook_post_jit.js
// Hooking natif d'une méthode .NET après compilation JIT
//
// Cette approche fonctionne quand le bridge CLR n'est pas disponible.
// Elle nécessite de connaître l'adresse de la méthode JIT-compilée.

"use strict";

function hookPostJIT() {
    // Étape 1 : trouver le module principal
    const mainModule = Process.enumerateModules()
        .find(m => m.name.includes("coreclr") ||
                    m.name.includes("libcoreclr"));

    if (!mainModule) {
        console.log("[-] CoreCLR non trouvé dans le processus.");
        return;
    }
    console.log(`[+] CoreCLR trouvé : ${mainModule.name} @ ${mainModule.base}`);

    // Étape 2 : scanner la mémoire pour trouver la méthode cible.
    // En pratique, on utilise des signatures ou on inspecte les
    // structures internes du runtime. Ici, on suppose que l'adresse
    // a été identifiée au préalable (par ex. via un breakpoint dans
    // dnSpy, qui affiche l'adresse native dans la fenêtre Disassembly).

    const targetAddr = ptr("0x<ADRESSE_JIT_COMPILÉE>");

    Interceptor.attach(targetAddr, {
        onEnter: function (args) {
            // En convention d'appel .NET (CoreCLR, x86-64) :
            // - args[0] = pointeur 'this' (l'instance LicenseValidator)
            // - args[1] = premier paramètre (string username)
            // - args[2] = deuxième paramètre (string licenseKey)
            //
            // Les strings .NET en mémoire sont des objets avec un header,
            // un champ Length, et les caractères en UTF-16.

            console.log("[+] Validate() appelé (hook natif post-JIT)");
        },
        onLeave: function (retval) {
            console.log(`[+] Validate() retourne : ${retval}`);
        }
    });

    console.log("[+] Hook natif installé sur la méthode JIT-compilée");
}

setTimeout(hookPostJIT, 500);
```

Cette approche est plus complexe et moins élégante que le bridge CLR — on retombe dans le monde du hooking natif vu au chapitre 13, avec la difficulté supplémentaire de devoir comprendre la représentation mémoire des objets .NET (headers d'objet, encodage des strings en UTF-16LE, pointeurs managés). Mais elle a un avantage : elle fonctionne toujours, quelle que soit la version de Frida ou du runtime, parce qu'elle opère au niveau de l'abstraction la plus basse — le code machine.

## Considérations pratiques

### Timing de l'injection

En mode `spawn` (Frida lance le processus), l'agent est injecté très tôt, avant que le runtime CLR ne soit entièrement initialisé. Le bridge CLR peut ne pas être prêt immédiatement. C'est pourquoi les scripts ci-dessus utilisent `setTimeout` pour retarder l'installation des hooks. En pratique, il peut être nécessaire d'ajuster ce délai, ou d'utiliser un mécanisme plus robuste : surveiller le chargement de l'assembly cible et installer les hooks dès qu'il apparaît.

```javascript
// Attendre que l'assembly LicenseChecker soit chargé
function waitForAssembly(name, callback) {
    const interval = setInterval(() => {
        if (CLR.assemblies && CLR.assemblies[name]) {
            clearInterval(interval);
            callback();
        }
    }, 100);
}

waitForAssembly("LicenseChecker", () => {
    console.log("[+] Assembly LicenseChecker détecté — installation des hooks");
    installKeygen();
});
```

En mode `attach` (Frida s'attache à un processus déjà en cours), l'assembly est généralement déjà chargé et le bridge CLR est immédiatement fonctionnel. C'est le mode le plus fiable pour le hooking CLR.

### Gestion des surcharges de méthodes

En C#, une classe peut avoir plusieurs méthodes portant le même nom mais avec des signatures différentes (surcharges). Le bridge CLR les distingue par leur signature. Si `methods["Validate"]` est ambigu, on peut utiliser la notation avec signature complète :

```javascript
// Spécifier la surcharge par les types de paramètres
const validate = klass.methods[
    "Validate(System.String, System.String)"
];
```

### Impact du Garbage Collector

Les objets .NET peuvent être déplacés en mémoire par le GC. Cela signifie qu'un pointeur brut vers un objet managé peut devenir invalide à tout moment. Le bridge CLR gère cela de manière transparente en utilisant des handles GC (GCHandle). Mais si on tente de manipuler des objets managés via les API natives de Frida (`Memory.read*`), on s'expose à des pointeurs périmés. La règle est simple : pour interagir avec des objets managés, utiliser le bridge CLR ; pour interagir avec du code natif, utiliser `Interceptor` et `NativeFunction`.

### Assemblies chargés dynamiquement

Si l'application cible charge des assemblies à l'exécution (via `Assembly.Load`), ceux-ci n'apparaissent dans `CLR.assemblies` qu'après leur chargement. Pour hooker des méthodes dans un assembly chargé dynamiquement, il faut surveiller le chargement (par exemple en hookant `Assembly.Load` lui-même) et installer les hooks à ce moment-là.

Cette technique est particulièrement pertinente face à des obfuscateurs qui déchiffrent un assembly en mémoire et le chargent dynamiquement : l'assembly déchiffré n'existe jamais sur disque, mais il apparaît dans l'espace managé dès son chargement, et devient immédiatement hookable.

## Comparaison avec Frida natif (chapitre 13)

| Aspect | Frida natif (ch. 13) | Frida CLR (cette section) |  
|---|---|---|  
| Cible | Fonctions C/C++ dans un ELF/PE | Méthodes C# dans un assembly .NET |  
| Identification | Adresse ou symbole exporté | Nom de classe + nom de méthode |  
| Arguments | Registres / pile (bas niveau) | Objets C# typés (haut niveau) |  
| Valeur de retour | Entier / pointeur brut | Objet C# (y compris types complexes) |  
| Création d'objets | `Memory.alloc` + écriture manuelle | `ClassName.$new()` |  
| Strings | `Memory.readUtf8String(ptr)` | Accès direct (propriété JavaScript) |  
| GC | N/A | Les handles sont gérés automatiquement |  
| Compatibilité | Universelle | Dépend du support CLR de Frida |

Le bridge CLR élève le niveau d'abstraction : on travaille avec des concepts C# (classes, méthodes, propriétés, instances) plutôt qu'avec des adresses et des registres. C'est plus confortable, mais c'est aussi une couche supplémentaire qui peut introduire des incompatibilités. L'approche post-JIT native reste le filet de sécurité universel.

---


⏭️ [Intercepter des appels P/Invoke (pont .NET → bibliothèques natives GCC)](/32-analyse-dynamique-dotnet/03-pinvoke-interception.md)
