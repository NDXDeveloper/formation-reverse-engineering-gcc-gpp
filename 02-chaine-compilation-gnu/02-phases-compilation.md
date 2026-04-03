🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.2 — Phases de compilation et fichiers intermédiaires (`.i`, `.s`, `.o`)

> 🎯 **Objectif de cette section** : Produire et inspecter concrètement chaque fichier intermédiaire de la chaîne de compilation, apprendre à lire leur contenu, et développer le réflexe d'exploiter ces artefacts quand ils sont disponibles lors d'une analyse RE.

---

## Pourquoi s'intéresser aux fichiers intermédiaires ?

En section 2.1, nous avons décrit le pipeline de compilation de manière conceptuelle. Ici, nous passons à la pratique. Le flag `-save-temps` de GCC conserve tous les fichiers intermédiaires sur disque, ce qui nous permet de « photographier » l'état du code à chaque étape de la transformation.

Pour le reverse engineer, ces fichiers intermédiaires sont précieux dans deux situations :

1. **Pendant l'apprentissage** : observer la sortie de chaque phase vous entraîne à reconnaître les patterns que vous retrouverez plus tard dans un binaire inconnu. Voir comment un `if/else` en C devient des instructions `cmp` + `jz` dans le `.s` vous prépare à faire le chemin inverse.

2. **Pendant une analyse réelle** : si vous avez accès au système de build d'un projet (audit de code, analyse d'un projet open source, incident de sécurité interne), les fichiers `.o` avant édition de liens contiennent souvent plus d'informations que l'exécutable final — symboles locaux, relocations non résolues, sections de débogage spécifiques à chaque unité de compilation.

## Générer tous les intermédiaires d'un coup

Reprenons notre `hello.c` fil conducteur et compilons-le en conservant tous les artefacts :

```bash
gcc -save-temps -O0 -o hello hello.c
```

Après exécution, le répertoire contient :

```
hello.c       ← Source original  
hello.i       ← Sortie du préprocesseur  
hello.s       ← Sortie du compilateur (assembleur textuel)  
hello.o       ← Sortie de l'assembleur (fichier objet ELF)  
hello         ← Exécutable final (après édition de liens)  
```

Vous pouvez aussi produire chaque fichier individuellement, ce qui est parfois plus explicite :

```bash
gcc -E  hello.c -o hello.i      # Préprocesseur seul  
gcc -S  hello.c -o hello.s      # Préprocesseur + compilation  
gcc -c  hello.c -o hello.o      # Préprocesseur + compilation + assemblage  
gcc     hello.c -o hello        # Pipeline complet  
```

> ⚠️ **Attention** : avec `-S` et `-c`, GCC exécute implicitement toutes les phases *précédentes*. Le flag `-S` ne suppose pas que vous fournissez un `.i` — il part du `.c` et s'arrête après la génération du `.s`.

Comparons maintenant les tailles pour avoir une première intuition :

```bash
wc -l hello.i hello.s  
ls -lh hello.o hello  
```

Résultat typique (les valeurs exactes dépendent de votre système et de la version de GCC) :

| Fichier | Taille indicative | Lignes indicatives |  
|---------|-------------------|--------------------|  
| `hello.c` | ~0,4 Ko | ~20 lignes |  
| `hello.i` | ~30–60 Ko | ~800–2000 lignes |  
| `hello.s` | ~2–4 Ko | ~80–150 lignes |  
| `hello.o` | ~2–4 Ko | (binaire) |  
| `hello` | ~16–20 Ko | (binaire) |

Le `.i` est volumineux car il intègre tous les headers. Le `.s` est compact car il ne contient que le code assembleur de *votre* source — les fonctions de la libc ne sont pas encore incluses. Le bond de taille entre `.o` et l'exécutable final s'explique par l'ajout du code CRT, des structures PLT/GOT, des en-têtes ELF complets et des métadonnées de liaison dynamique.

## Le fichier `.i` — Sortie du préprocesseur

### Structure du fichier

Le fichier `.i` est du **C pur**, syntaxiquement valide, prêt à être parsé par le compilateur. Il se compose de trois éléments :

**Les marqueurs de ligne** (*linemarkers*) sont des directives au format `# numéro "fichier" flags` qui permettent au compilateur de rapporter les erreurs et warnings en référençant les bons numéros de ligne dans les fichiers originaux :

```c
# 1 "hello.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 1 "<command-line>" 2
# 1 "hello.c"
# 1 "/usr/include/stdio.h" 1 3 4
# 27 "/usr/include/stdio.h" 3 4
# 1 "/usr/include/x86_64-linux-gnu/bits/libc-header-start.h" 1 3 4
...
```

Les flags numériques après le nom de fichier ont une signification précise : `1` indique le début d'un nouveau fichier (entrée dans un `#include`), `2` indique le retour au fichier incluant (sortie d'un `#include`), `3` signale que le contenu provient d'un header système, et `4` indique que le contenu doit être traité comme enveloppé dans un bloc `extern "C"` implicite.

**Le contenu des headers** occupe la grande majorité du fichier. Vous y retrouverez les déclarations de `printf`, `strcmp`, les typedefs système (`size_t`, `FILE`, etc.), et toute la chaîne transitive des inclusions.

**Votre code**, tout en bas du fichier, avec les macros expansées :

```c
# 6 "hello.c"
int check(const char *input) {
    return strcmp(input, "RE-101") == 0;
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

Notez que `SECRET` a disparu, remplacé par sa valeur `"RE-101"`.

### Ce qu'on peut en tirer (perspective RE)

Examiner un `.i` est rarement utile en RE classique (vous n'avez généralement pas accès aux fichiers intermédiaires d'un binaire cible). Mais c'est un excellent outil d'apprentissage pour comprendre **ce que le compilateur reçoit réellement** :

- Vous réalisez la quantité de code provenant des headers qui ne correspond pas au code de l'auteur.  
- Vous identifiez les prototypes exacts des fonctions de la libc tels que GCC les voit, ce qui aide à comprendre les conventions d'appel.  
- Vous pouvez vérifier comment les macros conditionnelles (`#ifdef DEBUG`, `#ifdef __linux__`, etc.) ont été résolues, ce qui éclaire les différences entre une build debug et une build release.

Pour naviguer rapidement dans un `.i` volumineux, cherchez vos fonctions :

```bash
grep -n "^int check\|^int main" hello.i
```

## Le fichier `.s` — Sortie du compilateur (assembleur textuel)

### Structure du fichier

Le fichier `.s` est du **code assembleur** au format texte, en syntaxe AT&T par défaut sous GCC. Il contient un mélange d'instructions machine et de **directives d'assembleur** (des méta-instructions destinées à `as`, pas au processeur).

Voici le `.s` complet de notre `hello.c` compilé avec `-O0` (simplifié pour la lisibilité, les détails exacts varient selon la version de GCC) :

```asm
        .file   "hello.c"
        .text
        .section        .rodata
.LC0:
        .string "RE-101"
.LC1:
        .string "Usage: %s <mot de passe>\n"
.LC2:
        .string "Acc\303\250s autoris\303\251."
.LC3:
        .string "Acc\303\250s refus\303\251."
        .text
        .globl  check
        .type   check, @function
check:
        pushq   %rbp
        movq    %rsp, %rbp
        subq    $16, %rsp
        movq    %rdi, -8(%rbp)
        movq    -8(%rbp), %rax
        leaq    .LC0(%rip), %rsi
        movq    %rax, %rdi
        call    strcmp@PLT
        testl   %eax, %eax
        sete    %al
        movzbl  %al, %eax
        leave
        ret
        .size   check, .-check
        .globl  main
        .type   main, @function
main:
        pushq   %rbp
        movq    %rsp, %rbp
        subq    $16, %rsp
        movl    %edi, -4(%rbp)
        movq    %rsi, -16(%rbp)
        cmpl    $2, -4(%rbp)
        je      .L4
        movq    -16(%rbp), %rax
        movq    (%rax), %rax
        movq    %rax, %rsi
        leaq    .LC1(%rip), %rdi
        movl    $0, %eax
        call    printf@PLT
        movl    $1, %eax
        jmp     .L5
.L4:
        movq    -16(%rbp), %rax
        addq    $8, %rax
        movq    (%rax), %rax
        movq    %rax, %rdi
        call    check
        testl   %eax, %eax
        je      .L6
        leaq    .LC2(%rip), %rdi
        call    puts@PLT
        jmp     .L7
.L6:
        leaq    .LC3(%rip), %rdi
        call    puts@PLT
.L7:
        movl    $0, %eax
.L5:
        leave
        ret
        .size   main, .-main
```

### Anatomie des éléments

**Les directives d'assembleur** (lignes commençant par un point) ne génèrent pas d'instructions machine. Elles guident l'assembleur `as` dans la construction du fichier objet :

| Directive | Rôle |  
|-----------|------|  
| `.file "hello.c"` | Enregistre le nom du fichier source (métadonnée de débogage) |  
| `.text` | Bascule dans la section `.text` (code exécutable) |  
| `.section .rodata` | Bascule dans la section `.rodata` (données en lecture seule) |  
| `.string "RE-101"` | Place une chaîne terminée par `\0` à la position courante |  
| `.globl check` | Déclare le symbole `check` comme global (visible depuis d'autres `.o`) |  
| `.type check, @function` | Indique que `check` est une fonction (information ELF) |  
| `.size check, .-check` | Enregistre la taille de la fonction (position courante moins début) |

**Les labels** (mots suivis de `:`) sont des ancres symboliques. Les labels de fonctions (`check:`, `main:`) portent des noms lisibles. Les labels générés par le compilateur (`.L4`, `.L5`, `.L6`, `.L7`) sont des **labels locaux** — ils correspondent aux points de branchement de vos `if/else`. Le préfixe `.L` est une convention GCC qui indique un label interne, non exporté dans la table de symboles.

**Les instructions** constituent le code machine que le processeur exécutera. Nous les étudierons en détail au Chapitre 3. Pour l'instant, notez quelques correspondances avec le code C :

| Code C | Assembleur correspondant |  
|--------|-------------------------|  
| `strcmp(input, SECRET)` | `leaq .LC0(%rip), %rsi` puis `call strcmp@PLT` |  
| `== 0` | `testl %eax, %eax` + `sete %al` |  
| `if (argc != 2)` | `cmpl $2, -4(%rbp)` + `je .L4` |  
| `printf(...)` | Chargement des arguments + `call printf@PLT` |  
| `return 0` | `movl $0, %eax` |

> 💡 **Observation** : GCC a remplacé `printf("Accès autorisé.\n")` par `call puts@PLT`. C'est une **optimisation courante** : quand `printf` est appelé avec une chaîne simple sans format (pas de `%`), GCC le remplace silencieusement par `puts`, qui est plus léger. C'est un premier exemple d'idiome compilateur que vous apprendrez à reconnaître (Chapitre 16, section 16.6).

### Obtenir la syntaxe Intel

Par défaut, GCC produit de l'assembleur en syntaxe AT&T (opérandes source avant destination, préfixes `%` et `$`). Si vous préférez la syntaxe Intel (celle utilisée par IDA, celle que beaucoup trouvent plus lisible) :

```bash
gcc -S -masm=intel hello.c -o hello_intel.s
```

La même instruction `movq %rdi, -8(%rbp)` devient alors `mov QWORD PTR [rbp-8], rdi`. Nous approfondirons les deux syntaxes au Chapitre 7, section 7.2.

### Comparer les niveaux d'optimisation dans le `.s`

L'un des usages les plus instructifs du `.s` est la comparaison entre niveaux d'optimisation. Générons les quatre variantes :

```bash
gcc -S -O0 hello.c -o hello_O0.s  
gcc -S -O1 hello.c -o hello_O1.s  
gcc -S -O2 hello.c -o hello_O2.s  
gcc -S -O3 hello.c -o hello_O3.s  
```

Puis comptons les lignes d'instructions (hors directives et lignes vides) :

```bash
for f in hello_O0.s hello_O1.s hello_O2.s hello_O3.s; do
    echo "$f : $(grep -cE '^\s+[a-z]' $f) instructions"
done
```

Résultat typique :

| Fichier | Instructions (approx.) | Observations |  
|---------|------------------------|--------------|  
| `hello_O0.s` | ~35–40 | Code verbeux, correspondance directe avec le C |  
| `hello_O1.s` | ~25–30 | Variables maintenues en registres, moins d'accès mémoire |  
| `hello_O2.s` | ~20–25 | `check()` potentiellement inliné dans `main()` |  
| `hello_O3.s` | ~20–25 | Similaire à `-O2` pour ce petit programme |

En `-O0`, chaque variable locale est stockée sur la pile et rechargée à chaque utilisation — c'est inefficace mais fidèle au source. Dès `-O1`, GCC commence à garder les valeurs dans les registres et à éliminer les aller-retours inutiles avec la pile. En `-O2`, la fonction `check()` peut être **inlinée** : son code est inséré directement dans `main()`, et le label `check:` disparaît. Nous approfondirons ces transformations au Chapitre 16.

Un `diff` entre deux fichiers `.s` est un outil d'apprentissage puissant :

```bash
diff --color hello_O0.s hello_O2.s
```

## Le fichier `.o` — Sortie de l'assembleur (fichier objet)

### Nature du fichier

Le fichier `.o` est le premier artefact **binaire** du pipeline. C'est un fichier au format ELF (Executable and Linkable Format), mais de type `ET_REL` (*relocatable*) — il ne peut pas être exécuté directement. Il contient :

- Le **code machine** encodé à partir des instructions assembleur.  
- Les **données** (chaînes littérales, variables globales initialisées).  
- Une **table de symboles** locale et globale.  
- Des **entrées de relocation** qui marquent les adresses à corriger par le linker.  
- Optionnellement, des **informations de débogage** (si compilé avec `-g`).

Vérifiez la nature du fichier :

```bash
file hello.o
# hello.o: ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
```

Le mot clé `relocatable` confirme qu'il s'agit d'un fichier objet (pas un exécutable, pas une bibliothèque partagée).

### Inspecter les sections

```bash
readelf -S hello.o
```

Sortie simplifiée :

```
Section Headers:
  [Nr] Name              Type             Offset   Size
  [ 0]                   NULL             000000   000000
  [ 1] .text             PROGBITS         000040   000089
  [ 2] .rela.text        RELA             000298   0000c0
  [ 3] .data             PROGBITS         0000c9   000000
  [ 4] .bss              NOBITS           0000c9   000000
  [ 5] .rodata           PROGBITS         0000c9   000050
  [ 6] .comment          PROGBITS         000119   00002c
  [ 7] .note.GNU-stack   PROGBITS         000145   000000
  [ 8] .eh_frame         PROGBITS         000148   000058
  [ 9] .rela.eh_frame    RELA             000358   000030
  [10] .symtab           SYMTAB           0001a0   000108
  [11] .strtab           STRTAB           0002a8   000035
  [12] .shstrtab         STRTAB           000388   000061
```

Points à noter :

- **`.text`** (89 octets) contient le code machine de `check()` et `main()`.  
- **`.rela.text`** contient les **relocations** de `.text` — les emplacements où le linker devra insérer les adresses finales de `strcmp`, `printf`, `puts` et de la chaîne `.LC0`.  
- **`.data`** et **`.bss`** sont vides : notre programme n'a pas de variables globales initialisées (`.data`) ni non-initialisées (`.bss`).  
- **`.rodata`** (80 octets) contient les chaînes littérales : `"RE-101"`, `"Usage: %s <mot de passe>\n"`, `"Accès autorisé."`, `"Accès refusé."`.  
- **`.symtab`** et **`.strtab`** forment la table de symboles et la table de noms associée.  
- **`.eh_frame`** contient les informations de déroulement de pile pour la gestion des exceptions (même en C, cette section est présente pour permettre le *stack unwinding* utilisé par les débogueurs et les outils de profiling).

### Inspecter la table de symboles

```bash
readelf -s hello.o
```

Sortie simplifiée :

```
Symbol table '.symtab' contains 11 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS hello.c
     2: 0000000000000000     0 SECTION LOCAL  DEFAULT    1 .text
     3: 0000000000000000     0 SECTION LOCAL  DEFAULT    5 .rodata
     4: 0000000000000000    41 FUNC    GLOBAL DEFAULT    1 check
     5: 0000000000000029    96 FUNC    GLOBAL DEFAULT    1 main
     6: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND strcmp
     7: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND printf
     8: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND puts
```

Cette table est riche en enseignements :

- **`check`** est défini dans la section 1 (`.text`), à l'offset 0, taille 41 octets, de type `FUNC`, avec un binding `GLOBAL` (visible depuis d'autres fichiers objet).  
- **`main`** est défini à l'offset `0x29` (41 en décimal — juste après `check`), taille 96 octets.  
- **`strcmp`**, **`printf`** et **`puts`** sont marqués `UND` (*undefined*) : ils sont référencés mais pas définis dans ce fichier objet. C'est le linker qui devra les résoudre.

> 💡 **Astuce RE** : Dans un fichier `.o`, tous les symboles locaux sont encore présents — même ceux marqués `static` en C. C'est le linker, lors du stripping, qui les supprime. Si vous avez accès aux `.o` d'un projet, vous avez plus d'informations que dans le binaire final strippé.

### Inspecter les relocations

```bash
readelf -r hello.o
```

Sortie simplifiée :

| Offset | Type | Sym. Name + Addend |  
|---|---|---|  
| `0x12` | `R_X86_64_PC32` | `.rodata - 4` |  
| `0x1a` | `R_X86_64_PLT32` | `strcmp - 4` |  
| `0x40` | `R_X86_64_PC32` | `.rodata + 22` |  
| `0x49` | `R_X86_64_PLT32` | `printf - 4` |  
| `0x60` | `R_X86_64_PLT32` | `puts - 4` |  
| `0x6f` | `R_X86_64_PLT32` | `puts - 4` |

Chaque entrée dit : « à l'offset X dans `.text`, il y a une référence au symbole Y qu'il faudra corriger. » Le type `R_X86_64_PLT32` indique une relocation relative pour un appel via PLT — c'est le mécanisme standard pour les fonctions de bibliothèques dynamiques (détaillé en section 2.9).

### Désassembler le `.o`

Vous pouvez désassembler le fichier objet exactement comme un exécutable :

```bash
objdump -d hello.o
# ou en syntaxe Intel :
objdump -d -M intel hello.o
```

Le résultat est presque identique au contenu du `.s`, mais sous forme décodée depuis le binaire. Vous remarquerez que les adresses des appels à `strcmp`, `printf` et `puts` sont à zéro — elles attendent d'être remplies par le linker :

```
  17:   e8 00 00 00 00          call   1c <check+0x1c>
```

Ces quatre octets `00 00 00 00` seront remplacés lors de l'édition de liens par le déplacement relatif vers le stub PLT correspondant.

## L'exécutable final — Après l'édition de liens

Pour compléter le panorama, examinons brièvement ce qui change dans le binaire final par rapport au `.o` (les détails viendront dans les sections 2.4 à 2.9 et au Chapitre 5) :

```bash
readelf -S hello | head -30
```

Vous constaterez l'apparition de nombreuses sections supplémentaires absentes du `.o` :

| Section | Origine |  
|---------|---------|  
| `.interp` | Chemin du loader dynamique (`/lib64/ld-linux-x86-64.so.2`) |  
| `.plt` et `.plt.got` | Stubs de la Procedure Linkage Table (résolution dynamique) |  
| `.got` et `.got.plt` | Global Offset Table (adresses résolues au runtime) |  
| `.init` et `.fini` | Code d'initialisation et de finalisation (CRT) |  
| `.init_array` et `.fini_array` | Pointeurs vers les constructeurs/destructeurs globaux |  
| `.dynamic` | Table d'informations pour le loader dynamique |  
| `.dynsym` et `.dynstr` | Table de symboles dynamiques et noms associés |

La section `.rela.text` a disparu : les relocations ont été résolues. Les adresses dans le code machine pointent maintenant vers les stubs PLT. Le fichier est passé du type `ET_REL` (relocatable) au type `ET_DYN` (shared object / position-independent executable) ou `ET_EXEC` (exécutable à adresse fixe) :

```bash
readelf -h hello | grep Type
# Type: DYN (Position-Independent Executable file)
```

## Récapitulatif : de la source au binaire en un coup d'œil

| Information | `hello.c` (Source C) | `hello.i` (C prétraité) | `hello.s` (ASM texte) | `hello.o` (ELF relocatable) | `hello` (ELF exécutable) |  
|---|---|---|---|---|---|  
| **Taille indicative** | ~20 lignes | ~800–2000 lignes | ~80–150 lignes | ~2–4 Ko | ~16–20 Ko |  
| Macros | ✅ | ❌ Expansées | ❌ | ❌ | ❌ |  
| Commentaires | ✅ | ❌ | ❌ | ❌ | ❌ |  
| Types C | ✅ | ✅ | ❌ Réduits à des tailles | ❌ | ❌ |  
| Noms de variables | ✅ | ✅ | ❌ Registres / offsets | ❌ | ❌ |  
| Noms de fonctions | ✅ | ✅ | ✅ Labels | ✅ Symboles | ⚠️ Si non strippé |  
| Structures de contrôle | ✅ | ✅ | ✅ Sauts | ✅ Sauts | ✅ Sauts |  
| Relocations | — | — | Références `@PLT` | ✅ Présentes | ❌ Résolues |

Chaque colonne est un instantané de l'information disponible. La lecture de droite à gauche — partir du binaire final et tenter de remonter vers le source — c'est précisément la définition du reverse engineering.

## Commandes essentielles à retenir

| Objectif | Commande |  
|----------|----------|  
| Conserver tous les intermédiaires | `gcc -save-temps hello.c -o hello` |  
| Préprocesseur seul | `gcc -E hello.c -o hello.i` |  
| Compilation seule (→ assembleur) | `gcc -S hello.c -o hello.s` |  
| Assemblage seul (→ objet) | `gcc -c hello.c -o hello.o` |  
| Syntaxe Intel pour le `.s` | `gcc -S -masm=intel hello.c -o hello.s` |  
| Voir les commandes internes de GCC | `gcc -v hello.c -o hello` |  
| Afficher sans exécuter | `gcc -### hello.c -o hello` |  
| Sections d'un `.o` ou d'un ELF | `readelf -S hello.o` |  
| Symboles | `readelf -s hello.o` |  
| Relocations | `readelf -r hello.o` |  
| Désassemblage | `objdump -d hello.o` |

---

> 📖 **Maintenant que nous savons produire et lire les fichiers intermédiaires**, il est temps de comprendre le format du produit final. Dans la section suivante, nous examinerons les trois grands formats de binaires natifs — ELF, PE et Mach-O — et pourquoi cette formation se concentre sur ELF.  
>  
> → [2.3 — Formats binaires : ELF (Linux), PE (Windows via MinGW), Mach-O (macOS)](/02-chaine-compilation-gnu/03-formats-binaires.md)

⏭️ [Formats binaires : ELF (Linux), PE (Windows via MinGW), Mach-O (macOS)](/02-chaine-compilation-gnu/03-formats-binaires.md)
