🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 25

> **Objectif** : produire les trois livrables du chapitre pour le format `ch25-fileformat` — un pattern `.hexpat`, un parser/sérialiseur Python, et une spécification documentée — et valider leur conformité avec le binaire original.

---

## Les trois livrables attendus

### Livrable 1 — Pattern ImHex (`.hexpat`)

Le pattern doit être applicable sur les trois archives fournies (`demo.cfr`, `packed_noxor.cfr`, `packed_xor.cfr`) et couvrir l'intégralité des octets de chaque fichier sans zone non attribuée.

**Critères de validation** :

| # | Critère | Comment vérifier |  
|---|---------|-----------------|  
| H1 | Le header (32 octets) est intégralement colorisé avec des noms de champs explicites. | Ouvrir chaque archive dans ImHex, appliquer le pattern, vérifier dans le panneau *Pattern Data* que les 8 champs du header sont nommés et que leurs valeurs sont cohérentes. |  
| H2 | Les flags sont décomposés en bits nommés (`xor_enabled`, `has_footer`). | Le bitfield doit afficher les bits individuels, pas juste la valeur numérique brute. |  
| H3 | Les records sont parsés en tableau de taille `num_records`, chacun avec ses sous-champs (type, flags, name_len, data_len, name, data, crc16). | Déplier le tableau `records` dans le panneau *Pattern Data*. Chaque record doit afficher son nom lisible et ses dimensions. |  
| H4 | Le type de chaque record utilise un `enum` nommé (TEXT, BINARY, META). | La colonne type doit afficher le nom symbolique, pas la valeur numérique. |  
| H5 | Le footer conditionnel est correctement placé en fin de fichier lorsque le flag `has_footer` est actif. | Le footer doit apparaître avec le magic `CRFE`, le `total_size` correspondant à la taille du fichier, et le `global_crc`. |  
| H6 | Le pattern ne produit aucune erreur sur les trois archives. | Aucun message d'erreur dans la console du *Pattern Editor*. |  
| H7 | Il ne reste aucun octet non colorisé entre le début du header et la fin du footer. | Visuellement, la vue hexadécimale doit être entièrement colorisée. |

### Livrable 2 — Parser/sérialiseur Python

Le module Python doit pouvoir lire, valider, écrire et recréer des archives CFR. Le test décisif est le round-trip validé par le binaire original.

**Critères de validation** :

| # | Critère | Comment vérifier |  
|---|---------|-----------------|  
| P1 | Le parser lit les trois archives fournies sans erreur et affiche le contenu de chaque record. | `python3 cfr_parser.py parse samples/demo.cfr` doit afficher les 4 records avec leurs noms, types et contenus. Idem pour les deux autres archives. |  
| P2 | Le parser détecte et signale les CRC invalides (header, records, global). | Corrompre manuellement un octet dans une copie d'archive, vérifier que le parser lève une erreur explicite identifiant le CRC en cause. |  
| P3 | Le parser gère correctement le dé-XOR des archives obfusquées. | Le contenu textuel des records de `packed_xor.cfr` doit être lisible après parsing, identique à celui de `packed_noxor.cfr`. |  
| P4 | Le sérialiseur produit des archives avec XOR que le binaire accepte. | Générer une archive avec `xor_enabled=True`, la valider avec `./fileformat_O0 validate`. |  
| P5 | Le sérialiseur produit des archives sans XOR que le binaire accepte. | Générer une archive avec `xor_enabled=False`, la valider avec `./fileformat_O0 validate`. |  
| P6 | Le sérialiseur produit des archives sans footer que le binaire accepte. | Générer une archive avec `include_footer=False`, vérifier avec `./fileformat_O0 list`. |  
| P7 | **Round-trip lecture → écriture** : lire chacune des trois archives, la réécrire, la faire valider par le binaire original. | Les trois commandes `./fileformat_O0 validate <roundtrip.cfr>` doivent retourner 0 erreur. |  
| P8 | **Génération ex nihilo** : créer une archive de toutes pièces (sans lire de fichier existant), la faire accepter par le binaire. | `./fileformat_O0 read <generated.cfr>` doit afficher les records avec le contenu attendu. |  
| P9 | Le parser rejette un fichier dont le magic n'est pas `CFRM`. | Créer un fichier de 32 octets avec un magic `XXXX`, vérifier que le parser refuse de le lire. |

### Livrable 3 — Spécification documentée

La spécification doit être un document Markdown autonome, compréhensible sans accès au binaire ni au code Python.

**Critères de validation** :

| # | Critère | Comment vérifier |  
|---|---------|-----------------|  
| S1 | Le document décrit la structure complète du header avec l'offset, la taille, le type et la sémantique de chaque champ. | Un lecteur doit pouvoir lire manuellement les 32 premiers octets d'une archive CFR avec uniquement `xxd` et la spec. |  
| S2 | Le document décrit la structure des records, incluant l'en-tête, le nom, les données et le CRC-16. | Un lecteur doit pouvoir localiser et délimiter chaque record dans un dump hexadécimal. |  
| S3 | Le document décrit le footer (structure, condition de présence, portée du CRC global). | — |  
| S4 | Les trois algorithmes (CRC-32, CRC-16, XOR) sont spécifiés avec leurs paramètres exacts (polynôme, valeur initiale, clé). | Un implémenteur doit pouvoir reproduire les CRC sur des données connues en suivant uniquement le pseudo-code de la spec. |  
| S5 | L'ordre des opérations CRC/XOR est documenté explicitement, du point de vue du producteur ET du parseur. | La spec doit lever toute ambiguïté sur le fait que le CRC-16 porte sur les données avant XOR. |  
| S6 | Les cas limites sont documentés (records vides, noms vides, types inconnus, footer absent). | — |  
| S7 | Les contraintes de validation sont listées (magic, num_records max, CRC, data_len_xor, footer). | Un implémenteur sait exactement quels invariants vérifier et dans quel ordre. |  
| S8 | Un exemple hexadécimal annoté est inclus. | Au moins les premiers 48 octets d'une archive doivent être annotés champ par champ. |

---

## Grille de validation croisée

Le test le plus fort consiste à vérifier que les trois livrables sont cohérents entre eux, pas seulement individuellement valides :

| Vérification croisée | Méthode |  
|----------------------|---------|  
| Le `.hexpat` et le parser Python produisent la même interprétation de chaque champ. | Comparer les valeurs affichées par ImHex (via le pattern) et celles affichées par le parser Python pour la même archive. Les noms, tailles, types, CRC doivent être identiques. |  
| La spec permet de recréer le parser. | Donner la spécification seule à un pair (ou la relire soi-même après quelques jours). Vérifier qu'il est possible d'écrire un parser fonctionnel en se basant uniquement sur le document, sans consulter le code Python ni le `.hexpat`. |  
| Le parser Python produit des fichiers que le `.hexpat` parse correctement. | Ouvrir une archive générée par le sérialiseur Python dans ImHex avec le pattern. Tous les champs doivent être colorisés et cohérents. |

---

## Ce qui dépasse le scope du checkpoint

Les éléments suivants sont des extensions possibles mais ne font pas partie de la validation minimale :

- Optimiser le parser Python pour des archives très volumineuses (streaming au lieu de lecture complète en mémoire).  
- Gérer une hypothétique version 3 du format.  
- Ajouter de la compression (zlib, lz4) comme nouveau flag — le format actuel ne supporte que le XOR.  
- Écrire un parser dans un autre langage (C, Rust) à partir de la spécification.  
- Intégrer le `.hexpat` dans la bibliothèque officielle de patterns d'ImHex.

Ces pistes sont laissées à la curiosité du lecteur.

---


⏭️ [Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)](/partie-6-malware.md)
