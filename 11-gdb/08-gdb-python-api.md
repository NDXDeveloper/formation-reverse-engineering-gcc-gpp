🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.8 — GDB Python API — scripting et automatisation

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Dépasser les limites du scripting interactif

Les sections précédentes ont montré comment automatiser des tâches simples avec les blocs `commands` et les convenience variables (`$count`, `$base`, etc.). Ces mécanismes suffisent pour du logging basique — afficher les arguments de `strcmp` à chaque appel, compter les itérations d'une boucle. Mais ils atteignent rapidement leurs limites :

- Le langage de commandes GDB n'a pas de structures de données (pas de listes, dictionnaires, ensembles).  
- La gestion des chaînes est rudimentaire (pas de découpage, formatage avancé, expressions régulières).  
- Il n'y a pas de gestion d'erreurs (un `x/s` sur une adresse invalide interrompt le script).  
- On ne peut pas interagir avec le système de fichiers, le réseau, ou d'autres outils.  
- L'écriture de logique conditionnelle complexe avec `if`/`else`/`while` en syntaxe GDB est pénible et illisible.

L'**API Python de GDB** résout toutes ces limitations. Depuis GDB 7.0 (2009), un interpréteur Python complet est intégré dans GDB. Il expose l'intégralité de l'état du débogueur — breakpoints, registres, mémoire, frames, threads, symboles — sous forme d'objets Python manipulables avec toute la puissance du langage. On peut écrire des scripts Python qui s'exécutent dans le contexte de GDB, accèdent aux mêmes informations que les commandes interactives, et produisent des résultats structurés.

Pour le reverse engineer, c'est un changement de catégorie : on passe d'un outil interactif qu'on utilise manuellement à une **plateforme d'analyse programmable**.

## Premiers pas : exécuter du Python dans GDB

### Commande `python` en ligne

La façon la plus directe d'exécuter du Python dans GDB est la commande `python` :

```
(gdb) python print("Hello from GDB Python")
Hello from GDB Python
```

Pour un bloc de plusieurs lignes :

```
(gdb) python
>import os
>print(f"PID du processus GDB : {os.getpid()}")
>print(f"Répertoire courant : {os.getcwd()}")
>end
PID du processus GDB : 12345  
Répertoire courant : /home/user/binaries/ch11-keygenme  
```

Le bloc se termine par `end` sur une ligne seule, comme les blocs `commands`.

### Charger un script Python externe

Pour les scripts plus longs, on les écrit dans un fichier `.py` et on les charge avec `source` :

```
(gdb) source mon_script.py
```

Ou au lancement de GDB :

```bash
$ gdb -q -x mon_script.py ./keygenme_O0
```

Le script s'exécute dans le contexte de GDB avec un accès complet au module `gdb`.

### Vérifier que Python est disponible

Sur la plupart des distributions modernes, GDB est compilé avec le support Python. Pour vérifier :

```
(gdb) python print(gdb.VERSION)
14.2

(gdb) python import sys; print(sys.version)
3.11.6 (main, Oct  2 2023, 13:45:54) [GCC 13.2.0]
```

Si `python` produit une erreur `Python scripting is not supported in this copy of GDB`, il faut installer une version de GDB compilée avec Python (paquet `gdb` standard sur Debian/Ubuntu/Fedora).

## Le module `gdb` : anatomie de l'API

Tout passe par le module `gdb`, importé automatiquement dans le contexte Python de GDB. Voici ses composants principaux, organisés par catégorie.

### Exécuter des commandes GDB depuis Python

La fonction `gdb.execute()` est le pont entre Python et le langage de commandes GDB :

```python
# Exécuter une commande GDB
gdb.execute("break main")

# Capturer la sortie dans une chaîne Python
output = gdb.execute("info registers", to_string=True)  
print(output)  

# Exécuter silencieusement (pas d'affichage dans le terminal GDB)
gdb.execute("continue", from_tty=False)
```

Le paramètre `to_string=True` est crucial : il redirige la sortie de la commande vers une chaîne Python au lieu de l'afficher dans le terminal. On peut ensuite parser cette chaîne avec les outils Python habituels.

### Lire et écrire les registres

```python
# Lire un registre
rax = gdb.selected_frame().read_register("rax")  
rdi = gdb.selected_frame().read_register("rdi")  
rip = gdb.selected_frame().read_register("rip")  

print(f"rax = {int(rax):#x}")  
print(f"rdi = {int(rdi):#x}")  
print(f"rip = {int(rip):#x}")  
```

`read_register()` retourne un objet `gdb.Value`. On le convertit en entier Python avec `int()` pour les manipulations numériques.

Pour écrire dans un registre :

```python
gdb.execute("set $rax = 1")
```

Il n'y a pas de méthode directe `write_register()` dans l'API — on passe par `gdb.execute()` avec la commande `set`.

### Lire et écrire la mémoire

L'objet `gdb.selected_inferior()` représente le processus débogué et donne accès à sa mémoire :

```python
inferior = gdb.selected_inferior()

# Lire 32 octets à une adresse
data = inferior.read_memory(0x7fffffffe100, 32)
# data est un objet memoryview, convertible en bytes
raw = bytes(data)  
print(raw)           # b'TEST-KEY\n\x00\x00...'  
print(raw.hex())     # 544553542d4b45590a00...  

# Lire une chaîne null-terminated
addr = 0x402010  
data = inferior.read_memory(addr, 64)  
string = bytes(data).split(b'\x00')[0].decode('utf-8', errors='replace')  
print(string)        # "Enter your key: "  
```

Pour écrire en mémoire :

```python
# Écrire des octets bruts
inferior.write_memory(0x7fffffffe100, b"PATCHED\x00")

# Écrire une valeur entière (4 octets, little-endian)
import struct  
inferior.write_memory(0x404050, struct.pack("<I", 42))  
```

### Évaluer des expressions C

`gdb.parse_and_eval()` évalue une expression dans le contexte du programme débogué, exactement comme la commande `print` :

```python
# Évaluer une expression C
result = gdb.parse_and_eval("check_key(input)")  
print(f"Retour : {int(result)}")  

# Lire une variable
argc = gdb.parse_and_eval("argc")  
print(f"argc = {int(argc)}")  

# Déréférencer un pointeur
val = gdb.parse_and_eval("*(int *)0x404050")  
print(f"Valeur à 0x404050 : {int(val)}")  

# Accéder à un registre
rax = gdb.parse_and_eval("$rax")  
print(f"rax = {int(rax):#x}")  
```

C'est souvent la méthode la plus concise pour lire une valeur, mais elle est plus lente que `read_register()` car elle passe par le parser d'expressions de GDB.

### Naviguer dans les frames de pile

```python
# Frame courante
frame = gdb.selected_frame()  
print(f"Fonction : {frame.name()}")           # "check_key" ou None si strippé  
print(f"PC : {frame.pc():#x}")                # Adresse courante  
print(f"Architecture : {frame.architecture().name()}")  

# Remonter la pile
caller = frame.older()  
if caller:  
    print(f"Appelant : {caller.name()} à {caller.pc():#x}")

# Itérer sur tous les frames
frame = gdb.newest_frame()  
while frame is not None:  
    print(f"  #{frame.level()} {frame.name() or '??'} @ {frame.pc():#x}")
    frame = frame.older()
```

### Manipuler les breakpoints

```python
# Créer un breakpoint
bp = gdb.Breakpoint("*0x401156")  
print(f"Breakpoint {bp.number} créé à {bp.location}")  

# Breakpoint conditionnel
bp2 = gdb.Breakpoint("strcmp")  
bp2.condition = '*(char *)$rdi == 0x56'    # Premier char = 'V'  

# Lister les breakpoints existants
for bp in gdb.breakpoints():
    print(f"BP #{bp.number}: {bp.location}, enabled={bp.enabled}, hits={bp.hit_count}")

# Désactiver / supprimer
bp.enabled = False  
bp.delete()  
```

### Accéder aux symboles et types

```python
# Chercher un symbole par nom
sym, _ = gdb.lookup_symbol("check_key")  
if sym:  
    print(f"Type : {sym.type}")
    print(f"Adresse : {sym.value().address}")

# Chercher un symbole global
sym = gdb.lookup_global_symbol("global_flag")  
if sym:  
    print(f"global_flag = {int(sym.value())}")

# Résoudre une adresse en symbole
block = gdb.block_for_pc(0x401156)  
if block and block.function:  
    print(f"Fonction à 0x401156 : {block.function.name}")
```

## Breakpoints scriptés : la classe `gdb.Breakpoint`

La vraie puissance de l'API Python réside dans la possibilité de sous-classer `gdb.Breakpoint` pour créer des breakpoints dont le comportement est entièrement défini en Python. La méthode `stop()` est appelée à chaque déclenchement et retourne `True` pour arrêter l'exécution ou `False` pour continuer.

### Structure de base

```python
class MonBreakpoint(gdb.Breakpoint):
    def __init__(self, location):
        super().__init__(location)
    
    def stop(self):
        # Inspecter l'état...
        # Retourner True pour s'arrêter, False pour continuer
        return False
```

### Exemple : logger tous les appels à `strcmp`

```python
class StrcmpLogger(gdb.Breakpoint):
    def __init__(self):
        super().__init__("strcmp")
        self.silent = True          # Supprimer le message standard de GDB
        self.calls = []             # Accumuler les résultats
    
    def stop(self):
        frame = gdb.selected_frame()
        rdi = int(frame.read_register("rdi"))
        rsi = int(frame.read_register("rsi"))
        
        inf = gdb.selected_inferior()
        try:
            s1 = bytes(inf.read_memory(rdi, 128)).split(b'\x00')[0].decode('utf-8', errors='replace')
            s2 = bytes(inf.read_memory(rsi, 128)).split(b'\x00')[0].decode('utf-8', errors='replace')
        except gdb.MemoryError:
            return False    # Adresse invalide, on ignore
        
        self.calls.append((s1, s2))
        print(f"[strcmp] \"{s1}\" vs \"{s2}\"")
        
        return False    # Ne pas s'arrêter — continuer l'exécution

# Instancier
logger = StrcmpLogger()
```

Après un `run`, la liste `logger.calls` contient toutes les paires de chaînes comparées. On peut les analyser après l'exécution :

```python
for s1, s2 in logger.calls:
    if "KEY" in s1 or "KEY" in s2:
        print(f"  → Comparaison intéressante : \"{s1}\" vs \"{s2}\"")
```

### Exemple : breakpoint conditionnel avancé

Les conditions Python peuvent être arbitrairement complexes — bien au-delà de ce que la syntaxe `if` de GDB permet :

```python
import re

class SmartBreak(gdb.Breakpoint):
    """S'arrête sur strcmp uniquement si un argument matche un pattern regex."""
    
    def __init__(self, pattern):
        super().__init__("strcmp")
        self.silent = True
        self.pattern = re.compile(pattern)
    
    def stop(self):
        inf = gdb.selected_inferior()
        frame = gdb.selected_frame()
        
        for reg in ("rdi", "rsi"):
            addr = int(frame.read_register(reg))
            try:
                s = bytes(inf.read_memory(addr, 256)).split(b'\x00')[0].decode('utf-8', errors='replace')
                if self.pattern.search(s):
                    print(f"[MATCH] {reg} = \"{s}\"")
                    return True     # Arrêter — on a trouvé quelque chose
            except gdb.MemoryError:
                continue
        
        return False    # Pas de match, continuer

# Ne s'arrêter que quand strcmp reçoit une chaîne contenant "KEY" ou "PASS"
SmartBreak(r"(?i)(key|pass)")
```

Ce breakpoint utilise des expressions régulières Python, gère les erreurs mémoire, et ne s'arrête que sur les comparaisons pertinentes. C'est incomparablement plus puissant qu'un `break strcmp if ...` classique.

## Événements GDB : réagir automatiquement

L'API expose un système d'événements auxquels on peut s'abonner :

```python
def on_stop(event):
    """Appelé à chaque arrêt du programme (breakpoint, watchpoint, signal...)."""
    if isinstance(event, gdb.BreakpointEvent):
        print(f"Arrêt sur breakpoint(s) : {[bp.number for bp in event.breakpoints]}")
    elif isinstance(event, gdb.SignalEvent):
        print(f"Signal reçu : {event.stop_signal}")
    
    # Afficher les registres clés à chaque arrêt
    frame = gdb.selected_frame()
    rip = int(frame.read_register("rip"))
    rax = int(frame.read_register("rax"))
    print(f"  rip={rip:#x}  rax={rax:#x}")

gdb.events.stop.connect(on_stop)
```

Les événements disponibles :

| Événement | Déclenché quand... |  
|---|---|  
| `gdb.events.stop` | Le programme s'arrête (breakpoint, signal, watchpoint, step) |  
| `gdb.events.cont` | Le programme reprend l'exécution |  
| `gdb.events.exited` | Le programme se termine |  
| `gdb.events.new_thread` | Un nouveau thread est créé |  
| `gdb.events.new_inferior` | Un nouveau processus (inferior) est créé |  
| `gdb.events.memory_changed` | La mémoire est modifiée par une commande GDB |  
| `gdb.events.register_changed` | Un registre est modifié par une commande GDB |

Pour se désabonner :

```python
gdb.events.stop.disconnect(on_stop)
```

## Créer des commandes GDB personnalisées

On peut définir de nouvelles commandes GDB en sous-classant `gdb.Command` :

```python
class DumpArgs(gdb.Command):
    """Affiche les 6 premiers arguments (convention System V AMD64)."""
    
    def __init__(self):
        super().__init__("dump-args", gdb.COMMAND_USER)
    
    def invoke(self, arg, from_tty):
        regs = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"]
        frame = gdb.selected_frame()
        inf = gdb.selected_inferior()
        
        for i, reg in enumerate(regs):
            val = int(frame.read_register(reg))
            # Essayer d'interpréter comme une chaîne
            desc = ""
            if 0x400000 <= val <= 0x7fffffffffff:
                try:
                    data = bytes(inf.read_memory(val, 64)).split(b'\x00')[0]
                    if all(0x20 <= b < 0x7f for b in data) and len(data) > 2:
                        desc = f'  → "{data.decode()}"'
                except gdb.MemoryError:
                    pass
            print(f"  arg{i+1} ({reg}) = {val:#018x}{desc}")

DumpArgs()
```

Après avoir chargé ce script, on dispose d'une nouvelle commande :

```
(gdb) dump-args
  arg1 (rdi) = 0x00007fffffffe100  → "TEST-KEY"
  arg2 (rsi) = 0x0000000000402020  → "VALID-KEY-2025"
  arg3 (rdx) = 0x000000000000000f
  arg4 (rcx) = 0x0000000000000000
  arg5 (r8)  = 0x0000000000000000
  arg6 (r9)  = 0x0000000000000000
```

La commande détecte automatiquement les pointeurs vers des chaînes imprimables et les affiche. On peut l'utiliser à n'importe quel breakpoint pour voir instantanément les arguments d'une fonction inconnue.

Un autre exemple — une commande pour scanner la pile à la recherche de pointeurs vers `.text` (détection d'adresses de retour, utile sur les binaires strippés) :

```python
class ScanStack(gdb.Command):
    """Scanne la pile à la recherche d'adresses de retour dans .text."""
    
    def __init__(self):
        super().__init__("scan-stack", gdb.COMMAND_USER)
    
    def invoke(self, arg, from_tty):
        depth = int(arg) if arg else 64     # Nombre de mots à scanner
        frame = gdb.selected_frame()
        rsp = int(frame.read_register("rsp"))
        inf = gdb.selected_inferior()
        
        # Trouver les bornes de .text
        mappings = gdb.execute("info proc mappings", to_string=True)
        text_start, text_end = None, None
        for line in mappings.splitlines():
            if "r-xp" in line and "libc" not in line and "ld-" not in line:
                parts = line.split()
                text_start = int(parts[0], 16)
                text_end = int(parts[1], 16)
                break
        
        if not text_start:
            print("Impossible de trouver .text")
            return
        
        import struct
        data = bytes(inf.read_memory(rsp, depth * 8))
        for i in range(depth):
            val = struct.unpack_from("<Q", data, i * 8)[0]
            if text_start <= val < text_end:
                offset = rsp + i * 8
                # Vérifier si l'instruction précédente est un call
                try:
                    disas = gdb.execute(f"x/i {val - 5}", to_string=True)
                    marker = " ← probable ret addr" if "call" in disas else ""
                    print(f"  rsp+{i*8:#06x} [{offset:#x}]: {val:#x}{marker}")
                except:
                    print(f"  rsp+{i*8:#06x} [{offset:#x}]: {val:#x}")

ScanStack()
```

```
(gdb) scan-stack
  rsp+0x0038 [0x7fffffffe0f8]: 0x4011a5 ← probable ret addr
  rsp+0x0078 [0x7fffffffe138]: 0x401060
```

## Scripts complets : exemples de workflows automatisés

### Tracer les appels à une liste de fonctions

Ce script crée un breakpoint de logging sur chaque fonction d'une liste et produit une trace d'exécution :

```python
# trace_calls.py — tracer les appels à des fonctions ciblées
import gdb  
import time  

class CallTracer(gdb.Breakpoint):
    trace = []
    
    def __init__(self, func_name):
        super().__init__(func_name)
        self.silent = True
        self.func_name = func_name
    
    def stop(self):
        frame = gdb.selected_frame()
        rdi = int(frame.read_register("rdi"))
        rsi = int(frame.read_register("rsi"))
        rdx = int(frame.read_register("rdx"))
        
        entry = {
            "func": self.func_name,
            "rdi": rdi, "rsi": rsi, "rdx": rdx,
            "rip": int(frame.read_register("rip")),
            "caller": frame.older().pc() if frame.older() else 0
        }
        CallTracer.trace.append(entry)
        return False

# Fonctions à tracer
targets = ["strcmp", "memcmp", "strlen", "strcpy", "malloc", "free", "open"]  
for func in targets:  
    try:
        CallTracer(func)
    except:
        pass    # La fonction n'existe pas dans ce binaire

# Hook de fin de programme pour afficher le résumé
def on_exit(event):
    print(f"\n{'='*60}")
    print(f"Trace complète : {len(CallTracer.trace)} appels capturés")
    print(f"{'='*60}")
    for i, e in enumerate(CallTracer.trace):
        print(f"  [{i:3d}] {e['func']:10s}  rdi={e['rdi']:#x}  "
              f"caller={e['caller']:#x}")

gdb.events.exited.connect(on_exit)
```

### Exporter les résultats en JSON

Un avantage majeur de Python : on peut exporter les résultats dans des formats structurés pour une analyse ultérieure :

```python
# export_analysis.py — exporter les données d'analyse en JSON
import gdb  
import json  

class AnalysisExporter:
    def __init__(self, output_path):
        self.output_path = output_path
        self.data = {
            "binary": gdb.current_progspace().filename,
            "breakpoint_hits": [],
            "memory_snapshots": [],
            "strings_found": []
        }
    
    def capture_state(self, label=""):
        frame = gdb.selected_frame()
        inf = gdb.selected_inferior()
        
        state = {
            "label": label,
            "rip": int(frame.read_register("rip")),
            "registers": {}
        }
        for reg in ["rax", "rbx", "rcx", "rdx", "rdi", "rsi", "rbp", "rsp"]:
            state["registers"][reg] = int(frame.read_register(reg))
        
        self.data["breakpoint_hits"].append(state)
    
    def save(self):
        with open(self.output_path, "w") as f:
            json.dump(self.data, f, indent=2, default=str)
        print(f"Analyse exportée vers {self.output_path}")

exporter = AnalysisExporter("/tmp/analysis_results.json")
```

Le fichier JSON peut ensuite être relu par un script Python externe, importé dans un notebook Jupyter, ou comparé avec l'analyse d'un autre binaire.

## Intégration dans le fichier `.gdbinit`

On peut charger automatiquement ses scripts Python au démarrage de GDB :

```
# ~/.gdbinit
source ~/re-toolkit/dump_args.py  
source ~/re-toolkit/scan_stack.py  
source ~/re-toolkit/strcmp_logger.py  

# Ou un répertoire entier
python  
import glob, os  
for f in sorted(glob.glob(os.path.expanduser("~/re-toolkit/gdb-scripts/*.py"))):  
    gdb.execute(f"source {f}")
end
```

Les commandes personnalisées (`dump-args`, `scan-stack`, etc.) deviennent disponibles dans toutes les sessions GDB, comme des commandes natives.

## Limites et bonnes pratiques

**Performance.** Chaque déclenchement d'un breakpoint Python provoque un aller-retour entre le processus débogué et l'interpréteur Python de GDB. Sur une boucle exécutée des millions de fois, cela ralentit considérablement l'exécution. Pour les cas de traçage intensif, préférer Frida (chapitre 13) qui injecte du code directement dans le processus sans les allers-retours ptrace.

**Thread safety.** L'API Python de GDB n'est pas thread-safe. Ne lancez pas de threads Python qui accèdent simultanément aux objets `gdb.*`. Tout le code doit s'exécuter dans le thread principal de GDB.

**Gestion des erreurs.** Encadrez systématiquement les accès mémoire avec `try/except gdb.MemoryError`. Une adresse invalide dans un registre ne doit pas faire planter le script entier :

```python
try:
    data = bytes(inferior.read_memory(addr, 64))
except gdb.MemoryError:
    data = None
```

**Idempotence.** Quand on recharge un script avec `source`, les classes et instances sont recréées. Si le script crée des breakpoints, ils s'accumulent à chaque rechargement. Ajoutez une logique de nettoyage :

```python
# Supprimer les anciens breakpoints de ce script avant d'en créer de nouveaux
for bp in gdb.breakpoints() or []:
    if hasattr(bp, '_my_script_marker'):
        bp.delete()
```

**Débogage des scripts eux-mêmes.** Les erreurs Python sont affichées dans le terminal GDB. Pour un débogage plus fin, on peut utiliser `traceback` :

```python
import traceback  
try:  
    # Code susceptible de planter
    pass
except Exception as e:
    traceback.print_exc()
```

---

> **À retenir :** L'API Python de GDB transforme le débogueur en plateforme d'analyse programmable. Les breakpoints scriptés (`gdb.Breakpoint` avec `stop()`) permettent de créer des sondes d'observation arbitrairement complexes. Les commandes personnalisées (`gdb.Command`) enrichissent le vocabulaire de GDB avec des outils adaptés au RE. Et la possibilité d'exporter les résultats en JSON, de parser les sorties avec des regex, d'accumuler des statistiques dans des dictionnaires Python — tout cela fait du scripting GDB Python le ciment qui lie l'analyse dynamique au reste du workflow de reverse engineering. Le checkpoint de ce chapitre mettra ces compétences en pratique avec un script complet de traçage automatisé.

⏭️ [Introduction à `pwntools` pour automatiser les interactions avec un binaire](/11-gdb/09-introduction-pwntools.md)
