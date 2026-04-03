🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 22.3 — Comprendre le dispatch virtuel : de la vtable à l'appel de méthode

> 🛠️ **Outils utilisés** : Ghidra, GDB (+ GEF/pwndbg), `objdump -M intel`, Frida  
> 📦 **Binaires** : `oop_O0`, `oop_O2`, `oop_O2_strip`  
> 📚 **Prérequis** : Section 22.1 (vtables et hiérarchie), Section 22.2 (plugins), Chapitre 17.2 (modèle objet vtable)

---

## Introduction

Dans les sections précédentes, vous avez reconstruit la hiérarchie de classes et compris comment les plugins sont chargés. Mais il reste une question centrale : quand l'application écrit `processor->process(input, len, output, cap)`, que se passe-t-il **réellement** au niveau du processeur ?

Le code source ne contient aucun `if` pour déterminer si `processor` est un `UpperCaseProcessor`, un `Rot13Processor` ou un `XorCipherProcessor`. Le compilateur ne le sait pas non plus — le type concret n'est connu qu'à l'exécution. Pourtant, la bonne méthode est appelée à chaque fois. Ce mécanisme, le **dispatch virtuel**, est le cœur du polymorphisme C++, et c'est aussi l'un des patterns les plus fréquents en reverse engineering de binaires C++.

Cette section décortique ce mécanisme sous tous les angles : l'assembleur brut, la vue Ghidra, le traçage en dynamique, et les optimisations du compilateur qui peuvent le transformer — voire le supprimer.

---

## Anatomie d'un appel virtuel en assembleur

### Le pattern canonique

Prenons l'appel `processor->process(input, in_len, output, out_cap)` dans la méthode `Pipeline::execute()`. En `-O0`, GCC produit un assembleur lisible et prévisible.

Le pointeur `processor` est de type `Processor*`. L'appel passe par cinq étapes :

```asm
; ── Étape 1 : charger le pointeur this ──────────────────────────
; processor est stocké sur la pile ou dans un registre.
; Ici, supposons qu'il est dans une variable locale à [rbp-0x28].
mov    rdi, QWORD PTR [rbp-0x28]       ; rdi = processor (= this)

; ── Étape 2 : lire le vptr ──────────────────────────────────────
; Le vptr est le premier champ de l'objet, à l'offset +0x00.
mov    rax, QWORD PTR [rdi]            ; rax = *this = vptr

; ── Étape 3 : indexer la vtable ─────────────────────────────────
; process() est la 5e entrée de la vtable (index 4, après 2 dtors,
; name, configure). Chaque entrée fait 8 octets sur x86-64.
; Offset = 4 × 8 = 0x20.
; Mais avec les 2 destructeurs GCC : dtor_complete, dtor_deleting,
; name, configure → process est à l'index 4, offset 0x20.
mov    rax, QWORD PTR [rax+0x20]       ; rax = vtable[4] = &process()

; ── Étape 4 : préparer les arguments ────────────────────────────
; Convention System V AMD64 : rdi=this, rsi=input, rdx=in_len,
; rcx=output, r8=out_cap
; rdi est déjà positionné (étape 1).
mov    rsi, QWORD PTR [rbp-0x30]       ; rsi = input  
mov    rdx, QWORD PTR [rbp-0x38]       ; rdx = in_len  
mov    rcx, QWORD PTR [rbp-0x40]       ; rcx = output  
mov    r8,  QWORD PTR [rbp-0x48]       ; r8  = out_cap  

; ── Étape 5 : appel indirect ────────────────────────────────────
call   rax                              ; appel de la méthode virtuelle
```

Ce qui distingue un appel virtuel d'un appel direct est le **`call rax`** (ou `call QWORD PTR [rax+offset]` quand les étapes 3 et 5 sont fusionnées). Un appel direct utilise `call <adresse_fixe>` ou `call <symbole@plt>`.

> 💡 **Règle de lecture rapide** : si vous voyez `call` suivi d'un registre ou d'une indirection mémoire (pas une adresse immédiate), c'est un appel indirect. Dans un binaire C++, un appel indirect précédé d'une lecture `[rdi]` est presque certainement un dispatch virtuel.

### Variantes syntaxiques

Le pattern ci-dessus est la forme décomposée en `-O0`. En pratique, GCC peut fusionner les étapes 2, 3 et 5 :

```asm
; Forme compacte (fréquente en -O1 et plus)
mov    rax, QWORD PTR [rdi]            ; lire le vptr  
call   QWORD PTR [rax+0x20]            ; indexer + appeler en une instruction  
```

Ou encore, si le pointeur `this` vient directement d'un registre :

```asm
; Forme ultra-compacte (fréquente en -O2)
mov    rax, QWORD PTR [rbx]            ; rbx = this, lire le vptr  
call   QWORD PTR [rax+0x20]            ; dispatch  
```

L'**offset dans `[rax+X]`** est la clé pour identifier quelle méthode virtuelle est appelée. Divisez l'offset par 8 (la taille d'un pointeur sur x86-64) pour obtenir l'index dans la vtable :

| Offset | Index | Méthode (dans notre binaire) |  
|--------|-------|------------------------------|  
| `+0x00` | 0 | `~Processor()` (complete destructor) |  
| `+0x08` | 1 | `~Processor()` (deleting destructor) |  
| `+0x10` | 2 | `name()` |  
| `+0x18` | 3 | `configure()` |  
| `+0x20` | 4 | `process()` |  
| `+0x28` | 5 | `status()` |

Ce tableau est votre **carte de référence** tout au long de l'analyse. Reconstruisez-le en section 22.1, puis utilisez-le ici pour décoder chaque appel virtuel rencontré dans le désassemblage.

---

## Lecture dans Ghidra

### Le décompileur et les appels virtuels

Le décompileur de Ghidra transforme le pattern assembleur en pseudo-C. Cependant, sans annotation manuelle, le résultat est souvent cryptique :

```c
/* Avant annotation */
iVar1 = (*(code *)*(long *)(*param_1 + 0x20))
            (param_1, local_38, local_40, local_48, local_50);
```

Cette ligne se lit comme suit :

- `*param_1` → déréférence de `this` → lecture du vptr.  
- `*param_1 + 0x20` → entrée à l'offset `0x20` dans la vtable.  
- `*(long *)( ... )` → lecture du pointeur de fonction.  
- `(*(code *)( ... ))( ... )` → appel de cette fonction avec les arguments.

Après avoir créé la structure `Processor` et appliqué le type sur `param_1`, le décompilé devient :

```c
/* Après annotation */
iVar1 = (*processor->vptr->process)(processor, input, in_len, output, out_cap);
```

Et si vous avez défini un type vtable structuré :

```c
/* Avec une vtable typée */
iVar1 = processor->vptr->process(processor, input, in_len, output, out_cap);
```

### Créer un type vtable dans Ghidra

Pour obtenir ce résultat lisible, créez une structure représentant la vtable dans le Data Type Manager :

```
struct Processor_vtable {
    void*  dtor_complete;      /* +0x00 */
    void*  dtor_deleting;      /* +0x08 */
    void*  name;               /* +0x10 — retourne const char* */
    void*  configure;          /* +0x18 — retourne bool */
    void*  process;            /* +0x20 — retourne int */
    void*  status;             /* +0x28 — retourne const char* */
};
```

Puis modifiez la structure `Processor` pour que le premier champ soit un pointeur vers `Processor_vtable` :

```
struct Processor {
    Processor_vtable*  vptr;       /* +0x00 */
    uint32_t           id_;        /* +0x08 */
    int                priority_;  /* +0x0C */
    bool               enabled_;   /* +0x10 */
    /* padding 7 bytes */
};
```

Appliquez ce type sur les paramètres et variables locales dans les fonctions pertinentes. Le décompilé deviendra immédiatement compréhensible.

> 💡 Pour aller plus loin, vous pouvez typer chaque entrée de la vtable avec un prototype de pointeur de fonction au lieu de `void*`. Par exemple, le champ `process` deviendrait `int (*process)(Processor*, const char*, size_t, char*, size_t)`. Le décompilé affichera alors les types des arguments.

### Naviguer depuis un appel virtuel jusqu'à l'implémentation

Face à un `call QWORD PTR [rax+0x20]`, vous savez que c'est `process()` (index 4). Mais **quelle implémentation** est appelée ? Le désassembleur ne peut pas le savoir — c'est déterminé à l'exécution par le type concret de l'objet.

Pour identifier les implémentations possibles :

1. Ouvrez chaque vtable reconstruite en section 22.1.  
2. Regardez l'entrée à l'index 4 dans chacune.  
3. Chaque pointeur de fonction à cet index est une implémentation candidate.

Pour notre binaire :

| Vtable | Entrée index 4 (`+0x20`) | Implémentation |  
|--------|--------------------------|----------------|  
| `Processor` | `__cxa_pure_virtual` | (virtuel pur — jamais appelé) |  
| `UpperCaseProcessor` | `FUN_00401a30` | `UpperCaseProcessor::process()` |  
| `ReverseProcessor` | `FUN_00401b80` | `ReverseProcessor::process()` |  
| `Rot13Processor` (plugin) | `0x7f...6150` | `Rot13Processor::process()` |  
| `XorCipherProcessor` (plugin) | `0x7f...a1c0` | `XorCipherProcessor::process()` |

L'entrée `__cxa_pure_virtual` dans la vtable de `Processor` confirme que `process()` est une méthode virtuelle pure dans la classe de base. Si elle était appelée (ce qui ne devrait pas arriver), le programme crasherait avec un message « pure virtual method called ».

---

## Le dispatch virtuel vu par GDB

### Observer le dispatch en temps réel

Posez un breakpoint dans `Pipeline::execute()`, juste avant l'appel virtuel à `process()`. En `-O0` avec symboles, vous pouvez cibler la ligne exacte. Sans symboles, cherchez le pattern `call rax` ou `call QWORD PTR [rax+0x20]` dans le désassemblage de la fonction.

```
(gdb) disas Pipeline::execute
   ...
   0x0000000000402340 <+208>:  mov    rax,QWORD PTR [rdi]
   0x0000000000402343 <+211>:  call   QWORD PTR [rax+0x20]
   ...
(gdb) break *0x0000000000402343
(gdb) run -p ./plugins "Hello World"
```

À chaque arrêt sur ce breakpoint, le processeur appelé est différent (selon la boucle du pipeline). Inspectez les valeurs :

```
Breakpoint hit.

(gdb) info registers rdi rax
rdi   0x555555808010       ← this (l'objet Processor courant)  
rax   0x7ffff7fb7d00       ← vptr (adresse de la vtable)  

(gdb) x/gx $rax+0x20
0x7ffff7fb7d20: 0x00007ffff7fb6150    ← adresse de process()

(gdb) info symbol 0x00007ffff7fb6150
Rot13Processor::process(...) in section .text of ./plugins/plugin_alpha.so
```

Vous avez identifié que cet appel va atteindre `Rot13Processor::process()`. Continuez avec `continue` pour voir le prochain passage dans la boucle — le vptr sera différent, pointant vers la vtable d'un autre processeur.

### Script GDB pour logger chaque dispatch

Automatisez le traçage avec un script GDB :

```python
# trace_dispatch.py — à charger dans GDB avec : source trace_dispatch.py

import gdb

class DispatchBreakpoint(gdb.Breakpoint):
    """Breakpoint sur un call indirect qui logue le dispatch virtuel."""

    def __init__(self, addr, vtable_offset):
        super().__init__("*" + hex(addr), internal=False)
        self.vtable_offset = vtable_offset
        self.hit_count = 0

    def stop(self):
        self.hit_count += 1
        rdi = int(gdb.parse_and_eval("$rdi"))
        vptr = int(gdb.parse_and_eval("*(unsigned long long*)$rdi"))
        target_addr = int(gdb.parse_and_eval(
            "*(unsigned long long*)({} + {})".format(vptr, self.vtable_offset)))

        # Résoudre le symbole
        try:
            sym = gdb.execute("info symbol {}".format(hex(target_addr)),
                              to_string=True).strip()
        except:
            sym = "???"

        print("[dispatch #{}] this={:#x} vptr={:#x} target={:#x} → {}".format(
            self.hit_count, rdi, vptr, target_addr, sym))

        return False  # Ne pas arrêter, juste logger

# Exemple d'utilisation :
# Remplacez l'adresse par celle du call indirect dans votre binaire
# DispatchBreakpoint(0x402343, 0x20)
```

Ce script pose un breakpoint non-bloquant qui affiche le type réel de l'objet et la méthode appelée à chaque passage. Chargez-le et instanciez-le :

```
(gdb) source trace_dispatch.py
(gdb) python DispatchBreakpoint(0x402343, 0x20)
(gdb) run -p ./plugins "Hello World"
```

Sortie :

```
[dispatch #1] this=0x5555558070f0 vptr=0x404a40 target=0x401a30
    → UpperCaseProcessor::process(...) in section .text of oop_O0
[dispatch #2] this=0x555555807130 vptr=0x404a90 target=0x401b80
    → ReverseProcessor::process(...) in section .text of oop_O0
[dispatch #3] this=0x555555808010 vptr=0x7ffff7fb7d00 target=0x7ffff7fb6150
    → Rot13Processor::process(...) in section .text of plugin_alpha.so
[dispatch #4] this=0x555555808050 vptr=0x7ffff7daba00 target=0x7ffff7daa1c0
    → XorCipherProcessor::process(...) in section .text of plugin_beta.so
```

En quatre lignes, vous voyez le pipeline complet : quatre objets de types différents, quatre vtables différentes, quatre implémentations différentes de `process()`, toutes appelées depuis le même site d'appel dans `Pipeline::execute()`. C'est le polymorphisme en action, rendu visible par l'instrumentation.

---

## Impact des optimisations sur le dispatch

### `-O0` : le dispatch canonique

En `-O0`, GCC ne fait aucune optimisation. Chaque appel virtuel passe systématiquement par la vtable, même si le compilateur pourrait théoriquement déduire le type. Le code est verbeux mais parfaitement lisible — c'est pourquoi nous commençons toujours l'analyse par cette variante.

### `-O2` : dévirtualisation

GCC en `-O2` (et surtout `-O3`) peut **dévirtualiser** un appel virtuel quand il est capable de prouver le type concret de l'objet au site d'appel. Concrètement, il remplace le `call QWORD PTR [rax+offset]` par un `call <adresse_directe>`.

Considérons cette séquence dans `main()` :

```cpp
UpperCaseProcessor* upper = new UpperCaseProcessor(0);  
upper->configure("skip_digits", "true");  
```

Ici, le compilateur sait que `upper` est de type `UpperCaseProcessor*` — le `new` et le constructeur sont dans la même fonction. Il n'y a aucune ambiguïté sur le type. En `-O2`, GCC peut remplacer :

```asm
; -O0 : dispatch virtuel (même quand le type est connu)
mov    rax, QWORD PTR [rdi]  
call   QWORD PTR [rax+0x18]        ; configure() via vtable  
```

par :

```asm
; -O2 : appel direct (dévirtualisé)
call   UpperCaseProcessor::configure()   ; appel direct, pas de vtable
```

Ou même inliner complètement la méthode si elle est assez courte.

**Conséquence pour le RE** : en `-O2`, certains appels qui étaient virtuels en `-O0` deviennent des appels directs. Vous pourriez croire que la méthode n'est pas virtuelle, ou que la classe n'a pas de vtable. Comparez toujours les versions `-O0` et `-O2` pour distinguer les vraies méthodes non-virtuelles des appels dévirtualisés.

### Comment reconnaître une dévirtualisation

Un appel dévirtualisé présente ces caractéristiques :

- C'est un `call <adresse>` direct (pas `call rax` ni `call [rax+X]`).  
- La fonction appelée **existe aussi** dans une vtable — vous la trouvez en tant qu'entrée dans la vtable de la classe quand vous l'analysez.  
- Le `this` passé en `rdi` provient d'un `new` récent ou d'un contexte où le type est non ambigu.

Si la même fonction apparaît à la fois comme appel direct (dans `main()`) et comme appel indirect (dans `Pipeline::execute()`), c'est un signe fort de dévirtualisation partielle : le compilateur a pu optimiser dans un contexte mais pas dans l'autre.

### `-O2` avec LTO (`-flto`) : dévirtualisation inter-module

Avec Link-Time Optimization (chapitre 16.5), GCC voit l'ensemble du programme au moment du link, y compris les types concrets créés. Il peut dévirtualiser des appels qui traversent les frontières de fichiers `.o`.

Cependant, les plugins chargés via `dlopen` ne sont **jamais** dévirtualisés, car ils ne sont pas disponibles au link time. Le dispatch virtuel reste toujours un `call` indirect pour les méthodes de plugins — c'est une garantie architecturale que vous pouvez exploiter en RE.

### Inlining du dispatch

En `-O3`, GCC peut aller plus loin que la dévirtualisation : il peut **inliner** la méthode dévirtualisée. Le code de la méthode est alors copié directement dans la fonction appelante. Il n'y a plus ni `call` indirect, ni `call` direct — la méthode a littéralement disparu en tant qu'entité séparée.

Pour le reverse engineer, c'est le scénario le plus complexe. Vous voyez du code de traitement dans `Pipeline::execute()` sans comprendre qu'il provient d'une méthode virtuelle inlinée. Les indices restants sont :

- Le code inliné est souvent **encadré par un test de type** (`cmp` sur le vptr suivi d'un branchement) si GCC n'est pas totalement certain du type. C'est une **dévirtualisation spéculative** : le compilateur injecte le code de la méthode attendue mais conserve un fallback virtuel au cas où.  
- Les accès mémoire dans le code inliné utilisent les offsets typiques de la structure de la classe (par exemple, `[rdi+0x11]` pour `skip_digits_` dans `UpperCaseProcessor`).

---

## Distinguer les différents types d'appels dans un binaire C++

Au fil de l'analyse, vous rencontrerez trois types d'appels. Savoir les distinguer instantanément est une compétence fondamentale.

### Appel direct (non virtuel ou dévirtualisé)

```asm
call   0x401a30                         ; adresse fixe dans .text
; ou
call   UpperCaseProcessor::process      ; si symboles présents
```

**Reconnaissance** : `call` suivi d'une adresse immédiate ou d'un symbole. Pas de lecture de vptr, pas d'indirection.

**Interprétation** : soit la méthode n'est pas virtuelle (méthode non-virtuelle, fonction libre, accesseur inliné), soit le compilateur a dévirtualisé l'appel.

### Appel virtuel (dispatch via vtable)

```asm
mov    rax, QWORD PTR [rdi]            ; lire vptr  
call   QWORD PTR [rax+0x20]            ; indexer et appeler  
```

**Reconnaissance** : `call` via registre ou indirection mémoire, précédé d'une lecture `[rdi]` (lecture du vptr depuis `this`).

**Interprétation** : appel polymorphe. La méthode appelée dépend du type réel de l'objet à l'exécution. L'offset vous donne l'index dans la vtable.

### Appel via pointeur de fonction (non-virtuel)

```asm
mov    rax, QWORD PTR [rbp-0x10]       ; charger un pointeur de fonction  
call   rax  
```

**Reconnaissance** : `call` via registre, mais **sans** lecture de vptr au préalable. Le pointeur de fonction vient d'une variable (pile, globale, structure), pas du premier champ d'un objet.

**Interprétation** : callback, pointeur de fonction stocké dans une structure (comme les `create_func_t` et `destroy_func_t` de la section 22.2), ou résultat de `dlsym`. Ce n'est pas du polymorphisme C++ — c'est du polymorphisme « C-style ».

La distinction entre les deux derniers cas repose sur l'**origine du pointeur**. Si le pointeur vient de `[rdi]` (premier champ de l'objet = vptr) puis est indexé, c'est du dispatch virtuel. Si le pointeur vient d'ailleurs, c'est un pointeur de fonction ordinaire.

---

## Tracer le dispatch avec Frida : qui appelle quoi, quand

Le script suivant intercepte un appel virtuel à une adresse donnée et résout dynamiquement la méthode cible :

```javascript
// frida_trace_vcall.js
// Trace un site d'appel virtuel dans Pipeline::execute()

var VCALL_ADDR = ptr("0x402343");  // Adresse du call indirect (à adapter)  
var VTABLE_OFFSET = 0x20;          // Offset dans la vtable (process = index 4)  

Interceptor.attach(VCALL_ADDR, {
    onEnter: function(args) {
        // À ce point, rdi = this
        var thisPtr = this.context.rdi;
        var vptr = thisPtr.readPointer();
        var targetFn = vptr.add(VTABLE_OFFSET).readPointer();
        var sym = DebugSymbol.fromAddress(targetFn);

        // Lire le nom via la méthode name() — index 2 dans la vtable
        var nameFn = new NativeFunction(
            vptr.add(0x10).readPointer(),   // name() à l'offset 0x10
            'pointer', ['pointer']);
        var name = nameFn(thisPtr).readUtf8String();

        console.log("[vcall] " + name + " → " + sym.name +
                    " (this=" + thisPtr + ")");
    }
});
```

Ce script va plus loin que le simple traçage d'adresses : il appelle `name()` (une autre méthode virtuelle) sur l'objet pour obtenir le nom lisible du processeur. Sortie :

```
[vcall] UpperCaseProcessor → UpperCaseProcessor::process() (this=0x5555558070f0)
[vcall] ReverseProcessor → ReverseProcessor::process() (this=0x555555807130)
[vcall] Rot13Processor → Rot13Processor::process() (this=0x555555808010)
[vcall] XorCipherProcessor → XorCipherProcessor::process() (this=0x555555808050)
```

---

## Cas avancé : dispatch virtuel et héritage multiple

Notre binaire `ch22-oop` utilise l'héritage simple. Mais en RE réel, vous rencontrerez de l'héritage multiple. Voici les différences clés à connaître pour ne pas être pris au dépourvu.

### Héritage simple : un seul vptr

Avec l'héritage simple (notre cas), chaque objet contient un seul vptr à l'offset `+0x00`. Toutes les méthodes virtuelles — de la classe de base et des classes dérivées — sont dans une seule vtable.

```
Objet UpperCaseProcessor (héritage simple) :
  +0x00  vptr ──→ vtable unique (toutes les méthodes)
  +0x08  id_
  +0x0C  priority_
  +0x10  enabled_
  +0x11  skip_digits_          (bool, juste après enabled_ sans padding)
  +0x18  bytes_processed_
```

### Héritage multiple : plusieurs vptrs

Quand une classe hérite de deux classes de base polymorphes, l'objet contient **deux vptrs** :

```
class Serializable { virtual void serialize(); ... };  
class Processor { virtual void process(); ... };  
class AdvancedProcessor : public Processor, public Serializable { ... };  
```

L'objet `AdvancedProcessor` en mémoire :

```
  +0x00  vptr_Processor      ──→ vtable Processor-part
  +0x08  id_ (de Processor)
  ...
  +0x20  vptr_Serializable   ──→ vtable Serializable-part
  +0x28  champs de Serializable
  ...
```

En RE, le signe révélateur est la présence de **deux pointeurs vers `.data.rel.ro`** au sein du même objet, à des offsets différents. Le typeinfo de la classe utilise alors `__vmi_class_type_info` au lieu de `__si_class_type_info`, et contient la liste de toutes les classes parentes avec leurs offsets.

L'autre signe est l'**offset-to-top** dans la vtable secondaire. Pour la partie `Serializable`, l'offset-to-top est négatif (par exemple `-0x20`) et indique la distance entre le sous-objet `Serializable` et le début de l'objet complet. Quand un appel virtuel passe par le vptr secondaire, le compilateur insère un **thunk** — une petite fonction qui ajuste le pointeur `this` avant de sauter vers la vraie méthode :

```asm
; Thunk pour AdvancedProcessor::serialize() via le vptr Serializable
; Ajuste this en soustrayant l'offset du sous-objet
sub    rdi, 0x20          ; rdi pointait vers le sous-objet Serializable  
jmp    AdvancedProcessor::serialize()   ; maintenant rdi pointe vers le début  
```

Les thunks sont des fonctions très courtes (2-3 instructions) que Ghidra identifie souvent automatiquement. Si vous voyez une fonction qui fait uniquement `sub rdi, N` puis `jmp <autre_fonction>`, c'est très probablement un thunk de vtable.

---

## Résumé : lire un appel virtuel en 30 secondes

Face à un appel indirect dans un binaire C++, appliquez cette routine mentale :

**1. Identifier le pattern.** `mov rax, [rdi]` suivi de `call [rax+X]` ? C'est un dispatch virtuel.

**2. Calculer l'index.** Offset ÷ 8 = index dans la vtable. Consultez votre tableau de correspondance (reconstruit en section 22.1).

**3. Identifier les implémentations candidates.** Pour chaque vtable reconstruite, regardez l'entrée à cet index. Ce sont les implémentations possibles.

**4. Déterminer le type réel (si nécessaire).** Remontez le flux de données pour trouver d'où vient le pointeur `this`. S'il sort d'un `new UpperCaseProcessor`, le type est connu. S'il sort d'un vecteur de `Processor*`, le type est ambigu — seule l'analyse dynamique tranchera.

**5. Vérifier les optimisations.** Comparez avec la version `-O0`. Si un appel est direct en `-O2` mais virtuel en `-O0`, c'est une dévirtualisation. Si du code apparaît inline, c'est un inlining post-dévirtualisation.

Ce réflexe deviendra automatique avec la pratique. Chaque binaire C++ que vous analyserez contiendra des dizaines, voire des centaines d'appels virtuels. La capacité à les décoder rapidement est ce qui distingue une analyse qui piétine d'une analyse qui progresse.

---


⏭️ [Patcher un comportement via `LD_PRELOAD`](/22-oop/04-patcher-ld-preload.md)
