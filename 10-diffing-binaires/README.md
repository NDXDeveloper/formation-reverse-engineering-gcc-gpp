🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 10 — Diffing de binaires

> **Partie II — Analyse Statique**  
>  
> 📦 Binaires utilisés : `binaries/ch10-keygenme/` (variantes `keygenme_v1` et `keygenme_v2`)  
> 🧰 Outils : BinDiff, Diaphora, `radiff2`, Ghidra, IDA Free

---

## Présentation du chapitre

Au fil des chapitres précédents, nous avons appris à désassembler un binaire, à naviguer dans son code avec Ghidra ou Radare2, et à comprendre ce qu'il fait. Mais dans la pratique, la question n'est pas toujours « que fait ce binaire ? » — c'est souvent **« qu'est-ce qui a changé entre ces deux versions ? »**.

Le **diffing de binaires** (ou *binary diffing*) consiste à comparer deux versions d'un même programme compilé pour identifier précisément les fonctions, blocs ou instructions qui ont été ajoutés, supprimés ou modifiés. C'est une discipline à part entière, au croisement du reverse engineering et de l'analyse de vulnérabilités.

### Pourquoi c'est indispensable

Quand un éditeur publie un correctif de sécurité, il ne fournit généralement pas le détail exact de la vulnérabilité corrigée — et c'est compréhensible. Mais le patch binaire, lui, contient toute l'information : en comparant la version vulnérable et la version corrigée, on peut localiser la fonction modifiée, comprendre la nature de la faille et, dans certains cas, écrire un exploit pour les systèmes non encore mis à jour. C'est la technique dite du **patch diffing** (ou *1-day analysis*), utilisée aussi bien par les équipes de sécurité défensive que par les chercheurs offensifs.

Au-delà de la sécurité, le diffing sert aussi à comprendre l'évolution d'un logiciel propriétaire entre deux releases, à vérifier qu'une recompilation produit un résultat identique (*reproducible builds*), ou encore à analyser les différences de comportement introduites par un changement de flags de compilation.

### Ce que vous allez apprendre

Ce chapitre couvre les outils et méthodologies de diffing les plus utilisés dans l'écosystème RE :

- **BinDiff** (Google) — l'outil de référence historique, qui fonctionne à partir des bases exportées par Ghidra ou IDA. Il compare les graphes de flot de contrôle (CFG) pour apparier les fonctions entre deux binaires et attribuer un score de similarité à chaque paire.

- **Diaphora** — une alternative open source sous forme de plugin Ghidra/IDA, qui offre des capacités similaires à BinDiff avec une flexibilité accrue grâce à son approche par heuristiques multiples (hashes de pseudo-code, noms de symboles, constantes, graphes d'appels).

- **`radiff2`** — l'outil de diffing intégré à la suite Radare2, utilisable entièrement en ligne de commande. Moins riche graphiquement, mais parfaitement adapté à l'automatisation et au scripting dans un pipeline d'analyse.

- **L'application concrète** — un cas pratique complet où vous comparerez deux versions d'un même binaire pour identifier une correction de vulnérabilité, en combinant les outils vus dans le chapitre.

### Prérequis

Ce chapitre s'appuie directement sur les compétences acquises dans les chapitres précédents :

- **Chapitre 7** — Désassemblage avec `objdump` et Binutils (lecture de code désassemblé, syntaxe Intel)  
- **Chapitre 8** — Ghidra (navigation dans le CodeBrowser, cross-references, decompiler)  
- **Chapitre 9** — IDA Free et Radare2 (workflow de base, commandes `r2` essentielles)

Une familiarité avec le concept de graphe de flot de contrôle (CFG) — vu dans le Function Graph de Ghidra au chapitre 8 — est particulièrement utile ici, car c'est la représentation sur laquelle reposent la plupart des algorithmes de diffing.

### Approche pédagogique

Le chapitre suit une progression en quatre temps :

1. **Le pourquoi** — comprendre les motivations et les scénarios concrets qui justifient le diffing de binaires.  
2. **Les outils graphiques** — BinDiff puis Diaphora, avec import depuis Ghidra, lecture des résultats et interprétation des scores de similarité.  
3. **La ligne de commande** — `radiff2` pour les workflows automatisés et l'intégration dans des scripts.  
4. **La mise en pratique** — un cas réaliste de patch diffing sur deux versions d'un binaire fourni.

Chaque section inclut des captures d'écran annotées et des commandes reproductibles sur les binaires du dépôt.

---

## Sections du chapitre

- 10.1 [Pourquoi comparer deux versions d'un même binaire (analyse de patch, détection de vuln)](/10-diffing-binaires/01-pourquoi-diffing.md)  
- 10.2 [BinDiff (Google) — installation, import depuis Ghidra/IDA, lecture du résultat](/10-diffing-binaires/02-bindiff.md)  
- 10.3 [Diaphora — plugin Ghidra/IDA open source pour le diffing](/10-diffing-binaires/03-diaphora.md)  
- 10.4 [`radiff2` — diffing en ligne de commande avec Radare2](/10-diffing-binaires/04-radiff2.md)  
- 10.5 [Cas pratique : identifier une correction de vulnérabilité entre deux versions d'un binaire](/10-diffing-binaires/05-cas-pratique-patch-vuln.md)  
- [**🎯 Checkpoint** : comparer `keygenme_v1` et `keygenme_v2`, identifier la fonction modifiée](/10-diffing-binaires/checkpoint.md)

---

> **💡 Astuce** — Si vous n'avez pas encore installé Radare2, revenez au chapitre 4 (section 4.2) pour la procédure d'installation. BinDiff et Diaphora seront installés au fil de leurs sections respectives.

⏭️ [Pourquoi comparer deux versions d'un même binaire (analyse de patch, détection de vuln)](/10-diffing-binaires/01-pourquoi-diffing.md)
