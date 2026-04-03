🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 12.3 — Recherche de gadgets ROP depuis GDB

> **Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg**  
> **Partie III — Analyse Dynamique**

---

## Qu'est-ce qu'un gadget ROP et pourquoi le chercher en RE ?

Le Return-Oriented Programming (ROP) est une technique qui réutilise des fragments de code déjà présents dans un binaire ou ses bibliothèques — appelés **gadgets** — pour construire un flux d'exécution arbitraire sans injecter de nouveau code. Chaque gadget est une courte séquence d'instructions machine qui se termine par un `ret` (ou parfois un `jmp reg` / `call reg`). En enchaînant les adresses de ces gadgets sur la pile, un attaquant peut contourner la protection NX (No eXecute) qui empêche l'exécution de code injecté dans la pile ou le tas.

Du point de vue du reverse engineer, la recherche de gadgets ROP est pertinente dans plusieurs contextes légitimes. Lors d'un **audit de sécurité**, identifier les gadgets disponibles dans un binaire permet d'évaluer sa surface d'attaque résiduelle même en présence de NX. Dans un **CTF**, la résolution de challenges d'exploitation passe quasi systématiquement par la construction de chaînes ROP. Pour la **compréhension d'un exploit existant**, savoir lire et rechercher des gadgets est indispensable pour analyser les rapports de vulnérabilités et les preuves de concept publiées.

Les extensions GDB intègrent des commandes de recherche de gadgets directement dans le débogueur, ce qui offre un avantage par rapport aux outils externes : la recherche s'effectue sur la **mémoire effective du processus** au moment de l'exécution, incluant les bibliothèques chargées dynamiquement et tenant compte de la résolution ASLR.

---

## Rappel : anatomie d'un gadget

Un gadget est une séquence d'instructions consécutives dans le segment `.text` (ou tout segment exécutable) qui se termine par une instruction de transfert de contrôle. L'instruction de terminaison la plus courante est `ret`, qui dépile une adresse de la pile et y saute. Voici quelques exemples de gadgets typiques :

```nasm
; Gadget "pop rdi ; ret" — charge une valeur de la pile dans RDI
pop rdi  
ret  

; Gadget "pop rsi ; pop r15 ; ret" — charge deux valeurs, une utile (RSI), une sacrifiée (R15)
pop rsi  
pop r15  
ret  

; Gadget "xor eax, eax ; ret" — met RAX à zéro
xor eax, eax  
ret  

; Gadget "syscall ; ret" — déclenche un appel système
syscall  
ret  
```

Le gadget `pop rdi ; ret` est probablement le plus recherché sur x86-64 : il permet de placer une valeur contrôlée dans `RDI`, premier argument des fonctions dans la convention System V AMD64. Combiné avec l'adresse de `system@plt` et un pointeur vers la chaîne `"/bin/sh"`, il suffit à obtenir un shell — le scénario classique `ret2libc`.

Un point crucial est que les gadgets ne correspondent pas nécessairement à des frontières d'instructions légitimes du programme original. Le désassembleur linéaire d'`objdump` découpe les octets à partir du début de chaque fonction, mais le jeu d'instructions x86-64 est de longueur variable. En commençant le décodage au milieu d'une instruction multi-octets, on peut obtenir une séquence d'instructions valide totalement différente, terminée par un `ret`, qui constitue un gadget exploitable. C'est ce qu'on appelle les **gadgets non alignés** (*unaligned gadgets*), et c'est l'une des raisons pour lesquelles les outils de recherche de gadgets parcourent les octets un par un plutôt que de se fier au désassemblage officiel.

---

## Recherche de gadgets avec PEDA

PEDA a été la première extension à intégrer une commande de recherche de gadgets. La commande `ropgadget` effectue un scan de la mémoire exécutable du processus :

```
gdb-peda$ ropgadget
```

Sans argument, `ropgadget` cherche dans le binaire principal et affiche tous les gadgets trouvés, triés par adresse. La sortie peut être volumineuse — un binaire lié dynamiquement de taille modeste contient facilement des centaines de gadgets, et si les bibliothèques partagées sont incluses, le nombre se compte en milliers.

Pour filtrer la recherche sur un pattern spécifique :

```
gdb-peda$ ropgadget "pop rdi"
```

Cette commande cherche tous les gadgets contenant la séquence `pop rdi`. PEDA affiche l'adresse et le désassemblage complet du gadget :

```
0x00005555555551a3 : pop rdi ; ret
0x00007ffff7e1b2a1 : pop rdi ; pop rbp ; ret
```

PEDA propose également la commande `ropsearch` pour une recherche plus ciblée par expression régulière sur le désassemblage :

```
gdb-peda$ ropsearch "pop r?i" binary  
gdb-peda$ ropsearch "pop rdi" libc  
```

Le deuxième argument spécifie la cible de recherche : `binary` pour le binaire principal, `libc` pour la bibliothèque C, ou une plage d'adresses explicite.

La commande `dumprop` de PEDA va plus loin en listant les gadgets les plus utiles dans un format organisé par fonction (gadgets de contrôle de registres, gadgets d'écriture mémoire, gadgets de saut) :

```
gdb-peda$ dumprop
```

Les limites de PEDA dans ce domaine sont principalement liées à la profondeur de recherche (nombre d'instructions maximal par gadget) et à l'absence de classification automatique avancée.

---

## Recherche de gadgets avec GEF

GEF ne contient pas de moteur de recherche de gadgets intégré dans son code de base. Il délègue cette fonctionnalité à l'outil externe **ropper**, qu'il invoque de manière transparente depuis le prompt GDB.

Si ropper est installé (voir section 12.1), la commande `ropper` est disponible directement dans GEF :

```
gef➤ ropper --search "pop rdi"
```

GEF passe automatiquement le contexte du binaire en cours de débogage à ropper, qui effectue la recherche et affiche les résultats dans le terminal GDB :

```
[INFO] Searching for gadgets: pop rdi
0x00005555555551a3: pop rdi; ret;
```

L'avantage de cette intégration est de bénéficier de toute la puissance de ropper (recherche de gadgets non alignés, support de multiples formats, filtrage avancé) sans quitter le débogueur. Les arguments acceptés sont les mêmes que ceux de ropper en ligne de commande.

Pour chercher dans une bibliothèque spécifique chargée par le processus :

```
gef➤ ropper --file /lib/x86_64-linux-gnu/libc.so.6 --search "pop rdi"
```

GEF propose également la commande `scan` pour chercher des séquences d'octets arbitraires dans les segments exécutables du processus, ce qui peut servir à localiser manuellement un opcode précis :

```
gef➤ scan executable 0x5f 0xc3
```

Cet exemple cherche la séquence d'octets `5f c3` (soit `pop rdi ; ret` en encodage x86-64). La commande retourne toutes les adresses où cette séquence apparaît dans la mémoire exécutable. C'est une approche plus bas niveau que la recherche par désassemblage, mais utile pour confirmer l'existence d'un gadget ou chercher des encodages exotiques.

---

## Recherche de gadgets avec pwndbg

pwndbg intègre sa propre commande `rop` :

```
pwndbg> rop --grep "pop rdi"
```

Cette commande parcourt la mémoire exécutable du processus, désassemble les séquences terminées par `ret` (et optionnellement `jmp` ou `call`), et filtre les résultats selon le pattern fourni.

Sans filtre, `rop` produit la liste complète des gadgets trouvés. Les résultats sont paginés pour éviter de noyer le terminal :

```
pwndbg> rop
```

pwndbg permet de restreindre la recherche à un module spécifique grâce à la commande `rop` combinée avec les informations du mapping mémoire :

```
pwndbg> rop --module-name keygenme  
pwndbg> rop --module-name libc  
```

### Recherche par registre cible

Un besoin fréquent est de trouver tous les gadgets qui contrôlent un registre donné. pwndbg ne propose pas de filtre sémantique natif pour cela, mais le filtrage par grep couvre la plupart des cas :

```
pwndbg> rop --grep "pop rdi"  
pwndbg> rop --grep "mov rdi"  
pwndbg> rop --grep "xchg .*, rdi"  
```

En enchaînant ces recherches, on construit progressivement un inventaire des moyens de contrôler chaque registre d'argument (`rdi`, `rsi`, `rdx`, `rcx`).

### La commande `ropper` dans pwndbg

En complément de sa commande `rop` native, pwndbg peut également invoquer ropper s'il est installé, de la même manière que GEF :

```
pwndbg> ropper -- --search "pop rdi; ret"
```

Le double tiret sépare les arguments de pwndbg de ceux passés à ropper. Cette redondance donne le choix entre le moteur interne (plus rapide, moins de fonctionnalités) et ropper (plus lent, plus complet).

---

## Recherche dans la mémoire effective vs recherche statique

Un avantage fondamental de la recherche de gadgets depuis GDB par rapport aux outils statiques (`ROPgadget`, `ropper` en standalone, `r2` avec `/R`) est l'accès à la **mémoire du processus en cours d'exécution**.

Lorsque le binaire est compilé avec PIE (Position Independent Executable) — ce qui est le cas par défaut avec les GCC récents — les adresses affichées par un outil statique sont des offsets relatifs. Pour obtenir les adresses absolues utilisables dans un exploit, il faut connaître l'adresse de base à laquelle le binaire a été chargé, ce qui dépend de l'ASLR. Depuis GDB, les gadgets sont directement affichés avec leurs adresses effectives après relocation, prêtes à l'emploi.

De même, les bibliothèques partagées (libc, libpthread, etc.) ne sont présentes en mémoire qu'après le chargement dynamique. Un outil statique peut analyser le fichier `.so` séparément, mais seul un outil opérant au runtime voit l'ensemble de l'espace d'adressage — binaire principal, bibliothèques, vDSO, stack — d'un seul tenant.

Pour visualiser le mapping mémoire et comprendre dans quelle région se trouve un gadget :

```
# GEF
gef➤ vmmap

# pwndbg
pwndbg> vmmap

# PEDA
gdb-peda$ vmmap
```

Cette commande affiche les régions mémoire avec leurs permissions (rwx). Les gadgets ne peuvent se trouver que dans les régions marquées avec le flag `x` (exécutable). Si un binaire est compilé avec Full RELRO et PIE, les segments exécutables se réduisent au `.text` du binaire et au `.text` des bibliothèques — le nombre de gadgets disponibles est limité par cette surface.

---

## Outils externes complémentaires

Les commandes intégrées aux extensions GDB sont pratiques pour une recherche rapide en cours de session de débogage, mais pour une analyse exhaustive des gadgets disponibles, les outils dédiés restent plus performants.

**ROPgadget** est un outil Python standalone qui effectue une recherche approfondie, incluant les gadgets non alignés, le chaînage automatique et la génération de ropchains complètes pour des scénarios courants (`execve`, `mprotect`) :

```bash
ROPgadget --binary ./keygenme_O0 --ropchain
```

**ropper** offre des fonctionnalités similaires avec un accent sur la performance et le support de formats variés (ELF, PE, Mach-O, raw) :

```bash
ropper --file ./keygenme_O0 --search "pop rdi"  
ropper --file /lib/x86_64-linux-gnu/libc.so.6 --search "pop rdi"  
```

**Radare2** propose la commande `/R` pour la recherche de gadgets dans son propre contexte, que nous verrons au chapitre 9 :

```bash
r2 -q -c '/R pop rdi' ./keygenme_O0
```

La stratégie recommandée est d'utiliser les commandes GDB intégrées pour des recherches ponctuelles et contextuelles pendant le débogage, et de basculer vers les outils externes pour une analyse complète ou pour la génération automatique de chaînes ROP.

---

## Interaction entre gadgets ROP et protections du binaire

La recherche de gadgets ne se fait pas dans le vide — elle doit tenir compte des protections compilées dans le binaire. Rappelons les protections pertinentes (vues avec `checksec`, section 5.6, et approfondies au chapitre 19) :

**NX (No eXecute)** est la protection qui rend le ROP nécessaire en premier lieu. Sans NX, un attaquant pourrait simplement injecter du shellcode sur la pile et l'exécuter. Avec NX activé, la pile et le tas sont non exécutables, ce qui force le recours à la réutilisation de code existant via ROP.

**ASLR (Address Space Layout Randomization)** randomise les adresses de base des bibliothèques et, si PIE est activé, du binaire principal. Les gadgets ont des adresses différentes à chaque exécution. L'exploitation ROP en présence d'ASLR nécessite soit une fuite d'adresse (*information leak*) pour calculer la base, soit l'utilisation exclusive de gadgets dans le binaire principal si PIE est désactivé (puisque celui-ci est alors chargé à une adresse fixe).

**PIE (Position Independent Executable)** soumet le binaire principal lui-même à l'ASLR. Lorsque PIE est actif, même les gadgets du binaire principal ont des adresses aléatoires. Vérifier le statut PIE est donc la première étape avant de construire une chaîne ROP :

```
gef➤ checksec
```

**Stack canaries** ne bloquent pas le ROP en soi, mais ils protègent contre le débordement de buffer qui est le vecteur habituel pour écraser l'adresse de retour et déclencher la chaîne ROP. Un canary corrompu provoque l'appel à `__stack_chk_fail` avant que le `ret` n'atteigne le premier gadget. Contourner un canary nécessite soit de le fuiter, soit de trouver un vecteur d'écriture qui n'écrase pas le canary (écriture arbitraire hors buffer linéaire, format string, etc.).

Les extensions GDB permettent de visualiser ces protections et de comprendre leur impact sur la faisabilité d'une chaîne ROP, le tout sans quitter le débogueur — ce qui fait le lien entre la recherche de gadgets et l'analyse globale du binaire.

---

## Méthodologie de recherche en pratique

Plutôt que de chercher des gadgets au hasard, une approche méthodique part de l'objectif à atteindre et remonte vers les gadgets nécessaires.

**Étape 1 — Identifier l'objectif.** Que veut-on faire ? Appeler `system("/bin/sh")` ? Invoquer `execve` via un syscall ? Appeler `mprotect` pour rendre une page exécutable puis y sauter ? L'objectif détermine quels registres doivent être contrôlés et avec quelles valeurs.

**Étape 2 — Lister les registres à contrôler.** Pour `system("/bin/sh")`, il faut `RDI = adresse de "/bin/sh"`. Pour un syscall `execve`, il faut `RAX = 0x3b`, `RDI = adresse de "/bin/sh"`, `RSI = 0`, `RDX = 0`. Chaque registre nécessite un gadget de type `pop reg ; ret` ou équivalent.

**Étape 3 — Chercher les gadgets correspondants.**

```
pwndbg> rop --grep "pop rdi"  
pwndbg> rop --grep "pop rsi"  
pwndbg> rop --grep "pop rdx"  
pwndbg> rop --grep "pop rax"  
pwndbg> rop --grep "syscall"  
```

**Étape 4 — Vérifier les effets de bord.** Un gadget `pop rsi ; pop r15 ; ret` contrôle `RSI` mais consomme un slot de pile supplémentaire pour `R15`. Ce n'est pas un problème — il suffit de placer une valeur quelconque à cet emplacement — mais il faut en tenir compte dans la construction de la chaîne. Lire attentivement le désassemblage complet de chaque gadget, pas seulement la partie filtrée.

**Étape 5 — Localiser les données.** La chaîne `"/bin/sh"` est souvent présente dans la libc elle-même. Pour la trouver depuis GDB :

```
# GEF
gef➤ grep "/bin/sh"

# pwndbg
pwndbg> search --string "/bin/sh"

# PEDA
gdb-peda$ searchmem "/bin/sh"
```

Ces commandes cherchent la chaîne dans toute la mémoire du processus et retournent les adresses trouvées.

**Étape 6 — Assembler la chaîne.** Cet assemblage sort du périmètre de ce chapitre (il relève de l'exploitation proprement dite), mais les informations collectées aux étapes précédentes — adresses des gadgets, adresses des données, mapping mémoire — constituent les briques nécessaires. L'outil `pwntools`, introduit en section 11.9, permet d'automatiser cette construction en Python.

---


⏭️ [Analyse de heap avec pwndbg (`vis_heap_chunks`, `bins`)](/12-gdb-extensions/04-analyse-heap-pwndbg.md)
