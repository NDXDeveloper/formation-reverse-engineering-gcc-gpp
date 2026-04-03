🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.5 — Breakpoints conditionnels et watchpoints (mémoire et registres)

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Le problème : trop de bruit, pas assez de signal

Un breakpoint classique sur `strcmp` s'arrête à **chaque** appel — y compris les dizaines d'appels internes de la libc pendant l'initialisation, les comparaisons de locales, les vérifications de variables d'environnement. Sur un programme de taille réelle, poser un breakpoint sur `malloc` peut déclencher des centaines d'arrêts avant qu'on atteigne le code qui nous intéresse. Avancer instruction par instruction dans une boucle de 10 000 itérations pour atteindre l'itération 9 999 est hors de question.

Les **breakpoints conditionnels** résolvent le premier problème : GDB évalue une condition à chaque déclenchement et ne s'arrête que si elle est vraie. Les **watchpoints** résolvent un problème différent mais complémentaire : au lieu de surveiller l'exécution d'une instruction, ils surveillent une **zone mémoire** et s'arrêtent quand elle est lue ou modifiée, quelle que soit l'instruction responsable. Ensemble, ces deux mécanismes transforment GDB en un filtre intelligent qui ne signale que les événements pertinents.

## Breakpoints conditionnels

### Syntaxe de base

On ajoute une condition à un breakpoint avec le mot-clé `if` :

```
(gdb) break strcmp if strcmp((char *)$rdi, "VALID-KEY") == 0
```

Mais cette forme est problématique : elle appelle `strcmp` *dans* la condition d'un breakpoint *sur* `strcmp`, ce qui crée une récursion. En pratique, on utilise des conditions plus simples portant sur les registres ou la mémoire :

```
(gdb) break strcmp if (char)*(char *)$rdi == 'V'
```

Ce breakpoint ne s'arrête que lorsque le premier caractère de la chaîne pointée par `rdi` (premier argument de `strcmp`) est `'V'`. On filtre ainsi les appels sans rapport avec la clé qu'on cherche.

Autre exemple classique — ne s'arrêter que quand une fonction reçoit une valeur spécifique :

```
(gdb) break *0x401140 if $rdi > 0x7fffffffe000
```

Ce breakpoint sur la fonction à `0x401140` ne se déclenche que si le premier argument (`rdi`) est une adresse dans la pile. Cela filtre les appels où l'argument est une adresse dans `.rodata` ou le heap.

### Ajouter une condition à un breakpoint existant

On peut conditionner un breakpoint après sa création avec la commande `condition` :

```
(gdb) break malloc
Breakpoint 1 at 0x7ffff7e5b0a0
(gdb) condition 1 $rdi > 1024
```

Le breakpoint 1 (sur `malloc`) ne s'arrêtera désormais que pour les allocations de plus de 1024 octets. C'est pratique pour ignorer les petites allocations de routine et ne capturer que les gros buffers — souvent les plus intéressants en RE.

Pour retirer la condition sans supprimer le breakpoint :

```
(gdb) condition 1
```

Sans expression après le numéro, GDB supprime la condition et le breakpoint redevient inconditionnel.

### Expressions de condition : ce qui est autorisé

Les conditions acceptent toute expression C valide que GDB peut évaluer. Voici les formes les plus utiles en RE :

**Comparaison de registres :**
```
(gdb) break *0x40117a if $rax == 0x42
(gdb) break *0x40117a if $rax != $rbx
(gdb) break *0x40117a if ($rax & 0xff) == 0x41    # Masque sur l'octet bas
```

**Comparaison de valeurs en mémoire :**
```
(gdb) break *0x401180 if *(int *)($rbp - 0x10) == 42
(gdb) break *0x401180 if *(char *)0x404050 == 'Y'
```

La syntaxe `*(type *)adresse` déréférence l'adresse comme un pointeur vers le type donné. C'est l'équivalent GDB d'un cast C suivi d'un déréférencement.

**Comparaison de chaînes partielles :**
```
(gdb) break strcmp if *(int *)$rdi == 0x494c4156    # "VALI" en little-endian
```

Ici, on lit les 4 premiers octets de la chaîne pointée par `rdi` comme un entier et on compare à `0x494c4156`, ce qui correspond aux caractères `V`, `A`, `L`, `I` encodés en little-endian. C'est une astuce pour filtrer les appels à `strcmp` par préfixe sans appeler de fonction dans la condition.

**Compteur d'itérations :**
```
(gdb) break *0x401160 if $rcx == 9999
```

Pour ne s'arrêter qu'à la 10 000e itération d'une boucle dont le compteur est dans `rcx`.

**Combinaisons logiques :**
```
(gdb) break *0x401180 if $rax > 0 && $rdi != 0
(gdb) break *0x401180 if $rax == 0 || $rbx == 0
```

### Ignorer les N premiers déclenchements : `ignore`

Complémentaire aux conditions, la commande `ignore` indique à GDB de laisser passer un nombre donné de déclenchements avant de s'arrêter :

```
(gdb) break strcmp
Breakpoint 1 at 0x401030
(gdb) ignore 1 50
Will ignore next 50 crossings of breakpoint 1.
```

Le breakpoint 1 laissera passer les 50 premiers appels à `strcmp` et ne s'arrêtera qu'au 51e. C'est utile quand on sait que les N premiers appels sont de la routine d'initialisation sans intérêt.

Pour connaître le compteur actuel :

```
(gdb) info breakpoints
Num  Type           Disp Enb Address            What
1    breakpoint     keep y   0x0000000000401030 <strcmp@plt>
     ignore next 42 hits
```

### Breakpoints avec commandes automatiques

La combinaison d'un breakpoint conditionnel avec un bloc `commands` permet de créer des « sondes » qui collectent des informations sans interrompre l'exécution :

```
(gdb) break strcmp
Breakpoint 1 at 0x401030
(gdb) commands 1
  silent
  if *(char *)$rdi != 0
    printf "strcmp(\"%s\", \"%s\")\n", (char *)$rdi, (char *)$rsi
  end
  continue
end
```

Ce breakpoint intercepte tous les appels à `strcmp`, affiche les deux arguments si le premier n'est pas une chaîne vide, puis reprend l'exécution automatiquement. Le programme tourne « normalement » du point de vue de l'utilisateur, mais GDB imprime un log de toutes les comparaisons de chaînes en arrière-plan.

On peut aller plus loin en combinant condition, commandes et logique :

```
(gdb) break *0x401180
Breakpoint 2 at 0x401180
(gdb) commands 2
  silent
  set $count = $count + 1
  if $rax == 1
    printf ">>> Itération %d : rax=1, match trouvé!\n", $count
  end
  continue
end
(gdb) set $count = 0
```

Ce breakpoint maintient un compteur d'itérations dans une convenience variable `$count`, et n'affiche un message que lorsque `rax` vaut 1. On obtient un log ciblé sans jamais interrompre l'exécution.

### Performance des breakpoints conditionnels

Un point important à comprendre : un breakpoint conditionnel n'est **pas** plus efficace qu'un breakpoint inconditionnel du point de vue du processeur. GDB utilise le même mécanisme matériel (remplacement de l'instruction par `int3`). À chaque déclenchement, le processus est interrompu, le contrôle passe à GDB, la condition est évaluée, et si elle est fausse, l'exécution reprend. Ce va-et-vient entre le processus et GDB a un coût.

Pour un breakpoint dans une boucle serrée exécutée des millions de fois, cela peut ralentir considérablement l'exécution. Dans ce cas, il est parfois préférable d'utiliser `ignore` (qui court-circuite l'évaluation de la condition), de poser le breakpoint plus loin dans le code (après un test préliminaire), ou d'utiliser les breakpoints matériels conditionnels quand le processeur les supporte.

## Watchpoints : surveiller la mémoire

Les watchpoints sont un mécanisme fondamentalement différent des breakpoints. Au lieu de surveiller l'exécution d'une instruction à une adresse donnée, un watchpoint surveille une **zone mémoire** et interrompt le programme quand cette zone est modifiée (ou lue, selon le type de watchpoint).

C'est l'outil idéal pour répondre à la question : « *Quelle instruction modifie cette variable ?* » — une question très fréquente en RE, où l'on repère une valeur intéressante en mémoire sans savoir quel code en est responsable.

### Watchpoints en écriture : `watch`

La commande `watch` surveille une expression et s'arrête quand sa valeur change :

```
(gdb) watch *(int *)0x404050
Hardware watchpoint 3: *(int *)0x404050
```

GDB s'arrêtera chaque fois que les 4 octets à l'adresse `0x404050` seront modifiés, et affichera l'ancienne et la nouvelle valeur :

```
Hardware watchpoint 3: *(int *)0x404050

Old value = 0  
New value = 42  
0x0000000000401168 in ?? ()
```

On voit immédiatement que l'instruction à `0x401168` a écrit la valeur 42 à cette adresse. On peut inspecter cette instruction :

```
(gdb) x/i 0x401168
   0x401168:  mov    DWORD PTR [rip+0x2ede],eax    # 0x404050
```

Avec les symboles DWARF, on peut utiliser directement le nom de la variable :

```
(gdb) watch result
Hardware watchpoint 4: result
```

GDB surveillera l'emplacement mémoire de la variable `result` et s'arrêtera à chaque modification.

#### Surveiller des zones de taille variable

L'expression passée à `watch` détermine la taille de la zone surveillée :

```
(gdb) watch *(char *)0x404050         # Surveille 1 octet
(gdb) watch *(short *)0x404050        # Surveille 2 octets
(gdb) watch *(int *)0x404050          # Surveille 4 octets
(gdb) watch *(long *)0x404050         # Surveille 8 octets
```

Pour surveiller un buffer plus large, on peut utiliser un cast vers un tableau :

```
(gdb) watch *(char[32] *)0x7fffffffe100
```

Cela crée un watchpoint sur 32 octets consécutifs. Cependant, la taille des watchpoints matériels est limitée par le processeur (voir la section sur les watchpoints matériels ci-dessous), et un watchpoint logiciel sur une grande zone sera très lent.

### Watchpoints en lecture : `rwatch`

`rwatch` (*read watch*) s'arrête quand la zone mémoire est **lue** :

```
(gdb) rwatch *(char *)0x402010
Hardware read watchpoint 5: *(char *)0x402010
```

Cela répond à la question : « *Quelle instruction lit cette donnée ?* » C'est utile pour tracer l'usage d'une constante, d'une clé de chiffrement stockée en mémoire, ou d'une configuration.

```
Hardware read watchpoint 5: *(char *)0x402010

Value = 69 'E'
0x000000000040116c in ?? ()
```

L'instruction à `0x40116c` a lu l'octet à `0x402010`.

### Watchpoints en lecture et écriture : `awatch`

`awatch` (*access watch*) combine les deux : il s'arrête sur tout accès — lecture ou écriture — à la zone mémoire :

```
(gdb) awatch *(int *)0x404050
Hardware access (read/write) watchpoint 6: *(int *)0x404050
```

C'est le filet le plus large : on capture toute interaction avec la zone mémoire, quel que soit le type d'accès.

### Watchpoints matériels vs logiciels

GDB utilise deux implémentations de watchpoints, et la distinction a un impact majeur sur les performances.

**Watchpoints matériels.** Le processeur x86-64 possède 4 registres de débogage (`DR0`–`DR3`) qui permettent de surveiller jusqu'à 4 adresses simultanément, avec des tailles de 1, 2, 4 ou 8 octets. Quand GDB utilise ces registres, la surveillance est effectuée par le matériel sans aucun ralentissement du programme. GDB affiche `Hardware watchpoint` pour indiquer ce mode.

Les limitations matérielles sont strictes :

| Contrainte | Valeur sur x86-64 |  
|---|---|  
| Nombre maximum simultané | 4 watchpoints |  
| Tailles supportées | 1, 2, 4 ou 8 octets |  
| Alignement requis | L'adresse doit être alignée sur la taille |  
| Types supportés | Écriture, lecture, lecture+écriture (selon le CPU) |

Si on tente de dépasser ces limites :

```
(gdb) watch *(int *)0x404050
Hardware watchpoint 1: *(int *)0x404050
(gdb) watch *(int *)0x404054
Hardware watchpoint 2: *(int *)0x404054
(gdb) watch *(int *)0x404058
Hardware watchpoint 3: *(int *)0x404058
(gdb) watch *(int *)0x40405c
Hardware watchpoint 4: *(int *)0x40405c
(gdb) watch *(int *)0x404060
# GDB bascule en watchpoint logiciel (ou refuse)
```

**Watchpoints logiciels.** Quand les registres matériels sont épuisés ou que la zone à surveiller dépasse la taille supportée, GDB passe en mode logiciel : il exécute le programme **instruction par instruction** et vérifie la mémoire après chaque instruction. C'est extrêmement lent — le programme peut tourner 100 à 1000 fois plus lentement.

GDB indique le type de watchpoint à la création :

```
(gdb) watch *(char[128] *)0x7fffffffe100
Watchpoint 5: *(char[128] *)0x7fffffffe100    # Pas de "Hardware" → logiciel
```

> 💡 **Conseil pratique :** gardez vos watchpoints aussi petits et peu nombreux que possible. Si vous avez besoin de surveiller une grande zone, posez d'abord un watchpoint de 8 octets sur la partie la plus susceptible d'être modifiée, identifiez le code responsable, puis affinez.

### Watchpoints conditionnels

Les watchpoints acceptent les mêmes conditions que les breakpoints :

```
(gdb) watch *(int *)0x404050 if *(int *)0x404050 > 100
```

Ce watchpoint ne s'arrête que lorsque la valeur à `0x404050` change **et** que la nouvelle valeur est supérieure à 100. Sans la condition, il s'arrêterait à chaque modification, y compris les initialisations à 0 ou les incréments intermédiaires.

Autre exemple utile — surveiller un pointeur et s'arrêter quand il devient `NULL` :

```
(gdb) watch *(void **)0x404060 if *(void **)0x404060 == 0
```

### Watchpoints sur des registres

Contrairement à ce que le titre de cette section pourrait suggérer, GDB **ne supporte pas directement les watchpoints sur les registres**. La commande `watch $rax` ne fonctionne pas comme on pourrait l'espérer : les registres ne sont pas des emplacements mémoire et les registres de débogage du processeur ne peuvent pas surveiller d'autres registres.

Pour obtenir un effet similaire — s'arrêter quand un registre atteint une valeur donnée — on utilise un **breakpoint conditionnel** :

```
(gdb) break *0x401160 if $rax == 42
```

Ou, pour surveiller un registre à chaque instruction (très lent mais parfois nécessaire) :

```
(gdb) display $rax
(gdb) stepi
# Répéter avec Entrée et observer visuellement
```

En pratique, la question « *quand `rax` devient-il 42 ?* » se résout mieux en posant des breakpoints conditionnels aux endroits susceptibles de modifier `rax`, identifiés via l'analyse statique.

## Cas d'usage en Reverse Engineering

### Trouver qui modifie une variable globale

Scénario : en analysant un binaire dans Ghidra, on repère une variable globale à `0x404050` qui semble contrôler l'accès à une fonctionnalité (0 = verrouillé, 1 = déverrouillé). On veut savoir quel code la modifie.

```
(gdb) watch *(int *)0x404050
Hardware watchpoint 1: *(int *)0x404050
(gdb) run

Hardware watchpoint 1: *(int *)0x404050  
Old value = 0  
New value = 1  
0x00000000004011b2 in ?? ()

(gdb) x/3i 0x4011ac
   0x4011ac:  call   0x401140         # Appel à check_key
   0x4011b1:  test   eax,eax
   0x4011b3:  ...
```

On a trouvé que l'instruction à `0x4011b2` modifie la variable. En remontant de quelques instructions, on comprend le contexte : c'est le résultat de `check_key` qui met à jour cette variable.

### Détecter un buffer overflow

Scénario : on suspecte qu'un `fgets` écrit au-delà du buffer alloué. On pose un watchpoint juste après la fin du buffer.

```
# Le buffer 'input' est à rbp-0x40, taille 64 octets
# Le canary ou la variable suivante est à rbp-0x08
(gdb) break *0x401190          # Juste avant fgets
(gdb) run
Breakpoint 1, 0x0000000000401190 in ?? ()

(gdb) watch *(long *)($rbp - 0x08)
Hardware watchpoint 2: *(long *)($rbp - 0x08)
(gdb) continue

# Si fgets déborde...
Hardware watchpoint 2: *(long *)($rbp - 0x08)  
Old value = 0x00000000deadbeef       # Valeur canary originale  
New value = 0x4141414141414141       # Écrasée par "AAAA..."  
0x00007ffff7e62123 in __GI__IO_fgets () from libc.so.6
```

Le watchpoint capture le moment exact où le canary est écrasé, et l'adresse fautive pointe dans `fgets` — confirmation du débordement.

### Tracer le déchiffrement d'un buffer

Scénario : un binaire chiffré déchiffre du code ou des données en mémoire avant de les utiliser. On veut capturer le moment où le buffer chiffré est transformé en clair.

```
# Le buffer chiffré est chargé à 0x555555559300 (identifié via analyse statique)
(gdb) watch *(long *)0x555555559300
Hardware watchpoint 1: *(long *)0x555555559300
(gdb) run

Hardware watchpoint 1: *(long *)0x555555559300  
Old value = 0x8a3c7f12e5d0b641       # Données chiffrées  
New value = 0x0068732f6e69622f       # /bin/sh\0 en little-endian !  
0x00000000004010e8 in ?? ()
```

Le watchpoint capture l'instruction de déchiffrement. On peut maintenant inspecter le buffer complet :

```
(gdb) x/s 0x555555559300
0x555555559300: "/bin/sh"
```

Et examiner le code de déchiffrement autour de `0x4010e8` pour comprendre l'algorithme.

### Suivre les modifications d'une vtable C++

Scénario : en analysant un binaire C++ (chapitre 17), on a identifié une vtable à une adresse donnée. Un exploit pourrait remplacer un pointeur de la vtable par l'adresse d'une fonction malveillante.

```
# La vtable du premier objet est pointée par le champ à offset 0 de l'objet
# L'objet est à 0x555555559260
(gdb) watch *(void **)0x555555559260
Hardware watchpoint 1: *(void **)0x555555559260
(gdb) commands 1
  silent
  printf "vptr modifié! Nouvelle vtable: %p\n", *(void **)0x555555559260
  x/4ag *(void **)0x555555559260
  continue
end
(gdb) run
```

On obtient un log de toutes les modifications du pointeur de vtable, avec le contenu de la vtable pointée à chaque changement.

## Lister et gérer les watchpoints

Les watchpoints apparaissent dans la même liste que les breakpoints :

```
(gdb) info breakpoints
Num  Type            Disp Enb Address            What
1    breakpoint      keep y   0x0000000000401190 
2    hw watchpoint   keep y                      *(int *)0x404050
3    hw watchpoint   keep y                      *(long *)($rbp - 0x08)
     stop only if *(long *)($rbp - 0x08) == 0
4    read watchpoint keep y                      *(char *)0x402010
```

Le type (`hw watchpoint`, `read watchpoint`) indique le mode. Les commandes de gestion sont identiques à celles des breakpoints :

```
(gdb) delete 3          # Supprimer le watchpoint 3
(gdb) disable 2         # Désactiver le watchpoint 2
(gdb) enable 2          # Réactiver
```

> ⚠️ **Durée de vie des watchpoints.** Un watchpoint sur une variable locale (par exemple `watch *(int *)($rbp - 0x10)`) utilise une adresse relative au frame courant. Quand la fonction retourne et que le frame est détruit, le watchpoint est automatiquement supprimé par GDB avec le message :  
> ```  
> Watchpoint 3 deleted because the program has left the block in  
> which its expression is valid.  
> ```  
> Pour surveiller une zone mémoire au-delà de la durée de vie d'une fonction, utilisez une adresse absolue plutôt qu'une expression relative à `$rbp`.

## Résumé des commandes

| Commande | Action |  
|---|---|  
| `break <loc> if <expr>` | Breakpoint conditionnel |  
| `condition <n> <expr>` | Ajouter/modifier la condition du breakpoint n |  
| `condition <n>` | Retirer la condition du breakpoint n |  
| `ignore <n> <count>` | Ignorer les `count` prochains déclenchements |  
| `watch <expr>` | Watchpoint en écriture (s'arrête quand la valeur change) |  
| `rwatch <expr>` | Watchpoint en lecture (s'arrête quand la valeur est lue) |  
| `awatch <expr>` | Watchpoint en accès (lecture ou écriture) |  
| `commands <n>` | Attacher des commandes automatiques au breakpoint/watchpoint n |  
| `info watchpoints` | Lister les watchpoints actifs (alias de `info breakpoints`) |

---

> **À retenir :** Les breakpoints conditionnels et les watchpoints transforment GDB d'un outil « arrêt-partout » en un outil de surveillance ciblée. Les breakpoints conditionnels filtrent le bruit en ne s'arrêtant que lorsqu'une condition précise est remplie. Les watchpoints inversent la logique de recherche : au lieu de chercher quel code agit sur une donnée, on surveille la donnée et on laisse GDB identifier le code. En RE, cette combinaison est particulièrement efficace pour tracer les modifications de variables globales, capturer les moments de déchiffrement, et isoler les itérations significatives dans des boucles massives.

⏭️ [Catchpoints : intercepter les `fork`, `exec`, `syscall`, signaux](/11-gdb/06-catchpoints.md)
