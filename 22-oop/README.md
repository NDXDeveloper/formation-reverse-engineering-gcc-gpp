🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 22 — Reverse d'une application C++ orientée objet

> 📦 **Binaire d'entraînement** : `binaries/ch22-oop/`  
> 🛠️ **Outils principaux** : Ghidra, GDB (+ GEF/pwndbg), `objdump`, `c++filt`, `nm`, `readelf`, Frida, `LD_PRELOAD`  
> 📚 **Prérequis** : Chapitres 3 (assembleur x86-64), 8 (Ghidra), 11 (GDB), 17 (RE du C++ avec GCC)

---

## Pourquoi ce chapitre ?

Les chapitres précédents vous ont donné les briques théoriques : le name mangling Itanium, le modèle objet C++ (vtables, vptr, RTTI), la gestion des exceptions et les internals de la STL. Le chapitre 17 en particulier vous a appris à **reconnaître** ces structures dans un désassemblage. Il est temps de passer à l'échelle.

Dans ce chapitre, vous allez affronter un binaire C++ complet — pas un snippet isolé, mais une application avec une hiérarchie de classes, de l'héritage, du polymorphisme, un système de plugins chargés dynamiquement, et des appels virtuels répartis sur plusieurs modules. L'objectif n'est plus de reconnaître une vtable dans un listing de 30 lignes : c'est de **reconstruire l'architecture logicielle** d'un programme dont vous n'avez pas les sources.

Ce type de cible est représentatif de ce que vous rencontrerez dans la réalité : applications métier compilées en C++, moteurs de jeux, frameworks avec architecture à plugins, ou encore malwares orientés objet. Savoir naviguer dans le dispatch virtuel et comprendre comment les objets interagissent à travers des interfaces abstraites est une compétence fondamentale en RE appliqué.

---

## Le binaire `ch22-oop`

Le binaire fourni dans `binaries/ch22-oop/` est une application C++ simulant un **système de traitement de données modulaire**. Son architecture repose sur plusieurs concepts que vous devrez identifier et reconstruire :

- **Une classe de base abstraite** définissant une interface commune (méthodes virtuelles pures).  
- **Plusieurs classes dérivées** implémentant cette interface avec des comportements distincts.  
- **Un mécanisme de plugins** : des bibliothèques partagées (`.so`) sont chargées à l'exécution via `dlopen` / `dlsym`, instanciées dynamiquement, et utilisées à travers l'interface de base.  
- **Du dispatch virtuel** omniprésent : l'application manipule des pointeurs vers la classe de base et appelle les méthodes via la vtable, sans jamais connaître le type concret au site d'appel.

Le `Makefile` du chapitre produit plusieurs variantes :

| Variante | Optimisation | Symboles | Usage pédagogique |  
|---|---|---|---|  
| `oop_O0` | `-O0` | oui (`-g`) | Première analyse, correspondance directe avec le source |  
| `oop_O2` | `-O2` | oui (`-g`) | Observer l'impact des optimisations sur le dispatch virtuel |  
| `oop_O2_strip` | `-O2` | non (`-s`) | Conditions réalistes — pas de symboles, pas de filet |  
| `plugin_alpha.so` | `-O2` | oui | Plugin chargé dynamiquement |  
| `plugin_beta.so` | `-O2` | oui | Second plugin, comportement différent |

> 💡 Commencez systématiquement par la variante `_O0` avec symboles. Une fois la logique comprise, passez à `_O2_strip` pour valider que vous savez retrouver les mêmes informations sans aide du compilateur.

---

## Ce que vous allez apprendre

Ce chapitre couvre quatre axes complémentaires, chacun correspondant à une section :

**22.1 — Reconstruction de la hiérarchie de classes et des vtables.** Vous partirez du binaire brut pour identifier les classes, leurs relations d'héritage, et reconstruire les vtables dans Ghidra. Vous apprendrez à remonter d'un appel `call [rax+0x10]` jusqu'à la méthode concrète appelée, en croisant les informations RTTI, les cross-references et la structure mémoire des objets.

**22.2 — RE d'un système de plugins (`dlopen` / `dlsym`).** Vous analyserez comment l'application découvre, charge et instancie des modules externes à l'exécution. Vous verrez comment tracer les appels à `dlopen` et `dlsym` (avec `ltrace`, GDB ou Frida) pour comprendre quel symbole sert de point d'entrée au plugin et comment l'objet retourné s'intègre dans la hiérarchie de classes du programme principal.

**22.3 — Comprendre le dispatch virtuel en pratique.** Vous plongerez dans la mécanique d'un appel de méthode virtuelle au niveau assembleur : lecture du vptr, indexation dans la vtable, appel indirect. Vous apprendrez à distinguer un appel virtuel d'un appel direct, à identifier la dévirtualisation opérée par le compilateur en `-O2`, et à reconnaître les cas où GCC remplace un appel indirect par un appel direct lorsqu'il peut résoudre le type statiquement.

**22.4 — Patcher un comportement via `LD_PRELOAD`.** Plutôt que de modifier le binaire lui-même, vous utiliserez `LD_PRELOAD` pour injecter une bibliothèque partagée qui **intercepte et remplace** des fonctions ou des méthodes à l'exécution. Cette technique, à la frontière entre analyse dynamique et instrumentation, est un outil puissant pour tester des hypothèses de RE rapidement, sans toucher au binaire cible.

---

## Méthodologie recommandée

L'analyse d'un binaire C++ orienté objet suit un flux spécifique qui diffère sensiblement du RE d'un programme C procédural. Voici l'approche que nous suivrons tout au long du chapitre :

```
1. Triage classique (file, strings, checksec, readelf)
       │
       ▼
2. Identifier les symboles C++ (nm -C, c++filt, RTTI strings)
       │
       ▼
3. Localiser les vtables (.rodata) et les structures RTTI
       │
       ▼
4. Reconstruire la hiérarchie de classes (Ghidra + XREF)
       │
       ▼
5. Tracer le dispatch virtuel (GDB : break sur call indirect)
       │
       ▼
6. Analyser le chargement dynamique (dlopen/dlsym → plugins .so)
       │
       ▼
7. Valider et expérimenter (LD_PRELOAD, Frida, plugin custom)
```

L'étape 3 est souvent la clé de voûte : une fois les vtables identifiées et annotées, la structure du programme se révèle d'elle-même. Chaque vtable correspond à une classe concrète, chaque entrée de la vtable est une méthode virtuelle, et les cross-references sur ces méthodes vous montrent **qui appelle quoi et dans quel contexte**.

---

## Rappels essentiels du chapitre 17

Avant de plonger dans l'analyse, assurez-vous d'être à l'aise avec ces concepts vus au chapitre 17. Si l'un d'eux vous semble flou, relisez la section correspondante avant de continuer.

**Le vptr et la vtable.** Chaque objet d'une classe polymorphe contient un pointeur caché (le *vptr*) situé au début de l'objet en mémoire (offset `+0x00` avec GCC/Itanium ABI). Ce pointeur référence la vtable de la classe concrète de l'objet, stockée dans `.rodata`. La vtable est un tableau de pointeurs de fonctions, un par méthode virtuelle, dans l'ordre de déclaration.

**Le name mangling Itanium.** Les symboles C++ sont encodés selon l'ABI Itanium. Par exemple, `_ZN7Vehicle5driveEv` se décode en `Vehicle::drive()`. L'outil `c++filt` et l'option `-C` de `nm` sont vos alliés permanents.

**La RTTI.** Lorsqu'elle n'est pas désactivée (`-fno-rtti`), la RTTI produit des structures `typeinfo` et des chaînes de noms de types lisibles dans `.rodata`. Ces chaînes (comme `7Vehicle`, `3Car`) sont souvent le premier indice visible dans un `strings` pour identifier les classes présentes.

**Le dispatch virtuel en assembleur.** Un appel virtuel typique (GCC, x86-64) se traduit par une séquence reconnaissable :

```asm
mov    rax, QWORD PTR [rdi]         ; rdi = this → lecture du vptr  
call   QWORD PTR [rax+0x10]         ; appel de la 3e entrée de la vtable  
```

L'offset dans `[rax+offset]` vous indique quelle méthode virtuelle est appelée (offset / 8 = index dans la vtable sur x86-64).

---

## Liens avec les autres chapitres

| Chapitre | Lien avec le chapitre 22 |  
|---|---|  
| **Ch. 17 — RE du C++ avec GCC** | Fondations théoriques directes : vtables, RTTI, name mangling, STL |  
| **Ch. 8 — Ghidra** | Outil principal pour la reconstruction statique des classes |  
| **Ch. 11 — GDB** | Tracer le dispatch virtuel et inspecter les objets en mémoire |  
| **Ch. 13 — Frida** | Hooker `dlopen`/`dlsym` et les méthodes virtuelles à la volée |  
| **Ch. 5 — Outils d'inspection** | Triage initial (`nm -C`, `readelf`, `strings`, `ltrace`) |  
| **Ch. 19 — Anti-reversing** | Comprendre l'impact du stripping sur l'analyse C++ |

---

## Sommaire du chapitre

- **22.1** — [Reconstruction de la hiérarchie de classes et des vtables](/22-oop/01-reconstruction-classes-vtables.md)  
- **22.2** — [RE d'un système de plugins (chargement dynamique `.so` via `dlopen`/`dlsym`)](/22-oop/02-systeme-plugins-dlopen.md)  
- **22.3** — [Comprendre le dispatch virtuel : de la vtable à l'appel de méthode](/22-oop/03-dispatch-virtuel.md)  
- **22.4** — [Patcher un comportement via `LD_PRELOAD`](/22-oop/04-patcher-ld-preload.md)  
- **🎯 Checkpoint** — [Écrire un plugin `.so` compatible qui s'intègre dans l'application sans les sources](/22-oop/checkpoint.md)

---


⏭️ [Reconstruction de la hiérarchie de classes et des vtables](/22-oop/01-reconstruction-classes-vtables.md)
