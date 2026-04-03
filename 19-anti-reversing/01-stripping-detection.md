🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.1 — Stripping (`strip`) et détection

> 🎯 **Objectif** : Comprendre ce que `strip` retire d'un binaire ELF, ce qu'il laisse intact, comment détecter un binaire strippé et quelles stratégies adopter pour l'analyser malgré tout.

---

## Le stripping, première ligne de défense

Le stripping est la technique anti-RE la plus simple, la plus rapide à appliquer et la plus universellement déployée. Presque tous les binaires distribués en production sont strippés. Ce n'est pas une technique sophistiquée — c'est un outil GNU standard (`strip`, fourni avec Binutils) — mais son impact sur le confort de l'analyste est considérable.

Le principe est direct : retirer du fichier ELF toutes les informations qui ne sont pas nécessaires à l'exécution. Le binaire fonctionne exactement de la même manière, mais l'analyste perd les repères qui facilitent la compréhension du code.

## Ce que `strip` retire

Pour bien comprendre l'impact du stripping, il faut distinguer ce qui disparaît de ce qui survit.

### Les symboles de la table `.symtab`

C'est la perte la plus visible. La section `.symtab` contient la table de symboles statiques : noms de fonctions, noms de variables globales, noms de fichiers sources. Après stripping, cette section entière est supprimée.

Concrètement, là où `objdump -d` affichait :

```
0000000000401156 <verify_password>:
  401156:  55                    push   rbp
  401157:  48 89 e5              mov    rbp,rsp
```

Il affichera désormais :

```
  401156:  55                    push   rbp
  401157:  48 89 e5              mov    rbp,rsp
```

La fonction existe toujours au même endroit, avec le même code — mais elle n'a plus de nom. Dans Ghidra, elle apparaîtra comme `FUN_00401156`. L'analyste devra lui donner un nom manuellement, en comprenant ce qu'elle fait.

### Les informations de débogage DWARF

Les sections `.debug_info`, `.debug_abbrev`, `.debug_line`, `.debug_str`, `.debug_ranges` et les autres sections DWARF sont entièrement supprimées. Ces sections contenaient :

- La correspondance entre les adresses et les lignes du code source  
- Les noms et types des variables locales  
- Les définitions de structures, unions et énumérations  
- Les informations sur les paramètres de chaque fonction  
- L'arbre des scopes (portées lexicales)

C'est une perte massive pour le débogage. Sans DWARF, GDB ne peut plus afficher les variables par leur nom, ni montrer la ligne de code source correspondante. L'analyste travaille exclusivement avec des registres, des adresses mémoire et du code assembleur.

### La section `.comment`

Cette section contient généralement la version du compilateur utilisé (par exemple `GCC: (Ubuntu 13.2.0-23ubuntu4) 13.2.0`). Utile pour l'analyste, inutile pour l'exécution — `strip` la retire.

### Les sections de notes `.note.*`

Certaines sections de notes (`.note.gnu.build-id` peut survivre selon les options) sont retirées. Le Build ID est parfois conservé car le loader peut l'utiliser.

## Ce que `strip` ne retire PAS

C'est ici que l'analyste reprend espoir. Plusieurs catégories d'information survivent au stripping parce qu'elles sont nécessaires à l'exécution.

### La table de symboles dynamiques `.dynsym`

C'est le point crucial. Les symboles dynamiques — ceux qui sont nécessaires au linker dynamique (`ld.so`) pour résoudre les appels aux bibliothèques partagées — ne sont **pas** touchés par `strip`. Ces symboles vivent dans la section `.dynsym`, pas dans `.symtab`.

Cela signifie qu'après stripping, on voit toujours :

- Les noms de toutes les fonctions importées depuis les bibliothèques partagées (`printf`, `strcmp`, `malloc`, `ptrace`, `fopen`…)  
- Les noms des fonctions exportées par le binaire (s'il en exporte)  
- Les entrées PLT/GOT associées

C'est une mine d'or. Voir un appel à `ptrace` dans `.dynsym` révèle immédiatement une technique anti-debug. Voir `AES_encrypt` trahit l'usage de chiffrement. Les symboles dynamiques racontent l'histoire du binaire même quand les symboles statiques ont été effacés.

### Les chaînes de caractères `.rodata`

La section `.rodata` (read-only data) contient les chaînes littérales du programme : messages d'erreur, prompts, format strings de `printf`, chemins de fichiers. `strip` ne touche pas à `.rodata` car ces données sont référencées par le code.

C'est pour cette raison que `strings` reste utile après stripping. Un message comme `"Erreur : environnement non conforme."` dans notre binaire d'entraînement donne immédiatement un indice sur la présence d'un contrôle anti-debug.

### Le code exécutable `.text`

Évidemment, le code machine lui-même est intact. Les instructions, les adresses de saut, la logique du programme — tout est là. Le stripping ne modifie pas une seule instruction. L'analyste peut toujours désassembler, poser des breakpoints, tracer l'exécution.

### Les sections `.plt`, `.got`, `.init`, `.fini`

Toute la mécanique d'édition de liens dynamique reste en place. Les stubs PLT, la table GOT, les constructeurs et destructeurs — tout ce qui est nécessaire au runtime survit.

### Les en-têtes ELF et la table des sections restantes

Les headers ELF (ELF header, program headers) sont intacts. La table des sections (section header table) est réduite — les entrées correspondant aux sections supprimées disparaissent — mais les sections restantes conservent leurs noms.

## Les variantes de `strip`

L'outil `strip` accepte plusieurs niveaux d'agressivité :

### `strip` (par défaut)

Retire `.symtab`, les sections de débogage DWARF, `.comment` et les sections de notes non essentielles. C'est le stripping standard, celui que vous rencontrerez le plus souvent.

```bash
strip anti_reverse_debug -o anti_reverse_stripped
```

### `strip --strip-all` (`-s`)

Équivalent à l'option par défaut dans la plupart des cas, mais explicite. Retire tout ce qui n'est pas nécessaire à l'exécution.

### `strip --strip-debug` (`-g`)

Retire uniquement les sections de débogage DWARF, mais conserve la table `.symtab`. On perd la correspondance avec le code source, mais on garde les noms de fonctions. C'est un compromis parfois utilisé pour les binaires distribués avec un support minimal de débogage.

### `strip --strip-unneeded`

Retire les symboles qui ne sont pas nécessaires au traitement des relocations. Plus sélectif que `--strip-all` : il peut conserver certains symboles globaux nécessaires.

### Le flag `-s` de GCC

On peut aussi stripper directement à la compilation :

```bash
gcc -s -o programme programme.c
```

C'est équivalent à compiler puis appeler `strip` sur le résultat. Le flag `-s` est passé au linker (`ld`), qui supprime les symboles à l'étape d'édition de liens.

## Détecter un binaire strippé

Identifier un binaire strippé est rapide. Plusieurs méthodes complémentaires permettent d'en être sûr.

### Avec `file`

La commande `file` indique explicitement si un binaire est strippé :

```bash
$ file anti_reverse_debug
anti_reverse_debug: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
for GNU/Linux 3.2.0, with debug_info, not stripped  

$ file anti_reverse_stripped
anti_reverse_stripped: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
for GNU/Linux 3.2.0, stripped  
```

Les mentions clés sont `not stripped`, `stripped` et `with debug_info`. Un binaire peut être `not stripped` sans avoir les informations de débogage (compilé sans `-g` mais sans avoir été strippé).

### Avec `readelf -S`

On vérifie la présence ou l'absence des sections révélatrices :

```bash
$ readelf -S anti_reverse_debug | grep -E '\.symtab|\.debug|\.comment'
  [29] .comment          PROGBITS  ...
  [30] .symtab           SYMTAB    ...
  [31] .strtab           STRTAB    ...
  [32] .debug_info       PROGBITS  ...
  [33] .debug_abbrev     PROGBITS  ...
  ...

$ readelf -S anti_reverse_stripped | grep -E '\.symtab|\.debug|\.comment'
  (aucun résultat)
```

L'absence totale de `.symtab` et des sections `.debug_*` confirme le stripping.

### Avec `nm`

L'outil `nm` affiche les symboles. Sur un binaire strippé, il le dit clairement :

```bash
$ nm anti_reverse_stripped
nm: anti_reverse_stripped: no symbols
```

En revanche, `nm -D` (symboles dynamiques) fonctionne toujours. Comparons la variante sans protections anti-debug et celle avec toutes les protections :

```bash
$ nm -D anti_reverse_stripped
                 U explicit_bzero@GLIBC_2.25
                 U fgets@GLIBC_2.2.5
                 U fprintf@GLIBC_2.2.5
                 U fflush@GLIBC_2.2.5
                 U printf@GLIBC_2.2.5
                 U signal@GLIBC_2.2.5
                 U strlen@GLIBC_2.2.5
                 U __stack_chk_fail@GLIBC_2.4

$ nm -D anti_reverse_all_checks
                 U clock_gettime@GLIBC_2.17
                 U explicit_bzero@GLIBC_2.25
                 U fgets@GLIBC_2.2.5
                 U fopen@GLIBC_2.2.5
                 U fprintf@GLIBC_2.2.5
                 U fflush@GLIBC_2.2.5
                 U printf@GLIBC_2.2.5
                 U ptrace@GLIBC_2.2.5
                 U signal@GLIBC_2.2.5
                 U strlen@GLIBC_2.2.5
                 U strncmp@GLIBC_2.2.5
                 U strtol@GLIBC_2.2.5
                 U __stack_chk_fail@GLIBC_2.4
```

La différence est frappante. La variante avec protections anti-debug importe `ptrace`, `fopen`, `clock_gettime`, `strncmp`, `strtol` — des fonctions absentes de la variante propre. La présence de `ptrace` dans les imports dynamiques est un indice majeur, même sur un binaire strippé.

### Avec la taille du fichier

Un indicateur simple mais souvent négligé. La différence de taille entre un binaire avec symboles et sa version strippée est significative :

```bash
$ ls -la anti_reverse_debug anti_reverse_stripped
-rwxr-xr-x 1 user user  23456  anti_reverse_debug
-rwxr-xr-x 1 user user  14832  anti_reverse_stripped
```

Un binaire qui pèse nettement moins que ce qu'on attendrait pour sa complexité apparente a probablement été strippé. Si en plus il contient des informations DWARF (`-g`), la différence peut atteindre un facteur 3 à 5.

## Stratégies d'analyse face à un binaire strippé

Le stripping rend l'analyse plus lente, mais pas impossible. Voici les approches à adopter.

### Exploiter les symboles dynamiques

Comme vu plus haut, `.dynsym` survit. Les appels via PLT restent nommés dans le désassemblage. Un appel à `call printf@plt` est toujours lisible. L'analyste peut remonter depuis les fonctions de bibliothèque connues pour comprendre la logique : si une fonction appelle `fopen`, `fread` puis `fclose`, c'est probablement une routine de lecture de fichier.

### Exploiter les chaînes de caractères

L'outil `strings` combiné avec les cross-references dans Ghidra permet de retrouver les fonctions intéressantes. Si vous cherchez la routine de vérification de mot de passe, cherchez la chaîne `"Mot de passe"` dans `.rodata`, puis trouvez quelle fonction la référence via XREF.

### Renommage progressif dans le désassembleur

Dans Ghidra, IDA ou Cutter, renommez chaque fonction au fur et à mesure que vous comprenez son rôle. Partez des fonctions que vous identifiez avec certitude (celles qui appellent des fonctions de bibliothèque connues avec des chaînes reconnaissables) et remontez le graphe d'appels.

### Signatures de fonctions de bibliothèques

Les outils comme FLIRT (IDA) ou les signatures Ghidra permettent d'identifier automatiquement les fonctions de bibliothèques standard qui auraient été liées statiquement. Si le binaire est linké dynamiquement, les fonctions de la libc sont déjà nommées via PLT. Si le binaire est statique, ces signatures deviennent essentielles pour ne pas perdre du temps à reverser `memcpy` ou `strlen`.

### Récupérer les symboles depuis un fichier de débogage séparé

Certaines distributions Linux fournissent des paquets `*-dbg` ou `*-dbgsym` contenant les informations de débogage dans un fichier séparé (`.debug`). GDB sait charger ces fichiers automatiquement via le Build ID. Si le binaire cible est un logiciel packagé dont la version est identifiable, cette approche peut restaurer la totalité des symboles.

```bash
# Trouver le Build ID
readelf -n binaire_cible | grep "Build ID"

# GDB cherche automatiquement dans /usr/lib/debug/
gdb ./binaire_cible
```

## Impact du stripping sur nos binaires d'entraînement

Le Makefile du chapitre produit deux variantes directement liées à cette section :

- **`anti_reverse_debug`** — Compilé avec `-O0 -g`, non strippé. Contient les symboles, DWARF, tout le confort de l'analyste. C'est la version de référence.  
- **`anti_reverse_stripped`** — Compilé avec `-O2`, strippé. Les protections anti-debug sont désactivées pour isoler l'effet du stripping seul.

Comparer ces deux variantes avec `readelf -S`, `nm`, `file` et `checksec` est le point de départ naturel pour intérioriser ce que `strip` change — et surtout ce qu'il ne change pas.

---


⏭️ [Packing avec UPX — détecter et décompresser](/19-anti-reversing/02-packing-upx.md)
