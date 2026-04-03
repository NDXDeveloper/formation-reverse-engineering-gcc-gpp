🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.4 — Renommage de fonctions et variables, ajout de commentaires, création de types

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Pourquoi annoter ?

L'analyse automatique de Ghidra produit un désassemblage structuré mais **anonyme**. Les fonctions s'appellent `FUN_004011a0`, les variables `local_28` ou `param_1`, les types sont `undefined8` ou `long`. Ce résultat est fonctionnellement correct — le décompileur montre fidèlement ce que fait le code — mais il est incompréhensible à l'échelle d'un programme entier. Identifier la logique métier d'un binaire en lisant uniquement du pseudo-code anonyme est comme lire un roman dont tous les personnages s'appellent « Personne A », « Personne B » et « Personne C ».

L'annotation est le processus par lequel vous transformez ce désassemblage brut en un document lisible et maintenable. C'est le cœur du travail de reverse engineering : chaque renommage, chaque commentaire, chaque type que vous définissez capture une part de votre compréhension et la rend exploitable pour la suite de l'analyse.

L'annotation n'est pas une étape séparée de l'analyse — elle en fait partie intégrante. Vous ne lisez pas d'abord tout le binaire puis annotez ensuite : vous annotez **au fil de l'eau**, chaque découverte étant immédiatement consignée dans le projet Ghidra. Cela présente un avantage majeur : les annotations se **propagent**. Renommer une fonction dans le Symbol Tree met à jour instantanément tous les `CALL` qui la référencent dans le Listing et toutes les invocations dans le Decompiler. Retyper un paramètre en `struct player_t *` transforme tous les accès à ses champs dans le pseudo-code. Chaque annotation amplifie la lisibilité globale.

---

## Renommage de fonctions

### Pourquoi renommer les fonctions en priorité

Le renommage des fonctions est l'action la plus rentable en termes de lisibilité. Une fonction nommée `FUN_00401230` ne dit rien ; la même fonction renommée `validate_license_key` transforme immédiatement la compréhension de toutes les fonctions qui l'appellent. Le pseudo-code d'un `main` qui appelle `FUN_00401230(param_1)` est opaque ; le même `main` qui appelle `validate_license_key(user_input)` raconte une histoire.

### Comment renommer

Plusieurs méthodes mènent au même résultat :

**Depuis le Listing** — Placez le curseur sur le label de la fonction (son nom au-dessus de la première instruction) et appuyez sur `L`. Un dialogue de saisie apparaît avec le nom actuel. Tapez le nouveau nom et validez.

**Depuis le Decompiler** — Cliquez sur le nom de la fonction en haut du pseudo-code (dans la signature), puis appuyez sur `L`. Même dialogue, même effet.

**Depuis le Symbol Tree** — Clic droit sur la fonction → **Rename**. Utile pour renommer sans naviguer vers la fonction.

**Depuis le menu** — Clic droit sur le nom de la fonction dans n'importe quel panneau → **Rename Function**.

### Conventions de nommage recommandées

Ghidra n'impose aucune convention, mais adopter un style cohérent dès le début vous évitera de la confusion à mesure que le nombre de fonctions renommées augmente :

- **snake_case** pour les fonctions C : `check_password`, `parse_header`, `send_response`. C'est la convention la plus courante dans le code C sous Linux et la plus naturelle à lire dans le Decompiler.  
- **CamelCase avec namespace** pour les fonctions C++ : si vous identifiez qu'une méthode appartient à une classe, utilisez la notation `ClassName::methodName`. Ghidra crée automatiquement le namespace correspondant dans le Symbol Tree.  
- **Préfixes fonctionnels** pour les fonctions dont vous comprenez le rôle général sans connaître les détails : `init_`, `cleanup_`, `handle_`, `process_`, `parse_`, `validate_`, `alloc_`, `free_`. Même un nom partiel comme `parse_something` est infiniment plus utile que `FUN_004015c0`.  
- **Préfixe `maybe_` ou `prob_`** pour les fonctions dont vous n'êtes pas certain du rôle : `maybe_decrypt_buffer`, `prob_auth_check`. Cela signale explicitement l'incertitude sans perdre l'hypothèse.

> 💡 **Ne cherchez pas la perfection** — Un nom approximatif est toujours meilleur que `FUN_XXXXXXXX`. Vous pouvez renommer une fonction autant de fois que vous le souhaitez au fil de votre compréhension. L'important est de capturer votre hypothèse actuelle.

### Renommage et propagation

Quand vous renommez une fonction, le changement se propage **immédiatement** dans :

- tous les `CALL` vers cette fonction dans le Listing ;  
- toutes les invocations dans le pseudo-code du Decompiler (dans la fonction elle-même et dans toutes les fonctions appelantes) ;  
- le Symbol Tree ;  
- les cross-references ;  
- les résultats de recherche.

Cette propagation automatique est cumulative. Renommer 10 fonctions clés peut rendre lisible le pseudo-code de dizaines de fonctions appelantes sans aucune autre modification.

---

## Renommage de variables et de paramètres

### Variables locales

Le décompileur nomme les variables locales selon leur position sur la pile (`local_28`, `local_10`) ou leur rôle déduit (`iVar1`, `pVar2`). Ces noms sont opaques. Renommez-les dès que vous comprenez leur usage.

**Depuis le Decompiler** — Cliquez sur la variable, appuyez sur `L`, saisissez le nouveau nom. Le renommage se propage dans tout le pseudo-code de la fonction courante.

**Depuis le Listing** — Cliquez sur la référence à la variable locale (affichée comme un offset de pile, par exemple `[RBP + local_28]`), appuyez sur `L`.

Le renommage d'une variable locale est **local à la fonction** — il n'affecte que le pseudo-code de la fonction où la variable est définie.

### Paramètres de fonctions

Les paramètres (`param_1`, `param_2`…) se renomment exactement comme les variables locales, mais avec un impact plus large. Quand vous renommez `param_1` en `filename` dans la fonction `open_config_file`, le Decompiler met à jour la signature affichée de cette fonction. Toute fonction appelante qui passe un argument à cette position verra la correspondance dans le contexte du `CALL`, facilitant la compréhension du flux de données.

Pour renommer un paramètre, vous pouvez aussi passer par l'édition de la signature complète de la fonction (voir « Modifier la signature d'une fonction » plus loin dans cette section).

### Variables globales

Les variables globales (situées dans `.data`, `.bss` ou `.rodata`) sont nommées par défaut selon leur adresse (`DAT_00404020`). Renommez-les via le Listing en plaçant le curseur sur le label de la donnée et en appuyant sur `L`.

Le renommage d'une variable globale est **global au programme** : il se propage dans toutes les fonctions qui y accèdent, dans le Listing comme dans le Decompiler. Renommer `DAT_00404020` en `g_config_buffer` (par convention, le préfixe `g_` signale une variable globale) clarifie instantanément tout le code qui la manipule.

---

## Ajout de commentaires

Les commentaires sont votre journal de bord d'analyse. Ils capturent vos hypothèses, vos observations et vos questions pour plus tard. Ghidra propose plusieurs types de commentaires, chacun adapté à un usage différent.

### Types de commentaires

**EOL Comment (End Of Line)** — Commentaire en fin de ligne, affiché à droite de l'instruction dans le Listing. C'est le type le plus courant, utilisé pour des annotations brèves sur une instruction spécifique.

- Raccourci : `;` (touche point-virgule)  
- Usage typique : expliquer le rôle d'une instruction, noter une valeur concrète observée en dynamique, signaler un pattern reconnu.

Exemple :

```
00401189  cmp  DWORD PTR [rbp-0x4], 0x5    ; compare le compteur de boucle à 5
```

**Pre Comment** — Commentaire affiché **au-dessus** de l'instruction, sur sa propre ligne. Utilisé pour des blocs explicatifs plus longs qui s'appliquent à un groupe d'instructions.

- Raccourci : `Ctrl+;`  
- Usage typique : décrire le début d'un bloc logique (« Ici commence la vérification du checksum »), documenter un algorithme complexe, noter une séquence d'instructions qui forme un idiome connu.

**Post Comment** — Commentaire affiché **en dessous** de l'instruction. Moins courant, utilisé pour noter les conséquences d'une instruction ou ce qui se passe après son exécution.

- Raccourci : clic droit → **Comments → Set Post Comment**

**Plate Comment** — Commentaire affiché dans un encadré au-dessus du label d'une fonction, comme un bloc de documentation. C'est l'équivalent d'un commentaire Doxygen pour une fonction désassemblée.

- Raccourci : clic droit → **Comments → Set Plate Comment**  
- Usage typique : résumé du rôle de la fonction, description des paramètres, notes sur le comportement observé.

Exemple de plate comment :

```
/****************************************
 * Vérifie si la clé de licence passée
 * en paramètre est valide.
 * param_1 : pointeur vers la chaîne clé
 * Retourne 1 si valide, 0 sinon.
 * Algorithme : XOR roulant sur chaque
 * caractère avec seed 0x42.
 ****************************************/
```

**Repeatable Comment** — Commentaire qui se propage automatiquement à tous les endroits qui référencent cette adresse. Si vous placez un repeatable comment sur une variable globale, il apparaîtra à côté de chaque instruction qui accède à cette variable. Puissant mais à utiliser avec parcimonie pour éviter le bruit visuel.

- Raccourci : clic droit → **Comments → Set Repeatable Comment**

### Commentaires dans le Decompiler

Le Decompiler affiche les commentaires Pre et EOL associés aux instructions assembleur correspondantes, intégrés dans le pseudo-code. Vous pouvez aussi ajouter des commentaires directement depuis le Decompiler via clic droit → **Comments**, mais ces commentaires sont en réalité attachés aux instructions assembleur sous-jacentes.

### Stratégie de commentaire

Quelques principes pour des commentaires utiles :

- **Commentez le « pourquoi », pas le « quoi »** — L'instruction `CMP EAX, 0x10` est déjà lisible. Ce qui a de la valeur, c'est « compare la longueur de l'input à 16, la taille attendue d'une clé AES-128 ».  
- **Utilisez les plate comments pour les résumés de fonctions** — C'est la première chose que vous lirez quand vous revisiterez une fonction des jours plus tard.  
- **Notez vos incertitudes** — Un commentaire comme « TODO: vérifier si c'est du RC4 ou un XOR custom » est précieux. Il vous évite de refaire le raisonnement plus tard.  
- **Marquez les points d'intérêt dynamique** — « Poser un breakpoint ici pour capturer la clé en clair » prépare la phase d'analyse dynamique (Partie III).

---

## Modification de la signature d'une fonction

La signature d'une fonction — son type de retour, son nom, et la liste typée de ses paramètres — est l'annotation la plus structurante que vous puissiez apporter. Elle conditionne la qualité du pseudo-code non seulement dans la fonction elle-même, mais dans toutes les fonctions qui l'appellent.

### Accéder à l'éditeur de signature

- Depuis le Decompiler : clic droit sur le nom de la fonction → **Edit Function Signature** (ou touche `F`).  
- Depuis le Listing : clic droit sur le label de la fonction → **Edit Function**.

L'éditeur affiche un champ texte contenant la signature actuelle dans un format proche du C :

```c
undefined8 FUN_004011a0(undefined8 param_1, undefined4 param_2)
```

Vous pouvez modifier directement cette ligne comme du code C :

```c
int check_password(char * user_input, int max_length)
```

En validant, Ghidra met à jour :

- le nom de la fonction dans le Symbol Tree et le Listing ;  
- le type de retour (ici `undefined8` → `int`) ;  
- les noms et types des paramètres ;  
- le pseudo-code du Decompiler, qui reflète immédiatement les nouveaux types.

### Options avancées de la signature

L'éditeur complet (accessible via le bouton **Edit** ou via **Function → Edit Function** dans le menu) offre des options supplémentaires :

**Convention d'appel (Calling Convention)** — Par défaut `__stdcall` pour System V AMD64 (malgré le nom hérité de Windows, Ghidra l'utilise comme convention par défaut sur x86-64 Linux). Vous n'avez généralement pas à la modifier, sauf si vous rencontrez une fonction qui utilise une convention non standard (par exemple, une fonction assembleur écrite à la main qui passe des paramètres par la pile plutôt que par les registres).

**Inline / No Return** — Deux attributs booléens. `Inline` indique à Ghidra que la fonction est destinée à être inlinée (rarement utile en RE). `No Return` indique que la fonction ne retourne jamais (comme `exit`, `abort`, `__stack_chk_fail`). Cocher `No Return` améliore la précision du graphe de flux de contrôle en empêchant Ghidra de traiter le code après un `CALL` à cette fonction comme du code atteignable.

**Custom Storage** — Permet de spécifier manuellement dans quels registres ou emplacements de pile chaque paramètre est passé. Utile quand Ghidra se trompe sur l'assignation des paramètres aux registres, ce qui peut arriver avec des fonctions variadiques, des fonctions issues de code assembleur inline, ou des conventions d'appel non standard.

### Impact de la signature sur le Decompiler

La signature est l'information qui a le plus d'impact sur la qualité du pseudo-code. Prenons un exemple concret. Avant correction de la signature :

```c
undefined8 FUN_004011a0(undefined8 param_1)
{
    undefined4 uVar1;
    uVar1 = *(undefined4 *)(param_1 + 0x10);
    if (*(int *)(param_1 + 0xc) < 100) {
        *(int *)(param_1 + 0xc) = *(int *)(param_1 + 0xc) + uVar1;
    }
    return 0;
}
```

Après création d'une structure `player_t` (section 8.6) et modification de la signature :

```c
int update_health(player_t * player)
{
    int heal_amount;
    heal_amount = player->heal_rate;
    if (player->health < 100) {
        player->health = player->health + heal_amount;
    }
    return 0;
}
```

Le code est fonctionnellement identique, mais la seconde version est immédiatement compréhensible. Tout le travail réside dans la définition de la structure et la correction de la signature — le Decompiler fait le reste.

---

## Création et application de types

### Les types dans Ghidra

Le système de types de Ghidra est l'un de ses atouts les plus puissants pour transformer un désassemblage opaque en pseudo-code lisible. Un « type » dans Ghidra peut être :

- un **type primitif** : `int`, `long`, `char`, `float`, `double`, `void *`, etc. ;  
- un **typedef** : un alias pour un autre type (`typedef unsigned int uint32_t`) ;  
- un **enum** : un ensemble de constantes nommées ;  
- une **structure** (`struct`) : un agencement de champs à offsets fixes ;  
- une **union** : des champs qui partagent le même emplacement mémoire ;  
- un **pointeur de fonction** : un type décrivant la signature d'un callback.

### Où gérer les types : le Data Type Manager

Le Data Type Manager (**Window → Data Type Manager**) est l'interface centrale pour créer, modifier, importer et appliquer des types. Il affiche une arborescence de catégories :

- **BuiltInTypes** — types primitifs, non modifiables ;  
- **Programme en cours** (le nom de votre binaire) — les types spécifiques à ce binaire. C'est ici que vous créerez vos structures personnalisées ;  
- **Archives** — les archives de types (`.gdt`) chargées. L'archive `generic_clib` est particulièrement utile, car elle contient les signatures de centaines de fonctions de la libc standard.

### Créer un typedef

Un typedef est utile pour remplacer les types génériques de Ghidra par des noms sémantiques. Par exemple, si vous observez qu'un `undefined4` est systématiquement utilisé comme identifiant de joueur, créez un typedef :

1. Dans le Data Type Manager, clic droit sur la catégorie de votre programme → **New → Typedef**.  
2. Nom : `player_id_t`.  
3. Type de base : `uint` (ou `unsigned int`).  
4. Validez.

Vous pouvez ensuite appliquer ce type aux variables et paramètres dans le Decompiler via `T` (Retype).

### Créer un enum

Les énumérations sont précieuses pour remplacer les constantes magiques par des noms significatifs. Quand vous voyez dans le pseudo-code des comparaisons comme `if (param_2 == 1)` … `else if (param_2 == 2)` … `else if (param_2 == 3)`, il y a probablement une énumération sous-jacente.

1. Dans le Data Type Manager, clic droit sur la catégorie de votre programme → **New → Enum**.  
2. Nommez l'enum (par exemple `command_type_e`).  
3. Ajoutez les valeurs : `CMD_PING = 0`, `CMD_AUTH = 1`, `CMD_DATA = 2`, `CMD_QUIT = 3`.  
4. Définissez la taille (1, 2, 4 ou 8 octets) selon l'usage observé dans le binaire.  
5. Validez.

Appliquez ensuite ce type au paramètre ou à la variable concernée dans le Decompiler. Le pseudo-code passe de `if (param_2 == 1)` à `if (command == CMD_AUTH)`, ce qui est considérablement plus lisible.

### Créer une structure (struct)

La création de structures est l'opération de typage la plus transformatrice. Elle est détaillée dans la section 8.6, mais voici le principe de base.

Quand le Decompiler montre des accès répétés à des offsets d'un pointeur :

```c
*(int *)(param_1 + 0)     // offset 0x00
*(int *)(param_1 + 4)     // offset 0x04
*(char *)(param_1 + 8)    // offset 0x08
*(long *)(param_1 + 0x10) // offset 0x10
```

Cela indique qu'il y a une structure dont les champs se situent à ces offsets. Vous pouvez la créer :

1. Data Type Manager → clic droit sur votre programme → **New → Structure**.  
2. Nommez-la (par exemple `config_t`).  
3. L'**éditeur de structure** s'ouvre. Ajoutez les champs un par un :  
   - Offset `0x00` : `int id`  
   - Offset `0x04` : `int flags`  
   - Offset `0x08` : `char name[8]`  
   - Offset `0x10` : `long timestamp`  
4. Validez.

Appliquez ensuite le type `config_t *` au paramètre dans le Decompiler. Ghidra remplace automatiquement tous les accès par offset par des accès nommés aux champs de la structure.

> 💡 **Raccourci depuis le Decompiler** — Au lieu de créer la structure entièrement à la main, vous pouvez utiliser la fonctionnalité **Auto Create Structure** : clic droit sur un paramètre de type pointeur dans le Decompiler → **Auto Create Structure**. Ghidra analyse tous les accès à ce pointeur dans la fonction et génère une structure avec les champs déduits. Le résultat n'est pas parfait (les noms de champs sont génériques comme `field_0x4`), mais la structure des offsets est correcte. Vous pouvez ensuite la raffiner en renommant les champs.

### Appliquer un type à une donnée dans le Listing

Dans le Listing, vous pouvez appliquer un type à une adresse de données (dans `.data`, `.rodata`, `.bss`) en positionnant le curseur sur l'adresse et en appuyant sur `T`. Un dialogue vous permet de choisir le type dans le Data Type Manager.

C'est utile pour typer des variables globales, des tableaux de structures, ou des constantes dont le format n'a pas été détecté automatiquement. Par exemple, appliquer le type `char[256]` à une zone de `.bss` transforme 256 octets `undefined1` individuels en un buffer nommé et cohérent.

### Importer des types depuis un fichier header C

Quand vous connaissez une partie des structures du programme (par exemple, si le binaire utilise une bibliothèque dont les headers sont publics), vous pouvez les importer directement :

1. **File → Parse C Source…** dans le CodeBrowser.  
2. Collez ou chargez le contenu du fichier `.h`.  
3. Ghidra parse les définitions C et ajoute les types résultants au Data Type Manager.

Cela fonctionne avec les structures, les enums, les typedefs et les signatures de fonctions. C'est un gain de temps considérable quand le binaire utilise des bibliothèques connues (OpenSSL, SQLite, protobuf, etc.) dont les headers sont disponibles publiquement.

> ⚠️ **Limitation** — Le parseur C de Ghidra ne supporte pas l'intégralité du langage C/C++. Les macros complexes, les templates C++, et certaines constructions GCC-spécifiques peuvent échouer. Dans ce cas, simplifiez le header en ne gardant que les structures et les typedefs pertinents avant de l'importer.

### Archives de types (`.gdt`)

Ghidra utilise des archives de types au format `.gdt` (Ghidra Data Types) pour stocker des ensembles de types réutilisables d'un projet à l'autre. Plusieurs archives sont fournies :

- `generic_clib.gdt` — types et signatures de la libc standard ;  
- `generic_clib_64.gdt` — variante 64 bits ;  
- `windows_*.gdt` — types de l'API Windows (utiles si vous analysez un binaire PE compilé avec MinGW).

Vous pouvez créer vos propres archives pour les types que vous définissez fréquemment. Dans le Data Type Manager, clic droit sur une catégorie → **Save As → Archive**. Cette archive peut ensuite être importée dans d'autres projets, ce qui est particulièrement utile si vous analysez plusieurs binaires d'un même logiciel qui partagent les mêmes structures de données.

---

## Propagation et cohérence

L'un des aspects les plus satisfaisants de l'annotation dans Ghidra est la **cascade de propagation**. Voici un exemple concret de la réaction en chaîne qu'une seule annotation peut déclencher :

1. Vous créez une structure `packet_header_t` avec les champs `magic`, `version`, `payload_size`, `command`.  
2. Vous identifiez la fonction `FUN_00401500` comme celle qui parse les paquets réseau. Vous la renommez `parse_packet` et modifiez sa signature pour que `param_1` soit de type `packet_header_t *`.  
3. Le Decompiler de `parse_packet` montre maintenant des accès nommés : `header->magic`, `header->command`, `header->payload_size`.  
4. La fonction `FUN_00401700` appelle `parse_packet`. Dans le Decompiler de `FUN_00401700`, l'argument passé à `parse_packet` est maintenant typé comme `packet_header_t *`, et les accès à cette variable avant l'appel montrent aussi les noms de champs.  
5. Vous renommez `FUN_00401700` en `handle_connection`. Son pseudo-code est maintenant largement lisible sans aucune autre annotation dans cette fonction.

Chaque annotation crée un effet domino. C'est pourquoi il est rentable d'investir du temps dans la création de bonnes structures et la correction des signatures de fonctions clés — l'effort se multiplie à travers le programme.

---

## Undo / Redo

Toutes les modifications que vous apportez — renommages, commentaires, types, signatures — sont **réversibles**. Ghidra maintient un historique complet d'annulation :

- **`Ctrl+Z`** — annuler la dernière modification ;  
- **`Ctrl+Shift+Z`** (ou `Ctrl+Y`) — rétablir une modification annulée.

L'historique d'annulation est persistant au sein d'une session. Si vous fermez et rouvrez le projet, les modifications sont sauvegardées mais l'historique d'annulation est perdu. Si vous souhaitez pouvoir revenir à un état antérieur après fermeture, utilisez les **versions de fichier** : clic droit sur le binaire dans le Project Manager → **Check In** pour créer un point de sauvegarde versionné.

---

## Résumé

L'annotation est le processus qui transforme un désassemblage anonyme en un document technique exploitable. Les trois piliers de cette transformation sont le renommage (fonctions, variables, données globales), les commentaires (EOL, Pre, Plate, Repeatable) et le système de types (typedefs, enums, structures, import de headers, archives `.gdt`). Chaque annotation se propage automatiquement dans le Listing, le Decompiler et le Symbol Tree, créant un effet cumulatif où chaque ajout enrichit la lisibilité globale du projet.

La section suivante aborde un cas d'application direct de ces compétences : la reconnaissance et l'annotation des structures spécifiques à GCC dans les binaires C++ — vtables, RTTI et tables d'exceptions.

---


⏭️ [Reconnaître les structures GCC : vtables C++, RTTI, exceptions](/08-ghidra/05-structures-gcc-vtables-rtti.md)
