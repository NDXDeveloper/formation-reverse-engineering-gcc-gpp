🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 15.3 — libFuzzer : fuzzing in-process avec sanitizers

> 🔗 **Prérequis** : Section 15.2 (AFL++, concepts d'instrumentation et de corpus), Chapitre 14 (sanitizers ASan/UBSan/MSan), Chapitre 2 (flags de compilation GCC/Clang)

---

## AFL++ vs libFuzzer : deux philosophies

En section 15.2, nous avons vu AFL++ fuzzer un **programme entier** : à chaque itération, AFL++ lance un nouveau processus, lui fournit un input via un fichier ou stdin, attend sa terminaison, puis analyse le résultat. Ce modèle *fork-exec* est universel — il fonctionne avec n'importe quel programme qui accepte une entrée — mais il a un coût : la création d'un processus à chaque exécution consomme du temps (même avec le *forkserver* optimisé d'AFL++).

libFuzzer adopte une approche radicalement différente : le **fuzzing in-process**. Au lieu de lancer le programme entier à chaque itération, libFuzzer appelle directement une **fonction cible** à l'intérieur du même processus, en boucle, sans jamais faire de `fork`. Le programme n'est lancé qu'une seule fois ; c'est la fonction de parsing qui est appelée des millions de fois avec des inputs différents.

Les conséquences sont immédiates :

- **Vitesse** — Sans le coût du fork, libFuzzer peut atteindre des dizaines de milliers à des centaines de milliers d'exécutions par seconde sur des fonctions rapides. C'est typiquement 5 à 50 fois plus rapide qu'AFL++ sur la même cible.  
- **Précision du ciblage** — On fuzze exactement la fonction qui nous intéresse, pas le programme entier. En contexte RE, cela signifie qu'on peut cibler directement la routine de parsing identifiée dans Ghidra, sans se soucier de la logique d'initialisation, de la lecture du fichier, ou de la gestion des arguments.  
- **Couplage natif avec les sanitizers** — libFuzzer fait partie du projet LLVM, tout comme ASan, UBSan et MSan. Leur intégration est native et optimisée.

La contrepartie est qu'il faut écrire un petit morceau de code — le **harness** (ou *fuzz target*) — qui fait le pont entre libFuzzer et la fonction à fuzzer. C'est un investissement minime qui rapporte énormément en termes de vitesse et de précision.

> 💡 **Pour le RE** — libFuzzer est l'outil idéal quand vous avez identifié une fonction de parsing spécifique dans le binaire et que vous voulez explorer exhaustivement ses chemins internes. AFL++ est préférable quand vous voulez fuzzer le programme « en boîte noire », sans savoir exactement quelle fonction cibler.

---

## Prérequis : Clang obligatoire

libFuzzer est un composant de la toolchain LLVM/Clang. Contrairement à AFL++ qui fonctionne aussi bien avec GCC qu'avec Clang, **libFuzzer nécessite Clang**. C'est une contrainte à accepter dans le cadre de cette formation centrée sur la chaîne GNU : on utilisera GCC pour tout le reste, mais Clang pour le fuzzing libFuzzer.

En pratique, cela ne pose aucun problème de compatibilité. Les binaires produits par Clang et GCC sont interopérables au niveau ABI (ils utilisent les mêmes conventions d'appel System V AMD64, les mêmes formats ELF, le même linker). Un harness compilé avec Clang peut parfaitement appeler du code compilé séparément avec GCC, tant que le linkage est correct.

### Installer Clang et les runtimes de fuzzing

Sur Debian 12+ / Ubuntu 22.04+ :

```bash
$ sudo apt install -y clang llvm lld
```

Vérifiez que le flag `-fsanitize=fuzzer` est reconnu :

```bash
$ echo 'extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t s) { return 0; }' > /tmp/test_fuzz.cc
$ clang++ -fsanitize=fuzzer /tmp/test_fuzz.cc -o /tmp/test_fuzz
$ /tmp/test_fuzz -runs=10
```

Si la compilation et les 10 exécutions de test passent sans erreur, libFuzzer est opérationnel.

> ⚠️ **Attention** — Sur des versions anciennes de Clang (< 6.0), libFuzzer était distribué comme une bibliothèque séparée (`libFuzzer.a`) qu'il fallait linker manuellement. Depuis Clang 6.0, le flag `-fsanitize=fuzzer` suffit — il active à la fois l'instrumentation de couverture et le linkage du runtime libFuzzer. Utilisez toujours une version récente de Clang (11+, idéalement 14+).

---

## Anatomie d'un harness libFuzzer

Le harness est une fonction C avec une signature imposée :

```c
#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Appeler la fonction cible avec les données fournies par libFuzzer
    // ...
    return 0;
}
```

C'est tout. Pas de `main()` — libFuzzer fournit le sien. Pas de lecture de fichier — libFuzzer fournit les données directement en mémoire via le pointeur `data` et la taille `size`. Pas de boucle — libFuzzer appelle cette fonction en boucle avec des inputs mutés à chaque itération.

La valeur de retour doit toujours être `0`. Une valeur non nulle est réservée à des usages avancés (signaler au fuzzer de ne pas ajouter cet input au corpus).

### Règles essentielles pour un bon harness

Un harness libFuzzer doit respecter quelques contraintes pour que le fuzzing soit efficace et fiable :

**Pas d'état global mutable entre les appels.** Chaque appel à `LLVMFuzzerTestOneInput` doit être indépendant des précédents. Si la fonction cible modifie des variables globales ou des structures persistantes, il faut les réinitialiser au début de chaque appel. Sinon, le comportement du programme dépend de l'ordre des inputs, ce qui rend les crashs non reproductibles et fausse la couverture.

**Pas d'appels à `exit()` ou `abort()` dans le code normal.** Si la fonction cible appelle `exit()` sur une entrée invalide, le processus entier s'arrête et le fuzzing est terminé. Il faut soit modifier le code pour retourner un code d'erreur au lieu d'appeler `exit()`, soit isoler la logique de parsing de la logique de terminaison.

**Pas de `fork()`.** Le fuzzing in-process repose sur l'exécution dans un seul processus. Un `fork()` dans le code cible perturberait le mécanisme de couverture.

**Libérer la mémoire allouée.** Puisque le même processus exécute des millions d'itérations, toute fuite mémoire s'accumule et finit par épuiser la RAM. Assurez-vous que chaque `malloc` a son `free` correspondant dans le harness.

---

## Exemple complet : fuzzer une fonction de parsing

Reprenons le `simple_parser.c` de la section 15.2 et écrivons un harness libFuzzer pour sa fonction `parse_input`.

### Le code cible (rappel)

Supposons que la fonction `parse_input` est déclarée dans un header ou directement accessible :

```c
// parse_input.h
#ifndef PARSE_INPUT_H
#define PARSE_INPUT_H

#include <stddef.h>

int parse_input(const char *data, size_t len);

#endif
```

Et son implémentation dans `parse_input.c` :

```c
// parse_input.c
#include "parse_input.h"
#include <stdio.h>
#include <string.h>

int parse_input(const char *data, size_t len) {
    if (len < 4) return -1;

    if (data[0] != 'R' || data[1] != 'E') return -1;

    unsigned char version = data[2];
    if (version == 1) {
        if (len < 8) return -1;
        int value = *(int *)(data + 4);
        if (value > 1000) {
            printf("Mode étendu activé\n");
        }
    } else if (version == 2) {
        if (len < 16) return -1;
        // Logique v2...
        if (data[4] == 0x00 && len > 20) {
            // Bug intentionnel : accès hors limites si data[5] > len
            char c = data[(unsigned char)data[5]];
            (void)c;
        }
    }

    return 0;
}
```

### Le harness

```c
// fuzz_parse_input.c — Harness libFuzzer
#include <stdint.h>
#include <stddef.h>
#include "parse_input.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Appel direct de la fonction cible
    // Le cast (const char *) est sûr ici — même représentation mémoire
    parse_input((const char *)data, size);
    return 0;
}
```

C'est minimaliste — et c'est voulu. Le harness ne fait que transmettre les données de libFuzzer à la fonction cible. Toute la complexité est dans `parse_input`, et c'est exactement ce qu'on veut explorer.

### Compilation

On compile le harness et le code cible ensemble avec Clang, en activant libFuzzer et les sanitizers :

```bash
$ clang -fsanitize=fuzzer,address,undefined -g -O1 \
    -o fuzz_parse_input \
    fuzz_parse_input.c parse_input.c
```

Décomposons les flags :

| Flag | Rôle |  
|------|------|  
| `-fsanitize=fuzzer` | Active libFuzzer (instrumentation de couverture + runtime + `main()`) |  
| `-fsanitize=address` | Active AddressSanitizer (détection des accès mémoire invalides) |  
| `-fsanitize=undefined` | Active UndefinedBehaviorSanitizer (détection des comportements indéfinis) |  
| `-g` | Inclut les symboles de débogage (pour des rapports de crash lisibles) |  
| `-O1` | Niveau d'optimisation modéré (recommandé pour le fuzzing — voir ci-dessous) |

> 💡 **Pourquoi `-O1` et pas `-O0` ?** — ASan fonctionne mieux avec un minimum d'optimisation. Le niveau `-O1` est le compromis recommandé par les mainteneurs de libFuzzer : il permet au sanitizer de fonctionner correctement tout en gardant un code suffisamment lisible pour le débogage. Le niveau `-O0` fonctionne aussi, mais `-O2` ou `-O3` peuvent masquer certains bugs via l'optimisation (variables éliminées, code réordonné).

### Lancement

```bash
$ mkdir corpus_parse
$ echo -ne 'RE\x01\x00AAAA' > corpus_parse/seed1.bin
$ ./fuzz_parse_input corpus_parse/
```

libFuzzer démarre immédiatement et affiche sa progression :

```
INFO: Running with entropic power schedule (0xFF, 100).  
INFO: Seed: 3847291056  
INFO: Loaded 1 modules   (47 inline 8-bit counters): 47 [0x5a3e40, 0x5a3e6f),  
INFO: Loaded 1 PC tables (47 PCs): 47 [0x5a3e70,0x5a4160),  
INFO:        1 files found in corpus_parse/  
INFO: seed corpus: files: 1 min: 8b max: 8b total: 8b  
#2	INITED cov: 7 ft: 7 corp: 1/8b exec/s: 0 rss: 30Mb
#16	NEW    cov: 9 ft: 9 corp: 2/13b lim: 4 exec/s: 0 rss: 30Mb
#128	NEW    cov: 12 ft: 14 corp: 4/38b lim: 4 exec/s: 0 rss: 30Mb
#1024	NEW    cov: 15 ft: 19 corp: 7/89b lim: 11 exec/s: 0 rss: 31Mb
#8192	NEW    cov: 17 ft: 23 corp: 9/142b lim: 80 exec/s: 0 rss: 31Mb
...
```

---

## Lire la sortie de libFuzzer

La sortie de libFuzzer est plus compacte que le tableau de bord d'AFL++, mais tout aussi informative. Chaque ligne préfixée par `#` correspond à un événement :

```
#8192   NEW    cov: 17 ft: 23 corp: 9/142b lim: 80 exec/s: 45230 rss: 31Mb
```

| Champ | Signification |  
|-------|---------------|  
| `#8192` | Numéro de l'exécution (l'input numéro 8192) |  
| `NEW` | Type d'événement : un nouvel input a été ajouté au corpus |  
| `cov: 17` | Nombre de *edges* (transitions entre blocs de base) couverts |  
| `ft: 23` | Nombre de *features* distinctes observées (métrique plus fine que `cov`) |  
| `corp: 9/142b` | Taille du corpus : 9 inputs totalisant 142 octets |  
| `lim: 80` | Limite de taille actuelle des inputs générés (augmente progressivement) |  
| `exec/s: 45230` | Nombre d'exécutions par seconde |  
| `rss: 31Mb` | Consommation mémoire résidente du processus |

Les types d'événements possibles :

| Événement | Signification |  
|-----------|---------------|  
| `INITED` | Initialisation terminée, corpus initial chargé |  
| `NEW` | Nouvel input ajouté au corpus (nouvelle couverture découverte) |  
| `REDUCE` | Un input existant a été remplacé par une version plus courte couvrant les mêmes chemins |  
| `pulse` | Battement de cœur périodique (aucune découverte, le fuzzer est toujours actif) |  
| `DONE` | Nombre d'itérations maximum atteint (si `-runs=N` a été spécifié) |

Pour le RE, les moments clés sont les événements `NEW` : chaque nouveau corpus entry représente un chemin d'exécution que le fuzzer a réussi à atteindre dans la fonction cible. Si les `NEW` s'enchaînent rapidement, le fuzzer explore activement de nouvelles branches. Si seuls des `pulse` apparaissent pendant de longues périodes, le fuzzer a probablement convergé — il est temps d'enrichir le corpus ou le dictionnaire, ou de passer à l'analyse des résultats.

---

## Quand libFuzzer détecte un bug

Quand un sanitizer détecte un problème (ou que le programme crashe), libFuzzer s'arrête et affiche un rapport détaillé. Voici un exemple avec ASan détectant un accès hors limites :

```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000098
    at pc 0x00000051a2f3 bp 0x7ffc12345678 sp 0x7ffc12345670
READ of size 1 at 0x602000000098 thread T0
    #0 0x51a2f2 in parse_input parse_input.c:22:20
    #1 0x51a3b7 in LLVMFuzzerTestOneInput fuzz_parse_input.c:7:5
    #2 0x43d0a1 in fuzzer::Fuzzer::ExecuteCallback(...) FuzzerLoop.cpp:611:15
    ...

0x602000000098 is located 0 bytes after 24-byte region [0x602000000080,0x602000000098)
allocated by thread T0 here:
    ...

SUMMARY: AddressSanitizer: heap-buffer-overflow parse_input.c:22:20 in parse_input

artifact_prefix='./'; Test unit written to ./crash-adc83b19e793491b1c6ea0fd8b46cd9f32e592fc
```

L'information cruciale pour le RE :

- **Type du bug** — `heap-buffer-overflow`, `stack-buffer-overflow`, `use-after-free`, `null-deref`… Chaque type indique un comportement différent de la logique interne.  
- **Localisation précise** — `parse_input.c:22:20` nous dit exactement quelle ligne et quelle colonne sont responsables. En contexte RE sur un binaire sans sources, l'adresse (`pc 0x00000051a2f3`) permet de localiser l'instruction dans Ghidra.  
- **Stack trace** — La chaîne d'appels montre le chemin emprunté pour atteindre le bug.  
- **Fichier crash** — `crash-adc83b19e...` est l'input qui a déclenché le bug. Il est sauvegardé automatiquement pour reproduction.

Pour reproduire le crash :

```bash
$ ./fuzz_parse_input crash-adc83b19e793491b1c6ea0fd8b46cd9f32e592fc
```

Ou pour l'examiner dans un débogueur :

```bash
$ gdb -q ./fuzz_parse_input
(gdb) run crash-adc83b19e793491b1c6ea0fd8b46cd9f32e592fc
```

> 💡 **Interprétation RE** — Ce crash nous apprend que la fonction `parse_input`, lorsqu'elle reçoit un input de version 2 avec `data[4] == 0x00` et une valeur de `data[5]` supérieure à la taille du buffer, effectue un accès mémoire hors limites. En inspectant l'input crash avec `xxd`, on peut reconstituer exactement quels champs du format d'entrée ont déclenché ce chemin — c'est une information directe sur la structure interne du parseur.

---

## Options de ligne de commande utiles

libFuzzer accepte ses options directement en ligne de commande (après le binaire, avant le corpus) :

### Contrôle de la durée et du nombre d'itérations

```bash
# Limiter à 60 secondes
$ ./fuzz_parse_input -max_total_time=60 corpus_parse/

# Limiter à 100 000 itérations
$ ./fuzz_parse_input -runs=100000 corpus_parse/
```

Sans limite, libFuzzer tourne indéfiniment jusqu'à `Ctrl+C` ou un crash.

### Contrôle de la taille des inputs

```bash
# Limiter les inputs à 256 octets maximum
$ ./fuzz_parse_input -max_len=256 corpus_parse/
```

Par défaut, libFuzzer augmente progressivement la taille maximale des inputs. Si vous savez que le format cible a une taille limitée (par exemple un header de 64 octets), fixer `-max_len` accélère significativement la convergence : le fuzzer ne perd pas de temps à générer des inputs de plusieurs kilo-octets qui ne seront jamais traités au-delà des premiers octets.

### Utilisation d'un dictionnaire

Comme AFL++, libFuzzer supporte les dictionnaires de tokens :

```bash
$ ./fuzz_parse_input -dict=my_dict.txt corpus_parse/
```

Le format du dictionnaire est identique à celui d'AFL++ (un token par ligne, cf. section 15.6).

### Fusion et minimisation du corpus

Après une longue session, le corpus peut contenir des inputs redondants. libFuzzer peut le minimiser :

```bash
# Fusion : ne garder que les inputs qui apportent une couverture unique
$ mkdir corpus_minimized
$ ./fuzz_parse_input -merge=1 corpus_minimized/ corpus_parse/
```

Cette commande lit tous les inputs de `corpus_parse/`, identifie ceux qui apportent une couverture unique, et les copie dans `corpus_minimized/`. C'est l'équivalent d'`afl-cmin` pour AFL++.

### Parallélisme

libFuzzer supporte le fuzzing parallèle via des jobs et des workers :

```bash
# Lancer 4 workers en parallèle
$ ./fuzz_parse_input -jobs=4 -workers=4 corpus_parse/
```

Chaque worker est un processus séparé qui partage le même répertoire de corpus. Les découvertes de chaque worker sont automatiquement visibles par les autres grâce au système de fichiers. Ce mécanisme est plus simple que le mode main/secondary d'AFL++, mais tout aussi efficace.

---

## Compilation mixte : harness Clang + code cible GCC

Dans cette formation, les sources sont destinées à être compilées avec GCC. Comment utiliser libFuzzer (qui nécessite Clang) sans tout recompiler ?

La stratégie est de compiler **séparément** :

1. Le code cible (la bibliothèque ou le fichier `.o`) avec GCC, en ajoutant l'instrumentation de couverture Clang-compatible.  
2. Le harness avec Clang et `-fsanitize=fuzzer`.  
3. Linker le tout ensemble.

### Approche 1 : tout compiler avec Clang (la plus simple)

Si les sources compilent sans modification avec Clang (ce qui est le cas pour du C standard et la grande majorité du C++), il suffit de remplacer `gcc` par `clang` :

```bash
$ clang -fsanitize=fuzzer,address -g -O1 \
    -o fuzz_target \
    fuzz_harness.c source1.c source2.c
```

C'est l'approche recommandée quand elle est possible. Les binaires d'entraînement de cette formation compilent tous correctement avec Clang.

### Approche 2 : compiler le code cible en objet avec GCC, linker avec Clang

Si le code cible dépend de fonctionnalités spécifiques à GCC ou si vous voulez minimiser les changements :

```bash
# Étape 1 : compiler le code cible avec GCC en fichier objet
$ gcc -c -g -O1 -fsanitize=address -o parse_input.o parse_input.c

# Étape 2 : compiler le harness et linker avec Clang
$ clang -fsanitize=fuzzer,address -g -O1 \
    -o fuzz_parse_input \
    fuzz_harness.c parse_input.o
```

> ⚠️ **Attention** — Dans cette configuration, le code cible (`parse_input.o`) est compilé avec ASan (grâce à `-fsanitize=address` de GCC) mais **sans l'instrumentation de couverture de libFuzzer**. Le fuzzer pourra toujours détecter les crashs et les bugs mémoire, mais le retour de couverture sera limité au code compilé avec Clang. Pour une couverture complète, préférez l'approche 1.

### Approche 3 : instrumentation de couverture GCC compatible

GCC supporte ses propres flags d'instrumentation de couverture (`-fsanitize-coverage=trace-pc-guard`) depuis GCC 8+. En théorie, cela permet de combiner la couverture GCC avec le runtime libFuzzer. En pratique, cette combinaison est fragile et peu documentée. Sauf besoin spécifique, préférez les approches 1 ou 2.

---

## Harness avancés : techniques courantes en RE

### Gérer les fonctions qui attendent un fichier, pas un buffer

Beaucoup de programmes ne traitent pas directement un buffer mémoire mais lisent un fichier. Le harness doit alors écrire les données dans un fichier temporaire :

```c
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

// Déclaration de la fonction cible qui lit un fichier
int process_file(const char *filename);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Écrire les données dans un fichier temporaire
    char tmpfile[] = "/tmp/fuzz_input_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) return 0;

    write(fd, data, size);
    close(fd);

    // Appeler la fonction cible avec le fichier temporaire
    process_file(tmpfile);

    // Nettoyer
    unlink(tmpfile);

    return 0;
}
```

Cette approche est plus lente que le fuzzing sur buffer direct (à cause des I/O disque), mais elle est parfois inévitable. Pour atténuer l'impact, on peut utiliser un tmpfs monté en RAM :

```bash
$ sudo mount -t tmpfs -o size=100M tmpfs /tmp/fuzz_tmp
```

### Limiter la surface fuzzée avec des gardes

Si la fonction cible est trop large et que vous voulez concentrer le fuzzing sur un sous-ensemble de sa logique, vous pouvez ajouter des conditions dans le harness :

```c
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Ne fuzzer que les inputs de version 2 (pour cibler ce chemin spécifique)
    if (size < 4) return 0;
    if (data[0] != 'R' || data[1] != 'E' || data[2] != 0x02) return 0;

    parse_input((const char *)data, size);
    return 0;
}
```

Ce filtrage pré-harness fait gagner un temps considérable quand on sait déjà, grâce à l'analyse statique dans Ghidra, quelle branche du parseur on veut explorer.

### Initialisation unique avec `LLVMFuzzerInitialize`

Si la fonction cible nécessite une initialisation coûteuse (chargement de tables, allocation de structures), libFuzzer offre un hook d'initialisation appelé une seule fois au démarrage :

```c
int LLVMFuzzerInitialize(int *argc, char ***argv) {
    // Initialisation unique : charger les tables, préparer le contexte
    init_parser_tables();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    parse_input_with_context(global_context, (const char *)data, size);
    return 0;
}
```

Ce hook est optionnel. Si défini, libFuzzer l'appelle avant la première itération de fuzzing. Il reçoit `argc` et `argv` du programme, ce qui permet de passer des options personnalisées au harness.

---

## Choisir les sanitizers selon l'objectif

La combinaison de sanitizers à activer dépend de ce qu'on cherche à découvrir sur le binaire :

| Combinaison | Commande | Ce qu'elle détecte | Usage RE |  
|-------------|----------|---------------------|----------|  
| ASan + UBSan | `-fsanitize=fuzzer,address,undefined` | Overflows, use-after-free, accès nuls, UB arithmétique, shifts invalides | Usage général, premier choix |  
| MSan | `-fsanitize=fuzzer,memory` | Lectures de mémoire non initialisée | Comprendre quels champs sont effectivement lus/utilisés |  
| ASan seul | `-fsanitize=fuzzer,address` | Bugs mémoire uniquement | Quand UBSan génère trop de faux positifs |  
| Sans sanitizer | `-fsanitize=fuzzer` | Crashs uniquement (signaux fatals) | Vitesse maximale, triage initial |

> ⚠️ **Attention** — ASan et MSan sont **mutuellement exclusifs** : on ne peut pas les activer en même temps. Si vous voulez les deux types de détection, lancez deux campagnes séparées avec deux builds différents.

MemorySanitizer (MSan) est particulièrement intéressant en contexte RE : il signale chaque lecture d'un octet non initialisé. Si le parseur lit le 12e octet d'un input de 8 octets, MSan le détecte immédiatement — même si cet accès tombe dans une zone mémoire valide (allouée mais non écrite). Cela révèle les hypothèses implicites du parseur sur la taille minimale des entrées.

---

## Workflow libFuzzer pour le RE : résumé étape par étape

1. **Identifier la fonction cible** dans Ghidra ou par analyse statique — typiquement la fonction de parsing qui reçoit les données d'entrée.

2. **Isoler le code cible** — extraire la fonction et ses dépendances dans des fichiers compilables séparément. Si le code a trop de dépendances, envisager de *stubber* les fonctions non essentielles (les remplacer par des fonctions vides qui retournent des valeurs par défaut).

3. **Écrire le harness** — un `LLVMFuzzerTestOneInput` minimaliste qui appelle la fonction cible.

4. **Compiler avec Clang** — activer `-fsanitize=fuzzer,address,undefined` et `-g -O1`.

5. **Préparer le corpus initial** — quelques inputs de base construits à partir des magic bytes et des constantes identifiées en analyse statique.

6. **Lancer le fuzzing** — observer la progression de `cov` et `ft`. Laisser tourner tant que des `NEW` apparaissent.

7. **Analyser les crashs** — chaque rapport ASan/UBSan est une pièce du puzzle de la logique interne (cf. section 15.4).

8. **Minimiser le corpus** — utiliser `-merge=1` pour ne garder que les inputs essentiels, puis les examiner pour reconstituer la spécification du format d'entrée.

---

## AFL++ ou libFuzzer : lequel choisir ?

Les deux outils ne s'opposent pas — ils se complètent. Voici un guide de décision rapide :

| Critère | AFL++ | libFuzzer |  
|---------|-------|-----------|  
| Sources disponibles | Oui ou non (mode QEMU) | Oui (Clang obligatoire) |  
| Cible | Programme entier (via fichier/stdin) | Fonction spécifique (via harness) |  
| Vitesse | Bonne (centaines à milliers exec/s) | Excellente (dizaines de milliers exec/s) |  
| Effort de mise en place | Minimal (juste recompiler) | Modéré (écrire un harness) |  
| Interface | Tableau de bord riche en temps réel | Sortie texte compacte |  
| Binaires sans sources | Oui (QEMU / Frida) | Non |  
| Idéal pour | Exploration large, première approche | Ciblage précis, parsing profond |

En pratique, dans un workflow RE complet, on commence souvent par AFL++ pour une exploration large du programme (comme notre premier run sur `ch15-keygenme` en section 15.2), puis on passe à libFuzzer sur les fonctions spécifiques identifiées comme intéressantes (comme le parseur `ch15-fileformat` du cas pratique en section 15.7). Les crashs de l'un alimentent le corpus de l'autre.

---


⏭️ [Analyser les crashs pour comprendre la logique de parsing](/15-fuzzing/04-analyser-crashs.md)
