🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 12.4 — Analyse de heap avec pwndbg (`vis_heap_chunks`, `bins`)

> **Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg**  
> **Partie III — Analyse Dynamique**

---

## Pourquoi analyser la heap en reverse engineering ?

La heap (le tas) est la région de mémoire où un programme alloue dynamiquement des données à l'exécution via `malloc`, `calloc`, `realloc` et `new`. En reverse engineering, la heap est omniprésente : chaînes de caractères construites dynamiquement, buffers de réception réseau, structures de données allouées à la volée, objets C++ instanciés avec `new` — tout cela réside sur le tas.

Comprendre l'état de la heap à un instant donné permet de répondre à des questions concrètes lors d'une analyse. Où le programme stocke-t-il le mot de passe saisi par l'utilisateur ? Quelle est la taille du buffer alloué pour les données réseau ? Quand un objet C++ est-il libéré, et sa mémoire est-elle réellement effacée ou le contenu persiste-t-il ? Un chiffrement custom utilise-t-il un buffer temporaire sur le tas pour stocker la clé dérivée ?

Au-delà du RE, l'analyse de la heap est fondamentale en exploitation de vulnérabilités. Les bugs de type use-after-free, double-free, heap overflow et heap corruption sont parmi les vulnérabilités les plus exploitées aujourd'hui. Les comprendre — ne serait-ce que pour les détecter lors d'un audit — exige de savoir lire l'état interne de l'allocateur.

pwndbg est l'extension qui excelle dans ce domaine. Ses commandes de heap parsent directement les structures internes de l'allocateur glibc (`ptmalloc2`) et les présentent sous une forme lisible. GEF propose des commandes similaires mais moins détaillées. PEDA n'offre pratiquement rien dans ce domaine. C'est pourquoi cette section se concentre sur pwndbg, avec des mentions de GEF quand l'équivalent existe.

---

## Rappel : comment fonctionne l'allocateur glibc (ptmalloc2)

Pour lire la sortie des commandes de heap de pwndbg, il faut comprendre les concepts de base de l'allocateur glibc. Cette section fournit le minimum nécessaire — l'étude complète de ptmalloc2 est un sujet à part entière qui dépasse le cadre de ce chapitre.

### Les chunks

L'unité fondamentale de la heap glibc est le **chunk**. Chaque appel à `malloc(n)` retourne un pointeur vers la zone de données d'un chunk. Le chunk lui-même commence quelques octets avant ce pointeur, dans un en-tête (metadata) que l'allocateur utilise pour sa gestion interne.

Sur x86-64, l'en-tête d'un chunk alloué contient deux champs de 8 octets :

- `prev_size` (8 octets) : taille du chunk précédent, uniquement si ce dernier est libre. Lorsque le chunk précédent est en usage, ce champ est recyclé comme espace de données par le chunk précédent.  
- `size` (8 octets) : taille totale du chunk courant (en-tête inclus), alignée sur 16 octets. Les 3 bits de poids faible de ce champ sont des flags : `PREV_INUSE` (bit 0), `IS_MMAPPED` (bit 1) et `NON_MAIN_ARENA` (bit 2).

Le pointeur retourné par `malloc` pointe vers les données qui suivent immédiatement ce header de 16 octets. Quand l'analyste voit une adresse retournée par `malloc`, l'en-tête du chunk se trouve 16 octets avant.

### Les chunks libérés et les bins

Lorsqu'un chunk est libéré par `free`, il n'est pas rendu au système d'exploitation — il est inséré dans une **liste de chunks libres** appelée **bin**. L'allocateur maintient plusieurs catégories de bins pour optimiser la réutilisation :

Les **tcache bins** (Thread-Local Cache, introduits dans glibc 2.26) sont des listes singly-linked par thread, indexées par taille de chunk. Chaque bin de tcache peut contenir jusqu'à 7 chunks de la même taille. C'est le premier endroit où l'allocateur cherche lors d'un nouveau `malloc` de taille correspondante. Les tcache bins sont les plus rapides mais aussi les plus simples à exploiter en cas de vulnérabilité, car ils effectuent peu de vérifications d'intégrité.

Les **fastbins** sont des listes singly-linked globales (partagées entre threads) pour les petits chunks (jusqu'à 160 octets par défaut sur x86-64). Ils fonctionnent en LIFO (Last In, First Out) : le dernier chunk libéré est le premier réalloué.

L'**unsorted bin** est une liste doubly-linked qui sert de tampon temporaire. Lorsqu'un chunk libéré ne rentre pas dans un fastbin ou un tcache, il est placé dans l'unsorted bin. Lors d'un prochain `malloc`, l'allocateur parcourt l'unsorted bin et trie les chunks dans les bins appropriés.

Les **small bins** (62 bins pour les tailles de 32 à 1008 octets) et les **large bins** (63 bins pour les tailles supérieures) sont des listes doubly-linked triées qui contiennent les chunks après leur passage par l'unsorted bin.

### L'arena et le top chunk

L'**arena** est la structure de données principale de l'allocateur. Elle contient les pointeurs vers tous les bins, le mutex de synchronisation et les métadonnées globales. Le processus principal utilise la **main arena**, et des threads supplémentaires peuvent créer des arenas secondaires.

Le **top chunk** est le chunk qui se trouve à la frontière supérieure de la heap. Lorsqu'aucun bin ne contient de chunk de taille suffisante pour satisfaire un `malloc`, l'allocateur découpe le top chunk. Si le top chunk lui-même est trop petit, la heap est étendue via `brk` ou `mmap`.

---

## Les commandes heap de pwndbg

### `heap` — vue d'ensemble

La commande `heap` (sans argument) affiche la liste de tous les chunks de la heap, dans l'ordre de leurs adresses :

```
pwndbg> heap  
Allocated chunk | PREV_INUSE  
Addr: 0x555555559000  
Size: 0x291  

Allocated chunk | PREV_INUSE  
Addr: 0x555555559290  
Size: 0x21  

Free chunk (tcachebins) | PREV_INUSE  
Addr: 0x5555555592b0  
Size: 0x31  

Allocated chunk | PREV_INUSE  
Addr: 0x5555555592e0  
Size: 0x41  

Top chunk | PREV_INUSE  
Addr: 0x555555559320  
Size: 0x20ce1  
```

Chaque entrée indique l'adresse du chunk, sa taille et son état (alloué, libre avec le type de bin, ou top chunk). Le flag `PREV_INUSE` est noté quand il est actif — ce qui est le cas normal pour un chunk dont le voisin précédent est en usage.

Cette vue permet de cartographier rapidement la heap : combien de chunks existent, quelles sont leurs tailles, lesquels sont libres. C'est souvent la première commande à taper lors de l'analyse de la heap.

### `vis_heap_chunks` — représentation visuelle

`vis_heap_chunks` (ou son alias `vis`) est la commande la plus emblématique de pwndbg pour l'analyse de la heap. Elle produit une représentation graphique des chunks en mémoire, avec une coloration par chunk qui permet de voir immédiatement les frontières, les en-têtes et les données :

```
pwndbg> vis_heap_chunks
```

La sortie ressemble à ceci (les couleurs sont représentées ici par des commentaires) :

```
0x555555559000  0x0000000000000000  0x0000000000000291  ........  ← chunk 1 header
0x555555559010  0x0000000000000000  0x0000000000000000  ........
...
0x555555559290  0x0000000000000000  0x0000000000000021  ........  ← chunk 2 header (size=0x21)
0x5555555592a0  0x00000000deadbeef  0x0000000000000000  ........  ← chunk 2 data
0x5555555592b0  0x0000000000000000  0x0000000000000031  ........  ← chunk 3 header (free, size=0x31)
0x5555555592c0  0x0000555555559010  0x0000000000000000  ........  ← chunk 3 fd pointer
...
0x555555559320  0x0000000000000000  0x0000000000020ce1  ........  ← top chunk
```

Dans un terminal avec couleurs, chaque chunk est affiché dans une couleur différente (alternance de teintes), ce qui rend les frontières entre chunks immédiatement visibles. Le champ `size` de chaque en-tête est mis en évidence. Pour les chunks libres, les pointeurs `fd` (forward) et `bk` (backward) des listes de bins sont visibles dans la zone de données.

On peut limiter l'affichage à une plage d'adresses ou à un nombre de chunks :

```
pwndbg> vis_heap_chunks 5              # les 5 premiers chunks  
pwndbg> vis_heap_chunks 0x555555559290 3  # 3 chunks à partir de cette adresse  
```

`vis_heap_chunks` est irremplaçable pour comprendre la disposition spatiale de la heap. En voyant les chunks côte à côte avec leurs tailles, l'analyste peut repérer un heap overflow (un chunk dont les données débordent dans le header du chunk suivant), un chunk corrompu (taille incohérente), ou simplement comprendre comment le programme organise ses structures en mémoire.

### `bins` — état des listes de chunks libres

La commande `bins` affiche l'état de toutes les catégories de bins de l'allocateur :

```
pwndbg> bins  
tcachebins  
0x30 [  1]: 0x5555555592c0 ◂— 0x0

fastbins  
empty  

unsortedbin  
empty  

smallbins  
empty  

largebins  
empty  
```

Cette sortie indique qu'un chunk de taille 0x30 est présent dans le tcache, et que toutes les autres catégories de bins sont vides. Le format pour chaque entrée montre la taille, le nombre de chunks entre crochets, puis la chaîne de pointeurs du bin.

Lorsque plusieurs chunks de la même taille sont libérés, la liste s'allonge :

```
tcachebins
0x30 [  3]: 0x555555559370 —▸ 0x555555559340 —▸ 0x5555555592c0 ◂— 0x0
```

La lecture se fait de gauche à droite : `0x555555559370` est la tête de la liste (le prochain chunk qui sera retourné par un `malloc(0x20)`), suivi de `0x555555559340`, puis `0x5555555592c0`. Le `0x0` final indique la fin de la liste.

pwndbg propose également des commandes spécialisées par catégorie :

```
pwndbg> tcachebins       # uniquement les tcache bins  
pwndbg> fastbins         # uniquement les fastbins  
pwndbg> unsortedbin      # uniquement l'unsorted bin  
pwndbg> smallbins        # uniquement les small bins  
pwndbg> largebins        # uniquement les large bins  
```

### `top_chunk` — le chunk frontière

```
pwndbg> top_chunk  
Top chunk  
Addr: 0x555555559320  
Size: 0x20ce1  
```

Cette commande affiche l'adresse et la taille du top chunk. La taille du top chunk diminue à chaque `malloc` qui n'est pas satisfait par un bin, et augmente lorsque la heap est étendue. Surveiller l'évolution du top chunk aide à comprendre le pattern d'allocation du programme.

### `arena` — métadonnées de l'allocateur

```
pwndbg> arena  
Arena main_arena (at 0x7ffff7e19c80)  
  Top:           0x555555559320
  Last Remainder: 0x0
  Bins:          ...
  Fastbins:      ...
```

La commande `arena` affiche les métadonnées de la main arena (ou d'une arena spécifiée). C'est utile pour vérifier que l'allocateur n'est pas dans un état corrompu et pour retrouver les pointeurs de bins manuellement si nécessaire.

### `mp_` — paramètres globaux de l'allocateur

```
pwndbg> mp_  
mp_ @ 0x7ffff7e1b280 {  
  trim_threshold   = 131072,
  top_pad          = 131072,
  mmap_threshold   = 131072,
  arena_test       = 8,
  arena_max        = 0,
  n_mmaps          = 0,
  n_mmaps_max      = 65536,
  max_n_mmaps      = 0,
  no_dyn_threshold = 0,
  mmapped_mem      = 0,
  max_mmapped_mem  = 0,
  sbrk_base        = 0x555555559000,
  tcache_bins      = 64,
  tcache_max_bytes = 1032,
  tcache_count     = 7,
  tcache_unsorted_limit = 0,
}
```

Ces paramètres gouvernent le comportement de l'allocateur : `tcache_count` confirme le nombre maximal de chunks par tcache bin (7 par défaut), `tcache_max_bytes` indique la taille maximale pour qu'un chunk soit éligible au tcache, et `mmap_threshold` est la taille au-delà de laquelle `malloc` utilise `mmap` plutôt que l'extension du tas via `brk`.

### `malloc_chunk` — inspection d'un chunk spécifique

Pour examiner en détail un chunk donné :

```
pwndbg> malloc_chunk 0x555555559290  
Allocated chunk | PREV_INUSE  
Addr: 0x555555559290  
prev_size: 0x0  
size: 0x21  
fd: 0xdeadbeef  
bk: 0x0  
```

Les champs `fd` et `bk` ne sont significatifs que pour un chunk libéré (ils forment les liens de la liste). Pour un chunk alloué, ces octets font partie des données utilisateur — ici, la valeur `0xdeadbeef` est simplement la donnée stockée par le programme.

---

## Équivalents dans GEF

GEF propose une famille de commandes `heap` qui couvrent une partie des fonctionnalités de pwndbg :

```
gef➤ heap chunks          # liste des chunks (équivalent de `heap` dans pwndbg)  
gef➤ heap bins             # état des bins (équivalent de `bins`)  
gef➤ heap bins tcache      # uniquement les tcache bins  
gef➤ heap bins fast        # uniquement les fastbins  
gef➤ heap bins unsorted    # uniquement l'unsorted bin  
gef➤ heap chunk 0x555555559290   # inspection d'un chunk spécifique  
gef➤ heap arenas           # liste des arenas  
```

GEF ne propose pas d'équivalent direct à `vis_heap_chunks` — c'est la commande la plus différenciante de pwndbg. L'affichage des chunks dans GEF est textuel et linéaire, sans la coloration par chunk qui rend la lecture visuelle si efficace. Pour une analyse de heap sérieuse, pwndbg reste l'outil de choix.

GEF compense partiellement avec la commande `heap set-arena` pour travailler avec des arenas non principales dans les programmes multi-threadés, et avec des heuristiques de détection de corruption de heap qui affichent des avertissements lorsque des incohérences sont détectées.

---

## Stratégie d'analyse de la heap en pratique

L'analyse de la heap n'est pas un exercice abstrait — elle répond à des questions concrètes. Voici une méthodologie adaptée au contexte du reverse engineering.

### Cartographier les allocations

La première étape consiste à comprendre le pattern d'allocation du programme. On pose un breakpoint après la phase d'initialisation (par exemple au début du traitement de l'entrée utilisateur) et on examine la heap :

```
pwndbg> break main  
pwndbg> run  
pwndbg> next 20          # avancer après les premières initialisations  
pwndbg> heap  
```

Cela donne une photo de la heap à un instant donné. On note les tailles des chunks : des chunks de 0x21 (16 octets de données + 16 d'en-tête, aligné) sont typiques de petites structures ou de chaînes courtes. Des chunks de 0x411 (1024 octets de données) suggèrent un buffer de lecture. Des chunks de tailles inhabituelles peuvent indiquer des structures de données complexes.

### Suivre une allocation spécifique

Pour tracer l'allocation d'un buffer précis, on pose un breakpoint sur `malloc` et on observe la taille demandée (dans `RDI`) et l'adresse retournée (dans `RAX` après le retour) :

```
pwndbg> break malloc  
pwndbg> commands  
> silent
> printf "malloc(%d) = ", $rdi
> finish
> printf "%p\n", $rax
> continue
> end
pwndbg> run
```

Ce script GDB affiche chaque appel à `malloc` avec sa taille et son résultat. En croisant ces informations avec `vis_heap_chunks`, on peut identifier quel chunk contient quelles données.

Pour une approche plus ciblée, un breakpoint conditionnel filtre sur une taille spécifique :

```
pwndbg> break malloc if $rdi == 256
```

### Observer les libérations et la réutilisation

Poser un breakpoint sur `free` révèle quand et quels chunks sont libérés :

```
pwndbg> break free  
pwndbg> commands  
> silent
> printf "free(%p)\n", $rdi
> continue
> end
```

Après une série d'allocations et de libérations, `bins` montre les chunks disponibles pour réutilisation. Si un programme libère un chunk contenant des données sensibles (clé de chiffrement, mot de passe) sans l'effacer au préalable, ces données persistent dans le chunk libre et sont visibles avec `vis_heap_chunks` — un point d'intérêt majeur pour le chapitre 24 (reverse d'un binaire avec chiffrement).

### Détecter les anomalies

`vis_heap_chunks` rend les corruptions de heap visibles. Un chunk dont le champ `size` ne s'aligne pas avec le début du chunk suivant indique une corruption. Un chunk libre dont le pointeur `fd` pointe en dehors de la heap suggère un write-after-free ou un heap overflow. pwndbg affiche des messages d'avertissement lorsqu'il détecte de telles incohérences :

```
pwndbg> heap
...
Corrupt chunk | PREV_INUSE  
Addr: 0x5555555593a0  
Size: 0x4141414141414141 (invalid)  
```

Une taille de `0x4141414141414141` est le signe classique d'un overflow ayant écrit des `'A'` (0x41) par-dessus l'en-tête du chunk — exactement le type de vulnérabilité que l'on cherche à identifier lors d'un audit.

---

## Combiner avec Frida et Valgrind

Les commandes de heap de pwndbg offrent un instantané à un point d'arrêt. Pour une vision plus dynamique, elles se complètent avec les outils vus dans les chapitres voisins.

**Frida** (chapitre 13) permet de hooker `malloc` et `free` pour loguer toutes les opérations d'allocation en continu, sans poser de breakpoints qui ralentissent l'exécution. Le script Frida peut enregistrer l'historique complet des allocations, puis on bascule dans pwndbg pour inspecter l'état résultant à un moment précis.

**Valgrind / Memcheck** (chapitre 14) détecte les fuites mémoire et les accès invalides au niveau de la heap avec un surcourant d'exécution, mais sans nécessiter de breakpoints manuels. Les rapports de Valgrind peuvent orienter l'analyse pwndbg : si Memcheck signale un use-after-free à l'adresse `0x5555555592c0`, on sait exactement quel chunk examiner avec `malloc_chunk`.

L'approche combinée — Valgrind pour le diagnostic global, pwndbg pour l'inspection détaillée, Frida pour le traçage continu — constitue un arsenal complet pour l'analyse de heap en reverse engineering.

---

## Limites et points d'attention

Les commandes de heap de pwndbg reposent sur le parsing des structures internes de la glibc. Cela implique plusieurs limitations.

**Dépendance à la version de la glibc.** Les structures internes de ptmalloc2 évoluent entre les versions de la glibc. Le tcache a été introduit en glibc 2.26. Les vérifications d'intégrité sur les pointeurs `fd` (safe-linking) ont été ajoutées en glibc 2.32. pwndbg maintient un support des différentes versions, mais un décalage est possible avec des versions très récentes ou très anciennes. La commande `heap` affiche un avertissement si elle détecte une incompatibilité.

**Allocateurs alternatifs.** Si le programme utilise un allocateur personnalisé (jemalloc, tcmalloc, mimalloc, ou un allocateur maison), les commandes de heap de pwndbg ne fonctionneront pas — elles sont spécifiques à ptmalloc2. pwndbg affiche un message d'erreur dans ce cas. L'analyse doit alors se faire manuellement en identifiant les structures de l'allocateur alternatif dans le binaire.

**Binaires statiquement liés.** Lorsque la glibc est liée statiquement, pwndbg peut avoir du mal à localiser les symboles internes de l'allocateur (`main_arena`, `mp_`). La commande `set main-arena` permet de spécifier manuellement l'adresse si pwndbg ne la trouve pas automatiquement.

**Programmes multi-threadés.** Les programmes avec plusieurs threads peuvent utiliser des arenas secondaires. Par défaut, les commandes de heap opèrent sur la main arena. Pour inspecter une arena spécifique :

```
pwndbg> arenas                    # liste toutes les arenas  
pwndbg> heap --arena 0x7ffff0000b60  # inspection d'une arena spécifique  
```

---


⏭️ [Commandes utiles spécifiques à chaque extension](/12-gdb-extensions/05-commandes-specifiques.md)
