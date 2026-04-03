🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 3.3 — Arithmétique et logique : `add`, `sub`, `imul`, `xor`, `shl`/`shr`, `test`, `cmp`

> 🎯 **Objectif de cette section** : savoir reconnaître et interpréter les opérations arithmétiques et logiques dans le code désassemblé, y compris les idiomes courants de GCC qui utilisent ces instructions de manière inattendue (multiplication déguisée en décalage, `xor` pour mettre à zéro, `test` pour tester la nullité…).

---

## Le principe commun

Toutes les instructions de cette section partagent deux caractéristiques fondamentales :

1. **Elles opèrent sur un ou deux opérandes** selon le format `op destination, source`, où le résultat remplace la destination (sauf pour `cmp` et `test` qui ne stockent rien).  
2. **Elles mettent à jour le registre `RFLAGS`** — c'est ce qui permet aux sauts conditionnels (section 3.4) de prendre leurs décisions juste après.

Ce lien entre instruction arithmétique/logique et flags est le mécanisme central de tout le flux de contrôle en assembleur x86-64. Chaque `if`, chaque boucle, chaque condition du code C passe par ce duo : une instruction qui positionne les flags, suivie d'un saut qui les lit.

---

## Instructions arithmétiques

### `add` — addition

```
add  destination, source      ; destination = destination + source
```

`add` additionne la source à la destination et stocke le résultat dans la destination. Les flags ZF, SF, CF et OF sont mis à jour.

```asm
add     eax, 1                ; eax = eax + 1  (incrémentation)  
add     eax, esi              ; eax = eax + esi  
add     dword [rbp-0x4], 1    ; incrémente directement une variable en mémoire  
add     rsp, 0x30             ; libère 48 octets de pile (épilogue)  
```

Correspondance C :

```c
x += 1;       // →  add  dword [rbp-0x4], 1  
total += val;  // →  add  eax, ecx  
```

> 💡 **Pour le RE** : `add rsp, N` en fin de fonction est le pendant de `sub rsp, N` dans le prologue — il libère l'espace réservé pour les variables locales. Si vous voyez `add rsp, 0x28` dans un épilogue, la fonction avait réservé 40 octets de pile.

### `sub` — soustraction

```
sub  destination, source      ; destination = destination - source
```

Même principe que `add`, mais en soustraction. Les flags sont mis à jour, et c'est exactement ce qui rend `sub` et `cmp` si proches (on y revient plus bas).

```asm
sub     eax, 1                ; eax = eax - 1  (décrémentation)  
sub     eax, ecx              ; eax = eax - ecx  
sub     rsp, 0x20             ; réserve 32 octets sur la pile (prologue)  
```

Correspondance C :

```c
count--;          // →  sub  dword [rbp-0x4], 1  
diff = a - b;     // →  mov eax, edi  /  sub eax, esi  
```

### `inc` et `dec` — incrémentation et décrémentation

```asm
inc     eax               ; eax = eax + 1  
dec     dword [rbp-0x4]   ; variable_locale -= 1  
```

`inc` et `dec` sont des raccourcis pour `add X, 1` et `sub X, 1`. Ils mettent à jour ZF, SF et OF mais **pas CF** — une subtilité qui n'a pas d'impact en RE courant, mais qui explique pourquoi GCC préfère parfois `add reg, 1` à `inc reg` quand le carry flag est nécessaire.

En pratique, GCC moderne utilise `add`/`sub` plutôt que `inc`/`dec` dans la plupart des cas, mais vous croiserez les deux formes.

### `neg` — négation (complément à deux)

```asm
neg     eax               ; eax = -eax
```

`neg` calcule le complément à deux de l'opérande — c'est la traduction de l'opérateur unaire `-` en C. On le voit dans les expressions comme `result = -value` ou dans certaines optimisations où GCC transforme une soustraction en négation suivie d'une addition.

### `imul` — multiplication signée

La multiplication est plus complexe que l'addition car le résultat d'une multiplication de deux valeurs 32 bits peut nécessiter 64 bits. L'architecture x86-64 propose plusieurs formes de `imul` :

**Forme à deux opérandes** (la plus courante en code GCC) :

```asm
imul    eax, ecx           ; eax = eax * ecx   (32 bits × 32 bits, résultat tronqué à 32 bits)  
imul    rax, rdx           ; rax = rax * rdx   (64 bits × 64 bits, tronqué à 64 bits)  
```

**Forme à trois opérandes** (multiplication avec immédiat) :

```asm
imul    eax, ecx, 0xc      ; eax = ecx * 12  
imul    eax, edi, 0x64     ; eax = edi * 100  
```

Cette forme est la traduction directe de `result = value * constante` en C.

**Forme à un opérande** (multiplication complète) :

```asm
imul    ecx                ; edx:eax = eax * ecx (64 bits complets dans edx:eax)
```

Cette forme stocke les 32 bits hauts dans `edx` et les 32 bits bas dans `eax`. On la voit rarement en code applicatif, mais elle apparaît quand le compilateur a besoin du résultat complet (par exemple pour une division ultérieure ou un calcul sur des entiers larges).

Correspondance C :

```c
area = width * height;     // →  imul  eax, ecx  
cost = qty * 12;           // →  imul  eax, edi, 0xc  
```

> 💡 **Pour le RE** : `imul` est la multiplication *signée*. Pour la multiplication *non signée*, le processeur utilise `mul`, mais GCC utilise presque exclusivement `imul` en forme deux ou trois opérandes, car le résultat tronqué est identique pour les deux (la distinction signé/non signé n'importe que pour les bits hauts du résultat complet).

### `div` et `idiv` — division

La division x86-64 est l'opération la plus « exotique » pour un développeur C habitué à un simple `/`. Elle utilise un protocole rigide impliquant les registres `rax` et `rdx` :

**Division non signée (`div`)** :

```asm
; Division 32 bits : edx:eax / ecx → quotient dans eax, reste dans edx
xor     edx, edx           ; met edx à 0 (extension non signée)  
div     ecx                 ; eax = edx:eax / ecx, edx = edx:eax % ecx  
```

**Division signée (`idiv`)** :

```asm
; Division 32 bits signée : edx:eax / ecx → quotient dans eax, reste dans edx
cdq                          ; étend le signe de eax dans edx (sign-extend)  
idiv    ecx                  ; eax = quotient, edx = reste  
```

L'instruction `cdq` (*Convert Doubleword to Quadword*) est le signal d'une division signée imminente — elle propage le bit de signe de `eax` dans tous les bits de `edx`. Son équivalent 64 bits est `cqo` (étend `rax` dans `rdx`).

Correspondance C :

```c
int q = a / b;     // →  cdq  /  idiv ecx     → résultat dans eax  
int r = a % b;     // →  même séquence         → reste dans edx  
```

> 💡 **Pour le RE** : quand vous voyez `cdq` ou `cqo` suivi d'un `idiv`, c'est une division signée. Quand vous voyez `xor edx, edx` suivi d'un `div`, c'est une division non signée. Le quotient est dans `eax`/`rax`, le reste dans `edx`/`rdx` — donc si le code utilise `edx` après le `div`/`idiv`, c'est l'opérateur `%` (modulo) du C.

### Quand GCC évite `div` : la multiplication magique

La division est l'instruction la plus lente du processeur (des dizaines de cycles). GCC l'évite systématiquement quand le diviseur est une **constante connue à la compilation**, en la remplaçant par une séquence de multiplication et de décalages. C'est un idiome extrêmement fréquent en `-O1` et au-delà :

```c
// Code C
int f(int x) {
    return x / 3;
}
```

```asm
; GCC -O2 — pas de div en vue !
mov     eax, edi  
mov     edx, 0x55555556       ; constante magique ≈ (2^32 + 2) / 3  
imul    edx                    ; edx:eax = eax * 0x55555556  
mov     eax, edx               ; eax = bits hauts du résultat  
shr     eax, 0x1f              ; extrait le bit de signe (ajustement pour négatifs)  
add     eax, edx               ; ajustement final  
```

Ce pattern est déroutant la première fois, mais il est mécanique. La « constante magique » est l'inverse multiplicatif modulaire du diviseur. Le chapitre 16 détaille ces optimisations — pour l'instant, retenez la règle suivante :

> ⚠️ **Règle de reconnaissance** : si vous voyez un `imul` avec une grande constante hexadécimale (comme `0x55555556`, `0xAAAAAAAB`, `0xCCCCCCCD`…) suivie de décalages (`shr`, `sar`) et éventuellement d'ajustements, c'est presque certainement une **division par une constante** que GCC a optimisée. L'absence de `div`/`idiv` est normale — c'est le comportement par défaut du compilateur en mode optimisé.

---

## Instructions logiques bit à bit

### `and` — ET logique

```asm
and     eax, 0xff          ; eax = eax & 0xFF (masque : garde l'octet bas)  
and     eax, ecx           ; eax = eax & ecx  
```

L'opération AND met chaque bit du résultat à 1 uniquement si les deux bits correspondants des opérandes sont à 1.

Correspondance C :

```c
masked = value & 0xFF;    // →  and  eax, 0xff  
flags &= MASK;            // →  and  eax, ecx  
```

Usages courants en code GCC :

- **Masquage de bits** : `and eax, 0xff` isole l'octet bas (équivalent d'un cast en `unsigned char`).  
- **Alignement** : `and rsp, 0xfffffffffffffff0` aligne `rsp` sur 16 octets — très fréquent dans le prologue de `main()` ou dans les fonctions utilisant des instructions SSE qui exigent l'alignement.  
- **Test de parité de bit** : `and eax, 1` teste si un nombre est pair ou impair (`n % 2` optimisé).

### `or` — OU logique

```asm
or      eax, 0x1           ; met le bit 0 à 1  (eax |= 1)  
or      eax, ecx           ; eax = eax | ecx  
```

Chaque bit du résultat est à 1 si au moins un des deux bits correspondants est à 1.

Correspondance C :

```c
flags |= FLAG_ACTIVE;     // →  or  eax, 0x4
```

Un cas spécial à retenir : `or reg, reg` ne modifie pas la valeur du registre mais met à jour les flags — c'est une alternative (rare) à `test reg, reg` pour tester la nullité.

### `xor` — OU exclusif

```asm
xor     eax, eax           ; eax = 0  (l'idiome le plus célèbre du x86)  
xor     eax, ecx           ; eax = eax ^ ecx  
xor     byte [rdi], 0x42   ; déchiffrement XOR octet par octet  
```

Le XOR met chaque bit à 1 si les deux bits correspondants sont **différents**, et à 0 s'ils sont identiques. Propriété fondamentale : `A XOR A = 0` pour toute valeur A.

**L'idiome `xor reg, reg`** est la méthode standard de GCC pour mettre un registre à zéro. C'est préféré à `mov reg, 0` car l'encodage est plus court (2 octets contre 5 pour `mov eax, 0`) et les processeurs modernes reconnaissent ce pattern comme un *zeroing idiom* qui brise les dépendances de données.

```asm
xor     eax, eax       ; 2 octets, brise les dépendances — préféré par GCC  
mov     eax, 0          ; 5 octets, fonctionnellement identique mais plus long  
```

> 💡 **Pour le RE** : quand vous voyez `xor eax, eax` en début de fonction ou avant un `call`, c'est simplement une mise à zéro. Ne cherchez pas de XOR cryptographique. En revanche, un `xor` avec un registre *différent* ou une constante non nulle est une vraie opération logique — potentiellement un chiffrement XOR simple (chapitre 24) ou un calcul de hash.

**Le XOR dans le contexte crypto** :

```asm
; Boucle de déchiffrement XOR typique
.loop:
    xor     byte [rdi+rcx], 0x42    ; déchiffre l'octet courant avec la clé 0x42
    inc     rcx
    cmp     rcx, rax
    jl      .loop
```

Ce pattern — une boucle qui XOR chaque octet d'un buffer avec une constante — est la forme la plus basique de chiffrement/obfuscation. On le retrouve dans les malwares simples et les CTF (chapitre 24 et 27).

### `not` — complément bit à bit

```asm
not     eax               ; eax = ~eax (inverse tous les bits)
```

C'est la traduction de l'opérateur `~` en C. Moins fréquent que les autres opérations logiques, mais on le croise dans des calculs de masques et dans certaines optimisations.

---

## Instructions de décalage

Les décalages déplacent les bits d'un registre vers la gauche ou la droite. Ils sont omniprésents dans le code optimisé car **un décalage à gauche de N positions équivaut à une multiplication par 2ᴺ**, et **un décalage à droite de N positions équivaut à une division par 2ᴺ** — des opérations infiniment plus rapides qu'un `imul` ou un `div`.

### `shl` / `sal` — décalage à gauche (*Shift Left*)

```asm
shl     eax, 1            ; eax = eax << 1   (× 2)  
shl     eax, 3            ; eax = eax << 3   (× 8)  
shl     eax, cl           ; eax = eax << cl  (décalage variable)  
```

Les bits sortant à gauche sont perdus (le dernier bit sorti va dans CF). Des zéros sont insérés à droite. `shl` et `sal` sont des synonymes — le comportement est identique.

Correspondance C :

```c
x <<= 3;           // →  shl  eax, 3  
x *= 8;            // →  shl  eax, 3    (GCC optimise automatiquement)  
```

### `shr` — décalage logique à droite (*Shift Right*)

```asm
shr     eax, 1            ; eax = eax >> 1  (÷ 2, non signé)  
shr     eax, 4            ; eax = eax >> 4  (÷ 16, non signé)  
```

Des zéros sont insérés à gauche. C'est le décalage pour les valeurs **non signées**.

### `sar` — décalage arithmétique à droite (*Shift Arithmetic Right*)

```asm
sar     eax, 1            ; eax = eax >> 1  (÷ 2, signé — préserve le signe)  
sar     eax, 0x1f         ; extrait le bit de signe (0 si positif, -1 si négatif)  
```

Le bit de signe (bit de poids fort) est répliqué à gauche. C'est le décalage pour les valeurs **signées** — il préserve le signe du nombre.

> 💡 **Pour le RE** : la distinction `shr` vs `sar` vous révèle la signedness de la variable. Si GCC utilise `shr`, la variable est `unsigned`. S'il utilise `sar`, elle est `signed` (ou le compilateur traite la valeur comme signée dans ce contexte). C'est un indice précieux pour reconstruire les types.

### Le pattern `shr reg, 0x1f` (ou `sar reg, 0x1f`)

Ce pattern extrait le bit de signe d'une valeur 32 bits :

```asm
sar     eax, 0x1f          ; eax = 0x00000000 si positif, 0xFFFFFFFF si négatif
```

On le voit dans les séquences de division par constante (ajustement pour les nombres négatifs) et dans les calculs de valeur absolue. Ne le confondez pas avec une division par 2³¹.

### Multiplications et divisions par puissances de 2

GCC remplace systématiquement les multiplications et divisions par des puissances de 2 par des décalages :

| Opération C | Instruction GCC | Équivalent |  
|---|---|---|  
| `x * 2` | `shl eax, 1` ou `add eax, eax` | Décalage gauche de 1 |  
| `x * 4` | `shl eax, 2` | Décalage gauche de 2 |  
| `x * 8` | `shl eax, 3` | Décalage gauche de 3 |  
| `x / 2` (unsigned) | `shr eax, 1` | Décalage droit logique de 1 |  
| `x / 4` (signed) | `sar eax, 2` (+ ajustement) | Décalage droit arithmétique |  
| `x % 4` (unsigned) | `and eax, 3` | Masque des bits bas |

Le remplacement de `x % puissance_de_2` par `and eax, (puissance - 1)` est particulièrement courant. Par exemple, `n % 16` devient `and eax, 0xf` — c'est instantané alors que `div` coûterait des dizaines de cycles.

> ⚠️ **Piège** : la division signée par une puissance de 2 n'est pas un simple `sar`. Pour les nombres négatifs, le C arrondit vers zéro, alors que `sar` arrondit vers -∞. GCC insère donc un ajustement :  
>  
> ```asm  
> ; x / 4 (signed)  
> mov     eax, edi  
> sar     eax, 0x1f        ; eax = bit de signe étendu (0 ou -1)  
> shr     eax, 0x1e        ; eax = 0 si positif, 3 si négatif (ajustement)  
> add     eax, edi          ; ajoute l'ajustement à x  
> sar     eax, 2            ; décalage arithmétique de 2  
> ```  
>  
> Ce pattern est mécanique et reconnaissable une fois qu'on l'a vu une première fois.

---

## Instructions de comparaison

### `cmp` — comparaison (soustraction sans résultat)

```
cmp  opérande1, opérande2     ; calcule opérande1 - opérande2, met à jour les flags, jette le résultat
```

`cmp` est fondamentalement identique à `sub`, à une différence près : **le résultat de la soustraction est jeté**. Seuls les flags sont affectés. C'est l'instruction qui précède presque systématiquement un saut conditionnel.

```asm
cmp     eax, 0x2a              ; compare eax avec 42  
jz      .equal                  ; saute si eax == 42 (ZF = 1)  

cmp     dword [rbp-0x4], 0     ; compare une variable locale avec 0  
jle     .negative_or_zero       ; saute si var <= 0  

cmp     rdi, rsi               ; compare deux pointeurs  
je      .same_pointer           ; saute s'ils sont égaux  
```

La paire `cmp` + saut conditionnel se lit naturellement comme un `if` en C :

| Assembleur | Condition C |  
|---|---|  
| `cmp eax, ebx` / `je .L` | `if (a == b)` |  
| `cmp eax, ebx` / `jne .L` | `if (a != b)` |  
| `cmp eax, ebx` / `jl .L` | `if (a < b)` — signé |  
| `cmp eax, ebx` / `jge .L` | `if (a >= b)` — signé |  
| `cmp eax, ebx` / `jb .L` | `if (a < b)` — non signé |  
| `cmp eax, ebx` / `jae .L` | `if (a >= b)` — non signé |

La table complète des sauts conditionnels est en section 3.4.

### `test` — ET logique sans résultat

```
test  opérande1, opérande2    ; calcule opérande1 AND opérande2, met à jour les flags, jette le résultat
```

`test` est à `and` ce que `cmp` est à `sub` : il effectue un ET logique, met à jour les flags, mais ne stocke pas le résultat. Ses deux usages dominants :

**Usage 1 — Tester la nullité d'un registre**

```asm
test    rax, rax           ; rax AND rax = rax → ZF = 1 si rax == 0  
jz      .is_null            ; saute si rax est NULL  
```

C'est l'idiome standard de GCC pour `if (ptr == NULL)` ou `if (value == 0)`. On le voit aussi sous la forme `test eax, eax` pour des valeurs 32 bits.

Pourquoi `test rax, rax` plutôt que `cmp rax, 0` ? Les deux donnent le même résultat sur ZF, mais `test` a un encodage plus court (pas d'immédiat à encoder) et le processeur le traite plus efficacement.

**Usage 2 — Tester un bit spécifique**

```asm
test    eax, 0x1           ; teste le bit 0 (parité)  
jnz     .is_odd             ; saute si le bit 0 est à 1 → nombre impair  

test    eax, 0x4           ; teste le bit 2  
jz      .flag_not_set       ; saute si le bit 2 est à 0  
```

Correspondance C :

```c
if (n % 2 != 0)        // →  test eax, 0x1  /  jnz  
if (flags & FLAG_X)    // →  test eax, FLAG_X  /  jnz  
```

> 💡 **Pour le RE** : distinguer `cmp` et `test` est important pour comprendre la nature de la comparaison. `cmp` compare des *valeurs* (égalité, supériorité…). `test` vérifie des *bits* (nullité, présence d'un flag, parité). Les deux précèdent des sauts conditionnels, mais la sémantique est différente.

---

## Récapitulatif : les flags modifiés par chaque instruction

Toutes les instructions arithmétiques et logiques modifient les flags, mais pas toutes de la même manière. Ce tableau résume le comportement pour les instructions de cette section :

| Instruction | ZF | SF | CF | OF | Note |  
|---|---|---|---|---|---|  
| `add`, `sub` | ✓ | ✓ | ✓ | ✓ | Tous les flags arithmétiques |  
| `inc`, `dec` | ✓ | ✓ | — | ✓ | CF **non modifié** |  
| `imul` (2/3 op.) | — | — | ✓ | ✓ | ZF et SF non définis |  
| `div`, `idiv` | — | — | — | — | Tous les flags **non définis** |  
| `and`, `or`, `xor` | ✓ | ✓ | 0 | 0 | CF et OF toujours mis à 0 |  
| `shl`, `shr`, `sar` | ✓ | ✓ | ✓ | * | OF défini seulement pour shift de 1 |  
| `cmp` | ✓ | ✓ | ✓ | ✓ | Identique à `sub` |  
| `test` | ✓ | ✓ | 0 | 0 | Identique à `and` |  
| `neg` | ✓ | ✓ | ✓ | ✓ | CF = 1 sauf si opérande = 0 |  
| `not` | — | — | — | — | **Aucun** flag modifié |

En RE courant, vous n'avez pas besoin de mémoriser cette table. Retenez simplement que **`cmp` et `test` sont les deux instructions « officielles » pour positionner les flags avant un saut**, et que les opérations arithmétiques standard (`add`, `sub`) le font aussi — ce qui explique pourquoi GCC omet parfois le `cmp` quand un `sub` précédent a déjà positionné les bons flags.

---

## Idiomes GCC à reconnaître

GCC produit des séquences récurrentes qui, sorties de leur contexte, peuvent sembler cryptiques. Voici les plus fréquentes impliquant les instructions de cette section :

### Mise à zéro

```asm
xor     eax, eax               ; eax = 0 (et rax = 0 par extension)
```

Toujours préféré à `mov eax, 0`. C'est *le* pattern le plus courant du x86.

### Test de nullité

```asm
test    rax, rax                ; positionne ZF selon que rax est nul ou non
```

Toujours préféré à `cmp rax, 0`.

### Multiplication par petite constante (sans `imul`)

GCC combine `lea`, `add` et `shl` pour les petits multiplicateurs :

| Opération C | Code GCC typique |  
|---|---|  
| `x * 2` | `add eax, eax` ou `shl eax, 1` |  
| `x * 3` | `lea eax, [rdi+rdi*2]` |  
| `x * 5` | `lea eax, [rdi+rdi*4]` |  
| `x * 9` | `lea eax, [rdi+rdi*8]` |  
| `x * 6` | `lea eax, [rdi+rdi*2]` puis `add eax, eax` |  
| `x * 7` | `lea eax, [rdi*8]` puis `sub eax, edi` |  
| `x * 10` | `lea eax, [rdi+rdi*4]` puis `add eax, eax` |

Ces combinaisons exploitent le `lea` avec scale (×2, ×4, ×8) vu en section 3.2, chaîné avec des additions ou décalages. GCC les préfère à `imul` parce qu'elles évitent l'utilisation de l'unité de multiplication, et les processeurs modernes exécutent `lea` et `add` avec une latence de 1 cycle.

### Division par constante (la « multiplication magique »)

Résumé du pattern vu plus haut — les constantes magiques les plus fréquentes :

| Diviseur | Constante magique (`imul`) | Décalage après |  
|---|---|---|  
| 3 | `0x55555556` | `shr 0` ou ajustement signe |  
| 5 | `0x66666667` | `sar 1` |  
| 7 | `0x92492493` | `sar 2` + ajustement |  
| 10 | `0x66666667` | `sar 2` |  
| 100 | `0x51EB851F` | `sar 5` |

Quand vous repérez une de ces constantes dans un `imul`, vous pouvez directement en déduire le diviseur original.

### Valeur absolue sans branchement

```asm
; abs(eax)
mov     edx, eax  
sar     edx, 0x1f        ; edx = 0 si positif, -1 si négatif  
xor     eax, edx          ; inverse tous les bits si négatif  
sub     eax, edx          ; +1 si négatif (complète le complément à deux)  
```

Ce pattern calcule `abs(x)` sans aucun branchement, en exploitant les propriétés du complément à deux et du XOR. Si `x` est positif, `edx` vaut 0, le `xor` et le `sub` ne changent rien. Si `x` est négatif, `edx` vaut -1 (`0xFFFFFFFF`), le `xor` inverse tous les bits, et le `sub` ajoute 1 — ce qui donne exactement `-x`.

---

## Ce qu'il faut retenir pour la suite

1. **`add`/`sub`** sont les opérations de base — elles modifient les flags et servent aussi implicitement de comparaisons.  
2. **`imul`** à deux ou trois opérandes est la forme standard de la multiplication GCC — `mul` est rare.  
3. **`div`/`idiv`** sont lentes et GCC les remplace par des « multiplications magiques » dès `-O1` — reconnaître les constantes magiques est une compétence RE essentielle.  
4. **`xor eax, eax`** = mise à zéro, **`test reg, reg`** = test de nullité — ce sont des idiomes, pas de la logique cryptographique.  
5. **`shl`/`shr`/`sar`** remplacent les multiplications et divisions par des puissances de 2 — `shr` indique un type non signé, `sar` un type signé.  
6. **`cmp`** positionne les flags pour une comparaison de valeurs, **`test`** pour une vérification de bits — les deux précèdent des sauts conditionnels (section 3.4).  
7. **Le choix d'instruction par GCC trahit le type** : `shr` vs `sar` → signé vs non signé, `movzx` vs `movsx` → unsigned vs signed, `jb`/`ja` vs `jl`/`jg` → comparaison non signée vs signée.

---


⏭️ [Sauts conditionnels et inconditionnels : `jmp`, `jz`/`jnz`, `jl`, `jge`, `jle`, `ja`…](/03-assembleur-x86-64/04-sauts-conditionnels.md)
