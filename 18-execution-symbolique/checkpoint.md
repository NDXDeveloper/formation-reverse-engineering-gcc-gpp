🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Résoudre `keygenme_O2_strip` avec angr en moins de 30 lignes Python

> **Chapitre 18 — Exécution symbolique et solveurs de contraintes**  
> Partie IV — Techniques Avancées de RE

---

## Objectif

Écrire un script Python autonome qui utilise angr pour trouver automatiquement un serial valide pour le binaire `keygenme_O2_strip` — compilé avec `-O2` et sans symboles. Le script doit faire **30 lignes de code utiles ou moins** (les lignes vides, les commentaires et les imports ne comptent pas).

Ce checkpoint valide votre capacité à :

- Charger un binaire strippé dans angr.  
- Créer des entrées symboliques de la bonne taille avec les bonnes contraintes.  
- Configurer et lancer une exploration.  
- Extraire et afficher la solution.

---

## Binaire cible

```
binaries/ch18-keygenme/keygenme_O2_strip
```

Si vous ne l'avez pas encore compilé :

```bash
cd binaries/ch18-keygenme/  
make keygenme_O2_strip  
```

### Rappel des caractéristiques

| Propriété | Valeur |  
|---|---|  
| Compilateur | GCC, `-O2` |  
| Symboles | Non (strippé avec `-s`) |  
| Architecture | x86-64 |  
| Entrée | `argv[1]`, 16 caractères hexadécimaux |  
| Succès | Affiche `Access Granted!` sur stdout |  
| Échec | Affiche `Access Denied.` sur stdout |

---

## Contraintes du checkpoint

1. **30 lignes de code utiles maximum.** Sont exclues du décompte : les lignes vides, les lignes de commentaires purs (`# ...`), les lignes d'import, et le `if __name__` final. Chaque ligne de logique (création de variables, appels angr, boucles, conditions, print) compte.

2. **Le script doit être autonome.** Il doit pouvoir être lancé avec `python3 solve.py` sans argument supplémentaire et afficher le serial valide.

3. **Le serial affiché doit fonctionner.** La vérification `./keygenme_O2_strip <serial>` doit afficher `Access Granted!`.

4. **Pas de hardcoding de la solution.** Le script doit réellement utiliser l'exécution symbolique pour trouver le serial, pas simplement l'imprimer.

5. **Pas de consultation du code source.** Le script doit fonctionner comme si vous n'aviez que le binaire. L'utilisation des chaînes stdout comme critère est autorisée (et recommandée).

---

## Ce que vous devez mobiliser

Toutes les briques nécessaires ont été vues dans ce chapitre :

| Concept | Section |  
|---|---|  
| Créer un `Project` avec `auto_load_libs=False` | 18.2 |  
| Créer des bitvectors symboliques avec `claripy.BVS` | 18.2 |  
| Concaténer des caractères symboliques | 18.2 |  
| Contraindre des caractères à un jeu de valeurs (hex) | 18.2 |  
| Créer un `entry_state` avec des arguments symboliques | 18.2 |  
| Lancer `explore()` avec des critères stdout | 18.3 |  
| Extraire la solution avec `solver.eval()` | 18.3 |

---

## Indices progressifs

N'ouvrez les indices que si vous êtes bloqué. Essayez d'abord sans.

<details>
<summary><strong>Indice 1 — Structure générale</strong></summary>

Le script suit le squelette en 7 étapes vu en section 18.2 :  
charger → symboliser → état initial → contraindre → explorer → vérifier → afficher.  

Chacune de ces étapes tient en 1 à 5 lignes.

</details>

<details>
<summary><strong>Indice 2 — Entrée symbolique</strong></summary>

Le serial fait 16 caractères. Créez une liste de 16 `BVS` de 8 bits chacun, puis concaténez-les avec `claripy.Concat`. Passez le résultat comme deuxième élément de `args` dans `entry_state`.

</details>

<details>
<summary><strong>Indice 3 — Contraintes</strong></summary>

Chaque caractère doit être un chiffre hexadécimal. Trois plages : `'0'–'9'` (0x30–0x39), `'A'–'F'` (0x41–0x46), `'a'–'f'` (0x61–0x66). Utilisez `claripy.Or` et `claripy.And` dans une boucle sur les 16 caractères.

</details>

<details>
<summary><strong>Indice 4 — Critères d'exploration</strong></summary>

Utilisez des fonctions lambda sur `s.posix.dumps(1)` pour détecter `b"Access Granted"` (find) et `b"Access Denied"` (avoid). Cela vous dispense de chercher des adresses dans un binaire strippé.

</details>

<details>
<summary><strong>Indice 5 — Extraction de la solution</strong></summary>

Après `explore()`, vérifiez `simgr.found`. Sur le premier état trouvé, appelez `s.solver.eval(serial_bvs, cast_to=bytes)` où `serial_bvs` est le bitvector concaténé de 128 bits (16 × 8). Décodez le résultat avec `.decode()`.

</details>

---

## Critères de validation

| Critère | Attendu |  
|---|---|  
| Le script se lance sans erreur | `python3 solve.py` s'exécute sans exception |  
| Le script affiche un serial | Une chaîne de 16 caractères hexadécimaux apparaît sur stdout |  
| Le serial est valide | `./keygenme_O2_strip <serial>` affiche `Access Granted!` |  
| Le code fait ≤ 30 lignes utiles | Décompte hors lignes vides, commentaires et imports |  
| Pas de hardcoding | Le serial est trouvé par exécution symbolique, pas écrit en dur |  
| Temps d'exécution raisonnable | Résolution en moins de 5 minutes sur une machine standard |

---

## Vérification

Une fois votre script écrit, lancez-le et vérifiez :

```bash
# 1. Activer l'environnement angr
source ~/angr-env/bin/activate

# 2. Lancer le script
python3 solve.py
# Sortie attendue : quelque chose comme
#   [+] Serial trouvé : 7f3a1b9e5c82d046

# 3. Vérifier la solution
./keygenme_O2_strip 7f3a1b9e5c82d046
# Sortie attendue : Access Granted!
```

> ⚠️ Le serial exact peut varier d'une exécution à l'autre et d'un build à l'autre. Ce qui compte, c'est que `./keygenme_O2_strip <votre_serial>` affiche `Access Granted!`.

---

## Solution de référence

> ⚠️ **Spoiler** — N'ouvrez le fichier de solution qu'après avoir tenté le checkpoint par vous-même.

Le corrigé complet se trouve dans :

```
solutions/ch18-checkpoint-solution.py
```

Ce fichier contient la solution principale (26 lignes utiles, commentée ligne par ligne), une vérification automatique du serial sur le binaire réel, et une résolution alternative en Z3 pur à titre de comparaison.

---

## Pour aller plus loin

Si le checkpoint vous a semblé facile, essayez ces variantes (non notées) :

- **Variante A** — Résoudre `keygenme_O3_strip` au lieu de `O2`. Le script devrait fonctionner sans modification, mais vérifiez.  
- **Variante B** — Modifier le script pour démarrer l'exploration à l'entrée de la fonction de vérification (`blank_state`) au lieu de `entry_state`. Vous devrez trouver l'adresse de cette fonction dans le binaire strippé avec `objdump`.  
- **Variante C** — Réécrire la résolution entièrement en Z3 (sans angr), en extrayant les contraintes depuis le pseudo-code Ghidra.  
- **Variante D** — Modifier le script pour qu'il trouve **toutes** les solutions valides (s'il y en a plusieurs du fait de la casse hex).

---

> ✅ **Checkpoint validé ?** Vous maîtrisez les bases de l'exécution symbolique avec angr. Vous êtes prêt pour les cas pratiques de la **Partie V**, notamment le chapitre 21 (reverse complet du keygenme avec toutes les approches) et le chapitre 24 (extraction de clés cryptographiques).

⏭️ [Chapitre 19 — Anti-reversing et protections compilateur](/19-anti-reversing/README.md)
