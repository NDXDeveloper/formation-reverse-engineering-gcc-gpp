🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Réaliser un triage complet du binaire `mystery_bin`

> **Chapitre 5 — Outils d'inspection binaire de base**  
> **Partie II — Analyse Statique**

---

## Objectif

Ce checkpoint valide votre maîtrise de l'ensemble des outils présentés au chapitre 5. Vous allez appliquer le workflow de triage rapide (section 5.7) sur un binaire que vous n'avez jamais vu — `mystery_bin` — et produire un rapport d'analyse structuré d'une page.

Le binaire `mystery_bin` a été compilé avec GCC à partir d'un code source C ou C++ que vous ne connaissez pas. Vous ne savez rien d'autre à son sujet. Votre travail est de découvrir, en utilisant exclusivement les outils du chapitre 5, tout ce qu'il est possible d'apprendre sur ce binaire **sans l'ouvrir dans un désassembleur**.

---

## Localisation du binaire

```bash
$ ls binaries/ch05-mystery_bin/mystery_bin
```

Si le binaire n'est pas encore compilé, exécutez depuis la racine du dépôt :

```bash
$ cd binaries && make all
```

---

## Consignes

### Ce que vous devez faire

Appliquez les six étapes du workflow de triage dans l'ordre. Pour chaque étape, exécutez les commandes appropriées, observez les résultats, et notez les informations pertinentes. À l'issue du triage, rédigez un rapport structuré qui synthétise vos observations et formule des hypothèses sur la nature et le comportement du programme.

### Ce que vous devez produire

Un fichier Markdown (ou texte) d'environ une page, structuré selon le modèle de rapport présenté à la section 5.7. Le rapport doit couvrir les six rubriques suivantes :

1. **Identification** — format, architecture, linking, stripping, compilateur.  
2. **Chaînes notables** — messages, chemins, données suspectes, formats.  
3. **Structure ELF** — entry point, sections remarquables, dépendances, anomalies éventuelles.  
4. **Fonctions et imports** — fonctions du programme (si disponibles), fonctions de bibliothèques importées.  
5. **Protections** — état de chaque protection (NX, PIE, canary, RELRO, FORTIFY).  
6. **Comportement dynamique** — observations `strace`/`ltrace` (dans un environnement approprié).

Le rapport doit se conclure par une **section d'hypothèses et de stratégie** : d'après vos observations, que fait ce programme ? Quelles sont les pistes d'investigation les plus prometteuses pour un reverse engineering approfondi ? Quel outil ouvririez-vous en premier pour continuer l'analyse ?

### Ce que vous ne devez pas utiliser

Ce checkpoint se limite aux outils du chapitre 5. N'ouvrez pas le binaire dans Ghidra, IDA, Radare2, ou tout autre désassembleur/décompilateur. N'utilisez pas GDB. L'objectif est précisément de mesurer ce qu'on peut apprendre **sans** ces outils avancés.

---

## Aide méthodologique

### Commandes de référence par étape

Voici un récapitulatif des commandes clés pour chaque étape, tel que présenté tout au long du chapitre :

**Étape 1 — Identification :**

```bash
$ file mystery_bin
```

**Étape 2 — Chaînes :**

```bash
$ strings mystery_bin | head -40
$ strings -t x mystery_bin | grep -iE '(error|fail|password|key|flag|secret|http|socket|crypt)'
$ strings mystery_bin | grep -iE '(GCC|clang|rustc)'
$ strings mystery_bin | grep '%'
```

**Étape 3 — Structure ELF :**

```bash
$ readelf -hW mystery_bin
$ readelf -SW mystery_bin
$ readelf -lW mystery_bin
$ readelf -d mystery_bin | grep NEEDED
```

**Étape 4 — Symboles et imports :**

```bash
# Tenter d'abord la table complète
$ nm -nS mystery_bin 2>/dev/null | grep ' T '

# Si "no symbols", passer aux symboles dynamiques
$ nm -D mystery_bin

# Vue détaillée
$ readelf -s mystery_bin
```

**Étape 5 — Protections :**

```bash
$ checksec --file=mystery_bin

# Ou manuellement si checksec n'est pas disponible :
$ readelf -lW mystery_bin | grep GNU_STACK
$ readelf -h mystery_bin | grep Type
$ readelf -s mystery_bin | grep __stack_chk_fail
$ readelf -lW mystery_bin | grep GNU_RELRO
$ readelf -d mystery_bin | grep BIND_NOW
```

**Étape 6 — Comportement dynamique (sandbox) :**

```bash
$ strace -e trace=file,network,process -s 256 -o strace.log ./mystery_bin
$ ltrace -s 256 -o ltrace.log ./mystery_bin
$ strace -c ./mystery_bin <<< "test"
$ ltrace -c ./mystery_bin <<< "test"
```

### Questions directrices

Pendant le triage, gardez ces questions en tête pour guider vos observations :

- Le binaire est-il strippé ? Si oui, quels noms de fonctions restent accessibles ?  
- Quelles bibliothèques partagées utilise-t-il ? Que révèlent-elles sur ses fonctionnalités ?  
- Les chaînes suggèrent-elles une interaction utilisateur ? Un protocole réseau ? Du chiffrement ? De la manipulation de fichiers ?  
- Y a-t-il des indices d'anti-reversing (sections inhabituelles, entropie suspecte, imports comme `ptrace`) ?  
- Le comportement dynamique (`strace`/`ltrace`) confirme-t-il ou contredit-il les hypothèses issues de l'analyse statique ?  
- Quelle serait votre prochaine action si vous deviez poursuivre l'analyse au-delà du triage ?

---

## Critères de validation

Votre rapport de triage est considéré comme complet si :

- [ ] Les six rubriques sont présentes et renseignées.  
- [ ] Le format, l'architecture et le type de linking sont correctement identifiés.  
- [ ] L'état du stripping est mentionné et ses conséquences sur l'analyse sont comprises.  
- [ ] Au moins 3 chaînes significatives sont relevées et interprétées (pas simplement listées).  
- [ ] Les sections ELF principales sont identifiées et les éventuelles anomalies sont signalées.  
- [ ] Les fonctions du programme et/ou les imports sont listés avec une interprétation fonctionnelle.  
- [ ] Les 5 protections principales (NX, PIE, canary, RELRO, FORTIFY) sont documentées.  
- [ ] Le comportement dynamique est observé et les résultats sont cohérents avec l'analyse statique.  
- [ ] Le rapport se conclut par des hypothèses argumentées et une stratégie pour la suite.  
- [ ] Le rapport tient en environ une page — concis et structuré, pas un copier-coller brut de sorties de commandes.

---

## Erreurs courantes à éviter

**Copier-coller la sortie brute des commandes sans l'interpréter.** Le rapport n'est pas un log de terminal. Chaque observation doit être accompagnée de son interprétation : que signifie ce résultat ? Qu'est-ce que ça nous apprend sur le binaire ?

**Oublier les symboles dynamiques sur un binaire strippé.** Si `nm` sans option retourne « no symbols », beaucoup de débutants concluent qu'il n'y a aucun symbole et passent à la suite. Il faut penser à `nm -D` pour les imports — c'est souvent la source d'information la plus précieuse sur un binaire strippé.

**Ignorer les résultats négatifs.** L'absence d'un élément est une information en soi. Si `strings` ne trouve aucune URL, c'est un indice que le programme ne fait probablement pas de réseau (ou qu'il obfusque ses chaînes). Si `strace` ne montre aucun `openat` au-delà du chargement des bibliothèques, le programme ne lit pas de fichier. Documentez ces absences.

**Lancer `ldd` ou l'étape 6 sur un binaire potentiellement malveillant sans sandbox.** Dans le cadre de ce checkpoint, le binaire est fourni par la formation et n'est pas malveillant. Mais prenez l'habitude de traiter l'étape 6 avec prudence — c'est un réflexe qui vous protégera en situation réelle.

**Confondre observation et hypothèse.** « Le binaire importe `strcmp` » est une observation. « Le programme compare probablement une entrée utilisateur avec une valeur attendue » est une hypothèse déduite de cette observation. Les deux ont leur place dans le rapport, mais elles doivent être clairement distinguées.

---

## Pour aller plus loin

Si vous terminez le triage rapidement et souhaitez approfondir, voici quelques pistes supplémentaires qui restent dans le périmètre des outils du chapitre 5 :

- Comparez la sortie de `readelf -s` et `nm` — les informations sont-elles strictement identiques ? Y a-t-il des symboles que l'un voit et pas l'autre ?  
- Utilisez `readelf -x .rodata` pour examiner la section de données en lecture seule. Les chaînes y sont-elles contiguës ou entrecoupées d'octets nuls et de données numériques ?  
- Si le binaire accède à des fichiers au runtime (visible dans `strace`), utilisez `strings -t x` pour localiser les chemins de fichiers dans le binaire, puis `xxd -s <offset>` pour examiner leur contexte hexadécimal.  
- Exécutez le binaire plusieurs fois avec des inputs différents et comparez les sorties `ltrace`. Le comportement change-t-il selon l'input ? Quelles fonctions sont appelées dans un cas et pas dans l'autre ?

---

## Solution

Le corrigé de ce checkpoint est disponible dans `solutions/ch05-checkpoint-solution.md`. Consultez-le **après** avoir rédigé votre propre rapport — la valeur de l'exercice réside dans la démarche, pas dans le résultat.

---

> ✅ **Checkpoint validé ?** Vous maîtrisez les outils d'inspection de base et le workflow de triage rapide. Vous êtes prêt pour le chapitre 6, où nous plongerons dans l'analyse hexadécimale avancée avec ImHex.  
>  
> 

⏭️ [Chapitre 6 — ImHex : analyse hexadécimale avancée](/06-imhex/README.md)
