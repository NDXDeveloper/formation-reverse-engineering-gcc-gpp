🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.4 — Obfuscation via LLVM (Hikari, O-LLVM) — reconnaître les patterns

> 🎯 **Objectif** : Comprendre pourquoi LLVM est devenu la plateforme d'obfuscation de référence, connaître les principaux projets (O-LLVM, Hikari, Pluto, Armariris), identifier les patterns spécifiques que chacune de leurs passes produit dans un binaire, et savoir les distinguer d'une obfuscation artisanale.

---

## Pourquoi LLVM change la donne pour l'obfuscation

La section précédente présentait le CFF et le BCF comme des techniques abstraites. Dans la pratique, la question qui se pose est : *comment ces transformations sont-elles appliquées au code ?*

La réponse, dans l'immense majorité des cas rencontrés aujourd'hui, tient en un mot : **LLVM**.

LLVM est une infrastructure de compilation modulaire. Son architecture repose sur une représentation intermédiaire (IR) indépendante de l'architecture cible. Le code source est transformé en IR par un frontend (Clang pour le C/C++), puis l'IR passe à travers une série de **passes** d'optimisation avant d'être traduit en code machine par un backend.

Ce qui rend LLVM si attractif pour l'obfuscation, c'est que les passes d'optimisation sont des modules indépendants et enfichables. N'importe qui peut écrire une passe custom qui transforme l'IR — et donc le code final — de la manière souhaitée. Une passe d'obfuscation s'intègre dans le pipeline exactement comme une passe d'optimisation standard. Du point de vue du développeur, obfusquer son code revient à ajouter un flag à la ligne de compilation.

Cela signifie que l'obfuscation opère sur l'IR, **avant** la génération du code machine. Elle bénéficie donc automatiquement de toutes les optimisations standard du backend LLVM (allocation de registres, scheduling, sélection d'instructions). Le code obfusqué est du code machine « propre » produit par un compilateur mature — pas un patchwork appliqué après coup sur le binaire. C'est ce qui le rend si difficile à distinguer du code simplement optimisé, et si résistant aux outils de dé-obfuscation naïfs.

### Le rapport avec GCC

Ce tutoriel est centré sur la chaîne GNU. Alors pourquoi parler de LLVM ?

Parce que les binaires obfusqués par LLVM se retrouvent dans les mêmes environnements que les binaires GCC : même format ELF, mêmes conventions d'appel System V AMD64, mêmes bibliothèques partagées, même loader. Un analyste qui reverse des binaires Linux rencontrera des binaires produits par Clang/LLVM obfusqué aussi souvent — voire plus souvent — que des binaires GCC protégés par des moyens artisanaux. Savoir reconnaître les patterns LLVM est une compétence indispensable, quel que soit le compilateur que *vous* utilisez.

De plus, le linker final peut être `ld` (GNU) même quand le compilateur est Clang. Les deux chaînes coexistent dans un même écosystème.

## O-LLVM : le projet fondateur

### Historique

Obfuscator-LLVM (O-LLVM) est un projet universitaire né en 2010 à l'Université de la Haute-Alsace (Mulhouse). C'est le premier framework d'obfuscation basé sur LLVM à avoir été publié en open source. Bien qu'il ne soit plus activement maintenu (son dernier support officiel cible LLVM 4.0), O-LLVM a défini les trois passes canoniques que tous les projets successeurs ont reprises et améliorées.

### Les trois passes d'O-LLVM

#### Passe 1 : Control Flow Flattening (`-fla`)

C'est l'implémentation de référence du CFF décrit en section 19.3. La passe `-fla` d'O-LLVM transforme chaque fonction en ajoutant :

- Une variable de dispatch (souvent nommée `switchVar` dans l'IR) allouée sur la pile  
- Une boucle `while(true)` englobante  
- Un `switch` sur la variable de dispatch, avec un case par bloc basique original

Le pattern assembleur produit par O-LLVM `-fla` possède des caractéristiques spécifiques qui le distinguent d'un CFF implémenté manuellement :

- **La variable de dispatch est toujours sur la pile**, jamais dans un registre dédié. On voit un `mov eax, [rbp-0xNN]` suivi d'une série de `cmp eax, imm` au début de chaque itération.  
- **Les valeurs de dispatch sont des entiers séquentiels** commençant souvent à 0. O-LLVM n'applique pas de randomisation sur les identifiants de cases dans sa version de base. Les valeurs 0, 1, 2, 3… apparaissent en clair dans les comparaisons.  
- **Le dispatcher est implémenté en cascade de comparaisons**, pas en table de sauts. Même avec 30 cases, O-LLVM génère une série linéaire de `cmp`/`je` plutôt qu'un `jmp [rax*8 + table]`. C'est un pattern très reconnaissable dans le graphe — un long « couloir » de nœuds de comparaison avant de bifurquer.  
- **Le bloc default du switch contient un saut vers la sortie** de la fonction ou un `unreachable`.

#### Passe 2 : Bogus Control Flow (`-bcf`)

L'implémentation O-LLVM du BCF insère des branchements conditionnels basés sur des prédicats opaques. Les prédicats utilisés par O-LLVM sont relativement simples et reconnaissables :

- **Prédicat sur des variables globales** — O-LLVM crée deux variables globales (souvent nommées `x` et `y` dans l'IR, visibles dans `.data` ou `.bss`) et utilise le prédicat `(x * (x - 1)) % 2 == 0`. Cette expression est toujours vraie car le produit de deux entiers consécutifs est toujours pair.  
- **Le code mort est un clone légèrement modifié du code réel** — O-LLVM ne génère pas du code aléatoire pour le faux chemin. Il clone le bloc basique réel et y insère des modifications mineures (constantes différentes, opérations supplémentaires). Cela rend le faux chemin crédible mais signifie aussi que l'analyste verra des blocs « presque identiques » dans le graphe.  
- **Les variables globales du prédicat ne sont jamais modifiées** — Elles sont initialisées à 0 et restent à 0. Si l'analyste repère deux variables globales lues fréquemment mais jamais écrites, c'est un indice fort de BCF O-LLVM.

#### Passe 3 : Substitution d'instructions (`-sub`)

La passe de substitution remplace les opérations arithmétiques et logiques simples par des séquences équivalentes mais plus complexes. Par exemple :

- `a + b` → `a - (-b)`  
- `a + b` → `(a ^ b) + 2 * (a & b)` (addition par manipulation de bits)  
- `a - b` → `a + (~b) + 1` (soustraction par complément à deux)  
- `a ^ b` → `(a & ~b) | (~a & b)` (XOR par décomposition logique)  
- `a | b` → `(a & ~b) | b`  
- `a & b` → `(~(~a | ~b))`

Ces substitutions sont appliquées récursivement : le résultat d'une première substitution peut être lui-même substitué, créant des chaînes d'opérations de plus en plus longues pour une opération originale triviale.

Le pattern en assembleur est caractéristique : on voit de longues séquences de `not`, `and`, `or`, `xor`, `neg`, `add` qui aboutissent finalement à un résultat simple. Un `add eax, ebx` de l'original peut devenir 8 à 12 instructions. L'analyste expérimenté reconnaît ces séquences d'identités arithmétiques et les simplifie mentalement — ou utilise un outil de simplification symbolique.

## Hikari : le successeur modernisé

### Présentation

Hikari (光, « lumière » en japonais — un nom ironique pour un obfuscateur) est un fork d'O-LLVM porté sur des versions plus récentes de LLVM. Il a été développé par Zhang (naville) et a été largement utilisé dans l'écosystème iOS/macOS, mais les binaires ELF obfusqués par Hikari existent également.

Hikari reprend les trois passes d'O-LLVM et en ajoute plusieurs.

### Passes supplémentaires de Hikari

#### Chiffrement de chaînes (`-enable-strcry`)

Hikari chiffre les chaînes de caractères de `.rodata` et insère du code de déchiffrement qui s'exécute à l'initialisation du programme (dans `.init_array` ou dans les constructeurs C++). Les chaînes en clair n'apparaissent jamais dans le fichier sur disque.

Le pattern est reconnaissable :

- La section `.rodata` contient des blobs d'octets apparemment aléatoires là où on attendrait des chaînes lisibles  
- La section `.init_array` contient des pointeurs vers des fonctions de déchiffrement qui n'existaient pas dans le code source  
- Ces fonctions de déchiffrement parcourent des tableaux d'octets et appliquent un XOR ou un déchiffrement plus complexe  
- `strings` ne retourne presque rien d'utile, même sans packing

Pour l'analyste, la contre-mesure est de récupérer les chaînes en mémoire après l'exécution des constructeurs (avec GDB ou Frida), puisque le déchiffrement a déjà eu lieu quand `main()` s'exécute.

#### Chiffrement de pointeurs de fonctions (`-enable-fco`)

Hikari chiffre les pointeurs de fonctions stockés dans les tables (vtables C++, tableaux de callbacks). Au runtime, chaque pointeur est déchiffré juste avant l'appel indirect. Cela complique l'analyse des XREF : Ghidra ne peut pas suivre un `call rax` si la valeur de `rax` est le résultat d'un déchiffrement dynamique.

#### Anti Class Dump (`-enable-acdobf`)

Spécifique à Objective-C (iOS/macOS), cette passe renomme les classes et méthodes pour détruire les métadonnées du runtime. Moins pertinente pour les binaires ELF C/C++ de ce tutoriel, mais mentionnée pour compléter le panorama.

#### Indirection d'appels (`-enable-indibran`)

Les appels directs (`call 0x401234`) sont remplacés par des appels indirects via un registre (`call rax`), où `rax` est calculé à partir d'une expression obfusquée. Cela détruit le graphe d'appels statique : Ghidra ne peut plus construire la liste des fonctions appelées par une fonction donnée, car les cibles sont calculées dynamiquement.

## Autres projets dérivés

L'héritage d'O-LLVM a engendré une constellation de projets :

- **Armariris** — Fork chinois d'O-LLVM, maintenu plus longtemps, avec des améliorations sur la robustesse des prédicats opaques. Les patterns sont quasi identiques à O-LLVM.  
- **Pluto** — Obfuscateur LLVM récent ciblant LLVM 16+, avec des passes de CFF améliorées (randomisation des valeurs de dispatch, dispatcher par table de sauts plutôt que cascade de comparaisons). Les patterns diffèrent sensiblement d'O-LLVM.  
- **Tigress** — Un obfuscateur source-à-source pour le C (pas basé sur LLVM, mais souvent comparé). Tigress peut appliquer de la virtualisation de code, une technique plus puissante que le CFF où le code original est traduit en bytecode d'une machine virtuelle custom. Le résultat est un interpréteur embarqué dans le binaire — fondamentalement différent des patterns LLVM.

## Reconnaître les patterns : guide de diagnostic

Face à un binaire suspect, voici la démarche pour identifier l'outil d'obfuscation utilisé.

### Étape 1 — Le binaire est-il compilé avec Clang/LLVM ?

Vérifier la section `.comment` (si non strippée) :

```bash
$ readelf -p .comment binaire_suspect
  [     0]  clang version 14.0.0 (https://...)
```

Si le binaire est strippé, d'autres indices trahissent Clang : les noms de sections internes, les patterns d'allocation de registres (Clang et GCC ont des stratégies de register allocation légèrement différentes), la structure des prologues/épilogues.

Attention : un binaire Clang n'est pas forcément obfusqué. Et inversement, un binaire obfusqué peut avoir sa section `.comment` strippée. Cette étape oriente l'analyse mais ne conclut pas.

### Étape 2 — Identifier le CFF

Ouvrir une fonction suspecte dans le Function Graph de Ghidra :

- **Pattern O-LLVM / Armariris** — Dispatcher en cascade linéaire de `cmp`/`je`. Valeurs de dispatch séquentielles (0, 1, 2…). Variable de dispatch sur la pile. Forme en « peigne » dans le graphe : un long couloir de comparaisons avec des branches latérales.  
- **Pattern Hikari** — Similaire à O-LLVM pour le CFF de base. Si les chaînes sont absentes et que `.init_array` contient des fonctions de déchiffrement, c'est un indice Hikari.  
- **Pattern Pluto** — Dispatcher par table de sauts (`jmp [reg*8 + table]`). Valeurs de dispatch randomisées (pas séquentielles). Forme en « étoile propre » dans le graphe plutôt qu'en peigne.

### Étape 3 — Identifier le BCF

Chercher dans `.data` ou `.bss` :

- **O-LLVM / Armariris** — Deux variables globales (souvent 4 octets chacune, initialisées à 0) référencées par de nombreuses fonctions mais jamais écrites. Le prédicat `(x * (x-1)) % 2 == 0` se traduit en assembleur par une séquence `imul`/`dec`/`and` (le `% 2` optimisé en `and 1`).  
- **Hikari** — Prédicats similaires, parfois avec des variables en `.bss` plutôt qu'en `.data`.  
- **Pluto** — Prédicats plus variés, utilisant parfois des fonctions mathématiques de la libc (`sin`, `cos`) pour rendre la résolution symbolique plus difficile.

### Étape 4 — Identifier la substitution d'instructions

Chercher des séquences anormalement longues d'opérations logiques :

- Voir 6 à 12 instructions `not`/`and`/`or`/`xor` en cascade pour un résultat qu'un seul `add` ou `xor` aurait produit  
- Les constantes intermédiaires n'ont pas de signification métier — elles sont des artefacts des identités mathématiques  
- Les mêmes patterns de substitution se répètent dans tout le binaire (l'obfuscateur applique les mêmes transformations partout)

### Étape 5 — Identifier le chiffrement de chaînes (Hikari)

```bash
$ strings binaire_suspect | wc -l
23
```

Si `strings` retourne très peu de résultats sur un binaire non packé (sections normales, pas d'entropie anormale sur `.text`), c'est un indice de chiffrement de chaînes. Vérifier `.init_array` :

```bash
$ readelf -x .init_array binaire_suspect
$ objdump -d -j .init_array binaire_suspect
```

Si `.init_array` contient de nombreuses entrées pointant vers des fonctions qui XOR-ent des blocs de données, c'est du chiffrement de chaînes Hikari (ou équivalent).

### Tableau récapitulatif

| Indicateur | O-LLVM | Hikari | Pluto |  
|---|---|---|---|  
| CFF dispatcher | Cascade `cmp`/`je` | Cascade `cmp`/`je` | Table de sauts |  
| Valeurs dispatch | Séquentielles (0,1,2…) | Séquentielles | Randomisées |  
| BCF globals | 2 vars dans `.data`, jamais écrites | 2 vars dans `.bss` | Patterns variés |  
| Substitution | `not`/`and`/`or` classiques | Idem O-LLVM | Identités étendues |  
| Chiffrement chaînes | Non | Oui (`-enable-strcry`) | Variable |  
| Indirection appels | Non | Oui (`-enable-indibran`) | Variable |  
| LLVM cible | ≤ 4.0 | 6.0 – 12.0 | ≥ 16.0 |

## Stratégies d'analyse face à l'obfuscation LLVM

Les stratégies générales de contournement du CFF et du BCF (section 19.3) s'appliquent. Voici les approches spécifiques aux binaires obfusqués par LLVM.

### Exploiter les faiblesses connues d'O-LLVM

Les valeurs de dispatch séquentielles et les prédicats opaques faibles d'O-LLVM sont exploitables programmatiquement :

- Le plugin IDA **D-810** détecte et supprime automatiquement le CFF et le BCF d'O-LLVM en identifiant la variable de dispatch et en reconstruisant le CFG. Ses auteurs ont publié les heuristiques utilisées, transposables en scripts Ghidra.  
- Les deux variables globales du BCF peuvent être identifiées par un script : chercher les variables globales référencées en lecture par plus de N fonctions mais jamais en écriture.

### Récupérer les chaînes chiffrées (Hikari)

Trois approches possibles :

- **Frida** — Hooker les fonctions de `.init_array` ou attendre qu'elles aient fini, puis dumper `.rodata` déchiffrée en mémoire.  
- **GDB** — Poser un breakpoint sur `main`, lancer le programme (les constructeurs auront déjà exécuté le déchiffrement), puis examiner les chaînes en mémoire avec `x/s`.  
- **Émulation** — Utiliser Unicorn Engine ou le mode d'émulation d'angr pour exécuter uniquement les fonctions de déchiffrement sans lancer tout le binaire.

### Résoudre les appels indirects (Hikari)

L'indirection d'appels de Hikari calcule la cible de chaque `call` à partir d'une constante obfusquée. En pratique, la cible est souvent `base + constante ^ clé`, où la clé est fixe pour tout le binaire. Un script Ghidra peut :

1. Identifier tous les `call reg` précédés d'un calcul obfusqué  
2. Extraire la constante et la clé  
3. Calculer la cible réelle  
4. Ajouter des XREF manuelles dans Ghidra

### Simplification symbolique

Pour la passe de substitution, les frameworks d'analyse symbolique excellent :

- **Miasm** — Lifter le code en IR, appliquer les règles de simplification, et constater que 10 instructions se réduisent à un `add`.  
- **Triton** — Évaluer symboliquement une séquence d'instructions et obtenir l'expression simplifiée.  
- **Z3** (chapitre 18) — Prouver l'équivalence entre la séquence obfusquée et une opération simple.

L'automatisation de cette simplification est la base des outils de dé-obfuscation modernes.

### L'approche réaliste

Face à un binaire obfusqué par Hikari avec les cinq passes activées (CFF + BCF + substitution + chiffrement de chaînes + indirection d'appels), l'analyse complète est un travail de plusieurs jours. L'approche réaliste est :

1. **Récupérer les chaînes** — GDB ou Frida, 5 minutes. Cela restaure les repères textuels.  
2. **Identifier les fonctions critiques** — Par XREF sur les chaînes récupérées et sur les imports dynamiques.  
3. **Analyser dynamiquement** — Hooker les fonctions critiques avec Frida, observer inputs/outputs.  
4. **Dé-obfusquer chirurgicalement** — Ne reconstruire le CFG que des 2 ou 3 fonctions qui nécessitent une compréhension structurelle (routine de vérification, déchiffrement, protocole).  
5. **Ignorer le reste** — Les fonctions auxiliaires obfusquées qui ne sont pas sur le chemin critique de l'analyse ne valent pas le temps investi.

---


⏭️ [Stack canaries (`-fstack-protector`), ASLR, PIE, NX](/19-anti-reversing/05-canaries-aslr-pie-nx.md)
