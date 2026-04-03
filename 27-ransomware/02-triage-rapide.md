🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 27.2 — Triage rapide : `file`, `strings`, `checksec`, premières hypothèses

> 🕐 **Temps visé** : 5 à 10 minutes maximum.  
>  
> Le triage rapide est la première phase de toute analyse de binaire suspect. L'objectif n'est pas de tout comprendre, mais de formuler un maximum d'**hypothèses exploitables** en un minimum de temps, sans jamais exécuter le binaire. Tout se fait en analyse statique passive.  
>  
> Cette section applique le workflow de triage du [Chapitre 5, section 5.7](/05-outils-inspection-base/07-workflow-triage-rapide.md) à notre sample `ransomware_O2_strip` — la variante strippée, celle qui simule le cas réel.

---

## Étape 1 — `file` : identification du format

La toute première commande face à un binaire inconnu est `file`. Elle identifie le type de fichier en analysant ses magic bytes et ses headers, sans l'exécuter.

```bash
$ file ransomware_O2_strip
ransomware_O2_strip: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, stripped  
```

Chaque fragment de cette sortie est une information exploitable :

| Fragment | Interprétation |  
|---|---|  
| `ELF 64-bit LSB` | Binaire natif Linux, architecture 64 bits, little-endian. Pas du bytecode (.NET, Java), pas un script. |  
| `pie executable` | Compilé en Position-Independent Executable. Les adresses seront relatives — ASLR actif. |  
| `x86-64` | Architecture Intel/AMD 64 bits. On travaillera avec les registres `rax`, `rdi`, `rsi`, etc. |  
| `dynamically linked` | Le binaire dépend de bibliothèques partagées (`.so`). On pourra les identifier avec `ldd`. |  
| `interpreter /lib64/ld-linux-x86-64.so.2` | Loader standard GNU/Linux. Rien d'exotique. |  
| `stripped` | Les symboles de débogage ont été supprimés. Pas de noms de fonctions internes, pas de DWARF. L'analyse sera plus difficile. |

**Première hypothèse** : nous avons affaire à un binaire natif ELF x86-64 classique, compilé avec une toolchain GNU standard, lié dynamiquement. Le fait qu'il soit strippé suggère une volonté de compliquer l'analyse — comportement typique d'un binaire malveillant.

---

## Étape 2 — `ldd` : dépendances dynamiques

Puisque `file` nous indique un binaire dynamiquement lié, listons ses dépendances :

```bash
$ ldd ransomware_O2_strip
    linux-vdso.so.1 (0x00007fff...)
    libssl.so.3 => /lib/x86_64-linux-gnu/libssl.so.3 (0x00007f...)
    libcrypto.so.3 => /lib/x86_64-linux-gnu/libcrypto.so.3 (0x00007f...)
    libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f...)
    /lib64/ld-linux-x86-64.so.2 (0x00007f...)
```

> ⚠️ **Rappel de sécurité** : `ldd` peut théoriquement déclencher l'exécution de code via le loader sur certaines configurations. Sur un binaire réellement suspect, préférez `objdump -p <binaire> | grep NEEDED` ou `readelf -d <binaire> | grep NEEDED`, qui lisent passivement les headers ELF sans invoquer le loader. Dans notre cas contrôlé, `ldd` est sans risque.

La présence de `libssl` et `libcrypto` est un **signal fort**. Un programme qui dépend d'OpenSSL utilise très probablement des primitives cryptographiques — chiffrement, hachage, signatures, ou TLS. Combinée à un binaire strippé d'origine inconnue, cette dépendance oriente immédiatement l'analyse vers une piste crypto.

**Hypothèse affinée** : le binaire effectue des opérations cryptographiques via OpenSSL. Reste à déterminer lesquelles (chiffrement symétrique ? asymétrique ? hachage ? TLS ?) et dans quel but.

---

## Étape 3 — `strings` : extraction des chaînes lisibles

La commande `strings` extrait les séquences de caractères imprimables d'un binaire. C'est souvent la source d'information la plus riche lors du triage. Sur un binaire strippé, les chaînes sont parfois le seul indice textuel restant.

```bash
$ strings ransomware_O2_strip | head -80
```

Plutôt que de lire un flux brut de centaines de lignes, procédons par recherches ciblées.

### 3a — Recherche de termes liés au système de fichiers

```bash
$ strings ransomware_O2_strip | grep -iE '(tmp|path|dir|file|open|read|write|unlink)'
```

Sortie attendue (extraits pertinents) :

```
/tmp/test
.locked
README_LOCKED.txt
```

Trois chaînes extrêmement parlantes. `/tmp/test` désigne un répertoire cible. `.locked` est une extension ajoutée à des fichiers. `README_LOCKED.txt` évoque une note déposée sur le système. Ce triptyque — répertoire cible, extension de fichiers transformés, note textuelle — est la signature comportementale classique d'un **ransomware** ou d'un outil de chiffrement de fichiers.

### 3b — Recherche de termes liés à la cryptographie

```bash
$ strings ransomware_O2_strip | grep -iE '(aes|crypt|encrypt|decrypt|key|cipher|EVP|ssl)'
```

Sortie attendue (extraits pertinents) :

```
EVP_EncryptInit_ex  
EVP_EncryptUpdate  
EVP_EncryptFinal_ex  
EVP_CIPHER_CTX_new  
EVP_CIPHER_CTX_free  
```

Ces noms de fonctions apparaissent parce qu'ils font partie de la table des symboles dynamiques (`.dynsym`) — ils ne sont pas supprimés par `strip`, car le loader en a besoin au runtime pour résoudre les appels à OpenSSL. C'est une information capitale : le binaire utilise l'API EVP d'OpenSSL pour du **chiffrement** (pas du déchiffrement — on ne voit pas `EVP_DecryptInit_ex`). Le préfixe `Encrypt` dans les trois fonctions confirme le sens de l'opération.

### 3c — Recherche de la clé et de messages

```bash
$ strings ransomware_O2_strip | grep -iE '(reverse|engineer|password|secret|key)'
```

Sortie attendue :

```
REVERSE_ENGINEERING_IS_FUN_2025!
```

Une chaîne de 32 caractères (32 octets) composée de caractères ASCII imprimables. Sa longueur correspond exactement à une clé AES-256. Elle est stockée dans la section `.rodata`, en clair, sans aucune obfuscation. À ce stade, nous avons une **candidate sérieuse** pour la clé de chiffrement.

### 3d — Contenu de la ransom note

```bash
$ strings ransomware_O2_strip | grep -A 15 "CHIFFRES"
```

Sortie attendue :

```
========================================
  VOS FICHIERS ONT ETE CHIFFRES !
========================================
Ceci est un exercice pedagogique.  
Formation Reverse Engineering  
Algorithme : AES-256-CBC  
La cle est dans le binaire. Trouvez-la.  
Indice : cherchez les constantes de 32 octets...  
========================================
```

La note confirme explicitement l'algorithme utilisé : **AES-256-CBC**. Dans un cas réel, la note ne serait évidemment pas aussi bavarde, mais il n'est pas rare que les ransomwares mentionnent l'algorithme utilisé pour convaincre la victime que le chiffrement est solide et que payer est la seule option.

### 3e — Recherche du magic header

```bash
$ strings ransomware_O2_strip | grep -i "RWARE"
```

Sortie attendue :

```
RWARE27
```

Cette chaîne de 7 caractères est probablement un **magic header** écrit au début des fichiers chiffrés. C'est un marqueur d'identification du format de sortie — utile pour une règle YARA et pour le parser ImHex.

### Synthèse de `strings`

En quelques commandes `grep`, nous avons extrait :

| Élément | Valeur | Signification |  
|---|---|---|  
| Répertoire cible | `/tmp/test` | Périmètre de l'attaque |  
| Extension | `.locked` | Renommage post-chiffrement |  
| Ransom note | `README_LOCKED.txt` | Note déposée pour la victime |  
| API crypto | `EVP_Encrypt*` | Chiffrement via OpenSSL EVP |  
| Algorithme | `AES-256-CBC` | Mentionné dans la note |  
| Clé candidate | `REVERSE_ENGINEERING_IS_FUN_2025!` | 32 octets, candidate AES-256 |  
| Magic header | `RWARE27` | Signature des fichiers chiffrés |

---

## Étape 4 — `readelf` : anatomie des sections et symboles dynamiques

Pour compléter le triage, examinons la structure ELF de plus près.

### Headers et sections

```bash
$ readelf -h ransomware_O2_strip
```

Les champs pertinents à noter sont le type (`DYN` pour un PIE executable), le point d'entrée (qui ne correspondra pas directement à `main` — voir le rôle de `_start` et `__libc_start_main`), et la machine (`Advanced Micro Devices X86-64`).

```bash
$ readelf -S ransomware_O2_strip | grep -E '(\.text|\.rodata|\.data|\.bss|\.plt|\.got)'
```

Sortie typique (numéros et adresses simplifiés) :

```
  [14] .text         PROGBITS   ...  AX  ...
  [16] .rodata       PROGBITS   ...  A   ...
  [23] .got          PROGBITS   ...  WA  ...
  [24] .got.plt      PROGBITS   ...  WA  ...
  [25] .data         PROGBITS   ...  WA  ...
  [26] .bss          NOBITS     ...  WA  ...
```

La section `.rodata` (read-only data) est celle qui contient nos chaînes de caractères et nos constantes — c'est là que la clé AES et l'IV résident en mémoire. La section `.text` contient le code exécutable. Les sections `.got` et `.got.plt` sont impliquées dans la résolution dynamique des appels à OpenSSL (mécanisme PLT/GOT vu au [Chapitre 2, section 2.9](/02-chaine-compilation-gnu/09-plt-got-lazy-binding.md)).

### Symboles dynamiques

```bash
$ readelf -s --dyn-syms ransomware_O2_strip | grep -i "FUNC"
```

Cette commande liste les fonctions importées depuis les bibliothèques partagées. On y retrouvera les appels OpenSSL (`EVP_*`), les fonctions libc standard (`fopen`, `fread`, `fwrite`, `fclose`, `malloc`, `free`, `opendir`, `readdir`, `stat`, `unlink`, `printf`, `fprintf`...), et éventuellement des indices sur le flux du programme.

La présence conjointe de `opendir`/`readdir` (parcours de répertoire), `stat` (vérification du type de fichier), `fopen`/`fread`/`fwrite` (lecture/écriture), `unlink` (suppression) et `EVP_Encrypt*` (chiffrement) dessine un scénario cohérent : le programme **parcourt une arborescence, lit des fichiers, les chiffre, écrit le résultat, et supprime les originaux**.

---

## Étape 5 — `checksec` : inventaire des protections

L'outil `checksec` (fourni par `pwntools` ou installable séparément) analyse les protections de sécurité compilées dans le binaire :

```bash
$ checksec --file=ransomware_O2_strip
```

Sortie typique :

```
    Arch:     amd64-64-little
    RELRO:    Partial RELRO
    Stack:    Canary found
    NX:       NX enabled
    PIE:      PIE enabled
```

Interprétons chaque ligne dans le contexte de notre analyse :

| Protection | Valeur | Impact sur le RE |  
|---|---|---|  
| **RELRO** | Partial | La GOT est partiellement protégée. Les entrées PLT/GOT restent modifiables après le premier appel (lazy binding). Un hook via GOT overwrite serait théoriquement possible. |  
| **Stack Canary** | Présent | GCC a inséré des canaris de pile (`-fstack-protector`). Présence de `__stack_chk_fail` dans les symboles dynamiques. Pas d'impact direct sur notre analyse, mais indique une compilation avec les flags de sécurité par défaut. |  
| **NX** | Activé | La pile n'est pas exécutable. Pas d'impact sur le RE pur, mais pertinent si on envisageait une exploitation. |  
| **PIE** | Activé | Confirme ce que `file` indiquait : les adresses sont relatives. Dans GDB, il faudra attendre le chargement en mémoire pour connaître les adresses absolues, ou travailler en offsets. |

`checksec` ne révèle pas directement le comportement du programme, mais il complète le profil du binaire. L'absence de protections anti-debug spécifiques (pas de `ptrace` détecté, pas d'obfuscation de flux de contrôle) suggère que le binaire ne cherche pas activement à empêcher l'analyse — ce qui est cohérent avec la conception de notre sample.

---

## Étape 6 — Vue hexadécimale rapide : repérage de l'IV

Avant de passer à l'analyse approfondie, un coup d'œil en hexadécimal sur la section `.rodata` peut révéler des constantes non-ASCII que `strings` ne capture pas :

```bash
$ objdump -s -j .rodata ransomware_O2_strip | head -50
```

En parcourant la sortie, on cherche des séquences reconnaissables. À proximité de la clé ASCII `REVERSE_ENGINEERING_IS_FUN_2025!`, on devrait repérer une séquence de 16 octets :

```
dead beef cafe babe 1337 4242 feed face
```

Ces valeurs — `0xDEADBEEF`, `0xCAFEBABE`, `0x1337`, `0xFEEDFACE` — sont des magic numbers classiques du monde système. Leur regroupement en un bloc de 16 octets (128 bits), situé à proximité immédiate d'une clé de 32 octets dans `.rodata`, constitue une **candidate très probable pour un IV AES**. En mode CBC, le vecteur d'initialisation fait exactement un bloc, soit 16 octets pour AES — la taille correspond.

> 💡 **Astuce** : la commande `objdump -s -j .rodata` est un bon compromis entre `strings` (qui ne montre que l'ASCII) et un hex editor complet (qui nécessite d'ouvrir l'outil). Pour une exploration plus poussée, ImHex prend le relais (section 27.3).

---

## Synthèse : tableau des hypothèses

Au terme de ce triage de 5 à 10 minutes, sans avoir ouvert de désassembleur ni exécuté le binaire, voici l'ensemble des hypothèses formulées :

| # | Hypothèse | Confiance | Source |  
|---|---|---|---|  
| H1 | Le binaire est un ransomware ciblant `/tmp/test/` | Forte | `strings` : répertoire cible, extension `.locked`, ransom note |  
| H2 | L'algorithme utilisé est AES-256-CBC | Forte | `strings` : note explicite + présence d'`EVP_Encrypt*` dans `.dynsym` |  
| H3 | La clé AES-256 est `REVERSE_ENGINEERING_IS_FUN_2025!` | Moyenne | `strings` : chaîne de 32 octets dans `.rodata`, à confirmer en dynamique |  
| H4 | L'IV est `DEADBEEF CAFEBABE 1337 4242 FEEDFACE` | Moyenne | `objdump -s .rodata` : 16 octets à proximité de la clé, à confirmer |  
| H5 | Les fichiers chiffrés portent un header `RWARE27` | Moyenne | `strings` : présence de la chaîne, format exact à cartographier avec ImHex |  
| H6 | Le binaire parcourt récursivement les fichiers et supprime les originaux | Forte | `readelf --dyn-syms` : `opendir`, `readdir`, `stat`, `unlink` |  
| H7 | Aucune communication réseau | Moyenne | Absence de `connect`, `send`, `recv`, `socket` dans `.dynsym`, à confirmer avec `strace` |  
| H8 | Pas de mécanisme anti-debug | Faible | `checksec` normal, mais absence de preuve n'est pas preuve d'absence |

Les hypothèses marquées « Moyenne » ou « Faible » devront être confirmées ou infirmées lors de l'analyse statique approfondie (section 27.3) et de l'analyse dynamique (section 27.5). Les hypothèses « Forte » reposent sur des indices multiples convergents.

---

## Réflexe méthodologique : documenter au fil de l'eau

Un analyste professionnel ne garde pas ces observations en tête : il les consigne immédiatement. Prenez l'habitude de tenir un fichier de notes structuré dès le triage. Même un simple fichier texte suffit :

```
# Triage — ransomware_O2_strip
Date : 2025-xx-xx  
Analyste : [votre nom]  

## Identité
- ELF 64-bit, x86-64, PIE, dynamically linked, stripped
- Dépendances : libssl, libcrypto, libc

## IOC (Indicators of Compromise)
- Fichier : ransomware_O2_strip (SHA256 : ...)
- Répertoire cible : /tmp/test
- Extension ajoutée : .locked
- Ransom note : README_LOCKED.txt
- Magic header fichiers chiffrés : RWARE27

## Hypothèses crypto
- Algorithme probable : AES-256-CBC (via OpenSSL EVP)
- Clé candidate : REVERSE_ENGINEERING_IS_FUN_2025! (32 bytes)
- IV candidat : DEADBEEF CAFEBABE 1337 4242 FEEDFACE (16 bytes)

## À confirmer
- [ ] Clé et IV réellement utilisés (GDB/Frida sur EVP_EncryptInit_ex)
- [ ] Format exact du fichier .locked (ImHex)
- [ ] Absence de communication réseau (strace)
- [ ] Absence d'anti-debug (analyse dynamique)
```

Ce document évoluera tout au long de l'analyse et servira de base au rapport final (section 27.7). Calculer le hash SHA-256 du binaire dès le triage (`sha256sum ransomware_O2_strip`) est également un réflexe essentiel : c'est l'identifiant unique du sample, utilisable pour les recherches sur VirusTotal, les échanges entre analystes et la traçabilité du rapport.

---

## Ce que le triage ne nous dit pas

Il est tout aussi important de noter ce que le triage rapide **ne permet pas** de déterminer :

- **La clé est-elle réellement celle trouvée par `strings` ?** — Il pourrait s'agir d'un leurre, d'une clé de test non utilisée, ou d'une chaîne sans rapport. Seule l'analyse dynamique (breakpoint sur `EVP_EncryptInit_ex`) confirmera quel buffer est effectivement passé comme argument.  
- **Le format exact du fichier `.locked`** — On sait qu'un header `RWARE27` existe, mais sa taille, les champs qui suivent, et l'agencement des données chiffrées restent à cartographier.  
- **Le mode opératoire précis** — L'ordre des opérations (lire, chiffrer, écrire, supprimer), la gestion des erreurs, le traitement des fichiers vides ou très volumineux ne sont pas visibles depuis le triage.  
- **L'existence de fonctionnalités cachées** — Un binaire peut contenir du code mort, des branches conditionnelles rarement atteintes, ou des fonctionnalités activées par des arguments en ligne de commande. Le triage n'explore que les données statiques, pas le flux de contrôle.

C'est précisément pour répondre à ces questions que les étapes suivantes — analyse statique approfondie dans Ghidra et ImHex — sont nécessaires.

⏭️ [Analyse statique : Ghidra + ImHex (repérer les constantes AES, flux de chiffrement)](/27-ransomware/03-analyse-statique-ghidra-imhex.md)
