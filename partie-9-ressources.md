🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Partie IX — Ressources & Automatisation

Vous savez analyser un binaire de bout en bout — statiquement, dynamiquement, sur du C, du C++, du Rust, du Go, du .NET, du malware packé. Cette dernière partie vous fait passer de l'analyste qui sait tout faire à la main au reverseur qui **automatise, capitalise et continue de progresser**. Vous y construirez votre propre toolkit de scripts réutilisables, vous intégrerez l'analyse binaire dans des pipelines automatisés, et vous repartirez avec une feuille de route claire pour la suite : CTF, certifications, communautés, contributions.

---

## 🎯 Objectifs de cette partie

À l'issue de ces deux chapitres, vous serez capable de :

1. **Automatiser l'analyse de binaires ELF** avec des scripts Python utilisant `pyelftools` et `lief` pour parser, modifier et instrumenter des binaires par programme.  
2. **Lancer des analyses Ghidra en mode headless** sur des lots de binaires, exporter les résultats automatiquement, et intégrer cette étape dans un pipeline CI/CD d'audit de régression binaire.  
3. **Écrire des règles YARA** pour détecter des patterns récurrents (constantes crypto, signatures de packers, marqueurs de compilateur) dans une collection de binaires.  
4. **Organiser votre toolkit RE personnel** : structurer vos scripts, snippets, templates et règles pour les réutiliser d'un projet à l'autre.  
5. **Identifier les ressources et les communautés** pour continuer à progresser : plateformes CTF, lectures de référence, conférences, certifications, et construction d'un portfolio public.

---

## 📋 Chapitres

| N° | Titre | Description | Lien |  
|----|-------|-------------|------|  
| 35 | Automatisation et scripting | Parsing et modification d'ELF avec `pyelftools` et `lief`, Ghidra headless pour analyse batch, scripting `pwntools`, écriture de règles YARA, intégration dans un pipeline CI/CD, construction d'un toolkit RE personnel. | [Chapitre 35](/35-automatisation-scripting/README.md) |  
| 36 | Ressources pour progresser | Plateformes CTF (pwnable.kr, crackmes.one, root-me.org, picoCTF, Hack The Box), lectures recommandées (livres, papers, blogs), communautés et conférences (REcon, DEF CON RE Village, PoC\|\|GTFO), certifications (GREM, OSED), construction d'un portfolio RE. | [Chapitre 36](/36-ressources-progresser/README.md) |

---

## 🚀 Et après ?

La formation vous a donné les fondations et les outils. Voici quatre pistes concrètes pour transformer ces acquis en expertise :

**Pratiquer sur des CTF.** C'est le moyen le plus direct de progresser. Commencez par les challenges RE de picoCTF et root-me.org (accessibles), puis montez vers crackmes.one, pwnable.kr et Hack The Box (intermédiaire à avancé). Visez un challenge par semaine — la régularité compte plus que l'intensité.

**Contribuer à l'écosystème open source.** Ghidra, Radare2, Frida, YARA, `lief` — tous ces projets acceptent des contributions. Écrire un script Ghidra, ajouter des signatures pour une stdlib, corriger un bug dans r2 : c'est formateur, visible, et ça enrichit votre portfolio.

**Viser une certification.** GREM (SANS) pour l'analyse malware, OSED (OffSec) pour l'exploitation binaire. Ces certifications structurent votre progression et sont reconnues par l'industrie. Le chapitre 36 détaille les parcours et les retours d'expérience.

**Se spécialiser.** Le RE est un domaine large. Vous pouvez approfondir l'analyse malware (Partie VI comme point de départ), la recherche de vulnérabilités (fuzzing + exploitation), le RE de firmware/IoT, ou l'analyse de protocoles. Choisissez le domaine qui vous motive et allez-y en profondeur — la polyvalence viendra avec le temps.

---

## ⏱️ Durée estimée

**~8-12 heures** pour un praticien ayant complété les parties précédentes.

Le chapitre 35 (automatisation, ~5-7h) est le plus technique : vous écrirez des scripts Python conséquents, configurerez Ghidra en headless, et mettrez en place un pipeline. Prenez le temps de produire des scripts propres et documentés — ce sont eux que vous réutiliserez dans vos futurs projets. Le chapitre 36 (ressources, ~3-5h) est un chapitre de lecture et d'exploration : parcourez les plateformes CTF, feuilletez les livres recommandés, inscrivez-vous aux communautés. Le temps réel dépend de combien vous creusez chaque ressource.

---

## 📌 Prérequis

**Obligatoires :**

- Avoir complété les **[Partie I](/partie-1-fondamentaux.md)** à **[Partie V](/partie-5-cas-pratiques.md)** — le chapitre 35 automatise des workflows que vous devez savoir faire manuellement, et le chapitre 36 suppose que vous avez le niveau pour aborder des challenges CTF intermédiaires.  
- Maîtriser Python à un niveau suffisant pour écrire des scripts de 100-200 lignes utilisant des bibliothèques tierces.

**Recommandé :**

- Avoir parcouru au moins une des parties bonus (**[Partie VI](/partie-6-malware.md)**, **[Partie VII](/partie-7-dotnet.md)**, **[Partie VIII](/partie-8-rust-go.md)**) — les règles YARA et les scripts d'automatisation du chapitre 35 couvrent des cas issus de ces parties.  
- Avoir un compte sur au moins une plateforme CTF (root-me.org ou picoCTF) pour pouvoir suivre les exercices du chapitre 36 en direct.

---

## ⬅️ Partie précédente

← [**Partie VIII — Bonus : RE de Binaires Rust et Go**](/partie-8-rust-go.md)

## 🏠 Retour au sommaire

Vous avez parcouru l'ensemble de la formation. Retrouvez la table des matières complète, les annexes, les binaires d'entraînement et les corrigés des checkpoints depuis le sommaire principal.

→ [**Sommaire de la formation**](/README.md)

⏭️ [Chapitre 35 — Automatisation et scripting](/35-automatisation-scripting/README.md)
