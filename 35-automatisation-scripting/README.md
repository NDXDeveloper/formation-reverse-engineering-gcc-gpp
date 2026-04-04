🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 35 — Automatisation et scripting

> 📂 `35-automatisation-scripting/`  
> 🎯 **Objectif** : transformer les techniques manuelles vues tout au long de la formation en workflows reproductibles, scriptables et intégrables dans des pipelines d'analyse.

---

## Pourquoi automatiser le Reverse Engineering ?

Au fil des chapitres précédents, chaque analyse a suivi un schéma récurrent : triage initial, inspection des sections et symboles, désassemblage, analyse dynamique, extraction de données, rédaction d'un rapport. Réalisées manuellement, ces étapes sont fiables sur un binaire isolé, mais elles deviennent un goulot d'étranglement dès que le volume augmente — qu'il s'agisse de comparer vingt variantes d'un même firmware, de surveiller les mises à jour d'une bibliothèque tierce, ou de scanner un parc de binaires à la recherche de constantes cryptographiques suspectes.

L'automatisation ne remplace pas le jugement de l'analyste. Elle prend en charge les tâches répétitives et déterministes pour lui permettre de concentrer son attention là où elle a le plus de valeur : comprendre la logique métier, identifier les anomalies subtiles, et prendre des décisions face à l'ambiguïté. Un bon script de RE ne fait pas l'analyse *à votre place* — il vous débarrasse de tout ce qui ne nécessite pas votre cerveau.

## Ce que couvre ce chapitre

Ce chapitre rassemble les outils et les méthodes de scripting que nous avons croisés ponctuellement dans les parties précédentes, et les organise en une boîte à outils cohérente :

**Parsing et modification de binaires ELF** — avec `pyelftools` pour l'inspection en lecture seule et `lief` pour les transformations structurelles (ajout de sections, modification de headers, patching d'entry points). Ces deux bibliothèques Python constituent le socle programmatique de toute automatisation sur des binaires ELF.

**Analyse batch avec Ghidra headless** — Ghidra expose un mode sans interface graphique (`analyzeHeadless`) qui permet de lancer l'auto-analyse et d'exécuter des scripts Java ou Python sur un ou plusieurs binaires, en ligne de commande. C'est la clé pour traiter des corpus entiers sans ouvrir manuellement chaque projet.

**Scripting RE avec `pwntools`** — au-delà de son usage en exploitation, `pwntools` offre un cadre complet pour interagir avec des binaires (lancement de processus, envoi/réception de données, patching en mémoire), ce qui en fait un outil polyvalent pour automatiser les tests dynamiques et la validation de hypothèses.

**Détection de patterns avec YARA** — écrire des règles YARA permet de rechercher des signatures (constantes crypto, chaînes caractéristiques, séquences d'opcodes) à travers une collection de binaires. C'est le pont naturel entre l'analyse unitaire et le scanning à grande échelle.

**Intégration CI/CD** — les scripts d'analyse peuvent s'insérer dans un pipeline d'intégration continue pour auditer automatiquement chaque build, détecter des régressions binaires (symboles de debug oubliés en production, protections désactivées, dépendances inattendues), et produire des rapports exploitables par toute l'équipe.

**Construction d'un toolkit personnel** — la dernière section aborde l'organisation pratique : comment structurer ses scripts, gérer les dépendances, documenter ses outils, et constituer progressivement une boîte à outils RE adaptée à ses besoins.

## Prérequis pour ce chapitre

Ce chapitre s'appuie sur l'ensemble des compétences acquises dans les parties précédentes. En particulier :

- **Python intermédiaire** — les scripts présentés utilisent des bibliothèques tierces, manipulent des structures binaires et interagissent avec des processus. Une aisance avec `pip`, les environnements virtuels, et la manipulation de bytes en Python est nécessaire.  
- **Familiarité avec les outils du tuto** — Ghidra (chapitre 8), GDB (chapitre 11), `pwntools` (chapitre 11, section 9), ImHex et YARA (chapitre 6), `readelf`/`objdump` (chapitres 5 et 7). Vous n'avez pas besoin de les maîtriser parfaitement, mais vous devez savoir ce qu'ils font et avoir déjà lancé chacun d'eux au moins une fois.  
- **Notions de base en shell Linux** — les pipelines CI/CD et l'orchestration de scripts supposent une aisance avec Bash, les redirections, et les outils classiques (`find`, `xargs`, `jq`).

## Philosophie d'approche

Tout au long de ce chapitre, nous suivrons trois principes directeurs :

**Commencer petit, itérer vite.** Un script de 20 lignes qui fait une chose bien vaut mieux qu'un framework de 2 000 lignes jamais terminé. Chaque section part d'un cas concret minimal avant de généraliser.

**Sortie structurée, toujours.** Les résultats d'un script doivent être exploitables par un autre script. Cela signifie du JSON en sortie plutôt que du texte libre, des codes de retour cohérents, et des logs séparés du résultat. Un rapport humainement lisible peut toujours être généré *à partir* d'une sortie structurée — l'inverse est rarement vrai.

**Reproduire avant d'optimiser.** L'automatisation n'a de valeur que si elle produit des résultats fiables et reproductibles. Avant d'accélérer un workflow, on s'assure qu'il donne le même résultat à chaque exécution sur le même input, dans le même environnement.

## Structure du chapitre

| Section | Contenu | Outils principaux |  
|---|---|---|  
| 35.1 | Parsing et modification d'ELF en Python | `pyelftools`, `lief` |  
| 35.2 | Analyse batch avec Ghidra headless | `analyzeHeadless`, scripts Ghidra |  
| 35.3 | Scripting RE avec `pwntools` | `pwntools` |  
| 35.4 | Détection de patterns avec YARA | `yara-python`, règles `.yar` |  
| 35.5 | Intégration dans un pipeline CI/CD | GitHub Actions, scripts shell |  
| 35.6 | Construction d'un toolkit RE personnel | organisation, documentation |  
| **Checkpoint** | Script d'analyse batch → rapport JSON | tous les outils du chapitre |

---

> 💡 **Conseil** : gardez un terminal ouvert avec le dépôt `binaries/` à portée. Les exemples de ce chapitre s'appuient directement sur les binaires compilés dans les chapitres précédents — en particulier `ch21-keygenme/`, `ch24-crypto/` et `ch25-fileformat/`. Si vous ne les avez pas encore compilés, un simple `make all` depuis `binaries/` suffit.

---


⏭️ [Scripts Python avec `pyelftools` et `lief` (parsing et modification d'ELF)](/35-automatisation-scripting/01-pyelftools-lief.md)
