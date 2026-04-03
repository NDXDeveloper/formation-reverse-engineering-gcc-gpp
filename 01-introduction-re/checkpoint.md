🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 1

> **Chapitre 1 — Introduction au Reverse Engineering**  
> 🎯 Ce checkpoint valide votre compréhension des concepts fondamentaux avant de passer au chapitre 2.  
> ⏱️ Temps estimé : 10 minutes.

---

## Objectif

La distinction entre analyse statique et analyse dynamique est l'un des concepts structurants de cette formation (cf. [section 1.4](/01-introduction-re/04-statique-vs-dynamique.md)). Ce checkpoint vous demande de classer cinq scénarios concrets dans l'une ou l'autre catégorie — ou d'identifier les cas qui relèvent des deux.

Au-delà de la simple classification, l'objectif est de vous assurer que vous comprenez **pourquoi** chaque scénario relève d'une approche plutôt que d'une autre. Le critère déterminant, rappelons-le, est simple : le programme est-il exécuté pendant l'analyse, ou non ?

---

## Les 5 scénarios

Lisez chaque scénario et déterminez s'il relève de l'**analyse statique**, de l'**analyse dynamique**, ou d'une **combinaison des deux**. Justifiez votre réponse en une ou deux phrases.

---

### Scénario 1

> Vous recevez un binaire ELF suspect dans le cadre d'une réponse à incident. Avant toute exécution, vous lancez `file` pour identifier le format, `strings` pour extraire les chaînes de caractères lisibles, et `readelf -S` pour lister les sections. Vous repérez une chaîne qui ressemble à une URL de serveur C2.

---

### Scénario 2

> Vous avez identifié la fonction de vérification de mot de passe d'un crackme grâce à Ghidra. Pour confirmer votre compréhension de l'algorithme, vous lancez le programme dans GDB, posez un breakpoint juste avant l'appel à `strcmp`, entrez un mot de passe arbitraire, et inspectez les registres `rdi` et `rsi` pour voir les deux chaînes comparées.

---

### Scénario 3

> Vous ouvrez un binaire dans Ghidra. Vous naviguez dans le graphe de flux de contrôle de la fonction `main`, renommez les variables locales en fonction de leur usage apparent, ajoutez des commentaires sur les blocs conditionnels et reconstruisez une structure `struct packet_header` à partir des offsets d'accès mémoire observés dans le pseudo-code décompilé.

---

### Scénario 4

> Vous analysez un binaire réseau. Vous le lancez dans un environnement sandboxé pendant que Wireshark capture le trafic sur l'interface locale. Parallèlement, vous exécutez `strace` pour observer les appels système `connect`, `send` et `recv`. Vous identifiez l'adresse IP du serveur distant et la structure des premiers paquets échangés.

---

### Scénario 5

> Vous soupçonnez qu'un binaire est packé avec UPX. Pour vérifier, vous examinez les noms de sections avec `readelf -S` (vous trouvez `UPX0` et `UPX1`), puis vous lancez le binaire dans GDB, posez un breakpoint sur l'entry point original (OEP) après décompression, et dumpez le code décompressé en mémoire pour l'analyser dans Ghidra.

---

## Réponses

> ⚠️ **Essayez de répondre avant de lire les solutions ci-dessous.** Le checkpoint n'a de valeur que si vous formulez vos propres réponses d'abord.

<details>
<summary><strong>Cliquez pour révéler les réponses</strong></summary>

---

### Scénario 1 — Analyse statique

Le binaire n'est jamais exécuté. Toutes les opérations (`file`, `strings`, `readelf`) examinent le fichier tel qu'il est stocké sur le disque. C'est la phase de triage, qui est entièrement statique. L'URL du serveur C2 a été trouvée dans les données embarquées du binaire, pas en observant une connexion réseau réelle.

---

### Scénario 2 — Combinaison des deux

Ce scénario illustre le cycle statique → dynamique décrit en section 1.4. L'identification de la fonction de vérification et la localisation de l'appel à `strcmp` ont été réalisées par **analyse statique** (lecture du code décompilé dans Ghidra). La confirmation en posant un breakpoint, en exécutant le programme et en inspectant les registres relève de l'**analyse dynamique** (le programme est en cours d'exécution). C'est l'exemple type de la complémentarité entre les deux approches : le statique identifie *où* regarder, le dynamique révèle les *valeurs concrètes*.

---

### Scénario 3 — Analyse statique

Le binaire n'est pas exécuté. Tout le travail — navigation dans le CFG, renommage de variables, ajout de commentaires, reconstruction de structures — se fait sur la représentation désassemblée et décompilée du binaire dans Ghidra. C'est de l'analyse statique approfondie : on lit et on annote le code sans jamais le faire tourner.

---

### Scénario 4 — Analyse dynamique

Le programme est exécuté dans un sandbox, et son comportement est observé en temps réel via deux canaux : le trafic réseau capturé par Wireshark et les appels système tracés par `strace`. L'analyste n'examine pas le code désassemblé ici — il observe ce que le programme **fait** (se connecter à une adresse IP, envoyer des paquets d'une certaine structure). C'est de l'analyse dynamique pure : l'information provient entièrement de l'observation de l'exécution.

---

### Scénario 5 — Combinaison des deux

Ce scénario enchaîne les deux approches de manière séquentielle. L'examen des noms de sections avec `readelf` est une opération **statique** — on lit la structure du fichier sur le disque pour identifier le packer. Le lancement dans GDB, la pose d'un breakpoint sur l'OEP et le dump du code décompressé en mémoire relèvent de l'**analyse dynamique** — le programme doit s'exécuter pour que la décompression ait lieu. L'analyse ultérieure dans Ghidra du code dumpé sera à nouveau statique. C'est un cas typique du RE de binaires packés, où l'analyse dynamique est nécessaire pour obtenir le code réel avant de pouvoir l'analyser statiquement.

</details>

---

## Auto-évaluation

Si vous avez correctement classé les cinq scénarios et que vos justifications correspondent à la logique exposée ci-dessus, vous avez assimilé les concepts fondamentaux du chapitre 1. Vous pouvez passer au chapitre 2.

Si vous avez hésité sur un ou plusieurs scénarios, relisez la [section 1.4 — Différence entre RE statique et RE dynamique](/01-introduction-re/04-statique-vs-dynamique.md) avant de continuer. Le critère central est toujours le même : **le programme est-il exécuté pendant l'opération décrite ?** Si oui, c'est de l'analyse dynamique. Si non, c'est de l'analyse statique. Si le scénario enchaîne les deux, c'est une combinaison — et c'est le cas le plus fréquent en pratique.

---


⏭️ [Chapitre 2 — La chaîne de compilation GNU](/02-chaine-compilation-gnu/README.md)
