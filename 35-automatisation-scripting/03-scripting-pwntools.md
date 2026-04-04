🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 35.3 — Scripting RE avec `pwntools` (interactions, patching, exploitation)

> 📦 **Bibliothèque** : `pwntools` 4.x (Python 3)  
> 🐍 **Installation** : `pip install pwntools`  
> 📁 **Binaires d'exemple** : `keygenme_O0`, `crypto_O0`, `fileformat_O0`, binaires réseau du chapitre 23

---

## `pwntools` au-delà de l'exploitation

Au chapitre 11 (section 9), nous avons introduit `pwntools` comme un outil d'interaction avec les binaires — lancer un processus, envoyer des données, recevoir la sortie. Au chapitre 21 (section 8), nous l'avons utilisé pour écrire un keygen. Ces usages ne représentent qu'une fraction de ce que la bibliothèque offre.

`pwntools` est un framework complet pour le scripting de sécurité offensive, mais ses primitives sont tout aussi utiles dans un contexte de reverse engineering défensif et d'automatisation. Ses points forts pour le RE sont les suivants : interaction programmatique avec des processus locaux et distants (via des tubes uniformes), manipulation fine de données binaires (packing/unpacking, recherche de patterns), patching de binaires ELF sur disque, intégration avec GDB pour le débogage scriptable, et assemblage/désassemblage à la volée.

Cette section organise ces capacités autour de trois axes : interagir avec un binaire, le patcher, et combiner les deux dans un workflow d'analyse automatisée.

---

## Configuration et contexte

Avant tout script `pwntools`, on configure le contexte global qui détermine l'architecture cible, l'endianness et le niveau de verbosité :

```python
from pwn import *

# Configuration pour nos binaires x86-64 Linux
context.arch = 'amd64'  
context.os = 'linux'  
context.log_level = 'info'   # 'debug' pour voir tous les échanges  
```

Le contexte influence le comportement de toutes les fonctions de packing (`p32`, `p64`, `u32`, `u64`), d'assemblage (`asm`), et de génération de shellcode. Pour nos binaires compilés avec GCC sur Linux x86-64, la configuration ci-dessus sera la même dans tous les scripts de cette section.

---

## Partie A — Interagir avec un processus

### Lancer un binaire et dialoguer

La classe `process` crée un tube connecté aux flux stdin/stdout d'un processus local. Les méthodes `sendline()`, `recvuntil()`, `recvline()` permettent un dialogue structuré :

```python
from pwn import *

def test_key(username, key):
    """Lance keygenme_O0 et teste un couple username/key."""
    p = process("./keygenme_O0")

    # Attendre le prompt et envoyer le username
    p.recvuntil(b"Enter username: ")
    p.sendline(username.encode())

    # Attendre le prompt et envoyer la clé
    p.recvuntil(b"XXXX-XXXX-XXXX-XXXX): ")
    p.sendline(key.encode())

    # Lire la réponse
    response = p.recvall(timeout=2)
    p.close()

    return b"Valid license" in response

# Test rapide
if test_key("alice", "0000-0000-0000-0000"):
    print("Clé acceptée !")
else:
    print("Clé rejetée (attendu).")
```

Ce pattern est la base de tout script de validation automatique. On peut l'utiliser pour vérifier qu'un keygen produit bien des clés acceptées, ou pour fuzzer les entrées et observer le comportement du binaire.

### Interactions réseau

Pour les binaires réseau du chapitre 23, `pwntools` offre la classe `remote` qui fournit exactement la même interface que `process`, mais sur une connexion TCP :

```python
from pwn import *

# Se connecter au serveur du chapitre 23
r = remote("127.0.0.1", 4444)

# Le dialogue est identique à celui d'un process local
r.recvuntil(b"Welcome")  
r.sendline(b"AUTH user pass")  
response = r.recvline()  
print(f"Réponse du serveur : {response}")  

r.close()
```

L'uniformité de l'interface est un avantage considérable. Un script développé en local avec `process` peut être basculé sur un serveur distant en remplaçant une seule ligne — ce qui est exactement ce qu'on fait dans les CTF et dans l'analyse de protocoles réseau.

### Tubes interchangeables

Pour rendre un script agnostique à la cible, on utilise un pattern courant : un argument qui sélectionne le mode de connexion.

```python
from pwn import *  
import sys  

def get_tube():
    if len(sys.argv) > 1 and sys.argv[1] == "remote":
        return remote("target.example.com", 4444)
    else:
        return process("./keygenme_O0")

io = get_tube()
# ... le reste du script est identique quel que soit le mode
io.close()
```

---

## Partie B — Manipulation de données binaires

### Packing et unpacking

`pwntools` fournit des fonctions de conversion entre valeurs numériques et représentations binaires, dans l'endianness configurée dans le contexte. Ces fonctions sont omniprésentes dans les scripts de RE :

```python
from pwn import *  
context.arch = 'amd64'  

# Packer un entier 32 bits en little-endian
packed = p32(0x5A3C6E2D)    # HASH_SEED du keygenme  
print(packed)                # b'\x2d\x6e\x3c\x5a'  

# Unpacker des octets lus dans un binaire
value = u32(b'\xef\xbe\xad\xde')  
print(f"0x{value:08x}")     # 0xdeadbeef — HASH_XOR du keygenme  

# 64 bits
entry = p64(0x401060)  
addr = u64(b'\x60\x10\x40\x00\x00\x00\x00\x00')  

# Packing signé
neg = p32(-1, signed=True)  # b'\xff\xff\xff\xff'

# Forcer big-endian ponctuellement
be = p32(0xDEADBEEF, endian='big')  # b'\xde\xad\xbe\xef'
```

Comparées à `struct.pack` / `struct.unpack` de la bibliothèque standard, ces fonctions sont plus concises et adaptées au contexte — `p32(x)` remplace `struct.pack("<I", x)`, ce qui réduit le bruit dans les scripts qui manipulent beaucoup de données binaires.

### Recherche de patterns dans un binaire

La classe `ELF` de `pwntools` charge un binaire et expose ses sections, ses symboles, et un moteur de recherche :

```python
from pwn import *

elf = ELF("./keygenme_O0")

# Informations de base
print(f"Arch :  {elf.arch}")  
print(f"Entry : {hex(elf.entry)}")  
print(f"PIE :   {elf.pie}")  

# Accéder aux symboles (binaire non strippé)
if 'check_license' in elf.symbols:
    addr = elf.symbols['check_license']
    print(f"check_license @ {hex(addr)}")

if 'main' in elf.symbols:
    print(f"main @ {hex(elf.symbols['main'])}")

# Imports (PLT)
if 'strcmp' in elf.plt:
    print(f"strcmp@plt : {hex(elf.plt['strcmp'])}")

# GOT
if 'strcmp' in elf.got:
    print(f"strcmp@got : {hex(elf.got['strcmp'])}")
```

La méthode `search()` parcourt les données brutes du binaire à la recherche d'une séquence d'octets :

```python
from pwn import *

elf = ELF("./keygenme_O0")

# Chercher la constante HASH_SEED (0x5A3C6E2D) en little-endian
seed_bytes = p32(0x5A3C6E2D)  
for addr in elf.search(seed_bytes):  
    print(f"HASH_SEED trouvé à {hex(addr)}")

# Chercher une chaîne
for addr in elf.search(b"KeyGenMe"):
    print(f"Bannière trouvée à {hex(addr)}")

# Chercher la constante HASH_XOR (0xDEADBEEF)
for addr in elf.search(p32(0xDEADBEEF)):
    print(f"HASH_XOR trouvé à {hex(addr)}")
```

Sur `keygenme_O0`, ce script localisera `HASH_SEED` dans `.text` (comme opérande immédiate d'une instruction `mov`) et possiblement dans les informations DWARF, ainsi que la bannière dans `.rodata`. Sur `crypto_O0`, attention : `KEY_MASK` est un tableau d'octets (`unsigned char[]`), pas un `uint32_t` — les octets `DE AD BE EF` sont stockés dans cet ordre en mémoire. Il faut donc chercher la séquence brute `b"\xDE\xAD\xBE\xEF"`, et non `p32(0xDEADBEEF)` qui produirait `EF BE AD DE` (little-endian). En revanche, `HASH_XOR` dans `keygenme.c` est un `uint32_t` utilisé comme opérande immédiate — le compilateur l'encode bien en LE, et `p32` le trouve correctement.

### Construire et parser des structures binaires

Pour les formats custom comme celui du chapitre 25 (archive CFR), `pwntools` offre un moyen concis de construire et parser des paquets binaires. Combiné avec `struct` ou la méthode `flat()`, on peut reproduire le format en quelques lignes :

```python
from pwn import *  
import struct  
import time  

def build_cfr_header(num_records, flags=0x02, author=b"analyst"):
    """Construit un header CFR de 32 octets conforme au format du ch25."""
    magic = b"CFRM"
    version = 0x0002
    timestamp = int(time.time())

    # Packer les 16 premiers octets (pour le CRC)
    first_16 = struct.pack("<4sHHII",
                           magic, version, flags,
                           num_records, timestamp)

    # CRC-32 des 16 premiers octets (simplifié ici)
    import zlib
    header_crc = zlib.crc32(first_16) & 0xFFFFFFFF

    # Compléter : CRC(4) + author(8) + reserved(4) = 16 octets
    author_padded = author.ljust(8, b'\x00')[:8]
    reserved = p32(0)  # sera recalculé selon les data_len

    header = first_16 + p32(header_crc) + author_padded + reserved
    assert len(header) == 32
    return header

hdr = build_cfr_header(num_records=2, flags=0x03)  
print(f"Header CFR : {hdr.hex()}")  
print(f"Taille : {len(hdr)} octets")  
```

Ce pattern est exactement celui utilisé au chapitre 25, section 4 pour écrire un parser/sérialiseur Python indépendant. `pwntools` ne remplace pas `struct`, mais ses fonctions de packing raccourcissent le code quand on manipule beaucoup de valeurs de tailles variées.

---

## Partie C — Patching de binaires ELF

### Patcher avec la classe `ELF`

La classe `ELF` de `pwntools` permet de modifier un binaire en mémoire puis de l'écrire sur disque. C'est plus simple que `lief` pour les patchs ponctuels et bien intégré dans le workflow `pwntools` :

```python
from pwn import *

elf = ELF("./keygenme_O0")

# Lire l'octet à l'adresse de check_license
check_addr = elf.symbols['check_license']  
print(f"check_license @ {hex(check_addr)}")  

# Chercher le premier JNZ (0x75) après l'appel à strcmp dans check_license
# On lit un bloc de bytes autour de la fonction
func_bytes = elf.read(check_addr, 200)

# Localiser l'appel à strcmp@plt (opcode E8 + offset relatif)
strcmp_plt = elf.plt['strcmp']
# Chercher le pattern JNZ (0x75) qui suit le test du retour de strcmp
# Dans check_license compilé en -O0, la séquence typique est :
#   call strcmp@plt
#   test eax, eax      (85 C0)
#   jne  .Lfail        (75 xx)
pattern = b'\x85\xc0\x75'  
offset = func_bytes.find(pattern)  

if offset >= 0:
    # L'octet 0x75 est à check_addr + offset + 2
    jnz_addr = check_addr + offset + 2
    print(f"JNZ trouvé à {hex(jnz_addr)}")

    # Remplacer JNZ (0x75) par JZ (0x74)
    elf.write(jnz_addr, b'\x74')
    elf.save("./keygenme_O0_cracked")
    print("[+] Binaire patché sauvegardé")
else:
    print("[-] Pattern non trouvé")
```

Ce script automatise exactement le patching manuel réalisé avec ImHex au chapitre 21, section 6. La recherche du pattern `test eax, eax` suivi de `jnz` est plus robuste qu'une recherche isolée de `0x75` — le triplet `\x85\xc0\x75` est caractéristique du test du retour de `strcmp` en code non optimisé.

### Vérifier le patch

Après avoir patché le binaire, on vérifie automatiquement qu'il accepte n'importe quelle clé :

```python
from pwn import *

def verify_crack(binary_path, username, fake_key):
    """Vérifie qu'un binaire patché accepte une clé arbitraire."""
    p = process(binary_path)
    p.recvuntil(b"Enter username: ")
    p.sendline(username.encode())
    p.recvuntil(b"XXXX-XXXX-XXXX-XXXX): ")
    p.sendline(fake_key.encode())
    response = p.recvall(timeout=2)
    p.close()
    return b"Valid license" in response

# La clé est volontairement fausse
assert verify_crack("./keygenme_O0_cracked", "test_user", "AAAA-BBBB-CCCC-DDDD")  
print("[+] Crack vérifié : toute clé est acceptée")  
```

L'enchaînement patch → vérification dans un même script est un pattern fondamental en automatisation RE. Il garantit que la transformation a eu l'effet escompté et documente le résultat de manière reproductible.

---

## Partie D — Assemblage et désassemblage à la volée

`pwntools` intègre un assembleur et un désassembleur qui ne nécessitent aucun outil externe (ils utilisent `keystone` et `capstone` sous le capot quand ils sont disponibles, avec un fallback sur les binutils GNU).

### Assembler des instructions

```python
from pwn import *  
context.arch = 'amd64'  

# Assembler une instruction unique
nop = asm('nop')  
print(f"nop = {nop.hex()}")  # 90  

# Assembler un bloc
code = asm('''
    xor rdi, rdi
    mov rax, 60
    syscall
''')
print(f"exit(0) = {code.hex()}")

# Utile pour le patching : générer le bon opcode
jz_short = asm('jz $+0x10')  
print(f"jz +16 = {jz_short.hex()}")  
```

En contexte RE, l'assemblage à la volée sert à générer les octets de remplacement lors d'un patch, sans devoir se souvenir des encodages d'opcodes. Si on veut remplacer un `jnz` par un saut inconditionnel vers une adresse précise, `asm()` produit les bons octets automatiquement.

### Désassembler des octets

```python
from pwn import *  
context.arch = 'amd64'  

# Désassembler des octets lus dans un binaire
raw = bytes.fromhex("554889e54883ec10897dfc")  
print(disasm(raw))  
```

Sortie :

```
   0:   55                      push   rbp
   1:   48 89 e5                mov    rbp, rsp
   4:   48 83 ec 10             sub    rsp, 0x10
   8:   89 7d fc                mov    DWORD PTR [rbp-0x4], edi
```

On reconnaît immédiatement le prologue classique d'une fonction GCC en `-O0` : sauvegarde de `rbp`, mise en place du frame, allocation de l'espace local, sauvegarde du premier argument (`edi`) dans une variable locale.

### Désassembler une fonction entière

En combinant la classe `ELF` et `disasm`, on peut extraire et désassembler n'importe quelle fonction d'un binaire non strippé :

```python
from pwn import *  
context.arch = 'amd64'  

elf = ELF("./keygenme_O0", checksec=False)

# Lire les octets de compute_hash
func_addr = elf.symbols['compute_hash']
# On lit un bloc raisonnable (la taille exacte est dans les symboles DWARF,
# mais 512 octets suffisent pour une fonction de cette taille)
func_bytes = elf.read(func_addr, 512)

print(f"=== compute_hash @ {hex(func_addr)} ===")  
print(disasm(func_bytes, vma=func_addr))  
```

Le paramètre `vma` (Virtual Memory Address) ajuste les adresses affichées pour qu'elles correspondent aux adresses réelles du binaire — les sauts et les appels afficheront leurs cibles correctes plutôt que des offsets relatifs à zéro.

---

## Partie E — Intégration avec GDB

`pwntools` peut lancer un processus attaché à GDB, ce qui permet de scripter des sessions de débogage complexes. C'est le pont entre l'automatisation et l'analyse dynamique fine.

### Lancer un processus sous GDB

```python
from pwn import *  
context.arch = 'amd64'  

# Lancer keygenme_O0 sous GDB avec des commandes automatiques
p = gdb.debug("./keygenme_O0", '''
    break check_license
    continue
''')

# Envoyer les inputs
p.recvuntil(b"Enter username: ")  
p.sendline(b"alice")  
p.recvuntil(b"XXXX-XXXX-XXXX-XXXX): ")  
p.sendline(b"AAAA-BBBB-CCCC-DDDD")  

# À ce point, GDB est arrêté sur le breakpoint dans check_license.
# L'analyste peut interagir manuellement avec GDB dans le terminal,
# ou le script peut continuer automatiquement.
p.interactive()
```

L'appel `gdb.debug()` ouvre un terminal GDB séparé, attaché au processus. Le script Python continue de contrôler stdin/stdout pendant que GDB contrôle l'exécution. C'est extrêmement puissant pour reproduire un scénario précis — par exemple, naviguer automatiquement jusqu'au point de comparaison de la clé, puis inspecter les registres manuellement.

### Script GDB automatisé

Pour une automatisation complète sans interaction manuelle, on peut passer un script GDB plus élaboré qui dumpe les informations voulues :

```python
from pwn import *  
context.arch = 'amd64'  

# Script GDB qui dumpe les arguments de strcmp
gdb_script = '''
    set pagination off
    break strcmp
    commands
        silent
        printf "strcmp(\\"%s\\", \\"%s\\")\\n", (char*)$rdi, (char*)$rsi
        continue
    end
    continue
'''

p = gdb.debug("./keygenme_O0", gdb_script)

p.recvuntil(b"Enter username: ")  
p.sendline(b"alice")  
p.recvuntil(b"XXXX-XXXX-XXXX-XXXX): ")  
p.sendline(b"AAAA-BBBB-CCCC-DDDD")  

# Laisser le programme terminer
response = p.recvall(timeout=3)  
p.close()  
```

Dans le terminal GDB, on verra apparaître la ligne `strcmp("XXXX-XXXX-XXXX-XXXX", "AAAA-BBBB-CCCC-DDDD")` — le premier argument est la clé attendue, calculée par `check_license`. Ce script automatise exactement le checkpoint du chapitre 11 (écrire un script GDB qui dumpe les arguments de chaque appel à `strcmp`), mais piloté depuis Python plutôt que depuis un fichier `.gdb`.

---

## Partie F — Keygen automatisé complet

En combinant tout ce qui précède, voici un keygen complet pour `keygenme_O0` qui fonctionne en deux étapes : extraction de la clé attendue via GDB, puis vérification automatique.

```python
#!/usr/bin/env python3
"""
keygen_auto.py — Keygen automatisé pour keygenme (toutes variantes)

Stratégie :
  1. Lancer le binaire sous GDB
  2. Poser un breakpoint sur strcmp
  3. Envoyer un username et une clé bidon
  4. Lire la clé attendue dans RDI au moment du strcmp
  5. Relancer le binaire proprement avec la bonne clé
  6. Vérifier que la licence est acceptée

Usage : python3 keygen_auto.py <binary> <username>
"""

from pwn import *  
import sys  
import re  

context.arch = 'amd64'  
context.log_level = 'warn'  # Réduire le bruit  

def extract_expected_key(binary, username):
    """Lance le binaire sous GDB et extrait la clé attendue."""

    # Script GDB : breakpoint sur strcmp, afficher RDI (1er argument)
    gdb_script = '''
        set pagination off
        break strcmp
        commands
            silent
            printf "KEYDUMP:%s\\n", (char*)$rdi
            continue
        end
        continue
    '''

    p = gdb.debug(binary, gdb_script, level='warn')

    p.recvuntil(b"Enter username: ")
    p.sendline(username.encode())
    p.recvuntil(b"XXXX-XXXX-XXXX-XXXX): ")
    p.sendline(b"AAAA-BBBB-CCCC-DDDD")

    # Lire toute la sortie (GDB + programme)
    try:
        output = p.recvall(timeout=5).decode(errors='replace')
    except Exception:
        output = ""
    p.close()

    # Extraire la clé du format KEYDUMP:XXXX-XXXX-XXXX-XXXX
    match = re.search(r'KEYDUMP:([0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4})',
                      output)
    if match:
        return match.group(1)
    return None

def verify_key(binary, username, key):
    """Vérifie que le couple username/key est accepté."""
    p = process(binary)
    p.recvuntil(b"Enter username: ")
    p.sendline(username.encode())
    p.recvuntil(b"XXXX-XXXX-XXXX-XXXX): ")
    p.sendline(key.encode())
    response = p.recvall(timeout=2)
    p.close()
    return b"Valid license" in response

if __name__ == "__main__":
    binary = sys.argv[1] if len(sys.argv) > 1 else "./keygenme_O0"
    username = sys.argv[2] if len(sys.argv) > 2 else "student"

    print(f"[*] Cible    : {binary}")
    print(f"[*] Username : {username}")

    key = extract_expected_key(binary, username)
    if key:
        print(f"[+] Clé extraite : {key}")
        if verify_key(binary, username, key):
            print(f"[+] Vérification réussie")
        else:
            print(f"[-] Vérification échouée (bug dans l'extraction ?)")
    else:
        print("[-] Impossible d'extraire la clé")
```

Ce script fonctionne sur toutes les variantes du keygenme — y compris les versions strippées — parce qu'il ne dépend pas des symboles du binaire cible : `strcmp` est un import dynamique dont le symbole est toujours présent dans `.dynsym`, même après `strip --strip-all`. C'est un point crucial : les symboles de la PLT survivent au stripping.

---

## Partie G — Automatiser l'analyse du format CFR

Pour le binaire `fileformat_O0` du chapitre 25, `pwntools` sert à la fois à interagir avec le programme et à valider un parser Python indépendant. Voici un script qui génère une archive CFR, l'inspecte avec le binaire officiel, puis compare le résultat avec un parsing direct :

```python
from pwn import *  
import struct  
import tempfile  
import os  

context.log_level = 'warn'

def cfr_list_via_binary(binary, archive_path):
    """Utilise le binaire officiel pour lister le contenu d'une archive."""
    p = process([binary, "list", archive_path])
    output = p.recvall(timeout=5).decode()
    p.close()
    return output

def cfr_validate_via_binary(binary, archive_path):
    """Lance la validation intégrée du binaire."""
    p = process([binary, "validate", archive_path])
    output = p.recvall(timeout=5).decode()
    ret = p.poll()
    p.close()
    return ret == 0, output

# Générer une archive de test
with tempfile.TemporaryDirectory() as tmpdir:
    archive = os.path.join(tmpdir, "test.cfr")
    p = process(["./fileformat_O0", "generate", archive])
    p.recvall(timeout=5)
    p.close()

    # Lister le contenu via le binaire
    listing = cfr_list_via_binary("./fileformat_O0", archive)
    print("=== Listing via binaire ===")
    print(listing)

    # Valider l'intégrité
    ok, details = cfr_validate_via_binary("./fileformat_O0", archive)
    print(f"=== Validation : {'PASS' if ok else 'FAIL'} ===")
    print(details)

    # Parser l'archive directement en Python pour comparaison
    with open(archive, "rb") as f:
        magic = f.read(4)
        version, flags, num_rec, timestamp = struct.unpack("<HHII", f.read(12))
        print(f"\n=== Parsing Python direct ===")
        print(f"Magic   : {magic}")
        print(f"Version : 0x{version:04x}")
        print(f"Flags   : 0x{flags:04x}")
        print(f"Records : {num_rec}")
```

Ce pattern de double vérification — résultat du binaire officiel vs parsing indépendant — est caractéristique de l'approche RE : on utilise le binaire comme oracle pour valider notre compréhension du format, et les divergences révèlent les erreurs dans notre parser ou les subtilités du format qu'on n'a pas encore comprises.

---

## Résumé des primitives `pwntools` pour le RE

| Primitive | Usage RE | Exemple |  
|---|---|---|  
| `process()` | Lancer et dialoguer avec un binaire local | Tester un keygen, fuzzer les entrées |  
| `remote()` | Connexion TCP vers un service distant | Analyse de protocole réseau (ch23) |  
| `ELF()` | Charger un binaire, accéder aux symboles | Localiser `check_license`, `strcmp@plt` |  
| `elf.search()` | Chercher des octets dans le binaire | Trouver les constantes magiques |  
| `elf.write()` / `elf.save()` | Patcher des octets et sauvegarder | Inverser un saut conditionnel |  
| `p32()` / `u32()` / `p64()` / `u64()` | Conversion entiers ↔ bytes | Parser des headers binaires |  
| `asm()` | Assembler des instructions | Générer les octets d'un patch |  
| `disasm()` | Désassembler des octets | Inspecter une fonction |  
| `gdb.debug()` | Lancer un processus sous GDB | Extraire la clé au point de comparaison |  
| `flat()` | Construire un buffer binaire structuré | Forger des paquets ou des headers |  
| `context.arch` | Configurer l'architecture cible | `amd64`, `i386`, `arm`, `mips` |

La force de `pwntools` est l'unification de ces primitives dans un seul framework cohérent. Un script qui commence par charger un ELF, localise une fonction, la désassemble, identifie le point de patch, modifie le binaire, le relance, interagit avec lui et vérifie le résultat — tout cela tient dans un seul fichier Python de quelques dizaines de lignes, sans outil externe.

---


⏭️ [Écrire des règles YARA pour détecter des patterns dans une collection de binaires](/35-automatisation-scripting/04-regles-yara.md)
