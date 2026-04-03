🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 13.1 — Architecture de Frida — agent JS injecté dans le processus cible

> 🧰 **Outils utilisés** : `frida`, `frida-ps`, `frida-trace`, Python 3 + module `frida`  
> 📖 **Prérequis** : [Chapitre 2 — La chaîne de compilation GNU](/02-chaine-compilation-gnu/README.md) (sections sur le loader et les bibliothèques dynamiques), [Chapitre 5 — Outils d'inspection](/05-outils-inspection-base/README.md) (`strace`, `ltrace`, `/proc`)

---

## Vue d'ensemble : l'idée fondamentale

Pour comprendre Frida, il faut d'abord comprendre le problème qu'il résout.

Quand on utilise GDB, le débogueur est un **processus externe** qui contrôle la cible via l'appel système `ptrace`. Cette relation parent–enfant est visible par le noyau, par le processus lui-même (via `/proc/self/status`), et impose un coût structurel : chaque breakpoint déclenche un signal `SIGTRAP`, un changement de contexte vers GDB, une attente, puis une reprise. Le programme est alternativement figé et relancé.

Frida adopte une stratégie radicalement différente. Au lieu de contrôler le programme de l'extérieur, Frida **place un agent à l'intérieur du processus cible**. Cet agent est une bibliothèque partagée (`.so` sous Linux) qui embarque un moteur JavaScript complet. Une fois injecté, l'agent s'exécute dans le même espace d'adressage que le programme, avec les mêmes permissions mémoire, et peut manipuler le code et les données comme s'il en faisait partie — parce qu'il en fait partie.

C'est la distinction essentielle : **GDB observe de l'extérieur, Frida opère de l'intérieur.**

---

## Les composants de l'architecture

L'architecture de Frida repose sur quatre composants qui communiquent entre eux. Comprendre leur rôle et leurs interactions est indispensable pour utiliser l'outil efficacement et diagnostiquer les problèmes quand ils surviennent.

### Le client (votre script Python ou la CLI)

C'est le point d'entrée pour l'utilisateur. Le client s'exécute dans un processus séparé — typiquement un terminal où vous lancez `frida`, `frida-trace`, ou un script Python utilisant le module `frida`. Son rôle est de piloter l'ensemble : sélectionner le processus cible, envoyer le code JavaScript de l'agent, recevoir les messages en retour, et orchestrer le cycle de vie de l'instrumentation.

Le client ne touche jamais directement à la mémoire du processus cible. Il communique exclusivement avec le composant suivant.

```
┌─────────────────────────────┐
│   Client (Python / CLI)     │
│   - Pilotage                │
│   - Envoi du script JS      │
│   - Réception des messages  │
└──────────┬──────────────────┘
           │  communication inter-processus
           ▼
```

### Le service `frida-server` (ou l'injection directe)

Sur Linux en local, Frida utilise le plus souvent une **injection directe** via `ptrace` pour charger sa bibliothèque dans le processus cible. Ce recours à `ptrace` est bref et ponctuel : il sert uniquement à injecter le code de l'agent, puis Frida relâche le contrôle `ptrace`. Cela contraste avec GDB, qui maintient l'attachement `ptrace` pendant toute la session de débogage.

Dans d'autres configurations (débogage distant, instrumentation sur Android ou iOS), un démon `frida-server` tourne sur la machine cible et gère l'injection à la demande. Le client communique alors avec `frida-server` via un socket réseau (TCP par défaut, sur le port 27042).

```
           │
           ▼
┌─────────────────────────────────────┐
│   Injection (ptrace ou frida-server)│
│   - Charge frida-agent.so           │
│   - Relâche ptrace après injection  │
└──────────┬──────────────────────────┘
           │
           ▼
```

### L'agent (`frida-agent.so`)

C'est le cœur de Frida. L'agent est une bibliothèque partagée qui, une fois chargée dans le processus cible, démarre un **moteur JavaScript** (basé sur V8 de Google ou sur Duktape selon la configuration). Ce moteur exécute le script que vous avez envoyé depuis le client.

L'agent expose au JavaScript une API riche qui permet de :

- **Lire et écrire la mémoire** du processus (`Memory.read*`, `Memory.write*`, `Memory.scan`).  
- **Résoudre des symboles** par nom (`Module.findExportByName`, `Module.enumerateExports`).  
- **Intercepter des fonctions** à l'entrée et à la sortie (`Interceptor.attach`).  
- **Remplacer des fonctions** entièrement (`Interceptor.replace`).  
- **Allouer de la mémoire** et y écrire du code natif (`Memory.alloc`, `NativeFunction`, `NativeCallback`).  
- **Tracer l'exécution instruction par instruction** (`Stalker`).

L'agent vit dans un ou plusieurs threads dédiés à l'intérieur du processus cible. Le code JavaScript s'exécute dans le contexte de ces threads, mais les hooks qu'il installe sont déclenchés dans le contexte des threads originaux du programme — un point crucial que nous approfondirons dans les sections suivantes.

```
           │
           ▼
┌──────────────────────────────────────────────┐
│   Processus cible                            │
│                                              │
│   ┌────────────────────────────────────┐     │
│   │  frida-agent.so                    │     │
│   │  ┌──────────────────────────┐      │     │
│   │  │  Moteur JS (V8/Duktape)  │      │     │
│   │  │  - Votre script          │      │     │
│   │  │  - API Frida             │      │     │
│   │  └──────────────────────────┘      │     │
│   │  Interceptor, Stalker, Memory...   │     │
│   └────────────────────────────────────┘     │
│                                              │
│   ┌────────────────────────────────────┐     │
│   │  Code original du programme        │     │
│   │  (.text, .data, heap, stack...)    │     │
│   └────────────────────────────────────┘     │
│                                              │
└──────────────────────────────────────────────┘
```

### Le canal de communication

Le client et l'agent communiquent via un canal bidirectionnel de messages. Côté agent (JavaScript), on envoie des données au client avec la fonction `send()`. Côté client (Python), on reçoit ces messages via un callback `on_message`. Ce canal transporte du JSON, ce qui permet d'envoyer des structures de données arbitraires — arguments de fonctions interceptées, dumps mémoire encodés, compteurs, logs.

```python
# Côté client (Python)
def on_message(message, data):
    if message['type'] == 'send':
        print(message['payload'])

session = frida.attach("cible")  
script = session.create_script("""  
    // Côté agent (JavaScript, exécuté dans le processus cible)
    send({event: "hello", pid: Process.id});
""")
script.on('message', on_message)  
script.load()  
```

Le paramètre `data` du callback Python permet de recevoir des données binaires brutes (un buffer mémoire, un dump de structure) aux côtés du message JSON, sans passer par un encodage base64 coûteux.

---

## Le mécanisme d'injection en détail

Comprendre comment l'agent arrive dans le processus cible éclaire à la fois les capacités et les limites de Frida.

### Injection via `ptrace` (mode par défaut sous Linux)

Quand vous exécutez `frida -p <PID>` ou `frida.attach(pid)` en Python, voici la séquence qui se déroule :

1. **Attachement `ptrace`** — Frida s'attache brièvement au processus cible via `PTRACE_ATTACH`. Le processus est suspendu.

2. **Injection du bootstrapper** — Frida alloue une petite zone mémoire dans le processus (via un appel `mmap` contraint par `ptrace`) et y écrit un stub de code machine. Ce stub appelle `dlopen()` pour charger `frida-agent.so`.

3. **Exécution du bootstrapper** — Frida modifie le pointeur d'instruction (`rip`) du thread principal pour qu'il exécute le stub. Le processus reprend brièvement, `dlopen` charge l'agent, et le constructeur de l'agent (`__attribute__((constructor))`) initialise le moteur JavaScript.

4. **Détachement `ptrace`** — Une fois l'agent chargé et opérationnel, Frida restaure le contexte original des registres et se détache via `PTRACE_DETACH`. Le processus reprend son exécution normale, avec l'agent en plus.

À partir de ce point, toute l'instrumentation passe par l'agent interne — plus aucun `ptrace` n'est utilisé. C'est pourquoi Frida est compatible avec des programmes qui détectent `ptrace` : l'attachement est si bref que de nombreuses vérifications anti-debug passent à côté (bien que les protections les plus robustes puissent quand même le détecter, comme nous le verrons au chapitre 19).

### Mode spawn (`frida -f ./programme`)

Au lieu de s'attacher à un processus existant, Frida peut **lancer** le programme lui-même. Dans ce mode :

1. Frida crée le processus via `fork` + `exec`, mais le suspend avant que la première instruction du programme ne s'exécute (grâce à `ptrace` ou à un mécanisme équivalent).  
2. L'agent est injecté dans ce processus suspendu.  
3. Le programme est relancé, avec l'agent déjà en place.

Ce mode est essentiel quand on veut instrumenter du code qui s'exécute très tôt — un constructeur global C++, une routine d'initialisation, ou une vérification anti-debug exécutée au démarrage. En mode attach, ces routines auraient déjà été exécutées avant qu'on puisse intervenir.

### Conséquence sur les permissions

L'injection via `ptrace` requiert que le processus Frida ait les privilèges nécessaires pour s'attacher au processus cible. Sous Linux, cela signifie :

- Soit exécuter Frida en tant que `root`.  
- Soit que la valeur de `/proc/sys/kernel/yama/ptrace_scope` autorise l'attachement (la valeur `0` permet l'attachement libre, `1` restreint aux processus enfants).

Dans notre environnement de lab (VM dédiée, chapitre 4), travailler en `root` ou ajuster `ptrace_scope` est sans risque et simplifie la pratique.

---

## Frida-agent en mémoire : ce que voit le processus

Une fois l'injection terminée, le processus cible porte en lui des traces de la présence de Frida. Il est utile de savoir les reconnaître — à la fois pour comprendre ce qui se passe et pour anticiper les mécanismes de détection.

### Bibliothèques chargées

L'agent apparaît dans la carte mémoire du processus. Un `cat /proc/<PID>/maps` révèle des entrées liées à Frida :

```
7f3a8c000000-7f3a8c400000 rwxp 00000000 00:00 0    frida-agent-64.so
```

La présence de `frida-agent` dans `/proc/self/maps` est le vecteur de détection le plus simple. Un programme malveillant ou protégé peut lire sa propre carte mémoire et chercher la chaîne `"frida"`. Nous verrons dans la section 13.7 et au chapitre 19 comment certaines techniques tentent de masquer cette signature.

### Threads supplémentaires

L'agent crée un ou plusieurs threads pour le moteur JavaScript et pour le canal de communication. Un `ls /proc/<PID>/task/` montrera des TID (Thread IDs) supplémentaires qui n'existaient pas avant l'injection. Un programme qui surveille le nombre de ses propres threads peut y voir une anomalie.

### File descriptors et sockets

Le canal de communication entre l'agent et le client passe par un socket (souvent un pipe Unix ou une connexion TCP locale). Ces descripteurs de fichiers apparaissent dans `/proc/<PID>/fd/` et constituent un autre indice de la présence de Frida.

---

## Le moteur JavaScript : V8 vs Duktape

Frida embarque deux moteurs JavaScript, et le choix entre les deux a des implications pratiques.

**V8** (le moteur de Chrome et Node.js) est le choix par défaut. Il offre un compilateur JIT complet, des performances JavaScript excellentes, et le support d'ES6+ (classes, arrow functions, async/await, déstructuration). C'est le moteur que vous utiliserez dans 99 % des cas sur x86-64.

**Duktape** est un moteur JavaScript minimaliste, interprété (pas de JIT), avec une empreinte mémoire bien plus faible. Frida l'utilise sur des plateformes où V8 n'est pas disponible ou trop lourd (certains appareils embarqués, environnements très contraints). Son support du langage est limité à ES5 — pas de `let`/`const`, pas d'arrow functions, pas de `class`.

Dans le cadre de cette formation, nous utiliserons systématiquement V8 et la syntaxe JavaScript moderne. Tous les scripts présentés utilisent `const`, `let`, les arrow functions et `async/await`.

Pour vérifier quel moteur est actif, on peut exécuter depuis un script agent :

```javascript
send({runtime: Script.runtime});  // "V8" ou "DUK"
```

---

## Comment Interceptor fonctionne en coulisses

L'API `Interceptor` est la fonctionnalité la plus utilisée de Frida. Avant d'apprendre à l'utiliser en détail (sections 13.3 à 13.5), il est utile de comprendre ce qui se passe sous le capot quand on écrit :

```javascript
Interceptor.attach(ptr("0x401234"), {
    onEnter(args) { /* ... */ },
    onLeave(retval) { /* ... */ }
});
```

### Le trampoline

Quand vous attachez un hook à une adresse, Frida ne pose pas un breakpoint logiciel (pas d'octet `0xCC` comme GDB). À la place, il réécrit les **premières instructions de la fonction cible** pour les remplacer par un saut (`jmp`) vers un **trampoline** — un petit bloc de code généré dynamiquement par Frida.

Ce trampoline effectue la séquence suivante :

1. **Sauvegarde du contexte** — Tous les registres sont poussés sur la pile.  
2. **Appel du callback `onEnter`** — Le moteur JavaScript est invoqué avec les arguments de la fonction (lus depuis les registres `rdi`, `rsi`, `rdx`… conformément à la convention System V AMD64 vue au chapitre 3).  
3. **Exécution des instructions originales** — Les instructions qui ont été écrasées par le `jmp` sont exécutées depuis une copie (le « prologue relocalisé »).  
4. **Saut vers la suite** — Le contrôle retourne à la fonction cible, juste après les instructions déplacées. La fonction s'exécute normalement.  
5. **Interception du retour** — Pour capturer la valeur de retour, Frida remplace l'adresse de retour sur la pile par l'adresse d'un second trampoline qui appelle `onLeave` avant de retourner à l'appelant réel.

### Pourquoi c'est important pour le RE

Ce mécanisme a plusieurs conséquences pratiques :

- **Pas de `SIGTRAP`** — Contrairement aux breakpoints GDB, les hooks Frida ne génèrent aucun signal. Le processus ne sait pas qu'il est instrumenté (à condition de ne pas chercher activement).  
- **Performances** — Le surcoût d'un hook est celui d'un `jmp` supplémentaire plus l'exécution du JavaScript. Pour des fonctions appelées des millions de fois, ce coût peut devenir significatif, mais il reste négligeable pour des hooking ciblés.  
- **Relocalisation** — Frida doit « comprendre » les premières instructions de la fonction pour les déplacer correctement. Si ces instructions contiennent des références relatives à `rip` (adressage PC-relatif, fréquent en code PIE compilé avec `-fPIC`), Frida doit les ajuster. Ce processus est robuste dans la grande majorité des cas, mais peut occasionnellement échouer sur du code très atypique ou fortement obfusqué.

---

## Frida dans l'écosystème : positionnement par rapport aux autres outils

Pour situer Frida dans votre boîte à outils de reverse engineer, voici comment il se compare aux autres approches d'analyse dynamique rencontrées dans cette formation :

| Critère | `strace`/`ltrace` | GDB | Frida |  
|---|---|---|---|  
| **Granularité** | Appels système / bibliothèque | Instruction par instruction | Fonction par fonction (ou instruction via Stalker) |  
| **Intrusivité** | Faible (observation passive) | Forte (arrêt à chaque BP) | Moyenne (injection, mais pas d'arrêt) |  
| **Modification live** | Non | Oui (mais manuel, registre par registre) | Oui (scriptable, automatisé) |  
| **Langage de scripting** | Aucun | Python (API GDB) | JavaScript (agent) + Python (client) |  
| **Détectable** | Oui (`ptrace` permanent) | Oui (`ptrace` permanent) | Oui (mais `ptrace` ponctuel, agent en mémoire) |  
| **Utilisation de `ptrace`** | Permanent | Permanent | Ponctuel (injection uniquement) |

Le point clé : Frida ne remplace pas GDB. Les deux outils sont complémentaires. On utilise GDB quand on a besoin d'inspecter l'état du programme à un instant précis, instruction par instruction, avec un contrôle total. On utilise Frida quand on veut observer ou modifier le comportement de fonctions spécifiques sur la durée, sans interrompre le flux d'exécution. Dans une session de RE typique, il est courant d'alterner entre les deux.

---

## Résumé de l'architecture

```
  Votre machine (ou machine de contrôle)
  ┌────────────────────────────────┐
  │  Client Python / CLI frida     │
  │  ┌──────────────────────────┐  │
  │  │  frida.attach(pid)       │  │
  │  │  session.create_script() │  │
  │  │  script.on('message')    │  │
  │  └──────────┬───────────────┘  │
  └─────────────┼──────────────────┘
                │ canal de messages (JSON + binaire)
                │
  Machine cible (ou même machine)
  ┌─────────────┼────────────────────────────────────┐
  │  Processus  ▼  cible (PID 1234)                  │
  │                                                  │
  │  ┌────────────────────────────────────────────┐  │
  │  │  frida-agent.so                            │  │
  │  │                                            │  │
  │  │  ┌──────────────┐   ┌───────────────────┐  │  │
  │  │  │  Moteur V8   │   │  Interceptor      │  │  │
  │  │  │  (votre JS)  │◄─►│  Stalker          │  │  │
  │  │  │              │   │  Memory API       │  │  │
  │  │  └──────────────┘   └───────┬───────────┘  │  │
  │  │                             │              │  │
  │  └─────────────────────────────┼──────────────┘  │
  │                                │                 │
  │  ┌─────────────────────────────▼──────────────┐  │
  │  │  Code original (.text)                     │  │
  │  │  ┌─────────┐  ┌─────────┐  ┌────────────┐  │  │
  │  │  │ main()  │  │ check() │  │ libc.so    │  │  │
  │  │  │ ┌─jmp──►│  │ ┌─jmp──►│  │ malloc()   │  │  │
  │  │  │ │tramp. │  │ │tramp. │  │ free()     │  │  │
  │  │  └─┼───────┘  └─┼───────┘  └────────────┘  │  │
  │  │    │ hooks      │ hooks                    │  │
  │  └────┼────────────┼──────────────────────────┘  │
  │       └────────────┘                             │
  └──────────────────────────────────────────────────┘
```

L'agent vit **dans** le processus. Le client vit **à l'extérieur**. Les hooks sont des trampolines insérés **dans le code** de la cible. Le moteur JavaScript orchestre le tout depuis l'intérieur, et communique les résultats vers l'extérieur via le canal de messages.

---

## Ce qu'il faut retenir

- Frida injecte une bibliothèque partagée (`frida-agent.so`) dans le processus cible, qui embarque un moteur JavaScript complet (V8 par défaut).  
- L'injection utilise `ptrace` de manière ponctuelle sous Linux, puis le relâche — contrairement à GDB qui maintient l'attachement.  
- Le mode **attach** instrumente un processus déjà en cours d'exécution ; le mode **spawn** lance le programme et injecte l'agent avant la première instruction.  
- Les hooks sont implémentés par des **trampolines** (réécriture des premières instructions d'une fonction), pas par des breakpoints logiciels.  
- Le client (Python) et l'agent (JavaScript) communiquent via un canal bidirectionnel de messages JSON.  
- L'agent est détectable en mémoire (bibliothèques, threads, file descriptors) — un point à garder en tête face à des binaires protégés.

---

> **Prochaine section** : [13.2 — Modes d'injection : `frida`, `frida-trace`, spawn vs attach](/13-frida/02-modes-injection.md) — nous passerons de la théorie à la pratique en explorant les différentes manières de lancer Frida sur nos binaires d'entraînement.

⏭️ [Modes d'injection : `frida`, `frida-trace`, spawn vs attach](/13-frida/02-modes-injection.md)
