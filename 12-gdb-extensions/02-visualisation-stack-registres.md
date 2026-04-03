🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 12.2 — Visualisation de la stack et des registres en temps réel

> **Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg**  
> **Partie III — Analyse Dynamique**

---

## Le contexte automatique : le cœur de l'expérience

La fonctionnalité qui justifie à elle seule l'installation d'une extension GDB est l'affichage automatique du **contexte** à chaque point d'arrêt. Dans GDB vanilla, après un `stepi`, le débogueur affiche une seule ligne — l'instruction courante — et rend la main. L'analyste doit reconstruire mentalement l'état de la machine en enchaînant des commandes d'inspection. Avec une extension, chaque arrêt produit un tableau de bord complet qui présente simultanément les registres, la pile, le désassemblage environnant, et parfois le code source correspondant.

Ce tableau de bord est déclenché par un **hook GDB** : les extensions enregistrent une fonction Python sur l'événement `stop`, qui s'exécute automatiquement chaque fois que le programme s'arrête, quelle qu'en soit la raison (breakpoint, watchpoint, signal, fin de `stepi` ou `nexti`). Aucune intervention manuelle n'est nécessaire — le contexte apparaît, l'analyste lit, puis tape sa prochaine commande.

---

## Anatomie du contexte dans chaque extension

### Le contexte de PEDA

PEDA affiche trois blocs séparés par des lignes de tirets colorés.

Le premier bloc, **registers**, liste les registres généraux 64 bits (`RAX` à `R15`), le pointeur d'instruction `RIP`, le pointeur de pile `RSP` et le pointeur de base `RBP`. Chaque valeur est suivie d'un premier niveau de déréférencement : si `RAX` contient une adresse valide, PEDA affiche la valeur stockée à cette adresse. Si cette valeur est elle-même un pointeur vers une chaîne de caractères ASCII lisible, la chaîne est affichée entre guillemets. Ce déréférencement à un seul niveau est suffisant pour repérer rapidement un argument de type `char *` dans `RDI` avant un `call`, mais il ne suit pas les chaînes de pointeurs plus profondes.

Le deuxième bloc, **code**, montre le désassemblage autour de l'instruction courante. L'instruction sur le point d'être exécutée est mise en évidence par une flèche `=>` et une coloration distincte. Quelques instructions avant et après sont affichées pour donner le contexte du flux de contrôle.

Le troisième bloc, **stack**, affiche les premières entrées de la pile à partir de `RSP`. Chaque slot de 8 octets (sur x86-64) est présenté avec son adresse, sa valeur brute et un déréférencement d'un niveau, identique à celui des registres.

```
[----------------------------------registers-----------------------------------]
RAX: 0x0  
RBX: 0x0  
RCX: 0x7ffff7e15a80 (<__libc_start_call_main+128>: mov edi,eax)  
RDX: 0x0  
RSI: 0x7fffffffe1a8 --> 0x7fffffffe47a ("./keygenme_O0")  
RDI: 0x1  
...
[-------------------------------------code-------------------------------------]
   0x555555555169 <main>:       push   rbp
   0x55555555516a <main+1>:     mov    rbp,rsp
   0x55555555516d <main+4>:     sub    rsp,0x30
=> 0x555555555171 <main+8>:     mov    DWORD PTR [rbp-0x24],edi
   0x555555555174 <main+11>:    mov    QWORD PTR [rbp-0x30],rsi
...
[------------------------------------stack-------------------------------------]
0000| 0x7fffffffe090 --> 0x7fffffffe1a8 --> 0x7fffffffe47a ("./keygenme_O0")
0008| 0x7fffffffe098 --> 0x100000000
0016| 0x7fffffffe0a0 --> 0x0
...
```

L'affichage est fonctionnel et lisible, mais manque de granularité : on ne peut pas facilement voir quels registres ont changé depuis le dernier arrêt, et le déréférencement limité oblige à taper des commandes supplémentaires pour suivre des structures de données en mémoire.

### Le contexte de GEF

GEF organise son contexte en **sections** modulaires, chacune identifiée par un bandeau de couleur. Par défaut, les sections affichées sont (dans cet ordre) : `registers`, `stack`, `code`, `threads` et `trace`.

La section **registers** de GEF apporte deux améliorations par rapport à PEDA. D'abord, les registres dont la valeur a changé depuis le dernier arrêt sont affichés dans une couleur différente (typiquement rouge ou jaune selon le thème), ce qui permet de repérer immédiatement l'effet de l'instruction qui vient de s'exécuter. Ensuite, le déréférencement est récursif : GEF suit les chaînes de pointeurs jusqu'à atteindre une valeur scalaire, une chaîne de caractères ou une adresse non mappée. Cela donne des lignes comme :

```
$rdi   : 0x00007fffffffe1a8  →  0x00007fffffffe47a  →  "./keygenme_O0"
```

Ici, `RDI` pointe vers une entrée de `argv` qui elle-même pointe vers la chaîne du nom du programme. Les deux niveaux d'indirection sont visibles sans commande supplémentaire.

La section **stack** de GEF utilise la même logique de déréférencement récursif. Chaque entrée de la pile est suivie d'une chaîne de flèches (`→`) jusqu'à la valeur finale. Les adresses qui appartiennent à des régions connues (pile, heap, bibliothèques partagées, sections du binaire) sont annotées avec le nom de la région entre crochets, ce qui aide à distinguer immédiatement un pointeur vers le tas d'un pointeur vers le code.

La section **code** affiche le désassemblage avec coloration syntaxique. L'instruction courante est marquée par une flèche verte `→`. Si les symboles DWARF sont présents, GEF peut intercaler les lignes de code source C/C++ correspondantes au-dessus du bloc assembleur, ce qui facilite la corrélation entre la logique haut niveau et les instructions machine.

La section **trace** montre la backtrace compacte (pile d'appels), équivalente à un `bt` mais intégrée au contexte. Cette section est précieuse pour garder en permanence une vue d'ensemble de la profondeur d'appel sans taper de commande.

### Le contexte de pwndbg

pwndbg pousse l'affichage contextuel encore plus loin. Ses sections par défaut sont `REGISTERS`, `DISASM`, `STACK` et `BACKTRACE`, mais plusieurs comportements supplémentaires sont activés automatiquement selon la situation.

La section **REGISTERS** de pwndbg est la plus informative des trois. Chaque registre modifié est mis en évidence, et l'ancienne valeur apparaît à côté en gris ou en couleur atténuée, permettant de voir à la fois l'état actuel et la transition. Le déréférencement récursif est présent, avec en plus une détection contextuelle : si `RDI` contient une adresse qui correspond au premier argument attendu par la fonction libc sur le point d'être appelée, pwndbg l'annote. Par exemple, juste avant un `call strcmp@plt`, on peut voir :

```
 RAX  0x0
 RBX  0x0
*RDI  0x7fffffffe0b0 ◂— 'user_input'
*RSI  0x555555556020 ◂— 'expected_key'
```

L'astérisque devant `RDI` et `RSI` indique que ces registres ont été modifiés. Les chaînes pointées sont directement lisibles — l'analyste voit les deux arguments de `strcmp` sans aucune commande supplémentaire.

La section **DISASM** de pwndbg va au-delà du simple désassemblage linéaire. Lorsqu'une instruction `call` est rencontrée, pwndbg résout la cible et affiche le nom de la fonction. Pour les sauts conditionnels, il indique si le saut sera pris ou non en analysant l'état actuel des flags dans `RFLAGS`. Cette annotation prédictive (`TAKEN` / `NOT TAKEN`) épargne à l'analyste le calcul mental consistant à vérifier le flag `ZF`, `CF` ou `SF` pour déterminer le prochain chemin d'exécution.

```
 ► 0x5555555551c2 <main+89>    je     0x5555555551d8 <main+111>    NOT TAKEN
```

La section **STACK** utilise la commande `telescope` intégrée. Le terme « telescope » décrit bien le principe : chaque entrée de la pile est suivie d'une chaîne de déréférencement qui « zoome » à travers les indirections successives. Le nombre de niveaux est configurable. Par défaut, pwndbg affiche 8 entrées de pile, chacune avec un déréférencement complet.

---

## La commande `telescope` en détail

La commande `telescope` (disponible dans GEF et pwndbg, absente de PEDA) est l'un des outils les plus utiles pour comprendre l'état de la mémoire. Elle prend une adresse en argument et affiche une série de slots mémoire, chacun suivi de la chaîne complète de déréférencement.

Dans pwndbg :

```
pwndbg> telescope $rsp 12
```

Cette commande affiche les 12 premiers slots de 8 octets à partir du sommet de la pile. Chaque ligne montre l'offset par rapport à `RSP`, l'adresse absolue, la valeur brute et la chaîne de déréférencement :

```
00:0000│ rsp 0x7fffffffe090 —▸ 0x7fffffffe1a8 —▸ 0x7fffffffe47a ◂— './keygenme_O0'
01:0008│     0x7fffffffe098 ◂— 0x100000000
02:0010│ rbp 0x7fffffffe0a0 ◂— 0x0
03:0018│     0x7fffffffe0a8 —▸ 0x7ffff7e15a80 ◂— mov edi, eax
04:0020│     0x7fffffffe0b0 ◂— 'user_input'
...
```

La colonne de gauche indique l'offset. Lorsqu'un registre pointe sur un slot donné, son nom est affiché (ici `rsp` pour le slot `00` et `rbp` pour le slot `02`). Les symboles `—▸` indiquent un pointeur valide vers une adresse mappée, tandis que `◂—` marque la valeur finale (soit une constante, soit une chaîne de caractères, soit une instruction désassemblée).

Dans GEF, la commande équivalente s'appelle `dereference` :

```
gef➤ dereference $rsp 12
```

Le format de sortie est légèrement différent mais le principe est identique : déréférencement récursif avec annotation des régions mémoire.

`telescope` est particulièrement précieux pour inspecter les frames de pile lors de l'analyse d'un appel de fonction. En pointant sur `RBP` plutôt que `RSP`, on visualise d'un coup le saved `RBP`, l'adresse de retour et les variables locales de la frame appelante :

```
pwndbg> telescope $rbp 4
00:0000│ rbp 0x7fffffffe0c0 —▸ 0x7fffffffe0e0 ◂— 0x0        # saved RBP
01:0008│     0x7fffffffe0c8 —▸ 0x555555555210 <main+200>     # adresse de retour
02:0010│     0x7fffffffe0d0 ◂— 0x41414141                    # variable locale
03:0018│     0x7fffffffe0d8 ◂— 0x0
```

---

## Configurer et personnaliser l'affichage

### Configuration dans GEF

GEF offre le système de configuration le plus granulaire. Chaque aspect de l'affichage est contrôlé par une variable accessible via `gef config`.

Pour lister toutes les variables de configuration liées au contexte :

```
gef➤ gef config context
```

Quelques paramètres fréquemment ajustés :

```
gef➤ gef config context.nb_lines_code 12  
gef➤ gef config context.nb_lines_stack 10  
gef➤ gef config context.nb_lines_code_prev 5  
```

Le premier paramètre contrôle le nombre de lignes de désassemblage affichées *après* l'instruction courante, le deuxième le nombre d'entrées de pile, et le troisième le nombre de lignes *avant* l'instruction courante. Augmenter ces valeurs donne plus de contexte mais consomme plus d'espace vertical dans le terminal — un compromis à ajuster selon la taille de l'écran.

Pour choisir quelles sections afficher et dans quel ordre :

```
gef➤ gef config context.layout "regs code stack trace extra"
```

On peut retirer une section en l'omettant de la liste. Par exemple, pour un binaire strippé sans symboles DWARF, la section `extra` (code source) est inutile et peut être supprimée pour gagner de la place :

```
gef➤ gef config context.layout "regs code stack"
```

Pour rendre ces changements permanents, GEF propose de sauvegarder la configuration :

```
gef➤ gef save
```

Cela écrit un fichier `~/.gef.rc` qui sera rechargé automatiquement aux prochaines sessions.

### Configuration dans pwndbg

pwndbg utilise un système similaire. La commande `config` (ou `configfile`) permet de modifier les paramètres :

```
pwndbg> config context-stack-lines 12  
pwndbg> config context-code-lines 14  
```

Pour désactiver une section entière du contexte :

```
pwndbg> config context-sections "regs disasm stack backtrace"
```

Les modifications sont persistées dans le fichier `~/.pwndbg` (créé automatiquement). pwndbg supporte également un fichier de thème (`~/.pwndbg-theme`) pour ajuster les couleurs indépendamment de la logique d'affichage.

### Configuration dans PEDA

PEDA offre moins de flexibilité. La configuration se fait en modifiant les variables Python dans `peda.py` ou via la commande `pset` :

```
gdb-peda$ pset option context "register,code,stack"  
gdb-peda$ pset option context_code_lines 12  
```

Les options sont moins nombreuses et moins bien documentées que dans GEF ou pwndbg. C'est l'une des raisons pour lesquelles PEDA est moins adaptée à un usage prolongé que ses deux successeurs.

---

## Affichage conditionnel : adapter le contexte à la situation

Un piège courant avec les extensions GDB est la **surcharge d'information**. Lorsqu'on débogue un programme complexe avec de nombreux threads, ou lorsqu'on exécute des centaines de `stepi` dans une boucle, le défilement constant du contexte peut noyer l'information utile.

Les trois extensions permettent de désactiver temporairement le contexte :

```
# GEF
gef➤ gef config context.enable false
# ... exécuter plusieurs commandes sans contexte ...
gef➤ gef config context.enable true

# pwndbg
pwndbg> set context-output /dev/null
# ... ou plus simplement :
pwndbg> ctx off  
pwndbg> ctx on  

# PEDA
gdb-peda$ pset option context "none"  
gdb-peda$ pset option context "register,code,stack"  
```

Dans GEF et pwndbg, on peut également forcer un réaffichage du contexte sans exécuter d'instruction, ce qui est utile après avoir modifié manuellement un registre ou une zone mémoire :

```
# GEF
gef➤ context

# pwndbg
pwndbg> context
```

---

## Surveiller des registres et des adresses spécifiques

Au-delà du contexte global, il est fréquent de vouloir surveiller une adresse ou un registre particulier tout au long de l'exécution. Les extensions proposent des mécanismes complémentaires aux watchpoints de GDB vanilla.

### La commande `display` de GDB (toujours disponible)

Avant d'utiliser les commandes des extensions, rappelons que GDB vanilla possède la commande `display`, qui affiche automatiquement une expression à chaque arrêt :

```
(gdb) display/x $rax
(gdb) display/s (char*)$rdi
(gdb) display/4gx $rsp
```

Ces expressions s'ajoutent au contexte de l'extension et apparaissent dans la sortie à chaque arrêt. C'est un moyen simple de suivre une variable locale ou un registre qui n'apparaît pas assez en évidence dans le contexte standard.

### Commandes de surveillance dans GEF

GEF propose la commande `registers` pour afficher les registres à la demande avec un format personnalisé, mais surtout la commande `memory watch` pour surveiller une zone mémoire en continu :

```
gef➤ memory watch 0x555555558040 16 byte
```

Cette commande ajoute une section supplémentaire au contexte, qui affiche les 16 octets à partir de l'adresse spécifiée à chaque arrêt. On peut empiler plusieurs zones de surveillance :

```
gef➤ memory watch $rbp-0x10 8 qword  
gef➤ memory watch 0x555555556020 32 byte  
```

Pour retirer une surveillance :

```
gef➤ memory unwatch 0x555555558040
```

### Commandes de surveillance dans pwndbg

pwndbg n'a pas d'équivalent direct à `memory watch` de GEF, mais il intègre la commande `display` de GDB de manière fluide dans son contexte. De plus, la commande `hexdump` permet une inspection rapide formatée :

```
pwndbg> hexdump $rsp 64
```

Pour une surveillance continue, l'approche recommandée avec pwndbg est de combiner `display` avec des expressions de formatage GDB :

```
pwndbg> display/4gx $rsp  
pwndbg> display/s *(char**)($rbp-0x18)  
```

---

## Application au reverse engineering : lire la pile comme un livre

La visualisation en temps réel prend tout son sens lors de l'analyse concrète d'un binaire. Prenons un scénario typique : on veut comprendre comment un programme traite un mot de passe entré par l'utilisateur.

Avec GEF ou pwndbg actif, on pose un breakpoint sur la fonction de comparaison :

```
gef➤ break strcmp  
gef➤ run  
Entrez le mot de passe : test123  
```

Au moment où `strcmp` est atteinte, le contexte s'affiche automatiquement. La section des registres montre immédiatement les deux arguments — la convention System V AMD64 place les deux premiers arguments dans `RDI` et `RSI` :

```
$rdi : 0x00007fffffffe0b0  →  "test123"
$rsi : 0x0000555555556004  →  "s3cr3t_p4ss"
```

Sans extension, obtenir cette information aurait nécessité :

```
(gdb) info registers rdi rsi
(gdb) x/s $rdi
(gdb) x/s $rsi
```

Trois commandes au lieu de zéro. Sur une session de débogage de plusieurs heures avec des centaines de points d'arrêt, cette différence se traduit en un gain de temps considérable et surtout en une réduction de la charge cognitive : l'information est là, visible, sans effort de l'analyste.

La pile, visible simultanément dans la section `stack`, montre l'adresse de retour et le contexte de l'appelant. En un coup d'œil, on identifie depuis quelle fonction `strcmp` a été invoquée, ce qui aide à remonter vers la routine de vérification sans naviguer manuellement dans le graphe d'appels.

Lorsqu'on avance pas à pas dans la fonction de comparaison avec `nexti`, chaque modification de registre est mise en évidence dans le contexte suivant. On voit les octets comparés un par un, les flags modifiés, et le moment exact où la comparaison échoue (le saut conditionnel marqué `TAKEN` ou `NOT TAKEN` par pwndbg). Cette visibilité immédiate sur le flux de contrôle transforme le débogage d'un exercice de mémoire en un exercice de lecture.

---

## Bonnes pratiques

**Ajuster la taille du terminal.** Les contextes des trois extensions sont conçus pour des terminaux larges. Un terminal de 80 colonnes produira un affichage tronqué et peu lisible. Utiliser au minimum 120 colonnes et 40 lignes. Un écran dédié au terminal GDB, ou un multiplexeur comme `tmux` avec un panneau large, améliore considérablement le confort.

**Réduire le contexte quand on ne s'en sert pas.** Lors de l'exécution de longues boucles avec `continue` entre deux breakpoints distants, le contexte qui s'affiche à chaque arrêt intermédiaire (watchpoint, breakpoint conditionnel fréquent) peut ralentir la session. Désactiver temporairement le contexte accélère l'exécution dans ces cas.

**Combiner `telescope` avec `display`.** Plutôt que de retaper `telescope $rsp 8` après chaque instruction, ajouter un `display` permanent qui montre les 4 premiers slots de pile. Cela crée un mini-contexte personnalisé qui complète le contexte de l'extension.

**Ne pas négliger les flags.** Les registres et la pile attirent naturellement l'attention dans le contexte, mais les flags (`EFLAGS` / `RFLAGS`) sont tout aussi importants. GEF et pwndbg affichent les flags individuels (`ZF`, `CF`, `SF`, `OF`) de manière lisible. Prendre l'habitude de les consulter à chaque saut conditionnel évite de se perdre dans le flux de contrôle.

---


⏭️ [Recherche de gadgets ROP depuis GDB](/12-gdb-extensions/03-recherche-gadgets-rop.md)
