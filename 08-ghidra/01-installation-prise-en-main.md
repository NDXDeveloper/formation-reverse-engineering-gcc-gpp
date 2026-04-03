🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 8.1 — Installation et prise en main de Ghidra

> **Chapitre 8 — Désassemblage avancé avec Ghidra**  
> **Partie II — Analyse Statique**

---

## Ghidra : contexte et philosophie

Ghidra a été développé en interne par la NSA pendant plus d'une décennie avant d'être rendu public en mars 2019 lors de la conférence RSA. Sa publication sous licence Apache 2.0 a été un événement majeur dans la communauté du reverse engineering : pour la première fois, un outil de calibre professionnel — intégrant un décompileur de qualité — devenait accessible gratuitement et en open source.

Avant Ghidra, le paysage se divisait schématiquement en deux camps. D'un côté, IDA Pro avec son décompileur Hex-Rays, considéré comme la référence industrielle, mais dont la licence coûte plusieurs milliers d'euros par an et par architecture. De l'autre, des outils gratuits comme Radare2, puissants mais dotés d'une courbe d'apprentissage abrupte et dépourvus de décompileur intégré comparable. Ghidra a comblé ce fossé en offrant un décompileur multi-architecture dans un framework open source, extensible par scripts et plugins.

Aujourd'hui, Ghidra est utilisé aussi bien par des chercheurs en sécurité, des analystes malware, des participants de CTF, que par des développeurs qui ont besoin de comprendre un binaire sans disposer du code source. Il est activement maintenu par la NSA sur GitHub, avec des contributions de la communauté.

> ⚠️ **Note sur l'origine NSA** — Le fait que Ghidra soit développé par la NSA suscite parfois des interrogations légitimes. Le code source est entièrement public et auditable sur GitHub (`NationalSecurityAgency/ghidra`). De nombreux chercheurs indépendants l'ont examiné sans y trouver de porte dérobée. Ghidra est un outil d'analyse, pas un outil d'exploitation : il lit des binaires, il n'en exécute pas. Cela dit, comme pour tout logiciel, il est recommandé de le télécharger exclusivement depuis les sources officielles.

---

## Prérequis système

### Java Development Kit (JDK)

Ghidra est écrit en Java (avec un décompileur natif en C++). Il nécessite un **JDK compatible** pour fonctionner — un simple JRE ne suffit pas.

Les versions de Ghidra récentes (11.x) requièrent **JDK 17 ou supérieur** (LTS recommandé : JDK 17 ou JDK 21). Les versions 21+ de JDK sont supportées à partir de Ghidra 11.0. Vérifiez toujours la page de téléchargement officielle pour connaître la version minimale requise par la version de Ghidra que vous installez.

Distributions JDK recommandées (toutes sont gratuites et fonctionnent avec Ghidra) :

- **Eclipse Temurin** (Adoptium) — distribution communautaire de référence, disponible sur `adoptium.net` ;  
- **Amazon Corretto** — distribution maintenue par Amazon ;  
- **Oracle JDK** — la distribution officielle Oracle, gratuite pour un usage personnel depuis la licence NFTC.

Vérifiez que Java est bien installé et accessible :

```bash
java -version
```

La sortie doit indiquer une version 17 ou supérieure. Si plusieurs versions de Java coexistent sur votre système, assurez-vous que la variable d'environnement `JAVA_HOME` pointe vers la bonne :

```bash
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
```

Sur les distributions Debian/Ubuntu, vous pouvez installer le JDK avec :

```bash
sudo apt update  
sudo apt install openjdk-17-jdk  
```

Sur Kali Linux, le JDK est généralement déjà présent. Vérifiez simplement la version.

### Ressources matérielles

Ghidra n'est pas un outil léger. Pour une utilisation confortable :

- **RAM** : 4 Go minimum, 8 Go recommandés. L'analyse de gros binaires C++ (avec templates instanciés, STL, etc.) peut consommer beaucoup de mémoire.  
- **Disque** : Ghidra lui-même pèse environ 500 Mo une fois décompressé. Chaque projet d'analyse crée une base de données locale qui peut atteindre plusieurs centaines de Mo pour un binaire complexe.  
- **CPU** : l'analyse automatique est gourmande en CPU lors de l'import initial. Un processeur multi-cœur accélère sensiblement cette phase.

---

## Téléchargement et installation

### Étape 1 — Télécharger Ghidra

Rendez-vous sur la page officielle des releases GitHub :

```
https://github.com/NationalSecurityAgency/ghidra/releases
```

Téléchargez l'archive `.zip` correspondant à la dernière version stable. Le nom du fichier suit le format `ghidra_VERSION_PUBLIC_DATE.zip` (par exemple `ghidra_11.3_PUBLIC_20250108.zip`). Ne téléchargez jamais Ghidra depuis un site tiers — seul le dépôt GitHub officiel de la NSA garantit l'intégrité du logiciel.

> ⚠️ **Vérifiez la version courante** sur la [page des releases GitHub](https://github.com/NationalSecurityAgency/ghidra/releases) — les numéros de version et les dates évoluent régulièrement.

> 💡 **Vérification d'intégrité** — Chaque release fournit des checksums SHA-256. Prenez l'habitude de les vérifier :  
> ```bash  
> sha256sum ghidra_*_PUBLIC_*.zip  
> ```  
> Comparez le hash obtenu avec celui publié sur la page de release.

### Étape 2 — Extraire l'archive

Ghidra ne nécessite pas d'installateur. Il suffit de décompresser l'archive dans un répertoire de votre choix :

```bash
cd /opt  
sudo unzip ~/Downloads/ghidra_*_PUBLIC_*.zip  
sudo ln -sf /opt/ghidra_*_PUBLIC /opt/ghidra    # symlink pour un accès stable  
```

> 💡 Le chapitre 4 (section 4.2) crée ce symlink `/opt/ghidra`. Tous les exemples de ce chapitre et du reste de la formation utilisent `/opt/ghidra/` comme chemin.

Le répertoire `/opt` est un choix courant pour les outils tiers sous Linux, mais vous pouvez installer Ghidra où vous le souhaitez (`~/tools/ghidra`, `/usr/local/share/ghidra`, etc.). L'essentiel est de choisir un emplacement stable que vous ne déplacerez pas, car Ghidra enregistre des chemins absolus dans ses projets.

La structure du répertoire décompressé ressemble à ceci :

```
ghidra_VERSION_PUBLIC/
├── ghidraRun                  ← Script de lancement principal (Linux/macOS)
├── ghidraRun.bat              ← Script de lancement (Windows)
├── support/
│   ├── analyzeHeadless        ← Lancement en mode headless (section 8.9)
│   ├── launch.properties      ← Configuration mémoire JVM
│   └── ...
├── Ghidra/
│   ├── Features/              ← Modules d'analyse (processeurs, formats, etc.)
│   ├── Processors/            ← Définitions d'architectures
│   └── Extensions/            ← Extensions installables
├── docs/                      ← Documentation intégrée
├── server/                    ← Ghidra Server (collaboration multi-utilisateurs)
└── LICENSE
```

### Étape 3 — Configurer la mémoire JVM

Par défaut, Ghidra alloue une quantité de mémoire modeste à la JVM. Pour l'analyse de binaires C++ conséquents, il est recommandé d'augmenter cette allocation. Éditez le fichier `support/launch.properties` :

```bash
nano /opt/ghidra/support/launch.properties
```

Recherchez la ligne `MAXMEM` et ajustez-la selon votre RAM disponible :

```properties
MAXMEM=4G
```

Une valeur de 4 Go convient pour la plupart des binaires que nous analyserons dans ce tutoriel. Si vous travaillez sur des binaires très volumineux (plusieurs dizaines de Mo) ou des projets contenant de nombreux fichiers, envisagez 8 Go.

### Étape 4 — Créer un alias ou un lanceur

Pour un accès rapide, ajoutez un alias dans votre fichier `~/.bashrc` ou `~/.zshrc` :

```bash
alias ghidra='/opt/ghidra/ghidraRun'
```

Rechargez le fichier :

```bash
source ~/.bashrc
```

Vous pourrez désormais lancer Ghidra simplement en tapant `ghidra` dans un terminal.

> 💡 **Alternative : fichier `.desktop`** — Si vous préférez un lanceur graphique, créez un fichier `~/.local/share/applications/ghidra.desktop` avec les champs `Exec`, `Icon` (un icône est fourni dans `docs/images/`) et `Name`.

---

## Premier lancement

Lancez Ghidra :

```bash
ghidra
```

ou directement :

```bash
/opt/ghidra/ghidraRun
```

### Acceptation de la licence

Au tout premier lancement, Ghidra affiche la licence Apache 2.0. Lisez-la et acceptez-la pour continuer. Cette étape ne se produit qu'une seule fois.

### La fenêtre Project Manager

L'interface qui s'ouvre n'est **pas** l'environnement d'analyse — c'est le **Project Manager** (gestionnaire de projets). C'est le point d'entrée de Ghidra, celui depuis lequel vous créez des projets, importez des binaires et lancez les différents outils.

Le Project Manager se compose de :

- **La barre de menu** — accès aux fonctions de création de projet, d'import, de configuration et d'aide.  
- **L'arbre de fichiers du projet actif** — une fois un projet ouvert, les binaires importés apparaissent ici sous forme d'arborescence.  
- **Le panneau « Tool Chest »** — les icônes des outils disponibles. Le plus important est le **CodeBrowser** (icône de dragon vert), que nous utiliserons presque exclusivement.

### Tip : mise à jour de Ghidra

Ghidra ne dispose pas de mécanisme de mise à jour automatique. Pour mettre à jour, téléchargez la nouvelle version, décompressez-la dans un nouveau répertoire et recréez votre alias. Vos projets existants restent compatibles : Ghidra sait migrer les bases de données de projets vers un format plus récent lors de l'ouverture. En revanche, cette migration est irréversible — une fois un projet ouvert dans une version plus récente, il ne pourra plus être ouvert dans une version antérieure.

---

## Création d'un projet

Ghidra organise le travail en **projets**. Un projet est un conteneur qui regroupe un ou plusieurs binaires analysés, avec toutes les annotations, les types personnalisés, les commentaires et les renommages que vous y avez apportés. Chaque projet correspond à un répertoire sur le disque contenant une base de données propriétaire.

Il existe deux types de projets :

- **Non-Shared Project** — projet local, stocké uniquement sur votre machine. C'est le type que nous utiliserons dans ce tutoriel.  
- **Shared Project** — projet hébergé sur un Ghidra Server, permettant à plusieurs analystes de travailler simultanément sur les mêmes binaires. Utile en contexte professionnel, mais hors du périmètre de ce chapitre.

### Créer un projet local

1. Dans le Project Manager, cliquez sur **File → New Project…**  
2. Sélectionnez **Non-Shared Project**, puis cliquez sur **Next**.  
3. Choisissez un **répertoire** pour stocker le projet. Créez un dossier dédié, par exemple `~/ghidra-projects/`.  
4. Donnez un **nom** au projet. Pour ce tutoriel, nommez-le `formation-re` ou `chapitre-08`.  
5. Cliquez sur **Finish**.

Le projet est créé. L'arbre de fichiers est vide — il est temps d'importer un premier binaire.

> 💡 **Convention de nommage** — Adoptez dès le départ une convention de nommage pour vos projets. Par exemple, un projet par chapitre (`ch08-ghidra`, `ch21-keygenme`…) ou un projet unique `formation-re` avec des dossiers internes pour organiser les binaires. Ghidra permet de créer des dossiers dans l'arborescence d'un projet via clic droit → **New Folder**.

---

## Comprendre la structure d'un projet sur disque

Lorsque vous créez un projet nommé `formation-re` dans `~/ghidra-projects/`, Ghidra crée deux éléments :

```
~/ghidra-projects/
├── formation-re.gpr          ← Fichier projet (pointeur, métadonnées)
└── formation-re.rep/         ← Répertoire de la base de données
    ├── project.prp
    ├── idata/                 ← Données d'index
    ├── user/                  ← Préférences utilisateur pour ce projet
    └── ...
```

Le fichier `.gpr` est le point d'entrée : c'est lui que vous ouvrez pour retrouver votre projet. Le répertoire `.rep` contient toutes les données d'analyse. Ces fichiers ne sont pas destinés à être édités manuellement.

Pour sauvegarder ou partager un projet, il suffit de copier le fichier `.gpr` et le répertoire `.rep` associé. Vous pouvez aussi utiliser **File → Archive Current Project…** pour créer une archive `.gar` portable.

---

## Import d'un premier binaire (aperçu rapide)

Pour vérifier que votre installation fonctionne correctement, importons un binaire simple. Nous détaillerons le processus d'import et ses options dans la section 8.2 — ici, l'objectif est simplement de valider l'environnement.

1. Depuis le Project Manager, cliquez sur **File → Import File…** (ou glissez-déposez un fichier directement dans l'arbre du projet).  
2. Sélectionnez le binaire `keygenme_O0` depuis `binaries/ch08-keygenme/`.  
3. Ghidra affiche une boîte de dialogue **Import** :  
   - **Format** : Ghidra détecte automatiquement le format. Pour un binaire ELF Linux, il affiche `Executable and Linking Format (ELF)`.  
   - **Language** : Ghidra détecte l'architecture. Pour un binaire x86-64, il propose `x86:LE:64:default` (x86, Little Endian, 64 bits).  
   - Si ces valeurs sont correctes — et elles le seront dans l'immense majorité des cas avec des binaires GCC — cliquez sur **OK**.  
4. Une boîte de dialogue de résumé s'affiche avec les détails de l'import. Cliquez sur **OK**.  
5. Ghidra propose de lancer l'**analyse automatique**. Cliquez sur **Yes** et acceptez les options par défaut pour l'instant (nous les détaillerons en 8.2).

L'analyse prend quelques secondes pour un petit binaire. Une fois terminée, le **CodeBrowser** s'ouvre avec le binaire chargé. Vous devriez voir le listing assembleur, le panneau du décompileur et l'arbre des symboles.

Si tout s'affiche correctement, votre installation est fonctionnelle.

---

## Tour d'horizon de l'interface (vue d'ensemble)

Le CodeBrowser est l'espace de travail principal de Ghidra. Nous le détaillerons dans la section 8.3, mais voici une première orientation pour ne pas vous sentir perdu lors de l'ouverture.

L'interface se divise en plusieurs panneaux agençables par glisser-déposer :

- **Program Trees** (en haut à gauche) — affiche la structure du binaire sous forme d'arbre : segments, sections, fragments mémoire. Utile pour naviguer par section ELF.  
- **Symbol Tree** (en bas à gauche) — liste toutes les fonctions, les labels, les classes, les namespaces et les imports/exports détectés. C'est votre point d'entrée principal pour naviguer dans le binaire. Double-cliquez sur un nom de fonction pour y accéder directement.  
- **Listing** (au centre) — le désassemblage. C'est ici que vous lisez le code assembleur, adresse par adresse. Ce panneau est interactif : vous pouvez cliquer sur une instruction pour voir ses références, renommer des éléments, ajouter des commentaires.  
- **Decompiler** (à droite) — le pseudo-code C produit par le décompileur de Ghidra. Ce panneau se synchronise avec le Listing : cliquer sur une ligne de pseudo-code met en surbrillance les instructions assembleur correspondantes, et inversement.  
- **Console** (en bas) — affiche les messages d'analyse, les erreurs de scripts et les logs divers.  
- **Data Type Manager** (accessible via le menu **Window**) — le gestionnaire de types. C'est ici que vous définirez vos structures, enums et typedefs personnalisés.

> 💡 **Disposition personnalisable** — Tous les panneaux peuvent être déplacés, empilés en onglets, redimensionnés ou détachés dans des fenêtres flottantes. Si vous fermez accidentellement un panneau, retrouvez-le via le menu **Window**. Pour revenir à la disposition par défaut, utilisez **Window → Reset Window Layout**.

---

## Raccourcis clavier essentiels

Voici les raccourcis que vous utiliserez le plus fréquemment dès vos premières sessions. Inutile de tous les mémoriser maintenant — ils reviendront naturellement avec la pratique.

| Raccourci | Action |  
|---|---|  
| `G` | **Go To Address** — sauter à une adresse précise |  
| `L` | **Rename** — renommer la fonction, variable ou label sous le curseur |  
| `;` | **Set Comment** — ajouter un commentaire EOL (End Of Line) |  
| `Ctrl+;` | Ajouter un commentaire Pre (au-dessus de l'instruction) |  
| `T` | **Set Data Type** — changer le type d'une variable ou d'un paramètre |  
| `X` | **Show References** — afficher toutes les cross-references vers l'élément sous le curseur |  
| `Ctrl+Shift+F` | **Search for Strings** — rechercher des chaînes dans le binaire |  
| `F` | **Edit Function** — modifier la signature d'une fonction (nom, type de retour, paramètres) |  
| `Space` | Basculer entre la vue Listing et la vue Function Graph |  
| `Ctrl+E` | Exporter le programme (différents formats) |

---

## Ghidra et les mises à jour de sécurité

Ghidra embarque un serveur web local (pour sa fonctionnalité d'aide intégrée) et utilise des composants Java. Comme tout logiciel, il peut être affecté par des vulnérabilités. Quelques bonnes pratiques :

- **Gardez Ghidra à jour** en suivant les releases sur GitHub. Les notes de version mentionnent systématiquement les correctifs de sécurité.  
- **Gardez votre JDK à jour** — les vulnérabilités Java sont régulièrement corrigées dans les mises à jour trimestrielles.  
- **Ne lancez pas Ghidra en tant que root** — ce n'est ni nécessaire ni souhaitable. Ghidra n'a besoin que de droits de lecture sur les binaires à analyser et de droits d'écriture dans le répertoire de projet.  
- **N'ouvrez pas de binaires non fiables hors d'une VM isolée** — si Ghidra est un outil d'analyse statique (il ne lance pas le binaire), certains parseurs de formats pourraient théoriquement être exploités par un fichier malformé. Dans le cadre de l'analyse malware (Partie VI), travaillez toujours dans la VM sandboxée du Chapitre 26.

---

## Résumé

À ce stade, vous disposez d'une installation fonctionnelle de Ghidra avec un JDK compatible, un projet créé et un premier binaire importé. Vous avez une vision d'ensemble de l'interface — le Project Manager pour la gestion de projets et le CodeBrowser pour l'analyse — ainsi que les raccourcis clavier qui accéléreront votre travail quotidien.

La section suivante plonge dans le détail du processus d'import et des options d'analyse automatique, qui déterminent la qualité du résultat avant même que vous ne commenciez à lire le désassemblage.

---


⏭️ [Import d'un binaire ELF — analyse automatique et options](/08-ghidra/02-import-elf-analyse.md)
