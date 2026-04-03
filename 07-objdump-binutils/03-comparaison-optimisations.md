🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 7.3 — Comparaison avec/sans optimisations GCC (`-O0` vs `-O2` vs `-O3`)

> 🔧 **Outils utilisés** : `objdump`, `gcc`, `wc`, `diff`  
> 📦 **Binaires** : `keygenme_O0`, `keygenme_O2`, `keygenme_O3` (répertoire `binaries/ch07-keygenme/`)  
> 📝 **Syntaxe** : Intel (via `-M intel`), conformément au choix établi en 7.2.

---

## Pourquoi cette comparaison est fondamentale pour le RE

Quand vous faites du reverse engineering sur un binaire « dans la nature », vous ne choisissez pas le niveau d'optimisation — c'est le développeur qui l'a choisi au moment de la compilation. Or, la grande majorité des binaires de production sont compilés avec `-O2` ou `-O3`. Les builds de débogage en `-O0` sont rarement distribués.

Cela signifie que le code assembleur que vous allez rencontrer en conditions réelles ne ressemble **pas** à une traduction naïve du C. Le compilateur a réorganisé les instructions, éliminé des variables, fusionné des opérations, supprimé du code mort, et parfois transformé des boucles au point de les rendre méconnaissables. Si vous n'avez jamais vu ces transformations, vous risquez de perdre des heures à comprendre une séquence d'instructions qui, en réalité, correspond à trois lignes de C triviales.

L'objectif de cette section est de vous montrer ces transformations sur un exemple concret, pour que vous sachiez les reconnaître quand vous les croiserez. Nous ne ferons pas un catalogue exhaustif de toutes les optimisations GCC (le chapitre 16 y est consacré) — nous nous concentrerons ici sur ce qu'un débutant en RE doit impérativement savoir reconnaître en lisant un listing `objdump`.

---

## Préparation : compiler les trois variantes

Si vous n'avez pas déjà compilé les variantes via le `Makefile`, voici les commandes manuelles :

```bash
gcc -O0 -o keygenme_O0 keygenme.c  
gcc -O2 -o keygenme_O2 keygenme.c  
gcc -O3 -o keygenme_O3 keygenme.c  
```

Commençons par une mesure rapide :

```bash
$ ls -l keygenme_O0 keygenme_O2 keygenme_O3
-rwxr-xr-x 1 user user  16696  keygenme_O0
-rwxr-xr-x 1 user user  16432  keygenme_O2
-rwxr-xr-x 1 user user  16464  keygenme_O3
```

La version `-O0` est légèrement plus grosse. La taille de l'exécutable complet n'est pas un indicateur très fiable (elle inclut les headers ELF, les tables de symboles, etc.), mais elle donne une première intuition. Regardons plutôt la taille de la section `.text` seule :

```bash
$ for f in keygenme_O0 keygenme_O2 keygenme_O3; do
    echo -n "$f .text: "
    readelf -S $f | grep '\.text' | awk '{print $6}' 
  done
keygenme_O0 .text: 0x...  
keygenme_O2 .text: 0x...  
keygenme_O3 .text: 0x...  
```

Et comptons le nombre de lignes d'instructions dans la section `.text` :

```bash
$ for f in keygenme_O0 keygenme_O2 keygenme_O3; do
    echo -n "$f : "
    objdump -d -M intel -j .text $f | grep '^ ' | wc -l
  done
```

Vous constaterez typiquement que `-O2` produit **moins** d'instructions que `-O0`, et `-O3` peut en produire légèrement **plus** que `-O2`. C'est contre-intuitif au premier abord : `-O3` optimise plus agressivement, mais certaines de ses transformations (déroulage de boucles, vectorisation) *augmentent* la taille du code au profit de la vitesse. Optimiser ne signifie pas toujours « produire moins de code » — cela signifie « produire du code plus rapide ».

---

## `-O0` : la traduction fidèle du C

Le niveau `-O0` demande à GCC de ne faire **aucune optimisation**. Le code assembleur produit est une traduction quasi-littérale du code source C, instruction par instruction. C'est le niveau le plus facile à comprendre en RE, car chaque variable locale a une place dédiée sur la pile, chaque opération correspond à une instruction visible, et le flux de contrôle suit exactement celui du source.

Voici les caractéristiques dominantes d'un code compilé en `-O0` :

### Tout passe par la pile

Chaque variable locale est stockée à une position fixe sur la pile (relative à `rbp`). Même quand une valeur vient d'être calculée dans un registre, GCC la range immédiatement en mémoire, puis la recharge depuis la mémoire pour l'utiliser à l'opération suivante. Cela produit des séquences de type *store-load* systématiques :

```asm
; hash += (int)input[i];
mov    eax, DWORD PTR [rbp-0x8]       ; charge 'i' depuis la pile  
cdqe                                    ; extension signe eax → rax  
add    rax, QWORD PTR [rbp-0x18]      ; ajoute le pointeur 'input'  
movzx  eax, BYTE PTR [rax]            ; charge input[i] (1 octet)  
movsx  eax, al                         ; extension signe → 32 bits  
add    DWORD PTR [rbp-0x4], eax        ; hash += ... (directement en mémoire)  
```

Six instructions pour une simple addition dans une boucle. En `-O2`, cette séquence sera réduite à deux ou trois instructions en gardant les valeurs dans des registres.

### Prologue/épilogue systématique avec frame pointer

Chaque fonction commence par `push rbp` / `mov rbp, rsp` et se termine par `leave` / `ret`. Le *frame pointer* (`rbp`) est toujours maintenu, ce qui permet de naviguer facilement dans la pile avec GDB. Les variables locales sont toutes accessibles via `[rbp-N]`.

### Pas d'inlining, pas de réordonnancement

Chaque appel de fonction dans le code C produit un `call` dans l'assembleur. Les instructions apparaissent dans le même ordre que le code source. Les branches `if/else` se traduisent par un `cmp` suivi d'un saut conditionnel (`jz`, `jne`…) qui reflète directement la condition du `if`.

### Les boucles sont littérales

Une boucle `for` produit exactement le pattern attendu : initialisation, saut vers le test, corps, incrémentation, test, saut conditionnel vers le corps.

```asm
; for (int i = 0; input[i] != '\0'; i++)
    mov    DWORD PTR [rbp-0x8], 0x0    ; i = 0
    jmp    test_label                   ; saut vers le test
body_label:
    ; ... corps de la boucle ...
    add    DWORD PTR [rbp-0x8], 0x1    ; i++
test_label:
    ; ... charger input[i], comparer avec 0 ...
    jne    body_label                   ; si != 0, continuer
```

C'est presque du pseudocode structuré — et c'est exactement pourquoi les binaires `-O0` sont idéaux pour l'apprentissage du RE.

---

## `-O2` : l'optimisation standard de production

Le niveau `-O2` est celui que vous rencontrerez le plus souvent sur les binaires de production. GCC active une large batterie d'optimisations : allocation de registres, élimination de sous-expressions communes, propagation de constantes, *strength reduction*, réordonnancement d'instructions, et bien d'autres.

Désassemblons la même fonction `compute_hash` en `-O2` et observons les transformations :

```bash
objdump -d -M intel keygenme_O2 | less
```

### Les variables vivent dans les registres

La transformation la plus visible : les variables locales ne sont plus stockées sur la pile. Le compilateur utilise les registres du processeur (il y en a 16 en x86-64, c'est généreux) pour garder les valeurs en vol. Le résultat : beaucoup moins d'accès mémoire, des instructions plus courtes, et un listing plus compact.

Là où `-O0` faisait :

```asm
; Charger i, l'utiliser, ranger le résultat
mov    eax, DWORD PTR [rbp-0x8]       ; charge i depuis la pile
...
mov    DWORD PTR [rbp-0x4], eax        ; range hash sur la pile
```

En `-O2`, vous verrez quelque chose comme :

```asm
; i reste dans ecx, hash reste dans edx — tout en registres
movsx  eax, BYTE PTR [rdi+rcx]        ; charge input[i] directement  
add    edx, eax                        ; hash += input[i]  
```

Deux instructions au lieu de six. Pas d'accès à `[rbp-...]`. Les noms de variables ont disparu — ce sont juste des registres.

> 💡 **Conséquence pour le RE** : en `-O2`, vous devez **tracer les registres** au lieu de tracer les positions sur la pile. C'est plus difficile car un même registre peut être réutilisé pour des variables différentes au fil de la fonction. Un bon réflexe : annotez dans la marge quel registre correspond à quelle « variable logique » au fur et à mesure de votre lecture.

### Le frame pointer disparaît (souvent)

Avec `-O2`, GCC active par défaut `-fomit-frame-pointer`. Le prologue `push rbp` / `mov rbp, rsp` disparaît. La fonction commence directement par son code utile, et `rbp` est utilisé comme un registre général supplémentaire. Les accès aux variables locales (s'il en reste sur la pile) se font via `rsp` au lieu de `rbp`.

```asm
; -O0 : prologue classique
push   rbp  
mov    rbp, rsp  
sub    rsp, 0x20  

; -O2 : prologue minimal (ou absent)
sub    rsp, 0x8          ; juste l'alignement si nécessaire
; ... ou rien du tout si la fonction n'a pas besoin de pile
```

Cette absence de prologue casse la méthode de recherche de frontières de fonctions par `push rbp` que nous avons vue en 7.1. Sur un binaire strippé compilé en `-O2`, il faut s'appuyer davantage sur les cibles de `call` et sur les instructions `ret` pour délimiter les fonctions.

### Réordonnancement des instructions

GCC réorganise les instructions pour maximiser le parallélisme au niveau du pipeline du processeur. Le résultat : le code ne suit plus l'ordre du source. Une instruction qui initialise une variable peut se retrouver plusieurs lignes avant l'endroit où vous l'attendriez, parce que le compilateur l'a « avancée » pour masquer la latence d'un accès mémoire.

C'est déstabilisant au début. En `-O0`, vous pouviez lire le listing de haut en bas et retrouver le fil du C. En `-O2`, il faut parfois reconstituer le flux logique en suivant les dépendances de données (« ce registre a été écrit ici, lu là ») plutôt que l'ordre séquentiel.

### Remplacement d'idiomes : strength reduction

Certaines opérations « coûteuses » sont remplacées par des équivalents plus rapides. L'exemple classique est la multiplication par une constante, remplacée par des combinaisons de décalages et d'additions :

```asm
; hash = hash * 8  en -O0
imul   eax, DWORD PTR [rbp-0x4], 0x8

; hash = hash * 8  en -O2  (strength reduction)
shl    edx, 3                          ; décalage à gauche de 3 = ×8
```

De même, une division par une constante peut être remplacée par une multiplication par l'inverse modulaire suivi d'un décalage — une séquence qui semble totalement opaque si on ne connaît pas le pattern :

```asm
; x / 10  en -O2 (magic number division)
mov    eax, edi  
mov    edx, 0xcccccccd  
mul    edx  
shr    edx, 3  
```

Ces transformations seront étudiées en détail au chapitre 16. Pour l'instant, retenez simplement que si vous tombez sur une multiplication bizarre suivie d'un décalage, c'est probablement une division par une constante optimisée par GCC.

### Élimination de code mort et propagation de constantes

Si GCC peut déterminer au moment de la compilation qu'une branche ne sera jamais prise, il la supprime entièrement. Si une variable est toujours égale à une constante, il remplace la variable par la constante partout. Le code assembleur résultant peut contenir significativement moins de branches que le code source C ne le laissait présager.

---

## `-O3` : optimisation agressive

Le niveau `-O3` inclut tout ce que fait `-O2`, plus des transformations supplémentaires qui peuvent considérablement modifier la structure du code :

### Déroulage de boucles (*loop unrolling*)

Au lieu d'exécuter le corps d'une boucle une fois par itération avec un test de continuation, GCC duplique le corps plusieurs fois pour réduire le nombre de sauts :

```asm
; Boucle originale en C : for (i=0; i<4; i++) hash += input[i];

; En -O2 : boucle classique avec test
.loop:
    movsx  eax, BYTE PTR [rdi+rcx]
    add    edx, eax
    inc    rcx
    cmp    rcx, r8
    jl     .loop

; En -O3 : boucle déroulée (le compilateur peut traiter 2 ou 4 éléments par itération)
    movsx  eax, BYTE PTR [rdi+rcx]
    movsx  r9d, BYTE PTR [rdi+rcx+1]
    add    edx, eax
    add    edx, r9d
    add    rcx, 2
    cmp    rcx, r8
    jl     .loop
```

Le résultat est plus de code (le corps est dupliqué), mais moins de sauts (le test n'est évalué qu'une fois pour deux itérations). En RE, une boucle déroulée peut ressembler à du code séquentiel « copié-collé » — si vous voyez des séquences très similaires répétées deux ou quatre fois, c'est probablement du déroulage.

### Vectorisation SIMD

GCC peut transformer une boucle scalaire en opérations SIMD utilisant les registres `xmm` ou `ymm` (SSE/AVX). Un code qui traitait les éléments d'un tableau un par un se met à les traiter par paquets de 4 (SSE, 128 bits) ou 8 (AVX, 256 bits) :

```asm
; Traitement vectorisé en -O3
movdqu xmm0, XMMWORD PTR [rdi+rcx]    ; charge 16 octets d'un coup  
paddb  xmm1, xmm0                      ; addition parallèle sur 16 octets  
add    rcx, 16  
cmp    rcx, rax  
jb     .loop_vec  
```

Si vous n'avez jamais vu d'instructions SIMD, ces mnémoniques (`movdqu`, `paddb`, `pxor`, `pmaddwd`…) semblent appartenir à un autre langage. Le chapitre 3 (section 3.9) les introduit brièvement, et le chapitre 16 les couvre en profondeur. Pour l'instant, retenez que si vous voyez des registres `xmm`/`ymm` et des instructions commençant par `p` ou `v`, c'est de la vectorisation — et la boucle originale était probablement scalaire.

### Inlining plus agressif

En `-O3`, GCC est plus enclin à intégrer (*inline*) les petites fonctions directement dans leur appelant. Une fonction qui existait comme un `call` distinct en `-O2` peut disparaître complètement en `-O3` : son code est copié à chaque point d'appel. Le résultat : moins de `call`, des fonctions plus longues, et des fonctions « fantômes » qui n'apparaissent plus dans le listing mais dont le code est dispersé dans d'autres fonctions.

---

## Comparaison visuelle : la même logique, trois visages

Pour synthétiser les différences, voici ce que vous pouvez observer en désassemblant les trois variantes d'un même programme :

| Caractéristique | `-O0` | `-O2` | `-O3` |  
|---|---|---|---|  
| Variables locales | Sur la pile (`[rbp-N]`) | Dans les registres | Dans les registres (+ SIMD) |  
| Frame pointer (`rbp`) | Toujours présent | Souvent omis | Souvent omis |  
| Prologue `push rbp`/`mov rbp,rsp` | Systématique | Rare | Rare |  
| Nombre d'instructions | Élevé (beaucoup de load/store) | Réduit | Variable (déroulage ↑, vectorisation ↑) |  
| Correspondance avec le source C | Quasi-directe | Reconnaissable mais réorganisée | Parfois très éloignée |  
| Sauts conditionnels | Reflètent les `if`/`else` du C | Peuvent être inversés, réordonnés | Idem + branchless (`cmov`) |  
| Appels de fonctions | Tous visibles comme `call` | Petites fonctions possiblement inlinées | Inlining agressif |  
| Boucles | Structure for/while reconnaissable | Compactes, parfois inversées | Déroulées, vectorisées |  
| Multiplication/division par constante | `imul`/`idiv` littéral | Strength reduction (shifts, magic numbers) | Idem |  
| Registres `xmm`/`ymm` | Absents (sauf calculs flottants) | Possibles | Fréquents (vectorisation) |  
| Difficulté de RE | ★☆☆ Facile | ★★☆ Intermédiaire | ★★★ Difficile |

---

## `-Os` : le cas particulier de l'optimisation en taille

Bien que le `Makefile` ne produise pas de variante `-Os`, ce niveau mérite une mention. `-Os` active les mêmes optimisations que `-O2`, **sauf** celles qui augmentent la taille du code. Pas de déroulage de boucles, pas de vectorisation extensive, inlining limité. Le code résultant est compact et ressemble beaucoup à du `-O2`, mais sans les duplications de `-O3`.

On rencontre `-Os` dans les firmwares, les systèmes embarqués, et les binaires distribués sur réseau (installateurs, mises à jour) où la taille compte. En RE, il se lit comme du `-O2` « sage ».

---

## Méthode pratique : comparer deux listings avec `diff`

Pour voir concrètement les différences entre deux niveaux d'optimisation, générez les listings puis comparez-les :

```bash
# Générer les listings (uniquement .text, syntaxe Intel)
objdump -d -M intel -j .text keygenme_O0 > /tmp/O0.asm  
objdump -d -M intel -j .text keygenme_O2 > /tmp/O2.asm  

# Comparaison visuelle
diff --color /tmp/O0.asm /tmp/O2.asm | less

# Ou côte à côte
diff -y --width=160 /tmp/O0.asm /tmp/O2.asm | less
```

Cette comparaison directe est extrêmement formatrice. Vous verrez les fonctions se contracter (moins de lignes), les accès `[rbp-N]` disparaître au profit de registres, les prologues se simplifier, et les boucles changer de structure.

Une alternative encore plus parlante : utiliser la sortie `-S` (source entrelacée) avec les symboles de débogage, en compilant les deux versions avec `-g` :

```bash
gcc -O0 -g -o keygenme_O0g keygenme.c  
gcc -O2 -g -o keygenme_O2g keygenme.c  

objdump -d -S -M intel keygenme_O0g > /tmp/O0_src.asm  
objdump -d -S -M intel keygenme_O2g > /tmp/O2_src.asm  
```

Avec `-O2 -g`, GCC conserve les informations DWARF même en optimisant. Le listing entrelacé montre alors les mêmes lignes de C associées à un assembleur très différent — c'est la démonstration la plus frappante de l'impact des optimisations.

> 💡 **Outil complémentaire** : le site [Compiler Explorer (godbolt.org)](https://godbolt.org) permet de visualiser en temps réel le code assembleur produit par GCC à différents niveaux d'optimisation, avec coloration par correspondance source/assembleur. C'est un complément idéal à `objdump` pour explorer les transformations du compilateur.

---

## Implications pour la stratégie de RE

Cette comparaison a des conséquences directes sur votre approche en reverse engineering :

**Commencez toujours par identifier le niveau d'optimisation probable.** Si le binaire contient des prologues `push rbp`/`mov rbp,rsp` systématiques et des accès `[rbp-N]` omniprésents, c'est du `-O0` (ou `-O1`) — vous êtes en terrain facile. Si le frame pointer est absent et que les variables vivent dans les registres, c'est au moins du `-O2`. Si vous voyez du déroulage de boucles, des registres `xmm`, et des *magic numbers* pour les divisions, c'est du `-O3` ou équivalent.

**Adaptez votre granularité de lecture.** Sur du `-O0`, vous pouvez presque lire instruction par instruction et traduire en C au fil de l'eau. Sur du `-O2`/`-O3`, il vaut mieux d'abord identifier les blocs logiques (prologue, boucle, appels de fonction, retour) puis comprendre chaque bloc comme un ensemble, plutôt que de s'accrocher à chaque instruction individuelle.

**Ne cherchez pas un mapping 1:1 avec le source.** Sur un binaire optimisé, une seule ligne de C peut produire zéro instruction (optimisée) ou dix instructions (déroulage + vectorisation). Inversement, trois lignes de C peuvent être fusionnées en deux instructions assembleur. Cherchez la **logique** plutôt que la correspondance syntaxique.

**Exploitez les binaires d'entraînement fournis.** Ce tutoriel fournit chaque binaire à plusieurs niveaux d'optimisation précisément pour que vous puissiez pratiquer cette comparaison. Analysez d'abord la version `-O0` pour comprendre la logique, puis attaquez la version `-O2` en sachant déjà ce que vous cherchez. C'est un luxe que vous n'aurez pas en conditions réelles, alors profitez-en pendant la formation.

---

## Résumé

Le niveau d'optimisation de GCC transforme radicalement l'assembleur produit. En `-O0`, le code est une traduction fidèle du C : variables sur la pile, prologues complets, boucles littérales. En `-O2`, les variables migrent vers les registres, le frame pointer disparaît, les instructions sont réordonnées, et les idiomes du compilateur (strength reduction, propagation de constantes) remplacent les opérations naïves. En `-O3`, le déroulage de boucles, la vectorisation SIMD et l'inlining agressif peuvent rendre le code assembleur très éloigné du source original. Reconnaître ces transformations est une compétence essentielle : elle vous permet d'identifier rapidement le niveau d'optimisation d'un binaire inconnu et d'adapter votre stratégie d'analyse en conséquence.

---


⏭️ [Lecture du prologue/épilogue de fonctions en pratique](/07-objdump-binutils/04-prologue-epilogue.md)
