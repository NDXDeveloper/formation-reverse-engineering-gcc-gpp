🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 28 — Analyse d'un dropper ELF avec communication réseau

> ⚠️ **Rappel de sécurité** — Le binaire analysé dans ce chapitre (`dropper_sample`) est un sample **créé par nous**, à des fins strictement pédagogiques. Il ne doit **jamais** être exécuté en dehors de la VM sandboxée mise en place au [Chapitre 26](/26-lab-securise/README.md). Aucun malware réel n'est distribué dans cette formation.

---

## Objectifs du chapitre

Un **dropper** est un programme dont la fonction principale est de déposer (*drop*) ou télécharger une charge utile (*payload*) sur le système cible, puis de l'exécuter. À la différence d'un ransomware qui porte sa logique destructrice en lui-même, le dropper n'est qu'un **vecteur de livraison** : il communique avec un serveur de commande et contrôle (C2) pour recevoir des instructions, télécharger des composants supplémentaires, et piloter les étapes suivantes de l'infection.

Ce chapitre vous place dans la peau d'un analyste qui reçoit un binaire ELF suspect présentant une activité réseau anormale. Votre mission est de comprendre **intégralement** son fonctionnement, depuis les appels système réseau jusqu'au protocole C2 qu'il implémente, puis de démontrer votre compréhension en écrivant un faux serveur C2 capable de le contrôler.

À l'issue de ce chapitre, vous serez capable de :

- **Identifier et tracer les appels réseau** d'un binaire suspect à l'aide de `strace` et Wireshark, en corrélant les syscalls observés avec les trames capturées sur le réseau.  
- **Hooker les fonctions socket** (`connect`, `send`, `recv`, `socket`, `getaddrinfo`…) avec Frida pour intercepter les données échangées en temps réel, sans modifier le binaire.  
- **Reconstruire un protocole C2 custom** en identifiant le handshake initial, le format des commandes, l'encodage utilisé et la machine à états du client.  
- **Écrire un faux serveur C2** en Python qui simule le véritable serveur, permettant d'observer le comportement complet du dropper dans un environnement contrôlé.

---

## Contexte : qu'est-ce qu'un dropper ?

Dans la chaîne d'attaque (*kill chain*), le dropper intervient généralement après la phase d'exploitation initiale. Son rôle est minimaliste par conception : il doit être **petit**, **discret** et **suffisamment générique** pour ne pas déclencher les heuristiques des antivirus. Une fois exécuté, il contacte un serveur C2, s'identifie, attend des ordres, et exécute les commandes reçues — typiquement télécharger et exécuter un second binaire plus sophistiqué.

Notre sample pédagogique (`binaries/ch28-dropper/dropper_sample.c`) reproduit ce schéma de manière simplifiée :

1. **Connexion** — Le dropper ouvre une socket TCP vers une adresse et un port codés en dur.  
2. **Handshake** — Il envoie un message d'identification contenant des métadonnées sur la machine (hostname, PID, un identifiant de version).  
3. **Boucle de commandes** — Il entre dans une boucle où il attend des commandes du serveur, les interprète et renvoie un résultat.  
4. **Payload** — Sur réception d'une commande spécifique, il écrit des données dans un fichier sur `/tmp/` et tente de l'exécuter.

Le protocole utilisé est **custom** (non-HTTP, non-TLS) : les messages sont structurés en champs binaires avec un *magic byte*, un octet de type de commande, une longueur sur deux octets (little-endian) et un corps de taille variable. Ce type de protocole maison est fréquent dans les malwares réels, car il rend la détection par signatures réseau plus difficile.

---

## Pourquoi ce chapitre est important

L'analyse de communications réseau est l'un des piliers de l'analyse de malware. Un binaire malveillant peut être parfaitement inoffensif tant qu'il ne reçoit pas d'instructions de son C2. Comprendre le protocole réseau, c'est comprendre **ce que l'attaquant peut ordonner au malware de faire** — et donc évaluer l'étendue réelle de la menace.

Ce chapitre mobilise et combine des compétences acquises tout au long de la formation :

| Compétence | Chapitres de référence |  
|---|---|  
| Triage rapide et analyse statique | [Ch. 5](/05-outils-inspection-base/README.md), [Ch. 7](/07-objdump-binutils/README.md), [Ch. 8](/08-ghidra/README.md) |  
| Analyse hexadécimale et patterns `.hexpat` | [Ch. 6](/06-imhex/README.md) |  
| Débogage avec GDB | [Ch. 11](/11-gdb/README.md), [Ch. 12](/12-gdb-extensions/README.md) |  
| Instrumentation dynamique avec Frida | [Ch. 13](/13-frida/README.md) |  
| RE d'un protocole réseau | [Ch. 23](/23-network/README.md) |  
| Lab d'analyse sécurisé | [Ch. 26](/26-lab-securise/README.md) |

Si le [Chapitre 23](/23-network/README.md) vous a appris à reverse-engineerer un protocole réseau **légitime** (client/serveur documenté), ce chapitre franchit une étape supplémentaire : le protocole est **volontairement opaque**, le binaire est potentiellement hostile, et l'objectif final n'est plus seulement de comprendre mais de **prendre le contrôle** du malware en simulant son infrastructure.

---

## Le binaire du chapitre

Le sample est compilé depuis `binaries/ch28-dropper/dropper_sample.c` via le `Makefile` dédié. Plusieurs variantes sont produites :

| Binaire | Optimisation | Symboles | Usage |  
|---|---|---|---|  
| `dropper_O0` | `-O0` | Oui (`-g`) | Apprentissage — code lisible, symboles DWARF disponibles |  
| `dropper_O2` | `-O2` | Oui (`-g`) | Intermédiaire — observer l'impact des optimisations |  
| `dropper_O2_strip` | `-O2` | Non (`strip`) | Réaliste — conditions proches d'un vrai sample |

> 💡 Commencez **toujours** par la variante `_O0` pour comprendre la logique, puis passez à `_O2_strip` pour valider que votre analyse tient sans les symboles.

---

## Méthodologie d'analyse

L'approche recommandée pour ce chapitre suit une progression en quatre temps, chaque section correspondant à une sous-partie :

```
 ┌──────────────────────────────────────────────────────────┐
 │  28.1  Identifier les appels réseau (strace + Wireshark) │
 │        → Quelles adresses ? Quels ports ? Quels syscalls ?
 └──────────────────┬───────────────────────────────────────┘
                    ▼
 ┌──────────────────────────────────────────────────────────┐
 │  28.2  Hooker les sockets avec Frida                     │
 │        → Intercepter les données en clair, buffer par    │
 │          buffer, sans toucher au binaire                 │
 └──────────────────┬───────────────────────────────────────┘
                    ▼
 ┌──────────────────────────────────────────────────────────┐
 │  28.3  RE du protocole C2 custom                         │
 │        → Reconstruire le format des messages, la machine │
 │          à états, les commandes supportées               │
 └──────────────────┬───────────────────────────────────────┘
                    ▼
 ┌──────────────────────────────────────────────────────────┐
 │  28.4  Simuler un serveur C2                             │
 │        → Écrire un faux C2 Python, observer le           │
 │          comportement complet du dropper                 │
 └──────────────────────────────────────────────────────────┘
```

Cette progression reproduit le workflow réel d'un analyste malware : on commence par une observation passive (que fait ce binaire sur le réseau ?), puis on instrumente de plus en plus activement jusqu'à pouvoir piloter le malware soi-même.

---

## Prérequis

Avant d'aborder ce chapitre, assurez-vous que :

- Votre **lab d'analyse** est opérationnel (VM isolée, réseau en host-only ou bridge dédié, snapshots en place) — cf. [Chapitre 26](/26-lab-securise/README.md).  
- Vous êtes à l'aise avec **`strace`** pour tracer les appels système — cf. [Section 5.5](/05-outils-inspection-base/05-strace-ltrace.md).  
- Vous maîtrisez les **bases de Frida** (injection, `Interceptor.attach`, lecture de buffers) — cf. [Chapitre 13](/13-frida/README.md).  
- Vous avez déjà analysé un **protocole réseau custom** au [Chapitre 23](/23-network/README.md).  
- **Wireshark** (ou `tcpdump`) est installé dans votre environnement — cf. [Section 4.2](/04-environnement-travail/02-installation-outils.md).

> ⚠️ **Ne lancez jamais le dropper avec une connexion au réseau réel.** Même si le sample est pédagogique et que le serveur C2 n'existe pas, il est essentiel de maintenir des pratiques d'hygiène rigoureuses. Travaillez exclusivement dans votre réseau isolé.

---

## Plan du chapitre

- **28.1** — [Identifier les appels réseau avec `strace` + Wireshark](/28-dropper/01-appels-reseau-strace-wireshark.md)  
- **28.2** — [Hooker les sockets avec Frida (`connect`, `send`, `recv`)](/28-dropper/02-hooker-sockets-frida.md)  
- **28.3** — [RE du protocole C2 custom (commandes, encodage, handshake)](/28-dropper/03-re-protocole-c2.md)  
- **28.4** — [Simuler un serveur C2 pour observer le comportement complet](/28-dropper/04-simuler-serveur-c2.md)  
- **🎯 Checkpoint** — [Écrire un faux serveur C2 qui contrôle le dropper](/28-dropper/checkpoint.md)

⏭️ [Identifier les appels réseau avec `strace` + Wireshark](/28-dropper/01-appels-reseau-strace-wireshark.md)
