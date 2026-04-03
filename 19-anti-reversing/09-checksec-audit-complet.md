🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.9 — Inspecter l'ensemble des protections avec `checksec` avant toute analyse

> 🎯 **Objectif** : Intégrer `checksec` dans un workflow systématique de triage, savoir interpréter chaque ligne de sa sortie à la lumière des sections précédentes, et construire un réflexe de « fiche de protections » qui guide la stratégie d'analyse avant même d'ouvrir le désassembleur.

---

## Pourquoi `checksec` est le premier réflexe

Les sections 19.1 à 19.8 ont couvert les protections une par une. Dans la réalité, un binaire en combine plusieurs — parfois toutes. L'analyste qui ouvre directement Ghidra sans avoir caractérisé les protections risque de perdre du temps sur des fausses pistes : tenter un GOT overwrite sur un binaire Full RELRO, chercher des chaînes dans un binaire packé, poser des software breakpoints sur un binaire qui scanne `int3`.

`checksec` est l'outil qui répond en une seconde à la question : *à quoi ai-je affaire ?* Il inspecte les headers ELF, les segments, les sections et les flags d'un binaire et produit un résumé des protections actives. C'est le premier outil à lancer, avant `file`, avant `strings`, avant tout le reste.

## Installation

`checksec` existe sous deux formes principales :

**Version shell script (pwntools/checksec.sh)** — Le script Bash historique, souvent disponible dans les dépôts :

```bash
# Debian / Ubuntu
sudo apt install checksec

# Ou directement depuis le dépôt
git clone https://github.com/slimm609/checksec.sh
```

**Version Python (pwntools)** — Intégrée dans le framework `pwntools`, utilisable en ligne de commande ou en script :

```bash
pip install pwntools  
checksec ./binaire  
# ou depuis Python :
# from pwn import ELF
# elf = ELF('./binaire')
# print(elf.checksec())
```

Les deux versions produisent des résultats équivalents. La version `pwntools` est souvent plus à jour et s'intègre mieux dans les scripts d'automatisation.

## Anatomie d'une sortie `checksec`

Lançons `checksec` sur nos deux variantes extrêmes du chapitre :

```bash
$ checksec --file=build/vuln_max_protection
[*] 'build/vuln_max_protection'
    Arch:     amd64-64-little
    RELRO:    Full RELRO
    Stack:    Canary found
    NX:       NX enabled
    PIE:      PIE enabled
    FORTIFY:  Enabled

$ checksec --file=build/vuln_min_protection
[*] 'build/vuln_min_protection'
    Arch:     amd64-64-little
    RELRO:    No RELRO
    Stack:    No canary found
    NX:       NX disabled
    PIE:      No PIE
    FORTIFY:  Disabled
```

Chaque ligne correspond à une protection analysée dans ce chapitre. Détaillons ce que `checksec` vérifie pour chacune et comment il arrive à sa conclusion.

### Ligne `Arch`

L'architecture du binaire, déterminée depuis l'ELF header (`e_machine` et `e_ident[EI_DATA]`). `amd64-64-little` signifie x86-64 en little-endian. Cette ligne n'est pas une « protection » mais une information de contexte indispensable : elle détermine quel jeu d'instructions attendre dans le désassemblage et quelles conventions d'appel s'appliquent.

### Ligne `RELRO` (section 19.6)

`checksec` inspecte deux éléments :

1. La présence du segment `GNU_RELRO` dans les program headers (`readelf -l`). Si absent → `No RELRO`.  
2. La présence du flag `BIND_NOW` dans la section `.dynamic` (`readelf -d`). Si `GNU_RELRO` est présent mais pas `BIND_NOW` → `Partial RELRO`. Si les deux sont présents → `Full RELRO`.

| Sortie checksec | Segment GNU_RELRO | Flag BIND_NOW | GOT writable |  
|---|---|---|---|  
| No RELRO | Absent | Absent | Oui (toute la GOT) |  
| Partial RELRO | Présent | Absent | Oui (`.got.plt` seulement) |  
| Full RELRO | Présent | Présent | Non |

### Ligne `Stack` (section 19.5)

`checksec` cherche le symbole `__stack_chk_fail` dans la table de symboles dynamiques (`.dynsym`). Si ce symbole est importé, le binaire utilise des stack canaries. `checksec` ne distingue pas entre `-fstack-protector`, `-fstack-protector-strong` et `-fstack-protector-all` — les trois importent `__stack_chk_fail`.

Pour déterminer le niveau de protection précis, il faut inspecter le désassemblage : si chaque fonction a un accès `fs:0x28`, c'est `-fstack-protector-all`. Si seules les fonctions avec des buffers en ont un, c'est `-fstack-protector` ou `-strong`.

### Ligne `NX` (section 19.5)

`checksec` lit les flags du segment `GNU_STACK` dans les program headers :

- Flags `RW` (pas de `E`) → `NX enabled` — la pile n'est pas exécutable.  
- Flags `RWE` (avec `E`) → `NX disabled` — la pile est exécutable.  
- Segment `GNU_STACK` absent → comportement dépendant du kernel (généralement NX activé sur les systèmes modernes).

### Ligne `PIE` (section 19.5)

`checksec` vérifie le type de l'ELF dans le header (`e_type`) :

- `ET_DYN` (type 3) → `PIE enabled` — le binaire est position-independent.  
- `ET_EXEC` (type 2) → `No PIE` — le binaire est chargé à une adresse fixe.

Techniquement, une bibliothèque partagée (`.so`) est aussi de type `ET_DYN`. `checksec` et `file` distinguent les PIE des `.so` par la présence d'un point d'entrée (`e_entry`) non nul et d'un interpréteur (`PT_INTERP`).

### Ligne `FORTIFY` (bonus)

`FORTIFY_SOURCE` est une protection GCC qui remplace certaines fonctions de la libc (`memcpy`, `sprintf`, `strcpy`…) par des versions vérifiées qui contrôlent les tailles de buffers au runtime. `checksec` détecte sa présence en cherchant les symboles `__*_chk` (par exemple `__printf_chk`, `__memcpy_chk`) dans les imports dynamiques.

`FORTIFY_SOURCE` n'a pas été couvert en détail dans ce chapitre car son impact sur le RE est minime — il ajoute des appels à des variantes `_chk` au lieu des fonctions standards, ce qui est transparent pour l'analyste. Mais sa présence dans `checksec` indique un binaire compilé avec un niveau de durcissement élevé.

Pour l'activer :

```bash
gcc -D_FORTIFY_SOURCE=2 -O2 -o binaire source.c
```

Le flag nécessite au moins `-O1` pour fonctionner (le compilateur a besoin d'optimiser pour insérer les vérifications de taille).

## Ce que `checksec` ne détecte PAS

`checksec` est un outil d'inspection des protections compilateur et système. Il ne couvre pas les protections applicatives vues dans ce chapitre :

| Protection | Détectée par checksec ? | Comment la détecter |  
|---|---|---|  
| RELRO / Canary / NX / PIE | Oui | `checksec` |  
| Stripping | Non | `file` (`stripped` / `not stripped`) |  
| Packing (UPX, etc.) | Non | `strings`, entropie, `file`, sections absentes |  
| Obfuscation CFF/BCF | Non | Function Graph dans Ghidra, complexité cyclomatique |  
| Obfuscation LLVM (Hikari) | Non | `.comment`, patterns de dispatcher, chaînes chiffrées |  
| Détection ptrace | Non | `nm -D` (chercher `ptrace`), `strings` (`/proc/self/status`) |  
| Timing checks | Non | `nm -D` (chercher `clock_gettime`, `gettimeofday`) |  
| Scan int3 / checksum | Non | Analyse statique dans Ghidra |  
| Self-modifying code | Non | `nm -D` (chercher `mprotect`), analyse du CFG |

C'est pourquoi `checksec` est le *premier* outil, pas le *seul*. Il s'inscrit dans un workflow de triage plus large.

## Le workflow de triage complet intégrant `checksec`

Voici la routine systématique recommandée face à un binaire inconnu. Elle intègre `checksec` dans le workflow de triage du chapitre 5 (section 5.7) en y ajoutant la dimension anti-RE de ce chapitre.

### Étape 1 — Identification de base (10 secondes)

```bash
file binaire_cible
```

Que cherche-t-on : le format (ELF, PE, Mach-O), l'architecture (x86-64, ARM…), le linkage (dynamique, statique), le stripping (`stripped` / `not stripped`), la présence de debug info (`with debug_info`). Si `file` mentionne des section headers manquantes ou un linkage statique inattendu, suspecter un packing.

### Étape 2 — Protections compilateur (5 secondes)

```bash
checksec --file=binaire_cible
```

Que cherche-t-on : le niveau de RELRO, la présence de canaries, NX, PIE, FORTIFY. Ce résultat conditionne les stratégies d'analyse dynamique (adresses fixes ou non, GOT modifiable ou non, pile exécutable ou non).

### Étape 3 — Imports dynamiques (10 secondes)

```bash
nm -D binaire_cible | grep -iE 'ptrace|proc|time|mprotect|signal|dlopen'
```

Que cherche-t-on : les fonctions suspectes dans les imports. Chaque import raconte une histoire :

- `ptrace` → détection de débogueur (section 19.7)  
- `clock_gettime`, `gettimeofday` → potentiel timing check (section 19.7)  
- `mprotect` → code auto-modifiant ou manipulation de permissions (section 19.8)  
- `signal` → potentiel handler SIGTRAP pour anti-debug (section 19.7)  
- `dlopen`, `dlsym` → chargement dynamique de plugins ou de code (chapitre 22)

### Étape 4 — Chaînes et signatures (15 secondes)

```bash
strings binaire_cible | head -50  
strings binaire_cible | grep -iE 'upx|pack|proc/self|TracerPid|password|flag|key'  
strings binaire_cible | wc -l  
```

Que cherche-t-on : les signatures de packers (`UPX!`, `$Info:`), les chemins procfs (`/proc/self/status`), les chaînes révélatrices de la logique métier, et le nombre total de chaînes (une chute brutale indique un packing ou un chiffrement de chaînes).

### Étape 5 — Entropie et structure (15 secondes)

```bash
readelf -S binaire_cible | head -30  
binwalk -E binaire_cible  
```

Que cherche-t-on : des sections ELF normales ou absentes (packing), des noms de sections inhabituels (obfuscation), une entropie anormalement élevée (compression ou chiffrement).

### Étape 6 — Synthèse : la fiche de protections

À l'issue de ces cinq étapes (environ une minute au total), l'analyste dispose de suffisamment d'information pour remplir une fiche de protections qui guidera toute la suite de l'analyse :

```
╔══════════════════════════════════════════════════╗
║         FICHE DE PROTECTIONS — binaire_cible     ║
╠══════════════════════════════════════════════════╣
║ Format     : ELF 64-bit x86-64, dynamique        ║
║ Stripping  : stripped (pas de symboles)          ║
║ Packing    : non détecté                         ║
║ RELRO      : Full RELRO                          ║
║ Canary     : présent                             ║
║ NX         : activé                              ║
║ PIE        : activé                              ║
║ FORTIFY    : activé                              ║
║ Anti-debug : ptrace + /proc/self/status détectés ║
║ Timing     : clock_gettime importé (suspect)     ║
║ SMC        : mprotect non importé (peu probable) ║
║ Obfuscation: à confirmer dans Ghidra             ║
╠══════════════════════════════════════════════════╣
║ STRATÉGIE RECOMMANDÉE :                          ║
║ • Hardware breakpoints (anti int3 probable)      ║
║ • Frida pour contourner ptrace + timing          ║
║ • Adresses en offsets relatifs (PIE)             ║
║ • GOT non modifiable (Full RELRO)                ║
║ • LD_PRELOAD viable pour hooks                   ║
╚══════════════════════════════════════════════════╝
```

Cette fiche n'a pas besoin d'être formelle — un bloc-notes, un commentaire en tête d'un script Python, ou un fichier texte dans le répertoire de travail suffit. L'important est de l'avoir posée avant de commencer l'analyse en profondeur.

## Audit batch avec `checksec`

Quand on travaille avec un répertoire de binaires (comme nos variantes du chapitre), `checksec` peut être lancé en batch. Notre Makefile fournit une cible dédiée :

```bash
$ make checksec
```

Cette commande lance `checksec` sur chaque binaire du répertoire `build/` et affiche les résultats. C'est l'occasion idéale pour observer les différences entre les variantes et ancrer visuellement la correspondance entre les flags de compilation et la sortie `checksec`.

On peut aussi utiliser `checksec` en mode processus pour inspecter un binaire en cours d'exécution :

```bash
$ checksec --proc=12345
```

Cette variante inspecte les protections du processus avec le PID donné, en lisant `/proc/<pid>/maps` et les informations ELF. C'est utile pour vérifier les protections effectives d'un binaire qui a été unpacké en mémoire.

## Automatisation avec `pwntools`

Pour les analyses répétitives ou les pipelines automatisés, `pwntools` expose les résultats de `checksec` en Python :

```python
from pwn import ELF, context

context.log_level = 'warn'  # réduire le bruit

import os, json

results = []  
for fname in os.listdir('build'):  
    path = os.path.join('build', fname)
    if not os.path.isfile(path):
        continue
    try:
        elf = ELF(path)
        results.append({
            'name': fname,
            'arch': elf.arch,
            'relro': elf.relro or 'No RELRO',
            'canary': elf.canary,
            'nx': elf.nx,
            'pie': elf.pie,
        })
    except Exception:
        pass

# Affichage tabulaire
print(f"{'Binaire':<35} {'RELRO':<15} {'Canary':<8} {'NX':<8} {'PIE':<8}")  
print("=" * 74)  
for r in sorted(results, key=lambda x: x['name']):  
    print(f"{r['name']:<35} {r['relro']:<15} "
          f"{'Yes' if r['canary'] else 'No':<8} "
          f"{'Yes' if r['nx'] else 'No':<8} "
          f"{'Yes' if r['pie'] else 'No':<8}")
```

Ce script produit un tableau récapitulatif de toutes les variantes, directement exploitable dans un rapport d'analyse ou un pipeline CI/CD (chapitre 35, section 35.5).

## Relier `checksec` à la stratégie d'analyse

Le résultat de `checksec` ne dit pas seulement « quelles protections sont actives ». Il oriente les choix d'outils et de techniques pour toute la suite de l'analyse. Voici les décisions clés :

### RELRO → choix de la méthode de hooking

- `No RELRO` ou `Partial RELRO` → GOT overwrite possible. Utile pour l'instrumentation rapide.  
- `Full RELRO` → GOT verrouillée. Utiliser `LD_PRELOAD` ou Frida `Interceptor.attach` (inline hooking).

### Canary → comportement attendu sous GDB

- `Canary found` → Les fonctions avec buffers auront le pattern `fs:0x28`. Ne pas écraser la zone du canary lors de manipulations mémoire. Si le programme crash avec `*** stack smashing detected ***`, c'est le canary, pas un bug d'analyse.  
- `No canary found` → Pas de contrainte sur les manipulations de pile.

### NX → techniques d'injection possibles

- `NX enabled` → Impossible d'exécuter du code injecté sur la pile ou le tas. Les stubs custom doivent être placés dans des pages déjà exécutables.  
- `NX disabled` → Injection de code possible. Rare sur un binaire moderne — sa présence est suspecte (binaire ancien, challenge CTF, ou compilation spéciale).

### PIE → gestion des adresses

- `PIE enabled` → Raisonner en offsets relatifs. Calculer les adresses comme `base + offset`. Les scripts doivent déterminer la base au runtime. GDB avec GEF/pwndbg affiche la base automatiquement.  
- `No PIE` → Adresses absolues stables (hors ASLR sur pile/heap/libs). Les breakpoints par adresse sont reproductibles entre sessions.

### Combinaison complète → niveau de difficulté global

La combinaison des protections donne une indication du niveau de sophistication du binaire et de l'effort d'analyse requis :

- **Tout désactivé** (`No RELRO`, `No canary`, `NX disabled`, `No PIE`) → Binaire pédagogique, ancien, ou volontairement vulnérable. Analyse directe, toutes les techniques fonctionnent.  
- **Défauts modernes** (`Partial RELRO`, `Canary`, `NX`, `PIE`) → Binaire standard de production. Analyse normale avec des outils modernes, aucune difficulté particulière.  
- **Tout activé + strippé** (`Full RELRO`, `Canary`, `NX`, `PIE`, `FORTIFY`, strippé) → Binaire durci. L'analyse fonctionne mais nécessite des outils à jour et des scripts qui calculent les adresses dynamiquement.  
- **Tout activé + protections applicatives** (anti-debug, obfuscation, packing) → Binaire activement protégé. Le triage complet (pas seulement `checksec`) est indispensable pour planifier la stratégie avant de plonger.

---


⏭️ [🎯 Checkpoint : identifier toutes les protections du binaire `ch27-packed`, les contourner une par une](/19-anti-reversing/checkpoint.md)
