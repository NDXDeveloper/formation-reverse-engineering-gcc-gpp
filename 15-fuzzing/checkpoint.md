🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 15 : Fuzzing pour le Reverse Engineering

> **Objectif** : fuzzer le binaire `ch15-fileformat` avec AFL++, trouver au moins 2 crashs et les analyser.  
> 📦 **Binaire** : `binaries/ch15-fileformat/`  
> ⏱️ **Durée estimée** : 1h30 à 2h (compilation + fuzzing + analyse)  
> 📄 **Corrigé** : `solutions/ch15-checkpoint-solution.md`

---

## Contexte

Ce checkpoint valide votre maîtrise de l'ensemble du pipeline de fuzzing orienté RE présenté dans ce chapitre. Vous travaillez sur le parseur de format custom `ch15-fileformat` — le même binaire que celui du cas pratique (section 15.7). Si vous avez suivi le cas pratique en parallèle, vous pouvez repartir de vos résultats existants ; sinon, vous partez de zéro.

Le checkpoint est considéré réussi quand les **cinq livrables** ci-dessous sont produits.

---

## Livrables attendus

### 1. Binaire instrumenté et fonctionnel

Compiler `ch15-fileformat` avec `afl-gcc` (ou `afl-clang-fast`) et vérifier que l'instrumentation est active. Compiler également un build avec ASan pour le triage des crashs.

**Critère de validation** : `afl-showmap` produit une sortie non vide en exécutant le binaire instrumenté avec un seed valide.

### 2. Corpus initial et dictionnaire

Construire un corpus initial d'au moins **3 seeds** ciblant des branches différentes du parseur, et un dictionnaire d'au moins **10 tokens** pertinents extraits du triage (`strings`, constantes, valeurs limites).

**Critère de validation** : chaque seed atteint au moins une branche distincte du parseur (vérifiable avec `afl-showmap` — les bitmaps des seeds doivent différer).

### 3. Campagne de fuzzing AFL++ productive

Lancer `afl-fuzz` avec le corpus et le dictionnaire. Laisser tourner jusqu'à obtenir au minimum :

- **20 inputs** dans le corpus (`corpus count ≥ 20`)  
- **2 crashs** sauvegardés (`saved crashes ≥ 2`)

**Critère de validation** : le répertoire `out/default/queue/` contient au moins 20 fichiers et `out/default/crashes/` contient au moins 2 fichiers.

### 4. Analyse détaillée de 2 crashs

Pour chacun des 2 crashs (au minimum) :

- **Reproduire** le crash sur le build ASan et noter le type de bug (heap-buffer-overflow, SEGV, stack-buffer-overflow, etc.) et la fonction concernée.  
- **Minimiser** l'input crash avec `afl-tmin`.  
- **Examiner** l'input minimisé avec `xxd` et proposer une interprétation des champs (quels octets correspondent au magic, à la version, au type de section, à la longueur, etc.).  
- **Tracer** le crash dans GDB (ou GEF/pwndbg) pour identifier le chemin de conditions emprunté depuis l'entrée du parseur jusqu'au point de crash.

**Critère de validation** : pour chaque crash, un paragraphe décrivant le type de bug, la fonction fautive, le chemin d'exécution, et les champs du format impliqués.

### 5. Ébauche de cartographie du format

À partir des crashs analysés et de l'examen du corpus, produire un tableau (même partiel) décrivant les champs identifiés du format d'entrée :

```
Offset  Taille  Champ              Valeurs connues
──────  ──────  ─────────────────  ──────────────────────
0x00    ?       ...                ...
...
```

**Critère de validation** : au moins 4 champs identifiés avec leur offset, leur taille et une description de leur rôle.

---

## Grille d'auto-évaluation

| Critère | Insuffisant | Acquis | Maîtrisé |  
|---------|-------------|--------|----------|  
| **Compilation instrumentée** | Le binaire n'est pas instrumenté ou `afl-showmap` échoue | Build AFL++ fonctionnel, `afl-showmap` produit une bitmap | Build AFL++ + build ASan + build gcov pour la couverture |  
| **Corpus et dictionnaire** | Corpus vide ou générique (un seul `\x00`), pas de dictionnaire | 3+ seeds ciblés, dictionnaire de 10+ tokens issus du triage | Seeds construits par branche identifiée, dictionnaire enrichi après première passe |  
| **Campagne de fuzzing** | Moins de 20 inputs ou 0 crash | 20+ inputs, 2+ crashs, campagne stable | 50+ inputs, 3+ crashs, fuzzing parallèle, dictionnaire itéré |  
| **Analyse des crashs** | Crashs listés mais non analysés | 2 crashs reproduits avec ASan, inputs examinés avec `xxd` | Crashs minimisés, tracés dans GDB, chemin de conditions documenté |  
| **Cartographie du format** | Aucune information structurelle extraite | 4+ champs identifiés avec offset et taille | Tableau de champs + arbre de décisions du parseur |

---

## Conseils avant de commencer

**Commencez par le triage.** Les 5 premières minutes avec `file`, `strings` et `checksec` économisent des heures de fuzzing à l'aveugle. Chaque chaîne de caractère trouvée est un indice potentiel pour le dictionnaire ou le corpus.

**Testez vos seeds avant de lancer le fuzzer.** Exécutez chaque seed manuellement sur le binaire et vérifiez qu'il ne provoque pas un rejet immédiat (« file too small », « invalid magic »). Un seed qui passe au moins la première validation est infiniment plus utile qu'un seed rejeté à l'entrée.

**Ne laissez pas tourner indéfiniment.** Si `last new find` dans le tableau de bord AFL++ ne bouge plus depuis 15 minutes, passez à l'analyse des résultats ou enrichissez le corpus/dictionnaire. Le temps du reverse engineer est plus précieux que le temps CPU.

**Minimisez avant d'analyser.** Un crash de 200 octets est difficile à interpréter. Le même crash minimisé à 18 octets est souvent directement lisible — chaque octet compte.

**Documentez au fil de l'eau.** Notez vos observations à chaque étape : ce que `strings` a révélé, pourquoi vous avez choisi tel seed, ce que le crash vous a appris. Ces notes constituent votre livrable final et seront réutilisées au Chapitre 25.

---

## Passerelle vers la suite

Ce checkpoint clôt la Partie III (Analyse Dynamique). Le binaire `ch15-fileformat` sera repris au Chapitre 25 avec une analyse complète : ImHex pour la cartographie hexadécimale, Ghidra pour l'analyse statique approfondie, et un parser Python complet. Le corpus, le dictionnaire et les connaissances structurelles produits ici seront directement réutilisés.

La Partie IV (Techniques Avancées) commence au Chapitre 16 avec l'étude des optimisations du compilateur — un sujet qui change radicalement l'apparence du code désassemblé et impacte directement les résultats du fuzzing (un binaire `-O2` ne crashe pas aux mêmes endroits qu'un binaire `-O0`).

---


⏭️ [Partie IV — Techniques Avancées de RE](/partie-4-techniques-avancees.md)
