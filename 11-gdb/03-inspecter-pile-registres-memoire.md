🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.3 — Inspecter la pile, les registres, la mémoire (format et tailles)

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

La section précédente a introduit les commandes `print`, `x` et `info registers` comme outils d'affichage. Cette section va plus loin : elle explique **comment lire et interpréter** ce que ces commandes montrent. Savoir taper `x/10gx $rsp` ne suffit pas — il faut comprendre ce que représentent les 10 valeurs affichées, où commence un frame de pile, quel registre contient quel argument, et comment naviguer dans les différentes régions mémoire d'un processus. C'est cette capacité d'interprétation qui distingue un utilisateur de GDB d'un reverse engineer efficace.

## Anatomie des registres x86-64 dans GDB

Le chapitre 3 a présenté les registres du point de vue de l'architecture. Ici, nous les abordons du point de vue pratique : ce qu'ils contiennent à un instant donné pendant le débogage et comment les exploiter.

### Les registres et leurs sous-registres

Chaque registre 64 bits possède des alias pour accéder à ses sous-parties. GDB les connaît tous :

```
(gdb) print/x $rax        # 64 bits complets
$1 = 0x00000000deadbeef
(gdb) print/x $eax        # 32 bits bas
$2 = 0xdeadbeef
(gdb) print/x $ax         # 16 bits bas
$3 = 0xbeef
(gdb) print/x $al         # 8 bits bas
$4 = 0xef
(gdb) print/x $ah         # 8 bits hauts du mot bas (rax[15:8])
$5 = 0xbe
```

C'est important en RE car les compilateurs utilisent fréquemment les sous-registres. Une comparaison sur un `char` utilisera `al` ou `dil`, une opération sur un `int` utilisera `eax` ou `edi`. Lire le mauvais registre (64 bits au lieu de 32) donnera une valeur qui semble absurde alors que les 32 bits bas contiennent exactement la valeur attendue.

> 💡 **Piège classique :** quand GCC opère sur un `int` (32 bits), il utilise `eax` et non `rax`. L'instruction `mov eax, 5` met implicitement à zéro les 32 bits hauts de `rax`. Mais l'instruction `mov al, 5` ne touche **pas** les bits supérieurs. Si `rax` valait `0xFFFFFFFF00000000` avant `mov al, 5`, il vaudra `0xFFFFFFFF00000005` après — pas `0x05`. Quand vous inspectez un registre dans GDB, vérifiez toujours quelle largeur le code utilise réellement.

### Registres à surveiller selon le contexte

Plutôt que de lister les 16 registres à chaque arrêt, un reverse engineer efficace sait quels registres observer selon la situation :

**À l'entrée d'une fonction** (juste après le `call`, sur la première instruction) :

| Registre | Contenu (convention System V AMD64) |  
|---|---|  
| `rdi` | 1er argument entier/pointeur |  
| `rsi` | 2e argument |  
| `rdx` | 3e argument |  
| `rcx` | 4e argument |  
| `r8` | 5e argument |  
| `r9` | 6e argument |  
| `rsp` | Pointe vers l'adresse de retour sur la pile |

Si la fonction a plus de 6 arguments entiers, les suivants sont sur la pile, à `rsp+8`, `rsp+16`, etc. (l'adresse de retour occupe `rsp+0`).

**Au retour d'une fonction** (juste après le `ret`, de retour dans l'appelant) :

| Registre | Contenu |  
|---|---|  
| `rax` | Valeur de retour entière / pointeur |  
| `xmm0` | Valeur de retour flottante (si applicable) |

**Pendant un appel à `strcmp` / `memcmp`** :

| Registre | Contenu |  
|---|---|  
| `rdi` | Pointeur vers la première chaîne |  
| `rsi` | Pointeur vers la seconde chaîne |  
| `rdx` | Taille (pour `memcmp` / `strncmp`) |

On peut immédiatement lire les deux chaînes comparées :

```
(gdb) x/s $rdi
0x7fffffffe100: "USER_INPUT"
(gdb) x/s $rsi
0x402020: "EXPECTED_KEY"
```

C'est l'une des techniques les plus directes pour extraire une clé attendue d'un crackme.

**Pendant un appel système (`syscall`)** :

| Registre | Contenu |  
|---|---|  
| `rax` | Numéro du syscall |  
| `rdi` | 1er argument |  
| `rsi` | 2e argument |  
| `rdx` | 3e argument |  
| `r10` | 4e argument |  
| `r8` | 5e argument |  
| `r9` | 6e argument |

Remarquez que la convention syscall utilise `r10` au lieu de `rcx` pour le 4e argument (car `rcx` est écrasé par l'instruction `syscall` elle-même, qui y sauvegarde `rip`).

### Le registre RFLAGS

Le registre de flags ne contient pas une « valeur » au sens habituel, mais un ensemble de bits individuels. GDB les affiche de manière lisible :

```
(gdb) print $eflags
$1 = [ CF PF ZF IF ]
```

Les flags que vous rencontrerez le plus en RE :

| Flag | Nom | Mis à 1 quand... |  
|---|---|---|  
| `ZF` | Zero Flag | Le résultat de la dernière opération est zéro |  
| `CF` | Carry Flag | Un dépassement non signé s'est produit |  
| `SF` | Sign Flag | Le résultat est négatif (bit de poids fort à 1) |  
| `OF` | Overflow Flag | Un dépassement signé s'est produit |

Après une instruction `cmp rax, rbx` (qui effectue `rax - rbx` sans stocker le résultat), les flags indiquent le résultat de la comparaison :

- `ZF = 1` → `rax == rbx`  
- `ZF = 0` et `SF == OF` → `rax > rbx` (signé)  
- `ZF = 0` et `CF = 0` → `rax > rbx` (non signé)

Quand on est arrêté juste avant un saut conditionnel (`jz`, `jne`, `jl`...), inspecter les flags indique immédiatement quel chemin sera pris :

```
(gdb) print $eflags
$2 = [ PF IF ]          # ZF absent → jz ne sautera PAS, jnz sautera
```

## La pile : structure et navigation

### Anatomie d'un frame de pile

À chaque appel de fonction, un nouveau **frame** (*stack frame*) est créé sur la pile. Rappelons la convention System V AMD64 vue au chapitre 3 : après le prologue standard `push rbp ; mov rbp, rsp`, le frame a la structure suivante (adresses croissantes vers le bas) :

```
Adresses hautes (fond de la pile)
┌──────────────────────────────┐
│  Arguments 7+ de l'appelant  │  [rbp+24], [rbp+16] ...
├──────────────────────────────┤
│  Adresse de retour           │  [rbp+8]
├──────────────────────────────┤
│  Ancien rbp (sauvegardé)     │  [rbp+0]   ← rbp pointe ici
├──────────────────────────────┤
│  Variables locales           │  [rbp-8], [rbp-16] ...
├──────────────────────────────┤
│  Zone d'alignement / padding │
├──────────────────────────────┤
│  (espace pour les appels)    │  ← rsp pointe ici (sommet de pile)
└──────────────────────────────┘
Adresses basses (sommet de la pile)
```

Dans GDB, on peut reconstituer cette structure manuellement :

```
(gdb) x/gx $rbp          # Ancien rbp (frame de l'appelant)
0x7fffffffe0f0: 0x00007fffffffe130

(gdb) x/gx $rbp+8        # Adresse de retour
0x7fffffffe0f8: 0x00000000004011a5

(gdb) x/a $rbp+8         # Même chose, format "adresse" (résolution symbolique)
0x7fffffffe0f8: 0x4011a5 <main+47>
```

Le format `/a` est précieux : il affiche non seulement l'adresse mais aussi le symbole correspondant, si disponible. On voit ici que l'adresse de retour pointe vers `main+47`, ce qui signifie qu'on est dans une fonction appelée depuis `main`.

### `backtrace` — la pile d'appels

La commande `backtrace` (abrégée `bt`) reconstitue la chaîne complète des appels de fonction en remontant les frames :

```
(gdb) backtrace
#0  check_key (input=0x7fffffffe100 "TEST-KEY\n") at keygenme.c:24
#1  0x00000000004011a5 in main (argc=1, argv=0x7fffffffe208) at keygenme.c:39
```

Chaque ligne est un frame numéroté. Le frame `#0` est la fonction courante (la plus récente), `#1` est l'appelant, et ainsi de suite jusqu'au `_start` ou `__libc_start_main` tout en bas.

Avec les symboles DWARF, GDB affiche les noms des fonctions, les arguments avec leurs valeurs, et les numéros de lignes. Sans symboles, la sortie est plus spartiate mais reste exploitable :

```
(gdb) backtrace
#0  0x0000000000401162 in ?? ()
#1  0x00000000004011a5 in ?? ()
#2  0x00007ffff7de0b6a in __libc_start_call_main () from /lib/x86_64-linux-gnu/libc.so.6
```

Les `?? ()` indiquent que GDB ne connaît pas le nom de la fonction. Les adresses restent présentes et peuvent être corrélées avec le désassemblage statique.

Pour limiter la profondeur de la trace :

```
(gdb) backtrace 5        # Les 5 frames les plus récents
(gdb) backtrace -3       # Les 3 frames les plus anciens
(gdb) backtrace full     # Avec les variables locales de chaque frame
```

`backtrace full` est particulièrement utile : il affiche les variables locales de chaque frame, offrant un instantané complet de l'état du programme à travers toute la chaîne d'appels.

### Naviguer entre les frames : `frame`, `up`, `down`

Par défaut, les commandes `print`, `info locals` et `info args` opèrent sur le frame courant (frame `#0`). On peut changer de contexte :

```
(gdb) frame 1            # Se placer dans le frame de main()
#1  0x00000000004011a5 in main (argc=1, argv=0x7fffffffe208) at keygenme.c:39
(gdb) info locals        # Variables locales de main(), pas de check_key()
input = "TEST-KEY\n\000..."
(gdb) info args          # Arguments de main()
argc = 1  
argv = 0x7fffffffe208  
```

Les commandes `up` et `down` montent ou descendent d'un frame :

```
(gdb) up                 # Monte d'un frame (vers l'appelant)
(gdb) down               # Descend d'un frame (vers l'appelé)
```

La navigation entre frames ne modifie pas l'exécution — elle change uniquement le contexte d'inspection. Quand on reprend l'exécution avec `continue` ou `step`, on repart toujours du frame `#0`.

### Inspecter la pile brute

Au-delà des commandes structurées (`backtrace`, `info frame`), il est souvent nécessaire d'examiner la pile comme une zone mémoire brute. C'est particulièrement vrai sur les binaires strippés où `backtrace` peut être incomplet ou incorrect.

Afficher le contenu de la pile depuis le sommet :

```
(gdb) x/20gx $rsp
0x7fffffffe0c0: 0x0000000000000000  0x00007fffffffe100
0x7fffffffe0d0: 0x00007fffffffe208  0x0000000100000000
0x7fffffffe0e0: 0x0000000000000000  0x0000000000000000
0x7fffffffe0f0: 0x00007fffffffe130  0x00000000004011a5
0x7fffffffe100: 0x59454b2d54534554  0x000000000000000a
...
```

Interprétons cette sortie en nous appuyant sur ce que nous savons de la structure du frame :

- `0x7fffffffe0f0` contient `0x00007fffffffe130` — c'est le `rbp` sauvegardé (l'ancien frame pointer).  
- `0x7fffffffe0f8` contient `0x00000000004011a5` — c'est l'adresse de retour, qui pointe vers `main+47`.  
- `0x7fffffffe100` contient `0x59454b2d54534554` — en lisant les octets en little-endian, cela donne `54 53 54 2d 4b 45 59` → « TEST-KEY ». C'est le buffer `input` sur la pile.

Pour afficher la pile avec résolution symbolique des adresses :

```
(gdb) x/20ag $rsp
0x7fffffffe0c0: 0x0                 0x7fffffffe100
0x7fffffffe0d0: 0x7fffffffe208      0x100000000
0x7fffffffe0e0: 0x0                 0x0
0x7fffffffe0f0: 0x7fffffffe130      0x4011a5 <main+47>
```

Le format `a` (adresse) fait apparaître `<main+47>` à côté de l'adresse de retour — c'est un repère immédiat pour identifier les adresses de retour dans un dump de pile brut.

### `info frame` — détails sur un frame

La commande `info frame` donne une vue structurée du frame courant :

```
(gdb) info frame
Stack level 0, frame at 0x7fffffffe0f8:
 rip = 0x401162 in check_key (keygenme.c:24); saved rip = 0x4011a5
 called by frame at 0x7fffffffe138
 source language c.
 Arglist at 0x7fffffffe0e8, args: input=0x7fffffffe100 "TEST-KEY\n"
 Locals at 0x7fffffffe0e8, Locals list:
  result = 0
 Saved registers:
  rbp at 0x7fffffffe0e8, rip at 0x7fffffffe0f0
```

On y retrouve l'adresse de retour sauvegardée (`saved rip`), le frame appelant, les arguments, les variables locales, et l'emplacement des registres sauvegardés. C'est un excellent point de départ pour comprendre la disposition d'un frame avant de plonger dans les adresses brutes.

## Inspecter la mémoire : méthodologie et cas d'usage

La commande `x` a été présentée dans la section précédente. Ici, nous abordons les **stratégies d'inspection** qui reviennent constamment en RE.

### Identifier les régions mémoire

Avant d'examiner une adresse, il est utile de savoir dans quelle région elle se trouve :

```
(gdb) info proc mappings
  Start Addr           End Addr       Size     Offset  Perms  objfile
  0x00400000         0x00401000     0x1000        0x0  r--p   keygenme_O0
  0x00401000         0x00402000     0x1000     0x1000  r-xp   keygenme_O0
  0x00402000         0x00403000     0x1000     0x2000  r--p   keygenme_O0
  0x00403000         0x00404000     0x1000     0x2000  rw-p   keygenme_O0
  0x00007ffff7dc0000 0x00007ffff7de8000 0x28000  0x0  r--p   libc.so.6
  0x00007ffff7de8000 0x00007ffff7f5d000 0x175000 0x28000 r-xp libc.so.6
  ...
  0x00007ffffffde000 0x00007ffffffff000 0x21000  0x0  rw-p   [stack]
```

Les permissions indiquent le type de contenu :

| Permissions | Région typique | Contenu |  
|---|---|---|  
| `r-xp` | `.text` | Code exécutable |  
| `r--p` | `.rodata`, headers | Données en lecture seule (chaînes constantes, tables) |  
| `rw-p` | `.data`, `.bss`, heap, stack | Données modifiables |  
| `r-xp` + libc | Code de la libc | Fonctions de bibliothèque |

Quand vous examinez un pointeur et que sa valeur est `0x7fffffffe100`, le préfixe `0x7fffff...` indique immédiatement la pile. Une adresse `0x402xxx` pointe dans les données du binaire. Une adresse `0x7ffff7...` pointe dans une bibliothèque partagée. Avec l'habitude, cette identification devient instantanée.

### Lire des chaînes de caractères

Les chaînes sont omniprésentes en RE : messages d'erreur, clés de chiffrement, URLs, noms de fichiers. Plusieurs approches selon la situation :

```
(gdb) x/s 0x402010
0x402010: "Enter your key: "
```

Chaîne C classique (null-terminated) dans `.rodata`.

```
(gdb) x/s $rdi
0x7fffffffe100: "TEST-KEY\n"
```

Chaîne pointée par un registre — typiquement un argument de fonction.

Si la chaîne n'est pas null-terminated (cas fréquent avec des buffers de taille fixe ou des protocoles réseau), on examine les octets bruts :

```
(gdb) x/32bx 0x7fffffffe100
0x7fffffffe100: 0x54 0x45 0x53 0x54 0x2d 0x4b 0x45 0x59
0x7fffffffe108: 0x0a 0x00 0x00 0x00 0x00 0x00 0x00 0x00
0x7fffffffe110: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
0x7fffffffe118: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
```

Et pour visualiser les caractères imprimables :

```
(gdb) x/32bc 0x7fffffffe100
0x7fffffffe100: 84 'T'  69 'E'  83 'S'  84 'T'  45 '-'  75 'K'  69 'E'  89 'Y'
0x7fffffffe108: 10 '\n' 0 '\000' 0 '\000' 0 '\000' ...
```

### Lire des structures en mémoire

Quand les symboles DWARF sont présents et que le type est connu, GDB peut afficher une structure de manière lisible :

```
(gdb) print *player
$1 = {
  name = "Alice\000...",
  health = 100,
  x = 15.5,
  y = -3.2000000000000002,
  inventory = {0, 3, 0, 1, 0, 0, 0, 0, 0, 0}
}
```

L'option `set print pretty on` (qu'on met généralement dans `.gdbinit`) active l'indentation, rendant les structures imbriquées beaucoup plus lisibles.

Sans symboles, il faut reconstruire manuellement. Si l'analyse statique dans Ghidra a révélé qu'une structure commence à l'adresse contenue dans `rdi` et que ses champs sont disposés comme suit : un tableau de 32 caractères, un `int`, deux `double` — on peut vérifier :

```
(gdb) x/s $rdi                 # Champ name (offset 0, 32 chars)
0x555555558040: "Alice"

(gdb) x/dw $rdi+32             # Champ health (offset 32, int = 4 bytes)
0x555555558060: 100

(gdb) x/fg $rdi+40             # Champ x (offset 40, double = 8 bytes)
0x555555558068: 15.5

(gdb) x/fg $rdi+48             # Champ y (offset 48, double = 8 bytes)
0x555555558070: -3.2000000000000002
```

Le format `/f` avec la taille `g` (8 octets) affiche un `double`. Avec la taille `w` (4 octets), il afficherait un `float`. Le format `/d` avec la taille `w` affiche un `int` signé.

### L'endianness dans les dumps mémoire

x86-64 est une architecture **little-endian** : l'octet de poids faible est stocké à l'adresse la plus basse. C'est un piège récurrent lors de la lecture des dumps hexadécimaux.

Considérons la valeur `0x0000000000402010` stockée en mémoire à l'adresse `0x7fffffffe0d0` :

```
(gdb) x/gx 0x7fffffffe0d0        # Lecture en mot de 8 octets
0x7fffffffe0d0: 0x0000000000402010   # GDB reconstruit la valeur correctement

(gdb) x/8bx 0x7fffffffe0d0       # Lecture octet par octet
0x7fffffffe0d0: 0x10  0x20  0x40  0x00  0x00  0x00  0x00  0x00
```

En lisant octet par octet, on voit `10 20 40 00...` — les octets sont en ordre **inversé** par rapport à la valeur `0x0000000000402010`. C'est l'ordre little-endian : `0x10` (octet de poids faible) est à l'adresse la plus basse.

Quand on utilise `x/gx` ou `x/wx`, GDB effectue automatiquement la conversion et affiche la valeur dans l'ordre naturel (big-endian pour la lecture humaine). Mais quand on lit octet par octet avec `x/bx` — ou quand on examine un dump dans ImHex — il faut penser à inverser mentalement les octets pour reconstruire les valeurs multi-octets.

Cela affecte aussi la lecture des chaînes dans les dumps en mots :

```
(gdb) x/2gx 0x7fffffffe100
0x7fffffffe100: 0x59454b2d54534554  0x000000000000000a
```

La valeur `0x59454b2d54534554` correspond, en little-endian, aux octets `54 53 54 2d 4b 45 59` → `T S T - K E Y`. On lit la chaîne « à l'envers » dans le mot affiché.

### Surveiller les appels de bibliothèque par leurs arguments

Une technique puissante consiste à poser un breakpoint sur une fonction de bibliothèque et à inspecter systématiquement ses arguments. Voici les fonctions les plus révélatrices et ce qu'il faut regarder :

**`strcmp` / `strncmp` / `memcmp`** — comparaison de données :
```
(gdb) break strcmp
(gdb) commands
  silent
  printf "strcmp(%s, %s)\n", (char *)$rdi, (char *)$rsi
  continue
end
```

Le bloc `commands` s'exécute automatiquement quand le breakpoint est atteint. `silent` supprime le message standard de GDB. On obtient un log continu de toutes les comparaisons de chaînes :

```
strcmp(TEST-KEY, VALID-KEY-2025)  
strcmp(en_US.UTF-8, C)  
...
```

**`malloc` / `free`** — allocations mémoire :
```
(gdb) break malloc
(gdb) commands
  silent
  printf "malloc(%d)\n", $rdi
  continue
end
```

**`open` / `fopen`** — fichiers accédés :
```
(gdb) break open
(gdb) commands
  silent
  printf "open(\"%s\", %d)\n", (char *)$rdi, $rsi
  continue
end
```

**`send` / `recv`** — données réseau :
```
(gdb) break send
(gdb) commands
  silent
  printf "send(fd=%d, buf=%p, len=%d)\n", $rdi, $rsi, $rdx
  x/s $rsi
  continue
end
```

Cette approche transforme GDB en un outil de traçage ciblé : au lieu de `strace` ou `ltrace` qui montrent tout, on ne capture que les appels qui nous intéressent, avec le format d'affichage qu'on a choisi.

## Le heap

Le tas (*heap*) est la zone de mémoire allouée dynamiquement via `malloc`, `calloc`, `realloc` (ou `new` en C++). Contrairement à la pile dont la structure est régulière et prévisible, le heap est géré par l'allocateur de la glibc (`ptmalloc2`) qui maintient ses propres métadonnées.

### Localiser le heap

```
(gdb) info proc mappings | grep heap
  0x0000555555559000 0x000055555557a000 0x21000  0x0  rw-p   [heap]
```

On peut aussi trouver le début du heap via le symbole interne de la glibc :

```
(gdb) print (void *)&__malloc_hook
```

### Inspecter une allocation

Si on connaît l'adresse retournée par `malloc` (par exemple en ayant breaké sur `malloc` et noté `$rax` au retour), on peut examiner la zone allouée :

```
(gdb) x/8gx 0x555555559260
0x555555559260: 0x0000000000000000  0x0000000000000031  ← header du chunk
0x555555559270: 0x4141414141414141  0x4242424242424242  ← données utilisateur
0x555555559280: 0x0000000000000000  0x0000000000000000
0x555555559290: 0x0000000000000000  0x0000000000020d71  ← top chunk
```

Le mot précédant les données utilisateur (`0x31` ici) est le **header du chunk** de l'allocateur. En `ptmalloc2`, il encode la taille du chunk (bits de poids fort) et des flags dans les bits de poids faible. La valeur `0x31` signifie : taille = `0x30` (48 octets), bit `PREV_INUSE` à 1 (le chunk précédent est occupé).

L'analyse détaillée du heap est un sujet avancé couvert en section 12.4 avec pwndbg, mais savoir lire les headers de chunk basiques est utile dès maintenant.

## Mémoire mappée et fichiers : `/proc` et `info`

GDB donne accès à des informations supplémentaires sur le processus via le pseudo-filesystem `/proc` :

```
(gdb) shell cat /proc/$(pidof keygenme_O0)/maps
```

C'est l'équivalent de `info proc mappings`, mais directement depuis le noyau. On peut aussi consulter :

```
(gdb) shell cat /proc/$(pidof keygenme_O0)/status     # État du processus
(gdb) shell cat /proc/$(pidof keygenme_O0)/fd/         # Descripteurs de fichiers ouverts
(gdb) shell ls -la /proc/$(pidof keygenme_O0)/fd/      # Liens symboliques vers les fichiers
```

La commande `shell` dans GDB exécute n'importe quelle commande shell sans quitter la session de débogage. C'est un moyen rapide d'accéder à des informations système pendant l'analyse.

Alternativement, `info files` (ou `info target`) liste les sections du binaire chargé avec leurs adresses :

```
(gdb) info files
Symbols from "/home/user/keygenme_O0".  
Local exec file:  
  Entry point: 0x401060
  0x00400318 - 0x00400334 is .interp
  0x00400338 - 0x00400358 is .note.gnu.build-id
  ...
  0x00401000 - 0x004011b4 is .text
  0x00402000 - 0x00402038 is .rodata
  0x00403e00 - 0x00404030 is .got.plt
  ...
```

## Dumps mémoire : sauvegarder et restaurer

En cours d'analyse, on peut avoir besoin de sauvegarder une région mémoire pour l'examiner dans un outil externe (ImHex, un script Python, etc.) :

```
(gdb) dump binary memory /tmp/stack_dump.bin 0x7fffffffe000 0x7fffffffe200
(gdb) dump binary memory /tmp/heap_dump.bin 0x555555559000 0x55555555a000
```

Ces fichiers binaires bruts peuvent ensuite être ouverts dans ImHex (chapitre 6) pour une analyse hexadécimale confortable, ou lus par un script Python :

```python
with open("/tmp/stack_dump.bin", "rb") as f:
    data = f.read()
```

On peut aussi sauvegarder la valeur d'une expression :

```
(gdb) dump binary value /tmp/buffer.bin input
```

Et pour charger des données en mémoire (utile pour modifier un buffer à la volée) :

```
(gdb) restore /tmp/patched_data.bin binary 0x7fffffffe100
```

## Synthèse : workflow d'inspection en RE

Pour conclure, voici le workflow typique quand on est arrêté sur un breakpoint et qu'on veut comprendre ce qui se passe :

**1. Situer — où suis-je ?**
```
(gdb) backtrace 3          # Pile d'appels récente
(gdb) x/5i $rip            # Instructions autour du point courant
```

**2. Observer — quel est l'état ?**
```
(gdb) info registers       # Vue d'ensemble des registres
(gdb) x/8gx $rsp           # Sommet de la pile
```

**3. Interpréter — que signifient ces valeurs ?**
```
(gdb) x/s $rdi             # Si rdi est un pointeur vers une chaîne
(gdb) x/a $rbp+8           # Adresse de retour
(gdb) print/x $rax         # Valeur de retour / accumulateur
```

**4. Décider — que faire ensuite ?**
```
(gdb) stepi                # Avancer d'une instruction
(gdb) finish               # Sortir de la fonction
(gdb) continue             # Aller au prochain breakpoint
```

Ce cycle situer → observer → interpréter → décider se répète à chaque arrêt. Avec la pratique, il devient un réflexe et chaque itération ne prend que quelques secondes.

---

> **À retenir :** Inspecter la mémoire dans GDB, ce n'est pas seulement connaître la syntaxe de `x` et `print` — c'est savoir où regarder et comment interpréter ce qu'on voit. La pile a une structure prévisible qu'on peut lire manuellement quand `backtrace` échoue. Les registres ont des rôles conventionnels qui changent selon le contexte (entrée de fonction, syscall, retour). Et les adresses elles-mêmes — par leur préfixe — trahissent la région mémoire à laquelle elles appartiennent. Cette capacité de lecture est ce qui rend l'analyse dynamique opérationnelle.

⏭️ [GDB sur un binaire strippé — travailler sans symboles](/11-gdb/04-gdb-binaire-strippe.md)
