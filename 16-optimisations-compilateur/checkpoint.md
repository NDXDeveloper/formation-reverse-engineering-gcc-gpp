🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 16

## Objectif

Identifier **au moins 3 optimisations** appliquées par GCC sur le binaire `opt_levels_demo_O2` fourni, en comparant son désassemblage avec la version `opt_levels_demo_O0`.

Ce checkpoint valide votre capacité à reconnaître les transformations du compilateur dans un binaire réel — la compétence centrale de ce chapitre.

---

## Contexte

Vous disposez de deux variantes du même programme, compilées à partir du même code source (`opt_levels_demo.c`) :

| Binaire | Flags | Caractéristiques |  
|---|---|---|  
| `build/opt_levels_demo_O0` | `-O0 -g` | Référence non optimisée, symboles DWARF |  
| `build/opt_levels_demo_O2` | `-O2 -g` | Optimisations standard, symboles DWARF |

Si les binaires ne sont pas encore compilés :

```bash
cd binaries/ch16-optimisations/  
make s16_1  
```

---

## Consigne

Analysez le binaire `opt_levels_demo_O2` en le comparant avec `opt_levels_demo_O0`. Pour chaque optimisation identifiée, documentez :

1. **Le nom de l'optimisation** (inlining, magic number, cmov, déroulage, tail call, etc.).  
2. **Où elle se trouve** dans le désassemblage (adresse ou fonction concernée).  
3. **Ce que le code faisait en `-O0`** (le pattern « naïf »).  
4. **Ce que le code fait en `-O2`** (le pattern optimisé).  
5. **Comment vous l'avez reconnue** (quel indice visuel vous a mis sur la piste).

Vous devez en identifier **au moins 3**. Le binaire en contient bien plus — un analyste expérimenté peut en trouver 8 à 10 sans effort.

---

## Méthodologie suggérée

### Étape 1 — Comparer le nombre de fonctions

```bash
echo "=== Fonctions en O0 ==="  
nm build/opt_levels_demo_O0 | grep ' t \| T ' | sort  

echo ""  
echo "=== Fonctions en O2 ==="  
nm build/opt_levels_demo_O2 | grep ' t \| T ' | sort  
```

Notez les fonctions qui ont **disparu** entre `-O0` et `-O2`. Chaque disparition est une optimisation (inlining ou élimination de code mort).

### Étape 2 — Comparer le désassemblage de `main()`

```bash
# Méthode rapide avec le Makefile
make disasm_compare BIN=opt_levels_demo

# Ou manuellement
objdump -d -M intel build/opt_levels_demo_O0 | sed -n '/<main>:/,/^$/p' > /tmp/main_O0.asm  
objdump -d -M intel build/opt_levels_demo_O2 | sed -n '/<main>:/,/^$/p' > /tmp/main_O2.asm  
diff --color /tmp/main_O0.asm /tmp/main_O2.asm | less  
```

Parcourez `main()` en `-O2` et cherchez les patterns décrits dans ce chapitre :

- Un `imul` par une grande constante hexadécimale → magic number (section 16.6, idiome 1).  
- Un `cmp` + `cmovCC` → branchement éliminé (section 16.6, idiome 5).  
- L'absence de `call square` ou `call clamp` → inlining (section 16.2).  
- Un `lea` avec facteur d'échelle → multiplication par constante (section 16.6, idiome 4).  
- Un compteur incrémenté de 2+ dans une boucle → déroulage (section 16.3).  
- Un `jmp` en fin de fonction au lieu de `call` + `ret` → tail call (section 16.4).  
- Un `lea` + `movsxd` + `jmp rax` → jump table (section 16.6, idiome 8).

### Étape 3 — Examiner une fonction spécifique

Si `main()` est trop dense, examinez une fonction qui a survécu à l'inlining (si elle existe) ou comparez une fonction spécifique entre les deux versions :

```bash
# Chercher une fonction spécifique
objdump -d -M intel build/opt_levels_demo_O0 | grep -A 30 '<classify_grade>:'  
objdump -d -M intel build/opt_levels_demo_O2 | grep -A 30 '<classify_grade>:'  
```

### Étape 4 (optionnel) — Vérifier avec Ghidra

Importez les deux binaires dans Ghidra et comparez les graphes d'appels. Le graphe de `main()` en `-O2` devrait être nettement plus pauvre en XREF que celui en `-O0`.

---

## Ce que vous devriez trouver

Sans révéler les réponses exactes, voici les catégories d'optimisations présentes dans ce binaire. Vous devez en documenter au moins 3 parmi celles-ci :

- **Inlining** de fonctions `static` triviales et moyennes.  
- **Remplacement de division par magic number** (au moins une division par constante dans le code source).  
- **Conditional move** au lieu de branchement pour un if/else simple.  
- **Jump table** pour un switch dense.  
- **Allocation registre** — variables dans des registres au lieu de la pile.  
- **Propagation de constantes** — valeurs calculées à la compilation.  
- **Multiplication par constante via `lea`** au lieu de `imul`.  
- **`strlen` résolu à la compilation** pour les chaînes littérales.  
- **Remplacement de `printf` par `puts`** (si applicable à votre version de GCC).

---

## Format du livrable

Rédigez un court document (1–2 pages) structuré comme suit :

```
# Rapport de checkpoint — Chapitre 16

## Environnement
- GCC version : ...
- OS : ...
- Commande de compilation : make s16_1

## Optimisation 1 : [nom]
- Localisation : [fonction ou adresse]
- En O0 : [description du pattern naïf]
- En O2 : [description du pattern optimisé]
- Indice de reconnaissance : [ce qui vous a mis sur la piste]

## Optimisation 2 : [nom]
...

## Optimisation 3 : [nom]
...

## Observations supplémentaires (optionnel)
...
```

---

## Critères de validation

| Critère | Attendu |  
|---|---|  
| Nombre d'optimisations identifiées | ≥ 3 |  
| Chaque optimisation est correctement nommée | Oui |  
| Le pattern `-O0` est décrit | Oui |  
| Le pattern `-O2` est décrit | Oui |  
| L'indice de reconnaissance est pertinent | Oui |

Si vous avez identifié 3 optimisations avec les descriptions correctes, vous pouvez passer au chapitre 17. Si vous en avez trouvé 5 ou plus, vous maîtrisez le sujet.

---

## Pour aller plus loin

Si vous avez terminé rapidement, appliquez la même analyse aux autres binaires du chapitre :

- Comparez `inlining_demo_O0` et `inlining_demo_O2` — comptez les fonctions disparues.  
- Comparez `loop_unroll_vec_O2` et `loop_unroll_vec_O3` — cherchez les instructions SIMD.  
- Comparez `loop_unroll_vec_O3` et `loop_unroll_vec_O3_avx2` — cherchez les registres `ymm`.  
- Comparez `lto_demo_O2` et `lto_demo_O2_flto` — utilisez `make lto_compare`.  
- Comparez `gcc_idioms_O2` et `gcc_idioms_clang_O2` (si `clang` est installé) — cherchez les marqueurs de compilateur de la section 16.7.

---


⏭️ [Chapitre 17 — Reverse Engineering du C++ avec GCC](/17-re-cpp-gcc/README.md)
