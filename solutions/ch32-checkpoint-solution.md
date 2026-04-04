🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 32

> ⚠️ **Spoilers** — Ne consultez cette page qu'après avoir tenté le checkpoint par vous-même.

---

## Livrable 1 — Script Frida de capture (`capture.js`)

```javascript
// capture.js — Script Frida combiné CLR + natif
//
// Usage (spawn) :
//   frida -f ./LicenseChecker --runtime=clr -l capture.js
//
// Usage (attach) :
//   frida -p <PID> --runtime=clr -l capture.js
//
// Entrez un username quelconque et une clé bidon (ex: 0000-0000-0000-0000).
// Le script affiche la clé valide calculée par l'application elle-même.

"use strict";

const seg = { A: null, B: null, C: null, D: null };  
const hex4 = (v) =>  
    v !== null
        ? (v >>> 0).toString(16).toUpperCase().padStart(4, "0")
        : "????";

// ═══════════════════════════════════════════════════════════
//  HOOKS NATIFS — libnative_check.so
// ═══════════════════════════════════════════════════════════

function installNativeHooks() {
    const mod = Process.findModuleByName("libnative_check.so");
    if (!mod) return false;

    // ── compute_native_hash → segment B ──
    const hashAddr = Module.findExportByName(
        "libnative_check.so", "compute_native_hash"
    );
    if (hashAddr) {
        Interceptor.attach(hashAddr, {
            onEnter(args) {
                this.len = args[1].toInt32();
                this.data = Memory.readUtf8String(args[0], this.len);
                console.log(`  [natif] compute_native_hash("${this.data}", ${this.len})`);
            },
            onLeave(retval) {
                seg.B = retval.toUInt32() & 0xFFFF;
                console.log(`  [natif]   → 0x${hex4(seg.B)}  (segment B)`);
            }
        });
    }

    // ── compute_checksum → partie native du segment D ──
    const chkAddr = Module.findExportByName(
        "libnative_check.so", "compute_checksum"
    );
    if (chkAddr) {
        Interceptor.attach(chkAddr, {
            onEnter(args) {
                const a = args[0].toUInt32() & 0xFFFF;
                const b = args[1].toUInt32() & 0xFFFF;
                const c = args[2].toUInt32() & 0xFFFF;
                console.log(`  [natif] compute_checksum(` +
                    `0x${hex4(a)}, 0x${hex4(b)}, 0x${hex4(c)})`);
            },
            onLeave(retval) {
                const v = retval.toUInt32() & 0xFFFF;
                console.log(`  [natif]   → 0x${hex4(v)}  (checksum natif)`);
            }
        });
    }

    console.log("[+] Hooks natifs installés sur libnative_check.so");
    return true;
}

// ═══════════════════════════════════════════════════════════
//  HOOKS CLR — méthodes managées
// ═══════════════════════════════════════════════════════════

function installCLRHooks() {
    const klass = CLR.classes["LicenseChecker.LicenseValidator"];
    if (!klass) return false;

    // ── Hook principal : Validate ──
    //
    // Point crucial : la méthode Validate() originale est
    // séquentielle avec retour anticipé. Si le segment A
    // est incorrect, elle retourne immédiatement sans jamais
    // appeler CheckSegmentB, ComputeCrossXor, etc.
    // Avec une clé bidon, seul ComputeUserHash serait atteint.
    //
    // Solution : notre hook appelle directement chaque méthode
    // de calcul dans le bon ordre, en leur passant les bonnes
    // valeurs, au lieu de déléguer à l'implémentation originale.
    // Les hooks natifs se déclenchent de manière synchrone
    // pendant les appels P/Invoke (CheckSegmentB et
    // ComputeFinalChecksum), capturant seg.B côté natif.

    klass.methods["Validate"].implementation = function (username, licenseKey) {
        console.log("\n┌──────────────────────────────────────────────┐");
        console.log(`│  Validate("${username}", "${licenseKey}")`);
        console.log("├──────────────────────────────────────────────┤");

        // Réinitialiser les captures
        seg.A = seg.B = seg.C = seg.D = null;

        // ── Segment A : appel direct à ComputeUserHash ──
        seg.A = this.ComputeUserHash(username) & 0xFFFF;
        console.log(`│  [CLR] Segment A = 0x${hex4(seg.A)}`);

        // ── Segment B : déclencher CheckSegmentB pour provoquer
        //    l'appel P/Invoke. Le hook natif capture seg.B. ──
        try {
            this.CheckSegmentB(username, 0);
        } catch (e) {
            console.log(`│  [!] CheckSegmentB exception : ${e}`);
        }
        if (seg.B !== null) {
            console.log(`│  [natif] Segment B = 0x${hex4(seg.B)}`);
        } else {
            console.log("│  [!] Segment B non capturé (lib native absente ?)");
        }

        // ── Segment C : appel avec les vrais A et B ──
        if (seg.B !== null) {
            seg.C = this.ComputeCrossXor(seg.A, seg.B) & 0xFFFF;
            console.log(`│  [CLR] Segment C = 0x${hex4(seg.C)}`);
        }

        // ── Segment D : appel avec les vrais A, B, C ──
        if (seg.B !== null && seg.C !== null) {
            seg.D = this.ComputeFinalChecksum(
                seg.A, seg.B, seg.C, username) & 0xFFFF;
            console.log(`│  [CLR] Segment D = 0x${hex4(seg.D)}`);
        }

        console.log("├──────────────────────────────────────────────┤");

        if (seg.A !== null && seg.B !== null &&
            seg.C !== null && seg.D !== null) {
            const key = `${hex4(seg.A)}-${hex4(seg.B)}-${hex4(seg.C)}-${hex4(seg.D)}`;
            console.log("│");
            console.log(`│  ★ CLÉ VALIDE : ${key}`);
        } else {
            console.log("│");
            console.log("│  ⚠ Capture incomplète :");
            console.log(`│    A=${hex4(seg.A)} B=${hex4(seg.B)} C=${hex4(seg.C)} D=${hex4(seg.D)}`);
        }
        console.log("└──────────────────────────────────────────────┘\n");

        // Appeler l'original (il échouera au segment A, mais on a
        // déjà nos valeurs — le programme affichera son message).
        return this.Validate(username, licenseKey);
    };

    console.log("[+] Hooks CLR installés sur LicenseValidator");
    return true;
}

// ═══════════════════════════════════════════════════════════
//  ORCHESTRATION — attendre le chargement des deux côtés
// ═══════════════════════════════════════════════════════════

let nativeOk = false;  
let clrOk    = false;  

console.log("[*] Attente du chargement de l'assembly et de la bibliothèque native...");

const poll = setInterval(() => {
    if (!nativeOk) {
        nativeOk = installNativeHooks();
    }

    if (!clrOk) {
        try {
            if (CLR && CLR.assemblies && CLR.assemblies["LicenseChecker"]) {
                clrOk = installCLRHooks();
            }
        } catch (e) { /* CLR pas encore prêt */ }
    }

    if (clrOk) {
        // La lib native est chargée au premier P/Invoke — re-tenter
        if (!nativeOk) {
            nativeOk = installNativeHooks();
        }
    }

    if (clrOk && nativeOk) {
        clearInterval(poll);
        console.log("\n[+] Tous les hooks sont actifs. Entrez un username et une clé.\n");
    }
}, 100);

// Timeout : si après 10s les hooks natifs ne sont pas installés,
// continuer — ils seront installés au premier appel P/Invoke
setTimeout(() => {
    if (clrOk && !nativeOk) {
        console.log("[*] Hooks CLR actifs, hooks natifs en attente " +
                    "(la lib sera hookée au premier appel P/Invoke).");
        const nativePoll = setInterval(() => {
            if (installNativeHooks()) {
                clearInterval(nativePoll);
            }
        }, 20);
    }
}, 10000);
```

### Notes sur la solution

Le script gère trois difficultés spécifiques :

**Le retour anticipé de `Validate()`.** C'est le point crucial. L'implémentation originale de `Validate()` est séquentielle : si le segment A est incorrect, elle retourne immédiatement sans jamais appeler `CheckSegmentB`, `ComputeCrossXor` ni `ComputeFinalChecksum`. Avec une clé bidon comme `0000-0000-0000-0000`, seul `ComputeUserHash` serait atteint par l'original. La solution est de ne pas déléguer à l'original pour la phase de capture : le hook appelle directement chaque méthode de calcul dans le bon ordre, en leur passant les bonnes valeurs (le segment A fraîchement calculé, puis B capturé côté natif, etc.). L'original n'est appelé qu'à la fin, pour que le programme affiche son message normalement.

**Le chargement paresseux de la `.so`.** La bibliothèque native n'est chargée par le CLR qu'au premier appel P/Invoke. En mode `spawn`, le processus démarre sous contrôle de Frida avant que l'utilisateur n'ait soumis de clé — la bibliothèque n'est donc pas encore en mémoire. Le polling périodique dans l'orchestrateur détecte l'apparition du module et installe les hooks natifs dès qu'il est disponible. Le hook `Validate` déclenche lui-même le chargement en appelant `CheckSegmentB`, ce qui provoque l'appel P/Invoke et donc le `dlopen` de la bibliothèque.

**La synchronicité des hooks natifs.** Quand le hook CLR `Validate` appelle `this.CheckSegmentB(username, 0)`, cet appel descend jusqu'à `NativeBridge.ComputeNativeHash` via P/Invoke, qui exécute `compute_native_hash` dans `libnative_check.so`. Le hook natif `Interceptor.attach` sur `compute_native_hash` se déclenche **de manière synchrone** pendant cet appel — quand `CheckSegmentB` retourne, `seg.B` est déjà renseigné. C'est ce qui permet au script d'enchaîner immédiatement avec `ComputeCrossXor(seg.A, seg.B)` sans attente.

---

## Livrable 2 — Keygen Python (`keygen.py`)

```python
#!/usr/bin/env python3
"""
keygen.py — Générateur de clés pour LicenseChecker (Chapitre 32)

Schéma de validation :
  Segment A = fold16(FNV-1a(lowercase(username) || b"REV3RSE!"))
  Segment B = fold16(FNV-1a(lowercase(username) || b"NATIVERE"))
  Segment C = ((rotl16(A, 5) ^ B) * 0x9E37 & 0xFFFF) ^ 0xA5A5
  Segment D = ((A + B + C) & 0xFFFF) ^ native_checksum(A, B, C)

  native_checksum(A, B, C) :
      val = rotl16(A, 3) ^ B
      val = rotl16(val, 9) ^ C      # rotl16(x, 9) == rotr16(x, 7)
      val = (val * 0x5BD1) & 0xFFFF
      val ^= 0x1337

Usage :
  python3 keygen.py <username>
  python3 keygen.py --test          # valide sur plusieurs usernames
"""

import sys

# ── Constantes ────────────────────────────────────────────────────────

FNV_OFFSET = 0x811C9DC5  
FNV_PRIME  = 0x01000193  
MASK32     = 0xFFFFFFFF  
MASK16     = 0xFFFF  

SALT_MANAGED = b"REV3RSE!"   # Salt C# (segment A)  
SALT_NATIVE  = b"NATIVERE"   # Salt natif (segment B)  


# ── Primitives ────────────────────────────────────────────────────────

def fnv1a_32(data: bytes) -> int:
    """Hash FNV-1a 32 bits."""
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & MASK32
    return h


def fold16(h: int) -> int:
    """Repliement XOR 32→16 bits."""
    return ((h >> 16) ^ (h & MASK16)) & MASK16


def rotl16(value: int, shift: int) -> int:
    """Rotation à gauche sur 16 bits."""
    value &= MASK16
    return ((value << shift) | (value >> (16 - shift))) & MASK16


# ── Calcul des segments ──────────────────────────────────────────────

def segment_a(username: str) -> int:
    """Segment A — Hash FNV-1a managé, salt = 'REV3RSE!'."""
    data = username.lower().encode("utf-8") + SALT_MANAGED
    return fold16(fnv1a_32(data))


def segment_b(username: str) -> int:
    """Segment B — Hash FNV-1a natif, salt = 'NATIVERE'."""
    data = username.lower().encode("utf-8") + SALT_NATIVE
    return fold16(fnv1a_32(data))


def segment_c(a: int, b: int) -> int:
    """Segment C — XOR croisé avec rotation et mélange multiplicatif."""
    rot_a = rotl16(a, 5)
    result = rot_a ^ b
    result = (result * 0x9E37) & MASK16
    result ^= 0xA5A5
    return result


def native_checksum(a: int, b: int, c: int) -> int:
    """Partie native du segment D — reproduit compute_checksum() de la .so."""
    val = a & MASK16
    val = rotl16(val, 3)         # rotation gauche 3 bits
    val ^= b & MASK16
    val = rotl16(val, 9)         # rotation gauche 9 bits = droite 7 bits
    val ^= c & MASK16
    val = (val * 0x5BD1) & MASK16
    val ^= 0x1337
    return val


def segment_d(a: int, b: int, c: int) -> int:
    """Segment D — XOR entre la somme managée et le checksum natif."""
    managed = (a + b + c) & MASK16
    native  = native_checksum(a, b, c)
    return (managed ^ native) & MASK16


# ── Keygen ────────────────────────────────────────────────────────────

def keygen(username: str) -> str:
    """Génère une clé de licence valide pour le username donné."""
    a = segment_a(username)
    b = segment_b(username)
    c = segment_c(a, b)
    d = segment_d(a, b, c)
    return f"{a:04X}-{b:04X}-{c:04X}-{d:04X}"


# ── Tests ─────────────────────────────────────────────────────────────

def run_tests():
    """Génère des clés pour plusieurs usernames de test."""
    test_users = [
        "alice",
        "bob",
        "Charlie",           # majuscule → vérifie la normalisation
        "café",              # caractère non-ASCII (UTF-8 multi-octets)
        "müller",            # tréma
        "naïve",             # tréma + accent
        "user@2025",         # caractères spéciaux
        "",                  # vide (edge case — le programme rejette avant)
        "a",                 # très court
        "A" * 100,           # long
    ]

    print("┌──────────────────┬───────────────────────┐")
    print("│    Username      │    Clé générée        │")
    print("├──────────────────┼───────────────────────┤")

    for user in test_users:
        if not user:
            print(f"│ {'(vide)':16s} │ {'N/A — rejeté par le programme':21s} │")
            continue
        key = keygen(user)
        display = user if len(user) <= 16 else user[:13] + "..."
        print(f"│ {display:16s} │ {key:21s} │")

    print("└──────────────────┴───────────────────────┘")
    print()
    print("Vérification :")
    print("  cd binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64")
    print('  LD_LIBRARY_PATH=. ./LicenseChecker <username> "<clé>"')


# ── Point d'entrée ────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage : {sys.argv[0]} <username>")
        print(f"        {sys.argv[0]} --test")
        sys.exit(1)

    if sys.argv[1] == "--test":
        run_tests()
    else:
        username = sys.argv[1]
        key = keygen(username)
        print(f"  Username : {username}")
        print(f"  Clé      : {key}")
```

### Points clés de la solution

**Le piège des salts.** C'est le point central du checkpoint. Le segment A utilise le salt `"REV3RSE!"` (octets `52 45 56 33 52 53 45 21`), lu dans le champ `MagicSalt` de `LicenseValidator`. Le segment B utilise le salt `"NATIVERE"` (octets `4E 41 54 49 56 45 52 45`), lu dans le tableau `NATIVE_SALT` de `native_check.c`. Si on utilise le même salt pour les deux segments, le keygen produit des clés incorrectes pour le segment B.

**La normalisation en minuscules.** Les deux côtés (C# et natif) convertissent le username en minuscules avant le hashing. Le code C# utilise `username.ToLowerInvariant()`, qui applique les règles Unicode. Le code C soustrait simplement `0x20` aux caractères A-Z (conversion ASCII uniquement). Pour les usernames purement ASCII, les deux conversions sont identiques. Pour les usernames contenant des caractères Unicode (comme `café` ou `müller`), le comportement peut diverger — mais `ToLowerInvariant()` sur un username déjà en minuscules est un no-op, et la conversion C ne touche que les lettres ASCII. En pratique, la clé est correcte si le username est converti en minuscules par Python avec `.lower()` avant le hashing.

**La rotation droite 7 = rotation gauche 9.** Dans `compute_checksum`, le code C effectue `(val >> 7) | (val << 9)` sur 16 bits. C'est une rotation à droite de 7 bits, ce qui est équivalent à une rotation à gauche de 9 bits (puisque 7 + 9 = 16). La fonction `rotl16(val, 9)` du keygen implémente cela correctement.

**Le segment D combine deux sources.** La partie managée est la somme `(A + B + C) & 0xFFFF`. La partie native est le retour de `compute_checksum(A, B, C)`. Le résultat final est le XOR des deux. Un oubli de l'une des deux parties produit un segment D incorrect.

---

## Livrable 3 — Assembly patché

### Variante A — Patch C# (`LicenseChecker_patch_csharp.dll`)

Dans dnSpy, clic droit sur `LicenseValidator.Validate()` → **Edit Method (C#)**. Remplacer le corps entier par :

```csharp
public ValidationResult Validate(string username, string licenseKey)
{
    return new ValidationResult
    {
        IsValid        = true,
        FailureReason  = "",
        LicenseLevel   = "Enterprise",
        ExpirationInfo = "Perpétuelle"
    };
}
```

Cliquer **Compile**, puis **File → Save Module** → `LicenseChecker_patch_csharp.dll`.

**Pourquoi cette approche élimine la dépendance native** : la nouvelle implémentation de `Validate()` ne contient aucun appel à `CheckSegmentB` ni à `ComputeFinalChecksum`, qui sont les deux méthodes transitant par `NativeBridge`. Les déclarations `[DllImport]` restent dans les métadonnées mais ne sont jamais invoquées, donc `libnative_check.so` n'est jamais chargée.

### Variante B — Patch IL minimal (`LicenseChecker_patch_il.dll`)

On ouvre `LicenseValidator.Validate()` dans l'éditeur IL de dnSpy (clic droit → **Edit IL Instructions**).

La méthode contient quatre blocs de comparaison/échec, un par segment. Chaque bloc suit le même pattern. Voici les modifications pour chacun.

#### Check du segment A

On cherche la séquence qui compare `actualA` et `expectedA`. Le pattern exact dépend de la compilation, mais il ressemble à :

```
IL_XXXX: ldloc.s  V_actualA       // empile actualA (uint)  
IL_XXXX: ldloc.s  V_expectedA     // empile expectedA (uint)  
IL_XXXX: bne.un   IL_ECHEC_A      // si ≠, sauter au bloc d'échec  
```

**Modification** : remplacer ces trois instructions par :

```
IL_XXXX: pop                       // consommer expectedA (résidu de ldloc)  
IL_XXXX: pop                       // consommer actualA (résidu de ldloc)  
         nop                       // padding si nécessaire
```

Ou, plus proprement, remplacer uniquement le `bne.un IL_ECHEC_A` par la séquence suivante (en laissant les deux `ldloc` en place, puisqu'ils empilent les valeurs qui doivent être consommées) :

```
IL_XXXX: ldloc.s  V_actualA       // (inchangé)  
IL_XXXX: ldloc.s  V_expectedA     // (inchangé)  
IL_XXXX: pop                       // jeter expectedA  
IL_XXXX: pop                       // jeter actualA  
IL_XXXX: br       IL_SUITE_A      // sauter au check suivant  
```

La cible `IL_SUITE_A` est l'instruction qui suit le bloc d'échec — c'est l'instruction où le check du segment B commence.

> **Pourquoi `pop` + `pop` + `br` et non simplement `br`** : les deux `ldloc` précédents ont empilé deux valeurs. L'instruction `bne.un` les consommait ; si on la remplace par `br` (qui ne consomme rien), la pile sera incohérente — le vérificateur IL du CLR détectera deux valeurs excédentaires et lèvera une `InvalidProgramException`. Les deux `pop` restaurent la pile à son état attendu avant le saut.

#### Check du segment B

Le pattern est légèrement différent : `CheckSegmentB` retourne un `bool`, et le code IL teste ce booléen :

```
IL_XXXX: ldloc.s  V_segBValid     // empile le booléen  
IL_XXXX: brfalse  IL_ECHEC_B      // si false, sauter au bloc d'échec  
```

**Modification** : remplacer par :

```
IL_XXXX: ldloc.s  V_segBValid     // (inchangé)  
IL_XXXX: pop                       // jeter le booléen  
IL_XXXX: br       IL_SUITE_B      // sauter au check suivant  
```

Ici un seul `pop` suffit car `brfalse` ne consomme qu'une valeur.

#### Checks des segments C et D

Même pattern que le segment A — deux valeurs empilées, `bne.un` vers le bloc d'échec. Même correction : `pop` + `pop` + `br` vers la suite.

#### Résumé des modifications

| Check | Instruction originale | Remplacement | Pile |  
|---|---|---|---|  
| Segment A | `bne.un IL_ECHEC_A` | `pop` + `pop` + `br IL_SUITE_A` | -2 + 0 = OK |  
| Segment B | `brfalse IL_ECHEC_B` | `pop` + `br IL_SUITE_B` | -1 + 0 = OK |  
| Segment C | `bne.un IL_ECHEC_C` | `pop` + `pop` + `br IL_SUITE_C` | -2 + 0 = OK |  
| Segment D | `bne.un IL_ECHEC_D` | `pop` + `pop` + `br IL_SUITE_D` | -2 + 0 = OK |

Après ces quatre modifications, cliquer **OK** puis **File → Save Module** → `LicenseChecker_patch_il.dll`.

**Vérification** : la logique de calcul (FNV-1a, XOR croisé, checksums) s'exécute toujours normalement. Les appels P/Invoke sont toujours effectués. Seuls les résultats des comparaisons sont ignorés. Si `libnative_check.so` est absente, l'exception dans `CheckSegmentB` est attrapée par le `try/catch` du code original, `segBValid` vaut `false`, mais le patch saute quand même au-dessus du bloc d'échec — le programme continue.

> 💡 **Variante alternative du patch IL** : au lieu de remplacer les sauts conditionnels, on peut remplacer chaque `ldloc.s V_actualX` par `ldloc.s V_expectedX` juste avant la comparaison. Les deux valeurs empilées étant alors identiques, le `bne.un` n'est jamais pris et le flux continue naturellement. Cette approche ne modifie qu'une instruction par check (au lieu de trois) et ne change pas la structure du flux de contrôle, ce qui la rend moins détectable. Mais elle ne fonctionne que si les variables `expectedX` sont bien des variables locales distinctes — ce qui dépend du niveau d'optimisation du compilateur C#.

---

## Rapport d'accompagnement (modèle)

### Schéma de validation reconstitué

L'application `LicenseChecker` valide une clé au format `AAAA-BBBB-CCCC-DDDD` (4 groupes de 4 caractères hexadécimaux) pour un nom d'utilisateur donné. Chaque segment est calculé par un algorithme distinct.

**Segment A** — Hash FNV-1a 32 bits du username (en minuscules, encodé UTF-8) concaténé avec le salt `"REV3RSE!"` (8 octets : `52 45 56 33 52 53 45 21`). Le hash 32 bits est replié sur 16 bits par XOR des moitiés haute et basse. Ce calcul est effectué entièrement en C# dans la méthode `ComputeUserHash()`. Constantes FNV-1a : offset basis `0x811C9DC5`, prime `0x01000193`.

**Segment B** — Même algorithme FNV-1a que le segment A, mais avec un salt différent : `"NATIVERE"` (8 octets : `4E 41 54 49 56 45 52 45`). Ce calcul est effectué dans la bibliothèque native `libnative_check.so`, fonction `compute_native_hash()`, appelée via P/Invoke depuis `CheckSegmentB()`. La différence de salt entre les segments A et B est le piège principal de l'application — un reverse engineer qui ne lit que le code C# et suppose le même salt pour le côté natif produit un segment B incorrect.

**Segment C** — Combinaison des segments A et B par rotation, XOR et mélange multiplicatif. Algorithme : rotation à gauche de 5 bits de A (sur 16 bits), XOR avec B, multiplication par `0x9E37` masquée sur 16 bits, XOR avec `0xA5A5`. Calcul entièrement en C# dans `ComputeCrossXor()`.

**Segment D** — Combinaison d'une partie managée et d'une partie native. La partie managée est la somme `(A + B + C) & 0xFFFF`. La partie native est le retour de `compute_checksum(A, B, C)` via P/Invoke, qui effectue des rotations (gauche 3 bits, droite 7 bits), des XOR, une multiplication par `0x5BD1` et un XOR avec `0x1337`. Le segment D final est le XOR des deux parties, masqué sur 16 bits.

### Démarche suivie

**Triage** : `file` sur les deux binaires, `strings` sur l'assembly (noms de classes, messages d'erreur, noms de fonctions P/Invoke) et sur la `.so` (salt `"NATIVERE"`), `nm -D` sur la `.so` (fonctions exportées).

**Analyse statique managée** : ouverture de `LicenseChecker.dll` dans dnSpy, lecture du code décompilé de `LicenseValidator.Validate()` et de ses méthodes auxiliaires. Identification du flux linéaire en 5 étapes, extraction des constantes (salts, FNV offset/prime, constantes multiplicatives).

**Analyse statique native** : import de `libnative_check.so` dans Ghidra, décompilation de `compute_native_hash` et `compute_checksum`. Identification du salt natif différent du salt managé. Reconstruction des algorithmes en pseudo-code.

**Validation dynamique** : script Frida combiné CLR + natif pour capturer les 4 segments. Vérification que les clés capturées sont acceptées par l'application. Confirmation de la compréhension des algorithmes.

**Keygen** : implémentation en Python des 4 segments, avec les bons salts. Validation sur 5+ usernames incluant des caractères non-ASCII.

**Patching** : variante A (réécriture C# de `Validate()`) et variante B (4 patchs IL sur les sauts conditionnels, avec gestion de la pile par `pop` avant les `br`).

### Difficultés et résolutions

La principale difficulté a été l'identification des deux salts différents. L'analyse statique du côté C# seul ne révèle que `"REV3RSE!"`. Il faut analyser la bibliothèque native (avec Ghidra, `strings`, ou un hook Frida) pour découvrir `"NATIVERE"`. Le hooking natif sur `compute_native_hash` permet de valider rapidement que le hash natif diffère du hash managé pour le même input.

La seconde difficulté a été la gestion de la pile IL pour le patch de variante B. Le remplacement naïf d'un `bne.un` par un `br` provoque une `InvalidProgramException` parce que la pile contient deux valeurs excédentaires. L'ajout de deux `pop` avant le `br` corrige le problème. L'éditeur IL de dnSpy signale l'erreur de pile avant la sauvegarde, ce qui permet de la détecter et de la corriger immédiatement.

---


⏭️
