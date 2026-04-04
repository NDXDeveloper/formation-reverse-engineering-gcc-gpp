🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Annexes

> 📎 **Références rapides et tables récapitulatives** — à garder sous la main pendant toute la formation et au-delà.

---

## Pourquoi des annexes ?

Tout au long de cette formation, vous avez manipulé des registres, tapé des commandes GDB, écrit des patterns ImHex, lu des sections ELF et cherché des constantes cryptographiques dans des dumps hexadécimaux. Chacune de ces activités repose sur un ensemble de références factuelles — opcodes, conventions, syntaxes, signatures — qu'il est ni réaliste ni souhaitable de mémoriser intégralement.

Ces annexes centralisent l'ensemble de ces références en un point unique. Elles ne sont pas conçues pour être lues de manière linéaire mais pour être **consultées à la demande**, comme un manuel de référence posé à côté de votre terminal. Imprimez-les, épinglez-les dans votre éditeur de notes, ou gardez-les ouvertes dans un onglet permanent : c'est leur raison d'être.

---

## Organisation des annexes

Les annexes sont regroupées par domaine fonctionnel. Chacune est autonome et peut être consultée indépendamment des autres.

### Architecture et assembleur x86-64

- **Annexe A** — Référence rapide des opcodes x86-64 fréquents en RE  
- **Annexe B** — Conventions d'appel System V AMD64 ABI (tableau récapitulatif)  
- **Annexe I** — Patterns GCC reconnaissables à l'assembleur (idiomes compilateur)

L'annexe A fournit la table des instructions que vous rencontrerez dans la grande majorité des binaires. L'annexe B détaille la convention d'appel qui régit le passage de paramètres, la préservation des registres et l'utilisation de la pile sur Linux x86-64. L'annexe I complète ces deux premières en cataloguant les séquences d'instructions caractéristiques que GCC produit pour des constructions C/C++ courantes : divisions par constante via multiplication magique, switch-case transformés en jump tables, idiomes de comparaison, etc. Ces trois annexes forment un bloc cohérent pour quiconque lit du désassemblage au quotidien.

### Outils de RE — Cheat sheets

- **Annexe C** — Cheat sheet GDB / GEF / pwndbg  
- **Annexe D** — Cheat sheet Radare2 / Cutter  
- **Annexe E** — Cheat sheet ImHex : syntaxe `.hexpat` de référence

Ces trois fiches condensent les commandes et raccourcis essentiels des outils que vous avez utilisés tout au long des parties II, III et V. Elles suivent un format uniforme : commande, description courte, exemple d'utilisation. L'annexe C couvre GDB natif ainsi que les commandes ajoutées par les extensions GEF et pwndbg. L'annexe D se concentre sur les commandes `r2` en mode console et leur équivalent dans l'interface graphique Cutter. L'annexe E documente la syntaxe du langage de patterns `.hexpat` propre à ImHex, avec les types de base, les attributs, les structures conditionnelles et les fonctions intégrées.

### Format ELF

- **Annexe F** — Table des sections ELF et leurs rôles

Cette annexe dresse la liste complète des sections ELF que vous pouvez rencontrer dans un binaire produit par GCC ou G++, avec pour chacune son nom, son type, ses flags, son contenu typique et le contexte dans lequel elle intervient lors du reverse. Elle complète directement le chapitre 2 (section 2.4) et sert de référence permanente pour les chapitres d'analyse statique.

### Comparatifs d'outils

- **Annexe G** — Comparatif des outils natifs (outil / usage / gratuit / CLI ou GUI)  
- **Annexe H** — Comparatif des outils .NET (ILSpy / dnSpy / dotPeek / de4dot)

L'annexe G synthétise sous forme de tableau l'ensemble des outils présentés dans la formation pour le reverse de binaires natifs (ELF x86-64), en indiquant pour chacun s'il est libre, s'il fonctionne en ligne de commande ou via une interface graphique, et dans quels chapitres il est couvert. L'annexe H fait de même pour l'écosystème .NET abordé dans la partie VII, en comparant les décompilateurs et outils de patching sur des critères de fonctionnalités, de maintenance et de cas d'usage.

### Cryptographie et détection

- **Annexe J** — Constantes magiques crypto courantes (AES, SHA, MD5, RC4…)

Cette annexe regroupe les valeurs hexadécimales caractéristiques des algorithmes cryptographiques les plus répandus. Lorsque vous tombez sur une séquence suspecte dans `.rodata` ou dans un dump mémoire, cette table vous permet de l'identifier rapidement. Elle est particulièrement utile dans le contexte des chapitres 24 (reverse crypto) et 27 (analyse de ransomware).

### Glossaire

- **Annexe K** — Glossaire du Reverse Engineering

Le glossaire définit les termes techniques utilisés dans l'ensemble de la formation, du vocabulaire de base (ELF, section, segment) aux concepts avancés (RTTI, lazy binding, control flow flattening). Chaque entrée renvoie vers le chapitre où le concept est introduit pour la première fois.

---

## Comment utiliser ces annexes efficacement

La manière la plus productive de tirer parti de ces annexes dépend de votre contexte de travail.

**Pendant la formation** — consultez l'annexe correspondante chaque fois qu'un chapitre fait référence à une convention, un opcode ou une commande que vous ne maîtrisez pas encore. Les renvois sont indiqués dans le corps des chapitres par la mention *(voir Annexe X)*.

**Pendant un CTF ou une analyse réelle** — gardez les annexes A, B, C et I ouvertes en permanence. Ce sont les quatre références que vous consulterez le plus souvent lorsque vous êtes face à un désassemblage inconnu et que vous devez avancer vite.

**Pour identifier un algorithme crypto** — commencez par chercher les premiers octets de la séquence suspecte dans l'annexe J. Si vous obtenez une correspondance, vous savez immédiatement quel algorithme est en jeu et vous pouvez orienter votre analyse en conséquence.

**Pour choisir un outil** — les annexes G et H vous permettent de comparer les options disponibles selon vos contraintes (budget, système d'exploitation, préférence CLI/GUI) sans avoir à relire les chapitres de présentation.

---

## Table des annexes

| Annexe | Titre | Domaine |  
|--------|-------|---------|  
| **A** | [Référence rapide des opcodes x86-64 fréquents en RE](/annexes/annexe-a-opcodes-x86-64.md) | Architecture x86-64 |  
| **B** | [Conventions d'appel System V AMD64 ABI](/annexes/annexe-b-system-v-abi.md) | Architecture x86-64 |  
| **C** | [Cheat sheet GDB / GEF / pwndbg](/annexes/annexe-c-cheatsheet-gdb.md) | Outils — Débogage |  
| **D** | [Cheat sheet Radare2 / Cutter](/annexes/annexe-d-cheatsheet-radare2.md) | Outils — Désassemblage |  
| **E** | [Cheat sheet ImHex : syntaxe `.hexpat`](/annexes/annexe-e-cheatsheet-imhex.md) | Outils — Hex editing |  
| **F** | [Table des sections ELF et leurs rôles](/annexes/annexe-f-sections-elf.md) | Format ELF |  
| **G** | [Comparatif des outils natifs](/annexes/annexe-g-comparatif-outils-natifs.md) | Comparatifs |  
| **H** | [Comparatif des outils .NET](/annexes/annexe-h-comparatif-outils-dotnet.md) | Comparatifs |  
| **I** | [Patterns GCC reconnaissables à l'assembleur](/annexes/annexe-i-patterns-gcc.md) | Architecture x86-64 |  
| **J** | [Constantes magiques crypto courantes](/annexes/annexe-j-constantes-crypto.md) | Cryptographie |  
| **K** | [Glossaire du Reverse Engineering](/annexes/annexe-k-glossaire.md) | Référence générale |

---

> Accédez directement à l'annexe dont vous avez besoin via les liens ci-dessus, ou commencez par l'**Annexe A — Référence rapide des opcodes x86-64** pour une lecture séquentielle.

⏭️ [Référence rapide des opcodes x86-64 fréquents en RE](/annexes/annexe-a-opcodes-x86-64.md)
