🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 15.6 — Corpus management et dictionnaires custom

> 🔗 **Prérequis** : Section 15.2 (AFL++, corpus initial et lancement), Section 15.3 (libFuzzer, option `-dict` et `-merge`), Section 15.5 (couverture et zones non couvertes), Chapitre 5 (outils de triage : `strings`, `file`)

---

## Le corpus et le dictionnaire : deux leviers de vitesse

Le fuzzer est un moteur d'exploration. Sa bitmap de couverture lui indique *où* aller, ses algorithmes de mutation lui indiquent *comment* transformer les inputs. Mais deux facteurs déterminent à quelle vitesse il progresse dans les couches profondes du code cible :

- **Le corpus** — l'ensemble des inputs à partir desquels le fuzzer génère ses mutations. Un corpus de qualité place le fuzzer à mi-chemin des chemins intéressants ; un corpus vide ou inadapté le force à tout découvrir depuis zéro, octet par octet.  
- **Le dictionnaire** — une liste de tokens (séquences d'octets) que le fuzzer peut insérer dans ses mutations. Sans dictionnaire, le fuzzer doit découvrir les magic bytes, les mots-clés et les délimiteurs par mutation aléatoire — ce qui peut prendre des heures. Avec un dictionnaire approprié, il les trouve en secondes.

En contexte de reverse engineering, la construction du corpus et du dictionnaire est un acte d'analyse en soi. C'est le moment où les connaissances accumulées pendant le triage (Chapitre 5), l'inspection des chaînes (section 5.1) et l'analyse statique dans Ghidra (Chapitre 8) se transforment en **carburant concret** pour le fuzzer.

---

## Construire un corpus initial efficace

### Sources d'inputs pour le corpus

Le corpus initial idéal contient des inputs **valides**, **variés** et **minimaux**. Valides parce qu'ils passent les premières validations du parseur et atteignent les couches profondes. Variés parce qu'ils exercent des branches différentes. Minimaux parce que les mutations sont plus efficaces sur des inputs courts — chaque octet muté a une plus grande probabilité de provoquer un changement de comportement.

En pratique, les sources d'inputs dépendent du type de binaire analysé :

**Parseur de fichiers.** Si le binaire traite un format de fichier (notre cas avec `ch15-fileformat`), les meilleurs seeds sont des fichiers réels dans ce format. Si le format est propriétaire et qu'on n'a pas de fichiers exemples, on les construit à la main à partir des informations de l'analyse statique : magic bytes, champs de version, tailles de header. Un fichier minimal qui passe les premières validations vaut mieux que cent fichiers aléatoires.

**Protocole réseau.** Si le binaire est un serveur ou un client réseau (cf. Chapitre 23), les captures Wireshark/tcpdump fournissent des trames réelles. Exporter les payloads bruts (sans les en-têtes TCP/IP) et les utiliser comme seeds. Chaque type de message du protocole devrait être représenté au moins une fois.

**Arguments en ligne de commande ou stdin.** Si le binaire lit depuis stdin ou traite des arguments textuels, les seeds sont des chaînes de texte représentatives. Les messages d'erreur trouvés avec `strings` donnent souvent des indices sur les entrées attendues (par exemple, `"Invalid command: use GET, SET, or DEL"` indique trois commandes valides).

**Aucune information disponible.** Dans le pire des cas, un corpus contenant un seul fichier d'un octet (`\x00`) suffit pour démarrer. Le fuzzer finira par découvrir le format attendu, mais la convergence sera beaucoup plus lente. Même un corpus minimal construit à la hâte est préférable.

### Construire des seeds à la main

Reprenons notre exemple de parseur avec le format `RE`. L'analyse statique a révélé :

- Magic bytes : `RE` (0x52 0x45)  
- Champ version à l'offset 2 : valeurs 1, 2 et possiblement 3  
- Taille minimale variable selon la version

On construit un seed par branche identifiée :

```bash
$ mkdir corpus_initial

# Version 1 — taille minimale 8 octets
$ printf 'RE\x01\x00\x00\x00\x00\x00' > corpus_initial/v1_minimal.bin

# Version 1 — valeur > 1000 pour atteindre le mode étendu
$ printf 'RE\x01\x00\xe9\x03\x00\x00' > corpus_initial/v1_extended.bin
# (0x03e9 = 1001 en little-endian)

# Version 2 — taille minimale 16 octets
$ printf 'RE\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_initial/v2_minimal.bin

# Version 2 — flag mode à data[4]=0x00, taille > 20
$ printf 'RE\x02\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_initial/v2_mode.bin

# Version 3 — hypothétique, pour explorer une branche potentielle
$ printf 'RE\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_initial/v3_guess.bin
```

Chaque seed est ciblé : il est conçu pour atteindre une branche spécifique identifiée dans Ghidra. Le fuzzer n'a plus qu'à muter les champs restants pour explorer les sous-branches.

> 💡 **Astuce** — La commande `printf` avec des séquences `\x` est l'outil le plus direct pour créer des seeds binaires. Pour des formats plus complexes, un script Python avec `struct.pack()` est plus lisible. Pour le format de `ch15-fileformat`, le script `scripts/keygen_template.py` illustre cette approche.

### Récupérer des inputs existants

Quand le format est connu ou semi-connu, des inputs existants sont souvent disponibles :

- **Fichiers de test du projet** — si les sources contiennent un répertoire `tests/` ou `testdata/`, les fichiers qui s'y trouvent sont des seeds idéaux.  
- **Corpus publics** — pour les formats standards, des corpus de fuzzing existent déjà. Par exemple, le projet Google OSS-Fuzz maintient des corpus pour des centaines de parseurs open source. Le dépôt `google/fuzzing` contient aussi des dictionnaires réutilisables.  
- **Fichiers générés par le programme lui-même** — si le binaire peut *écrire* dans le format qu'il lit (sérialiseur/désérialiseur), faire générer quelques fichiers et les utiliser comme seeds.

---

## Minimiser le corpus : `afl-cmin` et `afl-tmin`

Au fil du fuzzing, le corpus grandit : AFL++ y ajoute chaque input qui découvre un nouveau chemin. Après des heures de fuzzing, le corpus peut contenir des centaines d'inputs dont beaucoup sont redondants — ils couvrent les mêmes chemins que d'autres inputs déjà présents.

La minimisation du corpus réduit cet ensemble au **sous-ensemble minimal** qui préserve la même couverture totale. Moins d'inputs signifie des cycles de fuzzing plus rapides et un corpus plus facile à analyser manuellement.

### `afl-cmin` : minimiser le nombre d'inputs

`afl-cmin` identifie le plus petit sous-ensemble d'inputs qui couvre l'ensemble des edges atteints par le corpus complet :

```bash
$ afl-cmin -i out/default/queue/ -o corpus_minimized/ \
    -- ./simple_parser_afl @@
```

Résultat typique :

```
[*] Testing the target binary...
[+] OK, 247 tuples recorded.
[*] Obtaining traces for 89 input files in 'out/default/queue/'.
[*] Narrowing to 23 files with unique tuples...
[+] Narrowed down to 23 files, saved in 'corpus_minimized/'.
```

De 89 inputs, on passe à 23 — une réduction de 74% sans perte de couverture. Ce corpus minimisé est le point de départ idéal pour une nouvelle campagne de fuzzing (par exemple avec un dictionnaire enrichi) ou pour une analyse manuelle des inputs.

### `afl-tmin` : minimiser la taille de chaque input

Là où `afl-cmin` réduit le *nombre* d'inputs, `afl-tmin` réduit la *taille* de chaque input individuel en supprimant les octets non essentiels :

```bash
$ afl-tmin -i corpus_minimized/id:000003,... \
           -o corpus_minimized/id:000003_min \
           -- ./simple_parser_afl @@
```

`afl-tmin` tente itérativement de supprimer des blocs d'octets, de remplacer des séquences par des zéros, et de raccourcir l'input, en vérifiant à chaque étape que la couverture est préservée.

Pour traiter tout le corpus minimisé :

```bash
$ mkdir corpus_tmin
$ for f in corpus_minimized/*; do
    name=$(basename "$f")
    afl-tmin -i "$f" -o "corpus_tmin/${name}_tmin" \
        -- ./simple_parser_afl @@
  done
```

> ⚠️ **Attention** — `afl-tmin` est lent : il exécute le binaire des centaines de fois par input pour tester chaque suppression. Sur un corpus de 23 inputs, comptez quelques minutes. Sur un corpus de 500 inputs, lancez-le pendant la nuit. C'est pourquoi on applique d'abord `afl-cmin` (rapide, réduit le nombre) puis `afl-tmin` (lent, réduit la taille de chacun).

### Minimisation avec libFuzzer

libFuzzer intègre la minimisation via le flag `-merge` :

```bash
$ mkdir corpus_merged
$ ./fuzz_parse_input -merge=1 corpus_merged/ corpus_parse/
```

Cette commande est l'équivalent d'`afl-cmin` : elle ne garde que les inputs apportant une couverture unique. libFuzzer ne propose pas d'équivalent direct d'`afl-tmin`, mais on peut utiliser `-reduce_inputs=1` pendant le fuzzing pour que libFuzzer remplace progressivement les inputs par des versions plus courtes couvrant les mêmes edges.

---

## Les dictionnaires : accélérer la découverte de structure

### Le problème des tokens multi-octets

Le moteur de mutation d'AFL++ et de libFuzzer opère principalement au niveau de l'octet : il flip des bits, remplace des octets par des valeurs intéressantes (0, 1, 0xff, 0x7f, etc.), insère ou supprime des blocs. Cette approche fonctionne bien pour découvrir des valeurs numériques, mais elle est très lente pour découvrir des **séquences multi-octets** significatives.

Prenons un exemple concret. Si le parseur commence par vérifier un magic number de 4 octets `\x89PNG`, le fuzzer doit produire exactement ces 4 octets dans le bon ordre. Par mutation aléatoire d'un octet à la fois, la probabilité est de (1/256)⁴ = 1 chance sur 4 milliards. Même à 10 000 exécutions par seconde, il faudrait en moyenne 5 jours pour tomber dessus par hasard.

Avec un dictionnaire contenant le token `"\x89PNG"`, le fuzzer peut **insérer directement** cette séquence dans ses mutations. Il la trouve dès les premières secondes.

### Format d'un dictionnaire

Le format est identique pour AFL++ et libFuzzer — un fichier texte avec un token par ligne :

```
# Dictionnaire pour le format RE
# Chaque ligne : "nom_optionnel" = "valeur" ou juste "valeur"

# Magic bytes
magic_re="RE"

# Versions connues
version_1="\x01"  
version_2="\x02"  
version_3="\x03"  

# Valeurs intéressantes pour les champs numériques
val_0="\x00"  
val_ff="\xff"  
val_1000="\xe8\x03"  
val_1001="\xe9\x03"  

# Délimiteurs et marqueurs observés
null_word="\x00\x00\x00\x00"
```

La syntaxe :

- Les lignes commençant par `#` sont des commentaires.  
- Chaque token est une chaîne entre guillemets doubles.  
- Les séquences `\xNN` représentent des octets hexadécimaux.  
- Le préfixe `nom=` avant le token est optionnel (il sert uniquement de documentation).  
- Les tokens peuvent être de longueur quelconque, mais les tokens courts (1 à 8 octets) sont les plus efficaces.

### Construire un dictionnaire depuis l'analyse RE

Le dictionnaire est l'endroit où les connaissances du reverse engineer se transforment le plus directement en accélération du fuzzing. Chaque information récoltée pendant l'analyse statique peut se traduire en token :

**Depuis `strings` sur le binaire.** Les chaînes extraites du binaire contiennent souvent des mots-clés, des noms de commandes, des messages d'erreur qui révèlent les tokens attendus par le parseur :

```bash
$ strings simple_parser | grep -i "error\|invalid\|expected\|unknown"
Error: invalid magic  
Error: unknown version  
Expected payload length >= 16  
```

Ces messages indiquent que le parseur s'attend à un champ appelé "magic", un champ "version", et un "payload" d'au moins 16 octets. Les mots `GET`, `SET`, `DEL` trouvés dans un binaire réseau sont des commandes du protocole — à injecter directement dans le dictionnaire.

```bash
# Extraction automatique de toutes les chaînes comme tokens
$ strings -n 3 simple_parser | sort -u | \
    awk '{printf "\"%s\"\n", $0}' > dict_from_strings.txt
```

> ⚠️ **Attention** — Un dictionnaire trop grand (des centaines de tokens) peut ralentir le fuzzer : à chaque mutation, il peut choisir parmi tous les tokens, diluant la probabilité de choisir les bons. Filtrez les chaînes extraites pour ne garder que celles qui semblent être des mots-clés du format (pas les messages d'erreur complets, juste les tokens structurels).

**Depuis les constantes dans Ghidra.** Les valeurs comparées dans les conditions de branchement du parseur sont des candidats directs :

```
; Ghidra decompile, fonction parse_header:
if (*(int *)data == 0x45520001) {    // "RE" + version 1 en little-endian
    ...
}
if (data[8] == 0x7f) {               // Marqueur de section
    ...
}
```

Le dictionnaire correspondant :

```
magic_v1="\x52\x45\x01\x00"  
magic_v2="\x52\x45\x02\x00"  
section_marker="\x7f"  
```

**Depuis les crashs analysés (section 15.4).** Chaque crash minimisé contient des octets significatifs. Les positions critiques identifiées pendant l'analyse des crashs deviennent des tokens :

```
# Séquence qui a déclenché le mode étendu v2
mode_trigger="\x00\xff"
```

**Depuis des spécifications connues.** Si le format est partiellement documenté ou si on a identifié une bibliothèque embarquée (cf. Chapitre 20, FLIRT/signatures), les constantes de cette bibliothèque enrichissent le dictionnaire. Par exemple, pour un binaire utilisant JSON :

```
brace_open="{"  
brace_close="}"  
bracket_open="["  
bracket_close="]"  
colon=":"  
comma=","  
quote="\""  
kw_null="null"  
kw_true="true"  
kw_false="false"  
```

**Dictionnaires communautaires.** Le dépôt AFL++ et le dépôt `google/fuzzing` contiennent des dictionnaires pré-construits pour des dizaines de formats : PNG, JPEG, PDF, XML, HTML, ELF, JSON, SQL, HTTP, TLS, et bien d'autres. Si le binaire analysé traite un de ces formats, utiliser le dictionnaire existant est un gain de temps considérable :

```bash
$ ls AFLplusplus/dictionaries/
elf.dict  gif.dict  html.dict  jpeg.dict  json.dict  pdf.dict  png.dict  ...
```

### Utiliser un dictionnaire

Avec AFL++ :

```bash
$ afl-fuzz -i corpus_minimized -o out -x my_dict.txt -- ./simple_parser_afl @@
```

Avec libFuzzer :

```bash
$ ./fuzz_parse_input -dict=my_dict.txt corpus_parse/
```

Le fuzzer intègre les tokens du dictionnaire dans ses stratégies de mutation : il les insère à des positions aléatoires, remplace des séquences existantes par des tokens, et combine des tokens entre eux. Le dictionnaire ne remplace pas les mutations classiques — il les complète.

---

## Stratégies avancées de gestion de corpus

### Corpus rotatif entre AFL++ et libFuzzer

Les deux fuzzers utilisent des stratégies de mutation différentes et découvrent des chemins complémentaires. Une technique efficace consiste à faire circuler le corpus entre les deux :

```bash
# Phase 1 : fuzzing AFL++ (exploration large)
$ afl-fuzz -i corpus_initial -o out_afl -x dict.txt -- ./simple_parser_afl @@
# (laisser tourner quelques heures, puis Ctrl+C)

# Phase 2 : minimiser le corpus AFL++
$ afl-cmin -i out_afl/default/queue/ -o corpus_after_afl -- ./simple_parser_afl @@

# Phase 3 : fuzzing libFuzzer (ciblage profond)
$ ./fuzz_parse_input -dict=dict.txt -max_total_time=3600 corpus_after_afl/

# Phase 4 : fusionner les corpus
$ mkdir corpus_combined
$ ./fuzz_parse_input -merge=1 corpus_combined/ corpus_after_afl/ out_afl/default/crashes/

# Phase 5 : relancer AFL++ avec le corpus enrichi
$ afl-fuzz -i corpus_combined -o out_afl_v2 -x dict.txt -- ./simple_parser_afl @@
```

À chaque rotation, le corpus s'enrichit des découvertes des deux fuzzers. Les chemins trouvés par AFL++ grâce à ses mutations déterministes alimentent libFuzzer, et les explorations profondes de libFuzzer alimentent AFL++ au cycle suivant.

### Enrichir le corpus à la main après analyse

L'analyse des crashs (section 15.4) et de la couverture (section 15.5) révèle des zones non couvertes gardées par des conditions précises. Quand on comprend la condition bloquante, on peut construire un seed qui la satisfait et l'injecter dans le corpus :

```bash
# La couverture montre que la branche "version == 3, subtype == 0x42" n'est pas atteinte
# Construire un seed qui satisfait ces conditions
$ printf 'RE\x03\x00\x42\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > corpus_minimized/v3_sub42.bin

# Relancer le fuzzing avec ce seed ajouté
$ afl-fuzz -i corpus_minimized -o out_v2 -x dict.txt -- ./simple_parser_afl @@
```

Ce seed débloque l'accès à une branche entière que le fuzzer n'atteignait pas. À partir de là, ses mutations peuvent explorer les sous-branches de cette zone nouvelle.

### Corpus de régression

Une fois la campagne de fuzzing terminée et les résultats analysés, le corpus minimisé constitue un **jeu de tests de régression** pour le parseur. Si le binaire évolue (nouvelle version, patch de sécurité), rejouer ce corpus permet de vérifier que les chemins précédemment atteints sont toujours présents et que les crashs corrigés ne réapparaissent pas :

```bash
# Vérifier qu'aucun input du corpus ne crashe sur le binaire patché
$ for f in corpus_minimized/*; do
    ./parser_v2_asan "$f" 2>&1 | grep -q "ERROR:" && echo "REGRESSION: $f"
  done
```

Ce workflow est particulièrement pertinent dans le contexte du diffing de binaires (Chapitre 10) : quand on compare deux versions d'un binaire, le corpus du fuzzer sert de base de test pour identifier les changements de comportement.

---

## Dictionnaires et corpus : impact mesurable

Pour illustrer l'impact concret de ces techniques, voici des ordres de grandeur typiques observés sur un parseur de format binaire de complexité moyenne :

| Configuration | Temps pour atteindre 50% de couverture | Crashs trouvés en 1h |  
|---|---|---|  
| Corpus vide (un seul `\x00`), sans dictionnaire | > 8 heures | 0–1 |  
| Corpus minimal (3 seeds valides), sans dictionnaire | ~45 minutes | 2–4 |  
| Corpus minimal, avec dictionnaire de base (10 tokens) | ~10 minutes | 4–8 |  
| Corpus ciblé (un seed par branche), avec dictionnaire enrichi | ~2 minutes | 8–15 |

Ces chiffres sont indicatifs et varient selon la complexité du parseur, la profondeur des branches, et la vitesse d'exécution du binaire. Mais le rapport entre les configurations est typiquement de cet ordre : **le passage de « corpus vide » à « corpus ciblé + dictionnaire » accélère la découverte d'un facteur 50 à 200**.

Cet investissement de 15 à 30 minutes de préparation (construire les seeds, extraire les tokens) est le meilleur rapport temps/résultat de toute la chaîne de fuzzing.

---

## En résumé

Le corpus et le dictionnaire sont les deux canaux par lesquels le reverse engineer injecte ses connaissances dans le fuzzer :

- **Le corpus initial** se construit à partir des magic bytes, des valeurs de champs et des contraintes de taille identifiées pendant le triage et l'analyse statique. Un seed par branche identifiée est la règle de base.  
- **`afl-cmin`** réduit le corpus au sous-ensemble minimal préservant la couverture (rapide, à lancer en premier). **`afl-tmin`** réduit ensuite la taille de chaque input individuel (lent, à lancer en second). Pour libFuzzer, `-merge=1` remplit le même rôle.  
- **Le dictionnaire** contient les tokens structurels du format cible : magic bytes, mots-clés, délimiteurs, constantes de Ghidra, valeurs critiques des crashs. Des dictionnaires communautaires existent pour les formats standards.  
- **La rotation corpus** entre AFL++ et libFuzzer, complétée par l'injection manuelle de seeds basés sur la couverture, est la stratégie la plus efficace pour maximiser l'exploration.

Avec un corpus ciblé et un dictionnaire adapté, le fuzzer ne part plus de zéro — il part de là où l'analyse statique s'est arrêtée. La section suivante met toute cette méthodologie en pratique sur un cas concret : le parseur de format custom de `ch15-fileformat`.

---


⏭️ [Cas pratique : découvrir des chemins cachés dans un parseur binaire](/15-fuzzing/07-cas-pratique-parseur.md)
