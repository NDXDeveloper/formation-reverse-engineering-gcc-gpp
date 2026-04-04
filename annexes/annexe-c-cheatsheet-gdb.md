🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe C — Cheat sheet GDB / GEF / pwndbg

> 📎 **Fiche de référence** — Cette annexe regroupe les commandes les plus utiles de GDB natif ainsi que celles ajoutées par les extensions GEF et pwndbg. Elle est organisée par tâche plutôt que par ordre alphabétique, pour que vous puissiez trouver rapidement ce dont vous avez besoin pendant une session de débogage. Les commandes spécifiques à une extension sont marquées par les badges **[GEF]** et **[pwndbg]**.

---

## 1 — Lancement et attachement

### 1.1 — Démarrer GDB

| Commande shell | Description |  
|----------------|-------------|  
| `gdb ./binary` | Lance GDB sur un binaire |  
| `gdb -q ./binary` | Lance GDB en mode silencieux (supprime la bannière) |  
| `gdb -q -nx ./binary` | Lance sans charger les fichiers d'initialisation (`.gdbinit`) |  
| `gdb -q --args ./binary arg1 arg2` | Lance avec des arguments pour le programme |  
| `gdb -q -p <pid>` | Attache GDB à un processus en cours d'exécution |  
| `gdb -q -c core ./binary` | Analyse un core dump |

### 1.2 — Commandes de session à l'intérieur de GDB

| Commande GDB | Abréviation | Description |  
|-------------|-------------|-------------|  
| `file ./binary` | — | Charge un binaire dans une session GDB déjà ouverte |  
| `attach <pid>` | — | S'attache à un processus en cours |  
| `detach` | — | Se détache du processus sans le tuer |  
| `set args arg1 arg2` | — | Définit les arguments du programme |  
| `show args` | — | Affiche les arguments actuels |  
| `set env VAR=value` | — | Définit une variable d'environnement pour le programme |  
| `unset env VAR` | — | Supprime une variable d'environnement |  
| `set follow-fork-mode child` | — | Suit le processus enfant après un `fork()` |  
| `set follow-fork-mode parent` | — | Suit le processus parent après un `fork()` (défaut) |  
| `set disable-randomization off` | — | Active l'ASLR dans GDB (désactivé par défaut) |  
| `quit` | `q` | Quitte GDB |

> 💡 GDB **désactive l'ASLR** par défaut pour faciliter le débogage reproductible. Si vous testez un exploit ou analysez un binaire PIE avec ASLR, pensez à le réactiver avec `set disable-randomization off`.

---

## 2 — Exécution et navigation

### 2.1 — Lancer et contrôler l'exécution

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `run` | `r` | Lance le programme depuis le début |  
| `run < input.txt` | `r < input.txt` | Lance avec l'entrée redirigée depuis un fichier |  
| `start` | — | Lance et s'arrête automatiquement à `main()` |  
| `starti` | — | Lance et s'arrête à la toute première instruction (avant le loader) |  
| `continue` | `c` | Reprend l'exécution jusqu'au prochain breakpoint ou fin |  
| `kill` | `k` | Tue le programme en cours de débogage |

### 2.2 — Avancer pas à pas

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `next` | `n` | Exécute la ligne source suivante (passe par-dessus les appels) |  
| `step` | `s` | Exécute la ligne source suivante (entre dans les appels) |  
| `nexti` | `ni` | Exécute l'instruction assembleur suivante (passe par-dessus les `call`) |  
| `stepi` | `si` | Exécute l'instruction assembleur suivante (entre dans les `call`) |  
| `finish` | `fin` | Exécute jusqu'au retour de la fonction courante |  
| `until <addr>` | `u <addr>` | Exécute jusqu'à atteindre l'adresse ou la ligne spécifiée |  
| `advance <location>` | — | Exécute jusqu'à atteindre la localisation (comme un breakpoint temporaire) |

La distinction `next`/`nexti` vs `step`/`stepi` est fondamentale. En RE sur un binaire strippé, vous travaillerez presque exclusivement avec `ni` et `si` (niveau instruction) car les informations de lignes source ne sont pas disponibles.

### 2.3 — Revenir en arrière (Reverse Debugging)

| Commande | Description |  
|----------|-------------|  
| `target record-full` | Active l'enregistrement pour le reverse debugging |  
| `reverse-continue` | `rc` — Continue en arrière jusqu'au breakpoint précédent |  
| `reverse-nexti` | `rni` — Recule d'une instruction (sans entrer dans les `call`) |  
| `reverse-stepi` | `rsi` — Recule d'une instruction (entre dans les `call`) |  
| `reverse-finish` | Recule jusqu'à l'appel de la fonction courante |

> ⚠️ Le reverse debugging est très lent (facteur ×10 000 ou plus). Il est utile pour de courtes séquences quand vous avez dépassé un point critique, mais pas pour naviguer globalement dans un programme.

---

## 3 — Breakpoints

### 3.1 — Breakpoints classiques

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `break main` | `b main` | Breakpoint sur la fonction `main` |  
| `break *0x401234` | `b *0x401234` | Breakpoint à une adresse exacte |  
| `break *main+42` | `b *main+42` | Breakpoint à un offset depuis le début d'une fonction |  
| `break file.c:25` | `b file.c:25` | Breakpoint à la ligne 25 de `file.c` (nécessite les symboles) |  
| `tbreak *0x401234` | `tb *0x401234` | Breakpoint temporaire (supprimé automatiquement après le premier arrêt) |  
| `rbreak regex` | — | Breakpoint sur toutes les fonctions correspondant à l'expression régulière |

### 3.2 — Breakpoints conditionnels

| Commande | Description |  
|----------|-------------|  
| `break *0x401234 if $rax == 0x42` | S'arrête uniquement si `rax` vaut `0x42` |  
| `break *0x401234 if *(int*)($rsp+8) > 100` | Condition sur une valeur en mémoire |  
| `break *0x401234 if strcmp($rdi, "admin") == 0` | Condition avec appel de fonction (si disponible) |  
| `condition <num> $rcx < 10` | Ajoute/modifie la condition du breakpoint n° `<num>` |  
| `condition <num>` | Supprime la condition (le breakpoint redevient inconditionnel) |  
| `ignore <num> 50` | Ignore les 50 premiers passages au breakpoint `<num>` |

### 3.3 — Gérer les breakpoints

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `info breakpoints` | `i b` | Liste tous les breakpoints avec leur état |  
| `delete <num>` | `d <num>` | Supprime le breakpoint n° `<num>` |  
| `delete` | `d` | Supprime tous les breakpoints (demande confirmation) |  
| `disable <num>` | `dis <num>` | Désactive le breakpoint sans le supprimer |  
| `enable <num>` | `en <num>` | Réactive un breakpoint désactivé |  
| `enable once <num>` | — | Active le breakpoint pour un seul passage |  
| `commands <num>` | — | Exécute des commandes automatiquement quand le breakpoint est atteint |

La commande `commands` est extrêmement puissante pour le RE automatisé. Par exemple, pour logger tous les arguments de `strcmp` sans s'arrêter :

```
break strcmp  
commands  
  silent
  printf "strcmp(%s, %s)\n", (char*)$rdi, (char*)$rsi
  continue
end
```

### 3.4 — Watchpoints (breakpoints sur données)

| Commande | Description |  
|----------|-------------|  
| `watch *0x7fffffffe000` | S'arrête quand la valeur à cette adresse mémoire **change** (écriture) |  
| `watch $rax` | S'arrête quand la valeur de `rax` change |  
| `watch *(int*)0x404060` | S'arrête quand le `int` à l'adresse `0x404060` est modifié |  
| `rwatch *0x404060` | S'arrête quand l'adresse est **lue** (hardware watchpoint) |  
| `awatch *0x404060` | S'arrête sur lecture **ou** écriture |  
| `info watchpoints` | Liste tous les watchpoints actifs |

Les watchpoints matériels (implémentés par les registres debug du processeur DR0–DR3) sont limités à 4 simultanés et à des tailles de 1, 2, 4 ou 8 octets. Les watchpoints logiciels (fallback) sont beaucoup plus lents car GDB doit exécuter instruction par instruction et vérifier la mémoire.

### 3.5 — Catchpoints

| Commande | Description |  
|----------|-------------|  
| `catch syscall` | S'arrête sur tout appel système |  
| `catch syscall write` | S'arrête sur le syscall `write` uniquement |  
| `catch syscall 1` | S'arrête sur le syscall numéro 1 (`write` sur x86-64) |  
| `catch fork` | S'arrête quand le programme exécute `fork()` |  
| `catch exec` | S'arrête quand le programme exécute `execve()` |  
| `catch signal SIGSEGV` | S'arrête sur réception de `SIGSEGV` |  
| `catch throw` | S'arrête sur chaque `throw` C++ |  
| `catch catch` | S'arrête sur chaque `catch` C++ |  
| `catch load libcrypto` | S'arrête quand `libcrypto.so` est chargée dynamiquement |

---

## 4 — Inspection des registres

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `info registers` | `i r` | Affiche tous les registres généraux |  
| `info all-registers` | `i r a` | Affiche tous les registres y compris SSE, FPU, etc. |  
| `print $rax` | `p $rax` | Affiche la valeur de `rax` en décimal |  
| `print/x $rax` | `p/x $rax` | Affiche `rax` en hexadécimal |  
| `print/t $rax` | `p/t $rax` | Affiche `rax` en binaire |  
| `print/d $rax` | `p/d $rax` | Affiche `rax` en décimal signé |  
| `print/u $rax` | `p/u $rax` | Affiche `rax` en décimal non signé |  
| `print (char*)$rdi` | — | Affiche la chaîne pointée par `rdi` |  
| `set $rax = 0x42` | — | Modifie la valeur d'un registre |  
| `set $rip = 0x401234` | — | Déplace le pointeur d'instruction (saute à une adresse) |  
| `set $eflags \|= (1 << 6)` | — | Active le Zero Flag (ZF, bit 6 de EFLAGS) |  
| `set $eflags &= ~(1 << 6)` | — | Désactive le Zero Flag |

**Astuce RE** : modifier `$eflags` pour forcer ou empêcher un saut conditionnel est une technique rapide pour explorer un branchement sans patcher le binaire. Par exemple, inverser ZF juste avant un `jz` pour prendre ou ignorer le saut.

### Formats d'affichage de `print`

| Suffixe | Format | Exemple avec `$rax = 0x41` |  
|---------|--------|---------------------------|  
| `/x` | Hexadécimal | `0x41` |  
| `/d` | Décimal signé | `65` |  
| `/u` | Décimal non signé | `65` |  
| `/t` | Binaire | `1000001` |  
| `/o` | Octal | `0101` |  
| `/c` | Caractère | `'A'` |  
| `/f` | Flottant | `9.10844e-44` |  
| `/a` | Adresse (symbole le plus proche) | `0x41` |  
| `/s` | Chaîne C (si le registre est un pointeur) | `"ABC..."` |

---

## 5 — Inspection de la mémoire

### 5.1 — La commande `x` (examine)

La commande `x` est la commande la plus utilisée en RE pour inspecter la mémoire. Sa syntaxe complète est `x/NFS addr` où N = nombre d'éléments, F = format, S = taille.

| Paramètre | Valeurs | Signification |  
|-----------|---------|---------------|  
| **N** (nombre) | Entier positif | Nombre d'éléments à afficher |  
| **F** (format) | `x`, `d`, `u`, `o`, `t`, `c`, `s`, `i`, `a`, `f` | Même codes que `print` + `i` (instruction) et `s` (string) |  
| **S** (taille) | `b` (1 octet), `h` (2), `w` (4), `g` (8) | Taille de chaque élément |

### 5.2 — Exemples concrets

| Commande | Description |  
|----------|-------------|  
| `x/10gx $rsp` | 10 qwords en hexa depuis le sommet de la pile |  
| `x/20wx $rsp` | 20 dwords en hexa depuis le sommet de la pile |  
| `x/s $rdi` | Chaîne C pointée par `rdi` |  
| `x/10s 0x402000` | 10 chaînes consécutives depuis l'adresse `0x402000` |  
| `x/5i $rip` | 5 instructions à partir de l'instruction courante |  
| `x/20i main` | 20 instructions depuis le début de `main` |  
| `x/10i $rip-20` | Instructions autour de l'instruction courante (contexte avant) |  
| `x/40bx $rsp` | 40 octets bruts en hexa depuis le sommet de la pile |  
| `x/gx $rbp-0x8` | Un qword à `[rbp-8]` (variable locale typique) |  
| `x/4gx $rdi` | 4 qwords depuis l'adresse pointée par `rdi` (début d'un objet/struct) |  
| `x/wx 0x404060` | Un dword à une adresse fixe (variable globale, entrée GOT) |

### 5.3 — Dump mémoire vers un fichier

| Commande | Description |  
|----------|-------------|  
| `dump binary memory out.bin 0x400000 0x401000` | Dumpe une plage mémoire dans un fichier binaire |  
| `dump binary value out.bin $rdi` | Dumpe la valeur d'une expression dans un fichier |  
| `dump ihex memory out.hex 0x400000 0x401000` | Dumpe en format Intel HEX |  
| `restore out.bin binary 0x400000` | Restaure un dump binaire à une adresse mémoire |

### 5.4 — Recherche en mémoire

| Commande | Description |  
|----------|-------------|  
| `find 0x400000, 0x500000, "FLAG{"` | Cherche une chaîne dans une plage mémoire |  
| `find /b 0x400000, 0x500000, 0x90, 0x90, 0x90` | Cherche une séquence d'octets (ici : 3 × `nop`) |  
| `find /w 0x400000, +0x1000, 0xDEAD` | Cherche un word (2 octets) dans un intervalle relatif |

---

## 6 — Inspection de la pile et des frames

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `backtrace` | `bt` | Affiche la pile d'appels (call stack) |  
| `backtrace full` | `bt full` | Pile d'appels avec les variables locales de chaque frame |  
| `backtrace 5` | `bt 5` | Les 5 frames les plus récentes uniquement |  
| `frame <num>` | `f <num>` | Sélectionne la frame n° `<num>` pour l'inspection |  
| `up` | — | Monte d'une frame (vers l'appelant) |  
| `down` | — | Descend d'une frame (vers l'appelé) |  
| `info frame` | `i f` | Détails de la frame courante (adresses, registres sauvegardés) |  
| `info locals` | `i lo` | Variables locales de la frame courante (nécessite les symboles) |  
| `info args` | `i ar` | Arguments de la frame courante (nécessite les symboles) |

Sur un binaire strippé, `bt` affichera des adresses brutes sans noms de fonctions. GEF et pwndbg améliorent considérablement cet affichage (voir §11 et §12).

---

## 7 — Désassemblage et source

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `disassemble` | `disas` | Désassemble la fonction courante |  
| `disassemble main` | `disas main` | Désassemble la fonction `main` |  
| `disassemble 0x401100,0x401180` | — | Désassemble une plage d'adresses |  
| `disassemble /r main` | — | Désassemble avec les octets bruts (opcodes) |  
| `disassemble /m main` | — | Désassemble avec les lignes source entrelacées (si disponibles) |  
| `disassemble /s main` | — | Comme `/m` mais avec un meilleur formatage (GDB ≥ 7.11) |  
| `set disassembly-flavor intel` | — | Passe en syntaxe Intel (recommandé pour le RE) |  
| `set disassembly-flavor att` | — | Revient en syntaxe AT&T (défaut GDB) |  
| `list` | `l` | Affiche le code source autour de la position courante (si disponible) |  
| `list main` | `l main` | Affiche le source de `main` |

> 💡 Ajoutez `set disassembly-flavor intel` à votre `~/.gdbinit` pour ne plus jamais avoir à le taper. GEF et pwndbg utilisent Intel par défaut.

---

## 8 — Inspection des symboles et de la mémoire du processus

### 8.1 — Symboles et fonctions

| Commande | Abréviation | Description |  
|----------|-------------|-------------|  
| `info functions` | `i fu` | Liste toutes les fonctions connues |  
| `info functions regex` | — | Filtre les fonctions par expression régulière |  
| `info variables` | `i va` | Liste toutes les variables globales/statiques |  
| `info symbol 0x401234` | — | Affiche le symbole le plus proche de l'adresse |  
| `info address main` | — | Affiche l'adresse du symbole `main` |  
| `info sharedlibrary` | `i shl` | Liste les bibliothèques partagées chargées |  
| `info target` | — | Informations sur le binaire cible (entry point, sections) |  
| `info files` | `i fi` | Détails des sections et plages mémoire du binaire |  
| `maintenance info sections` | — | Liste exhaustive des sections ELF avec flags |

### 8.2 — Mappage mémoire du processus

| Commande | Description |  
|----------|-------------|  
| `info proc mappings` | Affiche le mappage mémoire (`/proc/<pid>/maps`) |  
| `!cat /proc/<pid>/maps` | Accès direct au fichier maps (si PID connu) |  
| `info proc status` | Informations sur le processus (PID, PPID, état) |

---

## 9 — Modification de données et patching

| Commande | Description |  
|----------|-------------|  
| `set *(int*)0x404060 = 42` | Écrit la valeur `42` (4 octets) à l'adresse `0x404060` |  
| `set *(char*)0x401234 = 0x90` | Écrit un octet `0x90` (`nop`) à l'adresse `0x401234` |  
| `set *(short*)$rsp = 0x1337` | Écrit un word à `[rsp]` |  
| `set {int}0x404060 = 42` | Syntaxe alternative pour écrire un `int` |  
| `set $rdi = 0x402000` | Modifie la valeur d'un registre |  
| `set $rip = 0x401250` | Force le saut à une adresse spécifique |  
| `set variable x = 10` | Modifie une variable C nommée (nécessite les symboles) |  
| `call (int)puts("hello")` | Appelle une fonction dans le contexte du programme |

**Patching inline en RE** : pour inverser un saut conditionnel à la volée pendant le débogage, vous pouvez écraser l'opcode directement. Par exemple, pour changer un `jz` (`0x74`) en `jnz` (`0x75`) :

```
set *(char*)0x401234 = 0x75
```

C'est un patch temporaire en mémoire. Pour un patch permanent, utilisez ImHex ou un script Python avec `lief`/`pwntools` (voir chapitres 21.6 et 35.1).

---

## 10 — GDB Python scripting

GDB intègre un interpréteur Python complet accessible via la commande `python` ou `py`.

### 10.1 — Commandes de base

| Commande | Description |  
|----------|-------------|  
| `python print(gdb.execute("info registers", to_string=True))` | Exécute une commande GDB et capture la sortie |  
| `python print(hex(gdb.parse_and_eval("$rax")))` | Lit la valeur d'un registre comme entier Python |  
| `python gdb.execute("set $rax = 0x42")` | Exécute une commande GDB depuis Python |  
| `source mon_script.py` | Charge et exécute un script Python GDB |

### 10.2 — Objets Python utiles

| Expression Python | Description |  
|-------------------|-------------|  
| `gdb.parse_and_eval("$rip")` | Évalue une expression GDB et retourne un objet `gdb.Value` |  
| `int(gdb.parse_and_eval("$rax"))` | Convertit un registre en entier Python |  
| `gdb.inferiors()[0].read_memory(addr, size)` | Lit `size` octets à l'adresse `addr` |  
| `gdb.inferiors()[0].write_memory(addr, data)` | Écrit des octets à une adresse |  
| `gdb.breakpoints()` | Liste les breakpoints sous forme d'objets Python |  
| `gdb.selected_frame()` | Frame de pile actuellement sélectionnée |  
| `gdb.selected_frame().read_register("rax")` | Lit un registre via l'objet frame |  
| `gdb.events.stop.connect(callback)` | Enregistre un callback appelé à chaque arrêt |

### 10.3 — Exemple : logger les appels à `strcmp`

```python
import gdb

class StrcmpLogger(gdb.Breakpoint):
    def __init__(self):
        super().__init__("strcmp", internal=True)
        self.silent = True

    def stop(self):
        rdi = int(gdb.parse_and_eval("$rdi"))
        rsi = int(gdb.parse_and_eval("$rsi"))
        s1 = gdb.inferiors()[0].read_memory(rdi, 64).tobytes().split(b'\x00')[0]
        s2 = gdb.inferiors()[0].read_memory(rsi, 64).tobytes().split(b'\x00')[0]
        print(f"strcmp({s1.decode(errors='replace')}, {s2.decode(errors='replace')})")
        return False  # False = ne pas s'arrêter, continuer l'exécution

StrcmpLogger()
```

---

## 11 — Commandes GEF

GEF (*GDB Enhanced Features*) est une extension mono-fichier qui enrichit GDB avec des commandes de haut niveau orientées exploitation et RE. Elle s'installe en ajoutant une seule ligne à `~/.gdbinit`.

### 11.1 — Affichage contextuel

GEF affiche automatiquement un « contexte » à chaque arrêt : registres, pile, code désassemblé et code source (si disponible). Cet affichage est contrôlé par les commandes suivantes :

| Commande | Description |  
|----------|-------------|  
| `context` | Force le réaffichage du contexte |  
| `gef config context.layout` | Affiche/modifie les panneaux affichés (`regs`, `stack`, `code`, `source`, `threads`, `extra`) |  
| `gef config context.nb_lines_code 15` | Nombre de lignes de désassemblage affichées |  
| `gef config context.nb_lines_stack 10` | Nombre de lignes de pile affichées |  
| `gef config context.show_registers_raw true` | Affiche les valeurs brutes des registres |

### 11.2 — Informations sur le binaire et le processus

| Commande | Description |  
|----------|-------------|  
| `checksec` | Affiche les protections du binaire (PIE, NX, canary, RELRO, Fortify) |  
| `vmmap` | Affiche le mappage mémoire du processus avec permissions et noms de fichiers |  
| `vmmap stack` | Filtre le vmmap sur la pile |  
| `vmmap libc` | Filtre sur la libc |  
| `xfiles` | Liste les sections du binaire avec leurs adresses en mémoire |  
| `entry-break` | Place un breakpoint sur le point d'entrée réel du binaire |  
| `got` | Affiche la table GOT avec les adresses résolues |  
| `canary` | Affiche la valeur courante du stack canary |  
| `elf-info` | Affiche les informations de l'en-tête ELF |

### 11.3 — Recherche et exploration mémoire

| Commande | Description |  
|----------|-------------|  
| `search-pattern "FLAG{"` | Cherche une chaîne dans toute la mémoire du processus |  
| `search-pattern 0xdeadbeef` | Cherche une valeur hexadécimale |  
| `search-pattern "FLAG{" stack` | Cherche uniquement dans la pile |  
| `search-pattern "FLAG{" heap` | Cherche uniquement dans le heap |  
| `xinfo 0x7fff12345678` | Affiche des informations sur une adresse (à quel mapping elle appartient) |  
| `dereference $rsp 20` | Déréférence récursivement 20 entrées depuis `rsp` (suit les pointeurs) |  
| `hexdump byte $rsp 64` | Dump hexadécimal de 64 octets depuis `rsp` |  
| `hexdump qword $rsp 8` | Dump de 8 qwords depuis `rsp` |

### 11.4 — Analyse de heap

| Commande | Description |  
|----------|-------------|  
| `heap chunks` | Liste tous les chunks alloués sur le heap |  
| `heap bins` | Affiche l'état des bins de l'allocateur (fastbins, unsorted, small, large) |  
| `heap arenas` | Affiche les arenas de malloc |  
| `heap chunk <addr>` | Détaille un chunk spécifique (taille, flags, contenu) |

### 11.5 — Exploitation et gadgets

| Commande | Description |  
|----------|-------------|  
| `rop --search "pop rdi"` | Cherche des gadgets ROP dans le binaire |  
| `rop --search "pop rdi" --range 0x400000-0x500000` | Cherche dans une plage d'adresses spécifique |  
| `format-string-helper` | Aide à construire des chaînes de format pour les vulnérabilités format string |  
| `pattern create 200` | Crée un pattern De Bruijn de 200 octets (pour trouver l'offset d'un overflow) |  
| `pattern offset 0x41416141` | Calcule l'offset correspondant à une valeur trouvée dans un registre |

### 11.6 — Divers

| Commande | Description |  
|----------|-------------|  
| `gef save` | Sauvegarde la configuration GEF courante dans `~/.gef.rc` |  
| `gef restore` | Restaure la configuration sauvegardée |  
| `gef install <plugin>` | Installe un plugin GEF supplémentaire |  
| `pcustom` | Gère les structures personnalisées pour la visualisation mémoire |  
| `highlight add "keyword" "color"` | Colore un mot-clé dans la sortie |  
| `aliases add <alias> <command>` | Crée un alias de commande |

---

## 12 — Commandes pwndbg

pwndbg est une extension orientée exploitation avec un affichage riche et de nombreuses commandes pour l'analyse de heap, le RE et le développement d'exploits. Ses commandes sont plus nombreuses que celles de GEF et leur nommage diffère parfois.

### 12.1 — Affichage contextuel

pwndbg affiche automatiquement un contexte riche à chaque arrêt, similaire à GEF mais avec un formatage différent.

| Commande | Description |  
|----------|-------------|  
| `context` | Force le réaffichage du contexte |  
| `contextoutput <section> <cmd>` | Redirige une section du contexte vers un terminal séparé |  
| `set context-sections regs disasm code stack backtrace` | Configure les sections affichées |  
| `set context-code-lines 15` | Nombre de lignes de code désassemblé |  
| `set context-stack-lines 10` | Nombre de lignes de pile |

### 12.2 — Informations sur le binaire et le processus

| Commande | Description |  
|----------|-------------|  
| `checksec` | Protections du binaire (identique à GEF) |  
| `vmmap` | Mappage mémoire du processus |  
| `vmmap libc` | Filtre le vmmap |  
| `aslr` | Affiche l'état de l'ASLR |  
| `got` | Table GOT avec adresses résolues |  
| `plt` | Table PLT |  
| `gotplt` | GOT et PLT combinées |  
| `canary` | Valeur du stack canary |  
| `piebase` | Adresse de base du binaire PIE |  
| `libs` | Liste les bibliothèques chargées |  
| `entry` | Adresse du point d'entrée |

### 12.3 — Recherche et exploration mémoire

| Commande | Description |  
|----------|-------------|  
| `search --string "FLAG{"` | Cherche une chaîne en mémoire |  
| `search --dword 0xdeadbeef` | Cherche un dword |  
| `search --qword 0xdeadbeefcafebabe` | Cherche un qword |  
| `search --string "FLAG{" --writable` | Cherche uniquement dans les pages accessibles en écriture |  
| `search --string "FLAG{" --executable` | Cherche uniquement dans les pages exécutables |  
| `xinfo <addr>` | Informations détaillées sur une adresse |  
| `telescope $rsp 20` | Déréférence récursive de 20 entrées (suit les chaînes de pointeurs) |  
| `hexdump $rsp 64` | Dump hexadécimal |  
| `dq $rsp 10` | 10 qwords depuis `rsp` (format compact) |  
| `dd $rsp 10` | 10 dwords depuis `rsp` |  
| `db $rsp 40` | 40 octets depuis `rsp` |  
| `dc $rsp 80` | Dump avec caractères ASCII (comme `xxd`) |

### 12.4 — Analyse de heap (glibc ptmalloc2)

pwndbg excelle dans l'analyse du heap glibc. Ces commandes sont parmi les plus avancées de l'extension.

| Commande | Description |  
|----------|-------------|  
| `vis_heap_chunks` | Visualisation graphique colorée des chunks du heap |  
| `vis_heap_chunks 0x555555559000 10` | Visualise 10 chunks à partir d'une adresse |  
| `heap` | Vue d'ensemble du heap (arenas, top chunk) |  
| `bins` | État de tous les bins (fastbins, tcache, unsorted, small, large) |  
| `fastbins` | Fastbins uniquement |  
| `unsortedbin` | Unsorted bin uniquement |  
| `smallbins` | Small bins uniquement |  
| `largebins` | Large bins uniquement |  
| `tcachebins` | Tcache bins uniquement |  
| `tcache` | Détails complets du tcache |  
| `mp_` | Affiche la structure `mp_` de malloc (paramètres globaux) |  
| `malloc_chunk <addr>` | Détaille un chunk spécifique |  
| `top_chunk` | Affiche le top chunk (wilderness) |  
| `arena` | Affiche l'arena courante |  
| `arenas` | Liste toutes les arenas |  
| `find_fake_fast <addr>` | Cherche des faux fast chunks utilisables pour un fastbin attack |

### 12.5 — Désassemblage amélioré

| Commande | Description |  
|----------|-------------|  
| `nearpc` | Désassemble autour de `rip` avec coloration syntaxique |  
| `nearpc 30` | 30 instructions autour de `rip` |  
| `u <addr>` | Désassemble à partir d'une adresse (alias de `nearpc`) |  
| `emulate 20` | Émule les 20 prochaines instructions (prédit le chemin d'exécution) |  
| `pdisass` | Désassemblage amélioré avec annotations |  
| `nextcall` | Continue jusqu'au prochain `call` |  
| `nextjmp` | Continue jusqu'au prochain saut |  
| `nextret` | Continue jusqu'au prochain `ret` |  
| `nextsyscall` | Continue jusqu'au prochain `syscall` |  
| `stepret` | Exécute pas à pas jusqu'au `ret` de la fonction courante |

### 12.6 — Exploitation et gadgets

| Commande | Description |  
|----------|-------------|  
| `rop` | Cherche tous les gadgets ROP |  
| `rop --grep "pop rdi"` | Filtre les gadgets |  
| `ropper --search "pop rdi"` | Interface vers l'outil Ropper |  
| `cyclic 200` | Génère un pattern De Bruijn de 200 octets |  
| `cyclic -l 0x6161616b` | Calcule l'offset correspondant à une valeur |  
| `cyclic -l aaak` | Calcule l'offset à partir de la chaîne ASCII |

### 12.7 — Divers

| Commande | Description |  
|----------|-------------|  
| `distance <addr1> <addr2>` | Calcule la distance entre deux adresses |  
| `plist <addr>` | Affiche une liste chaînée en mémoire |  
| `errno` | Affiche la valeur courante de `errno` avec sa signification |  
| `mprotect <addr> <size> <perms>` | Change les permissions mémoire (nécessite que le programme appelle `mprotect`) |  
| `procinfo` | Informations complètes sur le processus |  
| `regs` | Affichage compact et coloré de tous les registres |

---

## 13 — Comparaison GEF vs pwndbg : équivalences

| Fonctionnalité | GEF | pwndbg |  
|----------------|-----|--------|  
| Mappage mémoire | `vmmap` | `vmmap` |  
| Protections binaire | `checksec` | `checksec` |  
| Déréférencement récursif | `dereference $rsp 20` | `telescope $rsp 20` |  
| Recherche en mémoire | `search-pattern "str"` | `search --string "str"` |  
| Info sur une adresse | `xinfo <addr>` | `xinfo <addr>` |  
| Stack canary | `canary` | `canary` |  
| Table GOT | `got` | `got` / `gotplt` |  
| Dump hexa | `hexdump byte $rsp 64` | `hexdump $rsp 64` / `db $rsp 64` |  
| Heap bins | `heap bins` | `bins` |  
| Heap chunks visuels | `heap chunks` | `vis_heap_chunks` |  
| Gadgets ROP | `rop --search "..."` | `rop --grep "..."` |  
| Pattern De Bruijn (create) | `pattern create 200` | `cyclic 200` |  
| Pattern De Bruijn (lookup) | `pattern offset 0x...` | `cyclic -l 0x...` |  
| Exécuter jusqu'au prochain call | — | `nextcall` |  
| Exécuter jusqu'au prochain ret | — | `stepret` / `nextret` |  
| Émulation de code | — | `emulate 20` |  
| Base PIE | — | `piebase` |

---

## 14 — Fichier `~/.gdbinit` recommandé

Voici un `.gdbinit` minimal recommandé pour le RE. Adaptez-le à votre extension (GEF ou pwndbg — n'activez qu'une seule à la fois).

```
# ─── GDB natif ─────────────────────────────────
set disassembly-flavor intel  
set pagination off  
set confirm off  
set print pretty on  
set print array on  
set print elements 256  

# ─── Historique des commandes ──────────────────
set history save on  
set history filename ~/.gdb_history  
set history size 10000  

# ─── Suivi de fork ─────────────────────────────
set follow-fork-mode parent  
set detach-on-fork on  

# ─── Extension (décommenter UNE seule ligne) ──
# source ~/.gdbinit-gef.py        # GEF
# source ~/pwndbg/gdbinit.py      # pwndbg
# source ~/peda/peda.py           # PEDA
```

---

## 15 — Raccourcis clavier et astuces de productivité

| Raccourci / Astuce | Description |  
|---------------------|-------------|  
| `Entrée` (sans commande) | Répète la dernière commande (très utile avec `ni`, `si`, `x`) |  
| `Ctrl+C` | Interrompt le programme en cours d'exécution (comme `kill -INT`) |  
| `Ctrl+L` | Efface l'écran |  
| `Ctrl+R` | Recherche dans l'historique des commandes |  
| `!commande` | Exécute une commande shell depuis GDB |  
| `shell commande` | Identique à `!commande` |  
| `define mycommand` ... `end` | Crée une macro (commande personnalisée) |  
| `pipe <cmd> \| grep pattern` | Pipe la sortie d'une commande GDB vers grep (GDB ≥ 10) |

---

> 📚 **Pour aller plus loin** :  
> - **Annexe D** — [Cheat sheet Radare2 / Cutter](/annexes/annexe-d-cheatsheet-radare2.md) — la fiche de référence de l'autre débogueur/désassembleur majeur.  
> - **Chapitre 11** — [Débogage avec GDB](/11-gdb/README.md) — couverture pédagogique complète des commandes GDB pour le RE.  
> - **Chapitre 12** — [GDB amélioré : PEDA, GEF, pwndbg](/12-gdb-extensions/README.md) — installation, comparaison et cas d'usage des extensions.  
> - **Documentation GDB** — `help <commande>` dans GDB, ou le [manuel officiel](https://sourceware.org/gdb/current/onlinedocs/gdb/).  
> - **GEF** — [https://hugsy.github.io/gef/](https://hugsy.github.io/gef/) — documentation officielle.  
> - **pwndbg** — [https://pwndbg.re/](https://pwndbg.re/) — documentation officielle.

⏭️ [Cheat sheet Radare2 / Cutter](/annexes/annexe-d-cheatsheet-radare2.md)
