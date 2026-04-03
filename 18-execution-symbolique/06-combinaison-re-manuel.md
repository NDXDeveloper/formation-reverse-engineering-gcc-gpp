🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 18.6 — Combinaison avec le RE manuel : quand utiliser l'exécution symbolique

> **Chapitre 18 — Exécution symbolique et solveurs de contraintes**  
> Partie IV — Techniques Avancées de RE

---

## Deux approches, un seul objectif

Tout au long de ce chapitre, nous avons présenté l'exécution symbolique et le reverse engineering manuel comme deux approches distinctes. En réalité, les analystes expérimentés ne choisissent pas l'une **ou** l'autre — ils les combinent en permanence, en basculant de l'une à l'autre selon ce que le binaire leur oppose.

Le RE manuel (Ghidra, GDB, Frida) excelle là où l'exécution symbolique échoue : comprendre l'architecture globale d'un programme, identifier les structures de données, suivre les interactions réseau, naviguer dans un code volumineux. L'exécution symbolique excelle là où le RE manuel échoue : résoudre des systèmes de contraintes complexes, trouver une entrée qui satisfait une condition précise, explorer méthodiquement des centaines de branchements.

L'enjeu est de savoir **quand** basculer de l'un à l'autre, et **comment** faire circuler l'information entre les deux.

---

## L'arbre de décision du praticien

Face à un nouveau binaire, voici le raisonnement que suit un analyste qui maîtrise les deux approches :

```
  Nouveau binaire à analyser
           │
           ▼
  ┌─────────────────────────┐
  │  Triage rapide (ch. 5)  │    file, strings, checksec, readelf
  │  5 minutes              │
  └──────────┬──────────────┘
             │
             ▼
  ┌─────────────────────────────────────────┐
  │  Le binaire contient-il des chaînes     │
  │  de succès/échec claires ?              │
  │  ("Access Granted", "Valid", "OK"...)   │
  └──────┬─────────────────────┬────────────┘
         │                     │
        Oui                   Non
         │                     │
         ▼                     ▼
  ┌──────────────┐    ┌──────────────────────┐
  │ Tenter angr  │    │ Analyse statique     │
  │ en mode      │    │ classique (Ghidra)   │
  │ stdout       │    │ pour comprendre la   │
  │ (10 min max) │    │ logique              │
  └──────┬───────┘    └──────────┬───────────┘
         │                       │
    ┌────┴────┐                  │
    │         │                  │
  Succès   Échec                 │
    │         │                  │
    ▼         ▼                  ▼
  Terminé   ┌──────────────────────────────┐
            │  Identifier la fonction de   │
            │  vérification et ses entrées │
            └──────────┬───────────────────┘
                       │
              ┌────────┴────────┐
              │                 │
        Logique simple    Logique complexe
        (< 50 lignes      (transformations
         décompilées)      crypto, boucles
              │              imbriquées)
              ▼                   │
        ┌────────────┐            │
        │ Résoudre   │      ┌─────┴──────┐
        │ à la main  │      │            │
        │ ou avec    │   angr sur     Z3 avec
        │ GDB        │   la fonction  contraintes
        └────────────┘   isolée       extraites
                         (blank_state) manuellement
```

Ce n'est pas un algorithme rigide — c'est une heuristique qui s'affine avec l'expérience. Le point essentiel est que **l'exécution symbolique n'est jamais la première étape**. Le triage rapide et un minimum d'analyse statique viennent toujours en premier, ne serait-ce que pour déterminer si l'exécution symbolique a une chance de fonctionner.

---

## Workflow hybride n°1 — Le « tir à l'aveugle contrôlé »

C'est le workflow le plus courant en CTF et sur des binaires de petite taille. Il consiste à tenter angr dès que possible, en investissant un minimum d'effort humain.

### Étapes

1. **Triage** (5 minutes) — `file`, `strings`, `checksec`. Identifier le type de binaire, les chaînes de succès/échec, les protections actives.

2. **Tentative angr brute** (10 minutes) — Écrire un script minimal avec `entry_state`, critères par stdout, contraintes basiques sur les entrées (ASCII imprimable ou hexadécimal selon ce que `strings` suggère). Lancer avec un timeout de 5 minutes.

3. **Évaluer le résultat** :  
   - **Succès** → Vérifier la solution sur le binaire, passer au problème suivant.  
   - **Timeout avec milliers d'états actifs** → Explosion des chemins. Passer au workflow n°2.  
   - **Timeout avec peu d'états** → Le solveur est bloqué sur des expressions complexes. Passer au workflow n°3.  
   - **Erreur** → Lire le message, hooker la fonction problématique, relancer. Si le problème persiste, passer au workflow n°2.

### Investissement humain

Environ 15 minutes. Si ça fonctionne, c'est le meilleur rapport effort/résultat possible.

### Quand ça marche

- Crackmes de CTF (la majorité).  
- Binaires avec une logique de vérification isolée et des messages de succès/échec clairs.  
- Programmes de petite taille (< 100 Ko) sans interactions complexes avec l'environnement.

---

## Workflow hybride n°2 — Le « scalpel guidé par Ghidra »

Quand le tir à l'aveugle échoue, il faut investir de l'effort humain pour guider l'exécution symbolique. Le principe : utiliser l'analyse statique pour **réduire le périmètre** de l'exécution symbolique au strict nécessaire.

### Étapes

1. **Analyse statique** (30–60 minutes) — Ouvrir le binaire dans Ghidra. Localiser la fonction de vérification (par les cross-references vers les chaînes de succès/échec, chapitre 8). Renommer les variables, reconstruire les types, comprendre le flux de contrôle global.

2. **Identifier les frontières** — Déterminer précisément :  
   - L'**adresse d'entrée** de la fonction de vérification.  
   - Les **registres ou adresses mémoire** qui contiennent les entrées au point d'entrée de la fonction (convention System V : `rdi`, `rsi`, `rdx`…).  
   - Les **adresses de sortie** : les différents `return` (succès vs échec).  
   - Les **fonctions appelées** par la routine de vérification et si elles posent problème (crypto, I/O, boucles complexes).

3. **Préparer l'état angr** — Créer un `blank_state` à l'adresse d'entrée de la fonction, injecter les entrées symboliques dans les bons registres, allouer les buffers en mémoire si nécessaire.

4. **Hooker les zones problématiques** — Si l'analyse Ghidra a révélé des fonctions problématiques (hashing, accès fichier, boucles géantes), les hooker avec des SimProcedures adaptées.

5. **Lancer angr sur le périmètre réduit** — Avec `find` et `avoid` pointant vers les adresses de retour identifiées dans Ghidra.

### Exemple concret

Supposons que Ghidra révèle cette structure dans un binaire de validation de licence :

```c
// Pseudo-code Ghidra (nettoyé)
int validate_license(char *key) {
    if (strlen(key) != 24) return -1;           // 0x401200

    char decoded[12];
    base64_decode(key, decoded);                 // 0x401220

    uint32_t checksum = crc32(decoded, 8);       // 0x401250
    if (checksum != 0xCAFEBABE) return -2;       // 0x401270

    uint32_t *vals = (uint32_t *)decoded;
    transform(vals[0], vals[1]);                 // 0x401290
    if (vals[0] != 0x1337 || vals[1] != 0x7331)
        return -3;                               // 0x4012C0

    return 0;                                    // 0x4012D0 (succès)
}
```

L'analyse statique révèle trois obstacles :

- `base64_decode` — Bien supportée par angr (SimProcedure existante ou facile à modéliser).  
- `crc32` — Modélisable en Z3 (section 18.4) mais peut ralentir angr si l'entrée est longue.  
- `transform` — Quelques opérations arithmétiques, candidat idéal pour l'exécution symbolique.

**Stratégie hybride** : on ne lance pas angr sur la totalité de `validate_license`. On découpe le problème :

1. Le CRC-32 porte sur les 8 premiers octets décodés. On modélise `crc32(octets[0:8]) == 0xCAFEBABE` dans Z3, on obtient les octets 0 à 7.  
2. Les octets 8 à 11 (les deux `uint32_t` restants) doivent satisfaire `transform(vals[0], vals[1]) == (0x1337, 0x7331)`. On lance angr en `blank_state` à l'adresse de `transform` avec `vals[0]` et `vals[1]` symboliques.  
3. On combine les 12 octets trouvés, on les encode en base64, et on obtient la clé de 24 caractères.

Chaque sous-problème est résolu en quelques secondes, là où angr sur la totalité de la fonction aurait probablement bloqué sur le CRC-32 symbolique.

---

## Workflow hybride n°3 — Le « tout Z3 guidé par Ghidra »

Quand angr ne peut tout simplement pas gérer le binaire (obfuscation, code auto-modifiant, interactions système complexes), on abandonne l'exécution symbolique automatique au profit d'une modélisation manuelle complète dans Z3.

### Étapes

1. **Analyse statique approfondie** (1–3 heures) — Comprendre intégralement la logique de vérification dans Ghidra. Renommer chaque variable, annoter chaque opération, reconstruire le pseudo-code complet.

2. **Validation dynamique** — Exécuter le binaire dans GDB avec des entrées connues pour confirmer la compréhension du code. Comparer les valeurs observées en mémoire avec celles prédites par votre compréhension.

3. **Traduction en Z3** — Traduire le pseudo-code Ghidra en script Z3, opération par opération (comme en section 18.4). Vérifier chaque étape en comparant Z3 et GDB sur des valeurs concrètes.

4. **Résolution** — Soumettre les contraintes à Z3. Vérifier la solution sur le binaire réel.

### Le ping-pong Ghidra ↔ Z3 ↔ GDB

Ce workflow n'est pas linéaire — c'est un va-et-vient constant entre trois fenêtres :

```
  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
  │   Ghidra    │     │  Python/Z3  │     │     GDB     │
  │             │     │             │     │             │
  │  Lire le    │────→│  Traduire   │────→│  Valider    │
  │  pseudo-code│     │  en Z3      │     │  avec une   │
  │             │←────│             │←────│  entrée     │
  │  Corriger   │     │  Ajuster    │     │  connue     │
  │  si erreur  │     │  si erreur  │     │             │
  └─────────────┘     └─────────────┘     └─────────────┘
```

Le cycle typique :

1. Lire un bloc de 5–10 lignes dans Ghidra.  
2. Le traduire en Z3.  
3. Tester la traduction avec une valeur concrète connue : calculer le résultat dans Z3 (`simplify(expression)`) et le comparer à ce que GDB affiche au même point du programme.  
4. S'ils correspondent, passer au bloc suivant. S'ils divergent, il y a une erreur de traduction — revenir à Ghidra et vérifier.

Ce processus est méthodique et fiable. Chaque bloc est validé individuellement, ce qui évite d'accumuler des erreurs silencieuses. Sur une fonction de 50 lignes décompilées, comptez 30 à 60 minutes pour la traduction et la validation complète.

---

## Quand ne pas utiliser l'exécution symbolique

Le réflexe de vouloir tout résoudre avec angr est naturel quand on vient de découvrir l'outil. Mais certains problèmes sont mieux résolus autrement, même quand l'exécution symbolique est techniquement possible :

### Le problème est trivial

Si la vérification est un simple `strcmp(input, "password123")`, vous n'avez besoin ni d'angr ni de Z3. `strings` l'a trouvé en 2 secondes, ou GDB en posant un breakpoint sur `strcmp`. L'exécution symbolique fonctionnerait, mais elle serait une surenchère inutile.

**Règle pratique** : si vous pouvez résoudre le problème en moins de 5 minutes avec GDB ou `strings`, ne lancez pas angr.

### Le problème est dynamique

Le programme télécharge une clé depuis un serveur, déchiffre un payload en mémoire, ou adapte son comportement en fonction de l'heure. L'exécution symbolique opère dans un monde statique et déterministe. Pour les problèmes intrinsèquement dynamiques, Frida (chapitre 13) est l'outil de choix :

```javascript
// Frida : intercepter le résultat du déchiffrement à la volée
Interceptor.attach(ptr("0x401300"), {
    onLeave: function(retval) {
        console.log("Clé déchiffrée : " + Memory.readByteArray(retval, 32));
    }
});
```

### Le problème est structurel

Vous cherchez à comprendre l'architecture d'un programme, identifier ses modules, reconstruire ses classes C++ (chapitre 17), ou cartographier ses interactions réseau (chapitre 23). L'exécution symbolique ne vous aide pas ici — c'est un travail pour Ghidra, les cross-references, la reconstruction de structures, et l'analyse du graphe d'appels.

### Le problème nécessite un patching

Vous voulez modifier le comportement d'un binaire : inverser un saut conditionnel (chapitre 21), remplacer une fonction (chapitre 22), contourner une vérification via `LD_PRELOAD`. L'exécution symbolique **trouve** la bonne entrée, mais elle ne **modifie** pas le binaire. Si votre objectif est le patching, passez directement à ImHex (chapitre 6) ou à un éditeur de binaires.

---

## La boîte à outils complète de l'analyste

Pour conclure ce chapitre, voici une vue d'ensemble de l'ensemble des techniques et outils vus dans cette formation, positionnés selon leur usage par rapport à l'exécution symbolique :

```
  ┌───────────────────────────────────────────────────────────────┐
  │                     COMPRENDRE LE BINAIRE                     │
  │                                                               │
  │   strings, file, checksec    → Triage (5 min)                 │
  │   readelf, objdump           → Structure ELF                  │
  │   Ghidra, IDA, Binary Ninja → Décompilation                   │
  │   ImHex                      → Analyse hexadécimale           │
  └───────────────────────────────────────┬───────────────────────┘
                                          │
                                          ▼
  ┌───────────────────────────────────────────────────────────────┐
  │                    OBSERVER L'EXÉCUTION                       │
  │                                                               │
  │   GDB / GEF / pwndbg         → Débogage pas à pas             │
  │   strace / ltrace            → Appels système / bibliothèque  │
  │   Frida                      → Hooking dynamique              │
  │   Valgrind                   → Analyse mémoire                │
  └───────────────────────────────────────┬───────────────────────┘
                                          │
                                          ▼
  ┌───────────────────────────────────────────────────────────────┐
  │                    RÉSOUDRE AUTOMATIQUEMENT                   │
  │                                                               │
  │   angr                       → Exécution symbolique complète  │
  │   Z3                         → Résolution de contraintes      │
  │   AFL++ / libFuzzer          → Fuzzing (exploration aléatoire)│
  └───────────────────────────────────────┬───────────────────────┘
                                          │
                                          ▼
  ┌───────────────────────────────────────────────────────────────┐
  │                    MODIFIER LE BINAIRE                        │
  │                                                               │
  │   ImHex                      → Patching hexadécimal           │
  │   LD_PRELOAD                 → Remplacement de fonctions      │
  │   pwntools                   → Scripting et interactions      │
  │   LIEF / pyelftools          → Modification programmatique    │
  └───────────────────────────────────────────────────────────────┘
```

L'exécution symbolique (angr + Z3) se situe dans la catégorie « résoudre automatiquement ». Elle est la plus puissante des trois catégories quand elle fonctionne, mais elle dépend des deux premières (comprendre et observer) pour être correctement configurée, et elle peut nécessiter la quatrième (modifier) pour exploiter ses résultats.

---

## Cas de synthèse : comment les approches se complètent

Pour ancrer ces idées, voici le déroulement typique d'une analyse complète sur un binaire non trivial — le genre de cible que vous rencontrerez dans les chapitres 21 à 25 :

**Minute 0–5 : Triage** — `file` confirme un ELF x86-64 dynamiquement lié. `strings` révèle `"License valid"` et `"Invalid license"`. `checksec` montre PIE activé, pas de canary. Hypothèse : crackme ou vérification de licence.

**Minute 5–15 : Tentative angr** — Script standard avec `entry_state`, critère stdout. Timeout après 5 minutes — 4000 états actifs, aucun trouvé. L'explosion des chemins est claire. On note que `strace` montre un `open("config.ini", ...)` — le programme lit un fichier de configuration.

**Minute 15–60 : Analyse Ghidra** — On ouvre le binaire dans Ghidra. Le décompileur révèle que `main` lit une clé depuis `argv[1]`, charge des paramètres depuis `config.ini`, puis appelle `validate(key, params)`. La fonction `validate` effectue 3 opérations : un décodage base64, un hash HMAC-SHA256 du résultat avec les paramètres du fichier config, et une comparaison finale. Le hash est le bloqueur — c'est lui qui fait exploser angr.

**Minute 60–75 : Stratégie hybride** — On identifie que le hash porte sur le résultat décodé, pas sur l'entrée brute. On lance GDB, on pose un breakpoint après le base64_decode, on note la valeur attendue du hash (extraite de la comparaison dans Ghidra). On réalise qu'on ne peut pas inverser SHA-256, mais on peut intercepter la valeur *avant* le hash.

**Minute 75–90 : Frida** — On écrit un script Frida qui hooke la fonction de hash et logue son argument d'entrée avec une licence valide (obtenue en version d'essai, par exemple). On obtient les octets qui doivent être produits par le base64_decode.

**Minute 90–100 : Z3** — On modélise la transformation inverse du base64 en Z3 (ou simplement en Python pur, base64 étant bijectif) pour remonter des octets attendus à la clé de licence.

**Minute 100–105 : Vérification** — On teste la clé. `"License valid"`. Terminé.

Dans cette analyse, **aucun outil seul** n'aurait suffi. angr a échoué. Ghidra seul aurait identifié le problème mais pas trouvé la clé. GDB seul n'aurait pas montré la structure globale. Frida seul n'aurait pas su quoi intercepter sans Ghidra. C'est la **combinaison** qui produit le résultat.

---

## Les réflexes à développer

Au fil des analyses, certains réflexes deviennent automatiques :

**Toujours commencer par le triage.** Cinq minutes de `strings`/`file`/`checksec` peuvent vous épargner une heure de travail inutile. Si `strings` révèle directement le mot de passe en clair, aucun outil avancé n'est nécessaire.

**Tenter angr tôt, mais avec un timeout.** Le coût d'une tentative angr ratée est de 5–10 minutes. Le gain d'une tentative réussie est potentiellement des heures de RE manuel économisées. Le ratio est favorable.

**Quand angr échoue, comprendre pourquoi avant de réessayer.** Relancer le même script en espérant un résultat différent ne fonctionne pas. Diagnostiquez (section 18.5), puis ajustez la stratégie.

**Isoler la fonction cible.** La majorité des problèmes de l'exécution symbolique viennent du code **autour** de la fonction de vérification, pas de la fonction elle-même. `blank_state` est votre meilleur ami.

**Valider chaque étape.** Que vous utilisiez angr, Z3 ou une analyse manuelle, testez les résultats intermédiaires avec GDB. Une erreur de traduction détectée tôt coûte 2 minutes à corriger. Détectée à la fin, elle peut coûter une heure de debugging.

**Documenter votre analyse.** Notez les adresses clés, les hypothèses, les résultats intermédiaires. Quand vous basculez entre Ghidra, angr, Z3 et GDB, il est facile de perdre le fil. Un simple fichier texte avec vos observations chronologiques fait toute la différence.

---

## Points clés à retenir

- L'exécution symbolique n'est **jamais la première étape**. Le triage rapide et un minimum d'analyse statique précèdent toujours.

- **Trois workflows hybrides** couvrent la majorité des situations : le tir à l'aveugle (angr brut, 15 min), le scalpel guidé par Ghidra (angr ciblé, 1–2h), et le tout-Z3 manuel (3h+).

- La clé est de savoir **découper le problème** : isoler les sous-fonctions, résoudre chaque morceau avec l'outil le mieux adapté, puis recombiner les résultats.

- L'exécution symbolique est inadaptée aux problèmes **triviaux** (utilisez `strings`/GDB), **dynamiques** (utilisez Frida), **structurels** (utilisez Ghidra) ou de **patching** (utilisez ImHex/LD_PRELOAD).

- La **combinaison** des outils — pas la maîtrise d'un seul — est ce qui fait la différence entre un débutant et un analyste efficace.

- **Documenter au fil de l'eau** et **valider chaque étape** sont les habitudes qui transforment un tâtonnement en une méthodologie fiable.

---

> Vous disposez maintenant de toutes les connaissances nécessaires pour aborder le **checkpoint** de ce chapitre : résoudre `keygenme_O2_strip` avec angr en moins de 30 lignes de Python. Tout ce dont vous avez besoin se trouve dans les sections 18.2 et 18.3 — ce checkpoint est l'occasion de vérifier que vous pouvez le faire de manière autonome, sans recopier les scripts du cours.

⏭️ [🎯 Checkpoint : résoudre `keygenme_O2_strip` avec angr en moins de 30 lignes Python](/18-execution-symbolique/checkpoint.md)
