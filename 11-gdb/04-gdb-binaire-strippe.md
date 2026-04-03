🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.4 — GDB sur un binaire strippé — travailler sans symboles

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## La réalité du terrain

Les sections précédentes ont présenté GDB dans un contexte confortable : un binaire compilé avec `-g`, des noms de fonctions résolus, des variables locales accessibles par leur nom, des numéros de lignes affichés à chaque pas. Ce confort n'existe presque jamais en reverse engineering réel.

La majorité des binaires rencontrés en situation concrète — logiciels commerciaux, malwares, firmwares, binaires de CTF — sont **strippés**. La commande `strip` (ou le flag `-s` de GCC) a supprimé la table de symboles (`.symtab`) et les sections DWARF (`.debug_*`). Parfois même la section `.dynsym` est réduite au strict minimum nécessaire pour la résolution dynamique.

Quand on ouvre un tel binaire dans GDB, la différence est immédiate :

```
$ gdb -q ./keygenme_O2_strip
Reading symbols from ./keygenme_O2_strip...
(No debugging symbols found in ./keygenme_O2_strip)
(gdb) break main
Function "main" not found.  
Make breakpoint pending on future shared library load? (y or [n]) n  
```

GDB ne connaît pas `main`. Il ne connaît aucune fonction du binaire. Les commandes `info locals`, `info args`, `list`, `break <nom>` sont inutilisables. On pourrait croire que GDB devient inutile — c'est tout le contraire. Il reste un outil d'observation extrêmement puissant, mais il faut changer de méthode.

## Ce qui reste disponible dans un binaire strippé

Un binaire strippé n'est pas une boîte noire totale. Plusieurs sources d'information subsistent :

**Le header ELF et le point d'entrée.** Le champ `e_entry` du header ELF indique l'adresse de démarrage du programme (le `_start` généré par la libc). Ce point d'entrée est toujours présent — sans lui, le loader ne pourrait pas lancer le programme.

```
(gdb) info files
Entry point: 0x401060
  0x00401000 - 0x004011e0 is .text
  0x00402000 - 0x00402030 is .rodata
  ...
```

**La section `.dynsym` et la PLT.** Les symboles dynamiques — ceux nécessaires pour résoudre les fonctions de bibliothèques partagées — survivent au stripping. On peut toujours poser des breakpoints sur `strcmp`, `printf`, `malloc`, `open`, etc. :

```
(gdb) break strcmp
Breakpoint 1 at 0x401030
(gdb) break printf
Breakpoint 2 at 0x401040
```

C'est une information précieuse : même si on ne sait pas comment les fonctions internes du binaire s'appellent, on sait quelles fonctions externes elles utilisent.

**La section `.rodata`.** Les chaînes de caractères constantes ne sont pas affectées par le stripping. Elles restent intactes et lisibles :

```
(gdb) x/s 0x402000
0x402000: "Enter your key: "
(gdb) x/s 0x402011
0x402011: "Correct!\n"
(gdb) x/s 0x40201b
0x40201b: "Wrong key!\n"
```

**Les sections `.plt` et `.got`.** Le mécanisme de liaison dynamique (chapitre 2, section 2.9) reste intact et fonctionnel. Les appels `call printf@plt` apparaissent clairement dans le désassemblage.

**Le code machine lui-même.** Aucun octet de la section `.text` n'est modifié par le stripping. Le code est identique, instruction pour instruction, à celui du binaire non strippé. Seules les métadonnées qui le décrivaient ont disparu.

## Trouver `main()` sans symboles

La première tâche sur un binaire strippé est de localiser la fonction `main`. Elle n'apparaît plus par son nom, mais sa position est déterministe.

### Méthode 1 : remonter depuis le point d'entrée

Le point d'entrée (`_start`) est un trampoline minimaliste généré par la libc. Sur un binaire GCC/glibc classique, il appelle `__libc_start_main` en lui passant l'adresse de `main` comme premier argument (dans `rdi`). Le pattern assembleur est stable et reconnaissable :

```
(gdb) x/20i 0x401060
   0x401060:  endbr64
   0x401064:  xor    ebp,ebp
   0x401066:  mov    r9,rdx
   0x401069:  pop    rsi
   0x40106a:  mov    rdx,rsp
   0x40106d:  and    rsp,0xfffffffffffffff0
   0x401071:  push   rax
   0x401072:  push   rsp
   0x401073:  xor    r8d,r8d
   0x401076:  xor    ecx,ecx
   0x401078:  lea    rdi,[rip+0x111]        # 0x401190
   0x40107f:  call   0x401050 <__libc_start_main@plt>
```

L'instruction `lea rdi, [rip+0x111]` charge l'adresse `0x401190` dans `rdi` — c'est le premier argument de `__libc_start_main`, et selon la convention de la glibc, ce premier argument est **l'adresse de `main`**. On vient de localiser `main` à `0x401190`.

> 💡 **Variante avec GCC récent et glibc ≥ 2.34 :** le point d'entrée peut appeler `__libc_start_main_impl` ou passer par une table d'adresses. Le principe reste le même — `main` est un argument passé dans `rdi` — mais le code de démarrage peut être légèrement différent. En cas de doute, la méthode 2 est plus fiable.

### Méthode 2 : breakpoint sur `__libc_start_main`

Plutôt que de lire le code de `_start`, on peut laisser GDB faire le travail :

```
(gdb) break __libc_start_main
Breakpoint 1 at 0x7ffff7de0b40
(gdb) run
Breakpoint 1, __libc_start_main () at ../csu/libc-start.c:...
(gdb) print/x $rdi
$1 = 0x401190
```

L'adresse dans `rdi` au moment où `__libc_start_main` est atteint est l'adresse de `main`. On peut maintenant poser un breakpoint dessus :

```
(gdb) break *0x401190
Breakpoint 2 at 0x401190
(gdb) continue
Breakpoint 2, 0x0000000000401190 in ?? ()
```

On est à l'entrée de `main`. Le `?? ()` confirme que GDB ne connaît pas le nom de la fonction, mais on y est.

### Méthode 3 : utiliser l'analyse statique préalable

La méthode la plus courante en pratique est de repérer les adresses d'intérêt **dans Ghidra ou objdump avant de lancer GDB**. L'analyse statique (Partie II) nous a déjà permis d'identifier les fonctions, leurs rôles et leurs adresses. On importe ces connaissances dans GDB sous forme de breakpoints par adresse :

```
# Adresses identifiées dans Ghidra
(gdb) break *0x401190         # main
(gdb) break *0x401140         # fonction de vérification (check_key)
(gdb) break *0x40117a         # instruction cmp dans check_key
```

C'est le workflow standard : **Ghidra pour comprendre, GDB pour observer**. Les deux outils sont complémentaires, pas interchangeables.

## Naviguer sans symboles : techniques fondamentales

### Poser des breakpoints par adresse

Tous les breakpoints passent par l'adresse préfixée de `*` :

```
(gdb) break *0x401156
(gdb) break *0x401156 if $rdi != 0     # Breakpoint conditionnel sur adresse
(gdb) tbreak *0x4011a0                  # Breakpoint temporaire sur adresse
```

Pour poser un breakpoint relatif au pointeur d'instruction actuel :

```
(gdb) break *($rip + 0x20)
```

### Désassembler autour du point courant

Sans symboles, `disassemble` ne connaît pas les limites des fonctions. La commande sans argument tente de deviner les bornes, mais échoue souvent. On utilise à la place des plages d'adresses explicites :

```
(gdb) x/30i $rip
```

Affiche les 30 prochaines instructions. Pour voir ce qui précède le point courant :

```
(gdb) x/10i $rip-30
```

Ou une plage précise :

```
(gdb) disassemble 0x401140, 0x4011a0
```

> ⚠️ **Attention au désassemblage rétrospectif.** x86-64 utilise des instructions de longueur variable (1 à 15 octets). Si on commence à désassembler au milieu d'une instruction, tout le listing sera décalé et absurde. `x/10i $rip-30` peut tomber au milieu d'une instruction et produire un résultat trompeur. Pour éviter cela, utilisez les adresses de début de fonction identifiées en analyse statique, ou partez d'un point connu (le `$rip` courant, un breakpoint confirmé) et avancez en avant uniquement.

### Identifier les fonctions en observant les patterns

Même sans symboles, les frontières de fonctions sont repérables grâce aux prologues et épilogues standard de GCC :

**Prologue classique (non optimisé, `-O0`) :**
```
push   rbp  
mov    rbp, rsp  
sub    rsp, 0x30          # Alloue l'espace pour les variables locales  
```

**Épilogue classique :**
```
leave                      # Équivalent à mov rsp, rbp ; pop rbp  
ret  
```

**Prologue optimisé (`-O2` et plus) :**
```
sub    rsp, 0x28           # Pas de frame pointer, allocation directe
```

Ou pour les fonctions « leaf » (qui n'appellent rien) :
```
# Pas de prologue du tout — la fonction utilise directement les registres
```

En parcourant la section `.text` avec `x/100i 0x401000`, on peut repérer les séquences `ret` suivies d'alignement (des `nop` ou `nop DWORD PTR [rax]` en padding) et `push rbp` — chacune marque probablement le début d'une nouvelle fonction.

### Reconstruire la pile d'appels manuellement

Sur un binaire strippé, `backtrace` peut être incomplet ou incorrect — surtout avec les optimisations qui suppriment le frame pointer (`-fomit-frame-pointer`, activé par défaut à `-O2`). Quand `bt` affiche des résultats douteux, on reconstruit la pile manuellement.

**Avec frame pointer (`rbp` utilisé) :**

La chaîne des frames est une liste chaînée accessible via `rbp` :

```
(gdb) x/a $rbp           # rbp sauvegardé du frame appelant
0x7fffffffe0f0: 0x7fffffffe130
(gdb) x/a $rbp+8         # Adresse de retour → identifie l'appelant
0x7fffffffe0f8: 0x4011a5

# On remonte d'un cran
(gdb) x/a 0x7fffffffe130        # rbp sauvegardé du frame suivant
0x7fffffffe130: 0x0
(gdb) x/a 0x7fffffffe138        # Adresse de retour suivante
0x7fffffffe138: 0x7ffff7de0c8a
```

On peut ainsi remonter la pile frame par frame. L'adresse `0x4011a5` se corrèle avec le désassemblage : c'est l'instruction juste après le `call` qui nous a amenés dans la fonction courante.

**Sans frame pointer (`rbp` non utilisé) :**

C'est le cas le plus fréquent en `-O2`. La pile ne contient pas de chaîne de `rbp` sauvegardés, et il faut scanner les valeurs sur la pile pour identifier celles qui ressemblent à des adresses de retour :

```
(gdb) x/30ag $rsp
0x7fffffffe0c0: 0x0                          0x7fffffffe100
0x7fffffffe0d0: 0x7fffffffe208               0x100000000
0x7fffffffe0e0: 0x0                          0x0
0x7fffffffe0f0: 0x0                          0x4011a5
0x7fffffffe100: 0x59454b2d54534554           0xa
...
```

On cherche les valeurs qui tombent dans la plage d'adresses de `.text` (`0x401000`–`0x4011e0` dans notre exemple). La valeur `0x4011a5` à l'adresse `0x7fffffffe0f8` est un candidat : elle pointe dans `.text` et c'est probablement une adresse de retour. On vérifie :

```
(gdb) x/3i 0x4011a0
   0x4011a0:  call   0x401140          # L'appel qui nous a amenés ici
   0x4011a5:  test   eax,eax           # ← adresse de retour (instruction après le call)
   0x4011a7:  je     0x4011b5
```

L'instruction à `0x4011a5` suit un `call` — c'est bien une adresse de retour. On a identifié l'appelant.

## Étiqueter les découvertes : créer des symboles à la volée

Au fur et à mesure de l'analyse, on identifie des fonctions, des variables globales, des constantes. GDB permet de leur donner des noms temporaires pour rendre la session plus lisible.

### Définir des « convenience variables »

Les variables préfixées par `$` dans GDB sont des variables internes (dites *convenience variables*) qu'on peut définir librement :

```
(gdb) set $main = 0x401190
(gdb) set $check_key = 0x401140
(gdb) set $cmp_instruction = 0x40117a

(gdb) break *$main
Breakpoint 1 at 0x401190
(gdb) break *$check_key
Breakpoint 2 at 0x401140
```

Ces variables n'existent que dans la session GDB courante. Elles ne modifient pas le binaire et ne sont pas sauvegardées automatiquement.

### Sauvegarder les annotations dans un fichier de commandes

Pour ne pas refaire ce travail à chaque session, on crée un fichier de commandes GDB :

```bash
# keygenme_stripped.gdb — annotations manuelles
set $main         = 0x401190  
set $check_key    = 0x401140  
set $validate     = 0x401100  
set $transform    = 0x4010c0  

# Breakpoints de travail
break *$main  
break *$check_key  

# Affichages automatiques
display/x $rax  
display/x $rdi  
display/6i $rip  

# Configuration
set disassembly-flavor intel  
set pagination off  
```

On charge ce fichier au lancement :

```bash
$ gdb -q -x keygenme_stripped.gdb ./keygenme_O2_strip
```

Ou depuis une session en cours :

```
(gdb) source keygenme_stripped.gdb
```

Ce fichier de commandes devient le **carnet de notes** de l'analyse. Au fil de la session, on y ajoute les adresses identifiées, les breakpoints utiles, les commandes automatiques. C'est l'équivalent dynamique des renommages qu'on fait dans Ghidra — et comme dans Ghidra, c'est l'accumulation de ces petites annotations qui rend l'analyse progressivement lisible.

### Charger des symboles depuis un autre fichier

Si vous disposez d'une version non strippée du même binaire (par exemple une version de développement, ou un paquet `-dbgsym`), vous pouvez charger ses symboles :

```
(gdb) symbol-file keygenme_O2_debug
```

GDB appliquera les symboles de `keygenme_O2_debug` au binaire strippé en cours de débogage, à condition que les adresses correspondent (même version, mêmes options de compilation, pas de rebase ASLR). C'est la situation idéale — mais rarement disponible en RE « hostile ».

On peut aussi charger un fichier de symboles séparé produit par `objcopy` (vu en section 11.1) :

```
(gdb) add-symbol-file /chemin/vers/programme.debug 0x401000
```

Le second argument est l'adresse de chargement de la section `.text`, nécessaire si le binaire est PIE et que l'adresse diffère entre les deux fichiers.

## Travailler avec un binaire PIE strippé

Les binaires PIE (*Position-Independent Executable*) compilés avec `-pie` ajoutent une difficulté supplémentaire : les adresses changent à chaque exécution à cause de l'ASLR. Les adresses vues dans Ghidra (qui analyse le binaire non chargé) ne correspondent pas à celles en mémoire pendant l'exécution.

### Le problème

Dans Ghidra, la fonction d'intérêt est à l'adresse `0x1190` (adresse relative à la base). Mais à l'exécution, le binaire est chargé à une adresse aléatoire :

```
(gdb) info proc mappings
  0x0000555555554000 0x0000555555555000 0x1000  0x0  r--p  keygenme_pie_strip
  0x0000555555555000 0x0000555555556000 0x1000  0x1000 r-xp keygenme_pie_strip
```

La base de chargement est `0x555555554000`. La fonction qui était à `0x1190` dans Ghidra se trouve maintenant à `0x555555554000 + 0x1190 = 0x555555555190`.

### Méthode : calculer le rebase

Le workflow est le suivant :

**1. Lancer le programme et l'arrêter tôt.** On ne peut pas connaître la base de chargement avant que le programme soit en mémoire. On utilise `starti` pour s'arrêter sur la toute première instruction (avant même `_start`) :

```
(gdb) starti
Starting program: ./keygenme_pie_strip  
Program stopped.  
0x00007ffff7fe4c40 in _start () from /lib64/ld-linux-x86-64.so.2
```

Attention : ce `_start` est celui du **loader**, pas celui du programme. Le programme n'est pas encore initialisé, mais il est déjà mappé en mémoire.

**2. Lire la base de chargement :**

```
(gdb) info proc mappings
  0x0000555555554000 ...  r--p  keygenme_pie_strip    ← base
```

Ou plus directement :

```
(gdb) info files
  Entry point: 0x555555555060
```

Si le point d'entrée dans le binaire est `0x1060`, et que `info files` montre `0x555555555060`, alors la base est `0x555555555060 - 0x1060 = 0x555555554000`.

**3. Calculer les adresses effectives :**

```
(gdb) set $base = 0x555555554000

# Adresses de Ghidra + base
(gdb) break *($base + 0x1190)       # main (0x1190 dans Ghidra)
(gdb) break *($base + 0x1140)       # check_key (0x1140 dans Ghidra)
```

**4. Automatiser dans le fichier de commandes :**

```bash
# keygenme_pie.gdb
starti

# Lire la base depuis le point d'entrée
# (suppose que l'entry point offset dans le binaire est 0x1060)
python  
import gdb  
entry = int(gdb.execute("info files", to_string=True).split("Entry point: ")[1].split("\n")[0], 16)  
base = entry - 0x1060  
gdb.execute(f"set $base = {base}")  
print(f"Base address: {hex(base)}")  
end  

break *($base + 0x1190)  
break *($base + 0x1140)  
continue  
```

Ce script Python intégré à GDB extrait automatiquement le point d'entrée, calcule la base, et pose les breakpoints avec les offsets corrects.

### Désactiver l'ASLR pour simplifier

Pendant le développement de l'analyse, on peut désactiver l'ASLR pour que les adresses restent stables entre les exécutions :

```
(gdb) set disable-randomization on
```

C'est le comportement **par défaut** de GDB — il désactive l'ASLR pour le processus qu'il lance. Si vous constatez que les adresses ne changent pas entre les `run` successifs, c'est pour cette raison. Pour activer l'ASLR (reproduire le comportement réel) :

```
(gdb) set disable-randomization off
```

On peut aussi désactiver l'ASLR au niveau système (pour tous les processus) :

```bash
$ echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
# 0 = désactivé, 1 = partiel, 2 = complet (défaut)
```

> ⚠️ **Ne faites cela que dans une VM de travail.** Désactiver l'ASLR sur un système de production est une mauvaise pratique de sécurité.

## Stratégies avancées sur binaires strippés

### Breakpoints sur les fonctions de la PLT

Même strippé, un binaire lié dynamiquement expose ses appels de bibliothèque via la PLT. C'est une surface d'observation riche. On peut lister les entrées de la PLT :

```
(gdb) info functions @plt
0x0000000000401030  strcmp@plt
0x0000000000401040  printf@plt
0x0000000000401050  fgets@plt
```

La commande `info functions` fonctionne ici car les symboles dynamiques (`.dynsym`) ne sont pas supprimés par `strip`. On obtient le « profil fonctionnel » du binaire : il utilise `strcmp`, `printf`, `fgets` — c'est déjà une indication forte sur son comportement.

### Breakpoints sur les chaînes de `.rodata`

Si on a identifié une chaîne intéressante dans `.rodata` (par exemple `"Correct!"` à l'adresse `0x402011`), on peut trouver les instructions qui la référencent en cherchant les cross-references dans le désassemblage. Sans Ghidra, on peut utiliser une approche indirecte dans GDB : poser un **watchpoint matériel en lecture** sur l'adresse de la chaîne n'est pas directement possible (les watchpoints x86 surveillent les écritures par défaut), mais on peut chercher dans le désassemblage avec `find` :

```
(gdb) find /b 0x401000, 0x4011e0, 0x11, 0x20, 0x40
```

Cela cherche la séquence d'octets `11 20 40` (qui serait l'encodage partiel d'un offset vers `0x402011` dans une instruction `lea`) dans la section `.text`. C'est rudimentaire — mieux vaut utiliser Ghidra pour les cross-references — mais c'est faisable entièrement depuis GDB.

### Identifier les fonctions par leur comportement

Quand l'analyse statique n'est pas disponible ou pas suffisante, on peut identifier les fonctions par **observation dynamique** :

```
(gdb) break *0x401140
(gdb) run
Breakpoint 1, 0x0000000000401140 in ?? ()

(gdb) x/s $rdi
0x7fffffffe100: "TEST-KEY\n"

(gdb) finish
Run till exit from #0  0x0000000000401140 in ?? ()
0x00000000004011a5 in ?? ()

(gdb) print $rax
$1 = 0
```

La fonction à `0x401140` reçoit la chaîne entrée par l'utilisateur dans `rdi` et retourne `0`. Si on entre la bonne clé, elle retournera probablement `1`. C'est la fonction de vérification. On peut la nommer :

```
(gdb) set $check_key = 0x401140
```

Cette approche « boîte noire » — observer les entrées et sorties d'une fonction pour déduire son rôle — est parfois plus rapide que la lecture laborieuse de son désassemblage.

### Combiner GDB et `strace` / `ltrace`

Sur un binaire strippé, lancer `ltrace` en parallèle donne rapidement le profil des appels de bibliothèque :

```bash
$ ltrace ./keygenme_O2_strip
printf("Enter your key: ")                    = 16  
fgets("TEST-KEY\n", 64, 0x7f...)              = 0x7ffe...  
strcmp("TEST-KEY\n", "VALID-KEY-2025\n")       = -1  
printf("Wrong key!\n")                        = 11  
```

On voit immédiatement que `strcmp` compare l'entrée utilisateur avec `"VALID-KEY-2025\n"`. C'est parfois suffisant pour résoudre un crackme simple sans même ouvrir GDB. Pour des cas plus complexes, `ltrace` donne les premiers indices qu'on approfondira ensuite dans GDB.

## Synthèse : adapter sa méthode au niveau de stripping

| Situation | Ce qui est disponible | Stratégie |  
|---|---|---|  
| **Symboles DWARF** (`-g`) | Noms, types, lignes, variables locales | Débogage classique : `break <nom>`, `print <var>`, `next` |  
| **Table de symboles seule** (pas de `-g`) | Noms de fonctions, pas de types ni de variables | `break <nom>` fonctionne, mais `info locals` est vide. On complète avec `x` et `print $reg` |  
| **Strippé, non PIE** | Adresses fixes, PLT, `.rodata` | Breakpoints par adresse, cross-référence avec Ghidra, fichier `.gdb` |  
| **Strippé + PIE** | Adresses variables, PLT, `.rodata` | `starti` + calcul de base, `$base + offset`, ou désactiver ASLR |  
| **Strippé + PIE + lié statiquement** | Aucun symbole dynamique, pas de PLT | Le cas le plus difficile. Analyse statique lourde obligatoire (Ghidra), signatures FLIRT |

Quel que soit le niveau de stripping, le code machine reste identique. Il est toujours là, il s'exécute de la même façon, et GDB peut toujours l'observer instruction par instruction. Les symboles sont un confort, pas une nécessité. L'essentiel est d'avoir une **stratégie d'analyse préalable** — repérer les fonctions et adresses d'intérêt en statique — et un **fichier de commandes GDB** qui capitalise les découvertes au fil de la session.

---

> **À retenir :** Travailler sur un binaire strippé dans GDB, c'est accepter de raisonner en adresses plutôt qu'en noms, et compenser l'absence de symboles par une préparation rigoureuse en analyse statique. Les trois piliers sont : localiser `main` via `__libc_start_main`, poser des breakpoints par adresse (`*0x...`), et maintenir un fichier de commandes `.gdb` qui sert de mémoire persistante. Le binaire strippé ne cache rien — il force simplement à travailler plus méthodiquement.

⏭️ [Breakpoints conditionnels et watchpoints (mémoire et registres)](/11-gdb/05-breakpoints-conditionnels-watchpoints.md)
