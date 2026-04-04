🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 32.5 — Cas pratique : contourner une vérification de licence C#

> 📁 **Fichiers utilisés** : `binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64/LicenseChecker.dll`, `binaries/ch32-dotnet/native/libnative_check.so`  
> 🔧 **Outils** : dnSpy, Frida, GDB/GEF, nm, strings, objdump  
> 📖 **Prérequis** : Sections [32.1](/32-analyse-dynamique-dotnet/01-debug-dnspy-sans-sources.md) à [32.4](/32-analyse-dynamique-dotnet/04-patcher-il-dnspy.md)

---

## Objectif et contexte

Ce cas pratique mobilise l'ensemble des techniques vues dans les quatre sections précédentes pour atteindre deux objectifs distincts sur notre application `LicenseChecker` :

**Objectif 1 — Keygen.** Comprendre le schéma de validation, extraire les algorithmes, et écrire un générateur de clés capable de produire une licence valide pour n'importe quel nom d'utilisateur. Cet objectif exige de reverser les deux côtés du pont managé–natif : le hash C# (segment A), le hash natif (segment B), le XOR croisé (segment C) et le checksum combiné (segment D).

**Objectif 2 — Patch universel.** Produire une version modifiée de `LicenseChecker.dll` qui accepte toute clé sans vérification. Cet objectif est plus rapide à atteindre mais moins formateur — on ne comprend pas les algorithmes, on les neutralise.

La démarche suit le workflow de RE que les chapitres précédents ont construit : triage, analyse statique, analyse dynamique, exploitation. Chaque phase fait appel à des outils spécifiques et chaque découverte alimente la suivante.

---

## Phase 1 — Triage et reconnaissance

On commence par le même réflexe qu'au chapitre 5 : prendre la mesure de la cible avant de plonger dans les détails.

### Inspection du livrable

Le répertoire de publication contient plusieurs fichiers. `LicenseChecker.dll` est l'assembly principal — c'est notre cible managée. `LicenseChecker.runtimeconfig.json` indique le framework cible (.NET 8.0). `libnative_check.so` est la bibliothèque native appelée par P/Invoke.

```bash
cd binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64

file LicenseChecker.dll
# → PE32 executable (console) Intel 80386 Mono/.Net assembly, ...

file libnative_check.so
# → ELF 64-bit LSB shared object, x86-64, dynamically linked, ...

strings LicenseChecker.dll | grep -i licen
# → LicenseValidator
# → LicenseChecker
# → LicenseLevel
# → ValidationResult
# → Licence valide
# → Licence invalide
# → libnative_check.so
# → compute_native_hash
# → compute_checksum
# → verify_integrity
```

La commande `strings` sur l'assembly .NET est immédiatement révélatrice. Contrairement à un binaire natif strippé où `strings` ne capture que les littéraux, un assembly .NET conserve tous les noms de types, de méthodes, de champs et de chaînes dans ses tables de métadonnées. On voit apparaître les noms des classes (`LicenseValidator`, `ValidationResult`), les noms des fonctions P/Invoke (`compute_native_hash`, `compute_checksum`, `verify_integrity`), et les messages utilisateur (`Licence valide`, `Licence invalide`). Le nom de la bibliothèque native (`libnative_check.so`) est aussi présent en clair.

Du côté natif, on applique le triage classique :

```bash
nm -D libnative_check.so
# → T compute_checksum
# → T compute_native_hash
# → T verify_integrity

strings libnative_check.so
# → NATIVERE

objdump -d -M intel libnative_check.so | head -80
```

Le symbole exporté `compute_native_hash` et la chaîne `"NATIVERE"` confirment la structure attendue. Les trois fonctions sont visibles et analysables.

### Premier lancement

On lance l'application pour observer son comportement nominal :

```
$ LD_LIBRARY_PATH=. ./LicenseChecker

    ╔══════════════════════════════════════════╗
    ║   LicenseChecker v3.2.1 — Ch.32 RE Lab   ║
    ║   © 2025 Formation RE GCC/G++            ║
    ╚══════════════════════════════════════════╝

  Nom d'utilisateur : alice
  Clé de licence    : AAAA-BBBB-CCCC-DDDD

  ╔═══════════════════════════════════════╗
  ║  ❌  Licence invalide.                ║
  ╚═══════════════════════════════════════╝

  Raison : Segment 1 invalide (lié au nom d'utilisateur).
```

Le message d'erreur est précieux. « Segment 1 invalide (lié au nom d'utilisateur) » nous indique que la validation échoue dès le premier segment, et que ce segment dépend du nom d'utilisateur. C'est un point d'ancrage pour l'analyse.

---

## Phase 2 — Analyse statique dans dnSpy

On ouvre `LicenseChecker.dll` dans dnSpy. Le code décompilé est immédiatement lisible — l'assembly n'est pas obfusqué.

### Cartographie du flux de validation

En examinant `LicenseValidator.Validate()`, on reconstitue le schéma complet. La méthode effectue cinq étapes séquentielles. Si une étape échoue, elle retourne immédiatement un `ValidationResult` avec `IsValid = false` et un message d'erreur spécifique. Si toutes les étapes réussissent, elle retourne `IsValid = true`.

La structure est linéaire — pas de boucle, pas de récursion, pas de callback. Chaque étape est isolée dans sa propre méthode privée. C'est une architecture favorable au RE : on peut attaquer chaque segment indépendamment.

### Analyse du segment A : `ComputeUserHash()`

Le code décompilé de `ComputeUserHash` révèle un algorithme FNV-1a standard. On identifie les constantes caractéristiques : `0x811C9DC5` (offset basis 32 bits) et `0x01000193` (prime). Le username est converti en minuscules, concaténé avec un salt (`MagicSalt`), puis hashé. Le résultat 32 bits est replié sur 16 bits par XOR des moitiés haute et basse.

En inspectant le champ `MagicSalt`, on lit ses octets : `0x52, 0x45, 0x56, 0x33, 0x52, 0x53, 0x45, 0x21`. En convertissant en ASCII : `"REV3RSE!"`. C'est le salt managé.

On a maintenant toutes les informations pour implémenter le calcul du segment A en Python.

### Analyse du segment B : `CheckSegmentB()` et le pont P/Invoke

Le code de `CheckSegmentB` appelle `NativeBridge.ComputeNativeHash(data, data.Length)` puis compare le résultat (masqué sur 16 bits) avec le segment B fourni. La déclaration P/Invoke dans `NativeBridge` pointe vers `compute_native_hash` dans `libnative_check.so`.

Pour comprendre l'algorithme natif, deux options s'offrent à nous : reverser la bibliothèque `.so` avec les outils natifs, ou capturer la valeur dynamiquement. On fera les deux — la première pour le keygen, la seconde pour valider notre compréhension.

On ouvre `libnative_check.so` dans Ghidra. Le décompilé de `compute_native_hash` révèle le même algorithme FNV-1a que côté C#, mais avec un salt différent. En examinant les octets référencés dans la fonction, on trouve : `0x4E, 0x41, 0x54, 0x49, 0x56, 0x45, 0x52, 0x45` → `"NATIVERE"`.

C'est le piège que le chapitre annonçait : les deux côtés utilisent le même algorithme de hash (FNV-1a) mais avec des salts différents (`"REV3RSE!"` côté C#, `"NATIVERE"` côté natif). Un reverse engineer qui ne regarderait que le code C# et supposerait que le hash natif est identique produirait un segment B incorrect.

### Analyse du segment C : `ComputeCrossXor()`

Le code décompilé est explicite : rotation à gauche de 5 bits de `segA` (sur 16 bits), XOR avec `segB`, multiplication par `0x9E37`, masquage sur 16 bits, XOR avec `0xA5A5`. Toutes les constantes sont visibles dans le code décompilé — pas de recours au natif ici.

### Analyse du segment D : `ComputeFinalChecksum()`

Le segment D combine une partie managée (somme de A + B + C, masquée sur 16 bits) et une partie native (retour de `compute_checksum(A, B, C)` via P/Invoke). Le résultat final est le XOR des deux parties, masqué sur 16 bits.

On retourne dans Ghidra pour analyser `compute_checksum`. Le décompilé montre une séquence de rotations et de XOR avec les trois segments en entrée, suivie d'une multiplication par `0x5BD1` et d'un XOR avec `0x1337`. Toutes les constantes sont extractibles.

### Bilan de l'analyse statique

À ce stade, on possède une compréhension complète du schéma de licence :

```
Username → lowercase → UTF-8 bytes

Segment A = fold16(FNV-1a(bytes || "REV3RSE!"))  
Segment B = fold16(FNV-1a(bytes || "NATIVERE"))  
Segment C = ((rotl5_16(A) ^ B) * 0x9E37 & 0xFFFF) ^ 0xA5A5  
Segment D = (A + B + C) & 0xFFFF) ^ native_checksum(A, B, C)  

avec :
  fold16(h)          = (h >> 16) ^ (h & 0xFFFF)
  rotl5_16(x)        = ((x << 5) | (x >> 11)) & 0xFFFF
  native_checksum    = ((((rotl3_16(A) ^ B) >> 7 | ... ) * 0x5BD1) ^ 0x1337
```

On pourrait s'arrêter ici et écrire le keygen. Mais la phase dynamique va nous permettre de valider cette compréhension — et de démontrer les techniques des sections 32.1 à 32.3.

---

## Phase 3 — Validation dynamique

### Débogage avec dnSpy (§32.1)

On lance `LicenseChecker` dans le débogueur dnSpy. On pose des breakpoints sur les cinq points de comparaison dans `Validate()`. On entre `alice` comme username et `0000-0000-0000-0000` comme clé.

L'exécution s'arrête au premier check (segment A). Dans la fenêtre Locals, on lit la valeur de `expectedA`. Supposons qu'elle vaut `0x7B3F`. On note cette valeur.

On utilise **Set Next Statement** pour sauter le bloc d'échec et atteindre le deuxième check. L'exécution tente d'appeler `CheckSegmentB`, qui déclenche le chargement de `libnative_check.so` et l'appel P/Invoke. Dans Locals, on lit la valeur de `expected` dans `CheckSegmentB` — c'est le segment B. Supposons `0xD4A2`.

On continue de la même manière pour les segments C et D, en forçant les comparaisons précédentes à réussir (par modification de variables dans Locals). À la fin, on a les quatre valeurs :

```
alice → 7B3F-D4A2-????-????
```

Les segments C et D dépendent des valeurs correctes de A et B. Pour les obtenir, on relance le débogage avec la clé partiellement correcte `7B3F-D4A2-0000-0000`. Cette fois, les checks A et B passent, et on atteint les checks C et D avec les bonnes valeurs intermédiaires. On lit `expectedC` et `expectedD`. La clé complète se construit itérativement.

### Hooking Frida — capture automatique (§32.2 + §32.3)

Le débogage itératif fonctionne mais demande plusieurs passes manuelles. Le script Frida combiné de la section 32.3 automatise tout en une seule exécution. Le point clé : la méthode `Validate()` originale retourne dès le premier check raté, donc un simple wrapper ne suffit pas. Le script court-circuite ce flux en appelant directement chaque méthode de calcul (`ComputeUserHash`, `CheckSegmentB`, `ComputeCrossXor`, `ComputeFinalChecksum`) dans le bon ordre, avec les bonnes valeurs. Les hooks natifs sur `compute_native_hash` et `compute_checksum` se déclenchent de manière synchrone pendant les appels P/Invoke, capturant les valeurs côté natif. On lance :

```bash
frida -f ./LicenseChecker --runtime=clr -l keygen_complete.js
```

L'application démarre sous le contrôle de Frida. On entre `alice` et `0000-0000-0000-0000`. Le hook `Validate` appelle les méthodes de calcul directement, les hooks natifs capturent le segment B au passage, et la clé complète s'affiche :

```
╔═════════════════════════════════════════════╗
║      KEYGEN COMPLET — CLR + NATIF           ║
╠═════════════════════════════════════════════╣
║  Username  : alice                          ║
║  Segment A : 7B3F                           ║
║  Segment B : D4A2                           ║
║  Segment C : E819                           ║
║  Segment D : 5CF6                           ║
║                                             ║
║  CLÉ VALIDE : 7B3F-D4A2-E819-5CF6           ║
╚═════════════════════════════════════════════╝
```

> 💡 Les valeurs ci-dessus sont fictives — elles dépendent de l'implémentation exacte des algorithmes. Vos valeurs réelles seront différentes.

On vérifie en relançant l'application sans Frida, avec la clé obtenue :

```
$ LD_LIBRARY_PATH=. ./LicenseChecker alice 7B3F-D4A2-E819-5CF6

  ╔═══════════════════════════════════╗
  ║  ✅  Licence valide ! Bienvenue.  ║
  ╚═══════════════════════════════════╝

  Utilisateur : alice
  Niveau      : Professional
  Expiration  : Perpétuelle
```

La clé est valide. La phase dynamique confirme notre compréhension statique.

---

## Phase 4 — Écriture du keygen

Fort de l'analyse statique (algorithmes complets) et de la validation dynamique (valeurs de référence), on écrit un keygen autonome en Python. Ce script n'a besoin d'aucun outil externe — il réimplémente les algorithmes de calcul de chaque segment.

```python
#!/usr/bin/env python3
"""
keygen.py — Générateur de clés pour LicenseChecker (Chapitre 32)

Réimplémente les 4 segments du schéma de licence :
  A = fold16(FNV-1a(username || "REV3RSE!"))
  B = fold16(FNV-1a(username || "NATIVERE"))
  C = ((rotl5(A) ^ B) * 0x9E37 & 0xFFFF) ^ 0xA5A5
  D = ((A + B + C) & 0xFFFF) ^ native_checksum(A, B, C)

Usage :
  python3 keygen.py <username>
"""

import sys


# ── FNV-1a 32-bit ──────────────────────────────────────────────────

FNV_OFFSET = 0x811C9DC5  
FNV_PRIME  = 0x01000193  
MASK32     = 0xFFFFFFFF  
MASK16     = 0xFFFF  


def fnv1a_32(data: bytes) -> int:
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & MASK32
    return h


def fold16(h: int) -> int:
    return ((h >> 16) ^ (h & MASK16)) & MASK16


# ── Segment A : hash managé (salt = "REV3RSE!") ───────────────────

def compute_segment_a(username: str) -> int:
    data = username.lower().encode("utf-8") + b"REV3RSE!"
    return fold16(fnv1a_32(data))


# ── Segment B : hash natif (salt = "NATIVERE") ────────────────────

def compute_segment_b(username: str) -> int:
    data = username.lower().encode("utf-8") + b"NATIVERE"
    return fold16(fnv1a_32(data))


# ── Segment C : XOR croisé avec rotation ──────────────────────────

def rotl16(value: int, shift: int) -> int:
    return ((value << shift) | (value >> (16 - shift))) & MASK16


def compute_segment_c(seg_a: int, seg_b: int) -> int:
    rot_a = rotl16(seg_a, 5)
    result = rot_a ^ seg_b
    result = (result * 0x9E37) & MASK16
    result ^= 0xA5A5
    return result


# ── Segment D : checksum final (managé ^ natif) ───────────────────

def native_checksum(seg_a: int, seg_b: int, seg_c: int) -> int:
    val = seg_a & MASK16
    # Rotation gauche 3 bits (16 bits)
    val = rotl16(val, 3)
    val ^= seg_b & MASK16
    # Rotation droite 7 bits (16 bits) = rotation gauche 9 bits
    val = rotl16(val, 9)
    val ^= seg_c & MASK16
    # Mélange multiplicatif
    val = (val * 0x5BD1) & MASK16
    val ^= 0x1337
    return val


def compute_segment_d(seg_a: int, seg_b: int, seg_c: int) -> int:
    managed = (seg_a + seg_b + seg_c) & MASK16
    native  = native_checksum(seg_a, seg_b, seg_c)
    return (managed ^ native) & MASK16


# ── Point d'entrée ────────────────────────────────────────────────

def keygen(username: str) -> str:
    a = compute_segment_a(username)
    b = compute_segment_b(username)
    c = compute_segment_c(a, b)
    d = compute_segment_d(a, b, c)
    return f"{a:04X}-{b:04X}-{c:04X}-{d:04X}"


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage : {sys.argv[0]} <username>")
        sys.exit(1)

    username = sys.argv[1]
    key = keygen(username)

    print(f"  Username : {username}")
    print(f"  Clé      : {key}")
```

On valide le keygen :

```bash
$ python3 keygen.py alice
  Username : alice
  Clé      : 7B3F-D4A2-E819-5CF6

$ LD_LIBRARY_PATH=. ./LicenseChecker alice 7B3F-D4A2-E819-5CF6
  ✅  Licence valide ! Bienvenue.

$ python3 keygen.py bob
  Username : bob
  Clé      : 39F1-AB07-C4E3-8D2A

$ LD_LIBRARY_PATH=. ./LicenseChecker bob 39F1-AB07-C4E3-8D2A
  ✅  Licence valide ! Bienvenue.
```

Le keygen fonctionne pour n'importe quel nom d'utilisateur. L'objectif 1 est atteint.

---

## Phase 5 — Patch universel

Le deuxième objectif — une version patchée qui accepte toute clé — se réalise en quelques minutes avec dnSpy.

### Approche 1 : réécriture C# de `Validate()`

On ouvre `LicenseChecker.dll` dans dnSpy. Clic droit sur `Validate()` → **Edit Method (C#)**. On remplace le corps :

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

Compile → **File → Save Module** sous le nom `LicenseChecker_patched.dll`. On copie ce fichier à la place de l'original. Toute combinaison username/clé est acceptée.

C'est la méthode la plus rapide et la plus robuste. Elle supprime intégralement la dépendance envers `libnative_check.so` — l'application patchée fonctionne même si la bibliothèque native est absente, puisque les appels P/Invoke ne sont plus atteints.

### Approche 2 : patching IL minimal

Si on préfère un patch moins visible — qui laisse la logique de validation en place mais neutralise les quatre checks — on utilise l'éditeur IL.

Pour chaque étape de validation, on identifie le saut conditionnel qui mène au bloc d'échec et on le transforme en saut inconditionnel vers la suite. En pratique, dans le code IL de `Validate()`, on repère les patterns suivants :

```
ldloc  actualX          ← charge la valeur fournie par l'utilisateur  
ldloc  expectedX        ← charge la valeur attendue  
bne.un IL_ECHEC_X       ← si différent, sauter au bloc d'échec  
```

Pour chacun de ces quatre patterns, on remplace `bne.un IL_ECHEC_X` par `pop` + `pop` + `br IL_SUITE_X` — on dépile les deux valeurs de comparaison et on saute inconditionnellement vers l'étape suivante. Le deuxième check (segment B) a un pattern légèrement différent puisqu'il passe par `CheckSegmentB` qui retourne un booléen, mais le principe est le même : on remplace le `brfalse` par un `pop` + `br`.

> Pourquoi `pop` + `pop` + `br` plutôt que simplement remplacer `bne.un` par `br` ? Parce que `bne.un` consomme deux valeurs sur la pile (les deux opérandes de la comparaison), tandis que `br` n'en consomme aucune. Si on remplaçait directement sans dépiler, la pile serait incohérente et le vérificateur IL rejetterait le bytecode.

Après avoir patché les quatre sauts, on sauvegarde. L'application modifiée exécute tout le flux de validation — elle calcule les hashes, appelle les fonctions natives, effectue les XOR — mais ignore silencieusement les résultats des comparaisons. Chaque étape « réussit » quel que soit l'input.

### Approche 3 : `LD_PRELOAD` sur la bibliothèque native

Pour compléter le panorama, on peut aussi intervenir côté natif sans toucher à l'assembly .NET. La technique `LD_PRELOAD` vue au chapitre 22 permet de remplacer `libnative_check.so` par notre propre version qui retourne des valeurs contrôlées.

On écrit un fichier `fake_native.c` :

```c
#include <stdint.h>

uint32_t compute_native_hash(const uint8_t *data, int length)
{
    (void)data; (void)length;
    return 0x0000;  /* Retourne toujours 0 */
}

uint32_t compute_checksum(uint32_t a, uint32_t b, uint32_t c)
{
    (void)a; (void)b; (void)c;
    return (a + b + c) & 0xFFFF;  /* Retourne la somme (= partie managée) */
}

int verify_integrity(const char *u, uint32_t a, uint32_t b,
                     uint32_t c, uint32_t d)
{
    (void)u; (void)a; (void)b; (void)c; (void)d;
    return 1;  /* Toujours valide */
}
```

```bash
gcc -shared -fPIC -o fake_native.so fake_native.c  
LD_PRELOAD=./fake_native.so LD_LIBRARY_PATH=. ./LicenseChecker  
```

Cette approche ne bypasse pas la validation côté C# — il faut encore fournir une clé dont le segment A correspond au hash managé et dont les segments suivants sont cohérents avec les valeurs retournées par la fausse bibliothèque. C'est donc un outil partiel, utile pour isoler le comportement natif pendant l'analyse, mais insuffisant pour un bypass complet à lui seul.

---

## Phase 6 — Synthèse et comparaison des approches

Chacune des techniques employées dans ce cas pratique a ses forces et ses limites. Le tableau ci-dessous les met en perspective pour guider le choix en situation réelle.

| Approche | Effort | Résultat | Compréhension acquise | Détectabilité |  
|---|---|---|---|---|  
| **Débogage dnSpy** (itératif) | Moyen | Clé valide pour 1 username | Partielle (valeurs observées, pas algorithmes) | Aucune trace sur disque |  
| **Hooking Frida** (keygen dynamique) | Moyen | Clé valide pour 1 username par exécution | Partielle (idem) | Aucune trace sur disque |  
| **Keygen Python** (reimplementation) | Élevé | Clé valide pour tout username, offline | Totale (tous les algorithmes compris) | Aucune — programme indépendant |  
| **Patch C# dans dnSpy** | Faible | Bypass universel permanent | Minimale (on sait où est le check, pas comment) | Assembly modifié (hash, taille, signature) |  
| **Patch IL minimal** | Moyen | Bypass universel permanent | Partielle (structure du flux comprise) | Assembly modifié (moins visible) |  
| **`LD_PRELOAD`** | Faible | Contrôle du côté natif uniquement | Partielle (interface P/Invoke comprise) | Fichier `.so` supplémentaire |

En pratique, un reverse engineer expérimenté combine les approches. Il commence par le hooking Frida pour un résultat rapide (obtenir une clé valide en quelques minutes), puis il approfondit avec l'analyse statique pour écrire un keygen autonome. Le patch IL est réservé aux situations où on a besoin d'une modification permanente et où le keygen n'est pas une option (par exemple, un logiciel qui vérifie la licence en continu pendant son fonctionnement, pas seulement au démarrage).

---

## Récapitulatif du chapitre

Ce cas pratique conclut le chapitre 32. En partant d'un assembly .NET inconnu accompagné d'une bibliothèque native, on a parcouru un cycle complet de reverse engineering dynamique :

En **section 32.1**, on a découvert que dnSpy transforme le débogage d'un programme .NET sans sources en une expérience comparable à celle d'un développeur dans son IDE. Les breakpoints sur le code décompilé, l'inspection des variables, la modification du flux d'exécution — autant de capacités qui rendent l'analyse dynamique .NET qualitativement plus confortable que son équivalent natif avec GDB.

En **section 32.2**, on a étendu cette capacité avec Frida et son bridge CLR, passant de l'observation manuelle (un breakpoint à la fois) à l'instrumentation programmable (des hooks sur toutes les méthodes intéressantes simultanément). Le keygen par hooking illustre un paradigme puissant : on ne reverse pas l'algorithme, on laisse l'algorithme calculer et on capture ses résultats.

En **section 32.3**, on a franchi le pont P/Invoke pour atteindre le code natif que le monde managé ne peut pas voir. La combinaison Frida CLR + Frida natif dans un seul script a permis de compléter le keygen là où le hooking managé seul échouait. GDB a montré que les outils du chapitre 11 s'appliquent sans modification au code natif appelé par un processus .NET.

En **section 32.4**, on est passé de l'intervention éphémère au patch permanent. L'édition en C# dans dnSpy offre un confort sans équivalent dans le monde natif. L'édition IL permet des patchs chirurgicaux quand la réécriture complète est disproportionnée. Les métadonnées sont une surface d'intervention supplémentaire, souvent sous-estimée.

Ce cas pratique a montré comment ces quatre compétences se composent pour atteindre des objectifs de complexité croissante — de la capture d'une valeur unique dans le débogueur jusqu'à l'écriture d'un keygen autonome qui réimplémente l'intégralité du schéma de validation.

---

> ⏭️ **Prochaine étape** : [🎯 Checkpoint — Patcher et keygenner l'application .NET fournie](/32-analyse-dynamique-dotnet/checkpoint.md)

⏭️ [🎯 Checkpoint : patcher et keygenner l'application .NET fournie](/32-analyse-dynamique-dotnet/checkpoint.md)
