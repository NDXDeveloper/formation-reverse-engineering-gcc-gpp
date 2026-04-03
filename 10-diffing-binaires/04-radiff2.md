🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 10.4 — `radiff2` — diffing en ligne de commande avec Radare2

> **Chapitre 10 — Diffing de binaires**  
> **Partie II — Analyse Statique**

---

## Présentation de `radiff2`

`radiff2` est l'outil de diffing intégré à la suite Radare2, que nous avons découverte au chapitre 9. Contrairement à BinDiff et Diaphora, qui reposent sur une interface graphique au sein d'un désassembleur, `radiff2` est un outil **purement en ligne de commande**. Pas de fenêtre, pas de CFG coloré, pas de clic : des commandes, des flags, et une sortie textuelle.

Cette austérité apparente est en réalité sa force. `radiff2` excelle dans trois situations où les outils graphiques sont moins à l'aise :

- **L'automatisation** — un `radiff2` dans un script Bash ou Python s'intègre naturellement dans un pipeline d'analyse. Comparer 50 paires de binaires en boucle ne demande qu'une boucle `for`.  
- **Le triage rapide** — quand on veut une réponse immédiate (« ces deux binaires sont-ils identiques ? », « combien de fonctions ont changé ? ») sans lancer un désassembleur complet et attendre l'auto-analyse.  
- **Les environnements sans GUI** — serveurs distants, conteneurs Docker, machines de CI/CD. Partout où un terminal suffit.

`radiff2` propose plusieurs niveaux de comparaison, du diff octet-par-octet le plus brut jusqu'au diff de fonctions avec appariement structurel. C'est cette gradation qui le rend polyvalent.

---

## Prérequis

`radiff2` est installé automatiquement avec Radare2. Si vous avez suivi le chapitre 4 (section 4.2) ou le chapitre 9, Radare2 est déjà en place. Vérifiez :

```bash
radiff2 -h
```

Si la commande affiche l'aide, tout est prêt. Sinon, installez Radare2 :

```bash
# Depuis les paquets (version stable)
sudo apt install radare2

# Ou depuis les sources (version la plus récente)
git clone https://github.com/radareorg/radare2.git  
cd radare2  
sys/install.sh  
```

---

## Niveau 1 — Diff octet par octet

Le mode le plus basique de `radiff2` compare les deux fichiers octet par octet, sans aucune connaissance du format binaire. C'est l'équivalent d'un `cmp` amélioré, avec affichage des différences en hexadécimal.

```bash
radiff2 keygenme_v1 keygenme_v2
```

La sortie ressemble à ceci :

```
0x00001234 48 => 49
0x00001235 83fb05 => 83fb06
0x00002010 7406 => 750e
```

Chaque ligne indique une adresse (offset dans le fichier) suivie des octets dans la version 1 et leur remplacement dans la version 2. Ce mode est utile pour détecter rapidement si deux fichiers sont identiques ou non, et pour localiser les différences brutes. Mais il ne fournit aucune interprétation : un `74` qui devient `75` n'est pas identifié comme un `jz` devenu `jnz` — c'est juste un changement d'octet.

### Options utiles en mode octet

```bash
# Affichage en colonnes côte à côte
radiff2 -d keygenme_v1 keygenme_v2

# Compter simplement le nombre de différences
radiff2 -s keygenme_v1 keygenme_v2
```

Le flag `-s` (*distance*) calcule la distance entre les deux fichiers. C'est une métrique rapide pour quantifier l'ampleur des changements : une distance faible sur un gros binaire indique un patch ciblé.

```bash
# Résultat typique
$ radiff2 -s keygenme_v1 keygenme_v2
similarity: 0.987  
distance: 42  
```

Le champ `similarity` est un ratio entre 0.0 (fichiers totalement différents) et 1.0 (fichiers identiques). Le champ `distance` compte le nombre d'octets qui diffèrent. Pour un premier triage, ces deux valeurs suffisent souvent à orienter l'analyse.

---

## Niveau 2 — Diff avec désassemblage

Le mode octet ne comprend pas la structure du binaire. Pour un diff qui parle en assembleur plutôt qu'en hexadécimal, on passe au mode code avec le flag `-c` :

```bash
radiff2 -c keygenme_v1 keygenme_v2
```

Dans ce mode, `radiff2` désassemble les zones qui diffèrent et affiche les instructions côte à côte. Au lieu de voir `7406 => 750e`, on obtient quelque chose comme :

```
  0x00001234   jz  0x123c    |   0x00001234   jnz 0x1244
```

C'est immédiatement plus lisible. On voit qu'un saut conditionnel a été inversé — une modification classique dans les crackmes et les patches de vérification. Ce mode reste limité aux zones de code qui diffèrent au niveau binaire ; il ne fait pas d'appariement de fonctions.

### Syntaxe Intel

Par défaut, `radiff2` utilise la syntaxe de Radare2 (proche d'Intel mais avec quelques particularités). Pour forcer une syntaxe Intel standard, ajoutez l'option appropriée via la variable d'environnement :

```bash
R2_ARCH=x86 R2_BITS=64 radiff2 -c keygenme_v1 keygenme_v2
```

Ou configurez-le globalement dans votre `~/.radare2rc` (cf. chapitre 9, section 9.3).

---

## Niveau 3 — Diff de fonctions avec analyse

C'est le mode le plus puissant de `radiff2`, et celui qui se rapproche le plus de ce que font BinDiff et Diaphora. Le flag `-A` demande à `radiff2` d'effectuer une analyse complète des deux binaires (identification des fonctions, construction des CFG) avant de comparer :

```bash
radiff2 -A keygenme_v1 keygenme_v2
```

> ⚠️ **Temps d'exécution** — Le flag `-A` déclenche l'équivalent de la commande `aaa` de Radare2 sur chaque binaire (analyse automatique complète, vue au chapitre 9). Sur un petit binaire, c'est quasi instantané. Sur un gros binaire, cela peut prendre un moment. Pour une analyse encore plus approfondie, `-AA` existe mais est rarement nécessaire.

La sortie liste les fonctions appariées avec leur score de similarité :

```
  sym.main   0x00001149 |   sym.main   0x00001149   (MATCH 0.95)
  sym.check  0x000011a0 |   sym.check  0x000011a0   (MATCH 0.72)
  sym.usage  0x00001230 |   sym.usage  0x00001230   (MATCH 1.00)
```

Les fonctions avec un score de 1.00 sont identiques. Celles avec un score inférieur ont été modifiées. Les fonctions non appariées sont listées séparément.

### Combiner avec le diff de code

Pour avoir à la fois l'appariement de fonctions et le détail des instructions modifiées, combinez les flags :

```bash
radiff2 -AC keygenme_v1 keygenme_v2
```

Ce mode affiche d'abord la liste des fonctions appariées avec leurs scores, puis, pour chaque fonction modifiée, le diff instruction par instruction. C'est le mode le plus informatif en une seule commande.

---

## Modes de comparaison spécialisés

`radiff2` propose plusieurs modes supplémentaires, activés par des flags, qui couvrent des besoins spécifiques.

### Diff par graphe (`-g`)

Le flag `-g` compare les graphes de flot de contrôle de deux fonctions spécifiques. Vous devez fournir l'adresse de la fonction à comparer dans chaque binaire :

```bash
radiff2 -g sym.check keygenme_v1 keygenme_v2
```

La sortie est une description textuelle du graphe : nombre de nœuds (blocs de base), nombre d'arêtes, et les différences structurelles. C'est moins visuel que les CFG colorés de BinDiff, mais c'est parsable par un script.

Pour une visualisation graphique, vous pouvez générer un diff au format DOT et le convertir en image avec Graphviz :

```bash
radiff2 -g sym.check keygenme_v1 keygenme_v2 > diff.dot  
dot -Tpng diff.dot -o diff.png  
```

Le résultat est un graphe où les blocs communs, modifiés et ajoutés/supprimés sont représentés avec des couleurs différentes. Ce n'est pas aussi ergonomique que l'interface de BinDiff, mais c'est une visualisation que l'on peut générer automatiquement dans un script et intégrer dans un rapport.

### Diff binaire brut (`-x`)

Le flag `-x` affiche un diff hexadécimal en colonnes, avec les deux fichiers côte à côte. C'est utile pour inspecter des différences dans des zones non-code (headers, données, tables de relocation) :

```bash
radiff2 -x keygenme_v1 keygenme_v2
```

### Comparaison de sections spécifiques

Il est parfois utile de ne comparer qu'une portion des binaires — par exemple, uniquement la section `.text` (le code) en ignorant les headers et les données. Combiné avec `rabin2` (l'outil d'inspection de binaires de la suite Radare2), on peut extraire les offsets des sections et passer des plages d'adresses à `radiff2` :

```bash
# Trouver l'offset et la taille de .text dans chaque binaire
rabin2 -S keygenme_v1 | grep .text  
rabin2 -S keygenme_v2 | grep .text  

# Comparer uniquement la section .text (exemple avec offsets fictifs)
radiff2 -r 0x1000:0x3000 keygenme_v1 keygenme_v2
```

Cette approche réduit le bruit dû aux différences dans les métadonnées, les tables de symboles ou les sections de données qui ne sont pas pertinentes pour l'analyse du code.

---

## Intégration dans des scripts

La véritable puissance de `radiff2` se révèle dans l'automatisation. Voici quelques patterns d'utilisation courants.

### Triage d'un répertoire de binaires

Comparer chaque binaire d'un répertoire à une version de référence et produire un résumé :

```bash
#!/bin/bash
REFERENCE="keygenme_v1"

for binary in builds/*; do
    sim=$(radiff2 -s "$REFERENCE" "$binary" 2>/dev/null | grep similarity | awk '{print $2}')
    echo "$binary : similarity = $sim"
done
```

Ce script produit une liste de scores de similarité. Les binaires avec un score inférieur à 1.0 méritent une investigation plus poussée.

### Extraction des fonctions modifiées en JSON

Pour un traitement programmatique, la sortie de `radiff2` peut être parsée. Combinée avec les capacités JSON de Radare2 (`r2 -qc '...' -j`), on peut construire des rapports structurés :

```bash
#!/bin/bash
# Liste les fonctions modifiées entre deux versions
radiff2 -A keygenme_v1 keygenme_v2 2>/dev/null \
    | grep -v "MATCH 1.00" \
    | grep "MATCH"
```

Pour un traitement plus fin, passez par `r2pipe` en Python (vu au chapitre 9, section 9.4), qui donne un accès programmatique complet à toutes les fonctionnalités de Radare2, y compris `radiff2` :

```python
import r2pipe

r1 = r2pipe.open("keygenme_v1")  
r1.cmd("aaa")  # analyse complète  
funcs_v1 = r1.cmdj("aflj")  # liste des fonctions en JSON  

r2 = r2pipe.open("keygenme_v2")  
r2.cmd("aaa")  
funcs_v2 = r2.cmdj("aflj")  

# Comparaison programmatique des fonctions
# (noms, tailles, nombre de blocs...)
```

### Intégration CI/CD

Dans un pipeline d'intégration continue, `radiff2` peut servir de garde-fou : à chaque build, on compare le binaire produit à la version précédente et on alerte si des fonctions critiques ont été modifiées de manière inattendue.

```bash
# Dans un script CI
SIMILARITY=$(radiff2 -s build/app_current build/app_previous \
    | grep similarity | awk '{print $2}')

if (( $(echo "$SIMILARITY < 0.95" | bc -l) )); then
    echo "WARNING: significant binary changes detected (similarity: $SIMILARITY)"
    radiff2 -AC build/app_current build/app_previous > diff_report.txt
    # Envoyer le rapport par mail ou Slack...
fi
```

---

## `radiff2` vs BinDiff / Diaphora

Il est important de positionner `radiff2` correctement par rapport aux outils vus dans les sections précédentes. Ce ne sont pas des concurrents directs — ils opèrent à des niveaux différents.

### Ce que `radiff2` fait mieux

- **Vitesse de triage** — lancer `radiff2 -s` prend une fraction de seconde, sans aucune analyse préalable. Pour répondre à « ces deux fichiers sont-ils différents et de combien ? », rien n'est plus rapide.  
- **Scriptabilité** — tout passe par la ligne de commande, tout produit du texte parsable. L'intégration dans des scripts, des pipelines et des outils automatisés est triviale.  
- **Légèreté** — pas besoin de lancer un désassembleur graphique, pas de projet à créer, pas d'export intermédiaire. Un terminal et une commande suffisent.  
- **Granularité** — `radiff2` permet de comparer à tous les niveaux, de l'octet brut jusqu'à la fonction, en passant par l'instruction et le graphe. On choisit le niveau de détail adapté au besoin.

### Ce que BinDiff / Diaphora font mieux

- **Qualité de l'appariement** — sur des binaires complexes avec des milliers de fonctions, les algorithmes multi-passes de BinDiff et les heuristiques de pseudo-code de Diaphora produisent des appariements plus fiables que ceux de `radiff2`.  
- **Visualisation** — les CFG colorés côte à côte, le diff de pseudo-code de Diaphora, la navigation intégrée dans Ghidra — tout cela facilite considérablement la compréhension des changements. La sortie textuelle de `radiff2` est fonctionnelle mais demande plus d'effort cognitif.  
- **Contexte** — dans Ghidra ou IDA, on peut immédiatement passer du diff à l'analyse complète d'une fonction (XREF, décompilation, types). Avec `radiff2`, il faut basculer manuellement vers `r2` pour approfondir.

### En pratique

Un workflow efficace utilise `radiff2` comme premier filtre — un triage en quelques secondes pour quantifier les changements et identifier les zones d'intérêt — puis bascule vers BinDiff ou Diaphora pour l'analyse détaillée des fonctions modifiées. Les deux approches sont complémentaires : le terminal pour la vitesse et l'automatisation, l'interface graphique pour la compréhension en profondeur.

---

## Récapitulatif des flags essentiels

| Flag | Mode | Usage |  
|------|------|-------|  
| *(aucun)* | Diff octet par octet | Différences brutes en hexadécimal |  
| `-s` | Similarité | Score de similarité et distance — triage rapide |  
| `-d` | Diff colonnes | Affichage côte à côte en hexadécimal |  
| `-x` | Hex dump | Dump hexadécimal comparatif |  
| `-c` | Diff code | Désassemblage des zones modifiées |  
| `-A` | Analyse + fonctions | Appariement de fonctions avec scores |  
| `-AC` | Analyse + code | Appariement de fonctions + diff instructions |  
| `-g addr` | Diff graphe | Comparaison de CFG d'une fonction spécifique |

---

## En résumé

`radiff2` n'a pas la puissance d'analyse de BinDiff ni la richesse du diff de pseudo-code de Diaphora, et il ne prétend pas les remplacer. Son rôle est différent : c'est l'outil qu'on lance en premier, en quelques secondes, pour quantifier et localiser les changements avant de sortir l'artillerie lourde. Sa nature purement CLI en fait un compagnon indispensable pour l'automatisation, le scripting et les environnements sans interface graphique.

Le réflexe à développer : avant de lancer Ghidra pour un diff, commencez par un `radiff2 -s` pour savoir à quoi vous attendre, puis un `radiff2 -AC` pour avoir un premier aperçu des fonctions touchées. Vous saurez exactement où concentrer votre analyse approfondie.

---


⏭️ [Cas pratique : identifier une correction de vulnérabilité entre deux versions d'un binaire](/10-diffing-binaires/05-cas-pratique-patch-vuln.md)
