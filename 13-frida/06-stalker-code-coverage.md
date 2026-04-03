🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 13.6 — Stalker : tracer toutes les instructions exécutées (code coverage dynamique)

> 🧰 **Outils utilisés** : `frida`, Python 3 + module `frida`  
> 📦 **Binaires utilisés** : `binaries/ch13-keygenme/keygenme_O0`, `binaries/ch25-fileformat/fileformat_O0`  
> 📖 **Prérequis** : [13.3 — Hooking de fonctions](/13-frida/03-hooking-fonctions-c-cpp.md), [13.5 — Modifier arguments et retours](/13-frida/05-modifier-arguments-retour.md), [Chapitre 3 — Assembleur x86-64](/03-assembleur-x86-64/README.md)

---

## Au-delà du hooking de fonctions

Avec `Interceptor`, on place des sondes à l'entrée et à la sortie de fonctions précises. C'est un éclairage chirurgical — on illumine exactement les points qu'on a choisis, et le reste du programme demeure dans l'ombre. Mais certaines questions de RE exigent une vue d'ensemble : quelles instructions ont été exécutées ? Quels blocs basiques ont été traversés ? Quels chemins de branchement ont été empruntés — et lesquels n'ont jamais été atteints ?

C'est le domaine du **code coverage dynamique**, et Frida y répond avec un moteur dédié : **Stalker**.

Stalker suit un thread instruction par instruction, en temps réel, pendant qu'il s'exécute. Il ne pose pas de breakpoints. Il ne modifie pas le code original de manière permanente. À la place, il utilise une technique appelée **dynamic binary translation** (traduction binaire dynamique) : le code machine du processus est copié, instrumenté à la volée, puis exécuté depuis cette copie. Le thread cible ne parcourt jamais le code original — il exécute en permanence du code traduit par Stalker, qui contient des appels à vos callbacks entre chaque instruction, appel ou branchement.

Le résultat : une trace complète de l'exécution, à la granularité que vous choisissez — instruction par instruction, bloc basique par bloc basique, ou appel par appel.

---

## Principes de fonctionnement

### Traduction binaire dynamique

Quand Stalker commence à suivre un thread, il intercepte le flux d'exécution et procède ainsi :

1. **Lecture du bloc basique courant** — Stalker lit les instructions machine à l'adresse courante du thread, jusqu'au prochain branchement (un `jmp`, `jcc`, `call`, `ret` ou toute instruction qui modifie `rip`).

2. **Copie et instrumentation** — Le bloc est copié dans une zone mémoire gérée par Stalker (le « slab »). Des instructions supplémentaires sont insérées dans la copie — des appels à vos callbacks, des compteurs, des écritures dans un buffer d'événements.

3. **Exécution de la copie** — Le thread est redirigé vers la copie instrumentée. Il exécute le bloc traduit au lieu de l'original.

4. **Suivi du branchement** — Quand le bloc traduit se termine (branchement, call, ret), Stalker intercepte la destination, traduit le bloc suivant, et le cycle recommence.

Ce processus est transparent pour le programme : les instructions exécutées produisent exactement le même résultat que les originales, les registres et la mémoire évoluent de manière identique. La seule différence observable est un ralentissement — le code traduit est plus volumineux et les callbacks ajoutent un surcoût.

### Différence avec Interceptor

La distinction est fondamentale et conditionne le choix de l'outil :

| Critère | Interceptor | Stalker |  
|---|---|---|  
| **Granularité** | Point d'entrée/sortie de fonctions | Chaque instruction, bloc, ou appel |  
| **Portée** | Fonctions choisies explicitement | Tout le code exécuté par un thread |  
| **Surcoût** | Minimal (un trampoline par hook) | Important (traduction de tout le code) |  
| **Données produites** | Arguments et retours de fonctions ciblées | Trace complète d'exécution |  
| **Cas d'usage** | Observer/modifier des fonctions spécifiques | Cartographier le code exécuté, analyser les chemins |

En pratique, on utilise Stalker quand on ne sait pas encore quelles fonctions sont intéressantes — quand on veut une vue d'ensemble avant de zoomer. Une fois les zones identifiées, on repasse à Interceptor pour un hooking ciblé et performant.

---

## API de base : `Stalker.follow` et `Stalker.unfollow`

### Suivre un thread

```javascript
const threadId = Process.getCurrentThreadId();

Stalker.follow(threadId, {
    events: {
        call: true,    // tracer les instructions CALL
        ret: false,    // ne pas tracer les RET
        exec: false,   // ne pas tracer chaque instruction individuellement
        block: false,  // ne pas tracer les blocs basiques
        compile: false // ne pas signaler la compilation des blocs
    },
    onReceive(events) {
        // Appelé périodiquement avec un lot d'événements
        const parsed = Stalker.parse(events);
        console.log(JSON.stringify(parsed, null, 2));
    }
});
```

`Stalker.follow` prend un thread ID et un objet de configuration. Le champ `events` est un masque qui contrôle quels types d'événements sont générés. Le callback `onReceive` est invoqué périodiquement (pas à chaque événement — les événements sont bufférisés pour les performances) avec un buffer binaire compact que `Stalker.parse` décode en tableau JavaScript.

### Arrêter le suivi

```javascript
Stalker.unfollow(threadId);
```

Ou pour arrêter le suivi du thread courant :

```javascript
Stalker.unfollow();
```

Après `unfollow`, le thread reprend son exécution normale sur le code original, sans aucune instrumentation résiduelle.

### Suivre le thread principal au spawn

Le cas le plus courant est de suivre le thread principal dès le lancement du programme :

```javascript
// En mode spawn, le thread principal est celui qui exécute main()
Stalker.follow(Process.getCurrentThreadId(), {
    events: { call: true, ret: true },
    onReceive(events) {
        // ...
    }
});
```

En combinant avec le mode spawn de Frida (section 13.2), on obtient la trace complète de l'exécution dès la première instruction.

---

## Les types d'événements

### `call` — appels de fonctions

Chaque instruction `call` génère un événement contenant l'adresse de l'instruction `call` elle-même et l'adresse cible (la fonction appelée). C'est l'événement le plus utilisé pour la vue d'ensemble.

```javascript
Stalker.follow(threadId, {
    events: { call: true },
    onReceive(events) {
        const parsed = Stalker.parse(events, { annotate: true, stringify: true });
        parsed.forEach(ev => {
            if (ev[0] === 'call') {
                const from = ev[1];    // adresse du call
                const target = ev[2];  // adresse cible
                const fromSym = DebugSymbol.fromAddress(ptr(from));
                const targetSym = DebugSymbol.fromAddress(ptr(target));
                console.log(`CALL ${fromSym} → ${targetSym}`);
            }
        });
    }
});
```

### `ret` — retours de fonctions

Chaque instruction `ret` génère un événement avec l'adresse du `ret` et l'adresse de retour (où le contrôle revient). Combiné avec `call`, on obtient un graphe d'appels dynamique complet.

### `exec` — chaque instruction exécutée

C'est le mode le plus fin et le plus coûteux. Chaque instruction individuelle génère un événement. Le volume de données est colossal — un programme peut exécuter des millions d'instructions par seconde. Ce mode n'est utilisable que sur de courtes séquences d'exécution ou avec un filtrage agressif.

### `block` — blocs basiques

Un bloc basique est une séquence linéaire d'instructions sans branchement interne — il commence à une cible de saut ou de call, et se termine par un branchement. C'est la granularité naturelle pour le code coverage : savoir quels blocs basiques ont été exécutés donne une carte précise du code traversé, sans le surcoût d'un suivi instruction par instruction.

### `compile` — compilation de blocs

Cet événement est émis quand Stalker traduit un nouveau bloc basique pour la première fois. C'est un indicateur indirect de « nouveau code atteint » — si un bloc est compilé, c'est qu'il n'avait pas encore été exécuté dans cette session de Stalker. Ce mode est très léger et utile pour le coverage-guided fuzzing.

---

## `transform` : instrumenter le code traduit

Le callback `transform` est la fonctionnalité la plus puissante de Stalker. Il est appelé quand Stalker traduit un bloc basique, et vous donne accès aux instructions du bloc **avant qu'elles ne soient émises** dans la copie. Vous pouvez inspecter chaque instruction, ajouter des instructions supplémentaires, ou même supprimer des instructions.

### Observer chaque instruction

```javascript
Stalker.follow(threadId, {
    transform(iterator) {
        let instruction = iterator.next();

        do {
            // Afficher chaque instruction
            console.log(`${instruction.address}: ${instruction.mnemonic} ${instruction.opStr}`);

            // Émettre l'instruction dans la copie (indispensable)
            iterator.keep();

        } while ((instruction = iterator.next()) !== null);
    }
});
```

Le pattern est toujours le même : `iterator.next()` avance à l'instruction suivante du bloc, et `iterator.keep()` l'émet dans la copie traduite. Si vous appelez `iterator.next()` sans appeler `iterator.keep()`, l'instruction est **supprimée** de la copie — elle ne sera pas exécutée. C'est un mécanisme de patching dynamique extrêmement puissant.

> ⚠️ Ne jamais oublier `iterator.keep()`. Si une instruction est lue avec `next()` mais pas émise avec `keep()`, elle disparaît de l'exécution. Supprimer accidentellement une instruction critique (comme un `ret` ou un `push rbp`) provoque un crash immédiat.

### Filtrer par module

En pratique, on ne veut pas instrumenter le code de la libc, du loader, ou d'autres bibliothèques — seulement le code du binaire analysé. `transform` est appelé pour **tous** les blocs de code exécutés par le thread, toutes bibliothèques confondues. Le filtrage par plage d'adresses est indispensable :

```javascript
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

Stalker.follow(threadId, {
    transform(iterator) {
        let instruction = iterator.next();
        const isMainModule = instruction.address.compare(modBase) >= 0 &&
                             instruction.address.compare(modEnd) < 0;

        do {
            if (isMainModule) {
                console.log(`${instruction.address}: ${instruction.mnemonic} ${instruction.opStr}`);
            }
            iterator.keep();
        } while ((instruction = iterator.next()) !== null);
    }
});
```

On vérifie l'adresse de la première instruction du bloc (toutes les instructions d'un même bloc basique sont dans le même module, par définition). Si le bloc est dans le module principal, on loggue ; sinon, on émet les instructions silencieusement.

### Injecter des callouts

Plutôt que de logguer dans `transform` (qui est appelé une seule fois lors de la compilation du bloc), on peut injecter un **callout** — un callback JavaScript qui sera appelé chaque fois que l'instruction est exécutée :

```javascript
Stalker.follow(threadId, {
    transform(iterator) {
        let instruction = iterator.next();

        do {
            // Si c'est un CMP suivi d'un saut conditionnel, injecter un callout
            if (instruction.mnemonic === 'cmp') {
                iterator.putCallout((context) => {
                    console.log(`CMP exécuté @ ${context.pc}`);
                    console.log(`  rax=${context.rax} rbx=${context.rbx}`);
                    console.log(`  rdi=${context.rdi} rsi=${context.rsi}`);
                });
            }

            iterator.keep();
        } while ((instruction = iterator.next()) !== null);
    }
});
```

`iterator.putCallout(callback)` insère un appel à `callback` juste avant l'instruction courante dans le code traduit. Le callback reçoit un objet `context` identique à `this.context` dans Interceptor — tous les registres sont lisibles et modifiables.

Les callouts sont l'outil idéal pour instrumenter des instructions spécifiques (comparaisons, accès mémoire, sauts) sans le surcoût d'un logging systématique de chaque instruction.

---

## Construire une carte de code coverage

L'application la plus directe de Stalker en RE est la construction d'une carte de couverture : quels blocs basiques du binaire ont été exécutés pour un input donné ?

### Collecter les adresses de blocs

```javascript
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

const coveredBlocks = new Set();

Stalker.follow(Process.getCurrentThreadId(), {
    transform(iterator) {
        let instruction = iterator.next();
        const blockAddr = instruction.address;

        const isMainModule = blockAddr.compare(modBase) >= 0 &&
                             blockAddr.compare(modEnd) < 0;

        if (isMainModule) {
            // Enregistrer l'adresse du bloc (offset relatif à la base)
            const offset = blockAddr.sub(modBase).toInt32();
            coveredBlocks.add(offset);
        }

        do {
            iterator.keep();
        } while ((instruction = iterator.next()) !== null);
    }
});
```

Le callback `transform` est appelé une fois par bloc basique, lors de sa première exécution. C'est exactement ce dont on a besoin pour le coverage : chaque bloc est enregistré une seule fois, et l'overhead est minimal puisque le callback ne se déclenche pas aux exécutions suivantes du même bloc.

### Exporter la couverture vers Python

```javascript
// Quand le programme se termine ou sur demande
recv('dump_coverage', () => {
    const offsets = Array.from(coveredBlocks).sort((a, b) => a - b);
    send({
        event: 'coverage',
        module: mod.name,
        base: modBase.toString(),
        blocks: offsets,
        count: offsets.length
    });
});
```

```python
def on_message(message, data):
    if message['type'] == 'send':
        payload = message['payload']
        if payload.get('event') == 'coverage':
            blocks = payload['blocks']
            print(f"[*] Couverture : {payload['count']} blocs basiques atteints")

            # Sauvegarder au format compatible avec d'autres outils
            with open("coverage.txt", "w") as f:
                for offset in blocks:
                    f.write(f"0x{offset:x}\n")

# Demander le dump
script.post({'type': 'dump_coverage'})
```

### Exploiter la couverture dans Ghidra

La liste d'offsets exportée peut être importée dans Ghidra pour coloriser les blocs exécutés. Un script Ghidra Python simple parcourt la liste et applique une couleur :

```python
# Script Ghidra (Jython) — à exécuter dans le Script Manager
from ghidra.app.plugin.core.colorizer import ColorizingService  
from java.awt import Color  

service = state.getTool().getService(ColorizingService)  
base = currentProgram.getImageBase()  

with open("/tmp/coverage.txt") as f:
    for line in f:
        offset = int(line.strip(), 16)
        addr = base.add(offset)
        service.setBackgroundColor(addr, addr.add(1), Color.GREEN)
```

Les blocs verts sont ceux qui ont été exécutés. Les blocs non colorés représentent du code mort pour cet input — des branches non empruntées, des gestionnaires d'erreur non déclenchés, des chemins conditionnels inexplorés. C'est une information précieuse pour guider l'analyse : les blocs non couverts sont souvent ceux qui contiennent la logique la plus intéressante (traitement d'erreur, chemins secrets, code de débogage).

### Format drcov pour la compatibilité

Pour une intégration avec des outils standard de coverage (notamment le plugin `lighthouse` pour IDA/Ghidra et Binary Ninja), on peut exporter au format **drcov** (DynamoRIO coverage) :

```javascript
const coveredBlocksInfo = [];

Stalker.follow(Process.getCurrentThreadId(), {
    transform(iterator) {
        let instruction = iterator.next();
        const blockStart = instruction.address;
        let blockSize = 0;

        const isMainModule = blockStart.compare(modBase) >= 0 &&
                             blockStart.compare(modEnd) < 0;

        do {
            if (isMainModule) {
                blockSize += instruction.size;
            }
            iterator.keep();
        } while ((instruction = iterator.next()) !== null);

        if (isMainModule && blockSize > 0) {
            coveredBlocksInfo.push({
                start: blockStart.sub(modBase).toInt32(),
                size: blockSize
            });
        }
    }
});
```

On collecte non seulement l'adresse de début de chaque bloc, mais aussi sa taille (somme des tailles des instructions du bloc). Le format drcov a besoin de ces deux informations.

```python
# Exporter au format drcov
def export_drcov(module_name, module_base, module_size, blocks, filename):
    with open(filename, 'wb') as f:
        f.write(b"DRCOV VERSION: 2\n")
        f.write(b"DRCOV FLAVOR: frida\n")
        f.write(f"Module Table: version 2, count 1\n".encode())
        f.write(b"Columns: id, base, end, entry, path\n")
        f.write(f"  0, {module_base}, {hex(int(module_base, 16) + module_size)}, 0x0, {module_name}\n".encode())
        f.write(f"BB Table: {len(blocks)} bbs\n".encode())

        import struct
        for block in blocks:
            # Format drcov : start(uint32), size(uint16), mod_id(uint16)
            f.write(struct.pack('<IHH', block['start'], block['size'], 0))

    print(f"[*] drcov exporté : {filename} ({len(blocks)} blocs)")
```

Ce fichier peut être directement ouvert dans lighthouse (Ghidra/IDA) ou Binary Ninja pour une visualisation interactive de la couverture, avec des statistiques de coverage par fonction et un code couleur appliqué automatiquement.

---

## Couverture comparative : deux inputs, deux traces

Une technique de RE puissante consiste à comparer la couverture entre deux exécutions — une avec un input valide et une avec un input invalide. Les blocs qui diffèrent entre les deux traces sont exactement ceux qui implémentent la logique de validation.

### Approche avec Python

```python
import frida  
import sys  

AGENT_CODE = """
'use strict';

const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  
const covered = new Set();  

Stalker.follow(Process.getCurrentThreadId(), {
    transform(iterator) {
        let insn = iterator.next();
        const addr = insn.address;
        if (addr.compare(modBase) >= 0 && addr.compare(modEnd) < 0) {
            covered.add(addr.sub(modBase).toInt32());
        }
        do { iterator.keep(); } while ((insn = iterator.next()) !== null);
    }
});

recv('dump', () => {
    send({ blocks: Array.from(covered) });
    Stalker.unfollow();
    Stalker.garbageCollect();
});
"""

def get_coverage(binary, input_data):
    """Lance le binaire avec input_data et retourne les blocs couverts."""
    blocks = []

    def on_msg(msg, data):
        nonlocal blocks
        if msg['type'] == 'send':
            blocks = set(msg['payload']['blocks'])

    pid = frida.spawn([binary], stdin=input_data.encode())
    session = frida.attach(pid)
    script = session.create_script(AGENT_CODE)
    script.on('message', on_msg)
    script.load()
    frida.resume(pid)

    import time
    time.sleep(2)  # Laisser le programme s'exécuter
    script.post({'type': 'dump'})
    time.sleep(1)
    session.detach()

    return blocks

# Exécuter avec deux inputs différents
cov_valid = get_coverage("./keygenme_O0", "CORRECT_KEY")  
cov_invalid = get_coverage("./keygenme_O0", "wrong_input")  

# Blocs exécutés uniquement avec l'input valide
only_valid = cov_valid - cov_invalid
# Blocs exécutés uniquement avec l'input invalide
only_invalid = cov_invalid - cov_valid
# Blocs communs
common = cov_valid & cov_invalid

print(f"Blocs communs         : {len(common)}")  
print(f"Uniquement input OK   : {len(only_valid)}")  
print(f"Uniquement input KO   : {len(only_invalid)}")  

print("\nBlocs spécifiques à l'input valide (offsets) :")  
for offset in sorted(only_valid):  
    print(f"  0x{offset:x}")
```

Les offsets listés dans `only_valid` pointent directement vers le code qui s'exécute quand la clé est correcte — le chemin « succès ». En les examinant dans Ghidra, on identifie la branche de validation et on comprend la logique de vérification.

---

## Stalker sur des portions ciblées

Suivre un thread entier pendant toute l'exécution du programme est coûteux. Souvent, on veut activer Stalker uniquement pendant une phase précise — par exemple, pendant l'exécution d'une fonction spécifique. La combinaison d'Interceptor et de Stalker permet exactement cela.

### Activer Stalker pendant l'exécution d'une fonction

```javascript
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  
const targetFunc = modBase.add(0x11a9);  // check_password  

const coveredInFunc = new Set();

Interceptor.attach(targetFunc, {
    onEnter(args) {
        this.tid = Process.getCurrentThreadId();

        Stalker.follow(this.tid, {
            transform(iterator) {
                let insn = iterator.next();
                const addr = insn.address;
                if (addr.compare(modBase) >= 0 && addr.compare(modEnd) < 0) {
                    coveredInFunc.add(addr.sub(modBase).toInt32());
                }
                do { iterator.keep(); } while ((insn = iterator.next()) !== null);
            }
        });

        console.log("[Stalker] Activé pour check_password()");
    },
    onLeave(retval) {
        Stalker.unfollow(this.tid);
        Stalker.garbageCollect();

        console.log(`[Stalker] Désactivé. ${coveredInFunc.size} blocs tracés.`);
        send({ event: 'func_coverage', blocks: Array.from(coveredInFunc) });
    }
});
```

Stalker s'active quand `check_password` commence et se désactive quand elle retourne. On ne trace que le code exécuté pendant cette fonction — y compris les sous-fonctions qu'elle appelle. Le surcoût de Stalker ne s'applique que pendant cette fenêtre.

---

## Tracer les appels de fonctions (graphe d'appels dynamique)

En combinant les événements `call` et `ret`, on peut reconstruire le graphe d'appels dynamique — l'arbre des fonctions réellement appelées pendant l'exécution.

```javascript
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

let depth = 0;

Stalker.follow(Process.getCurrentThreadId(), {
    events: { call: true, ret: true },
    onReceive(events) {
        const parsed = Stalker.parse(events, { annotate: true, stringify: true });
        parsed.forEach(ev => {
            const type = ev[0];

            if (type === 'call') {
                const target = ptr(ev[2]);
                if (target.compare(modBase) >= 0 && target.compare(modEnd) < 0) {
                    const sym = DebugSymbol.fromAddress(target);
                    const indent = '  '.repeat(depth);
                    console.log(`${indent}→ ${sym.name || target}`);
                    depth++;
                }
            } else if (type === 'ret') {
                const from = ptr(ev[1]);
                if (from.compare(modBase) >= 0 && from.compare(modEnd) < 0) {
                    depth = Math.max(0, depth - 1);
                }
            }
        });
    }
});
```

Sortie typique :

```
→ main
  → init_config
    → read_file
    → parse_config
  → check_password
    → hash_input
    → compare_hash
  → print_result
```

Cette vue hiérarchique est l'équivalent dynamique du graphe d'appels de Ghidra (chapitre 8, section 8.7) ou du graphe de Callgrind/KCachegrind (chapitre 14, section 14.2), mais avec les vrais chemins d'exécution au lieu de toutes les possibilités statiques.

---

## Performances et bonnes pratiques

Stalker est le composant le plus gourmand de Frida. Quelques règles pour maintenir des performances acceptables :

**Limitez la portée temporelle.** Activez Stalker uniquement pendant la phase d'exécution intéressante, pas pendant toute la durée de vie du programme. Utilisez Interceptor pour déclencher et arrêter Stalker au bon moment.

**Filtrez par module.** Ne loguez que les blocs du binaire principal. Le code de la libc, du loader, et des bibliothèques génère un volume énorme de blocs qui n'intéressent généralement pas l'analyse.

**Préférez `transform` à `events: { exec: true }`.** Le mode `exec` génère un événement par instruction et les bufférise pour `onReceive`. Le callback `transform` est appelé une seule fois par bloc lors de sa compilation. Pour le coverage, `transform` est infiniment plus efficace.

**Appelez `Stalker.garbageCollect()`.** Après un `unfollow`, le code traduit reste en mémoire. `garbageCollect()` libère les blocs traduits qui ne sont plus nécessaires. Appelez-le après chaque cycle follow/unfollow pour éviter l'accumulation mémoire.

**Attention aux threads.** Stalker suit un thread à la fois. Si le programme est multi-threadé, vous devez appeler `Stalker.follow` sur chaque thread d'intérêt, ou énumérer les threads et tous les suivre :

```javascript
Process.enumerateThreads().forEach(thread => {
    Stalker.follow(thread.id, { /* ... */ });
});
```

**Le `flush`.** Par défaut, les événements sont bufférisés. Pour forcer l'envoi immédiat :

```javascript
Stalker.flush();
```

Utile juste avant un `unfollow` pour s'assurer que tous les événements ont été transmis.

---

## Stalker et fuzzing : le pont vers le chapitre 15

La carte de couverture produite par Stalker est exactement l'information dont un fuzzer coverage-guided a besoin pour décider si un input est « intéressant » (il a atteint du nouveau code). C'est le pont conceptuel entre ce chapitre et le chapitre 15 (Fuzzing).

En effet, on peut construire un mini-fuzzer avec Frida :

1. Générer un input.  
2. Lancer le programme avec cet input sous Stalker.  
3. Collecter la couverture.  
4. Si de nouveaux blocs ont été atteints, conserver l'input dans le corpus.  
5. Muter l'input et recommencer.

Cette approche artisanale n'atteint pas les performances d'AFL++ (chapitre 15, section 15.2), qui utilise une instrumentation à la compilation bien plus légère. Mais elle fonctionne sur des binaires fermés, sans recompilation — c'est précisément le contexte du reverse engineering.

---

## Ce qu'il faut retenir

- **Stalker** utilise la traduction binaire dynamique pour suivre l'exécution d'un thread instruction par instruction, sans breakpoints.  
- Les types d'événements (`call`, `ret`, `exec`, `block`, `compile`) offrent différents niveaux de granularité et de surcoût.  
- Le callback **`transform`** est le mécanisme le plus puissant : il permet d'inspecter, modifier ou supprimer des instructions pendant la traduction, et d'injecter des **callouts** exécutés à chaque passage.  
- La construction d'une **carte de couverture** (offsets des blocs basiques exécutés) est l'application principale en RE — elle révèle le code vivant vs le code mort pour un input donné.  
- L'export au format **drcov** permet une intégration avec lighthouse (Ghidra, IDA, Binary Ninja) pour la visualisation interactive.  
- La **couverture comparative** entre deux inputs identifie les blocs de code qui implémentent la logique de validation — une technique de localisation redoutablement efficace.  
- Combiner **Interceptor + Stalker** permet de limiter le traçage à une fenêtre d'exécution précise (pendant une fonction), ce qui maintient des performances acceptables.  
- `Stalker.garbageCollect()` et `Stalker.flush()` sont essentiels pour la gestion mémoire et la fiabilité des données.

---

> **Prochaine section** : 13.7 — Cas pratique : contourner une vérification de licence — nous mettrons en œuvre l'ensemble des techniques vues dans ce chapitre (Interceptor, modification de retours, Stalker) sur un scénario complet de contournement de protection logicielle.

⏭️ [Cas pratique : contourner une vérification de licence](/13-frida/07-cas-pratique-licence.md)
