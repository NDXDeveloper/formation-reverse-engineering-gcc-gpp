🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 21.6 — Patching binaire : inverser un saut directement dans le binaire (avec ImHex)

> 📖 **Rappel** : l'utilisation d'ImHex (navigation, édition hexadécimale, bookmarks, recherche) est couverte au chapitre 6. Les opcodes des sauts conditionnels x86-64 pertinents sont récapitulés en section 21.4 et en Annexe A.

---

## Introduction

Les sections précédentes ont identifié le point de décision exact du keygenme : un saut conditionnel de quelques octets qui sépare le chemin « clé valide » du chemin « clé invalide ». En section 21.5, on a montré qu'on pouvait contourner ce saut *dynamiquement* en modifiant un registre dans GDB — mais cette modification disparaissait à la fin de la session.

Le patching binaire rend le contournement **permanent**. On modifie directement les octets du fichier ELF sur disque pour que le programme accepte *n'importe quelle clé*, sans débogueur, sans script, sans condition. C'est une modification chirurgicale : un ou deux octets suffisent.

Cette technique est un classique du RE. Elle ne remplace pas la compréhension de l'algorithme (qui viendra avec le keygen en section 21.8), mais elle est immédiate et démontre la puissance de la maîtrise du code machine. Dans un contexte professionnel, le patching est aussi utilisé pour désactiver temporairement des vérifications lors d'audits de sécurité ou pour contourner des bugs dans des binaires dont on n'a pas le source.

> ⚠️ **Rappel légal** : modifier un binaire dont vous n'êtes pas l'auteur peut constituer une violation du droit d'auteur ou des conditions de licence (chapitre 1, section 1.2). Ici, nous travaillons sur un binaire que nous avons compilé nous-mêmes à des fins éducatives — aucune restriction ne s'applique.

---

## Quel saut patcher ?

Nous avons identifié deux sauts conditionnels en section 21.3 :

1. **Dans `check_license`** : un `JNE` après `strcmp` qui saute vers `return 0` si les chaînes diffèrent.  
2. **Dans `main`** : un `JZ` après `CALL check_license` qui saute vers le chemin d'échec si la fonction retourne 0.

Les deux sont des cibles valides pour un patch, mais leurs conséquences diffèrent :

| Cible du patch | Effet | Avantage | Inconvénient |  
|---|---|---|---|  
| `JNE` dans `check_license` | `check_license` retourne toujours 1 | Patch localisé dans la fonction de vérification | Si `check_license` est appelée ailleurs, toutes les vérifications sont neutralisées |  
| `JZ` dans `main` | `main` emprunte toujours le chemin succès | Ne modifie pas `check_license` elle-même | Ne fonctionne que pour cet appel spécifique dans `main` |

Sur notre keygenme, `check_license` n'est appelée qu'une seule fois (depuis `main`), donc les deux approches sont équivalentes. Nous allons patcher le `JNE` dans `check_license` — c'est la cible la plus naturelle, car c'est l'embranchement qui décide directement du résultat de la vérification.

---

## Étape 1 — Localiser l'opcode dans le fichier

### Trouver l'offset du saut dans Ghidra

Dans Ghidra, naviguer vers le `JNE` dans `check_license` (celui qui suit le `TEST EAX, EAX` après `CALL strcmp@plt`). Le panneau Listing affiche, pour chaque instruction, son adresse virtuelle et ses octets machine. Par exemple :

```
0010143a    85 c0           TEST    EAX, EAX
0010143c    75 07           JNE     .return_zero
0010143e    b8 01 00 00 00  MOV     EAX, 0x1
00101443    eb 05           JMP     .epilogue
00101445    b8 00 00 00 00  MOV     EAX, 0x0       ; .return_zero
```

L'instruction qui nous intéresse est à l'adresse virtuelle `0x0010143c` et ses octets sont **`75 07`** :
- `75` = opcode de `JNE` (forme courte, déplacement relatif sur 1 octet).  
- `07` = déplacement de +7 octets en avant (depuis l'instruction suivante, soit `0x10143e + 7 = 0x101445`, l'adresse de `.return_zero`).

### Convertir l'adresse virtuelle en offset fichier

L'adresse affichée par Ghidra (`0x00101337`) est une adresse virtuelle dans l'espace d'adressage du processus. Pour patcher le fichier sur disque, il faut l'**offset dans le fichier**. Sur un binaire PIE typique, Ghidra utilise une image base de `0x00100000`, et la section `.text` commence souvent à l'offset fichier `0x1000` pour l'adresse virtuelle `0x00101000`. Le calcul est :

```
offset_fichier = adresse_virtuelle - base_section_virtuelle + offset_section_fichier
```

On peut obtenir ces valeurs avec `readelf` :

```bash
$ readelf -S keygenme_O0 | grep '\.text'
  [16] .text    PROGBITS    0000000000001120  00001120  0000050b  ...
```

La colonne « Address » (`0x1120`) est l'adresse virtuelle de début de `.text`, et la colonne « Offset » (`0x1120` aussi dans ce cas) est son offset dans le fichier. Sur un binaire PIE standard, les deux coïncident (l'offset fichier et l'adresse virtuelle relative à la base sont identiques).

Donc l'offset fichier de notre `JNE` est :

```
0x10143c - 0x100000 = 0x143c
```

> 💡 **Raccourci** : dans Ghidra, l'image base par défaut pour les binaires PIE ELF est `0x100000`. L'offset fichier est simplement `adresse_virtuelle - 0x100000`. Attention : ce raccourci ne fonctionne que si vous n'avez pas changé l'image base lors de l'import.

### Vérification avec `objdump`

On peut confirmer avec `objdump` en cherchant le pattern d'octets :

```bash
$ objdump -d -M intel keygenme_O0 | grep -A1 "call.*strcmp@plt" | head -4
    1435:   e8 d6 fc ff ff      call   1110 <strcmp@plt>
    143a:   85 c0               test   eax,eax
    143c:   75 07               jne    1445 <check_license+0x74>
```

`objdump` affiche directement les offsets fichier (pour un binaire PIE, les adresses affichées par `objdump` correspondent aux offsets dans la section). On confirme : **offset `0x143c`**, octets **`75 07`**.

---

## Étape 2 — Ouvrir le binaire dans ImHex

### Créer une copie de travail

On ne patche jamais l'original. Toujours travailler sur une copie :

```bash
$ cp keygenme_O0 keygenme_O0_patched
```

### Ouvrir dans ImHex

Lancer ImHex et ouvrir `keygenme_O0_patched`. L'éditeur hexadécimal affiche le contenu brut du fichier, octet par octet.

### Naviguer vers l'offset

Utiliser la fonction de navigation par offset :

1. **Edit → Go to...** (ou raccourci `Ctrl+G`).  
2. Entrer l'offset `0x143c`.  
3. ImHex positionne le curseur sur l'octet à cet offset.

On doit voir les octets `75 07` à cette position. Pour confirmer qu'on est au bon endroit, les octets précédents doivent être `85 C0` (le `TEST EAX, EAX`) et les octets suivants `B8 01 00 00 00` (le `MOV EAX, 1`).

> 💡 **Vérification contextuelle** : ne jamais se fier uniquement à l'offset calculé. Toujours vérifier le contexte environnant. Si les octets voisins ne correspondent pas à ce que Ghidra/objdump montrait, c'est qu'une erreur de calcul s'est produite.

### Poser un bookmark

Avant de modifier quoi que ce soit, on marque la position :

1. Sélectionner les deux octets `75 07`.  
2. Clic droit → **Add bookmark** (ou via le panneau Bookmarks).  
3. Nommer le bookmark : `JNE après strcmp — point de décision`.

Ce bookmark servira de repère visuel et documentera la modification dans le projet ImHex.

---

## Étape 3 — Choisir la technique de patch

Trois techniques classiques permettent de neutraliser un saut conditionnel. Chacune a un comportement et un effet différent.

### Technique A — Inverser la condition : `75` → `74`

On remplace l'opcode `JNE` (`75`) par `JZ` (`74`). Le saut existe toujours, mais sa condition est inversée :

- Avant : saut si les chaînes sont **différentes** (clé fausse → échec).  
- Après : saut si les chaînes sont **identiques** (clé correcte → échec).

Le résultat est un programme qui refuse les bonnes clés et accepte les mauvaises. C'est paradoxal mais pédagogiquement intéressant — et dans certains scénarios de RE, inverser un saut est exactement ce qu'on veut (par exemple, inverser un `JZ` qui mène au succès vers un `JNZ`).

```
Offset 0x143c : 75 → 74  
Octets modifiés : 1  
```

### Technique B — NOP le saut : `75 07` → `90 90`

On remplace les deux octets du `JNE` par deux `NOP` (No Operation, opcode `90`). Le saut disparaît complètement — l'exécution passe **toujours** séquentiellement à l'instruction suivante, qui est `MOV EAX, 1` (retourner 1, succès).

- Avant : saut conditionnel vers `return 0` si clé fausse.  
- Après : l'instruction de saut n'existe plus. L'exécution tombe toujours dans `return 1`.

```
Offset 0x143c : 75 07 → 90 90  
Octets modifiés : 2  
```

C'est la technique la plus propre pour neutraliser un saut : le comportement est prévisible (toujours le chemin séquentiel), et les `NOP` ne perturbent pas l'alignement du code qui suit.

### Technique C — Saut inconditionnel vers le succès : `75` → `EB`

On remplace l'opcode `JNE` (`75`) par `JMP` (`EB`). Le déplacement (`07`) reste inchangé. Le saut est maintenant **inconditionnel** — il est *toujours* pris, quelle que soit la valeur des flags.

- Avant : saut vers `return 0` seulement si clé fausse.  
- Après : saut vers `return 0` systématiquement → le programme rejette *toute* clé.

Attention : cette technique est un piège si on ne réfléchit pas à la cible du saut. Ici, le saut mène à `.return_zero` (échec), donc le rendre inconditionnel **aggrave** la situation au lieu de la résoudre. La technique C n'est utile que lorsque la cible du saut est le chemin de *succès*.

Par exemple, si dans `main` le code était :

```nasm
TEST    EAX, EAX  
JNZ     .label_success    ; saut vers succès si EAX ≠ 0  
```

Alors remplacer `JNZ` par `JMP` forcerait toujours le chemin de succès — ce serait la bonne application de la technique C.

### Résumé des trois techniques

| Technique | Modification | Résultat sur notre `JNE` | Recommandée ici ? |  
|---|---|---|---|  
| A — Inverser (`75` → `74`) | 1 octet | Accepte les mauvaises clés, rejette les bonnes | ❌ (effet inverse) |  
| B — NOP (`75 07` → `90 90`) | 2 octets | Accepte toute clé (tombe dans `return 1`) | ✅ |  
| C — JMP (`75` → `EB`) | 1 octet | Rejette toute clé (saut inconditionnel vers `return 0`) | ❌ (effet opposé) |

La **technique B** (NOP) est le bon choix pour notre cas. Elle est aussi la plus répandue dans la pratique du patching de crackmes.

---

## Étape 4 — Appliquer le patch dans ImHex

### Modification des octets

1. Le curseur est positionné sur l'offset `0x143c` (grâce au bookmark).  
2. Dans le panneau hexadécimal, cliquer sur l'octet `75`.  
3. Taper `90` — l'octet passe de `75` à `90`. ImHex le surligne en rouge pour signaler la modification.  
4. Le curseur avance automatiquement sur l'octet suivant (`07`).  
5. Taper `90` — l'octet passe de `07` à `90`.

Le résultat dans l'éditeur :

```
Avant : ... 85 C0 75 07 B8 01 00 00 00 ...  
Après : ... 85 C0 90 90 B8 01 00 00 00 ...  
```

Le `TEST EAX, EAX` est toujours là (il est inoffensif — il positionne les flags mais plus rien ne les lit). Les deux `NOP` ont remplacé le saut, et l'instruction suivante (`MOV EAX, 1`) est exécutée inconditionnellement.

### Sauvegarder

**File → Save** (ou `Ctrl+S`). ImHex écrit les modifications directement dans `keygenme_O0_patched`.

> 💡 **Astuce ImHex** : le panneau **Diff** permet de comparer le fichier modifié avec l'original. Si vous avez ouvert `keygenme_O0` et `keygenme_O0_patched` simultanément (dans deux onglets), **View → Diff** surligne les octets modifiés. C'est un excellent moyen de vérifier qu'on n'a touché que ce qu'on voulait.

---

## Étape 5 — Vérifier le patch

### Test fonctionnel

On rend le binaire patché exécutable (si nécessaire) et on le lance :

```bash
$ chmod +x keygenme_O0_patched
$ ./keygenme_O0_patched
=== KeyGenMe v1.0 — RE Training ===

Enter username: Alice  
Enter license key (XXXX-XXXX-XXXX-XXXX): AAAA-BBBB-CCCC-DDDD  
[+] Valid license! Welcome, Alice.
```

Le programme accepte une clé totalement fantaisiste. Le patch fonctionne.

On peut aussi vérifier que la bonne clé est toujours acceptée (elle devrait l'être, puisque le chemin de succès est désormais le seul chemin possible) :

```bash
$ ./keygenme_O0_patched
Enter username: Alice  
Enter license key (XXXX-XXXX-XXXX-XXXX): DCEB-0DFC-B51F-3428  
[+] Valid license! Welcome, Alice.
```

### Vérification par désassemblage

On confirme que le patch est correct en désassemblant la zone modifiée :

```bash
$ objdump -d -M intel keygenme_O0_patched | grep -A5 "call.*strcmp@plt"
    1435:   e8 d6 fc ff ff      call   1110 <strcmp@plt>
    143a:   85 c0               test   eax,eax
    143c:   90                  nop
    143d:   90                  nop
    143e:   b8 01 00 00 00      mov    eax,0x1
    1443:   eb 05               jmp    144a
```

Les deux `NOP` apparaissent à la place du `JNE`. Le `MOV EAX, 1` est maintenant la prochaine instruction exécutée après le `TEST` — exactement ce qu'on voulait.

### Vérification de l'intégrité globale

Pour s'assurer qu'on n'a pas accidentellement modifié d'autres octets :

```bash
$ cmp -l keygenme_O0 keygenme_O0_patched
  4920 165 220
  4921   7 220
```

`cmp -l` liste les octets qui diffèrent entre les deux fichiers, avec leurs positions (en décimal) et leurs valeurs (en octal). Position 5181 en décimal = `0x143d` en hexadécimal… ce qui est `0x143c` en indexation base 0 (cmp compte à partir de 1). Les valeurs `165` (octal) = `0x75` et `220` (octal) = `0x90`. Deuxième octet : `7` (octal) = `0x07` → `220` = `0x90`. Exactement nos deux modifications, et rien d'autre.

---

## Patch alternatif : cibler le `JZ` dans `main`

Pour illustrer l'importance du contexte, appliquons un patch au deuxième point de décision — le `JZ` dans `main` qui saute vers le chemin d'échec quand `check_license` retourne 0.

### Localiser le `JZ`

Dans Ghidra ou `objdump`, on repère le code dans `main` :

```
    15d6:   e8 f6 fd ff ff      call   13d1 <check_license>
    15db:   85 c0               test   eax,eax
    15dd:   74 22               je     1601 <main+0x120>
```

L'instruction `JZ` est à l'offset `0x15dd`, octets `74 22`.

### Appliquer le NOP

On remplace `74 1A` par `90 90` dans une nouvelle copie du binaire :

```bash
$ cp keygenme_O0 keygenme_O0_patched_main
```

Dans ImHex : naviguer à `0x15dd`, remplacer `74 22` par `90 90`, sauvegarder.

### Différence de comportement

Ce patch a le même effet visible (toute clé est acceptée), mais le mécanisme est différent :
- `check_license` retourne toujours sa vraie valeur (0 pour une mauvaise clé, 1 pour une bonne).  
- C'est `main` qui ignore le résultat et emprunte toujours le chemin séquentiel (succès).

La distinction est subtile mais importante sur des binaires plus complexes : si `check_license` avait des effets de bord (écriture dans un fichier de log, mise à jour d'un compteur de tentatives…), le patch dans `main` laisserait ces effets intacts, tandis que le patch dans `check_license` les contournerait aussi.

---

## Gestion des formes near (sauts longs)

Nos deux sauts (`75 07` et `74 1A`) sont des sauts **courts** (short jump) : l'opcode tient sur 1 octet et le déplacement sur 1 octet signé, pour un total de 2 octets. C'est le cas le plus fréquent dans les fonctions compactes.

Sur des fonctions plus longues, GCC peut émettre des sauts **near** (ou long) :

```nasm
0F 85 xx xx xx xx    JNE (near, rel32)    ; 6 octets
0F 84 xx xx xx xx    JZ  (near, rel32)    ; 6 octets
```

Le déplacement est sur 4 octets signés (portée ±2 Go). La technique de patch est la même, mais il faut adapter le nombre de NOP :

| Saut | Taille | Patch NOP |  
|---|---|---|  
| `75 xx` (short JNE) | 2 octets | `90 90` |  
| `74 xx` (short JZ) | 2 octets | `90 90` |  
| `0F 85 xx xx xx xx` (near JNE) | 6 octets | `90 90 90 90 90 90` |  
| `0F 84 xx xx xx xx` (near JZ) | 6 octets | `90 90 90 90 90 90` |

Alternativement, pour un saut near de 6 octets, on peut utiliser un NOP multi-octet. Le processeur x86-64 reconnaît des séquences NOP allant jusqu'à 15 octets. Pour 6 octets, une forme courante est `66 0F 1F 44 00 00` (un seul NOP de 6 octets). En pratique, 6 fois `90` fonctionne tout aussi bien — le processeur les exécute simplement plus vite avec un NOP long, ce qui est négligeable pour notre cas d'usage.

---

## Patching des variantes optimisées

### `-O2` avec `SETcc`

En section 21.4, nous avons vu que GCC en `-O2` peut remplacer le branchement par une instruction `SETE` :

```nasm
CALL    strcmp@plt  
TEST    EAX, EAX  
SETE    AL              ; AL = 1 si ZF=1, 0 sinon  
MOVZX   EAX, AL  
RET  
```

Il n'y a plus de saut à inverser. Pour patcher ce code, deux options :

**Option 1** — Remplacer `SETE AL` par `MOV AL, 1` :
```
Avant : 0F 94 C0    (SETE AL)  
Après : B0 01 90    (MOV AL, 1 ; NOP)  
```

`MOV AL, 1` s'encode sur 2 octets (`B0 01`). Comme `SETE AL` en fait 3, on comble avec un `NOP`.

**Option 2** — Remplacer le `TEST EAX, EAX` par `XOR EAX, EAX` :
```
Avant : 85 C0       (TEST EAX, EAX)  
Après : 31 C0       (XOR EAX, EAX)  
```

`XOR EAX, EAX` met `EAX` à zéro et positionne ZF = 1. L'instruction `SETE AL` voit ZF = 1 et met AL = 1. Le résultat : `check_license` retourne toujours 1, quelle que soit la valeur renvoyée par `strcmp`.

### `-O3` avec `CMOVcc`

Si le compilateur utilise un déplacement conditionnel :

```nasm
XOR     ECX, ECX  
TEST    EAX, EAX  
MOV     EAX, 0x1  
CMOVNE  EAX, ECX         ; si strcmp ≠ 0, EAX ← 0  
```

On peut NOPer le `CMOVNE` :
```
Avant : 0F 45 C1    (CMOVNE EAX, ECX)  
Après : 90 90 90    (NOP NOP NOP)  
```

Sans le `CMOVNE`, `EAX` conserve la valeur 1 chargée par le `MOV` précédent → la fonction retourne toujours 1.

---

## Synthèse

Le patching binaire est la démonstration la plus directe de la compréhension acquise lors de l'analyse statique et dynamique. En modifiant un ou deux octets à un emplacement précis du fichier, on altère fondamentalement le comportement du programme.

Voici le workflow complet, applicable à tout crackme :

```
1. Identifier le point de décision (Ghidra, section 21.3)
         ↓
2. Comprendre le sens du saut (section 21.4)
         ↓
3. Confirmer dynamiquement (GDB, section 21.5)
         ↓
4. Calculer l'offset fichier (readelf + Ghidra)
         ↓
5. Localiser les octets dans ImHex (Go to offset)
         ↓
6. Vérifier le contexte (octets voisins)
         ↓
7. Appliquer le patch (NOP, inversion, ou remplacement)
         ↓
8. Vérifier (test fonctionnel + désassemblage + cmp)
```

Le patch résout le problème de manière permanente, mais il a une limitation fondamentale : il ne produit pas de clé valide. Il contourne la vérification au lieu de la satisfaire. Pour n'importe quel username donné, le programme patché dira « clé valide » quelle que soit la clé saisie — mais on ne saura pas quelle est la *vraie* clé.

Les deux sections suivantes s'attaquent à ce problème de deux manières complémentaires : la résolution automatique par exécution symbolique avec angr (section 21.7), qui trouve la bonne clé sans comprendre l'algorithme en détail, et l'écriture d'un keygen (section 21.8), qui reproduit l'algorithme pour générer des clés valides à la demande.

⏭️ [Résolution automatique avec angr](/21-keygenme/07-resolution-angr.md)
