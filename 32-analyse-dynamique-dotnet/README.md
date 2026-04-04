🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 32 — Analyse dynamique et hooking .NET

> 🔗 *Ce chapitre fait partie de la [Partie VII — Bonus : RE sur Binaires .NET / C#](/partie-7-dotnet.md).*  
> 📦 Prérequis : avoir lu le Chapitre 30 — Introduction au RE .NET  et le Chapitre 31 — Décompilation d'assemblies .NET .

---

## Pourquoi l'analyse dynamique sur .NET ?

Les chapitres 30 et 31 nous ont montré que le monde .NET offre un avantage considérable au reverse engineer : grâce aux métadonnées riches embarquées dans les assemblies, la décompilation produit un code C# souvent très proche de l'original. On pourrait alors se demander pourquoi s'embêter avec l'analyse dynamique.

La réponse tient en plusieurs constats. D'abord, l'obfuscation. Des outils comme ConfuserEx, Dotfuscator ou SmartAssembly ne se contentent pas de renommer les symboles : ils chiffrent les chaînes de caractères à l'exécution, aplatissent le flux de contrôle, injectent du code anti-débogage et rendent le pseudo-code décompilé partiellement ou totalement illisible. Face à un assembly lourdement obfusqué, la lecture statique seule atteint rapidement ses limites.

Ensuite, le comportement réel d'un programme ne se lit pas toujours dans son code. Les valeurs concrètes des variables à un instant donné, l'ordre exact des appels de méthodes, les données reçues depuis le réseau ou lues depuis un fichier de configuration — tout cela ne se révèle qu'à l'exécution. Un décompileur vous montre ce que le programme *pourrait* faire ; le débogueur et l'instrumentation vous montrent ce qu'il *fait*.

Enfin, le patching. Le modèle d'exécution .NET repose sur le bytecode CIL (Common Intermediate Language), qui est interprété puis compilé à la volée par le JIT. Cette architecture offre une surface d'intervention que le code natif x86-64 n'a pas : on peut modifier des instructions IL directement dans l'assembly, hooker des méthodes au niveau du runtime CLR, ou encore injecter du code C# dans un processus en cours d'exécution. Ces techniques sont à la fois plus accessibles et plus puissantes que leurs équivalents natifs.

## Ce que vous allez apprendre

Ce chapitre couvre cinq axes complémentaires qui forment un workflow complet d'analyse dynamique sur .NET :

**Le débogage sans sources avec dnSpy.** dnSpy (et son fork maintenu dnSpyEx) est bien plus qu'un décompileur : c'est un environnement de débogage complet. Vous apprendrez à poser des breakpoints directement sur le code C# décompilé, à inspecter les variables locales, la pile d'appels et les objets en mémoire — le tout sans disposer d'aucun fichier source. C'est l'équivalent .NET de GDB sur un binaire natif, mais avec un confort incomparablement supérieur.

**Le hooking de méthodes .NET avec Frida.** Frida, que vous connaissez déjà depuis le [Chapitre 13](/13-frida/README.md) pour l'instrumentation de binaires natifs, dispose d'un module `frida-clr` capable d'interagir avec le runtime .NET. Vous verrez comment intercepter des appels de méthodes C#, lire et modifier leurs arguments à la volée, et remplacer des valeurs de retour — sans toucher au binaire sur disque.

**L'interception des appels P/Invoke.** Les applications .NET ne vivent pas dans un monde isolé. Le mécanisme P/Invoke (Platform Invocation Services) permet à du code C# d'appeler des fonctions dans des bibliothèques natives — typiquement des DLL Windows ou des `.so` Linux compilés avec GCC. Ces appels constituent un pont entre le monde managé et le monde natif, et représentent souvent des points d'intérêt critiques en RE : vérifications de licence déléguées à une bibliothèque native, appels cryptographiques via OpenSSL, interactions bas niveau avec le système. Vous apprendrez à les identifier et à les intercepter.

**Le patching d'IL avec dnSpy.** Là où le patching d'un binaire natif impose de manipuler des opcodes x86 bruts et de gérer des contraintes d'alignement, le patching d'IL est presque confortable. dnSpy permet d'éditer les instructions CIL d'une méthode, de modifier le corps d'une fonction, voire de réécrire des blocs entiers en C# que l'outil recompile automatiquement en IL. Vous verrez comment modifier le comportement d'une application .NET de manière chirurgicale.

**Un cas pratique intégré.** Pour consolider ces techniques, vous les appliquerez sur une application .NET de vérification de licence fournie dans le dépôt. L'objectif : comprendre le mécanisme de validation, le contourner par débogage, par hooking et par patching, puis écrire un keygen.

## Parallèles avec l'analyse dynamique native

Si vous avez suivi les parties précédentes de cette formation, vous disposez déjà de tous les réflexes nécessaires. Le tableau ci-dessous met en correspondance les techniques natives et leurs équivalents .NET :

| Technique native (Parties II–V) | Équivalent .NET (ce chapitre) |  
|---|---|  
| GDB / GEF sur binaire ELF | dnSpy debugger sur assembly .NET |  
| Breakpoint sur adresse (`break *0x401234`) | Breakpoint sur méthode C# décompilée |  
| Frida hooking de fonctions C/C++ | Frida `frida-clr` hooking de méthodes .NET |  
| `LD_PRELOAD` pour intercepter des appels | Hooking P/Invoke pour intercepter les appels natifs |  
| Patching d'opcodes x86 avec ImHex | Édition d'instructions IL avec dnSpy |  
| `strace` / `ltrace` pour tracer les appels | dnSpy + Frida pour tracer les appels .NET |

La philosophie reste identique : observer, comprendre, puis intervenir. Seuls les outils et le niveau d'abstraction changent.

## Spécificités du runtime .NET à garder en tête

Avant de plonger dans la pratique, quelques particularités du runtime .NET méritent d'être rappelées, car elles influencent directement notre approche dynamique.

**La compilation JIT.** Le code CIL n'est pas exécuté directement : le compilateur JIT (Just-In-Time) du CLR le traduit en code natif au moment de l'exécution, méthode par méthode, lors de leur premier appel. Cela signifie que lorsque vous posez un breakpoint dans dnSpy, celui-ci est en réalité placé sur le code natif généré par le JIT — mais l'outil vous présente la correspondance avec le C# décompilé. Cette couche d'abstraction est transparente la plupart du temps, mais elle peut avoir des effets sur l'ordre d'exécution et les optimisations observées.

**Le Garbage Collector.** Le GC .NET déplace les objets en mémoire. Contrairement à l'analyse d'un binaire C/C++ où une adresse mémoire reste stable tant qu'on ne libère pas explicitement, un objet .NET peut changer d'adresse entre deux pauses du débogueur. Les outils comme dnSpy gèrent cela de manière transparente via les handles du runtime, mais c'est un point à connaître si vous faites de l'instrumentation bas niveau.

**Les domaines d'application et le chargement d'assemblies.** Le CLR peut charger des assemblies dynamiquement à l'exécution (`Assembly.Load`, `Assembly.LoadFrom`). Certains obfuscateurs exploitent ce mécanisme pour déchiffrer et charger du code en mémoire sans jamais l'écrire sur disque. L'analyse dynamique est alors le seul moyen d'accéder au code réel.

**NativeAOT et ReadyToRun.** Comme vu au chapitre 30, les applications compilées en NativeAOT ne passent plus par le CLR et le JIT : elles produisent un binaire natif classique. Dans ce cas, les techniques de ce chapitre ne s'appliquent pas, et il faut revenir aux méthodes des parties II à V. Les assemblies ReadyToRun contiennent à la fois du code pré-compilé et du CIL, ce qui les rend analysables par les deux approches.

## Outils utilisés dans ce chapitre

| Outil | Rôle | Installation |  
|---|---|---|  
| **dnSpy / dnSpyEx** | Décompilation + débogage intégré | [github.com/dnSpyEx/dnSpy](https://github.com/dnSpyEx/dnSpy) |  
| **Frida + frida-clr** | Instrumentation dynamique du CLR | `pip install frida-tools` |  
| **dotnet CLI** | Compilation et exécution des samples | [dot.net](https://dot.net) |  
| **ILSpy** | Décompilation de référence (comparaison) | [github.com/icsharpcode/ILSpy](https://github.com/icsharpcode/ILSpy) |

> 💡 **Note :** dnSpy original n'est plus maintenu. Le fork **dnSpyEx** est la version à utiliser. Dans la suite de ce chapitre, le terme « dnSpy » désigne dnSpyEx sauf mention contraire.

## Plan du chapitre

- 32.1 [Déboguer un assembly avec dnSpy sans les sources](/32-analyse-dynamique-dotnet/01-debug-dnspy-sans-sources.md)  
- 32.2 [Hooking de méthodes .NET avec Frida (`frida-clr`)](/32-analyse-dynamique-dotnet/02-hooking-frida-clr.md)  
- 32.3 [Intercepter des appels P/Invoke (pont .NET → bibliothèques natives GCC)](/32-analyse-dynamique-dotnet/03-pinvoke-interception.md)  
- 32.4 [Patcher un assembly .NET à la volée (modifier l'IL avec dnSpy)](/32-analyse-dynamique-dotnet/04-patcher-il-dnspy.md)  
- 32.5 [Cas pratique : contourner une vérification de licence C#](/32-analyse-dynamique-dotnet/05-cas-pratique-licence-csharp.md)  
- [**🎯 Checkpoint** : patcher et keygenner l'application .NET fournie](/32-analyse-dynamique-dotnet/checkpoint.md)

---


⏭️ [Déboguer un assembly avec dnSpy sans les sources](/32-analyse-dynamique-dotnet/01-debug-dnspy-sans-sources.md)
