🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 31 — Décompilation d'assemblies .NET

> 📦 **Partie VII — Bonus : RE sur Binaires .NET / C#**  
> 

---

## Pourquoi ce chapitre ?

Le chapitre 30 a posé les bases : structure d'un assembly .NET, rôle du bytecode CIL, headers PE spécifiques et obfuscateurs courants. Vous savez désormais *ce que* contient un assembly. Ce chapitre répond à la question suivante : **comment le transformer en code C# lisible et exploitable ?**

C'est ici que le reverse engineering .NET révèle son avantage majeur par rapport au RE de binaires natifs x86-64. Le bytecode CIL conserve une quantité considérable d'informations sémantiques — noms de types, signatures de méthodes, hiérarchies d'héritage, métadonnées de propriétés — que le compilateur natif détruit irrémédiablement. Un décompilateur .NET ne produit pas du pseudo-code approximatif comme Ghidra sur un binaire ELF strippé : il produit du **code C# quasi identique au source original**, avec les noms de variables locales en moins (sauf si le PDB est présent).

Cette fidélité de la décompilation a une conséquence directe sur votre méthodologie de RE : là où l'analyse d'un binaire natif exige des heures de renommage, de reconstruction de structures et de va-et-vient entre désassembleur et débogueur, un assembly .NET non obfusqué se lit presque comme un projet open source. Le vrai défi commence quand l'auteur a appliqué un obfuscateur — et c'est précisément pourquoi ce chapitre couvre aussi les techniques de contournement.

---

## Ce que vous allez apprendre

Ce chapitre explore trois décompilateurs .NET majeurs — **ILSpy**, **dnSpy/dnSpyEx** et **dotPeek** — en les confrontant aux mêmes assemblies pour que vous puissiez juger par vous-même de leurs forces et faiblesses. Vous apprendrez à :

- Ouvrir, naviguer et exporter du code C# décompilé avec chacun des trois outils.  
- Exploiter les fonctionnalités uniques de chaque décompilateur : le débogage intégré de dnSpy, l'export de projet complet d'ILSpy, la navigation avancée de dotPeek.  
- Comparer objectivement leurs résultats sur un même assembly, y compris en présence d'optimisations ou de constructions C# modernes (`async/await`, `Span<T>`, pattern matching).  
- Identifier l'obfuscateur appliqué à un assembly et utiliser **de4dot** pour restaurer un bytecode analysable.  
- Appliquer des techniques manuelles de contournement quand de4dot ne suffit pas.

---

## Prérequis

Ce chapitre suppose que vous maîtrisez les notions du chapitre 30 :

- La distinction entre bytecode CIL et code natif (section 30.1).  
- La structure d'un assembly .NET : metadata tables, PE headers, sections CIL (section 30.2).  
- Les obfuscateurs courants et leurs effets visibles (section 30.3) — ce chapitre montre comment les contrer en pratique.

Une familiarité minimale avec le langage C# est nécessaire pour évaluer la qualité du code décompilé. Vous n'avez pas besoin d'être un développeur C# expérimenté, mais vous devez pouvoir lire une classe, comprendre une propriété, suivre un `if/else` et reconnaître un appel de méthode.

Si vous venez directement de la Partie IV (RE natif), gardez à l'esprit que le workflow est fondamentalement différent : on ne raisonne plus en registres et en offsets mémoire, mais en types, méthodes et namespaces.

---

## Outils utilisés dans ce chapitre

| Outil | Rôle | Licence | Plateforme |  
|---|---|---|---|  
| **ILSpy** | Décompilateur C# open source, référence communautaire | MIT | Windows, Linux, macOS (via Avalonia) |  
| **dnSpy / dnSpyEx** | Décompilateur + débogueur intégré, édition d'IL | GPL v3 (dnSpyEx = fork maintenu) | Windows (.NET Framework) |  
| **dotPeek** | Décompilateur JetBrains, navigation type IDE | Gratuit (propriétaire) | Windows |  
| **de4dot** | Déobfuscateur automatique d'assemblies .NET | GPL v3 | Windows, Linux (Mono/.NET) |

> 💡 **dnSpy** n'est plus maintenu par son auteur original depuis 2020. Le fork communautaire **dnSpyEx** reprend le développement actif et ajoute le support de .NET 6/7/8+. Dans la suite de ce chapitre, « dnSpy » fait référence à dnSpyEx sauf mention contraire.

---

## Décompilation .NET vs décompilation native : un changement de paradigme

Pour bien situer ce chapitre par rapport aux Parties II–IV du tutoriel, il est utile de comprendre *pourquoi* la décompilation .NET est si différente de la décompilation d'un binaire GCC.

Quand GCC compile du C/C++ vers du code machine x86-64, il effectue des transformations destructives : les noms de variables locales disparaissent, les structures sont aplaties en accès mémoire par offset, les boucles `for` deviennent des séquences de `cmp`/`jcc`, et les optimisations (`-O2`, `-O3`) réarrangent le code au point de le rendre méconnaissable. Le décompilateur (Ghidra, RetDec) doit alors *deviner* la structure originale à partir d'indices indirects — c'est un processus heuristique, approximatif par nature.

Le compilateur C# (Roslyn) effectue une transformation très différente. Il produit du bytecode CIL (Common Intermediate Language) qui est conçu pour être interprété ou compilé à la volée (JIT) par le runtime .NET. Ce bytecode conserve :

- **Les noms complets des types** : namespaces, classes, interfaces, enums, delegates.  
- **Les signatures de méthodes** : noms, types de paramètres, type de retour, modificateurs d'accès (`public`, `private`, `internal`…).  
- **Les métadonnées de propriétés et événements** : getters, setters, handlers.  
- **La hiérarchie d'héritage et les implémentations d'interfaces**.  
- **Les attributs personnalisés** appliqués aux types et méthodes.  
- **Les tokens de références** vers d'autres assemblies et types externes.

Toute cette information est stockée dans les **metadata tables** de l'assembly, et les décompilateurs s'en servent pour reconstruire un code C# de haute fidélité. La perte d'information se limite essentiellement aux noms de variables locales (sauf si le fichier PDB est disponible), aux commentaires du développeur, et à certaines subtilités syntaxiques (un opérateur ternaire peut devenir un `if/else`, un `switch` sur des patterns peut être restructuré).

Cette richesse a un revers : elle rend la protection de la propriété intellectuelle beaucoup plus difficile pour les développeurs .NET, d'où l'existence d'une industrie entière d'obfuscateurs. C'est pour cette raison que la section 31.5 de ce chapitre est consacrée au contournement de l'obfuscation — c'est le seul véritable obstacle entre vous et le code source.

---

## Plan du chapitre

- **31.1** — [ILSpy — décompilation C# open source](/31-decompilation-dotnet/01-ilspy.md)  
- **31.2** — [dnSpy / dnSpyEx — décompilation + débogage intégré](/31-decompilation-dotnet/02-dnspy-dnspyex.md)  
- **31.3** — [dotPeek (JetBrains) — navigation et export de sources](/31-decompilation-dotnet/03-dotpeek.md)  
- **31.4** — [Comparatif ILSpy vs dnSpy vs dotPeek](/31-decompilation-dotnet/04-comparatif-outils.md)  
- **31.5** — [Décompiler malgré l'obfuscation : de4dot et techniques de contournement](/31-decompilation-dotnet/05-de4dot-contournement.md)

---


⏭️ [ILSpy — décompilation C# open source](/31-decompilation-dotnet/01-ilspy.md)
