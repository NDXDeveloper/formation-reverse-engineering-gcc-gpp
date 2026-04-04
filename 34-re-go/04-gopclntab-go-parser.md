🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 34.4 — Récupérer les noms de fonctions : `gopclntab` et `go_parser` pour Ghidra/IDA

> 🐹 *Quand vous strippez un binaire C, les noms de fonctions disparaissent définitivement. Quand vous strippez un binaire Go, ils sont toujours là — cachés dans une structure que le runtime utilise pour générer les stack traces et alimenter le garbage collector. Cette structure s'appelle `gopclntab`, et elle est la meilleure amie du reverse engineer face à un binaire Go.*

---

## Pourquoi les noms survivent au stripping

Pour comprendre pourquoi `gopclntab` existe et pourquoi `strip` ne la supprime pas, il faut revenir aux besoins du runtime Go.

### Les besoins du runtime

Le runtime Go a besoin, à l'exécution, de pouvoir :

1. **Générer des stack traces lisibles.** Quand une goroutine panique ou qu'un profiler capture un état, Go affiche des traces du type `main.parseKey(...)` avec le fichier source et le numéro de ligne. Ces informations doivent être dans le binaire.  
2. **Identifier les frames de pile pour le GC.** Le garbage collector doit parcourir les piles de toutes les goroutines pour trouver les pointeurs vivants. Pour cela, il doit savoir, pour chaque adresse PC (program counter), quelle fonction est en cours d'exécution, quelle est la taille de sa frame, et où se trouvent les pointeurs dans cette frame.  
3. **Supporter `runtime.Callers` et le package `runtime`.** Les fonctions `runtime.Caller()`, `runtime.FuncForPC()` et le package `runtime/pprof` dépendent tous de la capacité à résoudre une adresse PC en nom de fonction.

Ces besoins sont **fonctionnels**, pas optionnels. Si vous supprimez `gopclntab`, le binaire ne fonctionne plus : les panics crashent sans trace, le GC ne peut plus parcourir les piles, et le programme est instable voire inutilisable.

### Ce que `strip` supprime réellement

La commande `strip -s` sur un binaire Go supprime :

- la table de symboles ELF (`.symtab`),  
- les informations de débogage DWARF (`.debug_*`),  
- la table de symboles dynamiques si applicable.

Mais elle **ne touche pas** aux sections `.gopclntab`, `.go.buildid`, `.noptrdata`, `.noptrbss`, ni aux structures de type dans `.rodata` — parce que ces données sont référencées par le code du runtime et participent au fonctionnement du programme. Du point de vue de `strip`, ce sont des données comme les autres, pas des métadonnées de débogage.

> 💡 **Astuce RE** : c'est la différence fondamentale avec le C. En C, les noms de fonctions dans `.symtab` ne servent qu'au débogage et au linker — le programme n'en a jamais besoin à l'exécution. En Go, les métadonnées de fonctions sont une dépendance runtime. Le stripping est donc largement cosmétique.

---

## Anatomie de `gopclntab`

### Localiser la table

La table `gopclntab` (Go PC-Line Table) est stockée dans une section ELF dédiée ou dans `.noptrdata`. Pour la localiser :

**Méthode 1 — Par le nom de section :**

```bash
readelf -S binaire | grep gopclntab
```

Sur un binaire non strippé, vous verrez une section `.gopclntab`. Sur certaines versions de Go ou après stripping, la section peut ne pas avoir ce nom, mais les données sont toujours présentes dans `.noptrdata`.

**Méthode 2 — Par le magic number :**

La table commence par un en-tête avec un magic number qui varie selon la version de Go :

| Version Go | Magic (4 octets, little-endian) |  
|---|---|  
| Go 1.2 – 1.15 | `FB FF FF FF` |  
| Go 1.16 – 1.17 | `FA FF FF FF` |  
| Go 1.18 – 1.19 | `F0 FF FF FF` |  
| Go 1.20+ | `F1 FF FF FF` |

```bash
# Rechercher le magic dans le binaire
xxd binaire | grep -i 'f1ff ffff\|f0ff ffff\|faff ffff\|fbff ffff'
```

**Méthode 3 — Via le runtime :**

Le runtime accède à `gopclntab` via la variable globale `runtime.pclntab` (ou `runtime.firstmoduledata.pclntable`). Si vous trouvez le symbole `runtime.firstmoduledata` (section 34.6), le champ `pclntable` vous donne directement l'adresse et la taille de la table.

### Structure de l'en-tête (Go 1.20+)

L'en-tête de `gopclntab` a évolué significativement entre les versions. Voici le format actuel (Go 1.20+) :

```
Offset   Taille   Champ            Description
──────   ──────   ─────            ───────────
+0x00    4        magic            Magic number (0xFFFFFFF1 en Go 1.20+)
+0x04    1        pad1             Padding (0x00)
+0x05    1        pad2             Padding (0x00)
+0x06    1        minLC            Quantum d'instruction minimum (1 sur amd64)
+0x07    1        ptrSize          Taille d'un pointeur (8 sur amd64)
+0x08    N        nfunc            Nombre de fonctions (entier de taille ptrSize)
+0x08+N  N        nfiles           Nombre de fichiers source
+0x08+2N ...      textStart        Adresse de base du segment .text
+...     ...      funcnameOffset   Offset vers la table des noms de fonctions
+...     ...      cutabOffset      Offset vers la table CU (compilation units)
+...     ...      filetabOffset    Offset vers la table des noms de fichiers
+...     ...      pctabOffset      Offset vers la table PC (program counter)
+...     ...      pclnOffset       Offset vers la table pcln (entrées de fonctions)
```

Les offsets exacts des champs après `nfiles` dépendent de la version. L'important est que l'en-tête contient les offsets relatifs vers cinq sous-tables.

### La table des fonctions (`functab`)

Après l'en-tête, la table `functab` contient une entrée par fonction. Chaque entrée associe une adresse PC de début à un offset vers un enregistrement `_func` :

```
functab entry (Go 1.20+, chaque entrée = 2 × 4 octets)
┌───────────────────────┬───────────────────────────┐
│ funcoff (uint32)      │ funcdata_offset (uint32)  │
│ Offset PC relatif     │ Offset vers _func record  │
│ au textStart          │ dans pclntab              │
└───────────────────────┴───────────────────────────┘
```

### L'enregistrement `_func`

Chaque fonction est décrite par un enregistrement `_func` :

```
_func (Go 1.20+, simplifié)
Offset   Taille   Champ        Description
──────   ──────   ─────        ───────────
+0x00    4        entryOff     Offset PC d'entrée (relatif à textStart)
+0x04    4        nameOff      Offset dans la table des noms → nom de la fonction
+0x08    4        args          Taille des arguments en octets
+0x0C    4        deferreturn   Offset du point de retour defer (0 si pas de defer)
+0x10    4        pcsp          Offset dans pctab → table PC-to-SP delta
+0x14    4        pcfile        Offset dans pctab → table PC-to-file index
+0x18    4        pcln          Offset dans pctab → table PC-to-line number
+0x1C    4        npcdata       Nombre d'entrées pcdata
+0x20    4        cuOffset      Index de la compilation unit
+0x24    1        startLine     Ligne de début (offset relatif)
+0x25    1        funcID        ID de la fonction (pour les fonctions runtime spéciales)
+0x26    1        flag          Flags
+0x27    1        (padding)
+0x28    4        nfuncdata     Nombre d'entrées funcdata
```

Le champ `nameOff` est votre cible principale : il pointe dans la table des noms de fonctions, où vous trouverez la chaîne `main.parseKey`, `runtime.newproc`, etc.

### La table des noms de fonctions

C'est une simple zone de chaînes null-terminées (un des rares endroits où Go utilise des null terminators — pour la compatibilité avec le C et le système). Chaque nom est un chemin qualifié complet : `main.main`, `main.(*ChecksumValidator).Validate`, `runtime.mallocgc`, etc.

### Les tables PC-value (pcsp, pcfile, pcln)

Les tables `pcsp`, `pcfile` et `pcln` utilisent un encodage compact appelé **pc-value encoding** : une séquence d'octets qui encode des paires (delta-PC, delta-valeur) en utilisant un encodage variable (similaire à LEB128/varint). Pour chaque adresse PC dans la fonction, ces tables permettent de récupérer :

- **pcsp** : le delta entre SP et le haut de la frame → donne la taille de la stack frame à chaque instruction,  
- **pcfile** : l'index du fichier source → quel fichier `.go` est concerné,  
- **pcln** : le numéro de ligne → correspondance PC-to-line.

En RE, la table pcln est utile pour remonter au numéro de ligne source, et pcsp pour comprendre la disposition de la pile.

---

## Extraire les noms manuellement

### Avec un script Python minimaliste

Pour extraire les noms de fonctions d'un binaire Go strippé sans outils spécialisés, le principe est :

1. Localiser le magic de `gopclntab` dans le fichier.  
2. Parser l'en-tête pour obtenir `nfunc` et les offsets des sous-tables.  
3. Itérer sur `functab` : pour chaque entrée, lire `nameOff` dans le record `_func`, puis lire la chaîne correspondante dans la table des noms.

Voici le squelette (pour Go 1.20+, amd64) :

```python
import struct

def find_gopclntab(data):
    """Cherche le magic gopclntab dans le binaire."""
    magics = [b'\xf1\xff\xff\xff', b'\xf0\xff\xff\xff',
              b'\xfa\xff\xff\xff', b'\xfb\xff\xff\xff']
    for m in magics:
        off = data.find(m)
        if off != -1:
            return off, m
    return None, None

def read_cstring(data, offset):
    """Lit une chaîne null-terminée."""
    end = data.index(b'\x00', offset)
    return data[offset:end].decode('utf-8', errors='replace')

def extract_func_names(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    base, magic = find_gopclntab(data)
    if base is None:
        print("gopclntab non trouvée")
        return

    ptr_size = data[base + 7]
    # Lire nfunc (8 octets sur amd64)
    nfunc = struct.unpack_from('<Q', data, base + 8)[0]
    # ... parser les offsets des sous-tables selon la version ...
    # ... itérer sur functab et lire les noms ...
    print(f"Trouvé {nfunc} fonctions à l'offset 0x{base:x}")
```

Ce squelette illustre la démarche. En pratique, utilisez les outils dédiés présentés ci-dessous — ils gèrent les subtilités de chaque version.

### Avec `objdump` sur un binaire non strippé

Si le binaire n'est pas strippé, `objdump -t` affiche les symboles Go normalement :

```bash
objdump -t crackme_go | grep 'main\.'
```

Mais sur un binaire strippé, `objdump -t` ne montre rien. C'est là que les outils spécifiques entrent en jeu.

---

## GoReSym (Mandiant)

### Présentation

GoReSym est un outil open source développé par Mandiant (Google) spécifiquement pour extraire les métadonnées des binaires Go. Il parse `gopclntab`, les structures de type et `moduledata` pour produire un listing complet des fonctions, types et fichiers source.

### Installation

```bash
go install github.com/mandiant/GoReSym@latest
```

Ou téléchargez un binaire pré-compilé depuis les releases GitHub.

### Utilisation

```bash
# Extraction complète au format JSON
GoReSym -t -d -p /chemin/vers/crackme_go_strip

# Options :
#   -t   extraire les types
#   -d   extraire les informations de fichiers/lignes
#   -p   extraire les noms de packages
```

La sortie JSON contient :

```json
{
  "Version": "go1.22.1",
  "BuildInfo": { ... },
  "TabMeta": {
    "VA": 5234688,
    "Version": "1.20",
    "Endian": "LittleEndian",
    "CpuQuantum": 1,
    "CpuWordSize": 8
  },
  "Types": [ ... ],
  "UserFunctions": [
    {
      "Start": 4923648,
      "End": 4924160,
      "PackageName": "main",
      "FullName": "main.parseKey",
      "FileName": "/home/user/crackme_go/main.go",
      "StartLine": 71
    },
    ...
  ],
  "StdFunctions": [ ... ]
}
```

Les points importants pour le RE :

- **`UserFunctions`** vs **`StdFunctions`** : GoReSym sépare automatiquement les fonctions du code métier de celles de la bibliothèque standard et du runtime. C'est exactement le filtrage dont vous avez besoin.  
- **`Start` / `End`** : les adresses virtuelles de début et fin de chaque fonction — directement utilisables pour créer des symboles dans Ghidra ou IDA.  
- **`FileName` / `StartLine`** : le chemin source et le numéro de ligne. Le chemin révèle la structure du projet, les noms de packages, et parfois le nom d'utilisateur ou l'environnement de build du développeur.  
- **`Version`** : la version exacte du compilateur Go — indispensable pour déterminer l'ABI (section 34.2).

> 💡 **Astuce RE** : lancez GoReSym en tout premier sur un binaire Go inconnu. En quelques secondes, vous obtenez la version du compilateur, la liste des fonctions utilisateur avec leurs adresses, et les types définis. C'est l'équivalent d'un `nm` surpuissant pour Go.

### Exploiter la sortie avec `jq`

```bash
# Lister uniquement les fonctions du package main
GoReSym -p crackme_go_strip | jq '.UserFunctions[] | select(.PackageName=="main") | .FullName'

# Compter les fonctions par package
GoReSym -p crackme_go_strip | jq '[.UserFunctions[].PackageName] | group_by(.) | map({pkg: .[0], count: length}) | sort_by(-.count)'

# Extraire les types définis par l'utilisateur
GoReSym -t crackme_go_strip | jq '.Types[] | select(.PackageName=="main")'
```

---

## go_parser pour IDA

### Présentation

`go_parser` est un script IDAPython qui parse les structures internes d'un binaire Go et applique automatiquement les noms de fonctions, les types et les commentaires dans la base IDA.

### Installation et usage

1. Clonez le dépôt : `git clone https://github.com/0xjiayu/go_parser.git`  
2. Dans IDA, ouvrez le binaire Go strippé.  
3. Lancez `File → Script File...` et sélectionnez `go_parser.py`.  
4. Le script détecte automatiquement la version de Go et parse `gopclntab`.

Après exécution, le script :

- renomme toutes les fonctions avec leurs noms Go (`main.parseKey`, `runtime.mallocgc`, etc.),  
- crée des commentaires avec les noms de fichiers et numéros de lignes,  
- définit les strings Go comme des données nommées,  
- reconstruit partiellement les structures de type.

> ⚠️ **Note** : `go_parser` n'est plus activement maintenu et peut ne pas supporter les versions les plus récentes de Go (1.22+). Pour les binaires récents, privilégiez GoReSym combiné avec un script d'import.

---

## Plugins et scripts pour Ghidra

### Support natif de Ghidra

Depuis Ghidra 10.2 (fin 2022), l'analyseur intègre un support basique des binaires Go :

- détection automatique du langage Go lors de l'import,  
- parsing partiel de `gopclntab` pour renommer les fonctions,  
- reconnaissance du préambule de vérification de pile.

Ce support natif est un bon point de départ mais reste limité. Les noms de fonctions sont généralement récupérés, mais les types, les structures et les signatures de fonctions ne sont pas reconstruits automatiquement.

### `GoReSym` + script d'import Ghidra

La méthode la plus fiable consiste à utiliser GoReSym pour l'extraction, puis un script Ghidra (Java ou Python) pour appliquer les résultats :

**Étape 1 — Extraction avec GoReSym :**

```bash
GoReSym -t -d -p crackme_go_strip > metadata.json
```

**Étape 2 — Script Ghidra Python d'import :**

```python
# apply_goresym.py — Script Ghidra pour appliquer les résultats GoReSym
# À exécuter dans Ghidra Script Manager (Window → Script Manager)
import json

# Charger le fichier JSON produit par GoReSym
json_path = askFile("Sélectionner le JSON GoReSym", "Ouvrir").getPath()  
with open(json_path, 'r') as f:  
    data = json.load(f)

listing = currentProgram.getListing()  
func_mgr = currentProgram.getFunctionManager()  
space = currentProgram.getAddressFactory().getDefaultAddressSpace()  

count = 0  
for func_info in data.get("UserFunctions", []) + data.get("StdFunctions", []):  
    addr = space.getAddress(func_info["Start"])
    name = func_info["FullName"]

    # Renommer la fonction si elle existe
    func = func_mgr.getFunctionAt(addr)
    if func is not None:
        func.setName(name, ghidra.program.model.symbol.SourceType.USER_DEFINED)
        count += 1
    else:
        # Créer la fonction si Ghidra ne l'a pas détectée
        try:
            createFunction(addr, name)
            count += 1
        except:
            pass

    # Ajouter un commentaire avec le fichier source et la ligne
    if "FileName" in func_info and "StartLine" in func_info:
        comment = "{}:{}".format(func_info["FileName"], func_info["StartLine"])
        setPreComment(addr, comment)

print("Appliqué {} noms de fonctions.".format(count))
```

Ce script parcourt le JSON, renomme chaque fonction à son adresse, et ajoute un commentaire pré-instruction avec le fichier source et le numéro de ligne. Après exécution, le Symbol Tree et le Listing de Ghidra deviennent lisibles.

### `ghidra-go-analyzer` (communautaire)

Le projet `ghidra-go-analyzer` est une extension Ghidra dédiée qui va plus loin que le script d'import :

- parsing complet de `gopclntab` avec support multi-version,  
- reconstruction des types Go (structs, interfaces, slices),  
- application de l'ABI correcte (stack ou registres) aux signatures,  
- détection des closures et rattachement aux fonctions parentes.

L'installation se fait via le Ghidra Extension Manager. Cherchez le projet sur GitHub pour la version compatible avec votre version de Ghidra.

---

## Radare2 et `r2go`

Radare2 dispose d'un support Go basique via l'analyse automatique `aaa`. Les versions récentes de Radare2 détectent `gopclntab` et renomment les fonctions.

Pour une analyse plus poussée, le plugin `r2go` et les scripts r2pipe dédiés permettent d'extraire les métadonnées. L'approche en ligne de commande :

```bash
# Analyse automatique (détecte Go et parse gopclntab)
r2 -A crackme_go_strip

# Lister les fonctions (les noms Go devraient apparaître)
afl~main.

# Si l'analyse automatique n'a pas fonctionné, forcer le parsing
# en cherchant manuellement le magic gopclntab
/x f1ffffff
```

Le support est moins complet que GoReSym + Ghidra, mais suffisant pour un triage rapide en ligne de commande.

---

## `moduledata` : la clé de voûte

### Structure `runtime.firstmoduledata`

Toutes les métadonnées du binaire Go sont accessibles via la structure `runtime.firstmoduledata` (ou `runtime.moduledata` dans certaines versions). C'est le point d'entrée central du runtime vers les tables de fonctions, de types, et de noms :

```
runtime.moduledata (champs principaux, simplifié)  
Champ              Description  
─────              ───────────
pclntable          Slice vers la table gopclntab complète  
ftab               Slice vers la function table  
filetab            Slice vers la table des fichiers source  
findfunctab        Pointeur vers la table de lookup rapide  
text               Adresse de début du segment .text  
etext              Adresse de fin du segment .text  
noptrdata          Début de la section .noptrdata  
enoptrdata         Fin de la section .noptrdata  
data               Début de la section .data  
edata              Fin de la section .data  
bss                Début de .bss  
ebss               Fin de .bss  
typelinks          Slice d'offsets vers les descripteurs de type  
itablinks          Slice de pointeurs vers les itabs  
modulename         Nom du module (string)  
next               Pointeur vers le moduledata suivant (plugins)  
```

### Localiser `moduledata` dans un binaire strippé

Si les symboles sont absents, `moduledata` peut être retrouvé par plusieurs méthodes :

**Méthode 1 — Par référence depuis `runtime.main` :**

La fonction `runtime.main` (retrouvable via `gopclntab`) accède à `firstmoduledata` tôt dans son exécution. En suivant les cross-references depuis cette fonction, vous trouverez l'adresse de `moduledata`.

**Méthode 2 — Par recherche de pattern :**

`moduledata` contient les adresses de `text` et `etext` (début et fin du segment `.text`). Si vous connaissez ces adresses (via `readelf -l`), cherchez en mémoire deux pointeurs consécutifs correspondant à ces valeurs.

**Méthode 3 — Via GoReSym :**

GoReSym localise automatiquement `moduledata` et l'expose dans sa sortie JSON (champ `ModuleMeta`).

> 💡 **Astuce RE** : une fois `moduledata` localisé, vous avez accès à la totalité des métadonnées du binaire. Le champ `typelinks` vous mène aux descripteurs de type (section 34.3), `pclntable` vous donne `gopclntab`, et les paires `text`/`etext`, `data`/`edata` vous donnent les limites exactes des segments.

---

## Workflow recommandé

Voici la procédure optimale face à un binaire Go strippé :

```
1. Identification
   └─► strings binaire | grep 'go1\.'       → version du compilateur
   └─► readelf -S binaire | grep gopclntab  → présence de la section

2. Extraction des métadonnées
   └─► GoReSym -t -d -p binaire > meta.json
   └─► jq '.Version' meta.json              → confirmer la version Go
   └─► jq '.UserFunctions | length' meta.json → nombre de fonctions utilisateur

3. Triage des fonctions
   └─► jq '.UserFunctions[] | .FullName' meta.json
       → identifier les packages et fonctions métier

4. Import dans le désassembleur
   └─► Ghidra : exécuter le script d'import GoReSym
   └─► IDA : lancer go_parser ou importer via IDAPython
   └─► Radare2 : r2 -A (analyse auto) puis vérifier afl~main.

5. Analyse ciblée
   └─► Se concentrer sur les fonctions main.* et packages métier
   └─► Ignorer runtime.*, internal/*, vendor/*
```

Ce workflow transforme un binaire Go strippé — apparemment opaque avec ses milliers de fonctions anonymes — en une cible presque aussi lisible qu'un binaire avec symboles.

---

## Limites et contre-mesures

### Ce que `gopclntab` ne vous donne pas

- **Le code source.** Vous obtenez les noms de fonctions et les numéros de lignes, mais pas le code Go lui-même.  
- **Les noms de variables locales.** Seuls les noms de fonctions et de types sont dans les métadonnées runtime. Les variables locales n'y figurent pas (elles sont dans les infos DWARF, supprimées par `strip`).  
- **Les commentaires du développeur.** Évidemment absents du binaire.

### Contre-mesures des auteurs de malware

Certains développeurs de malware Go tentent de neutraliser ces métadonnées :

- **Garbling des noms** avec des outils comme `garble` (anciennement `burrern`). `garble` remplace les noms de fonctions, de packages et de types par des identifiants aléatoires ou des hashes. Les métadonnées sont toujours présentes dans `gopclntab`, mais les noms sont illisibles (`a0b1c2d3.x4y5z6` au lieu de `main.parseKey`).  
- **Modification de `gopclntab`** en post-compilation. Techniquement possible mais fragile : si les noms sont corrompus de manière incohérente, le runtime peut crasher.  
- **Compilation avec `-ldflags="-s -w"`** qui supprime les informations DWARF et la table de symboles, mais **ne supprime pas `gopclntab`** — cette option est souvent mal comprise par les développeurs qui pensent avoir caché leurs noms.

Face à un binaire garblé :

1. Les noms sont illisibles, mais la **structure** des fonctions reste intacte. Vous pouvez toujours compter les fonctions, mesurer leur taille et analyser les cross-references.  
2. Les **descripteurs de type** sont partiellement garblés, mais les types de la bibliothèque standard (qui ne sont pas garblés) vous donnent des indices sur les structures utilisées.  
3. Les **chaînes littérales** dans le code ne sont généralement pas garblées par `garble` (sauf option spécifique). Elles restent une source d'information.  
4. L'**analyse dynamique** (GDB, Frida) n'est pas affectée par le garbling — les comportements et les valeurs en mémoire restent identiques.

> 💡 **Astuce RE** : face à un binaire garblé, concentrez-vous sur les strings littérales, les constantes, les appels au runtime (`runtime.mapaccess*`, `runtime.chansend1`, etc.) et les patterns structurels plutôt que sur les noms de fonctions. Le garbling cache les noms, pas la logique.

---

## Ce qu'il faut retenir

1. **`gopclntab` survit à `strip`** parce qu'elle est une dépendance fonctionnelle du runtime, pas une métadonnée de débogage.  
2. **GoReSym est l'outil de référence.** Lancez-le systématiquement en premier sur tout binaire Go. Il vous donne la version du compilateur, les noms de fonctions, les types et les fichiers source.  
3. **Importez les résultats dans votre désassembleur.** Un script de quelques dizaines de lignes suffit pour transformer Ghidra ou IDA d'un amas de fonctions anonymes en une vue structurée et nommée.  
4. **`moduledata` est la clé de voûte.** Elle lie toutes les métadonnées entre elles — `gopclntab`, types, segments, itabs.  
5. **Le garbling (`garble`) est la principale contre-mesure.** Il rend les noms illisibles mais ne supprime pas les structures. L'analyse par comportement et par strings littérales reste efficace.  
6. **Filtrez le bruit.** Avec des milliers de fonctions runtime, votre santé mentale dépend de votre capacité à ignorer `runtime.*` et à vous concentrer sur les packages applicatifs.

⏭️ [Strings en Go : structure `(ptr, len)` et implications pour `strings`](/34-re-go/05-strings-go-ptr-len.md)
