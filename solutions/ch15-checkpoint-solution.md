🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 15 : Fuzzing pour le Reverse Engineering

> ⚠️ **Spoilers** — Ce fichier contient le corrigé complet du checkpoint. Essayez de réaliser le checkpoint par vous-même avant de consulter cette solution.  
> 📦 **Binaire** : `binaries/ch15-fileformat/`

---

## Livrable 1 — Compilation instrumentée

### Build AFL++ (fuzzing principal)

```bash
$ cd binaries/ch15-fileformat/
$ make clean
$ make fuzz
```

Cela produit `fileformat_afl` (instrumentation standard) et `fileformat_afl_asan` (instrumentation + ASan). Si `afl-gcc` n'est pas trouvé, on peut surcharger avec `make fuzz AFL_CC=/chemin/vers/afl-gcc`, ou compiler directement :

```bash
$ afl-gcc -O0 -g -o fileformat_afl fileformat.c
```

Sortie attendue lors de la compilation :

```
[+] Instrumented X locations (non-hardened mode, ratio 100%).
```

Le nombre exact de locations varie selon le code, mais doit être supérieur à 0.

### Build ASan (triage des crashs)

Si la cible `make fuzz` a déjà été exécutée, `fileformat_afl_asan` est déjà disponible. Sinon :

```bash
$ AFL_USE_ASAN=1 afl-gcc -O0 -g -o fileformat_afl_asan fileformat.c
```

### Build gcov (couverture — optionnel mais recommandé)

```bash
$ make coverage
```

Ou directement :

```bash
$ gcc --coverage -O0 -g -o fileformat_gcov fileformat.c
```

### Vérification

```bash
$ echo -ne 'CSTM\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > /tmp/test_seed.bin
$ afl-showmap -o /dev/stdout -- ./fileformat_afl /tmp/test_seed.bin 2>/dev/null | head -5
```

La sortie doit afficher plusieurs lignes au format `NNNNN:N` — ce sont les edges couverts par ce seed. Si la sortie est vide, l'instrumentation a échoué (vérifier que `afl-gcc` est bien dans le PATH et que la compilation a produit le message `Instrumented X locations`).

---

## Livrable 2 — Corpus initial et dictionnaire

### Triage préalable

```bash
$ file fileformat_afl
fileformat_afl: ELF 64-bit LSB pie executable, x86-64, ...

$ strings fileformat_afl | grep -iE "error|invalid|usage|section|magic|version|checksum"
```

Chaînes pertinentes typiquement trouvées :

```
Usage: %s <input_file>  
Error: file too small  
CSTM  
Error: invalid magic  
Error: unsupported version %d  
Section type: DATA  
Section type: INDEX  
Section type: META  
Error: unknown section type 0x%02x  
Decoding section at offset %d, length %d  
Error: section length exceeds file size  
Checksum mismatch: expected 0x%08x, got 0x%08x  
Processing complete: %d sections parsed  
```

Vérification rapide par exécution :

```bash
$ echo "AAAA" | ./fileformat_afl /dev/stdin
Error: file too small

$ echo -ne 'CSTM\x01\x00\x00\x00' | ./fileformat_afl /dev/stdin
Parsing header...
```

Le magic `CSTM` est confirmé, et la version `\x01` passe la première validation.

### Corpus initial (5 seeds)

```bash
$ mkdir corpus_initial

# Seed 1 : version 1, taille minimale (16 octets)
$ printf 'CSTM\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' \
    > corpus_initial/s01_v1_minimal.bin

# Seed 2 : version 2, taille minimale
$ printf 'CSTM\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' \
    > corpus_initial/s02_v2_minimal.bin

# Seed 3 : version 1, 1 section de type DATA (0x01), longueur 8
$ python3 -c "
import sys  
header  = b'CSTM'          # magic  
header += b'\x01'           # version 1  
header += b'\x00'           # flags  
header += b'\x01\x00'       # section_count = 1 (LE 16-bit)  
section = b'\x01'           # type = DATA  
section += b'\x00\x00\x00'  # padding  
section += b'\x08\x00\x00\x00'  # length = 8 (LE 32-bit)  
payload = b'AAAAAAAA'       # 8 octets de données  
sys.stdout.buffer.write(header + section + payload)  
" > corpus_initial/s03_v1_data_section.bin

# Seed 4 : version 1, 1 section de type INDEX (0x02), longueur 16
$ python3 -c "
import sys  
header  = b'CSTM\x01\x00\x01\x00'  
section = b'\x02\x00\x00\x00\x10\x00\x00\x00'  # type=INDEX, length=16  
payload = b'\x00' * 16  
sys.stdout.buffer.write(header + section + payload)  
" > corpus_initial/s04_v1_index_section.bin

# Seed 5 : version 1, 1 section de type META (0x03), longueur 8
$ python3 -c "
import sys  
header  = b'CSTM\x01\x00\x01\x00'  
section = b'\x03\x00\x00\x00\x08\x00\x00\x00'  # type=META, length=8  
payload = b'METADATA'  
sys.stdout.buffer.write(header + section + payload)  
" > corpus_initial/s05_v1_meta_section.bin
```

Vérification que les seeds empruntent des chemins différents :

```bash
$ afl-showmap -o map_s01.txt -- ./fileformat_afl corpus_initial/s01_v1_minimal.bin 2>/dev/null
$ afl-showmap -o map_s03.txt -- ./fileformat_afl corpus_initial/s03_v1_data_section.bin 2>/dev/null
$ afl-showmap -o map_s04.txt -- ./fileformat_afl corpus_initial/s04_v1_index_section.bin 2>/dev/null

$ wc -l map_s01.txt map_s03.txt map_s04.txt
  12 map_s01.txt
  23 map_s03.txt
  27 map_s04.txt
```

Les bitmaps ont des tailles différentes — chaque seed couvre un nombre distinct d'edges, confirmant qu'ils empruntent des chemins différents dans le parseur.

### Dictionnaire (18 tokens)

```bash
$ cat > dict_ch25.txt << 'EOF'
# === Magic ===
magic="CSTM"

# === Versions ===
v1="\x01"  
v2="\x02"  
v3="\x03"  

# === Types de section ===
type_data="\x01"  
type_index="\x02"  
type_meta="\x03"  

# === Mots-clés (au cas où le parseur gère du texte) ===
kw_data="DATA"  
kw_index="INDEX"  
kw_meta="META"  

# === Valeurs numériques limites (champs 32-bit LE) ===
zero_32="\x00\x00\x00\x00"  
one_32="\x01\x00\x00\x00"  
ff_32="\xff\xff\xff\xff"  
max_short="\xff\xff"  

# === Tailles typiques ===
len_16="\x10\x00\x00\x00"  
len_256="\x00\x01\x00\x00"  
EOF  
```

---

## Livrable 3 — Campagne de fuzzing

### Configuration système

```bash
$ echo core | sudo tee /proc/sys/kernel/core_pattern
```

### Lancement

```bash
$ afl-fuzz -i corpus_initial -o out_ch15 -x dict_ch25.txt \
    -m none -- ./fileformat_afl @@
```

### Résultat attendu après 15-30 minutes

Le tableau de bord AFL++ devrait afficher des valeurs dans ces ordres de grandeur :

```
        run time : 0 days, 0 hrs, 20 min, ...
   last new find : 0 days, 0 hrs, 0-5 min, ...
 corpus count    : 30–80
 saved crashes   : 2–10
 map density     : 2–6%
 exec speed      : 1000–8000/sec
```

Les valeurs exactes dépendent de la machine et du binaire. L'essentiel est de dépasser les seuils du checkpoint : `corpus count ≥ 20` et `saved crashes ≥ 2`.

Si après 10 minutes le `corpus count` stagne sous 10 et qu'il y a 0 crash, vérifier :

- Les seeds sont-ils correctement construits ? (Les exécuter manuellement sur le binaire non instrumenté pour voir si le parseur les accepte.)  
- Le dictionnaire est-il chargé ? (AFL++ affiche `Loaded N tokens from dict_ch25.txt` au démarrage.)  
- Le binaire est-il bien instrumenté ? (Le message `Instrumented X locations` est apparu à la compilation.)

### Arrêt de la campagne

Arrêter avec `Ctrl+C` une fois les seuils atteints. La campagne peut aussi être laissée plus longtemps pour accumuler davantage de crashs et de couverture.

### Vérification des seuils

```bash
$ ls out_ch15/default/queue/id:* | wc -l
47

$ ls out_ch15/default/crashes/id:* 2>/dev/null | wc -l
5
```

47 inputs dans le corpus et 5 crashs — les seuils sont largement dépassés.

---

## Livrable 4 — Analyse détaillée de 2 crashs

### Inventaire et triage

```bash
$ for crash in out_ch15/default/crashes/id:*; do
    echo "=== $(basename "$crash") ($(wc -c < "$crash") octets) ==="
    ./fileformat_afl_asan "$crash" 2>&1 | grep "^SUMMARY:" || echo "Pas de rapport ASan"
    echo ""
  done
```

Sortie typique :

```
=== id:000000,sig:06,src:000008,time:3241,... (28 octets) ===
SUMMARY: AddressSanitizer: heap-buffer-overflow fileformat.c:87 in decode_section

=== id:000001,sig:11,src:000019,time:7830,... (34 octets) ===
SUMMARY: AddressSanitizer: SEGV fileformat.c:142 in process_index_section

=== id:000002,sig:06,src:000024,time:11205,... (26 octets) ===
SUMMARY: AddressSanitizer: heap-buffer-overflow fileformat.c:87 in decode_section

=== id:000003,sig:11,src:000019,time:14782,... (41 octets) ===
SUMMARY: AddressSanitizer: SEGV fileformat.c:142 in process_index_section

=== id:000004,sig:06,src:000037,time:21490,... (52 octets) ===
SUMMARY: AddressSanitizer: stack-buffer-overflow fileformat.c:201 in validate_checksum
```

Trois groupes de bugs distincts :

| Groupe | Crashs | Fonction | Type |  
|--------|--------|----------|------|  
| A | 000000, 000002 | `decode_section` | heap-buffer-overflow |  
| B | 000001, 000003 | `process_index_section` | SEGV |  
| C | 000004 | `validate_checksum` | stack-buffer-overflow |

On analyse en détail un représentant du groupe A et un du groupe B (les deux plus fréquents).

---

### Crash A — `decode_section` : heap-buffer-overflow

#### Minimisation

```bash
$ afl-tmin -i out_ch15/default/crashes/id:000000,sig:06,... \
           -o crash_A_min.bin \
           -- ./fileformat_afl @@
```

```bash
$ wc -c out_ch15/default/crashes/id:000000,sig:06,...
28
$ wc -c crash_A_min.bin
18
```

L'input passe de 28 à 18 octets.

#### Examen hexadécimal

```bash
$ xxd crash_A_min.bin
00000000: 4353 544d 0100 0100 0100 0000 2000 0000  CSTM........  ..
00000010: 4100                                     A.
```

#### Interprétation des champs

```
Offset  Hex             Interprétation
──────  ──────────────  ─────────────────────────────────────────
0x00    43 53 54 4d     Magic "CSTM" — validé par parse_header
0x04    01              Version = 1 — branche v1
0x05    00              Flags = 0 (pas de flag spécial)
0x06    01 00           Section count = 1 (uint16_t LE)
0x08    01              Type de section = 0x01 (DATA)
0x09    00 00 00        Padding / réservé
0x0c    20 00 00 00     Longueur déclarée = 32 (uint32_t LE)
0x10    41 00           Payload réel : seulement 2 octets
```

Le bug : la longueur déclarée (32 octets) dépasse largement le payload réel (2 octets). La fonction `decode_section` tente de lire 32 octets à partir de l'offset 0x10, provoquant un accès hors limites du buffer heap.

#### Trace GDB

```bash
$ gdb -q ./fileformat_afl_asan
(gdb) run crash_A_min.bin
```

Rapport ASan :

```
==XXXXX==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6020000000XX
READ of size 32 at 0x6020000000XX thread T0
    #0 0xXXXXXX in decode_section fileformat.c:87
    #1 0xXXXXXX in parse_sections fileformat.c:113
    #2 0xXXXXXX in main fileformat.c:178
```

On relance avec un breakpoint au début de `decode_section` pour tracer le chemin :

```
(gdb) break decode_section
(gdb) run crash_A_min.bin

Breakpoint 1, decode_section (...)
(gdb) backtrace
#0  decode_section (data=..., offset=16, length=32, type=1) at fileformat.c:80
#1  parse_sections (file_data=..., file_size=18, header=...) at fileformat.c:113
#2  main (argc=2, argv=...) at fileformat.c:178
```

La fonction `decode_section` est appelée avec `offset=16`, `length=32`, et `type=1`. Le `file_size` total est 18. La lecture de 32 octets à l'offset 16 dépasse la fin du fichier (16 + 32 = 48 > 18).

#### Chemin de conditions reconstitué

```
main()
  → fichier ouvert, lu en mémoire (18 octets)
  → parse_header()
      → file_size >= 8            ✓ (18 ≥ 8)
      → magic == "CSTM"          ✓
      → version == 1              ✓
      → section_count = 1         (lu à offset 0x06, uint16_t LE)
  → parse_sections()
      → pour chaque section (i=0..0) :
          → type = data[8] = 0x01 (DATA)
          → length = *(uint32_t*)(data+12) = 32
          → decode_section(data, offset=16, length=32, type=1)
              → memcpy(buf, data+16, 32)   ← CRASH : 16+32 > 18
```

#### Connaissances RE extraites

- Le header fait 8 octets : magic (4) + version (1) + flags (1) + section_count (2).  
- Chaque descripteur de section fait 8 octets : type (1) + padding (3) + length (4).  
- Le payload commence immédiatement après le descripteur.  
- La fonction `decode_section` ne vérifie pas que `offset + length ≤ file_size`.

---

### Crash B — `process_index_section` : SEGV

#### Minimisation

```bash
$ afl-tmin -i out_ch15/default/crashes/id:000001,sig:11,... \
           -o crash_B_min.bin \
           -- ./fileformat_afl @@
```

```bash
$ wc -c crash_B_min.bin
24
```

#### Examen hexadécimal

```bash
$ xxd crash_B_min.bin
00000000: 4353 544d 0100 0100 0200 0000 0800 0000  CSTM............
00000010: ff00 0000 0000 0000                      ........
```

#### Interprétation des champs

```
Offset  Hex             Interprétation
──────  ──────────────  ─────────────────────────────────────────
0x00    43 53 54 4d     Magic "CSTM"
0x04    01              Version = 1
0x05    00              Flags = 0
0x06    01 00           Section count = 1
0x08    02              Type de section = 0x02 (INDEX)
0x09    00 00 00        Padding
0x0c    08 00 00 00     Longueur = 8 (cette fois cohérente avec le payload)
0x10    ff 00 00 00     Payload INDEX : premier champ = 0xff (255)
0x14    00 00 00 00     Payload INDEX : second champ = 0
```

Le type INDEX (0x02) déclenche un traitement différent de DATA. Le payload semble contenir des entrées d'index. La valeur `0xff` (255) est vraisemblablement utilisée comme index dans un tableau ou un offset dans les données — une valeur hors limites qui provoque le SEGV.

#### Trace GDB

```bash
$ gdb -q ./fileformat_afl_asan
(gdb) run crash_B_min.bin
```

```
==XXXXX==ERROR: AddressSanitizer: SEGV on unknown address 0x0000000000XX
    #0 0xXXXXXX in process_index_section fileformat.c:142
    #1 0xXXXXXX in decode_section fileformat.c:95
    #2 0xXXXXXX in parse_sections fileformat.c:113
    #3 0xXXXXXX in main fileformat.c:178
```

Analyse du point de crash :

```
(gdb) break process_index_section
(gdb) run crash_B_min.bin

Breakpoint 1, process_index_section (section_data=..., section_len=8)
(gdb) x/8bx section_data
0x...: 0xff  0x00  0x00  0x00  0x00  0x00  0x00  0x00

(gdb) next
(gdb) next
(gdb) info locals
index_entry = 255
```

La fonction lit le premier uint32_t du payload (valeur 255) et l'utilise comme index pour accéder à un tableau interne. Le tableau ne contient pas 256 entrées — d'où le SEGV.

#### Chemin de conditions reconstitué

```
main()
  → parse_header()  — identique au crash A
  → parse_sections()
      → type = 0x02 (INDEX)
      → length = 8
      → decode_section() dispatche vers process_index_section()
          → index_entry = *(uint32_t*)(section_data+0) = 255
          → accès table[255]   ← CRASH : index hors limites
```

#### Connaissances RE extraites

- Le type 0x02 (INDEX) a un traitement spécifique dans `process_index_section`.  
- Le payload INDEX contient des entrées uint32_t utilisées comme index.  
- Aucune vérification de bornes n'est effectuée sur ces index.  
- La structure du payload INDEX : tableau de uint32_t, chaque entrée est un index dans une table interne (probablement la table des sections ou une table de données).

---

## Livrable 5 — Cartographie du format

### Tableau des champs identifiés

```
Offset  Taille  Champ               Valeurs / Contraintes
──────  ──────  ──────────────────  ──────────────────────────────────────
0x00    4       Magic               "CSTM" (0x43 0x53 0x54 0x4d) — obligatoire
0x04    1       Version             0x01, 0x02 confirmés ; 0x03 probable (non testé)
0x05    1       Flags               0x00 observé ; rôle exact inconnu
0x06    2       Section Count       uint16_t LE — nombre de sections dans le fichier
0x08    1       Section Type        0x01=DATA, 0x02=INDEX, 0x03=META
0x09    3       Section Reserved    Toujours 0x00 dans les inputs observés
0x0c    4       Section Length      uint32_t LE — taille du payload en octets
0x10    N       Section Payload     Contenu variable selon le type de section
```

Pour les fichiers multi-sections, les descripteurs (type + reserved + length) et payloads se succèdent séquentiellement à partir de l'offset 0x08.

### Structure visuelle

```
┌─────────────────────── FILE HEADER (8 octets) ──────────────────────┐
│  Magic (4B)  │  Version (1B)   │  Flags (1B)  │  Section Count (2B) │
│   "CSTM"     │    0x01-0x03    │    0x00 ?    │     uint16_t LE     │
├──────────────────── SECTION 0 — DESCRIPTOR (8 octets) ──────────────┤
│  Type (1B)   │  Reserved (3B)  │         Length (4B, LE)            │
│  01/02/03    │   00 00 00      │         uint32_t                   │
├──────────────────── SECTION 0 — PAYLOAD (Length octets) ────────────┤
│                    Contenu dépendant du type                        │
│  DATA  (0x01) : données brutes                                      │
│  INDEX (0x02) : tableau de uint32_t (index dans une table interne)  │
│  META  (0x03) : format à déterminer                                 │
├──────────────────── SECTION 1 — DESCRIPTOR ─────────────────────────┤
│  ...                                                                │
├──────────────────── SECTION 1 — PAYLOAD ────────────────────────────┤
│  ...                                                                │
├──────────────────── CHECKSUM (position et format TBD) ──────────────┤
│  Mentionné dans les strings ("Checksum mismatch: expected 0x%08x")  │
│  Probablement uint32_t — algorithme et offset non déterminés        │
└─────────────────────────────────────────────────────────────────────┘
```

### Bugs documentés

| ID | Fonction | Type | Description | Champs impliqués |  
|----|----------|------|-------------|------------------|  
| A | `decode_section` (l.87) | heap-buffer-overflow READ | Lecture de `length` octets sans vérifier que `offset + length ≤ file_size` | Section Length (0x0c) |  
| B | `process_index_section` (l.142) | SEGV (index OOB) | Valeur uint32_t du payload utilisée comme index de tableau sans validation | Payload INDEX (0x10+) |  
| C | `validate_checksum` (l.201) | stack-buffer-overflow WRITE | Buffer de travail de 1024 octets (pour 256 sections max) indexé par `section_count` sans vérification de bornes | Section Count (0x06) |

### Questions ouvertes pour le Chapitre 25

1. **Checksum** — Algorithme (CRC32 ? somme ? XOR ?), position dans le fichier (fin de fichier ? dans le header ?), et quels octets couvre-t-il ?  
2. **Version 3** — Quels champs additionnels ? Lien avec `verify_signature` ?  
3. **Compression** — `decompress_section` n'a jamais été atteinte. Quel flag l'active ? Quel algorithme ?  
4. **Payload META** — Quelle structure interne ? Le crash C dans `validate_checksum` y est-il lié ?  
5. **Champ Flags (0x05)** — Aucun crash ni branche observée en variant ce champ. Quel est son rôle réel ?

---

## Commandes récapitulatives

Pour référence, voici l'enchaînement complet des commandes de ce checkpoint :

```bash
# === COMPILATION ===
cd binaries/ch15-fileformat/  
make clean  
make fuzz          # produit fileformat_afl et fileformat_afl_asan  
make coverage      # produit fileformat_gcov  

# === CORPUS & DICTIONNAIRE ===
mkdir corpus_initial  
printf 'CSTM\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_initial/s01.bin  
printf 'CSTM\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_initial/s02.bin  
# (+ seeds s03, s04, s05 avec sections DATA, INDEX, META — voir ci-dessus)
# (+ dict_ch25.txt — voir ci-dessus)

# === VÉRIFICATION SEEDS ===
afl-showmap -o /dev/stdout -- ./fileformat_afl corpus_initial/s01.bin 2>/dev/null | wc -l

# === CONFIGURATION SYSTÈME ===
echo core | sudo tee /proc/sys/kernel/core_pattern

# === FUZZING ===
afl-fuzz -i corpus_initial -o out_ch15 -x dict_ch25.txt -m none -- ./fileformat_afl @@
# (Ctrl+C après 15-30 min ou quand corpus ≥ 20 et crashes ≥ 2)

# === TRIAGE DES CRASHS ===
for crash in out_ch15/default/crashes/id:*; do
    echo "=== $(basename "$crash") ==="
    ./fileformat_afl_asan "$crash" 2>&1 | grep "^SUMMARY:"
done

# === MINIMISATION ===
afl-tmin -i out_ch15/default/crashes/id:000000,... -o crash_A_min.bin -- ./fileformat_afl @@  
afl-tmin -i out_ch15/default/crashes/id:000001,... -o crash_B_min.bin -- ./fileformat_afl @@  

# === ANALYSE ===
xxd crash_A_min.bin  
xxd crash_B_min.bin  
gdb -q ./fileformat_afl_asan -ex "run crash_A_min.bin"  
gdb -q ./fileformat_afl_asan -ex "run crash_B_min.bin"  

# === COUVERTURE (optionnel) ===
lcov --directory . --zerocounters  
for f in out_ch15/default/queue/id:*; do ./fileformat_gcov "$f" 2>/dev/null; done  
for f in out_ch15/default/crashes/id:*; do ./fileformat_gcov "$f" 2>/dev/null; done  
lcov --directory . --capture --output-file cov.info  
lcov --remove cov.info '/usr/*' --output-file cov_filtered.info  
genhtml cov_filtered.info --output-directory cov_html/  
```

---

## Auto-évaluation

| Critère | Votre résultat | Niveau |  
|---------|---------------|--------|  
| Build AFL++ + ASan fonctionnels, `afl-showmap` OK | | ☐ Acquis ☐ Maîtrisé |  
| 3+ seeds ciblés, 10+ tokens dans le dictionnaire | | ☐ Acquis ☐ Maîtrisé |  
| corpus ≥ 20, crashes ≥ 2 | | ☐ Acquis ☐ Maîtrisé |  
| 2 crashs reproduits, minimisés, tracés dans GDB | | ☐ Acquis ☐ Maîtrisé |  
| 4+ champs du format identifiés | | ☐ Acquis ☐ Maîtrisé |

Si vous avez atteint le niveau « Acquis » sur les 5 critères, vous maîtrisez les fondamentaux du fuzzing orienté RE. Si vous avez également produit un rapport de couverture `lcov` et identifié les fonctions non couvertes avec des hypothèses sur les conditions bloquantes, vous êtes au niveau « Maîtrisé ».

---


⏭️
