🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 9

## Analyser le même binaire dans 2 outils différents, comparer les résultats du décompileur

> 📘 **Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja**  
> Section précédente : [9.6 — Comparatif Ghidra vs IDA vs Radare2 vs Binary Ninja](/09-ida-radare2-binja/06-comparatif-outils.md)

---

## Objectif

Ce checkpoint valide votre capacité à utiliser au moins deux désassembleurs de manière autonome sur un même binaire, et à porter un regard critique sur les différences de résultats entre outils. Il mobilise l'ensemble des compétences du chapitre : navigation, annotation, décompilation, et cross-references.

Le livrable est un **court rapport comparatif** (1 à 2 pages) documentant votre analyse et vos observations.

## Binaire cible

Le binaire à analyser est `keygenme_O2_strip`, situé dans `binaries/ch09-keygenme/`. C'est le binaire fil rouge utilisé tout au long du chapitre : compilé avec `-O2`, strippé, ELF x86-64. Si vous avez suivi les sections 9.1 à 9.5, vous l'avez déjà ouvert dans plusieurs outils — ce checkpoint vous demande de formaliser la comparaison.

## Choix des outils

Choisissez **deux outils parmi les quatre** couverts dans les chapitres 8 et 9 :

- Ghidra (chapitre 8)  
- IDA Free (section 9.1)  
- Radare2 / Cutter (sections 9.2–9.4)  
- Binary Ninja Cloud (section 9.5)

La combinaison recommandée pour un premier checkpoint est **Ghidra + un autre outil de votre choix**. Ghidra étant l'outil principal de la formation, le comparer avec un second outil ancre la pratique du cross-checking qui sera utile tout au long des parties suivantes.

## Éléments attendus dans le rapport

Le rapport comparatif doit couvrir les points suivants.

### 1 — Reconnaissance de fonctions

Indiquez pour chaque outil le nombre total de fonctions détectées après l'analyse automatique complète. Identifiez les éventuelles divergences : l'un des outils a-t-il détecté des fonctions que l'autre a manquées ? Si oui, à quelles adresses, et pourquoi selon vous (heuristique de détection différente, code confondu avec des données, fonction trop courte pour être détectée) ?

Notez également si les deux outils ont identifié `main` automatiquement, et par quel mécanisme (reconnaissance du pattern `__libc_start_main`, symboles résiduels, heuristique de nom).

### 2 — Qualité du décompilé sur la fonction principale

Concentrez-vous sur la fonction de vérification du serial (celle qui contient les appels à `strcmp` et les branchements vers les chaînes « Access granted » / « Wrong key »). Exportez ou capturez le pseudo-code produit par chacun des deux outils pour cette même fonction.

Comparez les résultats sur les axes suivants :

- **Lisibilité** — lequel des deux pseudo-codes est le plus immédiatement compréhensible ? Les noms de variables sont-ils plus explicites dans l'un ou l'autre ?  
- **Fidélité** — les deux décompileurs reconstruisent-ils la même structure de contrôle (mêmes `if/else`, mêmes conditions) ? Y a-t-il des `goto` dans l'un mais pas dans l'autre ?  
- **Types** — les types des variables et des paramètres sont-ils inférés de la même manière ? L'un des outils a-t-il mieux propagé les types (par exemple, reconnaître un `char *` là où l'autre affiche un `undefined *` ou un `int64_t`) ?  
- **Erreurs ou artefacts** — l'un des décompileurs produit-il du code manifestement incorrect ou trompeur (variable fantôme, condition inversée, appel de fonction mal résolu) ?

### 3 — Cross-references et navigation

Pour la chaîne « Access granted » (ou « Wrong key »), documentez le chemin de navigation dans chaque outil : comment avez-vous localisé la chaîne, comment avez-vous remonté les XREF, et comment avez-vous atteint la fonction de vérification. Notez les différences d'ergonomie : nombre de clics ou de commandes nécessaires, clarté de l'affichage des XREF, facilité du retour en arrière.

### 4 — Annotations et renommages

Dans chacun des deux outils, renommez la fonction de vérification et au moins deux variables locales avec des noms significatifs. Documentez la facilité de l'opération : raccourci clavier ou menu, propagation immédiate ou non dans le désassemblage et le décompilé, persistance après fermeture et réouverture du projet.

### 5 — Synthèse et préférence argumentée

Concluez le rapport par une synthèse personnelle. Sur ce binaire précis et avec les tâches effectuées, quel outil avez-vous trouvé le plus efficace et pourquoi ? Y a-t-il des tâches où l'un surpassait clairement l'autre ? Quel outil utiliseriez-vous en premier sur un prochain binaire similaire, et dans quel cas basculeriez-vous vers le second ?

Il n'y a pas de « bonne réponse » à cette synthèse — l'objectif est de construire votre propre grille de décision informée, pas de reproduire celle de la section 9.6.

## Critères de validation

Le checkpoint est validé si le rapport :

- Couvre les cinq points ci-dessus avec des observations concrètes (adresses, noms de fonctions, extraits de pseudo-code, comptages).  
- Contient au moins un exemple précis de divergence entre les deux outils (même mineure).  
- Montre que vous avez effectivement manipulé les deux outils (et pas seulement décrit leurs fonctionnalités théoriques).  
- Formule une synthèse argumentée basée sur votre expérience directe.

## Format du rapport

Le rapport peut être rédigé en Markdown, en texte brut, ou dans le format de votre choix. Il est destiné à votre usage personnel — c'est un document de travail, pas un livrable formel. Des captures d'écran ou des copier-coller de pseudo-code sont bienvenus pour illustrer les points de comparaison.

Un modèle minimaliste :

```
# Checkpoint Ch.9 — Comparatif [Outil A] vs [Outil B]
Binaire : keygenme_O2_strip

## 1. Reconnaissance de fonctions
[Outil A] : XX fonctions détectées
[Outil B] : XX fonctions détectées
Divergences : ...

## 2. Décompilé de la fonction de vérification
### [Outil A]
(pseudo-code ou capture)
### [Outil B]
(pseudo-code ou capture)
### Comparaison
...

## 3. Navigation et XREF
...

## 4. Annotations
...

## 5. Synthèse
...
```

## Pour aller plus loin

Si vous souhaitez approfondir l'exercice, vous pouvez étendre la comparaison sur deux axes supplémentaires :

- **Comparer trois outils** au lieu de deux, ce qui rend les divergences plus visibles et permet de départager les cas où deux outils s'accordent contre un troisième.  
- **Répéter l'analyse sur `keygenme_O0`** (avec symboles, sans optimisation) puis sur `keygenme_O2_strip` pour observer comment la difficulté du binaire affecte la qualité relative des outils. Un outil peut exceller sur un binaire `-O0` et être en retrait sur un `-O2` strippé, ou inversement.

---

> ✅ **Checkpoint terminé ?** Vous maîtrisez maintenant les bases de quatre désassembleurs majeurs et vous savez les comparer de manière critique. La suite de la formation (Partie III — Analyse Dynamique) introduira GDB et les outils de débogage, qui complèteront votre arsenal avec la dimension dynamique du reverse engineering.  
>  
> 

⏭️ [Chapitre 10 — Diffing de binaires](/10-diffing-binaires/README.md)
