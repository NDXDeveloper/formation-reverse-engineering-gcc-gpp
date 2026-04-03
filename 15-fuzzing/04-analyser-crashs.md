🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 15.4 — Analyser les crashs pour comprendre la logique de parsing

> 🔗 **Prérequis** : Section 15.2 (AFL++, structure du répertoire de sortie), Section 15.3 (libFuzzer, rapports de sanitizers), Chapitre 11 (GDB), Chapitre 14 (sanitizers)  
> 📦 **Binaire de démonstration** : les exemples de cette section utilisent le parseur `ch15-fileformat` (riche en crashs). Le keygenme `ch15-keygenme`, utilisé en section 15.2 pour le premier run, produit rarement des crashs mais son corpus est exploitable pour comprendre la logique de validation (voir la fin de cette section).

---

## Changer de regard sur les crashs

Dans un contexte de développement logiciel ou de recherche de vulnérabilités, un crash est un **problème à corriger** ou une **faille à exploiter**. En reverse engineering, un crash est avant tout une **source d'information**. C'est un des apports les plus précieux du fuzzing : chaque crash nous enseigne quelque chose sur la logique interne du programme, sur les hypothèses de son auteur, sur les formats qu'il attend et sur les chemins qu'il emprunte pour les traiter.

Un crash nous dit, en substance : « voici un input qui a traversé telle séquence de validations, emprunté telle branche, atteint telle fonction, et provoqué telle opération mémoire invalide à tel endroit précis du code ». En décomposant cette phrase, on reconstitue une tranche complète du comportement du programme — du point d'entrée jusqu'au point de crash.

Cette section présente une méthodologie systématique pour transformer les crashs bruts produits par AFL++ ou libFuzzer en **connaissances exploitables** sur la logique du binaire.

---

## Étape 1 — Inventaire et tri des crashs

Après une campagne de fuzzing, le répertoire de crashs peut contenir des dizaines, voire des centaines de fichiers. La première étape est de les trier pour éviter de passer du temps sur des doublons ou des crashs superficiels.

### Lister les crashs

Pour AFL++ :

```bash
$ ls -la out/default/crashes/
total 48
-rw------- 1 user user   24 Mar 15 14:23 id:000000,sig:11,src:000007,time:1842,...
-rw------- 1 user user   19 Mar 15 14:25 id:000001,sig:06,src:000012,time:3107,...
-rw------- 1 user user   31 Mar 15 14:31 id:000002,sig:11,src:000007,time:5891,...
-rw------- 1 user user   22 Mar 15 14:38 id:000003,sig:08,src:000019,time:8204,...
-rw------- 1 user user  142 Mar 15 14:52 id:000004,sig:06,src:000031,time:14320,...
```

Pour libFuzzer, les fichiers crash sont dans le répertoire courant :

```bash
$ ls crash-* oom-* timeout-*
crash-adc83b19e793491b1c6ea0fd8b46cd9f32e592fc  
crash-e7f6c011776e8db7cd330b54174fd76f7d0216b4  
oom-b3f2c8a901e43f2b7854da2608b1a210c67d907e  
```

### Décoder les noms de fichiers AFL++

Les noms de fichiers AFL++ encodent des métadonnées précieuses :

| Champ | Exemple | Signification |  
|-------|---------|---------------|  
| `id` | `000002` | Identifiant séquentiel du crash |  
| `sig` | `11` | Signal qui a tué le processus |  
| `src` | `000007` | ID de l'input parent (dans `queue/`) qui a été muté pour produire ce crash |  
| `time` | `5891` | Millisecondes écoulées depuis le début de la campagne |  
| `op` | `havoc` | Stratégie de mutation qui a produit ce crash |

Les signaux les plus fréquents et leur signification :

| Signal | Numéro | Cause typique | Intérêt RE |  
|--------|--------|---------------|------------|  
| `SIGSEGV` | 11 | Accès mémoire invalide (déréférencement nul, lecture/écriture hors limites) | Révèle les accès à des données contrôlées par l'input — souvent un indice sur la structure interne des buffers |  
| `SIGABRT` | 6 | Appel à `abort()` — typiquement déclenché par ASan, UBSan, un `assert()`, ou un `free()` sur un pointeur invalide | ASan : bug mémoire précis. `assert` : violation d'un invariant interne du programme |  
| `SIGFPE` | 8 | Division par zéro ou overflow arithmétique piégé | Révèle un calcul dépendant de l'input — souvent un champ de taille ou un compteur |  
| `SIGBUS` | 7 | Accès mémoire non aligné (rare sur x86-64, plus fréquent sur ARM) | Indique un cast vers un type aligné sur des données non alignées |  
| `SIGILL` | 4 | Instruction illégale — exécution de données comme du code | Corruption sévère du flux d'exécution — le plus souvent un stack buffer overflow qui a écrasé l'adresse de retour |

### Premier tri par signal

Un tri rapide par signal permet de prioriser l'analyse :

```bash
# Compter les crashs par signal
$ for f in out/default/crashes/id:*; do
    echo "$f" | grep -oP 'sig:\K[0-9]+'
  done | sort | uniq -c | sort -rn
     12 11
      5 06
      2 08
      1 04
```

Les crashs `sig:04` (SIGILL) sont les plus rares et souvent les plus intéressants — ils indiquent une corruption profonde du flux d'exécution. Les crashs `sig:11` sont les plus courants et méritent d'être sous-triés par adresse de crash (voir étape 2).

### Dédupliquer manuellement

AFL++ déduplique déjà les crashs par chemin d'exécution (deux inputs qui crashent au même endroit via le même chemin ne sont sauvegardés qu'une fois). Mais des crashs avec des chemins légèrement différents peuvent aboutir au même bug fondamental. Pour une déduplication plus agressive, on peut regrouper par adresse de crash :

```bash
# Obtenir l'adresse de crash pour chaque input (nécessite un build ASan)
$ for f in out/default/crashes/id:*; do
    ./target_asan "$f" 2>&1 | grep "pc 0x" | head -1
  done
```

Les crashs qui partagent la même adresse `pc` sont probablement des variantes du même bug. On peut se concentrer sur un représentant de chaque groupe.

---

## Étape 2 — Reproduire et caractériser un crash

Une fois un crash sélectionné, l'objectif est de comprendre **exactement ce qui s'est passé** entre l'entrée du programme et le moment du crash.

### Reproduire avec le binaire instrumenté

```bash
$ ./simple_parser_asan out/default/crashes/id:000000,sig:11,src:000007,time:1842,execs:52341,op:havoc,rep:8
```

Si le binaire a été compilé avec ASan, le rapport est détaillé et auto-explicatif. S'il s'agit d'un binaire sans sanitizer, on obtient simplement un `Segmentation fault` — moins informatif, mais reproductible.

> ⚠️ **Attention** — Un crash trouvé par AFL++ en mode normal (sans ASan) peut ne **pas** crasher quand il est rejoué sur un build ASan, et inversement. La raison : ASan modifie la disposition de la mémoire (ajout de *redzones* autour des allocations, *quarantine* des blocs libérés). Un accès hors limites de 1 octet peut tomber dans une zone valide sans ASan mais dans une redzone avec ASan. **Compilez toujours un build de triage avec ASan** pour une détection fiable, et rejouez-y les crashs trouvés sans ASan.

### Examiner l'input crash avec xxd

Avant même de lancer GDB, examiner l'input brut révèle souvent des indices :

```bash
$ xxd out/default/crashes/id:000000,sig:11,...
00000000: 5245 02ff 00ff ffff ffff ffff ffff ffff  RE..............
00000010: ffff ffff ffff ff00                      ........
```

Ce qu'on peut déduire immédiatement de cet hexdump :

- Les deux premiers octets sont `52 45` → `RE` en ASCII. Le magic number est correct — l'input a passé la première validation.  
- Le troisième octet est `02` → version 2. Le parseur a emprunté la branche v2.  
- Le quatrième octet est `ff` — possiblement un champ de taille ou un flag.  
- Les octets suivants sont majoritairement `ff` — des valeurs extrêmes, typiques des mutations du fuzzer qui cherche à déclencher des overflows.

Cette lecture rapide nous donne déjà une hypothèse : le crash est probablement lié au traitement de la branche v2 avec des valeurs de champs extrêmes.

### Examiner avec ImHex

Pour un examen plus structuré, ouvrir l'input dans ImHex (cf. Chapitre 6) et appliquer le pattern `.hexpat` du format, s'il existe déjà. Si le pattern est en cours de construction (ce qui est le cas pendant le RE), l'input crash est un excellent point de données pour le raffiner :

- Les octets qui correspondent à des champs connus se colorisent correctement.  
- Les octets qui déclenchent le crash sont ceux que le pattern ne couvre pas encore, ou dont les valeurs sortent des limites attendues.

---

## Étape 3 — Analyse dans GDB

La reproduction avec GDB est le cœur de l'analyse d'un crash. L'objectif est de remonter la chaîne causale : de l'instruction qui crashe jusqu'à la décision qui a mené le programme sur ce chemin.

### Charger le crash dans GDB

```bash
$ gdb -q ./simple_parser_asan
(gdb) run out/default/crashes/id:000000,sig:11,...
```

Le programme s'arrête au point de crash. Si vous utilisez GEF ou pwndbg (Chapitre 12), l'affichage est immédiatement riche : registres, stack, code désassemblé autour du point de crash.

### Examiner le point de crash

```
Program received signal SIGSEGV, Segmentation fault.
0x000000000040128a in parse_input (data=0x6020000000a0, len=24) at parse_input.c:22
22          char c = data[(unsigned char)data[5]];
```

Si les symboles de débogage sont présents (`-g`), GDB montre la ligne source exacte. Sans symboles, on obtient l'adresse et le désassemblage :

```
Program received signal SIGSEGV, Segmentation fault.
0x000000000040128a in ?? ()
(gdb) x/5i $rip-8
   0x401282:    movzx  eax, BYTE PTR [rbp-0x1]
   0x401286:    cdqe
=> 0x40128a:    movzx  eax, BYTE PTR [rdi+rax*1]
   0x40128e:    mov    BYTE PTR [rbp-0x2], al
   0x401291:    nop
```

L'instruction fautive est `movzx eax, BYTE PTR [rdi+rax*1]` — une lecture indexée dans le buffer `data` (pointé par `rdi`), avec un index provenant de `rax`. Si `rax` dépasse la taille du buffer, l'accès est hors limites.

### Inspecter les registres et la mémoire

```
(gdb) info registers rdi rax
rdi    0x6020000000a0   # Adresse du buffer data  
rax    0xff             # Index = 255 (provient de data[5] = 0xff)  

(gdb) print len
$1 = 24
```

L'index est 255, mais le buffer ne fait que 24 octets. Le programme accède à `data[255]` — bien au-delà de la zone allouée. L'octet `data[5]` vaut `0xff` dans l'input crash, et le code l'utilise directement comme index sans vérifier qu'il est inférieur à `len`.

### Remonter la stack trace

```
(gdb) backtrace
#0  0x000000000040128a in parse_input (data=0x6020000000a0, len=24) at parse_input.c:22
#1  0x00000000004013b7 in LLVMFuzzerTestOneInput (data=0x6020000000a0, size=24) at fuzz_parse_input.c:7
#2  0x000000000043d0a1 in fuzzer::Fuzzer::ExecuteCallback (...) at FuzzerLoop.cpp:611
```

La stack trace confirme le chemin : libFuzzer → harness → `parse_input`, ligne 22. Pas d'appels intermédiaires surprenants — le crash est directement dans la fonction cible.

### Examiner les conditions de branchement en amont

Le crash nous a appris *où* le programme échoue. Maintenant, il faut comprendre *comment* il est arrivé là — c'est-à-dire quelles conditions ont été satisfaites pour atteindre cette ligne.

On place un breakpoint au début de `parse_input` et on relance :

```
(gdb) break parse_input
(gdb) run out/default/crashes/id:000000,sig:11,...

Breakpoint 1, parse_input (data=0x6020000000a0, len=24) at parse_input.c:4
(gdb) next    # if (len < 4) → passé (len=24)
(gdb) next    # if (data[0] != 'R') → passé (data[0]='R')
(gdb) next    # if (data[1] != 'E') → passé (data[1]='E')
(gdb) next    # version = data[2] → version = 2
(gdb) next    # if (version == 1) → non pris
(gdb) next    # else if (version == 2) → pris
(gdb) next    # if (len < 16) → passé (len=24)
(gdb) next    # if (data[4] == 0x00 && len > 20) → data[4]=0x00, len=24 → pris
(gdb) next    # char c = data[(unsigned char)data[5]] → CRASH
```

On a maintenant le chemin complet :

```
Entrée
  → len ≥ 4         ✓
  → data[0] == 'R'  ✓
  → data[1] == 'E'  ✓
  → version == 2     ✓  (data[2] == 0x02)
  → len ≥ 16        ✓
  → data[4] == 0x00 ✓
  → len > 20        ✓
  → accès data[data[5]] sans vérification de bornes → CRASH si data[5] ≥ len
```

Ce chemin est une **spécification partielle** de la branche v2 du parseur. Chaque condition est un champ du format d'entrée avec ses contraintes. On vient de documenter, grâce à un seul crash, la structure du header et les conditions d'activation d'un chemin de traitement spécifique.

---

## Étape 4 — Extraire l'information RE de chaque crash

Chaque crash analysé produit des connaissances qui alimentent directement le processus de reverse engineering. Voici comment les organiser.

### Documenter le chemin d'exécution

Pour chaque crash significatif, noter :

- **L'offset et la nature du bug** — « buffer over-read à `parse_input+0x48`, index contrôlé par `data[5]` »  
- **Le chemin de conditions** — la séquence de tests passés pour atteindre le crash (comme ci-dessus)  
- **Les champs du format impliqués** — « octets 0-1 : magic 'RE', octet 2 : version, octet 4 : flag de mode, octet 5 : index/longueur »  
- **La taille minimale de l'input** — ici 21 octets (len > 20 est une condition)

### Reconstituer la structure du format

Au fil des crashs, une image du format d'entrée se dessine. Chaque crash ajoute des pièces au puzzle :

```
Offset  Taille  Champ               Contraintes connues
──────  ──────  ──────────────────  ─────────────────────────────
0x00    2       Magic               "RE" (0x52 0x45)
0x02    1       Version             1 ou 2 (d'autres valeurs = rejet)
0x03    1       (inconnu)           pas encore observé dans les crashs
0x04    1       Flag de mode        0x00 active le chemin étendu (v2)
0x05    1       Index/Longueur      utilisé comme index dans data[]
0x06    2       (inconnu)           ...
0x08    8       Payload v1          accédé si version==1, len≥8
0x08    ?       Payload v2          accédé si version==2, len≥16
```

Cette table est exactement le type d'information qu'on injectera ensuite dans un pattern ImHex (`.hexpat`), dans un script de parsing Python, ou dans les commentaires Ghidra pour renommer les variables et les champs de structures.

### Cartographier les branches du parseur

En accumulant les crashs et les inputs du corpus (pas seulement les crashs), on peut construire un **arbre des décisions** du parseur :

```
parse_input()
│
├─ len < 4 ? → retour -1 (rejet)
│
├─ data[0:2] != "RE" ? → retour -1 (rejet)
│
├─ version == 1
│   ├─ len < 8 ? → retour -1
│   └─ value > 1000 ? → mode étendu v1
│
├─ version == 2
│   ├─ len < 16 ? → retour -1
│   └─ data[4] == 0x00 && len > 20 ?
│       └─ accès data[data[5]]  ← BUG si data[5] >= len
│
└─ autre version → retour 0 (accepté silencieusement)
```

Cet arbre est une **reconstruction de la logique de contrôle** du parseur, obtenue sans lire une seule ligne de code source ni de désassemblage. En pratique, on vérifiera et complétera cet arbre en le confrontant au désassemblage dans Ghidra — mais le fuzzing a fourni le squelette.

---

## Étape 5 — Minimiser les inputs crash

Les crashs bruts produits par le fuzzer contiennent souvent des octets superflus — des résidus de mutations qui n'ont aucun impact sur le crash. Un input de 142 octets qui crashe pourrait se réduire à 22 octets essentiels. La minimisation produit un input **minimal** qui déclenche exactement le même crash, ce qui facilite l'analyse.

### Avec `afl-tmin` (AFL++)

```bash
$ afl-tmin -i out/default/crashes/id:000000,sig:11,... \
           -o crash_minimized.bin \
           -- ./simple_parser_afl @@
```

`afl-tmin` essaie progressivement de supprimer des octets, de remplacer des séquences par des zéros, et de raccourcir l'input, en vérifiant à chaque étape que le crash est toujours reproduit. Le résultat est le plus petit input possible qui provoque le même crash.

L'input minimisé est souvent spectaculairement plus court que l'original :

```bash
$ wc -c out/default/crashes/id:000000,sig:11,...
142
$ wc -c crash_minimized.bin
22
```

### Avec libFuzzer (`-minimize_crash`)

```bash
$ ./fuzz_parse_input -minimize_crash=1 -runs=10000 crash-adc83b19e...
```

libFuzzer tente de réduire l'input en effectuant des mutations qui préservent le crash. L'input minimisé est sauvegardé avec le préfixe `minimized-from-`.

### Pourquoi minimiser est crucial pour le RE

Un input minimisé de 22 octets est **directement interprétable** : chaque octet a un rôle, chaque modification d'un octet change le comportement. On peut alors procéder par substitution systématique pour identifier le rôle de chaque position :

```bash
# L'input minimisé :
$ xxd crash_minimized.bin
00000000: 5245 0200 00ff 0000 0000 0000 0000 0000  RE..............
00000010: 0000 0000 0000                           ......

# Changer l'octet 2 (version) de 0x02 à 0x01 :
$ printf '\x52\x45\x01...' > test_v1.bin
$ ./simple_parser_asan test_v1.bin
# → pas de crash : le chemin v1 est différent

# Changer l'octet 5 (index) de 0xff à 0x05 :
$ printf '\x52\x45\x02\x00\x00\x05...' > test_safe_index.bin
$ ./simple_parser_asan test_safe_index.bin
# → pas de crash : l'index 5 est dans les limites
```

Cette méthode de **perturbation d'un octet à la fois** transforme un crash en une carte précise des champs du format d'entrée. C'est un complément direct à l'analyse statique dans Ghidra.

---

## Étape 6 — Trier les crashs par classe de bug

Quand la campagne produit de nombreux crashs, il est utile de les regrouper par **classe de bug** plutôt que par signal brut. Chaque classe correspond à un type de faiblesse dans la logique du parseur et oriente l'analyse différemment.

### Accès hors limites en lecture (heap/stack-buffer-overflow READ)

C'est la classe la plus courante sur les parseurs. Le programme lit au-delà des limites d'un buffer, typiquement parce qu'un champ de l'input est utilisé comme index ou comme longueur sans validation.

**Ce que ça révèle** : un champ de l'input contrôle directement un accès mémoire. En identifiant quel octet de l'input correspond à l'index ou à la longueur, on documente un champ clé du format.

### Accès hors limites en écriture (heap/stack-buffer-overflow WRITE)

Plus rare et plus grave. Le programme écrit au-delà d'un buffer, corrompant potentiellement des données adjacentes ou le flux de contrôle.

**Ce que ça révèle** : une opération de copie ou de décodage dont la taille est contrôlée par l'input. Souvent liée à un champ « longueur du payload » non validé.

### Use-after-free

Le programme accède à un bloc mémoire déjà libéré. Typiquement, une structure est désallouée dans un chemin d'erreur mais un pointeur vers elle persiste dans un autre.

**Ce que ça révèle** : la logique de gestion d'erreurs du parseur — quels objets sont créés et détruits à quels moments. Utile pour comprendre le cycle de vie des structures internes.

### Lecture de mémoire non initialisée (MSan)

Le programme lit un octet qui n'a jamais été écrit. Pas de crash visible en conditions normales, mais MSan le signale.

**Ce que ça révèle** : un champ de la structure interne qui n'est pas initialisé avant d'être utilisé. Indique une hypothèse implicite du parseur sur l'ordre de traitement des champs.

### Division par zéro / overflow arithmétique (SIGFPE, UBSan)

Le programme effectue une division dont le diviseur provient de l'input, ou un calcul arithmétique dont le résultat dépasse les limites du type.

**Ce que ça révèle** : un champ numérique de l'input utilisé dans un calcul — souvent un compteur, une taille de bloc, ou un facteur de redimensionnement.

---

## Automatiser le triage avec un script

Quand le nombre de crashs est important, un script de triage automatique permet de les caractériser rapidement. Voici la logique générale d'un tel script (l'implémentation complète est disponible dans `scripts/triage.py`) :

```bash
# Pour chaque crash, extraire :
# 1. Le signal (depuis le nom de fichier AFL++ ou depuis l'exécution)
# 2. L'adresse du crash (depuis la sortie ASan ou GDB)
# 3. Le type de bug ASan (depuis le rapport)
# 4. La taille de l'input

$ for crash in out/default/crashes/id:*; do
    echo "=== $crash ==="
    echo "Taille: $(wc -c < "$crash") octets"

    # Extraire le type de bug ASan
    ./simple_parser_asan "$crash" 2>&1 | grep "^SUMMARY:" || echo "Pas de rapport ASan"

    echo ""
  done > triage_report.txt
```

Le résultat est un fichier de triage qui ressemble à :

```
=== out/default/crashes/id:000000,sig:11,... ===
Taille: 24 octets  
SUMMARY: AddressSanitizer: heap-buffer-overflow parse_input.c:22:20 in parse_input  

=== out/default/crashes/id:000001,sig:06,... ===
Taille: 19 octets  
SUMMARY: AddressSanitizer: heap-use-after-free parse_input.c:45:12 in cleanup_context  

=== out/default/crashes/id:000002,sig:11,... ===
Taille: 31 octets  
SUMMARY: AddressSanitizer: heap-buffer-overflow parse_input.c:22:20 in parse_input  
```

On voit immédiatement que les crashs 000000 et 000002 sont des variantes du même bug (même localisation), tandis que 000001 est un bug différent dans une autre fonction. On priorise : analyser en détail un représentant de chaque groupe, en commençant par les plus rares ou les plus profonds dans le code.

---

## Retour vers l'analyse statique : enrichir Ghidra

Les connaissances extraites des crashs alimentent directement le travail dans Ghidra (Chapitre 8). Voici les actions concrètes à effectuer après l'analyse d'un crash :

**Renommer les fonctions.** Si le crash a révélé qu'une fonction à l'adresse `0x4012a0` est une routine de décodage de payload v2, on la renomme `decode_payload_v2` dans Ghidra.

**Créer des types de données.** La table des champs reconstituée à l'étape 4 se traduit en une structure dans Ghidra :

```c
struct FileHeader {
    char magic[2];        // "RE"
    uint8_t version;      // 1 ou 2
    uint8_t reserved;     // pas encore compris
    uint8_t mode_flag;    // 0x00 = mode étendu
    uint8_t data_index;   // index dans le payload
    uint8_t unknown[2];   // à explorer
};
```

**Annoter les conditions de branchement.** Dans le listing assembleur, ajouter des commentaires sur les conditions identifiées : `/* version == 2 : branche v2 */`, `/* data[4] == 0x00 : active le mode étendu */`.

**Marquer les bugs.** Si un bug a été identifié (accès hors limites sans vérification), le signaler dans Ghidra avec un bookmark ou un commentaire `BUG: no bounds check on data[5]`. Ces annotations seront utiles si l'objectif final du RE est un audit de sécurité.

---

## En résumé

L'analyse des crashs suit un pipeline en six étapes :

1. **Inventaire et tri** — lister, compter par signal, dédupliquer par adresse.  
2. **Reproduction** — relancer l'input crash sur un build ASan, examiner le rapport.  
3. **Analyse GDB** — remonter la chaîne causale, identifier le chemin de conditions.  
4. **Extraction d'information RE** — documenter les champs du format, reconstruire l'arbre de décisions.  
5. **Minimisation** — réduire l'input au strict nécessaire pour isoler chaque champ.  
6. **Classification** — regrouper par classe de bug pour prioriser et éviter les doublons.

Chaque crash analysé est une tranche de compréhension du binaire. Accumulés et recoupés, ils produisent une cartographie détaillée de la logique de parsing — que la couverture de code, vue dans la section suivante, viendra compléter en révélant les zones que les crashs n'ont pas atteintes.

---

## Note : quand le fuzzer ne crashe pas (cas du keygenme)

Tous les binaires ne produisent pas de crashs. Le keygenme `ch21-keygenme`, fuzzé en section 15.2, est un programme simple qui compare des chaînes et retourne « valide » ou « invalide » — il ne manipule pas de buffers de façon dangereuse et ne crashe généralement pas.

Dans ce cas, l'information RE se trouve dans le **corpus**, pas dans les crashs. Chaque input du corpus représente une chaîne qui emprunte un chemin distinct dans la routine de vérification. En les examinant :

- Les inputs les plus courts qui atteignent les branches profondes révèlent les **préfixes et délimiteurs** attendus par la routine (par exemple, si tous les inputs avec plus de 10 edges commencent par `"KEY-"`, le préfixe est identifié).  
- Les inputs qui provoquent un code de retour différent (0 vs 1) permettent de distinguer les **chemins de succès et d'échec**.  
- La progression du corpus dans le temps (via les timestamps dans les noms de fichiers AFL++) montre dans quel ordre le fuzzer a « déverrouillé » les couches de la validation.

Cette analyse de corpus est un complément naturel à l'analyse des crashs — et dans certains cas, c'est la seule source d'information dynamique disponible.

---


⏭️ [Coverage-guided fuzzing : lire les cartes de couverture (`afl-cov`, `lcov`)](/15-fuzzing/05-coverage-guided.md)
