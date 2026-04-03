🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 5.6 — `checksec` — inventaire des protections d'un binaire (ASLR, PIE, NX, canary, RELRO)

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Introduction

Au fil des sections précédentes, nous avons déjà croisé plusieurs protections de sécurité sans les nommer explicitement. Avec `readelf -l`, nous avons constaté qu'aucun segment n'était simultanément inscriptible et exécutable (`RWE`) — c'est la protection NX. Avec `readelf -l`, nous avons vu le segment `GNU_RELRO` — c'est le mécanisme RELRO. Avec `file`, nous avons lu `pie executable` — c'est la protection PIE qui permet l'ASLR complet.

Ces protections sont des **mécanismes de défense** intégrés par le compilateur, le linker et le noyau pour rendre l'exploitation de vulnérabilités (buffer overflow, format string, use-after-free…) plus difficile. En reverse engineering, les connaître est essentiel pour deux raisons :

- **En audit de sécurité** : dresser l'inventaire des protections permet d'évaluer la posture de défense d'un binaire et d'identifier les faiblesses exploitables.  
- **En RE pur** : certaines protections modifient le comportement du binaire de manière visible dans le désassemblage. Les stack canaries ajoutent du code dans chaque prologue/épilogue de fonction. Le PIE change le schéma d'adressage. Le RELRO affecte la GOT. Savoir quelles protections sont actives permet de comprendre des patterns qui seraient autrement déroutants.

`checksec` est un script qui automatise la vérification de toutes ces protections en une seule commande. Il interroge les headers ELF, les sections, et les propriétés du binaire pour produire un rapport synthétique.

---

## Installation de `checksec`

`checksec` existe sous plusieurs formes. La version la plus répandue est le script shell standalone, mais la version intégrée à `pwntools` (le framework Python d'exploitation) est tout aussi courante en pratique.

```bash
# Version standalone (script shell)
# Disponible sur la plupart des distributions
$ sudo apt install checksec       # Debian/Ubuntu
$ checksec --file=keygenme_O0

# Version pwntools (Python)
$ pip install pwntools
$ checksec keygenme_O0

# Depuis un script Python pwntools
from pwn import *  
elf = ELF('./keygenme_O0')  
# Les protections sont affichées automatiquement à l'import
```

Les deux versions produisent des résultats identiques. Dans la suite, nous utilisons la syntaxe de la version standalone.

---

## Lecture de la sortie de `checksec`

### Exemple sur notre crackme

```bash
$ checksec --file=keygenme_O0
RELRO           STACK CANARY      NX            PIE             RPATH      RUNPATH      Symbols         FORTIFY Fortified   Fortifiable  FILE  
Full RELRO      Canary found      NX enabled    PIE enabled     No RPATH   No RUNPATH   72 Symbols      No      0           2           keygenme_O0  
```

En une seule ligne, `checksec` résume l'état de chaque protection. Passons-les en revue une par une, en expliquant ce que chacune signifie, comment elle fonctionne, et comment elle se manifeste dans le binaire.

---

## NX (No-eXecute) — la pile et les données ne sont pas exécutables

### Principe

NX (aussi appelé DEP — *Data Execution Prevention* — sous Windows, ou W^X — *Write XOR Execute*) est le principe selon lequel une zone de mémoire ne doit jamais être simultanément **inscriptible et exécutable**. Le code est exécutable mais non modifiable ; les données sont modifiables mais non exécutables.

Sans NX, un attaquant qui parvient à injecter du code sur la pile (via un buffer overflow classique) peut immédiatement l'exécuter, car la pile est à la fois inscriptible (pour les variables locales) et exécutable. Avec NX activé, toute tentative d'exécuter du code depuis la pile déclenche un `SIGSEGV` (segmentation fault).

### Comment `checksec` le vérifie

`checksec` examine le segment `GNU_STACK` dans les program headers :

```bash
$ readelf -lW keygenme_O0 | grep GNU_STACK
  GNU_STACK      0x000000 0x000000 0x000000 0x000000 0x000000 RW  0x10
```

Le flag est `RW` (lisible et inscriptible, **sans** `E` pour exécutable). La pile n'est donc pas exécutable : **NX est activé**.

Si NX était désactivé, on verrait `RWE` :

```bash
# Compilation volontairement sans NX (à des fins de test uniquement)
$ gcc -z execstack -o vuln vuln.c
$ readelf -lW vuln | grep GNU_STACK
  GNU_STACK      0x000000 0x000000 0x000000 0x000000 0x000000 RWE 0x10
```

### Impact sur le RE

NX est activé par défaut depuis des années sur GCC. Son absence dans un binaire moderne est un signal fort : soit le binaire a été compilé intentionnellement avec `-z execstack` (JIT compilers, certains exploits éducatifs), soit c'est un binaire très ancien, soit c'est du self-modifying code.

Dans le désassemblage, NX ne produit aucun code visible — c'est une propriété des segments, pas du code machine. Mais si vous voyez un appel `mprotect` avec les flags `PROT_READ|PROT_WRITE|PROT_EXEC` dans `strace` ou le désassemblage, cela signifie que le programme contourne NX dynamiquement en rendant une zone mémoire exécutable à la volée. C'est un comportement typique des packers et des malwares (chapitre 29).

---

## PIE (Position-Independent Executable) — l'exécutable peut être chargé n'importe où

### Principe

Un binaire PIE est compilé de sorte que tout son code utilise un **adressage relatif** — aucune adresse absolue n'est hardcodée. Cela permet au loader de charger le binaire à une adresse de base aléatoire à chaque exécution, grâce à l'ASLR (Address Space Layout Randomization) du noyau.

Sans PIE, le binaire est chargé à une adresse fixe (typiquement `0x400000` pour un ELF 64 bits non-PIE). Un attaquant connaît donc exactement les adresses de toutes les fonctions et de tous les gadgets ROP. Avec PIE, l'adresse de base change à chaque exécution, ce qui rend les attaques par réutilisation de code beaucoup plus difficiles.

### Comment `checksec` le vérifie

`checksec` regarde le champ `e_type` du ELF header :

```bash
$ readelf -h keygenme_O0 | grep Type
  Type:                              DYN (Position-Independent Executable file)
```

Le type `DYN` indique un binaire PIE (ou une bibliothèque partagée). Un binaire non-PIE aurait le type `EXEC` :

```bash
# Compilation volontairement sans PIE
$ gcc -no-pie -o nopie nopie.c
$ readelf -h nopie | grep Type
  Type:                              EXEC (Executable file)
```

### Impact sur le RE

PIE affecte significativement le travail de reverse :

**Dans le désassemblage statique** : toutes les adresses affichées par `objdump`, Ghidra, ou IDA sont des adresses relatives à la base de chargement (offsets). L'adresse `0x1189` pour `main` n'est pas l'adresse réelle en mémoire — c'est un offset qui sera ajouté à l'adresse de base (aléatoire) au chargement.

**Dans le débogage dynamique** : les adresses effectives changent à chaque exécution. Pour poser un breakpoint sur `main` dans GDB, il faut soit utiliser le nom symbolique (`break main`), soit calculer l'adresse réelle en ajoutant l'offset à l'adresse de base actuelle. GDB gère cela automatiquement dans la plupart des cas, mais c'est une source fréquente de confusion pour les débutants.

**Sur les binaires non-PIE** : les adresses sont fixes et identiques entre le désassemblage statique et l'exécution dynamique. C'est plus simple à analyser, mais c'est aussi moins sécurisé.

### ASLR — le pendant noyau du PIE

PIE est une propriété du **binaire**. L'ASLR est une fonctionnalité du **noyau** qui randomise les adresses de chargement. Les deux sont complémentaires :

| PIE | ASLR noyau | Résultat |  
|---|---|---|  
| Oui | Activé | Adresse du binaire aléatoire à chaque exécution (protection maximale) |  
| Oui | Désactivé | Le binaire est PIE mais chargé à une adresse fixe (ASLR off) |  
| Non | Activé | La pile, le heap et les bibliothèques sont randomisés, mais le binaire lui-même est à adresse fixe |  
| Non | Désactivé | Aucune randomisation (aucune protection) |

Pour vérifier l'état de l'ASLR sur le système :

```bash
$ cat /proc/sys/kernel/randomize_va_space
2
```

La valeur `2` signifie ASLR complet (pile, heap, mmap, bibliothèques). `1` = ASLR partiel (pas le heap). `0` = ASLR désactivé. Pour le RE dynamique, on désactive parfois temporairement l'ASLR pour obtenir des adresses reproductibles :

```bash
# Désactiver l'ASLR pour la session courante (nécessite root)
$ echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

# Ou pour une seule commande (sans root)
$ setarch $(uname -m) -R ./keygenme_O0
```

---

## Stack Canary — détecter les buffer overflows sur la pile

### Principe

Un *stack canary* (aussi appelé *stack guard* ou *stack cookie*) est une valeur aléatoire placée entre les variables locales d'une fonction et l'adresse de retour sauvegardée sur la pile. Avant de retourner, la fonction vérifie que cette valeur n'a pas été modifiée. Si un buffer overflow a écrasé des données au-delà du buffer, il aura aussi écrasé le canary, et la vérification échouera, provoquant un arrêt immédiat du programme (`__stack_chk_fail`).

Le nom « canary » est une référence aux canaris que les mineurs emportaient dans les galeries de charbon pour détecter les gaz toxiques : si le canari mourait, les mineurs savaient qu'il fallait évacuer. De la même manière, si le canary de la pile est corrompu, le programme sait qu'un débordement s'est produit.

### Comment `checksec` le vérifie

`checksec` cherche la présence du symbole `__stack_chk_fail` dans les tables de symboles :

```bash
$ readelf -s keygenme_O0 | grep stack_chk
     8: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __stack_chk_fail@GLIBC_2.4

$ nm -D keygenme_O0 | grep stack_chk
                 U __stack_chk_fail@GLIBC_2.4
```

La présence de ce symbole indique que le binaire contient du code de vérification de canary — donc **Canary found**.

### Impact sur le RE

Les stack canaries sont visibles dans le désassemblage de chaque fonction protégée. GCC insère un **prologue** qui lit la valeur du canary depuis le segment TLS (Thread-Local Storage) et la place sur la pile, et un **épilogue** qui la vérifie avant le `ret` :

```asm
; Prologue — mise en place du canary
push   rbp  
mov    rbp, rsp  
sub    rsp, 0x30  
mov    rax, QWORD PTR fs:[0x28]     ; ← Lecture du canary depuis fs:[0x28]  
mov    QWORD PTR [rbp-0x8], rax     ; ← Stockage sur la pile  
xor    eax, eax  

; ... corps de la fonction ...

; Épilogue — vérification du canary
mov    rax, QWORD PTR [rbp-0x8]     ; ← Relecture du canary depuis la pile  
cmp    rax, QWORD PTR fs:[0x28]     ; ← Comparaison avec la valeur originale  
jne    .Lfail                        ; ← Si différent : overflow détecté !  
leave  
ret  
.Lfail:
call   __stack_chk_fail              ; ← Termine le programme
```

Ce pattern `fs:[0x28]` → stockage → ... → relecture → comparaison → `__stack_chk_fail` est un **idiome GCC** que vous rencontrerez dans pratiquement toutes les fonctions d'un binaire compilé avec `-fstack-protector`. Apprenez à le reconnaître immédiatement — il ne fait pas partie de la logique du programme, c'est du code de protection injecté par le compilateur.

L'accès via `fs:[0x28]` utilise le registre de segment `fs`, qui pointe vers la structure TLS du thread courant. L'offset `0x28` est l'emplacement du canary dans cette structure sur x86-64 Linux. La valeur du canary est générée aléatoirement au démarrage du processus.

### Niveaux de protection

GCC propose plusieurs niveaux de protection par canary :

| Flag de compilation | Comportement |  
|---|---|  
| `-fstack-protector` | Protège uniquement les fonctions qui utilisent des tableaux de caractères (heuristique de GCC). |  
| `-fstack-protector-strong` | Protège les fonctions qui utilisent des tableaux, des variables locales dont l'adresse est prise, ou des appels à `alloca`. C'est le défaut sur les distributions modernes. |  
| `-fstack-protector-all` | Protège **toutes** les fonctions, sans exception. Plus sûr mais plus coûteux en performance. |  
| `-fno-stack-protector` | Désactive complètement les canaries. |

`checksec` ne distingue pas ces niveaux — il indique simplement si des canaries sont présents ou non.

---

## RELRO (Relocation Read-Only) — protéger la GOT

### Principe

La GOT (Global Offset Table) est une table en mémoire qui contient les adresses résolues des fonctions de bibliothèques partagées (voir chapitre 2, section 2.9). Par défaut, cette table est inscriptible car le loader dynamique y écrit les adresses au fur et à mesure que les fonctions sont appelées pour la première fois (lazy binding).

Une GOT inscriptible est une cible de choix pour les attaquants : en écrasant une entrée de la GOT (via un buffer overflow ou une écriture arbitraire), on peut rediriger un appel de fonction vers du code arbitraire. Par exemple, remplacer l'adresse de `printf` dans la GOT par l'adresse de `system` permet d'exécuter des commandes shell la prochaine fois que le programme appelle `printf`.

RELRO (Relocation Read-Only) contre cette attaque en rendant la GOT en lecture seule après la résolution des symboles.

### Partial RELRO vs Full RELRO

Il existe deux niveaux de RELRO, qui offrent des protections très différentes :

**Partial RELRO** (`-Wl,-z,relro`) — le linker réorganise les sections ELF pour placer la GOT après les sections de données inscriptibles, et marque certaines régions (`.init_array`, `.fini_array`, `.dynamic`) comme en lecture seule après le chargement. Cependant, **la partie de la GOT utilisée par la PLT** (`.got.plt`) reste inscriptible, car le lazy binding en a besoin.

**Full RELRO** (`-Wl,-z,relro,-z,now`) — en plus de la réorganisation de Partial RELRO, le flag `-z now` force le loader à résoudre **tous** les symboles immédiatement au chargement (eager binding), au lieu de les résoudre paresseusement à la première utilisation. Une fois toutes les adresses écrites dans la GOT, la **totalité** de la GOT est rendue en lecture seule via `mprotect`. La GOT n'est plus modifiable du tout après l'initialisation.

### Comment `checksec` le vérifie

`checksec` examine la présence du segment `GNU_RELRO` et de l'entrée `BIND_NOW` dans la section `.dynamic` :

```bash
# Vérifier le segment GNU_RELRO
$ readelf -lW keygenme_O0 | grep GNU_RELRO
  GNU_RELRO      0x002db8 0x003db8 0x003db8 0x000248 0x000248 R   0x1

# Vérifier BIND_NOW (distingue Partial de Full)
$ readelf -d keygenme_O0 | grep -E 'BIND_NOW|FLAGS'
 0x0000000000000018 (BIND_NOW)
 0x000000006ffffffb (FLAGS_1)            Flags: NOW PIE
```

La logique est la suivante :

| `GNU_RELRO` présent ? | `BIND_NOW` présent ? | Résultat |  
|---|---|---|  
| Non | — | No RELRO |  
| Oui | Non | Partial RELRO |  
| Oui | Oui | Full RELRO |

### Impact sur le RE

**No RELRO** : la GOT est entièrement inscriptible. Technique d'exploitation classique : GOT overwrite.

**Partial RELRO** : la GOT de la PLT reste inscriptible. Le lazy binding est actif — dans le désassemblage, le premier appel à une fonction passe par le stub PLT qui invoque le resolver, et les appels suivants sautent directement à l'adresse résolue dans la GOT.

**Full RELRO** : la GOT est en lecture seule après l'initialisation. Le lazy binding est désactivé — toutes les adresses sont résolues au chargement. Dans `strace`, on observe un `mprotect` qui rend la région GOT en lecture seule pendant la phase d'initialisation. Les techniques d'exploitation basées sur le GOT overwrite ne fonctionnent plus.

---

## FORTIFY_SOURCE — durcissement des fonctions libc

### Principe

`FORTIFY_SOURCE` est un mécanisme de compilation (`-D_FORTIFY_SOURCE=2` ou `=3`) qui remplace certaines fonctions dangereuses de la libc par des versions sécurisées qui vérifient les tailles de buffers à la compilation et/ou au runtime. Par exemple, `memcpy(dst, src, n)` est remplacé par `__memcpy_chk(dst, src, n, dst_size)` qui vérifie que `n` ne dépasse pas `dst_size`.

### Comment `checksec` le vérifie

`checksec` compte les fonctions « fortifiées » (suffixe `_chk`) dans les symboles dynamiques et les compare au nombre de fonctions « fortifiables » :

```
FORTIFY   Fortified   Fortifiable  
No        0           2  
```

Ici, `checksec` indique que 2 fonctions *pourraient* être fortifiées, mais 0 le sont. Cela signifie que le binaire n'a pas été compilé avec `-D_FORTIFY_SOURCE`.

Si la protection était active, on verrait des symboles comme `__printf_chk` et `__read_chk` au lieu de `printf` et `read` :

```bash
$ nm -D binaire_fortifie | grep _chk
                 U __printf_chk@GLIBC_2.3.4
                 U __read_chk@GLIBC_2.4
```

### Impact sur le RE

Les fonctions fortifiées (`__printf_chk`, `__memcpy_chk`, `__strcpy_chk`…) prennent un argument supplémentaire (la taille du buffer de destination). Dans le désassemblage, cela se traduit par un paramètre additionnel passé dans un registre et un appel à la version `_chk` au lieu de la version standard. C'est un pattern mineur mais bon à connaître pour ne pas être dérouté par ces noms de fonctions inhabituels.

---

## RPATH / RUNPATH — chemins de recherche de bibliothèques

`checksec` signale aussi la présence de `RPATH` et `RUNPATH` — les chemins de recherche de bibliothèques encodés dans le binaire (voir section 5.4). Du point de vue de la sécurité, un `RPATH` ou `RUNPATH` pointant vers un répertoire inscriptible par un utilisateur non privilégié est une vulnérabilité : un attaquant pourrait y placer une bibliothèque malveillante qui serait chargée à la place de la légitime.

```bash
# Pas de RPATH/RUNPATH (situation normale et sûre)
RPATH      RUNPATH  
No RPATH   No RUNPATH  

# RPATH vers un répertoire potentiellement dangereux
RPATH      RUNPATH
/tmp/libs  No RUNPATH    # ← Alerte : /tmp est inscriptible par tous !
```

---

## Comparer les protections de plusieurs binaires

`checksec` peut analyser plusieurs binaires en une seule commande, ce qui permet de comparer rapidement les protections :

```bash
$ checksec --dir=binaries/ch05-keygenme/
RELRO           STACK CANARY      NX            PIE             RPATH      RUNPATH      Symbols         FORTIFY Fortified   Fortifiable  FILE  
Full RELRO      Canary found      NX enabled    PIE enabled     No RPATH   No RUNPATH   72 Symbols      No      0           2           keygenme_O0  
Full RELRO      Canary found      NX enabled    PIE enabled     No RPATH   No RUNPATH   72 Symbols      No      0           2           keygenme_O2  
Full RELRO      Canary found      NX enabled    PIE enabled     No RPATH   No RUNPATH   No Symbols      No      0           2           keygenme_O2_strip  
```

On constate que les protections sont identiques entre les trois versions — les flags de sécurité ne dépendent pas du niveau d'optimisation (`-O0` vs `-O2`), mais des flags de linking et de compilation. La seule différence est la colonne `Symbols` : le binaire strippé affiche `No Symbols`.

---

## Vérifier les protections manuellement (sans `checksec`)

`checksec` est un outil de commodité, pas une boîte noire. Tout ce qu'il fait peut être reproduit avec `readelf`. Voici la correspondance :

| Protection | Commande manuelle | Indice de présence |  
|---|---|---|  
| NX | `readelf -lW binaire \| grep GNU_STACK` | Flags `RW` (sans `E`) |  
| PIE | `readelf -h binaire \| grep Type` | `DYN` = PIE, `EXEC` = non-PIE |  
| Canary | `readelf -s binaire \| grep __stack_chk_fail` | Symbole présent = canary activé |  
| RELRO | `readelf -lW binaire \| grep GNU_RELRO` | Segment présent = au moins Partial |  
| Full RELRO | `readelf -d binaire \| grep BIND_NOW` | Entrée présente = Full RELRO |  
| FORTIFY | `readelf -s binaire \| grep _chk@` | Symboles `_chk` = fortifié |  
| RPATH | `readelf -d binaire \| grep RPATH` | Entrée présente = RPATH défini |  
| RUNPATH | `readelf -d binaire \| grep RUNPATH` | Entrée présente = RUNPATH défini |

Savoir reproduire ces vérifications manuellement est important : d'une part, `checksec` n'est pas toujours disponible (environnement minimal, machine cible restreinte) ; d'autre part, comprendre *comment* les protections sont encodées dans l'ELF renforce votre maîtrise du format.

---

## Résumé des protections

| Protection | Protège contre | Activée par | Désactivée par |  
|---|---|---|---|  
| **NX** | Exécution de code injecté (pile, heap) | Défaut GCC | `-z execstack` |  
| **PIE** | Prédiction des adresses du binaire | Défaut GCC récent | `-no-pie` |  
| **ASLR** | Prédiction des adresses (pile, heap, libs) | Défaut noyau | `echo 0 > /proc/sys/kernel/randomize_va_space` |  
| **Stack Canary** | Buffer overflow écrasant l'adresse de retour | `-fstack-protector-strong` (défaut) | `-fno-stack-protector` |  
| **Partial RELRO** | Réorganisation des sections sensibles | Défaut GCC | `-Wl,-z,norelro` |  
| **Full RELRO** | GOT overwrite (rend la GOT read-only) | `-Wl,-z,now` | Absence de `-z now` |  
| **FORTIFY** | Débordements sur fonctions libc (`memcpy`, `printf`…) | `-D_FORTIFY_SOURCE=2` | Absence du define |

---

## Ce qu'il faut retenir pour la suite

- **`checksec` est le premier réflexe de l'audit de sécurité binaire**. En une commande, vous connaissez l'état de toutes les protections. C'est aussi un réflexe de triage RE : les protections influencent directement votre stratégie d'analyse.  
- **NX activé** signifie que l'injection et l'exécution directe de shellcode sont impossibles sans contournement. Sa désactivation est un signal d'alerte.  
- **PIE activé** signifie que les adresses du binaire sont relatives et randomisées par l'ASLR. En debug, désactivez temporairement l'ASLR pour obtenir des adresses stables.  
- **Les stack canaries** ajoutent un pattern reconnaissable dans le désassemblage (`fs:[0x28]` → comparaison → `__stack_chk_fail`). Apprenez à le reconnaître pour le distinguer de la logique applicative.  
- **Full RELRO** rend la GOT en lecture seule — les entrées PLT/GOT sont résolues au chargement, pas paresseusement.  
- **Tout ce que fait `checksec` est reproductible avec `readelf`**. Connaître les commandes manuelles correspondantes vous rend autonome sur n'importe quel système.  
- Le chapitre 19 approfondira chacune de ces protections et les techniques pour les contourner dans un contexte d'analyse avancée.

---


⏭️ [Workflow « triage rapide » : la routine des 5 premières minutes face à un binaire](/05-outils-inspection-base/07-workflow-triage-rapide.md)
