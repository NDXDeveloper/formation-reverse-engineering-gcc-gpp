🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 35.6 — Construire son propre toolkit RE : organiser ses scripts et snippets

> 🎯 **Objectif** : structurer les scripts développés tout au long de cette formation (et ceux que vous écrirez ensuite) en un toolkit personnel cohérent, documenté, versionné et réutilisable d'une analyse à l'autre.

---

## Le problème du script jetable

Chaque analyste RE accumule des scripts. Un one-liner `pyelftools` pour lister les fonctions. Un snippet Frida pour hooker `strcmp`. Un template de keygen `pwntools`. Un script GDB qui dumpe la mémoire autour d'un breakpoint. Une règle YARA écrite à 2h du matin pendant un CTF.

Ces scripts naissent dans des répertoires temporaires, avec des noms comme `test.py`, `solve2_final_v3.py`, ou directement dans l'historique du terminal. Trois semaines plus tard, face à un binaire similaire, l'analyste réécrit le même script depuis zéro parce qu'il ne retrouve plus l'original — ou le retrouve, mais ne comprend plus ce qu'il fait.

Le coût de cette désorganisation est réel. Ce n'est pas un problème de discipline personnelle — c'est un problème d'ingénierie. Les mêmes principes qui font qu'un projet logiciel fonctionne (structure de répertoires, documentation, gestion de dépendances, versionnement) s'appliquent à un toolkit RE. Cette section pose les fondations pratiques.

---

## Structure de répertoires

Un toolkit RE n'est pas un projet logiciel classique. Il ne produit pas un unique livrable — c'est une collection d'outils indépendants qui partagent des utilitaires communs. La structure doit refléter cette réalité : chaque script est autonome, mais les briques réutilisables sont factorisées.

```
re-toolkit/
│
├── README.md                  ← Description du toolkit, installation, index
├── requirements.txt           ← Dépendances Python (lief, yara-python, pwntools…)
├── setup.sh                   ← Installation automatisée de l'environnement
│
├── triage/                    ← Scripts de premier contact avec un binaire
│   ├── triage_elf.py          ← Triage complet (lief + checksec + YARA)
│   ├── quick_strings.py       ← Extraction de strings avec contexte (section, offset)
│   └── compare_builds.py      ← Diff entre deux versions d'un binaire
│
├── static/                    ← Analyse statique automatisée
│   ├── list_functions.py      ← pyelftools : lister les fonctions
│   ├── find_crypto.py         ← Recherche de constantes crypto (lief + patterns)
│   ├── extract_rodata.py      ← Dumper .rodata avec annotations
│   └── ghidra/                ← Scripts Ghidra headless
│       ├── list_functions.py
│       ├── decompile_all.py
│       ├── find_callers.py
│       └── scan_crypto.py
│
├── dynamic/                   ← Analyse dynamique et instrumentation
│   ├── gdb/
│   │   ├── dump_strcmp.py      ← Script GDB Python : log des args strcmp
│   │   ├── dump_malloc.py      ← Script GDB Python : log malloc/free
│   │   └── break_on_crypto.gdb ← Breakpoints sur les fonctions crypto courantes
│   └── frida/
│       ├── hook_strcmp.js       ← Hook strcmp avec log des arguments
│       ├── hook_network.js     ← Hook send/recv/connect
│       └── hook_crypto.js      ← Hook EVP_*, SHA256, etc.
│
├── patching/                  ← Modification de binaires
│   ├── flip_jump.py           ← Inverser un saut conditionnel (lief/pwntools)
│   ├── nop_range.py           ← NOP-out une plage d'adresses
│   └── add_section.py         ← Ajouter une section custom à un ELF
│
├── keygen/                    ← Templates de keygens et solvers
│   ├── keygen_template.py     ← Template pwntools (process + GDB extract)
│   ├── angr_template.py       ← Template angr (find/avoid)
│   └── z3_template.py         ← Template Z3 (contraintes manuelles)
│
├── formats/                   ← Parsers de formats custom
│   ├── parse_cfr.py           ← Parser du format CFR (ch25)
│   ├── parse_crypt24.py       ← Parser du format CRYPT24 (ch24)
│   └── hexpat/
│       ├── elf_header.hexpat
│       ├── cfr_format.hexpat
│       └── crypt24_format.hexpat
│
├── yara/                      ← Règles YARA
│   ├── crypto_constants.yar
│   ├── packer_signatures.yar
│   └── custom/                ← Règles spécifiques à vos analyses
│       └── .gitkeep
│
├── ci/                        ← Intégration CI/CD
│   ├── audit_binary.py
│   ├── diff_audits.py
│   └── policies/
│       └── default_policy.json
│
├── lib/                       ← Utilitaires partagés
│   ├── __init__.py
│   ├── elf_helpers.py         ← Fonctions communes pyelftools/lief
│   ├── format_utils.py        ← Packing, unpacking, hex dump
│   ├── report.py              ← Génération de rapports JSON/Markdown
│   └── constants.py           ← Constantes crypto connues, signatures
│
└── docs/                      ← Documentation du toolkit
    ├── INSTALL.md             ← Guide d'installation détaillé
    ├── CONVENTIONS.md         ← Conventions de code et de nommage
    └── CATALOG.md             ← Index des scripts avec description et usage
```

### Principes de cette structure

**Un répertoire par phase d'analyse.** `triage/`, `static/`, `dynamic/`, `patching/` suivent le workflow naturel du RE. Quand on commence une analyse, on sait dans quel répertoire chercher.

**Scripts Ghidra séparés.** Les scripts Ghidra headless vivent dans `static/ghidra/` parce qu'ils s'exécutent dans l'interpréteur Jython de Ghidra, pas dans l'environnement Python du toolkit. Ils ne peuvent pas importer `lib/` — cette séparation physique évite la confusion.

**Templates dans `keygen/`.** Un template n'est pas un script fini — c'est un squelette avec des sections à remplir (`# TODO : insérer l'adresse du find`, `# TODO : modéliser les contraintes`). Il économise les dix premières minutes de chaque nouveau challenge.

**`lib/` pour le code partagé.** Les fonctions utilisées par plusieurs scripts (parsing d'ELF, formatage hexadécimal, génération de rapports) sont factorisées ici. Le `__init__.py` en fait un package importable :

```python
# Dans n'importe quel script du toolkit :
from lib.elf_helpers import list_functions, get_imports  
from lib.report import generate_json_report  
```

---

## Le module `lib/` : utilitaires partagés

Le module `lib/` est le ciment du toolkit. Plutôt que de copier-coller les mêmes dix lignes de parsing ELF dans chaque script, on les écrit une fois ici.

### `lib/elf_helpers.py`

```python
"""Fonctions utilitaires pour l'inspection d'ELF avec lief et pyelftools."""

import lief  
from elftools.elf.elffile import ELFFile  
from elftools.elf.sections import SymbolTableSection  

def quick_info(path):
    """Retourne un dict avec les propriétés essentielles d'un binaire."""
    b = lief.parse(str(path))
    if b is None:
        return None
    return {
        "path": str(path),
        "pie": b.is_pie,
        "nx": b.has_nx,
        "stripped": len(list(b.static_symbols)) == 0,
        "libraries": list(b.libraries),
        "imports": sorted(s.name for s in b.imported_symbols if s.name),
        "sections": {s.name: {"size": s.size, "entropy": round(s.entropy, 2)}
                     for s in b.sections if s.name},
    }

def list_functions(path):
    """Liste les fonctions via pyelftools (binaire non strippé)."""
    functions = []
    with open(str(path), "rb") as f:
        elf = ELFFile(f)
        for section in elf.iter_sections():
            if not isinstance(section, SymbolTableSection):
                continue
            for sym in section.iter_symbols():
                if sym['st_info']['type'] == 'STT_FUNC' and sym['st_value']:
                    functions.append({
                        "name": sym.name,
                        "addr": sym['st_value'],
                        "size": sym['st_size'],
                    })
    return sorted(functions, key=lambda f: f["addr"])

def get_imports(path):
    """Retourne l'ensemble des symboles importés."""
    b = lief.parse(str(path))
    return {s.name for s in b.imported_symbols if s.name} if b else set()

def find_bytes(path, pattern, section_name=None):
    """Cherche une séquence d'octets, optionnellement restreinte à une section."""
    b = lief.parse(str(path))
    if b is None:
        return []
    results = []
    if section_name:
        s = b.get_section(section_name)
        if s:
            content = bytes(s.content)
            offset = 0
            while True:
                idx = content.find(pattern, offset)
                if idx < 0:
                    break
                results.append(s.virtual_address + idx)
                offset = idx + 1
    else:
        for addr in b.elf.search(pattern) if hasattr(b, 'elf') else []:
            results.append(addr)
    return results
```

### `lib/format_utils.py`

```python
"""Utilitaires de formatage et de manipulation de données binaires."""

import struct

def hexdump(data, base_addr=0, width=16):
    """Produit un hexdump formaté d'un buffer."""
    lines = []
    for offset in range(0, len(data), width):
        chunk = data[offset:offset + width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {base_addr + offset:08x}  {hex_part:<{width*3}}  {ascii_part}")
    return "\n".join(lines)

def u8(data, offset):
    return struct.unpack_from("<B", data, offset)[0]

def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]

def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]

def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]

def p32(value):
    return struct.pack("<I", value & 0xFFFFFFFF)

def find_all(data, pattern):
    """Retourne tous les offsets d'un pattern dans un buffer."""
    results = []
    offset = 0
    while True:
        idx = data.find(pattern, offset)
        if idx < 0:
            break
        results.append(idx)
        offset = idx + 1
    return results
```

### `lib/report.py`

```python
"""Génération de rapports structurés."""

import json  
import time  
from pathlib import Path  

def generate_json_report(binary_path, data, output_path=None):
    """Enveloppe un dict de résultats dans un rapport horodaté."""
    report = {
        "metadata": {
            "binary": str(binary_path),
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "toolkit_version": "1.0.0",
        },
        "results": data,
    }
    if output_path:
        Path(output_path).parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, "w") as f:
            json.dump(report, f, indent=2)
    return report

def generate_markdown_report(binary_path, data):
    """Produit un rapport Markdown lisible par un humain."""
    lines = [
        f"# Rapport d'analyse — `{Path(binary_path).name}`",
        f"",
        f"**Date** : {time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime())}",
        f"**Binaire** : `{binary_path}`",
        f"",
    ]
    for section_name, section_data in data.items():
        lines.append(f"## {section_name}")
        lines.append("")
        if isinstance(section_data, dict):
            for k, v in section_data.items():
                lines.append(f"- **{k}** : {v}")
        elif isinstance(section_data, list):
            for item in section_data:
                lines.append(f"- {item}")
        else:
            lines.append(str(section_data))
        lines.append("")
    return "\n".join(lines)
```

---

## Gestion des dépendances

Un toolkit inutilisable à cause d'un `ModuleNotFoundError` est un toolkit mort. La gestion explicite des dépendances est la première chose à mettre en place.

### `requirements.txt`

```
# re-toolkit/requirements.txt
# Dépendances Python du toolkit RE
# Installation : pip install -r requirements.txt

# Parsing et modification d'ELF (section 35.1)
pyelftools>=0.29  
lief>=0.13.0  

# Interaction avec les binaires (section 35.3)
pwntools>=4.11.0

# Scan de patterns (section 35.4)
yara-python>=4.3.0

# Exécution symbolique (chapitre 18)
angr>=9.2.0  
z3-solver>=4.12.0  

# Utilitaires
capstone>=5.0.0        # désassemblage  
keystone-engine>=0.9.2 # assemblage  
```

### `setup.sh`

```bash
#!/bin/bash
# setup.sh — Installation de l'environnement du toolkit RE
set -e

echo "=== Installation du toolkit RE ==="

# Environnement virtuel Python
if [ ! -d ".venv" ]; then
    python3 -m venv .venv
    echo "[+] Environnement virtuel créé"
fi  
source .venv/bin/activate  

# Dépendances Python
pip install --upgrade pip  
pip install -r requirements.txt  
echo "[+] Dépendances Python installées"  

# Dépendances système (Debian/Ubuntu)
if command -v apt-get &>/dev/null; then
    sudo apt-get install -y yara gdb gcc g++ make binutils \
        libssl-dev strace ltrace
    echo "[+] Dépendances système installées"
fi

# Vérification
echo ""  
echo "=== Vérification ==="  
python3 -c "import lief; print(f'  lief {lief.__version__}')"  
python3 -c "import yara; print(f'  yara-python OK')"  
python3 -c "import pwn; print(f'  pwntools {pwn.__version__}')"  
python3 -c "from elftools import __version__; print(f'  pyelftools {__version__}')"  
yara --version 2>/dev/null && echo "  yara CLI OK"  
gdb --version 2>/dev/null | head -1  

echo ""  
echo "[+] Toolkit prêt. Activez l'environnement : source .venv/bin/activate"  
```

L'environnement virtuel Python (`.venv`) isole les dépendances du toolkit de celles du système. C'est indispensable pour la reproductibilité — le même `requirements.txt` produit le même environnement sur toute machine.

---

## Documentation

Un toolkit sans documentation est une collection de fichiers. Trois documents suffisent à rendre le toolkit exploitable par quelqu'un d'autre (y compris vous-même dans six mois).

### `README.md`

Le README est le point d'entrée. Il répond à trois questions : qu'est-ce que c'est, comment l'installer, et comment l'utiliser. Il n'a pas besoin d'être long — un paragraphe d'introduction, les commandes d'installation, et un exemple d'utilisation suffisent.

### `docs/CONVENTIONS.md`

Les conventions de code garantissent la cohérence entre les scripts. On y fixe les décisions une fois pour toutes, au lieu de les redécouvrir à chaque nouveau script :

```markdown
# Conventions du toolkit RE

## Nommage
- Scripts : `snake_case.py` (ex: `find_crypto.py`)
- Fonctions : `snake_case` (ex: `list_functions()`)
- Constantes : `UPPER_CASE` (ex: `HASH_SEED`)

## Entrées/Sorties
- Le premier argument positionnel est toujours le chemin du binaire cible
- `--output` pour spécifier un fichier de sortie (JSON par défaut)
- Sans `--output`, le résultat va sur stdout (JSON ou texte lisible)
- Code de retour : 0 = succès, 1 = problème détecté, 2 = erreur d'exécution

## Format de sortie
- JSON structuré pour tout résultat exploitable par un autre script
- Chaque rapport JSON contient un champ `metadata` (voir lib/report.py)
- Les adresses sont formatées en hexadécimal avec préfixe 0x

## Scripts Ghidra (static/ghidra/)
- Syntaxe Python 2 (Jython) — pas de f-strings, pas de type hints
- Sortie via fichier (GHIDRA_OUTPUT) — jamais sur stdout (pollué par Ghidra)
- Variable `args` pour les arguments, `currentProgram` pour le binaire
```

### `docs/CATALOG.md`

Le catalogue est un index de tous les scripts, avec pour chacun une ligne de description et un exemple d'invocation. C'est le document qu'on consulte quand on se demande « est-ce que j'ai déjà un script qui fait ça ? » :

```markdown
# Catalogue des scripts

## Triage
| Script | Description | Usage |
|---|---|---|
| `triage/triage_elf.py` | Triage complet d'un binaire ELF | `python3 triage/triage_elf.py ./binary` |
| `triage/quick_strings.py` | Strings avec section et offset | `python3 triage/quick_strings.py ./binary` |
| `triage/compare_builds.py` | Diff entre deux builds | `python3 triage/compare_builds.py v1 v2` |

## Analyse statique
| Script | Description | Usage |
|---|---|---|
| `static/list_functions.py` | Lister les fonctions (pyelftools) | `python3 static/list_functions.py ./binary` |
| `static/find_crypto.py` | Chercher des constantes crypto | `python3 static/find_crypto.py ./binary` |
...
```

Ce catalogue se maintient manuellement — chaque fois qu'on ajoute un script, on ajoute une ligne. C'est un investissement de trente secondes qui économise des heures de recherche.

---

## Versionnement avec Git

Le toolkit vit dans un dépôt Git. Chaque script ajouté ou modifié fait l'objet d'un commit avec un message qui explique *pourquoi*, pas seulement *quoi*.

### `.gitignore`

```gitignore
# Environnement Python
.venv/
__pycache__/
*.pyc
*.egg-info/

# Artefacts d'analyse (ne pas versionner les résultats)
reports/
*.json
!policies/*.json
!docs/*.json

# Binaires (trop volumineux, recompilables depuis les sources)
*.o
*.elf
*.bin

# Projets Ghidra (volumineux et spécifiques à la machine)
*.gpr
*.rep/

# Règles YARA compilées (recompilables)
*.yarc
```

Les rapports JSON et les binaires analysés ne sont pas versionnés — ils sont le résultat de l'exécution du toolkit, pas le toolkit lui-même. Les fichiers de politique (`policies/*.json`) et les règles YARA (`yara/*.yar`) sont versionnés parce qu'ils font partie de la configuration.

### Stratégie de branches

Pour un toolkit personnel, une branche `main` stable suffit. Les expérimentations (nouveau script en cours de développement, test d'une approche différente) vivent dans des branches temporaires. Le critère pour merger dans `main` : le script fonctionne sur au moins un binaire réel et est documenté dans le catalogue.

---

## Faire évoluer le toolkit

Un toolkit RE n'est jamais terminé. Il grandit au rythme des analyses. Voici les moments naturels où il s'enrichit.

**Après chaque analyse.** La question à se poser en fin d'analyse : « Quel script aurais-je aimé avoir au début ? » Si un snippet a été écrit pendant l'analyse, le nettoyer et l'intégrer au toolkit prend dix minutes. Le reporter à plus tard signifie ne jamais le faire.

**Après chaque CTF.** Les CTF sont des générateurs de scripts à haute fréquence. Beaucoup sont jetables, mais certains patterns reviennent : solver angr, hook Frida pour un type de protection, parser pour un format exotique. Extraire le pattern générique et l'ajouter comme template dans `keygen/` est un investissement rentable.

**Quand un script est utilisé deux fois.** La première utilisation d'un snippet dans un contexte différent de celui où il a été écrit est le signal qu'il mérite d'être promu dans `lib/` ou dans le répertoire approprié.

**Quand un outil externe évolue.** Une nouvelle version de Ghidra, de `lief`, ou de `pwntools` peut casser des scripts existants ou offrir de nouvelles capacités. Mettre à jour `requirements.txt` et vérifier que les scripts fonctionnent est une maintenance minimale.

Le piège à éviter est l'inverse : sur-ingéniérer le toolkit avant d'en avoir besoin. Un framework abstrait avec des classes de base, des plugins, et un système de configuration avant d'avoir écrit dix scripts est un exercice de procrastination architecturale. Le bon moment pour factoriser est quand la duplication devient douloureuse — pas avant.

---

## Checklist de maturité

Pour évaluer où en est votre toolkit, voici une grille progressive. Le niveau 1 est suffisant pour commencer. Le niveau 4 est celui d'un toolkit professionnel partagé en équipe.

**Niveau 1 — Fonctionnel.** Les scripts marchent. Ils sont dans un répertoire avec un README minimal. L'installation se fait à la main.

**Niveau 2 — Organisé.** La structure de répertoires reflète le workflow. Les dépendances sont dans `requirements.txt`. Un `setup.sh` installe tout. Le catalogue existe.

**Niveau 3 — Reproductible.** L'environnement virtuel isole les dépendances. Les conventions de code sont documentées et respectées. Le toolkit est versionné avec Git. Les scripts produisent du JSON structuré.

**Niveau 4 — Partageable.** Un collègue peut cloner le dépôt, lancer `setup.sh`, et utiliser le toolkit sans aide. Chaque script a un `--help`. Les rapports sont exploitables par des outils tiers. Le pipeline CI/CD intègre le toolkit.

L'objectif n'est pas d'atteindre le niveau 4 immédiatement. C'est de progresser d'un niveau à chaque fois que le besoin s'en fait sentir — et pas avant.

---


⏭️ [🎯 Checkpoint : écrire un script qui analyse automatiquement un répertoire de binaires et produit un rapport JSON](/35-automatisation-scripting/checkpoint.md)
