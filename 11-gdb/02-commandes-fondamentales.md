🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.2 — Commandes GDB fondamentales : `break`, `run`, `next`, `step`, `info`, `x`, `print`

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Lancer GDB

GDB s'invoque en lui passant le binaire à analyser en argument :

```bash
$ gdb ./keygenme_O0
```

GDB affiche son message d'accueil (version, licence) puis présente son prompt `(gdb)`. Le programme n'est **pas encore lancé** — GDB l'a simplement chargé en mémoire et a lu ses sections ELF, ses symboles et ses éventuelles informations DWARF. On est dans un état d'attente, prêt à configurer l'analyse avant de démarrer l'exécution.

Pour supprimer le message d'accueil et les informations de licence :

```bash
$ gdb -q ./keygenme_O0
(gdb)
```

Le flag `-q` (ou `--quiet`) deviendra vite un réflexe. On peut aussi passer des arguments au programme cible dès le lancement :

```bash
$ gdb -q --args ./keygenme_O0 mon_argument
```

Sans `--args`, GDB interpréterait `mon_argument` comme un core dump à charger. Avec `--args`, tout ce qui suit le nom du binaire est transmis au programme quand on le lancera.

### Le fichier `.gdbinit`

À chaque démarrage, GDB exécute les commandes contenues dans deux fichiers s'ils existent :

- `~/.gdbinit` — configuration globale, appliquée à toutes les sessions.  
- `.gdbinit` dans le répertoire courant — configuration spécifique au projet.

On peut y placer des réglages fréquents :

```
# ~/.gdbinit — exemple de configuration globale
set disassembly-flavor intel  
set pagination off  
set print pretty on  
```

La première ligne est particulièrement importante pour le RE : elle bascule l'affichage assembleur en **syntaxe Intel** au lieu de la syntaxe AT&T par défaut. Si vous avez suivi le chapitre 7 et travaillé avec `objdump -M intel`, vous retrouverez vos repères. Cette préférence peut aussi être définie en cours de session :

```
(gdb) set disassembly-flavor intel
```

> ⚠️ **Sécurité :** par défaut, GDB refuse d'exécuter un `.gdbinit` local pour éviter du code malveillant dans un répertoire non fiable. Il faut l'autoriser explicitement dans `~/.gdbinit` avec la ligne :  
> ```  
> set auto-load safe-path /  
> ```  
> Ou, de manière plus ciblée, indiquer le chemin de votre répertoire de travail.

## Contrôle de l'exécution

### `run` — lancer le programme

La commande `run` (abrégée `r`) démarre l'exécution du programme depuis son point d'entrée :

```
(gdb) run
Starting program: /home/user/binaries/ch11-keygenme/keygenme_O0  
Enter your key: _  
```

Le programme s'exécute normalement et attend l'entrée utilisateur. Si le programme attend des arguments en ligne de commande, on les passe directement à `run` :

```
(gdb) run ABCD-1234-EFGH
```

Ou, si on les a déjà spécifiés avec `--args` au lancement, `run` les utilise automatiquement. Pour modifier les arguments entre deux exécutions :

```
(gdb) set args XXXX-9999-YYYY
(gdb) run
```

On peut aussi rediriger l'entrée standard depuis un fichier — très utile quand le programme attend une saisie interactive qu'on veut automatiser :

```
(gdb) run < input.txt
```

### `start` — lancer et s'arrêter à `main()`

Si on veut commencer le débogage dès l'entrée dans `main()` sans poser manuellement un breakpoint :

```
(gdb) start
Temporary breakpoint 1 at 0x401196: file keygenme.c, line 35.  
Starting program: /home/user/binaries/ch11-keygenme/keygenme_O0  

Temporary breakpoint 1, main () at keygenme.c:35
35      int main(int argc, char *argv[]) {
```

GDB pose un breakpoint temporaire sur `main`, lance le programme, et s'arrête immédiatement. C'est un raccourci commode en début d'analyse. Notez que `start` nécessite que le symbole `main` soit présent — sur un binaire strippé, il faudra utiliser une autre approche (section 11.4).

### `continue` — reprendre l'exécution

Quand le programme est arrêté sur un breakpoint, `continue` (abrégé `c`) reprend l'exécution normale jusqu'au prochain breakpoint ou jusqu'à la fin du programme :

```
(gdb) continue
Continuing.  
Enter your key: TEST-KEY  
Wrong key!  
[Inferior 1 (process 12345) exited with code 01]
```

### `next` et `step` — avancer pas à pas

Ce sont les deux commandes de progression pas à pas, et leur différence est fondamentale :

**`next`** (abrégé `n`) exécute la ligne source courante en entier. Si cette ligne contient un appel de fonction, la fonction est exécutée intégralement et GDB s'arrête à la ligne suivante dans la fonction courante. `next` ne « descend » pas dans les fonctions appelées.

**`step`** (abrégé `s`) exécute la ligne source courante, mais si elle contient un appel de fonction, GDB entre dans cette fonction et s'arrête à sa première ligne.

Prenons un exemple concret avec ce code :

```c
35: int main(int argc, char *argv[]) {
36:     char input[64];
37:     printf("Enter your key: ");
38:     fgets(input, sizeof(input), stdin);
39:     if (check_key(input)) {
40:         printf("Correct!\n");
```

Si on est arrêté à la ligne 39 :

```
(gdb) next
# → Exécute check_key(input) en entier, s'arrête à la ligne 40 (ou au else)

(gdb) step
# → Entre dans check_key(), s'arrête à la première ligne de check_key()
```

En RE, `step` est ce qu'on utilise le plus souvent : on veut descendre dans les fonctions pour comprendre leur logique interne. `next` est utile pour « enjamber » les appels de bibliothèque qu'on ne souhaite pas explorer (comme `printf` ou `fgets`).

### `nexti` et `stepi` — avancer instruction par instruction

Les commandes `next` et `step` travaillent au niveau des **lignes source**. Leurs équivalents au niveau **instruction assembleur** sont :

- **`nexti`** (abrégé `ni`) — exécute une seule instruction machine. Si c'est un `call`, la fonction appelée est exécutée en entier.  
- **`stepi`** (abrégé `si`) — exécute une seule instruction machine. Si c'est un `call`, GDB entre dans la fonction appelée.

Ces commandes sont indispensables en RE, en particulier sur les binaires sans symboles où les commandes niveau source ne fonctionnent pas. Elles permettent un contrôle absolu, instruction par instruction :

```
(gdb) stepi
0x0000000000401162 in check_key ()
(gdb) stepi
0x0000000000401165 in check_key ()
```

Un raccourci extrêmement pratique : après un premier `stepi` ou `nexti`, appuyer sur **Entrée** sans taper de commande répète la dernière commande exécutée. On peut ainsi avancer instruction par instruction en pressant simplement Entrée de manière répétée.

### `finish` — terminer la fonction courante

Si on est entré dans une fonction avec `step` et qu'on a vu ce qu'on voulait voir, `finish` (abrégé `fin`) exécute le reste de la fonction et s'arrête juste après le `ret`, de retour dans la fonction appelante :

```
(gdb) finish
Run till exit from #0  check_key (input=0x7fffffffe100 "TEST-KEY\n") at keygenme.c:24
0x00000000004011a5 in main () at keygenme.c:39
39          if (check_key(input)) {
Value returned is $1 = 0
```

GDB affiche la valeur de retour (`Value returned is $1 = 0`), ce qui est souvent exactement l'information qu'on cherche — par exemple, savoir si `check_key` a retourné 0 (échec) ou 1 (succès).

### `until` — avancer jusqu'à une ligne ou adresse

`until` (abrégé `u`) continue l'exécution jusqu'à atteindre une ligne supérieure à la ligne courante dans la même fonction. C'est particulièrement utile pour sortir d'une boucle sans poser de breakpoint :

```
(gdb) until 45
# → Continue jusqu'à la ligne 45
```

On peut aussi donner une adresse :

```
(gdb) until *0x401190
```

## Points d'arrêt : `break`

### Breakpoints par nom de fonction

La forme la plus simple pose un breakpoint à l'entrée d'une fonction :

```
(gdb) break main
Breakpoint 1 at 0x401196: file keygenme.c, line 35.
(gdb) break check_key
Breakpoint 2 at 0x401156: file keygenme.c, line 24.
```

GDB indique l'adresse résolue et, si les symboles DWARF sont présents, le fichier et la ligne correspondants.

### Breakpoints par numéro de ligne

Avec des symboles DWARF, on peut poser un breakpoint directement sur un numéro de ligne :

```
(gdb) break keygenme.c:39
Breakpoint 3 at 0x40119e: file keygenme.c, line 39.
```

Si le fichier source courant est non ambigu, le nom de fichier est optionnel :

```
(gdb) break 39
```

### Breakpoints par adresse

C'est la méthode universelle, qui fonctionne même sans aucun symbole. On préfixe l'adresse avec `*` :

```
(gdb) break *0x401156
Breakpoint 4 at 0x401156
```

En RE sur binaire strippé, c'est la méthode principale. On repère l'adresse d'intérêt dans Ghidra ou `objdump`, puis on pose le breakpoint dans GDB.

### Breakpoints sur des appels de bibliothèque

On peut poser un breakpoint sur une fonction de bibliothèque partagée :

```
(gdb) break strcmp
Breakpoint 5 at 0x7ffff7e42a40
(gdb) break printf
Breakpoint 6 at 0x7ffff7e12e10
```

C'est une technique de RE fondamentale : plutôt que de chercher où le programme compare une clé, on pose un breakpoint sur `strcmp` (ou `memcmp`, `strncmp`) et on examine les arguments à chaque appel. GDB résout le nom via la PLT/GOT (chapitre 2, section 2.9).

### Gérer les breakpoints

```
(gdb) info breakpoints
Num     Type           Disp Enb Address            What
1       breakpoint     keep y   0x0000000000401196 in main at keygenme.c:35
2       breakpoint     keep y   0x0000000000401156 in check_key at keygenme.c:24
3       breakpoint     keep y   0x000000000040119e in main at keygenme.c:39
```

Les colonnes clés : `Num` est l'identifiant du breakpoint, `Enb` indique s'il est activé (`y`) ou désactivé (`n`), `Address` est l'adresse en mémoire.

Les commandes de gestion :

```
(gdb) disable 2         # Désactive le breakpoint 2 (reste en place mais ne déclenche plus)
(gdb) enable 2          # Réactive le breakpoint 2
(gdb) delete 2          # Supprime définitivement le breakpoint 2
(gdb) delete            # Supprime TOUS les breakpoints (demande confirmation)
```

### Breakpoints temporaires

Un breakpoint temporaire se supprime automatiquement après le premier déclenchement :

```
(gdb) tbreak check_key
Temporary breakpoint 7 at 0x401156: file keygenme.c, line 24.
```

C'est exactement ce que `start` utilise en interne. Les breakpoints temporaires sont utiles quand on veut s'arrêter une seule fois à un endroit (par exemple, la première fois qu'une fonction d'initialisation est appelée).

## Afficher des informations : `print`, `x`, `info`, `display`

### `print` — évaluer et afficher une expression

`print` (abrégé `p`) est la commande la plus polyvalente de GDB. Elle évalue une expression C et affiche le résultat :

```
(gdb) print argc
$1 = 1
(gdb) print argv[0]
$2 = 0x7fffffffe3a0 "/home/user/keygenme_O0"
(gdb) print input
$3 = "TEST-KEY\n\000\000\000..."
```

Chaque résultat est stocké dans une variable numérotée (`$1`, `$2`, `$3`...) réutilisable dans les expressions suivantes :

```
(gdb) print $1 + 5
$4 = 6
```

#### Formater la sortie de `print`

On peut forcer le format d'affichage avec un suffixe `/` :

```
(gdb) print/x argc       # Hexadécimal
$5 = 0x1
(gdb) print/t argc       # Binaire
$6 = 1
(gdb) print/c 0x41       # Caractère
$7 = 65 'A'
(gdb) print/d 0xff       # Décimal signé
$8 = -1
(gdb) print/u 0xff       # Décimal non signé
$9 = 255
```

Les formats disponibles sont :

| Suffixe | Format |  
|---|---|  
| `/x` | Hexadécimal |  
| `/d` | Décimal signé |  
| `/u` | Décimal non signé |  
| `/t` | Binaire |  
| `/o` | Octal |  
| `/c` | Caractère |  
| `/f` | Flottant |  
| `/a` | Adresse (symbolique si possible) |  
| `/s` | Chaîne de caractères (null-terminated) |

#### `print` sur les registres

On accède aux registres en les préfixant par `$` :

```
(gdb) print $rax
$10 = 0
(gdb) print/x $rdi
$11 = 0x7fffffffe100
(gdb) print (char *)$rdi
$12 = 0x7fffffffe100 "TEST-KEY\n"
```

La dernière forme est extrêmement utile : on caste le contenu d'un registre vers un type C pour que GDB l'interprète correctement. Ici, `$rdi` contient un pointeur vers une chaîne — en le castant en `char *`, GDB affiche la chaîne pointée.

#### `print` avec des expressions complexes

`print` accepte des expressions C arbitraires, y compris des déréférencements de pointeurs, des accès à des membres de structures, et de l'arithmétique :

```
(gdb) print *player               # Déréférence un pointeur vers une structure
(gdb) print player->health        # Accès à un champ
(gdb) print input[5]              # Accès à un élément de tableau
(gdb) print strlen(input)         # Appel de fonction (!)
```

La dernière forme est remarquable : GDB peut **appeler des fonctions** du programme ou des bibliothèques chargées. Cela signifie qu'on peut appeler `strlen`, `strcmp`, `printf` ou même des fonctions du programme lui-même directement depuis le prompt GDB. C'est un outil puissant, mais à utiliser avec prudence — l'appel modifie l'état du processus (pile, registres, effets de bord).

### `x` — examiner la mémoire brute

Là où `print` interprète des expressions C, `x` (*examine*) lit la mémoire brute à une adresse donnée. Sa syntaxe est :

```
x/NFS adresse
```

Où `N` est le nombre d'unités à afficher, `F` est le format, et `S` est la taille de chaque unité.

**Formats** (`F`) — les mêmes que pour `print` : `x` (hex), `d` (décimal), `s` (chaîne), `i` (instruction assembleur), `c` (caractère), `t` (binaire), `a` (adresse), `f` (flottant).

**Tailles** (`S`) :

| Lettre | Taille | Nom |  
|---|---|---|  
| `b` | 1 octet | byte |  
| `h` | 2 octets | halfword |  
| `w` | 4 octets | word |  
| `g` | 8 octets | giant (quad word) |

Quelques exemples concrets qui couvrent les cas d'usage les plus fréquents en RE :

```
(gdb) x/s 0x402010
0x402010: "Enter your key: "
```

Affiche la chaîne null-terminated à l'adresse `0x402010`. Utile pour vérifier le contenu des chaînes dans `.rodata`.

```
(gdb) x/20bx 0x7fffffffe100
0x7fffffffe100: 0x54 0x45 0x53 0x54 0x2d 0x4b 0x45 0x59
0x7fffffffe108: 0x0a 0x00 0x00 0x00 0x00 0x00 0x00 0x00
0x7fffffffe110: 0x00 0x00 0x00 0x00
```

Affiche 20 octets en hexadécimal à partir de l'adresse donnée. On reconnaît « TEST-KEY\n » (0x54='T', 0x45='E', etc.). C'est la vue « hex dump » la plus courante.

```
(gdb) x/4gx $rsp
0x7fffffffe0f0: 0x0000000000000001  0x00007fffffffe3a0
0x7fffffffe100: 0x59454b2d54534554  0x000000000000000a
```

Affiche 4 mots de 8 octets (giant) en hexadécimal à partir du sommet de la pile. Essentiel pour inspecter les arguments et variables locales sur la pile.

```
(gdb) x/10i $rip
=> 0x401156 <check_key>:     push   rbp
   0x401157 <check_key+1>:   mov    rbp,rsp
   0x40115a <check_key+4>:   sub    rsp,0x30
   0x40115e <check_key+8>:   mov    QWORD PTR [rbp-0x28],rdi
   0x401162 <check_key+12>:  mov    rax,QWORD PTR [rbp-0x28]
   ...
```

Le format `i` est celui du **désassemblage**. `x/10i $rip` affiche les 10 prochaines instructions à partir du pointeur d'instruction courant. C'est la commande de référence pour voir le code assembleur autour du point d'exécution actuel, et elle fonctionne même sans symboles. La flèche `=>` indique l'instruction qui va être exécutée au prochain `stepi`.

### `display` — affichage automatique à chaque arrêt

Si on veut voir la même information après chaque `step`, `next` ou breakpoint, `display` évite de retaper `print` ou `x` en boucle :

```
(gdb) display/x $rax
1: /x $rax = 0x0
(gdb) display/i $rip
2: x/i $rip
=> 0x40115e <check_key+8>:  mov    QWORD PTR [rbp-0x28],rdi
(gdb) display/s $rdi
3: x/s $rdi
0x7fffffffe100: "TEST-KEY\n"
```

Après chaque arrêt, GDB affichera automatiquement ces trois informations. Pour gérer les affichages :

```
(gdb) info display        # Lister les displays actifs
(gdb) undisplay 2         # Supprimer le display numéro 2
(gdb) disable display 1   # Désactiver temporairement le display 1
```

Un ensemble de `display` bien choisi transforme la session GDB en un tableau de bord en temps réel. Une configuration classique pour le RE assembleur :

```
(gdb) display/x $rax
(gdb) display/x $rdi
(gdb) display/x $rsi
(gdb) display/6i $rip
```

On voit ainsi, à chaque pas, la valeur de retour / accumulateur (`rax`), les deux premiers arguments de fonction (`rdi`, `rsi`) et les prochaines instructions à exécuter.

### `info` — interroger l'état de GDB et du programme

`info` est un préfixe qui ouvre l'accès à une grande variété d'informations. Les sous-commandes les plus utiles en RE :

```
(gdb) info registers
```

Affiche la valeur de tous les registres généraux. C'est l'instantané complet de l'état du processeur :

```
rax    0x0                 0  
rbx    0x0                 0  
rcx    0x7ffff7f14a80      140737353030272  
rdx    0x7fffffffe218      140737488347672  
rsi    0x7fffffffe208      140737488347656  
rdi    0x1                 1  
rbp    0x7fffffffe0f0      0x7fffffffe0f0  
rsp    0x7fffffffe0f0      0x7fffffffe0f0  
rip    0x401196            0x401196 <main>  
eflags 0x246               [ PF ZF IF ]  
...
```

Pour les registres flottants et SIMD :

```
(gdb) info all-registers    # Tous les registres, y compris SSE/AVX
```

Autres sous-commandes essentielles :

```
(gdb) info breakpoints      # Liste des breakpoints (vu plus haut)
(gdb) info functions         # Liste de toutes les fonctions connues
(gdb) info locals            # Variables locales du frame courant (nécessite DWARF)
(gdb) info args              # Arguments de la fonction courante (nécessite DWARF)
(gdb) info frame             # Détails sur le frame de pile courant
(gdb) info proc mappings     # Carte mémoire du processus (sections, bibliothèques)
(gdb) info sharedlibrary     # Bibliothèques partagées chargées
(gdb) info threads           # Liste des threads
```

La commande `info proc mappings` mérite une attention particulière — elle affiche les plages d'adresses virtuelles du processus, équivalent de `/proc/<pid>/maps` :

```
(gdb) info proc mappings
  Start Addr           End Addr       Size     Offset  Perms  objfile
  0x00400000         0x00401000     0x1000        0x0  r--p   keygenme_O0
  0x00401000         0x00402000     0x1000     0x1000  r-xp   keygenme_O0
  0x00402000         0x00403000     0x1000     0x2000  r--p   keygenme_O0
  0x7ffff7dc0000     0x7ffff7de8000 0x28000        0x0  r--p   libc.so.6
  ...
```

On y voit les segments du binaire (code en `r-xp`, données en lecture seule en `r--p`), les bibliothèques partagées, la pile, le heap. C'est indispensable pour comprendre le layout mémoire du processus au moment de l'analyse.

## Désassemblage dans GDB : `disassemble`

La commande `disassemble` (abrégée `disas`) est le complément de `x/i`. Elle désassemble une fonction entière :

```
(gdb) disassemble check_key
Dump of assembler code for function check_key:
   0x0000000000401156 <+0>:     push   rbp
   0x0000000000401157 <+1>:     mov    rbp,rsp
   0x000000000040115a <+4>:     sub    rsp,0x30
   0x000000000040115e <+8>:     mov    QWORD PTR [rbp-0x28],rdi
   ...
   0x00000000004011a2 <+76>:    leave
   0x00000000004011a3 <+77>:    ret
End of assembler dump.
```

Les offsets entre chevrons (`<+0>`, `<+1>`, `<+4>`...) indiquent la distance en octets depuis le début de la fonction, ce qui facilite la navigation.

Si on est arrêté au milieu d'une fonction, on peut utiliser :

```
(gdb) disassemble $rip-20, $rip+40
```

Cela désassemble une plage d'adresses autour du point d'exécution courant. C'est la technique à utiliser sur les binaires strippés où GDB ne connaît pas les limites des fonctions.

Pour mélanger code source et assembleur (nécessite DWARF) :

```
(gdb) disassemble /m check_key
```

Ou sa variante améliorée `/s` qui gère mieux les optimisations :

```
(gdb) disassemble /s check_key
```

## Modifier l'état du programme : `set`

GDB ne se contente pas d'observer — il peut modifier l'état du programme en cours d'exécution. C'est un outil fondamental en RE.

### Modifier un registre

```
(gdb) set $rax = 1
(gdb) set $rip = 0x4011a0
```

La première commande force la valeur de `rax` à 1 — par exemple, pour simuler un retour « succès » d'une fonction de vérification. La seconde modifie le pointeur d'instruction, faisant sauter l'exécution à une autre adresse. C'est l'équivalent dynamique du patching binaire vu au chapitre 21.6.

### Modifier la mémoire

```
(gdb) set {int}0x7fffffffe100 = 0x41414141
(gdb) set {char}0x402010 = 'X'
```

La syntaxe `{type}adresse` permet d'écrire en mémoire avec le type spécifié. On peut aussi utiliser des variables si les symboles sont disponibles :

```
(gdb) set variable result = 1
```

### Modifier le flux d'exécution : `jump`

```
(gdb) jump *0x4011a0
(gdb) jump keygenme.c:40
```

`jump` est similaire à `set $rip = ...` mais déclenche aussi la reprise de l'exécution. Attention : sauter à un endroit arbitraire sans ajuster la pile peut provoquer un crash. C'est néanmoins utile pour contourner un branchement conditionnel — par exemple, sauter par-dessus un `if` qui vérifie une licence.

## Récapitulatif des commandes essentielles

Pour référence rapide, voici les commandes couvertes dans cette section avec leurs abréviations :

| Commande | Abrév. | Action |  
|---|---|---|  
| `run [args]` | `r` | Lancer le programme |  
| `start` | — | Lancer et s'arrêter à `main()` |  
| `continue` | `c` | Reprendre l'exécution |  
| `next` | `n` | Avancer d'une ligne (sans entrer dans les fonctions) |  
| `step` | `s` | Avancer d'une ligne (en entrant dans les fonctions) |  
| `nexti` | `ni` | Avancer d'une instruction (sans entrer dans les `call`) |  
| `stepi` | `si` | Avancer d'une instruction (en entrant dans les `call`) |  
| `finish` | `fin` | Terminer la fonction courante |  
| `until [loc]` | `u` | Continuer jusqu'à une ligne/adresse |  
| `break [loc]` | `b` | Poser un breakpoint |  
| `tbreak [loc]` | `tb` | Breakpoint temporaire |  
| `delete [n]` | `d` | Supprimer un breakpoint |  
| `disable [n]` | `dis` | Désactiver un breakpoint |  
| `enable [n]` | `en` | Réactiver un breakpoint |  
| `print[/fmt] expr` | `p` | Afficher une expression |  
| `x/NFS addr` | — | Examiner la mémoire brute |  
| `display[/fmt] expr` | — | Affichage automatique à chaque arrêt |  
| `info registers` | `i r` | Afficher les registres |  
| `info breakpoints` | `i b` | Lister les breakpoints |  
| `info locals` | — | Variables locales |  
| `info proc mappings` | — | Carte mémoire du processus |  
| `disassemble` | `disas` | Désassembler une fonction |  
| `set $reg = val` | — | Modifier un registre |  
| `set {type}addr = val` | — | Modifier la mémoire |  
| `jump loc` | `j` | Sauter à une adresse et continuer |  
| `quit` | `q` | Quitter GDB |

> 💡 **Astuce mnémotechnique :** les commandes de progression forment une hiérarchie naturelle. Au niveau le plus haut, `continue` exécute tout jusqu'au prochain breakpoint. En dessous, `next` et `step` avancent ligne par ligne. Encore en dessous, `nexti` et `stepi` avancent instruction par instruction. À chaque niveau, la variante sans `i` reste dans la fonction courante, et la variante avec `i` (ou `step`) descend dans les appels.

---

> **À retenir :** Ces commandes forment le vocabulaire de base de toute session GDB. En RE, la boucle de travail typique est : poser un breakpoint sur une fonction d'intérêt (identifiée en analyse statique), lancer le programme, inspecter les registres et la mémoire au point d'arrêt, puis avancer pas à pas pour observer le comportement. Maîtriser `break`, `run`, `stepi`, `x` et `print` suffit pour conduire la majorité des analyses dynamiques — les sections suivantes ajouteront des capacités plus fines, mais le cœur est ici.

⏭️ [Inspecter la pile, les registres, la mémoire (format et tailles)](/11-gdb/03-inspecter-pile-registres-memoire.md)
