🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 35

## Objectif

Écrire un script Python unique, `batch_analyze.py`, qui prend en argument un répertoire contenant des binaires ELF, les analyse automatiquement, et produit un rapport JSON consolidé. Le script doit mobiliser les outils et techniques vus dans les six sections du chapitre : `lief` et `pyelftools` pour le parsing (35.1), l'architecture de sortie structurée de Ghidra headless (35.2), `pwntools` pour la recherche de patterns (35.3), `yara-python` pour la détection de signatures (35.4), la logique d'audit de la section CI/CD (35.5), et l'organisation en modules réutilisables (35.6).

## Cahier des charges

**Entrée** : un chemin vers un répertoire (ex : `binaries/`). Le script parcourt récursivement tous les fichiers et ne retient que les binaires ELF valides.

**Traitement** : pour chaque binaire ELF détecté, le script collecte les informations suivantes.

*Identification* — nom du fichier, chemin, taille en octets, architecture (x86, x86-64, ARM…), type (exécutable, shared object).

*Protections* — PIE (oui/non), NX (oui/non), stack canary (présence de `__stack_chk_fail` dans les imports), RELRO (full, partial, none), strippé (oui/non), présence de sections DWARF.

*Sections* — liste des sections avec nom, taille et entropie. Signaler les sections dont l'entropie dépasse 7.0.

*Dépendances* — liste des bibliothèques dynamiques (`NEEDED`).

*Imports remarquables* — catégoriser les fonctions importées en familles : crypto (`EVP_*`, `SHA*`, `AES_*`, `RAND_bytes`…), réseau (`socket`, `connect`, `send`, `recv`…), fichiers (`fopen`, `open`, `read`, `write`…), mémoire dangereuse (`strcpy`, `strcat`, `sprintf`, `gets`…).

*Symboles* — si le binaire n'est pas strippé, compter les fonctions locales et lister les cinq plus grandes (par taille).

*YARA* — si un répertoire de règles est fourni via `--yara-rules`, scanner chaque binaire et rapporter les correspondances.

**Sortie** : un fichier JSON (via `--output`, défaut `stdout`) contenant un objet avec deux clés : `summary` (tableau résumant chaque binaire en une ligne) et `details` (rapport complet par binaire). Le script affiche aussi un résumé lisible sur stderr.

**Code de retour** : 0 si l'exécution se termine normalement, 2 en cas d'erreur fatale.

## Solution

```python
#!/usr/bin/env python3
"""
batch_analyze.py — Analyse automatique d'un répertoire de binaires ELF  
Formation Reverse Engineering — Chapitre 35, Checkpoint  

Ce script combine lief, pyelftools et yara-python pour produire un  
rapport JSON structuré couvrant l'identification, les protections,  
les sections, les dépendances, les imports et les signatures YARA  
de chaque binaire ELF trouvé dans un répertoire.  

Usage :
  python3 batch_analyze.py binaries/
  python3 batch_analyze.py binaries/ --yara-rules yara-rules/ --output report.json
  python3 batch_analyze.py binaries/ch21-keygenme/ --verbose

Dépendances :
  pip install lief pyelftools yara-python
"""

import argparse  
import json  
import sys  
import time  
from pathlib import Path  

# ── Imports avec gestion d'absence ──────────────────────────

try:
    import lief
except ImportError:
    print("ERREUR : lief requis (pip install lief)", file=sys.stderr)
    sys.exit(2)

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
except ImportError:
    print("ERREUR : pyelftools requis (pip install pyelftools)", file=sys.stderr)
    sys.exit(2)

try:
    import yara
    HAS_YARA = True
except ImportError:
    HAS_YARA = False


# ── Constantes : familles d'imports ─────────────────────────

IMPORT_FAMILIES = {
    "crypto": {
        "EVP_EncryptInit_ex", "EVP_EncryptUpdate", "EVP_EncryptFinal_ex",
        "EVP_DecryptInit_ex", "EVP_DecryptUpdate", "EVP_DecryptFinal_ex",
        "EVP_CipherInit_ex", "EVP_CIPHER_CTX_new", "EVP_CIPHER_CTX_free",
        "EVP_aes_256_cbc", "EVP_aes_128_cbc", "EVP_aes_256_gcm",
        "SHA256", "SHA256_Init", "SHA256_Update", "SHA256_Final",
        "SHA1", "SHA1_Init", "SHA1_Update", "SHA1_Final",
        "MD5", "MD5_Init", "MD5_Update", "MD5_Final",
        "AES_encrypt", "AES_decrypt", "AES_set_encrypt_key",
        "RAND_bytes", "RAND_seed",
    },
    "network": {
        "socket", "connect", "bind", "listen", "accept", "accept4",
        "send", "recv", "sendto", "recvfrom", "sendmsg", "recvmsg",
        "getaddrinfo", "gethostbyname", "inet_pton", "inet_ntop",
        "select", "poll", "epoll_create", "epoll_ctl", "epoll_wait",
        "shutdown", "setsockopt", "getsockopt",
    },
    "file_io": {
        "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "fgets",
        "open", "close", "read", "write", "lseek", "stat", "fstat",
        "opendir", "readdir", "closedir", "rename", "unlink", "mkdir",
        "mmap", "munmap",
    },
    "dangerous": {
        "gets", "strcpy", "strcat", "sprintf", "vsprintf",
        "scanf", "fscanf", "sscanf",
    },
}


# ── Détection des fichiers ELF ──────────────────────────────

def is_elf(path):
    """Vérifie qu'un fichier commence par le magic ELF (\x7FELF)."""
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except (OSError, PermissionError):
        return False


def find_elfs(directory):
    """Parcourt récursivement un répertoire et retourne les chemins ELF."""
    elfs = []
    for path in sorted(Path(directory).rglob("*")):
        if path.is_file() and is_elf(path):
            elfs.append(path)
    return elfs


# ── Analyse d'un binaire avec lief ─────────────────────────

def analyze_protections(binary):
    """Détecte PIE, NX, canary, RELRO."""
    imports = {s.name for s in binary.imported_symbols if s.name}

    # RELRO
    has_relro_seg = False
    has_bind_now = False
    for seg in binary.segments:
        if seg.type == lief.ELF.Segment.TYPE.GNU_RELRO:
            has_relro_seg = True
    try:
        for entry in binary.dynamic_entries:
            if entry.tag == lief.ELF.DynamicEntry.TAG.BIND_NOW:
                has_bind_now = True
            if entry.tag == lief.ELF.DynamicEntry.TAG.FLAGS:
                if entry.value & 0x08:
                    has_bind_now = True
    except Exception:
        pass

    if has_relro_seg and has_bind_now:
        relro = "full"
    elif has_relro_seg:
        relro = "partial"
    else:
        relro = "none"

    return {
        "pie": binary.is_pie,
        "nx": binary.has_nx,
        "canary": "__stack_chk_fail" in imports,
        "relro": relro,
    }


def analyze_sections(binary):
    """Liste les sections avec taille et entropie."""
    sections = []
    high_entropy = []
    for s in binary.sections:
        if not s.name:
            continue
        entry = {
            "name": s.name,
            "size": s.size,
            "entropy": round(s.entropy, 2),
        }
        sections.append(entry)
        if s.entropy > 7.0 and s.size > 64:
            high_entropy.append(s.name)
    return sections, high_entropy


def analyze_imports(binary):
    """Catégorise les imports par famille."""
    all_imports = {s.name for s in binary.imported_symbols if s.name}
    categorized = {}
    for family, signatures in IMPORT_FAMILIES.items():
        found = sorted(all_imports & signatures)
        if found:
            categorized[family] = found
    return categorized, len(all_imports)


def analyze_stripped(binary):
    """Détermine si le binaire est strippé et s'il contient du DWARF."""
    static_syms = list(binary.static_symbols)
    has_symtab = len(static_syms) > 0
    debug_sections = [s.name for s in binary.sections
                      if s.name.startswith(".debug_")]
    return {
        "stripped": not has_symtab,
        "static_symbols_count": len(static_syms),
        "has_dwarf": len(debug_sections) > 0,
        "debug_sections": debug_sections,
    }


# ── Analyse des symboles avec pyelftools ────────────────────

def analyze_symbols_pyelftools(path):
    """Extrait les fonctions locales et les 5 plus grandes."""
    functions = []
    try:
        with open(str(path), "rb") as f:
            elf = ELFFile(f)
            for section in elf.iter_sections():
                if not isinstance(section, SymbolTableSection):
                    continue
                for sym in section.iter_symbols():
                    if (sym['st_info']['type'] == 'STT_FUNC'
                            and sym['st_value'] != 0
                            and sym['st_size'] > 0):
                        functions.append({
                            "name": sym.name,
                            "addr": f"0x{sym['st_value']:x}",
                            "size": sym['st_size'],
                            "bind": sym['st_info']['bind'],
                        })
    except Exception:
        pass

    local = [f for f in functions if f["bind"] == "STB_LOCAL"]
    by_size = sorted(functions, key=lambda f: f["size"], reverse=True)
    top5 = [{"name": f["name"], "size": f["size"]} for f in by_size[:5]]

    return {
        "total_functions": len(functions),
        "local_functions": len(local),
        "top5_by_size": top5,
    }


# ── Scan YARA ──────────────────────────────────────────────

def compile_yara_rules(rules_dir):
    """Compile tous les .yar d'un répertoire."""
    if not HAS_YARA:
        return None
    rules_dir = Path(rules_dir)
    if not rules_dir.is_dir():
        return None
    rule_files = {}
    for i, path in enumerate(sorted(rules_dir.glob("*.yar"))):
        rule_files[f"ns_{i}"] = str(path)
    if not rule_files:
        return None
    try:
        return yara.compile(filepaths=rule_files)
    except yara.Error as e:
        print(f"AVERTISSEMENT : erreur YARA : {e}", file=sys.stderr)
        return None


def scan_yara(rules, path):
    """Scanne un binaire et retourne les noms de règles matchées."""
    if rules is None:
        return None
    try:
        matches = rules.match(str(path))
        return [m.rule for m in matches]
    except yara.Error:
        return []


# ── Analyse complète d'un binaire ──────────────────────────

def analyze_binary(path, yara_rules=None, verbose=False):
    """Point d'entrée : analyse complète d'un binaire ELF."""
    if verbose:
        print(f"  Analyse de {path.name}...", file=sys.stderr)

    binary = lief.parse(str(path))
    if binary is None:
        return {"path": str(path), "error": "lief parse failed"}

    # Identification
    ident = {
        "name": path.name,
        "path": str(path),
        "size_bytes": path.stat().st_size,
        "arch": str(binary.header.machine_type).split(".")[-1],
        "type": str(binary.header.file_type).split(".")[-1],
        "entry_point": f"0x{binary.entrypoint:x}",
    }

    # Protections
    protections = analyze_protections(binary)

    # Symboles (strippé / DWARF)
    strip_info = analyze_stripped(binary)

    # Sections
    sections, high_entropy = analyze_sections(binary)

    # Dépendances
    libraries = list(binary.libraries)

    # Imports catégorisés
    import_families, total_imports = analyze_imports(binary)

    # Symboles détaillés (pyelftools, seulement si non strippé)
    if not strip_info["stripped"]:
        symbols = analyze_symbols_pyelftools(path)
    else:
        symbols = {
            "total_functions": 0,
            "local_functions": 0,
            "top5_by_size": [],
            "note": "binary is stripped",
        }

    # YARA
    yara_matches = scan_yara(yara_rules, path)

    # Assemblage du rapport
    report = {
        "identification": ident,
        "protections": protections,
        "strip_info": strip_info,
        "sections": {
            "count": len(sections),
            "high_entropy": high_entropy,
            "details": sections,
        },
        "libraries": libraries,
        "imports": {
            "total": total_imports,
            "families": import_families,
        },
        "symbols": symbols,
    }
    if yara_matches is not None:
        report["yara"] = yara_matches

    return report


# ── Génération du rapport consolidé ────────────────────────

def build_summary(results):
    """Produit un tableau résumé d'une ligne par binaire."""
    summary = []
    for r in results:
        if "error" in r:
            summary.append({"name": r.get("path", "?"), "error": r["error"]})
            continue

        ident = r["identification"]
        prot = r["protections"]
        imp = r["imports"]
        families = list(imp["families"].keys())

        entry = {
            "name": ident["name"],
            "arch": ident["arch"],
            "size": ident["size_bytes"],
            "pie": prot["pie"],
            "nx": prot["nx"],
            "canary": prot["canary"],
            "relro": prot["relro"],
            "stripped": r["strip_info"]["stripped"],
            "libraries_count": len(r["libraries"]),
            "imports_total": imp["total"],
            "import_families": families,
            "high_entropy_sections": r["sections"]["high_entropy"],
            "functions_detected": r["symbols"]["total_functions"],
        }
        if "yara" in r:
            entry["yara_matches"] = r["yara"]

        summary.append(entry)

    return summary


def print_summary(summary, file=sys.stderr):
    """Affiche un résumé lisible sur stderr."""
    print("", file=file)
    print(f"{'Binaire':<30} {'Arch':<8} {'PIE':>4} {'NX':>3} "
          f"{'Can':>4} {'RELRO':<8} {'Strip':>6} {'Funcs':>6} "
          f"{'Libs':>5} {'Imports':>8}  Flags",
          file=file)
    print("-" * 110, file=file)

    for s in summary:
        if "error" in s:
            print(f"{s['name']:<30} ERREUR : {s['error']}", file=file)
            continue

        flags = []
        if s.get("import_families"):
            flags.extend(s["import_families"])
        if s.get("high_entropy_sections"):
            flags.append("HIGH_ENTROPY")
        if s.get("yara_matches"):
            flags.append(f"YARA({len(s['yara_matches'])})")

        pie = "✓" if s["pie"] else "✗"
        nx = "✓" if s["nx"] else "✗"
        can = "✓" if s["canary"] else "✗"
        strip = "✓" if s["stripped"] else "✗"

        print(f"{s['name']:<30} {s['arch']:<8} {pie:>4} {nx:>3} "
              f"{can:>4} {s['relro']:<8} {strip:>6} {s['functions_detected']:>6} "
              f"{s['libraries_count']:>5} {s['imports_total']:>8}  "
              f"{', '.join(flags) if flags else '-'}",
              file=file)

    print("", file=file)
    print(f"Total : {len(summary)} binaire(s) analysé(s)", file=file)


# ── Point d'entrée ─────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Analyse automatique d'un répertoire de binaires ELF",
        epilog="Chapitre 35 — Formation Reverse Engineering GNU",
    )
    parser.add_argument(
        "directory",
        help="Répertoire contenant les binaires à analyser",
    )
    parser.add_argument(
        "--output", "-o",
        help="Fichier de sortie JSON (défaut : stdout)",
        default=None,
    )
    parser.add_argument(
        "--yara-rules",
        help="Répertoire contenant les fichiers .yar",
        default=None,
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Afficher la progression sur stderr",
    )
    args = parser.parse_args()

    # Vérifier le répertoire
    target = Path(args.directory)
    if not target.is_dir():
        print(f"ERREUR : {target} n'est pas un répertoire", file=sys.stderr)
        sys.exit(2)

    # Trouver les ELF
    elfs = find_elfs(target)
    if not elfs:
        print(f"Aucun binaire ELF trouvé dans {target}", file=sys.stderr)
        sys.exit(0)

    print(f"[*] {len(elfs)} binaire(s) ELF trouvé(s) dans {target}",
          file=sys.stderr)

    # Compiler les règles YARA (si fournies)
    yara_rules = None
    if args.yara_rules:
        if not HAS_YARA:
            print("AVERTISSEMENT : yara-python non installé, "
                  "scan YARA ignoré", file=sys.stderr)
        else:
            yara_rules = compile_yara_rules(args.yara_rules)
            if yara_rules:
                print(f"[*] Règles YARA compilées depuis {args.yara_rules}",
                      file=sys.stderr)

    # Analyser chaque binaire
    results = []
    for path in elfs:
        report = analyze_binary(path, yara_rules, args.verbose)
        results.append(report)

    # Construire le rapport final
    summary = build_summary(results)

    final_report = {
        "metadata": {
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "directory": str(target.resolve()),
            "binaries_found": len(elfs),
            "yara_rules": args.yara_rules,
        },
        "summary": summary,
        "details": {r["identification"]["name"]: r
                    for r in results if "identification" in r},
    }

    # Afficher le résumé lisible
    print_summary(summary)

    # Écrire le JSON
    json_output = json.dumps(final_report, indent=2, ensure_ascii=False)
    if args.output:
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        with open(args.output, "w") as f:
            f.write(json_output)
        print(f"[+] Rapport écrit dans {args.output}", file=sys.stderr)
    else:
        print(json_output)


if __name__ == "__main__":
    main()
```

## Exécution sur les binaires de la formation

### Scan minimal (sans YARA)

```bash
python3 batch_analyze.py binaries/ch21-keygenme/
```

Le script détecte les cinq variantes du keygenme. Le résumé sur stderr ressemble à ceci :

```
[*] 5 binaire(s) ELF trouvé(s) dans binaries/ch21-keygenme/

Binaire                        Arch     PIE   NX  Can  RELRO    Strip  Funcs  Libs  Imports  Flags
--------------------------------------------------------------------------------------------------------------
keygenme_O0                    X86_64     ✓    ✓    ✗  partial      ✗     12      1       10  -  
keygenme_O2                    X86_64     ✓    ✓    ✗  partial      ✗      9      1       10  -  
keygenme_O3                    X86_64     ✓    ✓    ✗  partial      ✗      8      1       10  -  
keygenme_O2_strip              X86_64     ✓    ✓    ✗  partial      ✓      0      1       10  -  
keygenme_strip                 X86_64     ✓    ✓    ✗  partial      ✓      0      1       10  -  

Total : 5 binaire(s) analysé(s)
```

Plusieurs observations exploitables apparaissent immédiatement. La colonne `Funcs` passe de 12 (`_O0`) à 9 (`_O2`) puis 8 (`_O3`) — l'inlining progressif des fonctions par le compilateur est visible quantitativement. Les variantes strippées montrent 0 fonctions. Aucune variante n'a de canary (le Makefile ne passe pas `-fstack-protector`). Le RELRO est partiel (pas de `-Wl,-z,now`).

### Scan complet (avec YARA)

```bash
python3 batch_analyze.py binaries/ \
    --yara-rules yara-rules/ \
    --output report.json \
    --verbose
```

Le rapport JSON produit est exploitable directement avec `jq` :

```bash
# Quels binaires ont des imports crypto ?
jq '.summary[] | select(.import_families | index("crypto"))
    | .name' report.json

# Quels binaires sont strippés ET sans canary ?
jq '.summary[] | select(.stripped and (.canary | not))
    | {name, relro}' report.json

# Quels binaires ont déclenché des règles YARA ?
jq '.summary[] | select(.yara_matches | length > 0)
    | {name, yara_matches}' report.json

# Sections à haute entropie (suspicion de packing) ?
jq '.summary[] | select(.high_entropy_sections | length > 0)
    | {name, high_entropy_sections}' report.json
```

## Correspondance avec les sections du chapitre

| Composant du script | Section de référence |  
|---|---|  
| `lief.parse()`, `analyze_protections()`, `analyze_sections()` | 35.1 — `pyelftools` et `lief` |  
| Sortie JSON structurée avec `metadata` / `summary` / `details` | 35.2 — Architecture des rapports Ghidra headless |  
| `analyze_symbols_pyelftools()`, recherche de patterns dans les imports | 35.3 — `pwntools` (pattern de catégorisation) |  
| `compile_yara_rules()`, `scan_yara()` | 35.4 — Règles YARA |  
| `analyze_protections()` comme assertions, politique de whitelist implicite | 35.5 — Pipeline CI/CD |  
| Séparation en fonctions réutilisables, gestion des dépendances optionnelles | 35.6 — Construction du toolkit |

## Critères de validation

Le script est considéré fonctionnel s'il satisfait les conditions suivantes.

**Détection** — il identifie correctement tous les binaires ELF d'un répertoire et ignore les fichiers non-ELF (sources `.c`, Makefiles, fichiers `.hexpat`, archives `.cfr`).

**Protections** — les valeurs PIE, NX, canary, RELRO et strippé correspondent à celles rapportées par `checksec` sur les mêmes binaires.

**Sections** — l'entropie des sections est cohérente (`.text` entre 5 et 6.5, `.rodata` entre 3 et 5 pour un binaire GCC standard non packé).

**Imports** — les familles sont correctement détectées : `crypto` présent pour les variantes de `crypto_O0`, `network` présent pour les binaires du chapitre 23, `dangerous` présent si le binaire utilise `gets` ou `strcpy`.

**YARA** — si le répertoire `yara-rules/` est fourni, les règles `crypto_constants.yar` et `packer_signatures.yar` sont compilées et appliquées. Les binaires du chapitre 24 déclenchent les règles crypto. Les binaires du chapitre 29 (packés UPX) déclenchent les règles packer.

**Symboles** — les variantes non strippées affichent un nombre de fonctions cohérent (diminuant avec le niveau d'optimisation). Les variantes strippées affichent zéro.

**Sortie** — le JSON est valide, parsable par `jq`, et contient les trois clés `metadata`, `summary`, `details`. Le résumé stderr est lisible et aligné.

---

> ✅ **Ce checkpoint valide l'ensemble du chapitre 35.** Le script `batch_analyze.py` est la brique centrale du toolkit RE automatisé — il peut être étendu avec de nouvelles vérifications, intégré dans un pipeline CI/CD, ou utilisé comme point de départ pour une analyse plus approfondie avec Ghidra headless.

---


⏭️ [Chapitre 36 — Ressources pour progresser](/36-ressources-progresser/README.md)
