🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 30.2 — Structure d'un assembly .NET : metadata, PE headers, sections CIL

> 📚 **Objectif de cette section** — Savoir lire l'anatomie interne d'un assembly .NET, en établissant des parallèles systématiques avec la structure ELF que vous maîtrisez depuis le chapitre 2.

---

## Un fichier PE, pas un ELF

Premier point qui déroute souvent le reverser Linux : un assembly .NET est encapsulé dans un fichier au format **PE** (Portable Executable), le format binaire historique de Windows. Cela reste vrai même lorsque l'application cible .NET 6+ et s'exécute sous Linux — le runtime .NET sait charger un fichier PE quel que soit l'OS hôte.

Si vous avez suivi la section 2.3 sur les formats binaires, vous connaissez déjà la structure générale d'un PE : un DOS header hérité (avec le fameux magic `MZ`), suivi d'un PE header (`PE\0\0`), des Optional Headers, et d'une table de sections. Un assembly .NET conserve tout cela, mais y ajoute une couche spécifique : le **CLR Header** et l'ensemble des **metadata**.

En pratique, quand vous lancez `file` sur un assembly .NET, vous obtenez quelque chose comme :

```
$ file MyApp.dll
MyApp.dll: PE32 executable (console) Intel 80386 Mono/.Net assembly, for MS Windows
```

Le mot-clé `Mono/.Net assembly` confirme la présence du CLR Header. Sur un exécutable natif Windows compilé par MinGW, `file` afficherait simplement `PE32 executable` sans cette mention.

> 💡 **Parallèle ELF** — Là où un binaire Linux commence par le magic `\x7fELF` et expose sa structure via `readelf`, un assembly .NET commence par `MZ` (DOS) puis `PE\0\0` et expose sa structure via des outils comme `monodis`, `ildasm` ou `dotnet-dump`. Le principe est le même — un header qui décrit le contenu — mais le format conteneur diffère.

## Vue d'ensemble de l'architecture

Voici comment s'emboîtent les différentes couches d'un assembly .NET, de l'extérieur vers l'intérieur :

```
┌─────────────────────────────────────────────────────┐
│                    Fichier PE (.dll / .exe)         │
│                                                     │
│  ┌──────────────┐                                   │
│  │  DOS Header  │  Magic "MZ" — héritage DOS        │
│  │  + DOS Stub  │  "This program cannot be run..."  │
│  └──────────────┘                                   │
│  ┌──────────────┐                                   │
│  │  PE Headers  │  Signature, COFF Header,          │
│  │              │  Optional Header (32 ou 64 bits)  │
│  └──────────────┘                                   │
│  ┌───────────────────────────────────────────────┐  │
│  │   Sections                                       │
│  │                                                  │
│  │  ┌────────────────────────────────────────┐      │
│  │  │ .text                                  │      │
│  │  │                                        │      │
│  │  │  ┌─────────────┐  ┌─────────────────┐  │      │
│  │  │  │ CLR Header  │  │   Bytecode CIL  │  │      │
│  │  │  │ (72 octets) │  │  (method bodies)│  │      │
│  │  │  └─────────────┘  └─────────────────┘  │      │
│  │  │                                        │      │
│  │  │  ┌──────────────────────────────────┐  │      │
│  │  │  │         METADATA                 │  │      │
│  │  │  │  ┌────────┐ ┌─────────────────┐  │  │      │
│  │  │  │  │ Tables │ │     Heaps       │  │  │      │
│  │  │  │  │  (#~)  │ │ #Strings  #US   │  │  │      │
│  │  │  │  │        │ │ #Blob    #GUID  │  │  │      │
│  │  │  │  └────────┘ └─────────────────┘  │  │      │
│  │  │  └──────────────────────────────────┘  │      │
│  │  └────────────────────────────────────────┘      │
│  │                                                  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │  │ .rsrc    │  │ .reloc   │  │ (autres) │        │
│  │  └──────────┘  └──────────┘  └──────────┘        │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

La section `.text` concentre l'essentiel de ce qui intéresse le reverser : le CLR Header, le bytecode CIL des méthodes, et le bloc de metadata. Examinons chaque composant.

## Le DOS Header et le PE Header : l'enveloppe extérieure

Ces deux headers sont identiques à ceux d'un exécutable Windows natif. Ils ne contiennent rien de spécifique à .NET, mais leur présence est obligatoire car le format PE l'impose.

**Le DOS Header** occupe les 64 premiers octets du fichier. On y retrouve le magic `MZ` (octets `4D 5A`) à l'offset 0, et le champ `e_lfanew` à l'offset `0x3C` qui pointe vers le PE Header. Entre les deux, un DOS Stub affiche le message classique « This program cannot be run in DOS mode » si quelqu'un tentait d'exécuter le fichier sous DOS. En RE .NET, ce header ne présente aucun intérêt analytique — on le traverse pour atteindre le PE Header.

**Le PE Header** commence par la signature `PE\0\0` (octets `50 45 00 00`), suivie du COFF Header (20 octets) et de l'Optional Header. C'est dans l'Optional Header que se trouvent deux informations clés pour le reverser .NET :

- Le **Data Directory** à l'index 14 (CLI Header, aussi appelé « COM Descriptor ») : il donne le RVA (Relative Virtual Address) et la taille du CLR Header. C'est le point d'entrée dans le monde .NET au sein du PE. Si ce Data Directory est vide ou absent, le fichier PE n'est pas un assembly .NET — c'est un exécutable natif.  
- L'**Entry Point** dans l'Optional Header : pour un assembly .NET classique, il pointe vers un stub minuscule (`_CorExeMain` ou `_CorDllMain`) dont le seul rôle est de passer la main au CLR. Ce n'est pas l'équivalent du `main()` de votre programme C.

> 💡 **Parallèle ELF** — Le champ `e_entry` de l'ELF Header (vu en section 2.4) pointe vers le point d'entrée du processus, typiquement `_start`, qui appelle `__libc_start_main` puis `main()`. En .NET, le PE Entry Point pointe vers un stub CLR, qui initialise le runtime, puis invoque la méthode `Main()` de votre programme C# via les metadata. La mécanique est analogue — un bootstrap avant le code utilisateur — mais le niveau d'indirection est plus élevé.

## Le CLR Header : la porte d'entrée dans le monde managé

Le CLR Header (aussi nommé CLI Header ou COR20 Header) est une structure de 72 octets, définie par le standard ECMA-335. C'est la première structure spécifiquement .NET que l'on rencontre en parcourant le fichier. Elle contient les pointeurs (RVA + taille) vers toutes les zones critiques de l'assembly.

Voici ses champs principaux, avec leur rôle pour le reverser :

| Champ | Taille | Rôle pour le RE |  
|-------|--------|-----------------|  
| `cb` | 4 octets | Taille de la structure (toujours 72) |  
| `MajorRuntimeVersion` | 2 octets | Version minimale du CLR requise |  
| `MinorRuntimeVersion` | 2 octets | (suite) |  
| `MetaData` (RVA + Size) | 8 octets | **Pointe vers le bloc de metadata** — le champ le plus important |  
| `Flags` | 4 octets | Indicateurs : `ILONLY` (pas de code natif mixte), `32BITREQUIRED`, `STRONGNAMESIGNED`… |  
| `EntryPointToken` | 4 octets | Token metadata de la méthode `Main()` (ou du module natif si mixte) |  
| `Resources` (RVA + Size) | 8 octets | Ressources managées embarquées |  
| `StrongNameSignature` | 8 octets | Signature cryptographique de l'assembly |  
| `VTableFixups` | 8 octets | Table de fixups pour les vtables (assemblies mixtes C++/CLI) |

Le champ `Flags` est un indicateur rapide lors du triage. Le flag `COMIMAGE_FLAGS_ILONLY` (valeur `0x01`) signifie que l'assembly ne contient que du CIL pur, sans code natif embarqué. Si ce flag est absent, vous avez potentiellement affaire à un assembly **mixte** (C++/CLI) qui contient à la fois du CIL et du code machine x86-64 — un cas hybride où les techniques natives des parties I à IV et les techniques .NET du chapitre 31 doivent être combinées.

Le champ `EntryPointToken` est un **metadata token** — un entier de 32 bits dont l'octet de poids fort identifie la table de metadata (ici `0x06` pour la table `MethodDef`) et les trois octets suivants donnent l'index dans cette table. Par exemple, un token `0x06000001` désigne la première méthode dans la table `MethodDef`. En résolvant ce token, vous trouvez directement la méthode `Main()` du programme.

## Le bloc de metadata : le cœur de l'assembly

C'est ici que réside la différence fondamentale avec un ELF natif, et c'est la raison pour laquelle le RE .NET est structurellement plus riche en informations que le RE natif.

Le bloc de metadata commence par un **Metadata Root Header** identifiable par son magic `BSJB` (octets `42 53 4A 42`) — un acronyme historique lié aux initiales des développeurs du CLR. Ce header indique la version du format et, surtout, liste les **streams** (flux) qui composent les metadata.

### Les cinq streams de metadata

Un assembly .NET standard contient cinq streams, chacun stockant un type de données différent :

**`#~` (ou `#-`)** — Le stream de **tables de metadata**. C'est la structure la plus importante. Il contient un ensemble de tables relationnelles (similaires à une base de données) qui décrivent tous les types, méthodes, champs, paramètres, attributs et références de l'assembly. Le format `#~` est optimisé (tables compressées) ; `#-` est le format non optimisé, plus rare. Nous détaillons les tables principales ci-après.

**`#Strings`** — Le **heap de chaînes d'identifiants**. Il contient les noms des types, méthodes, champs, namespaces, paramètres — tout ce qui constitue l'API et la structure du code. Ces chaînes sont encodées en UTF-8 et terminées par un octet nul. Les tables du stream `#~` référencent les chaînes par leur offset dans ce heap.

> 💡 **Parallèle ELF** — Le heap `#Strings` est l'équivalent fonctionnel de la section `.strtab` (string table) d'un ELF, qui contient les noms des symboles. La différence cruciale : en ELF, `.strtab` disparaît avec `strip -s` ; en .NET, `#Strings` est **toujours présent** car il est nécessaire au fonctionnement du CLR.

**`#US` (User Strings)** — Le **heap de chaînes utilisateur**. Il contient les littéraux de chaînes définis dans le code source C# (les `"Hello World"`, les messages d'erreur, les clés de configuration…). Encodées en UTF-16LE, préfixées par leur longueur, ces chaînes sont distinctes des identifiants du heap `#Strings`. Pour le reverser, `#US` est une mine d'or : c'est l'équivalent de la section `.rodata` d'un ELF, mais avec un lien direct vers la méthode qui utilise chaque chaîne (via l'instruction CIL `ldstr` et son token).

**`#Blob`** — Le **heap de données binaires**. Il stocke les signatures de méthodes encodées, les valeurs par défaut des champs, les marshalling specs, et d'autres données binaires structurées. En analyse courante, on le consulte rarement directement — les outils de décompilation le décodent automatiquement.

**`#GUID`** — Le **heap d'identifiants GUID**. Chaque assembly possède un MVID (Module Version Identifier), un GUID unique généré à chaque compilation. Ce GUID permet de distinguer deux builds du même code source. Pour le reverser, le MVID est utile dans le cadre de diffing (chapitre 10) : deux assemblies avec le même MVID proviennent de la même compilation exacte.

### Les tables de metadata principales

Le stream `#~` organise ses données en **tables numérotées** (de `0x00` à `0x2C`). Chaque table contient des **lignes** (rows) dont les colonnes sont des index vers d'autres tables ou vers les heaps. Voici les tables les plus utiles au reverser :

| Table | N° | Contenu | Équivalent natif approximatif |  
|-------|----|---------|-------------------------------|  
| `Module` | `0x00` | Identité du module (nom, MVID) | Nom du fichier ELF |  
| `TypeRef` | `0x01` | Références vers des types **externes** (autres assemblies) | Symboles dynamiques importés (`.dynsym`) |  
| `TypeDef` | `0x02` | Définitions de tous les **types** de l'assembly (classes, structs, enums, interfaces) | Pas d'équivalent direct en ELF strippé |  
| `FieldDef` | `0x04` | Champs de chaque type (attributs de classe) | Données dans `.data` / `.bss` (sans noms) |  
| `MethodDef` | `0x06` | Définitions de toutes les **méthodes** (nom, signature, RVA du corps CIL) | Entrées dans `.symtab` (si non strippé) |  
| `Param` | `0x08` | Paramètres de chaque méthode (nom, position, flags) | Informations DWARF (si compilé avec `-g`) |  
| `MemberRef` | `0x0A` | Références vers des méthodes/champs **externes** | Entrées PLT/GOT pour les fonctions importées |  
| `CustomAttribute` | `0x0C` | Attributs .NET appliqués aux types/méthodes | Pas d'équivalent |  
| `Assembly` | `0x20` | Identité de l'assembly (nom, version, culture, clé publique) | Champ `SONAME` d'un `.so` |  
| `AssemblyRef` | `0x23` | Références vers les assemblies **dépendantes** | Table `DT_NEEDED` en ELF |

La table `TypeDef` est le point de départ de toute analyse structurelle. Chaque ligne décrit un type avec son nom, son namespace, ses flags de visibilité, et des index vers les tables `FieldDef` et `MethodDef` pour lister ses membres. C'est grâce à cette table que les décompilateurs peuvent reconstruire instantanément la hiérarchie de classes — un travail qui, sur un binaire C++ natif, nécessite la reconstruction manuelle des vtables et du RTTI (sections 17.2 et 17.3).

La table `MethodDef` est tout aussi centrale. Pour chaque méthode, elle donne le **RVA** (Relative Virtual Address) de son corps CIL dans la section `.text`. Ce RVA pointe vers un **method header** suivi des instructions CIL de la méthode. Le method header existe en deux variantes : un format **Tiny** (1 octet, pour les petites méthodes sans variables locales ni exceptions) et un format **Fat** (12 octets, pour les méthodes avec variables locales, try/catch, ou stack size supérieure à 8).

## La section .text : là où vit le code

Dans un ELF natif, la section `.text` contient le code machine exécutable — des instructions x86-64 que vous désassemblez avec `objdump` ou Ghidra. Dans un assembly .NET, la section `.text` contient un mélange de :

- Le **CLR Header** (72 octets).  
- Les **corps des méthodes CIL** (method bodies), chacun précédé de son method header (Tiny ou Fat).  
- Le **bloc de metadata** complet (root header + streams).  
- Éventuellement, des **ressources managées** et la **strong name signature**.

Le point important est que le bytecode CIL n'est pas organisé en une zone contiguë unique comme le serait le `.text` d'un ELF natif. Les corps de méthodes sont dispersés dans la section, et c'est la table `MethodDef` dans les metadata qui permet de les localiser un par un grâce à leurs RVA. Sans les metadata, le bytecode CIL est un flux d'octets sans structure — les metadata sont la carte qui donne sens au territoire.

## Les autres sections

Au-delà de `.text`, un assembly .NET contient généralement quelques sections supplémentaires :

**`.rsrc`** — Les **ressources Win32** (icônes, manifestes, informations de version). Cette section n'a rien de spécifique à .NET ; elle existe aussi dans les PE natifs. À ne pas confondre avec les ressources managées .NET (stockées dans `.text` et pointées par le CLR Header).

**`.reloc`** — Les **relocations** nécessaires au chargement du PE à une adresse de base différente de celle prévue. Sur un assembly .NET pur (flag `ILONLY`), cette section est minimaliste car le CIL est indépendant des adresses — seul le stub d'entrée natif nécessite des relocations.

Certains assemblies peuvent contenir une section **`.sdata`** ou **`.data`** si le code utilise des variables globales initialisées ou du code natif mixte (C++/CLI). C'est rare dans un assembly C# pur.

> 💡 **Parallèle ELF** — En comparaison, un binaire ELF typique contient bien plus de sections exploitables : `.text` (code), `.rodata` (constantes), `.data` (globales initialisées), `.bss` (globales non initialisées), `.plt`/`.got` (résolution dynamique), `.init`/`.fini` (constructeurs/destructeurs), `.eh_frame` (exceptions). La relative simplicité de la structure sectionnelle d'un PE .NET reflète le fait que la complexité est déportée dans les metadata plutôt que dans l'organisation mémoire.

## Le manifeste : l'identité de l'assembly

Chaque assembly .NET contient un **manifeste** — un sous-ensemble des metadata qui décrit l'identité publique de l'assembly : son nom, sa version (format `Major.Minor.Build.Revision`), sa culture (langue), et éventuellement sa clé publique de signature forte (strong name).

Le manifeste liste aussi les **références vers d'autres assemblies** (table `AssemblyRef`) dont dépend le code. C'est l'équivalent fonctionnel de la commande `ldd` sur un ELF (section 5.4) : il vous dit quelles bibliothèques externes sont nécessaires à l'exécution.

```
$ monodis --assemblyref MyApp.dll
AssemblyRef Table
  1: Version=8.0.0.0  Name=System.Runtime
  2: Version=8.0.0.0  Name=System.Console
  3: Version=8.0.0.0  Name=System.Collections
```

Comparez avec la sortie de `ldd` sur un binaire natif :

```
$ ldd my_app
    linux-vdso.so.1
    libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
    /lib64/ld-linux-x86-64.so.2
```

La même information — quelles dépendances externes sont requises — est exprimée dans les deux mondes, mais avec une granularité différente. Les dépendances .NET sont des assemblies nommés et versionnés ; les dépendances ELF sont des fichiers `.so` résolus par le loader dynamique `ld.so` (section 2.7).

## Récapitulatif : lire un assembly .NET comme on lit un ELF

Pour ancrer ces concepts, voici la correspondance entre les étapes de triage d'un ELF (workflow du chapitre 5) et leur équivalent sur un assembly .NET :

| Étape de triage ELF | Commande | Équivalent .NET | Commande / outil |  
|---------------------|----------|-----------------|------------------|  
| Identifier le type de fichier | `file binary` | Identifier un assembly .NET | `file MyApp.dll` → chercher `Mono/.Net assembly` |  
| Lister les sections | `readelf -S binary` | Lister les sections PE | `objdump -h MyApp.dll` ou ImHex |  
| Extraire les chaînes | `strings binary` | Extraire les chaînes | `strings MyApp.dll` (capture `#Strings` et `#US`) |  
| Lister les symboles | `nm binary` | Lister les types et méthodes | `monodis --typedef MyApp.dll` |  
| Lister les dépendances | `ldd binary` | Lister les assembly references | `monodis --assemblyref MyApp.dll` |  
| Inspecter les headers | `readelf -h binary` | Inspecter le CLR Header | ImHex avec un pattern `.hexpat` ou `monodis` |  
| Désassembler | `objdump -d binary` | Désassembler le CIL | `monodis MyApp.dll` ou `ildasm` |

Cette correspondance n'est pas parfaite — les abstractions sont différentes — mais elle vous donne un cadre de travail immédiat en réutilisant les réflexes acquis dans les parties précédentes.

---

> 📖 **À retenir** — Un assembly .NET est un fichier PE dont la section `.text` embarque à la fois le bytecode CIL et un bloc de metadata richement structuré (tables + heaps). Ce sont les metadata — et non le bytecode — qui font la spécificité du format : elles transportent l'intégralité de la structure logique du programme (types, méthodes, hiérarchie, signatures, chaînes), rendant la décompilation quasi transparente en l'absence d'obfuscation. Le magic `BSJB` dans le bloc de metadata et le Data Directory 14 dans le PE Optional Header sont les deux marqueurs qui confirment la nature .NET d'un fichier.

---


⏭️ [Obfuscateurs courants : ConfuserEx, Dotfuscator, SmartAssembly](/30-introduction-re-dotnet/03-obfuscateurs-courants.md)
