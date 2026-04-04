🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 29

> ⚠️ **Spoilers** — Ce document contient la solution complète du checkpoint du chapitre 29. Ne le consultez qu'après avoir produit vos propres livrables.

---

## Livrable 1 — Rapport de détection

### 1.1 — `file`

```
$ file packed_sample_upx_tampered
packed_sample_upx_tampered: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux),  
statically linked, no section header  
```

**Interprétation :**

- `statically linked` — Anormal pour un programme GCC utilisant `printf`, `fgets`, `strcmp`, etc., qui sont normalement résolus dynamiquement via la libc. Le binaire a été rendu autonome par le packer.  
- `no section header` — La table des sections a été supprimée. Le fichier reste exécutable (le noyau n'utilise que les program headers), mais les outils d'analyse statique perdent toute visibilité sur la structure interne.

→ **Indice 1 ✓** (structure anormale pour un binaire GCC standard)

### 1.2 — `strings`

```
$ strings packed_sample_upx_tampered | wc -l
9

$ strings packed_sample_O2_strip | wc -l
87
```

Le binaire cible ne contient que 9 chaînes lisibles (essentiellement celles du stub) contre 87 pour la version non packée. Aucune chaîne fonctionnelle du programme (banner, messages, flag) n'est visible.

Examen des rares chaînes présentes :

```
$ strings packed_sample_upx_tampered
FKP!  
XP_0  
XP_1  
linux/x86  
...
```

On note `FKP!` et les noms `XP_0`, `XP_1` — des artefacts de l'altération des signatures UPX (`UPX!` → `FKP!`, `UPX0` → `XP_0`, `UPX1` → `XP_1`).

→ **Indice 2 ✓** (quasi-absence de chaînes)  
→ **Indice 8 ✓** (signatures altérées reconnaissables comme dérivées d'UPX)

### 1.3 — `checksec`

```
$ checksec --file=packed_sample_upx_tampered
    Arch:     amd64-64-little
    RELRO:    No RELRO
    Stack:    No canary found
    NX:       NX disabled
    PIE:      No PIE
```

Toutes les protections standard de GCC ont disparu. En comparaison, le binaire original (avant packing) avait au minimum Partial RELRO et un stack canary. La disparition simultanée de NX, canary et RELRO est caractéristique du remplacement du code compilé par un stub de packer.

→ **Indice 5 ✓** (protections absentes)

### 1.4 — `readelf -l` (program headers)

```
$ readelf -l packed_sample_upx_tampered

Elf file type is EXEC (Executable file)  
Entry point 0x4013e8  
There are 3 program headers, starting at offset 64  

Program Headers:
  Type    Offset             VirtAddr           PhysAddr
          FileSiz            MemSiz              Flags  Align
  LOAD    0x0000000000000000 0x0000000000400000 0x0000000000400000
          0x0000000000000000 0x0000000000004000  RW     0x1000
  LOAD    0x0000000000000000 0x0000000000404000 0x0000000000404000
          0x0000000000001bf4 0x0000000000001bf4  RWE    0x1000
  LOAD    0x0000000000001794 0x0000000000607794 0x0000000000607794
          0x0000000000000000 0x0000000000000000  RW     0x1000
```

Deux anomalies majeures :

- Le deuxième segment a les flags **`RWE`** (Read-Write-Execute). Un binaire normal n'a jamais de segment simultanément inscriptible et exécutable : le code est `R-X`, les données sont `RW-`. Le flag `RWE` est nécessaire au stub pour écrire le code décompressé puis l'exécuter.  
- Le premier segment a un `FileSiz` de **0** mais un `MemSiz` de `0x4000` (16 Ko). Le loader allouera 16 Ko de mémoire mais n'y copiera rien depuis le fichier — c'est la zone de destination où le stub décompressera le code original. Le ratio `MemSiz`/`FileSiz` est infini.

→ **Indice 3 ✓** (segment RWE)  
→ **Indice 4 ✓** (ratio MemSiz ≫ FileSiz)

### 1.5 — `readelf -S` (section headers)

```
$ readelf -S packed_sample_upx_tampered
There are no sections in this file.
```

La table des sections est totalement absente.

→ **Indice 2 bis ✓** (sections absentes)

### 1.6 — Entropie

```
$ python3 -c "
import math, sys  
from collections import Counter  

data = open('packed_sample_upx_tampered', 'rb').read()  
counts = Counter(data)  
length = len(data)  
ent = -sum((c/length) * math.log2(c/length) for c in counts.values())  
print(f'Taille   : {length} octets')  
print(f'Entropie : {ent:.4f} bits/octet')  
"
Taille   : 7152 octets  
Entropie : 7.6823 bits/octet  
```

L'entropie globale est de **7.68**, bien au-dessus du seuil de 7.5 qui indique des données compressées ou chiffrées. À titre de comparaison, le binaire non packé a une entropie d'environ 5.8.

→ **Indice 6 ✓** (entropie > 7.5)

On peut aussi vérifier visuellement dans ImHex (View → Data Information) : l'histogramme de distribution des octets est quasi plat.

→ **Indice 7 ✓** (distribution uniforme)

### 1.7 — Conclusion du rapport de détection

**7 indices sur 8** convergent vers un diagnostic de packing :

| # | Indice | Résultat |  
|---|--------|----------|  
| 1 | Peu de chaînes lisibles | ✓ — 9 chaînes contre 87 |  
| 2 | Sections absentes | ✓ — `no section header` |  
| 3 | Segment RWE | ✓ — deuxième LOAD avec flags RWE |  
| 4 | MemSiz ≫ FileSiz | ✓ — 0x4000 vs 0x0000 |  
| 5 | Protections absentes | ✓ — NX off, no canary, no RELRO |  
| 6 | Entropie > 7.5 | ✓ — 7.68 bits/octet |  
| 7 | Distribution uniforme | ✓ — histogramme ImHex plat |  
| 8 | Signature packer | ✓ partiel — `FKP!`, `XP_0`, `XP_1` (UPX altéré) |

**Packer identifié : UPX avec signatures altérées** (`UPX!` → `FKP!`, `UPX0` → `XP_0`, `UPX1` → `XP_1`). La commande `upx -d` échouera ; une approche par restauration des magic bytes ou par dump mémoire dynamique est nécessaire.

---

## Livrable 2 — Binaire décompressé

Deux approches sont présentées ci-dessous. Les deux sont valides.

### Approche A — Restauration des signatures + `upx -d`

On restaure les magic bytes dans une copie du binaire :

```python
#!/usr/bin/env python3
# restore_upx_magic.py

import sys, shutil

src = "packed_sample_upx_tampered"  
dst = "packed_sample_upx_fixed"  

shutil.copy2(src, dst)

data = open(dst, "rb").read()  
data = data.replace(b"FKP!", b"UPX!")  
data = data.replace(b"XP_0", b"UPX0")  
data = data.replace(b"XP_1", b"UPX1")  
data = data.replace(b"XP_2", b"UPX2")  
open(dst, "wb").write(data)  

n = data.count(b"UPX!")  
print(f"[+] Signatures restaurées ({n} occurrence(s) de UPX!)")  
print(f"[+] Fichier : {dst}")  
```

```
$ python3 restore_upx_magic.py
[+] Signatures restaurées (1 occurrence(s) de UPX!)
[+] Fichier : packed_sample_upx_fixed

$ upx -d packed_sample_upx_fixed
        File size         Ratio      Format      Name
   --------------------   ------   -----------   -----------
     18472 <-      7152   38.72%   linux/amd64   packed_sample_upx_fixed

Unpacked 1 file.

$ file packed_sample_upx_fixed
packed_sample_upx_fixed: ELF 64-bit LSB executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, ...  

$ strings packed_sample_upx_fixed | grep FLAG
FLAG{unp4ck3d_and_r3c0nstruct3d}
```

L'ELF est complet : `upx -d` restaure les sections, les program headers, et le point d'entrée original. C'est la méthode la plus propre.

### Approche B — Dump mémoire avec GDB + reconstruction LIEF

Cette approche est plus générale et fonctionne même quand la restauration des magic bytes n'est pas possible.

#### B.1 — Trouver l'OEP

```
$ gdb -q ./packed_sample_upx_tampered

gef➤ info file  
Entry point: 0x4013e8  

gef➤ # Le premier segment LOAD a vaddr=0x400000, memsz=0x4000  
gef➤ # Le code décompressé sera écrit à partir de 0x400000  
gef➤ # On pose un hbreak sur le début probable du .text  
gef➤ hbreak *0x401000  
Hardware assisted breakpoint 1 at 0x401000  

gef➤ run  
Starting program: ./packed_sample_upx_tampered  

Breakpoint 1, 0x0000000000401000 in ?? ()
```

L'OEP est **`0x401000`**. On vérifie que le code à cette adresse ressemble à du code légitime :

```
gef➤ x/5i $rip
=> 0x401000:  endbr64
   0x401004:  xor    ebp,ebp
   0x401006:  mov    r9,rdx
   0x401009:  pop    rsi
   0x40100a:  mov    rdx,rsp
```

C'est le prologue classique de `_start` (code CRT GCC). Le stub a terminé son travail.

#### B.2 — Mapper et dumper

```
gef➤ vmmap  
Start              End                Perm  Path  
0x0000000000400000 0x0000000000401000 r--p  packed_sample_upx_tampered
0x0000000000401000 0x0000000000404000 r-xp  packed_sample_upx_tampered
0x0000000000404000 0x0000000000405000 r--p  packed_sample_upx_tampered
0x0000000000405000 0x0000000000406000 rw-p  packed_sample_upx_tampered
...

gef➤ dump binary memory /tmp/hdr.bin  0x400000 0x401000  
gef➤ dump binary memory /tmp/text.bin 0x401000 0x404000  
gef➤ dump binary memory /tmp/ro.bin   0x404000 0x405000  
gef➤ dump binary memory /tmp/rw.bin   0x405000 0x406000  
```

#### B.3 — Reconstruire avec LIEF

```python
#!/usr/bin/env python3
# reconstruct_elf.py

import lief

OEP = 0x401000

regions = [
    ("/tmp/hdr.bin",  0x400000, "r--", None),
    ("/tmp/text.bin", 0x401000, "r-x", ".text"),
    ("/tmp/ro.bin",   0x404000, "r--", ".rodata"),
    ("/tmp/rw.bin",   0x405000, "rw-", ".data"),
]

elf = lief.ELF.Binary("reconstructed", lief.ELF.ELF_CLASS.CLASS64)  
elf.header.entrypoint = OEP  
elf.header.file_type  = lief.ELF.E_TYPE.EXECUTABLE  

for path, vaddr, perms, name in regions:
    data = list(open(path, "rb").read())

    seg = lief.ELF.Segment()
    seg.type             = lief.ELF.SEGMENT_TYPES.LOAD
    seg.flags            = 0
    if "r" in perms: seg.flags |= lief.ELF.SEGMENT_FLAGS.R
    if "w" in perms: seg.flags |= lief.ELF.SEGMENT_FLAGS.W
    if "x" in perms: seg.flags |= lief.ELF.SEGMENT_FLAGS.X
    seg.virtual_address  = vaddr
    seg.physical_address = vaddr
    seg.alignment        = 0x1000
    seg.content          = data
    elf.add(seg)

    if name:
        sec = lief.ELF.Section(name)
        sec.content         = data
        sec.virtual_address = vaddr
        sec.alignment       = 0x10
        if "x" in perms:
            sec.type  = lief.ELF.SECTION_TYPES.PROGBITS
            sec.flags = (lief.ELF.SECTION_FLAGS.ALLOC |
                         lief.ELF.SECTION_FLAGS.EXECINSTR)
        elif "w" in perms:
            sec.type  = lief.ELF.SECTION_TYPES.PROGBITS
            sec.flags = (lief.ELF.SECTION_FLAGS.ALLOC |
                         lief.ELF.SECTION_FLAGS.WRITE)
        else:
            sec.type  = lief.ELF.SECTION_TYPES.PROGBITS
            sec.flags = lief.ELF.SECTION_FLAGS.ALLOC
        elf.add(sec, loaded=True)

elf.write("packed_sample_reconstructed")  
print("[+] ELF reconstruit : packed_sample_reconstructed")  
```

```
$ python3 reconstruct_elf.py
[+] ELF reconstruit : packed_sample_reconstructed

$ readelf -h packed_sample_reconstructed | grep "Entry point"
  Entry point address:               0x401000

$ readelf -S packed_sample_reconstructed | grep -E "text|rodata|data"
  [ 1] .text             PROGBITS         0000000000401000  ...
  [ 2] .rodata           PROGBITS         0000000000404000  ...
  [ 3] .data             PROGBITS         0000000000405000  ...

$ strings packed_sample_reconstructed | grep FLAG
FLAG{unp4ck3d_and_r3c0nstruct3d}
```

Le binaire est prêt pour l'analyse.

---

## Livrable 3 — Analyse de la logique

### 3.1 — Fonctions identifiées dans Ghidra

Après import dans Ghidra et auto-analyse, les fonctions suivantes sont identifiées (noms attribués manuellement par l'analyste, le binaire étant strippé) :

| Adresse (approx.) | Nom attribué | Rôle |  
|--------------------|--------------|------|  
| `0x401000` | `_start` | Point d'entrée CRT, appelle `__libc_start_main` |  
| `0x401150` | `main` | Fonction principale : affiche le banner, lit l'input, appelle la vérification |  
| `0x401290` | `check_license_key` | Vérifie le format et la validité de la clé saisie |  
| `0x401320` | `compute_checksum` | Calcule un checksum pondéré sur un buffer |  
| `0x401360` | `xor_decode` | Déchiffre un buffer par XOR cyclique avec une clé |  
| `0x4013a0` | `print_debug_info` | Affiche les métadonnées internes (mode `--debug`) |

> **Note** — Les adresses exactes varient selon le niveau d'optimisation et la version de GCC. Les adresses ci-dessus sont indicatives pour une compilation `-O2`.

### 3.2 — Pseudo-code de `check_license_key`

Extrait du décompilé Ghidra, renommé et annoté :

```c
int check_license_key(char *key)
{
    /* Le format attendu est exactement 9 caractères */
    if (strlen(key) != 9)
        return 0;

    /* Les 5 premiers caractères doivent être "RE29-" */
    if (strncmp(key, "RE29-", 5) != 0)
        return 0;

    /* Les 4 derniers caractères sont interprétés comme un nombre hexadécimal */
    char *endptr;
    unsigned long user_val = strtoul(key + 5, &endptr, 16);
    if (*endptr != '\0')
        return 0;

    /* Le nombre doit correspondre au checksum du préfixe */
    uint32_t expected = compute_checksum("RE29-", 5);
    return (user_val == expected);
}
```

### 3.3 — Algorithme de checksum

Le décompilé de `compute_checksum` révèle l'algorithme :

```c
uint32_t compute_checksum(char *buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += (uint32_t)buf[i] * (uint32_t)(i + 1);
    }
    return sum & 0xFFFF;
}
```

C'est une **somme pondérée** des codes ASCII, où le poids de chaque caractère est sa position (indexée à 1), le tout réduit modulo `0xFFFF` (masque 16 bits).

### 3.4 — Calcul de la clé valide

On applique `compute_checksum` au préfixe `"RE29-"` :

| Position (i) | Caractère | ASCII (décimal) | Poids (i+1) | Contribution |  
|--------------|-----------|-----------------|-------------|--------------|  
| 0            | `R`       | 82              | 1           | 82           |  
| 1            | `E`       | 69              | 2           | 138          |  
| 2            | `2`       | 50              | 3           | 150          |  
| 3            | `9`       | 57              | 4           | 228          |  
| 4            | `-`       | 45              | 5           | 225          |

```
sum = 82 + 138 + 150 + 228 + 225 = 823  
expected = 823 & 0xFFFF = 823 = 0x0337  
```

La clé valide est donc : **`RE29-0337`**

Vérification rapide en Python :

```python
>>> prefix = "RE29-"
>>> checksum = sum(ord(c) * (i+1) for i, c in enumerate(prefix)) & 0xFFFF
>>> f"RE29-{checksum:04X}"
'RE29-0337'
```

### 3.5 — Routine XOR

La fonction `xor_decode` effectue un XOR cyclique entre un message chiffré et une clé de 8 octets :

**Clé XOR** (extraite de `.rodata` dans Ghidra ou ImHex) :

```
DE AD BE EF CA FE BA BE
```

**Message chiffré** (8 octets) :

```
8D F8 FD AC 8F AD E9 9F
```

**Déchiffrement** :

| Index | Chiffré | Clé   | XOR   | ASCII |  
|-------|---------|-------|-------|-------|  
| 0     | `0x8D`  | `0xDE`| `0x53`| `S`   |  
| 1     | `0xF8`  | `0xAD`| `0x55`| `U`   |  
| 2     | `0xFD`  | `0xBE`| `0x43`| `C`   |  
| 3     | `0xAC`  | `0xEF`| `0x43`| `C`   |  
| 4     | `0x8F`  | `0xCA`| `0x45`| `E`   |  
| 5     | `0xAD`  | `0xFE`| `0x53`| `S`   |  
| 6     | `0xE9`  | `0xBA`| `0x53`| `S`   |  
| 7     | `0x9F`  | `0xBE`| `0x21`| `!`   |

**Message déchiffré : `SUCCESS!`**

### 3.6 — Constantes crypto (bonus)

Dans `.rodata`, à l'adresse correspondant à `g_fake_sbox`, on identifie les 16 premiers octets de la S-box AES (annexe J) :

```
63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76
```

Ces constantes sont un marqueur classique de l'utilisation d'AES dans un binaire. Dans notre programme, elles sont présentes à titre pédagogique (variable `g_fake_sbox` non utilisée fonctionnellement), mais la technique d'identification est la même que sur un vrai binaire utilisant AES.

---

## Livrable 4 — Preuve de résolution

### Exécution directe (VM sandbox)

```
$ ./packed_sample_upx_tampered
╔══════════════════════════════════════╗
║   Ch29 — PackedSample v1.0           ║
║   Formation RE — Chaîne GNU          ║
╚══════════════════════════════════════╝

[*] Entrez votre clé de licence (format RE29-XXXX) : RE29-0337

[+] Clé valide ! Message déchiffré : SUCCESS!
[+] Flag : FLAG{unp4ck3d_and_r3c0nstruct3d}
[+] Bravo, vous avez retrouvé la logique après unpacking !
```

### Preuve GDB (alternative)

```
$ gdb -q ./packed_sample_upx_tampered

gef➤ hbreak *0x401000  
gef➤ run  
Breakpoint 1, 0x0000000000401000 in ?? ()  

gef➤ # Poser un breakpoint sur check_license_key  
gef➤ break *0x401290  
gef➤ continue  

[*] Entrez votre clé de licence (format RE29-XXXX) : RE29-0337

Breakpoint 2, 0x0000000000401290 in ?? ()

gef➤ x/s $rdi
0x7fffffffe0a0: "RE29-0337"

gef➤ finish  
Value returned is $1 = 1  

gef➤ # retour = 1 → clé valide ✓
```

### Script pwntools (alternative)

```python
#!/usr/bin/env python3
# solve_ch29.py

from pwn import *

p = process("./packed_sample_upx_tampered")  
p.recvuntil(b"RE29-XXXX) : ")  
p.sendline(b"RE29-0337")  

response = p.recvall(timeout=2).decode()  
print(response)  

assert "FLAG{unp4ck3d_and_r3c0nstruct3d}" in response  
log.success("Checkpoint validé !")  
```

```
$ python3 solve_ch29.py
[+] Starting local process './packed_sample_upx_tampered': pid 12345

[+] Clé valide ! Message déchiffré : SUCCESS!
[+] Flag : FLAG{unp4ck3d_and_r3c0nstruct3d}
[+] Bravo, vous avez retrouvé la logique après unpacking !

[+] Checkpoint validé !
```

---

## Grille d'auto-évaluation

| Critère | Statut |  
|---------|--------|  
| Rapport cite ≥ 4 indices convergents | ✅ 7/8 indices documentés |  
| Packer identifié comme UPX altéré | ✅ Signatures `FKP!` / `XP_0` identifiées |  
| ELF reconstruit passe `readelf` sans erreur | ✅ (approche A : complet / approche B : minimal mais valide) |  
| Ghidra produit un désassemblage cohérent | ✅ Fonctions identifiées, décompilé lisible |  
| Algorithme de vérification correctement décrit | ✅ Checksum pondéré + masque 16 bits |  
| Clé `RE29-0337` trouvée et validée | ✅ Calcul détaillé + preuve d'exécution |  
| SHT avec `.text` et `.rodata` (recommandé) | ✅ Approche B : 3 sections créées |  
| Message `SUCCESS!` retrouvé (recommandé) | ✅ XOR déchiffré octet par octet |  
| Constantes S-box AES repérées (bonus) | ✅ 16 octets identifiés dans `.rodata` |  
| Script de reconstruction automatisé (bonus) | ✅ `reconstruct_elf.py` avec LIEF |

⏭️
