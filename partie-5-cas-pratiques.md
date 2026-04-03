🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Partie V — Cas Pratiques sur Nos Applications

Vous avez les outils, vous avez les techniques — il est temps de les utiliser pour de vrai. Cette partie vous plonge dans cinq exercices complets de bout en bout, chacun ciblant un type d'application différent : un crackme C classique, une application C++ orientée objet avec plugins, un binaire réseau avec protocole custom, un programme utilisant du chiffrement, et un parseur de format de fichier propriétaire. Les binaires sont les vôtres — vous les compilez depuis les sources fournies, puis vous les analysez comme si vous ne les aviez jamais vus.

---

## 🎯 Objectifs de cette partie

À l'issue de ces cinq chapitres, vous serez capable de :

1. **Mener une analyse complète d'un binaire C** de bout en bout : du triage initial jusqu'à l'écriture d'un keygen fonctionnel, en passant par le patching binaire et la résolution automatique avec angr.  
2. **Reconstruire la hiérarchie de classes d'une application C++** à partir du désassemblage seul, comprendre son système de plugins dynamiques, et écrire un plugin `.so` compatible sans disposer des sources.  
3. **Reverser un protocole réseau custom** : identifier les champs et la state machine du parseur, visualiser les trames avec ImHex, et produire un client de remplacement capable de s'authentifier auprès du serveur.  
4. **Extraire des clés cryptographiques** depuis un binaire en cours d'exécution (GDB, Frida), identifier les algorithmes utilisés par leurs constantes magiques, et reproduire le schéma de chiffrement en Python.  
5. **Documenter intégralement un format de fichier propriétaire** : cartographier sa structure avec ImHex, valider l'interprétation par fuzzing, et produire un parser Python autonome accompagné d'une spécification.  
6. **Adapter votre approche au niveau de difficulté** : résoudre le même défi sur un binaire `-O0` avec symboles, puis sur sa variante `-O2` strippée, en ajustant vos techniques.

---

## 📋 Chapitres

| N° | Titre | Cible | Techniques mobilisées | Lien |  
|----|-------|-------|----------------------|------|  
| 21 | Reverse d'un programme C simple (keygenme) | Crackme avec vérification de clé | `strings`, `checksec`, Ghidra, GDB, patching ImHex, angr, keygen `pwntools` | [Chapitre 21](/21-keygenme/README.md) |  
| 22 | Reverse d'une application C++ orientée objet | Application avec hiérarchie de classes et plugins `.so` | Ghidra (vtables, RTTI), `c++filt`, `dlopen`/`dlsym`, `LD_PRELOAD`, écriture de plugin | [Chapitre 22](/22-oop/README.md) |  
| 23 | Reverse d'un binaire réseau (client/serveur) | Protocole custom sur TCP | `strace`, Wireshark, ImHex (`.hexpat` protocole), replay attack, client `pwntools` | [Chapitre 23](/23-network/README.md) |  
| 24 | Reverse d'un binaire avec chiffrement | Programme chiffrant des fichiers | Constantes magiques (AES S-box, SHA IV), GDB, Frida, ImHex, reproduction crypto Python | [Chapitre 24](/24-crypto/README.md) |  
| 25 | Reverse d'un format de fichier custom | Parseur de format propriétaire | `file`, `binwalk`, ImHex (`.hexpat` itératif), AFL++ (fuzzing du parseur), parser Python, rédaction de spec | [Chapitre 25](/25-fileformat/README.md) |

---

## 📦 Binaires fournis

Chaque chapitre dispose de son répertoire dans `binaries/` avec les sources et un `Makefile` dédié :

```
binaries/
├── ch21-keygenme/    ← keygenme.c + Makefile
├── ch22-oop/         ← oop.cpp, processor.h, plugin_alpha.cpp, plugin_beta.cpp + Makefile
├── ch23-network/     ← client.c, server.c + Makefile
├── ch24-crypto/      ← crypto.c + Makefile
└── ch25-fileformat/  ← fileformat.c + Makefile
```

Chaque `Makefile` produit **plusieurs variantes** du même binaire :

- `*_O0` — sans optimisation, avec symboles (`-O0 -g`) : la version la plus lisible, idéale pour un premier passage.  
- `*_O2` — optimisé (`-O2 -g`) : le compilateur réorganise le code, certaines fonctions sont inlinées.  
- `*_O3` — optimisation agressive (`-O3 -g`) : vectorisation, déroulage de boucles.  
- `*_strip` — strippé (`-O0 -s`) : symboles supprimés, noms de fonctions absents.  
- `*_O2_strip` — optimisé et strippé (`-O2 -s`) : la configuration la plus proche d'un binaire de production.

Compilez tout en une commande depuis la racine du dépôt :

```bash
cd binaries && make all
```

---

## 🧭 Approche recommandée

Pour chaque chapitre, procédez par difficulté croissante :

1. **Commencez par la variante `-O0` avec symboles.** Les noms de fonctions sont visibles, le code désassemblé suit la structure du source, le decompiler Ghidra produit un pseudo-code très lisible. C'est votre terrain d'entraînement pour comprendre la logique du programme.

2. **Passez à la variante `-O2` avec symboles.** Les noms sont encore là, mais le code est réorganisé par le compilateur. Vous appliquez ici les techniques du chapitre 16 (reconnaissance des optimisations).

3. **Attaquez la variante strippée (`-O0 -s` puis `-O2 -s`).** Plus de noms de fonctions, plus de types — vous devez tout reconstruire. C'est l'exercice le plus formateur et le plus proche des conditions réelles.

Ne passez à la variante suivante que lorsque vous avez complété le checkpoint de la variante courante. La progression est conçue pour consolider vos réflexes à chaque palier.

---

## ⏱️ Durée estimée

**~25-35 heures** pour un praticien ayant complété les Parties I à IV.

Chaque chapitre représente un mini-projet complet. Comptez ~5-7h par cas pratique si vous travaillez sur au moins deux variantes du binaire (typiquement `-O0` puis `-O2 -s`). Le chapitre 21 (keygenme) est le plus guidé et sert de rampe d'accès — les chapitres suivants laissent progressivement plus d'autonomie. Le chapitre 25 (format de fichier) est le plus ouvert : la rédaction de la spécification demande un effort de synthèse supplémentaire.

---

## 📌 Prérequis

Avoir complété les **[Partie I](/partie-1-fondamentaux.md)**, **[Partie II](/partie-2-analyse-statique.md)**, **[Partie III](/partie-3-analyse-dynamique.md)** et **[Partie IV](/partie-4-techniques-avancees.md)**.

Concrètement, chaque cas pratique mobilise un sous-ensemble spécifique de compétences :

- **Chapitre 21** — Ghidra, GDB, ImHex (patching), angr, `pwntools`.  
- **Chapitre 22** — Ghidra (vtables, RTTI), `c++filt`, compréhension du dispatch virtuel et de `dlopen`.  
- **Chapitre 23** — `strace`, Wireshark, ImHex (`.hexpat`), `pwntools` (sockets).  
- **Chapitre 24** — Constantes crypto (annexe J), GDB, Frida, Python (`pycryptodome` ou équivalent).  
- **Chapitre 25** — `binwalk`, ImHex, AFL++, Python (parsing binaire avec `struct`).

Si vous maîtrisez déjà certains de ces outils, vous pouvez aborder les chapitres dans le désordre — ils sont indépendants les uns des autres.

---

## ⬅️ Partie précédente

← [**Partie IV — Techniques Avancées de RE**](/partie-4-techniques-avancees.md)

## ➡️ Partie suivante

Les cas pratiques terminés, vous passerez à l'analyse de code malveillant dans un environnement contrôlé : ransomware Linux, dropper avec communication C2, et techniques d'unpacking.

→ [**Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**](/partie-6-malware.md)

⏭️ [Chapitre 21 — Reverse d'un programme C simple (keygenme)](/21-keygenme/README.md)
