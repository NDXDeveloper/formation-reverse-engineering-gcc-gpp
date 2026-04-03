🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.8 — Écriture d'un keygen en Python avec `pwntools`

> 📖 **Rappel** : l'utilisation de `pwntools` pour interagir avec un binaire (tubes, `process`, `send`/`recv`) est introduite au chapitre 11, section 11.9. Cette section suppose que le module est installé (`pip install pwntools`) et que vous avez déjà lancé un script basique.

---

## Introduction

Les sections précédentes ont progressé du passif vers l'intrusif : triage, analyse statique, analyse dynamique, patching, exécution symbolique. Chaque technique a produit un résultat — mais aucune n'a exigé de *comprendre* l'algorithme de vérification dans son intégralité.

L'écriture d'un keygen est le point d'aboutissement du reverse engineering d'un crackme. Un keygen (key generator) est un programme indépendant qui, pour n'importe quel username donné, calcule et produit une clé de licence valide. Il ne contourne pas la vérification — il la **satisfait**. Pour y parvenir, il faut avoir reconstruit l'algorithme complet : le hachage du username, la dérivation des groupes, et le formatage de la clé.

C'est l'exercice le plus exigeant du chapitre, mais aussi le plus gratifiant : le keygen prouve que l'on a compris le binaire de bout en bout, au point de pouvoir en reproduire la logique dans un autre langage.

---

## Rappel de l'algorithme à reproduire

L'analyse statique dans Ghidra (section 21.3) et la confirmation dynamique dans GDB (section 21.5) ont révélé la chaîne de traitement suivante à l'intérieur de `check_license` :

```
username (chaîne)
    │
    ▼
compute_hash(username) → hash (uint32)
    │
    ▼
derive_key(hash, groups) → groups[4] (uint16 × 4)
    │
    ▼
format_key(groups, expected) → "XXXX-XXXX-XXXX-XXXX"
    │
    ▼
strcmp(expected, user_key) → 0 si match
```

Le keygen doit reproduire les trois premières étapes. La quatrième (`strcmp`) est la vérification elle-même — le keygen n'a pas à comparer, il produit directement la clé attendue.

---

## Étape 1 — Reconstruire `compute_hash`

### Pseudo-C de Ghidra

Le decompiler de Ghidra produit un pseudo-C semblable à celui-ci pour `compute_hash` :

```c
uint32_t compute_hash(char *username)
{
    uint32_t h = 0x5A3C6E2D;
    size_t len = strlen(username);

    for (size_t i = 0; i < len; i++) {
        h += (uint32_t)username[i];
        h *= 0x1003F;
        h = (h << (username[i] & 0xF)) | (h >> (32 - (username[i] & 0xF)));
        h ^= 0xDEADBEEF;
    }

    h ^= (h >> 16);
    h *= 0x45D9F3B;
    h ^= (h >> 16);

    return h;
}
```

Plusieurs éléments doivent être identifiés et traduits :

**Constantes** — `0x5A3C6E2D` (seed initial), `0x1003F` (multiplicateur), `0xDEADBEEF` (masque XOR), `0x45D9F3B` (multiplicateur d'avalanche). Ces constantes sont visibles directement dans le désassemblage comme des opérandes immédiats (`mov reg, 0x5A3C6E2D`). Elles sont le premier repère pour identifier la fonction de hachage dans un binaire inconnu.

**Rotation à gauche** — L'expression `(h << n) | (h >> (32 - n))` est le pattern classique de la rotation à gauche sur 32 bits. Dans le désassemblage, GCC peut émettre un `ROL` direct (si le compteur est dans `CL`) ou le couple `SHL`/`SHR`/`OR`. Le decompiler de Ghidra reconnaît souvent ce pattern et le restitue sous forme compacte — mais pas toujours. Si le pseudo-C affiche deux shifts et un OR, c'est une rotation.

**Troncature à 32 bits** — En C, les opérations sur `uint32_t` tronquent automatiquement à 32 bits. En Python, les entiers n'ont pas de taille fixe — il faut masquer manuellement avec `& 0xFFFFFFFF` après chaque opération qui peut déborder.

### Traduction en Python

```python
import struct

def rotate_left_32(value, count):
    """Rotation à gauche sur 32 bits."""
    count &= 31
    return ((value << count) | (value >> (32 - count))) & 0xFFFFFFFF

def compute_hash(username: bytes) -> int:
    """Reproduit la fonction compute_hash du keygenme."""
    SEED = 0x5A3C6E2D
    MUL  = 0x1003F
    XOR  = 0xDEADBEEF

    h = SEED

    for byte in username:
        h = (h + byte) & 0xFFFFFFFF
        h = (h * MUL) & 0xFFFFFFFF
        h = rotate_left_32(h, byte & 0x0F)
        h ^= XOR

    # Avalanche final
    h ^= (h >> 16)
    h = (h * 0x45D9F3B) & 0xFFFFFFFF
    h ^= (h >> 16)

    return h
```

### Vérification croisée

La première chose à faire après avoir traduit une fonction est de **vérifier** qu'elle produit le même résultat que le binaire. On utilise la clé capturée en GDB (section 21.5) comme référence :

```python
h = compute_hash(b"Alice")  
print(f"Hash de 'Alice' : 0x{h:08X}")  
```

Si le hash est correct, les étapes suivantes (`derive_key`, `format_key`) produiront la bonne clé. Si le hash diffère, l'erreur est dans la traduction — il faut relire le désassemblage instruction par instruction.

> 💡 **Technique de débogage** : en cas de divergence, on peut comparer les valeurs intermédiaires. Dans GDB, poser un breakpoint au début de la boucle dans `compute_hash` et afficher `h` à chaque itération avec un breakpoint conditionnel commandé. En Python, ajouter un `print(f"i={i} h=0x{h:08X}")` dans la boucle. La première itération qui diverge pointe vers l'instruction mal traduite.

### Pièges courants de la traduction

**Signe des entiers** — En C, `uint32_t` est non signé. En Python, les entiers sont signés et de taille arbitraire. Le masquage `& 0xFFFFFFFF` est indispensable après chaque multiplication et addition pour simuler le débordement non signé sur 32 bits. Oublier un seul masquage peut produire un hash totalement différent à cause de l'effet cascade.

**Ordre des octets** — `username[i]` en C accède au i-ème octet de la chaîne. En Python, itérer sur un `bytes` (`for byte in username`) produit des entiers 0-255 — c'est le comportement correct. Attention à ne pas itérer sur un `str` Python, qui produirait des caractères Unicode (points de code potentiellement > 255).

**Rotation avec count = 0** — Quand `username[i] & 0x0F` vaut 0, la rotation ne fait rien. L'expression `h >> (32 - 0)` est un shift de 32 bits, ce qui est un comportement indéfini en C sur les types 32 bits. En pratique, GCC le compile souvent en un shift de 0 (pas de changement), mais en Python `h >> 32` donne 0. La garde `count &= 31` dans `rotate_left_32` protège contre ce cas : si `count` vaut 0, les deux shifts sont de 0 bits et le résultat est `h` inchangé.

---

## Étape 2 — Reconstruire `derive_key`

### Pseudo-C de Ghidra

```c
void derive_key(uint32_t hash, uint16_t groups[4])
{
    groups[0] = (uint16_t)((hash & 0xFFFF) ^ 0xA5A5);
    groups[1] = (uint16_t)(((hash >> 16) & 0xFFFF) ^ 0x5A5A);
    groups[2] = (uint16_t)((rotate_left(hash, 7) & 0xFFFF) ^ 0x1234);
    groups[3] = (uint16_t)((rotate_left(hash, 13) & 0xFFFF) ^ 0xFEDC);
}
```

Les quatre groupes sont des transformations indépendantes du hash, chacune combinant une extraction de 16 bits (masquage ou rotation + masquage) avec un XOR par une constante.

### Traduction en Python

```python
def derive_key(hash_val: int) -> list[int]:
    """Dérive 4 groupes de 16 bits depuis le hash."""
    groups = [
        (hash_val & 0xFFFF) ^ 0xA5A5,
        ((hash_val >> 16) & 0xFFFF) ^ 0x5A5A,
        (rotate_left_32(hash_val, 7) & 0xFFFF) ^ 0x1234,
        (rotate_left_32(hash_val, 13) & 0xFFFF) ^ 0xFEDC,
    ]
    return groups
```

Les constantes `0xA5A5`, `0x5A5A`, `0x1234`, `0xFEDC` sont visibles directement dans le désassemblage comme opérandes immédiats d'instructions `XOR`. Elles sont un repère fiable pour identifier cette fonction, même dans un binaire strippé.

---

## Étape 3 — Reconstruire `format_key`

### Pseudo-C de Ghidra

```c
void format_key(uint16_t groups[4], char *out)
{
    snprintf(out, 20, "%04X-%04X-%04X-%04X",
             groups[0], groups[1], groups[2], groups[3]);
}
```

La chaîne de format `"%04X-%04X-%04X-%04X"` a été repérée dès le triage par `strings` (section 21.1). Chaque groupe de 16 bits est affiché en hexadécimal majuscule, padé à 4 chiffres.

### Traduction en Python

```python
def format_key(groups: list[int]) -> str:
    """Formate les 4 groupes en clé XXXX-XXXX-XXXX-XXXX."""
    return "{:04X}-{:04X}-{:04X}-{:04X}".format(*groups)
```

---

## Étape 4 — Assembler le keygen

### Version standalone

En combinant les trois fonctions, on obtient un keygen minimal :

```python
#!/usr/bin/env python3
"""
keygen_keygenme.py — Générateur de clés pour le keygenme (chapitre 21).

Usage : python3 keygen_keygenme.py <username>
"""

import sys

# ── Fonctions reconstruites depuis le désassemblage ──────────

def rotate_left_32(value, count):
    count &= 31
    return ((value << count) | (value >> (32 - count))) & 0xFFFFFFFF

def compute_hash(username: bytes) -> int:
    SEED = 0x5A3C6E2D
    MUL  = 0x1003F
    XOR  = 0xDEADBEEF

    h = SEED
    for byte in username:
        h = (h + byte) & 0xFFFFFFFF
        h = (h * MUL) & 0xFFFFFFFF
        h = rotate_left_32(h, byte & 0x0F)
        h ^= XOR

    h ^= (h >> 16)
    h = (h * 0x45D9F3B) & 0xFFFFFFFF
    h ^= (h >> 16)
    return h

def derive_key(hash_val: int) -> list[int]:
    return [
        (hash_val & 0xFFFF) ^ 0xA5A5,
        ((hash_val >> 16) & 0xFFFF) ^ 0x5A5A,
        (rotate_left_32(hash_val, 7) & 0xFFFF) ^ 0x1234,
        (rotate_left_32(hash_val, 13) & 0xFFFF) ^ 0xFEDC,
    ]

def format_key(groups: list[int]) -> str:
    return "{:04X}-{:04X}-{:04X}-{:04X}".format(*groups)

def keygen(username: str) -> str:
    """Génère une clé valide pour le username donné."""
    h = compute_hash(username.encode())
    groups = derive_key(h)
    return format_key(groups)

# ── Point d'entrée ───────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage : {sys.argv[0]} <username>")
        print(f"  Le username doit faire entre 3 et 31 caractères.")
        sys.exit(1)

    username = sys.argv[1]

    if len(username) < 3 or len(username) > 31:
        print("[-] Le username doit faire entre 3 et 31 caractères.")
        sys.exit(1)

    key = keygen(username)
    print(f"[+] Username : {username}")
    print(f"[+] License  : {key}")
```

### Exécution

```bash
$ python3 keygen_keygenme.py Alice
[+] Username : Alice
[+] License  : DCEB-0DFC-B51F-3428

$ python3 keygen_keygenme.py Bob
[+] Username : Bob
[+] License  : 679E-0910-0F9D-94B5

$ python3 keygen_keygenme.py ReverseEngineer
[+] Username : ReverseEngineer
[+] License  : 6865-6B66-F22C-F8FB
```

Le keygen produit instantanément une clé pour n'importe quel username.

---

## Étape 5 — Validation automatisée avec `pwntools`

Le keygen standalone génère des clés, mais la validation reste manuelle : il faut copier-coller la clé dans le programme. Avec `pwntools`, on peut automatiser l'intégralité du processus — générer la clé *et* la soumettre au binaire pour vérifier qu'elle est acceptée.

### Script de validation

```python
#!/usr/bin/env python3
"""
validate_keygen.py — Génère une clé et la soumet automatiquement  
au binaire pour validation.  

Usage : python3 validate_keygen.py [username]
"""

from pwn import *  
import sys  

# Importer le keygen (même répertoire)
from keygen_keygenme import keygen

# ── Configuration ────────────────────────────────────────────
BINARY = "./keygenme_O0"  
USERNAME = sys.argv[1] if len(sys.argv) > 1 else "Alice"  

# ── Génération de la clé ─────────────────────────────────────
key = keygen(USERNAME)  
log.info(f"Username : {USERNAME}")  
log.info(f"Clé générée : {key}")  

# ── Interaction avec le binaire ──────────────────────────────
p = process(BINARY)

# Attendre le prompt du username
p.recvuntil(b"Enter username: ")  
p.sendline(USERNAME.encode())  

# Attendre le prompt de la clé
p.recvuntil(b"Enter license key")  
p.recvuntil(b": ")  
p.sendline(key.encode())  

# Lire la réponse
response = p.recvall(timeout=2).decode().strip()  
p.close()  

# ── Vérification ─────────────────────────────────────────────
if "Valid license" in response:
    log.success(f"Validation réussie : {response}")
else:
    log.failure(f"Validation échouée : {response}")
    sys.exit(1)
```

### Exécution

```bash
$ python3 validate_keygen.py Alice
[*] Username : Alice
[*] Clé générée : DCEB-0DFC-B51F-3428
[+] Starting local process './keygenme_O0'
[+] Receiving all data: Done (36B)
[*] Stopped process './keygenme_O0' (pid 12345)
[+] Validation réussie : [+] Valid license! Welcome, Alice.
```

### Test en masse

Pour valider le keygen de façon exhaustive, on peut tester des centaines de usernames automatiquement :

```python
#!/usr/bin/env python3
"""
batch_validate.py — Test en masse du keygen sur N usernames aléatoires.
"""

from pwn import *  
import random  
import string  

from keygen_keygenme import keygen

BINARY = "./keygenme_O0"  
NUM_TESTS = 50  

context.log_level = "error"  # Silencer pwntools pour le batch

def random_username(min_len=3, max_len=20):
    length = random.randint(min_len, max_len)
    return "".join(random.choices(string.ascii_letters + string.digits, k=length))

passed = 0  
failed = 0  

for i in range(NUM_TESTS):
    username = random_username()
    key = keygen(username)

    p = process(BINARY)
    p.recvuntil(b"Enter username: ")
    p.sendline(username.encode())
    p.recvuntil(b": ")
    p.sendline(key.encode())

    response = p.recvall(timeout=2).decode()
    p.close()

    if "Valid license" in response:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: username='{username}' key='{key}'")
        print(f"        response: {response.strip()}")

print(f"\nRésultat : {passed}/{NUM_TESTS} validés, {failed} échecs.")
```

```bash
$ python3 batch_validate.py
Résultat : 50/50 validés, 0 échecs.
```

Un score de 100% sur 50 usernames aléatoires confirme que le keygen est correct. Si un seul test échoue, c'est qu'il y a une erreur dans la traduction — le username qui provoque l'échec est un cas de test précieux pour déboguer.

---

## Étape 6 — Tester sur les variantes optimisées

Le keygen reproduit l'algorithme du code source — il ne dépend pas du niveau d'optimisation du binaire. La même clé est valide pour `keygenme_O0`, `keygenme_O2`, `keygenme_O3`, `keygenme_strip` et `keygenme_O2_strip`, car l'algorithme est identique dans les cinq variantes. Seule la représentation en assembleur change.

On peut le vérifier facilement :

```python
for binary in ["./keygenme_O0", "./keygenme_O2", "./keygenme_O3",
               "./keygenme_strip", "./keygenme_O2_strip"]:
    p = process(binary)
    p.recvuntil(b"Enter username: ")
    p.sendline(b"Alice")
    p.recvuntil(b": ")
    p.sendline(keygen("Alice").encode())
    response = p.recvall(timeout=2).decode()
    p.close()

    status = "OK" if "Valid license" in response else "FAIL"
    print(f"  [{status}] {binary}")
```

```
  [OK] ./keygenme_O0
  [OK] ./keygenme_O2
  [OK] ./keygenme_O3
  [OK] ./keygenme_strip
  [OK] ./keygenme_O2_strip
```

Ce test confirme un principe fondamental : **l'optimisation et le stripping ne modifient pas la sémantique du programme**. Ils rendent le RE plus difficile pour l'analyste humain, mais le comportement observable (entrées/sorties) reste identique.

---

## Anatomie de la reconstruction : du désassemblage au Python

Pour résumer la méthodologie de traduction qui sous-tend tout ce keygen, voici le processus général applicable à n'importe quelle fonction :

### 1. Lire le pseudo-C de Ghidra

Le decompiler fournit un squelette en C. Ce squelette est souvent imparfait : noms de variables génériques, types approximatifs (`undefined4` au lieu de `uint32_t`), casts superflus. Mais la structure (boucles, conditions, appels) est correcte dans la grande majorité des cas.

### 2. Croiser avec le désassemblage

Quand le pseudo-C est ambigu, revenir au Listing assembleur. L'assembleur ne ment pas — c'est la vérité du binaire. Le pseudo-C est une *interprétation* du désassembleur, susceptible d'erreurs de décompilation.

Cas typiques où le désassemblage tranche :
- **Taille des opérandes** : le pseudo-C affiche `int`, mais le désassemblage montre `EAX` (32 bits) vs `RAX` (64 bits) vs `AX` (16 bits). La taille exacte compte pour les masquages et les débordements.  
- **Signedness** : le pseudo-C peut hésiter entre `int` et `uint`. Le choix entre `IMUL` (signé) et `MUL` (non signé), ou entre `SAR` (shift arithmétique) et `SHR` (shift logique), tranche la question.  
- **Rotations** : le decompiler ne reconnaît pas toujours le pattern `(x << n) | (x >> (32-n))` comme une rotation. Il peut afficher deux opérations séparées.

### 3. Traduire en Python

La traduction est mécanique une fois que le C est correct :
- `uint32_t` → `int` Python + masquage `& 0xFFFFFFFF` après chaque opération.  
- `uint16_t` → `int` Python + masquage `& 0xFFFF`.  
- `for (size_t i = 0; i < len; i++)` → `for byte in username:` (itération sur `bytes`).  
- `h << n | h >> (32-n)` → fonction `rotate_left_32` dédiée.  
- `snprintf(buf, 20, "%04X-...", ...)` → `"{:04X}-...".format(...)`.

### 4. Valider par comparaison

Générer une clé pour un username connu et comparer avec la valeur capturée en GDB (section 21.5). Si les valeurs correspondent, la traduction est correcte. Sinon, déboguer en comparant les valeurs intermédiaires (hash avant et après chaque itération de boucle).

---

## Quand la traduction directe échoue

Sur des binaires plus complexes que notre keygenme pédagogique, la reconstruction de l'algorithme peut se heurter à plusieurs obstacles :

### Fonctions inlinées en `-O2`/`-O3`

Si `compute_hash` a été inliné dans `check_license`, on ne voit plus une fonction séparée — juste un bloc de code au milieu de `check_license`. La logique est la même, mais les frontières sont floues. Le travail du reverse engineer est de reconnaître les patterns (boucle, constantes, rotation) et de les isoler mentalement.

### Bibliothèques crypto standard

Si le binaire utilise OpenSSL ou libsodium pour le hachage (au lieu d'un algorithme custom), il ne sert à rien de reconstruire la fonction — il suffit d'identifier quelle fonction de quelle bibliothèque est utilisée (chapitre 24) et d'appeler la même fonction en Python :

```python
import hashlib  
h = hashlib.sha256(username).hexdigest()  
```

La détection se fait via les constantes magiques (Annexe J) ou les signatures FLIRT/Ghidra (chapitre 20, section 5).

### Algorithmes irréversibles

Parfois, la vérification ne compare pas la clé à une valeur attendue, mais vérifie une *propriété* de la clé (par exemple : « le CRC32 de la clé XORé avec le hash du username doit valoir zéro »). Dans ce cas, le keygen doit résoudre une équation plutôt que reproduire un calcul. C'est le domaine du solveur Z3 utilisé manuellement (chapitre 18, section 4), ou d'angr comme vu en section 21.7.

---

## Synthèse

Le keygen est la synthèse de tout le chapitre. Chaque section a contribué une pièce du puzzle :

| Section | Contribution au keygen |  
|---|---|  
| 21.1 — Triage | Format de la clé (`XXXX-XXXX-XXXX-XXXX`), chaîne `%04X`, absence de crypto externe |  
| 21.2 — checksec | Confirmation qu'aucune protection ne bloque l'analyse |  
| 21.3 — Ghidra | Pseudo-C de `compute_hash`, `derive_key`, `format_key` — squelette de l'algorithme |  
| 21.4 — Sauts | Compréhension du prédicat de succès (`strcmp == 0`) |  
| 21.5 — GDB | Capture de la clé attendue → valeur de référence pour valider la traduction |  
| 21.6 — Patching | Non utilisé directement, mais confirmation du point de décision |  
| 21.7 — angr | Validation indépendante (la clé trouvée par angr doit correspondre) |  
| **21.8 — Keygen** | **Reconstruction complète de l'algorithme en Python** |

Le workflow de construction d'un keygen est réutilisable sur n'importe quel crackme :

```
1. Identifier la fonction de vérification (Ghidra, XREF)
         ↓
2. Lire le pseudo-C de chaque sous-fonction
         ↓
3. Croiser avec le désassemblage en cas d'ambiguïté
         ↓
4. Traduire en Python, fonction par fonction
         ↓
5. Valider chaque fonction individuellement (GDB comme oracle)
         ↓
6. Assembler le keygen complet
         ↓
7. Tester en masse avec pwntools
```

Ce chapitre 21 a parcouru le cycle complet du reverse engineering d'un programme C simple. La compétence acquise — mener une analyse de bout en bout, du triage initial au keygen fonctionnel — est la fondation sur laquelle s'appuient les chapitres suivants. Les cibles seront plus complexes (C++ orienté objet au chapitre 22, binaire réseau au chapitre 23, chiffrement au chapitre 24), mais la méthodologie reste la même : observer, comprendre, reproduire.

⏭️ [🎯 Checkpoint : produire un keygen fonctionnel pour les 3 variantes du binaire](/21-keygenme/checkpoint.md)
