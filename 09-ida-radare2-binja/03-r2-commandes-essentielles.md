🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 9.3 — `r2` : commandes essentielles (`aaa`, `pdf`, `afl`, `iz`, `iS`, `VV`)

> 📘 **Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja**  
> Section précédente : [9.2 — Radare2 / Cutter — analyse en ligne de commande et GUI](/09-ida-radare2-binja/02-radare2-cutter.md)

---

La section 9.2 a présenté l'architecture de Radare2 et la logique mnémonique de ses commandes. Cette section entre dans la pratique : nous allons parcourir, commande par commande, les gestes essentiels pour mener une analyse statique complète dans `r2`. Chaque commande est illustrée sur notre binaire fil rouge `keygenme_O2_strip`.

> 💡 **Convention.** Dans les exemples ci-dessous, le prompt `[0x...]>` représente le shell interactif de `r2`. Les lignes sans prompt sont la sortie produite par la commande. Les commentaires explicatifs sont précédés de `#`.

## Ouvrir un binaire et lancer l'analyse

### Ouverture simple

```
$ r2 keygenme_O2_strip
[0x00401050]>
```

Le binaire est chargé en mémoire, le seek est positionné sur le point d'entrée. Aucune analyse n'est lancée : `r2` attend vos instructions.

### Ouverture avec analyse automatique

```
$ r2 -A keygenme_O2_strip
[x] Analyze all flags starting with sym. and entry0 (aa)
[x] Analyze function calls (aac)
[x] Analyze len bytes of instructions for references (aar)
[x] ...
[0x00401050]>
```

Le flag `-A` exécute `aaa` automatiquement au chargement. C'est l'option la plus courante pour commencer une session d'analyse.

### Ouverture en écriture (pour le patching)

```
$ r2 -w keygenme_O2_strip
```

Le flag `-w` ouvre le binaire en mode écriture. Les commandes `w*` (write) modifieront directement le fichier sur le disque. À manipuler avec précaution — travaillez toujours sur une copie.

### Exécution non interactive (one-liner)

```
$ r2 -qc 'aaa; afl' keygenme_O2_strip
```

Le flag `-q` (*quiet*) supprime la bannière, et `-c` exécute la chaîne de commandes puis quitte. C'est le mode idéal pour le scripting shell : vous pouvez enchaîner des commandes `r2` dans un pipeline Unix comme n'importe quel outil CLI.

## Commandes d'analyse (`a`)

La famille `a` regroupe tout ce qui concerne l'analyse statique du binaire. C'est la première chose à exécuter après le chargement.

### `aa` — analyse de base

```
[0x00401050]> aa
```

Effectue une première passe d'analyse : détection des fonctions à partir des points d'entrée connus, résolution des appels directs, identification des blocs de base. C'est rapide mais peut manquer des fonctions qui ne sont pas atteignables par un chemin d'appel direct depuis `_start`.

### `aaa` — analyse approfondie

```
[0x00401050]> aaa
```

Enchaîne plusieurs passes d'analyse plus agressives que `aa`. En plus de l'analyse de base, `aaa` effectue la résolution des appels indirects, l'analyse de la pile (variables locales et arguments), l'auto-nommage des fonctions basé sur les conventions de la glibc, et la propagation des types. C'est le niveau d'analyse recommandé pour la plupart des cas d'usage.

### `aaaa` — analyse expérimentale

```
[0x00401050]> aaaa
```

Ajoute des heuristiques supplémentaires plus coûteuses en temps : tentative de récupération de fonctions « orphelines » (code mort, fonctions non appelées), analyse d'émulation ESIL pour résoudre des valeurs dynamiques, et d'autres passes exploratoires. Utile sur les binaires strippés ou obfusqués, mais peut être lent sur les gros binaires et peut aussi produire des faux positifs.

### `af` — analyser une fonction spécifique

```
[0x00401160]> af
```

Analyse uniquement la fonction à l'adresse du seek courant. Utile si vous préférez une analyse ciblée plutôt que globale, ou si vous voulez forcer `r2` à reconnaître une fonction qu'il n'a pas détectée automatiquement.

### `afr` — analyser récursivement depuis une fonction

```
[0x00401160]> afr
```

Analyse la fonction au seek courant et toutes les fonctions qu'elle appelle, récursivement. Un bon compromis entre `af` (une seule fonction) et `aaa` (tout le binaire).

## Commandes d'information (`i`)

La famille `i` (*info*) affiche des métadonnées sur le binaire. Ces commandes ne nécessitent pas d'analyse préalable — elles lisent directement les headers et tables du format ELF.

### `iI` — informations générales

```
[0x00401050]> iI
arch     x86  
baddr    0x400000  
binsz    14328  
bintype  elf  
bits     64  
canary   false  
class    ELF64  
endian   little  
machine  AMD x86-64 architecture  
nx       true  
os       linux  
pic      false  
relro    partial  
stripped true  
```

Vue d'ensemble en un coup d'œil : architecture, taille, protections de sécurité (canary, NX, PIE, RELRO), et confirmation que le binaire est strippé. C'est l'équivalent de `file` + `checksec` combinés.

### `iS` — sections

```
[0x00401050]> iS
[Sections]

nth paddr        size vaddr       vsize perm type     name
―――――――――――――――――――――――――――――――――――――――――――――――――――――――――――
0   0x00000000    0x0 0x00000000    0x0 ---- NULL
1   0x00000318   0x1c 0x00400318   0x1c -r-- NOTE     .note.gnu.build-id
2   0x00000338   0x24 0x00400338   0x24 -r-- NOTE     .note.ABI-tag
3   0x00000360   0x28 0x00400360   0x28 -r-- GNU_HASH .gnu.hash
...
```

Liste toutes les sections ELF avec leur adresse physique dans le fichier (`paddr`), leur adresse virtuelle en mémoire (`vaddr`), leur taille, leurs permissions, et leur nom. C'est le même résultat que `readelf -S`, présenté dans le format tabulaire de `r2`.

Les sections les plus pertinentes pour le RE sont `.text` (code exécutable), `.rodata` (données en lecture seule, notamment les chaînes), `.data` et `.bss` (données modifiables), `.plt` et `.got` (résolution dynamique des imports).

### `iS~.text` — filtrer une section

```
[0x00401050]> iS~.text
7   0x00001050  0x1a2 0x00401050  0x1a2 -r-x PROGBITS .text
```

L'opérateur `~` est le **grep interne** de `r2`. Il filtre la sortie de n'importe quelle commande par une expression textuelle. `iS~.text` affiche uniquement la ligne de la section `.text`. Ce mécanisme de filtrage est omniprésent dans les workflows `r2` et permet d'extraire rapidement l'information pertinente d'une sortie verbeuse.

Quelques variantes utiles du grep interne :

- `~mot` — filtre les lignes contenant « mot »  
- `~!mot` — filtre les lignes ne contenant PAS « mot »  
- `~mot[2]` — extrait la 3ᵉ colonne (index 0) des lignes contenant « mot »  
- `~..` — affiche la sortie dans un pager interactif (moins de défilement perdu)

### `ii` — imports

```
[0x00401050]> ii
[Imports]
nth vaddr      bind   type   lib name
―――――――――――――――――――――――――――――――――――――――
1   0x00401030 GLOBAL FUNC       puts
2   0x00401040 GLOBAL FUNC       strcmp
3   0x00000000 WEAK   NOTYPE     __gmon_start__
```

Liste les fonctions importées depuis les bibliothèques partagées. Sur un binaire dynamiquement lié, cette liste révèle les appels système de haut niveau que le programme utilise. Voir `strcmp` dans les imports d'un crackme est un indice immédiat que la comparaison de l'entrée utilisateur est probablement un simple `strcmp`.

### `ie` — point d'entrée

```
[0x00401050]> ie
[Entrypoints]
vaddr=0x00401050 paddr=0x00001050 haddr=0x00000018 type=program
```

Affiche le point d'entrée du binaire (champ `e_entry` du header ELF). C'est l'adresse de `_start`, pas de `main` — la distinction est importante comme expliqué au chapitre 2.7.

### `iz` — chaînes dans les sections de données

```
[0x00401050]> iz
[Strings]
nth paddr      vaddr      len  size section type  string
――――――――――――――――――――――――――――――――――――――――――――――――――――――――――
0   0x00002000 0x00402000 13   14   .rodata ascii Enter key:
1   0x0000200e 0x0040200e 14   15   .rodata ascii Access granted
2   0x0000201d 0x0040201d 9    10   .rodata ascii Wrong key
```

Extrait les chaînes de caractères des sections de données (`.rodata`, `.data`). Chaque chaîne est affichée avec son adresse physique, son adresse virtuelle, sa taille, la section d'appartenance et son encodage. C'est l'équivalent enrichi de la commande `strings`.

### `izz` — chaînes dans tout le binaire

```
[0x00401050]> izz
```

Variante plus agressive qui cherche les chaînes dans **tout** le fichier, y compris dans les headers, les sections de code, et les zones non mappées. Produit davantage de résultats (dont du bruit), mais peut révéler des chaînes cachées dans des sections atypiques.

### `iz~granted` — chercher une chaîne spécifique

```
[0x00401050]> iz~granted
1   0x0000200e 0x0040200e 14   15   .rodata ascii Access granted
```

Le grep interne combiné avec `iz` permet de localiser instantanément une chaîne d'intérêt.

## Commandes de déplacement (`s`)

### `s addr` — déplacer le seek

```
[0x00401050]> s 0x00401160
[0x00401160]>
```

Déplace le curseur à l'adresse spécifiée. Accepte des adresses numériques, des noms de fonctions (`s main`, `s sym.check_serial`), des flags (`s entry0`), ou des expressions (`s $$+0x10` pour avancer de 16 octets depuis la position courante).

### `s-` et `s+` — historique de navigation

```
[0x00401160]> s-
[0x00401050]>
[0x00401050]> s+
[0x00401160]>
```

Naviguent dans l'historique des positions précédentes et suivantes, comme les boutons « précédent » et « suivant » d'un navigateur web. Indispensable quand on suit des chaînes de `call` et qu'on veut revenir au point de départ.

### `sr` — seek vers un registre (mode debug)

```
[0x00401160]> sr rip
```

En mode débogage, déplace le seek à la valeur d'un registre. Utile pour synchroniser la vue désassemblage avec le compteur d'instructions courant.

## Commandes d'affichage (`p`)

La famille `p` (*print*) est le couteau suisse de l'affichage. Elle permet de visualiser le contenu du binaire sous toutes les formes possibles à partir du seek courant.

### `pdf` — désassembler la fonction courante

```
[0x00401160]> pdf
            ; DATA XREF from entry0 @ 0x40106d(r)
┌ 78: int main (int argc, char **argv, char **envp);
│           0x00401160      4883ec18       sub rsp, 0x18
│           0x00401164      488d3e95e0..   lea rdi, str.Enter_key:
│           0x0040116b      e8c0feffff     call sym.imp.puts
│           0x00401170      488d7424..     lea rsi, [rsp + 4]
│           0x00401175      488d3d84..     lea rdi, str._25s
│           0x0040117c      b800000000     mov eax, 0
│           0x00401181      e8cafeffff     call sym.imp.__isoc99_scanf
│           ...
│       ┌─< 0x0040119a      7512           jne 0x4011ae
│       │   0x0040119c      488d3d6b..     lea rdi, str.Access_granted
│       │   0x004011a3      e888feffff     call sym.imp.puts
│      ┌──< 0x004011a8      eb0e           jmp 0x4011b8
│      │└─> 0x004011ae      488d3d68..     lea rdi, str.Wrong_key
│      │    0x004011b5      e876feffff     call sym.imp.puts
│      └──> 0x004011b8      b800000000     mov eax, 0
│           0x004011bd      4883c418       add rsp, 0x18
└           0x004011c1      c3             ret
```

C'est la commande la plus utilisée de `r2`. `pdf` signifie **p**rint **d**isassembly **f**unction. Elle affiche le désassemblage complet de la fonction dans laquelle se trouve le seek, avec :

- Les adresses virtuelles dans la colonne de gauche.  
- Les octets bruts de chaque instruction.  
- Les mnémoniques et opérandes en syntaxe Intel (par défaut dans les versions récentes).  
- Les annotations automatiques : noms de chaînes (`str.Enter_key:`), noms de fonctions importées (`sym.imp.puts`), références croisées (commentaires `; DATA XREF`).  
- Les flèches ASCII (`┌─<`, `└─>`) qui tracent visuellement les sauts conditionnels et inconditionnels. C'est un mode graphe simplifié directement dans le listing texte.

### `pd N` — désassembler N instructions

```
[0x00401160]> pd 5
            0x00401160      4883ec18       sub rsp, 0x18
            0x00401164      488d3e95e0..   lea rdi, str.Enter_key:
            0x0040116b      e8c0feffff     call sym.imp.puts
            0x00401170      488d7424..     lea rsi, [rsp + 4]
            0x00401175      488d3d84..     lea rdi, str._25s
```

Désassemble exactement N instructions à partir du seek courant, indépendamment des bornes de fonction. Utile quand le seek n'est pas dans une fonction reconnue, ou quand vous voulez examiner un fragment précis.

### `pds` — résumé de la fonction (appels et chaînes)

```
[0x00401160]> pds
0x0040116b call sym.imp.puts           ; "Enter key: "
0x00401181 call sym.imp.__isoc99_scanf
0x00401193 call sym.imp.strcmp
0x004011a3 call sym.imp.puts           ; "Access granted"
0x004011b5 call sym.imp.puts           ; "Wrong key"
```

Affiche un résumé ultra-condensé de la fonction : uniquement les appels de fonctions et les chaînes référencées, sans le code intermédiaire. C'est une commande de triage remarquablement efficace. En trois secondes, vous savez que cette fonction lit une entrée (`scanf`), la compare (`strcmp`), et affiche un résultat conditionnel. Vous avez l'essentiel de la logique du crackme.

### `pdc` — pseudo-code (décompilation simplifiée)

```
[0x00401160]> pdc
```

Produit une décompilation rudimentaire en pseudo-code C. La qualité est inférieure au décompileur Ghidra intégré via `pdg` (voir ci-dessous), mais `pdc` est toujours disponible sans plugin externe. Utile pour un premier aperçu rapide.

### `pdg` — décompileur Ghidra (si installé)

```
[0x00401160]> pdg
```

Si le plugin `r2ghidra` est installé (via `r2pm -i r2ghidra`), cette commande invoque le décompileur Ghidra sur la fonction courante et affiche le pseudo-code résultant. La qualité est comparable à celle obtenue dans Ghidra ou Cutter. C'est un atout majeur de `r2` : le décompileur le plus puissant du monde open source, accessible depuis la ligne de commande.

### `px N` — hex dump

```
[0x00402000]> px 32
- offset -   0 1  2 3  4 5  6 7  8 9  A B  C D  E F  0123456789ABCDEF
0x00402000  456e 7465 7220 6b65 793a 2000 4163 6365  Enter key: .Acce
0x00402010  7373 2067 7261 6e74 6564 0057 726f 6e67  ss granted.Wrong
```

Affiche N octets en hexadécimal avec la vue ASCII correspondante, à partir du seek courant. C'est la vue classique d'un éditeur hexadécimal.

### `ps` — afficher comme chaîne

```
[0x00402000]> ps
Enter key:
```

Interprète les octets au seek courant comme une chaîne de caractères et l'affiche. Variantes : `psz` pour les chaînes null-terminated, `psw` pour les chaînes wide (UTF-16).

## Commandes sur les fonctions (`af`)

### `afl` — lister toutes les fonctions

```
[0x00401050]> afl
0x00401050    1     46 entry0
0x00401080    4     31 sym.deregister_tm_clones
0x004010b0    4     49 sym.register_tm_clones
0x004010f0    3     28 sym.__do_global_dtors_aux
0x00401110    1      6 sym.frame_dummy
0x00401120    3     63 sym.transform_key
0x00401160    4     98 main
0x004011d0    1      5 sym.__libc_csu_fini
0x004011e0    4    101 sym.__libc_csu_init
```

Liste toutes les fonctions détectées par l'analyse avec leur adresse, le nombre de blocs de base, la taille en octets, et le nom. C'est l'équivalent de la fenêtre « Functions » d'IDA. On retrouve les fonctions d'infrastructure GCC (`deregister_tm_clones`, `register_tm_clones`, `frame_dummy`, `__libc_csu_init`, `__libc_csu_fini`) et les fonctions applicatives (`main`, `transform_key`).

### `afl~transform` — filtrer les fonctions

```
[0x00401050]> afl~transform
0x00401120    3     63 sym.transform_key
```

Combiné avec le grep interne, `afl` permet de localiser rapidement une fonction par fragment de nom.

### `aflj` — sortie JSON

```
[0x00401050]> aflj
```

Le suffixe `j` produit la sortie au format JSON. C'est essentiel pour le scripting : la sortie JSON est parsable proprement depuis Python, contrairement à la sortie texte tabulaire. La quasi-totalité des commandes `r2` supporte le suffixe `j`.

### `afn nouveau_nom` — renommer la fonction courante

```
[0x00401120]> afn check_serial
[0x00401120]> afl~check
0x00401120    3     63 check_serial
```

Renomme la fonction au seek courant. Le nouveau nom se propage dans tout le désassemblage. Équivalent de la touche `N` dans IDA.

### `afvn ancien nouveau` — renommer une variable locale

```
[0x00401160]> afvn var_ch user_input
```

Renomme une variable locale de la fonction courante. Les variables locales sont identifiées par l'analyse de la pile (`afv` pour les lister).

### `afv` — lister les variables locales

```
[0x00401160]> afv
var char *user_input @ rsp+0x4
```

Affiche les variables locales et arguments détectés pour la fonction courante, avec leur type estimé et leur emplacement (offset par rapport au stack pointer ou au base pointer).

## Commandes de recherche (`/`)

### `/s chaîne` — chercher une chaîne

```
[0x00401050]> / Access
Searching 6 bytes in [0x00400000-0x00402040]  
hits: 1  
0x0040200e hit0_0 "Access granted"
```

Recherche une séquence d'octets interprétée comme une chaîne ASCII dans les segments mappés du binaire. Affiche les adresses des correspondances.

### `/x DEADBEEF` — chercher une séquence hexadécimale

```
[0x00401050]> /x 7512
```

Recherche une séquence d'octets exacte. Utile pour trouver des patterns d'opcodes spécifiques — par exemple, `7512` correspond à `jne +0x12`, le saut conditionnel qui décide entre les deux chemins dans un crackme.

### `/R` — chercher des gadgets ROP

```
[0x00401050]> /R ret
```

Recherche des séquences d'instructions se terminant par `ret` — les gadgets ROP utilisés dans les techniques d'exploitation. Cette fonctionnalité, native dans `r2`, est couverte plus en détail au chapitre 12.3 sur les extensions GDB.

## Références croisées (`ax`)

### `axt addr` — qui référence cette adresse ?

```
[0x0040200e]> axt
main 0x40119c [DATA:r--] lea rdi, str.Access_granted
```

Affiche toutes les cross-references **vers** l'adresse courante (*to*). Dans cet exemple, la chaîne « Access granted » est référencée par un `lea` dans `main` à l'adresse `0x40119c`. C'est l'équivalent de la touche `X` dans IDA.

### `axf addr` — que référence cette adresse ?

```
[0x0040116b]> axf
sym.imp.puts 0x401030 [CODE:--x] call sym.imp.puts
```

Affiche les cross-references **depuis** l'adresse courante (*from*). Ici, l'instruction à `0x40116b` est un `call` vers `puts`.

### Combinaison XREF + chaînes : le workflow de base

La séquence de commandes suivante illustre le workflow classique de triage d'un crackme :

```
[0x00401050]> iz~granted           # 1. Trouver la chaîne de succès
1   0x0040200e 0x0040200e 14   15   .rodata ascii Access granted

[0x00401050]> s 0x0040200e         # 2. Se déplacer sur cette chaîne

[0x0040200e]> axt                  # 3. Qui utilise cette chaîne ?
main 0x40119c [DATA:r--] lea rdi, str.Access_granted

[0x0040200e]> s main               # 4. Aller dans main

[0x00401160]> pdf                  # 5. Désassembler la fonction
```

En cinq commandes, vous avez localisé la routine de vérification. C'est la puissance du workflow CLI : chaque commande produit une information qui nourrit la suivante.

## Modes visuels

Les modes visuels ont été introduits en section 9.2. Voici les détails pratiques de chacun.

### `V` — mode visuel

```
[0x00401160]> V
```

Bascule dans un mode plein écran avec le désassemblage centré sur le seek courant. Les raccourcis clavier en mode visuel :

| Touche | Action |  
|---|---|  
| `p` / `P` | Cycle entre les vues (hex, désassemblage, debug, etc.) |  
| `j` / `k` | Descendre / monter d'une instruction |  
| `J` / `K` | Descendre / monter d'une page |  
| `Entrée` | Suivre un call ou un jump |  
| `u` | Revenir en arrière |  
| `x` / `X` | Afficher les XREF vers / depuis l'instruction courante |  
| `d` | Menu de définition (changer le type : code, donnée, chaîne, etc.) |  
| `:` | Ouvrir le prompt de commande (exécuter une commande `r2` ponctuelle) |  
| `n` / `N` | Renommer le symbole sous le curseur / Aller au prochain flag |  
| `;` | Ajouter un commentaire |  
| `q` | Quitter le mode visuel |

Le mode visuel est un bon compromis entre la rapidité du CLI et le confort de navigation d'une interface graphique. Il est particulièrement utile en SSH, où Cutter n'est pas disponible.

### `VV` — mode graphe

```
[0x00401160]> VV
```

Affiche le graphe de flux de contrôle (CFG) de la fonction courante en ASCII art. Chaque bloc de base est un rectangle de texte contenant les instructions, relié aux blocs successeurs par des lignes.

Raccourcis spécifiques au mode graphe :

| Touche | Action |  
|---|---|  
| `tab` | Passer au bloc suivant |  
| `Tab` (Shift+Tab) | Passer au bloc précédent |  
| `t` / `f` | Suivre le branchement *true* / *false* |  
| `g` + lettre | Sauter au bloc étiqueté (les étiquettes apparaissent quand vous appuyez sur `g`) |  
| `R` | Changer les couleurs |  
| `+` / `-` | Zoom avant / arrière (ajuste le nombre de colonnes) |  
| `hjkl` ou flèches | Déplacer la vue |  
| `p` | Alterner entre vue graphe et mini-graphe (overview) |

### `V!` — mode panneaux

```
[0x00401160]> V!
```

Le mode panneaux divise le terminal en plusieurs fenêtres configurables. Par défaut, vous obtenez le désassemblage, les registres et la pile côte à côte. Vous pouvez ajouter, supprimer et redimensionner les panneaux avec les touches `Tab` (changer de panneau actif), `w` (menu de gestion des panneaux), et `e` (dans certaines versions).

Ce mode se rapproche de l'expérience offerte par les extensions GDB comme GEF ou pwndbg (chapitre 12), mais en analyse statique.

## Flags et commentaires

### Flags

Les flags dans `r2` sont des étiquettes nommées associées à des adresses. Les noms de fonctions (`main`, `sym.imp.puts`), les chaînes (`str.Enter_key:`), les sections (`section..text`) sont tous des flags. Vous pouvez créer vos propres flags :

```
[0x0040119a]> f cmp_branch @ 0x0040119a
[0x0040119a]> f~cmp
0x0040119a 1 cmp_branch
```

La commande `f nom @ adresse` crée un flag. `f` sans argument liste tous les flags, filtrable avec `~`.

### Commentaires

```
[0x0040119a]> CC Branchement succès/échec selon strcmp
[0x0040119a]> pdf~CC
│           ; Branchement succès/échec selon strcmp
│       ┌─< 0x0040119a      7512           jne 0x4011ae
```

`CC texte` ajoute un commentaire à l'adresse du seek courant. Le commentaire apparaît dans le désassemblage (`pdf`) et dans les modes visuels.

## Sortie JSON et composition

L'un des principes de design de `r2` est que chaque commande doit pouvoir produire une sortie exploitable par un programme. Le suffixe `j` active la sortie JSON sur la grande majorité des commandes :

```
[0x00401050]> aflj    # Liste des fonctions en JSON
[0x00401050]> iIj     # Informations binaire en JSON
[0x00401050]> izj     # Chaînes en JSON
[0x00401050]> axtj    # Cross-references en JSON
```

Combiné avec le mode non-interactif (`r2 -qc '...'`), cela permet de construire des pipelines d'analyse :

```bash
# Extraire les noms de toutes les fonctions en JSON, traiter avec jq
r2 -qc 'aaa; aflj' keygenme_O2_strip | jq '.[].name'
```

Cette capacité est la pierre angulaire du scripting avec `r2pipe`, que nous couvrirons en détail dans la section 9.4.

## Récapitulatif des commandes essentielles

| Commande | Mnémonique | Fonction |  
|---|---|---|  
| `aaa` | **a**nalyze **a**ll **a**dvanced | Analyse approfondie du binaire |  
| `afl` | **a**nalyze **f**unctions **l**ist | Lister les fonctions détectées |  
| `afn nom` | **a**nalyze **f**unction **n**ame | Renommer une fonction |  
| `pdf` | **p**rint **d**isasm **f**unction | Désassembler la fonction courante |  
| `pd N` | **p**rint **d**isasm N | Désassembler N instructions |  
| `pds` | **p**rint **d**isasm **s**ummary | Résumé : appels et chaînes uniquement |  
| `pdc` | **p**rint **d**ecompile | Pseudo-décompilation |  
| `pdg` | **p**rint **d**ecompile **g**hidra | Décompileur Ghidra (plugin) |  
| `px N` | **p**rint he**x** | Hex dump de N octets |  
| `iI` | **i**nfo **I**nfo | Métadonnées du binaire |  
| `iS` | **i**nfo **S**ections | Sections ELF |  
| `ii` | **i**nfo **i**mports | Fonctions importées |  
| `ie` | **i**nfo **e**ntrypoint | Point d'entrée |  
| `iz` | **i**nfo string**z** | Chaînes dans les sections de données |  
| `s addr` | **s**eek | Déplacer le curseur |  
| `s-` / `s+` | **s**eek back / forward | Historique de navigation |  
| `axt` | **a**nalyze **x**ref **t**o | XREF vers l'adresse courante |  
| `axf` | **a**nalyze **x**ref **f**rom | XREF depuis l'adresse courante |  
| `/ text` | search | Chercher une chaîne |  
| `/x hex` | search hex | Chercher des octets |  
| `CC text` | **C**omment **C**omment | Ajouter un commentaire |  
| `f nom` | **f**lag | Créer un flag (étiquette) |  
| `V` | **V**isual | Mode visuel plein écran |  
| `VV` | **V**isual **V**isual | Mode graphe ASCII |  
| `V!` | **V**isual panels | Mode panneaux |  
| `q` | **q**uit | Quitter |

---


⏭️ [Scripting avec r2pipe (Python)](/09-ida-radare2-binja/04-scripting-r2pipe.md)
