🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 7

## Désassembler `keygenme_O0` et `keygenme_O2`, lister les différences clés

> 📦 **Binaires** : `keygenme_O0` et `keygenme_O2` (répertoire `binaries/ch07-keygenme/`)  
> 🔧 **Outils** : `objdump`, `readelf`, `nm`, `c++filt`, `grep`, `diff`  
> ⏱️ **Durée estimée** : 30 à 45 minutes  
> 📝 **Livrable** : un court rapport (texte libre ou Markdown) documentant vos observations

---

## Objectif

Ce checkpoint valide les compétences acquises dans l'ensemble du chapitre 7. Vous allez désassembler deux variantes du même programme — compilées respectivement en `-O0` et en `-O2` — et produire un rapport comparatif structuré qui met en évidence les transformations introduites par l'optimisation.

Il ne s'agit pas de comprendre la logique complète du programme (ce sera l'objet du chapitre 21). L'objectif ici est de **démontrer votre capacité à lire, naviguer et comparer des listings `objdump`** en appliquant les techniques vues aux sections 7.1 à 7.7.

---

## Ce que votre rapport doit couvrir

### 1. Triage initial des deux binaires

Avant de désassembler, caractérisez chaque binaire avec les outils du chapitre 5 :

- Taille des fichiers et taille de la section `.text` (via `readelf -S`).  
- Présence ou absence de symboles (`file`, `nm`).  
- Nombre approximatif de fonctions utilisateur dans `.text`.

Notez les premières différences quantitatives entre les deux versions.

### 2. Identification de `main()` et des fonctions utilisateur

En appliquant les techniques de la section 7.5 :

- Localisez `main()` dans chaque binaire (par les symboles si disponibles, ou via `_start` et `__libc_start_main` si vous voulez pratiquer la méthode sur binaire strippé).  
- Listez les fonctions utilisateur présentes dans les deux versions (noms et adresses).  
- Notez si les deux binaires contiennent le même nombre de fonctions utilisateur, ou si certaines ont disparu en `-O2` (inlining).

### 3. Comparaison des prologues et épilogues

En appliquant les techniques de la section 7.4, pour chaque fonction utilisateur :

- Décrivez le prologue en `-O0` : présence du frame pointer, taille de l'allocation sur la pile (`sub rsp, N`), registres callee-saved sauvegardés.  
- Décrivez le prologue en `-O2` : le frame pointer est-il encore présent ? La fonction alloue-t-elle de l'espace sur la pile ? Quels registres sont sauvegardés ?  
- Notez les épilogues correspondants (`leave`+`ret` vs `pop`+`ret` vs `ret` seul).

### 4. Différences dans le corps des fonctions

C'est le cœur du rapport. Pour au moins une fonction (de préférence la plus intéressante — celle qui contient la logique de vérification), comparez :

- **Accès aux variables** : en `-O0`, identifiez les accès `[rbp-N]` (variables sur la pile). En `-O2`, identifiez les registres qui remplacent ces variables.  
- **Nombre d'instructions** : comptez (ou estimez) le nombre d'instructions dans la fonction pour chaque version.  
- **Optimisations visibles** : repérez les transformations concrètes — disparition des store-load inutiles, strength reduction (multiplication remplacée par un shift), propagation de constantes, réordonnancement d'instructions.  
- **Structure des boucles** : si la fonction contient une boucle, comparez sa structure dans les deux versions. Le pattern d'initialisation/test/corps/incrémentation est-il préservé en `-O2` ?

### 5. Appels de fonctions et PLT

- Les mêmes fonctions de la libc sont-elles appelées dans les deux versions (`printf`, `strcmp`, `puts`…) ?  
- Y a-t-il des `call` internes qui ont disparu en `-O2` (signe d'inlining) ?  
- Les appels PLT sont-ils identiques dans les deux listings ?

### 6. Synthèse

Concluez avec un paragraphe de synthèse répondant à la question : **si vous receviez le binaire `-O2` sans jamais avoir vu la version `-O0`, quelles difficultés supplémentaires auriez-vous rencontrées pour comprendre la logique du programme ?**

---

## Méthode recommandée

Voici un workflow efficace pour produire le rapport :

```bash
# 1. Générer les deux listings complets
objdump -d -M intel keygenme_O0 > /tmp/O0.asm  
objdump -d -M intel keygenme_O2 > /tmp/O2.asm  

# 2. Optionnel : générer une version avec source entrelacée (si -g)
objdump -d -S -M intel keygenme_O0 > /tmp/O0_src.asm

# 3. Comparer côte à côte
diff -y --width=160 /tmp/O0.asm /tmp/O2.asm | less

# 4. Compter les fonctions (approximation par les prologues)
grep -c "push   rbp" /tmp/O0.asm  
grep -c "push   rbp" /tmp/O2.asm  

# 5. Compter les instructions dans .text
grep -c '^ ' /tmp/O0.asm  
grep -c '^ ' /tmp/O2.asm  

# 6. Lister les appels de fonctions internes
grep "call" /tmp/O0.asm | grep -v "plt" | sort -u  
grep "call" /tmp/O2.asm | grep -v "plt" | sort -u  
```

Ouvrez les deux fichiers `.asm` dans votre éditeur de texte avec une vue en split, et travaillez fonction par fonction.

---

## Critères de validation

Votre checkpoint est validé si votre rapport :

- ✅ Identifie correctement `main()` et les fonctions utilisateur dans les deux binaires.  
- ✅ Décrit au moins une différence de prologue/épilogue entre `-O0` et `-O2`.  
- ✅ Identifie au moins deux optimisations concrètes dans le corps d'une fonction (par exemple : variables passées de la pile aux registres, suppression de store-load, strength reduction).  
- ✅ Note la différence de nombre d'instructions entre les deux versions pour au moins une fonction.  
- ✅ Contient une synthèse sur l'impact de l'optimisation sur la lisibilité en RE.

Ne cherchez pas l'exhaustivité : un rapport clair de 1 à 2 pages couvrant ces points est suffisant. La qualité de l'observation prime sur la quantité.

---

## Vérification

Comparez votre rapport avec le corrigé disponible dans `solutions/ch07-checkpoint-solution.md`. Le corrigé liste les différences attendues — les vôtres ne doivent pas nécessairement être identiques (les adresses peuvent varier selon votre version de GCC et votre distribution), mais les observations de fond (type d'optimisations, impact sur la structure) doivent converger.

---

> **Chapitre suivant** : [Chapitre 8 — Désassemblage avancé avec Ghidra](/08-ghidra/README.md)

⏭️ [Chapitre 8 — Désassemblage avancé avec Ghidra](/08-ghidra/README.md)
