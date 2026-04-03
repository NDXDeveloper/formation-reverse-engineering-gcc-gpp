🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint du Chapitre 2 — Compiler, comparer, comprendre

> **Objectif** : Valider votre compréhension de la chaîne de compilation GNU en compilant un même programme sous deux configurations extrêmes et en analysant concrètement les différences avec `readelf`.

---

## Contexte

Tout au long de ce chapitre, nous avons vu comment les flags de compilation transforment le binaire produit : les sections présentes, la quantité d'informations disponibles, la lisibilité du code machine. Ce checkpoint vous demande de le vérifier par vous-même en confrontant deux builds diamétralement opposées :

| Variante | Flags | Philosophie |  
|---|---|---|  
| **Build debug** | `-O0 -g` | Maximum d'informations, aucune optimisation — le rêve du reverse engineer |  
| **Build release** | `-O2 -s` | Optimisé et strippé — la réalité quotidienne du RE |

## Source

Utilisez le fichier `hello.c` fil conducteur du chapitre (disponible dans `binaries/ch02-hello/`) :

```c
/* hello.c — fil conducteur du Chapitre 2 */
#include <stdio.h>
#include <string.h>

#define SECRET "RE-101"

int check(const char *input) {
    return strcmp(input, SECRET) == 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <mot de passe>\n", argv[0]);
        return 1;
    }
    if (check(argv[1])) {
        printf("Accès autorisé.\n");
    } else {
        printf("Accès refusé.\n");
    }
    return 0;
}
```

## Étape 1 — Compiler les deux variantes

```bash
gcc -O0 -g -o hello_debug hello.c  
gcc -O2 -s -o hello_release hello.c  
```

Vérifiez que les deux binaires fonctionnent :

```bash
./hello_debug RE-101
# Accès autorisé.

./hello_release RE-101
# Accès autorisé.
```

Le comportement est identique. Ce sont les **métadonnées et la structure interne** qui diffèrent.

## Étape 2 — Comparer les tailles

```bash
ls -lh hello_debug hello_release
```

**Ce que vous devriez observer** : le binaire debug est significativement plus gros que le binaire release — souvent 2 à 4 fois. L'essentiel de cette différence vient des sections `.debug_*` (informations DWARF) présentes dans le build debug et absentes du build release.

Mesurez aussi avec `size`, qui affiche la taille des sections de code et de données :

```bash
size hello_debug hello_release
```

La commande `size` montre la taille de `.text`, `.data` et `.bss` — les sections qui comptent pour le code et les données effectifs. Vous constaterez que la section `.text` du build release est légèrement plus petite (code optimisé, plus compact) malgré un binaire global plus petit grâce à l'absence de debug et de symboles.

## Étape 3 — Comparer les sections

Listez les sections des deux binaires :

```bash
readelf -S hello_debug > sections_debug.txt  
readelf -S hello_release > sections_release.txt  
diff sections_debug.txt sections_release.txt  
```

**Ce que vous devriez observer :**

Le build debug contient des sections absentes du build release. Comptez les sections dans chaque binaire :

```bash
readelf -S hello_debug | grep -c '\['  
readelf -S hello_release | grep -c '\['  
```

Le build debug a typiquement 30 à 40 sections, le build release entre 25 et 30. Les sections manquantes dans le release sont principalement :

- **Les sections `.debug_*`** (`.debug_info`, `.debug_abbrev`, `.debug_line`, `.debug_str`, `.debug_aranges`, `.debug_frame`, `.debug_loc`, `.debug_ranges`…) — absentes car `-g` n'a pas été utilisé.  
- **`.symtab` et `.strtab`** — supprimées par le flag `-s` (stripping). Ce sont les tables de symboles non dynamiques.

Vérifiez explicitement :

```bash
# Sections debug
readelf -S hello_debug | grep debug
# Devrait lister 6-10 sections .debug_*

readelf -S hello_release | grep debug
# Aucun résultat

# Tables de symboles
readelf -S hello_debug | grep -E 'symtab|strtab'
# .symtab et .strtab présentes

readelf -S hello_release | grep -E 'symtab|strtab'
# Seulement .dynstr et .shstrtab — pas de .symtab
```

## Étape 4 — Comparer les symboles

```bash
# Build debug : symboles complets
readelf -s hello_debug | grep FUNC
# Vous devriez voir : check, main, et des fonctions CRT/libc

# Build release : symboles supprimés
readelf -s hello_release
# Aucune sortie (pas de .symtab)

# Mais les symboles dynamiques survivent dans les deux cas :
readelf --dyn-syms hello_debug | grep FUNC  
readelf --dyn-syms hello_release | grep FUNC  
# strcmp, printf, puts... visibles dans les deux binaires
```

**Point clé à retenir** : le stripping (`-s`) supprime `.symtab` (noms de vos fonctions internes) mais préserve `.dynsym` (noms des fonctions importées depuis les `.so`). C'est le principe vu en section 2.5.

## Étape 5 — Comparer le type et les protections

```bash
file hello_debug  
file hello_release  
```

Les deux binaires devraient être de type `PIE executable` (c'est le défaut de GCC moderne). La différence visible dans `file` est la mention `not stripped` (debug) vs `stripped` (release).

Si `checksec` est installé :

```bash
checksec --file=hello_debug  
checksec --file=hello_release  
```

Les protections (PIE, NX, RELRO, canary) sont identiques — elles ne dépendent pas de `-O` ni de `-g`/`-s` mais de flags dédiés (`-pie`, `-fstack-protector`, `-Wl,-z,relro,-z,now`).

## Étape 6 — Comparer le désassemblage

```bash
# Build debug : la fonction check est identifiable par son nom
objdump -d hello_debug | grep -A 20 '<check>'
# Prologue classique push rbp / mov rbp,rsp, variables sur la pile

# Build release : check est-elle toujours visible ?
objdump -d hello_release | grep '<check>'
```

**Ce que vous devriez observer** : dans le build release, la fonction `check` peut avoir disparu en tant que symbole nommé (car strippé), mais surtout elle a pu être **inlinée** dans `main` par l'optimiseur `-O2`. Si c'est le cas, le code de `check` (l'appel à `strcmp` et la comparaison) se retrouve directement dans le corps de `main`, sans `call` dédié.

Pour comparer la taille du code de `main` dans les deux variantes :

```bash
# Nombre d'instructions dans main (debug)
objdump -d hello_debug | sed -n '/<main>/,/^$/p' | grep -cE '^\s+[0-9a-f]+:'

# Dans le release, main n'a plus de label nommé (strippé).
# On peut compter les instructions totales de .text :
objdump -d -j .text hello_debug | grep -cE '^\s+[0-9a-f]+:'  
objdump -d -j .text hello_release | grep -cE '^\s+[0-9a-f]+:'  
```

Le build release contient typiquement moins d'instructions au total — le code est plus compact grâce aux optimisations.

## Étape 7 — Comparer les informations DWARF

```bash
# Build debug : DWARF complet
readelf --debug-dump=info hello_debug | head -40
# Vous voyez les DIE : DW_TAG_compile_unit, DW_TAG_subprogram "check", types, lignes...

readelf --debug-dump=decodedline hello_debug | head -20
# Table de correspondance adresse → ligne source

# Build release : rien
readelf --debug-dump=info hello_release 2>&1
# readelf: Error: Section '.debug_info' was not dumped because it does not exist!
```

Le build debug contient la correspondance complète entre chaque instruction machine et la ligne source correspondante. Le build release n'en a aucune trace.

## Étape 8 — Vérifier ce qui reste exploitable dans le build release

Même dans le binaire strippé et optimisé, certaines informations subsistent et sont exploitables en RE :

```bash
# Les chaînes littérales dans .rodata
strings hello_release | grep -E 'RE-101|Usage|autorisé|refusé'
# RE-101, les messages d'usage et d'accès sont toujours là !

# La section .comment (version du compilateur)
readelf -p .comment hello_release
# GCC: (Ubuntu XX.X.X) XX.X.X

# Les bibliothèques requises
readelf -d hello_release | grep NEEDED
# libc.so.6

# Le niveau de RELRO
readelf -d hello_release | grep -E 'BIND_NOW|FLAGS'
```

Le mot de passe `RE-101` est toujours visible en clair via `strings` — le stripping supprime les noms de fonctions et de variables, mais ne chiffre ni ne masque les données constantes dans `.rodata`. C'est pourquoi `strings` est l'un des premiers outils du workflow de triage rapide (Chapitre 5).

## Synthèse attendue

À l'issue de ce checkpoint, vous devriez pouvoir remplir ce tableau de mémoire :

| Critère | `hello_debug` (`-O0 -g`) | `hello_release` (`-O2 -s`) |  
|---|---|---|  
| Taille du binaire | __ Ko | __ Ko |  
| Nombre de sections | __ | __ |  
| Sections `.debug_*` | ✅ Présentes (__ sections) | ❌ Absentes |  
| `.symtab` / `.strtab` | ✅ Présentes | ❌ Supprimées (strip) |  
| `.dynsym` / `.dynstr` | ✅ Présentes | ✅ Présentes |  
| `file` dit | `not stripped` | `stripped` |  
| Fonction `check` visible par nom | ✅ Oui | ❌ Non |  
| Fonction `check` existe en tant que fonction | ✅ Oui (non inlinée) | ⚠️ Probablement inlinée |  
| Chaîne `"RE-101"` visible avec `strings` | ✅ Oui | ✅ Oui |  
| Noms des imports (`strcmp`, `puts`…) | ✅ Oui | ✅ Oui |

Remplissez-le avec vos propres valeurs mesurées. Si vos observations correspondent au schéma attendu, vous maîtrisez les fondamentaux de ce chapitre.

## Ce que ce checkpoint démontre

Ce simple exercice de comparaison illustre le spectre complet vu dans ce chapitre :

1. **La chaîne de compilation** (section 2.1) produit un binaire différent selon les flags, bien que le source soit identique.  
2. **Les fichiers intermédiaires** (section 2.2) — si vous ajoutez `-save-temps`, vous pouvez comparer les `.s` des deux builds et constater les transformations de l'optimiseur.  
3. **Le format ELF** (sections 2.3–2.4) organise le contenu en sections dont la présence dépend des options de compilation.  
4. **Les flags de compilation** (section 2.5) sont le levier principal qui détermine la difficulté d'une analyse RE.  
5. **DWARF** (section 2.6) est la source d'information la plus riche — et la première à disparaître dans un build de production.  
6. **Les mécanismes dynamiques** (sections 2.7–2.9) — PLT/GOT, symboles dynamiques — survivent au stripping car ils sont indispensables au runtime.

---

> ✅ **Checkpoint validé ?** Vous êtes prêt à aborder le Chapitre 3, où nous apprendrons à lire le code assembleur x86-64 que vous avez vu dans les désassemblages de ce chapitre.  
>  
> → Chapitre 3 — Bases de l'assembleur x86-64 pour le RE

⏭️ [Chapitre 3 — Bases de l'assembleur x86-64 pour le RE](/03-assembleur-x86-64/README.md)
