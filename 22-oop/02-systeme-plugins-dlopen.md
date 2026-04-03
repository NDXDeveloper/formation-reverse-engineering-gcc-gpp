🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 22.2 — RE d'un système de plugins (chargement dynamique `.so` via `dlopen`/`dlsym`)

> 🛠️ **Outils utilisés** : `ltrace`, `strace`, GDB (+ GEF/pwndbg), Ghidra, Frida, `readelf`, `nm`  
> 📦 **Binaires** : `oop_O0`, `oop_O2_strip`, `plugins/plugin_alpha.so`, `plugins/plugin_beta.so`  
> 📚 **Prérequis** : Section 22.1 (vtables et hiérarchie), Chapitre 5 (`ltrace`/`strace`), Chapitre 11 (GDB), Chapitre 13 (Frida)

---

## Introduction

Dans la section précédente, nous avons reconstruit la hiérarchie de classes et localisé les vtables. Mais une question reste en suspens : les classes `Rot13Processor` et `XorCipherProcessor` n'apparaissent nulle part dans l'exécutable principal. Elles vivent dans des bibliothèques partagées (`.so`) chargées **à l'exécution**, pas au link time. L'analyse statique de l'exécutable seul ne les révèle pas.

Ce pattern — une application hôte qui charge des modules externes via `dlopen` / `dlsym` — est extrêmement courant dans le monde réel. On le retrouve dans les navigateurs (plugins), les serveurs web (modules Apache/Nginx), les moteurs de jeu (mods), les frameworks audio (plugins VST/LV2), et dans de nombreux malwares qui téléchargent et chargent des composants additionnels à la volée.

Pour le reverse engineer, un système de plugins pose des défis spécifiques :

- **Le code n'est pas présent dans l'exécutable analysé** — il faut identifier *quels* fichiers sont chargés et *où* les trouver.  
- **Les symboles sont résolus à l'exécution** — les appels passent par des pointeurs obtenus via `dlsym`, pas par la PLT.  
- **L'interface du plugin est implicite** — le contrat entre l'hôte et le plugin (quels symboles exporter, quelle classe instancier) n'est documenté nulle part dans le binaire.

Cette section vous apprend à détecter, tracer et comprendre un système de plugins, du triage statique jusqu'au hooking dynamique.

---

## Les API `dl*` : rappel technique

Avant de plonger dans l'analyse, rappelons les quatre fonctions de `libdl` que vous rencontrerez systématiquement.

**`dlopen(const char *filename, int flags)`** — Charge une bibliothèque partagée en mémoire. Retourne un *handle* opaque (`void*`) ou `NULL` en cas d'erreur. Les flags courants sont `RTLD_NOW` (résolution immédiate de tous les symboles) et `RTLD_LAZY` (résolution à la demande). Le flag choisi influence le comportement observable au chargement.

**`dlsym(void *handle, const char *symbol)`** — Recherche un symbole par son nom dans la bibliothèque chargée. Retourne un pointeur vers le symbole (`void*`) ou `NULL` si le symbole n'existe pas. C'est ici que le nom du symbole factory apparaît **en clair** dans le binaire — un indice crucial pour le RE.

**`dlclose(void *handle)`** — Décharge la bibliothèque. Décrémente un compteur de références ; la bibliothèque n'est réellement déchargée que lorsque le compteur atteint zéro.

**`dlerror(void)`** — Retourne une chaîne décrivant la dernière erreur survenue dans les fonctions `dl*`. Souvent appelée juste après un `dlopen` ou `dlsym` qui a échoué.

Ces quatre fonctions sont dans `libdl.so` et apparaissent dans la PLT de l'exécutable. Elles sont donc visibles avec `objdump -d` (entrées `dlopen@plt`, `dlsym@plt`, etc.) même sur un binaire strippé.

---

## Étape 1 — Détection statique du mécanisme de plugins

### 1.1 — Repérer les imports `dl*`

La première question face à un binaire inconnu est : « utilise-t-il du chargement dynamique ? ». La réponse est dans la table d'imports.

```bash
$ objdump -T oop_O0 | grep -i dl
0000000000000000      DF *UND*  0000000000000000  GLIBC_2.34  dlopen
0000000000000000      DF *UND*  0000000000000000  GLIBC_2.34  dlsym
0000000000000000      DF *UND*  0000000000000000  GLIBC_2.34  dlclose
0000000000000000      DF *UND*  0000000000000000  GLIBC_2.34  dlerror
```

Quatre imports `dl*` — le binaire charge des modules dynamiquement. Sur un binaire strippé, cette information est toujours disponible car les symboles dynamiques (`.dynsym`) ne sont pas supprimés par `strip`.

Alternativement, avec `readelf` :

```bash
$ readelf -d oop_O2_strip | grep NEEDED
 0x0000000000000001 (NEEDED)  Shared library: [libdl.so.2]
 0x0000000000000001 (NEEDED)  Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)  Shared library: [libc.so.6]
```

La dépendance à `libdl.so.2` confirme l'utilisation des API de chargement dynamique.

> 💡 Sur les versions récentes de glibc (≥ 2.34), les fonctions `dl*` sont intégrées directement dans `libc.so.6` et `libdl.so` n'est plus qu'un stub de compatibilité. Ne vous fiez pas à l'absence de `libdl.so` dans les NEEDED pour conclure qu'il n'y a pas de chargement dynamique — vérifiez toujours les symboles importés.

### 1.2 — Trouver les noms de symboles recherchés

`dlsym` prend un nom de symbole en argument sous forme de chaîne. Ce nom est stocké en clair dans `.rodata` :

```bash
$ strings oop_O0 | grep -i 'processor\|plugin\|create\|destroy\|factory'
create_processor  
destroy_processor  
[Pipeline] loading plugin: %s
[Pipeline] dlopen error: %s
[Pipeline] missing symbols in %s
./plugins
.so
```

Vous obtenez immédiatement :

- Les **noms des symboles factory** : `create_processor` et `destroy_processor`.  
- Le **répertoire de recherche par défaut** : `./plugins`.  
- Le **suffixe filtré** : `.so`.  
- Des **messages de diagnostic** qui révèlent la logique de chargement.

Sur un binaire strippé, ces chaînes sont toujours présentes — elles font partie de `.rodata` et sont indispensables au fonctionnement du programme.

### 1.3 — Le pattern `opendir` / `readdir` : découverte automatique de plugins

Notre application ne charge pas un plugin nommé en dur — elle scanne un répertoire. Ce pattern se détecte par les imports :

```bash
$ objdump -T oop_O0 | grep -E 'opendir|readdir|closedir'
0000000000000000      DF *UND*  ...  opendir
0000000000000000      DF *UND*  ...  readdir
0000000000000000      DF *UND*  ...  closedir
```

La présence conjointe de `opendir`/`readdir` et de `dlopen` indique un mécanisme de **découverte automatique** : l'application liste les fichiers d'un répertoire, filtre par extension, et charge chaque `.so` trouvé. C'est un pattern classique des architectures à plugins.

---

## Étape 2 — Analyse statique du loader de plugins dans Ghidra

### 2.1 — Localiser la fonction de chargement

Dans Ghidra, cherchez les cross-references vers `dlopen` dans la PLT. Cliquez sur `dlopen` dans le Symbol Tree (sous *Imports* ou *External*), puis `Ctrl+Shift+F` (References To).

Vous trouverez un ou plusieurs sites d'appel. Sur notre binaire, il y en a un seul, dans la méthode `Pipeline::load_plugin()`. Avec symboles, Ghidra l'identifie directement. Sans symboles, vous verrez une `FUN_XXXXXXXX` — renommez-la d'après le contexte.

### 2.2 — Décortiquer le flux de chargement

En lisant le décompilé (ou le désassemblage) de cette fonction, vous identifierez le flux suivant :

```
load_plugin(path):
  1. handle = dlopen(path, RTLD_NOW)
     └── si NULL → log erreur via dlerror(), retourner false

  2. create_fn = dlsym(handle, "create_processor")
     destroy_fn = dlsym(handle, "destroy_processor")
     └── si l'un est NULL → log erreur, dlclose(handle), retourner false

  3. instance = create_fn(next_id++)
     └── si NULL → log erreur, dlclose(handle), retourner false

  4. Stocker {handle, create_fn, destroy_fn, instance} dans un vecteur
     Ajouter instance au vecteur global de Processor*

  5. Retourner true
```

Chaque étape est identifiable par les appels aux fonctions `dl*` et par les vérifications de `NULL` qui suivent. En désassemblage, le pattern est très régulier :

```asm
; Étape 1 — dlopen
lea    rdi, [rbp-0x110]       ; path (variable locale ou argument)  
mov    esi, 0x2               ; RTLD_NOW = 2  
call   dlopen@plt  
test   rax, rax  
je     .error_dlopen          ; saut si handle == NULL  
mov    [rbp-0x08], rax        ; sauvegarder le handle  

; Étape 2 — dlsym pour create_processor
mov    rdi, rax               ; handle  
lea    rsi, [rip+0x...]       ; → "create_processor"  
call   dlsym@plt  
test   rax, rax  
je     .error_dlsym  
mov    [rbp-0x10], rax        ; sauvegarder le pointeur de fonction  

; Étape 2bis — dlsym pour destroy_processor
mov    rdi, [rbp-0x08]        ; handle  
lea    rsi, [rip+0x...]       ; → "destroy_processor"  
call   dlsym@plt  
test   rax, rax  
je     .error_dlsym  
mov    [rbp-0x18], rax  
```

> 💡 **Indice important** : la chaîne passée en second argument de `dlsym` est toujours un littéral dans `.rodata`. Ghidra l'affiche directement dans le décompilé. C'est la preuve formelle du contrat d'interface entre l'hôte et le plugin.

### 2.3 — Comprendre le contrat d'interface

À ce stade, vous avez reconstitué le contrat du plugin sans avoir les sources :

- Le plugin doit exporter un symbole `extern "C"` nommé `create_processor` qui prend un `uint32_t` et retourne un `Processor*`.  
- Le plugin doit exporter un symbole `extern "C"` nommé `destroy_processor` qui prend un `Processor*` et ne retourne rien.  
- L'objet retourné par `create_processor` est manipulé exclusivement à travers l'interface `Processor` (dispatch virtuel).

C'est exactement l'information dont vous aurez besoin à la section 22.4 (et au checkpoint) pour écrire votre propre plugin compatible.

### 2.4 — Analyser `load_plugins_from_dir`

La méthode qui appelle `load_plugin` en boucle suit ce pattern :

```
load_plugins_from_dir(dir):
  d = opendir(dir)
  └── si NULL → retourner 0

  tant que (entry = readdir(d)) != NULL:
      si entry->d_name se termine par ".so":
          construire le chemin complet (dir + "/" + d_name)
          load_plugin(chemin_complet)
          incrémenter compteur

  closedir(d)
  retourner compteur
```

Le test d'extension `.so` se traduit généralement par un calcul de `strlen` suivi d'un `strcmp` ou `memcmp` sur les 3 derniers caractères. En `-O2`, GCC peut optimiser cette vérification en une comparaison directe sur 3 octets ou même un `cmp` sur un entier de 32 bits masqué.

---

## Étape 3 — Traçage dynamique avec `ltrace`

L'analyse statique vous a donné la structure du loader. L'analyse dynamique vous montre ce qui se passe **réellement** à l'exécution.

### 3.1 — `ltrace` : tracer les appels à `libdl`

`ltrace` intercepte les appels aux bibliothèques partagées. C'est l'outil idéal pour observer `dlopen`/`dlsym` en action :

```bash
$ ltrace -e dlopen,dlsym,dlclose ./oop_O0 -p ./plugins "Hello RE"
```

Sortie typique :

```
dlopen("./plugins/plugin_alpha.so", 2)          = 0x5555557a4000  
dlsym(0x5555557a4000, "create_processor")       = 0x7ffff7fb6200  
dlsym(0x5555557a4000, "destroy_processor")      = 0x7ffff7fb6280  
dlopen("./plugins/plugin_beta.so", 2)           = 0x5555557b8000  
dlsym(0x5555557b8000, "create_processor")       = 0x7ffff7daa180  
dlsym(0x5555557b8000, "destroy_processor")      = 0x7ffff7daa210  
...
dlclose(0x5555557b8000)                         = 0  
dlclose(0x5555557a4000)                         = 0  
```

Vous obtenez en une commande :

- Les **chemins exacts** des plugins chargés.  
- Le **flag** passé à `dlopen` (2 = `RTLD_NOW`).  
- Les **noms de symboles** recherchés par `dlsym`.  
- Les **adresses** retournées pour chaque symbole — ce sont les adresses des fonctions factory dans l'espace mémoire du processus.  
- L'**ordre de chargement** et de déchargement.

### 3.2 — `strace` : voir les accès au système de fichiers

`strace` montre les appels système sous-jacents. Combiné avec `ltrace`, il révèle quels fichiers sont effectivement ouverts :

```bash
$ strace -e openat,mmap ./oop_O0 -p ./plugins "Hello RE" 2>&1 | grep plugin
```

```
openat(AT_FDCWD, "./plugins", O_RDONLY|O_NONBLOCK|O_DIRECTORY) = 3  
openat(AT_FDCWD, "./plugins/plugin_alpha.so", O_RDONLY|O_CLOEXEC) = 4  
mmap(NULL, 16384, PROT_READ, MAP_PRIVATE, 4, 0) = 0x7ffff7fb0000  
openat(AT_FDCWD, "./plugins/plugin_beta.so", O_RDONLY|O_CLOEXEC)  = 4  
mmap(NULL, 16384, PROT_READ, MAP_PRIVATE, 4, 0) = 0x7ffff7da0000  
```

On voit l'`openat` sur le répertoire (pour `opendir`), puis l'ouverture de chaque `.so` par `dlopen` (qui utilise `openat` + `mmap` en interne).

> 💡 **Contexte malware** : sur un sample suspect, `strace` est votre premier réflexe pour voir si le binaire tente de charger des modules depuis des emplacements inattendus (`/tmp`, `/dev/shm`, un chemin réseau…). Un `dlopen` sur un fichier dans `/tmp/.cache/libupdate.so` serait un signal d'alarme immédiat.

---

## Étape 4 — Traçage avec GDB : observer le chargement en temps réel

### 4.1 — Breakpoints sur `dlopen` et `dlsym`

Lancez GDB et posez des breakpoints sur les fonctions `dl*` :

```
$ gdb -q ./oop_O0
(gdb) break dlopen
(gdb) break dlsym
(gdb) run -p ./plugins "Hello RE"
```

Premier arrêt sur `dlopen` :

```
Breakpoint 1, dlopen (file=0x7fffffffd4f0 "./plugins/plugin_alpha.so", mode=2)
```

GDB vous montre directement les arguments. Le flag `mode=2` correspond à `RTLD_NOW`. Continuez avec `continue` pour atteindre le `dlsym` :

```
Breakpoint 2, dlsym (handle=0x5555557a4000, name=0x404120 "create_processor")
```

L'argument `name` est la chaîne du symbole recherché.

### 4.2 — Examiner la valeur de retour

Placez un breakpoint conditionnel après le retour de `dlsym` pour capturer le pointeur de fonction :

```
(gdb) break dlsym
(gdb) commands
> finish
> print/x $rax
> info symbol $rax
> end
(gdb) continue
```

Après le `finish`, `$rax` contient l'adresse retournée par `dlsym`. La commande `info symbol` vous indique à quel symbole cette adresse correspond :

```
$1 = 0x7ffff7fb6200
create_processor in section .text of ./plugins/plugin_alpha.so
```

### 4.3 — Suivre l'appel à la factory

Le moment clé est l'appel `instance = create_fn(next_id)`. Pour l'intercepter, il faut trouver le site d'appel indirect. Deux approches :

**Approche 1 — Breakpoint sur la factory elle-même** :

```
(gdb) break create_processor
```

Si le plugin a des symboles, GDB résout le nom directement. Sinon, utilisez l'adresse obtenue à l'étape précédente :

```
(gdb) break *0x7ffff7fb6200
```

À l'arrêt, vous êtes à l'entrée de la factory du plugin. Inspectez les arguments :

```
(gdb) print $rdi
$2 = 1                          ← l'id passé au constructeur
```

Continuez pas à pas (`step`) pour voir le `new` et le constructeur de `Rot13Processor`.

**Approche 2 — Inspecter l'objet retourné** :

Après le retour de `create_processor`, examinez l'objet :

```
(gdb) finish
(gdb) print/x $rax
$3 = 0x555555808010              ← adresse de l'objet alloué

(gdb) x/6gx $rax
0x555555808010: 0x00007ffff7fb7d00  0x0000000100000001
0x555555808020: 0x0000000000000000  0x0000000000000000
0x555555808030: 0x0000000000000000  0x0000000000000000
```

Le premier quadword (`0x00007ffff7fb7d00`) est le **vptr** — il pointe vers la vtable de `Rot13Processor` dans `plugin_alpha.so`. Examinez cette vtable :

```
(gdb) x/8gx 0x00007ffff7fb7d00
0x7ffff7fb7d00: 0x00007ffff7fb6050   ← ~Rot13Processor() (complete)
0x7ffff7fb7d08: 0x00007ffff7fb60a0   ← ~Rot13Processor() (deleting)
0x7ffff7fb7d10: 0x00007ffff7fb60f0   ← name()
0x7ffff7fb7d18: 0x00007ffff7fb6110   ← configure()
0x7ffff7fb7d20: 0x00007ffff7fb6150   ← process()
0x7ffff7fb7d28: 0x00007ffff7fb61c0   ← status()
```

Chaque entrée pointe vers une fonction dans l'espace d'adressage de `plugin_alpha.so`. Vous pouvez le vérifier avec `info symbol` sur chaque adresse.

### 4.4 — La commande `info sharedlibrary`

À tout moment, GDB peut lister les bibliothèques partagées chargées :

```
(gdb) info sharedlibrary
From                To                  Syms Read   Shared Object Library
0x00007ffff7fc1000  0x00007ffff7fe2000  Yes          /lib64/ld-linux-x86-64.so.2
0x00007ffff7f80000  0x00007ffff7fb0000  Yes          /lib/x86-64-linux-gnu/libdl.so.2
0x00007ffff7d00000  0x00007ffff7e80000  Yes          /lib/x86-64-linux-gnu/libc.so.6
0x00007ffff7fb0000  0x00007ffff7fb8000  Yes          ./plugins/plugin_alpha.so
0x00007ffff7da0000  0x00007ffff7dac000  Yes          ./plugins/plugin_beta.so
```

Les plugins apparaissent dans la liste une fois chargés par `dlopen`. Si vous posez le breakpoint avant le `dlopen`, ils ne sont pas encore visibles — et GDB ne peut pas résoudre leurs symboles. C'est pourquoi le breakpoint sur `dlopen` suivi d'un `finish` est nécessaire pour pouvoir ensuite travailler avec les symboles du plugin.

---

## Étape 5 — Hooking avec Frida : interception en profondeur

Frida offre une approche plus souple que GDB pour tracer un système de plugins. L'idée est d'intercepter `dlopen` et `dlsym` pour logger automatiquement tout le processus de chargement, puis de hooker les fonctions factory pour inspecter les objets créés.

### 5.1 — Script de base : tracer `dlopen` et `dlsym`

```javascript
// frida_trace_plugins.js
// Usage : frida -l frida_trace_plugins.js -- ./oop_O0 -p ./plugins "Hello RE"

Interceptor.attach(Module.findExportByName(null, "dlopen"), {
    onEnter: function(args) {
        this.path = args[0].readUtf8String();
        this.flags = args[1].toInt32();
        console.log("[dlopen] path=" + this.path + " flags=" + this.flags);
    },
    onLeave: function(retval) {
        console.log("[dlopen] handle=" + retval);
        if (retval.isNull()) {
            // Appeler dlerror pour obtenir le message
            var dlerror = new NativeFunction(
                Module.findExportByName(null, "dlerror"), 'pointer', []);
            var err = dlerror().readUtf8String();
            console.log("[dlopen] ERROR: " + err);
        }
    }
});

Interceptor.attach(Module.findExportByName(null, "dlsym"), {
    onEnter: function(args) {
        this.handle = args[0];
        this.symbol = args[1].readUtf8String();
        console.log("[dlsym] handle=" + this.handle +
                    " symbol=\"" + this.symbol + "\"");
    },
    onLeave: function(retval) {
        console.log("[dlsym] → " + retval);

        // Si c'est la factory, hooker dynamiquement le pointeur retourné
        if (this.symbol === "create_processor" && !retval.isNull()) {
            hookFactory(retval, this.handle);
        }
    }
});

function hookFactory(fnPtr, dlHandle) {
    Interceptor.attach(fnPtr, {
        onEnter: function(args) {
            this.id = args[0].toInt32();
            console.log("[create_processor] id=" + this.id);
        },
        onLeave: function(retval) {
            if (retval.isNull()) {
                console.log("[create_processor] returned NULL");
                return;
            }
            console.log("[create_processor] object at " + retval);

            // Lire le vptr (premier quadword de l'objet)
            var vptr = retval.readPointer();
            console.log("[create_processor] vptr = " + vptr);

            // Lire les 6 premières entrées de la vtable
            for (var i = 0; i < 6; i++) {
                var entry = vptr.add(i * Process.pointerSize).readPointer();
                var info = DebugSymbol.fromAddress(entry);
                console.log("  vtable[" + i + "] = " + entry +
                            " (" + info.name + ")");
            }
        }
    });
}
```

### 5.2 — Sortie du script Frida

```
[dlopen] path=./plugins/plugin_alpha.so flags=2
[dlopen] handle=0x5555557a4000
[dlsym] handle=0x5555557a4000 symbol="create_processor"
[dlsym] → 0x7ffff7fb6200
[dlsym] handle=0x5555557a4000 symbol="destroy_processor"
[dlsym] → 0x7ffff7fb6280
[create_processor] id=1
[create_processor] object at 0x555555808010
[create_processor] vptr = 0x7ffff7fb7d00
  vtable[0] = 0x7ffff7fb6050 (Rot13Processor::~Rot13Processor())
  vtable[1] = 0x7ffff7fb60a0 (Rot13Processor::~Rot13Processor())
  vtable[2] = 0x7ffff7fb60f0 (Rot13Processor::name() const)
  vtable[3] = 0x7ffff7fb6110 (Rot13Processor::configure())
  vtable[4] = 0x7ffff7fb6150 (Rot13Processor::process())
  vtable[5] = 0x7ffff7fb61c0 (Rot13Processor::status() const)
[dlopen] path=./plugins/plugin_beta.so flags=2
[dlopen] handle=0x5555557b8000
...
```

En un seul run, Frida vous donne :

- Chaque plugin chargé, avec son handle.  
- Les symboles recherchés et leurs adresses résolues.  
- L'objet créé par chaque factory, son vptr, et la vtable complète avec les noms des méthodes (si les symboles du plugin sont présents).

### 5.3 — Hooking des méthodes virtuelles du plugin

Une fois la vtable connue, vous pouvez hooker individuellement les méthodes du plugin. Par exemple, pour intercepter `process()` sur chaque plugin :

```javascript
function hookProcessMethod(vptr, className) {
    // process() est à l'index 4 de la vtable (après 2 dtors, name, configure)
    var processAddr = vptr.add(4 * Process.pointerSize).readPointer();

    Interceptor.attach(processAddr, {
        onEnter: function(args) {
            // args[0] = this, args[1] = input, args[2] = in_len
            var input = args[1].readUtf8String();
            var len = args[2].toInt32();
            console.log("[" + className + "::process] input=\"" +
                        input + "\" len=" + len);
        },
        onLeave: function(retval) {
            console.log("[" + className + "::process] returned " +
                        retval.toInt32());
        }
    });
}
```

Cette technique est particulièrement puissante sur les binaires strippés : vous n'avez besoin d'aucun symbole, seulement du vptr et des offsets dans la vtable.

---

## Étape 6 — Analyse statique des plugins dans Ghidra

### 6.1 — Importer un `.so` séparément

Chaque plugin doit être importé comme un projet Ghidra distinct (ou dans le même projet, mais comme un binaire séparé). Lors de l'import, Ghidra détecte automatiquement le format ELF shared object.

Après l'analyse automatique, le point d'entrée de votre investigation est le symbole `create_processor` — visible dans le Symbol Tree sous *Functions* ou *Exports*.

### 6.2 — De la factory à la vtable

Le décompilé de `create_processor` dans `plugin_alpha.so` ressemblera à :

```c
Processor * create_processor(uint32_t id) {
    Rot13Processor *obj = (Rot13Processor *)operator.new(0x28);
    Rot13Processor::Rot13Processor(obj, id);
    return obj;
}
```

La taille passée à `operator new` (`0x28` = 40 octets) vous donne le `sizeof(Rot13Processor)`. En la comparant au `sizeof(Processor)` de base (24 octets reconstitué en section 22.1), vous savez que `Rot13Processor` ajoute 16 octets de données propres.

En entrant dans le constructeur, vous verrez l'assignation du vptr :

```asm
lea    rax, [rip + vtable_for_Rot13Processor + 0x10]  
mov    QWORD PTR [rdi], rax  
```

Suivez cette adresse pour atteindre la vtable et identifier toutes les méthodes du plugin.

### 6.3 — Relier le plugin à l'hôte

Le typeinfo de `Rot13Processor` contient une référence au typeinfo de `Processor` :

```
typeinfo for Rot13Processor:
  [0x00]  ptr → __si_class_type_info
  [0x08]  ptr → "15Rot13Processor"
  [0x10]  RELOCATION → typeinfo for Processor (dans l'exécutable)
```

Dans Ghidra, cette relocation apparaît dans la section `.rela.dyn` du plugin. L'entrée pointe vers un symbole externe (`typeinfo for Processor`), ce qui confirme que `Rot13Processor` hérite de `Processor` et que la résolution se fait au chargement via le linker dynamique.

Vérifiez avec `readelf` :

```bash
$ readelf -r plugins/plugin_alpha.so | grep typeinfo
0000000000003d90  R_X86_64_64  0000000000000000 _ZTI9Processor + 0
```

Le symbole `_ZTI9Processor` (`typeinfo for Processor`) est une relocation externe — le linker dynamique la résoudra vers l'adresse du typeinfo dans l'exécutable principal (d'où l'importance du flag `-rdynamic` à la compilation de l'hôte).

---

## Étape 7 — Reconstituer le protocole complet du système de plugins

En combinant toutes les observations des étapes précédentes, vous pouvez maintenant documenter le protocole complet :

```
┌─────────────────────────────────────────────────────────────────┐
│                  PROTOCOLE DE PLUGIN                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. DÉCOUVERTE                                                  │
│     L'hôte scanne le répertoire ./plugins/ (configurable -p)    │
│     et filtre les fichiers par extension .so                    │
│                                                                 │
│  2. CHARGEMENT                                                  │
│     dlopen(path, RTLD_NOW)                                      │
│     → Le .so est mappé en mémoire, ses relocations résolues     │
│                                                                 │
│  3. RÉSOLUTION DES SYMBOLES                                     │
│     dlsym(handle, "create_processor")  → create_func_t          │
│     dlsym(handle, "destroy_processor") → destroy_func_t         │
│     → Deux symboles extern "C" obligatoires                     │
│                                                                 │
│  4. INSTANCIATION                                               │
│     Processor* obj = create_processor(id)                       │
│     → Le plugin alloue et construit un objet dérivé             │
│     → L'objet est retourné comme Processor*                     │
│                                                                 │
│  5. UTILISATION                                                 │
│     L'hôte appelle obj->name(), obj->process(), etc.            │
│     → Dispatch virtuel via la vtable du plugin                  │
│     → L'hôte ne connaît jamais le type concret                  │
│                                                                 │
│  6. DESTRUCTION                                                 │
│     destroy_processor(obj)                                      │
│     → Le plugin libère l'objet (delete)                         │
│     dlclose(handle)                                             │
│     → Le .so est déchargé                                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

Ce schéma est le livrable principal de cette section. Avec lui, vous avez tout ce qu'il faut pour :

- Comprendre comment l'hôte interagit avec les plugins.  
- Écrire un plugin compatible (section 22.4 et checkpoint).  
- Identifier un comportement suspect si un plugin fait plus que ce que l'interface prévoit (contexte malware).

---

## Cas particuliers et pièges courants

**`RTLD_LAZY` vs `RTLD_NOW`** — Avec `RTLD_LAZY`, les symboles ne sont résolus qu'au premier appel. Cela signifie qu'un `dlopen` peut réussir même si le plugin a des dépendances manquantes — l'erreur ne surviendra qu'à l'exécution. En RE, si vous voyez `RTLD_LAZY` (flag = 1), sachez que le `dlopen` seul ne garantit pas que le plugin est valide.

**Symboles manglés comme factory** — Certaines applications utilisent des noms de symboles C++ manglés comme points d'entrée de plugins (au lieu de `extern "C"`). Le `dlsym` contiendra alors une chaîne comme `_ZN6PluginC1Ev`. C'est plus rare car fragile (le mangling dépend du compilateur et de l'ABI), mais ça existe.

**`dlmopen` et espaces de noms** — Sur certaines architectures de plugins complexes, vous rencontrerez `dlmopen` qui charge la bibliothèque dans un espace de noms de linker séparé. Les symboles ne sont pas partagés entre espaces, ce qui complique la résolution RTTI. En RE, la démarche reste la même mais les adresses de vtable entre l'hôte et le plugin ne partagent plus le même typeinfo.

**Plugins qui chargent d'autres plugins** — Un plugin peut lui-même appeler `dlopen`. C'est fréquent dans les architectures en couches (un plugin de codec qui charge un sous-plugin de décodage matériel, par exemple). Frida avec le hook récursif montré ci-dessus capturera ces chargements imbriqués automatiquement.

---

## Résumé

L'analyse d'un système de plugins `dlopen`/`dlsym` suit un processus en trois temps. D'abord, la **détection statique** : repérer les imports `dl*`, les chaînes de symboles factory dans `.rodata`, et le pattern `opendir`/`readdir`. Ensuite, la **reconstruction statique** dans Ghidra : suivre le flux depuis `dlopen` jusqu'à l'appel de la factory, identifier le contrat d'interface et relier le plugin à la hiérarchie de classes de l'hôte via les relocations RTTI. Enfin, la **validation dynamique** : `ltrace` pour une vue d'ensemble rapide, GDB pour inspecter les objets et les vtables en mémoire, Frida pour un traçage automatisé et le hooking des méthodes virtuelles du plugin.

Le résultat de cette analyse est la compréhension complète du protocole de plugin : comment il est découvert, chargé, instancié, utilisé et détruit. Cette compréhension est la base nécessaire pour la section suivante, où nous plongerons dans les détails du dispatch virtuel qui permet à l'hôte d'appeler les méthodes du plugin sans connaître son type concret.

---


⏭️ [Comprendre le dispatch virtuel : de la vtable à l'appel de méthode](/22-oop/03-dispatch-virtuel.md)
