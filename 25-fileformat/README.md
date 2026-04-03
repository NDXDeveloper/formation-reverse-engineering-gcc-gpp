🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 25 — Reverse d'un format de fichier custom

> 📦 **Binaire d'entraînement** : `binaries/ch25-fileformat/`  
> Compilable via `make` à différents niveaux d'optimisation. Le binaire lit et écrit des fichiers dans un format propriétaire inventé pour ce chapitre.

---

## Pourquoi reverser un format de fichier ?

Dans la pratique du Reverse Engineering, on ne s'attaque pas uniquement à des binaires exécutables pour comprendre leur logique interne. Très souvent, le véritable objectif est de comprendre **les données que ces binaires produisent et consomment**. Un logiciel propriétaire qui sauvegarde ses projets dans un format `.xyz` non documenté, un firmware qui stocke sa configuration dans un blob binaire, un jeu vidéo qui empaquette ses assets dans une archive custom — autant de situations où la cible du reverse n'est pas le code lui-même, mais le **format de fichier** qu'il manipule.

Reverser un format de fichier, c'est répondre à une question apparemment simple : **« comment ces octets sont-ils organisés, et que signifient-ils ? »** En pratique, cela exige de mobiliser simultanément l'analyse statique du binaire (pour comprendre comment le parseur lit le fichier), l'analyse hexadécimale (pour observer directement la structure des données), et parfois l'analyse dynamique (pour voir le parseur en action et confirmer des hypothèses).

Ce type de travail débouche sur des résultats concrets et immédiatement utiles :

- **Interopérabilité** — pouvoir lire ou écrire un format propriétaire depuis un outil tiers, sans dépendre du logiciel d'origine.  
- **Migration de données** — convertir des fichiers d'un format obsolète ou fermé vers un format ouvert.  
- **Audit de sécurité** — comprendre comment un parseur traite les données permet d'identifier des failles (buffer overflows sur des champs de taille, integer overflows sur des compteurs, absence de validation des magic bytes…).  
- **Forensics et investigation** — extraire des informations d'un fichier dont le format n'est pas documenté publiquement.  
- **Documentation** — produire une spécification exploitable par d'autres développeurs ou chercheurs.

## Ce que nous allons faire dans ce chapitre

Le binaire `ch25-fileformat` implémente un format de fichier custom conçu spécifiquement pour cet exercice. Ce format possède les caractéristiques typiques que l'on retrouve dans les formats propriétaires réels : un header avec des magic bytes et des métadonnées, des champs de taille variable, des enregistrements répétés, et quelques subtilités volontairement introduites pour rendre l'analyse plus intéressante.

Notre objectif est de partir de zéro — sans aucune documentation du format — et d'aboutir à trois livrables complets :

1. **Un pattern ImHex (`.hexpat`)** capable de parser et coloriser n'importe quel fichier valide dans ce format, rendant sa structure immédiatement lisible.  
2. **Un parser/sérialiseur Python** autonome, capable de lire un fichier dans ce format, d'en extraire le contenu, et d'en produire de nouveaux fichiers conformes.  
3. **Une spécification documentée** du format, suffisamment précise pour qu'un développeur tiers puisse implémenter son propre parser sans jamais toucher au binaire d'origine.

## Méthodologie générale

Reverser un format de fichier suit une méthodologie itérative qui alterne entre observation directe des données et analyse du code qui les traite. On ne procède pas de façon purement linéaire : chaque découverte dans le hex viewer peut orienter l'analyse du désassemblage, et inversement, chaque structure identifiée dans le code du parseur se confirme (ou se corrige) en observant les octets bruts.

La démarche que nous suivrons dans ce chapitre se décompose en grandes étapes :

**Reconnaissance initiale.** Avant de lancer un désassembleur, on commence par les outils les plus simples : `file` pour tenter une identification automatique, `strings` pour repérer des chaînes lisibles, et `binwalk` pour détecter des sous-structures connues ou des zones d'entropie élevée. Cette étape donne un premier aperçu grossier — magic bytes, présence ou absence de compression, taille des blocs.

**Cartographie hexadécimale.** On ouvre ensuite le fichier dans ImHex et on commence à annoter ce que l'on observe : les premiers octets (header probable), les motifs répétitifs (enregistrements), les zones de padding, les valeurs qui ressemblent à des tailles ou des offsets. On construit progressivement un pattern `.hexpat` qui évolue au fil de nos découvertes. C'est un processus itératif : on écrit une première ébauche, on l'applique, on observe ce qui colle et ce qui ne colle pas, on affine.

**Validation par fuzzing.** Une fois qu'on a une hypothèse raisonnable sur la structure du format, on utilise AFL++ pour fuzzer le parseur du binaire. Les crashs révèlent les chemins de code que notre compréhension n'avait pas encore couverts : un champ qu'on pensait ignoré mais qui est en réalité vérifié, une taille maximale implicite, un cas limite dans la gestion des enregistrements. Le fuzzing ne remplace pas l'analyse manuelle, mais il la complète en explorant mécaniquement les recoins du parseur.

**Implémentation Python.** Quand la compréhension du format est suffisamment solide, on écrit un parser en Python capable de lire les fichiers existants. On valide en comparant la sortie du parser Python avec le comportement du binaire original. Puis on ajoute la capacité d'écriture : générer un fichier dans le format, le faire lire par le binaire original, et vérifier qu'il est accepté. Cette étape de round-trip (lecture → écriture → relecture) est le test ultime de notre compréhension.

**Documentation.** Enfin, on formalise tout ce qui a été découvert dans une spécification structurée. Un bon document de spécification de format décrit chaque champ avec son offset, sa taille, son type, ses valeurs possibles et ses contraintes. Il inclut des diagrammes de la structure et des exemples annotés.

## Prérequis pour ce chapitre

Ce chapitre mobilise des compétences et des outils introduits dans les parties précédentes. En particulier :

- **ImHex et le langage `.hexpat`** (chapitre 6) — nous allons écrire des patterns de complexité réelle, pas juste des exemples pédagogiques.  
- **Ghidra ou un désassembleur équivalent** (chapitres 8-9) — pour analyser le code du parseur dans le binaire quand l'observation hexadécimale ne suffit pas.  
- **AFL++** (chapitre 15) — pour valider notre compréhension du format en faisant explorer au fuzzer les chemins du parseur.  
- **Python** — pour le parser final. Aucune bibliothèque exotique n'est nécessaire ; `struct`, `io` et les types de base suffisent.  
- **Les outils de triage** (chapitre 5) — `file`, `strings`, `binwalk`, `xxd` pour la reconnaissance initiale.

## Organisation des sections

| Section | Contenu |  
|---|---|  
| 25.1 | Reconnaissance initiale avec `file`, `strings` et `binwalk` |  
| 25.2 | Cartographie itérative avec ImHex et écriture du `.hexpat` |  
| 25.3 | Validation de la structure par fuzzing avec AFL++ |  
| 25.4 | Écriture d'un parser/sérialiseur Python indépendant |  
| 25.5 | Rédaction de la spécification documentée du format |

## 🎯 Checkpoint du chapitre

> Produire les trois livrables pour le format `ch25-fileformat` :  
> - un pattern `.hexpat` fonctionnel,  
> - un parser Python capable de lire et écrire le format,  
> - une spécification documentée du format.  
>  
> Le parser Python doit passer le test de round-trip : un fichier généré par le parser doit être accepté et correctement lu par le binaire original.

---


⏭️ [Identifier la structure générale avec `file`, `strings` et `binwalk`](/25-fileformat/01-identifier-structure.md)
