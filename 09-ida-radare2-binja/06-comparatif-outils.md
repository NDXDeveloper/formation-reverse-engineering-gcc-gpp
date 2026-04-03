🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 9.6 — Comparatif Ghidra vs IDA vs Radare2 vs Binary Ninja

> 📘 **Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja**  
> Section précédente : [9.5 — Binary Ninja Cloud — prise en main rapide](/09-ida-radare2-binja/05-binary-ninja-cloud.md)

---

## Objectif de cette section

Après deux chapitres complets consacrés aux outils de désassemblage (Ghidra au chapitre 8, IDA Free, Radare2 et Binary Ninja dans les sections 9.1 à 9.5), il est temps de prendre du recul et de comparer ces quatre outils selon des critères concrets. L'objectif n'est pas de couronner un vainqueur — il n'y en a pas — mais de vous donner les clés pour choisir l'outil le plus adapté à chaque situation.

Le reverse engineering est un domaine où la polyvalence est une force. Le meilleur outil est celui que vous maîtrisez et qui correspond au contexte du moment : le binaire cible, les contraintes de licence, l'environnement de travail, et la nature de l'analyse. Cette section vous aide à construire votre propre grille de décision.

## Tableau comparatif synthétique

Le tableau ci-dessous couvre les éditions gratuites de chaque outil, sauf mention contraire. C'est la configuration la plus pertinente pour cette formation.

### Informations générales

| Critère | Ghidra | IDA Free | Radare2 / Cutter | Binary Ninja Cloud |  
|---|---|---|---|---|  
| **Éditeur** | NSA (open source) | Hex-Rays | Communauté (open source) | Vector 35 |  
| **Licence** | Apache 2.0 | Propriétaire, non commercial | LGPL v3 (r2), GPL v3 (Cutter) | Propriétaire, gratuit (Cloud) |  
| **Usage commercial** | Oui | Non | Oui | Non (Cloud) |  
| **Prix (version payante)** | Gratuit | ~600 € (Home) / ~2 800 €+ (Pro) | Gratuit | ~299 $ (Personal) / ~999 $+ (Commercial) |  
| **Open source** | Oui | Non | Oui | Non |  
| **Interface** | GUI (Java/Swing) | GUI (Qt) | CLI + GUI (Cutter/Qt) | Web (Cloud) / GUI native (payant) |  
| **Hors ligne** | Oui | Oui | Oui | Non (Cloud) |  
| **Plateformes hôtes** | Linux, macOS, Windows | Linux, macOS, Windows | Linux, macOS, Windows, Android, iOS | Navigateur (Cloud) / Linux, macOS, Windows (payant) |

### Capacités d'analyse

| Critère | Ghidra | IDA Free | Radare2 / Cutter | Binary Ninja Cloud |  
|---|---|---|---|---|  
| **Architectures cibles** | ~30+ (x86, ARM, MIPS, PPC, SPARC, AVR, etc.) | x86 / x64 uniquement | ~50+ (la plus large couverture) | x86, x64, ARM, ARM64 |  
| **Formats binaires** | ELF, PE, Mach-O, DEX, raw, etc. | ELF, PE, Mach-O | ELF, PE, Mach-O, DEX, COFF, raw, etc. | ELF, PE, Mach-O |  
| **Décompileur intégré** | Oui (toutes architectures supportées) | Cloud, avec quotas | Via plugin r2ghidra / rz-ghidra | Oui (HLIL) |  
| **Qualité de l'auto-analyse** | Très bonne | Excellente (référence historique) | Bonne (parfois inférieure sur binaires strippés) | Très bonne |  
| **Reconnaissance de fonctions (binaire strippé)** | Très bonne | Excellente | Bonne | Très bonne |  
| **Signatures de bibliothèques** | FID (Function ID) | FLIRT (plus large base) | Signatures Zignatures | Signature Libraries |  
| **Propagation de types** | Bonne | Très bonne | Correcte | Excellente |  
| **Représentation intermédiaire** | P-code (interne) | Microcode (interne, non exposé en Free) | ESIL (émulation) | BNIL 4 niveaux (LLIL → MLIL → HLIL) |

### Scripting et automatisation

| Critère | Ghidra | IDA Free | Radare2 / Cutter | Binary Ninja Cloud |  
|---|---|---|---|---|  
| **Langage de scripting** | Java, Python (Jython/Pyhidra) | IDAPython (limité en Free) | r2pipe (Python, JS, Go, Rust, etc.) | Non disponible (Cloud) |  
| **API objet** | Riche (Program, Function, Instruction, DataType…) | Riche (limité en Free) | Commandes texte/JSON | Excellente (payant uniquement) |  
| **Mode headless (batch)** | Oui (analyzeHeadless) | Non (Free) | Oui (`r2 -qc`) | Non (Cloud) |  
| **Plugins communautaires** | Écosystème croissant | Écosystème le plus large (30 ans) | Écosystème actif (r2pm) | Écosystème plus restreint |  
| **Intégration pipeline CI/CD** | Possible (headless) | Non (Free) | Naturelle (CLI-first) | Non (Cloud) |

### Fonctionnalités complémentaires

| Critère | Ghidra | IDA Free | Radare2 / Cutter | Binary Ninja Cloud |  
|---|---|---|---|---|  
| **Débogueur intégré** | Oui (basique) | Non (Free) | Oui (r_debug, supporte ptrace et gdbserver) | Non (Cloud) |  
| **Patching binaire** | Via plugin ou script | Limité | Natif (`r2 -w`) | Non (Cloud) |  
| **Diffing de binaires** | Via BinDiff / Diaphora | Via BinDiff / Diaphora | Natif (`radiff2`) | Via plugin (payant) |  
| **Émulation** | Via extension (PCode emulation) | Non (Free) | Natif (ESIL) | Non (Cloud) |  
| **Analyse collaborative** | Oui (Ghidra Server) | Non (Free) | Non (natif) | Partage de lien (Cloud) |  
| **Recherche de gadgets ROP** | Via script | Non (Free) | Natif (`/R`) | Non (Cloud) |  
| **Règles YARA** | Via script | Via plugin | Via plugin | Non (Cloud) |

## Analyse détaillée par critère

### Qualité du décompileur

Le décompileur est souvent le facteur décisif dans le choix d'un outil, car il détermine la vitesse à laquelle un analyste comprend le code.

**Ghidra** offre le meilleur rapport qualité/accessibilité. Son décompileur est gratuit, fonctionne hors ligne, couvre toutes les architectures supportées, et produit un pseudo-code de bonne qualité. Sur les binaires GCC x86-64, il gère correctement les structures de contrôle, les appels de fonctions, et la plupart des patterns d'optimisation `-O2`. Ses faiblesses apparaissent sur les binaires très optimisés (`-O3` avec vectorisation), sur certaines reconstructions de `switch/case`, et sur le traitement des types complexes du C++ (templates, héritage multiple). Le pseudo-code tend à être verbeux avec des casts explicites et des noms de variables génériques.

**IDA Pro** (version payante) est considéré comme la référence en matière de décompilation grâce au moteur Hex-Rays. La version Free offre un décompileur cloud avec des quotas, ce qui le rend moins pratique pour une utilisation intensive. Quand il est disponible, le résultat est souvent le plus concis et le plus lisible des quatre outils.

**Binary Ninja** se distingue par son architecture BNIL multi-niveaux (section 9.5). Le décompileur HLIL produit un pseudo-code propre et bien typé, parfois plus lisible que celui de Ghidra grâce à une meilleure simplification des expressions. La possibilité de descendre au MLIL ou LLIL pour vérifier une décompilation suspecte est un avantage unique. Dans la version Cloud, le décompileur est accessible sans restriction de quota.

**Radare2** ne possède pas de décompileur natif de qualité comparable. La commande `pdc` produit un pseudo-code rudimentaire. Cependant, le plugin `r2ghidra` intègre le décompileur de Ghidra directement dans `r2` et Cutter, offrant une qualité équivalente. C'est la solution recommandée.

### Reconnaissance de fonctions et analyse de binaires strippés

C'est un terrain où les différences entre outils sont mesurables et pratiquement significatives. Sur un binaire strippé (`strip`), l'outil doit identifier les bornes de fonctions sans l'aide de la table des symboles, uniquement par heuristique.

**IDA** est historiquement le meilleur dans cet exercice. Son algorithme de reconnaissance de prologues de fonctions, combiné à FLIRT pour les bibliothèques statiquement liées, identifie souvent 5 à 15 % de fonctions de plus que ses concurrents sur les cas difficiles. C'est particulièrement visible sur les binaires avec du code mélangé à des données, ou sur les binaires obfusqués avec du control flow flattening.

**Ghidra** et **Binary Ninja** sont proches en performance, avec un léger avantage à Binary Ninja sur certains patterns GCC optimisés et un avantage à Ghidra sur les binaires multi-architectures grâce à sa couverture plus large.

**Radare2** est correct mais peut être en retrait sur les cas les plus difficiles. La commande `aaaa` avec ses heuristiques agressives rattrape une partie du retard, au prix de faux positifs potentiels.

En pratique, les différences sont souvent marginales sur les binaires « propres » compilés avec GCC. Elles deviennent significatives sur les binaires obfusqués, packés, ou statiquement liés avec de grandes bibliothèques.

### Scripting et automatisation

C'est un axe de différenciation majeur entre les outils, et le critère où le choix de l'outil a le plus d'impact sur la productivité à long terme.

**Radare2** est le champion de l'automatisation légère et de l'intégration Unix. `r2pipe` permet de piloter l'analyse depuis Python avec quelques lignes de code, le mode non-interactif (`r2 -qc`) s'intègre dans n'importe quel script shell, et la sortie JSON de chaque commande rend le parsing trivial. Pour les tâches de type « analyser 200 binaires et produire un rapport », `r2` est imbattable en termes de simplicité de mise en œuvre.

**Ghidra** offre le scripting le plus puissant dans l'écosystème gratuit grâce à son API objet riche. Les scripts Java ou Python (via Pyhidra) ont accès à un modèle complet : programmes, fonctions, instructions, types, mémoire, références. Le mode headless (`analyzeHeadless`) permet l'analyse batch sans interface graphique. La courbe d'apprentissage de l'API est plus raide que `r2pipe`, mais les possibilités sont nettement plus étendues pour les analyses complexes (reconstruction de types, propagation inter-procédurale, transformation de code).

**Binary Ninja** (édition payante uniquement) est souvent citée comme ayant la meilleure API Python de l'industrie : bien typée, bien documentée, avec des abstractions propres à chaque niveau BNIL. Mais cette API n'est pas disponible dans la version Cloud gratuite.

**IDA Free** offre un accès limité à IDAPython. La version Pro dispose de l'API la plus mature et la mieux documentée (grâce à 30 ans d'existence), mais son prix la place hors de portée de la plupart des utilisateurs individuels.

### Ergonomie et courbe d'apprentissage

**IDA** a l'interface la plus mature et la plus fluide. Les raccourcis clavier sont logiques et cohérents, la navigation est rapide, et l'outil répond bien même sur de gros binaires. Un analyste expérimenté avec IDA est extrêmement productif. La courbe d'apprentissage est modérée : l'interface est intuitive pour quiconque a déjà utilisé un IDE.

**Binary Ninja Cloud** offre l'expérience la plus moderne et la plus accessible. L'interface web est épurée, la synchronisation entre vues est immédiate, et la prise en main est rapide. C'est probablement l'outil le plus facile à aborder pour un débutant complet.

**Ghidra** souffre d'une interface Java/Swing qui peut paraître datée et parfois lente, surtout au démarrage et sur les gros binaires. L'organisation en fenêtres multiples (CodeBrowser, Symbol Tree, Decompiler, etc.) est puissante mais demande un temps d'adaptation. L'outil compense par la richesse de ses fonctionnalités et par sa documentation officielle détaillée.

**Radare2** a la courbe d'apprentissage la plus raide. L'absence d'interface graphique par défaut, les commandes cryptiques, et la documentation parfois lacunaire rebutent beaucoup de débutants. En contrepartie, une fois la barrière passée, la productivité en CLI est remarquable. Cutter atténue considérablement ce problème en offrant une interface graphique complète.

### Travail collaboratif

**Ghidra** est le seul outil gratuit à proposer une solution de travail collaboratif intégrée. Ghidra Server permet à plusieurs analystes de travailler simultanément sur le même projet, avec un système de versioning et de résolution de conflits. C'est un avantage décisif pour les équipes.

**Binary Ninja Cloud** permet le partage de lien vers une analyse, ce qui est utile pour la communication mais ne constitue pas une collaboration en temps réel.

**IDA Free** et **Radare2** ne proposent pas de fonctionnalité collaborative native. Des solutions de contournement existent (export/import de bases IDA, projets `r2` partagés via Git), mais elles sont artisanales.

## Grille de décision par scénario

Plutôt qu'un classement absolu, voici des recommandations par contexte d'usage. Chaque scénario indique l'outil à privilégier en premier choix et les alternatives pertinentes.

### Apprentissage du RE et suivi de cette formation

**Premier choix : Ghidra.** C'est l'outil autour duquel cette formation est principalement construite (chapitre 8). Gratuit, complet, avec décompileur, et utilisable sans restriction.

**Complément recommandé : Radare2.** Pour la culture CLI, le scripting, et parce qu'une grande partie de la littérature CTF l'utilise.

### CTF et compétitions

**Premier choix : Radare2 + Ghidra.** La combinaison `r2` pour le triage rapide et le scripting, et Ghidra pour la décompilation approfondie, est la plus populaire dans la communauté CTF. La vitesse de `r2` en CLI est un avantage en contexte de compétition chronométrée.

**Alternative : Binary Ninja Cloud** pour un second avis rapide sur un décompilé, ou **IDA Free** si le binaire est x86-64.

### Analyse de malware en entreprise

**Premier choix : IDA Pro** (si le budget le permet). La référence de l'industrie, avec le meilleur décompileur, la plus grande base de signatures FLIRT, et l'écosystème de plugins le plus mature pour l'analyse de malware.

**Alternative gratuite : Ghidra.** Le décompileur est gratuit et de qualité suffisante pour la grande majorité des analyses. Le mode headless et Ghidra Server sont des atouts pour les équipes SOC.

**Complément : Radare2** pour l'automatisation batch et les tâches scriptées (extraction d'IOC, analyse d'entropie, recherche de patterns).

### Recherche de vulnérabilités et analyse programmatique

**Premier choix : Binary Ninja** (édition payante). L'architecture BNIL et l'API Python sont spécifiquement conçues pour ce type d'analyse. Écrire des requêtes de taint analysis ou de pattern matching sur le MLIL est nettement plus naturel que sur le pseudo-code textuel des autres outils.

**Alternative gratuite : Ghidra** avec scripting Python. L'API est riche et le modèle objet permet des analyses similaires, avec un coût de développement plus élevé.

### Analyse de firmware et d'architectures exotiques

**Premier choix : Ghidra** pour les architectures courantes (ARM, MIPS, PowerPC) grâce à son décompileur multi-architecture.

**Complément : Radare2** pour les architectures rares (AVR, 8051, Z80, SPARC, etc.) grâce à sa couverture exceptionnelle.

**IDA Free et Binary Ninja Cloud** sont écartés dans ce scénario car limités à x86/x64 (IDA Free) ou à un sous-ensemble d'architectures (Binary Ninja Cloud).

### Scripting rapide et intégration pipeline

**Premier choix : Radare2 + r2pipe.** Le mode CLI, la sortie JSON native, et le mode non-interactif en font l'outil le plus naturel pour l'intégration dans des scripts shell, des pipelines CI/CD, ou des frameworks d'analyse automatisée.

**Alternative : Ghidra headless.** Plus lourd à mettre en place (JVM, scripts de lancement), mais plus puissant une fois configuré grâce à l'API objet.

### Travail sur serveur distant via SSH

**Premier choix : Radare2.** C'est le seul outil du comparatif qui fonctionne confortablement dans un terminal sans environnement graphique. Les modes visuels (`V`, `VV`, `V!`) offrent une expérience de navigation dans le terminal.

**Alternative limitée : Ghidra** en mode headless pour l'analyse batch, mais sans interface interactive.

## Ce qui ne figure pas dans les tableaux

Les tableaux comparatifs capturent les caractéristiques objectives, mais certains facteurs moins tangibles méritent d'être mentionnés.

### La documentation et la communauté

**IDA** bénéficie de 30 ans de littérature : des milliers de writeups, de tutoriels, de cours universitaires, et de présentations de conférence. Quand vous cherchez « comment faire X en RE », la réponse est souvent en langage IDA. C'est un avantage écosystémique considérable.

**Ghidra** rattrape rapidement son retard depuis sa publication en 2019. La documentation officielle est bonne, et la communauté produit un volume croissant de tutoriels, de plugins, et de scripts. Le fait qu'il soit open source attire les contributions académiques et de recherche.

**Radare2** a une documentation officielle (le *radare2 book*) et une communauté active, mais la barrière d'entrée reste la plus haute. Les mises à jour fréquentes peuvent invalider des tutoriels anciens.

**Binary Ninja** a une documentation officielle de très bonne qualité pour l'API (édition payante), et une communauté enthousiaste mais plus restreinte.

### La stabilité et la maturité

**IDA** est l'outil le plus stable et le plus prévisible. Les releases sont espacées et soigneusement testées. Le comportement est cohérent d'une version à l'autre.

**Ghidra** est stable pour un outil open source de cette envergure, mais certaines fonctionnalités (notamment le décompileur sur des patterns inhabituels) peuvent produire des résultats inattendus. Les mises à jour majeures sont régulières.

**Binary Ninja** offre une bonne stabilité dans les versions commerciales. La version Cloud dépend de l'infrastructure de Vector 35.

**Radare2** est l'outil le plus dynamique en termes de développement, ce qui a un revers : des régressions occasionnelles entre versions, des changements d'interface, et une documentation qui peut être en décalage avec la version installée. Le fork Rizin (moteur de Cutter) ajoute une source de confusion potentielle.

### Le facteur carrière

Dans le monde professionnel du RE (threat intelligence, recherche de vulnérabilités, forensique), **IDA Pro** reste le standard attendu par les employeurs. Maîtriser IDA et IDAPython est un atout sur un CV. **Ghidra** est de plus en plus accepté et reconnu, en particulier dans les agences gouvernementales et les équipes qui ne peuvent pas justifier le coût d'IDA. **Binary Ninja** est apprécié dans la recherche académique et les équipes de vulnerability research. **Radare2** est valorisé dans la communauté CTF et parmi les profils techniques qui privilégient la maîtrise des fondamentaux.

La recommandation pratique : maîtrisez Ghidra comme outil principal (gratuit, complet, reconnu), familiarisez-vous avec IDA (pour lire la littérature existante et être opérationnel si votre employeur l'utilise), et gardez Radare2 dans votre ceinture d'outils pour le scripting et les cas où le CLI est un avantage.

## Recommandation pour la suite de cette formation

Pour les chapitres suivants (Partie III — Analyse Dynamique, Partie IV — Techniques Avancées, et Partie V — Cas Pratiques), nous utiliserons principalement **Ghidra** pour l'analyse statique et la décompilation, et **GDB** (chapitre 11) pour l'analyse dynamique. Les scripts d'automatisation utiliseront **`r2pipe`** ou le **scripting Ghidra** selon le contexte. Quand une analyse bénéficie d'un cross-checking, nous le signalerons et vous encouragerons à comparer avec l'outil de votre choix.

L'important n'est pas l'outil, mais la méthodologie. Les chapitres qui suivent se concentrent sur les techniques de reverse engineering — les outils ne sont que le véhicule.

---


⏭️ [🎯 Checkpoint : analyser le même binaire dans 2 outils différents, comparer les résultats du decompiler](/09-ida-radare2-binja/checkpoint.md)
