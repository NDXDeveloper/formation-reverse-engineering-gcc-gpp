🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 20.6 — Exporter et nettoyer le pseudo-code pour produire un code recompilable

> 📘 **Chapitre 20 — Décompilation et reconstruction du code source**  
> **Partie IV — Techniques Avancées de RE**

---

## L'objectif et ses limites

Les sections précédentes ont montré comment obtenir du pseudo-code lisible, comment le guider en retypant et renommant dans Ghidra, comment croiser avec RetDec, comment formaliser les types dans un header, et comment éliminer le bruit des bibliothèques tierces. Cette dernière section pousse la démarche jusqu'à son aboutissement logique : **exporter le pseudo-code hors de Ghidra et le nettoyer suffisamment pour qu'il compile**.

Il faut être honnête d'emblée : produire un code fonctionnellement identique au binaire original qui compile et s'exécute correctement est un objectif rarement atteint à 100 %. La section 20.1 a expliqué pourquoi — trop d'informations sont irréversiblement perdues. Mais la poursuite de cet objectif n'a pas besoin d'aboutir complètement pour être utile. Un pseudo-code nettoyé à 80 % qui compile avec quelques stubs est déjà un livrable précieux : il sert de documentation exécutable, de base pour écrire des tests, de point de départ pour un port ou une réécriture, et de référence pour comprendre le comportement du binaire sans relancer Ghidra à chaque fois.

Cette section détaille le processus complet : extraction depuis Ghidra, nettoyage systématique, résolution des problèmes de compilation, et les scripts qui automatisent une partie du travail.

---

## Extraire le pseudo-code depuis Ghidra

### Export manuel fonction par fonction

La méthode la plus directe est le copier-coller depuis le panneau Decompiler. Sélectionner tout le pseudo-code d'une fonction (Ctrl+A dans le panneau), copier (Ctrl+C), et coller dans un éditeur de texte. C'est simple, mais fastidieux quand le binaire contient des dizaines de fonctions d'intérêt.

Le pseudo-code copié inclut la signature de la fonction, le corps, et les déclarations de variables locales. Il n'inclut pas les types définis dans le Data Type Manager (structures, enums), ni les prototypes des fonctions appelées — il faudra les ajouter séparément, typiquement via le header reconstruit en section 20.4.

### Export via le menu Ghidra

Ghidra propose un export plus structuré via *File → Export Program*. Parmi les formats disponibles, l'option **C/C++** exporte l'intégralité du pseudo-code décompilé dans un fichier `.c` unique. Ce fichier contient toutes les fonctions que le décompilateur a pu traiter, précédées de leurs déclarations de types.

Pour y accéder : *File → Export Program → Format: C/C++ → Options* (on peut choisir d'inclure ou non les headers, les types, les commentaires d'adresse). Le fichier résultant peut faire plusieurs milliers de lignes pour un binaire de taille moyenne.

L'export complet est un bon point de départ, mais il contient tout — y compris les fonctions de bibliothèque identifiées par Function ID, les stubs du runtime GCC, et les fonctions que l'on n'a pas besoin de reconstruire. Le travail de nettoyage qui suit est donc essentiel.

### Export via script Ghidra (headless ou interactif)

Pour un contrôle plus fin, un script Ghidra en Python (Jython) ou en Java permet d'exporter sélectivement les fonctions d'intérêt. Voici un exemple de script Python qui exporte uniquement les fonctions taguées `USER_CODE` (en utilisant les Function Tags mentionnés en section 20.5) :

```python
# export_user_functions.py — Script Ghidra (Jython)
# Exporte le pseudo-code des fonctions taguées USER_CODE

from ghidra.app.decompiler import DecompInterface  
from ghidra.util.task import ConsoleTaskMonitor  

output_path = "/tmp/decompiled_user_code.c"  
tag_name = "USER_CODE"  

decomp = DecompInterface()  
decomp.openProgram(currentProgram)  
monitor = ConsoleTaskMonitor()  

fm = currentProgram.getFunctionManager()  
functions = fm.getFunctions(True)  

with open(output_path, "w") as f:
    f.write("/* Pseudo-code exporté depuis Ghidra */\n")
    f.write("/* Binaire : %s */\n\n" % currentProgram.getName())

    for func in functions:
        tags = func.getTags()
        tag_names = [t.getName() for t in tags]
        if tag_name in tag_names:
            result = decomp.decompileFunction(func, 60, monitor)
            if result.decompileCompleted():
                code = result.getDecompiledFunction().getC()
                f.write("/* --- %s @ 0x%x --- */\n" %
                        (func.getName(), func.getEntryPoint().getOffset()))
                f.write(code)
                f.write("\n\n")

decomp.dispose()  
print("[+] Export terminé : %s" % output_path)  
```

Ce script peut être lancé en mode interactif (*Script Manager → Run*) ou en mode headless pour un traitement batch (chapitre 8, section 9). L'avantage est de ne produire que le code métier, sans le bruit des fonctions de bibliothèque.

On peut adapter ce script pour filtrer par d'autres critères : par namespace, par plage d'adresses, par convention de nommage (exclure toutes les fonctions commençant par `__` ou `std::`), ou par taille minimale (ignorer les fonctions stub de moins de 10 instructions).

---

## Anatomie du pseudo-code brut exporté

Avant de nettoyer, examinons ce que contient typiquement un export brut. Voici un extrait représentatif issu de `keygenme_O2_strip`, après renommage et retypage dans Ghidra mais sans nettoyage :

```c
void derive_key(char *username,uint32_t seed,uint8_t *out_key)

{
  size_t sVar1;
  uint32_t hash_r0;
  uint32_t hash_r1;
  uint32_t uVar2;
  size_t i;
  
  sVar1 = strlen(username);
  hash_r0 = seed;
  for (i = 0; i < sVar1; i = i + 1) {
    uVar2 = (uint32_t)(byte)username[i];
    hash_r0 = ((hash_r0 ^ uVar2) << 5 | (hash_r0 ^ uVar2) >> 0x1b) +
              uVar2 * 0x1000193;
    hash_r0 = hash_r0 ^ hash_r0 >> 0x10;
  }
  *out_key = (byte)(hash_r0 >> 0x18);
  out_key[1] = (byte)(hash_r0 >> 0x10);
  out_key[2] = (byte)(hash_r0 >> 8);
  out_key[3] = (byte)hash_r0;
  hash_r1 = hash_r0 ^ 1;
  for (i = 0; i < sVar1; i = i + 1) {
    uVar2 = (uint32_t)(byte)username[i];
    hash_r1 = ((hash_r1 ^ uVar2) << 5 | (hash_r1 ^ uVar2) >> 0x1b) +
              uVar2 * 0x1000193;
    hash_r1 = hash_r1 ^ hash_r1 >> 0x10;
  }
  out_key[4] = (byte)(hash_r1 >> 0x18);
  out_key[5] = (byte)(hash_r1 >> 0x10);
  /* ... suite pour rounds 2 et 3 ... */
  return;
}
```

Ce pseudo-code est fonctionnellement correct, mais il présente plusieurs problèmes qui empêchent la compilation et nuisent à la lisibilité. Identifions-les systématiquement.

---

## Les catégories de problèmes à corriger

### Problème 1 : types Ghidra non standard

Le pseudo-code de Ghidra utilise des types internes qui n'existent pas en C standard :

| Type Ghidra | Signification | Remplacement C |  
|---|---|---|  
| `byte` | Entier non signé 8 bits | `uint8_t` |  
| `ushort` | Entier non signé 16 bits | `uint16_t` |  
| `uint` | Entier non signé 32 bits | `uint32_t` |  
| `ulong` | Entier non signé 64 bits | `uint64_t` |  
| `undefined` | Octet non typé | `uint8_t` |  
| `undefined2` | 2 octets non typés | `uint16_t` |  
| `undefined4` | 4 octets non typés | `uint32_t` |  
| `undefined8` | 8 octets non typés | `uint64_t` |  
| `bool` | Booléen Ghidra | `_Bool` ou `int` |  
| `code` | Pointeur de code | Pointeur de fonction |  
| `longlong` | Entier signé 64 bits | `int64_t` |

La correction est mécanique : un rechercher-remplacer global dans l'éditeur de texte. L'ordre des remplacements importe — traiter `undefined4` avant `undefined` pour éviter les remplacements partiels.

### Problème 2 : casts explicites Ghidra

Ghidra insère des casts explicites très fréquemment, souvent plus que nécessaire. Le pseudo-code brut est truffé d'expressions comme :

```c
uVar2 = (uint32_t)(byte)username[i];
```

Ici, le double cast `(byte)` puis `(uint32_t)` est techniquement correct (extraire un octet non signé puis le promouvoir en 32 bits), mais le C standard effectue cette promotion implicitement quand `username` est un `char *`. Le nettoyage consiste à supprimer les casts redondants tout en conservant ceux qui sont sémantiquement nécessaires (notamment les casts de signedness).

La règle pratique : un cast est conservé s'il change le comportement (signed → unsigned, troncature explicite, conversion de pointeur). Il est supprimé s'il ne fait que rendre explicite une promotion implicite du C.

### Problème 3 : constantes hexadécimales

Le décompilateur affiche presque toutes les constantes en hexadécimal : `0x1b` au lieu de `27`, `0x18` au lieu de `24`, `0x10` au lieu de `16`. Certaines de ces valeurs sont plus lisibles en décimal (les tailles, les compteurs, les décalages de bits), d'autres sont plus lisibles en hexadécimal (les masques, les magic numbers, les adresses).

La conversion n'est pas automatisable aveuglément — elle nécessite un jugement sémantique. Quelques heuristiques utiles : les arguments de shift (`>> 0x18`) sont plus clairs en décimal (`>> 24`). Les constantes multiplicatives comme `0x01000193` sont des identifiants d'algorithme et doivent rester en hexadécimal. Les tailles comme `0x40` sont souvent plus claires en décimal (`64`) sauf quand elles correspondent à des puissances de 2 remarquables.

### Problème 4 : absence de prototypes et d'includes

Le pseudo-code exporté ne contient pas de `#include`. Les fonctions de la libc (`strlen`, `printf`, `fgets`, `memcmp`) sont appelées sans déclaration préalable. Le compilateur émet des avertissements (implicite function declaration) ou des erreurs selon le standard C ciblé.

La correction consiste à ajouter les includes nécessaires en tête du fichier. Les plus courants pour nos binaires :

```c
#include <stdio.h>      /* printf, fgets, fprintf, puts */
#include <stdlib.h>     /* atoi, malloc, free, exit */
#include <string.h>     /* strlen, memcmp, memcpy, strncmp, memset */
#include <stdint.h>     /* uint8_t, uint32_t, etc. */
#include <stddef.h>     /* size_t, offsetof */
#include <unistd.h>     /* close, read, write (POSIX) */
#include <sys/socket.h> /* socket, bind, listen, accept (réseau) */
#include <arpa/inet.h>  /* htons, inet_pton (réseau) */
```

Le header reconstruit en section 20.4 doit aussi être inclus pour fournir les définitions de structures, d'énumérations et de constantes.

### Problème 5 : variables locales déclarées au début du bloc

Ghidra déclare toutes les variables locales en début de fonction, dans un style C89. Cela compile, mais rend le code moins lisible que le style C99/C11 où les variables sont déclarées au point de première utilisation. Le nettoyage consiste à déplacer les déclarations au plus près de leur usage quand cela améliore la clarté.

Ce nettoyage est optionnel — il ne change pas la sémantique — mais il rapproche considérablement le code de ce qu'un développeur humain écrirait.

### Problème 6 : code déroulé par le compilateur

Comme vu en section 20.2, les boucles déroulées par GCC apparaissent dans le pseudo-code comme du code linéaire répétitif. La correction est de **re-factoriser** le code en réintroduisant les boucles et les fonctions auxiliaires que le compilateur avait inlinées ou déroulées.

Pour `derive_key`, cela signifie extraire la boucle de hashing en une fonction séparée `mix_hash` et réintroduire la boucle externe sur les 4 rounds. C'est la transformation inverse de l'inlining/déroulage effectué par GCC. C'est aussi la correction la plus impactante sur la lisibilité, et celle qui nécessite le plus de compréhension de la logique du code.

### Problème 7 : return void explicite

Ghidra ajoute un `return;` explicite en fin de chaque fonction `void`. C'est valide en C mais non idiomatique — la convention habituelle est d'omettre le `return` en fin de fonction `void`. Le supprimer est cosmétique mais contribue à la « normalité » du code produit.

### Problème 8 : expressions inutilement complexes

Le décompilateur produit parfois des expressions correctes mais plus complexes que nécessaire. L'expression `(hash ^ uVar2) << 5 | (hash ^ uVar2) >> 0x1b` est correcte, mais elle calcule `hash ^ uVar2` deux fois. Un nettoyage naturel est d'introduire une variable temporaire :

```c
uint32_t tmp = hash ^ (uint32_t)username[i];  
hash = (tmp << 5) | (tmp >> 27);  /* rotate_left(tmp, 5) */  
```

Mieux encore, si l'on a identifié le pattern de rotation (section 20.2), on peut réintroduire une macro ou une fonction inline :

```c
static inline uint32_t rotate_left(uint32_t val, unsigned int n) {
    return (val << n) | (val >> (32 - n));
}
```

---

## Processus de nettoyage pas à pas

Voici le processus complet, dans l'ordre recommandé. Chaque étape construit sur la précédente.

### Étape 1 : préparer le squelette

Créer un fichier `.c` avec les includes nécessaires, inclure le header reconstruit, et ajouter un commentaire d'en-tête documentant l'origine :

```c
/*
 * keygenme_reconstructed.c
 *
 * Code reconstruit par décompilation de keygenme_O2_strip
 * SHA256 du binaire source : [hash]
 * Outils : Ghidra 11.x + RetDec 5.0
 * Date : [date]
 *
 * NOTE : ce code est une reconstruction approximative.
 * Il peut différer du source original dans les noms,
 * le style et les micro-optimisations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "keygenme_reconstructed.h"
```

### Étape 2 : remplacer les types Ghidra

Appliquer les remplacements mécaniques sur tout le fichier. Un script `sed` fait le travail :

```bash
sed -i \
    -e 's/\bundefined8\b/uint64_t/g' \
    -e 's/\bundefined4\b/uint32_t/g' \
    -e 's/\bundefined2\b/uint16_t/g' \
    -e 's/\bundefined\b/uint8_t/g' \
    -e 's/\bbyte\b/uint8_t/g' \
    -e 's/\bushort\b/uint16_t/g' \
    -e 's/\bulong\b/uint64_t/g' \
    -e 's/\blonglong\b/int64_t/g' \
    keygenme_reconstructed.c
```

L'ordre des remplacements est important : `undefined8` avant `undefined` pour éviter de transformer `undefined8` en `uint8_t8`.

### Étape 3 : première tentative de compilation

Compiler avec des warnings maximaux pour identifier les problèmes restants :

```bash
gcc -Wall -Wextra -Wpedantic -std=c11 -c keygenme_reconstructed.c
```

Le flag `-c` compile sans linker — on ne cherche pas encore à produire un exécutable, seulement à vérifier que le code est syntaxiquement et sémantiquement valide. Les warnings et erreurs guident les corrections suivantes.

Les erreurs typiques à ce stade sont : fonctions non déclarées (ajouter un prototype ou un include), types non définis (vérifier le header), et incompatibilités de pointeurs (ajuster les casts).

### Étape 4 : nettoyer les casts et les constantes

Parcourir le code manuellement pour supprimer les casts redondants et convertir les constantes hexadécimales en décimal là où c'est approprié. Cette étape est la plus subjective — elle relève du style de codage plutôt que de la correction.

### Étape 5 : refactoriser les constructions déroulées

Identifier les blocs de code répétitifs et les refactoriser en boucles et en fonctions auxiliaires. C'est l'étape qui demande le plus de compréhension de la logique et qui a le plus d'impact sur la lisibilité.

Pour `derive_key`, on réintroduit `mix_hash` et `rotate_left` comme fonctions séparées, et on replace la séquence linéaire de 4 rounds par une boucle `for`. Le résultat est un code structurellement proche de l'original :

```c
static uint32_t rotate_left(uint32_t value, unsigned int count) {
    count &= 31;
    return (value << count) | (value >> (32 - count));
}

static uint32_t mix_hash(const char *data, size_t len, uint32_t seed) {
    uint32_t h = seed;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h  = rotate_left(h, 5);
        h += (uint32_t)data[i] * 0x01000193;
        h ^= (h >> 16);
    }
    return h;
}

static void derive_key(const char *username, uint32_t seed, uint8_t *out) {
    size_t ulen = strlen(username);
    uint32_t state[ROUND_COUNT];

    state[0] = mix_hash(username, ulen, seed);
    for (int r = 1; r < ROUND_COUNT; r++) {
        state[r] = mix_hash(username, ulen, state[r - 1] ^ (uint32_t)r);
    }

    for (int r = 0; r < ROUND_COUNT; r++) {
        out[r * 4 + 0] = (uint8_t)(state[r] >> 24);
        out[r * 4 + 1] = (uint8_t)(state[r] >> 16);
        out[r * 4 + 2] = (uint8_t)(state[r] >>  8);
        out[r * 4 + 3] = (uint8_t)(state[r]);
    }
}
```

Ce code est une reconstruction fidèle — il n'est pas identique au source original (les noms de variables et le style diffèrent), mais il implémente la même logique et produit les mêmes résultats.

### Étape 6 : compilation complète et test

Une fois toutes les fonctions nettoyées, tenter une compilation complète avec linkage :

```bash
gcc -Wall -Wextra -std=c11 -o keygenme_reconstructed keygenme_reconstructed.c
```

Si des fonctions appelées ne sont pas reconstruites (par exemple des fonctions de bibliothèque non incluses), créer des **stubs** — des implémentations minimales qui permettent la compilation sans reproduire le comportement complet :

```c
/* Stub — à remplacer par l'implémentation réelle si nécessaire */
void unresolved_FUN_004018a0(void) {
    fprintf(stderr, "[STUB] FUN_004018a0 appelée\n");
    abort();
}
```

Les stubs permettent au code de compiler et de tourner partiellement. Chaque appel à un stub non implémenté est clairement signalé, ce qui guide le travail de reconstruction restant.

### Étape 7 : validation fonctionnelle

Le test ultime est de vérifier que le binaire reconstruit se comporte comme l'original. Pour `keygenme`, cela signifie :

```bash
# Tester avec le même input sur les deux binaires
echo -e "testuser\n12345678-12345678-12345678-12345678" | ./keygenme_O2_strip  
echo -e "testuser\n12345678-12345678-12345678-12345678" | ./keygenme_reconstructed  
```

Les deux doivent produire la même sortie. Si c'est le cas pour plusieurs inputs (y compris un input valide), le code reconstruit est fonctionnellement équivalent au binaire original.

Pour le serveur réseau (`server_O2_strip`), la validation consiste à lancer le serveur reconstruit et à connecter le client original — si la communication fonctionne, le protocole est correctement implémenté.

---

## Automatiser le nettoyage avec des scripts

Certaines étapes du nettoyage sont suffisamment mécaniques pour être scriptées. Voici un script Python minimal qui effectue les transformations les plus courantes :

```python
#!/usr/bin/env python3
"""
clean_ghidra_export.py — Nettoyage basique d'un export C de Ghidra.

Effectue les remplacements de types, supprime les return void,  
et reformate les constantes hexadécimales courantes.  

Usage : python3 clean_ghidra_export.py input.c > output.c
"""

import re  
import sys  

def clean(source):
    # Remplacement des types Ghidra (ordre important)
    replacements = [
        (r'\bundefined8\b', 'uint64_t'),
        (r'\bundefined4\b', 'uint32_t'),
        (r'\bundefined2\b', 'uint16_t'),
        (r'\bundefined\b',  'uint8_t'),
        (r'\bbyte\b',       'uint8_t'),
        (r'\bushort\b',     'uint16_t'),
        (r'\buint\b',       'uint32_t'),
        (r'\bulong\b',      'uint64_t'),
        (r'\blonglong\b',   'int64_t'),
    ]
    for pattern, replacement in replacements:
        source = re.sub(pattern, replacement, source)

    # Supprimer les "return;" en fin de fonction void
    source = re.sub(r'\n\s*return;\n\}', '\n}', source)

    # Simplifier les doubles casts (uint32_t)(uint8_t) sur des accès char[]
    source = re.sub(
        r'\(uint32_t\)\(uint8_t\)(\w+\[)',
        r'(uint8_t)\1',
        source
    )

    # Convertir i = i + 1 en i++
    source = re.sub(
        r'(\w+) = \1 \+ 1',
        r'\1++',
        source
    )

    # Convertir i = i - 1 en i--
    source = re.sub(
        r'(\w+) = \1 - 1',
        r'\1--',
        source
    )

    return source

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <fichier.c>", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1], 'r') as f:
        source = f.read()

    print(clean(source))
```

Ce script gère les cas simples. Les transformations complexes (refactorisation de boucles déroulées, réintroduction de fonctions auxiliaires) restent un travail humain — c'est la partie du RE qui nécessite la compréhension de la logique, pas seulement de la syntaxe.

---

## Quand s'arrêter

Le nettoyage du pseudo-code est un processus à rendements décroissants. Les premières corrections (types, includes, prototypes) sont mécaniques et apportent un gain immédiat. La refactorisation des boucles et des fonctions inlinées demande plus d'effort mais améliore considérablement la lisibilité. Au-delà, on entre dans le territoire du polissage cosmétique — renommer des variables pour coller aux conventions du projet, réorganiser les fonctions dans un ordre logique, ajouter des commentaires documentant la compréhension acquise.

Le point d'arrêt dépend de l'objectif de l'analyse :

**Pour un audit de sécurité**, le pseudo-code brut annoté dans Ghidra suffit généralement. Le livrable est un rapport d'analyse, pas du code recompilable. Le travail de nettoyage se limite au header reconstruit et aux annotations dans le projet Ghidra.

**Pour écrire un outil compagnon** (keygen, client de remplacement, plugin), on nettoie uniquement les fonctions nécessaires pour comprendre l'interface et les algorithmes. Le reste du binaire n'a pas besoin d'être reconstruit. Le header de la section 20.4 est souvent suffisant.

**Pour une reconstruction complète** (interopérabilité, portage, analyse de malware en profondeur), le nettoyage complet est justifié. C'est le scénario le plus exigeant et le plus rare.

**Pour un rapport de RE professionnel**, le couple header `.h` + pseudo-code nettoyé `.c` des fonctions clés constitue un livrable standard. Il documente la compréhension acquise de manière vérifiable (le code compile) et réutilisable (un autre analyste peut le reprendre).

Dans tous les cas, le code reconstruit doit être clairement identifié comme tel — les commentaires d'en-tête indiquant l'origine (binaire source, hash, outils utilisés, date) sont indispensables pour éviter toute confusion avec du code source authentique.

---

## Pièges spécifiques au code C++

Les binaires C++ ajoutent des difficultés supplémentaires au processus de nettoyage.

### Le pseudo-code C pour du C++

Ghidra produit du pseudo-code C, même quand le binaire d'origine est du C++. Les appels de méthodes virtuelles apparaissent comme des appels à travers des pointeurs de fonction, le pointeur `this` est un paramètre explicite, et les constructeurs/destructeurs contiennent du code d'initialisation de vtable qui n'a pas d'équivalent syntaxique direct en C.

Si l'objectif est de produire un fichier `.cpp` recompilable, il faut **réécrire** les classes plutôt que nettoyer le pseudo-code C. Le header C++ reconstruit en section 20.4 sert de squelette, et les corps de méthodes sont adaptés depuis le pseudo-code Ghidra en les transcrivant dans la syntaxe C++ avec `this->` implicite, appels de méthodes par nom, et héritage explicite.

### Les exceptions C++

Le pseudo-code de Ghidra inclut parfois des blocs `try/catch` reconstruits, mais plus souvent il montre le mécanisme bas niveau : appels à `__cxa_allocate_exception`, `__cxa_throw`, tables de cleanup dans `.gcc_except_table`. Nettoyer ce code en C++ valide nécessite de comprendre le mécanisme d'exceptions GCC (chapitre 17, section 4) et de le retranscrire en `throw`/`catch` idiomatiques.

### La STL

Le code template instancié de la STL est verbeux et spécifique à l'implémentation de libstdc++. Tenter de le nettoyer en code C++ utilisant directement `std::vector` et `std::map` est légitime, mais il faut accepter que le code reconstruit utilise la STL à un niveau d'abstraction plus élevé que ce que montre le pseudo-code — on remplace des dizaines de lignes de manipulation interne par un simple `vec.push_back(elem)`.

---

## Le fichier reconstruit comme base de travail vivante

Le code reconstruit n'est pas un livrable figé — c'est un document vivant qui évolue avec l'analyse. À mesure que l'on comprend mieux le binaire (en découvrant de nouvelles structures, en corrigeant des hypothèses de types, en identifiant de nouvelles fonctions), le code reconstruit est mis à jour.

L'organisation recommandée dans le dépôt de la formation est la suivante :

```
analysis/
├── keygenme/
│   ├── keygenme_reconstructed.h    ← header (section 20.4)
│   ├── keygenme_reconstructed.c    ← pseudo-code nettoyé
│   ├── notes.md                    ← journal d'analyse
│   └── Makefile                    ← compilation du code reconstruit
├── server/
│   ├── ch20_network_reconstructed.h
│   ├── server_reconstructed.c
│   ├── notes.md
│   └── Makefile
└── ...
```

Le `Makefile` dans chaque sous-répertoire compile le code reconstruit et exécute les tests de validation. Le fichier `notes.md` documente les décisions prises pendant l'analyse — pourquoi telle structure a été reconstruite de telle manière, quelles hypothèses restent non vérifiées, quelles parties du binaire n'ont pas été analysées.

Cette discipline de documentation transforme un travail d'analyse ponctuel en une base de connaissances réutilisable. C'est ce qui distingue une analyse professionnelle d'un exercice de décompilation ad hoc — et c'est exactement ce que mettent en pratique les cas pratiques de la Partie V.

---

> 🎯 **Checkpoint du chapitre 20** : produire un `.h` complet pour le binaire `ch20-network` (le serveur réseau strippé). Le header doit contenir toutes les constantes du protocole, les structures des messages, les énumérations des types et des commandes, et les signatures des fonctions principales. Il doit compiler sans erreur quand inclus dans un fichier C vide, et les tailles des structures doivent correspondre aux offsets observés dans Ghidra.  
> 

⏭️ [🎯 Checkpoint : produire un `.h` complet pour le binaire `ch20-network`](/20-decompilation/checkpoint.md)
