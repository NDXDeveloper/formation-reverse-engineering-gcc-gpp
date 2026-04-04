🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution du Checkpoint — Chapitre 2

> **Exercice** : Compiler `hello.c` avec `-O0 -g` (debug) et `-O2 -s` (release), puis comparer les tailles, sections et symboles avec `readelf`.

---

## Compilation

```bash
cd binaries/ch02-hello  
gcc -O0 -g -o hello_debug hello.c  
gcc -O2 -s -o hello_release hello.c  
```

Vérification fonctionnelle :

```bash
./hello_debug RE-101    # → Accès autorisé.
./hello_release RE-101  # → Accès autorisé.
```

---

## Étape 1 — Tailles

```bash
ls -lh hello_debug hello_release
```

| Variante | Taille |  
|----------|--------|  
| `hello_debug` | ~18 Ko |  
| `hello_release` | ~15 Ko |

Le binaire debug est environ 20 % plus gros, principalement en raison des sections `.debug_*` et des tables de symboles complètes (`.symtab`, `.strtab`).

```bash
size hello_debug hello_release
```

| Variante | `.text` | `.data` | `.bss` |  
|----------|---------|---------|--------|  
| `hello_debug` | 1789 | 616 | 8 |  
| `hello_release` | 1775 | 616 | 8 |

La section `.text` du release est légèrement plus petite (code optimisé plus compact). Les sections `.data` et `.bss` sont identiques car les données globales ne changent pas.

---

## Étape 2 — Nombre de sections

```bash
readelf -S hello_debug | grep -c '\['    # → 38  
readelf -S hello_release | grep -c '\['  # → 30  
```

Le build debug a **38 sections**, le release **30**. La différence de 8 sections correspond aux sections `.debug_*` et aux tables de symboles non dynamiques.

---

## Étape 3 — Sections `.debug_*`

```bash
readelf -S hello_debug | grep debug
```

Le build debug contient **6 sections DWARF** :
- `.debug_aranges` — plages d'adresses par unité de compilation  
- `.debug_info` — informations de débogage principales (types, fonctions, variables)  
- `.debug_abbrev` — abréviations utilisées dans `.debug_info`  
- `.debug_line` — correspondance adresse → ligne source  
- `.debug_str` — chaînes utilisées dans les DIE  
- `.debug_line_str` — chaînes supplémentaires pour les noms de fichiers

```bash
readelf -S hello_release | grep debug
# Aucun résultat
```

Le build release ne contient **aucune section debug** — elles n'ont jamais été générées (pas de flag `-g`).

---

## Étape 4 — Tables de symboles

```bash
readelf -S hello_debug | grep -E 'symtab|strtab'
```

| Section | Debug | Release |  
|---------|-------|---------|  
| `.symtab` | ✅ Présente | ❌ Supprimée par `-s` |  
| `.strtab` | ✅ Présente | ❌ Supprimée par `-s` |  
| `.shstrtab` | ✅ Présente | ✅ Présente (noms de sections, toujours nécessaire) |

```bash
readelf -s hello_debug | grep -w check
#    34: 0000000000001189    48 FUNC    GLOBAL DEFAULT   16 check
```

La fonction `check` est visible par son nom dans le build debug.

```bash
readelf -s hello_release
# Aucune sortie (pas de .symtab)
```

La fonction `check` n'est plus identifiable par son nom dans le build release.

**Mais les symboles dynamiques survivent dans les deux cas :**

```bash
readelf --dyn-syms hello_release | grep FUNC
# puts, strcmp, printf (imports depuis libc) — toujours visibles
```

C'est le point clé : `-s` supprime `.symtab` (vos fonctions internes) mais **préserve** `.dynsym` (fonctions importées depuis les `.so`). Les noms des imports comme `strcmp`, `puts`, `printf` restent disponibles en RE même sur un binaire strippé.

---

## Étape 5 — Type et protections

```bash
file hello_debug
# ELF 64-bit LSB pie executable, x86-64, [...], with debug_info, not stripped

file hello_release
# ELF 64-bit LSB pie executable, x86-64, [...], stripped
```

Les deux binaires sont des **PIE executables** (défaut de GCC moderne). La différence visible : `not stripped` vs `stripped`, et `with debug_info` dans le debug.

Les protections (PIE, NX, RELRO) sont identiques — elles ne dépendent pas de `-O` ni de `-g`/`-s`.

---

## Étape 6 — Désassemblage

```bash
objdump -d hello_debug | grep '<check>'
# 0000000000001189 <check>:
# (48 octets, prologue push rbp / mov rbp,rsp classique)
```

La fonction `check` est présente comme fonction séparée dans le build debug.

```bash
objdump -d hello_release | grep 'check'
# (aucun résultat)
```

Dans le build release (`-O2`), la fonction `check` a été **inlinée** dans `main` par l'optimiseur. L'appel à `strcmp` se retrouve directement dans le corps de `main`, sans `call check` dédié. De plus, les symboles ayant été supprimés par `-s`, aucune étiquette `<check>` n'apparaît.

---

## Étape 7 — Informations DWARF

```bash
readelf --debug-dump=info hello_debug | head -20
# Compile Unit, DW_TAG_subprogram "check", types, lignes...

readelf --debug-dump=decodedline hello_debug | head -10
# Table de correspondance : adresse → hello.c ligne 22, 23, 26...
```

Le build debug contient la correspondance complète entre chaque instruction machine et la ligne source correspondante.

```bash
readelf --debug-dump=info hello_release 2>&1
# Error: Section '.debug_info' was not dumped because it does not exist!
```

Le build release n'a aucune information DWARF.

---

## Étape 8 — Ce qui reste exploitable dans le release

```bash
strings hello_release | grep -E 'RE-101|Usage|autorisé|refusé'
# RE-101
# Usage: %s <mot de passe>
```

Le mot de passe `RE-101` est **toujours visible en clair** via `strings`. Le stripping supprime les noms de fonctions et de variables, mais ne masque pas les données constantes dans `.rodata`.

```bash
readelf -p .comment hello_release
# GCC: (Ubuntu ...) ...
```

La version du compilateur reste disponible dans `.comment`.

```bash
readelf -d hello_release | grep NEEDED
# [NEEDED] libc.so.6
```

Les dépendances sont toujours lisibles.

---

## Tableau récapitulatif complété

| Critère | `hello_debug` (`-O0 -g`) | `hello_release` (`-O2 -s`) |  
|---|---|---|  
| Taille du binaire | ~18 Ko | ~15 Ko |  
| Nombre de sections | 38 | 30 |  
| Sections `.debug_*` | ✅ Présentes (6 sections) | ❌ Absentes |  
| `.symtab` / `.strtab` | ✅ Présentes | ❌ Supprimées (strip) |  
| `.dynsym` / `.dynstr` | ✅ Présentes | ✅ Présentes |  
| `file` dit | `not stripped`, `with debug_info` | `stripped` |  
| Fonction `check` visible par nom | ✅ Oui (`readelf -s`) | ❌ Non |  
| Fonction `check` existe en tant que fonction | ✅ Oui (non inlinée en `-O0`) | ❌ Inlinée dans `main` par `-O2` |  
| Chaîne `"RE-101"` visible avec `strings` | ✅ Oui | ✅ Oui |  
| Noms des imports (`strcmp`, `puts`…) | ✅ Oui (`.dynsym`) | ✅ Oui (`.dynsym`) |  
| Version du compilateur (`.comment`) | ✅ Oui | ✅ Oui |

---

## Ce que ce checkpoint démontre

1. **Les flags de compilation** déterminent radicalement ce qui est disponible pour le reverse engineer, même à partir d'un source identique.  
2. **DWARF** (flag `-g`) est la source d'information la plus riche — et la première à disparaître en production.  
3. **Le stripping** (flag `-s`) supprime `.symtab` mais préserve `.dynsym` — les noms des fonctions importées restent accessibles.  
4. **L'optimisation** (flag `-O2`) peut éliminer des fonctions entières par inlining, modifiant la structure visible du code.  
5. **Les données constantes** (`.rodata`) survivent à toutes les transformations — `strings` reste un outil de premier recours.  
6. **Les mécanismes dynamiques** (PLT/GOT, symboles dynamiques) sont indispensables au runtime et ne peuvent pas être supprimés.

---

⏭️ [Chapitre 3 — Bases de l'assembleur x86-64 pour le RE](/03-assembleur-x86-64/README.md)
