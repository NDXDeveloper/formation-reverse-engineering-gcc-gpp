🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.5 — Stack canaries (`-fstack-protector`), ASLR, PIE, NX

> 🎯 **Objectif** : Comprendre le fonctionnement interne des quatre protections mémoire les plus courantes sur Linux — stack canaries, ASLR, PIE et NX — leur impact concret sur l'analyse statique et dynamique, comment les détecter avec `checksec` et `readelf`, et dans quels cas elles compliquent (ou non) le travail du reverse engineer.

---

## Protections mémoire : une autre catégorie

Les sections précédentes couvraient des techniques conçues spécifiquement pour gêner l'analyste : stripping, packing, obfuscation. Les protections de cette section ont un objectif différent. Elles ne visent pas le reverse engineer — elles visent l'**attaquant** qui tente d'exploiter une vulnérabilité mémoire (buffer overflow, use-after-free, ROP chain, etc.).

Cela dit, ces protections ont un impact direct sur l'analyse dynamique. Un analyste qui tente de suivre l'exécution dans GDB, de prédire les adresses mémoire ou de patcher un binaire en mémoire se heurte à ces mécanismes. Les comprendre est indispensable pour ne pas confondre un artefact de protection avec un comportement intentionnel du programme.

Les quatre protections couvertes ici opèrent à des niveaux différents :

| Protection | Niveau | Contre quoi |  
|---|---|---|  
| Stack canary | Compilateur | Écrasement de l'adresse de retour (stack buffer overflow) |  
| NX | OS + matériel | Exécution de code injecté sur la pile ou le tas |  
| ASLR | OS (kernel) | Prédiction des adresses mémoire |  
| PIE | Compilateur + OS | Prédiction de l'adresse de base du binaire |

## Stack canaries (`-fstack-protector`)

### Le problème résolu

Un buffer overflow classique sur la pile écrase les données au-delà du buffer, incluant potentiellement l'adresse de retour sauvegardée par le prologue de la fonction. Si l'attaquant contrôle cette adresse de retour, il redirige l'exécution vers du code arbitraire au moment du `ret`.

### Le mécanisme

Le stack canary (parfois appelé « stack cookie ») est une valeur sentinelle placée entre les variables locales et l'adresse de retour sauvegardée. Si un buffer overflow écrase le buffer et poursuit vers l'adresse de retour, il écrase forcément le canary au passage. Le compilateur insère du code dans l'épilogue de la fonction pour vérifier que la valeur du canary n'a pas changé. Si elle a changé, le programme appelle `__stack_chk_fail` et se termine immédiatement.

### Ce que ça donne en assembleur

Voici le prologue et l'épilogue d'une fonction protégée par un canary, compilée avec GCC et `-fstack-protector` :

**Prologue — mise en place du canary :**

```nasm
push   rbp  
mov    rbp, rsp  
sub    rsp, 0x60                    ; allocation des variables locales  
mov    rax, qword [fs:0x28]        ; lecture du canary depuis le TLS  
mov    qword [rbp-0x8], rax        ; stockage sur la pile, juste avant rbp sauvé  
xor    eax, eax                    ; nettoyage du registre  
```

**Épilogue — vérification du canary :**

```nasm
mov    rax, qword [rbp-0x8]        ; relecture du canary depuis la pile  
cmp    rax, qword [fs:0x28]        ; comparaison avec la valeur de référence  
jne    .canary_fail                 ; si différent → overflow détecté  
leave  
ret  

.canary_fail:
call   __stack_chk_fail            ; termine le programme, ne retourne jamais
```

Plusieurs éléments sont remarquables dans ce code.

**L'accès à `fs:0x28`** — La valeur de référence du canary est stockée dans le Thread Local Storage (TLS), accessible via le segment register `fs`. L'offset `0x28` est standard sur Linux x86-64 pour le canary dans la structure `tcbhead_t` de la glibc. Cette valeur est initialisée aléatoirement par le kernel au démarrage du processus et reste constante pendant toute l'exécution. Chaque thread a son propre canary.

**La position du canary sur la pile** — Le canary est placé à `[rbp-0x8]`, c'est-à-dire immédiatement avant le `rbp` sauvegardé et l'adresse de retour. C'est le dernier rempart avant les données critiques de la pile.

**L'appel à `__stack_chk_fail`** — Cette fonction est fournie par la glibc. Elle affiche un message d'erreur (`*** stack smashing detected ***`), enregistre l'événement et appelle `abort()`. Le programme ne peut pas survivre à un canary corrompu.

### Les variantes de `-fstack-protector`

GCC propose quatre niveaux de protection par canary :

**`-fstack-protector`** — Le mode standard. N'insère un canary que dans les fonctions qui contiennent un buffer de caractères de 8 octets ou plus, ou qui appellent `alloca`. C'est un compromis entre protection et performance. C'est le défaut sur la plupart des distributions modernes.

**`-fstack-protector-strong`** — Plus conservateur. Protège les fonctions qui contiennent des tableaux de toute taille, des variables dont l'adresse est prise (`&var`), ou des variables locales utilisées dans des appels de fonctions. Couvre plus de cas que `-fstack-protector` sans le coût de protéger *toutes* les fonctions.

**`-fstack-protector-all`** — Insère un canary dans **chaque** fonction, sans exception. Coût en performance plus élevé (un accès TLS + une comparaison par appel de fonction), mais couverture maximale.

**`-fno-stack-protector`** — Désactive explicitement la protection. Aucun canary n'est inséré.

### Impact sur le reverse engineering

Pour l'analyste statique, le canary est un artefact reconnaissable mais inoffensif. L'accès à `fs:0x28` dans le prologue et le `call __stack_chk_fail` dans l'épilogue sont des patterns que l'on apprend à ignorer mentalement — ils ne font pas partie de la logique du programme.

Pour l'analyste dynamique, le canary a deux implications :

- **Il interdit le patching naïf de la pile** — Si vous tentez de modifier des données sur la pile dans GDB au-delà du buffer (par exemple pour changer la valeur d'une variable locale protégée), vous risquez d'écraser le canary et de déclencher `__stack_chk_fail`.  
- **Il révèle la disposition de la pile** — La position du canary indique précisément où se terminent les variables locales et où commence la zone critique (rbp sauvé, adresse de retour). C'est une information utile pour comprendre le layout de la pile.

### Détecter le canary

```bash
$ checksec --file=build/vuln_canary
    Stack:    Canary found

$ checksec --file=build/vuln_no_canary
    Stack:    No canary found
```

On peut aussi vérifier dans la table des symboles dynamiques :

```bash
$ nm -D build/vuln_canary | grep stack_chk
                 U __stack_chk_fail@GLIBC_2.4
```

La présence de `__stack_chk_fail` dans les imports dynamiques confirme l'utilisation de canaries.

### Observer le canary dans GDB

Les extensions GDB (GEF, pwndbg) affichent le canary directement :

```
gef> canary
[+] The canary of process 12345 is at 0x7ffff7d8a768, value is 0xd84f2c91a7e3b100
```

Notez que le dernier octet du canary est toujours `0x00` (null byte). C'est intentionnel : si un overflow utilise `strcpy` (qui s'arrête au null byte), le null byte du canary empêchera l'overflow de copier des données au-delà du canary. C'est une subtilité de design souvent mentionnée dans les CTF.

## NX (No-eXecute)

### Le problème résolu

Dans les attaques classiques par buffer overflow, l'attaquant injecte du shellcode (code machine) dans un buffer sur la pile, puis redirige l'exécution vers ce buffer. Le code injecté s'exécute avec les privilèges du processus victime.

### Le mécanisme

NX (aussi appelé DEP — Data Execution Prevention, ou W^X — Write XOR eXecute) est une protection matérielle et logicielle qui marque les pages mémoire comme soit écrivables, soit exécutables, mais **jamais les deux simultanément**. La pile et le tas sont marqués RW (read-write) mais pas X (execute). La section `.text` est marquée RX (read-execute) mais pas W (write).

Si le processeur tente d'exécuter une instruction sur une page non exécutable, le matériel génère une exception (page fault), et le kernel tue le processus.

Sur x86-64, NX est implémenté via le bit NX (bit 63) des entrées de la table des pages. Tous les processeurs x86-64 supportent ce bit. Sur les anciens x86 32 bits, le support matériel n'existait pas toujours, et NX était émulé par le kernel (ou absent).

### Impact sur le reverse engineering

NX n'a quasiment aucun impact sur l'analyse statique. Pour l'analyse dynamique, il empêche une technique spécifique : injecter du code en mémoire et l'exécuter. Par exemple, si un analyste voulait écrire un petit stub en assembleur dans la pile avec GDB et l'exécuter via `set $rip`, NX bloquerait cette tentative.

En pratique, cette limitation est rarement un obstacle pour le RE pur. Elle est beaucoup plus pertinente pour l'exploitation de vulnérabilités, où elle force l'attaquant à utiliser des techniques comme le ROP (Return-Oriented Programming) au lieu d'un shellcode direct.

### Détecter NX

```bash
$ checksec --file=build/vuln_no_canary
    NX:       NX enabled

$ checksec --file=build/vuln_execstack
    NX:       NX disabled
```

On peut aussi vérifier avec `readelf` en cherchant le segment `GNU_STACK` :

```bash
$ readelf -l build/vuln_no_canary | grep GNU_STACK
  GNU_STACK      0x000000 0x0000000000000000 ... 0x000000 RW  0x10

$ readelf -l build/vuln_execstack | grep GNU_STACK
  GNU_STACK      0x000000 0x0000000000000000 ... 0x000000 RWE 0x10
```

Le flag `RW` signifie NX activé (la pile est read-write, pas executable). Le flag `RWE` signifie NX désactivé (la pile est read-write-execute).

Pour compiler sans NX, on passe `-z execstack` au linker :

```bash
gcc -z execstack -o vuln_execstack vuln_demo.c
```

NX est activé par défaut sur toutes les distributions modernes. Un binaire avec `NX disabled` est soit très ancien, soit compilé explicitement avec `-z execstack` — ce qui mérite immédiatement votre attention.

## ASLR (Address Space Layout Randomization)

### Le problème résolu

De nombreuses attaques reposent sur la connaissance des adresses mémoire : adresse d'une fonction, adresse d'un gadget ROP, adresse d'un buffer. Si les adresses sont prévisibles, l'attaquant peut coder ces adresses en dur dans son payload.

### Le mécanisme

ASLR est une fonctionnalité du **kernel Linux**, pas du compilateur. À chaque exécution d'un programme, le kernel randomise les adresses de base de plusieurs régions mémoire :

- **La pile** — L'adresse de début de la pile est décalée aléatoirement.  
- **Le tas (heap)** — L'adresse de base du tas (`brk`) est randomisée.  
- **Les bibliothèques partagées** — L'adresse de chargement de chaque `.so` (libc, libpthread, etc.) change à chaque exécution.  
- **Le mapping `mmap`** — Les allocations `mmap` (utilisées entre autres pour les grandes allocations `malloc`) sont randomisées.  
- **Le binaire lui-même** — Uniquement si le binaire est compilé en PIE (voir section suivante).

ASLR est contrôlé par le paramètre kernel `/proc/sys/kernel/randomize_va_space` :

- `0` — ASLR désactivé. Adresses identiques à chaque exécution.  
- `1` — Randomisation partielle : pile, bibliothèques, mmap. Le binaire principal et le tas ne sont pas randomisés.  
- `2` — Randomisation complète : tout ce qui précède + le tas. C'est la valeur par défaut sur les distributions modernes.

### Observer l'ASLR en action

Notre binaire `vuln_demo` affiche les adresses de ses régions mémoire. En l'exécutant deux fois de suite :

```bash
$ ./build/vuln_pie
--- Informations d'adressage ---
  main()       @ 0x55d3a4401290  (.text)
  stack_var    @ 0x7ffd8b234a5c  (pile)
  heap_var     @ 0x55d3a5e2b2a0  (tas)

$ ./build/vuln_pie
--- Informations d'adressage ---
  main()       @ 0x563e1c801290  (.text)
  stack_var    @ 0x7ffcc3f19a1c  (pile)
  heap_var     @ 0x563e1da1c2a0  (tas)
```

Toutes les adresses ont changé. Comparez avec le même binaire compilé sans PIE :

```bash
$ ./build/vuln_no_pie
--- Informations d'adressage ---
  main()       @ 0x401290        (.text)
  stack_var    @ 0x7ffc92d1aa2c  (pile)
  heap_var     @ 0x1c3a2a0       (tas)

$ ./build/vuln_no_pie
--- Informations d'adressage ---
  main()       @ 0x401290        (.text)
  stack_var    @ 0x7fff5a88c91c  (pile)
  heap_var     @ 0x24432a0       (tas)
```

Sans PIE, l'adresse de `main()` est fixe (`0x401290`). La pile et le tas changent (ASLR kernel), mais le code du binaire reste à la même adresse.

### Impact sur le reverse engineering

L'ASLR a un impact direct sur l'analyse dynamique :

- **Les adresses ne sont pas reproductibles** — Si vous notez l'adresse d'une variable lors d'une session GDB, cette adresse sera différente lors de la session suivante. Il faut raisonner en offsets relatifs, pas en adresses absolues.  
- **Les breakpoints sur adresses absolues ne marchent qu'une seule fois** — Sous GDB, si vous posez `break *0x55d3a4401290`, ce breakpoint ne sera valide que pour cette exécution. Préférez les breakpoints sur noms de fonctions (`break main`) ou sur offsets depuis la base (`break *($base + 0x1290)`).  
- **Les scripts doivent calculer les adresses** — Tout script GDB ou Frida qui manipule des adresses doit d'abord déterminer l'adresse de base du binaire et des bibliothèques, puis calculer les adresses cibles.

### Désactiver l'ASLR pour l'analyse

Pour simplifier une session d'analyse, on peut désactiver l'ASLR temporairement :

```bash
# Pour un seul processus (via setarch)
$ setarch $(uname -m) -R ./binaire_cible

# Globalement (nécessite root, à rétablir après)
# echo 0 > /proc/sys/kernel/randomize_va_space

# Dans GDB (désactive l'ASLR pour le processus débogué)
(gdb) set disable-randomization on    # activé par défaut dans GDB
```

GDB désactive l'ASLR par défaut pour le processus qu'il lance. C'est un piège classique : un analyste développe un exploit qui fonctionne sous GDB (ASLR off) mais échoue hors GDB (ASLR on).

## PIE (Position Independent Executable)

### Le rapport avec ASLR

PIE est le complément de l'ASLR côté compilateur. ASLR randomise la pile, le tas et les bibliothèques partagées — mais il ne peut randomiser l'adresse de base du binaire principal que si celui-ci a été compilé en PIE.

Un binaire PIE est un binaire dont tout le code est position-independent : il n'utilise aucune adresse absolue, toutes les références sont relatives (via des adressages `rip`-relative). Techniquement, un PIE est un shared object (`ET_DYN` dans l'ELF header) que le loader peut charger à n'importe quelle adresse.

Un binaire non-PIE est de type `ET_EXEC` et est chargé à une adresse fixe (typiquement `0x400000` sur x86-64). ASLR ne peut pas déplacer un `ET_EXEC`.

### Ce que PIE change en assembleur

La différence la plus visible entre un binaire PIE et non-PIE se manifeste dans l'adressage des données globales et des fonctions :

**Non-PIE — adressage absolu :**

```nasm
; Chargement d'une chaîne depuis .rodata
mov    edi, 0x402010        ; adresse absolue de la chaîne  
call   0x401030             ; adresse absolue de puts@plt  
```

**PIE — adressage rip-relative :**

```nasm
; Chargement d'une chaîne depuis .rodata
lea    rdi, [rip+0x2e3f]   ; offset relatif à l'instruction courante  
call   printf@plt           ; résolu via PLT, lui-même relatif  
```

L'instruction `lea rdi, [rip+0x2e3f]` est le pattern signature du PIE. L'adresse effective est calculée en ajoutant l'offset `0x2e3f` à la valeur actuelle de `rip`. Quel que soit l'endroit où le binaire est chargé en mémoire, cet offset reste correct.

### Impact sur le reverse engineering

**Analyse statique** — PIE complique légèrement la lecture du désassemblage car les adresses affichées sont des offsets relatifs (commençant souvent à `0x0` ou `0x1000`), pas des adresses absolues. Ghidra gère bien les binaires PIE et affiche des adresses de base cohérentes, mais `objdump` sur un PIE peut afficher des adresses basses qui ne correspondent pas à l'adresse de chargement réelle.

**Analyse dynamique** — PIE combiné avec ASLR signifie que l'adresse de base du binaire change à chaque exécution. Les outils modernes (GEF, pwndbg) affichent automatiquement l'adresse de base :

```
gef> vmmap  
Start              End                Offset             Perm Path  
0x55555555400      0x555555556000     0x0000000000000000 r-x /path/to/binary
```

L'adresse de base est `0x555555554000` dans cet exemple. Chaque adresse dans le binaire est cette base + l'offset vu dans le désassembleur.

### Détecter PIE

```bash
$ checksec --file=build/vuln_pie
    PIE:      PIE enabled

$ checksec --file=build/vuln_no_pie
    PIE:      No PIE
```

Avec `readelf` :

```bash
$ readelf -h build/vuln_pie | grep Type
  Type:                              DYN (Position-Independent Executable)

$ readelf -h build/vuln_no_pie | grep Type
  Type:                              EXEC (Executable file)
```

`DYN` = PIE. `EXEC` = non-PIE.

Et avec `file` :

```bash
$ file build/vuln_pie
vuln_pie: ELF 64-bit LSB pie executable, ...

$ file build/vuln_no_pie
vuln_no_pie: ELF 64-bit LSB executable, ...
```

La mention `pie executable` vs `executable` est l'indicateur direct.

PIE est activé par défaut sur GCC depuis les versions récentes (≥ 8) sur la plupart des distributions. Pour le désactiver explicitement :

```bash
gcc -no-pie -fno-pie -o vuln_no_pie vuln_demo.c
```

Le flag `-no-pie` est pour le linker et `-fno-pie` est pour le compilateur (génération de code non position-independent). Les deux sont nécessaires.

## Interaction entre les quatre protections

Ces protections ne sont pas indépendantes. Elles se complètent et leur combinaison détermine la surface d'attaque résiduelle :

**ASLR + PIE** — C'est la combinaison qui rend toutes les adresses imprévisibles. ASLR seul sans PIE laisse le code du binaire principal à une adresse fixe. PIE sans ASLR produit un binaire relocatable mais toujours chargé au même endroit. Les deux ensemble sont nécessaires pour une randomisation complète.

**NX + canary** — NX empêche l'exécution de shellcode injecté. Le canary empêche l'écrasement de l'adresse de retour. Ensemble, ils forcent l'attaquant vers des techniques avancées (ROP + information leak pour contourner ASLR + canary).

**Toutes les protections combinées** — C'est l'état de notre variante `vuln_max_protection`. Un binaire avec canary + PIE + NX + Full RELRO (section suivante) offre une surface d'attaque minimale. L'analyse dynamique reste possible, mais les techniques naïves de patching et de prédiction d'adresses ne fonctionnent pas.

### Vérification globale avec `checksec`

Notre Makefile produit deux variantes extrêmes qui illustrent le contraste :

```bash
$ checksec --file=build/vuln_max_protection
    Arch:     amd64-64-little
    RELRO:    Full RELRO
    Stack:    Canary found
    NX:       NX enabled
    PIE:      PIE enabled
    FORTIFY:  Enabled

$ checksec --file=build/vuln_min_protection
    Arch:     amd64-64-little
    RELRO:    No RELRO
    Stack:    No canary found
    NX:       NX disabled
    PIE:      No PIE
```

Le premier binaire est durci au maximum. Le second est volontairement laissé sans aucune protection — c'est un binaire pédagogique, pas un modèle à suivre en production.

## Pour le reverse engineer : ce qui compte vraiment

En résumé, voici l'impact pratique de chaque protection sur une session de RE typique :

| Protection | Impact sur l'analyse statique | Impact sur l'analyse dynamique |  
|---|---|---|  
| Stack canary | Pattern `fs:0x28` reconnaissable, à ignorer mentalement | Empêche le patching de la pile au-delà du buffer |  
| NX | Aucun | Empêche l'injection et l'exécution de code custom en mémoire |  
| ASLR | Aucun | Adresses non reproductibles entre les sessions |  
| PIE | Adresses en offsets relatifs, `lea [rip+...]` omniprésent | Adresse de base du binaire non prévisible |

La bonne nouvelle : aucune de ces protections ne rend le code illisible. Contrairement à l'obfuscation (sections 19.3–19.4), elles ne transforment pas la logique du programme. Le code assembleur est le même avec ou sans canary — il y a juste quelques instructions en plus dans le prologue et l'épilogue. Le code est le même avec ou sans PIE — les `lea [rip+offset]` sont un peu moins lisibles que les adresses absolues, mais Ghidra les résout automatiquement.

Ces protections sont des obstacles pour l'exploitation, pas pour la compréhension. L'analyste RE les note lors du triage (avec `checksec`), adapte ses outils en conséquence, et poursuit son travail.

---


⏭️ [RELRO : Partial vs Full et impact sur la table GOT/PLT](/19-anti-reversing/06-relro-got-plt.md)
