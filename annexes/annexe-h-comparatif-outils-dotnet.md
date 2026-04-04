🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe H — Comparatif des outils .NET (ILSpy / dnSpy / dotPeek / de4dot)

> 📎 **Fiche de référence** — Cette annexe compare les outils de reverse engineering dédiés aux assemblies .NET (C#, F#, VB.NET). Elle couvre les décompilateurs, les débogueurs, les outils de déobfuscation et les utilitaires complémentaires. Elle est le pendant de l'Annexe G pour l'écosystème .NET, en lien direct avec les parties VII (chapitres 30–32) de la formation.

---

## Pourquoi des outils spécifiques pour le .NET ?

Le reverse engineering de binaires .NET est fondamentalement différent de celui des binaires natifs x86-64. Un assembly .NET ne contient pas de code machine : il contient du **bytecode CIL** (*Common Intermediate Language*), un langage intermédiaire de haut niveau compilé à la volée (JIT) par le runtime .NET au moment de l'exécution. Ce bytecode conserve une quantité considérable de métadonnées : noms de classes, méthodes, champs, types de paramètres, hiérarchie d'héritage, attributs, et souvent même les noms des variables locales.

Cette richesse de métadonnées signifie que la décompilation .NET produit un résultat **beaucoup plus fidèle** au code source original que la décompilation de binaires natifs. Un décompilateur .NET peut typiquement reconstruire un code C# presque identique à l'original, avec les bons noms de classes et de méthodes, les bons types, et une structure de contrôle fidèle. C'est un contraste saisissant avec la décompilation de binaires GCC, où Ghidra produit au mieux un pseudo-code C approximatif avec des noms de variables inventés.

La contrepartie est que les développeurs qui veulent protéger leur code .NET utilisent des **obfuscateurs** qui renomment les symboles, chiffrent les chaînes, aplatissent le flux de contrôle et ajoutent du code mort. Les outils de déobfuscation (comme de4dot) tentent de défaire ces transformations avant la décompilation.

Les outils présentés ici se répartissent en trois catégories : les décompilateurs (ILSpy, dnSpy/dnSpyEx, dotPeek), les débogueurs (dnSpy intègre un débogueur), et les déobfuscateurs (de4dot).

---

## 1 — Tableau comparatif principal

| Critère | **ILSpy** | **dnSpy / dnSpyEx** | **dotPeek** | **de4dot** |  
|---------|-----------|---------------------|-------------|------------|  
| **Catégorie** | Décompilateur | Décompilateur + débogueur | Décompilateur | Déobfuscateur |  
| **Licence** | MIT (open source) | GPL (open source) | Gratuit (propriétaire) | GPL (open source) |  
| **Mainteneur** | Communauté ICSharpCode | dnSpyEx (fork communautaire maintenu) | JetBrains | Communauté (maintenance réduite) |  
| **OS** | Windows, Linux, macOS (via Avalonia) | Windows | Windows | Windows, Linux (Mono/.NET) |  
| **Interface** | GUI | GUI | GUI | CLI |  
| **Décompilation C#** | Excellente | Excellente | Excellente | Non (prétraitement) |  
| **Décompilation VB.NET** | Oui | Oui | Oui | — |  
| **Décompilation F#** | Partielle | Partielle | Non | — |  
| **Décompilation IL** | Oui | Oui | Oui | — |  
| **Débogage intégré** | Non | **Oui** (breakpoints sur C# décompilé) | Non | Non |  
| **Édition / Patching IL** | Non | **Oui** (modification directe de l'IL et des métadonnées) | Non | — |  
| **Recherche de symboles** | Oui | Oui | Oui (avec serveur de symboles JetBrains) | — |  
| **Export de code source** | Oui (projet C# complet) | Oui (fichiers individuels ou projet) | Oui (projet C# complet) | — |  
| **Navigation xrefs** | Oui (Analyze) | Oui | Oui (avec ReSharper-like navigation) | — |  
| **Support NuGet / packages** | Oui | Oui | Oui (intégration JetBrains) | — |  
| **Gestion de l'obfuscation** | Limité (affiche le code obfusqué tel quel) | Limité (idem) | Limité | **Spécialisé** (contourne de nombreux obfuscateurs) |  
| **Support .NET Framework** | Oui | Oui | Oui | Oui |  
| **Support .NET Core / .NET 5+** | Oui | Oui | Oui | Partiel |  
| **Support NativeAOT** | Non (code natif) | Non | Non | Non |  
| **Taille d'installation** | ~30 Mo | ~50 Mo | ~200 Mo | ~10 Mo |  
| **Dernière activité** | Active (2024+) | Active (dnSpyEx, 2024+) | Active (JetBrains) | Faible (dernière release majeure ~2020) |  
| **Chapitres** | 31.1 | 31.2, 32.1, 32.4, 32.5 | 31.3 | 31.5 |

---

## 2 — ILSpy

### 2.1 — Présentation

ILSpy est le décompilateur .NET open source de référence, développé par l'équipe ICSharpCode (les créateurs de SharpDevelop). C'est un projet mature, activement maintenu et largement adopté par la communauté. Son moteur de décompilation (ICSharpCode.Decompiler) est aussi utilisé comme bibliothèque par d'autres outils et dans des pipelines CI/CD.

### 2.2 — Forces

**Qualité de décompilation** — Le moteur de décompilation d'ILSpy est considéré comme l'un des meilleurs disponibles. Il reconstruit fidèlement les constructions C# modernes : async/await, LINQ, pattern matching, tuples, records, types nullables, string interpolation. Il gère bien les différentes versions du langage C# (de 1.0 à 12+) et adapte la sortie à la version choisie.

**Multi-plateforme** — Depuis la version basée sur Avalonia, ILSpy fonctionne nativement sur Linux et macOS en plus de Windows. C'est le seul décompilateur .NET GUI qui tourne nativement sur les trois plateformes, ce qui le rend particulièrement intéressant pour les analystes qui travaillent sous Linux.

**Export complet** — ILSpy peut exporter un assembly entier sous forme de projet C# (`.csproj` + fichiers `.cs`), tentant de produire un code recompilable. Le résultat nécessite souvent des corrections manuelles, mais c'est un excellent point de départ.

**Bibliothèque réutilisable** — Le moteur `ICSharpCode.Decompiler` est disponible comme package NuGet et peut être intégré dans vos propres outils d'analyse automatisée. C'est un avantage significatif pour le scripting et le traitement batch.

**Extensible** — ILSpy supporte un système de plugins. Des extensions communautaires ajoutent des fonctionnalités comme la recherche de vulnérabilités, l'export vers d'autres formats, ou des vues supplémentaires.

### 2.3 — Limites

ILSpy ne propose ni débogage ni édition de l'IL. C'est un outil de **lecture seule** : vous pouvez examiner le code décompilé, naviguer dans les types, chercher des références, mais vous ne pouvez pas poser de breakpoint ni modifier le binaire. Pour le débogage, vous devrez compléter avec dnSpy ou Visual Studio. Pour le patching, vous aurez besoin de dnSpy, `ildasm`/`ilasm`, ou Mono.Cecil.

La gestion du code obfusqué est minimale : ILSpy affiche le code tel quel, avec les noms renommés et le flux de contrôle aplati. Il ne tente pas de contourner l'obfuscation. Vous devrez passer par de4dot en amont.

### 2.4 — Commandes et raccourcis essentiels

| Action | Raccourci / Méthode |  
|--------|---------------------|  
| Ouvrir un assembly | `File → Open` ou glisser-déposer |  
| Chercher un type / méthode | `Ctrl+Shift+F` (Search) ou `F3` |  
| Naviguer vers la définition | `F12` ou double-clic |  
| Analyser les références (xrefs) | Clic droit → `Analyze` |  
| Voir l'IL d'une méthode | Sélectionner la méthode → onglet IL |  
| Exporter en projet C# | Clic droit sur l'assembly → `Save Code...` |  
| Changer la version C# cible | `View → Options → Decompiler → C# version` |  
| Rechercher dans le code décompilé | `Ctrl+F` dans la vue code |  
| Copier le code d'une méthode | Sélectionner → `Ctrl+C` |

---

## 3 — dnSpy / dnSpyEx

### 3.1 — Présentation

dnSpy est l'outil le plus complet de cet écosystème car il combine trois fonctions en une : décompilateur, débogueur et éditeur. Le projet original (par 0xd4d) a été archivé en 2020, mais un fork communautaire activement maintenu, **dnSpyEx**, a pris le relais et continue le développement avec le support des runtimes .NET récents.

### 3.2 — Forces

**Débogage sur code décompilé** — La killer feature de dnSpy. Vous pouvez poser des breakpoints directement sur le code C# décompilé, inspecter les variables locales avec leurs noms et types reconstruits, parcourir la pile d'appels, et exécuter pas à pas — le tout **sans avoir le code source original**. C'est l'équivalent .NET de GDB+Ghidra combinés en un seul outil. Vous pouvez déboguer des assemblies du .NET Framework, de .NET Core et de .NET 5+.

**Édition de l'IL et des métadonnées** — dnSpy permet de modifier directement le code IL d'une méthode, de changer les valeurs de constantes, de renommer des types et des membres, de modifier les attributs et même d'ajouter ou supprimer des méthodes. Les modifications peuvent être sauvegardées dans un nouvel assembly. C'est l'outil de patching .NET par excellence.

**Édition en C#** — Au-delà de l'édition IL bas niveau, dnSpy permet d'éditer une méthode directement en C# : vous modifiez le code C# décompilé, et dnSpy le recompile en IL et le réinjecte dans l'assembly. C'est une fonctionnalité remarquable qui rend le patching .NET aussi simple que de modifier du code source.

**Qualité de décompilation** — Le moteur de décompilation de dnSpy est basé sur ILSpy (ICSharpCode.Decompiler), donc la qualité est comparable. dnSpy y ajoute des améliorations d'affichage et de navigation spécifiques.

**Interface unifiée** — Tout est dans la même fenêtre : arborescence des types, code décompilé, vue IL, débogueur, éditeur, recherche. La navigation est fluide et l'outil gère plusieurs assemblies simultanément.

### 3.3 — Limites

**Windows uniquement** — dnSpy et dnSpyEx ne fonctionnent que sous Windows. C'est leur principale limitation pour les analystes Linux. Si vous travaillez sous Linux, ILSpy (Avalonia) est votre alternative pour la décompilation, et vous pouvez utiliser le débogueur .NET en CLI (`dotnet-dump`, `dotnet-trace`) pour l'analyse dynamique.

**Projet en fork** — Le projet original a été abandonné. dnSpyEx est un fork communautaire qui maintient le projet, mais la dépendance envers des contributeurs bénévoles est un risque à long terme. À ce jour, dnSpyEx est activement maintenu et suit les nouvelles versions de .NET.

**Pas de support NativeAOT** — Comme tous les décompilateurs .NET, dnSpy ne peut pas décompiler les binaires compilés en NativeAOT (Ahead-Of-Time), qui sont du code natif sans bytecode CIL. Pour ces binaires, il faut revenir aux outils natifs (Ghidra, IDA).

### 3.4 — Commandes et raccourcis essentiels

| Action | Raccourci / Méthode |  
|--------|---------------------|  
| Ouvrir un assembly | `File → Open` ou glisser-déposer |  
| Chercher un type / méthode / chaîne | `Ctrl+Shift+K` |  
| Naviguer vers la définition | `F12` ou double-clic |  
| Revenir en arrière | `Ctrl+-` (navigation back) |  
| Analyser les références | Clic droit → `Analyze` |  
| Voir l'IL | Clic droit → `Show IL Code` |  
| **Poser un breakpoint** | `F9` (sur une ligne de code décompilé) |  
| **Lancer le débogage** | `Debug → Start Debugging` (`F5`) |  
| **Step over** | `F10` |  
| **Step into** | `F11` |  
| **Inspecter une variable** | Survoler la variable pendant le debug, ou panneau `Locals` |  
| **Éditer une méthode (C#)** | Clic droit sur la méthode → `Edit Method (C#)...` |  
| **Éditer une méthode (IL)** | Clic droit → `Edit IL Instructions...` |  
| **Modifier une constante** | Clic droit sur le champ → `Edit Field...` |  
| **Sauvegarder les modifications** | `File → Save Module...` |  
| Exporter en projet C# | `File → Export to Project...` |

### 3.5 — Workflow typique : contourner une vérification de licence

Ce workflow illustre la puissance combinée de dnSpy pour un cas d'usage RE courant (couvert en détail au chapitre 32.5) :

1. Ouvrir l'assembly dans dnSpy  
2. Chercher les chaînes liées à la licence (`Ctrl+Shift+K` → « license », « trial », « expired »)  
3. Naviguer vers le code qui utilise ces chaînes (double-clic sur le résultat)  
4. Identifier la méthode de vérification (souvent un `bool CheckLicense()` ou similaire)  
5. Poser un breakpoint (`F9`) et lancer le debug (`F5`) pour observer le flux  
6. Une fois la logique comprise, éditer la méthode (`Edit Method (C#)`) pour retourner `true` directement  
7. Sauvegarder le module modifié (`File → Save Module...`)

---

## 4 — dotPeek

### 4.1 — Présentation

dotPeek est le décompilateur .NET gratuit de JetBrains, la société derrière ReSharper, Rider et IntelliJ. Il bénéficie de l'expertise JetBrains en matière d'analyse de code et offre une expérience de navigation familière pour les utilisateurs de leurs IDE.

### 4.2 — Forces

**Qualité de décompilation** — Le moteur de décompilation de dotPeek est de très bonne qualité, comparable à ILSpy et dnSpy. JetBrains investit dans la maintenance de ce moteur car il alimente aussi la décompilation dans Rider et ReSharper.

**Navigation de type IDE** — dotPeek offre une expérience de navigation inspirée de ReSharper : recherche contextuelle, « Go to Declaration », « Find Usages », « Navigate To » avec filtrage par type, assemblage de graphes de dépendances. Pour un développeur C# habitué à Visual Studio + ReSharper, la prise en main est immédiate.

**Serveur de symboles** — dotPeek peut fonctionner comme un **serveur de symboles local** : il génère des fichiers PDB à la volée pour les assemblies décompilés, permettant à Visual Studio de poser des breakpoints et de déboguer en pas à pas sur le code décompilé. C'est une alternative au débogage intégré de dnSpy, bien que plus complexe à configurer.

**Gestion des packages NuGet** — dotPeek comprend nativement le format NuGet et peut ouvrir directement des packages `.nupkg` pour inspecter leur contenu.

**Export projet** — Comme ILSpy, dotPeek peut exporter un assembly en projet C# complet.

### 4.3 — Limites

**Windows uniquement** — dotPeek ne fonctionne que sous Windows.

**Propriétaire** — Bien que gratuit, dotPeek n'est pas open source. Vous ne pouvez pas modifier son comportement, l'intégrer dans un pipeline automatisé, ou accéder à son moteur de décompilation de manière programmatique (contrairement au moteur d'ILSpy disponible comme package NuGet).

**Pas d'édition ni de patching** — dotPeek est strictement en lecture seule. Aucune possibilité de modifier l'assembly, que ce soit en IL ou en C#.

**Taille d'installation** — dotPeek s'installe via le JetBrains Toolbox ou un installeur dédié, et pèse environ 200 Mo. C'est significativement plus lourd qu'ILSpy ou dnSpy.

**Pas de décompilation F#** — dotPeek ne supporte pas la décompilation vers F#. Pour les assemblies F#, le code est décompilé en C# (ce qui est souvent lisible mais perd les idiomes F#).

### 4.4 — Commandes et raccourcis essentiels

| Action | Raccourci / Méthode |  
|--------|---------------------|  
| Ouvrir un assembly | `File → Open` ou glisser-déposer |  
| Chercher partout | `Ctrl+T` (Go to Everything) |  
| Chercher un type | `Ctrl+N` |  
| Naviguer vers la définition | `F12` |  
| Trouver les usages | `Shift+F12` (Find Usages) |  
| Revenir en arrière | `Ctrl+-` |  
| Voir l'IL | `Windows → IL Viewer` |  
| Exporter en projet C# | Clic droit sur l'assembly → `Export to Project` |  
| Activer le serveur de symboles | `Tools → Symbol Server` |

---

## 5 — de4dot

### 5.1 — Présentation

de4dot est un déobfuscateur et nettoyeur d'assemblies .NET en ligne de commande. Il détecte automatiquement l'obfuscateur utilisé et applique les transformations inverses : restauration des noms de types et de méthodes, déchiffrement des chaînes, simplification du flux de contrôle, suppression du code mort et des protections anti-décompilation. Il est conçu pour être exécuté **avant** la décompilation avec ILSpy ou dnSpy.

### 5.2 — Obfuscateurs supportés

de4dot reconnaît et contourne (avec des degrés de succès variables) les obfuscateurs suivants :

| Obfuscateur | Niveau de support de4dot | Présence sur le marché |  
|-------------|--------------------------|------------------------|  
| **ConfuserEx** | Bon (renommage, chaînes, flux) | Très répandu (open source, gratuit) |  
| **Dotfuscator** | Bon (renommage, chaînes) | Répandu (inclus avec Visual Studio) |  
| **SmartAssembly** | Bon (renommage, chaînes, compression) | Courant (RedGate) |  
| **Babel.NET** | Partiel | Moins courant |  
| **Crypto Obfuscator** | Partiel | Courant |  
| **Eazfuscator.NET** | Partiel (chaînes, renommage) | Courant |  
| **.NET Reactor** | Partiel (renommage, chaînes ; le packing natif nécessite d'autres outils) | Très répandu |  
| **Agile.NET** (CliSecure) | Partiel | Moins courant |  
| **MaxtoCode** | Partiel | Marché chinois |  
| **Goliath.NET** | Basique | Rare |  
| **Obfuscateurs custom** | Détection heuristique + nettoyage générique | Variable |

Le niveau de support dépend de la version de l'obfuscateur. Les protections évoluent en permanence, et de4dot n'étant plus activement développé pour les dernières versions de ces outils, les résultats peuvent être partiels sur les protections récentes.

### 5.3 — Commandes essentielles

| Commande | Description |  
|----------|-------------|  
| `de4dot assembly.exe` | Détecte automatiquement l'obfuscateur et nettoie l'assembly. Produit `assembly-cleaned.exe` |  
| `de4dot assembly.dll -o output.dll` | Spécifie le fichier de sortie |  
| `de4dot assembly.exe -p cr` | Force la détection du type d'obfuscateur (`cr` = Crypto Obfuscator) |  
| `de4dot assembly.exe -p un` | Mode « unknown » : applique les heuristiques génériques sans supposer un obfuscateur spécifique |  
| `de4dot assembly.exe --dont-rename` | Nettoie sans renommer les symboles (utile si de4dot renomme mal) |  
| `de4dot assembly.exe --keep-types` | Préserve les types existants lors du nettoyage |  
| `de4dot *.dll` | Traite plusieurs assemblies d'un coup (utile quand une application est répartie sur plusieurs DLL) |

### 5.4 — Workflow avec de4dot

Le workflow standard est d'exécuter de4dot en premier, puis d'ouvrir le résultat dans un décompilateur :

1. `de4dot application.exe` → produit `application-cleaned.exe`  
2. Ouvrir `application-cleaned.exe` dans ILSpy ou dnSpy  
3. Vérifier si les noms sont restaurés et les chaînes déchiffrées  
4. Si le résultat est insuffisant, essayer avec `-p un` (mode générique) ou des options plus agressives  
5. Compléter manuellement dans le décompilateur si nécessaire

### 5.5 — Limites

**Maintenance réduite** — Le développement actif de de4dot a ralenti. Les dernières versions majeures datent d'environ 2020. Les obfuscateurs récents ou mis à jour peuvent ne plus être correctement détectés. Des forks communautaires existent et tentent de maintenir le support.

**Pas de solution miracle** — Certaines protections avancées (virtualisation du code IL, packing natif, intégration de code natif via mixed assemblies) dépassent les capacités de de4dot. Pour ces cas, un travail manuel combinant analyse dynamique (dnSpy en debug) et outils spécialisés est nécessaire.

**Support .NET Core / .NET 5+ limité** — de4dot a été principalement développé pour le .NET Framework. Le support des assemblies .NET Core et .NET 5+ peut être incomplet.

---

## 6 — Outils complémentaires

Au-delà des quatre outils principaux, plusieurs outils complémentaires méritent d'être mentionnés pour un toolkit .NET complet.

### 6.1 — Outils Microsoft

| Outil | Usage | Gratuit | Interface |  
|-------|-------|---------|-----------|  
| `ildasm` | Désassembleur IL officiel Microsoft — produit du code IL textuel | Oui (SDK .NET) | GUI + CLI |  
| `ilasm` | Assembleur IL — recompile du code IL textuel en assembly | Oui (SDK .NET) | CLI |  
| `dotnet-dump` | Capture et analyse de dumps mémoire de processus .NET | Oui (.NET CLI) | CLI |  
| `dotnet-trace` | Capture de traces d'exécution (ETW events) | Oui (.NET CLI) | CLI |  
| `dotnet-counters` | Monitoring temps réel des compteurs de performance .NET | Oui (.NET CLI) | CLI |  
| `PEVerify` / `ILVerify` | Vérifie la validité du code IL (utile après patching) | Oui (SDK .NET) | CLI |

La paire `ildasm`/`ilasm` constitue le pipeline de patching IL le plus basique : `ildasm` décompile l'assembly en texte IL, vous modifiez le texte avec un éditeur, puis `ilasm` le recompile. C'est plus laborieux que l'édition directe dans dnSpy, mais cela fonctionne sur toutes les plateformes et ne dépend d'aucun outil tiers.

### 6.2 — Bibliothèques de manipulation programmatique

| Bibliothèque | Usage | Licence |  
|--------------|-------|---------|  
| **Mono.Cecil** | Lecture et modification d'assemblies .NET en C# (le LIEF du monde .NET) | MIT |  
| **dnlib** | Lecture et modification d'assemblies .NET (utilisé par dnSpy et de4dot) | MIT |  
| **ICSharpCode.Decompiler** | Moteur de décompilation d'ILSpy, utilisable comme bibliothèque NuGet | MIT |  
| **System.Reflection.Metadata** | API officielle Microsoft pour lire les métadonnées .NET (lecture seule) | MIT |

**Mono.Cecil** est l'équivalent .NET de LIEF pour les binaires natifs : il permet de lire un assembly, d'inspecter et modifier ses types, méthodes, instructions IL, attributs, puis de sauvegarder le résultat. C'est l'outil de choix pour le patching programmatique d'assemblies .NET dans des scripts d'automatisation.

**dnlib** est la bibliothèque utilisée en interne par dnSpy et de4dot. Elle est plus complète que Mono.Cecil pour certains cas de figure (assemblies obfusqués, formats malformés) car elle a été durcie pour gérer les assemblies « cassés » que les obfuscateurs produisent intentionnellement.

### 6.3 — Frida pour .NET (`frida-clr`)

Frida (couvert en détail au chapitre 13 pour les binaires natifs) supporte aussi le runtime .NET via le bridge `frida-clr`. Il permet de hooker des méthodes .NET à la volée, de modifier des arguments et des valeurs de retour, et d'inspecter les objets managés en mémoire. C'est l'équivalent .NET du hooking Frida natif, couvert au chapitre 32.2.

---

## 7 — Matrice de décision : quel outil pour quel besoin ?

| Besoin | Outil recommandé | Alternative |  
|--------|------------------|-------------|  
| Décompiler un assembly .NET (lecture seule) | **ILSpy** | dotPeek |  
| Décompiler sous Linux ou macOS | **ILSpy** (Avalonia) | CLI : `dotnet-ildasm` |  
| Déboguer un assembly sans les sources | **dnSpy/dnSpyEx** | dotPeek (serveur de symboles) + Visual Studio |  
| Patcher un assembly (modifier le comportement) | **dnSpy/dnSpyEx** (édition C# ou IL) | `ildasm` → éditeur texte → `ilasm` |  
| Patching programmatique (scripting) | **Mono.Cecil** ou **dnlib** | — |  
| Déobfusquer un assembly (ConfuserEx, Dotfuscator, etc.) | **de4dot** puis ILSpy/dnSpy | Déobfuscation manuelle dans dnSpy |  
| Hooker des méthodes .NET en live | **Frida** (`frida-clr`) | dnSpy (breakpoints + modification registres) |  
| Intercepter des appels P/Invoke (.NET → natif) | **Frida** | `strace` + `ltrace` |  
| Analyser un assembly NativeAOT / ReadyToRun | **Ghidra** ou **IDA** (outils natifs) | — |  
| Exporter un assembly complet en projet C# | **ILSpy** ou **dotPeek** | dnSpy |  
| Automatiser la décompilation (batch/CI) | **ICSharpCode.Decompiler** (NuGet) | `ilspycmd` (CLI d'ILSpy) |

---

## 8 — Comparaison de la qualité de décompilation

Les trois décompilateurs (ILSpy, dnSpy, dotPeek) produisent des résultats très similaires car ils sont tous basés sur des moteurs matures qui manipulent les mêmes métadonnées CIL. Les différences apparaissent principalement dans les cas limites.

### 8.1 — Code non obfusqué

Sur du code C# standard non obfusqué, les trois outils produisent un résultat quasi identique et très fidèle au code source original. Les noms de classes, méthodes, propriétés et la plupart des noms de variables locales sont correctement restaurés. Les constructions modernes du langage (async/await, LINQ, pattern matching, etc.) sont généralement bien reconstruites par les trois.

Les différences mineures portent sur le style de formatage (placement des accolades, espaces) et sur certains choix de reconstruction : l'un peut produire un `foreach` là où l'autre produit un `for` avec index, par exemple. Ces différences sont cosmétiques et n'affectent pas la compréhension.

### 8.2 — Code obfusqué

Face à du code obfusqué, les trois décompilateurs se comportent de la même manière : ils affichent le code tel quel, avec les noms renommés en séquences illisibles (`\u0001`, `a`, `A`, etc.) et le flux de contrôle aplati. Aucun des trois ne tente de désobfusquer activement — c'est le rôle de de4dot en prétraitement.

La différence se fait sur la robustesse face aux assemblies intentionnellement malformés (métadonnées invalides, instructions IL illégales, structures récursives). dnSpy/dnlib est généralement le plus robuste car sa bibliothèque sous-jacente a été spécialement durcie pour ces cas. ILSpy peut parfois échouer à charger un assembly qu'un obfuscateur a volontairement « cassé » pour bloquer la décompilation.

### 8.3 — Code optimisé (Release)

Les binaires compilés en mode Release avec optimisations activées produisent un IL légèrement différent du mode Debug : certaines variables locales sont éliminées, des branches sont simplifiées, et l'inlining peut fusionner de petites méthodes. Les trois décompilateurs gèrent bien ces optimisations, mais le code décompilé peut être légèrement moins lisible que le code source original (variables temporaires manquantes, expressions plus compactes).

---

## 9 — NativeAOT et ReadyToRun : quand le .NET devient natif

Les technologies récentes de compilation .NET méritent une mention spéciale car elles changent fondamentalement l'approche de RE :

**ReadyToRun (R2R)** — Le code CIL est pré-compilé en code natif mais **le bytecode CIL est conservé** dans l'assembly aux côtés du code natif. Les décompilateurs .NET fonctionnent toujours car ils lisent le CIL, pas le code natif. Le code natif R2R n'est utilisé que pour accélérer le démarrage. En RE, vous pouvez ignorer la partie R2R et travailler sur le CIL normalement.

**NativeAOT** — L'assembly est compilé entièrement en code natif. **Le bytecode CIL est supprimé**. Le binaire résultant est un exécutable natif ELF ou PE, sans dépendance au runtime .NET. Les décompilateurs .NET (ILSpy, dnSpy, dotPeek) ne peuvent pas l'analyser. Il faut utiliser les outils de RE natifs : Ghidra, IDA, Radare2. Le code conserve certaines caractéristiques reconnaissables (structures de données du runtime, GC, gestion des exceptions) mais perd les métadonnées riches qui rendaient la décompilation .NET si efficace.

NativeAOT est encore minoritaire dans l'écosystème .NET, mais son adoption croît. Si un `file` sur un assembly .NET retourne « ELF 64-bit executable » au lieu de « PE32 executable (console) Intel 80386 Mono/.Net assembly », vous êtes probablement face à du NativeAOT et les outils de la Partie VII ne s'appliquent pas — basculez vers les outils de la Partie II.

---

## 10 — Tableau récapitulatif en une page

```
╔══════════════════════════════════════════════════════════════════╗
║              OUTILS .NET RE — AIDE-MÉMOIRE                       ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                  ║
║  DÉCOMPILATEURS                                                  ║
║  ┌──────────┬─────────────┬──────────────┬──────────────┐        ║
║  │          │   ILSpy     │ dnSpy(Ex)    │  dotPeek     │        ║
║  ├──────────┼─────────────┼──────────────┼──────────────┤        ║
║  │ Décompil.│     ✓       │      ✓       │      ✓       │        
║  │ Debug    │     ✗       │      ✓       │   (via PDB)  │        
║  │ Patching │     ✗       │      ✓       │      ✗       │        
║  │ Linux    │     ✓       │      ✗       │      ✗       │        
║  │ OSS      │     ✓       │      ✓       │      ✗       │        
║  └──────────┴─────────────┴──────────────┴──────────────┘        ║
║                                                                  ║
║  PIPELINE RECOMMANDÉ                                             ║
║  1. de4dot assembly.exe        (si obfusqué)                     ║
║  2. dnSpy → ouvrir le résultat → debug + patch                   ║
║  3. ILSpy → export projet C# si besoin de code complet           ║
║                                                                  ║
║  BIBLIOTHÈQUES PROGRAMMATIQUES                                   ║
║  Mono.Cecil / dnlib    → lire et modifier des assemblies         ║
║  ICSharpCode.Decompiler → décompiler en C# depuis du code        ║
║                                                                  ║
║  CAS SPÉCIAUX                                                    ║
║  NativeAOT → plus de CIL → utiliser Ghidra/IDA (outils natifs)   ║
║  ReadyToRun → CIL toujours présent → outils .NET normaux         ║
║  P/Invoke → pont .NET→natif → Frida ou strace côté natif         ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
```

---

> 📚 **Pour aller plus loin** :  
> - **Annexe G** — [Comparatif des outils natifs](/annexes/annexe-g-comparatif-outils-natifs.md) — le même comparatif pour les binaires ELF natifs.  
> - **Chapitre 30** — [Introduction au RE .NET](/30-introduction-re-dotnet/README.md) — différences CIL vs natif, structure d'un assembly .NET.  
> - **Chapitre 31** — [Décompilation d'assemblies .NET](/31-decompilation-dotnet/README.md) — couverture pédagogique d'ILSpy, dnSpy et dotPeek.  
> - **Chapitre 32** — [Analyse dynamique et hooking .NET](/32-analyse-dynamique-dotnet/README.md) — débogage avec dnSpy, hooking avec Frida, patching IL.  
> - **ILSpy** — [https://github.com/icsharpcode/ILSpy](https://github.com/icsharpcode/ILSpy)  
> - **dnSpyEx** — [https://github.com/dnSpyEx/dnSpy](https://github.com/dnSpyEx/dnSpy)  
> - **dotPeek** — [https://www.jetbrains.com/decompiler/](https://www.jetbrains.com/decompiler/)  
> - **de4dot** — [https://github.com/de4dot/de4dot](https://github.com/de4dot/de4dot)  
> - **Mono.Cecil** — [https://github.com/jbevain/cecil](https://github.com/jbevain/cecil)

⏭️ [Patterns GCC reconnaissables à l'assembleur (idiomes compilateur)](/annexes/annexe-i-patterns-gcc.md)
