🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Reconstruire les classes du binaire `ch17-oop` à partir du désassemblage seul

> **Chapitre 17 — Reverse Engineering du C++ avec GCC**  
> **Partie IV — Techniques Avancées de RE**

---

## Objectif

Ce checkpoint valide l'ensemble des acquis du chapitre 17. L'objectif est de produire une **reconstruction complète de l'architecture orientée objet** du binaire `ch17-oop` à partir du désassemblage, sans consulter le code source `oop.cpp`.

Le livrable final est un document structuré contenant : la hiérarchie de classes, les layouts mémoire des objets, les vtables annotées, les prototypes des méthodes, et un diagramme de classes reconstruit. Ce type de livrable correspond à ce qu'un analyste RE produirait dans un contexte professionnel — audit de sécurité, analyse de compatibilité, ou documentation d'un composant tiers sans source.

## Binaire cible

Le checkpoint se réalise en deux passes sur deux variantes du binaire :

| Passe | Binaire | Objectif |  
|-------|---------|----------|  
| **Passe 1** (apprentissage) | `ch17-oop_O0` (`-O0 -g`) | Reconstruire l'architecture avec le confort des symboles et du code non optimisé. Apprendre les patterns. |  
| **Passe 2** (validation) | `ch17-oop_O2_strip` (`-O2 -s`) | Reconstruire la même architecture **sans symboles et avec optimisations**. C'est la passe qui valide réellement les compétences. |

La passe 1 sert de référence et de filet de sécurité. La passe 2 est le vrai défi. Comparez vos résultats des deux passes pour évaluer votre progression.

Compilez les binaires si ce n'est pas déjà fait :

```bash
cd binaries/ch17-oop/  
make all  
```

## Livrables attendus

Le document de reconstruction doit contenir les sections suivantes.

### 1. Inventaire des classes polymorphes

La liste complète des classes qui possèdent au moins une méthode virtuelle, avec pour chacune :

- Le nom de la classe.  
- Le type d'héritage (racine, simple, multiple).  
- La ou les classes parentes.  
- Le caractère abstrait ou concret (présence de `__cxa_pure_virtual` dans la vtable).

**Format attendu :** un tableau ou une liste organisée. Pour la passe 2, documentez la méthode utilisée pour retrouver chaque nom (chaînes `_ZTS`, symboles dynamiques, typeinfo).

### 2. Diagramme de hiérarchie de classes

Un diagramme montrant les relations d'héritage entre toutes les classes, incluant :

- Les flèches d'héritage (simple trait pour héritage simple, double trait ou annotation pour héritage multiple).  
- Les classes abstraites marquées distinctement (italique, annotation, ou convention de votre choix).  
- Les classes d'exception dans une branche séparée.

Le diagramme peut être en ASCII art, en Mermaid, en PlantUML, ou dessiné à la main — le format importe peu tant que l'information est correcte et lisible.

### 3. Vtables annotées

Pour **chaque classe polymorphe**, la vtable complète avec :

- L'adresse de la vtable dans `.rodata`.  
- L'offset-to-top et le pointeur typeinfo.  
- Chaque slot numéroté avec : l'adresse de la fonction pointée, le nom de la méthode (démanglé ou reconstruit), et une indication si c'est une méthode propre, héritée, ou un override.

Pour les classes à héritage multiple, documenter les deux parties de la vtable composite et identifier les thunks.

**Exemple de format :**

```
vtable for Circle @ 0x403d00 :
  [-16] offset-to-top = 0
  [-8]  typeinfo      = 0x403f20 → _ZTI6Circle
  [0]   slot 0 : 0x401c10 → Circle::~Circle() [D1]    (override)
  [8]   slot 1 : 0x401c40 → Circle::~Circle() [D0]    (override)
  [16]  slot 2 : 0x401b14 → Circle::area() const       (override)
  [24]  slot 3 : 0x401b38 → Circle::perimeter() const  (override)
  [32]  slot 4 : 0x401b64 → Circle::describe() const   (override)
```

### 4. Layout mémoire des objets

Pour **chaque classe concrète** (instanciable), le layout mémoire de l'objet avec :

- Le `sizeof` total de l'objet.  
- Le vptr (ou les vptrs pour l'héritage multiple) avec leur offset.  
- Chaque membre avec son offset, sa taille, et son type reconstruit.

Les membres sont déduits de l'analyse des constructeurs (qui initialisent chaque champ) et des méthodes (qui lisent/écrivent les champs).

**Exemple de format :**

```
Circle (sizeof = 64) :
  offset 0   : vptr (8 bytes) → vtable for Circle
  offset 8   : name_ (std::string, 32 bytes)
  offset 40  : x_ (double, 8 bytes)
  offset 48  : y_ (double, 8 bytes)
  offset 56  : radius_ (double, 8 bytes)
```

### 5. Prototypes des méthodes reconstruits

Pour chaque classe, la liste des méthodes identifiées avec :

- Le prototype reconstruit (type de retour, nom, paramètres).  
- Le caractère virtuel ou non.  
- La convention d'appel (paramètres dans quels registres).

Pour la passe 2, les noms de méthodes seront des noms descriptifs que vous attribuez en fonction de la logique observée (par exemple `compute_area` au lieu de `area` si le nom exact est inconnu).

### 6. Identification des conteneurs STL

Lister chaque utilisation de conteneur STL identifiée dans les classes et fonctions, avec :

- Le type de conteneur (`std::vector`, `std::map`, `std::string`, etc.).  
- Le type des éléments (reconstruit).  
- La méthode d'identification utilisée (sizeof, pattern d'accès, symboles PLT).

### 7. Identification des mécanismes C++ utilisés

Pour chaque mécanisme du chapitre 17 que vous identifiez dans le binaire, une brève note documentant :

- **Name mangling** : exemples de symboles démanglés et ce qu'ils révèlent.  
- **Vtables/vptr** : comment vous avez identifié le dispatch virtuel.  
- **RTTI** : les structures typeinfo trouvées et la hiérarchie qu'elles révèlent.  
- **Exceptions** : les blocs `try`/`catch` identifiés, les types d'exceptions interceptés.  
- **Templates** : les instanciations trouvées, les types de paramètres.  
- **Lambdas** : les closures identifiées, leurs captures.  
- **Smart pointers** : les patterns `shared_ptr` (opérations atomiques) et `unique_ptr` identifiés.

## Méthodologie recommandée

La reconstruction suit un processus progressif. Chaque étape produit des informations qui alimentent les suivantes.

### Phase A — Reconnaissance initiale

Commencez par un triage du binaire avec les outils de base pour établir le périmètre :

- `file` pour confirmer le type (ELF 64-bit, dynamiquement linké).  
- `checksec` pour les protections.  
- `strings` avec filtrage des chaînes typeinfo (`grep -oP '^\d+[A-Z]\w+'`) pour obtenir immédiatement la liste des classes.  
- `nm -C` (passe 1) ou `nm -D -C` (passe 2) pour les symboles disponibles.  
- `readelf -S` pour identifier les sections présentes (`.rodata` pour les vtables, `.gcc_except_table` pour les exceptions).

### Phase B — Reconstruction de la hiérarchie via la RTTI

Localisez les structures typeinfo dans `.rodata` (section 17.3). Pour chaque typeinfo :

- Identifiez le type (`__class_type_info`, `__si_class_type_info`, `__vmi_class_type_info`).  
- Suivez les pointeurs `__base_type` pour reconstruire les liens d'héritage.  
- Pour les `__vmi_class_type_info`, lisez le tableau `__base_info` pour identifier les bases multiples et leurs offsets.

Produisez le diagramme de hiérarchie à la fin de cette phase.

### Phase C — Analyse des vtables

Pour chaque vtable identifiée via les symboles `_ZTV` (passe 1) ou via les pointeurs typeinfo (passe 2) :

- Listez les slots et résolvez les adresses vers les fonctions correspondantes.  
- Identifiez les méthodes virtuelles pures (`__cxa_pure_virtual`).  
- Comparez les vtables des classes dérivées avec celles des classes parentes pour identifier les overrides.  
- Pour les vtables composites (héritage multiple), séparez les parties et identifiez les thunks.

### Phase D — Analyse des constructeurs et layout mémoire

Localisez les constructeurs de chaque classe (section 17.2). Ils se reconnaissent par :

- L'appel au constructeur parent avec le même `this`.  
- L'écriture du vptr à l'offset 0.  
- L'initialisation des membres à des offsets fixes.

Pour chaque constructeur, notez chaque offset accédé et le type de donnée écrite. Croisez avec les méthodes de la classe (qui lisent les mêmes offsets) pour confirmer le layout.

### Phase E — Analyse des méthodes et de la logique

Analysez le contenu de chaque méthode virtuelle (identifiée via les vtables) et des méthodes non-virtuelles (identifiées via les XREF et le code de `main`). Reconstruisez les prototypes, identifiez les conteneurs STL utilisés, les patterns de lambdas, les opérations sur les smart pointers, et les blocs try/catch.

### Phase F — Vérification croisée

Vérifiez la cohérence de votre reconstruction :

- Chaque classe dans la hiérarchie RTTI doit avoir une vtable correspondante.  
- Les offsets des membres dans le layout doivent être cohérents entre le constructeur et toutes les méthodes.  
- Les vtables des classes dérivées doivent avoir au moins autant de slots que celles des parents.  
- Le `sizeof` de chaque classe doit être cohérent avec les allocations observées dans le code.

## Outils suggérés

| Outil | Usage dans ce checkpoint |  
|-------|--------------------------|  
| `nm -C` / `nm -D -C` | Lister et démangler les symboles |  
| `objdump -d -C -M intel` | Désassembler avec noms démanglés |  
| `strings` | Extraire les chaînes typeinfo name |  
| `readelf -S` / `readelf --debug-dump=frames` | Inspecter les sections, les FDE |  
| `c++filt` | Démangler des symboles individuels |  
| Ghidra | Analyse principale : décompileur, XREF, navigation vtables |  
| GDB (optionnel) | Confirmer des layouts en dynamique (`print sizeof(Circle)`, etc.) |

## Critères de validation

Le checkpoint est considéré réussi quand les conditions suivantes sont remplies :

**Niveau 1 — Fondamentaux (passe 1 avec `oop_O0`) :**

- [ ] Toutes les classes polymorphes sont identifiées et nommées.  
- [ ] La hiérarchie d'héritage est correcte (parents, type d'héritage).  
- [ ] Au moins 3 vtables sont entièrement annotées (tous les slots identifiés).  
- [ ] Le layout mémoire d'au moins 3 classes concrètes est reconstruit avec les bons offsets.  
- [ ] L'héritage multiple de `Canvas` est correctement documenté (deux vptr, thunks).  
- [ ] Au moins un bloc `try`/`catch` est identifié avec les types d'exceptions interceptés.

**Niveau 2 — Intermédiaire (passe 1 complète) :**

- [ ] Toutes les vtables sont annotées, y compris la vtable composite de `Canvas`.  
- [ ] Les layouts mémoire de toutes les classes concrètes sont reconstruits.  
- [ ] Les instanciations de templates (`Registry<K,V>`) sont identifiées avec leurs paramètres.  
- [ ] Au moins 2 conteneurs STL sont identifiés avec leur type d'élément.  
- [ ] Au moins 1 lambda est identifiée avec ses captures.  
- [ ] Les patterns `shared_ptr` (opérations atomiques) sont repérés.

**Niveau 3 — Avancé (passe 2 avec `oop_O2_strip`) :**

- [ ] La hiérarchie de classes est reconstruite sans symboles locaux, uniquement via la RTTI et les symboles dynamiques.  
- [ ] Au moins 5 vtables sont annotées sur le binaire strippé.  
- [ ] Les layouts mémoire d'au moins 3 classes sont reconstruits sur le binaire optimisé (les offsets peuvent différer légèrement de `-O0` à cause de l'alignement).  
- [ ] Les effets des optimisations sont documentés : dévirtualisation, inlining de méthodes, élimination de code.  
- [ ] Le document final pourrait servir de base pour écrire un header `.h` permettant d'interagir avec le binaire (ce qui sera l'objet du checkpoint du chapitre 20).

## Conseils pour réussir

**Commencez par la RTTI, pas par le code.** La RTTI vous donne la carte du territoire (quelles classes existent, comment elles sont liées) avant que vous n'exploriez le terrain (le code des méthodes). Sans cette carte, vous risquez de vous perdre dans les détails.

**Travaillez classe par classe.** Ne tentez pas de tout reconstruire en une passe. Prenez une classe (commencez par la plus simple, par exemple `Circle`), reconstruisez-la entièrement (vtable, layout, méthodes), puis passez à la suivante. Chaque classe reconstruite facilite les suivantes car les patterns se répètent.

**Utilisez les constructeurs comme source de vérité.** Les constructeurs initialisent tous les champs dans l'ordre et écrivent le vptr. Ils sont la meilleure source pour le layout mémoire. Les destructeurs confirment en miroir (destruction dans l'ordre inverse).

**Prenez des notes au fur et à mesure.** Chaque offset identifié, chaque fonction nommée, chaque relation d'héritage découverte doit être notée immédiatement. La reconstruction est un puzzle — chaque pièce compte et peut servir plus tard.

**Ne restez pas bloqué sur les internals STL.** Si vous identifiez un `std::vector<shared_ptr<Shape>>`, notez-le et passez à autre chose. Vous n'avez pas besoin de comprendre le code interne de `_M_realloc_insert` pour réussir ce checkpoint. Concentrez-vous sur l'architecture des classes applicatives.

**Pour la passe 2, appuyez-vous sur la passe 1.** Vous savez déjà ce que vous cherchez. La question n'est plus « quelles classes existent ? » mais « comment les retrouver sans symboles ? ». C'est un exercice de méthodologie, pas de découverte.

---

> 📋 **Corrigé disponible :** [`solutions/ch17-checkpoint-solution.md`](/solutions/ch17-checkpoint-solution.md)

---


⏭️ [Chapitre 18 — Exécution symbolique et solveurs de contraintes](/18-execution-symbolique/README.md)
