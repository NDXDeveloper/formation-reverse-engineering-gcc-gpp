🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 25.3 — Confirmer l'interprétation avec AFL++ (fuzzing du parser)

> 🎯 **Objectif de cette section** : utiliser le fuzzing coverage-guided pour valider et compléter notre compréhension du format CFR. Les crashs et les chemins de code découverts par AFL++ révèlent les contraintes du parseur que l'analyse hexadécimale seule ne pouvait pas mettre en évidence — tailles maximales, valeurs interdites, ordre des validations, cas limites.

---

## Pourquoi fuzzer maintenant ?

À ce stade, on dispose d'un pattern `.hexpat` qui couvre la totalité des octets d'une archive CFR valide. On connaît la structure du header, des enregistrements et du footer. Mais cette compréhension a été construite exclusivement sur des fichiers **bien formés** — ceux que le binaire a lui-même produits. On n'a aucune idée de ce qui se passe quand le parseur rencontre des données invalides.

Or, les contraintes de validation du parseur font partie intégrante de la spécification d'un format. Un champ `num_records` qui ne peut pas dépasser une certaine valeur, un `name_len` de zéro qui provoque un comportement différent, un CRC incorrect qui entraîne un rejet — tout cela définit ce qu'est un fichier CFR valide au même titre que la disposition des champs.

Le fuzzing est l'outil idéal pour explorer ces contraintes. AFL++ va mutater nos archives de référence, soumettre les variantes au parseur, et observer lesquelles provoquent des crashs, des timeouts ou de nouveaux chemins de code. Chaque crash est une fenêtre sur un aspect du parseur qu'on n'avait pas encore cartographié.

C'est aussi un test de robustesse pour notre compréhension. Si le parseur accepte des fichiers que notre pattern `.hexpat` considérerait comme invalides, c'est que notre modèle est trop restrictif. Si le parseur rejette des fichiers que notre modèle considère valides, c'est qu'il existe des contraintes supplémentaires à documenter.

---

## Préparer le fuzzing

### Recompiler avec l'instrumentation AFL++

AFL++ a besoin d'une version du binaire compilée avec son propre compilateur instrumenté (`afl-gcc` ou `afl-clang-fast`). L'instrumentation insère des sondes à chaque branchement du code, ce qui permet à AFL++ de mesurer la couverture de code à chaque exécution.

```bash
$ cd binaries/ch25-fileformat/
$ CC=afl-gcc make clean fileformat_O0
```

Ou, si `afl-clang-fast` est disponible (recommandé pour de meilleures performances) :

```bash
$ CC=afl-clang-fast make clean fileformat_O0
```

On compile en `-O0` pour le fuzzing : les optimisations réduisent le nombre de branchements instrumentés et peuvent masquer des chemins intéressants. Le binaire résultant est plus lent mais offre une couverture plus granulaire.

> 💡 **Pourquoi `-O0` pour le fuzzing ?** En `-O2` ou `-O3`, le compilateur fusionne des branchements, élimine du code mort et inline des fonctions. Du point de vue du fuzzer, cela réduit le nombre de « embranchements » visibles et donc la capacité à distinguer des chemins d'exécution différents. Pour la découverte de bugs et de chemins, `-O0` est préférable. Pour le fuzzing de performance (throughput), on privilégierait `-O2` avec `afl-clang-lto`.

### Choisir la sous-commande cible

Notre binaire supporte plusieurs sous-commandes (`list`, `read`, `validate`, `unpack`). Chacune traverse le parseur différemment :

- **`validate`** est le choix le plus naturel : c'est la sous-commande qui vérifie le plus de contraintes (CRC header, CRC par record, reserved, footer). Elle parcourt l'intégralité du fichier et valide chaque invariant. C'est celle qui exposera le plus de logique de validation.  
- **`read`** traverse le parseur complet et affiche les contenus — utile pour détecter les bugs dans le décodage XOR.  
- **`list`** ne parcourt que les métadonnées — couverture plus faible mais exécution plus rapide.

On va fuzzer `validate` en priorité, puis éventuellement `read` dans un second temps.

### Construire le corpus initial

AFL++ a besoin d'un corpus de fichiers de départ (seed corpus) qui seront mutés. La qualité du corpus initial influence directement la vitesse de découverte. On va utiliser nos trois archives existantes plus quelques variantes minimales :

```bash
$ mkdir -p fuzz/input fuzz/output

# Corpus principal : nos archives générées
$ cp samples/demo.cfr          fuzz/input/seed_demo.cfr
$ cp samples/packed_noxor.cfr  fuzz/input/seed_noxor.cfr
$ cp samples/packed_xor.cfr    fuzz/input/seed_xor.cfr
```

Il est utile de créer aussi un fichier **minimal** — une archive avec un seul enregistrement de contenu vide ou très court. Plus le fichier de base est petit, plus les mutations d'AFL++ ont de chances de toucher des champs structurels plutôt que des données :

```bash
# Archive minimale : un seul record texte, pas de XOR, contenu court
$ echo -n "A" > /tmp/tiny.txt
$ ./fileformat_O0 pack /tmp/minimal.cfr /tmp/tiny.txt
$ cp /tmp/minimal.cfr fuzz/input/seed_minimal.cfr
```

### Écrire un dictionnaire

Un dictionnaire AFL++ est un fichier texte qui liste des tokens pertinents pour le format. Le fuzzer utilisera ces tokens comme blocs de construction lors des mutations, ce qui accélère considérablement la découverte de chemins dans les parseurs binaires.

Créons `fuzz/cfr.dict` à partir de ce qu'on sait du format :

```
# fuzz/cfr.dict — Tokens pour le format CFR

# Magic bytes
"CFRM"
"CRFE"

# Types d'enregistrements
"\x01"
"\x02"
"\x03"

# Flags courants
"\x00\x00"
"\x01\x00"
"\x02\x00"
"\x03\x00"

# Versions connues
"\x02\x00"

# Tailles fréquentes (little-endian)
"\x00\x00\x00\x00"
"\x01\x00\x00\x00"
"\x04\x00\x00\x00"
"\xff\xff\xff\xff"

# Clé XOR (pourrait apparaître dans les données)
"\x5a\x3c\x96\xf1"

# Padding nul
"\x00\x00\x00\x00\x00\x00\x00\x00"
```

Le dictionnaire contient les magic bytes, les valeurs de types connues, les flags, et des tailles limites (`0` et `0xFFFFFFFF`). Les valeurs extrêmes sont particulièrement intéressantes : elles déclenchent souvent des integer overflows ou des allocations échouées dans le parseur.

---

## Lancer AFL++

### Commande de base

AFL++ a besoin de savoir comment invoquer le binaire avec un fichier d'entrée. Le symbole `@@` sera remplacé par le chemin du fichier muté :

```bash
$ afl-fuzz -i fuzz/input \
           -o fuzz/output \
           -x fuzz/cfr.dict \
           -- ./fileformat_O0 validate @@
```

Décomposition des options :

| Option | Rôle |  
|--------|------|  
| `-i fuzz/input` | Répertoire du corpus initial |  
| `-o fuzz/output` | Répertoire de sortie (queues, crashs, hangs) |  
| `-x fuzz/cfr.dict` | Dictionnaire de tokens |  
| `--` | Séparateur entre les options AFL++ et la commande cible |  
| `./fileformat_O0 validate @@` | Commande à exécuter ; `@@` = fichier muté |

### Lecture du tableau de bord

Après quelques secondes, AFL++ affiche son tableau de bord :

```
       american fuzzy lop ++4.09a {default} (./fileformat_O0)
┌─ process timing ────────────────────────────────────┐
│        run time : 0 days, 0 hrs, 2 min, 34 sec      │
│   last new find : 0 days, 0 hrs, 0 min, 12 sec      │
│   last uniq crash : 0 days, 0 hrs, 1 min, 47 sec    │
├─ overall results ───────────────────────────────────┤
│  cycles done : 3                                    │
│ corpus count : 47       (au départ : 4)             │
│  saved crashes : 5                                  │
│  saved hangs : 0                                    │
├─ map coverage ──────────────────────────────────────┤
│    map density : 4.21% / 6.83%                      │
│ count coverage : 2.18 bits/tuple                    │
└─────────────────────────────────────────────────────┘
```

Les métriques clés à surveiller :

**`corpus count`** — le nombre de fichiers dans la queue. Chaque entrée représente un fichier qui a déclenché un nouveau chemin de code. Passer de 4 (nos seeds) à 47 signifie qu'AFL++ a trouvé 43 mutations qui couvrent des branches inédites du parseur.

**`saved crashes`** — le nombre de fichiers qui font planter le binaire. Chaque crash est une entrée dans `fuzz/output/default/crashes/`.

**`saved hangs`** — les fichiers qui provoquent un timeout. Un hang dans un parseur peut indiquer une boucle infinie déclenchée par un champ de taille malformé.

**`map density`** — le pourcentage de la carte de couverture rempli. Plus il est élevé, plus AFL++ a exploré de branches du binaire.

**`last new find`** — le temps écoulé depuis la dernière découverte d'un nouveau chemin. Quand cette valeur stagne longtemps (plusieurs heures), le fuzzer a probablement atteint un plateau.

### Combien de temps laisser tourner ?

Pour un parseur de cette taille, quelques minutes à une heure suffisent pour obtenir des résultats exploitables. Les critères d'arrêt raisonnables :

- Le compteur `cycles done` a dépassé 5–10 (AFL++ a parcouru le corpus plusieurs fois).  
- Le `last new find` dépasse 15–20 minutes sans nouveau chemin.  
- Plusieurs crashs ont été trouvés.

Pour un reverse de format, on ne cherche pas l'exhaustivité du fuzzing de sécurité. On cherche des crashs représentatifs qui révèlent la logique de validation.

---

## Analyser les crashs

### Trier et reproduire

Chaque crash se trouve dans `fuzz/output/default/crashes/` sous forme d'un fichier binaire. Reproduisons-les :

```bash
$ ls fuzz/output/default/crashes/
id:000000,sig:06,src:000001,time:12345,execs:67890,op:havoc,rep:4  
id:000001,sig:11,src:000003,time:23456,execs:98765,op:flip1,rep:2  
...
```

Le nom du fichier contient des métadonnées utiles : `sig:06` = signal 6 (SIGABRT, souvent un `assert` ou un `abort`), `sig:11` = signal 11 (SIGSEGV, accès mémoire invalide), `op:havoc` / `op:flip1` = la stratégie de mutation qui a produit le crash.

Reproduisons chaque crash :

```bash
$ for crash in fuzz/output/default/crashes/id:*; do
    echo "=== $crash ==="
    ./fileformat_O0 validate "$crash" 2>&1 | head -5
    echo "---"
done
```

### Classifier les crashs

Après reproduction, on classe les crashs par catégorie. Voici les types courants pour un parseur de format :

**Crashs liés aux tailles** — un `name_len` ou `data_len` absurde (ex : `0xFFFFFFFF`) provoque une allocation massive ou un dépassement de tampon. Cela nous apprend que le parseur ne vérifie pas (ou vérifie insuffisamment) les tailles avant de les utiliser.

```bash
# Examiner un crash dans ImHex ou xxd
$ xxd fuzz/output/default/crashes/id:000000 | head -8
```

Si on observe un `data_len` de `0xFFFF0000` dans le crash, cela confirme que le parseur utilise cette valeur directement dans un `malloc` ou un `fread` sans borne.

**Crashs liés au nombre de records** — un `num_records` très grand fait sortir le parseur de l'espace alloué. Cela nous indique s'il existe un maximum (la constante `MAX_RECORDS` du code source, qu'on ne connaît pas encore à ce stade).

**Crashs liés au magic** — un magic corrompu peut déclencher un chemin d'erreur intéressant. Si le parseur rejette proprement un magic invalide (message d'erreur, code de retour non nul) plutôt que de crasher, c'est un chemin « non-crash » qui reste informatif : il confirme que la validation du magic est la première étape du parsing.

**Crashs liés au footer** — un fichier tronqué avant le footer, ou un footer avec un `total_size` incohérent, peut révéler l'ordre dans lequel le parseur vérifie les invariants.

### Examiner un crash en détail

Prenons un crash concret. Le fichier crashant a un `num_records` muté à une valeur élevée (par exemple `0x00010000` = 65536). Le parseur alloue un tableau en conséquence, puis tente de lire 65536 en-têtes d'enregistrements dans un fichier qui n'en contient que 4. Résultat : lecture au-delà de la fin du fichier, corruption de mémoire, SIGSEGV.

Ce crash nous apprend deux choses sur le format :

1. **Le parseur fait confiance à `num_records` pour dimensionner ses structures** — il n'y a pas de mécanisme de découverte dynamique (le parseur ne scanne pas le fichier jusqu'au footer pour compter les records).  
2. **Il existe probablement un `MAX_RECORDS` dans le code** — en mutatant `num_records` progressivement, on peut trouver la valeur seuil au-delà de laquelle le parseur refuse le fichier (avant même de crasher).

Testons :

```bash
# Créer un fichier avec num_records = 1025 (copie de demo.cfr, mutation manuelle)
$ python3 -c "
import struct  
with open('samples/demo.cfr', 'rb') as f:  
    data = bytearray(f.read())
# num_records est à l'offset 0x08, little-endian u32
struct.pack_into('<I', data, 0x08, 1025)  
with open('/tmp/test_1025.cfr', 'wb') as f:  
    f.write(data)
"
$ ./fileformat_O0 validate /tmp/test_1025.cfr
```

Si 1025 est rejeté avec un message `"Too many records"` mais 1024 ne l'est pas, on a trouvé la constante `MAX_RECORDS = 1024` (le parseur accepte les valeurs ≤ 1024 et rejette au-delà). Cette information enrichit notre spécification.

---

## Exploiter la queue (non-crashs)

Les crashs ne sont pas les seuls résultats intéressants. Le répertoire `fuzz/output/default/queue/` contient tous les fichiers qui ont déclenché un nouveau chemin de code *sans* crasher. Ce sont des fichiers que le parseur a traités différemment de nos seeds initiaux.

```bash
$ ls fuzz/output/default/queue/ | wc -l
47
```

Certains de ces fichiers sont des archives avec des caractéristiques inhabituelles que le parseur gère silencieusement :

- Un enregistrement avec `data_len = 0` (contenu vide).  
- Un `name_len = 0` (nom vide).  
- Un type inconnu (ex : `type = 0xFF`) que le parseur accepte sans erreur.  
- Le flag `has_footer` désactivé (bit 1 à 0) — le parseur s'arrête après le dernier record.  
- Un `record.flags` non nul — le parseur l'ignore, mais le champ existe.

Chaque entrée de la queue peut être examinée dans ImHex avec notre pattern pour vérifier si elle reste structurellement cohérente. Les entrées qui sont parsées sans crash mais dont la structure dévie de notre modèle sont les plus instructives : elles révèlent des tolérances du parseur qu'on n'avait pas anticipées.

```bash
# Lister les fichiers de la queue triés par taille (les plus petits
# sont souvent les plus intéressants structurellement)
$ ls -lS fuzz/output/default/queue/ | tail -20
```

---

## Mesurer la couverture

Pour savoir quelles parties du parseur ont été explorées par le fuzzer (et surtout lesquelles ne l'ont pas été), on peut générer un rapport de couverture de code.

### Recompiler avec le profiling de couverture

```bash
$ make clean
$ CFLAGS="-O0 -g -fprofile-arcs -ftest-coverage" make fileformat_O0
```

### Rejouer le corpus

```bash
# Rejouer tous les fichiers de la queue et des crashs
$ for f in fuzz/output/default/queue/id:* fuzz/output/default/crashes/id:*; do
    ./fileformat_O0 validate "$f" 2>/dev/null
done
```

Chaque exécution incrémente les compteurs dans les fichiers `.gcda` générés par `gcc`.

### Générer le rapport

```bash
$ lcov --capture --directory . --output-file coverage.info
$ genhtml coverage.info --output-directory coverage_report/
$ xdg-open coverage_report/index.html
```

Le rapport HTML montre ligne par ligne quelles parties du code source ont été exécutées. Les zones non couvertes sont des indices de chemins que le fuzzer n'a pas réussi à atteindre — peut-être des branches protégées par des conditions complexes sur les CRC, ou des chemins liés à des sous-commandes non fuzzées (`unpack`, `read`).

> 📝 **Note** : cette étape nécessite d'avoir le code source, ce qui suppose un contexte de type audit ou CTF où le source est disponible à des fins de vérification. Dans un scénario de reverse pur sans source, la couverture n'est pas directement mesurable, mais les crashs et les chemins de la queue restent tout aussi informatifs.

---

## Synthèse : ce que le fuzzing a révélé

Après une session de fuzzing, notre compréhension du format CFR s'est enrichie de contraintes de validation qui n'étaient pas visibles dans l'analyse hexadécimale. Mettons à jour le carnet de notes :

```markdown
## Contraintes découvertes par fuzzing

### Limites de taille
- num_records : maximum 1024 (rejeté au-delà)
- name_len : accepté à 0 (nom vide, pas de crash)
- data_len : valeurs très grandes → crash (pas de borne explicite
  dans le parseur, vulnérabilité potentielle)

### Ordre de validation du parseur
1. Lecture et vérification du magic "CFRM"
2. Vérification de num_records ≤ MAX_RECORDS
3. Lecture séquentielle des records (confiance en num_records)
4. Pour chaque record : lecture de name_len, name, data_len, data, crc16
5. Si flag footer : lecture et vérification du footer

### Tolérances
- record.flags : ignoré par le parseur (toujours lu, jamais vérifié)
- record.type : valeurs inconnues acceptées sans erreur
- header.version : non vérifié (une version 0xFFFF est parsée normalement)
- Si le CRC du header est faux : le parseur affiche un avertissement
  mais continue le parsing (non bloquant en mode "read"/"list",
  bloquant en mode "validate")

### Comportement avec footer
- Si has_footer est à 0 : le parseur s'arrête après le dernier record
- Si has_footer est à 1 mais le footer est absent/tronqué :
  avertissement en mode validate, ignoré sinon
```

### Mettre à jour le pattern `.hexpat`

Les découvertes du fuzzing peuvent nécessiter des ajustements au pattern. Par exemple, si on a confirmé que `name_len = 0` est valide, on s'assure que notre pattern le gère (un `char name[0]` en `.hexpat` ne pose pas de problème). Si on a découvert que des types au-delà de `0x03` sont acceptés, on peut ajouter un cas `UNKNOWN` à l'enum :

```hexpat
enum RecordType : u8 {
    TEXT    = 0x01,
    BINARY  = 0x02,
    META    = 0x03
    // Valeurs > 0x03 acceptées par le parseur sans erreur
};
```

### Ce qu'il reste à confirmer

Le fuzzing ne résout pas tout. Certaines questions restent ouvertes et seront traitées dans les sections suivantes :

- **Précision du CRC-16** — le fuzzing a montré que des CRC incorrects sont rejetés en mode `validate`, mais on n'a pas encore implémenté le calcul exact pour le vérifier nous-mêmes.  
- **Comportement du XOR sur les cas limites** — un record de `data_len = 0` avec le flag XOR actif est-il traité sans erreur ?  
- **Contenu du champ `reserved`** — le fuzzing a montré qu'une valeur incohérente est détectée en mode `validate`, ce qui confirme que ce n'est pas un simple padding.

Ces points seront adressés dans la section 25.4 (parser Python), où l'implémentation complète servira de test définitif pour chaque contrainte.

---


⏭️ [Écrire un parser/sérialiseur Python indépendant](/25-fileformat/04-parser-python.md)
