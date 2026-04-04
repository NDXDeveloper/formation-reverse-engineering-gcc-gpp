🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 35.4 — Écrire des règles YARA pour détecter des patterns dans une collection de binaires

> 🔧 **Outils couverts** : `yara` (CLI), `yara-python` (bibliothèque Python)  
> 📁 **Fichiers de règles** : `yara-rules/crypto_constants.yar`, `yara-rules/packer_signatures.yar`  
> 📁 **Binaires d'exemple** : toutes les variantes des chapitres 21, 24, 25, 27

---

## YARA dans le workflow RE

Nous avons croisé YARA à deux reprises dans cette formation : au chapitre 6 (section 10), où nous avons appliqué des règles existantes depuis ImHex pour identifier des signatures dans un binaire, et au chapitre 27 (section 4), où nous avons associé des règles YARA au sample de ransomware pour caractériser ses indicateurs de compromission. Dans les deux cas, les règles étaient fournies et appliquées à un binaire isolé.

Cette section change de perspective. On ne consomme plus des règles — on les écrit, et on les exécute à l'échelle d'une collection. L'objectif est de transformer les connaissances accumulées lors des analyses manuelles en règles de détection réutilisables, capables de scanner un répertoire de binaires et de répondre à des questions comme : lesquels contiennent des constantes AES ? Lesquels utilisent notre format CFR ? Lesquels présentent les marqueurs du ransomware du chapitre 27 ?

YARA fonctionne comme un moteur de recherche de patterns sur des fichiers binaires. On décrit ce qu'on cherche (chaînes, séquences hexadécimales, expressions régulières, conditions structurelles), et YARA scanne les fichiers en reportant ceux qui correspondent. C'est l'outil de référence en analyse de malware, mais son utilité ne s'y limite pas — toute détection de pattern dans un corpus de binaires relève de YARA.

---

## Anatomie d'une règle YARA

Une règle YARA se compose de trois blocs : les métadonnées (`meta`), les chaînes à rechercher (`strings`), et la condition de correspondance (`condition`).

```
rule NomDeLaRegle
{
    meta:
        description = "Ce que la règle détecte"
        author      = "votre nom"
        date        = "2025-01-01"

    strings:
        $a = "texte en clair"
        $b = { DE AD BE EF }
        $c = /regex[0-9]+/

    condition:
        $a or ($b and $c)
}
```

Le bloc `meta` est purement informatif — YARA l'ignore lors du scan. Les `strings` définissent les patterns cherchés : chaînes textuelles (entre guillemets), séquences hexadécimales (entre accolades), ou expressions régulières (entre slashes). La `condition` est une expression booléenne qui combine les patterns et détermine si le fichier correspond à la règle.

### Types de patterns

**Chaînes textuelles** — La forme la plus simple. Par défaut, la recherche est sensible à la casse. Le modificateur `nocase` rend la recherche insensible, et `wide` cherche la version UTF-16 (un octet nul entre chaque caractère, fréquent dans les binaires Windows).

```
strings:
    $banner = "KeyGenMe v1.0"
    $magic  = "CFRM"
    $crypto = "CRYPT24" nocase
```

**Séquences hexadécimales** — Permettent de chercher des octets arbitraires, y compris des opcodes ou des constantes numériques. Les wildcards `??` remplacent un octet quelconque, et les alternatives `(AA|BB)` permettent de matcher plusieurs variantes :

```
strings:
    // HASH_SEED = 0x5A3C6E2D en little-endian
    $hash_seed = { 2D 6E 3C 5A }

    // Prologue GCC -O0 : push rbp; mov rbp, rsp
    $prologue = { 55 48 89 E5 }

    // call strcmp@plt — l'offset varie selon le binaire
    $call_strcmp = { E8 ?? ?? ?? ?? }     // E8 + 4 octets d'offset relatif

    // XOR key du ch25 : 0x5A 0x3C 0x96 0xF1
    $xor_key = { 5A 3C 96 F1 }
```

**Expressions régulières** — Pour les patterns textuels plus flexibles :

```
strings:
    // Chaînes de type "version X.Y"
    $version = /version\s+[0-9]+\.[0-9]+/

    // Passphrase partielle du ch24 (construite par morceaux)
    $passphrase_part = /r3vers3_m3/
```

### Conditions courantes

La condition est le cœur logique de la règle. Voici les constructions les plus utilisées en RE.

**Présence simple** — Le fichier contient au moins une occurrence du pattern :

```
condition:
    $magic
```

**Combinaisons logiques** — Exiger plusieurs patterns simultanément :

```
condition:
    $magic and $version and ($data_a or $data_b)
```

**Comptage** — Exiger un nombre minimum d'occurrences ou un nombre minimum de patterns distincts parmi un ensemble :

```
condition:
    #hash_seed > 0 and #hash_xor > 0    // # = nombre d'occurrences

condition:
    2 of ($crypto_*)    // au moins 2 patterns parmi ceux préfixés crypto_
```

**Position dans le fichier** — Restreindre la recherche aux premiers octets (idéal pour les magic bytes de headers) :

```
condition:
    $magic at 0    // le magic doit être au tout début du fichier
```

**Taille du fichier** — Filtrer par taille pour éviter les faux positifs :

```
condition:
    filesize < 5MB and $marker
```

**Module ELF** — YARA dispose d'un module `elf` qui expose les métadonnées structurelles du binaire sans avoir à les chercher manuellement :

```
import "elf"

rule ELF_x86_64_PIE
{
    condition:
        elf.machine == elf.EM_X86_64 and
        elf.type == elf.ET_DYN    // PIE = ET_DYN
}
```

---

## Règles pour nos binaires d'entraînement

### Détecter le keygenme (chapitre 21)

Les cinq variantes de `keygenme` partagent des marqueurs communs, même dans les versions strippées. Les chaînes visibles via `strings` (bannière, messages d'erreur) sont le premier vecteur de détection. Les constantes numériques de l'algorithme de hachage constituent un second vecteur, indépendant de la présence de symboles.

```
rule KeyGenMe_Ch21
{
    meta:
        description = "Detects keygenme training binary (Chapter 21)"
        chapter     = "21"
        target      = "keygenme_O0 through keygenme_O2_strip"

    strings:
        // Chaînes caractéristiques (présentes même en strippé)
        $banner     = "KeyGenMe v1.0"
        $prompt_key = "XXXX-XXXX-XXXX-XXXX"
        $msg_valid  = "Valid license"
        $msg_fail   = "Invalid license"

        // Constantes de l'algorithme (little-endian dans .text ou .rodata)
        $hash_seed  = { 2D 6E 3C 5A }    // 0x5A3C6E2D
        $hash_xor   = { EF BE AD DE }    // 0xDEADBEEF
        $hash_mul   = { 3F 00 01 00 }    // 0x0001003F — attention : imm32

        // Pattern de dérivation : XOR avec 0xA5A5 et 0x5A5A
        $derive_a   = { A5 A5 }
        $derive_b   = { 5A 5A }

    condition:
        // Au moins 2 chaînes + au moins 2 constantes
        (2 of ($banner, $prompt_key, $msg_valid, $msg_fail)) and
        (2 of ($hash_seed, $hash_xor, $hash_mul))
}
```

Cette règle détecte toutes les variantes : les chaînes survivent au stripping (elles sont dans `.rodata`, pas dans `.symtab`), et les constantes numériques sont des opérandes immédiates dans le code machine, pas des symboles. La condition exige au moins deux chaînes *et* deux constantes, ce qui réduit considérablement le risque de faux positifs — la probabilité qu'un binaire sans rapport contienne à la fois `"KeyGenMe v1.0"` et la séquence `{ 2D 6E 3C 5A }` est infime.

> 💡 **Note sur `$hash_mul`** : la constante `0x1003F` tient sur 17 bits, mais GCC l'encodera en opérande immédiate 32 bits dans l'instruction `imul`. La séquence `{ 3F 00 01 00 }` est sa représentation little-endian sur 4 octets. En `-O2`, le compilateur peut remplacer la multiplication par une séquence `lea` + `shl` + `add`, auquel cas la constante brute disparaît du code — la règle continue de fonctionner grâce aux autres patterns.

### Détecter les constantes crypto (chapitre 24)

Cette règle cible les binaires qui embarquent des constantes cryptographiques connues. Elle est conçue pour fonctionner au-delà du seul `crypto.c` du chapitre 24 — elle détectera tout binaire contenant une S-box AES ou des constantes SHA-256.

```
rule Crypto_Constants_Embedded
{
    meta:
        description = "Binary contains well-known cryptographic constants"
        chapter     = "24, 27"

    strings:
        // AES S-box (16 premiers octets de la première ligne)
        $aes_sbox = {
            63 7C 77 7B F2 6B 6F C5
            30 01 67 2B FE D7 AB 76
        }

        // SHA-256 : valeurs initiales H0..H3 (big-endian, telles qu'en mémoire
        // si la lib les stocke en BE ; sinon chercher aussi en LE)
        $sha256_h0_be = { 6A 09 E6 67 }
        $sha256_h1_be = { BB 67 AE 85 }
        $sha256_h0_le = { 67 E6 09 6A }
        $sha256_h1_le = { 85 AE 67 BB }

        // SHA-256 K[0] et K[1] (constantes du round)
        $sha256_k0 = { 42 8A 2F 98 }
        $sha256_k1 = { 71 37 44 91 }

        // Masque XOR du ch24 (8 premiers octets reconnaissables)
        $ch24_mask = { DE AD BE EF CA FE BA BE }

        // Magic du format CRYPT24
        $crypt24_magic = "CRYPT24"

    condition:
        // S-box AES OU au moins 2 constantes SHA-256 OU marqueurs ch24
        $aes_sbox or
        (2 of ($sha256_h0_be, $sha256_h1_be, $sha256_h0_le, $sha256_h1_le,
               $sha256_k0, $sha256_k1)) or
        ($ch24_mask and $crypt24_magic)
}
```

Appliquée à nos binaires : `crypto_O0` et ses variantes déclencheront la règle via les constantes SHA-256 (liées statiquement ou via libcrypto) et le masque `$ch24_mask`. `crypto_static` la déclenchera aussi via `$aes_sbox`, puisque la S-box AES complète est embarquée dans le binaire statique. Les binaires des chapitres 21 et 25 ne correspondront pas — `0xDEADBEEF` seul n'est pas suffisant puisque la condition exige aussi `CRYPT24`.

### Détecter le format CFR (chapitre 25)

Pour les fichiers d'archive (pas les binaires), on cible la structure du header CFR :

```
rule CFR_Archive_Format
{
    meta:
        description = "Custom Format Records archive (Chapter 25)"
        chapter     = "25"

    strings:
        $hdr_magic = "CFRM"
        $ftr_magic = "CRFE"

    condition:
        // Magic en début de fichier, version valide aux octets 4-5
        $hdr_magic at 0 and
        (uint16(4) == 0x0002) and    // version == 2
        filesize > 32                 // au moins un header complet
}
```

La fonction `uint16(offset)` lit un entier 16 bits little-endian à l'offset donné dans le fichier. C'est l'un des mécanismes les plus puissants de YARA pour valider la structure d'un format : au lieu de chercher une séquence d'octets, on interprète les données comme des champs typés. Ici, on vérifie que les octets 4-5 correspondent à la version `0x0002`, ce qui élimine les fichiers qui contiendraient la chaîne `"CFRM"` par coïncidence.

### Détecter le ransomware pédagogique (chapitre 27)

Les marqueurs du ransomware combinent les constantes AES, la passphrase construite par morceaux, et des chaînes comportementales :

```
rule Ransomware_Ch27_Sample
{
    meta:
        description = "Pedagogical ransomware sample from Chapter 27"
        chapter     = "27"
        severity    = "training_only"

    strings:
        // Fragments de la passphrase (construite dynamiquement)
        $pp_part1 = "r3vers3_"
        $pp_part2 = "m3_1f_"
        $pp_part3 = "y0u_c4n!"

        // Masque XOR appliqué après SHA-256
        $key_mask = {
            DE AD BE EF CA FE BA BE
            13 37 42 42 FE ED FA CE
        }

        // Magic du format de sortie
        $out_magic = "CRYPT24"

        // Chaînes comportementales
        $encrypt_msg = "Encrypted file written to"
        $derived_key = "Derived key"

    condition:
        elf.machine == elf.EM_X86_64 and
        (2 of ($pp_part1, $pp_part2, $pp_part3)) and
        $key_mask and
        $out_magic
}
```

La détection de la passphrase est intéressante : bien que `build_passphrase()` dans `crypto.c` construise la chaîne caractère par caractère pour échapper à `strings`, les fragments intermédiaires (`"r3vers3_"`, `"m3_1f_"`, `"y0u_c4n!"`) sont des tableaux `const char[]` initialisés dans le code. Le compilateur les place dans `.rodata` comme des séquences d'octets contiguës — et YARA les retrouve sans difficulté. C'est une leçon importante : l'obfuscation de chaînes par construction dynamique ne résiste pas à une analyse statique des données initialisées. Pour une protection réelle, il faudrait chiffrer les fragments eux-mêmes.

### Détecter les binaires packés avec UPX (chapitre 29)

```
import "elf"

rule UPX_Packed_ELF
{
    meta:
        description = "ELF binary packed with UPX"
        chapter     = "29"

    strings:
        $upx_magic  = "UPX!"
        $upx_header = { 55 50 58 21 }    // "UPX!" en hex
        $upx_sect1  = "UPX0"
        $upx_sect2  = "UPX1"

        // Signature de l'en-tête UPX (version info)
        $upx_ver    = /UPX\s+[0-9]+\.[0-9]+/

    condition:
        elf.type == elf.ET_EXEC and
        ($upx_magic or $upx_header) and
        ($upx_sect1 or $upx_sect2)
}
```

---

## Exécuter les règles en ligne de commande

### Installation

```bash
# Debian / Ubuntu
sudo apt install yara

# Vérifier
yara --version
```

### Scanner un fichier unique

```bash
yara crypto_constants.yar crypto_O0
```

Si le fichier correspond, YARA affiche le nom de la règle suivi du chemin :

```
Crypto_Constants_Embedded crypto_O0
```

### Scanner un répertoire récursivement

```bash
yara -r crypto_constants.yar binaries/
```

L'option `-r` parcourt les sous-répertoires. Sur notre dépôt, cette commande scannerait tous les binaires d'entraînement et rapporterait ceux qui contiennent des constantes crypto — les variantes de `crypto_O0` et le ransomware du chapitre 27.

### Combiner plusieurs fichiers de règles

```bash
yara -r yara-rules/crypto_constants.yar \
        yara-rules/packer_signatures.yar \
        binaries/
```

Ou charger toutes les règles d'un répertoire :

```bash
# Compiler les règles en un fichier binaire pour des scans plus rapides
yarac yara-rules/*.yar compiled_rules.yarc

# Scanner avec les règles compilées
yara -r compiled_rules.yarc binaries/
```

La compilation préalable avec `yarac` accélère significativement le scan quand on a beaucoup de règles ou beaucoup de fichiers — le parsing des règles n'est fait qu'une fois.

### Options utiles

| Option | Effet |  
|---|---|  
| `-r` | Scan récursif des sous-répertoires |  
| `-s` | Afficher les chaînes matchées et leur offset |  
| `-m` | Afficher les métadonnées de la règle |  
| `-c` | Compter les correspondances au lieu de les afficher |  
| `-n` | Afficher les fichiers qui ne correspondent à aucune règle |  
| `-t tag` | Ne scanner que les règles portant ce tag |  
| `-p N` | Nombre de threads de scan |

L'option `-s` est particulièrement utile lors du développement de règles — elle montre exactement quels patterns ont matché et à quel offset, ce qui permet de vérifier que la règle détecte bien ce qu'on pense :

```bash
yara -s crypto_constants.yar crypto_O0
```

```
Crypto_Constants_Embedded crypto_O0
0x2040:$ch24_mask: DE AD BE EF CA FE BA BE
0x1a3b:$sha256_k0: 42 8A 2F 98
0x3012:$crypt24_magic: CRYPT24
```

---

## Intégration Python avec `yara-python`

Pour intégrer le scan YARA dans les scripts d'automatisation des sections précédentes, la bibliothèque `yara-python` offre une API native :

```bash
pip install yara-python
```

### Compiler et scanner

```python
import yara

# Compiler les règles depuis un fichier
rules = yara.compile(filepath="yara-rules/crypto_constants.yar")

# Scanner un binaire
matches = rules.match("crypto_O0")  
for match in matches:  
    print(f"Règle : {match.rule}")
    print(f"Tags  : {match.tags}")
    print(f"Meta  : {match.meta}")
    for s in match.strings:
        for instance in s.instances:
            print(f"  Offset 0x{instance.offset:x} : "
                  f"{s.identifier} = {instance.matched_data.hex()}")
```

### Compiler depuis une chaîne

Pour les règles générées dynamiquement — par exemple, à partir de constantes extraites lors d'une analyse précédente — on peut compiler directement depuis une chaîne Python :

```python
import yara

# Générer une règle à la volée à partir de constantes découvertes
hash_seed = 0x5A3C6E2D  
hash_xor  = 0xDEADBEEF  

rule_source = f'''  
rule Dynamic_KeygenMe_Detection  
{{
    strings:
        $seed = {{ {hash_seed & 0xFF:02X} {(hash_seed >> 8) & 0xFF:02X} \
{(hash_seed >> 16) & 0xFF:02X} {(hash_seed >> 24) & 0xFF:02X} }}
        $xor  = {{ {hash_xor & 0xFF:02X} {(hash_xor >> 8) & 0xFF:02X} \
{(hash_xor >> 16) & 0xFF:02X} {(hash_xor >> 24) & 0xFF:02X} }}
    condition:
        $seed and $xor
}}
'''

rules = yara.compile(source=rule_source)  
matches = rules.match("keygenme_O2_strip")  
print(f"Correspondances : {[m.rule for m in matches]}")  
```

Ce pattern de génération dynamique de règles est puissant : un script d'analyse peut extraire des constantes d'un premier binaire (via `pyelftools` ou `lief`, comme en section 35.1), puis générer automatiquement une règle YARA pour scanner d'autres binaires à la recherche des mêmes constantes.

### Scanner un répertoire complet

```python
import yara  
from pathlib import Path  
import json  

def scan_directory(rules_path, target_dir):
    """Scanne tous les fichiers d'un répertoire et retourne un rapport."""
    rules = yara.compile(filepath=rules_path)
    results = []

    for filepath in sorted(Path(target_dir).rglob("*")):
        if not filepath.is_file():
            continue
        try:
            matches = rules.match(str(filepath))
            if matches:
                results.append({
                    "file": str(filepath),
                    "matches": [
                        {
                            "rule": m.rule,
                            "meta": m.meta,
                            "strings_count": sum(
                                len(s.instances) for s in m.strings
                            ),
                        }
                        for m in matches
                    ],
                })
        except yara.Error:
            continue  # Fichier illisible ou trop gros

    return results

report = scan_directory("yara-rules/crypto_constants.yar", "binaries/")  
print(json.dumps(report, indent=2))  
```

---

## Intégration dans le pipeline batch

Le scan YARA s'insère naturellement dans le pipeline construit à la section 35.2. On ajoute une phase YARA entre l'analyse Ghidra et la consolidation :

```bash
# Phase supplémentaire dans batch_ghidra.sh

echo "=== Phase YARA : scan de patterns ==="  
python3 scripts/yara_scan.py \  
    --rules yara-rules/ \
    --target binaries/ \
    --output "$OUTPUT_DIR/yara_results.json"
```

Le script `yara_scan.py` combine toutes les règles du répertoire `yara-rules/` et produit un JSON fusionnable avec les résultats Ghidra dans `merge_reports.py`. Le rapport final contiendra, pour chaque binaire, à la fois les informations structurelles (fonctions, symboles) et les détections de patterns (constantes crypto, signatures de packers, marqueurs de format).

```python
#!/usr/bin/env python3
# yara_scan.py — Scan YARA batch avec sortie JSON
#
# Usage : python3 yara_scan.py --rules <dir> --target <dir> --output <file>

import yara  
import json  
import argparse  
from pathlib import Path  

def compile_all_rules(rules_dir):
    """Compile tous les .yar d'un répertoire en un seul objet Rules."""
    rule_files = {}
    for i, path in enumerate(sorted(Path(rules_dir).glob("*.yar"))):
        rule_files[f"ns_{i}"] = str(path)
    return yara.compile(filepaths=rule_files)

def scan_all(rules, target_dir):
    results = {}
    for filepath in sorted(Path(target_dir).rglob("*")):
        if not filepath.is_file() or filepath.stat().st_size == 0:
            continue
        try:
            matches = rules.match(str(filepath))
        except yara.Error:
            continue
        if matches:
            results[str(filepath)] = [m.rule for m in matches]
    return results

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rules", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    rules = compile_all_rules(args.rules)
    results = scan_all(rules, args.target)

    with open(args.output, "w") as f:
        json.dump(results, f, indent=2)

    total = sum(len(v) for v in results.values())
    print(f"[+] {total} detections across {len(results)} files -> {args.output}")
```

---

## Bonnes pratiques pour l'écriture de règles

**Nommer les règles de manière descriptive.** Le nom de la règle est ce qui apparaît dans les rapports. `Crypto_Constants_Embedded` est exploitable ; `rule1` ne l'est pas. Adopter une convention de nommage cohérente — par exemple `Categorie_Cible_Detail` — facilite le filtrage et le tri des résultats.

**Documenter dans `meta`.** Les champs `description`, `author`, `date`, `reference` (URL vers l'analyse source), et `severity` permettent de comprendre une règle des mois après son écriture. En environnement professionnel, les règles YARA sans métadonnées sont inutilisables par les collègues.

**Combiner plusieurs vecteurs de détection.** Une règle qui repose sur une seule chaîne produit des faux positifs. Exiger la conjonction de plusieurs indicateurs indépendants — une chaîne textuelle *et* une constante hexadécimale *et* une propriété structurelle — réduit drastiquement le bruit. La règle `Ransomware_Ch27_Sample` illustre ce principe : elle exige des fragments de passphrase, le masque XOR, le magic CRYPT24, *et* un binaire ELF x86-64.

**Tester sur des positifs *et* des négatifs.** Une règle n'est fiable que si elle détecte bien les cibles (vrais positifs) *et* ne détecte pas les non-cibles (absence de faux positifs). Sur notre dépôt, scanner `binaries/` avec chaque règle permet de vérifier les deux : la règle `KeyGenMe_Ch21` doit matcher les cinq variantes du keygenme et aucun autre binaire.

**Utiliser les wildcards avec parcimonie.** Un pattern comme `{ E8 ?? ?? ?? ?? }` (instruction `call` avec n'importe quel offset) matchera des centaines de fois dans n'importe quel binaire — il n'est utile qu'en combinaison avec d'autres patterns plus discriminants. Chaque wildcard élargit le champ de correspondance de manière exponentielle.

**Versionner les règles.** Les fichiers `.yar` sont du texte pur — ils ont leur place dans le même dépôt Git que les binaires et les scripts d'analyse. Chaque modification d'une règle doit être tracée, car un changement de condition peut transformer un scan propre en avalanche de faux positifs (ou, pire, en silence sur de vrais positifs).

---

## Ce que YARA ne fait pas

YARA est un moteur de *recherche de patterns* — pas un désassembleur, pas un analyseur sémantique. Il ne comprend pas la structure des instructions x86 : il cherche des séquences d'octets, point. Cela a des conséquences pratiques.

Un pattern hexadécimal peut matcher dans `.text` (comme opcode ou opérande), dans `.rodata` (comme donnée), dans `.debug_info` (comme information DWARF), ou même dans le padding entre sections. YARA ne fait pas la différence. Si le même motif `{ DE AD BE EF }` apparaît comme constante dans le code *et* comme artefact dans les sections de debug, YARA reportera les deux. C'est au script appelant — ou à l'analyste — de croiser l'offset de la correspondance avec la carte des sections (via `pyelftools` ou `lief`) pour déterminer dans quel contexte le pattern a été trouvé.

De même, YARA ne suit pas le flux de contrôle. Il ne peut pas exprimer une condition comme « la constante X est utilisée comme argument d'un appel à `EVP_EncryptInit_ex` ». Pour ce type de détection sémantique, il faut combiner YARA (détection rapide des candidats) avec une analyse plus fine (Ghidra headless, comme en section 35.2) sur les binaires présélectionnés. C'est cette combinaison en entonnoir — YARA comme filtre large et rapide, Ghidra comme analyseur ciblé et profond — qui constitue un pipeline d'analyse scalable.

---


⏭️ [Intégration dans un pipeline CI/CD pour audit de régression binaire](/35-automatisation-scripting/05-pipeline-ci-cd.md)
