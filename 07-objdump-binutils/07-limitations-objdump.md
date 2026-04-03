🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 7.7 — Limitations d'`objdump` : pourquoi un vrai désassembleur est nécessaire

> 🔧 **Outils évoqués** : `objdump`, Ghidra, IDA, Radare2, Binary Ninja  
> 📦 **Binaires** : `keygenme_O0`, `keygenme_O2`, `keygenme_strip` (répertoire `binaries/ch07-keygenme/`) et `oop` (répertoire `binaries/ch22-oop/`)  
> 📝 **Syntaxe** : Intel (via `-M intel`)

---

## Un outil indispensable, mais pas suffisant

Tout au long de ce chapitre, nous avons utilisé `objdump` pour désassembler, comparer, annoter et naviguer dans des binaires ELF. Sur les programmes d'entraînement que nous avons étudiés — quelques fonctions, compilés proprement avec GCC, sans obfuscation — `objdump` fait un travail remarquable. Combiné avec `nm`, `readelf`, `c++filt`, `grep` et un peu de méthode, il permet de comprendre la logique d'un binaire de bout en bout.

Mais les binaires d'entraînement sont conçus pour être pédagogiques. Les binaires que vous rencontrerez en conditions réelles — logiciels commerciaux, malware, firmware, challenges CTF avancés — posent des problèmes qu'`objdump` est structurellement incapable de résoudre. Cette section fait un inventaire honnête de ces limitations, pour que vous sachiez exactement à quel moment il faut passer à un outil plus puissant, et surtout **pourquoi**.

---

## Limitation 1 : le désassemblage linéaire

C'est la limitation fondamentale, celle dont découlent la plupart des autres. Nous l'avons introduite dans le README du chapitre, mais ses conséquences méritent d'être détaillées ici, maintenant que vous avez une expérience pratique d'`objdump`.

### Le problème des données dans `.text`

`objdump` décode séquentiellement tous les octets de `.text` comme des instructions. Si le compilateur ou le linker a inséré des **données** au milieu du code — tables de sauts (*jump tables*), constantes littérales, padding d'alignement non-NOP — `objdump` les interprète comme des instructions, produisant des séquences absurdes.

L'exemple classique est la *jump table* générée par GCC pour un `switch` avec de nombreux `case`. Le compilateur place un tableau d'offsets relatifs directement dans `.text`, juste après le code du `switch` :

```asm
; Code du switch (simplifié)
    cmp    eax, 7
    ja     default_case
    lea    rdx, [rip+0x...]          ; adresse de la jump table
    movsxd rax, DWORD PTR [rdx+rax*4]  ; charge l'offset relatif
    add    rax, rdx                   ; calcule l'adresse cible
    jmp    rax                        ; saut indirect

; Jump table (données, pas du code !)
    .long  case0 - table_base
    .long  case1 - table_base
    .long  case2 - table_base
    ...
```

`objdump` ne sait pas que les octets après le `jmp rax` sont des données. Il les décode comme des instructions, et vous voyez apparaître des mnémoniques sans signification :

```asm
    1200:       ff e0                   jmp    rax
    1202:       00 00                   add    BYTE PTR [rax], al
    1204:       1c 00                   sbb    al, 0x0
    1206:       00 00                   add    BYTE PTR [rax], al
    1208:       38 00                   cmp    BYTE PTR [rax], al
    120a:       00 00                   add    BYTE PTR [rax], al
```

Ces `add BYTE PTR [rax], al` n'existent pas dans le programme — ce sont des offsets 32 bits (`0x0000001c`, `0x00000038`…) mal interprétés comme des opcodes. Un reverse engineer expérimenté reconnaît le pattern (séquence de `add BYTE PTR [rax], al` ou d'instructions courtes et répétitives juste après un `jmp` indirect), mais un débutant peut perdre beaucoup de temps à essayer de comprendre ces « instructions ».

Un désassembleur récursif comme Ghidra ou IDA identifie le `jmp rax` comme un saut indirect, analyse les contraintes sur `rax` (la comparaison `cmp eax, 7` qui précède), reconnaît le pattern de *jump table*, et **étiquette les octets suivants comme données** au lieu de les décoder comme du code. La *jump table* apparaît alors correctement dans le listing, et chaque `case` est identifié.

### La désynchronisation

Les instructions x86-64 ont une longueur variable : de 1 octet (`ret` = `c3`) à 15 octets. Si `objdump` commence à décoder au mauvais endroit — typiquement après avoir traversé des données insérées dans `.text` — il peut tomber « au milieu » d'une instruction multi-octets. Le décodeur se désynchronise : il interprète un fragment d'instruction comme le début d'une autre, produisant une cascade d'instructions fantaisistes.

Le décodeur finit généralement par se **resynchroniser** après quelques instructions, car les opcodes x86 ont des longueurs caractéristiques et les séquences invalides sont rares dans du code réel. Mais entre le point de désynchronisation et la resynchronisation, le listing est inutilisable.

Les désassembleurs récursifs ne rencontrent pas ce problème, car ils ne décodent que les chemins atteignables depuis les points d'entrée connus. S'ils n'ont pas de raison de décoder une zone, ils la laissent comme données brutes.

---

## Limitation 2 : pas de graphe de flux de contrôle

`objdump` produit un listing **linéaire** : les instructions défilent de haut en bas, dans l'ordre des adresses. Les sauts conditionnels et inconditionnels apparaissent dans le texte, mais vous devez suivre mentalement les adresses cibles et reconstruire le graphe de flux dans votre tête.

Sur une fonction de 20 instructions avec un seul `if/else`, c'est faisable. Sur une fonction de 200 instructions avec des boucles imbriquées, des `switch`, et des branches de gestion d'erreur, le suivi mental des sauts devient rapidement impraticable. Vous passez votre temps à chercher les adresses cibles dans le listing, à revenir en arrière, à perdre le fil.

Les désassembleurs modernes offrent des **vues en graphe** qui transforment le listing linéaire en un diagramme de blocs de base (*basic blocks*) reliés par des flèches (vertes pour le branchement pris, rouges pour le non-pris). Cette représentation rend immédiatement visible la structure du code : les boucles (flèches qui remontent), les branches (`if/else` en forme de losange), les `switch` (un nœud avec de multiples flèches sortantes).

Ghidra appelle cette vue le « Function Graph ». IDA l'appelle le « Graph View ». Radare2/Cutter l'offre via le mode `VV`. Tous transforment la même information — un listing d'instructions avec des sauts — en une représentation que le cerveau humain traite beaucoup plus efficacement.

`objdump` n'a tout simplement pas cette capacité. C'est du texte brut, point.

---

## Limitation 3 : pas de décompilation

La fonctionnalité qui sépare le plus radicalement `objdump` des outils de RE modernes est la **décompilation** : la traduction automatique de l'assembleur en pseudo-code C lisible.

Quand Ghidra vous montre :

```c
int check_serial(char *input, char *expected) {
    int hash = compute_hash(input);
    char buffer[32];
    sprintf(buffer, "%08x", hash);
    return strcmp(buffer, expected) == 0;
}
```

… à côté du listing assembleur, le gain de temps est considérable. Le pseudo-code n'est pas parfait (les noms de variables sont inventés, les types sont parfois approximatifs, certaines constructions sont mal reconstruites), mais il donne une vue d'ensemble quasi-instantanée de ce que fait la fonction.

Avec `objdump`, vous avez les mêmes octets à votre disposition, mais il vous faut **mentalement** reconstruire ce pseudo-code en lisant chaque instruction une par une. C'est un exercice formateur (et c'est pourquoi nous l'avons fait dans ce chapitre), mais sur un binaire de taille réelle, ce n'est pas viable comme méthode de travail quotidienne.

Les principaux décompilateurs sont :

- **Ghidra** (gratuit, open source) — couvert au chapitre 8  
- **IDA + Hex-Rays** (commercial) — couvert au chapitre 9  
- **Binary Ninja** (commercial, version cloud gratuite) — couvert au chapitre 9  
- **RetDec** (gratuit, open source, Avast) — couvert au chapitre 20

---

## Limitation 4 : pas de cross-references (XREF)

Quand vous analysez une fonction dans `objdump`, une question revient sans cesse : **qui appelle cette fonction ?** Et symétriquement : **cette chaîne de caractères, où est-elle utilisée ?** Ces questions portent sur les *cross-references* (XREF) — les liens bidirectionnels entre un point du code et les endroits qui le référencent.

Avec `objdump`, répondre à ces questions exige un `grep` manuel :

```bash
# Qui appelle la fonction à 0x1139 ?
$ objdump -d -M intel keygenme_strip | grep "call.*1139"
    11f5:       e8 3f ff ff ff          call   1139
```

Ce `grep` fonctionne pour les `call` directs, mais rate les cas suivants :

- **Appels indirects** (`call rax` où `rax` contient `0x1139`) — le `grep` ne peut pas les résoudre sans analyse de flux.  
- **Références à des données** — une chaîne dans `.rodata` référencée par un `lea rdi, [rip+0x...]` n'est pas trouvable par un `grep` sur l'adresse de la chaîne, car le listing affiche l'offset RIP-relatif, pas l'adresse finale.  
- **Pointeurs de fonctions stockés dans des tableaux** (vtables, tableaux de callbacks) — la référence est dans `.data.rel.ro`, pas dans le code.  
- **Références multiples** — une fonction appelée depuis 15 endroits différents nécessite un `grep` qui peut retourner du bruit si l'adresse apparaît comme constante ailleurs.

Ghidra, IDA et Radare2 construisent automatiquement un **index complet de toutes les cross-references** lors de l'analyse initiale. Pour chaque fonction, chaque variable globale, chaque chaîne, vous pouvez obtenir instantanément la liste de tous les endroits qui y font référence (et inversement). C'est une base de données relationnelle du binaire, navigable en un clic.

Cette capacité change fondamentalement la façon de travailler. Au lieu de chercher une aiguille dans un listing, vous naviguez un graphe de relations.

---

## Limitation 5 : pas de typage ni de reconstruction de structures

Quand vous lisez un accès mémoire comme `mov eax, DWORD PTR [rbx+0x14]` dans `objdump`, vous savez qu'un entier de 4 octets est lu à l'offset `0x14` par rapport à `rbx`. Mais vous ne savez pas ce que **représente** cet offset. Est-ce le champ `age` d'une structure `Person` ? Le champ `length` d'un buffer ? Le quatrième élément d'un tableau d'entiers ?

`objdump` travaille au niveau des octets et des adresses. Il n'a aucune notion de types, de structures, de classes, ou de layouts mémoire.

Les désassembleurs avancés permettent de **définir des types** et de les appliquer aux accès mémoire. Dans Ghidra, vous pouvez créer une structure :

```c
struct Animal {
    void **vtable;     // offset 0x00
    char name[32];     // offset 0x08
    int age;           // offset 0x28
};
```

Une fois cette structure appliquée au registre `rbx`, l'accès `[rbx+0x14]` se transforme en `animal->name[12]` dans le listing et dans le décompilateur. Le code devient soudain lisible, et les accès mémoire prennent un sens.

Cette capacité de reconstruction de structures est couverte en détail au chapitre 8 (section 8.6) pour Ghidra et au chapitre 17 pour les structures C++ spécifiques (vtables, RTTI, STL).

---

## Limitation 6 : pas d'analyse inter-procédurale

`objdump` désassemble chaque instruction individuellement. Il ne « comprend » pas les relations entre fonctions : quel est le graphe d'appel complet du programme ? Quelles fonctions sont des *leaf functions* ? Quelle est la profondeur maximale de la pile d'appels ? Quelles fonctions accèdent à une variable globale donnée ?

Les outils de RE modernes construisent un **graphe d'appel** (*call graph*) global qui montre les relations appelant/appelé entre toutes les fonctions du programme. Ce graphe permet de :

- Identifier les fonctions « centrales » (appelées depuis de nombreux endroits) — souvent des fonctions utilitaires.  
- Repérer les fonctions « feuilles » (qui n'appellent aucune autre fonction) — souvent des fonctions de calcul pur.  
- Tracer le chemin d'exécution de `main()` jusqu'à une fonction cible — essentiel pour comprendre dans quel contexte une fonction est invoquée.  
- Identifier les fonctions mortes (jamais appelées) — potentiellement du code de débogage ou des fonctions désactivées.

Avec `objdump`, vous pouvez construire manuellement un graphe d'appel en collectant tous les `call` avec `grep`, mais c'est fastidieux et incomplet (les appels indirects échappent à cette méthode).

---

## Limitation 7 : pas de persistance de l'analyse

Quand vous passez une heure à analyser un binaire avec `objdump` — identifier les fonctions, deviner leurs rôles, comprendre les structures de données — tout ce travail existe uniquement dans votre tête (et peut-être dans des notes manuscrites ou des commentaires dans un fichier texte). Si vous fermez le terminal et revenez le lendemain, vous repartez de zéro.

Les désassembleurs graphiques offrent des **projets persistants**. Ghidra sauvegarde votre analyse dans un projet : les fonctions que vous avez renommées, les commentaires que vous avez ajoutés, les types que vous avez définis, les bookmarks que vous avez posés — tout est conservé. Vous pouvez interrompre votre travail et le reprendre des jours plus tard sans perdre le contexte.

IDA va plus loin avec ses fichiers `.idb`/`.i64` qui capturent l'état complet de l'analyse. Radare2 propose les « projects » avec un mécanisme similaire. Binary Ninja sauvegarde ses annotations dans des bases de données dédiées.

`objdump` ne sauvegarde rien. Chaque exécution repart de zéro. C'est parfait pour un coup d'œil rapide, insuffisant pour une analyse de longue haleine.

---

## Limitation 8 : pas de support des architectures multiples dans un même workflow

`objdump` supporte de nombreuses architectures (x86, ARM, MIPS, PowerPC, RISC-V…) via l'option `-m` ou automatiquement si le binaire contient l'information. Mais il ne peut désassembler qu'**une seule architecture à la fois**, et il n'offre aucun mécanisme pour comparer, corréler, ou naviguer entre des binaires d'architectures différentes.

Ghidra, en revanche, peut ouvrir simultanément dans le même projet un binaire x86-64 et un binaire ARM, appliquer les mêmes structures de données aux deux, et comparer le code produit par des compilateurs différents pour la même source. C'est crucial pour l'analyse de firmware (où le même code est parfois compilé pour plusieurs cibles) et pour le RE multi-plateforme.

---

## Quand rester sur `objdump`, quand passer à autre chose

Les limitations listées ci-dessus ne signifient pas qu'`objdump` est obsolète. Il reste le meilleur choix dans certains contextes :

| Situation | Outil recommandé |  
|---|---|  
| Coup d'œil rapide sur un binaire (triage) | `objdump` |  
| Vérifier le contenu réel d'une section | `objdump` |  
| Comparer deux compilations (diff rapide) | `objdump` + `diff` |  
| Environnement minimal (serveur SSH, conteneur) | `objdump` (seul outil disponible) |  
| Scripting et automatisation shell | `objdump` + `grep`/`awk`/`sed` |  
| Confirmer une sortie suspecte de Ghidra/IDA | `objdump` (vérité terrain) |  
| Analyse complète d'un binaire strippé de 50+ fonctions | Ghidra / IDA / Radare2 |  
| Reconstruction de structures et de classes C++ | Ghidra / IDA |  
| Suivi du flux de contrôle dans une fonction complexe | Ghidra (graph view) / IDA |  
| Analyse de malware avec obfuscation | Ghidra + analyse dynamique |  
| Travail d'équipe sur un binaire | Ghidra (projets partagés) / IDA |

La règle pratique est simple : **commencez par `objdump`** pour le triage et la compréhension initiale (c'est le workflow du chapitre 5, section 5.7). Si l'analyse dépasse quelques minutes et que le binaire a plus d'une poignée de fonctions, **importez-le dans Ghidra**. Vous pourrez toujours revenir à `objdump` ponctuellement pour vérifier un détail ou pour scripter une extraction.

---

## Ce que vous avez gagné dans ce chapitre

Malgré ses limitations, le travail avec `objdump` que nous avons fait dans ce chapitre vous a apporté quelque chose qu'aucun outil graphique ne peut remplacer : la capacité de **lire l'assembleur brut**. Quand Ghidra affiche un listing dans son CodeBrowser, c'est le même assembleur que celui d'`objdump` — avec des annotations supplémentaires, certes, mais la base est identique. Quand le décompilateur de Ghidra produit un pseudo-code suspect, c'est en revenant à l'assembleur que vous vérifierez s'il a raison.

Les compétences construites dans ce chapitre — repérer les frontières de fonctions, lire les prologues et épilogues, identifier le niveau d'optimisation, retrouver `main()` sur un binaire strippé, démangler les symboles C++ — sont des compétences que vous utiliserez **dans** Ghidra, **dans** IDA, **dans** GDB, et dans tout autre outil. `objdump` était le véhicule d'apprentissage ; les réflexes acquis sont portables.

Le chapitre 8 ouvrira Ghidra et vous montrera comment ces mêmes analyses deviennent plus rapides, plus profondes, et plus confortables avec un outil conçu pour le RE. Vous y arriverez avec les fondations nécessaires pour en tirer le meilleur parti.

---

## Résumé

`objdump` est un désassembleur linéaire : il décode les octets séquentiellement sans suivre les branchements. Cette approche le rend vulnérable aux données insérées dans le code (jump tables, constantes), à la désynchronisation, et incapable de reconstituer le graphe de flux de contrôle. Au-delà de l'algorithme de désassemblage, `objdump` ne propose ni décompilation, ni cross-references, ni typage de structures, ni graphe d'appel, ni persistance de l'analyse. Ces fonctionnalités sont précisément ce que les désassembleurs modernes comme Ghidra, IDA, Radare2 et Binary Ninja apportent. Pour autant, `objdump` reste irremplaçable pour le triage rapide, le scripting, la vérification d'une sortie suspecte d'un autre outil, et le travail en environnement minimal. L'enjeu n'est pas de choisir l'un ou l'autre, mais de savoir quand utiliser chacun.

---


⏭️ [🎯 Checkpoint : désassembler `keygenme_O0` et `keygenme_O2`, lister les différences clés](/07-objdump-binutils/checkpoint.md)
