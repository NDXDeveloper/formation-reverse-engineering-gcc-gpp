🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.7 — Le Loader Linux (`ld.so`) : du fichier ELF au processus en mémoire

> 🎯 **Objectif de cette section** : Comprendre la séquence complète qui transforme un fichier ELF sur disque en un processus en cours d'exécution, identifier le rôle du loader dynamique `ld.so`, et savoir exploiter cette connaissance lors de l'analyse dynamique d'un binaire.

---

## Le problème : un fichier n'est pas un processus

Quand vous tapez `./hello` dans un terminal, il se passe bien plus qu'une simple « exécution du fichier ». Le fichier ELF sur disque est une description statique — un plan d'architecte. Le processus en mémoire est la construction réelle : du code chargé à des adresses précises, des zones mémoire allouées avec des permissions spécifiques, des bibliothèques partagées mappées, des tables de pointeurs remplies, et un compteur de programme (`rip`) positionné sur la première instruction à exécuter.

La transformation du plan en construction est assurée par deux acteurs complémentaires : le **noyau Linux** et le **loader dynamique** (`ld.so`, aussi appelé *dynamic linker* ou *interpréteur ELF*).

## Vue d'ensemble : de `./hello` à l'exécution de `main()`

Voici la séquence complète, que nous détaillerons étape par étape :

```
Terminal : ./hello RE-101
        │
        ▼
   ┌────────────────────────────────────────────────────────────────────┐
   │  1. Le shell appelle execve("./hello", ["hello","RE-101"], env)    │
   └──────────────────────────┬─────────────────────────────────────────┘
                              │
                              ▼
   ┌────────────────────────────────────────────────────────────────────┐
   │  2. Le NOYAU Linux :                                               │
   │     a. Ouvre le fichier, lit le ELF header                         │
   │     b. Lit la program header table (segments)                      │
   │     c. Trouve la section .interp → "/lib64/ld-linux-x86-64.so.2"   │
   │     d. Mappe les segments LOAD du binaire en mémoire               │
   │     e. Mappe le loader ld.so en mémoire                            │
   │     f. Prépare la pile initiale (argc, argv, envp, auxv)           │
   │     g. Transfère le contrôle à ld.so (pas à votre code)            │
   └──────────────────────────┬─────────────────────────────────────────┘
                              │
                              ▼
   ┌────────────────────────────────────────────────────────────────────┐
   │  3. LE LOADER ld.so :                                              │
   │     a. Lit la section .dynamic du binaire                          │
   │     b. Identifie les bibliothèques requises (NEEDED)               │
   │     c. Recherche et mappe chaque .so (libc.so.6, etc.)             │
   │     d. Résout récursivement les dépendances des .so                │
   │     e. Effectue les relocations (remplit la GOT, etc.)             │
   │     f. Exécute les fonctions d'initialisation (.init, .init_array) │
   │     g. Transfère le contrôle à _start du binaire                   │
   └──────────────────────────┬─────────────────────────────────────────┘
                              │
                              ▼
   ┌────────────────────────────────────────────────────────────────────┐
   │  4. LE CODE CRT (_start) :                                         │
   │     a. Appelle __libc_start_main(main, argc, argv, ...)            │
   │     b. __libc_start_main initialise la libc                        │
   │     c. Appelle les constructeurs (.init_array)                     │
   │     d. Appelle main(argc, argv)                                    │
   └──────────────────────────┬─────────────────────────────────────────┘
                              │
                              ▼
                    Votre fonction main() s'exécute enfin
```

## Étape 1 — L'appel système `execve`

Quand le shell interprète `./hello RE-101`, il effectue un `fork()` pour créer un processus fils, puis appelle `execve()` dans ce fils :

```c
execve("./hello", ["hello", "RE-101"], environ);
```

L'appel système `execve` est le point d'entrée dans le noyau. Il remplace entièrement l'image mémoire du processus courant (le shell fils) par le nouveau programme. Après un `execve` réussi, il n'y a pas de retour — le code du shell n'existe plus dans ce processus.

> 💡 **En RE** : L'appel `execve` est interceptable avec `strace` (Chapitre 5) et avec les catchpoints GDB (`catch exec` — Chapitre 11, section 11.6). Si un binaire lance un autre programme (dropper, loader de malware), vous verrez l'appel `execve` dans la trace. Les arguments passés (le tableau `argv`) sont visibles en clair.

## Étape 2 — Le noyau prépare le terrain

### Lecture et validation du fichier ELF

Le noyau ouvre le fichier, lit les premiers octets et identifie le format grâce au magic number `\x7fELF`. Il parse ensuite l'en-tête ELF (*ELF header*) pour déterminer l'architecture (x86-64), le type (`ET_EXEC` ou `ET_DYN`), et localiser la **program header table** — la vue en segments.

Si le magic number ne correspond pas à un format reconnu (ELF, script `#!`, etc.), `execve` échoue avec `ENOEXEC` (*Exec format error*).

### Lecture de la program header table

Le noyau parcourt la program header table à la recherche de deux types d'entrées critiques :

**Les segments `PT_LOAD`** : ce sont les zones du fichier qui doivent être mappées en mémoire. Un binaire typique en a deux :

| Segment | Permissions | Contenu typique |  
|---|---|---|  
| `LOAD` #1 | `R-X` (lecture + exécution) | `.text`, `.plt`, `.rodata`, `.init`, `.fini`, `.eh_frame` |  
| `LOAD` #2 | `RW-` (lecture + écriture) | `.data`, `.bss`, `.got`, `.got.plt`, `.dynamic` |

**Le segment `PT_INTERP`** : il contient le chemin du loader dynamique. Le noyau lit la chaîne (typiquement `/lib64/ld-linux-x86-64.so.2`) et sait qu'il devra aussi charger ce programme.

```bash
# Voir la program header table
readelf -l hello
```

Sortie simplifiée :

```
Program Headers:
  Type           Offset   VirtAddr           FileSiz  MemSiz   Flg Align
  PHDR           0x000040 0x0000000000000040 0x0002d8 0x0002d8 R   0x8
  INTERP         0x000318 0x0000000000000318 0x00001c 0x00001c R   0x1
      [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
  LOAD           0x000000 0x0000000000000000 0x000628 0x000628 R   0x1000
  LOAD           0x001000 0x0000000000001000 0x0001d5 0x0001d5 R E 0x1000
  LOAD           0x002000 0x0000000000002000 0x000150 0x000150 R   0x1000
  LOAD           0x002db8 0x0000000000003db8 0x000260 0x000268 RW  0x1000
  DYNAMIC        0x002dc8 0x0000000000003dc8 0x0001f0 0x0001f0 RW  0x8
  ...
```

Chaque segment `LOAD` a un **offset dans le fichier** (`Offset`), une **adresse virtuelle** (`VirtAddr`), une **taille dans le fichier** (`FileSiz`), une **taille en mémoire** (`MemSiz`) et des **permissions** (`Flg`). Quand `MemSiz` est supérieur à `FileSiz`, la différence est remplie de zéros — c'est ainsi que la section `.bss` (type `NOBITS` dans le fichier) est allouée en mémoire.

### Mappage en mémoire (`mmap`)

Pour chaque segment `LOAD`, le noyau utilise l'appel système `mmap` pour projeter la portion correspondante du fichier dans l'espace d'adressage du processus. Les permissions du mappage (`PROT_READ`, `PROT_WRITE`, `PROT_EXEC`) correspondent aux flags du segment.

Pour un binaire PIE (`ET_DYN`), le noyau choisit une adresse de base **aléatoire** (ASLR — section 2.8) et ajoute cette base à toutes les adresses virtuelles des segments. Pour un binaire non-PIE (`ET_EXEC`), les adresses sont fixes (typiquement base `0x400000` sur x86-64).

### Préparation de la pile initiale

Le noyau alloue la pile du processus (en haut de l'espace d'adressage, croissant vers le bas) et y dépose une structure bien définie que `_start` et `__libc_start_main` s'attendent à trouver :

```
        Haut de la pile (adresses hautes)
    ┌──────────────────────────────┐
    │  Chaînes d'environnement     │ ← "PATH=/usr/bin:...", "HOME=/home/user", ...
    │  Chaînes d'arguments         │ ← "./hello\0", "RE-101\0"
    ├──────────────────────────────┤
    │  Auxiliary Vector (auxv)     │ ← Paires clé-valeur pour le loader
    │    AT_PHDR   = 0x...         │    (adresse de la program header table)
    │    AT_PHNUM  = 13            │    (nombre d'entrées)
    │    AT_ENTRY  = 0x...         │    (point d'entrée _start)
    │    AT_BASE   = 0x...         │    (adresse de base de ld.so)
    │    AT_RANDOM = 0x...         │    (16 octets aléatoires pour le canary)
    │    AT_NULL   = 0             │    (fin du vecteur)
    ├──────────────────────────────┤
    │  envp[n] = NULL              │
    │  envp[1] = ptr → "HOME=..."  │
    │  envp[0] = ptr → "PATH=..."  │
    ├──────────────────────────────┤
    │  argv[2] = NULL              │
    │  argv[1] = ptr → "RE-101"    │
    │  argv[0] = ptr → "./hello"   │
    ├──────────────────────────────┤
    │  argc = 2                    │ ← rsp pointe ici au démarrage
    └──────────────────────────────┘
        Bas de la pile (adresses basses, la pile croît vers ici)
```

L'**Auxiliary Vector** (`auxv`) est un mécanisme de communication noyau → loader. Il contient des informations que le noyau transmet au loader et à la libc : l'adresse de la program header table en mémoire, le point d'entrée du programme, l'adresse de base du loader lui-même, un pointeur vers 16 octets aléatoires (utilisés par la libc pour initialiser le stack canary), la taille des pages mémoire, etc.

```bash
# Voir l'auxiliary vector d'un processus en cours
LD_SHOW_AUXV=1 ./hello RE-101
```

> 💡 **En RE** : L'Auxiliary Vector est exploitable en analyse dynamique. La valeur `AT_RANDOM` sert de graine pour le stack canary — si vous pouvez la lire (par exemple via `/proc/<pid>/auxv` ou via GDB), vous connaissez la valeur du canary sans avoir besoin de la leaker. La valeur `AT_BASE` révèle l'adresse de chargement de `ld.so`, ce qui est utile pour contourner l'ASLR dans certains scénarios d'exploitation.

### Transfert au loader

Le noyau ne transfère **pas** le contrôle au point d'entrée du programme (`_start`). Il positionne `rip` sur le point d'entrée du **loader** (`ld.so`). C'est le loader qui prendra en charge la suite.

Pour un binaire statiquement lié (`-static`), il n'y a pas de segment `PT_INTERP` et donc pas de loader. Le noyau transfère le contrôle directement à `_start`.

## Étape 3 — Le loader dynamique (`ld.so`)

### Identité et localisation

Le loader dynamique est lui-même un binaire ELF partagé, installé sur le système. Son nom complet dépend de l'architecture et de la distribution :

| Architecture | Chemin typique |  
|---|---|  
| x86-64 | `/lib64/ld-linux-x86-64.so.2` |  
| x86 (32 bits) | `/lib/ld-linux.so.2` |  
| ARM64 (AArch64) | `/lib/ld-linux-aarch64.so.1` |

Ce chemin est celui enregistré dans la section `.interp` du binaire. Le loader est un programme spécial : il est conçu pour fonctionner sans dépendances dynamiques (il ne peut pas se charger lui-même). Tout son code est résolu statiquement.

### Résolution des dépendances

Le loader lit la section `.dynamic` du binaire et parcourt les entrées `DT_NEEDED` pour identifier les bibliothèques partagées requises :

```bash
readelf -d hello | grep NEEDED
#  0x0000000000000001 (NEEDED)   Shared library: [libc.so.6]
```

Pour chaque bibliothèque requise, le loader doit la trouver sur le système de fichiers. L'ordre de recherche est le suivant :

1. **`DT_RPATH`** (déprécié) ou **`DT_RUNPATH`** : chemins codés dans le binaire par le linker (option `-rpath`).  
2. **`LD_LIBRARY_PATH`** : variable d'environnement (ignorée pour les binaires setuid/setgid pour des raisons de sécurité).  
3. **Cache `ldconfig`** : le fichier `/etc/ld.so.cache`, un index pré-calculé des bibliothèques disponibles dans les répertoires configurés dans `/etc/ld.so.conf`.  
4. **Chemins par défaut** : `/lib`, `/usr/lib` (et leurs variantes 64 bits).

Une fois une `.so` trouvée, le loader la mappe en mémoire avec `mmap` (exactement comme le noyau l'a fait pour le binaire principal) et parse sa propre section `.dynamic` pour trouver ses dépendances — le processus est **récursif**. L'ensemble des bibliothèques chargées forme un graphe de dépendances.

```bash
# Voir l'ordre de recherche et les bibliothèques trouvées
LD_DEBUG=libs ./hello RE-101

# Lister les dépendances résolues
ldd hello
#   linux-vdso.so.1 (0x00007ffd3abfe000)
#   libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f8c3a200000)
#   /lib64/ld-linux-x86-64.so.2 (0x00007f8c3a5f0000)
```

> ⚠️ **Sécurité** : `ldd` exécute potentiellement le binaire pour résoudre les dépendances. Sur un binaire non fiable, utilisez plutôt `readelf -d binaire | grep NEEDED` ou `objdump -p binaire | grep NEEDED`, qui sont des analyses purement statiques.

### Relocations

Après avoir mappé toutes les bibliothèques, le loader doit **corriger les adresses** dans le code et les données du binaire et de ses dépendances. C'est la phase de relocation.

Les relocations sont stockées dans les sections `.rela.dyn` (pour les données, typiquement les entrées GOT) et `.rela.plt` (pour les appels de fonctions via PLT). Pour chaque entrée de relocation, le loader calcule l'adresse réelle du symbole référencé et l'écrit à l'emplacement indiqué.

Deux modes de relocation existent pour les fonctions PLT, contrôlés par la variable d'environnement `LD_BIND_NOW` ou le flag `DT_BIND_NOW` dans `.dynamic` :

- **Lazy binding** (par défaut) : les entrées `.got.plt` ne sont pas résolues immédiatement. Chaque entrée pointe initialement vers le stub PLT qui appelle le loader pour résoudre le symbole au premier appel. Ce mécanisme est détaillé en section 2.9.  
- **Immediate binding** (`LD_BIND_NOW=1` ou Full RELRO) : toutes les entrées GOT sont résolues au chargement, avant l'exécution du moindre code utilisateur. Plus lent au démarrage, mais plus sûr (la GOT peut être rendue read-only — section 2.9 et Chapitre 19).

### Initialisation

Une fois les relocations effectuées, le loader exécute les **fonctions d'initialisation** dans l'ordre :

1. Les fonctions `.init` et `.init_array` de chaque bibliothèque partagée, dans l'ordre de dépendance (les bibliothèques les plus profondes en premier).  
2. Les fonctions `.init` et `.init_array` du binaire principal.

En C++, c'est à ce moment que les constructeurs des variables globales et statiques sont exécutés. En C, les fonctions marquées `__attribute__((constructor))` sont appelées ici.

### Transfert au programme

Le loader transfère finalement le contrôle au **point d'entrée** du binaire, indiqué par le champ `e_entry` de l'en-tête ELF. Ce point d'entrée est `_start`, la première fonction du CRT (C Runtime).

## Étape 4 — Le code CRT et le chemin vers `main()`

Le point d'entrée `_start` n'est pas votre code — c'est du code fourni par la toolchain (fichiers `crt1.o`, `crti.o`, `crtn.o` liés automatiquement par GCC). Sa tâche est de préparer l'appel à `main()` :

```asm
; _start simplifié (crt1.o, x86-64)
_start:
    xor     ebp, ebp              ; Marquer le fond de pile (rbp = 0)
    mov     rdi, [rsp]            ; argc
    lea     rsi, [rsp+8]         ; argv
    call    __libc_start_main     ; Initialiser la libc et appeler main
    hlt                           ; Ne devrait jamais être atteint
```

La fonction `__libc_start_main` (définie dans la glibc) est le véritable orchestrateur :

1. Enregistre les pointeurs vers les fonctions `.init`, `.fini`, `__libc_csu_init` et `__libc_csu_fini`.  
2. Initialise les structures internes de la libc (allocateur mémoire, threads, signaux, flux I/O standards).  
3. Appelle les fonctions d'initialisation restantes (qui n'auraient pas été appelées par le loader).  
4. Appelle **`main(argc, argv, envp)`**.  
5. Au retour de `main()`, appelle `exit()` avec la valeur de retour, ce qui déclenche les destructeurs (`.fini_array`, `.fini`), vide les buffers I/O, et termine le processus.

> 💡 **En RE** : Quand vous ouvrez un binaire dans Ghidra ou IDA, le point d'entrée affiché est `_start`, pas `main`. Pour trouver `main` rapidement dans un binaire strippé, cherchez le premier argument passé à `__libc_start_main` — c'est l'adresse de `main`. En x86-64, cet argument est dans `rdi` :  
>  
> ```asm  
> lea    rdi, [rip + 0x1234]    ; ← Adresse de main !  
> call   __libc_start_main  
> ```  
>  
> Cette technique fonctionne sur la quasi-totalité des binaires ELF liés dynamiquement avec la glibc.

## L'espace mémoire du processus après chargement

Une fois toutes les étapes terminées, l'espace d'adressage virtuel du processus ressemble à ceci :

```
    Adresses hautes (0x7fff...)
    ┌──────────────────────────────────────┐
    │            PILE (Stack)              │ ← Croît vers le bas
    │  argc, argv, envp, auxv              │    rsp pointe ici
    │  Variables locales, frames d'appel   │
    │            ↓ ↓ ↓                     │
    ├──────────────────────────────────────┤
    │                                      │
    │        (espace non mappé)            │
    │                                      │
    ├──────────────────────────────────────┤
    │            ↑ ↑ ↑                     │
    │            TAS (Heap)                │ ← Croît vers le haut
    │  malloc(), new, brk/mmap             │    program break ici
    ├──────────────────────────────────────┤
    │                                      │
    │   Bibliothèques partagées (.so)      │
    │   libc.so.6      (R-X / RW-)         │ ← Mappées par ld.so
    │   ld-linux-x86-64.so.2               │
    │   linux-vdso.so.1                    │
    │                                      │
    ├──────────────────────────────────────┤
    │                                      │
    │   Binaire principal                  │
    │   Segment LOAD R-X (.text, .rodata)  │ ← Mappé par le noyau
    │   Segment LOAD RW- (.data, .bss,     │
    │                      .got, .got.plt) │
    │                                      │
    ├──────────────────────────────────────┤
    │   [vdso] / [vvar]                    │ ← Page spéciale du noyau
    └──────────────────────────────────────┘
    Adresses basses (0x0000...)
    (page zéro non mappée — accès = SIGSEGV)
```

Chaque zone est un **mappage mémoire** visible dans `/proc/<pid>/maps` :

```bash
# Voir la carte mémoire d'un processus
cat /proc/$(pidof hello)/maps
```

Sortie typique :

```
55a3c4000000-55a3c4001000 r--p 00000000 08:01 12345  /home/user/hello
55a3c4001000-55a3c4002000 r-xp 00001000 08:01 12345  /home/user/hello
55a3c4002000-55a3c4003000 r--p 00002000 08:01 12345  /home/user/hello
55a3c4003000-55a3c4005000 rw-p 00002db8 08:01 12345  /home/user/hello
7f8c3a200000-7f8c3a228000 r--p 00000000 08:01 67890  /lib/.../libc.so.6
7f8c3a228000-7f8c3a3bd000 r-xp 00028000 08:01 67890  /lib/.../libc.so.6
...
7ffd3ab80000-7ffd3aba1000 rw-p 00000000 00:00 0      [stack]
7ffd3abfe000-7ffd3ac00000 r-xp 00000000 00:00 0      [vdso]
```

Chaque ligne montre la plage d'adresses, les permissions (`r`ead / `w`rite / e`x`ecute / `p`rivate), l'offset dans le fichier, le device, l'inode et le nom du fichier mappé. Les entrées sans nom de fichier sont des mappages anonymes (pile, tas).

> 💡 **En RE** : La carte `/proc/<pid>/maps` est un outil fondamental en analyse dynamique. Elle vous dit exactement où chaque composant est chargé en mémoire, quelles sont ses permissions, et à quel fichier il correspond. Dans GDB, la commande `info proc mappings` (ou `vmmap` dans GEF/pwndbg) affiche les mêmes informations. Savoir lire cette carte est un prérequis pour le Chapitre 11 (GDB) et le Chapitre 12 (extensions GDB).

## Contrôler et observer le loader

Le loader `ld.so` offre plusieurs mécanismes de contrôle et de diagnostic via des variables d'environnement :

### Variables de diagnostic

| Variable | Effet |  
|---|---|  
| `LD_DEBUG=all` | Active le mode verbeux du loader — affiche chaque étape de la résolution |  
| `LD_DEBUG=libs` | Affiche uniquement la recherche des bibliothèques |  
| `LD_DEBUG=bindings` | Affiche chaque liaison symbole → adresse |  
| `LD_DEBUG=reloc` | Affiche les relocations effectuées |  
| `LD_DEBUG=symbols` | Affiche la recherche de chaque symbole |  
| `LD_DEBUG=versions` | Affiche le versioning des symboles |  
| `LD_DEBUG_OUTPUT=fichier` | Redirige la sortie de debug vers un fichier |

```bash
# Observer les symboles résolus par le loader
LD_DEBUG=bindings ./hello RE-101 2>&1 | grep strcmp
# binding file ./hello [0] to /lib/.../libc.so.6 [0]: normal symbol `strcmp'
```

### Variables de contrôle

| Variable | Effet |  
|---|---|  
| `LD_LIBRARY_PATH=/chemin` | Ajoute un répertoire de recherche pour les `.so` |  
| `LD_PRELOAD=libhook.so` | Force le chargement d'une `.so` avant toutes les autres |  
| `LD_BIND_NOW=1` | Désactive le lazy binding (résout tout au chargement) |  
| `LD_SHOW_AUXV=1` | Affiche l'Auxiliary Vector |  
| `LD_TRACE_LOADED_OBJECTS=1` | Simule `ldd` (liste les `.so` sans exécuter le programme) |

### `LD_PRELOAD` — L'outil du reverse engineer

La variable `LD_PRELOAD` est particulièrement puissante pour le RE. Elle force le loader à charger une bibliothèque partagée **en premier**, avant toutes les autres. Les symboles définis dans cette bibliothèque prennent alors priorité sur ceux des bibliothèques normales (y compris la libc).

Cela permet de **remplacer n'importe quelle fonction de bibliothèque** sans modifier le binaire :

```c
// hook_strcmp.c — intercepte strcmp pour afficher les arguments
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

int strcmp(const char *s1, const char *s2) {
    // Afficher les arguments
    fprintf(stderr, "[HOOK] strcmp(\"%s\", \"%s\")\n", s1, s2);

    // Appeler le vrai strcmp
    int (*real_strcmp)(const char *, const char *) = dlsym(RTLD_NEXT, "strcmp");
    return real_strcmp(s1, s2);
}
```

```bash
gcc -shared -fPIC -o hook_strcmp.so hook_strcmp.c -ldl  
LD_PRELOAD=./hook_strcmp.so ./hello test123  
# [HOOK] strcmp("test123", "RE-101")
# Accès refusé.
```

En une commande, vous avez intercepté l'appel à `strcmp` et révélé le mot de passe attendu — sans rien désassembler. Nous approfondirons cette technique au Chapitre 22, section 22.4.

> ⚠️ **Limitation** : `LD_PRELOAD` est ignorée pour les binaires **setuid/setgid** (pour des raisons de sécurité évidentes). Elle ne fonctionne pas non plus sur les binaires statiquement liés (pas de loader dynamique). Et elle ne peut intercepter que les appels via PLT/GOT — pas les appels directs au sein du même binaire.

## Cas particulier : les binaires statiquement liés

Un binaire compilé avec `-static` n'a pas de section `.interp`, pas de segment `PT_INTERP`, et pas de section `.dynamic`. Le noyau constate l'absence de loader et transfère le contrôle directement à `_start`. Il n'y a pas de résolution dynamique, pas de PLT/GOT, pas de bibliothèques partagées — tout le code nécessaire est dans le binaire.

```bash
gcc -static -o hello_static hello.c  
readelf -l hello_static | grep INTERP  
# (aucune sortie)
ldd hello_static
# not a dynamic executable
```

Pour le reverse engineer, l'absence de loader simplifie le démarrage (pas de résolution dynamique à comprendre) mais complique l'analyse globale : les milliers de fonctions de la libc sont directement dans le binaire, mélangées avec le code de l'application, et sans les noms pratiques que la PLT fournit dans un binaire dynamique.

## Le `vDSO` — L'optimisation invisible du noyau

Dans la carte mémoire, vous avez peut-être remarqué l'entrée `[vdso]` (*virtual Dynamic Shared Object*). C'est une petite bibliothèque partagée **injectée par le noyau** dans l'espace d'adressage de chaque processus, sans qu'aucun fichier ne soit mappé depuis le disque.

Le vDSO contient des implémentations optimisées de certains appels système fréquents (`gettimeofday`, `clock_gettime`, `getcpu`) qui peuvent être exécutés en espace utilisateur sans le coût d'une transition vers le noyau (pas de `syscall`). C'est une optimisation de performance transparente.

En RE, le vDSO explique pourquoi certains appels à `gettimeofday()` dans un `strace` n'apparaissent pas comme des `syscall` — ils sont résolus directement en espace utilisateur via le vDSO.

---

> 📖 **Le loader a chargé notre binaire en mémoire, mais les adresses choisies ne sont pas fixes.** L'ASLR brouille volontairement les cartes d'une exécution à l'autre. Dans la section suivante, nous verrons comment le mappage des segments, l'ASLR et les adresses virtuelles interagissent — et ce que cela change pour le reverse engineer.  
>  
> → 2.8 — Mappage des segments, ASLR et adresses virtuelles : pourquoi les adresses bougent

⏭️ [Mappage des segments, ASLR et adresses virtuelles : pourquoi les adresses bougent](/02-chaine-compilation-gnu/08-segments-aslr.md)
