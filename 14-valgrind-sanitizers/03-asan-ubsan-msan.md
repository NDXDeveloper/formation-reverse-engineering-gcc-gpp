🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 14.3 — AddressSanitizer (ASan), UBSan, MSan — compiler avec `-fsanitize`

> 🎯 **Objectif de cette section** : Comprendre le fonctionnement, les forces et les différences des trois principaux sanitizers de GCC/Clang — AddressSanitizer, UndefinedBehaviorSanitizer et MemorySanitizer — savoir recompiler un binaire pour les activer, et interpréter leurs rapports dans une perspective de reverse engineering pour en déduire la structure interne, les failles logiques et le flux de données du programme cible.

---

## Les sanitizers face à Valgrind : deux philosophies d'instrumentation

En sections 14.1 et 14.2, nous avons travaillé avec Valgrind, qui instrumente un binaire **de l'extérieur**, à l'exécution, sans le modifier. Les sanitizers adoptent l'approche inverse : ils instrumentent le binaire **de l'intérieur**, à la compilation, en injectant du code de vérification directement dans le binaire produit.

Cette différence fondamentale a des conséquences directes sur ce qu'on peut observer.

| Critère | Valgrind (Memcheck) | Sanitizers (ASan/UBSan/MSan) |  
|---|---|---|  
| Modification du binaire | Aucune | Recompilation nécessaire |  
| Méthode d'instrumentation | VM logicielle à l'exécution | Code injecté à la compilation |  
| Ralentissement typique | 10–50x | 1.5–3x (ASan), ~1x (UBSan) |  
| Détection stack overflow | Limitée | Excellente (ASan) |  
| Détection heap overflow | Bonne | Excellente (ASan, zones rouges) |  
| Détection UB (undefined behavior) | Non | Oui (UBSan) |  
| Détection lectures non initialisées | Oui (V-bits) | Oui (MSan uniquement) |  
| Fonctionne sur binaire tiers | Oui | Non (sources nécessaires) |

La question naturelle en contexte de RE : **si les sanitizers nécessitent les sources, à quoi servent-ils ?**

La réponse est triple. Premièrement, dans le cadre de cette formation, nous disposons des sources de tous les binaires d'entraînement — nous pouvons donc les recompiler avec les sanitizers pour explorer leur comportement. Deuxièmement, en situation réelle de RE, il arrive qu'on reconstruise un code source approximatif à partir de la décompilation (chapitre 20) — on peut alors le recompiler avec les sanitizers pour vérifier nos hypothèses. Troisièmement, certains projets open source distribuent des builds instrumentés (les « sanitizer builds ») destinés au fuzzing, et ces builds sont directement analysables.

Enfin, la vitesse d'exécution des sanitizers (seulement 1.5 à 3x de ralentissement pour ASan, contre 10–50x pour Valgrind) les rend beaucoup plus pratiques pour le **fuzzing instrumenté** (chapitre 15) : on peut exécuter des millions d'inputs sous ASan dans le temps qu'il faudrait pour en exécuter quelques milliers sous Valgrind.

---

## AddressSanitizer (ASan)

ASan est le plus utilisé des trois sanitizers. Il détecte les erreurs d'accès mémoire — débordements de buffers, use-after-free, use-after-return, double free — avec une précision et une vitesse remarquables.

### Principe de fonctionnement

ASan repose sur deux mécanismes complémentaires :

**1. Shadow memory** — Comme Memcheck, ASan maintient une mémoire ombre. Mais là où Memcheck utilise un bit par octet, ASan utilise un schéma plus compact : chaque groupe de 8 octets de mémoire applicative est représenté par **1 octet** de shadow memory. Cet octet encode combien des 8 octets sont accessibles (de 0 = aucun à 8 = tous, ou une valeur spéciale pour les zones empoisonnées). Ce ratio 1:8 est ce qui permet à ASan d'être beaucoup plus rapide que Memcheck (1:1 ou plus).

**2. Zones rouges (redzones)** — À la compilation, ASan insère des **zones empoisonnées** autour de chaque variable sur la pile et autour de chaque allocation sur le tas. Ces zones rouges sont marquées comme inaccessibles dans la shadow memory. Tout accès à une redzone déclenche immédiatement un rapport d'erreur.

Concrètement, quand vous déclarez un buffer de 64 octets sur la pile, ASan l'entoure de 32 octets de redzone de chaque côté. Si le programme écrit au-delà des 64 octets, il touche la redzone et ASan le détecte instantanément — même si le débordement est d'un seul octet.

C'est la raison pour laquelle ASan est **supérieur à Valgrind pour la détection des stack buffer overflows**. Valgrind ne peut pas empoisonner les zones autour des variables de pile (il ne connaît pas leur layout), tandis qu'ASan les instrumente à la compilation parce qu'il dispose de l'information du compilateur sur la taille et la position de chaque variable.

### Compilation avec ASan

La syntaxe de base avec GCC :

```bash
gcc -fsanitize=address -g -O0 -o mon_binaire_asan mon_binaire.c
```

Détaillons les flags :

**`-fsanitize=address`** — Active AddressSanitizer. GCC insère le code d'instrumentation (vérifications de shadow memory, redzones) dans le binaire compilé et lie automatiquement la bibliothèque runtime ASan (`libasan`).

**`-g`** — Ajoute les symboles de débogage. Indispensable pour qu'ASan affiche les numéros de ligne et noms de fonctions dans ses rapports. Sans `-g`, ASan affiche uniquement des adresses brutes — exploitables mais beaucoup moins lisibles.

**`-O0`** — Désactive les optimisations. Recommandé pour l'analyse la plus fine car l'inlining et les réorganisations de code masquent certains problèmes. Cependant, ASan fonctionne aussi avec `-O1` et `-O2`. Avec `-O2`, certaines variables sont optimisées dans des registres et échappent à l'instrumentation des redzones.

Pour un projet C++ avec plusieurs fichiers :

```bash
g++ -fsanitize=address -g -O0 -o oop_asan main.cpp utils.cpp -lstdc++
```

> ⚠️ **Attention** — Tous les fichiers objets doivent être compilés avec `-fsanitize=address`. Si vous liez un fichier compilé avec ASan avec un fichier compilé sans, les résultats seront incohérents et potentiellement des faux positifs apparaîtront.

### Options d'environnement ASan

Le comportement d'ASan à l'exécution se configure via la variable d'environnement `ASAN_OPTIONS` :

```bash
ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=0:log_path=asan_report" \
    ./mon_binaire_asan arg1 arg2
```

Les options les plus utiles en RE :

**`detect_leaks=1`** — Active LeakSanitizer (LSan), qui détecte les fuites mémoire de manière similaire à Memcheck. Activé par défaut sur Linux. Comme pour Memcheck, les fuites révèlent les tailles de structures et les fonctions d'allocation.

**`detect_stack_use_after_return=1`** — Détecte l'accès à des variables locales d'une fonction après son retour. Cette option est coûteuse en mémoire (ASan déplace les frames de pile sur le tas pour pouvoir les empoisonner après le retour), mais elle détecte un bug subtil qui échappe à Memcheck.

**`halt_on_error=0`** — Par défaut, ASan arrête le programme à la première erreur. Avec `halt_on_error=0`, il continue l'exécution et rapporte toutes les erreurs rencontrées. En RE, on veut généralement voir *toutes* les erreurs pour avoir le tableau le plus complet possible du comportement mémoire du programme.

**`log_path=asan_report`** — Redirige les rapports vers des fichiers préfixés par `asan_report` (un fichier par PID : `asan_report.12345`). Sans cette option, ASan écrit sur `stderr`.

**`symbolize=1`** — Active la symbolisation des adresses. Nécessite que `llvm-symbolizer` ou `addr2line` soit disponible dans le PATH. Si le binaire a été compilé avec `-g`, les rapports contiendront les noms de fonctions et numéros de ligne.

### Anatomie d'un rapport ASan

Un rapport ASan typique pour un stack buffer overflow :

```
=================================================================
==12345==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x7ffd4a3c2e50
    at pc 0x00401a3f bp 0x7ffd4a3c2df0 sp 0x7ffd4a3c2de8
WRITE of size 4 at 0x7ffd4a3c2e50 thread T0                     ← (1)
    #0 0x401a3e in process_key /src/keygenme.c:47                ← (2)
    #1 0x401b11 in validate_input /src/keygenme.c:82
    #2 0x4012e7 in main /src/keygenme.c:112
    #3 0x7f3a2c1b0d8f in __libc_start_call_main (libc.so.6+0x29d8f)

Address 0x7ffd4a3c2e50 is located in stack of thread T0          ← (3)
    at offset 80 in frame
    #0 0x401980 in process_key /src/keygenme.c:31

  This frame has 2 object(s):                                    ← (4)
    [32, 64) 'key_buffer' (line 33)                              ← taille = 32 octets
    [96, 128) 'hash_output' (line 34)                            ← taille = 32 octets
HINT: this may be a false positive if your program uses
    some custom stack unwind mechanism
```

Décortiquons chaque partie du point de vue RE :

**(1) Nature et taille de l'accès** — « WRITE of size 4 » : le programme écrit 4 octets (un `int` ou `uint32_t`) à une adresse invalide. C'est un **stack buffer overflow** — l'écriture dépasse les limites d'une variable locale.

**(2) Pile d'appels avec noms et lignes** — Grâce à `-g`, on voit que l'erreur se produit dans `process_key` à la ligne 47, appelée par `validate_input` (ligne 82), elle-même appelée par `main` (ligne 112). Si on travaille sans symboles, on verrait des adresses brutes, mais la structure de la pile d'appels resterait lisible.

**(3) Localisation dans le frame de pile** — « at offset 80 in frame » nous donne l'offset exact de l'accès fautif dans le frame de la fonction `process_key`. Le frame commence à l'offset 0, donc l'accès est à 80 octets du début du frame.

**(4) Layout des objets du frame** — C'est la partie la plus précieuse pour le RE. ASan nous donne le **layout exact des variables locales** de la fonction :

- `key_buffer` occupe les octets [32, 64) → taille 32 octets, commence à l'offset 32 du frame.  
- `hash_output` occupe les octets [96, 128) → taille 32 octets, commence à l'offset 96.  
- L'espace entre 64 et 96 (32 octets) est la **redzone** insérée par ASan entre les deux variables.

L'accès fautif est à l'offset 80, qui tombe dans la redzone entre `key_buffer` et `hash_output`. Cela signifie que le programme écrit 16 octets au-delà de la fin de `key_buffer` (64 + 16 = 80). Le buffer fait 32 octets mais le programme tente d'y écrire 48 octets — probablement parce que la routine de hashing produit un output de 48 octets sur un buffer dimensionné pour 32.

> 💡 **Astuce RE** — Le layout des objets du frame donné par ASan est une information que même Ghidra ne fournit pas toujours correctement. Les décompilateurs reconstruisent les variables locales par heuristique, avec parfois des erreurs de taille ou de position. ASan, lui, connaît le layout exact puisqu'il a accès à l'information du compilateur. C'est un outil de vérification puissant pour valider ou corriger la reconstruction des variables locales dans Ghidra.

### Rapport ASan : heap buffer overflow

```
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000040
READ of size 1 at 0x602000000040 thread T0
    #0 0x401c7e in decrypt_block /src/crypto.c:156
    #1 0x401da4 in process_file /src/crypto.c:201
    #2 0x4012e7 in main /src/crypto.c:245

0x602000000040 is located 0 bytes after 32-byte region [0x602000000020,0x602000000040)
allocated by thread T0 here:                                      ← (5)
    #0 0x7f3a2c8b0808 in __interceptor_malloc (libasan.so.8+0xb0808)
    #1 0x401b89 in init_cipher_ctx /src/crypto.c:98
    #2 0x401da4 in process_file /src/crypto.c:189
```

**(5) Provenance de l'allocation** — ASan indique que le bloc fautif fait exactement **32 octets**, a été alloué dans `init_cipher_ctx` à la ligne 98, et que l'accès se produit « 0 bytes after » — exactement un octet après la fin du bloc. C'est un off-by-one classique.

En RE, les informations exploitables sont identiques à celles de Memcheck (cf. section 14.1), mais avec deux avantages majeurs :

- ASan donne la **taille exacte des redzones** et donc la **position exacte du débordement**, alors que Memcheck détecte uniquement les accès hors du bloc alloué sans précision sur la distance.  
- ASan fournit ces rapports **beaucoup plus vite** (1.5–3x de ralentissement vs 10–50x), ce qui le rend viable pour des exécutions répétées avec de nombreux inputs.

### Rapport ASan : use-after-free

```
=================================================================
==12345==ERROR: AddressSanitizer: heap-use-after-free on address 0x603000000010
READ of size 8 at 0x603000000010 thread T0
    #0 0x401e23 in send_response /src/network.c:178
    #1 0x4012e7 in main /src/network.c:230

0x603000000010 is located 0 bytes inside of 128-byte region [0x603000000010,0x603000000090)
freed by thread T0 here:                                          ← (6)
    #0 0x7f3a2c8b1230 in __interceptor_free (libasan.so.8+0xb1230)
    #1 0x401d10 in close_connection /src/network.c:162

previously allocated by thread T0 here:                           ← (7)
    #0 0x7f3a2c8b0808 in __interceptor_malloc (libasan.so.8+0xb0808)
    #1 0x401c45 in open_connection /src/network.c:134
```

Ce rapport est une mine d'or pour le RE d'un binaire réseau :

**(6) Qui a libéré le bloc** — La fonction `close_connection` (ligne 162) libère un bloc de 128 octets. C'est la fonction de fermeture de connexion.

**(7) Qui l'avait alloué** — La fonction `open_connection` (ligne 134) avait alloué ce bloc. C'est la fonction d'ouverture de connexion.

Le programme tente ensuite de lire 8 octets dans ce bloc depuis `send_response` — une tentative d'envoyer des données sur une connexion déjà fermée. En RE, ce rapport nous révèle le **cycle de vie complet d'une structure de connexion** : allocation dans `open_connection`, utilisation dans `send_response`, libération dans `close_connection`. La taille de 128 octets nous donne la taille de la structure de connexion, et l'offset de la lecture (0 bytes inside = au tout début du bloc) pointe vers le premier champ de cette structure — probablement un descripteur de socket ou un pointeur vers le buffer d'envoi.

> 💡 **Astuce RE** — Les rapports use-after-free d'ASan sont particulièrement intéressants car ils révèlent trois fonctions d'un coup : l'allocateur, le libérateur et l'utilisateur tardif. C'est un graphe de cycle de vie que ni Memcheck ni le désassemblage statique ne fournissent aussi directement.

---

## UndefinedBehaviorSanitizer (UBSan)

UBSan détecte les **comportements indéfinis** (undefined behavior, UB) du C et du C++. Contrairement à ASan qui se concentre sur les erreurs mémoire, UBSan cible les violations de la norme du langage qui, bien que compilant sans erreur, produisent des résultats imprévisibles.

### Pourquoi les UB intéressent le reverse engineer

Les comportements indéfinis sont invisibles dans le désassemblage — on voit des instructions arithmétiques normales, des comparaisons, des sauts. Rien ne signale qu'une opération est indéfinie. Pourtant, les UB ont un impact majeur sur le comportement du programme, et surtout sur la manière dont le compilateur optimise le code.

Quand GCC détecte un potentiel UB à la compilation, il est autorisé par la norme à **supposer que le UB n'arrive jamais** et à optimiser en conséquence. Cela produit du code assembleur dont la logique semble incohérente avec ce qu'on imagine du code source. Par exemple, une comparaison qui devrait toujours être vraie peut disparaître du binaire parce que GCC a déduit qu'elle était inutile en supposant l'absence de UB.

En RE, quand on tombe sur du code dont la logique semble « cassée » ou « manquante » après décompilation, la cause est souvent un UB exploité par l'optimiseur. UBSan permet de **confirmer cette hypothèse** en recompilant le code et en observant les UB qui se déclenchent.

### Types de UB détectés par UBSan

UBSan détecte un large éventail de comportements indéfinis, parmi lesquels :

- **Signed integer overflow** — Une addition, soustraction ou multiplication de deux entiers signés dont le résultat dépasse la plage du type. En C, `INT_MAX + 1` est indéfini (contrairement aux unsigned qui wrappent). GCC peut optimiser des boucles ou des conditions en supposant que cet overflow n'arrive pas.  
- **Shift overflow** — Décalage d'un nombre négatif, ou décalage de plus de bits que la taille du type (`x << 33` pour un `int` 32 bits).  
- **Division par zéro** — Entière ou flottante.  
- **Null pointer dereference** — Déréférencement d'un pointeur nul.  
- **Out-of-bounds array access** — Accès hors limites d'un tableau (quand le compilateur peut déterminer les bornes).  
- **Misaligned pointer** — Accès à travers un pointeur mal aligné pour son type (par exemple, lire un `int` à une adresse impaire).  
- **Invalid enum value** — En C++, conversion vers un type `enum` d'une valeur qui n'est pas dans la plage définie.  
- **Invalid bool value** — En C++, un `bool` qui ne vaut ni 0 ni 1.  
- **Return from non-void function without value** — Une fonction déclarée comme retournant une valeur mais qui atteint un chemin sans `return`.

### Compilation avec UBSan

```bash
gcc -fsanitize=undefined -g -O0 -o mon_binaire_ubsan mon_binaire.c
```

On peut activer des sous-catégories spécifiques :

```bash
# Uniquement les overflows d'entiers signés et les décalages
gcc -fsanitize=signed-integer-overflow,shift -g -O0 -o mon_binaire_ubsan mon_binaire.c

# Tout sauf le vfptr (utile en C++ avec du polymorphisme complexe)
gcc -fsanitize=undefined -fno-sanitize=vptr -g -O0 -o mon_binaire_ubsan mon_binaire.cpp
```

> 💡 **Astuce RE** — En contexte de RE, le sous-sanitizer le plus révélateur est `signed-integer-overflow`. Les routines crypto et les fonctions de hashing manipulent intensivement des entiers, et les overflows intentionnels (qui sont techniquement du UB en signé) trahissent la logique du calcul. Si UBSan rapporte un signed overflow dans une boucle de 64 itérations, vous regardez probablement un round de SHA-256 avec des additions modulaires.

### Rapport UBSan typique

```
/src/keygenme.c:47:15: runtime error: signed integer overflow:
    2147483647 + 1 cannot be represented in type 'int'              ← (1)
    #0 0x401a3e in transform_key /src/keygenme.c:47
    #1 0x401b11 in validate_input /src/keygenme.c:82
    #2 0x4012e7 in main /src/keygenme.c:112
```

**(1) Diagnostic précis** — UBSan identifie l'opération exacte (`2147483647 + 1`), le type concerné (`int`), et explique pourquoi c'est un UB. Le nombre `2147483647` est `INT_MAX` — la valeur maximale d'un `int` 32 bits signé. L'ajouter avec 1 cause un overflow.

En RE, cette information nous dit que la fonction `transform_key` effectue une addition sur un `int` qui atteint `INT_MAX`. C'est caractéristique d'un **calcul de hash ou de checksum** qui utilise des additions modulaires. Le développeur utilisait probablement des `unsigned int` dans sa tête (où le wrapping est défini), mais a déclaré la variable comme `int` (signé) — une erreur courante.

### Combiner UBSan avec ASan

ASan et UBSan sont compatibles et peuvent être activés simultanément :

```bash
gcc -fsanitize=address,undefined -g -O0 -o mon_binaire_full mon_binaire.c
```

Cette combinaison est la configuration recommandée pour l'analyse RE : on obtient à la fois les erreurs mémoire (ASan) et les comportements indéfinis (UBSan) en un seul run. Le surcoût de performance d'UBSan est négligeable au-dessus d'ASan.

Le comportement d'UBSan à l'exécution se contrôle via `UBSAN_OPTIONS` :

```bash
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:log_path=ubsan_report" \
    ./mon_binaire_full arg1 arg2
```

**`print_stacktrace=1`** — Affiche la pile d'appels complète pour chaque erreur. Désactivé par défaut pour UBSan (contrairement à ASan). En RE, la pile d'appels est essentielle — activez toujours cette option.

**`halt_on_error=0`** — Comme pour ASan, on continue après chaque erreur pour voir l'ensemble des UB.

---

## MemorySanitizer (MSan)

MSan détecte les **lectures de mémoire non initialisée** — le même type d'erreurs que le V-bit tracking de Valgrind Memcheck. C'est le sanitizer le plus spécialisé des trois, et aussi le plus contraignant à mettre en place.

### Différence avec Memcheck

MSan et Memcheck détectent le même type de problème, mais leur approche diffère :

- **Memcheck** fonctionne sur n'importe quel binaire, sans recompilation. Son instrumentation par VM logicielle est universelle mais lente (10–50x).  
- **MSan** nécessite une recompilation, mais son instrumentation à la compilation est beaucoup plus rapide (environ 3x de ralentissement) et plus précise pour le tracking de propagation des valeurs non initialisées à travers les opérations.

MSan excelle là où Memcheck peut manquer des cas : quand une valeur non initialisée est copiée, combinée avec d'autres valeurs, puis utilisée beaucoup plus tard dans le programme. MSan suit cette propagation instruction par instruction grâce au code qu'il a injecté, tandis que Memcheck peut parfois perdre la trace dans des cas complexes (opérations SIMD, par exemple).

### Contraintes d'utilisation

MSan a une contrainte majeure : **toutes les bibliothèques liées au programme doivent être compilées avec MSan**. Si le programme appelle une fonction de la libc standard (compilée sans MSan), MSan ne peut pas tracker les valeurs non initialisées à travers cette fonction et peut produire des faux positifs ou des faux négatifs.

En pratique, cela signifie qu'il faut soit :

- Lier statiquement une libc compilée avec MSan (complexe à mettre en place).  
- Utiliser l'option `-fsanitize-memory-track-origins` et accepter quelques imprécisions aux frontières des bibliothèques.

> ⚠️ **Attention** — MSan est **uniquement disponible avec Clang**, pas avec GCC. Si votre chaîne de compilation est GCC, vous devrez utiliser Clang pour cette étape ou vous rabattre sur Valgrind Memcheck pour la détection de lectures non initialisées.

### Compilation avec MSan (Clang)

```bash
clang -fsanitize=memory -fsanitize-memory-track-origins=2 -g -O0 \
    -o mon_binaire_msan mon_binaire.c
```

**`-fsanitize=memory`** — Active MemorySanitizer.

**`-fsanitize-memory-track-origins=2`** — Active le tracking d'origine avec une profondeur de 2. Sans cette option, MSan signale l'utilisation d'une valeur non initialisée mais ne dit pas d'où elle vient. Avec `=1`, il remonte à l'allocation. Avec `=2`, il remonte en plus à la dernière opération de stockage — ce qui montre le chemin de propagation de la valeur non initialisée.

### Rapport MSan typique

```
==12345==WARNING: MemorySanitizer: use-of-uninitialized-value
    #0 0x401c7e in encrypt_block /src/crypto.c:156
    #1 0x401da4 in process_file /src/crypto.c:201
    #2 0x4012e7 in main /src/crypto.c:245

  Uninitialized value was stored to memory at
    #0 0x401b45 in prepare_iv /src/crypto.c:122
    #1 0x401da4 in process_file /src/crypto.c:195

  Uninitialized value was created by a heap allocation
    #0 0x7f3a2c4a0808 in malloc
    #1 0x401b20 in init_cipher_ctx /src/crypto.c:98
```

Ce rapport à trois niveaux nous donne le **flux complet d'une donnée non initialisée** :

1. **Création** — `init_cipher_ctx` alloue un bloc sur le tas (ligne 98). Ce bloc contient (entre autres) l'espace pour un IV.  
2. **Stockage** — `prepare_iv` écrit dans ce bloc (ligne 122), mais apparemment pas tous les octets — certains restent non initialisés.  
3. **Utilisation** — `encrypt_block` utilise la valeur non initialisée dans une opération de chiffrement (ligne 156).

> 💡 **Astuce RE** — Ce rapport à trois étages nous donne un **flux de données** (data flow) qui traverse trois fonctions. En RE, c'est de la taint analysis gratuite : on voit exactement comment une donnée se propage de l'allocation à l'utilisation en passant par une transformation intermédiaire. Ce type d'information est normalement difficile à extraire sans outils spécialisés comme Triton ou le taint engine d'angr.

---

## Combinabilité des sanitizers

Les trois sanitizers ne sont pas tous compatibles entre eux. Voici la matrice de compatibilité :

| Combinaison | Compatible ? | Notes |  
|---|---|---|  
| ASan + UBSan | Oui | Configuration recommandée, combinaison la plus courante |  
| ASan + MSan | Non | Les deux instrumentent la mémoire de manière incompatible |  
| UBSan + MSan | Oui | Possible avec Clang |  
| ASan + TSan | Non | TSan (ThreadSanitizer) a sa propre shadow memory |  
| UBSan + TSan | Oui | Possible |

En pratique, pour une analyse RE complète, on effectue **deux runs séparés** :

```bash
# Run 1 : ASan + UBSan (erreurs mémoire + comportements indéfinis)
gcc -fsanitize=address,undefined -g -O0 -o binaire_asan_ubsan source.c  
ASAN_OPTIONS="halt_on_error=0:log_path=asan" \  
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \  
    ./binaire_asan_ubsan input_test

# Run 2 : MSan (lectures non initialisées) — Clang uniquement
clang -fsanitize=memory -fsanitize-memory-track-origins=2 -g -O0 \
    -o binaire_msan source.c
MSAN_OPTIONS="halt_on_error=0:log_path=msan" \
    ./binaire_msan input_test
```

On fusionne ensuite les rapports pour avoir le tableau complet.

---

## Impact des flags de compilation sur les rapports

Les sanitizers interagissent avec les niveaux d'optimisation de manière subtile. Comprendre ces interactions est crucial pour ne pas mal interpréter un rapport.

### ASan et les optimisations

| Flag | Impact sur ASan |  
|---|---|  
| `-O0` | Meilleure couverture — toutes les variables sont en mémoire, toutes les redzones sont présentes. Rapports les plus détaillés. |  
| `-O1` | Certaines variables sont promues dans des registres et échappent aux redzones. Les rapports restent fiables mais peuvent manquer quelques overflows de variables locales. |  
| `-O2` / `-O3` | Inlining agressif — les fonctions inlinées perdent leurs redzones propres. Les rapports sont toujours valides pour le tas, mais la couverture pile est dégradée. |

### UBSan et les optimisations

UBSan présente une particularité intéressante : certains UB ne sont détectables **qu'avec les optimisations activées**.

Exemple : un signed integer overflow dans une boucle peut être invisible en `-O0` parce que le compilateur génère un code naïf qui wrappe naturellement. En `-O2`, GCC optimise la boucle en supposant que l'overflow n'arrive pas, ce qui produit un comportement différent — et c'est ce comportement différent que UBSan capture.

La recommandation en RE : lancer UBSan à la fois en `-O0` (détection exhaustive) et en `-O2` (détection des UB exploités par l'optimiseur). La comparaison des deux rapports révèle quels UB sont activement exploités par le compilateur dans la version optimisée du binaire.

### MSan et les optimisations

MSan est le plus sensible aux optimisations. Avec `-O2`, le compilateur peut réordonner les accès mémoire ou éliminer des lectures/écritures redondantes, ce qui modifie le flux de propagation des valeurs non initialisées. Les rapports restent corrects (pas de faux négatifs), mais le tracking d'origine peut devenir confus (le chemin affiché ne correspond plus exactement au code source).

En résumé :

```
┌─────────────────────────────────────────────────────┐
│  Recommandation : commencer TOUJOURS par -O0 -g     │
│  pour les rapports les plus lisibles et les plus    │
│  complets. Relancer en -O2 uniquement si on veut    │
│  vérifier un comportement spécifique à              │
│  l'optimisation.                                    │
└─────────────────────────────────────────────────────┘
```

---

## Compilation sélective : instrumenter un seul fichier

Dans un projet multi-fichiers, on n'a pas toujours besoin d'instrumenter l'intégralité du programme. GCC permet de compiler certains fichiers avec sanitizers et d'autres sans. Le linker s'occupe de lier la bibliothèque runtime appropriée dès qu'au moins un fichier est instrumenté :

```bash
# Instrumenter uniquement crypto.c, pas main.c ni utils.c
gcc -c -fsanitize=address -g -O0 crypto.c -o crypto_asan.o  
gcc -c -g -O0 main.c -o main.o  
gcc -c -g -O0 utils.c -o utils.o  
gcc -fsanitize=address -o programme crypto_asan.o main.o utils.o  
```

En RE, cette approche est utile quand on a reconstruit partiellement les sources d'un programme et qu'on veut instrumenter uniquement le module qu'on étudie (par exemple, le module crypto) sans subir le bruit des erreurs dans le reste du code.

> ⚠️ **Attention** — Les interactions entre code instrumenté et code non instrumenté peuvent produire des faux positifs. ASan ne voit pas les écritures faites par le code non instrumenté, et peut considérer que la mémoire écrite par `utils.o` est encore non initialisée. Utilisez cette technique avec discernement et vérifiez les résultats par recoupement.

---

## Lire un rapport ASan sans symboles

En situation réelle de RE, on recompile parfois les sources reconstruites **sans `-g`** (par choix ou par nécessité — les sources reconstruites ne compilent pas toujours proprement avec les symboles). Les rapports ASan contiennent alors uniquement des adresses.

```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000040
READ of size 1 at 0x602000000040 thread T0
    #0 0x401c7e (/path/to/mon_binaire+0x1c7e)
    #1 0x401da4 (/path/to/mon_binaire+0x1da4)
    #2 0x4012e7 (/path/to/mon_binaire+0x12e7)
```

Les adresses entre parenthèses (`+0x1c7e`) sont les offsets depuis la base du binaire. On peut les corréler avec le désassemblage :

```bash
# Retrouver la fonction contenant l'adresse
objdump -d mon_binaire | grep -B 20 "401c7e"

# Ou utiliser addr2line si le binaire a des symboles minimaux
addr2line -e mon_binaire 0x401c7e
```

La méthode est identique à celle décrite en section 14.1 pour Memcheck : on note les adresses, on les retrouve dans Ghidra ou objdump, et on enrichit progressivement notre compréhension du binaire.

---

## Résumé comparatif des trois sanitizers

| Aspect | ASan | UBSan | MSan |  
|---|---|---|---|  
| **Cible** | Erreurs mémoire | Comportements indéfinis | Lectures non initialisées |  
| **Compilateur** | GCC + Clang | GCC + Clang | Clang uniquement |  
| **Ralentissement** | ~2x | ~1.1x | ~3x |  
| **Mémoire supplémentaire** | ~3x | Négligeable | ~3x |  
| **Compatible avec ASan** | — | Oui | Non |  
| **Info RE clé** | Tailles de buffers, layout de structures, cycle de vie des allocations | Opérations arithmétiques, logique de hash/crypto, bugs d'optimisation | Flux de données, propagation de valeurs, taint analysis rudimentaire |  
| **Quand l'utiliser en RE** | Toujours en premier | Quand on suspecte des calculs d'entiers (crypto, hash, checksums) | Quand on cherche le flux de données sensibles (clés, IV, secrets) |

---

## Les sanitizers comme passerelle vers le fuzzing

Les sanitizers prennent toute leur dimension quand ils sont couplés au **fuzzing** (chapitre 15). Un fuzzer comme AFL++ génère des milliers d'inputs par seconde et les exécute contre le binaire instrumenté. Les sanitizers détectent les erreurs déclenchées par ces inputs et produisent des rapports pour chaque crash.

La combinaison fuzzer + ASan est le standard de l'industrie pour la recherche de vulnérabilités. En RE, elle permet de **découvrir des chemins de code** qu'on n'aurait jamais exercés manuellement et de **caractériser le comportement du parseur** sur des inputs malformés.

Nous approfondirons cette combinaison au chapitre 15. Pour l'instant, retenez que les sanitizers ne sont pas seulement des outils d'analyse ponctuelle — ils sont le **détecteur** qui donne de la valeur aux inputs générés par le fuzzer.

---


⏭️ [Exploiter les rapports de sanitizers pour comprendre la logique interne](/14-valgrind-sanitizers/04-exploiter-rapports.md)
