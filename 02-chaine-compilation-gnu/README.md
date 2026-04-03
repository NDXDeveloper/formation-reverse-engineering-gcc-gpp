🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 2 — La chaîne de compilation GNU

> 🎯 **Objectif du chapitre** : Comprendre le parcours complet d'un fichier source C/C++ jusqu'au binaire exécutable ELF chargé en mémoire, et maîtriser l'impact de chaque étape sur ce que vous observerez en reverse engineering.

---

## Pourquoi ce chapitre est essentiel

Faire du reverse engineering sur un binaire compilé avec GCC ou G++, c'est remonter un fleuve à contre-courant. Le compilateur a transformé votre code source lisible en une suite d'octets optimisée pour la machine — pas pour l'humain. Comprendre *comment* cette transformation s'opère, étape par étape, vous donne un avantage décisif : vous saurez **ce que le compilateur a fait et pourquoi**, au lieu de subir un désassemblage opaque.

Ce chapitre pose les fondations sur lesquelles reposent toutes les parties suivantes de cette formation. Quand vous chercherez une fonction dans Ghidra, que vous poserez un breakpoint dans GDB sur un appel à `printf`, ou que vous tenterez de comprendre pourquoi une boucle a disparu du binaire `-O2`, vous reviendrez mentalement ici.

Concrètement, à la fin de ce chapitre vous serez capable de :

- Décrire les quatre grandes phases de compilation (préprocesseur, compilation, assemblage, édition de liens) et identifier les fichiers intermédiaires produits à chaque étape.  
- Distinguer les principaux formats binaires (ELF, PE, Mach-O) et expliquer pourquoi cette formation se concentre sur ELF.  
- Nommer les sections clés d'un binaire ELF (`.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`…) et savoir quel type d'information chacune contient.  
- Prédire l'effet des flags de compilation courants (`-O0` à `-O3`, `-g`, `-s`, `-fPIC`, `-pie`) sur la facilité ou la difficulté d'une analyse RE.  
- Expliquer le rôle des informations de débogage DWARF et savoir les exploiter quand elles sont présentes.  
- Décrire le chargement d'un ELF en mémoire par le loader Linux (`ld.so`), le mappage des segments et le mécanisme d'ASLR.  
- Comprendre la résolution dynamique des symboles via PLT/GOT et le principe du lazy binding.

## Prérequis

Avant d'aborder ce chapitre, assurez-vous d'être à l'aise avec :

- Les notions vues au **Chapitre 1** (distinction analyse statique / dynamique, vocabulaire de base du RE).  
- Les **bases du langage C** : compilation d'un programme simple avec `gcc`, notion de fichier source, fichier objet et exécutable.  
- L'utilisation élémentaire d'un **terminal Linux** : naviguer dans l'arborescence, lancer des commandes, lire une sortie texte.

Aucune connaissance préalable en assembleur n'est requise — c'est l'objet du Chapitre 3.

## Plan du chapitre

| Section | Titre | Thème central |  
|---------|-------|---------------|  
| 2.1 | Architecture de GCC/G++ | Les 4 phases : préprocesseur → compilateur → assembleur → linker |  
| 2.2 | Phases de compilation et fichiers intermédiaires | Fichiers `.i`, `.s`, `.o` — observer chaque étape |  
| 2.3 | Formats binaires | ELF (Linux), PE (Windows), Mach-O (macOS) |  
| 2.4 | Sections ELF clés | `.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`, `.init`, `.fini` |  
| 2.5 | Flags de compilation et impact sur le RE | `-O0` à `-O3`, `-g`, `-s`, `-fPIC`, `-pie` |  
| 2.6 | Fichiers de symboles DWARF | Informations de débogage et leur exploitation |  
| 2.7 | Le Loader Linux (`ld.so`) | Du fichier ELF au processus en mémoire |  
| 2.8 | Segments, ASLR et adresses virtuelles | Pourquoi les adresses bougent d'une exécution à l'autre |  
| 2.9 | Résolution dynamique : PLT/GOT | Lazy binding et appels aux bibliothèques partagées |

## Fil conducteur

Tout au long de ce chapitre, nous utiliserons un même programme `hello.c` minimaliste comme fil rouge. En le compilant de différentes manières et en observant le résultat à chaque étape, vous verrez concrètement la théorie prendre forme. Ce fichier se trouve dans le dépôt sous `binaries/ch02-hello/`.

```c
/* hello.c — fil conducteur du Chapitre 2 */
#include <stdio.h>
#include <string.h>

#define SECRET "RE-101"

int check(const char *input) {
    return strcmp(input, SECRET) == 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <mot de passe>\n", argv[0]);
        return 1;
    }
    if (check(argv[1])) {
        printf("Accès autorisé.\n");
    } else {
        printf("Accès refusé.\n");
    }
    return 0;
}
```

Ce programme est volontairement simple, mais suffisamment riche pour illustrer chaque concept : le préprocesseur remplacera la macro `SECRET`, le compilateur transformera `check()` en instructions machine, le linker résoudra `strcmp` et `printf` depuis la libc, et le loader mettra tout cela en mémoire au moment de l'exécution. En section 2.5, nous le compilerons avec différents flags pour observer directement leur impact.

## Positionnement dans la formation

```
Chapitre 1 (Introduction au RE)
        │
        ▼
  ┌─────────────┐
  │ CHAPITRE 2  │ ◄── Vous êtes ici
  │  Chaîne de  │
  │ compilation │
  │     GNU     │
  └─────┬───────┘
        │
        ▼
Chapitre 3 (Assembleur x86-64)
        │
        ▼
Chapitre 4 (Environnement de travail)
        │
        ▼
Partie II — Analyse statique
```

Les concepts vus ici seront mobilisés en permanence dans la suite :

- **Chapitre 3** s'appuiera sur votre compréhension des fichiers `.s` et des conventions d'appel pour aborder l'assembleur x86-64.  
- **Chapitre 5** utilisera `readelf` et `objdump` pour inspecter les sections et headers ELF que vous aurez appris à identifier ici.  
- **Chapitre 7** comparera le désassemblage à différents niveaux d'optimisation — vous saurez déjà ce que `-O0` et `-O2` changent en coulisses.  
- **Chapitre 8** (Ghidra) et **Chapitre 11** (GDB) supposeront que vous comprenez le mécanisme PLT/GOT et le rôle du loader.  
- **Chapitre 19** (anti-reversing) approfondira les protections (PIE, ASLR, RELRO) dont les bases sont posées ici.

---

> 📖 **Prêt ?** Commençons par ouvrir le capot de GCC et observer les quatre grandes phases qui transforment du code C en un exécutable.  
>  
> → 2.1 — Architecture de GCC/G++ : préprocesseur → compilateur → assembleur → linker

⏭️ [Architecture de GCC/G++ : préprocesseur → compilateur → assembleur → linker](/02-chaine-compilation-gnu/01-architecture-gcc.md)
