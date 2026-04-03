🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 20.2 — Ghidra Decompiler — qualité selon le niveau d'optimisation

> 📘 **Chapitre 20 — Décompilation et reconstruction du code source**  
> **Partie IV — Techniques Avancées de RE**

---

## Le décompilateur de Ghidra en contexte

Le décompilateur intégré à Ghidra est un moteur d'analyse appelé en interne **Pcode Decompiler**. Il opère en plusieurs phases : le code machine x86-64 est d'abord traduit en une représentation intermédiaire appelée P-code (un langage de micro-opérations indépendant de l'architecture), puis ce P-code subit des passes d'optimisation et de simplification, et enfin le résultat est présenté sous forme de pseudo-code C. Cette architecture en couches est ce qui permet à Ghidra de décompiler des architectures très différentes (ARM, MIPS, PowerPC…) avec le même moteur de reconstruction.

Pour l'analyste travaillant sur des binaires ELF compilés avec GCC, le décompilateur de Ghidra est l'outil central du workflow quotidien. Il est gratuit, open source, et sa qualité rivalise avec celle de Hex-Rays (le décompilateur commercial d'IDA) sur la plupart des binaires conventionnels. Ses forces et ses faiblesses spécifiques face aux différents niveaux d'optimisation de GCC font l'objet de cette section.

---

## Anatomie d'une session de décompilation dans Ghidra

Avant de comparer les niveaux d'optimisation, rappelons le workflow de base dans le CodeBrowser de Ghidra (couvert au chapitre 8).

Quand on sélectionne une fonction dans la vue **Listing** (désassemblage), le panneau **Decompiler** affiche immédiatement le pseudo-code correspondant. Les deux vues sont synchronisées : cliquer sur une ligne de pseudo-code surligne les instructions assembleur correspondantes, et vice versa. Cette synchronisation bidirectionnelle est l'un des atouts majeurs de Ghidra — elle permet exactement le croisement désassemblage/pseudo-code recommandé dans la section 20.1.

Le décompilateur ne travaille pas dans le vide. Il s'appuie sur plusieurs sources d'information que l'analyste peut enrichir au fil de l'analyse :

**Le type store.** Ghidra maintient une base de types (Data Type Manager) qui contient les types C/C++ standards, les types des bibliothèques connues (libc, libstdc++…), et les types définis par l'analyste. Plus cette base est riche et correcte, meilleur est le pseudo-code.

**Les signatures de fonctions.** Quand Ghidra connaît la signature d'une fonction (nombre et types des paramètres, type de retour), il peut propager ces informations dans tout le code appelant. Pour les fonctions de la libc appelées via PLT (`printf`, `strcmp`, `malloc`…), Ghidra applique automatiquement les bonnes signatures grâce à ses fichiers `.gdt` (Ghidra Data Types).

**Les symboles.** Si le binaire n'est pas strippé, Ghidra récupère les noms de fonctions depuis la table de symboles ELF. Si les informations DWARF sont présentes (`-g`), il récupère aussi les noms de variables locales, les types de paramètres, et les noms de structures — ce qui rend le pseudo-code presque identique au source.

---

## Décompilation à -O0 : le cas de référence

Commençons par importer `keygenme_O0_dbg` dans Ghidra (compilé avec `-O0 -g`). Après l'analyse automatique, naviguons vers la fonction `derive_key`. Voici ce que le décompilateur produit (simplifié pour la lisibilité) :

```c
void derive_key(char *username, uint32_t seed, uint8_t *out)
{
    size_t ulen;
    uint32_t state[4];
    int r;

    ulen = strlen(username);
    state[0] = mix_hash(username, ulen, seed);
    for (r = 1; r < 4; r = r + 1) {
        state[r] = mix_hash(username, ulen, state[r - 1] ^ (uint32_t)r);
    }
    for (r = 0; r < 4; r = r + 1) {
        out[r * 4]     = (uint8_t)(state[r] >> 0x18);
        out[r * 4 + 1] = (uint8_t)(state[r] >> 0x10);
        out[r * 4 + 2] = (uint8_t)(state[r] >> 8);
        out[r * 4 + 3] = (uint8_t)state[r];
    }
    return;
}
```

Le résultat est remarquablement fidèle au source original. Les noms de paramètres sont corrects (grâce à DWARF), la structure en deux boucles `for` est intacte, l'appel à `mix_hash` est clairement visible, et les opérations de shift pour extraire les octets sont identiques. Les différences sont cosmétiques : `0x18` au lieu de `24`, `r = r + 1` au lieu de `r++`, et les constantes affichées en hexadécimal.

### Ce qui fonctionne bien à -O0

À ce niveau d'optimisation, le décompilateur de Ghidra excelle sur plusieurs points :

**Correspondance fonction-par-fonction.** Chaque fonction du source existe comme une fonction distincte dans le binaire. Pas d'inlining, pas de fusion — la navigation dans le Symbol Tree de Ghidra reflète fidèlement l'organisation du code.

**Variables locales sur la pile.** En `-O0`, GCC alloue chaque variable locale sur la pile. Ghidra les identifie clairement comme `local_XX` (ou avec leur vrai nom si DWARF est disponible) et peut leur attribuer un type cohérent.

**Structures de contrôle intactes.** Les boucles `for`, les `if/else`, les `switch` sont reconstruits tels quels. Il n'y a pas de loop inversion, pas de conversion en `cmov`, pas de déroulage.

**Appels de fonction explicites.** Chaque appel est un vrai `call` dans le désassemblage. Le décompilateur affiche l'appel avec les bons arguments passés dans les registres `rdi`, `rsi`, `rdx`… selon la convention System V AMD64.

### Ce qui manque quand même

Même dans ce cas favorable, certaines pertes sont visibles.

Le décompilateur ne reconstitue pas les macros : `ROUND_COUNT` apparaît comme le littéral `4`, et `MAGIC_SEED` comme `0xdeadbeef`. Le `typedef license_ctx_t` n'existe pas — Ghidra montre un accès à un bloc mémoire sur la pile avec des offsets. Et la distinction entre `uint8_t *` et `char *` pour le buffer de sortie dépend de la qualité de l'information DWARF.

Chargeons maintenant la même fonction depuis `keygenme_O0` (compilé avec `-O0` **sans** `-g`). Les noms disparaissent : `username` devient `param_1`, `seed` devient `param_2`, `out` devient `param_3`, et les variables locales deviennent `local_38`, `local_2c`, etc. La structure du code est identique, mais il faut maintenant faire le travail de nommage manuellement.

---

## Décompilation à -O2 : le cas réaliste

Importons `keygenme_O2` dans un nouveau projet Ghidra. La première différence visible est que toutes les fonctions `static` ont été inlinées dans `main` — `derive_key`, `mix_hash` et `rotate_left` n'existent plus comme fonctions séparées. Le pseudo-code de `main` contient tout. Voici le passage correspondant à la logique de `derive_key`, extrait du pseudo-code de `main` (version représentative) :

```c
void derive_key(char *param_1, uint32_t param_2, uint8_t *param_3)
{
    uint32_t uVar1;
    size_t sVar2;
    uint32_t uVar3;
    uint32_t uVar4;
    int iVar5;
    size_t sVar6;

    sVar2 = strlen(param_1);
    uVar3 = param_2;
    for (sVar6 = 0; sVar6 < sVar2; sVar6 = sVar6 + 1) {
        uVar1 = (uint8_t)param_1[sVar6];
        uVar3 = ((uVar3 ^ uVar1) << 5 | (uVar3 ^ uVar1) >> 0x1b) +
                uVar1 * 0x1000193;
        uVar3 = uVar3 ^ uVar3 >> 0x10;
    }
    *param_3 = (uint8_t)(uVar3 >> 0x18);
    param_3[1] = (uint8_t)(uVar3 >> 0x10);
    param_3[2] = (uint8_t)(uVar3 >> 8);
    param_3[3] = (uint8_t)uVar3;
    uVar4 = uVar3 ^ 1;
    for (sVar6 = 0; sVar6 < sVar2; sVar6 = sVar6 + 1) {
        uVar1 = (uint8_t)param_1[sVar6];
        uVar4 = ((uVar4 ^ uVar1) << 5 | (uVar4 ^ uVar1) >> 0x1b) +
                uVar1 * 0x1000193;
        uVar4 = uVar4 ^ uVar4 >> 0x10;
    }
    param_3[4] = (uint8_t)(uVar4 >> 0x18);
    param_3[5] = (uint8_t)(uVar4 >> 0x10);
    /* ... suite pour les rounds 2 et 3 ... */
    return;
}
```

Le code est beaucoup plus long et dense. Analysons ce qui s'est passé.

### Inlining visible

L'appel à `mix_hash()` a disparu. Son corps — la boucle XOR/rotate/multiply — est directement intégré dans le code. De plus, `rotate_left(h, 5)` a été remplacé par son expression algébrique : `(val << 5) | (val >> 0x1b)`. Le décompilateur ne sait pas que ce pattern provient d'une fonction séparée dans le source ; il affiche l'expression brute.

Pour l'analyste, le réflexe est de reconnaître le pattern `(x << n) | (x >> (32 - n))` comme une rotation à gauche. C'est l'un des idiomes GCC les plus courants (Annexe I). Une fois identifié, on peut créer une macro ou un commentaire dans Ghidra pour clarifier le pseudo-code.

### Déroulage de la boucle externe

La boucle `for (r = 0; r < ROUND_COUNT; r++)` qui itérait 4 fois a été partiellement ou totalement déroulée par GCC. Au lieu de voir une boucle avec un compteur, on voit 4 blocs de code séquentiels, chacun contenant la boucle interne de hashing. Le décompilateur affiche cela comme du code linéaire — la notion de « 4 rounds » est perdue dans la structure, même si la répétition du pattern est visible pour un œil entraîné.

### Variables fusionnées dans les registres

En `-O0`, chaque élément du tableau `state[4]` avait son emplacement sur la pile. En `-O2`, GCC a gardé ces valeurs dans des registres (`eax`, `edx`, etc.) et les a réutilisés séquentiellement. Le décompilateur crée des variables temporaires (`uVar3`, `uVar4`…) qui ne correspondent à aucune variable du source original — ce sont des artefacts de l'allocation de registres.

### Stratégie de lecture

Face à ce pseudo-code optimisé, la méthode de travail dans Ghidra est la suivante :

1. **Identifier les blocs répétitifs.** Les 4 copies de la boucle interne indiquent un déroulage. Annoter chaque copie avec un commentaire (`// Round 0`, `// Round 1`, etc.) rend le code immédiatement plus lisible.

2. **Renommer les variables.** Remplacer `uVar3` par `hash_r0`, `uVar4` par `hash_r1`, etc. Dans Ghidra, un clic droit sur une variable → *Rename Variable* (ou `L`) propage le nom dans tout le pseudo-code de la fonction.

3. **Retyper les paramètres.** Cliquer droit sur `param_1` → *Retype Variable* et choisir `char *` (ou mieux, créer un type `const char *username`) améliore la lisibilité et peut déclencher une re-décompilation plus propre.

4. **Créer des types structurés.** Si l'on a identifié que `param_3` pointe vers un buffer de 16 octets rempli par groupes de 4, on peut créer un `typedef uint8_t key_bytes_t[16]` dans le Data Type Manager et l'appliquer.

---

## Décompilation à -O3 : le cas difficile

Importons `keygenme_O3`. À ce niveau, GCC peut vectoriser la boucle interne de `mix_hash` en utilisant des instructions SSE2. Le pseudo-code résultant dans Ghidra peut contenir des opérations sur des types `undefined16` (registres XMM de 128 bits) ou des appels à des intrinsics que Ghidra ne reconnaît pas toujours correctement.

### Vectorisation et types SIMD

Quand GCC vectorise une boucle, il remplace les opérations scalaires par des opérations packed opérant sur plusieurs éléments en parallèle. Le décompilateur de Ghidra tente de représenter ces opérations, mais le résultat est souvent un pseudo-code utilisant des casts vers des types de grande taille (`ulong *`, `undefined8 *`) et des opérations bit-à-bit complexes qui masquent la logique originale.

Sur notre `keygenme_O3`, voici le type de pseudo-code qu'on peut observer pour la boucle de hashing :

```c
    uVar2 = *(ulong *)(param_1 + sVar4);
    uVar7 = (uint)uVar2 ^ uVar5;
    uVar8 = uVar7 << 5 | uVar7 >> 0x1b;
    /* ... opérations entrelacées sur plusieurs octets simultanément ... */
```

GCC traite ici plusieurs octets du username en une seule opération de chargement mémoire (`ulong *`), puis décompose les valeurs. Le décompilateur montre ce chargement large mais ne comprend pas qu'il s'agit d'une optimisation d'accès mémoire sur une boucle caractère par caractère.

### Tail call optimization

Dans `oop_O3` (le binaire C++), GCC peut appliquer le tail call optimization sur les appels de méthodes virtuelles en fin de fonction. Dans le désassemblage, un `call` devient un `jmp`, et le décompilateur peut interpréter cela comme un saut dans le corps de la fonction appelée plutôt qu'un appel suivi d'un retour. Le résultat peut être une fusion apparente de deux fonctions dans le pseudo-code, ou un `goto` inexpliqué en fin de fonction.

### Quand revenir au désassemblage

À `-O3`, le pseudo-code atteint ses limites de lisibilité. C'est le moment où la vue Listing de Ghidra et la vue Function Graph reprennent l'avantage. La stratégie recommandée est de travailler par blocs : utiliser le pseudo-code pour la vue d'ensemble et la navigation (grâce aux cross-references), puis basculer vers le désassemblage pour les passages critiques où le pseudo-code est incompréhensible.

Dans Ghidra, la touche `Espace` dans la fenêtre Listing bascule entre le mode linéaire et le mode graphe. Le mode graphe est particulièrement utile pour visualiser les branches d'un `switch` ou la structure d'une boucle vectorisée — les blocs de base et leurs connexions sont souvent plus parlants que le pseudo-code aplati.

---

## Cas du C++ : le binaire oop.cpp

Le binaire `oop_O0_dbg` nous permet d'observer le comportement du décompilateur face au C++ compilé par G++. Plusieurs phénomènes spécifiques apparaissent.

### Appels virtuels et vtables

Naviguons vers la fonction `DeviceManager::process_all()`. Dans le pseudo-code, l'appel virtuel `dev->process()` apparaît sous une forme comme :

```c
    (**(code **)(**(long **)(this->devices_._M_start + lVar2) + 0x20))
        (*(long *)(this->devices_._M_start + lVar2));
```

Cette expression opaque est en réalité la mécanique du dispatch virtuel : accéder au vptr de l'objet (premier déréférencement), indexer dans la vtable à l'offset `0x20` (trouver le slot de `process()`), puis appeler le pointeur de fonction résultant. C'est correct mais illisible.

La solution dans Ghidra est de **reconstruire les classes et les vtables** manuellement (technique couverte en détail au chapitre 17, section 2). Une fois que l'analyste a créé un type `Device` avec un champ `vptr` pointant vers une structure `Device_vtable` contenant les pointeurs de fonction dans le bon ordre, le pseudo-code se simplifie considérablement :

```c
    device->vtable->process(device);
```

C'est encore du C et non du C++, mais la logique est lisible.

### Name mangling

Sans DWARF, les noms de fonctions C++ dans la table de symboles sont manglés selon l'Itanium ABI. Ghidra les démangle automatiquement dans la plupart des cas. Par exemple, `_ZN13DeviceManager11process_allEv` est affiché comme `DeviceManager::process_all()` dans le Symbol Tree et dans le pseudo-code. Cette fonctionnalité fonctionne bien avec G++, et c'est l'un des premiers indices que l'analyste exploite pour cartographier les classes sans DWARF.

En revanche, dans la version strippée (`oop_O2_strip`), les noms de symboles locaux disparaissent. Ghidra attribue des noms comme `FUN_00401a30`. Le démanglement ne peut s'appliquer que s'il reste des symboles dynamiques (exports/imports) ou des chaînes RTTI. Heureusement, G++ conserve les informations RTTI par défaut (sauf si le binaire est compilé avec `-fno-rtti`), et Ghidra peut les exploiter pour retrouver les noms de classes et la hiérarchie d'héritage même dans un binaire strippé.

### Conteneurs STL

Les accès à `std::vector`, `std::string` et `std::map` produisent un pseudo-code verbeux. Un simple `devices_.push_back(...)` dans le source se transforme en plusieurs appels à des fonctions templates instanciées (`std::vector<std::unique_ptr<Device>>::push_back`, `std::__uniq_ptr_impl<Device>::...`), avec de la gestion d'exceptions intercalée.

Ghidra affiche ces noms démanglés quand les symboles sont disponibles, ce qui est utile pour l'identification mais rend le pseudo-code très long. La stratégie pratique est de repérer ces appels STL, confirmer leur rôle, puis les « lire en diagonale » pour se concentrer sur la logique métier qui les entoure.

---

## Guider le décompilateur : les interventions de l'analyste

Le décompilateur de Ghidra ne fonctionne pas en circuit fermé. Chaque correction apportée par l'analyste déclenche une re-décompilation qui peut améliorer le résultat en cascade. Voici les interventions les plus impactantes, classées par ordre de rendement.

### Corriger les signatures de fonctions

C'est l'intervention au meilleur rapport effort/résultat. Si le décompilateur croit qu'une fonction prend 2 paramètres alors qu'elle en prend 3, toutes les fonctions appelantes afficheront un pseudo-code faux (le troisième argument apparaîtra comme une variable locale non initialisée). Corriger la signature via clic droit → *Edit Function Signature* propage instantanément la correction dans tout le binaire.

Sur les binaires GCC non strippés, les signatures sont généralement correctes grâce aux symboles. Sur les binaires strippés, c'est le premier travail à faire manuellement pour chaque fonction clé.

### Retyper les variables

Changer le type d'une variable locale de `undefined4` à `uint32_t`, ou d'un `long` à un `char *`, permet au décompilateur de reformuler les expressions qui utilisent cette variable. Un accès mémoire affiché comme `*(int *)(lVar1 + 0x20)` peut se transformer en `param->field_0x20` si le type du paramètre est correctement défini — et en `ctx->expected_key[0]` si la structure est entièrement reconstruite.

### Définir des structures dans le Data Type Manager

Créer un type `license_ctx_t` avec les bons champs aux bons offsets, puis l'appliquer au paramètre `this` ou à une variable locale, transforme radicalement le pseudo-code. Au lieu d'arithmétique de pointeurs avec des offsets numériques, on obtient des accès par nom de champ. C'est le point de bascule où le pseudo-code passe de « lisible avec effort » à « compréhensible au premier coup d'œil ».

### Appliquer des conventions d'appel

Si Ghidra se trompe sur la convention d'appel d'une fonction (par exemple en utilisant `__cdecl` au lieu de `__fastcall`, ou en ne détectant pas un paramètre passé dans un registre non standard), le pseudo-code de cette fonction et de tous ses appelants sera faux. Corriger la convention dans les propriétés de la fonction résout le problème en cascade.

---

## Tableau comparatif : qualité du pseudo-code selon les variantes

Le tableau ci-dessous synthétise la qualité typique du pseudo-code produit par Ghidra sur nos binaires d'entraînement, évaluée sur 5 critères.

| Critère | O0 + DWARF | O0 sans debug | O2 + symboles | O2 strippé | O3 strippé |  
|---|---|---|---|---|---|  
| Noms de fonctions | ✅ Originaux | ✅ Originaux | ✅ Originaux | ❌ `FUN_XXXXX` | ❌ `FUN_XXXXX` |  
| Noms de variables | ✅ Originaux | ❌ `local_XX` | ❌ `local_XX` | ❌ `local_XX` | ❌ `local_XX` |  
| Structures de contrôle | ✅ Fidèles | ✅ Fidèles | ⚠️ Modifiées | ⚠️ Modifiées | ❌ Méconnaissables |  
| Correspondance source | ~95 % | ~85 % | ~60 % | ~50 % | ~30 % |  
| Effort analyste requis | Minimal | Renommage | Renommage + retypage | Tout reconstruire | Tout + désassemblage |

Ces pourcentages sont indicatifs et varient selon la complexité du code source. La boucle de hashing de `keygenme.c` se décompile mieux que le dispatch virtuel de `oop.cpp`, quel que soit le niveau d'optimisation. À noter que dans `keygenme`, toutes les fonctions sont `static` et sont entièrement inlinées dans `main` dès `-O2` — les colonnes O2/O3 du tableau s'appliquent alors au pseudo-code de `main` dans son ensemble.

---

## Pièges courants et solutions

### Le piège du « ça a l'air correct »

Le décompilateur produit toujours quelque chose qui ressemble à du C valide. L'analyste peut être tenté de lire le pseudo-code comme il lirait un vrai source, en faisant confiance à la logique affichée. C'est dangereux. Un type mal inféré peut inverser silencieusement le sens d'une comparaison (`signed` vs `unsigned`), et une variable non initialisée dans le pseudo-code peut en réalité être un paramètre que Ghidra n'a pas détecté.

La règle pratique : **quand le pseudo-code d'un passage critique semble trop simple ou trop propre, vérifier le désassemblage**. Un `if` simple dans le pseudo-code peut masquer une logique plus complexe dans le code machine.

### Le piège de la décompilation stale

Ghidra ne re-décompile pas toujours automatiquement une fonction après des modifications dans une fonction voisine. Si l'on corrige la signature de `mix_hash` et que le pseudo-code de `derive_key` ne change pas, il faut forcer la re-décompilation : clic droit dans le Decompiler → *Commit Params/Return* sur la fonction modifiée, puis naviguer à nouveau vers la fonction appelante.

### Le piège des fonctions non détectées

Ghidra identifie les fonctions par analyse heuristique des prologues (`push rbp; mov rbp, rsp` ou `sub rsp, ...`). Dans un binaire strippé optimisé, certaines fonctions peuvent ne pas avoir de prologue standard (surtout les leaf functions qui n'utilisent pas la pile). Ghidra peut alors ne pas les détecter comme des fonctions distinctes, et les inclure dans le corps de la fonction précédente. Le symptôme est une fonction anormalement longue dans le pseudo-code. La solution est de créer manuellement la fonction à la bonne adresse dans la vue Listing (*Create Function* avec `F`).

---


⏭️ [RetDec (Avast) — décompilation statique offline](/20-decompilation/03-retdec.md)
