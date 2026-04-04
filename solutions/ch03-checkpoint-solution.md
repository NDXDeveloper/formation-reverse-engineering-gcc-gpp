🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution du Checkpoint — Chapitre 3

> **Exercice** : Annoter manuellement le désassemblage d'une fonction inconnue compilée par GCC en `-O0` (syntaxe Intel, x86-64 Linux) en appliquant la méthode en 5 étapes de la section 3.7.

---

## Source d'origine (non communiqué à l'étudiant)

Le listing correspond à la fonction `count_lowercase` du fichier `binaries/ch03-checkpoint/count_lowercase.c` :

```c
int count_lowercase(const char *str, int len) {
    int count = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            count++;
        }
    }
    return count;
}
```

Pour reproduire le listing : `cd binaries/ch03-checkpoint && make all && make disass`

---

## Étape 1 — Délimiter

- **Prologue** : `push rbp / mov rbp, rsp` — frame pointer classique, pas de `sub rsp`  
- **Épilogue** : `pop rbp / ret`  
- **Arguments** : 2 paramètres — `rdi` (pointeur 64 bits = `const char *`) et `esi` (entier 32 bits = `int`)  
- **Variables locales** : 4 (2 arguments spillés + 2 locales)

---

## Étape 2 — Structurer

| Saut | Cible | Direction | Rôle |  
|---|---|---|---|  
| `jmp 0x4011c1` | Test de boucle | Vers le bas | Saut initial vers le test (pattern `for`) |  
| `jle 0x4011bd` | Incrémentation | Vers le bas | Court-circuit du `&&` (1ère condition fausse) |  
| `jg 0x4011bd` | Incrémentation | Vers le bas | Court-circuit du `&&` (2e condition fausse) |  
| `jl 0x401191` | Corps de boucle | **Vers le haut** | **Boucle** — retour au corps si `i < len` |

Structure : boucle `for` avec test en bas, corps contenant un `if` à double condition (pattern `&&`).

---

## Étape 3 — Caractériser

- **Aucun `call`** → fonction autonome, pas d'appels externes  
- **Constantes clés** : `0x60` (96 = `'a' - 1`) et `0x7a` (122 = `'z'`) → bornes des minuscules ASCII  
- **Pattern `&&`** : les deux `jle`/`jg` sautent vers la **même cible** (`0x4011bd`) → court-circuit du ET logique  
- **Rechargement intégral** : GCC `-O0` recharge `str[i]` entièrement pour la 2e condition (5 instructions au lieu de réutiliser le registre)

---

## Étape 4 — Carte des variables

| Offset | Taille | Registre d'origine | Nom | Type | Rôle |  
|---|---|---|---|---|---|  
| `[rbp-0x18]` | 8 octets | `rdi` | `str` | `const char *` | Pointeur vers le buffer |  
| `[rbp-0x1c]` | 4 octets | `esi` | `len` | `int` | Longueur du buffer |  
| `[rbp-0x08]` | 4 octets | — | `count` | `int` | Compteur de minuscules |  
| `[rbp-0x04]` | 4 octets | — | `i` | `int` | Index de boucle |

---

## Étape 5 — Pseudo-code C reconstruit

```c
int count_lowercase(const char *str, int len) {
    int count = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            count++;
        }
    }
    return count;
}
```

---

## Points d'attention pédagogiques

1. **Le `&&` en assembleur** : deux `cmp`/`jXX` consécutifs sautant vers la même cible. Dès qu'une condition est fausse, on court-circuite — signature classique du ET logique.

2. **L'inversion des conditions** : le `if (c >= 'a')` en C produit `cmp al, 0x60 / jle skip` en assembleur. GCC compare avec `'a' - 1` et saute si `<=` (inverse de `>=`). Le saut va vers le code qui saute le `count++`, pas vers le code qui l'exécute.

3. **Le rechargement `-O0`** : chaque sous-expression C est compilée indépendamment. `str[i]` est recalculé entièrement pour la seconde comparaison. En `-O2`, les 5 instructions de rechargement disparaissent.

4. **`movzx` vs `movsx`** : `movzx eax, byte [rax]` lit un octet non signé (zero-extend). Pour des caractères ASCII (0–127), la distinction avec `movsx` (sign-extend) n'a pas d'impact, mais `movzx` confirme que GCC traite `char` comme potentiellement non signé pour la comparaison de plage.

5. **`movsxd rdx, eax`** : extension signée de `int i` (32 bits) vers 64 bits pour l'arithmétique de pointeur. Nécessaire car `str + i` est un calcul sur un pointeur 64 bits.

---

⏭️ [Chapitre 4 — Mise en place de l'environnement de travail](/04-environnement-travail/README.md)
