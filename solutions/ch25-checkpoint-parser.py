#!/usr/bin/env python3
"""
cfr_parser.py — Parser/sérialiseur complet pour le format CFR v2.

Solution du checkpoint du chapitre 25.
Licence MIT — Usage strictement éducatif.

Usage:
    python3 cfr_parser.py parse     <archive.cfr>
    python3 cfr_parser.py validate  <archive.cfr>
    python3 cfr_parser.py create    [-x] <output.cfr> <file1> [file2 ...]
    python3 cfr_parser.py unpack    <archive.cfr> [output_dir]
    python3 cfr_parser.py roundtrip <archive.cfr> [binary_path]
    python3 cfr_parser.py test      [binary_path]
"""

import binascii
import io
import os
import struct
import subprocess
import sys
import time
from dataclasses import dataclass, field
from enum import IntEnum, IntFlag
from pathlib import Path
from typing import List, Optional

# ============================================================
#  Constants
# ============================================================

HEADER_MAGIC = b"CFRM"
FOOTER_MAGIC = b"CRFE"
FORMAT_VERSION = 0x0002

HEADER_SIZE = 32
FOOTER_SIZE = 12
REC_HEADER_SIZE = 8
MAX_RECORDS = 1024
AUTHOR_LEN = 8
XOR_KEY = bytes([0x5A, 0x3C, 0x96, 0xF1])

# ============================================================
#  Enums
# ============================================================

class RecordType(IntEnum):
    TEXT   = 0x01
    BINARY = 0x02
    META   = 0x03

class HeaderFlags(IntFlag):
    XOR_ENABLED = 1 << 0
    HAS_FOOTER  = 1 << 1

# ============================================================
#  CRC-32 (ISO 3309 / zlib)
# ============================================================

def crc32(data: bytes) -> int:
    """CRC-32 standard, identique à binascii.crc32 / zlib.crc32."""
    return binascii.crc32(data) & 0xFFFFFFFF

# ============================================================
#  CRC-16/CCITT (poly=0x1021, init=0x1D0F, xorOut=0x0000)
# ============================================================

def crc16_ccitt(data: bytes, init: int = 0x1D0F) -> int:
    """CRC-16 AUG-CCITT avec valeur initiale non standard 0x1D0F."""
    crc = init
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

# ============================================================
#  XOR rotatif (clé fixe 4 octets, involutif)
# ============================================================

def xor_transform(data: bytes) -> bytes:
    """Applique/retire le XOR rotatif. L'opération est son propre inverse."""
    kl = len(XOR_KEY)
    return bytes(b ^ XOR_KEY[i % kl] for i, b in enumerate(data))

# ============================================================
#  Data classes
# ============================================================

@dataclass
class CFRHeader:
    magic: bytes
    version: int
    flags: HeaderFlags
    num_records: int
    timestamp: int
    header_crc: int
    author: str
    data_len_xor: int

@dataclass
class CFRRecord:
    rec_type: int
    rec_flags: int
    name: str
    data: bytes          # données originales (après dé-XOR si applicable)
    stored_crc16: int

@dataclass
class CFRFooter:
    magic: bytes
    total_size: int
    global_crc: int

@dataclass
class CFRArchive:
    header: CFRHeader
    records: List[CFRRecord] = field(default_factory=list)
    footer: Optional[CFRFooter] = None

# ============================================================
#  Exceptions
# ============================================================

class CFRParseError(Exception):
    """Erreur lors du parsing d'une archive CFR."""
    pass

# ============================================================
#  Parser
# ============================================================

def parse_cfr(filepath: str, strict: bool = True) -> CFRArchive:
    """
    Parse une archive CFR complète.

    Args:
        filepath: chemin vers le fichier .cfr
        strict:   si True, lève une exception sur toute violation d'intégrité.
                  si False, émet des warnings mais continue.

    Returns:
        CFRArchive contenant le header, les records (données dé-XOR-ées)
        et le footer optionnel.
    """
    with open(filepath, "rb") as f:
        raw = f.read()

    if len(raw) < HEADER_SIZE:
        raise CFRParseError(
            f"Fichier trop court ({len(raw)} octets, minimum {HEADER_SIZE})"
        )

    buf = io.BytesIO(raw)

    # ── Header ──────────────────────────────────────────────
    hdr_bytes = buf.read(HEADER_SIZE)

    magic       = hdr_bytes[0:4]
    version     = struct.unpack_from("<H", hdr_bytes, 4)[0]
    flags       = HeaderFlags(struct.unpack_from("<H", hdr_bytes, 6)[0])
    num_records = struct.unpack_from("<I", hdr_bytes, 8)[0]
    timestamp   = struct.unpack_from("<I", hdr_bytes, 12)[0]
    header_crc  = struct.unpack_from("<I", hdr_bytes, 16)[0]
    author_raw  = hdr_bytes[20:28]
    dlx_stored  = struct.unpack_from("<I", hdr_bytes, 28)[0]

    # V1 — Magic
    if magic != HEADER_MAGIC:
        raise CFRParseError(f"Magic invalide : {magic!r} (attendu {HEADER_MAGIC!r})")

    # V2 — Nombre de records
    if num_records > MAX_RECORDS:
        raise CFRParseError(
            f"Trop de records : {num_records} (max {MAX_RECORDS})"
        )

    # V3 — CRC header (sur les 16 premiers octets)
    expected_hcrc = crc32(hdr_bytes[:16])
    if header_crc != expected_hcrc:
        msg = (f"Header CRC invalide : "
               f"stocké=0x{header_crc:08X} calculé=0x{expected_hcrc:08X}")
        if strict:
            raise CFRParseError(msg)
        print(f"[WARN] {msg}", file=sys.stderr)

    author = author_raw.rstrip(b"\x00").decode("ascii", errors="replace")

    header = CFRHeader(
        magic=magic, version=version, flags=flags,
        num_records=num_records, timestamp=timestamp,
        header_crc=header_crc, author=author,
        data_len_xor=dlx_stored,
    )

    do_xor = bool(flags & HeaderFlags.XOR_ENABLED)

    # ── Records ─────────────────────────────────────────────
    records: List[CFRRecord] = []
    dlx_computed = 0

    for i in range(num_records):
        pos = buf.tell()

        # Record header (8 octets)
        rh = buf.read(REC_HEADER_SIZE)
        if len(rh) < REC_HEADER_SIZE:
            raise CFRParseError(f"Record {i} : en-tête tronqué (offset 0x{pos:X})")

        rec_type  = rh[0]
        rec_flags = rh[1]
        name_len  = struct.unpack_from("<H", rh, 2)[0]
        data_len  = struct.unpack_from("<I", rh, 4)[0]

        # Name
        name_bytes = buf.read(name_len)
        if len(name_bytes) < name_len:
            raise CFRParseError(f"Record {i} : nom tronqué")

        # Data (potentiellement XOR-ée)
        raw_data = buf.read(data_len)
        if len(raw_data) < data_len:
            raise CFRParseError(f"Record {i} : données tronquées")

        # CRC-16
        crc_bytes = buf.read(2)
        if len(crc_bytes) < 2:
            raise CFRParseError(f"Record {i} : CRC-16 tronqué")
        stored_crc16 = struct.unpack("<H", crc_bytes)[0]

        # Dé-XOR
        if do_xor and data_len > 0:
            plain_data = xor_transform(raw_data)
        else:
            plain_data = raw_data

        # V5 — CRC-16 du record (sur name + data originales)
        expected_crc16 = crc16_ccitt(name_bytes + plain_data)
        if stored_crc16 != expected_crc16:
            name_str = name_bytes.decode("ascii", errors="replace")
            msg = (f"Record {i} ({name_str!r}) : CRC-16 invalide : "
                   f"stocké=0x{stored_crc16:04X} calculé=0x{expected_crc16:04X}")
            if strict:
                raise CFRParseError(msg)
            print(f"[WARN] {msg}", file=sys.stderr)

        name = name_bytes.decode("ascii", errors="replace")
        records.append(CFRRecord(
            rec_type=rec_type, rec_flags=rec_flags,
            name=name, data=plain_data,
            stored_crc16=stored_crc16,
        ))

        dlx_computed ^= data_len

    # V6 — data_len_xor
    if dlx_computed != dlx_stored:
        msg = (f"data_len_xor invalide : "
               f"stocké=0x{dlx_stored:08X} calculé=0x{dlx_computed:08X}")
        if strict:
            raise CFRParseError(msg)
        print(f"[WARN] {msg}", file=sys.stderr)

    # ── Footer ──────────────────────────────────────────────
    footer = None
    if flags & HeaderFlags.HAS_FOOTER:
        ftr_bytes = buf.read(FOOTER_SIZE)
        if len(ftr_bytes) < FOOTER_SIZE:
            msg = "Footer attendu (flag HAS_FOOTER) mais fichier tronqué"
            if strict:
                raise CFRParseError(msg)
            print(f"[WARN] {msg}", file=sys.stderr)
        else:
            ftr_magic = ftr_bytes[0:4]
            ftr_total = struct.unpack_from("<I", ftr_bytes, 4)[0]
            ftr_crc   = struct.unpack_from("<I", ftr_bytes, 8)[0]

            # V7 — Footer magic
            if ftr_magic != FOOTER_MAGIC:
                msg = f"Footer magic invalide : {ftr_magic!r}"
                if strict:
                    raise CFRParseError(msg)
                print(f"[WARN] {msg}", file=sys.stderr)

            # V8 — total_size
            if ftr_total != len(raw):
                msg = (f"Footer total_size incohérent : "
                       f"stocké={ftr_total} réel={len(raw)}")
                if strict:
                    raise CFRParseError(msg)
                print(f"[WARN] {msg}", file=sys.stderr)

            # V9 — Global CRC
            payload = raw[:len(raw) - FOOTER_SIZE]
            expected_gcrc = crc32(payload)
            if ftr_crc != expected_gcrc:
                msg = (f"Footer CRC global invalide : "
                       f"stocké=0x{ftr_crc:08X} calculé=0x{expected_gcrc:08X}")
                if strict:
                    raise CFRParseError(msg)
                print(f"[WARN] {msg}", file=sys.stderr)

            footer = CFRFooter(
                magic=ftr_magic,
                total_size=ftr_total,
                global_crc=ftr_crc,
            )

    return CFRArchive(header=header, records=records, footer=footer)

# ============================================================
#  Sérialiseur
# ============================================================

def serialize_cfr(
    records: List[CFRRecord],
    author: str = "python",
    xor_enabled: bool = True,
    include_footer: bool = True,
    timestamp: Optional[int] = None,
) -> bytes:
    """
    Sérialise une liste de records en une archive CFR v2 complète.

    L'ordre des opérations par record est :
      1. Calculer CRC-16 sur (name || data_original)
      2. Si XOR : transformer data_stored = XOR(data_original)
      3. Écrire : rec_header || name || data_stored || crc16

    Args:
        records:        liste de CFRRecord à inclure.
        author:         auteur (8 caractères max, ASCII).
        xor_enabled:    activer la transformation XOR sur les données.
        include_footer: inclure un footer avec CRC global.
        timestamp:      timestamp UNIX (défaut : now).

    Returns:
        bytes de l'archive CFR complète.
    """
    if len(records) > MAX_RECORDS:
        raise ValueError(f"Trop de records : {len(records)} (max {MAX_RECORDS})")

    flags = HeaderFlags(0)
    if xor_enabled:
        flags |= HeaderFlags.XOR_ENABLED
    if include_footer:
        flags |= HeaderFlags.HAS_FOOTER

    ts = timestamp if timestamp is not None else int(time.time())

    # ── Sérialiser les records ──────────────────────────────
    rec_buf = io.BytesIO()
    dlx = 0

    for rec in records:
        name_bytes = rec.name.encode("ascii")
        data_len = len(rec.data)
        dlx ^= data_len

        # Record header
        rec_buf.write(struct.pack(
            "<BBHI", rec.rec_type, rec.rec_flags, len(name_bytes), data_len
        ))

        # Name (jamais transformé)
        rec_buf.write(name_bytes)

        # CRC-16 sur name + data originales (AVANT XOR)
        rec_crc = crc16_ccitt(name_bytes + rec.data)

        # Data (XOR si flag actif)
        if xor_enabled and data_len > 0:
            rec_buf.write(xor_transform(rec.data))
        else:
            rec_buf.write(rec.data)

        # CRC-16
        rec_buf.write(struct.pack("<H", rec_crc))

    records_bytes = rec_buf.getvalue()

    # ── Header ──────────────────────────────────────────────
    author_padded = author.encode("ascii")[:AUTHOR_LEN].ljust(AUTHOR_LEN, b"\x00")
    dlx_bytes = struct.pack("<I", dlx)

    # Premiers 16 octets (couverts par le header CRC)
    hdr_prefix = struct.pack(
        "<4sHHII", HEADER_MAGIC, FORMAT_VERSION, int(flags), len(records), ts
    )
    assert len(hdr_prefix) == 16

    hdr_crc = crc32(hdr_prefix)
    header_bytes = hdr_prefix + struct.pack("<I", hdr_crc) + author_padded + dlx_bytes
    assert len(header_bytes) == HEADER_SIZE

    # ── Assemblage ──────────────────────────────────────────
    payload = header_bytes + records_bytes

    if include_footer:
        total_size = len(payload) + FOOTER_SIZE
        global_crc = crc32(payload)
        footer_bytes = struct.pack("<4sII", FOOTER_MAGIC, total_size, global_crc)
        return payload + footer_bytes

    return payload

# ============================================================
#  Helpers pour créer des records
# ============================================================

def make_text_record(name: str, text: str) -> CFRRecord:
    return CFRRecord(RecordType.TEXT, 0, name, text.encode("utf-8"), 0)

def make_binary_record(name: str, data: bytes) -> CFRRecord:
    return CFRRecord(RecordType.BINARY, 0, name, data, 0)

def make_meta_record(name: str, metadata: dict) -> CFRRecord:
    text = "\n".join(f"{k}={v}" for k, v in metadata.items())
    return CFRRecord(RecordType.META, 0, name, text.encode("utf-8"), 0)

def guess_type(filename: str) -> int:
    ext = Path(filename).suffix.lower()
    if ext in (".txt", ".md", ".csv", ".log"):
        return RecordType.TEXT
    if ext == ".meta":
        return RecordType.META
    return RecordType.BINARY

def make_record_from_file(filepath: str) -> CFRRecord:
    """Crée un record à partir d'un fichier sur disque."""
    p = Path(filepath)
    data = p.read_bytes()
    return CFRRecord(guess_type(p.name), 0, p.name, data, 0)

# ============================================================
#  Affichage
# ============================================================

TYPE_NAMES = {0x01: "TEXT", 0x02: "BINARY", 0x03: "META"}

def type_name(t: int) -> str:
    return TYPE_NAMES.get(t, f"UNKNOWN(0x{t:02X})")

def print_archive(ar: CFRArchive) -> None:
    """Affiche le contenu d'une archive CFR de façon lisible."""
    h = ar.header
    print(f"Archive CFR v{h.version}")
    print(f"  Flags      : 0x{int(h.flags):04X}", end="")
    if h.flags & HeaderFlags.XOR_ENABLED:
        print(" [XOR]", end="")
    if h.flags & HeaderFlags.HAS_FOOTER:
        print(" [FOOTER]", end="")
    print()
    print(f"  Records    : {h.num_records}")
    print(f"  Timestamp  : {h.timestamp}")
    print(f"  Author     : {h.author!r}")
    print(f"  Header CRC : 0x{h.header_crc:08X}")
    print(f"  DLen XOR   : 0x{h.data_len_xor:08X}")
    print()

    for i, rec in enumerate(ar.records):
        print(f"  Record {i}: {rec.name!r}  [{type_name(rec.rec_type)}, "
              f"{len(rec.data)} bytes, CRC=0x{rec.stored_crc16:04X}]")

        if rec.rec_type in (RecordType.TEXT, RecordType.META):
            text = rec.data.decode("utf-8", errors="replace")
            for line in text.splitlines():
                print(f"    | {line}")
        else:
            # Hex dump (64 octets max)
            show = min(len(rec.data), 64)
            for off in range(0, show, 16):
                chunk = rec.data[off:off + 16]
                hexstr = " ".join(f"{b:02X}" for b in chunk)
                print(f"    {off:04X}: {hexstr}")
            if len(rec.data) > 64:
                print(f"    ... ({len(rec.data)} bytes total)")
        print()

    if ar.footer:
        f = ar.footer
        print(f"  Footer")
        print(f"    Total size  : {f.total_size}")
        print(f"    Global CRC  : 0x{f.global_crc:08X}")

# ============================================================
#  Validation détaillée (mimique la commande validate du binaire)
# ============================================================

def validate_archive(filepath: str) -> int:
    """
    Valide une archive CFR, affiche un rapport détaillé.
    Retourne le nombre d'erreurs.
    """
    errors = 0

    try:
        ar = parse_cfr(filepath, strict=False)
    except CFRParseError as e:
        print(f"[FATAL] {e}")
        return 1

    h = ar.header

    # Header CRC
    with open(filepath, "rb") as f:
        raw_hdr = f.read(HEADER_SIZE)
    expected_hcrc = crc32(raw_hdr[:16])
    if h.header_crc == expected_hcrc:
        print(f"[ OK ] Header CRC: 0x{h.header_crc:08X}")
    else:
        print(f"[FAIL] Header CRC: stocké=0x{h.header_crc:08X} "
              f"calculé=0x{expected_hcrc:08X}")
        errors += 1

    # data_len_xor
    dlx = 0
    for rec in ar.records:
        dlx ^= len(rec.data)
    if dlx == h.data_len_xor:
        print(f"[ OK ] data_len_xor: 0x{dlx:08X}")
    else:
        print(f"[FAIL] data_len_xor: stocké=0x{h.data_len_xor:08X} "
              f"calculé=0x{dlx:08X}")
        errors += 1

    # Per-record CRC-16
    for i, rec in enumerate(ar.records):
        name_bytes = rec.name.encode("ascii")
        expected = crc16_ccitt(name_bytes + rec.data)
        if rec.stored_crc16 == expected:
            print(f"[ OK ] Record {i} ({rec.name!r}) CRC-16: 0x{expected:04X}")
        else:
            print(f"[FAIL] Record {i} ({rec.name!r}) CRC-16: "
                  f"stocké=0x{rec.stored_crc16:04X} calculé=0x{expected:04X}")
            errors += 1

    # Footer
    if ar.footer:
        with open(filepath, "rb") as f:
            raw = f.read()
        fsize = len(raw)

        if ar.footer.total_size == fsize:
            print(f"[ OK ] Footer total_size: {fsize}")
        else:
            print(f"[FAIL] Footer total_size: stocké={ar.footer.total_size} "
                  f"réel={fsize}")
            errors += 1

        payload = raw[:fsize - FOOTER_SIZE]
        expected_gcrc = crc32(payload)
        if ar.footer.global_crc == expected_gcrc:
            print(f"[ OK ] Footer global CRC: 0x{expected_gcrc:08X}")
        else:
            print(f"[FAIL] Footer global CRC: stocké=0x{ar.footer.global_crc:08X} "
                  f"calculé=0x{expected_gcrc:08X}")
            errors += 1
    elif h.flags & HeaderFlags.HAS_FOOTER:
        print("[WARN] Flag HAS_FOOTER actif mais pas de footer valide")
        errors += 1

    print(f"\n{filepath}: {errors} erreur(s)")
    return errors

# ============================================================
#  Commandes CLI
# ============================================================

def cmd_parse(filepath: str) -> int:
    ar = parse_cfr(filepath)
    print_archive(ar)
    return 0

def cmd_validate(filepath: str) -> int:
    return 0 if validate_archive(filepath) == 0 else 1

def cmd_create(args: List[str]) -> int:
    do_xor = False
    idx = 0
    if args and args[0] == "-x":
        do_xor = True
        idx += 1

    if len(args) - idx < 2:
        print("Usage: create [-x] <output.cfr> <file1> [file2 ...]",
              file=sys.stderr)
        return 1

    output_path = args[idx]
    input_files = args[idx + 1:]

    records = [make_record_from_file(f) for f in input_files]
    data = serialize_cfr(records, xor_enabled=do_xor)

    with open(output_path, "wb") as f:
        f.write(data)

    print(f"[+] Créé {output_path} ({len(records)} record(s), "
          f"{len(data)} octets, XOR={'oui' if do_xor else 'non'})")
    return 0

def cmd_unpack(filepath: str, outdir: Optional[str] = None) -> int:
    ar = parse_cfr(filepath)
    if outdir:
        os.makedirs(outdir, exist_ok=True)

    for rec in ar.records:
        if outdir:
            out = os.path.join(outdir, rec.name) if rec.name else os.path.join(outdir, "unnamed")
        else:
            out = rec.name if rec.name else "unnamed"

        with open(out, "wb") as f:
            f.write(rec.data)
        print(f"[+] Extrait: {out} ({len(rec.data)} octets)")

    return 0

def cmd_roundtrip(filepath: str, binary: str = "./fileformat_O0") -> int:
    """Test de round-trip : lecture → écriture → validation par le binaire."""
    print(f"=== Round-trip: {filepath} ===\n")

    # 1. Lire
    ar = parse_cfr(filepath)
    print(f"[1] Lecture OK ({ar.header.num_records} records)")

    # 2. Réécrire avec les mêmes paramètres
    xor = bool(ar.header.flags & HeaderFlags.XOR_ENABLED)
    ftr = ar.footer is not None
    output = serialize_cfr(
        ar.records,
        author=ar.header.author,
        xor_enabled=xor,
        include_footer=ftr,
        timestamp=ar.header.timestamp,
    )
    rt_path = filepath + ".roundtrip.cfr"
    with open(rt_path, "wb") as f:
        f.write(output)
    print(f"[2] Écriture OK → {rt_path} ({len(output)} octets)")

    # 3. Relire avec notre parser
    ar2 = parse_cfr(rt_path)
    assert ar.header.num_records == ar2.header.num_records
    for r1, r2 in zip(ar.records, ar2.records):
        assert r1.name == r2.name, f"Nom: {r1.name!r} ≠ {r2.name!r}"
        assert r1.data == r2.data, f"Data: différent pour {r1.name!r}"
        assert r1.rec_type == r2.rec_type, f"Type: différent pour {r1.name!r}"
    print(f"[3] Relecture Python OK (contenu identique)")

    # 4. Validation par le binaire original
    if os.path.isfile(binary):
        result = subprocess.run(
            [binary, "validate", rt_path],
            capture_output=True, text=True,
        )
        if result.returncode == 0:
            print(f"[4] Validation binaire OK ({binary} validate → 0 erreur)")
        else:
            print(f"[4] ÉCHEC validation binaire :")
            print(result.stdout)
            print(result.stderr)
            return 1
    else:
        print(f"[4] Binaire {binary!r} non trouvé, validation binaire ignorée")

    # Nettoyage
    os.remove(rt_path)
    print(f"\n✓ Round-trip réussi pour {filepath}\n")
    return 0

def cmd_test(binary: str = "./fileformat_O0") -> int:
    """
    Suite de tests automatisés couvrant tous les critères du checkpoint.
    """
    print("=" * 60)
    print("  TESTS DU CHECKPOINT — CHAPITRE 25")
    print("=" * 60)
    passed = 0
    failed = 0

    def check(name: str, condition: bool, detail: str = ""):
        nonlocal passed, failed
        if condition:
            print(f"  [PASS] {name}")
            passed += 1
        else:
            print(f"  [FAIL] {name}" + (f" — {detail}" if detail else ""))
            failed += 1

    # ── Test CRC-32 ─────────────────────────────────────────
    print("\n--- Primitives CRC/XOR ---")
    check("CRC-32 vide",     crc32(b"") == 0x00000000)
    check("CRC-32 'CFRM'",   crc32(b"CFRM") == binascii.crc32(b"CFRM") & 0xFFFFFFFF)

    # ── Test CRC-16 ─────────────────────────────────────────
    # CRC-16 d'un buffer vide avec init=0x1D0F = 0x1D0F
    check("CRC-16 vide",     crc16_ccitt(b"") == 0x1D0F)
    # Valeur connue pour "123456789" avec init=0x1D0F (CRC-16/AUG-CCITT)
    check("CRC-16 '123456789'", crc16_ccitt(b"123456789") == 0xE5CC)

    # ── Test XOR ────────────────────────────────────────────
    plain = b"Hello, World! RE"
    check("XOR involutif",   xor_transform(xor_transform(plain)) == plain)
    check("XOR non trivial", xor_transform(plain) != plain)

    # ── Test génération ex nihilo ───────────────────────────
    print("\n--- Génération ex nihilo ---")
    recs = [
        make_text_record("test.txt", "Checkpoint 25 test content."),
        make_binary_record("payload.bin", bytes(range(256))),
        make_meta_record("info.meta", {"generator": "cfr_parser.py", "ch": "25"}),
    ]

    # Variante XOR + footer
    data_xf = serialize_cfr(recs, author="chkpt", xor_enabled=True, include_footer=True)
    check("Sérialise XOR+footer (pas d'exception)", True)

    tmp_xf = "/tmp/ch25_test_xor_footer.cfr"
    with open(tmp_xf, "wb") as f:
        f.write(data_xf)

    # Relire avec notre parser
    try:
        ar_xf = parse_cfr(tmp_xf)
        check("Parse XOR+footer OK", True)
        check("3 records relus", ar_xf.header.num_records == 3)
        check("Contenu texte intact",
              ar_xf.records[0].data == b"Checkpoint 25 test content.")
        check("Contenu binaire intact",
              ar_xf.records[1].data == bytes(range(256)))
        check("Footer présent", ar_xf.footer is not None)
    except CFRParseError as e:
        check("Parse XOR+footer OK", False, str(e))
        failed += 4  # skip dependents

    # Variante sans XOR + footer
    data_nf = serialize_cfr(recs, author="chkpt", xor_enabled=False, include_footer=True)
    tmp_nf = "/tmp/ch25_test_noxor_footer.cfr"
    with open(tmp_nf, "wb") as f:
        f.write(data_nf)

    try:
        ar_nf = parse_cfr(tmp_nf)
        check("Parse noXOR+footer OK", True)
        check("Données identiques sans XOR",
              ar_nf.records[0].data == ar_xf.records[0].data)
    except CFRParseError as e:
        check("Parse noXOR+footer OK", False, str(e))
        failed += 1

    # Variante sans footer
    data_no_ftr = serialize_cfr(recs, author="chkpt",
                                xor_enabled=False, include_footer=False)
    tmp_no_ftr = "/tmp/ch25_test_nofooter.cfr"
    with open(tmp_no_ftr, "wb") as f:
        f.write(data_no_ftr)

    try:
        ar_no_ftr = parse_cfr(tmp_no_ftr)
        check("Parse sans footer OK", True)
        check("Pas de footer", ar_no_ftr.footer is None)
    except CFRParseError as e:
        check("Parse sans footer OK", False, str(e))
        failed += 1

    # ── Validation binaire ──────────────────────────────────
    print("\n--- Validation par le binaire original ---")
    if os.path.isfile(binary):
        for label, path in [
            ("XOR+footer", tmp_xf),
            ("noXOR+footer", tmp_nf),
        ]:
            result = subprocess.run(
                [binary, "validate", path],
                capture_output=True, text=True,
            )
            check(f"Binaire valide {label}", result.returncode == 0,
                  result.stdout.strip().split("\n")[-1] if result.returncode else "")

        # Sans footer : tester avec list (validate pourrait warn)
        result = subprocess.run(
            [binary, "list", tmp_no_ftr],
            capture_output=True, text=True,
        )
        check("Binaire list sans footer", result.returncode == 0)
    else:
        print(f"  [SKIP] Binaire {binary!r} non trouvé")

    # ── Round-trip sur samples existants ────────────────────
    print("\n--- Round-trip sur archives existantes ---")
    sample_dir = "samples"
    if os.path.isdir(sample_dir):
        for name in ["demo.cfr", "packed_noxor.cfr", "packed_xor.cfr"]:
            path = os.path.join(sample_dir, name)
            if not os.path.isfile(path):
                print(f"  [SKIP] {path} non trouvé")
                continue
            try:
                ar = parse_cfr(path)
                xor = bool(ar.header.flags & HeaderFlags.XOR_ENABLED)
                ftr = ar.footer is not None
                rewritten = serialize_cfr(
                    ar.records, author=ar.header.author,
                    xor_enabled=xor, include_footer=ftr,
                    timestamp=ar.header.timestamp,
                )
                rt_path = path + ".rt.cfr"
                with open(rt_path, "wb") as f:
                    f.write(rewritten)

                ar2 = parse_cfr(rt_path)
                data_match = all(
                    r1.name == r2.name and r1.data == r2.data
                    for r1, r2 in zip(ar.records, ar2.records)
                )
                check(f"Round-trip {name} (Python)", data_match)

                if os.path.isfile(binary):
                    result = subprocess.run(
                        [binary, "validate", rt_path],
                        capture_output=True, text=True,
                    )
                    check(f"Round-trip {name} (binaire)", result.returncode == 0)

                os.remove(rt_path)
            except (CFRParseError, AssertionError) as e:
                check(f"Round-trip {name}", False, str(e))
    else:
        print(f"  [SKIP] Répertoire {sample_dir!r} non trouvé")

    # ── Rejet de fichiers invalides ─────────────────────────
    print("\n--- Rejet de fichiers invalides ---")

    # Magic invalide
    bad_magic = b"XXXX" + b"\x00" * 28
    tmp_bad = "/tmp/ch25_bad_magic.cfr"
    with open(tmp_bad, "wb") as f:
        f.write(bad_magic)
    try:
        parse_cfr(tmp_bad)
        check("Rejet magic invalide", False, "Pas d'exception levée")
    except CFRParseError:
        check("Rejet magic invalide", True)

    # Fichier trop court
    tmp_short = "/tmp/ch25_short.cfr"
    with open(tmp_short, "wb") as f:
        f.write(b"CFRM")
    try:
        parse_cfr(tmp_short)
        check("Rejet fichier trop court", False, "Pas d'exception levée")
    except CFRParseError:
        check("Rejet fichier trop court", True)

    # CRC header corrompu
    if os.path.isfile(tmp_xf):
        with open(tmp_xf, "rb") as f:
            corrupted = bytearray(f.read())
        corrupted[0x10] ^= 0xFF  # flip un octet du header CRC
        tmp_corrupt = "/tmp/ch25_corrupt_hcrc.cfr"
        with open(tmp_corrupt, "wb") as f:
            f.write(corrupted)
        try:
            parse_cfr(tmp_corrupt, strict=True)
            check("Rejet CRC header corrompu", False, "Pas d'exception levée")
        except CFRParseError:
            check("Rejet CRC header corrompu", True)

    # ── Cas limites ─────────────────────────────────────────
    print("\n--- Cas limites ---")

    # Archive vide (0 records)
    empty_data = serialize_cfr([], author="empty")
    tmp_empty = "/tmp/ch25_empty.cfr"
    with open(tmp_empty, "wb") as f:
        f.write(empty_data)
    try:
        ar_empty = parse_cfr(tmp_empty)
        check("Archive vide (0 records)", len(ar_empty.records) == 0)
    except CFRParseError as e:
        check("Archive vide (0 records)", False, str(e))

    # Record avec data_len = 0
    recs_empty_data = [make_text_record("empty.txt", "")]
    ed = serialize_cfr(recs_empty_data, xor_enabled=True)
    tmp_ed = "/tmp/ch25_empty_data.cfr"
    with open(tmp_ed, "wb") as f:
        f.write(ed)
    try:
        ar_ed = parse_cfr(tmp_ed)
        check("Record data_len=0", ar_ed.records[0].data == b"")
    except CFRParseError as e:
        check("Record data_len=0", False, str(e))

    # Record avec name_len = 0
    recs_empty_name = [CFRRecord(RecordType.BINARY, 0, "", b"\xAB\xCD", 0)]
    en = serialize_cfr(recs_empty_name, xor_enabled=False)
    tmp_en = "/tmp/ch25_empty_name.cfr"
    with open(tmp_en, "wb") as f:
        f.write(en)
    try:
        ar_en = parse_cfr(tmp_en)
        check("Record name_len=0", ar_en.records[0].name == "")
        check("Record name_len=0 data OK", ar_en.records[0].data == b"\xAB\xCD")
    except CFRParseError as e:
        check("Record name_len=0", False, str(e))

    # ── Nettoyage ───────────────────────────────────────────
    for p in [tmp_xf, tmp_nf, tmp_no_ftr, tmp_bad, tmp_short,
              tmp_empty, tmp_ed, tmp_en]:
        if os.path.isfile(p):
            os.remove(p)
    if os.path.isfile("/tmp/ch25_corrupt_hcrc.cfr"):
        os.remove("/tmp/ch25_corrupt_hcrc.cfr")

    # ── Résumé ──────────────────────────────────────────────
    total = passed + failed
    print(f"\n{'=' * 60}")
    print(f"  RÉSULTAT : {passed}/{total} tests passés", end="")
    if failed == 0:
        print(" ✓ CHECKPOINT VALIDÉ")
    else:
        print(f" ✗ {failed} échec(s)")
    print(f"{'=' * 60}")

    return 0 if failed == 0 else 1

# ============================================================
#  Main
# ============================================================

def usage():
    print(__doc__.strip())

def main() -> int:
    if len(sys.argv) < 2:
        usage()
        return 1

    cmd = sys.argv[1]

    try:
        if cmd == "parse" and len(sys.argv) >= 3:
            return cmd_parse(sys.argv[2])

        elif cmd == "validate" and len(sys.argv) >= 3:
            return cmd_validate(sys.argv[2])

        elif cmd == "create":
            return cmd_create(sys.argv[2:])

        elif cmd == "unpack" and len(sys.argv) >= 3:
            outdir = sys.argv[3] if len(sys.argv) > 3 else None
            return cmd_unpack(sys.argv[2], outdir)

        elif cmd == "roundtrip" and len(sys.argv) >= 3:
            binary = sys.argv[3] if len(sys.argv) > 3 else "./fileformat_O0"
            return cmd_roundtrip(sys.argv[2], binary)

        elif cmd == "test":
            binary = sys.argv[2] if len(sys.argv) > 2 else "./fileformat_O0"
            return cmd_test(binary)

        else:
            usage()
            return 1

    except CFRParseError as e:
        print(f"[ERREUR] {e}", file=sys.stderr)
        return 1
    except FileNotFoundError as e:
        print(f"[ERREUR] Fichier non trouvé : {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
