🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.2 — Import d'un binaire ELF — analyse automatique et options

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Ce qui se passe quand vous importez un binaire

L'import dans Ghidra n'est pas une simple opération de copie. C'est un processus en deux phases distinctes qui transforme un fichier brut sur disque en une base de données structurée prête à être explorée :

1. **Le chargement (loading)** — Ghidra lit le fichier, identifie son format (ELF, PE, Mach-O, raw…), parse les headers, mappe les sections en mémoire virtuelle, résout les symboles disponibles et reconstruit la table d'imports/exports. À la fin de cette phase, vous disposez d'un espace d'adresses virtuel peuplé d'octets bruts, avec les métadonnées structurelles du format.

2. **L'analyse automatique** — Ghidra lance une batterie d'analyseurs qui transforment ces octets bruts en code désassemblé annoté. C'est cette phase qui identifie les fonctions, détecte les chaînes de caractères, résout les références croisées, reconstruit les signatures de fonctions, et alimente le décompileur. La qualité de cette analyse conditionne directement la lisibilité du résultat.

Comprendre ces deux phases est essentiel, car elles offrent chacune des options de configuration qui influencent significativement la qualité de l'analyse finale.

---

## Phase 1 : la boîte de dialogue d'import

Lorsque vous importez un fichier via **File → Import File…** (ou par glisser-déposer dans le Project Manager), Ghidra affiche une boîte de dialogue d'import comportant plusieurs champs configurables.

### Format

Ghidra détecte automatiquement le format du fichier grâce à ses magic bytes. Pour un binaire ELF produit par GCC, il affiche **Executable and Linking Format (ELF)**. Ce champ est un menu déroulant qui propose d'autres loaders si la détection automatique échoue ou si vous souhaitez forcer un format particulier.

Les formats que vous rencontrerez dans ce tutoriel sont principalement :

- **ELF** — les binaires Linux natifs produits par GCC/G++, le cas largement majoritaire de cette formation ;  
- **PE** — les binaires Windows, que vous pourriez croiser si vous compilez avec MinGW (mentionné au Chapitre 2) ;  
- **Raw Binary** — utile lorsque le fichier n'a pas de headers reconnus (firmware, dump mémoire, shellcode). Ghidra charge alors les octets bruts sans interprétation structurelle.

Dans la quasi-totalité des cas de ce tutoriel, la détection automatique est correcte et vous n'avez rien à modifier.

### Language / Architecture

Ce champ détermine le **processeur** et le **jeu d'instructions** que Ghidra utilisera pour le désassemblage. Pour un binaire ELF x86-64, Ghidra propose automatiquement :

```
x86:LE:64:default (gcc)
```

Décomposons cette notation :

- `x86` — famille de processeur Intel/AMD ;  
- `LE` — Little Endian (l'ordre des octets en mémoire, standard sur x86) ;  
- `64` — taille du mot (64 bits, c'est-à-dire l'architecture AMD64/x86-64) ;  
- `default` — variante du langage machine (Ghidra supporte parfois plusieurs variantes pour une même architecture) ;  
- `(gcc)` — le *compiler spec*, c'est-à-dire la convention d'appel. `gcc` correspond à la convention System V AMD64 ABI que nous avons étudiée au Chapitre 3.

Ce dernier point mérite une attention particulière. Le *compiler spec* indique à Ghidra comment interpréter les passages de paramètres et les valeurs de retour. Si le binaire a été compilé avec GCC sous Linux, `gcc` est le bon choix — et c'est ce que Ghidra sélectionne par défaut en détectant le format ELF. Pour un binaire PE Windows compilé avec MSVC, le compiler spec serait `windows`, qui correspond à la convention d'appel Microsoft x64.

> ⚠️ **Piège classique** — Si vous analysez un binaire 32 bits (compilé avec `gcc -m32`), Ghidra doit détecter `x86:LE:32:default`. Si par erreur vous forcez le mode 64 bits sur un binaire 32 bits, le désassemblage sera incohérent : les instructions seront mal décodées, les registres auront des noms incorrects et le décompileur produira du pseudo-code absurde. Vérifiez toujours la cohérence avec la sortie de `file` :  
> ```bash  
> file keygenme_O0  
> # keygenme_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), ...  
> ```

### Destination Folder

Le dossier dans l'arborescence du projet Ghidra où sera placé le binaire importé. Par défaut, il est placé à la racine du projet. Vous pouvez créer des sous-dossiers pour organiser vos binaires par chapitre ou par variante d'optimisation.

### Options d'import (bouton « Options… »)

Le bouton **Options…** en bas de la boîte de dialogue d'import donne accès à des réglages avancés spécifiques au loader ELF. Les plus utiles pour notre contexte sont les suivants.

#### Image Base

L'adresse de base à laquelle le binaire sera mappé en mémoire dans l'espace d'adresses de Ghidra. Pour un binaire ELF non-PIE, cette adresse est fixée par le linker (typiquement `0x400000` pour un exécutable x86-64 classique). Pour un binaire PIE (Position Independent Executable), le loader ELF de Ghidra choisit une adresse de base par défaut (souvent `0x100000`).

Dans la plupart des cas, laissez la valeur par défaut. Vous pourriez vouloir la modifier si vous analysez un dump mémoire et que vous connaissez l'adresse de chargement réelle, ou si vous comparez deux binaires et souhaitez qu'ils soient mappés à la même base.

#### Load External Libraries

Cette option contrôle si Ghidra tente de charger les bibliothèques partagées référencées par le binaire (comme `libc.so.6`, `libstdc++.so`, `libm.so`). Par défaut, elle est **désactivée**, et c'est généralement le bon choix pour nos exercices.

Charger les bibliothèques externes a l'avantage de résoudre les symboles importés et de permettre au décompileur de connaître les signatures exactes des fonctions de la libc. Mais cela alourdit considérablement le projet (la libc seule contient des milliers de fonctions) et augmente le temps d'analyse. Pour nos binaires d'entraînement, Ghidra dispose déjà de signatures intégrées pour les fonctions courantes de la libc (`printf`, `malloc`, `strcmp`, etc.) grâce à ses fichiers de types (Data Type Archives), ce qui rend le chargement externe généralement superflu.

#### Apply Processor/Loader-Defined Labels

Active l'application des labels issus des tables de symboles ELF (`.symtab`, `.dynsym`). Cette option doit rester **activée** — c'est elle qui permet à Ghidra de nommer les fonctions avec leurs vrais noms quand le binaire n'est pas strippé.

---

## Phase 2 : l'analyse automatique

Une fois l'import terminé, Ghidra affiche un résumé (nombre de sections chargées, plages d'adresses, symboles détectés) puis propose de lancer l'**Auto Analysis**. C'est ici que le binaire brut se transforme en un désassemblage structuré et navigable.

### La boîte de dialogue « Analysis Options »

En cliquant sur **Yes** pour lancer l'analyse, Ghidra ouvre une fenêtre listant tous les **analyseurs** disponibles, chacun accompagné d'une case à cocher et parfois d'options de configuration propres. La liste est longue — Ghidra 11.x propose plusieurs dizaines d'analyseurs. Vous n'avez pas besoin de comprendre chacun d'entre eux pour être efficace. Concentrons-nous sur ceux qui ont le plus d'impact sur l'analyse de binaires ELF x86-64 compilés avec GCC.

### Les analyseurs clés

#### ASCII Strings

Parcourt l'intégralité de l'espace d'adresses à la recherche de séquences d'octets qui ressemblent à des chaînes de caractères ASCII (et UTF-8/UTF-16). Chaque chaîne détectée est typée comme `string` dans le listing et devient consultable via **Window → Defined Strings**.

Cet analyseur est l'équivalent automatisé de la commande `strings` que vous avez utilisée au Chapitre 5, mais intégré dans le contexte du désassemblage : les chaînes sont directement liées aux instructions qui les référencent.

Les options de configuration de cet analyseur permettent de régler la longueur minimale des chaînes (par défaut 5 caractères), le jeu de caractères à détecter, et si l'analyseur doit créer des chaînes uniquement dans les sections de données ou aussi dans les sections de code.

> 💡 **Conseil** — Pour un premier triage, les options par défaut conviennent. Si vous constatez après l'analyse que des chaînes courtes mais significatives (comme des noms de commandes de 3-4 caractères dans un protocole réseau) n'ont pas été détectées, vous pourrez relancer cet analyseur seul avec un seuil plus bas (voir « Relancer une analyse ciblée » plus loin).

#### Decompiler Parameter ID

Cet analyseur utilise le décompileur de Ghidra pour déduire les **types des paramètres** de chaque fonction et propager cette information dans le listing. C'est l'un des analyseurs les plus puissants : il permet au décompileur de produire du pseudo-code avec des signatures de fonctions typées plutôt qu'un flux d'opérations sur des registres anonymes.

Par défaut, cet analyseur est **activé** dans les versions récentes de Ghidra. Vérifiez qu'il est bien coché. Son exécution rallonge sensiblement le temps d'analyse (il invoque le décompileur sur chaque fonction détectée), mais le gain en lisibilité est considérable.

#### ELF Scalar Operand References

Spécifique aux binaires ELF. Cet analyseur tente de résoudre les opérandes scalaires (constantes numériques dans les instructions) en références vers des adresses connues. Par exemple, si une instruction `mov` charge une constante qui correspond à l'adresse d'une chaîne dans `.rodata`, cet analyseur créera une référence explicite, rendant le listing beaucoup plus lisible.

#### Function Start Search

Recherche des débuts de fonctions que l'analyseur principal n'aurait pas détectés. Il utilise des heuristiques basées sur les patterns de prologue typiques (`push rbp ; mov rbp, rsp` en `-O0`, `endbr64` avec CET activé, etc.) et sur les alignements de fonctions.

Cet analyseur est particulièrement utile sur les binaires **strippés**, où la table de symboles ne fournit aucune information sur les frontières de fonctions. Ghidra doit alors s'appuyer entièrement sur l'analyse du flux de contrôle et sur ces heuristiques pour délimiter les fonctions.

#### GCC Exception Handlers

Parse les sections `.eh_frame` et `.gcc_except_table` produites par GCC pour la gestion des exceptions C++. Cet analyseur est crucial si vous analysez un binaire C++ : il reconstruit les relations entre les blocs `try`/`catch` et les fonctions de nettoyage (destructeurs appelés lors du déroulage de la pile).

Si vous analysez un binaire C pur (comme `keygenme`), cet analyseur est inoffensif — il ne trouvera simplement rien à traiter.

#### Stack

Analyse les prologues et épilogues de fonctions pour reconstruire le **layout de la pile** (stack frame) de chaque fonction : variables locales, paramètres empilés, zones de sauvegarde des registres. Le résultat apparaît comme des variables nommées `local_XX` et `param_X` dans le décompileur.

C'est l'un des analyseurs les plus critiques pour la lisibilité. Sans lui, le décompileur ne montrerait que des accès bruts à `RSP+offset`.

#### Demangler GNU

Applique le démanglement des symboles C++ selon les conventions Itanium ABI utilisées par GCC/G++ (les mêmes règles que `c++filt`, abordé au Chapitre 7). Transforme par exemple `_ZN6Animat5speakEv` en `Animal::speak(void)`.

Sans cet analyseur, les noms de fonctions C++ resteraient sous leur forme manglée, rendant la navigation dans le Symbol Tree laborieuse.

### Analyseurs de moindre impact immédiat

Certains analyseurs sont utiles dans des contextes spécifiques mais moins critiques pour une première analyse :

- **Aggressive Instruction Finder** — recherche du code dans des zones non référencées. Utile pour détecter du code mort ou du code obfusqué (Chapitre 19), mais peut générer du bruit en créant de fausses fonctions à partir de données interprétées comme des instructions. Désactivé par défaut, et c'est généralement le bon choix pour une première passe.  
- **DWARF** — parse les informations de débogage DWARF si le binaire a été compilé avec `-g`. Récupère les noms de variables locales, les numéros de ligne, les types des structures et des paramètres tels que définis dans le code source. Extrêmement précieux quand il est disponible — c'est comme avoir une partie du code source intégrée dans le binaire. Cet analyseur est actif par défaut et s'exécute automatiquement quand des sections DWARF sont détectées.  
- **Condense Filler Bytes** — regroupe les séquences de `NOP` (padding d'alignement entre fonctions) en blocs annotés plutôt que de les lister instruction par instruction. Purement cosmétique mais améliore la lisibilité.  
- **Non-Returning Functions** — identifie les fonctions qui ne retournent jamais (`exit`, `abort`, `__stack_chk_fail`, `__cxa_throw`…). Important pour la précision du graphe de flux de contrôle : sans cette information, Ghidra pourrait croire que du code suit un appel à `exit()` et tenter de le désassembler.

---

## Le processus d'analyse en détail

### Ordre d'exécution et dépendances

Les analyseurs ne s'exécutent pas en parallèle de manière désordonnée. Ghidra les orchestre selon un système de **priorités** et de **dépendances**. Par exemple :

1. Le loader ELF résout d'abord les symboles et mappe les sections.  
2. L'analyseur de chaînes identifie les strings dans `.rodata`.  
3. L'analyse de flux de contrôle identifie les fonctions.  
4. Le Demangler GNU transforme les noms manglés.  
5. L'analyseur Stack reconstruit les frames.  
6. Le Decompiler Parameter ID affine les types.

Vous n'avez pas besoin de connaître l'ordre exact, mais cette séquence explique pourquoi certaines informations n'apparaissent qu'après la fin complète de l'analyse. Si vous naviguez dans le binaire pendant que l'analyse tourne (c'est possible — le CodeBrowser est utilisable pendant l'analyse), vous constaterez que les noms de fonctions, les types et les cross-references s'enrichissent progressivement.

### Indicateur de progression

Pendant l'analyse, une barre de progression apparaît en bas à droite du CodeBrowser, accompagnée du nom de l'analyseur en cours d'exécution. Sur un petit binaire comme `keygenme_O0` (~15 Ko), l'analyse complète prend quelques secondes. Sur un binaire C++ conséquent avec STL et templates (~1-5 Mo), comptez quelques minutes. Sur un très gros binaire (serveur complet, jeu vidéo, firmware), l'analyse peut durer de longues minutes voire des dizaines de minutes.

Vous pouvez interrompre l'analyse en cours via le bouton d'annulation à côté de la barre de progression. L'analyse partielle est conservée — vous ne perdez pas le travail déjà réalisé.

### Relancer une analyse ciblée

Il est fréquent de vouloir relancer un ou plusieurs analyseurs après l'analyse initiale, par exemple :

- après avoir renommé des fonctions et ajouté des types (le Decompiler Parameter ID peut alors produire de meilleurs résultats en second passage) ;  
- après avoir modifié des options d'un analyseur (par exemple, abaisser la longueur minimale des chaînes) ;  
- après avoir identifié manuellement de nouvelles fonctions que l'analyse initiale avait manquées.

Pour relancer l'analyse, utilisez **Analysis → Auto Analyze…** depuis le CodeBrowser. La même boîte de dialogue d'options apparaît. Vous pouvez décocher tous les analyseurs sauf celui que vous souhaitez relancer, puis cliquer sur **Analyze**.

> 💡 **Conseil pratique** — Si vous avez substantiellement annoté le binaire (renommage de dizaines de fonctions, création de types de structures), relancez le **Decompiler Parameter ID** seul. Il bénéficiera de vos annotations pour propager les types de manière plus précise dans les fonctions appelantes et appelées.

---

## Impact du niveau d'optimisation sur l'analyse

Le comportement de l'analyse automatique varie notablement selon le niveau d'optimisation avec lequel le binaire a été compilé. Comprendre ces différences vous évitera de la frustration.

### Binaire `-O0` (sans optimisation)

C'est le cas le plus favorable pour l'analyse. Le code généré par GCC suit fidèlement la structure du code source : chaque variable locale a sa place sur la pile, chaque fonction est présente en tant qu'entité distincte, les branchements correspondent clairement aux structures `if`/`else`/`for`/`while` du code original.

L'analyse automatique de Ghidra produit un résultat très lisible :

- les fonctions sont correctement délimitées ;  
- les variables locales sont identifiées avec des offsets réguliers sur la pile ;  
- le décompileur produit du pseudo-code proche du source original ;  
- les paramètres sont correctement associés aux registres de la convention d'appel.

### Binaire `-O2` / `-O3` (avec optimisations)

L'optimisation transforme profondément la structure du code (nous y consacrerons le Chapitre 16 entier). Les effets les plus visibles sur l'analyse Ghidra sont :

- **Fonctions inlinées** — des fonctions courtes disparaissent du binaire, leur code étant intégré directement dans l'appelant. Le Symbol Tree contient moins d'entrées, et le décompileur montre un code plus dense mais moins modulaire.  
- **Variables en registres** — l'optimiseur évite d'utiliser la pile quand les registres suffisent. L'analyseur Stack a moins de matière, et le décompileur peut montrer des variables « fantômes » qui n'existent que dans un registre.  
- **Réordonnancement des instructions** — les instructions ne suivent plus l'ordre du code source. Les sauts conditionnels sont réorganisés. Le flux de contrôle peut sembler contre-intuitif.  
- **Tail call optimization** — un `call` + `ret` est remplacé par un simple `jmp`. Ghidra peut interpréter cela comme un saut interne plutôt qu'un appel de fonction, fusionnant visuellement deux fonctions distinctes.

Le décompileur produit un résultat fonctionnellement correct mais structurellement éloigné du source original. C'est normal et attendu.

### Binaire strippé (`-s` ou post-traitement `strip`)

Le stripping supprime les tables de symboles (`.symtab`) et les informations de débogage (sections DWARF). L'impact sur l'analyse Ghidra est significatif :

- **Noms de fonctions perdus** — toutes les fonctions non exportées apparaissent comme `FUN_00401234` (préfixe `FUN_` suivi de l'adresse). Seules les fonctions importées dynamiquement conservent leurs noms (car ils sont nécessaires au linker dynamique et restent dans `.dynsym`).  
- **Frontières de fonctions incertaines** — sans symboles, Ghidra s'appuie sur l'analyse de flux et les heuristiques de détection de prologue. Dans un binaire `-O0` strippé, les prologues `push rbp ; mov rbp, rsp` sont fiables. Dans un binaire `-O2` strippé, les fonctions sans prologue classique (fonctions *leaf* qui n'utilisent pas la pile) peuvent être manquées.  
- **Types perdus** — les structures, les noms de variables et les signatures de fonctions définies dans le code source disparaissent. Le décompileur produit du pseudo-code avec des types génériques (`undefined8`, `long`, `int`).

C'est la combinaison **`-O2` + strip** qui représente le cas le plus courant en conditions réelles (release builds, binaires distribués) et le plus exigeant pour l'analyste.

---

## Résumé de l'import et résultat attendu

Après avoir complété les phases de chargement et d'analyse automatique, le CodeBrowser vous présente :

- un **listing assembleur** dans lequel chaque adresse est annotée : instructions décodées, labels de fonctions, références vers les chaînes et les données, commentaires automatiques ;  
- un **Symbol Tree** peuplé des fonctions détectées (nommées si les symboles sont disponibles, `FUN_XXXXXX` sinon), des imports (`printf`, `malloc`, `strcmp`…), des exports, et des labels ;  
- un **décompileur** opérationnel qui affiche le pseudo-code C de toute fonction sélectionnée ;  
- un **graphe de flux de contrôle** accessible par la touche `Space` depuis le listing, montrant les blocs basiques et les arêtes de chaque fonction.

C'est sur cette base que vous allez travailler dans les sections suivantes. Le résultat de l'analyse automatique n'est qu'un **point de départ** — c'est votre travail d'annotation, de renommage et de reconstruction de types qui transformera un désassemblage anonyme en une compréhension réelle du programme.

---


⏭️ [Navigation dans le CodeBrowser : Listing, Decompiler, Symbol Tree, Function Graph](/08-ghidra/03-navigation-codebrowser.md)
