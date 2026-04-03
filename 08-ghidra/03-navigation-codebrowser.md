🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.3 — Navigation dans le CodeBrowser : Listing, Decompiler, Symbol Tree, Function Graph

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Le CodeBrowser : votre atelier de rétro-ingénierie

Le CodeBrowser est l'outil central de Ghidra — c'est ici que vous passerez l'essentiel de votre temps d'analyse. Si le Project Manager est le hall d'entrée, le CodeBrowser est l'atelier. Il s'ouvre automatiquement lorsque vous double-cliquez sur un binaire importé dans le Project Manager, ou manuellement en faisant glisser le binaire sur l'icône du dragon vert dans le Tool Chest.

Le CodeBrowser est composé de **panneaux** (windows) agencés dans une disposition par défaut que vous pouvez entièrement personnaliser. Chaque panneau offre une vue différente sur le même binaire, et ces vues sont **synchronisées** : naviguer dans l'une met à jour les autres. C'est cette synchronisation qui fait la puissance de l'outil — vous observez simultanément le code machine, le pseudo-code C, les symboles et le graphe de flux d'une même fonction.

Cette section décrit chaque panneau en détail, explique comment les utiliser efficacement et comment les faire collaborer.

---

## Le panneau Listing (vue assembleur)

### Rôle

Le Listing est le panneau central et historiquement le plus important. Il affiche le **désassemblage linéaire** du binaire : chaque ligne correspond à une adresse et montre l'instruction assembleur décodée à cette adresse, accompagnée de ses annotations.

C'est l'équivalent graphique et interactif de la sortie d'`objdump -d`, mais avec une différence fondamentale : le Listing n'est pas une vue en lecture seule. Vous pouvez interagir avec chaque élément — renommer, retyper, commenter, naviguer par clic, créer des références manuelles.

### Anatomie d'une ligne du Listing

Une ligne typique du Listing dans un binaire ELF x86-64 avec symboles ressemble à ceci :

```
00401156  48 89 e5        MOV    RBP,RSP
```

De gauche à droite :

- **L'adresse virtuelle** (`00401156`) — l'emplacement de l'instruction dans l'espace d'adresses du processus. C'est l'adresse telle qu'elle apparaîtrait dans `rip` à l'exécution (modulo l'ASLR pour un binaire PIE).  
- **Les octets bruts** (`48 89 e5`) — l'encodage machine de l'instruction. Cette colonne est masquable via le menu **Edit → Tool Options → Listing Fields → Bytes Field**. Elle est utile pour le patching binaire (Chapitre 21) et pour reconnaître des opcodes spécifiques.  
- **Le mnémonique** (`MOV`) — le nom de l'instruction assembleur.  
- **Les opérandes** (`RBP,RSP`) — les arguments de l'instruction, en syntaxe par défaut (Ghidra utilise un format proche de la syntaxe Intel).

Autour de cette structure de base, Ghidra ajoute plusieurs couches d'information contextuelle :

- **Labels** — au-dessus de la première instruction d'une fonction, Ghidra affiche le nom de la fonction (par exemple `main`, `check_key`, `FUN_004011a0`). Ces labels servent de points d'ancrage pour la navigation.  
- **Commentaires automatiques** — Ghidra ajoute des commentaires en fin de ligne pour les références résolues. Par exemple, un `LEA RDI,[.rodata:s_Enter_key:_]` sera annoté avec le contenu de la chaîne. Les appels à des fonctions connues sont annotés avec le nom de la cible.  
- **Cross-references (XREF)** — au-dessus d'un label de fonction ou d'une donnée, Ghidra liste les adresses qui font référence à cet emplacement. Par exemple, `XREF[2]: main:00401203(c), FUN_00401300:00401345(c)` indique que deux emplacements appellent cette fonction. Le suffixe `(c)` signifie *call*, `(j)` signifie *jump*, et `(*)` signifie *data reference* (pointeur).  
- **Séparateurs de fonctions** — des lignes horizontales séparent visuellement les fonctions les unes des autres, facilitant le repérage des frontières.

### Navigation dans le Listing

La navigation est au cœur de l'expérience. Voici les mécanismes essentiels :

**Navigation par clic** — Double-cliquez sur un opérande qui est une adresse ou un nom de fonction pour y sauter directement. Par exemple, double-cliquer sur `check_key` dans une instruction `CALL check_key` vous amène au début de cette fonction. Double-cliquer sur une référence à `.rodata` vous amène à la donnée correspondante.

**Historique de navigation** — Ghidra maintient un historique de vos déplacements, comme un navigateur web. Utilisez les boutons fléchés de la barre d'outils (ou `Alt+←` / `Alt+→`) pour revenir en arrière ou avancer. C'est indispensable quand vous explorez des chaînes d'appels imbriquées et voulez retrouver votre point de départ.

**Go To Address (`G`)** — Ouvre un champ de saisie pour sauter à une adresse précise, un nom de fonction, ou un label. Accepte les adresses hexadécimales (avec ou sans préfixe `0x`), les noms de symboles et les expressions arithmétiques simples.

**Recherche textuelle (`Ctrl+Shift+E`)** — Recherche dans le texte affiché du Listing (mnémoniques, opérandes, commentaires). Utile pour chercher toutes les occurrences d'un registre particulier ou d'une constante.

**Navigation par fonction** — Les touches de la barre d'outils permettent de sauter à la fonction suivante ou précédente dans l'espace d'adresses. Plus pratique : utilisez le Symbol Tree (décrit plus bas) pour naviguer directement par nom de fonction.

### La barre d'en-tête de champ (Field Header)

Le Listing est en réalité composé de **champs** (fields) configurables individuellement. En activant l'en-tête de champ via **Edit → Tool Options → Listing Fields**, vous pouvez :

- afficher ou masquer des colonnes (octets bruts, adresses, XREFs, commentaires de plate-forme) ;  
- réordonner les colonnes ;  
- ajuster la largeur de chaque champ.

La configuration par défaut convient dans la plupart des cas, mais vous pourriez vouloir masquer les octets bruts pour gagner de l'espace horizontal, ou afficher les offsets relatifs au début de la fonction pour faciliter la corrélation avec les sorties d'`objdump`.

---

## Le panneau Decompiler (pseudo-code C)

### Rôle

Le Decompiler est probablement la fonctionnalité qui distingue le plus Ghidra d'un simple désassembleur. Il transforme le code machine en **pseudo-code C** lisible par un humain, en reconstruisant les structures de contrôle (`if`, `while`, `for`, `switch`), les expressions arithmétiques, les appels de fonctions avec leurs arguments, et les accès aux variables locales.

Le panneau Decompiler affiche le pseudo-code de **la fonction actuellement sélectionnée dans le Listing**. Chaque fois que vous naviguez vers une nouvelle fonction dans le Listing, le Decompiler se met à jour pour montrer le pseudo-code correspondant.

### Synchronisation bidirectionnelle

La synchronisation entre le Listing et le Decompiler est **bidirectionnelle** :

- Cliquer sur une instruction dans le Listing met en surbrillance la ligne correspondante dans le Decompiler.  
- Cliquer sur une ligne ou une variable dans le Decompiler met en surbrillance les instructions assembleur correspondantes dans le Listing.

Cette correspondance n'est pas toujours bijective. Une seule ligne de pseudo-code peut correspondre à plusieurs instructions assembleur (par exemple, une expression complexe), et inversement, une instruction assembleur peut contribuer à plusieurs lignes de pseudo-code (cas rares, mais possibles avec les optimisations).

La surbrillance utilise un code couleur pour montrer quels éléments du pseudo-code correspondent à quelles instructions. Prenez l'habitude de cliquer alternativement dans les deux panneaux pour développer votre intuition de la correspondance entre C et assembleur — c'est l'un des meilleurs exercices pour progresser en reverse engineering.

### Lire le pseudo-code

Le pseudo-code produit par Ghidra ressemble à du C, mais ce n'est pas du C compilable. C'est une **approximation structurelle** du comportement du code machine. Voici les conventions à connaître :

**Types génériques** — Quand Ghidra ne connaît pas le type exact d'une variable, il utilise des types génériques basés sur la taille :

- `undefined1` — un octet de type inconnu ;  
- `undefined4` — 4 octets (souvent un `int` ou un `float`) ;  
- `undefined8` — 8 octets (souvent un `long`, un `double` ou un pointeur) ;  
- `long` — Ghidra utilise parfois `long` quand il détecte un entier de 8 octets via l'analyse de flux.

Au fur et à mesure que vous annoter le binaire (section 8.4), ces types génériques seront remplacés par des types précis.

**Variables locales** — Les variables sont nommées automatiquement selon un schéma prévisible :

- `local_XX` — variable locale à l'offset `XX` (hexadécimal) par rapport au frame pointer ou au stack pointer ;  
- `param_1`, `param_2`, etc. — paramètres de la fonction, dans l'ordre de la convention d'appel ;  
- `iVar1`, `lVar2`, `uVar3` — variables temporaires créées par le décompileur. Le préfixe indique le type déduit : `i` pour `int`, `l` pour `long`, `u` pour `uint`, `p` pour pointeur, `c` pour `char`, `b` pour `bool`.

**Casts explicites** — Le décompileur insère des casts `(type)` quand il détecte des conversions de type implicites dans le code machine. Ces casts sont souvent verbeux mais reflètent fidèlement le comportement du binaire.

**Qualité variable** — Le pseudo-code d'un binaire `-O0` avec symboles DWARF peut être quasiment identique au code source original. Le pseudo-code d'un binaire `-O3` strippé sera fonctionnellement correct mais structurellement méconnaissable : boucles déroulées, fonctions inlinées, variables fusionnées en registres. Ne vous attendez pas à retrouver le source original — attendez-vous à comprendre le comportement.

### Interactions dans le Decompiler

Le panneau Decompiler n'est pas qu'une vue passive. Vous pouvez interagir directement avec le pseudo-code :

- **Renommer une variable ou un paramètre** — Clic droit → **Rename Variable** (ou touche `L`). Le nouveau nom se propage dans tout le pseudo-code de la fonction et se reflète dans le Listing.  
- **Changer le type d'une variable** — Clic droit → **Retype Variable** (ou touche `T`). Particulièrement utile pour transformer un `undefined8 *` en `struct player_t *` quand vous avez reconstruit la structure.  
- **Naviguer vers une fonction appelée** — Double-cliquez sur un nom de fonction dans le pseudo-code pour y sauter, exactement comme dans le Listing.  
- **Afficher les cross-references** — Clic droit sur un identifiant → **References to** pour voir tous les endroits qui accèdent à cette variable ou appellent cette fonction.  
- **Modifier la signature de la fonction** — Clic droit sur le nom de la fonction en haut du pseudo-code → **Edit Function Signature**. Vous pouvez corriger le type de retour, les types et noms des paramètres, et la convention d'appel. Le décompileur se met à jour immédiatement.

> 💡 **Astuce de productivité** — Quand vous analysez une fonction, commencez par identifier et renommer les paramètres dans le Decompiler. Cette seule action rend souvent le reste du pseudo-code immédiatement compréhensible, car les noms se propagent dans toutes les expressions qui utilisent ces paramètres.

---

## Le panneau Symbol Tree

### Rôle

Le Symbol Tree est le **répertoire** de tout ce que Ghidra a identifié dans le binaire : fonctions, labels, namespaces, classes, imports, exports et variables globales. C'est votre principal outil de navigation à l'échelle du binaire — plutôt que de vous déplacer adresse par adresse dans le Listing, vous parcourez le Symbol Tree pour atteindre directement la fonction ou la donnée qui vous intéresse.

### Structure de l'arbre

Le Symbol Tree est organisé en catégories hiérarchiques :

**Imports** — Les fonctions importées depuis les bibliothèques partagées. Dans un binaire ELF lié dynamiquement à la libc, vous trouverez ici les fonctions comme `printf`, `malloc`, `strcmp`, `open`, `read`, etc. Ces noms proviennent de la table `.dynsym` et sont toujours disponibles, même dans un binaire strippé (car le linker dynamique en a besoin pour résoudre les symboles à l'exécution).

Les imports sont regroupés par bibliothèque d'origine. Vous verrez par exemple :

```
Imports
├── libc.so.6
│   ├── printf
│   ├── malloc
│   ├── strcmp
│   ├── exit
│   └── ...
├── libstdc++.so.6
│   ├── __cxa_throw
│   ├── operator new(unsigned long)
│   └── ...
└── libm.so.6
    ├── sqrt
    └── ...
```

Cette organisation vous donne immédiatement une vue d'ensemble des capacités du programme : un binaire qui importe des fonctions réseau (`socket`, `connect`, `send`, `recv`) est probablement un client ou serveur ; un binaire qui importe `dlopen`/`dlsym` charge des plugins dynamiquement ; un binaire qui importe des fonctions crypto (`EVP_EncryptInit`, `AES_encrypt`) effectue du chiffrement.

**Exports** — Les fonctions et données exportées par le binaire. Pour un exécutable classique, il y en a peu (essentiellement `_start` et les symboles liés à l'initialisation du runtime C). Pour une bibliothèque partagée (`.so`), les exports constituent l'API publique de la bibliothèque.

**Functions** — La liste de toutes les fonctions identifiées dans le binaire, qu'elles proviennent des symboles ou de l'analyse heuristique. Dans un binaire avec symboles, vous y trouvez les noms réels (`main`, `check_key`, `process_input`). Dans un binaire strippé, vous trouvez des noms auto-générés (`FUN_00401156`, `FUN_004012a0`).

Si le binaire est du C++ avec des symboles, les fonctions sont organisées en **namespaces** et **classes**, reflétant la hiérarchie du code source :

```
Functions
├── main
├── Animal
│   ├── Animal(void)
│   ├── ~Animal(void)
│   └── speak(void)
├── Dog
│   ├── Dog(char const *)
│   └── speak(void)
└── ...
```

Cette organisation hiérarchique est l'un des atouts majeurs de Ghidra pour l'analyse C++ — elle survit partiellement même dans un binaire strippé si le RTTI est présent (section 8.5).

**Labels** — Les labels nommés qui ne sont pas des fonctions : points d'entrée de blocs, cibles de sauts, adresses nommées dans les données.

**Classes** — Spécifique au C++ : les classes détectées via les vtables, le RTTI ou les informations DWARF. Ce nœud peut être vide pour un binaire C pur.

**Namespaces** — Les espaces de noms C++ et les regroupements logiques de symboles.

### Recherche et filtrage

Le Symbol Tree intègre un **champ de filtre** en bas du panneau. Tapez quelques caractères pour filtrer dynamiquement les entrées. C'est extrêmement efficace pour localiser rapidement une fonction par nom partiel.

Par exemple, dans un binaire de jeu C++, taper `player` filtrera toutes les fonctions et classes dont le nom contient « player » : `Player::update`, `Player::getHealth`, `process_player_input`, etc.

Le filtre accepte aussi le caractère joker `*` pour des recherches par motif.

### Navigation depuis le Symbol Tree

Double-cliquez sur n'importe quel élément du Symbol Tree pour naviguer directement vers son adresse dans le Listing (et par synchronisation, dans le Decompiler). C'est le moyen le plus rapide d'atteindre une fonction spécifique.

Clic droit sur un élément ouvre un menu contextuel qui permet, entre autres, de le renommer, de voir ses références (qui l'appelle, qui y accède), de l'éditer (pour une fonction : modifier la signature), ou de le chercher dans d'autres vues.

---

## Le panneau Function Graph

### Rôle

Le Function Graph (ou vue graphe) transforme le désassemblage linéaire d'une fonction en un **diagramme de blocs basiques** reliés par des arêtes. Chaque bloc basique est une séquence d'instructions qui s'exécutent toujours linéairement (pas de branchement interne) ; les arêtes représentent les sauts conditionnels et inconditionnels entre blocs.

Cette vue est indispensable pour comprendre la **logique de contrôle** d'une fonction : quels chemins d'exécution sont possibles, où se trouvent les branchements conditionnels, quelles sont les boucles, et comment les différents cas d'un `switch` sont organisés.

### Accéder au Function Graph

Depuis le Listing, placez le curseur dans la fonction que vous souhaitez visualiser, puis :

- appuyez sur **`Space`** pour basculer entre la vue Listing linéaire et la vue graphe (et inversement) ;  
- ou utilisez le menu **Window → Function Graph**.

Le graphe s'affiche dans le panneau central, remplaçant temporairement le Listing linéaire (si vous utilisez `Space`) ou dans une fenêtre séparée (si vous utilisez le menu).

### Lecture du graphe

Le graphe se lit de haut en bas. Le bloc d'entrée de la fonction (contenant le prologue) est en haut. Le ou les blocs de sortie (contenant `RET`) sont en bas.

**Code couleur des arêtes** — Ghidra colorie les arêtes pour indiquer le type de branchement :

- **Vert** — la branche prise quand la condition est **vraie** (par exemple, le chemin pris par `JZ` quand ZF=1, c'est-à-dire quand la comparaison est égale).  
- **Rouge** — la branche prise quand la condition est **fausse** (le *fall-through*, quand le saut conditionnel n'est pas pris).  
- **Bleu** — un saut inconditionnel (`JMP`).

> ⚠️ **Attention à l'interprétation** — La convention vert=vrai/rouge=faux concerne le résultat du test assembleur, pas la logique métier. Un `JNZ` (Jump if Not Zero) après un `CMP` saute (vert) quand les valeurs sont **différentes**. Si le code compare un mot de passe, la branche verte du `JNZ` correspond au cas « mot de passe incorrect » (les chaînes diffèrent), ce qui peut être contre-intuitif. Prenez toujours le temps de lire l'instruction de comparaison et le type de saut avant d'interpréter les couleurs.

**Contenu des blocs** — Chaque bloc affiche les mêmes informations que le Listing linéaire : adresses, instructions, opérandes, commentaires. Vous pouvez cliquer sur n'importe quel élément à l'intérieur d'un bloc pour les mêmes interactions que dans le Listing (renommage, retypage, navigation).

### Patterns visuels courants

À force de pratiquer, vous apprendrez à reconnaître visuellement des structures de contrôle classiques directement dans le graphe, sans même lire les instructions :

**Un `if`/`else`** — Le bloc de test a deux arêtes sortantes (verte et rouge) qui mènent à deux blocs distincts, lesquels convergent ensuite vers un bloc commun (le code après le `if`/`else`). La forme est un losange.

**Un `if` sans `else`** — Le bloc de test a deux arêtes : l'une mène à un bloc de traitement qui rejoint ensuite le flux principal, l'autre va directement au flux principal. La forme est un triangle.

**Une boucle `while` ou `for`** — Un bloc de test a une arête qui remonte vers un bloc situé plus haut dans le graphe (arête de retour). La condition de boucle est dans le bloc de test, et le corps de la boucle est dans les blocs entre le test et l'arête de retour.

**Un `switch`/`case`** — Un bloc unique a de nombreuses arêtes sortantes (une par cas), créant un éventail de blocs parallèles qui convergent vers un point commun après le `switch`. Ghidra détecte souvent les tables de sauts et les annote dans le listing.

**Fonctions linéaires** — Une séquence verticale de blocs sans branchement. Typique des fonctions d'initialisation ou des wrappers.

### Zoom et navigation dans le graphe

Les graphes de fonctions complexes peuvent devenir très vastes (des dizaines de blocs pour une fonction avec beaucoup de logique conditionnelle). Ghidra offre plusieurs mécanismes pour s'y retrouver :

- **Molette de souris** — zoom avant/arrière.  
- **Clic-glisser sur le fond** — déplacer la vue.  
- **Vue miniature** — une carte miniature du graphe complet apparaît dans un coin. Le rectangle blanc indique la portion actuellement visible. Cliquez-glissez dans la miniature pour naviguer rapidement.  
- **Clic droit → Reset Layout** — réorganise le graphe si l'agencement automatique a produit un résultat confus.

### Limites du Function Graph

Le Function Graph ne montre qu'**une seule fonction à la fois**. Il ne montre pas les appels vers d'autres fonctions sous forme de sous-graphes — un `CALL` apparaît comme une instruction dans un bloc, pas comme une arête vers un autre graphe. Pour explorer les fonctions appelées, double-cliquez sur le `CALL` pour naviguer vers la cible, puis appuyez sur `Space` pour voir son graphe.

Pour les très grandes fonctions (centaines de blocs), le graphe peut devenir difficile à lire. Dans ce cas, le Listing linéaire combiné avec le Decompiler est souvent plus efficace, et vous pouvez revenir au graphe pour des portions spécifiques.

---

## Panneaux secondaires

Au-delà des quatre panneaux principaux décrits ci-dessus, le CodeBrowser propose plusieurs panneaux complémentaires accessibles via le menu **Window**. En voici les plus utiles dans le contexte de ce tutoriel.

### Program Trees

Situé par défaut en haut à gauche (en onglet avec le Symbol Tree), le Program Trees affiche la structure du binaire en termes de **segments et sections** mémoire. Vous y retrouvez les sections ELF étudiées au Chapitre 2 : `.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`, `.init`, `.fini`, etc.

Double-cliquer sur une section navigue vers son début dans le Listing. C'est utile quand vous cherchez spécifiquement les données en lecture seule (`.rodata`), les variables globales (`.data`/`.bss`), ou les entrées PLT/GOT (`.plt`/`.got`).

### Data Type Manager

Accessible via **Window → Data Type Manager**, ce panneau est le gestionnaire de types de Ghidra. Il affiche les types disponibles, organisés en catégories :

- **BuiltInTypes** — les types primitifs C/C++ (`int`, `long`, `char`, `void *`, `float`, `double`, etc.) ;  
- **Archives de types** — Ghidra embarque des archives de types (`.gdt`) pour les bibliothèques courantes. L'archive `generic_clib` contient les signatures des fonctions de la libc ; `windows_vs12` contient les types de l'API Windows. Ces archives sont chargées automatiquement selon le contexte.  
- **Types du programme** — les types spécifiques au binaire en cours d'analyse, incluant les structures détectées automatiquement et celles que vous créerez manuellement (section 8.6).

Le Data Type Manager sera abordé en détail dans les sections 8.4 et 8.6. Retenez pour l'instant qu'il existe et qu'il est le point d'accès central pour tout ce qui concerne le typage.

### Defined Strings

Accessible via **Window → Defined Strings**, ce panneau liste toutes les chaînes de caractères identifiées dans le binaire. C'est l'équivalent interactif de `strings`, mais avec l'avantage d'un lien direct vers le contexte d'utilisation : double-cliquer sur une chaîne navigue vers son emplacement dans le Listing, et de là, vous pouvez utiliser les cross-references (`X`) pour voir quel code la référence.

Ce panneau est l'un des premiers que vous consulterez lors du triage d'un binaire dans Ghidra. Les chaînes de caractères révèlent souvent la fonctionnalité d'un programme : messages d'erreur, noms de fichiers, URLs, commandes de protocole, messages de licence.

### Console

Le panneau Console en bas du CodeBrowser affiche les messages de log : résultats d'analyse, erreurs, sorties de scripts. Quand vous exécuterez des scripts Ghidra (section 8.8), c'est ici que leurs `println` et messages d'erreur apparaîtront.

### Bookmarks

Ghidra permet de placer des **signets** (bookmarks) à n'importe quelle adresse via clic droit → **Bookmark…** ou `Ctrl+D`. Les signets apparaissent dans le panneau **Window → Bookmarks**. C'est un outil de prise de notes intégré au binaire : marquez les fonctions intéressantes, les points de décision critiques, les zones à revisiter plus tard.

L'analyse automatique crée aussi des bookmarks automatiques pour signaler des anomalies : code non résolu, références invalides, erreurs de désassemblage. Parcourez-les après l'analyse initiale pour identifier les zones problématiques.

---

## Workflow de navigation type

Pour illustrer comment ces panneaux collaborent, voici un workflow type lors de l'analyse initiale d'un binaire :

**Étape 1 — Orientation via le Symbol Tree.** Ouvrez le Symbol Tree et parcourez la catégorie **Functions**. Si le binaire a des symboles, repérez `main` et les fonctions au nom évocateur. Si le binaire est strippé, regardez les **Imports** pour comprendre les capacités du programme, puis trouvez le point d'entrée via **Exports → _start** ou via **Navigation → Go To → Entry Point**.

**Étape 2 — Lecture du Decompiler.** Double-cliquez sur `main` (ou le point d'entrée identifié). Le Decompiler affiche le pseudo-code. Lisez-le pour comprendre la structure globale : quelles fonctions sont appelées, dans quel ordre, avec quels arguments. Notez les noms de fonctions intéressantes à explorer.

**Étape 3 — Plongée dans une fonction cible.** Double-cliquez sur un appel de fonction dans le Decompiler pour y naviguer. Lisez le pseudo-code de cette nouvelle fonction. Si la logique de contrôle est complexe, basculez en vue graphe avec `Space` pour visualiser la structure des branchements.

**Étape 4 — Corrélation Listing ↔ Decompiler.** Quand une ligne du pseudo-code est obscure, cliquez dessus pour voir les instructions assembleur correspondantes dans le Listing. Analysez le code machine pour comprendre ce que le décompileur essaie d'exprimer.

**Étape 5 — Vérification par les chaînes.** Ouvrez **Defined Strings** pour chercher des chaînes révélatrices. Double-cliquez sur une chaîne intéressante, puis utilisez `X` (Show References) pour remonter jusqu'au code qui l'utilise.

**Étape 6 — Annotation progressive.** Au fil de votre compréhension, renommez les fonctions et variables (`L`), ajoutez des commentaires (`;`), et modifiez les types (`T`). Chaque annotation améliore la lisibilité des fonctions appelantes et appelées grâce à la propagation automatique.

Ce workflow n'est pas une recette rigide — c'est une trame que vous adapterez à chaque binaire. L'essentiel est de comprendre que l'analyse est un processus **itératif** : chaque annotation que vous ajoutez enrichit le contexte pour la suite.

---

## Personnaliser la disposition

L'agencement par défaut du CodeBrowser est un point de départ raisonnable, mais vous développerez rapidement vos préférences. Quelques configurations populaires :

**Disposition « analyse intensive »** — Listing et Decompiler côte à côte en occupation maximale, Symbol Tree dans un onglet réduit à gauche. Maximise l'espace de lecture pour les deux vues principales.

**Disposition « exploration »** — Symbol Tree largement ouvert à gauche, Decompiler seul à droite (Listing masqué ou en onglet). Privilégie la navigation par symboles et la lecture du pseudo-code. Adaptée à la phase d'orientation sur un gros binaire.

**Disposition « graphe »** — Function Graph en plein écran central, Decompiler en panneau latéral droit. Utilisée ponctuellement pour l'analyse de la logique de contrôle d'une fonction complexe.

Pour sauvegarder une disposition, utilisez **Window → Save Tool**. Vous pouvez créer plusieurs configurations et basculer entre elles selon la phase d'analyse.

---

## Résumé

Le CodeBrowser est l'environnement où se déroule l'essentiel du travail d'analyse. Ses quatre panneaux principaux — Listing, Decompiler, Symbol Tree et Function Graph — offrent chacun une perspective complémentaire sur le binaire, et leur synchronisation permet de naviguer fluidement entre le code machine et une vue de haut niveau. Les panneaux secondaires (Program Trees, Data Type Manager, Defined Strings, Bookmarks) enrichissent ce dispositif avec des vues spécialisées.

La maîtrise de cette interface est progressive. Concentrez-vous d'abord sur le triangle Listing–Decompiler–Symbol Tree, qui couvre 90 % des besoins d'analyse courante, puis intégrez le Function Graph quand vous abordez des fonctions à la logique de contrôle complexe.

La section suivante vous montrera comment transformer un désassemblage anonyme en un document lisible grâce au renommage, aux commentaires et à la création de types personnalisés.

---


⏭️ [Renommage de fonctions et variables, ajout de commentaires, création de types](/08-ghidra/04-renommage-commentaires-types.md)
