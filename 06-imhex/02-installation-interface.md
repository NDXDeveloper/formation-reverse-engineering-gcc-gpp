🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.2 — Installation et tour de l'interface (Pattern Editor, Data Inspector, Bookmarks, Diff)

> 🎯 **Objectif de cette section** : Installer ImHex sur votre distribution Linux, ouvrir un premier binaire ELF, et identifier les panneaux principaux de l'interface afin de naviguer efficacement dans l'outil pour la suite du chapitre.

---

## Installation

### Depuis les packages officiels (méthode recommandée)

ImHex propose des builds pré-compilés pour les principales distributions Linux. C'est la méthode la plus simple et celle qui garantit une version à jour.

**Flatpak (universel)** — fonctionne sur toutes les distributions avec Flatpak installé :

```bash
flatpak install flathub net.werwolv.ImHex
```

Pour lancer ImHex ensuite :

```bash
flatpak run net.werwolv.ImHex
```

**Ubuntu / Debian** — un `.deb` est disponible sur la page des releases GitHub :

```bash
# Télécharger le .deb depuis https://github.com/WerWolv/ImHex/releases
# Puis installer :
sudo dpkg -i imhex-*.deb  
sudo apt-get install -f   # résoudre les dépendances manquantes si nécessaire  
```

**Arch Linux / Manjaro** :

```bash
# Depuis les dépôts communautaires
sudo pacman -S imhex
```

**AppImage** — alternative portable, sans installation système :

```bash
chmod +x ImHex-*.AppImage
./ImHex-*.AppImage
```

> 💡 **Quelle version choisir ?** Pour cette formation, toute version ≥ 1.33 convient. Les fonctionnalités que nous utilisons (patterns `.hexpat`, Data Inspector, Diff, YARA) sont stables depuis plusieurs releases. Si vous utilisez la version packagée de votre distribution et qu'elle est plus ancienne, préférez le Flatpak ou l'AppImage pour disposer d'une version récente.

### Compilation depuis les sources

Si vous souhaitez la toute dernière version ou contribuer au projet, la compilation depuis les sources est possible mais nécessite plusieurs dépendances (CMake ≥ 3.16, GCC ≥ 11 ou Clang ≥ 14, et une série de bibliothèques). La procédure détaillée est documentée dans le `README.md` du dépôt GitHub. Pour suivre cette formation, les packages pré-compilés sont largement suffisants.

### Vérification de l'installation

Lancez ImHex. Si l'application s'ouvre et affiche l'écran d'accueil avec les options **Open File**, **Open Project** et les liens vers la documentation, tout est en place. Vous pouvez aussi vérifier depuis le terminal :

```bash
imhex --version
```

> ⚠️ **Note Flatpak** : Si vous avez installé via Flatpak, la commande `imhex` n'est pas directement dans votre `PATH`. Utilisez `flatpak run net.werwolv.ImHex` ou créez un alias dans votre `.bashrc` :  
> ```bash  
> alias imhex='flatpak run net.werwolv.ImHex'  
> ```

### Installation du Content Store (patterns et plugins communautaires)

Au premier lancement, ImHex vous propose de télécharger le **Content Store** — une collection de patterns `.hexpat` pré-écrits pour des formats courants (ELF, PE, PNG, ZIP, JPEG, etc.), ainsi que des plugins et des magic files. Acceptez ce téléchargement : ces patterns serviront de références et d'exemples tout au long du chapitre.

Si vous avez refusé ou si le téléchargement a échoué, vous pouvez y accéder à tout moment via le menu **Help → Content Store**.

---

## Premier contact : ouvrir un binaire ELF

Avant de détailler chaque panneau de l'interface, ouvrons un fichier pour avoir quelque chose de concret sous les yeux. Utilisez l'un des binaires que vous avez compilés au chapitre 2 ou 4 — par exemple le `hello` compilé avec symboles :

```bash
# Si vous n'avez pas de binaire sous la main :
echo '#include <stdio.h>  
int main() { printf("Hello RE!\\n"); return 0; }' > /tmp/hello.c  
gcc -O0 -g -o /tmp/hello /tmp/hello.c  
```

Ouvrez-le dans ImHex :

- via le menu **File → Open File** et naviguez jusqu'au binaire, ou  
- directement depuis le terminal : `imhex /tmp/hello`

L'interface se remplit immédiatement. Vous voyez des colonnes hexadécimales, une représentation ASCII sur la droite, et plusieurs panneaux autour de la vue principale. C'est cette interface que nous allons maintenant explorer méthodiquement.

---

## Anatomie de l'interface

L'interface d'ImHex est organisée autour d'une **vue hexadécimale centrale** entourée de **panneaux auxiliaires** que l'on peut afficher, masquer, redimensionner et réorganiser librement. Chaque panneau a un rôle précis dans le workflow d'analyse. Passons-les en revue.

### La vue hexadécimale (Hex Editor)

C'est le cœur de l'interface, la vue que vous verrez en permanence. Elle affiche le contenu du fichier sous trois colonnes synchronisées :

- **Les offsets** (colonne de gauche) — l'adresse de chaque ligne dans le fichier, en hexadécimal. Par défaut, ImHex affiche 16 octets par ligne, donc les offsets s'incrémentent de `0x10` en `0x10`.  
- **Les valeurs hexadécimales** (colonnes centrales) — chaque octet représenté par deux chiffres hexadécimaux. Les octets sont regroupés par paires ou par blocs selon la configuration.  
- **La représentation ASCII** (colonne de droite) — chaque octet interprété comme un caractère ASCII. Les octets non imprimables sont affichés comme des points (`.`).

Quand vous cliquez sur un octet dans la vue hex, le curseur se positionne et tous les panneaux auxiliaires se mettent à jour pour refléter les données à cet offset. C'est un comportement fondamental : **le curseur est le pivot central** de toute l'interface.

**Navigation de base** :

- `Ctrl+G` — aller à un offset précis (Go to address). Vous pouvez saisir une adresse en hexadécimal (`0x1040`) ou en décimal.  
- `Ctrl+F` — rechercher une séquence d'octets, une chaîne ASCII ou une chaîne UTF-16.  
- Molette de souris ou barre de défilement — navigation linéaire dans le fichier.  
- `Ctrl+Z` / `Ctrl+Y` — annuler / rétablir les modifications (ImHex conserve un historique complet des éditions).

**Édition directe** : vous pouvez modifier les octets directement dans la vue hexadécimale en tapant de nouvelles valeurs. Les octets modifiés apparaissent dans une couleur différente (rouge par défaut) pour les distinguer des données originales. Cette fonctionnalité est essentielle pour le patching binaire que nous verrons au chapitre 21.

### Le Pattern Editor

Le Pattern Editor est le panneau qui distingue le plus ImHex de ses concurrents. Il se compose de deux parties : un **éditeur de code** (en haut) où vous écrivez vos patterns `.hexpat`, et une **vue de résultats** (en bas, souvent appelée « Pattern Data ») où ImHex affiche les structures parsées sous forme d'arbre.

Le workflow est le suivant : vous écrivez (ou chargez) un pattern `.hexpat` dans l'éditeur, vous cliquez sur le bouton **Évaluer** (icône ▶) ou appuyez sur `F5`, et ImHex parse le fichier selon votre description. Les structures apparaissent dans l'arbre de résultats, et les régions correspondantes sont colorisées dans la vue hexadécimale.

Par exemple, si votre pattern déclare une structure `Elf64_Ehdr` de 64 octets à l'offset 0, les 64 premiers octets du fichier seront colorisés et chaque champ sera nommé et affiché avec sa valeur interprétée dans l'arbre.

L'éditeur de code offre la coloration syntaxique, l'auto-complétion et le surlignage des erreurs. Si votre pattern contient une erreur de syntaxe, ImHex affiche un message clair avec le numéro de ligne. Nous explorerons le langage `.hexpat` en profondeur à partir de la section 6.3.

> 💡 **Astuce** : Le panneau Pattern Data peut être détaché de la fenêtre principale (clic droit sur l'onglet → **Detach**). Sur un setup multi-écrans, placer l'arbre de patterns sur un écran et la vue hex sur l'autre est un gain de confort considérable.

### Le Data Inspector

Le Data Inspector est le panneau qui répond à la question « qu'est-ce que cet octet signifie ? » sous toutes ses formes possibles. Il prend les octets situés sous le curseur et les interprète simultanément comme tous les types de données courants.

Voici ce qu'affiche le Data Inspector quand le curseur se trouve sur une séquence d'octets :

- **Entiers** : `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`, `uint64_t`, `int64_t` — en little-endian et en big-endian.  
- **Flottants** : `float` (32 bits IEEE 754), `double` (64 bits IEEE 754).  
- **Booléen** : interprétation comme `true` / `false`.  
- **Caractère** : ASCII, UTF-8, wide char.  
- **Timestamp** : interprétation comme un timestamp Unix 32 bits et 64 bits (date et heure lisibles).  
- **Couleur** : RGBA 32 bits (utile pour les formats graphiques).  
- **Adresse** : interprétation comme pointeur 32 bits ou 64 bits.

Ce panneau est particulièrement précieux dans la phase exploratoire, quand vous ne savez pas encore quel type de données se trouve à un offset donné. Plutôt que de deviner et de convertir manuellement, vous balayez le fichier avec le curseur et le Data Inspector vous montre toutes les interprétations en temps réel. Quand une valeur « fait sens » — un timestamp qui tombe sur une date plausible, un entier qui correspond à une taille de section, un float qui ressemble à une coordonnée — vous avez un indice fort sur le type du champ.

> 💡 **Personnalisation** : Vous pouvez activer ou désactiver individuellement chaque type dans le Data Inspector via le menu contextuel. Si vous travaillez exclusivement sur des structures à entiers, masquer les flottants et les timestamps réduit le bruit visuel.

### Les Bookmarks

Le panneau Bookmarks permet de créer des **marque-pages nommés et colorisés** sur des régions du fichier. Chaque bookmark associe une plage d'offsets (début + taille) à un nom, une couleur et un commentaire libre.

Pour créer un bookmark : sélectionnez une plage d'octets dans la vue hexadécimale (clic + glisser), puis clic droit → **Create Bookmark**, ou utilisez le raccourci `Ctrl+B`. Une boîte de dialogue vous demande un nom et un commentaire. La région est immédiatement surlignée dans la vue hex avec la couleur choisie.

Les bookmarks servent à **documenter votre analyse au fil de l'eau**. Quand vous identifiez qu'une plage d'octets correspond au header, qu'une autre contient une table de chaînes, qu'une troisième semble être une clé de chiffrement, vous les bookmarkez immédiatement. Cela crée une cartographie progressive du fichier qui persiste entre les sessions si vous sauvegardez un projet ImHex.

Le panneau Bookmarks liste tous les marque-pages avec leurs offsets, tailles et commentaires. Un clic sur un bookmark dans la liste fait sauter la vue hexadécimale à l'offset correspondant — c'est un moyen efficace de naviguer dans un fichier volumineux par points d'intérêt plutôt que par adresses numériques.

### La vue Diff

La vue Diff permet de comparer **deux fichiers ouverts** côte à côte avec mise en évidence des différences. Pour l'utiliser, ouvrez deux fichiers dans des onglets séparés (ImHex supporte les onglets multiples), puis activez la vue Diff via **View → Diff**.

ImHex affiche les deux fichiers en colonnes parallèles avec un code couleur : les octets identiques sont sur fond neutre, les octets différents sont surlignés. Le défilement est synchronisé entre les deux vues — quand vous scrollez dans un fichier, l'autre suit. Des boutons de navigation permettent de sauter à la différence précédente ou suivante.

Cette fonctionnalité est utile dans plusieurs scénarios RE que nous rencontrerons dans la formation :

- Comparer un binaire compilé avec `-O0` et le même avec `-O2` pour voir l'impact des optimisations sur le code machine (en complément du diff au niveau désassemblage vu au chapitre 7).  
- Comparer un binaire avant et après stripping (`strip`) pour visualiser les sections supprimées.  
- Comparer un binaire original avec un binaire patché pour vérifier que votre patch ne touche que les octets voulus (chapitre 21).  
- Comparer deux versions d'un binaire pour localiser les modifications introduites par un correctif de sécurité (chapitre 10).

---

## Autres panneaux et vues utiles

Au-delà des cinq panneaux principaux décrits ci-dessus, ImHex propose plusieurs vues complémentaires que nous utiliserons ponctuellement dans les sections suivantes.

### Information (File → Information)

Ce panneau affiche des **statistiques globales** sur le fichier ouvert : taille, entropie par bloc, distribution des valeurs d'octets (histogramme), type détecté. L'**analyse d'entropie** est particulièrement utile en RE : une zone de haute entropie (proche de 8 bits/octet) dans un binaire suggère des données compressées ou chiffrées, tandis qu'une zone de faible entropie suggère du texte, des zéros de padding ou des données structurées régulières. Nous exploiterons cette analyse au chapitre 29 pour détecter des packers.

### Hashes

ImHex peut calculer des **empreintes cryptographiques** (MD5, SHA-1, SHA-256, CRC32…) sur tout le fichier ou sur une sélection. Cela évite de quitter l'éditeur pour lancer `sha256sum` quand vous avez besoin de vérifier l'intégrité d'un sample ou de documenter un IOC (Indicator of Compromise) dans un rapport d'analyse malware.

### Strings

Similaire à la commande `strings` en CLI mais avec une interface graphique. ImHex scanne le fichier à la recherche de séquences de caractères imprimables et les affiche avec leurs offsets. Un clic sur une chaîne fait sauter la vue hex à l'offset correspondant. ImHex supporte les recherches en ASCII et en UTF-16 (utile pour les binaires Windows compilés avec MinGW).

### Désassembleur et YARA

ImHex intègre un **désassembleur** (basé sur la bibliothèque Capstone) et un **moteur YARA** directement accessibles depuis l'interface. Nous leur consacrerons des sections dédiées (6.9 et 6.10) plus loin dans ce chapitre.

---

## Organisation de l'espace de travail

L'interface d'ImHex est entièrement modulaire. Vous pouvez réorganiser les panneaux par glisser-déposer, les empiler en onglets, les détacher dans des fenêtres flottantes, ou les masquer complètement via le menu **View**.

Voici une disposition de travail efficace pour le reverse engineering, que nous vous recommandons comme point de départ :

- **Centre** : la vue hexadécimale en grand, c'est elle qui occupe le plus d'espace.  
- **Droite** : le Data Inspector, toujours visible, pour interpréter en temps réel les octets sous le curseur.  
- **Bas** : le Pattern Editor (éditeur de code + arbre de résultats), que vous agrandirez quand vous travaillez sur un `.hexpat`.  
- **Gauche** : les Bookmarks, pour naviguer entre vos annotations.

Cette disposition n'est qu'une suggestion — adaptez-la à votre écran et à votre workflow. L'important est que le Data Inspector reste toujours visible (il est utilisé en permanence) et que la vue hexadécimale dispose de suffisamment d'espace pour afficher un nombre confortable de colonnes.

> 💡 **Projets ImHex** : Si vous travaillez sur un fichier de manière prolongée (ce qui sera le cas dans les chapitres de cas pratiques), sauvegardez votre travail comme un **projet** via **File → Save Project**. Un projet ImHex conserve le fichier ouvert, vos bookmarks, vos patterns chargés et la disposition de vos panneaux. Cela vous permet de reprendre exactement là où vous en étiez.

---

## Résumé

ImHex s'installe en quelques secondes via Flatpak, `.deb` ou AppImage. Son interface s'organise autour de la vue hexadécimale centrale, enrichie par le Pattern Editor (parsing structurel), le Data Inspector (interprétation multi-types en temps réel), les Bookmarks (documentation intégrée) et la vue Diff (comparaison visuelle). Ces panneaux forment un ensemble cohérent où chaque clic dans la vue hex met à jour tous les panneaux simultanément — le curseur comme pivot central de l'analyse. Avant de plonger dans le langage `.hexpat` en section 6.3, prenez quelques minutes pour ouvrir différents binaires de votre dossier `binaries/` et explorer librement l'interface : déplacez le curseur, observez le Data Inspector, créez un ou deux bookmarks, essayez la vue Diff entre deux versions d'un même binaire.

---


⏭️ [Le langage de patterns `.hexpat` — syntaxe et types de base](/06-imhex/03-langage-hexpat.md)
