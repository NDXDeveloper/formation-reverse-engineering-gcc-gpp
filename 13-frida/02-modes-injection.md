🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 13.2 — Modes d'injection : `frida`, `frida-trace`, spawn vs attach

> 🧰 **Outils utilisés** : `frida`, `frida-trace`, `frida-ps`, Python 3 + module `frida`  
> 📦 **Binaires utilisés** : `binaries/ch13-keygenme/keygenme_O0`  
> 📖 **Prérequis** : [13.1 — Architecture de Frida](/13-frida/01-architecture-frida.md)

---

## Deux stratégies d'injection fondamentales

En section 13.1, nous avons vu que Frida injecte son agent dans le processus cible. Reste une question pratique immédiate : **quand** cette injection a-t-elle lieu par rapport au cycle de vie du programme ? La réponse définit les deux modes fondamentaux de Frida, et le choix entre les deux conditionne ce que vous pourrez ou non instrumenter.

### Mode attach : s'accrocher à un processus existant

Dans ce mode, le programme cible est déjà en cours d'exécution. Frida s'y attache, injecte l'agent, et l'instrumentation commence à partir de ce point.

```bash
# Le programme tourne déjà (PID 4567)
frida -p 4567 -l mon_script.js
```

Ou par nom de processus :

```bash
frida -n keygenme_O0 -l mon_script.js
```

En Python :

```python
import frida

session = frida.attach("keygenme_O0")   # par nom
# ou
session = frida.attach(4567)            # par PID
```

**Quand l'utiliser.** Le mode attach est le choix naturel quand la cible est un service de longue durée (un serveur, un daemon), quand on veut instrumenter un programme qui est déjà dans un état intéressant (connexion réseau établie, fichier ouvert), ou quand on ne contrôle pas le lancement du programme (processus lancé par un autre service).

**La limite.** Tout ce qui s'est exécuté avant l'attachement est invisible. Si la fonction que vous voulez hooker a déjà été appelée — un constructeur global C++, une routine d'initialisation, une vérification de licence exécutée au `main()` avant toute interaction — vous arrivez trop tard. Les hooks ne s'appliquent qu'aux appels **futurs** de la fonction.

### Mode spawn : lancer et instrumenter dès le départ

Dans ce mode, Frida lance lui-même le programme, le suspend avant que la première instruction utilisateur ne s'exécute, injecte l'agent, puis relâche le processus.

```bash
frida -f ./keygenme_O0 -l mon_script.js
```

Le flag `-f` (pour *file*) indique à Frida de spawner le binaire. Le processus est créé en état suspendu : le loader dynamique (`ld.so`) a fait son travail (chargement des bibliothèques, résolution PLT/GOT), mais `main()` n'a pas encore commencé — ni même les constructeurs `__attribute__((constructor))` dans la plupart des cas.

En Python :

```python
import frida

pid = frida.spawn(["./keygenme_O0"])  
session = frida.attach(pid)  

script = session.create_script("""
    // Hooks installés ici, AVANT que main() ne s'exécute
    Interceptor.attach(Module.findExportByName(null, "strcmp"), {
        onEnter(args) {
            send({
                a: args[0].readUtf8String(),
                b: args[1].readUtf8String()
            });
        }
    });
""")
script.load()

frida.resume(pid)  # Le programme démarre maintenant, avec les hooks en place
```

Le détail crucial est l'appel à `frida.resume(pid)`. Après le `spawn`, le processus est en pause. Vous avez tout le temps d'installer vos hooks. Quand vous appelez `resume`, le programme démarre et chaque appel à `strcmp` sera intercepté dès le premier — y compris ceux qui se produisent dans `main()` ou dans les constructeurs globaux.

En CLI, Frida affiche un prompt `[Local::keygenme_O0]->` après le spawn. Le processus est suspendu. Vous pouvez taper `%resume` pour le relancer :

```
     ____
    / _  |   Frida 16.x.x - A world-class dynamic instrumentation toolkit
   | (_| |
    > _  |   Commands:
   /_/ |_|       help      -> Displays the help system
   . . . .       %resume   -> Resume the main thread of the spawned process

[Local::keygenme_O0]-> %resume
```

### La question qui tranche : « Ai-je besoin du démarrage ? »

Le choix entre attach et spawn se résume souvent à cette question. Voici les cas de figure courants :

| Scénario | Mode recommandé | Raison |  
|---|---|---|  
| Hooker `strcmp` lors de la saisie du mot de passe (après démarrage) | **attach** | La vérification intervient après une interaction utilisateur — pas de rush |  
| Hooker un constructeur `__attribute__((constructor))` | **spawn** | S'exécute avant `main()`, impossible à attraper en attach |  
| Instrumenter un serveur qui tourne en permanence | **attach** | Le serveur est déjà lancé, on veut observer les requêtes à venir |  
| Analyser la routine d'initialisation d'un malware | **spawn** | Le comportement intéressant se produit immédiatement au lancement |  
| Contourner une vérification anti-debug au démarrage | **spawn** | Il faut hooker la vérification avant qu'elle ne s'exécute |  
| Explorer interactivement un programme déjà en cours | **attach** | Le programme est dans un état intéressant qu'on ne veut pas perdre |

En pratique, le mode **spawn** est le plus sûr quand on ne connaît pas encore le binaire : on est certain de ne rien rater. Le mode **attach** est plus pratique pour les sessions interactives et les services persistants.

---

## Les outils en ligne de commande

Frida fournit plusieurs exécutables, chacun adapté à un usage différent. Ils partagent tous le même mécanisme d'injection (attach ou spawn), mais diffèrent dans ce qu'ils font une fois l'agent en place.

### `frida` — le REPL interactif

La commande `frida` ouvre une session interactive avec un prompt JavaScript. C'est l'équivalent du mode interactif de GDB : on tape des commandes, on explore, on expérimente.

```bash
# Attach interactif
frida -n keygenme_O0

# Spawn interactif
frida -f ./keygenme_O0
```

Une fois dans le REPL, vous avez accès à l'intégralité de l'API Frida :

```javascript
// Lister les modules chargés
Process.enumerateModules().forEach(m => console.log(m.name, m.base));

// Trouver l'adresse d'une fonction exportée
Module.findExportByName(null, "strcmp")
// => "0x7f3a8b2c4560"

// Lire une chaîne à une adresse
Memory.readUtf8String(ptr("0x402010"))

// Poser un hook rapide
Interceptor.attach(Module.findExportByName(null, "puts"), {
    onEnter(args) {
        console.log("puts() appelé avec :", args[0].readUtf8String());
    }
});
```

Le REPL est idéal pour la phase exploratoire : on cherche des adresses, on teste des hooks, on valide des hypothèses avant d'écrire un script complet. La complétion par tabulation fonctionne sur les objets de l'API Frida.

**Charger un script depuis un fichier.** On peut combiner le REPL avec un fichier de script via l'option `-l` :

```bash
frida -f ./keygenme_O0 -l hooks.js
```

Le script est chargé et exécuté automatiquement, et le REPL reste ouvert pour des commandes additionnelles. C'est le workflow le plus courant : un fichier de script pour les hooks principaux, le REPL pour les ajustements en direct.

**Le flag `--no-pause`.** Par défaut en mode spawn, Frida suspend le processus et attend `%resume`. Si votre script est autonome et n'a pas besoin d'intervention manuelle entre le chargement et le lancement, le flag `--no-pause` enchaîne automatiquement :

```bash
frida -f ./keygenme_O0 -l hooks.js --no-pause
```

### `frida-ps` — lister les processus

Utilitaire simple qui liste les processus accessibles, utile pour trouver un PID ou vérifier qu'un processus cible est bien en cours :

```bash
# Processus locaux
frida-ps

# Avec les PID
frida-ps -a

# Sur un appareil distant (via frida-server)
frida-ps -H 192.168.1.100
```

### `frida-trace` — traçage rapide sans écrire de code

`frida-trace` est l'outil le plus immédiatement productif de la suite Frida. Il génère automatiquement des hooks pour les fonctions que vous spécifiez, sans écrire une seule ligne de JavaScript.

```bash
# Tracer tous les appels à strcmp et strlen dans keygenme_O0
frida-trace -f ./keygenme_O0 -i "strcmp" -i "strlen"
```

Sortie typique :

```
Instrumenting...  
strcmp: Auto-generated handler at /tmp/__handlers__/libc.so.6/strcmp.js  
strlen: Auto-generated handler at /tmp/__handlers__/libc.so.6/strlen.js  
Started tracing 2 functions. Press Ctrl+C to stop.  

           /* TID 0x1234 */
  3245 ms  strcmp(s1="SECRETKEY", s2="userinput")
  3245 ms  strlen(s="userinput")
```

Ce qui vient de se passer est remarquable : en une seule commande, sans aucun script, vous avez obtenu les arguments de `strcmp` — et potentiellement la clé secrète du crackme. Ce qui aurait nécessité un breakpoint conditionnel dans GDB, puis une inspection manuelle des registres `rdi` et `rsi`, se fait ici en quelques secondes.

**Les options de sélection de fonctions :**

L'option `-i` (pour *include*) accepte des noms de fonctions et des globs :

```bash
# Toutes les fonctions commençant par "str"
frida-trace -f ./keygenme_O0 -i "str*"

# Toutes les fonctions contenant "crypt"
frida-trace -f ./keygenme_O0 -i "*crypt*"

# Combiner plusieurs patterns
frida-trace -f ./keygenme_O0 -i "strcmp" -i "memcmp" -i "open"
```

L'option `-x` (pour *exclude*) exclut des fonctions du traçage :

```bash
# Tracer toutes les fonctions "str*" sauf strlen
frida-trace -f ./keygenme_O0 -i "str*" -x "strlen"
```

L'option `-I` (majuscule) filtre par module (bibliothèque) :

```bash
# Uniquement les fonctions du binaire principal, pas de la libc
frida-trace -f ./keygenme_O0 -I "keygenme_O0"
```

**Les handlers auto-générés.** `frida-trace` crée des fichiers JavaScript dans un dossier `__handlers__/`. Chaque fichier contient un squelette de hook que vous pouvez modifier :

```javascript
// __handlers__/libc.so.6/strcmp.js (auto-généré)
{
  onEnter(log, args, state) {
    log('strcmp(' +
      'a="' + args[0].readUtf8String() + '"' +
      ', b="' + args[1].readUtf8String() + '"' +
    ')');
  },
  onLeave(log, retval, state) {
    // log('strcmp retval=' + retval);
  }
}
```

Vous pouvez éditer ce fichier pour ajouter de la logique — filtrer certains appels, logger la valeur de retour, sauvegarder les arguments dans un fichier. Au prochain lancement de `frida-trace`, il réutilisera votre version modifiée au lieu d'en générer une nouvelle. C'est un chemin de progression naturel : on commence par le traçage brut auto-généré, puis on raffine les handlers au fur et à mesure que l'on comprend mieux le binaire.

**Tracer les appels système avec `-S` (Stalker).** Depuis les versions récentes, `frida-trace` peut aussi tracer les appels système directement :

```bash
frida-trace -f ./keygenme_O0 -S open -S read -S write
```

### `frida-kill` — terminer un processus

Simple utilitaire pour tuer un processus par PID, utile dans des scripts d'automatisation :

```bash
frida-kill 4567
```

### `frida-ls-devices` — lister les appareils accessibles

Liste les « devices » Frida : la machine locale, les appareils USB connectés (Android/iOS), et les instances `frida-server` distantes :

```bash
frida-ls-devices
```

---

## Scripting Python : le mode production

Les outils CLI sont parfaits pour l'exploration, mais les scénarios de RE complexes nécessitent un script Python structuré. Le module `frida` en Python offre un contrôle total sur le cycle de vie de l'instrumentation.

### Anatomie d'un script Python Frida

Un script Python Frida suit toujours la même structure en cinq étapes :

```python
import frida  
import sys  

# 1. Code JavaScript de l'agent (exécuté DANS le processus cible)
agent_code = """
'use strict';

// Hook sur strcmp
Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        this.arg0 = args[0].readUtf8String();
        this.arg1 = args[1].readUtf8String();
    },
    onLeave(retval) {
        if (retval.toInt32() === 0) {
            send({
                event: "strcmp_match",
                a: this.arg0,
                b: this.arg1
            });
        }
    }
});
"""

# 2. Callback pour les messages reçus de l'agent
def on_message(message, data):
    if message['type'] == 'send':
        payload = message['payload']
        print(f"[*] Match strcmp : '{payload['a']}' == '{payload['b']}'")
    elif message['type'] == 'error':
        print(f"[!] Erreur agent : {message['stack']}")

# 3. Lancement du processus (spawn)
pid = frida.spawn(["./keygenme_O0"])  
session = frida.attach(pid)  

# 4. Chargement et activation du script
script = session.create_script(agent_code)  
script.on('message', on_message)  
script.load()  

# 5. Reprise de l'exécution et attente
frida.resume(pid)  
print("[*] Hooks actifs. Ctrl+C pour quitter.")  
sys.stdin.read()  # Bloque jusqu'à interruption  
```

Détaillons les étapes clés.

**Étape 1 : le code agent.** C'est une chaîne de caractères Python contenant du JavaScript. Ce code sera exécuté par le moteur V8 à l'intérieur du processus cible. On utilise `send()` pour envoyer des données au script Python, et l'API Frida (`Interceptor`, `Module`, `Memory`…) pour interagir avec le processus.

> 💡 **Bonne pratique** : utiliser `'use strict';` en début de script agent pour activer le mode strict de JavaScript, qui transforme les erreurs silencieuses en erreurs explicites et aide au débogage.

**Étape 2 : le callback `on_message`.** Chaque appel à `send()` dans l'agent déclenche ce callback côté Python. Le paramètre `message` est un dictionnaire avec un champ `type` (`'send'` pour les messages normaux, `'error'` pour les exceptions JavaScript) et un champ `payload` contenant le JSON envoyé par `send()`. Le paramètre `data` contient les données binaires optionnelles (nous verrons ce mécanisme en section 13.4).

**Étape 3 : spawn + attach.** `frida.spawn()` crée le processus en état suspendu et retourne son PID. `frida.attach()` connecte une session Frida à ce PID. Ces deux étapes sont séparées pour permettre d'installer les hooks entre les deux.

**Étape 5 : resume + attente.** `frida.resume()` relâche le processus. `sys.stdin.read()` bloque le script Python indéfiniment (jusqu'à Ctrl+C), ce qui maintient la session Frida active. Sans cette attente, le script Python se terminerait, la session serait détruite, et l'agent serait déchargé du processus.

### Variante : attach à un processus existant

```python
# Au lieu de spawn + resume :
session = frida.attach("keygenme_O0")  # ou frida.attach(pid)

script = session.create_script(agent_code)  
script.on('message', on_message)  
script.load()  
# Pas de resume() — le processus tournait déjà

sys.stdin.read()
```

### Variante : détecter la fin du processus

Quand le processus cible se termine (sortie normale, crash, kill), la session Frida est détruite. On peut réagir à cet événement :

```python
def on_detached(reason):
    print(f"[*] Détaché : {reason}")
    # reason: "application-requested", "process-terminated", "server-terminated"...

session.on('detached', on_detached)
```

Cela permet d'écrire des scripts robustes qui ne restent pas bloqués après la fin du processus cible, ou qui relancent automatiquement l'instrumentation.

---

## Passage d'arguments au processus cible

En mode spawn, les arguments en ligne de commande sont passés au processus via la liste fournie à `frida.spawn()` :

```python
# Équivalent de : ./keygenme_O0 monmotdepasse
pid = frida.spawn(["./keygenme_O0", "monmotdepasse"])
```

En CLI :

```bash
frida -f ./keygenme_O0 -l hooks.js -- monmotdepasse
```

Le double tiret `--` sépare les arguments de Frida des arguments du programme cible. Tout ce qui suit `--` est transmis au processus.

Pour les programmes qui lisent leur entrée sur `stdin` plutôt que via des arguments, la situation est plus délicate en mode spawn, car Frida contrôle le lancement. En Python, le processus hérite du `stdin` du script Python, ce qui permet de lui envoyer des données via un pipe :

```bash
echo "monmotdepasse" | python3 mon_script_frida.py
```

Ou en redirigeant un fichier :

```bash
python3 mon_script_frida.py < inputs.txt
```

---

## Variables d'environnement et répertoire de travail

`frida.spawn()` accepte des options supplémentaires pour configurer l'environnement du processus :

```python
pid = frida.spawn(
    ["./keygenme_O0"],
    env={
        "LD_LIBRARY_PATH": "/opt/libs",
        "MY_DEBUG_FLAG": "1"
    },
    cwd="/home/user/binaries/ch13-keygenme"
)
```

Cela évite de modifier l'environnement du script Python lui-même et donne un contrôle précis sur le contexte d'exécution du processus cible.

---

## Sessions, scripts et cycle de vie

Quand on commence à écrire des scripts Frida non triviaux, il est important de comprendre la hiérarchie des objets et leur durée de vie.

**Device** → Le système sur lequel tourne le processus cible (machine locale, appareil USB, serveur distant). Obtenu implicitement ou via `frida.get_local_device()`.

**Session** → La connexion à un processus spécifique. Créée par `frida.attach()`. Détruite quand le processus se termine, quand on appelle `session.detach()`, ou quand le script Python se termine.

**Script** → Un script JavaScript chargé dans une session. On peut charger plusieurs scripts dans la même session. Chaque script a son propre scope JavaScript, mais ils partagent le même espace d'adressage (puisqu'ils s'exécutent dans le même processus).

```python
session = frida.attach(pid)

# Deux scripts indépendants dans la même session
script_hooks = session.create_script(code_hooks_js)  
script_monitor = session.create_script(code_monitoring_js)  

script_hooks.load()  
script_monitor.load()  
```

La possibilité de charger plusieurs scripts est utile pour séparer les préoccupations : un script pour les hooks de fonctions, un autre pour le monitoring mémoire, un troisième pour la couverture de code. Chacun envoie ses messages avec un format distinct, et le callback Python les aiguille.

---

## Spawn gating : intercepter les processus enfants

Certains programmes créent des processus enfants via `fork()` ou `exec()`. Par défaut, Frida ne suit que le processus parent. Le **spawn gating** permet d'intercepter automatiquement les processus enfants :

```python
device = frida.get_local_device()  
device.on('child-added', on_child_added)  
device.enable_spawn_gating()  

def on_child_added(child):
    print(f"[*] Nouveau processus enfant : PID {child.pid}")
    child_session = device.attach(child.pid)
    # Instrumenter l'enfant...
    device.resume(child.pid)
```

Ce mécanisme est particulièrement utile lors de l'analyse de malwares qui se forquent pour contourner le débogage (le parent meurt, l'enfant continue), ou de services réseau qui créent un processus enfant par connexion client.

---

## Récapitulatif des commandes et options

| Commande | Mode | Usage |  
|---|---|---|  
| `frida -p <PID> -l script.js` | attach | Instrumenter un processus existant |  
| `frida -n <nom> -l script.js` | attach | Idem, par nom de processus |  
| `frida -f ./binaire -l script.js` | spawn | Lancer et instrumenter dès le départ |  
| `frida -f ./binaire -l script.js --no-pause` | spawn | Spawn sans attendre `%resume` |  
| `frida -f ./binaire -- arg1 arg2` | spawn | Passer des arguments au binaire |  
| `frida-trace -f ./binaire -i "func"` | spawn | Traçage rapide auto-généré |  
| `frida-trace -n <nom> -i "func*"` | attach | Traçage avec globs sur processus existant |  
| `frida-ps` | — | Lister les processus |  
| `frida-kill <PID>` | — | Terminer un processus |

---

## Ce qu'il faut retenir

- **Attach** s'accroche à un processus existant — pratique mais on rate tout ce qui s'est exécuté avant l'injection.  
- **Spawn** lance le binaire en état suspendu, injecte l'agent, puis reprend — garantit de ne rien rater, indispensable pour le code d'initialisation.  
- **`frida`** ouvre un REPL JavaScript interactif, idéal pour l'exploration.  
- **`frida-trace`** génère automatiquement des hooks pour les fonctions spécifiées — l'outil le plus rapide pour un premier aperçu du comportement dynamique d'un binaire.  
- En **Python**, le cycle est `spawn` → `attach` → `create_script` → `load` → `resume`, avec un callback `on_message` pour recevoir les données de l'agent.  
- Le flag `--no-pause` et la méthode `frida.resume()` contrôlent le moment où le processus commence réellement son exécution.  
- Le **spawn gating** permet de suivre les processus enfants créés par `fork`/`exec`.

---

> **Prochaine section** : 13.3 — Hooking de fonctions C et C++ à la volée — nous allons plonger dans l'API `Interceptor` et apprendre à poser des hooks sur des fonctions avec et sans symboles.

⏭️ [Hooking de fonctions C et C++ à la volée](/13-frida/03-hooking-fonctions-c-cpp.md)
