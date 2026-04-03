🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 3 — Bases de l'assembleur x86-64 pour le RE

> 🎯 **Objectif du chapitre** : acquérir les bases d'assembleur x86-64 indispensables pour lire et comprendre le code désassemblé produit par GCC/G++. Il ne s'agit pas de devenir un développeur assembleur, mais de savoir *lire* ce que le compilateur a généré.

---

## Pourquoi ce chapitre est essentiel

Quand on ouvre un binaire dans un désassembleur — que ce soit `objdump`, Ghidra, IDA ou Radare2 — on ne voit ni du C, ni du C++. On voit de l'assembleur. C'est la langue maternelle du processeur, et c'est le niveau de représentation le plus bas auquel le reverse engineer travaille au quotidien.

Pas besoin de tout maîtriser d'un coup : en pratique, une poignée d'instructions couvre l'immense majorité du code généré par GCC. Ce chapitre se concentre sur ce noyau essentiel, en gardant toujours le même angle : **« qu'est-ce que je vois dans un binaire réel, et qu'est-ce que ça signifie ? »**

---

## Ce que vous allez apprendre

- **Les registres** du x86-64 et leur rôle dans le code compilé : registres généraux, pointeur de pile, pointeur d'instruction, registre de flags.  
- **Les instructions fondamentales** : déplacements de données (`mov`, `lea`), manipulation de la pile (`push`, `pop`), appels de fonctions (`call`, `ret`).  
- **L'arithmétique et la logique** : les opérations que le compilateur utilise pour traduire vos expressions C (`add`, `sub`, `imul`, `xor`, `shl`, `shr`, `test`, `cmp`).  
- **Les sauts conditionnels** : comment un simple `if` en C devient une paire `cmp` / `jz` en assembleur, et comment reconnaître les boucles `for` et `while`.  
- **La pile et les conventions d'appel** : le prologue et l'épilogue de fonction, la convention System V AMD64 utilisée sous Linux, et comment les paramètres sont passés via les registres puis la pile.  
- **Une méthode de lecture** : une approche pratique en 5 étapes pour aborder un listing assembleur sans se sentir submergé.  
- **La différence entre `call printf@plt` et `syscall`** : deux façons très différentes d'interagir avec le système, que l'on croise constamment en RE.  
- **Un premier aperçu des instructions SIMD** (SSE/AVX) : pas pour les maîtriser, mais pour les reconnaître quand elles apparaissent dans du code optimisé et ne pas paniquer.

---

## Positionnement dans la formation

Ce chapitre fait le pont entre la théorie de la chaîne de compilation (chapitre 2) et la pratique des outils d'analyse (parties II et III). Tout ce qui suit — désassemblage avec `objdump`, analyse dans Ghidra, débogage avec GDB — suppose que vous êtes capables de lire un listing assembleur et d'en extraire le sens général.

```
Chapitre 2                    Chapitre 3                     Chapitre 4+  
Chaîne de compilation    →    Lire l'assembleur produit  →   Utiliser les outils  
(comment le C devient         par GCC/G++                    de RE sur du vrai
 un binaire ELF)                                             code désassemblé
```

---

## Prérequis

- Avoir lu le **chapitre 2** (ou connaître les grandes lignes de la compilation GCC et le format ELF).  
- Être à l'aise avec le **C** (variables, pointeurs, fonctions, structures de contrôle) — c'est le code source qu'on cherchera à reconnaître « de l'autre côté ».  
- Disposer d'un terminal Linux avec `gcc` et `objdump` installés (cf. chapitre 4 pour l'environnement complet, mais ces deux outils suffisent pour suivre ce chapitre).

---

## Organisation des sections

| Section | Thème | Ce que vous saurez faire après |  
|---|---|---|  
| 3.1 | Registres, pointeurs et flags | Identifier le rôle de chaque registre dans un listing |  
| 3.2 | Instructions essentielles | Lire les `mov`, `push`, `pop`, `call`, `ret`, `lea` |  
| 3.3 | Arithmétique et logique | Reconnaître les calculs et les opérations bit à bit |  
| 3.4 | Sauts conditionnels | Comprendre les `if`, `else`, boucles dans le désassemblage |  
| 3.5 | Pile, prologue et épilogue | Décoder l'entrée et la sortie d'une fonction |  
| 3.6 | Passage des paramètres | Savoir quels registres portent quels arguments |  
| 3.7 | Méthode de lecture en 5 étapes | Aborder un listing inconnu avec une démarche structurée |  
| 3.8 | `call @plt` vs `syscall` | Distinguer appel de bibliothèque et appel système direct |  
| 3.9 | Introduction SIMD (SSE/AVX) | Reconnaître ces instructions sans les confondre avec du bruit |

---

## Conventions utilisées dans ce chapitre

Tout au long de ce chapitre, l'assembleur est affiché en **syntaxe Intel** (destination à gauche, source à droite), qui est la syntaxe par défaut de la plupart des outils de RE modernes (Ghidra, IDA, Binary Ninja). La section 3.2 aborde brièvement les différences avec la syntaxe AT&T (utilisée par défaut par `objdump` et GAS), et le chapitre 7 y revient en détail.

Les exemples de code assembleur sont systématiquement mis en regard du code C correspondant, pour faciliter la correspondance mentale entre les deux niveaux de représentation :

```c
// Code C
int result = a + b;
```

```asm
; Assembleur x86-64 (syntaxe Intel)
mov     eax, dword [rbp-0x8]    ; charge a  
add     eax, dword [rbp-0xc]    ; ajoute b  
mov     dword [rbp-0x4], eax    ; stocke dans result  
```

Les compilations d'exemple utilisent GCC avec `-O0` (aucune optimisation) sauf mention contraire, afin que le code assembleur reste proche de la structure du C source. Les effets des niveaux d'optimisation supérieurs sont traités en profondeur au chapitre 16.

---

## Sommaire du chapitre

- 3.1 [Registres généraux, pointeurs et flags (`rax`, `rsp`, `rbp`, `rip`, `RFLAGS`…)](/03-assembleur-x86-64/01-registres-pointeurs-flags.md)  
- 3.2 [Instructions essentielles : `mov`, `push`/`pop`, `call`/`ret`, `lea`](/03-assembleur-x86-64/02-instructions-essentielles.md)  
- 3.3 [Arithmétique et logique : `add`, `sub`, `imul`, `xor`, `shl`/`shr`, `test`, `cmp`](/03-assembleur-x86-64/03-arithmetique-logique.md)  
- 3.4 [Sauts conditionnels et inconditionnels : `jmp`, `jz`/`jnz`, `jl`, `jge`, `jle`, `ja`…](/03-assembleur-x86-64/04-sauts-conditionnels.md)  
- 3.5 [La pile : prologue, épilogue et conventions d'appel System V AMD64](/03-assembleur-x86-64/05-pile-prologue-epilogue.md)  
- 3.6 [Passage des paramètres : `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` puis la pile](/03-assembleur-x86-64/06-passage-parametres.md)  
- 3.7 [Lire un listing assembleur sans paniquer : méthode pratique en 5 étapes](/03-assembleur-x86-64/07-methode-lecture-asm.md)  
- 3.8 [Différence entre appel de bibliothèque (`call printf@plt`) et syscall direct (`syscall`)](/03-assembleur-x86-64/08-call-plt-vs-syscall.md)  
- 3.9 [Introduction aux instructions SIMD (SSE/AVX) — les reconnaître sans les craindre](/03-assembleur-x86-64/09-introduction-simd.md)  
- [**🎯 Checkpoint** : annoter manuellement un désassemblage réel (fourni)](/03-assembleur-x86-64/checkpoint.md)

---


⏭️ [Registres généraux, pointeurs et flags (`rax`, `rsp`, `rbp`, `rip`, `RFLAGS`…)](/03-assembleur-x86-64/01-registres-pointeurs-flags.md)
