🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.9 — Ghidra en mode headless pour le traitement batch

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Qu'est-ce que le mode headless ?

Jusqu'ici, toutes les interactions avec Ghidra passaient par l'interface graphique : le Project Manager pour gérer les projets, le CodeBrowser pour naviguer et annoter. Le **mode headless** (sans interface graphique) permet d'exécuter Ghidra entièrement en ligne de commande. Pas de fenêtre, pas de clic, pas d'affichage — uniquement un processus qui importe des binaires, lance l'analyse automatique, exécute des scripts et produit des résultats sur la sortie standard ou dans des fichiers.

Ce mode est conçu pour les scénarios où l'intervention humaine n'est pas nécessaire ou pas souhaitable :

- **Analyse batch** — analyser automatiquement 50, 100 ou 1000 binaires d'un seul coup, par exemple toutes les versions d'un firmware, tous les exécutables d'un répertoire suspect, ou toutes les variantes d'optimisation de vos binaires d'entraînement.  
- **Intégration CI/CD** — intégrer l'analyse Ghidra dans un pipeline d'intégration continue pour auditer automatiquement chaque build d'un projet (détection de régression binaire, vérification de protections, extraction de métriques).  
- **Extraction de données** — exécuter un script qui extrait des informations structurées (listes de fonctions, chaînes, constantes crypto, signatures) et les écrit dans un fichier JSON, CSV ou base de données.  
- **Pré-analyse** — préparer un projet Ghidra avec l'analyse automatique complétée avant de l'ouvrir dans le CodeBrowser. Cela économise du temps d'attente lors de l'ouverture interactive, surtout pour les gros binaires.  
- **Serveur d'analyse** — déployer Ghidra sur une machine serveur (sans écran) pour traiter des requêtes d'analyse à la demande.

---

## L'outil `analyzeHeadless`

Le point d'entrée du mode headless est le script `analyzeHeadless`, situé dans le répertoire `support/` de l'installation Ghidra :

```
/opt/ghidra/support/analyzeHeadless
```

C'est un script shell (Linux/macOS) ou batch (Windows) qui configure la JVM et lance Ghidra en mode non-interactif. Pour simplifier l'usage, créez un alias :

```bash
alias analyzeHeadless='/opt/ghidra/support/analyzeHeadless'
```

### Syntaxe générale

```bash
analyzeHeadless <project_dir> <project_name> [options]
```

Les deux premiers arguments sont obligatoires :

- `<project_dir>` — le répertoire qui contient (ou contiendra) le projet Ghidra. Si le répertoire n'existe pas, Ghidra le crée.  
- `<project_name>` — le nom du projet (correspondant au fichier `.gpr`). Si le projet n'existe pas, Ghidra le crée automatiquement.

Tout le reste est contrôlé par des options.

---

## Commandes fondamentales

### Importer et analyser un binaire

La commande la plus basique importe un binaire dans un projet, lance l'analyse automatique et termine :

```bash
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -import binaries/ch08-keygenme/keygenme_O0
```

Ghidra :

1. Crée le projet `HeadlessProject` dans `~/ghidra-projects/` s'il n'existe pas.  
2. Importe le fichier `keygenme_O0`, détecte automatiquement le format (ELF) et l'architecture (x86-64).  
3. Lance l'analyse automatique complète (tous les analyseurs par défaut).  
4. Sauvegarde le résultat dans la base de données du projet.  
5. Termine le processus.

La sortie sur le terminal affiche les messages de progression : import réussi, analyseurs exécutés, nombre de fonctions détectées, temps écoulé.

### Importer plusieurs binaires

Vous pouvez spécifier plusieurs fichiers ou un répertoire entier :

```bash
# Plusieurs fichiers
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -import keygenme_O0 keygenme_O2 keygenme_O3

# Un répertoire entier (tous les fichiers seront importés)
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -import binaries/ch08-keygenme/
```

Quand vous importez un répertoire, Ghidra tente d'importer chaque fichier. Les fichiers non reconnus (Makefiles, fichiers source `.c`, README) sont ignorés avec un message d'avertissement.

### Travailler sur un projet existant (sans import)

Si le projet existe déjà et contient des binaires analysés, vous pouvez exécuter un script sans réimporter :

```bash
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -process -noanalysis \
    -postScript mon_script.py
```

L'option `-process` sans argument traite **tous** les fichiers du projet. Pour cibler un binaire spécifique :

```bash
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -process keygenme_O0 -noanalysis \
    -postScript mon_script.py
```

L'option `-noanalysis` évite de relancer l'analyse automatique sur un binaire déjà analysé. Sans cette option, Ghidra relancerait l'analyse complète, ce qui est inutile si le binaire a déjà été traité.

---

## Exécuter des scripts en mode headless

### Les options `-preScript` et `-postScript`

Le mode headless prend tout son intérêt quand il exécute des scripts. Deux points d'insertion sont disponibles :

**`-preScript <script> [args...]`** — Exécute le script **avant** l'analyse automatique. Utile pour configurer des options d'analyse, définir des types préalables, ou modifier des paramètres avant que les analyseurs ne se lancent.

**`-postScript <script> [args...]`** — Exécute le script **après** l'analyse automatique. C'est le cas le plus courant : l'analyse a été réalisée, le script exploite les résultats (extraction de données, renommage, génération de rapport).

Vous pouvez enchaîner plusieurs scripts :

```bash
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -import keygenme_O0 \
    -postScript rename_network_funcs.py \
    -postScript export_functions_json.py "/tmp/report.json"
```

Les scripts sont exécutés dans l'ordre spécifié. Les arguments supplémentaires après le nom du script sont passés au script et accessibles via `getScriptArgs()` dans l'API Flat.

### Passer des arguments aux scripts

Dans le script Python :

```python
args = getScriptArgs()  
if len(args) > 0:  
    output_path = args[0]
else:
    output_path = "/tmp/ghidra_export.json"

println("Export vers : {}".format(output_path))
```

En ligne de commande :

```bash
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -process keygenme_O0 -noanalysis \
    -postScript export_functions_json.py "/tmp/keygenme_report.json"
```

### Localisation des scripts

Ghidra recherche les scripts dans les répertoires configurés. Par défaut :

- `~/ghidra_scripts/` — vos scripts personnels.  
- Les répertoires de scripts intégrés de Ghidra (dans `Ghidra/Features/*/ghidra_scripts/`).

Vous pouvez ajouter des répertoires supplémentaires via l'option `-scriptPath` :

```bash
analyzeHeadless ~/ghidra-projects HeadlessProject \
    -import keygenme_O0 \
    -scriptPath /home/user/re-formation/scripts/ \
    -postScript mon_script_custom.py
```

---

## Options importantes

### Contrôle de l'analyse

| Option | Rôle |  
|---|---|  
| `-noanalysis` | Ne pas lancer l'analyse automatique. Utile si vous voulez uniquement importer ou exécuter un script sur un binaire déjà analysé. |  
| `-analysisTimeoutPerFile <seconds>` | Limite le temps d'analyse par fichier. Indispensable pour le batch : un binaire malformé ou très complexe ne bloquera pas la chaîne entière. |

### Contrôle de l'import

| Option | Rôle |  
|---|---|  
| `-import <path>` | Importe le fichier ou le répertoire spécifié. |  
| `-overwrite` | Écrase un binaire déjà présent dans le projet au lieu de l'ignorer. Utile quand vous réimportez une version recompilée. |  
| `-recursive` | Importe récursivement les sous-répertoires. |  
| `-loader <loader>` | Force un loader spécifique (par exemple `ElfLoader` ou `BinaryLoader`). Rarement nécessaire — la détection automatique est fiable. |  
| `-processor <language_id>` | Force l'architecture. Par exemple `-processor x86:LE:64:default` pour forcer x86-64. Utile pour les fichiers raw sans headers. |

### Contrôle de la sortie

| Option | Rôle |  
|---|---|  
| `-deleteProject` | Supprime le projet après traitement. Utile quand le projet est temporaire et que seule la sortie du script compte. |  
| `-log <logfile>` | Redirige les logs Ghidra vers un fichier. Recommandé pour le batch afin de garder une trace des erreurs. |  
| `-scriptlog <logfile>` | Redirige spécifiquement la sortie des scripts (`println`) vers un fichier séparé des logs système de Ghidra. |

---

## Exemple complet : analyse batch d'un répertoire

Voici un scénario complet qui illustre la puissance du mode headless. L'objectif est d'analyser tous les binaires du répertoire `binaries/ch08-keygenme/`, extraire pour chacun la liste des fonctions avec leurs tailles et leurs cross-references, et écrire le résultat dans un fichier JSON.

### Le script d'extraction

Créez le fichier `~/ghidra_scripts/batch_export.py` :

```python
# Export des fonctions en JSON pour analyse batch
# @category Export
# @author Formation RE

import json  
import os  

args = getScriptArgs()  
output_dir = args[0] if len(args) > 0 else "/tmp/ghidra_batch"  

# Creer le repertoire de sortie si necessaire
if not os.path.exists(output_dir):
    os.makedirs(output_dir)

program_name = currentProgram.getName()  
output_file = os.path.join(output_dir, program_name + ".json")  

functions_data = []  
fm = currentProgram.getFunctionManager()  

for func in fm.getFunctions(True):
    monitor.checkCancelled()
    
    entry = func.getEntryPoint().toString()
    name = func.getName()
    size = func.getBody().getNumAddresses()
    
    # Compter les XREFs entrants (appels vers cette fonction)
    refs_to = getReferencesTo(func.getEntryPoint())
    call_count = 0
    for ref in refs_to:
        if ref.getReferenceType().isCall():
            call_count += 1
    
    # Lister les fonctions appelees
    called = []
    for called_func in func.getCalledFunctions(monitor):
        called.append(called_func.getName())
    
    functions_data.append({
        "name": name,
        "address": entry,
        "size": int(size),
        "callers": call_count,
        "calls": called,
        "is_thunk": func.isThunk(),
        "is_external": func.isExternal()
    })

# Metadata du programme
report = {
    "program": program_name,
    "format": currentProgram.getExecutableFormat(),
    "language": currentProgram.getLanguageID().toString(),
    "compiler": currentProgram.getCompilerSpec().getCompilerSpecID().toString(),
    "function_count": len(functions_data),
    "functions": functions_data
}

with open(output_file, "w") as f:
    f.write(json.dumps(report, indent=2))

println("Export termine: {} fonctions -> {}".format(len(functions_data), output_file))
```

### La commande batch

```bash
analyzeHeadless ~/ghidra-projects BatchAnalysis \
    -import binaries/ch08-keygenme/ \
    -recursive \
    -overwrite \
    -analysisTimeoutPerFile 300 \
    -postScript batch_export.py "/tmp/keygenme_reports" \
    -log /tmp/ghidra_batch.log \
    -scriptlog /tmp/ghidra_script.log
```

Cette commande :

1. Crée le projet `BatchAnalysis`.  
2. Importe récursivement tous les binaires de `binaries/ch08-keygenme/`.  
3. Analyse chaque binaire avec un timeout de 5 minutes.  
4. Exécute `batch_export.py` sur chaque binaire analysé, en passant le répertoire de sortie en argument.  
5. Écrit les logs dans des fichiers séparés.

Le résultat est un fichier JSON par binaire dans `/tmp/keygenme_reports/` :

```
/tmp/keygenme_reports/
├── keygenme_O0.json
├── keygenme_O0_strip.json
├── keygenme_O2.json
├── keygenme_O2_strip.json
└── keygenme_O3.json
```

Chaque fichier contient les métadonnées du programme et la liste complète de ses fonctions avec leurs attributs. Ces fichiers peuvent ensuite être exploités par un script Python externe pour comparer les binaires, produire des statistiques, ou alimenter un tableau de bord.

---

## Intégration dans un pipeline

### Script shell d'orchestration

Pour un usage régulier, encapsulez la commande `analyzeHeadless` dans un script shell qui gère les paramètres, les répertoires et le nettoyage :

```bash
#!/bin/bash
# analyze_batch.sh — Analyse batch avec Ghidra headless

GHIDRA_HOME="/opt/ghidra"  
HEADLESS="${GHIDRA_HOME}/support/analyzeHeadless"  
PROJECT_DIR="/tmp/ghidra_batch_$$"  
PROJECT_NAME="batch"  
INPUT_DIR="${1:?Usage: $0 <input_dir> <output_dir>}"  
OUTPUT_DIR="${2:?Usage: $0 <input_dir> <output_dir>}"  
TIMEOUT=300  

mkdir -p "${OUTPUT_DIR}"

echo "[*] Analyse de ${INPUT_DIR}..."
"${HEADLESS}" "${PROJECT_DIR}" "${PROJECT_NAME}" \
    -import "${INPUT_DIR}" \
    -recursive \
    -overwrite \
    -analysisTimeoutPerFile ${TIMEOUT} \
    -postScript batch_export.py "${OUTPUT_DIR}" \
    -log "${OUTPUT_DIR}/ghidra.log" \
    -scriptlog "${OUTPUT_DIR}/script.log" \
    -deleteProject \
    2>&1 | tail -20

echo "[*] Resultats dans ${OUTPUT_DIR}"  
echo "[*] $(ls -1 "${OUTPUT_DIR}"/*.json 2>/dev/null | wc -l) fichier(s) genere(s)"  
```

Usage :

```bash
chmod +x analyze_batch.sh
./analyze_batch.sh binaries/ch08-keygenme/ /tmp/reports/
```

L'option `-deleteProject` supprime le projet temporaire après traitement — seuls les fichiers JSON de sortie sont conservés. Le PID (`$$`) dans le nom du répertoire de projet évite les conflits si plusieurs instances s'exécutent en parallèle.

### Intégration CI/CD

Dans un pipeline d'intégration continue (Jenkins, GitLab CI, GitHub Actions), le mode headless permet de vérifier automatiquement des propriétés du binaire à chaque build :

- **Vérification des protections** — un script post-analyse vérifie que le binaire a bien PIE, NX, canary et Full RELRO activés, et échoue le build si une protection manque.  
- **Détection de régression** — comparaison du nombre de fonctions, des symboles exportés, ou des constantes crypto entre deux versions du binaire.  
- **Extraction de signatures** — génération automatique de signatures YARA à partir des constantes et patterns détectés dans le binaire.

Le Chapitre 35 (Automatisation et scripting) détaillera un pipeline CI/CD complet avec Ghidra headless.

---

## Performance et ressources

### Consommation mémoire

Le mode headless consomme autant de mémoire que l'interface graphique — voire plus si vous traitez de gros binaires séquentiellement sans décharger les précédents. Configurez la mémoire JVM dans `support/analyzeHeadless` ou via la variable d'environnement `MAXMEM` :

```bash
MAXMEM=8G analyzeHeadless ~/projects BatchProject -import big_binary
```

Pour un batch de petits binaires (< 1 Mo chacun), 4 Go suffisent. Pour des binaires volumineux (> 10 Mo) ou du C++ avec STL, prévoyez 8 Go ou plus.

### Temps d'exécution

Le temps d'analyse dépend de la taille et de la complexité du binaire :

| Type de binaire | Taille typique | Temps d'analyse approx. |  
|---|---|---|  
| Petit binaire C (`keygenme`) | 10-50 Ko | 5-15 secondes |  
| Application C moyenne | 100 Ko - 1 Mo | 30 secondes - 2 minutes |  
| Application C++ avec STL | 1-10 Mo | 2-10 minutes |  
| Gros binaire (serveur, jeu) | 10-100 Mo | 10-60 minutes |

Ces temps sont indicatifs et varient fortement selon le CPU, la mémoire disponible et la complexité du code (densité des cross-references, nombre de fonctions, profondeur des templates C++).

L'option `-analysisTimeoutPerFile` est essentielle pour le batch : elle évite qu'un binaire pathologique (très obfusqué, format inhabituel) ne bloque l'ensemble du traitement. Une valeur de 300 secondes (5 minutes) est un bon compromis pour des binaires de taille modérée.

### Parallélisation

`analyzeHeadless` traite les binaires **séquentiellement** au sein d'une même invocation. Pour paralléliser, lancez plusieurs instances sur des sous-ensembles de binaires dans des **projets séparés** (Ghidra verrouille le projet — deux instances ne peuvent pas écrire dans le même projet simultanément) :

```bash
# Traiter les lots en parallele
analyzeHeadless /tmp/proj_1 batch -import lot_1/ -postScript export.py &  
analyzeHeadless /tmp/proj_2 batch -import lot_2/ -postScript export.py &  
analyzeHeadless /tmp/proj_3 batch -import lot_3/ -postScript export.py &  
wait  
echo "Tous les lots sont termines"  
```

Chaque instance consomme sa propre mémoire JVM. Avec 4 instances à 4 Go chacune, prévoyez 16 Go de RAM disponible.

---

## Dépannage courant

**« Java not found » ou « Unsupported Java version »** — Vérifiez que `JAVA_HOME` pointe vers un JDK 17+ et que `java -version` confirme la bonne version. Le script `analyzeHeadless` utilise la même configuration Java que l'interface graphique.

**« Project is locked »** — Un autre processus Ghidra (interface graphique ou autre instance headless) a verrouillé le projet. Fermez l'autre instance ou utilisez un nom de projet différent. En cas de crash, un fichier `.lock` résiduel peut persister dans le répertoire `.rep/` — supprimez-le manuellement.

**Script introuvable** — Vérifiez que le script est dans `~/ghidra_scripts/` ou dans un répertoire référencé par `-scriptPath`. Le nom doit correspondre exactement, extension comprise.

**Timeout dépassé** — Le binaire est trop complexe pour le timeout configuré. Augmentez `-analysisTimeoutPerFile` ou analysez ce binaire séparément avec un timeout plus long.

**OutOfMemoryError** — La JVM manque de mémoire. Augmentez `MAXMEM` comme décrit plus haut. Pour les très gros binaires, 16 Go peuvent être nécessaires.

---

## Résumé

Le mode headless est l'extension naturelle du scripting Ghidra vers l'automatisation complète. L'outil `analyzeHeadless` permet d'importer, d'analyser et de scripter des binaires entièrement en ligne de commande, sans interface graphique. Combiné aux scripts Python ou Java développés en section 8.8, il ouvre la voie à l'analyse batch de collections de binaires, à l'intégration dans des pipelines CI/CD, et à la production systématique de rapports structurés. Les options de timeout, de logging et de gestion de projet rendent le processus robuste pour une utilisation en production.

C'est la dernière section technique de ce chapitre. Le checkpoint qui suit vous permettra de valider l'ensemble des compétences acquises en important un binaire C++ dans Ghidra et en reconstruisant sa hiérarchie de classes.

---


⏭️ [🎯 Checkpoint : importer `ch20-oop` dans Ghidra, reconstruire la hiérarchie de classes](/08-ghidra/checkpoint.md)
