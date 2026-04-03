🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.6 — Colorisation, annotations et bookmarks de régions binaires

> 🎯 **Objectif de cette section** : Exploiter les mécanismes de colorisation, d'annotation et de bookmarks d'ImHex pour documenter progressivement l'analyse d'un binaire, produire des cartographies visuelles lisibles et partageables, et maintenir un contexte cumulatif entre les sessions de travail.

---

## Le problème de la mémoire de l'analyste

Le reverse engineering est un travail de longue haleine. On ne comprend pas un binaire en une seule session. On explore une zone, on identifie quelques champs, on passe à une autre région du fichier, on revient sur la première avec de nouvelles informations obtenues entre-temps par le désassemblage ou le débogage. Ce va-et-vient peut s'étaler sur des heures, des jours, parfois des semaines pour des cibles complexes.

Le risque principal de ce processus itératif est la **perte de contexte**. Vous identifiez qu'à l'offset `0x2040` commence une table de 12 entrées de 32 octets chacune, que le champ à l'offset `+0x08` de chaque entrée est un pointeur vers `.rodata`, et que les 4 premiers octets semblent être un identifiant numérique. Vous passez à autre chose. Deux jours plus tard, vous revenez sur cette zone et vous avez tout oublié. Les octets sont toujours là, muets, identiques — c'est votre compréhension qui s'est évaporée.

Les patterns `.hexpat` résolvent une partie de ce problème en formalisant la structure des données. Mais un pattern est une description **structurelle** : il dit quels types occupent quels octets. Il ne capture pas le raisonnement, les hypothèses provisoires, les questions ouvertes, ni la cartographie de haut niveau du fichier. C'est exactement le rôle des bookmarks, des annotations et de la colorisation manuelle.

---

## Bookmarks : marquer et nommer des régions

### Créer un bookmark

La création d'un bookmark suit un geste simple : sélectionnez une plage d'octets dans la vue hexadécimale, puis utilisez l'une de ces méthodes :

- Clic droit sur la sélection → **Create Bookmark**  
- Raccourci `Ctrl+B`

ImHex ouvre une boîte de dialogue avec quatre champs :

- **Name** — le nom du bookmark, affiché dans la liste et dans la vue hex au survol. Choisissez un nom descriptif et concis : `Table des entrées de configuration`, `Clé AES hardcodée`, `Début du payload chiffré`.  
- **Color** — la couleur de surlignage dans la vue hexadécimale. ImHex propose un sélecteur de couleur complet. Nous verrons plus bas comment choisir des couleurs de manière cohérente.  
- **Comment** — un texte libre de longueur arbitraire. C'est ici que vous documentez votre raisonnement : pourquoi vous avez identifié cette zone, quelles hypothèses vous avez formulées, quelles questions restent ouvertes.  
- **Region** — l'offset de début et la taille, pré-remplis à partir de votre sélection. Vous pouvez les ajuster manuellement si nécessaire.

### Le panneau Bookmarks

Tous les bookmarks créés apparaissent dans le panneau **Bookmarks** (activable via **View → Bookmarks** s'il n'est pas déjà visible). Ce panneau affiche une liste ordonnée par offset avec le nom, la couleur, l'offset de début, la taille et le début du commentaire de chaque bookmark.

Un clic sur un bookmark dans la liste **fait sauter la vue hexadécimale** à l'offset correspondant et sélectionne la région. C'est un moyen de navigation bien plus ergonomique que de mémoriser des adresses hexadécimales ou de les noter dans un fichier texte séparé.

### Modifier et supprimer des bookmarks

Un double-clic sur un bookmark dans la liste ouvre la boîte d'édition. Vous pouvez modifier le nom, la couleur, le commentaire et la région à tout moment. Pour supprimer un bookmark, clic droit → **Delete** dans la liste.

### Bookmarks comme journal d'analyse

Au-delà de leur fonction de navigation, les bookmarks constituent un **journal d'analyse intégré au fichier**. Le champ commentaire peut contenir des observations détaillées :

```
Nom : Routine de vérification du serial  
Offset : 0x1240  
Taille : 86 octets  
Commentaire :  
  Cette fonction compare le serial saisi par l'utilisateur
  avec une valeur dérivée du nom d'utilisateur.
  - Le XOR avec 0x5A est appliqué octet par octet (cf. offset 0x1258)
  - Le résultat est comparé avec strcmp à l'offset 0x1280
  - Hypothèse : la clé de dérivation pourrait aussi dépendre
    de la longueur du nom (à vérifier avec GDB, chapitre 11)
  Statut : HYPOTHÈSE — non confirmé dynamiquement
```

Ce niveau de documentation transforme votre fichier ImHex en un livrable d'analyse, pas seulement en un outil de travail. Un collègue qui ouvre le projet peut retracer votre raisonnement sans vous demander d'explications.

---

## Colorisation : rendre la structure visible

### Couleurs automatiques des patterns

Quand vous évaluez un pattern `.hexpat`, ImHex attribue automatiquement des couleurs distinctes aux différentes structures et à leurs champs dans la vue hexadécimale. Ce comportement par défaut est souvent suffisant : les structures sont visuellement séparées les unes des autres, et les champs au sein d'une même structure sont différenciés par des nuances.

Vous pouvez influencer ces couleurs depuis le pattern lui-même avec l'attribut `[[color(...)]]` vu en section 6.3 :

```cpp
u32 magic [[color("FF4444")]];           // rouge vif — attirer l'attention  
u32 checksum [[color("44FF44")]];        // vert — valeur de contrôle  
u8  encrypted_data[256] [[color("4444FF")]]; // bleu — données chiffrées  
```

### Couleurs manuelles via les bookmarks

Indépendamment des patterns, les bookmarks ajoutent une **couche de colorisation manuelle** par-dessus la vue hexadécimale. Les deux systèmes coexistent : les couleurs des patterns montrent la structure granulaire (champ par champ), tandis que les couleurs des bookmarks montrent les **régions fonctionnelles** de haut niveau (« cette zone est le header », « cette zone est le payload chiffré », « cette zone est du padding »).

### Choisir une palette cohérente

La colorisation n'est utile que si elle est lisible. Quand vous marquez manuellement des régions, adoptez une convention de couleurs cohérente et tenez-vous-y tout au long de l'analyse. Voici une palette que nous utiliserons dans les cas pratiques de cette formation :

| Couleur | Code hex | Usage |  
|---|---|---|  
| Rouge | `#FF6666` | Données critiques : clés crypto, mots de passe, secrets |  
| Orange | `#FFaa44` | Headers et métadonnées de structure |  
| Jaune | `#FFEE55` | Chaînes de caractères, noms, identifiants textuels |  
| Vert | `#66DD66` | Checksums, CRC, valeurs de contrôle d'intégrité |  
| Bleu | `#6699FF` | Code exécutable, opcodes |  
| Violet | `#CC88FF` | Données chiffrées ou compressées |  
| Gris | `#AAAAAA` | Padding, zones réservées, octets non significatifs |

Cette convention est arbitraire — l'important est la **cohérence** au sein d'un même projet. Si vous travaillez en équipe, documentez votre palette dans un fichier `CONVENTIONS.md` à la racine du projet.

> 💡 **Contraste et lisibilité** : ImHex affiche le texte hexadécimal par-dessus la couleur de fond. Évitez les couleurs trop sombres qui rendent les octets illisibles, et les couleurs trop saturées qui fatiguent l'œil. Les pastels (valeurs hautes avec un peu de blanc) fonctionnent mieux pour un usage prolongé.

---

## Annotations dans les patterns : `[[comment]]` et `[[name]]`

Les bookmarks documentent des **régions** du fichier. Les attributs de pattern documentent des **champs** individuels. Les deux se complètent.

### `[[comment]]` pour le contexte technique

L'attribut `[[comment(...)]]` ajoute un texte explicatif visible au survol d'un champ dans l'arbre Pattern Data. Utilisez-le pour les informations techniques qui aident à interpréter la valeur :

```cpp
struct NetworkPacket {
    be u16 total_length  [[comment("Taille totale du paquet, header inclus, big-endian")]];
    u8     ttl           [[comment("Time To Live — décrémenté à chaque hop")]];
    u8     protocol      [[comment("6 = TCP, 17 = UDP, 1 = ICMP")]];
    be u32 src_addr      [[comment("Adresse IP source, big-endian")]];
    be u32 dst_addr      [[comment("Adresse IP destination, big-endian")]];
};
```

Chaque champ porte son explication. Un lecteur qui déplie cette structure dans Pattern Data comprend immédiatement la signification de chaque valeur sans devoir consulter une documentation externe.

### `[[name]]` pour la lisibilité

L'attribut `[[name(...)]]` remplace le nom technique de la variable par un libellé plus lisible dans l'arbre :

```cpp
u16 e_shstrndx [[name("Index de la section .shstrtab")]];
```

Dans l'arbre, au lieu de voir `e_shstrndx = 29`, vous voyez `Index de la section .shstrtab = 29`. C'est particulièrement utile quand les noms de variables suivent une convention de nommage technique (comme les noms de champs ELF) qui n'est pas immédiatement parlante.

### `[[format]]` pour les représentations alternatives

Nous l'avons déjà utilisé en section 6.4, mais rappelons-le dans ce contexte d'annotation : `[[format("hex")]]` affiche une valeur en hexadécimal plutôt qu'en décimal. C'est une forme d'annotation qui améliore la lisibilité des adresses, des offsets, des masques de bits et des magic numbers.

Quelques formats utiles :

```cpp
u32 address   [[format("hex")]];      // 0x00401000 au lieu de 4198400  
u32 perms     [[format("octal")]];    // 0755 au lieu de 493  
u8  flags     [[format("binary")]];   // 0b10110001 au lieu de 177  
```

---

## Combiner patterns et bookmarks : une stratégie à deux niveaux

En pratique, les patterns et les bookmarks ne s'utilisent pas de la même façon ni au même moment de l'analyse. Voici comment les articuler efficacement.

### Phase exploratoire : bookmarks d'abord

Quand vous ouvrez un binaire inconnu pour la première fois, vous ne savez pas encore quelles structures il contient. La phase exploratoire consiste à parcourir le fichier, repérer des zones intéressantes et les marquer. Les bookmarks sont l'outil de cette phase :

- Vous repérez un magic number à l'offset `0x00` → bookmark « Magic number / header ».  
- Vous trouvez une zone de chaînes ASCII à l'offset `0x3000` → bookmark « Table de chaînes ».  
- Vous observez une zone de haute entropie à l'offset `0x5000` → bookmark « Données chiffrées ? ».  
- Vous identifiez des séquences d'opcodes reconnaissables à l'offset `0x1000` → bookmark « Début du code ».

En quelques minutes, vous avez une **cartographie grossière** du fichier, matérialisée par des blocs colorés dans la vue hex et une liste navigable dans le panneau Bookmarks.

### Phase structurelle : patterns ensuite

Une fois les zones d'intérêt identifiées, vous commencez à écrire des patterns `.hexpat` pour les parser structurellement. Le pattern du header remplace progressivement le bookmark « Magic number / header » par une description champ par champ. Le pattern de la table de chaînes remplace le bookmark correspondant par un parsing qui affiche chaque chaîne individuellement.

Les bookmarks ne disparaissent pas pour autant. Ils évoluent : les bookmarks exploratoires deviennent des bookmarks de **documentation** qui capturent le raisonnement de haut niveau, tandis que les patterns prennent en charge la documentation structurelle de bas niveau.

### Le résultat : un fichier auto-documenté

À la fin de l'analyse, le fichier ImHex (sauvegardé comme projet) contient :

- Des **patterns** qui parsent et colorisent les structures de données identifiées.  
- Des **bookmarks** qui nomment les grandes régions fonctionnelles et documentent le raisonnement.  
- Des **commentaires** dans les patterns qui expliquent chaque champ.

Ensemble, ces trois couches forment une **documentation vivante** du binaire — bien plus riche qu'un rapport texte statique, car elle est interactive, navigable et vérifiable directement sur les octets.

---

## Projets ImHex : persister l'analyse entre les sessions

Toute cette documentation (bookmarks, patterns chargés, disposition des panneaux) serait perdue à la fermeture d'ImHex si elle n'était pas sauvegardée. C'est le rôle des **projets**.

### Sauvegarder un projet

**File → Save Project** (ou `Ctrl+Shift+S`) sauvegarde l'état complet de votre session dans un fichier `.hexproj`. Ce fichier contient :

- La référence au fichier binaire analysé (chemin).  
- Tous les bookmarks créés (noms, couleurs, régions, commentaires).  
- Le pattern `.hexpat` actuellement chargé dans le Pattern Editor.  
- La disposition des panneaux de l'interface.  
- Les données de la vue Diff si elle est active.

### Rouvrir un projet

**File → Open Project** restaure l'intégralité de l'état sauvegardé. Vous retrouvez vos bookmarks, votre pattern, votre disposition de fenêtres — exactement là où vous en étiez.

### Bonnes pratiques de nommage

Adoptez une convention de nommage pour vos projets. Une suggestion :

```
<binaire>_analysis_<date>.hexproj
```

Par exemple : `keygenme_O0_analysis_2025-03-15.hexproj`. Si vous travaillez sur plusieurs aspects du même binaire (structure, crypto, protocole), vous pouvez créer des projets distincts, chacun avec ses bookmarks et patterns spécialisés.

> 💡 **Versionnement** : Les fichiers `.hexproj` sont du texte structuré (JSON). Vous pouvez les versionner dans un dépôt Git aux côtés de vos patterns `.hexpat` et de vos scripts d'analyse. Le dossier `hexpat/` de notre formation est prévu pour cela.

---

## Exporter la cartographie

ImHex ne produit pas nativement un « rapport PDF » de votre analyse, mais vous pouvez **exporter** les informations de documentation de plusieurs façons.

**Captures d'écran annotées.** La vue hexadécimale colorisée avec les bookmarks visibles produit des captures visuelles très parlantes pour un rapport ou une présentation. Sous Linux, un outil comme `flameshot` ou simplement `Ctrl+PrintScreen` fait l'affaire.

**Export de la liste de bookmarks.** Le panneau Bookmarks ne dispose pas d'un bouton « Export » dédié, mais les données sont sauvegardées dans le fichier `.hexproj` (au format JSON) et peuvent être extraites par un script Python si vous avez besoin d'un tableau récapitulatif pour un rapport.

**Le pattern comme documentation.** Un fichier `.hexpat` bien commenté (avec des `[[comment]]`, des `[[name]]` et des commentaires `//`) est en soi une documentation technique du format analysé. Vous pouvez le partager indépendamment du projet ImHex — quiconque possède ImHex et le binaire peut évaluer votre pattern et retrouver votre analyse.

---

## Résumé

Les bookmarks, la colorisation et les annotations transforment ImHex d'un simple outil d'inspection en un **environnement d'analyse documentée**. Les bookmarks capturent le raisonnement de haut niveau et permettent une navigation par points d'intérêt. La colorisation — automatique via les patterns ou manuelle via les bookmarks — rend la structure du fichier visible d'un coup d'œil. Les attributs `[[comment]]`, `[[name]]` et `[[format]]` dans les patterns documentent chaque champ individuellement. La stratégie optimale combine les deux niveaux : bookmarks exploratoires d'abord pour cartographier le fichier, patterns structurels ensuite pour formaliser la compréhension. Le tout est persisté dans un projet `.hexproj` qui préserve l'intégralité de l'analyse entre les sessions et peut être versionné dans un dépôt Git.

---


⏭️ [Comparaison de deux versions d'un même binaire GCC (diff)](/06-imhex/07-comparaison-diff.md)
