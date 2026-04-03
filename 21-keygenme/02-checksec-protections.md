🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.2 — Inventaire des protections avec `checksec`

> 📖 **Rappel** : les protections binaires (ASLR, PIE, NX, canary, RELRO) ont été présentées en détail au chapitre 19. Cette section les applique concrètement à notre keygenme. Si les termes vous semblent flous, revisitez les sections 19.5 et 19.6 avant de continuer.

---

## Introduction

Le triage de la section 21.1 a révélé la nature du binaire (ELF x86-64, PIE, dynamiquement linké) et son contenu (fonctions internes, chaînes en clair). Avant de plonger dans le désassemblage, il reste une question essentielle : **quelles protections le compilateur et le linker ont-ils activées ?**

La réponse conditionne directement la stratégie d'analyse. Un binaire avec Full RELRO et un stack canary ne se patche pas de la même façon qu'un binaire sans aucune protection. Un binaire PIE avec ASLR actif impose de travailler en offsets relatifs dans GDB. Connaître les protections *avant* d'analyser, c'est éviter de perdre du temps sur des approches vouées à l'échec.

L'outil `checksec` automatise cet inventaire en une seule commande.

---

## `checksec` : présentation rapide

`checksec` est un script shell (historiquement) ou un module Python (via `pwntools`) qui inspecte un binaire ELF et liste ses protections de sécurité. Il ne fait rien de magique : il lit les headers ELF, les sections et les flags du programme — exactement ce que `readelf` permet, mais en synthétisant l'information de manière immédiatement lisible.

Deux façons de l'invoquer :

```bash
# Via le script checksec standalone
$ checksec --file=keygenme_O0

# Via pwntools (Python)
$ pwn checksec keygenme_O0
```

Les deux produisent un résultat équivalent. Nous utiliserons la syntaxe `pwntools` dans la suite du chapitre, car elle s'intègre naturellement dans les scripts Python des sections 21.7 et 21.8.

---

## Résultat sur `keygenme_O0`

```bash
$ pwn checksec keygenme_O0
    Arch:       amd64-64-little
    RELRO:      Full RELRO
    Stack:      Canary found
    NX:         NX enabled
    PIE:        PIE enabled
    DEBUGINFO:  Yes
```

Six lignes, six informations. Décortiquons chacune dans le contexte de notre analyse.

---

## Analyse détaillée de chaque protection

### Arch : `amd64-64-little`

L'architecture confirme ce que `file` avait déjà indiqué : x86-64 en little-endian. Ce n'est pas une « protection » à proprement parler, mais l'information est critique pour toute la suite : on travaille avec des registres de 64 bits, la convention d'appel System V AMD64 (paramètres dans `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`), et un encodage little-endian des adresses en mémoire.

### RELRO : `Full RELRO`

RELRO (RELocation Read-Only) contrôle les permissions de la section `.got.plt` après le chargement du binaire.

En **Full RELRO**, toutes les relocations sont résolues au démarrage (eager binding) et la GOT est ensuite marquée en lecture seule. Concrètement :

- La GOT ne peut pas être écrasée à l'exécution. Les techniques d'exploitation de type « GOT overwrite » sont neutralisées.  
- Les appels aux fonctions de la libc (`printf`, `strcmp`…) ne passent plus par le mécanisme de lazy binding — l'adresse réelle est inscrite dans la GOT dès le chargement par `ld.so`.

**Impact sur notre analyse** : pour le keygenme, Full RELRO signifie qu'on ne pourra pas modifier la GOT pour détourner un appel (par exemple remplacer l'adresse de `strcmp` par celle d'une fonction qui retourne toujours 0). Ce n'est pas un problème ici — notre approche sera le patching direct du saut conditionnel (section 21.6) ou l'écriture d'un keygen (section 21.8). Mais sur une cible où l'on chercherait à exploiter une vulnérabilité, Full RELRO fermerait un vecteur d'attaque classique.

> 💡 **Comment GCC active Full RELRO** : c'est le comportement par défaut sur les distributions modernes (depuis Debian 11, Ubuntu 22.04, Fedora 33+). Le flag explicite est `-Wl,-z,relro,-z,now` passé au linker. L'option `-z,now` est ce qui transforme Partial RELRO en Full RELRO en forçant le eager binding.

### Stack : `Canary found`

Un stack canary (ou stack protector) est une valeur sentinelle placée entre les variables locales et l'adresse de retour sur la pile. À la fin de chaque fonction protégée, le programme vérifie que le canary n'a pas été altéré. Si c'est le cas, il appelle `__stack_chk_fail` et termine immédiatement — empêchant l'exploitation d'un buffer overflow sur la pile.

En assembleur, on reconnaît le canary à deux endroits :

**Dans le prologue** de la fonction :
```nasm
mov    rax, QWORD PTR fs:0x28    ; lecture du canary depuis le TLS  
mov    QWORD PTR [rbp-0x8], rax  ; copie sur la pile  
```

**Dans l'épilogue**, juste avant `ret` :
```nasm
mov    rax, QWORD PTR [rbp-0x8]  ; relecture du canary depuis la pile  
xor    rax, QWORD PTR fs:0x28    ; comparaison avec la valeur originale  
jne    .canary_fail               ; si différent → stack smashing détecté  
```

La valeur du canary est lue depuis `fs:0x28` (Thread Local Storage). Elle est aléatoire et change à chaque exécution du programme.

**Impact sur notre analyse** : le canary ajoute quelques instructions dans le prologue et l'épilogue de chaque fonction. Il ne faut pas les confondre avec la logique métier du programme. Quand on lit le désassemblage de `check_license` dans Ghidra ou GDB, les instructions liées au canary (accès à `fs:0x28`, le `xor` final, le `jne` vers `__stack_chk_fail`) sont du « bruit de protection » qu'on peut mentalement ignorer. On les reconnaît facilement grâce au pattern `fs:0x28`.

> 💡 **Comment GCC active le canary** : le flag `-fstack-protector-strong` (défaut sur les distributions modernes) protège les fonctions qui contiennent des tableaux locaux ou des appels à `alloca`. Le flag `-fstack-protector-all` protège *toutes* les fonctions. Le flag `-fno-stack-protector` désactive la protection.

### NX : `NX enabled`

NX (No eXecute), aussi appelé DEP (Data Execution Prevention) ou W^X, interdit l'exécution de code dans les segments de données (pile, heap, `.data`, `.bss`). Le processeur refuse d'exécuter une instruction située dans une page mémoire marquée « non exécutable ».

On avait déjà déduit cette protection en section 21.1 en examinant les segments avec `readelf -l` : le segment de pile (`GNU_STACK`) n'avait pas le flag `E` (exécutable).

**Impact sur notre analyse** : NX empêche l'injection de shellcode classique (écrire du code sur la pile puis sauter dessus). Pour un keygenme, cette protection est sans conséquence — on ne cherche pas à exécuter du code arbitraire, mais à comprendre l'algorithme de vérification. NX est néanmoins important à noter car il conditionne les techniques d'exploitation (ROP, ret2libc) si l'on devait exploiter une vulnérabilité dans le binaire.

### PIE : `PIE enabled`

PIE (Position-Independent Executable) signifie que le binaire est compilé entièrement en code indépendant de la position. Combiné avec ASLR (au niveau de l'OS), l'adresse de base du binaire change à chaque exécution.

**Impact sur notre analyse** : c'est la protection qui affecte le plus le workflow quotidien du reverse engineer.

En **analyse statique** (Ghidra, objdump), les adresses affichées sont des offsets relatifs à la base du binaire. Ghidra utilise par défaut une base fictive (`0x00100000`) et toutes les adresses sont cohérentes entre elles — pas de problème.

En **analyse dynamique** (GDB), les adresses absolues changent à chaque lancement. Pour poser un breakpoint sur `check_license` dans GDB :

```bash
# Avec symboles : pas de problème, GDB résout le nom
(gdb) break check_license

# Sans symboles (binaire strippé) : il faut calculer l'adresse
# Méthode : offset Ghidra - base Ghidra + base réelle
(gdb) info proc mappings
# ... repérer la base de chargement du binaire
(gdb) break *0x<base_réelle + offset>
```

On peut aussi désactiver ASLR pour simplifier le débogage :

```bash
# Désactiver ASLR globalement (temporaire, nécessite root)
$ echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

# Ou uniquement pour GDB
(gdb) set disable-randomization on
```

La deuxième méthode est préférable : elle ne touche que la session GDB en cours et ne réduit pas la sécurité du système hôte.

### DEBUGINFO : `Yes`

Indique la présence des informations de débogage DWARF (compilé avec `-g`). Ce n'est pas une protection — c'est l'inverse : leur présence facilite considérablement le RE en fournissant les noms de fonctions, les types de variables, les numéros de ligne et la correspondance source-assembleur.

Sur les variantes strippées (`keygenme_strip`, `keygenme_O2_strip`), cette ligne affichera `No`.

---

## Comparaison des 5 variantes

Pour avoir une vue d'ensemble, exécutons `checksec` sur les cinq variantes :

```bash
$ for bin in keygenme_O0 keygenme_O2 keygenme_O3 keygenme_strip keygenme_O2_strip; do
    echo "── $bin ──"
    pwn checksec $bin 2>/dev/null | tail -6
    echo ""
done
```

Le résultat attendu (avec un GCC récent sur une distribution moderne) :

| Protection | `_O0` | `_O2` | `_O3` | `_strip` | `_O2_strip` |  
|---|---|---|---|---|---|  
| RELRO | Full | Full | Full | Full | Full |  
| Stack Canary | ✅ | ✅ | ✅ | ✅ | ✅ |  
| NX | ✅ | ✅ | ✅ | ✅ | ✅ |  
| PIE | ✅ | ✅ | ✅ | ✅ | ✅ |  
| Debug info | ✅ | ✅ | ✅ | ❌ | ❌ |  
| Symboles | ✅ | ✅ | ✅ | ❌ | ❌ |

Les protections sont **identiques** sur les cinq variantes. C'est logique : RELRO, canary, NX et PIE sont des options du compilateur et du linker, pas du niveau d'optimisation. Le stripping supprime les symboles et les informations de débogage, mais ne touche pas aux protections de sécurité.

Ce tableau confirme un point important : **la difficulté croissante entre les variantes ne vient pas des protections** (elles sont les mêmes), mais de deux facteurs :

1. **L'optimisation** (`-O0` → `-O2` → `-O3`) : le code assembleur devient plus compact, les fonctions peuvent être inlinées, les boucles déroulées, les variables maintenues dans des registres au lieu de la pile. L'algorithme est le même, mais son expression en assembleur est plus difficile à lire.

2. **Le stripping** : sans symboles, on perd les noms de fonctions (`check_license` devient `FUN_XXXXXXXX` dans Ghidra), les types des variables, et la correspondance avec le source. Il faut reconstituer ces informations manuellement.

---

## Ce que `checksec` ne dit pas

`checksec` couvre les protections standard d'un binaire ELF moderne, mais ne détecte pas :

- **Le packing** (UPX, packers custom) — il faut vérifier l'entropie des sections et chercher les signatures de packers connus. Voir chapitre 29.  
- **L'obfuscation de flux de contrôle** (control flow flattening, bogus control flow) — visible uniquement dans le désassemblage. Voir chapitre 19, section 3.  
- **Les techniques anti-débogueur** (`ptrace` detection, timing checks, `/proc/self/status`) — détectables par analyse statique ou en exécutant sous `strace`. Voir chapitre 19, section 7.  
- **Le chiffrement de chaînes ou de sections** — détectable via l'analyse d'entropie ou l'absence de chaînes lisibles dans `strings`.  
- **Fortify Source** (`_FORTIFY_SOURCE`) — le remplacement de fonctions dangereuses (`strcpy` → `__strcpy_chk`) est visible dans les symboles mais pas toujours signalé par `checksec`.

Notre keygenme n'utilise aucune de ces techniques supplémentaires — les protections se limitent aux défenses standard activées par défaut par GCC.

---

## Synthèse et implications pour la suite

L'inventaire des protections complète le triage de la section 21.1. On peut maintenant résumer notre connaissance du binaire en deux catégories :

**Ce qui facilite l'analyse :**
- Symboles et informations DWARF présents (sur la variante `_O0`)  
- Pas de packing ni d'obfuscation  
- Pas de technique anti-débogueur  
- Chaînes en clair

**Ce qui contraint l'analyse :**
- **PIE + ASLR** → travailler en offsets relatifs dans GDB, ou désactiver ASLR  
- **Full RELRO** → pas de GOT overwrite possible  
- **Stack canary** → bruit supplémentaire dans le prologue/épilogue (instructions `fs:0x28` à ignorer)  
- **NX** → pas d'injection de shellcode (non pertinent pour un keygen, mais bon à savoir)

Aucune de ces contraintes n'empêche de mener à bien notre objectif : comprendre l'algorithme de vérification et écrire un keygen. Elles ajoutent simplement quelques précautions méthodologiques dans l'utilisation de GDB (section 21.5) et ferment certaines voies d'exploitation qui ne sont de toute façon pas notre approche ici.

Le terrain est balisé. La section suivante (21.3) plonge dans Ghidra pour localiser la routine de vérification par approche top-down, en partant de `main()` et en suivant les cross-references jusqu'au `strcmp` décisif.

⏭️ [Localisation de la routine de vérification (approche top-down)](/21-keygenme/03-localisation-routine.md)
