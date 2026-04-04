🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 31.3 — dotPeek (JetBrains) — navigation et export de sources

> 📦 **Chapitre 31 — Décompilation d'assemblies .NET**  
> 

---

## Présentation

dotPeek est le décompilateur .NET gratuit de **JetBrains**, l'éditeur de Rider, ReSharper et IntelliJ IDEA. Publié pour la première fois en 2011, il a été conçu dès l'origine pour offrir une expérience de navigation dans du code décompilé identique à celle d'un IDE JetBrains — et c'est précisément là que réside sa force.

dotPeek n'est ni open source comme ILSpy, ni orienté débogage comme dnSpy. Son positionnement est celui d'un **explorateur de code à la précision chirurgicale** : navigation instantanée, recherche multi-critères, affichage des relations entre types, et surtout intégration avec l'écosystème JetBrains. Si vous travaillez déjà avec Rider ou ReSharper, dotPeek s'intègre naturellement dans votre workflow.

Pour le reverse engineer, dotPeek occupe une niche spécifique. Il excelle quand l'objectif est de **comprendre l'architecture d'une application .NET volumineuse** — cartographier les dépendances, tracer les hiérarchies d'héritage, naviguer dans du code fortement typé avec des dizaines de namespaces. Là où ILSpy est un couteau suisse et dnSpy un atelier complet, dotPeek est une loupe de haute précision.

---

## Installation

dotPeek est disponible gratuitement sur le site de JetBrains. Téléchargez l'installeur Windows depuis la page produit et suivez l'assistant. Contrairement à ILSpy et dnSpy qui sont portables (simples archives ZIP), dotPeek utilise un installeur classique qui s'enregistre dans le menu Démarrer et le système de fichiers.

### Contraintes de plateforme

dotPeek est une application **Windows uniquement**, comme dnSpy. Il repose sur les technologies WPF et le framework .NET Framework (pas .NET 6+). Aucune version Linux ou macOS n'est disponible, et aucun port n'est prévu — JetBrains oriente les utilisateurs multiplateforme vers le décompilateur intégré à Rider.

### Licence

dotPeek est gratuit pour tout usage, y compris commercial. Il n'est pas open source : le code n'est pas disponible, et la licence est propriétaire (JetBrains). En pratique, cette distinction n'affecte pas l'usage en RE — l'outil est entièrement fonctionnel sans achat ni abonnement.

> 💡 Si vous utilisez déjà **Rider** (l'IDE C# multiplateforme de JetBrains), sachez qu'il intègre nativement le moteur de décompilation de dotPeek. Vous pouvez naviguer dans le code décompilé d'un assembly directement dans Rider via `Navigate > Go to Declaration` sur un type externe. Dans ce cas, installer dotPeek en standalone n'est nécessaire que si vous voulez travailler en dehors de Rider.

---

## Tour de l'interface

L'interface de dotPeek est calquée sur celle des IDE JetBrains. Les utilisateurs de Rider, IntelliJ ou WebStorm retrouveront immédiatement leurs repères : barre de navigation en haut, arbre de projet à gauche, éditeur de code au centre, panneaux d'outils ancrables sur les côtés et en bas.

### L'explorateur d'assemblies (Assembly Explorer)

Le panneau gauche affiche les assemblies chargés dans une arborescence hiérarchique similaire à celles d'ILSpy et dnSpy. La structure est la même — namespaces, types, membres — mais avec les icônes et la présentation visuelle caractéristiques de JetBrains. Chaque nœud affiche le modificateur d'accès et le type de membre de manière explicite.

Une particularité de dotPeek : l'explorateur distingue visuellement les **assemblies de votre analyse** (que vous avez ouvertes explicitement) des **assemblies de framework** (BCL, runtime) résolues automatiquement via les références. Cette distinction réduit le bruit visuel quand vous travaillez sur une application qui dépend de dizaines d'assemblies système.

### Le panneau de code

Le code C# décompilé s'affiche dans l'éditeur central avec coloration syntaxique, repliement de blocs et numérotation des lignes. dotPeek supporte les mêmes modes de visualisation qu'ILSpy — C# et IL — mais son mode C# bénéficie de la technologie de rendu de code de JetBrains, qui offre quelques raffinements visuels supplémentaires :

- **Coloration sémantique** : les variables locales, les paramètres, les champs d'instance, les champs statiques et les propriétés sont colorés différemment, même sans PDB. Cette distinction visuelle est un gain notable lors de la lecture de méthodes longues et complexes.  
- **Inlays et hints** : des annotations contextuelles affichées en gris clair directement dans le code — noms de paramètres aux sites d'appel, types inférés pour les variables `var`, valeurs des constantes. Ces hints sont configurables et désactivables.  
- **Surbrillance d'usage** : cliquer sur un symbole surligne automatiquement toutes ses occurrences dans le fichier courant, ce qui permet de tracer visuellement le flux d'une variable à travers une méthode.

### Panneaux d'outils

dotPeek propose plusieurs panneaux ancrables accessibles via le menu `View > Tool Windows` :

- **Find Results** : résultats de la dernière recherche, avec navigation par double-clic.  
- **Type Hierarchy** : arbre d'héritage complet d'un type sélectionné (classes de base, interfaces implémentées, classes dérivées). Ce panneau est un atout majeur pour le RE d'applications orientées objet complexes — il répond instantanément à la question « quelles classes implémentent cette interface ? » ou « qui hérite de cette classe abstraite ? ».  
- **IL Viewer** : affichage du bytecode CIL de la méthode sélectionnée, synchronisé avec le code C#. En cliquant sur une ligne C#, la vue IL se positionne sur les instructions CIL correspondantes, et vice versa. Cette synchronisation bidirectionnelle est plus fluide que le mode hybride « IL avec commentaires C# » d'ILSpy.  
- **Assembly Explorer** : le panneau d'arborescence décrit plus haut.

---

## Navigation : la force principale de dotPeek

dotPeek hérite du système de navigation de ReSharper/Rider, qui est le produit de plus de vingt ans de développement sur l'analyse de code .NET. C'est dans ce domaine que l'outil distance ses concurrents.

### Go to Everything (`Ctrl+N`)

La commande de navigation universelle de dotPeek. Tapez n'importe quel fragment de nom et dotPeek cherche dans l'ensemble des assemblies chargés — types, méthodes, propriétés, champs, chaînes — avec un algorithme de fuzzy matching intelligent. Le matching fonctionne sur les initiales en CamelCase : taper `VLK` trouve `ValidateLicenseKey`, taper `CrHlp` trouve `CryptoHelper`.

Cette navigation est qualitativement différente de la recherche textuelle d'ILSpy ou dnSpy. Elle ne cherche pas des sous-chaînes dans des noms : elle comprend la structure des identifiants .NET et propose des résultats pondérés par pertinence. Sur une application volumineuse avec des milliers de types, cette différence est palpable.

### Go to Type (`Ctrl+T`) et Go to Member (`Alt+\`)

Des versions ciblées de la navigation universelle : `Go to Type` filtre exclusivement les classes, structs, interfaces, enums et delegates ; `Go to Member` filtre les méthodes, propriétés, champs et événements. Ces raccourcis évitent le bruit quand vous savez quel type d'entité vous cherchez.

### Find Usages (`Alt+F7`)

L'équivalent de la commande *Analyze* d'ILSpy et dnSpy, mais avec une présentation plus structurée. Les résultats sont groupés par catégorie d'usage :

- **Invocations** : sites d'appel de la méthode.  
- **Accès en lecture / écriture** : pour les champs et propriétés.  
- **Héritages et implémentations** : pour les types et interfaces.  
- **Instanciations** : pour les constructeurs.  
- **Attributs** : usages en tant qu'attribut sur un type ou membre.

Chaque résultat affiche un extrait de code contextuel, pas seulement le nom de la méthode appelante. Ce contexte supplémentaire permet souvent de comprendre le rôle d'un appel sans même naviguer vers le code complet.

### Navigate To (menu contextuel)

Le clic droit sur un symbole dans le code décompilé ouvre un sous-menu **Navigate To** qui regroupe toutes les destinations possibles :

- **Declaration** : la définition du symbole.  
- **Base Symbols** : la méthode de base surchargée ou l'interface dont cette méthode est l'implémentation.  
- **Derived Symbols** : toutes les surcharges dans les classes dérivées.  
- **Containing Type** : la classe ou struct qui contient ce membre.  
- **Related Files** : les autres types définis dans le même module.

Pour le RE d'une application utilisant intensivement le polymorphisme, le pattern Strategy ou l'injection de dépendances, ces commandes de navigation sont indispensables. Quand vous voyez un appel à `IValidator.Validate(input)` et que vous voulez savoir *quelle implémentation concrète* sera exécutée, `Navigate To > Derived Symbols` vous donne immédiatement la liste de toutes les classes qui implémentent `IValidator`.

---

## Fonctionnalité Symbol Server

dotPeek possède une fonctionnalité unique que ni ILSpy ni dnSpy ne proposent : il peut fonctionner comme un **serveur de symboles local**. Activée via `Tools > Symbol Server`, cette fonctionnalité transforme dotPeek en un serveur PDB que Visual Studio ou Rider peuvent consommer.

### Principe de fonctionnement

Quand dotPeek génère le code décompilé d'un assembly, il peut simultanément produire un fichier **PDB synthétique** — un fichier de symboles de débogage qui mappe les offsets IL vers les lignes du code C# décompilé. En configurant Visual Studio pour interroger le serveur de symboles de dotPeek (une URL locale du type `http://localhost:33417/`), vous obtenez la capacité de :

- **Naviguer dans le code décompilé** directement depuis Visual Studio pendant une session de débogage standard.  
- **Poser des breakpoints** dans le code décompilé depuis Visual Studio (similaire à dnSpy, mais via le mécanisme standard de Visual Studio).  
- **Voir la pile d'appels** avec des noms de méthodes et des numéros de lignes correspondant au code décompilé.

### Intérêt pour le RE

Le Symbol Server est particulièrement utile dans deux scénarios :

**Scénario 1 — Débogage d'une application dont vous avez le projet, mais pas les sources d'une dépendance.** Vous déboguez votre propre code dans Visual Studio, et quand l'exécution entre dans une bibliothèque tierce dont vous n'avez que le `.dll`, Visual Studio utilise les PDB générés par dotPeek pour vous montrer le code décompilé au lieu d'un écran « Source Not Found ».

**Scénario 2 — Alternative à dnSpy.** Si vous ne pouvez pas (ou ne voulez pas) utiliser dnSpy, la combinaison dotPeek + Visual Studio offre une expérience de débogage sur code décompilé comparable, en passant par les mécanismes standard de Visual Studio. L'avantage est que vous bénéficiez de toute la puissance de Visual Studio (diagnostics, profiling, IntelliTrace) ; l'inconvénient est que la configuration est plus lourde et que l'intégration n'est pas aussi transparente que dans dnSpy.

---

## Export de sources

dotPeek permet d'exporter le code décompilé, mais avec des options différentes de celles d'ILSpy.

### Copie de code

La méthode la plus simple : sélectionnez le code dans le panneau central et copiez-le (`Ctrl+C`). Le code est copié avec la coloration syntaxique si vous le collez dans un éditeur qui la supporte. Pour une méthode ou un type entier, faites un clic droit sur le nœud dans l'arbre et choisissez `Copy` — le code décompilé complet est copié dans le presse-papier.

### Export en projet

`File > Export to Project` génère une arborescence de fichiers `.cs` avec un fichier de solution `.sln`. Cette fonctionnalité est comparable au `Save Code` d'ILSpy, mais l'export de dotPeek tend à produire un projet mieux structuré en termes de namespaces et de dossiers, reflétant la compréhension plus fine que dotPeek a de l'architecture de l'assembly grâce à son moteur d'analyse JetBrains.

Comme pour ILSpy, le projet exporté n'est pas garanti recompilable immédiatement — les dépendances manquantes, les références circulaires et les artefacts de décompilation peuvent nécessiter des corrections manuelles. Mais la base est solide pour une lecture dans un IDE complet.

---

## Workflow type en RE avec dotPeek

dotPeek brille dans un scénario précis : **vous faites face à une application .NET volumineuse et bien architecturée** (dizaines de namespaces, centaines de types, utilisation intensive d'interfaces et d'injection de dépendances), et votre objectif est de comprendre sa structure avant d'entrer dans les détails d'implémentation.

### Phase 1 — Vue d'ensemble architecturale

Chargez l'assembly principal et ses dépendances. Utilisez `Go to Everything` pour explorer les namespaces de haut niveau. Ouvrez le panneau **Type Hierarchy** sur les interfaces clés (`ILicenseService`, `IAuthProvider`, `IDataRepository`…) pour cartographier les implémentations concrètes.

### Phase 2 — Cartographie des dépendances

Notez les assemblies référencés dans l'explorateur. Pour chaque dépendance tierce, identifiez son rôle : framework web, ORM, bibliothèque crypto, système de logging, framework d'injection de dépendances. Cette cartographie vous indique *comment* l'application est construite, ce qui guide votre stratégie d'analyse.

### Phase 3 — Analyse ciblée

Une fois l'architecture comprise, utilisez `Find Usages` pour tracer les flux de données critiques. Partez d'un point d'entrée connu (une méthode de contrôleur, un handler d'événement, un endpoint d'API) et suivez les appels en profondeur. La coloration sémantique et les inlays de dotPeek rendent cette lecture particulièrement fluide.

### Phase 4 — Relais vers dnSpy

Quand vous avez identifié la méthode exacte que vous devez observer à l'exécution, basculez vers dnSpy pour le débogage dynamique. dotPeek vous a permis de trouver l'aiguille dans la botte de foin — dnSpy vous permet de l'examiner sous tous les angles.

---

## Forces et limites de dotPeek

### Forces

- **Navigation de classe mondiale** : le système de navigation hérité de ReSharper/Rider est le plus puissant de tous les décompilateurs .NET. La recherche fuzzy CamelCase, le `Find Usages` structuré et la `Type Hierarchy` sont des outils de productivité sans équivalent dans ILSpy ou dnSpy.  
- **Coloration sémantique et inlays** : la lisibilité du code décompilé est supérieure grâce à la distinction visuelle des catégories de symboles et aux annotations contextuelles.  
- **Symbol Server** : fonctionnalité unique permettant de déboguer dans Visual Studio avec du code décompilé via des PDB synthétiques.  
- **Vue IL synchronisée** : la synchronisation bidirectionnelle C#↔IL est plus fluide que dans les outils concurrents.  
- **Gratuit** : aucun coût, même pour un usage professionnel ou commercial.

### Limites

- **Windows uniquement** : même contrainte que dnSpy. Les utilisateurs Linux/macOS doivent se rabattre sur ILSpy Avalonia ou sur Rider.  
- **Pas de débogueur intégré** : comme ILSpy, dotPeek est un outil d'analyse statique. Le Symbol Server offre un pont vers Visual Studio, mais ce n'est pas aussi direct que le débogueur intégré de dnSpy.  
- **Pas d'édition d'assembly** : aucune possibilité de modifier le code IL ou C# et de sauvegarder. dotPeek est strictement en lecture seule.  
- **Propriétaire et fermé** : le code source n'est pas disponible. Vous ne pouvez pas étendre l'outil par des plugins (contrairement à ILSpy) ni auditer son comportement. Si JetBrains décide de discontinuer dotPeek ou de le rendre payant, vous n'avez pas de recours.  
- **Pas d'outil CLI** : dotPeek n'offre pas d'équivalent à `ilspycmd`. L'intégration dans des pipelines d'automatisation (chapitre 35) n'est pas possible directement.  
- **Assemblies obfusqués** : mêmes limites qu'ILSpy et dnSpy face à l'obfuscation — la navigation de qualité ne compense pas des noms de symboles rendus illisibles.

---

## Résumé

dotPeek est l'outil de choix quand la priorité est la **compréhension architecturale** d'une application .NET complexe. Sa navigation héritée de ReSharper, sa coloration sémantique et sa Type Hierarchy en font un explorateur de code décompilé sans rival. Il ne remplace ni ILSpy (export, multiplateforme, CLI) ni dnSpy (débogage, édition), mais il les complète sur le terrain spécifique de la lecture et de l'exploration de code à grande échelle. Le Symbol Server constitue par ailleurs un pont unique vers le débogage dans Visual Studio. La section suivante (31.4) formalise les forces respectives des trois outils dans un comparatif structuré.

---


⏭️ [Comparatif ILSpy vs dnSpy vs dotPeek](/31-decompilation-dotnet/04-comparatif-outils.md)
