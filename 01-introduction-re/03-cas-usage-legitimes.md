🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 1.3 — Cas d'usage légitimes

> **Chapitre 1 — Introduction au Reverse Engineering**  
> 📦 Aucun prérequis technique — section de lecture.  
> 📖 Cette section prolonge le cadre posé en [1.2 — Cadre légal et éthique](/01-introduction-re/02-cadre-legal-ethique.md) en illustrant concrètement les contextes dans lesquels le RE est pratiqué de manière légitime.

---

## Au-delà du cliché

Le reverse engineering souffre d'un problème d'image. Dans la culture populaire et dans une partie de la presse, il est souvent associé au piratage logiciel, à la création de cracks et à des activités clandestines. Cette perception est à la fois compréhensible (le piratage utilise effectivement des techniques de RE) et profondément trompeuse (la grande majorité du RE pratiqué au quotidien n'a rien à voir avec le piratage).

En réalité, le reverse engineering est une compétence professionnelle mobilisée dans de nombreux domaines de l'informatique, par des personnes qui agissent dans un cadre parfaitement légal et souvent mandaté. Cette section passe en revue les principaux cas d'usage légitimes — ceux que cette formation vous prépare à aborder.

---

## Audit de sécurité et recherche de vulnérabilités

### Le contexte

Les entreprises, les administrations et les éditeurs de logiciels font régulièrement appel à des spécialistes pour évaluer la sécurité de leurs produits. Lorsque le code source est disponible, l'audit prend la forme d'une revue de code (*source code review*). Mais dans de nombreux cas, le code source n'est pas accessible — soit parce que le logiciel est propriétaire, soit parce que l'audit porte sur un binaire livré par un sous-traitant, soit parce qu'on souhaite vérifier que le binaire déployé correspond bien au source audité.

C'est là que le RE intervient. L'analyste examine le binaire compilé pour y rechercher des failles de sécurité : dépassements de tampon, vulnérabilités de format de chaîne, erreurs de gestion mémoire, faiblesses cryptographiques, secrets codés en dur, communications non chiffrées, portes dérobées.

### Les cadres d'intervention

**Pentest (test d'intrusion)** — Un client mandate une équipe de sécurité pour tester ses défenses. Le périmètre inclut souvent l'analyse de binaires déployés sur l'infrastructure : applications serveur, agents de monitoring, firmwares d'équipements réseau. Le RE permet d'identifier des vulnérabilités qui ne seraient pas visibles par un simple scan réseau ou un test en boîte noire.

**Recherche de vulnérabilités (*vulnerability research*)** — Des chercheurs en sécurité analysent des logiciels largement déployés (navigateurs web, systèmes d'exploitation, bibliothèques réseau) pour y trouver des failles avant que des attaquants ne les découvrent. Ce travail repose massivement sur le RE et le fuzzing. Les vulnérabilités trouvées sont signalées aux éditeurs via des programmes de divulgation coordonnée, et souvent récompensées par des *bug bounties*.

**Audit de conformité et certification** — Dans certains secteurs (défense, aéronautique, systèmes industriels, santé), les logiciels doivent passer des certifications de sécurité. Le RE peut être utilisé pour vérifier que le binaire livré respecte les spécifications de sécurité — par exemple, qu'il n'effectue pas de communications réseau non prévues ou qu'il n'inclut pas de fonctionnalités non documentées.

**Réponse à incidents** — Quand une organisation subit une intrusion, l'équipe de réponse à incidents (*incident response*) doit analyser les outils laissés par l'attaquant : malwares, backdoors, scripts d'exfiltration. Le RE de ces artefacts est essentiel pour comprendre l'étendue de la compromission, identifier les données exfiltrées, et attribuer l'attaque. C'est le cœur de la Partie VI de cette formation.

> 💡 **Lien avec la formation** — Le chapitre 15 (fuzzing) et les chapitres 26 à 29 (analyse de code malveillant) mettent directement en pratique ce cas d'usage. Le chapitre 19 (anti-reversing) vous apprend à reconnaître les protections que vous rencontrerez lors d'un audit réel.

---

## Compétitions CTF (*Capture The Flag*)

### Le principe

Les CTF sont des compétitions de sécurité informatique où des équipes ou des individus résolvent des challenges techniques pour marquer des points. Parmi les catégories classiques (web, crypto, pwn, forensics, misc), la catégorie **« reversing »** occupe une place centrale : on vous donne un binaire, et vous devez comprendre sa logique pour en extraire un *flag* — une chaîne de caractères secrète qui prouve que vous avez résolu le challenge.

### Pourquoi c'est un cas d'usage légitime

Les CTF sont des environnements conçus pour être analysés. Les binaires sont créés spécifiquement pour la compétition, avec le consentement (et souvent l'encouragement) de leurs auteurs. Il n'y a aucune ambiguïté juridique ni éthique : l'objectif explicite est que les participants reversent le programme.

### Ce que les CTF développent comme compétences

La catégorie reversing des CTF couvre un spectre large de compétences qui correspondent précisément à celles enseignées dans cette formation :

- Triage rapide d'un binaire inconnu (identification du format, de l'architecture, des protections).  
- Lecture et interprétation de code assembleur x86-64.  
- Utilisation de désassembleurs et décompileurs (Ghidra, IDA, Binary Ninja).  
- Analyse dynamique avec GDB et ses extensions.  
- Identification de routines cryptographiques ou d'algorithmes d'encodage.  
- Exécution symbolique pour résoudre des contraintes (angr, Z3).  
- Contournement de protections anti-RE (obfuscation, packing, anti-debug).

Les CTF ont aussi une vertu pédagogique importante : ils imposent des contraintes de temps qui forcent à développer une méthodologie efficace, et les *write-ups* publiés après la compétition constituent une mine d'or de techniques documentées par la communauté.

> 💡 **Lien avec la formation** — Le chapitre 21 (reverse d'un keygenme) est directement inspiré des challenges CTF de type reversing. Le chapitre 36 liste les plateformes de CTF où vous pourrez vous entraîner après la formation.

---

## Débogage avancé sans code source

### Le problème

Tout développeur a un jour rencontré un crash ou un comportement inexplicable dans une bibliothèque tierce, un driver ou un composant système dont il ne possède pas le code source. Les outils de débogage classiques montrent une pile d'appels avec des adresses hexadécimales, un registre d'instruction qui pointe dans le vide, et aucun nom de variable ni de fonction pour comprendre ce qui se passe.

### Comment le RE aide

Les techniques de RE permettent de transformer cette boîte noire en quelque chose de compréhensible :

- **Désassembler la zone du crash** pour comprendre quelle instruction a échoué et pourquoi (déréférencement de pointeur NULL, accès hors limites, division par zéro).  
- **Remonter le flux de contrôle** pour identifier comment le programme est arrivé dans cet état (quels arguments ont été passés, quelle branche a été prise).  
- **Inspecter les structures de données en mémoire** pour comprendre l'état interne du programme au moment du crash (la pile est-elle corrompue ? Un compteur de références est-il devenu négatif ? Un buffer a-t-il débordé ?).  
- **Tracer les appels système** avec `strace` pour observer les interactions du programme avec le noyau (fichiers ouverts, sockets créées, signaux reçus).  
- **Hooker des fonctions** avec Frida pour observer les arguments et les valeurs de retour sans modifier le binaire.

Ce cas d'usage est particulièrement fréquent dans les environnements suivants :

**Développement embarqué** — Les bibliothèques fournies par les fabricants de puces (HAL, SDK) sont souvent livrées sous forme de binaires précompilés sans code source. Quand un bug survient dans cette couche, le développeur n'a pas d'autre choix que d'analyser le binaire.

**Intégration de bibliothèques tierces** — Une application qui utilise une bibliothèque propriétaire (codec vidéo, moteur de rendu, module de paiement) peut rencontrer des crashes dans cette bibliothèque. Si l'éditeur est lent à répondre ou a cessé son activité, le RE du composant incriminé est parfois le seul moyen d'avancer.

**Débogage de production** — Un programme crash en production mais pas en environnement de développement. Le binaire de production est compilé avec des optimisations agressives (`-O2`, `-O3`) qui modifient le code au point de rendre le débogage source-level inefficace. Comprendre l'assembleur optimisé devient alors nécessaire pour interpréter les core dumps.

> 💡 **Lien avec la formation** — Les chapitres 11 et 12 (GDB et ses extensions) couvrent en détail le débogage sur binaires strippés et optimisés. Le chapitre 16 (optimisations du compilateur) vous apprendra à reconnaître les transformations appliquées par GCC pour ne pas être déstabilisé par du code optimisé.

---

## Interopérabilité

### Le besoin

L'interopérabilité est la capacité de systèmes différents à fonctionner ensemble. Dans un monde idéal, tous les protocoles et formats de fichiers seraient documentés par des spécifications publiques. En pratique, de nombreux systèmes utilisent des protocoles propriétaires non documentés ou des formats de fichiers fermés. Quand vous devez faire communiquer votre logiciel avec un tel système, et que l'éditeur ne fournit pas de documentation ni d'API publique, le RE est souvent le seul recours.

### Exemples historiques et contemporains

**Samba et le protocole SMB/CIFS** — Le projet Samba est probablement l'exemple le plus emblématique de RE pour l'interopérabilité. L'équipe Samba a dû reverser le protocole de partage de fichiers de Microsoft pour permettre aux systèmes Linux et Unix de rejoindre des domaines Windows, d'accéder à des partages réseau et de fournir des services de fichiers compatibles. Ce travail de RE, mené sur plus de deux décennies, a été reconnu comme légitime au regard du droit européen.

**LibreOffice et les formats Microsoft Office** — La capacité de LibreOffice à ouvrir et éditer des fichiers `.docx`, `.xlsx` et `.pptx` repose en partie sur du reverse engineering des formats binaires originaux de Microsoft Office (`.doc`, `.xls`, `.ppt`), mené avant que Microsoft ne publie des spécifications partielles sous pression antitrust.

**Drivers graphiques open source** — Les projets Nouveau (pour les cartes NVIDIA) et les premiers drivers AMD open source ont été développés en reversant les interfaces matérielles et les blobs binaires fournis par les fabricants. Ce travail a permis d'offrir un support graphique Linux fonctionnel pour du matériel dont les spécifications n'étaient pas publiques.

**Protocoles IoT et domotique** — De nombreux appareils connectés (caméras, ampoules, thermostats, serrures) utilisent des protocoles propriétaires pour communiquer avec leur application mobile ou leur cloud. Des communautés de développeurs reversent ces protocoles pour les intégrer dans des plateformes domotiques ouvertes comme Home Assistant, permettant aux utilisateurs de contrôler leurs appareils sans dépendre de l'écosystème fermé du fabricant.

### Le RE d'interopérabilité en pratique

Ce type de RE combine typiquement :

- L'analyse du trafic réseau (capture de paquets avec Wireshark, traçage des appels avec `strace`).  
- Le RE du binaire client ou serveur pour comprendre comment les paquets sont construits et interprétés.  
- L'écriture d'un client ou d'un serveur de remplacement qui implémente le protocole identifié.

C'est exactement le parcours suivi dans le chapitre 23 de cette formation (reverse d'un binaire réseau client/serveur).

> 💡 **Lien avec la formation** — Le chapitre 23 (binaire réseau) et le chapitre 25 (format de fichier custom) correspondent directement à ce cas d'usage. Vous y reverserez un protocole réseau et un format de fichier non documentés pour écrire des implémentations indépendantes.

---

## Analyse de malware et réponse à incidents

### Le contexte

Quand une organisation détecte un logiciel malveillant sur son infrastructure, la première priorité est de comprendre ce que ce malware fait, comment il est arrivé là, ce qu'il a pu exfiltrer, et comment l'éradiquer complètement. Le RE est l'outil central de cette compréhension.

### Ce que le RE apporte à l'analyse de malware

- **Identification des capacités** — Le malware est-il un ransomware ? Un voleur de credentials ? Un keylogger ? Un bot de DDoS ? Un dropper qui télécharge d'autres composants ? Seule l'analyse du binaire permet de répondre avec certitude.  
- **Extraction des indicateurs de compromission (IOC)** — Adresses IP ou noms de domaine des serveurs de commande et contrôle (C2), chemins de fichiers créés ou modifiés, clés de registre (sur Windows), mutex, chaînes de caractères distinctives. Ces IOC sont ensuite utilisés pour détecter le malware sur d'autres machines du réseau.  
- **Compréhension du protocole C2** — Comment le malware communique-t-il avec son opérateur ? Quel protocole utilise-t-il ? Les communications sont-elles chiffrées ? Peut-on les décoder pour comprendre les commandes envoyées et les données exfiltrées ?  
- **Identification de la famille de malware** — En comparant les techniques, les constantes, les morceaux de code réutilisés, l'analyste peut rattacher le sample à une famille de malware connue, ce qui accélère considérablement la réponse.  
- **Développement de contre-mesures** — Comprendre le mécanisme de chiffrement d'un ransomware peut permettre d'écrire un déchiffreur. Comprendre le mécanisme de persistance d'un implant permet de l'éradiquer proprement.

### Un métier à part entière

L'analyse de malware est devenue une spécialisation reconnue dans l'industrie de la cybersécurité, avec ses propres certifications (GREM du SANS, par exemple), ses conférences dédiées, et une demande forte sur le marché de l'emploi. Le RE est la compétence technique fondamentale de ce métier.

> 💡 **Lien avec la formation** — La Partie VI (chapitres 26 à 29) est entièrement consacrée à l'analyse de code malveillant en environnement contrôlé. Vous y analyserez un ransomware ELF et un dropper avec communication réseau — des samples créés par nos soins à des fins pédagogiques.

---

## Préservation et archéologie logicielle

### Sauver le patrimoine numérique

Le logiciel est un artefact culturel et technique fragile. Des programmes dont dépendent des industries entières peuvent devenir inutilisables parce que leur éditeur a fait faillite, parce que le système d'exploitation cible n'est plus supporté, ou parce que le matériel requis n'est plus fabriqué.

Le RE joue un rôle crucial dans la préservation de ce patrimoine :

**Émulation et rétrocompatibilité** — Les émulateurs de consoles de jeu, de micro-ordinateurs et de systèmes d'exploitation anciens reposent sur le RE du matériel et du logiciel système d'origine. Sans ce travail, des décennies de logiciels deviendraient irrémédiablement inaccessibles.

**Migration de systèmes industriels** — Des usines, des centrales électriques et des systèmes de transport fonctionnent parfois avec des logiciels de contrôle dont le code source a été perdu et dont l'éditeur a disparu. Le RE permet de comprendre la logique de ces systèmes pour les migrer vers des plateformes modernes sans rupture de fonctionnement.

**Récupération de données** — Des formats de fichiers propriétaires deviennent illisibles quand le logiciel qui les produisait n'est plus disponible. Le RE du format permet de développer des convertisseurs qui sauvent les données de l'obsolescence.

---

## Apprentissage et recherche académique

### Comprendre pour mieux construire

Le RE est un outil d'apprentissage remarquable. Désassembler un programme, c'est voir le résultat concret des choix faits par un compilateur, un développeur ou un architecte système. C'est passer de la théorie (« un appel de méthode virtuelle en C++ utilise une vtable ») à l'observation directe (« voici les instructions exactes que GCC génère pour résoudre cet appel »).

De nombreuses universités intègrent le RE dans leurs cursus de sécurité informatique et d'architecture des systèmes, précisément parce qu'il force les étudiants à comprendre les mécanismes à un niveau de détail que la programmation de haut niveau ne requiert jamais.

### Recherche académique

Le RE est également un outil de recherche dans plusieurs domaines :

- **Analyse de protocoles** — Des chercheurs reversent des protocoles réseau pour en évaluer la sécurité ou pour développer des modèles formels.  
- **Analyse de firmwares** — Le RE de firmwares d'objets connectés alimente la recherche sur la sécurité de l'IoT.  
- **Amélioration des compilateurs** — L'étude du code généré par différents compilateurs à différents niveaux d'optimisation contribue à la recherche sur l'optimisation de code.  
- **Détection de malware** — La recherche sur les techniques d'analyse automatique de malware (classification, détection de familles, analyse de similarité) repose sur le RE pour constituer des jeux de données annotés.

> 💡 **Lien avec la formation** — L'ensemble de cette formation est conçu dans une optique d'apprentissage par la pratique. Les chapitres 2 et 3 (compilation et assembleur) puis 16 et 17 (optimisations et C++) utilisent le RE comme outil de compréhension du fonctionnement interne des programmes.

---

## Synthèse : le RE est un outil, pas une intention

Le reverse engineering est une compétence technique neutre. Comme un tournevis, il peut être utilisé pour construire ou pour détruire. Ce qui distingue un usage légitime d'un usage illicite, c'est l'**intention**, le **contexte** et le **cadre juridique** dans lequel l'activité s'inscrit.

Les cas d'usage présentés dans cette section — audit de sécurité, CTF, débogage avancé, interopérabilité, analyse de malware, préservation, recherche — sont tous des contextes où le RE apporte une valeur claire et reconnue. Ce sont ces contextes que cette formation vous prépare à affronter.

| Cas d'usage | Chapitres principaux | Objectif |  
|---|---|---|  
| Audit de sécurité | 15, 19, 24, 26–29 | Trouver des vulnérabilités, vérifier des propriétés de sécurité |  
| CTF (reversing) | 21, 18 | Comprendre la logique d'un binaire, extraire un flag |  
| Débogage avancé | 11, 12, 14, 16 | Diagnostiquer un crash ou un comportement sans code source |  
| Interopérabilité | 23, 25 | Reverser un protocole ou un format de fichier non documenté |  
| Analyse de malware | 26–29 | Identifier les capacités, extraire les IOC, écrire des contre-mesures |  
| Apprentissage | 2, 3, 16, 17 | Comprendre la compilation, l'assembleur, les optimisations |

---

> 📖 **À retenir** — Le RE est pratiqué quotidiennement par des professionnels dans des contextes légitimes et reconnus. L'audit de sécurité, les CTF, le débogage sans code source, l'interopérabilité, l'analyse de malware et la recherche académique sont autant de domaines où cette compétence est valorisée et demandée. Cette formation vous prépare à chacun d'entre eux.

---


⏭️ [Différence entre RE statique et RE dynamique](/01-introduction-re/04-statique-vs-dynamique.md)
