🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 7.2 — Syntaxe AT&T vs Intel — passer de l'une à l'autre (`-M intel`)

> 🔧 **Outils utilisés** : `objdump`, `gcc`, `gdb`  
> 📦 **Binaires** : `keygenme_O0` (répertoire `binaries/ch07-keygenme/`)

---

## Deux syntaxes pour le même code machine

Quand vous désassemblez un binaire x86/x86-64, les octets machine sont toujours les mêmes — c'est le processeur qui les exécute, et il ne connaît ni AT&T ni Intel. En revanche, la **représentation textuelle** de ces instructions varie selon la convention choisie par l'outil de désassemblage. Deux conventions coexistent dans l'écosystème, héritées de traditions historiques différentes, et elles se rencontrent constamment en pratique.

La syntaxe **AT&T** est née dans les laboratoires Bell, berceau d'Unix. Elle a été adoptée par la suite GNU : GCC, GAS (GNU Assembler), GDB (par défaut), et `objdump` (par défaut). Si vous travaillez sous Linux avec des outils GNU sans configuration particulière, c'est ce que vous verrez.

La syntaxe **Intel** est celle de la documentation officielle d'Intel (*Intel Software Developer's Manual*). Elle a été adoptée par NASM, puis par la majorité des outils de reverse engineering : IDA, Ghidra (par défaut), Binary Ninja, Radare2/Cutter, et la plupart des tutoriels, livres, articles et write-ups de CTF.

Il ne s'agit pas de deux langages différents. Les mêmes instructions, les mêmes registres, les mêmes opérations sont représentés — seule la notation change. C'est comparable à la différence entre la notation `3 + 4` et la notation `(+ 3 4)` : le résultat est identique, mais la lisibilité diffère selon ce à quoi vous êtes habitué.

Maîtriser les deux syntaxes n'est pas un luxe : c'est une nécessité. Vous lirez de la syntaxe AT&T dans les sorties GCC, les *inline assembly* du noyau Linux, les sorties par défaut de GDB, et les fichiers `.s` générés par le compilateur. Vous lirez de la syntaxe Intel dans Ghidra, IDA, les manuels Intel/AMD, et la quasi-totalité de la littérature RE. Basculer de l'une à l'autre doit devenir un réflexe automatique.

---

## Les différences, une par une

Prenons une même séquence de code machine et examinons chaque différence entre les deux représentations. Toutes les instructions ci-dessous correspondent exactement aux mêmes octets — seule l'écriture change.

### 1. Ordre des opérandes

C'est la différence la plus importante et la source de confusion la plus fréquente.

```
AT&T :    mov    %rsp, %rbp          # source, destination  
Intel :   mov    rbp, rsp             # destination, source  
```

En AT&T, on lit « déplace `rsp` vers `rbp` » de gauche à droite. En Intel, on lit « `rbp` reçoit la valeur de `rsp` », comme une affectation dans un langage de programmation (`rbp = rsp`). L'ordre Intel est souvent jugé plus intuitif par les développeurs habitués au C, et c'est l'une des raisons de sa prédominance en RE.

Cette inversion s'applique à **toutes** les instructions à deux opérandes : `mov`, `add`, `sub`, `cmp`, `xor`, `lea`, `test`, etc.

```
AT&T :    add    $0x10, %rax          # rax = rax + 0x10  
Intel :   add    rax, 0x10            # rax = rax + 0x10  
```

Le résultat est le même, mais l'ordre de lecture est inversé.

> ⚠️ **Piège classique** : quand vous passez d'un listing AT&T à un listing Intel (ou l'inverse), l'opérande qui était à gauche se retrouve à droite. Si vous annotez mentalement « le premier opérande est la source », assurez-vous de savoir dans quelle syntaxe vous êtes. Une erreur sur l'ordre des opérandes de `cmp` ou `sub` peut inverser complètement votre compréhension de la logique conditionnelle.

### 2. Préfixes des registres et des valeurs immédiates

En AT&T, les registres sont préfixés par `%` et les valeurs immédiates (constantes) par `$`. En Intel, il n'y a aucun préfixe.

```
AT&T :    mov    $0x42, %eax          # eax = 0x42  
Intel :   mov    eax, 0x42            # eax = 0x42  
```

```
AT&T :    push   %rbp  
Intel :   push   rbp  
```

```
AT&T :    cmp    $0x0, %edi  
Intel :   cmp    edi, 0x0  
```

La syntaxe Intel est visuellement plus épurée. La syntaxe AT&T a l'avantage d'être **non ambiguë** : en voyant `$0x42`, vous savez immédiatement que c'est une constante, pas une adresse mémoire. En Intel, la distinction repose sur le contexte.

### 3. Suffixes de taille vs mots-clés de taille

En AT&T, la taille de l'opération est encodée dans un **suffixe** ajouté au mnémonique de l'instruction :

| Suffixe | Taille | Exemple AT&T |  
|---|---|---|  
| `b` | 1 octet (byte) | `movb $0x41, %al` |  
| `w` | 2 octets (word) | `movw $0x0, %ax` |  
| `l` | 4 octets (long/dword) | `movl $0x0, %eax` |  
| `q` | 8 octets (quad) | `movq %rsp, %rbp` |

En Intel, la taille est indiquée par un **mot-clé** placé avant l'opérande mémoire quand c'est nécessaire :

| Mot-clé | Taille | Exemple Intel |  
|---|---|---|  
| `BYTE PTR` | 1 octet | `mov BYTE PTR [rax], 0x41` |  
| `WORD PTR` | 2 octets | `mov WORD PTR [rax], 0x0` |  
| `DWORD PTR` | 4 octets | `mov DWORD PTR [rax], 0x0` |  
| `QWORD PTR` | 8 octets | `mov QWORD PTR [rbp-0x8], rdi` |

Quand la taille est évidente d'après les registres utilisés (par exemple `mov rax, rbx` — les deux sont des registres 64 bits), Intel omet le mot-clé. AT&T ajoute quand même souvent le suffixe, bien que GCC puisse l'omettre quand il n'y a pas d'ambiguïté.

```
AT&T :    movl   $0x0, -0x4(%rbp)     # 'l' = 4 octets  
Intel :   mov    DWORD PTR [rbp-0x4], 0x0  
```

### 4. Accès mémoire : parenthèses vs crochets

C'est la différence la plus visible sur les instructions qui accèdent à la mémoire.

**AT&T** utilise des **parenthèses** et une syntaxe de la forme `déplacement(base, index, échelle)` :

```
AT&T :    movl   -0x4(%rbp), %eax           # [rbp - 4]  
AT&T :    movl   (%rax,%rcx,4), %edx        # [rax + rcx*4]  
AT&T :    movl   0x10(%rax,%rcx,4), %edx    # [rax + rcx*4 + 0x10]  
```

**Intel** utilise des **crochets** et une syntaxe algébrique naturelle :

```
Intel :   mov    eax, DWORD PTR [rbp-0x4]  
Intel :   mov    edx, DWORD PTR [rax+rcx*4]  
Intel :   mov    edx, DWORD PTR [rax+rcx*4+0x10]  
```

La formule générale pour l'adressage mémoire x86-64 est :

```
Adresse effective = base + (index × échelle) + déplacement
```

Les deux syntaxes expriment cette formule, mais de manière radicalement différente. La version Intel est une expression mathématique lisible ; la version AT&T est un format positionnel compact.

Voici un tableau récapitulatif avec plusieurs formes d'adressage :

| Mode d'adressage | AT&T | Intel |  
|---|---|---|  
| Direct (registre) | `%rax` | `rax` |  
| Immédiat | `$42` | `42` |  
| Indirect simple | `(%rax)` | `[rax]` |  
| Base + déplacement | `-0x8(%rbp)` | `[rbp-0x8]` |  
| Base + index | `(%rax,%rcx)` | `[rax+rcx]` |  
| Base + index × échelle | `(%rax,%rcx,4)` | `[rax+rcx*4]` |  
| Complet | `0x10(%rax,%rcx,8)` | `[rax+rcx*8+0x10]` |  
| RIP-relatif | `0x2f3a(%rip)` | `[rip+0x2f3a]` |

### 5. Instructions de saut et d'appel (long)

Pour les sauts absolus indirects et les appels indirects, AT&T utilise un préfixe `*` :

```
AT&T :    jmpq   *%rax                # saut indirect via registre  
Intel :   jmp    rax  

AT&T :    callq  *0x2fe2(%rip)        # appel indirect via mémoire  
Intel :   call   QWORD PTR [rip+0x2fe2]  
```

De plus, AT&T suffixe parfois les instructions de saut et d'appel avec `q` (pour *quad*, 64 bits) ou `l` — vous verrez souvent `callq` et `jmpq` au lieu de `call` et `jmp`. En Intel, c'est toujours simplement `call` et `jmp`.

### 6. Mnémoniques légèrement différents

Quelques instructions ont un mnémonique différent entre les deux syntaxes :

| AT&T | Intel | Opération |  
|---|---|---|  
| `movzbl` | `movzx` | Extension zéro (byte → long) |  
| `movsbl` | `movsx` | Extension signe (byte → long) |  
| `movzbq` | `movzx` | Extension zéro (byte → quad) |  
| `movslq` | `movsxd` | Extension signe (long → quad) |  
| `cbtw` | `cbw` | Conversion byte → word |  
| `cwtl` | `cwde` | Conversion word → dword |  
| `cltq` | `cdqe` | Conversion dword → qword |  
| `cltd` | `cdq` | Extension signe eax → edx:eax |  
| `cqto` | `cqo` | Extension signe rax → rdx:rax |

En AT&T, les mnémoniques de `movzx`/`movsx` encodent les tailles source et destination dans le nom (`movzbl` = *move zero-extend byte to long*). En Intel, l'instruction est simplement `movzx` ou `movsx`, et la taille est déterminée par les opérandes.

---

## Exemple complet comparé

Prenons un extrait réaliste du début d'une fonction, tel que le produirait `objdump` sur `keygenme_O0` :

### Version AT&T (par défaut)

```
0000000000001139 <compute_hash>:
    1139:       55                      push   %rbp
    113a:       48 89 e5                mov    %rsp,%rbp
    113d:       48 89 7d e8             mov    %rdi,-0x18(%rbp)
    1141:       c7 45 fc 00 00 00 00    movl   $0x0,-0x4(%rbp)
    1148:       c7 45 f8 00 00 00 00    movl   $0x0,-0x8(%rbp)
    114f:       eb 1d                   jmp    116e <compute_hash+0x35>
    1151:       8b 45 f8                mov    -0x8(%rbp),%eax
    1154:       48 98                   cltq
    1156:       48 03 45 e8             add    -0x18(%rbp),%rax
    115a:       0f b6 00                movzbl (%rax),%eax
    115d:       0f be c0                movsbl %al,%eax
    1160:       01 45 fc                add    %eax,-0x4(%rbp)
    1163:       8b 45 fc                mov    -0x4(%rbp),%eax
    1166:       c1 e0 03                shl    $0x3,%eax
    1169:       89 45 fc                mov    %eax,-0x4(%rbp)
    116c:       83 45 f8 01             addl   $0x1,-0x8(%rbp)
```

### Version Intel (`-M intel`)

```
0000000000001139 <compute_hash>:
    1139:       55                      push   rbp
    113a:       48 89 e5                mov    rbp,rsp
    113d:       48 89 7d e8             mov    QWORD PTR [rbp-0x18],rdi
    1141:       c7 45 fc 00 00 00 00    mov    DWORD PTR [rbp-0x4],0x0
    1148:       c7 45 f8 00 00 00 00    mov    DWORD PTR [rbp-0x8],0x0
    114f:       eb 1d                   jmp    116e <compute_hash+0x35>
    1151:       8b 45 f8                mov    eax,DWORD PTR [rbp-0x8]
    1154:       48 98                   cdqe
    1156:       48 03 45 e8             add    rax,QWORD PTR [rbp-0x18]
    115a:       0f b6 00                movzx  eax,BYTE PTR [rax]
    115d:       0f be c0                movsx  eax,al
    1160:       01 45 fc                add    DWORD PTR [rbp-0x4],eax
    1163:       8b 45 fc                mov    eax,DWORD PTR [rbp-0x4]
    1166:       c1 e0 03                shl    eax,0x3
    1169:       89 45 fc                mov    DWORD PTR [rbp-0x4],eax
    116c:       83 45 f8 01             add    DWORD PTR [rbp-0x8],0x1
```

Constatez que la colonne des octets machine (au centre) est **strictement identique** dans les deux cas. `55` est `push rbp`/`push %rbp` quoi qu'il arrive — c'est le même octet décodé par le processeur. Seule la colonne de droite change.

Quelques observations spécifiques sur cet extrait :

- `cltq` (AT&T) devient `cdqe` (Intel) — extension signe de `eax` vers `rax`.  
- `movzbl (%rax),%eax` (AT&T) devient `movzx eax, BYTE PTR [rax]` (Intel) — l'encodage de la taille source passe du suffixe au mot-clé.  
- L'opérande mémoire `-0x4(%rbp)` devient `[rbp-0x4]` — parenthèses vers crochets, même sémantique.  
- Les `$` et `%` disparaissent en Intel, rendant le listing plus aéré.

---

## Basculer entre les syntaxes dans les outils courants

### `objdump`

Comme vu à la section 7.1, l'option `-M intel` bascule en syntaxe Intel :

```bash
objdump -d -M intel keygenme_O0
```

Pour revenir en AT&T (ou vérifier explicitement) :

```bash
objdump -d -M att keygenme_O0
```

On peut aussi combiner des sous-options de `-M`. Par exemple, `-M intel,addr32` forcerait l'affichage des adresses en 32 bits (rarement utile, mentionné pour exhaustivité).

### GCC : voir l'assembleur généré

GCC peut produire un fichier assembleur au lieu d'un binaire, via l'option `-S` :

```bash
gcc -S -O0 -o keygenme.s keygenme.c          # AT&T par défaut  
gcc -S -O0 -masm=intel -o keygenme.s keygenme.c  # Intel  
```

Le fichier `.s` résultant contient les directives de l'assembleur GNU (GAS) et le code assembleur de votre programme. Comparer les deux versions est un excellent exercice pour ancrer les différences.

### GDB

Par défaut, GDB utilise la syntaxe AT&T. Pour basculer en Intel :

```
(gdb) set disassembly-flavor intel
```

Pour rendre ce choix permanent, ajoutez cette ligne à votre fichier `~/.gdbinit` :

```
set disassembly-flavor intel
```

Si vous utilisez GEF, pwndbg ou PEDA (chapitre 12), la syntaxe Intel est souvent configurée par défaut.

### Ghidra, IDA, Binary Ninja, Radare2

Ces outils utilisent la syntaxe Intel par défaut. Ghidra permet de basculer en AT&T via *Edit → Tool Options → Listing Fields → Operands Field* (rarement souhaité). IDA est historiquement lié à Intel. Radare2 supporte les deux via la commande `e asm.syntax=att` ou `e asm.syntax=intel`.

En pratique, si vous travaillez avec ces outils, vous lirez de l'Intel. Si vous passez ensuite à `objdump` ou GDB sans avoir configuré l'alias, vous tomberez sur de l'AT&T. D'où l'importance de maîtriser les deux.

---

## Quelle syntaxe choisir pour ce tutoriel ?

Dans la suite de cette formation, **nous utiliserons la syntaxe Intel par défaut**, sauf mention contraire. Les raisons :

- C'est la convention dominante dans la communauté RE, les livres de référence (*Practical Reverse Engineering*, *Reversing: Secrets of Reverse Engineering*, *The IDA Pro Book*…), et les write-ups de CTF.  
- C'est la syntaxe par défaut de Ghidra et IDA, les deux outils majeurs que nous utiliserons aux chapitres 8 et 9.  
- L'ordre `destination, source` correspond à l'assignation en C (`a = b`), ce qui facilite la traduction mentale vers du pseudo-code.  
- Les accès mémoire en crochets (`[rbp-0x8]`) se lisent plus naturellement qu'en parenthèses (` -0x8(%rbp)`).

Cela dit, vous **devez** rester à l'aise avec AT&T. Vous la rencontrerez dans le code source du noyau Linux, dans les sorties brutes de GCC et GDB, dans les fichiers `.S` des bibliothèques système, et dans certains articles académiques. Ne la traitez pas comme un dialecte exotique à éviter — considérez-la comme un accent régional de la même langue.

> 💡 **Conseil pratique** : configurez vos outils une fois pour toutes en Intel (alias `objdump`, `~/.gdbinit`), mais forcez-vous régulièrement à lire un listing AT&T sans le convertir. Le jour où vous déboguerez un crash en production sur un serveur où seul `objdump` est installé et où votre alias n'existe pas, vous serez content de pouvoir lire les deux.

---

## Aide-mémoire de conversion rapide

Pour la référence rapide, voici un tableau synthétique des transformations à appliquer mentalement quand vous passez d'une syntaxe à l'autre :

| Élément | AT&T → Intel | Intel → AT&T |  
|---|---|---|  
| Opérandes | Inverser l'ordre | Inverser l'ordre |  
| Registres | Retirer `%` | Ajouter `%` |  
| Immédiats | Retirer `$` | Ajouter `$` |  
| Accès mémoire | `(…)` → `[…]` | `[…]` → `(…)` |  
| Taille sur mnémonique | Retirer le suffixe `b`/`w`/`l`/`q` | Ajouter le suffixe correspondant |  
| Taille sur opérande mémoire | Ajouter `BYTE`/`WORD`/`DWORD`/`QWORD PTR` | Retirer le mot-clé de taille |  
| Saut/appel indirect | Retirer `*` | Ajouter `*` |  
| `movzbl`/`movsbl` | → `movzx`/`movsx` | `movzx`/`movsx` → ajouter suffixes source+dest |  
| `cltq`/`cltd`/`cwtl` | → `cdqe`/`cdq`/`cwde` | Inversement |

Avec un peu de pratique, cette conversion devient instantanée. Vous ne penserez plus en « AT&T » ou « Intel » — vous penserez directement en termes d'opérations sur les registres et la mémoire, quelle que soit la notation affichée à l'écran.

---

## Résumé

AT&T et Intel sont deux notations différentes pour le même jeu d'instructions. Leurs principales différences portent sur l'ordre des opérandes (source/destination inversés), les préfixes (`%`, `$` en AT&T, rien en Intel), la notation des accès mémoire (parenthèses vs crochets), et l'encodage de la taille des opérations (suffixe sur le mnémonique vs mot-clé `PTR`). Les outils GNU (GCC, GAS, GDB, `objdump`) utilisent AT&T par défaut mais supportent Intel via une option. Les outils de RE (Ghidra, IDA, Binary Ninja) utilisent Intel par défaut. Savoir lire les deux couramment est indispensable — la syntaxe Intel sera notre choix par défaut pour la suite de cette formation.

---


⏭️ [Comparaison avec/sans optimisations GCC (`-O0` vs `-O2` vs `-O3`)](/07-objdump-binutils/03-comparaison-optimisations.md)
