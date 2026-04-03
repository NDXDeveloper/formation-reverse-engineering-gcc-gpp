🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.7 — Lire un listing assembleur sans paniquer : méthode pratique en 5 étapes

> 🎯 **Objectif de cette section** : fournir une méthode structurée et reproductible pour aborder n'importe quel listing assembleur, même inconnu et sans symboles. Les sections 3.1 à 3.6 ont présenté les briques individuelles — cette section les assemble en un workflow concret applicable dès aujourd'hui.

---

## Le problème

Vous ouvrez un binaire dans un désassembleur. Vous voyez des centaines de lignes de `mov`, `lea`, `cmp`, `jne`, `call`… Le réflexe naturel est de tenter de lire le code instruction par instruction, de haut en bas, comme du C. Ça ne fonctionne pas : on se noie dans les détails, on perd le fil, on panique.

L'assembleur ne se lit pas comme du code source. Il se lit **par couches successives**, du plus général vers le plus précis — exactement comme on analyse un texte dans une langue étrangère qu'on maîtrise partiellement : d'abord la structure globale, puis le sens de chaque paragraphe, puis les mots individuels.

La méthode en 5 étapes qui suit formalise cette approche. Elle s'applique à une **fonction unique** — en RE, on travaille presque toujours fonction par fonction.

---

## Vue d'ensemble de la méthode

```
Étape 1 — Délimiter        Trouver le début et la fin de la fonction
       ↓
Étape 2 — Structurer       Identifier les blocs et le flux de contrôle
       ↓
Étape 3 — Caractériser     Lire les appels et les constantes remarquables
       ↓
Étape 4 — Annoter          Nommer les variables, les arguments, les blocs
       ↓
Étape 5 — Reformuler       Réécrire en pseudo-code C
```

Chaque étape produit un résultat concret qui alimente la suivante. On ne cherche **jamais** à tout comprendre d'un coup — on raffine progressivement.

---

## Étape 1 — Délimiter la fonction

Avant de lire quoi que ce soit, il faut savoir **où commence** et **où finit** la fonction.

### Trouver le début

Si le binaire a des symboles, le désassembleur affiche le nom de la fonction et son adresse de début. Sinon, cherchez un **prologue** :

- `push rbp` / `mov rbp, rsp` — prologue classique avec frame pointer.  
- `sub rsp, N` — prologue sans frame pointer.  
- `push rbx` / `push r12` / … — sauvegardes de callee-saved en tout début.  
- Une adresse référencée par un `call` depuis une autre fonction.

Dans Ghidra ou IDA, la détection est automatique. Dans `objdump`, repérez les labels `<function_name>:` ou, si le binaire est strippé, les adresses cibles des `call`.

### Trouver la fin

Cherchez tous les points de sortie de la fonction :

- `ret` — le cas le plus courant (il peut y en avoir plusieurs dans la même fonction).  
- `jmp` vers une autre fonction (tail call, cf. chapitre 16).  
- `call __stack_chk_fail` — sortie par détection de corruption de pile (ne retourne jamais).

> 💡 **Astuce** : dans `objdump`, la fonction suivante commence généralement juste après le dernier `ret` (ou après quelques `nop` de padding). Dans Ghidra/IDA, les frontières de fonctions sont marquées visuellement.

### Ce que vous savez déjà après l'étape 1

- L'adresse de début et de fin de la fonction.  
- Sa taille approximative en octets (un indicateur de complexité).  
- Si elle a un frame pointer ou non (présence de `push rbp` / `mov rbp, rsp`).  
- Combien de registres callee-saved elle utilise (nombre de `push` en prologue).  
- Si elle a un stack canary (présence de `mov rax, [fs:0x28]`).  
- La taille de l'espace local réservé (valeur du `sub rsp, N`).

---

## Étape 2 — Structurer : identifier les blocs et le flux de contrôle

L'objectif est de transformer le listing linéaire en une **carte** des blocs logiques et de leurs connexions — sans encore comprendre le détail de chaque instruction.

### Repérer les basic blocks

Un *basic block* est une séquence d'instructions linéaires sans saut interne ni cible de saut. Il commence à une cible de saut (ou au début de la fonction) et se termine à un saut ou un `ret`. Concrètement :

- Chaque **cible de saut** (`jXX target` → `target` est le début d'un nouveau bloc).  
- Chaque **instruction de saut** (`jXX`, `jmp`) est la fin d'un bloc.  
- Chaque **`call`** ne coupe PAS un bloc (on considère que l'exécution continue après le `call`, sauf cas spéciaux comme `exit` ou `abort`).

### Tracer les flèches

Pour chaque saut, tracez une flèche vers sa cible :

- **Flèche vers le bas** (saut vers une adresse supérieure) → branchement `if`/`else`, `break`, `continue`.  
- **Flèche vers le haut** (saut vers une adresse inférieure) → **boucle** (c'est le signal le plus fiable).  
- **`jmp` vers une autre fonction** → tail call.

### Reconnaître les structures de haut niveau

Avec les flèches tracées, les patterns de la section 3.4 deviennent visuels :

| Pattern de flèches | Structure C probable |  
|---|---|  
| `cmp` → saut vers le bas (contournement) → bloc → convergence | `if` |  
| `cmp` → saut vers le bas → bloc → `jmp` vers le bas → bloc → convergence | `if` / `else` |  
| `cmp` → saut vers le haut (retour) | Boucle (`while`, `for`, `do…while`) |  
| Cascade de `cmp`/`je` vers des blocs distincts, chacun suivi d'un `jmp` vers la même sortie | `switch` (cascade) |  
| `cmp` + `ja` (borne) → `jmp` indirect | `switch` (jump table) |

### Utiliser la vue graphique du désassembleur

Si vous utilisez Ghidra, IDA ou Cutter, **passez en vue graphique** — l'étape 2 est faite automatiquement :

- Ghidra : `Window → Function Graph`  
- IDA : touche `Espace` pour basculer entre listing et graphe  
- Cutter : vue graphique par défaut

Chaque nœud est un basic block, chaque arête est un saut. Les flèches vertes indiquent généralement la branche « condition vraie » et les rouges la branche « condition fausse ».

Même si vous travaillez dans `objdump` (listing brut), **dessiner le graphe à la main** sur papier est un investissement qui accélère toutes les étapes suivantes.

### Ce que vous savez déjà après l'étape 2

- Le nombre de blocs logiques (= complexité structurelle).  
- Les boucles et leur emplacement.  
- Les branchements conditionnels et la structure `if`/`else`/`switch`.  
- Les points de sortie de la fonction.  
- Un « squelette » de la logique sans connaître les détails.

---

## Étape 3 — Caractériser : appels, constantes et opérations remarquables

Maintenant qu'on a la carte, on cherche les **points de repère** — les éléments qui donnent du sens aux blocs sans avoir besoin de lire chaque instruction.

### Lister tous les `call`

Chaque `call` est un indice majeur. Pour chaque appel, notez :

- **La cible** : fonction de la libc connue (`strcmp`, `malloc`, `printf`, `open`…), fonction interne nommée, ou appel indirect (pointeur de fonction / vtable).  
- **Les arguments** : remontez les écritures dans `rdi`, `rsi`, `rdx`… juste avant le `call` (méthode de la section 3.6).  
- **L'utilisation du retour** : que fait le code avec `rax`/`eax` après le `call` ?

Un seul `call strcmp` suivi d'un `test eax, eax` / `jne` vous dit que le bloc est une **vérification de chaîne**. Un `call malloc` suivi d'un `test rax, rax` / `je` vous dit que le bloc est une **allocation avec vérification d'erreur**. Ces informations contextuelles sont souvent suffisantes pour comprendre le rôle de chaque bloc sans lire le détail.

### Repérer les constantes remarquables

Certaines valeurs immédiates sont des signatures :

| Constante | Signification probable |  
|---|---|  
| `0x0` / `0x1` | Booléens, flags, NULL |  
| `0xa` (10), `0x64` (100) | Bases numériques, tailles |  
| `0x20` (32), `0x7e` (126) | Bornes ASCII imprimables |  
| `0x41` ('A'), `0x61` ('a'), `0x30` ('0') | Manipulation de caractères |  
| `0xff`, `0xffff`, `0xffffffff` | Masques, -1 en non signé |  
| `0x400`, `0x1000` | Tailles de page (1024, 4096) |  
| `0x5f3759df` | Fast inverse square root (constante célèbre) |  
| `0x67452301`, `0xefcdab89`… | Constantes d'initialisation MD5 |  
| `0x6a09e667`… | Constantes d'initialisation SHA-256 |  
| `0x63727970`, `0x746f0000` | ASCII encodé en entier ("cryp", "to") |

> 💡 **Astuce** : dans Ghidra, cliquez sur une constante et regardez si le décompilateur l'interprète comme un caractère ASCII. Dans `objdump`, convertissez mentalement les constantes hexadécimales en ASCII ou en décimal — cela révèle souvent leur signification.

### Identifier les chaînes référencées

Les `lea rdi, [rip+offset]` qui chargent des adresses `.rodata` pointent vers des chaînes littérales. Dans Ghidra/IDA, ces chaînes sont affichées en commentaire. Dans `objdump`, il faut aller les chercher avec `strings` ou `readelf` :

```asm
lea     rdi, [rip+0x1a2b]    ; dans Ghidra : "Invalid password"
```

Une seule chaîne comme `"Invalid password"` ou `"License expired"` identifie instantanément le rôle d'un bloc entier.

### Repérer les opérations structurantes

Sans lire le détail, certaines opérations révèlent la nature du code :

- **`xor` en boucle** sur un buffer → chiffrement/déchiffrement XOR.  
- **Série de `shl`/`shr`/`and`/`or`** → manipulation de bits, parsing de champs.  
- **`imul` avec constante magique** → division par une constante (cf. section 3.3).  
- **Accès `[reg+offset]` répétés avec le même registre base** → accès aux champs d'une structure.  
- **`[reg+index*4]` ou `[reg+index*8]`** → parcours de tableau.

### Ce que vous savez déjà après l'étape 3

- Le **rôle probable** de chaque bloc (vérification, allocation, calcul, I/O, crypto…).  
- Les **données manipulées** (chaînes, fichiers, buffers, structures).  
- Les **dépendances externes** (fonctions de la libc, syscalls).  
- Une **hypothèse globale** sur ce que fait la fonction.

---

## Étape 4 — Annoter : nommer les variables, les arguments et les blocs

C'est l'étape où l'on transforme le désassemblage brut en quelque chose de lisible. L'annotation est le travail fondamental du reverse engineer — c'est ce qui distingue un listing incompréhensible d'une analyse exploitable.

### Nommer les arguments de la fonction

D'après l'analyse des sites d'appel (XREF) et le début de la fonction, identifiez les registres d'entrée et donnez-leur des noms :

```
rdi → input_str    (pointeur vers une chaîne, déduit d'un call strcmp plus loin)  
esi → key_length   (entier, utilisé comme borne de boucle)  
```

### Nommer les variables locales

Pour chaque offset pile récurrent (`[rbp-0x4]`, `[rbp-0x8]`, `[rsp+0x1c]`…), attribuez un nom basé sur l'usage observé :

```
[rbp-0x04] → counter     (incrémenté dans une boucle, comparé à key_length)
[rbp-0x08] → result      (mis à 0 ou 1, retourné dans eax en fin de fonction)
[rbp-0x10] → temp_ptr    (pointeur, passé comme argument à strcmp)
```

### Nommer les blocs

Chaque basic block identifié à l'étape 2 mérite un nom descriptif :

```
Bloc 0x401120–0x40113a → prologue + init  
Bloc 0x40113a–0x401158 → loop_body (boucle principale)  
Bloc 0x401158–0x401168 → loop_test (condition de continuation)  
Bloc 0x401168–0x401178 → success_path (retourne 1)  
Bloc 0x401178–0x401188 → failure_path (retourne 0)  
```

### Ajouter des commentaires aux instructions clés

Pas besoin de commenter chaque ligne — concentrez-vous sur :

- Les **`cmp`** : quelle condition est testée et pourquoi.  
- Les **`call`** : ce que fait l'appel, avec quels arguments.  
- Les **points de décision** : pourquoi le code prend un chemin ou l'autre.  
- Les **calculs non triviaux** : les `lea` arithmétiques, les constantes magiques de division, les manipulations de bits.

Dans Ghidra/IDA, utilisez les fonctions de renommage (touche `L` pour les labels, `N` pour les fonctions dans Ghidra ; `N` dans IDA) et les commentaires (`/` ou `;`). Dans un listing texte, annotez directement dans un fichier à part ou utilisez des commentaires inline.

### Ce que vous savez après l'étape 4

- Chaque variable a un nom significatif.  
- Chaque bloc a un rôle identifié.  
- Les parties obscures sont réduites aux instructions individuelles les plus complexes.  
- Le listing est suffisamment annoté pour être relu des jours plus tard sans tout reprendre de zéro.

---

## Étape 5 — Reformuler en pseudo-code C

L'étape finale consiste à **réécrire la logique** dans un pseudo-code C lisible. Ce n'est pas de la décompilation automatique — c'est votre compréhension, validée par le listing annoté, traduite dans un langage humain.

### Méthode de reconstruction

Partez de la structure identifiée à l'étape 2 et remplissez-la avec les détails des étapes 3 et 4 :

```c
// Pseudo-code reconstitué
int check_key(const char *input, int expected_length) {
    // Vérifie la longueur
    if (strlen(input) != expected_length)
        return 0;

    // Parcourt chaque caractère
    int sum = 0;
    for (int i = 0; i < expected_length; i++) {
        sum += input[i] ^ 0x42;   // XOR avec clé fixe
    }

    // Compare le checksum
    if (sum == 0x1337)
        return 1;
    
    return 0;
}
```

### Règles de rédaction du pseudo-code

- **Ne cherchez pas la perfection syntaxique.** L'objectif est la compréhension, pas la recompilation. `if (truc)` est suffisant même si le code exact est `if (*(int*)(buf + off) == 0x42)`.  
- **Conservez les constantes significatives** (`0x42`, `0x1337`) — elles sont importantes pour le RE.  
- **Indiquez les incertitudes.** Si vous n'êtes pas sûr d'un type ou d'une opération, marquez-le : `/* type exact inconnu, probablement unsigned */`.  
- **Faites des allers-retours** avec le listing. Le pseudo-code révèle souvent des incohérences qui renvoient à une relecture ciblée du désassemblage.

### Valider le pseudo-code

Trois moyens de vérification :

1. **Cohérence interne** : le pseudo-code a-t-il du sens ? Les types sont-ils cohérents ? Les boucles terminent-elles ?  
2. **Test dynamique** : si vous avez accès au binaire, utilisez GDB pour exécuter la fonction avec des entrées connues et vérifier que le comportement correspond à votre pseudo-code (cf. chapitres 11–12).  
3. **Comparaison avec le décompilateur** : confrontez votre pseudo-code avec la sortie de Ghidra Decompiler. Les divergences pointent vers des erreurs de l'un ou de l'autre — les deux sont utiles.

---

## La méthode appliquée : exemple complet

Voici un listing assembleur brut (syntaxe Intel, 20 lignes). Appliquons les 5 étapes.

```asm
0x401120:  push    rbp
0x401121:  mov     rbp, rsp
0x401124:  mov     dword [rbp-0x14], edi
0x401127:  mov     qword [rbp-0x20], rsi
0x40112b:  mov     dword [rbp-0x4], 0x0
0x401132:  mov     dword [rbp-0x8], 0x0
0x401139:  jmp     0x401156
0x40113b:  mov     eax, dword [rbp-0x8]
0x40113e:  movsxd  rdx, eax
0x401141:  mov     rax, qword [rbp-0x20]
0x401145:  add     rax, rdx
0x401148:  movzx   eax, byte [rax]
0x40114b:  movsx   eax, al
0x40114e:  add     dword [rbp-0x4], eax
0x401151:  add     dword [rbp-0x8], 0x1
0x401155:  nop
0x401156:  mov     eax, dword [rbp-0x8]
0x401159:  cmp     eax, dword [rbp-0x14]
0x40115c:  jl      0x40113b
0x40115e:  mov     eax, dword [rbp-0x4]
0x401161:  pop     rbp
0x401162:  ret
```

### Étape 1 — Délimiter

- **Début** : `0x401120` — prologue `push rbp` / `mov rbp, rsp`.  
- **Fin** : `0x401162` — `ret`.  
- Frame pointer : **oui** (`push rbp` / `mov rbp, rsp`).  
- Pas de callee-saved supplémentaire (un seul `push`).  
- Pas de `sub rsp` → espace local petit (red zone suffisante ou compilateur confiant).  
- Pas de stack canary.  
- Taille : 67 octets → fonction courte.

Arguments spillés :

```
[rbp-0x14] ← edi   → 1er argument, 32 bits (int)
[rbp-0x20] ← rsi   → 2e argument, 64 bits (pointeur)
```

La fonction prend 2 paramètres : un `int` et un pointeur.

### Étape 2 — Structurer

Identifions les sauts :

- `0x401139: jmp 0x401156` → saut vers le bas (contournement, va au test).  
- `0x40115c: jl 0x40113b` → saut vers le **haut** → **boucle**.

Blocs :

```
Bloc A [0x401120–0x401139] : prologue + initialisation + jmp vers le test  
Bloc B [0x40113b–0x401155] : corps de la boucle  
Bloc C [0x401156–0x40115c] : test de boucle (cmp + jl retour au corps)  
Bloc D [0x40115e–0x401162] : retour de la valeur  
```

Structure : `init → jmp test → [corps → test → si vrai retour au corps] → retour`. C'est une boucle **`for`/`while` avec test en bas** (pattern GCC classique).

### Étape 3 — Caractériser

- **Aucun `call`** → fonction autonome, pas d'appels externes.  
- **Constante `0x0`** : initialisation de deux variables locales à 0.  
- **`movzx` + `movsx`** sur un octet (`byte [rax]`) → lecture d'un `char` signé.  
- **`add dword [rbp-0x4], eax`** → accumulation (somme).  
- **`add dword [rbp-0x8], 0x1`** → incrémentation → compteur de boucle.  
- **`cmp eax, dword [rbp-0x14]`** → compare le compteur avec le 1er argument (la borne).  
- **`jl`** → comparaison signée, boucle tant que `compteur < borne`.  
- **Retour dans `eax`** → retourne `[rbp-0x4]`, la somme accumulée.

Hypothèse : la fonction **somme les valeurs des caractères** d'une chaîne (ou d'un buffer) sur une longueur donnée.

### Étape 4 — Annoter

```
[rbp-0x14] : edi  → len       (borne de la boucle, int)
[rbp-0x20] : rsi  → str       (pointeur vers le buffer, char*)
[rbp-0x04] :      → sum       (accumulateur, initialisé à 0)
[rbp-0x08] :      → i         (compteur de boucle, initialisé à 0)

Bloc A : prologue, spill des arguments, init sum=0, i=0  
Bloc B : corps de boucle — lit str[i], ajoute à sum, incrémente i  
Bloc C : test — si i < len, retourne au bloc B  
Bloc D : retourne sum  
```

### Étape 5 — Reformuler

```c
int sum_chars(const char *str, int len) {
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum += (int)str[i];    // movsx → char signé promu en int
    }
    return sum;
}
```

Vingt lignes d'assembleur → cinq lignes de C. La méthode a pris quelques minutes. Sans méthode, les mêmes vingt lignes pourraient prendre bien plus longtemps et donner un résultat moins fiable.

---

## Quand accélérer, quand ralentir

La méthode en 5 étapes est complète et rigoureuse, mais en pratique vous adapterez son intensité au contexte :

### Accélérer (survol rapide)

- **Fonctions triviales** (< 10 instructions) : les étapes 1 et 5 suffisent souvent.  
- **Fonctions de la libc / wrappers évidents** : un seul `call` vers une fonction connue avec passage direct des arguments → le pseudo-code se lit directement.  
- **Quand vous avez un décompilateur** : Ghidra Decompiler fait les étapes 2 à 5 automatiquement. Validez rapidement sa sortie plutôt que de tout refaire à la main.

### Ralentir (analyse approfondie)

- **Boucles imbriquées** : dessinez le graphe de flux avec soin, chaque saut arrière est une boucle.  
- **Fonctions longues (> 100 instructions)** : découpez en sous-fonctions logiques (les blocs entre deux `call` majeurs).  
- **Code optimisé (`-O2`/`-O3`)** : les idiomes du compilateur (constantes magiques, `cmov`, `lea` arithmétique) nécessitent un décodage plus lent à l'étape 3.  
- **Code obfusqué** : chaque étape prend plus de temps, et il faut ajouter une étape de « désobfuscation mentale » (cf. chapitre 19).  
- **Crypto / bit manipulation** : les opérations `xor`, `shl`, `ror`, `and` en série nécessitent souvent une analyse instruction par instruction avec des valeurs concrètes sur papier.

---

## Erreurs courantes à éviter

### Lire de haut en bas sans structurer d'abord

C'est l'erreur la plus fréquente chez les débutants. Sans l'étape 2 (structurer), on se perd dans les sauts. Identifiez **toujours** la structure de contrôle avant de lire le détail des blocs.

### Confondre logique applicative et mécanique ABI

Le prologue, l'épilogue, les spills d'arguments, le canary, le padding d'alignement — tout cela est de la « plomberie » de la convention d'appel. Apprenez à les reconnaître pour les **ignorer** rapidement et vous concentrer sur le code qui compte.

### Négliger les types

La taille des registres (`eax` vs `rax`), le choix de `movsx` vs `movzx`, le choix de `jl` vs `jb` — chaque détail vous donne des informations de typage. Ignorer ces indices mène à des pseudo-codes incorrects.

### Vouloir tout comprendre d'un seul passage

Certaines instructions résisteront au premier passage. Notez-les, avancez, et revenez-y une fois que le contexte global est compris. La compréhension est itérative — acceptez l'incertitude temporaire.

### Oublier de vérifier dynamiquement

Un pseudo-code élégant peut être complètement faux. Dès que possible, validez votre compréhension avec GDB : posez un breakpoint, exécutez avec des entrées connues, vérifiez que les registres et la mémoire correspondent à vos prédictions (cf. chapitres 11–12).

---

## Aide-mémoire de la méthode

```
┌─────────────────────────────────────────────────────────────────┐
│                   MÉTHODE DE LECTURE EN 5 ÉTAPES                │
├──────────────┬──────────────────────────────────────────────────┤
│ 1. DÉLIMITER │ Début (prologue), fin (ret), taille, arguments   │
│              │ → Résultat : périmètre de la fonction            │
├──────────────┼──────────────────────────────────────────────────┤
│ 2. STRUCTURER│ Blocs, sauts, boucles (flèches vers le haut),    │
│              │ if/else, switch                                  │
│              │ → Résultat : graphe de flux de contrôle          │
├──────────────┼──────────────────────────────────────────────────┤
│ 3. CARACTÉR- │ call (+ arguments + retour), constantes,         │
│    ISER      │ chaînes, opérations remarquables                 │
│              │ → Résultat : rôle de chaque bloc                 │
├──────────────┼──────────────────────────────────────────────────┤
│ 4. ANNOTER   │ Nommer arguments, variables locales, blocs,      │
│              │ commenter les instructions clés                  │
│              │ → Résultat : listing lisible et pérenne          │
├──────────────┼──────────────────────────────────────────────────┤
│ 5. REFORMULER│ Réécrire en pseudo-code C, vérifier              │
│              │ la cohérence, confronter au décompilateur        │
│              │ → Résultat : compréhension validée               │
└──────────────┴──────────────────────────────────────────────────┘
```

---

## Ce qu'il faut retenir pour la suite

1. **Ne jamais lire un listing instruction par instruction de haut en bas** — toujours structurer d'abord (étape 2).  
2. **Les `call` et les chaînes sont les meilleurs points de repère** — ils donnent le contexte sans effort (étape 3).  
3. **L'annotation est l'investissement le plus rentable** — un listing annoté se relit en minutes, un listing brut nécessite de tout recommencer (étape 4).  
4. **Le pseudo-code C est le livrable final** — c'est ce que vous partagez, ce que vous documentez, ce que vous vérifiez (étape 5).  
5. **La méthode s'améliore avec la pratique** — les patterns se reconnaissent de plus en plus vite, les étapes fusionnent, et ce qui prenait une heure finit par prendre quelques minutes.

---


⏭️ [Différence entre appel de bibliothèque (`call printf@plt`) et syscall direct (`syscall`)](/03-assembleur-x86-64/08-call-plt-vs-syscall.md)
