🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 33.3 — Reconnaître les patterns Rust : `Option`, `Result`, `match`, panics

> 🔍 Quand les symboles sont absents, la capacité à reconnaître les constructions idiomatiques de Rust directement dans l'assembleur devient l'atout principal de l'analyste. Cette section catalogue les patterns les plus fréquents — ceux que vous rencontrerez dans pratiquement tous les binaires Rust — et vous apprend à les identifier sans hésitation.

---

## Le principe fondamental : les enums Rust sont des tagged unions

Avant de plonger dans l'assembleur, il faut comprendre le modèle mémoire sous-jacent. En Rust, `Option<T>`, `Result<T, E>`, et tous les `enum` utilisateur partagent la même représentation en mémoire : une **tagged union** (ou union discriminée).

```rust
enum Option<T> {
    None,    // discriminant = 0
    Some(T), // discriminant = 1
}

enum Result<T, E> {
    Ok(T),   // discriminant = 0
    Err(E),  // discriminant = 1
}
```

En mémoire, le compilateur alloue :

```
┌────────────────┬──────────────────────────┐
│  Discriminant  │  Payload (T ou E)        │
│  (tag)         │                          │
│  1 à 8 octets  │  taille du plus grand    │
│                │  variant                 │
└────────────────┴──────────────────────────┘
```

Le discriminant (ou « tag ») est un entier qui indique quel variant est actif. Sa taille dépend du nombre de variants : 1 octet suffit pour `Option` (2 variants) et `Result` (2 variants), mais un `enum` à plus de 256 variants utiliserait 2 octets, etc.

> 💡 **L'optimisation de niche (niche optimization).** Le compilateur Rust exploite les valeurs invalides d'un type pour y loger le discriminant. Par exemple, `Option<&T>` n'a **pas** de tag séparé : `None` est représenté par un pointeur nul (`0x0`), et `Some(&T)` par le pointeur non nul lui-même. Le tout tient dans 8 octets au lieu de 16. De même, `Option<NonZeroU32>` encode `None` comme la valeur `0`. En RE, cela signifie qu'un test sur `Option<&T>` se traduit par un simple `test rdi, rdi` / `jz` — pas de lecture de tag explicite. C'est un pattern extrêmement courant qu'il faut savoir reconnaître.

---

## Pattern 1 : `Option<T>` et le test de discriminant

### Cas général (pas de niche optimization)

Pour un `Option<u32>` par exemple, le compilateur produit un layout de 8 octets : 4 octets de tag + 4 octets de payload (ou l'inverse, selon l'alignement). Le test du variant ressemble à ceci en `-O0` :

```nasm
; Option<u32> stocké sur la pile à [rbp-0x10]
; Layout : [rbp-0x10] = tag (4 octets), [rbp-0x0C] = payload (4 octets)

    mov     eax, dword [rbp-0x10]     ; Charge le discriminant
    cmp     eax, 0                     ; 0 = None
    je      .handle_none
    ; Ici le variant est Some — on accède au payload :
    mov     ecx, dword [rbp-0x0C]     ; Charge la valeur u32 du Some
    ; ... utilisation de ecx ...
    jmp     .after_match

.handle_none:
    ; ... traitement du None ...

.after_match:
```

Le pattern reconnaissable : **lecture d'un entier, comparaison à 0 (ou 1), branchement conditionnel, puis accès à la mémoire adjacente pour le payload**. C'est la signature d'un `match` sur un `Option` ou tout enum à deux variants.

En `-O2` / `-O3`, LLVM simplifie souvent en fusionnant le test et le branchement :

```nasm
    cmp     dword [rbp-0x10], 0
    je      .handle_none
    mov     ecx, dword [rbp-0x0C]
```

### Cas avec niche optimization (`Option<&T>`, `Option<Box<T>>`)

Pour les types dont la valeur `0` est invalide (références, `Box`, `NonZero*`), le discriminant est le pointeur lui-même :

```nasm
; Option<&str> — le fat pointer (ptr, len) est sur la pile
; None = ptr est nul

    mov     rax, qword [rbp-0x10]     ; Charge le pointeur
    test    rax, rax                   ; Test si nul
    jz      .is_none
    ; Some — rax contient le pointeur valide
    mov     rcx, qword [rbp-0x08]     ; Charge la longueur du &str
    ; ... utilisation du &str ...
```

Ce pattern `test reg, reg` / `jz` est omniprésent dans le code Rust. Quand vous voyez un pointeur testé contre zéro suivi d'un branchement vers du code de panique ou un chemin d'erreur, c'est presque certainement un `Option` avec niche optimization.

> 🔑 **Astuce RE** : dans Ghidra, cherchez les XREF vers les fonctions de panique. Chaque XREF est potentiellement un `unwrap()` ou un `expect()` sur un `Option` ou `Result`. Remonter la chaîne d'XREF permet de localiser rapidement les points de décision critiques du programme.

---

## Pattern 2 : `unwrap()` — le branchement vers la panique

L'appel `.unwrap()` sur un `Option` ou un `Result` se traduit en assembleur par un test du discriminant suivi d'un branchement vers une fonction de panique en cas d'échec.

### `Option::unwrap()`

```rust
let value = some_option.unwrap();
```

Produit en assembleur (version optimisée) :

```nasm
    test    rax, rax                    ; Teste le discriminant / pointeur
    jz      .panic_unwrap_none          ; Si None → panique
    ; Suite normale avec la valeur déballée dans rax
    ; ...

.panic_unwrap_none:
    ; Prépare les arguments pour le message de panique
    lea     rdi, [rip + .Lstr_unwrap_msg]  ; "called `Option::unwrap()` on a `None` value"
    lea     rsi, [rip + .Lstr_location]     ; "src/main.rs:42:17"
    call    core::panicking::panic          ; Ne retourne jamais
    ud2                                     ; Instruction illégale (hint: unreachable)
```

Les éléments reconnaissables :

1. **Le test** (`test` / `cmp`) immédiatement avant un branchement conditionnel.  
2. **L'appel à `core::panicking::panic`** (ou `core::panicking::panic_fmt` pour les messages formatés). Même sur un binaire strippé, la chaîne `"called \`Option::unwrap()\` on a \`None\` value"` est dans `.rodata` et identifiable via `strings`.  
3. **L'instruction `ud2`** après l'appel de panique. LLVM insère cette instruction illégale comme marqueur de code inatteignable (le `panic` ne retourne jamais). C'est un indice visuel très fiable : si vous voyez `ud2` après un `call`, le `call` en question est presque certainement une fonction qui ne retourne pas (`noreturn`).

### `Result::unwrap()`

Le pattern est identique, mais le message de panique diffère :

```nasm
    cmp     byte [rbp-0x20], 0          ; Discriminant de Result : 0 = Ok, 1 = Err
    jne     .panic_unwrap_err           ; Si Err → panique
    ; Suite normale avec la valeur Ok
    ; ...

.panic_unwrap_err:
    lea     rdi, [rip + .Lstr_result_msg]  ; "called `Result::unwrap()` on an `Err` value: ..."
    ; ...
    call    core::panicking::panic_fmt
    ud2
```

### `expect()` — la variante avec message custom

La méthode `.expect("message")` produit le même pattern que `.unwrap()`, mais le message dans `.rodata` est celui fourni par le développeur :

```nasm
    lea     rdi, [rip + .Lstr_custom]   ; "Impossible de parser le fichier config"
```

Ce message custom est un indice supplémentaire pour l'analyste : il décrit souvent l'intention du développeur à cet endroit du code.

---

## Pattern 3 : l'opérateur `?` (propagation d'erreur)

L'opérateur `?` est le sucre syntaxique le plus utilisé en Rust pour propager les erreurs. Il se traduit par un test du discriminant de `Result` suivi d'un retour anticipé de la fonction courante avec la valeur `Err`.

```rust
fn parse_input(s: &str) -> Result<u32, String> {
    let value = s.parse::<u32>().map_err(|e| e.to_string())?;
    Ok(value * 2)
}
```

En assembleur, le `?` produit :

```nasm
    ; Retour de s.parse::<u32>() — Result<u32, ParseIntError> dans [rsp+...]
    cmp     byte [rsp+0x20], 0           ; Test du discriminant : 0 = Ok, 1 = Err
    jne     .propagate_error             ; Si Err → propager

    ; Chemin Ok : extraire la valeur et continuer
    mov     eax, dword [rsp+0x24]        ; Payload Ok (u32)
    shl     eax, 1                       ; value * 2
    ; Construire le Result::Ok de retour
    mov     byte [rsp+0x30], 0           ; Tag = Ok
    mov     dword [rsp+0x34], eax        ; Payload = value * 2
    jmp     .return

.propagate_error:
    ; Chemin Err : convertir l'erreur et la propager
    ; ... appel à map_err / to_string ...
    mov     byte [rsp+0x30], 1           ; Tag = Err
    ; Copier le payload Err dans le Result de retour
    ; ...

.return:
    ; Le Result est dans [rsp+0x30], retour à l'appelant
    ret
```

La différence avec `unwrap()` est cruciale : **il n'y a pas d'appel à `panic`**. Le chemin d'erreur retourne proprement à l'appelant en construisant un `Result::Err`. C'est un retour anticipé, pas un crash.

> 🔑 **Astuce RE** : quand vous voyez un test de discriminant suivi d'un `jmp` vers l'épilogue de la fonction (pas vers un `call panic`), c'est probablement un `?`. Si le saut mène à un appel de panique, c'est un `unwrap()` ou `expect()`. Cette distinction vous dit si le développeur a choisi de gérer l'erreur proprement ou de crasher en cas d'échec.

---

## Pattern 4 : `match` sur un enum utilisateur

Notre crackme définit un `enum LicenseType` à trois variants :

```rust
enum LicenseType {
    Trial { days_left: u32 },       // discriminant = 0
    Standard { seats: u32 },        // discriminant = 1
    Enterprise { seats: u32, support: bool },  // discriminant = 2
}
```

Le `match` exhaustif sur cet enum se traduit par une **cascade de comparaisons** ou une **table de sauts** selon le nombre de variants et le niveau d'optimisation.

### Cascade de comparaisons (peu de variants, `-O0` à `-O2`)

```nasm
    ; Le discriminant de LicenseType est dans eax (ou sur la pile)
    movzx   eax, byte [rbp-0x28]        ; Charge le tag (1 octet suffit pour 3 variants)
    
    test    eax, eax                     ; == 0 ? (Trial)
    je      .match_trial
    
    cmp     eax, 1                       ; == 1 ? (Standard)
    je      .match_standard
    
    cmp     eax, 2                       ; == 2 ? (Enterprise)
    je      .match_enterprise
    
    ; Si on arrive ici, c'est unreachable (le match est exhaustif)
    ud2

.match_trial:
    ; Accès à days_left : dword [rbp-0x24]
    mov     ecx, dword [rbp-0x24]
    test    ecx, ecx
    je      .trial_expired               ; days_left == 0
    mov     eax, 5                       ; return 5
    jmp     .match_end

.trial_expired:
    xor     eax, eax                     ; return 0
    jmp     .match_end

.match_standard:
    ; Accès à seats : dword [rbp-0x24]
    mov     eax, dword [rbp-0x24]
    add     eax, 10                      ; return 10 + seats
    jmp     .match_end

.match_enterprise:
    ; Accès à seats et support
    mov     eax, dword [rbp-0x24]        ; seats
    shl     eax, 1                       ; seats * 2
    add     eax, 50                      ; 50 + seats * 2
    movzx   ecx, byte [rbp-0x20]        ; support (bool)
    test    ecx, ecx
    je      .no_support
    add     eax, 100                     ; + 100 si support
.no_support:
    ; eax contient le résultat

.match_end:
    ; Suite du code avec le résultat dans eax
```

Le pattern reconnaissable : **une séquence de `cmp` / `je` sur le même registre ou emplacement mémoire, avec des valeurs consécutives (0, 1, 2…)**. Chaque branchement mène à un bloc qui accède au payload à un offset fixe depuis la base de l'enum. C'est exactement la structure d'un `switch` en C, et les outils de décompilation le reconstruisent généralement bien.

### Table de sauts (nombreux variants, `-O2` / `-O3`)

Quand l'enum a suffisamment de variants (typiquement ≥ 4), LLVM peut choisir d'émettre une table de sauts plutôt qu'une cascade de comparaisons :

```nasm
    movzx   eax, byte [rbp-0x28]        ; Charge le tag
    cmp     eax, 5                       ; Borne supérieure (nombre de variants - 1)
    ja      .unreachable                 ; Défense contre les valeurs invalides
    lea     rcx, [rip + .Ljump_table]   ; Adresse de la table de sauts
    movsxd  rax, dword [rcx + rax*4]    ; Offset depuis la table
    add     rax, rcx                     ; Adresse cible = table + offset
    jmp     rax                          ; Saut indirect

.Ljump_table:
    .long   .variant_0 - .Ljump_table
    .long   .variant_1 - .Ljump_table
    .long   .variant_2 - .Ljump_table
    ; ...
```

Ce pattern — `lea` vers une table, `movsxd` indexé par le tag, `add`, `jmp` indirect — est le même que celui généré pour un `switch` C/C++ optimisé. Ghidra et IDA le reconstituent généralement en `switch-case` dans le décompilateur.

### Match sur des ranges

Notre crackme utilise un `match` avec des plages de valeurs dans `determine_license` :

```rust
match value {
    0x0000..=0x00FF => LicenseType::Trial { days_left: 30 },
    0x0100..=0x0FFF => LicenseType::Standard { ... },
    0x1000..=0xFFFF => LicenseType::Enterprise { ... },
}
```

Ce pattern se traduit par des **comparaisons bornées** :

```nasm
    movzx   eax, word [rbp-0x0A]        ; value (u16)
    
    cmp     eax, 0xFF
    jbe     .range_trial                 ; 0x0000..=0x00FF
    
    cmp     eax, 0xFFF
    jbe     .range_standard              ; 0x0100..=0x0FFF (implicite : > 0xFF)
    
    ; Sinon : 0x1000..=0xFFFF → Enterprise
    jmp     .range_enterprise
```

Les instructions `jbe` (jump if below or equal, comparaison non signée) ou `jle` (signée) sur des constantes immédiates trahissent un match sur des ranges. L'analyste peut reconstituer les bornes directement depuis les valeurs immédiates.

---

## Pattern 5 : les panics

Les panics sont omniprésentes dans le code Rust — même le code qui ne les appelle jamais explicitement en contient, car le compilateur insère des vérifications de bornes sur les accès aux slices, des tests de débordement en mode debug, et du code d'`unwrap` implicite.

### Les fonctions de panique

Il existe plusieurs fonctions de panique dans la stdlib Rust. Les plus courantes, classées par fréquence d'apparition :

| Fonction | Déclenchée par | Message typique en `.rodata` |  
|---|---|---|  
| `core::panicking::panic` | `unwrap()` sur `None`, `panic!("message")` | Le message littéral ou le message standard d'unwrap |  
| `core::panicking::panic_fmt` | `panic!("format {}", arg)`, `expect()`, `unwrap()` sur `Err` | Message formaté avec arguments |  
| `core::panicking::panic_bounds_check` | Accès `array[i]` avec `i` hors bornes | `"index out of bounds: the len is {} but the index is {}"` |  
| `core::slice::index::slice_index_order_fail` | Slice `&arr[a..b]` avec `a > b` | `"slice index starts at {} but ends at {}"` |

Sur un binaire strippé, ces fonctions n'ont plus de nom, mais les chaînes associées dans `.rodata` sont toujours là. Chercher ces chaînes avec `strings` puis remonter les XREF dans le désassembleur est la méthode la plus fiable pour localiser les points de panique.

### Reconnaître un appel de panique en assembleur

Toutes les fonctions de panique partagent un trait commun : elles sont marquées `#[cold]` et `-> !` (divergentes, ne retournent jamais). LLVM les place dans des blocs de code « froids » en fin de fonction, et insère systématiquement `ud2` après l'appel.

```nasm
; Bloc principal (chemin "chaud")
    test    rax, rax
    jz      .cold_path              ; Branchement vers le bloc froid
    ; ... suite normale ...
    ret

; Bloc froid (panique) — souvent placé après le `ret` de la fonction
.cold_path:
    lea     rdi, [rip + .Lpanic_msg]
    lea     rsi, [rip + .Lpanic_loc]
    call    _some_panic_function
    ud2
```

La structure est toujours la même :

1. Un branchement conditionnel (`jz`, `jne`, `ja`…) vers un bloc en fin de fonction.  
2. Le bloc charge l'adresse d'un message (`.Lpanic_msg`) et d'une localisation source (`.Lpanic_loc`) dans les registres d'arguments.  
3. Un `call` vers la fonction de panique.  
4. `ud2` immédiatement après.

> 💡 **Le `ud2` comme signature visuelle.** Dans un listing assembleur, les instructions `ud2` se repèrent facilement. Chaque `ud2` signale la fin d'un chemin de panique. Si vous voyez beaucoup de `ud2` dispersés dans une fonction, c'est que la fonction contient de nombreuses vérifications (bounds checks, unwraps, etc.). C'est un indicateur de la « densité de validation » du code Rust.

### La structure de localisation de panique

La localisation source passée en argument aux fonctions de panique est une structure `core::panic::Location` stockée dans `.rodata` :

```
struct Location {
    file: &str,     // (pointeur, longueur) → "src/main.rs"
    line: u32,      // 42
    column: u32,    // 17
}
```

En mémoire, cela donne une séquence de 24 octets dans `.rodata` : pointeur vers la chaîne du fichier (8 octets), longueur de la chaîne (8 octets), numéro de ligne (4 octets), numéro de colonne (4 octets).

Dans Ghidra, définir un type `PanicLocation` avec cette structure et l'appliquer à chaque référence permet de décoder automatiquement les localisations source — un gain de temps considérable sur les gros binaires.

### `panic = "abort"` vs `panic = "unwind"`

Le comportement en cas de panique dépend du profil de compilation :

Avec `panic = "unwind"` (défaut), la panique déroule la pile en appelant les destructeurs (`Drop`) de chaque variable locale. Cela nécessite les tables `.eh_frame` et génère du code supplémentaire dans chaque fonction (les « landing pads » de gestion d'exception, similaires au C++). L'avantage pour l'analyste : davantage de code structurel visible.

Avec `panic = "abort"`, la panique appelle directement `abort()` (qui déclenche `SIGABRT`). Pas de déroulement de pile, pas de destructeurs, pas de landing pads. Le binaire est plus petit et plus simple, mais contient moins d'indices structurels. C'est le profil utilisé par notre variante `crackme_rust_strip`.

---

## Pattern 6 : les bounds checks (vérifications de bornes)

Rust vérifie systématiquement que les accès aux slices et aux vecteurs sont dans les bornes. Cet accès anodin en Rust :

```rust
let x = my_vec[i];
```

Produit en assembleur :

```nasm
    ; rdi = pointeur vers les données du Vec
    ; rsi = longueur du Vec
    ; rcx = index i

    cmp     rcx, rsi                  ; i >= len ?
    jae     .bounds_check_failed      ; Si oui → panique (comparaison non signée)
    mov     eax, dword [rdi + rcx*4]  ; Accès valide : charge l'élément
    ; ...

.bounds_check_failed:
    ; rcx = index, rsi = len → passés comme arguments au message de panique
    mov     rdi, rcx
    mov     rsi, rsi
    call    core::panicking::panic_bounds_check
    ud2
```

Le pattern `cmp` + `jae` (jump if above or equal, non signé) avant chaque accès indexé est la signature des bounds checks. En `-O2`/`-O3`, LLVM élimine parfois ces vérifications quand il peut prouver que l'index est toujours valide (par exemple, dans une boucle `for i in 0..vec.len()`). Mais dans le cas général, chaque accès indexé génère ce test.

> 🔑 **Astuce RE** : la densité des bounds checks dans une fonction vous indique qu'elle manipule des collections (slices, `Vec`, `String`). Si vous voyez un bounds check suivi d'un accès mémoire à `[base + index * N]`, la valeur `N` vous donne la taille de chaque élément, ce qui aide à reconstruire le type : `N = 1` pour `u8`/`i8`/`Vec<u8>`, `N = 4` pour `u32`/`i32`/`f32`, `N = 8` pour `u64`/`f64`/pointeurs, etc.

---

## Pattern 7 : les closures et itérateurs

Rust encourage l'utilisation d'itérateurs et de closures au lieu des boucles indexées. Ce code idiomatique :

```rust
let sum: u32 = values.iter().filter(|&&x| x > 10).map(|&x| x * 2).sum();
```

Se compile en code souvent **aussi efficace qu'une boucle C** grâce à l'inlining agressif de LLVM. Après optimisation, la chaîne d'itérateurs est fusionnée en une seule boucle :

```nasm
    ; Boucle fusionnée — plus aucune trace de filter/map/sum individuels
    xor     eax, eax                  ; sum = 0
    xor     ecx, ecx                  ; i = 0
.loop:
    cmp     ecx, edx                  ; i < len ?
    jge     .done
    mov     esi, dword [rdi + rcx*4]  ; charge values[i]
    cmp     esi, 10                   ; filter : x > 10 ?
    jle     .skip
    lea     esi, [rsi + rsi]          ; map : x * 2
    add     eax, esi                  ; sum += x * 2
.skip:
    inc     ecx
    jmp     .loop
.done:
```

En `-O0`, la situation est très différente : chaque adaptateur d'itérateur (`filter`, `map`, `sum`) est une fonction séparée, avec des structures intermédiaires allouées sur la pile. Le code est beaucoup plus verbeux mais plus facile à suivre, car chaque étape est distincte.

> 💡 **Conséquence RE** : en mode optimisé, ne cherchez pas des appels à `Iterator::filter` ou `Iterator::map` — ils ont été inlinés. Ce que vous verrez est une boucle compacte. En mode debug, vous verrez des dizaines de fonctions d'itérateurs avec des noms manglés explicites — c'est plus lisible mais plus verbeux.

---

## Pattern 8 : le trait object et le dispatch dynamique

Notre crackme utilise `Box<dyn Validator>`, un trait object qui passe par un dispatch dynamique (vtable). En mémoire, un `Box<dyn Validator>` est un fat pointer de 16 octets :

```
┌────────────────┬────────────────┐
│  data_ptr      │  vtable_ptr    │
│  (8 octets)    │  (8 octets)    │
└────────────────┴────────────────┘
```

Le `vtable_ptr` pointe vers une table en `.rodata` qui contient :

```
┌────────────────────────────────────┐
│  drop_fn       (destructeur)       │  offset 0x00
│  size           (taille du type)   │  offset 0x08
│  align          (alignement)       │  offset 0x10
│  method_1       (Validator::name)  │  offset 0x18
│  method_2       (Validator::validate) offset 0x20
└────────────────────────────────────┘
```

Les trois premiers champs (`drop`, `size`, `align`) sont toujours présents dans toute vtable de trait Rust. Les méthodes du trait suivent dans l'ordre de leur déclaration.

L'appel `validator.validate(serial)` via le trait object se traduit par :

```nasm
    ; rax = pointeur vers le fat pointer (data_ptr, vtable_ptr)
    mov     rdi, qword [rax]          ; data_ptr → premier argument (self)
    mov     rcx, qword [rax+8]        ; vtable_ptr
    mov     rsi, qword [rbp-0x18]     ; serial (deuxième argument)
    mov     rdx, qword [rbp-0x10]     ; longueur de serial
    call    qword [rcx+0x20]          ; Appel indirect via la vtable (offset 0x20 = validate)
```

Le pattern reconnaissable : **un `call` indirect via un registre indexé** (`call [rcx+offset]`), où le registre a été chargé depuis un pointeur stocké à côté d'un autre pointeur (le fat pointer). L'offset constant (`0x20` dans cet exemple) indique quelle méthode du trait est appelée.

> 🔑 **Différence avec le C++** : les vtables C++ contiennent uniquement les méthodes virtuelles. Les vtables Rust contiennent en plus `drop`, `size` et `align` en en-tête. Si vous voyez une vtable dont les trois premiers slots sont un pointeur de fonction, un entier « raisonnable » (~quelques octets à quelques Ko), et une petite puissance de 2 (1, 2, 4, 8, 16…), c'est presque certainement une vtable de trait Rust.

---

## Récapitulatif visuel des patterns

| Pattern assembleur | Construction Rust probable |  
|---|---|  
| `test reg, reg` / `jz` (sur un pointeur) | `Option<&T>` ou `Option<Box<T>>` avec niche optimization |  
| `cmp byte/dword [mem], 0` / `je`\|`jne` | `match` sur `Option<T>` ou `Result<T, E>` (test de discriminant) |  
| Test + `jz` → `call panic` + `ud2` | `.unwrap()` ou `.expect()` |  
| Test + `jne` → copie du payload Err + `jmp` épilogue | Opérateur `?` (propagation d'erreur) |  
| `cmp reg, imm` / `jae` → `call panic_bounds_check` + `ud2` | Accès indexé `slice[i]` ou `vec[i]` |  
| Cascade de `cmp` / `je` sur valeurs 0, 1, 2… | `match` sur un enum à peu de variants |  
| `lea` + `movsxd` + `jmp [reg]` (table de sauts) | `match` sur un enum à nombreux variants (optimisé) |  
| `cmp` + `jbe` / `ja` sur des constantes | `match` sur des ranges de valeurs |  
| `call [reg+offset]` avec chargement préalable d'un fat pointer | Appel via un trait object (`dyn Trait`) |  
| `ud2` | Fin d'un chemin qui ne retourne jamais (panique, `process::exit`) |

---

> **Section suivante : 33.4 — Strings en Rust : `&str` vs `String` en mémoire (pas de null terminator)** — nous verrons comment Rust représente les chaînes de caractères et pourquoi les outils classiques comme `strings` donnent des résultats incomplets.

⏭️ [Strings en Rust : `&str` vs `String` en mémoire (pas de null terminator)](/33-re-rust/04-strings-rust-memoire.md)
