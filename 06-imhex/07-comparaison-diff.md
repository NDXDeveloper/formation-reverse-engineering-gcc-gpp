🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.7 — Comparaison de deux versions d'un même binaire GCC (diff)

> 🎯 **Objectif de cette section** : Utiliser la vue Diff d'ImHex pour comparer visuellement deux binaires ELF issus de la même source C/C++ mais compilés différemment, comprendre la nature des différences observées, et savoir dans quels scénarios de RE cette technique apporte une valeur que les outils de diffing structurel (BinDiff, Diaphora) ne couvrent pas.

> 📦 **Binaires de test** : `binaries/ch21-keygenme/keygenme_O0` et `binaries/ch21-keygenme/keygenme_O2`, ou tout couple de binaires compilés depuis la même source avec des flags GCC différents.

---

## Pourquoi comparer des binaires au niveau hexadécimal ?

Le chapitre 10 est entièrement consacré au diffing de binaires avec des outils structurels comme BinDiff, Diaphora et `radiff2`. Ces outils comparent au niveau des **fonctions** : ils identifient quelles fonctions ont changé, quelles instructions ont été ajoutées ou supprimées, et produisent des graphes de correspondance entre les deux versions. C'est l'approche idéale pour comprendre l'impact d'un patch de sécurité ou l'évolution d'un algorithme entre deux releases.

Mais il existe des scénarios où le diffing structurel ne suffit pas, et où une comparaison **octet par octet** dans ImHex est plus appropriée.

**Vérifier un patch binaire localisé.** Vous avez modifié un seul octet dans un binaire (inverser un `jz` en `jnz`, par exemple — chapitre 21). Avant de tester, vous voulez confirmer visuellement que votre modification touche exactement l'octet voulu et rien d'autre. Un diff hexadécimal est immédiat ; importer les deux versions dans BinDiff serait disproportionné.

**Comparer l'impact des flags de compilation.** Vous compilez le même `hello.c` avec `-O0` puis avec `-O2`. Les deux binaires sont structurellement très différents (fonctions inlinées, boucles déroulées, code réordonné), mais vous voulez observer les différences au niveau brut : quelle est la différence de taille ? Quelles sections ont grossi ou rétréci ? Le header ELF a-t-il changé ? Le diff hexadécimal répond à ces questions rapidement.

**Comparer avant et après stripping.** Vous lancez `strip` sur un binaire. Quelles sections ont été supprimées ? Les octets du code machine dans `.text` ont-ils changé ? Le diff hexadécimal montre immédiatement que `.text` est identique mais que les sections `.symtab`, `.strtab` et les informations DWARF ont disparu.

**Analyser des données embarquées.** Deux versions d'un binaire contiennent des configurations différentes hardcodées dans `.rodata` ou `.data`. Le diff structurel (au niveau fonctions) ne verra aucune différence dans le code — seules les données changent. Le diff hexadécimal les révèle directement.

**Détecter des modifications furtives.** Dans un contexte d'analyse malware (partie VI), vous comparez un binaire sain avec un binaire potentiellement trojanisé. Les modifications peuvent porter sur quelques octets dans une section de données ou un saut détourné. Le diff hexadécimal ne rate rien — chaque octet modifié est surlignée.

---

## Utiliser la vue Diff d'ImHex

### Ouvrir deux fichiers

ImHex supporte les **onglets multiples**. Ouvrez le premier binaire via **File → Open File**, puis ouvrez le second dans un nouvel onglet via **File → Open File** à nouveau (ou `Ctrl+O`). Vous devriez voir deux onglets dans la barre supérieure de la vue hexadécimale, chacun portant le nom du fichier.

### Activer la vue Diff

Accédez à la vue Diff via **View → Diff**. Un panneau s'ouvre vous demandant de sélectionner les deux fournisseurs de données (« providers ») à comparer. Choisissez vos deux fichiers dans les menus déroulants — le premier comme « Provider A » et le second comme « Provider B ».

ImHex affiche alors les deux fichiers **côte à côte** dans le panneau Diff. Les colonnes sont synchronisées : chaque ligne montre les mêmes offsets dans les deux fichiers. Le code couleur est le suivant :

- **Fond neutre** — les octets sont identiques dans les deux fichiers.  
- **Fond coloré (surlignage)** — les octets diffèrent. La couleur exacte dépend de votre thème ImHex, mais elle est conçue pour attirer immédiatement l'attention.

### Naviguer entre les différences

La vue Diff fournit des boutons de navigation (flèches haut/bas ou un résumé des régions différentes) qui vous permettent de **sauter d'une différence à la suivante**. Quand les deux fichiers sont largement identiques avec quelques zones de divergence (cas typique d'un patch ou d'une recompilation avec des flags proches), cette navigation est bien plus efficace que de scroller manuellement dans des centaines de kilooctets d'octets identiques.

Le défilement est synchronisé entre les deux vues : si vous scrollez manuellement dans Provider A, Provider B suit au même offset. Cela vous permet de balayer visuellement le fichier tout en repérant les zones divergentes par leur couleur.

---

## Cas d'étude 1 : `-O0` vs `-O2` sur un même source

Compilons le keygenme avec deux niveaux d'optimisation et comparons les résultats :

```bash
cd binaries/ch21-keygenme/  
make keygenme_O0 keygenme_O2  
```

Ouvrez `keygenme_O0` et `keygenme_O2` dans ImHex et activez la vue Diff.

### Ce que vous observez

**Le ELF Header (offsets 0x00–0x3F).** La plupart des champs sont identiques : même magic number, même architecture, même type. Mais `e_entry` (le point d'entrée) peut différer si le linker a placé `_start` à une adresse différente. `e_shoff` (l'offset de la Section Header Table) diffère presque certainement, car la taille du code a changé et les sections sont décalées.

**La section `.text`.** C'est ici que les différences sont les plus massives. Le code compilé en `-O0` est verbeux : chaque variable est stockée sur la pile, chaque accès passe par un `mov` mémoire, les fonctions ne sont pas inlinées. Le code `-O2` est compact : les variables vivent dans les registres, les fonctions courtes sont inlinées, les boucles sont déroulées. Le diff hexadécimal montre un bloc quasi entièrement divergent sur toute la longueur de `.text`.

**Les sections `.rodata` et `.data`.** Si le programme contient des constantes chaînes ou des données initialisées, ces sections sont souvent identiques entre `-O0` et `-O2` — les optimisations portent sur le code, pas sur les données. Le diff confirme cette hypothèse : les octets de `.rodata` sont neutres (pas de surlignage).

**Les sections de débogage.** Le binaire `-O0` compilé avec `-g` contient des sections DWARF volumineuses (`.debug_info`, `.debug_abbrev`, `.debug_line`, etc.) qui sont absentes du binaire `-O2` compilé sans `-g`. Si les deux binaires ont été compilés sans `-g`, ces sections n'existent dans aucun des deux et cette différence ne s'applique pas.

**La taille globale.** Le binaire `-O2` est généralement plus petit que le `-O0` pour le code, mais peut être plus grand si l'inlining a dupliqué du code. La barre de défilement du diff vous donne une indication visuelle immédiate de la différence de taille — si Provider A est plus long que Provider B, la zone au-delà de la fin de B apparaît comme entièrement différente.

### Ce que vous en tirez

Ce diff ne sert pas à comprendre les optimisations en détail — le chapitre 16 y est consacré avec des outils adaptés (comparaison de désassemblage, graphes de flux). En revanche, il vous donne une **vue d'ensemble structurelle** en quelques secondes : quelles régions du fichier sont impactées par les optimisations, quelle est l'ampleur du changement, et quelles sections restent stables. Cette information guide votre stratégie d'analyse : si vous cherchez des constantes cryptographiques, vous savez que `.rodata` est un terrain stable entre les niveaux d'optimisation.

---

## Cas d'étude 2 : avant et après `strip`

```bash
cp keygenme_O0 keygenme_O0_stripped  
strip keygenme_O0_stripped  
```

Ouvrez les deux versions dans ImHex et activez le diff.

### Ce que vous observez

**Le code est identique.** Les sections `.text`, `.rodata`, `.data` et `.plt` sont octet pour octet identiques entre le binaire original et le binaire strippé. C'est une confirmation importante : `strip` ne modifie pas le code exécutable, il ne touche qu'aux métadonnées.

**Les sections de symboles ont disparu.** Le binaire strippé est significativement plus court. Les sections `.symtab` (table des symboles), `.strtab` (table des chaînes de symboles) et les sections DWARF (si présentes) ont été supprimées. Dans le diff, ces régions apparaissent dans le fichier original mais n'ont pas de correspondance dans le fichier strippé.

**Le Section Header Table a changé.** Le nombre de Section Headers (`e_shnum`) est réduit dans le binaire strippé. Les entrées correspondant aux sections supprimées n'existent plus. L'offset de la table (`e_shoff`) a probablement changé aussi.

### Ce que vous en tirez

Ce diff confirme visuellement un fait théorique que nous avons vu au chapitre 2 : le stripping supprime les métadonnées sans toucher au code. Mais il révèle aussi un détail subtil — l'ordre des sections restantes et leurs offsets dans le fichier peuvent être modifiés par `strip`, même pour les sections dont le contenu n'a pas changé. C'est parce que `strip` réécrit le fichier ELF sans les sections supprimées, ce qui peut décaler les offsets des sections suivantes.

---

## Cas d'étude 3 : vérification d'un patch binaire

Vous avez inversé un saut conditionnel dans le binaire : remplacé un `jz` (opcode `74`) par un `jnz` (opcode `75`) à un offset précis. Avant de tester le binaire patché, vous voulez confirmer que la modification est correcte.

Ouvrez le binaire original et le binaire patché dans le diff ImHex. La navigation par différences devrait vous amener à **exactement un octet modifié**. Vérifiez :

- L'offset correspond bien à celui que vous visiez.  
- L'octet original est bien `74` et l'octet patché est bien `75`.  
- Aucune autre différence n'apparaît dans le fichier.

Si vous voyez des différences supplémentaires que vous n'attendiez pas, votre outil de patching a probablement touché d'autres octets par erreur (certains éditeurs modifient des timestamps ou des checksums lors de la sauvegarde). Le diff ImHex rend ce genre de problème immédiatement visible.

Ce scénario sera mis en pratique au chapitre 21 quand nous patcherons le keygenme.

---

## Diff hexadécimal vs diff structurel : positionnement

Pour clore cette section, clarifions la complémentarité entre le diff hexadécimal d'ImHex et les outils de diffing structurel que nous verrons au chapitre 10.

| Critère | Diff hexadécimal (ImHex) | Diff structurel (BinDiff, Diaphora) |  
|---|---|---|  
| Granularité | Octet par octet | Fonction par fonction |  
| Ce qu'il montre | Quels octets ont changé, où et combien | Quelles fonctions ont changé, ajoutées ou supprimées |  
| Rapidité | Immédiat, pas d'analyse préalable | Nécessite une analyse complète des deux binaires |  
| Contexte sémantique | Aucun — des octets bruts | Élevé — correspondance de fonctions, graphes de flux |  
| Cas d'usage principal | Patch localisé, impact de flags, vérification, données | Analyse de patch de sécurité, évolution de code |  
| Fichiers de tailles très différentes | Gère bien (montre les zones sans correspondance) | Peut mal apparier les fonctions si le delta est trop grand |

Les deux approches ne sont pas en compétition. Le diff hexadécimal est votre **premier regard** — rapide, exhaustif, sans hypothèse. Le diff structurel est votre **analyse approfondie** — lent mais riche en sémantique. Dans un workflow typique, vous commencez par un diff ImHex pour évaluer l'ampleur des changements et localiser les zones impactées, puis vous passez à BinDiff ou Diaphora pour comprendre le sens de ces changements au niveau du code.

---

## Limites de la vue Diff

La vue Diff d'ImHex est un outil de comparaison **linéaire** : elle compare les octets à un même offset dans les deux fichiers. Elle ne sait pas détecter les **insertions** et **suppressions** — si un bloc de données a été inséré au milieu du fichier, tout ce qui suit apparaît comme différent parce que les octets sont décalés, même si leur contenu est identique à un offset près.

Cette limitation est inhérente au diff hexadécimal brut. Pour gérer correctement les insertions et suppressions dans un binaire, les outils structurels (BinDiff, Diaphora) qui raisonnent sur les fonctions et les blocs de base plutôt que sur les offsets sont bien plus adaptés.

En pratique, cette limitation est rarement gênante pour les trois cas d'usage que nous avons décrits — patch localisé, impact de flags sur une même source, avant/après stripping — car dans ces scénarios, les différences sont soit ponctuelles soit en fin de fichier (sections ajoutées ou supprimées), et le diff linéaire fonctionne correctement.

---

## Résumé

La vue Diff d'ImHex compare deux fichiers côte à côte, octet par octet, avec surlignage des divergences et navigation entre les zones différentes. Elle excelle dans trois scénarios : vérifier un patch binaire localisé (un seul octet modifié doit apparaître), observer l'impact des flags de compilation sur la structure du binaire (quelles sections changent, lesquelles restent stables), et confirmer les effets du stripping (le code est intact, seules les métadonnées disparaissent). Le diff hexadécimal est complémentaire du diff structurel du chapitre 10 : le premier est immédiat et exhaustif au niveau octet, le second est plus lent mais riche en sémantique au niveau fonction. Pour les analyses de la partie V, les deux seront utilisés en tandem.

---


⏭️ [Recherche de magic bytes, chaînes encodées et séquences d'opcodes](/06-imhex/08-recherche-magic-bytes.md)
