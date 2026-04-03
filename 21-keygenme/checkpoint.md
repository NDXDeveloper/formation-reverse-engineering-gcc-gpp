🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 21

## Objectif

Produire un **keygen Python fonctionnel** capable de générer des clés de licence valides pour n'importe quel username, et **valider automatiquement** ces clés contre les trois variantes principales du binaire : `keygenme_O0`, `keygenme_O2` et `keygenme_O2_strip`.

Ce checkpoint vérifie que l'ensemble du cycle de RE présenté dans ce chapitre a été assimilé, de l'analyse statique initiale jusqu'à la reconstruction de l'algorithme.

---

## Critères de validation

Le checkpoint est considéré comme réussi lorsque les **cinq critères** suivants sont remplis simultanément :

### 1. Le keygen est autonome

Le script Python fonctionne sans dépendre du binaire à l'exécution. Il prend un username en entrée et produit une clé en sortie, en reproduisant l'algorithme extrait du désassemblage. Il n'appelle ni GDB, ni angr, ni le binaire lui-même pour calculer la clé.

### 2. Les clés sont valides sur `keygenme_O0`

Pour au moins 10 usernames distincts (de longueurs variées, entre 3 et 31 caractères), la clé générée est acceptée par `keygenme_O0` avec le message `[+] Valid license!`.

### 3. Les clés sont valides sur `keygenme_O2`

Les mêmes clés, pour les mêmes usernames, sont acceptées par `keygenme_O2`. Ce critère vérifie que le keygen reproduit l'algorithme sémantique du programme et non un artefact lié au niveau d'optimisation `-O0`.

### 4. Les clés sont valides sur `keygenme_O2_strip`

Les mêmes clés sont acceptées par `keygenme_O2_strip`. Ce critère vérifie que le keygen fonctionne indépendamment de la présence ou l'absence de symboles — c'est le même algorithme, seule l'enveloppe binaire diffère.

### 5. La validation est automatisée

Un script de validation (utilisant `pwntools` ou `subprocess`) soumet automatiquement les clés générées aux trois binaires et affiche un rapport succès/échec. Pas de copier-coller manuel.

---

## Livrable attendu

Le checkpoint produit **deux fichiers** :

| Fichier | Rôle |  
|---|---|  
| `keygen_keygenme.py` | Le keygen autonome. Prend un username en argument et affiche la clé. |  
| `validate_checkpoint.py` | Le script de validation automatisée. Teste le keygen contre les 3 variantes pour N usernames et affiche un rapport. |

### Format de sortie attendu du script de validation

```
══════════════════════════════════════════════════
  Checkpoint 21 — Validation du keygen
══════════════════════════════════════════════════

  Username            Clé générée           O0    O2    O2s
  ──────────────────────────────────────────────────────────
  Alice               DCEB-0DFC-B51F-3428   ✅    ✅    ✅
  Bob                 679E-0910-0F9D-94B5   ✅    ✅    ✅
  X1z                 B818-3F1B-CC86-5274   ✅    ✅    ✅
  ReverseEngineer     6865-6B66-F22C-F8FB   ✅    ✅    ✅
  ...

  Résultat : 30/30 validations réussies.
  ✅ Checkpoint réussi.
```

---

## Rappel méthodologique

Le chemin complet pour arriver au keygen a été couvert dans les sections du chapitre. Voici la correspondance entre chaque critère et la section qui fournit les outils pour le remplir :

| Critère | Compétence requise | Section de référence |  
|---|---|---|  
| Keygen autonome | Reconstruction de l'algorithme en Python | 21.8 |  
| Valide sur `_O0` | Traduction correcte du pseudo-C Ghidra | 21.3, 21.8 |  
| Valide sur `_O2` | Compréhension que l'optimisation ne change pas la sémantique | 21.4, 21.8 |  
| Valide sur `_O2_strip` | Capacité à analyser un binaire sans symboles | 21.1, 21.3, 21.5 |  
| Validation automatisée | Utilisation de `pwntools` (`process`, `sendline`, `recvuntil`) | 21.8 |

### Points de vérification intermédiaires

Si le keygen échoue, les étapes de diagnostic suivantes permettent d'isoler le problème :

**Le hash est-il correct ?** — Comparer la sortie de `compute_hash(b"Alice")` en Python avec la valeur observée dans GDB (section 21.5). Poser un breakpoint après l'appel à `compute_hash` dans `check_license` et lire `EAX`. Si les deux valeurs divergent, l'erreur est dans la traduction de `compute_hash` — vérifier les masquages `& 0xFFFFFFFF`, la rotation quand le count vaut 0, et l'ordre des opérations dans la boucle.

**Les groupes sont-ils corrects ?** — Comparer les quatre groupes produits par `derive_key` en Python avec les valeurs dans le tableau `groups[4]` en mémoire (lisible via `x/4hx $rsp+offset` dans GDB après l'appel à `derive_key`). Si le hash est bon mais les groupes divergent, l'erreur est dans les constantes XOR ou dans les rotations de `derive_key`.

**Le formatage est-il correct ?** — Comparer la chaîne produite par `format_key` en Python avec la chaîne `expected` capturée dans GDB juste avant `strcmp` (section 21.5, `x/s $rdi`). Si les groupes sont bons mais la chaîne diffère, vérifier le format (`%04X` = hexadécimal majuscule padé à 4 chiffres, `{:04X}` en Python).

**La soumission est-elle correcte ?** — Si la clé affichée par le keygen correspond à celle capturée en GDB mais que le binaire la refuse, le problème est dans l'interaction `pwntools` : caractère de fin de ligne parasite (`\r\n` vs `\n`), troncature de la clé, ou timing de lecture. Vérifier avec `p.recvall()` ce que le binaire renvoie exactement.

---

## Variantes pour aller plus loin

Une fois le checkpoint validé, les extensions suivantes permettent d'approfondir :

- **Tester sur `keygenme_O3` et `keygenme_strip`** en plus des trois variantes requises. Le keygen devrait fonctionner sans modification (5/5 variantes).  
- **Comparer la clé produite par le keygen avec celle trouvée par angr** (section 21.7) pour le même username. Les deux doivent être identiques — c'est une double validation par des méthodes indépendantes.  
- **Mesurer le temps d'exécution** du keygen vs angr. Le keygen devrait être quasi instantané (millisecondes), tandis qu'angr prend plusieurs secondes à chaque exécution. Cette différence illustre le coût de l'exécution symbolique par rapport à la reproduction directe de l'algorithme.  
- **Adapter le keygen en C** au lieu de Python, en utilisant exactement les mêmes types (`uint32_t`, `uint16_t`) que le binaire original. Comparer le binaire produit avec le keygenme via `objdump` pour observer les similitudes dans le code machine.

---

## Solution

Le corrigé complet est disponible dans `solutions/ch21-checkpoint-keygen.py`. Consultez-le uniquement après avoir tenté de produire votre propre version — la valeur pédagogique du checkpoint réside dans la démarche de reconstruction, pas dans le résultat final.

⏭️ [Chapitre 22 — Reverse d'une application C++ orientée objet](/22-oop/README.md)
