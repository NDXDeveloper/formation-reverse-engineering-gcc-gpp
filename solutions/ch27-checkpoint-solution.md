# Corrigé — Checkpoint Chapitre 27

> ⚠️ **Spoilers** — Ne consultez ce document qu'après avoir tenté le checkpoint par vous-même.

---

## 1. Résumé de la démarche attendue

Le checkpoint demande de traiter `ransomware_O2_strip` (variante strippée) comme un binaire inconnu et de produire quatre livrables. Voici le cheminement complet.

---

## 2. Triage rapide (5–10 min)

### Commandes et résultats clés

```bash
# Identification
$ file ransomware_O2_strip
ransomware_O2_strip: ELF 64-bit LSB pie executable, x86-64, [...] stripped

# Dépendances → libssl + libcrypto = crypto OpenSSL
$ readelf -d ransomware_O2_strip | grep NEEDED
  (NEEDED)  Shared library: [libssl.so.3]
  (NEEDED)  Shared library: [libcrypto.so.3]
  (NEEDED)  Shared library: [libc.so.6]

# Chaînes critiques
$ strings ransomware_O2_strip | grep -E '(tmp|locked|RWARE|REVERSE|EVP_|CHIFFRE)'
/tmp/test
.locked
README_LOCKED.txt  
RWARE27  
REVERSE_ENGINEERING_IS_FUN_2025!  
EVP_EncryptInit_ex  
EVP_EncryptUpdate  
EVP_EncryptFinal_ex  
EVP_aes_256_cbc  
VOS FICHIERS ONT ETE CHIFFRES !  

# Protections
$ checksec --file=ransomware_O2_strip
    Arch:     amd64-64-little
    RELRO:    Partial RELRO
    Stack:    Canary found
    NX:       NX enabled
    PIE:      PIE enabled

# Symboles dynamiques pertinents
$ readelf -s --dyn-syms ransomware_O2_strip | grep FUNC | \
    grep -E '(EVP_|opendir|readdir|unlink|fopen|stat|malloc)'
    [...] EVP_CIPHER_CTX_new
    [...] EVP_CIPHER_CTX_free
    [...] EVP_EncryptInit_ex
    [...] EVP_EncryptUpdate
    [...] EVP_EncryptFinal_ex
    [...] EVP_aes_256_cbc
    [...] opendir
    [...] readdir
    [...] stat
    [...] fopen
    [...] unlink
    [...] malloc

# IV dans .rodata (recherche hexadécimale)
$ objdump -s -j .rodata ransomware_O2_strip | grep -A1 "dead"
 [...] deadbeef cafebabe 13374242 feedface  ................
```

### Hypothèses formulées

| # | Hypothèse | Confiance |  
|---|---|---|  
| H1 | Ransomware ciblant `/tmp/test/` | Forte |  
| H2 | AES-256-CBC via OpenSSL EVP | Forte |  
| H3 | Clé = `REVERSE_ENGINEERING_IS_FUN_2025!` (32 octets) | Moyenne |  
| H4 | IV = `DEADBEEFCAFEBABE13374242FEEDFACE` (16 octets) | Moyenne |  
| H5 | Header `RWARE27` dans les fichiers chiffrés | Moyenne |  
| H6 | Parcours récursif + suppression des originaux | Forte |  
| H7 | Pas de communication réseau | Moyenne |

---

## 3. Analyse statique dans Ghidra (30–45 min)

### Stratégie sur binaire strippé

Le binaire ne contient aucun symbole interne. La méthode consiste à partir des **imports nommés** (fonctions OpenSSL dans `.dynsym`) et à remonter par cross-references (XREF).

### Étapes de reconstruction

**Étape 1 — Trouver `aes256cbc_encrypt`.**
Dans le Symbol Tree, localiser `EVP_EncryptInit_ex` → clic droit → *References → Show References to*. Un unique site d'appel mène à la fonction wrapper de chiffrement. La renommer `aes256cbc_encrypt`.

Le décompilateur montre la séquence `Init → Update → Final` et deux adresses `.rodata` passées en arguments 4 (`rcx` = clé) et 5 (`r8` = IV) de `EVP_EncryptInit_ex`.

**Étape 2 — Confirmer la clé et l'IV.**
Naviguer vers l'adresse du 4ᵉ argument : 32 octets ASCII = `REVERSE_ENGINEERING_IS_FUN_2025!`. Naviguer vers le 5ᵉ : 16 octets = `DE AD BE EF CA FE BA BE 13 37 42 42 FE ED FA CE`. Renommer en `AES_KEY` et `AES_IV`.

**Étape 3 — Trouver `encrypt_file`.**
XREF depuis `aes256cbc_encrypt` → un unique appelant. Le décompilateur montre : `fopen` → `fseek/ftell/fread` → `aes256cbc_encrypt` → `fwrite` (magic + taille + ciphertext) → `unlink`. Renommer `encrypt_file`.

**Étape 4 — Trouver `traverse_directory`.**
XREF depuis `encrypt_file` → appelant avec boucle `opendir/readdir/stat` et appel récursif à elle-même. Renommer `traverse_directory`.

**Étape 5 — Trouver `main`.**
XREF depuis `traverse_directory`. Ou bien : localiser `__libc_start_main` dans les imports → XREF vers `_start` → le premier argument est `main`. Le pseudo-code montre : `stat("/tmp/test")` → `traverse_directory` → `drop_ransom_note`.

### Graphe d'appels final

```
main()
 ├── print_banner()            → printf()
 ├── stat()                    → vérifie /tmp/test
 ├── traverse_directory()
 │     ├── opendir() / readdir() / closedir()
 │     ├── stat()              → fichier ou répertoire ?
 │     ├── should_skip()       → strcmp(".locked"), strcmp("README_LOCKED.txt")
 │     ├── traverse_directory() → récursion
 │     └── encrypt_file()
 │           ├── fopen/fseek/ftell/fread/fclose  → lecture
 │           ├── malloc/free
 │           ├── aes256cbc_encrypt()
 │           │     ├── EVP_CIPHER_CTX_new()
 │           │     ├── EVP_aes_256_cbc()
 │           │     ├── EVP_EncryptInit_ex()      → AES_KEY, AES_IV
 │           │     ├── EVP_EncryptUpdate()
 │           │     ├── EVP_EncryptFinal_ex()
 │           │     └── EVP_CIPHER_CTX_free()
 │           ├── snprintf()    → "%s.locked"
 │           ├── fopen/fwrite/fclose             → écriture .locked
 │           └── unlink()                        → suppression original
 └── drop_ransom_note()
       ├── snprintf() / fopen() / fputs() / fclose()
```

### Format `.locked` (déduit de `encrypt_file`)

Trois `fwrite` successifs dans `encrypt_file` :

1. `fwrite("RWARE27\0", 1, 8, fp)` → magic, 8 octets  
2. `fwrite(&file_size, 8, 1, fp)` → taille originale, uint64_t LE  
3. `fwrite(ciphertext, 1, ciphertext_len, fp)` → payload chiffré

```
Offset   Taille   Type          Contenu
0x00     8        char[8]       "RWARE27\0"
0x08     8        uint64_t LE   Taille originale
0x10     var.     byte[]        AES-256-CBC ciphertext (PKCS#7)
```

---

## 4. Analyse dynamique — GDB (15–20 min)

### Capture de la clé et de l'IV

```gdb
$ gdb -q ./ransomware_O2_strip

(gdb) break EVP_EncryptInit_ex
Make breakpoint pending on future shared library load? (y or [n]) y

(gdb) run

Breakpoint 1, 0x00007ffff7... in EVP_EncryptInit_ex ()

(gdb) x/32xb $rcx
0x...: 0x52 0x45 0x56 0x45 0x52 0x53 0x45 0x5f
0x...: 0x45 0x4e 0x47 0x49 0x4e 0x45 0x45 0x52
0x...: 0x49 0x4e 0x47 0x5f 0x49 0x53 0x5f 0x46
0x...: 0x55 0x4e 0x5f 0x32 0x30 0x32 0x35 0x21

(gdb) x/s $rcx
0x...: "REVERSE_ENGINEERING_IS_FUN_2025!"

(gdb) x/16xb $r8
0x...: 0xde 0xad 0xbe 0xef 0xca 0xfe 0xba 0xbe
0x...: 0x13 0x37 0x42 0x42 0xfe 0xed 0xfa 0xce
```

H3 et H4 passent de « Moyenne » à **« Confirmée définitivement »**.

### Vérification de la rotation de clé

```gdb
(gdb) commands 1
    silent
    printf "Key: "
    x/32xb $rcx
    printf "IV:  "
    x/16xb $r8
    continue
end
(gdb) run
```

Les 6 appels affichent les mêmes valeurs → **aucune rotation de clé**.

### Confirmation absence de réseau

```bash
$ strace -e trace=network ./ransomware_O2_strip 2>&1 | grep -v "^---"
# (aucune sortie) → H7 confirmée
```

---

## 5. Analyse dynamique — Frida (10–15 min)

```javascript
// hook_evp.js
const evpInit = Module.findExportByName("libcrypto.so.3", "EVP_EncryptInit_ex");  
Interceptor.attach(evpInit, {  
    onEnter(args) {
        console.log("=== EVP_EncryptInit_ex ===");
        console.log("Key:"); console.log(hexdump(args[3], { length: 32 }));
        console.log("IV:");  console.log(hexdump(args[4], { length: 16 }));
    }
});
```

```bash
$ frida -f ./ransomware_O2_strip -l hook_evp.js --no-pause
```

Résultat identique à GDB — confirmation croisée par un deuxième outil.

---

## 6. Pattern ImHex (`.hexpat`)

```hexpat
/*!
 * Corrigé Ch27 — Format .locked
 */

#pragma endian little

struct MagicHeader {
    char signature[7] [[comment("Format ID")]];
    u8   null_term    [[comment("\\0")]];
} [[color("FF6B6B"), name("Magic")]];

struct FileMetadata {
    u64 original_size [[comment("Taille fichier original")]];
} [[color("4ECDC4"), name("Metadata")]];

struct EncryptedPayload {
    u8 data[std::mem::size() - 16] [[comment("AES-256-CBC + PKCS#7")]];
} [[color("FFE66D"), name("Ciphertext")]];

struct LockedFile {
    MagicHeader      header;
    FileMetadata     metadata;
    EncryptedPayload payload;
};

LockedFile file @ 0x00;
```

Vérification : charger dans ImHex sur `document.txt.locked`. Le champ `original_size` dans le Data Inspector doit afficher `47` (taille de `document.txt`).

---

## 7. Règles YARA

```yara
rule ransomware_ch27_exact
{
    meta:
        description = "Corrigé Ch27 — détection exacte du sample"
        author      = "Formation RE"

    strings:
        $aes_key = {
            52 45 56 45 52 53 45 5F 45 4E 47 49 4E 45 45 52
            49 4E 47 5F 49 53 5F 46 55 4E 5F 32 30 32 35 21
        }
        $aes_iv = {
            DE AD BE EF CA FE BA BE 13 37 42 42 FE ED FA CE
        }
        $target   = "/tmp/test"       ascii
        $ext      = ".locked"         ascii
        $note     = "README_LOCKED"   ascii
        $magic    = "RWARE27"         ascii

    condition:
        uint32(0) == 0x464C457F
        and $aes_key and $aes_iv
        and 3 of ($target, $ext, $note, $magic)
}

rule ransomware_ch27_generic
{
    meta:
        description = "Corrigé Ch27 — détection générique par comportement"
        author      = "Formation RE"

    strings:
        $evp_init   = "EVP_EncryptInit_ex"  ascii
        $evp_update = "EVP_EncryptUpdate"   ascii
        $evp_final  = "EVP_EncryptFinal_ex" ascii
        $evp_aes    = "EVP_aes_256_cbc"     ascii
        $fs_opendir = "opendir"             ascii
        $fs_readdir = "readdir"             ascii
        $fs_unlink  = "unlink"              ascii
        $locked     = ".locked"             ascii
        $magic      = "RWARE27"             ascii

    condition:
        uint32(0) == 0x464C457F
        and filesize < 500KB
        and 3 of ($evp_init, $evp_update, $evp_final, $evp_aes)
        and 2 of ($fs_opendir, $fs_readdir, $fs_unlink)
        and ($locked or $magic)
}

rule ransomware_ch27_locked_file
{
    meta:
        description = "Corrigé Ch27 — détection des fichiers .locked"
        author      = "Formation RE"

    strings:
        $magic = { 52 57 41 52 45 32 37 00 }

    condition:
        $magic at 0 and filesize > 16 and filesize < 100MB
}
```

### Validation

```bash
$ yara ch27.yar ransomware_O2_strip
ransomware_ch27_exact ransomware_O2_strip  
ransomware_ch27_generic ransomware_O2_strip  

$ yara ch27.yar /usr/bin/openssl
# (aucune sortie → pas de faux positif)

$ yara -r ch27.yar /tmp/test/
ransomware_ch27_locked_file /tmp/test/document.txt.locked  
ransomware_ch27_locked_file /tmp/test/notes.md.locked  
[...]
```

---

## 8. Déchiffreur Python

Le script complet est dans `solutions/ch27-checkpoint-decryptor.py`.

### Validation par hash

```bash
# Avant chiffrement
$ make reset
$ find /tmp/test -type f -exec sha256sum {} \; | sort > /tmp/before.txt

# Chiffrement
$ ./ransomware_O2_strip

# Déchiffrement
$ python3 solutions/ch27-checkpoint-decryptor.py /tmp/test/ --verify

# Comparaison
$ find /tmp/test -type f ! -name "*.locked" ! -name "README_LOCKED.txt" \
    -exec sha256sum {} \; | sort > /tmp/after.txt
$ diff /tmp/before.txt /tmp/after.txt
# (aucune sortie = restauration parfaite)
```

---

## 9. Rapport d'analyse (résumé du corrigé)

Le rapport complet suit le template de la section 27.7. Voici les éléments essentiels attendus :

**Résumé exécutif** — Ransomware ELF Linux, AES-256-CBC, clé hardcodée, fichiers récupérables à 100 %, sophistication faible, pas de réseau, pas de persistance.

**IOC minimum attendus** :

| Type | Valeur |  
|---|---|  
| SHA-256 du binaire | `[hash calculé par l'étudiant]` |  
| Répertoire cible | `/tmp/test` |  
| Extension | `.locked` |  
| Ransom note | `README_LOCKED.txt` |  
| Magic header | `RWARE27\0` |  
| Clé AES-256 | `REVERSE_ENGINEERING_IS_FUN_2025!` |  
| IV AES | `DEADBEEFCAFEBABE13374242FEEDFACE` |  
| API crypto | `EVP_EncryptInit_ex`, `EVP_EncryptUpdate`, `EVP_EncryptFinal_ex` |

**Matrice ATT&CK** — T1204 (User Execution), T1083 (File Discovery), T1486 (Data Encrypted for Impact), T1485 (Data Destruction).

**Recommandations minimum** :
1. Déployer le déchiffreur sur les systèmes affectés.  
2. Scanner l'infrastructure avec les règles YARA.  
3. Identifier le vecteur d'infection initial.  
4. Vérifier/mettre en place des sauvegardes hors-ligne.

---

## 10. Grille de validation remplie

| Point | Statut |  
|---|---|  
| Algorithme identifié (AES-256-CBC) sans exécution | ✅ `strings` + `EVP_aes_256_cbc` dans `.dynsym` |  
| Clé localisée dans `.rodata` | ✅ 32 octets ASCII via Ghidra XREF |  
| IV localisé dans `.rodata` | ✅ 16 octets via Ghidra XREF + `objdump` |  
| Graphe d'appels reconstruit | ✅ 6 fonctions internes renommées |  
| Format `.locked` cartographié | ✅ 3 champs (magic 8B + size 8B + ciphertext) |  
| Clé capturée en dynamique ($rcx) | ✅ GDB breakpoint sur `EVP_EncryptInit_ex` |  
| IV capturé en dynamique ($r8) | ✅ GDB breakpoint sur `EVP_EncryptInit_ex` |  
| Pas de rotation de clé | ✅ 6 appels, mêmes valeurs |  
| Absence réseau confirmée | ✅ `strace -e trace=network` muet |  
| Deux outils dynamiques utilisés | ✅ GDB + Frida (ou strace) |  
| Déchiffreur parse le header | ✅ Magic + uint64_t LE |  
| Déchiffrement AES-256-CBC correct | ✅ |  
| Padding PKCS#7 retiré | ✅ `padding.PKCS7(128)` — 128 bits |  
| Parcours récursif | ✅ `os.walk` |  
| Validation par hash | ✅ `diff` sans sortie |  
| Gestion d'erreurs | ✅ Magic invalide, fichier tronqué, ciphertext non multiple de 16 |  
| Règle YARA détecte le sample | ✅ |  
| Pas de faux positif sur `/usr/bin/openssl` | ✅ |  
| Règle YARA détecte les `.locked` | ✅ |  
| Pattern ImHex charge sans erreur | ✅ |  
| Rapport avec résumé exécutif | ✅ |  
| Hashes SHA-256 inclus | ✅ |  
| Au moins 5 IOC | ✅ 8 listés |  
| Paramètres crypto avec source de confirmation | ✅ |  
| Au moins 3 recommandations | ✅ 4 formulées |  
| Rapport lisible par un tiers | ✅ |

**Niveau atteint : Excellent** (analyse intégrale sur variante strippée, tous livrables produits, matrice ATT&CK incluse).
