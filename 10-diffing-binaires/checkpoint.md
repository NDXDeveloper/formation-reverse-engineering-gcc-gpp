🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 10 : Diffing de binaires

> **Objectif** : valider votre maîtrise du workflow de diffing en comparant deux versions d'un binaire et en identifiant la fonction modifiée.  
>  
> 📦 Binaires : `binaries/ch10-keygenme/keygenme_v1` et `binaries/ch10-keygenme/keygenme_v2`  
> 📄 Corrigé : `solutions/ch10-checkpoint-solution.md`

---

## Énoncé

Vous recevez deux versions d'un même binaire compilé avec GCC : `keygenme_v1` et `keygenme_v2`. Un bulletin de sécurité indique qu'une vulnérabilité de validation d'entrée a été corrigée dans la version v2, sans plus de détails.

Votre mission est de produire un **rapport de patch diffing** qui documente précisément les modifications apportées entre les deux versions.

---

## Livrable attendu

Un rapport au format Markdown (ou texte) contenant les éléments suivants :

### 1. Triage initial

- Le score de similarité global entre les deux binaires (obtenu avec `radiff2 -s`).  
- Le nombre d'octets qui diffèrent.  
- Votre conclusion à ce stade : le patch est-il ciblé ou étendu ?

### 2. Inventaire des fonctions modifiées

- La liste complète des fonctions appariées et leur score de similarité, obtenue avec au moins un des trois outils du chapitre (`radiff2 -AC`, BinDiff ou Diaphora).  
- L'identification claire de la ou des fonctions dont le score est inférieur à 1.0.  
- Pour chaque fonction non modifiée, une simple mention confirmant qu'elle est identique entre les deux versions.

### 3. Analyse de la fonction modifiée

- Le nom (ou l'adresse, si le binaire est strippé) de la fonction modifiée.  
- Une description des changements observés au niveau du graphe de flot de contrôle : combien de blocs de base dans la v1, combien dans la v2, quels blocs ont été ajoutés/modifiés/supprimés.  
- Le diff de pseudo-code (obtenu avec Diaphora) ou, à défaut, le diff assembleur côte à côte montrant les instructions ajoutées ou modifiées.  
- Votre interprétation du changement : que fait le code ajouté ? Quelle vérification manquait dans la v1 ?

### 4. Caractérisation de la vulnérabilité

- La nature de la vulnérabilité corrigée, décrite en une ou deux phrases.  
- Le type de vulnérabilité selon la taxonomie CWE (identifiant et nom).  
- L'impact potentiel si la vulnérabilité était exploitée.

### 5. Tableau de synthèse

Un tableau récapitulatif sur le modèle suivant :

| Élément | Détail |  
|---------|--------|  
| Fonction modifiée | *(nom ou adresse)* |  
| Nature du changement | *(description courte)* |  
| Type de vulnérabilité | *(CWE-XX)* |  
| Impact potentiel | *(description courte)* |  
| Correction appliquée | *(description courte)* |

---

## Outils à utiliser

Le rapport doit démontrer l'utilisation d'**au moins deux** des trois outils suivants :

- `radiff2` (triage obligatoire — c'est la première étape du workflow)  
- BinDiff (export BinExport depuis Ghidra ou IDA, comparaison, lecture du CFG)  
- Diaphora (export SQLite depuis Ghidra ou IDA, diff de pseudo-code)

L'utilisation des trois outils est encouragée mais pas obligatoire. L'important est de montrer que vous savez choisir le bon outil pour chaque étape de l'analyse.

---

## Critères de validation

Votre checkpoint est validé si votre rapport :

- ✅ Identifie correctement la fonction modifiée par le patch.  
- ✅ Décrit les changements au niveau du CFG (nombre de blocs, blocs ajoutés).  
- ✅ Explique la nature de la vulnérabilité corrigée avec une interprétation cohérente.  
- ✅ Inclut un score de similarité issu de `radiff2` ou BinDiff.  
- ✅ Présente un diff (pseudo-code ou assembleur) de la fonction modifiée.  
- ✅ Contient un tableau de synthèse exploitable.

---

## Conseils avant de commencer

- Suivez le workflow en entonnoir présenté dans le chapitre : **triage** (`radiff2`) → **vue d'ensemble** (BinDiff) → **analyse détaillée** (Diaphora) → **vérification assembleur** (Ghidra). Chaque étape réduit le périmètre d'investigation.  
- Ne cherchez pas à comprendre l'intégralité du binaire. Le diffing sert précisément à éviter cela — concentrez-vous sur ce qui a changé.  
- Si vous êtes bloqué sur l'installation d'un outil, n'hésitez pas à sauter à l'outil suivant. Deux outils sur trois suffisent pour produire un rapport complet.  
- Prenez des captures d'écran des vues CFG et des diffs de pseudo-code pour enrichir votre rapport. Un bon rapport de patch diffing est autant visuel que textuel.

---

> 📄 **Corrigé disponible** : `solutions/ch10-checkpoint-solution.md` — ne le consultez qu'après avoir produit votre propre rapport.  
>  
> 

⏭️ [Partie III — Analyse Dynamique](/partie-3-analyse-dynamique.md)
