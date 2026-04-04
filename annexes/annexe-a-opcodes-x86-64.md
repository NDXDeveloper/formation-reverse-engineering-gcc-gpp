🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe A — Référence rapide des opcodes x86-64 fréquents en RE

> 📎 **Fiche de référence** — Cette annexe recense les instructions x86-64 que vous rencontrerez le plus souvent en reverse engineering de binaires compilés avec GCC/G++. Elle n'a pas vocation à remplacer le manuel Intel (plus de 2 500 pages) mais à couvrir les ~95 % d'instructions présentes dans un binaire ELF typique.

---

## Conventions de notation

Tout au long de cette annexe, les instructions sont présentées en **syntaxe Intel** (destination à gauche, source à droite), qui est la syntaxe par défaut de Ghidra, IDA et la plus répandue dans la littérature RE. Pour obtenir cette syntaxe avec `objdump`, utilisez le flag `-M intel`.

Les abréviations suivantes sont utilisées dans les tableaux :

| Notation | Signification |  
|----------|---------------|  
| `reg` | Registre général (ex : `rax`, `ecx`, `r8d`) |  
| `r/m` | Registre ou opérande mémoire (ex : `rax`, `[rbp-0x10]`) |  
| `imm` | Valeur immédiate (constante encodée dans l'instruction) |  
| `mem` | Opérande mémoire uniquement |  
| `rel` | Adresse relative (offset par rapport à `rip`) |  
| `RFLAGS` | Registre de flags (ZF, SF, CF, OF, etc.) |

Les suffixes de taille suivent la convention Intel :

| Suffixe / Préfixe | Taille | Exemple registre |  
|--------------------|--------|------------------|  
| `byte` | 8 bits | `al`, `cl`, `r8b` |  
| `word` | 16 bits | `ax`, `cx`, `r8w` |  
| `dword` | 32 bits | `eax`, `ecx`, `r8d` |  
| `qword` | 64 bits | `rax`, `rcx`, `r8` |

> 💡 **Rappel important** : en x86-64, une écriture dans un registre 32 bits (ex : `mov eax, 0`) met automatiquement à zéro les 32 bits supérieurs du registre 64 bits correspondant (`rax`). GCC exploite massivement ce comportement pour économiser un octet d'encodage (`xor eax, eax` au lieu de `xor rax, rax`).

---

## 1 — Transfert de données

Ces instructions représentent à elles seules une part considérable du code désassemblé. La famille `mov` et ses variantes constituent le socle de tout programme.

### 1.1 — Déplacements de base

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `mov` | `r/m, r/m/imm` | Copie la source vers la destination | Aucun |  
| `movzx` | `reg, r/m` (taille inférieure) | Copie avec extension par zéros (non signé) | Aucun |  
| `movsx` | `reg, r/m` (taille inférieure) | Copie avec extension de signe (signé) | Aucun |  
| `movsxd` | `reg64, r/m32` | Extension de signe 32→64 bits | Aucun |  
| `cmovcc` | `reg, r/m` | Copie conditionnelle (selon les flags, voir §7) | Aucun |

**Ce que vous verrez en RE** : `movzx eax, byte ptr [rbp-0x1]` est le pattern classique de GCC pour charger un `char` ou un `unsigned char` dans un registre 32 bits. `movsx` apparaît lorsque la variable source est signée et que le compilateur doit préserver le signe lors de l'élargissement. `movsxd` est fréquent quand un `int` (32 bits) sert d'index dans un tableau de pointeurs (64 bits).

### 1.2 — Chargement d'adresse

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `lea` | `reg, mem` | Charge l'adresse effective (et non le contenu) | Aucun |

`lea` (*Load Effective Address*) est l'une des instructions les plus polyvalentes et les plus trompeuses pour le débutant en RE. Elle calcule une adresse mais **ne lit jamais la mémoire**. GCC l'utilise dans trois contextes principaux :

- **Calcul d'adresse réelle** : `lea rdi, [rip+0x2a3e]` charge l'adresse d'une chaîne dans `.rodata` pour un futur appel à `printf` ou `puts`. C'est l'usage « normal » de `lea`.  
- **Arithmétique déguisée** : `lea eax, [rdi+rsi*4+0x5]` calcule `rdi + rsi*4 + 5` en une seule instruction, sans toucher aux flags. GCC préfère souvent `lea` à une séquence `add`/`imul` car elle est plus compacte et ne modifie pas `RFLAGS`.  
- **Passage de pointeur sur variable locale** : `lea rdi, [rbp-0x20]` passe l'adresse d'un buffer local en premier argument d'une fonction. Si vous voyez `lea` suivi de `call`, c'est presque toujours un passage de pointeur.

### 1.3 — Opérations sur la pile

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `push` | `r/m/imm` | Décrémente `rsp` de 8, puis écrit la valeur à `[rsp]` | Aucun |  
| `pop` | `r/m` | Lit la valeur à `[rsp]`, puis incrémente `rsp` de 8 | Aucun |

En x86-64 avec la convention System V AMD64, `push` et `pop` sont principalement visibles dans les **prologues et épilogues de fonctions** pour sauvegarder/restaurer les registres callee-saved (`rbx`, `rbp`, `r12`–`r15`). Le passage de paramètres se fait par registres (voir Annexe B), donc les `push` d'arguments sont rares sauf quand une fonction a plus de 6 paramètres entiers.

### 1.4 — Échange et conversion

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `xchg` | `r/m, reg` | Échange atomiquement les deux opérandes | Aucun |  
| `bswap` | `reg` | Inverse l'ordre des octets (endianness) | Aucun (indéfini pour 16 bits) |  
| `cbw` / `cwde` / `cdqe` | (implicite) | Extension de signe dans `ax`/`eax`/`rax` | Aucun |  
| `cwd` / `cdq` / `cqo` | (implicite) | Étend le signe de `ax`/`eax`/`rax` vers `dx`/`edx`/`rdx` | Aucun |

`cdq` est extrêmement fréquent : il précède presque toujours une instruction `idiv` pour préparer la paire `edx:eax` avant une division signée 32 bits. Si vous voyez `cdq` suivi de `idiv`, vous êtes face à une division signée en C (`/` ou `%` sur des `int`). `bswap` apparaît dans le code réseau pour convertir entre l'ordre des octets hôte et réseau (`htonl`/`ntohl`).

---

## 2 — Arithmétique entière

### 2.1 — Opérations de base

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `add` | `r/m, r/m/imm` | Addition : `dest = dest + src` | CF, OF, SF, ZF, AF, PF |  
| `sub` | `r/m, r/m/imm` | Soustraction : `dest = dest - src` | CF, OF, SF, ZF, AF, PF |  
| `inc` | `r/m` | Incrémentation : `dest = dest + 1` | OF, SF, ZF, AF, PF (pas CF) |  
| `dec` | `r/m` | Décrémentation : `dest = dest - 1` | OF, SF, ZF, AF, PF (pas CF) |  
| `neg` | `r/m` | Négation (complément à 2) : `dest = -dest` | CF, OF, SF, ZF, AF, PF |  
| `adc` | `r/m, r/m/imm` | Addition avec retenue : `dest = dest + src + CF` | CF, OF, SF, ZF, AF, PF |  
| `sbb` | `r/m, r/m/imm` | Soustraction avec emprunt : `dest = dest - src - CF` | CF, OF, SF, ZF, AF, PF |

> 💡 `inc` et `dec` ne modifient pas le **Carry Flag (CF)**. Cette subtilité est rarement importante en RE pur, mais elle est exploitée dans certaines séquences d'arithmétique multi-précision où le CF doit être préservé entre un `add`/`adc`.

**Pattern GCC courant** : lorsque GCC optimise un compteur de boucle, il utilise souvent `add reg, 1` plutôt que `inc reg` à partir de `-O2`, car les considérations de performance sur les anciens processeurs (dépendance partielle sur les flags) ont laissé des traces dans les heuristiques du compilateur. Ne soyez pas surpris de voir les deux formes coexister dans le même binaire.

### 2.2 — Multiplication

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `imul` | `r/m` | Multiplication signée : `rdx:rax = rax × r/m` (forme 1 opérande) | CF, OF (SF, ZF indéfinis) |  
| `imul` | `reg, r/m` | Multiplication signée tronquée : `reg = reg × r/m` (forme 2 opérandes) | CF, OF |  
| `imul` | `reg, r/m, imm` | Multiplication signée tronquée : `reg = r/m × imm` (forme 3 opérandes) | CF, OF |  
| `mul` | `r/m` | Multiplication non signée : `rdx:rax = rax × r/m` | CF, OF |

La forme à 2 ou 3 opérandes de `imul` est de loin la plus courante dans le code GCC. La forme à 1 opérande (`imul r/m` ou `mul r/m`), qui utilise implicitement `rax` et produit un résultat double largeur dans `rdx:rax`, apparaît principalement dans deux cas : les divisions optimisées par constante (voir ci-dessous) et l'arithmétique multi-précision.

**Pattern critique — Division par constante via multiplication magique** : GCC transforme systématiquement les divisions par constante en une multiplication par le « magic number » (inverse multiplicatif) suivie d'un décalage. Par exemple, une division par 10 d'un `unsigned int` peut devenir :

```
mov     eax, edi  
mov     edx, 0xCCCCCCCD  
imul    rdx, rax            ; ou mul edx selon le contexte  
shr     rdx, 35  
```

Si vous voyez un `imul` ou `mul` avec une constante hexadécimale « bizarre » comme `0xCCCCCCCD`, `0x55555556`, `0x92492493` ou `0xAAAAAAAB`, c'est presque certainement une division optimisée. L'annexe I détaille les constantes magiques les plus courantes et les diviseurs correspondants.

### 2.3 — Division

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `idiv` | `r/m` | Division signée : `rax = rdx:rax / r/m`, `rdx = rdx:rax % r/m` | Indéfinis |  
| `div` | `r/m` | Division non signée : `rax = rdx:rax / r/m`, `rdx = rdx:rax % r/m` | Indéfinis |

Les instructions `div`/`idiv` sont **rares** dans du code optimisé, justement parce que GCC les remplace par des multiplications magiques (voir ci-dessus). Quand vous en voyez une, c'est généralement dans du code compilé en `-O0` (sans optimisation) ou dans une division dont le diviseur n'est pas connu à la compilation (variable).

Le pattern typique d'une division signée en `-O0` est :

```
mov     eax, [rbp-0x4]     ; charge le dividende  
cdq                         ; étend le signe de eax dans edx  
idiv    dword ptr [rbp-0x8] ; divise edx:eax par le diviseur  
; résultat : eax = quotient, edx = reste
```

---

## 3 — Opérations logiques et bit à bit

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `and` | `r/m, r/m/imm` | ET bit à bit | CF=0, OF=0, SF, ZF, PF |  
| `or` | `r/m, r/m/imm` | OU bit à bit | CF=0, OF=0, SF, ZF, PF |  
| `xor` | `r/m, r/m/imm` | OU exclusif bit à bit | CF=0, OF=0, SF, ZF, PF |  
| `not` | `r/m` | Inversion de tous les bits (complément à 1) | Aucun |  
| `test` | `r/m, r/m/imm` | ET bit à bit **sans stocker le résultat** (positionne les flags) | CF=0, OF=0, SF, ZF, PF |

### Idiomes fondamentaux à reconnaître

**`xor reg, reg`** — C'est l'idiome universel pour mettre un registre à zéro. `xor eax, eax` est encodé en 2 octets contre 5 pour `mov eax, 0`. GCC l'utilise systématiquement. Quand vous lisez `xor eax, eax`, lisez mentalement `eax = 0`.

**`test reg, reg`** — Teste si un registre vaut zéro en effectuant un AND avec lui-même sans modifier la valeur. Positionne ZF si le registre est nul, SF si le bit de signe est à 1. C'est le pattern standard pour `if (x == 0)` ou `if (x != 0)` en C, suivi d'un `jz` (saute si zéro) ou `jnz` (saute si non-zéro).

**`test reg, imm`** — Teste si certains bits sont positionnés. `test eax, 1` vérifie la parité (bit 0), ce qui correspond à `if (x % 2 == 0)` ou `if (x & 1)`. `test eax, 0x80` vérifie le bit 7 (signe d'un octet).

**`and reg, imm`** — Sert souvent de masque ou de modulo par une puissance de 2. `and eax, 0xFF` équivaut à un cast vers `unsigned char`. `and eax, 0x7` équivaut à `x % 8` pour un entier non signé.

**`or reg, 0xFFFFFFFF`** — Met tous les bits à 1, équivalent à `reg = -1` en signé. Parfois utilisé pour des valeurs de retour d'erreur.

---

## 4 — Décalages et rotations

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `shl` / `sal` | `r/m, imm8/cl` | Décalage logique/arithmétique à gauche | CF (dernier bit sorti), OF, SF, ZF, PF |  
| `shr` | `r/m, imm8/cl` | Décalage logique à droite (insère des 0) | CF, OF, SF, ZF, PF |  
| `sar` | `r/m, imm8/cl` | Décalage arithmétique à droite (préserve le signe) | CF, OF, SF, ZF, PF |  
| `rol` | `r/m, imm8/cl` | Rotation à gauche | CF, OF |  
| `ror` | `r/m, imm8/cl` | Rotation à droite | CF, OF |  
| `rcl` | `r/m, imm8/cl` | Rotation à gauche à travers le carry | CF, OF |  
| `rcr` | `r/m, imm8/cl` | Rotation à droite à travers le carry | CF, OF |  
| `shld` | `r/m, reg, imm8/cl` | Décalage double précision à gauche | CF, OF, SF, ZF, PF |  
| `shrd` | `r/m, reg, imm8/cl` | Décalage double précision à droite | CF, OF, SF, ZF, PF |

**`shl` et `shr` comme multiplication/division par puissance de 2** — `shl eax, 3` est équivalent à `eax *= 8` et `shr eax, 2` est équivalent à `eax /= 4` (division non signée). GCC utilise systématiquement ces décalages quand le multiplicateur ou diviseur est une puissance de 2 exacte.

**`sar` pour la division signée** — `sar eax, 31` extrait le bit de signe (produit `0` si positif, `-1` si négatif). Ce pattern apparaît dans l'idiome de division signée par puissance de 2 : pour calculer `x / 4` sur un `int` signé, GCC génère une séquence qui ajoute un biais de `3` (`diviseur - 1`) si `x` est négatif, puis effectue le `sar`. Cela corrige l'arrondi vers zéro requis par la norme C pour les divisions signées.

**Rotations** — `rol` et `ror` sont relativement rares dans du code C compilé classique. Leur présence est un indice fort de code cryptographique (SHA-256, ChaCha20, etc.) ou de routines d'obfuscation. Si vous voyez des rotations dans du code autrement « normal », c'est un signal d'alerte intéressant.

---

## 5 — Comparaison et test

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `cmp` | `r/m, r/m/imm` | Soustraction sans stocker le résultat (positionne les flags) | CF, OF, SF, ZF, AF, PF |  
| `test` | `r/m, r/m/imm` | ET logique sans stocker le résultat (positionne les flags) | CF=0, OF=0, SF, ZF, PF |

`cmp` et `test` sont les deux instructions qui précèdent presque systématiquement un saut conditionnel ou un `cmovcc`/`setcc`. Elles ne modifient aucune donnée — elles positionnent uniquement les flags pour les instructions suivantes.

**Différence fondamentale** : `cmp a, b` effectue `a - b` (soustraction) tandis que `test a, b` effectue `a & b` (ET logique). Utilisez cette distinction pour comprendre ce que le code teste :

- `cmp eax, 5` suivi de `jl` → teste si `eax < 5` (comparaison numérique)  
- `test eax, eax` suivi de `jz` → teste si `eax == 0` (test de nullité)  
- `test al, 0x20` suivi de `jnz` → teste si le bit 5 est positionné (test de bit)

---

## 6 — Sauts inconditionnels et appels

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `jmp` | `rel/r/m` | Saut inconditionnel | Aucun |  
| `call` | `rel/r/m` | Appel de fonction : push `rip` suivant, puis saut | Aucun |  
| `ret` | (aucun ou `imm16`) | Retour de fonction : pop `rip` (et ajoute `imm16` à `rsp` si présent) | Aucun |

**`jmp` en RE** — Trois formes principales :
- `jmp 0x401234` — saut direct, cible fixe (le cas le plus simple à analyser)  
- `jmp rax` — saut indirect via registre, typique des switch-case compilés en jump table ou du dispatch virtuel C++ (vtable)  
- `jmp qword ptr [rip+0x2abc]` — saut via GOT/PLT, typique des appels à des fonctions de bibliothèque partagée en code PIC/PIE

**`call` en RE** — Même schéma que `jmp` :
- `call 0x401100` — appel direct à une fonction interne  
- `call rax` — appel indirect, souvent un pointeur de fonction ou un appel virtuel C++ (`call qword ptr [rax+0x10]` = appel de la 3ᵉ méthode virtuelle)  
- `call printf@plt` — appel via PLT à une fonction de bibliothèque (voir chapitre 2.9)

**`ret`** — Dans la majorité des cas, `ret` ne prend pas d'opérande. La forme `ret imm16` (qui ajuste `rsp` après le `pop rip`) est un vestige de la convention `stdcall` (Windows 32 bits) et n'apparaît quasiment jamais dans du code System V AMD64.

---

## 7 — Sauts conditionnels

Les sauts conditionnels testent l'état des flags positionnés par une instruction précédente (`cmp`, `test`, `sub`, `add`, etc.). Chaque mnémonique correspond à une condition spécifique. Certains ont des alias (synonymes) pour améliorer la lisibilité.

### 7.1 — Conditions sur le zéro et l'égalité

| Instruction | Alias | Condition (flags) | Sémantique C après `cmp a, b` |  
|-------------|-------|-------------------|-------------------------------|  
| `jz` | `je` | ZF = 1 | `a == b` |  
| `jnz` | `jne` | ZF = 0 | `a != b` |

### 7.2 — Conditions non signées

Ces sauts s'utilisent après une comparaison entre valeurs **non signées** (unsigned). Les mnémoniques utilisent les termes *above* (au-dessus) et *below* (en-dessous).

| Instruction | Alias | Condition (flags) | Sémantique C après `cmp a, b` (unsigned) |  
|-------------|-------|-------------------|------------------------------------------|  
| `ja` | `jnbe` | CF = 0 ET ZF = 0 | `a > b` |  
| `jae` | `jnb`, `jnc` | CF = 0 | `a >= b` |  
| `jb` | `jnae`, `jc` | CF = 1 | `a < b` |  
| `jbe` | `jna` | CF = 1 OU ZF = 1 | `a <= b` |

### 7.3 — Conditions signées

Ces sauts s'utilisent après une comparaison entre valeurs **signées** (int, long). Les mnémoniques utilisent les termes *greater* (plus grand) et *less* (plus petit).

| Instruction | Alias | Condition (flags) | Sémantique C après `cmp a, b` (signed) |  
|-------------|-------|-------------------|----------------------------------------|  
| `jg` | `jnle` | ZF = 0 ET SF = OF | `a > b` |  
| `jge` | `jnl` | SF = OF | `a >= b` |  
| `jl` | `jnge` | SF ≠ OF | `a < b` |  
| `jle` | `jng` | ZF = 1 OU SF ≠ OF | `a <= b` |

### 7.4 — Conditions sur des flags individuels

| Instruction | Alias | Condition (flags) | Usage typique en RE |  
|-------------|-------|-------------------|---------------------|  
| `js` | — | SF = 1 | Le résultat est négatif |  
| `jns` | — | SF = 0 | Le résultat est positif ou nul |  
| `jo` | — | OF = 1 | Débordement signé (overflow) |  
| `jno` | — | OF = 0 | Pas de débordement signé |  
| `jp` | `jpe` | PF = 1 | Parité paire (rare en RE classique) |  
| `jnp` | `jpo` | PF = 0 | Parité impaire (rare en RE classique) |  
| `jcxz` | `jecxz`, `jrcxz` | `(r/e)cx = 0` | Rare — teste directement le registre compteur |

### 7.5 — Lire un saut conditionnel en RE : méthode rapide

Lorsque vous rencontrez un saut conditionnel dans un désassemblage, remontez à l'instruction qui a positionné les flags (généralement le `cmp` ou `test` immédiatement au-dessus) et appliquez la correspondance :

1. Identifiez les deux opérandes du `cmp`/`test` : ce sont les valeurs comparées (appelons-les `A` et `B` pour `cmp A, B`).  
2. Regardez le mnémonique du saut pour déterminer la condition.  
3. Déterminez si la comparaison est signée ou non signée en regardant les mnémoniques (*above/below* = non signé, *greater/less* = signé).

Le saut est pris si la condition est vraie, et l'exécution tombe au travers (*fall-through*) si la condition est fausse. Dans une structure `if`/`else` compilée par GCC, le saut conditionnel saute généralement vers le bloc `else` (ou vers la fin du `if`), et le *fall-through* exécute le bloc `then`. Autrement dit, la condition du saut est souvent l'**inverse** de la condition du `if` en C :

```c
// Code C
if (x == 5) {
    do_something();
}
```

```asm
; Code assembleur typique (GCC)
cmp     eax, 5  
jne     skip          ; saute si x != 5 (inverse de la condition C)  
call    do_something  
skip:  
```

---

## 8 — Instructions conditionnelles sans saut

### 8.1 — `SETcc` — Positionner un octet selon une condition

| Instruction | Opérandes | Description |  
|-------------|-----------|-------------|  
| `setcc` | `r/m8` | Met l'opérande à 1 si la condition `cc` est vraie, 0 sinon |

Les suffixes de condition sont les mêmes que pour les sauts (`sete`, `setne`, `setl`, `setge`, `seta`, `setb`, etc.).

**En RE** : `sete al` suivi de `movzx eax, al` est le pattern standard de GCC pour une expression booléenne du type `return (a == b);`. Le `movzx` élargit le résultat 8 bits en 32 bits pour l'utiliser comme valeur de retour `int` (ou `bool` promu en `int`).

### 8.2 — `CMOVcc` — Déplacement conditionnel

| Instruction | Opérandes | Description |  
|-------------|-----------|-------------|  
| `cmovcc` | `reg, r/m` | Copie la source dans la destination si la condition `cc` est vraie |

Mêmes suffixes de condition que pour les sauts et `setcc`. `cmovcc` est très utilisé par GCC à partir de `-O2` pour éviter les branchements dans les expressions ternaires et les `min`/`max` :

```c
// Code C
int result = (a > b) ? a : b;  // max(a, b)
```

```asm
; Code assembleur (GCC -O2)
cmp     edi, esi  
cmovl   edi, esi      ; si edi < esi, alors edi = esi  
mov     eax, edi       ; retourne le résultat  
```

---

## 9 — Manipulation de chaînes et blocs mémoire

Ces instructions opèrent sur des blocs d'octets pointés par `rsi` (source) et `rdi` (destination), avec `rcx` comme compteur. Elles sont souvent précédées du préfixe `rep` ou `repne`.

| Instruction | Préfixe courant | Description |  
|-------------|-----------------|-------------|  
| `movsb/w/d/q` | `rep` | Copie `rcx` éléments de `[rsi]` vers `[rdi]` |  
| `stosb/w/d/q` | `rep` | Remplit `rcx` éléments à `[rdi]` avec la valeur de `al`/`ax`/`eax`/`rax` |  
| `lodsb/w/d/q` | (rare) | Charge un élément de `[rsi]` dans `al`/`ax`/`eax`/`rax` |  
| `cmpsb/w/d/q` | `repz`/`repnz` | Compare les éléments de `[rsi]` et `[rdi]` |  
| `scasb/w/d/q` | `repnz` | Cherche `al`/`ax`/`eax`/`rax` dans le bloc à `[rdi]` |

**En RE** : ces instructions apparaissent souvent dans les implémentations inline de `memcpy` (`rep movsb`), `memset` (`rep stosb`), `strcmp`/`memcmp` (`repz cmpsb`) et `strlen` (`repnz scasb`). GCC génère ces séquences soit directement (fonctions intégrées), soit quand il inline les fonctions de la libc. `rep movsq` copie 8 octets par itération et est utilisé pour les copies de taille connue à la compilation et alignées.

Le flag de direction (DF dans RFLAGS) contrôle le sens de parcours : DF = 0 signifie parcours croissant (le cas normal, garanti par la convention System V à l'entrée de chaque fonction), DF = 1 signifie parcours décroissant (très rare, doit être restauré par `cld` avant le retour).

---

## 10 — Manipulation de bits avancée

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `bt` | `r/m, reg/imm8` | Copie le bit n° `src` de `dest` dans CF | CF |  
| `bts` | `r/m, reg/imm8` | Comme `bt`, puis met le bit à 1 | CF |  
| `btr` | `r/m, reg/imm8` | Comme `bt`, puis met le bit à 0 | CF |  
| `btc` | `r/m, reg/imm8` | Comme `bt`, puis inverse le bit | CF |  
| `bsf` | `reg, r/m` | Bit Scan Forward : index du premier bit à 1 (depuis le LSB) | ZF |  
| `bsr` | `reg, r/m` | Bit Scan Reverse : index du premier bit à 1 (depuis le MSB) | ZF |  
| `popcnt` | `reg, r/m` | Compte le nombre de bits à 1 | ZF (CF=OF=SF=PF=0) |  
| `lzcnt` | `reg, r/m` | Compte les zéros en tête (leading zeros) | CF, ZF |  
| `tzcnt` | `reg, r/m` | Compte les zéros en queue (trailing zeros) | CF, ZF |

Ces instructions sont moins fréquentes dans du code « business logic » classique mais apparaissent dans les implémentations de structures de données (bitmaps, sets), les algorithmes de hashing, et les builtins GCC (`__builtin_ctz`, `__builtin_clz`, `__builtin_popcount`). `bsf`/`tzcnt` correspond à `__builtin_ctz()` et `bsr`/`lzcnt` à `__builtin_clz()`. GCC peut émettre `popcnt` si `-mpopcnt` ou `-march=native` est utilisé sur un processeur compatible.

---

## 11 — Appels système et instructions spéciales

| Instruction | Opérandes | Description | Flags affectés |  
|-------------|-----------|-------------|----------------|  
| `syscall` | (implicite) | Appel système Linux x86-64 | RCX, R11 écrasés |  
| `nop` | (aucun ou `r/m`) | No Operation (ne fait rien) | Aucun |  
| `int 3` | — | Breakpoint (trap pour le débogueur) | — |  
| `ud2` | — | Instruction non définie (lève `#UD` — crash intentionnel) | — |  
| `endbr64` | — | End Branch 64 — marqueur CET (Control-flow Enforcement) | Aucun |  
| `hlt` | — | Arrêt du processeur (ring 0 uniquement) | — |  
| `cpuid` | (implicite) | Identification du processeur | EAX, EBX, ECX, EDX |  
| `rdtsc` | — | Lit le compteur de cycles dans `edx:eax` | Aucun |  
| `pause` | — | Indice de spin-wait (optimise les boucles d'attente) | Aucun |

**`syscall`** — En Linux x86-64, l'interface d'appel système passe le numéro du syscall dans `rax`, et les arguments dans `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` (attention : `r10` et non `rcx` pour le 4ᵉ argument, contrairement à la convention d'appel des fonctions). Le résultat revient dans `rax`. Voir `unistd_64.h` ou `ausyscall --dump` pour la table des numéros.

**`nop`** — Les `nop` multi-octets (`nop dword ptr [rax+0x0]`, etc.) sont du **padding d'alignement** inséré par GCC/l'assembleur pour aligner les cibles de saut ou les débuts de fonctions sur des frontières de 16 octets. Ils n'ont aucune signification logique. Ne perdez pas de temps à les analyser.

**`endbr64`** — Présent en début de chaque fonction et chaque cible de saut indirect si le binaire a été compilé avec CET (Intel Control-flow Enforcement Technology, `-fcf-protection`). C'est un marqueur de sécurité qui valide les destinations des branchements indirects. Il est fonctionnellement un `nop` sur les processeurs sans CET.

**`int 3`** (opcode `0xCC`) — Le breakpoint logiciel. GDB écrit cet opcode à l'adresse du breakpoint en remplaçant l'octet original. C'est aussi l'instruction que certaines techniques anti-débogage recherchent dans leur propre code (voir chapitre 19.8).

**`ud2`** — Génère intentionnellement une exception « instruction non définie ». GCC l'insère après `__builtin_unreachable()` ou à la fin d'un `switch` exhaustif pour marquer un chemin théoriquement inatteignable. En pratique, si l'exécution atteint `ud2`, le programme crash avec `SIGILL`.

**`rdtsc`** — Lit le compteur de cycles du processeur (Time Stamp Counter). Utilisé dans les mesures de performance et — de manière plus pertinente en RE — dans les **techniques anti-débogage par timing** (chapitre 19.7). Le pattern typique : deux `rdtsc` encadrant un bloc de code, avec une comparaison de la différence contre un seuil.

---

## 12 — Instructions flottantes courantes (SSE/SSE2)

Le code flottant moderne en x86-64 utilise les registres SSE (`xmm0`–`xmm15`) plutôt que le FPU x87 obsolète. La convention System V AMD64 passe les flottants dans `xmm0`–`xmm7` et les retourne dans `xmm0`.

| Instruction | Opérandes | Description |  
|-------------|-----------|-------------|  
| `movss` | `xmm, xmm/m32` | Déplace un `float` (scalar single) |  
| `movsd` | `xmm, xmm/m64` | Déplace un `double` (scalar double) |  
| `movaps` / `movups` | `xmm, xmm/m128` | Déplace 128 bits aligné / non aligné (packed) |  
| `addss` / `addsd` | `xmm, xmm/m` | Addition `float` / `double` |  
| `subss` / `subsd` | `xmm, xmm/m` | Soustraction `float` / `double` |  
| `mulss` / `mulsd` | `xmm, xmm/m` | Multiplication `float` / `double` |  
| `divss` / `divsd` | `xmm, xmm/m` | Division `float` / `double` |  
| `comiss` / `comisd` | `xmm, xmm/m` | Comparaison ordonnée (positionne ZF, CF, PF) |  
| `ucomiss` / `ucomisd` | `xmm, xmm/m` | Comparaison non ordonnée (gère les NaN) |  
| `cvtsi2ss` / `cvtsi2sd` | `xmm, r/m32/64` | Convertit entier → `float` / `double` |  
| `cvtss2si` / `cvtsd2si` | `reg, xmm/m` | Convertit `float` / `double` → entier (avec arrondi) |  
| `cvttss2si` / `cvttsd2si` | `reg, xmm/m` | Convertit `float` / `double` → entier (troncature vers 0) |  
| `cvtss2sd` / `cvtsd2ss` | `xmm, xmm/m` | Convertit entre `float` et `double` |  
| `xorps` / `xorpd` | `xmm, xmm/m128` | XOR packed (utilisé pour mettre à zéro un registre SSE) |  
| `sqrtss` / `sqrtsd` | `xmm, xmm/m` | Racine carrée `float` / `double` |  
| `maxss` / `minss` | `xmm, xmm/m` | Maximum / minimum `float` |  
| `maxsd` / `minsd` | `xmm, xmm/m` | Maximum / minimum `double` |

**Mnémonique des suffixes** : `ss` = Scalar Single (`float`), `sd` = Scalar Double (`double`), `ps` = Packed Single (4 × `float`), `pd` = Packed Double (2 × `double`).

**`xorps xmm0, xmm0`** — L'équivalent de `xor eax, eax` pour les flottants : met le registre `xmm0` à zéro. Pattern omniprésent en début de fonction quand une variable flottante est initialisée à `0.0`.

**En RE** : si vous voyez des instructions avec le suffixe `ss`, le code manipule des `float`. Si les suffixes sont `sd`, ce sont des `double`. Les instructions `cvt*` indiquent des conversions de type (casts explicites ou implicites en C). Les instructions packed (`ps`, `pd`) en dehors du code explicitement vectorisé signalent souvent des optimisations SIMD automatiques de GCC (`-ftree-vectorize`, activé par défaut à `-O2`).

---

## 13 — Instructions SIMD fréquentes (au-delà du scalaire)

Lorsque GCC vectorise une boucle ou que le code source utilise des intrinsics, vous rencontrerez des instructions packed. Voici les plus courantes, à savoir reconnaître sans nécessairement comprendre en détail :

| Instruction | Registres | Description courte |  
|-------------|-----------|-------------------|  
| `paddd` / `paddq` | `xmm` | Addition packed d'entiers 32/64 bits |  
| `psubd` / `psubq` | `xmm` | Soustraction packed d'entiers 32/64 bits |  
| `pmulld` | `xmm` | Multiplication packed d'entiers 32 bits |  
| `pcmpeqd` / `pcmpgtd` | `xmm` | Comparaison packed d'entiers (==, >) |  
| `pand` / `por` / `pxor` | `xmm` | Opérations logiques packed |  
| `pshufd` | `xmm, xmm, imm8` | Mélange (shuffle) de dwords dans un registre |  
| `punpcklbw/wd/dq/qdq` | `xmm` | Entrelacement (interleave) packed |  
| `movdqa` / `movdqu` | `xmm, m128` | Déplacement 128 bits aligné / non aligné (entiers) |  
| `pshufb` | `xmm, xmm` | Shuffle d'octets (SSSE3 — lookup tables rapides) |

**Préfixes AVX** : si le binaire est compilé avec `-mavx` ou `-march=haswell` (et au-delà), les mêmes instructions apparaissent avec un préfixe `v` et des registres `ymm` (256 bits) ou `zmm` (512 bits) : `vaddps`, `vmovups`, `vpaddd`, etc. La logique est identique mais la largeur est doublée ou quadruplée.

**En RE** : ne paniquez pas face aux instructions SIMD. Identifiez d'abord la boucle qu'elles implémentent (les instructions SIMD sont presque toujours dans le corps d'une boucle), puis cherchez une version non vectorisée du même traitement — GCC génère souvent un « scalar tail » après la boucle vectorisée, qui traite les éléments restants un par un et est beaucoup plus lisible. Analysez ce tail pour comprendre la logique, puis vérifiez que la boucle SIMD fait la même chose en parallèle.

---

## 14 — Tableau récapitulatif par fréquence en RE

Pour conclure, voici un classement approximatif des instructions par fréquence d'apparition dans un binaire ELF x86-64 typique compilé avec GCC. Ce classement est basé sur des comptages statistiques de binaires courants et donne une idée de ce que vous verrez le plus souvent.

| Rang | Instructions | Catégorie |  
|------|-------------|-----------|  
| 1 | `mov`, `lea` | Transfert |  
| 2 | `call`, `ret` | Appel/retour |  
| 3 | `cmp`, `test` | Comparaison |  
| 4 | `jz/je`, `jnz/jne`, `jmp` | Sauts |  
| 5 | `push`, `pop` | Pile |  
| 6 | `add`, `sub` | Arithmétique |  
| 7 | `xor`, `and`, `or` | Logique |  
| 8 | `shl`, `shr`, `sar` | Décalages |  
| 9 | `movzx`, `movsx` | Extensions |  
| 10 | `imul` | Multiplication |  
| 11 | `nop`, `endbr64` | Padding/CET |  
| 12 | `cmovcc`, `setcc` | Conditionnel sans saut |  
| 13 | `jl`, `jg`, `jle`, `jge`, `ja`, `jb`, `jbe`, `jae` | Sauts signés/non signés |  
| 14 | `movss`, `movsd`, `addsd`, `mulsd` | Flottants SSE |  
| 15 | `rep movsb/q`, `rep stosb/q` | Blocs mémoire |

Les instructions des rangs 1 à 8 constituent à elles seules la vaste majorité du code que vous analyserez. Si vous maîtrisez parfaitement ces ~20 instructions et les patterns d'utilisation décrits dans cette annexe, vous pouvez lire confortablement la plupart des fonctions désassemblées.

---

> 📚 **Pour aller plus loin** :  
> - **Annexe B** — [Conventions d'appel System V AMD64 ABI](/annexes/annexe-b-system-v-abi.md) — complète cette annexe avec le détail du passage de paramètres et de la gestion de la pile.  
> - **Annexe I** — [Patterns GCC reconnaissables à l'assembleur](/annexes/annexe-i-patterns-gcc.md) — catalogue les séquences d'instructions idiomatiques que GCC génère pour des constructions C/C++ courantes.  
> - **Chapitre 3** — [Bases de l'assembleur x86-64 pour le RE](/03-assembleur-x86-64/README.md) — reprend ces instructions dans un contexte d'apprentissage progressif avec des exemples pratiques.  
> - **Manuel Intel** — *Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume 2* — la référence exhaustive (et volumineuse) de toutes les instructions x86-64.

⏭️ [Conventions d'appel System V AMD64 ABI (tableau récapitulatif)](/annexes/annexe-b-system-v-abi.md)
