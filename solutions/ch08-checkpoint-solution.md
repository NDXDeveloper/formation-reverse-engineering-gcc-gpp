🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 8

> ⚠️ **Spoilers** — Ce document contient la solution complète du checkpoint du Chapitre 8. Ne le consultez qu'après avoir tenté l'exercice par vous-même.

---

## Contexte

Le binaire `ch08-oop_O0` est une application C++ compilée avec `g++ -O0 -g`. Il met en scène un petit système de gestion de véhicules utilisant le polymorphisme, l'héritage simple et l'héritage à deux niveaux. L'analyse est réalisée sur la variante avec symboles ; une section en fin de document aborde les différences pour la variante strippée.

---

## Étape 1 — Import et analyse

### Création du projet

1. **File → New Project → Non-Shared Project** dans le Project Manager.  
2. Répertoire : `~/ghidra-projects/`, nom : `ch08-checkpoint`.  
3. **File → Import File** → sélectionner `binaries/ch08-oop/ch08-oop_O0`.  
4. Vérifier les paramètres d'import détectés automatiquement :  
   - Format : `Executable and Linking Format (ELF)`  
   - Language : `x86:LE:64:default (gcc)`  
5. Valider l'import.

### Analyse automatique

Cliquer **Yes** pour lancer l'Auto Analysis. Vérifier que les analyseurs suivants sont bien activés (ils le sont par défaut) :

- **Demangler GNU** — essentiel pour le C++, transforme les symboles `_ZN*` en noms lisibles.  
- **GCC Exception Handlers** — parse les tables `.eh_frame` et `.gcc_except_table`.  
- **Decompiler Parameter ID** — déduit les types des paramètres via le décompileur.  
- **DWARF** — exploite les informations de débogage (présentes car compilé avec `-g`).  
- **ASCII Strings** — identifie les chaînes dans `.rodata`.

L'analyse prend quelques secondes sur ce petit binaire. Attendre que la barre de progression disparaisse avant de commencer l'exploration.

---

## Étape 2 — Orientation et identification des classes

### Exploration du Symbol Tree

Ouvrir le **Symbol Tree** (panneau gauche du CodeBrowser). Déplier le nœud **Functions**. Grâce au Demangler GNU et à DWARF, les fonctions sont organisées par namespace/classe :

```
Functions
├── main
├── Vehicle
│   ├── Vehicle(std::string, int)
│   ├── ~Vehicle()
│   ├── display_info()
│   ├── start()
│   └── get_fuel_level()
├── Car
│   ├── Car(std::string, int, int)
│   ├── ~Car()
│   ├── start()
│   └── open_trunk()
├── Motorcycle
│   ├── Motorcycle(std::string, int, bool)
│   ├── ~Motorcycle()
│   └── start()
├── ElectricCar
│   ├── ElectricCar(std::string, int, int, int)
│   ├── ~ElectricCar()
│   ├── start()
│   └── charge()
└── (fonctions de support : __libc_csu_init, _start, etc.)
```

**Observation** — Quatre classes identifiées directement depuis les namespaces : `Vehicle`, `Car`, `Motorcycle`, `ElectricCar`.

### Vérification par le RTTI

Recherche des typeinfo names via **Search → For Strings**, filtre `Vehicle` :

| Adresse | Chaîne | Classe |  
|---|---|---|  
| `0x00402200` | `7Vehicle` | `Vehicle` |  
| `0x00402210` | `3Car` | `Car` |  
| `0x00402218` | `10Motorcycle` | `Motorcycle` |  
| `0x00402228` | `11ElectricCar` | `ElectricCar` |

Navigation vers les structures `typeinfo for` associées :

**`typeinfo for Vehicle`** (`0x00402240`) :

```
.rodata:00402240     addr    vtable for __class_type_info + 0x10
.rodata:00402248     addr    typeinfo name for Vehicle    ; → "7Vehicle"
```

→ `__class_type_info` : **Vehicle est une classe racine** (pas de parent polymorphe).

**`typeinfo for Car`** (`0x00402260`) :

```
.rodata:00402260     addr    vtable for __si_class_type_info + 0x10
.rodata:00402268     addr    typeinfo name for Car        ; → "3Car"
.rodata:00402270     addr    typeinfo for Vehicle         ; parent
```

→ `__si_class_type_info` avec `__base_type` → `Vehicle` : **Car hérite de Vehicle**.

**`typeinfo for Motorcycle`** (`0x00402280`) :

```
.rodata:00402280     addr    vtable for __si_class_type_info + 0x10
.rodata:00402288     addr    typeinfo name for Motorcycle ; → "10Motorcycle"
.rodata:00402290     addr    typeinfo for Vehicle         ; parent
```

→ **Motorcycle hérite de Vehicle**.

**`typeinfo for ElectricCar`** (`0x004022a0`) :

```
.rodata:004022a0     addr    vtable for __si_class_type_info + 0x10
.rodata:004022a8     addr    typeinfo name for ElectricCar ; → "11ElectricCar"
.rodata:004022b0     addr    typeinfo for Car              ; parent = Car, pas Vehicle
```

→ **ElectricCar hérite de Car** (qui lui-même hérite de Vehicle).

---

## Étape 3 — Hiérarchie de classes reconstruite

```
Vehicle                     ← classe racine (__class_type_info)
├── Car                     ← héritage simple (__si_class_type_info, base = Vehicle)
│   └── ElectricCar         ← héritage simple (__si_class_type_info, base = Car)
└── Motorcycle              ← héritage simple (__si_class_type_info, base = Vehicle)
```

**Preuves** :

- La hiérarchie est confirmée par trois sources indépendantes : le RTTI (pointeurs `__base_type`), l'ordre d'initialisation des vptrs dans les constructeurs, et les appels aux constructeurs parents visibles dans le Decompiler.  
- Toutes les relations sont de l'héritage simple (toutes les classes dérivées utilisent `__si_class_type_info`, pas `__vmi_class_type_info`).

---

## Étape 4 — Analyse des vtables

### `vtable for Vehicle` (`0x00402300`)

```
.rodata:004022f0     addr    0x0                          ; offset-to-top
.rodata:004022f8     addr    typeinfo for Vehicle         ; RTTI
.rodata:00402300     addr    Vehicle::~Vehicle() [D1]     ; complete destructor
.rodata:00402308     addr    Vehicle::~Vehicle() [D0]     ; deleting destructor
.rodata:00402310     addr    Vehicle::start()             ; slot [2]
.rodata:00402318     addr    Vehicle::display_info()      ; slot [3]
```

→ 4 entrées (2 destructeurs + 2 méthodes virtuelles : `start` et `display_info`).

### `vtable for Car` (`0x00402340`)

```
.rodata:00402330     addr    0x0                          ; offset-to-top
.rodata:00402338     addr    typeinfo for Car             ; RTTI
.rodata:00402340     addr    Car::~Car() [D1]             ; override destructeur
.rodata:00402348     addr    Car::~Car() [D0]             ; override destructeur
.rodata:00402350     addr    Car::start()                 ; override de Vehicle::start()
.rodata:00402358     addr    Vehicle::display_info()      ; HÉRITÉ (même pointeur que Vehicle)
```

→ `Car` surcharge `start()` (adresse différente de `Vehicle::start()`) mais **hérite** `display_info()` sans la surcharger (même adresse que dans la vtable de `Vehicle`).

### `vtable for Motorcycle` (`0x00402380`)

```
.rodata:00402370     addr    0x0
.rodata:00402378     addr    typeinfo for Motorcycle
.rodata:00402380     addr    Motorcycle::~Motorcycle() [D1]
.rodata:00402388     addr    Motorcycle::~Motorcycle() [D0]
.rodata:00402390     addr    Motorcycle::start()          ; override
.rodata:00402398     addr    Vehicle::display_info()      ; HÉRITÉ
```

→ Même pattern que `Car` : surcharge `start()`, hérite `display_info()`.

### `vtable for ElectricCar` (`0x004023c0`)

```
.rodata:004023b0     addr    0x0
.rodata:004023b8     addr    typeinfo for ElectricCar
.rodata:004023c0     addr    ElectricCar::~ElectricCar() [D1]
.rodata:004023c8     addr    ElectricCar::~ElectricCar() [D0]
.rodata:004023d0     addr    ElectricCar::start()         ; override
.rodata:004023d8     addr    Vehicle::display_info()      ; HÉRITÉ (de Vehicle, via Car)
```

→ `ElectricCar` surcharge `start()` une troisième fois. `display_info()` reste celle de `Vehicle` à travers toute la hiérarchie.

### Tableau récapitulatif des vtables

| Slot | Vehicle | Car | Motorcycle | ElectricCar |  
|---|---|---|---|---|  
| [0] D1 dtor | `Vehicle::~Vehicle` | `Car::~Car` | `Motorcycle::~Motorcycle` | `ElectricCar::~ElectricCar` |  
| [1] D0 dtor | `Vehicle::~Vehicle` | `Car::~Car` | `Motorcycle::~Motorcycle` | `ElectricCar::~ElectricCar` |  
| [2] `start` | `Vehicle::start` | **`Car::start`** | **`Motorcycle::start`** | **`ElectricCar::start`** |  
| [3] `display_info` | `Vehicle::display_info` | `Vehicle::display_info` | `Vehicle::display_info` | `Vehicle::display_info` |

Les cellules en **gras** indiquent une surcharge (pointeur différent du parent). Les cellules normales indiquent un héritage sans surcharge (même pointeur).

**Observation** — Aucune classe ne contient `__cxa_pure_virtual` dans sa vtable. Toutes les classes sont **concrètes** (instanciables). `Vehicle` possède une implémentation par défaut de `start()`, confirmée par la lecture de son pseudo-code (elle affiche un message générique).

---

## Étape 5 — Analyse des constructeurs et reconstruction des champs

### `Vehicle::Vehicle(std::string, int)`

Pseudo-code du Decompiler après analyse DWARF :

```c
void Vehicle::Vehicle(Vehicle * this, std::string name, int fuel_level)
{
    *(void **)this = &vtable_for_Vehicle + 0x10;  // vptr → vtable Vehicle
    *(std::string *)(this + 0x08) = name;          // copie de la string
    *(int *)(this + 0x28) = fuel_level;             // entier
    return;
}
```

Appels à `operator new` observés dans `main` : `operator new(0x30)` pour un `Vehicle`.

→ `sizeof(Vehicle)` = **0x30 (48 octets)**.

Layout déduit :

| Offset | Taille | Type | Champ | Source |  
|---|---|---|---|---|  
| `0x00` | 8 | `void *` | `vptr` | Écriture de l'adresse de la vtable dans le constructeur |  
| `0x08` | 32 | `std::string` | `name` | Initialisé par copie du paramètre `name` ; la taille de 32 octets correspond au `sizeof(std::string)` de libstdc++ sur x86-64 |  
| `0x28` | 4 | `int` | `fuel_level` | Initialisé par le paramètre `fuel_level` |  
| `0x2c` | 4 | — | *(padding)* | Alignement à 0x30 pour la taille totale de la structure |

### `Car::Car(std::string, int, int)`

```c
void Car::Car(Car * this, std::string name, int fuel_level, int num_doors)
{
    Vehicle::Vehicle((Vehicle *)this, name, fuel_level);  // appel au constructeur parent
    *(void **)this = &vtable_for_Car + 0x10;               // écrase le vptr → vtable Car
    *(int *)(this + 0x30) = num_doors;                      // champ propre
    return;
}
```

Appel `operator new(0x38)` dans `main`.

→ `sizeof(Car)` = **0x38 (56 octets)**.

Layout déduit :

| Offset | Taille | Type | Champ | Source |  
|---|---|---|---|---|  
| `0x00` | 8 | `void *` | `vptr` | Hérité (écrasé vers vtable Car) |  
| `0x08` | 32 | `std::string` | `name` | Hérité de Vehicle |  
| `0x28` | 4 | `int` | `fuel_level` | Hérité de Vehicle |  
| `0x2c` | 4 | — | *(padding hérité)* | — |  
| `0x30` | 4 | `int` | `num_doors` | Propre à Car |  
| `0x34` | 4 | — | *(padding)* | Alignement à 0x38 |

**Indice clé** — Le constructeur de `Car` appelle d'abord `Vehicle::Vehicle(this, ...)`, puis écrase le vptr. C'est le pattern standard de l'Itanium ABI : le constructeur parent initialise le vptr vers sa propre vtable, puis le constructeur dérivé le remplace par sa vtable. En reverse, ce double-écriture du vptr confirme la relation d'héritage.

### `Motorcycle::Motorcycle(std::string, int, bool)`

```c
void Motorcycle::Motorcycle(Motorcycle * this, std::string name, int fuel_level,
                             bool has_sidecar)
{
    Vehicle::Vehicle((Vehicle *)this, name, fuel_level);
    *(void **)this = &vtable_for_Motorcycle + 0x10;
    *(bool *)(this + 0x30) = has_sidecar;
    return;
}
```

Appel `operator new(0x38)` dans `main`.

→ `sizeof(Motorcycle)` = **0x38 (56 octets)** (même taille que `Car`, mais un seul champ propre de type `bool` avec 7 octets de padding).

| Offset | Taille | Type | Champ | Source |  
|---|---|---|---|---|  
| `0x00` – `0x2f` | 48 | — | *(champs hérités de Vehicle)* | Identiques au layout de Vehicle |  
| `0x30` | 1 | `bool` | `has_sidecar` | Propre à Motorcycle |  
| `0x31` – `0x37` | 7 | — | *(padding)* | Alignement à 0x38 |

### `ElectricCar::ElectricCar(std::string, int, int, int)`

```c
void ElectricCar::ElectricCar(ElectricCar * this, std::string name,
                               int fuel_level, int num_doors, int battery_kw)
{
    Car::Car((Car *)this, name, fuel_level, num_doors);  // appel à Car, pas Vehicle
    *(void **)this = &vtable_for_ElectricCar + 0x10;
    *(int *)(this + 0x38) = battery_kw;
    return;
}
```

Appel `operator new(0x40)` dans `main`.

→ `sizeof(ElectricCar)` = **0x40 (64 octets)**.

| Offset | Taille | Type | Champ | Source |  
|---|---|---|---|---|  
| `0x00` – `0x2f` | 48 | — | *(champs hérités de Vehicle)* | — |  
| `0x30` | 4 | `int` | `num_doors` | Hérité de Car |  
| `0x34` | 4 | — | *(padding hérité)* | — |  
| `0x38` | 4 | `int` | `battery_kw` | Propre à ElectricCar |  
| `0x3c` | 4 | — | *(padding)* | Alignement à 0x40 |

**Indice clé** — Le constructeur appelle `Car::Car` (pas `Vehicle::Vehicle`), confirmant que `ElectricCar` hérite de `Car`.

---

## Étape 6 — Structures créées dans le Data Type Manager

Quatre structures créées dans la catégorie du programme :

### `Vehicle` (0x30 = 48 octets)

```
struct Vehicle {
    void *          vptr;           // 0x00
    std::string     name;           // 0x08  (32 octets avec libstdc++)
    int             fuel_level;     // 0x28
    byte[4]         _padding;       // 0x2c
};
```

### `Car` (0x38 = 56 octets)

```
struct Car {
    Vehicle         _base;          // 0x00  (héritage, 48 octets)
    int             num_doors;      // 0x30
    byte[4]         _padding;       // 0x34
};
```

### `Motorcycle` (0x38 = 56 octets)

```
struct Motorcycle {
    Vehicle         _base;          // 0x00  (héritage, 48 octets)
    bool            has_sidecar;    // 0x30
    byte[7]         _padding;       // 0x31
};
```

### `ElectricCar` (0x40 = 64 octets)

```
struct ElectricCar {
    Car             _base;          // 0x00  (héritage, 56 octets)
    int             battery_kw;     // 0x38
    byte[4]         _padding;       // 0x3c
};
```

> 💡 **Note d'implémentation** — Deux approches sont possibles pour modéliser l'héritage dans le Data Type Manager : inclure la structure parente comme premier champ inline (comme ci-dessus), ou dupliquer les champs individuellement. La première approche est plus propre et reflète mieux la sémantique, mais le Decompiler peut afficher les accès comme `this->_base._base.fuel_level` au lieu de `this->fuel_level`. La seconde est moins élégante mais produit un pseudo-code plus lisible. Choisissez selon votre préférence.

Après application de ces structures aux paramètres `this` de chaque méthode (touche `T` dans le Decompiler), le pseudo-code devient immédiatement lisible. Par exemple, `ElectricCar::start()` :

```c
// Avant annotation
void FUN_00401800(long param_1)
{
    printf("Starting electric vehicle %s with %d kW battery\n",
           *(char **)(param_1 + 8), *(int *)(param_1 + 0x38));
    *(int *)(param_1 + 0x28) = *(int *)(param_1 + 0x28) - 1;
}

// Après application de la structure ElectricCar
void ElectricCar::start(ElectricCar * this)
{
    printf("Starting electric vehicle %s with %d kW battery\n",
           this->_base._base.name.c_str, this->battery_kw);
    this->_base._base.fuel_level = this->_base._base.fuel_level - 1;
}
```

---

## Étape 7 — Méthodes non-virtuelles identifiées

En plus des méthodes virtuelles listées dans les vtables, certaines classes possèdent des méthodes **non-virtuelles** qui n'apparaissent pas dans les vtables mais sont visibles dans le Symbol Tree :

| Classe | Méthode non-virtuelle | Indice d'identification |  
|---|---|---|  
| `Vehicle` | `get_fuel_level()` | Présente dans le namespace `Vehicle` du Symbol Tree. Pas d'entrée dans la vtable. Appelée directement par `CALL` (pas via indirection vptr). |  
| `Car` | `open_trunk()` | Même observation. Appel direct, pas de slot vtable. |  
| `ElectricCar` | `charge()` | Appel direct uniquement. |

**Comment distinguer une méthode virtuelle d'une méthode non-virtuelle dans le désassemblage** — Les appels virtuels passent par une indirection : le code charge le vptr depuis l'objet (`MOV RAX, [RDI]`), indexe dans la vtable (`MOV RAX, [RAX + offset]`), puis appelle via le registre (`CALL RAX`). Les appels non-virtuels utilisent un `CALL` direct vers une adresse constante. Dans le Decompiler, l'appel virtuel apparaît comme `(*this->vptr[N])(this, ...)` et l'appel direct comme `ClassName::method(this, ...)`.

---

## Étape 8 — Analyse de `main()`

Le pseudo-code de `main()` après annotation complète résume le fonctionnement du programme :

```c
int main(int argc, char ** argv)
{
    Vehicle * fleet[4];
    
    fleet[0] = new Vehicle("Truck", 80);
    fleet[1] = new Car("Sedan", 60, 4);
    fleet[2] = new Motorcycle("Harley", 20, false);
    fleet[3] = new ElectricCar("Tesla", 100, 4, 75);
    
    for (int i = 0; i < 4; i++) {
        fleet[i]->start();          // appel virtuel → dispatch polymorphe
        fleet[i]->display_info();   // appel virtuel → Vehicle::display_info pour tous
    }
    
    ((Car *)fleet[1])->open_trunk();       // appel non-virtuel, cast explicite
    ((ElectricCar *)fleet[3])->charge();   // appel non-virtuel, cast explicite
    
    for (int i = 0; i < 4; i++) {
        delete fleet[i];            // appel virtuel au deleting destructor [D0]
    }
    
    return 0;
}
```

**Observations clés dans `main()`** :

- Le tableau `fleet` est un tableau de `Vehicle *` — le polymorphisme est démontré par le fait que les appels à `start()` passent tous par le vptr mais exécutent des implémentations différentes selon le type réel de l'objet.  
- Les appels à `open_trunk()` et `charge()` ne passent pas par la vtable (méthodes non-virtuelles), et le code effectue un cast vers le type dérivé avant l'appel.  
- Les `delete` appellent le destructeur virtuel via le slot [1] (deleting destructor D0), ce qui garantit que le bon destructeur est appelé même à travers un pointeur de base.

---

## Résumé du livrable

### Diagramme de hiérarchie

```
Vehicle                     [concrète, 0x30 octets, 2 méthodes virtuelles + 1 non-virtuelle]
├── Car                     [concrète, 0x38 octets, override start(), +1 non-virtuelle]
│   └── ElectricCar         [concrète, 0x40 octets, override start(), +1 non-virtuelle]
└── Motorcycle              [concrète, 0x38 octets, override start()]
```

### Métriques

- **4 classes** identifiées et documentées.  
- **3 relations d'héritage** reconstruites, toutes confirmées par le RTTI, les constructeurs et les vtables.  
- **4 vtables** analysées, avec 4 slots chacune (2 destructeurs + 2 méthodes virtuelles).  
- **4 structures** créées dans le Data Type Manager et appliquées aux méthodes.  
- **1 méthode virtuelle surchargée** (`start()`) par chaque classe dérivée.  
- **1 méthode virtuelle héritée sans surcharge** (`display_info()`) à travers toute la hiérarchie.  
- **3 méthodes non-virtuelles** identifiées (`get_fuel_level`, `open_trunk`, `charge`).

---

## Extension : analyse de `ch08-oop_O2_strip`

Sur la variante optimisée et strippée, les différences principales observées sont :

**Noms de fonctions perdus** — Toutes les fonctions apparaissent comme `FUN_XXXXXXXX`. Le Symbol Tree n'a plus de namespaces de classes. Les méthodes doivent être identifiées par leur contenu et leur position dans les vtables.

**RTTI toujours présent** — Les chaînes `7Vehicle`, `3Car`, `10Motorcycle`, `11ElectricCar` sont toujours dans `.rodata`. Les structures `typeinfo for` sont intactes. La hiérarchie d'héritage est reconstructible exactement comme sur le binaire non-strippé.

**Inlining partiel** — Avec `-O2`, certaines méthodes courtes comme `get_fuel_level()` sont inlinées dans `main()`. Elles disparaissent en tant que fonctions distinctes. Le nombre de fonctions détectées est inférieur.

**Constructeurs simplifiés** — Les constructeurs peuvent être fusionnés ou partiellement inlinés. Le pattern d'initialisation du vptr reste identifiable, mais les appels aux constructeurs parents peuvent être inlinés directement.

**Temps d'analyse supplémentaire** — Environ 2 à 3 fois plus long que la version avec symboles, principalement à cause du travail de renommage manuel et de l'identification des méthodes sans l'aide du Demangler.

**Conclusion** — Le RTTI est le pivot de l'analyse sur un binaire C++ strippé. Tant qu'il est présent, la hiérarchie de classes est récupérable. Les noms de méthodes nécessitent une analyse fonctionnelle (lecture du pseudo-code) pour être reconstitués.

⏭️
