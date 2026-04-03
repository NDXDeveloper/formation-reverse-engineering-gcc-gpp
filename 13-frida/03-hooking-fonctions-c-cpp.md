🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 13.3 — Hooking de fonctions C et C++ à la volée

> 🧰 **Outils utilisés** : `frida`, `frida-trace`, Python 3 + module `frida`  
> 📦 **Binaires utilisés** : `binaries/ch13-keygenme/keygenme_O0`, `binaries/ch13-oop/oop_O0`  
> 📖 **Prérequis** : [13.1 — Architecture de Frida](/13-frida/01-architecture-frida.md), [13.2 — Modes d'injection](/13-frida/02-modes-injection.md), [Chapitre 3 — Assembleur x86-64](/03-assembleur-x86-64/README.md) (conventions d'appel)

---

## L'API Interceptor : la pièce maîtresse

Nous avons vu en section 13.1 que Frida installe des trampolines dans le code du processus cible. L'API qui expose cette mécanique au JavaScript est `Interceptor`. C'est l'outil que vous utiliserez le plus souvent — la quasi-totalité des scénarios de RE dynamique avec Frida passent par `Interceptor.attach()`.

Le principe est simple : vous désignez une adresse en mémoire (l'entrée d'une fonction), et vous fournissez deux callbacks optionnels — `onEnter`, appelé quand l'exécution atteint cette adresse, et `onLeave`, appelé quand la fonction retourne. Entre les deux, la fonction originale s'exécute normalement, sans modification.

```javascript
Interceptor.attach(adresse, {
    onEnter(args) {
        // Appelé AVANT que la fonction ne s'exécute
        // args[0], args[1]... = arguments de la fonction
    },
    onLeave(retval) {
        // Appelé APRÈS que la fonction a retourné
        // retval = valeur de retour
    }
});
```

La difficulté n'est pas dans l'API elle-même — elle est concise et intuitive. La difficulté est de **trouver l'adresse** de la fonction à hooker et d'**interpréter correctement ses arguments**. C'est là que convergent les compétences acquises dans les parties précédentes : analyse statique (Ghidra, objdump), connaissance des conventions d'appel (chapitre 3), et compréhension du format ELF (chapitre 2).

---

## Hooker une fonction C exportée par nom

Le cas le plus simple est celui d'une fonction dont le symbole est présent dans la table d'exports d'une bibliothèque partagée — typiquement les fonctions de la libc.

### Résolution par nom avec `Module.findExportByName`

```javascript
// Trouver l'adresse de strcmp dans n'importe quel module chargé
const strcmp_addr = Module.findExportByName(null, "strcmp");
```

Le premier argument est le nom du module (bibliothèque). `null` signifie « chercher dans tous les modules chargés ». On peut restreindre la recherche :

```javascript
// Uniquement dans la libc
const strcmp_addr = Module.findExportByName("libc.so.6", "strcmp");
```

Si la fonction n'est pas trouvée, `findExportByName` retourne `null`. Il existe une variante qui lève une exception :

```javascript
// Lance une exception si le symbole n'existe pas
const strcmp_addr = Module.getExportByName(null, "strcmp");
```

> 💡 **Convention** : les méthodes `find*` retournent `null` en cas d'échec, les méthodes `get*` lèvent une exception. Ce pattern est systématique dans l'API Frida.

### Hook complet sur `strcmp`

Combinons la résolution de symbole et le hook. Rappelons la signature de `strcmp` :

```c
int strcmp(const char *s1, const char *s2);
```

Selon la convention System V AMD64 (chapitre 3, section 3.6), `s1` est dans `rdi` et `s2` dans `rsi`. Dans Frida, `args[0]` correspond à `rdi`, `args[1]` à `rsi`, et ainsi de suite pour les arguments suivants (`rdx`, `rcx`, `r8`, `r9`).

```javascript
Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        const s1 = args[0].readUtf8String();
        const s2 = args[1].readUtf8String();
        console.log(`strcmp("${s1}", "${s2}")`);
    },
    onLeave(retval) {
        console.log(`  => retour : ${retval.toInt32()}`);
    }
});
```

Sortie typique sur `keygenme_O0` :

```
strcmp("SECRETKEY-2024", "test123")
  => retour : 1
strcmp("SECRETKEY-2024", "SECRETKEY-2024")
  => retour : 0
```

La valeur de retour `0` indique une correspondance. En deux lignes de hook, on a extrait la clé attendue — exactement comme avec un breakpoint conditionnel dans GDB, mais sans interrompre le programme.

### L'objet `args` : un tableau de `NativePointer`

Chaque élément de `args` est un `NativePointer` — un objet Frida qui encapsule une adresse mémoire 64 bits. Il dispose de méthodes de lecture adaptées au type de donnée pointée :

| Méthode | Type C correspondant | Exemple |  
|---|---|---|  
| `args[i].readUtf8String()` | `const char *` (UTF-8) | Chaîne de caractères |  
| `args[i].readCString()` | `const char *` (ASCII) | Chaîne C classique |  
| `args[i].readU8()` | `uint8_t` (déréférencé) | Octet à l'adresse pointée |  
| `args[i].readU32()` | `uint32_t` (déréférencé) | Entier 32 bits à l'adresse pointée |  
| `args[i].readU64()` | `uint64_t` (déréférencé) | Entier 64 bits à l'adresse pointée |  
| `args[i].readPointer()` | `void *` (déréférencé) | Pointeur à l'adresse pointée |  
| `args[i].readByteArray(n)` | `void *` + taille | Buffer de `n` octets |  
| `args[i].toInt32()` | `int` (valeur directe) | Entier passé par valeur |  
| `args[i].toUInt32()` | `unsigned int` (valeur directe) | Entier non signé par valeur |

La distinction est fondamentale : `.readUtf8String()` **déréférence le pointeur** et lit la chaîne à l'adresse pointée, tandis que `.toInt32()` interprète la **valeur du pointeur elle-même** comme un entier. Pour un argument `int fd` passé par valeur, on utilise `.toInt32()`. Pour un argument `const char *buf` qui est un pointeur vers des données, on utilise `.readUtf8String()` ou `.readByteArray(n)`.

### Hook sur `open`

Illustrons avec un autre exemple classique — l'appel système `open` (en réalité, l'appel à la wrapper libc `open`) :

```c
int open(const char *pathname, int flags, mode_t mode);
```

```javascript
Interceptor.attach(Module.findExportByName(null, "open"), {
    onEnter(args) {
        this.path = args[0].readUtf8String();
        this.flags = args[1].toInt32();
    },
    onLeave(retval) {
        const fd = retval.toInt32();
        console.log(`open("${this.path}", ${this.flags}) = ${fd}`);
    }
});
```

Notez l'utilisation de `this` pour transmettre des données de `onEnter` à `onLeave`. L'objet `this` est un espace de stockage par invocation : chaque appel à la fonction hookée dispose de son propre `this`, ce qui évite les collisions quand la fonction est appelée simultanément par plusieurs threads ou de manière récursive.

---

## Hooker une fonction locale (sans export) par adresse

Les fonctions intéressantes d'un binaire ne sont pas toujours exportées. Une fonction `check_password` dans `keygenme_O0` n'apparaîtra pas dans les exports de la libc — c'est une fonction interne au binaire. Si le binaire n'est pas strippé, elle peut apparaître dans la table de symboles locaux. Si le binaire est strippé, il faudra trouver son adresse par d'autres moyens (Ghidra, objdump, analyse statique).

### Résolution dans le binaire principal

Pour les symboles locaux (non exportés mais présents dans la table de symboles), `Module.enumerateSymbols()` ou `DebugSymbol.fromName()` peuvent fonctionner :

```javascript
// Lister tous les symboles du binaire principal
const mod = Process.enumerateModules()[0]; // premier module = binaire principal  
const symbols = mod.enumerateSymbols();  

symbols.forEach(sym => {
    if (sym.name.includes("check") || sym.name.includes("verify")) {
        console.log(`${sym.name} @ ${sym.address}`);
    }
});
```

### Hook par adresse brute

Quand le binaire est strippé et que les symboles ont disparu, il reste l'adresse. Supposons que Ghidra vous indique que la fonction de vérification commence à l'offset `0x1234` dans le binaire. Il faut convertir cet offset en adresse virtuelle au runtime.

```javascript
// Adresse de base du module principal
const base = Process.enumerateModules()[0].base;

// Offset trouvé dans Ghidra
const offset = 0x1234;

// Adresse virtuelle = base + offset
const check_addr = base.add(offset);

console.log(`Fonction cible @ ${check_addr}`);

Interceptor.attach(check_addr, {
    onEnter(args) {
        console.log("check_password() appelée");
        console.log("  arg0 (rdi) :", args[0].readUtf8String());
    },
    onLeave(retval) {
        console.log("  retour :", retval.toInt32());
    }
});
```

**Pourquoi ajouter la base ?** Les binaires compilés avec `-pie` (Position-Independent Executable, valeur par défaut de GCC moderne) sont chargés à une adresse de base aléatoire par l'ASLR (section 2.8). L'offset `0x1234` dans Ghidra est relatif au début du fichier ELF. En mémoire, le binaire commence à l'adresse `base`, donc la fonction se trouve à `base + 0x1234`.

Pour un binaire non-PIE (compilé avec `-no-pie`), les adresses dans Ghidra correspondent directement aux adresses virtuelles, et on peut utiliser l'adresse brute :

```javascript
// Binaire non-PIE : l'adresse Ghidra est l'adresse en mémoire
Interceptor.attach(ptr("0x401234"), {
    onEnter(args) { /* ... */ }
});
```

> ⚠️ **Piège classique** : utiliser une adresse absolue de Ghidra sur un binaire PIE. L'adresse de base dans Ghidra est souvent `0x100000` ou `0x0`, tandis que l'adresse réelle en mémoire sera quelque chose comme `0x556a3f200000`. Toujours vérifier avec `Process.enumerateModules()[0].base` si le binaire est PIE.

### Raccourci avec `Module.getBaseAddress`

Si vous connaissez le nom du module :

```javascript
const base = Module.getBaseAddress("keygenme_O0");  
Interceptor.attach(base.add(0x1234), { /* ... */ });  
```

---

## Hooker des fonctions C++

Le C++ introduit deux complications majeures pour le hooking : le **name mangling** et les **fonctions virtuelles**. Les deux ont été étudiés en détail au chapitre 17 (sections 17.1 et 17.2), et Frida fournit les outils pour les gérer.

### Name mangling : trouver le symbole C++ réel

Rappelons que le compilateur C++ transforme les noms de fonctions pour encoder leur signature. La méthode `Animal::speak(std::string const&)` devient quelque chose comme `_ZN6Animal5speakERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE`. C'est ce nom manglé qui apparaît dans la table de symboles du binaire.

**Approche 1 : chercher le symbole manglé dans le binaire.**

On peut énumérer les exports ou symboles et filtrer par le nom démanglé :

```javascript
const mod = Process.enumerateModules()[0];

mod.enumerateSymbols().forEach(sym => {
    // Le champ name contient le nom manglé
    // Frida ne démangle pas automatiquement, mais on peut chercher des fragments
    if (sym.name.includes("Animal") && sym.name.includes("speak")) {
        console.log(`${sym.name} @ ${sym.address}`);
    }
});
```

**Approche 2 : utiliser le nom manglé directement.**

Si vous avez identifié le symbole manglé via `nm`, `objdump -t`, `c++filt` (chapitre 7, section 7.6) ou Ghidra, vous pouvez le passer directement :

```javascript
const speak_addr = Module.findExportByName(null,
    "_ZN6Animal5speakERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE"
);

if (speak_addr) {
    Interceptor.attach(speak_addr, {
        onEnter(args) {
            // En C++, args[0] = this (le pointeur implicite vers l'objet)
            console.log("Animal::speak() appelé");
            console.log("  this :", args[0]);
        }
    });
}
```

**Approche 3 : `frida-trace` avec des globs.**

`frida-trace` gère très bien le name mangling grâce aux globs :

```bash
# Hooker toutes les méthodes de la classe Animal
frida-trace -f ./oop_O0 -i "*Animal*"

# Hooker toutes les méthodes speak, quelle que soit la classe
frida-trace -f ./oop_O0 -i "*speak*"
```

`frida-trace` résout les symboles manglés et affiche les noms démanglés dans sa sortie, ce qui en fait un excellent outil de reconnaissance pour le C++.

### Le pointeur `this` implicite

Point crucial pour le hooking C++ : dans la convention Itanium ABI (utilisée par GCC, voir chapitre 17 section 17.1), le pointeur `this` est passé comme **premier argument implicite** de toute méthode non statique. Cela signifie que dans un hook Frida :

- `args[0]` = `this` (pointeur vers l'objet)  
- `args[1]` = premier argument explicite de la méthode  
- `args[2]` = deuxième argument explicite  
- etc.

```cpp
// Signature C++ :
class Crypto {
    int decrypt(const char *input, char *output, int length);
};
```

```javascript
// Hook Frida — attention au décalage causé par this
Interceptor.attach(decrypt_addr, {
    onEnter(args) {
        // args[0] = this (pointeur vers l'objet Crypto)
        // args[1] = input (const char *)
        // args[2] = output (char *)
        // args[3] = length (int)

        this.thisPtr = args[0];
        this.input = args[1];
        this.length = args[3].toInt32();

        console.log(`Crypto::decrypt() appelé`);
        console.log(`  this   : ${this.thisPtr}`);
        console.log(`  input  : ${this.input.readByteArray(this.length)}`);
        console.log(`  length : ${this.length}`);
    },
    onLeave(retval) {
        // Lire le buffer output après que la fonction l'a rempli
        // (output est à args[2], sauvegardé si nécessaire dans onEnter)
    }
});
```

Oublier le décalage `this` est l'erreur la plus fréquente en hooking C++. Si vos arguments semblent décalés d'un cran — la « chaîne » que vous lisez dans `args[0]` ressemble à une adresse de heap plutôt qu'à du texte — c'est probablement parce que vous lisez `this` au lieu du premier argument réel.

### Hooker des fonctions virtuelles via la vtable

Pour les fonctions virtuelles, l'adresse de la fonction concrète appelée dépend du type dynamique de l'objet, via le mécanisme de vtable (chapitre 17, section 17.2). On peut hooker soit l'implémentation spécifique d'une classe (en trouvant son adresse par symbole ou dans Ghidra), soit intercepter l'appel indirect en hookant le slot de la vtable.

**Approche directe : hooker l'implémentation.**

Si Ghidra ou `nm` vous montre que `Dog::speak()` est à l'adresse `0x2a40` (offset), hookez-la comme n'importe quelle fonction :

```javascript
const base = Process.enumerateModules()[0].base;  
Interceptor.attach(base.add(0x2a40), {  
    onEnter(args) {
        console.log("Dog::speak() (implémentation concrète)");
    }
});
```

Ce hook ne capturera que les appels à `Dog::speak()`, pas à `Cat::speak()` — même si les deux sont appelés via un pointeur `Animal*`. C'est généralement ce qu'on veut en RE : comprendre quelle implémentation concrète est exécutée.

**Approche vtable : lire et remplacer les pointeurs de la table.**

Cette approche avancée consiste à localiser la vtable en mémoire et à remplacer le pointeur de fonction par celui d'un trampoline custom. C'est plus complexe et rarement nécessaire — nous la mentionnons pour être exhaustif, mais le hooking par adresse directe couvre la grande majorité des cas.

```javascript
// Lire le vptr d'un objet (premier champ de l'objet en mémoire)
const obj_addr = ptr("0x...");  // adresse d'un objet Animal  
const vtable_ptr = obj_addr.readPointer();  

// Le premier slot de la vtable est souvent la première fonction virtuelle
const first_virtual_fn = vtable_ptr.readPointer();  
console.log(`Première fonction virtuelle @ ${first_virtual_fn}`);  

// On peut hooker cette adresse
Interceptor.attach(first_virtual_fn, {
    onEnter(args) {
        console.log("Fonction virtuelle slot 0 appelée");
    }
});
```

---

## Enumération et recherche de fonctions

Avant de hooker, il faut souvent explorer le binaire pour trouver les fonctions intéressantes. Frida offre plusieurs mécanismes d'énumération.

### Lister les modules chargés

```javascript
Process.enumerateModules().forEach(mod => {
    console.log(`${mod.name.padEnd(30)} base=${mod.base} size=${mod.size}`);
});
```

Sortie typique :

```
keygenme_O0                    base=0x555555554000 size=0x3000  
linux-vdso.so.1                base=0x7ffd12ffe000 size=0x2000  
libc.so.6                      base=0x7f8a3c200000 size=0x1c1000  
ld-linux-x86-64.so.2           base=0x7f8a3c3e0000 size=0x2b000  
```

### Lister les exports d'un module

```javascript
const libc = Process.getModuleByName("libc.so.6");  
libc.enumerateExports().forEach(exp => {  
    if (exp.type === 'function' && exp.name.includes("str")) {
        console.log(`${exp.name} @ ${exp.address}`);
    }
});
```

### Lister les imports d'un module

Les imports montrent quelles fonctions externes le binaire appelle — c'est l'équivalent dynamique de l'analyse PLT/GOT (chapitre 2, section 2.9) :

```javascript
const main_mod = Process.enumerateModules()[0];  
main_mod.enumerateImports().forEach(imp => {  
    console.log(`Import: ${imp.name} depuis ${imp.module} @ ${imp.address}`);
});
```

Sortie :

```
Import: puts depuis libc.so.6 @ 0x7f8a3c245e10  
Import: strcmp depuis libc.so.6 @ 0x7f8a3c2c4560  
Import: printf depuis libc.so.6 @ 0x7f8a3c25f900  
Import: scanf depuis libc.so.6 @ 0x7f8a3c260120  
```

Cette liste est une mine d'or pour le RE. Les imports révèlent les capacités du binaire : il utilise `strcmp` (comparaison de chaînes — peut-être une vérification de mot de passe), `printf`/`scanf` (I/O console), etc.

### Recherche par pattern dans les symboles

Pour les binaires avec symboles (non strippés), `DebugSymbol` offre une recherche inverse — de l'adresse vers le nom :

```javascript
// Quel symbole se trouve à cette adresse ?
const sym = DebugSymbol.fromAddress(ptr("0x555555555189"));  
console.log(sym);  // { address: 0x555555555189, name: "check_password", ... }  
```

---

## Hooker plusieurs fonctions en une seule passe

En situation réelle, on veut souvent hooker un ensemble de fonctions simultanément. Voici un pattern courant qui installe des hooks sur toutes les fonctions de comparaison :

```javascript
const targets = ["strcmp", "strncmp", "memcmp", "strcasecmp"];

targets.forEach(name => {
    const addr = Module.findExportByName(null, name);
    if (addr === null) {
        console.log(`[!] ${name} non trouvé`);
        return;
    }

    Interceptor.attach(addr, {
        onEnter(args) {
            this.funcName = name;
            try {
                // Tenter de lire les deux premiers arguments comme des chaînes
                this.a = args[0].readUtf8String();
                this.b = args[1].readUtf8String();
            } catch (e) {
                // Si la lecture échoue (buffer binaire, adresse invalide), on loggue l'adresse
                this.a = args[0].toString();
                this.b = args[1].toString();
            }
        },
        onLeave(retval) {
            const result = retval.toInt32();
            // Ne logger que les correspondances (retour == 0)
            if (result === 0) {
                send({
                    func: this.funcName,
                    a: this.a,
                    b: this.b,
                    match: true
                });
            }
        }
    });

    console.log(`[+] Hook installé sur ${name} @ ${addr}`);
});
```

Le bloc `try/catch` autour de `readUtf8String()` est une précaution indispensable. `strcmp` reçoit toujours des chaînes valides, mais `memcmp` peut recevoir des buffers binaires arbitraires. Tenter de lire un buffer binaire comme une chaîne UTF-8 peut lever une exception si les octets ne forment pas une séquence UTF-8 valide.

---

## Filtrage des hooks : éviter le bruit

Un hook sur `strcmp` dans un programme non trivial peut générer des centaines d'appels par seconde — la libc elle-même utilise `strcmp` en interne pour toutes sortes d'opérations. Il est crucial de filtrer pour ne capturer que les appels pertinents.

### Filtrer par contenu des arguments

```javascript
Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        const s1 = args[0].readUtf8String();
        const s2 = args[1].readUtf8String();

        // Ne logger que si un des arguments contient "KEY" ou "password"
        if (s1 && (s1.includes("KEY") || s1.includes("password")) ||
            s2 && (s2.includes("KEY") || s2.includes("password"))) {
            console.log(`strcmp("${s1}", "${s2}")`);
        }
    }
});
```

### Filtrer par appelant (backtrace)

On peut inspecter la pile d'appels pour ne hooker que les `strcmp` appelés depuis le binaire principal (pas depuis la libc ou d'autres bibliothèques) :

```javascript
const main_mod = Process.enumerateModules()[0];  
const main_base = main_mod.base;  
const main_end = main_base.add(main_mod.size);  

Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        // Adresse de retour = qui a appelé strcmp
        const caller = this.returnAddress;

        // Ne continuer que si l'appelant est dans le binaire principal
        if (caller.compare(main_base) >= 0 && caller.compare(main_end) < 0) {
            console.log(`strcmp depuis le binaire principal :`);
            console.log(`  "${args[0].readUtf8String()}" vs "${args[1].readUtf8String()}"`);
        }
    }
});
```

`this.returnAddress` est un `NativePointer` qui contient l'adresse de l'instruction suivant le `call` qui a invoqué la fonction hookée. En vérifiant que cette adresse est dans la plage mémoire du module principal, on élimine tous les appels internes de la libc.

### Backtrace complète

Pour un diagnostic plus poussé, Frida peut produire une backtrace complète :

```javascript
Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        console.log("strcmp() appelé, backtrace :");
        console.log(Thread.backtrace(this.context, Backtracer.ACCURATE)
            .map(DebugSymbol.fromAddress)
            .join('\n'));
    }
});
```

`Thread.backtrace()` retourne un tableau d'adresses de retour (la pile d'appels). `DebugSymbol.fromAddress` convertit chaque adresse en un nom de symbole lisible. Le mode `Backtracer.ACCURATE` utilise les informations DWARF pour un déroulement précis de la pile ; `Backtracer.FUZZY` est plus rapide mais moins fiable.

---

## Détacher un hook

Les hooks installés par `Interceptor.attach()` restent actifs jusqu'à ce qu'on les retire explicitement ou que le script soit déchargé. Pour retirer un hook spécifique :

```javascript
const listener = Interceptor.attach(addr, {
    onEnter(args) { /* ... */ }
});

// Plus tard, quand on n'en a plus besoin :
listener.detach();
```

Pour retirer **tous** les hooks installés par le script :

```javascript
Interceptor.detachAll();
```

Le détachement restaure les instructions originales de la fonction — le trampoline est supprimé et la fonction retrouve son comportement natif.

---

## `Interceptor.replace` : remplacer une fonction entièrement

`Interceptor.attach` observe et peut modifier les arguments et la valeur de retour, mais la fonction originale s'exécute toujours. `Interceptor.replace` va plus loin : il remplace complètement la fonction par une implémentation JavaScript.

```javascript
const orig_check = new NativeFunction(
    Module.findExportByName(null, "check_password"),
    'int',           // type de retour
    ['pointer']      // types des arguments
);

Interceptor.replace(Module.findExportByName(null, "check_password"),
    new NativeCallback(function (input) {
        console.log(`check_password("${input.readUtf8String()}") → forcé à 1`);
        return 1;  // Toujours retourner "succès"
    }, 'int', ['pointer'])
);
```

Ici, `check_password` ne s'exécutera plus jamais — elle est intégralement remplacée par notre callback qui retourne systématiquement `1`. C'est l'équivalent d'un patching binaire, mais sans modifier le fichier sur disque et avec la possibilité de conserver une logique conditionnelle.

On conserve une référence à la fonction originale via `NativeFunction` au cas où on voudrait l'appeler depuis notre remplacement :

```javascript
Interceptor.replace(check_addr,
    new NativeCallback(function (input) {
        const str = input.readUtf8String();

        // Appeler la fonction originale pour les inputs "normaux"
        if (str.startsWith("admin_")) {
            console.log("Bypass admin activé");
            return 1;
        }
        // Sinon, laisser la vérification originale se faire
        return orig_check(input);
    }, 'int', ['pointer'])
);
```

> ⚠️ `Interceptor.replace` est plus fragile que `Interceptor.attach`. Si la signature (types et nombre d'arguments) ne correspond pas exactement à la fonction originale, le comportement est indéfini — crash probable. Vérifiez toujours la signature via Ghidra ou le décompilateur avant de remplacer une fonction.

---

## `NativeFunction` : appeler des fonctions natives depuis JavaScript

L'objet `NativeFunction` permet d'appeler n'importe quelle fonction du processus cible depuis votre code JavaScript, comme si vous appeliez une fonction C :

```javascript
const puts = new NativeFunction(
    Module.findExportByName(null, "puts"),
    'int',          // type de retour
    ['pointer']     // types des arguments
);

// Allouer une chaîne en mémoire et la passer à puts
const msg = Memory.allocUtf8String("Message injecté par Frida !");  
puts(msg);  
```

Ce code alloue une chaîne dans le heap du processus cible, puis appelle la vraie fonction `puts` de la libc pour l'afficher. Le message apparaît dans `stdout` du programme cible — pas dans la console Frida.

Les types supportés par `NativeFunction` et `NativeCallback` suivent une syntaxe simplifiée :

| Type Frida | Type C | Taille |  
|---|---|---|  
| `'void'` | `void` | — |  
| `'int'` | `int` / `int32_t` | 32 bits |  
| `'uint'` | `unsigned int` | 32 bits |  
| `'long'` | `long` / `int64_t` | 64 bits |  
| `'pointer'` | `void *`, `char *`, tout pointeur | 64 bits |  
| `'float'` | `float` | 32 bits |  
| `'double'` | `double` | 64 bits |

---

## Robustesse : gérer les erreurs dans les hooks

Un hook qui lève une exception JavaScript non capturée sera silencieusement désactivé par Frida. Le processus cible continue de tourner, mais votre hook ne s'exécute plus — et si vous n'observez pas la console, vous ne saurez pas pourquoi vos logs ont cessé.

**Règle de survie** : toujours entourer le corps des hooks d'un `try/catch` dans les scripts de production.

```javascript
Interceptor.attach(addr, {
    onEnter(args) {
        try {
            const s = args[0].readUtf8String();
            console.log(`arg0 = "${s}"`);
        } catch (e) {
            console.log(`[!] Erreur dans onEnter : ${e.message}`);
            console.log(`    args[0] = ${args[0]}`);
        }
    }
});
```

Les erreurs les plus courantes sont les tentatives de lecture de mémoire non mappée (`readUtf8String` sur un pointeur `NULL` ou invalide), et les problèmes d'encodage (buffer binaire lu comme UTF-8).

---

## Ce qu'il faut retenir

- `Interceptor.attach(adresse, {onEnter, onLeave})` est la fonction centrale du hooking Frida. Elle ne modifie pas l'exécution de la fonction cible, mais permet d'observer et d'altérer ses arguments et sa valeur de retour.  
- Pour les fonctions **exportées** (libc, bibliothèques), on résout l'adresse par nom avec `Module.findExportByName`.  
- Pour les fonctions **internes** d'un binaire (surtout strippé), on calcule `base + offset` à partir de l'offset trouvé dans Ghidra ou objdump.  
- En **C++**, `args[0]` est le pointeur `this` implicite — les arguments explicites commencent à `args[1]`. Les noms de fonctions sont manglés.  
- `this.returnAddress` et `Thread.backtrace()` permettent de filtrer les appels par appelant.  
- `Interceptor.replace` remplace intégralement une fonction par un callback JavaScript.  
- `NativeFunction` permet d'appeler n'importe quelle fonction du processus cible depuis JavaScript.  
- Toujours protéger les hooks avec `try/catch` pour éviter les désactivations silencieuses.

---

> **Prochaine section** : 13.4 — Intercepter les appels à `malloc`, `free`, `open`, fonctions customs — nous appliquerons ces techniques de hooking à des scénarios concrets d'interception d'allocations mémoire, d'I/O fichier et de fonctions applicatives.

⏭️ [Intercepter les appels à `malloc`, `free`, `open`, fonctions customs](/13-frida/04-intercepter-appels.md)
