🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint du chapitre 5

> ⚠️ **Spoilers** — Ne consultez ce fichier qu'après avoir rédigé votre propre rapport de triage.

---

## Rapport de triage — `mystery_bin`

### 1. Identification

```bash
$ file mystery_bin
mystery_bin: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, with debug_info, not stripped  
```

| Propriété | Valeur | Implication |  
|---|---|---|  
| Format | ELF 64-bit | Binaire natif Linux |  
| Architecture | x86-64, LSB (little-endian) | Jeu d'instructions standard PC |  
| Type | PIE executable (`DYN`) | Adresses relatives, ASLR possible |  
| Linking | Dynamique (`libc.so.6`) | PLT/GOT présentes, imports visibles |  
| Symboles debug | `with debug_info` | Sections DWARF présentes (`.debug_*`) |  
| Stripping | `not stripped` | Noms de fonctions locales disponibles dans `.symtab` |  
| Compilateur | GCC (version visible dans `.comment`) | Chaîne GNU standard |

Le binaire n'est ni strippé ni packé. Les conditions sont idéales pour le triage : tous les outils produiront des résultats riches.

---

### 2. Chaînes notables

```bash
$ strings mystery_bin | grep -iE '(error|fail|password|key|access|encrypt|secret|mystery|config|verbose)'
```

**Messages d'interaction utilisateur :**

- `=== mystery-tool v2.4.1-beta ===` → nom et version de l'outil.  
- `Enter access password:` → le programme demande un mot de passe.  
- `Authentication failed. Access denied.` → message d'échec.  
- `Authentication successful. Welcome.` → message de succès.  
- `Commands: encrypt <message> | status | quit` → le programme a un mode interactif avec des commandes.

**Données sensibles :**

- `R3v3rs3M3!2024` → chaîne suspecte qui ressemble fortement à un mot de passe hardcodé. Hypothèse forte : c'est la valeur comparée par `strcmp` lors de l'authentification.  
- `MYSTERYK` / `EY012345` → fragments d'une clé, probablement une clé XOR visible dans `.rodata`.

**Chemins de fichiers :**

- `/tmp/mystery.conf` → fichier de configuration lu au démarrage.  
- `/tmp/mystery.out` → fichier de sortie écrit par le programme.  
- `/proc/self/status` → accès au pseudo-filesystem proc, technique classique de détection de débogueur (lecture de `TracerPid`).

**Informations de compilation :**

```bash
$ strings mystery_bin | grep GCC
GCC: (Ubuntu 13.2.0-23ubuntu4) 13.2.0
```

**Formats printf :**

```bash
$ strings mystery_bin | grep '%'
[!] Debugger detected (pid: %d)
[+] Message encrypted and written to %s (%zu bytes)
[*] Checksum: 0x%08X
[*] Timestamp: %lu
```

Ces formats révèlent que le programme affiche un PID de débogueur (anti-debug), un chemin de fichier de sortie avec une taille, un checksum en hexadécimal, et un timestamp. Le programme effectue donc un chiffrement et écrit le résultat.

**Autres chaînes significatives :**

- `MYST` → possible magic bytes d'un format de fichier custom (header du fichier de sortie).  
- `encrypt`, `status`, `quit`, `exit` → commandes du mode interactif.  
- `Unknown command:` → gestion d'erreur du parseur de commandes.

---

### 3. Structure ELF

```bash
$ readelf -hW mystery_bin | grep -E '(Type|Machine|Entry|Number of section)'
  Type:                              DYN (Position-Independent Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x10c0
  Number of section headers:         36
```

Le binaire a 36 sections — plus que la moyenne, ce qui s'explique par la présence des sections DWARF (`debug_info`).

```bash
$ readelf -SW mystery_bin | grep -E '\.(text|rodata|data|bss|debug|symtab)'
  [15] .text             PROGBITS  ...  000005XX  ...  AX  ...
  [17] .rodata           PROGBITS  ...  000002XX  ...   A  ...
  [24] .data             PROGBITS  ...  ...       ...  WA  ...
  [25] .bss              NOBITS    ...  ...       ...  WA  ...
  [27] .symtab           SYMTAB    ...  ...       ...      ...
  [28] .strtab           STRTAB    ...  ...       ...      ...
  [29] .debug_info       PROGBITS  ...  ...       ...      ...
  [30] .debug_abbrev     PROGBITS  ...  ...       ...      ...
  [31] .debug_line       PROGBITS  ...  ...       ...      ...
  [32] .debug_str        PROGBITS  ...  ...       ...      ...
```

**Observations :**

- `.text` de quelques centaines d'octets : programme de taille modeste.  
- `.rodata` contient les chaînes de caractères (confirmé par les résultats de `strings`).  
- `.symtab` et `.strtab` présentes : symboles complets disponibles.  
- Sections `.debug_*` présentes : le binaire a été compilé avec `-g` (symboles DWARF). Cela facilitera considérablement le débogage avec GDB si nécessaire.  
- Aucune section aux noms inhabituels : pas de signe de packing ou d'obfuscation.

```bash
$ readelf -d mystery_bin | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

Seule dépendance : `libc.so.6`. Le programme n'utilise pas de bibliothèque externe de cryptographie (pas de `libssl`, `libcrypto`, `libsodium`). Le chiffrement est donc implémenté en interne — probablement l'algorithme XOR suggéré par les chaînes.

```bash
$ readelf -lW mystery_bin | grep -E '(GNU_STACK|GNU_RELRO)'
  GNU_STACK      ... RW  0x10
  GNU_RELRO      ... R   0x1
```

Pile non exécutable (NX activé). Segment RELRO présent.

---

### 4. Fonctions et imports

**Fonctions du programme :**

```bash
$ nm -nS mystery_bin | grep ' T '
0000000000001189 000000000000005e T compute_checksum
00000000000011e7 0000000000000042 T xor_encrypt
0000000000001229 0000000000000089 T check_debugger
00000000000012b2 00000000000000c5 T authenticate_user
0000000000001377 0000000000000065 T load_config
00000000000013dc 0000000000000120 T process_message
00000000000014fc 000000000000014a T interactive_mode
0000000000001646 00000000000000a2 T main
```

> **Note** : les adresses et tailles exactes peuvent varier selon la version de GCC et les options de compilation. Les valeurs ci-dessus sont indicatives.

Les noms de fonctions sont extrêmement révélateurs et permettent de reconstruire l'architecture du programme :

| Fonction | Taille approx. | Rôle déduit |  
|---|---|---|  
| `main` | ~160 octets | Point d'entrée, orchestre les étapes |  
| `check_debugger` | ~140 octets | Détection de débogueur (anti-RE léger) |  
| `authenticate_user` | ~200 octets | Demande et vérifie le mot de passe |  
| `load_config` | ~100 octets | Charge `/tmp/mystery.conf` |  
| `process_message` | ~290 octets | Chiffre un message et l'écrit dans un fichier |  
| `xor_encrypt` | ~65 octets | Routine de chiffrement XOR (courte = algorithme simple) |  
| `compute_checksum` | ~95 octets | Calcul d'un checksum sur les données |  
| `interactive_mode` | ~330 octets | Boucle de commandes (la plus grosse fonction) |

**Flux d'exécution probable** (déduit des noms et des tailles) :
`main` → `check_debugger` → `load_config` → `authenticate_user` → `interactive_mode` → (`process_message` → `xor_encrypt` + `compute_checksum`).

**Imports (fonctions de bibliothèque) :**

```bash
$ nm -D mystery_bin | grep ' U '
                 U atoi@GLIBC_2.2.5
                 U fclose@GLIBC_2.2.5
                 U fgets@GLIBC_2.2.5
                 U fopen@GLIBC_2.2.5
                 U fprintf@GLIBC_2.2.5
                 U free@GLIBC_2.2.5
                 U fwrite@GLIBC_2.2.5
                 U malloc@GLIBC_2.2.5
                 U memcpy@GLIBC_2.14
                 U printf@GLIBC_2.2.5
                 U strcmp@GLIBC_2.2.5
                 U strlen@GLIBC_2.2.5
                 U strncmp@GLIBC_2.2.5
                 U time@GLIBC_2.2.5
                 U __stack_chk_fail@GLIBC_2.4
                 U fflush@GLIBC_2.2.5
```

**Interprétation des imports :**

- `strcmp`, `strncmp`, `strlen` → comparaisons de chaînes (authentification, parsing de commandes).  
- `fopen`, `fgets`, `fwrite`, `fclose` → opérations sur fichiers (config lue ligne par ligne avec `fgets`, sortie écrite avec `fwrite`).  
- `malloc`, `free`, `memcpy` → allocation et copie de buffers (traitement du message à chiffrer).  
- `printf`, `fprintf`, `fflush` → affichage (interface utilisateur, messages d'erreur sur stderr).  
- `atoi` → conversion chaîne → entier (parsing d'options dans le fichier de config).  
- `time` → horodatage (timestamp dans le header du fichier de sortie).  
- `__stack_chk_fail` → stack canary activé (confirmation pour checksec).

Aucun import réseau (`socket`, `connect`, `send`, `recv`) : le programme ne communique pas sur le réseau. Aucun import de processus (`fork`, `execve`, `system`) : il ne lance pas de sous-processus.

> **Note** : selon la version de GCC et le niveau d'optimisation, certaines fonctions libc peuvent être remplacées par leurs variantes fortifiées : `printf` → `__printf_chk`, `fprintf` → `__fprintf_chk`, `memcpy` → `__memcpy_chk`. De même, `printf("texte\n")` peut être optimisé en `puts("texte")` et `atoi` peut être remplacé par `strtol`. Ces substitutions ne changent pas l'interprétation fonctionnelle.

---

### 5. Protections

```bash
$ checksec --file=mystery_bin
RELRO           STACK CANARY      NX            PIE             RPATH      RUNPATH      Symbols         FORTIFY  Fortified  Fortifiable  FILE  
Full RELRO      Canary found      NX enabled    PIE enabled     No RPATH   No RUNPATH   XX Symbols      No       0          X            mystery_bin  
```

| Protection | État | Vérification manuelle |  
|---|---|---|  
| NX | Activé | `GNU_STACK` avec flags `RW` (pas de `E`) |  
| PIE | Activé | Type `DYN` dans le ELF header |  
| Stack Canary | Présent | Symbole `__stack_chk_fail` importé |  
| RELRO | Full | Segment `GNU_RELRO` + entrée `BIND_NOW` dans `.dynamic` |  
| FORTIFY | Non | Aucun symbole `_chk` dans les imports |  
| RPATH/RUNPATH | Absents | Pas de chemins de bibliothèques embarqués |

Le binaire est correctement protégé sur tous les axes sauf FORTIFY. Toutes les protections sont à leur niveau maximal (Full RELRO, pas juste Partial).

---

### 6. Comportement dynamique

**`strace` — appels système significatifs :**

```bash
$ strace -e trace=file,network,process -s 256 -o strace.log ./mystery_bin
```

Résultats pertinents (après filtrage du bruit de chargement des bibliothèques) :

```
openat(AT_FDCWD, "/proc/self/status", O_RDONLY)         = 3   # Anti-debug : lecture de TracerPid  
read(3, "Name:\tmystery_bin\n...", 256)                  = 256  
close(3)                                                  = 0  
openat(AT_FDCWD, "/tmp/mystery.conf", O_RDONLY)          = -1 ENOENT  # Config absente (normal)  
write(1, "=== mystery-tool v2.4.1-beta ===\n", 34)       = 34  
write(1, "Enter access password: ", 23)                   = 23  
read(0, "test\n", ...)                                    = 5  
write(2, "Authentication failed. Access denied.\n", 38)   = 38  # Sortie sur stderr  
exit_group(1)  
```

**Observations `strace` :**

- Le programme accède à `/proc/self/status` **avant** toute interaction utilisateur → détection de débogueur. En analyse sous GDB, ce check pourrait poser problème (il faut le contourner).  
- Il tente d'ouvrir `/tmp/mystery.conf` et gère proprement l'absence du fichier (`ENOENT` → continue sans erreur).  
- Aucun syscall réseau (`socket`, `connect`) → pas de communication réseau, confirmant l'analyse statique.  
- Aucun `fork`/`execve` → pas de lancement de sous-processus.  
- Le message d'échec est écrit sur `fd 2` (stderr via `fprintf(stderr, ...)`).

**`ltrace` — appels de bibliothèques :**

```bash
$ ltrace -s 256 -o ltrace.log ./mystery_bin <<< "test"
```

```
fopen("/proc/self/status", "r")                          = 0x55a...  
fgets("Name:\tmystery_bin\n", 256, 0x55a...)             = 0x7ff...  
strncmp("Name:\tmystery_bin\n", "TracerPid:", 10)        = -1  
fgets("...", 256, 0x55a...)                               = ...  
[... lecture ligne par ligne jusqu'à TracerPid ...]
strncmp("TracerPid:\t0\n", "TracerPid:", 10)             = 0   # Trouvé !  
atoi("0\n")                                               = 0   # Pas de traceur  
fclose(0x55a...)                                          = 0  
fopen("/tmp/mystery.conf", "r")                           = 0   # NULL = fichier absent  
printf("=== %s ===\n", "mystery-tool v2.4.1-beta")       = 34  
printf("Enter access password: ")                         = 23  
fgets("test\n", 256, 0x7f...)                             = 0x7ff...  
strlen("test")                                            = 4  
strcmp("test", "R3v3rs3M3!2024")                          = 1   # ← MOT DE PASSE RÉVÉLÉ !  
fprintf(0x7f..., "Authentication failed. Access denied.\n") = 38  
```

**Découverte critique** : la ligne `strcmp("test", "R3v3rs3M3!2024")` révèle le mot de passe en clair. `ltrace` affiche les deux arguments de `strcmp` — notre input `"test"` et la valeur attendue `"R3v3rs3M3!2024"`.

**Vérification avec le bon mot de passe :**

```bash
$ ltrace -s 256 ./mystery_bin <<< $'R3v3rs3M3!2024\nencrypt Hello World\nquit'
```

```
[... check_debugger, load_config comme avant ...]
strcmp("R3v3rs3M3!2024", "R3v3rs3M3!2024")               = 0   # Match !  
printf("Authentication successful. Welcome.\n")           = ...  
[... mode interactif ...]
strncmp("encrypt Hello World", "encrypt ", 8)             = 0  
strlen("Hello World")                                     = 11  
malloc(11)                                                = 0x55a...  
memcpy(0x55a..., "Hello World", 11)                       = 0x55a...  
time(NULL)                                                = 1711234567  
fopen("/tmp/mystery.out", "wb")                           = 0x55a...  
fwrite("\x4d\x59\x53\x54...", 24, 1, 0x55a...)           = 1   # Header (magic "MYST")  
fwrite("\x01\x28\x30\x20...", 1, 11, 0x55a...)           = 11  # Données chiffrées  
fclose(0x55a...)                                          = 0  
free(0x55a...)                                            = <void>  
strcmp("quit", "quit")                                    = 0  
```

Le programme écrit bien un fichier `/tmp/mystery.out` avec un header commençant par `MYST` suivi des données chiffrées par XOR.

**Profil statistique :**

```bash
$ ltrace -c ./mystery_bin <<< "test"
% time     seconds  usecs/call     calls      function
------ ----------- ----------- --------- --------------------
 30.00    0.000006           1         6 fgets
 20.00    0.000004           0        10 strncmp
 15.00    0.000003           3         1 strcmp
 10.00    0.000002           1         2 fopen
 10.00    0.000002           1         2 printf
  5.00    0.000001           1         1 strlen
  5.00    0.000001           0         2 fprintf
  5.00    0.000001           1         1 fclose
  [...]
```

Le nombre élevé de `strncmp` (10 appels) correspond à la lecture ligne par ligne de `/proc/self/status` — chaque ligne est comparée avec `"TracerPid:"` jusqu'à trouver la bonne.

---

### Conclusion et stratégie

**Nature du programme** : `mystery_bin` est un outil de chiffrement interactif en ligne de commande. Il authentifie l'utilisateur par mot de passe, puis propose un mode interactif où l'on peut chiffrer des messages avec un algorithme XOR. Les messages chiffrés sont écrits dans `/tmp/mystery.out` avec un header custom (magic `MYST`). Le programme intègre une détection de débogueur légère via `/proc/self/status`.

**Vulnérabilités identifiées lors du triage** :

1. **Mot de passe hardcodé** en clair dans `.rodata` (`R3v3rs3M3!2024`), directement visible avec `strings` et confirmé par `ltrace`. L'authentification est trivialement contournable.  
2. **Clé de chiffrement XOR en clair** dans `.rodata` (`MYSTERYKEY012345`). L'algorithme de chiffrement est donc réversible sans analyse du code — il suffit de XOR-er les données chiffrées avec cette clé.  
3. **Anti-debug contournable** : la vérification de `TracerPid` dans `/proc/self/status` peut être contournée en patchant le saut conditionnel, en utilisant `LD_PRELOAD` pour intercepter `fopen`, ou simplement en modifiant `/proc/self/status` via un faux fichier dans un namespace mount.

**Stratégie pour l'analyse approfondie** :

- **Objectif immédiat** : écrire un déchiffreur Python qui lit `/tmp/mystery.out`, parse le header `MysteryHeader` (magic + version + data_length + checksum + timestamp = probablement 24 octets), et XOR les données avec la clé connue.  
- **Outil recommandé** : ouvrir le binaire dans Ghidra (chapitre 8) pour confirmer la structure exacte du header (les offsets et tailles des champs) et valider l'algorithme de checksum.  
- **Alternative rapide** : utiliser ImHex (chapitre 6) avec un pattern `.hexpat` pour visualiser directement la structure du fichier `/tmp/mystery.out`.  
- **Pour le contournement de l'anti-debug** : un simple script Frida (chapitre 13) qui hook `fopen` et retourne NULL quand le chemin est `/proc/self/status` suffirait.

---

## Grille d'auto-évaluation

Comparez votre rapport avec cette grille :

| Critère | Points | Atteint ? |  
|---|---|---|  
| Format, architecture et linking correctement identifiés | 1 | |  
| Stripping et présence des symboles DWARF mentionnés | 1 | |  
| Au moins 3 chaînes significatives relevées **et interprétées** | 1 | |  
| Mot de passe hardcodé identifié (`R3v3rs3M3!2024`) | 1 | |  
| Mention du fichier `/proc/self/status` et interprétation comme anti-debug | 1 | |  
| Chemins `/tmp/mystery.conf` et `/tmp/mystery.out` identifiés | 1 | |  
| Fonctions du programme listées avec rôle déduit | 1 | |  
| Imports interprétés (notamment `strcmp`, absence de réseau) | 1 | |  
| Les 5 protections (NX, PIE, canary, RELRO, FORTIFY) documentées | 1 | |  
| `strace` : accès à `/proc/self/status` et `/tmp/mystery.conf` observés | 1 | |  
| `ltrace` : `strcmp` avec le mot de passe en clair capturé | 1 | |  
| Absence d'activité réseau constatée et mentionnée | 1 | |  
| Hypothèses argumentées sur la nature du programme | 1 | |  
| Stratégie de suite formulée (quel outil, quel objectif) | 1 | |  
| Rapport structuré et concis (~1 page), pas de copier-coller brut | 1 | |

**Barème indicatif** :

- **13–15 points** : excellent — vous maîtrisez le triage. Passez au chapitre 6.  
- **10–12 points** : bon niveau — relisez les sections correspondant aux points manqués.  
- **7–9 points** : correct — refaites le triage en suivant le workflow de la section 5.7 étape par étape.  
- **< 7 points** : reprenez les sections 5.1 à 5.6 avec les exemples pratiques avant de retenter le checkpoint.

⏭️
