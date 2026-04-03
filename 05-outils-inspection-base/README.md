🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 5 — Outils d'inspection binaire de base

> **Partie II — Analyse Statique**

---

## Objectif du chapitre

Avant d'ouvrir un désassembleur ou un décompilateur, le reverse engineer expérimenté commence toujours par une phase de **triage rapide**. Cette étape, souvent négligée par les débutants, permet en quelques minutes de répondre aux questions fondamentales sur un binaire inconnu : quel type de fichier est-ce ? Pour quelle architecture a-t-il été compilé ? Quelles bibliothèques utilise-t-il ? Quels appels système effectue-t-il ? Quelles protections sont en place ?

Ce chapitre présente les outils en ligne de commande qui constituent la **boîte à outils de premier contact** de tout analyste. Ces outils sont légers, rapides, disponibles sur la quasi-totalité des distributions Linux, et ne nécessitent aucune interface graphique. Ils forment le socle sur lequel reposent toutes les analyses plus poussées des chapitres suivants.

L'objectif n'est pas seulement de connaître ces outils individuellement, mais d'apprendre à les **enchaîner méthodiquement** pour construire une image mentale du binaire avant même de lire une seule ligne d'assembleur.

---

## Ce que vous allez apprendre

- Identifier instantanément le type, l'architecture et le format d'un binaire avec `file`.  
- Extraire les chaînes de caractères lisibles d'un binaire pour obtenir des indices sur son comportement (`strings`).  
- Inspecter le contenu brut d'un fichier octet par octet avec `xxd` et `hexdump`.  
- Disséquer la structure interne d'un ELF — headers, sections, segments — avec `readelf` et `objdump`.  
- Explorer les tables de symboles pour retrouver les noms de fonctions et de variables avec `nm`.  
- Lister les dépendances dynamiques d'un binaire avec `ldd` et comprendre le mécanisme de résolution avec `ldconfig`.  
- Observer le comportement runtime d'un binaire sans le modifier grâce à `strace` (appels système) et `ltrace` (appels de bibliothèques).  
- Dresser l'inventaire des protections de sécurité appliquées à un binaire avec `checksec`.  
- Assembler tous ces outils dans un **workflow de triage rapide** reproductible : la routine des 5 premières minutes face à un binaire inconnu.

---

## Prérequis

Ce chapitre s'appuie sur les notions introduites dans les chapitres précédents :

- **Chapitre 2** — La chaîne de compilation GNU : vous devez comprendre ce qu'est un fichier ELF, connaître les sections principales (`.text`, `.data`, `.rodata`, `.bss`, `.plt`, `.got`) et savoir ce que sont les symboles et le linking dynamique.  
- **Chapitre 3** — Bases de l'assembleur x86-64 : une familiarité minimale avec les registres et les instructions de base vous aidera à interpréter certaines sorties d'`objdump`, même si le désassemblage approfondi est traité aux chapitres 7 et 8.  
- **Chapitre 4** — Environnement de travail : tous les outils de ce chapitre doivent être installés et fonctionnels. Si vous avez exécuté `check_env.sh` avec succès, vous êtes prêt.

---

## Plan du chapitre

- **5.1** — `file`, `strings`, `xxd` / `hexdump` — premier contact avec un binaire inconnu  
- **5.2** — `readelf` et `objdump` — anatomie d'un ELF (headers, sections, segments)  
- **5.3** — `nm` et `objdump -t` — inspection des tables de symboles  
- **5.4** — `ldd` et `ldconfig` — dépendances dynamiques et résolution  
- **5.5** — `strace` / `ltrace` — appels système et appels de bibliothèques (syscall vs libc)  
- **5.6** — `checksec` — inventaire des protections d'un binaire (ASLR, PIE, NX, canary, RELRO)  
- **5.7** — Workflow « triage rapide » : la routine des 5 premières minutes face à un binaire

---

## Binaires utilisés dans ce chapitre

Tous les binaires d'entraînement se trouvent dans le répertoire `binaries/` à la racine du dépôt. Pour ce chapitre, vous travaillerez principalement avec :

| Binaire | Description | Provenance |  
|---|---|---|  
| `ch05-keygenme/keygenme_O0` | Crackme compilé sans optimisation, avec symboles | `make` dans `binaries/ch05-keygenme/` |  
| `ch05-keygenme/keygenme_O2_strip` | Même crackme optimisé et strippé | idem |  
| `mystery_bin` | Binaire inconnu fourni pour le checkpoint | `binaries/ch05-mystery_bin/` |

Si vous ne les avez pas encore compilés, exécutez depuis la racine du dépôt :

```bash
cd binaries && make all
```

---

## Approche pédagogique

Chaque section de ce chapitre suit la même structure :

1. **Présentation de l'outil** — à quoi il sert et dans quel contexte l'utiliser.  
2. **Options essentielles** — les flags les plus utiles pour le RE, sans chercher l'exhaustivité (les pages `man` sont là pour ça).  
3. **Démonstration sur un binaire concret** — chaque commande est exécutée sur un des binaires d'entraînement, avec la sortie commentée.  
4. **Ce qu'il faut en retenir pour la suite** — les informations à noter et leur utilité dans le workflow global d'analyse.

> 💡 **Conseil pratique** : gardez un terminal ouvert en parallèle de votre lecture et reproduisez chaque commande. Le RE est une discipline qui s'apprend par la pratique — lire sans manipuler ne suffit pas.

---

## Conventions

- Les commandes à exécuter dans un terminal sont présentées dans des blocs de code précédés du prompt `$`.  
- Les sorties tronquées sont indiquées par `[...]`.  
- Les éléments importants dans les sorties sont signalés par des commentaires `# ← explication`.

---


⏭️ [`file`, `strings`, `xxd` / `hexdump` — premier contact avec un binaire inconnu](/05-outils-inspection-base/01-file-strings-xxd.md)
