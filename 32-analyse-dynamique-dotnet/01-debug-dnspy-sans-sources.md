🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 32.1 — Déboguer un assembly avec dnSpy sans les sources

> 📁 **Fichiers utilisés** : `binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64/LicenseChecker.dll`  
> 🔧 **Outils** : dnSpy (dnSpyEx), dotnet runtime  
> 📖 **Prérequis** : [Chapitre 31 — Décompilation d'assemblies .NET](/31-decompilation-dotnet/README.md)

---

## Le principe : le débogueur qui n'a besoin de rien

Dans le monde natif, déboguer un binaire ELF strippé avec GDB est un exercice austère. On travaille sur des adresses brutes, des registres, des dumps mémoire hexadécimaux. On pose des breakpoints sur des offsets qu'on a péniblement identifiés dans Ghidra. Chaque variable locale est un mystère qu'on reconstitue en inspectant la pile ou les registres un par un.

Le monde .NET renverse cette situation. Grâce aux métadonnées embarquées dans chaque assembly — tables de types, signatures de méthodes, tokens de champs, informations de debug optionnelles — un outil comme dnSpy peut simultanément décompiler le bytecode CIL en C# lisible **et** attacher un débogueur au processus en cours d'exécution. Le résultat est une expérience comparable à celle d'un développeur qui débogue son propre projet dans Visual Studio, sauf qu'ici on ne possède aucun fichier source.

Concrètement, cela signifie qu'on peut poser un breakpoint en cliquant sur une ligne de code C# décompilé, exécuter le programme pas à pas, inspecter les variables locales par leur nom reconstruit, consulter la pile d'appels avec les signatures de méthodes complètes, et même évaluer des expressions C# à la volée dans la fenêtre Immediate. C'est un niveau de confort que le reverse engineer natif ne peut qu'envier.

## dnSpy vs dnSpyEx : quel outil utiliser

Le projet dnSpy original, créé par 0xd4d, n'est plus maintenu depuis 2020. Son dépôt GitHub est archivé. Le fork communautaire **dnSpyEx** reprend le développement et apporte le support des versions récentes de .NET (6, 7, 8), des corrections de bugs, et une compatibilité avec les runtimes actuels. C'est la version à utiliser.

dnSpyEx est disponible pour Windows et, via Wine ou une compilation Mono, peut fonctionner partiellement sous Linux. Cependant, le débogueur intégré repose sur les API de debug Windows (ICorDebug) et ne fonctionne pleinement que sur Windows. Pour nos exercices, deux approches sont possibles : utiliser une VM Windows pour le débogage avec dnSpy, ou utiliser un débogueur .NET alternatif sous Linux (comme `dotnet-dump` ou le débogueur intégré de JetBrains Rider) et réserver dnSpy pour l'analyse statique. Dans ce qui suit, on suppose un environnement Windows avec dnSpyEx, qui est le cas d'usage canonique.

> 💡 **Rappel** : dans tout ce chapitre, « dnSpy » désigne le fork dnSpyEx sauf mention contraire.

## Ouvrir un assembly dans dnSpy

Au lancement, dnSpy présente une interface qui ressemble à un IDE classique. La partie gauche contient l'**Assembly Explorer** — un arbre qui liste les assemblies chargés et leur contenu. La partie centrale est l'éditeur de code décompilé. En bas, on trouve les fenêtres de débogage : Locals, Watch, Call Stack, Breakpoints, Output, Immediate.

Pour charger notre cible, on utilise **File → Open** (ou on glisse le fichier dans la fenêtre) et on sélectionne `LicenseChecker.dll`. dnSpy le décompile instantanément. Dans l'Assembly Explorer, on déplie l'arbre :

```
LicenseChecker (1.0.0.0)
  └── LicenseChecker (namespace)
        ├── Program
        ├── LicenseValidator
        ├── ValidationResult
        └── NativeBridge
```

En cliquant sur une classe, le panneau central affiche le code C# décompilé. Le résultat est immédiatement lisible : noms de classes, noms de méthodes, signatures complètes, corps des méthodes avec la logique de contrôle reconstituée. Si l'assembly n'est pas obfusqué (ce qui est le cas de notre `LicenseChecker` compilé sans protection), le code décompilé est quasi identique au code source original.

## Comprendre ce que dnSpy décompile réellement

Il est important de comprendre ce qui se passe sous le capot. L'assembly `LicenseChecker.dll` contient du bytecode CIL, pas du code source C#. dnSpy effectue une décompilation en temps réel : il lit les instructions IL, reconstruit le flux de contrôle (boucles, conditions, blocs try/catch), infère les types des variables locales à partir des métadonnées et des signatures de la pile IL, puis produit du C# syntaxiquement valide.

Ce processus n'est pas parfait. Voici les écarts typiques que l'on peut observer entre le code décompilé et le code source original :

**Les noms de variables locales.** Si l'assembly a été compilé en mode Release sans fichier PDB (Program Database), les noms des variables locales sont perdus. dnSpy les remplace par des noms générés : `text`, `num`, `flag`, `array`, etc. En mode Debug ou avec un PDB présent, les noms originaux sont conservés.

**Les structures de contrôle.** Le compilateur C# transforme certaines constructions en patterns IL qui ne se retranscrivent pas toujours dans la forme syntaxique d'origine. Un `switch` sur des chaînes de caractères peut devenir une cascade de `if/else` avec des appels à `string.op_Equality`. Un `foreach` peut apparaître comme un `while` avec un enumerator explicite. Les expressions `switch` de C# 8+ (comme notre `DeriveLicenseLevel`) peuvent être reconstruites différemment selon la version du décompileur.

**Les propriétés auto-implémentées.** Le compilateur génère un champ backing caché (nommé `<PropertyName>k__BackingField`). dnSpy les reconnaît généralement et les re-synthétise en propriétés auto-implémentées, mais pas toujours.

**Les expressions LINQ et les lambdas.** Le compilateur génère des classes internes cachées (nommées `<>c__DisplayClass...`) pour capturer les variables. dnSpy tente de les replier en expressions lambda, avec un succès variable.

Pour notre `LicenseChecker`, compilé en Release sans obfuscation, le résultat sera très propre. Les noms de méthodes publiques et privées, les noms de champs, les constantes — tout est présent dans les métadonnées. Seuls les noms de variables locales seront potentiellement remplacés.

## Configurer le débogage

Avant de lancer le débogage, il faut indiquer à dnSpy comment exécuter l'application. On utilise **Debug → Start Debugging** (F5) ou le menu **Debug → Attach to Process** selon qu'on veut lancer le programme ou s'attacher à un processus déjà en cours.

Pour un lancement direct, dnSpy demande de configurer l'exécution dans **Debug → Start Debugging → Debug an Executable**. Pour une application .NET moderne (comme notre cible .NET 8), on configure :

- **Executable** : le chemin vers le runtime `dotnet.exe` (sous Windows) ou directement vers `LicenseChecker.exe` si l'application a été publiée en mode self-contained.  
- **Arguments** : si on utilise `dotnet.exe`, on passe le chemin vers `LicenseChecker.dll` comme argument. On peut aussi ajouter les arguments de l'application elle-même (username et clé).  
- **Working Directory** : le répertoire contenant `LicenseChecker.dll` et `libnative_check.so` (ou sa version Windows `.dll`).

Pour un assembly .NET Framework classique (ce qui n'est pas notre cas, mais reste courant en RE), la configuration est plus simple : on pointe directement vers l'exécutable `.exe`.

> ⚠️ **Point d'attention** : pour que les appels P/Invoke vers `libnative_check` fonctionnent sous Windows, il faut avoir compilé une version Windows de la bibliothèque native (`.dll` au lieu de `.so`), ou travailler en mode debug partiel en acceptant que les étapes impliquant P/Invoke échouent. Pour le chapitre, on peut commencer par déboguer les parties purement managées (segments A et C) et traiter le P/Invoke séparément à la section 32.3.

## Poser des breakpoints sur le code décompilé

C'est ici que la magie opère. Dans le panneau central, on navigue jusqu'à la méthode `LicenseValidator.Validate()`. On voit le code décompilé qui correspond à notre flux de validation en cinq étapes. Pour poser un breakpoint, on clique dans la marge gauche en face de la ligne souhaitée — exactement comme dans Visual Studio ou tout autre IDE.

Commençons par placer des breakpoints stratégiques :

**Sur l'appel à `ValidateStructure`.** C'est le premier test. En s'arrêtant ici, on pourra observer la valeur de `licenseKey` telle qu'elle arrive dans le validateur, et vérifier le résultat du parsing.

**Sur la comparaison `actualA != expectedA`.** C'est le moment clé pour le segment A. En inspectant `expectedA` avant que la comparaison ne s'exécute, on obtient directement la valeur attendue pour le premier segment de la clé — sans avoir besoin de comprendre l'algorithme FNV-1a. C'est la puissance du débogage dynamique : on laisse le programme calculer pour nous.

**Sur la comparaison `actualC != expectedC`.** Même logique pour le segment C. La valeur de `expectedC` nous donne le troisième segment correct.

**Sur le retour final `result.IsValid = true`.** Si on atteint ce point, la licence est validée. En modifiant le flux d'exécution (voir plus loin), on pourrait aussi forcer l'exécution à atteindre ce point.

Les breakpoints posés apparaissent dans la fenêtre **Breakpoints** (menu **Debug → Windows → Breakpoints**), avec leur localisation dans le code décompilé. On peut les activer, désactiver, rendre conditionnels, ou leur ajouter des actions (comme logger une valeur sans arrêter l'exécution).

## Exécution pas à pas et inspection

Une fois les breakpoints posés, on lance le débogage avec F5. L'application démarre, affiche sa bannière, et attend un nom d'utilisateur et une clé. On entre par exemple `alice` comme username et `1111-2222-3333-4444` comme clé (volontairement incorrecte — on veut observer le processus de validation, pas réussir du premier coup).

L'exécution s'arrête sur notre premier breakpoint. À ce stade, plusieurs fenêtres deviennent exploitables.

**La fenêtre Locals.** Elle affiche les variables locales de la méthode en cours, avec leurs valeurs actuelles. On y voit `username = "alice"`, `licenseKey = "1111-2222-3333-4444"`, et l'objet `result` de type `ValidationResult` en cours de construction. Pour les types complexes (objets, tableaux), on peut déplier l'arbre pour inspecter chaque champ.

**La fenêtre Watch.** On peut y ajouter des expressions arbitraires à surveiller. Par exemple, si on ajoute `Convert.ToUInt32("1111", 16)`, dnSpy évalue l'expression et affiche `4369`. C'est utile pour vérifier des conversions sans quitter le débogueur.

**La fenêtre Call Stack.** Elle montre la chaîne d'appels qui a mené au point d'arrêt actuel. Dans notre cas, on verra quelque chose comme `Program.Main → LicenseValidator.Validate`. En double-cliquant sur un frame de la pile, on peut remonter dans le contexte de l'appelant et inspecter ses variables.

**La fenêtre Immediate.** C'est un REPL C# intégré. On peut y taper des expressions qui s'évaluent dans le contexte du point d'arrêt actuel. Par exemple, taper `this.ComputeUserHash("alice")` exécutera la méthode et affichera le résultat. C'est extrêmement puissant pour le RE : on peut appeler n'importe quelle méthode de l'objet courant avec les arguments de notre choix.

On avance ensuite pas à pas avec **F10** (Step Over — exécute la ligne courante sans entrer dans les méthodes appelées) ou **F11** (Step Into — entre dans la méthode appelée). La navigation est identique à celle d'un IDE de développement. En passant au-dessus de `ComputeUserHash(username)`, on voit la variable `expectedA` prendre sa valeur. On note cette valeur — c'est le segment A correct pour le username `alice`.

## Extraire les valeurs attendues par observation

L'approche la plus directe pour « craquer » notre `LicenseChecker` par débogage est de laisser le programme calculer lui-même les bonnes valeurs. Voici la stratégie, étape par étape.

**Récupérer le segment A.** On pose un breakpoint après l'appel `ComputeUserHash(username)`. On lit la valeur de `expectedA` dans la fenêtre Locals. C'est la valeur hexadécimale (sur 4 caractères) que doit contenir le premier segment de la clé.

**Récupérer le segment B.** C'est plus délicat car il dépend de l'appel P/Invoke. Si la bibliothèque native est disponible, on peut poser un breakpoint dans `CheckSegmentB` et observer la valeur de `expected` après l'appel à `NativeBridge.ComputeNativeHash`. Si la bibliothèque n'est pas disponible, l'exception `DllNotFoundException` sera attrapée et on devra aborder le problème autrement (section 32.3).

**Récupérer le segment C.** Même technique : breakpoint après `ComputeCrossXor(actualA, actualB)`, lecture de `expectedC`. Mais attention — pour que ce calcul soit correct, il faut que `actualA` et `actualB` soient eux-mêmes corrects. On doit donc entrer une clé dont les deux premiers segments sont déjà bons. C'est pour cela qu'on procède itérativement : on obtient A, puis B, on relance avec A-B corrects, puis on obtient C, etc.

**Récupérer le segment D.** Breakpoint après `ComputeFinalChecksum(...)`. Même contrainte : les trois premiers segments doivent être corrects pour que le calcul du quatrième soit valide.

Cette approche itérative — lancer, observer, corriger, relancer — est typique du débogage RE. Elle fonctionne sans comprendre les algorithmes internes : on traite le programme comme une boîte noire qu'on interroge pas à pas.

## Modifier le flux d'exécution

dnSpy permet de modifier les valeurs des variables et de déplacer le pointeur d'exécution pendant le débogage. Ces deux capacités ouvrent des possibilités supplémentaires.

**Modifier une variable.** Dans la fenêtre Locals ou Watch, on peut double-cliquer sur la valeur d'une variable et la changer. Par exemple, si `expectedA` vaut `0x7B3F` et `actualA` vaut `0x1111`, on peut modifier `actualA` pour lui donner la valeur `0x7B3F` — ou modifier `expectedA` pour qu'il corresponde à `actualA`. La comparaison qui suit retournera `true`, et l'exécution continuera vers l'étape suivante.

**Déplacer le pointeur d'instruction.** En faisant un clic droit sur une ligne de code et en choisissant **Set Next Statement**, on force l'exécution à reprendre à cette ligne, en sautant tout le code intermédiaire. On peut ainsi sauter par-dessus un bloc `if (!segBValid) { return result; }` pour atteindre l'étape suivante même si la vérification a échoué.

Ces manipulations sont des techniques de bypass temporaires — elles n'affectent que l'exécution en cours et ne modifient pas le binaire sur disque. Pour un patch permanent, il faudra éditer l'IL (section 32.4). Mais elles sont précieuses pendant la phase d'exploration : elles permettent de « débloquer » le flux pour atteindre des portions de code qu'on ne pourrait pas observer autrement.

## La fenêtre Modules et le chargement d'assemblies

Pendant le débogage, la fenêtre **Modules** (Debug → Windows → Modules) liste tous les assemblies chargés dans le processus. Pour notre `LicenseChecker`, on verra au minimum le runtime .NET (`System.Private.CoreLib.dll`, `System.Runtime.dll`...), notre assembly principal (`LicenseChecker.dll`), et potentiellement les assemblies chargées dynamiquement.

Cette fenêtre est particulièrement utile dans deux situations.

Quand l'application charge des assemblies à l'exécution — via `Assembly.Load()` ou `Assembly.LoadFrom()` — ils apparaissent dans la liste au moment de leur chargement. Certains obfuscateurs déchiffrent un assembly en mémoire puis le chargent dynamiquement. En surveillant la fenêtre Modules, on peut détecter ce chargement et immédiatement décompiler le nouvel assembly dans dnSpy.

Quand on cherche à comprendre la résolution des dépendances. Si un appel P/Invoke échoue ou si un type n'est pas trouvé, la fenêtre Modules indique quels assemblies sont effectivement chargés et lesquels manquent.

## Débogage d'un assembly obfusqué : les limites

Notre `LicenseChecker` est compilé sans obfuscation, ce qui rend le débogage confortable. En situation réelle, on rencontrera des assemblies protégés. Voici les obstacles courants et leur impact sur le débogage.

**Le renommage de symboles.** Les classes deviennent `\u0001`, `\u0002`, etc. Les méthodes deviennent des séquences Unicode illisibles. Le code décompilé reste syntaxiquement correct, mais la lecture est pénible. Le débogage fonctionne normalement — on peut toujours poser des breakpoints et inspecter les variables, mais il faut d'abord identifier les méthodes intéressantes par leur comportement plutôt que par leur nom.

**Le chiffrement de chaînes.** Les chaînes littérales sont remplacées par des appels à une fonction de déchiffrement : au lieu de `"REV3RSE!"`, on voit `DecryptString(12345)`. En débogage dynamique, c'est en fait un avantage : on laisse la fonction de déchiffrement s'exécuter et on inspecte la valeur résultante. Le débogueur montre la chaîne déchiffrée là où l'analyse statique ne voit qu'un appel opaque.

**Le contrôle de flux aplati.** Les méthodes sont transformées en une boucle `while(true)` avec un `switch` sur une variable d'état. Le code décompilé devient un enchevêtrement de cas numérotés. Le débogage pas à pas reste possible, mais le suivi logique est plus difficile — on navigue dans un automate plutôt que dans un flux structuré.

**La détection de débogueur.** Certains obfuscateurs insèrent des appels à `System.Diagnostics.Debugger.IsAttached` ou vérifient la présence de dnSpy par le nom du processus. Ces vérifications sont facilement contournables en modifiant la valeur de retour dans le débogueur (ou en les patchant, comme on le verra en section 32.4).

## Comparaison avec le débogage natif (GDB)

Pour ancrer ces concepts par rapport à ce que vous connaissez déjà, voici une mise en perspective des deux expériences de débogage.

| Aspect | GDB sur ELF natif | dnSpy sur assembly .NET |  
|---|---|---|  
| Informations disponibles sans symboles | Adresses brutes, registres, opcodes | Noms de types, méthodes, signatures, IL |  
| Pose de breakpoints | Sur adresse ou nom de fonction (si symboles) | Clic sur une ligne C# décompilée |  
| Inspection de variables | `x/` + adresse, `info locals` (si DWARF) | Fenêtre Locals avec noms et types |  
| Évaluation d'expressions | `print expr` (limité au C) | Fenêtre Immediate (C# complet) |  
| Step into une méthode | `step` (si symboles, sinon assembleur) | F11 → entre dans le C# décompilé |  
| Modification de valeurs | `set $rax = 42` ou `set {int}0x... = 42` | Double-clic sur la valeur dans Locals |  
| Contournement d'un check | Patch de l'opcode `jz` → `jnz` ou modification de flags | Modification de variable ou Set Next Statement |  
| Confort général | Spartiate | Comparable à un IDE de développement |

La différence fondamentale tient à la quantité d'informations préservées dans le binaire. Un ELF strippé ne contient pratiquement plus rien au-delà du code machine ; un assembly .NET, même sans PDB, conserve l'intégralité de sa structure de types. Cette richesse fait du débogage .NET une activité qualitativement différente du débogage natif.

## Méthodologie recommandée

Pour résumer, voici la démarche générale quand on aborde un assembly .NET inconnu avec dnSpy en mode débogage :

On commence par une **reconnaissance statique rapide**. On ouvre l'assembly dans dnSpy, on parcourt l'Assembly Explorer pour identifier les namespaces, les classes et les méthodes. On cherche les points d'entrée (`Main`, les constructeurs, les gestionnaires d'événements). On repère les chaînes de caractères intéressantes (messages d'erreur, URLs, noms de fichiers) en utilisant la recherche intégrée (Ctrl+Shift+K). Cette phase correspond au « triage rapide » du chapitre 5, transposé au monde .NET.

On identifie ensuite les **méthodes cibles**. Dans notre cas, c'est `Validate()` et les méthodes qu'elle appelle. En situation réelle, on cherche les méthodes liées à la fonctionnalité qu'on étudie : vérification de licence, parsing de protocole, déchiffrement de données.

On pose des **breakpoints stratégiques** aux points de décision — les `if` qui séparent le chemin « succès » du chemin « échec ». On lance le débogage et on observe les valeurs concrètes.

On **itère** : on ajuste les entrées en fonction de ce qu'on observe, on déplace les breakpoints pour explorer des branches différentes, on utilise la fenêtre Immediate pour tester des hypothèses.

Et on **documente** au fur et à mesure. dnSpy permet d'ajouter des commentaires dans le code décompilé (clic droit → Add Comment) et de renommer des symboles (clic droit → Edit Method/Field). Ces annotations survivent à la session et facilitent la reprise ultérieure.

---


⏭️ [Hooking de méthodes .NET avec Frida (`frida-clr`)](/32-analyse-dynamique-dotnet/02-hooking-frida-clr.md)
