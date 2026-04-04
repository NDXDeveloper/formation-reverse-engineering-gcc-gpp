🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 36.4 — Parcours de certification : GREM (SANS), OSED (OffSec)

> 📁 `36-ressources-progresser/04-certifications.md`

---

## Faut-il passer une certification en RE ?

La question se pose légitimement : le reverse engineering est un domaine où la compétence se démontre avant tout par la pratique — un write-up de CTF bien documenté, une analyse de malware publiée, un outil open source développé. Les recruteurs spécialisés en sécurité savent lire un profil technique et ne se fient pas uniquement aux certifications.

Cela dit, les certifications apportent des bénéfices concrets dans certains contextes. Elles **structurent l'apprentissage** autour d'un programme validé par des professionnels du domaine, ce qui évite les angles morts. Elles fournissent un **signal crédible** pour les employeurs généralistes (grandes entreprises, administrations, défense) qui ne disposent pas toujours de l'expertise interne pour évaluer un profil RE sur la base d'un portfolio. Et elles imposent une **échéance et un niveau d'exigence** qui poussent à franchir des paliers qu'on aurait pu repousser indéfiniment en auto-formation.

Les deux certifications les plus directement pertinentes pour le reverse engineering sont le **GREM** (GIAC/SANS), orienté analyse de malware, et l'**OSED** (OffSec), orienté exploitation et développement d'exploits. Elles couvrent des angles complémentaires du RE et s'adressent à des profils différents.

---

## GREM — GIAC Reverse Engineering Malware

### Vue d'ensemble

| | |  
|---|---|  
| **Organisme** | GIAC (Global Information Assurance Certification), affilié à SANS |  
| **Formation associée** | FOR610: Reverse-Engineering Malware (SANS) |  
| **Focus** | Analyse et reverse engineering de logiciels malveillants |  
| **Niveau** | Intermédiaire à avancé |  
| **Format de l'examen** | 66-75 questions, 3 heures, examen surveillé (proctored) en ligne |  
| **Score requis** | 73% minimum |  
| **Open book** | Oui (notes imprimées autorisées) |  
| **Coût total estimé** | ~2 800 USD (examen seul) à ~9 000+ USD (formation SANS + examen) |  
| **Renouvellement** | Tous les 4 ans (via CPE ou repassage) |  
| **Site** | [giac.org/certifications/reverse-engineering-malware-grem](https://www.giac.org/certifications/reverse-engineering-malware-grem/) |

### Contenu couvert

Le GREM valide la capacité à analyser et désassembler des logiciels malveillants ciblant les plateformes courantes (principalement Windows). L'examen évalue les compétences suivantes :

Les **fondamentaux de l'analyse de malware** : mise en place d'un laboratoire d'analyse, méthodologie de triage, analyse comportementale de base. Cela correspond directement à ce que nous avons couvert aux chapitres 26 (lab sécurisé) et 27 (triage rapide).

L'**assembleur x86 Windows et l'analyse statique** : lecture et interprétation de code désassemblé, identification des constructions C en assembleur (boucles, conditions, appels de fonctions), utilisation de désassembleurs. C'est le prolongement direct de nos chapitres 3 (assembleur x86-64), 7 (objdump) et 8 (Ghidra), transposé à l'écosystème Windows.

L'**analyse dynamique** : utilisation de débogueurs pour suivre l'exécution d'un sample malveillant, inspection de la mémoire et des registres. Chapitres 11-12 de notre formation.

L'**identification et le contournement des techniques anti-analyse** : anti-debug, anti-VM, détection d'outils de sécurité, obfuscation du flux de contrôle. Chapitre 19.

L'**analyse d'exécutables protégés** : packing, unpacking, reconstruction de binaires. Chapitre 29.

L'**analyse de documents malveillants et de programmes .NET**. Chapitres 30-32.

Depuis 2022, l'examen inclut des questions **CyberLive** — des exercices pratiques en environnement simulé où le candidat doit effectuer des actions concrètes d'analyse (et pas seulement répondre à des QCM).

### À qui s'adresse le GREM

Le GREM est conçu pour les analystes de malware, les analystes SOC avancés, les spécialistes en réponse à incidents, et les chercheurs en menaces. Si votre objectif professionnel s'oriente vers l'analyse de code malveillant (en CERT, en threat intelligence, ou dans une équipe de réponse à incidents), le GREM est la certification la plus reconnue dans ce créneau.

### Préparation

GIAC ne requiert officiellement aucun prérequis. En pratique, l'examen suppose une aisance significative avec l'assembleur x86, les outils de désassemblage et de débogage, et une compréhension du fonctionnement des systèmes Windows.

La voie la plus courante est de suivre la formation SANS **FOR610** (Reverse-Engineering Malware: Malware Analysis Tools and Techniques), dispensée en présentiel ou en ligne. C'est une formation de très haute qualité, mais le coût est élevé (plusieurs milliers de dollars). L'alternative est l'auto-formation à partir des livres recommandés en section 36.2 (*Practical Malware Analysis* de Sikorski et Honig, en priorité) combinée à la pratique régulière sur les plateformes de la section 36.1.

L'examen étant open book, la préparation d'un **index personnel** des notes de cours est une compétence à part entière. Les candidats qui réussissent le mieux sont ceux qui ont construit un index structuré permettant de retrouver rapidement une information pendant l'examen.

---

## OSED — OffSec Exploit Developer

### Vue d'ensemble

| | |  
|---|---|  
| **Organisme** | OffSec (anciennement Offensive Security) |  
| **Formation associée** | EXP-301: Windows User Mode Exploit Development |  
| **Focus** | Développement d'exploits et reverse engineering de vulnérabilités |  
| **Niveau** | Intermédiaire à avancé |  
| **Format de l'examen** | 3 cibles à exploiter, 47h45 d'examen pratique + 24h pour le rapport |  
| **Score requis** | Exploitation réussie des cibles avec documentation complète |  
| **Open book** | Oui (sauf chatbots IA et LLMs) |  
| **Outils imposés** | WinDbg (débogueur), IDA Free (désassembleur), Python 3 (exploits) |  
| **Coût** | Inclus dans l'abonnement OffSec (Learn One : ~$2 499/an, incluant cours + labs + 2 tentatives) |  
| **Site** | [offsec.com/courses/exp-301](https://www.offsec.com/courses/exp-301/) |

### Contenu couvert

L'OSED valide la capacité à reverse-engineerer des applications Windows pour y trouver des vulnérabilités, puis à développer des exploits fonctionnels contournant les protections modernes. Le cours EXP-301 est structuré en 13 modules progressifs :

Le **reverse engineering de binaires Windows** : utilisation d'IDA Free et WinDbg pour analyser le fonctionnement interne d'applications, tracer les entrées utilisateur à travers le code, et identifier les vulnérabilités exploitables. C'est la transposition directe à Windows des techniques d'analyse statique et dynamique vues dans les parties II et III de cette formation.

L'**exploitation de stack buffer overflows et SEH overflows** : compréhension de la pile x86, écriture d'exploits classiques puis contournement des mécanismes de gestion d'exceptions Windows. Prolongement des concepts du chapitre 3 (pile, prologue/épilogue) et du chapitre 19 (protections).

Le **développement de shellcode custom** : écriture de code assembleur pour obtenir un shell distant, gestion des contraintes d'espace (egghunters) et des restrictions de caractères. Cela pousse la maîtrise de l'assembleur x86 bien au-delà de la lecture passive que nous avons pratiquée dans cette formation.

Le **contournement de DEP et ASLR** : construction de chaînes ROP (Return-Oriented Programming) avancées pour contourner la prévention d'exécution des données, et techniques de fuite d'adresses mémoire pour vaincre la randomisation de l'espace d'adressage. Prolongement direct du chapitre 19.5 (ASLR, PIE, NX).

L'**exploitation de vulnérabilités de format string** : reverse engineering d'un protocole réseau, construction de primitives de lecture et d'écriture arbitraires à travers des spécificateurs de format.

### L'examen : 48 heures d'épreuve pratique

L'examen OSED est radicalement différent d'un QCM. Le candidat reçoit trois cibles indépendantes à exploiter en 47 heures et 45 minutes, suivies de 24 heures pour rédiger un rapport technique détaillé. Chaque cible nécessite du reverse engineering (obligatoirement avec IDA Free et WinDbg), l'écriture d'un exploit fonctionnel en Python 3, et l'obtention d'un shell distant comme preuve d'exploitation.

L'examen est open book (notes, ressources en ligne, plateforme OffSec) mais interdit l'utilisation de chatbots IA et de LLMs. Tous les exploits doivent être écrits en Python 3, et les outils de désassemblage sont limités à IDA Free — Ghidra n'est pas autorisé.

Les retours des candidats sont unanimes : l'examen est extrêmement exigeant. Les témoignages mentionnent régulièrement 36 à 48 heures de travail quasiment continu, combinant reverse engineering, développement d'exploits, contournement de protections, et rédaction du rapport.

### À qui s'adresse l'OSED

L'OSED s'adresse aux pentesters, red teamers, chercheurs en vulnérabilités et analystes de malware qui souhaitent maîtriser le développement d'exploits. Si votre objectif est de comprendre non seulement comment fonctionne un binaire (RE classique) mais aussi comment l'exploiter quand il contient une vulnérabilité, l'OSED est le choix pertinent.

L'OSED fait partie du trio **OSCE³** d'OffSec (avec OSEP pour le pentesting avancé et OSWE pour la sécurité web), qui constitue le niveau le plus élevé de certification OffSec.

### Préparation

OffSec recommande une solide maîtrise préalable de la programmation C, de l'assembleur x86, des internals Windows et des outils de débogage. Le cours EXP-301 part des fondamentaux de l'exploitation binaire mais la courbe d'apprentissage est raide.

Avoir complété cette formation (en particulier les chapitres 3, 11, 16 et 19) constitue une bonne préparation aux concepts fondamentaux. Le delta principal sera l'orientation Windows (vs Linux dans notre formation) et le passage de l'analyse pure à l'exploitation active.

---

## Comparaison GREM vs OSED

| Critère | GREM | OSED |  
|---|---|---|  
| **Orientation** | Analyse défensive de malware | Exploitation offensive de vulnérabilités |  
| **Compétence centrale** | Comprendre ce que fait un malware | Exploiter un binaire vulnérable |  
| **RE dans l'examen** | Analyse de samples malveillants | RE pour trouver et exploiter des vulnérabilités |  
| **Plateforme cible** | Windows (principalement) | Windows (exclusivement) |  
| **Format d'examen** | QCM + CyberLive (3h) | 100% pratique (~48h + rapport) |  
| **Difficulté perçue** | Élevée | Très élevée |  
| **Coût** | Élevé (formation SANS) | Inclus dans l'abonnement OffSec |  
| **Débouchés typiques** | Analyste malware, CERT, threat intel, SOC avancé | Pentester avancé, red team, vuln research |  
| **Prérequis réels** | Assembleur x86, outils RE, connaissance Windows | Assembleur x86, programmation C, débogage, bases exploit |

Les deux certifications sont complémentaires, pas concurrentes. Le GREM forme des analystes qui comprennent les menaces ; l'OSED forme des opérateurs qui savent les reproduire. Choisissez en fonction de votre orientation professionnelle.

---

## Autres certifications à considérer

Au-delà du GREM et de l'OSED, d'autres certifications touchent au reverse engineering de manière plus ou moins directe :

**GXPN (GIAC Exploit Researcher and Advanced Penetration Tester)** — Certification GIAC de niveau expert couvrant le développement d'exploits, le fuzzing, et les techniques avancées de pentesting. Plus large que l'OSED, elle inclut du RE appliqué à l'exploitation sur plusieurs plateformes. Formation associée : SANS SEC760.

**OSEE (OffSec Exploitation Expert)** — Le niveau le plus avancé d'OffSec en exploitation, couvrant l'exploitation kernel Windows et les techniques d'évasion avancées. L'OSEE est généralement considérée comme l'une des certifications les plus difficiles de l'industrie. La formation associée (EXP-401 / AWE) n'est dispensée qu'en présentiel (historiquement à Black Hat et lors de conférences partenaires), mais l'examen est en ligne : 72 heures pour découvrir et exploiter des vulnérabilités inconnues, suivies de 24 heures pour le rapport.

**eCRE (eLearnSecurity Certified Reverse Engineer)** — Proposée par INE (anciennement eLearnSecurity), cette certification couvre le reverse engineering sur x86/x64 avec une approche progressive. Le format d'examen est pratique. C'est une option plus accessible que le GREM ou l'OSED, tant en termes de difficulté que de coût.

**CompTIA CySA+ et CASP+** — Ces certifications généralistes en cybersécurité incluent des questions sur l'analyse de malware et le RE de base, mais ne constituent pas une validation approfondie des compétences en reverse engineering. Elles sont davantage pertinentes comme socle généraliste que comme spécialisation RE.

---

## Recommandations pratiques

**Si vous débutez votre carrière en sécurité** : ne vous précipitez pas sur une certification RE. Investissez d'abord dans la pratique (CTF, challenges, projets personnels) et dans la construction d'un portfolio (section 36.5). Une certification prend toute sa valeur quand elle valide des compétences déjà acquises, pas quand elle les remplace.

**Si vous visez un poste en analyse de malware** : le GREM est le choix le plus direct. Préparez-le en combinant *Practical Malware Analysis* (section 36.2) avec la pratique sur les challenges de la Partie VI de cette formation et les plateformes comme Hack The Box (packs Malware Reversing).

**Si vous visez un poste en pentesting avancé ou red team** : l'OSED est la suite logique, idéalement après avoir obtenu l'OSCP. Le cours EXP-301 constitue par ailleurs une excellente formation au RE appliqué, indépendamment de l'examen.

**Si votre employeur finance la formation** : profitez-en. Le coût est le principal frein de ces certifications (en particulier SANS/GIAC). Si l'opportunité de suivre FOR610 ou EXP-301 sur budget employeur se présente, c'est un investissement qui vaut la peine — la qualité pédagogique de ces formations est largement reconnue.

**Contexte francophone** : en France, les certifications GIAC (dont le GREM) sont particulièrement valorisées dans l'écosystème défense et institutionnel (ANSSI, DGA, COMCYBER, prestataires PRIS/PASSI). Les certifications OffSec (OSED, OSCP) sont davantage reconnues dans le milieu du pentesting et des entreprises de conseil en sécurité. Pour les postes en analyse de malware dans les CERT français (CERT-FR, CERTs sectoriels), le GREM ou une expérience documentée équivalente (portfolio, contributions publiées) est un atout significatif.

**Dans tous les cas** : la certification est un jalon, pas une fin. Les compétences en RE se maintiennent par la pratique régulière, et les certifications les plus respectées (GREM, OSED) perdent leur crédibilité si elles ne sont pas accompagnées d'une activité technique continue.

---

**Section suivante : 36.5 — Construire son portfolio RE : documenter ses analyses**

⏭️ [Construire son portfolio RE : documenter ses analyses](/36-ressources-progresser/05-construire-portfolio.md)
