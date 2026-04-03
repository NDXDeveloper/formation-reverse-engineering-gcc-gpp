🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 10.2 — BinDiff (Google) — installation, import depuis Ghidra/IDA, lecture du résultat

> **Chapitre 10 — Diffing de binaires**  
> **Partie II — Analyse Statique**

---

## Présentation de BinDiff

BinDiff est l'outil de référence historique pour la comparaison de binaires. Développé à l'origine par Zynamics, il a été racheté par Google en 2011 et est distribué gratuitement depuis 2016. Son rôle est simple : prendre deux binaires analysés par un désassembleur (Ghidra ou IDA), comparer leurs fonctions, et produire un rapport détaillé des similitudes et des différences.

BinDiff ne désassemble pas lui-même les binaires. Il travaille à partir de fichiers d'export produits par le désassembleur — ce sont des fichiers `.BinExport` qui contiennent la représentation structurelle du binaire (fonctions, blocs de base, arêtes du CFG, mnémoniques, opérandes). Cette architecture en deux temps — export puis comparaison — lui permet de rester indépendant du désassembleur utilisé.

Le cœur de BinDiff repose sur un algorithme d'appariement en plusieurs passes, allant des correspondances les plus fiables (noms de fonctions identiques, hash de CFG exact) aux heuristiques plus souples (propagation par le graphe d'appels, correspondance partielle de blocs). Pour chaque paire de fonctions appariées, il calcule un score de similarité et peut descendre jusqu'au diff bloc-par-bloc.

---

## Installation

### Téléchargement

BinDiff est distribué sur le dépôt GitHub officiel de Google :

```
https://github.com/google/bindiff/releases
```

Téléchargez le paquet correspondant à votre distribution. Pour Ubuntu/Debian :

```bash
wget https://github.com/google/bindiff/releases/download/v8/bindiff_8_amd64.deb  
sudo dpkg -i bindiff_8_amd64.deb  
sudo apt-get install -f   # résout les dépendances manquantes si nécessaire  
```

> 💡 **Note** — Vérifiez la dernière version disponible sur la page des releases. Les numéros de version et les URLs évoluent. Au moment de la rédaction, la version 8 est la plus récente.

L'installation place plusieurs composants :

- **`bindiff`** — l'interface graphique principale (Java).  
- **`binexport2dump`** — utilitaire en ligne de commande pour inspecter les fichiers `.BinExport`.  
- **Les plugins BinExport** — extensions pour Ghidra et IDA qui permettent d'exporter les analyses au format `.BinExport`.

### Installation du plugin BinExport pour Ghidra

Le plugin BinExport pour Ghidra est inclus dans le paquet BinDiff. Pour l'installer :

1. Lancez Ghidra.  
2. Ouvrez le menu **File → Install Extensions…**  
3. Cliquez sur le **+** (Add Extension) et naviguez jusqu'au répertoire d'installation de BinDiff. Le fichier du plugin se trouve typiquement dans `/opt/bindiff/extra/ghidra/` et porte un nom du type `ghidra_BinExport.zip`.  
4. Sélectionnez-le, validez, et redémarrez Ghidra.

Après redémarrage, un nouveau menu **BinExport** apparaît dans le CodeBrowser. Vous pouvez vérifier l'installation en ouvrant n'importe quel binaire déjà analysé et en cherchant l'option **File → Export BinExport2…** (l'intitulé exact peut varier selon la version).

### Installation du plugin BinExport pour IDA Free

Si vous utilisez IDA Free (vu au chapitre 9), le plugin BinExport se trouve dans `/opt/bindiff/extra/ida/`. Copiez le fichier `.so` (Linux) ou `.dll` (Windows) correspondant à votre version d'IDA dans le répertoire `plugins/` de votre installation IDA :

```bash
cp /opt/bindiff/extra/ida/binexport12_ida.so ~/.idapro/plugins/
```

> ⚠️ **Attention** — Le numéro dans le nom du fichier (`binexport12`) correspond à la version de l'API IDA supportée. Assurez-vous de copier celui qui correspond à votre version d'IDA. Un plugin incompatible sera simplement ignoré au chargement.

### Vérification de l'installation

Pour vérifier que tout fonctionne :

```bash
# Vérifier que le binaire BinDiff est accessible
bindiff --version

# Vérifier que binexport2dump est disponible
binexport2dump --help
```

---

## Workflow complet : de l'analyse à la comparaison

Le processus de diffing avec BinDiff suit toujours le même enchaînement en trois étapes : analyser chaque binaire séparément dans le désassembleur, exporter les analyses au format BinExport, puis lancer la comparaison.

### Étape 1 — Analyser les deux binaires dans Ghidra

Commençons par l'import et l'analyse des deux versions du binaire que l'on souhaite comparer. Prenons l'exemple de nos binaires d'entraînement `keygenme_v1` et `keygenme_v2` :

1. Créez un projet Ghidra dédié (par exemple `ch10-diffing`).  
2. Importez `keygenme_v1` : **File → Import File…**, sélectionnez le binaire, acceptez les options par défaut, puis lancez l'auto-analyse quand Ghidra le propose (**Yes** sur la boîte de dialogue d'analyse). Attendez que l'analyse se termine (la barre de progression en bas à droite du CodeBrowser doit être inactive).  
3. Répétez l'opération pour `keygenme_v2`.

> 💡 **Conseil** — La qualité du diff dépend directement de la qualité de l'analyse du désassembleur. Si vous avez renommé des fonctions ou créé des types lors d'une analyse précédente, ces annotations seront prises en compte par BinExport et amélioreront l'appariement. Pour un premier diff, l'auto-analyse par défaut suffit largement.

### Étape 2 — Exporter au format BinExport

Pour chaque binaire, depuis le CodeBrowser de Ghidra :

1. Ouvrez le binaire analysé (double-clic dans le projet).  
2. Allez dans **File → Export BinExport2…** (ou **File → Export Program…** puis sélectionnez le format BinExport2 dans la liste).  
3. Choisissez un emplacement et un nom de fichier. Par convention, on garde le nom du binaire avec l'extension `.BinExport` :  
   - `keygenme_v1.BinExport`  
   - `keygenme_v2.BinExport`  
4. Validez. L'export prend quelques secondes sur un petit binaire.

Répétez l'opération pour le second binaire.

> 📝 **Depuis IDA** — Le workflow est similaire. Ouvrez le binaire dans IDA, attendez la fin de l'auto-analyse, puis utilisez **File → BinExport2…** (le plugin ajoute cette entrée au menu). Le fichier `.BinExport` produit est au même format, compatible avec BinDiff quel que soit le désassembleur source.

### Étape 3 — Lancer la comparaison avec BinDiff

Deux options s'offrent à vous : l'interface graphique ou la ligne de commande.

**Via l'interface graphique :**

```bash
bindiff
```

L'interface Java de BinDiff s'ouvre. Utilisez **Diffs → New Diff…** et sélectionnez les deux fichiers `.BinExport` (primary = version ancienne, secondary = version nouvelle, par convention). BinDiff lance la comparaison et affiche les résultats.

**Via la ligne de commande :**

```bash
bindiff keygenme_v1.BinExport keygenme_v2.BinExport
```

Cette commande produit un fichier de résultats `.BinDiff` (une base de données SQLite) dans le répertoire courant, que vous pouvez ensuite ouvrir dans l'interface graphique :

```bash
bindiff --ui keygenme_v1_vs_keygenme_v2.BinDiff
```

La ligne de commande est particulièrement utile pour automatiser des comparaisons dans un script ou un pipeline.

---

## Lire les résultats de BinDiff

L'interface de BinDiff organise les résultats en plusieurs vues complémentaires. Prenons le temps de les parcourir.

### Vue d'ensemble (*Statistics*)

La première chose que BinDiff affiche est un résumé statistique de la comparaison :

- **Nombre total de fonctions** dans chaque binaire.  
- **Fonctions appariées** (*matched*) — avec le pourcentage par rapport au total.  
- **Fonctions non appariées** (*unmatched*) — celles qui n'existent que dans l'un des deux binaires.  
- **Score de similarité global** — un nombre entre 0.0 (binaires complètement différents) et 1.0 (binaires identiques). Pour un patch de sécurité sur un gros binaire, ce score est typiquement supérieur à 0.95.

Ce résumé permet une première évaluation rapide : si le score global est de 0.99 et que seules 2 fonctions sur 500 sont marquées comme modifiées, vous savez immédiatement que le patch est chirurgical et que votre investigation se concentrera sur ces 2 fonctions.

### Table des fonctions appariées (*Matched Functions*)

C'est le cœur de l'interface. Cette table liste toutes les paires de fonctions appariées entre les deux binaires, avec pour chacune :

- **Adresse dans le binaire primaire et secondaire** — les adresses diffèrent quasi systématiquement entre deux compilations, ce qui est normal.  
- **Nom de la fonction** — si les binaires ne sont pas strippés. Sinon, BinDiff affiche `sub_XXXX` comme Ghidra ou IDA.  
- **Score de similarité** (0.0 à 1.0) — c'est la colonne la plus importante. Triez par cette colonne en ordre croissant pour faire remonter les fonctions les plus modifiées en haut de la liste.  
- **Score de confiance** — indique la fiabilité de l'appariement lui-même. Un score de confiance faible signifie que BinDiff n'est pas certain que ces deux fonctions sont réellement la même fonction.  
- **Algorithme d'appariement utilisé** — BinDiff indique quelle heuristique a permis l'appariement (hash exact, propagation par le graphe d'appels, correspondance de nom, etc.). Cette information est utile pour évaluer la fiabilité du résultat.  
- **Nombre de blocs de base et d'arêtes** dans chaque version.

**La stratégie de lecture** est simple : triez par similarité croissante. Les fonctions avec un score de 1.0 sont identiques — ignorez-les. Les fonctions avec un score inférieur à 1.0 sont celles qui ont changé, et les scores les plus bas correspondent aux changements les plus importants. Dans le cas d'un patch de sécurité, la ou les fonctions corrigées se trouvent typiquement parmi celles ayant le score le plus bas (mais pas forcément le plus bas de tous — une fonction massivement refactorisée pour des raisons cosmétiques peut avoir un score plus bas qu'une correction de sécurité d'une seule instruction).

### Table des fonctions non appariées (*Unmatched Functions*)

Deux sous-tables : les fonctions présentes uniquement dans le binaire primaire, et celles présentes uniquement dans le secondaire. Une fonction « non appariée » peut signifier :

- **Fonction réellement nouvelle ou supprimée** — ajout d'une fonctionnalité, suppression de code mort.  
- **Échec de l'appariement** — la fonction existe dans les deux binaires mais a tellement changé que BinDiff ne l'a pas reconnue. Cela arrive notamment avec les fonctions inlinées ou les fonctions réorganisées en profondeur.  
- **Artefact du compilateur** — les fonctions générées par le compilateur (trampolines, thunks, fonctions d'initialisation) peuvent varier entre deux compilations sans que cela ait de signification fonctionnelle.

Examinez toujours cette table, surtout les fonctions non appariées du côté secondaire (nouveau binaire) : une fonction ajoutée par un patch peut contenir du code de mitigation intéressant.

### Vue de comparaison des CFG (*Flow Graphs*)

C'est la vue la plus spectaculaire et la plus utile pour comprendre un changement. Quand vous double-cliquez sur une paire de fonctions modifiées dans la table, BinDiff ouvre une vue côte à côte des deux graphes de flot de contrôle, avec un code couleur :

- **Vert** — blocs de base identiques dans les deux versions.  
- **Jaune** — blocs de base appariés mais dont le contenu a été modifié (instructions ajoutées, supprimées ou changées).  
- **Rouge** — blocs de base présents uniquement dans une version (ajoutés ou supprimés).  
- **Gris** — blocs non appariés.

Les arêtes (transitions entre blocs) sont également colorées pour indiquer si la structure du flot a changé. Cette visualisation permet de localiser immédiatement le changement au sein d'une fonction.

Par exemple, si un patch ajoute une vérification de taille avant un appel à `memcpy`, vous verrez :

- Un bloc jaune là où se trouvait l'appel `memcpy` dans la version originale — le bloc existe toujours mais son contenu a été modifié.  
- Un ou deux blocs rouges représentant le nouveau chemin de vérification (le test de taille et le branchement vers un code d'erreur en cas de dépassement).  
- Les blocs verts environnants, inchangés, qui fournissent le contexte.

### Vue de comparaison des instructions (*Instruction Diff*)

En zoomant sur un bloc jaune (modifié), BinDiff peut afficher un diff au niveau des instructions assembleur, ligne par ligne. Cette vue montre exactement quelles instructions ont été ajoutées, supprimées ou modifiées au sein du bloc. C'est le niveau de détail le plus fin, utile pour comprendre précisément la nature d'un changement — par exemple, un `jl` remplacé par un `jle` (correction d'un off-by-one) ou un `cmp` dont l'opérande immédiat a changé (modification d'une limite de taille).

---

## Workflow intégré Ghidra + BinDiff

BinDiff peut aussi être utilisé directement depuis Ghidra sans passer par l'interface graphique séparée. Si le plugin est correctement installé, vous pouvez accéder à la comparaison depuis le CodeBrowser via le menu BinDiff. Ce mode intégré permet de naviguer dans les résultats du diff tout en bénéficiant du contexte complet de Ghidra (décompileur, cross-references, annotations).

Le workflow intégré est le suivant :

1. Ouvrez le binaire **primaire** dans le CodeBrowser.  
2. Exportez-le en BinExport (comme décrit précédemment).  
3. Ouvrez le binaire **secondaire** dans le CodeBrowser.  
4. Exportez-le en BinExport.  
5. Depuis le CodeBrowser (avec l'un des deux binaires ouvert), lancez la comparaison via le menu BinDiff.  
6. Les résultats apparaissent dans des fenêtres intégrées au CodeBrowser.

L'avantage principal de ce mode est de pouvoir cliquer sur une fonction modifiée dans le résultat du diff et d'être immédiatement positionné dans le Listing et le Decompiler de Ghidra, avec tout le contexte (XREF, types, commentaires) à portée de main.

---

## Utilisation en ligne de commande pour l'automatisation

Pour les workflows répétitifs ou l'intégration dans des scripts, BinDiff s'utilise entièrement en ligne de commande. Voici les commandes les plus utiles :

```bash
# Comparaison simple
bindiff primary.BinExport secondary.BinExport

# Le fichier de résultats est créé dans le répertoire courant
# Son nom est dérivé des noms des fichiers d'entrée
ls *.BinDiff
```

Le fichier `.BinDiff` est une base de données SQLite. Vous pouvez l'interroger directement avec `sqlite3` pour extraire les résultats de manière programmatique :

```bash
# Ouvrir la base de résultats
sqlite3 primary_vs_secondary.BinDiff

# Lister les fonctions modifiées (similarité < 1.0)
sqlite3 primary_vs_secondary.BinDiff \
  "SELECT address1, address2, similarity, name 
   FROM function 
   WHERE similarity < 1.0 
   ORDER BY similarity ASC;"
```

Cette approche est précieuse pour intégrer le diffing dans un pipeline d'analyse automatisé — par exemple, un script qui compare chaque nouveau build à une version de référence et alerte si des fonctions critiques ont été modifiées.

> 💡 **Astuce** — L'utilitaire `binexport2dump` permet d'inspecter le contenu d'un fichier `.BinExport` sans ouvrir BinDiff, ce qui est utile pour le débogage ou pour vérifier qu'un export s'est bien passé :  
> ```bash  
> binexport2dump keygenme_v1.BinExport  
> ```

---

## Limites de BinDiff

BinDiff est un outil mature et fiable, mais il a ses limites qu'il est important de connaître :

- **Pas de décompilation intégrée** — BinDiff compare au niveau de l'assembleur et des blocs de base. Pour voir le pseudo-code, il faut revenir dans Ghidra ou IDA. C'est un outil de navigation et de localisation, pas un outil d'analyse à part entière.  
- **Dépendance au désassembleur** — la qualité du diff dépend de la qualité de l'analyse initiale. Si Ghidra n'a pas correctement identifié les limites d'une fonction, BinDiff ne pourra pas l'apparier correctement. Des erreurs de désassemblage se propagent dans le diff.  
- **Binaires fortement obfusqués** — les techniques d'obfuscation de flux de contrôle (control flow flattening, vu au chapitre 19) perturbent considérablement les algorithmes d'appariement de BinDiff, car elles transforment radicalement la structure du CFG.  
- **Changements massifs de compilateur** — passer de GCC à Clang, ou changer de version majeure du compilateur avec des niveaux d'optimisation différents, peut modifier suffisamment la structure du code généré pour dégrader la qualité de l'appariement.  
- **Interface graphique datée** — l'interface Java de BinDiff est fonctionnelle mais spartiate par rapport aux standards actuels. Pour une expérience plus confortable, le mode intégré dans Ghidra ou l'alternative Diaphora (section 10.3) peuvent être préférables.

---

## En résumé

BinDiff est l'outil de diffing le plus établi de l'écosystème. Son workflow en trois temps — analyser, exporter, comparer — s'intègre naturellement dans un processus de RE existant basé sur Ghidra ou IDA. Sa force réside dans la robustesse de ses algorithmes d'appariement, la clarté de sa visualisation des CFG, et la possibilité d'automatiser les comparaisons via la ligne de commande et l'accès SQLite aux résultats.

Le réflexe à développer : face à deux versions d'un binaire, **exportez et diffez avant de reverser**. Les quelques minutes passées à configurer le diff vous économiseront des heures d'analyse manuelle en vous pointant directement vers les fonctions qui comptent.

---


⏭️ [Diaphora — plugin Ghidra/IDA open source pour le diffing](/10-diffing-binaires/03-diaphora.md)
