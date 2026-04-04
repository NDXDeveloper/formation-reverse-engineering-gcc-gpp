🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 31.4 — Comparatif ILSpy vs dnSpy vs dotPeek

> 📦 **Chapitre 31 — Décompilation d'assemblies .NET**  
> 


---

## Pourquoi comparer ?

Les sections 31.1 à 31.3 ont présenté chaque outil individuellement, avec son workflow propre. Cette section les met face à face sur des critères concrets afin que vous puissiez choisir le bon outil pour chaque situation — et surtout comprendre comment ils se complètent. La logique est la même que celle du comparatif Ghidra vs IDA vs Radare2 vs Binary Ninja de la section 9.6, transposée à l'écosystème .NET.

Un point essentiel avant de commencer : **ces trois outils ne sont pas en compétition frontale**. Chacun occupe une niche distincte. L'objectif de ce comparatif n'est pas de désigner un « meilleur outil » mais de construire une grille de décision qui vous permette de savoir lequel lancer en fonction de ce que vous faites.

---

## Tableau comparatif synthétique

| Critère | ILSpy | dnSpy (dnSpyEx) | dotPeek |  
|---|---|---|---|  
| **Licence** | MIT (open source) | GPL v3 (open source) | Gratuit (propriétaire) |  
| **Plateformes** | Windows, Linux, macOS | Windows uniquement | Windows uniquement |  
| **Décompilation C#** | Excellente | Très bonne | Très bonne |  
| **Support C# récent** | C# 12+ | C# 11 (rattrapage en cours) | C# 12+ |  
| **Vue IL** | Oui | Oui | Oui (synchronisée C#↔IL) |  
| **Débogueur intégré** | Non | Oui (.NET Fw, .NET 6+, Unity) | Non (Symbol Server → VS) |  
| **Édition C# / IL** | Non (lecture seule) | Oui (C# via Roslyn + IL direct) | Non (lecture seule) |  
| **Breakpoints sur décompilé** | Non | Oui | Indirect (via Symbol Server) |  
| **Export projet .csproj** | Oui (complet) | Non | Oui |  
| **Outil CLI** | `ilspycmd` | Non | Non |  
| **Navigation / recherche** | Bonne | Bonne | Excellente (héritage ReSharper) |  
| **Type Hierarchy** | Basique | Basique | Avancée (arbre complet) |  
| **Find Usages structuré** | Analyze (catégories) | Analyze (catégories) | Find Usages (catégories + contexte) |  
| **Coloration sémantique** | Syntaxique | Syntaxique | Sémantique (champs, params, locaux…) |  
| **Hex editor intégré** | Non | Oui | Non |  
| **Plugins / extensibilité** | Oui (architecture de plugins) | Limitée | Non |  
| **Développement actif** | Très actif (ICSharpCode) | Communautaire (irrégulier) | Actif (JetBrains) |  
| **Poids / portabilité** | Léger, portable (ZIP) | Léger, portable (ZIP) | Installeur, non portable |

---

## Analyse par critère

### Qualité de décompilation

Les trois outils produisent du C# de haute qualité sur des assemblies non obfusqués compilés en Release. Les différences se manifestent sur les cas limites :

**Constructions C# modernes.** ILSpy mène la course grâce à son développement très actif. Les patterns introduits par les dernières versions du langage — primary constructors (C# 12), collection expressions, `required` members — sont reconnus et restitués fidèlement. dotPeek suit de près, porté par les ressources de JetBrains. dnSpyEx accuse un léger retard sur les constructions les plus récentes, le fork communautaire ayant moins de bande passante pour suivre chaque évolution de Roslyn.

**`async/await`.** Les trois outils reconstruisent correctement les méthodes asynchrones à partir des machines à états générées par le compilateur. ILSpy et dotPeek tendent à produire un code légèrement plus propre (moins de variables temporaires résiduelles) sur les cas complexes impliquant plusieurs `await` imbriqués dans des boucles et des blocs `try/catch`.

**Code optimisé.** Quand l'assembly a été compilé en mode Release avec optimisations activées, le compilateur Roslyn effectue des transformations (inlining de petites méthodes, élimination de code mort, simplification de branchements) qui compliquent la décompilation. Les résultats divergent marginalement entre les outils : une variable peut être nommée différemment, un `if/else` peut être restructuré en opérateur ternaire ou inversement. Ces différences sont cosmétiques — la sémantique est correcte dans les trois cas.

**Assemblies obfusqués.** Face à un assembly protégé par un obfuscateur (ConfuserEx, Dotfuscator, SmartAssembly…), les trois outils se comportent de manière similaire : ils décompilent sans erreur, mais le résultat est illisible car les noms de symboles ont été remplacés par des identifiants aléatoires ou des caractères non imprimables. Aucun des trois n'intègre de déobfuscateur — c'est le rôle de de4dot, traité en section 31.5. La seule différence notable est que le débogueur de dnSpy reste fonctionnel sur du code obfusqué, ce qui permet de tracer l'exécution même quand les noms sont incompréhensibles.

### Débogage

C'est le critère qui sépare le plus nettement les trois outils.

**dnSpy** est le seul à proposer un débogueur intégré complet : breakpoints (simples et conditionnels), pas à pas, inspection de variables, évaluation d'expressions arbitraires, modification de valeurs en live, Set Next Statement. Tout cela directement sur le code C# décompilé, sans PDB, sans code source. Pour l'analyse dynamique d'assemblies .NET, c'est l'outil de référence.

**dotPeek** offre une alternative indirecte via son Symbol Server. En générant des PDB synthétiques consommés par Visual Studio, il permet de poser des breakpoints dans Visual Studio sur du code décompilé. Cette approche fonctionne mais ajoute de la friction : il faut configurer Visual Studio pour utiliser le serveur de symboles de dotPeek, et les deux outils doivent tourner simultanément. L'avantage est l'accès à l'écosystème complet de Visual Studio (diagnostics, profiling, IntelliTrace) ; l'inconvénient est la lourdeur de mise en place.

**ILSpy** ne propose aucune fonctionnalité de débogage. C'est un outil d'analyse purement statique. Pour le débogage, vous devez basculer vers dnSpy ou vers la combinaison dotPeek + Visual Studio.

### Édition et patching

**dnSpy** est le seul des trois à permettre la modification d'un assembly. L'édition en C# (recompilation via Roslyn) et l'édition IL directe (modification opcode par opcode) couvrent l'ensemble des scénarios de patching, du plus simple (faire retourner `true` à une méthode de validation) au plus chirurgical (inverser un branchement conditionnel dans l'IL). Le module sauvegardé est un assembly .NET valide, immédiatement exécutable.

**ILSpy** et **dotPeek** sont strictement en lecture seule. Pour patcher un assembly analysé dans l'un de ces outils, vous devez soit basculer vers dnSpy, soit utiliser un outil externe comme `Mono.Cecil` (bibliothèque programmatique de manipulation d'assemblies .NET) ou `ildasm`/`ilasm` (désassemblage en texte IL, modification, réassemblage).

### Navigation et recherche

**dotPeek** domine ce critère. Son système de navigation hérité de ReSharper offre une fluidité et une profondeur que les deux autres n'atteignent pas. Le fuzzy matching CamelCase dans `Go to Everything`, le `Find Usages` structuré avec extraits de code contextuels, la `Type Hierarchy` complète avec classes dérivées et implémentations d'interfaces — ces fonctionnalités font de dotPeek l'outil le plus productif pour explorer une base de code .NET volumineuse.

**ILSpy** et **dnSpy** offrent une navigation fonctionnellement suffisante — recherche textuelle, Analyze/XREF, Go to Definition — mais sans la finesse de dotPeek. La différence est peu perceptible sur un assembly avec une dizaine de types, mais elle devient significative sur une application avec des centaines de classes réparties dans des dizaines de namespaces.

### Multiplateforme et intégration

**ILSpy** est le seul outil utilisable nativement sous **Linux et macOS** grâce à sa version Avalonia. C'est un avantage décisif pour les analystes travaillant dans une VM Linux (la configuration recommandée au chapitre 4). De plus, `ilspycmd` permet l'intégration dans des scripts et des pipelines d'automatisation (chapitre 35).

**dnSpy** et **dotPeek** sont verrouillés sur Windows. Si votre environnement de RE est une VM Linux, ces outils nécessitent une VM Windows supplémentaire ou un dual-boot.

### Pérennité et maintenance

La question de la pérennité est pertinente pour le choix d'un outil que vous allez intégrer à votre workflow quotidien.

**ILSpy** bénéficie du modèle open source le plus sain des trois. Le projet est maintenu par ICSharpCode depuis plus de treize ans, avec des releases régulières et une communauté de contributeurs active. La licence MIT garantit que le code restera disponible même si l'équipe principale cessait de le maintenir.

**dotPeek** est soutenu par JetBrains, une entreprise établie et profitable. Le risque d'abandon brutal est faible, mais le modèle propriétaire signifie que vous dépendez entièrement des décisions de JetBrains. L'outil pourrait devenir payant, être intégré exclusivement dans Rider, ou être discontinué au profit d'un autre produit — vous n'auriez pas de recours.

**dnSpyEx** est dans la position la plus fragile. Le projet original a été abandonné par son auteur, et le fork communautaire dépend du bénévolat de quelques contributeurs. Le rythme des releases est irrégulier. Le code est sous GPL v3, ce qui garantit la disponibilité du source, mais sans mainteneur actif l'outil risque de décrocher progressivement du runtime .NET. C'est un argument supplémentaire pour ne pas dépendre d'un seul outil.

---

## Matrice de décision par scénario

Plutôt qu'un classement abstrait, voici quel outil choisir en fonction de ce que vous faites concrètement.

### « Je veux comprendre rapidement ce que fait un assembly inconnu »

**Premier choix : ILSpy.** Ouverture instantanée, navigation claire, recherche de chaînes. Le workflow de triage est le plus fluide. Si vous travaillez sous Linux, c'est votre seule option GUI native. Sous Windows, les trois outils conviennent pour cette tâche, mais ILSpy reste le plus léger et le plus rapide à lancer.

### « Je veux tracer l'exécution d'une méthode et observer les valeurs à l'exécution »

**Premier choix : dnSpy.** Le débogueur intégré est sans équivalent. Breakpoints sur code décompilé, inspection de variables, évaluation d'expressions — aucun autre outil ne propose cette combinaison de manière aussi directe.

**Alternative : dotPeek + Visual Studio.** Plus lourd à configurer mais fonctionnel, avec l'avantage d'accéder aux outils de diagnostic de Visual Studio.

### « Je veux patcher un assembly (modifier une vérification, contourner une protection) »

**Premier choix : dnSpy.** L'édition C# et IL intégrée permet de modifier, recompiler et sauvegarder sans quitter l'outil. C'est le workflow de patching le plus direct. Cette capacité est approfondie au chapitre 32, section 32.4.

**Alternative : Modification programmatique** avec `Mono.Cecil` si vous devez automatiser le patching ou l'appliquer à plusieurs assemblies.

### « Je veux cartographier l'architecture d'une application .NET complexe »

**Premier choix : dotPeek.** La navigation ReSharper, la Type Hierarchy et le Find Usages structuré sont taillés pour ce scénario. L'écart avec ILSpy et dnSpy est particulièrement marqué sur les grandes applications.

**Second choix : ILSpy** avec export de projet, ouvert ensuite dans Rider ou Visual Studio pour bénéficier de la navigation IDE.

### « Je veux exporter le code décompilé pour le lire dans un IDE »

**Premier choix : ILSpy.** L'export en projet `.csproj` est la fonctionnalité la plus mature et la mieux intégrée. `ilspycmd -p` permet d'automatiser l'opération en ligne de commande.

**Second choix : dotPeek** via `Export to Project`, avec une structuration de dossiers parfois plus fidèle à l'architecture originale.

### « Je travaille sous Linux et je n'ai pas de VM Windows »

**Seul choix : ILSpy** (version Avalonia ou `ilspycmd`). Pour l'analyse dynamique, vous devrez utiliser les techniques Frida-CLR du chapitre 32, section 32.2, ou monter une VM Windows pour les sessions dnSpy.

### « J'analyse un assembly obfusqué »

**Phase 1 : de4dot** (section 31.5) pour déobfusquer l'assembly.

**Phase 2 : ILSpy ou dotPeek** pour l'analyse statique de l'assembly nettoyé.

**Phase 3 : dnSpy** si un débogage dynamique est nécessaire pour comprendre les parties que de4dot n'a pas réussi à clarifier. Le débogueur de dnSpy fonctionne même sur du code obfusqué — les noms sont illisibles mais les valeurs à l'exécution restent observables.

### « Je veux intégrer la décompilation dans un script ou un pipeline CI »

**Seul choix : ILSpy** via `ilspycmd`. Ni dnSpy ni dotPeek ne proposent d'interface en ligne de commande. Pour la manipulation programmatique d'assemblies (parsing, modification, analyse automatisée), les bibliothèques `Mono.Cecil` et `System.Reflection.Metadata` sont les compléments naturels.

---

## Stratégie recommandée : les trois ensemble

En pratique, un reverse engineer .NET efficace ne choisit pas un seul outil — il les utilise tous les trois en fonction du contexte, exactement comme un analyste de binaires natifs alterne entre Ghidra (analyse statique approfondie), GDB (débogage dynamique) et les outils CLI (triage rapide).

La combinaison recommandée pour ce tutoriel est la suivante :

**ILSpy** est votre outil principal et permanent. C'est celui que vous lancez en premier sur tout assembly inconnu. Il couvre le triage, la décompilation statique, l'export et l'automatisation. Il fonctionne sur toutes les plateformes. Il est open source et pérenne.

**dnSpy** est votre outil d'analyse dynamique. Vous le lancez quand ILSpy a identifié un point d'intérêt que vous devez observer à l'exécution — une routine de validation, un flux de déchiffrement, un échange réseau. Vous y revenez aussi pour le patching rapide d'assemblies.

**dotPeek** est votre outil d'exploration architecturale. Vous le lancez quand l'assembly est volumineux et que la navigation d'ILSpy ou dnSpy ne suffit plus — quand vous avez besoin de cartographier des hiérarchies d'héritage complexes, de tracer des chaînes d'injection de dépendances, ou simplement de lire du code décompilé avec le confort d'un IDE JetBrains.

Cette répartition n'est pas rigide. Sur un petit assembly, ILSpy seul peut suffire. Sur un CTF où l'objectif est de contourner une vérification le plus vite possible, dnSpy seul fait le travail. L'important est de connaître les forces de chaque outil pour ne pas perdre de temps à utiliser le mauvais dans la mauvaise situation.

---

## Parallèle avec l'outillage natif

Pour les lecteurs venant des Parties II–IV, voici un tableau de correspondance qui aide à transposer les réflexes acquis sur les binaires natifs.

| Besoin | Outillage natif (ELF/x86-64) | Outillage .NET |  
|---|---|---|  
| Décompilation statique | Ghidra, RetDec | ILSpy, dotPeek |  
| Débogage sur code décompilé | GDB + GEF/pwndbg (asm) | dnSpy (C# décompilé) |  
| Édition/patching binaire | ImHex, `objcopy`, hex editor | dnSpy (édition C#/IL) |  
| Triage rapide CLI | `file`, `strings`, `readelf` | `ilspycmd`, `file`, `strings` |  
| Navigation dans le code | Ghidra CodeBrowser | dotPeek, ILSpy |  
| Références croisées | Ghidra XREF | Analyze (ILSpy/dnSpy), Find Usages (dotPeek) |  
| Export de pseudo-code | Ghidra Export C | ILSpy `Save Code`, dotPeek `Export to Project` |  
| Scripting / automatisation | Ghidra headless, r2pipe | `ilspycmd`, `Mono.Cecil` |

La différence fondamentale reste la qualité du résultat : là où le décompilateur natif produit un pseudo-code approximatif que vous passez des heures à annoter, le décompilateur .NET produit du C# quasiment identique au source. C'est cette asymétrie qui rend le RE .NET à la fois plus accessible et — quand l'obfuscation entre en jeu — dépendant d'une étape préalable de déobfuscation. C'est l'objet de la section suivante.

---


⏭️ [Décompiler malgré l'obfuscation : de4dot et techniques de contournement](/31-decompilation-dotnet/05-de4dot-contournement.md)
