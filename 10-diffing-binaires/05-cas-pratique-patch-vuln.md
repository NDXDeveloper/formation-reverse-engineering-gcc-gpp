🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 10.5 — Cas pratique : identifier une correction de vulnérabilité entre deux versions d'un binaire

> **Chapitre 10 — Diffing de binaires**  
> **Partie II — Analyse Statique**  
>  
> 📦 Binaires utilisés : `binaries/ch10-keygenme/keygenme_v1` et `binaries/ch10-keygenme/keygenme_v2`  
> 🧰 Outils : `radiff2`, BinDiff, Diaphora, Ghidra

---

## Contexte du scénario

Ce cas pratique simule un scénario d'analyse 1-day tel qu'on le rencontre dans l'industrie de la sécurité. Voici la situation :

Vous êtes analyste sécurité. L'éditeur d'un logiciel a publié une mise à jour accompagnée d'un bulletin de sécurité laconique : *« Correction d'une vulnérabilité de validation d'entrée dans le module d'authentification, pouvant mener à un contournement de la vérification. Sévérité : haute. »* Aucun détail technique supplémentaire n'est fourni — pas de CVE, pas de description de la faille, pas de crédit au chercheur.

Vous disposez des deux versions du binaire : `keygenme_v1` (version vulnérable) et `keygenme_v2` (version corrigée). Votre mission est d'identifier précisément la fonction modifiée, de comprendre la nature de la vulnérabilité corrigée, et de documenter vos conclusions.

Nous allons dérouler l'analyse étape par étape, en combinant les trois outils vus dans ce chapitre selon le workflow recommandé : triage rapide avec `radiff2`, vue d'ensemble avec BinDiff, analyse détaillée avec Diaphora.

---

## Étape 1 — Triage avec `radiff2`

Avant de lancer un désassembleur, prenons la mesure du changement. Quelques secondes suffisent.

### Quantifier la différence

```bash
radiff2 -s keygenme_v1 keygenme_v2
```

Sortie typique :

```
similarity: 0.992  
distance: 28  
```

Un score de similarité de 0.992 confirme ce que le bulletin de sécurité laissait entendre : le patch est chirurgical. Sur l'ensemble du binaire, seuls 28 octets diffèrent. Ce n'est pas une refactorisation majeure — c'est une correction ciblée, probablement localisée dans une ou deux fonctions.

### Localiser les zones modifiées

```bash
radiff2 keygenme_v1 keygenme_v2
```

La sortie hexadécimale montre les offsets exacts des octets modifiés. Observons la distribution de ces offsets : s'ils sont concentrés dans une plage étroite, le changement touche probablement une seule fonction. S'ils sont dispersés, le patch est plus étendu.

Dans notre cas, les différences se concentrent dans une zone restreinte — disons autour de `0x1180`–`0x11d0` — ce qui suggère une modification dans une seule fonction.

### Premier aperçu du code modifié

```bash
radiff2 -AC keygenme_v1 keygenme_v2
```

Ce mode lance l'analyse de fonctions sur les deux binaires et produit la liste des appariements. En filtrant les fonctions qui ne sont pas à 1.00 de similarité, on identifie rapidement la ou les fonctions touchées :

```
sym.check_serial  0x00001180 | sym.check_serial  0x00001180  (MATCH 0.82)  
sym.main          0x00001060 | sym.main          0x00001060  (MATCH 1.00)  
sym.usage         0x00001230 | sym.usage         0x00001230  (MATCH 1.00)  
sym.transform     0x00001140 | sym.transform     0x00001140  (MATCH 1.00)  
```

Le tableau est sans ambiguïté : seule `check_serial` a un score inférieur à 1.00 (0.82). Toutes les autres fonctions sont identiques. Nous avons notre cible.

> 💡 **Observation** — Le nom `check_serial` est visible ici parce que nos binaires d'entraînement ne sont pas strippés. En situation réelle, face à un binaire strippé, vous verriez `fcn.00001180` ou `sub_1180`. Le processus reste le même — c'est l'adresse et le score qui guident l'analyse, pas le nom.

**Bilan du triage** : en moins d'une minute et trois commandes, nous savons que le patch modifie une seule fonction (`check_serial` à `0x1180`), que le changement est modéré (similarité de 0.82) et que le reste du binaire est intact. Nous pouvons maintenant concentrer l'analyse.

---

## Étape 2 — Vue d'ensemble avec BinDiff

`radiff2` nous a donné la cible. BinDiff va nous montrer la structure du changement.

### Export et comparaison

Si ce n'est pas déjà fait, importez les deux binaires dans Ghidra, lancez l'auto-analyse, et exportez chacun au format BinExport (cf. section 10.2) :

```bash
# Après les exports depuis Ghidra
bindiff keygenme_v1.BinExport keygenme_v2.BinExport
```

Ouvrez le résultat dans l'interface BinDiff :

```bash
bindiff --ui keygenme_v1_vs_keygenme_v2.BinDiff
```

### Lecture de la vue d'ensemble

Le résumé statistique confirme les observations de `radiff2` : score de similarité global très élevé, une seule fonction marquée comme modifiée. Triez la table des fonctions appariées par similarité croissante : `check_serial` apparaît en tête avec le score le plus bas.

### Inspection du CFG

Double-cliquez sur la paire `check_serial`. BinDiff ouvre la vue côte à côte des graphes de flot de contrôle. Voici ce que l'on observe typiquement dans ce genre de patch :

**Version v1 (vulnérable) :**
Le CFG de `check_serial` comporte, disons, 6 blocs de base. Le flot est linéaire : le serial est transformé, puis comparé à une valeur attendue via un unique test suivi d'un branchement vers « succès » ou « échec ».

**Version v2 (corrigée) :**
Le CFG comporte désormais 8 blocs de base — deux de plus que la v1. Le code couleur de BinDiff révèle :

- **Blocs verts** (identiques) — les blocs d'entrée de la fonction (prologue), le calcul de transformation du serial, et les blocs de sortie (épilogue) n'ont pas changé.  
- **Blocs jaunes** (modifiés) — le bloc contenant la comparaison finale a été modifié. En examinant les instructions, on peut constater par exemple qu'un `je` (jump if equal) a été remplacé par une séquence plus complexe.  
- **Blocs rouges** (ajoutés) — deux nouveaux blocs apparaissent dans la v2. Ce sont les blocs qui n'existaient pas dans la v1 : typiquement, une vérification supplémentaire de la longueur de l'entrée et un branchement vers le chemin d'échec si cette vérification échoue.

Cette visualisation nous donne la structure du patch : la correction ajoute une validation qui n'existait pas dans la version originale. Avant de plonger dans le détail des instructions, passons dans Diaphora pour le diff de pseudo-code.

---

## Étape 3 — Analyse détaillée avec Diaphora

### Export et comparaison

Depuis Ghidra, exportez les deux binaires au format Diaphora (fichiers `.sqlite`), puis lancez la comparaison comme décrit en section 10.3.

### Localisation dans les résultats

Dans les résultats de Diaphora, `check_serial` apparaît dans l'onglet **Partial matches** — elle a été reconnue comme correspondante mais avec un score de similarité inférieur au seuil des « best matches ». C'est cohérent avec ce que `radiff2` et BinDiff nous ont montré.

### Le diff de pseudo-code

Sélectionnez la paire `check_serial` et ouvrez le diff de pseudo-code. C'est ici que Diaphora révèle toute sa valeur. Voici un exemple représentatif de ce que l'on pourrait observer (simplifié pour la clarté) :

**Version v1 (pseudo-code décompilé) :**

```c
int check_serial(char *input) {
    int transformed = transform(input);
    if (transformed == 0x5a42) {
        puts("Access granted!");
        return 1;
    }
    puts("Access denied.");
    return 0;
}
```

**Version v2 (pseudo-code décompilé) :**

```c
int check_serial(char *input) {
    size_t len = strlen(input);
    if (len < 4 || len > 32) {
        puts("Access denied.");
        return 0;
    }
    int transformed = transform(input);
    if (transformed == 0x5a42) {
        puts("Access granted!");
        return 1;
    }
    puts("Access denied.");
    return 0;
}
```

Le diff de Diaphora met en évidence les lignes ajoutées (en vert dans l'interface) : l'appel à `strlen`, la vérification `len < 4 || len > 32`, et le branchement vers le chemin d'échec. Le reste de la fonction est identique (en blanc).

### Interprétation de la vulnérabilité

Le diff de pseudo-code rend l'analyse limpide. Dans la version v1, la fonction `check_serial` passe l'entrée utilisateur directement à `transform()` sans vérifier sa longueur. La vulnérabilité dépend de ce que fait `transform()` — si cette fonction travaille avec un buffer de taille fixe en interne, une entrée trop longue provoque un dépassement de buffer. Une entrée trop courte peut également poser problème (accès hors limites si `transform` s'attend à un minimum de caractères).

La version v2 corrige cela en ajoutant une vérification de longueur **avant** l'appel à `transform()`. L'entrée doit faire entre 4 et 32 caractères, sinon la fonction retourne immédiatement en échec sans jamais atteindre le code vulnérable.

> 📝 **Note** — Dans ce scénario pédagogique, la vulnérabilité est volontairement simple à comprendre. En situation réelle, les patches sont parfois plus subtils : un changement de `<` en `<=` (correction d'un off-by-one), l'ajout d'un `NULL` check, le remplacement d'un `strcpy` par un `strncpy`, ou encore la modification d'un calcul d'index. La méthodologie reste identique — seule la complexité de l'interprétation change.

---

## Étape 4 — Vérification au niveau assembleur

Le pseudo-code nous a donné la compréhension sémantique. Vérifions au niveau assembleur pour être certains de l'interprétation. Depuis Ghidra (avec `keygenme_v1` ouvert), naviguez jusqu'à la fonction `check_serial` et examinez le listing.

**Version v1 — début de `check_serial` :**

```asm
check_serial:
    push   rbp
    mov    rbp, rsp
    sub    rsp, 0x20
    mov    qword [rbp-0x18], rdi      ; sauvegarde de input
    mov    rdi, qword [rbp-0x18]
    call   transform                   ; appel direct sans vérification
    ...
```

**Version v2 — début de `check_serial` :**

```asm
check_serial:
    push   rbp
    mov    rbp, rsp
    sub    rsp, 0x30
    mov    qword [rbp-0x28], rdi      ; sauvegarde de input
    mov    rdi, qword [rbp-0x28]
    call   strlen                      ; NOUVEAU : calcul de la longueur
    mov    qword [rbp-0x10], rax       ; stockage de len
    cmp    qword [rbp-0x10], 0x3
    jbe    .Ldenied                    ; NOUVEAU : len <= 3 → échec
    cmp    qword [rbp-0x10], 0x20
    ja     .Ldenied                    ; NOUVEAU : len > 32 → échec
    mov    rdi, qword [rbp-0x28]
    call   transform                   ; appel protégé par la vérification
    ...
```

L'assembleur confirme le pseudo-code : trois instructions nouvelles (`call strlen`, `cmp`+`jbe`, `cmp`+`ja`) et un branchement vers le chemin d'échec encadrent l'appel à `transform`. Le frame de pile a été agrandi (`0x20` → `0x30`) pour accommoder la variable locale `len`.

Ce sont exactement les blocs jaunes et rouges que BinDiff nous montrait dans la vue CFG.

---

## Étape 5 — Documentation des conclusions

L'analyse est terminée. Synthétisons les résultats sous une forme exploitable — c'est une étape souvent négligée mais essentielle dans un cadre professionnel.

### Résumé de l'analyse de patch

| Élément | Détail |  
|---------|--------|  
| **Binaire analysé** | `keygenme` (v1 → v2) |  
| **Fonction modifiée** | `check_serial` (adresse `0x1180`) |  
| **Nature du changement** | Ajout d'une vérification de longueur de l'entrée utilisateur |  
| **Vulnérabilité corrigée** | Absence de validation de la taille de l'entrée avant traitement par `transform()` |  
| **Type de vulnérabilité** | Validation d'entrée insuffisante (CWE-20), pouvant mener à un buffer overflow (CWE-120) |  
| **Impact potentiel** | Contournement de la vérification d'authentification, potentielle exécution de code |  
| **Correction appliquée** | Vérification `strlen(input)` avec bornes [4, 32] avant l'appel à `transform()` |  
| **Fonctions non modifiées** | `main`, `usage`, `transform` — identiques entre v1 et v2 |

### Classification CWE

La référence aux identifiants CWE (*Common Weakness Enumeration*) est une bonne pratique dans un rapport d'analyse. Elle permet de classer la vulnérabilité dans une taxonomie reconnue et facilite la communication avec les équipes de développement et de gestion des risques. Dans notre cas :

- **CWE-20** (*Improper Input Validation*) — la cause racine. L'entrée utilisateur n'est pas validée avant traitement.  
- **CWE-120** (*Buffer Copy without Checking Size of Input*) — la conséquence probable, si `transform()` copie l'entrée dans un buffer de taille fixe.

---

## Retour sur la méthodologie

Ce cas pratique illustre le workflow en entonnoir que nous avons construit au fil du chapitre :

1. **`radiff2`** — 30 secondes — a répondu à « combien de fonctions ont changé ? » et nous a donné la cible (`check_serial`).  
2. **BinDiff** — 5 minutes — a confirmé la cible et montré la structure du changement (blocs ajoutés, blocs modifiés) via la visualisation des CFG.  
3. **Diaphora** — 5 minutes — a fourni le diff de pseudo-code qui rend le changement immédiatement compréhensible sans avoir à décoder l'assembleur instruction par instruction.  
4. **Ghidra (assembleur)** — 5 minutes — a permis de vérifier l'interprétation au niveau le plus bas et de confirmer les détails techniques (taille du frame, opcodes exacts des sauts conditionnels).

Au total, l'identification complète de la vulnérabilité corrigée a pris environ 15 minutes. Sur un binaire strippé, plus gros, ou avec un patch plus subtil, le processus serait plus long, mais la méthodologie reste identique : triage, vue d'ensemble, analyse détaillée, vérification, documentation.

Ce qui est remarquable, c'est que nous n'avons pas eu besoin de comprendre l'intégralité du binaire. Nous n'avons pas analysé `main`, ni `transform`, ni `usage`. Le diffing nous a permis d'ignorer 100 % du code inchangé et de concentrer l'effort sur les quelques lignes qui comptent. C'est tout l'intérêt de cette technique.

---


⏭️ [🎯 Checkpoint : comparer `keygenme_v1` et `keygenme_v2`, identifier la fonction modifiée](/10-diffing-binaires/checkpoint.md)
