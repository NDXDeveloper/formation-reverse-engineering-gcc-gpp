🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 14

## Objectif

Lancer Valgrind sur le binaire `ch14-crypto`, identifier les buffers de clés en mémoire, et produire un document de synthèse structuré selon la méthode ACRF vue en section 14.4.

Ce checkpoint valide votre capacité à **exploiter les outils d'analyse mémoire non pas comme des outils de débogage, mais comme des instruments de reverse engineering** — en extrayant des rapports bruts les informations structurelles nécessaires à la compréhension d'un binaire inconnu.

---

## Binaire cible

```
binaries/ch14-crypto/
```

Utilisez la version `-O0` avec symboles pour le premier passage (rapports les plus lisibles), puis la version `-O2` strippée pour vérifier que vos conclusions tiennent sur un binaire réaliste.

Préparez un fichier de test de taille raisonnable (quelques centaines d'octets) comme input de chiffrement. Un mot de passe quelconque servira de second argument.

---

## Ce que vous devez produire

### 1. Carte des allocations (méthode ACRF — étape A)

Un tableau répertoriant **tous les blocs dynamiques** alloués par le binaire pendant l'opération de chiffrement, obtenu via Memcheck (`--leak-check=full --show-leak-kinds=all`) et/ou ASan. Pour chaque bloc, documentez :

- Sa taille exacte.  
- L'adresse de la fonction qui l'alloue.  
- L'adresse de la fonction qui le libère (le cas échéant).  
- Sa catégorie Memcheck (definitely lost, still reachable, libéré normalement).  
- Votre hypothèse sur sa nature (clé, IV, buffer d'E/S, contexte crypto, etc.).

**Critère de validation** : vous devez identifier au moins **deux blocs dont la taille correspond à des primitives cryptographiques standard** (clé symétrique, IV, bloc de hash, expanded key…). Justifiez le lien entre la taille observée et la primitive supposée.

### 2. Identification des buffers de clés

C'est le cœur du checkpoint. À partir des rapports Memcheck et/ou MSan, vous devez identifier précisément :

- **Le buffer contenant la clé** — sa taille, son adresse d'allocation, la fonction qui y écrit la clé dérivée, et la fonction qui la lit pour le chiffrement.  
- **Le buffer contenant l'IV** (s'il existe) — même informations. Notez si l'IV est correctement initialisé ou si Memcheck/MSan signale des octets non initialisés.  
- **Le contexte crypto** (s'il existe) — un bloc persistant contenant la clé expansée ou d'autres données d'état. Proposez un layout partiel de cette structure basé sur les offsets d'accès observés dans les rapports.

**Critère de validation** : pour chaque buffer de clé identifié, vous devez fournir au moins **deux sources indépendantes** qui confirment votre hypothèse (par exemple : taille Memcheck + offset ASan, ou taille Memcheck + nombre d'accès Callgrind).

### 3. Graphe fonctionnel de la chaîne crypto (méthode ACRF — étape C)

Un graphe d'appels annoté — textuel ou visuel — montrant les fonctions impliquées dans le flux de chiffrement, obtenu via Callgrind. Pour chaque fonction, indiquez :

- Son adresse.  
- Son coût Callgrind (pourcentage du total).  
- Son rôle hypothétique.  
- Les blocs mémoire qu'elle manipule (référence à votre carte des allocations).

**Critère de validation** : votre graphe doit distinguer clairement les phases d'**initialisation** (allocation des buffers, dérivation de clé), de **traitement** (chiffrement bloc par bloc) et de **finalisation** (écriture, nettoyage). Le hotspot computationnel (routine de chiffrement) doit être identifié avec son coût relatif.

### 4. Ébauche d'au moins une structure C reconstruite (méthode ACRF — étape R)

Proposez au minimum une structure `struct` en C correspondant à l'un des blocs identifiés (contexte crypto de préférence). Chaque champ doit être justifié par un rapport d'outil :

```c
struct cipher_ctx {
    // offset 0, taille X — source : [outil, type d'erreur]
    // offset X, taille Y — source : [outil, type d'erreur]
    // ...
};
```

**Critère de validation** : la structure doit avoir au minimum deux champs documentés avec leurs sources. La somme des tailles des champs (plus le padding éventuel) doit correspondre à la taille d'allocation observée.

---

## Indices et points d'attention

- Pensez à désactiver l'ASLR si le binaire est compilé en PIE, pour obtenir des adresses stables entre les runs :
  ```bash
  setarch x86_64 -R valgrind [options] ./ch14-crypto [args]
  ```

- Les tailles cryptographiques courantes à garder en tête : 16 octets (128 bits — bloc AES, IV), 32 octets (256 bits — clé AES-256, hash SHA-256), 20 octets (160 bits — SHA-1), 48 octets (384 bits — SHA-384), 64 octets (512 bits — SHA-512, bloc SHA-256).

- Si vous utilisez les sanitizers (ASan, UBSan, MSan), pensez à recompiler depuis les sources fournies dans `binaries/ch14-crypto/`. La version ASan + UBSan combinée est la plus productive en un seul run.

- Un fichier de suppression Memcheck filtrant les erreurs de la libc et des bibliothèques crypto système vous fera gagner du temps pour isoler les erreurs du binaire cible.

- Lancez Callgrind **avec un input de petite taille** (quelques dizaines d'octets) pour obtenir un profil lisible. Un input trop gros noie les fonctions d'initialisation sous le volume des itérations de chiffrement.

- Le nombre d'appels sur les arcs du graphe Callgrind est votre meilleur allié pour identifier l'algorithme de chiffrement. Croisez-le avec l'Annexe J (constantes magiques crypto).

---

## Auto-évaluation

Avant de consulter le corrigé, vérifiez que votre document répond à ces questions :

- [ ] Avez-vous identifié au moins deux blocs dont la taille correspond à une primitive crypto ?  
- [ ] Pouvez-vous tracer le chemin de la clé depuis l'input utilisateur jusqu'à son utilisation dans le chiffrement (flux F) ?  
- [ ] Votre graphe fonctionnel distingue-t-il clairement init / traitement / finalisation ?  
- [ ] Chaque champ de vos structures reconstruites est-il justifié par au moins une source ?  
- [ ] Avez-vous testé avec au moins deux inputs différents pour confirmer quels blocs sont à taille fixe et lesquels varient ?  
- [ ] Seriez-vous capable d'ouvrir Ghidra et de renommer immédiatement les fonctions clés grâce à votre analyse ?

Si vous cochez toutes les cases, vous maîtrisez l'exploitation des outils Valgrind et sanitizers en contexte de reverse engineering. Vous êtes prêt pour le chapitre 15 (Fuzzing), où ces mêmes outils seront couplés à la génération automatique d'inputs pour explorer systématiquement les chemins de code d'un binaire.

---

📂 **Corrigé** : [`/solutions/ch14-checkpoint-solution.md`](/solutions/ch14-checkpoint-solution.md)


⏭️ [Chapitre 15 — Fuzzing pour le Reverse Engineering](/15-fuzzing/README.md)
