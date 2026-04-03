🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 2.9 — Résolution dynamique des symboles : PLT/GOT en détail (lazy binding)

> 🎯 **Objectif de cette section** : Comprendre en détail le mécanisme PLT/GOT qui permet aux binaires dynamiquement liés d'appeler des fonctions de bibliothèques partagées, maîtriser le fonctionnement du lazy binding, et savoir exploiter ces connaissances en analyse statique comme dynamique.

---

## Le problème à résoudre

Quand notre `hello.c` appelle `strcmp`, le code de `strcmp` ne se trouve pas dans le binaire — il est dans `libc.so.6`, une bibliothèque partagée chargée à une adresse inconnue au moment de la compilation. Pire, avec l'ASLR (section 2.8), cette adresse change à chaque exécution.

Le compilateur ne peut donc pas insérer un `call 0x7f8c3a2XXXXX` directement dans le code machine — cette adresse n'est pas connue. Il faut un mécanisme d'**indirection** : le code appelle un emplacement fixe (connu à la compilation), et cet emplacement contient — ou finira par contenir — la véritable adresse de `strcmp` en mémoire.

Ce mécanisme repose sur deux structures complémentaires :

- La **PLT** (*Procedure Linkage Table*) : une série de petits morceaux de code (*stubs*) dans la section `.plt`, en zone `R-X` (exécutable, non inscriptible).  
- La **GOT** (*Global Offset Table*) : un tableau de pointeurs dans la section `.got.plt`, en zone `RW-` (inscriptible, non exécutable).

## Architecture générale

Voici le schéma complet du mécanisme pour un appel à `strcmp` :

```
     Votre code (.text)              PLT (.plt)                  GOT (.got.plt)
    ┌─────────────────┐         ┌──────────────────┐         ┌──────────────────┐
    │                 │         │                  │         │                  │
    │ call strcmp@plt ├────────►│ strcmp@plt:      │    ┌───►│ GOT[3]:          │
    │                 │         │   jmp *GOT[3]    ├────┘    │  (adresse réelle │
    └─────────────────┘         │                  │         │   de strcmp dans │
                                │   push 0x0       │         │   libc.so.6)     │
                                │   jmp PLT[0]     │         │                  │
                                ├──────────────────┤         ├──────────────────┤
                                │ PLT[0]:          │         │ GOT[0]:          │
                                │   push GOT[1]    │         │  (adresse de     │
                                │   jmp  *GOT[2]   ├────────►│   link_map)      │
                                │                  │         │ GOT[1]:          │
                                └──────────────────┘         │  (link_map)      │
                                                             │ GOT[2]:          │
                                                             │  (adresse de     │
                                         ┌───────────────────┤   _dl_runtime_   │
                                         │                   │   resolve)       │
                                         ▼                   └──────────────────┘
                                ┌──────────────────┐
                                │ ld.so:           │
                                │ _dl_runtime_     │
                                │   resolve()      │
                                │ → cherche strcmp │
                                │ → écrit adresse  │
                                │   dans GOT[3]    │
                                │ → saute à strcmp │
                                └──────────────────┘
```

## Les trois entrées réservées de la GOT

Les trois premières entrées de `.got.plt` ont un rôle spécial, réservé au mécanisme de résolution :

| Entrée | Contenu | Rôle |  
|---|---|---|  
| `GOT[0]` | Adresse de la section `.dynamic` | Permet au resolver de retrouver les tables de symboles et de relocation |  
| `GOT[1]` | Pointeur vers la structure `link_map` | Identifie le binaire ou la `.so` pour laquelle la résolution est demandée |  
| `GOT[2]` | Adresse de `_dl_runtime_resolve` | Point d'entrée du resolver dans `ld.so` |

Ces trois entrées sont remplies par le loader `ld.so` au démarrage du programme, avant l'exécution de la moindre instruction utilisateur. Les entrées suivantes (`GOT[3]`, `GOT[4]`, etc.) correspondent chacune à une fonction importée.

## Le stub PLT en détail

Désassemblons la PLT de notre `hello` pour voir les stubs réels :

```bash
objdump -d -j .plt --no-show-raw-insn hello
```

Sortie typique (syntaxe AT&T) :

```asm
Disassembly of section .plt:

0000000000001020 <.plt>:                          ; ← PLT[0] : le résolveur
    1020:   push   0x2fe2(%rip)                   ; push GOT[1] (link_map)
    1026:   jmp    *0x2fe4(%rip)                  ; jmp  GOT[2] (_dl_runtime_resolve)
    102c:   nopl   0x0(%rax)                      ; padding d'alignement

0000000000001030 <strcmp@plt>:                     ; ← stub pour strcmp
    1030:   jmp    *0x2fd2(%rip)                  ; jmp *GOT[3]
    1036:   push   $0x0                            ; index de relocation = 0
    103b:   jmp    1020 <.plt>                    ; sauter à PLT[0]

0000000000001040 <printf@plt>:                     ; ← stub pour printf
    1040:   jmp    *0x2fca(%rip)                  ; jmp *GOT[4]
    1046:   push   $0x1                            ; index de relocation = 1
    104b:   jmp    1020 <.plt>                    ; sauter à PLT[0]

0000000000001050 <puts@plt>:                       ; ← stub pour puts
    1050:   jmp    *0x2fc2(%rip)                  ; jmp *GOT[5]
    1056:   push   $0x2                            ; index de relocation = 2
    105b:   jmp    1020 <.plt>                    ; sauter à PLT[0]
```

Chaque stub fait exactement 16 octets et suit le même schéma en trois instructions :

1. **`jmp *GOT[n]`** — saut indirect via la GOT. Si l'adresse dans `GOT[n]` est déjà la vraie adresse de la fonction, le saut l'atteint directement. Sinon, l'adresse pointe vers l'instruction suivante (le `push`), ce qui déclenche la résolution.  
2. **`push $index`** — empile l'index de relocation de ce symbole dans la table `.rela.plt`. Le resolver en a besoin pour savoir quelle fonction résoudre.  
3. **`jmp PLT[0]`** — saute vers le stub résolveur commun.

## Le lazy binding pas à pas

Suivons un appel à `strcmp` à travers les deux scénarios : le premier appel (résolution nécessaire) et les appels suivants (résolution déjà faite).

### Premier appel — Résolution lazy

```
1. main() exécute : call strcmp@plt
   → rip saute à 0x1030 (stub strcmp dans .plt)

2. Le stub exécute : jmp *GOT[3]
   → GOT[3] contient 0x1036 (adresse de l'instruction push juste en dessous)
   → Le saut « retombe » sur l'instruction suivante dans le stub

3. Le stub exécute : push $0x0
   → Empile l'index de relocation de strcmp (0)

4. Le stub exécute : jmp PLT[0]
   → Saute au stub résolveur commun à 0x1020

5. PLT[0] exécute : push GOT[1]
   → Empile le pointeur link_map (identifie le binaire)

6. PLT[0] exécute : jmp *GOT[2]
   → Saute à _dl_runtime_resolve dans ld.so

7. _dl_runtime_resolve :
   a. Lit l'index de relocation (0) depuis la pile
   b. Consulte .rela.plt pour l'entrée 0 → symbole "strcmp"
   c. Cherche "strcmp" dans les tables de symboles des .so chargées
   d. Trouve strcmp dans libc.so.6 à l'adresse 0x7f8c3a2XXXXX
   e. ÉCRIT cette adresse dans GOT[3]     ← modification clé
   f. Saute à strcmp (0x7f8c3a2XXXXX) pour exécuter l'appel initial
```

Le point crucial est l'étape 7e : le resolver **écrit** l'adresse réelle de `strcmp` dans l'entrée GOT correspondante. Cette écriture est permanente (pour la durée de vie du processus).

### Appels suivants — Résolution déjà faite

```
1. main() exécute : call strcmp@plt
   → rip saute à 0x1030 (stub strcmp dans .plt)

2. Le stub exécute : jmp *GOT[3]
   → GOT[3] contient maintenant 0x7f8c3a2XXXXX (adresse réelle de strcmp)
   → Le saut atteint DIRECTEMENT strcmp dans libc.so.6

   (les instructions push et jmp PLT[0] ne sont jamais exécutées)
```

Après la première résolution, l'appel ne coûte qu'**une seule indirection** : le `jmp` via la GOT. Le stub PLT sert de trampoline initial mais est court-circuité dès que la GOT est remplie. C'est le « lazy » du lazy binding : la résolution est **paresseuse**, repoussée au moment du premier appel réel.

## Observer le lazy binding en action

### Avec GDB

Vous pouvez observer la GOT avant et après le premier appel :

```bash
gdb -q ./hello
(gdb) break main
(gdb) run RE-101
# Arrêté au début de main

# Trouver l'adresse de la GOT pour strcmp
(gdb) got             # Commande GEF/pwndbg
# ou manuellement :
(gdb) x/gx 0x555555557fd8    # adresse de GOT[3] (selon readelf -r)
# 0x555555557fd8: 0x0000555555555036
#                 ^^^^^^^^^^^^^^^^ pointe vers le push dans le stub PLT
#                                  → strcmp PAS ENCORE RÉSOLU

# Continuer jusqu'après l'appel à strcmp
(gdb) break *0x555555555166    # adresse juste après le call strcmp@plt
(gdb) continue

# Relire la GOT
(gdb) x/gx 0x555555557fd8
# 0x555555557fd8: 0x00007ffff7e5a420
#                 ^^^^^^^^^^^^^^^^ adresse réelle de strcmp dans libc
#                                  → strcmp RÉSOLU

# Vérifier
(gdb) info symbol 0x00007ffff7e5a420
# strcmp in section .text of /lib/x86_64-linux-gnu/libc.so.6
```

### Avec `LD_DEBUG`

Le loader peut tracer les résolutions en temps réel :

```bash
LD_DEBUG=bindings ./hello RE-101 2>&1 | grep strcmp
# binding file ./hello [0] to /lib/.../libc.so.6 [0]:
#   normal symbol `strcmp' [GLIBC_2.2.5]
```

### Avec les relocations ELF

Les entrées de `.rela.plt` décrivent précisément les emplacements GOT à patcher :

```bash
readelf -r hello | grep plt
```

| Offset | Type | Sym. Name |  
|---|---|---|  
| `0x3fd8` | `R_X86_64_JUMP_SLOT` | `strcmp@GLIBC_2.2.5` |  
| `0x3fe0` | `R_X86_64_JUMP_SLOT` | `printf@GLIBC_2.2.5` |  
| `0x3fe8` | `R_X86_64_JUMP_SLOT` | `puts@GLIBC_2.2.5` |

Le type `R_X86_64_JUMP_SLOT` est le type de relocation spécifique au mécanisme PLT/GOT : il indique que l'entrée à l'offset donné dans `.got.plt` doit être remplie avec l'adresse du symbole indiqué. L'offset `0x3fd8` correspond à `GOT[3]` dans notre exemple.

## Immediate binding et Full RELRO

### Le problème de sécurité du lazy binding

Le lazy binding a un défaut inhérent : la GOT doit rester **inscriptible** pendant toute la durée de vie du processus, puisque des entrées peuvent être résolues à tout moment (au premier appel de chaque fonction). Une GOT inscriptible est une cible de choix pour les attaquants :

**GOT overwrite** : si un attaquant trouve une vulnérabilité permettant d'écrire à une adresse arbitraire (buffer overflow, format string, use-after-free…), il peut écraser une entrée GOT pour rediriger un appel de fonction vers une adresse de son choix. Par exemple, écraser l'entrée GOT de `puts` avec l'adresse de `system` — le prochain `puts("/bin/sh")` exécutera `system("/bin/sh")`.

### Immediate binding (`LD_BIND_NOW`)

L'immediate binding résout **toutes** les entrées GOT au chargement, avant l'exécution du moindre code utilisateur. On l'active par la variable d'environnement `LD_BIND_NOW=1` ou par le flag `DT_BIND_NOW` dans la section `.dynamic` du binaire :

```bash
# À l'exécution
LD_BIND_NOW=1 ./hello RE-101

# À la compilation (inscrit le flag dans .dynamic)
gcc -Wl,-z,now -o hello_bindnow hello.c  
readelf -d hello_bindnow | grep BIND_NOW  
# 0x0000000000000018 (BIND_NOW)
```

Avantage : toutes les résolutions sont faites au démarrage, éliminant le coût de la première résolution à chaud. Inconvénient : le démarrage est plus lent si le binaire importe beaucoup de fonctions, car toutes sont résolues même si certaines ne sont jamais appelées.

### RELRO — Relocation Read-Only

RELRO est un mécanisme de protection qui rend certaines sections en lecture seule après les relocations. Il existe en deux niveaux :

**Partial RELRO** (par défaut avec GCC) : après le chargement, les sections `.init_array`, `.fini_array`, `.dynamic` et `.got` (hors `.got.plt`) sont rendues en lecture seule via `mprotect`. La section `.got.plt` reste inscriptible car le lazy binding en a besoin.

```bash
gcc -o hello_partial hello.c    # Partial RELRO par défaut  
checksec --file=hello_partial  
# RELRO: Partial RELRO
```

**Full RELRO** : combine l'immediate binding (`-z now`) avec la protection RELRO (`-z relro`). Toutes les entrées GOT sont résolues au chargement, puis **l'intégralité** de `.got.plt` est rendue en lecture seule avec `mprotect`. Toute tentative d'écriture dans la GOT provoque un `SIGSEGV`.

```bash
gcc -Wl,-z,relro,-z,now -o hello_fullrelro hello.c  
checksec --file=hello_fullrelro  
# RELRO: Full RELRO
```

Comparaison synthétique :

| Aspect | Pas de RELRO | Partial RELRO | Full RELRO |  
|---|---|---|---|  
| `.got` (données) | `RW-` | `R--` après init | `R--` après init |  
| `.got.plt` (fonctions) | `RW-` | `RW-` (lazy binding) | `R--` après résolution |  
| Lazy binding | ✅ Actif | ✅ Actif | ❌ Désactivé |  
| GOT overwrite possible | ✅ Oui | ⚠️ Seulement `.got.plt` | ❌ Non |  
| Coût au démarrage | Minimal | Minimal | Plus élevé |  
| Flag GCC | `-Wl,-z,norelro` | (défaut) | `-Wl,-z,relro,-z,now` |

> 💡 **En RE** : `checksec` (Chapitre 5, section 5.6) vous indique immédiatement le niveau de RELRO. Un binaire Full RELRO est plus résistant à l'exploitation par GOT overwrite, ce qui oriente l'attaquant vers d'autres techniques (ROP pur, hooks de `__malloc_hook`/`__free_hook` — également supprimés dans les glibc récentes, attaques sur la pile, etc.). Pour le reverse engineer défensif, Full RELRO est un indice que le développeur a pris la sécurité au sérieux.

## PLT/GOT et les outils de RE

### Ghidra

Ghidra reconnaît et affiche le mécanisme PLT/GOT nativement. Dans la fenêtre *Symbol Tree*, les fonctions importées apparaissent avec le suffixe `@PLT` (par exemple `strcmp@PLT`). Le décompilateur résout les indirections PLT/GOT et affiche directement `strcmp(input, "RE-101")` dans le pseudo-code — l'analyste n'a pas besoin de comprendre le mécanisme pour lire le résultat.

Cependant, comprendre PLT/GOT devient essentiel quand :
- Vous cherchez à hooker ou patcher un appel spécifique (il faut savoir si vous modifiez le stub PLT, l'entrée GOT, ou le `call` dans `.text`).  
- Vous analysez un exploit qui cible la GOT.  
- Le décompilateur n'arrive pas à résoudre un appel indirect (obfuscation, vtable C++, pointeurs de fonctions).

### GDB et extensions

Les extensions GDB (GEF, pwndbg) fournissent des commandes dédiées pour inspecter la GOT :

```bash
# GEF
gef> got
# Affiche chaque entrée GOT avec l'adresse résolue (ou non)

# pwndbg
pwndbg> gotplt
# Similaire, avec coloration selon l'état de résolution
```

Ces commandes sont précieuses pour vérifier si une fonction a déjà été appelée (GOT résolue) ou non (GOT pointe encore vers le stub PLT).

### Frida

L'instrumentation dynamique avec Frida (Chapitre 13) peut intercepter les appels au niveau PLT en hookant les stubs, ou au niveau GOT en modifiant les entrées de la table. La modification de la GOT est une technique de hooking particulièrement propre : il suffit de remplacer l'adresse dans la GOT par l'adresse de votre fonction de remplacement.

```javascript
// Frida : remplacer l'entrée GOT de strcmp par un hook
var strcmpGotEntry = Module.findExportByName(null, "strcmp");  
Interceptor.attach(strcmpGotEntry, {  
    onEnter: function(args) {
        console.log("strcmp(" + args[0].readUtf8String() +
                    ", " + args[1].readUtf8String() + ")");
    }
});
```

## PLT/GOT pour les variables globales externes

Le mécanisme PLT/GOT ne concerne pas uniquement les fonctions. Les **variables globales importées** depuis des bibliothèques partagées (par exemple `errno`, `stdin`, `stdout`, `stderr`) utilisent la section `.got` (pas `.got.plt`) avec des relocations de type `R_X86_64_GLOB_DAT` :

```bash
readelf -r hello | grep GLOB_DAT
```

| Offset | Type | Sym. Name |  
|---|---|---|  
| `0x3fc0` | `R_X86_64_GLOB_DAT` | `__libc_start_main@GLIBC_2.34` |  
| `0x3fc8` | `R_X86_64_GLOB_DAT` | `__gmon_start__` |

Contrairement aux fonctions (lazy binding), les variables globales sont **toujours résolues immédiatement** au chargement — il n'y a pas de lazy binding pour les données. C'est pourquoi `.got` (pour les données) est rendue en lecture seule même avec Partial RELRO, alors que `.got.plt` (pour les fonctions) ne l'est qu'avec Full RELRO.

## La PLT secondaire : `.plt.got` et `.plt.sec`

Sur les binaires récents (GCC 8+, Binutils 2.29+), vous pouvez rencontrer des sections PLT supplémentaires :

**`.plt.got`** : contient des stubs pour les fonctions résolues via `.got` (pas `.got.plt`). Ces stubs sont utilisés quand le linker sait qu'une fonction sera résolue immédiatement (par exemple avec Full RELRO) et n'a donc pas besoin du mécanisme lazy complet. Le stub est plus simple — un seul `jmp *GOT[n]` sans le `push` ni le saut vers PLT[0].

**`.plt.sec`** : introduite avec les processeurs supportant la protection **IBT** (*Indirect Branch Tracking*, partie de la technologie Intel CET). Chaque stub commence par une instruction `endbr64` (*End Branch*) qui valide que le saut indirect est légitime. Le mécanisme PLT/GOT reste identique, mais les stubs contiennent cette instruction de garde supplémentaire.

```asm
; Stub PLT classique
strcmp@plt:
    jmp    *GOT[3](%rip)
    push   $0x0
    jmp    PLT[0]

; Stub PLT avec CET/IBT (.plt.sec)
strcmp@plt:
    endbr64                     ; ← instruction de garde IBT
    jmp    *GOT[3](%rip)
    nop    ...                  ; padding
```

En RE, ces variantes ne changent pas fondamentalement la logique — le principe de l'indirection via la GOT reste le même. Mais il est utile de les reconnaître pour ne pas être désorienté par une instruction `endbr64` inattendue en tête de chaque stub PLT.

## Résumé du mécanisme PLT/GOT

| Composant | Section | Permissions | Rôle |  
|---|---|---|---|  
| Code appelant | `.text` | `R-X` | Contient `call fonction@plt` |  
| Stub PLT | `.plt` | `R-X` | Trampoline : `jmp *GOT[n]`, sinon déclenche la résolution |  
| PLT[0] (résolveur) | `.plt` | `R-X` | Point d'entrée vers `_dl_runtime_resolve` dans `ld.so` |  
| Entrée GOT | `.got.plt` | `RW-` (ou `R--` si Full RELRO) | Contient l'adresse résolue (ou l'adresse du `push` dans le stub) |  
| Relocations | `.rela.plt` | Non chargée | Décrit quel symbole correspond à quelle entrée GOT |  
| Resolver | `ld.so` | `R-X` | `_dl_runtime_resolve` : cherche le symbole, écrit l'adresse dans la GOT |

Le flux pour chaque appel de fonction importée :

```
.text                .plt               .got.plt            libc.so.6
  │                    │                    │                    │
  │  call strcmp@plt   │                    │                    │
  ├───────────────────►│  jmp *GOT[3]       │                    │
  │                    ├───────────────────►│                    │
  │                    │                    │──── si résolu ────►│ strcmp()
  │                    │                    │                    │
  │                    │◄── si non résolu ──│                    │
  │                    │  push index        │                    │
  │                    │  jmp PLT[0]        │                    │
  │                    │  → _dl_runtime_    │                    │
  │                    │    resolve()       │                    │
  │                    │    écrit GOT[3] ──►│ (adresse réelle)   │
  │                    │    jmp strcmp ─────────────────────────►│ strcmp()
```

---

> 📖 **Ce chapitre touche à sa fin.** Vous disposez maintenant d'une compréhension complète de la chaîne de compilation GNU, du fichier source jusqu'au processus en mémoire avec ses appels dynamiques résolus via PLT/GOT. Ces fondations sont le socle sur lequel s'appuient toutes les techniques de RE que nous aborderons à partir du Chapitre 3.  
>  
> Avant de passer à la suite, validez vos acquis avec le checkpoint ci-dessous.  
>  
> → 🎯 Checkpoint du Chapitre 2 : compiler un même `hello.c` avec `-O0 -g` puis `-O2 -s`, comparer les tailles et sections avec `readelf`.

⏭️ [🎯 Checkpoint : compiler un même `hello.c` avec `-O0 -g` puis `-O2 -s`, comparer les tailles et sections avec `readelf`](/02-chaine-compilation-gnu/checkpoint.md)
