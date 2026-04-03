🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 11.9 — Introduction à `pwntools` pour automatiser les interactions avec un binaire

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Le chaînon manquant entre l'analyse et l'action

Les sections précédentes ont montré comment observer un programme avec GDB : poser des breakpoints, inspecter les registres, lire la mémoire, automatiser la collecte d'informations avec l'API Python. Mais observer ne suffit pas toujours. À un moment, il faut **interagir** avec le binaire : lui envoyer des entrées précises, lire ses sorties, adapter l'entrée suivante en fonction de la réponse, et tout cela de manière reproductible et programmable.

Prenons un scénario concret. L'analyse de `keygenme_O0` dans GDB a révélé que la fonction `check_key` compare l'entrée utilisateur avec une chaîne dérivée d'un calcul. On connaît maintenant l'algorithme. On veut écrire un programme qui :

1. Lance le binaire.  
2. Lit le prompt `"Enter your key: "`.  
3. Calcule la bonne clé.  
4. L'envoie au programme.  
5. Vérifie que la réponse est `"Correct!"`.

On pourrait faire cela avec un script shell et des pipes, ou avec `subprocess` en Python. Mais ces approches se heurtent rapidement à des problèmes de buffering, de timing, de gestion des entrées/sorties interactives, et d'intégration avec GDB. **pwntools** résout tout cela dans une bibliothèque Python unique, conçue spécifiquement pour interagir avec des binaires.

## Qu'est-ce que pwntools

pwntools est une bibliothèque Python développée par l'équipe Gallopsled, initialement pour les compétitions CTF (*Capture The Flag*), mais devenue un outil de référence en reverse engineering et en recherche de vulnérabilités. Elle fournit :

- Une abstraction unifiée pour interagir avec des processus locaux, des binaires distants (via réseau), et GDB.  
- Des primitives d'envoi et de réception de données avec gestion fine du buffering et des délais.  
- Des outils de construction de payloads : packing/unpacking d'entiers, génération de patterns cycliques, manipulation de shellcode.  
- Une intégration native avec GDB : lancer un processus sous GDB depuis le script Python, poser des breakpoints, reprendre l'exécution.  
- Des utilitaires pour l'analyse ELF : lecture des sections, symboles, GOT/PLT, calcul d'offsets.

Dans le contexte de ce chapitre, nous nous concentrons sur les deux premiers aspects : l'interaction avec un processus et l'intégration avec GDB. Les aspects exploitation (shellcode, ROP) sont hors du périmètre de cette formation.

## Installation

pwntools s'installe via pip :

```bash
$ pip install pwntools
```

Les dépendances incluent `capstone` (désassembleur), `pyelftools` (parsing ELF), `unicorn` (émulation) et plusieurs autres. L'installation est plus lourde qu'une bibliothèque classique, mais tout est automatique.

Vérification :

```python
$ python3 -c "from pwn import *; print(pwnlib.version)"
```

> ⚠️ **Convention d'import :** pwntools s'importe conventionnellement avec `from pwn import *`, ce qui injecte un grand nombre de noms dans l'espace global. C'est inhabituel pour une bibliothèque Python, mais c'est le style standard de pwntools, optimisé pour l'écriture rapide de scripts. Pour un code plus propre, on peut importer sélectivement : `from pwnlib.tubes.process import process`.

## Interagir avec un processus local

### Lancer un processus

```python
from pwn import *

# Lancer un binaire
p = process("./keygenme_O0")
```

L'objet `p` est un **tube** (*tube*) — l'abstraction centrale de pwntools pour tout canal de communication bidirectionnel. Un tube peut être un processus local, une connexion réseau, un port série, ou même une session SSH. L'interface est identique quel que soit le transport.

Le processus est lancé immédiatement et tourne en arrière-plan. Il attend ses entrées via le tube `p`.

Pour passer des arguments ou des variables d'environnement :

```python
p = process(["./keygenme_O0", "arg1", "arg2"])  
p = process("./keygenme_O0", env={"LD_PRELOAD": "./hook.so"})  
```

### Recevoir des données

```python
# Lire jusqu'à une chaîne spécifique
prompt = p.recvuntil(b"Enter your key: ")  
print(prompt)    # b'Enter your key: '  

# Lire une ligne complète (jusqu'à \n)
line = p.recvline()  
print(line)      # b'Some output\n'  

# Lire exactement N octets
data = p.recv(16)

# Lire tout ce qui est disponible (non bloquant avec timeout)
data = p.recv(timeout=1)

# Lire jusqu'à la fin du processus
all_output = p.recvall()
```

La méthode la plus importante est `recvuntil()` : elle bloque jusqu'à ce que la chaîne spécifiée apparaisse dans la sortie, puis retourne tout ce qui a été reçu y compris le délimiteur. C'est la façon standard de se synchroniser avec un programme interactif — on attend le prompt avant d'envoyer la réponse.

Le paramètre `timeout` (en secondes) est disponible sur toutes les méthodes de réception. Si le timeout expire sans que la condition soit remplie, une exception `EOFError` est levée (ou les données partielles sont retournées, selon la méthode).

### Envoyer des données

```python
# Envoyer des octets bruts
p.send(b"TEST-KEY")

# Envoyer avec un retour à la ligne (\n) ajouté automatiquement
p.sendline(b"TEST-KEY")

# Attendre un prompt, puis envoyer
p.sendlineafter(b"Enter your key: ", b"TEST-KEY")
```

`sendlineafter()` combine `recvuntil()` et `sendline()` en un seul appel — c'est le pattern le plus fréquent pour les interactions prompt/réponse. La méthode attend le prompt, puis envoie la réponse suivie d'un newline.

Sa variante `sendafter()` fait la même chose sans ajouter de newline.

### Exemple complet : interagir avec le keygenme

```python
from pwn import *

# Configurer le niveau de log
context.log_level = 'info'    # 'debug' pour voir tout le trafic

# Lancer le binaire
p = process("./keygenme_O0")

# Attendre le prompt et envoyer une clé
p.sendlineafter(b"Enter your key: ", b"TEST-KEY")

# Lire la réponse
response = p.recvline()  
print(f"Réponse : {response}")  

if b"Correct" in response:
    log.success("Clé acceptée !")
else:
    log.failure("Clé refusée.")

p.close()
```

L'objet `log` est le logger intégré de pwntools, avec des niveaux colorés : `log.info()`, `log.success()`, `log.failure()`, `log.warning()`. Le réglage `context.log_level = 'debug'` affiche tout le trafic brut envoyé et reçu — indispensable pour diagnostiquer les problèmes de synchronisation.

## Intégration avec GDB

C'est la fonctionnalité qui justifie la présence de pwntools dans ce chapitre sur GDB. pwntools peut lancer un processus **directement sous GDB**, permettant d'interagir avec le binaire depuis le script Python tout en posant des breakpoints et inspectant l'état dans GDB.

### Lancer un processus sous GDB

```python
from pwn import *

# Lancer sous GDB avec un terminal dédié
p = gdb.debug("./keygenme_O0", '''
    break check_key
    continue
''')
```

Cette commande :

1. Lance le binaire sous `gdbserver`.  
2. Ouvre un **nouveau terminal** avec une session GDB connectée au serveur.  
3. Exécute les commandes GDB passées en second argument (ici, poser un breakpoint et continuer).  
4. Retourne un tube `p` qui permet d'interagir avec le stdin/stdout du binaire depuis le script Python.

On se retrouve avec deux fenêtres : le terminal GDB où on peut inspecter interactivement, et le script Python qui contrôle les entrées/sorties. Les deux opèrent sur le même processus.

### Configurer le terminal

pwntools doit ouvrir un nouveau terminal pour GDB. Par défaut, il essaie `tmux` (si on est dans une session tmux), puis `gnome-terminal`, `xterm`, etc. On peut forcer le choix :

```python
context.terminal = ['tmux', 'splitw', '-h']     # Split horizontal dans tmux  
context.terminal = ['gnome-terminal', '--', 'sh', '-c']  
context.terminal = ['xterm', '-e']  
```

La configuration tmux est la plus pratique : GDB s'ouvre dans un panneau à côté du script, dans le même terminal.

### Workflow combiné : script + GDB interactif

Voici un workflow réaliste. Le script envoie des données au binaire, et GDB est positionné pour observer l'état au moment critique :

```python
from pwn import *

context.arch = 'amd64'  
context.terminal = ['tmux', 'splitw', '-h']  

# Lancer sous GDB, breakpoint juste avant la comparaison
p = gdb.debug("./keygenme_O0", '''
    set disassembly-flavor intel
    break *check_key+30
    continue
''')

# Le script attend que GDB ait atteint le 'continue'
# puis envoie l'entrée
p.sendlineafter(b"Enter your key: ", b"REVERSE-2025")

# GDB est maintenant arrêté sur le breakpoint dans check_key
# → On peut inspecter interactivement les registres dans le terminal GDB
# → Le script attend que l'exécution reprenne (quand on tape 'continue' dans GDB)

# Lire le résultat
p.interactive()
```

La méthode `p.interactive()` à la fin bascule le tube en mode interactif : le stdin du terminal est connecté au stdin du processus, et sa sortie est affichée directement. C'est utile pour les phases où on veut interagir manuellement après une phase automatisée.

### Attacher GDB à un processus existant

On peut aussi lancer le processus normalement et attacher GDB ensuite :

```python
from pwn import *

p = process("./keygenme_O0")

# Effectuer une partie de l'interaction
p.recvuntil(b"Enter your key: ")

# Attacher GDB maintenant (le processus est mis en pause)
gdb.attach(p, '''
    break *check_key+30
    continue
''')

# Envoyer l'entrée (GDB est attaché et le breakpoint est en place)
p.sendline(b"REVERSE-2025")

p.interactive()
```

`gdb.attach()` ouvre un terminal GDB attaché au processus de `p`. Le second argument contient les commandes GDB à exécuter après l'attachement. Le processus est brièvement mis en pause pendant l'attachement, puis reprend quand GDB exécute `continue`.

## Utilitaires de packing et de données

pwntools fournit des fonctions pour convertir entre entiers et octets, opération constante en RE.

### Packing et unpacking

```python
from pwn import *

context.arch = 'amd64'     # Détermine l'endianness et la taille par défaut

# Pack : entier → octets (little-endian par défaut sur x86-64)
p64(0xdeadbeef)              # b'\xef\xbe\xad\xde\x00\x00\x00\x00'  
p32(0xdeadbeef)              # b'\xef\xbe\xad\xde'  
p16(0x4141)                  # b'AA'  
p8(0x41)                     # b'A'  

# Unpack : octets → entier
u64(b'\xef\xbe\xad\xde\x00\x00\x00\x00')    # 0xdeadbeef  
u32(b'\xef\xbe\xad\xde')                      # 0xdeadbeef  
u16(b'AA')                                     # 0x4141  

# Unpack avec padding automatique (quand on n'a pas exactement 8 octets)
u64(b'\xef\xbe\xad\xde\x00\x00'.ljust(8, b'\x00'))
```

Ces fonctions remplacent `struct.pack("<Q", val)` et `struct.unpack("<Q", data)[0]` avec une syntaxe beaucoup plus concise. La convention d'endianness est déterminée par `context.arch`.

### Manipulation de chaînes et patterns

```python
# Pattern cyclique pour identifier les offsets dans un crash
pattern = cyclic(200)          # b'aaaabaaacaaadaaa...'
# Si le programme crash avec rip = 0x6161616c :
offset = cyclic_find(0x6161616c)  
print(f"Offset du crash : {offset}")    # ex: 44  

# Hex encode/decode
enhex(b"TEST")                 # '54455354'  
unhex('54455354')              # b'TEST'  

# XOR
xor(b"SECRET", 0x42)          # XOR chaque octet avec 0x42  
xor(b"CIPHER", b"KEYKEY")     # XOR avec une clé répétée  
```

La fonction `cyclic()` génère un pattern De Bruijn où chaque sous-séquence de N octets est unique. En envoyant ce pattern comme entrée à un binaire vulnérable, la valeur trouvée dans `rip` (ou `rsp`) après le crash identifie l'offset exact du débordement. C'est un outil classique d'exploitation, mais aussi utile en RE pour cartographier les buffers.

## Analyse de binaire ELF

pwntools inclut un parseur ELF complet qui complète les outils vus au chapitre 5 :

```python
from pwn import *

elf = ELF("./keygenme_O0")

# Informations de base
print(f"Architecture : {elf.arch}")        # 'amd64'  
print(f"Entry point  : {elf.entry:#x}")    # 0x401060  
print(f"PIE          : {elf.pie}")         # False  
print(f"NX           : {elf.nx}")          # True (ou False)  
print(f"Canary       : {elf.canary}")      # False  

# Adresses des fonctions (si non strippé)
print(f"main         : {elf.symbols['main']:#x}")  
print(f"check_key    : {elf.symbols['check_key']:#x}")  

# Table PLT (fonctions importées)
print(f"strcmp@plt    : {elf.plt['strcmp']:#x}")  
print(f"printf@plt   : {elf.plt['printf']:#x}")  

# Table GOT
print(f"strcmp@got    : {elf.got['strcmp']:#x}")

# Sections
print(f".text         : {elf.sections['.text'].header.sh_addr:#x}")  
print(f".rodata       : {elf.sections['.rodata'].header.sh_addr:#x}")  

# Chercher une chaîne dans le binaire
addr = next(elf.search(b"Correct!"))  
print(f"'Correct!' trouvé à {addr:#x}")  

# Chercher un pattern d'octets
for addr in elf.search(b"\x48\x89\xe5"):     # mov rbp, rsp
    print(f"Prologue trouvé à {addr:#x}")
```

L'objet `ELF` est particulièrement utile pour les scripts qui doivent s'adapter à différentes compilations du même binaire. Au lieu de coder en dur les adresses, on les résout dynamiquement :

```python
elf = ELF("./keygenme_O0")

# L'adresse est résolue dynamiquement
p = process(elf.path)  
gdb.attach(p, f'''  
    break *{elf.symbols['check_key']:#x}
    continue
''')
```

Si on recompile le binaire et que les adresses changent, le script s'adapte automatiquement.

## Connexion réseau

Le même paradigme de tubes s'applique aux connexions réseau — ce sera exploité en détail au chapitre 23 pour le reverse d'un protocole client/serveur :

```python
from pwn import *

# Se connecter à un service distant
r = remote("192.168.56.10", 4444)

# L'interface est identique à process()
r.sendlineafter(b"login: ", b"admin")  
r.sendlineafter(b"password: ", b"s3cr3t")  
response = r.recvline()  
print(response)  

r.close()
```

L'abstraction est transparente : un script écrit pour un processus local (`process()`) peut être converti en client réseau (`remote()`) en changeant une seule ligne. C'est exactement ce qu'on fait quand on écrit un client de remplacement pour un protocole qu'on a reverse-engineered.

## Contexte et architecture

L'objet global `context` configure le comportement de pwntools :

```python
from pwn import *

# Architecture cible
context.arch = 'amd64'        # Aussi : 'i386', 'arm', 'aarch64', 'mips'  
context.bits = 64  
context.endian = 'little'  

# Niveau de log
context.log_level = 'debug'   # Tout afficher (envois, réceptions, hex dumps)  
context.log_level = 'info'    # Informations normales  
context.log_level = 'warn'    # Avertissements uniquement  
context.log_level = 'error'   # Erreurs uniquement  

# Configuration automatique depuis un binaire ELF
context.binary = ELF("./keygenme_O0")
# → context.arch, bits, endian sont déduits automatiquement
```

`context.log_level = 'debug'` est l'outil de diagnostic le plus utile de pwntools : il affiche chaque octet envoyé et reçu, avec des hex dumps. Quand un script ne se synchronise pas correctement avec le binaire, le mode debug révèle immédiatement où se situe le décalage.

## Script complet : keygen automatisé

Pour conclure, voici le squelette d'un keygen complet qui combine tout ce que nous avons vu — pwntools pour l'interaction, intégration GDB pour la vérification, analyse ELF pour la résolution d'adresses :

```python
#!/usr/bin/env python3
"""
Keygen automatisé pour keygenme_O0.  
Utilise pwntools pour l'interaction et GDB pour la vérification.  
"""
from pwn import *

# Configuration
context.arch = 'amd64'  
context.log_level = 'info'  
elf = ELF("./keygenme_O0")  

def compute_key(username):
    """Algorithme de génération de clé, reconstruit par RE."""
    key = 0
    for i, c in enumerate(username):
        key ^= ord(c) << (i % 8)
        key = (key * 0x5DEECE66D + 0xB) & 0xFFFFFFFF
    return f"KEY-{key:08X}"

def verify_key(binary_path, key):
    """Vérifie la clé en l'envoyant au binaire."""
    p = process(binary_path)
    p.sendlineafter(b"Enter your key: ", key.encode())
    response = p.recvline(timeout=3)
    p.close()
    return b"Correct" in response

def verify_with_gdb(binary_path, key):
    """Vérifie en observant le retour de check_key dans GDB."""
    p = process(binary_path)
    gdb.attach(p, f'''
        break *{elf.symbols.get("check_key", 0):#x}
        commands
          silent
          finish
          printf "check_key returned %d\\n", $rax
          continue
        end
        continue
    ''')
    
    p.sendlineafter(b"Enter your key: ", key.encode())
    p.interactive()

# Générer et tester
key = compute_key("user123")  
log.info(f"Clé générée : {key}")  

if verify_key(elf.path, key):
    log.success(f"Vérification réussie : {key}")
else:
    log.failure("Échec — lancement du mode debug")
    verify_with_gdb(elf.path, key)
```

Ce script illustre le workflow complet :

1. `ELF()` résout les adresses dynamiquement.  
2. `compute_key()` implémente l'algorithme reconstruit par l'analyse statique et dynamique.  
3. `verify_key()` teste la clé via `process()` + `sendlineafter()` — entièrement automatisé.  
4. `verify_with_gdb()` relance avec GDB attaché si la vérification échoue, pour diagnostiquer.

C'est exactement le type de script que nous développerons au chapitre 21 pour le keygenme complet.

## Récapitulatif de l'API pwntools essentielle

| Fonction / Méthode | Rôle |  
|---|---|  
| `process(binary)` | Lancer un processus local |  
| `remote(host, port)` | Se connecter à un service réseau |  
| `gdb.debug(binary, script)` | Lancer sous GDB avec des commandes |  
| `gdb.attach(proc, script)` | Attacher GDB à un processus existant |  
| `p.send(data)` | Envoyer des octets bruts |  
| `p.sendline(data)` | Envoyer des octets + newline |  
| `p.sendlineafter(delim, data)` | Attendre un prompt, puis envoyer |  
| `p.recv(n)` | Recevoir n octets |  
| `p.recvline()` | Recevoir une ligne |  
| `p.recvuntil(delim)` | Recevoir jusqu'à un délimiteur |  
| `p.recvall()` | Recevoir tout jusqu'à la fin |  
| `p.interactive()` | Passer en mode interactif |  
| `p.close()` | Fermer le tube |  
| `p64()` / `u64()` | Pack / unpack 64 bits |  
| `p32()` / `u32()` | Pack / unpack 32 bits |  
| `ELF(path)` | Parser un binaire ELF |  
| `elf.symbols[name]` | Adresse d'un symbole |  
| `elf.plt[name]` / `elf.got[name]` | Adresses PLT / GOT |  
| `elf.search(bytes)` | Chercher des octets dans le binaire |  
| `cyclic(n)` / `cyclic_find(val)` | Pattern De Bruijn |  
| `xor(data, key)` | XOR de données |  
| `context.arch` | Architecture cible |  
| `context.log_level` | Verbosité des logs |

---

> **À retenir :** pwntools est le complément naturel de GDB pour le RE : là où GDB observe, pwntools agit. Son abstraction en tubes (`process`, `remote`) permet d'interagir avec n'importe quel binaire de manière programmatique, et son intégration GDB (`gdb.debug`, `gdb.attach`) permet de combiner l'automatisation Python avec l'inspection interactive du débogueur. Le trio ELF + process + GDB dans un seul script Python forme un workflow d'analyse complet, reproductible et partageable — exactement ce dont on a besoin pour passer de l'analyse à la production d'un keygen ou d'un client de remplacement.

⏭️ [🎯 Checkpoint : écrire un script GDB Python qui dumpe automatiquement les arguments de chaque appel à `strcmp`](/11-gdb/checkpoint.md)
