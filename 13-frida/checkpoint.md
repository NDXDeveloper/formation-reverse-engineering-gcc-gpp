🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 13

## Écrire un script Frida qui logue tous les appels à `send()` avec leurs buffers

> 📦 **Binaire cible** : `binaries/ch13-network/server_O0` et `binaries/ch13-network/client_O0`  
> 🧰 **Outils requis** : `frida`, Python 3 + module `frida`  
> 📖 **Sections mobilisées** : 13.1 à 13.7 (ensemble du chapitre)  
> ⏱️ **Durée estimée** : 45 minutes à 1 heure  
> 📄 **Solution** : `solutions/ch13-checkpoint-solution.js`

---

## Contexte

Le binaire `client_O0` est un client réseau qui se connecte au `server_O0`, s'authentifie via un protocole custom, puis échange une série de messages. Vous ne disposez ni du code source, ni de la documentation du protocole. Votre mission est d'écrire un script Frida complet qui capture et logue l'intégralité des données envoyées par le client via la fonction `send()`, afin de pouvoir reconstruire le protocole dans un chapitre ultérieur (chapitre 23).

Ce checkpoint valide votre capacité à combiner les techniques fondamentales du chapitre :

- Choisir le bon mode d'injection (section 13.2).  
- Poser un hook sur une fonction de bibliothèque (section 13.3).  
- Lire correctement des arguments de types différents — entier, pointeur vers buffer, taille (section 13.4).  
- Transmettre des données binaires brutes au client Python (section 13.4).  
- Filtrer le bruit pour ne conserver que les appels pertinents (section 13.3).  
- Enrichir la trace avec des métadonnées contextuelles (section 13.6).

---

## Cahier des charges

Le script Frida à produire doit satisfaire les exigences suivantes.

### Exigences fonctionnelles

**EF-1 — Interception de `send()`.** Chaque appel à la fonction `send()` de la libc effectué par le client doit être intercepté. Rappelons la signature :

```c
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
```

**EF-2 — Logging structuré.** Pour chaque appel intercepté, le script doit envoyer au client Python un message JSON contenant au minimum :

- Le numéro du file descriptor (`sockfd`).  
- La taille demandée (`len`).  
- Le nombre d'octets réellement envoyés (valeur de retour).  
- Un horodatage (millisecondes depuis le début du traçage).  
- Un numéro de séquence incrémental (1er appel = 1, 2e = 2, etc.).

**EF-3 — Transmission du buffer brut.** Le contenu binaire du buffer doit être transmis au client Python via le second paramètre de `send()` côté JavaScript (le canal binaire), et non encodé dans le JSON. Le client Python doit pouvoir exploiter ces octets directement.

**EF-4 — Affichage hexadécimal.** Le script doit afficher dans la console Frida un `hexdump` du buffer pour chaque appel, limité aux 128 premiers octets si le buffer est plus grand.

**EF-5 — Interception de `connect()`.** En complément, le script doit hooker `connect()` pour logger l'adresse IP et le port de destination (parsing de `sockaddr_in`), afin de contextualiser les `send()` ultérieurs.

### Exigences techniques

**ET-1 — Mode spawn.** Le script doit lancer le client en mode spawn (`frida -f` ou `frida.spawn`) pour capturer les `send()` dès le premier octet, y compris ceux du handshake d'authentification.

**ET-2 — Robustesse.** Les callbacks `onEnter` et `onLeave` doivent être protégés par des blocs `try/catch`. Un buffer invalide ou un appel inattendu ne doit pas faire crasher le script.

**ET-3 — Filtrage optionnel.** Le script doit pouvoir filtrer les `send()` par file descriptor (ne logger qu'un socket spécifique), activable via une variable de configuration en tête de script.

**ET-4 — Export des données.** Le client Python doit sauvegarder les buffers capturés dans un fichier `capture.bin` (octets bruts concaténés) et un fichier `capture.json` (métadonnées de chaque appel), exploitables pour l'analyse ultérieure.

---

## Structure attendue

Le livrable est composé de deux fichiers :

```
checkpoint-ch13/
├── agent.js          ← Code JavaScript de l'agent Frida
└── capture.py        ← Script Python client (orchestration + export)
```

### Squelette de `agent.js`

Le script JavaScript de l'agent devra contenir au minimum :

- Une section d'initialisation (horodatage de début, compteur de séquence, configuration de filtrage).  
- Un hook sur `connect()` avec parsing de `sockaddr_in` (famille AF_INET, extraction IP et port).  
- Un hook sur `send()` avec lecture du file descriptor, du buffer, de la taille, et envoi structuré vers Python.  
- Un affichage `hexdump` dans la console pour chaque appel.

### Squelette de `capture.py`

Le script Python devra :

- Spawner le processus `client_O0` via `frida.spawn()`.  
- Charger l'agent, enregistrer le callback `on_message`.  
- Recevoir les messages JSON et les buffers binaires.  
- Afficher un résumé formaté dans le terminal.  
- Sauvegarder les résultats dans `capture.bin` et `capture.json`.  
- Gérer proprement la fin du processus (`detached`, Ctrl+C).

---

## Indices et rappels

Les points suivants peuvent guider votre réflexion sans donner la solution :

**Lecture du buffer dans le bon callback.** Le buffer passé à `send()` contient déjà les données à envoyer au moment de l'appel. On peut donc le lire dès `onEnter`. Cependant, le nombre d'octets réellement envoyés n'est connu qu'au retour de la fonction (`retval` dans `onLeave`). Il faut coordonner les deux callbacks via `this`.

**Taille du `readByteArray`.** Lisez `Math.min(len, retval)` octets — pas plus que ce que le programme demande d'envoyer, et pas plus que ce qui a réellement été transmis. Si `send()` retourne `-1` (erreur), ne tentez pas de lire le buffer.

**Parsing de `sockaddr_in`.** Revoyez la section 13.4. La famille d'adresse est sur 2 octets, le port sur 2 octets en big-endian, l'adresse IP sur 4 octets. L'offset de chaque champ dans la structure est fixe.

**Transmission binaire.** Le second argument de `send()` côté JavaScript accepte un objet retourné par `.readByteArray()`. Côté Python, ce buffer arrive dans le paramètre `data` du callback `on_message` sous forme de `bytes`.

**`sendall` et `write`.** Selon l'implémentation du client, les données peuvent être envoyées via `send()`, `sendto()`, `sendmsg()`, ou même `write()` sur un socket. Pour une couverture complète, envisagez de hooker aussi `write()` en filtrant par file descriptor (les sockets et les fichiers ordinaires partagent l'espace des file descriptors).

---

## Critères de validation

Votre checkpoint est réussi si :

| # | Critère | Vérifié ? |  
|---|---|---|  
| 1 | Le script capture le `connect()` initial et affiche l'IP et le port de destination | ☐ |  
| 2 | Chaque appel à `send()` est loggué avec le fd, la taille et le nombre d'octets envoyés | ☐ |  
| 3 | Le `hexdump` de chaque buffer est affiché dans la console Frida | ☐ |  
| 4 | Les buffers binaires bruts sont transmis au client Python via le canal binaire | ☐ |  
| 5 | Le fichier `capture.json` contient les métadonnées de chaque appel (fd, taille, séquence, timestamp) | ☐ |  
| 6 | Le fichier `capture.bin` contient la concaténation des buffers bruts et est exploitable par un parser | ☐ |  
| 7 | Le script ne crashe pas en cas d'erreur `send()` (retour `-1`) ou de buffer invalide | ☐ |  
| 8 | Le filtrage par file descriptor fonctionne quand il est activé | ☐ |

---

## Pour aller plus loin

Si vous terminez avant le temps imparti, voici des extensions qui renforcent la maîtrise :

- **Intercepter aussi `recv()`** et produire une trace chronologique bidirectionnelle (envois et réceptions entrelacés), avec un marqueur de direction (`>>>` pour `send`, `<<<` pour `recv`).  
- **Ajouter une backtrace** à chaque `send()` pour identifier quelle fonction du binaire est à l'origine de chaque envoi.  
- **Calculer un hash MD5** de chaque buffer côté Python et le stocker dans le JSON, pour détecter les retransmissions ou les messages dupliqués.  
- **Produire un fichier PCAP** reconstruit à partir des captures, importable dans Wireshark pour une analyse visuelle du protocole.

---

## Compétences validées

La réussite de ce checkpoint confirme que vous maîtrisez :

| Compétence | Sections |  
|---|---|  
| Choisir entre attach et spawn selon le besoin | 13.2 |  
| Hooker une fonction de bibliothèque par nom | 13.3 |  
| Lire des arguments de types hétérogènes (int, pointeur, taille) | 13.3, 13.4 |  
| Parser une structure C en mémoire (`sockaddr_in`) | 13.4 |  
| Transmettre des données binaires de l'agent JS au client Python | 13.4 |  
| Coordonner `onEnter` et `onLeave` via `this` | 13.3 |  
| Protéger les hooks contre les erreurs runtime | 13.3 |  
| Orchestrer une session Frida complète en Python (spawn → attach → load → resume) | 13.2 |

Vous êtes prêt à aborder le Chapitre 14 — Analyse avec Valgrind et sanitizers, et à appliquer Frida sur les cas pratiques de la Partie V, où les binaires réseau (chapitre 23), crypto (chapitre 24) et malware (chapitres 27–28) mobiliseront intensivement ces techniques d'interception.

⏭️ [Chapitre 14 — Analyse avec Valgrind et sanitizers](/14-valgrind-sanitizers/README.md)
