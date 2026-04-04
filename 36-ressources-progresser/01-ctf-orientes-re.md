🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 36.1 — CTF orientés RE : pwnable.kr, crackmes.one, root-me.org, picoCTF, Hack The Box

> 📁 `36-ressources-progresser/01-ctf-orientes-re.md`

---

## Pourquoi les CTF sont le meilleur terrain d'entraînement

La théorie sans la pratique ne mène nulle part en reverse engineering. Les compétitions et plateformes de type Capture The Flag (CTF) offrent exactement ce dont un analyste en progression a besoin : des binaires conçus pour être analysés, une difficulté progressive, et un feedback immédiat — soit vous trouvez le flag, soit vous cherchez encore.

Contrairement à l'analyse de logiciels réels (où le contexte est souvent flou et les résultats difficilement vérifiables), un challenge CTF fournit un cadre clair : un objectif précis, une solution qui existe, et souvent une communauté qui publie des write-ups après la compétition. Ces write-ups sont en eux-mêmes une source d'apprentissage précieuse — ils révèlent des approches et des outils auxquels on n'aurait pas pensé.

Les plateformes présentées ci-dessous couvrent un large spectre de niveaux et de spécialités. Certaines sont exclusivement dédiées au RE, d'autres proposent le RE parmi d'autres catégories (pwn, crypto, forensics…). Nous les avons sélectionnées pour leur pertinence directe avec les compétences développées dans cette formation.

---

## Plateformes détaillées

### crackmes.one — La référence communautaire pour le RE pur

**URL** : [https://crackmes.one](https://crackmes.one)  
**Coût** : Gratuit  
**Niveau** : Débutant à expert  
**Focus** : Reverse engineering exclusif (crackmes)  

Crackmes.one est une plateforme communautaire où les utilisateurs soumettent des crackmes — de petits binaires conçus pour être analysés et « crackés ». Chaque crackme est accompagné d'un niveau de difficulté, d'une description de la plateforme cible (Windows, Linux, macOS) et du langage utilisé. Les solutions sont également soumises par la communauté.

C'est la plateforme la plus directement alignée avec le contenu de cette formation. Les crackmes couvrent exactement les techniques que nous avons vues : analyse de routines de vérification, patching de sauts conditionnels, extraction de clés, contournement de protections anti-debug, et écriture de keygens.

La communauté est active : en février 2026, crackmes.one a organisé son premier CTF officiel — une semaine de compétition exclusivement dédiée au reverse engineering, avec 12 challenges couvrant des thématiques allant des VMs custom à l'analyse de shellcode, en passant par le déchiffrement JIT et le DRM. L'événement est prévu pour devenir annuel.

**Point d'entrée recommandé** : filtrer les crackmes par difficulté 1-2, plateforme Linux, langage C. Cela correspond directement aux binaires sur lesquels nous avons travaillé dans les parties II à V.

> 🔒 Tous les téléchargements sont protégés par mot de passe : `crackmes.one`. Pensez à travailler dans une VM — même si les soumissions sont vérifiées, la prudence reste de mise face à tout exécutable téléchargé.

---

### picoCTF — L'entrée en matière idéale

**URL** : [https://picoctf.org](https://picoctf.org)  
**Coût** : Gratuit  
**Niveau** : Débutant à intermédiaire  
**Focus** : Multi-catégories (RE, crypto, forensics, web, pwn)  

PicoCTF est développé par l'université Carnegie Mellon. Conçu à l'origine pour les lycéens et étudiants, il offre une progression pédagogique particulièrement soignée. La compétition annuelle a lieu chaque printemps (l'édition 2026 s'est tenue du 9 au 19 mars), mais l'intégralité des challenges des éditions précédentes reste accessible toute l'année via la plateforme **picoGym**.

La catégorie « Reverse Engineering » de picoCTF est un excellent point de départ pour qui débute. Les premiers challenges consistent à analyser des scripts Python ou des binaires simples avec `strings` et `file`. La difficulté monte progressivement vers de l'analyse de binaires compilés, du contournement d'anti-debug, de l'unpacking UPX, et du RE de binaires .NET — des thématiques que nous avons couvertes respectivement aux chapitres 5, 19 et 30-32.

**Point d'entrée recommandé** : commencer par picoGym, filtrer par catégorie « Reverse Engineering », et résoudre les challenges par difficulté croissante. Les challenges des éditions précédentes restent un matériel pédagogique de premier choix.

---

### Root-Me — La plateforme francophone de référence

**URL** : [https://www.root-me.org](https://www.root-me.org)  
**Coût** : Gratuit (version PRO payante pour les entreprises)  
**Niveau** : Débutant à avancé  
**Focus** : Multi-catégories avec une section « Cracking » dédiée au RE  

Root-Me est une plateforme française qui propose des centaines de challenges dans de nombreuses catégories : web, réseau, cryptanalyse, stéganographie, forensics, et bien sûr **Cracking** (leur terme pour le reverse engineering). La section Cracking contient des challenges portant sur des binaires ELF et PE, couvrant des architectures variées (x86, ARM, MIPS).

La plateforme offre également un dépôt de documentation technique sur le reverse engineering, accessible depuis leur section « Repository ». Root-Me est particulièrement pertinente pour les francophones, puisque les descriptions de challenges et une partie de la documentation sont disponibles en français.

Root-Me a développé une version professionnelle, **Root-Me PRO**, utilisée par des entreprises et des institutions pour la formation en cybersécurité. La DGSE française a notamment utilisé cette plateforme pour organiser des challenges de recrutement incluant du reverse engineering.

**Point d'entrée recommandé** : section « Challenges > Cracking », en commençant par les challenges notés 1 ou 2 étoiles. Les prérequis indiqués par Root-Me (compréhension de l'assembleur, des formats exécutables, maîtrise des désassembleurs et débogueurs) correspondent exactement aux parties I à III de cette formation.

---

### pwnable.kr — L'exploitation binaire en mode wargame

**URL** : [http://pwnable.kr](http://pwnable.kr)  
**Coût** : Gratuit  
**Niveau** : Débutant à expert  
**Focus** : Exploitation binaire (pwn) avec une forte composante RE  

Pwnable.kr est une plateforme de type wargame orientée exploitation binaire. Les challenges sont organisés en quatre catégories de difficulté croissante : **Toddler's Bottle** (erreurs simples), **Rookiss** (exploitation classique pour débutants), **Grotesque** (challenges particulièrement retors) et **Hacker's Secret** (le niveau le plus avancé).

Bien que l'accent soit mis sur l'exploitation (buffer overflow, use-after-free, format strings…), chaque challenge implique une phase de reverse engineering significative. Il faut comprendre le binaire avant de pouvoir l'exploiter. Pwnable.kr est donc un excellent complément pour ceux qui souhaitent étendre leurs compétences du RE vers le pwn — un prolongement naturel des techniques vues dans cette formation.

Les challenges se résolvent en se connectant par SSH à des serveurs distants contenant les binaires à analyser. Chaque challenge fournit un flag sous forme de fichier à lire, dont l'accès nécessite l'exploitation d'une vulnérabilité.

**Point d'entrée recommandé** : la catégorie Toddler's Bottle. Les challenges `fd`, `collision`, `bof` et `flag` (ce dernier étant un pur challenge de RE impliquant un binaire packé avec UPX) constituent une excellente transition depuis notre chapitre 19 sur l'anti-reversing.

---

### Hack The Box — L'écosystème complet

**URL** : [https://www.hackthebox.com](https://www.hackthebox.com)  
**Coût** : Gratuit (accès limité) / Abonnement VIP payant  
**Niveau** : Intermédiaire à expert  
**Focus** : Multi-catégories avec une section « Reversing » dédiée  

Hack The Box (HTB) est l'une des plateformes de cybersécurité les plus populaires au monde. Elle propose à la fois des machines complètes à compromettre (pentesting) et des **challenges isolés** par catégorie. La catégorie **Reversing** contient des challenges dédiés au reverse engineering, du niveau facile au niveau « insane ».

Les challenges de RE sur HTB couvrent un large spectre : binaires ELF et PE, applications .NET, binaires packés, binaires obfusqués, et même des challenges combinant RE et cryptographie. La plateforme fournit les binaires sous forme d'archives protégées par le mot de passe `hackthebox`.

HTB propose également des **packs thématiques**, comme « Malware Reversing — Essentials », qui regroupent plusieurs challenges dans un parcours structuré. Ces packs sont particulièrement pertinents pour prolonger les chapitres 26 à 29 de cette formation sur l'analyse de code malveillant.

L'un des avantages de HTB est son système de classement et sa communauté très active. Les forums regorgent d'indices (sans spoiler direct) pour débloquer les situations. En revanche, la version gratuite limite l'accès à un nombre restreint de challenges actifs — l'abonnement VIP déverrouille l'intégralité du catalogue, y compris les challenges retirés.

**Point d'entrée recommandé** : filtrer les challenges par catégorie « Reversing » et difficulté « Easy ». Les premiers challenges impliquent typiquement l'analyse de binaires ELF avec Ghidra ou IDA — exactement les outils des chapitres 8 et 9.

---

## Autres plateformes et compétitions à connaître

Au-delà des cinq plateformes principales, plusieurs autres ressources méritent d'être mentionnées :

**Flare-On Challenge** ([https://flare-on.com](https://flare-on.com)) — La compétition annuelle de l'équipe FLARE de Google Cloud (anciennement FireEye/Mandiant). C'est **le** rendez-vous incontournable du reverse engineering. Chaque automne, une série de challenges progressifs est publiée, allant du crackme accessible à des analyses de malware d'une complexité redoutable. La compétition est individuelle et se déroule sur plusieurs semaines. Les solutions officielles publiées après l'événement sont des ressources pédagogiques de très haute qualité. Pour l'anecdote, c'est Flare-On qui a directement inspiré la création du CTF crackmes.one en 2026.

**Microcorruption** ([https://microcorruption.com](https://microcorruption.com)) — Créé par Matasano Security (aujourd'hui NCC Group) en collaboration avec Square, Microcorruption simule un microcontrôleur Texas Instruments MSP430 directement dans le navigateur. Les challenges consistent à exploiter des vulnérabilités dans le firmware d'un système de serrure électronique simulé (le « Lockitall LockIT Pro »). L'interface intégrée inclut un débogueur complet (breakpoints, inspection mémoire, pas-à-pas). C'est une excellente introduction au RE embarqué pour ceux qui souhaitent aller au-delà du x86-64 couvert dans cette formation.

**challenges.re** ([https://challenges.re](https://challenges.re)) — Créé par Dennis Yurichev (auteur de *Reverse Engineering for Beginners*), cette plateforme propose 87 exercices de RE « académiques » inspirés par Project Euler. L'approche est différente des CTF classiques : les challenges sont centrés sur la compréhension de fragments de code désassemblé, sans contexte de compétition. Point fort : les exercices couvrent de nombreuses architectures (x86, x64, ARM, ARM64, MIPS) et plateformes (Windows, Linux, macOS), ce qui en fait un bon terrain de pratique pour aller au-delà du x86-64 Linux couvert dans cette formation. Les solutions ne sont délibérément pas publiées — la vérification se fait par échange direct avec l'auteur.

**reversing.kr** — Une collection historique de challenges de RE et de crackmes, active de 2012 à 2019. Le domaine a expiré fin 2025 et le site n'est plus accessible directement. Cependant, les challenges et leurs binaires restent disponibles via les nombreux dépôts GitHub qui les ont archivés (rechercher « reversing.kr challenges »), et les write-ups détaillés pour chaque exercice sont toujours en ligne. Les challenges portent principalement sur des binaires Windows PE (cracking, anti-debug, unpacking) et restent pertinents pour la pratique.

**pwn.college** ([https://pwn.college](https://pwn.college)) — Plateforme pédagogique gratuite développée par l'Arizona State University (ASU). Elle propose plus de 1 000 challenges couvrant le reverse engineering, l'exploitation binaire, le shellcoding et le sandboxing, avec une progression incrémentale très structurée. pwn.college est utilisée pour les cours CSE 365 (Introduction to Cybersecurity) et CSE 466 (Computer Systems Security) d'ASU. Le module « Reverse Engineering » est directement pertinent pour consolider les compétences acquises dans cette formation. La plateforme est soutenue par un financement DARPA via l'ACE Institute (American Cybersecurity Education).

**CTFtime** ([https://ctftime.org](https://ctftime.org)) — Ce n'est pas une plateforme de challenges, mais l'annuaire de référence de toutes les compétitions CTF dans le monde. CTFtime recense les événements à venir, les classements des équipes, et surtout les **write-ups** publiés après chaque compétition. En filtrant par catégorie « Reverse Engineering », vous trouverez un flux continu de nouveaux challenges et de solutions à étudier.

---

## Comment structurer sa pratique

La quantité de plateformes et de challenges disponibles peut sembler écrasante. Voici une approche structurée pour en tirer le maximum, adaptée au niveau de progression :

**Phase 1 — Consolider les fondamentaux** (après cette formation) : Commencez par picoCTF (picoGym) et crackmes.one (difficulté 1-2). L'objectif est de résoudre régulièrement des challenges simples pour automatiser les réflexes : lancer `file` et `strings`, charger dans Ghidra, identifier le point de vérification, comprendre la logique. Visez un challenge tous les deux ou trois jours.

**Phase 2 — Monter en difficulté** : Passez aux challenges de difficulté moyenne sur Root-Me (Cracking) et Hack The Box (Reversing Easy/Medium). Commencez à diversifier les cibles : binaires strippés, binaires C++, binaires .NET, binaires packés. Lisez systématiquement les write-ups des challenges que vous n'arrivez pas à résoudre après un effort raisonnable.

**Phase 3 — Compétitions et spécialisation** : Participez à des CTF en ligne (consultez CTFtime pour le calendrier). Tentez le Flare-On annuel. Abordez les challenges « Hard » et « Insane » sur HTB. À ce stade, vous commencez à développer des spécialités : analyse de malware, RE de protocoles, RE embarqué, etc.

**Règle transversale** : Documentez chaque challenge résolu. Même un court fichier texte décrivant votre approche, les outils utilisés et les difficultés rencontrées constitue un matériel de révision précieux — et le début d'un portfolio (voir section 36.5).

---

## Tableau récapitulatif

| Plateforme | URL | Coût | Spécialité RE | Niveau | Langue |  
|---|---|---|---|---|---|  
| **crackmes.one** | crackmes.one | Gratuit | RE exclusif (crackmes) | Débutant → Expert | EN |  
| **picoCTF** | picoctf.org | Gratuit | RE parmi d'autres catégories | Débutant → Intermédiaire | EN |  
| **Root-Me** | root-me.org | Gratuit | Section « Cracking » dédiée | Débutant → Avancé | FR/EN |  
| **pwnable.kr** | pwnable.kr | Gratuit | Pwn + RE | Débutant → Expert | EN |  
| **Hack The Box** | hackthebox.com | Freemium | Section « Reversing » dédiée | Intermédiaire → Expert | EN |  
| **Flare-On** | flare-on.com | Gratuit | RE exclusif (annuel) | Intermédiaire → Expert | EN |  
| **Microcorruption** | microcorruption.com | Gratuit | RE embarqué (MSP430) | Débutant → Intermédiaire | EN |  
| **challenges.re** | challenges.re | Gratuit | RE académique | Intermédiaire | EN |  
| **pwn.college** | pwn.college | Gratuit | RE + pwn (pédagogique) | Débutant → Avancé | EN |  
| **CTFtime** | ctftime.org | Gratuit | Annuaire + write-ups | Tous niveaux | EN |

---

**Section suivante : 36.2 — Lectures recommandées (livres, papers, blogs)**

⏭️ [Lectures recommandées (livres, papers, blogs)](/36-ressources-progresser/02-lectures-recommandees.md)
