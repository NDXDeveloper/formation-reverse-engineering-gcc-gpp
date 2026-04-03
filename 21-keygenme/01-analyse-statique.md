🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.1 — Analyse statique complète du binaire (triage, strings, sections)

> 📖 **Rappel** : cette section applique le workflow de « triage rapide en 5 minutes » présenté au chapitre 5 (section 5.7). Si la démarche vous semble floue, relisez-le avant de continuer.

---

## Introduction

Face à un binaire inconnu, la tentation de l'ouvrir immédiatement dans Ghidra est forte. C'est une erreur fréquente chez le débutant. Un triage méthodique de quelques minutes fournit un cadre mental indispensable : on sait *ce qu'on analyse* avant de plonger dans le *comment*. Cette première phase est entièrement passive — on ne lance jamais le binaire, on ne le modifie pas, on l'observe.

Dans cette section, nous appliquons ce triage à la variante `keygenme_O0` (compilée avec `-O0 -g`, symboles présents). Les résultats serviront de référence pour comparer avec les variantes optimisées et strippées dans les sections suivantes.

---

## Étape 1 — `file` : identification du format

La toute première commande à exécuter sur un binaire inconnu est `file`. Elle identifie le format du fichier en analysant ses magic bytes et ses headers, sans jamais l'exécuter.

```bash
$ file keygenme_O0
keygenme_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, with debug_info, not stripped  
```

Chaque élément de cette sortie est une information exploitable :

| Fragment | Signification pour le RE |  
|---|---|  
| `ELF 64-bit LSB` | Format ELF, architecture 64 bits, little-endian. On travaille en x86-64 avec des registres de 64 bits (`rax`, `rdi`…) et la convention d'appel System V AMD64. |  
| `pie executable` | Position-Independent Executable — les adresses dans le désassemblage sont des offsets relatifs. En dynamique (GDB), les adresses absolues changeront à chaque exécution si ASLR est actif. |  
| `dynamically linked` | Le binaire dépend de bibliothèques partagées (au minimum la libc). On s'attend à trouver une section `.plt`/`.got` et des appels via `call xxx@plt`. |  
| `interpreter /lib64/ld-linux-x86-64.so.2` | Le dynamic linker standard Linux. Confirme un binaire GNU/Linux classique. |  
| `with debug_info` | Les informations DWARF sont présentes — Ghidra et GDB pourront afficher les noms de fonctions, variables et types originaux. |  
| `not stripped` | La table de symboles (`.symtab`) est intacte. `nm` listera toutes les fonctions. |

> 💡 **À retenir** : la mention `with debug_info, not stripped` est un luxe en RE. Sur une cible réelle, ces informations sont presque toujours absentes. Ici, elles servent de point de référence. Nous verrons en section 21.3 comment s'en passer sur `keygenme_strip`.

### Comparaison rapide avec la variante strippée

Pour mesurer la différence, appliquons la même commande sur `keygenme_strip` :

```bash
$ file keygenme_strip
keygenme_strip: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, stripped  
```

La mention `with debug_info` a disparu et `stripped` remplace `not stripped`. Le format, l'architecture et le linking sont identiques — seule la quantité d'information disponible pour l'analyste a changé. C'est exactement le rôle du stripping vu au chapitre 19.

---

## Étape 2 — `strings` : extraction des chaînes lisibles

La commande `strings` extrait toutes les séquences de caractères ASCII imprimables d'une longueur minimale (par défaut 4). C'est souvent l'étape la plus révélatrice du triage : messages d'erreur, prompts, noms de fonctions, constantes, chemins de fichiers — tout ce que le développeur a laissé en clair.

```bash
$ strings keygenme_O0
```

Parmi les centaines de chaînes retournées (dont beaucoup proviennent de la libc et des métadonnées ELF), certaines sautent aux yeux :

```
=== KeyGenMe v1.0 — RE Training ===
Enter username:  
Enter license key (XXXX-XXXX-XXXX-XXXX):  
[+] Valid license! Welcome, %s.
[-] Invalid license. Try again.
[-] Username must be between 3 and 31 characters.
%04X-%04X-%04X-%04X
```

### Analyse de chaque chaîne

**`=== KeyGenMe v1.0 — RE Training ===`** — La bannière du programme. Elle confirme qu'on est face à un crackme/keygenme. Le nom et la version peuvent être utiles pour des recherches en source ouverte sur une cible réelle.

**`Enter username:` et `Enter license key (XXXX-XXXX-XXXX-XXXX):`** — Les prompts utilisateur. Ils révèlent le format attendu de la clé : quatre groupes de quatre caractères séparés par des tirets. C'est une information capitale : la clé fait 19 caractères au format hexadécimal.

**`[+] Valid license! Welcome, %s.`** — Le message de succès. Le `%s` indique un appel à `printf` (ou `sprintf`) avec le nom d'utilisateur en paramètre. En analyse statique dans Ghidra, on pourra chercher la cross-reference vers cette chaîne pour remonter directement à la branche « succès » du code.

**`[-] Invalid license. Try again.`** — Le message d'échec. Même stratégie : sa cross-reference mène à la branche « échec ». L'embranchement entre les deux branches est le saut conditionnel qu'on cherche à identifier (section 21.4).

**`[-] Username must be between 3 and 31 characters.`** — Un contrôle de longueur sur le username. On en déduit que le programme valide l'entrée avant de procéder à la vérification de clé. En RE, cette information aide à comprendre le flux : validation du username → calcul/vérification de la clé → résultat.

**`%04X-%04X-%04X-%04X`** — Un format `printf`/`snprintf`. Quatre valeurs hexadécimales de 4 chiffres (le `04X` signifie : entier non signé en hexadécimal majuscule, padé à 4 caractères avec des zéros). Cette chaîne est un indice majeur : elle révèle que le programme *construit* en interne une chaîne au format `XXXX-XXXX-XXXX-XXXX`, probablement la clé attendue, avant de la comparer à l'entrée utilisateur.

### Filtrer le bruit

La sortie brute de `strings` contient énormément de bruit (noms de sections, chemins du linker, symboles DWARF…). Quelques astuces pour filtrer :

```bash
# Chaînes de 8 caractères minimum (élimine le bruit court)
$ strings -n 8 keygenme_O0

# Chercher des patterns spécifiques
$ strings keygenme_O0 | grep -i "license\|key\|valid\|invalid\|password\|serial"

# Chaînes dans la section .rodata uniquement (données en lecture seule)
$ strings -t x keygenme_O0 | head -40
```

L'option `-t x` ajoute l'offset hexadécimal de chaque chaîne dans le fichier. Cet offset est précieux : on peut le retrouver dans ImHex ou dans le désassemblage pour identifier exactement quelle fonction référence cette chaîne.

### Ce que `strings` ne montre pas

Il est important de garder à l'esprit les limites de `strings` :

- Les chaînes **encodées ou chiffrées** n'apparaîtront pas (XOR simple, base64, chiffrement AES…). On verra cette problématique au chapitre 24.  
- Les chaînes **construites dynamiquement** (caractère par caractère sur la pile) échappent à `strings`. C'est une technique d'obfuscation légère mais efficace.  
- Les chaînes **Unicode** (UTF-16) nécessitent l'option `-e l` (little-endian 16 bits) pour être détectées.

Sur notre keygenme, toutes les chaînes sont en clair en ASCII — c'est volontaire pour l'apprentissage.

---

## Étape 3 — `readelf` : anatomie du binaire ELF

Après l'identification par `file` et l'extraction de chaînes, on examine la structure interne du binaire ELF. La commande `readelf` permet d'inspecter les headers, sections et segments sans désassembler.

### Header ELF

```bash
$ readelf -h keygenme_O0
```

Les champs essentiels à relever :

- **Type** : `DYN (Position-Independent Executable)` — confirme PIE, cohérent avec `file`.  
- **Entry point address** : l'adresse du point d'entrée (`_start`, pas `main`). En PIE, c'est un offset relatif (typiquement `0x1080` ou similaire). Utile pour retrouver le début de l'exécution dans un binaire strippé.  
- **Number of section headers** : le nombre de sections. Un binaire non strippé en a typiquement entre 25 et 35 ; un binaire strippé en perd plusieurs.

### Table des sections

```bash
$ readelf -S keygenme_O0
```

Cette commande affiche toutes les sections du binaire. Sur un keygenme compilé avec GCC, les sections pertinentes pour le RE sont :

| Section | Rôle | Intérêt RE |  
|---|---|---|  
| `.text` | Code exécutable (instructions machine) | C'est ici que vit toute la logique du programme : `main`, `check_license`, `compute_hash`… |  
| `.rodata` | Données en lecture seule (constantes, chaînes) | Les chaînes trouvées par `strings` résident ici. Les constantes numériques aussi (seeds de hash, masques XOR). |  
| `.data` | Variables globales initialisées | Rarement intéressant sur un petit programme, mais peut contenir des tables ou des clés sur une cible plus complexe. |  
| `.bss` | Variables globales non initialisées | Allouée en mémoire mais absente du fichier (taille zéro sur disque). |  
| `.plt` / `.plt.sec` | Procedure Linkage Table | Stubs de redirection vers les fonctions de bibliothèques (`printf`, `strcmp`, `strlen`…). Chaque `call xxx@plt` passe par ici. |  
| `.got` / `.got.plt` | Global Offset Table | Table d'adresses résolues dynamiquement. En Full RELRO, elle est en lecture seule après le chargement. |  
| `.symtab` | Table de symboles | Noms de toutes les fonctions et variables. Absente après `strip`. |  
| `.strtab` | Table de chaînes de symboles | Les noms référencés par `.symtab`. |  
| `.debug_info` | Informations DWARF | Types, variables locales, numéros de ligne. Présente uniquement avec `-g`. |

> 💡 **Point clé** : la taille de `.text` donne une idée de la complexité du code. Comparez :  
> ```  
> keygenme_O0        .text : ~0x50B octets  
> keygenme_O2        .text : ~0x31C octets  (code plus compact après optimisation)  
> keygenme_O3        .text : ~0x31C octets  (même taille que -O2 ici ; sur des boucles plus complexes, -O3 peut être plus gros à cause du déroulage)  
> ```  
> Les tailles exactes dépendent de la version de GCC, mais la tendance est constante : `-O2` compacte le code, `-O3` peut le regonfler à cause du déroulage de boucles et de la vectorisation (chapitre 16).

### Segments (Program Headers)

```bash
$ readelf -l keygenme_O0
```

Les segments décrivent comment le loader mappe le fichier en mémoire. Les plus importants :

- **LOAD (R-X)** : le segment exécutable (contient `.text`, `.plt`). Permissions : lecture + exécution, pas d'écriture. NX est actif.  
- **LOAD (RW-)** : le segment de données (contient `.data`, `.bss`, `.got`). Permissions : lecture + écriture, pas d'exécution.  
- **INTERP** : chemin du dynamic linker (`/lib64/ld-linux-x86-64.so.2`).  
- **GNU_RELRO** : indique le segment en lecture seule après relocation (RELRO partiel ou complet).  
- **GNU_STACK** : permissions de la pile. Si `RWE` est absent (pas de flag `E`), NX protège la pile.

La séparation stricte entre segments exécutables (pas d'écriture) et segments inscriptibles (pas d'exécution) est la base de la protection NX. On le confirmera avec `checksec` en section 21.2.

---

## Étape 4 — `nm` : inventaire des symboles

Sur un binaire non strippé, `nm` liste toutes les fonctions et variables globales avec leurs adresses et leurs types.

```bash
$ nm keygenme_O0 | grep ' [Tt] '
```

Le filtre `[Tt]` sélectionne les symboles de type « texte » (fonctions). On s'attend à trouver :

```
0000000000001209 t rotate_left
0000000000001229 t compute_hash
00000000000012d8 t derive_key
0000000000001358 t format_key
00000000000013d1 t check_license
0000000000001460 t read_line
00000000000014e1 T main
```

Le `t` minuscule indique une fonction `static` (visibilité locale au fichier), tandis que le `T` majuscule de `main` indique un symbole global. Toutes les fonctions internes (`rotate_left`, `compute_hash`, `derive_key`, `format_key`, `check_license`, `read_line`) sont statiques — un choix courant en C qui limite l'exposition des symboles.

> ⚠️ **Attention** : sur `keygenme_strip`, cette commande ne retourne rien :  
> ```bash  
> $ nm keygenme_strip  
> nm: keygenme_strip: no symbols  
> ```  
> La table `.symtab` a été supprimée par `strip`. Il reste cependant les symboles dynamiques dans `.dynsym` (nécessaires pour le linking dynamique) :  
> ```bash  
> $ nm -D keygenme_strip  
> ```  
> Ceux-ci ne contiennent que les fonctions importées de la libc (`printf`, `strcmp`, `strlen`, `fgets`…), pas les fonctions internes du programme. C'est exactement ce qui rend le RE d'un binaire strippé plus difficile.

### Hiérarchie d'appel déduite

À partir des noms de fonctions, on peut déjà esquisser une hiérarchie d'appel probable :

```
main
 ├── read_line       (lecture d'entrée)
 ├── check_license   (vérification — cible principale)
 │    ├── compute_hash    (transformation du username)
 │    ├── derive_key      (dérivation de la clé attendue)
 │    └── format_key      (mise en forme XXXX-XXXX-...)
 └── printf          (affichage du résultat)
```

Cette esquisse est une *hypothèse* à confirmer dans Ghidra via les cross-references (section 21.3). Mais elle guide déjà l'analyse : on sait que `check_license` est le point névralgique.

---

## Étape 5 — `objdump` : aperçu du désassemblage

Sans ouvrir Ghidra, on peut obtenir un premier aperçu du code machine avec `objdump`. Cette étape est optionnelle dans un triage, mais utile pour vérifier une hypothèse rapide.

```bash
# Désassembler uniquement la fonction check_license
$ objdump -d -M intel --no-show-raw-insn keygenme_O0 | \
    sed -n '/<check_license>:/,/^$/p'
```

On observe le schéma classique d'une fonction en `-O0` :

1. **Prologue** : `push rbp` / `mov rbp, rsp` / `sub rsp, N` — mise en place du cadre de pile.  
2. **Corps** : les appels successifs à `compute_hash`, `derive_key`, `format_key` via `call`, puis l'appel à `strcmp@plt`.  
3. **Point de décision** : un `test eax, eax` suivi d'un `jne` (ou `jnz`) après le retour de `strcmp`. C'est le saut conditionnel qui sépare le chemin « clé valide » du chemin « clé invalide ».  
4. **Épilogue** : `leave` / `ret`.

En `-O0`, le code est verbeux mais très lisible : chaque variable locale est sur la pile, chaque appel de fonction est explicite. En `-O2`, le compilateur pourrait inliner certaines fonctions, utiliser des registres au lieu de la pile, et réordonner les instructions — rendant la lecture plus difficile (chapitre 16).

---

## Étape 6 — `ldd` : dépendances dynamiques

```bash
$ ldd keygenme_O0
    linux-vdso.so.1 (0x...)
    libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x...)
    /lib64/ld-linux-x86-64.so.2 (0x...)
```

Le binaire ne dépend que de la libc standard — pas de bibliothèque crypto externe, pas de framework réseau. Cela signifie que toute la logique de vérification est **implémentée en interne** dans le binaire. Il n'y a pas de `libssl`, pas de `libcrypto` — l'algorithme de hachage est custom, ce qu'on avait pressenti en voyant les noms `compute_hash` et `derive_key` dans `nm`.

> ⚠️ **Rappel sécurité** : `ldd` exécute partiellement le binaire via le dynamic linker. Sur un binaire potentiellement malveillant, préférez `objdump -p` ou `readelf -d` qui sont purement statiques :  
> ```bash  
> $ readelf -d keygenme_O0 | grep NEEDED  
>  0x0000000000000001 (NEEDED)  Shared library: [libc.so.6]  
> ```

---

## Synthèse du triage

Après ces quelques minutes d'inspection, voici ce que l'on sait sans avoir exécuté le binaire ni ouvert de désassembleur graphique :

| Information | Valeur | Source |  
|---|---|---|  
| Format | ELF 64-bit, x86-64, little-endian | `file` |  
| Linking | Dynamique (libc uniquement) | `file`, `ldd` |  
| PIE | Oui | `file` |  
| Symboles | Présents (debug + symtab) | `file`, `nm` |  
| Fonctions internes | `main`, `check_license`, `compute_hash`, `derive_key`, `format_key`, `rotate_left`, `read_line` | `nm` |  
| Format de la clé | `XXXX-XXXX-XXXX-XXXX` (hex, 19 caractères) | `strings` |  
| Validation du username | 3 à 31 caractères | `strings` |  
| Algorithme de clé | Hash custom → dérivation → formatage → `strcmp` | `strings` + `nm` + `objdump` |  
| Bibliothèques crypto | Aucune (algorithme interne) | `ldd` |  
| Message succès | `[+] Valid license! Welcome, %s.` | `strings` |  
| Message échec | `[-] Invalid license. Try again.` | `strings` |

Ce tableau constitue le **rapport de triage** du binaire. Sur une analyse professionnelle, il serait la première section d'un rapport formel. Pour notre formation, il sert de feuille de route : on sait exactement où chercher et quoi chercher dans les étapes suivantes.

La prochaine section (21.2) complètera ce triage en inventoriant les protections actives avec `checksec`, avant de plonger dans Ghidra pour localiser précisément la routine de vérification (21.3).

⏭️ [Inventaire des protections avec `checksec`](/21-keygenme/02-checksec-protections.md)
