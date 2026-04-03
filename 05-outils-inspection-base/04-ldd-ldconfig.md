🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 5.4 — `ldd` et `ldconfig` — dépendances dynamiques et résolution

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Introduction

À la section précédente, `nm -D` nous a révélé les noms des fonctions importées par le binaire — `printf`, `strcmp`, `strlen`… Mais il ne nous a pas dit **d'où** ces fonctions viennent concrètement au runtime. Quand le loader dynamique (`ld.so`) charge le programme en mémoire, il doit localiser chaque bibliothèque partagée (`.so`) contenant les symboles requis, la mapper en mémoire, et résoudre les adresses. Si une seule dépendance est introuvable ou incompatible, le programme refuse de se lancer.

Comprendre les dépendances dynamiques d'un binaire est une étape essentielle du triage pour plusieurs raisons :

- **Indice fonctionnel** : la liste des bibliothèques liées révèle les capacités du programme. Un binaire qui dépend de `libssl.so` fait de la cryptographie. Un binaire qui dépend de `libpcap.so` capture du trafic réseau. Un binaire qui dépend de `libpthread.so` est multi-threadé.  
- **Compatibilité** : un binaire compilé sur une distribution peut ne pas fonctionner sur une autre si les versions des bibliothèques diffèrent. Connaître les dépendances exactes permet de diagnostiquer ce type de problème.  
- **Surface d'attaque** : chaque bibliothèque liée est un vecteur potentiel de détournement (`LD_PRELOAD`, DLL hijacking à la Linux, remplacement de `.so`). Savoir quelles bibliothèques sont chargées et depuis quel chemin est une information de sécurité.  
- **Binaire statique vs dynamique** : un binaire sans dépendances dynamiques est lié statiquement — tout le code est embarqué. Cela change considérablement l'approche de RE (binaire plus gros, pas de symboles dynamiques, pas de PLT/GOT).

Cette section présente `ldd`, l'outil qui liste les dépendances dynamiques et leurs chemins de résolution, ainsi que `ldconfig` et le mécanisme de résolution de bibliothèques sous Linux.

---

## `ldd` — lister les dépendances dynamiques

### Principe de fonctionnement

`ldd` affiche la liste des bibliothèques partagées dont dépend un binaire, ainsi que le **chemin complet** vers le fichier `.so` qui sera effectivement chargé au runtime. Il fonctionne en invoquant le loader dynamique (`ld.so`) avec des variables d'environnement spéciales qui lui demandent d'afficher les résolutions sans réellement exécuter le programme.

### Utilisation de base

```bash
$ ldd keygenme_O0
	linux-vdso.so.1 (0x00007ffcabffe000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f2a3c600000)
	/lib64/ld-linux-x86-64.so.2 (0x00007f2a3c8f2000)
```

Chaque ligne suit le format : **nom de la bibliothèque** `=>` **chemin résolu** `(adresse de chargement)`. Décortiquons les trois entrées :

**`linux-vdso.so.1`** — le *Virtual Dynamic Shared Object*. Ce n'est pas un fichier sur disque — c'est une page mémoire injectée directement par le noyau Linux dans l'espace d'adressage de chaque processus. Elle contient des implémentations optimisées de certains appels système fréquents (`gettimeofday`, `clock_gettime`, `getcpu`) qui peuvent s'exécuter en espace utilisateur sans le coût d'un passage en mode noyau. Le vDSO n'a pas de chemin sur le système de fichiers, c'est pourquoi il n'y a pas de `=>` suivi d'un chemin. L'adresse entre parenthèses varie à chaque exécution à cause de l'ASLR.

**`libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6`** — la bibliothèque C standard GNU (glibc). C'est ici que résident `printf`, `strcmp`, `strlen`, `malloc`, et des centaines d'autres fonctions. Le `=>` indique que le nom `libc.so.6` a été résolu vers le fichier `/lib/x86_64-linux-gnu/libc.so.6` sur le système. Ce chemin dépend de la distribution : sur Arch Linux, ce serait `/usr/lib/libc.so.6`, sur CentOS `/lib64/libc.so.6`. L'adresse entre parenthèses est l'adresse de base à laquelle la bibliothèque serait chargée.

**`/lib64/ld-linux-x86-64.so.2`** — le loader dynamique lui-même. Il est listé car il fait techniquement partie des dépendances du processus (c'est lui qui est invoqué en premier pour charger tout le reste). Son chemin est absolu et correspond au champ `INTERP` vu dans les program headers avec `readelf -l`.

### Un binaire avec davantage de dépendances

Sur un programme plus complexe, la liste est plus longue et révèle les fonctionnalités du programme :

```bash
$ ldd /usr/bin/curl
	linux-vdso.so.1 (0x00007fff6c5fc000)
	libcurl.so.4 => /lib/x86_64-linux-gnu/libcurl.so.4 (0x00007f3e8c400000)
	libz.so.1 => /lib/x86_64-linux-gnu/libz.so.1 (0x00007f3e8c3e0000)
	libpthread.so.0 => /lib/x86_64-linux-gnu/libpthread.so.0 (0x00007f3e8c3d8000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f3e8c200000)
	libssl.so.3 => /lib/x86_64-linux-gnu/libssl.so.3 (0x00007f3e8c150000)
	libcrypto.so.3 => /lib/x86_64-linux-gnu/libcrypto.so.3 (0x00007f3e8bd00000)
	libnghttp2.so.14 => /lib/x86_64-linux-gnu/libnghttp2.so.14 (0x00007f3e8bcd0000)
	[...]
	/lib64/ld-linux-x86-64.so.2 (0x00007f3e8c6a0000)
```

Sans même connaître `curl`, cette sortie nous apprend que le programme utilise des transferts réseau (`libcurl`), de la compression (`libz`), du multi-threading (`libpthread`), du TLS/SSL (`libssl`, `libcrypto`), et du HTTP/2 (`libnghttp2`). Chaque bibliothèque est une piste d'investigation pour le RE.

### Cas d'un binaire statiquement lié

```bash
$ ldd binaire_statique
	not a dynamic executable
```

Ce message indique que le binaire n'a aucune dépendance dynamique — tout le code est embarqué dans l'exécutable. On peut le confirmer avec `file` :

```bash
$ file binaire_statique
binaire_statique: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), statically linked, [...]
```

Un binaire statiquement lié est significativement plus gros (souvent plusieurs mégaoctets même pour un programme simple) car il embarque une copie de toutes les fonctions libc qu'il utilise. Pour le RE, cela signifie :

- Pas de section `.dynsym` — les noms de fonctions importées sont absents.  
- Pas de PLT/GOT — les appels de fonctions sont directs.  
- Les fonctions de la libc sont inlinées dans le binaire et peuvent être difficiles à identifier sans signatures (FLIRT dans IDA, function ID dans Ghidra — voir chapitre 20, section 20.5).

### Dépendances transitives

`ldd` ne se contente pas de lister les dépendances **directes** du binaire (celles déclarées par les entrées `NEEDED` dans la section `.dynamic`). Il résout aussi les dépendances **transitives** — les bibliothèques dont les bibliothèques du programme dépendent elles-mêmes.

Par exemple, si votre binaire dépend de `libcurl.so.4`, et que `libcurl.so.4` dépend de `libssl.so.3`, `libz.so.1`, etc., toutes ces dépendances indirectes apparaîtront dans la sortie de `ldd`. Pour distinguer les dépendances directes des transitives, comparez avec `readelf -d` :

```bash
# Dépendances directes uniquement (depuis la section .dynamic)
$ readelf -d keygenme_O0 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]

# Toutes les dépendances (directes + transitives)
$ ldd keygenme_O0
	linux-vdso.so.1 (0x00007ffcabffe000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f2a3c600000)
	/lib64/ld-linux-x86-64.so.2 (0x00007f2a3c8f2000)
```

Dans ce cas simple, la seule dépendance directe est `libc.so.6`. Les deux autres (`linux-vdso.so.1` et `ld-linux-x86-64.so.2`) sont des composants système toujours présents. Sur un binaire plus complexe, la différence entre dépendances directes et transitives peut être significative.

### Dépendances manquantes

Si une bibliothèque est introuvable, `ldd` le signale clairement :

```bash
$ ldd binaire_avec_dep_manquante
	linux-vdso.so.1 (0x00007ffce93f8000)
	libcustom_crypto.so.1 => not found
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f8a1c600000)
	/lib64/ld-linux-x86-64.so.2 (0x00007f8a1c8f0000)
```

`not found` indique que le loader ne parvient pas à trouver `libcustom_crypto.so.1`. Le programme refusera de se lancer tant que cette dépendance ne sera pas satisfaite. Pour le RE, une dépendance manquante est un indice : elle peut correspondre à une bibliothèque propriétaire, à un composant déployé séparément, ou à un artefact de l'environnement de compilation.

### Avertissement de sécurité sur `ldd`

> ⚠️ **`ldd` n'est pas sans risque sur un binaire non fiable.**

`ldd` fonctionne en invoquant le loader dynamique sur le binaire cible. Sur certains systèmes et dans certaines configurations, cela peut entraîner l'exécution partielle du code du binaire — en particulier les constructeurs définis dans les sections `.init` et `.init_array`, qui s'exécutent avant `main()`.

Un binaire malveillant pourrait exploiter ce comportement pour exécuter du code simplement en étant analysé avec `ldd`. C'est un risque connu et documenté.

**Alternatives sûres** pour lister les dépendances sans risque d'exécution :

```bash
# Méthode 1 : readelf -d (parse uniquement les headers, n'exécute rien)
$ readelf -d binaire_suspect | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]

# Méthode 2 : objdump -p (même principe, parsing seul)
$ objdump -p binaire_suspect | grep NEEDED
  NEEDED               libc.so.6
```

Ces deux méthodes ne listent que les dépendances **directes** (sans résolution de chemin ni dépendances transitives), mais elles sont complètement sûres car elles se contentent de parser les headers ELF sans jamais invoquer le loader.

En environnement d'analyse de malware (chapitre 26), **n'utilisez jamais `ldd` directement sur un binaire suspect**. Préférez `readelf -d` ou exécutez `ldd` dans une sandbox jetable.

---

## Le mécanisme de résolution des bibliothèques sous Linux

Quand le loader dynamique (`ld-linux-x86-64.so.2`) reçoit une dépendance comme `libc.so.6`, il doit trouver le fichier `.so` correspondant sur le système de fichiers. Il suit un algorithme de recherche précis, dans l'ordre suivant :

### 1. `DT_RPATH` / `DT_RUNPATH` (encodé dans le binaire)

Le binaire peut contenir des chemins de recherche compilés directement dans sa section `.dynamic`. Ces chemins sont ajoutés à la compilation avec les options `-rpath` ou `-runpath` de `ld` :

```bash
$ readelf -d binaire | grep -E 'RPATH|RUNPATH'
 0x000000000000001d (RUNPATH)            Library runpath: [/opt/myapp/lib]
```

Si présent, le loader cherchera les `.so` dans `/opt/myapp/lib` avant tout autre emplacement. C'est fréquent dans les logiciels commerciaux qui embarquent leurs propres bibliothèques.

### 2. `LD_LIBRARY_PATH` (variable d'environnement)

Si la variable d'environnement `LD_LIBRARY_PATH` est définie, ses répertoires sont consultés ensuite :

```bash
$ export LD_LIBRARY_PATH=/home/user/custom_libs:/opt/libs
$ ldd keygenme_O0
# Le loader cherchera d'abord dans /home/user/custom_libs, puis /opt/libs
```

Pour le RE, `LD_LIBRARY_PATH` est un outil d'interposition : on peut forcer le chargement d'une version modifiée d'une bibliothèque en la plaçant dans un répertoire prioritaire. C'est une technique proche de `LD_PRELOAD` (chapitre 22, section 22.4).

### 3. Le cache `ldconfig` (`/etc/ld.so.cache`)

C'est le mécanisme principal de résolution sur un système standard. Le fichier `/etc/ld.so.cache` est un cache binaire qui associe les noms de bibliothèques à leurs chemins complets. Il est généré par la commande `ldconfig`.

### 4. Les répertoires par défaut

En dernier recours, le loader cherche dans les répertoires système standard : `/lib`, `/usr/lib`, et sur les systèmes 64 bits, `/lib64`, `/usr/lib64`, ainsi que les répertoires multi-arch comme `/lib/x86_64-linux-gnu` sur Debian/Ubuntu.

---

## `ldconfig` — gérer le cache des bibliothèques

### Rôle de `ldconfig`

`ldconfig` est l'outil d'administration qui maintient le cache `/etc/ld.so.cache`. Son rôle est double :

1. **Scanner les répertoires** listés dans `/etc/ld.so.conf` (et ses fichiers inclus depuis `/etc/ld.so.conf.d/`) pour inventorier toutes les bibliothèques partagées disponibles.  
2. **Créer et mettre à jour les liens symboliques** de versioning (par exemple, `libssl.so.3` → `libssl.so.3.0.12`).

### Lister le cache actuel

```bash
$ ldconfig -p | head -20
1847 libs found in cache `/etc/ld.so.cache'
	libz.so.1 (libc6,x86-64) => /lib/x86_64-linux-gnu/libz.so.1
	libxtables.so.12 (libc6,x86-64) => /lib/x86_64-linux-gnu/libxtables.so.12
	libxml2.so.2 (libc6,x86-64) => /lib/x86_64-linux-gnu/libxml2.so.2
	[...]
```

L'option `-p` (ou `--print-cache`) affiche le contenu du cache sous forme lisible. Chaque entrée montre le nom de la bibliothèque, son architecture (`libc6,x86-64`), et son chemin résolu.

### Chercher une bibliothèque spécifique

```bash
# Où se trouve libssl ?
$ ldconfig -p | grep libssl
	libssl.so.3 (libc6,x86-64) => /lib/x86_64-linux-gnu/libssl.so.3

# Quelles bibliothèques crypto sont disponibles ?
$ ldconfig -p | grep -iE '(crypto|ssl|gnutls)'
	libssl.so.3 (libc6,x86-64) => /lib/x86_64-linux-gnu/libssl.so.3
	libgnutls.so.30 (libc6,x86-64) => /lib/x86_64-linux-gnu/libgnutls.so.30
	libcrypto.so.3 (libc6,x86-64) => /lib/x86_64-linux-gnu/libcrypto.so.3

# Combien de bibliothèques au total ?
$ ldconfig -p | head -1
1847 libs found in cache `/etc/ld.so.cache'
```

### Utilité pour le RE

`ldconfig -p` est utile dans deux situations :

**Vérifier la disponibilité d'une dépendance** — quand `ldd` signale `not found`, on peut vérifier si la bibliothèque existe dans le cache, si elle est présente sous un nom légèrement différent (problème de version), ou si elle est totalement absente du système.

**Identifier la version exacte d'une bibliothèque** — pour un reverse approfondi, il est parfois nécessaire d'analyser le code de la bibliothèque elle-même (par exemple, pour comprendre le comportement exact de la fonction de chiffrement appelée par le binaire). `ldconfig -p` donne le chemin exact du fichier `.so`, sur lequel on peut ensuite utiliser tous les outils vus dans ce chapitre.

---

## `LD_PRELOAD` — un mot sur l'interposition de bibliothèques

Bien que le chapitre 22 (section 22.4) soit consacré au patching via `LD_PRELOAD`, il est utile d'en comprendre le principe dès maintenant, car il est intimement lié au mécanisme de résolution des bibliothèques.

La variable d'environnement `LD_PRELOAD` permet de forcer le chargement d'une bibliothèque partagée **avant** toutes les autres. Si cette bibliothèque définit un symbole portant le même nom qu'un symbole d'une bibliothèque standard (par exemple `strcmp`), c'est la version préchargée qui sera utilisée, car le loader résout les symboles dans l'ordre de chargement.

```bash
# Principe (détaillé au chapitre 22)
$ LD_PRELOAD=./ma_libc_custom.so ./keygenme_O0
```

Cette technique est un outil de RE puissant : on peut intercepter et modifier le comportement de n'importe quelle fonction de bibliothèque sans modifier le binaire lui-même. Elle repose entièrement sur le mécanisme de résolution dynamique que nous venons de décrire.

> ⚠️ `LD_PRELOAD` est ignoré pour les binaires setuid/setgid, pour des raisons de sécurité évidentes.

---

## Vérifier les versions de symboles requises

Les bibliothèques GNU/Linux utilisent un mécanisme de **versioning de symboles** qui permet à une même bibliothèque de fournir plusieurs versions d'une même fonction. Par exemple, `libc.so.6` peut fournir simultanément `realpath@GLIBC_2.2.5` (ancienne implémentation) et `realpath@GLIBC_2.3` (nouvelle implémentation).

On a déjà entrevu ce versioning à la section 5.3 avec les suffixes `@GLIBC_2.2.5` et `@GLIBC_2.34` dans la sortie de `nm -D`. Pour obtenir une vue détaillée des versions requises :

```bash
$ readelf -V keygenme_O0

Version needs section '.gnu.version_r' contains 1 entry:
 Addr: 0x0000000000000598  Offset: 0x000598  Link: 7 (.dynstr)
  000000: Version: 1  File: libc.so.6  Cnt: 2
  0x0010:   Name: GLIBC_2.2.5  Flags: none  Version: 3
  0x0020:   Name: GLIBC_2.34   Flags: none  Version: 2
```

Cette sortie nous dit que le binaire requiert deux versions de l'interface GLIBC : `GLIBC_2.2.5` et `GLIBC_2.34`. La version la plus élevée (`GLIBC_2.34`) est la contrainte déterminante — le binaire ne fonctionnera pas sur un système avec une glibc antérieure à la version 2.34.

Pour savoir quelle version de la glibc est installée :

```bash
$ /lib/x86_64-linux-gnu/libc.so.6 --version
GNU C Library (Ubuntu GLIBC 2.39-0ubuntu8) stable release version 2.39.
[...]

# Ou plus simplement :
$ ldd --version
ldd (Ubuntu GLIBC 2.39-0ubuntu8) 2.39
```

Cette vérification est essentielle quand un binaire compilé sur un système récent refuse de s'exécuter sur un système plus ancien avec l'erreur classique :

```
./programme: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.34' not found
```

---

## Workflow pratique : de la dépendance à l'analyse

Voici comment les informations de dépendances s'intègrent dans le processus de RE :

```bash
# 1. Lister les dépendances directes (sûr, même sur un binaire suspect)
$ readelf -d keygenme_O0 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]

# 2. Résoudre les chemins complets (si le binaire est fiable)
$ ldd keygenme_O0
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f2a3c600000)
	[...]

# 3. Vérifier la version exacte de la bibliothèque
$ file /lib/x86_64-linux-gnu/libc.so.6
/lib/x86_64-linux-gnu/libc.so.6: ELF 64-bit LSB shared object, x86-64, [...]

# 4. Si besoin, analyser la bibliothèque elle-même
$ nm -D /lib/x86_64-linux-gnu/libc.so.6 | grep strcmp
0000000000091ef0 T strcmp
0000000000091ef0 i strcmp

# 5. Vérifier les versions de symboles requises
$ readelf -V keygenme_O0
```

Ce workflow part de la question « de quoi dépend ce binaire ? » et descend jusqu'au niveau de détail nécessaire. Dans la majorité des cas, les étapes 1 et 2 suffisent. Les étapes 3 à 5 entrent en jeu quand on doit comprendre le comportement exact d'une fonction de bibliothèque ou diagnostiquer un problème de compatibilité.

---

## Ce qu'il faut retenir pour la suite

- **`ldd` donne la carte complète des dépendances** — noms, chemins résolus, et adresses de chargement. Chaque bibliothèque listée est un indice fonctionnel sur le comportement du programme.  
- **Ne jamais utiliser `ldd` sur un binaire suspect** — préférez `readelf -d | grep NEEDED`, qui parse les headers sans risque d'exécution de code.  
- **`readelf -d | grep NEEDED`** liste les dépendances directes. `ldd` ajoute les dépendances transitives et la résolution de chemins.  
- **L'ordre de résolution** est : `RPATH`/`RUNPATH` → `LD_LIBRARY_PATH` → cache `ldconfig` → répertoires par défaut. C'est cet ordre qui rend possibles les techniques d'interposition comme `LD_PRELOAD`.  
- **`ldconfig -p`** permet de chercher une bibliothèque dans le cache système et d'obtenir son chemin exact.  
- **Le versioning de symboles** (`@GLIBC_2.x.y`) indique la version minimale de la bibliothèque requise. `readelf -V` donne le détail complet.  
- Un message `not a dynamic executable` de `ldd` signifie un binaire statiquement lié — l'approche RE change significativement (pas de `.dynsym`, pas de PLT/GOT, binaire plus volumineux).

---


⏭️ [`strace` / `ltrace` — appels système et appels de bibliothèques (syscall vs libc)](/05-outils-inspection-base/05-strace-ltrace.md)
