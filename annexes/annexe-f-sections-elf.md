🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe F — Table des sections ELF et leurs rôles

> 📎 **Fiche de référence** — Cette annexe dresse la liste complète des sections que vous pouvez rencontrer dans un binaire ELF x86-64 produit par GCC/G++. Pour chaque section, elle indique son contenu, ses flags, son rôle dans le processus d'exécution et son intérêt spécifique pour le reverse engineering. Elle complète directement le chapitre 2 (section 2.4) et sert de référence permanente pour toute l'analyse statique.

---

## Rappel : sections vs segments

Avant de plonger dans la table, il est essentiel de clarifier la distinction entre **sections** et **segments**, deux concepts souvent confondus dans le format ELF.

Les **sections** sont la vue du *linker* et du *reverse engineer*. Elles découpent le binaire en unités logiques nommées (`.text` pour le code, `.data` pour les données initialisées, etc.). Chaque section a un nom, un type, des flags et une taille. C'est la granularité à laquelle travaillent `readelf -S`, `objdump`, Ghidra et tous les outils d'analyse statique. Un binaire strippé peut avoir ses sections supprimées ou renommées, mais cela ne l'empêche pas de fonctionner.

Les **segments** (ou *program headers*) sont la vue du *loader* du noyau. Ils regroupent une ou plusieurs sections en zones de mémoire contiguës avec des permissions communes (lecture, écriture, exécution). C'est la granularité à laquelle travaille le noyau Linux quand il mappe le binaire en mémoire via `mmap`. Un segment de type `PT_LOAD` avec les flags `R+X` contiendra typiquement `.text`, `.plt`, `.rodata` et d'autres sections en lecture seule et exécutables.

En résumé : les sections sont pour l'analyse, les segments sont pour l'exécution. Un binaire peut fonctionner sans table de sections (les packers la suppriment souvent) mais pas sans table de segments.

```
            Vue du linker / RE                Vue du loader
         ┌──────────────────────┐        ┌──────────────────────┐
         │  Section .text       │        │                      │
         │  Section .rodata     │───────▶│  Segment LOAD (R+X)  │
         │  Section .plt        │        │                      │
         ├──────────────────────┤        ├──────────────────────┤
         │  Section .data       │        │                      │
         │  Section .bss        │───────▶│  Segment LOAD (R+W)  │
         │  Section .got        │        │                      │
         └──────────────────────┘        └──────────────────────┘
```

---

## Flags des sections

Chaque section possède des flags (`sh_flags`) qui indiquent ses propriétés mémoire. Les trois flags principaux sont :

| Flag | Lettre `readelf` | Signification |  
|------|-------------------|---------------|  
| `SHF_WRITE` | `W` | La section est accessible en écriture à l'exécution |  
| `SHF_ALLOC` | `A` | La section occupe de la mémoire à l'exécution (mappée en mémoire) |  
| `SHF_EXECINSTR` | `X` | La section contient du code exécutable |

D'autres flags moins courants existent :

| Flag | Lettre | Signification |  
|------|--------|---------------|  
| `SHF_MERGE` | `M` | Les éléments de la section peuvent être fusionnés (déduplication) |  
| `SHF_STRINGS` | `S` | La section contient des chaînes null-terminated (combiné avec `M` pour la déduplication) |  
| `SHF_INFO_LINK` | `I` | Le champ `sh_info` contient un index de section |  
| `SHF_GROUP` | `G` | La section fait partie d'un groupe (COMDAT, templates C++) |  
| `SHF_TLS` | `T` | La section contient des données Thread-Local Storage |

Combinaisons typiques rencontrées en RE :

| Flags | Signification pratique | Sections typiques |  
|-------|------------------------|-------------------|  
| `AX` | Code exécutable en lecture seule | `.text`, `.plt`, `.init`, `.fini` |  
| `A` | Données en lecture seule | `.rodata`, `.eh_frame`, `.dynsym` |  
| `WA` | Données accessibles en lecture-écriture | `.data`, `.bss`, `.got`, `.got.plt` |  
| `AMS` | Chaînes fusionnables en lecture seule | `.rodata` (quand le compilateur fusionne les chaînes identiques) |  
| (aucun) | Pas mappée en mémoire (métadonnées) | `.symtab`, `.strtab`, `.shstrtab`, `.comment` |

---

## Table complète des sections ELF

### Sections de code

#### `.text`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `AX` (Alloc + Exec) |  
| **Contenu** | Code machine compilé — le corps de toutes les fonctions du programme |  
| **Intérêt RE** | C'est **la** section principale d'analyse. Tout le désassemblage de `main()`, des fonctions utilisateur et des fonctions statiques de bibliothèques linkées statiquement se trouve ici. |

La section `.text` est la plus grande section exécutable du binaire. Elle contient l'intégralité du code compilé, à l'exception des trampolines PLT (dans `.plt`) et du code d'initialisation/finalisation (dans `.init`/`.fini`). GCC aligne les fonctions sur des frontières de 16 octets par défaut, ce qui produit du padding NOP entre les fonctions — ne les confondez pas avec du code significatif.

#### `.plt` (Procedure Linkage Table)

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `AX` |  
| **Contenu** | Trampolines de saut indirect pour les appels aux fonctions de bibliothèques partagées |  
| **Intérêt RE** | Chaque entrée correspond à une fonction importée. Un `call printf@plt` saute dans cette section, qui redirige via la GOT vers la vraie adresse de `printf` dans la libc. |

La PLT implémente le mécanisme de *lazy binding* : au premier appel, le trampoline invoque le résolveur dynamique (`ld.so`) pour trouver l'adresse réelle de la fonction et la stocker dans la GOT. Les appels suivants sautent directement à l'adresse résolue. En RE, les entrées PLT sont facilement reconnaissables par leur structure uniforme (un `jmp` indirect via la GOT, suivi d'un `push` d'index et d'un `jmp` vers le résolveur).

#### `.plt.got`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `AX` |  
| **Contenu** | Variante de la PLT pour les fonctions résolues de manière *eager* (non-lazy) |  
| **Intérêt RE** | Présente dans les binaires compilés avec `-z now` (Full RELRO). Les entrées sont de simples `jmp` indirects sans mécanisme de résolution lazy. |

#### `.plt.sec`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `AX` |  
| **Contenu** | PLT sécurisée avec protection CET (Intel Control-flow Enforcement) |  
| **Intérêt RE** | Présente dans les binaires compilés avec `-fcf-protection`. Chaque entrée commence par `endbr64` suivi d'un `jmp` indirect. Structure identique à `.plt.got` mais avec le marqueur CET. |

#### `.init`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `AX` |  
| **Contenu** | Code d'initialisation exécuté **avant** `main()` |  
| **Intérêt RE** | Contient généralement un appel à `__gmon_start__` (profiling) et l'invocation des fonctions listées dans `.init_array`. Utile pour repérer du code qui s'exécute avant `main()` (constructeurs globaux C++, anti-debugging). |

#### `.fini`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `AX` |  
| **Contenu** | Code de finalisation exécuté **après** le retour de `main()` |  
| **Intérêt RE** | Exécute les fonctions listées dans `.fini_array`. Les destructeurs globaux C++ et les fonctions enregistrées par `atexit()` passent par ce mécanisme. Un malware peut y cacher du code de nettoyage de traces. |

---

### Sections de données en lecture seule

#### `.rodata`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` (ou `AMS` si les chaînes sont fusionnées) |  
| **Contenu** | Données constantes : chaînes littérales, constantes `const`, tables de valeurs, jump tables de `switch` |  
| **Intérêt RE** | C'est la **mine d'or** des chaînes. `strings` et `iz` (r2) ciblent principalement cette section. Les messages d'erreur, noms de fichiers, URLs, clés hardcodées et format strings se trouvent ici. Les jump tables des `switch` compilés par GCC y résident également. |

Quand GCC compile un `switch` avec de nombreux `case` consécutifs, il génère une jump table dans `.rodata` : un tableau d'offsets relatifs, indexé par la valeur du `switch`. Reconnaître ce pattern (une série de dwords dans `.rodata` référencée par un `lea` + `movsxd` + `add` + `jmp` indirect) vous permet de reconstruire le `switch` original.

#### `.rodata1`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Données constantes supplémentaires (overflow de `.rodata`) |  
| **Intérêt RE** | Rare dans les binaires GCC courants. Même usage que `.rodata`. |

---

### Sections de données modifiables

#### `.data`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` (Write + Alloc) |  
| **Contenu** | Variables globales et statiques initialisées avec une valeur non nulle |  
| **Intérêt RE** | Contient les valeurs initiales des variables globales. Si un binaire contient un mot de passe hardcodé dans une variable globale (et non une constante `const`), il sera ici plutôt que dans `.rodata`. Les variables statiques de fonction (`static int count = 42;`) sont aussi dans `.data`. |

#### `.data1`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Données initialisées supplémentaires |  
| **Intérêt RE** | Rare. Même usage que `.data`. |

#### `.bss` (Block Started by Symbol)

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Variables globales et statiques initialisées à zéro (ou non initialisées) |  
| **Intérêt RE** | `.bss` **n'occupe pas d'espace dans le fichier** — elle est entièrement constituée de zéros alloués par le loader en mémoire. Sa taille indique la quantité de mémoire « vierge » que le programme utilise au démarrage. Les grands buffers globaux (`static char buffer[65536];`) se retrouvent ici. |

La distinction entre `.data` et `.bss` est une optimisation de taille de fichier : pourquoi stocker 64 Ko de zéros dans le fichier alors que le loader peut simplement allouer la mémoire et la remplir de zéros ? En RE, les variables dans `.bss` n'ont pas de valeur intéressante à lire dans le fichier lui-même — leur valeur ne devient significative qu'à l'exécution (analyse dynamique).

---

### Sections de liaison dynamique

#### `.dynamic`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Table de structures `Elf64_Dyn` — métadonnées pour le linker dynamique (`ld.so`) |  
| **Intérêt RE** | Contient les informations critiques pour la résolution dynamique : chemins des bibliothèques nécessaires (`DT_NEEDED`), adresses de la GOT, PLT, tables de symboles dynamiques, flags RELRO, etc. `readelf -d` affiche cette section de manière lisible. |

#### `.dynsym`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Table des symboles dynamiques (fonctions et variables importées/exportées) |  
| **Intérêt RE** | Contrairement à `.symtab`, cette table **survit au stripping** (`strip`). Elle contient les noms des fonctions importées (libc, libcrypto, etc.) et exportées. C'est la table que `nm -D` affiche. Sur un binaire strippé, c'est souvent la seule source de noms de fonctions. |

#### `.dynstr`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Table des chaînes associées aux symboles dynamiques |  
| **Intérêt RE** | Contient les noms des fonctions et bibliothèques référencées par `.dynsym` et `.dynamic`. Survit au stripping. |

#### `.gnu.hash`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Table de hachage GNU pour la résolution rapide des symboles dynamiques |  
| **Intérêt RE** | Remplace l'ancienne `.hash` (SysV). Utilisée par `ld.so` pour trouver rapidement un symbole par nom. Sa structure interne (bloom filter + buckets + chains) peut être exploitée pour énumérer les symboles même si d'autres tables sont corrompues. |

#### `.hash`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Table de hachage SysV (ancien format) pour la résolution de symboles |  
| **Intérêt RE** | Présente parfois en parallèle de `.gnu.hash` pour la compatibilité. Même rôle, algorithme de hachage différent. |

#### `.gnu.version` / `.gnu.version_r`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Informations de versioning des symboles (quel symbole appartient à quelle version de la libc) |  
| **Intérêt RE** | Permet de déterminer contre quelle version de la glibc le binaire a été compilé. Par exemple, `GLIBC_2.34` indique que le binaire nécessite au minimum la glibc 2.34. |

---

### Sections GOT (Global Offset Table)

#### `.got`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Table des offsets globaux pour les variables globales en code PIC/PIE |  
| **Intérêt RE** | Contient les adresses résolues des variables globales et de certaines fonctions. En Full RELRO, cette section est remappée en lecture seule après la résolution initiale. |

#### `.got.plt`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Sous-table de la GOT dédiée aux entrées PLT (fonctions de bibliothèques) |  
| **Intérêt RE** | Chaque entrée correspond à une fonction importée. Avant la résolution lazy, les entrées pointent vers le stub PLT de résolution. Après résolution, elles contiennent l'adresse réelle de la fonction dans la bibliothèque partagée. En RE dynamique, lire `.got.plt` avec GDB (`x/gx 0x...`) permet de voir quelles fonctions ont déjà été résolues. |

La distinction entre `.got` et `.got.plt` est liée au RELRO. En Partial RELRO (défaut GCC), `.got` est en lecture seule mais `.got.plt` reste modifiable (nécessaire pour le lazy binding). En Full RELRO (`-z now`), les deux sont en lecture seule après le chargement, ce qui bloque les attaques de type GOT overwrite.

---

### Sections d'initialisation et finalisation

#### `.init_array`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Tableau de pointeurs de fonctions exécutées avant `main()` |  
| **Intérêt RE** | Les constructeurs globaux C++ (constructeurs d'objets statiques), les fonctions marquées `__attribute__((constructor))` et les initialiseurs de bibliothèques partagées sont listés ici. Un malware peut enregistrer du code malveillant dans cette table pour qu'il s'exécute automatiquement. |

#### `.fini_array`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Tableau de pointeurs de fonctions exécutées après le retour de `main()` |  
| **Intérêt RE** | Destructeurs globaux C++ et fonctions marquées `__attribute__((destructor))`. Même considération de sécurité que `.init_array`. |

#### `.preinit_array`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Fonctions exécutées avant les constructeurs de `.init_array` |  
| **Intérêt RE** | Très rare. Uniquement dans les exécutables (pas les bibliothèques partagées). |

---

### Sections de symboles et de débogage

#### `.symtab`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | (aucun — pas mappée en mémoire) |  
| **Contenu** | Table complète des symboles (toutes les fonctions, variables, labels) |  
| **Intérêt RE** | Contient les noms de **toutes** les fonctions, y compris les fonctions statiques (`static`) et les variables locales. C'est la table la plus riche en informations. **Supprimée par `strip`** — si elle est présente, le binaire n'est pas strippé et votre travail de RE est considérablement facilité. |

#### `.strtab`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | (aucun) |  
| **Contenu** | Table des chaînes associées à `.symtab` |  
| **Intérêt RE** | Contient les noms de tous les symboles de `.symtab`. Supprimée par `strip` avec `.symtab`. |

#### `.shstrtab`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | (aucun) |  
| **Contenu** | Table des chaînes des noms de sections (`.text`, `.data`, etc.) |  
| **Intérêt RE** | Permet de donner un nom à chaque section. Survit généralement au stripping (sauf si le binaire est packé ou volontairement altéré). Si les noms de sections sont absents, `readelf` affiche des indices numériques à la place. |

#### `.debug_info`, `.debug_abbrev`, `.debug_line`, `.debug_str`, `.debug_ranges`, etc.

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | (aucun) |  
| **Contenu** | Informations de débogage au format DWARF (types, lignes source, variables, scopes) |  
| **Intérêt RE** | Présentes uniquement si le binaire a été compilé avec `-g`. C'est un trésor pour le RE : correspondance instruction ↔ ligne source, types complets des variables, noms des paramètres, arborescence des scopes. **Supprimées par `strip` ou `-s`**. Si elles sont présentes, exploitez-les immédiatement. |

#### `.note.gnu.build-id`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Hash unique identifiant cette compilation précise (SHA-1 ou UUID) |  
| **Intérêt RE** | Permet d'identifier de manière unique un build. Utile pour associer un binaire strippé à ses fichiers de symboles de débogage séparés (`.debug` files). Les serveurs de symboles (debuginfod) utilisent le build-id comme clé de recherche. |

#### `.note.ABI-tag`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Tag d'ABI indiquant le système cible (Linux, GNU, version minimale du noyau) |  
| **Intérêt RE** | Confirme que le binaire cible Linux et indique la version minimale requise du noyau. |

---

### Sections de gestion des exceptions

#### `.eh_frame`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Tables de déroulage de pile au format DWARF CFI (Call Frame Information) |  
| **Intérêt RE** | Utilisée par le mécanisme d'exceptions C++ et par les outils de profiling/débogage pour reconstruire la pile d'appels (stack unwinding). Même dans les binaires C purs, cette section est présente car GCC la génère par défaut. Elle survit au stripping et peut être exploitée pour reconstruire les limites des fonctions dans un binaire strippé. |

La section `.eh_frame` est une source d'information sous-estimée en RE. Chaque entrée (FDE — Frame Description Entry) décrit comment dérouler la pile pour une fonction donnée, ce qui implique qu'elle contient implicitement les adresses de début et de fin de chaque fonction. Des outils comme `dwarfdump --eh-frame` ou le plugin Ghidra peuvent exploiter cette information pour améliorer la détection de fonctions dans les binaires strippés.

#### `.eh_frame_hdr`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Index binaire (table de recherche) pour accéder rapidement aux entrées de `.eh_frame` |  
| **Intérêt RE** | Accélère le stack unwinding. Contient un tableau trié par adresse de début de fonction, ce qui en fait une mini-table de fonctions exploitable pour le RE. |

#### `.gcc_except_table`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Tables LSDA (Language Specific Data Area) pour les exceptions C++ |  
| **Intérêt RE** | Décrit les régions de code couvertes par des blocs `try`/`catch`, les types d'exceptions attrapées et les actions de nettoyage (cleanup). Présente uniquement dans les binaires C++ qui utilisent des exceptions. Aide à reconstruire la logique `try`/`catch` lors de la décompilation. |

---

### Sections de relocation

#### `.rela.dyn`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Table de relocations pour les données (variables globales, adresses dans `.got`) |  
| **Intérêt RE** | Indique quelles adresses dans le binaire doivent être ajustées par le loader au chargement. En PIE/PIC, presque toutes les références absolues nécessitent une relocation. `readelf -r` affiche cette table. |

#### `.rela.plt`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Table de relocations pour les entrées PLT (fonctions importées) |  
| **Intérêt RE** | Chaque entrée associe un slot de `.got.plt` à un symbole de fonction importée. C'est cette table qui permet de savoir que l'entrée GOT n° 5 correspond à `printf`, la n° 6 à `malloc`, etc. Essentielle pour nommer les appels PLT dans le désassemblage. |

---

### Sections TLS (Thread-Local Storage)

#### `.tdata`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WAT` (Write + Alloc + TLS) |  
| **Contenu** | Variables thread-local initialisées (`__thread int x = 42;` ou `thread_local int x = 42;`) |  
| **Intérêt RE** | Chaque thread reçoit sa propre copie de ces variables. L'accès passe par le registre de segment `fs` (ou `gs` selon l'ABI). En RE, un accès à `fs:[offset]` ou un pattern `mov rax, qword ptr fs:[0x28]` suivi de comparaisons indique soit du TLS, soit le **stack canary** (le canary est stocké en TLS à `fs:0x28` sur la glibc x86-64). |

#### `.tbss`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WAT` |  
| **Contenu** | Variables thread-local non initialisées (ou initialisées à zéro) |  
| **Intérêt RE** | Équivalent TLS de `.bss`. N'occupe pas d'espace dans le fichier. |

---

### Sections spéciales GCC/GNU

#### `.comment`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | (aucun) |  
| **Contenu** | Chaîne identifiant le compilateur et sa version |  
| **Intérêt RE** | Contient typiquement `GCC: (Ubuntu 12.3.0-1ubuntu1~22.04) 12.3.0` ou similaire. Permet d'identifier précisément la version du compilateur, la distribution et parfois les options de compilation. Information précieuse pour choisir les bonnes signatures dans Ghidra ou pour reproduire le build. |

#### `.note.gnu.property`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Propriétés GNU du binaire (support CET, BTI pour ARM, etc.) |  
| **Intérêt RE** | Indique si le binaire a été compilé avec des protections de flux de contrôle (CET/IBT). Survit au stripping. |

#### `.gnu.warning.*`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | (variable) |  
| **Contenu** | Avertissements émis par le linker quand un symbole spécifique est utilisé |  
| **Intérêt RE** | Rare. Les avertissements comme « this function is dangerous, use X instead » pour `gets()` passent par ce mécanisme. |

#### `.interp`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `A` |  
| **Contenu** | Chemin du linker dynamique (typiquement `/lib64/ld-linux-x86-64.so.2`) |  
| **Intérêt RE** | Indique quel interpréteur ELF est utilisé pour charger le binaire. Un chemin inhabituel peut indiquer un binaire cross-compilé, un environnement chroot ou une chaîne d'exploitation qui remplace le loader. Les binaires statiques n'ont pas cette section. |

---

### Sections de sécurité et de contrôle de flux

#### `.rela.dyn` / `.rela.plt` et RELRO

L'interaction entre les sections de relocation et la protection RELRO mérite une mention spécifique :

| Mode RELRO | `.got` | `.got.plt` | Impact RE |  
|------------|--------|------------|-----------|  
| **No RELRO** | `WA` (modifiable) | `WA` (modifiable) | Toute la GOT est modifiable → vulnérable au GOT overwrite |  
| **Partial RELRO** (défaut GCC) | Lecture seule après chargement | `WA` (modifiable, lazy binding) | `.got.plt` reste la cible d'attaques GOT overwrite |  
| **Full RELRO** (`-z now`) | Lecture seule | Lecture seule | Toute la GOT est protégée. Plus de lazy binding. Plus sûr mais démarrage plus lent |

---

### Sections rarement rencontrées mais à connaître

#### `.ctors` / `.dtors` (obsolète)

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Anciennes tables de constructeurs/destructeurs (remplacées par `.init_array`/`.fini_array`) |  
| **Intérêt RE** | Peuvent encore être présentes dans de vieux binaires ou des binaires compilés avec d'anciennes versions de GCC. Même rôle que `.init_array`/`.fini_array`. |

#### `.jcr` (Java Class Registration — obsolète)

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | `WA` |  
| **Contenu** | Table d'enregistrement de classes Java pour GCJ (GNU Compiler for Java, abandonné) |  
| **Intérêt RE** | Obsolète. Peut apparaître dans de très vieux binaires. |

#### `.stab` / `.stabstr`

| Propriété | Valeur |  
|-----------|--------|  
| **Flags** | (aucun) |  
| **Contenu** | Informations de débogage au format STABS (ancien format, remplacé par DWARF) |  
| **Intérêt RE** | Très rare sur les systèmes modernes. Si vous les rencontrez, le binaire est probablement très ancien ou cross-compilé depuis un système BSD/Solaris. |

---

## Commandes pour inspecter les sections

| Outil | Commande | Description |  
|-------|----------|-------------|  
| `readelf` | `readelf -S ./binary` | Liste toutes les sections avec flags, offsets et tailles |  
| `readelf` | `readelf -l ./binary` | Liste les segments (program headers) et les sections qu'ils contiennent |  
| `readelf` | `readelf -d ./binary` | Affiche la section `.dynamic` |  
| `readelf` | `readelf -r ./binary` | Affiche les tables de relocation (`.rela.dyn`, `.rela.plt`) |  
| `readelf` | `readelf -s ./binary` | Affiche la table des symboles (`.symtab` et `.dynsym`) |  
| `readelf` | `readelf -p .comment ./binary` | Affiche le contenu de la section `.comment` |  
| `readelf` | `readelf -n ./binary` | Affiche les notes (`.note.*`) |  
| `readelf` | `readelf --debug-dump=info ./binary` | Dump des informations DWARF |  
| `objdump` | `objdump -h ./binary` | Liste les sections avec leurs VMA et tailles |  
| `objdump` | `objdump -j .rodata -s ./binary` | Dump hexadécimal du contenu de `.rodata` |  
| r2 | `iS` | Liste les sections |  
| r2 | `iSS` | Liste les segments |  
| GDB | `maintenance info sections` | Sections avec flags et adresses |  
| GDB (GEF) | `xfiles` | Sections avec adresses en mémoire |

---

## Sections typiques d'un binaire GCC — vue synthétique

Le tableau suivant montre les sections que vous trouverez dans un binaire ELF x86-64 typique compilé par GCC, dans l'ordre approximatif où elles apparaissent dans le fichier :

| Section | Flags | Segment | Contenu résumé | Survit à `strip` ? |  
|---------|-------|---------|----------------|---------------------|  
| `.interp` | `A` | `INTERP` | Chemin du loader | Oui |  
| `.note.gnu.build-id` | `A` | `NOTE` | Build ID unique | Oui |  
| `.note.ABI-tag` | `A` | `NOTE` | Tag ABI Linux | Oui |  
| `.gnu.hash` | `A` | `LOAD R` | Hash table symboles | Oui |  
| `.dynsym` | `A` | `LOAD R` | Symboles dynamiques | Oui |  
| `.dynstr` | `A` | `LOAD R` | Noms des symboles dynamiques | Oui |  
| `.gnu.version` | `A` | `LOAD R` | Versioning des symboles | Oui |  
| `.gnu.version_r` | `A` | `LOAD R` | Versioning requis | Oui |  
| `.rela.dyn` | `A` | `LOAD R` | Relocations données | Oui |  
| `.rela.plt` | `A` | `LOAD R` | Relocations PLT | Oui |  
| `.init` | `AX` | `LOAD RX` | Code d'initialisation | Oui |  
| `.plt` | `AX` | `LOAD RX` | Trampolines PLT | Oui |  
| `.text` | `AX` | `LOAD RX` | **Code principal** | Oui |  
| `.fini` | `AX` | `LOAD RX` | Code de finalisation | Oui |  
| `.rodata` | `A` | `LOAD R` | **Données constantes, chaînes** | Oui |  
| `.eh_frame_hdr` | `A` | `LOAD R` | Index des frames d'exception | Oui |  
| `.eh_frame` | `A` | `LOAD R` | Tables de déroulage de pile | Oui |  
| `.init_array` | `WA` | `LOAD RW` | Pointeurs constructeurs | Oui |  
| `.fini_array` | `WA` | `LOAD RW` | Pointeurs destructeurs | Oui |  
| `.dynamic` | `WA` | `DYNAMIC` | Métadonnées linker dynamique | Oui |  
| `.got` | `WA` | `LOAD RW` | GOT (variables) | Oui |  
| `.got.plt` | `WA` | `LOAD RW` | GOT (fonctions PLT) | Oui |  
| `.data` | `WA` | `LOAD RW` | **Variables initialisées** | Oui |  
| `.bss` | `WA` | `LOAD RW` | Variables zéro-initialisées | Oui |  
| `.comment` | — | — | Version du compilateur | Parfois |  
| `.symtab` | — | — | Table de symboles complète | **Non** |  
| `.strtab` | — | — | Noms des symboles | **Non** |  
| `.shstrtab` | — | — | Noms des sections | Oui |  
| `.debug_*` | — | — | Informations DWARF | **Non** |

La colonne « Survit à `strip` ? » indique si la section est présente après un `strip --strip-all`. Les sections qui survivent sont celles nécessaires à l'exécution (flags `A`) ou à l'identification du binaire. Les sections de débogage et la table de symboles complète sont supprimées.

---

> 📚 **Pour aller plus loin** :  
> - **Chapitre 2, section 2.4** — [Sections ELF clés](/02-chaine-compilation-gnu/04-sections-elf.md) — couverture pédagogique avec des exemples concrets.  
> - **Chapitre 2, section 2.9** — [PLT/GOT en détail](/02-chaine-compilation-gnu/09-plt-got-lazy-binding.md) — fonctionnement détaillé du lazy binding.  
> - **Annexe A** — [Référence rapide des opcodes x86-64](/annexes/annexe-a-opcodes-x86-64.md) — les instructions que vous trouverez dans `.text`.  
> - **Annexe B** — [Conventions d'appel System V AMD64 ABI](/annexes/annexe-b-system-v-abi.md) — la convention qui régit le code de `.text`.  
> - **Annexe E** — [Cheat sheet ImHex](/annexes/annexe-e-cheatsheet-imhex.md) — écrire un `.hexpat` pour visualiser un header ELF.  
> - **Spécification ELF** — *Tool Interface Standard (TIS) Executable and Linking Format (ELF) Specification* — le document de référence officiel.  
> - **`man 5 elf`** — la page man Linux qui documente les structures ELF.

⏭️ [Comparatif des outils natifs (outil / usage / gratuit / CLI ou GUI)](/annexes/annexe-g-comparatif-outils-natifs.md)
