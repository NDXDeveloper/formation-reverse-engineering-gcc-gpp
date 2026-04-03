🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 20.5 — Identifier des bibliothèques tierces embarquées (FLIRT / signatures Ghidra)

> 📘 **Chapitre 20 — Décompilation et reconstruction du code source**  
> **Partie IV — Techniques Avancées de RE**

---

## Le problème : des centaines de fonctions qui ne sont pas les vôtres

Quand on ouvre un binaire strippé dans Ghidra, le Symbol Tree affiche des centaines, parfois des milliers de fonctions nommées `FUN_XXXXXXXX`. L'analyste sait que seule une fraction de ces fonctions constitue le code métier du programme — le reste provient de bibliothèques liées statiquement ou de code runtime embarqué par le compilateur.

Le phénomène est particulièrement marqué dans trois situations :

**Les binaires liés statiquement.** Quand un programme est compilé avec `gcc -static`, toute la libc (et potentiellement d'autres bibliothèques) est copiée dans le binaire final. Un simple `hello world` statique pèse plus de 750 Ko sur x86-64 et contient plus d'un millier de fonctions — dont une seule est du code utilisateur.

**Les binaires embarquant des bibliothèques crypto.** Notre `crypto.c` (chapitre 24) peut embarquer des fonctions d'OpenSSL ou de libsodium compilées en statique. L'analyste qui ne reconnaît pas ces fonctions perdra des heures à tenter de comprendre une implémentation d'AES ou de SHA-256 qui est en réalité du code de bibliothèque standard.

**Les binaires C++ avec la STL.** Les templates de la Standard Template Library sont instanciées dans chaque unité de compilation qui les utilise. Le binaire `oop_O2_strip` contient des dizaines de fonctions issues de `std::vector`, `std::map`, `std::string` et `std::unique_ptr` — du code template qui pollue la liste des fonctions et noie le code métier.

L'identification de ces bibliothèques tierces est un multiplicateur de productivité considérable. Reconnaître qu'une fonction de 200 lignes de pseudo-code est en réalité `memcpy` optimisée par la glibc, ou que les 15 fonctions interconnectées à partir de l'adresse `0x408000` constituent l'implémentation AES de mbedTLS, permet de les nommer, de les « mettre de côté », et de concentrer l'effort d'analyse sur le code qui importe réellement.

---

## Principe général : la correspondance par signatures

L'idée fondamentale est simple : on calcule une empreinte (signature) de chaque fonction connue d'une bibliothèque à partir de ses premiers octets de code machine, puis on compare ces empreintes avec les fonctions du binaire analysé. Si une empreinte correspond, on peut attribuer le nom de la fonction de bibliothèque à la fonction inconnue du binaire.

Ce principe est implémenté par deux systèmes distincts que l'on va étudier : **FLIRT** (Fast Library Identification and Recognition Technology), originellement développé par Hex-Rays pour IDA, et **Function ID** (FID), le système natif de Ghidra.

### Les défis de la correspondance

L'approche par signatures n'est pas triviale. Plusieurs facteurs compliquent la correspondance :

**Les relocations.** Quand une bibliothèque est compilée, les adresses internes (appels entre fonctions, accès aux données globales) ne sont pas encore résolues — elles contiennent des slots de relocation qui seront remplis par le linker. Deux compilations de la même bibliothèque avec des adresses de base différentes produisent des octets différents à ces emplacements. La signature doit donc **masquer** les octets correspondant à des relocations et ne comparer que les octets invariants.

**Les versions de compilateur.** La même fonction `strlen` compilée par GCC 11 et GCC 13 peut produire un code machine légèrement différent (choix de registres, ordonnancement des instructions). Les bases de signatures doivent couvrir plusieurs versions du compilateur pour être efficaces.

**Les niveaux d'optimisation.** La même bibliothèque compilée en `-O0` et en `-O2` produit un code radicalement différent. Les signatures sont donc spécifiques à un niveau d'optimisation — une base de signatures créée à partir de la glibc en `-O2` ne reconnaîtra pas une glibc liée en `-O0`.

**Les fonctions courtes.** Une fonction de 5 instructions produit une signature très courte et donc potentiellement ambiguë — plusieurs fonctions différentes peuvent avoir la même signature. Les systèmes de signatures gèrent cette ambiguïté par des mécanismes de confirmation (vérification des octets suivants, vérification des cross-references).

---

## FLIRT : le standard historique

FLIRT est le système de signatures le plus ancien et le plus répandu dans le monde du RE. Bien qu'il ait été conçu pour IDA, ses fichiers de signatures (`.sig`) sont documentés et peuvent être utilisés dans d'autres outils, y compris indirectement dans Ghidra via des plugins tiers.

### Comment fonctionne FLIRT

Le processus FLIRT se déroule en deux phases :

**Phase de création des signatures (hors ligne).** On part des fichiers objets (`.o`) ou des bibliothèques statiques (`.a`) d'une bibliothèque connue. L'outil `pelf` (pour ELF) ou `pcf` (pour COFF) parse ces fichiers, extrait le code de chaque fonction, masque les octets de relocation, et produit un fichier de patterns (`.pat`). Ensuite, l'outil `sigmake` compile ces patterns en un fichier de signatures compact (`.sig`) qui peut être distribué et appliqué rapidement.

La chaîne est donc : `.a` / `.o` → `pelf` → `.pat` → `sigmake` → `.sig`

**Phase d'application (pendant l'analyse).** Quand l'analyste charge un fichier `.sig` dans IDA (ou dans un outil compatible), le moteur FLIRT parcourt toutes les fonctions du binaire et compare leurs premiers octets avec les patterns de la base. Pour chaque correspondance, la fonction est renommée avec le nom de la fonction de bibliothèque.

### Format d'un pattern FLIRT

Un pattern FLIRT est une séquence d'octets hexadécimaux où les points (`..`) représentent les octets masqués (relocations). Par exemple :

```
558BEC83EC..8B45..8945..8B4D..894D..EB..8B55..0FB602 ... strlen
```

Les premiers octets (`558BEC83EC`) correspondent au prologue x86 32-bit de la fonction (`push ebp; mov ebp, esp; sub esp, ...`). En x86-64, le prologue équivalent serait `554889E54883EC...`. Les `..` marquent les tailles variables (relocation de l'offset de la pile), et les octets suivants sont le corps invariant de la fonction. Le nom `strlen` est associé à cette signature.

### Créer ses propres signatures FLIRT pour GCC

Pour nos binaires d'entraînement, on pourrait vouloir créer des signatures pour la glibc utilisée sur notre système. Les outils FLIRT (`pelf`, `sigmake`) sont distribués avec le SDK d'IDA (disponible pour les détenteurs de licence). Pour ceux qui n'ont pas IDA, des alternatives open source existent :

**`flair` / `pat2sig`** — réimplémentation open source du pipeline FLIRT, disponible sur GitHub. Permet de créer des fichiers `.pat` depuis des `.a` et de les compiler en `.sig`.

**`lscan`** — outil Python qui applique des signatures FLIRT sur un binaire ELF sans nécessiter IDA.

**`sig-from-lib`** — script qui automatise l'extraction de signatures depuis les bibliothèques statiques installées sur le système (`/usr/lib/x86_64-linux-gnu/*.a`).

Le workflow typique pour créer une base de signatures de la glibc locale :

```bash
# Localiser la bibliothèque statique de la libc
ls /usr/lib/x86_64-linux-gnu/libc.a

# Extraire les patterns (avec un outil compatible)
pelf /usr/lib/x86_64-linux-gnu/libc.a libc_glibc236.pat

# Résoudre les collisions et compiler
sigmake libc_glibc236.pat libc_glibc236.sig
```

Si `sigmake` détecte des collisions (plusieurs fonctions avec le même pattern), il produit un fichier `.exc` (exceptions) que l'analyste doit éditer manuellement pour résoudre les ambiguïtés avant de relancer `sigmake`.

---

## Function ID : le système natif de Ghidra

Ghidra intègre son propre système d'identification de fonctions appelé **Function ID** (FID). Il est conceptuellement similaire à FLIRT mais avec une implémentation et un format de base de données différents.

### Architecture de Function ID

Function ID utilise des bases de données au format `.fidb` (Function ID Database) stockées dans le répertoire d'installation de Ghidra. Chaque base contient des enregistrements associant un hash du code machine d'une fonction à son nom et à sa bibliothèque d'origine.

Le système utilise un hashing en deux niveaux :

**Hash court (full hash).** Calculé sur l'intégralité du corps de la fonction, en masquant les opcodes de relocation. C'est le critère principal de correspondance.

**Hash spécifique (specific hash).** Calculé sur un sous-ensemble restreint d'octets invariants (typiquement les opcodes sans opérandes). Utilisé pour confirmer les correspondances ambiguës du hash court.

Quand Ghidra analyse un binaire, Function ID compare automatiquement chaque fonction détectée avec ses bases de données. Les correspondances trouvées sont affichées dans la colonne *Function Signature Source* du Symbol Tree, marquées comme « FID » avec un score de confiance.

### Bases de données fournies avec Ghidra

Ghidra est livré avec des bases FID pré-construites pour les bibliothèques les plus courantes. On les trouve dans le répertoire `Ghidra/Features/FunctionID/data/` :

```
Ghidra/Features/FunctionID/data/
├── libc_glibc_2.XX_x64.fidb      ← glibc pour x86-64
├── libstdcpp_XX_x64.fidb          ← libstdc++ pour x86-64
├── libm_glibc_2.XX_x64.fidb      ← libm (math) pour x86-64
├── libcrypto_openssl_XX.fidb      ← OpenSSL libcrypto
└── ...
```

Les versions disponibles dépendent de la release de Ghidra. Si la version exacte de la glibc de votre binaire cible n'est pas couverte, les signatures d'une version proche peuvent quand même produire des correspondances partielles — les fonctions stables entre versions (comme `strlen`, `memcpy`, `printf`) sont reconnues même avec un léger décalage de version.

### Vérifier et configurer Function ID

Pour voir quelles bases FID sont actives dans un projet Ghidra : menu *Tools → Function ID → Manage FID Databases*. Cette fenêtre liste toutes les bases installées, leur état (active/inactive) et le nombre de fonctions qu'elles contiennent.

Si l'analyse automatique initiale n'a pas appliqué Function ID (cela peut arriver si l'option était désactivée), on peut le relancer manuellement : menu *Analysis → One Shot → Function ID*. Ghidra parcourt toutes les fonctions et applique les correspondances trouvées.

Après l'exécution, vérifier les résultats dans le Symbol Tree : les fonctions identifiées sont renommées avec leur nom de bibliothèque. La fenêtre *Function ID Results* (accessible via *Window → Function ID Results*) affiche un résumé des correspondances, avec le score de confiance pour chaque identification.

### Créer ses propres bases Function ID

Quand les bases fournies ne couvrent pas les bibliothèques embarquées dans le binaire cible, on peut créer ses propres bases FID. Ghidra fournit les outils nécessaires via le menu *Tools → Function ID*.

Le workflow consiste à :

1. **Créer une nouvelle base FID vide.** *Tools → Function ID → Create New FID Database*. Choisir un nom descriptif comme `libsodium_1.0.18_x64_O2.fidb`.

2. **Importer la bibliothèque de référence.** Compiler (ou récupérer) la bibliothèque cible en statique avec les symboles. Importer le `.a` ou les `.o` dans un projet Ghidra dédié et lancer l'analyse complète.

3. **Peupler la base FID.** *Tools → Function ID → Populate FID from Programs*. Sélectionner le programme de référence (la bibliothèque analysée) et la base FID cible. Ghidra calcule les hashes de chaque fonction nommée et les enregistre dans la base.

4. **Appliquer la base au binaire cible.** Copier le fichier `.fidb` dans le répertoire FID de Ghidra, ou le charger manuellement via *Manage FID Databases*. Relancer l'analyse Function ID sur le binaire cible.

Cette méthode est particulièrement utile pour les bibliothèques spécialisées que Ghidra ne connaît pas nativement — des bibliothèques crypto (libsodium, mbedTLS, wolfSSL), des frameworks réseau (libevent, libcurl compilé en statique), ou des moteurs de jeu.

---

## Signatures Ghidra avancées : les Function Tags et les Data Type Archives

Au-delà de Function ID, Ghidra offre deux mécanismes complémentaires qui aident à identifier et à gérer les fonctions de bibliothèque.

### Data Type Archives (.gdt)

Les archives de types de données sont des fichiers `.gdt` qui contiennent des définitions de types C/C++ pour des bibliothèques connues. Quand Ghidra détecte un appel à `printf` via PLT, il cherche la signature de `printf` dans les archives `.gdt` pour appliquer automatiquement le bon nombre et les bons types de paramètres.

Les archives fournies avec Ghidra couvrent les en-têtes système standards (POSIX, Windows API, etc.). On peut en créer de nouvelles via *Parse C Source* (le même mécanisme utilisé en section 20.4 pour réimporter un header reconstruit). En important les headers de libsodium ou d'OpenSSL dans une archive `.gdt`, on enrichit les signatures de types que Ghidra appliquera quand il reconnaîtra ces fonctions.

La combinaison FID (identification par hash du code) + GDT (application des types) est puissante : FID trouve le nom, GDT applique la signature complète avec les types corrects.

### Function Tags

Les Function Tags permettent de catégoriser les fonctions identifiées. Après avoir reconnu un ensemble de fonctions comme appartenant à la glibc, on peut leur attribuer le tag `LIBRARY_GLIBC` pour les filtrer facilement dans le Symbol Tree. Ghidra applique automatiquement le tag `LIBRARY_FUNCTION` aux fonctions identifiées par FID, mais on peut créer des tags personnalisés plus granulaires.

En pratique, on construit progressivement une taxonomie : `LIBRARY_GLIBC`, `LIBRARY_LIBCRYPTO`, `LIBRARY_STL`, `COMPILER_RUNTIME`, `USER_CODE`. Cette classification permet de filtrer le Symbol Tree pour n'afficher que les fonctions « intéressantes » — un confort considérable quand le binaire contient 3000 fonctions dont 2500 sont du code de bibliothèque.

---

## Cas pratique : identifier la glibc dans un binaire statique

Prenons un scénario concret. On compile notre `keygenme.c` en statique :

```bash
gcc -static -O2 -s -o keygenme_static keygenme.c
```

Le binaire résultant fait environ 900 Ko et contient plus de 1100 fonctions dans Ghidra, toutes nommées `FUN_XXXXXXXX`.

### Avant Function ID

L'analyse initiale de `main` dans Ghidra montre des appels vers des fonctions inconnues. Le pseudo-code de `main` contient des appels comme :

```c
    FUN_00410230(local_88, 0x40, FUN_004112a0);  /* fgets ? */
    sVar1 = FUN_00409c10(local_88);               /* strlen ? */
    FUN_00410180("Username: ");                    /* printf ? */
```

On devine la sémantique grâce aux arguments (un buffer de 64 octets, `stdin`, une chaîne format), mais sans certitude.

### Application de Function ID

On active les bases FID de la glibc : *Tools → Function ID → Manage FID Databases* → cocher la base `libc_glibc` correspondante à l'architecture x86-64. Puis *Analysis → One Shot → Function ID*.

En quelques secondes, Ghidra identifie des centaines de fonctions. Le pseudo-code de `main` se transforme :

```c
    fgets(local_88, 0x40, stdin);
    sVar1 = strlen(local_88);
    printf("Username: ");
```

Les fonctions appelées sont maintenant nommées, et — grâce aux archives `.gdt` — leurs signatures (types de paramètres et retour) sont correctement appliquées. Le pseudo-code entier de `main` gagne en lisibilité d'un coup.

### Ce qui reste non identifié

Function ID ne reconnaît pas tout. Les fonctions internes à la glibc (fonctions statiques, fonctions d'initialisation du runtime) et les fonctions du code utilisateur restent en `FUN_XXXXXXXX`. Mais le rapport signal/bruit est radicalement amélioré : au lieu de 1100 fonctions inconnues, on en a peut-être 200, dont la majorité sont des fonctions internes de la libc qu'on peut ignorer.

Pour identifier le code utilisateur dans cette masse, on cherche les fonctions appelées depuis `main` qui ne sont pas marquées `LIBRARY_FUNCTION`. Dans notre cas, `derive_key`, `parse_key_input` et `verify_key` sont les trois fonctions non identifiées appelées depuis `main` — ce sont celles qui contiennent la logique métier.

---

## Reconnaître les bibliothèques crypto sans signatures

Les signatures sont l'approche idéale quand elles sont disponibles. Mais face à une bibliothèque crypto inconnue ou une implémentation custom, il faut recourir à d'autres indices. Cette technique complète l'approche par signatures et sera approfondie au chapitre 24.

### Constantes magiques

Chaque algorithme cryptographique utilise des constantes caractéristiques qui apparaissent en clair dans le binaire, dans la section `.rodata` ou directement comme valeurs immédiates. L'Annexe J du tutoriel fournit une table de référence complète. Quelques exemples parmi les plus courants :

**AES.** La S-box AES est un tableau de 256 octets commençant par `0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5`. Sa présence dans `.rodata` est une preuve quasi certaine d'une implémentation AES.

**SHA-256.** Les 8 valeurs d'initialisation (IV) sont `0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19`. Les 64 constantes de round sont également caractéristiques.

**MD5.** Les constantes de round sont dérivées de la fonction sinus et commencent par `0xd76aa478, 0xe8c7b756, 0x242070db`.

**RC4.** Pas de constante fixe, mais le pattern d'initialisation du tableau S (permutation de 0 à 255) est reconnaissable dans le désassemblage.

### Recherche dans Ghidra

La recherche de ces constantes dans Ghidra se fait via *Search → Memory* en mode hexadécimal. Chercher la séquence `63 7c 77 7b f2 6b 6f c5` localise la S-box AES. Une fois trouvée, les cross-references depuis cette adresse mènent directement aux fonctions de chiffrement/déchiffrement.

L'outil `strings` en ligne de commande peut aussi révéler des chaînes indicatrices : `"AES-256-CBC"`, `"EVP_EncryptInit"`, `"mbedtls_aes_crypt_ecb"` — même dans un binaire strippé, ces chaînes internes aux bibliothèques ne sont pas toujours supprimées.

### Règles YARA

Les règles YARA (couvertes au chapitre 6, section 10, et au chapitre 35, section 4) formalisent la recherche de constantes magiques en règles réutilisables. Le dossier `yara-rules/crypto_constants.yar` de la formation contient des règles prêtes à l'emploi pour les algorithmes les plus courants. Appliquer ces règles avec `yara` en ligne de commande (ou depuis ImHex) sur un binaire inconnu permet une identification rapide avant même d'ouvrir Ghidra :

```bash
yara yara-rules/crypto_constants.yar keygenme_O2_strip
```

---

## Impact sur le workflow de décompilation

L'identification des bibliothèques tierces transforme le workflow de décompilation de plusieurs manières.

### Réduction du périmètre d'analyse

Sur notre `keygenme_static` de plus de 1100 fonctions, après application de Function ID, seules une dizaine de fonctions non identifiées méritent une analyse détaillée. Le temps d'analyse passe de plusieurs jours (si on tentait de comprendre chaque fonction) à quelques heures.

### Amélioration du pseudo-code en cascade

Quand Ghidra renomme `FUN_00409c10` en `strlen` et lui applique la signature `size_t strlen(const char *)`, le pseudo-code de **toutes les fonctions appelantes** s'améliore automatiquement. Le paramètre passé à `strlen` est maintenant typé comme `const char *`, ce qui peut à son tour corriger le type de la variable locale source, qui peut à son tour améliorer le pseudo-code des fonctions qui l'initialisent. L'effet en cascade est significatif.

### Identification de l'écosystème technique

Savoir qu'un binaire embarque mbedTLS plutôt qu'OpenSSL, ou libcurl plutôt qu'une implémentation HTTP custom, oriente immédiatement l'analyse. On peut consulter la documentation de la bibliothèque identifiée pour comprendre le schéma d'appel attendu, les structures de données utilisées, et les erreurs de configuration courantes. La décompilation devient un travail d'audit de l'utilisation d'une API connue plutôt qu'un travail de compréhension d'un code opaque.

### Constitution d'une base de signatures personnelle

Au fil des analyses, l'analyste constitue progressivement sa propre collection de bases `.fidb` pour les bibliothèques qu'il rencontre fréquemment. Cette collection devient un actif réutilisable : chaque nouveau binaire analysé bénéficie automatiquement des identifications faites lors des analyses précédentes. Le chapitre 35 (section 6 — Construire son propre toolkit RE) aborde l'organisation de cette collection.

---


⏭️ [Exporter et nettoyer le pseudo-code pour produire un code recompilable](/20-decompilation/06-exporter-pseudo-code.md)
