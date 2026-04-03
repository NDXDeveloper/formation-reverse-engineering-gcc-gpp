🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 20.3 — RetDec (Avast) — décompilation statique offline

> 📘 **Chapitre 20 — Décompilation et reconstruction du code source**  
> **Partie IV — Techniques Avancées de RE**

---

## Pourquoi un second décompilateur ?

La section précédente a présenté Ghidra comme l'outil central de décompilation dans cette formation, et c'est le cas. Alors pourquoi consacrer une section entière à un autre décompilateur ?

La raison est double. D'abord, **aucun décompilateur n'est universellement meilleur** — chacun utilise des heuristiques différentes, et là où Ghidra produit un pseudo-code confus, RetDec peut parfois proposer une reconstruction plus lisible, et inversement. Croiser les résultats de deux décompilateurs sur un même binaire est une pratique courante en RE professionnel, exactement comme on croise un désassemblage `objdump` avec une vue Ghidra.

Ensuite, RetDec a une caractéristique qui le distingue fondamentalement de Ghidra : il fonctionne **entièrement en ligne de commande**, sans interface graphique, et produit directement des fichiers `.c` et `.dsm` sur disque. Cela le rend idéal pour l'**intégration dans des scripts et des pipelines automatisés** — un sujet que l'on retrouvera au chapitre 35 (Automatisation et scripting).

---

## Présentation de RetDec

RetDec (pour *Retargetable Decompiler*) est un décompilateur open source développé initialement par le laboratoire de recherche d'Avast Software, aujourd'hui maintenu comme projet communautaire sur GitHub. Son code source est publié sous licence MIT.

### Architecture interne

RetDec adopte une architecture en pipeline à trois étapes, conceptuellement similaire à celle de Ghidra mais avec des choix techniques différents :

**Front-end : désassemblage et levée (lifting).** Le binaire ELF est analysé pour extraire les sections, les symboles et les fonctions. Le code machine x86-64 est traduit en une représentation intermédiaire basée sur LLVM IR (Intermediate Representation). Ce choix de LLVM comme IR interne est ce qui rend RetDec « retargetable » — il peut en théorie accepter n'importe quelle architecture pour laquelle un front-end de lifting existe.

**Middle-end : optimisations et analyses.** Sur cette IR, RetDec applique des passes d'analyse qui incluent la détection de types, la reconstruction de structures de contrôle, la reconnaissance d'idiomes (comme les divisions par multiplication inverse), l'identification de fonctions de bibliothèque (via ses propres signatures), et la propagation de types. Ces passes utilisent les optimisations LLVM standard enrichies de passes spécifiques au RE.

**Back-end : émission du pseudo-code.** L'IR optimisée est convertie en pseudo-code C. Ce pseudo-code est écrit dans un fichier `.c` sur disque, accompagné d'un fichier de désassemblage `.dsm` et éventuellement d'un graphe de flux de contrôle.

### Formats supportés

RetDec gère nativement les formats ELF (Linux), PE (Windows), Mach-O (macOS), COFF, et les binaires bruts (raw). Pour nos binaires d'entraînement compilés avec GCC sous Linux, le format ELF est détecté automatiquement. Les architectures supportées incluent x86, x86-64, ARM, MIPS et PowerPC — un spectre comparable à celui de Ghidra.

---

## Installation

RetDec s'installe de plusieurs manières selon l'environnement de travail.

### Via les paquets pré-compilés

Le dépôt GitHub officiel (`avast/retdec`) publie des releases pré-compilées pour Linux. Sur Ubuntu/Debian, la méthode recommandée est de télécharger l'archive de la dernière release et de l'extraire :

```bash
# Télécharger la dernière release (adapter le numéro de version)
wget https://github.com/avast/retdec/releases/download/v5.0/RetDec-v5.0-Linux-Release.tar.xz

# Extraire
tar -xf RetDec-v5.0-Linux-Release.tar.xz

# Ajouter au PATH (à mettre dans ~/.bashrc pour persister)
export PATH="$PWD/retdec/bin:$PATH"

# Vérifier l'installation
retdec-decompiler --help
```

### Compilation depuis les sources

Pour ceux qui veulent la dernière version de développement ou qui ont besoin de modifier RetDec :

```bash
git clone https://github.com/avast/retdec.git  
cd retdec  
mkdir build && cd build  
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/retdec-install  
make -j$(nproc)  
make install  
```

La compilation depuis les sources nécessite CMake ≥ 3.16, un compilateur C++17, et peut prendre 20 à 40 minutes selon la machine. Les dépendances LLVM sont incluses dans le dépôt (pas besoin d'une installation LLVM système).

### Vérification rapide

Pour confirmer que RetDec fonctionne sur notre environnement, décompilons un binaire de test :

```bash
retdec-decompiler keygenme_O0
```

RetDec doit produire deux fichiers : `keygenme_O0.c` (pseudo-code) et `keygenme_O0.dsm` (désassemblage annoté). Si ces fichiers apparaissent, l'installation est fonctionnelle.

---

## Utilisation en ligne de commande

### Décompilation basique

La commande la plus simple prend un binaire en argument et produit le pseudo-code correspondant :

```bash
retdec-decompiler keygenme_O2_strip
```

RetDec affiche sa progression sur la sortie standard — détection du format, identification de l'architecture, analyse des fonctions, passes d'optimisation, émission du code. Les fichiers de sortie sont créés dans le même répertoire que le binaire d'entrée :

```
keygenme_O2_strip.c      ← pseudo-code C  
keygenme_O2_strip.dsm    ← désassemblage annoté  
```

### Options utiles

RetDec propose plusieurs options qui modifient le comportement de la décompilation.

**Sélection d'une fonction spécifique.** Par défaut, RetDec décompile tout le binaire. Pour ne traiter qu'une seule fonction (ce qui est beaucoup plus rapide sur les gros binaires) :

```bash
# Par nom de symbole
retdec-decompiler --select-functions derive_key keygenme_O2

# Par plage d'adresses
retdec-decompiler --select-ranges 0x401200-0x401350 keygenme_O2_strip
```

**Désactivation de certaines passes.** Si une passe d'optimisation produit un résultat moins lisible que l'entrée (cela arrive avec l'élimination de variables ou la simplification de boucles), on peut la désactiver :

```bash
retdec-decompiler --backend-no-opts keygenme_O3
```

**Sortie de l'IR LLVM intermédiaire.** Pour les analystes avancés qui souhaitent inspecter la représentation intermédiaire produite par le front-end de lifting :

```bash
retdec-decompiler --print-after-all keygenme_O2
```

Cette commande affiche l'IR LLVM après chaque passe d'optimisation, ce qui permet d'observer les transformations appliquées. L'IR produite peut ensuite être analysée avec les outils LLVM standards (`opt`, `llvm-dis`).

**Fichier de configuration.** Pour les analyses répétées avec les mêmes options, RetDec accepte un fichier de configuration JSON :

```bash
retdec-decompiler --config config.json keygenme_O2
```

---

## Analyse du pseudo-code produit

Comparons maintenant la sortie de RetDec avec celle de Ghidra sur les mêmes fonctions de nos binaires d'entraînement.

### Fonction verify_key à -O0

Voici une sortie typique de RetDec pour la fonction `verify_key` de `keygenme_O0` :

```c
// Address range: 0x401290 - 0x4012d8
int32_t verify_key(uint8_t * expected, uint8_t * provided) {
    int32_t result = 0;
    for (int32_t i = 0; i < 16; i++) {
        result |= (int32_t)expected[i] ^ (int32_t)provided[i];
    }
    return result == 0;
}
```

Plusieurs observations immédiates. D'abord, la reconstruction est très fidèle au source original — la boucle `for` est propre, la comparaison XOR octet par octet est claire, et le retour conditionnel est bien exprimé. RetDec a même identifié la taille de la boucle comme le littéral `16` et non `0x10`, un choix de formatage qui améliore la lisibilité.

Ensuite, on note que RetDec affiche les adresses de la fonction en commentaire (`Address range`), ce qui facilite le repérage dans le désassemblage. Il a aussi inféré correctement les types `uint8_t *` pour les deux paramètres, probablement grâce aux patterns d'accès mémoire byte-par-byte.

### Fonction derive_key à -O2

Sur `keygenme_O2`, la logique de `derive_key` est inlinée dans `main`. Mais les passes LLVM de RetDec peuvent dans certains cas reconstituer la boucle externe là où Ghidra montrait 4 blocs de code linéaire (section 20.2). Voici une reconstruction représentative du passage de dérivation dans le pseudo-code de `main` :

```c
// Address range: 0x401580 - 0x401720
void derive_key(char * username, uint32_t seed, uint8_t * out) {
    int64_t len = strlen(username);
    uint32_t state = seed;
    for (int32_t r = 0; r < 4; r++) {
        if (r != 0) {
            state = state ^ (uint32_t)r;
        }
        for (int64_t i = 0; i < len; i++) {
            uint32_t c = (uint32_t)(uint8_t)username[i];
            state = (state ^ c) << 5 | (state ^ c) >> 27;
            state += c * 0x1000193;
            state ^= state >> 16;
        }
        out[r * 4] = (uint8_t)(state >> 24);
        out[r * 4 + 1] = (uint8_t)(state >> 16);
        out[r * 4 + 2] = (uint8_t)(state >> 8);
        out[r * 4 + 3] = (uint8_t)state;
    }
}
```

Ce résultat est en réalité **meilleur** que celui de Ghidra sur ce cas précis : la boucle externe en 4 rounds est reconstituée, l'inlining de `mix_hash` et `rotate_left` est visible mais la structure reste compacte. La logique est beaucoup plus facile à comprendre d'un seul coup d'œil.

Ce n'est pas une règle générale — sur d'autres fonctions du même binaire, Ghidra peut produire un pseudo-code supérieur. C'est exactement pourquoi croiser deux décompilateurs a de la valeur.

### Le cas C++ : oop_O2

Sur le binaire C++, RetDec montre une limitation significative : sa gestion du dispatch virtuel et des conteneurs STL est moins mature que celle de Ghidra. Les appels virtuels apparaissent comme des appels indirects à travers des pointeurs de fonction, sans tentative de résolution de la vtable. Les fonctions STL template ne sont pas toujours correctement identifiées, et le pseudo-code peut contenir des casts volumineux vers des types génériques.

Pour les binaires C++ avec héritage et polymorphisme, Ghidra reste l'outil de choix. RetDec brille davantage sur le C pur et les binaires avec une logique procédurale.

---

## RetDec vs Ghidra : forces et faiblesses comparées

### Où RetDec fait mieux

**Reconstruction de boucles.** Les passes LLVM de RetDec excellent parfois à reconstituer la structure itérative originale à partir de code déroulé, comme on l'a vu avec `derive_key`. Les heuristiques de détection de boucles de LLVM bénéficient de décennies de recherche sur les compilateurs.

**Lisibilité du formatage.** RetDec produit un pseudo-code C bien indenté et formaté, avec des commentaires d'adresses, des séparateurs entre fonctions, et une convention de nommage cohérente. Le fichier `.c` de sortie est directement lisible dans n'importe quel éditeur de texte ou IDE.

**Traitement batch.** Décompiler 50 binaires en une seule boucle shell est trivial avec RetDec et impraticable avec Ghidra en mode graphique (bien que Ghidra headless le permette aussi — chapitre 8, section 9). Un pipeline d'analyse automatisée peut intégrer RetDec comme étape de pré-traitement.

**Reconnaissance d'idiomes compilateur.** RetDec reconnaît certains idiomes GCC que Ghidra ne simplifie pas toujours, notamment les divisions par constante via multiplication inverse. Là où Ghidra peut afficher `(uint64_t)x * 0xAAAAAAABull >> 33`, RetDec peut reconstruire `x / 3`.

### Où Ghidra fait mieux

**Interactivité.** C'est la différence fondamentale. Ghidra permet de renommer, retyper, restructurer, commenter, naviguer par cross-references — tout cela en temps réel, avec re-décompilation immédiate. RetDec produit un fichier statique. Si le résultat est insatisfaisant, on peut modifier les options et relancer, mais on ne peut pas guider la décompilation fonction par fonction.

**Support C++.** Ghidra gère les vtables, le RTTI, le démanglement des noms, les exceptions C++ et les conteneurs STL avec beaucoup plus de maturité. Pour un binaire G++, Ghidra est clairement supérieur.

**Écosystème de scripts et plugins.** Les scripts Ghidra (Java/Python) permettent d'automatiser des tâches de renommage, de création de types et d'annotation qui n'ont pas d'équivalent dans RetDec.

**Synchronisation désassemblage/pseudo-code.** La vue bidirectionnelle Listing ↔ Decompiler de Ghidra est irremplaçable pour le travail analytique quotidien. Avec RetDec, il faut manuellement corréler les adresses entre le fichier `.c` et le fichier `.dsm`.

**Communauté et documentation.** Ghidra, soutenu par la NSA et une large communauté, bénéficie de milliers de scripts tiers, de tutoriels, de plugins, et d'un support actif. L'écosystème RetDec est plus modeste.

### Tableau synthétique

| Critère | Ghidra Decompiler | RetDec |  
|---|---|---|  
| Mode d'utilisation | GUI interactive + headless | CLI uniquement |  
| Licence | Apache 2.0 | MIT |  
| Langage de sortie | Pseudo-C (panneau) | Fichier `.c` sur disque |  
| IR interne | P-code | LLVM IR |  
| Support C++ | Excellent (vtable, RTTI, STL) | Basique |  
| Reconstruction de boucles | Bon | Très bon |  
| Idiomes compilateur | Bon | Très bon |  
| Intégration pipeline | Via headless (lourd) | Natif CLI (léger) |  
| Correction interactive | Oui (renommage, retypage) | Non |  
| Qualité sur code C pur -O2 | Très bon | Très bon |  
| Qualité sur C++ -O2 | Très bon | Moyen |

---

## Workflow combiné RetDec + Ghidra

En pratique, les deux outils ne s'excluent pas — ils se complètent. Voici un workflow qui tire parti des forces de chacun.

**Étape 1 : décompilation rapide avec RetDec.** Lancer RetDec en ligne de commande sur le binaire cible pour obtenir une vue d'ensemble. Ouvrir le fichier `.c` dans un éditeur de texte et parcourir les fonctions. Cette étape prend quelques secondes et donne un premier aperçu de la structure du programme — nombre de fonctions, appels de bibliothèque, chaînes de caractères, constantes remarquables.

**Étape 2 : analyse interactive dans Ghidra.** Importer le même binaire dans Ghidra pour le travail d'analyse approfondi. Utiliser le pseudo-code interactif pour renommer, retyper, créer des structures, et suivre les cross-references.

**Étape 3 : croisement sur les fonctions difficiles.** Quand le pseudo-code de Ghidra est confus sur une fonction spécifique (souvent à cause d'une boucle mal reconstruite ou d'un idiome non reconnu), consulter la sortie RetDec pour la même fonction. Comparer les deux représentations permet souvent de converger vers l'interprétation correcte.

**Étape 4 : validation par le désassemblage.** En cas de doute persistant entre les deux pseudo-codes, le désassemblage dans Ghidra (vue Listing ou Function Graph) est l'arbitre final. Le code machine ne ment pas — les décompilateurs l'interprètent.

Ce workflow s'applique particulièrement bien aux binaires C compilés avec GCC à `-O2` ou `-O3`, où les deux décompilateurs ont des forces complémentaires. Sur le C++, Ghidra domine suffisamment pour que RetDec serve principalement de « second avis » ponctuel.

---

## Limitations connues de RetDec

Au-delà de la comparaison avec Ghidra, RetDec a quelques limitations propres que l'analyste doit connaître.

**Temps de décompilation sur les gros binaires.** Le pipeline LLVM est computationnellement intensif. Un binaire de quelques mégaoctets peut prendre plusieurs minutes à décompiler intégralement. L'option `--select-functions` ou `--select-ranges` est indispensable pour les gros binaires — décompiler uniquement les fonctions d'intérêt plutôt que l'ensemble.

**Pas de gestion de session persistante.** Contrairement à Ghidra qui sauvegarde le projet avec toutes les annotations, RetDec produit des fichiers de sortie sans état. Si l'on relance la décompilation, on repart de zéro. Les annotations et les corrections doivent être maintenues dans des fichiers séparés (ou dans Ghidra).

**Support DWARF limité.** RetDec exploite les informations DWARF quand elles sont présentes, mais avec moins de profondeur que Ghidra. Les types complexes (structures imbriquées, unions, types C++ templates) ne sont pas toujours correctement reconstruits depuis DWARF.

**Développement ralenti.** Depuis le transfert du projet hors d'Avast, le rythme de développement a diminué. Les issues GitHub ne sont pas toujours traitées rapidement, et certaines fonctionnalités annoncées (support amélioré du C++, meilleure gestion des exceptions) n'ont pas encore été implémentées. L'analyste doit être conscient que RetDec est un outil mature mais dont l'évolution est incertaine.

---


⏭️ [Reconstruire un fichier `.h` depuis un binaire (types, structs, API)](/20-decompilation/04-reconstruire-header.md)
