🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.6 — Reconstruire des structures de données (`struct`, `class`, `enum`)

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Le défi central du reverse engineering

Quand un compilateur produit du code machine, les structures de données du code source — `struct`, `class`, `enum`, `union` — cessent d'exister en tant qu'entités nommées. Elles sont réduites à des **patterns d'accès mémoire** : des lectures et écritures à des offsets constants par rapport à un pointeur de base. Le compilateur sait qu'un `player->health` se traduit par un accès à `[rdi + 0x0c]`, mais cette information sémantique (« c'est le champ health d'un joueur ») est perdue dans le binaire.

Reconstruire ces structures à partir du code désassemblé est l'une des tâches les plus exigeantes et les plus gratifiantes du reverse engineering. C'est un travail de déduction : vous observez les patterns d'accès dans le code, vous collectez les indices (tailles, alignements, types déduits, contexte d'utilisation), et vous reconstituez progressivement la structure originale — ou du moins une structure fonctionnellement équivalente.

Ghidra fournit un ensemble d'outils puissants pour faciliter ce travail. Cette section vous montre comment les utiliser méthodiquement.

---

## Reconnaître qu'une structure existe

Avant de reconstruire une structure, il faut d'abord détecter sa présence. Voici les patterns caractéristiques dans le pseudo-code du Decompiler qui trahissent l'existence d'une structure sous-jacente.

### Accès à offsets multiples depuis un même pointeur

C'est le signal le plus courant. Quand le Decompiler montre des expressions comme :

```c
*(int *)(param_1 + 0)
*(int *)(param_1 + 4)
*(char **)(param_1 + 8)
*(long *)(param_1 + 0x10)
*(undefined4 *)(param_1 + 0x18)
```

Le paramètre `param_1` est un pointeur vers une structure dont les champs se trouvent aux offsets 0x00, 0x04, 0x08, 0x10 et 0x18. Chaque déréférencement avec un type et un offset distincts correspond à un champ différent.

### Allocation suivie d'initialisations séquentielles

Quand une fonction appelle `malloc` (ou `operator new`) avec une taille constante, puis écrit à des offsets successifs du pointeur retourné, c'est un constructeur qui initialise les champs d'une structure :

```c
void * pvVar1 = malloc(0x20);       // allocation de 32 octets
*(int *)pvVar1 = 1;                  // offset 0x00 : un entier
*(int *)((long)pvVar1 + 4) = 100;    // offset 0x04 : un entier
*(char **)((long)pvVar1 + 8) = "default";  // offset 0x08 : un pointeur
*(long *)((long)pvVar1 + 0x10) = 0;  // offset 0x10 : un long
```

La taille passée à `malloc` (ici 0x20 = 32 octets) vous donne la **taille totale** de la structure. Les initialisations vous donnent les offsets, les types et les valeurs par défaut de chaque champ.

### Passage comme premier argument à de multiples fonctions

En C++, le pointeur `this` est passé en premier argument (registre `rdi` dans la convention System V AMD64). Si vous observez qu'un même pointeur est passé comme `param_1` à plusieurs fonctions différentes qui accèdent toutes à des offsets de ce pointeur, ces fonctions sont probablement des méthodes de la même classe, et le pointeur est un `this`.

### Tableaux de structures

Un pattern d'accès comme :

```c
*(int *)(base + i * 0x18)
*(int *)(base + i * 0x18 + 4)
*(long *)(base + i * 0x18 + 8)
```

révèle un tableau de structures de taille 0x18 (24 octets), où `i` est l'index. Le facteur multiplicatif constant correspond au `sizeof` de la structure, et les petits offsets ajoutés correspondent aux champs individuels.

---

## L'éditeur de structures de Ghidra

### Accéder à l'éditeur

Pour créer une nouvelle structure :

1. Ouvrez le **Data Type Manager** (**Window → Data Type Manager**).  
2. Dans l'arborescence, localisez la catégorie portant le nom de votre programme (par exemple `ch08-oop_O0`).  
3. Clic droit sur cette catégorie → **New → Structure**.  
4. Nommez la structure (par exemple `player_t`, `packet_header_t`, `config_entry`).  
5. L'**éditeur de structure** s'ouvre.

### Interface de l'éditeur

L'éditeur de structure est une fenêtre dédiée qui présente les champs sous forme tabulaire :

| Colonne | Rôle |  
|---|---|  
| **Offset** | Position du champ en octets depuis le début de la structure |  
| **Length** | Taille du champ en octets |  
| **DataType** | Type du champ (`int`, `char *`, `float`, une autre structure, etc.) |  
| **Name** | Nom du champ |  
| **Comment** | Commentaire optionnel |

La structure est initialement vide, avec une taille de 0. Vous pouvez la construire de deux manières.

### Construction champ par champ (méthode manuelle)

C'est la méthode la plus contrôlée. Vous ajoutez les champs un par un en vous basant sur les accès observés dans le Decompiler :

1. Cliquez sur la première ligne vide de la table.  
2. Dans la colonne **DataType**, tapez le type du premier champ (par exemple `int`).  
3. Dans la colonne **Name**, nommez le champ (par exemple `id`).  
4. Passez à la ligne suivante pour le champ suivant.

L'éditeur gère automatiquement les offsets et la taille totale de la structure. Si vous devez insérer un champ à un offset précis qui laisse un trou entre deux champs existants, Ghidra insère automatiquement des octets de padding (`undefined1`) pour combler l'espace.

**Gestion du padding et de l'alignement** — GCC aligne les champs selon les règles de la plateforme cible. Sur x86-64, un `int` (4 octets) est aligné sur une frontière de 4 octets, un `long` ou un pointeur (8 octets) sur une frontière de 8 octets. Cela signifie que des « trous » (padding) apparaissent parfois entre les champs. Par exemple :

```c
struct example {
    char  a;        // offset 0x00, 1 octet
    // 3 octets de padding implicite
    int   b;        // offset 0x04, 4 octets
    char  c;        // offset 0x08, 1 octet
    // 7 octets de padding implicite
    long  d;        // offset 0x10, 8 octets
};  // sizeof = 0x18 (24 octets)
```

Dans Ghidra, vous modélisez ces trous en laissant des octets `undefined1` entre les champs nommés, ou en les représentant explicitement comme un champ `byte[3] padding_1`. En pratique, il est plus propre de laisser Ghidra gérer le padding automatiquement — les octets non assignés entre deux champs sont affichés en gris.

### Construction par taille totale (méthode par squelette)

Si vous connaissez la taille totale de la structure (via l'argument de `malloc` ou `operator new`), vous pouvez commencer par la définir :

1. Dans l'éditeur de structure, modifiez la taille en bas de la fenêtre (champ **Size**) à la valeur observée.  
2. La structure est remplie d'octets `undefined1`.  
3. Cliquez sur l'offset d'un champ que vous avez identifié et changez son type.

Cette approche est utile quand vous ne connaissez que quelques champs sur une grande structure : vous posez le squelette de la bonne taille, vous remplissez les champs connus, et vous laissez les zones inconnues en `undefined` pour les compléter plus tard.

### Structures imbriquées

Les structures peuvent contenir d'autres structures — en tant que champ inline (pas un pointeur). Par exemple, une structure `game_state_t` pourrait contenir un champ de type `player_t` directement inclus à un certain offset. Dans l'éditeur, vous tapez simplement le nom de la structure imbriquée dans la colonne **DataType** : Ghidra la reconnaît si elle existe dans le Data Type Manager.

De même, un champ peut être un **pointeur vers une structure** : tapez `player_t *` pour un pointeur vers `player_t`. Cette distinction est importante : un champ inline occupe `sizeof(player_t)` octets dans la structure parente, tandis qu'un pointeur occupe toujours 8 octets (sur x86-64).

### Tableaux dans les structures

Pour un champ qui est un tableau, utilisez la notation entre crochets : `char[32]` pour un buffer de 32 caractères, `int[10]` pour un tableau de 10 entiers. Ghidra calcule automatiquement la taille résultante.

---

## Auto Create Structure : la méthode assistée

Ghidra propose une fonctionnalité semi-automatique qui accélère considérablement la reconstruction de structures en analysant les patterns d'accès dans le code.

### Comment l'utiliser

1. Dans le Decompiler, identifiez un paramètre ou une variable qui est visiblement un pointeur vers une structure (vous voyez des accès à `param_1 + offset`).  
2. Clic droit sur ce paramètre → **Auto Create Structure**.  
3. Ghidra analyse tous les accès à ce pointeur dans la fonction courante, déduit les offsets et les types de chaque champ, crée une structure dans le Data Type Manager, et l'applique automatiquement au paramètre.

### Résultat typique

Le pseudo-code passe de :

```c
undefined8 FUN_004012a0(long param_1)
{
    if (*(int *)(param_1 + 0xc) < 100) {
        *(int *)(param_1 + 0xc) = *(int *)(param_1 + 0xc) + *(int *)(param_1 + 0x10);
    }
    puts(*(char **)(param_1 + 0x18));
    return 0;
}
```

à :

```c
undefined8 FUN_004012a0(astruct * param_1)
{
    if (param_1->field_0xc < 100) {
        param_1->field_0xc = param_1->field_0xc + param_1->field_0x10;
    }
    puts(param_1->field_0x18);
    return 0;
}
```

### Limites de l'Auto Create Structure

L'outil a des limites qu'il faut connaître :

**Portée limitée à une seule fonction** — L'auto-création n'analyse que la fonction courante. Si la structure est utilisée dans 10 fonctions différentes et que chaque fonction accède à des champs différents, l'auto-création dans une seule fonction ne verra qu'un sous-ensemble des champs. Vous devrez compléter la structure manuellement en examinant les autres fonctions.

**Noms génériques** — Les champs sont nommés `field_0xNN` (où NN est l'offset hexadécimal). Vous devrez les renommer manuellement en noms sémantiques.

**Types parfois imprécis** — Le type déduit est basé sur la taille et le contexte d'utilisation dans la fonction. Un champ accédé comme `*(int *)(ptr + 0x4)` sera typé `int`, ce qui est souvent correct. Mais un champ accédé uniquement par `MOV` de 8 octets pourrait être un `long`, un `double`, un pointeur, ou un autre type de même taille — l'outil ne peut pas toujours distinguer.

**Ne détecte pas les tableaux** — Si un champ est en réalité un tableau inline (par exemple `char name[64]`), l'auto-création peut le fragmenter en plusieurs champs de petite taille ou le traiter comme un unique blob.

### Stratégie recommandée

La meilleure approche combine l'auto-création et le travail manuel :

1. Lancez l'Auto Create Structure sur la fonction qui accède au plus grand nombre de champs — souvent le constructeur ou une fonction d'initialisation.  
2. Examinez la structure créée dans l'éditeur. Renommez les champs pour lesquels vous avez un nom significatif.  
3. Parcourez les autres fonctions qui utilisent le même type de pointeur. Pour chaque nouveau champ accédé, ajoutez-le manuellement dans l'éditeur de structure.  
4. Affinez les types au fur et à mesure : remplacez les `undefined4` par des `int`, `float` ou `enum` selon le contexte.

---

## Reconstruire des classes C++

La reconstruction de classes C++ suit les mêmes principes que pour les structures C, avec des spécificités liées au modèle objet décrit en section 8.5.

### Layout mémoire d'un objet C++

Un objet C++ en mémoire est organisé comme suit (héritage simple, sans classes virtuelles de base) :

```
Offset    Contenu
──────    ───────
0x00      vptr (pointeur vers la vtable) — uniquement si la classe a des méthodes virtuelles
0x08      champs hérités de la classe parente (dans l'ordre de déclaration)
...       champs propres à la classe (dans l'ordre de déclaration)
```

Le vptr, s'il est présent, est **toujours** le premier champ (offset 0). Il occupe 8 octets sur x86-64. Les champs de la classe parente viennent ensuite (sauf le vptr du parent, qui est « fusionné » avec celui de la classe enfant). Puis les champs propres à la classe dérivée.

### Étapes de reconstruction

**1. Identifier le vptr** — Si le constructeur de la classe écrit une constante à `[this + 0]`, ce champ est le vptr. Créez un premier champ `void * vptr` à l'offset 0x00 (ou un type `pointer` vers la vtable si vous l'avez déjà modélisée).

**2. Déterminer la taille totale** — Cherchez l'appel à `operator new` qui alloue l'objet. L'argument est le `sizeof` de la classe. Par exemple, `operator new(0x28)` indique une classe de 40 octets.

**3. Identifier les champs hérités** — Si vous avez déjà reconstruit la classe parente, ses champs occupent les mêmes offsets dans la classe dérivée (après le vptr). Vous pouvez inclure la structure parente comme premier champ (inline), ou dupliquer ses champs.

**4. Identifier les champs propres** — Examinez le constructeur et les méthodes de la classe pour trouver les accès aux offsets situés après les champs hérités.

**5. Modéliser dans Ghidra** — Créez la structure dans le Data Type Manager. Voici un exemple pour une classe `Dog` héritant de `Animal` :

```
struct Dog {                          // sizeof = 0x28 (40 octets)
    void *      vptr;                 // 0x00 — pointeur vers vtable_Dog
    int         age;                  // 0x08 — hérité de Animal
    int         health;               // 0x0c — hérité de Animal
    char *      name;                 // 0x10 — hérité de Animal
    int         tricks_count;         // 0x18 — propre à Dog
    int         padding;              // 0x1c — alignement
    char *      breed;                // 0x20 — propre à Dog
};
```

**6. Appliquer le type** — Modifiez la signature de chaque méthode pour que `param_1` (le `this` implicite) soit de type `Dog *`. Le Decompiler remplacera alors tous les accès bruts par des accès nommés aux champs.

### Héritage multiple

L'héritage multiple complique le layout mémoire. Quand une classe `C` hérite à la fois de `A` et de `B`, l'objet contient **deux** sous-objets : le sous-objet `A` commence à l'offset 0, suivi du sous-objet `B` (qui inclut son propre vptr). L'objet a donc **deux vptrs** :

```
struct C {
    // sous-objet A
    void * vptr_A;          // 0x00 — vtable de C en tant que A
    // champs de A...
    
    // sous-objet B
    void * vptr_B;          // 0xNN — vtable de C en tant que B
    // champs de B...
    
    // champs propres à C
};
```

La valeur de l'offset-to-top dans la vtable secondaire (celle du sous-objet `B`) indique le décalage négatif pour retrouver le début de l'objet `C` complet depuis le sous-objet `B`. L'héritage multiple est plus rare et nettement plus complexe à reconstruire. Le Chapitre 17 y reviendra en détail.

---

## Reconstruire des enums à partir de constantes

### Détecter un enum dans le pseudo-code

Les enums se manifestent dans le code désassemblé comme des **constantes entières utilisées dans des comparaisons ou des switch**. Voici les patterns révélateurs :

**Chaîne de `if`/`else if` avec des constantes séquentielles** :

```c
if (local_c == 0) { ... }  
else if (local_c == 1) { ... }  
else if (local_c == 2) { ... }  
else if (local_c == 3) { ... }  
```

**Instruction `switch` avec des cas numérotés** — Ghidra détecte souvent les tables de sauts et les présente comme un `switch` dans le Decompiler :

```c
switch(command) {
    case 0: ...
    case 1: ...
    case 2: ...
    case 3: ...
}
```

**Constantes utilisées comme flags avec des opérations bitwise** :

```c
if ((flags & 1) != 0) { ... }   // FLAG_ACTIVE   = 0x01  
if ((flags & 2) != 0) { ... }   // FLAG_VISIBLE  = 0x02  
if ((flags & 4) != 0) { ... }   // FLAG_LOCKED   = 0x04  
```

### Déduire les noms des valeurs

Les noms des valeurs d'enum ne sont jamais présents dans le binaire (sauf si des messages de log ou d'erreur les mentionnent en clair). Vous devez les déduire du contexte :

- **Chaînes associées** — Si un `case` affiche un message comme `puts("Processing authentication...")`, la valeur correspondante est probablement `CMD_AUTH` ou `STATE_AUTHENTICATING`.  
- **Fonctions appelées** — Si un `case` appelle `send_ping_response()`, la valeur est probablement `CMD_PING`.  
- **Position dans la séquence** — Les valeurs 0, 1, 2, 3… d'un enum correspondent souvent à une progression logique : `STATE_INIT`, `STATE_CONNECTED`, `STATE_AUTHENTICATED`, `STATE_RUNNING`.  
- **Valeurs puissances de 2** — 1, 2, 4, 8, 16… indiquent des flags combinables par OR bitwise.

### Créer l'enum dans Ghidra

1. Data Type Manager → clic droit sur la catégorie du programme → **New → Enum**.  
2. Nommez l'enum (`command_e`, `state_e`, `flags_e`).  
3. Ajoutez chaque valeur avec son nom et sa constante numérique.  
4. Définissez la **taille** de l'enum. Examinez le code assembleur pour déterminer si les comparaisons utilisent des opérations 32 bits (`cmp eax, ...` → 4 octets) ou 8 bits (`cmp al, ...` → 1 octet). La taille par défaut de 4 octets est correcte dans la majorité des cas sur x86-64.  
5. Validez.

Appliquez ensuite le type de l'enum à la variable ou au paramètre concerné dans le Decompiler (touche `T`). Le pseudo-code remplace immédiatement les constantes numériques par les noms symboliques :

```c
// Avant
if (param_2 == 2) { send_data(param_1); }

// Après application de l'enum command_e
if (command == CMD_DATA) { send_data(connection); }
```

---

## Reconstruire des unions

Les unions sont plus rares que les structures, mais elles apparaissent dans certains contextes : protocoles réseau avec des paquets de formats différents, interpréteurs avec des nœuds d'AST polymorphes, ou structures à double usage (accès par champ individuel ou par bloc brut).

### Détecter une union

Le signal caractéristique est un **même offset accédé avec des types différents** selon le contexte :

```c
// Dans une branche
*(int *)(param_1 + 8) = 42;

// Dans une autre branche
*(float *)(param_1 + 8) = 3.14;

// Dans une troisième branche
*(char **)(param_1 + 8) = "hello";
```

Si les trois accès portent sur le même offset (0x08) mais avec des types incompatibles, c'est le signe d'une union à cet offset.

Un autre indice est un champ de **discriminant** (ou *tag*) : un entier à un offset proche (souvent juste avant la zone union) dont la valeur détermine quel « membre » de l'union est actif :

```c
if (*(int *)(param_1 + 4) == 0) {
    // accès int à param_1 + 8
} else if (*(int *)(param_1 + 4) == 1) {
    // accès float à param_1 + 8
} else {
    // accès char* à param_1 + 8
}
```

Ici, l'offset 0x04 est le tag, et l'offset 0x08 est une union.

### Créer l'union dans Ghidra

1. Data Type Manager → clic droit → **New → Union**.  
2. Nommez l'union (par exemple `value_u`).  
3. Ajoutez les membres avec leurs types respectifs (`int int_val`, `float float_val`, `char * str_val`).  
4. La taille de l'union est automatiquement celle du plus grand membre.

Vous pouvez ensuite inclure cette union comme champ d'une structure englobante :

```
struct tagged_value {
    int         type_tag;     // 0x00
    int         padding;      // 0x04
    value_u     value;        // 0x08 — union
};
```

---

## Types composites fréquents dans les binaires GCC

Certains types complexes apparaissent fréquemment dans les binaires compilés avec GCC/G++. Les reconnaître accélère considérablement l'analyse.

### Listes chaînées

Pattern caractéristique : un champ de type pointeur à un offset fixe qui pointe vers une structure du même type.

```c
struct node {
    int     data;        // 0x00
    int     padding;     // 0x04
    node *  next;        // 0x08
};
```

Dans le Decompiler, vous verrez une boucle qui suit les pointeurs :

```c
while (current != NULL) {
    // ... traitement de current->data ...
    current = *(long *)(current + 8);   // current = current->next
}
```

### Structures avec tableau flexible (flexible array member)

En C99/C11, une structure peut se terminer par un tableau de taille non spécifiée :

```c
struct message {
    int     length;
    char    data[];    // tableau flexible
};
```

L'allocation est typiquement `malloc(sizeof(struct message) + data_length)`. Dans le Decompiler, vous verrez un `malloc` dont l'argument est une somme d'une constante et d'une variable. Le tableau flexible commence juste après le dernier champ fixe.

Dans Ghidra, modélisez la partie fixe comme une structure normale. Le tableau flexible ne peut pas être représenté directement dans l'éditeur de structure (sa taille est variable), mais vous pouvez ajouter un champ `char[0] data` comme indicateur sémantique, ou simplement ajouter un commentaire.

### Types STL courants

Les conteneurs de la bibliothèque standard C++ (STL) ont des layouts mémoire prévisibles avec GCC/libstdc++. Le Chapitre 17 les détaillera exhaustivement, mais voici un aperçu pour la reconnaissance :

**`std::string`** (aussi appelé `std::basic_string<char>`) — Avec libstdc++, un `std::string` en mémoire occupe 32 octets sur x86-64 et contient un pointeur vers les données du buffer, la taille (length), et la capacité. Les chaînes courtes (15 caractères ou moins) utilisent l'optimisation SSO (Small String Optimization) et stockent les données directement dans l'objet sans allocation dynamique.

**`std::vector<T>`** — Un `std::vector` occupe 24 octets et contient trois pointeurs : début du buffer (`_M_start`), fin des données (`_M_finish`), et fin du buffer alloué (`_M_end_of_storage`). La taille est `finish - start`, la capacité est `end - start`.

Si vous reconnaissez ces patterns, vous pouvez créer les structures correspondantes dans le Data Type Manager ou importer les types depuis un header libstdc++.

---

## Bonnes pratiques de reconstruction

**Commencez par les structures les plus utilisées.** Une structure passée en paramètre à 20 fonctions aura un impact de propagation bien plus grand qu'une structure utilisée dans une seule fonction. Identifiez les « types centraux » du programme en cherchant quels pointeurs sont les plus fréquemment passés entre fonctions.

**Utilisez les constructeurs comme point de départ.** Les constructeurs (et les fonctions d'initialisation en C) accèdent généralement à tous les champs de la structure pour les initialiser. Ils sont le meilleur endroit pour obtenir une vue complète du layout.

**Itérez.** Vous ne reconstruirez pas une structure en un seul passage. Commencez par les champs que vous identifiez avec certitude, laissez les zones inconnues en `undefined`, et revenez les compléter au fur et à mesure que vous analysez d'autres fonctions.

**Nommez sémantiquement, pas structurellement.** Préférez `health` à `field_0x0c`, et `connection_state` à `int_at_offset_4`. Si vous ne connaissez pas encore le rôle d'un champ, un nom temporaire comme `field_0x0c_int` est acceptable, mais remplacez-le dès que vous avez une hypothèse.

**Documentez les incertitudes.** Utilisez la colonne **Comment** de l'éditeur de structure pour noter vos hypothèses et vos questions : « probablement un compteur de références », « pourrait être un flags bitfield », « TODO: vérifier en dynamique ».

**Comparez les tailles.** Si `malloc` alloue 0x40 octets et que votre structure n'en fait que 0x30, il manque des champs (ou du padding final). Vérifiez que la taille totale de votre structure correspond à la taille allouée.

**Relancez le Decompiler Parameter ID.** Après avoir créé et appliqué des structures significatives, relancez l'analyseur **Decompiler Parameter ID** (via **Analysis → Auto Analyze…**). Il propagera vos nouveaux types dans les fonctions appelantes et appelées, révélant potentiellement de nouvelles informations.

---

## Résumé

La reconstruction de structures de données est un processus itératif qui repose sur l'observation des patterns d'accès mémoire dans le code désassemblé. Ghidra fournit un éditeur de structures complet, une fonctionnalité d'auto-création qui accélère le travail initial, et un système de types qui propage les structures à travers tout le programme. Les classes C++ suivent les mêmes principes avec la couche supplémentaire du vptr et de l'héritage. Les enums et les unions complètent la palette en remplaçant les constantes magiques et en modélisant les champs à usage multiple. Chaque structure reconstruite et appliquée enrichit le pseudo-code du Decompiler et rend les fonctions voisines plus lisibles, créant un effet d'entraînement qui accélère l'analyse globale.

La section suivante aborde un outil essentiel pour naviguer dans les relations entre fonctions et données à l'échelle du programme entier : les cross-references (XREF).

---


⏭️ [Cross-references (XREF) : tracer l'usage d'une fonction ou d'une donnée](/08-ghidra/07-cross-references-xref.md)
