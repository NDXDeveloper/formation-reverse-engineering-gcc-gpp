🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 12

> Tracer l'exécution complète de `keygenme_O0` avec GEF, capturer le moment de la comparaison

---

## ⚠️ Spoilers

Ce fichier contient la solution complète du checkpoint du chapitre 12. Tenter de réaliser le checkpoint par soi-même avant de consulter cette solution est fortement recommandé — c'est en pratiquant que les commandes deviennent des réflexes.

---

## Environnement attendu

```bash
$ gdb-gef --version
GNU gdb (Ubuntu 15.x-...) ...

$ file binaries/ch12-keygenme/keygenme_O0
keygenme_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
for GNU/Linux 3.2.0, BuildID[sha1]=..., not stripped  
```

Le binaire est un ELF 64-bit, PIE, lié dynamiquement et non strippé. La mention « not stripped » confirme la présence des symboles de débogage.

---

## Solution détaillée

### 1. Reconnaissance initiale

```
$ gdb-gef -q ./keygenme_O0

gef➤ checksec
[+] checksec for './keygenme_O0'
Canary                        : ✓  
NX                            : ✓  
PIE                           : ✓  
Fortify                       : ✗  
RelRO                         : Full  
```

Le binaire possède les protections standard d'un GCC récent. Pour ce checkpoint (analyse de la logique, pas exploitation), ces protections n'empêchent rien — elles sont simplement notées.

```
gef➤ info functions
...
0x0000000000001169  main
0x0000000000001245  check_password
...

gef➤ got  
GOT protection: Full RELRO | GOT functions: 6  

[0x3fd8] puts@GLIBC_2.2.5          →  0x...
[0x3fe0] printf@GLIBC_2.2.5        →  0x...
[0x3fe8] fgets@GLIBC_2.2.5         →  0x...
[0x3ff0] strcmp@GLIBC_2.2.5         →  0x...
[0x3ff8] strlen@GLIBC_2.2.5        →  0x...
```

**Observations clés :**

- Le binaire contient une fonction `check_password` — c'est probablement la routine de vérification.  
- La table GOT confirme l'import de `strcmp` — le mécanisme de comparaison est une comparaison de chaînes standard.  
- `fgets` est importée — le programme lit l'entrée utilisateur via `fgets` (pas `scanf`), ce qui signifie que l'entrée inclut potentiellement un `\n` en fin de chaîne.

### 2. Breakpoints et lancement

```
gef➤ break main  
Breakpoint 1 at 0x116d: file keygenme.c, line 18.  

gef➤ break check_password  
Breakpoint 2 at 0x1249: file keygenme.c, line 8.  

gef➤ break strcmp  
Breakpoint 3 at 0x1030  

gef➤ run
```

Trois breakpoints posés : `main` pour l'observation générale, `check_password` pour la routine de vérification, et `strcmp` pour le moment de la comparaison. L'ordre de déclenchement attendu est `main` → `check_password` → `strcmp`.

### 3. Contexte à l'entrée de `main`

GDB s'arrête sur `main`. Le contexte GEF affiche :

```
──────────────────────────────── registers ────────────────────────────────
$rax   : 0x0000555555555169  →  <main+0> push rbp
$rdi   : 0x1                              ← argc = 1
$rsi   : 0x00007fffffffe1a8  →  0x00007fffffffe47a  →  "./keygenme_O0"
...
──────────────────────────────────── code ─────────────────────────────────
   0x555555555169 <main+0>:     push   rbp
   0x55555555516a <main+1>:     mov    rbp, rsp
 → 0x55555555516d <main+4>:     sub    rsp, 0x40
   0x555555555171 <main+8>:     mov    DWORD PTR [rbp-0x34], edi
   0x555555555174 <main+11>:    mov    QWORD PTR [rbp-0x40], rsi
   ...
   0x555555555198 <main+47>:    call   0x555555555030 <puts@plt>
   ...
   0x5555555551b5 <main+76>:    call   0x555555555050 <fgets@plt>
   ...
   0x5555555551d0 <main+103>:   call   0x555555555245 <check_password>
──────────────────────────────────── stack ────────────────────────────────
0x00007fffffffe090│+0x0000: 0x00007fffffffe1a8  →  ...    ← $rsp
...
```

**Vérification avec `xinfo` :**

```
gef➤ xinfo $rsi
──────────────────── xinfo: 0x7fffffffe1a8 ────────────────────
Page: 0x7ffffffde000 → 0x7ffffffff000 (size=0x21000)  
Permissions: rw-  
Pathname: [stack]  
Segment: [stack]  
```

Confirmé : `RSI` pointe vers la pile, c'est bien `argv`.

**Lecture du désassemblage :** Le code de `main` visible dans la section code montre la séquence logique attendue : appel à `puts` (affichage du prompt), appel à `fgets` (lecture de l'entrée), puis appel à `check_password`. C'est un flux linéaire classique pour un crackme simple.

### 4. Avancer jusqu'à `check_password`

```
gef➤ continue
```

Le programme affiche son prompt :

```
Entrez le mot de passe :
```

On tape `test123` et on valide. GDB s'arrête sur le breakpoint `check_password`.

Le contexte montre :

```
──────────────────────────────── registers ────────────────────────────────
$rdi   : 0x00007fffffffe050  →  "test123\n"
...
```

**Observation importante :** L'entrée dans `RDI` est `"test123\n"` avec un retour à la ligne. C'est une conséquence de `fgets` qui conserve le `\n`. Si le programme compare cette chaîne directement avec `strcmp`, le `\n` fera échouer la comparaison même avec le bon mot de passe. Deux possibilités : soit le programme retire le `\n` avant de comparer (un `strlen` + remplacement est un pattern courant), soit la chaîne attendue inclut le `\n` (peu probable). La présence de `strlen` dans les imports GOT suggère la première hypothèse.

### 5. Capturer la comparaison sur `strcmp`

```
gef➤ continue
```

GDB s'arrête sur `strcmp`. Le contexte affiche les arguments :

```
──────────────────────────────── registers ────────────────────────────────
$rdi   : 0x00007fffffffe050  →  "test123"
$rsi   : 0x0000555555556004  →  "s3cr3t_k3y"
...
```

Le `\n` a disparu de l'entrée utilisateur — le programme l'a retiré entre `fgets` et `strcmp`, confirmant l'hypothèse.

**Résultat principal : le mot de passe attendu est `s3cr3t_k3y`.**

**Qualification des adresses avec `xinfo` :**

```
gef➤ xinfo $rdi
──────────────────── xinfo: 0x7fffffffe050 ────────────────────
Page: 0x7ffffffde000 → 0x7ffffffff000 (size=0x21000)  
Permissions: rw-  
Pathname: [stack]  
```

```
gef➤ xinfo $rsi
──────────────────── xinfo: 0x555555556004 ────────────────────
Page: 0x555555556000 → 0x555555557000 (size=0x1000)  
Permissions: r--  
Pathname: /home/user/binaries/ch12-keygenme/keygenme_O0  
Segment: .rodata  
```

- `RDI` → pile → buffer local contenant l'entrée utilisateur nettoyée  
- `RSI` → `.rodata` → constante compilée dans le binaire = le mot de passe attendu

La chaîne attendue est une constante statique en lecture seule. Elle aurait pu être trouvée avec un simple `strings` sur le binaire, mais l'objectif du checkpoint est de la capturer dynamiquement via le contexte GEF.

**Inspection approfondie avec `dereference` :**

```
gef➤ dereference $rdi 4
0x00007fffffffe050│+0x0000: "test123"           ← $rdi
0x00007fffffffe058│+0x0008: 0x0000000000000000
0x00007fffffffe060│+0x0010: 0x0000000000000000
0x00007fffffffe068│+0x0018: 0x0000000000000000

gef➤ dereference $rsi 4
0x0000555555556004│+0x0000: "s3cr3t_k3y"        ← $rsi
0x000055555555600c│+0x0008: 0x0000000000007934
0x0000555555556014│+0x0010: "Mot de passe correct !"
0x000055555555601c│+0x0018: "e correct !"
```

Le `dereference` sur `RSI` révèle un bonus : la chaîne `"Mot de passe correct !"` se trouve juste après le mot de passe attendu dans `.rodata`. C'est le message de succès — une confirmation supplémentaire que nous sommes au bon endroit.

### 6. Flux de contrôle post-comparaison

```
gef➤ finish  
Run till exit from #0  strcmp () ...  
0x0000555555555268 in check_password ()
```

Le contexte après le retour de `strcmp` :

```
──────────────────────────────── registers ────────────────────────────────
$rax   : 0xffffffffffffffa1             ← retour de strcmp, non nul (chaînes différentes)
...
──────────────────────────────────── code ─────────────────────────────────
   0x555555555263 <check_password+30>: call   0x555555555060 <strcmp@plt>
 → 0x555555555268 <check_password+35>: test   eax, eax
   0x55555555526a <check_password+37>: jne    0x55555555527e <check_password+57>
   0x55555555526c <check_password+39>: lea    rdi, [rip+0xda1]    ; "Mot de passe correct !"
   0x555555555273 <check_password+46>: call   0x555555555030 <puts@plt>
   ...
   0x55555555527e <check_password+57>: lea    rdi, [rip+0xd93]    ; "Mot de passe incorrect."
   0x555555555285 <check_password+64>: call   0x555555555030 <puts@plt>
```

**Analyse du flux :**

- `RAX = 0xffffffffffffffa1` — valeur non nulle, les chaînes diffèrent.  
- L'instruction suivante est `test eax, eax` qui positionne `ZF` selon que `EAX` est nul ou non.  
- `jne 0x55555555527e` saute vers le message d'échec si `ZF == 0` (c'est-à-dire si `EAX != 0`, c'est-à-dire si les chaînes diffèrent).  
- Si `ZF == 1` (chaînes identiques), l'exécution tombe dans le bloc de succès (`"Mot de passe correct !"`).

Le pattern est : `strcmp` → `test eax, eax` → `jne échec` → (sinon) succès. C'est la variante classique « le code de succès est le fall-through, le code d'échec est le saut ».

```
gef➤ stepi
```

Après exécution du `test eax, eax`, le contexte montre les flags :

```
$eflags: [...SF ... NF ...] — ZF absent (ZF=0)
```

`ZF` n'est pas positionné car `EAX` était non nul. Le `jne` va donc être pris → échec.

```
gef➤ stepi
```

Le pointeur d'instruction (`RIP`) saute à `0x55555555527e` (le bloc d'échec), confirmant l'analyse.

### 7. Forcer le chemin de succès

On recommence depuis le saut conditionnel. Relancer le programme :

```
gef➤ run  
Entrez le mot de passe : test123  
```

GDB s'arrête sur `strcmp`. Avancer jusqu'au saut :

```
gef➤ finish  
gef➤ stepi          # exécuter test eax, eax  
```

Le contexte montre `ZF=0`. Forcer `ZF` à 1 :

```
gef➤ edit-flags +zero
```

Vérifier dans la section registres que `ZF` est maintenant actif. Continuer :

```
gef➤ continue
```

Sortie du programme :

```
Mot de passe correct !
```

Le programme a emprunté le chemin de succès grâce à la modification du flag. Cela confirme que notre compréhension du flux est correcte : la seule condition qui sépare le succès de l'échec est la valeur de `ZF` après le `test eax, eax`.

**Contre-vérification avec le bon mot de passe :**

```
gef➤ run  
Entrez le mot de passe : s3cr3t_k3y  
```

GDB s'arrête sur `strcmp`. Le contexte montre cette fois :

```
$rdi   : 0x00007fffffffe050  →  "s3cr3t_k3y"
$rsi   : 0x0000555555556004  →  "s3cr3t_k3y"
```

Les deux chaînes sont identiques.

```
gef➤ finish
```

`RAX = 0x0` — `strcmp` retourne 0.

```
gef➤ stepi          # test eax, eax
```

`ZF = 1` — les chaînes sont égales.

```
gef➤ stepi          # jne → NOT TAKEN
```

Le `jne` n'est pas pris. L'exécution tombe dans le bloc de succès.

```
gef➤ continue  
Mot de passe correct !  
```

Le bon mot de passe fonctionne sans modification de flags.

### 8. Vérification croisée avec pwndbg

```bash
gdb-pwndbg -q ./keygenme_O0
```

```
pwndbg> break strcmp  
pwndbg> run  
Entrez le mot de passe : test123  
```

À l'arrêt sur `strcmp`, le contexte pwndbg affiche :

```
 REGISTERS
*RAX  ...
*RDI  0x7fffffffe050 ◂— 'test123'
*RSI  0x555555556004 ◂— 's3cr3t_k3y'
 ...
```

Les astérisques devant `RDI` et `RSI` indiquent que ces registres ont été modifiés depuis le dernier arrêt. Les chaînes pointées sont affichées directement — même résultat que GEF, présentation différente.

Navigation jusqu'au saut conditionnel :

```
pwndbg> finish  
pwndbg> nextjmp  
```

Le contexte pwndbg s'arrête sur le `jne` et affiche :

```
 ► 0x55555555526a <check_password+37>    jne    0x55555555527e <check_password+57>    TAKEN
```

L'annotation `TAKEN` confirme que le saut va être pris (vers l'échec), cohérent avec un mot de passe incorrect.

**Différences observées entre GEF et pwndbg sur ce checkpoint :**

| Aspect | GEF | pwndbg |  
|---|---|---|  
| Registres modifiés | Coloration de la valeur | Astérisque `*` + ancienne valeur en grisé |  
| Prédiction de saut | Dépend de la version | `TAKEN` / `NOT TAKEN` explicite |  
| Arguments de `strcmp` | Visibles via déréférencement dans la section registres | Visibles dans les registres + annotés dans le désassemblage |  
| Atteindre le saut | `stepi` manuels | `nextjmp` (une commande) |  
| Modification de flags | `edit-flags +zero` (par nom) | `set $eflags \|= 0x40` (par masque) |

---

## Résumé des résultats

| Élément | Valeur |  
|---|---|  
| Mot de passe attendu | `s3cr3t_k3y` |  
| Emplacement du mot de passe | Section `.rodata` à l'offset `0x6004` |  
| Fonction de comparaison | `strcmp`, appelée depuis `check_password+30` |  
| Mécanisme de lecture d'entrée | `fgets` → nettoyage du `\n` via `strlen` → `strcmp` |  
| Pattern de branchement | `test eax, eax` → `jne` vers échec (succès = fall-through) |  
| Flag décisif | `ZF` (Zero Flag) après `test eax, eax` |  
| Protections du binaire | Canary ✓, NX ✓, PIE ✓, Full RELRO, Fortify ✗ |

---

## Erreurs courantes

**Le breakpoint sur `strcmp` ne se déclenche pas.** Si le programme utilise `strncmp` ou `memcmp` au lieu de `strcmp`, le breakpoint ne sera jamais atteint. Vérifier la table GOT avec `got` pour identifier la bonne fonction de comparaison et ajuster le breakpoint.

**Le contexte affiche `\n` dans la chaîne de l'utilisateur.** Si le contexte au moment de `strcmp` montre `"test123\n"` au lieu de `"test123"`, cela signifie que le programme n'a pas nettoyé le retour à la ligne de `fgets`. La comparaison échouera toujours, même avec le bon mot de passe. Ce n'est pas un bug de l'analyse — c'est un comportement réel du programme qu'il faut noter. Si le programme ne retire pas le `\n`, le mot de passe à fournir ne pourra jamais correspondre via `stdin` (sauf en envoyant l'entrée sans `\n` via un pipe ou `pwntools`).

**`edit-flags` ne semble pas avoir d'effet.** S'assurer d'exécuter `edit-flags` *après* le `test eax, eax` et *avant* le `jne`. Si la commande est exécutée avant le `test`, celui-ci écrasera les flags. L'ordre correct est : `finish` → `stepi` (exécute `test`) → `edit-flags +zero` → `stepi` ou `continue`.

**Le mot de passe capturé ne fonctionne pas en dehors de GDB.** Si le binaire est compilé avec PIE et ASLR, les adresses changent à chaque exécution, mais la chaîne dans `.rodata` reste la même. Le mot de passe `s3cr3t_k3y` fonctionne indépendamment de l'ASLR. Si le mot de passe ne fonctionne pas, vérifier que le `\n` en fin de saisie n'interfère pas avec le programme lancé hors GDB (tester avec `echo -n "s3cr3t_k3y" | ./keygenme_O0`).

**Le contexte GEF est trop volumineux et défile hors de l'écran.** Réduire le nombre de lignes affichées :

```
gef➤ gef config context.nb_lines_code 8  
gef➤ gef config context.nb_lines_stack 6  
```

Ou retirer temporairement les sections non nécessaires :

```
gef➤ gef config context.layout "regs code"
```

---


⏭️
