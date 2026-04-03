🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 27 — Analyse d'un ransomware Linux ELF (auto-compilé GCC)

> ⚠️ **Avertissement — Environnement contrôlé obligatoire**  
>  
> Le binaire étudié dans ce chapitre est un **sample pédagogique créé par nos soins**, compilé avec GCC, dont le code source est fourni et auditable. Il ne s'agit en aucun cas d'un malware réel. Néanmoins, son comportement — chiffrer des fichiers sur disque — est destructeur par nature.  
>  
> **Ne l'exécutez jamais en dehors de la VM sandboxée configurée au [Chapitre 26](/26-lab-securise/README.md).** Restaurez systématiquement un snapshot propre après chaque exécution.

---

## Pourquoi analyser un ransomware ?

Le ransomware est l'une des menaces les plus répandues et les plus coûteuses du paysage cyber actuel. Pour un analyste en reverse engineering, savoir disséquer un échantillon de ransomware, c'est être capable de répondre à trois questions critiques lors d'un incident :

1. **Quel algorithme de chiffrement est utilisé, et comment ?** — Identifier la routine crypto permet de déterminer si un déchiffrement est envisageable sans payer de rançon.  
2. **La clé est-elle récupérable ?** — Une clé hardcodée, un générateur de nombres aléatoires mal initialisé ou une clé transmise en clair sur le réseau sont autant de failles exploitables.  
3. **Quel est le périmètre d'impact ?** — Quels répertoires sont ciblés, quelles extensions, quels volumes ? Cette information est essentielle pour évaluer les dégâts et guider la réponse à incident.

Ce chapitre vous place dans la peau d'un analyste qui reçoit un binaire ELF suspect et doit produire un rapport complet ainsi qu'un outil de déchiffrement, en mobilisant l'ensemble des techniques acquises depuis le début de cette formation.

---

## Présentation du sample `ch27-ransomware`

Le binaire que nous allons étudier est volontairement réaliste dans sa logique tout en restant simple dans son implémentation, afin de rester accessible pédagogiquement. Voici ses caractéristiques générales (que vous découvrirez par vous-même au fil de l'analyse) :

- **Langage** : C, compilé avec GCC.  
- **Cible** : fichiers situés dans `/tmp/test/` uniquement (périmètre limité pour la sécurité du lab).  
- **Algorithme** : chiffrement symétrique AES (la variante exacte — mode, taille de clé — fait partie de l'analyse).  
- **Gestion de la clé** : la clé est hardcodée dans le binaire (volontairement, pour permettre sa récupération).  
- **Comportement post-chiffrement** : les fichiers originaux sont remplacés par leur version chiffrée, avec une extension ajoutée.

Le code source (`binaries/ch27-ransomware/ransomware_sample.c`) et le `Makefile` associé sont fournis dans le dépôt. Plusieurs variantes du binaire sont disponibles :

| Variante | Optimisation | Symboles | Fichier |  
|---|---|---|---|  
| Debug | `-O0` | Oui (`-g`) | `ransomware_O0` |  
| Optimisé | `-O2` | Oui | `ransomware_O2` |  
| Strippé | `-O2` | Non (`strip`) | `ransomware_O2_strip` |

Nous recommandons de commencer l'analyse sur la variante debug (`ransomware_O0`) pour établir une compréhension de référence, puis de la confronter à la variante strippée pour pratiquer l'analyse sans filet.

---

## Ce que ce chapitre mobilise

Ce chapitre est conçu comme un exercice d'intégration. Il fait appel aux compétences et outils des parties précédentes :

- **Triage rapide** ([Chapitre 5](/05-outils-inspection-base/README.md)) — `file`, `strings`, `checksec` pour les premières hypothèses.  
- **Analyse hexadécimale** ([Chapitre 6](/06-imhex/README.md)) — ImHex pour repérer les constantes cryptographiques et visualiser les structures internes.  
- **Désassemblage et décompilation** ([Chapitres 7–9](/07-objdump-binutils/README.md)) — Ghidra pour reconstruire le flux de chiffrement.  
- **Détection de patterns crypto** ([Chapitre 24](/24-crypto/README.md)) — identification des constantes magiques (S-box AES, IV…) et des routines associées.  
- **Règles YARA** ([Chapitre 35](/35-automatisation-scripting/README.md)) — écriture de signatures pour détecter ce type de sample dans une collection de binaires.  
- **Analyse dynamique avec GDB et Frida** ([Chapitres 11–13](/11-gdb/README.md)) — extraction de la clé et de l'IV en mémoire à l'exécution.  
- **Scripting Python** — écriture d'un déchiffreur fonctionnel.  
- **Rédaction de rapport** — production d'un livrable structuré incluant les IOC (Indicators of Compromise), le comportement observé et les recommandations.

---

## Méthodologie suivie dans ce chapitre

L'analyse se déroule en sept étapes séquentielles, chacune correspondant à une section du chapitre :

```
27.1  Conception du sample
 │    Comprendre ce qu'on analyse et pourquoi il est construit ainsi.
 ▼
27.2  Triage rapide
 │    file, strings, checksec → premières hypothèses.
 ▼
27.3  Analyse statique (Ghidra + ImHex)
 │    Repérer les constantes AES, reconstruire le flux de chiffrement.
 ▼
27.4  Règles YARA
 │    Écrire des signatures de détection depuis ImHex.
 ▼
27.5  Analyse dynamique (GDB + Frida)
 │    Confirmer les hypothèses, extraire la clé en mémoire.
 ▼
27.6  Écriture du déchiffreur Python
 │    Reproduire le schéma crypto en sens inverse.
 ▼
27.7  Rapport d'analyse
      Formaliser les résultats dans un livrable professionnel.
```

Cette progression reflète un workflow réaliste d'analyse de malware : on commence toujours par le triage le moins risqué (statique, hors exécution), on formule des hypothèses, puis on les confirme en dynamique dans un environnement contrôlé.

---

## Prérequis

Avant d'aborder ce chapitre, assurez-vous :

- D'avoir configuré et validé votre **lab d'analyse sécurisé** ([Chapitre 26](/26-lab-securise/README.md)) — VM isolée, snapshots fonctionnels, réseau coupé.  
- D'être à l'aise avec **Ghidra** ([Chapitre 8](/08-ghidra/README.md)) et **GDB** ([Chapitre 11](/11-gdb/README.md)).  
- D'avoir parcouru le **Chapitre 24** (reverse de binaires avec chiffrement), en particulier l'identification des constantes cryptographiques et l'extraction de clés.  
- D'avoir compilé les binaires du chapitre : depuis le répertoire `binaries/ch27-ransomware/`, exécutez `make all`.

---

## Sommaire du chapitre

- 27.1 [Conception du sample : chiffrement AES sur `/tmp/test`, clé hardcodée](/27-ransomware/01-conception-sample.md)  
- 27.2 [Triage rapide : `file`, `strings`, `checksec`, premières hypothèses](/27-ransomware/02-triage-rapide.md)  
- 27.3 [Analyse statique : Ghidra + ImHex (repérer les constantes AES, flux de chiffrement)](/27-ransomware/03-analyse-statique-ghidra-imhex.md)  
- 27.4 [Identifier les règles YARA correspondantes depuis ImHex](/27-ransomware/04-regles-yara.md)  
- 27.5 [Analyse dynamique : GDB + Frida (extraire la clé en mémoire)](/27-ransomware/05-analyse-dynamique-gdb-frida.md)  
- 27.6 [Écriture du déchiffreur Python](/27-ransomware/06-dechiffreur-python.md)  
- 27.7 [Rédiger un rapport d'analyse type (IOC, comportement, recommandations)](/27-ransomware/07-rapport-analyse.md)  
- 🎯 **Checkpoint** : [déchiffrer les fichiers et produire un rapport complet](/27-ransomware/checkpoint.md)

⏭️ [Conception du sample : chiffrement AES sur `/tmp/test`, clé hardcodée](/27-ransomware/01-conception-sample.md)
