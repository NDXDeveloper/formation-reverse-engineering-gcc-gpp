🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 34

## Analyser un binaire Go strippé, retrouver les fonctions et reconstruire la logique

> ⚠️ **Spoilers** — Ne consultez ce document qu'après avoir tenté le checkpoint par vous-même.

---

## Objectif 1 — Triage et identification

### Version du compilateur

```bash
$ strings crackme_go_strip | grep -oP 'go1\.\d+\.\d+'
go1.22.1
```

La version exacte dépend de votre installation. L'important est de confirmer ≥ 1.17, ce qui signifie ABI register-based (section 34.2).

### Caractéristiques du binaire

```bash
$ file crackme_go_strip
crackme_go_strip: ELF 64-bit LSB executable, x86-64, version 1 (SYSV),  
statically linked, Go BuildID=..., stripped  

$ ls -lh crackme_go_strip
-rwxr-xr-x 1 user user 1.8M ... crackme_go_strip

$ ldd crackme_go_strip
        not a dynamic executable

$ checksec --file=crackme_go_strip
    Arch:       amd64-64-little
    RELRO:      No RELRO
    Stack:      No canary found
    NX:         NX enabled
    PIE:        No PIE
```

**Résumé de triage** : binaire ELF 64 bits, lié statiquement (typique de Go), strippé (pas de `.symtab` ni DWARF), environ 1.8 Mo pour un programme simple (runtime Go embarqué). NX activé, pas de PIE ni de canary — protections minimales. La compilation Go ≥ 1.17 implique l'ABI register-based.

### Chaînes pertinentes

```bash
$ strings -n 6 crackme_go_strip | grep -v '/usr/local/go' \
    | grep -v 'runtime\.' | grep -v 'internal/' \
    | grep -iE 'valid|check|bravo|usage|format|key|ordre'
```

Les chaînes sont fusionnées dans `.rodata` (section 34.5), mais un filtrage révèle des fragments comme `checksum`, `Bravo, reverser`, `format invalide`, `XXXX-XXXX`, qui confirment un programme de validation de clé. On repère aussi `contrainte d'ordre` et `vérification croisée`, suggérant plusieurs étapes de validation.

---

## Objectif 2 — Récupération des symboles

### Extraction avec GoReSym

```bash
$ GoReSym -t -d -p crackme_go_strip > metadata.json

$ jq '.TabMeta' metadata.json
{
  "VA": ...,
  "Version": "1.20",
  "Endian": "LittleEndian",
  "CpuQuantum": 1,
  "CpuWordSize": 8
}

$ jq '.UserFunctions | length' metadata.json
11

$ jq '[.UserFunctions[], .StdFunctions[]] | length' metadata.json
2341
```

11 fonctions utilisateur sur environ 2 300 fonctions totales — le runtime et la stdlib représentent plus de 99 % du binaire.

### Fonctions du package `main`

```bash
$ jq -r '.UserFunctions[] | select(.PackageName=="main")
    | "\(.FullName)\t0x\(.Start | tostring)"' metadata.json
```

| Fonction | Adresse (exemple) |  
|---|---|  
| `main.main` | `0x497A00` |  
| `main.parseKey` | `0x497520` |  
| `main.hexVal` | `0x4976E0` |  
| `main.validateGroups` | `0x497740` |  
| `main.validateGroups.func1` | `0x497880` |  
| `main.validateGroups.func2` | `0x4978E0` |  
| `main.validateCross` | `0x497900` |  
| `main.validateOrder` | `0x497960` |  
| `main.(*ChecksumValidator).Validate` | `0x4979C0` |

Les adresses varient selon la version du compilateur et l'environnement. Les fonctions `.func1` et `.func2` sont les closures anonymes lancées comme goroutines dans `validateGroups` (section 34.2) : `.func1` exécute la validation d'un groupe, `.func2` attend la fin de toutes les goroutines puis ferme le channel.

---

## Objectif 3 — Import dans Ghidra et identification de l'ABI

### Import des symboles

Exécutez le script `apply_goresym.py` (section 34.4) dans Ghidra. Après exécution, le Symbol Tree affiche les noms Go dans le Listing.

### Identification de l'ABI

Examinons le prologue de `main.hexVal` (petite fonction, facile à lire) :

```asm
main.hexVal:
    CMP     RSP, [R14+0x10]        ; vérification de pile (g.stackguard0)
    JBE     .morestack
    SUB     RSP, 0x10
    MOV     [RSP+0x08], RBP
    LEA     RBP, [RSP+0x08]
    ; Le premier argument est dans RAX (pas sur la pile)
    MOVZX   ECX, AL                ; utilise AL = octet bas de RAX
    CMP     ECX, 0x30              ; compare avec '0'
    ...
```

**Preuves de l'ABI register-based** :

1. `R14` est utilisé comme pointeur de goroutine `g` → Go ≥ 1.17.  
2. Le premier argument est lu depuis `RAX` (`AL`), pas depuis `[RSP+offset]` → convention registres.  
3. Le préambule `CMP RSP, [R14+0x10]; JBE` est le stack growth check caractéristique de Go.

**Conclusion** : ABI register-based, registres d'arguments `RAX`, `RBX`, `RCX`, `RDI`, `RSI`, `R8`–`R11` (section 34.2).

---

## Objectif 4 — Reconstruction des types

### Extraction des types avec GoReSym

```bash
$ jq '.Types[] | select(.PackageName=="main")' metadata.json
```

Types reconstruits :

```go
// --- Interface ---
type Validator interface {
    Validate(group []byte, index int) bool
}

// --- Structs ---
type ChecksumValidator struct {
    ExpectedSums map[int]uint16    // offset +0x00, 8 octets (pointeur vers hmap)
}

type validationResult struct {
    Index int                      // offset +0x00, 8 octets
    OK    bool                     // offset +0x08, 1 octet
    // padding 7 octets → taille totale : 16 octets
}
```

`ChecksumValidator` est le seul type qui implémente l'interface `Validator`. L'itab correspondante lie `*ChecksumValidator` à `Validator`, avec `fun[0]` pointant vers `main.(*ChecksumValidator).Validate`.

### Structures de données dynamiques identifiées

En cherchant les appels runtime dans le code des fonctions `main.*` :

| Appel runtime | Localisation | Structure de données |  
|---|---|---|  
| `runtime.makemap` | `main.main` | `map[int]uint16` (expectedSums) |  
| `runtime.makechan` | `main.validateGroups` | `chan validationResult` (buffered, cap=4) |  
| `runtime.chansend1` | `main.validateGroups.func1` | envoi sur le channel |  
| `runtime.chanrecv1` | `main.validateGroups` | réception depuis le channel |  
| `runtime.newproc` | `main.validateGroups` | lancement de goroutines (×4, plus la goroutine de fermeture) |  
| `runtime.growslice` | `main.parseKey` | construction du slice `[][2]byte` |

---

## Objectif 5 — Analyse de la logique de validation

### Graphe d'appels depuis `main.main`

```
main.main
 ├─► main.parseKey            (parsing de la clé)
 ├─► runtime.makemap          (construction de expectedSums)
 ├─► main.validateGroups      (étape 1 : checksum par groupe)
 │    ├─► runtime.makechan    (channel buffered de capacité 4)
 │    ├─► runtime.newproc ×4  (lance 4 goroutines de validation)
 │    │    └─► main.(*ChecksumValidator).Validate  (via dispatch itab)
 │    ├─► runtime.newproc     (goroutine de fermeture du channel)
 │    └─► runtime.chanrecv1   (boucle de collecte des résultats)
 ├─► main.validateOrder       (étape 2 : ordre croissant)
 └─► main.validateCross       (étape 3 : XOR global)
```

### Étape 1 — Checksum par groupe (`validateGroups` → `ChecksumValidator.Validate`)

En décompilant `main.(*ChecksumValidator).Validate` :

```
Pour chaque octet b du groupe (2 octets) :
    xored = b XOR magic[i % 4]
    sum += xored

Comparer sum avec ExpectedSums[index_du_groupe]
```

**Constantes extraites :**

Le tableau `magic` est trouvable dans `.rodata` ou en posant un breakpoint sur la fonction Validate :

```bash
# Dans GDB — breakpoint sur Validate, inspecter les accès mémoire
break main.(*ChecksumValidator).Validate  
run 1111-2222-3333-4444  
# Pas à pas, observer les octets lus pour le XOR
```

```
magic = { 0xDE, 0xAD, 0xC0, 0xDE }
```

Puisque chaque groupe fait 2 octets, seuls `magic[0] = 0xDE` et `magic[1] = 0xAD` sont utilisés.

La map `expectedSums` est construite dans `main.main` avec des appels à `runtime.mapassign`. En posant un breakpoint sur `runtime.mapassign_fast64` et en inspectant les arguments :

```
expectedSums = {
    0: 0x010E   (270)
    1: 0x0122   (290)
    2: 0x0136   (310)
    3: 0x013E   (318)
}
```

**Formule** : pour le groupe `i` avec octets `(g0, g1)` :

```
(g0 XOR 0xDE) + (g1 XOR 0xAD) == expectedSums[i]
```

### Étape 2 — Ordre croissant (`validateOrder`)

Décompilation de `main.validateOrder` :

```
Pour chaque groupe (i=0 à 3) :
    val = g[0]  (premier octet du groupe)
    Si val <= prev : retourner false
    prev = val

Retourner true
```

**Contrainte** : le premier octet de chaque groupe doit être strictement croissant.

```
g0[0] < g1[0] < g2[0] < g3[0]
```

### Étape 3 — XOR croisé (`validateCross`)

Décompilation de `main.validateCross` :

```
globalXOR = 0  
Pour chaque groupe :  
    Pour chaque octet b du groupe :
        globalXOR ^= b

Retourner globalXOR == 0x42
```

**Contrainte** : le XOR de l'ensemble des 8 octets de la clé doit valoir `0x42`.

---

## Objectif 6 — Produire une clé valide

### Résolution manuelle

On cherche 4 groupes de 2 octets `(g0, g1)` vérifiant simultanément :

**Contrainte C1** (checksum) — pour chaque groupe `i` :

```
(gi[0] ⊕ 0xDE) + (gi[1] ⊕ 0xAD) = T[i]
avec T = {270, 290, 310, 318}
```

**Contrainte C2** (ordre) :

```
g0[0] < g1[0] < g2[0] < g3[0]
```

**Contrainte C3** (XOR global) :

```
g0[0] ⊕ g0[1] ⊕ g1[0] ⊕ g1[1] ⊕ g2[0] ⊕ g2[1] ⊕ g3[0] ⊕ g3[1] = 0x42
```

**Démarche** : pour chaque groupe, on pose `a = gi[0] ⊕ 0xDE` et `b = gi[1] ⊕ 0xAD`, avec `a + b = T[i]`. On choisit `a` librement (0 ≤ a ≤ min(T[i], 255), et `b = T[i] − a` ≤ 255), puis on calcule `gi[0] = a ⊕ 0xDE` et `gi[1] = b ⊕ 0xAD`.

On résout d'abord C1 + C2 en choisissant des valeurs de `a` qui produisent des premiers octets croissants, puis on ajuste un seul octet pour satisfaire C3.

| Groupe | T | a choisie | b = T−a | g[0] = a⊕0xDE | g[1] = b⊕0xAD | g[0] décimal |  
|---|---|---|---|---|---|---|  
| 0 | 270 | 200 | 70 | 0x16 | 0xEB | 22 |  
| 1 | 290 | 230 | 60 | 0x38 | 0x91 | 56 |  
| 2 | 310 | 135 | 175 | 0x59 | 0x02 | 89 |  
| 3 | 318 | 160 | 158 | 0x7E | 0x33 | 126 |

**Vérification C2** : 22 < 56 < 89 < 126 ✓

**Vérification C3** :

```
0x16 ⊕ 0xEB = 0xFD
0xFD ⊕ 0x38 = 0xC5
0xC5 ⊕ 0x91 = 0x54
0x54 ⊕ 0x59 = 0x0D
0x0D ⊕ 0x02 = 0x0F
0x0F ⊕ 0x7E = 0x71
0x71 ⊕ 0x33 = 0x42 ✓
```

### Clé valide

```
16EB-3891-5902-7E33
```

```bash
$ ./crackme_go_strip 16EB-3891-5902-7E33

   ╔══════════════════════════════════════════╗
   ║   crackme_go — Chapitre 34               ║
   ║   Formation Reverse Engineering GNU      ║
   ╚══════════════════════════════════════════╝

[*] Vérification de la clé : 16EB-3891-5902-7E33
[✓] Checksums de groupes valides.
[✓] Contrainte d'ordre respectée.
[✓] Vérification croisée OK.

══════════════════════════════════════
  🎉  Clé valide ! Bravo, reverser !
══════════════════════════════════════
```

### Keygen Python

```python
#!/usr/bin/env python3
"""
Keygen pour crackme_go — Chapitre 34  
Résout les trois contraintes par recherche exhaustive sur les degrés  
de liberté (un paramètre 'a' par groupe).  
"""

import random

MAGIC_0 = 0xDE  
MAGIC_1 = 0xAD  
TARGETS = {0: 270, 1: 290, 2: 310, 3: 318}  
CROSS_XOR = 0x42  

def solve():
    # Pour chaque groupe, énumérer les paires (g0, g1) valides pour C1
    candidates = {}
    for idx, target in TARGETS.items():
        candidates[idx] = []
        for a in range(max(0, target - 255), min(target, 255) + 1):
            b = target - a
            g0 = a ^ MAGIC_0
            g1 = b ^ MAGIC_1
            candidates[idx].append((g0, g1))

    # Chercher une combinaison vérifiant C2 (ordre) et C3 (XOR global)
    solutions = []
    for c0 in candidates[0]:
        for c1 in candidates[1]:
            if c1[0] <= c0[0]:        # C2 : premier octet croissant
                continue
            for c2 in candidates[2]:
                if c2[0] <= c1[0]:
                    continue
                for c3 in candidates[3]:
                    if c3[0] <= c2[0]:
                        continue
                    # C3 : XOR global
                    xor = 0
                    for g0, g1 in [c0, c1, c2, c3]:
                        xor ^= g0
                        xor ^= g1
                    if xor == CROSS_XOR:
                        solutions.append((c0, c1, c2, c3))

    return solutions

def format_key(groups):
    parts = []
    for g0, g1 in groups:
        parts.append(f"{g0:02X}{g1:02X}")
    return "-".join(parts)

if __name__ == "__main__":
    solutions = solve()
    print(f"[*] {len(solutions)} clé(s) valide(s) trouvée(s).\n")

    if solutions:
        # Afficher quelques exemples
        shown = random.sample(solutions, min(5, len(solutions)))
        for groups in shown:
            print(f"    {format_key(groups)}")

        print(f"\n[*] Clé de référence : {format_key(solutions[0])}")
```

Ce keygen par force brute est quasi instantané : l'espace de recherche effectif est très réduit grâce à l'élagage par C2 (ordre croissant des premiers octets).

---

## Récapitulatif des compétences validées

| Objectif | Sections mobilisées | Compétence clé |  
|---|---|---|  
| 1 — Triage | 34.1, 34.5 | Identifier un binaire Go strippé et filtrer le bruit |  
| 2 — Symboles | 34.4 | Extraire les fonctions via `gopclntab` / GoReSym |  
| 3 — Ghidra + ABI | 34.2, 34.4 | Distinguer l'ABI Go de System V, repérer R14 et le stack check |  
| 4 — Types | 34.3, 34.6 | Reconstruire structs, interfaces et identifier les structures runtime |  
| 5 — Logique | 34.1–34.5 | Tracer le graphe d'appels, identifier les goroutines et les contraintes |  
| 6 — Keygen | Synthèse | Modéliser les contraintes et produire une clé valide |

⏭️
