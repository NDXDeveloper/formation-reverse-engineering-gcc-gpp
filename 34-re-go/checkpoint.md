🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 34

## Analyser un binaire Go strippé, retrouver les fonctions et reconstruire la logique

> *Ce checkpoint valide l'ensemble des compétences du chapitre 34. Vous travaillez exclusivement sur `crackme_go_strip` — le binaire strippé, sans symboles ELF ni DWARF. Votre objectif : reconstruire suffisamment de contexte pour comprendre la logique de validation et produire une clé valide.*

---

## Binaire cible

```
binaries/ch34-go/crackme_go_strip
```

Compilez-le au préalable si ce n'est pas fait :

```bash
cd binaries/ch34-go && make
```

> ⚠️ **Règle du jeu** : ne consultez **pas** le code source `main.go` avant d'avoir terminé le checkpoint. Vous pouvez en revanche utiliser le binaire non strippé `crackme_go` pour vérifier vos résultats a posteriori.

---

## Objectifs

Le checkpoint se décompose en six objectifs progressifs. Chacun mobilise une ou plusieurs sections du chapitre.

### Objectif 1 — Triage et identification (sections 34.1, 34.5)

Déterminez, sans consulter le source, les informations suivantes :

- la version exacte du compilateur Go utilisé,  
- la taille du binaire et la raison de cette taille,  
- si le binaire est lié statiquement ou dynamiquement,  
- les protections actives (`checksec`),  
- les chaînes de caractères pertinentes pour le code métier (en filtrant le bruit du runtime).

**Livrable** : un paragraphe de triage résumant vos trouvailles, similaire au workflow du chapitre 5 adapté aux spécificités Go.

### Objectif 2 — Récupération des symboles (section 34.4)

Extrayez les noms de fonctions depuis les structures internes du binaire :

- localisez `gopclntab` (par son magic number ou via un outil),  
- extrayez la liste complète des fonctions utilisateur (package `main.*`),  
- identifiez la version du format `pclntab` et confirmez la cohérence avec la version du compilateur.

**Livrable** : la liste des fonctions du package `main` avec leurs adresses de début et de fin, et le nombre total de fonctions dans le binaire (runtime inclus).

### Objectif 3 — Import dans le désassembleur et identification de l'ABI (sections 34.2, 34.4)

Importez les symboles récupérés dans Ghidra (ou l'outil de votre choix) :

- appliquez les noms de fonctions au désassemblage,  
- déterminez quelle convention d'appel est utilisée (stack-based ou register-based) en examinant le prologue d'au moins deux fonctions du package `main`,  
- identifiez le registre utilisé pour le pointeur de goroutine `g` et vérifiez le préambule de vérification de pile.

**Livrable** : une capture ou un extrait annoté du désassemblage de `main.main` montrant les noms de fonctions restaurés et une note sur l'ABI identifiée.

### Objectif 4 — Reconstruction des types et structures de données (sections 34.3, 34.6)

Identifiez les structures de données utilisées par le crackme :

- quels types du package `main` sont définis (structs, interfaces) — extrayez-les via GoReSym ou `typelinks`,  
- pour chaque struct, donnez la liste des champs, leurs types et offsets,  
- identifiez les interfaces et listez les types concrets qui les implémentent (via les itabs),  
- repérez les usages de slices, maps et channels dans le code en cherchant les appels runtime caractéristiques (`runtime.makemap`, `runtime.makechan`, `runtime.growslice`, etc.).

**Livrable** : un fichier de définitions de types reconstruit (pseudo-Go ou pseudo-C), et une liste des structures de données dynamiques utilisées par le programme.

### Objectif 5 — Analyse de la logique de validation (sections 34.1, 34.2, 34.3, 34.5)

Reconstruisez le flux de validation de la clé licence :

- depuis `main.main`, tracez le graphe d'appels vers les fonctions de validation,  
- identifiez combien d'étapes de validation existent et dans quel ordre elles s'exécutent,  
- pour chaque étape, déterminez la condition de succès (quelle comparaison, quels opérandes, quelle valeur attendue),  
- identifiez si des goroutines sont lancées (`runtime.newproc`) et quel rôle elles jouent dans la validation,  
- extrayez les constantes clés (valeurs attendues, seeds, magic bytes) depuis `.rodata` ou le code.

**Livrable** : une description en langage naturel de l'algorithme de validation, étape par étape, avec les constantes extraites.

### Objectif 6 — Produire une clé valide

À partir de votre compréhension de la logique, produisez une clé de licence acceptée par le programme :

- exécutez le binaire avec votre clé candidate et vérifiez le message de succès,  
- si vous le souhaitez, écrivez un court script (Python, Go, ou autre) qui génère des clés valides.

**Livrable** : au moins une clé valide, accompagnée de votre raisonnement.

---

## Outils suggérés

| Outil | Usage dans ce checkpoint |  
|---|---|  
| `file`, `readelf`, `checksec` | Triage initial (objectif 1) |  
| `strings` + filtrage | Extraction de chaînes brutes (objectif 1) |  
| GoReSym | Extraction des fonctions, types, version Go (objectifs 2, 4) |  
| `jq` | Exploitation de la sortie JSON de GoReSym |  
| Ghidra + script d'import | Désassemblage et décompilation (objectifs 3, 4, 5) |  
| GDB (+ GEF/pwndbg) | Analyse dynamique, inspection des registres (objectifs 5, 6) |  
| Frida (optionnel) | Hooking des fonctions de validation (objectif 5) |  
| Python | Keygen (objectif 6) |

---

## Critères de validation

| Critère | Atteint | Non atteint |  
|---|---|---|  
| Version du compilateur identifiée | La version exacte (ex: `go1.22.1`) est trouvée | Version absente ou incorrecte |  
| Fonctions `main.*` listées | Liste complète avec adresses cohérentes | Liste partielle ou adresses erronées |  
| ABI correctement identifiée | Convention nommée (stack ou registre) avec preuve assembleur | ABI non déterminée ou confondue avec System V |  
| Types reconstruits | Au moins les structs et l'interface principale avec champs et offsets | Types absents ou incomplets |  
| Logique de validation décrite | Toutes les étapes identifiées avec conditions et constantes | Étapes manquantes ou conditions erronées |  
| Clé valide produite | Le binaire affiche le message de succès | Clé rejetée |

Le checkpoint est réussi quand les six critères sont atteints.

---

## Conseils méthodologiques

**Suivez l'ordre des objectifs.** Chaque étape s'appuie sur la précédente. Tenter de reverser la logique (objectif 5) sans avoir d'abord récupéré les noms de fonctions (objectif 2) est possible mais beaucoup plus laborieux.

**Commencez par l'analyse statique, validez par le dynamique.** Formulez des hypothèses en lisant le désassemblage, puis confirmez-les avec GDB. Par exemple, si vous pensez qu'une fonction retourne un booléen, posez un breakpoint à son retour et observez `RAX`.

**Filtrez le runtime.** Quand vous ouvrez le binaire dans Ghidra après import des symboles, le Symbol Tree contiendra des milliers d'entrées. Filtrez immédiatement sur `main.*` dans la barre de recherche du Symbol Tree. Les fonctions `runtime.*`, `fmt.*`, `sync.*`, etc. ne sont pertinentes que si vous les rencontrez comme cibles d'appel depuis le code `main`.

**Pour les channels et goroutines, pensez en flux de données.** Si une validation passe par des goroutines communicant via un channel, la question n'est pas comment le scheduler fonctionne, mais quelles données entrent et sortent du channel. Posez un breakpoint sur `runtime.chansend1` et `runtime.chanrecv1` pour capturer ces données.

**Notez tout.** Tenez un carnet (fichier texte, commentaires Ghidra, ou cahier papier) avec vos découvertes au fur et à mesure. Le RE est un processus itératif — une information apparemment insignifiante à l'objectif 2 peut devenir cruciale à l'objectif 5.

---

## Vérification

Une fois votre clé trouvée, vous pouvez vérifier avec le binaire non strippé :

```bash
./crackme_go VOTRE-CLEF-ICI-TEST
```

Le binaire non strippé et le binaire strippé exécutent le même code — une clé valide pour l'un est valide pour l'autre. La comparaison des deux binaires dans Ghidra (l'un avec symboles, l'autre avec vos symboles reconstruits) est un excellent moyen de mesurer la qualité de votre reconstruction.

Le corrigé complet se trouve dans `solutions/ch34-checkpoint-solution.md`.

⏭️ [Partie IX — Ressources & Automatisation](/partie-9-ressources.md)
