🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.8 — Recherche de magic bytes, chaînes encodées et séquences d'opcodes

> 🎯 **Objectif de cette section** : Maîtriser les fonctions de recherche d'ImHex — recherche hexadécimale, recherche de chaînes, recherche par expressions régulières et analyse d'entropie — pour localiser rapidement des signatures, des données encodées et des séquences d'instructions dans un binaire.

---

## Le contexte : chercher une aiguille dans une botte d'octets

Un binaire ELF de taille moyenne pèse entre quelques dizaines et quelques centaines de kilooctets. Un binaire lié statiquement ou un binaire Go/Rust peut dépasser le mégaoctet. Parcourir visuellement des milliers de lignes hexadécimales pour trouver un motif précis — un magic number, une constante cryptographique, une chaîne obfusquée, un opcode particulier — est impraticable.

La commande `strings` en CLI (vue au chapitre 5) résout une partie du problème pour les chaînes ASCII visibles. Mais elle ne trouve ni les séquences d'octets arbitraires, ni les chaînes encodées (XOR, Base64, UTF-16), ni les patterns d'opcodes machine. ImHex offre un ensemble de fonctions de recherche bien plus riche, directement intégrées à la vue hexadécimale.

---

## La boîte de dialogue de recherche

La recherche s'ouvre avec `Ctrl+F`. ImHex affiche une barre de recherche en haut de la vue hexadécimale avec plusieurs modes sélectionnables. Chaque mode correspond à un type de motif différent.

### Recherche hexadécimale (Hex)

C'est le mode le plus fondamental : vous saisissez une séquence d'octets en notation hexadécimale, et ImHex trouve toutes les occurrences dans le fichier.

```
Motif : 7F 45 4C 46
```

Cette recherche trouve le magic number ELF. Les résultats sont surlignés dans la vue hexadécimale, et vous pouvez naviguer entre les occurrences avec les boutons suivant/précédent (ou `F3` / `Shift+F3`).

La recherche hexadécimale accepte les espaces entre les octets (pour la lisibilité) mais ne les exige pas — `7F454C46` fonctionne aussi. Elle accepte également le caractère `??` comme **wildcard** pour un octet quelconque :

```
Motif : 48 8B ?? 10
```

Ce motif cherche l'opcode `mov reg, [reg+0x10]` en x86-64 : `48 8B` est le préfixe REX.W + opcode `MOV`, le troisième octet varie selon les registres source et destination, et `10` est le déplacement. Les wildcards sont essentiels pour chercher des patterns d'instructions sans se soucier des registres exacts — nous y reviendrons dans la sous-section sur les opcodes.

### Recherche de chaînes (String)

Le mode String recherche une chaîne de caractères dans le fichier. ImHex convertit automatiquement la chaîne en octets selon l'encodage choisi.

```
Chaîne : "password"
```

ImHex cherche les octets `70 61 73 73 77 6F 72 64` (encodage ASCII). Les options de recherche permettent de choisir l'encodage : ASCII, UTF-8, UTF-16 LE, UTF-16 BE. La recherche en UTF-16 LE est particulièrement utile pour les binaires Windows compilés avec MinGW, où les chaînes sont souvent en wide chars (`wchar_t`, 2 octets par caractère).

La recherche de chaînes n'est pas sensible à la casse par défaut — vous pouvez activer ou désactiver cette option selon le contexte.

### Recherche par expression régulière (Regex)

ImHex supporte la recherche par expressions régulières sur le contenu interprété comme texte. Ce mode est utile pour chercher des **patterns de chaînes** plutôt que des chaînes exactes :

```
Regex : [A-Za-z0-9+/]{20,}={0,2}
```

Ce pattern cherche des séquences qui ressemblent à du **Base64** : 20 caractères ou plus dans l'alphabet Base64, suivis de 0 à 2 signes `=` de padding. C'est une heuristique, pas une garantie — des données binaires peuvent accidentellement correspondre — mais c'est un point de départ efficace pour repérer des données encodées.

D'autres expressions régulières utiles en RE :

```
https?://[^\x00]+          # URLs embarquées
[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}   # Adresses IPv4
[A-Fa-f0-9]{32,64}        # Hash MD5/SHA-1/SHA-256 en hexadécimal ASCII
```

---

## Recherche de magic bytes : identifier les formats embarqués

Les magic bytes (ou magic numbers) sont des séquences d'octets fixes placées au début d'un fichier ou d'un bloc de données pour identifier son format. La commande `file` sur Linux s'appuie justement sur une base de données de magic bytes pour identifier les types de fichiers.

Dans un binaire, des fichiers ou des blocs de données formatés peuvent être **embarqués** dans les sections `.rodata` ou `.data` : des images, des certificats, des archives, des fichiers de configuration sérialisés. La recherche de magic bytes connus permet de les localiser rapidement.

### Magic bytes courants à connaître

Voici les signatures les plus fréquemment rencontrées en reverse engineering de binaires ELF compilés avec GCC :

| Format | Magic bytes (hex) | Représentation ASCII |  
|---|---|---|  
| ELF | `7F 45 4C 46` | `.ELF` |  
| PNG | `89 50 4E 47 0D 0A 1A 0A` | `.PNG....` |  
| JPEG | `FF D8 FF` | `...` |  
| ZIP / JAR | `50 4B 03 04` | `PK..` |  
| gzip | `1F 8B` | `..` |  
| PDF | `25 50 44 46` | `%PDF` |  
| SQLite | `53 51 4C 69 74 65 20 66 6F 72 6D 61 74` | `SQLite format` |  
| DER (certificat X.509) | `30 82` | `0.` |  
| PEM (certificat texte) | `2D 2D 2D 2D 2D 42 45 47 49 4E` | `-----BEGIN` |  
| Protobuf (varint) | pas de magic fixe, mais `08` suivi de petits entiers est un pattern fréquent | — |

Quand vous analysez un binaire inconnu, lancer une série de recherches hexadécimales sur ces magic bytes est un réflexe de triage rapide. Si vous trouvez un magic PNG dans `.rodata`, vous savez qu'une image est embarquée. Si vous trouvez un magic SQLite, le programme utilise probablement une base de données locale.

### Cas particulier : les constantes cryptographiques

Certaines séquences d'octets signalent l'utilisation d'algorithmes cryptographiques spécifiques. Ce ne sont pas des magic bytes au sens classique, mais des **constantes d'initialisation** (IV, S-box, valeurs de round) que le compilateur place dans `.rodata` ou directement inline dans le code.

Exemples :

| Algorithme | Constante | Séquence hex (début) |  
|---|---|---|  
| AES | S-box (première ligne) | `63 7C 77 7B F2 6B 6F C5` |  
| SHA-256 | Valeurs initiales H0–H3 | `67 E6 09 6A 85 AE 67 BB` (little-endian) |  
| MD5 | Constantes T[1]–T[2] | `78 A4 6A D7 56 B7 C7 E8` (little-endian) |  
| RC4 | Pas de constante fixe | — (mais une S-box de 256 octets initialisée séquentiellement `00 01 02 ... FF` avant permutation) |  
| ChaCha20 | Constante "expand 32-byte k" | `65 78 70 61 6E 64 20 33 32 2D 62 79 74 65 20 6B` |

La recherche de ces constantes est une technique fondamentale de l'analyse cryptographique que nous approfondirons au chapitre 24. L'annexe J fournit une table de référence plus complète. Pour le moment, retenez le principe : une recherche hexadécimale sur `63 7C 77 7B` dans un binaire inconnu vous dit en quelques secondes si AES est embarqué.

---

## Recherche de chaînes encodées et obfusquées

Les chaînes en clair (`strings`) sont la première chose qu'un analyste cherche dans un binaire. Les développeurs qui veulent entraver l'analyse le savent, et ils obfusquent les chaînes sensibles — mots de passe, URLs de C2, messages d'erreur révélateurs. Les méthodes d'obfuscation les plus courantes sont le XOR avec une clé fixe, l'encodage Base64, et le chiffrement à la volée (la chaîne est déchiffrée en mémoire au runtime).

### Chaînes XORées avec une clé fixe

Le XOR avec un octet constant est l'obfuscation la plus primitive mais encore très répandue, surtout dans les malwares de faible sophistication. Si la chaîne `password` est XORée avec la clé `0x5A`, les octets résultants sont `2A 3B 29 29 2D 35 28 3E` — illisibles par `strings` mais présents dans le binaire.

ImHex ne propose pas nativement de « recherche avec déchiffrement XOR », mais plusieurs approches permettent de détecter ces chaînes.

**Approche par le code.** Le désassemblage (chapitres 7–8) révèle souvent la routine de désobfuscation : une boucle qui XOR chaque octet d'un buffer avec une constante. Une fois la clé identifiée, vous pouvez écrire un pattern `.hexpat` qui applique le XOR à l'affichage :

```cpp
struct XorString {
    u8 raw[16];
};

fn decode_xor(u8 byte) {
    return byte ^ 0x5A;
};

XorString obfuscated @ 0x...;
```

Nous verrons des techniques plus avancées avec le scripting `.hexpat` et les fonctions de transformation dans les chapitres suivants.

**Approche par l'entropie.** Une zone de données XORées avec un octet constant conserve les patterns statistiques de la chaîne originale (un texte anglais XORé avec `0x5A` a toujours une distribution de fréquences caractéristique). L'analyse d'entropie (voir plus bas) peut aider à distinguer ces zones des données véritablement aléatoires ou compressées.

### Chaînes Base64

Le Base64 transforme des données binaires en caractères ASCII imprimables (alphabet `A-Z`, `a-z`, `0-9`, `+`, `/`, avec `=` comme padding). Dans un binaire, une chaîne Base64 apparaît comme une séquence de caractères ASCII — `strings` la trouve, mais sans contexte elle ressemble à du charabia.

La recherche par regex vue plus haut est efficace pour repérer les blocs Base64. Une fois un candidat identifié, vous pouvez le décoder en dehors d'ImHex :

```bash
echo "SGVsbG8gUkUh" | base64 -d
# Hello RE!
```

### Chaînes UTF-16

Les binaires compilés avec MinGW pour Windows ou les binaires qui utilisent des API wide-char stockent les chaînes en UTF-16 : chaque caractère ASCII occupe 2 octets (le caractère suivi d'un octet nul). La chaîne `Hello` devient `48 00 65 00 6C 00 6C 00 6F 00`.

La commande `strings` standard ne les trouve pas (sauf avec l'option `-e l` pour little-endian 16 bits). La recherche String d'ImHex en mode UTF-16 LE les détecte directement.

---

## Recherche de séquences d'opcodes

Au-delà des données, ImHex permet de chercher des **patterns d'instructions machine** directement dans la section `.text`. C'est utile pour localiser des instructions spécifiques sans importer le binaire dans un désassembleur complet.

### Opcodes fréquemment recherchés

Voici des séquences d'opcodes x86-64 que l'on cherche couramment en RE, et les motifs hexadécimaux correspondants :

**L'instruction `int 3` (breakpoint logiciel)** :

```
Motif : CC
```

Un octet `CC` dans `.text` est un breakpoint (`int3`). Les débogueurs insèrent cette instruction pour arrêter l'exécution. Trouver des `CC` persistants dans un binaire peut indiquer une technique anti-débogage (le programme vérifie si ses propres opcodes ont été modifiés — chapitre 19).

**L'instruction `syscall`** :

```
Motif : 0F 05
```

Chercher `0F 05` localise tous les appels système directs dans le binaire. Un binaire typique compilé avec GCC utilise la libc et fait peu de syscalls directs. En trouver beaucoup est un indice de code qui contourne intentionnellement la libc — comportement fréquent dans les shellcodes et les malwares (partie VI).

**L'instruction `nop` (et ses variantes)** :

```
Motif : 90                    # nop simple (1 octet)  
Motif : 0F 1F 00              # nop long (3 octets)  
Motif : 0F 1F 40 00           # nop long (4 octets)  
Motif : 0F 1F 44 00 00        # nop long (5 octets)  
```

GCC utilise des `nop` de différentes tailles pour aligner les fonctions et les boucles sur des frontières d'adresses. Une longue séquence de `nop` signale souvent une frontière de fonction ou du padding d'alignement.

**Les instructions `call` et `jmp` relatives** :

```
Motif : E8 ?? ?? ?? ??        # call rel32 (appel relatif)  
Motif : E9 ?? ?? ?? ??        # jmp rel32 (saut inconditionnel relatif)  
```

`E8` suivi de 4 octets est un `call` avec un déplacement relatif de 32 bits. En cherchant ce motif avec des wildcards, vous pouvez compter le nombre d'appels de fonction dans le binaire — une mesure grossière mais rapide de la complexité du code.

**Les séquences `push rbp; mov rbp, rsp` (prologue de fonction)** :

```
Motif : 55 48 89 E5
```

Ce motif identifie le prologue standard des fonctions compilées par GCC sans l'option `-fomit-frame-pointer` (typiquement en `-O0`). Chercher cette séquence revient à chercher les points d'entrée de fonctions — un raccourci utile quand le binaire est strippé et que vous ne disposez pas d'une table de symboles.

### Précautions avec la recherche d'opcodes

La recherche hexadécimale ne tient pas compte des **frontières d'instructions**. Les octets `0F 05` peuvent apparaître au milieu d'une instruction plus longue sans être un `syscall` — par exemple, dans un immédiat ou un déplacement d'adresse. Pour cette raison, la recherche d'opcodes dans ImHex est un outil de **triage** : elle identifie des candidats que vous devez ensuite vérifier dans le désassembleur (section 6.9) ou dans Ghidra (chapitre 8). Ne concluez jamais qu'une instruction existe uniquement sur la base d'une correspondance hexadécimale.

De même, les octets recherchés peuvent se trouver dans des sections de données (`.rodata`, `.data`) plutôt que dans `.text`. Vérifiez toujours que les résultats se situent dans la plage d'offsets de la section `.text` — votre pattern ELF de la section 6.4 vous donne ces offsets.

---

## L'analyse d'entropie : une recherche par la statistique

Toutes les recherches précédentes supposent que vous savez **quoi** chercher : un magic number précis, une chaîne, un opcode. Mais comment repérer des zones intéressantes quand vous ne savez pas ce qu'elles contiennent ? L'analyse d'entropie est la réponse.

### Le principe

L'entropie de Shannon mesure le degré de « désordre » d'une séquence d'octets, sur une échelle de 0 à 8 bits par octet. En pratique :

- **Entropie proche de 0** — données très régulières : zones de zéros, padding uniforme. On retrouve cela dans `.bss` (non initialisé, rempli de zéros dans le fichier) ou dans les zones d'alignement.  
- **Entropie entre 3 et 5** — données structurées : code machine, texte ASCII, structures de données. Le code x86-64 a une entropie typique autour de 5–6 bits/octet, le texte anglais autour de 4.  
- **Entropie entre 5 et 7** — données compressées ou code machine dense, avec quelques régularités résiduelles.  
- **Entropie proche de 8** — données quasi-aléatoires : contenu chiffré, données compressées de haute qualité (zlib, zstd), clés cryptographiques.

### La vue Information d'ImHex

Accédez à l'analyse d'entropie via **View → Information** (ou parfois intégrée dans **View → Data Information** selon la version). ImHex calcule l'entropie par blocs et affiche un **graphe d'entropie** sur toute la longueur du fichier — l'axe horizontal représente les offsets, l'axe vertical l'entropie locale.

Ce graphe est une **carte thermique** du fichier. D'un coup d'œil, vous repérez :

- Les plateaux de haute entropie (proches de 8) — données chiffrées ou compressées. Si un plateau couvre une région de `.data` ou `.rodata`, le programme contient probablement des données chiffrées qu'il déchiffre à l'exécution.  
- Les pics d'entropie dans `.text` — zones de code particulièrement denses ou tronçons de données embarquées dans le code (tables de constantes, jump tables).  
- Les creux d'entropie — zones de padding, sections `.bss`, tables de chaînes (beaucoup de zéros terminaux).

### Entropie et détection de packing

L'application la plus directe de l'analyse d'entropie en RE est la **détection de packing**. Un binaire packé (compressé ou chiffré) avec UPX ou un packer custom a un profil d'entropie caractéristique : la quasi-totalité du fichier est à haute entropie (> 7), avec seulement un petit stub de décompression en début de `.text` à entropie normale. Ce profil contraste fortement avec un binaire normal où `.text` oscille autour de 5–6 et `.rodata` oscille autour de 4–5.

Nous exploiterons cette technique au chapitre 29 pour détecter et identifier des packers. Pour l'instant, retenez que le graphe d'entropie d'ImHex est un outil de triage immédiat : un coup d'œil suffit pour distinguer un binaire normal d'un binaire packé.

---

## Combiner les techniques : workflow de recherche

En pratique, les différentes techniques de recherche se combinent dans un workflow séquentiel. Voici l'ordre que nous recommandons face à un binaire inconnu, en complément du workflow de triage rapide du chapitre 5.

**D'abord l'entropie.** Ouvrez la vue Information pour obtenir le profil d'entropie global. Identifiez les zones de haute entropie (chiffrement/compression ?), les zones de basse entropie (padding/données structurées) et les zones intermédiaires (code/texte). Posez des bookmarks exploratoires sur les zones remarquables.

**Ensuite les magic bytes.** Lancez des recherches hexadécimales sur les signatures courantes : ELF embarqué, PNG, ZIP, gzip, SQLite, certificats. Chaque résultat vous apprend quelque chose sur les dépendances et les données embarquées du programme.

**Puis les constantes crypto.** Cherchez les premières octets des S-box AES, des IV SHA-256, de la constante ChaCha20. Un hit signale l'utilisation d'un algorithme spécifique et vous oriente vers une analyse cryptographique (chapitre 24).

**Ensuite les chaînes.** Utilisez le panneau Strings d'ImHex ou la recherche String pour les chaînes ASCII et UTF-16. Cherchez aussi par regex les patterns Base64, URLs, adresses IP.

**Enfin les opcodes.** Si vous avez des hypothèses sur le comportement du binaire (fait-il des syscalls directs ? contient-il des `int3` anti-debug ?), cherchez les séquences d'opcodes correspondantes. Vérifiez chaque hit dans le désassembleur.

Ce workflow prend quelques minutes et produit une quantité d'informations considérable sur le binaire avant même d'avoir ouvert un désassembleur.

---

## Résumé

ImHex offre un arsenal de recherche bien plus riche que la commande `strings` : recherche hexadécimale avec wildcards pour les magic bytes et les opcodes, recherche de chaînes multi-encodage (ASCII, UTF-16) pour les textes visibles, recherche regex pour les données encodées (Base64, URLs, IPs), et analyse d'entropie pour cartographier statistiquement les zones chiffrées, compressées, textuelles ou de padding. En combinant ces techniques dans un workflow séquentiel — entropie, magic bytes, constantes crypto, chaînes, opcodes — vous extrayez en quelques minutes une vue d'ensemble du contenu d'un binaire qui guide toute l'analyse ultérieure. Chaque résultat de recherche peut être immédiatement bookmarké et colorisé (section 6.6) pour construire la cartographie progressive du fichier.

---


⏭️ [Intégration avec le désassembleur intégré d'ImHex](/06-imhex/09-desassembleur-integre.md)
