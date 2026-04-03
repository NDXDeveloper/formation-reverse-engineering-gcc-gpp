🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 6 — ImHex : analyse hexadécimale avancée

> 📦 **Binaires utilisés dans ce chapitre** : `binaries/ch06-fileformat/`, ainsi que des ELF générés dans les chapitres précédents.  
> 📁 **Patterns ImHex** : `hexpat/elf_header.hexpat`, `hexpat/ch06_fileformat.hexpat`  
> 🧰 **Outil requis** : [ImHex](https://imhex.werwolv.net/) (open source, multiplateforme)

---

## Pourquoi ce chapitre ?

Au chapitre 5, nous avons posé les bases du triage binaire avec des outils en ligne de commande : `file`, `strings`, `xxd`, `readelf`, `objdump`. Ces utilitaires sont indispensables, mais ils partagent une limite commune — ils restent **textuels**. Face à un format de fichier inconnu, à une structure C enfouie dans un blob binaire ou à un header ELF qu'on veut décortiquer octet par octet, un terminal ne suffit plus. On a besoin de **voir** les données, de les **coloriser**, de les **annoter**, et surtout de les **parser structurellement** sans écrire un programme dédié.

C'est précisément le rôle d'un éditeur hexadécimal avancé. Et parmi les outils disponibles aujourd'hui, **ImHex** s'est imposé comme la référence open source dans la communauté reverse engineering. Développé par WerWolv, il va bien au-delà de l'affichage d'octets en colonnes : son langage de patterns `.hexpat`, son inspecteur de données intégré, sa capacité de diff, son support YARA et son désassembleur embarqué en font un véritable **environnement d'analyse binaire visuel**.

Ce chapitre est volontairement le plus long de la Partie II, car ImHex sera un outil transversal que nous utiliserons dans presque tous les cas pratiques à venir — du patching de binaires (chapitre 21) à l'analyse de protocoles réseau (chapitre 23), en passant par le reverse de formats de fichiers (chapitre 25) et l'analyse de malware (chapitres 27–29).

---

## Ce que vous allez apprendre

À l'issue de ce chapitre, vous serez capable de :

- Expliquer en quoi ImHex se distingue des éditeurs hexadécimaux classiques et dans quels contextes de RE il apporte une vraie valeur ajoutée.  
- Naviguer efficacement dans l'interface d'ImHex : Pattern Editor, Data Inspector, Bookmarks, vue Diff.  
- Écrire des patterns `.hexpat` pour parser et visualiser automatiquement des structures binaires — des types primitifs aux structures imbriquées avec pointeurs et tableaux dynamiques.  
- Construire un pattern complet pour le header ELF, en partant de zéro, afin de comprendre le format en profondeur.  
- Parser des structures C/C++ personnalisées directement dans un binaire, sans disposer du code source.  
- Coloriser, annoter et bookmarker des régions d'un fichier binaire pour documenter une analyse en cours.  
- Comparer deux versions d'un même binaire compilé avec GCC à l'aide de la vue Diff intégrée.  
- Rechercher des magic bytes, des chaînes encodées et des séquences d'opcodes spécifiques dans un fichier.  
- Utiliser le désassembleur intégré d'ImHex pour inspecter du code machine sans quitter l'éditeur.  
- Appliquer des règles YARA directement depuis ImHex, faisant le pont avec les techniques d'analyse malware.  
- Mener un cas pratique complet : cartographier un format de fichier propriétaire en combinant toutes les fonctionnalités vues.

---

## Prérequis pour ce chapitre

- **Chapitre 2** — Vous devez comprendre la structure d'un fichier ELF (sections, segments, headers) pour suivre la construction du pattern ELF en section 6.4.  
- **Chapitre 3** — Les notions de base en assembleur x86-64 seront utiles pour la section 6.9 sur le désassembleur intégré.  
- **Chapitre 4** — ImHex doit être installé dans votre environnement de travail. Si ce n'est pas le cas, reportez-vous à la section 4.2.  
- **Chapitre 5** — Le workflow de triage rapide vu en 5.7 sera notre point de départ avant de plonger dans ImHex.

Une familiarité avec la syntaxe C (types, `struct`, pointeurs) est nécessaire pour écrire des patterns `.hexpat`, dont la syntaxe s'en inspire directement.

---

## Plan du chapitre

- **6.1** — Pourquoi ImHex dépasse le simple hex editor  
- **6.2** — Installation et tour de l'interface (Pattern Editor, Data Inspector, Bookmarks, Diff)  
- **6.3** — Le langage de patterns `.hexpat` — syntaxe et types de base  
- **6.4** — Écrire un pattern pour visualiser un header ELF depuis zéro  
- **6.5** — Parser une structure C/C++ maison directement dans le binaire  
- **6.6** — Colorisation, annotations et bookmarks de régions binaires  
- **6.7** — Comparaison de deux versions d'un même binaire GCC (diff)  
- **6.8** — Recherche de magic bytes, chaînes encodées et séquences d'opcodes  
- **6.9** — Intégration avec le désassembleur intégré d'ImHex  
- **6.10** — Appliquer des règles YARA depuis ImHex (pont vers l'analyse malware)  
- **6.11** — Cas pratique : cartographier un format de fichier custom avec `.hexpat`  
- **🎯 Checkpoint** — Écrire un `.hexpat` complet pour le format `ch23-fileformat`

---

## Fil conducteur du chapitre

Le chapitre suit une progression en trois temps. Nous commençons par découvrir l'outil et son interface (sections 6.1–6.2), puis nous plongeons dans le cœur d'ImHex — le langage `.hexpat` — en construisant des patterns de complexité croissante (sections 6.3–6.5). Enfin, nous explorons les fonctionnalités complémentaires qui font d'ImHex un compagnon quotidien du reverse engineer : diff, recherche, désassemblage et YARA (sections 6.6–6.10). Le chapitre se conclut par un cas pratique intégrateur (section 6.11) et un checkpoint qui mobilise l'ensemble des compétences acquises.

Tout au long du chapitre, nous travaillerons sur des binaires ELF que vous avez déjà compilés dans les chapitres précédents, ainsi que sur le binaire `ch25-fileformat` qui utilise un format de fichier propriétaire. Les patterns `.hexpat` que nous écrirons ensemble seront réutilisables dans les chapitres ultérieurs — conservez-les dans le dossier `hexpat/` de votre dépôt.

> 💡 **Conseil** : Gardez ImHex ouvert en permanence pendant vos sessions de RE. Même quand vous travaillez principalement dans Ghidra ou GDB, pouvoir basculer rapidement vers une vue hexadécimale structurée est un réflexe qui vous fera gagner un temps considérable.

⏭️ [Pourquoi ImHex dépasse le simple hex editor](/06-imhex/01-pourquoi-imhex.md)
