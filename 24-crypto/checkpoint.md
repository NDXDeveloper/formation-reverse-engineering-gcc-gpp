🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 24

## Déchiffrer le fichier `secret.enc` fourni en extrayant la clé du binaire

> **Binaire cible** : `binaries/ch24-crypto/crypto_O2_strip` (optimisé, strippé)  
> **Fichier à déchiffrer** : `binaries/ch24-crypto/secret.enc`  
> **Fichier de référence** : `binaries/ch24-crypto/secret.txt` (pour validation finale uniquement)  
> **Corrigé** : `solutions/ch24-checkpoint-decrypt.py`

---

## Contexte

Vous disposez du binaire `crypto_O2_strip` et du fichier `secret.enc` qu'il a produit. Le binaire est optimisé (`-O2`) et strippé — les symboles locaux ont été supprimés. Votre objectif est de produire un script Python capable de déchiffrer `secret.enc` et de restituer le contenu original.

Ce checkpoint valide l'ensemble des compétences du chapitre 24 : identification de l'algorithme, identification de la bibliothèque, extraction des secrets, compréhension du format de fichier, et reproduction du schéma en Python.

---

## Critères de validation

Le checkpoint est considéré comme réussi lorsque les **quatre conditions** suivantes sont remplies.

### 1. Identification documentée de l'algorithme et de la bibliothèque

Vous devez être en mesure de répondre, preuves à l'appui, aux questions suivantes :

- Quel algorithme de chiffrement est utilisé, et dans quel mode d'opération ?  
- Quel algorithme de hachage intervient dans la dérivation de clé ?  
- Les routines crypto proviennent-elles d'une bibliothèque externe ou d'une implémentation custom ?  
- Comment avez-vous obtenu ces réponses ? (quels outils, quelles commandes, quels résultats)

**Méthodes attendues** : recherche de constantes magiques (section 24.1), inspection des symboles dynamiques et/ou des chaînes internes (section 24.2).

### 2. Extraction de la clé et de l'IV

Vous devez avoir capturé :

- La clé de chiffrement (32 octets pour AES-256) — soit directement depuis la mémoire, soit en reproduisant la dérivation.  
- L'IV (16 octets) — soit depuis la mémoire, soit depuis le fichier `.enc` lui-même.  
- Bonus : la passphrase source et la logique de dérivation complète (passphrase → hash → transformation → clé finale).

**Méthodes attendues** : breakpoints GDB et/ou hooks Frida sur les fonctions crypto (section 24.3).

### 3. Compréhension du format de fichier

Vous devez produire une carte du format de `secret.enc` indiquant :

- L'offset, la taille et la signification de chaque champ du header.  
- L'offset exact de début du ciphertext.  
- Le schéma de padding utilisé.

**Méthodes attendues** : inspection dans ImHex, analyse d'entropie, idéalement un pattern `.hexpat` fonctionnel (section 24.4).

### 4. Script de déchiffrement fonctionnel

Vous devez produire un script Python (`decrypt.py`) qui :

- Prend en argument le chemin vers un fichier `.enc` au format CRYPT24.  
- Parse le header pour extraire l'IV, la taille originale, et le ciphertext.  
- Dérive (ou utilise) la clé AES-256.  
- Déchiffre en AES-256-CBC avec retrait du padding.  
- Écrit le résultat déchiffré dans un fichier de sortie.

**Validation finale** :

```bash
$ python3 decrypt.py secret.enc decrypted.txt
$ diff secret.txt decrypted.txt
# (aucune sortie = fichiers identiques = succès)
```

---

## Niveaux de difficulté suggérés

Ce checkpoint peut être abordé à trois niveaux, selon votre degré de maîtrise et le temps que vous souhaitez y investir.

### Niveau 1 — Standard

Travailler sur `crypto_O0` (non optimisé, non strippé, lié dynamiquement). Tous les symboles sont visibles, les breakpoints se posent par nom de fonction, et `ldd` + `nm` donnent l'identification immédiate.

### Niveau 2 — Intermédiaire

Travailler sur `crypto_O2_strip` (optimisé, strippé, lié dynamiquement). Les symboles locaux ont disparu, mais les imports dynamiques (`EVP_*`) restent visibles. L'optimisation `-O2` réarrange le code et inline certaines fonctions — le décompilateur Ghidra est moins lisible qu'en `-O0`.

### Niveau 3 — Avancé

Travailler sur `crypto_static` après l'avoir strippé manuellement (`strip crypto_static`). Aucun symbole, aucune bibliothèque dynamique visible. Il faut identifier l'algorithme par les constantes, retrouver les fonctions par XREF dans Ghidra, et poser les breakpoints par adresse. C'est le scénario le plus proche d'un cas réel (malware, firmware).

---

## Livrables attendus

| Livrable | Format | Description |  
|---|---|---|  
| `decrypt.py` | Script Python | Déchiffre un fichier `.enc` au format CRYPT24 |  
| `decrypted.txt` | Fichier texte | Résultat du déchiffrement de `secret.enc` |  
| `crypt24.hexpat` | Pattern ImHex | Cartographie du format CRYPT24 (optionnel mais recommandé) |  
| Notes d'analyse | Texte libre | Commandes utilisées, observations, captures d'écran (optionnel) |

---

## Checklist rapide

Cochez chaque étape au fur et à mesure :

- [ ] Triage initial du binaire (`file`, `checksec`, `strings`, `readelf`)  
- [ ] Algorithme identifié (AES-256-CBC + SHA-256)  
- [ ] Bibliothèque identifiée (OpenSSL)  
- [ ] Clé AES-256 extraite (32 octets)  
- [ ] IV extrait (16 octets, depuis la mémoire ou le fichier `.enc`)  
- [ ] Passphrase retrouvée  
- [ ] Logique de dérivation reconstruite (SHA-256 → XOR masque)  
- [ ] Format de `secret.enc` cartographié (offsets, champs, tailles)  
- [ ] Script `decrypt.py` écrit et fonctionnel  
- [ ] `diff secret.txt decrypted.txt` ne retourne aucune différence  
- [ ] (Bonus) Pattern `.hexpat` fonctionnel  
- [ ] (Bonus) Script capable de *chiffrer* un fichier au format CRYPT24

---

## Indices progressifs

Si vous êtes bloqué, ces indices sont classés du plus vague au plus précis. Essayez de n'en lire qu'un à la fois avant de retenter.

> **Indice 1** — Le binaire est lié dynamiquement. Même strippé, certains symboles restent accessibles. Quelle commande `nm` permet de les voir ?

> **Indice 2** — La passphrase n'apparaît pas dans `strings` car elle est construite par morceaux en mémoire. Mais elle existe en clair à un moment précis. Quelle fonction la construit ?

> **Indice 3** — Le masque XOR de 32 octets est une variable globale dans `.rodata`. Ses premiers octets sont `0xDE 0xAD 0xBE 0xEF`. Cherchez cette séquence dans Ghidra.

> **Indice 4** — La convention d'appel System V place le 4ᵉ argument dans `rcx` et le 5ᵉ dans `r8`. Pour `EVP_EncryptInit_ex`, le 4ᵉ argument est la clé et le 5ᵉ est l'IV.

> **Indice 5** — Le ciphertext commence à l'offset `0x20` dans le fichier `.enc`. Tout ce qui précède est du header en clair.

---

## Ce que ce checkpoint démontre

En complétant ce checkpoint, vous avez prouvé votre capacité à mener une analyse RE crypto de bout en bout sur un binaire compilé avec GCC. Vous avez combiné analyse statique (constantes, signatures, structures) et analyse dynamique (breakpoints, hooks, inspection mémoire) pour extraire des informations qu'aucune des deux approches n'aurait suffi à obtenir seule. Et vous avez traduit cette compréhension en un outil fonctionnel — le script de déchiffrement — qui est la validation concrète et indiscutable du travail accompli.

Ces compétences sont directement transférables aux scénarios du monde réel : analyse de ransomware (chapitre 27), audit de protocoles chiffrés (chapitre 23), et plus généralement tout contexte où un binaire manipule des données protégées.

---


⏭️ [Chapitre 25 — Reverse d'un format de fichier custom](/25-fileformat/README.md)
