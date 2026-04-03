🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 27 : Déchiffrer les fichiers et produire un rapport complet

> **Variante cible** : `ransomware_O2_strip` (optimisée, sans symboles).  
> C'est la variante qui simule un scénario réaliste. Si vous avez travaillé sur `ransomware_O0` pendant les sections précédentes pour vous familiariser avec le sample, c'est le moment de tout reprendre depuis zéro sur la variante strippée.  
>  
> 📁 Le corrigé est disponible dans `solutions/ch27-checkpoint-decryptor.py` et `solutions/ch27-checkpoint-solution.md`. Consultez-le **uniquement** après avoir terminé votre propre analyse.

---

## Critères de validation

Ce checkpoint est réussi lorsque les **quatre livrables** suivants sont produits et fonctionnels :

### Livrable 1 — Déchiffreur Python fonctionnel

Votre script doit :

- Parser correctement le header des fichiers `.locked` (magic `RWARE27\0` + taille originale `uint64_t` LE).  
- Déchiffrer le payload en AES-256-CBC avec la clé et l'IV extraits par votre analyse.  
- Retirer le padding PKCS#7 et restaurer les fichiers à leur taille originale exacte.  
- Traiter récursivement un répertoire entier.  
- Rejeter proprement les fichiers qui ne portent pas le bon magic header.

**Test de validation** :

```bash
# 1. Préparer l'environnement propre
make reset

# 2. Calculer les hashes des fichiers originaux
find /tmp/test -type f -exec sha256sum {} \; | sort > /tmp/before.txt

# 3. Exécuter le ransomware
./ransomware_O2_strip

# 4. Vérifier que les fichiers sont chiffrés
ls /tmp/test/*.locked

# 5. Exécuter VOTRE déchiffreur
python3 votre_decryptor.py /tmp/test/

# 6. Recalculer les hashes
find /tmp/test -type f ! -name "*.locked" ! -name "README_LOCKED.txt" \
    -exec sha256sum {} \; | sort > /tmp/after.txt

# 7. Comparer — aucune différence = succès
diff /tmp/before.txt /tmp/after.txt
```

Si `diff` ne produit aucune sortie, la restauration est parfaite au bit près. C'est le critère objectif de réussite de ce livrable.

### Livrable 2 — Règles YARA opérationnelles

Votre fichier `.yar` doit contenir au minimum deux règles :

- Une règle ciblant le **binaire** (sample lui-même).  
- Une règle ciblant les **fichiers `.locked`** produits.

**Test de validation** :

```bash
# La règle binaire doit matcher le sample
yara votre_fichier.yar ransomware_O2_strip
# Attendu : au moins un match

# La règle binaire ne doit PAS matcher un binaire légitime
yara votre_fichier.yar /usr/bin/openssl
# Attendu : aucun match

# La règle fichiers doit matcher les .locked
yara -r votre_fichier.yar /tmp/test/
# Attendu : un match par fichier .locked
```

### Livrable 3 — Rapport d'analyse structuré

Votre rapport doit contenir au minimum les sections suivantes :

- **Résumé exécutif** — Nature du sample, récupérabilité des données, niveau de sophistication.  
- **Identification** — Hashes (SHA-256 au minimum), type, dépendances.  
- **IOC** — Au moins 5 indicateurs de compromission classés par catégorie (file-based, behavioral).  
- **Analyse comportementale** — Description du flux de chiffrement, paramètres cryptographiques (algorithme, clé, IV), format des fichiers produits.  
- **Recommandations** — Au moins 3 recommandations actionnables.

Le rapport doit être rédigé de manière à ce qu'un analyste tiers, n'ayant pas accès à votre environnement, puisse comprendre le sample, déployer les IOC et utiliser le déchiffreur à partir de la seule lecture du document.

### Livrable 4 — Pattern ImHex du format `.locked`

Votre fichier `.hexpat` doit :

- Identifier et coloriser le magic header.  
- Extraire et afficher la taille originale en tant qu'entier 64 bits.  
- Délimiter visuellement le payload chiffré.  
- Se charger sans erreur dans ImHex sur n'importe quel fichier `.locked` produit par le sample.

---

## Grille d'auto-évaluation

Cette grille vous permet de mesurer la complétude de votre travail avant de consulter le corrigé.

### Analyse statique

| Point de vérification | ✅ / ❌ |  
|---|---|  
| J'ai identifié l'algorithme de chiffrement (AES-256-CBC) sans exécuter le binaire | |  
| J'ai localisé la clé dans le binaire (adresse dans `.rodata` + valeur hex complète) | |  
| J'ai localisé l'IV dans le binaire | |  
| J'ai reconstruit le graphe d'appels depuis `main` jusqu'aux fonctions EVP | |  
| J'ai renommé au moins 5 fonctions dans Ghidra avec des noms significatifs | |  
| J'ai cartographié le format `.locked` (offsets, types, tailles de chaque champ) | |

### Analyse dynamique

| Point de vérification | ✅ / ❌ |  
|---|---|  
| J'ai posé un breakpoint sur `EVP_EncryptInit_ex` et capturé la clé depuis un registre | |  
| J'ai posé un breakpoint sur `EVP_EncryptInit_ex` et capturé l'IV depuis un registre | |  
| J'ai vérifié que la clé et l'IV sont identiques pour chaque fichier chiffré | |  
| J'ai confirmé l'absence de communication réseau (via `strace` ou absence de symboles réseau) | |  
| J'ai utilisé au moins deux outils différents (GDB, Frida, ltrace, strace) | |

### Déchiffreur

| Point de vérification | ✅ / ❌ |  
|---|---|  
| Mon script parse le header `.locked` et valide le magic | |  
| Mon script déchiffre correctement avec AES-256-CBC | |  
| Mon script gère le padding PKCS#7 | |  
| Mon script traite récursivement un répertoire | |  
| Les fichiers restaurés sont identiques bit-à-bit aux originaux (validation par hash) | |  
| Mon script gère les erreurs proprement (fichier corrompu, mauvais magic, fichier vide) | |

### Règles YARA

| Point de vérification | ✅ / ❌ |  
|---|---|  
| Ma règle détecte le sample binaire | |  
| Ma règle ne produit pas de faux positif sur `/usr/bin/openssl` | |  
| Ma règle détecte les fichiers `.locked` | |  
| J'ai testé mes règles avec la commande `yara` | |

### Rapport

| Point de vérification | ✅ / ❌ |  
|---|---|  
| Le résumé exécutif tient en un paragraphe et répond à « les données sont-elles récupérables ? » | |  
| Les hashes SHA-256 du sample sont inclus | |  
| Au moins 5 IOC sont listés et classés | |  
| Les paramètres cryptographiques sont documentés avec leur source de confirmation | |  
| Le format `.locked` est décrit (offsets, types, tailles) | |  
| Au moins 3 recommandations actionnables sont formulées | |  
| Le rapport est compréhensible par quelqu'un qui n'a pas suivi ma démarche | |

---

## Barème indicatif

| Niveau | Critères |  
|---|---|  
| **Essentiel** | Déchiffreur fonctionnel (validation par hash réussie) + paramètres crypto documentés |  
| **Complet** | Essentiel + règles YARA testées + rapport avec IOC et recommandations |  
| **Excellent** | Complet + analyse menée intégralement sur la variante strippée + pattern ImHex + script GDB ou Frida d'extraction automatique + matrice ATT&CK dans le rapport |

---

## Erreurs fréquentes à surveiller

**Oublier l'endianness de la taille originale.** Le champ `original_size` est stocké en little-endian (`uint64_t` sur x86-64). Un `struct.unpack(">Q", ...)` (big-endian) produira une valeur absurde et le déchiffrement semblera « fonctionner » mais les fichiers seront tronqués ou rallongés de manière incorrecte. Utilisez `<Q`.

**Confondre taille de bloc en bits et en octets.** AES a un bloc de 16 octets = 128 bits. La bibliothèque `cryptography` de Python attend la taille de bloc en **bits** dans `padding.PKCS7(128)`. Écrire `PKCS7(16)` provoquera une erreur silencieuse ou un mauvais retrait de padding.

**Négliger le header de 16 octets.** Si vous passez l'intégralité du fichier `.locked` (y compris les 16 octets de header) à la routine de déchiffrement AES, le résultat sera incohérent. Le ciphertext commence à l'offset `0x10`, pas à `0x00`.

**Poser un breakpoint sur la mauvaise fonction.** Sur la variante strippée, `break main` peut ne pas fonctionner (symbole absent). En revanche, `break EVP_EncryptInit_ex` fonctionne toujours car c'est un symbole dynamique. Si GDB demande de rendre le breakpoint *pending*, répondez `y` — il sera résolu au chargement de `libcrypto.so`.

**Écrire un rapport purement descriptif.** Un rapport qui dit « le malware chiffre des fichiers en AES » sans préciser la clé, l'IV, la méthode d'extraction, et sans fournir de déchiffreur n'est pas actionnable. La valeur d'un rapport de malware réside dans ses **livrables concrets** : IOC déployables, règles YARA, outil de remédiation.

---

## Pour aller plus loin

Si vous avez complété ce checkpoint et souhaitez approfondir, voici des pistes d'extension qui ne nécessitent pas de nouveau chapitre :

**Modifier le sample et recommencer.** Changez la clé et l'IV dans le code source, recompilez, et refaites l'analyse complète sur le nouveau binaire sans regarder les sources. Vérifiez que vos règles YARA génériques détectent encore la variante modifiée.

**Ajouter du packing.** Compressez le binaire avec UPX (`upx ransomware_O2_strip`), puis tentez de mener l'analyse. Quelles étapes sont bloquées ? Comment dépacker avant d'analyser ? Ce scénario fait le lien direct avec le [Chapitre 29 — Unpacking](/29-unpacking/README.md).

**Comparer avec un vrai rapport public.** Consultez des rapports publiés par des équipes de threat intelligence (Mandiant, CrowdStrike, Kaspersky GReAT, ANSSI-CERT) sur des ransomwares réels. Comparez leur structure avec votre rapport : quelles sections ajoutent-ils ? Quel niveau de détail fournissent-ils sur la cryptographie ? Comment présentent-ils les IOC ?

**Automatiser le pipeline complet.** Écrivez un script unique qui prend un binaire suspect en entrée et produit automatiquement : le triage (`file`, `strings`, `checksec`), l'extraction des chaînes intéressantes, un rapport JSON structuré. Ce travail d'automatisation prépare le [Chapitre 35 — Automatisation et scripting](/35-automatisation-scripting/README.md).

⏭️ [Chapitre 28 — Analyse d'un dropper ELF avec communication réseau](/28-dropper/README.md)
