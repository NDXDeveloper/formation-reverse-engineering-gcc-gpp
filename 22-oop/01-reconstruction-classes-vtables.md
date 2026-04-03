🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 22.1 — Reconstruction de la hiérarchie de classes et des vtables

> 🛠️ **Outils utilisés** : `nm`, `c++filt`, `readelf`, `objdump`, Ghidra  
> 📦 **Binaires** : `oop_O0`, `oop_O2_strip`, `plugins/plugin_alpha.so`, `plugins/plugin_beta.so`  
> 📚 **Prérequis** : Chapitre 17, sections 17.1 (name mangling), 17.2 (vtable/vptr), 17.3 (RTTI)

---

## Introduction

Quand on reverse un programme C procédural, la structure du code est relativement plate : des fonctions qui s'appellent les unes les autres, des données globales, un `main` qui orchestre le tout. Le graphe d'appels suffit souvent à comprendre l'architecture.

Avec du C++ orienté objet, la donne change radicalement. La logique est répartie dans des classes, les appels passent par des pointeurs de méthodes virtuelles, et les relations d'héritage définissent l'architecture du programme au moins autant que le flux de contrôle. Reconstruire cette architecture — la hiérarchie de classes, les interfaces, les implémentations concrètes — est la première étape indispensable avant toute analyse approfondie.

Cette section vous guide pas à pas, du triage initial jusqu'à l'annotation complète dans Ghidra, en utilisant notre binaire `ch22-oop`.

---

## Étape 1 — Triage : repérer les indices C++ dès les premières minutes

Avant même d'ouvrir un désassembleur, les outils en ligne de commande révèlent beaucoup d'informations sur un binaire C++.

### 1.1 — `strings` : les chaînes RTTI comme premier indice

Les chaînes RTTI sont stockées dans `.rodata` et suivent un format spécifique. Elles commencent par un chiffre (la longueur du nom encodé selon l'ABI Itanium) suivi du nom de la classe.

```bash
$ strings oop_O0 | grep -E '^[0-9]+[A-Z]'
```

Sur notre binaire avec symboles, vous obtiendrez des chaînes comme :

```
9Processor
19UpperCaseProcessor
16ReverseProcessor
8Pipeline
```

Ces chaînes sont des **typeinfo names** — chaque chaîne correspond à une classe polymorphe (c'est-à-dire possédant au moins une méthode virtuelle). Le chiffre en préfixe est la longueur du nom de la classe qui suit : `9Processor` signifie un nom de 9 caractères, « Processor ».

> 💡 **Astuce** : sur un binaire strippé, les chaînes RTTI survivent au stripping car elles font partie de `.rodata`, pas de `.symtab`. C'est souvent la seule information nominative restante. Seul le flag `-fno-rtti` à la compilation les supprime.

Testons sur la variante strippée :

```bash
$ strings oop_O2_strip | grep -E '^[0-9]+[A-Z]'
9Processor
19UpperCaseProcessor
16ReverseProcessor
8Pipeline
```

Les mêmes chaînes apparaissent. C'est votre point d'entrée, même sans aucun symbole.

### 1.2 — `nm -C` : les symboles démanglés

Sur un binaire non strippé, `nm -C` (ou `nm --demangle`) est une mine d'or. Filtrons les symboles liés à nos classes :

```bash
$ nm -C oop_O0 | grep -E '(vtable|typeinfo|Processor|Pipeline|Upper|Reverse)'
```

Résultat typique (adresses simplifiées) :

```
0000000000404a00 V vtable for Processor
0000000000404a40 V vtable for UpperCaseProcessor
0000000000404a90 V vtable for ReverseProcessor
0000000000404ae0 V vtable for Pipeline
0000000000404b20 V typeinfo for Processor
0000000000404b38 V typeinfo for UpperCaseProcessor
0000000000404b58 V typeinfo for ReverseProcessor
0000000000404b78 V typeinfo for Pipeline
0000000000404b90 V typeinfo name for Processor
0000000000404b9a V typeinfo name for UpperCaseProcessor
...
0000000000401a30 T UpperCaseProcessor::process(char const*, ...)
0000000000401b80 T ReverseProcessor::process(char const*, ...)
0000000000401230 T UpperCaseProcessor::name() const
0000000000401430 T ReverseProcessor::name() const
```

Plusieurs observations cruciales :

- Les symboles marqués `V` (weak global) dans `.rodata` sont les **vtables** et **typeinfo**. Leur adresse dans `.rodata` est fixe dans le binaire.  
- Les symboles marqués `T` dans `.text` sont les **implémentations des méthodes**.  
- Le démanglement (`-C`) transforme `_ZTV9Processor` en `vtable for Processor` et `_ZN19UpperCaseProcessor4nameEv` en `UpperCaseProcessor::name() const`.

### 1.3 — `readelf` : localiser les vtables dans les sections

Les vtables résident dans `.rodata` (ou parfois `.data.rel.ro` si elles contiennent des relocations). Vérifions :

```bash
$ readelf -S oop_O0 | grep -E '(rodata|data\.rel)'
  [16] .rodata           PROGBITS   0000000000404000  004000  000c80  ...
  [20] .data.rel.ro      PROGBITS   0000000000407000  007000  000200  ...
```

Avec GCC et `-rdynamic`, les vtables se retrouvent souvent dans `.data.rel.ro` car elles contiennent des pointeurs vers des fonctions dont les adresses sont fixées au chargement (relocations). C'est un détail technique important : si vous cherchez les vtables uniquement dans `.rodata`, vous risquez de les manquer.

---

## Étape 2 — Anatomie d'une vtable GCC (rappel appliqué)

Avant de plonger dans Ghidra, clarifions la structure exacte d'une vtable telle que GCC la produit selon l'ABI Itanium. Chaque vtable commence **deux entrées avant** le pointeur réellement utilisé par le vptr :

```
Adresse              Contenu                    Rôle
─────────────────────────────────────────────────────────────
vtable - 0x10        0x0000000000000000         offset-to-top (0 pour héritage simple)  
vtable - 0x08        ptr → typeinfo             pointeur vers la structure typeinfo  
vtable + 0x00  ←──── ptr → ~Destructor()        ← c'est ICI que pointe le vptr  
vtable + 0x08        ptr → name()  
vtable + 0x10        ptr → configure()  
vtable + 0x18        ptr → process()  
vtable + 0x20        ptr → status()  
```

Le vptr stocké dans l'objet pointe vers `vtable + 0x00`, c'est-à-dire **après** l'offset-to-top et le typeinfo. Quand vous lisez en mémoire :

- `[rdi + 0x00]` → vptr → pointe vers la première entrée de fonction  
- `[vptr + 0x00]` → destructeur virtuel  
- `[vptr + 0x08]` → `name()`  
- `[vptr + 0x10]` → `configure()`  
- `[vptr + 0x18]` → `process()`  
- `[vptr + 0x20]` → `status()`

> ⚠️ **Destructeur virtuel** : GCC émet souvent **deux entrées** pour le destructeur dans la vtable — le « complete object destructor » et le « deleting destructor » (qui appelle `operator delete` après destruction). Ne soyez pas surpris de voir deux pointeurs consécutifs avant `name()`. L'index exact dépend de la version de GCC et du contexte. Vérifiez toujours empiriquement avec les symboles en `-O0 -g` avant d'analyser la version strippée.

En pratique avec notre binaire, la vtable de `UpperCaseProcessor` ressemblera à :

```
.data.rel.ro + offset:
  [0x00]  0x0000000000000000               ← offset-to-top
  [0x08]  ptr → typeinfo for UpperCaseProcessor
  [0x10]  ptr → UpperCaseProcessor::~UpperCaseProcessor()   ← vptr pointe ici
  [0x18]  ptr → UpperCaseProcessor::~UpperCaseProcessor()   (deleting dtor)
  [0x20]  ptr → UpperCaseProcessor::name()
  [0x28]  ptr → UpperCaseProcessor::configure()
  [0x30]  ptr → UpperCaseProcessor::process()
  [0x38]  ptr → UpperCaseProcessor::status()
```

Chaque pointeur dans cette table est un lien direct vers une fonction dans `.text`. C'est grâce à ces pointeurs que vous allez pouvoir **relier chaque vtable à ses méthodes concrètes**.

---

## Étape 3 — Identifier la hiérarchie via la RTTI

### 3.1 — Structure typeinfo de l'ABI Itanium

Les structures `typeinfo` stockées dans `.rodata` / `.data.rel.ro` encodent les relations d'héritage. Leur layout dépend du type d'héritage :

**Classe sans parent polymorphe** (ou racine de la hiérarchie) — type `__class_type_info` :

```
typeinfo for Processor:
  [0x00]  ptr → vtable for __class_type_info + 0x10
  [0x08]  ptr → "9Processor"                              ← typeinfo name
```

**Classe avec un seul parent polymorphe** — type `__si_class_type_info` (« si » pour *single inheritance*) :

```
typeinfo for UpperCaseProcessor:
  [0x00]  ptr → vtable for __si_class_type_info + 0x10
  [0x08]  ptr → "19UpperCaseProcessor"                    ← typeinfo name
  [0x10]  ptr → typeinfo for Processor                    ← PARENT
```

C'est le champ à l'offset `+0x10` qui nous intéresse : il pointe vers le typeinfo du **parent direct**. En suivant ces pointeurs, vous reconstruisez l'arbre d'héritage complet.

### 3.2 — Lecture pratique avec `objdump`

```bash
$ objdump -s -j .data.rel.ro oop_O0 | head -40
```

Cette commande dumpe le contenu brut de la section. Vous y verrez les pointeurs en petit-boutiste (little-endian). En croisant avec les adresses des typeinfo connues via `nm -C`, vous pouvez reconstruire les liens parent-enfant manuellement.

En pratique cependant, c'est dans Ghidra que ce travail est le plus efficace, grâce au suivi automatique des pointeurs et aux cross-references.

### 3.3 — Reconstruction de l'arbre d'héritage

En appliquant la méthode ci-dessus à tous les typeinfo de notre binaire, on obtient :

```
Processor                    (classe abstraite, racine)
├── UpperCaseProcessor       (hérite de Processor)
└── ReverseProcessor         (hérite de Processor)

Pipeline                     (classe non-polymorphe ? À vérifier)
```

> 💡 `Pipeline` possède un destructeur mais aucune méthode virtuelle pure dans notre code. Selon que GCC émet ou non une vtable pour elle (elle a un destructeur non-virtuel dans notre implémentation), elle pourrait ne pas apparaître dans les vtables. Ce genre de nuance se découvre pendant l'analyse — ne vous attendez pas à une correspondance parfaite avec vos hypothèses initiales.

Pour les plugins, la même technique s'applique en analysant chaque `.so` séparément :

```bash
$ nm -C plugins/plugin_alpha.so | grep typeinfo
```

Vous y découvrirez `Rot13Processor` héritant de `Processor`, et dans `plugin_beta.so`, `XorCipherProcessor` héritant de `Processor`.

---

## Étape 4 — Analyse dans Ghidra : la reconstruction complète

### 4.1 — Import et analyse initiale

Importez `oop_O0` dans Ghidra (File → Import File). Dans les options d'analyse, assurez-vous que les options suivantes sont activées :

- **Demangler GNU** — traduit automatiquement les symboles manglés en noms C++ lisibles.  
- **Class Recovery from RTTI** — tente de reconstruire automatiquement les classes à partir des structures RTTI. Cette option est expérimentale mais donne de bons résultats sur les binaires GCC.  
- **Aggressive Instruction Finder** — utile pour les binaires strippés.

Lancez l'analyse (Analysis → Auto Analyze). Ghidra va identifier les fonctions, démanger les symboles et tenter de reconstruire les classes.

### 4.2 — Le Symbol Tree : vue d'ensemble des classes

Après l'analyse, ouvrez le **Symbol Tree** (panneau gauche) et naviguez vers la catégorie **Classes**. Si la récupération RTTI a fonctionné, vous devriez voir :

```
Classes/
├── Processor
│   ├── name()
│   ├── configure()
│   ├── process()
│   ├── status()
│   ├── ~Processor()
│   └── Processor()
├── UpperCaseProcessor
│   ├── name()
│   ├── configure()
│   ├── process()
│   ├── status()
│   └── ~UpperCaseProcessor()
├── ReverseProcessor
│   ├── ...
└── Pipeline
    ├── ...
```

Si Ghidra a correctement détecté l'héritage, les classes dérivées apparaissent comme enfants de `Processor` dans l'arbre. Sinon, vous devrez établir manuellement ces relations (ce qui sera le cas sur un binaire strippé).

### 4.3 — Localiser et annoter les vtables manuellement

Même quand l'analyse automatique fonctionne, il est essentiel de savoir retrouver les vtables soi-même. Voici la méthode.

**Méthode A — Depuis les symboles.** Dans le Symbol Tree, cherchez `vtable for UpperCaseProcessor` (ou `_ZTV19UpperCaseProcessor` si le démanglement n'a pas fonctionné). Double-cliquez pour naviguer à l'adresse correspondante dans le Listing. Vous verrez une suite de pointeurs 64 bits.

**Méthode B — Depuis les chaînes RTTI.** Ouvrez la fenêtre Defined Strings (Window → Defined Strings) et cherchez `UpperCaseProcessor`. Vous trouverez la chaîne typeinfo name `19UpperCaseProcessor`. Faites un clic droit → References → Show References to Address. Cela vous mène au typeinfo, et le typeinfo est référencé par la vtable (deux entrées avant le premier pointeur de fonction).

**Méthode C — Depuis un appel virtuel (méthode inverse).** Vous tombez sur un `call [rax+0x18]` dans le désassemblage. Vous remontez à `rax` qui vient d'un `mov rax, [rdi]` — c'est une lecture de vptr. Posez un breakpoint en dynamique (chapitre 11) pour obtenir l'adresse du vptr. Cette adresse pointe au milieu d'une vtable dans `.data.rel.ro`. De là, vous identifiez toutes les entrées.

### 4.4 — Créer des types structurés dans Ghidra

Une fois la vtable identifiée, l'étape suivante est de **créer une structure Ghidra** pour le type. Ouvrez le Data Type Manager (Window → Data Type Manager), faites un clic droit sur votre programme → New → Structure.

Pour `Processor` :

```
struct Processor {
    void*      vptr;         /* +0x00 — pointeur vers la vtable */
    uint32_t   id_;          /* +0x08 */
    int        priority_;    /* +0x0C — attention : vérifier l'alignement */
    bool       enabled_;     /* +0x10 */
    /* padding */            /* +0x11 à +0x17 */
};                           /* taille totale : 0x18 (24 octets) */
```

> 💡 L'alignement réel peut différer selon le niveau d'optimisation et les options de GCC. Les tailles ci-dessus correspondent à la version `-O0`. En `-O2`, GCC peut réorganiser ou optimiser le padding. Vérifiez toujours en observant les offsets utilisés par les `mov` dans le code des méthodes.

Pour `UpperCaseProcessor` (qui hérite de `Processor`) :

```
struct UpperCaseProcessor {
    /* hérité de Processor */
    void*      vptr;              /* +0x00 */
    uint32_t   id_;               /* +0x08 */
    int        priority_;         /* +0x0C */
    bool       enabled_;          /* +0x10 */
    /* propre à UpperCaseProcessor */
    bool       skip_digits_;      /* +0x11 — placé juste après enabled_ (pas de padding) */
    /* padding */                 /* +0x12 à +0x17 (6 octets, alignement pour size_t) */
    size_t     bytes_processed_;  /* +0x18 */
};  /* taille totale : 0x20 (32 octets) */
```

La manière la plus fiable de déterminer le layout exact est d'examiner les méthodes de la classe : chaque accès à `this->member` se traduit par un `mov ... [rdi + offset]` (en System V AMD64, `rdi` est `this` au début de la méthode). Notez systématiquement les offsets observés.

### 4.5 — Cross-references (XREF) : qui appelle quoi ?

Les cross-references sont votre arme principale pour comprendre le flot d'exécution à travers le polymorphisme.

**Sur une méthode** : placez le curseur sur `UpperCaseProcessor::process()` et appuyez sur `Ctrl+Shift+F` (ou clic droit → References → Show References To). Vous verrez :

- Une référence depuis la **vtable** (c'est l'entrée qui pointe vers cette méthode).  
- Des références depuis les sites d'appel **directs** (si le compilateur a dévirtualisé l'appel en `-O2`).

**Sur la vtable** : les XREF sur la vtable elle-même montrent **où la vtable est assignée au vptr** — c'est-à-dire dans le constructeur de la classe. Le pattern typique dans le constructeur est :

```asm
; Constructeur de UpperCaseProcessor
lea    rax, [vtable for UpperCaseProcessor + 0x10]  
mov    QWORD PTR [rdi], rax          ; this->vptr = &vtable[0]  
```

L'offset `+0x10` (ou `+0x10`) saute l'offset-to-top et le typeinfo pour pointer vers la première entrée de fonction. Chaque classe concrète a ce pattern dans son constructeur, et les XREF sur la vtable listent donc tous les constructeurs.

**Sur le typeinfo** : les XREF sur un typeinfo mènent à la vtable (qui le référence à l'offset `-0x08`) et éventuellement à des `dynamic_cast` ou des blocs `catch` qui l'utilisent pour la résolution de types à l'exécution.

### 4.6 — Function Graph : visualiser le dispatch

Pour un appel virtuel particulier, le **Function Graph** (Window → Function Graph) montre bien la séquence :

1. Chargement du vptr depuis l'objet  
2. Lecture de l'entrée dans la vtable  
3. Appel indirect

En survolant les nœuds du graphe, vous pouvez suivre le flux de données et identifier quel slot de vtable est appelé.

---

## Étape 5 — Le même travail sur un binaire strippé

Sur `oop_O2_strip`, les symboles de la table `.symtab` ont disparu. Plus de `vtable for UpperCaseProcessor`, plus de `UpperCaseProcessor::process()` dans `nm`. Voici ce qui change et ce qui reste.

### Ce qui disparaît

- Tous les noms de fonctions dans `.symtab` et `.dynsym` (sauf les symboles dynamiques nécessaires au linker : `dlopen`, `printf`, etc.).  
- Les noms de méthodes dans le Symbol Tree de Ghidra — les fonctions apparaissent comme `FUN_00401a30`, `FUN_00401b80`, etc.  
- Les labels `vtable for X` — les vtables sont toujours là, mais anonymes.

### Ce qui survit

- **Les chaînes RTTI** dans `.rodata` (`9Processor`, `19UpperCaseProcessor`…). Elles survivent au stripping standard. Seul `-fno-rtti` les supprime.  
- **Les structures typeinfo** — toujours en `.data.rel.ro`, toujours fonctionnelles.  
- **Les vtables elles-mêmes** — les tableaux de pointeurs de fonctions sont intacts.  
- **Les chaînes littérales** utilisées dans le code (`"UpperCaseProcessor"`, `"skip_digits"`, `"[UpperCase #%u] destroyed"`, etc.) — référencées par les méthodes.

### Stratégie de reconstruction sans symboles

1. **Partez des chaînes RTTI** pour identifier les classes existantes et leurs relations d'héritage (même méthode qu'à l'étape 3).

2. **Localisez les vtables** en cherchant les structures typeinfo : chaque typeinfo est précédée (à offset `-0x08`) par son adresse dans une vtable. Vous pouvez aussi chercher des patterns de pointeurs alignés dans `.data.rel.ro` qui pointent vers `.text`.

3. **Identifiez les méthodes** en suivant les pointeurs dans chaque vtable. Chaque entrée pointe vers une `FUN_XXXXXXXX` — c'est une méthode virtuelle. Renommez-la en fonction de sa position dans la vtable et de ce que révèle le décompileur.

4. **Utilisez les chaînes littérales** comme indices. Si `FUN_00401a30` contient une référence à la chaîne `"[UpperCase #%u] destroyed"`, c'est probablement le destructeur de `UpperCaseProcessor`. Si une autre fonction référence `"skip_digits"`, c'est probablement `configure()`.

5. **Comparez les vtables entre classes**. La vtable de la classe de base `Processor` contiendra potentiellement des entrées `__cxa_pure_virtual` (pour les méthodes virtuelles pures). Les vtables des classes dérivées auront les mêmes slots remplis avec des fonctions concrètes. L'entrée `__cxa_pure_virtual` est un marqueur fiable de classe abstraite.

6. **Vérifiez vos hypothèses** en examinant les constructeurs. Chaque constructeur assigne un vptr spécifique — les XREF sur une vtable identifient le constructeur de la classe correspondante.

> 💡 **La boucle d'identification** : vtable → méthodes → chaînes utilisées → nom de la classe → typeinfo → parent → vtable parent. Chaque pièce du puzzle vous aide à identifier la suivante. C'est rarement linéaire, mais toutes les informations se recoupent.

---

## Étape 6 — Reconstruction des classes des plugins `.so`

Les plugins (`plugin_alpha.so`, `plugin_beta.so`) sont des binaires ELF indépendants. Ils doivent être analysés séparément, mais la méthode est identique.

La particularité des plugins est qu'ils exportent des symboles `extern "C"` :

```bash
$ nm -CD plugins/plugin_alpha.so | grep ' T '
0000000000001200 T create_processor
0000000000001280 T destroy_processor
```

Ces deux symboles sont votre **point d'entrée** dans le plugin. En analysant `create_processor` dans Ghidra, vous verrez :

1. Un appel à `operator new` avec la taille de l'objet — cela vous donne le `sizeof` de la classe.  
2. L'appel au constructeur de la classe du plugin.  
3. Dans le constructeur, l'assignation du vptr — ce qui vous mène à la vtable du plugin.

De la vtable, vous retrouvez toutes les méthodes implémentées par le plugin.

La structure typeinfo du plugin pointe vers `typeinfo for Processor` dans l'exécutable principal (résolution via le linker dynamique grâce à `-rdynamic`). C'est ainsi que la RTTI fonctionne à travers les frontières de modules — et c'est aussi ainsi que vous confirmez que la classe du plugin hérite bien de `Processor`.

```
typeinfo for Rot13Processor (dans plugin_alpha.so):
  [0x00]  ptr → __si_class_type_info vtable
  [0x08]  ptr → "15Rot13Processor"
  [0x10]  ptr → typeinfo for Processor          ← relocation vers l'exécutable
```

---

## Étape 7 — Construire le diagramme de classes final

En combinant toutes les informations recueillies — de l'exécutable et des plugins — vous devriez pouvoir reconstruire le diagramme suivant :

```
                        ┌───────────────────────────┐
                        │      <<abstract>>         │
                        │       Processor           │
                        ├───────────────────────────┤
                        │ - id_: uint32_t           │
                        │ - priority_: int          │
                        │ - enabled_: bool          │
                        ├───────────────────────────┤
                        │ + ~Processor()            │   ← virtuel
                        │ + name(): const char*     │   ← virtuel pur
                        │ + configure(): bool       │   ← virtuel pur
                        │ + process(): int          │   ← virtuel pur
                        │ + status(): const char*   │   ← virtuel pur
                        │ + id(): uint32_t          │   ← non virtuel
                        │ + priority(): int         │   ← non virtuel
                        │ + enabled(): bool         │   ← non virtuel
                        │ + set_enabled(bool): void │   ← non virtuel
                        └──────────┬────────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
   ┌──────────┴────────┐   ┌───────┴────────┐   ┌───────┴──────────┐
   │ UpperCaseProcessor│   │ReverseProcessor│   │  (via plugins)   │
   ├───────────────────┤   ├────────────────┤   │                  │
   │- skip_digits_     │   │- word_mode_    │   │ Rot13Processor   │
   │- bytes_processed_ │   │- chunks_proc._ │   │ XorCipherProc.   │
   └───────────────────┘   └────────────────┘   └──────────────────┘
```

Les méthodes non virtuelles (`id()`, `priority()`, etc.) **n'apparaissent pas dans la vtable**. En `-O2`, elles sont souvent inlinées par GCC et disparaissent complètement en tant que fonctions distinctes. Vous les retrouverez sous forme d'accès mémoire directs (`mov eax, [rdi+0x08]` pour `id()`) dans le code appelant.

---

## Résumé de la méthode

La reconstruction de la hiérarchie de classes suit un processus itératif que l'on peut résumer ainsi :

**Avec symboles** (`-g`, non strippé) : les noms des vtables, des typeinfo et des méthodes sont directement lisibles. La reconstruction est quasi automatique avec Ghidra. Servez-vous de cette version pour **apprendre le layout** avant de passer à la version strippée.

**Sans symboles** (strippé) : les chaînes RTTI survivent et donnent les noms de classes. Les typeinfo encodent l'héritage. Les vtables contiennent les pointeurs vers les méthodes. Les chaînes littérales dans les méthodes confirment vos identifications. Le travail est plus long mais toutes les informations sont présentes — il faut simplement les relier manuellement.

**Sans RTTI** (`-fno-rtti` + strippé) : le cas le plus difficile. Plus de noms de classes, plus de typeinfo. Il faut identifier les vtables par leur structure (tableaux de pointeurs vers `.text` dans `.data.rel.ro`), les constructeurs par l'écriture du vptr, et les relations d'héritage par le partage des premiers slots de vtable entre classes parentes et enfants. Ce cas dépasse le cadre de ce chapitre, mais la méthode fondamentale reste la même — seuls les indices changent.

Quel que soit le niveau de difficulté, le principe directeur est le même : **la vtable est le noyau de l'analyse**. Trouvez les vtables, et vous trouverez les classes, leurs méthodes, leurs constructeurs, et leurs relations d'héritage.

---


⏭️ [RE d'un système de plugins (chargement dynamique `.so` via `dlopen`/`dlsym`)](/22-oop/02-systeme-plugins-dlopen.md)
