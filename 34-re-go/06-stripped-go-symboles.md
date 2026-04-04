🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 34.6 — Stripped Go binaries : retrouver les symboles via les structures internes

> 🐹 *Les sections précédentes ont posé les fondations : vous connaissez le runtime (34.1), les conventions d'appel (34.2), les structures de données (34.3), `gopclntab` (34.4) et les strings (34.5). Cette dernière section rassemble tous ces éléments dans un workflow complet face au cas le plus courant en situation réelle : un binaire Go strippé, sans DWARF, parfois garblé, que vous devez analyser de bout en bout.*

---

## Le spectre du stripping Go

Tous les binaires Go strippés ne se valent pas. Le niveau d'opacité varie selon les mesures prises par le développeur :

| Niveau | Mesures appliquées | Ce qui reste exploitable |  
|---|---|---|  
| 0 | Aucune (binaire brut) | Tout : `.symtab`, DWARF, `gopclntab`, types, noms |  
| 1 | `strip -s` | `gopclntab`, types, noms de fonctions, `moduledata` |  
| 2 | `-ldflags="-s -w"` | `gopclntab`, types, noms de fonctions, `moduledata` |  
| 3 | `strip -s` + `-ldflags="-s -w"` | `gopclntab`, types, noms de fonctions, `moduledata` |  
| 4 | `garble build` | `gopclntab` (noms garblés), types (noms garblés), structure intacte |  
| 5 | `garble -literals -seed=random build` | `gopclntab` (garblé), types (garblés), strings littérales chiffrées |

Le point essentiel : les niveaux 1 à 3 sont **fonctionnellement identiques** pour le reverse engineer. Que le développeur utilise `strip`, `-ldflags`, ou les deux, les structures internes du runtime restent intactes. La vraie rupture se situe au niveau 4, avec l'introduction de `garble`.

---

## Étape 1 — Identification et triage

Avant de plonger dans la reconstruction, confirmez que vous êtes face à un binaire Go et évaluez le niveau de stripping.

### Confirmer la nature Go du binaire

```bash
# Test rapide — au moins un de ces indicateurs suffit
file binaire                                    # souvent "statically linked"  
readelf -S binaire | grep -E 'gopclntab|go\.buildid|noptrdata'  
strings binaire | grep -c 'runtime\.'          # > 100 = quasi certain Go  
strings binaire | grep -oP 'go1\.\d+\.\d+'    # version du compilateur  
xxd binaire | grep -i 'f1ff ffff'              # magic gopclntab (Go 1.20+)  
```

### Évaluer le niveau de stripping

```bash
# Table de symboles ELF présente ?
readelf -s binaire | head -20
# Si "no symbols" → strippé (niveau ≥ 1)

# Sections DWARF présentes ?
readelf -S binaire | grep debug
# Si aucune section .debug_* → DWARF supprimé

# Noms de fonctions lisibles dans gopclntab ?
strings binaire | grep 'main\.' | head -10
# Si "main.parseKey", "main.main" → noms intacts (niveaux 1-3)
# Si "a1b2c3.x4y5" ou absent → garblé (niveaux 4-5)

# Strings littérales lisibles ?
strings binaire | grep -iE 'error|invalid|usage|license'
# Si présentes → pas de chiffrement de littéraux
# Si absentes ou inintelligibles → garble -literals (niveau 5)
```

> 💡 **Astuce RE** : cette phase de triage prend moins d'une minute et vous évite de perdre du temps avec une stratégie inadaptée. Un binaire de niveau 1-3 se traite en quelques minutes avec GoReSym. Un binaire de niveau 5 nécessite une approche radicalement différente.

---

## Étape 2 — Récupération des noms de fonctions

### Niveaux 1-3 : extraction directe

C'est le cas favorable. Les noms complets sont dans `gopclntab`, lisibles en clair :

```bash
# Extraction complète avec GoReSym
GoReSym -t -d -p binaire > metadata.json

# Vérification rapide
jq '.TabMeta.Version' metadata.json        # version pclntab  
jq '.UserFunctions | length' metadata.json  # nombre de fonctions utilisateur  
jq '.UserFunctions[:5]' metadata.json       # aperçu des premières fonctions  
```

Importez ensuite les résultats dans votre désassembleur (script Ghidra de la section 34.4).

### Niveau 4 : noms garblés

`garble` remplace les noms par des identifiants dérivés d'un hash. Vous obtenez des fonctions nommées selon un schéma du type :

```
Lhx3F2a.Kp9mW1    (au lieu de main.parseKey)  
Lhx3F2a.Yn4rQ7    (au lieu de main.validateGroups)  
Lhx3F2a.main       (main.main est parfois préservé)  
```

Le préfixe de package est garblé (`Lhx3F2a` au lieu de `main`), mais il reste cohérent — toutes les fonctions du même package partagent le même préfixe. Cela vous permet de regrouper les fonctions par package, même sans connaître le vrai nom.

**Stratégie de renommage progressif :**

1. Identifiez `main.main` (parfois préservé, sinon c'est la fonction appelée par `runtime.main`).  
2. Depuis `main.main`, suivez les appels pour cartographier les fonctions du package principal.  
3. Renommez-les manuellement au fur et à mesure de votre compréhension : `Lhx3F2a.Kp9mW1` → `main.func_validate_key` (nom descriptif choisi par vous).  
4. Utilisez les cross-references et les strings pour deviner le rôle de chaque fonction.

### Niveau 5 : noms garblés + littéraux chiffrés

`garble -literals` chiffre les string littérales en les remplaçant par des appels de déchiffrement au runtime. En désassemblage, au lieu d'un `LEA` vers `.rodata`, vous verrez :

```asm
; String littérale chiffrée — déchiffrée à l'exécution
LEA     RAX, [données_chiffrées]  
MOV     RBX, longueur  
CALL    garble_decrypt_func         ; fonction de déchiffrement insérée par garble  
; Après le retour : RAX = ptr vers la string déchiffrée, RBX = len
```

Les fonctions de déchiffrement de `garble` utilisent typiquement du XOR avec une clé dérivée. Elles sont reconnaissables par leur pattern : allocation d'un buffer, boucle XOR, retour d'un string header.

**Contre-mesures :**

1. **Analyse dynamique** — posez un breakpoint juste après l'appel de déchiffrement et lisez la string en clair dans les registres de retour. C'est la méthode la plus directe.  
2. **Identification du pattern de déchiffrement** — localisez les fonctions de déchiffrement (petites fonctions appelées fréquemment, avec une boucle XOR), puis écrivez un script qui les émule pour déchiffrer toutes les strings statiquement.  
3. **Frida** — hookez la fonction de déchiffrement et loguez chaque string déchiffrée avec son adresse de retour (pour savoir quelle fonction l'utilise).

```javascript
// Frida — capturer les strings déchiffrées par garble
// Identifier d'abord l'adresse de la fonction de déchiffrement
var decryptFunc = ptr("0x4A1234"); // adresse trouvée en analyse statique

Interceptor.attach(decryptFunc, {
    onLeave: function(retval) {
        var strPtr = this.context.rax;
        var strLen = this.context.rbx.toInt32();
        if (strLen > 0 && strLen < 4096) {
            var caller = this.returnAddress;
            console.log("[" + caller + "] Déchiffré: " +
                        Memory.readUtf8String(strPtr, strLen));
        }
    }
});
```

---

## Étape 3 — Reconstruction de `moduledata`

### Pourquoi `moduledata` est central

Comme vu en section 34.4, `runtime.firstmoduledata` est le nœud central qui relie toutes les métadonnées. Sa reconstruction dans un binaire strippé ouvre l'accès à l'ensemble des informations de type, aux limites des segments, et à la table de fonctions.

### Localisation par signature

`moduledata` a un layout prévisible dont certains champs contiennent des adresses connues. Le principe : retrouver en mémoire une zone de `.noptrdata` ou `.data` dont les champs correspondent aux adresses des segments ELF.

```python
# locate_moduledata.py — heuristique de localisation
# Principe : moduledata contient les adresses text/etext/data/edata
# qui correspondent aux limites des segments ELF.

import struct  
from elftools.elf.elffile import ELFFile  

def find_moduledata(filepath):
    with open(filepath, 'rb') as f:
        elf = ELFFile(f)
        f.seek(0)
        data = f.read()

    # Récupérer les limites des segments depuis les headers ELF
    text_start = None
    text_end = None
    for seg in elf.iter_segments():
        if seg['p_type'] == 'PT_LOAD' and seg['p_flags'] & 0x1:  # executable
            text_start = seg['p_vaddr']
            text_end = seg['p_vaddr'] + seg['p_memsz']
            break

    if text_start is None:
        print("Segment .text non trouvé")
        return

    # Chercher dans les sections de données une paire (text_start, text_end)
    # Moduledata contient ces adresses dans des champs consécutifs
    target = struct.pack('<QQ', text_start, text_end)

    offset = 0
    while True:
        pos = data.find(target, offset)
        if pos == -1:
            break
        print(f"Candidat moduledata à l'offset fichier 0x{pos:x}")
        # Vérification : le champ pclntable devrait pointer vers le magic gopclntab
        # (les offsets exacts dépendent de la version de Go)
        offset = pos + 1

find_moduledata("crackme_go_strip")
```

### Localisation par référence depuis `runtime.main`

Si vous avez déjà récupéré les noms de fonctions via `gopclntab`, `runtime.main` est identifiable. Au début de son exécution, elle accède à `firstmoduledata` :

```asm
; Extrait de runtime.main (simplifié)
LEA     RAX, [runtime.firstmoduledata]  
MOV     RCX, [RAX + offset_hasmain]  
TEST    RCX, RCX  
JZ      .no_main  
```

L'adresse chargée par le `LEA` est celle de `moduledata`. Dans Ghidra, suivez la cross-reference depuis `runtime.main` vers les données.

### Parser `moduledata`

Une fois l'adresse trouvée, les champs utiles à extraire (offsets pour Go 1.21+, amd64) :

```
Offset   Taille   Champ          Ce que ça vous donne
──────   ──────   ─────          ────────────────────
+0x00    24       pclntable      Slice (ptr, len, cap) vers gopclntab
+0x18    24       ftab           Slice vers la table de fonctions
+0x30    24       filetab        Slice vers la table des fichiers source
+0x48    8        findfunctab    Pointeur vers la table de lookup rapide
+0x50    8        minpc          Plus petite adresse PC (≈ début de .text)
+0x58    8        maxpc          Plus grande adresse PC (≈ fin de .text)
+0x60    8        text           Adresse de .text
+0x68    8        etext          Fin de .text
+0x70    24       noptrdata      Slice de .noptrdata
+0x88    24       data           Slice de .data
...
+0x???   24       typelinks      Slice d'offsets vers les types   ← clé pour l'étape 4
+0x???   24       itablinks      Slice vers les itabs             ← clé pour les interfaces
```

Les offsets varient entre versions de Go. La méthode fiable : localisez le champ `pclntable` (son `ptr` doit pointer vers le magic de `gopclntab`) et utilisez-le comme ancre pour calibrer les offsets des autres champs.

> 💡 **Astuce RE** : GoReSym fait tout ce travail automatiquement et expose `moduledata` dans sa sortie JSON. Mais comprendre le mécanisme manuel est crucial pour les cas où GoReSym échoue (versions Go très récentes non encore supportées, binaires modifiés).

---

## Étape 4 — Reconstruction des types

### Le champ `typelinks`

Le champ `typelinks` de `moduledata` contient un slice d'offsets (int32) relatifs à une adresse de base. Chaque offset pointe vers un descripteur `runtime._type` (section 34.3). En itérant sur ce slice, vous obtenez la liste complète des types définis et utilisés dans le programme.

### Extraire les types avec GoReSym

```bash
GoReSym -t crackme_go_strip | jq '.Types[] | select(.PackageName=="main")'
```

Sortie typique :

```json
{
  "Kind": "struct",
  "Name": "main.ChecksumValidator",
  "Size": 8,
  "Fields": [
    { "Name": "ExpectedSums", "Type": "map[int]uint16", "Offset": 0 }
  ]
}
```

Chaque type expose son `Kind` (struct, interface, slice, map, etc.), son nom complet, sa taille et, pour les structs, la liste des champs avec leurs noms, types et offsets. C'est suffisant pour reconstruire les headers `.h` équivalents.

### Reconstruction manuelle dans Ghidra

Quand GoReSym fournit les définitions de types, créez-les dans Ghidra pour améliorer le pseudo-code :

1. Ouvrez le **Data Type Manager** (panneau gauche).  
2. Créez un nouveau dossier `go_types`.  
3. Pour chaque struct, créez une structure avec les champs aux bons offsets et tailles.  
4. Appliquez ces types aux variables dans le décompilateur : clic droit sur une variable → *Retype Variable*.

Par exemple, pour `ChecksumValidator` :

```
Structure : ChecksumValidator (8 octets)
  +0x00  pointer  ExpectedSums    (pointeur vers hmap)
```

Et pour une interface `Validator` passée en arguments (16 octets, section 34.3) :

```
Structure : Validator_iface (16 octets)
  +0x00  pointer  tab     (pointeur vers itab)
  +0x08  pointer  data    (pointeur vers la valeur concrète)
```

Appliquer ces types transforme le pseudo-code Ghidra d'un amas d'accès mémoire opaques en code lisible avec des noms de champs.

### Reconstruction des itabs

Le champ `itablinks` de `moduledata` liste les itabs pré-construites. Chaque itab (section 34.3) lie un type concret à une interface et contient les pointeurs de méthodes. En les parsant, vous obtenez :

- la liste des couples (type, interface) implémentés dans le programme,  
- les adresses des méthodes concrètes pour chaque interface.

C'est l'équivalent Go de la reconstruction des vtables en C++ (chapitre 17). La différence est que les itabs Go sont explicitement listées dans `moduledata`, alors que les vtables C++ doivent être trouvées par heuristique.

```python
# Pseudo-code : parser les itablinks depuis moduledata
itablinks_ptr, itablinks_len, _ = read_slice(moduledata + ITABLINKS_OFFSET)  
for i in range(itablinks_len):  
    itab_addr = read_uint64(itablinks_ptr + i * 8)
    inter_type = read_uint64(itab_addr + 0x00)    # type descriptor de l'interface
    concrete_type = read_uint64(itab_addr + 0x08)  # type descriptor du type concret
    first_method = read_uint64(itab_addr + 0x18)   # adresse de la première méthode
    # ... lire le nom de l'interface et du type concret depuis les type descriptors ...
```

---

## Étape 5 — Reconstruction du graphe d'appels

### Cross-references et fonctions du package `main`

Une fois les fonctions nommées et les types reconstruits, le graphe d'appels se construit naturellement dans Ghidra via les cross-references (XREF, chapitre 8). Pour un binaire Go, la stratégie la plus efficace :

1. **Partir de `main.main`** et explorer en profondeur (depth-first).  
2. **Marquer les appels directs** (`CALL main.parseKey`, `CALL main.validateGroups`) — ce sont les arêtes du graphe.  
3. **Identifier les appels indirects** (`CALL reg` via itab) — chaque dispatch d'interface est un point de polymorphisme. Utilisez les itabs reconstruites pour résoudre les cibles possibles.  
4. **Repérer les lancements de goroutines** (`CALL runtime.newproc`) — chaque occurrence crée une branche concurrente. L'argument est l'adresse de la fonction cible.

### Script Ghidra : graphe d'appels du package main

```python
# call_graph_main.py — Script Ghidra
# Construit le graphe d'appels des fonctions main.* et l'affiche.

func_mgr = currentProgram.getFunctionManager()  
ref_mgr = currentProgram.getReferenceManager()  
funcs = func_mgr.getFunctions(True)  

main_funcs = {}  
for f in funcs:  
    name = f.getName()
    if name.startswith("main."):
        main_funcs[f.getEntryPoint()] = name

print("=== Graphe d'appels du package main ===")  
for addr, name in sorted(main_funcs.items(), key=lambda x: x[1]):  
    callees = []
    # Parcourir les instructions de la fonction
    body = f.getBody() if f.getEntryPoint() == addr else \
           func_mgr.getFunctionAt(addr).getBody()
    inst_iter = currentProgram.getListing().getInstructions(body, True)
    while inst_iter.hasNext():
        inst = inst_iter.next()
        if inst.getMnemonicString() == "CALL":
            for ref in inst.getReferencesFrom():
                target = ref.getToAddress()
                target_func = func_mgr.getFunctionAt(target)
                if target_func:
                    callees.append(target_func.getName())
    # Filtrer pour ne garder que les appels vers main.* et runtime clés
    interesting = [c for c in callees
                   if c.startswith("main.") or c in
                   ("runtime.newproc", "runtime.gopanic")]
    if interesting:
        f_name = main_funcs.get(addr, "?")
        for callee in interesting:
            print("  {} --> {}".format(f_name, callee))
```

---

## Étape 6 — Reconstituer la logique sans noms (niveau 5)

Face à un binaire garblé avec des littéraux chiffrés (le pire cas), les noms et les strings ne vous aident plus. Il faut s'appuyer sur la **structure** et le **comportement**.

### Les invariants exploitables

Même au niveau d'obfuscation maximal, certains éléments restent inchangés :

| Élément | Pourquoi il survit | Ce qu'il vous apprend |  
|---|---|---|  
| Appels runtime (`runtime.makemap`, `runtime.chansend1`, etc.) | Le runtime n'est jamais garblé | Types de données utilisés (map, channel, slice) |  
| Structure de `gopclntab` | Nécessaire au runtime | Nombre de fonctions, tailles, correspondance PC-fichier |  
| Préambules de pile | Générés par le compilateur | Identification des frontières de fonctions |  
| Appels système (`syscall.Syscall6`) | Interface avec le noyau | Comportement réseau, fichier, processus |  
| Types de la stdlib | Non garblés | `net.Conn`, `crypto/aes.Block`, `os.File` révèlent les capacités |  
| Taille des structures | Dans `runtime._type.size` | Empreinte mémoire, même sans noms |

### Workflow par comportement

```
1. Identifier les syscalls (strace)
   └─► socket, connect, open, read, write, mmap
   └─► Classe le binaire : réseau ? fichier ? crypto ?

2. Identifier les packages stdlib utilisés
   └─► strings binaire | grep -E 'crypto/|net/|os/|encoding/'
   └─► Les noms de la stdlib ne sont PAS garblés par garble

3. Cartographier les fonctions par taille et complexité
   └─► Les grosses fonctions main.* (garblées) = logique métier
   └─► Les petites fonctions = utilitaires, validations

4. Analyse dynamique ciblée
   └─► Breakpoints sur runtime.chansend1 → flux de données
   └─► Breakpoints sur crypto/aes.newCipher → extraction de clé
   └─► Breakpoints sur net.(*conn).Write → données réseau

5. Renommage progressif
   └─► func_0x4A1234 → func_send_data (d'après le comportement observé)
   └─► Itérer jusqu'à couverture suffisante
```

### L'avantage décisif de la stdlib non garblée

`garble` ne peut pas garbler la bibliothèque standard de Go — elle est précompilée et partagée entre tous les programmes. Cela signifie que les **frontières entre le code métier et la stdlib** sont toujours visibles :

```
main.Lhx3F2a.Kp9mW1           ← code métier (garblé)
  └─► crypto/aes.NewCipher     ← stdlib (lisible !)
  └─► encoding/hex.Decode      ← stdlib (lisible !)
  └─► net.(*Dialer).DialContext ← stdlib (lisible !)
```

En suivant les appels depuis les fonctions garblées vers la stdlib, vous reconstruisez le comportement du programme : cette fonction fait du chiffrement AES, celle-ci décode de l'hexadécimal, celle-là ouvre une connexion réseau.

> 💡 **Astuce RE** : dans Ghidra, utilisez la fonctionnalité *Function Call Trees* (clic droit sur une fonction → *References → Show Call Trees*) pour visualiser rapidement quelles fonctions stdlib sont appelées par chaque fonction garblée. C'est souvent suffisant pour comprendre le rôle général de la fonction.

---

## Outils complémentaires

### `redress` (go-re)

`redress` est un outil spécialisé dans l'analyse de binaires Go. Il reconstruit les interfaces, les types, les packages et le graphe d'appels :

```bash
# Installation
go install github.com/goretk/redress@latest

# Lister les packages
redress -pkg binaire

# Afficher les types et interfaces
redress -type binaire

# Afficher les informations du compilateur
redress -compiler binaire
```

`redress` utilise la bibliothèque `gore` (Go Reverse Engineering library), qui est également utilisable directement en Go pour écrire des scripts d'analyse custom.

### `gore` (bibliothèque Go)

```go
// Exemple minimal avec gore
package main

import (
    "fmt"
    "github.com/goretk/gore"
)

func main() {
    f, err := gore.Open("crackme_go_strip")
    if err != nil { panic(err) }
    defer f.Close()

    // Version du compilateur
    v, _ := f.GetCompilerVersion()
    fmt.Println("Go version:", v)

    // Packages
    pkgs, _ := f.GetPackages()
    for _, p := range pkgs {
        fmt.Printf("Package: %s (%d fonctions)\n", p.Name, len(p.Functions))
    }

    // Types
    types, _ := f.GetTypes()
    for _, t := range types {
        fmt.Printf("Type: %s (kind: %d, size: %d)\n", t.Name, t.Kind, t.Size)
    }
}
```

### `GoStringUngarbler` et outils de désobfuscation

Pour les binaires garblés avec `-literals`, des outils communautaires tentent d'identifier et d'émuler les fonctions de déchiffrement de strings. Recherchez `GoStringUngarbler` ou `garble deobfuscator` sur GitHub. Ces outils sont par nature fragiles (liés à la version de `garble`), mais peuvent faire gagner un temps considérable quand ils fonctionnent.

---

## Workflow complet récapitulatif

```
                    Binaire Go inconnu
                           │
                    ┌──────┴──────┐
                    │   Triage    │
                    │  (1 min)    │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         Noms lisibles  Noms garblés  Noms garblés
         (niveaux 1-3)  (niveau 4)   + lits chiffrés
              │            │           (niveau 5)
              ▼            ▼            │
         GoReSym       GoReSym          ▼
         + import      + renommage   Analyse
         Ghidra        progressif    dynamique
              │            │         (GDB/Frida)
              │            │            │
              └────────────┼────────────┘
                           │
                    ┌──────┴──────┐
                    │ moduledata  │
                    │ + typelinks │
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │   Types     │
                    │ reconstruits│
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │  Graphe     │
                    │ d'appels    │
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │  Analyse    │
                    │  logique    │
                    └─────────────┘
```

---

## Ce qu'il faut retenir

1. **Évaluez le niveau de stripping en premier.** Les niveaux 1-3 (strip/ldflags) se traitent rapidement. Les niveaux 4-5 (garble) demandent une stratégie adaptée.  
2. **`gopclntab` + `moduledata` + `typelinks` forment un triptyque.** Ensemble, ils contiennent les noms de fonctions, les définitions de types et les tables d'interfaces — tout ce qu'il faut pour reconstruire l'architecture du programme.  
3. **La stdlib n'est jamais garblée.** C'est votre levier principal face à un binaire obfusqué : les appels vers `crypto/*`, `net/*`, `os/*` révèlent le comportement du programme.  
4. **L'analyse dynamique contourne le garbling.** Les valeurs en mémoire, les strings déchiffrées et les arguments des syscalls sont toujours en clair à l'exécution.  
5. **Renommez progressivement.** Ne cherchez pas à tout comprendre d'un coup. Partez de `main.main`, suivez les appels, et renommez chaque fonction dès que vous comprenez son rôle. La clarté du désassemblage s'améliore itération après itération.  
6. **Outillez-vous.** GoReSym, `redress`, `gore`, les scripts Ghidra — chaque outil vous épargne des heures de travail manuel. Investissez le temps de les installer et de les maîtriser.

⏭️ [🎯 Checkpoint : analyser un binaire Go strippé, retrouver les fonctions et reconstruire la logique](/34-re-go/checkpoint.md)
