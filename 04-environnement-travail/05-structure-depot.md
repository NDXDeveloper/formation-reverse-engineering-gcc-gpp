🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 4.5 — Structure du dépôt : organisation de `binaries/` et des `Makefile` par chapitre

> 🎯 **Objectif de cette section** : comprendre l'organisation du dépôt de la formation — où se trouvent les sources, les binaires, les scripts, les patterns et les corrigés — afin de naviguer efficacement dans le matériel pédagogique tout au long des 36 chapitres.

---

## Vue d'ensemble du dépôt

Le dépôt `formation-reverse-engineering-gcc-gpp/` est organisé selon un principe simple : **chaque type de contenu a sa place dédiée**, et chaque chapitre est autonome dans son dossier.

```
formation-reverse-engineering-gcc-gpp/
│
├── README.md                    ← Présentation générale + liens vers les parties
├── LICENSE                      ← MIT + disclaimer éthique (FR/EN)
├── check_env.sh                 ← Script de vérification de l'environnement
├── preface.md                   ← Préface du tutoriel
│
├── partie-1-fondamentaux.md     ← Introduction Partie I
├── partie-2-analyse-statique.md ← Introduction Partie II
├── ...                          ← Une introduction par partie (9 fichiers)
│
├── 01-introduction-re/          ┐
├── 02-chaine-compilation-gnu/   │
├── ...                          ├─ Un dossier par chapitre (36 dossiers)
├── 35-automatisation-scripting/ │
├── 36-ressources-progresser/    ┘
│
├── binaries/                    ← Sources C/C++/Rust/Go + Makefile → binaires d'entraînement
├── scripts/                     ← Scripts Python utilitaires
├── hexpat/                      ← Patterns ImHex (.hexpat)
├── yara-rules/                  ← Règles YARA
├── annexes/                     ← Annexes A–K (références, cheat sheets, glossaire)
│
└── solutions/                   ← Corrigés des checkpoints (⚠️ spoilers)
```

Quatre grandes catégories de contenu coexistent :

1. **Le cours** — les 36 dossiers de chapitres, chacun contenant des fichiers Markdown (`.md`) et un `checkpoint.md`.  
2. **Les binaires d'entraînement** — le répertoire `binaries/`, cœur pratique de la formation.  
3. **Les ressources transverses** — scripts Python, patterns ImHex, règles YARA, annexes.  
4. **Les corrigés** — le répertoire `solutions/`, à consulter uniquement après avoir tenté les checkpoints.

---

## Les dossiers de chapitres

Chaque chapitre suit la même structure interne :

```
XX-nom-du-chapitre/
├── README.md               ← Introduction et plan du chapitre
├── 01-premiere-section.md   ← Sections numérotées
├── 02-deuxieme-section.md
├── ...
└── checkpoint.md            ← Mini-exercice de validation des acquis
```

Le `README.md` de chaque chapitre joue le rôle de page d'accueil : il introduit le sujet, liste les prérequis, résume le plan et renvoie vers les sections. C'est le point d'entrée si vous accédez à un chapitre directement, sans parcourir les précédents.

Les sections sont numérotées dans l'ordre de lecture recommandé. Chaque fichier est autonome — il contient le texte, les extraits de code, les commandes et les références vers les binaires concernés.

Le `checkpoint.md` clôture le chapitre avec un exercice pratique qui mobilise les compétences couvertes. Le corrigé correspondant se trouve dans `solutions/`.

> 💡 Les chapitres ne contiennent pas de binaires compilés. Les sources se trouvent dans `binaries/`, et c'est à vous de les compiler (section 4.6). Cette séparation est intentionnelle : elle garantit que les binaires sont compilés pour *votre* système, avec *votre* version de GCC, exactement comme dans un contexte de RE réel.

---

## Le répertoire `binaries/` — cœur de la pratique

C'est le répertoire le plus important du dépôt. Il contient les **sources** des applications d'entraînement et les **Makefile** pour les compiler en plusieurs variantes.

### Arborescence

```
binaries/
├── Makefile                   ← Makefile racine : `make all` compile tout
│
├── ch21-keygenme/
│   ├── keygenme.c             ← Source C du crackme
│   └── Makefile               ← Produit les variantes du binaire
│
├── ch22-oop/
│   ├── oop.cpp                ← Source C++ orienté objet
│   └── Makefile
│
├── ch23-network/
│   ├── client.c               ← Client réseau
│   ├── server.c               ← Serveur réseau
│   └── Makefile
│
├── ch24-crypto/
│   ├── crypto.c               ← Application avec chiffrement
│   └── Makefile
│
├── ch25-fileformat/
│   ├── fileformat.c           ← Parseur de format custom
│   └── Makefile
│
├── ch27-ransomware/           ← ⚠️ Sandbox uniquement
│   ├── ransomware_sample.c
│   └── Makefile
│
├── ch28-dropper/              ← ⚠️ Sandbox uniquement
│   ├── dropper_sample.c
│   └── Makefile
│
├── ch29-packed/
│   ├── packed_sample.c
│   └── Makefile
│
├── ch33-rust/
│   ├── crackme_rust/
│   │   ├── src/
│   │   │   └── main.rs
│   │   └── Cargo.toml
│   └── Makefile
│
└── ch34-go/
    ├── crackme_go/
    │   └── main.go
    └── Makefile
```

### Convention de nommage des sous-dossiers

Chaque sous-dossier est préfixé par le **numéro du chapitre** qui l'utilise principalement : `ch21-keygenme`, `ch23-network`, etc. Ce préfixe permet de faire immédiatement le lien entre un binaire et le chapitre qui le documente.

Certains binaires sont réutilisés dans plusieurs chapitres. Par exemple, `ch21-keygenme` apparaît dans le chapitre 21 (cas pratique principal), mais aussi dans les chapitres 7 (comparaison d'optimisations avec `objdump`), 12 (traçage avec GEF), 18 (résolution avec angr) et d'autres. Le nom du dossier indique le chapitre d'*introduction* du binaire, pas le seul chapitre qui l'utilise.

### Ce que contient chaque sous-dossier

Un sous-dossier typique contient :

- **Un ou plusieurs fichiers sources** (`.c`, `.cpp`, `.rs`, `.go`) — le code que vous compilez.  
- **Un `Makefile` dédié** — qui sait produire toutes les variantes nécessaires du binaire.

Les **binaires compilés** ne sont pas versionnés dans le dépôt. Ils sont générés localement par `make` et apparaissent dans le même répertoire que les sources après compilation. C'est un choix délibéré : les binaires dépendent du compilateur, de sa version et du système, et ne doivent pas être partagés comme des fichiers statiques.

---

## Anatomie d'un Makefile de chapitre

Chaque Makefile de sous-dossier suit le même modèle. Prenons l'exemple de `ch21-keygenme/Makefile` :

```makefile
CC      = gcc  
CFLAGS  = -Wall -Wextra  
SRC     = keygenme.c  
NAME    = keygenme  

all: $(NAME)_O0 $(NAME)_O2 $(NAME)_O3 $(NAME)_O0_strip $(NAME)_O2_strip

# --- Variantes par niveau d'optimisation ---

$(NAME)_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

$(NAME)_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -o $@ $<

$(NAME)_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -g -o $@ $<

# --- Variantes strippées (sans symboles) ---

$(NAME)_O0_strip: $(NAME)_O0
	cp $< $@
	strip $@

$(NAME)_O2_strip: $(NAME)_O2
	cp $< $@
	strip $@

clean:
	rm -f $(NAME)_O0 $(NAME)_O2 $(NAME)_O3 $(NAME)_O0_strip $(NAME)_O2_strip

.PHONY: all clean
```

Ce Makefile produit **cinq binaires** à partir d'un seul fichier source :

| Binaire | Optimisation | Symboles de débogage | Strippé |  
|---|---|---|---|  
| `keygenme_O0` | `-O0` (aucune) | Oui (`-g`) | Non |  
| `keygenme_O2` | `-O2` (standard) | Oui (`-g`) | Non |  
| `keygenme_O3` | `-O3` (agressive) | Oui (`-g`) | Non |  
| `keygenme_O0_strip` | `-O0` | Non | Oui |  
| `keygenme_O2_strip` | `-O2` | Non | Oui |

### Pourquoi ces variantes ?

Cette approche multi-variantes est au cœur de la pédagogie de la formation :

**Les niveaux d'optimisation** (`-O0` à `-O3`) permettent de voir comment le même code source se transforme radicalement à l'assembleur selon les options du compilateur. Le chapitre 7 compare les listings `objdump` de `keygenme_O0` et `keygenme_O2`. Le chapitre 16 analyse en détail les optimisations appliquées par GCC. Disposer des variantes côte à côte rend ces différences tangibles.

**Les variantes avec symboles** (`-g`) facilitent l'apprentissage : les noms de fonctions, les numéros de ligne et les noms de variables sont présents dans les informations DWARF. C'est un filet de sécurité quand vous débutez — vous pouvez vérifier vos déductions contre les symboles.

**Les variantes strippées** simulent les conditions réelles du RE. Un binaire commercial, un malware, un firmware — aucun ne vous fournira de symboles de débogage. Travailler sur les variantes strippées est plus difficile, mais c'est la compétence que vous construisez.

> 💡 **Progression recommandée** : pour chaque cas pratique, commencez par la variante `_O0` (la plus lisible à l'assembleur), puis passez à `_O2` (plus réaliste), et enfin tentez la variante `_O2_strip` (conditions réelles). C'est une montée en difficulté naturelle qui consolide les acquis.

---

## Le Makefile racine

Le fichier `binaries/Makefile` (à la racine du répertoire `binaries/`) orchestre la compilation de l'ensemble des sous-dossiers :

```makefile
SUBDIRS = ch21-keygenme ch22-oop ch23-network ch24-crypto ch25-fileformat \
          ch27-ransomware ch28-dropper ch29-packed ch33-rust ch34-go

all:
	@for dir in $(SUBDIRS); do \
		echo "=== Compilation de $$dir ==="; \
		$(MAKE) -C $$dir all; \
	done

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

.PHONY: all clean
```

Un simple `make all` depuis `binaries/` compile toutes les variantes de tous les chapitres. Un `make clean` supprime tous les binaires générés. La section 4.6 détaille cette opération.

> ⚠️ **Prérequis** : la compilation des sous-dossiers `ch33-rust` et `ch34-go` nécessite respectivement les toolchains Rust (`rustc`/`cargo`) et Go (`go`). Si elles ne sont pas installées, ces deux cibles échoueront sans affecter les autres. Les toolchains sont marquées comme optionnelles en section 4.2.

---

## Le répertoire `scripts/`

```
scripts/
├── triage.py             ← Triage automatique d'un binaire
├── keygen_template.py    ← Template de keygen avec pwntools
└── batch_analyze.py      ← Script d'analyse batch pour Ghidra headless
```

Ces scripts Python sont des **outils utilitaires** utilisés et détaillés dans les chapitres suivants :

**`triage.py`** (chapitre 5) — automatise le workflow de « triage rapide » vu en section 5.7. Il exécute `file`, `strings`, `readelf`, `checksec` et `ldd` sur un binaire donné et produit un rapport structuré. C'est un point de départ que vous enrichirez au fil de la formation.

**`keygen_template.py`** (chapitre 21) — squelette de keygen basé sur pwntools. Le chapitre 21 vous guide pour le compléter afin de générer des clés valides pour le crackme `keygenme`.

**`batch_analyze.py`** (chapitre 35) — script d'analyse batch utilisant Ghidra en mode headless. Il importe un répertoire de binaires, lance l'analyse automatique sur chacun et produit un rapport JSON. C'est le checkpoint du chapitre 35.

> 💡 Ces scripts sont des points de départ, pas des solutions clé-en-main. Ils contiennent des `TODO` et des fonctions incomplètes que vous remplirez lors des exercices.

---

## Le répertoire `hexpat/`

```
hexpat/
├── elf_header.hexpat          ← Pattern pour visualiser un header ELF
├── ch25_fileformat.hexpat     ← Pattern pour le format custom du chapitre 25
└── ch23_protocol.hexpat       ← Pattern pour le protocole réseau du chapitre 23
```

Les fichiers `.hexpat` sont des **patterns ImHex** écrits dans le langage de description de structures d'ImHex. Ils permettent de projeter une grammaire structurée sur un fichier binaire brut, transformant une suite d'octets en champs nommés, typés et colorisés.

**`elf_header.hexpat`** (chapitre 6) — pattern de référence qui parse et colore les headers d'un fichier ELF (magic bytes, type, architecture, entry point, programme headers, section headers). C'est un outil pédagogique pour comprendre la structure ELF visuellement, et un exemple complet de la syntaxe `.hexpat`.

**`ch25_fileformat.hexpat`** (chapitre 25) — pattern pour le format de fichier custom créé pour le cas pratique du chapitre 25. Le checkpoint du chapitre 6 vous demande de l'écrire vous-même avant de consulter cette version.

**`ch23_protocol.hexpat`** (chapitre 23) — pattern pour décoder les trames du protocole réseau custom utilisé par le client/serveur du chapitre 23. Permet de charger un fichier `.pcap` (ou un dump brut) dans ImHex et de visualiser les champs de chaque paquet.

---

## Le répertoire `yara-rules/`

```
yara-rules/
├── crypto_constants.yar      ← Détection de constantes cryptographiques
└── packer_signatures.yar     ← Détection de packers courants (UPX, etc.)
```

Les fichiers `.yar` contiennent des **règles YARA** — des signatures de pattern matching appliquées sur des fichiers binaires.

**`crypto_constants.yar`** (chapitres 24, 27) — détecte la présence de constantes cryptographiques connues : S-box AES, vecteurs d'initialisation SHA-256, constantes MD5, tables RC4. Quand cette règle matche sur un binaire, c'est un indice fort que le binaire utilise de la cryptographie — et un point de départ pour identifier les routines correspondantes dans le désassembleur.

**`packer_signatures.yar`** (chapitre 29) — détecte les signatures de packers courants : header UPX, sections aux noms caractéristiques, patterns d'entropie. Utile dans le workflow de triage pour savoir si un binaire est compressé ou obfusqué avant de tenter de le désassembler.

> 📌 Le chapitre 35 vous guide pour écrire vos propres règles YARA et les intégrer dans un pipeline d'analyse automatisé.

---

## Le répertoire `annexes/`

```
annexes/
├── README.md
├── annexe-a-opcodes-x86-64.md
├── annexe-b-system-v-abi.md
├── annexe-c-cheatsheet-gdb.md
├── annexe-d-cheatsheet-radare2.md
├── annexe-e-cheatsheet-imhex.md
├── annexe-f-sections-elf.md
├── annexe-g-comparatif-outils-natifs.md
├── annexe-h-comparatif-outils-dotnet.md
├── annexe-i-patterns-gcc.md
├── annexe-j-constantes-crypto.md
└── annexe-k-glossaire.md
```

Les annexes sont des **documents de référence** que vous consulterez régulièrement pendant la formation et au-delà. Ce ne sont pas des chapitres à lire de bout en bout, mais des aide-mémoire à garder sous la main :

- **Annexe A** — Les opcodes x86-64 les plus fréquents en RE, avec leur effet sur les registres et les flags.  
- **Annexes C, D, E** — Cheat sheets pour GDB/GEF/pwndbg, Radare2/Cutter et ImHex. Imprimez-les ou gardez-les dans un onglet.  
- **Annexe F** — Tableau des sections ELF et leurs rôles — complément du chapitre 2.  
- **Annexe I** — Patterns d'assembleur caractéristiques de GCC — complément du chapitre 16.  
- **Annexe J** — Constantes magiques cryptographiques — complément du chapitre 24.  
- **Annexe K** — Glossaire complet du reverse engineering, de « ABI » à « zero-day ».

---

## Le répertoire `solutions/`

```
solutions/
├── ch01-checkpoint-solution.md
├── ch02-checkpoint-solution.md
├── ...
├── ch21-checkpoint-keygen.py
├── ch22-checkpoint-plugin.cpp
├── ch23-checkpoint-client.py
├── ...
└── ch35-checkpoint-batch.py
```

Chaque fichier correspond au corrigé du checkpoint d'un chapitre. Les formats varient selon la nature de l'exercice : Markdown pour les analyses écrites, Python pour les scripts, C++ pour le plugin du chapitre 22, `.hexpat` pour le checkpoint ImHex du chapitre 6.

> ⚠️ **Spoilers.** Consultez les solutions uniquement après avoir tenté le checkpoint par vous-même. Le RE est une discipline qui s'acquiert par la pratique — lire un corrigé sans avoir cherché ne développe pas les réflexes nécessaires.

---

## Fichiers à la racine

Trois fichiers à la racine du dépôt méritent d'être mentionnés :

**`README.md`** — le sommaire général de la formation. C'est le point d'entrée principal, avec les liens vers toutes les parties, tous les chapitres et toutes les sections. Si vous êtes perdu, revenez ici.

**`LICENSE`** — licence MIT avec un disclaimer éthique bilingue (français/anglais). Elle rappelle que le contenu est strictement éducatif et que l'utilisation des techniques enseignées sur des logiciels sans autorisation est illégale.

**`check_env.sh`** — le script de vérification de l'environnement détaillé en section 4.7. Il parcourt tous les outils attendus, vérifie leurs versions et contrôle que les binaires d'entraînement sont compilés.

---

## Cloner le dépôt

Si ce n'est pas déjà fait, clonez le dépôt dans votre VM :

```bash
[vm] git clone https://github.com/NDXDeveloper/formation-reverse-engineering-gcc-gpp.git ~/formation-re
[vm] cd ~/formation-re
[vm] ls
```

Vous devriez voir la structure décrite ci-dessus. Le répertoire `binaries/` ne contient que les sources et les Makefile — pas encore de binaires compilés. C'est l'objet de la section suivante.

---

## Résumé

- Le dépôt est organisé par **type de contenu** : chapitres (cours), `binaries/` (pratique), `scripts/`/`hexpat/`/`yara-rules/` (ressources), `solutions/` (corrigés), `annexes/` (références).  
- Chaque chapitre est un dossier autonome contenant ses sections Markdown et un checkpoint.  
- Le répertoire `binaries/` contient les **sources** et les **Makefile**, pas les binaires compilés. Chaque Makefile produit plusieurs variantes (niveaux d'optimisation, avec/sans symboles) pour faciliter l'apprentissage progressif.  
- Le Makefile racine de `binaries/` compile tout en une commande (`make all`).  
- Les ressources transverses (patterns `.hexpat`, règles YARA, scripts Python) sont centralisées dans des répertoires dédiés et référencées depuis les chapitres concernés.

---


⏭️ [Compiler tous les binaires d'entraînement en une commande (`make all`)](/04-environnement-travail/06-compiler-binaires.md)
