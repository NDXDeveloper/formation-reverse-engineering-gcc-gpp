🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 35.1 — Scripts Python avec `pyelftools` et `lief` (parsing et modification d'ELF)

> 📦 **Bibliothèques couvertes** :  
> - `pyelftools` (lecture seule) — parsing fidèle des structures ELF, DWARF, `.symtab`, `.dynamic`  
> - `lief` (lecture + écriture) — parsing, modification et reconstruction de binaires ELF, PE, Mach-O  
>  
> 🐍 **Prérequis** : Python 3.8+, `pip install pyelftools lief`

---

## Pourquoi deux bibliothèques ?

L'écosystème Python offre plusieurs bibliothèques pour manipuler des binaires ELF. Deux d'entre elles se sont imposées comme les piliers de tout toolkit d'automatisation RE, et elles ne se substituent pas l'une à l'autre — elles se complètent.

`pyelftools` est une bibliothèque pure Python créée par Eli Bendersky. Elle ne fait qu'une chose : parser les structures ELF conformément à la spécification, en exposant chaque champ exactement tel qu'il apparaît sur disque. Elle ne modifie rien, ne reconstruit rien, et c'est précisément ce qui fait sa force. Quand on veut lire un header ELF, parcourir la table des symboles, inspecter les informations DWARF ou extraire les entrées de la section `.dynamic`, `pyelftools` donne un accès direct et sans surprise à chaque structure. Son modèle mental est celui de `readelf` : on lit, on inspecte, on rapporte.

`lief` (Library to Instrument Executable Formats) est un projet plus ambitieux. Écrit en C++ avec des bindings Python, il parse les formats ELF, PE et Mach-O, mais il permet aussi de les *modifier* : ajouter une section, renommer un symbole, changer l'entry point, injecter une dépendance dynamique, patcher des octets dans `.text`, puis réécrire le binaire sur disque. Là où `pyelftools` est un microscope, `lief` est à la fois un microscope et un scalpel.

En pratique, un script de triage automatique commencera souvent par `pyelftools` pour l'inspection (c'est plus léger, plus rapide à écrire, et la correspondance avec la sortie de `readelf` facilite la vérification). Dès que le script doit *transformer* le binaire — patcher un octet, ajouter une section de métadonnées, modifier un flag — on bascule sur `lief`.

---

## Partie A — `pyelftools` : inspection programmatique d'ELF

### Installation et import de base

```bash
pip install pyelftools
```

Le point d'entrée principal est la classe `ELFFile`, qui prend un objet fichier ouvert en mode binaire :

```python
from elftools.elf.elffile import ELFFile

with open("keygenme_O0", "rb") as f:
    elf = ELFFile(f)
    print(f"Classe :  {elf.elfclass}-bit")
    print(f"Endian :  {elf.little_endian and 'little' or 'big'}")
    print(f"Machine : {elf['e_machine']}")
    print(f"Type :    {elf['e_type']}")
    print(f"Entry :   0x{elf['e_entry']:x}")
```

> ⚠️ **Point important** : `ELFFile` lit le fichier à la demande (*lazy parsing*). Le fichier doit rester ouvert pendant toute la durée de l'utilisation de l'objet `elf`. Si vous fermez le fichier puis tentez d'accéder à une section, vous obtiendrez une erreur.

### Lire le header ELF

L'objet `elf.header` expose tous les champs du header ELF sous forme de dictionnaire. Les noms de champs suivent exactement la nomenclature de la spécification ELF (`e_type`, `e_machine`, `e_version`, `e_entry`, etc.), ce qui rend la correspondance avec `readelf -h` immédiate :

```python
hdr = elf.header  
print(f"Program headers : {hdr['e_phnum']}")  
print(f"Section headers : {hdr['e_shnum']}")  
print(f"String table    : index {hdr['e_shstrndx']}")  
```

### Parcourir les sections

La méthode `iter_sections()` renvoie un itérateur sur toutes les sections du binaire. Chaque section expose son nom, son type, ses flags et ses données brutes :

```python
from elftools.elf.elffile import ELFFile

def list_sections(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        print(f"{'Index':>5}  {'Nom':<20}  {'Type':<18}  {'Taille':>10}  {'Addr':>12}")
        print("-" * 72)
        for i, section in enumerate(elf.iter_sections()):
            print(f"{i:5d}  {section.name:<20}  "
                  f"{section['sh_type']:<18}  "
                  f"{section['sh_size']:10d}  "
                  f"0x{section['sh_addr']:010x}")

list_sections("keygenme_O0")
```

Pour récupérer les données brutes d'une section spécifique — par exemple `.rodata`, là où résident les constantes comme `HASH_SEED` ou les chaînes de `keygenme.c` — on utilise `get_section_by_name()` :

```python
rodata = elf.get_section_by_name(".rodata")  
if rodata:  
    data = rodata.data()
    print(f".rodata : {len(data)} octets")
    # Chercher la bannière du keygenme
    idx = data.find(b"KeyGenMe")
    if idx >= 0:
        print(f"  Trouvé 'KeyGenMe' à l'offset {idx} dans .rodata")
```

### Extraire la table des symboles

La table des symboles (`.symtab`) n'existe que dans les binaires non strippés. `pyelftools` expose les symboles via des sections de type `SymbolTableSection` :

```python
from elftools.elf.sections import SymbolTableSection

def list_functions(path):
    """Liste les symboles de type FUNC (fonctions) d'un binaire."""
    with open(path, "rb") as f:
        elf = ELFFile(f)
        functions = []
        for section in elf.iter_sections():
            if not isinstance(section, SymbolTableSection):
                continue
            for sym in section.iter_symbols():
                if sym['st_info']['type'] == 'STT_FUNC' and sym['st_value'] != 0:
                    functions.append({
                        "name": sym.name,
                        "addr": sym['st_value'],
                        "size": sym['st_size'],
                        "bind": sym['st_info']['bind'],
                    })
        # Tri par adresse
        functions.sort(key=lambda s: s["addr"])
        return functions

for fn in list_functions("keygenme_O0"):
    print(f"  0x{fn['addr']:08x}  {fn['size']:5d}  {fn['bind']:<8}  {fn['name']}")
```

Sur `keygenme_O0` (compilé avec `-g`), ce script listera `main`, `check_license`, `compute_hash`, `derive_key`, `format_key`, `rotate_left` et `read_line`. Sur `keygenme_strip`, la liste sera vide — c'est exactement ce qui rend un binaire strippé plus difficile à analyser.

### Comparer les symboles entre variantes

L'un des cas d'usage immédiats de `pyelftools` en automatisation est la comparaison de deux variantes d'un même binaire. Le script suivant prend deux chemins et rapporte les fonctions présentes dans l'un mais absentes de l'autre :

```python
def compare_symbols(path_a, path_b):
    funcs_a = {fn["name"] for fn in list_functions(path_a)}
    funcs_b = {fn["name"] for fn in list_functions(path_b)}

    only_a = funcs_a - funcs_b
    only_b = funcs_b - funcs_a

    if only_a:
        print(f"Fonctions présentes uniquement dans {path_a}:")
        for name in sorted(only_a):
            print(f"  - {name}")
    if only_b:
        print(f"Fonctions présentes uniquement dans {path_b}:")
        for name in sorted(only_b):
            print(f"  - {name}")
    if not only_a and not only_b:
        print("Tables de symboles identiques (noms de fonctions).")

compare_symbols("keygenme_O0", "keygenme_O2")
```

Sur `keygenme`, une comparaison `-O0` vs `-O2` révèlera typiquement que certaines fonctions comme `rotate_left` ont disparu — le compilateur les a inlinées. Ce type de détection automatique est précieux quand on analyse un patch entre deux versions d'un logiciel.

### Inspecter les segments (program headers)

Les segments définissent comment le loader mappe le binaire en mémoire. Ils sont essentiels pour comprendre la disposition mémoire à l'exécution :

```python
def list_segments(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for seg in elf.iter_segments():
            print(f"  {seg['p_type']:<16}  "
                  f"offset=0x{seg['p_offset']:06x}  "
                  f"vaddr=0x{seg['p_vaddr']:010x}  "
                  f"memsz=0x{seg['p_memsz']:06x}  "
                  f"flags={'R' if seg['p_flags'] & 4 else '-'}"
                  f"{'W' if seg['p_flags'] & 2 else '-'}"
                  f"{'X' if seg['p_flags'] & 1 else '-'}")
```

### Lire les entrées dynamiques

Pour les binaires dynamiquement liés — comme `crypto_O0` qui dépend de `libcrypto` — la section `.dynamic` contient les dépendances, les chemins de recherche, et les flags de binding. `pyelftools` les expose via `DynamicSection` :

```python
from elftools.elf.dynamic import DynamicSection

def list_needed(path):
    """Équivalent de `ldd` simplifié : liste les NEEDED."""
    with open(path, "rb") as f:
        elf = ELFFile(f)
        needed = []
        for section in elf.iter_sections():
            if not isinstance(section, DynamicSection):
                continue
            for tag in section.iter_tags():
                if tag.entry.d_tag == 'DT_NEEDED':
                    needed.append(tag.needed)
        return needed

libs = list_needed("crypto_O0")  
print("Dépendances dynamiques :")  
for lib in libs:  
    print(f"  - {lib}")
# Attendu : libcrypto.so.x, libc.so.6
```

Ce script est la brique de base pour détecter automatiquement si un binaire embarque des dépendances cryptographiques — un indicateur précieux lors d'un triage.

### Accéder aux informations DWARF

Si le binaire a été compilé avec `-g`, les informations de debug DWARF sont accessibles. `pyelftools` offre un parser DWARF complet qui permet de retrouver les noms de fichiers sources, les numéros de ligne, et les types de données :

```python
def list_source_files(path):
    """Extrait les fichiers sources référencés dans les informations DWARF."""
    with open(path, "rb") as f:
        elf = ELFFile(f)
        if not elf.has_dwarf_info():
            print("Pas d'informations DWARF.")
            return
        dwarf = elf.get_dwarf_info()
        sources = set()
        for cu in dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag == 'DW_TAG_compile_unit':
                    name = die.attributes.get('DW_AT_name')
                    comp_dir = die.attributes.get('DW_AT_comp_dir')
                    if name:
                        sources.add(name.value.decode())
        for src in sorted(sources):
            print(f"  {src}")

list_source_files("keygenme_O0")
# Attendu : keygenme.c
```

---

## Partie B — `lief` : inspection *et* modification de binaires

### Installation et premier contact

```bash
pip install lief
```

Avec `lief`, le parsing se fait en un seul appel statique. Le binaire est entièrement chargé en mémoire — pas de *lazy parsing* comme `pyelftools`, ce qui signifie qu'on n'a pas besoin de garder le fichier ouvert :

```python
import lief

binary = lief.parse("keygenme_O0")  
print(f"Format :     {binary.format}")  
print(f"Type :       {binary.header.file_type}")  
print(f"Machine :    {binary.header.machine_type}")  
print(f"Entry point: 0x{binary.entrypoint:x}")  
print(f"PIE :        {binary.is_pie}")  
print(f"NX :         {binary.has_nx}")  
```

On note immédiatement que `lief` expose des propriétés de haut niveau (`is_pie`, `has_nx`) qui nécessiteraient plusieurs lignes avec `pyelftools`. C'est cette ergonomie qui rend `lief` adapté aux scripts de triage rapide.

### Sections : lecture et recherche

```python
for section in binary.sections:
    print(f"  {section.name:<20}  "
          f"size={section.size:>8}  "
          f"offset=0x{section.offset:06x}  "
          f"entropy={section.entropy:.2f}")
```

La propriété `entropy` est calculée automatiquement par `lief`. C'est un indicateur de premier ordre pour détecter des données compressées ou chiffrées. Sur `crypto_O2_strip`, les sections `.text` et `.rodata` auront une entropie modérée (4–6), tandis qu'une section contenant des données chiffrées ou packées dépassera souvent 7.

Pour chercher des octets spécifiques dans une section — par exemple les constantes magiques du `keygenme` :

```python
import struct

rodata = binary.get_section(".rodata")  
if rodata:  
    content = bytes(rodata.content)
    # Chercher HASH_SEED = 0x5A3C6E2D (little-endian)
    seed_bytes = struct.pack("<I", 0x5A3C6E2D)
    offset = content.find(seed_bytes)
    if offset >= 0:
        print(f"HASH_SEED trouvé dans .rodata à l'offset +0x{offset:x}")
```

### Symboles et imports

`lief` distingue les symboles statiques (`.symtab`), les symboles dynamiques (`.dynsym`) et les fonctions importées. Pour lister les imports — ce qui est essentiel pour caractériser un binaire même strippé :

```python
def audit_imports(path):
    binary = lief.parse(path)
    imported = [sym.name for sym in binary.imported_symbols if sym.name]
    print(f"Imports de {path} ({len(imported)} symboles) :")
    for name in sorted(imported):
        print(f"  {name}")
    return imported

# Sur crypto_O0, on verra : EVP_EncryptInit_ex, SHA256, RAND_bytes, etc.
# Sur keygenme_O0 : strcmp, printf, fgets, strlen, etc.
```

Ce script, appliqué à `crypto_O0`, révélera immédiatement les fonctions OpenSSL utilisées — `EVP_EncryptInit_ex`, `EVP_EncryptUpdate`, `EVP_EncryptFinal_ex`, `SHA256`, `RAND_bytes` — ce qui permet d'identifier en un instant que le binaire fait du chiffrement AES via l'API EVP d'OpenSSL.

### Triage automatique multi-binaires

En combinant les fonctionnalités de `lief`, on peut construire un script de triage qui produit un rapport structuré pour chaque binaire d'un répertoire :

```python
import lief  
import json  
import sys  
from pathlib import Path  

def triage(path):
    """Triage rapide d'un binaire ELF. Retourne un dict JSON-serializable."""
    binary = lief.parse(str(path))
    if binary is None:
        return {"path": str(path), "error": "not a valid binary"}

    # Fonctions importées remarquables (crypto, réseau, dangereux)
    imports = {sym.name for sym in binary.imported_symbols if sym.name}

    crypto_markers = imports & {
        "EVP_EncryptInit_ex", "EVP_DecryptInit_ex", "EVP_CipherInit_ex",
        "SHA256", "SHA1", "MD5", "AES_encrypt", "AES_decrypt",
        "RAND_bytes", "EVP_aes_256_cbc", "EVP_aes_128_cbc",
    }
    network_markers = imports & {
        "socket", "connect", "bind", "listen", "accept",
        "send", "recv", "sendto", "recvfrom", "getaddrinfo",
    }

    # Sections et entropie
    sections_info = []
    for s in binary.sections:
        sections_info.append({
            "name": s.name,
            "size": s.size,
            "entropy": round(s.entropy, 2),
        })

    # Détection de protections
    report = {
        "path":       str(path),
        "type":       str(binary.header.file_type).split(".")[-1],
        "machine":    str(binary.header.machine_type).split(".")[-1],
        "entry":      f"0x{binary.entrypoint:x}",
        "pie":        binary.is_pie,
        "nx":         binary.has_nx,
        "stripped":   len(list(binary.static_symbols)) == 0,
        "num_sections": len(binary.sections),
        "sections":   sections_info,
        "needed":     [lib for lib in binary.libraries],
        "imports_count": len(imports),
        "crypto":     sorted(crypto_markers),
        "network":    sorted(network_markers),
    }
    return report

# Utilisation sur un répertoire
if __name__ == "__main__":
    target_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    results = []
    for path in sorted(target_dir.glob("*")):
        if path.is_file() and not path.suffix:
            results.append(triage(path))
    print(json.dumps(results, indent=2))
```

Lancé sur le répertoire `binaries/ch21-keygenme/`, ce script produira un JSON contenant le rapport de triage des cinq variantes. On verra apparaître clairement les différences : `keygenme_O0` a des symboles, `keygenme_strip` n'en a plus ; tous importent `strcmp` (confirmant la présence d'une comparaison de chaînes) ; aucun n'a de marqueur crypto ou réseau. Lancé sur `binaries/ch24-crypto/`, le champ `crypto` sera rempli de fonctions OpenSSL.

### Modifier un binaire : patcher l'entry point

`lief` permet de modifier les structures ELF puis de réécrire le binaire. L'exemple le plus simple est la modification de l'entry point :

```python
binary = lief.parse("keygenme_O0")  
print(f"Entry point original : 0x{binary.entrypoint:x}")  

# Supposons qu'on veuille pointer l'entry vers une autre adresse
# (en pratique, vers une fonction qu'on a identifiée lors de l'analyse)
# Ici on ne fait que démontrer le mécanisme :
original_entry = binary.entrypoint  
binary.header.entrypoint = 0x401000  # adresse fictive pour l'exemple  

binary.write("keygenme_O0_patched")  
print("Binaire patché écrit dans keygenme_O0_patched")  

# Restaurer pour ne pas casser notre binaire de travail
binary.header.entrypoint = original_entry
```

> ⚠️ Modifier l'entry point vers une adresse invalide produit un binaire qui segfaultera au lancement. En contexte RE, cette capacité sert à rediriger l'exécution vers du code injecté ou à contourner un packer qui déchiffre le code au démarrage.

### Modifier un binaire : patcher des octets

Le cas d'usage le plus fréquent en RE est le *patching* : modifier quelques octets dans `.text` pour changer le comportement du programme. On a vu au chapitre 21 que la vérification de licence dans `keygenme` se termine par un `strcmp` suivi d'un saut conditionnel (`jz` / `jnz`). Avec `lief`, on peut automatiser cette transformation.

Le principe consiste à localiser la séquence d'octets à modifier, puis à utiliser `patch_address()` pour écrire les nouveaux octets :

```python
binary = lief.parse("keygenme_O0")

# Chercher l'opcode 0x75 (jnz) suivi d'un déplacement court
# dans la section .text, proche de l'appel à strcmp
text = binary.get_section(".text")  
content = bytes(text.content)  
base_addr = text.virtual_address  

# Recherche naïve — en production, on utiliserait un désassembleur
# pour identifier précisément l'offset du saut.
# Ici, on illustre le mécanisme de patch.
offset = content.find(b"\x75")  # jnz (short jump)  
if offset >= 0:  
    target_addr = base_addr + offset
    # Remplacer jnz (0x75) par jz (0x74) — inverse la condition
    binary.patch_address(target_addr, [0x74])
    binary.write("keygenme_O0_cracked")
    print(f"Patché jnz → jz à 0x{target_addr:x}")
```

> 💡 La recherche naïve d'un opcode `0x75` dans tout `.text` n'est évidemment pas fiable — un même octet peut apparaître comme donnée immédiate ou dans un autre contexte. En production, on combinerait `lief` avec un désassembleur (`capstone`) pour localiser précisément l'instruction à patcher. Le chapitre 21, section 6 détaille l'approche manuelle avec ImHex ; ici, l'objectif est de montrer comment `lief` permet de programmer cette transformation.

### Ajouter une section

`lief` permet d'ajouter des sections à un binaire existant. C'est utile pour injecter des métadonnées d'audit (date d'analyse, hash, identifiant) ou pour préparer un binaire à recevoir du code instrumenté :

```python
import lief  
import time  

binary = lief.parse("keygenme_O0")

# Créer une section de métadonnées
meta = lief.ELF.Section()  
meta.name = ".re_audit"  
meta.type = lief.ELF.Section.TYPE.NOTE  
meta.content = list(f"Audited on {time.ctime()} by triage.py\x00".encode())  
meta.alignment = 1  

binary.add(meta)  
binary.write("keygenme_O0_audited")  

# Vérification
check = lief.parse("keygenme_O0_audited")  
audit_section = check.get_section(".re_audit")  
print(f"Section ajoutée : {audit_section.name}, {audit_section.size} octets")  
print(f"Contenu : {bytes(audit_section.content).decode(errors='replace')}")  
```

### Modifier les dépendances dynamiques

On peut ajouter ou supprimer des bibliothèques dans la liste des `NEEDED`. C'est le mécanisme programmatique derrière la technique `LD_PRELOAD` vue au chapitre 22 — sauf qu'ici, la modification est permanente dans le binaire :

```python
binary = lief.parse("crypto_O0")  
print("Bibliothèques avant :", binary.libraries)  

# Ajouter une bibliothèque (par exemple un hook custom)
binary.add_library("libhook.so")  
binary.write("crypto_O0_hooked")  

modified = lief.parse("crypto_O0_hooked")  
print("Bibliothèques après :", modified.libraries)  
```

---

## Partie C — Combiner les deux bibliothèques

En pratique, un script de triage sophistiqué utilisera les deux bibliothèques selon leurs forces respectives. Voici un pattern courant : `pyelftools` pour l'inspection fine des structures DWARF et des tables de symboles, `lief` pour le triage rapide et les transformations.

```python
import lief  
from elftools.elf.elffile import ELFFile  
from elftools.elf.sections import SymbolTableSection  

def deep_audit(path):
    """
    Audit combiné :
    - lief pour les propriétés de haut niveau et l'entropie
    - pyelftools pour les détails DWARF et la table de symboles
    """
    # --- Phase 1 : lief (vue d'ensemble) ---
    binary = lief.parse(path)
    report = {
        "path": path,
        "pie": binary.is_pie,
        "nx": binary.has_nx,
        "libraries": list(binary.libraries),
        "imports": sorted(s.name for s in binary.imported_symbols if s.name),
        "high_entropy_sections": [
            s.name for s in binary.sections if s.entropy > 6.5
        ],
    }

    # --- Phase 2 : pyelftools (détails fins) ---
    with open(path, "rb") as f:
        elf = ELFFile(f)

        # Fonctions locales (non importées)
        local_funcs = []
        for section in elf.iter_sections():
            if isinstance(section, SymbolTableSection):
                for sym in section.iter_symbols():
                    if (sym['st_info']['type'] == 'STT_FUNC'
                            and sym['st_info']['bind'] == 'STB_LOCAL'
                            and sym['st_value'] != 0):
                        local_funcs.append(sym.name)
        report["local_functions"] = sorted(local_funcs)

        # Fichiers sources (DWARF)
        if elf.has_dwarf_info():
            dwarf = elf.get_dwarf_info()
            sources = set()
            for cu in dwarf.iter_CUs():
                for die in cu.iter_DIEs():
                    if die.tag == 'DW_TAG_compile_unit':
                        name_attr = die.attributes.get('DW_AT_name')
                        if name_attr:
                            sources.add(name_attr.value.decode())
            report["source_files"] = sorted(sources)
        else:
            report["source_files"] = []

    return report
```

Sur `keygenme_O0`, le champ `local_functions` contiendra `compute_hash`, `derive_key`, `format_key`, `rotate_left`, `check_license`, `read_line` — toutes les fonctions déclarées `static` dans `keygenme.c`. Sur la variante strippée, cette liste sera vide, mais les `imports` contiendront toujours `strcmp`, `printf`, `strlen`, `fgets`, confirmant la nature du binaire. Le champ `source_files` contiendra `keygenme.c` pour les variantes compilées avec `-g`, et sera vide pour les variantes strippées.

---

## Quand utiliser quoi — résumé

| Tâche | `pyelftools` | `lief` |  
|---|---|---|  
| Lire les headers ELF | ✅ Fidèle à la spec | ✅ Plus ergonomique |  
| Parser DWARF (debug info) | ✅ Parser complet | ❌ Non supporté |  
| Inspecter `.symtab` / `.dynsym` | ✅ | ✅ |  
| Calculer l'entropie des sections | ❌ (à coder soi-même) | ✅ Intégré |  
| Détection PIE / NX / RELRO | ❌ (lecture manuelle des flags) | ✅ Propriétés directes |  
| Patcher des octets dans `.text` | ❌ Lecture seule | ✅ `patch_address()` |  
| Ajouter / supprimer une section | ❌ | ✅ `add()` / `remove()` |  
| Modifier les `NEEDED` | ❌ | ✅ `add_library()` |  
| Réécrire le binaire sur disque | ❌ | ✅ `write()` |  
| Dépendance externe minimale | ✅ Pure Python | ❌ Extension C++ |

La règle empirique est simple : si votre script ne fait que *lire*, `pyelftools` est plus prévisible et colle mieux à la sortie de `readelf`. Si votre script doit *modifier* le binaire ou si vous avez besoin de propriétés de haut niveau (entropie, PIE, NX) sans code supplémentaire, `lief` est le choix naturel. Et si vous avez besoin des deux — ce qui est fréquent — rien n'empêche de les utiliser ensemble dans le même script.

---


⏭️ [Automatiser Ghidra en headless mode (analyse batch de N binaires)](/35-automatisation-scripting/02-ghidra-headless-batch.md)
