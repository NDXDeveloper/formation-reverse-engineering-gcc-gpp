🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.7 — Techniques de détection de débogueur (`ptrace`, timing checks, `/proc/self/status`)

> 🎯 **Objectif** : Comprendre les techniques les plus courantes de détection de débogueur sur Linux, savoir les identifier dans le désassemblage et le décompilateur, et maîtriser les méthodes de contournement adaptées à chaque technique.

---

## Le principe de la détection de débogueur

Les sections précédentes couvraient des protections passives : le stripping retire de l'information, le packing cache le code, l'obfuscation déforme la logique, les protections mémoire restreignent les manipulations. Aucune de ces techniques ne réagit activement à la présence d'un analyste.

La détection de débogueur est une protection **active**. Le binaire inspecte son propre environnement d'exécution à la recherche d'indices trahissant la présence d'un débogueur. S'il en détecte un, il modifie son comportement : quitter immédiatement, afficher un faux résultat, corrompre ses propres données, emprunter un chemin d'exécution différent, ou simplement boucler à l'infini.

C'est un jeu du chat et de la souris. Chaque technique de détection a ses contournements, et chaque contournement peut lui-même être détecté par une technique de second niveau. En pratique, la plupart des binaires n'implémentent que les techniques classiques — celles couvertes dans cette section — et un analyste qui les connaît les neutralise en quelques minutes.

Notre binaire d'entraînement `anti_reverse.c` implémente trois de ces techniques, activables individuellement pour les étudier en isolation.

## Technique 1 — `PTRACE_TRACEME`

### Fonctionnement

C'est la technique de détection de débogueur la plus classique sur Linux, et souvent la première que rencontre un débutant en RE. Elle repose sur une propriété fondamentale de l'API `ptrace` : **un processus ne peut être tracé que par un seul parent à la fois**.

Quand GDB attache un processus (ou le lance), il utilise `ptrace(PTRACE_ATTACH, ...)` ou `ptrace(PTRACE_TRACEME, ...)` pour établir la relation de traçage. Cette relation est exclusive — le kernel refuse un second `ptrace` sur un processus déjà tracé.

La technique exploite cette exclusivité : le binaire tente de se tracer lui-même au démarrage avec `PTRACE_TRACEME`. Si l'appel réussit, personne d'autre ne le trace — pas de débogueur. Si l'appel échoue (retourne `-1` avec `errno = EPERM`), c'est qu'un débogueur est déjà attaché.

### Implémentation dans notre binaire

Voici l'implémentation dans `anti_reverse.c` :

```c
static int check_ptrace(void)
{
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
        return 1; /* débogueur détecté */
    }
    ptrace(PTRACE_DETACH, 0, NULL, NULL);
    return 0;
}
```

Et dans `main()` :

```c
if (check_ptrace()) {
    fprintf(stderr, "%s", msg_env_error);
    return 1;
}
```

Le message d'erreur est volontairement vague (`"Erreur : environnement non conforme."`) pour ne pas donner d'indication sur la cause du refus. Un message comme `"Débogueur détecté"` faciliterait trop le travail de l'analyste.

### Reconnaître la technique dans le désassemblage

Dans le désassemblage, la détection `ptrace` se manifeste par :

```nasm
; Appel à ptrace(PTRACE_TRACEME, 0, 0, 0)
xor    ecx, ecx              ; 4e argument = NULL  
xor    edx, edx              ; 3e argument = NULL  
xor    esi, esi              ; 2e argument = 0 (pid)  
xor    edi, edi              ; 1er argument = PTRACE_TRACEME (0)  
call   ptrace@plt  
; Vérification du retour
cmp    rax, -1               ; ou : test rax, rax / js  
je     .debugger_detected  
```

Les indices clés sont :

- Un appel à `ptrace@plt` visible dans les imports dynamiques (même sur un binaire strippé)  
- Le premier argument (`edi`) est `0`, ce qui correspond à `PTRACE_TRACEME` (la constante vaut 0 dans `<sys/ptrace.h>`)  
- Le retour est comparé à `-1` ou testé pour un résultat négatif  
- Le branchement en cas d'échec mène vers une sortie du programme ou un message d'erreur

Dans Ghidra, le décompilateur produit quelque chose de très lisible :

```c
if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) {
    fprintf(stderr, "Erreur : environnement non conforme.\n");
    return 1;
}
```

### Contournements

**Méthode 1 — `LD_PRELOAD` avec un `ptrace` factice**

On crée une bibliothèque partagée qui redéfinit `ptrace` pour qu'il retourne toujours 0 (succès) :

```c
/* fake_ptrace.c */
long ptrace(int request, ...) {
    return 0;
}
```

```bash
gcc -shared -fPIC -o fake_ptrace.so fake_ptrace.c  
LD_PRELOAD=./fake_ptrace.so ./anti_reverse_ptrace_only  
```

La fonction `ptrace` du binaire appelle notre version (qui retourne 0) au lieu de la vraie. Le check passe sans problème. Cette méthode est simple mais ne fonctionne pas si le binaire détecte `LD_PRELOAD` (en lisant `/proc/self/maps` ou la variable d'environnement).

**Méthode 2 — Patcher le saut conditionnel**

Dans GDB ou avec un éditeur hexadécimal, inverser le saut conditionnel qui suit l'appel à `ptrace`. Remplacer `je .debugger_detected` par `jne .debugger_detected` (changer l'opcode `0x74` en `0x75`, ou `0x84` en `0x85` pour les sauts near). Le binaire suit alors le chemin « pas de débogueur » même quand ptrace échoue.

**Méthode 3 — NOP le call à ptrace**

Remplacer l'instruction `call ptrace@plt` par des `nop` (opcode `0x90`), et s'assurer que `eax` contient 0 après (ce qui est souvent déjà le cas si le `xor` de préparation des arguments a mis `eax` à 0 plus tôt). Cela supprime complètement le check.

**Méthode 4 — Breakpoint après le check dans GDB**

La méthode la plus pragmatique : ne pas essayer de contourner le check lui-même, mais poser un breakpoint *après* le bloc de détection et modifier le flux manuellement.

```
(gdb) break main
(gdb) run
(gdb) # identifier l'adresse après le check ptrace
(gdb) set $rip = 0x<adresse_après_check>
(gdb) continue
```

Ou plus élégamment, poser un breakpoint sur `ptrace` et forcer sa valeur de retour :

```
(gdb) break ptrace
(gdb) run
(gdb) finish
(gdb) set $rax = 0
(gdb) continue
```

**Méthode 5 — Frida**

```javascript
Interceptor.attach(Module.findExportByName(null, "ptrace"), {
    onLeave: function(retval) {
        retval.replace(ptr(0));
        console.log("[*] ptrace() → return 0 (bypassed)");
    }
});
```

Frida intercepte l'appel à `ptrace` et force la valeur de retour à 0.

### Variantes et durcissements

Des implémentations plus robustes de la détection par `ptrace` existent :

- **Appel via syscall direct** — Au lieu de `ptrace()` (qui passe par la PLT et peut être hookée via `LD_PRELOAD`), le binaire utilise `syscall(SYS_ptrace, PTRACE_TRACEME, ...)` ou l'instruction `syscall` directement en assembleur inline. Cela contourne `LD_PRELOAD` et les hooks PLT.  
- **Appels multiples** — Le check `ptrace` est répété à plusieurs endroits du programme, pas uniquement au démarrage. Un contournement ponctuel ne suffit plus.  
- **Fork + ptrace** — Le processus crée un fils avec `fork()`, le fils tente de tracer le parent avec `PTRACE_ATTACH`. Si le parent est déjà débogué, l'attach échoue. Cette variante résiste au contournement par `LD_PRELOAD` sur `ptrace` car c'est un processus séparé qui fait le test.

## Technique 2 — Lecture de `/proc/self/status`

### Fonctionnement

Le système de fichiers virtuel `/proc` expose des informations sur chaque processus en cours d'exécution. Le fichier `/proc/self/status` contient des métadonnées sur le processus courant, parmi lesquelles le champ `TracerPid` :

```
$ cat /proc/self/status | grep TracerPid
TracerPid:	0
```

`TracerPid` indique le PID du processus qui trace le processus courant. Si aucun débogueur n'est attaché, la valeur est `0`. Si GDB (ou tout autre traceur `ptrace`) est attaché, la valeur est le PID de GDB.

### Implémentation dans notre binaire

```c
static int check_procfs(void)
{
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp)
        return 0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            long pid = strtol(line + 10, NULL, 10);
            fclose(fp);
            return (pid != 0) ? 1 : 0;
        }
    }
    fclose(fp);
    return 0;
}
```

### Reconnaître la technique dans le désassemblage

Les indices dans le désassemblage sont :

- Un appel à `fopen@plt` avec la chaîne `"/proc/self/status"` comme argument (visible dans `.rodata`, même sur un binaire strippé)  
- Une boucle de lecture avec `fgets@plt`  
- Un `strncmp@plt` ou une comparaison manuelle contre la chaîne `"TracerPid:"`  
- Un `strtol@plt` ou `atoi@plt` pour convertir la valeur numérique  
- Un test contre 0 suivi d'un branchement vers la sortie

L'apparition de la chaîne `"/proc/self/status"` dans `strings` est un signal d'alarme immédiat lors du triage. D'autres fichiers procfs utilisés pour la détection incluent `/proc/self/stat`, `/proc/self/wchan` et `/proc/self/maps`.

### Contournements

**Méthode 1 — Hooker `fopen` pour rediriger le fichier**

```c
/* fake_procfs.c */
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

typedef FILE *(*real_fopen_t)(const char *, const char *);

FILE *fopen(const char *path, const char *mode) {
    real_fopen_t real_fopen = dlsym(RTLD_NEXT, "fopen");
    if (strcmp(path, "/proc/self/status") == 0) {
        /* Rediriger vers un fichier forgé avec TracerPid: 0 */
        return real_fopen("/tmp/fake_status", mode);
    }
    return real_fopen(path, mode);
}
```

On prépare `/tmp/fake_status` avec un contenu identique à `/proc/self/status` mais `TracerPid: 0`, puis on lance le binaire avec `LD_PRELOAD=./fake_procfs.so`.

**Méthode 2 — Frida**

```javascript
Interceptor.attach(Module.findExportByName(null, "fopen"), {
    onEnter: function(args) {
        var path = args[0].readUtf8String();
        if (path && path.indexOf("/proc/self/status") !== -1) {
            this.shouldPatch = true;
        }
    },
    onLeave: function(retval) {
        /* Le patching se fait plutôt sur fgets ou strncmp */
    }
});

/* Approche plus directe : hooker strncmp */
Interceptor.attach(Module.findExportByName(null, "strncmp"), {
    onEnter: function(args) {
        var s1 = args[0].readUtf8String();
        if (s1 && s1.indexOf("TracerPid:") !== -1) {
            /* Réécrire le contenu pour mettre TracerPid: 0 */
            args[0].writeUtf8String("TracerPid:\t0\n");
        }
    }
});
```

**Méthode 3 — Patcher le branchement dans GDB**

Comme pour `ptrace` : identifier le `cmp` qui teste la valeur de `TracerPid` contre 0, et forcer le résultat ou sauter par-dessus le check.

```
(gdb) break check_procfs
(gdb) run
(gdb) finish
(gdb) set $rax = 0
(gdb) continue
```

**Méthode 4 — Namespace `mount` (isolation avancée)**

On peut exécuter le binaire dans un namespace `mount` où `/proc` est un système de fichiers custom, sans les vraies informations de traçage. C'est la solution la plus robuste mais aussi la plus lourde.

### Variantes

- **Lecture de `/proc/self/stat`** — Le champ numéro 6 de `/proc/self/stat` indique également le PID du traceur. L'avantage pour l'auteur du binaire : la chaîne `"TracerPid"` n'apparaît nulle part, le parsing se fait par position dans le fichier.  
- **Lecture de `/proc/self/wchan`** — Si le processus est en attente sur `ptrace_stop`, le fichier `wchan` contient cette information.  
- **Lecture de `/proc/self/maps`** — Pour détecter des bibliothèques injectées (`LD_PRELOAD`) ou la présence de Frida (dont l'agent apparaît comme un mapping mémoire nommé `frida-agent-*`).

## Technique 3 — Timing checks

### Fonctionnement

C'est la technique la plus subtile des trois. Elle ne cherche pas une signature spécifique de débogueur — elle mesure le **temps** d'exécution d'un bloc de code.

Quand un analyste utilise GDB en mode pas-à-pas (`stepi`, `nexti`), chaque instruction est exécutée individuellement. Le processus est arrêté et relancé à chaque pas, ce qui prend des millisecondes par instruction. Un bloc de 1000 instructions qui s'exécute normalement en quelques microsecondes prend plusieurs secondes en single-stepping.

Le binaire mesure le temps avant et après un bloc trivial. Si le temps écoulé dépasse un seuil anormalement élevé, c'est qu'un débogueur ralentit l'exécution.

### Implémentation dans notre binaire

```c
static int check_timing(void)
{
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* Bloc trivial — devrait prendre < 1 ms */
    volatile int dummy = 0;
    for (int i = 0; i < 1000; i++) {
        dummy += i;
    }

    clock_gettime(CLOCK_MONOTONIC, &t2);

    long elapsed_ms = (t2.tv_sec - t1.tv_sec) * 1000 +
                      (t2.tv_nsec - t1.tv_nsec) / 1000000;

    if (elapsed_ms > 50) {
        return 1; /* exécution anormalement lente */
    }
    return 0;
}
```

Le seuil de 50 ms est conservateur. En exécution normale, la boucle de 1000 itérations prend bien moins d'une milliseconde. Sous GDB en single-stepping, elle prend des dizaines de secondes.

### Sources de temps utilisées

Différentes sources de temps offrent différents niveaux de précision et de résistance au contournement :

**`clock_gettime(CLOCK_MONOTONIC)`** — C'est ce qu'utilise notre binaire. Horloge monotone (ne recule jamais), précision nanoseconde. C'est un appel libc qui utilise le vDSO pour éviter un vrai syscall — donc rapide et difficile à intercepter par un simple hook sur les syscalls.

**`rdtsc` / `rdtscp`** — L'instruction assembleur `rdtsc` (Read Time-Stamp Counter) lit directement le compteur de cycles du processeur. C'est la méthode la plus précise et la plus difficile à contourner car il n'y a aucun appel de fonction à hooker — c'est une seule instruction machine :

```nasm
rdtsc                     ; EDX:EAX = compteur de cycles  
shl    rdx, 32  
or     rax, rdx           ; RAX = compteur 64 bits complet  
```

**`gettimeofday`** — Moins précis que `clock_gettime` mais largement utilisé. Même principe.

**`time(NULL)`** — Précision à la seconde seulement. Utilisé avec des seuils larges, ou en combinaison avec des boucles longues.

### Reconnaître la technique dans le désassemblage

Les indices sont :

- Deux appels à une fonction de temps (`clock_gettime@plt`, `gettimeofday@plt`) encadrant un bloc de code trivial  
- Ou deux instructions `rdtsc` encadrant un bloc de code, avec la différence des deux valeurs comparée à un seuil  
- Une comparaison du temps écoulé (`cmp`, `sub`) contre une constante (le seuil en millisecondes ou en cycles)  
- Un branchement vers un chemin d'erreur si le seuil est dépassé  
- Le mot-clé `volatile` dans le code original se manifeste en assembleur par des accès mémoire répétés sur la variable de boucle au lieu d'une optimisation en registre

La présence de `clock_gettime` ou `gettimeofday` dans les imports dynamiques n'est pas suspecte en soi (beaucoup de programmes légitimes mesurent le temps), mais combinée avec un branchement conditionnel immédiatement après, c'est un pattern de timing check.

### Contournements

**Méthode 1 — Contourner le bloc entier dans GDB**

La méthode la plus simple : ne pas single-stepper à travers le timing check. Poser un breakpoint **après** le check et exécuter le bloc en mode `continue` (pleine vitesse).

```
(gdb) break *0x<adresse_après_check_timing>
(gdb) run
(gdb) continue
```

En exécution pleine vitesse, GDB ne ralentit pas le processus de manière détectable. Le timing check ne se déclenche que si l'analyste single-step à travers le bloc mesuré.

**Méthode 2 — Forcer le résultat de la comparaison**

Poser un breakpoint sur le `cmp` qui compare le temps écoulé au seuil, et modifier le registre ou le flag :

```
(gdb) break *0x<adresse_du_cmp>
(gdb) run
(gdb) set $eflags = ($eflags | 0x40)    # set ZF=1 pour que jg ne saute pas
(gdb) continue
```

Ou modifier la variable de temps directement pour qu'elle contienne une valeur sous le seuil.

**Méthode 3 — Hooker la source de temps**

Avec `LD_PRELOAD` ou Frida, intercepter `clock_gettime` pour retourner des valeurs cohérentes qui simulent une exécution rapide :

```javascript
var clock_gettime = Module.findExportByName(null, "clock_gettime");  
var callCount = 0;  
var baseTime = 0;  

Interceptor.attach(clock_gettime, {
    onLeave: function(retval) {
        var timespec = this.context.rsi; // 2e argument = struct timespec*
        if (callCount === 0) {
            baseTime = timespec.readU64();
        } else {
            // Simuler 1 µs écoulée à chaque appel
            timespec.writeU64(baseTime + callCount * 1000);
        }
        callCount++;
    }
});
```

Cette approche est plus complexe mais nécessaire si le timing check est répété tout au long de l'exécution.

**Méthode 4 — Patcher le seuil ou le NOP**

Modifier la constante du seuil dans le binaire (remplacer `50` par `999999999`) ou NOP la comparaison et le saut conditionnel. C'est un patching permanent qui rend le check inopérant.

**Contournement de `rdtsc`** — Si le timing check utilise `rdtsc` plutôt qu'un appel libc, les hooks `LD_PRELOAD` et Frida sur les fonctions ne fonctionnent pas (il n'y a pas de fonction à hooker). Les options sont :

- Patcher les instructions `rdtsc` dans le binaire (les remplacer par des `nop` et charger une valeur fixe)  
- Utiliser un hyperviseur qui intercepte `rdtsc` (certaines VM permettent de contrôler la valeur retournée)  
- Single-stepper uniquement en dehors du bloc mesuré (méthode 1)

## Combinaison des techniques

En pratique, un binaire sérieux combine les trois techniques — c'est ce que fait notre variante `anti_reverse_all_checks`. L'ordre d'exécution est pensé pour maximiser la résistance :

1. **`ptrace` en premier** — Détecte GDB immédiatement au lancement.  
2. **`/proc/self/status` en second** — Détecte les débogueurs qui ne déclenchent pas `ptrace` (Frida en mode attach, certains traceurs custom).  
3. **Timing check en dernier** — Détecte le single-stepping à travers le code, même si les deux checks précédents ont été contournés.

Chaque check utilise le même message d'erreur vague, ce qui empêche l'analyste de savoir quel check a déclenché la détection sans lire le code.

### Stratégie de contournement globale

Face à un binaire qui combine plusieurs checks, la stratégie efficace est d'identifier et neutraliser tous les checks en une seule passe :

1. **Triage avec `strings`** — Chercher `"/proc/self/status"`, `"TracerPid"`, et noter la présence de `ptrace` et `clock_gettime` dans les imports (`nm -D`).  
2. **Analyse statique rapide** — Dans Ghidra, repérer les appels à `ptrace`, `fopen("/proc/self/status")` et `clock_gettime` dans les premières fonctions appelées par `main`. Les cross-references sur ces imports mènent directement aux fonctions de check.  
3. **Script Frida global** — Écrire un script unique qui hooke `ptrace`, `fopen` et `clock_gettime` simultanément. Lancer le binaire avec ce script et tous les checks passent d'un coup.  
4. **Patching binaire** — Pour une solution permanente, NOP les appels ou inverser les sauts conditionnels dans chacun des blocs de détection avec un éditeur hexadécimal.

## Techniques de détection supplémentaires

Au-delà des trois techniques implémentées dans notre binaire, d'autres méthodes de détection de débogueur existent sur Linux. Elles ne sont pas implémentées dans `anti_reverse.c` pour garder le binaire pédagogique, mais vous les rencontrerez dans la nature :

**Signal handlers** — Le processus installe un handler pour `SIGTRAP`. Normalement, quand le processus exécute une instruction `int3` (opcode `0xCC`), le handler est appelé et positionne un flag. Sous GDB, le `SIGTRAP` est intercepté par le débogueur avant d'atteindre le handler du processus — le flag n'est jamais positionné. Le binaire vérifie le flag et déduit la présence d'un débogueur. Notre binaire installe un handler `SIGTRAP` à titre de démonstration (`sigtrap_handler`), bien qu'il ne l'utilise pas activement comme check.

**Détection de l'environnement** — Vérifier la présence de variables d'environnement comme `_` (qui contient le chemin de l'exécutable lanceur — `gdb` ou `ltrace` s'y trahissent), `LD_PRELOAD` (qui trahit une injection de bibliothèque), ou `LINES`/`COLUMNS` (définies par les terminaux mais pas toujours dans un contexte de débogage).

**Lecture de `/proc/self/maps`** — Chercher des mappings mémoire suspects : `frida-agent`, `vgdb` (Valgrind), ou des bibliothèques `.so` inconnues qui indiqueraient une injection.

**Détection par `ppid`** — Vérifier si le processus parent (`getppid()`) est un débogueur en lisant `/proc/<ppid>/comm` ou `/proc/<ppid>/cmdline`.

**Lecture de `/proc/self/exe`** — Comparer le binaire sur disque avec l'image en mémoire pour détecter des modifications (breakpoints logiciels).

Chacune de ces techniques suit le même schéma : inspecter l'environnement, détecter une anomalie, réagir. Et chacune peut être contournée avec les mêmes familles d'outils : hooking (`LD_PRELOAD`, Frida), patching (GDB, éditeur hex), isolation (namespaces, VM).

---


⏭️ [Contre-mesures aux breakpoints (self-modifying code, int3 scanning)](/19-anti-reversing/08-contre-mesures-breakpoints.md)
