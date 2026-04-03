🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.4 — Gestion des exceptions (`.eh_frame`, `.gcc_except_table`, `__cxa_throw`)

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Pourquoi les exceptions sont complexes en RE

Les exceptions C++ sont le mécanisme le plus difficile à suivre dans un désassemblage. Contrairement aux `if`/`else` et aux boucles, qui se traduisent par des sauts conditionnels visibles dans le flux de contrôle, les exceptions créent un **flux de contrôle invisible** : quand un `throw` est exécuté, l'exécution saute directement au `catch` correspondant, potentiellement plusieurs frames de pile plus haut, en déroulant la pile et en appelant les destructeurs de tous les objets locaux au passage. Rien de tout cela n'apparaît comme des instructions `jmp` ou `call` dans le listing assembleur.

Ce flux invisible est orchestré par deux acteurs :

1. **Le runtime C++** (fonctions `__cxa_*` dans `libstdc++` et `libgcc`) qui implémente la mécanique de lancement, propagation et capture des exceptions.  
2. **Les métadonnées générées par le compilateur** (sections `.eh_frame` et `.gcc_except_table`) qui décrivent, pour chaque instruction du programme, quoi faire si une exception survient : quel destructeur appeler, quel `catch` correspond, comment restaurer les registres.

Pour le reverse engineer, la conséquence est double. D'une part, le flux `throw` → `catch` est pratiquement invisible dans un désassembleur classique (objdump ne montre rien de ce mécanisme). D'autre part, les métadonnées d'exceptions contiennent des informations précieuses : elles indiquent quelles fonctions peuvent lancer des exceptions, quels types sont interceptés, et quelles plages de code sont protégées par un `try`.

## Le modèle « zero-cost » de GCC

GCC utilise le modèle dit **zero-cost exception handling** (aussi appelé *table-driven*). Le principe est que le chemin normal d'exécution (quand aucune exception n'est lancée) ne paie **aucun coût** : pas d'instructions supplémentaires, pas de registres réservés, pas de comparaisons. Tout le surcoût est transféré sur le chemin exceptionnel et sur les métadonnées statiques stockées dans le binaire.

Concrètement :

- **Pas de `try` visible en assembleur.** Un bloc `try` ne génère aucune instruction. Il est défini uniquement par une plage d'adresses dans les tables de métadonnées.  
- **Le `throw` est un appel de fonction.** Il appelle `__cxa_allocate_exception` puis `__cxa_throw`, deux fonctions du runtime.  
- **Le `catch` est un « landing pad ».** C'est un bloc de code dans la fonction qui n'est pas atteignable par le flux de contrôle normal. Il est référencé uniquement par les tables de métadonnées.  
- **Le déroulage de pile (stack unwinding)** est piloté par les tables `.eh_frame`, qui décrivent comment restaurer les registres et le pointeur de pile frame par frame.

Ce modèle contraste avec le modèle plus ancien de Windows (SEH, *Structured Exception Handling*) où des structures sont explicitement empilées sur la stack à l'entrée de chaque `try`.

## Les fonctions runtime `__cxa_*`

Le runtime C++ de GCC (`libstdc++` et `libgcc`) fournit un ensemble de fonctions dont le préfixe `__cxa_` identifie le C++ ABI. Voici celles qu'un reverse engineer rencontre le plus souvent.

### `__cxa_allocate_exception`

```c
void* __cxa_allocate_exception(size_t thrown_size);
```

Alloue la mémoire pour l'objet exception sur un heap dédié aux exceptions (pas le heap général). L'argument `thrown_size` est la taille de l'objet exception. Le pointeur retourné est l'espace où l'objet exception sera construit.

En assembleur :

```nasm
mov    edi, 48                        ; taille de l'objet AppException  
call   __cxa_allocate_exception@plt  
; rax = pointeur vers la mémoire allouée pour l'exception
```

> 💡 **En RE :** la valeur passée dans `edi` est la taille de la classe d'exception. Cela vous donne directement le `sizeof` de l'objet lancé, ce qui aide à reconstruire la structure de la classe d'exception.

### `__cxa_throw`

```c
void __cxa_throw(void* thrown_exception, std::type_info* tinfo, void (*dest)(void*));
```

Lance l'exception. Cette fonction **ne retourne jamais** — elle déclenche le déroulage de pile. Ses trois arguments sont :

| Argument | Registre | Signification |  
|----------|----------|---------------|  
| `thrown_exception` | `rdi` | Pointeur vers l'objet exception (retourné par `__cxa_allocate_exception`) |  
| `tinfo` | `rsi` | Pointeur vers la structure `_ZTI` du type de l'exception |  
| `dest` | `rdx` | Pointeur vers le destructeur de l'objet exception (ou `nullptr`) |

En assembleur, la séquence complète d'un `throw AppException("msg", 42)` ressemble à :

```nasm
; 1. Allouer l'espace pour l'exception
mov    edi, 48  
call   __cxa_allocate_exception@plt  
mov    rbx, rax                       ; sauvegarder le pointeur  

; 2. Construire l'objet exception dans l'espace alloué
mov    rdi, rbx                       ; this = espace alloué  
lea    rsi, [rip+.LC_msg]            ; "msg"  
mov    edx, 42                        ; code  
call   AppException::AppException(std::string const&, int)  

; 3. Lancer l'exception
mov    rdi, rbx                       ; exception object  
lea    rsi, [rip+_ZTI12AppException]  ; typeinfo → identifie le type  
lea    rdx, [rip+_ZN12AppExceptionD1Ev] ; destructeur de l'exception  
call   __cxa_throw@plt  
; ← le flux ne revient JAMAIS ici
```

> 💡 **Pattern RE crucial :** la séquence `__cxa_allocate_exception` → construction → `__cxa_throw` identifie un `throw`. Le deuxième argument de `__cxa_throw` (dans `rsi`) pointe vers la `_ZTI` qui donne le type exact de l'exception lancée. Le troisième argument (dans `rdx`) est le destructeur de la classe, ce qui fournit encore un lien vers la hiérarchie de classes.

### `__cxa_begin_catch`

```c
void* __cxa_begin_catch(void* exception_object);
```

Appelée au début d'un bloc `catch`. Elle marque l'exception comme « en cours de traitement » et retourne un pointeur vers l'objet exception. Ce pointeur est ensuite utilisé dans le corps du `catch` pour accéder aux membres de l'exception (par exemple `e.what()`, `e.code()`).

```nasm
; Début d'un landing pad (catch)
mov    rdi, rax                       ; rax contient l'exception propagée  
call   __cxa_begin_catch@plt  
; rax = pointeur vers l'objet exception
; Le code du catch suit...
mov    rdi, rax  
call   AppException::what() const     ; utiliser l'exception  
```

### `__cxa_end_catch`

```c
void __cxa_end_catch();
```

Appelée à la fin d'un bloc `catch`. Elle détruit l'objet exception (si le compteur de références atteint 0) et nettoie l'état du runtime. Si elle n'est pas appelée, le runtime fuit des exceptions.

```nasm
; Fin du bloc catch
call   __cxa_end_catch@plt
; L'exécution continue normalement après le try/catch
```

### `__cxa_rethrow`

```c
void __cxa_rethrow();
```

Relance l'exception actuellement en cours de traitement (le `throw;` sans argument). Ne retourne jamais.

### `__cxa_get_exception_ptr`

```c
void* __cxa_get_exception_ptr(void* exception_object);
```

Retourne un pointeur vers l'objet exception sans modifier l'état du runtime. Utilisée dans certains cas de copie d'exception.

### `_Unwind_Resume`

```c
void _Unwind_Resume(struct _Unwind_Exception* exception_object);
```

Fonction de `libgcc` (pas de `libstdc++`) qui reprend le déroulage de pile. Elle est appelée quand un landing pad (code de nettoyage ou `catch`) ne peut pas gérer l'exception et doit la propager plus haut dans la pile. En assembleur, vous la verrez après un appel à des destructeurs dans un cleanup landing pad :

```nasm
; Cleanup landing pad : détruire les objets locaux puis propager
mov    rdi, rax                       ; objet exception
; ... appeler les destructeurs des variables locales ...
call   _Unwind_Resume@plt
; ← ne retourne jamais
```

> 💡 **En RE :** `_Unwind_Resume` marque la fin d'un landing pad de nettoyage (pas un `catch`, mais un cleanup qui détruit les objets locaux). Si vous voyez `_Unwind_Resume` dans une fonction, la fonction contient des objets à durée de vie automatique qui nécessitent un nettoyage en cas d'exception (typiquement des `std::string`, `std::vector`, smart pointers, etc.).

## Vue d'ensemble du flux d'une exception

Voici le parcours complet d'une exception depuis le `throw` jusqu'au `catch`, tel que le runtime GCC l'exécute :

```
Code source :                      Ce qui se passe dans le binaire :

                                   ┌────────────────────────────────┐
throw AppException("x", 1);  ──→   │ __cxa_allocate_exception(48)   │
                                   │ AppException::AppException()   │
                                   │ __cxa_throw(obj, _ZTI, dtor)   │
                                   └───────────┬────────────────────┘
                                               │ (ne retourne pas)
                                               ▼
                                   ┌────────────────────────────────┐
                                   │ Phase 1 : Search               │
                                   │ _Unwind_RaiseException()       │
                                   │ parcourt .eh_frame pour        │
                                   │ remonter les frames            │
                                   │ consulte .gcc_except_table     │
                                   │ pour trouver un catch qui      │
                                   │ matche le type _ZTI            │
                                   └───────────┬────────────────────┘
                                               │
                                               ▼
                                   ┌────────────────────────────────┐
                                   │ Phase 2 : Cleanup              │
                                   │ redéroule la pile frame par    │
                                   │ frame, exécute les landing     │
                                   │ pads de nettoyage (dtors),     │
                                   │ restaure les registres         │
                                   └───────────┬────────────────────┘
                                               │
                                               ▼
                                   ┌────────────────────────────────┐
catch (const AppException& e)  ──→ │ Landing pad du catch :         │
                                   │ __cxa_begin_catch(obj)         │
                                   │ ... corps du catch ...         │
                                   │ __cxa_end_catch()              │
                                   └────────────────────────────────┘
```

Le runtime procède en deux phases : d'abord une **phase de recherche** (search phase) qui remonte la pile sans la modifier pour trouver un handler compatible, puis une **phase de nettoyage** (cleanup phase) qui déroule effectivement la pile, appelle les destructeurs, et transfère le contrôle au landing pad du `catch`.

## La section `.eh_frame`

La section `.eh_frame` contient les informations de déroulage de pile (*unwind information*). Elle est encodée au format **DWARF Call Frame Information (CFI)**, le même format que celui utilisé par les débogueurs pour parcourir la pile d'appels. Elle est présente même en C (GCC l'utilise aussi pour `__attribute__((cleanup))` et pour les débogueurs) mais elle est indispensable en C++ pour les exceptions.

### Structure de `.eh_frame`

`.eh_frame` est composée de deux types d'entrées :

**CIE (Common Information Entry)** — partagée entre plusieurs fonctions ayant des propriétés de déroulage similaires. Contient :
- La version du format  
- L'augmentation string (indique des extensions, comme la présence d'un pointeur vers `.gcc_except_table`)  
- L'alignement des codes et des données  
- Le registre d'adresse de retour  
- Les instructions CFI initiales (état par défaut des registres au début d'une fonction)

**FDE (Frame Description Entry)** — une par fonction (ou par plage de code). Contient :
- L'adresse de début et la longueur de la plage de code décrite  
- Un pointeur vers la CIE parente  
- Un pointeur optionnel vers la **LSDA** (Language-Specific Data Area) dans `.gcc_except_table`  
- Les instructions CFI spécifiques à cette fonction (comment les registres évoluent instruction par instruction)

Les instructions CFI sont un mini-langage de bytecode qui décrit, pour chaque point du code, comment retrouver le pointeur de pile précédent et les registres sauvegardés. Le dérouleur (unwinder) interprète ces instructions pour remonter la pile frame par frame.

### Visualiser `.eh_frame`

```bash
# Afficher le contenu brut de .eh_frame
$ readelf --debug-dump=frames oop_O0

# Version plus lisible avec décodage des instructions CFI
$ readelf --debug-dump=frames-interp oop_O0
```

Un extrait typique de la sortie :

```
00000098 0000002c 0000009c FDE cie=00000000 pc=0000000000401a2c..0000000000401b14
  Augmentation data: Pointer to LSDA = 0x00403120
  DW_CFA_advance_loc: 1 to 0000000000401a2d
  DW_CFA_def_cfa_offset: 16
  DW_CFA_offset: r6 (rbp) at cfa-16
  DW_CFA_advance_loc: 3 to 0000000000401a30
  DW_CFA_def_cfa_register: r6 (rbp)
  ...
```

Les points importants pour le RE :

- La plage `pc=0x401a2c..0x401b14` identifie la fonction couverte.  
- **`Pointer to LSDA`** est le lien vers `.gcc_except_table` — si ce pointeur est non nul, la fonction a des `try/catch` ou des objets nécessitant un cleanup en cas d'exception.  
- Les instructions `DW_CFA_*` décrivent comment restaurer les registres. Elles ne sont généralement pas nécessaires pour le RE des exceptions, mais elles sont utilisées par GDB pour le backtrace.

> 💡 **En RE :** si une FDE a un pointeur LSDA non nul, la fonction correspondante contient des blocs `try/catch` ou des objets locaux avec destructeurs. Si le pointeur LSDA est nul (ou si la FDE n'a pas d'augmentation), la fonction ne gère pas les exceptions localement — si une exception traverse cette fonction, la pile est simplement déroulée sans action spéciale.

## La section `.gcc_except_table`

C'est ici que réside l'information la plus intéressante pour le reverse engineer. La `.gcc_except_table` (aussi appelée **LSDA**, *Language-Specific Data Area*) contient, pour chaque fonction qui gère des exceptions, les tables suivantes :

### Structure de la LSDA

Chaque LSDA commence par un header suivi de trois tables :

```
LSDA Header :
┌─────────────────────────────────────────────┐
│  @LPStart encoding (1 octet)                │  Comment les adresses de landing pad sont encodées
├─────────────────────────────────────────────┤
│  @LPStart (variable, si encoding ≠ omit)    │  Base des adresses de landing pad
├─────────────────────────────────────────────┤
│  @TType encoding (1 octet)                  │  Comment les types sont encodés
├─────────────────────────────────────────────┤
│  @TType base offset (ULEB128, si enc ≠ omit)│  Offset vers la fin de la type table
├─────────────────────────────────────────────┤
│  Call site encoding (1 octet)               │  Comment les entrées call site sont encodées
├─────────────────────────────────────────────┤
│  Call site table length (ULEB128)           │  Taille de la call site table en octets
├═════════════════════════════════════════════╡
│  Call Site Table                            │  (voir ci-dessous)
├═════════════════════════════════════════════╡
│  Action Table                               │  (voir ci-dessous)
├═════════════════════════════════════════════╡
│  Type Table (lu à l'envers)                 │  (voir ci-dessous)
└─────────────────────────────────────────────┘
```

### La Call Site Table

La call site table est la table principale. Chaque entrée décrit une plage d'instructions et ce qui doit se passer si une exception est lancée pendant l'exécution de cette plage :

| Champ | Encodage | Signification |  
|-------|----------|---------------|  
| `cs_start` | offset | Début de la plage (relatif au début de la fonction) |  
| `cs_len` | longueur | Longueur de la plage en octets |  
| `cs_lp` | offset | Adresse du **landing pad** (relatif à `@LPStart`), ou 0 s'il n'y a pas de handler |  
| `cs_action` | ULEB128 | Index dans l'action table (1-indexé), ou 0 pour un cleanup-only |

**Interprétation des champs :**

- Si `cs_lp == 0` : aucun landing pad pour cette plage. Si une exception survient ici, le déroulage continue vers le frame appelant.  
- Si `cs_lp != 0` et `cs_action == 0` : il y a un landing pad, mais c'est un **cleanup** uniquement (pas de `catch`). Le landing pad appelle les destructeurs des objets locaux puis appelle `_Unwind_Resume` pour propager l'exception.  
- Si `cs_lp != 0` et `cs_action != 0` : il y a un landing pad avec un ou plusieurs `catch`. L'action table décrit quels types sont interceptés.

> 💡 **En RE :** la call site table vous dit exactement quelles plages de code sont dans un bloc `try`. Si `cs_start` à `cs_start + cs_len` couvre les instructions de `0x401a50` à `0x401a90`, cela signifie qu'un bloc `try` englobe ce code dans le source original.

### L'Action Table

L'action table est un tableau d'entrées, chacune composée de deux champs encodés en SLEB128 :

| Champ | Signification |  
|-------|---------------|  
| `ar_filter` | Index dans la type table (1-indexé). Positif = `catch` de ce type. 0 = cleanup. Négatif = exception specification filter. |  
| `ar_disp` | Déplacement vers l'action suivante dans la chaîne (en octets), ou 0 si c'est la dernière action. |

Les actions sont chaînées : si `ar_disp` est non nul, il y a un prochain handler à vérifier (pour les `catch` multiples dans un même `try`). Le runtime parcourt la chaîne jusqu'à trouver un type qui correspond ou jusqu'à atteindre la fin.

**Exemple — un `try` avec trois `catch` :**

```cpp
try {
    // ...
} catch (const ParseError& e) {     // action #1: type index 3
    // ...
} catch (const AppException& e) {   // action #2: type index 2
    // ...
} catch (const std::exception& e) { // action #3: type index 1
    // ...
}
```

La chaîne d'actions serait :

```
Action #1: ar_filter = 3, ar_disp = → action #2  
Action #2: ar_filter = 2, ar_disp = → action #3  
Action #3: ar_filter = 1, ar_disp = 0 (fin de chaîne)  
```

Le runtime teste d'abord `ParseError`, puis `AppException`, puis `std::exception` — dans l'ordre du code source. L'ordre est important car `ParseError` hérite de `AppException` qui hérite de `std::exception` : un `catch(std::exception&)` en premier intercepterait toutes les exceptions.

### La Type Table

La type table contient des pointeurs vers les structures `_ZTI` (typeinfo) des types interceptés par les `catch`. Elle est lue **à l'envers** depuis l'adresse `@TType base` : l'index 1 est le dernier élément, l'index 2 est l'avant-dernier, etc.

```
@TType base (fin de la type table) :
  ...
  [index 3] → _ZTI10ParseError        (adresse de la typeinfo)
  [index 2] → _ZTI12AppException
  [index 1] → _ZTISt9exception
```

> 💡 **En RE :** la type table vous dit quels types d'exceptions sont interceptés dans la fonction. En résolvant les pointeurs vers les `_ZTI`, vous obtenez les noms de classes d'exception via les chaînes `_ZTS`. Cela révèle quels types d'erreurs le développeur a prévu de gérer, ce qui est une information de haut niveau sur la logique du programme.

### Visualiser `.gcc_except_table`

Il n'existe malheureusement pas d'outil standard qui décode proprement `.gcc_except_table` de manière aussi lisible que `readelf` le fait pour `.eh_frame`. Voici les options disponibles :

```bash
# Dump brut de la section
$ readelf -x .gcc_except_table oop_O0

# Avec objdump (hexadécimal)
$ objdump -s -j .gcc_except_table oop_O0

# Outil dédié : dwarfdump (si installé)
$ dwarfdump --eh-frame oop_O0
```

En pratique, le reverse engineer utilise souvent Ghidra ou un script Python pour parser la LSDA. Les encodages ULEB128/SLEB128 et les pointeurs relatifs rendent le décodage manuel fastidieux mais pas impossible.

**Dans Ghidra :** la section `.gcc_except_table` apparaît dans le Memory Map. Ghidra ne la décode pas automatiquement en structures lisibles, mais le décompileur en tient compte pour certaines analyses. Vous pouvez créer un script Ghidra qui parse les entrées LSDA et annote les landing pads dans le listing.

## Les landing pads en assembleur

Un landing pad est le point d'entrée du code de gestion d'exception dans une fonction. C'est l'adresse vers laquelle le runtime transfère le contrôle après le déroulage de pile. Il en existe deux types.

### Landing pad de `catch`

Le landing pad d'un `catch` commence toujours par un appel à `__cxa_begin_catch` :

```nasm
; Landing pad pour catch (const AppException& e)
.L_catch_AppException:
    mov    rdi, rax                       ; rax = exception object (passé par le runtime)
    call   __cxa_begin_catch@plt
    mov    rbx, rax                       ; rbx = pointeur vers AppException

    ; --- Corps du catch ---
    mov    rdi, rbx
    call   AppException::what() const     ; e.what()
    mov    rsi, rax
    lea    rdi, [rip+.LC_format]
    call   printf@plt

    ; --- Fin du catch ---
    call   __cxa_end_catch@plt
    jmp    .L_after_try_catch             ; continuer après le try/catch
```

Le registre `rax` contient le pointeur vers l'objet exception au moment de l'entrée dans le landing pad. C'est une convention du runtime de déroulage : le registre est défini par les instructions CFI dans `.eh_frame`.

> 💡 **En RE :** pour trouver les landing pads dans une fonction, cherchez les appels à `__cxa_begin_catch`. Chaque appel correspond à un `catch`. Le code entre `__cxa_begin_catch` et `__cxa_end_catch` est le corps du catch. Si le landing pad se termine par un `jmp` vers le code qui suit le try/catch, c'est un catch normal. S'il se termine par `__cxa_rethrow`, c'est un `throw;` (re-lancement).

### Landing pad de cleanup

Un landing pad de cleanup n'intercepte pas l'exception — il exécute les destructeurs des objets locaux puis propage l'exception vers le haut :

```nasm
; Cleanup landing pad : détruire un std::string local
.L_cleanup:
    mov    rbx, rax                       ; sauver l'exception
    lea    rdi, [rbp-0x40]               ; adresse du string local
    call   std::string::~basic_string()   ; destructeur
    mov    rdi, rbx                       ; restaurer l'exception
    call   _Unwind_Resume@plt            ; propager
```

Le pattern est : sauvegarder l'exception, appeler un ou plusieurs destructeurs, puis appeler `_Unwind_Resume`.

> 💡 **En RE :** un landing pad qui ne contient pas `__cxa_begin_catch` mais se termine par `_Unwind_Resume` est un cleanup. La présence de cleanups indique que la fonction construit des objets locaux à durée de vie automatique (RAII) — typiquement des `std::string`, `std::vector`, smart pointers, lock guards, etc. Le nombre et le type de destructeurs appelés dans le cleanup vous donnent des indices sur les variables locales de la fonction.

### Landing pads multiples dans une fonction

Une fonction peut avoir plusieurs landing pads si elle contient :
- Plusieurs blocs `catch` pour le même `try` (un landing pad par `catch`, ou un seul avec dispatch interne).  
- Plusieurs blocs `try` imbriqués ou séquentiels.  
- Des objets locaux nécessitant un cleanup à différents points de la fonction.

Dans le désassemblage, les landing pads apparaissent souvent **après** le code normal de la fonction (après le `ret` ou le `jmp` final), dans une zone de code qui n'est pas atteignable par le flux de contrôle ordinaire. C'est une signature visuelle utile : du code après le `ret` d'une fonction, qui commence par manipuler `rax` et appelle `__cxa_begin_catch` ou `_Unwind_Resume`, est un landing pad.

## La séquence complète `throw` → `catch` dans le binaire

Rassemblons tous les éléments en suivant une exception de bout en bout dans notre binaire. Prenons le cas de la construction d'un `Circle` avec un rayon négatif :

```cpp
// Dans Circle::Circle(), si r <= 0 :
throw AppException("Invalid radius", 10);

// Capturé plus haut dans main() :
catch (const AppException& e) {
    std::cerr << e.what() << " (code " << e.code() << ")" << std::endl;
    return 1;
}
```

**Étape 1 — `throw` dans `Circle::Circle()` :**

```nasm
; Circle::Circle() — vérification du rayon
    ucomisd xmm2, xmm3               ; comparer r avec 0.0
    ja     .radius_ok                  ; si r > 0, continuer

    ; r <= 0 : lancer l'exception
    mov    edi, 48                     ; sizeof(AppException)
    call   __cxa_allocate_exception@plt
    mov    rbx, rax

    mov    rdi, rbx                    ; this = exception space
    lea    rsi, [rip+.LC_invalid_radius] ; "Invalid radius"
    mov    edx, 10                     ; code = 10
    call   _ZN12AppExceptionC1ERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi

    mov    rdi, rbx
    lea    rsi, [rip+_ZTI12AppException]
    lea    rdx, [rip+_ZN12AppExceptionD1Ev]
    call   __cxa_throw@plt
    ; ← flux ne revient pas ici
```

**Étape 2 — Le runtime prend le contrôle :**

`__cxa_throw` appelle `_Unwind_RaiseException` (de `libgcc`), qui consulte `.eh_frame` pour trouver la FDE de `Circle::Circle()`. Si la FDE a une LSDA, le runtime consulte `.gcc_except_table` pour cette fonction. Si aucun handler ne matche (ou s'il n'y a que des cleanups), le runtime remonte au frame appelant (la fonction qui a appelé `Circle::Circle()`), et ainsi de suite.

**Étape 3 — Cleanups intermédiaires :**

Si des frames intermédiaires contiennent des objets locaux à détruire, leurs landing pads de cleanup sont exécutés. Par exemple, si `Circle::Circle()` a été appelé depuis une fonction qui avait construit un `std::string` local, le cleanup de ce string est exécuté avant de remonter plus haut.

**Étape 4 — Arrivée au `catch` dans `main()` :**

Le runtime trouve la LSDA de `main()`, parcourt la call site table, trouve la plage contenant l'appel à `Circle::Circle()`, vérifie l'action table, et détermine que le type `AppException` correspond au `catch`. Il transfere le contrôle au landing pad :

```nasm
; Landing pad dans main()
.L_catch_AppException:
    mov    rdi, rax
    call   __cxa_begin_catch@plt
    mov    rbx, rax                    ; rbx = AppException*

    ; Corps du catch : e.what()
    mov    rdi, rbx
    call   _ZNK12AppException4whatEv   ; AppException::what() const
    mov    rsi, rax
    ; ... imprimer le message ...

    ; e.code()
    mov    rdi, rbx
    mov    eax, DWORD PTR [rbx+0x28]  ; accès direct au champ code_ (offset connu)

    ; ... imprimer le code ...

    call   __cxa_end_catch@plt
    mov    eax, 1                      ; return 1
    jmp    .L_main_epilogue
```

## Reconnaître les `try`/`catch` sans les métadonnées

Même sans parser `.gcc_except_table`, un reverse engineer peut repérer les blocs `try`/`catch` en observant les patterns dans le désassemblage :

**Indices d'un `throw` :**
- Appel à `__cxa_allocate_exception` suivi d'un constructeur puis `__cxa_throw`.  
- `__cxa_throw` est souvent le dernier appel avant du code inaccessible ou un landing pad.

**Indices d'un `catch` :**
- Appel à `__cxa_begin_catch` suivi de code de traitement puis `__cxa_end_catch`.  
- Le code du catch est souvent situé après le flux normal (après le `ret` de la fonction), dans une zone atteignable uniquement via le runtime d'exceptions.

**Indices de cleanup (RAII) :**
- Code qui sauvegarde `rax`, appelle un ou plusieurs destructeurs, puis appelle `_Unwind_Resume`.  
- Souvent situé après le code normal, comme les catch.

**Indices d'un `try` (sans parser les tables) :**
- Plus difficile à identifier. Les appels de fonction situés entre le début de la fonction et le premier landing pad sont probablement dans un bloc `try`, surtout si la fonction contient des catch.  
- Ghidra reconstruit parfois les blocs `try`/`catch` dans le décompileur, les affichant comme des pseudo-instructions `try`/`catch`.

## Impact de `-fno-exceptions`

Le flag `-fno-exceptions` désactive complètement le support des exceptions C++. Le binaire résultant :

- Ne contient pas de `.gcc_except_table`.  
- Peut encore contenir `.eh_frame` (pour les débogueurs), mais sans pointeurs LSDA.  
- Ne contient aucun appel à `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch`, `_Unwind_Resume`.  
- Ne contient aucun landing pad.  
- `throw` provoque une erreur de compilation.  
- `try`/`catch` provoque une erreur de compilation.

En RE, l'absence totale de ces symboles dans la PLT et de la section `.gcc_except_table` confirme que le binaire a été compilé avec `-fno-exceptions`.

> ⚠️ **Attention :** certains projets désactivent les exceptions mais utilisent des bibliothèques qui les activent (comme `libstdc++`). Dans ce cas, vous verrez les symboles `__cxa_*` dans les dépendances dynamiques mais pas d'utilisation directe dans le code du binaire.

## Impact des niveaux d'optimisation

Les optimisations de GCC affectent les exceptions de plusieurs manières :

**En `-O0` :** le code est fidèle à la source. Chaque `throw`, `catch`, et cleanup est clairement séparé. Les landing pads sont faciles à identifier.

**En `-O2` / `-O3` :**
- GCC peut **éliminer du code mort** après un `throw` (puisque `__cxa_throw` ne retourne jamais).  
- Les destructeurs dans les cleanups peuvent être **inlinés**, rendant les landing pads plus longs et moins reconnaissables.  
- Si GCC prouve qu'une exception ne peut pas être lancée dans une plage, il peut **supprimer le landing pad** correspondant et la call site entry.  
- Le `catch(...)` (catch-all) peut être optimisé différemment.  
- Les `noexcept` déclarés sur des fonctions permettent à GCC de supprimer les cleanups pour les appels à ces fonctions.

**L'attribut `noexcept` :** les fonctions déclarées `noexcept` ne génèrent pas de landing pads pour les appels qu'elles contiennent. Si une exception s'échappe néanmoins d'une fonction `noexcept`, le runtime appelle `std::terminate()`. En RE, si vous voyez un appel à `std::terminate` dans un landing pad (au lieu de `_Unwind_Resume` ou `__cxa_begin_catch`), c'est le signe d'une violation de `noexcept` :

```nasm
; noexcept violation handler
.L_noexcept_violation:
    call   std::terminate@plt         ; termine le programme
```

## Résumé des patterns à reconnaître

| Pattern assembleur | Signification |  
|--------------------|---------------|  
| `call __cxa_allocate_exception; ...; call __cxa_throw` | `throw ExceptionType(args)` |  
| Argument `rsi` de `__cxa_throw` = `lea [_ZTI...]` | Type de l'exception lancée (typeinfo) |  
| Argument `rdx` de `__cxa_throw` = `lea [_ZN...D1Ev]` | Destructeur de la classe d'exception |  
| Argument `edi` de `__cxa_allocate_exception` = constante | `sizeof(ExceptionClass)` |  
| `call __cxa_begin_catch; ...; call __cxa_end_catch` | Bloc `catch` |  
| Code après le `ret` contenant `__cxa_begin_catch` | Landing pad de catch |  
| Code sauvegardant `rax`, appelant des dtors, puis `_Unwind_Resume` | Landing pad de cleanup (RAII) |  
| `call __cxa_rethrow` | `throw;` (re-lancement dans un catch) |  
| `call std::terminate` dans un landing pad | Violation de `noexcept` |  
| FDE avec pointeur LSDA non nul dans `.eh_frame` | Fonction contenant des try/catch ou des cleanups |  
| Absence de `.gcc_except_table` et de `__cxa_*` dans la PLT | Binaire compilé avec `-fno-exceptions` |

---


⏭️ [STL internals : `std::vector`, `std::string`, `std::map`, `std::unordered_map` en mémoire](/17-re-cpp-gcc/05-stl-internals.md)
