🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 1.2 — Cadre légal et éthique

> **Chapitre 1 — Introduction au Reverse Engineering**  
> 📦 Aucun prérequis technique — section de lecture.

---

## Pourquoi parler de droit dans une formation technique ?

Le reverse engineering est une activité technique qui touche directement à des questions juridiques. Désassembler un logiciel, c'est accéder à sa logique interne d'une manière que l'éditeur n'a généralement pas prévue — et parfois pas souhaitée. Selon le contexte, la juridiction et les conditions d'utilisation du logiciel, cette activité peut être parfaitement légale, tolérée dans une zone grise, ou constituer une infraction.

Ignorer ce cadre juridique ne vous protège pas. Un professionnel de la sécurité qui publie une analyse de vulnérabilité, un chercheur qui présente un talk en conférence, un développeur qui reverse une bibliothèque tierce — tous peuvent se retrouver confrontés à des questions légales s'ils n'ont pas pris un minimum de précautions en amont.

Cette section n'a pas vocation à remplacer un avis juridique. Elle vise à vous donner les repères essentiels pour comprendre le paysage légal du RE, identifier les situations à risque, et savoir quand il est prudent de consulter un juriste. Les textes législatifs évoluent et leur interprétation varie selon les juridictions — ce qui suit est un état des lieux général, pas un conseil juridique.

> ⚠️ **Cette section décrit le cadre légal à titre informatif et éducatif. Elle ne constitue en aucun cas un avis juridique. En cas de doute sur la légalité d'une activité de RE dans votre contexte, consultez un avocat spécialisé en propriété intellectuelle ou en droit du numérique.**

---

## Les trois piliers juridiques à connaître

Trois familles de textes encadrent le RE dans la plupart des juridictions occidentales : la protection du droit d'auteur sur les logiciels, les lois anti-contournement des mesures techniques de protection, et les lois sur l'accès non autorisé aux systèmes informatiques. Chacune intervient à un niveau différent et peut s'appliquer — ou non — selon ce que vous faites et pourquoi vous le faites.

---

## Le droit d'auteur appliqué aux logiciels

### Le principe

Dans la quasi-totalité des juridictions, un logiciel est protégé par le droit d'auteur (ou *copyright*) dès sa création. Cette protection couvre le **code source** en tant qu'œuvre littéraire, mais aussi, dans de nombreux pays, les **éléments du programme** qui expriment des choix créatifs de l'auteur — ce qui inclut potentiellement la structure, l'organisation et certains aspects du code objet.

Concrètement, cela signifie que copier, modifier ou distribuer un logiciel (y compris sous forme binaire) sans autorisation de l'ayant droit constitue en principe une violation du droit d'auteur.

### Où se situe le RE ?

Le reverse engineering pose une question délicate au droit d'auteur : désassembler un binaire produit une représentation intermédiaire (du code assembleur, du pseudo-code) qui est techniquement une « reproduction » de l'œuvre protégée, même si cette représentation est très éloignée du code source original.

La réponse juridique varie selon les pays, mais la tendance générale est de reconnaître des **exceptions spécifiques** pour le RE lorsqu'il poursuit certains objectifs légitimes. Nous y reviendrons dans la section consacrée à chaque texte législatif.

### Les licences logicielles

Avant même de s'interroger sur la loi, il faut regarder la **licence** du logiciel que l'on souhaite analyser. La licence est un contrat entre l'éditeur et l'utilisateur qui définit les droits concédés et les restrictions imposées.

**Logiciels open source** — Les licences open source (MIT, GPL, BSD, Apache, etc.) accordent généralement le droit d'étudier le fonctionnement du programme, y compris par l'examen du code source. Le RE d'un binaire open source est donc rarement problématique sur le plan juridique : vous avez accès au code source, et la licence vous autorise explicitement à l'étudier. En pratique, le RE d'un binaire open source est surtout un exercice d'apprentissage (comparer le binaire au source est un excellent moyen de progresser).

**Logiciels propriétaires** — Les licences propriétaires (EULA — *End User License Agreement*) contiennent fréquemment des clauses qui interdisent explicitement le reverse engineering, le désassemblage et la décompilation. La force juridique de ces clauses varie selon les juridictions :

- En **Europe**, une clause contractuelle ne peut pas interdire la décompilation à des fins d'interopérabilité si les conditions prévues par la directive sont remplies (voir ci-dessous). La clause est tout simplement inopposable sur ce point.  
- Aux **États-Unis**, la situation est plus complexe. Les tribunaux ont parfois fait prévaloir le contrat (la EULA) sur les exceptions légales, et parfois l'inverse. Le contexte spécifique de chaque affaire pèse lourd dans la décision.

> 💡 **Règle pratique** — Lisez toujours la licence avant de reverser un logiciel propriétaire. Si elle interdit explicitement le RE et que vous n'êtes pas dans un cas d'exception légale clairement établi, vous prenez un risque juridique.

---

## Les États-Unis : DMCA et CFAA

### Le DMCA — Digital Millennium Copyright Act (1998)

Le **DMCA** est une loi fédérale américaine qui, entre autres dispositions, interdit le **contournement des mesures techniques de protection** (*Technical Protection Measures* — TPM) mises en place par un ayant droit pour contrôler l'accès à une œuvre protégée.

En d'autres termes : si un logiciel est protégé par un mécanisme anti-copie, un système de vérification de licence, du chiffrement ou toute autre mesure technique conçue pour en restreindre l'accès, le simple fait de contourner cette mesure peut constituer une violation du DMCA — indépendamment de ce que vous faites ensuite avec le logiciel.

Le DMCA interdit également la **fabrication et la distribution d'outils** dont l'objectif principal est de permettre le contournement de ces mesures. C'est cette disposition qui a historiquement suscité le plus de controverses dans la communauté de la sécurité informatique.

**Exceptions notables pour le RE :**

Le DMCA prévoit une exception spécifique pour le reverse engineering à l'article 17 U.S.C. § 1201(f). Cette exception autorise le contournement d'une mesure technique de protection **à des fins d'interopérabilité** entre programmes informatiques, sous certaines conditions strictes :

- La personne doit avoir obtenu légalement une copie du programme.  
- Le RE doit être nécessaire pour identifier et analyser les éléments du programme requis pour l'interopérabilité.  
- Les informations obtenues ne doivent pas être utilisées à d'autres fins ni mises à disposition de tiers de manière préjudiciable.

Au-delà de cette exception codifiée, le **Copyright Office** américain accorde périodiquement (tous les trois ans) des **exemptions temporaires** au DMCA via un processus de réglementation (*rulemaking*). Certaines de ces exemptions concernent directement la recherche en sécurité informatique. Depuis 2015, des exemptions ont été progressivement élargies pour couvrir la recherche de vulnérabilités de bonne foi (*good faith security research*), à condition de respecter des conditions sur la notification des vulnérabilités et l'absence de violation d'autres lois.

> ⚠️ **Les exemptions DMCA sont limitées dans le temps et dans leur portée.** Elles sont réévaluées tous les trois ans et leurs contours précis évoluent. Si vous faites de la recherche en sécurité sur des logiciels propriétaires aux États-Unis ou susceptible d'être soumise au droit américain, vérifiez les exemptions en vigueur au moment de votre recherche.

### Le CFAA — Computer Fraud and Abuse Act (1986, amendé)

Le **CFAA** est la principale loi fédérale américaine sur la cybercriminalité. À la différence du DMCA (qui porte sur la propriété intellectuelle et les mesures de protection), le CFAA porte sur l'**accès non autorisé** à des systèmes informatiques.

Le texte sanctionne quiconque « accède intentionnellement à un ordinateur sans autorisation, ou excède son autorisation d'accès » (*"intentionally accesses a computer without authorization, or exceeds authorized access"*). Les sanctions vont de l'amende à la peine de prison, selon la nature et la gravité de l'infraction.

**En quoi le CFAA concerne-t-il le RE ?**

En règle générale, le reverse engineering d'un binaire que vous avez installé localement sur votre propre machine ne relève pas du CFAA — vous accédez à votre propre ordinateur. Le CFAA devient pertinent lorsque le RE implique d'interagir avec un système distant (serveur, API, service cloud) de manière non prévue par le fournisseur, ou lorsque le binaire analysé a été obtenu en accédant à un système sans autorisation.

Le CFAA a fait l'objet de nombreuses critiques pour la **vague définition** de l'expression « excéder son autorisation d'accès ». Pendant des années, certains procureurs ont interprété cette notion de manière très large, au point qu'une simple violation des conditions d'utilisation d'un site web pouvait être poursuivie comme un crime fédéral.

Un arrêt important de la Cour suprême, **Van Buren v. United States (2021)**, a significativement restreint cette interprétation. La Cour a jugé que le CFAA ne s'applique qu'aux personnes qui accèdent à des informations situées dans des zones du système auxquelles elles n'ont pas du tout accès — et non aux personnes qui accèdent à des informations autorisées mais les utilisent à des fins non prévues. Cette décision a clarifié le paysage pour la recherche en sécurité, même si ses implications concrètes continuent d'être précisées par la jurisprudence.

> 💡 **En résumé pour le RE sous droit américain** — Le DMCA concerne le contournement de mesures de protection (DRM, vérifications de licence). Le CFAA concerne l'accès non autorisé à des systèmes. Les deux peuvent s'appliquer selon le contexte, et les exceptions pour la recherche en sécurité et l'interopérabilité existent mais sont encadrées par des conditions précises.

---

## L'Europe : directive EUCD et directive logiciel

### La directive EUCD — European Union Copyright Directive (2001/29/CE)

L'**EUCD** (parfois appelée *InfoSoc Directive*) est l'équivalent européen du DMCA en matière de mesures techniques de protection. Elle interdit le contournement des mesures techniques efficaces mises en place pour protéger une œuvre, ainsi que la fabrication et la distribution d'outils de contournement.

Comme le DMCA, l'EUCD prévoit des exceptions, mais elle **laisse aux États membres le soin de les transposer** dans leur droit national. Le niveau de protection et les exceptions varient donc d'un pays européen à l'autre.

En **France**, la transposition de l'EUCD est intégrée au Code de la propriété intellectuelle (articles L. 331-5 et suivants). La loi DADVSI (2006) a été le véhicule principal de cette transposition. Le contournement de mesures techniques de protection est sanctionné, avec des exceptions limitées (copie privée, accessibilité, sécurité informatique dans certaines conditions).

### La directive sur la protection juridique des programmes d'ordinateur (2009/24/CE)

C'est le texte le plus important pour le RE en Europe. Cette directive (qui a codifié et remplacé la directive 91/250/CEE de 1991) établit un **droit explicite à la décompilation** à des fins d'interopérabilité, sous conditions.

L'article 6 de la directive autorise la reproduction du code et la traduction de sa forme (c'est-à-dire la décompilation) **sans l'autorisation de l'ayant droit** lorsque les trois conditions cumulatives suivantes sont réunies :

1. **L'acte est accompli par une personne licenciée** (qui a le droit d'utiliser une copie du programme) ou par une personne autorisée par le licencié.  
2. **Les informations nécessaires à l'interopérabilité ne sont pas facilement et rapidement accessibles** par d'autres moyens (documentation publique, API ouvertes, etc.).  
3. **L'acte se limite aux parties du programme nécessaires à l'interopérabilité.**

De plus, les informations obtenues par cette décompilation ne peuvent pas être utilisées à d'autres fins que l'interopérabilité, ne peuvent pas être communiquées à des tiers (sauf si c'est nécessaire pour l'interopérabilité), et ne peuvent pas servir à développer un programme concurrent substantiellement similaire.

Un arrêt de la Cour de justice de l'Union européenne (CJUE) a apporté une précision significative en 2021 dans l'affaire **Top System SA c. État belge (C-13/20)** : la décompilation peut être autorisée non seulement pour assurer l'interopérabilité, mais aussi pour **corriger des erreurs** affectant le fonctionnement du programme, y compris des failles de sécurité. La Cour a élargi l'interprétation de l'article 5(1) de la directive, en jugeant que le licencié a le droit de décompiler le programme dans la mesure nécessaire à la correction de ces erreurs.

> 💡 **En résumé pour le RE en Europe** — Le cadre européen est globalement plus favorable au RE que le cadre américain, notamment grâce au droit explicite de décompilation pour interopérabilité (et désormais pour correction d'erreurs). Cependant, ce cadre impose des conditions strictes et ne couvre pas tous les objectifs de RE (la recherche en sécurité offensive pure, par exemple, n'est pas explicitement couverte par l'exception d'interopérabilité). La transposition varie par pays — vérifiez votre droit national.

---

## Autres juridictions

Le paysage juridique varie considérablement selon les pays. Quelques repères sans prétention d'exhaustivité :

**Royaume-Uni** — Le *Copyright, Designs and Patents Act 1988* (amendé) contenait des dispositions similaires aux directives européennes. Depuis le Brexit, le Royaume-Uni dispose de son propre cadre, qui reprend largement les exceptions pour l'interopérabilité et la recherche en sécurité, mais peut évoluer indépendamment du droit de l'UE.

**Japon** — La loi japonaise sur le droit d'auteur autorise le RE à des fins de recherche sur la sécurité depuis une révision de 2018, ce qui en fait l'une des juridictions les plus permissives pour la recherche en sécurité informatique.

**Chine** — La protection du logiciel est assurée par un règlement spécifique sur la protection des droits d'auteur des logiciels. Les exceptions pour le RE sont limitées et la jurisprudence est en développement.

> ⚠️ Ce tour d'horizon est nécessairement incomplet. Si vous pratiquez le RE dans un cadre professionnel ou si vos résultats sont destinés à être publiés, vérifiez le droit applicable dans votre juridiction.

---

## Le cadre éthique : au-delà du droit

Respecter la loi est un minimum, pas un objectif suffisant. Le reverse engineering soulève des questions éthiques que le droit ne tranche pas toujours.

### Divulgation responsable (*responsible disclosure*)

Si vous découvrez une vulnérabilité dans un logiciel au cours d'une analyse de RE, la question de la divulgation se pose immédiatement. La pratique standard dans la communauté de la sécurité est la **divulgation responsable** (également appelée *coordinated disclosure*) :

1. Vous contactez l'éditeur du logiciel en privé pour lui signaler la vulnérabilité.  
2. Vous lui accordez un délai raisonnable pour développer et déployer un correctif (90 jours est un standard courant, popularisé par Google Project Zero).  
3. Vous publiez les détails de la vulnérabilité une fois le correctif disponible, ou à l'expiration du délai si l'éditeur n'a pas réagi.

Cette pratique n'est pas toujours une obligation légale (bien que certaines réglementations sectorielles l'imposent), mais c'est un standard éthique largement reconnu. Publier une vulnérabilité sans laisser à l'éditeur le temps de corriger (*full disclosure* immédiate) expose les utilisateurs du logiciel à un risque d'exploitation, tandis que ne jamais la divulguer laisse la vulnérabilité ouverte indéfiniment.

### Respect de la finalité

Le fait de pouvoir reverser un logiciel ne signifie pas que tous les usages de l'information obtenue soient acceptables. Quelques principes de base :

- **Ne pas utiliser le RE pour voler de la propriété intellectuelle.** Comprendre le fonctionnement d'un algorithme pour assurer l'interopérabilité est différent de copier cet algorithme pour développer un clone concurrent.  
- **Ne pas distribuer de logiciels piratés.** Comprendre comment fonctionne une vérification de licence à des fins d'apprentissage (comme dans les cas pratiques de cette formation) est différent de distribuer un crack au public.  
- **Ne pas exploiter les vulnérabilités découvertes.** Identifier une faille dans un logiciel dans le cadre d'un audit autorisé est légitime. L'exploiter contre des systèmes tiers sans autorisation est une infraction pénale, quelle que soit la juridiction.

### Le cas particulier de cette formation

Dans cette formation, tous les binaires analysés sont **compilés par vous** à partir de sources fournies, ou sont des binaires créés spécifiquement à des fins pédagogiques. Vous ne reverserez aucun logiciel propriétaire, et aucun sample malveillant réel n'est distribué.

Ce cadre élimine la quasi-totalité des risques juridiques : vous analysez vos propres programmes, sur votre propre machine, à des fins d'apprentissage. C'est l'équivalent numérique de démonter votre propre montre pour comprendre comment elle fonctionne.

Les compétences que vous développerez ici devront ensuite être appliquées dans le respect du cadre légal et éthique de votre juridiction et de votre contexte professionnel.

---

## Récapitulatif des textes clés

| Texte | Juridiction | Porte sur | Exception RE notable |  
|---|---|---|---|  
| DMCA (17 U.S.C. § 1201) | États-Unis | Contournement de mesures techniques | § 1201(f) : interopérabilité ; exemptions triennales pour la recherche en sécurité |  
| CFAA (18 U.S.C. § 1030) | États-Unis | Accès non autorisé à un système | Pas d'exception RE explicite ; *Van Buren* (2021) a restreint la portée du texte |  
| EUCD (2001/29/CE) | Union européenne | Contournement de mesures techniques | Transposition nationale variable ; exceptions possibles pour sécurité et interopérabilité |  
| Directive 2009/24/CE art. 6 | Union européenne | Décompilation de logiciels | Droit de décompilation pour interopérabilité (et correction d'erreurs depuis *Top System*, 2021) |  
| DADVSI (2006) | France | Transposition EUCD | Exceptions limitées ; voir Code de la propriété intellectuelle art. L. 331-5 et suivants |

---

> 📖 **À retenir** — Le RE est légal dans de nombreux contextes, mais il n'est pas un droit absolu. Le cadre juridique dépend de votre juridiction, de la licence du logiciel, de l'objectif poursuivi et de la présence éventuelle de mesures de protection technique. En Europe, le droit de décompilation pour interopérabilité est explicitement protégé. Aux États-Unis, les exceptions DMCA et la jurisprudence CFAA offrent un espace pour la recherche en sécurité, mais avec des conditions strictes. Dans le doute, consultez un juriste avant de reverser un logiciel propriétaire dans un cadre professionnel.

---


⏭️ [Cas d'usage légitimes : audit de sécurité, CTF, débogage avancé, interopérabilité](/01-introduction-re/03-cas-usage-legitimes.md)
