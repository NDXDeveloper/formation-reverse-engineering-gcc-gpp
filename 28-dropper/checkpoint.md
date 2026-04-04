🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 28

## Écrire un faux serveur C2 qui contrôle le dropper

> **Binaire cible** : `dropper_O2_strip` (optimisé, sans symboles)  
> **Temps estimé** : 2 à 3 heures  
> **Livrables** : un script Python `fake_c2.py`, une capture `session.pcap`, un rapport `rapport_ch28.md`

---

## Énoncé

Vous êtes analyste dans une équipe de réponse à incident. Un binaire ELF suspect nommé `dropper_O2_strip` a été extrait d'une machine compromise. L'équipe réseau a observé des connexions TCP sortantes vers `127.0.0.1:4444` (l'adresse a été modifiée par le SOC pour pointer vers localhost). Aucun code source n'est disponible. Aucune documentation du protocole n'existe.

Votre mission est de **prendre le contrôle du dropper** en écrivant un faux serveur C2 capable de :

1. **Accepter la connexion** du dropper et compléter le handshake.  
2. **Envoyer chacune des 5 commandes** du protocole (PING, EXEC, DROP, SLEEP, EXIT) et recevoir les réponses correctement.  
3. **Décoder toutes les données** échangées, y compris celles protégées par l'encodage XOR.  
4. **Capturer la session réseau** complète dans un fichier pcap.  
5. **Documenter vos findings** dans un rapport d'analyse structuré.

> ⚠️ **Condition de difficulté** — Vous devez travailler sur la variante **strippée** (`dropper_O2_strip`). Les symboles de débogage ne sont pas disponibles. Les variantes `_O0` et `_O2` peuvent être utilisées comme aide à la compréhension, mais le livrable final doit fonctionner avec le binaire strippé.

---

## Critères de validation

### 1. Le faux C2 fonctionne (`fake_c2.py`)

| Critère | Requis | Bonus |  
|---|---|---|  
| Le handshake aboutit (le dropper ne se déconnecte pas) | ✅ | — |  
| `CMD_PING` → le dropper répond `MSG_PONG` | ✅ | — |  
| `CMD_EXEC` → la commande est exécutée, le résultat décodé est lisible | ✅ | — |  
| `CMD_DROP` → un fichier est créé dans `/tmp/`, exécuté, ACK reçu | ✅ | — |  
| `CMD_SLEEP` → le dropper modifie son intervalle de beacon | ✅ | — |  
| `CMD_EXIT` → le dropper se termine proprement | ✅ | — |  
| Les beacons périodiques sont correctement reçus et parsés | ✅ | — |  
| Le C2 gère les erreurs de connexion sans crasher | — | ✅ |  
| Le C2 propose un mode interactif (menu) | — | ✅ |  
| Le C2 propose un mode script (séquence automatisée) | — | ✅ |  
| Le C2 affiche un hexdump lisible des messages bruts et décodés | — | ✅ |

### 2. La capture réseau est complète (`session.pcap`)

| Critère | Requis |  
|---|---|  
| La capture contient le handshake TCP (SYN/SYN-ACK/ACK) | ✅ |  
| Le handshake applicatif (HANDSHAKE → ACK) est visible | ✅ |  
| Au moins un échange de chaque type de commande est présent | ✅ |  
| Au moins un beacon est visible dans la capture | ✅ |  
| La fermeture propre de la connexion (FIN/ACK) est capturée | ✅ |

### 3. Le rapport d'analyse est complet (`rapport_ch28.md`)

Le rapport doit contenir les sections suivantes :

| Section | Contenu attendu |  
|---|---|  
| **Résumé exécutif** | Description en 5 lignes du binaire, de son comportement et de la menace |  
| **IOC réseau** | Adresse IP, port, protocole de transport, magic byte, clé XOR, chaîne de version |  
| **Spécification du protocole** | Format du header, table des types de messages (commandes et réponses), encodage |  
| **Comportement par commande** | Description de chaque commande, format du body, effets observés |  
| **Diagramme de séquence** | Schéma ASCII ou Mermaid montrant le flux d'une session complète |  
| **Mécanismes de résilience** | Reconnexion automatique, nombre de tentatives, intervalle |  
| **Recommandations de détection** | Au moins une règle de détection réseau (Snort, Suricata ou signature custom) |

---

## Indications méthodologiques

### Par où commencer

L'approche recommandée est celle suivie tout au long du chapitre. Appliquez-la **de zéro** sur `dropper_O2_strip`, comme si vous n'aviez pas lu les sections précédentes :

1. **Triage** — `file`, `strings`, `checksec`, `ldd`. Notez chaque indice.  
2. **Observation passive** — `strace` + `tcpdump` avec un listener `nc`. Identifiez l'adresse, le port, le magic byte, la taille du premier message.  
3. **Instrumentation** — Hooks Frida sur `connect`, `send`, `recv`. Identifiez les types de messages, l'encodage XOR, le format du body.  
4. **Analyse statique** — Ouvrez le binaire dans Ghidra pour confirmer les constantes (`0xDE`, `0x5A`), identifier le `switch/case` du dispatcher, retrouver la structure du handshake.  
5. **Construction du C2** — Implémentez le protocole couche par couche : transport → messages → commandes → interface.  
6. **Validation** — Lancez le C2, connectez le dropper, exercez chaque commande, capturez le tout.

### Points d'attention sur le binaire strippé

Sans les symboles, les noms de fonctions ne sont pas disponibles dans Ghidra (vous verrez `FUN_001XXXXX`). Voici les repères pour retrouver les fonctions clés :

- **`main`** — Point d'entrée référencé par `__libc_start_main` dans la section `.init`. Contient la boucle de reconnexion (`while` + `connect` + `sleep`).  
- **La fonction de handshake** — Cherchez les appels à `gethostname` et `getpid` suivis d'un `send`. La chaîne `"DRP-1.0"` dans `.rodata` est une ancre fiable.  
- **Le dispatcher** — Cherchez un pattern `switch`/`case` avec les constantes `0x01` à `0x05`. En `-O2`, GCC génère souvent une table de sauts (*jump table*) plutôt qu'une cascade de `cmp`/`jz`.  
- **La fonction XOR** — Cherchez une boucle contenant `xor` avec un opérande immédiat `0x5A` (ou un registre chargé avec `0x5A`). En `-O2`, la boucle peut être déroulée ou vectorisée.  
- **`send_message` / `recv_message`** — Cherchez les fonctions qui écrivent ou lisent le magic byte `0xDE` (`0xDE` = 222 en décimal). Ces fonctions appellent `send`/`recv` de la `libc`.

### Pièges fréquents

- **Endianness du champ `length`** — Le champ longueur du header est en **little-endian** natif (pas de `htons`). Sur x86-64, `struct.pack("<H", ...)` est correct. Utiliser `"!H"` (big-endian) cassera le protocole.  
- **XOR sélectif** — Le XOR ne s'applique **pas** à tous les messages. Seuls `CMD_EXEC`, `CMD_DROP` et `MSG_RESULT` ont leur body encodé. `CMD_SLEEP`, `MSG_HANDSHAKE`, `MSG_BEACON`, `MSG_ACK`, `MSG_PONG` et `MSG_ERROR` sont en clair.  
- **Le handshake attend un ACK** — Si vous envoyez autre chose que `MSG_ACK` (type `0x13`) en réponse au handshake, le dropper considère la connexion comme rejetée et se déconnecte.  
- **Les beacons arrivent entre les commandes** — Le dropper envoie un beacon toutes les `BEACON_INTERVAL` secondes via `select()`. Votre C2 doit les consommer pour ne pas confondre un beacon avec la réponse à une commande.  
- **`recv` ne retourne pas forcément tout d'un coup** — Implémentez un `recv_all` qui boucle jusqu'à avoir reçu le nombre exact d'octets demandé.

---

## Fichiers fournis

| Fichier | Emplacement | Description |  
|---|---|---|  
| `dropper_O0` | `binaries/ch28-dropper/` | Variante debug (aide à la compréhension) |  
| `dropper_O2` | `binaries/ch28-dropper/` | Variante optimisée avec symboles |  
| `dropper_O2_strip` | `binaries/ch28-dropper/` | **Variante cible** (strippée) |  
| `dropper_sample.c` | `binaries/ch28-dropper/` | Code source (⚠️ ne pas consulter avant d'avoir terminé) |  
| `Makefile` | `binaries/ch28-dropper/` | Pour recompiler les variantes |

---

## Grille d'auto-évaluation

Avant de consulter le corrigé, vérifiez chaque point :

| # | Vérification | ✅ / ❌ |  
|---|---|---|  
| 1 | Mon C2 accepte la connexion et complète le handshake sans erreur | |  
| 2 | J'ai correctement identifié le magic byte, les types et l'endianness | |  
| 3 | La commande PING produit un PONG | |  
| 4 | La commande EXEC retourne un résultat lisible (décodé du XOR) | |  
| 5 | La commande DROP crée un fichier dans `/tmp/` et l'exécute | |  
| 6 | La commande SLEEP modifie effectivement l'intervalle de beacon | |  
| 7 | La commande EXIT termine le dropper proprement | |  
| 8 | Les beacons sont reçus et parsés (cmd_count + timestamp) | |  
| 9 | Ma capture pcap contient au moins un échange de chaque type | |  
| 10 | Mon rapport contient les IOC, la spécification du protocole et un diagramme | |  
| 11 | Tout fonctionne avec `dropper_O2_strip` (pas seulement `_O0`) | |  
| 12 | Mon C2 ne crashe pas si le dropper se déconnecte inopinément | |

**Score :**
- **10–12** ✅ — Excellent. Vous maîtrisez l'analyse de protocole C2 et l'émulation de serveur.  
- **7–9** ✅ — Solide. Revoyez les points manqués, souvent liés à des cas limites.  
- **4–6** ✅ — Les bases sont là. Reprenez les sections 28.2 et 28.3 pour consolider la compréhension du protocole.  
- **< 4** ✅ — Reprenez le chapitre depuis 28.1. L'analyse séquentielle est essentielle.

---

## Corrigé

Le corrigé complet est disponible dans [`solutions/ch28-checkpoint-fake-c2.py`](/solutions/ch28-checkpoint-fake-c2.py).

> ⚠️ **Conseil** — Ne consultez le corrigé qu'après avoir fait une tentative sérieuse. La valeur pédagogique de ce checkpoint réside dans la **démarche** : reconstruire un protocole inconnu depuis un binaire strippé et prouver sa compréhension en écrivant un outil fonctionnel. Lire le corrigé sans avoir essayé, c'est apprendre la recette sans avoir jamais cuisiné.

⏭️ [Chapitre 29 — Détection de packing, unpack et reconstruction](/29-unpacking/README.md)
