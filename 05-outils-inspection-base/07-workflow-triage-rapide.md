🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 5.7 — Workflow « triage rapide » : la routine des 5 premières minutes face à un binaire

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Introduction

Les sections 5.1 à 5.6 ont présenté chaque outil individuellement. Cette dernière section les assemble en un **workflow reproductible** — une séquence ordonnée de commandes que vous exécuterez systématiquement face à tout nouveau binaire, avant même d'ouvrir un désassembleur.

Ce workflow n'est pas une invention théorique. C'est la routine que pratiquent les analystes en CTF, en audit de sécurité et en reverse engineering professionnel. L'objectif est de construire en moins de 5 minutes une **fiche d'identité** du binaire qui guidera toutes les analyses ultérieures : quel outil utiliser en premier, où concentrer l'attention, quelles hypothèses formuler.

Un triage discipliné évite les erreurs coûteuses : ouvrir un binaire ARM dans un désassembleur x86, perdre du temps à chercher des symboles sur un binaire strippé, lancer un malware hors sandbox, ou ignorer une protection qui rendra inutile une technique d'exploitation.

---

## Vue d'ensemble du workflow

Le triage se décompose en **six étapes**, chacune répondant à une question précise. Les étapes 1 à 5 sont purement statiques (aucune exécution). L'étape 6 est dynamique et **ne doit être réalisée que si le binaire est considéré comme fiable ou dans un environnement sandboxé**.

| Étape | Question | Outil principal | Durée |  
|---|---|---|---|  
| 1 — Identification | Qu'est-ce que c'est ? | `file` | 5 secondes |  
| 2 — Chaînes | Quels textes révélateurs contient-il ? | `strings` | 30 secondes |  
| 3 — Structure ELF | Comment est-il organisé ? | `readelf` | 1 minute |  
| 4 — Symboles et imports | Quelles fonctions contient-il et utilise-t-il ? | `nm`, `readelf -s` | 1 minute |  
| 5 — Protections | Quelles défenses sont en place ? | `checksec` | 15 secondes |  
| 6 — Comportement (sandbox) | Que fait-il au runtime ? | `strace`, `ltrace` | 2 minutes |

---

## Étape 1 — Identification : qu'est-ce que c'est ?

**Objectif** : déterminer le type de fichier, l'architecture cible, et les propriétés de base.

```bash
$ file keygenme_O0
keygenme_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=a3f5...c4e2, for GNU/Linux 3.2.0, not stripped  
```

**Informations à noter** :

- **Format** : ELF, PE, Mach-O, script, archive ? — Détermine quels outils utiliser.  
- **Architecture** : x86-64, ARM, MIPS, RISC-V ? — Détermine quel désassembleur et quelles conventions d'appel s'appliquent.  
- **Linking** : dynamique ou statique ? — Affecte la disponibilité des symboles dynamiques et de la PLT/GOT.  
- **Stripping** : `stripped` ou `not stripped` ? — Détermine si les noms de fonctions locales sont disponibles.  
- **PIE** : `pie executable` ou `executable` ? — Affecte le schéma d'adressage.

**Décisions immédiates** :

- Si le fichier n'est pas un ELF (script, bytecode Java, .NET assembly, archive…), le workflow change complètement. Adaptez votre outillage.  
- Si l'architecture n'est pas x86-64, vérifiez que vos outils la supportent (Ghidra supporte la plupart des architectures ; `objdump` nécessite le bon backend binutils).  
- Si le binaire est statiquement lié, les étapes basées sur les symboles dynamiques (`nm -D`, `ldd`) seront sans résultat.

---

## Étape 2 — Chaînes : quels textes révélateurs contient-il ?

**Objectif** : extraire les chaînes lisibles pour obtenir des indices sur le comportement, les dépendances, et les données sensibles.

```bash
# Extraction brute avec offsets
$ strings -t x keygenme_O0 > /tmp/strings_output.txt

# Recherches ciblées
$ strings keygenme_O0 | grep -iE '(error|fail|denied|invalid|success|grant|password|key|flag)'
$ strings keygenme_O0 | grep -iE '(http|ftp|ssh|/tmp/|/etc/|\.conf|\.log)'
$ strings keygenme_O0 | grep -iE '(aes|sha|md5|rsa|encrypt|decrypt|crypt)'
$ strings keygenme_O0 | grep -E '(GLIBC|GCC|clang|rustc|go\.|Go build)'
$ strings keygenme_O0 | grep '%'
```

**Informations à noter** :

- **Messages utilisateur** : prompts, messages d'erreur, messages de succès — ils révèlent le flux du programme.  
- **Formats de données** : patterns comme `XXXX-XXXX-XXXX`, adresses email, formats de date — ils révèlent les inputs attendus.  
- **Chemins et URLs** : fichiers ouverts, serveurs contactés, fichiers de configuration.  
- **Données sensibles** : mots de passe hardcodés, clés d'API, tokens, constantes cryptographiques.  
- **Informations de compilation** : version de GCC/Clang/rustc, nom des fichiers sources — renseignent sur l'environnement de développement.  
- **Formats printf** : `%s`, `%d`, `%x` — chaque format est un indice sur le type de données manipulées.

**Décisions immédiates** :

- Si les chaînes sont anormalement rares ou charabia, le binaire est peut-être packé ou obfusqué. Notez cette hypothèse pour l'étape 3.  
- Si vous trouvez des chemins réseau, des URLs suspectes ou des commandes shell, traitez le binaire comme potentiellement malveillant et ne l'exécutez qu'en sandbox.

---

## Étape 3 — Structure ELF : comment est-il organisé ?

**Objectif** : disséquer les headers, les sections et les segments pour comprendre la structure interne et détecter les anomalies.

```bash
# ELF header : entry point, type, nombre de sections
$ readelf -hW keygenme_O0

# Sections : liste complète avec flags
$ readelf -SW keygenme_O0

# Segments : permissions mémoire et mapping sections→segments
$ readelf -lW keygenme_O0

# Dépendances directes
$ readelf -d keygenme_O0 | grep NEEDED
```

**Informations à noter** :

- **Entry point** : adresse de `_start`. Point de départ pour le désassemblage si les symboles sont absents.  
- **Sections présentes/absentes** : `.symtab` présente = non strippé. `.debug_*` présentes = symboles DWARF. Absence de sections standards ou présence de sections aux noms inhabituels = possible packing ou obfuscation.  
- **Taille de `.text`** : donne une estimation de la quantité de code à analyser. Un `.text` minuscule avec une section inconnue volumineuse suggère un packer.  
- **Permissions des segments** : vérifier qu'aucun segment n'est `RWE` (signe de NX désactivé ou de self-modifying code).  
- **Dépendances `NEEDED`** : chaque bibliothèque est un indice fonctionnel.

**Détection d'anomalies structurelles** :

Certains patterns dans les sections signalent un binaire non-standard :

```bash
# Entropie des sections (indicateur de packing/chiffrement)
# Une section avec une entropie proche de 8.0 (maximum) est probablement compressée ou chiffrée
# Cet aspect sera détaillé au chapitre 29, mais garder l'œil ouvert dès le triage

# Sections avec des noms non-standard
$ readelf -SW keygenme_O0 | grep -vE '\.(text|data|bss|rodata|symtab|strtab|shstrtab|dynsym|dynstr|plt|got|init|fini|comment|note|gnu|rela|eh_frame|interp|dynamic)'

# Taille de .text vs taille totale du fichier
$ readelf -SW keygenme_O0 | grep '\.text'
$ ls -l keygenme_O0
```

Si `.text` ne représente qu'une fraction minuscule du fichier et qu'une section inconnue occupe la majorité de l'espace, c'est un signe fort de packing : le code réel est compressé/chiffré dans la grosse section, et le petit `.text` contient uniquement le stub de décompression.

**Complément sur un binaire fiable** :

```bash
# Résolution complète des chemins (uniquement si le binaire est fiable !)
$ ldd keygenme_O0
```

---

## Étape 4 — Symboles et imports : quelles fonctions contient-il et utilise-t-il ?

**Objectif** : dresser la carte des fonctions du programme et des fonctions de bibliothèques qu'il utilise.

```bash
# Sur un binaire non strippé : table complète des symboles
$ nm -n keygenme_O0 | grep ' [TtWw] '

# Sur un binaire strippé : uniquement les symboles dynamiques (imports)
$ nm -D keygenme_O0

# Vue détaillée avec tailles
$ nm -nS keygenme_O0 | grep ' T '

# Pour du C++ : démangling
$ nm -nC programme_cpp | grep ' T '
```

**Informations à noter** :

- **Fonctions du programme** (symboles `T`/`t`) : leur nom, leur adresse, leur taille estimée. Les noms de fonctions sont souvent le meilleur résumé de l'architecture d'un programme.  
- **Fonctions importées** (symboles `U`) : `strcmp` = comparaison de chaînes, `socket`/`connect` = réseau, `EVP_*` = OpenSSL, `pthread_*` = multi-threading, `dlopen`/`dlsym` = chargement dynamique de plugins.  
- **Nombre de fonctions** : un programme avec 5 fonctions se reverse en une heure ; un programme avec 500 fonctions demande une stratégie de priorisation.

**Tableau des imports révélateurs** :

| Imports détectés | Fonctionnalité probable |  
|---|---|  
| `strcmp`, `strncmp`, `memcmp` | Comparaison (crackme, authentification) |  
| `socket`, `connect`, `send`, `recv` | Communication réseau |  
| `EVP_*`, `AES_*`, `SHA*`, `RSA_*` | Chiffrement (OpenSSL) |  
| `fopen`, `fread`, `fwrite`, `mmap` | Manipulation de fichiers |  
| `fork`, `execve`, `system`, `popen` | Lancement de processus/commandes |  
| `dlopen`, `dlsym` | Chargement dynamique de bibliothèques/plugins |  
| `pthread_create`, `pthread_mutex_*` | Multi-threading |  
| `ptrace` | Anti-debugging (ou debugging) |  
| `getenv`, `setenv` | Lecture de variables d'environnement |

---

## Étape 5 — Protections : quelles défenses sont en place ?

**Objectif** : inventorier les protections de sécurité pour adapter la stratégie d'analyse et d'exploitation.

```bash
$ checksec --file=keygenme_O0
```

**Informations à noter** :

- **NX** : activé ou non. Impact sur l'exploitabilité des buffer overflows.  
- **PIE** : activé ou non. Impact sur la prévisibilité des adresses.  
- **Canary** : présent ou non. Impact sur les buffer overflows stack-based.  
- **RELRO** : No / Partial / Full. Impact sur les GOT overwrite.  
- **FORTIFY** : fortifié ou non. Impact sur les dépassements de buffer libc.

Si `checksec` n'est pas disponible, reproduisez les vérifications avec `readelf` (voir le tableau de correspondance en section 5.6).

---

## Étape 6 — Comportement dynamique (sandbox uniquement)

> ⚠️ **Cette étape implique l'exécution du binaire.** Ne procédez que si le binaire est de confiance (CTF, binaire compilé par vous) ou dans un environnement isolé (VM sandboxée, chapitre 26). En cas de doute, arrêtez-vous à l'étape 5.

**Objectif** : observer le comportement réel du programme — interactions avec le système de fichiers, le réseau, l'utilisateur.

```bash
# Appels système : quels fichiers, sockets, processus ?
$ strace -e trace=file,network,process -s 256 -o strace.log ./keygenme_O0

# Appels de bibliothèques : quels arguments pour strcmp, printf, etc. ?
$ ltrace -s 256 -o ltrace.log ./keygenme_O0

# Profil statistique rapide
$ strace -c ./keygenme_O0 <<< "test"
$ ltrace -c ./keygenme_O0 <<< "test"
```

**Informations à noter** :

- **Fichiers accédés** : quels fichiers le programme ouvre-t-il en dehors de ses bibliothèques ? Des accès à `/etc/passwd`, `/proc/self/status`, ou des fichiers temporaires sont des comportements à investiguer.  
- **Connexions réseau** : adresses IP, ports, protocoles — c'est le point de départ de l'analyse réseau (chapitre 23).  
- **Comparaisons de chaînes** : `ltrace` peut révéler directement les clés, mots de passe ou valeurs attendues si le programme utilise `strcmp`/`strncmp`.  
- **Processus enfants** : le programme lance-t-il d'autres commandes ? `fork` + `execve` sur `/bin/sh` est un comportement suspect.  
- **Modifications mémoire** : des `mprotect` avec `PROT_EXEC` suggèrent du self-modifying code ou de l'unpacking.

---

## Le rapport de triage

À l'issue des 6 étapes, rassemblez vos observations dans un rapport structuré. Ce rapport servira de référence tout au long de l'analyse et pourra être partagé avec d'autres analystes.

Voici un modèle de rapport de triage :

```markdown
# Rapport de triage — [nom du binaire]

## 1. Identification
- **Fichier** : keygenme_O0
- **Format** : ELF 64-bit LSB PIE executable
- **Architecture** : x86-64
- **Linking** : dynamique (libc.so.6)
- **Stripping** : non strippé (symboles disponibles)
- **Compilateur** : GCC 13.2.0 (Ubuntu)

## 2. Chaînes notables
- Messages : "Enter your license key:", "Access granted!", "Access denied."
- Format attendu : XXXX-XXXX-XXXX-XXXX
- Donnée suspecte : "SuperSecret123" (possible clé hardcodée)
- Fonctions libc : strcmp, strlen, printf, puts

## 3. Structure
- Entry point : 0x10c0
- Sections : 31 (dont .symtab, .strtab — symboles complets)
- Taille .text : 0x225 (549 octets) — programme court
- Dépendances : libc.so.6 uniquement
- Anomalies : aucune

## 4. Fonctions identifiées
- main (0x1189, 108 octets)
- check_license (0x11f5, 139 octets) ← cible principale
- generate_expected_key (0x1280, 53 octets)
- Imports critiques : strcmp, strlen

## 5. Protections
- NX : activé
- PIE : activé
- Canary : présent
- RELRO : Full
- FORTIFY : non

## 6. Comportement dynamique
- Demande une clé en entrée, la compare avec strcmp
- ltrace révèle la comparaison : input vs "K3Y9-AX7F-QW2M-PL8N"
- Aucune activité réseau
- Aucune activité fichier suspecte
- Code de retour : 0 (succès) ou 1 (échec)

## Conclusion et stratégie
Programme simple de type crackme. Trois approches possibles :
1. La clé est directement visible dans ltrace → résolution immédiate.
2. Analyse statique de check_license dans Ghidra pour comprendre l'algorithme.
3. Résolution automatique avec angr via les adresses de succès/échec.
```

Ce rapport tient en une page et contient tout ce qu'un analyste a besoin de savoir pour décider de la suite. Le temps total de production : moins de 5 minutes.

---

## Adapter le workflow selon le contexte

Le workflow ci-dessus est le cas général. Selon le contexte, certaines étapes prennent plus d'importance ou nécessitent des ajustements :

### CTF / Crackme

La priorité est la vitesse. `strings` + `ltrace` peuvent suffire à résoudre un challenge simple en quelques secondes. Si `ltrace` ne révèle rien, passez directement au désassembleur.

### Audit de sécurité

L'étape 5 (protections) est centrale. Documentez chaque protection absente comme un finding. L'étape 4 (imports) révèle les fonctions dangereuses utilisées (`gets`, `strcpy`, `sprintf` sans format constant).

### Analyse de malware

**N'exécutez jamais l'étape 6 hors sandbox.** Remplacez `ldd` par `readelf -d | grep NEEDED`. Portez une attention particulière aux chaînes réseau (C2 addresses), aux imports suspects (`ptrace`, `fork`, `execve`, `unlink`), aux sections non-standard, et à l'entropie élevée (packing).

### Binaire legacy / sans sources

L'étape 4 est cruciale pour comprendre l'architecture du programme. Passez plus de temps sur les noms de fonctions et les imports pour construire un modèle mental avant d'ouvrir le désassembleur.

---

## Automatiser le triage

Une fois le workflow maîtrisé manuellement, il est naturel de vouloir l'automatiser. Le dépôt de cette formation inclut un script `scripts/triage.py` qui exécute les étapes 1 à 5 automatiquement et produit un rapport structuré. Le chapitre 35 (section 35.6) reviendra sur la construction de votre propre toolkit d'automatisation.

En attendant, voici l'enchaînement minimal en une séquence de commandes shell :

```bash
#!/bin/bash
# triage_minimal.sh — Triage rapide d'un binaire ELF
BINARY="$1"

echo "=== IDENTIFICATION ==="  
file "$BINARY"  
echo ""  

echo "=== CHAÎNES NOTABLES ==="  
strings "$BINARY" | grep -iE '(error|fail|denied|success|grant|password|key|flag|http|ftp|/tmp/|/etc/)' | head -20  
echo ""  

echo "=== COMPILATEUR ==="  
strings "$BINARY" | grep -iE '(GCC|clang|rustc|Go build)' | head -5  
echo ""  

echo "=== ELF HEADER ==="  
readelf -hW "$BINARY" 2>/dev/null | grep -E '(Type|Machine|Entry)'  
echo ""  

echo "=== SECTIONS ==="  
readelf -SW "$BINARY" 2>/dev/null | grep -E '\.(text|data|bss|rodata|symtab|strtab|dynsym)'   
echo ""  

echo "=== DÉPENDANCES ==="  
readelf -d "$BINARY" 2>/dev/null | grep NEEDED  
echo ""  

echo "=== SYMBOLES (fonctions globales) ==="  
nm -n "$BINARY" 2>/dev/null | grep ' T ' | grep -v -E '(_start|_init|_fini|__libc|_IO_|__do_global|register_tm|deregister_tm|frame_dummy)' | head -20  
echo ""  

echo "=== IMPORTS ==="  
nm -D "$BINARY" 2>/dev/null | grep ' U ' | head -20  
echo ""  

echo "=== PROTECTIONS ==="  
checksec --file="$BINARY" 2>/dev/null || echo "(checksec non disponible — utiliser readelf manuellement)"  
```

```bash
$ chmod +x triage_minimal.sh
$ ./triage_minimal.sh keygenme_O0
```

Ce script ne remplace pas l'analyse humaine — il accélère la collecte d'informations brutes. L'interprétation, la formulation d'hypothèses et le choix de la stratégie restent votre travail.

---

## Ce qu'il faut retenir pour la suite

- **Le triage est une discipline, pas une option.** Les 5 premières minutes face à un binaire déterminent l'efficacité des heures qui suivent. Un triage bâclé mène à des impasses ; un triage rigoureux ouvre les bonnes pistes.  
- **L'ordre des étapes n'est pas arbitraire.** On commence par le statique et le sûr (pas d'exécution), on termine par le dynamique et le risqué (exécution contrôlée). Chaque étape enrichit le contexte de la suivante.  
- **Documentez vos observations.** Un rapport de triage, même minimal, est une référence précieuse quand l'analyse s'allonge. Il évite de refaire le même travail et facilite le partage avec d'autres analystes.  
- **Adaptez le workflow au contexte.** CTF, audit, malware, legacy — les priorités changent, mais la structure reste la même.  
- **Automatisez les tâches répétitives.** Une fois le workflow intériorisé, scriptez-le. Votre futur vous en remerciera.

Avec ce workflow de triage maîtrisé, vous êtes prêt à passer à l'étape suivante : l'analyse approfondie avec les éditeurs hexadécimaux avancés (chapitre 6), le désassemblage (chapitres 7–9), et le débogage (chapitre 11).

---


⏭️ [🎯 Checkpoint : réaliser un triage complet du binaire `mystery_bin` fourni, rédiger un rapport d'une page](/05-outils-inspection-base/checkpoint.md)
