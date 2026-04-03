🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 9.1 — IDA Free — workflow de base sur binaire GCC

> 📘 **Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja**  
> Section précédente : [README — Introduction du chapitre](/09-ida-radare2-binja/README.md)

---

## IDA en quelques mots

IDA (Interactive DisAssembler) est le désassembleur commercial développé par Hex-Rays depuis le début des années 1990. Pendant plus de deux décennies, il a été *le* standard de facto en reverse engineering professionnel — analyse de malwares, recherche de vulnérabilités, forensique, audit de firmware. La quasi-totalité de la littérature académique et industrielle en RE, des writeups de CTF aux rapports de threat intelligence, utilise IDA comme référence. Comprendre son interface et sa terminologie est donc indispensable, ne serait-ce que pour lire le travail des autres.

IDA existe en plusieurs déclinaisons. IDA Pro est la version complète, vendue plusieurs milliers d'euros, qui inclut le décompileur Hex-Rays pour de nombreuses architectures. IDA Home est une licence personnelle à prix réduit. **IDA Free** est la version gratuite que nous utiliserons ici. Elle est limitée mais reste un outil d'analyse puissant pour les binaires x86-64.

## Capacités et limitations d'IDA Free

Avant de commencer, il est important de savoir exactement ce que la version gratuite permet et ne permet pas, pour éviter les frustrations en cours d'analyse.

**Ce qu'IDA Free offre :**

Le moteur d'analyse automatique d'IDA est disponible dans la version gratuite. C'est le cœur de l'outil : sa capacité à identifier les fonctions, à résoudre les références croisées, à reconnaître les prologues et épilogues de fonctions, et à distinguer le code des données est historiquement l'une des meilleures de l'industrie. Sur un binaire strippé produit par GCC, IDA identifie souvent davantage de fonctions correctement qu'`objdump` en mode désassemblage linéaire. IDA Free prend en charge les binaires ELF x86-64, ce qui correspond exactement à notre contexte. L'interface graphique complète est présente : vue désassemblage (texte et graphe), vue hexadécimale, références croisées, renommage de fonctions et variables, ajout de commentaires, et navigation dans le code.

**Ce qu'IDA Free ne propose pas :**

La limitation la plus significative concerne le décompileur. IDA Free inclut un décompileur cloud pour x86-64 depuis les versions récentes (8.x+), mais il nécessite une connexion internet et a des quotas d'utilisation. Le scripting IDAPython est disponible mais peut être restreint selon la version. Le support multi-architecture est absent : seul x86/x64 est pris en charge, là où IDA Pro couvre ARM, MIPS, PowerPC et des dizaines d'autres processeurs. Enfin, la licence interdit l'usage commercial.

> 💡 Pour le contexte de cette formation (ELF x86-64, usage éducatif), IDA Free est parfaitement adapté. Les fonctionnalités absentes sont couvertes par Ghidra (chapitre 8) que vous avez déjà en main.

## Installation

IDA Free se télécharge depuis le site officiel de Hex-Rays à l'adresse `hex-rays.com/ida-free`. L'installation requiert de créer un compte et d'accepter la licence d'utilisation non commerciale.

Sur Linux, le programme est distribué sous forme d'un installeur `.run` ou d'une archive. Après extraction, le binaire principal est `ida64` (pour analyser des binaires 64 bits). Sur les distributions récentes, certaines dépendances graphiques Qt peuvent être nécessaires — le script `check_env.sh` du chapitre 4 vérifie leur présence.

```bash
# Lancer IDA Free (64 bits) depuis le répertoire d'installation
./ida64
```

Au premier lancement, IDA affiche un écran d'accueil proposant soit de créer une nouvelle analyse (« New »), soit d'ouvrir une base existante (« Previous »). Les bases de données IDA portent l'extension `.i64` (pour les binaires 64 bits) et conservent l'intégralité de votre travail d'analyse : renommages, commentaires, types définis, etc.

## Import et analyse initiale d'un binaire ELF

### Charger le binaire

Ouvrons notre binaire fil rouge `keygenme_O2_strip` :

1. Cliquer sur **New** (ou *File → Open*).  
2. Naviguer jusqu'au fichier `binaries/ch09-keygenme/keygenme_O2_strip`.  
3. IDA affiche une boîte de dialogue de chargement (« Load a new file »).

Cette boîte de dialogue est le premier point de décision important. IDA détecte automatiquement le format du fichier — ici `ELF64 for x86-64` — et propose le processeur correspondant. Dans la grande majorité des cas, les valeurs par défaut sont correctes et il suffit de valider.

Quelques options méritent cependant votre attention :

- **Loading segment et loading offset** — laissez les valeurs par défaut sauf si vous savez que le binaire doit être chargé à une adresse spécifique (cas de firmware ou de dumps mémoire).  
- **Manual load** — cocher cette case permet de contrôler finement quels segments sont chargés. C'est utile pour les binaires atypiques ou corrompus, mais inutile pour un ELF standard.  
- **Analysis options** — le bouton « Kernel options » ou « Analysis options » donne accès aux paramètres du moteur d'analyse. La configuration par défaut convient pour commencer.

Validez avec **OK**. IDA charge le binaire et lance immédiatement son analyse automatique.

### L'auto-analyse : que fait IDA en arrière-plan ?

Dès le chargement, la barre de statut en bas de la fenêtre affiche une barre de progression et le message « Autoanalysis ». C'est le moteur d'IDA qui parcourt le binaire pour :

- **Identifier les fonctions** — IDA utilise une analyse récursive descendante (*recursive descent*), en partant des points d'entrée connus (le symbole `_start`, les entrées de la table `.init_array`, etc.) et en suivant les branchements. C'est fondamentalement différent du balayage linéaire d'`objdump`, et c'est ce qui permet à IDA de distinguer le code des données même dans un binaire strippé.  
- **Reconnaître les signatures de bibliothèques** — grâce à sa technologie **FLIRT** (*Fast Library Identification and Recognition Technology*), IDA compare les séquences d'octets au début de chaque fonction détectée avec une base de signatures connues. C'est ainsi qu'il peut nommer automatiquement des fonctions de la libc ou d'autres bibliothèques standard, même dans un binaire statiquement lié et strippé.  
- **Propager les types** — quand IDA reconnaît un appel à `printf`, il sait que le premier argument est un `const char *` format et propage cette information dans son analyse.  
- **Résoudre les références croisées** — chaque adresse référencée (par un `call`, un `jmp`, un `lea`, un accès mémoire…) est enregistrée. Ce réseau de cross-references (XREF) est l'un des outils les plus puissants d'IDA pour naviguer dans un binaire.

> ⏳ Attendez que l'auto-analyse se termine (la mention « idle » apparaît dans la barre de statut) avant de commencer votre exploration. Travailler pendant l'analyse peut donner des résultats incomplets.

## Découvrir l'interface principale

Une fois l'analyse terminée, IDA affiche son interface organisée autour de plusieurs vues. Prenons le temps de les identifier, car elles constituent votre espace de travail permanent.

### La vue IDA View (désassemblage)

C'est la vue centrale. Elle affiche le code désassemblé de la fonction courante. IDA propose deux modes de visualisation, accessibles via la barre d'espace :

- **Mode texte** — le listing linéaire classique, semblable à ce que produit `objdump` mais enrichi par les annotations d'IDA (noms de fonctions reconnues, commentaires automatiques, types propagés). Chaque ligne affiche l'adresse virtuelle, les octets bruts (optionnels), le mnémonique et les opérandes.

- **Mode graphe** — IDA découpe la fonction en *blocs de base* (basic blocks) reliés par des flèches de couleur. Les flèches vertes indiquent un branchement conditionnel pris (condition vraie), les rouges un branchement non pris (condition fausse), et les bleues un saut inconditionnel. Cette vue est extrêmement utile pour comprendre la logique de contrôle d'une fonction : les boucles forment des cycles visibles, les `if/else` se manifestent comme des losanges de décision, et les `switch/case` apparaissent comme des étoiles de branchement.

Pour basculer entre les deux modes, appuyez sur **Espace** dans la vue IDA View.

### La fenêtre Functions

Accessible via *View → Open Subviews → Functions* (ou le raccourci selon la version), cette fenêtre liste toutes les fonctions identifiées par l'auto-analyse. Pour chaque fonction, IDA affiche son adresse de début, sa taille, et son nom — qui sera soit un symbole reconnu (comme `_start` ou `__libc_csu_init`), soit un nom généré automatiquement de la forme `sub_XXXXXXXX` pour les fonctions sans symbole.

Dans le cas de `keygenme_O2_strip`, la plupart des fonctions portent des noms `sub_*`. C'est normal : le binaire a été strippé. La quantité de fonctions identifiées et la pertinence de leurs bornes (adresse de début et de fin) sont un bon indicateur de la qualité de l'analyse. Comparez ce nombre avec celui obtenu sous Ghidra au chapitre 8 — les résultats peuvent diverger, en particulier pour les petites fonctions ou les fonctions non alignées.

### La fenêtre Strings

*View → Open Subviews → Strings* (ou raccourci **Shift+F12**) ouvre une fenêtre listant toutes les chaînes de caractères détectées dans le binaire, avec leur adresse, leur longueur et leur encodage. C'est l'équivalent amélioré de la commande `strings` : IDA ne se contente pas de chercher des séquences ASCII, il identifie également les chaînes référencées par le code et celles en UTF-8/UTF-16.

Double-cliquer sur une chaîne vous amène à son emplacement dans le segment de données (`.rodata` typiquement). De là, vous pouvez utiliser les références croisées (touche **X**) pour savoir quelles fonctions utilisent cette chaîne — un point d'entrée classique dans l'analyse d'un binaire inconnu.

### La fenêtre Hex View

La vue hexadécimale affiche le contenu brut du binaire. Elle est synchronisée avec la vue désassemblage : naviguer dans l'une déplace le curseur dans l'autre. Cette vue est utile pour vérifier les octets réels d'une instruction, examiner des données binaires, ou repérer des patterns que le désassembleur n'a pas interprétés.

### Les autres vues utiles

- **Imports** (*View → Open Subviews → Imports*) — liste les fonctions importées depuis les bibliothèques dynamiques (via la PLT/GOT). Sur un binaire dynamiquement lié, c'est une mine d'or : `strcmp`, `printf`, `malloc`, `open`, `send`… chaque import raconte une partie de ce que fait le programme.  
- **Exports** — liste les symboles exportés. Sur un exécutable classique, il y en a peu (souvent juste `_start`). Sur une bibliothèque `.so`, c'est l'API publique.  
- **Segments** — affiche les segments du binaire (`.text`, `.data`, `.rodata`, `.bss`, `.plt`, `.got`, etc.) avec leurs attributs (lecture, écriture, exécution). Correspond à ce que `readelf -S` affiche.

## Naviguer dans le code

La navigation efficace dans IDA repose sur un ensemble de raccourcis clavier et de mécanismes qu'il faut connaître pour être productif.

### Aller à une adresse ou un symbole

Le raccourci **G** ouvre une boîte de dialogue « Jump to address ». Vous pouvez y saisir une adresse virtuelle (par exemple `0x401230`), un nom de fonction (`sub_401230`, `main`, `_start`), ou une expression. C'est le moyen le plus direct de naviguer.

### Suivre un appel ou une référence

Placer le curseur sur un opérande (une adresse cible d'un `call`, d'un `jmp`, ou d'un `lea`) et appuyer sur **Entrée** vous amène à la cible. C'est l'équivalent du « clic sur un lien hypertexte ». Pour revenir en arrière, utilisez **Échap** — IDA maintient un historique de navigation.

### Cross-references (XREF)

C'est l'un des mécanismes les plus puissants d'IDA. Placer le curseur sur une adresse, un nom de fonction ou une variable, puis appuyer sur **X** ouvre la fenêtre des cross-references. Elle liste tous les endroits du binaire qui font référence à cet élément : tous les `call` vers cette fonction, tous les accès à cette variable, tous les sauts vers cette adresse.

Les XREF se décomposent en plusieurs types :

- `p` — *code reference (procedure call)* : un `call` vers cette adresse.  
- `j` — *code reference (jump)* : un `jmp` (conditionnel ou non) vers cette adresse.  
- `r` — *data reference (read)* : une instruction qui lit cette adresse mémoire.  
- `w` — *data reference (write)* : une instruction qui écrit à cette adresse.  
- `o` — *data reference (offset)* : une instruction qui prend l'adresse elle-même comme valeur (typiquement un `lea`).

Dans l'analyse d'un binaire inconnu, les XREF sont votre boussole. Par exemple, pour trouver la routine de vérification du `keygenme`, une approche classique consiste à repérer les chaînes `"Access granted"` ou `"Wrong key"` dans la fenêtre Strings, puis à remonter les XREF pour identifier la fonction qui les utilise.

## Annoter le binaire : renommage et commentaires

L'analyse d'un binaire est un processus incrémental. Au fur et à mesure que vous comprenez le rôle de chaque fonction ou variable, vous devez capturer cette compréhension dans la base IDA. IDA conserve toutes vos annotations dans le fichier `.i64`, ce qui vous permet de reprendre votre travail là où vous l'avez laissé.

### Renommer une fonction ou une variable

Sélectionner un nom (par exemple `sub_40117A`) et appuyer sur **N** ouvre la boîte de dialogue de renommage. Remplacez `sub_40117A` par un nom descriptif comme `check_serial` ou `validate_key`. Ce nouveau nom sera immédiatement propagé partout dans la base : tous les `call sub_40117A` deviendront `call check_serial`, toutes les XREF seront mises à jour.

Le renommage est probablement l'action la plus fréquente et la plus utile lors d'une analyse. Un binaire strippé rempli de `sub_*` est illisible. Après une heure de renommage méthodique, le même binaire devient compréhensible.

### Ajouter des commentaires

IDA propose deux types de commentaires :

- **Commentaire régulier** (touche **:**) — affiché à droite de l'instruction, sur la même ligne. Utilisé pour annoter une instruction précise (« compare le serial avec la valeur attendue », « boucle de déchiffrement XOR »).  
- **Commentaire répétable** (touche **;**) — similaire au commentaire régulier, mais il est automatiquement affiché partout où l'adresse commentée est référencée. Si vous mettez un commentaire répétable sur une variable globale, ce commentaire apparaîtra dans chaque instruction qui accède à cette variable. Extrêmement utile pour les constantes et les variables globales.

### Définir un type

Le raccourci **Y** sur un nom de fonction permet de modifier sa signature (prototype). Si vous avez identifié qu'une fonction `sub_401230` prend un `char *` en premier argument et retourne un `int`, vous pouvez spécifier `int sub_401230(char *input)`. IDA propagera ensuite ces types dans l'analyse : les registres `rdi` au point d'appel seront annotés comme `input`, et le `eax` de retour comme un `int`.

## Particularités des binaires GCC dans IDA

Les binaires compilés avec GCC présentent des caractéristiques qu'IDA gère bien dans l'ensemble, mais qu'il faut connaître pour ne pas être dérouté.

### Fonctions d'initialisation et de terminaison

Un binaire ELF compilé avec GCC ne commence pas directement à `main()`. Le point d'entrée réel est `_start`, qui appelle `__libc_start_main` avec `main` comme argument. IDA identifie généralement cette séquence et nomme correctement `main`, même dans un binaire strippé, en reconnaissant le pattern d'appel à `__libc_start_main`. Si ce n'est pas le cas, recherchez `__libc_start_main` dans les imports : son premier argument (passé dans `rdi` selon la convention System V) est l'adresse de `main`.

Vous trouverez également des fonctions comme `__libc_csu_init`, `__libc_csu_fini`, `_init`, `_fini`, `frame_dummy`, `register_tm_clones`, et `deregister_tm_clones`. Ce sont des fonctions d'infrastructure insérées par GCC et la glibc. Elles n'ont rien à voir avec la logique de votre programme. Il est bon de les reconnaître pour les ignorer et concentrer votre attention sur le code applicatif.

### Appels via la PLT

Sur un binaire dynamiquement lié, les appels aux fonctions de bibliothèques passent par la PLT (*Procedure Linkage Table*), comme détaillé au chapitre 2.9. IDA résout automatiquement ces indirections : un `call` vers un stub PLT est affiché avec le nom de la fonction importée (par exemple `call _strcmp` ou `call _printf`). C'est un avantage considérable par rapport à `objdump` qui affiche l'adresse brute du stub.

### Niveaux d'optimisation

Le niveau d'optimisation de GCC a un impact direct sur la facilité de lecture dans IDA :

- **`-O0`** — le code est quasi-littéral par rapport au source. Les variables locales sont sur la pile, chaque opération est une instruction distincte, et les fonctions sont rarement inlinées. IDA produit un résultat très lisible.  
- **`-O2` / `-O3`** — les variables vivent dans les registres, les fonctions courtes sont inlinées (elles disparaissent de la liste des fonctions), les boucles sont déroulées, et le flux de contrôle peut être réorganisé. L'analyse d'IDA reste correcte, mais le code est nettement plus dense et difficile à lire pour un humain.

C'est pour cette raison que nous travaillons sur `keygenme_O2_strip` : il représente le cas réaliste d'un binaire « dans la nature », et c'est là que la qualité de l'outil d'analyse fait la différence.

### FLIRT et reconnaissance de bibliothèques

La technologie FLIRT d'IDA compare les premiers octets de chaque fonction avec des signatures pré-calculées de bibliothèques connues. Sur un binaire statiquement lié (compilé avec `gcc -static`), FLIRT peut reconnaître et nommer automatiquement des centaines de fonctions de la glibc — `strlen`, `memcpy`, `malloc`, etc. — qui seraient autrement des `sub_*` anonymes.

IDA Free est livré avec un jeu de signatures FLIRT, mais il est plus restreint que celui d'IDA Pro. Si vous travaillez fréquemment sur des binaires statiquement liés, notez que Ghidra offre une fonctionnalité équivalente via ses Function ID databases (FID), abordée au chapitre 20.5.

## Workflow type sur `keygenme_O2_strip`

Voici la séquence d'actions typique lors de l'ouverture d'un nouveau binaire dans IDA Free, appliquée à notre fil rouge.

**1 — Charger et attendre l'auto-analyse.** Ouvrir `keygenme_O2_strip`, accepter les options par défaut, et attendre que la barre de statut affiche « idle ».

**2 — Explorer les chaînes.** Ouvrir la fenêtre Strings (**Shift+F12**) et chercher des chaînes intéressantes. Dans un crackme, on cherche des messages de succès ou d'échec. Double-cliquer sur une chaîne prometteuse amène à son emplacement en `.rodata`.

**3 — Remonter les XREF.** Depuis la chaîne identifiée, appuyer sur **X** pour voir quelle(s) fonction(s) la référencent. Naviguer vers cette fonction.

**4 — Passer en mode graphe.** Appuyer sur **Espace** pour basculer en vue graphe. Identifier la structure de contrôle : où est le branchement qui décide entre le chemin « succès » et le chemin « échec » ?

**5 — Renommer et annoter.** Renommer la fonction (`check_serial`, `validate_input`…), renommer les variables locales si possible, ajouter des commentaires sur les instructions clés.

**6 — Explorer le voisinage.** Utiliser les XREF pour remonter vers les fonctions appelantes (`main` ?), descendre vers les sous-fonctions appelées. Renommer au fur et à mesure.

**7 — Sauvegarder.** IDA enregistre automatiquement dans le `.i64`, mais un *File → Save* explicite après une session de travail significative est une bonne habitude.

Ce workflow est fondamentalement le même que celui présenté avec Ghidra au chapitre 8. La méthodologie de reverse engineering est indépendante de l'outil — ce sont les raccourcis clavier et les capacités spécifiques qui changent.

## Raccourcis clavier essentiels

| Action | Raccourci |  
|---|---|  
| Basculer texte / graphe | `Espace` |  
| Aller à une adresse | `G` |  
| Renommer | `N` |  
| Cross-references | `X` |  
| Commentaire régulier | `:` |  
| Commentaire répétable | `;` |  
| Modifier le type/prototype | `Y` |  
| Fenêtre Strings | `Shift+F12` |  
| Retour arrière (navigation) | `Échap` |  
| Convertir en code | `C` |  
| Convertir en donnée | `D` |  
| Annuler la dernière action | `Ctrl+Z` |

## Quand IDA Free surpasse Ghidra (et inversement)

Il ne s'agit pas de déclarer un vainqueur absolu, mais de connaître les situations où l'un brille davantage que l'autre.

**IDA Free a l'avantage quand :**

- La reconnaissance initiale de fonctions est critique — sur certains binaires strippés ou obfusqués, IDA identifie plus correctement les bornes de fonctions que Ghidra.  
- Vous avez besoin d'une navigation rapide et fluide — l'interface d'IDA est optimisée pour la vitesse, les raccourcis sont cohérents, et la réactivité sur de gros binaires est généralement meilleure.  
- Le binaire est statiquement lié et FLIRT peut identifier les fonctions de bibliothèque.  
- Vous lisez un writeup ou un rapport écrit avec IDA et vous avez besoin de reproduire l'analyse dans le même environnement.

**Ghidra a l'avantage quand :**

- Vous avez besoin d'un décompileur complet sans restriction — le décompileur de Ghidra est inclus, gratuit, sans quota, et fonctionne hors ligne.  
- Vous travaillez sur des architectures non-x86 (ARM, MIPS…) — IDA Free ne les supporte pas.  
- Vous avez besoin de scripting avancé — l'API Java/Python de Ghidra est riche et bien documentée.  
- La licence doit permettre un usage commercial ou professionnel — IDA Free l'interdit, Ghidra est Apache 2.0.  
- Vous travaillez en équipe — Ghidra Server permet l'analyse collaborative, une fonctionnalité absente d'IDA Free.

La section 9.6 fournira un comparatif détaillé intégrant aussi Radare2 et Binary Ninja.

---


⏭️ [Radare2 / Cutter — analyse en ligne de commande et GUI](/09-ida-radare2-binja/02-radare2-cutter.md)
