🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 36.5 — Construire son portfolio RE : documenter ses analyses

> 📁 `36-ressources-progresser/05-construire-portfolio.md`

---

## Pourquoi un portfolio est votre meilleur atout

Dans un domaine aussi spécialisé que le reverse engineering, le CV classique a ses limites. Lister « Ghidra », « GDB » et « analyse de malware » dans une section compétences ne dit rien de votre capacité réelle à analyser un binaire inconnu. Ce qui convainc un recruteur, un responsable d'équipe CERT ou un client potentiel, c'est de **voir comment vous raisonnez** face à un problème technique concret.

Un portfolio RE est une collection de documents qui démontrent votre processus d'analyse : comment vous abordez un binaire, quels outils vous utilisez et pourquoi, comment vous surmontez les obstacles, et comment vous communiquez vos résultats. Chaque write-up, chaque rapport d'analyse, chaque outil que vous publiez est une preuve tangible de votre niveau.

Le portfolio sert trois objectifs distincts. Il est un **outil de recrutement** — les employeurs spécialisés (CERT, threat intel, éditeurs de sécurité, red teams) le lisent avec attention et préfèrent souvent un candidat avec un blog technique actif à un candidat avec trois certifications mais aucune production visible. Il est un **outil d'apprentissage** — le fait de rédiger force à structurer sa pensée, à vérifier ses hypothèses et à combler ses lacunes. On comprend mieux ce qu'on est capable d'expliquer clairement. Enfin, il est une **contribution à la communauté** — vos write-ups aident d'autres analystes à progresser, exactement comme les write-ups d'autres vous ont aidé (section 36.1).

---

## Que mettre dans un portfolio RE

### Write-ups de CTF

C'est le format le plus naturel pour commencer. Chaque challenge de CTF que vous résolvez est un candidat potentiel pour un write-up. Le format est simple : description du challenge, analyse progressive (avec captures d'écran des outils), solution, et enseignements tirés.

Les meilleurs write-ups ne se contentent pas de montrer la solution — ils documentent aussi les **fausses pistes** explorées et les **raisonnements intermédiaires**. Un recruteur qui lit votre write-up veut comprendre comment vous pensez, pas simplement vérifier que vous avez trouvé le flag. Montrer que vous avez d'abord tenté une approche statique avec Ghidra, que vous avez buté sur une routine obfusquée, que vous êtes passé en dynamique avec GDB, puis que vous avez finalement utilisé angr pour résoudre la contrainte — ce parcours est bien plus révélateur qu'un script de 10 lignes qui donne le flag.

Quelques principes pour un write-up de qualité :

Commencez par le **triage** — montrez que vous avez le réflexe de lancer `file`, `strings`, `checksec` avant de plonger dans le désassembleur. C'est le workflow du chapitre 5.7, et les recruteurs vérifient que ce réflexe est acquis.

Incluez des **captures d'écran annotées** de vos outils (Ghidra, GDB, ImHex…). Une capture du graphe de flux de contrôle dans Ghidra avec vos renommages de fonctions et vos commentaires montre votre capacité à rendre lisible un binaire strippé.

Expliquez le **pourquoi** de chaque décision, pas seulement le quoi. « J'ai posé un breakpoint sur `strcmp` parce que `strings` avait révélé des messages de succès/échec, ce qui suggère une comparaison de mot de passe » est infiniment plus utile que « j'ai posé un breakpoint sur `strcmp` ».

Terminez par une section **enseignements** où vous notez ce que ce challenge vous a appris et ce que vous feriez différemment la prochaine fois.

---

### Rapports d'analyse de malware

Si vous vous orientez vers l'analyse de malware (prolongement de notre Partie VI), les rapports d'analyse sont la pièce maîtresse de votre portfolio. Le chapitre 27.7 de cette formation couvrait la rédaction d'un rapport d'analyse type — le portfolio est l'endroit où vous publiez ces rapports.

Un rapport d'analyse de malware professionnel suit une structure que les employeurs reconnaissent :

Le **résumé exécutif** — deux ou trois phrases accessibles à un non-technicien, décrivant ce que fait le sample et le risque qu'il représente.

Les **indicateurs de compromission (IOC)** — hashes du sample (MD5, SHA-256), adresses IP/domaines contactés, fichiers créés ou modifiés, clés de registre touchées. Ce sont les éléments directement exploitables par les équipes de défense.

L'**analyse technique détaillée** — le cœur du rapport, où vous décrivez votre processus de reverse engineering étape par étape : analyse statique (Ghidra, ImHex), analyse dynamique (GDB, Frida), identification des routines crypto, extraction des clés, reconstruction du protocole C2, etc.

Les **règles de détection** — règles YARA, signatures Snort/Suricata, ou indicateurs Sigma que vous avez développés à partir de votre analyse. Produire des règles de détection démontre que vous savez transformer une analyse en protection concrète.

> ⚠️ **Important** : ne publiez des rapports d'analyse que sur des samples que vous avez vous-même créés (comme ceux de notre Partie VI) ou sur des samples publiquement disponibles dans des bases comme MalwareBazaar. Ne publiez jamais d'analyse sur des samples obtenus dans un contexte professionnel sans autorisation explicite.

---

### Outils et scripts

Chaque script utilitaire que vous développez pendant votre pratique du RE est un candidat pour votre portfolio. Les types de contributions les plus valorisés :

Les **scripts d'automatisation** — un script Python qui automatise le triage d'un répertoire de binaires (chapitre 35 checkpoint), un plugin Ghidra qui identifie des patterns spécifiques, un script Frida réutilisable pour hooker une catégorie de fonctions.

Les **parseurs et outils** — un parseur Python pour un format de fichier que vous avez reverse-engineeré (chapitre 25), un pattern `.hexpat` pour ImHex, un déchiffreur pour un schéma crypto spécifique.

Les **règles YARA** — un ensemble de règles pour détecter des familles de malware ou des patterns de compilateur spécifiques.

Publiez ces outils sur GitHub avec un README clair qui explique le problème résolu, l'utilisation, et les limitations. Un dépôt GitHub bien tenu est un signal fort pour un employeur technique.

---

### Articles techniques et recherches originales

Au-delà des write-ups de CTF et des rapports d'analyse, vous pouvez publier des articles techniques sur des sujets de RE que vous avez approfondis. Quelques exemples de sujets qui constituent d'excellentes pièces de portfolio :

Une comparaison détaillée du code assembleur produit par GCC et Clang pour un même programme C (prolongement du chapitre 16.7). Un article documentant le reverse engineering d'un protocole réseau propriétaire (chapitre 23). Une analyse des structures internes d'un binaire Rust ou Go strippé (chapitres 33-34). Un tutoriel sur l'utilisation d'angr pour résoudre une catégorie spécifique de crackmes (chapitre 18).

Ce type de contenu démontre une capacité d'approfondissement et de communication qui va au-delà de la résolution ponctuelle de challenges.

---

## Où publier

### Blog personnel

Un blog personnel est le format le plus flexible et le plus pérenne. Vous contrôlez entièrement le contenu, la mise en page et l'archivage. Plusieurs options techniques s'offrent à vous :

**GitHub Pages + Jekyll/Hugo** — Gratuit, hébergé sur GitHub, contenu en Markdown. C'est le choix le plus populaire dans la communauté sécurité. Vos articles vivent dans un dépôt Git, ce qui facilite le versionnement et la contribution. De très nombreux chercheurs en RE utilisent ce format (n1ght-w0lf, clearbluejar, vaktibabat…).

**Un site statique auto-hébergé** — Pour ceux qui souhaitent plus de contrôle. Les générateurs de sites statiques (Hugo, Eleventy, Zola) produisent des sites rapides et sans dépendance à un service tiers.

**Medium / Hashnode / dev.to** — Plus simple à mettre en place, mais vous dépendez d'une plateforme tierce et la mise en forme du code technique est parfois limitée.

Quel que soit le support choisi, l'essentiel est de **commencer**. Un blog avec trois write-ups bien rédigés a plus de valeur qu'un blog parfaitement designé mais vide.

---

### GitHub

Votre profil GitHub est un complément naturel au blog. Organisez vos dépôts par catégorie :

Un dépôt `ctf-writeups` regroupant vos write-ups par compétition et par année. Un dépôt par outil ou script significatif que vous avez développé. Un dépôt pour vos règles YARA et patterns ImHex. Un éventuel dépôt `re-notes` où vous consignez vos notes de recherche et vos cheat sheets personnelles.

Soignez les README : un dépôt sans README est un dépôt que personne ne regarde. Un bon README explique en une phrase ce que fait le projet, montre un exemple d'utilisation, et liste les prérequis.

---

### Partage communautaire

Une fois vos write-ups publiés, partagez-les sur les canaux de la communauté (section 36.3) : r/ReverseEngineering, les forums de CTF concernés, Mastodon/infosec.exchange, le Discord de crackmes.one. Le feedback de la communauté est précieux — il corrige vos erreurs, suggère des approches alternatives, et augmente la visibilité de votre travail.

---

## Structure type d'un write-up RE

Voici un squelette réutilisable pour structurer vos write-ups de manière cohérente :

**Titre et métadonnées** — Nom du challenge/sample, plateforme (CTF, crackmes.one, HTB…), date, difficulté, catégorie (RE, pwn, malware…), outils utilisés.

**Contexte** — D'où vient ce binaire, quel est l'objectif (trouver le flag, écrire un keygen, analyser le comportement, déchiffrer un fichier…).

**Triage initial** — Résultats de `file`, `strings`, `checksec`, `readelf`/`objdump`. Premières hypothèses.

**Analyse statique** — Chargement dans le désassembleur, identification des fonctions clés, renommage, reconstruction des structures. Captures d'écran annotées.

**Analyse dynamique** (si applicable) — Sessions GDB/Frida, breakpoints posés, valeurs observées, comportement runtime.

**Résolution** — Le raisonnement qui mène à la solution. Le code du keygen, du déchiffreur, ou l'exploit fonctionnel.

**Enseignements** — Ce que ce challenge a enseigné, les difficultés rencontrées, les pistes abandonnées, ce que vous feriez différemment.

Cette structure n'est pas rigide — adaptez-la au contexte. Un rapport de malware aura une structure différente d'un write-up de crackme. Mais la constante est la **transparence du raisonnement** : montrez comment vous pensez, pas seulement ce que vous faites.

---

## Les erreurs à éviter

**Publier uniquement les challenges faciles.** Un portfolio composé exclusivement de challenges de difficulté 1 n'impressionne personne. Incluez des analyses où vous avez dû chercher, tâtonner, et apprendre quelque chose de nouveau. Les write-ups les plus intéressants sont souvent ceux où l'auteur a galéré.

**Omettre les échecs.** Un write-up partiel — « voilà jusqu'où je suis arrivé, voilà où je suis bloqué » — a de la valeur. Il montre votre méthodologie et votre honnêteté intellectuelle. La communauté RE respecte les gens qui documentent leurs échecs autant que leurs succès.

**Négliger la rédaction.** Un write-up mal structuré, sans captures d'écran, avec du code non commenté et des phrases du type « et ensuite j'ai fait un truc et ça a marché » n'a aucune valeur démonstrative. Prenez le temps de rédiger. Relisez. Faites relire si possible.

**Publier des informations confidentielles.** Ne publiez jamais d'analyse issue d'un contexte professionnel (engagement client, incident en cours, sample interne) sans autorisation. C'est une faute professionnelle grave qui peut avoir des conséquences juridiques et détruire une réputation.

**Attendre d'être « assez bon ».** Le syndrome de l'imposteur est omniprésent en RE. Tout le monde a l'impression de ne pas en savoir assez pour publier. La réalité est que votre write-up de crackme niveau 2 aidera quelqu'un qui est niveau 1, et que le processus de rédaction vous fera progresser vous-même. Publiez tôt, publiez régulièrement, améliorez-vous en chemin.

---

## Plan d'action concret

Pour transformer cette section en résultats tangibles, voici un plan sur trois mois :

**Mois 1** — Créez votre support de publication (blog GitHub Pages ou équivalent). Rédigez et publiez votre premier write-up sur un challenge CTF que vous avez déjà résolu pendant cette formation. Un seul write-up bien fait suffit pour commencer.

**Mois 2** — Publiez deux write-ups supplémentaires, en variant les types (un crackme, un challenge réseau ou crypto, un rapport d'analyse sur un des samples de la Partie VI). Créez un dépôt GitHub pour vos scripts et outils.

**Mois 3** — Partagez vos publications sur r/ReverseEngineering ou le Discord de crackmes.one. Intégrez le lien de votre blog dans votre profil professionnel (LinkedIn, CV). Résolvez un nouveau challenge spécifiquement pour le documenter — choisir un challenge en se disant « je vais écrire un write-up dessus » change profondément l'attention que l'on porte à chaque étape.

À partir de là, maintenez un rythme d'un ou deux write-ups par mois. En un an, vous aurez un portfolio d'une quinzaine de pièces couvrant des techniques variées — un signal très fort pour n'importe quel employeur du domaine.

---

## Ce que les recruteurs regardent vraiment

Pour conclure cette section avec un angle concret, voici ce que les responsables d'équipes RE et analyse de malware cherchent quand ils consultent un portfolio de candidat :

La **méthodologie** — Le candidat suit-il une approche structurée ou fonce-t-il tête baissée dans le désassembleur ? Le triage initial est-il présent ?

La **rigueur** — Les affirmations sont-elles vérifiées ? Le candidat distingue-t-il ses hypothèses de ses certitudes ? Les captures d'écran correspondent-elles au texte ?

La **communication** — Le write-up est-il lisible par quelqu'un qui n'a pas le binaire sous les yeux ? Les étapes sont-elles reproductibles ?

La **curiosité** — Le candidat va-t-il au-delà de la solution minimale ? Explore-t-il des aspects du binaire qui n'étaient pas strictement nécessaires pour le flag ?

La **progression** — Les write-ups récents sont-ils plus approfondis que les premiers ? Le candidat s'attaque-t-il à des challenges de difficulté croissante ?

Aucun recruteur n'attend un portfolio parfait. Ce qu'il veut voir, c'est un analyste qui pense clairement, qui documente son travail, et qui continue d'apprendre.

---

> 🎓 **Vous avez terminé le Chapitre 36 et la Partie IX de cette formation.** Si vous avez suivi l'ensemble du parcours depuis le chapitre 1, vous disposez maintenant des fondations techniques, des outils, de la méthodologie, et des ressources pour poursuivre votre progression en reverse engineering de manière autonome. Le chemin ne fait que commencer — et votre portfolio en sera le témoin.

---


⏭️ [Annexes](/annexes/README.md)
