🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 10.1 — Pourquoi comparer deux versions d'un même binaire (analyse de patch, détection de vuln)

> **Chapitre 10 — Diffing de binaires**  
> **Partie II — Analyse Statique**

---

## Le problème fondamental

Quand on reverse un binaire isolé, on cherche à comprendre **ce qu'il fait**. Mais dans de nombreux scénarios professionnels, la question est différente : **qu'est-ce qui a changé ?** Entre deux versions d'un firmware, entre un patch de sécurité mardi et le binaire de lundi, entre une build de développement et celle de production — la différence est souvent plus parlante que le code complet.

Le problème, c'est qu'on ne peut pas simplement lancer `diff` sur deux binaires. Un `diff` textuel ou octet par octet est inutilisable en pratique : le moindre changement dans une fonction décale les adresses de toutes les fonctions suivantes, les offsets de sauts sont recalculés, les tables de relocation changent, et le résultat est un mur de différences sans signification. Même deux compilations successives du **même code source** avec le même compilateur peuvent produire des binaires légèrement différents (timestamps, chemins de build intégrés dans les métadonnées DWARF, randomisation de certains layouts internes).

Le **binary diffing** résout ce problème en travaillant à un niveau d'abstraction supérieur : au lieu de comparer des octets, on compare des **fonctions**, des **blocs de base** (basic blocks) et des **graphes de flot de contrôle** (CFG). Les outils de diffing apparient les fonctions des deux binaires en s'appuyant sur des heuristiques structurelles — signature du CFG, constantes utilisées, noms de symboles quand ils existent, hash du pseudo-code décompilé — puis, pour chaque paire de fonctions appariées, identifient les blocs qui ont été modifiés, ajoutés ou supprimés.

---

## Les scénarios concrets du diffing

### Patch diffing (analyse 1-day)

C'est le cas d'usage le plus emblématique. Lorsqu'un éditeur publie un correctif de sécurité — que ce soit Microsoft lors du Patch Tuesday, Adobe pour un Reader, ou un projet open source qui ne fournit qu'un binaire mis à jour — le bulletin de sécurité associé donne rarement plus qu'une description vague : « vulnérabilité de corruption mémoire dans le module de parsing PDF, permettant une exécution de code arbitraire ». Parfois même moins.

Le binaire patché, lui, contient **toute** l'information. En comparant la version vulnérable (avant le patch) et la version corrigée (après le patch), on peut :

1. **Localiser la fonction modifiée** — sur un binaire de plusieurs milliers de fonctions, le diff réduit le périmètre d'investigation à une poignée de fonctions, souvent une ou deux.  
2. **Comprendre la nature de la vulnérabilité** — le diff montre exactement ce qui a changé. Un ajout de vérification de taille avant un `memcpy` suggère un buffer overflow. Un nouveau test de nullité pointe vers un null pointer dereference. Un changement dans une condition de boucle indique un off-by-one.  
3. **Évaluer la criticité réelle** — le bulletin dit « critique », mais qu'est-ce que cela signifie concrètement ? Le diff permet de juger si la vulnérabilité est réellement exploitable dans un contexte donné.  
4. **Développer une preuve de concept** — pour les chercheurs offensifs ou les équipes de red team, le diff accélère considérablement la compréhension nécessaire à l'écriture d'un exploit (on parle alors d'analyse **1-day**, par opposition au **0-day** où la vulnérabilité est trouvée sans connaissance préalable du patch).

> ⚠️ **Note éthique** — L'analyse 1-day est une pratique légitime et courante dans l'industrie de la sécurité. Elle est utilisée par les équipes défensives pour évaluer l'urgence d'un déploiement de patch, par les éditeurs de solutions de sécurité pour créer des signatures de détection, et par les chercheurs pour comprendre les classes de vulnérabilités. Comme toujours, c'est l'intention et le cadre légal qui déterminent la légitimité de l'usage (cf. chapitre 1, section 1.2).

### Analyse de régression binaire

Dans un contexte de développement, le diffing permet de vérifier que les modifications apportées au code source se traduisent bien par les changements attendus — et uniquement ceux-là — dans le binaire final. C'est particulièrement utile dans les cas suivants :

- **Changement de version du compilateur** — passer de GCC 12 à GCC 14 peut modifier le code généré de manière subtile. Le diffing permet d'identifier précisément les fonctions dont la génération de code a changé et de vérifier qu'aucune régression de performance ou de comportement n'a été introduite.  
- **Changement de flags de compilation** — activer `-O2` au lieu de `-O0`, ajouter `-fstack-protector-strong`, passer en `-fPIC` pour une bibliothèque partagée… chaque flag a des conséquences visibles dans le binaire. Le diffing quantifie et localise ces conséquences.  
- **Audit de la chaîne de build** — dans les environnements où la sécurité de la supply chain logicielle est critique (embarqué, aérospatial, défense), le diffing permet de vérifier qu'un binaire livré correspond bien au code source audité, en comparant le résultat d'une recompilation contrôlée au binaire de production.

### Reproducible builds

Le mouvement des *reproducible builds* vise à garantir que n'importe qui peut recompiler un logiciel à partir de ses sources et obtenir un binaire **bit-à-bit identique** à celui distribué. L'objectif est de permettre une vérification indépendante : si le binaire distribué diffère du résultat de la recompilation, c'est qu'il a été altéré (backdoor, compromission de la chaîne de build).

En pratique, atteindre la reproductibilité parfaite est difficile — les timestamps, les chemins de fichiers, l'ordre de traitement des fichiers par le compilateur introduisent des variations. Le diffing intervient alors pour distinguer les différences **cosmétiques** (un timestamp différent dans un header ELF) des différences **sémantiques** (une fonction dont le code a changé). C'est un outil précieux pour les projets qui tendent vers la reproductibilité sans l'avoir encore atteinte complètement.

### Suivi de l'évolution d'un logiciel propriétaire

Quand on travaille sur l'interopérabilité avec un logiciel propriétaire — un protocole réseau, un format de fichier, un driver — chaque nouvelle version peut modifier le comportement qu'on a soigneusement documenté. Le diffing permet de suivre ces évolutions de manière ciblée : au lieu de re-reverser l'intégralité du binaire à chaque release, on compare les deux versions et on concentre l'effort sur les fonctions qui ont changé. C'est un gain de temps considérable sur des binaires de plusieurs mégaoctets contenant des milliers de fonctions.

### Analyse de malware : suivi de variantes

Les familles de malware évoluent par itérations. Un ransomware version 2.1 partage l'essentiel de son code avec la version 2.0, mais peut avoir modifié son algorithme de chiffrement, ajouté un mécanisme d'évasion ou changé son protocole C2. Le diffing permet aux analystes de malware de se concentrer immédiatement sur les nouveautés au lieu de repartir de zéro, et de maintenir une chronologie des modifications au fil des variantes.

---

## Ce que le diffing révèle — et ce qu'il ne révèle pas

### Ce que le diffing fait bien

Le diffing excelle à répondre à des questions structurelles et quantitatives :

- **Quelles fonctions ont changé ?** — avec un score de confiance sur l'appariement.  
- **Quelles fonctions sont nouvelles ?** — présentes dans la version B mais absentes de la version A.  
- **Quelles fonctions ont été supprimées ?** — présentes dans A, absentes de B.  
- **Quelle est l'ampleur du changement ?** — une modification d'un seul bloc de base dans une fonction de 50 blocs, ou une réécriture complète ?  
- **Où exactement dans la fonction se situe le changement ?** — au niveau du bloc de base, avec visualisation côte à côte.

### Ce que le diffing ne fait pas tout seul

Le diffing localise les changements, mais il ne les **explique** pas. Savoir qu'un `jl` (jump if less) a été remplacé par un `jle` (jump if less or equal) dans la fonction `parse_header` est une information précieuse, mais comprendre que cela corrige un off-by-one qui permettait un heap overflow nécessite toujours l'expertise du reverse engineer. Le diffing est un **outil d'orientation** : il réduit un binaire de 10 000 fonctions à 3 fonctions modifiées, et c'est là que le travail de RE classique reprend.

De même, le diffing ne gère pas bien les changements massifs de structure. Si un binaire a été entièrement recompilé avec un compilateur différent, ou si une refactorisation majeure a réorganisé le code, les algorithmes d'appariement peuvent échouer à reconnaître les fonctions correspondantes. Dans ces cas, les outils signaleront un grand nombre de fonctions « non appariées », et l'analyse devra être complétée manuellement.

---

## Anatomie d'un diff binaire

Pour comprendre ce que les outils vont nous montrer dans les sections suivantes, il est utile de fixer le vocabulaire et les concepts. Un résultat de diff binaire se présente typiquement sous la forme de trois catégories de fonctions :

### Fonctions appariées identiques (*matched, identical*)

Ces fonctions existent dans les deux binaires et sont considérées comme identiques par l'algorithme. Elles n'ont pas été modifiées entre les deux versions. C'est généralement la très grande majorité des fonctions — un patch de sécurité modifie rarement plus de quelques fonctions sur un total de plusieurs milliers.

### Fonctions appariées modifiées (*matched, changed*)

Ces fonctions existent dans les deux binaires et ont été reconnues comme correspondantes, mais leur contenu diffère. C'est ici que se trouvent les informations les plus intéressantes. Pour chaque paire, l'outil fournit :

- Un **score de similarité** (typiquement entre 0.0 et 1.0) — une fonction avec un score de 0.95 n'a subi qu'un changement mineur, tandis qu'un score de 0.3 indique une réécriture substantielle.  
- Un **diff au niveau des blocs de base** — visualisation côte à côte des basic blocks, avec coloration des blocs modifiés, ajoutés et supprimés.  
- Les **instructions modifiées** au sein de chaque bloc.

### Fonctions non appariées (*unmatched*)

Ces fonctions n'existent que dans l'un des deux binaires. Dans la version A mais pas dans B : la fonction a été supprimée (ou renommée/refactorisée au point d'être méconnaissable). Dans B mais pas dans A : c'est une fonction nouvelle. Attention cependant : une fonction « non appariée » n'est pas nécessairement nouvelle — il arrive que l'algorithme échoue à reconnaître une fonction qui a été significativement modifiée, et la classe dans cette catégorie par erreur.

---

## Algorithmes d'appariement : l'intuition

Sans entrer dans les détails mathématiques (chaque outil a ses propres heuristiques), voici les grandes familles de critères utilisés pour apparier les fonctions entre deux binaires :

- **Correspondance de nom** — si les deux binaires ne sont pas strippés, les noms de fonctions sont le critère le plus fiable. Deux fonctions portant le même symbole sont appariées en priorité.  
- **Hash structurel du CFG** — le graphe de flot de contrôle (nombre de blocs, nombre d'arêtes, structure des branchements) est converti en un hash. Deux fonctions avec le même hash structurel sont très probablement les mêmes.  
- **Constantes et chaînes référencées** — si une fonction référence la chaîne `"Invalid header size"` et un appel à `malloc(0x200)` dans les deux versions, c'est un fort indice de correspondance.  
- **Hash du pseudo-code** — certains outils (notamment Diaphora) comparent le pseudo-code produit par le décompileur. C'est plus résistant aux changements cosmétiques (réordonnancement de registres, changement d'adresses) que la comparaison au niveau assembleur.  
- **Position dans le graphe d'appels** — une fonction appelée par les mêmes callers et appelant les mêmes callees dans les deux versions est probablement la même, même si son code interne a changé.  
- **Propagation** — une fois que certaines fonctions sont appariées avec haute confiance, leurs voisines dans le graphe d'appels peuvent être appariées par propagation (si A appelle B et C dans la version 1, et A' appelle B' et C' dans la version 2, et qu'on sait déjà que A↔A' et B↔B', alors C↔C' est probable).

Les outils combinent ces critères en plusieurs passes, des plus fiables (correspondance exacte de nom ou de hash) aux plus heuristiques (propagation par le graphe d'appels), pour maximiser le nombre de fonctions appariées.

---

## En résumé

Le diffing de binaires est un multiplicateur de productivité pour le reverse engineer. Plutôt que de noyer l'analyste sous l'intégralité du code d'un programme, il isole chirurgicalement les changements et permet de concentrer l'effort humain là où il a le plus de valeur. C'est un outil incontournable dans la boîte à outils du professionnel de la sécurité, qu'il travaille en défense (évaluation de patches, analyse de variantes) ou en recherche (compréhension de vulnérabilités, interopérabilité).

Dans les sections suivantes, nous allons mettre ces concepts en pratique avec les trois outils principaux de l'écosystème : BinDiff, Diaphora et `radiff2`.

---


⏭️ [BinDiff (Google) — installation, import depuis Ghidra/IDA, lecture du résultat](/10-diffing-binaires/02-bindiff.md)
