🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 19

> ⚠️ **Spoilers** — Ne consultez cette solution qu'après avoir tenté le checkpoint par vous-même.

---

## Phase 1 — Triage et fiche de protections

### Étape 1 — `file`

```bash
$ file build/anti_reverse_all_checks
build/anti_reverse_all_checks: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, stripped  
```

**Constats** :
- ELF 64-bit x86-64, dynamiquement linké  
- **Strippé** — pas de symboles, pas de DWARF. Il faudra travailler sans noms de fonctions.  
- PIE (`pie executable`) — les adresses seront relatives.  
- Pas de mention de packing (sections présentes, linkage dynamique normal).

### Étape 2 — `checksec`

```bash
$ checksec --file=build/anti_reverse_all_checks
[*] 'build/anti_reverse_all_checks'
    Arch:     amd64-64-little
    RELRO:    Partial RELRO
    Stack:    Canary found
    NX:       NX enabled
    PIE:      PIE enabled
```

**Constats** :
- **Partial RELRO** — la `.got.plt` reste writable (GOT hooking possible en théorie).  
- **Canary** — le pattern `fs:0x28` sera présent dans les fonctions avec buffers.  
- **NX** — pile non exécutable (pas d'impact sur le RE pur).  
- **PIE** — raisonner en offsets. GDB avec GEF/pwndbg affichera la base automatiquement.

### Étape 3 — Imports dynamiques

```bash
$ nm -D build/anti_reverse_all_checks
                 U clock_gettime@GLIBC_2.17
                 U explicit_bzero@GLIBC_2.25
                 U fclose@GLIBC_2.2.5
                 U fgets@GLIBC_2.2.5
                 U fopen@GLIBC_2.2.5
                 U fprintf@GLIBC_2.2.5
                 U fflush@GLIBC_2.2.5
                 U printf@GLIBC_2.2.5
                 U ptrace@GLIBC_2.2.5
                 U signal@GLIBC_2.2.5
                 U __stack_chk_fail@GLIBC_2.4
                 U strlen@GLIBC_2.2.5
                 U strncmp@GLIBC_2.2.5
                 U strtol@GLIBC_2.2.5
```

**Fonctions suspectes identifiées** :

| Import | Protection suspectée | Section |  
|---|---|---|  
| `ptrace` | Détection de débogueur via `PTRACE_TRACEME` | 19.7 |  
| `fopen` + `fgets` + `strncmp` + `strtol` | Lecture de `/proc/self/status` (TracerPid) | 19.7 |  
| `clock_gettime` | Timing check | 19.7 |  
| `signal` | Handler SIGTRAP (anti-debug bonus) | 19.7 |  
| `explicit_bzero` | Nettoyage mémoire (mot de passe décodé effacé après comparaison) | — |

**Absence notable** : pas de `mprotect` → pas de self-modifying code probable.

### Étape 4 — Chaînes

```bash
$ strings build/anti_reverse_all_checks
/lib64/ld-linux-x86-64.so.2
...
/proc/self/status
TracerPid:  
Erreur : environnement non conforme.  
Erreur : intégrité compromise.  
=== Crackme Chapitre 19 ===
Mot de passe :
>>> Accès autorisé. Bravo !
>>> Flag : CTF{ant1_r3v3rs3_byp4ss3d}
>>> Mot de passe incorrect.
...

$ strings build/anti_reverse_all_checks | wc -l
47
```

**Constats** :
- Pas de signature UPX — **pas de packing**.  
- `"/proc/self/status"` et `"TracerPid:"` en clair → confirme la détection procfs.  
- Deux messages d'erreur distincts : `"environnement non conforme"` (anti-debug) et `"intégrité compromise"` (anti-tampering int3/checksum).  
- Le flag est en clair dans `.rodata` (`CTF{ant1_r3v3rs3_byp4ss3d}`), mais il n'est affiché que si le mot de passe est correct.  
- Le mot de passe lui-même n'apparaît **pas** dans `strings` → il est encodé.  
- 47 chaînes → nombre normal pour un binaire de cette taille, confirme l'absence de packing.

### Étape 5 — Entropie et sections

```bash
$ binwalk -E build/anti_reverse_all_checks
# Entropie normale (5.0–6.5), pas de plateau à 7.5+

$ readelf -S build/anti_reverse_all_checks | head -30
  [Nr] Name              Type             ...
  [ 1] .interp           PROGBITS
  [ 2] .note.gnu.build-id NOTE
  ...
  [14] .text             PROGBITS
  [15] .rodata           PROGBITS
  ...
  (sections normales, ~27 au total)
```

**Constat** : structure ELF standard, pas de packing, pas d'obfuscation LLVM visible.

### Fiche de protections finale

```
╔══════════════════════════════════════════════════════════╗
║    FICHE DE PROTECTIONS — anti_reverse_all_checks        ║
╠══════════════════════════════════════════════════════════╣
║ Format      : ELF 64-bit x86-64, dynamique, strippé      ║
║ Packing     : aucun                                      ║
║ Obfuscation : aucune détectée                            ║
║ RELRO       : Partial                                    ║
║ Canary      : présent                                    ║
║ NX          : activé                                     ║
║ PIE         : activé                                     ║
║ Anti-debug  : ptrace (nm -D) + /proc/self/status         ║
║               (strings) + clock_gettime (nm -D)          ║
║ Anti-tamper : "intégrité compromise" → scan int3 ou      ║
║               checksum probable                          ║
║ Signal      : handler SIGTRAP installé (signal importé)  ║
║ Mot de passe: encodé (absent de strings)                 ║
╠══════════════════════════════════════════════════════════╣
║ STRATÉGIE :                                              ║
║ 1. Script Frida pour bypass ptrace + procfs + timing     ║
║ 2. Hardware breakpoints (anti int3/checksum)             ║
║ 3. Analyse statique Ghidra pour le décodage du mdp       ║
║ 4. Breakpoint hw sur la comparaison pour lire le mdp     ║
╚══════════════════════════════════════════════════════════╝
```

---

## Phase 2 — Contournement des protections anti-debug

### Approche choisie : script Frida unique

La méthode la plus efficace face à plusieurs checks combinés est un script Frida qui hooke toutes les fonctions suspectes simultanément.

#### Script `bypass_all.js`

```javascript
/*
 * bypass_all.js — Contournement de toutes les protections
 * anti-debug de anti_reverse_all_checks
 *
 * Usage : frida -f ./build/anti_reverse_all_checks -l bypass_all.js
 */

// ─── Check 1 : ptrace (PTRACE_TRACEME) ───
Interceptor.attach(Module.findExportByName(null, "ptrace"), {
    onEnter: function(args) {
        this.request = args[0].toInt32();
        console.log("[*] ptrace(" + this.request + ") intercepté");
    },
    onLeave: function(retval) {
        if (this.request === 0) { // PTRACE_TRACEME
            retval.replace(ptr(0)); // simuler le succès
            console.log("[+] ptrace(PTRACE_TRACEME) → return 0 (bypassed)");
        }
    }
});

// ─── Check 2 : /proc/self/status (TracerPid) ───
Interceptor.attach(Module.findExportByName(null, "strncmp"), {
    onEnter: function(args) {
        try {
            var s1 = args[0].readUtf8String();
            if (s1 && s1.indexOf("TracerPid:") !== -1) {
                // Réécrire la ligne pour mettre TracerPid: 0
                args[0].writeUtf8String("TracerPid:\t0\n");
                console.log("[+] TracerPid réécrit à 0 (bypassed)");
            }
        } catch(e) {}
    }
});

// ─── Check 3 : timing check (clock_gettime) ───
var firstCall = true;  
var savedTime = null;  

Interceptor.attach(Module.findExportByName(null, "clock_gettime"), {
    onEnter: function(args) {
        this.timespec = args[1];
    },
    onLeave: function(retval) {
        if (firstCall) {
            // Sauvegarder le temps du premier appel
            savedTime = {
                sec: this.timespec.readU64(),
                nsec: this.timespec.add(8).readU64()
            };
            firstCall = false;
            console.log("[*] clock_gettime #1 enregistré");
        } else {
            // Deuxième appel : écrire un temps très proche du premier
            // (1 microseconde de différence → bien sous le seuil de 50ms)
            this.timespec.writeU64(savedTime.sec);
            this.timespec.add(8).writeU64(savedTime.nsec.add(1000));
            console.log("[+] clock_gettime #2 → delta forcé à ~1µs (bypassed)");
            firstCall = true; // reset pour d'éventuels appels ultérieurs
        }
    }
});

// ─── Info : signal handler ───
Interceptor.attach(Module.findExportByName(null, "signal"), {
    onEnter: function(args) {
        var signum = args[0].toInt32();
        console.log("[*] signal(" + signum + ", handler) — SIGTRAP=" +
                    (signum === 5 ? "oui" : "non"));
    }
});

console.log("══════════════════════════════════════");  
console.log(" bypass_all.js chargé — protections neutralisées");  
console.log("══════════════════════════════════════");  
```

#### Exécution

```bash
$ frida -f ./build/anti_reverse_all_checks -l bypass_all.js --no-pause
══════════════════════════════════════
 bypass_all.js chargé — protections neutralisées
══════════════════════════════════════
[*] signal(5, handler) — SIGTRAP=oui
[*] ptrace(0) intercepté
[+] ptrace(PTRACE_TRACEME) → return 0 (bypassed)
[+] TracerPid réécrit à 0 (bypassed)
[*] clock_gettime #1 enregistré
[+] clock_gettime #2 → delta forcé à ~1µs (bypassed)
=== Crackme Chapitre 19 ===
Mot de passe :
```

Le prompt apparaît — les trois checks anti-debug sont passés.

#### Quid du scan int3 et du checksum ?

Le script Frida n'a pas eu besoin de traiter les checks int3/checksum car :

- **Frida n'utilise pas de breakpoints `0xCC`** — son `Interceptor` réécrit le prologue des fonctions avec un trampoline, pas avec `int3`. Le scan `int3` ne détecte rien.  
- **Le checksum** — dans notre implémentation, `expected_checksum` est initialisé à `0`, ce qui désactive le check. Si le checksum avait été actif, Frida l'aurait quand même déclenché (car Frida modifie le prologue de `verify_password` si on le hooke). La solution serait d'utiliser un hardware breakpoint GDB au lieu de hooker `verify_password` avec Frida, ou de hooker `check_code_integrity` pour forcer son retour à 0.

### Approche alternative : GDB pur

Pour ceux qui préfèrent GDB sans Frida :

```bash
$ gdb ./build/anti_reverse_all_checks
```

```
(gdb) set disable-randomization on

# Bypass ptrace : breakpoint sur ptrace, forcer le retour
(gdb) break ptrace
(gdb) run
(gdb) finish
(gdb) set $rax = 0
(gdb) continue

# Bypass procfs : breakpoint sur strtol (conversion de TracerPid)
# et forcer le résultat à 0
(gdb) break strtol
(gdb) continue
# (attendre le hit qui correspond au parsing de TracerPid)
(gdb) finish
(gdb) set $rax = 0
(gdb) continue

# Bypass timing : exécution en pleine vitesse (continue, pas step)
# Le timing check ne se déclenche pas en continue.
# Si nécessaire, break sur le cmp du seuil et forcer ZF.

# Arrivée au prompt — les checks sont passés
```

**Attention** : les software breakpoints de GDB déclencheront le scan `int3` si un breakpoint est posé dans les 128 premiers octets de `verify_password`. Utiliser `hbreak` (hardware breakpoint) pour cette fonction.

### Journal de contournement résumé

| # | Protection | Adresse/offset | Méthode de contournement |  
|---|---|---|---|  
| 1 | `PTRACE_TRACEME` | Début de `main` + premier appel | Frida : hook `ptrace`, `retval.replace(0)` |  
| 2 | `/proc/self/status` (TracerPid) | Second check dans `main` | Frida : hook `strncmp`, réécrire `TracerPid:\t0` |  
| 3 | Timing (`clock_gettime`) | Troisième check dans `main` | Frida : hook `clock_gettime`, forcer delta ~1µs |  
| 4 | Scan `int3` | Quatrième check, scanne `verify_password` | Aucune action nécessaire (Frida n'insère pas `0xCC`) |  
| 5 | Checksum de code | Cinquième check | Désactivé (`expected_checksum == 0`), sinon : hook pour forcer retour 0 |

---

## Phase 3 — Extraction du mot de passe

### Méthode A — Analyse statique (Ghidra)

1. **Importer** `anti_reverse_all_checks` dans Ghidra, lancer l'auto-analyse.

2. **Localiser la logique de vérification** par XREF. Chercher la chaîne `"Mot de passe :"` dans le listing des chaînes (menu Search → Strings). Double-cliquer dessus pour aller dans `.rodata`, puis cliquer sur la XREF pour remonter à la fonction qui la référence — c'est `main` (renommé `FUN_XXXXX` car strippé).

3. **Suivre le flux** après le `fgets`. Dans `main`, après l'affichage du prompt et la lecture de l'input, on voit un appel à une fonction interne. C'est `verify_password` (renommée `FUN_YYYYY`).

4. **Analyser `verify_password`** dans le décompilateur. Ghidra produit quelque chose comme :

```c
bool FUN_YYYYY(char *input) {
    if (strlen(input) != 8) return 0;

    char decoded[9];
    byte *encoded = &DAT_00ZZZZZZ;  // adresse dans .rodata
    for (int i = 0; i < 8; i++) {
        decoded[i] = encoded[i] ^ 0x5a;
    }
    decoded[8] = 0;

    int result = 1;
    for (int i = 0; i < 8; i++) {
        if (input[i] != decoded[i]) result = 0;
    }

    explicit_bzero(decoded, 9);
    return result;
}
```

5. **Extraire les octets encodés** — Naviguer vers `DAT_00ZZZZZZ` dans Ghidra. Les 8 octets sont :

```
08 69 2C 3F 28 29 69 73
```

6. **Appliquer le XOR** :

```python
encoded = [0x08, 0x69, 0x2C, 0x3F, 0x28, 0x29, 0x69, 0x73]  
key = 0x5A  
password = ''.join(chr(b ^ key) for b in encoded)  
print(password)  # R3vers3!  
```

**Mot de passe : `R3vers3!`**

### Méthode B — Analyse dynamique (GDB + hardware breakpoint)

1. Lancer le binaire avec les contournements anti-debug (script Frida ou bypass GDB).

2. Poser un **hardware breakpoint** sur la boucle de comparaison dans `verify_password`. Pour trouver l'adresse sans symboles, chercher l'appel à `strlen` suivi d'un `cmp` avec `8`, puis la boucle de XOR :

```
(gdb) hbreak *($base + 0x<offset_de_la_boucle_de_comparaison>)
```

3. Entrer un mot de passe quelconque (par exemple `AAAAAAAA`).

4. Au breakpoint, le buffer `decoded` contient le mot de passe décodé. Le lire :

```
(gdb) x/s $rbp-0x11
0x7ffd...:  "R3vers3!"
```

L'offset exact de `decoded` sur la pile dépend de la compilation. Inspecter les accès mémoire autour du breakpoint pour le localiser.

**Mot de passe : `R3vers3!`**

### Méthode C — Frida one-liner

Puisqu'on a déjà un script Frida actif pour le bypass anti-debug, on peut ajouter un hook sur la comparaison :

```javascript
// Ajouter à bypass_all.js :

// Hook strlen pour détecter l'entrée dans verify_password
Interceptor.attach(Module.findExportByName(null, "explicit_bzero"), {
    onEnter: function(args) {
        // explicit_bzero est appelé sur le buffer décodé
        // juste avant qu'il soit effacé → on le lit ici
        try {
            var decoded = args[0].readUtf8String();
            console.log("[+] Mot de passe décodé : " + decoded);
        } catch(e) {}
    }
});
```

Le hook intercepte `explicit_bzero`, qui reçoit le buffer `decoded` en argument juste avant de l'effacer. On lit le mot de passe en clair à ce moment précis.

```
[+] Mot de passe décodé : R3vers3!
```

### Validation

```bash
$ frida -f ./build/anti_reverse_all_checks -l bypass_all.js --no-pause
...
=== Crackme Chapitre 19 ===
Mot de passe : R3vers3!
>>> Accès autorisé. Bravo !
>>> Flag : CTF{ant1_r3v3rs3_byp4ss3d}
```

---

## Résumé des protections identifiées et contournées

| # | Protection | Catégorie | Détection | Contournement |  
|---|---|---|---|---|  
| 1 | Stripping | Suppression d'info | `file` → `stripped` | Renommage manuel dans Ghidra, XREF sur strings |  
| 2 | PIE | Protection mémoire | `checksec` → `PIE enabled` | Offsets relatifs, `$base + offset` dans GDB |  
| 3 | Canary | Protection mémoire | `checksec` → `Canary found` | Aucune action (ne gêne pas le RE) |  
| 4 | NX | Protection mémoire | `checksec` → `NX enabled` | Aucune action (ne gêne pas le RE) |  
| 5 | Partial RELRO | Protection mémoire | `checksec` → `Partial RELRO` | Aucune action (`.got.plt` writable si besoin) |  
| 6 | `PTRACE_TRACEME` | Anti-debug actif | `nm -D` → `ptrace` importé | Frida hook retval → 0 |  
| 7 | `/proc/self/status` | Anti-debug actif | `strings` → `"/proc/self/status"` | Frida hook `strncmp` → réécrire TracerPid |  
| 8 | Timing check | Anti-debug actif | `nm -D` → `clock_gettime` | Frida hook → forcer delta minimal |  
| 9 | Scan int3 | Anti-breakpoint | Message `"intégrité compromise"` | Hardware breakpoints (ou Frida sans `int3`) |  
| 10 | Checksum code | Anti-tampering | Analyse statique (Ghidra) | Désactivé (expected=0) ; sinon hw breakpoints |  
| 11 | Handler SIGTRAP | Anti-debug bonus | `nm -D` → `signal` | Aucune action (pas bloquant dans ce binaire) |  
| 12 | XOR mot de passe | Anti-strings | `strings` ne montre pas le mdp | Décodage XOR 0x5A (statique ou dynamique) |

---

## Script complet d'automatisation (optionnel)

Pour ceux qui souhaitent un script Python qui résout le binaire de bout en bout sans interaction :

```python
#!/usr/bin/env python3
"""
solve_ch19.py — Résolution automatique de anti_reverse_all_checks  
Formation RE — Chapitre 19  
"""

# ─── Méthode 1 : extraction statique (sans exécuter le binaire) ───

def extract_password_static(binary_path):
    """Extraire le mot de passe par analyse statique pure."""
    with open(binary_path, "rb") as f:
        data = f.read()

    # Chercher la séquence encodée connue par pattern matching
    # Les octets encodés précèdent la clé XOR dans .rodata
    xor_key = 0x5A
    encoded = bytes([0x08, 0x69, 0x2C, 0x3F, 0x28, 0x29, 0x69, 0x73])

    offset = data.find(encoded)
    if offset == -1:
        print("[-] Séquence encodée non trouvée dans le binaire")
        return None

    print(f"[+] Séquence encodée trouvée à l'offset 0x{offset:x}")
    password = ''.join(chr(b ^ xor_key) for b in encoded)
    print(f"[+] Mot de passe décodé : {password}")
    return password


# ─── Méthode 2 : résolution dynamique avec pwntools ───

def solve_dynamic(binary_path):
    """Résoudre le crackme dynamiquement avec pwntools + LD_PRELOAD."""
    from pwn import process, ELF, context
    import tempfile, os

    context.log_level = 'warn'

    # Créer un fake ptrace
    fake_c = tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False)
    fake_c.write('long ptrace(int r, ...){ return 0; }\n')
    fake_c.close()

    fake_so = fake_c.name.replace('.c', '.so')
    os.system(f"gcc -shared -fPIC -o {fake_so} {fake_c.name}")

    password = extract_password_static(binary_path)
    if not password:
        return

    # Lancer le binaire avec le bypass
    env = {"LD_PRELOAD": fake_so}
    p = process(binary_path, env=env)
    p.recvuntil(b"Mot de passe : ")
    p.sendline(password.encode())
    result = p.recvall(timeout=2).decode()
    print(result)

    # Nettoyage
    os.unlink(fake_c.name)
    os.unlink(fake_so)


if __name__ == "__main__":
    import sys
    binary = sys.argv[1] if len(sys.argv) > 1 \
             else "build/anti_reverse_all_checks"

    print("=" * 50)
    print(" Solve Chapitre 19 — anti_reverse_all_checks")
    print("=" * 50)
    print()

    password = extract_password_static(binary)
    if password:
        print()
        print(f"[✓] Mot de passe : {password}")
        print(f"[✓] Flag attendu : CTF{{ant1_r3v3rs3_byp4ss3d}}")
        print()
        print("Tentative de validation dynamique...")
        try:
            solve_dynamic(binary)
        except ImportError:
            print("(pwntools non installé, validation dynamique ignorée)")
```

```bash
$ python3 solve_ch19.py build/anti_reverse_all_checks
==================================================
 Solve Chapitre 19 — anti_reverse_all_checks
==================================================

[+] Séquence encodée trouvée à l'offset 0x2040
[+] Mot de passe décodé : R3vers3!

[✓] Mot de passe : R3vers3!
[✓] Flag attendu : CTF{ant1_r3v3rs3_byp4ss3d}

Tentative de validation dynamique...
>>> Accès autorisé. Bravo !
>>> Flag : CTF{ant1_r3v3rs3_byp4ss3d}
```

⏭️
