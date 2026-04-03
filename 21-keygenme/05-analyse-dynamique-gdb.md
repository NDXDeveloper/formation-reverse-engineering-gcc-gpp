🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.5 — Analyse dynamique : tracer la comparaison avec GDB

> 📖 **Rappel** : les commandes fondamentales de GDB (`break`, `run`, `next`, `step`, `info`, `x`, `print`) sont présentées au chapitre 11 (sections 11.2 à 11.5). Les extensions GEF/pwndbg sont couvertes au chapitre 12. Cette section suppose une maîtrise de base de ces outils.

---

## Introduction

Jusqu'ici, toute notre analyse a été statique : on a lu le binaire sans jamais l'exécuter. On connaît la structure du code, les fonctions impliquées, les sauts conditionnels et les opcodes correspondants. Mais l'analyse statique produit des *hypothèses*. L'analyse dynamique les *confirme*.

L'objectif de cette section est double :

1. **Valider notre compréhension** en observant le programme en cours d'exécution — vérifier que le flux suit bien le chemin que nous avons déduit.  
2. **Capturer la clé attendue** directement en mémoire, au moment où `check_license` construit la chaîne `expected` avant de la comparer avec `strcmp`.

L'outil est GDB, le débogueur standard de la toolchain GNU. Nous utiliserons l'extension GEF pour la visualisation des registres et de la pile, mais toutes les commandes GDB natives fonctionnent sans extension.

---

## Préparation de la session GDB

### Lancement avec GEF

```bash
$ gdb -q keygenme_O0
```

Le flag `-q` (quiet) supprime le message d'accueil. Si GEF est installé (chapitre 12), le prompt passe de `(gdb)` à `gef➤`. Sinon, toutes les commandes restent identiques — seul l'affichage diffère.

### Désactivation de l'ASLR

Notre binaire est PIE (section 21.2). Avec ASLR actif, l'adresse de base change à chaque exécution, ce qui complique le suivi des adresses. On désactive la randomisation pour cette session :

```bash
gef➤ set disable-randomization on
```

Ce réglage est activé par défaut dans GDB, mais il est bon de le vérifier explicitement. Il ne touche que le processus lancé depuis GDB — le système hôte reste protégé.

### Vérification des symboles

Puisque nous travaillons sur `keygenme_O0` compilé avec `-g`, GDB a accès aux symboles DWARF :

```bash
gef➤ info functions
```

On retrouve la liste complète des fonctions : `main`, `check_license`, `compute_hash`, `derive_key`, `format_key`, `rotate_left`, `read_line`. Si cette commande retourne une liste vide ou uniquement des symboles de la libc, le binaire est strippé — nous traiterons ce cas en fin de section.

---

## Stratégie 1 — Breakpoint sur `check_license`

La première approche consiste à poser un breakpoint à l'entrée de `check_license` pour observer les arguments reçus, puis à avancer pas à pas jusqu'au `strcmp` pour capturer la clé attendue.

### Poser le breakpoint

```bash
gef➤ break check_license  
Breakpoint 1 at 0x5555555552f0: file keygenme.c, line 90.  
```

GDB affiche l'adresse résolue et, grâce à DWARF, le fichier source et le numéro de ligne correspondants.

### Lancer le programme

```bash
gef➤ run
```

Le programme démarre et affiche sa bannière, puis attend la saisie du username :

```
=== KeyGenMe v1.0 — RE Training ===

Enter username:
```

On saisit un nom de test, par exemple `Alice`, puis une clé arbitraire quand le programme la demande :

```
Enter username: Alice  
Enter license key (XXXX-XXXX-XXXX-XXXX): AAAA-BBBB-CCCC-DDDD  
```

L'exécution s'arrête immédiatement au breakpoint :

```
Breakpoint 1, check_license (username=0x7fffffffe010 "Alice",
    user_key=0x7fffffffe030 "AAAA-BBBB-CCCC-DDDD") at keygenme.c:90
```

GDB affiche les arguments de la fonction. On voit déjà nos deux entrées : `"Alice"` et `"AAAA-BBBB-CCCC-DDDD"`. Avec GEF, le panneau des registres montre :

```
$rdi = 0x7fffffffe010 → "Alice"
$rsi = 0x7fffffffe030 → "AAAA-BBBB-CCCC-DDDD"
```

C'est cohérent avec la convention System V AMD64 : le premier argument est dans `RDI`, le second dans `RSI`.

### Avancer jusqu'au `strcmp`

Plutôt que de stepper instruction par instruction à travers `compute_hash`, `derive_key` et `format_key`, on pose un second breakpoint directement sur l'appel à `strcmp` :

```bash
gef➤ break strcmp@plt  
Breakpoint 2 at 0x555555555080  
gef➤ continue  
```

L'exécution reprend et s'arrête à l'entrée de `strcmp`. À ce moment précis, les deux arguments de `strcmp` sont dans `RDI` et `RSI` :

```bash
gef➤ info registers rdi rsi  
rdi    0x7fffffffdfe0    → pointe vers la clé ATTENDUE (calculée)  
rsi    0x7fffffffe030    → pointe vers la clé SAISIE (par l'utilisateur)  
```

### Capturer la clé attendue

On lit la chaîne pointée par `RDI` — c'est la clé que le programme a calculée pour le username `"Alice"` :

```bash
gef➤ x/s $rdi
0x7fffffffdfe0: "DCEB-0DFC-B51F-3428"
```

La clé attendue pour `"Alice"` est `DCEB-0DFC-B51F-3428`.

On vérifie la clé saisie pour comparaison :

```bash
gef➤ x/s $rsi
0x7fffffffe030: "AAAA-BBBB-CCCC-DDDD"
```

Les deux chaînes sont différentes. Si on laisse l'exécution continuer, `strcmp` retournera une valeur non nulle, le `JNE` sera pris, et le programme affichera « Invalid license ».

### Vérification : relancer avec la bonne clé

On relance le programme avec la clé capturée :

```bash
gef➤ run
```

GDB propose de relancer le processus — confirmer avec `y`. On saisit à nouveau `Alice` comme username, mais cette fois on entre la clé capturée :

```
Enter username: Alice  
Enter license key (XXXX-XXXX-XXXX-XXXX): DCEB-0DFC-B51F-3428  
```

Le breakpoint sur `strcmp` s'active. On vérifie :

```bash
gef➤ x/s $rdi
0x7fffffffdfe0: "DCEB-0DFC-B51F-3428"
gef➤ x/s $rsi
0x7fffffffe030: "DCEB-0DFC-B51F-3428"
```

Les deux chaînes sont identiques. On continue l'exécution :

```bash
gef➤ continue
[+] Valid license! Welcome, Alice.
```

L'hypothèse est confirmée : on a capturé la clé valide directement en mémoire.

---

## Stratégie 2 — Breakpoint sur `strcmp` uniquement

La stratégie précédente nécessitait de connaître le nom `check_license`. Sur un binaire strippé, ce nom n'existe plus. Une approche plus directe consiste à poser un breakpoint uniquement sur `strcmp@plt`, sans se soucier de la fonction appelante.

```bash
gef➤ break strcmp@plt  
gef➤ run  
```

Après avoir saisi username et clé, le breakpoint s'active sur chaque appel à `strcmp` dans le programme. Sur notre keygenme, il n'y a qu'un seul appel, donc on tombe directement au bon endroit. Sur un binaire plus complexe avec plusieurs `strcmp`, on utilise les commandes GDB pour identifier le bon :

```bash
# Afficher la backtrace pour voir d'où vient l'appel
gef➤ backtrace
#0  __strcmp_sse2 () at ...
#1  0x0000555555555338 in check_license (...)
#2  0x0000555555555405 in main (...)
```

La backtrace confirme que cet appel à `strcmp` provient bien de `check_license`, elle-même appelée depuis `main`.

> 💡 **Sur un binaire strippé**, la backtrace affichera des adresses brutes au lieu des noms de fonctions :  
> ```  
> #1  0x0000555555555338 in ?? ()  
> #2  0x0000555555555405 in ?? ()  
> ```  
> Les adresses restent exploitables : on peut les corréler avec les offsets dans Ghidra pour confirmer qu'on est au bon endroit.

---

## Stratégie 3 — Observer le retour de `strcmp` et le saut

Au lieu de capturer la clé avant `strcmp`, on peut observer ce qui se passe *après* — le retour de `strcmp` et le saut conditionnel.

### Breakpoint après `strcmp`

On pose le breakpoint non pas sur `strcmp` lui-même, mais sur l'instruction qui suit son appel dans `check_license`. Depuis Ghidra, on a noté que le `TEST EAX, EAX` se trouve à une certaine adresse (par exemple `0x1335` en offset). En GDB, avec ASLR désactivé et une base connue :

```bash
# Avec symboles : utiliser un offset relatif à check_license
gef➤ disassemble check_license
```

On repère l'instruction `TEST EAX, EAX` après le `CALL strcmp@plt` et on pose le breakpoint sur son adresse :

```bash
gef➤ break *check_license+69
```

L'astuce `*fonction+offset` permet de cibler une instruction précise à l'intérieur d'une fonction. L'offset en octets est calculé depuis le début de la fonction.

### Observer les flags

Après avoir saisi username et clé, l'exécution s'arrête sur le `TEST EAX, EAX`. On inspecte :

```bash
# Valeur de retour de strcmp
gef➤ print $eax
$1 = -14
```

`strcmp` a retourné -14 (valeur non nulle — les chaînes diffèrent). Après l'exécution du `TEST`, on avance d'une instruction :

```bash
gef➤ stepi
```

On est maintenant sur le `JNE`. On vérifie l'état du Zero Flag :

```bash
gef➤ print $eflags
$2 = [ PF IF ]
```

Le Zero Flag (`ZF`) n'apparaît pas dans la liste — il vaut 0. Avec GEF, le panneau des flags affiche directement :

```
flags: ... [zero:0] ...
```

Puisque ZF = 0, le `JNE` (Jump if Not Zero) **sera pris** → le programme saute vers le chemin d'échec.

### Relancer avec la bonne clé

En relançant avec la clé correcte :

```bash
gef➤ print $eax
$3 = 0
```

`strcmp` a retourné 0 (chaînes identiques). Après `TEST EAX, EAX` :

```
flags: ... [zero:1] ...
```

ZF = 1, donc le `JNE` **ne sera pas pris** → l'exécution continue séquentiellement vers `return 1` → succès.

On a observé en temps réel exactement le mécanisme décrit en section 21.4.

---

## Stratégie 4 — Modifier `EAX` à la volée

GDB ne sert pas uniquement à observer — il permet aussi de modifier l'état du processus en cours d'exécution. On peut forcer le programme à prendre le chemin de succès *sans connaître la clé* en modifiant la valeur de `EAX` après `strcmp`.

### Procédure

1. Poser un breakpoint juste après l'appel à `strcmp` dans `check_license` (sur le `TEST EAX, EAX`).  
2. Lancer le programme et saisir un username et une clé quelconque.  
3. Au breakpoint, `EAX` contient une valeur non nulle (clé incorrecte).  
4. Forcer `EAX` à 0 :

```bash
gef➤ set $eax = 0
```

5. Continuer l'exécution :

```bash
gef➤ continue
[+] Valid license! Welcome, Alice.
```

Le programme affiche le message de succès alors que la clé saisie était fausse. On a « triché » en simulant un retour 0 de `strcmp`.

### Alternative : modifier le Zero Flag directement

On peut aussi agir sur le flag plutôt que sur le registre. On se place sur l'instruction `JNE` (après que `TEST` a positionné les flags) :

```bash
# Forcer ZF = 1 en activant le bit correspondant dans EFLAGS
gef➤ set $eflags |= (1 << 6)  
gef➤ continue  
[+] Valid license! Welcome, Alice.
```

Le bit 6 de `EFLAGS` est le Zero Flag. En le forçant à 1, le `JNE` n'est plus pris et l'exécution emprunte le chemin de succès.

> ⚠️ **Attention** : cette modification est éphémère. Elle n'existe que dans cette exécution, dans cette session GDB. Le binaire sur disque n'est pas modifié. Pour un changement permanent, il faut patcher le binaire — c'est l'objet de la section 21.6.

---

## Stratégie 5 — Dumper la clé avec un breakpoint conditionnel

Pour aller plus loin dans l'automatisation, on peut utiliser un breakpoint conditionnel avec commandes automatiques. L'idée : chaque fois que `strcmp` est appelé, GDB affiche automatiquement les deux chaînes comparées, puis laisse le programme continuer.

```bash
gef➤ break strcmp@plt  
gef➤ commands 1  
  > silent
  > printf "── strcmp intercepté ──\n"
  > printf "  expected : %s\n", (char*)$rdi
  > printf "  user_key : %s\n", (char*)$rsi
  > printf "────────────────────\n"
  > continue
  > end
gef➤ run
```

Désormais, à chaque passage dans `strcmp`, GDB affiche les deux arguments puis reprend automatiquement l'exécution. La sortie ressemble à :

```
=== KeyGenMe v1.0 — RE Training ===

Enter username: Alice  
Enter license key (XXXX-XXXX-XXXX-XXXX): TEST-TEST-TEST-TEST  
── strcmp intercepté ──
  expected : DCEB-0DFC-B51F-3428
  user_key : TEST-TEST-TEST-TEST
────────────────────
[-] Invalid license. Try again.
```

On a capturé la clé attendue sans interrompre le flux du programme. Cette technique est particulièrement utile sur des binaires qui effectuent plusieurs vérifications successives ou qui sont sensibles aux interruptions (anti-debug basé sur le timing).

### Breakpoint conditionnel avec filtre

Si le binaire contenait de nombreux appels à `strcmp` (vérification de configuration, comparaison de chemins de fichiers…), on pourrait filtrer pour ne loguer que les appels pertinents :

```bash
# Ne s'arrêter que si la chaîne attendue contient un tiret (format XXXX-XXXX)
gef➤ break strcmp@plt if $_regex((char*)$rdi, ".*-.*-.*-.*")
```

La commande `$_regex` est une fonction interne de GDB qui effectue une correspondance par expression régulière. Elle permet de cibler précisément les appels `strcmp` liés à la vérification de licence sans être dérangé par les autres.

---

## Travailler sans symboles : le cas `keygenme_strip`

Sur la variante strippée, les noms de fonctions internes ont disparu. Les stratégies 1 et 3 (qui utilisent le nom `check_license`) ne fonctionnent plus directement. Voici comment s'adapter.

### `strcmp@plt` reste accessible

Les symboles dynamiques (fonctions importées de la libc) survivent au stripping car ils sont dans `.dynsym`, pas dans `.symtab`. On peut toujours poser :

```bash
gef➤ break strcmp@plt
```

C'est la raison pour laquelle la **stratégie 2** (breakpoint sur `strcmp@plt`) fonctionne identiquement sur un binaire strippé. La backtrace affichera des adresses brutes au lieu des noms, mais les arguments dans `RDI`/`RSI` sont les mêmes.

### Breakpoint par adresse absolue

Si on veut poser un breakpoint sur une fonction interne (l'équivalent strippé de `check_license`), on utilise l'offset trouvé dans Ghidra :

```bash
# Offset dans Ghidra : 0x001012f0
# Base PIE (ASLR off) : souvent 0x555555554000 sur x86-64
# Adresse absolue = base + offset

gef➤ break *0x5555555552f0
```

On peut aussi utiliser la commande `info proc mappings` après un premier `run` (puis `Ctrl+C` ou breakpoint temporaire) pour connaître la base exacte :

```bash
gef➤ starti  
gef➤ info proc mappings  
```

`starti` lance le programme et s'arrête sur la toute première instruction (avant même `_start`). La sortie de `info proc mappings` montre la base de chargement du binaire, à partir de laquelle on calcule les adresses absolues.

### Script GDB pour automatiser

Pour ne pas recalculer les adresses manuellement à chaque session, on peut écrire un petit fichier de commandes GDB :

```bash
# fichier: trace_strcmp.gdb
set disable-randomization on  
break strcmp@plt  
commands  
  silent
  printf "expected: %s\n", (char*)$rdi
  printf "user_key: %s\n", (char*)$rsi
  continue
end  
run  
```

Lancement :

```bash
$ gdb -q -x trace_strcmp.gdb keygenme_strip
```

GDB exécute automatiquement les commandes du fichier. On obtient la clé attendue sans intervention manuelle, que le binaire soit strippé ou non.

---

## Synthèse des stratégies

| Stratégie | Cible du breakpoint | Information obtenue | Fonctionne strippé ? |  
|---|---|---|---|  
| 1 — BP sur `check_license` | Entrée de la fonction | Arguments (username, user_key) | ❌ (nom absent) |  
| 2 — BP sur `strcmp@plt` | Appel libc | Clé attendue vs clé saisie | ✅ |  
| 3 — BP après `strcmp` | `TEST EAX, EAX` | Valeur de retour, état des flags | ✅ (par adresse) |  
| 4 — Modification de `EAX` | `TEST EAX, EAX` | Bypass de la vérification | ✅ (par adresse) |  
| 5 — BP conditionnel auto | `strcmp@plt` | Log automatique sans interruption | ✅ |

La **stratégie 2** est la plus universelle : elle fonctionne avec ou sans symboles, ne nécessite aucun calcul d'adresse, et donne directement la clé attendue. C'est celle à privilégier en première approche.

La **stratégie 4** (modification de `EAX`) est un bypass dynamique — utile pour vérifier rapidement qu'on a identifié le bon point de décision, mais éphémère. Pour un contournement permanent, on passe au patching binaire (section 21.6). Pour une solution propre, on écrit un keygen (section 21.8).

---

## Ce que l'analyse dynamique nous a apporté

En complément de l'analyse statique des sections précédentes, GDB nous a permis de :

- **Confirmer** que `check_license` reçoit bien le username et la clé saisie en arguments.  
- **Observer** la clé attendue en mémoire, calculée par `compute_hash` → `derive_key` → `format_key`.  
- **Capturer** une clé valide pour un username donné, prouvant notre compréhension de l'algorithme.  
- **Vérifier** le mécanisme du saut conditionnel en observant `EAX` et le Zero Flag en temps réel.  
- **Démontrer** qu'un bypass dynamique est possible en modifiant un registre ou un flag.

On dispose maintenant de toutes les pièces du puzzle. Les trois dernières sections du chapitre exploitent cette compréhension de trois façons différentes : patching permanent du binaire (21.6), résolution automatique par exécution symbolique (21.7), et écriture d'un keygen qui reproduit l'algorithme (21.8).

⏭️ [Patching binaire : inverser un saut directement dans le binaire (avec ImHex)](/21-keygenme/06-patching-imhex.md)
