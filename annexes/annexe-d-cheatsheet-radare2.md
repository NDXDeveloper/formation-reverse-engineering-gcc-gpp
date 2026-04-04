🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexe D — Cheat sheet Radare2 / Cutter

> 📎 **Fiche de référence** — Cette annexe regroupe les commandes Radare2 (`r2`) les plus utiles pour le reverse engineering de binaires ELF x86-64 compilés avec GCC. Elle couvre le mode console natif ainsi que l'interface graphique Cutter. Les commandes sont organisées par tâche pour un accès rapide pendant une session d'analyse.

---

## Comprendre la philosophie de Radare2

Radare2 est un framework de RE en ligne de commande dont l'interface repose sur des **commandes courtes et composables**. La courbe d'apprentissage est raide, mais la logique sous-jacente est cohérente : chaque commande est une lettre ou un groupe de lettres, et les variantes s'obtiennent par suffixe. Par exemple, `p` = *print*, `pd` = *print disassembly*, `pdf` = *print disassembly of function*, `pdfj` = idem en JSON.

Les suffixes récurrents sont :

| Suffixe | Signification | Exemple |  
|---------|---------------|---------|  
| `j` | Sortie JSON | `ij` = `i` en JSON |  
| `q` | Mode *quiet* (sortie minimale) | `aflq` = liste de fonctions compacte |  
| `*` | Sortie sous forme de commandes `r2` rejouables | `afl*` = liste de fonctions en format script |  
| `~` | Grep interne (filtre la sortie) | `afl~main` = filtre les fonctions contenant « main » |  
| `~:N` | Grep + sélection de la Nᵉ colonne | `afl~:0` = première colonne uniquement (adresses) |  
| `?` | Aide de la commande | `pd?` = aide sur `pd` |

> 💡 **Règle d'or** : quand vous ne savez pas comment utiliser une commande, ajoutez `?` à la fin. `a?`, `p?`, `s?`, `w?` — chaque famille de commandes a sa page d'aide intégrée.

---

## 1 — Lancement et modes d'ouverture

### 1.1 — Ouvrir un binaire

| Commande shell | Description |  
|----------------|-------------|  
| `r2 ./binary` | Ouvre un binaire en mode lecture seule (analyse non automatique) |  
| `r2 -A ./binary` | Ouvre et lance l'analyse automatique (`aaa`) |  
| `r2 -AA ./binary` | Ouvre avec analyse approfondie (`aaaa`) |  
| `r2 -d ./binary` | Ouvre en mode débogage (lance le programme) |  
| `r2 -d ./binary arg1 arg2` | Mode débogage avec arguments |  
| `r2 -d -A ./binary` | Mode débogage avec analyse automatique |  
| `r2 -w ./binary` | Ouvre en mode écriture (pour patcher le binaire) |  
| `r2 -n ./binary` | Ouvre sans aucune analyse ni chargement des métadonnées |  
| `r2 -B 0x400000 ./binary` | Ouvre avec une adresse de base spécifiée (binaire rebasé) |  
| `r2 -e bin.cache=true ./binary` | Active le cache binaire (accélère l'analyse de gros fichiers) |  
| `r2 -i script.r2 ./binary` | Exécute un script de commandes au lancement |  
| `r2 malloc://512` | Ouvre un bloc mémoire de 512 octets (sandbox d'écriture) |

### 1.2 — Commandes de session

| Commande r2 | Description |  
|-------------|-------------|  
| `o ./other_binary` | Ouvre un autre fichier dans la même session |  
| `o` | Liste les fichiers ouverts |  
| `oo+` | Réouvre le fichier courant en mode écriture |  
| `oo` | Réouvre le fichier courant (recharge) |  
| `q` | Quitte r2 |  
| `q!` | Quitte sans confirmation |  
| `?` | Aide générale |  
| `?v $$ ` | Affiche l'adresse courante en hexadécimal |  
| `?v $$ - sym.main` | Calcule une différence d'adresses |  
| `? 0xff + 1` | Calculatrice intégrée |

---

## 2 — Analyse

L'analyse est l'étape qui permet à r2 de reconnaître les fonctions, les blocs de base, les cross-références et les structures du binaire. Sans analyse, r2 ne montre que des octets bruts.

### 2.1 — Commandes d'analyse

| Commande | Description |  
|----------|-------------|  
| `aa` | Analyse basique (fonctions identifiées par symboles et entrées) |  
| `aaa` | Analyse approfondie (heuristiques de détection de fonctions, xrefs) |  
| `aaaa` | Analyse expérimentale (encore plus d'heuristiques, plus lent) |  
| `aab` | Analyse des blocs de base |  
| `aac` | Analyse des appels (`call` → détection de fonctions) |  
| `aap` | Détection de fonctions par prélude (recherche des prologues `push rbp; mov rbp, rsp`) |  
| `aar` | Analyse des références (data references) |  
| `aav` | Analyse des valeurs et constantes (cherche les pointeurs dans les données) |  
| `aan` | Auto-nommage des fonctions (heuristiques sur les chaînes et appels) |  
| `afr` | Réanalyse la fonction courante |  
| `af` | Analyse la fonction à l'adresse courante (si non détectée automatiquement) |  
| `af+ <addr> <size> <name>` | Crée manuellement une fonction |  
| `af- <addr>` | Supprime la fonction à l'adresse spécifiée |

La commande `aaa` est le bon compromis entre rapidité et exhaustivité pour la majorité des binaires. Sur les très gros binaires (plusieurs Mo), `aa` peut suffire pour un premier triage, suivi d'une analyse ciblée avec `af` sur les fonctions d'intérêt.

### 2.2 — Informations sur les fonctions

| Commande | Description |  
|----------|-------------|  
| `afl` | Liste toutes les fonctions détectées (adresse, taille, nom) |  
| `aflq` | Liste compacte (adresses uniquement) |  
| `afl~main` | Filtre les fonctions contenant « main » dans leur nom |  
| `afl~:1` | Affiche uniquement la colonne des tailles |  
| `afll` | Liste détaillée (nombre de blocs, appels, xrefs, complexité cyclomatique) |  
| `aflt` | Liste avec le nombre d'instructions par fonction |  
| `afi` | Informations sur la fonction courante (adresse, taille, variables, arguments) |  
| `afi.` | Informations sur la fonction contenant l'adresse courante |  
| `afn <name>` | Renomme la fonction courante |  
| `afn <name> <addr>` | Renomme la fonction à l'adresse spécifiée |  
| `afvn <old> <new>` | Renomme une variable locale |  
| `afvt <name> <type>` | Change le type d'une variable locale |  
| `afv` | Liste les variables locales et arguments de la fonction courante |

---

## 3 — Navigation et déplacement

### 3.1 — La commande `s` (seek)

Dans r2, la position courante est un **curseur** (*seek*) que vous déplacez dans le fichier. Toutes les commandes qui affichent du contenu (désassemblage, hexdump, etc.) opèrent à partir de cette position.

| Commande | Description |  
|----------|-------------|  
| `s main` | Se déplace au début de la fonction `main` |  
| `s sym.main` | Identique (préfixe `sym.` explicite) |  
| `s 0x401234` | Se déplace à une adresse absolue |  
| `s entry0` | Se déplace au point d'entrée du binaire |  
| `s $s` | Se déplace au début de la section courante |  
| `s+10` | Avance de 10 octets |  
| `s-10` | Recule de 10 octets |  
| `s++` | Avance à la prochaine fonction |  
| `s--` | Recule à la fonction précédente |  
| `s-` | Retourne à la position précédente (historique, comme le bouton « Back ») |  
| `s*` | Affiche l'historique des positions |  
| `sf` | Se déplace au début de la fonction courante |  
| `sf.` | Se déplace au début de la fonction contenant la position courante |

### 3.2 — Variables et adresses spéciales

| Variable | Signification |  
|----------|---------------|  
| `$$` | Adresse courante (seek position) |  
| `$s` | Début de la section courante |  
| `$S` | Taille de la section courante |  
| `$b` | Taille du bloc courant |  
| `$l` | Taille de l'opcode à la position courante |  
| `$e` | Début de la pile (si en mode debug) |  
| `$f` | Début de la fonction courante |  
| `$j` | Destination du saut à la position courante |

---

## 4 — Informations sur le binaire

### 4.1 — Métadonnées et headers

| Commande | Description |  
|----------|-------------|  
| `i` | Résumé du binaire (format, architecture, endianness, taille) |  
| `ia` | Architecture et bits |  
| `ie` | Point d'entrée (entrypoints) |  
| `iI` | Informations détaillées du binaire (type, OS, machine, classe) |  
| `ih` | Headers du binaire |  
| `iH` | Headers détaillés (ELF program headers et section headers) |  
| `il` | Bibliothèques liées dynamiquement (équivalent de `ldd`) |  
| `ii` | Table des imports (fonctions importées depuis les bibliothèques) |  
| `iE` | Table des exports (fonctions exportées) |  
| `iS` | Sections du binaire (nom, taille, permissions, adresse) |  
| `iSS` | Segments du binaire (program headers ELF) |  
| `iS~.text` | Filtre sur la section `.text` |  
| `is` | Table des symboles |  
| `is~FUNC` | Filtre les symboles de type fonction |

### 4.2 — Chaînes de caractères

| Commande | Description |  
|----------|-------------|  
| `iz` | Chaînes trouvées dans les sections de données (`.rodata`, `.data`) |  
| `izz` | Chaînes trouvées dans tout le binaire (y compris les sections non standard) |  
| `iz~password` | Filtre les chaînes contenant « password » |  
| `izj` | Chaînes en format JSON (utile pour le scripting) |  
| `izzq` | Chaînes compactes (adresse + contenu uniquement) |

### 4.3 — Cross-références (XREF)

| Commande | Description |  
|----------|-------------|  
| `axt <addr>` | Xrefs **vers** l'adresse (qui appelle ou référence cette adresse) |  
| `axt @ sym.strcmp` | Qui appelle `strcmp` ? |  
| `axt @ str.password` | Qui utilise la chaîne « password » ? |  
| `axf <addr>` | Xrefs **depuis** l'adresse (qu'est-ce que cette adresse appelle ou référence) |  
| `axf @ sym.main` | Quelles fonctions sont appelées par `main` ? |  
| `ax` | Liste toutes les cross-références |  
| `axtj <addr>` | Xrefs vers l'adresse en JSON |

Les cross-références sont l'un des outils les plus puissants du RE. La commande `axt` en particulier est indispensable pour tracer l'utilisation d'une chaîne, d'une fonction ou d'une constante à travers tout le binaire. Le workflow typique est : trouver une chaîne intéressante avec `iz`, puis `axt @ str.xxx` pour trouver le code qui l'utilise.

### 4.4 — Protections et sécurité

| Commande | Description |  
|----------|-------------|  
| `ik~canary` | Vérifie la présence du stack canary |  
| `ik~nx` | Vérifie NX (non-executable stack) |  
| `ik~pic` | Vérifie PIC/PIE |  
| `ik~relro` | Vérifie RELRO |  
| `rabin2 -I ./binary` | Résumé des protections depuis le shell (outil compagnon) |

> 💡 Pour un audit de protections complet, l'outil `checksec` (voir chapitre 5.6) ou la commande `checksec` de GEF/pwndbg reste plus lisible. Mais `rabin2 -I` est pratique quand vous travaillez uniquement avec la suite r2.

---

## 5 — Affichage et désassemblage

### 5.1 — Désassemblage (`p` = print)

| Commande | Description |  
|----------|-------------|  
| `pd 20` | Désassemble 20 instructions à partir de la position courante |  
| `pd -10` | Désassemble 10 instructions **avant** la position courante |  
| `pdf` | Désassemble la fonction courante (print disassembly function) |  
| `pdf @ main` | Désassemble la fonction `main` |  
| `pdr` | Désassemblage récursif de la fonction (suit les sauts internes) |  
| `pds` | Résumé de la fonction (uniquement les appels et les chaînes utilisées) |  
| `pds @ main` | Résumé de `main` : quelles fonctions sont appelées, avec quelles chaînes |  
| `pdc` | Pseudo-code C (décompilation basique intégrée) |  
| `pdc @ main` | Pseudo-code de `main` |  
| `pdg` | Décompilation Ghidra (nécessite le plugin `r2ghidra`) |  
| `pdg @ main` | Décompilation Ghidra de `main` |  
| `pdi 20` | Désassemble 20 instructions (format simplifié : opcode uniquement) |  
| `pid 20` | Instructions avec opcodes bruts (bytes + mnémonique) |  
| `pif` | Instructions de la fonction courante (format simplifié) |

### 5.2 — Affichage hexadécimal

| Commande | Description |  
|----------|-------------|  
| `px 64` | Dump hexadécimal de 64 octets (format `xxd`-like) |  
| `px 64 @ main` | Dump de 64 octets depuis le début de `main` |  
| `pxw 32` | Dump en dwords (4 octets par élément) |  
| `pxq 32` | Dump en qwords (8 octets par élément) |  
| `pxr 64` | Dump avec déréférencement récursif (suit les pointeurs) |  
| `p8 16` | Affiche 16 octets bruts en hexadécimal (compact, sans offset) |  
| `pc 32` | Dump au format tableau C (`unsigned char buf[] = {0x...}`) |  
| `pcp 32` | Dump au format Python (`buf = b"\x..."`) |  
| `pcj 32` | Dump au format JSON |

### 5.3 — Affichage de chaînes et données

| Commande | Description |  
|----------|-------------|  
| `ps @ <addr>` | Affiche la chaîne C (null-terminated) à l'adresse |  
| `psz @ <addr>` | Identique (zero-terminated string) |  
| `psw @ <addr>` | Chaîne wide (UTF-16) |  
| `psp @ <addr>` | Chaîne Pascal (longueur préfixée) |  
| `pf x` | Affiche un dword hexadécimal à la position courante |  
| `pf xxxx` | Affiche 4 dwords consécutifs |  
| `pf s` | Affiche une chaîne C |  
| `pf d` | Affiche un entier signé |  
| `pf q` | Affiche un qword |  
| `pf.elf_header @ 0` | Applique un format de structure nommé (si défini) |

### 5.4 — Configuration de l'affichage

| Commande | Description |  
|----------|-------------|  
| `e asm.syntax = intel` | Passe en syntaxe Intel (recommandé pour le RE) |  
| `e asm.syntax = att` | Syntaxe AT&T |  
| `e asm.bytes = true` | Affiche les opcodes bruts à côté du désassemblage |  
| `e asm.bytes = false` | Masque les opcodes (plus lisible) |  
| `e asm.comments = true` | Affiche les commentaires automatiques |  
| `e asm.describe = true` | Ajoute une description courte de chaque instruction |  
| `e asm.lines = true` | Affiche les lignes de connexion entre les sauts |  
| `e asm.xrefs = true` | Affiche les xrefs inline dans le désassemblage |  
| `e scr.color = 3` | Niveau de coloration maximal (0 = pas de couleur) |  
| `e scr.utf8 = true` | Active les caractères UTF-8 (flèches, bordures) |  
| `e asm.cmt.col = 50` | Colonne des commentaires (ajuste l'alignement) |

Pour rendre la configuration permanente, ajoutez ces commandes dans `~/.radare2rc` :

```
e asm.syntax = intel  
e scr.color = 3  
e scr.utf8 = true  
e asm.bytes = false  
e asm.describe = false  
```

---

## 6 — Graphes visuels et modes interactifs

### 6.1 — Mode visuel (`V`)

Le mode visuel est un affichage interactif plein écran, navigable au clavier. C'est le moyen le plus confortable d'explorer un binaire dans r2 en console.

| Commande / Touche | Description |  
|--------------------|-------------|  
| `V` | Entre en mode visuel (désassemblage défilant) |  
| `V!` | Entre en mode visuel « panneaux » (layout configurable) |  
| `p` / `P` | Cycle entre les vues : hex → désassemblage → debug → résumé |  
| `j` / `k` | Descend / monte d'une ligne |  
| `J` / `K` | Descend / monte d'une page |  
| `Enter` | Suit un `call` ou un `jmp` (entre dans la cible) |  
| `u` | Revient en arrière (undo seek, comme le bouton « Back ») |  
| `U` | Avance dans l'historique (redo seek) |  
| `o` | Aller à une adresse ou un symbole (invite de saisie) |  
| `/` | Recherche de chaîne ou de valeur |  
| `;` | Ajouter un commentaire à l'adresse courante |  
| `d` | Menu de définition : fonction (`df`), données, chaîne, etc. |  
| `n` | Renommer le symbole à l'adresse courante |  
| `x` | Afficher les xrefs vers l'adresse courante |  
| `X` | Afficher les xrefs depuis l'adresse courante |  
| `c` | Afficher le curseur d'édition hexa (mode patch) |  
| `q` | Quitter le mode visuel et revenir au prompt |  
| `:` | Ouvrir le prompt de commande r2 sans quitter le mode visuel |

### 6.2 — Mode graphe (`VV`)

Le mode graphe affiche le graphe de flux de contrôle (CFG) de la fonction courante, avec les blocs de base reliés par des flèches.

| Commande / Touche | Description |  
|--------------------|-------------|  
| `VV` | Entre en mode graphe pour la fonction courante |  
| `VV @ main` | Mode graphe de la fonction `main` |  
| `h` / `j` / `k` / `l` | Navigation directionnelle dans le graphe |  
| `H` / `J` / `K` / `L` | Déplace le graphe plus rapidement |  
| `tab` | Bascule entre les blocs de base (cycle) |  
| `t` / `f` | Suit la branche true / false d'un saut conditionnel |  
| `g` | Sauter à un bloc spécifique |  
| `+` / `-` | Zoom avant / arrière |  
| `0` | Réinitialise le zoom et recentre |  
| `p` | Cycle le mode d'affichage des blocs (asm, mini-blocs, résumé) |  
| `R` | Randomise les couleurs des blocs (aide à distinguer les chemins) |  
| `;` | Ajouter un commentaire |  
| `x` | Afficher les xrefs |  
| `q` | Quitter le mode graphe |

### 6.3 — Mode panneaux (`V!`)

Le mode panneaux permet de diviser l'écran en plusieurs zones simultanées (désassemblage, registres, pile, hexdump) similaire à GEF/pwndbg.

| Touche | Description |  
|--------|-------------|  
| `V!` | Entre en mode panneaux |  
| `tab` | Bascule entre les panneaux |  
| `w` | Menu de gestion des panneaux |  
| `e` | Changer le contenu du panneau sélectionné |  
| `|` | Split vertical |  
| `-` | Split horizontal |  
| `X` | Fermer le panneau sélectionné |  
| `m` | Sélectionner un layout prédéfini |  
| `?` | Aide du mode panneaux |  
| `q` | Quitter |

---

## 7 — Flags, commentaires et annotations

### 7.1 — Flags (marqueurs nommés)

Les flags sont des étiquettes nommées attachées à des adresses. Les symboles du binaire, les noms de fonctions et les chaînes sont tous représentés comme des flags en interne.

| Commande | Description |  
|----------|-------------|  
| `f` | Liste tous les flags |  
| `f flag_name @ 0x401234` | Crée un flag nommé à l'adresse `0x401234` |  
| `f- flag_name` | Supprime un flag |  
| `fs` | Liste les espaces de flags (catégories : `symbols`, `strings`, `imports`, etc.) |  
| `fs strings` | Sélectionne l'espace de flags « strings » |  
| `f~pattern` | Filtre les flags correspondant au motif |  
| `fl` | Nombre total de flags |

### 7.2 — Commentaires

| Commande | Description |  
|----------|-------------|  
| `CC comment text` | Ajoute un commentaire à l'adresse courante |  
| `CC comment text @ 0x401234` | Ajoute un commentaire à une adresse spécifique |  
| `CC-` | Supprime le commentaire à l'adresse courante |  
| `CC` | Affiche le commentaire à l'adresse courante |  
| `CCl` | Liste tous les commentaires |

### 7.3 — Types et structures

| Commande | Description |  
|----------|-------------|  
| `t` | Liste les types chargés |  
| `to file.h` | Charge les types depuis un fichier header C |  
| `ts` | Liste les structures |  
| `ts struct_name` | Affiche la définition d'une structure |  
| `tp struct_name @ addr` | Affiche la mémoire à `addr` interprétée selon la structure |  
| `tl struct_name @ addr` | Lie (link) une structure à une adresse (l'affichage est permanent) |

---

## 8 — Recherche

### 8.1 — Recherche dans le binaire

| Commande | Description |  
|----------|-------------|  
| `/ string` | Cherche la chaîne ASCII « string » |  
| `/x 9090` | Cherche une séquence d'octets hex (`0x90 0x90`) |  
| `/x 4889..24` | Cherche avec des octets wildcard (`.` = n'importe quel nibble) |  
| `/w string` | Cherche une chaîne wide (UTF-16) |  
| `/a jmp rax` | Cherche une instruction assembleur |  
| `/A push rbp; mov rbp, rsp` | Cherche un pattern assembleur multi-instructions |  
| `/r sym.strcmp` | Cherche les références (xrefs) vers `strcmp` |  
| `/R pop rdi` | Cherche des gadgets ROP contenant `pop rdi` |  
| `/R/ pop r..;ret` | Cherche des gadgets par expression régulière |  
| `/c jmp` | Cherche les instructions de type `jmp` |  
| `/v 0xdeadbeef` | Cherche une valeur numérique (gère l'endianness) |  
| `/i password` | Cherche une chaîne insensible à la casse |

### 8.2 — Résultats de recherche

| Commande | Description |  
|----------|-------------|  
| `fs searches` | Sélectionne l'espace de flags des résultats de recherche |  
| `f~hit` | Liste les résultats (chaque match crée un flag `hit0_N`) |

---

## 9 — Débogage

### 9.1 — Contrôle de l'exécution

| Commande | Description |  
|----------|-------------|  
| `dc` | Continue l'exécution |  
| `ds` | Step into (exécute une instruction, entre dans les `call`) |  
| `dso` | Step over (exécute une instruction, passe par-dessus les `call`) |  
| `dsf` | Step until end of function (exécute jusqu'au `ret`) |  
| `dsu <addr>` | Continue jusqu'à l'adresse spécifiée |  
| `dsu sym.main` | Continue jusqu'à `main` |  
| `dcr` | Continue until return (exécute jusqu'au retour de la fonction) |  
| `dcu <addr>` | Continue until address |  
| `dcu sym.main` | Continue until `main` |  
| `dk 9` | Envoie le signal 9 (SIGKILL) au processus |  
| `dk` | Liste les signaux en attente |

### 9.2 — Breakpoints

| Commande | Description |  
|----------|-------------|  
| `db <addr>` | Place un breakpoint à l'adresse |  
| `db sym.main` | Breakpoint au début de `main` |  
| `db-*` | Supprime tous les breakpoints |  
| `db- <addr>` | Supprime le breakpoint à l'adresse |  
| `dbi` | Liste tous les breakpoints avec leur index |  
| `dbe <index>` | Active le breakpoint n° `<index>` |  
| `dbd <index>` | Désactive le breakpoint |  
| `dbH <addr>` | Place un breakpoint matériel (hardware) |  
| `dbc <addr> <cmd>` | Exécute une commande quand le breakpoint est atteint |  
| `dbw <addr> <rw>` | Watchpoint : `r` (lecture), `w` (écriture), `rw` (les deux) |

### 9.3 — Inspection des registres

| Commande | Description |  
|----------|-------------|  
| `dr` | Affiche tous les registres |  
| `dr rax` | Affiche la valeur de `rax` |  
| `dr rax=0x42` | Modifie la valeur de `rax` |  
| `dr=` | Affiche les registres en format compact avec barres de progression |  
| `drt` | Affiche les registres par type (general, fpu, mmx, xmm) |  
| `drt xmm` | Affiche uniquement les registres XMM |  
| `drr` | Déréférencement récursif de chaque registre (suit les pointeurs) |

### 9.4 — Inspection de la pile et de la mémoire

| Commande | Description |  
|----------|-------------|  
| `dbt` | Backtrace (pile d'appels) |  
| `dbt.` | Backtrace à partir de la frame courante |  
| `dm` | Mappage mémoire du processus (équivalent `/proc/pid/maps`) |  
| `dm.` | Section de mémoire contenant l'adresse courante |  
| `dm libc` | Filtre le mappage sur la libc |  
| `dmh` | Informations sur le heap |  
| `dmhg` | Affichage graphique des chunks du heap |  
| `dmhb` | Affiche les bins du heap (fastbin, unsorted, etc.) |  
| `dmp <addr> <size> <perms>` | Change les permissions mémoire d'une page |

### 9.5 — Traces et profiling

| Commande | Description |  
|----------|-------------|  
| `dt` | Affiche les traces collectées |  
| `dts+` | Crée un nouveau timestamp (marqueur temporel) pour le profiling |  
| `dte` | Active le tracing des appels système (syscall) |  
| `e dbg.trace = true` | Active le tracing global (log toutes les instructions exécutées) |

---

## 10 — Écriture et patching

r2 peut modifier un binaire directement si le fichier est ouvert en mode écriture (`r2 -w` ou `oo+`).

| Commande | Description |  
|----------|-------------|  
| `wa nop` | Assemble et écrit un `nop` à la position courante |  
| `wa jmp 0x401250` | Assemble et écrit un `jmp` vers l'adresse spécifiée |  
| `wa nop @ 0x401234` | Écrit un `nop` à l'adresse `0x401234` |  
| `"wa nop;nop;nop"` | Écrit plusieurs instructions (séparées par `;`) |  
| `wx 90` | Écrit l'octet `0x90` (nop) à la position courante |  
| `wx 9090909090` | Écrit 5 octets |  
| `wx 9090 @ 0x401234` | Écrit à une adresse spécifique |  
| `wv 0x41414141` | Écrit une valeur de 4 octets (little-endian) |  
| `wv8 0x4141414141414141` | Écrit une valeur de 8 octets |  
| `wz "hello"` | Écrit une chaîne null-terminated |  
| `wo` | Sous-menu d'opérations sur les octets (xor, add, etc.) |  
| `wox 0xff` | XOR tous les octets du bloc courant avec `0xFF` |  
| `woa 1` | Ajoute 1 à chaque octet du bloc courant |

**Patching de sauts conditionnels** — L'opération la plus courante en RE : inverser un `jz` en `jnz` ou vice versa.

| Patch souhaité | Commande |  
|----------------|----------|  
| `jz` → `jnz` | `wx 75 @ <addr>` (change l'opcode `0x74` en `0x75`) |  
| `jnz` → `jz` | `wx 74 @ <addr>` (change `0x75` en `0x74`) |  
| NOP-out une instruction (2 octets) | `wx 9090 @ <addr>` |  
| NOP-out un `call` (5 octets) | `wx 9090909090 @ <addr>` |  
| Forcer un saut (remplacer `jz` par `jmp` short) | `wx eb @ <addr>` (change en `jmp rel8`) |

> ⚠️ Après toute modification, vérifiez le résultat avec `pd 5 @ <addr>` pour confirmer que le désassemblage est correct. Un octet de trop ou de moins peut désaligner toutes les instructions suivantes.

---

## 11 — Outils compagnons (suite r2)

Radare2 est livré avec plusieurs outils en ligne de commande utilisables indépendamment de la session interactive.

| Outil | Description |  
|-------|-------------|  
| `rabin2` | Analyse de headers et métadonnées binaires (équivalent de `readelf` + `file` + `strings`) |  
| `rasm2` | Assembleur/désassembleur en ligne de commande |  
| `rahash2` | Calcul de hash et checksums |  
| `radiff2` | Diffing de binaires |  
| `rafind2` | Recherche de patterns dans des fichiers |  
| `ragg2` | Générateur de shellcode et de patterns |  
| `rarun2` | Lanceur de programmes avec environnement contrôlé |  
| `rax2` | Convertisseur de bases et calculatrice |

### 11.1 — `rabin2` — Analyse rapide

| Commande | Description |  
|----------|-------------|  
| `rabin2 -I ./binary` | Informations générales (arch, bits, protections, endian) |  
| `rabin2 -z ./binary` | Chaînes dans les sections de données |  
| `rabin2 -zz ./binary` | Chaînes dans tout le binaire |  
| `rabin2 -i ./binary` | Imports |  
| `rabin2 -E ./binary` | Exports |  
| `rabin2 -S ./binary` | Sections |  
| `rabin2 -s ./binary` | Symboles |  
| `rabin2 -l ./binary` | Bibliothèques liées |  
| `rabin2 -e ./binary` | Entrypoints |  
| `rabin2 -H ./binary` | Headers ELF |

### 11.2 — `rasm2` — Assembler/désassembler

| Commande | Description |  
|----------|-------------|  
| `rasm2 -a x86 -b 64 "nop"` | Assemble `nop` → affiche `90` |  
| `rasm2 -a x86 -b 64 "push rbp; mov rbp, rsp"` | Assemble un prologue |  
| `rasm2 -a x86 -b 64 -d "554889e5"` | Désassemble les octets → `push rbp; mov rbp, rsp` |  
| `rasm2 -a x86 -b 64 -D "554889e5"` | Désassemble avec les adresses et les tailles |

### 11.3 — `radiff2` — Diffing

| Commande | Description |  
|----------|-------------|  
| `radiff2 binary_v1 binary_v2` | Diff octet par octet |  
| `radiff2 -g main binary_v1 binary_v2` | Diff graphique des blocs de la fonction `main` |  
| `radiff2 -AC binary_v1 binary_v2` | Diff au niveau des fonctions avec analyse de code |  
| `radiff2 -ss binary_v1 binary_v2` | Diff basé sur la similarité des fonctions |

### 11.4 — `rax2` — Conversions et calculatrice

| Commande | Description |  
|----------|-------------|  
| `rax2 0x41` | Hex → décimal → ASCII (`65 0x41 A`) |  
| `rax2 65` | Décimal → hex |  
| `rax2 -s 414243` | Hex → chaîne ASCII (`ABC`) |  
| `rax2 -S "ABC"` | Chaîne → hex (`414243`) |  
| `rax2 -e 0x41424344` | Swap endianness |  
| `rax2 -b 0xff` | Hex → binaire |  
| `rax2 -k 1024` | Taille lisible (1K) |  
| `rax2 '0x100+0x50'` | Calculatrice hexadécimale (`0x150`) |

### 11.5 — `rahash2` — Hashes et checksums

| Commande | Description |  
|----------|-------------|  
| `rahash2 -a md5 ./binary` | Calcule le hash MD5 du fichier |  
| `rahash2 -a sha256 ./binary` | Hash SHA-256 |  
| `rahash2 -a all ./binary` | Tous les algorithmes de hash |  
| `rahash2 -a crc32 ./binary` | CRC32 |  
| `rahash2 -a entropy -b 256 ./binary` | Entropie par blocs de 256 octets (utile pour détecter le packing) |  
| `rahash2 -D base64 < encoded.txt` | Décode du Base64 |  
| `rahash2 -E base64 < plain.txt` | Encode en Base64 |

### 11.6 — `rafind2` — Recherche dans des fichiers

| Commande | Description |  
|----------|-------------|  
| `rafind2 -ZS "password" ./binary` | Cherche la chaîne « password » |  
| `rafind2 -x 7f454c46 ./binary` | Cherche la séquence hex (ici : magic ELF `\x7fELF`) |  
| `rafind2 -X ./binary` | Affiche les chaînes imprimables (similaire à `strings`) |

### 11.7 — `ragg2` — Générateur de patterns et shellcode

| Commande | Description |  
|----------|-------------|  
| `ragg2 -P 200` | Génère un pattern De Bruijn de 200 octets |  
| `ragg2 -q 0x41416241` | Calcule l'offset correspondant dans un pattern De Bruijn |  
| `ragg2 -a x86 -b 64 -i exec` | Génère un shellcode `execve` x86-64 |

### 11.8 — `rarun2` — Lanceur d'exécution contrôlé

`rarun2` permet de définir un profil d'exécution pour un programme (stdin, stdout, arguments, variables d'environnement, limites) sans écrire de script shell. On l'utilise via un fichier `.rr2` :

```ini
# profile.rr2
program=./binary  
arg1=AAAA  
arg2=test  
stdin=input.txt  
timeout=5  
setenv=DEBUG=1  
```

Lancement :

```bash
r2 -d -e dbg.profile=profile.rr2 ./binary
# ou directement :
rarun2 profile.rr2
```

C'est particulièrement utile pour le fuzzing et l'automatisation des tests en RE, quand vous devez fournir un input reproductible au binaire cible depuis r2.

---

## 12 — Scripting avec r2pipe

r2pipe est la bibliothèque Python officielle pour interagir avec r2 de manière programmatique. Elle ouvre une session r2 et envoie des commandes via un pipe.

### 12.1 — Usage de base

```python
import r2pipe

r2 = r2pipe.open("./binary")  
r2.cmd("aaa")                      # Analyse  

# Informations sur le binaire
info = r2.cmdj("ij")               # 'j' = JSON → retourne un dict Python  
print(f"Architecture: {info['bin']['arch']}")  
print(f"Bits: {info['bin']['bits']}")  

# Lister les fonctions
functions = r2.cmdj("aflj")        # Liste des fonctions en JSON  
for f in functions:  
    print(f"0x{f['offset']:08x}  {f['size']:5d}  {f['name']}")

# Désassembler main
r2.cmd("s main")  
disasm = r2.cmd("pdf")  
print(disasm)  

# Chercher les chaînes
strings = r2.cmdj("izj")  
for s in strings:  
    if "password" in s.get("string", "").lower():
        print(f"0x{s['vaddr']:08x}: {s['string']}")

# Xrefs vers strcmp
xrefs = r2.cmdj("axtj @ sym.imp.strcmp")  
for x in xrefs:  
    print(f"strcmp called from 0x{x['from']:08x} in {x.get('fcn_name', '?')}")

r2.quit()
```

### 12.2 — Commandes r2pipe courantes

| Méthode Python | Description |  
|----------------|-------------|  
| `r2.cmd("commande")` | Exécute une commande et retourne le résultat en texte |  
| `r2.cmdj("commandej")` | Exécute une commande JSON et retourne un objet Python (dict/list) |  
| `r2.quit()` | Ferme la session r2 |

Le pattern standard est d'utiliser `cmdj` avec le suffixe `j` de la commande r2 chaque fois que vous voulez traiter les résultats programmatiquement, et `cmd` quand vous voulez simplement afficher du texte.

### 12.3 — Exemple avancé : analyse batch de plusieurs binaires

```python
import r2pipe  
import os  
import json  

def analyze_binary(path):
    """Analyse un binaire et retourne un résumé structuré."""
    r2 = r2pipe.open(path)
    r2.cmd("aaa")

    info = r2.cmdj("ij")
    functions = r2.cmdj("aflj") or []
    imports = r2.cmdj("iij") or []
    strings = r2.cmdj("izj") or []

    summary = {
        "file": path,
        "arch": info.get("bin", {}).get("arch", "?"),
        "bits": info.get("bin", {}).get("bits", 0),
        "language": info.get("bin", {}).get("lang", "?"),
        "num_functions": len(functions),
        "num_imports": len(imports),
        "num_strings": len(strings),
        "interesting_imports": [
            i["name"] for i in imports
            if any(kw in i.get("name", "").lower()
                   for kw in ["crypt", "strcmp", "exec", "system",
                              "socket", "connect", "send", "recv"])
        ],
        "interesting_strings": [
            s["string"] for s in strings
            if any(kw in s.get("string", "").lower()
                   for kw in ["password", "key", "flag", "secret",
                              "admin", "login", "http", "token"])
        ][:20]  # Limiter à 20 chaînes
    }

    r2.quit()
    return summary

# Analyse de tous les binaires d'un répertoire
results = []  
for fname in os.listdir("./binaries"):  
    fpath = os.path.join("./binaries", fname)
    if os.path.isfile(fpath):
        try:
            result = analyze_binary(fpath)
            results.append(result)
            print(f"[OK] {fname}: {result['num_functions']} fonctions")
        except Exception as e:
            print(f"[ERR] {fname}: {e}")

with open("analysis_report.json", "w") as f:
    json.dump(results, f, indent=2)
print(f"\nRapport écrit dans analysis_report.json ({len(results)} binaires)")
```

Ce type de script illustre la force de r2pipe pour l'automatisation : l'intégralité de l'API r2 est accessible depuis Python, et le suffixe `j` rend le parsing des résultats trivial.

---

## 13 — Cutter — Interface graphique de Radare2

Cutter est le front-end graphique officiel de r2. Il expose les mêmes fonctionnalités que la console r2 dans une interface Qt avec des widgets déplaçables.

### 13.1 — Panneaux principaux

| Panneau | Équivalent r2 | Description |  
|---------|---------------|-------------|  
| **Disassembly** | `pd` / `pdf` | Désassemblage linéaire ou par fonction |  
| **Graph** | `VV` | Graphe de flux de contrôle interactif |  
| **Decompiler** | `pdc` / `pdg` | Pseudo-code C (décompilateur intégré ou r2ghidra) |  
| **Hexdump** | `px` | Vue hexadécimale |  
| **Functions** | `afl` | Liste des fonctions avec recherche et filtrage |  
| **Strings** | `iz` / `izz` | Liste des chaînes avec double-clic pour naviguer |  
| **Imports** | `ii` | Table des imports |  
| **Exports** | `iE` | Table des exports |  
| **Sections** | `iS` | Sections du binaire |  
| **Symbols** | `is` | Table des symboles |  
| **XRefs** | `axt` / `axf` | Cross-références (accessible par clic droit) |  
| **Registers** | `dr` | Registres (en mode debug) |  
| **Stack** | `pxr @ rsp` | Pile (en mode debug) |  
| **Console** | (prompt r2) | Terminal r2 intégré pour les commandes manuelles |  
| **Dashboard** | `i` | Résumé du binaire |

### 13.2 — Raccourcis clavier Cutter

| Raccourci | Description |  
|-----------|-------------|  
| `Space` | Bascule entre vue linéaire et vue graphe |  
| `g` | Aller à une adresse ou un symbole |  
| `n` | Renommer la fonction ou le symbole sélectionné |  
| `;` | Ajouter un commentaire |  
| `x` | Afficher les xrefs vers l'élément sélectionné |  
| `Ctrl+Shift+F` | Recherche dans tout le binaire |  
| `Tab` | Basculer entre le désassemblage et le décompilateur |  
| `Escape` | Retour en arrière dans l'historique de navigation |  
| `Ctrl+F5` | Lancer le débogage |  
| `F2` | Placer/retirer un breakpoint |  
| `F5` | Continuer l'exécution (debug) |  
| `F7` | Step into (debug) |  
| `F8` | Step over (debug) |  
| `F9` | Continue (debug) |  
| `Ctrl+R` | Ouvrir la console r2 intégrée |

### 13.3 — Plugins Cutter

| Plugin | Description |  
|--------|-------------|  
| **r2ghidra** | Intègre le décompilateur de Ghidra dans Cutter (qualité de décompilation nettement supérieure au `pdc` natif) |  
| **r2dec** | Décompilateur alternatif intégré |  
| **r2yara** | Recherche de patterns YARA |  
| **cutterref** | Feuille de référence des commandes intégrée |

Pour installer r2ghidra (recommandé) :

```bash
r2pm -ci r2ghidra
```

Une fois installé, le panneau Decompiler dans Cutter utilise automatiquement le moteur Ghidra, et la commande `pdg` devient disponible en console r2.

---

## 14 — Fichier `~/.radare2rc` recommandé

Le fichier `~/.radare2rc` est lu automatiquement à chaque lancement de r2. Il permet de définir des préférences persistantes sans avoir à les retaper à chaque session.

```bash
# ─── Affichage ──────────────────────────────────
e asm.syntax = intel          # Syntaxe Intel (indispensable pour le RE)  
e scr.color = 3               # Coloration maximale  
e scr.utf8 = true             # Caractères Unicode (flèches, bordures)  
e scr.wheel = true            # Support de la molette souris dans le terminal  

# ─── Désassemblage ──────────────────────────────
e asm.bytes = false           # Masque les opcodes bruts (plus lisible)  
e asm.describe = false        # Pas de description des instructions (trop verbeux)  
e asm.lines = true            # Lignes de connexion entre sauts  
e asm.lines.call = true       # Lignes pour les call aussi  
e asm.xrefs = true            # Affiche les xrefs inline  
e asm.cmt.col = 55            # Colonne des commentaires  
e asm.var = true              # Affiche les noms de variables locales  

# ─── Analyse ────────────────────────────────────
e anal.jmp.ref = true         # Suit les références de sauts pendant l'analyse  
e anal.jmp.cref = true        # Suit les références de call  
e anal.hasnext = true         # Détecte les fonctions qui suivent immédiatement  

# ─── Débogage ───────────────────────────────────
e dbg.follow.child = false    # Suit le parent par défaut après fork  
e dbg.btalgo = fuzzy          # Algorithme de backtrace plus tolérant  

# ─── Performance ────────────────────────────────
e bin.cache = true            # Cache les résultats d'analyse du binaire  
e io.cache = true             # Cache les lectures I/O (plus rapide)  
```

---

## 15 — Correspondance r2 ↔ GDB ↔ Cutter

Ce tableau établit les équivalences entre les trois environnements pour les opérations les plus courantes. Utile si vous avez l'habitude de GDB et que vous passez à r2, ou inversement.

| Opération | GDB | r2 (console) | Cutter |  
|-----------|-----|--------------|--------|  
| Lancer le programme | `run` | `dc` (en mode `-d`) | `Ctrl+F5` |  
| Continuer | `continue` | `dc` | `F9` |  
| Step into | `stepi` | `ds` | `F7` |  
| Step over | `nexti` | `dso` | `F8` |  
| Finish (jusqu'au ret) | `finish` | `dsf` / `dcr` | — |  
| Breakpoint | `break *0x401234` | `db 0x401234` | `F2` |  
| Lister breakpoints | `info breakpoints` | `dbi` | Panneau Breakpoints |  
| Voir registres | `info registers` | `dr` | Panneau Registers |  
| Modifier registre | `set $rax = 42` | `dr rax=42` | Double-clic sur la valeur |  
| Backtrace | `backtrace` | `dbt` | Panneau Stack |  
| Examiner mémoire (hex) | `x/20gx $rsp` | `pxq 160 @ rsp` | Panneau Hexdump |  
| Examiner mémoire (instructions) | `x/10i $rip` | `pd 10 @ rip` | Panneau Disassembly |  
| Chaîne à une adresse | `x/s $rdi` | `ps @ rdi` | — |  
| Memory map | `info proc mappings` | `dm` | — |  
| Désassembler une fonction | `disas main` | `pdf @ main` | Double-clic dans Functions |  
| Chercher une chaîne | `find 0x400000, 0x500000, "str"` | `/ str` | `Ctrl+Shift+F` |  
| Lister les fonctions | `info functions` | `afl` | Panneau Functions |  
| Lister les chaînes | — | `iz` | Panneau Strings |  
| Imports | — | `ii` | Panneau Imports |  
| Sections | `maintenance info sections` | `iS` | Panneau Sections |  
| Xrefs vers | — | `axt <addr>` | Clic droit → Xrefs to |  
| Xrefs depuis | — | `axf <addr>` | Clic droit → Xrefs from |  
| Ajouter commentaire | — | `CC texte` | `;` |  
| Renommer | — | `afn nom` | `n` |  
| Patcher (nop) | `set *(char*)0x...=0x90` | `wx 90 @ 0x...` | Clic droit → Edit → NOP |  
| Graphe de la fonction | — | `VV` | `Space` |  
| Décompiler | — | `pdc` / `pdg` | `Tab` |  
| Quitter | `quit` | `q` | Fermer la fenêtre |

---

## 16 — Workflows types en r2

### 16.1 — Triage rapide d'un binaire inconnu (5 minutes)

```bash
r2 -A ./mystery_bin
```

```
i                        # Format, arch, bits, endianness  
iI                       # Protections (canary, NX, PIE, RELRO)  
ie                       # Entry point  
il                       # Bibliothèques liées  
iS                       # Sections (vérifier les tailles, sections inhabituelles)  
iz                       # Chaînes dans .rodata/.data  
iz~pass                  # Chercher des mots-clés : pass, key, flag, secret, admin  
iz~key  
iz~flag  
ii                       # Imports → quelles fonctions de bibliothèque sont utilisées ?  
ii~crypt                 # Fonctions crypto importées ?  
ii~strcmp                # Comparaisons de chaînes ?  
ii~socket                # Activité réseau ?  
afl                      # Liste des fonctions détectées  
afl~main                 # La fonction main  
pdf @ main               # Désassembler main pour une vue d'ensemble  
pds @ main               # Résumé de main : appels et chaînes utilisées  
```

Ce workflow correspond au « triage rapide » du chapitre 5.7 transposé en commandes r2. En moins de 5 minutes, vous avez une vue d'ensemble de ce que fait le binaire, de ses dépendances, de ses chaînes intéressantes et de sa structure de fonctions.

### 16.2 — Tracer une chaîne suspecte jusqu'au code

```
iz~password              # Trouver la chaîne
                         # Résultat : 0x00402010 "Enter password:"
axt @ 0x00402010         # Qui utilise cette chaîne ?
                         # Résultat : appel depuis 0x00401185 dans sym.check_auth
s sym.check_auth         # Se déplacer à la fonction  
pdf                      # Désassembler la fonction complète  
VV                       # Graphe de flux de contrôle pour voir les branches  
```

### 16.3 — Analyser un crackme en mode debug

```bash
r2 -d -A ./crackme
```

```
db sym.main              # Breakpoint à main  
dc                       # Run  
pdf                      # Voir où on en est  
afl~check                # Chercher une fonction de vérification  
db sym.check_password    # Breakpoint sur la vérification  
dc                       # Continue (le programme attend un input)  
                         # → Taper un mot de passe bidon dans le terminal
dr                       # Inspecter les registres au point de comparaison  
ps @ rdi                 # Voir la chaîne en premier argument (notre input ?)  
ps @ rsi                 # Voir la chaîne en second argument (le mot de passe ?)  
```

### 16.4 — Patcher un binaire pour contourner une vérification

```bash
r2 -w ./crackme          # Ouvrir en écriture
```

```
aaa                      # Analyse  
afl~check                # Trouver la fonction de vérification  
pdf @ sym.check_password # Désassembler  
                         # Repérer le jnz/jz critique
                         # Supposons qu'il est à 0x00401234
wx 75 @ 0x00401234       # Inverser jz (0x74) en jnz (0x75)
                         # OU
wx eb @ 0x00401234       # Forcer un jmp inconditionnel  
pd 5 @ 0x00401234        # Vérifier le patch  
q                        # Quitter (les modifications sont sauvegardées)  
```

---

## 17 — Aide-mémoire : les 30 commandes essentielles

Pour les sessions de RE quotidiennes, ce tableau condense les commandes à connaître par cœur. Si vous ne devez retenir qu'une page de cette annexe, c'est celle-ci.

| # | Commande | Ce qu'elle fait |  
|---|----------|-----------------|  
| 1 | `r2 -A ./bin` | Ouvre + analyse automatique |  
| 2 | `aaa` | Analyse approfondie |  
| 3 | `i` | Infos générales du binaire |  
| 4 | `iS` | Sections |  
| 5 | `ii` | Imports |  
| 6 | `iz` | Chaînes |  
| 7 | `afl` | Liste des fonctions |  
| 8 | `s <addr/sym>` | Se déplacer (seek) |  
| 9 | `s-` | Retour en arrière |  
| 10 | `pdf` | Désassembler la fonction courante |  
| 11 | `pd 20` | Désassembler 20 instructions |  
| 12 | `pds` | Résumé de la fonction (appels + chaînes) |  
| 13 | `pdc` / `pdg` | Décompiler |  
| 14 | `px 64` | Dump hex |  
| 15 | `ps @ <addr>` | Afficher une chaîne C |  
| 16 | `axt <addr>` | Xrefs vers (qui utilise cette adresse ?) |  
| 17 | `axf <addr>` | Xrefs depuis (qu'appelle cette adresse ?) |  
| 18 | `/ string` | Chercher une chaîne |  
| 19 | `/R pop rdi` | Chercher un gadget ROP |  
| 20 | `V` | Mode visuel |  
| 21 | `VV` | Mode graphe |  
| 22 | `afn <name>` | Renommer une fonction |  
| 23 | `CC <text>` | Ajouter un commentaire |  
| 24 | `db <addr>` | Breakpoint (mode debug) |  
| 25 | `dc` | Continuer (mode debug) |  
| 26 | `ds` / `dso` | Step into / step over |  
| 27 | `dr` | Registres |  
| 28 | `dm` | Memory map |  
| 29 | `wx <hex>` | Écrire des octets (patch) |  
| 30 | `q` | Quitter |

---

> 📚 **Pour aller plus loin** :  
> - **Annexe C** — [Cheat sheet GDB / GEF / pwndbg](/annexes/annexe-c-cheatsheet-gdb.md) — la fiche de référence du débogueur complémentaire.  
> - **Annexe E** — [Cheat sheet ImHex : syntaxe `.hexpat`](/annexes/annexe-e-cheatsheet-imhex.md) — référence pour l'analyse hexadécimale avancée.  
> - **Chapitre 9, sections 9.2–9.4** — [Radare2 / Cutter et scripting r2pipe](/09-ida-radare2-binja/02-radare2-cutter.md) — couverture pédagogique de r2 avec des cas d'usage progressifs.  
> - **Radare2 Book** — [https://book.rada.re/](https://book.rada.re/) — le manuel officiel complet de r2.  
> - **Cutter** — [https://cutter.re/](https://cutter.re/) — documentation et téléchargement de l'interface graphique.  
> - **r2pipe** — `pip install r2pipe` — documentation sur [GitHub](https://github.com/radareorg/radare2-r2pipe).

⏭️ [Cheat sheet ImHex : syntaxe `.hexpat` de référence](/annexes/annexe-e-cheatsheet-imhex.md)
