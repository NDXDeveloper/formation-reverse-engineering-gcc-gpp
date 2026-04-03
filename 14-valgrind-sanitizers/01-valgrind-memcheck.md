🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 14.1 — Valgrind / Memcheck — fuites mémoire et comportement runtime

> 🎯 **Objectif de cette section** : Maîtriser l'utilisation de Valgrind Memcheck sur un binaire cible pour extraire des informations structurelles exploitables en RE — tailles des allocations, durée de vie des buffers, flux de données sensibles — à partir des rapports d'erreurs mémoire et de fuites.

---

## Qu'est-ce que Valgrind ?

Valgrind est un framework d'**instrumentation binaire dynamique** pour Linux. Concrètement, il intercepte l'exécution d'un programme instruction par instruction, sans le modifier sur le disque, et y injecte du code d'analyse à la volée. Le programme cible s'exécute dans une sorte de machine virtuelle logicielle contrôlée par Valgrind.

Le point fondamental pour nous : **Valgrind ne nécessite aucune recompilation ni modification du binaire**. Il fonctionne sur n'importe quel exécutable ELF — strippé, optimisé, sans symboles. C'est ce qui en fait un outil de RE à part entière et pas uniquement un outil de développement.

Valgrind est en réalité une suite d'outils (appelés « tools ») partageant le même moteur d'instrumentation. Les principaux sont :

- **Memcheck** (outil par défaut) — détecte les erreurs d'accès mémoire, les fuites, les lectures non initialisées.  
- **Callgrind** — profileur d'appels et d'instructions (couvert en section 14.2).  
- **Massif** — profileur d'utilisation du tas (heap).  
- **Helgrind** / **DRD** — détecteurs de race conditions dans les programmes multi-threadés.

Dans cette section, nous nous concentrons sur **Memcheck**, l'outil le plus utile en contexte de RE.

---

## Comment Memcheck instrumente l'exécution

Pour comprendre ce que Memcheck peut nous apprendre et quelles sont ses limites, il est utile de savoir comment il fonctionne sous le capot.

### Le modèle de shadow memory

Memcheck maintient une **copie ombre** (shadow memory) de chaque octet de mémoire utilisé par le programme. Pour chaque octet réel, Memcheck stocke deux informations :

- **Bit d'adressabilité** (A-bit) : cet octet est-il dans une zone valide ? A-t-il été alloué par `malloc`, fait-il partie de la pile active, ou appartient-il à un segment mappé ? Un accès à un octet dont l'A-bit est « invalide » déclenche une erreur *Invalid read* ou *Invalid write*.  
- **Bit de définition** (V-bit) : cet octet a-t-il été initialisé ? A-t-on écrit une valeur dedans depuis son allocation ? Une utilisation d'un octet dont le V-bit est « non défini » dans une condition, un appel système ou une opération d'E/S déclenche une erreur *Conditional jump or move depends on uninitialised value(s)* ou *Syscall param ... contains uninitialised byte(s)*.

### Ce que cela signifie pour le RE

Ce modèle de shadow memory implique que Memcheck traque **chaque octet manipulé par le programme**, de son allocation à sa libération. En lisant les rapports, on obtient :

- Les **tailles exactes de chaque allocation dynamique** — quand Memcheck rapporte une fuite de 32 octets alloués à l'adresse `0x5204a0`, on sait qu'il existe une structure de 32 octets dans le programme.  
- Le **moment précis où une donnée est écrite puis lue** — les erreurs de lecture non initialisée nous indiquent les buffers qui sont alloués mais pas encore remplis, ce qui est caractéristique des buffers de clés cryptographiques ou de tampons de réception réseau.  
- Les **dépassements de buffers** — un *Invalid read of size 4* juste après la fin d'un bloc de 64 octets nous dit que le programme indexe un tableau de 64 octets et dépasse sa limite.

### Le coût de l'instrumentation

Memcheck ralentit l'exécution d'un facteur **10 à 50x** environ. Cela signifie que sur un binaire qui s'exécute en une seconde, l'analyse prendra entre 10 et 50 secondes. Pour la plupart des binaires d'entraînement de cette formation, c'est parfaitement acceptable. Pour des programmes longs ou interactifs, il faudra parfois adapter la stratégie (limiter les inputs, automatiser les interactions).

---

## Lancement de base

La commande fondamentale pour analyser un binaire avec Memcheck :

```bash
valgrind ./mon_binaire arg1 arg2
```

Memcheck est l'outil par défaut, il n'est donc pas nécessaire de le spécifier explicitement. Cependant, pour la clarté, on peut écrire :

```bash
valgrind --tool=memcheck ./mon_binaire arg1 arg2
```

Le programme s'exécute normalement (entrées/sorties, interactions utilisateur), mais l'ensemble de l'activité mémoire est instrumentée. À la fin de l'exécution, Memcheck affiche un résumé sur `stderr`.

### Options essentielles pour le RE

Voici la ligne de commande que nous utiliserons systématiquement dans cette formation :

```bash
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file=valgrind_report.txt \
    ./mon_binaire arg1 arg2
```

Détaillons chaque option :

**`--leak-check=full`** — Active le rapport détaillé des fuites mémoire. Sans cette option, Memcheck ne donne qu'un résumé global (nombre total d'octets perdus). Avec `full`, on obtient pour chaque fuite : la taille du bloc, l'adresse, et la pile d'appels au moment de l'allocation. C'est cette pile d'appels qui nous intéresse en RE : elle révèle quelle fonction a alloué le buffer.

**`--show-leak-kinds=all`** — Par défaut, Memcheck ne montre que les fuites « definitely lost » et « possibly lost ». En RE, les fuites « still reachable » (blocs encore accessibles à la sortie du programme mais jamais libérés) sont tout aussi intéressantes : elles correspondent souvent à des structures globales persistantes comme des tables de configuration, des caches, ou des contextes cryptographiques.

**`--track-origins=yes`** — Quand Memcheck détecte l'utilisation d'une valeur non initialisée, cette option remonte à l'**origine** de la non-initialisation (l'allocation du bloc ou la déclaration de la variable). Cela coûte un peu plus de mémoire et de temps, mais c'est indispensable pour comprendre le flux de données. Sans cette option, on sait qu'une valeur non initialisée est utilisée, mais on ne sait pas d'où elle vient.

**`--verbose`** — Affiche des informations supplémentaires sur le processus d'analyse, les bibliothèques chargées, et les statistiques d'instrumentation.

**`--log-file=valgrind_report.txt`** — Redirige la sortie de Valgrind dans un fichier. C'est important car Memcheck écrit sur `stderr`, qui se mélange avec la sortie d'erreur du programme cible. Avec un fichier dédié, on sépare proprement les deux flux et on peut analyser le rapport à tête reposée.

---

## Anatomie d'un rapport Memcheck

Un rapport Memcheck se compose de trois grandes parties : les erreurs détectées pendant l'exécution, le résumé des fuites à la sortie, et les statistiques globales. Examinons chacune en détail.

### En-tête du rapport

```
==12345== Memcheck, a memory error detector
==12345== Copyright (C) 2002-2024, and GNU GPL'd, by Julian Seward et al.
==12345== Using Valgrind-3.22.0 and LibVEX; rerun with -h for copyright info
==12345== Command: ./ch14-crypto encrypt secret.txt output.enc
==12345== Parent PID: 6789
```

Le nombre `12345` est le PID du processus analysé. Il préfixe chaque ligne du rapport, ce qui permet de démêler les sorties si on analyse plusieurs processus (par exemple un fork). La ligne `Command:` rappelle exactement la commande lancée — utile quand on analyse plusieurs variantes d'un même binaire.

### Erreurs d'accès mémoire

Ce sont les erreurs signalées **pendant l'exécution**, au moment où elles se produisent.

#### Invalid read / Invalid write

```
==12345== Invalid read of size 4                          ← taille de la lecture
==12345==    at 0x401A3F: ??? (in ./ch24-crypto)          ← adresse de l'instruction fautive
==12345==    by 0x401B12: ??? (in ./ch24-crypto)          ← appelant
==12345==    by 0x4012E8: ??? (in ./ch24-crypto)          ← appelant de l'appelant
==12345==    by 0x7FEDC3: (below main) (libc-start.c:308)
==12345==  Address 0x5204a40 is 0 bytes after a block of size 64 alloc'd
==12345==    at 0x4C2FB0F: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==12345==    by 0x40198A: ??? (in ./ch24-crypto)
==12345==    by 0x4012E8: ??? (in ./ch24-crypto)
```

Décortiquons ce rapport du point de vue RE :

- **`Invalid read of size 4`** — Le programme tente de lire 4 octets à une adresse invalide. La taille `4` nous indique probablement un accès à un `int` ou un `uint32_t`.  
- **`at 0x401A3F`** — L'adresse de l'instruction qui effectue la lecture. On peut la retrouver dans Ghidra, objdump ou GDB pour identifier précisément l'instruction.  
- **La pile d'appels** (`by 0x401B12`, `by 0x4012E8`) — Sans symboles, on voit `???`, mais les adresses sont exploitables. On peut les croiser avec le désassemblage pour reconstruire le chemin d'appel.  
- **`Address 0x5204a40 is 0 bytes after a block of size 64 alloc'd`** — C'est la ligne la plus précieuse. Elle nous dit que l'adresse lue se situe **immédiatement après** un bloc de 64 octets alloué par `malloc`. Autrement dit, le programme accède à `buffer[64]` sur un buffer de taille 64, soit un **off-by-one** ou un **débordement de tableau**.

> 💡 **Astuce RE** — La mention « 0 bytes after a block of size N » est un indicateur fiable de la taille réelle d'une structure allouée dynamiquement. Notez cette taille : elle vous aidera à reconstruire le `struct` correspondant dans Ghidra.

- **La pile d'allocation** (`at 0x4C2FB0F: malloc`, `by 0x40198A`) — On sait que la fonction à l'adresse `0x40198A` est celle qui a alloué ce bloc de 64 octets. En RE, c'est souvent une fonction d'initialisation ou un constructeur.

#### Conditional jump depends on uninitialised value

```
==12345== Conditional jump or move depends on uninitialised value(s)
==12345==    at 0x401C7E: ??? (in ./ch24-crypto)
==12345==    by 0x401D45: ??? (in ./ch24-crypto)
==12345==    by 0x4012E8: ??? (in ./ch24-crypto)
==12345==  Uninitialised value was created by a heap allocation
==12345==    at 0x4C2FB0F: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==12345==    by 0x401B89: ??? (in ./ch24-crypto)
```

Ce type d'erreur est particulièrement intéressant en RE crypto. Il nous dit qu'une **décision de branchement** dans le programme dépend d'une donnée qui n'a pas encore été initialisée. En contexte cryptographique, cela peut indiquer :

- Un buffer de clé alloué mais pas encore rempli au moment où le programme tente de l'utiliser dans un calcul.  
- Un IV (vecteur d'initialisation) non initialisé — ce qui constitue à la fois un bug de sécurité et un indicateur structurel pour le RE.  
- Un état interne d'un PRNG (générateur de nombres pseudo-aléatoires) qui utilise de la mémoire non initialisée comme source d'entropie (mauvaise pratique, mais fréquente dans du code amateur).

> 💡 **Astuce RE** — Avec `--track-origins=yes`, la ligne « Uninitialised value was created by a heap allocation at ... » vous donne l'adresse de la fonction qui a alloué le buffer. Corrélée avec l'adresse de l'utilisation, vous pouvez tracer le flux de données entre allocation et premier usage — c'est une forme de taint analysis rudimentaire mais efficace.

#### Syscall param contains uninitialised byte(s)

```
==12345== Syscall param write(buf) points to uninitialised byte(s)
==12345==    at 0x4F4E810: write (write.c:27)
==12345==    by 0x401E23: ??? (in ./ch24-crypto)
==12345==    by 0x4012E8: ??? (in ./ch24-crypto)
==12345==  Address 0x5205040 is 8 bytes inside a block of size 128 alloc'd
==12345==    at 0x4C2FB0F: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==12345==    by 0x401DA1: ??? (in ./ch24-crypto)
```

Ce rapport nous dit que le programme passe à l'appel système `write()` un buffer contenant des octets non initialisés. Les informations exploitables :

- Le buffer fait **128 octets** (taille du bloc alloué).  
- Les octets non initialisés commencent **8 octets** à l'intérieur du bloc — les 8 premiers octets sont donc initialisés. Cela ressemble fortement à un **header de 8 octets** suivi d'un payload partiellement non initialisé.  
- La fonction à `0x401E23` est celle qui écrit les données vers un descripteur de fichier, probablement une fonction d'envoi réseau ou d'écriture de fichier chiffré.  
- La fonction à `0x401DA1` est celle qui a alloué le buffer de 128 octets.

> 💡 **Astuce RE** — Quand Memcheck rapporte « Address 0x... is N bytes inside a block of size M », le couple (N, M) vous donne l'offset et la taille du buffer. Si vous observez que N vaut 8, 16 ou 32, c'est souvent un header de structure suivi d'un payload.

### Le résumé des fuites (Leak Summary)

À la fin de l'exécution, Memcheck affiche un résumé de tous les blocs alloués qui n'ont pas été libérés :

```
==12345== HEAP SUMMARY:
==12345==     in use at exit: 2,160 bytes in 5 blocks
==12345==   total heap usage: 23 allocs, 18 frees, 4,832 bytes allocated
==12345==
==12345== 32 bytes in 1 blocks are definitely lost in loss record 1 of 5
==12345==    at 0x4C2FB0F: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==12345==    by 0x401B89: ??? (in ./ch24-crypto)
==12345==    by 0x401C12: ??? (in ./ch24-crypto)
==12345==    by 0x4012E8: ??? (in ./ch24-crypto)
==12345==
==12345== 64 bytes in 1 blocks are definitely lost in loss record 2 of 5
==12345==    at 0x4C2FB0F: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==12345==    by 0x401A20: ??? (in ./ch24-crypto)
==12345==    by 0x401B89: ??? (in ./ch24-crypto)
==12345==    by 0x4012E8: ??? (in ./ch24-crypto)
==12345==
==12345== 240 bytes in 1 blocks are still reachable in loss record 3 of 5
==12345==    at 0x4C2FB0F: malloc (in /usr/libexec/valgrind/vgpreload_memcheck-amd64-linux.so)
==12345==    by 0x401C30: ??? (in ./ch24-crypto)
==12345==    by 0x4012E8: ??? (in ./ch24-crypto)
==12345==
==12345== LEAK SUMMARY:
==12345==    definitely lost: 96 bytes in 2 blocks
==12345==    indirectly lost: 0 bytes in 0 blocks
==12345==      possibly lost: 0 bytes in 0 blocks
==12345==    still reachable: 1,528 bytes in 3 blocks
==12345==         suppressed: 0 bytes in 0 blocks
```

#### Les catégories de fuites et leur signification en RE

**Definitely lost** — Blocs dont le pointeur a été perdu : plus aucune variable du programme ne pointe vers eux. En RE, ces blocs correspondent souvent à des allocations temporaires (buffers intermédiaires de calcul, buffers de conversion, résultats de parsing) que le développeur a oublié de libérer. La taille de ces blocs est un indicateur direct de la taille des structures temporaires du programme.

**Indirectly lost** — Blocs accessibles uniquement via un bloc « definitely lost ». Par exemple, si une structure contient un pointeur vers un buffer, et que la structure elle-même est perdue, le buffer est « indirectly lost ». En RE, cela révèle des **structures imbriquées** — une structure parent contenant des pointeurs vers des sous-structures.

**Possibly lost** — Blocs dont Memcheck n'est pas certain qu'ils soient perdus — typiquement quand un pointeur pointe vers le milieu d'un bloc plutôt que vers son début. En C++, cela arrive avec les pointeurs intérieurs (pointeur vers un membre d'un objet alloué). En RE, c'est un indicateur de manipulation de structures avec décalage (accès via offset plutôt que via le pointeur de base).

**Still reachable** — Blocs encore pointés par des variables au moment de la sortie du programme, mais jamais libérés. C'est techniquement une fuite, mais souvent intentionnelle (allocations globales libérées implicitement à la sortie). En RE, ces blocs sont particulièrement intéressants car ils correspondent aux **structures persistantes** du programme : contextes crypto, tables de configuration, caches, état global.

> 💡 **Astuce RE** — La ligne « total heap usage: 23 allocs, 18 frees, 4,832 bytes allocated » donne un profil de la gestion mémoire du programme. Si le nombre d'allocations est très élevé par rapport à la taille du programme, il utilise probablement des structures dynamiques (listes chaînées, arbres, maps). Si au contraire il y a peu d'allocations de grande taille, il préalloue des buffers fixes.

---

## Lecture des adresses et corrélation avec le désassemblage

Sur un binaire strippé, Memcheck affiche `???` à la place des noms de fonctions. Les adresses brutes restent néanmoins exploitables. Voici la méthode pour les corréler avec le désassemblage.

### Étape 1 — Noter les adresses clés du rapport

Dans l'exemple précédent, les adresses intéressantes sont :

- `0x401B89` — alloue un bloc de 32 octets  
- `0x401DA1` — alloue un bloc de 128 octets  
- `0x4019F0` — alloue un bloc de 1024 octets  
- `0x401A3F` — effectue une lecture invalide de 4 octets  
- `0x401C7E` — utilise une valeur non initialisée dans un branchement

### Étape 2 — Retrouver ces adresses dans le désassemblage

Avec `objdump` :

```bash
objdump -d -M intel ./ch24-crypto | grep -A 5 "401b89"
```

Ou dans Ghidra, en utilisant `G` (Go to Address) et en saisissant `0x401b89`.

L'instruction à cette adresse sera typiquement un `call` vers `malloc@plt` ou l'instruction juste après (l'adresse de retour). En remontant dans la fonction, on peut identifier la logique d'allocation : quels calculs déterminent la taille passée à `malloc`, d'où vient cette taille, etc.

### Étape 3 — Combiner avec GDB

On peut poser un breakpoint à l'adresse signalée par Valgrind pour inspecter l'état du programme au moment de l'erreur :

```bash
gdb ./ch24-crypto
(gdb) break *0x401A3F
(gdb) run encrypt secret.txt output.enc
```

> ⚠️ **Attention** — On ne peut pas exécuter GDB *à l'intérieur* de Valgrind (ou inversement) de manière triviale. L'approche est séquentielle : on lance d'abord Valgrind pour obtenir les adresses intéressantes, puis on utilise GDB séparément pour inspecter ces adresses. Cependant, Valgrind offre un serveur GDB intégré via `--vgdb=yes` qui permet de s'attacher avec GDB à un programme en cours d'analyse Valgrind (voir l'encadré ci-dessous).

### Le serveur GDB intégré de Valgrind

Valgrind dispose d'une fonctionnalité puissante mais peu connue : un **serveur GDB intégré** qui permet de déboguer le programme *pendant* son exécution sous Valgrind. Cela combine le meilleur des deux mondes : l'instrumentation mémoire de Memcheck et le contrôle interactif de GDB.

```bash
# Terminal 1 : lancer Valgrind avec le serveur GDB activé
valgrind --vgdb=yes --vgdb-error=0 ./ch24-crypto encrypt secret.txt output.enc
```

L'option `--vgdb-error=0` indique à Valgrind de s'arrêter **avant la première instruction** du programme et d'attendre une connexion GDB.

```bash
# Terminal 2 : se connecter avec GDB
gdb ./ch24-crypto
(gdb) target remote | vgdb
(gdb) continue
```

À partir de là, GDB contrôle l'exécution du programme à travers Valgrind. On peut poser des breakpoints, inspecter la mémoire, et en même temps bénéficier des diagnostics de Memcheck. Quand Memcheck détecte une erreur, l'exécution s'arrête automatiquement dans GDB, permettant d'inspecter l'état exact du programme au moment de l'erreur.

> 💡 **Astuce RE** — Avec `--vgdb-error=1`, Valgrind ne s'arrête qu'à la première erreur Memcheck. C'est souvent le mode le plus pratique : on laisse le programme tourner librement jusqu'à ce que Memcheck détecte quelque chose d'intéressant, puis on bascule dans GDB pour explorer.

---

## Les fichiers de suppression

Lors de l'analyse d'un binaire avec Memcheck, vous verrez souvent des dizaines d'erreurs provenant de **bibliothèques système** (glibc, libstdc++, libcrypto, etc.) qui ne concernent pas directement le programme cible. Ces faux positifs — ou plutôt ces erreurs dans du code qui ne nous intéresse pas — polluent le rapport et compliquent l'analyse.

Valgrind utilise des **fichiers de suppression** (`.supp`) pour filtrer ces erreurs connues. Le système est livré avec des suppressions par défaut (souvent dans `/usr/lib/valgrind/default.supp`), mais elles ne couvrent pas tout.

### Générer un fichier de suppression

```bash
valgrind --gen-suppressions=all --log-file=raw_report.txt ./ch24-crypto encrypt secret.txt output.enc
```

L'option `--gen-suppressions=all` fait que Memcheck affiche, après chaque erreur, un bloc de suppression prêt à copier-coller. On extrait ensuite les suppressions qu'on veut ignorer et on les place dans un fichier `mon_projet.supp` :

```
{
   glibc_cond_uninit
   Memcheck:Cond
   obj:/usr/lib/x86_64-linux-gnu/libc.so.6
}
{
   libcrypto_reachable
   Memcheck:Leak
   match-leak-kinds: reachable
   fun:malloc
   obj:/usr/lib/x86_64-linux-gnu/libcrypto.so.*
}
```

### Utiliser un fichier de suppression

```bash
valgrind --leak-check=full --suppressions=./mon_projet.supp ./ch24-crypto encrypt secret.txt output.enc
```

> 💡 **Astuce RE** — Construisez votre fichier de suppression de manière itérative. Lors de la première exécution, identifiez les erreurs provenant des bibliothèques système et supprimez-les. À la deuxième exécution, le rapport ne contiendra plus que les erreurs du binaire cible. Ce fichier `.supp` devient un artefact réutilisable pour toutes les analyses du même type de binaire.

---

## Cas concret : analyse de `ch24-crypto` sous Memcheck

Mettons tout cela en pratique sur le binaire de chiffrement du chapitre 24. L'objectif est d'extraire des informations structurelles **avant même d'ouvrir Ghidra**.

### Lancement de l'analyse

```bash
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --log-file=ch24_valgrind.txt \
    ./ch24-crypto encrypt testfile.txt output.enc
```

On crée au préalable un fichier de test :

```bash
echo "This is a test file for Valgrind analysis" > testfile.txt
```

### Extraction méthodique des informations

Après exécution, on ouvre `ch24_valgrind.txt` et on procède à une lecture systématique. Voici le type d'informations qu'on peut en extraire et la manière de les consigner :

**1. Profil d'allocation global** — On commence par le `HEAP SUMMARY` :

```
==12345== total heap usage: 15 allocs, 12 frees, 3,456 bytes allocated
```

15 allocations pour un programme de chiffrement de fichier, c'est relativement peu. Le programme utilise probablement des buffers de taille fixe plutôt que des allocations dynamiques en boucle. Les 3 blocs non libérés (15 - 12 = 3) sont nos cibles prioritaires.

**2. Inventaire des blocs fuyants** — On note chaque bloc non libéré avec sa taille et l'adresse de la fonction d'allocation :

| Taille | Catégorie | Fonction d'allocation | Hypothèse RE |  
|--------|-----------|----------------------|--------------|  
| 32 octets | definitely lost | `0x401B89` | Clé AES-256 (256 bits = 32 octets) |  
| 16 octets | definitely lost | `0x401B45` | IV / nonce (128 bits = 16 octets) |  
| 1024 octets | still reachable | `0x4019F0` | Buffer de lecture/écriture fichier |

Les tailles de 32 et 16 octets sont immédiatement suspectes dans un programme de chiffrement : 32 octets = 256 bits (taille d'une clé AES-256) et 16 octets = 128 bits (taille d'un bloc AES, et donc d'un IV pour les modes CBC/CTR).

**3. Erreurs de lecture non initialisée** — Si Memcheck signale une utilisation de valeur non initialisée dans la fonction qui manipule le bloc de 32 octets, cela suggère que la clé est **dérivée** d'un autre buffer (par exemple par un KDF) et qu'il y a un moment où le buffer est alloué mais pas encore rempli.

**4. Construction du graphe d'appels partiel** — En collectant toutes les adresses de la pile d'appels dans les différents rapports d'erreur, on peut reconstruire un graphe d'appels partiel :

```
0x4012E8 (main ou wrapper)
├── 0x4019F0 → alloue 4096 octets (buffer fichier) — libéré dans cleanup
├── 0x401B89 → alloue 32 octets (clé probable)
├── 0x401A20 → alloue 64 octets (sel de dérivation)
├── 0x401B45 → alloue 16 octets (IV probable)
├── 0x401C30 → alloue 240 octets (contexte crypto)
├── 0x401C12 → utilise le bloc de 32 octets (expansion de clé)
├── 0x401DA1 → alloue 128 octets (buffer de sortie par bloc)
└── 0x401E23 → écrit le buffer de 128 octets (write syscall)
```

Ce graphe, obtenu **uniquement à partir du rapport Memcheck**, nous donne déjà une compréhension structurelle du programme : initialisation, allocation des buffers crypto, chiffrement, écriture du résultat.

> 💡 **Astuce RE** — Avant même d'ouvrir le désassembleur, notez ces adresses et ces tailles dans un fichier de travail. Quand vous ouvrirez le binaire dans Ghidra, vous pourrez directement renommer les fonctions : `0x401B89` → `alloc_key_buffer`, `0x401B45` → `alloc_iv_buffer`, etc. Ce pré-travail Valgrind accélère considérablement l'analyse statique.

---

## Options avancées utiles en RE

### Traquer les origines en profondeur

```bash
valgrind --track-origins=yes --expensive-definedness-checks=yes ./mon_binaire
```

L'option `--expensive-definedness-checks=yes` active des vérifications supplémentaires sur la propagation des valeurs non définies à travers les opérations arithmétiques et logiques. C'est plus lent, mais cela permet de détecter des cas subtils où une valeur non initialisée est masquée par une opération (`xor`, `and`) avant d'être utilisée — un pattern courant dans le code cryptographique.

### Suivre les descripteurs de fichiers

```bash
valgrind --track-fds=yes ./mon_binaire
```

Cette option liste tous les descripteurs de fichiers ouverts et non fermés à la sortie du programme. En RE, cela révèle les fichiers, sockets et pipes manipulés par le programme, avec les numéros de descripteurs — information directement corrélable avec les appels `read`/`write`/`send`/`recv` observés dans le désassemblage.

### Limiter l'analyse à une plage d'adresses

Memcheck instrumente l'intégralité du processus, y compris les bibliothèques partagées. On ne peut pas limiter l'instrumentation à une plage d'adresses, mais on peut filtrer le rapport a posteriori :

```bash
grep "by 0x40" ch24_valgrind.txt | sort -u
```

Cette commande extrait toutes les adresses d'appel dans le segment `.text` du binaire (typiquement en `0x40xxxx` pour un binaire non-PIE), en éliminant les doublons. On obtient ainsi la liste de toutes les fonctions du binaire impliquées dans des opérations mémoire signalées par Memcheck.

> ⚠️ **Attention** — Pour un binaire compilé en PIE (Position Independent Executable), les adresses seront randomisées à chaque exécution. Vous pouvez désactiver l'ASLR pour obtenir des adresses stables :  
> ```bash  
> setarch x86_64 -R valgrind --leak-check=full ./mon_binaire_pie  
> ```  
> L'option `-R` de `setarch` désactive la randomisation des adresses pour ce processus uniquement.

---

## Limites de Memcheck en contexte de RE

Memcheck est un outil puissant, mais il est important d'en connaître les limites pour ne pas sur-interpréter ses rapports.

**Memcheck ne détecte que les erreurs qui se produisent.** Si un chemin de code contenant un buffer overflow n'est pas exécuté lors de l'analyse, Memcheck ne le signalera pas. La couverture dépend des inputs fournis. C'est pourquoi on combinera souvent Memcheck avec le fuzzing (chapitre 15) : le fuzzer génère des inputs variés qui exercent des chemins différents, et Memcheck détecte les erreurs sur ces chemins.

**Memcheck ne détecte pas les dépassements de pile (stack overflow).** Les A-bits de la pile sont gérés de manière simplifiée. Un buffer overflow sur la pile ne sera détecté que s'il sort de la zone de pile mappée. Pour les dépassements de buffers sur la pile, les sanitizers (ASan, voir section 14.3) sont plus efficaces.

**Memcheck ne détecte pas les accès hors limites à l'intérieur d'un même bloc alloué.** Si un programme alloue un bloc de 128 octets et accède à l'octet 100 alors qu'il ne devrait accéder qu'aux 64 premiers, Memcheck ne signalera rien (l'adresse est dans un bloc valide). Seuls les accès *après* la fin du bloc ou *avant* son début sont détectés.

**Le ralentissement est significatif.** Le facteur 10-50x peut rendre impraticable l'analyse de programmes interactifs ou à longue durée d'exécution. Dans ces cas, on privilégiera des exécutions courtes avec des inputs ciblés.

**Les binaires statiquement liés posent problème.** Si le binaire inclut sa propre copie de `malloc`/`free` (lien statique avec la glibc), Memcheck ne pourra pas intercepter les allocations automatiquement. Il faudra utiliser l'option `--soname-synonyms` ou des techniques de redirection plus avancées.

---

## Résumé : ce que Memcheck nous apprend en RE

Pour conclure cette section, voici les informations concrètes qu'on peut extraire d'un rapport Memcheck et leur utilité directe en reverse engineering :

| Information Memcheck | Utilité RE |  
|---|---|  
| Taille des blocs alloués | Taille des structures / buffers du programme |  
| Adresse de la fonction d'allocation | Identification des fonctions d'initialisation |  
| Pile d'appels des allocations | Graphe d'appels partiel, sans symboles |  
| Offset des accès invalides dans un bloc | Layout des champs d'une structure |  
| Lectures non initialisées | Flux de données, buffers de clés crypto |  
| Catégories de fuites (lost/reachable) | Distinction structures temporaires vs persistantes |  
| Descripteurs de fichiers non fermés | Fichiers et sockets manipulés |  
| Profil d'allocation global | Complexité de la gestion mémoire du programme |

Memcheck est un outil de **premier passage** : il ne donne pas toutes les réponses, mais il fournit un cadre de travail — des adresses, des tailles, des relations entre fonctions — qui accélère considérablement l'analyse statique qui suit.

---


⏭️ [Callgrind + KCachegrind — profiling et graphe d'appels](/14-valgrind-sanitizers/02-callgrind-kcachegrind.md)
