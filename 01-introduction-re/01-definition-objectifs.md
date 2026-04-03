🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 1.1 — Définition et objectifs du Reverse Engineering

> **Chapitre 1 — Introduction au Reverse Engineering**  
> 📦 Aucun prérequis technique — section de lecture.

---

## Qu'est-ce que le reverse engineering ?

Le terme **reverse engineering** (rétro-ingénierie en français, souvent abrégé **RE**) désigne le processus qui consiste à analyser un système fini — un produit, un mécanisme, un logiciel — pour en comprendre le fonctionnement interne, sans disposer de sa documentation de conception ni de ses plans originaux.

L'idée n'est pas propre à l'informatique. L'ingénierie inverse existe depuis aussi longtemps que l'ingénierie elle-même. Un horloger qui démonte un mécanisme concurrent pour comprendre son échappement fait du reverse engineering. Un chimiste qui analyse la composition d'un matériau pour en retrouver la formule fait du reverse engineering. Le principe est toujours le même : partir du résultat observable pour remonter vers la logique de conception.

En informatique, et plus précisément dans le contexte de cette formation, le reverse engineering désigne l'analyse d'un **binaire compilé** — un fichier exécutable produit par un compilateur comme GCC — dans le but de comprendre ce que fait le programme, comment il le fait, et parfois pourquoi il le fait d'une certaine manière, le tout **sans accès au code source original**.

### La perte d'information au cœur du problème

Pour comprendre pourquoi le RE est une discipline à part entière, il faut saisir un point fondamental : **la compilation est un processus à sens unique qui détruit de l'information**.

Quand vous compilez un fichier `main.c` avec GCC, le compilateur transforme du code source lisible par un humain en une séquence d'instructions machine lisibles par un processeur. Au passage, un grand nombre d'informations disparaissent :

- Les **noms de variables locales** n'existent plus. Ce qui était `int compteur` dans le source devient une opération sur un registre ou une position sur la pile, sans aucune étiquette.  
- Les **noms de fonctions** peuvent être supprimés si le binaire est strippé (option `-s` de GCC). Même quand ils sont conservés, les fonctions `static` et les fonctions inlinées disparaissent du binaire final.  
- Les **commentaires** sont éliminés dès la phase de préprocessing — ils n'atteignent même pas le compilateur.  
- Les **types de données** ne sont pas conservés en tant que tels dans le code machine. Un `int`, un `unsigned int` et un pointeur occupent tous 4 ou 8 octets — le processeur ne fait pas la différence.  
- La **structure du code** (boucles `for`, `while`, `switch/case`, conditions `if/else`) est transformée en séquences de comparaisons et de sauts. Un `switch` à 5 cas peut devenir une table de sauts, une cascade de `cmp`/`jmp`, ou un arbre de décision binaire — selon le niveau d'optimisation.  
- Le **flux de contrôle** lui-même peut être profondément réorganisé par les passes d'optimisation du compilateur (inlining, déroulage de boucles, réordonnancement de blocs basiques, élimination de code mort).

Le reverse engineer travaille donc avec un matériau appauvri. Son travail consiste à **reconstruire du sens** à partir de ce qui reste : les instructions machine, les données en mémoire, les chaînes de caractères, les constantes numériques, les appels système, et la structure du binaire lui-même.

C'est cette reconstruction — parfois qualifiée de « remontée d'abstraction » — qui fait toute la difficulté et tout l'intérêt du RE.

### RE logiciel vs débogage : une distinction importante

Le reverse engineering est parfois confondu avec le débogage, et les deux activités partagent effectivement des outils (GDB, par exemple, est central dans les deux cas). Mais leurs objectifs et leurs contextes diffèrent fondamentalement :

Le **débogage** part d'un programme dont on possède le code source. On cherche à localiser un bug précis : une valeur incorrecte, un accès mémoire invalide, un comportement inattendu. Le code source est la référence — le débogueur aide simplement à observer l'exécution pour confirmer ou infirmer une hypothèse.

Le **reverse engineering** part d'un binaire dont on ne possède **pas** le code source (ou dont on ne peut pas se servir directement). L'objectif n'est pas de corriger un bug connu, mais de **comprendre la logique globale** du programme : que fait-il ? Comment traite-t-il ses entrées ? Quel protocole utilise-t-il ? Quelle routine de vérification applique-t-il ? Où stocke-t-il ses clés de chiffrement ?

En résumé : le débogage est une activité de vérification (on sait ce que le programme devrait faire, on cherche pourquoi il ne le fait pas). Le RE est une activité de découverte (on ne sait pas ce que le programme fait, on cherche à le comprendre).

> 💡 En pratique, les deux activités se chevauchent souvent. Un développeur qui débogue un crash dans une bibliothèque tierce sans code source fait du RE sans nécessairement le nommer ainsi. Et un reverse engineer utilise des techniques de débogage tout au long de son analyse. La distinction est avant tout une question d'intention et de contexte, pas d'outillage.

---

## Les objectifs du reverse engineering

Le RE n'est pas une fin en soi — c'est un moyen au service d'objectifs concrets. Selon le contexte, l'analyste cherche à atteindre un ou plusieurs des objectifs suivants.

### 1. Comprendre le comportement d'un programme

L'objectif le plus fondamental : déterminer **ce que fait** un binaire. Cela peut aller d'une vue d'ensemble (« ce programme est un client FTP qui se connecte au port 2121 ») à une compréhension fine d'une routine spécifique (« cette fonction implémente un XOR roulant avec une clé de 16 octets dérivée du timestamp »).

C'est l'objectif central de l'analyse de malware (Partie VI de cette formation) : face à un échantillon suspect, l'analyste doit déterminer ses capacités, ses cibles, ses mécanismes de persistance et de communication, sans aucune documentation.

C'est également l'objectif des compétitions CTF de type « reversing » : on vous donne un binaire, vous devez comprendre sa logique pour en extraire un flag.

### 2. Évaluer la sécurité d'un logiciel

Dans le cadre d'un audit de sécurité mandaté, le RE permet d'examiner un binaire pour y rechercher des vulnérabilités : dépassements de tampon, injections de format, use-after-free, conditions de course, faiblesses cryptographiques, secrets codés en dur (clés, mots de passe, tokens).

Cet objectif est central dans le domaine de la **recherche de vulnérabilités** (*vulnerability research*). Les chercheurs en sécurité analysent régulièrement des logiciels commerciaux — navigateurs, systèmes d'exploitation, firmwares — pour y découvrir des failles avant qu'elles ne soient exploitées. Le RE est la compétence technique qui rend cette recherche possible lorsque le code source n'est pas disponible.

### 3. Assurer l'interopérabilité

Quand un logiciel propriétaire utilise un protocole réseau non documenté ou un format de fichier fermé, le RE est parfois le seul moyen de comprendre ce protocole ou ce format pour développer une implémentation compatible.

L'exemple historique le plus connu est le projet Samba, qui a dû reverser le protocole SMB/CIFS de Microsoft pour permettre l'interopérabilité entre systèmes Linux et Windows. Plus récemment, de nombreux projets open source ont recours au RE pour interagir avec des périphériques dont les fabricants ne publient pas de spécifications (pilotes de cartes graphiques, contrôleurs USB, protocoles IoT).

> 💡 L'interopérabilité est l'un des cas d'usage où le cadre légal offre le plus de protections au reverse engineer. La directive européenne 2009/24/CE (qui a remplacé la directive 91/250/CEE) autorise explicitement la décompilation d'un programme à des fins d'interopérabilité sous certaines conditions. Le chapitre 1.2 détaille ce point.

### 4. Récupérer une implémentation perdue

Il arrive qu'une organisation dépende d'un logiciel critique dont le code source a été perdu, dont l'éditeur a disparu, ou dont la documentation est inexistante. Le RE permet de comprendre suffisamment la logique du programme pour le maintenir, le porter sur une nouvelle plateforme, ou le remplacer par une implémentation documentée.

Ce cas se rencontre fréquemment dans l'industrie (automates programmables, logiciels de contrôle industriel) et dans la préservation du patrimoine numérique (émulateurs de consoles, rétrocompatibilité).

### 5. Analyser un patch ou une mise à jour

Quand un éditeur publie un correctif de sécurité sans détailler la vulnérabilité corrigée (ce qui est la pratique standard pour ne pas faciliter l'exploitation), le RE permet de comparer la version vulnérable et la version corrigée du binaire pour identifier précisément ce qui a changé. Cette technique, appelée **patch diffing** (ou *binary diffing*), est traitée en détail au chapitre 10.

Elle est utilisée aussi bien par les équipes de sécurité défensive (pour évaluer l'urgence d'un déploiement) que par les chercheurs offensifs (pour développer des exploits ciblant les systèmes non encore patchés — dans un cadre de recherche autorisé).

### 6. Vérifier des propriétés de confiance

Dans certains contextes critiques (défense, infrastructures sensibles, certifications de sécurité), il est nécessaire de vérifier qu'un binaire livré par un fournisseur correspond bien à ce qui a été spécifié et ne contient pas de fonctionnalités non documentées : portes dérobées, mécanismes de collecte de données, appels réseau cachés.

Le RE est alors utilisé comme un outil de **vérification indépendante**, complémentaire aux audits de code source quand ceux-ci sont possibles — ou comme substitut quand le fournisseur refuse de communiquer ses sources.

### 7. Apprendre et comprendre en profondeur

C'est l'objectif le moins utilitaire mais peut-être le plus formateur : utiliser le RE pour **comprendre comment les choses fonctionnent réellement**. Désassembler un programme compilé avec GCC, c'est voir de ses propres yeux comment le compilateur traduit un `switch`, comment il organise une vtable C++, comment il implémente un appel système, comment il gère l'alignement mémoire.

Beaucoup de développeurs expérimentés affirment que le RE les a rendus meilleurs programmeurs — parce qu'il force à comprendre ce qui se passe réellement sous le capot, au-delà des abstractions du langage de haut niveau.

C'est dans cet esprit que cette formation a été conçue. Les techniques de RE que vous allez apprendre sont utiles en soi, mais elles sont aussi un moyen incomparable d'approfondir votre compréhension du fonctionnement des systèmes informatiques.

---

## Le reverse engineering dans cette formation

Cette formation se concentre sur le RE de **binaires natifs ELF**, compilés avec **GCC ou G++**, pour l'architecture **x86-64 sous Linux**. Ce périmètre est volontairement ciblé pour permettre une couverture en profondeur plutôt qu'un survol superficiel de toutes les plateformes.

Les objectifs que nous venons de lister seront tous mis en pratique au fil des chapitres :

| Objectif | Chapitres principaux |  
|---|---|  
| Comprendre le comportement | 5–9 (analyse statique), 11–13 (analyse dynamique), 21–25 (cas pratiques) |  
| Évaluer la sécurité | 15 (fuzzing), 19 (anti-reversing), 24 (crypto) |  
| Assurer l'interopérabilité | 23 (binaire réseau), 25 (format de fichier custom) |  
| Analyser un patch | 10 (diffing de binaires) |  
| Vérifier des propriétés | 27–29 (analyse de malware en lab isolé) |  
| Apprendre en profondeur | L'ensemble de la formation, et en particulier les chapitres 2–3 (compilation, assembleur) et 16–17 (optimisations, C++) |

La section 1.6 reviendra sur le périmètre exact de cette formation et le situera dans le paysage plus large du RE (bytecode managé, firmware, hardware).

---

> 📖 **À retenir** — Le reverse engineering de logiciels consiste à analyser un binaire compilé pour en comprendre le fonctionnement sans accès au code source. C'est un processus de reconstruction de sens à partir d'un matériau appauvri par la compilation. Ses objectifs vont de la compréhension pure à l'audit de sécurité, en passant par l'interopérabilité, l'analyse de patches et la vérification de confiance.

---


⏭️ [Cadre légal et éthique (licences, lois CFAA / EUCD / DMCA)](/01-introduction-re/02-cadre-legal-ethique.md)
