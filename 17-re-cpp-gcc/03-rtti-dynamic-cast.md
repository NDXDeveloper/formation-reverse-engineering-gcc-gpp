🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 17.3 — RTTI (Run-Time Type Information) et `dynamic_cast`

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Ce que la RTTI apporte au reverse engineer

La RTTI est un mécanisme du C++ qui permet au programme de déterminer le type réel d'un objet polymorphe à l'exécution. Pour le développeur, cela se manifeste à travers deux opérateurs : `typeid` (pour interroger le type) et `dynamic_cast` (pour effectuer un cast sûr avec vérification de type).

Pour le reverse engineer, la RTTI est une aubaine. GCC génère dans le binaire des **structures de métadonnées** qui contiennent en clair les noms de toutes les classes polymorphes, les relations d'héritage entre elles, et les offsets de sous-objets. Ces structures survivent au stripping car elles sont nécessaires au runtime. Savoir les lire, c'est pouvoir reconstruire la hiérarchie de classes complète d'un binaire C++ sans accès au code source.

> ⚠️ **Quand la RTTI est absente.** Le flag de compilation `-fno-rtti` désactive la génération de RTTI. Dans ce cas, les structures `typeinfo` sont remplacées par des pointeurs nuls dans les vtables, et ni `typeid` ni `dynamic_cast` ne sont disponibles. Certains projets (moteurs de jeux, systèmes embarqués) désactivent la RTTI pour réduire la taille du binaire. Le premier réflexe en RE est de vérifier si le champ typeinfo des vtables est nul ou non.

## Les structures typeinfo de l'Itanium ABI

L'Itanium C++ ABI définit trois types de structures typeinfo, chacune correspondant à un scénario d'héritage différent. Toutes dérivent (au sens C++) de `std::type_info` et sont instanciées par le compilateur dans `.rodata`.

### `__class_type_info` — classe sans base polymorphe

C'est la structure la plus simple, utilisée pour les classes qui n'héritent d'aucune autre classe polymorphe (les racines de la hiérarchie). Sa structure en mémoire est :

```
_ZTI<classe> :
┌─────────────────────────────────────────────┐
│  vptr → vtable de __class_type_info         │  offset 0   (8 octets)
├─────────────────────────────────────────────┤
│  __name → pointeur vers _ZTS<classe>        │  offset 8   (8 octets)
└─────────────────────────────────────────────┘
                                                 total : 16 octets
```

Le champ `vptr` pointe vers la vtable interne de `__class_type_info` (définie dans `libstdc++`), pas vers celle de la classe utilisateur. Le champ `__name` pointe vers la chaîne `_ZTS` qui contient le nom manglé de la classe sous forme lisible (par exemple, `5Shape` pour `Shape`).

**Exemple — `Shape` est la racine de la hiérarchie :**

```
_ZTI5Shape :
  [vptr]    → _ZTVN10__cxxabiv117__class_type_infoE+16
  [__name]  → _ZTS5Shape → "5Shape"
```

> 💡 **En RE :** le pointeur `vptr` de la structure typeinfo pointe vers une vtable de `libstdc++`. L'adresse de cette vtable identifie le **type de la structure typeinfo** elle-même, ce qui vous dit le type d'héritage de la classe. Trois vtables de `libstdc++` sont à reconnaître :  
> - `__class_type_info` → pas de classe parent polymorphe  
> - `__si_class_type_info` → héritage simple  
> - `__vmi_class_type_info` → héritage multiple ou virtuel

### `__si_class_type_info` — héritage simple non virtuel

Utilisée quand une classe hérite d'**une seule** base polymorphe, sans héritage virtuel. Elle étend `__class_type_info` avec un pointeur vers la typeinfo de la classe parente :

```
_ZTI<classe_dérivée> :
┌─────────────────────────────────────────────┐
│  vptr → vtable de __si_class_type_info      │  offset 0
├─────────────────────────────────────────────┤
│  __name → pointeur vers _ZTS<classe>        │  offset 8
├─────────────────────────────────────────────┤
│  __base_type → pointeur vers _ZTI<parent>   │  offset 16  (8 octets)
└─────────────────────────────────────────────┘
                                                 total : 24 octets
```

Le champ `__base_type` est un pointeur vers la structure `_ZTI` de la classe parente. C'est un **lien direct** dans la hiérarchie d'héritage.

**Exemple — `Circle` hérite de `Shape` :**

```
_ZTI6Circle :
  [vptr]        → _ZTVN10__cxxabiv120__si_class_type_infoE+16
  [__name]      → _ZTS6Circle → "6Circle"
  [__base_type] → _ZTI5Shape
```

En suivant le pointeur `__base_type`, on arrive à `_ZTI5Shape`, ce qui confirme que `Circle` hérite de `Shape`. En répétant l'opération pour toutes les typeinfo du binaire, on reconstruit la hiérarchie complète.

**Exemple — chaîne d'héritage `ParseError` → `AppException` → `std::exception` :**

```
_ZTI10ParseError :
  [vptr]        → __si_class_type_info
  [__name]      → "10ParseError"
  [__base_type] → _ZTI12AppException

_ZTI12AppException :
  [vptr]        → __si_class_type_info
  [__name]      → "12AppException"
  [__base_type] → _ZTISt9exception      (std::exception, dans libstdc++)
```

La chaîne de pointeurs `__base_type` forme un graphe orienté qui **est** la hiérarchie de classes.

### `__vmi_class_type_info` — héritage multiple ou virtuel

C'est la structure la plus complexe, utilisée pour les classes avec héritage multiple, héritage virtuel, ou les deux. Elle contient un tableau de descripteurs de bases :

```
_ZTI<classe> :
┌─────────────────────────────────────────────┐
│  vptr → vtable de __vmi_class_type_info     │  offset 0
├─────────────────────────────────────────────┤
│  __name → pointeur vers _ZTS<classe>        │  offset 8
├─────────────────────────────────────────────┤
│  __flags (unsigned int)                     │  offset 16  (4 octets)
├─────────────────────────────────────────────┤
│  __base_count (unsigned int)                │  offset 20  (4 octets)
├─════════════════════════════════════════════╡
│  __base_info[0] :                           │  offset 24
│    __base_type → _ZTI<parent_0>             │    +0  (8 octets)
│    __offset_flags (long)                    │    +8  (8 octets)
├─────────────────────────────────────────────┤
│  __base_info[1] :                           │  offset 40
│    __base_type → _ZTI<parent_1>             │    +0
│    __offset_flags (long)                    │    +8
├─────────────────────────────────────────────┤
│  ... (autant d'entrées que __base_count)    │
└─────────────────────────────────────────────┘
```

Les champs spécifiques :

**`__flags`** encode des propriétés de l'héritage :

| Bit | Masque | Signification |  
|-----|--------|---------------|  
| 0 | `0x1` | `__non_diamond_repeat_mask` — une base apparaît plus d'une fois dans la hiérarchie sans diamant |  
| 1 | `0x2` | `__diamond_shaped_mask` — héritage en diamant détecté |

**`__base_count`** est le nombre de classes parentes directes.

**`__base_info[i].__offset_flags`** est une valeur de 8 octets qui encode à la fois l'offset du sous-objet et des flags :

| Bits | Masque | Signification |  
|------|--------|---------------|  
| 0 | `0x1` | `__virtual_mask` — héritage virtuel |  
| 1 | `0x2` | `__public_mask` — héritage public |  
| 8–63 | `>> 8` | Offset du sous-objet dans l'objet complet (en octets) |

L'offset est stocké dans les bits de poids fort (décalage de 8 bits vers la droite pour l'extraire). Les deux bits de poids faible sont les flags.

**Exemple — `Canvas` hérite de `Drawable` et `Serializable` :**

```
_ZTI6Canvas :
  [vptr]         → __vmi_class_type_info
  [__name]       → "6Canvas"
  [__flags]      = 0x0                (pas de diamant, pas de répétition)
  [__base_count] = 2                  (deux classes parentes)

  __base_info[0] :
    [__base_type]    → _ZTI8Drawable
    [__offset_flags] = 0x0000000000000002
                       offset = 0 >> 8 = 0 (sous-objet à l'offset 0)
                       flags  = 0x2 = public, non virtuel

  __base_info[1] :
    [__base_type]    → _ZTI12Serializable
    [__offset_flags] = 0x0000000000000802
                       offset = 0x802 >> 8 = 0x8 = 8 (sous-objet à l'offset 8)
                       flags  = 0x2 = public, non virtuel
```

Cette structure nous dit que `Canvas` hérite publiquement de `Drawable` (sous-objet à l'offset 0) et de `Serializable` (sous-objet à l'offset 8). C'est exactement le layout mémoire que nous avons vu en section 17.2.

## Les chaînes typeinfo name (`_ZTS`)

Chaque structure `_ZTI` référence une chaîne `_ZTS` stockée dans `.rodata`. Cette chaîne contient le nom de la classe au format manglé Itanium, mais **sans le préfixe `_Z`** — c'est directement la forme `<longueur><nom>`.

```bash
$ strings oop_O0 | grep -E '^[0-9]+[A-Z][a-z]'
5Shape
6Circle
9Rectangle
8Triangle
8Drawable
12Serializable
6Canvas
6Config
12AppException
10ParseError
12NetworkError
```

Ces chaînes sont des marqueurs extrêmement fiables en RE car :

1. Elles sont toujours dans `.rodata` (section en lecture seule).  
2. Elles survivent au stripping (nécessaires pour la RTTI au runtime).  
3. Leur format `<longueur><nom>` est facile à reconnaître automatiquement.  
4. Elles contiennent le **nom exact** de la classe tel qu'il apparaît dans le code source.

> 💡 **Premier réflexe face à un binaire C++ inconnu :** lancez `strings binaire | grep -oP '^\d+[A-Z]\w+'` pour extraire toutes les chaînes de typeinfo name. Vous obtiendrez immédiatement la liste de toutes les classes polymorphes du programme.

Pour les classes dans des namespaces, le format est celui du mangling imbriqué :

```
N5MyApp7Network6ClientE     →   MyApp::Network::Client
```

Le `N...E` encadrant indique un nom qualifié, exactement comme dans le mangling complet (voir section 17.1).

## Parcourir la RTTI dans Ghidra

### Avec les symboles

Si le binaire a ses symboles, la méthode la plus directe est de chercher les `_ZTI` dans le Symbol Tree :

1. Ouvrez le Symbol Tree, filtrez par `_ZTI`.  
2. Pour chaque `_ZTI`, naviguez à l'adresse correspondante dans le Listing.  
3. Identifiez le type de structure en regardant le vptr : s'il pointe vers `__class_type_info`, `__si_class_type_info` ou `__vmi_class_type_info`.  
4. Pour les `__si_class_type_info`, suivez le pointeur `__base_type` (offset 16) pour remonter dans la hiérarchie.  
5. Pour les `__vmi_class_type_info`, lisez `__base_count` (offset 20) et parcourez le tableau `__base_info`.

### Sans les symboles (binaire strippé)

Sur un binaire strippé, les symboles `_ZTI` et `_ZTS` ne sont pas directement visibles dans le Symbol Tree. Mais les données sont toujours présentes dans `.rodata`. Voici la procédure :

**Étape 1 — Trouver les chaînes typeinfo name.** Dans Ghidra, ouvrez la fenêtre Defined Strings (Window → Defined Strings) ou lancez une recherche de chaînes. Filtrez les résultats pour trouver les chaînes au format `<nombre><nom>` (par exemple `6Circle`, `5Shape`). Ces chaînes sont les `_ZTS`.

**Étape 2 — Remonter aux structures typeinfo.** Pour chaque chaîne `_ZTS` trouvée, faites un clic droit → References → Find References to. Le résultat vous donne l'adresse de la structure `_ZTI` qui référence cette chaîne (le champ `__name` à l'offset 8).

**Étape 3 — Identifier le type de typeinfo.** À l'adresse trouvée, lisez le QWORD à l'offset 0 (le vptr de la structure typeinfo). Ce pointeur pointe vers une vtable dans `libstdc++.so`. Vous devez identifier à quelle classe de typeinfo il appartient :

- Si le pointeur cible est dans le même binaire et porte un symbole contenant `class_type_info`, c'est un `__class_type_info` (racine).  
- Si le symbole contient `si_class_type_info`, c'est un héritage simple.  
- Si le symbole contient `vmi_class_type_info`, c'est un héritage multiple/virtuel.

En pratique, sur un binaire dynamiquement linké, ces vtables sont dans `libstdc++.so` et ne sont pas résolues directement dans le binaire. Vous verrez des entrées de relocation. L'astuce est de noter les trois adresses distinctes qui apparaissent comme vptr des différentes structures typeinfo, puis de les classer : la plus fréquente chez les classes dérivées simples est `__si_class_type_info`, celle des racines est `__class_type_info`, et celle des cas complexes est `__vmi_class_type_info`.

**Étape 4 — Lire les champs et suivre les pointeurs.** Selon le type identifié, lisez les champs comme décrit dans les structures ci-dessus. Pour chaque pointeur `__base_type`, suivez-le pour découvrir la classe parente.

**Étape 5 — Construire le graphe de hiérarchie.** En connectant tous les liens parent-enfant, vous obtenez l'arbre d'héritage complet.

### Script Ghidra pour automatiser l'extraction

L'extraction manuelle est fastidieuse quand le binaire contient des dizaines de classes. Voici l'algorithme d'un script Ghidra (Java ou Python) qui automatise le processus :

```
Pour chaque chaîne dans .rodata qui matche /^\d+[A-Z]\w*$/ :
    Trouver les références à cette chaîne → adresse_typeinfo + 8
    adresse_typeinfo = référence - 8
    Lire vptr = QWORD[adresse_typeinfo]
    Lire name_ptr = QWORD[adresse_typeinfo + 8]

    Si vptr correspond à __si_class_type_info :
        parent_ti = QWORD[adresse_typeinfo + 16]
        Enregistrer lien : classe → parent

    Si vptr correspond à __vmi_class_type_info :
        base_count = DWORD[adresse_typeinfo + 20]
        Pour i = 0 à base_count - 1 :
            base_ti = QWORD[adresse_typeinfo + 24 + i*16]
            offset_flags = QWORD[adresse_typeinfo + 24 + i*16 + 8]
            offset = offset_flags >> 8
            is_virtual = offset_flags & 1
            is_public = (offset_flags >> 1) & 1
            Enregistrer lien : classe → parent_i (offset, virtuel, public)

Produire le graphe de hiérarchie
```

Ce script peut être étendu pour croiser les résultats avec les vtables et produire une reconstruction complète de la hiérarchie, incluant les noms des méthodes virtuelles.

## `typeid` en assembleur

L'opérateur `typeid` appliqué à un objet polymorphe (via une référence ou un pointeur déréférencé) lit la RTTI à travers le vptr. Voici ce que GCC génère :

```cpp
const std::type_info& ti = typeid(*shape_ptr);
```

```nasm
; rdi = shape_ptr (Shape*)
mov    rax, QWORD PTR [rdi]          ; rax = vptr  
mov    rax, QWORD PTR [rax-8]        ; rax = typeinfo ptr (offset -8 dans la vtable)  
; rax pointe maintenant vers la structure _ZTI de la classe réelle
```

Le pattern est simple : charger le vptr, puis lire le QWORD à l'offset `-8` (un cran avant les slots de méthodes virtuelles). C'est le champ typeinfo pointer de la vtable, décrit en section 17.2.

Pour `typeid` sur un type statique (pas via un pointeur polymorphe), GCC émet directement une référence à la structure `_ZTI` sans passer par le vptr :

```cpp
const std::type_info& ti = typeid(Circle);
```

```nasm
lea    rax, [rip+_ZTI6Circle]        ; adresse directe de la typeinfo
```

L'appel à `typeid(*ptr).name()` appelle ensuite la méthode `name()` de `std::type_info`, qui retourne le pointeur `__name` de la structure typeinfo — la chaîne `_ZTS`.

> 💡 **En RE :** si vous voyez un accès à `[vptr-8]` suivi d'opérations sur le résultat, c'est un accès à la RTTI via `typeid`. Cela indique que le code source utilise `typeid` ou une fonctionnalité qui en dépend (certaines implémentations de logging, sérialisation dynamique, etc.).

## `dynamic_cast` en assembleur

`dynamic_cast` est le cast sûr du C++ : il vérifie à l'exécution si la conversion est valide en consultant la RTTI. Si la conversion échoue, il retourne `nullptr` (pour les pointeurs) ou lance une exception `std::bad_cast` (pour les références).

### Le cas pointeur

```cpp
Circle* c = dynamic_cast<Circle*>(shape_ptr);  
if (c) {  
    // utiliser c
}
```

GCC traduit ce `dynamic_cast` en un appel à la fonction runtime `__dynamic_cast` définie dans `libstdc++` :

```nasm
; shape_ptr est dans rdi (déjà le premier argument)
; Préparer les arguments de __dynamic_cast
mov    rdi, rbx                       ; arg1 : pointeur source (shape_ptr)  
lea    rsi, [rip+_ZTI5Shape]          ; arg2 : typeinfo de la classe source  
lea    rdx, [rip+_ZTI6Circle]         ; arg3 : typeinfo de la classe cible  
mov    ecx, 0                         ; arg4 : hint (0 = pas d'info supplémentaire)  
call   __dynamic_cast  

; rax = résultat : pointeur casté ou NULL
test   rax, rax                       ; vérifier si le cast a réussi  
jz     .cast_failed  
; ... utiliser rax comme Circle*
```

La signature de `__dynamic_cast` est :

```c
void* __dynamic_cast(
    const void* src_ptr,              // pointeur à convertir
    const __class_type_info* src_type, // typeinfo du type source
    const __class_type_info* dst_type, // typeinfo du type cible
    ptrdiff_t src2dst_offset           // hint d'offset (-1 = inconnu, 0 = pas d'info)
);
```

> 💡 **Pattern RE clé :** un `call __dynamic_cast` (ou `call __dynamic_cast@plt`) suivi d'un `test rax, rax` et d'un `jz` est la signature d'un `dynamic_cast<T*>()`. Les deux arguments `lea ... _ZTI...` qui précèdent l'appel vous donnent directement le type source et le type cible du cast, en clair (via les chaînes `_ZTS` associées). C'est une information de très haute valeur en RE — vous savez exactement quels types le code source manipulait.

### Le cas référence

Pour un `dynamic_cast` sur une référence, l'échec ne retourne pas `nullptr` mais lance `std::bad_cast`. GCC génère un code légèrement différent :

```cpp
Circle& c = dynamic_cast<Circle&>(shape_ref);
```

```nasm
; Même appel à __dynamic_cast
call   __dynamic_cast  
test   rax, rax  
jnz    .cast_ok  

; Échec → lancer std::bad_cast
call   __cxa_allocate_exception       ; allouer l'objet exception
; ... initialiser bad_cast ...
call   __cxa_throw                    ; lancer l'exception

.cast_ok:
; utiliser rax comme Circle&
```

La différence est que le chemin d'échec se termine par un `__cxa_throw` au lieu d'un simple saut. En RE, si vous voyez `__dynamic_cast` suivi d'une branche qui appelle `__cxa_throw` avec un type `bad_cast`, c'est un `dynamic_cast` sur référence.

### Optimisations de `dynamic_cast`

GCC applique des optimisations importantes sur `dynamic_cast` :

**Downcast dans une hiérarchie simple sans héritage virtuel.** Si le compilateur sait que la hiérarchie ne contient pas d'héritage virtuel et que le cast est un downcast (de base vers dérivée), il peut remplacer `__dynamic_cast` par une simple comparaison de vptr :

```nasm
; dynamic_cast<Circle*>(shape_ptr) optimisé
mov    rax, QWORD PTR [rdi]            ; charger le vptr  
lea    rdx, [rip+_ZTV6Circle+16]       ; vtable attendue de Circle  
cmp    rax, rdx  
jne    .not_circle  
; rdi est un Circle*, l'utiliser directement
mov    rax, rdi  
jmp    .done  
.not_circle:
xor    eax, eax                        ; return nullptr
.done:
```

Ce pattern est plus rapide que l'appel à `__dynamic_cast` et apparaît fréquemment en `-O2`. En RE, il ressemble à un test de type « fait main » — la comparaison du vptr contre une vtable connue est le signe d'un `dynamic_cast` optimisé ou d'une vérification de type explicite.

**Cast vers une base.** Un `dynamic_cast` vers une classe de base (upcast) est toujours valide. GCC le remplace par un simple ajustement de pointeur (identique à un `static_cast`), voire par rien du tout si la base est primaire.

**Cast trivial.** Si le compilateur peut prouver que le type de l'objet est déjà le type cible, le `dynamic_cast` est éliminé entièrement.

## `static_cast` vs `dynamic_cast` en assembleur

Il est important de distinguer les deux types de cast dans le désassemblage :

| Cast | Code généré | Vérification à l'exécution |  
|------|-------------|---------------------------|  
| `static_cast<Derived*>(base_ptr)` | Simple ajustement de pointeur (add/sub) ou rien | **Aucune** — si le type est faux, comportement indéfini |  
| `dynamic_cast<Derived*>(base_ptr)` | Appel à `__dynamic_cast` ou comparaison de vptr | **Oui** — retourne nullptr ou lance bad_cast si invalide |  
| `reinterpret_cast<T*>(ptr)` | Rien du tout (même adresse) | **Aucune** |

En RE, quand vous voyez un pointeur utilisé directement sans vérification ni appel à `__dynamic_cast`, le code source utilisait probablement un `static_cast` ou un `reinterpret_cast`. Quand vous voyez `__dynamic_cast` ou une comparaison de vptr suivie d'un test nul, c'est un `dynamic_cast`.

## Reconstruire la hiérarchie complète depuis la RTTI : exemple

Mettons en pratique tout ce qui précède sur le binaire `oop_O2_strip` (optimisé et strippé). On ne dispose d'aucun symbole local, mais les structures RTTI sont présentes.

**Étape 1 — Extraire les noms de classes :**

```bash
$ strings oop_O2_strip | grep -oP '^\d+[A-Z]\w+'
5Shape
6Circle
9Rectangle
8Triangle
8Drawable
12Serializable
6Canvas
6Config
12AppException
10ParseError
12NetworkError
```

On a 11 classes polymorphes (ou plus exactement 11 classes dont la RTTI est présente).

**Étape 2 — Localiser les structures typeinfo dans `.rodata` :**

Pour chaque chaîne, on cherche ses références. Par exemple, pour `6Circle`, on cherche l'adresse de la chaîne, puis l'adresse de la structure qui y pointe (8 octets avant le champ `__name`).

**Étape 3 — Classifier chaque typeinfo :**

En examinant le vptr de chaque structure typeinfo, on détermine le type :

| Classe | Type de typeinfo | Raison |  
|--------|-----------------|--------|  
| `Shape` | `__class_type_info` | Racine de la hiérarchie Shape |  
| `Circle` | `__si_class_type_info` | Hérite de Shape uniquement |  
| `Rectangle` | `__si_class_type_info` | Hérite de Shape uniquement |  
| `Triangle` | `__si_class_type_info` | Hérite de Shape uniquement |  
| `Drawable` | `__class_type_info` | Racine de la hiérarchie Drawable |  
| `Serializable` | `__class_type_info` | Racine de la hiérarchie Serializable |  
| `Canvas` | `__vmi_class_type_info` | Hérite de Drawable ET Serializable |  
| `AppException` | `__si_class_type_info` | Hérite de std::exception |  
| `ParseError` | `__si_class_type_info` | Hérite de AppException |  
| `NetworkError` | `__si_class_type_info` | Hérite de AppException |  
| `Config` | (pas de typeinfo polymorphe) | Pas de méthode virtuelle |

> Note : `Config` dans notre binaire n'a pas de méthode virtuelle, donc pas de vtable ni de typeinfo. Il n'apparaîtra pas dans les structures RTTI. Les 10 autres classes sont polymorphes.

**Étape 4 — Suivre les pointeurs `__base_type` :**

| Classe | `__base_type` pointe vers | Relation |  
|--------|---------------------------|----------|  
| `Circle` | `_ZTI5Shape` | Circle → Shape |  
| `Rectangle` | `_ZTI5Shape` | Rectangle → Shape |  
| `Triangle` | `_ZTI5Shape` | Triangle → Shape |  
| `ParseError` | `_ZTI12AppException` | ParseError → AppException |  
| `NetworkError` | `_ZTI12AppException` | NetworkError → AppException |  
| `AppException` | `_ZTISt9exception` | AppException → std::exception |

Pour `Canvas` (type `__vmi_class_type_info`) :

| Base | `__base_type` | offset_flags | Offset | Public | Virtuel |  
|------|---------------|-------------|--------|--------|---------|  
| #0 | `_ZTI8Drawable` | `0x002` | 0 | oui | non |  
| #1 | `_ZTI12Serializable` | `0x802` | 8 | oui | non |

**Étape 5 — Dessiner la hiérarchie :**

```
std::exception
    └── AppException
            ├── ParseError
            └── NetworkError

Shape (abstraite)
    ├── Circle
    ├── Rectangle
    └── Triangle

Drawable                Serializable
    └───────┬───────────────┘
          Canvas
```

Toute cette information a été extraite d'un binaire **strippé** uniquement grâce aux structures RTTI.

## RTTI et exceptions : le lien caché

La RTTI n'est pas seulement utilisée par `typeid` et `dynamic_cast`. Elle est également essentielle au mécanisme d'exceptions C++. Quand une exception est lancée avec `throw`, le runtime doit déterminer quel `catch` correspond au type de l'exception — ce qui nécessite une comparaison de types à l'exécution, exactement comme `dynamic_cast`.

C'est pourquoi même un programme compilé avec `-fno-rtti` **conserve les structures typeinfo des classes utilisées comme exceptions**. Le compilateur ne peut pas les supprimer sans casser le mécanisme d'exceptions. En pratique, cela signifie que :

- Un binaire `-fno-rtti` qui utilise des exceptions aura les typeinfo des classes d'exception, mais pas celles des autres classes.  
- Un binaire `-fno-rtti -fno-exceptions` n'aura aucune typeinfo.  
- Un binaire par défaut (sans flags spéciaux) aura les typeinfo de toutes les classes polymorphes ET de toutes les classes d'exception (même non polymorphes si elles sont lancées).

> 💡 **En RE :** si un binaire semble compilé avec `-fno-rtti` (les vtables ont un `0` à l'offset -8 au lieu d'un pointeur typeinfo) mais que vous trouvez quand même des structures typeinfo pour certaines classes, ce sont les classes d'exception. Cela vous donne au minimum la hiérarchie des exceptions du programme.

## Détecter la présence ou l'absence de RTTI

Voici une procédure rapide pour déterminer si un binaire contient de la RTTI :

```bash
# 1. Chercher les symboles typeinfo (avec symboles)
$ nm -C binaire | grep 'typeinfo for'

# 2. Chercher les chaînes typeinfo name (même strippé)
$ strings binaire | grep -cP '^\d+[A-Z]'

# 3. Vérifier le champ typeinfo dans une vtable connue
#    (si le QWORD à l'offset -8 du début des slots est 0 → pas de RTTI)
$ objdump -s -j .rodata binaire | less
```

Si la commande 2 retourne des résultats significatifs, la RTTI est présente. Si elle retourne 0 ou très peu de résultats (uniquement des chaînes pour les exceptions standard), la RTTI est probablement désactivée.

## Résumé des patterns à reconnaître

| Pattern | Signification |  
|---------|---------------|  
| `mov rax, [vptr]; mov rax, [rax-8]` | Accès à la typeinfo via le vptr (`typeid`) |  
| `lea rsi, [_ZTI...]; lea rdx, [_ZTI...]; call __dynamic_cast` | `dynamic_cast` avec type source et type cible identifiables |  
| `call __dynamic_cast; test rax, rax; jz ...` | `dynamic_cast<T*>()` — branche sur échec = nullptr |  
| `call __dynamic_cast; test rax, rax; jnz .ok; call __cxa_throw` | `dynamic_cast<T&>()` — lance bad_cast sur échec |  
| `cmp [rdi], vtable_addr; jne ...` | `dynamic_cast` optimisé (comparaison directe de vptr) |  
| Chaîne `<nombre><NomClasse>` dans `.rodata` | Typeinfo name (`_ZTS`) — nom de classe en clair |  
| QWORD pointant vers `__si_class_type_info` vtable, suivi d'un ptr chaîne, suivi d'un ptr `_ZTI` | Structure typeinfo d'héritage simple |  
| QWORD pointant vers `__vmi_class_type_info` vtable, suivi d'un ptr chaîne, flags, count, tableau de bases | Structure typeinfo d'héritage multiple |  
| `__cxa_pure_virtual` dans une vtable + typeinfo avec `__class_type_info` | Classe abstraite racine |

---


⏭️ [Gestion des exceptions (`.eh_frame`, `.gcc_except_table`, `__cxa_throw`)](/17-re-cpp-gcc/04-gestion-exceptions.md)
