🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 25.4 — Écrire un parser/sérialiseur Python indépendant

> 🎯 **Objectif de cette section** : implémenter en Python un parser capable de lire n'importe quelle archive CFR, et un sérialiseur capable d'en produire de nouvelles. Le test ultime est le **round-trip** : un fichier généré par notre code Python doit être accepté et correctement lu par le binaire original.

---

## Stratégie d'implémentation

On pourrait écrire le parser d'un bloc, mais il est plus sûr de procéder par couches, en validant chaque couche indépendamment avant de passer à la suivante :

1. **Couche CRC** — implémenter les deux variantes de CRC utilisées par le format (CRC-32 standard et CRC-16/CCITT init=0x1D0F). Les tester isolément sur des données connues.  
2. **Couche XOR** — implémenter la transformation XOR rotative. La tester en déchiffrant un bloc connu.  
3. **Couche parsing** — lire le header, les records, le footer. Valider les CRC à chaque étape.  
4. **Couche sérialisation** — écrire un fichier CFR complet depuis des données en mémoire.  
5. **Test de round-trip** — lire une archive, la réécrire, vérifier que le binaire original l'accepte.

Cette progression garantit que chaque brique est solide avant d'être intégrée dans l'ensemble. Un bug dans le CRC-16 qui ne serait détecté qu'au moment du round-trip final serait beaucoup plus difficile à diagnostiquer.

---

## Les fondations : CRC et XOR

### CRC-32

Le format utilise un CRC-32 classique — polynôme `0xEDB88320` (forme réfléchie), valeur initiale `0xFFFFFFFF`, XOR final `0xFFFFFFFF`. C'est exactement la variante implémentée par le module `binascii` de Python :

```python
import binascii

def crc32(data: bytes) -> int:
    """CRC-32 standard (ISO 3309), identique à zlib/binascii."""
    return binascii.crc32(data) & 0xFFFFFFFF
```

Le `& 0xFFFFFFFF` garantit un résultat non signé sur 32 bits, quel que soit la version de Python.

Vérifions immédiatement sur le header de `demo.cfr` :

```python
with open("samples/demo.cfr", "rb") as f:
    header_bytes = f.read(32)

# Le CRC du header est stocké à l'offset 0x10 (4 octets, little-endian)
import struct  
stored_crc = struct.unpack_from("<I", header_bytes, 0x10)[0]  

# Le CRC est calculé sur les 16 premiers octets, avec le champ CRC à zéro
check_data = bytearray(header_bytes[:16])
# Les octets 0x10–0x13 ne font PAS partie des 16 premiers octets,
# donc pas besoin de les mettre à zéro — ils ne sont pas inclus.
computed_crc = crc32(bytes(check_data))

assert stored_crc == computed_crc, \
    f"Header CRC mismatch: stored={stored_crc:#010x} computed={computed_crc:#010x}"
print(f"Header CRC OK: {computed_crc:#010x}")
```

Si cette assertion passe, notre implémentation CRC-32 est validée sur le cas réel. Si elle échoue, il faut revenir vérifier l'étendue exacte des octets couverts par le CRC (c'est le piège classique : le CRC couvre-t-il les 16 premiers octets, ou les 16 premiers octets avec le champ CRC mis à zéro ?).

> 💡 **Piège courant** : certaines implémentations calculent le CRC du header en incluant le champ `header_crc` lui-même, mis à zéro. D'autres excluent purement ce champ et ne calculent que sur les octets qui le précèdent. La différence est subtile mais produit des résultats différents. En section 25.2, on a formulé l'hypothèse que le CRC couvre les 16 premiers octets (offsets `0x00`–`0x0F`), soit exactement les champs `magic + version + flags + num_records + timestamp`. Le champ `header_crc` commence à l'offset `0x10` et n'est donc pas inclus dans le calcul.

### CRC-16/CCITT (init=0x1D0F)

Cette variante n'existe pas dans la bibliothèque standard Python. On l'implémente manuellement :

```python
def crc16_ccitt(data: bytes, init: int = 0x1D0F) -> int:
    """CRC-16/CCITT avec polynôme 0x1021 et valeur initiale custom."""
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
```

La valeur initiale `0x1D0F` est la particularité de ce format. La variante standard CRC-16/CCITT utilise `0xFFFF` ; ici, l'auteur du format a choisi une valeur non standard. C'est exactement le genre de détail qui ne peut être découvert que par le reverse — aucune documentation publique ne le mentionne puisque le format est propriétaire.

Validons sur un enregistrement connu de `packed_noxor.cfr` (pas de XOR, donc les données sont en clair et le CRC est calculé directement dessus) :

```python
# Lire le premier record de packed_noxor.cfr
with open("samples/packed_noxor.cfr", "rb") as f:
    f.seek(32)  # passer le header
    rec_type, rec_flags, name_len, data_len = struct.unpack("<BBHI", f.read(8))
    name = f.read(name_len)
    data = f.read(data_len)
    stored_crc16 = struct.unpack("<H", f.read(2))[0]

# CRC-16 calculé sur name + data (données originales, avant tout XOR)
computed_crc16 = crc16_ccitt(name + data)

assert stored_crc16 == computed_crc16, \
    f"Record CRC-16 mismatch: stored={stored_crc16:#06x} computed={computed_crc16:#06x}"
print(f"Record CRC-16 OK: {computed_crc16:#06x}")
```

### Transformation XOR

La transformation est un XOR rotatif avec une clé de 4 octets :

```python
XOR_KEY = bytes([0x5A, 0x3C, 0x96, 0xF1])

def xor_transform(data: bytes) -> bytes:
    """Applique/retire le XOR rotatif. L'opération est son propre inverse."""
    key_len = len(XOR_KEY)
    return bytes(b ^ XOR_KEY[i % key_len] for i, b in enumerate(data))
```

L'élégance du XOR est que la même fonction sert à chiffrer et déchiffrer. Vérifions :

```python
original = b"Hello from the CFR archive format!"  
encrypted = xor_transform(original)  
decrypted = xor_transform(encrypted)  
assert decrypted == original  
print(f"XOR round-trip OK")  
```

---

## Le parser : lire une archive CFR

### Structures de données

Avant de parser, définissons des classes pour représenter le contenu en mémoire. On utilise des `dataclass` pour la lisibilité :

```python
import struct  
from dataclasses import dataclass, field  
from typing import List, Optional  
from enum import IntEnum, IntFlag  

class RecordType(IntEnum):
    TEXT   = 0x01
    BINARY = 0x02
    META   = 0x03

class HeaderFlags(IntFlag):
    XOR_ENABLED = 1 << 0
    HAS_FOOTER  = 1 << 1

@dataclass
class CFRHeader:
    magic: bytes            # 4 octets, b"CFRM"
    version: int            # uint16
    flags: HeaderFlags      # uint16
    num_records: int        # uint32
    timestamp: int          # uint32
    header_crc: int         # uint32
    author: str             # 8 octets, null-padded
    data_len_xor: int       # uint32 (XOR de tous les data_len)

@dataclass
class CFRRecord:
    rec_type: int           # uint8 (RecordType)
    rec_flags: int          # uint8
    name: str               # longueur variable
    data: bytes             # longueur variable, APRÈS dé-XOR
    stored_crc16: int       # uint16

@dataclass
class CFRFooter:
    magic: bytes            # 4 octets, b"CRFE"
    total_size: int         # uint32
    global_crc: int         # uint32

@dataclass
class CFRArchive:
    header: CFRHeader
    records: List[CFRRecord] = field(default_factory=list)
    footer: Optional[CFRFooter] = None
```

### Fonction de parsing

```python
import io

HEADER_SIZE = 32  
FOOTER_SIZE = 12  
REC_HEADER_SIZE = 8  
MAX_RECORDS = 1024  

class CFRParseError(Exception):
    """Erreur de parsing d'une archive CFR."""
    pass

def parse_cfr(filepath: str) -> CFRArchive:
    """Parse une archive CFR et retourne sa représentation en mémoire."""
    with open(filepath, "rb") as f:
        raw = f.read()

    if len(raw) < HEADER_SIZE:
        raise CFRParseError(f"Fichier trop court pour un header ({len(raw)} octets)")

    buf = io.BytesIO(raw)

    # ── Header ──────────────────────────────────────────────
    hdr_bytes = buf.read(HEADER_SIZE)
    # Découpons manuellement chaque champ pour plus de clarté
    # (un seul struct.unpack sur 32 octets serait possible mais fragile) :
    magic       = hdr_bytes[0:4]
    version     = struct.unpack_from("<H", hdr_bytes, 4)[0]
    flags       = HeaderFlags(struct.unpack_from("<H", hdr_bytes, 6)[0])
    num_records = struct.unpack_from("<I", hdr_bytes, 8)[0]
    timestamp   = struct.unpack_from("<I", hdr_bytes, 12)[0]
    header_crc  = struct.unpack_from("<I", hdr_bytes, 16)[0]
    author_raw  = hdr_bytes[20:28]
    dlx_raw     = hdr_bytes[28:32]

    if magic != b"CFRM":
        raise CFRParseError(f"Magic invalide : {magic!r} (attendu : b'CFRM')")

    if num_records > MAX_RECORDS:
        raise CFRParseError(f"Trop de records : {num_records} (max {MAX_RECORDS})")

    # Vérification du CRC header (sur les 16 premiers octets)
    expected_crc = crc32(hdr_bytes[:16])
    if header_crc != expected_crc:
        raise CFRParseError(
            f"Header CRC invalide : stocké={header_crc:#010x} "
            f"calculé={expected_crc:#010x}"
        )

    author = author_raw.rstrip(b"\x00").decode("ascii", errors="replace")
    data_len_xor = struct.unpack_from("<I", dlx_raw, 0)[0]

    header = CFRHeader(
        magic=magic, version=version, flags=flags,
        num_records=num_records, timestamp=timestamp,
        header_crc=header_crc, author=author,
        data_len_xor=data_len_xor
    )

    do_xor = bool(flags & HeaderFlags.XOR_ENABLED)

    # ── Records ─────────────────────────────────────────────
    records = []
    dlx_check = 0

    for i in range(num_records):
        rh_bytes = buf.read(REC_HEADER_SIZE)
        if len(rh_bytes) < REC_HEADER_SIZE:
            raise CFRParseError(f"Record {i} : en-tête tronqué")

        rec_type  = rh_bytes[0]
        rec_flags = rh_bytes[1]
        name_len  = struct.unpack_from("<H", rh_bytes, 2)[0]
        data_len  = struct.unpack_from("<I", rh_bytes, 4)[0]

        name_bytes = buf.read(name_len)
        if len(name_bytes) < name_len:
            raise CFRParseError(f"Record {i} : nom tronqué")

        raw_data = buf.read(data_len)
        if len(raw_data) < data_len:
            raise CFRParseError(f"Record {i} : données tronquées")

        crc_bytes = buf.read(2)
        if len(crc_bytes) < 2:
            raise CFRParseError(f"Record {i} : CRC-16 tronqué")
        stored_crc16 = struct.unpack("<H", crc_bytes)[0]

        # Dé-XOR des données si nécessaire
        if do_xor and data_len > 0:
            plain_data = xor_transform(raw_data)
        else:
            plain_data = raw_data

        # Vérification CRC-16 (calculé sur name + données ORIGINALES)
        expected_crc16 = crc16_ccitt(name_bytes + plain_data)
        if stored_crc16 != expected_crc16:
            raise CFRParseError(
                f"Record {i} ({name_bytes!r}) : CRC-16 invalide : "
                f"stocké={stored_crc16:#06x} calculé={expected_crc16:#06x}"
            )

        name = name_bytes.decode("ascii", errors="replace")
        records.append(CFRRecord(
            rec_type=rec_type, rec_flags=rec_flags,
            name=name, data=plain_data,
            stored_crc16=stored_crc16
        ))

        dlx_check ^= data_len

    # Vérification du champ data_len_xor
    if dlx_check != data_len_xor:
        raise CFRParseError(
            f"data_len_xor invalide : stocké={data_len_xor:#010x} "
            f"calculé={dlx_check:#010x}"
        )

    # ── Footer ──────────────────────────────────────────────
    footer = None
    if flags & HeaderFlags.HAS_FOOTER:
        ftr_bytes = buf.read(FOOTER_SIZE)
        if len(ftr_bytes) == FOOTER_SIZE:
            ftr_magic = ftr_bytes[0:4]
            ftr_total = struct.unpack_from("<I", ftr_bytes, 4)[0]
            ftr_crc   = struct.unpack_from("<I", ftr_bytes, 8)[0]

            if ftr_magic != b"CRFE":
                raise CFRParseError(
                    f"Footer magic invalide : {ftr_magic!r} (attendu : b'CRFE')"
                )

            if ftr_total != len(raw):
                raise CFRParseError(
                    f"Footer total_size incohérent : "
                    f"stocké={ftr_total} taille réelle={len(raw)}"
                )

            # CRC global = CRC-32 de tout ce qui précède le footer
            payload = raw[:len(raw) - FOOTER_SIZE]
            expected_global = crc32(payload)
            if ftr_crc != expected_global:
                raise CFRParseError(
                    f"Footer CRC global invalide : "
                    f"stocké={ftr_crc:#010x} calculé={expected_global:#010x}"
                )

            footer = CFRFooter(
                magic=ftr_magic,
                total_size=ftr_total,
                global_crc=ftr_crc
            )

    return CFRArchive(header=header, records=records, footer=footer)
```

### Points de conception à noter

**L'ordre des opérations XOR/CRC est crucial.** Le CRC-16 est vérifié sur les données *après* dé-XOR (c'est-à-dire sur les données originales). Si on inversait l'ordre — calculer le CRC sur les données encore XOR-ées — la vérification échouerait systématiquement sur les archives XOR. C'est une information obtenue en section 25.2 par comparaison des CRC entre archives XOR et non-XOR.

**Le parsing est séquentiel, pas basé sur des offsets.** On lit le header, puis les records un par un en suivant le curseur. Les champs `name_len` et `data_len` de chaque record déterminent combien d'octets avancer. Il n'y a pas de table d'offsets dans le format — c'est un design linéaire. L'implication est qu'un fichier corrompu au milieu rend le reste illisible.

**Les erreurs sont des exceptions explicites.** Pour le reverse, il est préférable de crasher bruyamment sur chaque anomalie plutôt que de tenter une récupération silencieuse. Chaque `CFRParseError` pointe vers un invariant spécifique du format.

---

## Le sérialiseur : écrire une archive CFR

### Fonction de sérialisation

```python
import time

def serialize_cfr(
    records: List[CFRRecord],
    author: str = "python",
    xor_enabled: bool = True,
    include_footer: bool = True,
    timestamp: Optional[int] = None,
) -> bytes:
    """Sérialise une liste de records en une archive CFR complète."""

    if len(records) > MAX_RECORDS:
        raise ValueError(f"Trop de records : {len(records)} (max {MAX_RECORDS})")

    flags = HeaderFlags(0)
    if xor_enabled:
        flags |= HeaderFlags.XOR_ENABLED
    if include_footer:
        flags |= HeaderFlags.HAS_FOOTER

    ts = timestamp if timestamp is not None else int(time.time())

    # ── Sérialiser les records dans un buffer ───────────────
    records_buf = io.BytesIO()
    dlx = 0

    for rec in records:
        name_bytes = rec.name.encode("ascii")
        name_len = len(name_bytes)
        data_len = len(rec.data)
        dlx ^= data_len

        # En-tête du record
        rh = struct.pack("<BBHI", rec.rec_type, rec.rec_flags, name_len, data_len)
        records_buf.write(rh)

        # Nom (jamais transformé)
        records_buf.write(name_bytes)

        # CRC-16 sur name + données originales (AVANT XOR)
        rec_crc = crc16_ccitt(name_bytes + rec.data)

        # Données (XOR si flag actif)
        if xor_enabled and data_len > 0:
            records_buf.write(xor_transform(rec.data))
        else:
            records_buf.write(rec.data)

        # CRC-16
        records_buf.write(struct.pack("<H", rec_crc))

    records_bytes = records_buf.getvalue()

    # ── Construire le header ────────────────────────────────
    #
    # On doit calculer le header_crc sur les 16 premiers octets.
    # Or le header_crc fait partie des octets 16–19 (après les 16 premiers),
    # donc il ne s'inclut pas lui-même.

    author_padded = author.encode("ascii")[:8].ljust(8, b"\x00")
    dlx_bytes = struct.pack("<I", dlx)

    # Premiers 16 octets (magic + version + flags + num_records + timestamp)
    hdr_prefix = struct.pack("<4sHHII",
        b"CFRM", 2, int(flags), len(records), ts
    )
    assert len(hdr_prefix) == 16

    hdr_crc = crc32(hdr_prefix)

    header_bytes = hdr_prefix + struct.pack("<I", hdr_crc) + author_padded + dlx_bytes
    assert len(header_bytes) == HEADER_SIZE

    # ── Assembler sans footer ───────────────────────────────
    payload = header_bytes + records_bytes

    # ── Footer (optionnel) ──────────────────────────────────
    if include_footer:
        total_size = len(payload) + FOOTER_SIZE
        global_crc = crc32(payload)
        footer_bytes = struct.pack("<4sII", b"CRFE", total_size, global_crc)
        return payload + footer_bytes
    else:
        return payload
```

### Helpers pour créer des records

Pour simplifier la construction de records, ajoutons quelques fonctions utilitaires :

```python
def make_text_record(name: str, text: str) -> CFRRecord:
    """Crée un enregistrement de type TEXT."""
    return CFRRecord(
        rec_type=RecordType.TEXT, rec_flags=0,
        name=name, data=text.encode("utf-8"),
        stored_crc16=0  # sera recalculé à la sérialisation
    )

def make_binary_record(name: str, data: bytes) -> CFRRecord:
    """Crée un enregistrement de type BINARY."""
    return CFRRecord(
        rec_type=RecordType.BINARY, rec_flags=0,
        name=name, data=data,
        stored_crc16=0
    )

def make_meta_record(name: str, metadata: dict) -> CFRRecord:
    """Crée un enregistrement de type META depuis un dictionnaire clé=valeur."""
    text = "\n".join(f"{k}={v}" for k, v in metadata.items())
    return CFRRecord(
        rec_type=RecordType.META, rec_flags=0,
        name=name, data=text.encode("utf-8"),
        stored_crc16=0
    )
```

---

## Le test de round-trip

Le round-trip est la validation définitive : on prouve que notre implémentation est conforme au format en vérifiant que le binaire original accepte les fichiers qu'on produit.

### Round-trip n°1 : lire puis réécrire

```python
def test_roundtrip_read_write(filepath: str, output_path: str):
    """Lit une archive, la réécrit, vérifie que le résultat est identique."""
    # 1. Lire l'archive originale
    archive = parse_cfr(filepath)

    # 2. Réécrire avec les mêmes paramètres
    xor = bool(archive.header.flags & HeaderFlags.XOR_ENABLED)
    footer = archive.footer is not None
    output = serialize_cfr(
        archive.records,
        author=archive.header.author,
        xor_enabled=xor,
        include_footer=footer,
        timestamp=archive.header.timestamp,
    )

    # 3. Écrire sur disque
    with open(output_path, "wb") as f:
        f.write(output)

    # 4. Relire et comparer
    archive2 = parse_cfr(output_path)

    assert archive.header.num_records == archive2.header.num_records
    for r1, r2 in zip(archive.records, archive2.records):
        assert r1.name == r2.name, f"Nom différent : {r1.name!r} vs {r2.name!r}"
        assert r1.data == r2.data, f"Données différentes pour {r1.name}"
        assert r1.rec_type == r2.rec_type, f"Type différent pour {r1.name}"

    print(f"Round-trip OK : {filepath} → {output_path}")
```

### Round-trip n°2 : validation par le binaire original

C'est le test le plus important. Notre parser Python peut avoir un bug cohérent (il écrit mal et relit mal de la même façon), donc un round-trip interne ne suffit pas. Il faut confronter notre sortie au binaire de référence :

```python
import subprocess

def test_binary_validation(cfr_path: str, binary: str = "./fileformat_O0"):
    """Vérifie qu'une archive CFR est acceptée par le binaire original."""
    result = subprocess.run(
        [binary, "validate", cfr_path],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"ÉCHEC : {binary} validate {cfr_path}")
        print(result.stdout)
        print(result.stderr)
        raise AssertionError("Le binaire original rejette notre archive")
    print(f"Validation binaire OK : {cfr_path}")

def test_full_roundtrip(filepath: str):
    """Test complet : lecture Python → écriture Python → validation binaire."""
    archive = parse_cfr(filepath)

    output_path = filepath + ".roundtrip.cfr"
    xor = bool(archive.header.flags & HeaderFlags.XOR_ENABLED)
    footer = archive.footer is not None
    output = serialize_cfr(
        archive.records,
        author=archive.header.author,
        xor_enabled=xor,
        include_footer=footer,
        timestamp=archive.header.timestamp,
    )
    with open(output_path, "wb") as f:
        f.write(output)

    # Le binaire original doit accepter notre fichier
    test_binary_validation(output_path)

    # Le binaire original doit aussi pouvoir le lire et afficher les records
    result = subprocess.run(
        ["./fileformat_O0", "read", output_path],
        capture_output=True, text=True
    )
    print(result.stdout[:500])
    print(f"Round-trip complet OK : {filepath}")
```

Lançons sur nos trois archives :

```python
test_full_roundtrip("samples/demo.cfr")  
test_full_roundtrip("samples/packed_noxor.cfr")  
test_full_roundtrip("samples/packed_xor.cfr")  
```

### Round-trip n°3 : génération ex nihilo

Le test le plus ambitieux est de créer une archive entièrement depuis Python, sans jamais avoir lu de fichier existant, et de la faire accepter par le binaire :

```python
def test_generate_from_scratch():
    """Génère une archive de toutes pièces et la valide avec le binaire."""
    records = [
        make_text_record("hello.txt", "Written entirely by our Python serializer."),
        make_binary_record("payload.bin", bytes(range(256))),
        make_meta_record("build.meta", {
            "generator": "cfr_parser.py",
            "chapter": "25",
        }),
    ]

    # Variante avec XOR
    data_xor = serialize_cfr(records, author="pytest", xor_enabled=True)
    with open("/tmp/python_generated_xor.cfr", "wb") as f:
        f.write(data_xor)
    test_binary_validation("/tmp/python_generated_xor.cfr")

    # Variante sans XOR
    data_plain = serialize_cfr(records, author="pytest", xor_enabled=False)
    with open("/tmp/python_generated_plain.cfr", "wb") as f:
        f.write(data_plain)
    test_binary_validation("/tmp/python_generated_plain.cfr")

    # Variante sans footer
    data_noftr = serialize_cfr(
        records, author="pytest", xor_enabled=False, include_footer=False
    )
    with open("/tmp/python_generated_nofooter.cfr", "wb") as f:
        f.write(data_noftr)
    # Note : validate pourrait avertir sur l'absence de footer,
    # mais list/read doivent fonctionner
    result = subprocess.run(
        ["./fileformat_O0", "list", "/tmp/python_generated_nofooter.cfr"],
        capture_output=True, text=True
    )
    assert result.returncode == 0
    print(result.stdout)

    print("Génération ex nihilo OK (3 variantes)")
```

Si les trois variantes passent la validation, notre implémentation est conforme au format.

---

## Déboguer les échecs de round-trip

Les échecs de round-trip sont fréquents lors du développement du parser. Voici les causes les plus courantes et les techniques de diagnostic.

### Le CRC du header ne matche pas

Symptôme : le binaire affiche `[FAIL] Header CRC`.

Cause probable : l'étendue des octets couverts par le CRC n'est pas celle qu'on croit. Affichons les octets exacts :

```python
with open("output.cfr", "rb") as f:
    hdr = f.read(32)
print("Octets CRC-és :", hdr[:16].hex())  
print("CRC stocké    :", struct.unpack_from("<I", hdr, 16)[0])  
print("CRC calculé   :", crc32(hdr[:16]))  
```

### Le CRC-16 d'un record ne matche pas

Symptôme : le binaire affiche `[FAIL] Record N CRC-16`.

Cause probable n°1 : l'ordre XOR/CRC est inversé. Le CRC doit être calculé sur les données originales (avant XOR), pas sur les données stockées (après XOR).

Cause probable n°2 : le CRC couvre le nom + les données, mais on a oublié le nom ou on a inclus un octet de trop (par exemple le null terminator, qui n'existe pas dans ce format — les noms ne sont pas null-terminés dans le fichier).

Technique de diagnostic — comparer octet par octet ce que notre sérialiseur écrit avec ce que le binaire écrit pour les mêmes données :

```python
with open("samples/packed_noxor.cfr", "rb") as f:
    original = f.read()
with open("output.cfr", "rb") as f:
    ours = f.read()

for i, (a, b) in enumerate(zip(original, ours)):
    if a != b:
        print(f"Différence à l'offset {i:#06x} : original={a:#04x} notre={b:#04x}")
```

Le premier octet différent pointe directement vers le champ problématique.

### Le `data_len_xor` ne matche pas

Symptôme : le binaire affiche `[FAIL] Reserved check`.

Cause : on a inversé l'opération ou oublié un record dans le XOR. Vérifions :

```python
dlx = 0  
for rec in records:  
    dlx ^= len(rec.data)
    print(f"  {rec.name}: data_len={len(rec.data)}, dlx cumulé={dlx:#010x}")
print(f"dlx final = {dlx:#010x}")
```

### La taille totale du footer ne matche pas

Symptôme : le binaire affiche `[FAIL] Footer total_size`.

Cause : le calcul `len(payload) + FOOTER_SIZE` est décalé. Vérifions que `payload` contient bien le header + tous les records (sans le footer).

---

## Structure du fichier final

Organisons le code en un seul module réutilisable `cfr_parser.py` :

```
cfr_parser.py
├── Constantes (HEADER_SIZE, FOOTER_SIZE, MAX_RECORDS, XOR_KEY, ...)
├── Fonctions CRC (crc32, crc16_ccitt)
├── Fonction XOR (xor_transform)
├── Classes de données (CFRHeader, CFRRecord, CFRFooter, CFRArchive)
├── Parser (parse_cfr)
├── Sérialiseur (serialize_cfr)
├── Helpers (make_text_record, make_binary_record, make_meta_record)
├── Affichage (print_archive)
└── CLI (__main__)
    ├── parse <input.cfr>           — afficher le contenu
    ├── validate <input.cfr>        — vérifier l'intégrité
    ├── create <output.cfr> [files] — créer une archive
    └── roundtrip <input.cfr>       — test de round-trip
```

La section CLI rend le parser utilisable directement :

```bash
$ python3 cfr_parser.py parse samples/demo.cfr
$ python3 cfr_parser.py create /tmp/test.cfr file1.txt file2.bin
$ python3 cfr_parser.py roundtrip samples/demo.cfr
```

---

## Ce que l'implémentation confirme

Écrire le parser et le sérialiseur est le moment où chaque ambiguïté restante doit être résolue. Les questions qui pouvaient rester vagues dans le pattern `.hexpat` (« le CRC couvre probablement ces octets ») deviennent des choix binaires dans le code (la valeur est correcte ou elle ne l'est pas).

Le round-trip validé par le binaire original est la preuve la plus forte de la compréhension du format. C'est un test qui ne pardonne aucune approximation : un seul octet erroné et la validation échoue. Si les trois variantes (round-trip, génération avec XOR, génération sans footer) passent, notre modèle du format CFR est complet et correct.

Il reste une dernière étape : formaliser cette compréhension dans un document de spécification autonome — c'est l'objet de la section 25.5.

---


⏭️ [Documenter le format (produire une spécification)](/25-fileformat/05-documenter-specification.md)
