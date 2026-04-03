🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 9.4 — Scripting avec r2pipe (Python)

> 📘 **Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja**  
> Section précédente : [9.3 — `r2` : commandes essentielles](/09-ida-radare2-binja/03-r2-commandes-essentielles.md)

---

## Pourquoi scripter l'analyse ?

La section 9.3 a montré comment mener une analyse complète dans le shell interactif de `r2`. Cette approche fonctionne bien pour une cible unique, mais elle atteint ses limites dès que la tâche devient répétitive ou volumineuse : analyser 50 binaires d'une même campagne malware, extraire systématiquement les chaînes et imports de chaque sample, chercher un pattern spécifique dans toutes les fonctions d'un gros binaire, ou encore produire un rapport structuré à partir des résultats d'analyse.

C'est exactement le rôle de `r2pipe` : une interface de programmation qui permet de piloter Radare2 depuis un langage de haut niveau. Vous écrivez du Python (ou du JavaScript, du Go, du Rust — mais Python est de loin le plus utilisé), et chaque appel envoie une commande `r2` au moteur, récupère la sortie, et vous la renvoie sous forme de chaîne de caractères ou de structure JSON directement exploitable.

L'avantage par rapport au scripting shell pur (`r2 -qc '...' | grep | awk`) est considérable : vous disposez de toute la puissance de Python pour le parsing, la logique conditionnelle, la génération de rapports, l'interaction avec d'autres outils (bases de données, APIs, frameworks comme `pwntools` ou `angr`), et la gestion d'erreurs.

## Installation de r2pipe

`r2pipe` est un module Python léger qui communique avec `r2` via un pipe ou un socket. Il ne contient pas le moteur d'analyse lui-même — Radare2 doit être installé séparément (cf. section 9.2).

```bash
pip install r2pipe
```

Vérification rapide :

```bash
python3 -c "import r2pipe; print(r2pipe.version())"
```

> 💡 Si vous utilisez un environnement virtuel Python (recommandé), activez-le avant l'installation. Le module `r2pipe` n'a pas de dépendance lourde : il pèse quelques dizaines de kilo-octets.

## Premiers pas : ouvrir un binaire et envoyer des commandes

### Connexion à un binaire

```python
import r2pipe

# Ouvrir un binaire — lance une instance r2 en arrière-plan
r2 = r2pipe.open("keygenme_O2_strip")

# Lancer l'analyse approfondie
r2.cmd("aaa")

# Envoyer une commande et récupérer la sortie texte
output = r2.cmd("afl")  
print(output)  

# Fermer la session
r2.quit()
```

La méthode `r2pipe.open()` accepte plusieurs types de cibles :

- Un chemin vers un fichier binaire — lance une nouvelle instance `r2` en arrière-plan.  
- `"-"` — se connecte à la session `r2` parente si le script est lancé depuis le shell `r2` avec la commande `#!pipe python script.py`.  
- Une URL `http://host:port` — se connecte à une instance `r2` distante lancée avec `r2 -c 'h' binaire` (serveur HTTP intégré).

### `cmd()` vs `cmdj()` : texte brut ou JSON

La distinction entre ces deux méthodes est fondamentale dans tout script `r2pipe`.

**`cmd(commande)`** envoie la commande et retourne la sortie brute sous forme de chaîne Python. C'est l'équivalent exact de taper la commande dans le shell `r2` et de copier-coller le résultat. La sortie est du texte tabulaire lisible par un humain, mais pénible à parser programmatiquement.

```python
# Sortie texte — il faudrait parser chaque ligne manuellement
text = r2.cmd("afl")
# "0x00401050    1     46 entry0\n0x00401080    4     31 sym.deregister..."
```

**`cmdj(commande)`** envoie la commande avec le suffixe `j` (JSON) et parse automatiquement la sortie en objet Python (liste ou dictionnaire). C'est la méthode à privilégier systématiquement quand une sortie structurée est disponible.

```python
# Sortie JSON — directement exploitable
functions = r2.cmdj("aflj")
# [{"offset": 4198480, "name": "entry0", "size": 46, "nbbs": 1, ...}, ...]

for fn in functions:
    print(f"{fn['name']:40s}  addr=0x{fn['offset']:08x}  size={fn['size']}")
```

> ⚠️ Toutes les commandes `r2` ne supportent pas le suffixe `j`. Si `cmdj()` reçoit une sortie non-JSON, elle lèvera une exception ou retournera `None`. Dans le doute, vérifiez dans le shell interactif que la commande avec `j` produit bien du JSON avant de l'utiliser dans un script.

## Anatomie d'un script d'analyse

Voyons un script complet qui effectue un triage automatique de notre binaire fil rouge. Ce script reproduit en quelques secondes le workflow manuel que nous avons déroulé dans les sections précédentes.

```python
#!/usr/bin/env python3
"""
triage_r2.py — Triage automatique d'un binaire ELF avec r2pipe.  
Usage : python3 triage_r2.py <chemin_binaire>  
"""

import sys  
import json  
import r2pipe  


def triage(binary_path):
    r2 = r2pipe.open(binary_path)
    r2.cmd("aaa")  # Analyse approfondie

    report = {}

    # ── 1. Informations générales ──
    info = r2.cmdj("iIj")
    report["file"] = binary_path
    report["arch"] = info.get("arch", "unknown")
    report["bits"] = info.get("bits", 0)
    report["os"] = info.get("os", "unknown")
    report["stripped"] = info.get("stripped", False)
    report["canary"] = info.get("canary", False)
    report["nx"] = info.get("nx", False)
    report["pic"] = info.get("pic", False)
    report["relro"] = info.get("relro", "none")

    # ── 2. Sections ──
    sections = r2.cmdj("iSj")
    report["sections"] = [
        {"name": s["name"], "size": s["size"], "perm": s["perm"]}
        for s in sections
        if s.get("size", 0) > 0
    ]

    # ── 3. Imports ──
    imports = r2.cmdj("iij")
    report["imports"] = [imp["name"] for imp in imports] if imports else []

    # ── 4. Fonctions détectées ──
    functions = r2.cmdj("aflj")
    report["function_count"] = len(functions) if functions else 0

    # Séparer fonctions applicatives et fonctions d'infrastructure
    infra_prefixes = (
        "sym.deregister_tm", "sym.register_tm", "sym.frame_dummy",
        "sym.__libc_csu", "sym.__do_global", "entry", "sym._init",
        "sym._fini", "sym.imp."
    )
    app_functions = [
        {"name": fn["name"], "addr": hex(fn["offset"]), "size": fn["size"]}
        for fn in (functions or [])
        if not fn["name"].startswith(infra_prefixes)
    ]
    report["app_functions"] = app_functions

    # ── 5. Chaînes intéressantes ──
    strings = r2.cmdj("izj")
    report["strings"] = [
        {"value": s["string"], "addr": hex(s["vaddr"]), "section": s["section"]}
        for s in (strings or [])
        if len(s.get("string", "")) > 3  # ignorer les chaînes très courtes
    ]

    # ── 6. Résumé des appels dans main ──
    r2.cmd("s main")
    summary = r2.cmd("pds")
    report["main_summary"] = summary.strip().split("\n") if summary else []

    r2.quit()
    return report


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <binary>", file=sys.stderr)
        sys.exit(1)

    result = triage(sys.argv[1])
    print(json.dumps(result, indent=2, ensure_ascii=False))
```

### Exécution

```bash
$ python3 triage_r2.py keygenme_O2_strip
{
  "file": "keygenme_O2_strip",
  "arch": "x86",
  "bits": 64,
  "os": "linux",
  "stripped": true,
  "canary": false,
  "nx": true,
  "pic": false,
  "relro": "partial",
  "sections": [
    {"name": ".text", "size": 418, "perm": "-r-x"},
    {"name": ".rodata", "size": 45, "perm": "-r--"},
    ...
  ],
  "imports": ["puts", "strcmp", "__isoc99_scanf"],
  "function_count": 9,
  "app_functions": [
    {"name": "sym.transform_key", "addr": "0x401120", "size": 63},
    {"name": "main", "addr": "0x401160", "size": 98}
  ],
  "strings": [
    {"value": "Enter key: ", "addr": "0x402000", "section": ".rodata"},
    {"value": "Access granted", "addr": "0x40200e", "section": ".rodata"},
    {"value": "Wrong key", "addr": "0x40201d", "section": ".rodata"}
  ],
  "main_summary": [
    "0x0040116b call sym.imp.puts           ; \"Enter key: \"",
    "0x00401181 call sym.imp.__isoc99_scanf",
    "0x00401193 call sym.imp.strcmp",
    "0x004011a3 call sym.imp.puts           ; \"Access granted\"",
    "0x004011b5 call sym.imp.puts           ; \"Wrong key\""
  ]
}
```

En une vingtaine de lignes de logique, le script produit un rapport JSON complet et structuré. Ce rapport peut être stocké dans une base de données, comparé avec d'autres samples, ou intégré dans un pipeline d'analyse plus large.

### Points clés du script

Quelques observations sur les choix de conception, transposables à n'importe quel script `r2pipe` :

Le script utilise `cmdj()` partout où c'est possible et `cmd()` uniquement pour `pds` qui ne dispose pas d'un mode JSON fiable. C'est la règle d'or : toujours préférer la sortie structurée.

Le filtrage des fonctions d'infrastructure GCC (`deregister_tm_clones`, `register_tm_clones`, etc.) est fait côté Python plutôt que dans `r2`. C'est plus lisible et plus maintenable que de construire des commandes `r2` avec des filtres complexes.

L'appel `r2.cmd("s main")` déplace le seek dans la session `r2` avant d'appeler `pds`. Chaque commande `r2` est exécutée dans le contexte de la session courante — le seek, les flags, les commentaires persistent d'une commande à l'autre au sein d'une même session `r2pipe.open()`.

La fermeture explicite avec `r2.quit()` est importante pour libérer le processus `r2` en arrière-plan. En cas de boucle sur de nombreux binaires, oublier ce `quit()` peut saturer les ressources système.

## Cas d'usage courants

### Extraire les cross-references d'une chaîne

```python
import r2pipe

r2 = r2pipe.open("keygenme_O2_strip")  
r2.cmd("aaa")  

# Trouver l'adresse de la chaîne "Access granted"
strings = r2.cmdj("izj")  
target = next(s for s in strings if "granted" in s.get("string", ""))  

# Récupérer les XREF vers cette chaîne
r2.cmd(f"s {target['vaddr']}")  
xrefs = r2.cmdj("axtj")  

for xref in xrefs:
    print(f"Référencée par : {xref.get('fcn_name', '?')} "
          f"@ 0x{xref['from']:x} "
          f"(type: {xref['type']})")

r2.quit()
```

Ce pattern — trouver une chaîne, remonter les XREF, identifier la fonction appelante — est le fondement de l'analyse automatisée. Il est directement transposable à des scénarios plus complexes : trouver toutes les fonctions qui appellent `recv()` dans un binaire réseau, identifier toutes les fonctions qui accèdent à une variable globale suspecte, etc.

### Lister les appels de fonctions importées dans chaque fonction

```python
import r2pipe

r2 = r2pipe.open("keygenme_O2_strip")  
r2.cmd("aaa")  

functions = r2.cmdj("aflj") or []

for fn in functions:
    r2.cmd(f"s {fn['offset']}")
    summary = r2.cmd("pds").strip()

    if not summary:
        continue

    calls = [line for line in summary.split("\n") if "call" in line]
    if calls:
        print(f"\n── {fn['name']} (0x{fn['offset']:x}) ──")
        for c in calls:
            print(f"  {c.strip()}")

r2.quit()
```

Ce script produit une cartographie des dépendances de chaque fonction : quelles fonctions importées elle appelle, et avec quels arguments (les chaînes sont annotées par `pds`). Sur un gros binaire, cette cartographie est un accélérateur d'analyse considérable : elle permet de repérer en quelques secondes les fonctions « intéressantes » (celles qui appellent des fonctions de chiffrement, de réseau, de fichier…) parmi des centaines de fonctions anonymes.

### Analyse batch de plusieurs binaires

```python
import r2pipe  
import json  
import glob  


def extract_info(path):
    r2 = r2pipe.open(path)
    r2.cmd("aaa")

    info = r2.cmdj("iIj") or {}
    imports = r2.cmdj("iij") or []
    strings = r2.cmdj("izj") or []

    result = {
        "file": path,
        "stripped": info.get("stripped", False),
        "nx": info.get("nx", False),
        "imports": [i["name"] for i in imports],
        "string_count": len(strings),
        "crypto_hints": [
            s["string"] for s in strings
            if any(kw in s.get("string", "").lower()
                   for kw in ("aes", "key", "encrypt", "decrypt", "cipher"))
        ]
    }

    r2.quit()
    return result


# Analyser tous les binaires d'un répertoire
results = []  
for binary in glob.glob("binaries/ch09-keygenme/keygenme_*"):  
    print(f"Analyse de {binary}...", flush=True)
    results.append(extract_info(binary))

# Rapport global
print(json.dumps(results, indent=2))
```

Ce script analyse toutes les variantes du keygenme (`_O0`, `_O2`, `_O3`, `_strip`, `_O2_strip`) et produit un rapport comparatif. On pourrait le compléter avec des métriques : nombre de fonctions, taille du `.text`, ratio de fonctions reconnues par FLIRT, etc. C'est le germe du script `batch_analyze.py` mentionné dans le dossier `scripts/` du dépôt, et un avant-goût du chapitre 35 sur l'automatisation.

### Décompiler une fonction et sauvegarder le résultat

```python
import r2pipe

r2 = r2pipe.open("keygenme_O2_strip")  
r2.cmd("aaa")  
r2.cmd("s main")  

# Décompilation via le plugin Ghidra (nécessite r2ghidra installé)
decompiled = r2.cmd("pdg")

if decompiled:
    with open("main_decompiled.c", "w") as f:
        f.write(decompiled)
    print("Pseudo-code sauvegardé dans main_decompiled.c")
else:
    print("Décompileur non disponible — installez r2ghidra : r2pm -i r2ghidra")

r2.quit()
```

## Lancer un script depuis le shell `r2`

Au lieu d'exécuter le script Python depuis le terminal système, vous pouvez le lancer depuis une session `r2` interactive :

```
[0x00401050]> #!pipe python3 mon_script.py
```

Dans ce mode, le script peut se connecter à la session `r2` parente en utilisant `r2pipe.open("-")` au lieu d'un chemin de fichier. L'avantage est que le script hérite de l'analyse déjà effectuée et de toutes les annotations (renommages, commentaires, flags) de la session en cours.

```python
import r2pipe

# Se connecte à la session r2 parente
r2 = r2pipe.open("-")

# Pas besoin de r2.cmd("aaa") — l'analyse est déjà faite
functions = r2.cmdj("aflj")  
print(f"Nombre de fonctions : {len(functions)}")  

# Pas de r2.quit() — la session parente continue après le script
```

Ce mécanisme est particulièrement utile pour les scripts utilitaires courts : un script qui renomme automatiquement les fonctions selon une heuristique, un script qui colorise les blocs de base selon une propriété calculée, etc.

## Bonnes pratiques

### Gérer les erreurs et les sorties vides

Les commandes `r2` peuvent échouer silencieusement ou retourner des résultats inattendus, surtout sur des binaires malformés ou obfusqués. Un script robuste doit toujours vérifier les retours.

```python
# Mauvais — crash si cmdj retourne None
functions = r2.cmdj("aflj")  
for fn in functions:  # TypeError si functions est None  
    ...

# Bon — valeur par défaut et vérification
functions = r2.cmdj("aflj") or []  
for fn in functions:  
    name = fn.get("name", "unknown")
    offset = fn.get("offset", 0)
    ...
```

### Préférer `cmdj()` à du parsing manuel

Il peut être tentant de parser la sortie texte de `cmd()` avec des expressions régulières. C'est fragile : le format texte de `r2` change entre les versions, les alignements de colonnes varient selon la largeur du terminal, et les caractères spéciaux dans les noms de symboles peuvent casser une regex. La sortie JSON est stable, typée, et autoportante.

### Réutiliser la session

Ouvrir et fermer une session `r2` a un coût non négligeable : le processus `r2` est lancé, le binaire est chargé, l'analyse est exécutée. Si vous devez envoyer de nombreuses commandes au même binaire, ouvrez une seule session et réutilisez-la.

```python
# Mauvais — ouvre et ferme r2 pour chaque fonction
for addr in addresses:
    r2 = r2pipe.open("binary")
    r2.cmd("aaa")
    r2.cmd(f"s {addr}")
    result = r2.cmd("pdf")
    r2.quit()

# Bon — une seule session pour tout
r2 = r2pipe.open("binary")  
r2.cmd("aaa")  
for addr in addresses:  
    r2.cmd(f"s {addr}")
    result = r2.cmd("pdf")
r2.quit()
```

### Sauvegarder un projet `r2`

Après un travail d'annotation scripté (renommages, commentaires, flags), vous pouvez sauvegarder l'état de la session dans un projet `r2` réouvrable ultérieurement :

```python
r2.cmd("Ps mon_projet")  # Sauvegarder le projet
# Plus tard : r2 -p mon_projet pour le rouvrir
```

## `r2pipe` vs les APIs des autres outils

Il est utile de situer `r2pipe` par rapport aux interfaces de scripting des désassembleurs concurrents, car vous serez amené à choisir l'un ou l'autre selon le contexte.

**`r2pipe` (Radare2)** communique par échange de commandes texte/JSON avec un processus `r2` externe. C'est un modèle simple et découplé : votre script Python est un programme indépendant, et `r2` est un serveur d'analyse. L'avantage est la légèreté et la portabilité. L'inconvénient est que chaque commande est une chaîne de texte à construire et une sortie à parser — il n'y a pas d'API objet riche avec autocomplétion et typage fort.

**Ghidra scripting (Java/Python)** fonctionne à l'intérieur du processus Ghidra. Les scripts ont accès à un modèle objet complet : `Program`, `Function`, `Instruction`, `DataType`… avec des méthodes typées et documentées. C'est plus puissant pour les manipulations complexes (reconstruire des types, annoter des structures), mais les scripts ne tournent que dans l'environnement Ghidra. Nous l'avons couvert au chapitre 8.8.

**IDAPython (IDA Pro/Home)** offre un modèle similaire à Ghidra : accès direct aux objets internes d'IDA depuis Python. C'est historiquement le standard de l'industrie pour le scripting RE, mais il requiert une licence IDA Pro ou Home — il n'est pas pleinement disponible dans IDA Free.

**Binary Ninja API (Python)** est considérée par beaucoup comme la mieux conçue des quatre, avec un modèle objet propre et des abstractions bien pensées (l'IL multi-niveau BNIL). Mais elle nécessite une licence Binary Ninja commerciale pour un usage local.

En résumé : `r2pipe` est le meilleur choix quand vous avez besoin d'un scripting gratuit, léger, découplé et exécutable dans un pipeline Unix. Pour de l'analyse programmatique plus profonde avec un modèle objet riche, le scripting Ghidra est l'alternative gratuite la plus complète.

---


⏭️ [Binary Ninja Cloud (version gratuite) — prise en main rapide](/09-ida-radare2-binja/05-binary-ninja-cloud.md)
