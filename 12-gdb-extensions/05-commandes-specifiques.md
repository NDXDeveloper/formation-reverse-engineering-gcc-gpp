🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 12.5 — Commandes utiles spécifiques à chaque extension

> **Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg**  
> **Partie III — Analyse Dynamique**

---

## Vue d'ensemble

Les sections précédentes ont couvert les fonctionnalités phares des extensions GDB : l'affichage contextuel (12.2), la recherche de gadgets ROP (12.3) et l'analyse de la heap (12.4). Mais chaque extension embarque des dizaines de commandes supplémentaires qui accélèrent le travail quotidien du reverse engineer. Cette section passe en revue les commandes les plus utiles qui n'ont pas encore été abordées, organisées par extension puis par thématique transversale.

---

## Commandes spécifiques à PEDA

Malgré son développement ralenti, PEDA reste pertinente pour certaines commandes simples et bien pensées.

### `checksec` — vérification des protections

```
gdb-peda$ checksec  
CANARY    : ENABLED  
FORTIFY   : disabled  
NX        : ENABLED  
PIE       : ENABLED  
RELRO     : FULL  
```

PEDA a popularisé cette commande qui affiche les protections de sécurité du binaire en cours de débogage. C'est l'équivalent depuis GDB de l'outil `checksec` en ligne de commande (section 5.6), avec l'avantage d'opérer sur le processus chargé en mémoire plutôt que sur le fichier statique. GEF et pwndbg proposent chacun leur propre implémentation de `checksec`, mais c'est PEDA qui l'a introduite en premier dans le contexte GDB.

### `searchmem` / `find` — recherche de motifs en mémoire

```
gdb-peda$ searchmem "password"  
Searching for 'password' in: binary ranges  
Found 2 results, display max 2 items:  
keygenme_O0 : 0x555555556008 ("password_expected")  
[stack]     : 0x7fffffffe0b4 ("password_user_input")
```

`searchmem` parcourt la mémoire du processus à la recherche d'une chaîne ASCII, d'une valeur hexadécimale ou d'un motif d'octets. On peut restreindre la recherche à une région spécifique :

```
gdb-peda$ searchmem 0xdeadbeef stack  
gdb-peda$ searchmem "/bin/sh" libc  
```

Les cibles de recherche reconnues sont `binary`, `stack`, `heap`, `libc`, `all`, ou une plage d'adresses explicite `start-end`.

### `xormem` — XOR d'une région mémoire

```
gdb-peda$ xormem 0x555555559290 0x5555555592b0 0x42
```

Cette commande applique un XOR avec une clé donnée sur une plage mémoire. C'est un utilitaire de niche mais précieux lors de l'analyse de binaires qui utilisent un encodage XOR simple pour masquer des chaînes ou des données — un pattern fréquent dans les malwares simples (chapitres 27–28). `xormem` permet de décoder une zone directement dans la mémoire du processus sans script externe.

### `procinfo` — informations sur le processus

```
gdb-peda$ procinfo  
exe: /home/user/binaries/ch12-keygenme/keygenme_O0  
pid: 12345  
ppid: 12300  
uid: [1000, 1000, 1000, 1000]  
gid: [1000, 1000, 1000, 1000]  
```

`procinfo` regroupe les informations extraites de `/proc/pid/` en une seule commande : chemin de l'exécutable, PID, UID/GID, et d'autres métadonnées du processus. C'est un raccourci pratique pour confirmer que le bon binaire est chargé.

### `elfheader` et `elfsymbol` — structures ELF depuis le débogueur

```
gdb-peda$ elfheader
.interp    = 0x555555554318
.note.gnu.property = 0x555555554338
.gnu.hash  = 0x555555554368
.dynsym    = 0x555555554390
.dynstr    = 0x555555554438
.text      = 0x555555555060
.rodata    = 0x555555556000
...

gdb-peda$ elfsymbol printf  
printf@plt = 0x555555555030  
```

Ces commandes extraient les informations des tables ELF à partir de la mémoire du processus. `elfheader` liste les sections avec leurs adresses effectives (après relocation), et `elfsymbol` cherche un symbole par nom. Cela évite de basculer vers `readelf` ou `objdump` dans un autre terminal pendant une session de débogage.

---

## Commandes spécifiques à GEF

### `xinfo` — tout savoir sur une adresse

```
gef➤ xinfo 0x7ffff7e15a80
──────────────────── xinfo: 0x7ffff7e15a80 ────────────────────
Page: 0x7ffff7dd5000 → 0x7ffff7f5d000 (size=0x188000)  
Permissions: r-x  
Pathname: /lib/x86_64-linux-gnu/libc.so.6  
Offset (from page): 0x40a80  
Inode: 1835041  
Segment: .text (libc.so.6)  
Symbol: __libc_start_call_main+128  
```

`xinfo` est l'une des commandes les plus précieuses de GEF. Elle prend n'importe quelle adresse en argument et retourne tout ce que GEF sait à son sujet : la page mémoire qui la contient, les permissions, le fichier mappé, l'offset dans le fichier, la section ELF et le symbole le plus proche. C'est un outil d'investigation universel. Quand on voit une adresse inconnue dans un registre ou sur la pile, `xinfo` répond immédiatement à la question « c'est quoi, cette adresse ? ».

### `vmmap` — cartographie mémoire enrichie

```
gef➤ vmmap
[ Legend: Code | Heap | Stack | Writable | ReadOnly ]
Start              End                Size               Offset  Perm  Path
0x555555554000     0x555555555000     0x1000             0x0     r--   /home/user/keygenme_O0
0x555555555000     0x555555556000     0x1000             0x1000  r-x   /home/user/keygenme_O0
0x555555556000     0x555555557000     0x1000             0x2000  r--   /home/user/keygenme_O0
0x555555557000     0x555555559000     0x2000             0x2000  rw-   /home/user/keygenme_O0
0x555555559000     0x55555557a000     0x21000            0x0     rw-   [heap]
0x7ffff7dd5000     0x7ffff7f5d000     0x188000           0x0     r-x   /lib/.../libc.so.6
...
0x7ffffffde000     0x7ffffffff000     0x21000            0x0     rw-   [stack]
```

`vmmap` existe dans les trois extensions, mais GEF enrichit la sortie avec une légende colorée qui distingue visuellement les régions de code, de tas, de pile et les régions en écriture. pwndbg propose un affichage similaire. La commande accepte un filtre optionnel :

```
gef➤ vmmap libc  
gef➤ vmmap heap  
gef➤ vmmap stack  
```

### `pattern create` / `pattern search` — motifs De Bruijn

Ces commandes servent à calculer l'offset exact d'un débordement de buffer. On génère un motif cyclique unique, on l'envoie comme entrée au programme, et on identifie quelle portion du motif a écrasé un registre ou une adresse de retour.

```
gef➤ pattern create 200  
aaaaaaaabaaaaaaacaaaaaaadaaaa...  

gef➤ run  
Entrez le mot de passe : aaaaaaaabaaaaaaacaaaaaaadaaaa...  
Program received signal SIGSEGV  

gef➤ pattern search $rsp
[+] Found at offset 72 (little-endian search) likely
```

GEF génère un motif De Bruijn où chaque sous-séquence de 8 caractères (sur x86-64) est unique. Après le crash, `pattern search` prend la valeur corrompue d'un registre ou d'une adresse et retrouve sa position dans le motif, donnant directement l'offset en octets entre le début du buffer et le point d'écrasement.

pwndbg offre des commandes équivalentes nommées `cyclic` et `cyclic -l` :

```
pwndbg> cyclic 200  
pwndbg> cyclic -l 0x6161616161616166  
```

PEDA utilise `pattern_create` et `pattern_search` avec des underscores.

### `got` — table GOT en un coup d'œil

```
gef➤ got  
GOT protection: Full RELRO | GOT functions: 5  

[0x555555557fd8] puts@GLIBC_2.2.5  →  0x7ffff7e52420
[0x555555557fe0] printf@GLIBC_2.2.5  →  0x7ffff7e37e50
[0x555555557fe8] strcmp@GLIBC_2.2.5  →  0x7ffff7e6db70
[0x555555557ff0] malloc@GLIBC_2.2.5  →  0x7ffff7e92070
[0x555555557ff8] free@GLIBC_2.2.5  →  0x7ffff7e92460
```

La commande `got` affiche la Global Offset Table avec les adresses résolues de chaque fonction importée. Cela montre d'un coup toutes les fonctions de bibliothèque utilisées par le programme et leurs adresses effectives en mémoire. C'est un raccourci pour `x/gx adresse` sur chaque entrée GOT, et un moyen rapide de vérifier si le lazy binding a eu lieu (si une entrée pointe vers le stub PLT, la résolution n'a pas encore été effectuée).

pwndbg propose la même commande `got`. PEDA utilise `got` ou `elfgot` selon les versions.

### `highlight` — coloration dynamique de motifs

```
gef➤ highlight add "0xdeadbeef" yellow  
gef➤ highlight add "strcmp" red  
```

`highlight` ajoute une règle de coloration persistante : toute occurrence du motif spécifié dans la sortie de GDB sera colorisée automatiquement. C'est utile pour tracer visuellement une valeur sentinelle à travers les contextes successifs, ou pour mettre en évidence les appels à une fonction d'intérêt sans poser de breakpoint.

```
gef➤ highlight list                    # lister les règles actives  
gef➤ highlight remove "0xdeadbeef"     # retirer une règle  
```

### `edit-flags` — modifier les flags CPU

```
gef➤ edit-flags +zero       # activer le Zero Flag  
gef➤ edit-flags -carry       # désactiver le Carry Flag  
```

Cette commande permet de modifier directement les flags du registre `RFLAGS` par nom. En GDB vanilla, il faut calculer la valeur numérique complète de `RFLAGS` et la patcher avec `set $eflags = ...`. Avec `edit-flags`, on manipule chaque flag individuellement par son nom lisible. C'est particulièrement utile pour forcer un saut conditionnel : si le programme est sur un `jz` et qu'on veut prendre le saut, on active `ZF` avec `edit-flags +zero` avant de `stepi`.

### `aliases` — raccourcis personnalisés

GEF permet de définir des aliases pour des commandes fréquemment utilisées :

```
gef➤ aliases add "ctx" "context"  
gef➤ aliases add "tele" "dereference"  
```

Ces aliases sont sauvegardés dans `~/.gef.rc` via `gef save` et persistent entre les sessions.

---

## Commandes spécifiques à pwndbg

### `nextcall` / `nextjmp` / `nextret` — navigation sémantique

Ces commandes avancent l'exécution jusqu'à la prochaine instruction d'un type donné :

```
pwndbg> nextcall          # continue jusqu'au prochain `call`  
pwndbg> nextjmp           # continue jusqu'au prochain saut (conditionnel ou non)  
pwndbg> nextret           # continue jusqu'au prochain `ret`  
pwndbg> nextsyscall       # continue jusqu'au prochain `syscall`  
```

En GDB vanilla, obtenir le même résultat nécessite de poser un breakpoint temporaire sur chaque instruction du type voulu, ou d'utiliser des `stepi` en boucle. Ces commandes pwndbg transforment le parcours du code en une navigation par points d'intérêt sémantiques. `nextret` est particulièrement utile pour sortir rapidement d'une fonction sans connaître son épilogue exact. `nextcall` permet de parcourir un programme en ne s'arrêtant que sur les appels de fonction, ce qui donne une vue de haut niveau du flux d'exécution.

### `search` — recherche polymorphe en mémoire

```
pwndbg> search --string "password"  
pwndbg> search --dword 0xdeadbeef  
pwndbg> search --qword 0x00007ffff7e52420  
pwndbg> search --bytes "48 89 e5"  
pwndbg> search --string "/bin/sh" --executable  
```

La commande `search` de pwndbg est plus expressive que les équivalents dans PEDA et GEF. Elle accepte un type explicite (`--string`, `--byte`, `--word`, `--dword`, `--qword`, `--bytes` pour des séquences d'octets arbitraires) et un filtre de permissions (`--executable`, `--writable`). Le filtre par permissions est précieux : `--executable` restreint la recherche aux pages exécutables (utile pour chercher des gadgets ou des séquences d'opcodes), tandis que `--writable` cible les régions de données modifiables (utile pour trouver où écrire lors d'une exploitation).

### `regs` — filtrage des registres

```
pwndbg> regs  
pwndbg> regs --all            # inclut les registres SIMD, segments, flags détaillés  
```

Sans argument, `regs` affiche les registres généraux avec le même formatage que la section contexte. Avec `--all`, il inclut les registres de segments (`cs`, `ds`, `ss`, etc.), les registres SIMD (`xmm0`–`xmm15`) et un décodage détaillé de `RFLAGS` flag par flag. C'est un raccourci pour `info all-registers` de GDB vanilla, mais avec le déréférencement récursif et la coloration de pwndbg.

### `procinfo` — informations détaillées sur le processus

```
pwndbg> procinfo  
exe     /home/user/binaries/ch12-keygenme/keygenme_O0  
pid     12345  
tid     12345  
ppid    12300  
uid     1000  
gid     1000  
groups  [1000, 27, 110]  
fd[0]   /dev/pts/3  
fd[1]   /dev/pts/3  
fd[2]   /dev/pts/3  
fd[3]   socket:[54321]  
```

La version pwndbg de `procinfo` est plus détaillée que celle de PEDA : elle inclut les descripteurs de fichiers ouverts (file descriptors), ce qui est extrêmement utile pour comprendre les communications d'un binaire réseau (chapitre 23). Un `fd[3]` de type `socket` indique immédiatement une connexion réseau active.

### `plt` — table PLT

```
pwndbg> plt  
Section .plt 0x555555555020-0x555555555060:  
  0x555555555030: puts@plt
  0x555555555040: printf@plt
  0x555555555050: strcmp@plt
```

La commande `plt` liste les entrées de la Procedure Linkage Table avec les noms des fonctions importées. Combinée avec `got`, elle donne une vue complète du mécanisme de résolution dynamique vu au chapitre 2 (section 2.9).

### `distance` — calcul d'offset entre deux adresses

```
pwndbg> distance $rsp $rbp
0x7fffffffe090->0x7fffffffe0c0 is 0x30 bytes (0x6 words)
```

`distance` calcule la différence entre deux adresses et l'exprime en octets et en mots machine. C'est un raccourci simple mais qui évite les calculs mentaux en hexadécimal lors de la détermination de la taille d'une frame de pile ou de la distance entre deux buffers.

### `canary` — valeur du stack canary

```
pwndbg> canary  
AT_RANDOM = 0x7fffffffe2c9  
Found valid canaries on the stacks:  
00:0000│  0x7fffffffe0b8 ◂— 0xa3f2e1d0c9b8a7f6
```

Cette commande localise et affiche la valeur du stack canary du programme en cours. Elle cherche dans le TLS (Thread-Local Storage) et sur la pile pour retrouver la valeur originale et vérifier si elle a été corrompue. Connaître la valeur du canary est utile lors d'un audit : cela permet de construire un exploit de test qui préserve le canary, ou de vérifier qu'un overflow détecté atteint effectivement le canary.

### `patchelf` et `dumpargs`

`dumpargs` affiche les arguments de la fonction sur le point d'être appelée en interprétant la convention d'appel System V AMD64 :

```
pwndbg> dumpargs
        rdi = 0x7fffffffe0b0 → "user_input"
        rsi = 0x555555556004 → "expected_key"
```

Cette commande est implicitement active dans le contexte de pwndbg (les arguments sont annotés près des `call`), mais elle peut être invoquée manuellement quand on se trouve sur une instruction `call` et qu'on veut un affichage propre des arguments sans le reste du contexte.

---

## Commandes transversales : mêmes besoins, syntaxes différentes

Certaines opérations sont disponibles dans les trois extensions mais avec des noms ou des syntaxes différents. Le tableau suivant sert de référence rapide pour traduire entre les trois.

| Besoin | PEDA | GEF | pwndbg |  
|---|---|---|---|  
| Recherche de chaîne en mémoire | `searchmem "str"` | `grep memory "str"` | `search --string "str"` |  
| Recherche d'octets en mémoire | `searchmem 0xDEAD` | `scan section 0xDE 0xAD` | `search --bytes "DE AD"` |  
| Déréférencement récursif de pile | `telescope 20` (basique) | `dereference $rsp 20` | `telescope $rsp 20` |  
| Mapping mémoire | `vmmap` | `vmmap` | `vmmap` |  
| Protections du binaire | `checksec` | `checksec` | `checksec` |  
| Table GOT | `elfgot` / `got` | `got` | `got` |  
| Table PLT | — | — | `plt` |  
| Informations sur une adresse | `xinfo addr` (limité) | `xinfo addr` | `xinfo addr` |  
| Motif De Bruijn (création) | `pattern_create 200` | `pattern create 200` | `cyclic 200` |  
| Motif De Bruijn (recherche) | `pattern_search` | `pattern search $reg` | `cyclic -l val` |  
| Exécuter jusqu'au prochain `call` | — | — | `nextcall` |  
| Exécuter jusqu'au prochain `ret` | — | — | `nextret` |  
| Modifier un flag CPU | `set $eflags \|= 0x40` | `edit-flags +zero` | `set $eflags \|= 0x40` |  
| Valeur du canary | — | `canary` | `canary` |  
| Distance entre deux adresses | — | — | `distance a b` |  
| Afficher le contexte à la demande | — | `context` | `context` |

Les cases vides indiquent que la commande n'existe pas nativement dans l'extension concernée. Dans la plupart des cas, on peut obtenir le même résultat avec des commandes GDB vanilla plus verbeuses — les extensions offrent simplement un raccourci ergonomique.

---

## Écrire ses propres commandes

Les trois extensions étant écrites en Python, elles servent également de modèle pour créer ses propres commandes GDB personnalisées via l'API Python vue en section 11.8.

Dans GEF, l'architecture en classes facilite l'ajout d'une commande. Chaque commande est une classe qui hérite de `GenericCommand` :

```python
# ~/.gef-custom.py — commande GEF personnalisée
@register
class MyCustomCommand(GenericCommand):
    """Description de ma commande."""
    _cmdline_ = "mycommand"
    _syntax_ = f"{_cmdline_} [args]"

    def do_invoke(self, argv):
        gef_print(f"RSP = {gef.arch.register('$rsp'):#x}")
        gef_print(f"RIP = {gef.arch.register('$rip'):#x}")
```

Pour que cette commande soit chargée automatiquement, ajouter `source ~/.gef-custom.py` dans `~/.gdbinit` après le chargement de GEF.

Dans pwndbg, le mécanisme est similaire mais repose sur le framework interne de pwndbg. Pour une commande simple, l'utilisation directe de l'API GDB Python (section 11.8) est plus portable :

```python
# ~/.gdb-custom.py — commande GDB Python standard (compatible avec toute extension)
import gdb

class DumpStrcmpArgs(gdb.Command):
    """Affiche les arguments de strcmp au breakpoint courant."""

    def __init__(self):
        super().__init__("dump-strcmp", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        rdi = gdb.parse_and_eval("$rdi")
        rsi = gdb.parse_and_eval("$rsi")
        s1 = gdb.execute(f"x/s {int(rdi)}", to_string=True)
        s2 = gdb.execute(f"x/s {int(rsi)}", to_string=True)
        gdb.write(f"strcmp arg1: {s1}strcmp arg2: {s2}")

DumpStrcmpArgs()
```

Cette commande, chargée via `source ~/.gdb-custom.py`, fonctionne avec n'importe quelle extension et dans GDB vanilla. L'idée est de construire progressivement un fichier de commandes personnalisées qui complète l'extension choisie pour les besoins récurrents de ses analyses — c'est la logique du « toolkit personnel » développée au chapitre 35.

---

## Résumé : quelle extension pour quel usage

Après avoir parcouru l'ensemble des commandes de ce chapitre, le choix d'extension peut se résumer ainsi.

**PEDA** reste un bon outil d'apprentissage. Son code est lisible, ses commandes sont simples, et elle fonctionne partout sans dépendance. Pour un usage quotidien en 2024+, elle est supplantée par GEF et pwndbg sur tous les plans sauf la simplicité d'installation et la lisibilité du code source.

**GEF** est le couteau suisse portable. Son fichier unique, son absence de dépendances obligatoires, son support multi-architecture et sa configurabilité granulaire en font l'extension idéale pour le débogage distant, l'embarqué et l'usage généraliste. Les commandes `xinfo`, `pattern create/search`, `edit-flags`, `got` et `highlight` en font un outil complet pour le reverse engineering quotidien.

**pwndbg** est l'arsenal spécialisé. Ses commandes de heap (`vis_heap_chunks`, `bins`, `tcachebins`), sa navigation sémantique (`nextcall`, `nextret`), sa recherche mémoire polymorphe (`search`), et ses annotations contextuelles (arguments des fonctions libc, prédiction des sauts) en font l'extension la plus productive pour l'analyse de binaires complexes et l'exploitation de vulnérabilités.

Le mécanisme d'alias décrit en section 12.1 permet de basculer entre les trois en une commande. La meilleure approche est de choisir une extension par défaut pour le travail courant et de basculer ponctuellement quand une fonctionnalité spécifique de l'autre est nécessaire.

---


⏭️ [🎯 Checkpoint : tracer l'exécution complète de `keygenme_O0` avec GEF, capturer le moment de la comparaison](/12-gdb-extensions/checkpoint.md)
