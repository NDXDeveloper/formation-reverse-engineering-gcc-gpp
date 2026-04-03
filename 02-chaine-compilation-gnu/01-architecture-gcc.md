🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.1 — Architecture de GCC/G++ : préprocesseur → compilateur → assembleur → linker

> 🎯 **Objectif de cette section** : Identifier les quatre grandes phases de la chaîne de compilation GNU, comprendre le rôle de chacune, et savoir à quel moment l'information utile au reverse engineer est produite — ou perdue.

---

## La compilation n'est pas une opération atomique

Quand vous tapez `gcc hello.c -o hello`, vous avez l'impression qu'une seule commande transforme votre code source en exécutable. En réalité, GCC orchestre **quatre programmes distincts**, exécutés séquentiellement, chacun consommant la sortie du précédent :

```
                    Code source (.c / .cpp)
                            │
                            ▼
                 ┌──────────────────────┐
                 │   1. PRÉPROCESSEUR   │    cpp / cc1 -E
                 │      (cpp)           │
                 └──────────┬───────────┘
                            │  Fichier prétraité (.i / .ii)
                            ▼
                 ┌──────────────────────┐
                 │   2. COMPILATEUR     │    cc1 / cc1plus
                 │      (cc1)           │
                 └──────────┬───────────┘
                            │  Fichier assembleur (.s)
                            ▼
                 ┌──────────────────────┐
                 │   3. ASSEMBLEUR      │    as
                 │      (as)            │
                 └──────────┬───────────┘
                            │  Fichier objet (.o)
                            ▼
                 ┌──────────────────────┐
                 │  4. ÉDITEUR DE LIENS │    ld / collect2
                 │     (linker)         │
                 └──────────┬───────────┘
                            │
                            ▼
                     Exécutable (ELF)
```

Le programme `gcc` (ou `g++` pour le C++) n'est donc pas le compilateur à proprement parler : c'est un **driver** — un chef d'orchestre qui invoque les bons outils dans le bon ordre, avec les bons flags. Comprendre cette architecture en pipeline est fondamental pour le RE, car chaque étape transforme l'information de manière irréversible. Plus vous avancez dans le pipeline, plus vous vous éloignez du code source original — et c'est précisément ce chemin que le reverse engineer doit remonter.

## Phase 1 — Le préprocesseur (`cpp`)

### Ce qu'il fait

Le préprocesseur est un outil de **transformation textuelle** qui opère avant toute analyse syntaxique du code. Il traite les directives commençant par `#` :

- **`#include`** : insère le contenu intégral du fichier header désigné à l'emplacement de la directive. Un simple `#include <stdio.h>` peut injecter des milliers de lignes provenant des headers système.  
- **`#define` / `#undef`** : définit ou supprime des macros. Les macros sont ensuite substituées partout où elles apparaissent dans le code.  
- **`#ifdef` / `#ifndef` / `#if` / `#else` / `#endif`** : compilation conditionnelle — certaines portions du code sont incluses ou exclues selon les macros définies.  
- **`#pragma`** : directives spécifiques au compilateur (alignement mémoire, suppression de warnings, etc.).  
- Suppression des **commentaires** (`//` et `/* */`).  
- Insertion de **marqueurs de ligne** (`# 1 "hello.c"`) qui permettent au compilateur de rapporter les erreurs avec les bons numéros de ligne du fichier original.

### En pratique sur notre fil conducteur

Reprenons le `hello.c` du chapitre :

```c
#include <stdio.h>
#include <string.h>

#define SECRET "RE-101"

int check(const char *input) {
    return strcmp(input, SECRET) == 0;
}
```

Lancez le préprocesseur seul avec le flag `-E` :

```bash
gcc -E hello.c -o hello.i
```

Le fichier `hello.i` produit fait typiquement **plusieurs centaines de lignes** — alors que le source n'en comptait qu'une vingtaine. En l'ouvrant, vous constaterez que :

- Tout le contenu de `stdio.h`, `string.h` et de leurs dépendances transitives a été injecté en tête de fichier.  
- La macro `SECRET` a disparu : chaque occurrence a été remplacée textuellement par `"RE-101"`.  
- Les commentaires ont été supprimés.  
- Des marqueurs de type `# 1 "hello.c"` et `# 1 "/usr/include/stdio.h"` jalonnent le fichier.

La ligne de la fonction `check()` dans le `.i` ressemble maintenant à :

```c
int check(const char *input) {
    return strcmp(input, "RE-101") == 0;
}
```

### Pertinence pour le RE

Le préprocesseur a deux conséquences directes pour le reverse engineer :

1. **Les noms de macros disparaissent.** Dans le binaire final, vous ne verrez jamais `SECRET` — uniquement la chaîne littérale `"RE-101"` dans la section `.rodata`. C'est pour cela que l'outil `strings` (Chapitre 5) est si précieux : il permet de retrouver ces valeurs substituées.

2. **Les headers sont absorbés.** Il n'y a plus de trace de quels headers ont été inclus. Vous ne pourrez pas distinguer, dans le binaire, ce qui vient du code de l'auteur et ce qui vient de la bibliothèque standard — sauf en reconnaissant les fonctions de la libc par leurs signatures connues (FLIRT, signatures Ghidra — Chapitre 20).

> 💡 **Astuce RE** : Quand vous trouvez une chaîne intéressante avec `strings` dans un binaire, pensez qu'elle peut provenir d'un `#define` dans le source original. La valeur est là, mais le *nom* de la macro a été effacé par le préprocesseur.

## Phase 2 — Le compilateur proprement dit (`cc1` / `cc1plus`)

### Ce qu'il fait

C'est le cœur du processus. Le compilateur prend le fichier prétraité (`.i` ou `.ii`) et le transforme en **code assembleur** (`.s`). Cette phase est elle-même décomposée en plusieurs étapes internes :

1. **Analyse lexicale (lexing)** : le code source est découpé en *tokens* — mots-clés, identifiants, opérateurs, littéraux.  
2. **Analyse syntaxique (parsing)** : les tokens sont organisés en un *arbre syntaxique abstrait* (AST) selon la grammaire du langage.  
3. **Analyse sémantique** : vérification des types, résolution des noms, détection d'erreurs sémantiques.  
4. **Génération de la représentation intermédiaire** : GCC utilise plusieurs niveaux de représentation interne (GENERIC → GIMPLE → RTL). C'est sur ces représentations que s'appliquent les passes d'optimisation.  
5. **Optimisation** : selon le niveau demandé (`-O0` à `-O3`), des dizaines de passes transforment le code — élimination de code mort, propagation de constantes, inlining de fonctions, déroulage de boucles, vectorisation… Nous approfondirons cet aspect en section 2.5 et au Chapitre 16.  
6. **Génération de code assembleur** : la représentation interne optimisée est traduite en instructions assembleur pour l'architecture cible (x86-64 dans notre cas).

### En pratique

Pour arrêter la compilation juste après cette phase et obtenir le fichier assembleur :

```bash
gcc -S hello.c -o hello.s
```

Le fichier `hello.s` contient du code assembleur lisible (syntaxe AT&T par défaut sous GCC). Pour notre fonction `check()` compilée sans optimisation (`-O0`), vous obtiendrez quelque chose comme :

```asm
check:
        pushq   %rbp
        movq    %rsp, %rbp
        subq    $16, %rsp
        movq    %rdi, -8(%rbp)
        movq    -8(%rbp), %rax
        leaq    .LC0(%rip), %rdx
        movq    %rdx, %rsi
        movq    %rax, %rdi
        call    strcmp@PLT
        testl   %eax, %eax
        sete    %al
        movzbl  %al, %eax
        leave
        ret
```

Avec `-O2`, le même code pourrait être considérablement transformé, voire inliné directement dans `main()`.

### Pertinence pour le RE

Cette phase est **la plus destructrice d'information** du point de vue du reverse engineer :

- **Les noms de variables locales disparaissent** (sauf si `-g` est utilisé pour générer des informations DWARF — section 2.6).  
- **Les types sont réduits à des tailles et des alignements.** Un `int` devient simplement « 4 octets dans un registre 32 bits » ; un `struct` devient un bloc de mémoire à un certain offset.  
- **Les structures de contrôle sont aplaties** en séquences d'instructions et de sauts. Un `if/else` devient un `cmp` suivi d'un `jz` ou `jnz`. Une boucle `for` devient un label, un corps, un incrément et un `jmp` conditionnel.  
- **Les optimisations réordonnent, fusionnent et suppriment du code.** Une fonction peut être inlinée (intégrée dans l'appelant), une variable peut être maintenue uniquement dans un registre sans jamais toucher la mémoire, une branche `else` peut être supprimée si le compilateur prouve qu'elle est inaccessible.

C'est pour toutes ces raisons qu'analyser un binaire compilé avec `-O0` (sans optimisation) est radicalement plus simple qu'analyser le même programme compilé avec `-O2` ou `-O3`. En `-O0`, la correspondance entre le code source et l'assembleur est quasi directe. En `-O2`, le compilateur a réécrit la logique de manière parfois méconnaissable.

> 💡 **Astuce RE** : Quand vous avez accès au code source ou que vous suspectez l'utilisation d'une bibliothèque open source, recompilez cette bibliothèque vous-même à différents niveaux d'optimisation et comparez le résultat avec le binaire cible. Cela vous permet de calibrer le niveau d'optimisation utilisé et de reconnaître les patterns du compilateur.

## Phase 3 — L'assembleur (`as`)

### Ce qu'il fait

L'assembleur (GNU `as`, aussi appelé GAS — GNU Assembler) traduit le fichier assembleur textuel (`.s`) en un **fichier objet** (`.o`). Ce fichier objet est déjà au format binaire ELF, mais il est **incomplet** :

- Il contient le code machine correspondant aux instructions assembleur.  
- Il contient les données définies dans le source (chaînes littérales, variables globales initialisées…).  
- Il contient une **table de symboles** qui liste les fonctions et variables définies ou référencées.  
- Il contient des **entrées de relocation** : des emplacements dans le code machine où une adresse doit être remplie plus tard par le linker, parce qu'elle n'est pas encore connue (par exemple l'adresse de `strcmp`).

En revanche, les adresses finales ne sont pas fixées. Le fichier `.o` est un fragment — il ne peut pas être exécuté seul.

### En pratique

Pour produire uniquement le fichier objet sans lancer le linker :

```bash
gcc -c hello.c -o hello.o
```

Vous pouvez inspecter ce fichier objet avec `readelf` et `objdump` (outils que nous verrons en détail au Chapitre 5) :

```bash
# Voir les sections du fichier objet
readelf -S hello.o

# Voir la table de symboles
readelf -s hello.o

# Voir les entrées de relocation
readelf -r hello.o

# Désassembler la section .text
objdump -d hello.o
```

Dans la table de symboles, vous verrez `check` et `main` marqués comme définis (`DEF`), tandis que `strcmp`, `printf` et `puts` apparaîtront comme non définis (`UND` — *undefined*). Dans les relocations, chaque `call strcmp@PLT` génère une entrée qui dit : « à cet offset dans le code, il faudra insérer l'adresse de `strcmp` une fois qu'on la connaîtra. »

### Pertinence pour le RE

Du point de vue du reverse engineer, la transformation effectuée par l'assembleur est essentiellement **un encodage** — passage d'une représentation textuelle à une représentation binaire des mêmes instructions. Contrairement à la phase 2, cette étape ne perd presque aucune information structurelle. C'est d'ailleurs pour cela que le **désassemblage** (l'opération inverse) fonctionne si bien : il s'agit de décoder les opcodes binaires pour retrouver les mnémoniques assembleur.

Le fichier `.o` est intéressant pour le RE à un autre titre : il contient les **symboles locaux** et les **informations de relocation** qui sont parfois supprimées ou résolues dans l'exécutable final. Si vous avez accès aux fichiers `.o` d'un projet (avant l'édition de liens), vous disposez d'informations plus riches que dans le binaire final.

## Phase 4 — L'éditeur de liens (linker : `ld` / `collect2`)

### Ce qu'il fait

L'éditeur de liens (linker) est la phase finale. Il prend un ou plusieurs fichiers objets (`.o`) ainsi que des bibliothèques (`.a` pour les bibliothèques statiques, `.so` pour les bibliothèques dynamiques) et les assemble en un **unique fichier exécutable** ou en une bibliothèque partagée. Ses responsabilités sont les suivantes :

**Résolution des symboles.** Chaque symbole marqué comme non défini dans un `.o` doit être trouvé dans un autre `.o` ou dans une bibliothèque. Si `hello.o` référence `strcmp` sans le définir, le linker doit trouver cette définition — ici dans la libc (`libc.so`). Si un symbole reste non résolu, le linker produit une erreur `undefined reference`.

**Résolution des relocations.** Une fois l'adresse de chaque symbole connue (ou au moins son mécanisme de résolution dynamique via PLT/GOT), le linker remplit les emplacements laissés vides par l'assembleur. C'est le « raccordement » final.

**Fusion des sections.** Chaque fichier `.o` possède ses propres sections `.text`, `.data`, `.rodata`, etc. Le linker fusionne toutes les sections `.text` en une seule, toutes les sections `.data` en une seule, et ainsi de suite. Il organise ces sections en **segments** qui seront chargés en mémoire par le loader (section 2.7).

**Ajout du code d'amorçage (CRT — C Runtime).** Le linker ajoute automatiquement le code de démarrage (`crt0.o`, `crti.o`, `crtn.o`, `crtbegin.o`, `crtend.o`) fourni par la toolchain. Ce code est responsable de l'initialisation de l'environnement d'exécution C : mise en place de la pile, appel des constructeurs globaux (C++), passage de `argc` et `argv` à `main()`, et traitement de la valeur de retour de `main()` pour appeler `exit()`. Concrètement, le véritable point d'entrée d'un binaire ELF n'est pas `main()` mais `_start`, qui appelle `__libc_start_main`, qui finit par appeler votre `main()`.

**Création des structures PLT/GOT.** Pour les bibliothèques dynamiques, le linker génère les sections `.plt` (Procedure Linkage Table) et `.got` (Global Offset Table) qui permettront la résolution des symboles au moment de l'exécution. Nous détaillerons ce mécanisme en section 2.9.

### En pratique

Quand vous lancez `gcc hello.c -o hello`, GCC invoque le linker de manière transparente. Pour observer ce qui se passe, ajoutez le flag `-v` (verbose) :

```bash
gcc -v hello.c -o hello
```

La sortie révèle l'appel à `collect2` (le wrapper GCC autour de `ld`) avec une longue liste d'arguments : les fichiers CRT, la libc, le chemin des bibliothèques, les options de relocation, etc.

Vous pouvez aussi invoquer le linker manuellement, mais la liste des fichiers et options nécessaires est longue — c'est l'une des raisons d'être du driver `gcc`.

Pour examiner le résultat :

```bash
# Point d'entrée et type de l'exécutable
readelf -h hello

# Segments (vue du loader)
readelf -l hello

# Sections complètes
readelf -S hello

# Symboles dynamiques (ceux liés aux .so)
readelf --dyn-syms hello

# Vérifier les bibliothèques dynamiques requises
ldd hello
```

### Liaison statique vs liaison dynamique

Le linker peut fonctionner selon deux modes, et cette distinction a un impact majeur sur le RE :

**Liaison dynamique** (par défaut sous Linux) : les fonctions de la libc et des autres bibliothèques partagées ne sont **pas** copiées dans l'exécutable. Le binaire contient uniquement des références (via PLT/GOT) qui seront résolues au chargement par le loader `ld.so`. L'exécutable est compact, mais dépend des `.so` présentes sur le système.

```bash
gcc hello.c -o hello          # Liaison dynamique (défaut)  
ldd hello                     # Affiche les .so requises  
```

**Liaison statique** : tout le code des bibliothèques nécessaire est **copié** directement dans l'exécutable. Le binaire est autonome mais beaucoup plus volumineux.

```bash
gcc -static hello.c -o hello_static  
ldd hello_static              # "not a dynamic executable"  
```

Pour le reverse engineer, un binaire statiquement lié est paradoxalement plus difficile à analyser : le code de la libc est mélangé avec le code de l'application, et sans signatures (comme FLIRT pour IDA ou les signatures Ghidra — Chapitre 20), il est ardu de distinguer les fonctions standard des fonctions de l'auteur. À l'inverse, un binaire dynamiquement lié offre un repère précieux : chaque appel via PLT est un appel à une fonction de bibliothèque identifiable par son nom.

### Pertinence pour le RE

L'édition de liens est le dernier moment où vous pouvez perdre de l'information :

- **Le stripping** (`gcc -s` ou `strip` après compilation) supprime la table de symboles non dynamiques. Les noms de vos fonctions internes (`check`, `validate_input`, etc.) disparaissent du binaire. Seuls les symboles dynamiques (ceux des `.so`) sont préservés car ils sont nécessaires au runtime.  
- **Les fichiers objets intermédiaires sont fusionnés.** Vous ne pouvez plus distinguer ce qui vient de quel `.o` — tout est fondu dans les sections du binaire final.  
- **Le code CRT ajoute de la complexité.** Quand vous ouvrez un binaire dans Ghidra ou IDA, le point d'entrée `_start` et le code d'initialisation de la libc peuvent être déroutants. Savoir que `main()` est appelé *depuis* `__libc_start_main` vous permet de localiser rapidement la logique applicative.

## Vue d'ensemble : ce qui est préservé, ce qui est perdu

Le tableau ci-dessous résume, pour chaque phase, les informations qui survivent et celles qui sont irrémédiablement perdues (en compilation standard sans `-g`) :

| Information | Après CPP | Après CC1 | Après AS | Après LD |  
|---|---|---|---|---|  
| Noms de macros (`#define`) | ❌ Perdus | — | — | — |  
| Commentaires | ❌ Perdus | — | — | — |  
| Noms de variables locales | ✅ | ❌ Perdus | — | — |  
| Types précis (struct, enum…) | ✅ | ❌ Réduits à des tailles | — | — |  
| Structure de contrôle (if, for…) | ✅ | ❌ Aplatis en sauts | — | — |  
| Noms de fonctions internes | ✅ | ✅ | ✅ Labels | ⚠️ Perdus si strippé |  
| Code machine | — | — | ✅ | ✅ |  
| Relocations non résolues | — | — | ✅ | ❌ Résolues |  
| Noms de fonctions dynamiques | — | — | — | ✅ Toujours présents |

> 💡 **Point clé** : L'essentiel de ce que le reverse engineer doit reconstruire — noms, types, structures de contrôle — est perdu lors de la phase 2 (compilation). C'est pourquoi le décompilateur (Ghidra, IDA, RetDec — Chapitre 20) travaille si dur : il tente de **réinventer** ce que le compilateur a détruit.

## Comment GCC orchestre le tout

Pour bien fixer les idées, voici les commandes internes que GCC exécute quand vous tapez `gcc hello.c -o hello` (simplifié) :

```bash
# Phase 1 — Préprocesseur
cc1 -E hello.c -o /tmp/hello.i

# Phase 2 — Compilation (inclut implicitement le préprocesseur)
cc1 /tmp/hello.i -o /tmp/hello.s

# Phase 3 — Assemblage
as /tmp/hello.s -o /tmp/hello.o

# Phase 4 — Édition de liens
collect2 (ld) /usr/lib/crt1.o /usr/lib/crti.o /tmp/hello.o \
    -lc /usr/lib/crtn.o -o hello
```

En pratique, GCC combine souvent les phases 1 et 2 en un seul appel à `cc1`, et passe directement du source au `.s` ou au `.o`. Mais le modèle conceptuel en quatre étapes reste le bon schéma mental.

Vous pouvez observer l'intégralité des commandes exécutées avec :

```bash
gcc -v hello.c -o hello        # Verbose : affiche chaque commande  
gcc -### hello.c -o hello      # Affiche les commandes sans les exécuter  
```

Le flag `-save-temps` est particulièrement utile pour l'apprentissage : il conserve tous les fichiers intermédiaires dans le répertoire courant.

```bash
gcc -save-temps hello.c -o hello  
ls hello.*  
# hello.c  hello.i  hello.s  hello.o  hello
```

Vous obtenez ainsi les fichiers `.i`, `.s` et `.o` en plus de l'exécutable final — un instantané de chaque étape du pipeline. Nous exploiterons cette possibilité en section 2.2.

## Cas du C++ (`g++`)

Lorsque vous compilez du C++ avec `g++`, le pipeline est identique mais quelques points diffèrent :

- Le préprocesseur reconnaît les directives C++ et le fichier prétraité porte l'extension `.ii`.  
- Le compilateur est `cc1plus` au lieu de `cc1`. Il gère la syntaxe C++ : classes, templates, exceptions, surcharge d'opérateurs, etc.  
- Le **name mangling** entre en jeu : les noms de fonctions C++ sont encodés pour inclure les types de leurs paramètres (surcharge), l'espace de noms et la classe d'appartenance. Par exemple, une méthode `MyClass::check(std::string)` sera encodée en un symbole comme `_ZN7MyClass5checkENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE`. Le démanglement est couvert en détail au Chapitre 7 (section 7.6, `c++filt`) et au Chapitre 17 (section 17.1, règles Itanium ABI).  
- Le linker doit résoudre des symboles supplémentaires liés au runtime C++ : `libstdc++.so`, les fonctions de gestion des exceptions (`__cxa_throw`, `__cxa_begin_catch`…), le support RTTI, etc.

---

> 📖 **Dans la section suivante**, nous allons mettre les mains dans le cambouis : en utilisant `-save-temps`, nous produirons et inspecterons chaque fichier intermédiaire pour observer concrètement les transformations décrites ici.  
>  
> → 2.2 — Phases de compilation et fichiers intermédiaires (`.i`, `.s`, `.o`)

⏭️ [Phases de compilation et fichiers intermédiaires (`.i`, `.s`, `.o`)](/02-chaine-compilation-gnu/02-phases-compilation.md)
