🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 20.1 — Limites de la décompilation automatique (pourquoi le résultat n'est jamais parfait)

> 📘 **Chapitre 20 — Décompilation et reconstruction du code source**  
> **Partie IV — Techniques Avancées de RE**

---

## La promesse et la réalité

L'idée de la décompilation est séduisante : donner un binaire en entrée et récupérer le code source en sortie. En pratique, le résultat ressemble au code source original de la même manière qu'une photocopie de troisième génération ressemble au document initial — la structure générale est reconnaissable, mais les détails ont souffert. Cette section explique *pourquoi* c'est structurellement inévitable, quelles informations sont perdues à chaque étape de la compilation, et quelles conséquences concrètes cela a sur le travail de l'analyste.

Comprendre ces limites n'est pas un exercice académique. C'est ce qui sépare l'analyste qui fait confiance aveuglément au pseudo-code de celui qui sait où regarder de plus près, quand le décompilateur ment, et comment corriger ses erreurs.

---

## La compilation est une fonction à sens unique

Le problème fondamental de la décompilation tient en une phrase : **la compilation détruit de l'information de manière irréversible**. Ce n'est pas un défaut de GCC — c'est le principe même du processus. Le compilateur traduit un programme écrit pour des humains en un programme écrit pour un processeur, et le processeur n'a besoin ni des noms de variables, ni des commentaires, ni de la structure logique que le développeur avait en tête.

On peut modéliser le problème ainsi : si la compilation est une fonction `f(source) → binaire`, alors la décompilation tente de calculer `f⁻¹(binaire) → source`. Mais `f` n'est pas injective — des sources différentes peuvent produire le même binaire. Il n'existe donc pas de fonction inverse unique. Le décompilateur doit *choisir* parmi plusieurs reconstructions possibles, et ce choix est nécessairement heuristique.

### Ce que le compilateur détruit

Voici un inventaire concret de ce qui disparaît entre le fichier `.c` / `.cpp` et le binaire ELF final. Chaque élément de cette liste est une source de divergence entre le pseudo-code décompilé et le code source original.

**Noms de variables locales et de paramètres.** Le compilateur assigne les variables locales à des registres ou à des emplacements sur la pile. Une fois cette allocation faite, le nom d'origine (`counter`, `user_input`, `remaining_bytes`) n'existe plus. Le décompilateur les remplace par des noms générés : `iVar1`, `local_28`, `param_1`. Avec les symboles DWARF (`-g`), ces noms sont conservés dans une section de debug — mais un binaire distribué en production est presque toujours strippé.

**Noms de types définis par l'utilisateur.** Les `typedef`, les noms de `struct`, d'`enum` et de `class` n'existent plus dans le binaire. Le décompilateur voit des accès mémoire à des offsets (`*(int *)(param_1 + 0x18)`) et doit deviner qu'il s'agit d'un champ d'une structure. Il peut reconstituer un `struct` anonyme à partir du pattern d'accès, mais il ne retrouvera jamais le nom `license_ctx_t` ni le nom du champ `expected_key`.

**Commentaires.** C'est l'évidence, mais il faut le dire : les commentaires disparaissent dès le préprocesseur. Aucun décompilateur ne peut les recréer. L'intention du développeur — pourquoi ce calcul est fait de cette manière, pourquoi cette valeur est choisie — est perdue.

**Macros et constantes préprocesseur.** Les `#define` sont résolus par le préprocesseur avant même que le compilateur ne voie le code. Dans le binaire, `MAGIC_SEED` n'existe pas — il n'y a que la valeur littérale `0xDEADBEEF`. Le décompilateur affiche `0xdeadbeef` et c'est à l'analyste de reconnaître qu'il s'agit d'une constante nommée.

**Structure du code source.** L'organisation en fichiers multiples (`.c`, `.h`), en modules, en unités de compilation séparées — tout cela disparaît dans le binaire lié. Le décompilateur ne sait pas que `mix_hash()` était définie dans `keygenme.c` et que `proto_checksum()` venait de `protocol.h`. Tout est aplati dans un espace de fonctions unique.

**Ordre des fonctions et des déclarations.** GCC peut réordonner les fonctions dans la section `.text` selon ses heuristiques d'optimisation (notamment avec `-freorder-functions`). L'ordre dans lequel les fonctions apparaissent dans le désassemblage ne correspond pas nécessairement à l'ordre dans le fichier source.

**Informations de type fin.** Sur x86-64 Linux (modèle LP64), `int` fait 4 octets et `long` en fait 8. Mais quand un `int` est chargé dans un registre 64 bits (via `movsx` ou `movzx`), la distinction entre `int32_t` et `int64_t` dans le pseudo-code dépend d'une heuristique du décompilateur, qui peut se tromper. La distinction entre `signed` et `unsigned` ne se voit que par le choix des instructions de comparaison (`jl` vs `jb`, `sar` vs `shr`), et le décompilateur peut se tromper. La distinction entre un `char *` pointant vers une chaîne et un `uint8_t *` pointant vers un buffer binaire est invisible.

---

## L'impact des optimisations : quand le code se transforme

Si la perte d'informations symboliques est un problème constant, les optimisations du compilateur ajoutent une couche de difficulté variable. Plus le niveau d'optimisation est élevé, plus le code machine s'éloigne de la structure logique du source.

### -O0 : le cas favorable

À `-O0`, GCC produit un code machine qui suit fidèlement la structure du source. Chaque variable locale a son emplacement sur la pile, chaque appel de fonction est réellement un `call`, chaque expression est évaluée dans l'ordre écrit. Le décompilateur produit un pseudo-code qui ressemble fortement au source original, à la perte des noms près.

C'est le niveau idéal pour apprendre la décompilation, et c'est pourquoi nos binaires d'entraînement sont fournis en variante `_O0_dbg`. Mais c'est aussi le niveau le moins réaliste : personne ne distribue un binaire compilé en `-O0` en production.

### -O2 : le cas courant

À `-O2`, GCC applique des dizaines de passes d'optimisation qui transforment profondément le code. Voici les plus problématiques pour la décompilation.

**Inlining de fonctions.** Une fonction courte comme `rotate_left()` dans notre `keygenme.c` peut être intégrée directement dans le corps de `mix_hash()`. Dans le pseudo-code, la fonction n'apparaît plus comme un appel séparé — son code est fusionné avec celui de l'appelant. Le décompilateur montre un bloc de code plus long sans frontières claires, et l'analyste doit reconnaître manuellement le pattern de la rotation.

**Réordonnancement des instructions.** GCC réorganise les instructions pour optimiser le pipeline du processeur. Les calculs qui étaient séquentiels dans le source peuvent être entrelacés dans le binaire. Le décompilateur tente de les remettre en ordre logique, mais le résultat peut différer significativement de l'original.

**Élimination de code mort et propagation de constantes.** Si GCC détermine qu'une branche n'est jamais prise ou qu'une variable a toujours la même valeur, il supprime le code correspondant. Le décompilateur ne peut pas reconstituer du code qui n'existe tout simplement plus dans le binaire.

**Remplacement d'idiomes.** GCC transforme certaines constructions en idiomes machine plus efficaces. Une division par une constante devient une multiplication par l'inverse modulaire suivie d'un shift. Un `switch` sur des valeurs consécutives devient une table de sauts. Une boucle `for` simple peut être transformée en une boucle déroulée avec des instructions SIMD. Le décompilateur tente de reconnaître ces patterns et de les retransformer en constructions de haut niveau, mais il n'y arrive pas toujours — et quand il y arrive, le résultat peut prendre une forme différente de l'original.

**Strength reduction.** Une multiplication dans une boucle (`i * 4`) peut être remplacée par une addition incrémentale (`ptr += 4`). Le décompilateur montre de l'arithmétique de pointeurs là où le source utilisait un index.

### -O3 : le cas difficile

À `-O3`, GCC va encore plus loin avec la vectorisation automatique (instructions SSE/AVX), le déroulage agressif de boucles, et des transformations de boucles plus complexes (fusion, fission, échange de niveaux). Le pseudo-code résultant peut être méconnaissable par rapport au source : une boucle simple de 3 lignes peut devenir un bloc de 30 lignes opérant sur des registres `xmm` avec des opérations packed.

### Tableau récapitulatif

| Élément du source | -O0 | -O2 | -O3 |  
|---|---|---|---|  
| Structure des fonctions | Préservée | Modifiée (inlining partiel) | Très modifiée (inlining agressif) |  
| Boucles | Reconnaissables | Réorganisées, partiellement déroulées | Déroulées, vectorisées, fusionnées |  
| Variables locales | Sur la pile, identifiables | En registres, parfois fusionnées | En registres SIMD, méconnaissables |  
| Branchements conditionnels | Fidèles au source | Réordonnés, parfois éliminés | Convertis en `cmov`, prédication |  
| Appels de fonctions | Tous présents | Petites fonctions inlinées | Inlining agressif, tail calls |  
| Expressions arithmétiques | Directes | Strength reduction, idiomes | Vectorisées, réassociées |

---

## Les ambiguïtés structurelles

Au-delà de la perte d'information et des optimisations, certaines constructions du langage produisent un code machine ambigu que le décompilateur ne peut pas résoudre avec certitude.

### if/else vs opérateur ternaire vs cmov

Le code machine pour `if (x > 0) a = 1; else a = 0;`, pour `a = (x > 0) ? 1 : 0;`, et pour l'instruction `cmov` (conditional move) produite par GCC en `-O2` est souvent identique. Le décompilateur doit choisir une représentation, et ce choix est arbitraire.

### for vs while vs do-while

Une boucle `for (int i = 0; i < n; i++)` et une boucle `while` équivalente produisent le même code machine. En `-O2`, GCC transforme souvent les boucles `for` en boucles `do-while` avec un test préalable (loop inversion), ce qui modifie la structure de contrôle visible dans le pseudo-code. Le décompilateur peut afficher un `do { ... } while(...)` là où le source avait un `for`.

### switch vs chaîne de if/else

Un `switch` avec des valeurs dispersées peut être compilé en une série de comparaisons successives, identique à une chaîne de `if/else if`. Inversement, une chaîne de `if/else if` sur des valeurs consécutives peut être optimisée en jump table par GCC. Le décompilateur reconstitue la structure qu'il juge la plus probable, pas nécessairement celle du source.

### Structures vs variables séparées

Si une `struct` de trois champs `int` est passée par copie et que GCC la décompose en trois registres (ce qui est légal avec l'ABI System V AMD64 pour les petites structures), le décompilateur ne voit que trois paramètres `int` séparés. La structure originale est invisible.

---

## Les erreurs actives du décompilateur

Le décompilateur ne se contente pas de perdre de l'information — il peut aussi en *inventer* de la mauvaise. Ce ne sont pas des bugs au sens strict, mais des heuristiques qui échouent dans certains cas.

**Mauvaise inférence de types.** Le décompilateur peut interpréter un `uint32_t` comme un `int`, un pointeur comme un entier, ou un `float` stocké dans un registre XMM comme un entier 128 bits. C'est particulièrement fréquent avec les unions et les réinterprétations de type (`memcpy` entre types différents, casts de pointeurs).

**Reconstruction de signatures incorrecte.** Sans symboles, le décompilateur doit deviner le nombre et le type des paramètres d'une fonction. S'il se trompe sur le nombre de paramètres (par exemple en ne détectant pas que `rdx` est un troisième argument), le pseudo-code de toutes les fonctions appelantes sera faux aussi — l'erreur se propage.

**Faux flux de contrôle.** Des patterns comme le tail call optimization ou le code auto-modifiant (rare dans du code GCC standard, fréquent dans du code obfusqué) peuvent tromper l'algorithme de reconstruction du graphe de flux de contrôle. Le décompilateur peut alors afficher des `goto` inexplicables, des boucles infinies factices, ou fusionner deux fonctions distinctes en une seule.

**Faux positifs sur les structures.** Le décompilateur peut regrouper des variables locales adjacentes sur la pile en une fausse structure, simplement parce que leurs offsets sont contigus. Inversement, il peut éclater une vraie structure en variables séparées s'il ne détecte pas le pattern d'accès.

---

## Conséquences pratiques pour l'analyste

Ces limites ne sont pas des raisons d'éviter la décompilation — c'est un outil extraordinairement puissant malgré ses défauts. Mais elles imposent une discipline de travail.

**Toujours croiser avec le désassemblage.** Le pseudo-code est un point de départ, pas une vérité. Quand un passage semble incohérent dans le décompilateur, basculer vers la vue Listing de Ghidra pour lire les instructions réelles permet souvent de comprendre ce que le décompilateur a mal interprété.

**Retyper et renommer au fur et à mesure.** Chaque fois que l'analyste identifie le vrai type d'une variable ou le vrai rôle d'une fonction, il doit le corriger dans le décompilateur. Ces corrections s'accumulent et améliorent progressivement la qualité du pseudo-code — y compris dans les fonctions voisines, grâce à la propagation des types.

**Commencer par -O0 quand c'est possible.** Si le binaire d'entraînement est disponible en plusieurs niveaux d'optimisation (c'est le cas dans cette formation), commencer l'analyse par la variante `-O0` permet de comprendre la logique avant de s'attaquer à la version optimisée.

**Identifier les patterns du compilateur.** Reconnaître qu'une multiplication bizarre est en réalité une division par constante, qu'un bloc de `xmm` est une boucle vectorisée, ou qu'un `goto` est un tail call — c'est le savoir-faire qui compense les limites du décompilateur. Le chapitre 16 (Optimisations du compilateur) et l'Annexe I (Patterns GCC) sont des ressources directement applicables ici.

**Ne jamais présumer que le pseudo-code est complet.** Du code éliminé par le compilateur (branches mortes, assertions de debug, fonctions inlinées) n'apparaîtra pas dans le pseudo-code puisqu'il n'est pas dans le binaire. L'absence d'une vérification dans le pseudo-code ne signifie pas qu'elle n'existait pas dans le source.

---

## Ce que la décompilation fait bien malgré tout

Il serait injuste de terminer cette section sans reconnaître ce que les décompilateurs modernes réussissent remarquablement bien, en particulier Ghidra et IDA sur du code GCC.

La **reconstruction des structures de contrôle** (boucles, conditions, switch) est fiable dans la grande majorité des cas sur du code non obfusqué. La **propagation des types** à travers les appels de fonction fonctionne bien quand les signatures sont correctes. La **reconnaissance des appels de bibliothèque standard** (via PLT/GOT) est quasiment parfaite — le décompilateur affiche `printf(format, ...)` et non `call [rip+0x2f4a]`. Et la **navigation par cross-references** dans le pseudo-code est un multiplicateur de productivité sans équivalent dans le désassemblage brut.

La décompilation est un outil imparfait mais indispensable. Les sections suivantes montrent comment en tirer le meilleur parti.

---


⏭️ [Ghidra Decompiler — qualité selon le niveau d'optimisation](/20-decompilation/02-ghidra-decompiler.md)
