🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.7 — Cross-references (XREF) : tracer l'usage d'une fonction ou d'une donnée

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Qu'est-ce qu'une cross-reference ?

Une cross-reference (abrégée **XREF**) est un lien qui relie deux emplacements dans un binaire : une **source** (l'endroit qui fait référence) et une **destination** (l'endroit référencé). Quand une instruction `CALL 0x004011a0` se trouve à l'adresse `0x00401350`, Ghidra enregistre une cross-reference dont la source est `0x00401350` et la destination est `0x004011a0`. De même, quand une instruction `LEA RDI, [0x00402010]` charge l'adresse d'une chaîne dans `.rodata`, Ghidra crée une cross-reference de l'instruction vers la donnée.

Les cross-references sont le **tissu conjonctif** d'un binaire analysé. Elles matérialisent les relations entre le code et les données : quelles fonctions appellent quelles fonctions, quel code accède à quelle variable globale, quelles instructions référencent quelle chaîne de caractères, quels constructeurs initialisent quel vptr. Sans elles, le binaire serait un ensemble de fragments isolés ; avec elles, il devient un graphe navigable où chaque élément est relié à son contexte d'utilisation.

L'exploitation efficace des cross-references est l'une des compétences qui distingue un analyste débutant d'un analyste expérimenté. C'est ce qui permet de répondre à des questions comme : « Qui appelle cette fonction ? », « Où cette chaîne d'erreur est-elle utilisée ? », « Quelles fonctions accèdent à cette variable globale ? », « Depuis combien d'endroits ce flag est-il modifié ? ».

---

## Types de cross-references

Ghidra catégorise les cross-references selon la nature du lien entre la source et la destination. Chaque type est identifié par un suffixe dans l'affichage du Listing.

### Références de code (Code References)

Ces références relient des instructions entre elles ou des instructions vers des points d'entrée de fonctions.

**Call (`c`)** — La source est une instruction `CALL` et la destination est le point d'entrée de la fonction appelée. C'est le type de référence le plus courant et le plus utile pour comprendre l'architecture d'un programme.

```
XREF[2]:  main:00401203(c), handle_input:00401345(c)
```

Cet affichage au-dessus d'une fonction indique qu'elle est appelée depuis deux endroits : une fois depuis `main` à l'adresse `0x00401203`, et une fois depuis `handle_input` à l'adresse `0x00401345`.

**Unconditional Jump (`j`)** — La source est un saut inconditionnel (`JMP`) vers la destination. Ces références apparaissent entre les blocs basiques d'une même fonction (branchements internes) ou parfois entre fonctions (tail calls, trampolines PLT).

**Conditional Jump (`j`)** — La source est un saut conditionnel (`JZ`, `JNZ`, `JL`, `JGE`, etc.) vers la destination. Comme pour les sauts inconditionnels, ces références relient des blocs basiques.

> 💡 Ghidra utilise le même suffixe `(j)` pour les sauts conditionnels et inconditionnels. Pour les distinguer, il faut examiner l'instruction source.

**Fall-through (`f`)** — La référence implicite qui relie une instruction à l'instruction suivante en mémoire quand il n'y a pas de saut. Ce type est rarement affiché explicitement mais il existe dans le modèle interne de Ghidra et contribue à la construction du graphe de flux de contrôle.

### Références de données (Data References)

Ces références relient du code à des données, ou des données à d'autres données.

**Read (`r`)** — Le code source lit la donnée à la destination. Par exemple, une instruction `MOV EAX, [global_counter]` crée une référence en lecture vers `global_counter`.

**Write (`w`)** — Le code source écrit dans la donnée à la destination. Par exemple, `MOV [global_counter], EAX` crée une référence en écriture.

**Data/Pointer (`*`)** — La source est une donnée (pas une instruction) qui contient un pointeur vers la destination. C'est typique des vtables (pointeurs vers des méthodes), des tables de pointeurs de fonctions, des GOT entries, et des structures qui contiennent des pointeurs vers d'autres structures.

```
XREF[1]:  .data.rel.ro:00402050(*)
```

Cet affichage sur une fonction indique qu'un pointeur vers elle existe dans `.data.rel.ro` à l'adresse `0x00402050` — probablement une entrée de vtable.

### Récapitulatif des suffixes

| Suffixe | Type | Signification |  
|---|---|---|  
| `(c)` | Call | Appel de fonction (`CALL`) |  
| `(j)` | Jump | Saut conditionnel ou inconditionnel |  
| `(f)` | Fall-through | Flux séquentiel implicite |  
| `(r)` | Read | Lecture de donnée |  
| `(w)` | Write | Écriture de donnée |  
| `(*)` | Data/Pointer | Pointeur dans une table de données |

---

## Affichage des XREF dans le Listing

### En-tête de fonction

Au-dessus du label d'une fonction, Ghidra affiche un résumé des cross-references entrantes. Quand le nombre de références est faible (typiquement 5 ou moins), elles sont listées individuellement :

```
                     ******************************************************
                     *                    FUNCTION                         *
                     ******************************************************
                     XREF[3]:  main:00401203(c),
                               process_input:00401345(c),
                               handle_event:004015a0(c)
```

Quand le nombre dépasse le seuil d'affichage (configurable), Ghidra affiche uniquement le compteur `XREF[47]` avec un lien cliquable pour voir la liste complète.

### En-tête de donnée

Le même principe s'applique aux données (variables globales, chaînes, constantes). Au-dessus d'une chaîne dans `.rodata`, par exemple :

```
                     XREF[1]:  check_password:004012b8(r)
.rodata:00402060     ds  "Invalid password. Try again."
```

Cela indique qu'une seule instruction, à l'adresse `0x004012b8` dans la fonction `check_password`, référence cette chaîne — en lecture.

### Indicateurs dans la marge

Ghidra place aussi des petites icônes dans la marge gauche du Listing pour signaler les références. Les flèches indiquent la direction du flux : une flèche entrante signale une destination de saut ou d'appel, une flèche sortante signale un saut ou un appel depuis cette instruction.

---

## La fenêtre References

### Afficher les références d'un élément

Pour voir la liste complète et détaillée des cross-references vers ou depuis un élément, sélectionnez-le dans le Listing ou le Decompiler et appuyez sur **`X`** (ou clic droit → **References → Show References to**). La fenêtre **References** s'ouvre avec un tableau listant toutes les références.

Ce tableau contient les colonnes suivantes :

- **From Location** — l'adresse source de la référence ;  
- **From Label** — la fonction ou le label contenant la source ;  
- **Ref Type** — le type de la référence (`UNCONDITIONAL_CALL`, `DATA`, `READ`, `WRITE`, `COMPUTED_JUMP`, etc.) ;  
- **To Location** — l'adresse destination ;  
- **To Label** — la fonction ou le label de la destination.

Double-cliquez sur n'importe quelle ligne pour naviguer directement vers l'adresse source. C'est le mécanisme fondamental de navigation par contexte d'utilisation.

### Références depuis un élément (References from)

L'opération inverse est aussi possible. Clic droit sur un élément → **References → Show References from** affiche la liste de tout ce que cet élément référence. Pour une fonction, cela montre toutes les fonctions qu'elle appelle et toutes les données qu'elle accède — c'est essentiellement la liste de ses dépendances.

### Filtrer les références

La fenêtre References permet de filtrer par type. Si une variable globale a 200 références et que vous ne vous intéressez qu'aux écritures, vous pouvez filtrer pour n'afficher que les références de type `WRITE`. Cela réduit considérablement le bruit quand vous cherchez « où ce flag est-il modifié ? » parmi des dizaines de lectures.

---

## Cas d'usage concrets des XREF

Les cross-references sont un outil transversal qui intervient à presque toutes les étapes de l'analyse. Voici les scénarios les plus courants.

### Remonter depuis une chaîne de caractères

C'est probablement l'usage le plus fréquent pour un débutant, et il reste fondamental à tous les niveaux. Le scénario type :

1. Vous ouvrez **Defined Strings** et repérez une chaîne suspecte : `"License valid. Access granted."`.  
2. Double-cliquez pour naviguer vers cette chaîne dans `.rodata`.  
3. Appuyez sur `X` pour voir les références vers cette chaîne.  
4. La fenêtre References montre une unique référence : `check_license:00401456(r)`.  
5. Double-cliquez pour naviguer vers l'instruction qui charge cette chaîne. Vous êtes maintenant dans la fonction `check_license`, exactement à l'endroit qui affiche le message de succès.  
6. Remontez dans le flux de contrôle de cette fonction pour comprendre quelle condition mène à ce message.

Ce workflow « chaîne → XREF → code → analyse de flux » est l'un des plus efficaces pour localiser rapidement une fonctionnalité dans un binaire inconnu.

### Comprendre l'usage d'une variable globale

Quand vous identifiez une variable globale importante (un compteur, un flag de configuration, un buffer de données), les XREF vous montrent l'ensemble de son cycle de vie :

1. Naviguez vers la variable dans `.data` ou `.bss`.  
2. Appuyez sur `X`.  
3. Examinez les références classées par type :  
   - Les références `(w)` montrent les endroits où la variable est **modifiée** — typiquement l'initialisation et les mises à jour.  
   - Les références `(r)` montrent les endroits où la variable est **lue** — les fonctions qui dépendent de sa valeur.  
   - Les références `(*)` montrent les endroits où son **adresse** est prise — souvent pour la passer à une fonction par pointeur.

Ce diagnostic complet vous donne la carte d'impact d'une variable globale à travers le programme entier.

### Identifier les appelants d'une fonction (call graph ascendant)

Pour comprendre dans quel contexte une fonction est invoquée :

1. Naviguez vers la fonction cible.  
2. Appuyez sur `X`.  
3. Filtrez par type `CALL`.  
4. La liste résultante montre tous les sites d'appel.

Si la fonction n'est appelée que depuis un seul endroit, son rôle est fortement contraint par le contexte de l'appelant. Si elle est appelée depuis 50 endroits, c'est probablement une fonction utilitaire générique (un helper, un wrapper, une fonction de validation commune).

Le nombre d'appelants est aussi un indicateur de l'importance d'une fonction dans l'architecture du programme. Une fonction avec un grand nombre de XREF Call mérite souvent d'être analysée et renommée en priorité.

### Identifier les fonctions appelées (call graph descendant)

L'opération inverse — quelles fonctions sont appelées par une fonction donnée — est accessible via **References from**. Cela donne une vue de haut niveau de ce que fait la fonction : si elle appelle `socket`, `connect`, `send`, `recv`, c'est une fonction réseau. Si elle appelle `fopen`, `fread`, `fclose`, c'est une fonction d'entrée/sortie fichier.

### Tracer un pointeur de vtable

Dans un binaire C++, les XREF de type `(*)` sont particulièrement précieux pour relier les vtables aux constructeurs et destructeurs :

1. Naviguez vers une vtable dans `.rodata`.  
2. Appuyez sur `X` sur l'adresse de la vtable (le point d'entrée, pas l'offset-to-top).  
3. Les références `(w)` vers cette adresse proviennent des constructeurs et destructeurs qui initialisent le vptr. Chaque constructeur écrit l'adresse de la vtable au début de l'objet `this`.  
4. Vous identifiez immédiatement quelles fonctions sont des constructeurs pour cette classe.

Ce pattern est fiable même dans les binaires strippés, car le mécanisme d'initialisation du vptr est inhérent au modèle objet et ne peut pas être supprimé.

### Localiser les handlers d'un switch par table de fonctions

Certains programmes implémentent un dispatch via un tableau de pointeurs de fonctions plutôt qu'un `switch` classique :

```c
// Dans .rodata : une table de pointeurs
handler_table[0] = &handle_cmd_ping;  
handler_table[1] = &handle_cmd_auth;  
handler_table[2] = &handle_cmd_data;  
```

Les XREF de type `(*)` sur chaque handler indiquent leur présence dans cette table. En examinant le code qui indexe la table, vous comprenez le mécanisme de dispatch. Les XREF vous évitent de manquer des handlers qui ne sont jamais appelés directement par `CALL` mais uniquement via la table.

---

## Le graphe d'appels (Call Graph)

### Function Call Graph

Au-delà des XREF individuels, Ghidra peut construire un **graphe d'appels** visuel montrant les relations d'appel entre fonctions. Accédez-y via :

- **Window → Function Call Graph**

Ce graphe affiche la fonction courante au centre, avec ses appelants (fonctions qui l'appellent) d'un côté et ses appelés (fonctions qu'elle appelle) de l'autre. Vous pouvez étendre le graphe en double-cliquant sur un nœud pour explorer ses propres appelants et appelés.

Le graphe d'appels est utile pour :

- **Comprendre la position d'une fonction dans l'architecture globale** — est-elle une fonction de haut niveau (beaucoup d'appelés, peu d'appelants) ou une fonction utilitaire (peu d'appelés, beaucoup d'appelants) ?  
- **Identifier les chemins d'exécution** — comment atteint-on cette fonction depuis `main` ? Quelles sont les chaînes d'appels possibles ?  
- **Visualiser les dépendances** — quelles bibliothèques ou sous-systèmes sont sollicités par une fonction ?

### Function Call Trees

Le menu **Window → Function Call Trees** offre une vue arborescente (textuelle, pas graphique) des mêmes informations. Deux onglets :

- **Incoming Calls** — l'arbre des appelants, récursivement. La racine est la fonction courante, et chaque niveau montre qui appelle le niveau précédent.  
- **Outgoing Calls** — l'arbre des appelés, récursivement. La racine est la fonction courante, et chaque niveau montre ce qu'elle appelle.

Cette vue arborescente est souvent plus pratique que le graphe visuel pour les analyses en profondeur, car elle est plus compacte et plus facilement navigable quand le nombre de nœuds est élevé.

---

## Références manuelles

### Quand Ghidra manque une référence

L'analyse automatique de Ghidra est performante, mais elle ne détecte pas toutes les références. Les cas courants de références manquantes sont :

**Appels indirects via registre** — Une instruction `CALL RAX` appelle la fonction dont l'adresse est dans `RAX`. Ghidra ne peut pas toujours résoudre statiquement la valeur de `RAX` à cet endroit, et la référence peut manquer. C'est fréquent dans le dispatch virtuel C++ (appel via vtable) et dans les callbacks/pointeurs de fonctions.

**Adresses calculées** — Si une adresse est construite par une séquence arithmétique complexe (`LEA` + `ADD` + décalage), Ghidra peut ne pas la résoudre en référence.

**Tables de données non typées** — Un tableau de pointeurs dans `.data` qui n'a pas été typé comme tel apparaît comme une séquence d'entiers 64 bits, sans références vers les fonctions pointées.

### Créer une référence manuellement

Vous pouvez ajouter des références manuelles pour combler ces lacunes :

1. Placez le curseur dans le Listing à l'adresse source.  
2. Clic droit → **References → Add Reference from…**  
3. Spécifiez l'adresse destination et le type de référence (Call, Data, Read, Write).  
4. Validez.

La référence apparaît dans le Listing et dans les résultats de `X` (Show References). Les références manuelles sont visuellement identiques aux références automatiques — Ghidra les traite de la même manière pour la navigation et l'analyse.

> ⚠️ **Prudence** — N'ajoutez des références manuelles que lorsque vous êtes raisonnablement certain de la relation. Une référence erronée peut induire en erreur l'analyse ultérieure et le décompileur. En cas de doute, préférez un commentaire qui note l'hypothèse.

### Supprimer une référence incorrecte

Si l'analyse automatique a créé une référence erronée (ce qui arrive parfois avec les données mal typées ou les instructions ambiguës), vous pouvez la supprimer via clic droit → **References → Delete Reference from…**. Sélectionnez la référence à supprimer dans la liste qui apparaît.

---

## Recherche de références avancée

### Search → For Direct References

Le menu **Search → For Direct References** permet de chercher toutes les occurrences d'une adresse spécifique dans le binaire, y compris dans les zones non analysées ou les données brutes. C'est plus exhaustif que les XREF standards, qui ne montrent que les références détectées par l'analyse.

Ce type de recherche est utile pour trouver des références que l'analyse a manquées : des pointeurs dans des tables de données non typées, des adresses hardcodées dans des zones interprétées comme données plutôt que comme code, ou des références dans des sections inhabituelles.

### Search → For Address Tables

Cette recherche heuristique tente de localiser des **tables de pointeurs** dans les sections de données — des séquences contiguës de valeurs qui sont toutes des adresses valides dans l'espace du programme. C'est particulièrement utile pour détecter des tables de handlers, des vtables non labellisées, ou des tableaux de pointeurs de fonctions.

Ghidra paramètre cette recherche avec des critères de longueur minimale (nombre d'entrées consécutives qui doivent être des adresses valides) et d'alignement. Les résultats sont des candidats — vous devez les valider manuellement.

---

## Workflow XREF intégré à l'analyse

Les cross-references ne sont pas un outil isolé qu'on utilise ponctuellement — elles s'intègrent dans chaque étape du workflow d'analyse. Voici comment elles interviennent dans les différentes phases décrites dans les sections précédentes de ce chapitre.

**Pendant le triage initial** — Après avoir identifié les chaînes intéressantes dans Defined Strings, utilisez les XREF pour localiser le code qui les utilise. En quelques clics, vous passez d'une chaîne `"Connection refused"` à la fonction de gestion réseau qui la produit.

**Pendant le renommage (section 8.4)** — Quand vous renommez une fonction, vérifiez ses XREF pour vous assurer que le nom est cohérent dans tous les contextes d'appel. Si `FUN_00401500` est appelée depuis une fonction réseau et depuis une fonction de fichier, le nom choisi doit refléter cette polyvalence (par exemple `read_buffer` plutôt que `read_socket`).

**Pendant la reconstruction de structures (section 8.6)** — Les XREF de type `(w)` sur une variable globale ou un champ de structure montrent les fonctions qui modifient la structure. Analyser ces fonctions complète votre connaissance du layout en révélant des champs que la fonction initiale n'accédait pas.

**Pendant l'analyse C++ (section 8.5)** — Les XREF `(*)` vers les vtables identifient les constructeurs. Les XREF `(c)` vers `__cxa_throw` localisent les points de lancement d'exceptions. Les XREF vers les typeinfo permettent de tracer les relations d'héritage.

**Pendant la préparation de l'analyse dynamique (Partie III)** — Identifiez via les XREF les fonctions critiques (vérification de licence, déchiffrement, authentification) et notez leurs adresses pour y poser des breakpoints dans GDB (Chapitre 11) ou des hooks dans Frida (Chapitre 13).

---

## Résumé

Les cross-references sont le mécanisme central qui relie les éléments d'un binaire entre eux dans Ghidra. Elles se déclinent en références de code (Call, Jump) et de données (Read, Write, Pointer), identifiables par leurs suffixes dans le Listing. La touche `X` est le raccourci le plus important de cette section — il ouvre la fenêtre References depuis n'importe quel élément et permet de naviguer instantanément vers tous les contextes d'utilisation. Le graphe d'appels et les arbres d'appels offrent des vues de plus haut niveau sur l'architecture du programme. Enfin, les références manuelles et les recherches avancées comblent les lacunes de l'analyse automatique quand des relations sont manquées.

La section suivante introduit les scripts Ghidra en Java et Python, qui permettent d'automatiser les opérations répétitives — y compris l'exploitation systématique des cross-references à l'échelle d'un binaire entier.

---


⏭️ [Scripts Ghidra en Java/Python pour automatiser l'analyse](/08-ghidra/08-scripts-java-python.md)
