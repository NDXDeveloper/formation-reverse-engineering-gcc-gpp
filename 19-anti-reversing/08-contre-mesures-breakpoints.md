🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.8 — Contre-mesures aux breakpoints (self-modifying code, int3 scanning)

> 🎯 **Objectif** : Comprendre comment un binaire peut détecter ou neutraliser les breakpoints posés par un débogueur, connaître les deux grandes familles de contre-mesures — scanning d'opcodes et code auto-modifiant — et maîtriser les techniques de contournement qui permettent de déboguer malgré ces protections.

---

## Comment fonctionne un breakpoint logiciel

Pour comprendre les contre-mesures, il faut d'abord comprendre ce qu'elles ciblent. Un breakpoint logiciel (software breakpoint) est le type de breakpoint le plus courant dans GDB. Son fonctionnement est mécanique :

1. L'analyste demande un breakpoint à une adresse donnée (`break *0x401234`).  
2. GDB **sauvegarde** l'octet original à cette adresse.  
3. GDB **écrit** l'opcode `0xCC` (instruction `int3`) à la place du premier octet de l'instruction ciblée.  
4. Quand le processeur atteint cette adresse, il exécute `int3`, ce qui génère un signal `SIGTRAP`.  
5. Le kernel notifie GDB (via `ptrace`). GDB reprend le contrôle.  
6. GDB **restaure** l'octet original, affiche l'état du programme et attend les commandes de l'analyste.

Le point crucial est l'étape 3 : le breakpoint modifie physiquement le code en mémoire. L'octet `0xCC` est écrit dans la section `.text` du processus. Cet octet est visible par le processus lui-même s'il inspecte sa propre mémoire. C'est cette propriété que les contre-mesures exploitent.

## Technique 1 — Scanning d'opcodes `int3` (`0xCC`)

### Principe

L'idée est directe : le binaire lit ses propres instructions en mémoire et cherche l'octet `0xCC`. Si `0xCC` apparaît là où il ne devrait pas être, c'est qu'un débogueur a posé un breakpoint.

### Implémentation dans notre binaire

Notre binaire `anti_reverse.c` implémente cette technique dans `scan_int3` :

```c
static int scan_int3(void)
{
    const uint8_t *fn_ptr = (const uint8_t *)verify_password;

    for (int i = 0; i < 128; i++) {
        if (fn_ptr[i] == 0xCC) {
            return 1; /* breakpoint détecté */
        }
    }
    return 0;
}
```

Le binaire caste le pointeur de la fonction `verify_password` en pointeur d'octets, puis parcourt les 128 premiers octets à la recherche de `0xCC`. Si GDB a posé un breakpoint n'importe où dans les 128 premiers octets de `verify_password`, le scan le détecte.

### Le problème des faux positifs

L'octet `0xCC` n'est pas exclusivement l'opcode de `int3`. Il peut apparaître légitimement dans le code machine :

- Comme octet d'un opérande : `mov eax, 0x004011CC` contient `0xCC` dans l'encodage de l'adresse immédiate  
- Comme partie d'un opcode multi-octets : dans certaines instructions SSE/AVX, `0xCC` peut apparaître comme préfixe ou octet interne  
- Comme padding : GCC insère parfois des `int3` (`0xCC`) entre les fonctions pour aligner le code. Ces `0xCC` sont légitimes et ne sont pas des breakpoints.

Un scanner naïf qui cherche `0xCC` octet par octet produira des faux positifs. Les implémentations plus sophistiquées désassemblent leur propre code pour ne vérifier que les premiers octets de chaque instruction (là où GDB écrit le breakpoint), ou comparent le code en mémoire avec une copie de référence stockée ailleurs.

Notre implémentation est volontairement simpliste pour rester pédagogique. Un scanner en production utiliserait un désassembleur intégré ou un hash de référence.

### Reconnaître la technique dans le désassemblage

Le pattern est caractéristique :

```nasm
; Chargement de l'adresse de la fonction à scanner
lea    rdi, [rip+0x1234]       ; adresse de verify_password
; Boucle de scanning
xor    ecx, ecx                ; compteur i = 0
.loop:
  movzx  eax, byte [rdi+rcx]  ; lire l'octet à fn_ptr[i]
  cmp    al, 0xCC              ; comparer à int3
  je     .breakpoint_found     ; breakpoint détecté !
  inc    ecx
  cmp    ecx, 128              ; ou une autre limite
  jl     .loop
```

Les indices clés sont :

- Un pointeur vers une fonction du binaire lui-même (pas vers des données ou une bibliothèque externe) chargé en registre  
- Une boucle qui lit des octets individuels à partir de ce pointeur avec `movzx` ou `mov byte`  
- Une comparaison de chaque octet avec la constante `0xCC`  
- Un branchement vers un chemin d'erreur si `0xCC` est trouvé

Dans le décompilateur Ghidra, le pattern apparaît clairement comme une boucle parcourant un tableau d'octets avec une comparaison contre `0xCC` — même sans symboles, le cast vers `uint8_t*` et la constante `0xCC` sont des signaux forts.

### Contournements

**Méthode 1 — Utiliser des hardware breakpoints**

C'est la méthode la plus propre. Les processeurs x86-64 disposent de quatre registres de débogage matériels (`DR0` à `DR3`) qui permettent de poser des breakpoints sans modifier le code en mémoire. Un hardware breakpoint est invisible pour tout scan de `0xCC` car aucun octet n'est modifié.

```
(gdb) hbreak *0x401234
Hardware assisted breakpoint 1 at 0x401234
```

La commande `hbreak` (au lieu de `break`) dans GDB utilise un hardware breakpoint. La limitation est qu'on ne dispose que de quatre hardware breakpoints simultanés, ce qui peut être contraignant sur une session d'analyse complexe. Il faut les utiliser stratégiquement : les réserver pour les fonctions que le binaire scanne, et utiliser des software breakpoints pour le reste du code.

**Méthode 2 — Poser le breakpoint après le scan**

Si le scan se produit au début de `main()` (comme dans notre binaire), l'analyste peut laisser le scan passer en exécution normale, puis poser ses breakpoints après. Le scan a déjà eu lieu et ne se répétera pas (sauf si le binaire scanne périodiquement).

```
(gdb) break main
(gdb) run
(gdb) # avancer jusqu'après le scan
(gdb) next
(gdb) next
...
(gdb) # maintenant poser les breakpoints sur verify_password
(gdb) break verify_password
(gdb) continue
```

**Méthode 3 — NOP le scan lui-même**

Remplacer la fonction de scan par des `nop` + `xor eax, eax` + `ret` (pour qu'elle retourne 0 = « pas de breakpoint trouvé »). Cela désactive le scan de manière permanente.

Avec GDB, on peut le faire en mémoire sans modifier le fichier :

```
(gdb) # Écraser le début de scan_int3 par : xor eax, eax ; ret
(gdb) set *(unsigned int *)scan_int3 = 0x00C3C031
```

Les octets `31 C0 C3` correspondent à `xor eax, eax` (`31 C0`) suivi de `ret` (`C3`). La fonction retourne immédiatement 0.

**Méthode 4 — Frida**

Frida n'utilise pas de breakpoints `int3` pour son instrumentation. L'`Interceptor` de Frida réécrit le prologue de la fonction cible avec un trampoline (saut vers le hook), pas avec un `0xCC`. Le scan `int3` ne détecte pas Frida.

On peut aussi utiliser Frida pour désactiver le scan lui-même :

```javascript
// Remplacer scan_int3 par une fonction qui retourne 0
var scan_addr = Module.findExportByName(null, "scan_int3");
// Si le binaire est strippé, trouver l'adresse par pattern matching :
// var scan_addr = ptr("0x401234");

Interceptor.replace(scan_addr, new NativeCallback(function() {
    return 0;
}, 'int', []));
```

## Technique 2 — Vérification d'intégrité du code (checksum)

### Principe

Le scanning `0xCC` cherche un octet spécifique. La vérification d'intégrité est plus générale : elle calcule un hash ou un checksum sur une portion du code et compare le résultat à une valeur de référence. Toute modification — qu'il s'agisse d'un breakpoint `0xCC`, d'un `nop` de patching, d'un saut inversé, ou de n'importe quelle altération — change le checksum et déclenche la détection.

### Implémentation dans notre binaire

```c
#define CHECKSUM_LEN 64

static uint32_t compute_checksum(const uint8_t *ptr, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (sum << 3) | (sum >> 29); /* rotation */
        sum ^= ptr[i];
    }
    return sum;
}

static volatile uint32_t expected_checksum = 0;

static int check_code_integrity(void)
{
    if (expected_checksum == 0)
        return 0; /* checksum non initialisé */

    uint32_t actual = compute_checksum(
        (const uint8_t *)verify_password, CHECKSUM_LEN);

    if (actual != expected_checksum) {
        return 1; /* code modifié */
    }
    return 0;
}
```

Le checksum de référence (`expected_checksum`) est calculé lors d'une exécution non déboguée et injecté dans le binaire par un script post-build. À chaque exécution, le binaire recalcule le checksum sur les 64 premiers octets de `verify_password` et compare. Si un seul octet a changé (breakpoint, patch, NOP), le checksum diffère.

### Avantages par rapport au scan `int3`

Le checksum détecte **tout** type de modification, pas seulement les breakpoints :

- Un breakpoint `int3` (`0xCC`) → checksum différent  
- Un saut inversé (`je` → `jne`, opcode `0x74` → `0x75`) → checksum différent  
- Un `nop` inséré pour désactiver une instruction → checksum différent  
- Un inline hook de Frida (réécriture du prologue) → checksum différent

C'est une protection plus robuste que le simple scan `0xCC`, mais elle a ses propres faiblesses.

### Reconnaître la technique dans le désassemblage

Le pattern du checksum est :

```nasm
; Charger l'adresse de la fonction à vérifier
lea    rsi, [rip+0x...]          ; adresse de verify_password  
mov    edx, 64                    ; longueur à vérifier  
; Boucle de calcul du hash
xor    eax, eax                   ; sum = 0
.hash_loop:
  movzx  ecx, byte [rsi]         ; lire un octet du code
  rol    eax, 3                   ; rotation (ou shl+shr+or)
  xor    eax, ecx                ; XOR avec l'octet
  inc    rsi
  dec    edx
  jnz    .hash_loop
; Comparaison avec la valeur attendue
cmp    eax, dword [rip+0x...]    ; expected_checksum  
jne    .integrity_fail  
```

Les indices sont :

- Un pointeur vers le code du binaire lui-même (comme pour le scan `int3`)  
- Une boucle d'accumulation qui lit des octets et les combine (XOR, rotation, addition — les opérations typiques d'un hash simple)  
- Une comparaison du résultat avec une constante stockée dans `.data` ou `.rodata`  
- Un branchement vers un chemin d'erreur en cas de non-correspondance

### Contournements

**Méthode 1 — Hardware breakpoints**

Comme pour le scan `int3`, les hardware breakpoints ne modifient pas le code en mémoire. Le checksum reste identique. C'est la solution la plus directe.

**Méthode 2 — Patcher le checksum attendu**

Si on connaît la valeur du checksum avec nos modifications appliquées, on peut mettre à jour `expected_checksum` pour qu'il corresponde au nouveau code. Dans GDB :

```
(gdb) # Poser un breakpoint (qui modifie le code)
(gdb) break *verify_password
(gdb) # Calculer le nouveau checksum du code modifié
(gdb) # ... ou simplement patcher expected_checksum à 0
(gdb) # pour désactiver le check (0 = skip)
(gdb) set *(int*)&expected_checksum = 0
(gdb) continue
```

Dans notre implémentation, la valeur `0` pour `expected_checksum` désactive le check. C'est une faiblesse intentionnelle pour la pédagogie. Une implémentation robuste n'aurait pas cette échappatoire.

**Méthode 3 — Désactiver la fonction de vérification**

Même approche que pour le scan `int3` : écraser le début de `check_code_integrity` avec `xor eax, eax; ret` pour qu'elle retourne toujours 0.

**Méthode 4 — Restaurer le code avant le check**

Si l'analyste sait quand le checksum est vérifié, il peut restaurer les octets originaux juste avant le check (retirer les breakpoints temporairement), laisser le check passer, puis remettre ses breakpoints. GDB fait déjà une partie de ce travail quand il restore l'octet original lors d'un `continue`, mais les breakpoints multiples ou les breakpoints dans la zone vérifiée nécessitent une gestion manuelle.

Un script GDB Python peut automatiser cette danse :

```python
import gdb

class ChecksumBypass(gdb.Breakpoint):
    """Breakpoint qui se désactive pendant la vérification d'intégrité."""
    def __init__(self, addr, check_start, check_end):
        super().__init__("*" + hex(addr))
        self.check_start = check_start
        self.check_end = check_end

    def stop(self):
        # Vérifier si on est dans la zone de vérification
        rip = int(gdb.parse_and_eval("$rip"))
        if self.check_start <= rip <= self.check_end:
            return False  # ne pas s'arrêter pendant le check
        return True
```

## Technique 3 — Self-modifying code (code auto-modifiant)

### Principe

Le code auto-modifiant est une technique où le programme modifie ses propres instructions en mémoire pendant l'exécution. La séquence typique est :

1. Le code critique est stocké sous forme chiffrée ou encodée dans le binaire.  
2. Au runtime, une routine de déchiffrement décode les instructions.  
3. Le code déchiffré s'exécute.  
4. Optionnellement, le code est rechiffré après exécution pour ne pas rester en clair en mémoire.

L'impact sur le reverse engineering est double :

- **L'analyse statique est inefficace** — Le désassembleur (Ghidra, objdump) analyse le code tel qu'il est dans le fichier. Si ce code est chiffré, le désassemblage produit du bruit sans signification.  
- **Les breakpoints sont instables** — Si le binaire réécrit la zone de code où un breakpoint est posé, le breakpoint est écrasé. De plus, l'octet `0xCC` du breakpoint sera interprété comme un octet chiffré/encodé, ce qui corrompra le déchiffrement et produira un code déchiffré invalide.

### Implémentation sur Linux

Le code auto-modifiant nécessite que la page mémoire contenant le code soit à la fois writable et executable. Sur un système avec NX activé (ce qui est le défaut), les pages `.text` sont `R-X` (read-execute, pas write). Le binaire doit appeler `mprotect` pour ajouter le droit d'écriture :

```c
#include <sys/mman.h>
#include <unistd.h>

void decrypt_code(void *func_addr, size_t len, uint8_t key) {
    /* Rendre la page writable */
    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)func_addr & ~(page_size - 1));
    mprotect(page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);

    /* Déchiffrer le code (XOR simple pour l'exemple) */
    uint8_t *ptr = (uint8_t *)func_addr;
    for (size_t i = 0; i < len; i++) {
        ptr[i] ^= key;
    }

    /* Optionnel : remettre en read-execute seul */
    mprotect(page_start, page_size, PROT_READ | PROT_EXEC);
}
```

L'appel à `mprotect` avec `PROT_WRITE | PROT_EXEC` est un signal d'alarme majeur dans un triage. Des pages simultanément writable et executable sont rares dans du code légitime et indiquent soit du code auto-modifiant, soit un JIT compiler, soit un packer.

### Reconnaître le code auto-modifiant dans le désassemblage

Les indices dans l'analyse statique sont :

- **Appel à `mprotect@plt`** visible dans les imports dynamiques. L'argument `prot` (troisième argument, dans `edx`) contient `0x7` (`PROT_READ | PROT_WRITE | PROT_EXEC`). La constante `0x7` dans le troisième argument de `mprotect` est le signal le plus fiable.

- **Zones de code illisibles** — Le désassembleur produit des instructions invalides ou absurdes dans certaines zones. C'est le code chiffré. Ghidra peut marquer ces zones comme « bad instructions » ou les interpréter comme des données.

- **Pattern XOR en boucle sur le code** — Une boucle qui lit et réécrit des octets à partir d'une adresse dans `.text`, souvent avec un XOR ou une opération de déchiffrement :

```nasm
; Routine de déchiffrement typique
lea    rdi, [rip+0x...]      ; adresse du code chiffré  
mov    ecx, 256               ; longueur  
mov    al, 0x37               ; clé XOR  
.decrypt_loop:
  xor    byte [rdi], al       ; déchiffrer un octet
  inc    rdi
  dec    ecx
  jnz    .decrypt_loop
```

- **Appel suivi d'exécution de la zone déchiffrée** — Après la boucle de déchiffrement, un `call` ou un `jmp` vers l'adresse du code fraîchement déchiffré.

### Contournements

**Méthode 1 — Dump après déchiffrement**

La stratégie la plus simple : laisser le code se déchiffrer, puis récupérer le résultat.

1. Poser un hardware breakpoint sur l'instruction qui suit le déchiffrement (le `call` ou `jmp` vers le code déchiffré).  
2. Lancer le programme. Le déchiffrement s'exécute normalement.  
3. Au breakpoint, le code est en clair en mémoire. Le dumper avec GDB :

```
(gdb) dump memory decrypted_func.bin 0x<start> 0x<end>
```

On peut alors désassembler le dump séparément ou l'analyser dans Ghidra.

**Méthode 2 — Breakpoint sur `mprotect`**

L'appel à `mprotect` est le moment charnière : il précède le déchiffrement. En posant un breakpoint sur `mprotect`, on identifie les adresses et tailles des zones qui seront modifiées (les arguments de `mprotect` donnent l'adresse de base et la taille de la page).

```
(gdb) break mprotect
(gdb) run
(gdb) # Inspecter les arguments
(gdb) info registers rdi rsi rdx
# rdi = adresse de la page
# rsi = taille
# rdx = protections (0x7 = RWX)
(gdb) # Continuer jusqu'après le déchiffrement
(gdb) finish
(gdb) # Maintenant le code est déchiffré, on peut l'analyser
```

**Méthode 3 — Émulation de la routine de déchiffrement**

Si l'algorithme de déchiffrement est identifié (XOR simple, AES, RC4…), on peut le reproduire hors du binaire. Extraire le blob chiffré du fichier ELF, appliquer le déchiffrement en Python, et analyser le résultat statiquement sans jamais exécuter le binaire.

```python
with open("anti_reverse", "rb") as f:
    data = bytearray(f.read())

# Offset et taille du code chiffré (identifiés dans Ghidra)
start = 0x1234  
length = 256  
key = 0x37  

for i in range(length):
    data[start + i] ^= key

# Écrire le binaire déchiffré
with open("anti_reverse_decrypted", "wb") as f:
    f.write(data)
```

**Méthode 4 — Frida avec Memory.protect**

Frida peut intercepter les modifications de permissions mémoire et le déchiffrement :

```javascript
Interceptor.attach(Module.findExportByName(null, "mprotect"), {
    onEnter: function(args) {
        var addr = args[0];
        var size = args[1].toInt32();
        var prot = args[2].toInt32();
        if (prot === 7) { // PROT_READ|PROT_WRITE|PROT_EXEC
            console.log("[*] mprotect RWX sur " + addr +
                        " (taille: " + size + ")");
            this.target_addr = addr;
            this.target_size = size;
        }
    },
    onLeave: function(retval) {
        if (this.target_addr) {
            // Dumper la zone après que le code aura été déchiffré
            // (on peut mettre un hook plus loin pour le timing)
            console.log("[*] Zone RWX prête, le déchiffrement va suivre");
        }
    }
});
```

## Combinaison des techniques

Les trois techniques de cette section se renforcent mutuellement quand elles sont combinées :

- Le **scan `int3`** détecte les software breakpoints sur les fonctions ciblées.  
- Le **checksum** détecte tout type de modification, y compris les patches et les inline hooks.  
- Le **code auto-modifiant** empêche l'analyse statique et rend les breakpoints instables.

Un binaire qui combine les trois force l'analyste à travailler presque exclusivement avec des hardware breakpoints, à exécuter le code en pleine vitesse (pas de single-step), et à attendre que le code se déchiffre avant de pouvoir l'analyser.

Malgré cela, l'analyste a toujours des prises. Le code auto-modifiant doit bien se déchiffrer à un moment donné — et à ce moment, il est en clair en mémoire. Le checksum doit bien lire le code — et on peut forcer son résultat. Le scan `int3` ne détecte pas les hardware breakpoints. Chaque protection a son talon d'Achille.

## Synthèse : breakpoints logiciels vs hardware

Le tableau suivant résume la visibilité des deux types de breakpoints face à chaque contre-mesure :

| Contre-mesure | Software breakpoint (`0xCC`) | Hardware breakpoint (`DR0–DR3`) |  
|---|---|---|  
| Scan `int3` | Détecté | Invisible |  
| Checksum de code | Détecté | Invisible |  
| Self-modifying code | Écrasé par le déchiffrement | Survit au déchiffrement |  
| Détection des registres DR* | Invisible | Détectable (lecture de `DR7`) |

La dernière ligne mentionne une technique que nous n'avons pas implémentée dans notre binaire mais qui existe : certains programmes lisent les registres de débogage (`DR0`–`DR3` et le registre de contrôle `DR7`) pour détecter les hardware breakpoints. Sur Linux, un processus ne peut pas lire ses propres registres DR directement (ils sont accessibles uniquement via `ptrace`), ce qui rend cette détection plus complexe. Certains malwares utilisent des exceptions provoquées intentionnellement et inspectent le contexte de signal pour extraire les valeurs des registres DR, mais cette technique reste rare.

La règle pratique pour l'analyste : face à un binaire avec des contre-mesures aux breakpoints, commencer par les hardware breakpoints. Si les quatre slots sont insuffisants, combiner avec des breakpoints logiciels posés uniquement en dehors des zones scannées ou vérifiées.

---


⏭️ [Inspecter l'ensemble des protections avec `checksec` avant toute analyse](/19-anti-reversing/09-checksec-audit-complet.md)
