🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 36.3 — Communautés et conférences (REcon, DEF CON RE Village, PoC||GTFO, r/ReverseEngineering)

> 📁 `36-ressources-progresser/03-communautes-conferences.md`

---

## Pourquoi la communauté compte autant que la technique

Le reverse engineering est souvent perçu comme une activité solitaire — un analyste seul face à un désassembleur, armé de patience et de café. En réalité, les meilleurs reverse engineers du monde s'appuient constamment sur leur communauté. Ils partagent des techniques dans des write-ups, présentent leurs recherches en conférence, échangent des outils et des scripts sur des forums, et discutent de problèmes concrets sur des canaux spécialisés.

Intégrer ces communautés apporte trois bénéfices concrets. Le premier est l'**exposition à des approches différentes** : face à un même binaire, dix analystes emploieront dix stratégies différentes, et chacune révèle quelque chose sur les possibilités de l'outillage et de la méthodologie. Le deuxième est la **veille passive** : en suivant les discussions, vous absorbez naturellement les nouvelles techniques, les nouveaux outils et les tendances du domaine sans effort structuré. Le troisième est le **réseau professionnel** : dans un domaine aussi spécialisé que le RE, les opportunités de carrière circulent souvent par le bouche-à-oreille au sein de la communauté avant d'apparaître sur les plateformes d'emploi classiques.

---

## Conférences

### REcon — La conférence dédiée au reverse engineering

**Lieu** : Montréal, Canada (Hilton DoubleTree)  
**Fréquence** : Annuelle, en juin  
**Édition 2026** : 19-21 juin (conférence) + 15-18 juin (formations)  
**Site** : [recon.cx](https://recon.cx)  

REcon est *la* conférence de référence mondiale pour le reverse engineering. Organisée à Montréal depuis 2005, elle réunit chaque année les experts les plus reconnus du domaine — chercheurs en vulnérabilités, analystes de malware, développeurs d'outils de RE, spécialistes en exploitation. La conférence adopte un format single-track (une seule salle de présentations) qui garantit que tous les participants assistent aux mêmes talks, favorisant des discussions riches entre les sessions.

Le contenu technique est de très haut niveau. Les présentations couvrent le RE sur toutes les plateformes (Windows, Linux, macOS, embarqué, mobile), l'analyse de malware avancée, les techniques d'exploitation, la déobfuscation, et le développement d'outils. L'édition 2026 propose notamment 19 formations de quatre jours, dont des cours dispensés par l'équipe FLARE de Google sur l'analyse de malware Windows moderne, des formations sur la déobfuscation logicielle par Tim Blazytko, du RE de binaires Rust, et des cours sur l'automatisation du RE avec des agents IA et le Model Context Protocol.

REcon est l'endroit où se retrouvent les créateurs de Ghidra, IDA Pro, Binary Ninja, Frida et de nombreux autres outils que nous avons utilisés tout au long de cette formation. C'est aussi là que sont souvent annoncées les nouvelles techniques qui deviendront les standards du domaine quelques mois plus tard.

> 💡 Les vidéos des éditions précédentes sont disponibles sur la chaîne YouTube de REcon. C'est une ressource gratuite de premier ordre pour accéder à des présentations de niveau expert.

**Coût** : L'inscription à la conférence est payante (tarif dégressif selon la date d'inscription). Les formations sont facturées séparément (environ 5 500 à 6 000 CAD pour quatre jours). Pour les étudiants ou les budgets limités, les vidéos en ligne sont une excellente alternative.

---

### DEF CON — Le rendez-vous hacker mondial

**Lieu** : Las Vegas, États-Unis (Las Vegas Convention Center)  
**Fréquence** : Annuelle, en août  
**Édition 2026** : DEF CON 34, du 6 au 9 août ; DEF CON Singapore du 28 au 30 avril  
**Site** : [defcon.org](https://defcon.org)  

DEF CON est la convention hacker la plus grande et la plus célèbre au monde, organisée chaque année à Las Vegas depuis 1993. Contrairement à REcon qui se concentre sur le RE, DEF CON couvre l'ensemble du spectre de la sécurité informatique : pentesting, ingénierie sociale, hardware hacking, cryptographie, forensics, IA, et bien sûr reverse engineering.

L'un des aspects les plus marquants de DEF CON est son système de **villages** — des espaces thématiques autogérés, chacun dédié à un domaine spécifique. Pour le RE, plusieurs villages sont pertinents : le **Hardware Hacking Village** propose régulièrement des challenges de reverse engineering sur des circuits et du firmware embarqué, l'**IoT Village** permet de pratiquer le RE sur des objets connectés réels, et le **Packet Hacking Village** touche à l'analyse de protocoles réseau. Le CTF principal de DEF CON, organisé par des équipes comme Nautilus Institute, inclut toujours des challenges de RE de très haut niveau.

DEF CON propose aussi des **formations (trainings)** avant la conférence. L'édition 2026 inclut notamment des cours sur l'automatisation du RE avec des agents IA et Ghidra. En 2026, DEF CON s'étend au-delà de Las Vegas avec des éditions à Singapour (28-30 avril) et à Bahreïn (DEF CON Middle East, 11-12 novembre).

**Coût** : L'entrée à DEF CON est relativement accessible comparé à d'autres conférences de sécurité (paiement en espèces uniquement, historiquement). Les formations sont facturées séparément. Le voyage et l'hébergement à Las Vegas représentent toutefois un budget conséquent.

---

### SSTIC — La référence francophone

**Lieu** : Rennes, France (Couvent des Jacobins)  
**Fréquence** : Annuelle, début juin  
**Édition 2026** : 3-5 juin  
**Site** : [sstic.org](https://www.sstic.org)  

Le Symposium sur la Sécurité des Technologies de l'Information et des Communications (SSTIC) est la conférence francophone de référence en sécurité informatique. Organisée à Rennes depuis 2003, elle rassemble environ 800 participants issus du monde académique, de l'industrie et des organisations gouvernementales (ANSSI, DGSE, ministère des Armées).

Le SSTIC privilégie les contributions techniques et scientifiques de haut niveau. Les thématiques couvrent la sécurité matérielle, la sécurité système et logicielle (dont le reverse engineering et l'analyse de malware), la sécurité réseau, la cryptographie et l'analyse de moyens offensifs. Les présentations sont en français, et les actes complets sont publiés librement en ligne — constituant une archive d'articles techniques francophones de grande qualité.

Le SSTIC est accompagné chaque année d'un **challenge technique** (publié quelques semaines avant la conférence) qui inclut systématiquement des épreuves de reverse engineering. Résoudre le challenge du SSTIC est un excellent objectif pour valider un niveau intermédiaire-avancé.

Pour un francophone intéressé par le RE et la sécurité, le SSTIC est un incontournable — tant pour le contenu technique que pour le réseau professionnel qu'il permet de construire dans l'écosystème cyber français.

**Coût** : Les places sont abordables mais partent extrêmement vite (quelques minutes après l'ouverture de la billetterie). Les actes sont accessibles gratuitement en ligne.

---

### Autres conférences pertinentes

**Black Hat** (Las Vegas, août / Europe, décembre) — La conférence professionnelle de sécurité la plus établie, juste avant DEF CON à Las Vegas. Le contenu est d'excellent niveau mais l'orientation est davantage professionnelle/commerciale. Les Briefings incluent régulièrement des présentations de RE et d'analyse de malware. Les Arsenal sessions permettent de découvrir de nouveaux outils.

**Hack.lu** (Luxembourg, octobre) — Conférence européenne de sécurité avec un bon équilibre entre accessibilité et profondeur technique. Le contenu RE y est régulier et les vidéos sont publiées en ligne.

**OffensiveCon** (Berlin, généralement en mai) — Conférence très technique, orientée exploitation et RE avancé. Le public est majoritairement composé de chercheurs en vulnérabilités et d'analystes offensifs. Le niveau des présentations est comparable à REcon.

**BlackHoodie** ([blackhoodie.re](https://blackhoodie.re)) — Organisation à but non lucratif (501(c)(3)) fondée en 2015 par Marion Marschalek (ingénieure sécurité, ancienne chercheuse offensive chez Intel) proposant des workshops gratuits de RE et d'analyse de malware destinés aux femmes. Les workshops ont lieu en marge de conférences comme REcon, DEF CON, Troopers, DistrictCon et Sec-T. L'objectif est de réduire les barrières d'entrée dans un domaine historiquement peu diversifié. Plusieurs participantes de BlackHoodie sont ensuite devenues formatrices et conférencières dans le circuit principal.

**LeHack** (Paris, juin — [lehack.org](https://lehack.org)) — Anciennement Nuit du Hack, LeHack est l'une des plus anciennes et des plus grandes conférences de hacking en France. Organisée à la Cité des Sciences de Paris (édition 2026 du 26 au 28 juin), elle combine conférences techniques, workshops pratiques et un CTF ouvert à tous. Le contenu couvre le pentesting, le RE, le hardware hacking et la sécurité offensive. C'est un événement accessible aux étudiants et aux passionnés, avec un bon équilibre entre contenu avancé et ouverture aux débutants — un complément naturel au SSTIC pour le public francophone.

**Barbhack** (Toulon, août) — Conférence française décontractée avec du contenu technique solide, incluant du RE.

**Hardwear.io** (Amsterdam / USA) — Pour ceux qui souhaitent explorer le RE hardware et embarqué au-delà du logiciel pur.

---

## Publications communautaires

### PoC||GTFO — Le zine légendaire

**Archives** : [pocorgtfo.hacke.rs](https://pocorgtfo.hacke.rs) et [github.com/angea/pocorgtfo](https://github.com/angea/pocorgtfo)  
**Édition papier** : No Starch Press (3 volumes)  

PoC||GTFO (*Proof of Concept or Get The Fuck Out*) est un journal technique au format de tract religieux, orchestré par « Pastor Manul Laphroaig » (alias Travis Goodspeed) et une communauté de contributeurs incluant certains des reverse engineers les plus respectés au monde : Ange Albertini (expert en formats de fichiers et fichiers polyglottes), Natalie Silvanovich (Project Zero de Google), Colin O'Flynn (attaques par canaux auxiliaires), Peter Ferrie (expert en anti-malware), et bien d'autres.

Le contenu est un mélange unique d'articles profondément techniques — reverse engineering de radios amateur, exploitation de microcontrôleurs, techniques d'infection ELF, fichiers polyglottes qui sont simultanément des PDF, des ZIP et des images — et d'essais philosophiques sur la culture hacker, le tout avec un humour pince-sans-rire omniprésent. Les fichiers PDF des numéros sont eux-mêmes des preuves de concept : chaque numéro est un fichier polyglotte valide dans plusieurs formats simultanément.

PoC||GTFO n'est pas un point d'entrée pour débutants. C'est une lecture qui récompense les lecteurs ayant déjà une base technique solide — typiquement le niveau atteint en fin de cette formation. Mais c'est aussi une source d'inspiration incomparable sur ce qu'un ingénieur compétent peut accomplir avec de la curiosité et du temps libre. Les trois volumes compilés sont publiés par No Starch Press sous forme de livres au format bible (couverture en simili-cuir, tranche dorée, signet), fidèle à l'esthétique du zine.

---

### tmp.out — Le zine ELF/Linux

**Archives** : [tmpout.sh](https://tmpout.sh)

tmp.out est un groupe de recherche et un zine fondé en 2021, entièrement consacré au format ELF et au hacking Linux. C'est la publication la plus directement alignée avec le focus de cette formation : chaque article porte sur les binaires ELF, les techniques d'infection, l'instrumentation binaire, le code polymorphe, ou les subtilités du loader Linux. Les contributions sont profondément techniques et constituent un prolongement avancé de nos chapitres 2 (format ELF, sections, segments) et 19 (anti-reversing). tmp.out est lu aussi bien par les chercheurs en sécurité que par la communauté underground — un pont rare entre les deux mondes.

---

### Phrack Magazine

**Archives** : [phrack.org](http://phrack.org)

Phrack est le plus ancien zine hacker encore en activité, publié depuis 1985. Historiquement orienté exploitation et sécurité offensive, Phrack a publié certains des articles les plus influents du domaine : techniques d'injection de code dans les ELF, exploitation du heap, techniques anti-forensics, et analyses de vulnérabilités kernel. Le numéro 71, publié en août 2024 lors de DEF CON 32 après trois ans de silence, a confirmé la vitalité du projet avec des articles couvrant l'exploitation kernel, l'évasion par déoptimisation, et les format strings avancées. Les archives constituent une mine d'or historique pour comprendre l'évolution des techniques de RE et d'exploitation.

---

## Forums et espaces d'échange en ligne

### r/ReverseEngineering

**URL** : [reddit.com/r/ReverseEngineering](https://reddit.com/r/ReverseEngineering)

Le subreddit r/ReverseEngineering est le principal agrégateur de contenu RE en anglais. Les membres y partagent des articles de blogs, des write-ups de CTF, des annonces d'outils, des papers académiques et des discussions techniques. La modération maintient un niveau de qualité élevé : le contenu est technique, les questions débutantes sont orientées vers r/REGames, et le spam est filtré.

C'est l'endroit idéal pour rester en veille passive sur le domaine. En consultant le subreddit quelques fois par semaine, vous aurez une vision à jour des publications, outils et techniques qui circulent dans la communauté. Les discussions dans les commentaires sont souvent aussi instructives que les articles partagés.

---

### Tuts 4 You

**URL** : [forum.tuts4you.com](https://forum.tuts4you.com)

L'une des plus anciennes communautés en ligne dédiées au reverse engineering, active depuis les années 2000. Tuts 4 You est spécialisé dans l'unpacking, le cracking et le reverse engineering de protections logicielles. Le forum propose des tutoriels, des outils (souvent développés par la communauté), et des discussions techniques sur le contournement de packers, de machines virtuelles de protection et d'obfuscateurs. C'est également sur Tuts 4 You que sont relayés les événements communautaires comme le CTF crackmes.one (voir section 36.1).

La communauté est réputée pour sa culture du partage de connaissances et son accueil des débutants motivés, à condition qu'ils fassent preuve d'effort dans leurs questions.

---

### Discord et canaux spécialisés

Plusieurs communautés actives existent sur Discord, bien que leur nature éphémère rende les liens moins pérennes que les forums. Parmi les plus pertinents :

Le **Discord de crackmes.one** est directement lié à la plateforme de challenges du même nom. C'est un bon endroit pour demander des indices (sans spoiler) sur des crackmes en cours et pour échanger avec d'autres praticiens.

Les **serveurs Discord des outils** — Ghidra, Binary Ninja, Cutter/Radare2 — sont des lieux précieux pour poser des questions sur l'utilisation avancée de ces outils, signaler des bugs, et découvrir des plugins ou scripts développés par la communauté.

Les **serveurs CTF** — de nombreuses équipes de CTF ont un Discord ouvert ou semi-ouvert. Rejoindre une équipe, même informelle, est l'un des meilleurs moyens de progresser rapidement : vous bénéficiez du feedback direct de joueurs plus expérimentés sur vos approches.

---

### Mastodon / Fediverse (infosec.exchange)

Depuis la migration d'une partie de la communauté sécurité hors de Twitter/X, l'instance **infosec.exchange** sur Mastodon est devenue un point de ralliement pour de nombreux chercheurs en sécurité et reverse engineers. REcon, le SSTIC, et de nombreux chercheurs individuels y publient régulièrement. C'est un canal de veille complémentaire à Reddit.

---

## Parcours d'intégration recommandé

S'intégrer dans la communauté RE ne se fait pas du jour au lendemain, et ce n'est pas nécessaire de s'impliquer partout. Voici une approche progressive :

**Étape 1 — Observer** : Abonnez-vous à r/ReverseEngineering et suivez quelques comptes RE sur Mastodon ou le réseau social de votre choix. Lisez les write-ups qui y sont partagés, même si vous ne comprenez pas tout. Regardez les vidéos des conférences (REcon, SSTIC, DEF CON) sur YouTube. L'objectif est de vous familiariser avec le vocabulaire, les outils et les approches de la communauté.

**Étape 2 — Participer en ligne** : Publiez vos propres write-ups de CTF, même simples. Posez des questions sur les forums et Discords après avoir fait vos recherches. Contribuez à un projet open source lié au RE (un plugin Ghidra, une règle YARA, un script d'analyse). La communauté RE valorise fortement les contributions techniques, quelle que soit leur taille.

**Étape 3 — Rencontrer en personne** : Assistez à une conférence. Pour un francophone, le SSTIC est le point d'entrée le plus naturel. Si le budget le permet, REcon et DEF CON offrent une immersion incomparable. Les rencontres en personne transforment des pseudonymes en visages et ouvrent des portes qu'aucune interaction en ligne ne peut ouvrir.

**Étape 4 — Contribuer** : Soumettez un talk ou un outil à un CFP (Call for Papers). Le SSTIC accepte les soumissions en français, ce qui réduit la barrière linguistique. Les rump sessions (présentations courtes de 5 minutes) sont un format idéal pour une première prise de parole en conférence.

---

## Tableau récapitulatif

| Ressource | Type | Langue | Coût | Focus RE | Fréquence |  
|---|---|---|---|---|---|  
| **REcon** | Conférence | EN | Payant | Exclusif | Annuel (juin) |  
| **DEF CON** | Conférence | EN | Payant | Partiel (villages, CTF) | Annuel (août) + événements régionaux |  
| **SSTIC** | Conférence | FR | Payant (actes gratuits) | Fort | Annuel (juin) |  
| **Black Hat** | Conférence | EN | Payant (cher) | Partiel | Bi-annuel (USA + Europe) |  
| **LeHack** | Conférence | FR | Payant | Partiel | Annuel (juin) |  
| **BlackHoodie** | Workshop | EN | Gratuit | Exclusif | Plusieurs par an |  
| **PoC\|\|GTFO** | Zine | EN | Gratuit (PDF) | Fort | Irrégulier |  
| **tmp.out** | Zine | EN | Gratuit | Exclusif (ELF/Linux) | Irrégulier |  
| **Phrack** | Zine | EN | Gratuit | Fort | Irrégulier |  
| **r/ReverseEngineering** | Forum | EN | Gratuit | Exclusif | Continu |  
| **Tuts 4 You** | Forum | EN | Gratuit | Exclusif | Continu |  
| **infosec.exchange** | Réseau social | EN/FR | Gratuit | Partiel | Continu |

---

**Section suivante : 36.4 — Parcours de certification (GREM, OSED)**

⏭️ [Parcours de certification : GREM (SANS), OSED (OffSec)](/36-ressources-progresser/04-certifications.md)
