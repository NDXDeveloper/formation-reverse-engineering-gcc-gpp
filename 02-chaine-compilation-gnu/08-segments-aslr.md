🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.8 — Mappage des segments, ASLR et adresses virtuelles : pourquoi les adresses bougent

> 🎯 **Objectif de cette section** : Comprendre le mécanisme de mémoire virtuelle qui sous-tend le chargement d'un binaire ELF, maîtriser le fonctionnement de l'ASLR et ses conséquences sur le reverse engineering, et savoir travailler avec des adresses qui changent à chaque exécution.

---

## Adresses virtuelles vs adresses physiques

Quand vous lisez `0x55a3c4001149` dans GDB ou dans `/proc/<pid>/maps`, cette adresse n'existe pas en tant que telle dans la RAM physique. C'est une **adresse virtuelle** — une abstraction fournie par le processeur (via la MMU — *Memory Management Unit*) et gérée par le noyau.

Chaque processus possède son propre **espace d'adressage virtuel** : un espace de 2⁴⁸ octets (256 To) sur x86-64 dont seule une fraction est effectivement mappée. Deux processus différents peuvent utiliser la même adresse virtuelle `0x7ffff7e00000` — elle pointera vers des pages physiques différentes en RAM. C'est le fondement de l'isolation entre processus.

Le noyau maintient une **table des pages** pour chaque processus, qui traduit les adresses virtuelles en adresses physiques. L'unité de mappage est la **page** — typiquement 4 Ko sur x86-64. Quand le processeur accède à une adresse virtuelle, la MMU consulte la table des pages pour trouver la page physique correspondante. Si la page n'est pas mappée, le processeur déclenche un **page fault** que le noyau traite (soit en chargeant la page depuis le fichier, soit en envoyant un `SIGSEGV` si l'accès est illégitime).

Pour le reverse engineer, la conséquence pratique est la suivante : **toutes les adresses que vous manipulez sont virtuelles**. Les adresses dans le désassemblage, les adresses dans GDB, les adresses dans `/proc/<pid>/maps` — tout est virtuel. La traduction vers la RAM physique est transparente et hors de votre contrôle (sauf dans des scénarios d'exploitation noyau).

## Segments et mappage : du fichier ELF à la mémoire

### La program header table revisitée

En section 2.7, nous avons vu que le noyau utilise la **program header table** (vue en segments) pour savoir quoi charger en mémoire. Détaillons maintenant le mécanisme de mappage proprement dit.

Chaque entrée `PT_LOAD` dans la program header table spécifie :

| Champ | Signification |  
|---|---|  
| `p_offset` | Offset de début dans le fichier ELF |  
| `p_vaddr` | Adresse virtuelle souhaitée en mémoire |  
| `p_filesz` | Nombre d'octets à lire depuis le fichier |  
| `p_memsz` | Taille de la zone mémoire à allouer |  
| `p_flags` | Permissions (`PF_R`, `PF_W`, `PF_X`) |  
| `p_align` | Alignement requis (généralement taille de page : `0x1000`) |

Le noyau appelle `mmap` pour chaque segment `PT_LOAD`, en projetant la portion `[p_offset, p_offset + p_filesz)` du fichier aux adresses virtuelles `[p_vaddr, p_vaddr + p_memsz)`. Si `p_memsz > p_filesz`, les octets supplémentaires sont remplis de zéros — c'est le mécanisme qui implémente la section `.bss` en mémoire.

### Binaire non-PIE : adresses fixes

Pour un binaire compilé sans PIE (`gcc -no-pie`), le type ELF est `ET_EXEC` et les adresses virtuelles dans la program header table sont **absolues**. Le noyau mappe les segments exactement aux adresses demandées :

```bash
gcc -no-pie -o hello_nopie hello.c  
readelf -l hello_nopie | grep LOAD  
# LOAD  0x000000 0x0000000000400000 ... R   0x1000
# LOAD  0x001000 0x0000000000401000 ... R E 0x1000
# LOAD  0x002000 0x0000000000402000 ... R   0x1000
# LOAD  0x002e10 0x0000000000403e10 ... RW  0x1000
```

Le segment de code est toujours à `0x401000`, les données à `0x403e10`, d'une exécution à l'autre. Le point d'entrée est une adresse fixe comme `0x401050`.

C'est simple et prévisible — et c'est exactement le problème du point de vue de la sécurité.

### Binaire PIE : adresses relatives

Pour un binaire PIE (`ET_DYN`, le défaut moderne), les adresses virtuelles dans la program header table sont des **offsets relatifs à une base de chargement** qui sera choisie au runtime :

```bash
gcc -o hello_pie hello.c    # PIE par défaut  
readelf -l hello_pie | grep LOAD  
# LOAD  0x000000 0x0000000000000000 ... R   0x1000
# LOAD  0x001000 0x0000000000001000 ... R E 0x1000
# LOAD  0x002000 0x0000000000002000 ... R   0x1000
# LOAD  0x002db8 0x0000000000003db8 ... RW  0x1000
```

Les `p_vaddr` commencent à `0x0` — ce ne sont pas des adresses réelles mais des offsets. Au chargement, le noyau choisit une adresse de base (par exemple `0x55a3c4000000`) et ajoute cette base à chaque offset :

```
Adresse en mémoire = base_de_chargement + p_vaddr
                   = 0x55a3c4000000     + 0x1000
                   = 0x55a3c4001000
```

C'est cette base qui change à chaque exécution quand l'ASLR est actif.

## ASLR — Address Space Layout Randomization

### Principe

L'ASLR est une technique de sécurité qui **randomise l'adresse de base** à laquelle chaque composant est chargé en mémoire. À chaque exécution, la pile, le tas, les bibliothèques partagées et le binaire principal (s'il est PIE) sont placés à des adresses différentes.

L'objectif est de rendre l'exploitation des vulnérabilités mémoire (buffer overflows, use-after-free, etc.) beaucoup plus difficile. Un attaquant qui connaît une vulnérabilité doit aussi connaître les adresses des fonctions ou gadgets qu'il veut cibler — si ces adresses changent à chaque exécution, une attaque fiable nécessite d'abord une **fuite d'adresse** (*address leak*).

### Niveaux d'ASLR sous Linux

Le noyau Linux contrôle l'ASLR via le paramètre `/proc/sys/kernel/randomize_va_space` :

| Valeur | Comportement |  
|---|---|  
| `0` | ASLR désactivé — tout est à adresse fixe |  
| `1` | ASLR partiel — pile, bibliothèques, vDSO et mmap randomisés. Le binaire principal et le tas ne le sont pas |  
| `2` | ASLR complet (défaut) — pile, bibliothèques, vDSO, mmap **et tas** (via `brk`) randomisés. Le binaire principal est randomisé seulement s'il est PIE |

```bash
# Vérifier le niveau d'ASLR actuel
cat /proc/sys/kernel/randomize_va_space
# 2  (défaut sur la plupart des distributions)
```

### Ce qui est randomisé, ce qui ne l'est pas

| Composant | Non-PIE (`ET_EXEC`) | PIE (`ET_DYN`) |  
|---|---|---|  
| Binaire principal (`.text`, `.data`, `.got`…) | ❌ Fixe | ✅ Randomisé |  
| Bibliothèques partagées (`libc.so`, etc.) | ✅ Randomisé | ✅ Randomisé |  
| Pile (*stack*) | ✅ Randomisé | ✅ Randomisé |  
| Tas (*heap*, `brk`/`mmap`) | ✅ Randomisé (niveau 2) | ✅ Randomisé |  
| vDSO | ✅ Randomisé | ✅ Randomisé |  
| Loader `ld.so` | ✅ Randomisé | ✅ Randomisé |

Ce tableau révèle un point crucial : **un binaire non-PIE avec ASLR activé offre une protection incomplète**. Le code et les données du binaire principal restent à des adresses fixes — seules les bibliothèques et la pile bougent. C'est pourquoi les distributions modernes compilent tout en PIE par défaut.

### Observer l'ASLR en action

Exécutons notre programme deux fois et comparons les adresses :

```bash
# Avec ASLR activé (défaut)
cat /proc/sys/kernel/randomize_va_space
# 2

# Première exécution
LD_SHOW_AUXV=1 ./hello RE-101 2>&1 | grep AT_ENTRY
# AT_ENTRY: 0x5603a4001060

# Deuxième exécution
LD_SHOW_AUXV=1 ./hello RE-101 2>&1 | grep AT_ENTRY
# AT_ENTRY: 0x55b7e8c01060

# Les adresses de base diffèrent : 0x5603a4000000 vs 0x55b7e8c00000
# Mais l'offset est le même : 0x1060
```

On constate que la partie haute de l'adresse change (`0x5603a4` → `0x55b7e8c`) mais que l'**offset** (`0x1060`) reste identique. C'est la clé : les outils de RE travaillent sur les offsets, qui sont stables.

### Entropie de l'ASLR

La sécurité de l'ASLR dépend du nombre de bits aléatoires dans l'adresse de base — son **entropie**. Sur x86-64 avec Linux, l'entropie typique est :

| Composant | Bits d'entropie (approx.) | Positions possibles |  
|---|---|---|  
| Binaire PIE | 28 bits | ~268 millions |  
| Bibliothèques partagées (`mmap`) | 28 bits | ~268 millions |  
| Pile | 22 bits | ~4 millions |  
| Tas (`brk`) | 13 bits | ~8 000 |

L'entropie de la pile et surtout du tas est nettement plus faible. C'est pourquoi certaines techniques d'exploitation ciblent le tas en brute-force — avec seulement 8 000 positions possibles, quelques milliers de tentatives suffisent statistiquement.

Sur x86 32 bits, l'entropie est beaucoup plus faible (souvent 8 bits pour les bibliothèques — 256 positions), rendant le brute-force trivial. C'est l'une des raisons pour lesquelles les systèmes 64 bits sont significativement plus résistants à ce type d'attaque.

## Travailler avec l'ASLR en RE

### Désactiver l'ASLR pour le débogage

Pendant l'analyse dynamique, l'ASLR peut être gênant : les adresses changent à chaque exécution, les breakpoints sur adresses absolues ne fonctionnent pas d'une session à l'autre. Plusieurs méthodes permettent de le désactiver temporairement :

**Globalement** (déconseillé en dehors d'un lab) :

```bash
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
# Rétablir après :
echo 2 | sudo tee /proc/sys/kernel/randomize_va_space
```

**Pour un seul processus** (méthode recommandée) :

```bash
# Avec setarch
setarch x86_64 -R ./hello RE-101

# Avec le flag personality de GDB
# GDB désactive l'ASLR par défaut pour le processus débogué !
gdb ./hello
(gdb) show disable-randomization
# Disable randomization of debuggee's virtual address space is on.
(gdb) run RE-101
```

GDB désactive l'ASLR par défaut pour le processus qu'il lance (via le flag `ADDR_NO_RANDOMIZE` de l'appel système `personality`). C'est pourquoi les adresses sont reproductibles entre deux sessions GDB. Si vous avez besoin de tester avec ASLR actif dans GDB :

```bash
(gdb) set disable-randomization off
(gdb) run
```

### Calculer les offsets

La stratégie fondamentale pour travailler avec l'ASLR est de raisonner en **offsets** plutôt qu'en adresses absolues :

```
offset = adresse_runtime - base_de_chargement
```

L'offset d'une fonction ou d'une donnée est identique dans le fichier ELF sur disque et en mémoire (c'est le `p_vaddr` de la program header table). C'est cette valeur que les outils comme Ghidra et IDA affichent par défaut pour les binaires PIE.

Pour retrouver la base de chargement à runtime :

```bash
# Dans GDB (avec GEF ou pwndbg)
gef> vmmap
# ou
pwndbg> vmmap

# Depuis /proc
grep hello /proc/$(pidof hello)/maps | head -1
# 55a3c4000000-55a3c4001000 r--p 00000000 ...  /home/user/hello
# ^^^^^^^^^^^^ base de chargement
```

Pour convertir un offset Ghidra en adresse runtime :

```
adresse_runtime = base_de_chargement + offset_ghidra
```

Par exemple, si Ghidra montre la fonction `check` à l'offset `0x1149` et que la base de chargement dans GDB est `0x55a3c4000000` :

```bash
(gdb) break *0x55a3c4001149
# ou plus simplement, si les symboles sont disponibles :
(gdb) break check
```

Les extensions GDB comme GEF et pwndbg automatisent ces calculs avec des commandes comme `pie breakpoint` et `pie run` (Chapitre 12).

### Le cas des bibliothèques partagées

Les bibliothèques partagées sont toujours chargées à des adresses aléatoires, que le binaire principal soit PIE ou non. Pour trouver l'adresse de base d'une bibliothèque :

```bash
# Dans GDB
(gdb) info sharedlibrary
# From                To                  Syms Read   Shared Object Library
# 0x00007f8c3a228000  0x00007f8c3a3bd000  Yes         /lib/.../libc.so.6

# Depuis /proc
grep libc /proc/$(pidof hello)/maps | head -1
# 7f8c3a200000-7f8c3a228000 r--p ...  /lib/.../libc.so.6
```

Pour poser un breakpoint sur une fonction de bibliothèque (par exemple `strcmp` dans la libc), utilisez simplement son nom — GDB résout l'adresse automatiquement grâce aux symboles dynamiques :

```bash
(gdb) break strcmp
# Breakpoint 1 at 0x7f8c3a2xxxx
```

## Les protections mémoire des segments

Le mappage des segments ne concerne pas uniquement les adresses — les **permissions** sont tout aussi importantes. Le noyau configure la MMU pour appliquer les permissions de chaque page :

| Permission | Signification | Violation → signal |  
|---|---|---|  
| `R` (Read) | La mémoire peut être lue | Lecture d'une page non-R → `SIGSEGV` |  
| `W` (Write) | La mémoire peut être écrite | Écriture sur une page non-W → `SIGSEGV` |  
| `X` (Execute) | La mémoire peut être exécutée comme code | Exécution d'une page non-X → `SIGSEGV` |

### Le principe W⊕X (NX bit)

Le principe **W⊕X** (*Write XOR Execute*, aussi appelé **NX** — *No eXecute*, ou **DEP** — *Data Execution Prevention* sous Windows) stipule qu'une page mémoire ne devrait jamais être à la fois inscriptible **et** exécutable. En pratique :

- Le code (`.text`) est `R-X` : exécutable mais pas inscriptible. Impossible d'injecter du code en écrivant dans `.text`.  
- Les données (`.data`, `.bss`, pile, tas) sont `RW-` : inscriptibles mais pas exécutables. Impossible d'exécuter du code injecté dans un buffer.  
- Les données en lecture seule (`.rodata`) sont `R--` : ni inscriptibles, ni exécutables.

Cette protection est implémentée matériellement via le **bit NX** (No eXecute) du processeur, présent sur tous les processeurs x86-64 modernes. Le noyau Linux l'active par défaut.

```bash
# Vérifier NX avec checksec
checksec --file=hello
# NX:  NX enabled
```

### Conséquences en RE et en exploitation

Le W⊕X empêche les attaques classiques de type **shellcode injection** : même si un attaquant parvient à écrire du code machine dans un buffer sur la pile ou le tas, ce code ne peut pas être exécuté car la zone mémoire n'a pas la permission `X`.

C'est pourquoi les techniques d'exploitation modernes contournent W⊕X par des approches de **code reuse** : au lieu d'injecter du nouveau code, elles réutilisent des fragments de code existant déjà dans les zones `R-X` — c'est le principe du **ROP** (*Return-Oriented Programming*, Chapitre 12, section 12.3) et du **ret2libc**.

La GOT (`.got.plt`) est dans un segment `RW-` — inscriptible mais pas exécutable. C'est ce qui rend les attaques de type **GOT overwrite** possibles : on écrase un pointeur de fonction dans la GOT (écriture autorisée) pour rediriger un appel existant via PLT (exécution du code à l'adresse écrite). La protection **Full RELRO** (section 2.9, Chapitre 19) contrecarre cette attaque en rendant la GOT `R--` après la résolution initiale.

## La page zéro — Le piège à NULL

L'adresse `0x0` (et généralement toute la première page, `0x0`–`0xFFF`) n'est **jamais mappée**. Tout accès à cette zone déclenche un `SIGSEGV`. C'est une protection intentionnelle : le déréférencement d'un pointeur NULL produit un crash immédiat et détectable, au lieu de corrompre silencieusement des données.

```bash
# Vérifier que la page zéro n'est pas mappée
head -1 /proc/$(pidof hello)/maps
# L'adresse de départ est bien au-dessus de 0x0
```

Cette protection est configurable via `/proc/sys/vm/mmap_min_addr` (généralement `65536` — les 64 premières Ko sont interdites). Historiquement, certaines vulnérabilités noyau exploitaient le mappage de la page zéro ; cette restriction empêche les exploits de type *NULL pointer dereference*.

## Résumé des adresses typiques sur x86-64

Pour vous donner un repère mental, voici les plages d'adresses que vous rencontrerez fréquemment en RE sous Linux x86-64. Ces valeurs sont indicatives et varient avec l'ASLR :

| Composant | Plage typique (avec ASLR) | Plage typique (sans ASLR / non-PIE) |  
|---|---|---|  
| Binaire PIE | `0x55XX_XXXX_X000` – … | — |  
| Binaire non-PIE | `0x0040_0000` – `0x004X_XXXX` | Fixe |  
| Tas (heap / brk) | Juste après le binaire | Juste après le binaire |  
| Bibliothèques `.so` | `0x7fXX_XXXX_X000` – … | `0x7fXX_XXXX_X000` – … |  
| Loader `ld.so` | `0x7fXX_XXXX_X000` – … | `0x7fXX_XXXX_X000` – … |  
| Pile (stack) | `0x7fXX_XXXX_X000` (haut de la pile) | `0x7fff_XXXX_XXXX` |  
| vDSO | Proche de la pile | Proche de la pile |  
| Noyau (inaccessible en user) | `0xFFFF_8XXX_XXXX_XXXX` | `0xFFFF_8XXX_XXXX_XXXX` |

Un réflexe utile : quand vous voyez une adresse commençant par `0x55`, c'est probablement le binaire PIE. Une adresse commençant par `0x7f` est probablement une bibliothèque partagée, le loader ou la pile. Une adresse commençant par `0x40` est un binaire non-PIE.

## Impact sur le workflow RE

La combinaison PIE + ASLR influence votre workflow à chaque étape :

**Analyse statique** (Ghidra, IDA, objdump) : les outils affichent les offsets relatifs à la base `0x0` pour les binaires PIE. Ces offsets sont stables et correspondent exactement à ce que vous trouverez en mémoire (modulo l'ajout de la base). Aucun impact négatif — l'analyse statique est indépendante de l'ASLR.

**Analyse dynamique** (GDB, Frida, strace) : les adresses réelles dépendent de la base de chargement, qui change à chaque exécution (sauf si vous désactivez l'ASLR ou utilisez GDB qui le fait par défaut). Vous devez soit travailler avec des noms de symboles, soit calculer les adresses runtime à partir des offsets connus.

**Scripting et automatisation** (pwntools, angr) : les frameworks d'exploitation comme `pwntools` gèrent l'ASLR de manière explicite. Vous travaillez avec des offsets jusqu'au moment de l'exploitation, où vous injectez une adresse runtime calculée à partir d'une fuite d'adresse. Le module `ELF` de pwntools facilite ces calculs :

```python
from pwn import *  
elf = ELF('./hello')  
# elf.symbols['check'] → offset de check (ex: 0x1149)
# À runtime, adresse = base + elf.symbols['check']
```

> 💡 **Règle d'or** : En RE, pensez toujours en offsets. Notez les offsets dans vos annotations Ghidra, vos scripts et vos rapports. Les adresses absolues ne sont valides que pour une exécution donnée ; les offsets sont permanents.

---

> 📖 **Nous comprenons maintenant comment les segments sont mappés en mémoire et pourquoi les adresses bougent.** Il reste un dernier mécanisme essentiel à comprendre : comment les appels aux fonctions de bibliothèques partagées sont résolus à travers le couple PLT/GOT. C'est l'objet de la dernière section de ce chapitre.  
>  
> → 2.9 — Résolution dynamique des symboles : PLT/GOT en détail (lazy binding)

⏭️ [Résolution dynamique des symboles : PLT/GOT en détail (lazy binding)](/02-chaine-compilation-gnu/09-plt-got-lazy-binding.md)
