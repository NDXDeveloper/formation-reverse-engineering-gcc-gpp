🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 30.1 — Différences fondamentales : bytecode CIL vs code natif x86-64

> 📚 **Objectif de cette section** — Comprendre pourquoi un assembly .NET ne ressemble pas à un binaire GCC, et mesurer les conséquences directes de cette différence pour le reverse engineer.

---

## Deux philosophies de compilation

Depuis le début de cette formation, vous travaillez avec un modèle de compilation **direct** : GCC transforme votre code source C ou C++ en instructions x86-64 que le processeur exécute sans intermédiaire. Le résultat est un fichier ELF contenant du code machine natif — des octets que le CPU décode et exécute cycle par cycle.

```
┌──────────┐     ┌──────────────┐     ┌───────────────┐     ┌──────────┐
│  main.c  │ ──→ │ Préprocesseur│ ──→ │  Compilateur  │ ──→ │   main   │
│ (source) │     │  + Compilo   │     │  + Assembleur │     │  (ELF)   │
│          │     │   GCC        │     │  + Linker     │     │ x86-64   │
└──────────┘     └──────────────┘     └───────────────┘     └──────────┘
                                                             Code machine
                                                             natif → CPU
```

Le modèle .NET repose sur une philosophie radicalement différente : la **compilation en deux temps**.

```
┌───────────┐     ┌──────────────┐     ┌───────────────┐     ┌──────────┐
│ Program.cs│ ──→ │  Compilateur │ ──→ │   Assembly    │ ──→ │  Runtime │
│ (source)  │     │   Roslyn     │     │   .NET (PE)   │     │  CLR     │
│           │     │   (csc)      │     │  Bytecode CIL │     │  + JIT   │
└───────────┘     └──────────────┘     └───────────────┘     └──────────┘
                                        Code intermédiaire    Compilation
                                        indépendant du CPU    en x86-64
                                                              à l'exécution
```

La première phase — à la compilation — produit non pas du code machine, mais du **bytecode CIL** (Common Intermediate Language, anciennement appelé MSIL). Ce bytecode est un jeu d'instructions abstrait, conçu pour être indépendant de l'architecture matérielle. Il n'est exécutable par aucun processeur physique.

La seconde phase — à l'exécution — est assurée par le **CLR** (Common Language Runtime), qui embarque un compilateur **JIT** (Just-In-Time). Le JIT traduit le CIL en code machine natif (x86-64 sur votre PC, ARM64 sur un Mac Apple Silicon) au moment où chaque méthode est appelée pour la première fois. C'est seulement à ce stade que le processeur reçoit des instructions qu'il sait exécuter.

## Le CIL : un assembleur de haut niveau

Pour un reverser habitué à l'assembleur x86-64, le CIL est déroutant par son niveau d'abstraction. Là où x86-64 manipule des registres physiques (`rax`, `rdi`, `rsp`…) et des adresses mémoire brutes, le CIL fonctionne avec une **machine à pile virtuelle** (stack-based VM) et manipule des concepts de haut niveau : types, objets, méthodes, exceptions.

Prenons un exemple concret. Voici une fonction triviale en C et son équivalent en C# :

**C (compilé par GCC) :**
```c
int add(int a, int b) {
    return a + b;
}
```

**C# :**
```csharp
static int Add(int a, int b) {
    return a + b;
}
```

Après compilation par GCC en `-O0` (sans optimisation), le désassemblage x86-64 de la fonction C donne quelque chose comme :

```asm
add:
    push   rbp
    mov    rbp, rsp
    mov    DWORD PTR [rbp-4], edi    ; paramètre a (via registre)
    mov    DWORD PTR [rbp-8], esi    ; paramètre b (via registre)
    mov    edx, DWORD PTR [rbp-4]
    mov    eax, DWORD PTR [rbp-8]
    add    eax, edx
    pop    rbp
    ret
```

On retrouve ici les concepts vus au chapitre 3 : prologue (`push rbp` / `mov rbp, rsp`), passage des paramètres via les registres de la convention System V AMD64 (`edi`, `esi`), stockage sur la pile locale, instruction arithmétique matérielle (`add`), et retour via `eax`. Avec `-O2`, GCC réduit tout cela à une seule instruction `lea eax, [rdi+rsi]` suivie de `ret` — et toute trace de la structure du code source disparaît.

Le bytecode CIL de la méthode C#, lui, ressemble à ceci :

```
.method private hidebysig static int32 Add(int32 a, int32 b) cil managed
{
    .maxstack 2
    ldarg.0        // Empile le paramètre 'a' sur la pile d'évaluation
    ldarg.1        // Empile le paramètre 'b'
    add            // Dépile les deux, additionne, empile le résultat
    ret            // Retourne la valeur au sommet de la pile
}
```

Plusieurs différences sautent aux yeux.

**Le CIL conserve les types et les noms.** La signature de la méthode indique explicitement qu'elle s'appelle `Add`, qu'elle prend deux `int32` en paramètres, qu'elle retourne un `int32`, qu'elle est `static`, `private`, et `hidebysig`. Rien de tout cela n'existe dans le désassemblage x86-64 d'un binaire GCC strippé : le nom de la fonction a disparu, le type des paramètres est perdu, et la visibilité (`static`, `private`) n'a aucune représentation en code machine.

**Le CIL est indépendant de l'architecture.** Pas de registres physiques, pas d'adresses mémoire, pas de convention d'appel spécifique. Les instructions `ldarg.0` et `ldarg.1` désignent les paramètres par leur position logique, pas par un registre matériel. Ce même bytecode s'exécutera sans modification sur x86-64, ARM64, ou toute autre architecture supportée par le CLR.

**Le CIL ne subit pas d'optimisations destructrices à la compilation.** Le compilateur Roslyn (`csc`) applique très peu d'optimisations au bytecode CIL — c'est le JIT qui s'en charge au moment de l'exécution. Conséquence directe : le CIL reflète fidèlement la structure logique du code source (boucles, conditions, appels de méthodes), contrairement à du x86-64 en `-O2` ou `-O3` où l'inlining, le déroulage de boucles et la vectorisation (chapitres 16.2 à 16.4) peuvent rendre le code méconnaissable.

## La machine à pile vs. la machine à registres

Cette distinction architecturale mérite qu'on s'y arrête, car elle affecte la façon dont on lit le « désassemblage » dans les deux mondes.

Le x86-64 est une **architecture à registres**. Les opérations arithmétiques travaillent directement sur des registres nommés (`add eax, edx` additionne le contenu de `edx` au contenu de `eax`). Le reverser doit donc traquer la valeur de chaque registre à travers le flux d'exécution — c'est le cœur du travail d'analyse vu depuis le chapitre 3.

Le CIL est une **machine à pile d'évaluation**. Chaque instruction consomme des valeurs depuis le sommet de la pile et y empile son résultat. Il n'y a pas de registres à suivre : l'état de la computation est entièrement décrit par le contenu de la pile. Cela rend le CIL plus facile à analyser séquentiellement, mais aussi plus verbeux — une seule instruction x86-64 comme `lea rax, [rdi+rsi*4+8]` nécessiterait plusieurs instructions CIL.

En pratique, vous lirez rarement du CIL brut lors d'une analyse .NET. Les décompilateurs (ILSpy, dnSpy — chapitre 31) convertissent directement le CIL en code C# de haut niveau. Mais comprendre que le CIL existe et comment il fonctionne est indispensable pour savoir *pourquoi* la décompilation .NET est si fidèle — et *quand* elle cesse de l'être.

## Les metadata : la richesse informationnelle du .NET

C'est ici que la différence entre les deux mondes est la plus spectaculaire du point de vue du reverser.

Un binaire ELF natif compilé par GCC avec l'option `-s` (strip) ne contient pratiquement **aucune information sémantique**. Les noms de fonctions ont disparu. Les types des variables sont perdus. Les relations entre classes (en C++) ne sont que partiellement reconstruisibles via les vtables et le RTTI (chapitre 17). Reconstruire la logique d'un tel binaire est un travail patient qui repose sur l'analyse du code machine instruction par instruction.

Un assembly .NET, même après compilation en mode Release, transporte avec lui un bloc de **metadata** qui décrit exhaustivement :

- Toutes les **classes**, **interfaces**, **structures** et **enums** définies dans l'assembly, avec leurs noms, leur visibilité et leurs relations d'héritage.  
- Toutes les **méthodes** de chaque type, avec leurs noms, signatures complètes (types des paramètres et de la valeur de retour), modificateurs d'accès (`public`, `private`, `protected`, `internal`), et attributs (`virtual`, `override`, `static`, `async`…).  
- Tous les **champs** (fields) et **propriétés** de chaque type, avec leurs types et leur visibilité.  
- Les **chaînes de caractères** utilisées dans le code, stockées dans un heap dédié (`#Strings` et `#US` — User Strings).  
- Les **dépendances** vers d'autres assemblies (équivalent des bibliothèques partagées en natif).  
- Les **attributs custom** appliqués aux types et méthodes (annotations .NET).

Pour mettre cela en perspective : quand vous analysez un binaire GCC strippé dans Ghidra (chapitre 8), votre premier travail consiste à identifier les fonctions, deviner leurs paramètres, leur donner des noms significatifs, et reconstruire les structures de données — un processus qui peut prendre des heures ou des jours sur un binaire non trivial. Sur un assembly .NET non obfusqué, **toutes ces informations sont déjà présentes dans le fichier**. Le décompilateur n'a qu'à les lire.

C'est cette richesse de metadata qui explique pourquoi le RE .NET est souvent perçu comme « facile » comparé au RE natif. Mais cette facilité disparaît dès qu'un obfuscateur entre en jeu (section 30.3) : le renommage des symboles détruit les noms, le chiffrement des chaînes masque les constantes, et le control flow flattening rend le bytecode aussi opaque qu'un binaire natif obfusqué. Le reverser se retrouve alors face aux mêmes défis qu'en natif — mais avec des outils différents.

## Tableau comparatif synthétique

| Caractéristique | Binaire natif GCC (ELF x86-64) | Assembly .NET (PE + CIL) |  
|---|---|---|  
| **Contenu exécutable** | Instructions machine x86-64 | Bytecode CIL (machine à pile virtuelle) |  
| **Exécution** | Directe par le CPU | Via le CLR + compilation JIT |  
| **Noms de fonctions** | Présents seulement si non strippé | Toujours présents dans les metadata |  
| **Types et signatures** | Perdus après compilation (sauf DWARF `-g`) | Conservés intégralement dans les metadata |  
| **Hiérarchie de classes** | Partiellement reconstruisible (vtables, RTTI) | Décrite explicitement dans les metadata |  
| **Chaînes de caractères** | Dans `.rodata`, sans contexte d'usage | Dans les heaps de metadata, liées aux méthodes |  
| **Optimisations** | Appliquées à la compilation (`-O2`, `-O3`) | Appliquées au runtime par le JIT |  
| **Décompilation** | Approximative (pseudo-C dans Ghidra) | Quasi parfaite (C# lisible dans ILSpy/dnSpy) |  
| **Portabilité** | Liée à l'architecture cible | Indépendante de l'architecture (un seul binaire) |  
| **Format de fichier** | ELF (Linux), PE (Windows), Mach-O (macOS) | PE avec header CLR (toutes plateformes via .NET) |  
| **Protection anti-RE** | Strip, UPX, obfuscation LLVM, anti-debug | Obfuscateurs CIL (ConfuserEx, Dotfuscator…) |

## Ce que cela change pour le reverser

Si vous venez des parties I à VI de cette formation, voici les ajustements concrets que demande le passage au RE .NET :

**Ce qui disparaît.** Vous n'aurez plus besoin de lire de l'assembleur x86-64 instruction par instruction, de reconstruire les stack frames, de traquer les registres à travers les basic blocks, ni de deviner la convention d'appel. Le CIL — et surtout le C# décompilé — est lisible directement.

**Ce qui reste.** La démarche analytique est identique : comprendre le flux de contrôle, identifier les routines critiques (vérification de licence, chiffrement, parsing de protocole), tracer les données depuis l'entrée jusqu'à la décision. Seule la granularité change — vous travaillez avec des méthodes C# plutôt qu'avec des basic blocks assembleur.

**Ce qui apparaît.** De nouveaux défis spécifiques au monde managé : l'obfuscation de bytecode (qui n'a pas d'équivalent direct en natif), la réflexion .NET (qui permet de charger et exécuter du code dynamiquement de façon bien plus simple qu'un `dlopen`/`dlsym`), le garbage collector (qui rend le tracking mémoire différent de ce que vous connaissez avec `malloc`/`free`), et les mécanismes de sérialisation qui sont un vecteur d'attaque propre à l'écosystème .NET.

**Ce qui converge.** Avec NativeAOT (section 30.5), Microsoft permet désormais de compiler du C# directement en code natif, sans passer par le CIL ni le JIT. Le binaire résultant ressemble alors beaucoup à la sortie de GCC : un exécutable ELF ou PE contenant du code machine, sans metadata exploitables. Dans ce cas, les techniques des parties I à IV redeviennent pleinement applicables — et votre expérience du RE natif reprend tout son avantage.

---

> 📖 **À retenir** — La différence fondamentale entre un binaire GCC et un assembly .NET réside dans le **niveau d'abstraction** du code distribué : du code machine opaque vs. du bytecode richement annoté. Cette différence a des conséquences directes sur la facilité de décompilation, mais ne change pas la méthodologie de fond du reverse engineer. Les mêmes questions se posent : que fait ce programme, comment le fait-il, et comment puis-je l'observer ou le modifier ?

---


⏭️ [Structure d'un assembly .NET : metadata, PE headers, sections CIL](/30-introduction-re-dotnet/02-structure-assembly-dotnet.md)
