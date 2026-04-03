🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.5 — Reconnaître les structures GCC : vtables C++, RTTI, exceptions

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Pourquoi cette section ?

Jusqu'ici, les exemples de ce chapitre se sont appuyés principalement sur du code C, où le désassemblage se traduit relativement directement en structures de contrôle et en appels de fonctions. Le C++ compilé avec GCC/G++ introduit un niveau de complexité supplémentaire : le compilateur génère des **métadonnées structurelles** invisibles dans le code source mais omniprésentes dans le binaire — vtables, RTTI, tables d'exceptions. Ces artefacts ne sont pas du code exécutable au sens habituel : ce sont des **tables de données** insérées dans des sections spécifiques, que le runtime C++ utilise pour implémenter le polymorphisme, l'identification dynamique de types et le déroulage de pile lors des exceptions.

Pour un analyste qui ne les reconnaît pas, ces structures apparaissent comme des blocs de données opaques parsemés de pointeurs. Pour un analyste qui les maîtrise, elles sont une mine d'informations : elles révèlent la hiérarchie de classes, les noms de types (même dans un binaire partiellement strippé), et l'architecture des gestionnaires d'erreurs.

Cette section vous apprend à les identifier et les interpréter dans Ghidra. Le Chapitre 17 approfondira ces concepts avec une analyse systématique ; ici, l'objectif est de vous donner les clés de reconnaissance pour ne pas être dérouté quand vous les rencontrerez dans vos analyses.

> 💡 **Binaire de référence** — Les exemples de cette section utilisent le binaire `ch08-oop_O0` (C++ compilé sans optimisation, avec symboles). Compilez-le si ce n'est pas déjà fait :  
> ```bash  
> cd binaries/ch08-oop/  
> make all  
> ```

---

## Le modèle objet C++ de GCC : vue d'ensemble

GCC implémente le modèle objet C++ selon la spécification **Itanium C++ ABI**, qui est le standard de fait sur Linux, macOS et la plupart des plateformes Unix. Cette ABI définit précisément comment les concepts du langage C++ sont traduits en structures binaires : disposition mémoire des objets, encodage des noms de symboles (name mangling), format des vtables, format du RTTI, et mécanisme de gestion des exceptions.

Trois artefacts de l'Itanium ABI sont particulièrement importants pour le reverse engineering :

1. **Les vtables** (virtual tables) — tables de pointeurs de fonctions qui implémentent le dispatch virtuel (polymorphisme).  
2. **Le RTTI** (Run-Time Type Information) — métadonnées qui décrivent les types et les relations d'héritage, utilisées par `dynamic_cast` et `typeid`.  
3. **Les tables d'exceptions** — structures qui décrivent les blocs `try`/`catch`, les fonctions de nettoyage (cleanup), et les types d'exceptions attendus.

Ces trois artefacts sont interconnectés : une vtable contient un pointeur vers le RTTI de sa classe, et les tables d'exceptions référencent le RTTI pour savoir quel type d'exception un `catch` peut intercepter.

---

## Les vtables

### Qu'est-ce qu'une vtable ?

Quand une classe C++ déclare au moins une méthode `virtual`, GCC génère une **vtable** (virtual method table) pour cette classe. La vtable est un tableau de pointeurs de fonctions stocké dans la section `.rodata` (ou `.data.rel.ro` si les pointeurs nécessitent une relocation). Chaque entrée de la vtable pointe vers l'implémentation concrète d'une méthode virtuelle pour cette classe.

À l'exécution, chaque instance d'une classe avec des méthodes virtuelles contient un pointeur caché — le **vptr** (virtual pointer) — situé au tout début de l'objet en mémoire (offset 0). Ce vptr pointe vers la vtable de la classe réelle de l'objet. Quand le code appelle une méthode virtuelle, il suit le vptr pour trouver la vtable, puis indexe dans la vtable pour obtenir l'adresse de la méthode à appeler. C'est ce mécanisme d'indirection qui permet le polymorphisme.

### Anatomie d'une vtable GCC

La structure d'une vtable selon l'Itanium ABI est la suivante (pour une classe à héritage simple) :

```
Offset    Contenu
──────    ───────────────────────────────────────
-0x10     offset-to-top (0 pour la classe de base)
-0x08     pointeur vers le RTTI de la classe
 0x00  ←  adresse pointée par le vptr
          pointeur vers la 1ère méthode virtuelle
+0x08     pointeur vers la 2ème méthode virtuelle
+0x10     pointeur vers la 3ème méthode virtuelle
  ...     ...
```

Le vptr de l'objet pointe vers l'**offset 0x00** de cette structure — c'est-à-dire vers le premier pointeur de méthode. Les deux entrées à offsets négatifs (offset-to-top et pointeur RTTI) précèdent le point d'entrée de la vtable. C'est un détail crucial : quand vous examinez une vtable dans Ghidra, le pointeur RTTI se trouve **avant** les pointeurs de méthodes, à l'adresse `vtable - 8`.

L'**offset-to-top** vaut 0 pour une classe qui n'est pas une base secondaire dans un héritage multiple. Pour l'héritage multiple, il indique le décalage à appliquer au pointeur `this` pour retrouver le début de l'objet complet. Dans un premier temps, vous pouvez l'ignorer — il vaut 0 dans la grande majorité des cas simples.

### Reconnaître une vtable dans Ghidra

Dans Ghidra, les vtables apparaissent dans le Listing comme des séquences de **pointeurs alignés** dans `.rodata` ou `.data.rel.ro`. Si le binaire a des symboles, Ghidra les labellise avec le préfixe `vtable for` suivi du nom de la classe (ou le symbole manglé `_ZTV`). Si le binaire est strippé, vous verrez une séquence de `addr` (pointeurs) sans label explicite.

Voici comment une vtable typique apparaît dans le Listing de Ghidra pour une classe `Dog` qui hérite de `Animal` :

```
                     vtable for Dog
.rodata:00402040     addr       0x0                      ; offset-to-top
.rodata:00402048     addr       typeinfo for Dog          ; pointeur RTTI
.rodata:00402050     addr       Dog::speak               ; 1ère méthode virtuelle
.rodata:00402058     addr       Dog::~Dog                ; destructeur virtuel (complete)
.rodata:00402060     addr       Dog::~Dog                ; destructeur virtuel (deleting)
```

Quelques observations :

**Deux destructeurs** — GCC génère systématiquement deux entrées pour le destructeur virtuel : le *complete object destructor* (qui détruit l'objet mais ne libère pas la mémoire) et le *deleting destructor* (qui détruit l'objet et appelle `operator delete`). C'est un artefact de l'Itanium ABI que vous verrez dans chaque vtable de classe avec un destructeur virtuel. Les symboles manglés correspondants sont `_ZN3DogD1Ev` (D1, complete) et `_ZN3DogD0Ev` (D0, deleting).

**Méthodes héritées** — Si `Dog` ne redéfinit pas une méthode virtuelle héritée de `Animal`, la vtable de `Dog` contiendra un pointeur vers l'implémentation de `Animal`. En comparant les vtables de la classe de base et de la classe dérivée, vous pouvez identifier quelles méthodes sont surchargées (les pointeurs diffèrent) et lesquelles sont héritées telles quelles (les pointeurs sont identiques).

**Classes abstraites** — Si une méthode virtuelle est pure (`= 0`), l'entrée correspondante dans la vtable pointe vers `__cxa_pure_virtual`, une fonction du runtime C++ qui affiche une erreur et termine le programme. La présence de cette adresse dans une vtable signale immédiatement une classe abstraite.

### Identifier les vtables dans un binaire strippé

Sans symboles, les vtables ne sont pas labellisées. Mais elles restent identifiables par leurs caractéristiques structurelles :

- Elles se trouvent dans `.rodata` ou `.data.rel.ro`.  
- Elles commencent par un entier 0 (ou un petit entier négatif pour l'héritage multiple) suivi d'un pointeur vers `.rodata` (le RTTI).  
- Elles contiennent une séquence de pointeurs vers `.text` (les méthodes virtuelles).  
- Les constructeurs détectés dans `.text` contiennent du code qui écrit un pointeur constant (le vptr) au début de l'objet `this`. Cherchez des patterns comme `MOV QWORD PTR [RDI], 0x402050` dans les constructeurs — la constante est l'adresse de la vtable.

Dans Ghidra, la stratégie la plus efficace pour trouver les vtables dans un binaire strippé est de localiser les constructeurs (qui initialisent le vptr) et de suivre les pointeurs. Les constructeurs sont souvent identifiables car ils appellent `operator new` ou sont appelés avec un objet fraîchement alloué, et ils écrivent systématiquement une constante au début de l'objet.

### Annoter les vtables dans Ghidra

Une fois une vtable identifiée, vous pouvez l'annoter efficacement :

1. **Créer un label** — Placez le curseur sur la première entrée de méthode (pas sur l'offset-to-top) et créez un label (`L`) nommé par convention `vtable_ClassName` ou `vftable_ClassName`.

2. **Typer les entrées** — Chaque entrée est un pointeur de fonction. Appliquez le type `pointer` à chaque slot pour que Ghidra affiche les adresses cibles comme des références navigables.

3. **Créer un commentaire Plate** — Ajoutez un plate comment au-dessus de la vtable avec l'index des méthodes :
   ```
   vtable for Dog
   [0] speak()
   [1] ~Dog() (complete)
   [2] ~Dog() (deleting)
   ```

4. **Créer une structure** — Pour les projets d'envergure, créez un type `struct vtable_Dog` dans le Data Type Manager avec un champ pointeur de fonction pour chaque slot. Appliquez cette structure à l'adresse de la vtable pour obtenir un affichage nommé.

---

## Le RTTI (Run-Time Type Information)

### Qu'est-ce que le RTTI ?

Le RTTI est un ensemble de métadonnées que GCC insère dans le binaire pour chaque classe polymorphe (c'est-à-dire chaque classe ayant au moins une méthode virtuelle). Ces métadonnées permettent au runtime C++ d'effectuer des vérifications de type dynamiques via `dynamic_cast` et `typeid`.

Le RTTI est l'une des sources d'information les plus précieuses en reverse engineering de binaires C++, car il contient les **noms des classes en clair**, même dans un binaire strippé. Le stripping supprime la table `.symtab` (les noms de fonctions et de variables), mais le RTTI est stocké dans `.rodata` et fait partie de la sémantique du programme — le supprimer casserait `dynamic_cast` et `typeid`. La seule façon de le supprimer est de compiler avec `-fno-rtti`, ce qui est relativement rare dans les programmes C++ standards (cela empêche l'utilisation de `dynamic_cast` et des `catch` par type).

### Structure du RTTI selon l'Itanium ABI

Le RTTI est implémenté comme une hiérarchie de structures `type_info`. L'Itanium ABI définit plusieurs classes dérivées de `type_info` selon le type de classe :

**`__class_type_info`** — pour les classes sans base (classes racines de la hiérarchie). Structure :

```
Offset    Champ                    Contenu
──────    ─────                    ───────
0x00      vtable ptr               pointeur vers la vtable de __class_type_info
0x08      __type_name              pointeur vers la chaîne de nom manglé de la classe
```

**`__si_class_type_info`** — pour les classes à héritage simple non-virtuel (*si* = single inheritance). Structure :

```
Offset    Champ                    Contenu
──────    ─────                    ───────
0x00      vtable ptr               pointeur vers la vtable de __si_class_type_info
0x08      __type_name              pointeur vers le nom manglé
0x10      __base_type              pointeur vers le type_info de la classe parente
```

**`__vmi_class_type_info`** — pour les cas d'héritage multiple ou virtuel (*vmi* = virtual/multiple inheritance). Structure plus complexe contenant un tableau de descripteurs de bases.

### Reconnaître le RTTI dans Ghidra

Si le binaire a des symboles, Ghidra labellise les structures RTTI avec le préfixe `typeinfo for` (ou le symbole manglé `_ZTI`) et les chaînes de noms avec `typeinfo name for` (ou `_ZTS`).

Dans le Listing, le RTTI d'une classe `Dog` héritant de `Animal` ressemble à ceci :

```
                     typeinfo name for Dog
.rodata:00402100     ds         "3Dog"                   ; nom manglé (longueur + nom)

                     typeinfo for Dog
.rodata:00402108     addr       vtable for __si_class_type_info + 0x10
.rodata:00402110     addr       typeinfo name for Dog     ; pointeur vers "3Dog"
.rodata:00402118     addr       typeinfo for Animal       ; pointeur vers le RTTI parent
```

Le format du nom manglé est compact : `3Dog` signifie « une chaîne de 3 caractères : Dog ». Pour un namespace imbriqué, vous verriez par exemple `N4Game6PlayerE` (namespace `Game`, classe `Player`). Le démanglement suit les règles Itanium que nous avons abordées avec `c++filt` au Chapitre 7.

### Exploiter le RTTI pour reconstruire la hiérarchie de classes

Le RTTI est la clé de voûte de la reconstruction de la hiérarchie de classes dans un binaire C++. La procédure est la suivante :

**Lister tous les RTTI** — Dans Ghidra, utilisez **Search → For Strings** et cherchez des patterns de noms manglés. Les noms de typeinfo sont des chaînes courtes avec le format `<longueur><nom>`. Alternativement, cherchez les références à `__si_class_type_info` ou `__class_type_info` dans le Listing — chaque structure RTTI pointe vers la vtable de l'un de ces types.

**Suivre les pointeurs de base** — Chaque `__si_class_type_info` contient un pointeur vers le RTTI de sa classe parente. En suivant ces pointeurs, vous reconstruisez les chaînes d'héritage. Les `__class_type_info` (sans pointeur de base) sont les racines de la hiérarchie.

**Lier RTTI et vtables** — Chaque vtable contient un pointeur vers le RTTI de sa classe (à l'offset -8 par rapport au point d'entrée de la vtable). En partant d'une vtable, vous pouvez atteindre le RTTI et donc le nom de la classe. En partant du RTTI, vous pouvez retrouver les vtables en cherchant les cross-references vers le RTTI.

Dans Ghidra, cette exploration se fait efficacement par cross-references :

1. Localisez un RTTI (par exemple via une chaîne de nom).  
2. Appuyez sur `X` pour voir toutes les références vers ce RTTI.  
3. Parmi les références, identifiez celle qui provient d'une vtable (dans `.rodata` ou `.data.rel.ro`, à l'offset -8 d'une séquence de pointeurs vers `.text`).  
4. La vtable vous donne la liste des méthodes virtuelles.  
5. Le RTTI vous donne le nom de la classe et le lien vers la classe parente.

> 💡 **Astuce pour les binaires strippés** — Même sans symboles, le RTTI permet de retrouver les noms des classes. C'est souvent le premier réflexe quand on ouvre un binaire C++ strippé : chercher les chaînes correspondant aux typeinfo names. Si le binaire a été compilé avec `-fno-rtti`, cette information est absente, mais c'est un cas peu fréquent — vous pouvez le détecter par l'absence totale de références aux vtables de `__class_type_info` et `__si_class_type_info`.

### Résumé visuel : lien entre objet, vtable et RTTI

La relation entre un objet en mémoire, sa vtable et son RTTI peut se résumer ainsi :

```
Objet en mémoire (heap/stack)
┌───────────────────┐
│ vptr ─────────────────────┐
│ champ_1           │       │
│ champ_2           │       │
│ ...               │       │
└───────────────────┘       │
                            ▼
Vtable (.rodata)            
┌──────────────────┐       
│ offset-to-top (0)        
│ ptr RTTI ──────────────────┐
├──────────────────┤  ← vptr pointe ici
│ ptr méthode_1    │         │
│ ptr méthode_2    │         │
│ ptr méthode_3    │         │
└──────────────────┘         │
                             ▼
RTTI (.rodata)
┌──────────────────┐
│ ptr vtable_type_info 
│ ptr nom ("3Dog") │
│ ptr RTTI_parent ─────────→ RTTI de Animal
└──────────────────┘
```

---

## Les tables d'exceptions

### Mécanisme des exceptions C++ sous GCC

La gestion des exceptions en C++ (`throw`, `try`, `catch`) est implémentée par GCC selon le modèle **zero-cost exceptions** (aussi appelé *table-driven*). Ce modèle repose sur le principe suivant : en l'absence d'exception, le code ne paie aucun coût supplémentaire à l'exécution (pas de registre sauvegardé, pas de test de flag). Le coût n'est payé que lorsqu'une exception est effectivement lancée. En contrepartie, le compilateur génère des **tables de métadonnées** qui décrivent comment dérouler la pile et quels gestionnaires `catch` invoquer.

Ces métadonnées sont réparties dans deux sections ELF :

- **`.eh_frame`** — contient les *Call Frame Information* (CFI), qui décrivent comment restaurer l'état des registres pour chaque point du programme. C'est l'information que le dérouleur de pile (unwinder) utilise pour remonter la chaîne d'appels frame par frame. Cette section existe même dans du code C (elle est utilisée par les backtraces), mais elle est essentielle pour les exceptions C++.

- **`.gcc_except_table`** — contient les *Language-Specific Data Areas* (LSDA), spécifiques au C++. Chaque fonction qui contient un `try`/`catch` ou des objets locaux avec des destructeurs a une LSDA qui décrit les régions protégées (*call sites*), les actions à exécuter (appeler un handler `catch` ou un cleanup/destructeur), et les types d'exceptions attendus.

### Reconnaître les tables d'exceptions dans Ghidra

Ghidra parse automatiquement `.eh_frame` lors de l'analyse (via l'analyseur **GCC Exception Handlers** mentionné en section 8.2). Le résultat est visible de plusieurs manières :

**Dans le Symbol Tree** — Les fonctions de type *personality routine* apparaissent dans les imports. La plus courante est `__gxx_personality_v0`, qui est la routine de personnalité du C++ pour GCC. Sa présence dans les imports confirme que le binaire utilise des exceptions C++.

**Dans le Listing** — Les fonctions qui lancent des exceptions contiennent des appels à `__cxa_allocate_exception` (allocation de l'objet exception), `__cxa_throw` (lancement de l'exception) et éventuellement `__cxa_begin_catch` / `__cxa_end_catch` (entrée et sortie d'un bloc `catch`).

Un pattern typique de `throw` dans le Listing :

```asm
mov     edi, 0x8                    ; taille de l'exception (sizeof(std::runtime_error))  
call    __cxa_allocate_exception    ; alloue l'objet exception  
; ... initialisation de l'objet exception ...
mov     edx, offset _ZNSt13runtime_errorD1Ev  ; destructeur  
mov     esi, offset _ZTISt13runtime_error     ; typeinfo (RTTI) de l'exception  
mov     rdi, rax                    ; pointeur vers l'objet exception  
call    __cxa_throw                 ; lance l'exception (ne retourne jamais)  
```

Notez que `__cxa_throw` prend en paramètre le **RTTI** du type d'exception — c'est ainsi que le runtime sait quel bloc `catch` peut l'intercepter.

**Dans le Decompiler** — Ghidra représente les exceptions dans le pseudo-code, mais la qualité de la représentation varie. Dans les meilleurs cas, vous verrez des appels explicites à `__cxa_throw` avec les paramètres identifiables. Le décompileur ne reconstruit pas les blocs `try`/`catch` sous leur forme syntaxique C++, mais le flux de contrôle montre clairement les chemins normal et exceptionnel.

### Les cleanup handlers (destructeurs locaux)

Au-delà des `catch` explicites, les tables d'exceptions servent aussi à garantir que les destructeurs des objets locaux sont appelés lors du déroulage de pile. Si une fonction contient des variables locales de types avec destructeurs (comme `std::string`, `std::vector`, `std::unique_ptr`, ou toute classe avec un destructeur non trivial), le compilateur génère des *cleanup handlers* dans la LSDA.

Dans Ghidra, ces cleanup handlers apparaissent comme des blocs de code à la fin de la fonction qui appellent des destructeurs. Ils ne sont pas atteignables par le flux de contrôle normal (vous ne verrez pas de saut conditionnel qui y mène dans le graphe de flux standard). Ils sont uniquement invoqués par le mécanisme de déroulage de pile.

Si vous voyez des blocs de code apparemment « orphelins » à la fin d'une fonction C++ dans le Function Graph — du code qui appelle des destructeurs mais qui n'est relié à aucun bloc par une arête — ce sont très probablement des cleanup handlers pour les exceptions.

### Ce qu'il faut retenir pour l'analyse pratique

Les tables d'exceptions sont complexes dans leur format interne, mais leur exploitation en reverse engineering se résume à quelques observations clés :

- **La présence de `__gxx_personality_v0`** dans les imports confirme que le binaire utilise des exceptions C++.  
- **Les appels à `__cxa_throw`** marquent les points de lancement d'exceptions. Le deuxième paramètre est le RTTI du type lancé.  
- **Les appels à `__cxa_begin_catch` / `__cxa_end_catch`** délimitent les blocs `catch`.  
- **Les blocs orphelins** dans le Function Graph sont des cleanup handlers.  
- **La section `.gcc_except_table`** peut être inspectée manuellement dans le Listing pour les cas avancés, mais ce n'est généralement pas nécessaire pour une analyse fonctionnelle — les informations utiles sont visibles dans le code décompilé.

---

## Reconnaissance rapide : résumé des indices dans Ghidra

Voici un tableau récapitulatif des indicateurs qui signalent la présence de ces structures GCC dans un binaire ouvert dans Ghidra :

| Indicateur | Ce qu'il signale | Où le trouver |  
|---|---|---|  
| Symboles `_ZTV*` ou labels `vtable for *` | Vtables — le binaire contient des classes polymorphes | Symbol Tree → Labels, ou Listing dans `.rodata` |  
| Symboles `_ZTI*` ou labels `typeinfo for *` | RTTI — métadonnées de type disponibles | Symbol Tree → Labels, ou Listing dans `.rodata` |  
| Symboles `_ZTS*` ou labels `typeinfo name for *` | Noms de classes en clair | Defined Strings, ou Listing dans `.rodata` |  
| Chaînes au format `<n><nom>` (`3Dog`, `6Animal`) | Noms manglés de typeinfo | Search → For Strings |  
| Import `__gxx_personality_v0` | Le binaire utilise des exceptions C++ | Symbol Tree → Imports |  
| Imports `__cxa_throw`, `__cxa_begin_catch` | Code qui lance / attrape des exceptions | Symbol Tree → Imports |  
| Import `__cxa_pure_virtual` | Au moins une classe abstraite (méthode virtuelle pure) | Symbol Tree → Imports |  
| Imports `__dynamic_cast` | Le code utilise `dynamic_cast` | Symbol Tree → Imports |  
| Sections `.eh_frame`, `.gcc_except_table` | Tables d'exceptions présentes | Program Trees |  
| Écriture de constante à `[RDI+0]` dans un constructeur | Initialisation du vptr — la constante est l'adresse de la vtable | Listing / Decompiler, dans les fonctions constructeurs |  
| Références à `__si_class_type_info`, `__vmi_class_type_info` | Structures RTTI d'héritage simple / multiple | Listing dans `.rodata`, via XREF |

---

## Workflow d'analyse d'un binaire C++ dans Ghidra

En combinant les techniques décrites ci-dessus, voici un workflow efficace pour aborder un binaire C++ dans Ghidra :

**1. Vérifier la présence de C++** — Regardez les imports dans le Symbol Tree. La présence de `__gxx_personality_v0`, `__cxa_throw`, `operator new`, `__dynamic_cast`, ou de symboles dans le namespace `std::` confirme que le binaire est du C++.

**2. Localiser les vtables** — Cherchez les symboles `_ZTV` ou les labels `vtable for` dans le Symbol Tree. En l'absence de symboles, cherchez dans `.rodata` les séquences de pointeurs vers `.text` précédées d'un pointeur vers `.rodata` (le RTTI) et d'un zéro (l'offset-to-top).

**3. Extraire les noms de classes depuis le RTTI** — Cherchez les chaînes de typeinfo names. Même dans un binaire strippé, ces chaînes sont présentes si le RTTI n'a pas été désactivé. Listez toutes les classes détectées.

**4. Reconstruire la hiérarchie d'héritage** — Suivez les pointeurs `__base_type` dans les structures `__si_class_type_info` pour remonter les chaînes d'héritage. Dessinez la hiérarchie (même sur papier) pour avoir une vue d'ensemble.

**5. Identifier les méthodes de chaque classe** — Pour chaque vtable, notez les pointeurs de méthodes. Naviguez vers chaque méthode et renommez-la avec le format `ClassName::method_purpose`. Comparez les vtables parent et enfant pour distinguer les méthodes surchargées des méthodes héritées.

**6. Localiser les constructeurs et destructeurs** — Les constructeurs initialisent le vptr (écriture d'une constante au début de `this`) et appellent les constructeurs parents. Les destructeurs suivent le pattern inverse : ils réinitialisent le vptr vers la vtable de la classe courante (pas celle de la classe dérivée), appellent les destructeurs des membres, puis le destructeur parent.

**7. Créer les types dans le Data Type Manager** — Pour chaque classe identifiée, créez une structure dans Ghidra avec les champs déduits. Le premier champ est toujours le vptr (type `pointer`), suivi des champs de données de la classe. Appliquez ces structures aux paramètres `this` des méthodes.

Ce workflow sera mis en pratique de manière approfondie au Chapitre 17 (Reverse Engineering du C++ avec GCC) et dans le cas pratique du Chapitre 22 (Reverse d'une application C++ orientée objet).

---

## Résumé

Les binaires C++ compilés avec GCC contiennent trois catégories de métadonnées structurelles que tout analyste doit savoir reconnaître : les vtables (tables de pointeurs de méthodes virtuelles, dans `.rodata`), le RTTI (noms de classes et relations d'héritage, également dans `.rodata`), et les tables d'exceptions (`.eh_frame` et `.gcc_except_table`). Ces artefacts suivent la spécification Itanium C++ ABI et sont présents même dans les binaires strippés (sauf si `-fno-rtti` a été utilisé pour le RTTI). Ghidra les parse en partie automatiquement, mais savoir les identifier manuellement et les exploiter pour reconstruire la hiérarchie de classes est une compétence fondamentale pour l'analyse de binaires C++.

La section suivante poursuit cette logique en montrant comment utiliser le Data Type Manager de Ghidra pour reconstruire concrètement les structures de données — `struct`, `class`, `enum` — à partir des patterns observés dans le désassemblage.

---


⏭️ [Reconstruire des structures de données (`struct`, `class`, `enum`)](/08-ghidra/06-reconstruire-structures.md)
