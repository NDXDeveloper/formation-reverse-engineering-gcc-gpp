🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 30 — Introduction au RE .NET

> 🔗 **Partie VII — Bonus : RE sur Binaires .NET / C#**  
> *Pont direct avec le développement C# — les mêmes concepts de RE s'appliquent sur le bytecode CIL/.NET.*

---

## Pourquoi un chapitre sur .NET dans une formation centrée sur GCC ?

Tout au long des parties I à VI, nous avons travaillé exclusivement sur des binaires **natifs** : du code C ou C++ compilé par GCC (ou Clang) en instructions x86-64 directement exécutables par le processeur. Ce modèle — code source → code machine — est le cœur de cette formation, et il le restera.

Alors pourquoi ouvrir une parenthèse sur .NET ?

Parce que dans la pratique quotidienne du reverse engineer, les frontières entre les écosystèmes sont rarement étanches. Un audit de sécurité peut vous amener à analyser une application C# qui, en interne, appelle des bibliothèques natives compilées avec GCC via le mécanisme **P/Invoke**. Un dropper peut embarquer un payload .NET obfusqué à côté d'un loader ELF natif. Un outil interne d'entreprise peut mélanger du code managé et du code natif dans un même déploiement. Savoir reconnaître un assembly .NET quand on en croise un — et disposer des réflexes de base pour l'analyser — fait partie de la boîte à outils d'un reverser complet.

L'autre raison est **pédagogique** : le RE sur .NET illustre de façon frappante à quel point le modèle de compilation change la difficulté de l'analyse. Là où un binaire GCC strippé et optimisé en `-O2` vous confronte à des heures de reconstruction manuelle, un assembly .NET non obfusqué se décompile souvent en quelques secondes vers un code C# quasi identique au source original. Comprendre *pourquoi* cette différence existe — et *quand* elle disparaît (obfuscation, NativeAOT) — renforce votre compréhension des mécanismes fondamentaux vus dans les parties précédentes.

## Ce que ce chapitre couvre

Ce chapitre pose les bases nécessaires avant d'aborder la décompilation (chapitre 31) et l'analyse dynamique .NET (chapitre 32). Il ne prétend pas remplacer une formation dédiée au RE .NET, mais vous donner les clés pour :

- **Comprendre le modèle d'exécution .NET** et en quoi il diffère radicalement du modèle natif GCC. Le bytecode CIL (Common Intermediate Language) n'est pas du code machine : il est interprété puis compilé à la volée (JIT) par le runtime. Cette distinction a des conséquences majeures sur ce qu'un reverser peut extraire d'un binaire.

- **Lire la structure interne d'un assembly .NET** : les metadata, les tables de types, les headers PE spécifiques au CLR. Vous verrez que là où un ELF natif expose des sections comme `.text`, `.data` et `.rodata`, un assembly .NET transporte avec lui une description quasi complète de ses types, méthodes et dépendances — une mine d'or pour l'analyste.

- **Reconnaître les obfuscateurs courants** qui tentent de compenser cette transparence en rendant le bytecode illisible : renommage des symboles, chiffrement des chaînes, insertion de flux de contrôle factice. Vous apprendrez à identifier les signatures des outils les plus répandus (ConfuserEx, Dotfuscator, SmartAssembly) sans les confondre avec du code légitime.

- **Appliquer les outils que vous connaissez déjà** — `file`, `strings`, ImHex — pour réaliser un premier triage sur un assembly .NET, exactement comme vous le feriez sur un ELF natif (chapitre 5). Vous constaterez que certains réflexes se transposent directement, tandis que d'autres doivent être adaptés.

- **Anticiper l'évolution de l'écosystème** avec NativeAOT et ReadyToRun, deux technologies qui brouillent la frontière entre code managé et code natif. Quand un binaire C# est compilé en ahead-of-time, il produit un exécutable natif qui ressemble bien plus à la sortie de GCC qu'à un assembly .NET classique — et les techniques des parties I à IV redeviennent alors pertinentes.

## Ce que ce chapitre ne couvre pas

L'objectif n'est pas de faire de vous un expert du RE .NET en trois sections. Les sujets suivants sont volontairement hors périmètre ou traités dans les chapitres suivants :

- La **décompilation** proprement dite avec ILSpy, dnSpy ou dotPeek → chapitre 31.  
- Le **débogage** et le **hooking** d'assemblies .NET → chapitre 32.  
- Le RE de binaires **Java/JVM** (Kotlin, Scala) — un écosystème similaire dans l'esprit, mais avec ses propres outils et spécificités, qui sort du cadre de cette formation.  
- L'**exploitation** de vulnérabilités dans du code .NET (désérialisation, type confusion…) — un domaine à part entière qui relève davantage du pentest applicatif.

## Prérequis pour cette partie

Si vous avez suivi les parties I à V de cette formation, vous disposez de toutes les bases nécessaires. Les concepts suivants seront mobilisés :

- Le **workflow de triage rapide** vu au chapitre 5 (section 5.7) : `file`, `strings`, `readelf`, `checksec`.  
- La **structure d'un exécutable** (headers, sections, segments) vue au chapitre 2.  
- Les **notions de linking dynamique** (PLT/GOT, résolution de symboles) vues aux sections 2.7 à 2.9 — utiles pour comprendre P/Invoke et le pont entre code managé et code natif.  
- L'utilisation d'**ImHex** pour inspecter des structures binaires (chapitre 6).

Aucune expérience préalable en C# ou en développement .NET n'est requise. Les concepts seront introduits au fur et à mesure, et les parallèles avec le C/C++ seront systématiquement tracés.

## Plan du chapitre

| Section | Titre | Description |  
|---------|-------|-------------|  
| 30.1 | Différences fondamentales : bytecode CIL vs code natif x86-64 | Le modèle d'exécution .NET comparé à la compilation native GCC |  
| 30.2 | Structure d'un assembly .NET : metadata, PE headers, sections CIL | Anatomie d'un fichier .NET, en miroir de l'analyse ELF du chapitre 2 |  
| 30.3 | Obfuscateurs courants : ConfuserEx, Dotfuscator, SmartAssembly | Reconnaître et identifier les protections appliquées au bytecode |  
| 30.4 | Inspecter un assembly avec `file`, `strings` et ImHex | Appliquer le triage rapide du chapitre 5 sur une cible .NET |  
| 30.5 | NativeAOT et ReadyToRun : quand le C# devient du code natif | La convergence entre les deux mondes et ses implications pour le RE |

---

> 💡 **Note** — Ce chapitre ne comporte pas de checkpoint dédié. Les acquis seront validés par le checkpoint du chapitre 32, qui mobilise l'ensemble des connaissances des chapitres 30 à 32.

---

*Commençons par la question fondamentale : qu'est-ce qui distingue, au niveau le plus bas, un binaire produit par `gcc` d'un assembly produit par `dotnet build` ?*


⏭️ [Différences fondamentales : bytecode CIL vs code natif x86-64](/30-introduction-re-dotnet/01-cil-vs-natif.md)
