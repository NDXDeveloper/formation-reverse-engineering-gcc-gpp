🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 34.5 — Strings en Go : structure `(ptr, len)` et implications pour `strings`

> 🐹 *La commande `strings` est le réflexe numéro un du reverse engineer face à un binaire inconnu (chapitre 5). Sur un binaire C, elle fonctionne remarquablement bien. Sur un binaire Go, elle produit un résultat trompeur — des chaînes fusionnées, des fragments incompréhensibles, des faux positifs par milliers. Comprendre pourquoi, et savoir extraire les vraies chaînes, est une compétence indispensable pour le RE Go.*

---

## Rappel : les strings en C vs en Go

### Le modèle C : null-terminé

En C, une chaîne est un tableau d'octets terminé par un octet nul (`\0`). Le compilateur place chaque string littérale dans `.rodata` avec son terminateur, et le code la référence par un simple pointeur :

```
.rodata :
  0x1000: 48 65 6C 6C 6F 00         "Hello\0"
  0x1006: 57 6F 72 6C 64 00         "World\0"
```

La commande `strings` cherche exactement ce pattern : des séquences d'octets imprimables de longueur minimale (par défaut 4), terminées par un null ou une fin de section. Sur du C, chaque chaîne est nettement délimitée — `strings` les retrouve fidèlement.

### Le modèle Go : `(ptr, len)`

En Go, une string est un header de 16 octets (section 34.3) :

```
┌──────────────────┐
│  ptr    (8 oct.) │ → pointe vers les données UTF-8
├──────────────────┤
│  len    (8 oct.) │ → longueur en octets
└──────────────────┘
```

Les données pointées ne sont **pas** null-terminées. La longueur est portée par le header, pas par un octet sentinelle. Cela a une conséquence directe : le compilateur Go n'a aucune raison d'insérer un `\0` entre deux chaînes consécutives dans `.rodata`.

---

## Le blob de chaînes : comment GCC/Go stocke les littéraux

### Concaténation dans `.rodata`

Le compilateur Go regroupe toutes les string littérales du programme dans une zone contiguë de `.rodata` (ou `.go.string`). Les chaînes sont stockées bout à bout, sans séparateur :

```
.rodata (extrait de crackme_go) :
  0x4A000: 63 68 65 63 6B 73 75 6D 20 64 65 20 67 72 6F 75
           "checksum de grou"
  0x4A010: 70 65 20 69 6E 76 61 6C 69 64 65 2E 43 6F 6E 74
           "pe invalide.Cont"    ← pas de \0 entre les deux chaînes
  0x4A020: 72 61 69 6E 74 65 20 64 27 6F 72 64 72 65 42 72
           "rainte d'ordreBr"    ← "d'ordre" enchaîne directement sur "Br"
  0x4A030: 61 76 6F 2C 20 72 65 76 65 72 73 65 72 20 21 ...
           "avo, reverser !"
```

Aucun `\0` entre les chaînes. Chaque utilisation dans le code référence un offset et une longueur dans ce blob :

```asm
; Référence à "clé invalide" (13 octets à partir de 0x4A010)
LEA     RAX, [0x4A010]         ; ptr  
MOV     RBX, 13                ; len = 13  
```

### Partage de sous-chaînes (interning partiel)

Le compilateur peut aller plus loin : si une chaîne est un suffixe ou un sous-ensemble d'une autre, il réutilise le même stockage. Par exemple, si le programme contient les littéraux `"format invalide"` et `"invalide"`, le compilateur peut stocker uniquement `"format invalide"` et faire pointer `"invalide"` vers l'offset `+7` dans les mêmes données.

Ce partage est invisible au niveau du code source mais produit des pointeurs qui atterrissent au milieu d'une autre chaîne — ce qui rend toute reconstruction naïve encore plus difficile.

---

## Pourquoi `strings` échoue sur les binaires Go

### Problème 1 : fusion de chaînes

Sans séparateur null, `strings` voit une longue séquence contiguë de caractères imprimables et la rapporte comme une seule chaîne géante :

```bash
$ strings crackme_go_strip | head -5
Erreur de formatclé invalideChecksums de groupes valides.Contrainte  
d'ordre respectée.Vérification croisée OK.Clé valide ! Bravo, reve  
rser !Usage : ...  
```

Au lieu de sept chaînes distinctes, `strings` produit un bloc illisible. L'analyste doit deviner où une chaîne finit et où la suivante commence.

### Problème 2 : volume de bruit

Un binaire Go embarque la bibliothèque standard et le runtime. La section `.rodata` contient des milliers de chaînes internes : messages d'erreur du runtime, noms de fonctions, descripteurs de type, chaînes de format de `fmt`, messages de panique, noms de fichiers source du compilateur (`/usr/local/go/src/runtime/...`). Sur un `crackme_go` typique :

```bash
$ strings crackme_go | wc -l
12847

$ strings crackme_go_strip | wc -l
11203
```

Parmi ces milliers de lignes, seules quelques dizaines proviennent du code métier. Le ratio signal/bruit est catastrophique.

### Problème 3 : faux positifs binaires

Comme `strings` ne fait que chercher des séquences d'octets imprimables, les données binaires (opcodes, tables de hachage, constantes numériques) peuvent accidentellement former des séquences qui ressemblent à du texte. Ce phénomène existe en C aussi, mais il est amplifié en Go par la taille du binaire.

### Problème 4 : chaînes UTF-8 multi-octets

Go traite les strings comme des séquences d'octets UTF-8. Les caractères non-ASCII (accents, emojis, CJK) sont encodés sur 2 à 4 octets. L'option par défaut de `strings` (recherche ASCII) peut tronquer ou ignorer ces chaînes. L'option `-e S` (UTF-8 encoding) aide partiellement, mais ne résout pas les autres problèmes.

---

## Techniques d'extraction correcte des strings Go

### Technique 1 : extraction via `gopclntab` et les références assembleur

La méthode la plus fiable consiste à retrouver les chaînes en suivant les références dans le code désassemblé. Chaque string littérale utilisée dans le programme produit une paire d'instructions caractéristique :

```asm
; Pattern nouvelle ABI (Go ≥ 1.17) — string en argument
LEA     RAX, [rip + offset_dans_rodata]   ; ptr  
MOV     RBX, longueur_immédiate            ; len  
```

```asm
; Pattern ancienne ABI (Go < 1.17) — string sur la pile
LEA     RAX, [rip + offset_dans_rodata]  
MOV     [RSP+arg_offset], RAX              ; ptr sur la pile  
MOV     QWORD PTR [RSP+arg_offset+8], len  ; len sur la pile  
```

En cherchant ces patterns dans le désassemblage, vous pouvez extraire chaque paire (adresse, longueur) et reconstruire la chaîne exacte.

**Script Ghidra Python pour extraire les strings Go :**

```python
# extract_go_strings.py — Script Ghidra
# Cherche les patterns LEA + MOV imm qui indiquent des string literals Go.

from ghidra.program.model.scalar import Scalar  
import re  

listing = currentProgram.getListing()  
mem = currentProgram.getMemory()  
results = []  

# Itérer sur toutes les instructions du segment .text
text_block = mem.getBlock(".text")  
if text_block is None:  
    print("Section .text non trouvée")
else:
    inst_iter = listing.getInstructions(text_block.getStart(), True)
    prev_inst = None

    while inst_iter.hasNext():
        inst = inst_iter.next()
        # Chercher : LEA REG, [addr] suivi de MOV REG, immediate
        if prev_inst is not None:
            prev_mn = prev_inst.getMnemonicString()
            curr_mn = inst.getMnemonicString()

            if prev_mn == "LEA" and curr_mn == "MOV":
                # Vérifier que le MOV a un opérande immédiat (la longueur)
                num_ops = inst.getNumOperands()
                if num_ops >= 2:
                    scalar = inst.getScalar(1)
                    if scalar is not None:
                        str_len = scalar.getUnsignedValue()
                        if 1 <= str_len <= 4096:
                            # Lire les références du LEA
                            refs = prev_inst.getReferencesFrom()
                            for ref in refs:
                                addr = ref.getToAddress()
                                try:
                                    buf = bytearray(str_len)
                                    mem.getBytes(addr, buf)
                                    s = buf.decode('utf-8', errors='replace')
                                    if all(c.isprintable() or c in '\n\r\t' for c in s):
                                        results.append((addr, str_len, s))
                                except:
                                    pass
        prev_inst = inst

    print("Strings Go extraites : {}".format(len(results)))
    for addr, length, s in sorted(results, key=lambda x: x[0]):
        print("  0x{} [{}] : {}".format(addr, length, repr(s)))
```

Ce script est une heuristique — il ne capture pas 100 % des chaînes (certaines sont chargées indirectement, via des structures ou des tables), mais il extrait la grande majorité des littéraux utilisés dans le code applicatif.

### Technique 2 : extraction via les structures `runtime.stringStruct`

Les variables globales et les champs de structures contenant des strings sont stockés dans `.data` ou `.noptrdata` sous forme de headers `(ptr, len)`. En scannant ces sections à la recherche de paires (pointeur vers `.rodata`, entier raisonnable), vous pouvez reconstruire des strings supplémentaires :

```python
# Pseudo-code pour scanner les string headers dans .data
for offset in range(data_start, data_end, 8):
    ptr = read_uint64(offset)
    length = read_uint64(offset + 8)
    if rodata_start <= ptr < rodata_end and 1 <= length <= 10000:
        s = read_bytes(ptr, length).decode('utf-8', errors='replace')
        if is_printable(s):
            print(f"String header à 0x{offset:x}: [{length}] {s!r}")
```

### Technique 3 : GoReSym et les strings

GoReSym (section 34.4) n'extrait pas directement les string littérales du code, mais il fournit :

- les **noms de fonctions** (qui sont eux-mêmes des strings dans la table des noms de `gopclntab`),  
- les **noms de types** (dans les descripteurs `runtime._type`),  
- les **noms de fichiers source** (dans la file table).

Ces chaînes sont extraites proprement, avec leurs délimitations correctes.

### Technique 4 : `strings` amélioré avec filtrage

Si vous devez utiliser `strings` malgré ses limitations, combinez-le avec un filtrage intelligent pour réduire le bruit :

```bash
# Exclure les chemins du runtime et de la stdlib
strings -n 6 crackme_go_strip | grep -v '/usr/local/go' \
    | grep -v 'runtime\.' | grep -v 'internal/' \
    | grep -v 'sync\.' | grep -v 'syscall\.' \
    | grep -v 'encoding/' | grep -v 'unicode/' \
    | grep -v 'reflect\.' | grep -v 'errors\.'
```

```bash
# Chercher des patterns spécifiques au code métier
strings -n 6 crackme_go_strip | grep -iE 'key|license|password|valid|error|flag|secret'
```

```bash
# Limiter aux chaînes de longueur raisonnable (pas les blobs fusionnés)
strings -n 6 crackme_go_strip | awk 'length < 80'
```

Ces commandes ne résolvent pas le problème de fusion, mais réduisent significativement le bruit.

### Technique 5 : extraction dynamique avec GDB

En analyse dynamique, les strings sont plus faciles à capturer — elles apparaissent dans les registres et sur la pile sous forme de paires (ptr, len) au moment de leur utilisation :

```gdb
# Breakpoint sur une fonction qui reçoit un string en argument
# Nouvelle ABI : RAX = ptr, RBX = len
break main.parseKey  
run DEAD-BEEF-CAFE-BABE  

# À l'arrêt, afficher la string
x/s $rax
# Attention : x/s suppose un null terminator, il peut afficher trop.
# Méthode correcte — utiliser la longueur :
printf "%.*s\n", (int)$rbx, (char*)$rax
```

La commande GDB `printf "%.*s\n"` avec le format `%.*s` est la clé : elle utilise la longueur explicite (`$rbx`) au lieu de chercher un null.

```gdb
# Avec GEF/pwndbg, helper plus lisible :
define go_str
    set $ptr = $arg0
    set $len = $arg1
    printf "Go string [%d]: ", $len
    eval "x/%dbs $ptr", $len
end

# Usage :
go_str $rax $rbx
```

### Technique 6 : hooking avec Frida

Frida permet d'intercepter les fonctions qui manipulent des strings et de les afficher proprement :

```javascript
// Hook runtime.gostring pour capturer les conversions C string → Go string
// Mais plus utile : hooker les fonctions applicatives
Interceptor.attach(Module.findExportByName(null, "main.parseKey"), {
    onEnter: function(args) {
        // Nouvelle ABI Go : premier arg string = (RAX, RBX)
        // Frida expose les registres via this.context
        var ptr = this.context.rax;
        var len = this.context.rbx.toInt32();
        if (len > 0 && len < 4096) {
            console.log("parseKey appelé avec: " +
                        Memory.readUtf8String(ptr, len));
        }
    }
});
```

> 💡 **Astuce RE** : hooker `fmt.Fprintf`, `fmt.Sprintf` et `os.(*File).WriteString` avec Frida capture l'essentiel des chaînes affichées à l'écran ou écrites dans des fichiers par le programme. C'est souvent suffisant pour comprendre la logique de l'application sans analyser le désassemblage en détail.

---

## Strings dans le crackme : cas concret

Appliquons ces techniques à notre `crackme_go_strip`. Voici ce que chaque méthode révèle :

### `strings` brut

```bash
$ strings -n 8 crackme_go_strip | grep -i 'valid\|erreur\|clé\|check\|bravo'
```

Le résultat est probablement un fragment fusionné contenant plusieurs messages bout à bout. Difficile de déterminer les frontières exactes.

### GoReSym — noms de fonctions

```bash
$ GoReSym -p crackme_go_strip | jq '.UserFunctions[].FullName' | head
"main.main"
"main.parseKey"
"main.hexVal"
"main.validateGroups"
"main.validateCross"
"main.validateOrder"
"main.(*ChecksumValidator).Validate"
"main.(*CrossValidator).Validate"
```

Les noms des fonctions révèlent déjà la structure du programme et le vocabulaire métier. Chaque nom est extrait proprement grâce à la table de noms null-terminée de `gopclntab`.

### GDB — capture dynamique

```gdb
# Poser un breakpoint sur la comparaison de strings dans validateGroups
break main.(*ChecksumValidator).Validate  
run DEAD-BEEF-CAFE-BABE  

# Inspecter le receiver (*ChecksumValidator) et les arguments
info registers rax rbx rcx rdi rsi
```

En analyse dynamique, les strings apparaissent décomposées en paires (ptr, len) dans les registres, directement lisibles.

---

## Résumé des outils et de leur efficacité

```
Méthode                      Effort    Précision   Couverture
─────────────────────────    ──────    ─────────   ──────────
strings (brut)               Minimal   Faible      Élevée (mais bruitée et fusionnée)  
strings + filtrage grep      Faible    Moyenne     Moyenne  
GoReSym (noms fonc./types)   Faible    Excellente  Noms seulement, pas les littéraux  
Script Ghidra LEA+MOV        Moyen     Bonne       Bonne (littéraux référencés dans le code)  
Scan des string headers      Moyen     Bonne       Variables globales et champs de struct  
GDB printf %.*s              Moyen     Excellente  Limitée aux chemins exécutés  
Frida hooking                Moyen     Excellente  Limitée aux chemins exécutés  
```

En pratique, la combinaison **GoReSym** (noms de fonctions et types) + **script Ghidra** (littéraux dans le code) + **Frida/GDB** (validation dynamique) couvre la quasi-totalité des besoins.

---

## Pièges courants

### Piège 1 : `x/s` dans GDB

La commande `x/s $rax` de GDB affiche une chaîne null-terminée à l'adresse contenue dans `RAX`. Mais les strings Go n'étant pas null-terminées, GDB continuera de lire au-delà de la fin de la chaîne, affichant des données parasites provenant de la chaîne suivante dans le blob `.rodata`. Utilisez toujours `printf "%.*s\n"` avec la longueur explicite.

### Piège 2 : Ghidra et le type `char *`

Quand Ghidra détecte un `LEA` vers `.rodata`, il peut tenter de créer automatiquement une donnée de type `char *` null-terminée à cette adresse. Comme il n'y a pas de null, Ghidra crée une « string » qui englobe toutes les chaînes suivantes. Cela corrompt l'affichage de `.rodata`. Pour corriger, supprimez la donnée auto-créée (`Clear Code Bytes`) et recréez-la manuellement avec la bonne longueur.

### Piège 3 : confusion longueur en octets vs longueur en runes

En Go, `len("café")` retourne 5 (octets), pas 4 (caractères), parce que `é` est encodé sur 2 octets en UTF-8. Le champ `len` dans le string header est toujours en **octets**. Si vous lisez `len = 5` pour une chaîne qui semble avoir 4 caractères, pensez UTF-8 multi-octets.

### Piège 4 : strings vides et strings nil

La string vide `""` a `ptr` arbitraire (souvent non-nil, pointant vers un emplacement valide) et `len = 0`. La string zéro-value (jamais initialisée) a `ptr = nil` et `len = 0`. Les deux sont fonctionnellement équivalentes en Go, mais en RE, un pointeur nul dans un string header indique une variable non initialisée plutôt qu'un `""` explicite.

### Piège 5 : `[]byte` vs `string`

Le header d'un slice de bytes `[]byte` (24 octets : ptr, len, cap) et le header d'une string (16 octets : ptr, len) se ressemblent dans les deux premiers champs. Si vous voyez un pattern qui ressemble à une string mais avec un troisième mot (capacité), c'est un `[]byte`. La distinction est importante : les `[]byte` sont mutables, les strings ne le sont pas. Un buffer `[]byte` sera probablement modifié en place, tandis qu'une string sera copiée si elle doit changer.

---

## Ce qu'il faut retenir

1. **Les strings Go ne sont pas null-terminées.** C'est la source de tous les problèmes avec `strings`. Le compilateur les stocke bout à bout dans `.rodata` sans séparateur.  
2. **Ne faites pas confiance à `strings` brut.** Utilisez-le uniquement en première passe grossière, avec un filtrage agressif pour éliminer le bruit du runtime.  
3. **Cherchez les paires `LEA` + `MOV imm`.** C'est le pattern assembleur caractéristique d'un string littéral Go. Un script Ghidra ou un grep dans le désassemblage les capture efficacement.  
4. **En dynamique, utilisez la longueur explicite.** `printf "%.*s\n", len, ptr` dans GDB, ou `Memory.readUtf8String(ptr, len)` dans Frida.  
5. **GoReSym extrait les noms, pas les littéraux.** Combinez-le avec d'autres techniques pour couvrir l'ensemble des chaînes du programme.  
6. **Attention au partage de sous-chaînes.** Deux string headers différents peuvent pointer vers des régions qui se chevauchent dans `.rodata`. Cela ne signifie pas une erreur — c'est une optimisation du compilateur.

⏭️ [Stripped Go binaries : retrouver les symboles via les structures internes](/34-re-go/06-stripped-go-symboles.md)
