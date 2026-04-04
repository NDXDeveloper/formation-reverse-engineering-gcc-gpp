🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 30.5 — NativeAOT et ReadyToRun : quand le C# devient du code natif

> 📚 **Objectif de cette section** — Comprendre les modes de compilation ahead-of-time de .NET, savoir reconnaître un binaire produit par ReadyToRun ou NativeAOT, et identifier quelles techniques de RE — managées ou natives — s'appliquent dans chaque cas.

---

## Le modèle classique : tout repose sur le JIT

Dans le modèle .NET standard décrit en section 30.1, le bytecode CIL est compilé en code machine **au moment de l'exécution** par le compilateur JIT du CLR. Ce modèle a des avantages (le JIT peut optimiser pour le CPU exact de la machine hôte) mais aussi des inconvénients bien connus : temps de démarrage plus lent (chaque méthode doit être compilée à sa première invocation), consommation mémoire accrue (le JIT lui-même réside en mémoire), et dépendance au runtime .NET installé sur la machine cible.

Du point de vue du reverser, le modèle JIT est confortable : le fichier distribué contient du CIL pur avec des metadata complètes, et les outils de décompilation .NET (ILSpy, dnSpy) fonctionnent directement. C'est le cas de figure couvert par les sections 30.1 à 30.4.

Mais Microsoft a introduit deux alternatives qui compilent tout ou partie du code C# en instructions machine **avant l'exécution** — rapprochant ainsi le binaire .NET d'un exécutable natif classique. Ces deux modes ont des implications radicalement différentes pour l'analyste.

## ReadyToRun (R2R) : le modèle hybride

### Principe

ReadyToRun (R2R), introduit avec .NET Core 3.0 et stabilisé dans les versions suivantes, est un format de **pré-compilation partielle**. Le compilateur `crossgen2` (invoqué automatiquement par `dotnet publish` avec l'option appropriée) génère du code machine natif pour chaque méthode et l'embarque dans l'assembly, **à côté** du bytecode CIL original.

```
$ dotnet publish -c Release -r linux-x64 /p:PublishReadyToRun=true
```

Le résultat est un assembly PE qui contient les deux représentations :

```
┌───────────────────────────────────────────┐
│          Assembly ReadyToRun              │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  Bytecode CIL    (toujours présent) │  │
│  │  + Metadata       (intactes)        │  │
│  └─────────────────────────────────────┘  │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  Code natif R2R   (pré-compilé)     │  │
│  │  x86-64 ou ARM64 selon la cible     │  │
│  └─────────────────────────────────────┘  │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  R2R Header + ReadyToRunInfo        │  │
│  │  (table de correspondance           │  │
│  │   méthode CIL → code natif)         │  │
│  └─────────────────────────────────────┘  │
└───────────────────────────────────────────┘
```

Au runtime, le CLR utilise le code natif pré-compilé pour éviter la phase de JIT. Le CIL reste disponible comme **fallback** : si le runtime détecte que le code R2R n'est pas compatible (version du runtime différente, mise à jour du GC, etc.), il ignore les images natives et recompile le CIL via le JIT classique. C'est un système à double bande.

### Comment le reconnaître

Un assembly R2R reste un fichier PE avec un CLR Header et des metadata complètes. La commande `file` produit la même sortie qu'un assembly classique (`Mono/.Net assembly`). Le triage avec `strings` et `monodis` fonctionne normalement — les noms de types, méthodes et chaînes utilisateur sont intacts.

Le signal distinctif est la présence d'un **R2R Header** dans la section `.text`, identifiable par son magic `RTR\0` (octets `52 54 52 00`). Avec ImHex :

```
Recherche hexadécimale : 52 54 52 00  
Résultat : trouvé à l'offset 0x1A00 (exemple)  
```

L'outil en ligne de commande `r2rdump` (fourni avec le SDK .NET) permet d'inspecter le contenu R2R de manière structurée :

```
$ dotnet tool install -g r2rdump    # installation unique
$ r2rdump --in CrackMe.dll --header

ReadyToRun header:
  MajorVersion: 9
  MinorVersion: 2
  Flags: 0x00000023 (COMPOSITE | PLATFORM_NEUTRAL_SOURCE | MULTIMODULE_SINGLE_FILE)
  NumberOfSections: 17
```

On peut également lister la correspondance entre les méthodes CIL et leurs offsets natifs :

```
$ r2rdump --in CrackMe.dll --methods
  CrackMe.LicenseChecker.ValidateKey(String) @ 0x00002C40
  CrackMe.Program.Main(String[]) @ 0x00002B80
```

### Impact sur le RE

Voici le point crucial pour le reverser : **R2R ne retire rien, il ajoute**. Le CIL et les metadata sont intégralement préservés. Les décompilateurs .NET (ILSpy, dnSpy) continuent de fonctionner parfaitement — ils lisent le CIL et les metadata, ignorant complètement le code natif R2R.

En pratique, un assembly R2R se reverse exactement comme un assembly classique. La présence du code natif pré-compilé ne gêne pas l'analyse managée et ne constitue pas une protection. C'est une optimisation de performance, pas une technique d'obfuscation.

La seule situation où le code natif R2R présente un intérêt analytique est quand on souhaite étudier les **optimisations réellement appliquées** au runtime — le code R2R reflète les décisions du compilateur AOT (`crossgen2`), qui peuvent différer de celles du JIT. Pour le reverser typique, ce cas est marginal.

> 📖 **Résumé R2R** — CIL présent, metadata intactes, décompilation normale. Le code natif est un bonus de performance, pas un obstacle. L'analyste peut ignorer le R2R et travailler sur le CIL.

## NativeAOT : la rupture

### Principe

NativeAOT (Native Ahead-of-Time), disponible en production depuis .NET 7, est un mode de compilation fondamentalement différent. Le compilateur AOT (`ILC` — IL Compiler) transforme le CIL en un exécutable **entièrement natif** qui s'exécute **sans le CLR et sans le JIT**. Le résultat est un binaire autonome (self-contained) qui ne dépend d'aucun runtime .NET installé sur la machine.

```
$ dotnet publish -c Release -r linux-x64 /p:PublishAot=true
```

Le résultat sous Linux est un fichier **ELF** standard :

```
$ file CrackMe
CrackMe: ELF 64-bit LSB pie executable, x86-64, version 1 (GNU/Linux),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=a3b2c1..., for GNU/Linux 3.2.0, stripped  
```

Aucune mention de `Mono/.Net assembly`. Pas de CLR Header. Pas de magic `BSJB`. Le fichier ressemble à la sortie de GCC — parce que c'est, à peu de choses près, ce qu'il est : du code machine natif linké avec un runtime minimal embarqué.

```
┌───────────────────────────────────────────┐
│        Binaire NativeAOT (ELF)            │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  Code machine x86-64                │  │
│  │  (compilé depuis le CIL par ILC)    │  │
│  └─────────────────────────────────────┘  │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  Runtime minimal embarqué           │  │
│  │  (GC, threading, exception handling)│  │
│  └─────────────────────────────────────┘  │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  Metadata de réflexion (réduites)   │  │
│  │  (seulement si réflexion utilisée)  │  │
│  └─────────────────────────────────────┘  │
│                                           │
│  PAS de CIL                               │
│  PAS de CLR/JIT                           │
│  PAS de metadata complètes                │
└───────────────────────────────────────────┘
```

### Ce qui disparaît

La conséquence pour le reverser est massive. Comparons ce que contient un assembly classique et ce que contient un binaire NativeAOT :

| Élément | Assembly classique | NativeAOT |  
|---|---|---|  
| Bytecode CIL | Intégral | **Absent** — compilé en natif puis éliminé |  
| Metadata complètes (tables `TypeDef`, `MethodDef`…) | Intégrales | **Absentes** ou très réduites |  
| Heap `#Strings` (noms de types/méthodes) | Complet | **Partiellement présent** (uniquement pour les types utilisés par réflexion) |  
| Heap `#US` (chaînes utilisateur) | Complet | Les chaînes sont dans `.rodata` (comme en C natif) |  
| CLR Header + magic BSJB | Présents | **Absents** |  
| Noms de fonctions dans la table de symboles | Via metadata .NET | Présents si non strippé, **perdus si strippé** (comme GCC) |  
| Décompilation C# (ILSpy/dnSpy) | Quasi parfaite | **Impossible** — pas de CIL à décompiler |  
| Désassemblage x86-64 (Ghidra/IDA) | Non pertinent (CIL, pas x86) | **Pertinent** — c'est du code machine |

En d'autres termes : un binaire NativeAOT strippé est, du point de vue du reverser, **indiscernable d'un binaire C compilé par GCC et strippé**. Les outils .NET sont inutiles ; ce sont les outils natifs — Ghidra, IDA, GDB, Frida, `objdump` — qui reprennent la main. Toutes les techniques vues dans les parties I à IV s'appliquent directement.

### Comment le reconnaître

La détection d'un binaire NativeAOT est moins immédiate que celle d'un assembly classique, précisément parce qu'il ressemble à un exécutable natif ordinaire. Voici les indices :

**`file` rapporte un ELF ou PE natif** — sans mention de `.Net assembly`. C'est le premier signal : si vous attendiez un programme C# et que `file` dit ELF, vous êtes probablement face à du NativeAOT (ou à l'hôte natif d'un déploiement classique — vérifiez l'absence de `.dll` adjacente).

**`strings` révèle des traces du runtime .NET** — Même compilé en natif, un binaire NativeAOT embarque le runtime minimal .NET (garbage collector, gestion des threads, gestion des exceptions). Les chaînes de ce runtime sont présentes dans le binaire :

```
$ strings CrackMe | grep -iE "System\.|Microsoft\.|\.NET|coreclr|S_OK"
System.Private.CoreLib  
System.Runtime.ExceptionServices  
System.Collections.Generic  
Microsoft.Win32  
```

La présence de namespaces .NET (`System.*`, `Microsoft.*`) dans un ELF natif est un signe fort de NativeAOT. Un programme C compilé par GCC ne contiendrait jamais ces chaînes.

**`readelf` montre des symboles caractéristiques** — Si le binaire n'est pas strippé, la table de symboles contient des noms reconnaissables :

```
$ readelf -s CrackMe | grep -i "S_P_CoreLib\|RhNew\|RhpGc\|__managed__"
  142: 00000000004a2340  FUNC    CrackMe_CrackMe_LicenseChecker__ValidateKey
  143: 00000000004a2100  FUNC    CrackMe_CrackMe_Program__Main
  287: 00000000004f8000  FUNC    S_P_CoreLib_System_String__Concat
  412: 0000000000501200  FUNC    RhpGcAlloc
```

Le schéma de nommage des fonctions NativeAOT est caractéristique : `NomAssembly_Namespace_Classe__Méthode`, avec des doubles underscores pour séparer la classe de la méthode. Les fonctions préfixées par `Rhp` ou `Rh` appartiennent au runtime (Runtime Helper). Les fonctions préfixées par `S_P_CoreLib` proviennent de la bibliothèque standard .NET compilée en natif.

Si le binaire est strippé, ces symboles disparaissent — mais les chaînes du runtime (`System.*`) dans `.rodata` subsistent et trahissent l'origine .NET.

**La taille du binaire est un indice contextuel** — Un simple « Hello World » en C# compilé en NativeAOT pèse typiquement entre 1 et 3 Mo sous Linux (le runtime embarqué ajoute un poids de base significatif). Le même programme compilé par GCC en C pèse quelques dizaines de Ko. Un binaire inhabituellement gros pour sa fonctionnalité apparente peut indiquer un NativeAOT — ou un binaire Go ou Rust (chapitres 33–34), qui présentent le même phénomène de runtime embarqué.

### Impact sur le RE

Face à un binaire NativeAOT, l'analyste doit basculer mentalement vers le workflow natif des parties I à IV :

**Analyse statique** — Ghidra ou IDA sont les outils de désassemblage et décompilation. Le décompilateur de Ghidra produit du pseudo-C (pas du C#), avec la qualité habituelle — approximative mais exploitable. Les techniques de reconstruction vues aux chapitres 8 et 20 s'appliquent.

**Analyse dynamique** — GDB (chapitre 11) pour le débogage, Frida (chapitre 13) pour l'instrumentation dynamique, `strace`/`ltrace` (section 5.5) pour les appels système. dnSpy est inutile : il n'y a pas de CIL à déboguer.

**Identification des fonctions** — Si le binaire n'est pas strippé, le schéma de nommage NativeAOT (`Namespace_Classe__Méthode`) permet de reconstruire rapidement la structure du programme. Si le binaire est strippé, les signatures de la bibliothèque standard .NET (FLIRT pour IDA, signatures Ghidra — section 20.5) aident à identifier les fonctions du runtime et de la BCL (Base Class Library) pour les distinguer du code applicatif.

**Spécificités du runtime** — Le garbage collector de .NET est présent dans le binaire et intervient dans la gestion mémoire. Les allocations passent par des fonctions du runtime (`RhpGcAlloc`, `RhpNewObject`) plutôt que par `malloc`. Les appels virtuels passent par des dispatchers spécifiques. Ces patterns sont reconnaissables avec l'habitude, mais constituent un bruit de fond que l'analyste doit apprendre à filtrer — exactement comme le runtime Go (section 34.1) ou le runtime Rust ajoutent du code infrastructure autour du code applicatif.

### NativeAOT avec trimming : le cas extrême

NativeAOT active par défaut le **trimming** (élagage) : le compilateur analyse le graphe d'appels et supprime tout le code non atteignable statiquement. Le résultat est un binaire plus petit, mais aussi plus opaque — des pans entiers de la bibliothèque standard sont absents, et les mécanismes de réflexion sont limités ou désactivés.

Le trimming a un effet direct sur le RE : moins de code signifie moins de bruit, mais aussi moins de points d'ancrage connus. L'analyste dispose de moins de fonctions de la BCL identifiables par signature, ce qui complique l'orientation initiale dans le binaire.

## Synthèse : quel mode, quels outils ?

| Mode de publication | Format du binaire | CIL présent ? | Metadata complètes ? | Outils de RE principaux | Difficulté RE |  
|---|---|---|---|---|---|  
| `dotnet build` / `dotnet publish` (standard) | PE (.dll/.exe) | Oui | Oui | ILSpy, dnSpy, monodis | Faible (non obfusqué) à moyenne (obfusqué) |  
| `PublishReadyToRun=true` | PE (.dll/.exe) | Oui | Oui | ILSpy, dnSpy, monodis (ignorer le R2R) | Identique au standard |  
| `PublishAot=true` (NativeAOT) | ELF / PE natif | **Non** | **Non** | Ghidra, IDA, GDB, Frida, objdump | Élevée (comparable au RE natif C/C++) |  
| NativeAOT + `strip` | ELF / PE natif strippé | **Non** | **Non** | Ghidra, IDA, GDB, Frida | Très élevée (identique à GCC strippé) |

Le diagramme de décision pour l'analyste est simple :

```
Le binaire contient-il un CLR Header (Data Directory 14) ?
│
├─ OUI → Assembly .NET classique (ou R2R)
│        → Outils .NET : ILSpy, dnSpy, monodis
│        → Techniques des chapitres 30-32
│
└─ NON → Binaire natif
         │
         ├─ strings révèle "System.*" / "S_P_CoreLib" / "Rhp*" ?
         │  → Probable NativeAOT
         │  → Outils natifs : Ghidra, IDA, GDB
         │  → Techniques des parties I-IV
         │  → Les symboles NativeAOT (si non strippé) aident à l'orientation
         │
         └─ Aucune trace .NET ?
            → Binaire natif classique (C, C++, Rust, Go…)
            → Outils natifs : Ghidra, IDA, GDB
            → Techniques des parties I-IV (ou VIII pour Rust/Go)
```

## Perspective : la convergence des mondes

L'émergence de NativeAOT illustre une tendance de fond dans l'industrie : la frontière entre code managé et code natif s'estompe progressivement. Swift, Kotlin/Native, Dart (Flutter) et maintenant C#/.NET permettent de produire des binaires natifs autonomes à partir de langages à garbage collector.

Pour le reverser, cette convergence a une conséquence pratique : **les compétences en RE natif (x86-64, ELF, Ghidra, GDB) restent le socle universel**. Elles s'appliquent à tout binaire natif, que le code source ait été écrit en C, C++, Rust, Go, Swift, ou C# compilé en NativeAOT. Les compétences en RE managé (.NET, JVM) sont un complément spécialisé pour les cas — encore majoritaires — où le bytecode est distribué.

La formation que vous suivez, centrée sur la chaîne GNU et le RE natif, vous a donné ce socle. Les chapitres 30 à 32 y ajoutent la couche managée .NET. Avec les deux, vous êtes équipé pour faire face à l'ensemble du spectre — du binaire GCC strippé en `-O3` à l'assembly C# obfusqué par ConfuserEx, en passant par le cas hybride du NativeAOT.

---

> 📖 **À retenir** — ReadyToRun ajoute du code natif pré-compilé à côté du CIL, sans retirer les metadata : le RE reste managé. NativeAOT élimine le CIL et les metadata pour produire un exécutable entièrement natif : le RE bascule vers les techniques des parties I à IV. La détection passe par la présence ou l'absence du CLR Header et par la recherche de traces du runtime .NET (`System.*`, `Rhp*`) dans un binaire natif. Un reverser formé aux deux mondes — natif et managé — est préparé à tous les scénarios de déploiement .NET modernes.

---

> 🔚 **Fin du chapitre 30.** Les bases sont posées : vous comprenez le modèle d'exécution .NET, la structure d'un assembly, les protections courantes, le triage initial, et les implications de la compilation ahead-of-time. Le chapitre 31 passe à la pratique avec les outils de décompilation (ILSpy, dnSpy, dotPeek), et le chapitre 32 aborde l'analyse dynamique et le hooking d'assemblies .NET.


⏭️ [Chapitre 31 — Décompilation d'assemblies .NET](/31-decompilation-dotnet/README.md)
