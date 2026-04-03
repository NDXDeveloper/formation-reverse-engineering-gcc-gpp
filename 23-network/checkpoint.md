🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 23

## Écrire un client Python capable de s'authentifier auprès du serveur sans connaître le code source

> **Contexte** : on vous fournit uniquement les binaires `server_O2_strip` et `client_O2_strip` (optimisés `-O2`, strippés, sans symboles). Vous n'avez pas accès aux sources, ni aux variantes `-O0 -g`, ni aux sections précédentes du chapitre. Le serveur écoute sur le port TCP 4444. Quelque part sur le serveur, un fichier contient un flag au format `FLAG{...}`.

---

## Objectif

Produire un script Python autonome (`checkpoint_client.py`) capable de :

1. Se connecter au serveur.  
2. Compléter le handshake initial.  
3. S'authentifier avec des credentials valides.  
4. Lister les fichiers disponibles sur le serveur.  
5. Lire et afficher le contenu de chaque fichier.  
6. Extraire le flag.  
7. Se déconnecter proprement.

Le script doit fonctionner de manière reproductible : le lancer dix fois de suite doit produire dix sessions réussies avec extraction du flag à chaque fois.

---

## Livrables attendus

### 1. Rapport de triage (texte libre, ~1 page)

Un document court résumant les résultats de votre phase d'observation initiale :

- Résultat de `file`, `strings`, `checksec` sur les deux binaires.  
- Capture `strace` annotée montrant la séquence des échanges.  
- Capture Wireshark (fichier `.pcap`) d'une session complète entre le client et le serveur originaux.  
- Vos hypothèses initiales sur le format du protocole : magic byte, champs de l'en-tête, types de messages observés, séquence d'échanges.

### 2. Spécification du protocole

Un document structuré décrivant le protocole tel que vous l'avez reconstruit :

- Format de l'en-tête (taille de chaque champ, endianness, rôle).  
- Liste des types de messages avec leurs valeurs numériques et la direction (client → serveur ou serveur → client).  
- Format du payload pour chaque type de message.  
- Machine à états : quels messages sont acceptés dans quel état, et quelles transitions chaque message provoque.  
- Mécanisme d'authentification : comment le mot de passe est protégé, quel rôle joue le challenge du handshake.

### 3. Pattern ImHex (`checkpoint_protocol.hexpat`)

Un fichier `.hexpat` qui, appliqué sur un export brut "Follow TCP Stream" d'une session capturée, décode et colorise chaque message du protocole : en-têtes, types, longueurs, payloads typés (HELLO, AUTH, CMD, QUIT et leurs réponses).

### 4. Client Python (`checkpoint_client.py`)

Le script Python final. Critères de validation :

- Utilise `pwntools` (ou les sockets standard Python — au choix).  
- Implémente le protocole de zéro (pas de replay de données capturées).  
- Gère le challenge : lit le nonce du handshake et l'utilise correctement dans l'authentification.  
- Fonctionne contre `server_O2_strip` sans aucune modification du binaire serveur.  
- Affiche le flag en sortie.

---

## Critères de réussite

| Critère | Requis | Bonus |  
|---------|--------|-------|  
| Le client se connecte et complète le handshake | ✅ | — |  
| Le client s'authentifie avec succès | ✅ | — |  
| Le client exécute au moins une commande (LIST ou READ) | ✅ | — |  
| Le client extrait et affiche le flag | ✅ | — |  
| Le client se déconnecte proprement (QUIT + réponse BYE) | ✅ | — |  
| Le script fonctionne 10 fois de suite sans échec | ✅ | — |  
| Le `.hexpat` décode correctement une capture complète | ✅ | — |  
| Le rapport de triage documente la méthodologie | ✅ | — |  
| La spécification du protocole est complète et exacte | ✅ | — |  
| Le client fonctionne avec chacun des 3 comptes utilisateur | — | ⭐ |  
| Le client détecte et affiche proprement les erreurs serveur | — | ⭐ |  
| Le client accepte host/port/credentials en arguments CLI | — | ⭐ |  
| Le `.hexpat` colorise différemment requêtes et réponses | — | ⭐ |

---

## Contraintes

- **Pas d'accès aux sources.** Le travail doit partir des binaires strippés et optimisés uniquement. Les variantes `-O0 -g` ne sont autorisées que pour vérifier vos conclusions *après* avoir reconstruit le protocole depuis les variantes strippées.  
- **Pas de patching du serveur.** Le binaire `server_O2_strip` doit être exécuté tel quel, sans modification.  
- **Pas de copier-coller depuis les sections du chapitre.** L'objectif est de reproduire la démarche de manière autonome. Les sections 23.1 à 23.5 décrivent la méthodologie — le checkpoint demande de l'appliquer soi-même.

---

## Indices progressifs

Les indices ci-dessous sont à consulter **uniquement si vous êtes bloqué**. Chaque niveau révèle un peu plus d'information. Essayez d'aller le plus loin possible avant de les consulter.

<details>
<summary><strong>Indice 1 — Par où commencer</strong></summary>

Lancez `server_O2_strip` dans un terminal, puis `client_O2_strip 127.0.0.1` dans un second terminal, avec `strace -e trace=network,read,write -x -s 512` sur les deux. Cherchez un octet récurrent au début de chaque échange — c'est le magic byte.

</details>

<details>
<summary><strong>Indice 2 — Structure de l'en-tête</strong></summary>

L'en-tête fait 4 octets. Le premier est constant (magic), le deuxième varie (type de message), les deux derniers forment un entier 16 bits en big-endian (longueur du payload qui suit).

</details>

<details>
<summary><strong>Indice 3 — Requêtes vs réponses</strong></summary>

Comparez les types des messages envoyés par le client (`0x01`, `0x02`, `0x03`, `0x04`) avec ceux envoyés par le serveur (`0x81`, `0x82`, `0x83`, `0x84`). Quel bit les distingue ?

</details>

<details>
<summary><strong>Indice 4 — Le handshake</strong></summary>

Le client envoie une chaîne fixe de 5 caractères ASCII reconnaissable (visible avec `strings` sur la capture). Le serveur répond avec une chaîne de 7 caractères ASCII suivie de 8 octets qui changent à chaque connexion.

</details>

<details>
<summary><strong>Indice 5 — L'authentification</strong></summary>

Le payload d'authentification utilise des chaînes préfixées par leur longueur (1 octet de longueur + N octets de données). Il y en a deux : le nom d'utilisateur et le mot de passe. Le nom d'utilisateur est en clair. Le mot de passe ne l'est pas — comparez-le entre deux sessions pour voir ce qui change.

</details>

<details>
<summary><strong>Indice 6 — Protection du mot de passe</strong></summary>

Le mot de passe est transformé par une opération réversible avec les 8 octets variables du handshake. Cherchez dans Ghidra une boucle qui itère sur le mot de passe et le combine avec ces octets. L'opération est un XOR cyclique.

</details>

<details>
<summary><strong>Indice 7 — Trouver les credentials</strong></summary>

Lancez `strings` sur le binaire serveur et cherchez des chaînes qui ressemblent à des identifiants ou mots de passe. Il y en a plusieurs paires. Alternativement, posez un breakpoint sur `memcmp` dans GDB et observez les arguments lors d'une tentative d'authentification.

</details>

---

## Vérification

Pour vérifier que votre client fonctionne, lancez le serveur et votre script :

```bash
# Terminal 1
$ ./build/server_O2_strip

# Terminal 2
$ python3 checkpoint_client.py 127.0.0.1 -p 4444
```

La sortie attendue doit contenir au minimum :

- Une confirmation de handshake réussi.  
- Une confirmation d'authentification réussie.  
- Le contenu des fichiers du serveur.  
- Une ligne contenant `FLAG{...}`.  
- Une déconnexion propre.

Pour une vérification approfondie, capturez la session Wireshark de votre client Python et comparez-la avec une session du client C original. Les en-têtes doivent être structurellement identiques (même magic, mêmes types, mêmes longueurs). Seuls les payloads dépendant du challenge (AUTH) différeront d'une session à l'autre.

---

## Compétences validées par ce checkpoint

Ce checkpoint mobilise et valide les compétences suivantes, acquises tout au long du chapitre :

| Section | Compétence |  
|---------|------------|  
| 23.1 | Triage binaire, capture réseau avec `strace` et Wireshark, formulation d'hypothèses sur un protocole inconnu |  
| 23.2 | Désassemblage ciblé dans Ghidra, reconstruction d'un parseur de paquets, identification de la machine à états et du mécanisme d'authentification |  
| 23.3 | Écriture d'un pattern `.hexpat` pour décoder et visualiser les trames du protocole dans ImHex |  
| 23.4 | Compréhension du replay attack, identification des protections anti-replay, extraction de credentials depuis une capture |  
| 23.5 | Implémentation d'un client réseau autonome en Python avec `pwntools`, structuré en couches transport / opérations / scénarios |

⏭️ [Chapitre 24 — Reverse d'un binaire avec chiffrement](/24-crypto/README.md)
