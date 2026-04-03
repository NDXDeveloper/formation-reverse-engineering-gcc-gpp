🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 10.3 — Diaphora — plugin Ghidra/IDA open source pour le diffing

> **Chapitre 10 — Diffing de binaires**  
> **Partie II — Analyse Statique**

---

## Présentation de Diaphora

Diaphora (du grec *διαφορά*, « différence ») est un outil de diffing de binaires open source créé par Joxean Koret, un chercheur en sécurité espagnol reconnu dans la communauté RE. Distribué sous licence GPL, il se présente sous forme de plugin pour IDA et, plus récemment, pour Ghidra. C'est l'alternative open source la plus complète à BinDiff.

Ce qui distingue Diaphora de BinDiff n'est pas tant la finalité — les deux outils servent à comparer des binaires — que l'approche technique et la richesse des heuristiques. Là où BinDiff repose principalement sur la structure des graphes de flot de contrôle, Diaphora combine un nombre beaucoup plus large de critères de comparaison. Parmi les plus notables :

- **Hash du pseudo-code** — Diaphora exploite le décompileur intégré (celui de Ghidra ou le Hex-Rays d'IDA) pour produire un hash du pseudo-code de chaque fonction. Deux fonctions dont le pseudo-code est identique sont appariées avec une confiance très élevée, même si le code assembleur sous-jacent diffère (changement de registres, réordonnancement d'instructions par l'optimiseur).  
- **Hash ASM et hash partiel** — au-delà du pseudo-code, Diaphora calcule des hashes sur les mnémoniques assembleur, les constantes, et des sous-ensembles de ces éléments, ce qui permet des correspondances partielles quand le code a été légèrement modifié.  
- **Correspondance par constantes et chaînes** — les constantes numériques (magic numbers, tailles de buffer) et les chaînes de caractères référencées sont utilisées comme empreintes digitales.  
- **Topologie du graphe d'appels** — comme BinDiff, Diaphora utilise la position d'une fonction dans le graphe d'appels global (qui l'appelle, qui elle appelle) pour renforcer ou infirmer un appariement.  
- **Heuristiques spécialisées** — Diaphora dispose de passes dédiées à des cas particuliers comme les petites fonctions (wrappers, thunks), les fonctions qui ne diffèrent que par leurs constantes, ou encore les fonctions dont seule la gestion d'erreurs a changé.

Le résultat de cette combinaison est un outil souvent plus précis que BinDiff sur les binaires fortement optimisés ou partiellement obfusqués, au prix d'un temps d'analyse un peu plus long.

---

## Installation

### Pour Ghidra

Diaphora pour Ghidra est distribué via le dépôt GitHub officiel du projet :

```
https://github.com/joxeankoret/diaphora
```

L'installation se fait en tant que script Ghidra (et non en tant qu'extension packagée) :

1. Clonez le dépôt :
   ```bash
   cd ~/tools
   git clone https://github.com/joxeankoret/diaphora.git
   ```

2. Dans Ghidra, ouvrez le **Script Manager** : depuis le CodeBrowser, menu **Window → Script Manager**.

3. Ajoutez le répertoire du dépôt cloné aux chemins de scripts : cliquez sur l'icône **Manage Script Directories** (le dossier avec une clé), puis ajoutez le chemin vers le répertoire `diaphora/` que vous venez de cloner.

4. Dans le Script Manager, recherchez `diaphora`. Vous devriez voir apparaître le script `Diaphora.java` (ou `diaphora_ghidra.py` selon la version). Double-cliquez pour l'exécuter, ou assignez-lui un raccourci clavier pour un accès rapide.

> 💡 **Dépendances** — Diaphora utilise SQLite pour stocker ses résultats. Sur la plupart des distributions Linux, la bibliothèque SQLite est déjà présente. Si vous rencontrez des erreurs, vérifiez que le paquet `sqlite3` est installé (`sudo apt install sqlite3`).

### Pour IDA

L'installation pour IDA est plus directe. Diaphora a été conçu initialement pour IDA et c'est la plateforme la plus mature :

1. Clonez le même dépôt (ou téléchargez l'archive ZIP depuis GitHub).  
2. Copiez le fichier `diaphora.py` et le répertoire associé dans le dossier `plugins/` d'IDA, ou exécutez-le comme un script via **File → Script File…**  
3. Diaphora apparaît ensuite dans le menu **Edit → Plugins → Diaphora**.

### Vérification

Quel que soit le désassembleur, la façon la plus simple de vérifier l'installation est d'ouvrir un petit binaire analysé et de lancer un export Diaphora (décrit dans la section suivante). Si l'export produit un fichier `.sqlite` sans erreur, l'installation est fonctionnelle.

---

## Workflow de comparaison

Le workflow de Diaphora est conceptuellement similaire à celui de BinDiff — exporter les deux binaires, puis comparer les exports — mais le processus se déroule entièrement à l'intérieur du désassembleur, sans application séparée.

### Étape 1 — Exporter le binaire primaire

1. Ouvrez `keygenme_v1` dans Ghidra et attendez la fin de l'auto-analyse.  
2. Lancez le script Diaphora depuis le Script Manager.  
3. Diaphora affiche une boîte de dialogue vous demandant le chemin du fichier de sortie. Choisissez un emplacement et un nom explicite :
   ```
   /home/user/diffing/keygenme_v1.sqlite
   ```
4. Une série d'options apparaît. Pour une première utilisation, les options par défaut conviennent. Parmi les options notables :  
   - **Use decompiler** — activez cette option (cochée par défaut dans la plupart des versions). Elle demande à Diaphora de décompiler chaque fonction et d'en stocker le pseudo-code dans la base. C'est plus lent mais produit de bien meilleurs résultats.  
   - **Exclude library functions** — permet d'ignorer les fonctions identifiées comme provenant de bibliothèques standard (libc, libstdc++…), ce qui réduit le bruit dans les résultats.  
5. Validez. Diaphora parcourt toutes les fonctions du binaire, calcule les hashes, décompile si demandé, et stocke le tout dans le fichier SQLite. Une barre de progression indique l'avancement.

### Étape 2 — Exporter le binaire secondaire

Fermez `keygenme_v1` dans Ghidra (ou ouvrez une nouvelle instance du CodeBrowser), ouvrez `keygenme_v2`, et répétez la même opération d'export :

```
/home/user/diffing/keygenme_v2.sqlite
```

> 💡 **Conseil pratique** — Exportez toujours vers des fichiers dont le nom permet d'identifier clairement la version. Quand on accumule les exports au fil des analyses, des noms comme `export1.sqlite` deviennent vite inutilisables.

### Étape 3 — Lancer la comparaison

1. Ouvrez l'un des deux binaires dans Ghidra (le choix n'a pas d'importance, mais par convention on ouvre le secondaire — la version patchée).  
2. Lancez le script Diaphora.  
3. Cette fois, au lieu de simplement exporter, indiquez le fichier SQLite de l'**autre** version comme base de comparaison. Diaphora détecte qu'il s'agit d'un diff et non d'un simple export.  
4. La comparaison s'exécute. Selon la taille des binaires et le nombre d'heuristiques activées, cela peut prendre de quelques secondes (petit binaire) à plusieurs minutes (binaire de plusieurs mégaoctets avec décompilation activée).

Le résultat s'affiche directement dans une interface intégrée au désassembleur.

---

## Lecture des résultats

L'interface de résultats de Diaphora est organisée en onglets, chacun correspondant à une catégorie de correspondances. Cette organisation est l'une des forces de l'outil : au lieu d'une liste unique de fonctions triées par similarité, Diaphora sépare les résultats par niveau de confiance.

### Onglet *Best matches*

Ce sont les paires de fonctions appariées avec la confiance la plus élevée. Les fonctions de cet onglet ont été reconnues par des heuristiques fiables — hash de pseudo-code identique, hash assembleur identique, correspondance de nom exacte. En pratique, la quasi-totalité de ces correspondances sont correctes.

Pour chaque paire, Diaphora affiche :

- Les adresses dans les deux binaires.  
- Le nom de la fonction (si disponible).  
- Le **ratio de similarité** — un nombre entre 0.0 et 1.0, calculé à partir de la combinaison des heuristiques.  
- La **description de l'heuristique** ayant produit l'appariement — par exemple « pseudo-code hash », « bytes hash », « same name ».

Les fonctions avec un ratio de 1.0 sont identiques. Celles avec un ratio légèrement inférieur à 1.0 méritent une inspection : elles ont été reconnues comme correspondantes mais présentent des différences.

### Onglet *Partial matches*

Ici se trouvent les paires appariées avec une confiance moyenne. L'heuristique a trouvé suffisamment de points communs pour proposer un appariement, mais le score de similarité est sensiblement inférieur à 1.0. C'est souvent dans cet onglet que se trouvent les fonctions les plus intéressantes lors d'une analyse de patch : les fonctions corrigées par le patch présentent assez de points communs avec leur version originale pour être appariées, mais assez de différences pour ne pas être classées comme « best match ».

### Onglet *Unreliable matches*

Les appariements de cet onglet sont les plus spéculatifs. Diaphora a trouvé quelques indices de correspondance, mais pas assez pour garantir l'appariement. Consultez cet onglet avec un regard critique : certaines correspondances sont correctes, d'autres sont des faux positifs. L'information sur l'heuristique utilisée est précieuse ici — un appariement par propagation dans le graphe d'appels est plus fiable qu'un appariement basé uniquement sur le nombre de blocs de base.

### Onglet *Unmatched*

Comme dans BinDiff, cet onglet liste les fonctions qui n'ont pas pu être appariées. Deux sous-catégories : les fonctions présentes uniquement dans le binaire primaire et celles uniquement dans le secondaire. Les mêmes mises en garde qu'en section 10.2 s'appliquent : une fonction non appariée n'est pas nécessairement nouvelle — elle peut simplement avoir trop changé pour être reconnue.

### Vue diff du pseudo-code

C'est la fonctionnalité phare de Diaphora et son avantage le plus net par rapport à BinDiff. Quand vous sélectionnez une paire de fonctions dans n'importe quel onglet, Diaphora peut afficher un **diff côte à côte du pseudo-code décompilé** — pas seulement de l'assembleur. Cette vue utilise un format similaire à un diff textuel classique (lignes ajoutées en vert, supprimées en rouge, modifiées en jaune), mais appliqué au pseudo-code C produit par le décompileur.

L'intérêt est considérable. Comparer de l'assembleur bloc par bloc demande un effort mental important pour reconstituer la sémantique du changement. Le diff de pseudo-code montre directement des modifications exprimées dans un langage de haut niveau — par exemple, l'ajout d'une condition `if (size > MAX_BUFFER)` est immédiatement lisible en pseudo-code, alors qu'en assembleur, il faut identifier un `cmp` suivi d'un `ja` ou `jbe` et reconstituer mentalement la logique.

> ⚠️ **Rappel** — Le pseudo-code produit par un décompileur n'est jamais parfait (cf. chapitre 20). Il peut contenir des erreurs de typage, des variables mal nommées, ou des structures de contrôle reconstruites différemment de l'original. Le diff de pseudo-code est un outil de navigation et de compréhension rapide, pas une vérité absolue. En cas de doute, descendez toujours au niveau assembleur pour vérifier.

### Vue diff de l'assembleur

Diaphora propose également un diff au niveau assembleur, similaire à celui de BinDiff. Les instructions sont présentées côte à côte, avec coloration des différences. Cette vue est complémentaire du diff de pseudo-code : le pseudo-code donne la vue d'ensemble sémantique, l'assembleur donne le détail exact.

### Vue du graphe de flot de contrôle

Pour les paires de fonctions modifiées, Diaphora peut afficher les deux CFG côte à côte avec le même code couleur que BinDiff (blocs identiques, modifiés, ajoutés, supprimés). Cette vue est particulièrement utile pour les fonctions dont la structure de contrôle a changé — ajout d'un nouveau branchement, suppression d'un chemin d'exécution, réorganisation d'une boucle.

---

## Diaphora vs BinDiff : quand choisir l'un ou l'autre

Les deux outils couvrent le même besoin, mais leurs forces respectives les rendent complémentaires plutôt que concurrents.

### Forces de Diaphora

- **Diff de pseudo-code** — c'est son avantage décisif. Pouvoir comparer le code décompilé plutôt que l'assembleur seul accélère considérablement la compréhension des changements, surtout sur des fonctions longues ou complexes.  
- **Open source** — le code est disponible, modifiable, extensible. Si vous avez besoin d'une heuristique d'appariement spécifique à votre cas d'usage (par exemple, des signatures propres à un SDK ou un framework particulier), vous pouvez l'ajouter.  
- **Catégorisation des résultats** — la séparation en best/partial/unreliable matches est plus informative qu'une simple liste triée par score. Elle guide naturellement l'analyste vers les résultats les plus pertinents.  
- **Intégration directe** — tout se passe dans le désassembleur, sans application externe à lancer. Le contexte complet (XREF, types, commentaires) est disponible en permanence.

### Forces de BinDiff

- **Maturité et robustesse** — BinDiff est développé et maintenu par Google depuis plus d'une décennie. Ses algorithmes d'appariement sont extrêmement éprouvés sur des millions de comparaisons.  
- **Performance** — sur les très gros binaires (plusieurs dizaines de mégaoctets, dizaines de milliers de fonctions), BinDiff est généralement plus rapide que Diaphora avec décompilation activée.  
- **Format d'export standardisé** — le format BinExport est un standard de facto. Les fichiers `.BinExport` peuvent être partagés entre analystes sans que chacun ait besoin du même désassembleur.  
- **Automatisation SQL** — la base SQLite des résultats BinDiff est bien documentée et facile à interroger programmatiquement.  
- **Indépendance du désassembleur** — on peut comparer un export fait depuis Ghidra avec un export fait depuis IDA. Diaphora nécessite que les deux exports soient faits depuis le même outil.

### En pratique

Beaucoup d'analystes utilisent les deux. Un workflow courant consiste à lancer d'abord BinDiff pour avoir une vue d'ensemble rapide et fiable, puis à utiliser Diaphora sur les fonctions d'intérêt pour bénéficier du diff de pseudo-code. Sur les petits binaires ou pour une analyse rapide, Diaphora seul suffit amplement. Sur les gros binaires ou dans un pipeline automatisé, BinDiff est souvent préféré pour sa vitesse et la facilité d'extraction des résultats.

---

## Options avancées de Diaphora

Quelques options méritent d'être mentionnées pour les analyses plus exigeantes :

### Seuils de similarité

Diaphora permet de configurer les seuils en dessous desquels un appariement est classé comme « partial » ou « unreliable ». Abaisser ces seuils augmente le nombre de correspondances trouvées, mais au prix d'un taux de faux positifs plus élevé. Pour un patch diffing ciblé, les valeurs par défaut sont généralement appropriées.

### Exclusion de fonctions

Vous pouvez exclure des fonctions de l'analyse par leur taille (nombre de blocs de base) ou par leur nom (expressions régulières). C'est utile pour ignorer les fonctions triviales générées par le compilateur (thunks de PLT, trampolines) qui encombrent les résultats sans apporter d'information utile.

### Export incrémental

Si vous avez déjà exporté un binaire et que vous avez ensuite enrichi l'analyse dans Ghidra (renommage de fonctions, création de types), Diaphora permet de mettre à jour l'export existant sans tout recalculer depuis zéro. C'est un gain de temps appréciable sur les gros binaires.

### Mode batch

Diaphora peut être exécuté en mode non-interactif depuis la ligne de commande de Ghidra (via `analyzeHeadless`), ce qui permet d'intégrer le diffing dans un pipeline automatisé. Le résultat est stocké dans la base SQLite et peut être exploité par un script Python sans jamais ouvrir l'interface graphique.

---

## En résumé

Diaphora est un outil puissant et flexible qui complète naturellement BinDiff dans la boîte à outils du reverse engineer. Son diff de pseudo-code est une fonctionnalité unique qui change la donne pour l'analyse de patches complexes, et sa nature open source en fait un outil adaptable à des besoins spécifiques. Le fait qu'il s'exécute entièrement à l'intérieur de Ghidra (ou IDA) simplifie le workflow et évite les allers-retours entre applications.

Le réflexe à développer : quand BinDiff vous a montré *quelles* fonctions ont changé, passez dans Diaphora pour comprendre *comment* elles ont changé, grâce au diff de pseudo-code.

---


⏭️ [`radiff2` — diffing en ligne de commande avec Radare2](/10-diffing-binaires/04-radiff2.md)
