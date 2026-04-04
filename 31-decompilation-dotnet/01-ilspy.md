🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 31.1 — ILSpy — décompilation C# open source

> 📦 **Chapitre 31 — Décompilation d'assemblies .NET**  
> 


---

## Présentation d'ILSpy

ILSpy est le décompilateur .NET open source de référence. Développé et maintenu par l'équipe **ICSharpCode** (les mêmes auteurs que SharpDevelop, l'un des premiers IDE C# libres), il est publié sous licence MIT et bénéficie d'une communauté active depuis sa création en 2011. Son objectif est simple : transformer un assembly .NET compilé en code C# lisible, navigable et exportable.

Dans l'écosystème du reverse engineering .NET, ILSpy occupe une place particulière. Contrairement à dnSpy qui mise sur le débogage intégré, ou à dotPeek qui s'appuie sur l'écosystème JetBrains, ILSpy se concentre sur la **qualité de la décompilation** et la **fidélité du code produit**. C'est l'outil que la plupart des analystes lancent en premier pour obtenir une vue d'ensemble rapide d'un assembly inconnu.

---

## Installation

### Windows (version WPF — recommandée)

La version historique et la plus complète d'ILSpy est l'application WPF pour Windows. Vous pouvez l'obtenir de plusieurs manières :

- **Release GitHub** : téléchargez l'archive ZIP depuis le dépôt `icsharpcode/ILSpy` sur GitHub, dans l'onglet *Releases*. Décompressez et lancez `ILSpy.exe` — aucune installation n'est requise.  
- **Microsoft Store** : ILSpy est disponible gratuitement sur le Microsoft Store, ce qui simplifie les mises à jour.  
- **Chocolatey** : `choco install ilspy`.  
- **winget** : `winget install icsharpcode.ILSpy`.

### Linux et macOS (version Avalonia)

Depuis la version 8.0, ILSpy propose une interface basée sur **Avalonia UI**, le framework multiplateforme .NET. Cette version fonctionne nativement sous Linux et macOS sans passer par Wine ou Mono :

```bash
# Via le .NET tool global (nécessite le SDK .NET 8+)
dotnet tool install --global ilspycmd

# Pour l'interface graphique Avalonia, téléchargez le build
# correspondant à votre OS depuis les releases GitHub
```

La version Avalonia est fonctionnellement proche de la version WPF, mais certaines fonctionnalités avancées (notamment certains plugins) peuvent ne pas encore être portées. Pour un usage de RE quotidien, elle est parfaitement suffisante.

> 💡 **Si vous travaillez dans la VM Linux** configurée au chapitre 4, la version Avalonia est le choix naturel. Si vous avez un poste Windows dédié au RE .NET, préférez la version WPF pour bénéficier de l'ensemble complet des fonctionnalités.

### Extension VS Code

ILSpy est également disponible sous forme d'extension pour Visual Studio Code (`icsharpcode.ilspy-vscode`). Elle permet de décompiler un assembly directement depuis l'explorateur de fichiers de VS Code. Cette extension est pratique pour des inspections rapides, mais elle n'offre pas la profondeur de navigation de l'application standalone — considérez-la comme un complément, pas un remplacement.

---

## Tour de l'interface

Au lancement, ILSpy présente une interface organisée autour de trois zones principales.

### L'arbre des assemblies (panneau gauche)

C'est le point d'entrée de toute analyse. Cet arbre hiérarchique affiche les assemblies chargés et leur contenu structuré selon les métadonnées .NET :

```
📁 MonApplication.exe
├── 📁 References
│   ├── mscorlib
│   ├── System
│   └── System.Core
├── 📁 MonApplication (namespace racine)
│   ├── 📁 Controllers
│   │   └── 📄 MainController
│   │       ├── .ctor()
│   │       ├── ProcessInput(string) : bool
│   │       └── ValidateLicense(string) : LicenseResult
│   ├── 📁 Models
│   │   ├── 📄 User
│   │   └── 📄 LicenseResult
│   └── 📁 Utils
│       └── 📄 CryptoHelper
└── 📁 Metadata
    ├── Assembly Attributes
    └── Module Attributes
```

Chaque nœud de l'arbre est cliquable et affiche le code décompilé correspondant dans le panneau central. Les icônes indiquent visuellement le type de membre (classe, interface, enum, méthode, propriété, champ) et son modificateur d'accès (cadenas pour `private`, losange pour `protected`, etc.).

Pour charger un assembly, vous pouvez soit utiliser `File > Open`, soit glisser-déposer le fichier `.dll` ou `.exe` directement dans la fenêtre.

### Le panneau de code (zone centrale)

C'est ici que le code décompilé s'affiche. Par défaut, ILSpy décompile en **C#**, mais il propose plusieurs modes de visualisation accessibles via la barre d'outils ou le menu déroulant de langage :

- **C#** : le mode par défaut et le plus utile pour le RE. Le code produit est du C# syntaxiquement valide et souvent recompilable.  
- **IL** : affiche le bytecode CIL brut, instruction par instruction. Indispensable quand la décompilation C# masque un détail ou quand vous soupçonnez un artefact d'obfuscation.  
- **IL avec commentaires C#** : un mode hybride qui intercale le CIL et le C# correspondant — très utile pour comprendre la correspondance entre les deux niveaux.  
- **ReadyToRun** : si l'assembly contient du code précompilé R2R (cf. section 30.5), ce mode affiche le code natif généré.

Le code affiché dans ce panneau supporte la **coloration syntaxique**, le **repliement de blocs** (folding), et surtout la **navigation par clic** : cliquer sur un nom de type, de méthode ou de champ vous emmène directement à sa définition, exactement comme dans un IDE.

### La barre de recherche

Accessible via `Ctrl+Shift+F` ou l'icône de recherche, elle permet de chercher dans l'ensemble des assemblies chargés. La recherche porte sur les noms de types, les membres, les constantes littérales (chaînes de caractères, nombres) et les attributs. C'est l'équivalent fonctionnel de la commande `strings` pour le monde .NET, mais avec une conscience complète de la structure du code.

Vous pouvez filtrer la recherche par catégorie (type, méthode, champ, propriété, événement, chaîne littérale) et par modificateur d'accès, ce qui est extrêmement pratique pour localiser rapidement une routine de vérification de licence ou une méthode de déchiffrement.

---

## Fonctionnalités clés pour le RE

### Navigation par références croisées

Comme dans Ghidra avec les XREF (chapitre 8, section 8.7), la capacité de suivre les références est fondamentale en RE. ILSpy offre deux commandes essentielles, accessibles par clic droit sur n'importe quel symbole :

- **Analyze** (`Ctrl+R`) : ouvre un panneau dédié qui affiche toutes les utilisations d'un symbole, organisées en catégories — « Used By », « Uses », « Exposed By », etc. C'est l'équivalent des XREF de Ghidra, mais avec la granularité des métadonnées .NET.  
- **Go to Definition** (`F12`) : navigue vers la définition du type ou du membre sélectionné, y compris dans les assemblies référencés.

En RE .NET, la stratégie typique est de localiser un point d'intérêt (une chaîne suspecte, un nom de méthode évocateur comme `CheckLicense` ou `DecryptPayload`), puis de remonter les références pour comprendre le contexte d'appel. ILSpy rend ce processus fluide grâce à l'historique de navigation (`Alt+←` / `Alt+→`) qui fonctionne comme dans un navigateur web.

### Décompilation de versions C# récentes

Un point fort d'ILSpy est son support des constructions C# modernes. Le décompilateur ne se contente pas de produire un équivalent fonctionnel : il tente de **reconnaître les patterns** générés par le compilateur Roslyn et de les restituer sous leur forme syntaxique C# originale. Cela inclut :

- **`async` / `await`** : le compilateur transforme les méthodes asynchrones en machines à états complexes (classes générées implémentant `IAsyncStateMachine`). ILSpy reconstruit la méthode `async` originale avec ses `await`, ce qui est infiniment plus lisible que la machine à états brute.  
- **Pattern matching** (`switch` sur des types, `is` avec variable de décomposition) : le CIL généré est une cascade de vérifications de type et de casts. ILSpy les replie en syntaxe `switch` / `is` / `when`.  
- **Records** et **init-only setters** (C# 9+) : reconnus et restitués.  
- **Expressions `using`** (sans bloc) : le compilateur génère un `try/finally` avec `Dispose()`, ILSpy le reconvertit en `using`.  
- **Tuples nommés** : les `ValueTuple<T1, T2, ...>` sont affichés avec la syntaxe `(int x, string y)`.  
- **Nullable reference types** : les annotations `?` sont reconstruites à partir des attributs de métadonnées.

Vous pouvez contrôler le niveau de version C# utilisé pour la décompilation via `View > Options > Decompiler > C# Language Version`. Baisser la version peut être utile pour voir le code « tel que le compilateur le génère réellement » plutôt que la version sucrée syntaxiquement.

### Export de projet complet

Une fonctionnalité particulièrement utile d'ILSpy pour le RE approfondi est la possibilité d'exporter un assembly entier sous forme de **projet C# Visual Studio** (`.csproj`) :

```
Clic droit sur l'assembly > Save Code...
```

ILSpy génère alors une arborescence de fichiers `.cs` reproduisant la structure de namespaces, avec un fichier `.csproj` configuré. Ce projet exporté n'est pas toujours immédiatement recompilable (il peut manquer des dépendances ou contenir des ambiguïtés liées à la décompilation), mais il constitue une base de travail solide pour :

- **Lire le code dans un vrai IDE** (Visual Studio, Rider, VS Code) avec toute la puissance de l'IntelliSense, du *Find All References*, et du refactoring.  
- **Tenter une recompilation** pour valider votre compréhension : si le projet recompile et produit un comportement identique, votre analyse est correcte.  
- **Modifier et recompiler** une version instrumentée du programme, par exemple pour ajouter des traces de débogage.

> ⚠️ L'export de projet est un outil de **compréhension**, pas un outil de piratage. Recompiler un assembly propriétaire pour le redistribuer violerait la plupart des licences logicielles et les lois sur la propriété intellectuelle mentionnées au chapitre 1, section 1.2.

---

## Modes de visualisation avancés

### Vue IL (bytecode CIL)

Basculer en mode IL est indispensable dans plusieurs situations de RE :

- **Quand la décompilation C# semble incorrecte** : le décompilateur peut parfois mal interpréter un pattern, surtout si le code a été compilé avec un ancien compilateur ou obfusqué. La vue IL vous montre exactement ce qui est dans l'assembly.  
- **Pour comprendre les performances** : les instructions CIL comme `callvirt` vs `call`, `box`/`unbox`, ou les séquences `ldloc`/`stloc` révèlent des détails que le C# décompilé masque.  
- **Pour détecter l'obfuscation** : un code IL contenant des séquences absurdes (`nop` en masse, sauts vers des sauts, variables jamais utilisées) trahit l'intervention d'un obfuscateur (cf. section 31.5).

La vue IL d'ILSpy affiche les instructions avec leurs offsets, les métadonnées de tokens résolues en noms lisibles, et les blocs d'exception (`try`/`catch`/`finally`/`fault`) clairement délimités. C'est bien plus lisible que la sortie brute d'`ildasm` (l'outil Microsoft historique).

### Vue des métadonnées

Accessible via les nœuds « Metadata » dans l'arbre, cette vue permet d'inspecter les tables de métadonnées brutes de l'assembly — TypeDef, MethodDef, FieldDef, MemberRef, AssemblyRef, etc. C'est l'équivalent .NET de `readelf -S` pour les sections ELF (chapitre 5, section 5.2) : vous voyez la structure interne telle que le runtime la consomme.

Cette vue est particulièrement utile pour :

- Vérifier les attributs d'assembly (version, culture, strong name, signature).  
- Identifier les dépendances exactes (versions des assemblies référencés).  
- Détecter des entrées de métadonnées anormales laissées par un obfuscateur.

---

## Workflow type en RE avec ILSpy

Pour un assembly .NET inconnu, voici la démarche typique avec ILSpy, organisée en phases progressives.

### Phase 1 — Chargement et reconnaissance

Ouvrez l'assembly dans ILSpy et commencez par examiner l'arbre sans cliquer sur aucune méthode. Notez :

- Les **namespaces** présents : ils révèlent l'architecture de l'application (`Controllers`, `Services`, `Models`, `Data`, `Security`, `Licensing`…).  
- Les **noms de classes** : contrairement aux binaires ELF strippés, vous avez ici les noms originaux du développeur. Une classe `LicenseValidator` ou `CryptoEngine` se repère immédiatement.  
- Les **références** : quelles bibliothèques tierces sont utilisées ? `Newtonsoft.Json` pour la sérialisation ? `BouncyCastle` pour la cryptographie ? `System.Net.Http` pour des communications réseau ?  
- Les **attributs d'assembly** : version, copyright, configuration de débogage (`DebuggableAttribute` indique si l'assembly a été compilé en Debug ou Release).

### Phase 2 — Recherche de points d'intérêt

Utilisez la recherche (`Ctrl+Shift+F`) pour localiser :

- Les **chaînes littérales** suspectes : messages d'erreur de licence, URLs, chemins de fichiers, clés hardcodées.  
- Les **noms de méthodes** évocateurs : `Validate`, `Decrypt`, `Authenticate`, `CheckExpiry`, `GenerateKey`.  
- Les **appels cryptographiques** : recherchez `AES`, `RSA`, `SHA`, `HMAC`, `Encrypt`, `Decrypt` dans les noms de membres.

### Phase 3 — Analyse descendante

Une fois un point d'intérêt identifié, remontez le graphe d'appels avec **Analyze** (`Ctrl+R`). Partez de la méthode cible et répondez aux questions suivantes : qui appelle cette méthode ? Avec quels arguments ? Le résultat est-il vérifié dans un branchement conditionnel ? C'est exactement la même méthodologie top-down que celle décrite au chapitre 21 (section 21.3) pour le keygenme natif, mais avec des noms explicites au lieu d'adresses hexadécimales.

### Phase 4 — Export et analyse hors ILSpy

Si l'assembly est volumineux ou que vous devez traiter plusieurs composants en parallèle, exportez le projet complet (`Save Code...`) et poursuivez l'analyse dans un IDE. Cette transition est naturelle avec ILSpy — l'outil n'essaie pas de vous enfermer dans son interface.

---

## Ligne de commande : `ilspycmd`

Pour les analyses scriptées ou l'intégration dans un pipeline (cf. chapitre 35, section 35.5), ILSpy fournit un outil en ligne de commande :

```bash
# Décompiler un assembly entier en projet C#
ilspycmd -p -o ./output_project MonApplication.exe

# Décompiler un type spécifique
ilspycmd -t MonApplication.Controllers.MainController MonApplication.exe

# Lister les types présents
ilspycmd -l MonApplication.exe

# Produire la sortie IL au lieu de C#
ilspycmd --il MonApplication.exe
```

`ilspycmd` utilise le même moteur de décompilation que l'interface graphique — la qualité du résultat est identique. C'est l'outil idéal pour automatiser la décompilation d'un lot d'assemblies ou pour intégrer une étape de décompilation dans un script de triage (dans l'esprit du `triage.py` fourni dans `scripts/`).

---

## Forces et limites d'ILSpy

### Forces

- **Open source et gratuit** : aucune restriction de licence, code auditable, extensible par plugins.  
- **Qualité de décompilation** : parmi les meilleures du marché, avec un support actif des dernières versions du langage C# et du runtime .NET.  
- **Multiplateforme** : la version Avalonia fonctionne nativement sous Linux, ce qui est un avantage considérable pour les analystes travaillant dans une VM de RE.  
- **Export de projet** : une fonctionnalité que ni dotPeek ni dnSpy n'offrent avec la même facilité.  
- **Outil CLI** : `ilspycmd` permet l'automatisation et l'intégration dans des pipelines.  
- **Communauté active** : les bugs sont corrigés rapidement, les nouvelles constructions C# sont supportées à chaque version.

### Limites

- **Pas de débogueur intégré** : c'est la différence fondamentale avec dnSpy. ILSpy est un outil d'analyse statique — vous ne pouvez pas poser de breakpoints, inspecter la mémoire ou modifier des valeurs à la volée. Pour l'analyse dynamique, vous devrez compléter ILSpy avec dnSpy (section 31.2) ou avec les techniques du chapitre 32.  
- **Pas d'édition d'IL** : ILSpy est en lecture seule. Vous pouvez voir le code, mais pas le modifier directement dans l'assembly. Pour le patching, dnSpy est l'outil de choix (section 32.4).  
- **Assemblies obfusqués** : face à un obfuscateur agressif (renommage, control flow flattening, string encryption), ILSpy décompile sans erreur mais le résultat est illisible — des noms comme `a.b(c.d())` ne vous avancent pas. Il faut d'abord passer par un déobfuscateur comme de4dot (section 31.5).  
- **Assemblies mixtes (C++/CLI)** : les assemblies contenant du code natif mélangé au CIL (mode mixte) ne sont que partiellement décompilés. La partie native nécessite un outil comme Ghidra (chapitre 8).

---

## Résumé

ILSpy est le point de départ naturel de toute analyse d'assembly .NET. Sa combinaison de qualité de décompilation, de navigation riche, d'export de projet et de support multiplateforme en fait l'outil indispensable du reverse engineer .NET. Sa philosophie « statique et en lecture seule » est à la fois sa force (simplicité, fiabilité) et sa limite (pas de débogage, pas de patching) — des lacunes que dnSpy et les techniques dynamiques du chapitre 32 viennent combler.

---


⏭️ [dnSpy / dnSpyEx — décompilation + débogage intégré (breakpoints sur C# décompilé)](/31-decompilation-dotnet/02-dnspy-dnspyex.md)
