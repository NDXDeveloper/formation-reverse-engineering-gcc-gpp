🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.3 — Localisation de la routine de vérification (approche top-down)

> 📖 **Rappel** : cette section suppose une maîtrise de base de Ghidra (import, CodeBrowser, Decompiler, Symbol Tree, cross-references). Si ce n'est pas le cas, revenez au chapitre 8 (sections 8.1 à 8.7) avant de poursuivre.

---

## Introduction

Les deux premières sections nous ont fourni une carte du terrain : on connaît le format du binaire, ses protections, ses chaînes révélatrices et ses fonctions. Il est temps d'ouvrir le capot. L'objectif de cette section est précis : **localiser la fonction qui décide si la clé est valide ou non**, c'est-à-dire identifier le point exact dans le code où le programme emprunte le chemin « succès » ou le chemin « échec ».

Pour y parvenir, on adopte une approche **top-down** : partir de ce qu'on connaît (le point d'entrée, les chaînes de caractères) et descendre dans le graphe d'appels jusqu'à atteindre la fonction de vérification. C'est la stratégie la plus naturelle et la plus fiable sur un binaire de taille modeste. L'approche inverse — bottom-up, partir d'un appel à `strcmp` et remonter — est également valide et sera mentionnée en fin de section.

Nous travaillons sur `keygenme_O0` (avec symboles). En fin de section, nous verrons comment appliquer la même démarche sur la variante strippée `keygenme_strip`, où les noms de fonctions ont disparu.

---

## Étape 1 — Import et analyse automatique dans Ghidra

### Création du projet et import

Après avoir lancé Ghidra et créé un projet (ou réutilisé un projet existant), on importe le binaire :

1. **File → Import File** → sélectionner `keygenme_O0`.  
2. Ghidra détecte automatiquement le format (`ELF`, `x86:LE:64:default`). Accepter les valeurs par défaut.  
3. Un dialogue propose les options d'analyse. Cliquer sur **Yes** puis **Analyze** pour lancer l'analyse automatique avec les options par défaut.

L'analyse prend quelques secondes sur un binaire aussi petit. Ghidra effectue plusieurs passes :

- **Disassembly** : décodage des instructions machine en mnémoniques assembleur.  
- **Function identification** : détection des frontières de fonctions (prologues, épilogues, patterns `call`/`ret`).  
- **Decompilation** : traduction de l'assembleur en pseudo-C pour chaque fonction.  
- **Reference analysis** : construction du graphe de cross-references (qui appelle qui, qui référence quelle donnée).  
- **DWARF import** : puisque le binaire contient des informations de débogage, Ghidra importe les noms de fonctions, les types des variables et les structures.

> 💡 **Astuce** : sur un binaire avec DWARF, Ghidra peut parfois manquer certaines informations au premier passage. Si le decompiler affiche des types génériques (`undefined8`, `long`) alors que le source utilise des `uint32_t` ou des `uint16_t`, relancez l'analyse via **Analysis → Auto Analyze** en cochant spécifiquement **DWARF**.

### Premier coup d'œil au Symbol Tree

Une fois l'analyse terminée, le panneau **Symbol Tree** (à gauche dans le CodeBrowser) liste les fonctions détectées. En dépliant le dossier **Functions**, on retrouve exactement les symboles que `nm` avait listés en section 21.1 :

```
Functions/
├── _start
├── main
├── check_license
├── compute_hash
├── derive_key
├── format_key
├── read_line
├── rotate_left
└── ... (fonctions de la CRT : __libc_csu_init, _init, _fini, etc.)
```

Les fonctions de la C Runtime (`_start`, `__libc_csu_init`…) sont le code d'initialisation ajouté par GCC/glibc avant l'appel à `main`. On peut les ignorer pour l'analyse du keygenme.

---

## Étape 2 — Analyse de `main()` : le point de départ

Double-cliquer sur `main` dans le Symbol Tree ouvre la fonction dans les panneaux Listing (assembleur) et Decompiler (pseudo-C). Commençons par le pseudo-C, plus lisible en première lecture.

### Lecture du decompiler

Le decompiler de Ghidra produit un pseudo-C qui ressemble à ceci (les noms de variables peuvent différer légèrement selon la version de Ghidra et les options DWARF) :

```c
int main(void)
{
    char username[32];
    char user_key[21];
    size_t ulen;

    printf("%s\n\n", "=== KeyGenMe v1.0 — RE Training ===");

    printf("Enter username: ");
    if (read_line(username, 32) != 0) {
        return 1;
    }

    ulen = strlen(username);
    if ((ulen < 3) || (31 <= ulen)) {
        printf("[-] Username must be between 3 and 31 characters.\n");
        return 1;
    }

    printf("Enter license key (XXXX-XXXX-XXXX-XXXX): ");
    if (read_line(user_key, 21) != 0) {
        return 1;
    }

    if (check_license(username, user_key) != 0) {
        printf("[+] Valid license! Welcome, %s.\n", username);
        return 0;
    }
    else {
        printf("[-] Invalid license. Try again.\n");
        return 1;
    }
}
```

Même sans avoir vu le code source, ce pseudo-C est remarquablement clair en `-O0` avec symboles DWARF. On identifie immédiatement le flux du programme :

1. Affichage de la bannière.  
2. Lecture du username, vérification de longueur (3 à 31 caractères).  
3. Lecture de la clé de licence.  
4. Appel à **`check_license(username, user_key)`** — c'est le point de décision.  
5. Selon la valeur de retour : message de succès ou d'échec.

La fonction `check_license` est notre cible. Son nom est explicite ici grâce aux symboles, mais le raisonnement serait le même sans eux : on chercherait « la fonction appelée juste avant l'embranchement entre le message de succès et le message d'échec ».

### Le flux dans le Listing (assembleur)

En basculant vers le panneau Listing, on observe le code assembleur correspondant. Voici le passage critique autour de l'appel à `check_license` :

```nasm
; Préparation des arguments (System V AMD64)
LEA     RDI, [RBP + username]      ; 1er argument : username  
LEA     RSI, [RBP + user_key]      ; 2ème argument : user_key  
CALL    check_license              ; appel de la fonction  

; Retour dans EAX — convention : 1 = valide, 0 = invalide
TEST    EAX, EAX  
JZ      .label_fail                ; si EAX == 0 → saut vers le chemin "échec"  

; Chemin "succès" (EAX != 0)
LEA     RSI, [RBP + username]  
LEA     RDI, [.rodata + MSG_OK]    ; "[+] Valid license! ..."  
CALL    printf@plt  
...

.label_fail:
; Chemin "échec" (EAX == 0)
LEA     RDI, [.rodata + MSG_FAIL]  ; "[-] Invalid license. ..."  
CALL    printf@plt  
```

Ce bloc est le cœur de la mécanique du crackme. On y lit :

- **`CALL check_license`** : le programme délègue la vérification à une sous-fonction.  
- **`TEST EAX, EAX`** : teste si la valeur de retour est zéro. L'instruction `TEST` effectue un AND bit à bit sans stocker le résultat — elle positionne uniquement les flags, en particulier le Zero Flag (ZF).  
- **`JZ .label_fail`** : si ZF = 1 (c'est-à-dire EAX = 0, clé invalide), saute vers le chemin d'échec. Sinon (EAX ≠ 0, clé valide), l'exécution continue séquentiellement vers le chemin de succès.

Ce couple `TEST`/`JZ` est le **point de décision** du programme. C'est l'instruction qu'on patchera en section 21.6 pour inverser le comportement (transformer le `JZ` en `JNZ` ou en `JMP` inconditionnel).

---

## Étape 3 — Descente dans `check_license()`

On double-clique sur `check_license` dans le Listing (ou dans le Decompiler en cliquant sur le nom de la fonction) pour naviguer vers sa définition.

### Pseudo-C de `check_license`

```c
int check_license(char *username, char *user_key)
{
    uint32_t hash;
    uint16_t groups[4];
    char expected[20];

    hash = compute_hash(username);
    derive_key(hash, groups);
    format_key(groups, expected);

    if (strcmp(expected, user_key) == 0) {
        return 1;
    }
    return 0;
}
```

La logique est limpide :

1. **`compute_hash(username)`** — transforme le nom d'utilisateur en un entier 32 bits.  
2. **`derive_key(hash, groups)`** — dérive 4 valeurs 16 bits à partir du hash.  
3. **`format_key(groups, expected)`** — formate ces valeurs en chaîne `XXXX-XXXX-XXXX-XXXX`.  
4. **`strcmp(expected, user_key)`** — compare la clé attendue (calculée) avec la clé saisie par l'utilisateur.

Le `strcmp` est le **point de comparaison ultime**. Si les deux chaînes sont identiques, `strcmp` retourne 0, la condition est vraie, et `check_license` retourne 1 (succès). Sinon, elle retourne 0 (échec).

### Le Listing assembleur de `check_license`

Dans le panneau Listing, on retrouve ces quatre appels successifs :

```nasm
check_license:
    PUSH    RBP
    MOV     RBP, RSP
    SUB     RSP, 0x50                ; allocation du cadre de pile
    MOV     QWORD PTR [RBP-0x48], RDI   ; sauvegarde de username
    MOV     QWORD PTR [RBP-0x50], RSI   ; sauvegarde de user_key

    ; ── canary : lecture ──
    MOV     RAX, QWORD PTR FS:[0x28]
    MOV     QWORD PTR [RBP-0x8], RAX

    ; ── (1) compute_hash(username) ──
    MOV     RDI, QWORD PTR [RBP-0x48]
    CALL    compute_hash
    MOV     DWORD PTR [RBP-0x0c], EAX   ; hash stocké en local

    ; ── (2) derive_key(hash, groups) ──
    MOV     EDI, DWORD PTR [RBP-0x0c]
    LEA     RSI, [RBP-0x18]             ; adresse du tableau groups[4]
    CALL    derive_key

    ; ── (3) format_key(groups, expected) ──
    LEA     RDI, [RBP-0x18]
    LEA     RSI, [RBP-0x30]             ; adresse du buffer expected
    CALL    format_key

    ; ── (4) strcmp(expected, user_key) ──
    LEA     RDI, [RBP-0x30]             ; expected (1er argument)
    MOV     RSI, QWORD PTR [RBP-0x50]   ; user_key (2ème argument)
    CALL    strcmp@plt

    ; ── Point de décision ──
    TEST    EAX, EAX
    JNE     .return_zero                ; si strcmp != 0 → clé invalide

    MOV     EAX, 0x1                    ; retour 1 (succès)
    JMP     .epilogue

.return_zero:
    MOV     EAX, 0x0                    ; retour 0 (échec)

.epilogue:
    ; ── canary : vérification ──
    MOV     RDX, QWORD PTR [RBP-0x8]
    XOR     RDX, QWORD PTR FS:[0x28]
    JNE     __stack_chk_fail@plt

    LEAVE
    RET
```

Prenons le temps de lire ce listing méthodiquement, en appliquant la méthode en 5 étapes du chapitre 3 (section 3.7) :

**1. Prologue et cadre de pile** — Les trois premières instructions (`PUSH RBP` / `MOV RBP, RSP` / `SUB RSP, 0x50`) créent un cadre de pile de 80 octets. C'est classique en `-O0` : le compilateur alloue un espace confortable pour les variables locales, même s'il n'en utilise qu'une partie.

**2. Sauvegarde des arguments** — Les deux paramètres (`username` dans `RDI`, `user_key` dans `RSI`) sont immédiatement copiés sur la pile. En `-O0`, GCC sauvegarde systématiquement les arguments en mémoire plutôt que de les conserver dans des registres. C'est inefficace mais très lisible : on sait exactement où chaque variable réside.

**3. Canary** — La lecture de `FS:[0x28]` et son stockage à `[RBP-0x8]` constituent le code de protection du stack canary (vu en section 21.2). Ce bloc et son pendant dans l'épilogue sont du bruit de protection à ignorer.

**4. Les quatre appels** — On retrouve la séquence `compute_hash` → `derive_key` → `format_key` → `strcmp@plt`. Chaque appel est précédé de la mise en place des arguments dans les registres (`RDI`, `RSI`) conformément à la convention System V AMD64. Le résultat de chaque fonction est dans `EAX` (ou `RAX` pour les pointeurs).

**5. Point de décision** — Après `CALL strcmp@plt`, on retrouve le couple `TEST EAX, EAX` / `JNE .return_zero`. Si `strcmp` retourne une valeur non nulle (chaînes différentes), le saut est pris et la fonction retourne 0 (échec). Sinon, elle retourne 1 (succès).

> 💡 **Remarque sur les sauts** : dans `main`, c'est un `JZ` qui mène à l'échec (on teste `check_license() == 0`). Dans `check_license`, c'est un `JNE` qui mène au retour 0 (on teste `strcmp() != 0`). Les deux sont logiquement cohérents, mais le saut conditionnel utilisé dépend de la façon dont le compilateur organise le code. C'est pourquoi il est essentiel de lire le contexte (que se passe-t-il avant et après le saut) plutôt que de se fier uniquement au mnémonique.

---

## Étape 4 — Utiliser les cross-references (XREF)

Les cross-references sont l'un des outils les plus puissants d'un désassembleur. Elles permettent de répondre à deux questions fondamentales : « qui appelle cette fonction ? » et « qui référence cette donnée ? ».

### XREF vers les chaînes de succès/échec

Une approche alternative pour localiser le point de décision consiste à partir des chaînes trouvées par `strings` en section 21.1. Dans Ghidra :

1. **Window → Defined Strings** (ou **Search → For Strings**).  
2. Chercher `Valid license` ou `Invalid license`.  
3. Double-cliquer sur la chaîne pour naviguer vers son emplacement dans `.rodata`.  
4. Dans le panneau Listing, la chaîne apparaît avec ses cross-references :

```
XREF[1]: main:001015e6(*)
```

5. Cliquer sur la référence pour sauter directement au point dans `main` où cette chaîne est utilisée.

On atterrit sur le `LEA RDI, [MSG_OK]` qui précède le `CALL printf@plt` — exactement dans le chemin de succès. En remontant de quelques instructions, on retrouve le `TEST EAX, EAX` / `JZ` après l'appel à `check_license`.

### XREF vers `strcmp`

De la même façon, on peut chercher tous les appels à `strcmp` dans le binaire :

1. Dans le Symbol Tree, naviguer vers **Imports → strcmp**.  
2. Clic droit → **References → Show References to**.

Ghidra affiche la liste de tous les endroits du code qui appellent `strcmp`. Sur notre keygenme, il n'y a qu'un seul appel — dans `check_license`. Sur un binaire plus complexe avec plusieurs vérifications, cette technique permet d'identifier rapidement tous les points de comparaison.

### XREF depuis `check_license`

Inversement, on peut vérifier qui appelle `check_license` :

1. Cliquer sur le nom `check_license` dans le Listing.  
2. Clic droit → **References → Show References to**.

Résultat : un seul appelant — `main`. Cela confirme que toute la logique de vérification est centralisée dans cette unique fonction.

### Graphe d'appels

Pour une vue d'ensemble, Ghidra propose un graphe d'appels :

1. Sélectionner `main` dans le Symbol Tree.  
2. **Window → Function Call Graph**.

Le graphe affiche visuellement la hiérarchie :

```
main
 ├── printf@plt
 ├── read_line
 │    ├── fgets@plt
 │    └── strlen@plt
 ├── strlen@plt
 └── check_license
      ├── compute_hash
      │    └── strlen@plt
      ├── derive_key
      │    └── rotate_left
      ├── format_key
      │    └── snprintf@plt
      └── strcmp@plt
```

Ce graphe confirme et enrichit l'esquisse que nous avions déduite de `nm` en section 21.1. On voit désormais les appels à la libc (`strlen`, `snprintf`, `strcmp`) et on comprend le rôle de chaque fonction interne : `compute_hash` travaille sur le username (il appelle `strlen`), `format_key` produit une chaîne formatée (il appelle `snprintf`), et `check_license` orchestre le tout avant de comparer via `strcmp`.

---

## Étape 5 — Renommage et annotation

Même sur un binaire avec symboles, il est bonne pratique d'annoter le désassemblage pour documenter sa compréhension. Ghidra sauvegarde ces annotations dans le projet — elles seront disponibles à chaque réouverture.

### Commentaires aux points clés

On ajoute des commentaires (clic droit → **Comments → Set Pre/Post Comment**) aux emplacements stratégiques :

- Sur le `CALL check_license` dans `main` : `// Point de décision : vérifie la licence`  
- Sur le `TEST EAX, EAX` / `JZ` dans `main` : `// Si check_license retourne 0 → échec`  
- Sur le `CALL strcmp@plt` dans `check_license` : `// Compare clé calculée vs clé saisie`  
- Sur le `TEST EAX, EAX` / `JNE` dans `check_license` : `// strcmp != 0 → chaînes différentes → échec`

### Labels personnalisés

On peut renommer les labels de saut pour améliorer la lisibilité :

- `.label_fail` → `LICENSE_INVALID` (dans `main`)  
- `.return_zero` → `KEY_MISMATCH` (dans `check_license`)

### Bookmarks

Ghidra permet de poser des bookmarks (marque-pages) sur les adresses critiques. On marque :

- L'adresse du `CALL check_license` (point d'entrée de la vérification)  
- L'adresse du `CALL strcmp@plt` (point de comparaison)  
- L'adresse du `JZ`/`JNE` (saut conditionnel à patcher en section 21.6)

Ces bookmarks sont accessibles via **Window → Bookmarks** et permettent de naviguer instantanément vers les points d'intérêt lors des sessions ultérieures.

---

## Application sur un binaire strippé

Sur `keygenme_strip`, les symboles ont été supprimés. Le Symbol Tree ne contient plus `check_license` ni `compute_hash` — toutes les fonctions internes apparaissent sous des noms générés par Ghidra (`FUN_00101189`, `FUN_001011b2`, etc.). Comment s'y retrouver ?

### Stratégie 1 : partir des chaînes

Les chaînes dans `.rodata` ne sont **pas** supprimées par `strip` (elles font partie du code, pas des symboles de débogage). On peut donc appliquer exactement la même technique XREF :

1. Chercher `"Valid license"` dans les Defined Strings.  
2. Suivre la cross-reference → on atterrit dans une fonction (ex. `FUN_001013d0`).  
3. Cette fonction est `main` (elle contient le `printf` de la bannière, les lectures d'entrée, et l'appel à la fonction de vérification).  
4. Identifier l'appel précédant l'embranchement succès/échec → c'est `check_license` (renommer en conséquence).

### Stratégie 2 : partir de `strcmp`

Les fonctions importées de la libc restent dans `.dynsym` même après stripping (elles sont nécessaires au dynamic linker). On peut donc toujours chercher les appels à `strcmp@plt` :

1. Dans Imports, localiser `strcmp`.  
2. Suivre la XREF → on arrive dans la fonction qui appelle `strcmp` (c'est `check_license`).  
3. Remonter via les XREF de cette fonction → on trouve `main`.

### Stratégie 3 : partir de l'entry point

Si même les chaînes étaient absentes ou obfusquées (ce qui n'est pas le cas ici, mais arrive en pratique) :

1. Aller à l'entry point (`_start`, toujours présent car nécessaire au loader).  
2. `_start` appelle `__libc_start_main`, dont le premier argument (dans `RDI`) est l'adresse de `main`.  
3. Suivre cette adresse → on est dans `main`.  
4. De là, descendre dans les appels comme précédemment.

### Renommage manuel

Une fois les fonctions identifiées, on les renomme manuellement dans Ghidra (clic droit sur le nom → **Rename Function**) pour retrouver un état lisible :

```
FUN_001014e1  →  main  
FUN_001013d1  →  check_license  
FUN_00101229  →  compute_hash  
FUN_001012d8  →  derive_key  
FUN_00101358  →  format_key  
FUN_00101209  →  rotate_left  
FUN_00101460  →  read_line  
```

Après ce renommage, le decompiler produit un pseudo-C presque identique à celui de la variante avec symboles. C'est la puissance de l'annotation manuelle : en investissant quelques minutes dans le renommage, on transforme un binaire strippé opaque en un code lisible et documenté.

---

## Synthèse

À ce stade de l'analyse, la routine de vérification est entièrement localisée et sa structure comprise :

| Élément | Adresse (offset) | Rôle |  
|---|---|---|  
| `main` | `0x14e1` | Point d'entrée utilisateur, lit username et clé, appelle `check_license` |  
| `check_license` | `0x13d1` | Orchestre hash → dérivation → formatage → `strcmp` |  
| `compute_hash` | `0x1229` | Transforme le username en hash 32 bits |  
| `derive_key` | `0x12d8` | Dérive 4 groupes 16 bits depuis le hash |  
| `format_key` | `0x1358` | Produit la chaîne `XXXX-XXXX-XXXX-XXXX` attendue |  
| `strcmp@plt` | via PLT | Compare clé attendue et clé saisie |  
| Saut conditionnel (`main`) | `0x15dd` | `JZ` → échec si `check_license` retourne 0 |  
| Saut conditionnel (`check_license`) | `0x143c` | `JNE` → retour 0 si `strcmp` retourne non-zéro |

Les deux sauts conditionnels encadrés ci-dessus sont les cibles de la section suivante (21.4), où l'on analysera en détail la mécanique des `JZ`/`JNE` et leur rôle dans l'embranchement entre succès et échec.

⏭️ [Comprendre les sauts conditionnels (`jz`/`jnz`) dans le contexte du crackme](/21-keygenme/04-sauts-conditionnels-crackme.md)
