🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 13.7 — Cas pratique : contourner une vérification de licence

> 🧰 **Outils utilisés** : `frida`, `frida-trace`, Python 3 + module `frida`, Ghidra (résultats d'analyse statique)  
> 📦 **Binaire utilisé** : `binaries/ch13-keygenme/keygenme_O0`  
> 📖 **Prérequis** : Toutes les sections précédentes du chapitre 13

---

## Objectif

Ce cas pratique mobilise l'ensemble des techniques Frida vues dans ce chapitre — de la reconnaissance avec `frida-trace` jusqu'au contournement automatisé en Python, en passant par le code coverage avec Stalker. L'objectif n'est pas seulement de contourner la vérification : c'est de montrer une **méthodologie reproductible** applicable à n'importe quel binaire protégé.

Le binaire `keygenme_O0` est un crackme compilé avec GCC sans optimisations (`-O0`) et avec symboles de débogage. Il demande une clé à l'utilisateur et affiche un message de succès ou d'échec. Nous allons le traiter comme si nous ne disposions que du binaire — sans regarder le code source.

Le parcours se déroule en cinq phases, qui reflètent la méthodologie naturelle du reverse engineer face à une protection logicielle :

1. **Reconnaissance** — identifier les fonctions impliquées dans la vérification.  
2. **Localisation** — cibler précisément la routine de décision.  
3. **Compréhension** — analyser la logique de vérification.  
4. **Contournement** — modifier le comportement pour accepter n'importe quelle clé.  
5. **Extraction** — récupérer la clé valide sans modifier le programme.

---

## Phase 1 — Reconnaissance avec `frida-trace`

La première question face à un binaire inconnu est toujours : *quelles fonctions de bibliothèque utilise-t-il, et dans quel ordre ?* `frida-trace` donne la réponse en quelques secondes.

### Tracer les fonctions de chaîne et d'I/O

Un crackme qui vérifie une clé utilise nécessairement des fonctions de comparaison (`strcmp`, `strncmp`, `memcmp`) et des fonctions d'entrée/sortie (`printf`, `puts`, `scanf`, `fgets`). Commençons par tracer cette famille :

```bash
frida-trace -f ./keygenme_O0 -i "strcmp" -i "strncmp" -i "memcmp" \
            -i "printf" -i "puts" -i "scanf" -i "fgets" -i "strlen"
```

Le programme démarre, affiche son prompt, et attend une saisie. Tapons une clé arbitraire — `AAAA` :

```
Instrumenting...
           /* TID 0x7a3b */
   142 ms  puts("=== KeyGenMe v1.0 ===")
   142 ms  puts("Entrez la clé de licence :")
   142 ms  scanf()
  3891 ms  strlen("AAAA")
  3891 ms  strcmp("AAAA", "GCC-RE-2024-XPRO")
  3891 ms  puts("Clé invalide. Accès refusé.")
```

En une seule commande, sans breakpoint, sans Ghidra, sans la moindre ligne de JavaScript, la trace révèle :

- La clé attendue : `"GCC-RE-2024-XPRO"` (second argument de `strcmp`).  
- Le flux de vérification : `scanf` lit l'input, `strlen` vérifie sa longueur, `strcmp` compare avec la clé hardcodée.  
- Le message d'échec : `"Clé invalide. Accès refusé."`.

Pour un binaire `-O0` avec symboles, le travail est déjà terminé. Mais dans la réalité, les choses sont rarement aussi simples — la clé peut être calculée dynamiquement, la comparaison peut être custom, le binaire peut être strippé et optimisé. Poursuivons la méthodologie comme si la clé n'avait pas été visible directement.

### Élargir la reconnaissance

Traçons cette fois toutes les fonctions du binaire principal pour voir la structure des appels internes :

```bash
frida-trace -f ./keygenme_O0 -I "keygenme_O0"
```

```
           /* TID 0x7a3b */
   142 ms  main()
   142 ms     | print_banner()
   142 ms     | read_input()
  3891 ms     | validate_key()
  3891 ms     |    | compute_hash()
  3891 ms     |    | check_hash()
  3891 ms     | print_result()
```

La hiérarchie est claire : `main` appelle `validate_key`, qui appelle `compute_hash` puis `check_hash`. C'est `validate_key` qui encapsule la logique de décision.

---

## Phase 2 — Localisation de la routine de décision

La reconnaissance a identifié `validate_key` comme la fonction pivot. Confirmons avec un hook Frida qui observe son comportement :

```javascript
// recon_validate.js
'use strict';

const validate = Module.findExportByName(null, "validate_key");

if (validate) {
    Interceptor.attach(validate, {
        onEnter(args) {
            // Hypothèse : le premier argument est la chaîne saisie par l'utilisateur
            try {
                this.input = args[0].readUtf8String();
                console.log(`\n[*] validate_key("${this.input}")`);
            } catch (e) {
                console.log(`\n[*] validate_key(${args[0]})`);
            }
        },
        onLeave(retval) {
            const result = retval.toInt32();
            console.log(`[*] validate_key() → ${result}`);
            console.log(`    Interprétation : ${result === 1 ? 'VALIDE' : 'INVALIDE'}`);
        }
    });
} else {
    console.log("[!] Symbole validate_key non trouvé — binaire strippé ?");
    // Fallback : utiliser l'offset Ghidra (voir Phase 2b)
}
```

```bash
frida -f ./keygenme_O0 -l recon_validate.js --no-pause
```

Résultat avec l'input `test123` :

```
[*] validate_key("test123")
[*] validate_key() → 0
    Interprétation : INVALIDE
```

Confirmation : `validate_key` retourne `0` pour un input incorrect. L'hypothèse naturelle est que `1` correspond à un input valide. L'étape suivante est de vérifier cette hypothèse en forçant la valeur de retour.

### Phase 2b — Si le binaire était strippé

Sur un binaire strippé (`keygenme_O2_strip`), le symbole `validate_key` n'existe pas. Il faut retrouver l'adresse par d'autres moyens. Voici l'approche combinée Stalker + filtrage par appelant :

```javascript
// Poser un hook sur strcmp et remonter à l'appelant
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        if (this.returnAddress.compare(modBase) >= 0 &&
            this.returnAddress.compare(modEnd) < 0) {

            const caller = this.returnAddress;
            const offset = caller.sub(modBase);
            console.log(`[*] strcmp() appelé depuis le binaire principal`);
            console.log(`    Adresse de retour : ${caller} (offset 0x${offset.toString(16)})`);
            console.log(`    Backtrace :`);
            console.log(Thread.backtrace(this.context, Backtracer.ACCURATE)
                .map(DebugSymbol.fromAddress).join('\n    '));
        }
    }
});
```

La backtrace révèle la chaîne d'appels qui mène au `strcmp`, et donc l'offset de la fonction de vérification. On l'utilise ensuite avec `base.add(offset)` comme vu en section 13.3.

---

## Phase 3 — Compréhension avec Stalker

Avant de contourner, cherchons à comprendre la logique de vérification. Activons Stalker pendant l'exécution de `validate_key` pour voir quels blocs de code sont traversés, et comparons deux exécutions.

### Couverture comparative

```javascript
// coverage_compare.js
'use strict';

const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  
const validate = Module.findExportByName(null, "validate_key");  
const covered = new Set();  

Interceptor.attach(validate, {
    onEnter(args) {
        this.input = args[0].readUtf8String();
        this.tid = Process.getCurrentThreadId();

        Stalker.follow(this.tid, {
            transform(iterator) {
                let insn = iterator.next();
                const addr = insn.address;
                if (addr.compare(modBase) >= 0 && addr.compare(modEnd) < 0) {
                    covered.add(addr.sub(modBase).toInt32());
                }
                do { iterator.keep(); } while ((insn = iterator.next()) !== null);
            }
        });
    },
    onLeave(retval) {
        Stalker.unfollow(this.tid);
        Stalker.flush();
        Stalker.garbageCollect();

        send({
            input: this.input,
            result: retval.toInt32(),
            blocks: Array.from(covered)
        });
        covered.clear();
    }
});
```

Le script Python collecte les deux traces et les compare :

```python
import frida, sys, time, json

results = []

def on_message(msg, data):
    if msg['type'] == 'send':
        results.append(msg['payload'])

def run_with_input(binary, user_input):
    pid = frida.spawn([binary])
    session = frida.attach(pid)
    with open("coverage_compare.js") as f:
        code = f.read()
    script = session.create_script(code)
    script.on('message', on_message)
    script.load()
    frida.resume(pid)
    time.sleep(1)

    # Envoyer l'input via stdin (le programme attend scanf)
    import subprocess
    # Alternative : utiliser frida.spawn avec stdio='pipe'
    time.sleep(2)
    session.detach()

# Comparer les résultats
if len(results) >= 2:
    blocks_a = set(results[0]['blocks'])
    blocks_b = set(results[1]['blocks'])

    only_a = blocks_a - blocks_b
    only_b = blocks_b - blocks_a

    print(f"\nInput A ('{results[0]['input']}') : {len(blocks_a)} blocs, "
          f"retour={results[0]['result']}")
    print(f"Input B ('{results[1]['input']}') : {len(blocks_b)} blocs, "
          f"retour={results[1]['result']}")
    print(f"\nBlocs spécifiques à A : {len(only_a)}")
    for off in sorted(only_a):
        print(f"  0x{off:x}")
    print(f"Blocs spécifiques à B : {len(only_b)}")
    for off in sorted(only_b):
        print(f"  0x{off:x}")
```

Les blocs spécifiques à l'exécution valide correspondent à la branche « succès » de la vérification. Ceux spécifiques à l'exécution invalide correspondent à la branche « échec ». En examinant ces offsets dans Ghidra, on comprend exactement la structure du `if/else` qui décide de l'issue.

### Ce que la couverture révèle

Pour `keygenme_O0`, la couverture comparative montre typiquement :

- Un bloc commun qui appelle `compute_hash` et `strcmp` — le cœur de la vérification.  
- Un bloc spécifique à l'input valide qui appelle `puts("Clé valide ! Accès autorisé.")`.  
- Un bloc spécifique à l'input invalide qui appelle `puts("Clé invalide. Accès refusé.")`.  
- La différence se situe à un saut conditionnel unique — exactement le `jz`/`jnz` qu'on patcherait dans ImHex (chapitre 21, section 21.6).

---

## Phase 4 — Contournement

Nous avons maintenant une compréhension complète de la vérification. Trois approches de contournement se présentent, de la plus simple à la plus élégante.

### Approche 1 : forcer la valeur de retour de `validate_key`

La méthode la plus directe — on force `validate_key` à retourner `1` quel que soit l'input :

```javascript
// bypass_v1.js — forcer le retour
'use strict';

const validate = Module.findExportByName(null, "validate_key");

Interceptor.attach(validate, {
    onEnter(args) {
        this.input = args[0].readUtf8String();
    },
    onLeave(retval) {
        const original = retval.toInt32();
        if (original !== 1) {
            retval.replace(ptr(1));
            console.log(`[BYPASS] validate_key("${this.input}") : ${original} → 1`);
        }
    }
});

console.log("[*] Bypass v1 actif — validate_key() forcé à 1");
```

```bash
frida -f ./keygenme_O0 -l bypass_v1.js --no-pause
```

Résultat :

```
[*] Bypass v1 actif — validate_key() forcé à 1
=== KeyGenMe v1.0 ===
Entrez la clé de licence :  
nimportequoi  
[BYPASS] validate_key("nimportequoi") : 0 → 1
Clé valide ! Accès autorisé.
```

Le programme accepte n'importe quelle clé. C'est le contournement le plus rapide, mais il a une limite : il ne nous apprend rien sur la clé valide. Le programme pense que la clé est bonne, mais nous ne savons pas quelle clé serait acceptée légitimement.

### Approche 2 : forcer le retour de `strcmp`

Si la vérification repose sur un `strcmp`, on peut cibler cette fonction spécifique plutôt que la fonction enveloppante :

```javascript
// bypass_v2.js — forcer strcmp à retourner 0 (égalité)
'use strict';

const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        // Ne cibler que les strcmp appelés depuis notre binaire
        this.fromMain = this.returnAddress.compare(modBase) >= 0 &&
                        this.returnAddress.compare(modEnd) < 0;
        if (this.fromMain) {
            this.s1 = args[0].readUtf8String();
            this.s2 = args[1].readUtf8String();
        }
    },
    onLeave(retval) {
        if (this.fromMain && retval.toInt32() !== 0) {
            console.log(`[BYPASS] strcmp("${this.s1}", "${this.s2}") → forcé à 0`);
            retval.replace(ptr(0));
        }
    }
});

console.log("[*] Bypass v2 actif — strcmp forcé à 0 (depuis main module)");
```

Cette approche est plus précise : elle ne modifie que les `strcmp` initiés par le binaire, pas ceux de la libc. Et elle a un avantage majeur — les arguments du `strcmp` sont loggués, ce qui nous donne la clé attendue gratuitement.

### Approche 3 : patching mémoire du saut conditionnel

Pour un contournement persistant pendant toute la session (sans hook actif à chaque appel), on peut patcher l'instruction de branchement en mémoire :

```javascript
// bypass_v3.js — patch du jz en jnz
'use strict';

const mod = Process.enumerateModules()[0];  
const base = mod.base;  

// Offset du jz identifié par Ghidra ou par la couverture comparative
const jzOffset = 0x1234;  // À adapter selon le binaire réel  
const jzAddr = base.add(jzOffset);  

const opcode = jzAddr.readU8();  
console.log(`[*] Opcode @ offset 0x${jzOffset.toString(16)} : 0x${opcode.toString(16)}`);  

if (opcode === 0x74) {       // jz short
    Memory.patchCode(jzAddr, 1, code => { code.putU8(0x75); });
    console.log("[PATCH] jz (0x74) → jnz (0x75)");
} else if (opcode === 0x0F) {  // jz near (0F 84)
    const secondByte = jzAddr.add(1).readU8();
    if (secondByte === 0x84) {
        Memory.patchCode(jzAddr.add(1), 1, code => { code.putU8(0x85); });
        console.log("[PATCH] jz near (0F 84) → jnz near (0F 85)");
    }
} else {
    console.log(`[!] Opcode inattendu 0x${opcode.toString(16)}, abandon`);
}
```

Ce patch ne nécessite aucun hook actif — une fois l'opcode modifié en mémoire, la vérification est inversée de manière permanente (pour cette session d'exécution). Le surcoût est nul.

> 💡 **Trouver l'offset du saut.** La couverture comparative de la phase 3 identifie les blocs qui diffèrent entre input valide et invalide. Le dernier bloc commun avant la divergence se termine par le saut conditionnel recherché. Dans Ghidra, cette information est visible dans le Function Graph.

---

## Phase 5 — Extraction de la clé

Contourner la vérification est utile pour comprendre ce qui se passe « derrière le mur ». Mais l'objectif ultime est souvent de **récupérer la clé valide** — pour écrire un keygen (chapitre 21, section 21.8) ou pour comprendre l'algorithme de génération.

### Extraction passive via `strcmp`

La méthode la plus simple — déjà illustrée en phase 1 — consiste à lire les arguments de `strcmp` :

```javascript
// extract_key.js
'use strict';

const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

Interceptor.attach(Module.findExportByName(null, "strcmp"), {
    onEnter(args) {
        if (this.returnAddress.compare(modBase) >= 0 &&
            this.returnAddress.compare(modEnd) < 0) {

            const s1 = args[0].readUtf8String();
            const s2 = args[1].readUtf8String();

            send({
                event: 'strcmp',
                user_input: s1,
                expected_key: s2,
                caller_offset: '0x' + this.returnAddress.sub(modBase).toString(16)
            });
        }
    }
});
```

```python
def on_message(message, data):
    if message['type'] == 'send':
        p = message['payload']
        if p.get('event') == 'strcmp':
            print(f"\n{'='*50}")
            print(f"  Input utilisateur : {p['user_input']}")
            print(f"  Clé attendue      : {p['expected_key']}")
            print(f"  Appelé depuis     : {p['caller_offset']}")
            print(f"{'='*50}")
```

Sortie :

```
==================================================
  Input utilisateur : test123
  Clé attendue      : GCC-RE-2024-XPRO
  Appelé depuis     : 0x1234
==================================================
```

### Extraction quand la comparaison est custom

Si le binaire n'utilise pas `strcmp` mais une fonction de comparaison maison (boucle `for` octet par octet, XOR, hash), l'extraction directe ne fonctionne pas. Il faut alors intercepter les données en amont de la comparaison.

Stratégie : hooker la fonction `compute_hash` (identifiée en phase 1) pour capturer la transformation appliquée à l'input utilisateur et la valeur de référence :

```javascript
const compute_hash = Module.findExportByName(null, "compute_hash");

Interceptor.attach(compute_hash, {
    onEnter(args) {
        this.inputBuf = args[0];
        this.outputBuf = args[1];
        this.len = args[2].toInt32();

        console.log(`[*] compute_hash() — input (${this.len} octets) :`);
        console.log(hexdump(this.inputBuf, { length: this.len }));
    },
    onLeave(retval) {
        console.log(`[*] compute_hash() — output :`);
        console.log(hexdump(this.outputBuf, { length: 32 }));  // taille du hash
    }
});
```

On observe ainsi la transformation : quel hash est produit à partir de l'input. En hookant aussi `check_hash`, on voit la valeur de référence contre laquelle le hash est comparé. Avec ces deux informations, on peut soit inverser la transformation (si elle est simple), soit utiliser l'exécution symbolique (chapitre 18) pour trouver un input qui produit le hash attendu.

### Extraction par Stalker : observer la comparaison octet par octet

Pour les comparaisons custom qui procèdent octet par octet dans une boucle, Stalker avec un callout sur les instructions `cmp` est l'approche la plus directe :

```javascript
const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  
const validate = Module.findExportByName(null, "validate_key");  

const comparedBytes = [];

Interceptor.attach(validate, {
    onEnter(args) {
        this.tid = Process.getCurrentThreadId();

        Stalker.follow(this.tid, {
            transform(iterator) {
                let insn = iterator.next();
                do {
                    const addr = insn.address;
                    // Intercepter les instructions CMP dans le module principal
                    if (insn.mnemonic === 'cmp' &&
                        addr.compare(modBase) >= 0 &&
                        addr.compare(modEnd) < 0) {

                        iterator.putCallout((context) => {
                            // Lire les opérandes de CMP depuis les registres
                            // (dépend de l'instruction exacte — ici rax vs rbx)
                            const a = context.rax.toInt32() & 0xFF;
                            const b = context.rbx.toInt32() & 0xFF;
                            if (a >= 0x20 && a <= 0x7E && b >= 0x20 && b <= 0x7E) {
                                comparedBytes.push({
                                    user: String.fromCharCode(a),
                                    expected: String.fromCharCode(b)
                                });
                            }
                        });
                    }
                    iterator.keep();
                } while ((insn = iterator.next()) !== null);
            }
        });
    },
    onLeave(retval) {
        Stalker.unfollow(this.tid);
        Stalker.flush();
        Stalker.garbageCollect();

        if (comparedBytes.length > 0) {
            const expectedKey = comparedBytes.map(b => b.expected).join('');
            console.log(`\n[*] Octets comparés :`);
            comparedBytes.forEach((b, i) =>
                console.log(`  [${i}] user='${b.user}' vs expected='${b.expected}'`)
            );
            console.log(`\n[KEY] Clé reconstruite : "${expectedKey}"`);
        }
        comparedBytes.length = 0;
    }
});
```

> ⚠️ Les registres impliqués dans le `cmp` dépendent du code assembleur réel. Le script ci-dessus suppose `cmp al, bl` — en pratique, il faut examiner les opérandes de l'instruction dans Ghidra pour savoir quels registres lire. L'attribut `insn.opStr` dans le callback `transform` donne les opérandes sous forme textuelle (`"al, bl"`, `"byte ptr [rdi+rcx], dl"`, etc.) pour guider ce choix.

---

## Script complet : reconnaissance → extraction automatisée

Voici un script Python unique qui orchestre l'ensemble de la méthodologie — de la reconnaissance à l'extraction — de manière automatisée :

```python
#!/usr/bin/env python3
"""
Frida automation — keygenme license bypass & key extraction.  
Usage: python3 solve_keygenme.py ./keygenme_O0  
"""
import frida  
import sys  
import time  

AGENT = r"""
'use strict';

const mod = Process.enumerateModules()[0];  
const modBase = mod.base;  
const modEnd = modBase.add(mod.size);  

// Phase 1 : Reconnaissance — quelles fonctions de comparaison sont appelées ?
const compareFuncs = ["strcmp", "strncmp", "memcmp"];  
const foundKeys = [];  

compareFuncs.forEach(name => {
    const addr = Module.findExportByName(null, name);
    if (!addr) return;

    Interceptor.attach(addr, {
        onEnter(args) {
            this.fromMain = this.returnAddress.compare(modBase) >= 0 &&
                            this.returnAddress.compare(modEnd) < 0;
            if (this.fromMain) {
                try {
                    this.s1 = args[0].readUtf8String();
                    this.s2 = args[1].readUtf8String();
                } catch(e) {
                    this.fromMain = false;
                }
            }
        },
        onLeave(retval) {
            if (this.fromMain) {
                send({
                    phase: 'recon',
                    func: name,
                    s1: this.s1,
                    s2: this.s2,
                    result: retval.toInt32(),
                    caller: this.returnAddress.sub(modBase).toString(16)
                });
            }
        }
    });
});

// Phase 2 : Bypass — forcer validate_key si elle existe
const validate = Module.findExportByName(null, "validate_key");  
if (validate) {  
    Interceptor.attach(validate, {
        onLeave(retval) {
            const orig = retval.toInt32();
            if (orig !== 1) {
                retval.replace(ptr(1));
                send({ phase: 'bypass', original: orig, patched: 1 });
            }
        }
    });
    send({ phase: 'info', message: 'validate_key trouvé et hooké' });
} else {
    send({ phase: 'info', message: 'validate_key non trouvé — binaire strippé' });
}
"""

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <binary>")
        sys.exit(1)

    binary = sys.argv[1]
    extracted_keys = set()

    def on_message(msg, data):
        if msg['type'] == 'send':
            p = msg['payload']
            phase = p.get('phase')

            if phase == 'info':
                print(f"[INFO] {p['message']}")

            elif phase == 'recon':
                func = p['func']
                s1, s2 = p['s1'], p['s2']
                result = p['result']
                print(f"\n[RECON] {func}(\"{s1}\", \"{s2}\") = {result}")
                print(f"        appelé depuis offset 0x{p['caller']}")

                # L'un des arguments est l'input utilisateur, l'autre est la clé attendue
                # Heuristique : la clé attendue n'est pas ce qu'on a tapé
                for candidate in [s1, s2]:
                    if candidate and len(candidate) > 2:
                        extracted_keys.add(candidate)

                if result == 0:
                    print(f"  → MATCH ! Clé trouvée dans les arguments.")

            elif phase == 'bypass':
                print(f"\n[BYPASS] validate_key : {p['original']} → {p['patched']}")

        elif msg['type'] == 'error':
            print(f"[ERROR] {msg['stack']}")

    print(f"[*] Lancement de {binary} avec Frida...")
    pid = frida.spawn([binary])
    session = frida.attach(pid)

    script = session.create_script(AGENT)
    script.on('message', on_message)
    script.load()

    frida.resume(pid)
    print("[*] Hooks actifs. Interagissez avec le programme.")
    print("[*] Ctrl+C pour arrêter.\n")

    try:
        sys.stdin.read()
    except KeyboardInterrupt:
        pass

    print(f"\n{'='*50}")
    print(f"[RÉSULTAT] Clés candidates extraites :")
    for key in extracted_keys:
        print(f"  → \"{key}\"")
    print(f"{'='*50}")

    session.detach()

if __name__ == '__main__':
    main()
```

Exécution :

```bash
python3 solve_keygenme.py ./keygenme_O0
```

```
[*] Lancement de ./keygenme_O0 avec Frida...
[INFO] validate_key trouvé et hooké
[*] Hooks actifs. Interagissez avec le programme.

=== KeyGenMe v1.0 ===
Entrez la clé de licence :  
nimportequoi  

[RECON] strcmp("nimportequoi", "GCC-RE-2024-XPRO") = -7
        appelé depuis offset 0x1256

[BYPASS] validate_key : 0 → 1
Clé valide ! Accès autorisé.

^C
==================================================
[RÉSULTAT] Clés candidates extraites :
  → "nimportequoi"
  → "GCC-RE-2024-XPRO"
==================================================
```

Le script a simultanément contourné la vérification (le programme affiche « Clé valide ») et extrait la clé attendue (`GCC-RE-2024-XPRO`). Le tout en un seul lancement, sans aucune interaction manuelle hormis la saisie d'un input arbitraire.

---

## Synthèse méthodologique

Ce cas pratique illustre un workflow en cinq phases qui s'applique à une large gamme de binaires protégés :

| Phase | Outil Frida | Ce qu'on obtient |  
|---|---|---|  
| **Reconnaissance** | `frida-trace -i "str*"` | Liste des fonctions appelées, flux d'exécution |  
| **Localisation** | `Interceptor.attach` + backtrace | Adresse et signature de la fonction de décision |  
| **Compréhension** | `Stalker` (couverture comparative) | Carte des blocs exécutés, identification du branchement critique |  
| **Contournement** | `retval.replace` ou `Memory.patchCode` | Programme qui accepte n'importe quel input |  
| **Extraction** | Hook sur `strcmp`/`memcmp` + lecture des args | Clé valide ou données de référence |

Les trois approches de contournement correspondent à trois niveaux de compréhension :

- **Forcer `validate_key`** — boîte noire, on ne comprend pas le mécanisme interne mais on contourne le résultat.  
- **Forcer `strcmp`** — boîte grise, on sait que la vérification repose sur une comparaison de chaînes.  
- **Patcher le `jz`** — boîte blanche, on a identifié l'instruction exacte qui prend la décision.

En RE réel, on progresse souvent de la boîte noire vers la boîte blanche au fil de l'analyse. Frida permet de tester chaque hypothèse instantanément, sans cycle de recompilation, et de passer à l'hypothèse suivante en quelques secondes.

---

## Limites et cas plus complexes

Le binaire `keygenme_O0` est volontairement simple — c'est un exercice d'apprentissage. Les protections réelles présentent des défis supplémentaires :

**Vérifications multiples.** Certains programmes effectuent plusieurs vérifications successives (format de la clé, checksum, signature cryptographique). Contourner la première ne suffit pas — il faut identifier et traiter chacune.

**Clé dérivée de l'environnement.** La clé valide peut dépendre d'un identifiant matériel, d'un nom d'utilisateur, ou d'un timestamp. Dans ce cas, l'extraction passive ne donne la clé que pour cette exécution spécifique — un keygen doit reproduire l'algorithme de dérivation.

**Obfuscation.** Le binaire peut utiliser du control flow flattening (chapitre 19, section 19.3), de l'opaque predicate, ou du code auto-modifiant pour rendre l'analyse plus difficile. Stalker reste efficace (il suit le code réellement exécuté, quelle que soit l'obfuscation statique), mais le volume de blocs à analyser explose.

**Détection de Frida.** Le programme peut scanner `/proc/self/maps` à la recherche de `frida-agent`, vérifier le nombre de threads, ou utiliser des timing checks. Le chapitre 19 (section 19.7) détaille ces techniques et leurs contre-mesures.

Les chapitres suivants de la Partie V (21 à 25) appliquent cette méthodologie à des scénarios progressivement plus complexes — de la simple comparaison de chaînes au chiffrement AES avec clé dérivée.

---

## Ce qu'il faut retenir

- La méthodologie **reconnaissance → localisation → compréhension → contournement → extraction** est un cadre reproductible applicable à tout binaire protégé.  
- `frida-trace` est l'outil de première intention : en une commande, il révèle les fonctions appelées et leurs arguments.  
- La **couverture comparative** avec Stalker identifie automatiquement les blocs de code responsables de la décision valide/invalide.  
- Trois niveaux de contournement : **retour de la fonction de vérification** (le plus rapide), **retour de la fonction de comparaison** (plus précis, révèle la clé), **patching du saut conditionnel** (le plus chirurgical).  
- Un script Python unique peut combiner reconnaissance, contournement et extraction dans un workflow entièrement automatisé.  
- Les limites (vérifications multiples, clé dérivée, obfuscation, détection de Frida) motivent les techniques avancées des chapitres suivants.

---

> **Prochaine étape** : 🎯 Checkpoint du chapitre 13 — écrire un script Frida qui logue tous les appels à `send()` avec leurs buffers, en mettant en pratique les techniques d'interception vues tout au long du chapitre.

⏭️ [🎯 Checkpoint : écrire un script Frida qui logue tous les appels à `send()` avec leurs buffers](/13-frida/checkpoint.md)
