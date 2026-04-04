🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 34.1 — Spécificités du runtime Go : goroutines, scheduler, GC

> 🐹 *Avant d'analyser du code Go, il faut comprendre la machine invisible qui tourne en arrière-plan. Le runtime Go n'est pas une bibliothèque externe — il fait partie intégrante de chaque binaire. En RE, vous le verrez partout : dans les prologues de fonctions, dans les milliers de symboles `runtime.*`, et dans le comportement mémoire du processus. Cette section vous apprend à le reconnaître pour mieux l'ignorer quand il ne vous intéresse pas — et mieux l'exploiter quand il vous intéresse.*

---

## Vue d'ensemble du runtime Go

En C, le runtime est quasi inexistant : le `_start` appelle `__libc_start_main`, qui appelle votre `main()`, et le reste est délégué au noyau via la libc. En Go, c'est radicalement différent. Le binaire embarque un runtime complet qui s'initialise bien avant que votre `main.main()` ne s'exécute. Ce runtime comprend :

- un **scheduler coopératif** qui multiplexe des milliers de goroutines sur quelques threads OS,  
- un **garbage collector** concurrent et tri-color qui gère automatiquement la mémoire,  
- un **allocateur mémoire** à arènes inspiré de TCMalloc,  
- un système de **piles extensibles** (segmented stacks, puis contiguous stacks depuis Go 1.4),  
- la gestion des **signaux POSIX**, des **timers**, du **réseau** (netpoller intégré),  
- les mécanismes de **reflection**, **interfaces** et **type assertion**.

Quand vous ouvrez un binaire Go dans Ghidra et lancez l'auto-analyse, vous verrez apparaître des milliers de fonctions préfixées `runtime.`. C'est normal. Un « Hello, World! » en Go contient typiquement entre 1 500 et 2 500 fonctions — dont moins de dix appartiennent à votre code métier. Savoir filtrer ce bruit est la première compétence à acquérir.

---

## La séquence de démarrage : de `_rt0` à `main.main`

Comprendre le chemin d'exécution entre le point d'entrée ELF et votre code est essentiel pour ne pas se perdre lors d'une analyse dynamique. Voici la chaîne d'appels simplifiée sur Linux/amd64 :

```
_rt0_amd64_linux          ← entry point ELF (équivalent de _start)
  → runtime.rt0_go        ← initialisation bas niveau (TLS, stack, argc/argv)
    → runtime.schedinit    ← initialisation du scheduler, du GC, de la mémoire
    → runtime.newproc      ← crée la première goroutine pour runtime.main
    → runtime.mstart       ← démarre le thread M0 (premier thread OS)
      → runtime.main       ← initialisation des packages (init()), puis…
        → main.main        ← votre code
```

### Ce que vous verrez dans Ghidra / objdump

Le point d'entrée ELF (`readelf -h`) pointe sur `_rt0_amd64_linux`, une fonction de quelques instructions assembleur qui se contente de placer `argc` et `argv` dans les registres, puis saute vers `runtime.rt0_go`.

`runtime.rt0_go` est une longue fonction assembleur (écrite à la main dans `src/runtime/asm_amd64.s` des sources Go) qui :

1. configure la pile initiale du thread,  
2. initialise le TLS (Thread Local Storage) pour stocker le pointeur `g` (la goroutine courante),  
3. détecte les capacités CPU (CPUID) pour activer les optimisations SIMD,  
4. appelle `runtime.schedinit` pour initialiser le scheduler,  
5. crée une goroutine initiale avec `runtime.newproc` pointant vers `runtime.main`,  
6. appelle `runtime.mstart` pour lancer la boucle du scheduler.

> 💡 **Astuce RE** : si vous cherchez `main.main` dans un binaire Go, ne posez pas votre breakpoint sur le point d'entrée ELF. Cherchez directement le symbole `main.main` (ou son adresse via `gopclntab` si le binaire est strippé). Vous éviterez de traverser des centaines d'instructions d'initialisation du runtime.

### Repérer `main.main` dans un binaire strippé

Même sans symboles, la chaîne `main.main` est souvent récupérable via `gopclntab` (section 34.4). Mais une méthode rapide en analyse dynamique :

1. Poser un breakpoint sur `runtime.main` (retrouvable via `gopclntab`).  
2. Exécuter en pas-à-pas : `runtime.main` appelle les fonctions `init()` de chaque package importé, puis effectue un `call` vers `main.main`. C'est le dernier appel significatif avant la boucle de sortie.

---

## Les goroutines vues depuis le désassembleur

### Le modèle M:N

Go implémente un modèle de concurrence **M:N** — M goroutines multiplexées sur N threads OS. Trois structures internes orchestrent cette mécanique :

- **G** (goroutine) : la structure `runtime.g` — contient le contexte d'exécution, l'état de la pile, le pointeur de stack, le status (idle, runnable, running, syscall, waiting…).  
- **M** (machine) : la structure `runtime.m` — représente un thread OS. Chaque M exécute une goroutine à la fois.  
- **P** (processor) : la structure `runtime.p` — un processeur logique qui détient une file d'attente locale de goroutines. Le nombre de P est fixé par `GOMAXPROCS`.

Le scheduler assigne les G aux M via les P. Quand une goroutine bloque (I/O, channel, mutex), le scheduler détache le G du M et en exécute un autre — sans syscall `clone()` ni changement de contexte noyau.

### Ce que ça donne en assembleur

Quand vous voyez dans votre code source Go :

```go
go maFonction(arg1, arg2)
```

Le compilateur génère un appel à `runtime.newproc`. En assembleur (syntaxe Intel, Go récent avec ABI register-based) :

```asm
; Préparation des arguments pour runtime.newproc
LEA     RAX, [main.maFonction·f]   ; pointeur vers la closure/fonction
; ... arguments empilés ou placés en registres selon ABI ...
CALL    runtime.newproc
```

`runtime.newproc` alloue une structure `runtime.g` (environ 400 octets sur Go 1.21+), copie les arguments de la goroutine sur sa pile initiale (2 Ko par défaut depuis Go 1.4, extensible dynamiquement), et place le nouveau G dans la file d'attente locale du P courant.

> 💡 **Astuce RE** : chaque `CALL runtime.newproc` dans le désassemblage correspond à un `go func()` dans le code source. En comptant ces appels et en identifiant la fonction cible (le premier argument, souvent un `LEA` vers un symbole `main.xxx·f`), vous pouvez reconstruire la carte des goroutines lancées par le programme.

### Structure `runtime.g` en mémoire

La structure `g` est volumineuse et change entre versions de Go, mais les champs importants pour le RE restent stables :

| Offset (Go 1.21, amd64) | Champ | Description |  
|---|---|---|  
| `+0x00` | `stack.lo` | Bas de la pile de la goroutine |  
| `+0x08` | `stack.hi` | Haut de la pile |  
| `+0x10` | `stackguard0` | Sentinelle pour la détection de dépassement de pile |  
| `+0x18` | `stackguard1` | Sentinelle secondaire (utilisée par le runtime) |  
| `+0x???` | `goid` | ID unique de la goroutine (entier incrémental) |  
| `+0x???` | `atomicstatus` | État courant (idle=0, runnable=1, running=2…) |  
| `+0x???` | `sched` | `gobuf` — registres sauvegardés (SP, PC, ctxt…) |

Les offsets exacts varient selon la version du compilateur. Pour les déterminer sur votre binaire, cherchez les accès mémoire récurrents dans les fonctions `runtime.gogo`, `runtime.gosave` et `runtime.mcall` — elles manipulent directement les champs de `g`.

> 💡 **Astuce RE** : dans GDB, le registre `R14` (depuis Go 1.17 sur amd64) pointe en permanence vers la structure `g` de la goroutine en cours d'exécution. Avant Go 1.17, ce pointeur était stocké dans le TLS. La commande `info registers r14` suivie de `x/20gx $r14` vous donne un dump brut de la structure `g` courante.

---

## Les piles extensibles et le stack split

### Le problème

Chaque goroutine commence avec une pile de seulement 2 à 8 Ko (selon la version de Go). Avec potentiellement des milliers de goroutines actives, allouer 1 Mo par pile comme pour les threads POSIX serait prohibitif. Go résout ce problème avec des piles extensibles : quand la pile manque de place, le runtime en alloue une plus grande, y copie le contenu, puis met à jour tous les pointeurs.

### Le préambule de vérification de pile

Voici la conséquence directe la plus visible en RE : **pratiquement chaque fonction Go commence par un préambule de vérification de pile**. C'est un pattern extrêmement reconnaissable :

```asm
; Prologue typique d'une fonction Go (amd64)
; R14 = pointeur vers g (goroutine courante)
MOV     RAX, [R14+0x10]        ; RAX = g.stackguard0  
CMP     RSP, RAX               ; la pile actuelle dépasse-t-elle la sentinelle ?  
JBE     _morestack             ; si oui → agrandir la pile  
; --- corps de la fonction ---
...
; En fin de fichier, ou juste après :
_morestack:
    CALL    runtime.morestack_noctxt
    JMP     _début_de_la_fonction   ; réessayer après agrandissement
```

Ce pattern se répète dans quasiment toutes les fonctions du binaire. En RE :

- **Ne le confondez pas avec un stack canary.** Le mécanisme est fondamentalement différent : il ne protège pas contre les buffer overflows, il gère la croissance dynamique de la pile.  
- **Utilisez-le comme signature.** Si vous voyez ce pattern dans un binaire inconnu, c'est très probablement du Go compilé.  
- **Ignorez-le mentalement.** Quand vous analysez la logique métier, sautez directement après le `JBE` pour atteindre le vrai début de la fonction.

La fonction `runtime.morestack_noctxt` (ou `runtime.morestack`) est le mécanisme d'agrandissement : elle alloue une nouvelle pile plus grande, copie l'ancienne, met à jour les pointeurs, puis relance la fonction depuis le début.

> 💡 **Astuce RE** : dans Ghidra, vous pouvez écrire un script simple qui identifie toutes les fonctions contenant le pattern `CMP RSP, [R14+0x10]` suivi d'un `JBE`. Cela vous donne un inventaire fiable des fonctions Go dans le binaire, même sans symboles.

---

## Le scheduler vu depuis le RE

### Fonctions clés du scheduler

En parcourant un binaire Go, vous rencontrerez fréquemment ces fonctions du scheduler. Connaître leur rôle vous évitera de perdre du temps à les analyser :

| Fonction | Rôle |  
|---|---|  
| `runtime.schedule` | Boucle principale du scheduler — choisit le prochain G à exécuter |  
| `runtime.findRunnable` | Cherche une goroutine prête (file locale, file globale, vol de travail) |  
| `runtime.execute` | Bascule le contexte vers un G sélectionné |  
| `runtime.gogo` | Restaure les registres depuis `g.sched` et saute au PC sauvegardé |  
| `runtime.gosave` | Sauvegarde les registres du G courant dans `g.sched` |  
| `runtime.mcall` | Bascule vers la pile système du M pour exécuter une fonction runtime |  
| `runtime.park_m` | Met un G en attente (blocking sur channel, mutex, I/O…) |  
| `runtime.gopark` | Interface haut niveau pour `park_m` — appelée par les channels |  
| `runtime.goready` | Réveille un G parqué et le remet dans la file runnable |  
| `runtime.newproc` | Crée un nouveau G (correspond à `go func()`) |  
| `runtime.Goexit` | Termine la goroutine courante proprement |

### Les points de préemption

Depuis Go 1.14, le scheduler supporte la **préemption asynchrone** via des signaux (SIGURG sur Linux). Avant cela, la préemption n'était que coopérative — aux points de vérification de pile et aux appels de fonctions. En RE, cela se traduit par :

- des handlers de signal `runtime.sighandler` qui vérifient si le signal est un SIGURG de préemption,  
- des champs dans la structure `g` (`preempt`, `preemptStop`) qui contrôlent quand le G peut être interrompu.

Pour l'analyste, le point important est que les goroutines ne sont **pas** des threads OS. Un `strace` ne vous montrera que les quelques threads OS (les M), pas les goroutines individuelles. Pour tracer l'exécution d'une goroutine spécifique, vous devrez poser des breakpoints sur le code de la goroutine elle-même, pas sur les mécanismes du scheduler.

---

## Le Garbage Collector vu depuis le RE

### Architecture tri-color concurrent

Le GC de Go utilise un algorithme de marquage tri-color (blanc/gris/noir), concurrent et non-générationnel. Les détails algorithmiques dépassent le scope du RE, mais les impacts sur l'analyse sont concrets.

### Fonctions GC fréquemment rencontrées

| Fonction | Rôle |  
|---|---|  
| `runtime.gcStart` | Déclenche un cycle de GC |  
| `runtime.gcMarkDone` | Fin de la phase de marquage |  
| `runtime.gcSweep` | Phase de balayage — libère les objets non marqués |  
| `runtime.gcBgMarkWorker` | Worker de marquage en arrière-plan (goroutine dédiée) |  
| `runtime.mallocgc` | Point d'allocation principal — **chaque `make`, `new`, ou allocation implicite passe par ici** |  
| `runtime.newobject` | Wrapper autour de `mallocgc` pour les allocations simples |

### Write barriers

Le GC concurrent nécessite des **write barriers** — du code injecté par le compilateur à chaque écriture de pointeur. En assembleur, vous verrez fréquemment :

```asm
; Écriture d'un pointeur avec write barrier
LEA     RDI, [destination]  
MOV     RSI, [source_pointeur]  
CALL    runtime.gcWriteBarrier  
```

Ou, dans les versions récentes, une séquence inline plus rapide qui teste un flag du P courant avant de décider si la barrière est active.

Ces write barriers apparaissent dans **toutes** les fonctions qui manipulent des pointeurs, y compris votre code métier. Elles ajoutent du bruit significatif au désassemblage. Apprenez à les reconnaître pour les filtrer mentalement :

- Un `CALL runtime.gcWriteBarrier` (ou `runtime.gcWriteBarrierN` pour les variantes) au milieu d'une fonction est presque toujours une write barrier du GC.  
- Les variantes `runtime.gcWriteBarrier1` à `runtime.gcWriteBarrier8` gèrent des tailles de buffer différentes.

> 💡 **Astuce RE** : si vous voyez une fonction Ghidra dont le pseudo-code semble anormalement complexe avec beaucoup d'appels `runtime.gcWriteBarrier*`, ne vous inquiétez pas. Le code réel sous-jacent est souvent bien plus simple — ces appels ne sont que de l'instrumentation GC.

### L'allocateur mémoire

Go n'utilise pas `malloc`/`free` de la libc. Son allocateur est intégré au runtime et organisé en trois niveaux :

1. **mheap** — l'arène globale, gère les pages mémoire obtenues via `mmap`.  
2. **mcentral** — cache partagé par classe de taille.  
3. **mcache** — cache local par P (processeur logique), sans verrou.

Pour les petites allocations (≤ 32 Ko), le chemin rapide passe par `mcache` sans aucun lock, ce qui rend l'allocation très rapide. En RE, la conséquence est que vous ne verrez quasiment jamais de `mmap` ou `brk` dans `strace` pendant l'exécution normale — le runtime pré-alloue de grandes arènes et les découpe en interne.

Le point d'entrée principal est `runtime.mallocgc`. **Tout** passe par cette fonction : les `make([]byte, n)`, les `new(MyStruct)`, les allocations implicites lors de conversions d'interface, les closures échappant au scope local.

> 💡 **Astuce RE** : poser un breakpoint conditionnel sur `runtime.mallocgc` et filtrer par taille d'allocation est un moyen efficace de repérer les allocations de structures de données importantes dans un programme Go. Le premier argument (dans `RAX` avec l'ABI registre) est la taille de l'objet à allouer.

---

## Impact sur l'analyse dynamique

### Threads vs goroutines dans GDB

Quand vous lancez un binaire Go sous GDB, `info threads` vous montrera quelques threads (typiquement 4 à 8, correspondant aux M). Mais le programme peut avoir des centaines de goroutines actives. GDB n'a aucune connaissance native des goroutines.

Pour lister les goroutines, vous avez plusieurs options :

- **Delve** (`dlv`), le débogueur natif de Go, qui comprend les goroutines (`goroutines`, `goroutine <id>` pour changer de contexte). Mais Delve suppose l'accès aux symboles Go et n'est pas vraiment conçu pour le RE de binaires strippés.  
- **GDB avec un script custom** qui parcourt la liste chaînée `runtime.allgs` (slice de tous les G) et affiche l'état et le PC de chaque goroutine.  
- **GEF / pwndbg** n'ont pas de support Go natif, mais vous pouvez inspecter `R14` (pointeur vers le G courant) et naviguer manuellement dans les structures.

### Signaux et interférences

Le runtime Go intercepte de nombreux signaux pour ses propres besoins :

- `SIGURG` pour la préemption asynchrone,  
- `SIGSEGV` et `SIGBUS` pour la détection des piles guard pages,  
- `SIGPROF` pour le profiling CPU intégré (`pprof`).

Lors d'une session GDB, cela peut provoquer des arrêts intempestifs. Pensez à configurer GDB pour ignorer ces signaux :

```
handle SIGURG nostop noprint  
handle SIGPIPE nostop noprint  
```

---

## Reconnaître un binaire Go sans symboles

Même face à un binaire strippé et inconnu, plusieurs indices trahissent un binaire Go :

1. **La taille** — un exécutable de plusieurs mégaoctets pour une fonctionnalité simple est suspect.  
2. **Les chaînes caractéristiques** — `runtime.`, `GOROOT`, `gopclntab`, `go.buildid`, `go1.` apparaissent dans `strings` même après stripping. Cherchez en particulier `go.buildid` et `runtime.main`.  
3. **Le préambule de pile** — le pattern `CMP RSP, [R14+0x10]; JBE` répété dans la majorité des fonctions.  
4. **Les sections ELF** — la présence de sections `.gopclntab`, `.go.buildid`, `.noptrdata`, `.noptrbss` est un indicateur fort. Vérifiez avec `readelf -S`.  
5. **L'absence de dépendances dynamiques** — `ldd` retourne `not a dynamic executable` ou ne liste que quelques dépendances minimales.  
6. **L'entry point** — `readelf -h` affichera un entry point nommé `_rt0_amd64_linux` (ou l'équivalent pour l'architecture cible).  
7. **L'entropie des sections** — la section `.rodata` d'un binaire Go est souvent anormalement grande (elle contient toutes les chaînes, les tables de types et les métadonnées du runtime).

> 💡 **Astuce RE** : la commande rapide `strings binaire | grep -c 'runtime\.'` donne un bon indicateur. Un binaire C typique retournera 0. Un binaire Go retournera plusieurs centaines, voire milliers d'occurrences.

---

## Ce qu'il faut retenir pour la suite

Le runtime Go est omniprésent dans le binaire, mais il ne faut pas le subir. L'approche efficace en RE :

1. **Identifiez le runtime et ignorez-le.** Filtrez les fonctions `runtime.*`, `internal/*`, `sync.*`, etc. pour vous concentrer sur les packages applicatifs (`main.*` et les packages métier).  
2. **Reconnaissez les patterns récurrents.** Le préambule de vérification de pile, les write barriers du GC et les appels au scheduler sont du bruit : apprenez à les sauter mentalement.  
3. **Exploitez les structures du runtime.** Le pointeur `g` dans `R14`, la table `gopclntab`, et les structures de type offrent des informations précieuses que le runtime C ne fournit jamais.  
4. **Adaptez vos outils.** GDB seul n'est pas idéal pour Go. Delve, des scripts GDB custom, ou des plugins Ghidra spécifiques (section 34.4) changeront radicalement votre efficacité.

Les sections suivantes s'appuient sur cette compréhension du runtime pour aborder les conventions d'appel (34.2), les structures de données en mémoire (34.3) et la récupération des symboles (34.4).

⏭️ [Convention d'appel Go (stack-based puis register-based depuis Go 1.17)](/34-re-go/02-convention-appel.md)
