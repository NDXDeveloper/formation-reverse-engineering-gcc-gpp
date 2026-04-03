🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 5.1 — `file`, `strings`, `xxd` / `hexdump` — premier contact avec un binaire inconnu

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Introduction

Vous venez de recevoir un fichier binaire. Peut-être un exécutable suspect remonté par une équipe SOC, peut-être un challenge CTF, peut-être un binaire legacy dont les sources ont disparu. Quelle que soit la situation, la première question est toujours la même : **qu'est-ce que c'est ?**

L'extension du fichier ne vaut rien — elle peut être absente, trompeuse ou volontairement falsifiée. Le nom du fichier non plus. La seule source de vérité, c'est le **contenu du fichier lui-même**.

Les trois outils présentés dans cette section — `file`, `strings` et `xxd`/`hexdump` — constituent le tout premier réflexe du reverse engineer. Ils ne nécessitent aucune connaissance du format interne du binaire, n'exécutent jamais le fichier, et fournissent en quelques secondes une quantité d'informations surprenante. Ensemble, ils répondent à trois questions fondamentales :

1. **Quel type de fichier est-ce ?** → `file`  
2. **Quels textes lisibles contient-il ?** → `strings`  
3. **À quoi ressemble son contenu brut ?** → `xxd` / `hexdump`

---

## `file` — identifier la nature d'un fichier

### Principe de fonctionnement

La commande `file` identifie le type d'un fichier en examinant son contenu, pas son extension. Elle s'appuie sur une base de signatures appelée **magic database** (généralement `/usr/share/misc/magic` ou `/usr/share/file/magic`). Cette base contient des milliers de règles qui associent des séquences d'octets situées à des positions précises dans le fichier — les **magic bytes** ou **magic numbers** — à un type de fichier connu.

Par exemple, tout fichier ELF commence par les quatre octets `7f 45 4c 46` (soit le caractère DEL suivi des lettres ASCII `E`, `L`, `F`). Un fichier PDF commence par `%PDF`, un PNG par `89 50 4e 47`. La commande `file` lit ces premiers octets, les compare à sa base, et en déduit le type.

Pour un binaire ELF, `file` va bien au-delà du simple magic number : il parse les headers ELF pour extraire l'architecture cible, le type de binaire (exécutable, bibliothèque partagée, objet relogeable), l'endianness, l'OS cible, et d'autres métadonnées.

### Utilisation de base

```bash
$ file keygenme_O0
keygenme_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=a3f5...c4e2, for GNU/Linux 3.2.0, not stripped
```

Cette seule ligne nous apprend déjà énormément de choses. Décortiquons chaque élément :

| Fragment | Signification |  
|---|---|  
| `ELF` | Le format du fichier est ELF (Executable and Linkable Format), le standard sous Linux. |  
| `64-bit` | Le binaire cible une architecture 64 bits. Les registres, les pointeurs et les adresses sont sur 8 octets. |  
| `LSB` | Little-endian (Least Significant Byte first). L'octet de poids faible est stocké en premier en mémoire. C'est la norme sur x86/x86-64. |  
| `pie executable` | Le binaire est un exécutable **PIE** (Position-Independent Executable). Il peut être chargé à n'importe quelle adresse en mémoire, ce qui permet à l'ASLR de fonctionner pleinement. |  
| `x86-64` | L'architecture du jeu d'instructions est x86-64 (aussi appelée AMD64 ou Intel 64). |  
| `version 1 (SYSV)` | Version de l'ABI ELF. `SYSV` signifie System V, l'ABI standard sur Linux. |  
| `dynamically linked` | Le binaire dépend de bibliothèques partagées (`.so`) qui seront chargées au runtime par le loader dynamique. |  
| `interpreter /lib64/ld-linux-x86-64.so.2` | Le chemin du loader dynamique (aussi appelé *dynamic linker* ou *RTLD*). C'est lui qui résout les dépendances au lancement. |  
| `BuildID[sha1]=a3f5...c4e2` | Un identifiant unique du build, utile pour corréler un binaire avec ses symboles de débogage. |  
| `for GNU/Linux 3.2.0` | La version minimale du noyau Linux requise pour exécuter ce binaire. |  
| `not stripped` | Les symboles de débogage **n'ont pas été supprimés**. On trouvera donc les noms de fonctions et de variables dans les tables de symboles. C'est une information précieuse pour le RE. |

### Comparaison avec un binaire strippé

```bash
$ file keygenme_O2_strip
keygenme_O2_strip: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=d8b1...7f3a, for GNU/Linux 3.2.0, stripped
```

La différence saute aux yeux : `stripped` au lieu de `not stripped`. Cela signifie que les symboles ont été supprimés avec la commande `strip`. On ne trouvera plus les noms de fonctions dans les tables de symboles — le travail de reverse sera plus difficile. Notez que `file` ne nous dit rien sur le niveau d'optimisation (`-O0`, `-O2`…) : cette information n'est pas stockée de manière explicite dans les headers ELF.

### Options utiles pour le RE

```bash
# Afficher le type MIME au lieu de la description textuelle
$ file -i keygenme_O0
keygenme_O0: application/x-executable; charset=binary

# Suivre les liens symboliques
$ file -L /usr/bin/python3
/usr/bin/python3: ELF 64-bit LSB pie executable, x86-64, ...

# Ne pas afficher le nom du fichier (utile dans les scripts)
$ file -b keygenme_O0
ELF 64-bit LSB pie executable, x86-64, ...

# Traiter plusieurs fichiers d'un coup
$ file binaries/ch05-keygenme/*
```

### Ce que `file` ne dit pas

`file` est un outil de **classification**, pas d'analyse. Il vous dira qu'un fichier est un ELF x86-64, mais il ne vous dira pas quelles fonctions il contient, quelles bibliothèques il appelle, ni ce qu'il fait. Il ne détecte pas non plus les packers de manière fiable : un binaire UPX sera souvent identifié comme un simple ELF, même si son contenu est compressé. Pour aller plus loin, il faut examiner les sections et l'entropie — ce que nous verrons avec d'autres outils.

---

## `strings` — extraire les chaînes de caractères lisibles

### Principe de fonctionnement

Un binaire compilé n'est pas constitué uniquement d'instructions machine. Il contient aussi des **données textuelles** : messages d'erreur, prompts utilisateur, noms de fichiers, URLs, clés de configuration, formats de `printf`, noms de fonctions de bibliothèques, et parfois des informations que le développeur n'avait pas l'intention de laisser visibles.

La commande `strings` parcourt un fichier et extrait toutes les séquences d'octets qui correspondent à des caractères imprimables ASCII (par défaut, les séquences d'au moins 4 caractères consécutifs). Elle ne comprend pas la structure du fichier — elle cherche simplement des motifs textuels dans un flux d'octets bruts.

### Utilisation de base

```bash
$ strings keygenme_O0
/lib64/ld-linux-x86-64.so.2
libc.so.6  
puts  
printf  
strcmp  
strlen  
__cxa_finalize
__libc_start_main
GLIBC_2.2.5  
GLIBC_2.34  
_ITM_deregisterTMCloneTable
__gmon_start__
_ITM_registerTMCloneTable
[...]
Enter your license key:  
Invalid key format. Expected: XXXX-XXXX-XXXX-XXXX  
Checking key...  
Access granted! Welcome.  
Access denied. Invalid key.  
SuperSecret123  
[...]
GCC: (Ubuntu 13.2.0-23ubuntu4) 13.2.0
[...]
```

En quelques secondes et sans aucune analyse du code, `strings` nous fournit un ensemble d'indices remarquable :

- **Dépendances libc** : `puts`, `printf`, `strcmp`, `strlen` — on sait que le programme affiche du texte et compare des chaînes. La présence de `strcmp` dans un crackme est un indice majeur : c'est probablement la fonction qui compare le mot de passe saisi avec la valeur attendue.  
- **Messages utilisateur** : `Enter your license key:`, `Access granted!`, `Access denied.` — on comprend immédiatement le flux du programme : il demande une clé, la vérifie, et affiche un résultat.  
- **Format attendu** : `Expected: XXXX-XXXX-XXXX-XXXX` — on connaît maintenant le format de la clé sans avoir lu une seule instruction.  
- **Donnée suspecte** : `SuperSecret123` — une chaîne qui ressemble fortement à un mot de passe ou une clé hardcodée. Dans un vrai binaire, ce type de trouvaille est une faille de sécurité majeure.  
- **Informations de compilation** : `GCC: (Ubuntu 13.2.0-23ubuntu4) 13.2.0` — GCC embarque sa version dans une section `.comment` du binaire. On sait maintenant quel compilateur et quelle version ont été utilisés.

### Options essentielles pour le RE

```bash
# Changer la longueur minimale des chaînes (par défaut : 4)
# Une valeur plus élevée réduit le bruit
$ strings -n 8 keygenme_O0

# Afficher l'offset (position dans le fichier) de chaque chaîne
# Indispensable pour retrouver la chaîne dans un éditeur hexadécimal
$ strings -t x keygenme_O0
   2a8 /lib64/ld-linux-x86-64.so.2
   [...]
   2048 Enter your license key:
   2060 Invalid key format. Expected: XXXX-XXXX-XXXX-XXXX
   [...]

# Chercher aussi les chaînes encodées en UTF-16 little-endian
# (fréquent dans les binaires Windows ou les binaires multi-plateformes)
$ strings -e l keygenme_O0

# Scanner toutes les sections du fichier (pas seulement les sections de données)
$ strings -a keygenme_O0
```

L'option `-t x` est particulièrement utile : elle affiche l'offset hexadécimal de chaque chaîne dans le fichier. Cet offset vous permet de localiser précisément la chaîne dans `xxd` ou ImHex, et de remonter jusqu'au code qui la référence via les cross-references dans un désassembleur (chapitres 7 et 8).

### Filtrer intelligemment la sortie

La sortie brute de `strings` est souvent volumineuse et bruitée. En pratique, on la combine presque toujours avec `grep` ou d'autres outils de filtrage :

```bash
# Chercher des URLs ou des chemins réseau
$ strings keygenme_O0 | grep -iE '(http|ftp|/tmp/|/etc/|\.conf)'

# Chercher des formats de printf (révèle la logique d'affichage)
$ strings keygenme_O0 | grep '%'

# Chercher des messages d'erreur (souvent très révélateurs)
$ strings keygenme_O0 | grep -iE '(error|fail|denied|invalid|success|grant)'

# Chercher des noms de fonctions crypto (indices sur le chiffrement utilisé)
$ strings keygenme_O0 | grep -iE '(aes|sha|md5|rsa|encrypt|decrypt|key|iv)'

# Compter le nombre de chaînes pour estimer la « richesse » du binaire
$ strings keygenme_O0 | wc -l
```

### `strings` sur un binaire strippé

Sur un binaire strippé, les noms de fonctions locales disparaissent des tables de symboles, mais les **chaînes de données** restent intactes. Les messages d'erreur, les prompts, les formats de `printf` — tout cela survit au stripping, car ces chaînes sont stockées dans la section `.rodata` (read-only data), pas dans les tables de symboles.

```bash
$ strings keygenme_O2_strip | grep -i key
Enter your license key:  
Invalid key format. Expected: XXXX-XXXX-XXXX-XXXX  
```

C'est pourquoi `strings` reste efficace même sur des binaires dépourvus de symboles. En revanche, les noms de fonctions internes (`check_license`, `validate_key`…) auront disparu. On ne verra plus que les noms des fonctions importées depuis les bibliothèques partagées (stockés dans `.dynstr`), comme `strcmp` ou `printf`.

### Limites de `strings`

`strings` a des angles morts importants à garder en tête :

- **Chaînes obfusquées** : si le développeur a chiffré ou encodé ses chaînes (XOR, Base64, construction caractère par caractère à runtime), `strings` ne les trouvera pas. C'est une technique anti-RE courante.  
- **Chaînes construites dynamiquement** : une chaîne assemblée à l'exécution par concaténation (`strcat`, `snprintf`) n'existe pas en tant que séquence contiguë dans le binaire.  
- **Faux positifs** : des séquences aléatoires d'octets de code machine peuvent ressembler à du texte ASCII. Plus la longueur minimale est courte (en dessous de 6-7 caractères), plus le bruit augmente.  
- **Encodages non-ASCII** : par défaut, `strings` ne cherche que l'ASCII. Les chaînes en UTF-8 multi-octets, UTF-16 ou autres encodages nécessitent l'option `-e`.

Malgré ces limites, `strings` reste l'un des outils les plus rentables en termes de rapport effort/information. Quelques secondes d'exécution peuvent révéler l'essentiel du comportement d'un programme.

---

## `xxd` et `hexdump` — inspecter le contenu brut octet par octet

### Pourquoi regarder les octets bruts ?

`file` vous dit *ce que* c'est. `strings` vous montre *les textes* qu'il contient. Mais parfois, vous avez besoin de voir exactement **ce qui se trouve à une position précise** dans le fichier — un magic number, un header, une séquence d'opcodes, une valeur numérique encodée en binaire, un padding suspect. C'est là qu'interviennent les **dump hexadécimaux**.

Un dump hex affiche le contenu du fichier sous forme d'octets en hexadécimal, accompagnés de leur représentation ASCII. C'est la vue la plus « brute » possible d'un fichier, sans aucune interprétation de structure.

### `xxd` — dump hexadécimal polyvalent

`xxd` est distribué avec `vim` et disponible sur pratiquement tous les systèmes. Il produit un dump lisible et compact.

```bash
$ xxd keygenme_O0 | head -20
00000000: 7f45 4c46 0201 0100 0000 0000 0000 0000  .ELF............
00000010: 0300 3e00 0100 0000 c010 0000 0000 0000  ..>.............
00000020: 4000 0000 0000 0000 d839 0000 0000 0000  @........9......
00000030: 0000 0000 4000 3800 0d00 4000 1f00 1e00  ....@.8...@.....
00000040: 0600 0000 0400 0000 4000 0000 0000 0000  ........@.......
[...]
```

Chaque ligne se décompose en trois parties :

- **Colonne de gauche** (`00000000:`) : l'offset dans le fichier, en hexadécimal. C'est l'adresse de l'octet à partir du début du fichier.  
- **Partie centrale** (`7f45 4c46 0201 0100...`) : les octets en hexadécimal, regroupés par paires. Chaque paire représente un octet (deux chiffres hexadécimaux = 8 bits = valeurs de `00` à `ff`).  
- **Colonne de droite** (`.ELF............`) : la représentation ASCII des mêmes octets. Les octets non imprimables sont affichés comme un point `.`.

Sur la première ligne, on reconnaît immédiatement le **magic number ELF** : `7f 45 4c 46`. Le `7f` est le caractère DEL (non imprimable, affiché comme `.`), suivi des codes ASCII de `E` (`45`), `L` (`4c`) et `F` (`46`).

### Options essentielles de `xxd`

```bash
# Limiter la quantité de données affichées (ici : 64 octets)
$ xxd -l 64 keygenme_O0

# Commencer à un offset spécifique (ici : à partir de l'octet 0x2048)
# Utile quand strings -t x vous a indiqué l'offset d'une chaîne intéressante
$ xxd -s 0x2048 -l 64 keygenme_O0

# Afficher en octets individuels au lieu de groupes de 2
$ xxd -g 1 -l 32 keygenme_O0
00000000: 7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00  .ELF............
00000010: 03 00 3e 00 01 00 00 00 c0 10 00 00 00 00 00 00  ..>.............

# Afficher uniquement en binaire (utile pour les opérations bit à bit)
$ xxd -b -l 8 keygenme_O0
00000000: 01111111 01000101 01001100 01000110 00000010 00000001 00000001 00000000  .ELF....

# Mode "plain" : uniquement les octets hex, sans offset ni ASCII
# Utile pour le scripting et le piping
$ xxd -p -l 16 keygenme_O0
7f454c4602010100000000000000000
```

### Combiner `strings -t x` et `xxd` : un workflow courant

Supposons que `strings -t x` nous a révélé une chaîne suspecte à l'offset `0x2048`. On peut l'examiner en contexte avec `xxd` :

```bash
# 1. Repérer la chaîne avec strings
$ strings -t x keygenme_O0 | grep "SuperSecret"
  20a5 SuperSecret123

# 2. Examiner les octets autour de cette position
$ xxd -s 0x2090 -l 48 keygenme_O0
000020a0: 0053 7570 6572 5365 6372 6574 3132 3300  .SuperSecret123.
000020b0: 4163 6365 7373 2067 7261 6e74 6564 2120  Access granted! 
```

Ce workflow — repérer avec `strings`, localiser avec `xxd` — est un réflexe fondamental. L'offset vous servira ensuite à retrouver les cross-references dans Ghidra ou IDA (chapitre 8) pour identifier quel code utilise cette chaîne.

### `hexdump` — une alternative avec formatage flexible

`hexdump` est un autre outil de dump hexadécimal, présent dans le paquet `bsdmainutils` (ou `util-linux` selon la distribution). Sa syntaxe est différente de `xxd` et son format de sortie par défaut est moins lisible, mais il offre un système de formatage personnalisable très puissant.

```bash
# Format par défaut (peu lisible, groupes de 2 octets en little-endian)
$ hexdump keygenme_O0 | head -5
0000000 457f 464c 0102 0001 0000 0000 0000 0000
0000010 0003 003e 0001 0000 10c0 0000 0000 0000
[...]
```

> ⚠️ **Attention au piège** : le format par défaut de `hexdump` affiche les octets dans l'ordre **little-endian par groupes de 2 octets**. Sur la première ligne, on lit `457f` au lieu de `7f45`. Ce n'est pas une erreur — c'est `hexdump` qui inverse les octets au sein de chaque groupe de 16 bits. C'est déroutant et source d'erreurs. C'est pour cette raison qu'en RE, on préfère généralement `xxd` ou le mode canonique de `hexdump`.

```bash
# Mode canonique (-C) : même format que xxd, beaucoup plus lisible
$ hexdump -C keygenme_O0 | head -5
00000000  7f 45 4c 46 02 01 01 00  00 00 00 00 00 00 00 00  |.ELF............|
00000010  03 00 3e 00 01 00 00 00  c0 10 00 00 00 00 00 00  |..>.............|
[...]

# Limiter la sortie et commencer à un offset
$ hexdump -C -s 0x2048 -n 64 keygenme_O0
```

En pratique, `hexdump -C` et `xxd` produisent un résultat très similaire. Le choix entre les deux est essentiellement une question de préférence personnelle. `xxd` a l'avantage d'être installé partout où `vim` est présent, et son mode reverse (`xxd -r`) permet de reconvertir un dump hex en fichier binaire — une fonctionnalité utile pour le patching rapide.

### Lire le header ELF à la main avec `xxd`

Pour illustrer la puissance d'un dump hex, parcourons les premiers octets d'un ELF 64 bits en les interprétant manuellement. Cette lecture repose sur la structure `Elf64_Ehdr` définie dans la spécification ELF (voir chapitre 2, section 2.4) :

```bash
$ xxd -l 64 -g 1 keygenme_O0
00000000: 7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00  .ELF............
00000010: 03 00 3e 00 01 00 00 00 c0 10 00 00 00 00 00 00  ..>.............
00000020: 40 00 00 00 00 00 00 00 d8 39 00 00 00 00 00 00  @........9......
00000030: 00 00 00 00 40 00 38 00 0d 00 40 00 1f 00 1e 00  ....@.8...@.....
```

| Offset | Octets | Champ | Interprétation |  
|---|---|---|---|  
| `0x00` | `7f 45 4c 46` | `e_ident[EI_MAG]` | Magic number : `\x7fELF` |  
| `0x04` | `02` | `e_ident[EI_CLASS]` | Classe 2 = ELF 64 bits |  
| `0x05` | `01` | `e_ident[EI_DATA]` | Data 1 = little-endian (LSB) |  
| `0x06` | `01` | `e_ident[EI_VERSION]` | Version ELF = 1 (current) |  
| `0x07` | `00` | `e_ident[EI_OSABI]` | OS/ABI = ELFOSABI_NONE (System V) |  
| `0x10` | `03 00` | `e_type` | Type 3 = `ET_DYN` (shared object / PIE executable) |  
| `0x12` | `3e 00` | `e_machine` | Machine `0x3e` = 62 = EM_X86_64 |  
| `0x18` | `c0 10 00 00 00 00 00 00` | `e_entry` | Point d'entrée = `0x10c0` |  
| `0x20` | `40 00 00 00 00 00 00 00` | `e_phoff` | Program header table offset = `0x40` (64 octets) |  
| `0x34` | `38 00` | `e_phentsize` | Taille d'une entrée de program header = 56 octets |  
| `0x36` | `0d 00` | `e_phnum` | Nombre de program headers = 13 |

> 💡 **Rappel** : en little-endian, les octets sont lus « à l'envers ». `03 00` à l'offset `0x10` se lit comme la valeur `0x0003` = 3. De même, `c0 10 00 00 00 00 00 00` à l'offset `0x18` se lit `0x00000000000010c0` = `0x10c0`.

Cette capacité à lire un header directement dans les octets bruts peut sembler fastidieuse — et elle l'est. C'est exactement pour cela que des outils comme `readelf` (section 5.2) et ImHex (chapitre 6) existent. Mais comprendre le lien entre les octets bruts et les structures qu'ils représentent est une compétence fondamentale en RE. Quand un outil automatique échoue ou donne un résultat suspect, c'est en revenant aux octets que vous trouverez la vérité.

---

## Récapitulatif : quand utiliser quel outil

| Outil | Question à laquelle il répond | Temps d'exécution | Complexité |  
|---|---|---|---|  
| `file` | Quel type de fichier est-ce ? Quelle architecture ? Strippé ou non ? | < 1 seconde | Aucune |  
| `strings` | Quels textes lisibles contient ce binaire ? | < 1 seconde | Aucune |  
| `strings -t x` + `grep` | Où se trouve cette chaîne précise dans le fichier ? | < 1 seconde | Minimale |  
| `xxd` / `hexdump -C` | Que contiennent les octets à cette position ? | Instantané | Requiert de connaître les structures |

Ces trois outils sont toujours la **première étape** — avant `readelf`, avant `objdump`, avant Ghidra. Ils forment le socle du workflow de triage rapide que nous formaliserons à la section 5.7.

---

## Ce qu'il faut retenir pour la suite

- **Toujours commencer par `file`**. C'est le réflexe numéro un. Il vous évitera d'ouvrir un binaire ARM dans un désassembleur x86, ou de traiter un script Python comme un exécutable natif.  
- **`strings` est votre meilleur allié sur les binaires strippés**. Quand les symboles ont disparu, les chaînes de données restent. Un seul message d'erreur peut suffire à identifier une fonction ou un chemin d'exécution.  
- **`strings -t x` + `xxd`** forment un duo de localisation. Repérez une chaîne intéressante, notez son offset, puis examinez les octets environnants. Ces offsets vous serviront de points d'entrée dans les outils d'analyse plus avancés.  
- **Le dump hex est la vérité absolue**. Quand un outil vous donne un résultat surprenant, vérifiez dans les octets bruts. Aucune couche d'abstraction ne peut mentir sur le contenu réel du fichier.

---


⏭️ [`readelf` et `objdump` — anatomie d'un ELF (headers, sections, segments)](/05-outils-inspection-base/02-readelf-objdump.md)
