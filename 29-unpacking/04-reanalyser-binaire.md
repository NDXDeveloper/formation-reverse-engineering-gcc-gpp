🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 29.4 — Réanalyser le binaire unpacké

> 🎯 **Objectif de cette section** — Appliquer le workflow d'analyse classique (parties II–IV de la formation) sur le binaire reconstruit, valider que l'unpacking et la reconstruction ont produit un ELF exploitable, et retrouver intégralement la logique du programme original — depuis le triage rapide jusqu'à la décompilation dans Ghidra.

---

## Retour au point de départ

Aux sections 29.1 à 29.3, on a traversé trois étapes : détecter le packing, extraire le code décompressé, puis reconstruire un fichier ELF valide. On dispose maintenant de `packed_sample_reconstructed` — un ELF 64 bits contenant le code et les données du programme original. Il est temps de vérifier que tout ce travail a porté ses fruits en soumettant ce binaire à la même méthodologie d'analyse que n'importe quel autre exécutable de la formation.

Cette section sert à la fois de **validation technique** (la reconstruction est-elle correcte ?) et de **démonstration pédagogique** (l'analyse qui échouait complètement sur le binaire packé fonctionne désormais normalement). On la structurera autour du workflow de triage rapide présenté au chapitre 5 (section 5.7), puis on poussera l'analyse jusqu'à la décompilation.

---

## Phase 1 — Triage rapide : les 5 premières minutes

On reprend la routine du chapitre 5 et on l'applique en parallèle au binaire packé et au binaire reconstruit, pour mettre en évidence le contraste.

### `file`

```
$ file packed_sample_upx_tampered
packed_sample_upx_tampered: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux),  
statically linked, no section header  

$ file packed_sample_reconstructed
packed_sample_reconstructed: ELF 64-bit LSB executable, x86-64, version 1 (SYSV),  
statically linked, not stripped  
```

Le binaire reconstruit est reconnu comme un ELF classique. La mention `no section header` a disparu : `readelf` et Ghidra pourront exploiter la SHT que l'on a recréée à la section 29.3.

### `strings`

```
$ strings packed_sample_upx_tampered | wc -l
9

$ strings packed_sample_reconstructed | wc -l
84
```

Le nombre de chaînes lisibles passe d'une poignée à plusieurs dizaines. On peut maintenant extraire des informations concrètes sans exécuter le binaire :

```
$ strings packed_sample_reconstructed | grep -i flag
FLAG{unp4ck3d_and_r3c0nstruct3d}

$ strings packed_sample_reconstructed | grep -i "clé\|licence\|key\|RE29"
[*] Entrez votre clé de licence (format RE29-XXXX) :
[-] Indice : analysez la fonction check_license_key...

$ strings packed_sample_reconstructed | grep -i "auteur\|build\|watermark"
Auteur: Formation-RE-GNU  
BUILD:ch29-packed-2025  
<<< WATERMARK:PACKED_SAMPLE_ORIGINAL >>>
```

Ces seules sorties de `strings` révèlent déjà le fonctionnement global du programme : il demande une clé de licence au format `RE29-XXXX`, il contient un flag, et il porte des métadonnées identifiables. Sur le binaire packé, aucune de ces informations n'était accessible.

### `readelf -S` (sections)

```
$ readelf -S packed_sample_reconstructed
There are 6 section headers, starting at offset 0x6100:

Section Headers:
  [Nr] Name              Type             Address           Offset    Size
  [ 0]                   NULL             0000000000000000  00000000  0000...
  [ 1] .text             PROGBITS         0000000000401000  00001000  0003000
  [ 2] .rodata           PROGBITS         0000000000404000  00004000  0001000
  [ 3] .data             PROGBITS         0000000000405000  00005000  0001000
  [ 4] .bss              NOBITS           0000000000406000  00006000  0001000
  [ 5] .shstrtab         STRTAB           0000000000000000  00006000  000002d
```

La structure est propre. Le binaire packé avait au mieux deux ou trois sections aux noms exotiques ; le binaire reconstruit possède une SHT standard que tout outil d'analyse reconnaît.

### `checksec`

```
$ checksec --file=packed_sample_reconstructed
    Arch:     amd64-64-little
    RELRO:    No RELRO
    Stack:    No canary found
    NX:       NX enabled
    PIE:      No PIE
```

Les protections ne sont pas entièrement restaurées (pas de canary, pas de RELRO) car la reconstruction par dump mémoire ne recrée pas les sections `.got`, `.plt` ni les métadonnées de relocation. En revanche, NX peut être correctement positionné si les permissions des segments `LOAD` ont été configurées sans le flag `W+X` simultané. C'est un résultat attendu : le binaire reconstruit est destiné à l'analyse, pas à un déploiement en production.

---

## Phase 2 — Analyse statique dans Ghidra

### Import et auto-analyse

On ouvre Ghidra, on crée un nouveau projet et on importe `packed_sample_reconstructed`. Ghidra détecte automatiquement le format ELF x86-64. On lance l'auto-analyse avec les options par défaut (Decompiler Parameter ID, Aggressive Instruction Finder, etc.).

À titre de comparaison, si l'on avait tenté d'importer le binaire packé dans Ghidra, l'auto-analyse aurait produit un résultat inutilisable : le désassembleur aurait interprété les données compressées comme des instructions, générant des milliers de faux positifs et un graphe de flux incohérent. Sur le binaire reconstruit, l'analyse se déroule normalement.

### Identification des fonctions

Après l'auto-analyse, Ghidra devrait avoir identifié automatiquement plusieurs fonctions dans la section `.text`. Dans la vue **Symbol Tree → Functions**, on recherche les fonctions reconnaissables. Si le binaire n'est pas strippé (ou si les symboles ont survécu dans le dump), on trouve directement `main`, `check_license_key`, `xor_decode`, `compute_checksum` et `print_debug_info`.

Si le binaire a été strippé avant le packing (ce qui est le cas de notre `packed_sample_O2_strip` utilisé comme base), les noms de fonctions ne sont pas disponibles. Ghidra attribue des noms génériques (`FUN_00401000`, `FUN_004011a0`…). On doit alors identifier les fonctions par leur contenu — c'est exactement le travail de reverse engineering tel qu'il a été pratiqué dans les chapitres précédents.

### Identifier `main` sans symboles

On commence par le point d'entrée. En naviguant vers l'adresse `0x401000` (notre OEP), on observe le code de démarrage. Si l'OEP correspond à `_start`, on repère l'appel à `__libc_start_main` dont le premier argument (`rdi`) est l'adresse de `main`. Si l'OEP pointe directement vers `main` (cas d'une reconstruction simplifiée), on y est déjà.

Dans le décompilé de `main`, on reconnaît la structure du programme grâce aux chaînes de caractères. Ghidra résout les références vers `.rodata` et affiche les chaînes en clair dans le pseudo-code :

```c
void FUN_00401000(int argc, char **argv)
{
    puts("╔══════════════════════════════════════╗");
    // ...
    printf("[*] Entrez votre clé de licence (format RE29-XXXX) : ");
    fgets(local_58, 0x40, stdin);
    // ...
    if (FUN_004011a0(local_58) != 0) {
        // branche succès
    }
}
```

La fonction `FUN_004011a0` appelée avec l'input utilisateur comme argument est clairement `check_license_key`. On la renomme en double-cliquant sur le nom dans Ghidra.

### Reconstruire la logique de vérification

En naviguant dans `check_license_key` (renommée), le décompilé révèle l'algorithme :

```c
int check_license_key(char *key)
{
    if (strlen(key) != 9) return 0;
    if (strncmp(key, "RE29-", 5) != 0) return 0;
    
    unsigned long user_val = strtoul(key + 5, &endptr, 16);
    uint32_t expected = compute_checksum("RE29-", 5);
    
    return (user_val == expected);
}
```

On identifie le format attendu (`RE29-XXXX`), la base hexadécimale de la partie variable, et l'appel à `compute_checksum`. En naviguant dans cette dernière fonction :

```c
uint32_t compute_checksum(char *buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += (uint32_t)buf[i] * (uint32_t)(i + 1);
    }
    return sum & 0xFFFF;
}
```

On peut maintenant calculer la clé valide. Le checksum de `"RE29-"` est la somme pondérée des codes ASCII de chaque caractère, que l'on traduit en hexadécimal pour obtenir `RE29-0337`. Toute cette logique était complètement invisible tant que le binaire était packé.

### Identifier la routine XOR

En examinant la branche « succès » de `main`, on repère un appel à une fonction qui prend en paramètres un buffer de sortie, le tableau `g_encrypted_msg`, sa taille, la clé XOR et sa longueur. Le décompilé de cette fonction montre une boucle `XOR` classique :

```c
void xor_decode(char *dst, uint8_t *src, size_t len,
                uint8_t *key, size_t klen)
{
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i] ^ key[i % klen];
    }
    dst[len] = '\0';
}
```

En extrayant les valeurs de `g_encrypted_msg` et `g_xor_key` depuis `.rodata` (visibles dans le Listing de Ghidra ou dans ImHex), on peut reproduire le déchiffrement manuellement et retrouver le message `SUCCESS!`.

### Repérer les constantes crypto

Dans la vue **Defined Data** de Ghidra (ou via une recherche de séquences d'octets dans ImHex), on retrouve les 16 premiers octets de la S-box AES :

```
63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76
```

Ces octets sont des constantes bien connues (référencées dans l'annexe J du tutoriel). Leur présence dans un binaire réel signalerait l'utilisation d'AES — ici il s'agit d'un marqueur pédagogique, mais la technique d'identification est identique. Sur le binaire packé, ces constantes étaient noyées dans les données compressées et totalement indétectables par une recherche statique.

---

## Phase 3 — Validation croisée avec l'analyse dynamique

Pour confirmer les conclusions de l'analyse statique, on peut reprendre les techniques dynamiques des chapitres 11–13 sur le binaire **packé** (pas le reconstruit), en ciblant directement les adresses identifiées dans Ghidra.

### Vérifier la clé avec GDB

On relance le binaire packé sous GDB, on le laisse se décompresser jusqu'à l'OEP (section 29.2), puis on pose un breakpoint sur `check_license_key` à l'adresse identifiée dans Ghidra :

```
gef➤ break *0x4011a0  
gef➤ continue  
```

On saisit `RE29-0337` comme input. Au breakpoint, on inspecte l'argument :

```
gef➤ x/s $rdi
0x7fffffffe0a0: "RE29-0337"
```

On continue l'exécution ; le programme affiche le message de succès et le flag. La clé est correcte.

### Hooker avec Frida

On peut aussi utiliser Frida (chapitre 13) pour intercepter `check_license_key` sur le binaire packé en cours d'exécution, sans se soucier du packing :

```javascript
// hook_check_key.js
Interceptor.attach(ptr("0x4011a0"), {
    onEnter: function(args) {
        console.log("[*] check_license_key appelée");
        console.log("    clé = " + args[0].readUtf8String());
    },
    onLeave: function(retval) {
        console.log("    retour = " + retval.toInt32());
    }
});
```

Frida s'injecte dans le processus **après** la décompression du stub, ce qui signifie que les adresses du code original sont accessibles directement. C'est un avantage majeur de l'instrumentation dynamique pour le reverse de binaires packés : on contourne complètement le packing en opérant au niveau du processus en mémoire.

---

## Phase 4 — Comparaison avec le binaire de référence

Le Makefile du chapitre produit aussi `packed_sample_O2` (le binaire compilé en `-O2` avec symboles) et `packed_sample_O0` (en `-O0` avec symboles). Ces versions servent de **référence** pour évaluer la qualité de la reconstruction.

### Comparaison structurelle

```
$ readelf -S packed_sample_O2 | wc -l
31

$ readelf -S packed_sample_reconstructed | wc -l
9
```

Le binaire de référence possède 27 sections contre 6 dans notre reconstruction. Les sections manquantes (`.plt`, `.got`, `.dynamic`, `.init`, `.fini`, `.eh_frame`, `.comment`…) n'ont pas été recréées. C'est attendu : la reconstruction produit un ELF **minimal mais suffisant** pour l'analyse. Les sections absentes fourniraient des informations supplémentaires (résolution d'imports, unwinding des exceptions, métadonnées de compilation) mais ne sont pas indispensables pour comprendre la logique du programme.

### Comparaison du décompilé

En ouvrant le binaire de référence et le binaire reconstruit côte à côte dans deux instances de Ghidra, on peut comparer la qualité du décompilé fonction par fonction. Les différences typiques sont les suivantes :

- Le binaire de référence a des **noms de fonctions** (`main`, `check_license_key`…) grâce aux symboles DWARF. Le binaire reconstruit a des noms génériques (`FUN_00401000`…) qu'il faut renommer manuellement.  
- Le binaire de référence a des **types de variables** corrects (grâce au DWARF). Le binaire reconstruit utilise des types déduits par Ghidra (`undefined8`, `long`, `int`…) qui sont souvent corrects en taille mais pas en sémantique.  
- La **structure du code** (boucles, conditions, appels) est identique dans les deux cas. C'est le point essentiel : la logique est intégralement récupérable.

### Comparaison binaire avec `radiff2`

Pour une comparaison plus fine, on peut utiliser `radiff2` (chapitre 10) sur le segment de code :

```
$ radiff2 -s packed_sample_O2_strip packed_sample_reconstructed
```

Les différences devraient se limiter aux headers et aux métadonnées, pas au code machine lui-même (à condition que le dump mémoire ait été fait correctement et que le niveau d'optimisation soit le même).

---

## Synthèse : ce que l'unpacking a rendu possible

Pour conclure ce chapitre, récapitulons le chemin parcouru en comparant ce que l'on pouvait et ne pouvait pas faire avant et après l'unpacking :

| Analyse | Binaire packé | Binaire reconstruit |  
|---------|---------------|---------------------|  
| `strings` → chaînes utiles | Aucune chaîne du programme | Toutes les chaînes (flag, messages, marqueurs) |  
| `readelf -S` → sections | Absentes ou exotiques | `.text`, `.rodata`, `.data`, `.bss` |  
| `checksec` → protections | Tout désactivé (stub) | Reflet des segments reconstruits |  
| Ghidra → désassemblage | Bruit (données compressées interprétées comme code) | Code x86-64 cohérent, fonctions identifiées |  
| Ghidra → décompilation | Inutilisable | Pseudo-code C lisible, logique complète |  
| YARA → détection de patterns | Aucune correspondance (données compressées) | Constantes crypto, chaînes, signatures |  
| Analyse dynamique (GDB/Frida) | Possible mais nécessite de trouver l'OEP d'abord | Adresses directement utilisables depuis Ghidra |  
| Identification de la clé valide | Impossible en statique | Calcul direct depuis le décompilé |

L'unpacking est un **prérequis** à toute analyse sérieuse d'un binaire protégé. Sans lui, on travaille à l'aveugle ; avec lui, on retrouve la situation normale d'un binaire strippé — un défi certes, mais un défi traitable avec les techniques vues dans le reste de la formation.

---

> 📌 **Point clé à retenir** — L'objectif de l'unpacking n'est pas de produire un clone parfait du binaire original. C'est de produire un fichier suffisamment structuré pour que les outils d'analyse statique (Ghidra, radare2, IDA) puissent faire leur travail. Dès que le décompilé est lisible et que les cross-references fonctionnent, la mission est accomplie — le reste est du reverse engineering classique.

⏭️ [🎯 Checkpoint : unpacker `ch27-packed`, reconstruire l'ELF et retrouver la logique originale](/29-unpacking/checkpoint.md)
