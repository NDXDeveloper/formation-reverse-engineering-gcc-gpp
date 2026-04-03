🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 9.5 — Binary Ninja Cloud (version gratuite) — prise en main rapide

> 📘 **Chapitre 9 — Désassemblage avancé avec IDA Free, Radare2 et Binary Ninja**  
> Section précédente : [9.4 — Scripting avec r2pipe (Python)](/09-ida-radare2-binja/04-scripting-r2pipe.md)

---

## Binary Ninja en quelques mots

Binary Ninja (souvent abrégé « Binja ») est un désassembleur et décompileur développé par Vector 35, une entreprise fondée par des vétérans de la scène CTF et de la recherche en sécurité. Lancé en 2016, il s'est rapidement fait une place aux côtés d'IDA et Ghidra grâce à deux atouts distinctifs : une **API Python exceptionnellement bien conçue**, et un système de **représentation intermédiaire multi-niveaux** (BNIL — *Binary Ninja Intermediate Language*) qui facilite l'analyse programmatique à différents degrés d'abstraction.

Binary Ninja existe en plusieurs éditions commerciales (Personal, Commercial, Enterprise), mais Vector 35 propose une **version gratuite utilisable dans le navigateur** : **Binary Ninja Cloud**. C'est cette version que nous utiliserons ici. Elle offre un accès au décompileur, à la navigation graphique, et à l'essentiel des fonctionnalités d'analyse — suffisamment pour évaluer l'outil et l'intégrer dans votre boîte à outils de RE.

## Accéder à Binary Ninja Cloud

Binary Ninja Cloud est une application web. Il n'y a rien à installer — un navigateur moderne suffit.

1. Rendez-vous sur [cloud.binary.ninja](https://cloud.binary.ninja).  
2. Créez un compte gratuit (email + mot de passe) ou connectez-vous si vous en avez déjà un.  
3. Vous accédez à un tableau de bord listant vos analyses précédentes (vide au premier usage).

Pour commencer une analyse, cliquez sur **Upload** et sélectionnez votre binaire. Nous utilisons `keygenme_O2_strip` comme pour les sections précédentes.

> ⚠️ **Confidentialité.** Le binaire est uploadé sur les serveurs de Vector 35 pour être analysé. Ne chargez jamais un binaire contenant des données sensibles, propriétaires ou classifiées sur un service cloud tiers. Pour les binaires d'entraînement de cette formation, il n'y a aucun risque.

Après l'upload, Binary Ninja lance automatiquement son analyse. La progression est affichée en temps réel. Une fois l'analyse terminée, l'interface principale s'ouvre.

## Limitations de la version Cloud

Avant d'explorer l'interface, clarifions ce que la version Cloud permet et ne permet pas par rapport aux éditions payantes.

**Ce qui est disponible :**

La version Cloud donne accès au moteur d'analyse complet de Binary Ninja, y compris le décompileur (appelé *High Level IL* ou HLIL). La navigation graphique, le renommage de fonctions et de variables, les cross-references, les commentaires, et la visualisation des différents niveaux d'IL sont tous présents. C'est considérablement plus généreux qu'IDA Free : vous disposez d'un décompileur fonctionnel sans restriction de quota.

**Ce qui n'est pas disponible :**

L'API Python — l'un des principaux atouts de Binary Ninja — n'est pas accessible dans la version Cloud. Le scripting et l'automatisation nécessitent une licence locale (Personal ou supérieure). De même, le mode headless pour l'analyse batch, les plugins communautaires, et l'import/export avancé de types ne sont pas disponibles. La version Cloud fonctionne exclusivement dans le navigateur, ce qui implique une dépendance à la connexion internet et aux serveurs de Vector 35. Enfin, la taille des binaires uploadables et le nombre d'analyses simultanées peuvent être limités.

Pour le contexte de ce chapitre — prendre en main l'outil et comparer son analyse avec celle de Ghidra, IDA et Radare2 — ces limitations ne sont pas bloquantes.

## Découvrir l'interface

L'interface de Binary Ninja Cloud est épurée et moderne, avec une organisation qui rappelle à la fois IDA et Ghidra mais avec un vocabulaire propre.

### La vue centrale : Linear et Graph

La zone principale affiche le code analysé. Comme dans IDA, deux modes sont disponibles :

- **Linear View** — le listing séquentiel de tout le binaire, fonction après fonction, avec les adresses, les octets, et le désassemblage annoté. C'est l'équivalent du mode texte d'IDA.  
- **Graph View** — la vue en blocs de base avec les arêtes de contrôle de flux. Chaque bloc est un rectangle contenant les instructions, et les branchements conditionnels sont représentés par des arêtes colorées. La navigation se fait par glisser-déplacer (pan) et molette (zoom).

La bascule entre les deux vues se fait via les onglets en haut de la vue centrale ou les raccourcis clavier.

### Le panneau de fonctions

Le panneau latéral gauche liste les fonctions détectées. Chaque entrée affiche le nom (ou `sub_XXXX` pour les fonctions non reconnues), l'adresse, et parfois le type de retour déduit par l'analyse. Un champ de recherche en haut permet de filtrer par nom — très utile sur les gros binaires avec des centaines de fonctions.

Un clic sur une fonction navigue vers celle-ci dans la vue centrale.

### Le panneau de décompilation

C'est l'un des panneaux les plus importants. Binary Ninja affiche le pseudo-code décompilé de la fonction courante, synchronisé avec la vue désassemblage. La qualité de la décompilation est généralement bonne, comparable à celle de Ghidra sur les binaires GCC x86-64, avec parfois des différences dans le style de présentation et le traitement de certains patterns d'optimisation.

Le pseudo-code affiché correspond au niveau **HLIL** (*High Level Intermediate Language*) de Binary Ninja — le plus haut niveau d'abstraction, le plus proche du C. Nous reviendrons sur les niveaux d'IL plus bas.

### Les chaînes

L'accès aux chaînes se fait via le menu ou la barre de recherche. Binary Ninja détecte les chaînes dans les sections de données et permet de naviguer vers leurs références croisées, comme dans les autres outils.

### Les cross-references

En sélectionnant un symbole (nom de fonction, variable, adresse), vous pouvez afficher ses cross-references. Binary Ninja distingue les références de code (appels, sauts) et les références de données (lectures, écritures), avec un panneau dédié qui liste chaque occurrence avec son contexte.

## L'architecture BNIL : la particularité de Binary Ninja

Ce qui distingue fondamentalement Binary Ninja des autres désassembleurs est son système de **représentation intermédiaire multi-niveaux**, appelé BNIL (*Binary Ninja Intermediate Language*). Comprendre cette architecture, même sans accès à l'API, éclaire la philosophie de l'outil et aide à interpréter ses résultats.

### Le problème que BNIL résout

Un désassembleur classique travaille à deux niveaux : l'assembleur brut (instructions machine) et le pseudo-code décompilé (C approximatif). Le problème est que le saut entre ces deux niveaux est immense. L'assembleur est trop détaillé pour raisonner sur la logique du programme (chaque opération triviale en C devient 3 à 5 instructions), et le pseudo-code est parfois trop simplifié ou infidèle (le décompileur a fait des hypothèses qui peuvent être fausses).

BNIL introduit des niveaux intermédiaires qui permettent de choisir le degré d'abstraction adapté à votre besoin.

### Les quatre niveaux

Binary Ninja transforme le code machine à travers une chaîne de représentations de plus en plus abstraites :

**Disassembly** — les instructions machine natives (x86-64 dans notre cas). C'est le même niveau que ce que produisent `objdump`, IDA, ou `r2`. Chaque instruction est spécifique à l'architecture cible.

**LLIL (Low Level IL)** — une première abstraction qui uniformise les instructions de toutes les architectures en un langage intermédiaire commun. Les particularités de x86-64 (instructions à effets de bord multiples, flags implicites, adressage complexe) sont décomposées en opérations simples et explicites. Par exemple, une instruction `push rax` en x86-64 est décomposée en deux opérations LLIL : décrémenter `rsp` de 8, puis écrire `rax` à l'adresse pointée par `rsp`. Ce niveau est indépendant de l'architecture.

**MLIL (Medium Level IL)** — un niveau supplémentaire d'abstraction qui introduit les notions de variables (au lieu de registres et d'emplacements de pile), élimine les manipulations explicites de la pile, et résout les conventions d'appel. Le passage de paramètres par registres (`rdi`, `rsi`…) est remplacé par des arguments nommés. Ce niveau se rapproche d'un code trois-adresses classique en compilation.

**HLIL (High Level IL)** — le niveau le plus abstrait, le plus proche du C. Les structures de contrôle (`if`, `while`, `for`, `switch`) sont reconstruites, les expressions sont combinées et simplifiées, et le résultat ressemble à du C lisible. C'est ce qu'affiche le panneau de décompilation.

### Pourquoi c'est utile en pratique

Dans l'interface Cloud, vous pouvez basculer entre ces niveaux d'IL pour la fonction courante. L'intérêt est double.

Quand le décompileur HLIL produit un résultat douteux (variable mal typée, structure de contrôle aberrante), descendre au niveau MLIL ou LLIL permet de vérifier ce que le code fait réellement, sans retomber dans la complexité de l'assembleur brut. C'est un niveau intermédiaire de confiance que ni IDA ni Ghidra ne proposent de manière aussi structurée.

Quand vous voulez comprendre comment une instruction x86-64 complexe se décompose (par exemple un `rep movsb`, un `lock cmpxchg`, ou une instruction SIMD), le passage en LLIL montre explicitement chaque micro-opération, ce qui est pédagogiquement précieux.

> 💡 L'API Python de Binary Ninja (disponible dans les éditions payantes) permet de travailler programmatiquement sur chacun de ces niveaux d'IL. C'est ce qui rend l'outil particulièrement adapté à la recherche automatisée de vulnérabilités et à l'analyse de taint : on peut écrire des requêtes sur le MLIL comme « trouver tous les chemins où une donnée provenant de `recv()` atteint un `memcpy()` sans passer par une vérification de taille ». Ce type d'analyse est nettement plus complexe à écrire directement sur l'assembleur ou sur le pseudo-code textuel d'un décompileur.

## Workflow sur `keygenme_O2_strip`

Le workflow dans Binary Ninja Cloud est cohérent avec la méthodologie générale du chapitre.

**1 — Upload et analyse.** Charger `keygenme_O2_strip` via l'interface web. Attendre la fin de l'analyse automatique.

**2 — Inspecter les fonctions.** Parcourir la liste des fonctions dans le panneau latéral. Identifier `main` (si reconnu) ou les fonctions `sub_*` candidates. Binary Ninja détecte généralement `main` sur les binaires ELF GCC en suivant le pattern `__libc_start_main`, comme IDA.

**3 — Explorer les chaînes.** Utiliser la fonction de recherche pour localiser les chaînes caractéristiques (« Access granted », « Wrong key »). Naviguer vers la chaîne, puis remonter les cross-references.

**4 — Lire le décompilé.** Afficher le panneau HLIL pour la fonction de vérification. Comparer le pseudo-code avec celui obtenu sous Ghidra pour le même binaire — les différences dans le style et la fidélité de la décompilation sont instructives.

**5 — Descendre dans les niveaux d'IL.** Si un passage du HLIL est ambigu, basculer en MLIL puis en LLIL pour comprendre la mécanique réelle.

**6 — Annoter.** Renommer les fonctions et variables identifiées. Les annotations sont sauvegardées dans votre espace Cloud et persistent entre les sessions.

## Renommage et annotation

Binary Ninja Cloud supporte les opérations d'annotation standard :

- **Renommer une fonction** — clic droit sur le nom de la fonction → *Rename*. Le nouveau nom se propage dans toute l'analyse (désassemblage, décompilé, XREF).  
- **Renommer une variable** — dans le panneau de décompilation, clic droit sur une variable → *Rename*. Comme Binary Ninja travaille au niveau MLIL/HLIL avec de vraies variables (pas des registres), le renommage est cohérent et propagé dans tout le pseudo-code.  
- **Changer un type** — clic droit → *Change Type*. Vous pouvez spécifier le type d'une variable locale, d'un paramètre, ou de la valeur de retour d'une fonction. Binary Ninja propage les types à travers les niveaux d'IL.  
- **Ajouter un commentaire** — clic droit sur une instruction → *Add Comment*. Le commentaire apparaît dans le désassemblage et peut être visible dans la vue décompilée selon le contexte.

## Points forts et points faibles

### Où Binary Ninja excelle

**La qualité du décompileur.** Sur les binaires GCC x86-64 avec optimisation modérée (`-O2`), le décompileur HLIL de Binary Ninja produit un pseudo-code souvent plus propre que celui de Ghidra, avec un meilleur traitement des expressions arithmétiques et des casts de types. Sur les binaires `-O0`, les deux sont excellents. Sur les binaires `-O3` avec vectorisation, les résultats divergent et aucun n'est systématiquement meilleur que l'autre.

**L'architecture BNIL.** La possibilité de naviguer entre quatre niveaux d'abstraction dans l'interface est un atout pédagogique et analytique unique. Aucun autre outil gratuit ne propose cette granularité.

**L'interface utilisateur.** L'interface Cloud est réactive, épurée, et intuitive. La synchronisation entre la vue graphe et le décompilé est fluide. Les raccourcis clavier sont cohérents et la courbe d'apprentissage est douce pour quelqu'un qui vient d'IDA ou de Ghidra.

**Le typage et la propagation.** Binary Ninja est particulièrement bon pour déduire et propager les types de données. Si vous typez un paramètre comme `struct sockaddr_in *`, l'outil propage cette information dans la fonction et reconstruit les accès aux champs de la structure.

### Où Binary Ninja Cloud est limité

**Pas de scripting.** C'est la limitation majeure. L'API Python de Binary Ninja est considérée par beaucoup comme la meilleure du marché en termes de conception (documentation, cohérence, typage), mais elle n'est pas disponible dans la version Cloud. Pour l'automatisation, `r2pipe` ou le scripting Ghidra restent les alternatives gratuites.

**Pas de mode hors ligne.** La version Cloud nécessite une connexion internet permanente. Sur un réseau d'entreprise restrictif, dans un lab d'analyse isolé (chapitre 26), ou simplement dans un train, l'outil est inaccessible. Ghidra et Radare2 fonctionnent intégralement hors ligne.

**Upload obligatoire.** Le binaire est envoyé sur les serveurs de Vector 35. C'est rédhibitoire pour certains contextes professionnels (analyse de malware confidentiel, audit sous NDA, binaires gouvernementaux).

**Support d'architectures.** La version Cloud couvre x86, x86-64, ARM et ARM64, ce qui suffit pour la grande majorité des cas. Mais pour les architectures plus exotiques (MIPS, PowerPC, SPARC, microcontrôleurs), Ghidra et Radare2 ont un avantage.

**Écosystème de plugins.** IDA bénéficie de 30 ans de plugins communautaires, et Ghidra d'un écosystème en croissance rapide. Binary Ninja a une communauté de plugins active mais plus petite, et les plugins ne sont de toute façon pas utilisables dans la version Cloud.

## Comparaison rapide du décompilé : Binary Ninja vs Ghidra

Pour illustrer concrètement les différences, voici le type de résultat que vous pouvez obtenir pour la même fonction de vérification de `keygenme_O2_strip` dans les deux outils.

**Ghidra** produit typiquement un pseudo-code fidèle mais verbeux, avec des casts explicites (`(char *)`, `(int)`) et des noms de variables générés (`local_18`, `param_1`). La structure de contrôle est correcte mais peut paraître lourde.

**Binary Ninja HLIL** tend à produire un code plus concis, avec des noms de variables plus lisibles par défaut (`var_18` renommé en `arg1` si c'est un paramètre reconnu), et des expressions simplifiées. Les `if/else` sont parfois mieux reconstruits, avec moins de `goto` parasites.

Les deux outils peuvent se tromper différemment : Ghidra peut mal inférer un type de retour, Binary Ninja peut mal reconstruire une boucle déroulée. C'est pourquoi la pratique de **cross-checking** — comparer le décompilé de deux outils sur le même binaire — est si précieuse. Les erreurs de l'un sont souvent corrigées par l'autre.

## Quand utiliser Binary Ninja Cloud

Binary Ninja Cloud trouve sa place dans votre workflow dans les situations suivantes :

- **Second avis sur un décompilé.** Vous avez analysé un binaire dans Ghidra et un passage du pseudo-code vous semble suspect. Uploader le binaire dans Binary Ninja Cloud et comparer le HLIL avec le décompilé Ghidra prend quelques minutes et peut lever l'ambiguïté.

- **Exploration des niveaux d'IL.** Vous voulez comprendre comment une instruction assembleur complexe se décompose, ou vérifier ce que le compilateur a réellement fait à un niveau intermédiaire. Les vues LLIL et MLIL sont uniques à Binary Ninja.

- **Analyse rapide sans installation.** Vous êtes sur une machine où Ghidra et Radare2 ne sont pas installés. Binary Ninja Cloud fonctionne dans n'importe quel navigateur, sans installation, sans configuration.

- **Évaluation avant achat.** Si vous envisagez d'investir dans une licence Binary Ninja (Personal à quelques centaines de dollars), la version Cloud vous permet de tester la qualité de l'analyse et la philosophie de l'outil avant de vous engager.

Pour les analyses lourdes, le scripting, le travail hors ligne, ou les contextes de confidentialité, Ghidra et Radare2 restent les choix de prédilection dans l'écosystème gratuit.

---


⏭️ [Comparatif Ghidra vs IDA vs Radare2 vs Binary Ninja (fonctionnalités, prix, cas d'usage)](/09-ida-radare2-binja/06-comparatif-outils.md)
