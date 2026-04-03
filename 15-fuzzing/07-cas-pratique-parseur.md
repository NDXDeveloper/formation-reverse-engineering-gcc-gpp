🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 15.7 — Cas pratique : découvrir des chemins cachés dans un parseur binaire

> 🔗 **Prérequis** : Sections 15.1 à 15.6 (toutes les techniques vues dans ce chapitre), Chapitre 5 (triage rapide), Chapitre 8 (Ghidra)  
> 📦 **Binaire utilisé** : `binaries/ch15-fileformat/` — parseur de format de fichier custom compilé avec GCC  
> 🛠️ **Outils** : AFL++, libFuzzer (Clang), GDB/GEF, `afl-cmin`, `afl-tmin`, `afl-cov`, `lcov`, `strings`, `xxd`, Ghidra, ImHex

---

## Objectif

Ce cas pratique applique, de bout en bout, la méthodologie de fuzzing orientée RE construite tout au long de ce chapitre. Le binaire cible est le parseur de format custom fourni dans `binaries/ch15-fileformat/` — le même qui sera analysé en détail au Chapitre 25. Ici, on l'aborde sous l'angle du fuzzing : l'objectif n'est pas de produire une spécification complète du format (c'est le travail du Chapitre 25), mais de **découvrir le maximum de chemins d'exécution** et d'extraire les connaissances structurelles que le fuzzer révèle.

À la fin de ce cas pratique, nous aurons :

- Un corpus minimisé qui exerce l'ensemble des branches atteignables du parseur.  
- Un dictionnaire construit à partir de l'analyse du binaire.  
- Un rapport de couverture identifiant les zones couvertes et non couvertes.  
- Une cartographie partielle du format d'entrée, reconstruite uniquement à partir des résultats du fuzzing.  
- Une liste de questions précises à emporter vers l'analyse statique approfondie du Chapitre 25.

---

## Phase 1 — Triage rapide du binaire

Avant de fuzzer, on applique le workflow de triage du Chapitre 5 pour collecter les informations de base.

### Identification

```bash
$ cd binaries/ch15-fileformat/
$ file fileformat_O0
fileformat_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, not stripped  
```

Points notables : binaire ELF 64-bit, dynamiquement linké, PIE activé, **non strippé** — les symboles sont présents, ce qui facilitera la corrélation couverture/fonctions.

### Protections

```bash
$ checksec --file=fileformat_O0
    Arch:     amd64-64-little
    RELRO:    Full RELRO
    Stack:    Canary found
    NX:       NX enabled
    PIE:      PIE enabled
```

Toutes les protections sont actives. Pour le fuzzing, cela n'a pas d'impact direct — on ne cherche pas à exploiter mais à explorer. Le canary provoquera un `SIGABRT` (sig:06) en cas de stack buffer overflow, ce qui est détecté par le fuzzer.

### Extraction de chaînes

```bash
$ strings fileformat_O0 | head -40
```

Parmi les chaînes extraites (résultat typique) :

```
Usage: %s <input_file>  
Error: cannot open file  
Error: file too small  
CSTM  
Error: invalid magic  
Error: unsupported version %d  
Parsing header...  
Section type: DATA  
Section type: INDEX  
Section type: META  
Error: unknown section type 0x%02x  
Decoding section at offset %d, length %d  
Error: section length exceeds file size  
Checksum mismatch: expected 0x%08x, got 0x%08x  
Processing complete: %d sections parsed  
```

Ces chaînes sont un trésor d'information pour le fuzzer :

- Magic bytes : `CSTM` (4 octets).  
- Le format a un champ de version.  
- Trois types de sections : `DATA`, `INDEX`, `META`.  
- Les sections ont un offset, une longueur, et un type encodé sur un octet (format `0x%02x`).  
- Un **checksum** est vérifié — c'est potentiellement un obstacle majeur pour le fuzzer.  
- Le parseur traite les sections séquentiellement.

### Exécution de test

```bash
$ echo "test" > /tmp/test_input.bin
$ ./fileformat_O0 /tmp/test_input.bin
Error: file too small

$ printf 'CSTM\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > /tmp/test_magic.bin
$ ./fileformat_O0 /tmp/test_magic.bin
Parsing header...  
Error: unsupported version 0  
```

Le magic `CSTM` est validé, et le champ de version se situe juste après. On avance déjà dans la compréhension du format.

---

## Phase 2 — Construction du corpus initial et du dictionnaire

### Corpus initial

À partir du triage, on construit un seed par hypothèse de structure :

```bash
$ mkdir corpus_ff

# Seed 1 : magic + version 1 + padding minimal (16 octets)
$ printf 'CSTM\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_ff/v1_minimal.bin

# Seed 2 : magic + version 2 + padding
$ printf 'CSTM\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_ff/v2_minimal.bin

# Seed 3 : magic + version 1 + taille plus grande (32 octets)
#   pour atteindre potentiellement le parsing de sections
$ python3 -c "
import sys  
header = b'CSTM\x01\x00\x00\x00'  
padding = b'\x00' * 24  
sys.stdout.buffer.write(header + padding)  
" > corpus_ff/v1_padded.bin

# Seed 4 : essai avec un type de section DATA (0x01 hypothétique)
$ python3 -c "
import sys  
header = b'CSTM\x01\x00\x01\x00'  # magic, version 1, 1 section?  
section = b'\x01\x00\x00\x00\x08\x00\x00\x00'  # type=1, length=8?  
data = b'AAAAAAAA'  
sys.stdout.buffer.write(header + section + data)  
" > corpus_ff/v1_one_section.bin
```

Ces seeds sont des hypothèses — on ne connaît pas encore la structure exacte du header au-delà du magic et de la version. C'est normal : le fuzzer va explorer les combinaisons et nous montrer ce qui passe les validations.

### Dictionnaire

```bash
$ cat > dict_fileformat.txt << 'EOF'
# Magic bytes
magic="CSTM"

# Versions probables
v1="\x01"  
v2="\x02"  
v3="\x03"  

# Types de section (hypothèses depuis les strings)
# DATA, INDEX, META — les valeurs numériques restent à découvrir
type_01="\x01"  
type_02="\x02"  
type_03="\x03"  

# Mots-clés trouvés dans les strings
kw_data="DATA"  
kw_index="INDEX"  
kw_meta="META"  

# Valeurs limites pour les champs numériques
zero_32="\x00\x00\x00\x00"  
ff_32="\xff\xff\xff\xff"  
one_32="\x01\x00\x00\x00"  
max_short="\xff\xff"  
one_short="\x01\x00"  

# Tailles intéressantes (small values)
len_8="\x08\x00\x00\x00"  
len_16="\x10\x00\x00\x00"  
len_64="\x40\x00\x00\x00"  
len_256="\x00\x01\x00\x00"  
EOF  
```

Ce dictionnaire de 20 tokens combine les informations du triage (`strings`, exécution de test) et des hypothèses sur la structure (champs 32 bits little-endian pour les tailles). Il sera enrichi après les premiers résultats.

---

## Phase 3 — Compilation instrumentée

On prépare trois builds du binaire :

```bash
# Build 1 : AFL++ instrumenté (pour le fuzzing principal)
# Build 2 : AFL++ instrumenté + ASan (pour le triage des crashs)
$ cd binaries/ch15-fileformat/
$ make clean
$ make fuzz

# Build 3 : GCC avec couverture (pour le rapport lcov)
$ make coverage
```

La cible `fuzz` produit `fileformat_afl` et `fileformat_afl_asan`. La cible `coverage` produit `fileformat_gcov`. On peut aussi compiler directement sans Makefile :

```bash
$ afl-gcc -O0 -g -o fileformat_afl fileformat.c
$ AFL_USE_ASAN=1 afl-gcc -O0 -g -o fileformat_afl_asan fileformat.c
$ gcc --coverage -O0 -g -o fileformat_gcov fileformat.c
```

Vérification de l'instrumentation :

```bash
$ afl-showmap -o /dev/stdout -- ./fileformat_afl corpus_ff/v1_minimal.bin 2>/dev/null | wc -l
12
```

12 edges couverts par le premier seed — le binaire est correctement instrumenté et le seed atteint au moins quelques branches du parseur.

### Build libFuzzer (optionnel)

Si on veut aussi fuzzer avec libFuzzer, il faut isoler la fonction de parsing et écrire un harness. En examinant rapidement le source (ou le désassemblage dans Ghidra), on identifie la fonction principale de parsing — appelons-la `parse_file` :

```c
// fuzz_fileformat.c — Harness libFuzzer
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

// Prototype de la fonction de parsing (extrait du header ou déduit)
int parse_file(const char *filename);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char tmpfile[] = "/dev/shm/fuzz_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) return 0;
    write(fd, data, size);
    close(fd);

    parse_file(tmpfile);

    unlink(tmpfile);
    return 0;
}
```

On utilise `/dev/shm/` (tmpfs en RAM) pour éviter les I/O disque. Compilation :

```bash
$ clang -fsanitize=fuzzer,address,undefined -g -O1 \
    -o fuzz_fileformat fuzz_fileformat.c fileformat.c
```

---

## Phase 4 — Campagne de fuzzing AFL++

### Configuration système

```bash
$ echo core | sudo tee /proc/sys/kernel/core_pattern
$ echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null
```

### Lancement

```bash
$ afl-fuzz -i corpus_ff -o out_ff -x dict_fileformat.txt \
    -m none -- ./fileformat_afl @@
```

Le `-m none` anticipe un éventuel usage d'ASan lors des phases ultérieures (et ne gêne pas sans ASan).

### Observations pendant le fuzzing

Après quelques minutes, le tableau de bord AFL++ montre typiquement :

```
        run time : 0 days, 0 hrs, 5 min, 12 sec
   last new find : 0 days, 0 hrs, 0 min, 8 sec
 corpus count    : 47
 saved crashes   : 5
 map density     : 3.21% / 5.88%
 exec speed      : 4823/sec
```

**Ce qu'on observe :**

- **47 inputs dans le corpus** en 5 minutes — le fuzzer découvre activement de nouveaux chemins. Le dictionnaire a accéléré le passage des premières validations (magic, version).  
- **5 crashs** — des chemins qui mènent à des erreurs mémoire. À analyser en phase 5.  
- **3.21% de couverture bitmap** — une fraction relativement petite, ce qui est normal pour un parseur avec beaucoup de branches. Il reste de l'espace à explorer.  
- **4823 exec/s** — bonne vitesse pour un binaire qui lit un fichier depuis le disque.

On laisse tourner le fuzzer pendant 30 minutes à 1 heure. Si `last new find` dépasse 10-15 minutes sans bouger, le fuzzer a probablement convergé dans sa configuration actuelle.

### Lancer une instance parallèle (optionnel)

Dans un second terminal :

```bash
$ afl-fuzz -i corpus_ff -o out_ff -x dict_fileformat.txt \
    -S secondary01 -- ./fileformat_afl @@
```

> 💡 **Observation clé** — Regardez le `corpus count` augmenter. Chaque nouvel input dans le corpus est un chemin que le fuzzer n'avait jamais emprunté. Les premières minutes montrent une croissance rapide (le fuzzer passe les validations de base), puis la progression ralentit (les branches profondes sont plus difficiles à atteindre). Ce ralentissement est le signal que le dictionnaire et le corpus pourraient bénéficier d'un enrichissement.

---

## Phase 5 — Analyse des crashs

### Inventaire

```bash
$ ls out_ff/default/crashes/
README.txt  
id:000000,sig:06,src:000012,time:4231,execs:20147,op:havoc,rep:4  
id:000001,sig:11,src:000023,time:8920,execs:43021,op:havoc,rep:8  
id:000002,sig:06,src:000031,time:12847,execs:62109,op:havoc,rep:2  
id:000003,sig:11,src:000023,time:15203,execs:73488,op:splice,rep:4  
id:000004,sig:06,src:000041,time:22519,execs:108744,op:havoc,rep:16  
```

Deux signaux présents : SIGABRT (sig:06, probablement le canary ou un `assert`) et SIGSEGV (sig:11, accès mémoire invalide).

### Triage rapide avec ASan

```bash
$ for crash in out_ff/default/crashes/id:*; do
    echo "=== $(basename $crash) ==="
    ./fileformat_afl_asan "$crash" 2>&1 | grep "^SUMMARY:" || echo "No ASan report"
    echo ""
  done
```

Résultat typique :

```
=== id:000000,sig:06,... ===
SUMMARY: AddressSanitizer: heap-buffer-overflow fileformat.c:87 in decode_section

=== id:000001,sig:11,... ===
SUMMARY: AddressSanitizer: SEGV fileformat.c:142 in process_index_section

=== id:000002,sig:06,... ===
SUMMARY: AddressSanitizer: heap-buffer-overflow fileformat.c:87 in decode_section

=== id:000003,sig:11,... ===
SUMMARY: AddressSanitizer: SEGV fileformat.c:142 in process_index_section

=== id:000004,sig:06,... ===
SUMMARY: AddressSanitizer: stack-buffer-overflow fileformat.c:201 in validate_checksum
```

On identifie **trois bugs distincts** :

| Groupe | Crashs | Fonction | Type | Ligne |  
|--------|--------|----------|------|-------|  
| A | 000000, 000002 | `decode_section` | heap-buffer-overflow | 87 |  
| B | 000001, 000003 | `process_index_section` | SEGV | 142 |  
| C | 000004 | `validate_checksum` | stack-buffer-overflow | 201 |

### Analyse détaillée du crash A

On choisit le crash 000000 (le plus petit du groupe A) et on le minimise :

```bash
$ afl-tmin -i out_ff/default/crashes/id:000000,sig:06,... \
           -o crash_A_min.bin \
           -- ./fileformat_afl @@
```

Examen de l'input minimisé :

```bash
$ xxd crash_A_min.bin
00000000: 4353 544d 0100 0100 0100 0000 2000 0000  CSTM........  ..
00000010: 4141 4141 4141 4141                      AAAAAAAA
```

Interprétation hypothétique (à vérifier dans GDB) :

```
Offset 0x00-0x03 : "CSTM"         — magic (validé)  
Offset 0x04      : 0x01           — version 1 (validé)  
Offset 0x05      : 0x00           — (flags? padding?)  
Offset 0x06-0x07 : 0x01 0x00      — nombre de sections = 1 (LE 16-bit)  
Offset 0x08      : 0x01           — type de section (DATA = 0x01?)  
Offset 0x09-0x0b : 0x00 0x00 0x00 — (padding? offset?)  
Offset 0x0c-0x0f : 0x20 0x00 0x00 0x00 — longueur déclarée = 32 (0x20)  
Offset 0x10-0x17 : "AAAAAAAA"     — début du payload (8 octets réels)  
```

Le bug est clair : la section déclare une longueur de 32 octets (`0x20`), mais le fichier ne contient que 8 octets de payload après le header de section. La fonction `decode_section` lit 32 octets à partir de l'offset du payload et dépasse la fin du buffer alloué.

### Vérification dans GDB

```bash
$ gdb -q ./fileformat_afl_asan
(gdb) run crash_A_min.bin
```

Le rapport ASan confirme :

```
READ of size 32 at 0x602000000030
0x602000000030 is located 8 bytes after 24-byte region [0x602000000010,0x602000000028)
```

La lecture de 32 octets commence 8 octets avant la fin du buffer de 24 octets — exactement ce que prédit notre interprétation.

### Connaissances RE extraites du crash A

Ce seul crash nous a appris :

- La structure du header : magic (4 octets), version (1 octet), flags/padding (1 octet), nombre de sections (2 octets, LE).  
- Le format d'un descripteur de section : type (1 octet), padding (3 octets), longueur (4 octets, LE).  
- La fonction `decode_section` lit le payload selon la longueur déclarée, sans vérifier qu'elle ne dépasse pas la taille réelle du fichier.  
- Le type `0x01` correspond probablement à DATA.

### Analyse rapide des crashs B et C

**Crash B** (`process_index_section`, SEGV ligne 142) — en examinant l'input minimisé, on découvre que le type de section `0x02` (INDEX) déclenche un traitement différent qui déréférence un pointeur calculé à partir des données de la section. Un index hors limites dans le payload provoque le SEGV.

**Crash C** (`validate_checksum`, stack-buffer-overflow ligne 201) — ce crash est particulièrement intéressant : il prouve que le parseur **calcule et vérifie un checksum**. Le fuzzer a réussi à atteindre la fonction de validation malgré le checksum incorrect — probablement parce que la vérification se fait *après* le décodage, pas avant. Le buffer overflow dans `validate_checksum` indique que le buffer de travail est dimensionné pour un nombre fixe de sections (256), mais que le champ `section_count` du header n'est pas plafonné — une valeur supérieure à 256 provoque une écriture hors limites sur la pile.

---

## Phase 6 — Rapport de couverture

### Génération du rapport

```bash
# Réinitialiser les compteurs
$ lcov --directory . --zerocounters

# Rejouer tout le corpus (queue + crashs) sur le binaire gcov
$ for f in out_ff/default/queue/id:*; do
    ./fileformat_gcov "$f" 2>/dev/null
  done
$ for f in out_ff/default/crashes/id:*; do
    ./fileformat_gcov "$f" 2>/dev/null
  done

# Capturer et générer le rapport
$ lcov --directory . --capture --output-file cov_ff.info
$ lcov --remove cov_ff.info '/usr/*' --output-file cov_ff_filtered.info
$ genhtml cov_ff_filtered.info --output-directory cov_report/
$ firefox cov_report/index.html &
```

### Lecture du rapport

Résultat typique après 30 minutes de fuzzing avec corpus ciblé et dictionnaire :

```
Overall coverage rate:
  Lines:     67.3% (148 of 220 lines)
  Functions: 85.7% (12 of 14 functions)
```

**Fonctions couvertes (12/14) :**

- `main` — 100%  
- `parse_header` — 95% (une branche de version non atteinte)  
- `decode_section` — 88%  
- `process_data_section` — 72%  
- `process_index_section` — 81%  
- `process_meta_section` — 63%  
- `validate_checksum` — 45%  
- ... et quelques fonctions utilitaires à 100%

**Fonctions non couvertes (2/14) :**

- `decompress_section` — 0%  
- `verify_signature` — 0%

### Interprétation des zones non couvertes

**`validate_checksum` à 45%.** Le fuzzer atteint la fonction mais ne couvre pas toutes ses branches. La moitié non couverte correspond probablement au chemin « checksum correct » — le fuzzer produit des inputs dont le checksum est aléatoire, donc la validation échoue systématiquement et le chemin « checksum OK, continuer le traitement » n'est jamais emprunté. C'est l'obstacle classique décrit en section 15.5.

**`decompress_section` à 0%.** Jamais appelée. En vérifiant les XREF dans Ghidra, on découvre qu'elle est appelée depuis `decode_section` uniquement quand un flag de compression est activé dans le descripteur de section. Le fuzzer n'a pas découvert ce flag — on peut ajouter un seed avec ce flag activé.

**`verify_signature` à 0%.** Jamais appelée. L'XREF montre qu'elle est appelée depuis `parse_header` uniquement pour la version 3 du format. Le fuzzer a exploré les versions 1 et 2 mais n'a pas généré d'input valide pour la version 3 — ou la version 3 a des prérequis supplémentaires. À explorer manuellement dans Ghidra.

---

## Phase 7 — Enrichissement et second cycle

Les résultats de la phase 6 orientent directement les actions suivantes.

### Enrichir le corpus

```bash
# Seed pour la version 3 (débloquer verify_signature)
$ printf 'CSTM\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_ff/v3_explore.bin

# Seed avec flag de compression activé (débloquer decompress_section)
# Hypothèse : le flag est le bit 7 du champ type de section
$ python3 -c "
import sys  
header = b'CSTM\x01\x00\x01\x00'  
section = b'\x81\x00\x00\x00\x10\x00\x00\x00'  # type=0x81 (DATA+compressed?), len=16  
data = b'\x00' * 16  
sys.stdout.buffer.write(header + section + data)  
" > corpus_ff/v1_compressed.bin
```

### Enrichir le dictionnaire

```bash
$ cat >> dict_fileformat.txt << 'EOF'

# Ajouts après phase 5-6
v3="\x03"  
compressed_flag="\x80"  
type_data_compressed="\x81"  
type_index_compressed="\x82"  
type_meta_compressed="\x83"  
EOF  
```

### Minimiser et relancer

```bash
# Minimiser le corpus accumulé
$ afl-cmin -i out_ff/default/queue/ -o corpus_ff_min -- ./fileformat_afl @@

# Ajouter les nouveaux seeds
$ cp corpus_ff/v3_explore.bin corpus_ff/v1_compressed.bin corpus_ff_min/

# Relancer le fuzzing avec le corpus enrichi
$ rm -rf out_ff_v2
$ afl-fuzz -i corpus_ff_min -o out_ff_v2 -x dict_fileformat.txt \
    -m none -- ./fileformat_afl @@
```

Après ce second cycle, on s'attend à voir la couverture augmenter — en particulier dans `decompress_section` et `verify_signature` si nos hypothèses sur le flag de compression et la version 3 sont correctes.

Si la couverture de `validate_checksum` stagne toujours, c'est le signal qu'un harness libFuzzer avec calcul automatique du checksum est nécessaire — ou que l'exécution symbolique (Chapitre 18) doit être mobilisée pour résoudre la contrainte.

---

## Synthèse des connaissances acquises

Après deux cycles de fuzzing totalisant environ 1 à 2 heures, voici ce que le fuzzer a révélé sur le format `ch15-fileformat` — **sans avoir lu une seule ligne de code source ni de désassemblage en profondeur** :

### Structure du format (reconstituée)

```
┌─────────────────────────────────────────────────────┐
│                    FILE HEADER                      │
├──────────┬──────────┬────────────┬──────────────────┤
│  Magic   │ Version  │   Flags    │  Section Count   │
│  4 bytes │  1 byte  │   1 byte   │   2 bytes (LE)   │
│  "CSTM"  │  1,2,3   │   (TBD)    │                  │
├──────────┴──────────┴────────────┴──────────────────┤
│                                                     │
│              SECTION DESCRIPTOR (×N)                │
├──────────┬──────────────────┬───────────────────────┤
│   Type   │    Padding (3B)  │   Length (4B, LE)     │
│  1 byte  │                  │                       │
│  0x01=DATA  0x02=INDEX  0x03=META                   │
│  bit 7 = compression flag (hypothèse)               │
├──────────┴──────────────────┴───────────────────────┤
│                                                     │
│                 SECTION PAYLOAD                     │
│            (Length octets par section)              │
│                                                     │
├─────────────────────────────────────────────────────┤
│                    CHECKSUM                         │
│        (position et algorithme à déterminer)        │
└─────────────────────────────────────────────────────┘
```

### Bugs identifiés

| ID | Fonction | Type | Cause |  
|----|----------|------|-------|  
| A | `decode_section` | heap overflow read | Longueur de section non validée vs taille fichier |  
| B | `process_index_section` | null/OOB deref | Index dans le payload utilisé sans vérification |  
| C | `validate_checksum` | stack overflow write | Buffer de 1024 octets indexé par section_count (max 65535) sans borne |

### Questions ouvertes (à emporter au Chapitre 25)

Les questions suivantes n'ont pas été résolues par le fuzzing et nécessitent une analyse statique approfondie :

1. **Algorithme de checksum** — quelle fonction est utilisée (CRC32 ? somme simple ? custom ?) et à quel offset se trouve le champ checksum dans le fichier ?  
2. **Format de la version 3** — quels champs supplémentaires sont attendus ? Que fait `verify_signature` exactement ?  
3. **Algorithme de compression** — `decompress_section` utilise-t-elle zlib, LZ4, ou un algorithme custom ?  
4. **Structure interne des sections INDEX et META** — les crashs ont montré qu'elles ont un traitement spécifique, mais le format de leur payload reste à documenter.  
5. **Rôle exact du champ flags (offset 0x05)** — aucun crash n'a impliqué ce champ directement.

Ces questions sont des points d'entrée ciblés pour Ghidra : au lieu de lire le binaire de bout en bout, on sait exactement quelles fonctions examiner et quelles données chercher.

---

## Récapitulatif méthodologique

Ce cas pratique a suivi le cycle complet de fuzzing orienté RE :

```
Phase 1 — Triage rapide
    │   file, strings, checksec, exécution de test
    ▼
Phase 2 — Corpus initial + dictionnaire
    │   Seeds ciblés par branche, tokens depuis strings et Ghidra
    ▼
Phase 3 — Compilation instrumentée
    │   afl-gcc, afl-gcc+ASan, gcc --coverage
    ▼
Phase 4 — Campagne de fuzzing
    │   afl-fuzz avec dictionnaire, monitoring du tableau de bord
    ▼
Phase 5 — Analyse des crashs
    │   Triage ASan, minimisation, analyse GDB, extraction d'info RE
    ▼
Phase 6 — Rapport de couverture
    │   lcov + genhtml, identification des zones non couvertes
    ▼
Phase 7 — Enrichissement et second cycle
    │   Nouveaux seeds, tokens, relance — itérer jusqu'à convergence
    ▼
Synthèse — Format reconstitué, bugs documentés, questions ciblées
```

Chaque phase alimente la suivante. Les crashs de la phase 5 produisent des connaissances injectées dans le corpus de la phase 7. La couverture de la phase 6 identifie les branches bloquées qui orientent les nouveaux seeds. Le processus est itératif et converge vers une compréhension de plus en plus complète du binaire.

Le fuzzing n'a pas produit une spécification complète du format — ce n'est pas son rôle. Mais en 1 à 2 heures, il a fourni un squelette structurel, trois bugs analysables, et cinq questions précises à résoudre. L'analyste qui ouvre Ghidra après cette campagne sait exactement où regarder et pourquoi. C'est la valeur ajoutée du fuzzing dans une démarche de reverse engineering.

---


⏭️ [🎯 Checkpoint : fuzzer `ch23-fileformat` avec AFL++, trouver au moins 2 crashs et les analyser](/15-fuzzing/checkpoint.md)
