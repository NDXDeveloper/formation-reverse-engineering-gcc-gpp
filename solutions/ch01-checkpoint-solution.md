🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution du Checkpoint — Chapitre 1

> **Exercice** : Classer 5 scénarios en « analyse statique », « analyse dynamique » ou « combinaison des deux ».

---

## Critère de classification

Le critère déterminant est : **le programme est-il exécuté pendant l'opération décrite ?**

- **Non** → Analyse statique  
- **Oui** → Analyse dynamique  
- **Les deux** → Combinaison

---

## Scénario 1 — Analyse statique

> Triage d'un binaire ELF suspect avec `file`, `strings`, `readelf -S`.

**Classification : Analyse statique**

Le binaire n'est jamais exécuté. Toutes les opérations (`file`, `strings`, `readelf`) examinent le fichier tel qu'il est stocké sur le disque. C'est la phase de triage (cf. section 1.5, Phase 1), qui est entièrement statique. L'URL du serveur C2 a été trouvée dans les données embarquées du binaire, pas en observant une connexion réseau réelle.

---

## Scénario 2 — Combinaison des deux

> Identification de la fonction de vérification dans Ghidra, puis confirmation avec GDB en inspectant les registres à un breakpoint sur `strcmp`.

**Classification : Combinaison statique + dynamique**

Ce scénario illustre le cycle statique → dynamique décrit en section 1.4. L'identification de la fonction de vérification et la localisation de l'appel à `strcmp` ont été réalisées par **analyse statique** (lecture du code décompilé dans Ghidra). La confirmation en posant un breakpoint, en exécutant le programme et en inspectant les registres relève de l'**analyse dynamique** (le programme est en cours d'exécution).

C'est l'exemple type de la complémentarité entre les deux approches : le statique identifie *où* regarder, le dynamique révèle les *valeurs concrètes*.

---

## Scénario 3 — Analyse statique

> Navigation dans le CFG de `main` dans Ghidra, renommage de variables, ajout de commentaires, reconstruction d'une structure.

**Classification : Analyse statique**

Le binaire n'est pas exécuté. Tout le travail — navigation dans le CFG, renommage de variables, ajout de commentaires, reconstruction de structures — se fait sur la représentation désassemblée et décompilée du binaire dans Ghidra. C'est de l'analyse statique approfondie (cf. section 1.5, Phase 2) : on lit et on annote le code sans jamais le faire tourner.

---

## Scénario 4 — Analyse dynamique

> Exécution du binaire dans un sandbox avec capture Wireshark et traçage `strace`.

**Classification : Analyse dynamique**

Le programme est exécuté dans un sandbox, et son comportement est observé en temps réel via deux canaux : le trafic réseau capturé par Wireshark et les appels système tracés par `strace`. L'analyste n'examine pas le code désassemblé — il observe ce que le programme **fait** (se connecter à une adresse IP, envoyer des paquets d'une certaine structure). L'information provient entièrement de l'observation de l'exécution.

---

## Scénario 5 — Combinaison des deux

> Vérification du packing UPX avec `readelf -S` (statique), puis lancement dans GDB pour breakpoint sur l'OEP et dump du code décompressé (dynamique).

**Classification : Combinaison statique + dynamique**

Ce scénario enchaîne les deux approches de manière séquentielle :

1. **Statique** : L'examen des noms de sections avec `readelf` — on lit la structure du fichier sur le disque pour identifier le packer (sections `UPX0`, `UPX1`).  
2. **Dynamique** : Le lancement dans GDB, la pose d'un breakpoint sur l'OEP et le dump du code décompressé en mémoire — le programme doit s'exécuter pour que la décompression ait lieu.  
3. **Statique** (suite) : L'analyse dans Ghidra du code dumpé sera à nouveau statique.

C'est un cas typique du RE de binaires packés, où l'analyse dynamique est nécessaire pour obtenir le code réel avant de pouvoir l'analyser statiquement.

---

## Récapitulatif

| Scénario | Classification | Raison clé |  
|----------|---------------|------------|  
| 1 | Statique | `file`, `strings`, `readelf` — aucune exécution |  
| 2 | Combinaison | Ghidra (statique) + GDB breakpoint (dynamique) |  
| 3 | Statique | Ghidra CFG, renommage, annotations — aucune exécution |  
| 4 | Dynamique | Exécution en sandbox + Wireshark + `strace` |  
| 5 | Combinaison | `readelf` (statique) + GDB dump mémoire (dynamique) |

---

⏭️ [Chapitre 2 — La chaîne de compilation GNU](/02-chaine-compilation-gnu/README.md)
