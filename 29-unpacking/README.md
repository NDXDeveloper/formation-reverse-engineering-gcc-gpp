🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 29 — Détection de packing, unpack et reconstruction

> 📦 **Objectif** — Apprendre à reconnaître un binaire ELF packé, à le décompresser (statiquement ou dynamiquement), puis à reconstruire un exécutable analysable par les outils de RE classiques (Ghidra, objdump, radare2…).

---

## Pourquoi ce chapitre ?

Jusqu'ici dans la formation, les binaires analysés étaient soit fournis tels quels, soit protégés par des mécanismes que l'on pouvait contourner directement : stripping de symboles, obfuscation de flux de contrôle, détection de débogueur. Le **packing** représente un obstacle d'une nature différente : le binaire que vous ouvrez dans votre désassembleur n'est pas le « vrai » programme. Le code original est compressé — parfois chiffré — à l'intérieur du fichier, et un petit stub de décompression se charge de le restaurer en mémoire au moment de l'exécution. Tant que vous n'avez pas récupéré ce code décompressé, toute tentative d'analyse statique est vouée à l'échec : Ghidra affiche du bruit, `strings` ne renvoie presque rien d'utile, et le graphe de flux de contrôle ne ressemble à rien de cohérent.

Le packing est omniprésent dans l'analyse de code malveillant. La grande majorité des malwares ELF distribués dans la nature utilisent au minimum UPX, et les échantillons plus sophistiqués embarquent des routines de décompression sur mesure, parfois chaînées en plusieurs couches. Savoir détecter et défaire ces protections est donc une compétence indispensable pour quiconque veut aller au-delà du reverse de binaires « coopératifs ».

Ce chapitre fait le pont entre les techniques anti-reversing vues au chapitre 19 (où l'on a étudié le packing du point de vue du défenseur) et l'analyse de malware des chapitres 27–28 (où les binaires rencontrés sont activement protégés). On y combinera des outils déjà maîtrisés — `checksec`, ImHex, GDB, `readelf` — avec des concepts nouveaux : analyse d'entropie, dump mémoire d'un processus en cours d'exécution, et reconstruction manuelle d'un ELF fonctionnel.

---

## Qu'est-ce que le packing, concrètement ?

Un **packer** est un outil qui transforme un exécutable en un nouvel exécutable contenant une version compressée (ou chiffrée) du code original, précédée d'un **stub de décompression**. Au lancement, le stub s'exécute en premier : il décompresse le code original en mémoire, ajuste les relocations et les imports si nécessaire, puis transfère le contrôle au point d'entrée réel du programme. Du point de vue de l'utilisateur final, le comportement est identique ; du point de vue de l'analyste, le fichier sur disque ne contient que le stub et une masse de données compressées illisibles.

Le packing répond à deux motivations distinctes. La première est la **réduction de taille** : c'est la raison d'être historique d'UPX (Ultimate Packer for eXecutables), créé à une époque où la bande passante et l'espace disque étaient des ressources précieuses. La seconde est l'**évasion d'analyse** : en dissimulant le code réel derrière une couche de compression ou de chiffrement, le packer empêche l'extraction directe de chaînes de caractères, de signatures YARA, et de tout autre indicateur statique. C'est cette seconde motivation qui domine aujourd'hui dans le contexte de l'analyse de malware.

Il est important de distinguer le packing d'autres formes de protection. Le **stripping** supprime les symboles mais laisse le code machine intact et analysable. L'**obfuscation de flux de contrôle** rend le code plus difficile à lire, mais il reste visible dans le désassembleur. Le packing, lui, rend le code purement et simplement **absent** du fichier sur disque — il n'existe sous forme exécutable qu'en mémoire, pendant l'exécution. C'est pourquoi l'approche de décompression repose souvent sur une combinaison d'analyse statique (pour comprendre le stub) et d'analyse dynamique (pour capturer le code une fois décompressé).

---

## Panorama des packers que l'on rencontrera

Le packer le plus courant sur les binaires ELF reste **UPX**. Son format est bien documenté, il laisse des signatures reconnaissables (`UPX!` dans les headers de section, noms de sections caractéristiques comme `UPX0` / `UPX1`), et il fournit lui-même une option de décompression (`upx -d`). C'est le cas « facile » — mais c'est aussi le point de départ indispensable pour comprendre le mécanisme général.

Au-delà d'UPX, il existe des packers qui modifient volontairement les headers UPX pour empêcher la décompression automatique, des packers open source comme **Ezuri** (spécifiquement conçu pour les binaires ELF, répandu dans les botnets IoT), et des **packers custom** écrits à la main par les auteurs de malware. Ces derniers sont les plus difficiles à traiter : aucun outil automatique ne les reconnaît, et l'unpacking nécessite une compréhension fine du stub de décompression.

Dans ce chapitre, on travaillera principalement avec les binaires du répertoire `ch29-packed/` fourni dans le dépôt, qui utilisent UPX dans un premier temps, puis une variante modifiée qui résiste à `upx -d`, obligeant à passer par une approche dynamique.

---

## Stratégie générale d'unpacking

Quel que soit le packer rencontré, la démarche suit un schéma en quatre étapes que l'on détaillera dans les sections suivantes :

**Étape 1 — Détection.** Avant de tenter quoi que ce soit, il faut confirmer que le binaire est packé et, si possible, identifier le packer utilisé. On s'appuiera sur plusieurs indices convergents : l'analyse d'entropie (un binaire packé présente une entropie proche de 8.0 sur les sections de données compressées), les signatures dans les headers et les noms de sections, le ratio entre taille sur disque et taille des segments en mémoire, et le comportement de `checksec` / `readelf`.

**Étape 2 — Unpacking.** Deux approches sont possibles. L'**unpacking statique** consiste à utiliser l'outil du packer lui-même (comme `upx -d`) ou à écrire un script qui reproduit l'algorithme de décompression. L'**unpacking dynamique** consiste à laisser le stub faire son travail, puis à capturer l'état de la mémoire du processus une fois le code décompressé — typiquement en posant un breakpoint juste après le transfert de contrôle vers le code original, puis en dumpant les segments pertinents avec GDB.

**Étape 3 — Reconstruction.** Le dump mémoire brut n'est pas directement un fichier ELF valide. Il faut reconstruire les headers ELF (ou les corriger), rétablir les sections, fixer le point d'entrée, et s'assurer que les imports dynamiques sont correctement référencés. Cette étape est souvent la plus délicate, surtout avec des packers custom.

**Étape 4 — Réanalyse.** Une fois l'ELF reconstruit, on peut enfin l'importer dans Ghidra, le passer dans `strings`, appliquer des règles YARA, et procéder à une analyse classique comme on l'a fait dans les chapitres précédents.

---

## Prérequis pour ce chapitre

Ce chapitre mobilise des compétences et des outils vus dans plusieurs chapitres antérieurs. Assurez-vous d'être à l'aise avec les éléments suivants avant de poursuivre :

- **Structure d'un fichier ELF** — headers, sections, segments, point d'entrée (chapitre 2, sections 2.3–2.4).  
- **`readelf`, `objdump`, `checksec`, `file`, `strings`** — le workflow de triage rapide (chapitre 5).  
- **ImHex** — navigation hexadécimale, patterns `.hexpat`, analyse de magic bytes (chapitre 6).  
- **GDB avec GEF ou pwndbg** — pose de breakpoints, inspection mémoire, commandes `vmmap` et `dump memory` (chapitres 11–12).  
- **Concepts anti-reversing** — stripping, packing UPX, protections binaires (chapitre 19, sections 19.1–19.2).

Le binaire d'entraînement `ch29-packed/` est compilable via `make` dans le répertoire `binaries/ch29-packed/`. Le `Makefile` produit plusieurs variantes : une version packée avec UPX standard, une version avec headers UPX altérés, et le binaire original non packé (pour vérification).

---

## Plan du chapitre

- **29.1** — Identifier UPX et packers custom avec `checksec` + ImHex + entropie  
- **29.2** — Unpacking statique (UPX) et dynamique (dump mémoire avec GDB)  
- **29.3** — Reconstruire l'ELF original : fixer les headers, sections et entry point  
- **29.4** — Réanalyser le binaire unpacké  
- **🎯 Checkpoint** — Unpacker `ch29-packed`, reconstruire l'ELF et retrouver la logique originale

---

> ⚠️ **Rappel de sécurité** — Comme pour tous les binaires de la Partie VI, travaillez exclusivement dans votre VM sandboxée (chapitre 26). Même si les samples de cette formation sont pédagogiques et inoffensifs, adopter systématiquement les bonnes pratiques d'isolation est une habitude essentielle. Ne prenez jamais le raccourci d'exécuter un binaire packé directement sur votre machine hôte.

⏭️ [Identifier UPX et packers custom avec `checksec` + ImHex + entropie](/29-unpacking/01-identifier-packers.md)
