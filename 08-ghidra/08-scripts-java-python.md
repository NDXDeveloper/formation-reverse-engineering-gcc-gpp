🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.8 — Scripts Ghidra en Java/Python pour automatiser l'analyse

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Pourquoi scripter Ghidra ?

Les sections précédentes vous ont montré comment annoter un binaire manuellement : renommer une fonction, créer un type, suivre une cross-reference. Ce travail est efficace à petite échelle — quelques dizaines de fonctions, une poignée de structures. Mais les binaires réels contiennent souvent des centaines voire des milliers de fonctions, des dizaines de vtables, des centaines de chaînes de caractères à classifier. Les tâches répétitives consomment un temps considérable et sont sujettes à l'erreur humaine.

Le scripting est la réponse à ce problème de passage à l'échelle. Ghidra expose la quasi-totalité de ses fonctionnalités via une **API programmatique** accessible en Java et en Python (via Jython, une implémentation Python 2.7 en Java). Un script peut faire tout ce que vous faites manuellement dans l'interface, et plus encore : itérer sur toutes les fonctions du programme, filtrer par critère, renommer en masse, créer des structures automatiquement, extraire des données, produire des rapports.

Les cas d'usage courants du scripting sont :

- **Renommage en masse** — renommer toutes les fonctions qui correspondent à un pattern (par exemple, préfixer les fonctions qui appellent `malloc` avec `alloc_`).  
- **Extraction d'information** — lister toutes les chaînes référencées par une famille de fonctions, extraire tous les appels à une fonction spécifique avec leurs arguments.  
- **Détection de patterns** — chercher des séquences d'instructions caractéristiques (constantes cryptographiques, idiomes de compilateur, patterns d'obfuscation).  
- **Reconstruction automatique** — créer des structures pour chaque vtable détectée, appliquer des types à des familles de fonctions similaires.  
- **Génération de rapports** — produire un résumé JSON ou texte de l'analyse pour documentation ou intégration dans un pipeline.

---

## L'environnement de scripting

### Le Script Manager

Le point d'entrée pour le scripting dans le CodeBrowser est le **Script Manager**, accessible via **Window → Script Manager** ou l'icône de script (feuille avec une flèche verte) dans la barre d'outils.

Le Script Manager affiche :

- **Une arborescence de catégories** à gauche, organisant les scripts par thème (Analysis, Data, Functions, Search, etc.).  
- **La liste des scripts** au centre, avec leur nom, leur description et leur catégorie.  
- **Une barre d'outils** avec les boutons pour exécuter, éditer, créer et gérer les scripts.

Ghidra est livré avec **plusieurs centaines de scripts pré-intégrés** couvrant une grande variété de tâches. Avant d'écrire votre propre script, parcourez les scripts existants — il y a de bonnes chances que quelqu'un ait déjà résolu un problème similaire au vôtre. Les scripts pré-intégrés servent aussi d'exemples de code pour apprendre l'API.

### Créer un nouveau script

1. Dans le Script Manager, cliquez sur le bouton **New Script** (icône de page blanche avec un `+`).  
2. Choisissez le langage : **Java** ou **Python**.  
3. Donnez un nom au script (par exemple `ListCryptoConstants.java` ou `rename_network_funcs.py`).  
4. Ghidra ouvre l'éditeur de script intégré avec un squelette de base.

Les scripts utilisateur sont stockés par défaut dans `~/ghidra_scripts/`. Vous pouvez ajouter des répertoires de scripts supplémentaires via **Edit → Script Directories** dans le Script Manager.

### Exécuter un script

Sélectionnez le script dans le Script Manager et cliquez sur **Run** (bouton ▶ vert), ou double-cliquez directement sur le script. La sortie (appels à `println` en Java, `print` en Python) s'affiche dans la **Console** en bas du CodeBrowser.

Vous pouvez aussi assigner un raccourci clavier à un script fréquemment utilisé : clic droit sur le script → **Assign Key Binding**.

---

## L'API Flat : le point d'entrée universel

Que vous écriviez en Java ou en Python, vos scripts héritent d'une classe de base — `GhidraScript` — qui expose un ensemble de méthodes de haut niveau appelé l'**API Flat** (Flat API). Ces méthodes fournissent un accès simplifié aux fonctionnalités de Ghidra sans nécessiter de manipuler directement les classes internes du framework.

Les méthodes de l'API Flat sont disponibles directement dans le corps du script (sans préfixe d'objet en Java, et via les variables globales en Python). Voici les plus importantes, regroupées par domaine.

### Navigation et adresses

| Méthode | Rôle |  
|---|---|  
| `currentProgram` | Référence vers le programme actuellement ouvert dans le CodeBrowser |  
| `currentAddress` | L'adresse où se trouve le curseur dans le Listing |  
| `currentFunction` | La fonction contenant le curseur |  
| `toAddr(String)` | Convertit une chaîne hexadécimale en objet `Address` (ex : `toAddr("00401200")`) |  
| `getAddressFactory()` | Accès à la fabrique d'adresses du programme |

### Fonctions

| Méthode | Rôle |  
|---|---|  
| `getFirstFunction()` | Retourne la première fonction du programme (par adresse) |  
| `getFunctionAfter(Function)` | Retourne la fonction suivante dans l'espace d'adresses |  
| `getFunctionAt(Address)` | Retourne la fonction à l'adresse exacte donnée |  
| `getFunctionContaining(Address)` | Retourne la fonction qui contient l'adresse donnée |  
| `getGlobalFunctions(String)` | Recherche les fonctions par nom |

### Mémoire et données

| Méthode | Rôle |  
|---|---|  
| `getByte(Address)` | Lit un octet à l'adresse donnée |  
| `getInt(Address)` | Lit un entier 32 bits |  
| `getLong(Address)` | Lit un entier 64 bits |  
| `getBytes(Address, int)` | Lit un tableau d'octets de la taille spécifiée |  
| `getDataAt(Address)` | Retourne l'élément de donnée défini à cette adresse |

### Interaction utilisateur

| Méthode | Rôle |  
|---|---|  
| `println(String)` | Affiche un message dans la Console |  
| `askString(String, String)` | Affiche un dialogue demandant une saisie texte à l'utilisateur |  
| `askAddress(String, String)` | Demande une adresse |  
| `askChoice(String, String, List, T)` | Demande un choix parmi une liste d'options |  
| `askYesNo(String, String)` | Demande une confirmation oui/non |

### Monitoring

| Méthode | Rôle |  
|---|---|  
| `monitor` | Référence vers le moniteur de progression du script |  
| `monitor.setMessage(String)` | Met à jour le message de progression |  
| `monitor.checkCancelled()` | Vérifie si l'utilisateur a annulé le script (lève une exception si oui) |

---

## Écrire des scripts en Python (Jython)

### Environnement Python dans Ghidra

Ghidra utilise **Jython** — une implémentation de Python 2.7 qui s'exécute sur la JVM. Cela signifie que la syntaxe est celle de Python 2 (pas Python 3), mais vous avez accès à l'intégralité de l'API Java de Ghidra depuis Python. En pratique, les différences syntaxiques les plus notables sont :

- `print` est une instruction, pas une fonction : `print "Hello"` (pas `print("Hello")`, bien que cette forme fonctionne aussi en Python 2).  
- Les chaînes sont par défaut en ASCII/Latin-1, pas en Unicode.  
- Certaines bibliothèques Python 3 ne sont pas disponibles.

> ⚠️ **Ghidra 11.x et Pyhidra** — Les versions récentes de Ghidra introduisent progressivement le support de **CPython 3** via le module Pyhidra (basé sur JPype). Cette fonctionnalité est encore en évolution. Dans ce tutoriel, nous utilisons Jython (Python 2.7) qui est la méthode stable et documentée. Les principes et l'API restent identiques — seules les conventions syntaxiques Python 2 vs 3 diffèrent.

### Squelette d'un script Python

```python
# Lister toutes les fonctions du programme et leur nombre de cross-references
# @category Analysis
# @author Formation RE

from ghidra.program.model.symbol import RefType

func = getFirstFunction()  
while func is not None:  
    name = func.getName()
    entry = func.getEntryPoint()
    ref_count = len(getReferencesTo(entry))
    println("{} @ {} — {} XREFs".format(name, entry, ref_count))
    func = getFunctionAfter(func)
```

Les commentaires spéciaux en en-tête (`@category`, `@author`, `@keybinding`, `@description`) sont des **métadonnées** que Ghidra utilise pour organiser le script dans le Script Manager. `@category` détermine dans quelle catégorie le script apparaît.

### Itérer sur les fonctions

Le pattern le plus courant dans un script Ghidra est l'itération sur toutes les fonctions du programme :

```python
func = getFirstFunction()  
while func is not None:  
    # ... traitement de func ...
    func = getFunctionAfter(func)
```

Ce pattern utilise l'API Flat. Une alternative plus concise utilise le Function Manager :

```python
fm = currentProgram.getFunctionManager()  
for func in fm.getFunctions(True):  # True = forward iteration  
    # ... traitement de func ...
```

### Accéder aux instructions d'une fonction

Pour parcourir les instructions assembleur d'une fonction :

```python
listing = currentProgram.getListing()  
func = getFunctionAt(toAddr("00401200"))  
if func is not None:  
    body = func.getBody()  # AddressSetView couvrant le corps de la fonction
    instr = listing.getInstructionAt(body.getMinAddress())
    while instr is not None and body.contains(instr.getAddress()):
        println("{} {}".format(instr.getMnemonicString(), instr))
        instr = instr.getNext()
```

### Accéder aux cross-references

```python
# Trouver toutes les fonctions qui appellent strcmp
refs = getReferencesTo(toAddr("00401080"))  # adresse de strcmp dans PLT  
for ref in refs:  
    caller_addr = ref.getFromAddress()
    caller_func = getFunctionContaining(caller_addr)
    if caller_func is not None:
        println("strcmp appelee depuis {} @ {}".format(
            caller_func.getName(), caller_addr))
```

### Renommer une fonction par script

```python
func = getFunctionAt(toAddr("004011a0"))  
if func is not None:  
    func.setName("validate_key", ghidra.program.model.symbol.SourceType.USER_DEFINED)
    println("Fonction renommee en validate_key")
```

Le paramètre `SourceType.USER_DEFINED` indique à Ghidra que le nom a été défini par l'utilisateur (par opposition à un nom auto-généré ou importé des symboles).

### Ajouter un commentaire par script

```python
from ghidra.program.model.listing import CodeUnit

addr = toAddr("00401200")  
cu = listing.getCodeUnitAt(addr)  
cu.setComment(CodeUnit.EOL_COMMENT, "Debut de la verification de licence")  
```

Les constantes de type de commentaire sont `EOL_COMMENT`, `PRE_COMMENT`, `POST_COMMENT`, `PLATE_COMMENT` et `REPEATABLE_COMMENT`.

---

## Écrire des scripts en Java

### Squelette d'un script Java

```java
// Lister les fonctions importées
// @category Analysis
// @author Formation RE

import ghidra.app.script.GhidraScript;  
import ghidra.program.model.listing.Function;  
import ghidra.program.model.listing.FunctionIterator;  
import ghidra.program.model.symbol.SymbolTable;  

public class ListImports extends GhidraScript {

    @Override
    protected void run() throws Exception {
        FunctionIterator functions = currentProgram
            .getFunctionManager()
            .getExternalFunctions();
        
        while (functions.hasNext()) {
            Function func = functions.next();
            println("Import: " + func.getName() 
                    + " from " + func.getExternalLocation()
                                     .getLibraryName());
        }
    }
}
```

Le script Java est une classe qui étend `GhidraScript` et implémente la méthode `run()`. Le nom du fichier doit correspondre au nom de la classe (ici `ListImports.java`). Ghidra compile automatiquement le script avant de l'exécuter — vous n'avez pas besoin de le compiler manuellement.

### Java vs Python : quel langage choisir ?

Les deux langages accèdent à la même API. Le choix est une question de préférence et de contexte :

**Python (Jython)** est préférable pour le prototypage rapide, les scripts courts et les analyses exploratoires. La syntaxe est plus concise, il n'y a pas de compilation explicite, et la boucle d'essai-erreur est plus rapide. C'est le choix recommandé pour la plupart des scripts de ce tutoriel.

**Java** est préférable pour les scripts complexes et performants : gros volumes de données, logique métier élaborée, intégration avec des bibliothèques Java tierces. Java offre aussi un meilleur support de l'autocomplétion si vous utilisez un IDE externe (Eclipse, IntelliJ) avec le projet Ghidra configuré comme dépendance.

En pratique, commencez en Python pour tout, et migrez vers Java uniquement si vous rencontrez des limitations de performance ou de fonctionnalité.

---

## Exemples de scripts utiles

### Lister tous les appels à une fonction avec leurs arguments

Ce script cherche tous les sites d'appel à une fonction donnée et tente d'extraire la valeur du premier argument (souvent une chaîne passée dans `RDI`) :

```python
# Lister les appels a puts() avec la chaine passee en argument
# @category Analysis

target_name = askString("Fonction cible", "Nom de la fonction a tracer :")  
fm = currentProgram.getFunctionManager()  
listing = currentProgram.getListing()  
rm = currentProgram.getReferenceManager()  

# Trouver la fonction cible (peut etre un thunk/PLT)
targets = getGlobalFunctions(target_name)  
if not targets:  
    println("Fonction '{}' non trouvee".format(target_name))
else:
    for target in targets:
        refs = getReferencesTo(target.getEntryPoint())
        for ref in refs:
            if ref.getReferenceType().isCall():
                call_addr = ref.getFromAddress()
                caller = getFunctionContaining(call_addr)
                caller_name = caller.getName() if caller else "???"
                
                # Chercher l'instruction precedente qui charge RDI
                instr = listing.getInstructionAt(call_addr)
                prev = instr.getPrevious()
                arg_info = ""
                # Remonter quelques instructions pour trouver le LEA/MOV dans RDI
                for i in range(5):
                    if prev is None:
                        break
                    mnemonic = prev.getMnemonicString()
                    if "LEA" in mnemonic or "MOV" in mnemonic:
                        operands = prev.toString()
                        if "RDI" in operands.upper():
                            arg_info = " | arg1 hint: {}".format(operands)
                            break
                    prev = prev.getPrevious()
                
                println("[{}] CALL {} @ {}{}".format(
                    caller_name, target_name, call_addr, arg_info))
```

Ce script illustre plusieurs patterns importants : interaction utilisateur (`askString`), recherche de fonctions par nom, parcours des références filtrées par type, et remontée dans les instructions pour extraire un contexte.

### Renommer les fonctions selon un préfixe basé sur leurs imports

Ce script préfixe automatiquement les fonctions qui appellent des fonctions réseau :

```python
# Prefixer les fonctions qui font des appels reseau
# @category Analysis

network_funcs = ["socket", "connect", "bind", "listen", 
                 "accept", "send", "recv", "close"]
fm = currentProgram.getFunctionManager()

for net_name in network_funcs:
    targets = getGlobalFunctions(net_name)
    for target in targets:
        refs = getReferencesTo(target.getEntryPoint())
        for ref in refs:
            if ref.getReferenceType().isCall():
                caller = getFunctionContaining(ref.getFromAddress())
                if caller is None:
                    continue
                name = caller.getName()
                if name.startswith("FUN_") and not name.startswith("net_"):
                    new_name = "net_" + name
                    caller.setName(new_name, 
                        ghidra.program.model.symbol.SourceType.USER_DEFINED)
                    println("Renamed {} -> {}".format(name, new_name))
```

### Chercher des constantes cryptographiques

Ce script recherche des constantes connues (ici les premiers octets de la S-box AES) dans l'espace mémoire du programme :

```python
# Chercher la S-box AES dans le binaire
# @category Search

aes_sbox_start = [0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5]

memory = currentProgram.getMemory()  
search_bytes = bytes(bytearray(aes_sbox_start))  
addr = memory.findBytes(memory.getMinAddress(), search_bytes, None, True, monitor)  

if addr is not None:
    println("S-box AES trouvee a l'adresse : {}".format(addr))
    # Trouver les fonctions qui referencent cette adresse
    refs = getReferencesTo(addr)
    for ref in refs:
        func = getFunctionContaining(ref.getFromAddress())
        if func:
            println("  Referencee par : {} @ {}".format(
                func.getName(), ref.getFromAddress()))
else:
    println("S-box AES non trouvee")
```

### Exporter un résumé JSON des fonctions

```python
# Exporter un resume JSON de toutes les fonctions
# @category Export

import json

output = []  
fm = currentProgram.getFunctionManager()  

for func in fm.getFunctions(True):
    entry = func.getEntryPoint().toString()
    name = func.getName()
    size = func.getBody().getNumAddresses()
    xref_count = len(getReferencesTo(func.getEntryPoint()))
    called = []
    for f_called in func.getCalledFunctions(monitor):
        called.append(f_called.getName())
    
    output.append({
        "name": name,
        "address": entry,
        "size": int(size),
        "xref_count": xref_count,
        "calls": called
    })

# Ecrire le fichier
filepath = askString("Export", "Chemin du fichier JSON :")  
with open(filepath, "w") as f:  
    f.write(json.dumps(output, indent=2))
println("Export termine : {} fonctions".format(len(output)))
```

Ce type de script est particulièrement utile pour alimenter des outils externes (notebooks Jupyter, scripts d'analyse statistique, outils de visualisation de graphes) avec les données extraites de Ghidra.

---

## L'API interne : aller plus loin

L'API Flat couvre les besoins courants, mais pour des tâches avancées, vous devrez accéder aux **classes internes** de Ghidra. Les packages les plus importants sont :

### `ghidra.program.model`

Le cœur du modèle de données. Les sous-packages principaux :

- `ghidra.program.model.listing` — `Function`, `Instruction`, `Data`, `CodeUnit`, `Listing`. Accès au contenu désassemblé.  
- `ghidra.program.model.symbol` — `Symbol`, `SymbolTable`, `Namespace`, `Reference`, `RefType`, `SourceType`. Gestion des symboles, des namespaces et des références.  
- `ghidra.program.model.address` — `Address`, `AddressSet`, `AddressSpace`. Manipulation des adresses.  
- `ghidra.program.model.mem` — `Memory`, `MemoryBlock`. Accès à la mémoire brute du programme.  
- `ghidra.program.model.data` — `DataType`, `Structure`, `Enum`, `Union`, `Pointer`. Manipulation des types.  
- `ghidra.program.model.pcode` — `PcodeOp`, `Varnode`. Accès à la représentation intermédiaire P-Code utilisée par le décompileur.

### `ghidra.app.decompiler`

Accès programmatique au décompileur. Vous pouvez invoquer le décompileur sur une fonction et récupérer le pseudo-code C sous forme structurée :

```python
from ghidra.app.decompiler import DecompInterface

decomp = DecompInterface()  
decomp.openProgram(currentProgram)  

func = getFunctionAt(toAddr("00401200"))  
results = decomp.decompileFunction(func, 30, monitor)  # timeout 30s  

if results.decompileCompleted():
    code = results.getDecompiledFunction().getC()
    println(code)
else:
    println("Echec de la decompilation")

decomp.dispose()
```

Cet accès est puissant pour produire des exports de pseudo-code en masse ou pour analyser programmatiquement la structure du code décompilé.

### Documentation de l'API

La documentation Javadoc complète de l'API Ghidra est accessible :

- **Dans Ghidra** : menu **Help → Ghidra API Help** dans le CodeBrowser. La Javadoc s'ouvre dans un navigateur.  
- **En ligne** : sur le dépôt GitHub de Ghidra, les fichiers de documentation sont générés à chaque release.  
- **Par l'exploration** : dans le Script Manager, les scripts pré-intégrés sont commentés et constituent une base d'exemples pratiques. Utilisez **Edit** sur un script existant pour lire son code source.

> 💡 **Conseil d'apprentissage** — La meilleure façon d'apprendre l'API est par l'exploration. Ouvrez la console Python de Ghidra (**Window → Python**) et testez les appels interactivement. Tapez `currentProgram.` puis explorez les méthodes disponibles par autocomplétion (touche Tab). Cette boucle interactive est beaucoup plus efficace que la lecture exhaustive de la Javadoc.

---

## La console Python interactive

Au-delà des scripts sauvegardés, Ghidra offre une **console Python interactive** accessible via **Window → Python**. C'est un REPL (Read-Eval-Print Loop) qui vous permet de taper des commandes Python une par une et de voir immédiatement le résultat.

La console est idéale pour :

- **Explorer l'API** — testez des méthodes, inspectez des objets, vérifiez des hypothèses.  
- **Analyses ponctuelles** — « combien de fonctions appellent `malloc` ? » se répond en trois lignes dans la console, sans créer de fichier script.  
- **Prototypage** — testez la logique de votre script avant de la formaliser dans un fichier.

Toutes les variables de l'API Flat sont disponibles dans la console : `currentProgram`, `currentAddress`, `currentFunction`, `getFirstFunction()`, etc.

---

## Bonnes pratiques de scripting

**Utilisez `monitor.checkCancelled()`** dans les boucles longues. Cela permet à l'utilisateur d'interrompre le script proprement via le bouton d'annulation dans l'interface. Sans ce check, un script qui itère sur des milliers de fonctions ne peut pas être interrompu autrement qu'en tuant Ghidra.

```python
for func in fm.getFunctions(True):
    monitor.checkCancelled()  # leve CancelledException si annule
    monitor.setMessage("Analyse de {}".format(func.getName()))
    # ... traitement ...
```

**Encapsulez les modifications dans des transactions.** Ghidra utilise un système de transactions pour garantir l'intégrité de la base de données. Les scripts lancés depuis le Script Manager gèrent automatiquement les transactions. Si vous utilisez l'API depuis un contexte différent (plugin, console interactive pour les écritures), vous devrez gérer les transactions manuellement :

```python
txid = currentProgram.startTransaction("Mon script")  
try:  
    # ... modifications ...
    currentProgram.endTransaction(txid, True)  # True = commit
except:
    currentProgram.endTransaction(txid, False)  # False = rollback
```

**Préférez `println()` à `print`.** La fonction `println()` de l'API Flat écrit dans la Console du CodeBrowser, visible dans l'interface graphique. L'instruction `print` de Python écrit dans la sortie standard du processus Java, qui n'est pas visible dans l'interface (sauf si vous avez lancé Ghidra depuis un terminal).

**Testez sur un petit échantillon avant de lancer en masse.** Ajoutez un compteur et un `break` temporaire pour valider votre script sur les 10 premières fonctions avant de le lancer sur les 5000 fonctions du programme.

**Versionnez vos scripts.** Gardez vos scripts dans un répertoire versionné avec Git. Ils constituent un capital réutilisable d'un projet d'analyse à l'autre. Le répertoire `~/ghidra_scripts/` est un bon candidat pour en faire un dépôt Git.

---

## Résumé

Le scripting est le levier qui transforme Ghidra d'un outil interactif en une plateforme d'analyse automatisée. L'API Flat fournit un accès simplifié aux fonctions, instructions, données, références et types du programme, aussi bien en Python (Jython) qu'en Java. Les scripts pré-intégrés offrent une bibliothèque d'exemples immédiatement exploitables, et la console Python interactive permet l'exploration et le prototypage rapide. Pour les besoins avancés, l'API interne donne accès au décompileur, au modèle P-Code et à la totalité des structures de données de Ghidra.

Le Chapitre 35 (Automatisation et scripting) reviendra sur le scripting Ghidra dans un contexte d'automatisation à grande échelle, incluant le mode headless que nous abordons dans la section suivante.

---


⏭️ [Ghidra en mode headless pour le traitement batch](/08-ghidra/09-headless-mode-batch.md)
