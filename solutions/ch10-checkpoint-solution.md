🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 📄 Solution — Checkpoint Chapitre 10 : Diffing de binaires

> ⚠️ **Spoiler** — Ne consultez cette solution qu'après avoir produit votre propre rapport.  
>  
> 📦 Binaires : `binaries/ch10-keygenme/keygenme_v1` et `binaries/ch10-keygenme/keygenme_v2`

---

## 1. Triage initial avec `radiff2`

### Score de similarité

```bash
$ radiff2 -s keygenme_v1 keygenme_v2
similarity: 0.992  
distance: 28  
```

**Interprétation** : les deux binaires sont quasi identiques — 99.2 % de similarité, seulement 28 octets de différence. Le patch est très ciblé. On s'attend à trouver une modification localisée dans une seule fonction, voire un seul bloc de base.

### Localisation brute des octets modifiés

```bash
$ radiff2 keygenme_v1 keygenme_v2
0x00001182 4883ec20 => 4883ec30
0x0000118e 48897de8 => 48897dd8
0x00001193 488b7de8 => 488b7dd8
0x00001197 e8a4ffffff => e870ffffff
0x0000119c 89...     => 4889...
...
```

Les offsets modifiés sont tous concentrés dans la plage `0x1180`–`0x11d0`, ce qui confirme qu'une seule zone du binaire est touchée. L'offset `0x1180` correspond au début d'une fonction.

### Identification de la fonction cible

```bash
$ radiff2 -AC keygenme_v1 keygenme_v2
```

Résultat filtré (fonctions avec similarité < 1.00) :

```
sym.check_serial  0x00001180 | sym.check_serial  0x00001180  (MATCH 0.82)
```

Toutes les autres fonctions sont à 1.00 :

```
sym.main          0x00001060 | sym.main          0x00001060  (MATCH 1.00)  
sym.transform     0x00001140 | sym.transform     0x00001140  (MATCH 1.00)  
sym.usage         0x00001230 | sym.usage         0x00001230  (MATCH 1.00)  
```

**Conclusion du triage** : une seule fonction est modifiée — `check_serial` — avec un score de 0.82. Le patch est chirurgical. Temps écoulé : moins d'une minute.

---

## 2. Vue d'ensemble avec BinDiff

### Export et comparaison

Après import et auto-analyse des deux binaires dans Ghidra, export au format BinExport :

```bash
# Exports réalisés depuis Ghidra : File → Export BinExport2…
$ ls *.BinExport
keygenme_v1.BinExport  keygenme_v2.BinExport

$ bindiff keygenme_v1.BinExport keygenme_v2.BinExport
```

### Statistiques globales

| Métrique | Valeur |  
|----------|--------|  
| Fonctions dans v1 | 4 (+fonctions de libc) |  
| Fonctions dans v2 | 4 (+fonctions de libc) |  
| Fonctions appariées identiques | 3 (+ libc) |  
| Fonctions appariées modifiées | 1 |  
| Fonctions non appariées | 0 |  
| Similarité globale | 0.99 |

### Détail de la fonction modifiée

| Champ | v1 | v2 |  
|-------|----|----|  
| Nom | `check_serial` | `check_serial` |  
| Adresse | `0x00001180` | `0x00001180` |  
| Similarité | 0.82 | — |  
| Blocs de base | 6 | 8 |  
| Arêtes CFG | 6 | 9 |  
| Algorithme d'appariement | name hash matching | — |

### Analyse du CFG dans BinDiff

La vue côte à côte des graphes de flot de contrôle montre :

- **4 blocs verts** (identiques) : le prologue de la fonction, le bloc d'appel à `transform()`, le bloc affichant `"Access granted!"` et le bloc affichant `"Access denied."`.  
- **1 bloc jaune** (modifié) : le bloc situé entre le prologue et l'appel à `transform()`. Dans la v1, ce bloc enchaînait directement sur `transform()`. Dans la v2, il a été modifié pour inclure un appel à `strlen()` et stocker le résultat.  
- **2 blocs rouges** (ajoutés dans v2) : deux nouveaux blocs de test. Le premier compare la longueur à 3 (`cmp`/`jbe`) et branche vers le chemin d'échec si la longueur est insuffisante. Le second compare à 32 (`cmp`/`ja`) et branche vers l'échec si la longueur est excessive.  
- **1 bloc jaune** supplémentaire : le bloc de branchement final qui existait dans les deux versions mais dont les offsets de saut ont été recalculés pour accommoder les nouveaux blocs.

La structure du patch est claire : deux blocs de validation ont été insérés entre le prologue et l'appel à `transform()`, créant un nouveau chemin d'échec précoce (*early exit*).

---

## 3. Analyse détaillée avec Diaphora

### Diff de pseudo-code

Après export des deux binaires au format SQLite depuis Ghidra et lancement de la comparaison, `check_serial` apparaît dans l'onglet **Partial matches** (ratio 0.82).

Le diff de pseudo-code côte à côte montre :

**Version v1 :**

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

**Version v2 (lignes ajoutées marquées +) :**

```c
int check_serial(char *input) {
+   size_t len = strlen(input);
+   if (len < 4 || len > 32) {
+       puts("Access denied.");
+       return 0;
+   }
    int transformed = transform(input);
    if (transformed == 0x5a42) {
        puts("Access granted!");
        return 1;
    }
    puts("Access denied.");
    return 0;
}
```

Le diff est sans ambiguïté : trois lignes de code ont été ajoutées au début de la fonction, avant toute logique de traitement. Ces lignes calculent la longueur de l'entrée et rejettent immédiatement toute entrée dont la taille n'est pas comprise entre 4 et 32 caractères inclus.

---

## 4. Vérification au niveau assembleur

### Version v1 — début de `check_serial`

```asm
0x00001180  push   rbp
0x00001181  mov    rbp, rsp
0x00001184  sub    rsp, 0x20              ; frame de 32 octets
0x00001188  mov    qword [rbp-0x18], rdi  ; sauvegarde input
0x0000118c  mov    rdi, qword [rbp-0x18]
0x00001190  call   sym.transform          ; appel DIRECT, pas de validation
0x00001195  mov    dword [rbp-0x4], eax
0x00001198  cmp    dword [rbp-0x4], 0x5a42
0x0000119f  jne    0x11b5                 ; si != 0x5a42 → "denied"
```

### Version v2 — début de `check_serial`

```asm
0x00001180  push   rbp
0x00001181  mov    rbp, rsp
0x00001184  sub    rsp, 0x30              ; frame agrandi à 48 octets (+16)
0x00001188  mov    qword [rbp-0x28], rdi  ; sauvegarde input (offset décalé)
0x0000118c  mov    rdi, qword [rbp-0x28]
0x00001190  call   strlen                 ; NOUVEAU : calcul de la longueur
0x00001195  mov    qword [rbp-0x10], rax  ; NOUVEAU : stockage de len
0x00001199  cmp    qword [rbp-0x10], 0x3
0x0000119e  jbe    0x11d0                 ; NOUVEAU : len <= 3 → denied
0x000011a0  cmp    qword [rbp-0x10], 0x20
0x000011a5  ja     0x11d0                 ; NOUVEAU : len > 32 → denied
0x000011a7  mov    rdi, qword [rbp-0x28]
0x000011ab  call   sym.transform          ; appel PROTÉGÉ par la validation
0x000011b0  mov    dword [rbp-0x4], eax
0x000011b3  cmp    dword [rbp-0x4], 0x5a42
0x000011ba  jne    0x11d0                 ; si != 0x5a42 → "denied"
```

### Changements observés instruction par instruction

| Élément | v1 | v2 | Signification |  
|---------|----|----|---------------|  
| Taille du frame | `sub rsp, 0x20` (32) | `sub rsp, 0x30` (48) | +16 octets pour la variable `len` (8 octets `size_t` + alignement) |  
| Offset de `input` | `[rbp-0x18]` | `[rbp-0x28]` | Décalage dû à l'agrandissement du frame |  
| Instruction après sauvegarde | `call sym.transform` | `call strlen` | Remplacement : validation avant traitement |  
| Stockage résultat `strlen` | — | `mov qword [rbp-0x10], rax` | Nouvelle variable locale `len` |  
| Test borne inférieure | — | `cmp [rbp-0x10], 0x3` + `jbe` | Rejet si longueur ≤ 3 (< 4 caractères) |  
| Test borne supérieure | — | `cmp [rbp-0x10], 0x20` + `ja` | Rejet si longueur > 32 caractères |  
| Appel à `transform` | Sans protection | Après les deux tests | Ne s'exécute que si 4 ≤ len ≤ 32 |

L'assembleur confirme intégralement l'interprétation du pseudo-code.

---

## 5. Caractérisation de la vulnérabilité

### Nature de la faille

Dans la version v1, la fonction `check_serial` transmet l'entrée utilisateur directement à `transform()` sans aucune vérification de longueur. Si `transform()` travaille avec un buffer interne de taille fixe (ce qui est courant pour une routine de transformation de serial), une entrée de longueur arbitraire — excessivement longue ou trop courte — peut provoquer :

- **Entrée trop longue** : un dépassement de buffer (*buffer overflow*) dans `transform()`, avec écrasement potentiel de l'adresse de retour sur la pile et possibilité d'exécution de code arbitraire.  
- **Entrée trop courte** : un accès hors limites en lecture (*out-of-bounds read*) si `transform()` accède à des indices au-delà de la longueur réelle de la chaîne, avec fuite potentielle d'informations de la pile ou crash.

### Classification CWE

- **CWE-20** — *Improper Input Validation* : la cause racine. L'entrée utilisateur n'est pas validée avant d'être transmise à une fonction de traitement.  
- **CWE-120** — *Buffer Copy without Checking Size of Input* : la conséquence probable si `transform()` copie l'entrée dans un buffer de taille fixe.

### Impact potentiel

Un attaquant peut fournir une entrée spécialement conçue pour déclencher le dépassement de buffer dans `transform()`. Selon la configuration des protections du binaire (canary, NX, ASLR — vérifiables avec `checksec`), cela peut mener à :

- Un crash du programme (déni de service).  
- Un contournement de la vérification d'authentification.  
- Une exécution de code arbitraire dans le cas le plus sévère.

---

## 6. Tableau de synthèse

| Élément | Détail |  
|---------|--------|  
| **Binaire** | `keygenme` (v1 vulnérable → v2 corrigé) |  
| **Similarité globale** | 0.992 (`radiff2`) / 0.99 (BinDiff) |  
| **Fonction modifiée** | `check_serial` @ `0x00001180` |  
| **Score de similarité (fonction)** | 0.82 |  
| **Blocs de base** | 6 (v1) → 8 (v2), +2 blocs de validation |  
| **Nature du changement** | Ajout de `strlen()` + double vérification de bornes [4, 32] avant appel à `transform()` |  
| **Vulnérabilité corrigée** | Absence de validation de longueur de l'entrée (CWE-20 / CWE-120) |  
| **Impact potentiel** | Buffer overflow → contournement d'authentification / exécution de code |  
| **Fonctions non modifiées** | `main` (1.00), `transform` (1.00), `usage` (1.00) |

---

## Récapitulatif du workflow appliqué

| Étape | Outil | Temps | Résultat obtenu |  
|-------|-------|-------|-----------------|  
| Triage | `radiff2 -s` / `-AC` | ~30 sec | Similarité 0.992, cible = `check_serial` |  
| Vue d'ensemble CFG | BinDiff | ~5 min | 4 blocs verts, 2 jaunes, 2 rouges — structure du patch |  
| Diff pseudo-code | Diaphora | ~5 min | 3 lignes ajoutées : `strlen` + tests de bornes |  
| Vérification ASM | Ghidra | ~5 min | Confirmation : `call strlen`, `cmp`+`jbe`, `cmp`+`ja` |  
| Documentation | — | ~5 min | Rapport complet avec tableau de synthèse |  
| **Total** | | **~20 min** | |

---


⏭️
