🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.3 — Formats binaires : ELF (Linux), PE (Windows via MinGW), Mach-O (macOS)

> 🎯 **Objectif de cette section** : Comprendre ce qu'est un format binaire exécutable, connaître les trois formats majeurs des systèmes desktop, et savoir pourquoi cette formation se concentre sur ELF tout en étant capable de reconnaître les autres.

---

## Qu'est-ce qu'un format binaire ?

Le code machine produit par le compilateur et l'assembleur ne peut pas être livré « brut » au système d'exploitation. Le noyau a besoin de métadonnées pour savoir *comment* charger ce code en mémoire : où commence le code exécutable, où se trouvent les données, quelles bibliothèques sont nécessaires, quel est le point d'entrée du programme, quelles permissions appliquer à chaque zone mémoire, etc.

Le **format binaire** est le conteneur qui encapsule le code machine et toutes ces métadonnées dans une structure standardisée. C'est l'équivalent d'un format de fichier (comme PNG pour les images ou ZIP pour les archives) mais pour les programmes exécutables.

Chaque famille de systèmes d'exploitation a adopté son propre format :

| Format | Systèmes | Magic bytes | Spécification |  
|---|---|---|---|  
| **ELF** (Executable and Linkable Format) | Linux, BSD, Solaris, Android, PlayStation, Nintendo Switch… | `7f 45 4c 46` (`\x7fELF`) | System V ABI / extensions Linux |  
| **PE** (Portable Executable) | Windows (toutes versions) | `4d 5a` (`MZ`) | Microsoft PE/COFF specification |  
| **Mach-O** (Mach Object) | macOS, iOS, watchOS, tvOS | `fe ed fa ce` / `fe ed fa cf` / `ca fe ba be` (universal) | Apple Mach-O Reference |

> 💡 **Magic bytes** : les premiers octets d'un fichier binaire constituent sa « signature magique ». C'est la première chose que le noyau vérifie avant de tenter de charger un exécutable. C'est aussi la première chose que l'outil `file` examine (Chapitre 5, section 5.1).

## ELF — Le format de cette formation

### Origine et omniprésence

ELF a été conçu au début des années 1990 dans le cadre de la spécification System V ABI et a progressivement remplacé les formats plus anciens (a.out, COFF). Aujourd'hui, il est utilisé par la quasi-totalité des systèmes Unix-like et bien au-delà : Linux, les variantes BSD (FreeBSD, OpenBSD, NetBSD), Solaris/illumos, Android (les APK contiennent des `.so` ELF pour le code natif), les consoles de jeu (PlayStation, Nintendo Switch), et de nombreux systèmes embarqués.

### Structure générale

Un fichier ELF offre **deux vues complémentaires** du même contenu binaire, conçues pour deux consommateurs différents :

**La vue en sections** (*section header table*) est destinée au **linker** et aux outils d'analyse (objdump, readelf, Ghidra…). Elle découpe le fichier en sections nommées (`.text`, `.data`, `.rodata`, `.bss`, `.plt`, `.got`, etc.), chacune ayant un type, des flags et un rôle précis. C'est cette vue que nous avons explorée en section 2.2 et que nous détaillerons en section 2.4.

**La vue en segments** (*program header table*) est destinée au **loader** (`ld.so`) qui charge le programme en mémoire pour l'exécuter. Elle regroupe les sections en segments contigus auxquels sont associées des permissions mémoire (lecture, écriture, exécution). C'est cette vue que nous approfondirons en sections 2.7 et 2.8.

```
                        Fichier ELF
    ┌──────────────────────────────────────────┐
    │              ELF Header                  │ ← Magic, architecture, point d'entrée
    ├──────────────────────────────────────────┤
    │         Program Header Table             │ ← Vue segments (pour le loader)
    │  ┌────────────────────────────────────┐  │
    │  │  LOAD segment (R-X) ───────────────┼──┼── .text, .rodata, .plt
    │  │  LOAD segment (RW-) ───────────────┼──┼── .data, .bss, .got
    │  │  DYNAMIC segment ──────────────────┼──┼── .dynamic
    │  │  INTERP segment ───────────────────┼──┼── .interp
    │  │  ...                               │  │
    │  └────────────────────────────────────┘  │
    ├──────────────────────────────────────────┤
    │         Section Header Table             │ ← Vue sections (pour le linker / RE)
    │  ┌────────────────────────────────────┐  │
    │  │  .text   .rodata   .data   .bss    │  │
    │  │  .plt    .got      .dynamic        │  │
    │  │  .symtab .strtab   .shstrtab       │  │
    │  │  .eh_frame   .comment   ...        │  │
    │  └────────────────────────────────────┘  │
    └──────────────────────────────────────────┘
```

La dualité sections/segments est une particularité importante d'ELF. Un exécutable strippé peut avoir sa *section header table* supprimée (c'est rare mais possible, notamment dans les malwares) tout en restant parfaitement exécutable, car le loader n'utilise que la *program header table*. Pour le reverse engineer, cela signifie que la vue en sections est un **luxe** — précieux quand il est disponible, mais pas garanti.

### Types de fichiers ELF

L'en-tête ELF contient un champ `e_type` qui indique la nature du fichier :

| Type | Valeur | Description | Exemple |  
|---|---|---|---|  
| `ET_REL` | 1 | Fichier objet relocatable | `hello.o` |  
| `ET_EXEC` | 2 | Exécutable à adresse fixe | Binaire compilé sans `-pie` |  
| `ET_DYN` | 3 | Objet partagé / PIE | `libc.so.6`, ou exécutable compilé avec `-pie` |  
| `ET_CORE` | 4 | Fichier core dump | Dump mémoire après un crash |

Un point souvent source de confusion : depuis plusieurs années, GCC produit par défaut des exécutables **PIE** (Position-Independent Executable), qui sont techniquement de type `ET_DYN` — le même type que les bibliothèques partagées `.so`. Cela ne signifie pas que votre programme est une bibliothèque ; c'est simplement que le format est le même pour permettre le chargement à une adresse aléatoire (ASLR, section 2.8). La commande `file` distingue les deux en affichant « shared object » pour une `.so` et « pie executable » ou « Position-Independent Executable » pour un exécutable PIE.

### Pourquoi cette formation se concentre sur ELF

Le choix d'ELF comme format central de cette formation repose sur plusieurs raisons convergentes :

- **Ouverture de la spécification.** La spécification ELF est publique, gratuite et bien documentée. Pas de NDA, pas de documentation partielle.  
- **Outillage GNU natif.** Les outils de la chaîne GNU (`readelf`, `objdump`, `nm`, `ld`) sont conçus pour ELF. Ils sont gratuits, open source, et disponibles partout.  
- **Écosystème de RE riche.** Ghidra, Radare2, IDA Free, GDB, Frida, angr, AFL++ — tous ces outils supportent nativement ELF et sont souvent développés en priorité pour Linux.  
- **Cohérence pédagogique.** Travailler sur un seul format permet de creuser en profondeur plutôt que de survoler trois formats différents. Les concepts (sections, segments, symboles, relocations, liaison dynamique) se transposent ensuite naturellement aux autres formats.  
- **Pertinence pratique.** L'immense majorité des serveurs, des conteneurs Docker, des appareils Android, des objets connectés et des infrastructures cloud exécutent du code ELF.

## PE — Le format Windows

### Contexte

Le format PE (Portable Executable) est le format natif de Windows pour les exécutables (`.exe`), les bibliothèques dynamiques (`.dll`), les drivers (`.sys`) et les fichiers objet (`.obj`). Il dérive du format COFF (Common Object File Format) et a été introduit avec Windows NT en 1993.

La chaîne GNU peut produire des binaires PE via le cross-compilateur **MinGW** (Minimalist GNU for Windows) :

```bash
# Cross-compilation depuis Linux vers Windows
x86_64-w64-mingw32-gcc hello.c -o hello.exe
```

Le binaire produit est un PE valide, exécutable sous Windows, bien qu'il ait été compilé sur Linux avec GCC.

### Structure générale

Un fichier PE commence toujours par un **stub DOS** hérité de MS-DOS (le fameux en-tête `MZ`), suivi d'un en-tête PE proprement dit. Cette structure historique en couches est caractéristique :

```
    ┌──────────────────────────┐
    │   DOS Header (MZ)        │ ← Héritage MS-DOS, contient un offset vers le PE header
    │   DOS Stub               │ ← Mini-programme DOS ("This program cannot be run in DOS mode")
    ├──────────────────────────┤
    │   PE Signature ("PE\0\0")│
    │   COFF Header            │ ← Architecture, nombre de sections, timestamp
    │   Optional Header        │ ← Point d'entrée, taille d'image, base d'adresse, sous-système
    │     Data Directories     │ ← Import Table, Export Table, Resource Table, Relocation Table…
    ├──────────────────────────┤
    │   Section Table          │
    │   .text                  │ ← Code exécutable
    │   .rdata                 │ ← Données en lecture seule (équivalent de .rodata)
    │   .data                  │ ← Données initialisées
    │   .bss                   │ ← Données non initialisées
    │   .idata                 │ ← Import Address Table (équivalent fonctionnel de PLT/GOT)
    │   .edata                 │ ← Export Table (pour les DLL)
    │   .rsrc                  │ ← Ressources (icônes, menus, dialogues, version info)
    │   .reloc                 │ ← Table de relocations (pour l'ASLR)
    └──────────────────────────┘
```

### Différences clés avec ELF (perspective RE)

| Aspect | ELF | PE |  
|---|---|---|  
| Magic bytes | `\x7fELF` | `MZ` (DOS) puis `PE\0\0` |  
| Liaison dynamique | PLT/GOT + `ld.so` | Import Address Table (IAT) + `ntdll.dll` / `kernel32.dll` |  
| Bibliothèques partagées | `.so` (ELF partagé) | `.dll` (PE avec exports) |  
| Résolution de symboles | Par nom via `.dynsym` | Par nom ou par **ordinal** (numéro) |  
| Ressources embarquées | Pas de mécanisme standard | Section `.rsrc` (icônes, strings, dialogues…) |  
| Position-independent code | PIE / `-fPIC` | ASLR via `.reloc` + flag DLL characteristics |  
| Informations de débogage | DWARF (embarqué ou séparé) | PDB (fichier séparé `.pdb`) |  
| Deux vues (sections/segments) | Oui (section headers + program headers) | Non — une seule table de sections |

La différence la plus notable pour le RE quotidien est la gestion des imports. En ELF, les appels aux bibliothèques partagées passent par le couple PLT/GOT (section 2.9). En PE, ils passent par l'**Import Address Table** (IAT) : le loader Windows remplit un tableau de pointeurs de fonctions au chargement, et le code appelle ces fonctions via des indirections `call [IAT_entry]`. Le principe est similaire (une table d'adresses remplie au runtime) mais la mécanique et les structures de données diffèrent.

Autre différence importante : les **informations de débogage** PE ne sont pas embarquées dans le binaire au format DWARF. Windows utilise des fichiers **PDB** (Program Database) séparés. En RE sur un binaire Windows, avoir le `.pdb` correspondant est un avantage considérable — c'est l'équivalent d'avoir un binaire ELF compilé avec `-g`. Sans PDB, vous êtes dans la même situation qu'un ELF strippé.

### Outils de RE pour PE

Si vous devez un jour analyser un binaire PE, les mêmes outils multi-formats fonctionnent : Ghidra, IDA et Radare2 supportent nativement PE. Des outils spécifiques au monde Windows s'y ajoutent : PE-bear, CFF Explorer, x64dbg (débogueur), API Monitor, et Process Monitor (Sysinternals). Le framework `pefile` en Python joue un rôle similaire à `pyelftools` pour l'analyse programmatique.

## Mach-O — Le format Apple

### Contexte

Mach-O (Mach Object) est le format natif de l'écosystème Apple : macOS, iOS, watchOS, tvOS et visionOS. Il tire son nom du micro-noyau Mach qui constitue la base de XNU, le noyau de macOS. GCC a longtemps été le compilateur par défaut sous macOS, mais Apple est passé à **Clang/LLVM** depuis 2012 (Xcode 5). La commande `gcc` existe toujours sur macOS, mais c'est un alias vers Clang.

### Structure générale

Mach-O adopte une architecture fondée sur des **load commands** — une séquence d'instructions de chargement que le loader du noyau XNU (`dyld`) exécute séquentiellement :

```
    ┌───────────────────────────┐
    │   Mach-O Header           │ ← Magic, CPU type, nombre de load commands
    ├───────────────────────────┤
    │   Load Commands           │
    │   LC_SEGMENT_64 __TEXT    │ ← Segment code (contient les sections __text, __stubs, __cstring…)
    │   LC_SEGMENT_64 __DATA    │ ← Segment données (__data, __bss, __la_symbol_ptr…)
    │   LC_SEGMENT_64 __LINKEDIT│ ← Métadonnées de liaison (symboles, relocations, signatures)
    │   LC_DYLD_INFO_ONLY       │ ← Informations pour le loader dynamique dyld
    │   LC_SYMTAB               │ ← Table de symboles
    │   LC_LOAD_DYLIB           │ ← Bibliothèque dynamique requise (une par .dylib)
    │   LC_CODE_SIGNATURE       │ ← Signature du code (obligatoire sur iOS, courant sur macOS)
    │   ...                     │
    ├───────────────────────────┤
    │   Section Data            │
    │   __TEXT,__text           │ ← Code exécutable
    │   __TEXT,__cstring        │ ← Chaînes C (équivalent de .rodata)
    │   __TEXT,__stubs          │ ← Stubs d'appels dynamiques (équivalent de .plt)
    │   __DATA,__la_symbol_ptr  │ ← Pointeurs lazy (équivalent de .got.plt)
    │   __DATA,__data           │ ← Données initialisées
    │   ...                     │
    └───────────────────────────┘
```

### Particularités notables pour le RE

**La convention de nommage** utilise un double underscore : les segments sont en majuscules (`__TEXT`, `__DATA`), les sections en minuscules (`__text`, `__cstring`). Le couple `segment,section` est la référence canonique : `__TEXT,__text` désigne la section `__text` dans le segment `__TEXT`.

**Les Universal Binaries** (ou *fat binaries*) sont un mécanisme propre à Apple permettant d'empaqueter dans un seul fichier des binaires pour plusieurs architectures (x86-64 + ARM64, par exemple). Le magic byte `ca fe ba be` identifie un fat binary, qui contient un en-tête listant les architectures disponibles suivi des binaires Mach-O individuels concaténés. Avec la transition d'Apple vers Apple Silicon (ARM64), les universal binaries sont redevenus courants.

**La signature de code** (*code signing*) est omniprésente dans l'écosystème Apple. Sur iOS, chaque binaire doit être signé. Sur macOS, la signature est de plus en plus requise (Gatekeeper, notarization). Pour le RE, cela signifie qu'un binaire Mach-O modifié (patché) ne passera plus la vérification de signature — un obstacle supplémentaire par rapport à ELF, où la signature de code n'est pas un mécanisme standard du format.

**Le loader `dyld`** (dynamic link editor) est l'équivalent Apple de `ld.so` sous Linux. Son fonctionnement est similaire dans le principe (résolution de symboles, liaison dynamique au chargement) mais les mécanismes internes diffèrent. Apple publie le code source de `dyld`, ce qui est une ressource précieuse pour le RE sur macOS/iOS.

### Outils de RE pour Mach-O

Ghidra, IDA et Radare2 supportent nativement Mach-O. Des outils spécifiques incluent `otool` (équivalent macOS de `objdump`/`readelf`), `install_name_tool`, `codesign`, et le débogueur `lldb` (le pendant LLVM de GDB). Hopper Disassembler est un outil commercial populaire spécialisé dans le RE macOS/iOS. La bibliothèque Python `lief` supporte les trois formats (ELF, PE, Mach-O) et permet l'analyse programmatique unifiée.

## Comparatif synthétique

| Caractéristique | ELF | PE | Mach-O |  
|---|---|---|---|  
| **Systèmes** | Linux, BSD, Android… | Windows | macOS, iOS |  
| **Magic bytes** | `7f 45 4c 46` | `4d 5a` | `fe ed fa ce/cf` ou `ca fe ba be` |  
| **Extensions courantes** | (aucune), `.so`, `.o` | `.exe`, `.dll`, `.sys`, `.obj` | (aucune), `.dylib`, `.o`, `.app` |  
| **Code exécutable** | `.text` | `.text` | `__TEXT,__text` |  
| **Données en lecture seule** | `.rodata` | `.rdata` | `__TEXT,__cstring` |  
| **Données initialisées** | `.data` | `.data` | `__DATA,__data` |  
| **Données non initialisées** | `.bss` | `.bss` | `__DATA,__bss` |  
| **Imports dynamiques** | PLT + GOT | IAT (Import Address Table) | `__stubs` + `__la_symbol_ptr` |  
| **Infos de débogage** | DWARF (embarqué) | PDB (fichier séparé) | DWARF (embarqué ou dSYM séparé) |  
| **Loader** | `ld.so` (`ld-linux-x86-64.so.2`) | `ntdll.dll` | `dyld` |  
| **Multi-architecture** | Non (un fichier = une arch) | Non | Oui (Universal / fat binary) |  
| **Signature de code** | Optionnelle (rare) | Authenticode (optionnel) | Obligatoire (iOS), courant (macOS) |  
| **Outil d'inspection natif** | `readelf`, `objdump` | `dumpbin` (MSVC) | `otool`, `pagestuff` |  
| **Bibliothèque Python** | `pyelftools`, `lief` | `pefile`, `lief` | `macholib`, `lief` |

## Reconnaître un format en un coup d'œil

En situation de RE, la première étape face à un binaire inconnu est d'identifier son format. Trois méthodes complémentaires :

**La commande `file`** analyse les magic bytes et les en-têtes pour identifier le format, l'architecture et d'autres caractéristiques :

```bash
file hello
# hello: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),
#        dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,
#        for GNU/Linux 3.2.0, not stripped

file hello.exe
# hello.exe: PE32+ executable (console) x86-64, for MS Windows, 6 sections

file hello_macos
# hello_macos: Mach-O 64-bit x86_64 executable
```

**Les magic bytes** visibles avec `xxd` ou `hexdump` :

```bash
xxd -l 16 hello
# 00000000: 7f45 4c46 0201 0100 0000 0000 0000 0000  .ELF............

xxd -l 16 hello.exe
# 00000000: 4d5a 9000 0300 0000 0400 0000 ffff 0000  MZ..............

xxd -l 16 hello_macos
# 00000000: cffa edfe 0c00 0001 0000 0000 0200 0000  ................
```

**ImHex** (Chapitre 6) permet de visualiser ces en-têtes de manière structurée grâce à ses patterns `.hexpat` — nous y reviendrons.

## Ce que cette section change pour la suite

Bien que cette formation se concentre sur ELF, les concepts fondamentaux sont transversaux. Une fois que vous maîtrisez la distinction code/données/métadonnées, le mécanisme d'imports dynamiques et le rôle des informations de débogage sur ELF, vous pouvez aborder un binaire PE ou Mach-O en cherchant les équivalents dans le format cible. Les outils majeurs (Ghidra, IDA, Radare2) abstraient une grande partie de ces différences de format et vous présentent une vue unifiée.

---

> 📖 **Maintenant que nous savons dans quel conteneur le code machine est livré**, plongeons dans le détail des sections ELF — les compartiments qui organisent le contenu du binaire et que vous manipulerez quotidiennement en analyse statique.  
>  
> → 2.4 — Sections ELF clés : `.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`, `.init`, `.fini`

⏭️ [Sections ELF clés : `.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`, `.init`, `.fini`](/02-chaine-compilation-gnu/04-sections-elf.md)
