🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 15.2 — AFL++ : installation, instrumentation et premier run sur une appli GCC

> 🔗 **Prérequis** : Chapitre 2 (chaîne de compilation GNU, flags GCC), Chapitre 4 (environnement de travail), Section 15.1 (positionnement du fuzzing en RE)

---

## AFL++ en bref

AFL++ (*American Fuzzy Lop plus plus*) est le successeur communautaire d'AFL, le fuzzer grey-box qui a révolutionné la recherche de vulnérabilités à partir de 2013. Le projet original, créé par Michał Zalewski (lcamtuf) chez Google, n'est plus maintenu depuis 2017. AFL++ a pris le relais en intégrant des années de recherche académique et d'améliorations pratiques : nouveaux algorithmes de mutation, support étendu des modes d'instrumentation, intégration native des sanitizers, et performances significativement supérieures.

Pour un reverse engineer, AFL++ présente plusieurs avantages décisifs :

- **Instrumentation à la compilation avec GCC** — puisque nos binaires d'entraînement sont fournis avec leurs sources, on peut les recompiler avec `afl-gcc` ou `afl-clang-fast` pour obtenir une instrumentation quasi gratuite en termes de performance.  
- **Mode QEMU** (`-Q`) — pour les binaires dont on n'a pas les sources, AFL++ peut instrumenter à l'exécution via l'émulation QEMU. Plus lent, mais indispensable en RE réel.  
- **Mode Frida** — alternative au mode QEMU, utilisant Frida (cf. Chapitre 13) comme moteur d'instrumentation runtime. Souvent plus rapide que QEMU sur des cibles Linux x86-64.  
- **Interface textuelle en temps réel** — le tableau de bord d'AFL++ affiche en continu la couverture atteinte, le nombre de crashs, la vitesse d'exécution et l'état de l'exploration. C'est un retour visuel immédiat sur « ce que le fuzzer a compris » du binaire.

---

## Installation d'AFL++

### Depuis les paquets (Debian/Ubuntu)

La méthode la plus rapide sur Debian 12+ ou Ubuntu 22.04+ :

```bash
$ sudo apt update
$ sudo apt install -y afl++
```

Cette installation fournit les binaires principaux : `afl-fuzz`, `afl-gcc`, `afl-clang-fast`, `afl-tmin`, `afl-cmin`, `afl-showmap`, ainsi que le mode QEMU si le paquet `afl++-qemu` est disponible.

Vérifiez l'installation :

```bash
$ afl-fuzz --version
afl-fuzz++4.x (quelque chose)
```

> ⚠️ **Attention** — Les paquets des dépôts officiels ont parfois une ou deux versions de retard. Pour un usage en production ou en CTF, préférez la compilation depuis les sources (ci-dessous). Pour suivre ce chapitre, la version des dépôts suffit.

### Depuis les sources (recommandé pour la dernière version)

```bash
$ sudo apt install -y build-essential python3-dev automake cmake git flex bison \
    libglib2.0-dev libpixman-1-dev python3-setuptools cargo libgtk-3-dev \
    lld llvm llvm-dev clang
$ git clone https://github.com/AFLplusplus/AFLplusplus.git
$ cd AFLplusplus
$ make distrib    # compile tout, y compris le support QEMU et Frida
$ sudo make install
```

La cible `make distrib` est plus longue que `make all` car elle compile également les modes QEMU et Frida. Si vous n'avez pas besoin du mode QEMU immédiatement, `make all` suffit et sera plus rapide.

Vérifiez que les composants clés sont présents :

```bash
$ which afl-fuzz afl-gcc afl-clang-fast afl-showmap afl-tmin afl-cmin
```

### Sur Kali Linux

Kali intègre AFL++ dans ses dépôts. Un simple `sudo apt install afl++` suffit généralement. Vérifiez la version avec `afl-fuzz --version`.

---

## L'instrumentation : le cœur du grey-box fuzzing

Avant de lancer AFL++, il faut comprendre **ce que fait l'instrumentation** et **pourquoi elle est indispensable**.

### Le problème sans instrumentation

Si on exécute un binaire classique avec des inputs aléatoires, tout ce qu'on observe de l'extérieur, c'est : « le programme a-t-il crashé, oui ou non ? ». On n'a aucune idée de **quels chemins** chaque input a parcourus à l'intérieur du code. Deux inputs très différents peuvent produire le même résultat visible (pas de crash) tout en ayant traversé des branches complètement distinctes. Sans cette information, le fuzzer ne peut pas apprendre — il mute à l'aveugle, ce qui est le régime du black-box.

### Ce que l'instrumentation ajoute

L'instrumentation consiste à injecter, au moment de la compilation, de petits morceaux de code aux points de branchement du programme. À chaque transition d'un bloc de base vers un autre, le code instrumenté met à jour une **bitmap de couverture** partagée avec le fuzzer. Cette bitmap est un tableau en mémoire partagée (`shared memory`) où chaque cellule correspond à une paire (bloc source → bloc destination).

Concrètement, à chaque branchement, le code injecté fait quelque chose comme :

```
shared_mem[hash(bloc_précédent XOR bloc_courant)] += 1
```

Après chaque exécution, AFL++ compare la bitmap résultante avec celles des exécutions précédentes. Si de nouvelles cellules ont été activées (ou si leurs compteurs ont changé de manière significative), cela signifie que cet input a déclenché un **nouveau comportement** — et il mérite d'être conservé dans le corpus.

> 💡 **Pour le RE** — Cette bitmap est exactement la « carte du binaire » mentionnée en section 15.1. Chaque cellule activée correspond à une transition entre deux blocs de base que le fuzzer a réussi à provoquer. À la fin d'une campagne de fuzzing, cette carte vous indique quelles portions du code ont été atteintes — et surtout, lesquelles ne l'ont pas été.

### Les modes d'instrumentation d'AFL++

AFL++ propose plusieurs moteurs d'instrumentation, adaptés à différents scénarios :

| Mode | Commande | Quand l'utiliser | Performance |  
|------|----------|------------------|-------------|  
| `afl-gcc` | Compilation avec `afl-gcc` / `afl-g++` | Sources disponibles, chaîne GCC existante | Bonne |  
| `afl-clang-fast` | Compilation avec `afl-clang-fast` / `afl-clang-fast++` | Sources disponibles, meilleure instrumentation LLVM | Excellente |  
| QEMU | Flag `-Q` à `afl-fuzz` | Pas de sources, binaire précompilé | Modérée (~2-5× plus lent) |  
| Frida | Flag `-O` à `afl-fuzz` | Pas de sources, alternative à QEMU | Bonne (~1.5-3× plus lent) |  
| Unicorn | Via `afl-unicorn` | Fragments de code isolés (firmware) | Variable |

Pour ce chapitre, nous utiliserons principalement **`afl-gcc`** puisque nos binaires d'entraînement sont fournis avec leurs sources C. C'est le mode le plus simple à mettre en place et celui qui colle à notre contexte de formation (chaîne GNU).

Si vous souhaitez des performances optimales et que vous avez Clang/LLVM installé, `afl-clang-fast` est le choix supérieur : son instrumentation au niveau LLVM IR est plus fine et plus rapide que l'instrumentation assembleur d'`afl-gcc`. Mais les deux fonctionnent parfaitement pour notre usage.

---

## Compiler un binaire avec instrumentation AFL++

Prenons un exemple concret. Supposons que nous avons un petit programme C qui lit un fichier et le parse — exactement le type de cible idéale pour le fuzzing :

```c
// simple_parser.c — Parseur minimaliste pour démonstration
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parse_input(const char *data, size_t len) {
    if (len < 4) return -1;

    // Vérifie un magic number
    if (data[0] != 'R' || data[1] != 'E') return -1;

    // Vérifie un champ version
    unsigned char version = data[2];
    if (version == 1) {
        // Chemin v1 : traitement basique
        if (len < 8) return -1;
        int value = *(int *)(data + 4);
        if (value > 1000) {
            // Chemin rarement atteint
            printf("Mode étendu activé\n");
        }
    } else if (version == 2) {
        // Chemin v2 : traitement avancé
        if (len < 16) return -1;
        // ... logique plus complexe ...
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size);
    if (!buf) { fclose(f); return 1; }

    fread(buf, 1, size, f);
    fclose(f);

    int result = parse_input(buf, size);
    free(buf);

    return result;
}
```

### Compilation standard (sans instrumentation)

Pour référence, voici comment on compilerait normalement :

```bash
$ gcc -O0 -g -o simple_parser simple_parser.c
```

Ce binaire est parfaitement fonctionnel, mais AFL++ ne pourra pas observer sa couverture interne (sauf en mode QEMU/Frida).

### Compilation avec `afl-gcc`

Il suffit de remplacer `gcc` par `afl-gcc` :

```bash
$ afl-gcc -O0 -g -o simple_parser_afl simple_parser.c
```

Pendant la compilation, vous verrez un message d'AFL++ confirmant l'instrumentation :

```
[+] Instrumented X locations (non-hardened mode, ratio 100%).
```

Le nombre `X` correspond au nombre de points de branchement instrumentés. C'est un bon indicateur de la « taille » du programme du point de vue du fuzzer.

### Compilation avec `afl-clang-fast` (alternative LLVM)

```bash
$ afl-clang-fast -O0 -g -o simple_parser_afl simple_parser.c
```

Le message de confirmation sera similaire, mais l'instrumentation sous-jacente est différente (passage LLVM IR vs insertion assembleur). En pratique, les résultats de fuzzing sont comparables ; `afl-clang-fast` est simplement un peu plus rapide.

### Ajouter les sanitizers à la compilation

L'une des combinaisons les plus puissantes pour le RE est de coupler l'instrumentation AFL++ avec les sanitizers (cf. Chapitre 14). AddressSanitizer (ASan) détecte les accès mémoire invalides, les buffer overflows, les use-after-free — des bugs qui ne provoquent pas toujours un crash visible sans sanitizer.

```bash
$ AFL_USE_ASAN=1 afl-gcc -O0 -g -o simple_parser_asan simple_parser.c
```

Ou de manière équivalente :

```bash
$ afl-gcc -O0 -g -fsanitize=address -o simple_parser_asan simple_parser.c
```

Avec UndefinedBehaviorSanitizer en complément :

```bash
$ AFL_USE_ASAN=1 AFL_USE_UBSAN=1 afl-gcc -O0 -g -o simple_parser_asan_ubsan simple_parser.c
```

> ⚠️ **Attention** — ASan augmente significativement la consommation mémoire (environ 2 à 3×) et réduit un peu la vitesse d'exécution. Pour des sessions de fuzzing longues sur des programmes gourmands en mémoire, il peut être préférable de fuzzer d'abord *sans* ASan pour la vitesse, puis de rejouer les inputs intéressants sur un build ASan pour détecter les bugs subtils. On appelle cette stratégie le *triage ASan*.

### Adapter un Makefile existant

Les binaires de la formation utilisent des `Makefile` dédiés qui incluent une cible `fuzz` préconfigurée pour AFL++ :

```bash
$ cd binaries/ch15-fileformat/
$ make clean
$ make fuzz
```

Si le `Makefile` d'un projet tiers n'a pas de cible de fuzzing, la technique générale est de surcharger la variable `CC` (ou `CXX` pour du C++) :

```bash
$ make clean
$ make CC=afl-gcc CFLAGS="-O0 -g"
```

Cela fonctionne tant que le `Makefile` utilise `$(CC)` et `$(CFLAGS)` dans ses règles de compilation — ce qui est la convention standard. Pour un projet C++ :

```bash
$ make CXX=afl-g++ CXXFLAGS="-O0 -g"
```

---

## Préparer une campagne de fuzzing

Avant de lancer `afl-fuzz`, trois éléments doivent être en place : un **binaire instrumenté**, un **corpus initial**, et une **structure de répertoires**.

### Le corpus initial (seed corpus)

Le corpus initial est un ensemble de fichiers d'entrée valides (ou presque valides) que le fuzzer utilisera comme point de départ pour ses mutations. La qualité du corpus influence directement la vitesse à laquelle le fuzzer atteindra les couches profondes du code.

Pour notre `simple_parser`, créons un répertoire `in/` avec quelques seeds :

```bash
$ mkdir in
$ echo -ne 'RE\x01\x00AAAA' > in/seed_v1.bin    # Magic "RE", version 1, 8 octets
$ echo -ne 'RE\x02\x00AAAAAAAAAAAAAAAA' > in/seed_v2.bin  # Magic "RE", version 2, 18 octets
$ echo -ne 'RE\x00\x00' > in/seed_v0.bin          # Magic "RE", version inconnue
```

> 💡 **D'où viennent ces seeds en contexte RE ?** — En pratique, le corpus initial est construit à partir des informations récoltées pendant le triage (Chapitre 5) et l'analyse statique :  
> - `strings` sur le binaire peut révéler des chaînes de format, des magic bytes, des messages d'erreur qui indiquent le format attendu.  
> - L'analyse dans Ghidra des fonctions de parsing montre les constantes comparées en début de traitement.  
> - Si le binaire est un client/serveur, les captures réseau (Wireshark) fournissent des exemples d'inputs réels.  
> - Au pire, un fichier d'un seul octet `\x00` suffit — le fuzzer finira par trouver, mais ce sera beaucoup plus lent.

### La structure de répertoires

AFL++ attend au minimum un répertoire d'entrée (`-i`) et un répertoire de sortie (`-o`) :

```
fuzzing_session/
├── in/               ← Corpus initial (seeds)
│   ├── seed_v1.bin
│   ├── seed_v2.bin
│   └── seed_v0.bin
├── out/              ← Créé automatiquement par AFL++
│   ├── queue/        ← Inputs qui ont découvert de nouveaux chemins
│   ├── crashes/      ← Inputs qui ont provoqué des crashs
│   ├── hangs/        ← Inputs qui ont provoqué des timeouts
│   └── ...
└── simple_parser_afl ← Binaire instrumenté
```

Le répertoire `out/` est créé et géré par AFL++. Ne le créez pas manuellement avant le premier lancement — ou si vous le faites, assurez-vous qu'il est vide.

---

## Configuration système préalable

AFL++ a besoin de quelques ajustements système pour fonctionner de manière optimale. Sans ces ajustements, il fonctionnera quand même mais affichera des avertissements et sera moins performant.

### Désactiver le CPU frequency scaling

AFL++ recommande de fixer les CPU en mode performance :

```bash
$ echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

> 💡 **Dans une VM** — Ce réglage n'a généralement pas d'effet dans une machine virtuelle. Vous pouvez ignorer l'avertissement d'AFL++ à ce sujet si vous travaillez dans la VM du Chapitre 4.

### Configurer le comportement du noyau face aux crashs

Par défaut, Linux peut envoyer les core dumps vers un gestionnaire externe (comme `apport` sur Ubuntu), ce qui ralentit le fuzzing. AFL++ veut gérer les crashs lui-même :

```bash
$ echo core | sudo tee /proc/sys/kernel/core_pattern
```

Si vous ne faites pas cet ajustement, AFL++ refusera de démarrer et affichera un message d'erreur explicite vous demandant de le faire.

---

## Lancer le premier run

Tout est en place. Lançons AFL++ :

```bash
$ afl-fuzz -i in -o out -- ./simple_parser_afl @@
```

Décomposons cette commande :

| Élément | Rôle |  
|---------|------|  
| `afl-fuzz` | Le fuzzer principal |  
| `-i in` | Répertoire du corpus initial |  
| `-o out` | Répertoire de sortie (résultats) |  
| `--` | Séparateur entre les options d'AFL++ et la commande cible |  
| `./simple_parser_afl` | Le binaire instrumenté à fuzzer |  
| `@@` | Placeholder remplacé par le chemin du fichier d'input généré par AFL++ |

Le `@@` est crucial : il indique à AFL++ que le programme cible attend un **nom de fichier** en argument. AFL++ écrit chaque input muté dans un fichier temporaire et remplace `@@` par le chemin de ce fichier avant chaque exécution.

> 💡 **Si le programme lit depuis stdin** — Certains programmes lisent leur input depuis l'entrée standard plutôt que depuis un fichier. Dans ce cas, omettez `@@` :  
> ```bash  
> $ afl-fuzz -i in -o out -- ./my_program  
> ```  
> AFL++ enverra les données directement sur stdin du programme.

### L'interface d'AFL++ : lire le tableau de bord

Dès le lancement, AFL++ affiche un tableau de bord en mode texte qui se met à jour en temps réel :

```
                        american fuzzy lop ++4.09a
┌─ process timing ────────────────────────────────────┬─ overall results ───┐
│        run time : 0 days, 0 hrs, 2 min, 37 sec      │  cycles done : 14   │
│   last new find : 0 days, 0 hrs, 0 min, 3 sec       │ corpus count : 23   │
│ last saved crash : 0 days, 0 hrs, 1 min, 12 sec     │saved crashes : 3    │
│  last saved hang : none seen yet                    │  saved hangs : 0    │
├─ cycle progress ───────────────────┬─ map coverage ─┴─────────────────────┤
│  now processing : 17.2 (73.9%)     │    map density : 1.42% / 2.87%       │
│  runs timed out : 0 (0.00%)        │ count coverage : 4.31 bits/tuple     │
├─ stage progress ───────────────────┼─ findings in depth ──────────────────┤
│  now trying : havoc                │ favored items : 8 (34.8%)            │
│ stage execs : 1247/2048 (60.9%)    │  new edges on : 12 (52.2%)           │
│ total execs : 87.3k                │ total crashes : 7 (3 saved)          │
│  exec speed : 2341/sec             │  total tmouts : 0 (0 saved)          │
├─ fuzzing strategy yields ──────────┴──────────────────────────────────────┤
│ ...                                                                       │
└───────────────────────────────────────────────────────────────────────────┘
```

Les indicateurs essentiels pour le reverse engineer :

**`corpus count`** — Le nombre d'inputs dans la file d'attente. Chaque input représente un chemin distinct à travers le binaire. Si ce nombre augmente régulièrement, le fuzzer continue de découvrir de nouvelles parties du code. S'il stagne, le fuzzer a probablement exploré tout ce qu'il peut atteindre avec sa stratégie actuelle — c'est peut-être le moment d'enrichir le dictionnaire ou d'affiner le corpus (section 15.6).

**`saved crashes`** — Le nombre d'inputs ayant provoqué un crash. Chaque crash est un point d'entrée potentiel pour l'analyse avec GDB (section 15.4). AFL++ déduplique les crashs par chemin d'exécution : deux inputs qui crashent au même endroit de la même façon ne comptent qu'une fois.

**`map density`** — Le pourcentage de la bitmap de couverture qui a été activé. Les deux valeurs indiquent la couverture actuelle et la couverture cumulée maximale. Sur un petit programme, 2-5% est courant ; sur un programme plus gros, même 0.5% peut représenter une couverture significative. Ce chiffre est relatif à la taille de la bitmap (64 Ko par défaut), pas au nombre total de branches du programme.

**`exec speed`** — Le nombre d'exécutions par seconde. Pour un programme simple lu depuis un fichier, on attend typiquement entre 1 000 et 10 000 exec/s. En dessous de 100 exec/s, le fuzzing sera très lent et il faut investiguer (programme trop lent, I/O disque, absence d'instrumentation). Au-dessus de 10 000, les conditions sont excellentes.

**`cycles done`** — Le nombre de fois que le fuzzer a parcouru l'intégralité de son corpus actuel. Après le premier cycle complet, les découvertes deviennent plus rares. Si ce compteur atteint plusieurs dizaines sans nouveau crash ni nouvel input, la campagne a probablement convergé.

**`last new find`** — Le temps écoulé depuis la dernière découverte d'un nouveau chemin. Si ce compteur dépasse 30 minutes à 1 heure sur un programme simple, la campagne est probablement arrivée en fin de vie utile.

---

## Arrêter et reprendre une session

AFL++ peut être interrompu à tout moment avec `Ctrl+C`. L'état complet de la campagne est sauvegardé dans le répertoire de sortie. Pour reprendre :

```bash
$ afl-fuzz -i - -o out -- ./simple_parser_afl @@
```

Le `-i -` (tiret) indique à AFL++ de reprendre depuis l'état existant dans `out/` plutôt que de repartir d'un corpus initial. Les inputs déjà découverts, les crashs, et la bitmap de couverture sont restaurés.

> ⚠️ **Attention** — Ne relancez **jamais** avec `-i in` sur un répertoire de sortie existant contenant déjà des résultats. AFL++ refusera pour éviter d'écraser vos découvertes. Si vous voulez repartir de zéro, supprimez d'abord le répertoire `out/`.

---

## Fuzzing parallèle sur plusieurs cœurs

Par défaut, `afl-fuzz` utilise un seul cœur CPU. Pour exploiter une machine multi-cœurs, AFL++ propose un mode maître/esclave (renommé *main/secondary* dans les versions récentes) :

```bash
# Terminal 1 — Instance principale (déterministe)
$ afl-fuzz -i in -o out -M main -- ./simple_parser_afl @@

# Terminal 2 — Instance secondaire (aléatoire)
$ afl-fuzz -i in -o out -S secondary01 -- ./simple_parser_afl @@

# Terminal 3 — Autre instance secondaire
$ afl-fuzz -i in -o out -S secondary02 -- ./simple_parser_afl @@
```

Toutes les instances partagent le même répertoire de sortie `out/` et synchronisent automatiquement leurs découvertes. L'instance `-M` effectue des mutations déterministes (bit flips systématiques, insertions de valeurs intéressantes), tandis que les instances `-S` font des mutations aléatoires (*havoc*). La combinaison couvre plus de terrain que N instances identiques.

En règle générale, lancez **une instance `-M` et N-1 instances `-S`**, où N est le nombre de cœurs disponibles. Sur la VM recommandée au Chapitre 4 (2 à 4 cœurs), deux à trois instances sont un bon compromis.

---

## Fuzzing d'un binaire sans sources (mode QEMU)

En contexte de RE réel, on n'a pas toujours les sources. AFL++ peut alors instrumenter le binaire **au runtime** via l'émulation QEMU :

```bash
# Compiler le binaire normalement (pas avec afl-gcc)
$ gcc -O2 -o target_nosrc target.c
$ strip target_nosrc

# Fuzzer en mode QEMU
$ afl-fuzz -Q -i in -o out -- ./target_nosrc @@
```

Le flag `-Q` active le mode QEMU user-mode. AFL++ exécute le binaire dans un émulateur qui instrumente chaque bloc de base à la volée. C'est transparent pour le programme cible — il ne sait pas qu'il est émulé.

Le coût en performance est significatif : attendez-vous à une vitesse 2 à 5 fois inférieure par rapport à l'instrumentation à la compilation. Sur un programme rapide (10 000 exec/s instrumenté), le mode QEMU donnera typiquement 2 000 à 5 000 exec/s — ce qui reste largement exploitable.

L'alternative Frida (`-O` au lieu de `-Q`) est parfois plus rapide sur des binaires x86-64 et ne nécessite pas la compilation du support QEMU :

```bash
$ afl-fuzz -O -i in -o out -- ./target_nosrc @@
```

> 💡 **Stratégie en RE** — Si vous avez les sources, utilisez toujours l'instrumentation à la compilation (c'est notre cas dans cette formation). Réservez QEMU/Frida pour les cibles fermées. Et si vous disposez des sources pour *une partie* du projet (par exemple la bibliothèque de parsing mais pas le programme principal), vous pouvez écrire un **harness** qui appelle directement la fonction de parsing — nous verrons cette approche avec libFuzzer en section 15.3.

---

## Premiers résultats : explorer le répertoire de sortie

Après quelques minutes de fuzzing, le répertoire `out/` contient les découvertes d'AFL++ :

```bash
$ ls out/default/
crashes/  hangs/  queue/  cmdline  fuzz_bitmap  fuzzer_stats  plot_data
```

### `queue/` — Le corpus découvert

```bash
$ ls out/default/queue/
id:000000,time:0,execs:0,orig:seed_v1.bin  
id:000001,time:0,execs:0,orig:seed_v2.bin  
id:000002,time:0,execs:0,orig:seed_v0.bin  
id:000003,time:137,execs:4821,op:havoc,rep:2,+cov  
id:000004,time:298,execs:11037,op:havoc,rep:4,+cov  
...
```

Chaque fichier dans `queue/` est un input qui a déclenché un nouveau chemin. Le nom encode des métadonnées : le timestamp, le nombre d'exécutions au moment de la découverte, l'opération de mutation qui l'a produit, et `+cov` indique qu'il a ajouté de la couverture.

Pour le RE, ces fichiers sont précieux : en les examinant avec `xxd` ou ImHex, on peut observer comment le fuzzer a « appris » progressivement le format attendu par le parseur. Les premiers inputs ressemblent aux seeds ; les derniers peuvent être radicalement différents, reflétant des chemins profonds dans la logique du programme.

### `crashes/` — Les inputs qui font crasher le programme

```bash
$ ls out/default/crashes/
README.txt  
id:000000,sig:11,src:000003,time:1842,execs:52341,op:havoc,rep:8  
id:000001,sig:06,src:000004,time:3107,execs:89023,op:havoc,rep:2  
```

Chaque fichier est un input qui a provoqué la terminaison du programme par un signal. `sig:11` est un SIGSEGV (segfault), `sig:06` est un SIGABRT (typiquement déclenché par ASan ou un `assert`). Ces fichiers sont le point de départ de l'analyse détaillée en section 15.4.

Pour reproduire un crash :

```bash
$ ./simple_parser_afl out/default/crashes/id:000000,sig:11,*
Segmentation fault
```

Ou avec GDB pour une analyse approfondie :

```bash
$ gdb -q ./simple_parser_afl
(gdb) run out/default/crashes/id:000000,sig:11,src:000003,time:1842,execs:52341,op:havoc,rep:8
```

### `hangs/` — Les inputs qui provoquent un timeout

AFL++ considère qu'un programme « hang » s'il dépasse le timeout configuré (par défaut, déterminé automatiquement à partir du temps d'exécution des seeds, typiquement quelques centaines de millisecondes). Les hangs indiquent souvent des boucles infinies ou des attentes bloquantes — une information utile pour comprendre les conditions aux limites du parseur.

### `fuzzer_stats` — Les statistiques en format texte

```bash
$ cat out/default/fuzzer_stats
start_time        : 1711000000  
last_update       : 1711000157  
run_time          : 157  
execs_done        : 87342  
execs_per_sec     : 2341.00  
corpus_count      : 23  
saved_crashes     : 3  
...
```

Ce fichier est utile pour le scripting : on peut monitorer une campagne de fuzzing depuis un script externe en lisant ces statistiques.

---

## Options utiles d'`afl-fuzz`

Quelques options fréquemment utilisées, au-delà du lancement de base :

| Option | Effet |  
|--------|-------|  
| `-t 1000` | Fixe le timeout à 1000 ms (utile si le programme est lent) |  
| `-m none` | Désactive la limite mémoire (nécessaire avec ASan, qui consomme beaucoup de mémoire virtuelle) |  
| `-x dict.txt` | Fournit un dictionnaire de tokens (cf. section 15.6) |  
| `-p exploit` | Sélectionne le planning de puissance `exploit` (favorise l'exploitation des chemins connus vs l'exploration de nouveaux — utile en fin de campagne) |  
| `-D` | Active les mutations déterministes même en mode secondaire |

Pour une session avec ASan, la commande complète typique est :

```bash
$ afl-fuzz -i in -o out -m none -t 5000 -- ./simple_parser_asan @@
```

Le `-m none` est indispensable car ASan utilise une très grande quantité de mémoire virtuelle (via `mmap`) que la limite par défaut d'AFL++ bloquerait.

---

## Mise en pratique : premier run sur `ch15-keygenme`

Les exemples précédents utilisaient un `simple_parser.c` fictif pour illustrer les concepts. Passons à un vrai binaire de la formation : le keygenme du Chapitre 21. Ce programme lit une clé depuis `argv[1]` (ou stdin selon la variante) et vérifie sa validité — un cas d'usage classique pour le fuzzing.

### Compilation instrumentée du keygenme

```bash
$ cd binaries/ch15-keygenme/
$ make clean
$ make fuzz
```

La cible `fuzz` du `Makefile` compile directement les variantes instrumentées AFL++ (`keygenme_afl` et `keygenme_afl_asan`) en utilisant `afl-gcc`. Si `afl-gcc` n'est pas dans le PATH standard, on peut surcharger :

```bash
$ make fuzz AFL_CC=/chemin/vers/afl-gcc
```

Alternativement, on peut compiler directement sans passer par le Makefile :

```bash
$ afl-gcc -O0 -g -o keygenme_afl keygenme.c
```

Vérification :

```bash
$ echo -n "AAAA" | afl-showmap -o /dev/stdout -- ./keygenme_afl 2>/dev/null | wc -l
```

Le nombre d'edges doit être supérieur à zéro.

### Corpus initial et dictionnaire

Le keygenme attend une chaîne de caractères en argument. On prépare un corpus minimal :

```bash
$ mkdir in_keygen
$ echo -n "AAAA" > in_keygen/seed1.bin
$ echo -n "0000" > in_keygen/seed2.bin
$ echo -n "ABCD1234" > in_keygen/seed3.bin
```

Un petit dictionnaire avec des caractères typiques de clés de licence :

```bash
$ cat > dict_keygen.txt << 'EOF'
digits="0123456789"  
dash="-"  
upper="ABCDEFGHIJKLMNOPQRSTUVWXYZ"  
hex_prefix="0x"  
EOF  
```

### Lancement

Le keygenme accepte une clé soit via `argv[1]` (mode direct), soit via stdin (mode interactif, quand aucun argument n'est fourni). Pour le fuzzing avec AFL++, on utilise le **mode stdin** — c'est-à-dire sans `@@` — car AFL++ enverra le contenu de chaque input muté directement sur l'entrée standard du programme :

```bash
$ afl-fuzz -i in_keygen -o out_keygen -x dict_keygen.txt \
    -- ./keygenme_afl
```

> 💡 **Pourquoi pas `@@` ici ?** — Le marqueur `@@` indique à AFL++ de remplacer par un **chemin de fichier**. C'est adapté aux programmes qui *ouvrent et lisent* un fichier (comme le parseur `ch25-fileformat`). Ici, le keygenme utilise `argv[1]` comme la **chaîne-clé elle-même**, pas comme un nom de fichier — passer un chemin comme `/tmp/.cur_input` en guise de clé n'aurait aucun sens. Le mode stdin contourne ce problème : AFL++ écrit les données mutées sur stdin, et le keygenme les lit via `fgets`.

### Ce qu'on observe

Le keygenme est un programme très rapide (pas d'I/O fichier, pas de parsing complexe). Attendez-vous à des vitesses de 5 000 à 20 000 exec/s. Le `corpus count` devrait monter rapidement les premières minutes — chaque chaîne qui emprunte un chemin différent dans la routine de validation est conservée.

Les crashs seront rares (le keygenme est un programme simple sans manipulation de buffers risquée), mais le **corpus** produit est précieux : il contient des chaînes qui exercent différentes branches de la routine de vérification. En les examinant avec `xxd` ou `cat`, on peut déduire la structure attendue de la clé — préfixe, séparateurs, longueur, jeu de caractères valide.

```bash
# Examiner les inputs du corpus qui exercent le plus de branches
$ for f in out_keygen/default/queue/id:*; do
    edges=$(afl-showmap -q -o /dev/stdout -- ./keygenme_afl < "$f" 2>/dev/null | wc -l)
    echo "$edges $(cat "$f")"
  done | sort -rn | head -10
```

Les inputs avec le plus grand nombre d'edges sont ceux qui pénètrent le plus profondément dans la routine de validation — ce sont les meilleurs candidats pour comprendre la logique de vérification de la clé.

> 🔗 **Ce keygenme sera analysé en détail au Chapitre 21** avec l'ensemble des techniques (analyse statique, GDB, patching, angr). Le fuzzing ici sert de premier contact : en quelques minutes, il révèle des indices sur la structure de la clé attendue que l'analyse statique confirmera ensuite.

---

## En résumé

L'installation et la prise en main d'AFL++ suivent un workflow direct :

1. **Installer** AFL++ depuis les paquets ou les sources.  
2. **Compiler** le binaire cible avec `afl-gcc` (ou `afl-clang-fast`) au lieu de `gcc`, éventuellement avec ASan.  
3. **Préparer** un corpus initial à partir des connaissances acquises pendant le triage et l'analyse statique.  
4. **Configurer** le système (core_pattern, CPU governor).  
5. **Lancer** `afl-fuzz` et observer le tableau de bord.  
6. **Exploiter** les résultats dans `out/` — crashs, corpus, statistiques.

Le fuzzer est maintenant en marche. Mais AFL++ n'est pas le seul outil disponible : en section 15.3, nous verrons **libFuzzer**, qui adopte une approche différente — le fuzzing *in-process* — particulièrement efficace quand on veut cibler une fonction de parsing précise plutôt que le programme entier.

---


⏭️ [libFuzzer — fuzzing in-process avec sanitizers](/15-fuzzing/03-libfuzzer.md)
