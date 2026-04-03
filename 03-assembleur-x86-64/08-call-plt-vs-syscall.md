🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.8 — Différence entre appel de bibliothèque (`call printf@plt`) et syscall direct (`syscall`)

> 🎯 **Objectif de cette section** : comprendre les deux mécanismes par lesquels un programme interagit avec le monde extérieur (bibliothèques et noyau), savoir les distinguer dans le désassemblage, et connaître les implications de chacun pour le reverse engineering.

---

## Deux portes vers l'extérieur

Un programme compilé par GCC ne fait presque rien seul. Pour afficher du texte, lire un fichier, allouer de la mémoire ou communiquer sur le réseau, il doit faire appel à du code extérieur. Deux mécanismes distincts existent pour cela :

1. **L'appel de bibliothèque** (`call printf@plt`) — le programme appelle une fonction de la libc (ou d'une autre bibliothèque partagée `.so`), qui elle-même finit par invoquer le noyau si nécessaire.  
2. **L'appel système direct** (`syscall`) — le programme passe directement du mode utilisateur au mode noyau, sans passer par la libc.

```
Programme utilisateur
│
├── call printf@plt ──→ libc (printf) ──→ libc (write) ──→ syscall ──→ Noyau
│                        code userland      wrapper            transition
│
└── syscall ──────────────────────────────────────────────────→ Noyau
     transition directe (pas de libc)
```

La grande majorité du code compilé par GCC utilise le premier mécanisme. Le second apparaît dans du code bas niveau (assembleur inline, shellcode, binaires statiques, programmes minimalistes, certains malwares).

---

## Appel de bibliothèque via la PLT

### Ce que vous voyez dans le désassemblage

```asm
lea     rdi, [rip+0x2e5a]        ; argument : adresse de la chaîne  
call    puts@plt                   ; appel via la PLT  
```

Le suffixe `@plt` indique que l'appel passe par la **PLT** (*Procedure Linkage Table*), un mécanisme de redirection qui permet au programme de résoudre l'adresse réelle de la fonction dans la bibliothèque partagée au moment de l'exécution.

### Le mécanisme PLT/GOT en bref

Le chapitre 2 (section 2.9) couvre la PLT/GOT en détail. Voici le résumé nécessaire pour cette section :

**PLT** (*Procedure Linkage Table*) — section `.plt` : une table de petits trampolines, un par fonction externe. Chaque entrée fait un saut indirect via la GOT.

**GOT** (*Global Offset Table*) — section `.got.plt` : une table de pointeurs. Initialement, chaque pointeur renvoie vers le code de résolution du linker dynamique. Après le premier appel (*lazy binding*), il est remplacé par l'adresse réelle de la fonction dans la libc.

Le flux d'un `call puts@plt` :

```
1. call puts@plt
       │
       ▼
2. PLT stub : jmp [GOT entry for puts]
       │
       ├── Premier appel : GOT pointe vers le résolveur
       │         │
       │         ▼
       │   3. Résolveur dynamique (ld.so) : cherche puts dans libc
       │         │
       │         ▼
       │   4. Écrit l'adresse réelle de puts dans la GOT
       │         │
       │         ▼
       │   5. Saute vers puts (adresse réelle dans libc)
       │
       └── Appels suivants : GOT pointe directement vers puts
                 │
                 ▼
           puts s'exécute (pas de résolution, un seul jmp indirect)
```

### Ce que le stub PLT ressemble dans le désassemblage

Si vous examinez le code à l'adresse `puts@plt`, vous voyez typiquement :

```asm
; Section .plt
puts@plt:
    jmp     qword [rip+0x200a12]    ; saut indirect via la GOT
    push    0x0                      ; index dans la table de relocation
    jmp     0x401020                 ; saut vers le résolveur PLT commun
```

Les deux dernières instructions (`push` + `jmp` vers le résolveur) ne sont exécutées qu'au **premier appel** — ensuite, le `jmp` via la GOT va directement à la vraie fonction.

### Convention d'appel pour les fonctions de bibliothèque

Les fonctions appelées via la PLT suivent exactement la **convention System V AMD64** standard (section 3.5–3.6). Rien ne change du point de vue de l'appelant :

- Arguments dans `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`.  
- Retour dans `rax` (ou `xmm0` pour les flottants).  
- Registres caller-saved potentiellement écrasés.

C'est le point essentiel : **du point de vue du RE, un `call puts@plt` se lit exactement comme n'importe quel autre `call`**. Les arguments sont préparés dans les mêmes registres, la valeur de retour est au même endroit.

### Quelles fonctions passent par la PLT ?

Toutes les fonctions provenant de **bibliothèques partagées** (`.so`) liées dynamiquement :

- La libc : `printf`, `malloc`, `strcmp`, `open`, `read`, `write`…  
- La libm : `sin`, `cos`, `sqrt`…  
- La libpthread : `pthread_create`, `pthread_mutex_lock`…  
- OpenSSL, libcrypto, zlib, et toute autre bibliothèque dynamique.

Les fonctions **internes** au binaire (définies dans le code source du programme) ne passent **pas** par la PLT — elles sont appelées directement par adresse :

```asm
call    0x401250              ; appel direct à une fonction interne  
call    my_internal_func      ; idem, avec symbole si disponible  
```

> 💡 **Pour le RE** : la distinction `call func@plt` (externe) vs `call 0x4xxxxx` (interne) est un premier filtre de triage. Les appels PLT vous indiquent les dépendances externes du programme. Les appels directs vous montrent la logique propre du programme. Concentrez-vous sur les fonctions internes — ce sont elles qui contiennent la logique à reverser.

---

## Appel système direct : l'instruction `syscall`

### Ce que vous voyez dans le désassemblage

```asm
mov     eax, 1              ; numéro du syscall : sys_write (1)  
mov     edi, 1              ; 1er argument : fd = 1 (stdout)  
lea     rsi, [rip+0x1234]  ; 2e argument : buffer  
mov     edx, 14             ; 3e argument : count = 14  
syscall                      ; transition vers le noyau  
```

L'instruction `syscall` est une instruction matérielle spéciale qui provoque une **transition immédiate du mode utilisateur vers le mode noyau**. Le processeur change de niveau de privilège, saute vers le point d'entrée du noyau (`entry_SYSCALL_64` sous Linux), et le noyau exécute le service demandé.

### Convention d'appel des syscalls Linux x86-64

La convention des syscalls est **différente** de la convention System V AMD64 des fonctions normales :

| Aspect | Convention fonction (System V) | Convention syscall (Linux x86-64) |  
|---|---|---|  
| Numéro d'appel | N/A (adresse de la fonction) | `rax` |  
| 1er argument | `rdi` | `rdi` |  
| 2e argument | `rsi` | `rsi` |  
| 3e argument | `rdx` | `rdx` |  
| 4e argument | `rcx` | **`r10`** (pas `rcx` !) |  
| 5e argument | `r8` | `r8` |  
| 6e argument | `r9` | `r9` |  
| Valeur de retour | `rax` | `rax` |  
| Registres écrasés | caller-saved standards | **`rcx`** et **`r11`** (écrasés par le CPU) |

Deux différences majeures :

1. **Le 4e argument utilise `r10` au lieu de `rcx`**. La raison est matérielle : l'instruction `syscall` écrase automatiquement `rcx` (elle y sauvegarde `rip` pour le retour) et `r11` (elle y sauvegarde `RFLAGS`). Le noyau ne peut donc pas récupérer le 4e argument dans `rcx`.

2. **Le numéro du syscall est dans `rax`**, pas dans un registre dédié. Chaque service du noyau a un numéro fixe (défini dans les headers Linux).

### Les syscalls les plus courants

Voici les numéros que vous rencontrerez le plus souvent sur x86-64 Linux :

| Numéro (`rax`) | Nom | Signature | Rôle |  
|---|---|---|---|  
| 0 | `sys_read` | `read(fd, buf, count)` | Lire depuis un descripteur |  
| 1 | `sys_write` | `write(fd, buf, count)` | Écrire vers un descripteur |  
| 2 | `sys_open` | `open(path, flags, mode)` | Ouvrir un fichier |  
| 3 | `sys_close` | `close(fd)` | Fermer un descripteur |  
| 9 | `sys_mmap` | `mmap(addr, len, prot, flags, fd, off)` | Mapper de la mémoire |  
| 10 | `sys_mprotect` | `mprotect(addr, len, prot)` | Modifier les permissions mémoire |  
| 12 | `sys_brk` | `brk(addr)` | Étendre le segment de données |  
| 21 | `sys_access` | `access(path, mode)` | Vérifier les permissions d'un fichier |  
| 39 | `sys_getpid` | `getpid()` | Obtenir le PID du processus |  
| 57 | `sys_fork` | `fork()` | Créer un processus fils |  
| 59 | `sys_execve` | `execve(path, argv, envp)` | Exécuter un programme |  
| 60 | `sys_exit` | `exit(status)` | Terminer le processus |  
| 62 | `sys_kill` | `kill(pid, sig)` | Envoyer un signal |  
| 101 | `sys_ptrace` | `ptrace(request, pid, ...)` | Tracer un processus (anti-debug !) |  
| 231 | `sys_exit_group` | `exit_group(status)` | Terminer tous les threads |  
| 257 | `sys_openat` | `openat(dirfd, path, flags, mode)` | Ouvrir relatif à un répertoire |  
| 318 | `sys_getrandom` | `getrandom(buf, count, flags)` | Obtenir des octets aléatoires |

> 💡 **Pour le RE** : le numéro dans `rax` juste avant `syscall` identifie immédiatement le service demandé. Gardez une référence des numéros de syscalls à portée de main — la table complète est disponible dans `/usr/include/asm/unistd_64.h` ou en ligne sur des sites comme `filippo.io/linux-syscall-table`.

### Valeur de retour d'un syscall

Le noyau retourne le résultat dans `rax`. En cas de succès, c'est la valeur normale (descripteur de fichier pour `open`, nombre d'octets pour `read`/`write`, etc.). En cas d'erreur, `rax` contient une valeur négative correspondant au code d'erreur négé (par exemple `-2` pour `ENOENT`, `-13` pour `EACCES`).

La libc convertit cette convention en la paire retour/`errno` que connaît le développeur C :

```asm
; Après un syscall
cmp     rax, -4096           ; test d'erreur : valeurs de -1 à -4095 = erreur  
ja      .error               ; non signé : -1 (=0xFFFF...FFFF) > -4096 (=0xFFFF...F000)  
```

Ce pattern `cmp rax, -4096` / `ja` est la vérification d'erreur standard de la libc après un syscall. Si vous le voyez, vous êtes dans un wrapper syscall.

---

## La libc comme couche intermédiaire

En pratique, la quasi-totalité du code compilé par GCC n'invoque jamais `syscall` directement. Le code C appelle des fonctions de la libc, qui elles-mêmes invoquent les syscalls :

```c
// Code C de l'utilisateur
write(1, "Hello\n", 6);
```

```
Appel C         →  libc wrapper  →  syscall noyau  
write(fd,buf,n)    write() dans     mov eax, 1    (sys_write)  
                   la glibc          mov edi, fd
                                     mov rsi, buf
                                     mov edx, n
                                     syscall
```

La libc ajoute une couche de logique au-dessus du syscall brut :

- **Buffering** : `printf` accumule les données dans un buffer et n'appelle `write` que lorsque le buffer est plein ou qu'un `\n` est rencontré (en mode line-buffered).  
- **Gestion d'erreur** : la libc convertit la valeur de retour négative en `-1` + `errno`.  
- **Portabilité** : la libc adapte les numéros de syscalls (qui varient entre architectures) derrière une API stable.  
- **Fonctionnalités enrichies** : `malloc` utilise `mmap` et `brk` mais ajoute un allocateur complexe (ptmalloc) au-dessus. `fopen` ajoute le buffering et le parsing de modes (`"r"`, `"w"`, `"a"`).

### Exemple : ce qui se passe réellement lors d'un `printf`

```
printf("Hello %s\n", name)
   │
   ▼
vfprintf()          ← formatage de la chaîne dans un buffer interne
   │
   ▼
__overflow()        ← le buffer stdio est plein, il faut le vider
   │
   ▼
write()             ← wrapper libc autour du syscall
   │
   ▼
syscall (eax=1)     ← transition vers le noyau
   │
   ▼
sys_write()         ← code noyau qui écrit sur le terminal/fichier
```

En RE, quand vous tracez avec `strace`, vous ne voyez que le dernier niveau (les syscalls). Quand vous tracez avec `ltrace`, vous voyez les appels de bibliothèque (le premier niveau). Les deux perspectives sont complémentaires (cf. chapitre 5, section 5.5).

---

## Quand rencontre-t-on des `syscall` directs ?

Dans un binaire standard compilé par GCC avec liaison dynamique, vous ne verrez **jamais** l'instruction `syscall` dans le code de l'application — elle est cachée dans la libc. Mais plusieurs contextes la font apparaître :

### Binaires liés statiquement (`gcc -static`)

Quand la libc est liée statiquement, son code est embarqué dans le binaire. En analysant le binaire, vous verrez le code des wrappers de la libc, y compris les instructions `syscall`.

```asm
; write() dans une glibc liée statiquement
__write:
    mov     eax, 1              ; sys_write
    syscall
    cmp     rax, -4096
    ja      __syscall_error
    ret
```

### Shellcode et code d'exploitation

Le shellcode — code injecté lors d'une exploitation de vulnérabilité — utilise des syscalls directs pour être **indépendant de la libc** (qui peut ne pas être chargée à une adresse connue, ou dont les fonctions peuvent être hookées). Un shellcode classique `execve("/bin/sh")` :

```asm
; execve("/bin/sh", NULL, NULL)
xor     esi, esi              ; argv = NULL  
xor     edx, edx              ; envp = NULL  
lea     rdi, [rip+binsh]     ; pathname = "/bin/sh"  
mov     eax, 59               ; sys_execve  
syscall  

binsh: .string "/bin/sh"
```

### Programmes minimalistes

Certains programmes écrits directement en assembleur ou avec des frameworks ultra-légers (comme les entrées de concours de taille de binaire) évitent la libc entièrement :

```asm
; Programme complet : affiche "Hi\n" et quitte
_start:
    mov     eax, 1              ; sys_write
    mov     edi, 1              ; stdout
    lea     rsi, [rip+msg]
    mov     edx, 3              ; 3 octets
    syscall
    
    mov     eax, 60             ; sys_exit
    xor     edi, edi            ; status = 0
    syscall

msg: .ascii "Hi\n"
```

### Malware et anti-analyse

Certains malwares invoquent les syscalls directement pour **contourner le hooking** de la libc. Si un outil de sécurité a hooké `open()` dans la libc (en modifiant la GOT ou via `LD_PRELOAD`), un `syscall` direct avec `eax = 2` (`sys_open`) contourne complètement le hook.

```asm
; Ouverture furtive — contourne les hooks libc
mov     eax, 2                  ; sys_open (directement, pas via libc)  
lea     rdi, [rip+filepath]  
xor     esi, esi                ; O_RDONLY  
syscall  
; Le hook sur open@plt n'a jamais été déclenché
```

C'est aussi pour cette raison que certains outils d'analyse dynamique (comme Frida et `strace`) opèrent au niveau des syscalls plutôt qu'au niveau de la libc — ils ne peuvent pas être contournés par un simple appel direct.

> 💡 **Pour le RE de malware** : la présence de `syscall` directs dans du code applicatif (qui devrait normalement utiliser la libc) est un **signal d'alerte**. Cela suggère un effort délibéré pour éviter la détection, contourner des hooks, ou fonctionner sans dépendances. Les chapitres 27 et 28 explorent ce sujet en profondeur.

---

## `int 0x80` — l'ancien mécanisme (32 bits)

Avant l'instruction `syscall` (introduite avec AMD64), Linux x86 32 bits utilisait l'interruption logicielle `int 0x80` pour entrer dans le noyau. Vous pouvez encore la croiser dans :

- Du code 32 bits (binaires i386 analysés sur un système 64 bits).  
- Du vieux shellcode 32 bits.  
- Du code assembleur hérité non mis à jour.

```asm
; Ancien mécanisme 32 bits
mov     eax, 4           ; sys_write (numéro 32 bits ≠ numéro 64 bits !)  
mov     ebx, 1           ; fd = stdout  
mov     ecx, msg         ; buffer  
mov     edx, 14          ; count  
int     0x80             ; interruption → noyau  
```

La convention est totalement différente : les arguments passent par `ebx`, `ecx`, `edx`, `esi`, `edi`, `ebp` (pas `rdi`/`rsi`/`rdx`), et les numéros de syscalls ne sont pas les mêmes qu'en 64 bits.

> ⚠️ **Attention** : `int 0x80` fonctionne techniquement en mode 64 bits (pour la compatibilité), mais avec la **table de syscalls 32 bits** et des **registres tronqués à 32 bits**. C'est une source de bugs et de confusion. Si vous voyez `int 0x80` dans un binaire 64 bits, c'est soit du code hérité, soit une technique d'obfuscation délibérée.

---

## `sysenter` et `vDSO` — pour être complet

Deux autres mécanismes méritent une mention rapide :

**`sysenter`** : instruction Intel alternative à `int 0x80` pour le mode 32 bits, plus rapide. On la voit dans la glibc 32 bits sur les processeurs Intel. En 64 bits, c'est `syscall` qui est utilisé.

**vDSO** (*Virtual Dynamic Shared Object*) : un mécanisme du noyau Linux qui expose certains syscalls simples (`gettimeofday`, `clock_gettime`, `getcpu`) directement en espace utilisateur, sans transition vers le noyau. La libc appelle ces fonctions via le vDSO plutôt que par `syscall`, ce qui est beaucoup plus rapide.

Dans le désassemblage, les appels vDSO ressemblent à des appels de bibliothèque normaux (via la PLT ou un pointeur). Vous les verrez si vous analysez le code de la glibc elle-même, mais rarement dans du code applicatif.

---

## Guide de reconnaissance rapide

| Ce que vous voyez | Ce que c'est | Convention d'appel |  
|---|---|---|  
| `call func@plt` | Appel de bibliothèque dynamique | System V AMD64 standard |  
| `call 0x4xxxxx` | Appel de fonction interne | System V AMD64 standard |  
| `call qword [rax+0x...]` | Appel indirect (vtable, pointeur de fonction) | System V AMD64 standard |  
| `syscall` | Appel système direct (64 bits) | `rax` = numéro, `rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` |  
| `int 0x80` | Appel système ancien (32 bits) | `eax` = numéro, `ebx`/`ecx`/`edx`/`esi`/`edi`/`ebp` |

La question clé pour le RE :

- **`call func@plt`** → consultez la documentation de la fonction (man pages, headers) pour connaître le prototype et comprendre les arguments.  
- **`syscall`** → consultez la table des syscalls Linux pour le numéro dans `rax`, puis lisez les arguments dans `rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9`.

---

## Ce qu'il faut retenir pour la suite

1. **`call func@plt`** passe par la PLT/GOT pour atteindre les bibliothèques dynamiques — la convention d'appel est la System V AMD64 standard, identique aux fonctions internes.  
2. **`syscall`** est une transition directe vers le noyau — la convention est presque identique à System V mais le 4e argument est dans **`r10`** (pas `rcx`), et le numéro du service est dans **`rax`**.  
3. **Le code applicatif GCC standard** utilise exclusivement des `call @plt` — les `syscall` sont cachés dans la libc.  
4. **Les `syscall` directs** dans du code applicatif sont un signal d'alerte en analyse de malware : le programme cherche à contourner les hooks libc.  
5. **`strace` trace les syscalls**, **`ltrace` trace les appels de bibliothèque** — les deux niveaux sont complémentaires pour le RE dynamique (chapitre 5).  
6. **`int 0x80`** est le mécanisme 32 bits hérité — numéros et registres différents. Si vous le voyez dans un binaire 64 bits, c'est suspect.

---


⏭️ [Introduction aux instructions SIMD (SSE/AVX) — les reconnaître sans les craindre](/03-assembleur-x86-64/09-introduction-simd.md)
