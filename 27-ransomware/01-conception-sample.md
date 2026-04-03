🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 27.1 — Conception du sample : chiffrement AES sur `/tmp/test`, clé hardcodée

> 📁 **Fichiers concernés** :  
> - `binaries/ch27-ransomware/ransomware_sample.c` — code source complet  
> - `binaries/ch27-ransomware/Makefile` — compilation des 3 variantes  
>  
> 💡 Cette section décrit **intentionnellement** la conception interne du sample. Dans un scénario réel, vous ne disposeriez jamais du code source : tout ce qui est exposé ici, vous devrez le retrouver par vous-même dans les sections 27.2 à 27.6. L'objectif est de vous donner une carte mentale avant de plonger dans l'analyse.

---

## Pourquoi construire notre propre sample ?

Travailler sur un malware réel dans le cadre d'une formation pose trois problèmes majeurs. Le premier est légal : la distribution de code malveillant est encadrée voire interdite selon les juridictions. Le deuxième est sécuritaire : un vrai ransomware embarque souvent des mécanismes d'évasion, de propagation latérale ou de persistance qui le rendent dangereux même dans un lab. Le troisième est pédagogique : un sample réel est souvent obfusqué, packé, et communique avec une infrastructure C2 disparue — autant d'obstacles qui noient l'apprentissage sous la complexité accidentelle.

En concevant notre propre sample, nous contrôlons chaque paramètre. Le code est auditable, le comportement est déterministe, le périmètre de destruction est borné, et surtout, nous pouvons calibrer la difficulté en produisant plusieurs variantes du même binaire (debug, optimisé, strippé). L'étudiant sait *exactement* ce qu'il cherche — mais doit le retrouver par les seuls moyens du reverse engineering.

---

## Architecture fonctionnelle

Le sample suit un flux linéaire en cinq étapes, sans branchement conditionnel complexe ni mécanisme d'évasion. Cette simplicité est voulue : elle permet de se concentrer sur l'analyse crypto et la reconstruction du schéma de chiffrement.

```
main()
  │
  ├─ 1. Vérification que /tmp/test/ existe
  │     └─ Si absent → message d'erreur, exit(1)
  │
  ├─ 2. Parcours récursif de /tmp/test/
  │     └─ traverse_directory()
  │           ├─ Ignore « . » et « .. »
  │           ├─ Ignore les fichiers .locked (déjà chiffrés)
  │           ├─ Ignore README_LOCKED.txt (ransom note)
  │           ├─ Descend dans les sous-répertoires
  │           └─ Pour chaque fichier régulier → encrypt_file()
  │
  ├─ 3. Chiffrement de chaque fichier
  │     └─ encrypt_file()
  │           ├─ Lecture complète en mémoire (fread)
  │           ├─ Chiffrement AES-256-CBC (OpenSSL EVP)
  │           ├─ Écriture de <fichier>.locked avec header
  │           └─ Suppression du fichier original (unlink)
  │
  ├─ 4. Dépôt de la ransom note
  │     └─ drop_ransom_note()
  │
  └─ 5. Affichage du bilan et exit(0)
```

Il n'y a aucune communication réseau, aucun mécanisme de persistance, aucune tentative d'élévation de privilèges. Le binaire fait une seule chose : chiffrer des fichiers dans un répertoire précis.

---

## Choix cryptographiques

### Algorithme : AES-256-CBC

Le sample utilise l'AES en mode CBC (Cipher Block Chaining) avec une clé de 256 bits. Ce choix reflète ce que l'on observe fréquemment dans les ransomwares réels : AES est rapide, disponible partout, et le mode CBC est l'un des plus courants dans les implémentations basées sur OpenSSL.

L'appel central côté chiffrement repose sur l'API EVP d'OpenSSL, qui se décompose en trois phases :

```c
EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, AES_KEY, AES_IV);  
EVP_EncryptUpdate(ctx, out, &len, in, in_len);  
EVP_EncryptFinal_ex(ctx, out + len, &len);  
```

Du point de vue du reverse engineering, cette séquence de trois appels est un **pattern signature** : quand vous verrez trois appels successifs à des fonctions dont les noms contiennent `Init`, `Update` et `Final` (ou leurs adresses dans la PLT si le binaire est strippé), vous saurez que vous êtes face à une routine crypto EVP.

### Clé hardcodée (32 octets)

La clé est déclarée comme un tableau `static const unsigned char` de 32 octets :

```c
static const unsigned char AES_KEY[32] = {
    0x52, 0x45, 0x56, 0x45, 0x52, 0x53, 0x45, 0x5F,  /* REVERSE_ */
    0x45, 0x4E, 0x47, 0x49, 0x4E, 0x45, 0x45, 0x52,  /* ENGINEER */
    0x49, 0x4E, 0x47, 0x5F, 0x49, 0x53, 0x5F, 0x46,  /* ING_IS_F */
    0x55, 0x4E, 0x5F, 0x32, 0x30, 0x32, 0x35, 0x21   /* UN_2025! */
};
```

La valeur ASCII de cette clé est `REVERSE_ENGINEERING_IS_FUN_2025!`. Ce choix est délibéré à plusieurs niveaux :

- **Pour l'exercice `strings`** : la clé étant composée de caractères imprimables, un simple `strings ransomware_O0 | grep -i reverse` la révèle immédiatement. C'est la première victoire de l'étudiant, dès le triage.  
- **Pour l'exercice ImHex** : en vue hexadécimale, la séquence `52 45 56 45 52 53 45 5F ...` est visuellement identifiable dans la section `.rodata`.  
- **Pour l'exercice GDB/Frida** : la clé est passée en argument à `EVP_EncryptInit_ex`, donc capturable via un breakpoint ou un hook sur cette fonction.

Dans un ransomware réel, la clé symétrique ne serait évidemment jamais hardcodée ainsi. Elle serait typiquement générée aléatoirement à chaque exécution, puis chiffrée avec une clé publique RSA/ECDH embarquée, et transmise à un serveur C2 ou stockée localement sous forme chiffrée. L'asymétrie entre clé publique (embarquée) et clé privée (détenue par l'attaquant) est ce qui rend le déchiffrement impossible sans payer. Ici, nous avons volontairement court-circuité cette étape pour que la récupération de la clé soit faisable.

### IV hardcodé (16 octets)

Le vecteur d'initialisation est lui aussi statique :

```c
static const unsigned char AES_IV[16] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x13, 0x37, 0x42, 0x42, 0xFE, 0xED, 0xFA, 0xCE
};
```

Les valeurs `0xDEADBEEF`, `0xCAFEBABE`, `0x1337` et `0xFEEDFACE` sont des **magic numbers** bien connus dans le monde du développement système et de la sécurité. Elles sont utilisées ici pour deux raisons. D'une part, elles sont immédiatement reconnaissables en vue hexadécimale, ce qui facilite le repérage dans ImHex. D'autre part, elles rappellent à l'étudiant que les IV réels ne devraient jamais être statiques : en mode CBC, réutiliser le même IV avec la même clé permet de détecter si deux fichiers commencent par le même contenu (les premiers blocs chiffrés seront identiques).

### Padding PKCS#7

Le mode CBC exige que le texte clair soit un multiple de la taille de bloc (16 octets pour AES). OpenSSL applique par défaut le padding PKCS#7 : si le dernier bloc fait `n` octets de moins que 16, il est complété par `n` octets de valeur `n`. Ce détail est important pour le déchiffreur : après déchiffrement, il faut retirer ce padding — ou utiliser la taille originale stockée dans le header du fichier `.locked`.

---

## Format du fichier chiffré (`.locked`)

Chaque fichier chiffré suit un format binaire simple que l'étudiant devra cartographier avec ImHex :

```
Offset   Taille    Contenu
──────   ──────    ────────────────────────────────
0x00     8         Magic bytes : "RWARE27\0"
0x08     8         Taille originale du fichier (uint64_t, little-endian)
0x10     variable  Données chiffrées (AES-256-CBC, padding PKCS#7 inclus)
```

Le magic header `RWARE27` sert de marqueur d'identification. Dans un contexte de réponse à incident, c'est ce type de signature qui permet de recenser rapidement les fichiers affectés sur un système compromis (`find / -exec head -c 7 {} \; 2>/dev/null | grep RWARE27`). C'est aussi la base d'une règle YARA (section 27.4).

Le champ de taille originale à l'offset `0x08` est une commodité d'implémentation : il permet au déchiffreur de tronquer le résultat à la bonne longueur après suppression du padding. Sans cette information, il faudrait se fier uniquement au padding PKCS#7 — ce qui fonctionne, mais cette redondance est courante dans les implémentations réelles.

---

## Périmètre de destruction : pourquoi `/tmp/test/`

Le répertoire cible est fixé en dur à `/tmp/test/` via la constante `TARGET_DIR`. Ce choix borne explicitement le périmètre de destruction du sample :

- `/tmp/` est un répertoire temporaire, vidé au redémarrage sur la plupart des distributions Linux.  
- Le sous-répertoire `test/` doit être créé manuellement par l'étudiant (via `make testenv`), ce qui constitue une action intentionnelle.  
- Le programme vérifie l'existence de `/tmp/test/` au démarrage et refuse de s'exécuter si le répertoire n'existe pas.

Malgré cette limitation, le sample reste un programme qui **supprime des fichiers de manière irréversible** (via `unlink()`). C'est pourquoi l'exécution en VM sandboxée avec snapshot préalable reste impérative.

Le `Makefile` fournit deux cibles utilitaires pour gérer cet environnement :

- `make testenv` crée `/tmp/test/` et y dépose six fichiers de test : un document texte, un fichier Markdown, un faux PDF (contenu texte), un fichier binaire aléatoire (simule une image), un CSV et un fichier dans un sous-répertoire pour valider le parcours récursif.  
- `make reset` supprime `/tmp/test/` et le recrée à l'identique, ce qui évite de restaurer un snapshot complet entre deux tests.

---

## Parcours récursif et filtrage

La fonction `traverse_directory()` utilise l'API POSIX `opendir` / `readdir` / `closedir` pour énumérer les entrées du répertoire. Ce choix produit un code assembleur caractéristique que l'on retrouvera dans le désassemblage : une boucle appelant `readdir` jusqu'à obtenir `NULL`, avec des appels à `stat` pour distinguer fichiers et répertoires, et un appel récursif pour les sous-répertoires.

Le filtrage repose sur deux critères implémentés dans `should_skip()` :

1. **Extension `.locked`** — Un fichier déjà chiffré est ignoré. La vérification se fait par comparaison des derniers caractères du nom de fichier avec la chaîne `".locked"`. Ce pattern est visible avec `strings` et constitue un indicateur comportemental (IOC) du sample.  
2. **Nom `README_LOCKED.txt`** — La ransom note elle-même est exclue du chiffrement, sinon elle serait immédiatement rendue illisible.

Du point de vue du RE, ces deux chaînes de caractères (`.locked` et `README_LOCKED.txt`) apparaîtront dans la section `.rodata` et constitueront des indices précieux lors du triage.

---

## La ransom note

La fonction `drop_ransom_note()` écrit un fichier texte à la racine du répertoire cible. Son contenu est stocké en tant que chaîne littérale dans le binaire (section `.rodata`) :

```
========================================
  VOS FICHIERS ONT ETE CHIFFRES !
========================================

Ceci est un exercice pedagogique.  
Formation Reverse Engineering — Chapitre 27  

Algorithme : AES-256-CBC  
La cle est dans le binaire. Trouvez-la.  

Indice : cherchez les constantes de 32 octets...
========================================
```

Dans un ransomware réel, cette note contiendrait une adresse Bitcoin, un lien Tor vers un portail de paiement, et un identifiant unique de victime. Ici, elle contient un indice pédagogique qui oriente l'étudiant vers la bonne piste.

Notez que l'intégralité de ce texte sera visible via `strings`. C'est un choix de conception : dans un scénario de triage réel, la ransom note intégrée au binaire est souvent le premier élément qui permet de classifier un sample comme ransomware.

---

## Dépendance à OpenSSL

Le sample est lié dynamiquement à `libssl` et `libcrypto` (flags `-lssl -lcrypto` dans le `Makefile`). Ce choix a des conséquences directes sur l'analyse :

- **Avec symboles** : les appels à `EVP_EncryptInit_ex`, `EVP_EncryptUpdate` et `EVP_EncryptFinal_ex` sont visibles dans la table des symboles dynamiques. Un simple `nm -D ransomware_O0 | grep EVP` les liste.  
- **Sans symboles (strippé)** : les noms des fonctions internes disparaissent, mais les appels à la bibliothèque dynamique transitent toujours par la PLT. On verra des `call` vers des entrées de la PLT qui restent résolvables via `objdump -d -j .plt` ou dans Ghidra.  
- **`ldd`** : la commande `ldd ransomware_O0` affichera la dépendance à `libcrypto.so`, ce qui constitue un indice fort de la présence de routines cryptographiques.  
- **`ltrace`** : en traçant les appels de bibliothèque, `ltrace` capturera les appels EVP avec leurs arguments — y compris le pointeur vers la clé.

L'utilisation d'OpenSSL plutôt qu'une implémentation AES custom est un choix réaliste. De nombreux malwares réels embarquent OpenSSL ou en reprennent des portions. Cela permet aussi de s'entraîner à la reconnaissance de bibliothèques tierces via les signatures FLIRT (Ghidra/IDA) abordées au [Chapitre 20](/20-decompilation/README.md).

---

## Les trois variantes compilées

Le `Makefile` produit trois binaires à partir du même code source, chacun offrant un niveau de difficulté croissant pour l'analyse :

### `ransomware_O0` — Variante debug

```
gcc -Wall -Wextra -Wpedantic -O0 -g3 -ggdb -DDEBUG -o ransomware_O0 ransomware_sample.c -lssl -lcrypto
```

C'est la variante la plus confortable pour débuter. Le flag `-O0` désactive toute optimisation : chaque ligne de code C correspond directement à une séquence d'instructions assembleur, les variables locales sont toutes sur la pile, et aucune fonction n'est inlinée. Le flag `-g3` inclut les informations DWARF maximales : noms de fonctions, noms de variables, numéros de ligne, et même les définitions de macros.

Cette variante est idéale pour établir une correspondance mentale entre le code source et le désassemblage, avant de s'attaquer aux variantes plus difficiles.

### `ransomware_O2` — Variante optimisée avec symboles

```
gcc -Wall -Wextra -Wpedantic -O2 -g -o ransomware_O2 ransomware_sample.c -lssl -lcrypto
```

Le flag `-O2` active un large éventail d'optimisations : inlining de petites fonctions, réorganisation des blocs de base, élimination de code mort, propagation de constantes, et potentiellement vectorisation de certaines boucles. Le code assembleur sera sensiblement différent de la variante `-O0`, mais les symboles DWARF restent présents pour guider l'analyse.

L'intérêt de cette variante est double : observer concrètement l'impact des optimisations GCC sur un code que l'on connaît déjà, et s'habituer à la lecture d'un assembleur plus dense et moins séquentiel.

### `ransomware_O2_strip` — Variante strippée

```
gcc -Wall -Wextra -Wpedantic -O2 -s -o ransomware_O2_strip ransomware_sample.c -lssl -lcrypto  
strip --strip-all ransomware_O2_strip  
```

C'est la variante la plus proche de ce que l'on rencontrerait en situation réelle. Les symboles internes ont été supprimés : plus de noms de fonctions, plus de noms de variables, plus de numéros de ligne. Seuls subsistent les symboles dynamiques (les appels à OpenSSL via la PLT) et les chaînes de caractères dans `.rodata`.

Cette variante est celle sur laquelle le checkpoint final doit être réalisé. Elle force à mobiliser l'ensemble des techniques vues dans la formation : reconnaissance de patterns, cross-references, analyse dynamique et raisonnement par déduction.

---

## Limites volontaires du sample

Pour garder le focus sur l'analyse cryptographique et la méthodologie de reverse engineering, plusieurs aspects présents dans les ransomwares réels ont été volontairement omis :

- **Pas de génération aléatoire de clé** — La clé est statique, ce qui la rend récupérable. Un ransomware réel utiliserait `RAND_bytes()` ou `/dev/urandom`.  
- **Pas de chiffrement asymétrique** — Il n'y a pas de clé RSA/ECDH pour protéger la clé symétrique. C'est ce mécanisme qui rend les ransomwares réels si difficiles à contrer.  
- **Pas de communication réseau** — Aucune exfiltration de clé vers un serveur C2. Ce volet est couvert au [Chapitre 28](/28-dropper/README.md) avec le dropper.  
- **Pas de mécanisme de persistance** — Pas d'écriture dans les crontabs, les services systemd, ou les fichiers d'init.  
- **Pas d'anti-analyse** — Pas de détection de débogueur, pas de VM detection, pas de packing. Ces techniques sont traitées au [Chapitre 19](/19-anti-reversing/README.md).  
- **Pas de suppression sécurisée** — Le fichier original est supprimé par `unlink()`, mais les données restent récupérables sur disque avec des outils de forensics. Un ransomware avancé écraserait le contenu avant suppression.  
- **Pas de multithreading** — Le chiffrement est séquentiel. Les ransomwares modernes parallélisent massivement pour maximiser la vitesse de chiffrement.

Chacune de ces simplifications est une porte ouverte vers un approfondissement futur. L'étudiant curieux pourra, après avoir terminé ce chapitre, modifier le code source pour ajouter l'une de ces fonctionnalités et observer son impact sur l'analyse.

---

## Compilation et préparation

Depuis le répertoire `binaries/ch27-ransomware/`, la séquence de préparation est la suivante :

```bash
# Installer la dépendance OpenSSL (si ce n'est pas déjà fait)
sudo apt install libssl-dev

# Compiler les 3 variantes
make all

# Préparer l'environnement de test
make testenv

# Vérifier que les fichiers de test sont en place
ls -la /tmp/test/

# ⚠️ PRENDRE UN SNAPSHOT DE LA VM MAINTENANT
```

Après exécution du sample, l'environnement peut être restauré soit via le snapshot, soit via `make reset` qui recrée `/tmp/test/` à l'identique.

Pour vérifier rapidement que le binaire fonctionne comme attendu sur la variante debug :

```bash
./ransomware_O0
ls /tmp/test/        # Les fichiers .locked doivent apparaître  
xxd /tmp/test/document.txt.locked | head  
#   → Les 8 premiers octets doivent être 52 57 41 52 45 32 37 00 (RWARE27)
```

---

## Ce que vous allez chercher dans les sections suivantes

Maintenant que vous connaissez l'architecture du sample, mettez cette connaissance de côté. Les sections 27.2 à 27.6 vous demanderont de **retrouver chaque élément** par les seuls moyens du reverse engineering :

- **27.2** — Le triage rapide avec `file`, `strings`, `checksec` : quelles informations pouvez-vous extraire en moins de cinq minutes, sans ouvrir de désassembleur ?  
- **27.3** — L'analyse statique dans Ghidra et ImHex : où se trouvent les constantes AES dans le binaire ? Comment reconstruire le flux de chiffrement à partir du désassemblage ?  
- **27.4** — Les règles YARA : comment transformer vos observations en signatures de détection réutilisables ?  
- **27.5** — L'analyse dynamique avec GDB et Frida : comment capturer la clé et l'IV au moment précis où ils sont passés à OpenSSL ?  
- **27.6** — Le déchiffreur Python : comment reproduire le schéma AES-256-CBC en sens inverse pour restaurer les fichiers ?  
- **27.7** — Le rapport d'analyse : comment formaliser tout cela dans un document professionnel ?

⏭️ [Triage rapide : `file`, `strings`, `checksec`, premières hypothèses](/27-ransomware/02-triage-rapide.md)
