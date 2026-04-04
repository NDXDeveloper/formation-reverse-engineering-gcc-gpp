#!/usr/bin/env python3
"""
triage.py — Triage automatique d'un binaire ELF
Formation Reverse Engineering — Applications compilées avec la chaîne GNU

Première prise de contact programmatique avec un binaire inconnu.
Combine lief (protections, entropie, imports) et pyelftools (symboles, DWARF)
pour produire un rapport de triage structuré — l'équivalent scriptable du
workflow manuel « 5 premières minutes » du chapitre 5, section 7.

Usage :
  python3 triage.py <binary>
  python3 triage.py <binary> --json
  python3 triage.py <binary> --json --output rapport.json

Dépendances :
  pip install lief pyelftools

Licence MIT — Usage strictement éducatif.
"""

import argparse
import json
import sys
import struct
from pathlib import Path

try:
    import lief
    lief.logging.disable()
except ImportError:
    print("ERREUR : lief requis (pip install lief)", file=sys.stderr)
    sys.exit(2)

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
    from elftools.elf.dynamic import DynamicSection
except ImportError:
    print("ERREUR : pyelftools requis (pip install pyelftools)", file=sys.stderr)
    sys.exit(2)


# ═══════════════════════════════════════════════════════════════
#  Constantes
# ═══════════════════════════════════════════════════════════════

# Imports classés par famille (même dictionnaire que le checkpoint)
IMPORT_FAMILIES = {
    "crypto": {
        "EVP_EncryptInit_ex", "EVP_EncryptUpdate", "EVP_EncryptFinal_ex",
        "EVP_DecryptInit_ex", "EVP_DecryptUpdate", "EVP_DecryptFinal_ex",
        "EVP_CipherInit_ex", "EVP_CIPHER_CTX_new", "EVP_CIPHER_CTX_free",
        "EVP_aes_256_cbc", "EVP_aes_128_cbc", "EVP_aes_256_gcm",
        "SHA256", "SHA256_Init", "SHA256_Update", "SHA256_Final",
        "SHA1", "MD5", "AES_encrypt", "AES_decrypt",
        "RAND_bytes", "RAND_seed",
    },
    "network": {
        "socket", "connect", "bind", "listen", "accept",
        "send", "recv", "sendto", "recvfrom",
        "getaddrinfo", "gethostbyname",
    },
    "file_io": {
        "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "fgets",
        "open", "close", "read", "write", "lseek",
        "stat", "fstat", "mmap", "munmap",
    },
    "dangerous": {
        "gets", "strcpy", "strcat", "sprintf", "vsprintf",
    },
    "process": {
        "fork", "execve", "execvp", "system", "popen",
        "ptrace", "kill", "signal",
    },
    "dynamic_loading": {
        "dlopen", "dlsym", "dlclose",
    },
}

# Constantes crypto connues (recherche dans le binaire brut)
CRYPTO_SIGNATURES = {
    "AES S-box (row 0)": bytes.fromhex("637c777bf26b6fc53001672bfed7ab76"),
    "SHA-256 H0 (BE)":   bytes.fromhex("6a09e667"),
    "SHA-256 H0 (LE)":   bytes.fromhex("67e6096a"),
    "SHA-256 K[0] (BE)": bytes.fromhex("428a2f98"),
    "MD5 T[1] (LE)":     bytes.fromhex("78a46ad7"),
    "ChaCha20 sigma":    b"expand 32-byte k",
    # Marqueurs spécifiques à nos binaires d'entraînement
    "CRYPT24 magic":     b"CRYPT24",
    "CFR magic":         b"CFRM",
    "ch24 KEY_MASK":     bytes.fromhex("deadbeefcafebabe"),
    "ch21 HASH_SEED":    struct.pack("<I", 0x5A3C6E2D),
}

# Seuil d'entropie pour alerte
ENTROPY_THRESHOLD = 7.0


# ═══════════════════════════════════════════════════════════════
#  Analyse lief
# ═══════════════════════════════════════════════════════════════

def triage_lief(path):
    """Analyse via lief : identification, protections, sections, imports."""
    binary = lief.parse(str(path))
    if binary is None:
        return None

    # ── Identification ──
    ident = {
        "name":        path.name,
        "path":        str(path.resolve()),
        "size_bytes":  path.stat().st_size,
        "arch":        str(binary.header.machine_type).split(".")[-1],
        "type":        str(binary.header.file_type).split(".")[-1],
        "entry_point": f"0x{binary.entrypoint:x}",
    }

    # ── Protections ──
    imports = {s.name for s in binary.imported_symbols if s.name}

    has_relro_seg = any(
        seg.type == lief.ELF.Segment.TYPE.GNU_RELRO
        for seg in binary.segments
    )
    has_bind_now = False
    try:
        for entry in binary.dynamic_entries:
            if entry.tag == lief.ELF.DynamicEntry.TAG.BIND_NOW:
                has_bind_now = True
            if entry.tag == lief.ELF.DynamicEntry.TAG.FLAGS:
                if entry.value & 0x08:
                    has_bind_now = True
            if entry.tag == lief.ELF.DynamicEntry.TAG.FLAGS_1:
                if entry.value & 0x01:
                    has_bind_now = True
    except Exception:
        pass

    if has_relro_seg and has_bind_now:
        relro = "full"
    elif has_relro_seg:
        relro = "partial"
    else:
        relro = "none"

    protections = {
        "pie":    binary.is_pie,
        "nx":     binary.has_nx,
        "canary": "__stack_chk_fail" in imports,
        "relro":  relro,
    }

    # ── Stripped / DWARF ──
    static_syms = list(binary.static_symbols)
    debug_sections = sorted(
        s.name for s in binary.sections if s.name.startswith(".debug_")
    )
    strip_info = {
        "stripped":              len(static_syms) == 0,
        "static_symbols_count": len(static_syms),
        "has_dwarf":            len(debug_sections) > 0,
        "debug_sections":       debug_sections,
    }

    # ── Sections + entropie ──
    sections = []
    high_entropy = []
    for s in binary.sections:
        if not s.name:
            continue
        ent = round(s.entropy, 2)
        sections.append({
            "name": s.name, "size": s.size, "entropy": ent,
        })
        if ent > ENTROPY_THRESHOLD and s.size > 64:
            high_entropy.append(f"{s.name} (entropy={ent})")

    # ── Bibliothèques ──
    libraries = list(binary.libraries)

    # ── Imports catégorisés ──
    all_imports = {s.name for s in binary.imported_symbols if s.name}
    import_families = {}
    for family, sigs in IMPORT_FAMILIES.items():
        found = sorted(all_imports & sigs)
        if found:
            import_families[family] = found

    # ── Recherche de constantes crypto dans le binaire brut ──
    raw = path.read_bytes()
    crypto_findings = []
    for label, pattern in CRYPTO_SIGNATURES.items():
        offset = raw.find(pattern)
        if offset >= 0:
            crypto_findings.append({
                "label":  label,
                "offset": f"0x{offset:x}",
            })

    # ── Strings remarquables (extraites de .rodata) ──
    notable_strings = []
    rodata = binary.get_section(".rodata")
    if rodata:
        content = bytes(rodata.content)
        # Extraction simplifiée : séquences ASCII imprimables ≥ 6 caractères
        current = []
        for byte in content:
            if 32 <= byte < 127:
                current.append(chr(byte))
            else:
                if len(current) >= 6:
                    notable_strings.append("".join(current))
                current = []
        if len(current) >= 6:
            notable_strings.append("".join(current))

    return {
        "identification":    ident,
        "protections":       protections,
        "strip_info":        strip_info,
        "sections": {
            "count":         len(sections),
            "high_entropy":  high_entropy,
            "details":       sections,
        },
        "libraries":         libraries,
        "imports": {
            "total":         len(all_imports),
            "families":      import_families,
        },
        "crypto_signatures": crypto_findings,
        "notable_strings":   notable_strings[:50],  # limiter le volume
    }


# ═══════════════════════════════════════════════════════════════
#  Analyse pyelftools (symboles détaillés)
# ═══════════════════════════════════════════════════════════════

def triage_pyelftools(path):
    """Analyse via pyelftools : fonctions, DWARF, NEEDED."""
    result = {
        "functions": [],
        "source_files": [],
    }

    try:
        with open(str(path), "rb") as f:
            elf = ELFFile(f)

            # ── Fonctions ──
            for section in elf.iter_sections():
                if not isinstance(section, SymbolTableSection):
                    continue
                for sym in section.iter_symbols():
                    if (sym['st_info']['type'] == 'STT_FUNC'
                            and sym['st_value'] != 0
                            and sym['st_size'] > 0):
                        result["functions"].append({
                            "name": sym.name,
                            "addr": f"0x{sym['st_value']:x}",
                            "size": sym['st_size'],
                            "bind": sym['st_info']['bind'],
                        })

            result["functions"].sort(key=lambda fn: fn.get("size", 0),
                                     reverse=True)

            # ── DWARF : fichiers sources ──
            if elf.has_dwarf_info():
                dwarf = elf.get_dwarf_info()
                sources = set()
                for cu in dwarf.iter_CUs():
                    for die in cu.iter_DIEs():
                        if die.tag == 'DW_TAG_compile_unit':
                            name = die.attributes.get('DW_AT_name')
                            if name:
                                sources.add(name.value.decode(errors='replace'))
                result["source_files"] = sorted(sources)

    except Exception as e:
        result["error"] = str(e)

    return result


# ═══════════════════════════════════════════════════════════════
#  Assemblage et affichage
# ═══════════════════════════════════════════════════════════════

def triage(path):
    """Point d'entrée : triage complet d'un binaire ELF."""
    path = Path(path)

    if not path.is_file():
        return {"error": f"File not found: {path}"}

    # Vérifier le magic ELF
    with open(path, "rb") as f:
        if f.read(4) != b"\x7fELF":
            return {"error": f"Not an ELF file: {path}"}

    report = triage_lief(path)
    if report is None:
        return {"error": f"lief failed to parse: {path}"}

    # Enrichir avec pyelftools si non strippé
    if not report["strip_info"]["stripped"]:
        pe = triage_pyelftools(path)
        report["symbols"] = {
            "total_functions":  len(pe["functions"]),
            "local_functions":  len([f for f in pe["functions"]
                                     if f["bind"] == "STB_LOCAL"]),
            "global_functions": len([f for f in pe["functions"]
                                     if f["bind"] == "STB_GLOBAL"]),
            "top5_by_size":     pe["functions"][:5],
        }
        report["source_files"] = pe["source_files"]
    else:
        report["symbols"] = {
            "total_functions": 0,
            "note": "binary is stripped",
        }
        report["source_files"] = []

    return report


def print_human_report(report):
    """Affiche un rapport lisible sur stderr."""
    if "error" in report:
        print(f"ERREUR : {report['error']}", file=sys.stderr)
        return

    ident = report["identification"]
    prot = report["protections"]
    strip = report["strip_info"]

    def tick(val):
        return "✓" if val else "✗"

    print(f"\n{'═' * 60}", file=sys.stderr)
    print(f"  TRIAGE — {ident['name']}", file=sys.stderr)
    print(f"{'═' * 60}\n", file=sys.stderr)

    print(f"  Chemin     : {ident['path']}", file=sys.stderr)
    print(f"  Taille     : {ident['size_bytes']:,} octets", file=sys.stderr)
    print(f"  Arch       : {ident['arch']}", file=sys.stderr)
    print(f"  Type       : {ident['type']}", file=sys.stderr)
    print(f"  Entry      : {ident['entry_point']}", file=sys.stderr)

    print(f"\n  Protections :", file=sys.stderr)
    print(f"    PIE      : {tick(prot['pie'])}", file=sys.stderr)
    print(f"    NX       : {tick(prot['nx'])}", file=sys.stderr)
    print(f"    Canary   : {tick(prot['canary'])}", file=sys.stderr)
    print(f"    RELRO    : {prot['relro']}", file=sys.stderr)
    print(f"    Stripped  : {tick(strip['stripped'])}", file=sys.stderr)
    print(f"    DWARF    : {tick(strip['has_dwarf'])}", file=sys.stderr)

    # Bibliothèques
    libs = report["libraries"]
    print(f"\n  Bibliothèques ({len(libs)}) :", file=sys.stderr)
    for lib in libs:
        print(f"    - {lib}", file=sys.stderr)

    # Imports
    imp = report["imports"]
    print(f"\n  Imports ({imp['total']} total) :", file=sys.stderr)
    for family, funcs in imp["families"].items():
        print(f"    [{family}] {', '.join(funcs)}", file=sys.stderr)

    # Symboles
    sym = report.get("symbols", {})
    if sym.get("total_functions", 0) > 0:
        print(f"\n  Fonctions ({sym['total_functions']} total, "
              f"{sym['local_functions']} local, "
              f"{sym['global_functions']} global) :", file=sys.stderr)
        print(f"    Top 5 par taille :", file=sys.stderr)
        for fn in sym.get("top5_by_size", []):
            print(f"      {fn['addr']}  {fn['size']:>5}B  {fn['name']}",
                  file=sys.stderr)

    # Sources DWARF
    sources = report.get("source_files", [])
    if sources:
        print(f"\n  Fichiers sources (DWARF) :", file=sys.stderr)
        for src in sources:
            print(f"    - {src}", file=sys.stderr)

    # Constantes crypto
    crypto = report.get("crypto_signatures", [])
    if crypto:
        print(f"\n  Constantes crypto détectées :", file=sys.stderr)
        for c in crypto:
            print(f"    - {c['label']} @ {c['offset']}", file=sys.stderr)

    # Sections à haute entropie
    high_ent = report["sections"]["high_entropy"]
    if high_ent:
        print(f"\n  ⚠ Sections à haute entropie :", file=sys.stderr)
        for s in high_ent:
            print(f"    - {s}", file=sys.stderr)

    # Strings remarquables (10 premières)
    strings = report.get("notable_strings", [])
    if strings:
        print(f"\n  Strings remarquables ({len(strings)} trouvées, "
              f"10 premières) :", file=sys.stderr)
        for s in strings[:10]:
            display = s if len(s) <= 70 else s[:67] + "..."
            print(f"    \"{display}\"", file=sys.stderr)

    print(f"\n{'═' * 60}\n", file=sys.stderr)


# ═══════════════════════════════════════════════════════════════
#  Point d'entrée
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="Triage automatique d'un binaire ELF",
        epilog="Formation RE — Chapitre 35",
    )
    parser.add_argument("binary", help="Chemin vers le binaire à analyser")
    parser.add_argument("--json", action="store_true",
                        help="Sortie JSON sur stdout (défaut : humain sur stderr)")
    parser.add_argument("--output", "-o",
                        help="Fichier de sortie JSON")
    args = parser.parse_args()

    report = triage(args.binary)

    # Toujours afficher le rapport humain sur stderr
    print_human_report(report)

    # Sortie JSON si demandé
    if args.json or args.output:
        json_str = json.dumps(report, indent=2, ensure_ascii=False)
        if args.output:
            Path(args.output).parent.mkdir(parents=True, exist_ok=True)
            with open(args.output, "w") as f:
                f.write(json_str + "\n")
            print(f"[+] JSON → {args.output}", file=sys.stderr)
        else:
            print(json_str)

    # Code de retour
    sys.exit(2 if "error" in report else 0)


if __name__ == "__main__":
    main()
