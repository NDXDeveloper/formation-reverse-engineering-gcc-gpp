🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 36.2 — Lectures recommandées (livres, papers, blogs)

> 📁 `36-ressources-progresser/02-lectures-recommandees.md`

---

## Comment aborder cette liste

Le reverse engineering est un domaine qui couvre un spectre très large : architecture processeur, formats binaires, systèmes d'exploitation, compilateurs, cryptographie, analyse de malware, exploitation… Aucun livre ne couvre tout cela en profondeur. La progression passe nécessairement par la lecture croisée de plusieurs sources, chacune éclairant un angle différent.

Cette section organise les lectures en trois catégories : les **livres de référence** (à lire en profondeur, crayon en main), les **articles et papers académiques** (pour approfondir des techniques spécifiques), et les **blogs techniques** (pour rester en veille sur les pratiques actuelles). Pour chaque ressource, nous indiquons le niveau requis, la pertinence par rapport à cette formation, et ce qu'elle apporte concrètement.

> 💡 **Conseil pratique** : ne cherchez pas à tout lire. Choisissez un livre qui correspond à votre prochain objectif d'apprentissage, lisez-le en parallèle de votre pratique sur les CTF (section 36.1), et passez au suivant quand vous sentez que vous avez intégré l'essentiel.

---

## Livres de référence

### Fondamentaux du reverse engineering

**Dennis Yurichev — *Reverse Engineering for Beginners* (RE4B)**  
Gratuit en PDF sur [beginners.re](https://beginners.re)  
Niveau : débutant à intermédiaire  

C'est le livre gratuit le plus complet sur le sujet. Yurichev couvre l'analyse de code désassemblé produit par les compilateurs C/C++ sur x86, x64 et ARM. Le livre adopte une approche par l'exemple : chaque construction du langage C (boucles, conditions, structures, pointeurs de fonction…) est compilée puis analysée au niveau assembleur. C'est exactement l'approche que nous avons suivie dans les chapitres 3, 7 et 16 de cette formation, mais portée sur plus de 1000 pages avec une profondeur considérable. Le livre est aussi connu sous le titre *Understanding Assembly Language*.

**Pertinence formation** : prolongement direct des chapitres 3 (assembleur x86-64), 16 (optimisations compilateur) et 17 (RE du C++).

---

**Eldad Eilam — *Reversing: Secrets of Reverse Engineering***  
Wiley, 2005 — ISBN 978-0764574818  
Niveau : débutant à intermédiaire  

Malgré son âge, ce livre reste un classique incontournable. Eilam pose les bases méthodologiques du RE : comment aborder un binaire inconnu, comment naviguer dans le code désassemblé, comment reconnaître les structures du compilateur. Les chapitres sur le RE des systèmes d'exploitation et sur la protection logicielle restent pertinents. C'est un excellent premier livre pour quiconque souhaite une vision d'ensemble structurée du domaine.

**Pertinence formation** : vision globale complémentaire à l'ensemble de notre formation, en particulier les parties I et II.

---

**Bruce Dang, Alexandre Gazet, Elias Bachaalany — *Practical Reverse Engineering: x86, x64, ARM, Windows Kernel, Reversing Tools, and Obfuscation***  
Wiley, 2014 — ISBN 978-1118787311  
Niveau : intermédiaire à avancé  

Écrit par des ingénieurs de Microsoft et de QuarksLab, ce livre est le successeur naturel de celui d'Eilam. Il couvre x86, x64 et ARM (le premier livre à traiter les trois architectures), le reverse du kernel Windows, les techniques de protection par machine virtuelle, et l'obfuscation. L'approche est systématique, avec des exercices pratiques intégrés. C'est le livre de référence pour passer du niveau intermédiaire au niveau avancé.

**Pertinence formation** : prolongement des chapitres 16 (optimisations), 17 (RE C++) et 19 (anti-reversing).

---

**Chris Eagle, Kara Nance — *The Ghidra Book: The Definitive Guide***  
No Starch Press, 2e édition 2026 — ISBN 978-1718504684  
Niveau : débutant à avancé  

Le guide de référence pour maîtriser Ghidra, l'outil principal de cette formation. Eagle (40 ans d'expérience en RE, auteur de *The IDA Pro Book*) et Nance (consultante sécurité et formatrice Ghidra) couvrent l'ensemble du framework : navigation dans le CodeBrowser, décompilateur, analyse de types, scripting Java/Python, extensions, et mode headless. La 2e édition (2026) intègre les fonctionnalités les plus récentes de Ghidra. C'est le compagnon idéal des chapitres 8 et 9, et une ressource de référence pour toute analyse statique avancée.

**Pertinence formation** : prolongement direct du chapitre 8 (Ghidra), avec des techniques avancées de scripting (chapitre 35) et d'analyse de structures C++ (chapitre 17).

---

### Analyse de malware

**Michael Sikorski, Andrew Honig — *Practical Malware Analysis: The Hands-On Guide to Dissecting Malicious Software***  
No Starch Press, 2012 — ISBN 978-1593272906  
Niveau : intermédiaire  

Considéré comme *le* livre de référence pour l'analyse de malware. Sikorski (ex-NSA, Mandiant) et Honig (DoD) structurent l'apprentissage en quatre phases progressives : analyse statique de base, analyse dynamique de base, analyse statique avancée (désassemblage avec IDA), et analyse dynamique avancée (débogage). Le livre couvre les techniques anti-analyse (anti-debug, anti-VM, packing) et fournit des labs pratiques avec des samples réels. Bien qu'orienté Windows et IDA Pro, la méthodologie est universelle et se transpose directement à notre contexte Linux/Ghidra.

**Pertinence formation** : prolongement direct de la Partie VI (chapitres 26-29), en particulier la méthodologie d'analyse et les techniques anti-reversing.

---

### Systèmes d'exploitation et bas niveau

**Randal E. Bryant, David R. O'Hallaron — *Computer Systems: A Programmer's Perspective* (CS:APP)**  
Pearson, 3e édition 2015 — ISBN 978-0134092669  
Niveau : débutant à intermédiaire  

Ce n'est pas un livre de RE à proprement parler, mais c'est probablement le meilleur investissement pour comprendre les fondations sur lesquelles repose tout le reverse engineering : représentation des données, assembleur x86-64, chaîne de compilation, édition de liens, mémoire virtuelle, gestion des processus. CS:APP est le manuel de cours de référence dans les universités américaines (Carnegie Mellon en tête). Si vous sentez des lacunes sur le fonctionnement d'un programme « du code C au processeur », c'est le livre qu'il vous faut.

**Pertinence formation** : fondations des chapitres 2 (chaîne de compilation), 3 (assembleur), et tout le modèle mémoire utilisé dans la Partie III.

---

**Michael Kerrisk — *The Linux Programming Interface* (TLPI)**  
No Starch Press, 2010 — ISBN 978-1593272203  
Niveau : intermédiaire  

La référence absolue sur les interfaces de programmation Linux : appels système, processus, mémoire, signaux, IPC, sockets, systèmes de fichiers. Indispensable pour comprendre ce que fait un binaire Linux quand on l'observe avec `strace`, `ltrace` ou GDB. Le livre fait plus de 1500 pages et se lit davantage comme une encyclopédie que comme un tutoriel — gardez-le à portée de main comme référence.

**Pertinence formation** : complément aux chapitres 5 (`strace`/`ltrace`), 11 (GDB), et 23 (binaire réseau).

---

### Exploitation et sécurité offensive

**Jon Erickson — *Hacking: The Art of Exploitation***  
No Starch Press, 2e édition 2008 — ISBN 978-1593271442  
Niveau : intermédiaire  

Un classique qui enseigne la programmation C, l'assembleur x86, les buffer overflows, les shellcodes, et les techniques réseau — le tout de façon progressive et pratique, avec un LiveCD inclus. Bien que daté sur certains aspects (les protections modernes ont évolué), les fondamentaux qu'il enseigne restent valides. C'est le pont naturel entre le RE et l'exploitation binaire.

**Pertinence formation** : transition depuis les compétences RE acquises vers l'exploitation, en complément de pwnable.kr (section 36.1).

---

### Compilateurs et analyse de programmes

**Alfred V. Aho, Monica S. Lam, Ravi Sethi, Jeffrey D. Ullman — *Compilers: Principles, Techniques, and Tools* (le « Dragon Book »)**  
Pearson, 2e édition 2006 — ISBN 978-0321486813  
Niveau : avancé  

Le manuel de référence sur la théorie des compilateurs. Comprendre comment un compilateur transforme du code source en code machine est fondamental pour le RE avancé — cela permet de reconnaître les patterns d'optimisation, de comprendre pourquoi le code désassemblé a cette forme précise, et d'anticiper les transformations appliquées par GCC ou Clang. Ce n'est pas un livre qu'on lit en entier : les chapitres sur l'analyse lexicale et syntaxique sont moins pertinents pour le RE. Concentrez-vous sur la génération de code, l'allocation de registres, et les optimisations.

**Pertinence formation** : approfondissement du chapitre 16 (optimisations compilateur) et du chapitre 17 (modèle objet C++).

---

## Articles et papers de référence

La littérature académique et technique en RE est abondante. Plutôt qu'une liste exhaustive, voici les documents qui ont un impact direct sur les techniques couvertes dans cette formation.

### Architecture x86-64

**Intel — *Intel® 64 and IA-32 Architectures Software Developer's Manual***  
La référence absolue pour le jeu d'instructions x86-64, disponible gratuitement sur le site d'Intel. Le volume 2 (Instruction Set Reference) est celui que l'on consulte le plus en RE : il décrit chaque instruction avec sa sémantique exacte, ses encodages, et ses effets sur les flags. Les volumes 1 (Basic Architecture) et 3 (System Programming Guide) sont utiles pour comprendre les modes processeur, la pagination et les mécanismes de protection. Ce n'est pas un document qu'on lit — c'est un document qu'on recherche.

**System V Application Binary Interface — AMD64 Architecture Processor Supplement**  
La spécification qui définit les conventions d'appel couvertes au chapitre 3.5-3.6 de cette formation : quels registres transportent les arguments (`rdi`, `rsi`, `rdx`…), quels registres sont préservés par l'appelé, comment la pile est alignée. C'est le document source derrière chaque prologue et épilogue de fonction que vous analysez. Disponible librement en ligne.

### Format ELF et édition de liens

**TIS Committee — *Executable and Linkable Format (ELF) Specification v1.2***  
La spécification originale du format ELF. Document technique aride mais indispensable quand on travaille sur les headers, sections et segments ELF. À garder comme référence plutôt qu'à lire séquentiellement. Disponible librement en ligne.

**Ian Lance Taylor — *Linkers* (série de 20 articles de blog, 2007)**  
Une série d'articles de blog qui explique le fonctionnement des éditeurs de liens (linkers) avec une clarté remarquable. Couvre la résolution de symboles, le chargement dynamique, le PLT/GOT, et le PIC. C'est le meilleur complément au chapitre 2.9 de notre formation sur le lazy binding.

### Analyse de programmes et exécution symbolique

**Shoshitaishvili et al. — *SOK: (State of) The Art of War: Offensive Techniques in Binary Analysis* (IEEE S&P 2016)**  
Le paper fondateur du framework angr. Il présente une taxonomie des techniques d'analyse binaire (analyse de flux de données, exécution symbolique, fuzzing) et leur implémentation dans un framework unifié. Lecture essentielle pour comprendre les fondements théoriques du chapitre 18.

**De Moura, Bjørner — *Z3: An Efficient SMT Solver* (TACAS 2008)**  
Le paper introduisant le solveur Z3 de Microsoft Research. Comprendre les bases du SMT solving est nécessaire pour utiliser efficacement angr et Z3 (chapitre 18).

### Anti-reversing et obfuscation

**Collberg, Thomborson, Low — *A Taxonomy of Obfuscating Transformations* (1997)**  
Le paper fondateur sur la classification des techniques d'obfuscation de code. Définit les catégories que nous avons couvertes au chapitre 19 : obfuscation du flux de contrôle, des données, et du layout. Malgré son ancienneté, la taxonomie reste la référence utilisée dans toute la littérature.

### Analyse de malware

**Solutions officielles du Flare-On Challenge** (publiées chaque année sur le blog de l'équipe FLARE de Google Cloud)  
Ce ne sont pas des papers académiques, mais ces write-ups détaillés constituent certaines des meilleures ressources techniques disponibles en analyse de malware. Chaque solution décompose le raisonnement de l'analyste étape par étape, avec des captures d'écran des outils. Les archives des éditions précédentes sont disponibles sur [flare-on.com](https://flare-on.com).

---

## Blogs techniques

Les blogs sont la source d'information la plus à jour en RE. Les techniques, outils et menaces évoluent constamment — les blogs suivent ce rythme mieux que les livres.

### Blogs individuels de chercheurs

**Exploit Reversing** — [exploitreversing.com](https://exploitreversing.com)  
Blog d'Alexandre Borges, consacré à la recherche de vulnérabilités, au développement d'exploits et au reverse engineering. Borges publie des articles techniques d'une profondeur exceptionnelle (certains dépassant 100 pages en PDF), couvrant l'analyse de malware Windows, la recherche de vulnérabilités sur macOS, et l'exploitation kernel. La *Malware Analysis Series* (MAS, 10 articles) et l'*Exploiting Reversing Series* (ERS, en cours de publication) sont des ressources de niveau avancé remarquables.

**MalwareTech** — [malwaretech.com](https://malwaretech.com)  
Blog de Marcus Hutchins, couvrant la recherche de vulnérabilités, le threat intelligence, le reverse engineering et les internals Windows. Mélange d'analyses techniques approfondies et de prises de recul sur l'actualité sécurité.

**Möbius Strip Reverse Engineering** — [msreverseengineering.com](https://www.msreverseengineering.com)  
Blog orienté analyse de programmes et déobfuscation. Propose notamment une *Program Analysis Reading List* extrêmement détaillée pour ceux qui souhaitent approfondir les fondements théoriques de l'analyse statique et de l'exécution symbolique — un complément avancé aux chapitres 18 et 20.

**Dennis Yurichev** — [yurichev.com](https://yurichev.com)  
Le site de l'auteur de *RE for Beginners*. Au-delà du livre, Yurichev publie régulièrement des analyses de fragments de code désassemblé et des exercices de RE.

### Blogs d'équipes et d'entreprises

**Google Cloud Threat Intelligence (ex-Mandiant/FireEye)** — [cloud.google.com/blog/topics/threat-intelligence](https://cloud.google.com/blog/topics/threat-intelligence)  
L'équipe FLARE publie régulièrement des analyses de campagnes malveillantes, des techniques de RE avancées, et les solutions du Flare-On annuel. C'est l'une des sources les plus respectées de la communauté.

**Quarkslab Blog** — [blog.quarkslab.com](https://blog.quarkslab.com)  
Blog de la société de sécurité française Quarkslab, fondée en 2011 par Fred Raynal (créateur du magazine MISC). Les articles couvrent le reverse engineering, la recherche de vulnérabilités, l'exploitation kernel, le fuzzing, et l'analyse de protocoles propriétaires. Quarkslab publie également des outils open source d'analyse binaire. C'est l'un des rares blogs de recherche en sécurité de ce niveau produit par une équipe francophone.

**Kaspersky SecureList** — [securelist.com](https://securelist.com)  
Le bras recherche de Kaspersky publie des analyses APT (Advanced Persistent Threats) qui sont souvent accompagnées de détails techniques poussés sur le reverse engineering des implants analysés.

**Trail of Bits Blog** — [blog.trailofbits.com](https://blog.trailofbits.com)  
Trail of Bits publie des articles techniques de haute qualité sur l'analyse binaire, le fuzzing, l'exécution symbolique et le développement d'outils. Leur travail sur Manticore (exécution symbolique) et les outils d'analyse statique est particulièrement pertinent.

**Carnegie Mellon SEI Blog** — [sei.cmu.edu/blog](https://www.sei.cmu.edu/blog)  
Le Software Engineering Institute de Carnegie Mellon publie régulièrement des articles sur le reverse engineering appliqué à l'analyse de malware, en particulier autour de Ghidra. Leurs articles présentent des outils et des méthodologies directement utilisables.

**Hex-Rays Blog** — [hex-rays.com/blog](https://hex-rays.com/blog)  
Le blog des créateurs d'IDA Pro. Même si notre formation utilise principalement Ghidra, les articles de Hex-Rays sur les techniques de désassemblage, les problèmes de décompilation et les nouvelles fonctionnalités d'IDA éclairent des concepts universels.

### Blogs communautaires et agrégateurs

**Tuts 4 You** — [tuts4you.com](https://forum.tuts4you.com)  
L'une des plus anciennes communautés de reverse engineering. Le forum regroupe des discussions, des tutoriels, des outils et des unpacking tutorials. C'est également là que sont partagées les annonces des compétitions comme le CTF crackmes.one.

**r/ReverseEngineering** — [reddit.com/r/ReverseEngineering](https://reddit.com/r/ReverseEngineering)  
Le subreddit dédié au RE. C'est un excellent agrégateur : les articles de blogs, les nouveaux outils, les papers et les write-ups de CTF y sont régulièrement partagés et discutés par la communauté. Abonnez-vous pour recevoir un flux continu de contenu RE de qualité (voir section 36.3 pour plus de détails sur les communautés).

**PoC||GTFO** — [pocorgtfo.hacke.rs](https://pocorgtfo.hacke.rs)  
Un zine technique légendaire dans la communauté sécurité, publié par Travis Goodspeed et al. Chaque numéro contient des articles profondément techniques sur le RE, l'exploitation, la cryptographie et les formats de fichiers — souvent avec une bonne dose d'humour et de créativité. Le zine lui-même est un polyglotte (un fichier qui est simultanément un PDF valide, une image, et parfois un exécutable). Les archives sont disponibles gratuitement.

---

## Tableau récapitulatif des livres

| Livre | Auteur(s) | Année | Niveau | Thématique | Gratuit |  
|---|---|---|---|---|---|  
| *RE for Beginners* | D. Yurichev | Continu | Déb. → Inter. | RE généraliste (x86, x64, ARM) | Oui |  
| *Reversing* | E. Eilam | 2005 | Déb. → Inter. | RE méthodologique | Non |  
| *Practical Reverse Engineering* | Dang, Gazet, Bachaalany | 2014 | Inter. → Avancé | RE multi-arch + obfuscation | Non |  
| *The Ghidra Book* (2e éd.) | Eagle, Nance | 2026 | Déb. → Avancé | Maîtrise de Ghidra | Non |  
| *Practical Malware Analysis* | Sikorski, Honig | 2012 | Intermédiaire | Analyse de malware | Non |  
| *CS:APP* | Bryant, O'Hallaron | 2015 | Déb. → Inter. | Systèmes informatiques | Non |  
| *The Linux Programming Interface* | M. Kerrisk | 2010 | Intermédiaire | Programmation système Linux | Non |  
| *Hacking: Art of Exploitation* | J. Erickson | 2008 | Intermédiaire | Exploitation binaire | Non |  
| *Compilers (Dragon Book)* | Aho, Lam, Sethi, Ullman | 2006 | Avancé | Théorie des compilateurs | Non |

---

## Ordre de lecture suggéré

Pour un lecteur ayant terminé cette formation, voici un parcours de lecture cohérent :

**Première lecture** : *Reverse Engineering for Beginners* de Yurichev. C'est gratuit, c'est directement aligné avec nos chapitres sur l'assembleur et les optimisations, et la quantité d'exemples permet de consolider les acquis rapidement. Inutile de lire les 1000+ pages — concentrez-vous sur les chapitres correspondant aux constructions C/C++ que vous rencontrez dans vos challenges CTF.

**Deuxième lecture** : *Practical Malware Analysis* de Sikorski et Honig si vous vous orientez vers l'analyse de malware (prolongement de notre Partie VI), ou *Practical Reverse Engineering* de Dang et al. si vous visez le RE avancé de manière plus générale.

**En référence permanente** : *The Ghidra Book* (2e édition) d'Eagle et Nance, à garder ouvert à côté de Ghidra pendant vos sessions d'analyse. Il remplace la consultation de la documentation en ligne fragmentée et accélère considérablement la maîtrise de l'outil.

**En parallèle** : *CS:APP* si vous ressentez le besoin de solidifier vos fondations sur le fonctionnement des systèmes, ou *The Linux Programming Interface* si les appels système et le comportement runtime des binaires Linux vous posent encore question.

**En continu** : suivez deux ou trois des blogs listés ci-dessus via RSS ou via r/ReverseEngineering. La veille régulière sur les blogs est ce qui permet de rester en phase avec l'évolution des outils et des techniques.

---

**Section suivante : 36.3 — Communautés et conférences**

⏭️ [Communautés et conférences (REcon, DEF CON RE Village, PoC||GTFO, r/ReverseEngineering)](/36-ressources-progresser/03-communautes-conferences.md)
