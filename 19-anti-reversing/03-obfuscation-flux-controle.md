🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.3 — Obfuscation de flux de contrôle (Control Flow Flattening, bogus control flow)

> 🎯 **Objectif** : Comprendre les deux principales techniques d'obfuscation du flux de contrôle — le Control Flow Flattening (CFF) et le Bogus Control Flow (BCF) — savoir les reconnaître dans un désassemblage et connaître les approches pour restaurer une structure lisible.

---

## L'obfuscation de flux de contrôle : le problème

Le stripping retire les noms. Le packing cache le code sur disque. L'obfuscation de flux de contrôle s'attaque à quelque chose de plus fondamental : elle détruit la **structure logique** du programme telle qu'elle apparaît dans le désassemblage, tout en préservant son comportement fonctionnel.

Un programme C bien écrit se traduit en assembleur par un graphe de contrôle (CFG — Control Flow Graph) relativement lisible : des blocs basiques reliés par des branchements conditionnels et inconditionnels qui reflètent les `if`, `else`, `for`, `while` et `switch` du code source. Un analyste expérimenté peut reconstruire mentalement la logique haut niveau en lisant ce graphe.

L'obfuscation de flux de contrôle vise à rendre cette reconstruction impossible — ou du moins si laborieuse que l'analyste y passe des heures au lieu de minutes. Le code produit le même résultat, mais la route qu'il emprunte pour y arriver est devenue un labyrinthe.

Deux techniques dominent ce domaine : le **Control Flow Flattening** et le **Bogus Control Flow**. Elles sont souvent combinées.

## Control Flow Flattening (CFF)

### Le principe

Le Control Flow Flattening — aplatissement du flux de contrôle — est la technique la plus emblématique et la plus efficace. Son principe est simple à énoncer :

**Tous les blocs basiques d'une fonction sont extraits de leur hiérarchie naturelle et placés au même niveau, à l'intérieur d'une boucle `while(true)` pilotée par une variable de dispatch (un `switch`).**

Pour illustrer, prenons une fonction C triviale :

```c
int check(int x) {
    int result = 0;
    if (x > 10) {
        result = x * 2;
    } else {
        result = x + 5;
    }
    return result;
}
```

Le compilateur normal produit un CFG linéaire et clair :

```
    [entrée]
        |
    [cmp x, 10]
      /       \
   [x > 10]  [x <= 10]
     |            |
 [result=x*2] [result=x+5]
      \       /
     [return result]
```

Après Control Flow Flattening, la structure devient :

```c
int check_flattened(int x) {
    int result = 0;
    int state = 0;  /* variable de dispatch */

    while (1) {
        switch (state) {
            case 0:  /* bloc d'entrée */
                if (x > 10)
                    state = 1;
                else
                    state = 2;
                break;
            case 1:  /* branche then */
                result = x * 2;
                state = 3;
                break;
            case 2:  /* branche else */
                result = x + 5;
                state = 3;
                break;
            case 3:  /* sortie */
                return result;
        }
    }
}
```

Le CFG de cette version aplatie ressemble à ceci :

```
         ┌──────────────────────────┐
         │    dispatcher (switch)   │◄─────┐
         └──────────────────────────┘      │
          /      |       |       \         │
     [case 0] [case 1] [case 2]  [case 3]  │
         \       |       |       /         │
          └──────┴───────┴──────┘──────────┘
                                   │
                              (case 3: return)
```

Tous les blocs sont désormais des « frères » au même niveau hiérarchique, tous reliés au dispatcher central. La relation causale entre les blocs — le fait que le case 1 ne peut être atteint que si `x > 10` — est masquée derrière la variable `state`. Pour un analyste qui lit le désassemblage, chaque case semble indépendant. Reconstruire l'ordre logique exige de tracer manuellement toutes les transitions de la variable de dispatch.

### Ce que ça donne en assembleur

En assembleur x86-64, le dispatcher se manifeste par un pattern reconnaissable. On observe typiquement :

- Une variable sur la pile ou dans un registre qui sert de compteur d'état  
- Un bloc de comparaisons en cascade (`cmp` + `je`/`jne`) ou une table de sauts (`jmp [rax*8 + table]`) qui implémente le switch  
- Des sauts inconditionnels (`jmp`) à la fin de chaque bloc qui reviennent systématiquement au dispatcher  
- L'absence de sauts directs entre les blocs de logique métier — tout transite par le dispatcher

Dans Ghidra, le Function Graph d'une fonction aplatie présente une forme caractéristique en « étoile » ou en « araignée » : un nœud central (le dispatcher) avec de nombreuses arêtes sortantes vers les blocs de cas, et autant d'arêtes retour de chaque bloc vers le dispatcher.

### Impact sur l'analyse

Le CFF est redoutable contre les décompilateurs. Ghidra, IDA et les autres outils tentent de reconstruire des structures haut niveau (`if`, `while`, `for`) à partir du CFG. Face à une fonction aplatie, le décompilateur produit un `while(true)` géant contenant un `switch` massif — ce qui est techniquement correct mais inutilisable pour comprendre la logique.

Sur une fonction de 50 lignes de C, le CFF peut produire un switch de 20 à 30 cases. Sur une fonction complexe de plusieurs centaines de lignes, le résultat devient un monstre de centaines de cases où l'analyste se noie.

Le CFF résiste également bien à l'exécution symbolique (chapitre 18) : la variable de dispatch introduit des dépendances de données complexes qui augmentent le nombre de chemins à explorer.

## Bogus Control Flow (BCF)

### Le principe

Le Bogus Control Flow — flux de contrôle factice — adopte une approche complémentaire au CFF. Au lieu de réorganiser les blocs existants, il en **ajoute de faux**.

Le principe : insérer des branchements conditionnels dont la condition est **toujours vraie** (ou toujours fausse) mais dont l'évaluation est suffisamment complexe pour qu'un analyste statique — humain ou logiciel — ne puisse pas le déterminer facilement. Le code du « mauvais » côté du branchement est du code mort (jamais exécuté), mais il ressemble à du code légitime.

Reprenons notre fonction :

```c
int check(int x) {
    int result = 0;
    if (x > 10) {
        result = x * 2;
    } else {
        result = x + 5;
    }
    return result;
}
```

Après insertion de BCF :

```c
int check_bogus(int x) {
    int result = 0;

    /* Prédicat opaque : (y*y) >= 0 est TOUJOURS vrai
     * pour tout entier y, mais le compilateur et le
     * désassembleur ne le "voient" pas facilement. */
    volatile int y = x | 1;
    if ((y * y) >= 0) {
        /* Vrai chemin — toujours exécuté */
        if (x > 10) {
            result = x * 2;
        } else {
            result = x + 5;
        }
    } else {
        /* Faux chemin — JAMAIS exécuté
         * mais contient du code crédible */
        result = x ^ 0xDEAD;
        result = result << 3;
        if (result > 100)
            result = result - 42;
    }

    return result;
}
```

### Prédicats opaques

Le cœur du BCF repose sur les **prédicats opaques** (opaque predicates) : des expressions booléennes dont la valeur est constante (toujours vraie ou toujours fausse) mais dont la preuve de constance est mathématiquement difficile.

Les prédicats opaques classiques exploitent des propriétés de théorie des nombres :

- `(x * (x + 1)) % 2 == 0` — Le produit de deux entiers consécutifs est toujours pair. Toujours vrai.  
- `(x² ≥ 0)` — Le carré d'un entier est toujours positif ou nul. Toujours vrai (en ignorant l'overflow signé, que le compilateur ne peut pas supposer).  
- `(x³ - x) % 3 == 0` — Par le petit théorème de Fermat. Toujours vrai.  
- `(2 * x + 1) % 2 == 1` — Un nombre impair reste impair. Toujours vrai.

Un analyste humain peut reconnaître ces patterns avec de l'expérience. Mais un outil d'analyse statique automatique doit prouver formellement que l'expression est constante, ce qui est en général un problème indécidable dans le cas général (réductible au problème de l'arrêt pour des prédicats suffisamment complexes).

Les implémentations plus sophistiquées utilisent des variables globales modifiées dans d'autres fonctions pour rendre le prédicat encore plus opaque à l'analyse inter-procédurale.

### Impact sur l'analyse

Le BCF double ou triple le nombre de blocs basiques dans le CFG. L'analyste voit des branchements qu'il doit évaluer un par un : ce `if` est-il réel ou factice ? Le décompilateur, incapable de résoudre le prédicat opaque, affiche les deux chemins comme s'ils étaient tous les deux possibles.

Combiné avec le CFF, le résultat est dévastateur : une fonction aplatie dont la moitié des cases du switch contiennent du code mort, et où les transitions entre cases passent par des prédicats opaques. Le graphe de contrôle devient un enchevêtrement de nœuds où le signal (le vrai code) est noyé dans le bruit (le code factice).

## CFF + BCF combinés : reconnaître le pattern global

Sur un binaire rencontré « dans la nature » (malware, logiciel protégé, challenge CTF), les deux techniques sont presque toujours combinées. Voici les signatures visuelles et techniques pour les reconnaître.

### Dans le Function Graph (Ghidra / IDA / Cutter)

- **Forme en étoile** — Un nœud central massif (le dispatcher) relié à de nombreux blocs. C'est le CFF.  
- **Blocs dupliqués ou quasi-identiques** — Des blocs qui font presque la même chose, dont certains ne sont jamais atteints. C'est le BCF.  
- **Densité anormale de branchements** — Le ratio (nombre de branchements) / (nombre d'instructions utiles) est beaucoup plus élevé que sur du code normal.  
- **Variable de dispatch** — Une variable locale (souvent un entier) qui est lue et écrite dans chaque bloc, et qui contrôle un `cmp`/`je` ou un `switch` central.

### Dans le décompilateur

- Un `while(1)` ou `do { ... } while(true)` englobant toute la logique de la fonction  
- Un `switch` géant ou une cascade de `if/else if` sur une même variable  
- Des blocs de code mort que le décompilateur n'a pas pu éliminer  
- Des expressions conditionnelles complexes impliquant des opérations arithmétiques sans rapport avec la logique métier (prédicats opaques)

### Dans les métriques du binaire

- **Nombre de blocs basiques par fonction** anormalement élevé. Une fonction de 30 lignes de C qui produit 80 blocs basiques a probablement été obfusquée.  
- **Complexité cyclomatique** très élevée. La complexité cyclomatique mesure le nombre de chemins linéairement indépendants dans le CFG. Le CFF la fait exploser.  
- **Taille du binaire** — L'obfuscation augmente significativement la taille du code (×2 à ×5 selon l'agressivité).

## Stratégies de contournement

L'obfuscation de flux de contrôle est significativement plus difficile à contourner que le stripping ou le packing. Il n'existe pas d'équivalent de `upx -d` pour « dé-flattener » automatiquement un binaire. Les approches suivantes sont complémentaires.

### Analyse dynamique : contourner plutôt que comprendre

Face à une fonction fortement obfusquée, la première question est : *ai-je vraiment besoin de comprendre sa structure interne ?*

Souvent, la réponse est non. Si l'objectif est de comprendre ce qu'une fonction fait (pas comment elle le fait), l'analyse dynamique court-circuite l'obfuscation :

- **Frida** (chapitre 13) — Hooker l'entrée et la sortie de la fonction, observer les arguments et la valeur de retour. Peu importe que le flux interne soit un labyrinthe si on sait que `f(42)` retourne `1` et `f(0)` retourne `0`.  
- **GDB** — Poser un breakpoint à l'entrée, exécuter avec différents inputs, observer les résultats. Le code obfusqué s'exécute exactement comme le code original.  
- **Exécution symbolique** (chapitre 18) — angr peut explorer les chemins à travers le dispatcher et résoudre les contraintes, même si l'explosion de chemins est un risque sur les fonctions très aplaties.  
- **Traçage d'exécution** — Stalker (Frida) ou `strace`/`ltrace` pour observer le comportement sans lire le code.

### Analyse de la variable de dispatch

Si une compréhension structurelle est nécessaire, la clé du CFF est la variable de dispatch. C'est le talon d'Achille de la technique.

1. **Identifier la variable** — Dans le décompilateur, repérer la variable entière qui est lue au début de chaque itération de la boucle principale et écrite à la fin de chaque bloc.  
2. **Tracer les valeurs** — Pour chaque case du switch, noter la valeur assignée à la variable de dispatch à la fin du bloc. Cela donne les transitions réelles entre blocs.  
3. **Reconstruire le graphe** — Dessiner un graphe où chaque case est un nœud, et les transitions (valeurs de dispatch) sont les arêtes. Ce graphe est le CFG original, déplié.

Cette approche est fastidieuse mais fiable. C'est essentiellement ce que font les outils de dé-obfuscation automatique, en le formalisant.

### Élimination des prédicats opaques

Pour le BCF, l'objectif est d'identifier les branchements factices :

- **Pattern matching** — Reconnaître les prédicats opaques classiques (`x*(x+1) % 2`, `x² >= 0`) dans le décompilateur. Avec l'expérience, ils deviennent reconnaissables.  
- **Exécution concrète** — Exécuter la fonction avec plusieurs valeurs d'entrée et tracer quels blocs sont réellement atteints. Les blocs qui ne s'exécutent jamais, quel que soit l'input, sont du code mort.  
- **Simplification symbolique** — Des outils comme Miasm ou Triton peuvent simplifier les expressions et prouver qu'un prédicat est constant.

### Outils spécialisés de dé-obfuscation

Plusieurs projets de recherche et outils open source ciblent spécifiquement le CFF :

- **D-810** — Plugin IDA qui détecte et supprime le CFF produit par O-LLVM (section 19.4).  
- **SATURN** (recherche académique) — Approche par exécution symbolique pour reconstruire le CFG original.  
- **Scripts Ghidra/IDA custom** — L'analyse de la variable de dispatch peut être scriptée. Un script Python dans Ghidra peut identifier les blocs du switch, tracer les transitions, et produire un graphe simplifié.  
- **Miasm** — Framework d'analyse binaire qui permet de lifter le code en représentation intermédiaire, simplifier les expressions, et réémettre un code désobfusqué.

Ces outils ont des limites et ne fonctionnent pas de manière universelle. L'obfuscation étant une course entre attaquant et défenseur, chaque nouvelle génération d'outils est suivie par de nouvelles techniques d'obfuscation qui leur résistent.

### L'approche pragmatique

En pratique, face à un binaire obfusqué par CFF + BCF, l'analyste expérimenté combine les approches :

1. **Triage** — Identifier la présence de CFF/BCF (forme en étoile dans le graphe, switch géant dans le décompilateur).  
2. **Analyse dynamique d'abord** — Comprendre le comportement de la fonction par l'observation avant de plonger dans le code.  
3. **Cibler** — Ne dé-obfusquer manuellement que les fonctions critiques (la routine de vérification, le déchiffrement, le parsing). Ignorer les fonctions auxiliaires.  
4. **Automatiser** — Si plusieurs fonctions suivent le même pattern d'obfuscation (même outil, mêmes paramètres), écrire un script pour automatiser la reconstruction.

L'obfuscation de flux de contrôle est un multiplicateur de temps, pas un mur infranchissable. Elle transforme une analyse de 30 minutes en une analyse de plusieurs heures — mais elle ne rend pas le code incompréhensible pour un analyste déterminé et outillé.

---


⏭️ [Obfuscation via LLVM (Hikari, O-LLVM) — reconnaître les patterns](/19-anti-reversing/04-obfuscation-llvm.md)
