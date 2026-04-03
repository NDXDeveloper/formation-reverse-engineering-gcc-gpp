🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 9.2 — Radare2 / Cutter — analyse en ligne de commande et GUI

> 📘 **Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja**  
> Section précédente : [9.1 — IDA Free — workflow de base sur binaire GCC](/09-ida-radare2-binja/01-ida-free-workflow.md)

---

## Radare2 : un framework, pas juste un désassembleur

Radare2 (souvent abrégé `r2`) est un projet open source fondamentalement différent des outils vus jusqu'ici. Là où IDA et Ghidra sont des applications graphiques centrées sur une interface de navigation visuelle, Radare2 est d'abord et avant tout un **framework en ligne de commande**. Son architecture modulaire couvre le désassemblage, le débogage, l'analyse de binaires, le patching, le diffing, l'émulation, la recherche de gadgets ROP, et bien davantage — le tout pilotable depuis un terminal.

Cette approche CLI-first déconcerte souvent au premier contact. L'interface est austère, les commandes sont cryptiques (souvent deux ou trois lettres), et il n'y a pas de vue graphe dès l'ouverture. Mais c'est précisément cette philosophie qui fait la puissance de l'outil : chaque commande produit une sortie textuelle composable, filtrable et scriptable. Un analyste qui maîtrise `r2` peut enchaîner des dizaines d'opérations complexes à une vitesse qu'aucune interface graphique ne permet d'atteindre.

Radare2 est né en 2006 comme un éditeur hexadécimal en ligne de commande. Il a évolué au fil des années en un écosystème complet, entièrement gratuit et open source (licence LGPL v3). Il est disponible sur Linux, macOS, Windows, et même sur des plateformes plus exotiques comme Android ou iOS. Sa communauté est active, et l'outil évolue rapidement — parfois au prix de changements d'interface entre versions.

## Architecture de Radare2

Comprendre l'architecture interne de `r2` aide à appréhender sa logique d'utilisation. Le framework est composé de plusieurs bibliothèques et binaires spécialisés.

### Les composants principaux

Le cœur du framework est la bibliothèque `libr`, découpée en modules :

- **r_bin** — parsing des formats binaires (ELF, PE, Mach-O, DEX, etc.). C'est le module qui lit les headers, les sections, les symboles, les imports, les relocations. Il joue le même rôle que `readelf` et `objdump -h` combinés, mais de manière programmatique.  
- **r_asm** — désassemblage et assemblage. Supporte un nombre impressionnant d'architectures : x86, ARM (32 et 64), MIPS, PowerPC, SPARC, RISC-V, 6502, Z80, et des dizaines d'autres. Chaque architecture est un plugin, ce qui rend le système extensible.  
- **r_anal** — analyse statique du code. C'est le moteur qui identifie les fonctions, construit les graphes de flux, résout les références croisées et calcule les variables locales. Équivalent au moteur d'auto-analyse d'IDA.  
- **r_core** — le shell interactif qui lie tous les modules ensemble. Quand vous tapez une commande dans `r2`, c'est `r_core` qui l'interprète et dispatch vers le module approprié.  
- **r_debug** — le débogueur intégré. Il supporte `ptrace` sous Linux, le débogage de processus natifs, et peut se connecter à `gdbserver` en mode distant.  
- **r_io** — la couche d'entrée/sortie abstraite. Elle permet de travailler sur des fichiers, des processus en cours d'exécution, des dumps mémoire, des connexions réseau, ou même des ressources distantes via des protocoles comme `gdb://` ou `http://`.

### Les binaires compagnons

Radare2 s'installe avec une suite d'outils en ligne de commande, chacun autonome :

| Binaire | Rôle |  
|---|---|  
| `r2` / `radare2` | Shell interactif principal — c'est l'outil que vous utiliserez 90 % du temps |  
| `rabin2` | Inspection de binaires (headers, symboles, imports, strings, sections) — équivalent léger de `readelf` + `nm` + `strings` |  
| `rasm2` | Assembleur / désassembleur en ligne de commande — convertit entre mnémoniques et octets |  
| `rahash2` | Calcul de hashes (MD5, SHA1, SHA256, CRC32…) sur des fichiers ou des plages d'octets |  
| `radiff2` | Diffing de binaires — comparaison entre deux fichiers ou deux fonctions (couvert au chapitre 10.4) |  
| `rafind2` | Recherche de patterns dans un fichier (chaînes, séquences d'octets, expressions régulières) |  
| `ragg2` | Générateur de shellcode et de petits binaires |  
| `rarun2` | Lanceur de programmes avec contrôle fin de l'environnement (stdin, variables d'environnement, redirections) |  
| `r2pm` | Gestionnaire de paquets pour installer des plugins, scripts et extensions `r2` |

Ces outils sont utilisables indépendamment, sans lancer le shell `r2`. Par exemple, `rabin2 -I keygenme_O2_strip` affiche les informations générales du binaire (format, architecture, endianness, protections) en une seule commande, de manière comparable à `file` + `checksec`.

## Installation de Radare2

### Depuis les sources (recommandé)

La méthode recommandée est la compilation depuis les sources, car les versions dans les gestionnaires de paquets des distributions sont souvent en retard de plusieurs mois, et `r2` évolue rapidement.

```bash
git clone https://github.com/radareorg/radare2.git  
cd radare2  
sys/install.sh  
```

Le script `sys/install.sh` compile et installe `r2` et tous ses outils compagnons. Sur une machine moderne, la compilation prend quelques minutes. Pour mettre à jour :

```bash
cd radare2  
git pull  
sys/install.sh  
```

### Depuis le gestionnaire de paquets

Si vous préférez la simplicité au prix d'une version potentiellement ancienne :

```bash
# Debian / Ubuntu
sudo apt install radare2

# Arch Linux
sudo pacman -S radare2

# macOS (Homebrew)
brew install radare2
```

### Vérification

```bash
r2 -v
# Affiche la version et la date de build

rabin2 -I /bin/ls
# Affiche les informations du binaire /bin/ls — si ça fonctionne, l'installation est correcte
```

## Premier contact avec le shell `r2`

Ouvrons notre binaire fil rouge dans `r2` :

```bash
r2 keygenme_O2_strip
```

Le terminal affiche un prompt de la forme :

```
[0x00401050]>
```

L'adresse entre crochets est le **seek** courant — la position dans le binaire où vous vous trouvez. Ici, `0x00401050` est le point d'entrée (`_start`). Toute commande que vous tapez s'exécute par rapport à cette position, sauf si vous spécifiez une autre adresse.

### Le concept de seek

Le seek est le concept central de `r2`. Pensez-y comme un curseur dans un éditeur de texte : il indique où vous êtes. De nombreuses commandes opèrent sur « l'adresse courante » — désassembler la fonction au seek, afficher les octets au seek, poser un breakpoint au seek. Pour déplacer le seek :

```
[0x00401050]> s main
[0x00401160]>
```

La commande `s` (*seek*) déplace le curseur à l'adresse du symbole `main` (si le binaire a des symboles) ou à une adresse numérique (`s 0x401160`). Le prompt reflète immédiatement la nouvelle position.

### Lancer l'analyse

Par défaut, `r2` n'analyse pas automatiquement le binaire au chargement. C'est un choix de design délibéré : contrairement à IDA ou Ghidra qui lancent une analyse complète dès l'import, `r2` vous laisse le contrôle. Cela permet d'ouvrir un binaire de 500 Mo sans attendre 10 minutes — vous analysez uniquement ce dont vous avez besoin.

Pour lancer l'analyse, vous utilisez les commandes de la famille `a` (*analyze*) :

```
[0x00401050]> aaa
```

La commande `aaa` est le raccourci pour « analyser tout en profondeur ». Elle enchaîne plusieurs passes d'analyse : identification des fonctions, calcul des références croisées, résolution des noms de fonctions importées, propagation des types, et analyse récursive des appels. C'est l'équivalent de l'auto-analyse d'IDA.

> 💡 Vous pouvez aussi lancer l'analyse dès l'ouverture avec le flag `-A` : `r2 -A keygenme_O2_strip`. C'est l'usage le plus courant.

La section 9.3 détaillera les commandes d'analyse et toutes les commandes essentielles de `r2`.

### La logique mnémonique des commandes

Les commandes `r2` suivent un système hiérarchique qui peut sembler chaotique au début mais qui obéit à une logique cohérente. Chaque commande est construite à partir d'une lettre racine qui indique le domaine, suivie de modificateurs :

- `a` — **a**nalyze (analyse statique)  
- `p` — **p**rint (afficher du contenu)  
- `i` — **i**nfo (informations sur le binaire)  
- `s` — **s**eek (déplacement)  
- `w` — **w**rite (écriture / patching)  
- `d` — **d**ebug (débogage)  
- `/` — search (recherche)  
- `V` — **V**isual mode (modes visuels)

Le principe fondamental pour découvrir les commandes est d'ajouter `?` à n'importe quelle lettre pour obtenir l'aide :

```
[0x00401050]> a?
Usage: a  [abdefFghoprxstc] [...]
| a                  alias for aai - analysis information
| aa[?]              analyze all (fcns + bbs) (aa0 to avoid sub renaming)
| aaa[?]             autoname functions after aa (see afna)
| ...
```

Puis `aa?` pour affiner, `aaa?` pour encore plus de détail, et ainsi de suite. Cette exploration arborescente est le moyen naturel d'apprendre `r2`. Il n'est pas nécessaire (ni possible) de tout mémoriser : les analystes expérimentés consultent l'aide intégrée en permanence.

### Sortir de `r2`

```
[0x00401050]> q
```

La commande `q` (*quit*) ferme la session. Si vous avez modifié le binaire (patching), `r2` vous demandera confirmation.

## Les modes visuels de `r2`

Bien que `r2` soit fondamentalement un outil CLI, il propose plusieurs modes visuels en plein écran dans le terminal, qui offrent une expérience plus proche d'un désassembleur graphique.

### Mode visuel de base (`V`)

La commande `V` active le mode visuel. L'écran se divise en panneaux affichant le désassemblage, les registres, la pile, etc. Vous naviguez avec les touches directionnelles, et vous basculez entre différentes vues en appuyant sur `p` (cycle entre hex, désassemblage, débogage, etc.).

En mode visuel, le curseur se déplace d'instruction en instruction avec les flèches haut/bas. Appuyer sur **Entrée** sur un `call` ou un `jmp` suit la référence (comme dans IDA). Appuyer sur `u` revient en arrière.

| Touche | Action en mode visuel |  
|---|---|  
| `p` / `P` | Vue suivante / précédente (hex, désassemblage, debug…) |  
| `Entrée` | Suivre un appel ou un saut |  
| `u` | Revenir en arrière |  
| `j` / `k` | Descendre / monter d'une ligne |  
| `:` | Ouvrir le prompt de commande (taper une commande `r2` sans quitter le mode visuel) |  
| `q` | Quitter le mode visuel (retour au prompt) |

### Mode graphe (`VV`)

La commande `VV` (ou `V` puis `V` depuis le mode visuel) affiche le **graphe de flux de la fonction courante** en ASCII art, directement dans le terminal. Chaque bloc de base est un rectangle de texte, relié aux suivants par des lignes et des flèches. C'est l'équivalent terminal du mode graphe d'IDA.

```
[0x00401160]> VV
```

La navigation dans le graphe se fait avec les flèches, `tab` pour passer au bloc suivant, et les mêmes raccourcis que le mode visuel. Le rendu est naturellement plus spartiate que dans une interface graphique, mais il est fonctionnel et disponible partout — y compris via SSH sur un serveur distant sans environnement graphique, ce qui est un avantage considérable.

### Panneaux (`V!`)

Le mode panneaux (`V!` ou `v` selon les versions) offre une interface encore plus riche, avec des fenêtres redimensionnables et configurables : désassemblage, registres, pile, chaînes, fonctions, graphe… Ce mode se rapproche d'un IDE et peut être configuré pour afficher exactement les informations dont vous avez besoin côte à côte.

## Cutter : l'interface graphique de Radare2

### Pourquoi Cutter ?

La puissance de `r2` en ligne de commande est indéniable, mais la courbe d'apprentissage est raide. Pour les analystes qui préfèrent une interface graphique ou qui débutent avec le framework, le projet Radare2 propose **Cutter** — une interface graphique complète construite au-dessus du moteur `r2`.

Cutter n'est pas un outil séparé qui réimplémente les fonctionnalités de `r2`. C'est une couche graphique Qt/C++ qui appelle directement les commandes `r2` en arrière-plan. Cela signifie que toute la puissance d'analyse de `r2` est disponible, avec la navigation graphique en plus. Vous pouvez même ouvrir une console `r2` intégrée dans Cutter pour taper des commandes CLI quand l'interface graphique ne suffit pas — le meilleur des deux mondes.

### Installation

Cutter est distribué sous forme d'AppImage sur Linux, ce qui rend l'installation triviale :

```bash
# Télécharger l'AppImage depuis le site officiel ou GitHub
# https://cutter.re ou https://github.com/rizinorg/cutter/releases
chmod +x Cutter-*.AppImage
./Cutter-*.AppImage
```

Sur d'autres systèmes, des packages natifs sont disponibles. Le script `check_env.sh` du chapitre 4 vérifie la présence de Cutter.

> ⚠️ **Note sur Rizin et Cutter.** Le projet Cutter a historiquement été basé sur Radare2, mais en 2020, un fork nommé **Rizin** a été créé à partir du code de `r2`. Cutter utilise désormais Rizin comme moteur par défaut. En pratique, les différences entre Rizin et Radare2 sont mineures pour l'usage couvert dans ce chapitre — les concepts, la logique de commandes et l'interface sont les mêmes. Si vous installez Cutter depuis les releases officielles récentes, le moteur sous-jacent sera Rizin, mais les commandes que vous y taperez restent largement compatibles avec `r2`. Nous utilisons `r2` (le Radare2 original) dans les exemples CLI de ce chapitre, et Cutter pour les exemples GUI.

### L'interface de Cutter

Au lancement, Cutter vous demande de sélectionner un binaire, puis propose des options d'analyse similaires à celles d'IDA et Ghidra. Après validation, le binaire est chargé et analysé.

L'interface se compose de widgets disposables et ancrables autour d'une vue centrale :

**La vue Désassemblage** occupe le centre de l'écran. Comme dans IDA, elle offre un mode texte (listing linéaire) et un mode graphe (blocs de base interconnectés). Le mode graphe de Cutter est visuellement plus agréable que le ASCII art de `VV` dans le terminal : les blocs sont des rectangles colorés, les arêtes sont dessinées proprement, et les branchements conditionnels sont codés par couleur (vert pour « pris », rouge pour « non pris »).

**Le widget Functions** (panneau latéral gauche, typiquement) liste toutes les fonctions détectées, avec la possibilité de filtrer, trier, et chercher par nom ou adresse. Un double-clic navigue vers la fonction.

**Le widget Strings** affiche les chaînes de caractères extraites du binaire. Le double-clic navigue vers la chaîne dans la vue de données, et les XREF sont accessibles par clic droit.

**Le widget Décompileur** est l'un des points forts de Cutter. Il intègre le décompileur **Ghidra** (via le plugin `r2ghidra` / `rz-ghidra`) directement dans l'interface. Cela signifie que vous pouvez avoir une vue décompilée de qualité comparable à celle de Ghidra, synchronisée avec le désassemblage, dans l'interface de Cutter. Ce décompileur est disponible sans coût supplémentaire — c'est un avantage significatif par rapport à IDA Free.

**Le widget Console** donne accès au prompt `r2` / Rizin. Vous pouvez y taper n'importe quelle commande `r2` et voir le résultat. C'est la passerelle entre l'interface graphique et la puissance du CLI.

**Autres widgets disponibles :**

- **Hex View** — éditeur hexadécimal intégré.  
- **Imports / Exports** — listes des symboles importés et exportés.  
- **Sections / Segments** — cartographie des sections ELF.  
- **XREF** — références croisées de l'élément sélectionné.  
- **Dashboard** — vue d'ensemble avec les métadonnées du binaire (format, architecture, entropie, hashes).  
- **Graphe des appels** — visualisation du call graph global.

### Renommage et annotation dans Cutter

Cutter offre les mêmes capacités d'annotation que les autres désassembleurs :

- **Renommer** — clic droit sur un nom de fonction ou de variable → *Rename*. Le nouveau nom se propage dans toute l'analyse.  
- **Commenter** — clic droit sur une instruction → *Add comment*. Le commentaire apparaît en marge du désassemblage.  
- **Modifier un type** — clic droit sur une fonction → *Edit function* pour modifier la signature.

Ces annotations sont stockées dans le « projet » Cutter (qui est en réalité un projet `r2` sérialisé). Vous pouvez sauvegarder et rouvrir votre analyse ultérieurement.

### Workflow dans Cutter sur `keygenme_O2_strip`

Le workflow dans Cutter ressemble fortement à celui décrit pour IDA Free en section 9.1, avec les ajustements d'interface :

**1 — Ouvrir et analyser.** Sélectionner le binaire, laisser Cutter lancer l'analyse (l'option « aaaa » dans les paramètres d'analyse est l'équivalent d'une analyse approfondie).

**2 — Inspecter le Dashboard.** Le widget Dashboard donne une vue d'ensemble immédiate : architecture, format, protections (canary, NX, PIE, RELRO), entropie. C'est l'équivalent de `checksec` + `file` en un coup d'œil.

**3 — Explorer les chaînes.** Ouvrir le widget Strings, chercher des messages révélateurs. Double-cliquer pour naviguer.

**4 — Remonter les XREF.** Clic droit sur la chaîne → *Show X-Refs* (ou touche `X`). Naviguer vers la fonction qui utilise cette chaîne.

**5 — Passer en mode graphe.** Basculer la vue désassemblage en mode graphe pour visualiser le flux de contrôle.

**6 — Consulter le décompileur.** Ouvrir le widget décompileur pour voir le pseudo-code C de la fonction courante. Comparer avec le désassemblage pour vérifier la cohérence.

**7 — Annoter.** Renommer les fonctions et variables, ajouter des commentaires, sauvegarder le projet.

## Radare2 CLI vs Cutter : lequel choisir ?

Les deux accèdent au même moteur d'analyse. Le choix dépend du contexte.

**Privilégiez le CLI (`r2`) quand :**

- Vous travaillez sur un serveur distant via SSH sans environnement graphique — `r2` fonctionne dans n'importe quel terminal.  
- Vous avez besoin de scripter une analyse (avec `r2pipe`, couvert en section 9.4) ou d'enchaîner des commandes rapidement.  
- Vous analysez un grand nombre de binaires en batch — le mode non-interactif de `r2` (`r2 -qc 'commandes' binaire`) permet d'exécuter des séquences de commandes sans interaction.  
- Vous êtes à l'aise avec le CLI et vous voulez la vitesse brute : pas de latence de rendu graphique, pas de menus à parcourir.

**Privilégiez Cutter quand :**

- Vous débutez avec le framework Radare2 et vous voulez explorer l'interface visuellement avant de mémoriser les commandes.  
- Vous avez besoin du décompileur Ghidra intégré et synchronisé avec le désassemblage.  
- Vous faites une analyse approfondie d'une seule cible et vous voulez voir simultanément le graphe, le pseudo-code, les chaînes et les registres.  
- Vous préparez des captures d'écran ou une présentation de votre analyse.

En pratique, beaucoup d'analystes utilisent les deux de manière complémentaire : Cutter pour l'exploration visuelle et la décompilation, puis le terminal `r2` (ou la console intégrée dans Cutter) dès qu'une tâche répétitive ou un filtrage précis est nécessaire.

## Points forts de Radare2 dans l'écosystème

Pour conclure cette présentation, voici les domaines où `r2` se distingue particulièrement par rapport aux autres outils du chapitre :

- **Support d'architectures** — `r2` supporte plus d'architectures que tout autre outil gratuit. Si vous travaillez sur du firmware embarqué (ARM Cortex-M, MIPS, AVR, 8051…), c'est souvent le seul outil open source qui couvre votre cible.  
- **Légèreté** — `r2` tient dans quelques mégaoctets et s'installe sans dépendance lourde. Il fonctionne sur des machines modestes et dans des conteneurs Docker.  
- **Composabilité** — chaque commande produit une sortie textuelle utilisable dans un pipe Unix, exportable en JSON (`~{}` suffixé à la commande, ou le flag `j` sur beaucoup de commandes), ou traitable dans un script. C'est l'esprit Unix poussé à l'extrême.  
- **Patching intégré** — `r2` peut écrire dans le binaire directement (`r2 -w binaire`), ce qui en fait un outil de patching binaire rapide sans outil externe.  
- **Émulation** — via le plugin ESIL (*Evaluable Strings Intermediate Language*), `r2` peut émuler des instructions sans exécuter le binaire. Cela permet de tracer l'évolution des registres et de la mémoire de manière purement statique.  
- **Communauté CTF** — `r2` est très populaire dans la communauté CTF, où la rapidité d'analyse et le scripting sont des avantages décisifs. De nombreux writeups sont écrits avec `r2`, et le connaître vous permettra de les suivre.

---


⏭️ [`r2` : commandes essentielles (`aaa`, `pdf`, `afl`, `iz`, `iS`, `VV`)](/09-ida-radare2-binja/03-r2-commandes-essentielles.md)
