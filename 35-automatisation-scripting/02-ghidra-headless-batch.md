🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 35.2 — Automatiser Ghidra en headless mode (analyse batch de N binaires)

> 🔧 **Outil couvert** : `analyzeHeadless` (Ghidra ≥ 10.x)  
> 🐍 **Langage de script** : Ghidra Python (Jython 2.7) et Java — les deux sont supportés en headless  
> 📁 **Binaires d'exemple** : `keygenme_O0`, `keygenme_O2`, `keygenme_strip`, `crypto_O0`, `fileformat_O0`

---

## Pourquoi le mode headless ?

Au chapitre 8, nous avons utilisé Ghidra via son interface graphique — le CodeBrowser — pour importer un binaire, lancer l'auto-analyse, naviguer dans le désassemblage et le décompileur, renommer des fonctions, reconstruire des types. Ce workflow est efficace sur un binaire isolé, mais il devient impraticable dès que le volume augmente.

Quelques situations concrètes où l'interface graphique ne suffit plus : analyser les cinq variantes de `keygenme` et produire un rapport comparatif ; scanner tous les binaires d'un répertoire pour lister les fonctions qui appellent `strcmp` ; extraire le pseudo-code décompilé de chaque fonction d'un firmware ; vérifier qu'un build de production ne contient pas de symboles de debug oubliés.

Ghidra expose un mode d'exécution sans interface graphique appelé **headless mode**, accessible via le script `analyzeHeadless`. Ce mode permet de créer un projet Ghidra, d'y importer un ou plusieurs binaires, de lancer l'auto-analyse complète (la même que celle du GUI), et d'exécuter des scripts — le tout depuis la ligne de commande, sans ouvrir aucune fenêtre. C'est la clé pour intégrer Ghidra dans des pipelines automatisés.

---

## Localiser et lancer `analyzeHeadless`

Le script se trouve dans le répertoire `support/` de l'installation Ghidra :

```bash
# Localisation typique
ls $GHIDRA_HOME/support/analyzeHeadless

# Sur une installation par défaut
/opt/ghidra/support/analyzeHeadless
```

> 💡 **Astuce** : créez un alias ou un lien symbolique pour simplifier l'invocation :  
> ```bash  
> echo 'alias ghidra-headless="/opt/ghidra/support/analyzeHeadless"' >> ~/.bashrc  
> source ~/.bashrc  
> ```  
>  
> Tous les exemples de cette section utilisent cet alias `ghidra-headless`.

La syntaxe générale est :

```
ghidra-headless <project_dir> <project_name> [options]
```

Les deux premiers arguments sont obligatoires : le répertoire où stocker le projet Ghidra (créé automatiquement s'il n'existe pas), et le nom du projet. Le reste est constitué d'options qui contrôlent l'import, l'analyse et l'exécution de scripts.

---

## Importer et analyser un binaire

La commande minimale pour importer un binaire et lancer l'auto-analyse :

```bash
ghidra-headless /tmp/ghidra_projects MonProjet \
    -import keygenme_O0
```

Ghidra va :
1. Créer le projet `MonProjet` dans `/tmp/ghidra_projects/` (s'il n'existe pas)  
2. Importer `keygenme_O0` en détectant automatiquement le format (ELF x86-64)  
3. Lancer l'auto-analyse complète (désassemblage, détection de fonctions, propagation de types, analyse des références croisées)  
4. Sauvegarder le résultat dans le projet  
5. Quitter

La sortie console est verbeuse — Ghidra journalise chaque étape de l'analyse. Sur un binaire simple comme `keygenme_O0`, le processus prend quelques secondes. Sur un binaire de plusieurs mégaoctets (comme `crypto_static` linké statiquement), cela peut prendre plusieurs minutes.

### Options d'import utiles

| Option | Effet |  
|---|---|  
| `-import <fichier>` | Importe un fichier dans le projet |  
| `-overwrite` | Écrase un binaire déjà présent dans le projet |  
| `-recursive` | Importe tous les fichiers d'un répertoire |  
| `-readOnly` | Ouvre un binaire existant sans le modifier |  
| `-noanalysis` | Importe sans lancer l'auto-analyse |  
| `-analysisTimeoutPerFile <sec>` | Limite le temps d'analyse par fichier |  
| `-max-cpu <n>` | Nombre de threads pour l'analyse |  
| `-loader ElfLoader` | Force le loader ELF (rarement nécessaire) |

Pour importer tous les binaires du chapitre 21 en une seule commande :

```bash
ghidra-headless /tmp/ghidra_projects BatchCh21 \
    -import binaries/ch21-keygenme/keygenme_O0 \
            binaries/ch21-keygenme/keygenme_O2 \
            binaries/ch21-keygenme/keygenme_O3 \
            binaries/ch21-keygenme/keygenme_strip \
            binaries/ch21-keygenme/keygenme_O2_strip \
    -overwrite \
    -analysisTimeoutPerFile 120
```

Ou, plus concis, en utilisant `-recursive` sur le répertoire (Ghidra filtrera automatiquement les fichiers qu'il sait parser) :

```bash
ghidra-headless /tmp/ghidra_projects BatchCh21 \
    -import binaries/ch21-keygenme/ \
    -recursive \
    -overwrite
```

---

## Exécuter un script post-analyse

Le mode headless prend tout son intérêt quand on y associe un script qui s'exécute *après* l'auto-analyse, dans le contexte du binaire importé. Le script a accès à l'ensemble de l'API Ghidra — le même API que celui disponible dans la console Script Manager du GUI.

### Un premier script : lister les fonctions

Créons un script Python minimal qui liste toutes les fonctions détectées par Ghidra et les écrit dans un fichier :

```python
# list_functions.py — Script Ghidra headless
# Exécution : ghidra-headless ... -postScript list_functions.py

import json  
import os  

program = currentProgram  
name = program.getName()  
listing = program.getListing()  
func_mgr = program.getFunctionManager()  

functions = []  
func = func_mgr.getFunctionAt(program.getMinAddress())  
func_iter = func_mgr.getFunctions(True)  # True = forward  

for func in func_iter:
    functions.append({
        "name": func.getName(),
        "entry": "0x" + func.getEntryPoint().toString(),
        "size": func.getBody().getNumAddresses(),
        "is_thunk": func.isThunk(),
    })

# Écrire le résultat en JSON
output_dir = os.environ.get("GHIDRA_OUTPUT", "/tmp")  
output_path = os.path.join(output_dir, name + "_functions.json")  

with open(output_path, "w") as f:
    json.dump({"binary": name, "count": len(functions), "functions": functions},
              f, indent=2)

print("[+] {} functions written to {}".format(len(functions), output_path))
```

Pour l'exécuter après l'import et l'analyse :

```bash
export GHIDRA_OUTPUT=/tmp/results  
mkdir -p $GHIDRA_OUTPUT  

ghidra-headless /tmp/ghidra_projects BatchCh21 \
    -import keygenme_O0 \
    -overwrite \
    -postScript list_functions.py
```

L'option `-postScript` indique à Ghidra d'exécuter le script *après* l'auto-analyse. Il existe aussi `-preScript` (avant l'analyse, utile pour configurer des options d'analyse) et `-scriptPath` (pour spécifier un répertoire contenant les scripts).

> ⚠️ **Jython, pas CPython** : les scripts Python headless s'exécutent dans l'interpréteur Jython 2.7 embarqué dans Ghidra. Cela signifie que la syntaxe est Python 2, que les modules C natifs (`numpy`, `lief`) ne sont pas disponibles, et que les f-strings ne fonctionnent pas. On utilise `format()` ou `%` pour le formatage. L'approche habituelle consiste à faire produire au script Ghidra un fichier JSON ou CSV, puis à post-traiter ce fichier avec un script CPython standard qui a accès à tout l'écosystème.

### Passer des arguments au script

On peut transmettre des arguments au script via la ligne de commande. Ils sont accessibles dans le script via la variable globale `args` (une liste de chaînes) :

```bash
ghidra-headless /tmp/ghidra_projects MonProjet \
    -process keygenme_O0 \
    -postScript find_callers.py "strcmp" \
    -noanalysis  # le binaire est déjà analysé
```

```python
# find_callers.py — Trouve toutes les fonctions qui appellent un symbole donné

target_name = args[0] if args else "strcmp"

func_mgr = currentProgram.getFunctionManager()  
symbol_table = currentProgram.getSymbolTable()  
ref_mgr = currentProgram.getReferenceManager()  

# Trouver le symbole cible
symbols = symbol_table.getGlobalSymbols(target_name)  
if not symbols:  
    print("[-] Symbol '{}' not found".format(target_name))
else:
    for sym in symbols:
        addr = sym.getAddress()
        refs = ref_mgr.getReferencesTo(addr)
        callers = set()
        for ref in refs:
            caller_func = func_mgr.getFunctionContaining(ref.getFromAddress())
            if caller_func:
                callers.add(caller_func.getName())
        print("[+] Functions calling '{}':".format(target_name))
        for c in sorted(callers):
            print("    - {}".format(c))
```

Sur `keygenme_O0`, ce script affichera que `check_license` appelle `strcmp` — l'information clé que l'apprenant cherche à localiser au chapitre 21. Sur la variante strippée, la fonction s'appellera `FUN_00401xxx` (nom auto-généré par Ghidra), mais la référence à `strcmp@plt` sera toujours détectée puisque c'est un import dynamique.

---

## Traiter un binaire déjà importé : `-process`

Quand un binaire est déjà dans le projet Ghidra (importé et analysé lors d'une exécution précédente), on n'a pas besoin de le réimporter. L'option `-process` ouvre un programme existant du projet :

```bash
# Exécuter un script sur un binaire déjà analysé
ghidra-headless /tmp/ghidra_projects BatchCh21 \
    -process keygenme_O0 \
    -postScript list_functions.py \
    -noanalysis
```

L'option `-noanalysis` évite de relancer l'auto-analyse (inutile si le binaire est déjà analysé). Cela accélère considérablement l'exécution quand on itère sur les scripts.

Pour traiter *tous* les binaires d'un projet, on omet le nom après `-process` :

```bash
# Exécuter le script sur chaque binaire du projet
ghidra-headless /tmp/ghidra_projects BatchCh21 \
    -process \
    -postScript list_functions.py \
    -noanalysis
```

Ghidra exécutera `list_functions.py` une fois par binaire du projet. Chaque exécution aura accès à la variable `currentProgram` correspondant au binaire en cours de traitement. C'est le mécanisme fondamental du batch processing.

---

## Extraire le pseudo-code décompilé

L'une des fonctionnalités les plus puissantes de Ghidra en headless est l'accès au décompileur. Le script suivant extrait le pseudo-code C de toutes les fonctions (ou d'une fonction ciblée) et le sauvegarde dans un fichier :

```python
# decompile_all.py — Extrait le pseudo-code de toutes les fonctions
# Usage : ghidra-headless ... -postScript decompile_all.py [nom_fonction]

from ghidra.app.decompiler import DecompInterface  
import os  

# Initialiser le décompileur
decomp = DecompInterface()  
decomp.openProgram(currentProgram)  

func_mgr = currentProgram.getFunctionManager()  
prog_name = currentProgram.getName()  

# Si un argument est fourni, ne décompiler que cette fonction
target = args[0] if args else None

output_dir = os.environ.get("GHIDRA_OUTPUT", "/tmp")  
output_path = os.path.join(output_dir, prog_name + "_decompiled.c")  

count = 0  
with open(output_path, "w") as out:  
    out.write("/* Decompiled from: {} */\n\n".format(prog_name))

    for func in func_mgr.getFunctions(True):
        if target and func.getName() != target:
            continue

        result = decomp.decompileFunction(func, 30, monitor)
        if result and result.getDecompiledFunction():
            code = result.getDecompiledFunction().getC()
            out.write("/* --- {} @ {} --- */\n".format(
                func.getName(), func.getEntryPoint()))
            out.write(code)
            out.write("\n\n")
            count += 1

decomp.dispose()  
print("[+] Decompiled {} functions -> {}".format(count, output_path))  
```

Lancé sur `keygenme_O0` :

```bash
ghidra-headless /tmp/ghidra_projects BatchCh21 \
    -process keygenme_O0 \
    -postScript decompile_all.py "check_license" \
    -noanalysis
```

Le fichier produit contiendra le pseudo-code C de `check_license`, incluant les appels à `compute_hash`, `derive_key`, `format_key` et `strcmp`. C'est le même pseudo-code que celui visible dans le panneau Decompiler du GUI — mais ici, il est généré automatiquement et redirigé dans un fichier exploitable par d'autres scripts.

Lancé ensuite sur `keygenme_O2`, on observe les effets de l'optimisation : certaines fonctions sont inlinées, les variables temporaires ont disparu, et la structure du code est plus condensée. Comparer les deux fichiers décompilés programmatiquement permet de documenter précisément l'impact des optimisations — ce qu'on a fait manuellement au chapitre 16.

---

## Détecter les constantes crypto dans un batch

En combinant l'API Ghidra avec des patterns connus, on peut scanner automatiquement un ensemble de binaires à la recherche de constantes cryptographiques. Le script suivant cherche les constantes magiques AES, SHA-256 et le masque XOR du chapitre 24 :

```python
# scan_crypto_constants.py — Détection de constantes crypto
# Recherche dans .rodata et .data

import json  
import os  

CRYPTO_SIGS = {
    "AES_SBOX_FIRST_ROW": "637c777bf26b6fc53001672bfed7ab76",
    "SHA256_INIT_H0":     "6a09e667",
    "SHA256_INIT_H1":     "bb67ae85",
    "SHA256_K_FIRST":     "428a2f98",
    "DEADBEEF_BE":        "deadbeef",       # big-endian (tableaux d'octets, ex: KEY_MASK ch24)
    "DEADBEEF_LE":        "efbeadde",       # little-endian (opérande imm32 x86, ex: HASH_XOR ch21)
    "CAFEBABE_BE":        "cafebabe",
    "CH24_KEY_MASK_HEAD": "deadbeefcafebabe",
}

prog_name = currentProgram.getName()  
memory = currentProgram.getMemory()  
results = []  

for label, hex_pattern in CRYPTO_SIGS.items():
    pattern_bytes = hex_pattern.decode("hex")
    # Chercher dans toute la mémoire du programme
    addr = memory.findBytes(
        currentProgram.getMinAddress(),
        pattern_bytes,
        None,  # mask (None = exact match)
        True,  # forward
        monitor
    )
    if addr:
        # Identifier dans quelle section tombe l'adresse
        block = memory.getBlock(addr)
        block_name = block.getName() if block else "unknown"
        results.append({
            "constant": label,
            "address": "0x" + addr.toString(),
            "section": block_name,
        })
        print("[+] {} found at {} ({})".format(label, addr, block_name))

if not results:
    print("[-] No crypto constants found in {}".format(prog_name))

# Sauvegarder
output_dir = os.environ.get("GHIDRA_OUTPUT", "/tmp")  
output_path = os.path.join(output_dir, prog_name + "_crypto_scan.json")  
with open(output_path, "w") as f:  
    json.dump({"binary": prog_name, "findings": results}, f, indent=2)
```

Lancé en batch sur les binaires du chapitre 24, ce script détectera le masque `DE AD BE EF CA FE BA BE` dans `.rodata` (les huit premiers octets de `KEY_MASK`, stockés comme un tableau d'octets en big-endian). Sur les binaires du chapitre 21, il trouvera `DEADBEEF_LE` (`EF BE AD DE`) dans `.text`, correspondant à l'opérande immédiate de l'instruction `xor` qui applique `HASH_XOR` dans `compute_hash` — sur x86, les entiers 32 bits sont encodés en little-endian dans le flux d'instructions. Sur les binaires du chapitre 25, aucune constante crypto ne sera détectée — la clé XOR `{0x5A, 0x3C, 0x96, 0xF1}` est trop courte et trop générique pour figurer dans une base de signatures standard.

---

## Pipeline complet : du répertoire au rapport JSON

Voici le workflow type pour analyser un répertoire entier de binaires et produire un rapport consolidé. On utilise un script shell qui orchestre les appels à `analyzeHeadless`, puis un script Python standard (CPython) qui fusionne les résultats.

### Étape 1 : script d'orchestration shell

```bash
#!/bin/bash
# batch_ghidra.sh — Analyse un répertoire de binaires avec Ghidra headless
#
# Usage : ./batch_ghidra.sh <binaries_dir> <output_dir>

BINARIES_DIR="${1:?Usage: $0 <binaries_dir> <output_dir>}"  
OUTPUT_DIR="${2:?Usage: $0 <binaries_dir> <output_dir>}"  
PROJECT_DIR="/tmp/ghidra_batch_$$"  
PROJECT_NAME="batch"  
SCRIPT_DIR="$(dirname "$0")/ghidra_scripts"  

mkdir -p "$OUTPUT_DIR" "$PROJECT_DIR"

export GHIDRA_OUTPUT="$OUTPUT_DIR"

echo "=== Phase 1 : Import et analyse ==="  
ghidra-headless "$PROJECT_DIR" "$PROJECT_NAME" \  
    -import "$BINARIES_DIR" \
    -recursive \
    -overwrite \
    -analysisTimeoutPerFile 300 \
    -max-cpu 4

echo ""  
echo "=== Phase 2 : Extraction des fonctions ==="  
ghidra-headless "$PROJECT_DIR" "$PROJECT_NAME" \  
    -process \
    -postScript "${SCRIPT_DIR}/list_functions.py" \
    -noanalysis

echo ""  
echo "=== Phase 3 : Scan crypto ==="  
ghidra-headless "$PROJECT_DIR" "$PROJECT_NAME" \  
    -process \
    -postScript "${SCRIPT_DIR}/scan_crypto_constants.py" \
    -noanalysis

echo ""  
echo "=== Phase 4 : Consolidation ==="  
python3 "${SCRIPT_DIR}/merge_reports.py" "$OUTPUT_DIR"  

# Nettoyage du projet temporaire
rm -rf "$PROJECT_DIR"

echo ""  
echo "[+] Rapport final : ${OUTPUT_DIR}/report.json"  
```

La séparation en phases est délibérée. La phase 1 (import + analyse) est la plus coûteuse en temps et en CPU. Les phases 2 et 3 réutilisent le projet déjà analysé avec `-process` et `-noanalysis`, ce qui les rend rapides. Si on ajoute un nouveau script d'extraction plus tard, il suffit d'ajouter une phase — sans relancer l'analyse.

### Étape 2 : script de consolidation (CPython)

```python
#!/usr/bin/env python3
# merge_reports.py — Fusionne les JSON produits par les scripts Ghidra
#
# Usage : python3 merge_reports.py <output_dir>

import json  
import sys  
from pathlib import Path  

output_dir = Path(sys.argv[1])  
report = {}  

# Fusionner les rapports de fonctions
for path in sorted(output_dir.glob("*_functions.json")):
    with open(path) as f:
        data = json.load(f)
    binary_name = data["binary"]
    report.setdefault(binary_name, {})
    report[binary_name]["functions"] = data

# Fusionner les rapports crypto
for path in sorted(output_dir.glob("*_crypto_scan.json")):
    with open(path) as f:
        data = json.load(f)
    binary_name = data["binary"]
    report.setdefault(binary_name, {})
    report[binary_name]["crypto"] = data

# Résumé
summary = []  
for name, data in sorted(report.items()):  
    func_count = data.get("functions", {}).get("count", 0)
    crypto_count = len(data.get("crypto", {}).get("findings", []))
    summary.append({
        "binary": name,
        "functions_detected": func_count,
        "crypto_constants": crypto_count,
    })

final = {
    "summary": summary,
    "details": report,
}

output_path = output_dir / "report.json"  
with open(output_path, "w") as f:  
    json.dump(final, f, indent=2)

print(f"[+] Report: {output_path}")  
print(f"    {len(report)} binaries analyzed")  
for s in summary:  
    flag = " [CRYPTO]" if s["crypto_constants"] > 0 else ""
    print(f"    {s['binary']:<25} {s['functions_detected']:>4} functions{flag}")
```

Le résultat est un fichier `report.json` unique, structuré, exploitable par n'importe quel outil en aval — un dashboard web, un diff avec un rapport précédent, ou simplement un `jq` en ligne de commande :

```bash
# Quels binaires contiennent des constantes crypto ?
jq '.summary[] | select(.crypto_constants > 0)' report.json

# Combien de fonctions dans chaque variante du keygenme ?
jq '.summary[] | select(.binary | startswith("keygenme"))
    | {binary, functions_detected}' report.json
```

---

## Scripts Java vs Python : quand choisir l'un ou l'autre

Ghidra supporte deux langages pour les scripts headless : Java et Python (Jython). Le choix dépend de ce qu'on fait.

**Python (Jython)** est le choix par défaut pour les scripts d'extraction et de reporting. La syntaxe est concise, le prototypage est rapide, et la plupart des exemples de la communauté sont en Python. La limitation principale est l'absence de support pour les modules CPython natifs et la syntaxe Python 2.

**Java** est préférable quand le script interagit profondément avec les structures internes de Ghidra — par exemple pour créer des types de données complexes, manipuler le graphe de flux de contrôle, ou appeler des API internes non exposées en Python. Les scripts Java ont aussi l'avantage d'être plus performants sur des analyses lourdes (parcours de millions d'instructions).

En pratique dans un pipeline batch, on écrit les scripts d'extraction en Python (plus lisible, plus rapide à développer) et on réserve Java pour les cas où la performance ou l'accès à une API spécifique l'exige.

> 💡 **Ghidra 11+** introduit progressivement le support de Python 3 via Pyhidra/Jpype. Au moment de la rédaction, le mode headless classique reste en Jython 2.7. Si vous utilisez une version récente de Ghidra, consultez la documentation pour vérifier l'état du support Python 3 en headless.

---

## Considérations pratiques

### Performance et ressources

L'auto-analyse de Ghidra est gourmande en mémoire. Pour un batch de nombreux binaires, il faut ajuster les paramètres JVM. Le fichier `support/analyzeHeadless.bat` (ou le script shell équivalent) contient les options `-Xmx` et `-Xms`. Pour un batch lourd :

```bash
# Dans support/launch.properties (ou via variable d'environnement)
MAXMEM=8G
```

Si le batch porte sur des dizaines de binaires volumineux, il est souvent plus efficace de paralléliser en lançant plusieurs instances de `analyzeHeadless` sur des projets séparés, puis de fusionner les résultats en post-traitement — plutôt que de tout charger dans un seul projet.

### Gestion des erreurs

Un binaire corrompu ou un format non supporté ne doit pas interrompre le batch. Ghidra est robuste face aux fichiers invalides — il les ignorera avec un message d'erreur dans la console — mais il est bon de capturer les codes de retour dans le script shell :

```bash
ghidra-headless "$PROJECT_DIR" "$PROJECT_NAME" \
    -import "$file" \
    -overwrite \
    -postScript list_functions.py 2>&1 | tee "$OUTPUT_DIR/ghidra_log_${base}.txt"

if [ $? -ne 0 ]; then
    echo "[WARN] Ghidra returned non-zero for $file"
fi
```

### Reproductibilité

Pour que le même script produise le même résultat sur le même binaire à chaque exécution, il faut contrôler deux choses : la version de Ghidra (l'auto-analyse évolue entre versions) et les options d'analyse. On peut forcer des options spécifiques via un `-preScript` :

```python
# set_analysis_options.py — Garantir des options d'analyse cohérentes
from ghidra.program.util import GhidraProgramUtilities

# Désactiver les analyseurs qui ajoutent du bruit ou prennent trop de temps
setAnalysisOption(currentProgram, "Demangler GNU", "true")  
setAnalysisOption(currentProgram, "Stack", "true")  
setAnalysisOption(currentProgram, "Aggressive Instruction Finder", "false")  
```

```bash
ghidra-headless "$PROJECT_DIR" "$PROJECT_NAME" \
    -import keygenme_O0 \
    -preScript set_analysis_options.py \
    -postScript list_functions.py \
    -overwrite
```

---

## Résumé des commandes clés

| Action | Commande |  
|---|---|  
| Importer + analyser un binaire | `ghidra-headless <dir> <proj> -import <bin>` |  
| Importer un répertoire entier | `... -import <dir> -recursive` |  
| Exécuter un script après analyse | `... -postScript <script.py>` |  
| Exécuter un script avant analyse | `... -preScript <script.py>` |  
| Passer des arguments au script | `... -postScript <script.py> "arg1" "arg2"` |  
| Traiter un binaire déjà importé | `... -process <name> -noanalysis -postScript ...` |  
| Traiter tous les binaires du projet | `... -process -noanalysis -postScript ...` |  
| Spécifier le répertoire des scripts | `... -scriptPath <dir>` |  
| Limiter le temps d'analyse | `... -analysisTimeoutPerFile 300` |  
| Écraser les imports existants | `... -overwrite` |

---

*→ Section suivante : [35.3 — Scripting RE avec `pwntools`](/35-automatisation-scripting/03-scripting-pwntools.md)*

⏭️ [Scripting RE avec `pwntools` (interactions, patching, exploitation)](/35-automatisation-scripting/03-scripting-pwntools.md)
