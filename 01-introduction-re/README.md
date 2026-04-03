🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 1 — Introduction au Reverse Engineering

> **Partie I — Fondamentaux & Environnement**  
> 📦 Aucun prérequis technique spécifique — ce chapitre est accessible à tous les profils visés par la formation.

---

## Pourquoi ce chapitre ?

Avant d'ouvrir un désassembleur ou de poser un breakpoint, il est indispensable de comprendre ce qu'est le reverse engineering, pourquoi on le pratique, et dans quel cadre on a le droit de le faire. Ce chapitre pose les fondations conceptuelles, méthodologiques et éthiques sur lesquelles repose l'ensemble de la formation.

Trop de tutoriels plongent directement dans les outils sans prendre le temps de définir les objectifs ni le périmètre. Le résultat est souvent une accumulation de recettes techniques sans vision d'ensemble, et surtout sans conscience des limites légales. Ce chapitre corrige cela dès le départ.

---

## Ce que vous allez apprendre

Ce chapitre couvre six thèmes qui, pris ensemble, vous donneront une vision claire de la discipline avant d'entrer dans la pratique :

**Définition et objectifs du RE** — Qu'est-ce que le reverse engineering appliqué aux logiciels ? En quoi se distingue-t-il du simple débogage ? Quels sont les objectifs concrets qu'un analyste poursuit face à un binaire inconnu ?

**Cadre légal et éthique** — Le RE touche à la propriété intellectuelle et au droit informatique. Nous examinerons les lois qui l'encadrent (CFAA aux États-Unis, directive EUCD en Europe, DMCA) et les zones grises dans lesquelles il est facile de se retrouver sans le vouloir. Cette section n'est pas un cours de droit, mais elle vous donnera les repères essentiels pour pratiquer en toute légalité.

**Cas d'usage légitimes** — Le reverse engineering n'est pas une activité marginale réservée aux pirates. Il est pratiqué quotidiennement par des professionnels dans des contextes parfaitement légaux : audit de sécurité mandaté, compétitions CTF, débogage avancé lorsque le code source est indisponible, interopérabilité entre systèmes, et recherche de vulnérabilités (vulnerability research). Nous passerons en revue ces cas d'usage pour ancrer la formation dans des scénarios concrets et légitimes.

**Analyse statique vs analyse dynamique** — Le RE repose sur deux approches complémentaires. L'analyse statique consiste à examiner un binaire sans l'exécuter (désassemblage, décompilation, inspection hexadécimale). L'analyse dynamique consiste à observer le programme pendant son exécution (débogage, instrumentation, traçage). Comprendre la distinction entre ces deux familles de techniques — et surtout leur complémentarité — est fondamental pour structurer toute démarche d'analyse.

**Vue d'ensemble de la méthodologie et des outils** — Nous présenterons le workflow général que cette formation enseigne, du premier contact avec un binaire inconnu jusqu'à la reconstruction de sa logique interne. Vous découvrirez la liste des outils utilisés tout au long du tutoriel et leur place dans ce workflow, sans entrer dans les détails d'installation (ce sera l'objet du chapitre 4).

**Taxonomie des cibles** — Le terme « reverse engineering » couvre un spectre très large : binaires natifs (ELF, PE, Mach-O), bytecode managé (.NET CIL, JVM), firmware embarqué, protocoles réseau, formats de fichiers… Cette formation se concentre sur les **binaires natifs ELF compilés avec la chaîne GNU** sous Linux x86-64. Nous situerons ce périmètre dans le paysage global du RE pour que vous sachiez précisément ce que ce tutoriel couvre — et ce qu'il ne couvre pas (ou couvre partiellement dans les parties bonus).

---

## Plan du chapitre

- 1.1 — [Définition et objectifs du RE](/01-introduction-re/01-definition-objectifs.md)  
- 1.2 — [Cadre légal et éthique (licences, lois CFAA / EUCD / DMCA)](/01-introduction-re/02-cadre-legal-ethique.md)  
- 1.3 — [Cas d'usage légitimes : audit de sécurité, CTF, débogage avancé, interopérabilité](/01-introduction-re/03-cas-usage-legitimes.md)  
- 1.4 — [Différence entre RE statique et RE dynamique](/01-introduction-re/04-statique-vs-dynamique.md)  
- 1.5 — [Vue d'ensemble de la méthodologie et des outils du tuto](/01-introduction-re/05-methodologie-outils.md)  
- 1.6 — [Taxonomie des cibles : binaire natif, bytecode, firmware — où se situe ce tuto](/01-introduction-re/06-taxonomie-cibles.md)  
- 🎯 — [Checkpoint : classer 5 scénarios donnés en « statique » ou « dynamique »](/01-introduction-re/checkpoint.md)

---

## Temps estimé

Comptez environ **1 h à 1 h 30** pour parcourir l'ensemble du chapitre, checkpoint compris. C'est un chapitre de lecture et de réflexion — aucun outil à installer, aucun binaire à manipuler. L'objectif est que vous abordiez le chapitre 2 avec une compréhension claire de ce qu'est le RE, de ce que vous avez le droit de faire, et de la démarche globale que vous allez suivre.

---

> 💡 **Si vous êtes déjà familier avec le RE** et que vous connaissez le cadre légal, vous pouvez survoler ce chapitre et passer directement au checkpoint pour vérifier vos acquis. Si le checkpoint ne pose aucune difficulté, enchaînez avec le chapitre 2.

⏭️ [Définition et objectifs du RE](/01-introduction-re/01-definition-objectifs.md)
