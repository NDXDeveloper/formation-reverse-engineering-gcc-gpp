🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 29

## Unpacker `ch29-packed`, reconstruire l'ELF et retrouver la logique originale

---

## Contexte

Le binaire cible de ce checkpoint est `packed_sample_upx_tampered`, la variante dont les signatures UPX ont été volontairement altérées (section 29.1). La commande `upx -d` échoue sur ce binaire avec l'erreur `NotPackedException`. L'approche purement statique ne suffit pas : il faudra combiner les techniques statiques et dynamiques vues dans ce chapitre pour mener l'unpacking à terme.

Le binaire de référence `packed_sample_O0` (compilé avec symboles DWARF) est disponible dans le même répertoire. Il ne doit pas être consulté avant d'avoir terminé le checkpoint — il servira uniquement de corrigé pour comparer vos résultats à la fin.

> ⚠️ Toute la procédure doit être réalisée dans la **VM sandboxée** configurée au chapitre 26, même si le binaire est un sample pédagogique inoffensif.

---

## Objectifs

Ce checkpoint valide la maîtrise de l'ensemble du pipeline d'unpacking tel qu'il a été présenté dans les sections 29.1 à 29.4. Il couvre quatre compétences distinctes, correspondant chacune à une section du chapitre :

**Compétence 1 — Détection (section 29.1)**
Confirmer que le binaire est packé, identifier le packer malgré l'altération des signatures, et documenter les indices convergents qui soutiennent le diagnostic.

**Compétence 2 — Extraction (section 29.2)**
Décompresser le code original, soit en restaurant les signatures pour permettre un unpacking statique, soit en réalisant un unpacking dynamique par dump mémoire avec GDB.

**Compétence 3 — Reconstruction (section 29.3)**
Produire un fichier ELF valide à partir des données extraites, avec un ELF header correct, des segments `LOAD` cohérents, et idéalement une section header table exploitable par Ghidra.

**Compétence 4 — Analyse (section 29.4)**
Retrouver intégralement la logique du programme à partir du binaire reconstruit : identifier les fonctions, comprendre l'algorithme de vérification, et produire la clé valide.

---

## Livrables attendus

Le checkpoint est considéré comme réussi lorsque les quatre livrables suivants sont produits :

### Livrable 1 — Rapport de détection

Un court document (texte brut, Markdown ou commentaires dans un script) contenant :

- La sortie de `file` sur le binaire cible, avec une interprétation des éléments significatifs.  
- Le nombre de chaînes retournées par `strings`, comparé à celui du binaire non packé si disponible.  
- La sortie de `checksec`, avec une explication de l'absence des protections.  
- La sortie de `readelf -l` (program headers), avec identification des segments à flags `RWE` et du ratio `MemSiz`/`FileSiz`.  
- Une mesure d'entropie (via `binwalk -E`, ImHex ou script Python), avec la valeur numérique observée et le seuil de référence.  
- La conclusion : packer identifié (UPX altéré), indices convergents listés.

### Livrable 2 — Binaire décompressé

Le fichier ELF reconstruit (`packed_sample_reconstructed` ou nom équivalent), qui doit satisfaire les critères suivants :

- `file` le reconnaît comme un ELF 64-bit x86-64 valide.  
- `readelf -h` affiche un point d'entrée cohérent (adresse de l'OEP).  
- `readelf -l` affiche au moins deux segments `LOAD` avec des permissions distinctes (`r-x` pour le code, `rw-` pour les données).  
- `readelf -S` affiche au minimum les sections `.text` et `.rodata` (la présence de `.data`, `.bss` et `.shstrtab` est un bonus).  
- `strings` sur le fichier reconstruit retourne les chaînes caractéristiques du programme (banner, flag, messages utilisateur).

### Livrable 3 — Analyse de la logique

Un document décrivant la logique complète du programme, contenant :

- La liste des fonctions identifiées dans Ghidra (noms attribués par l'étudiant si le binaire est strippé), avec pour chacune une description en une phrase de son rôle.  
- Le pseudo-code de la fonction de vérification de licence, soit recopié depuis le décompilé Ghidra (renommé et annoté), soit réécrit en C par l'étudiant.  
- L'algorithme de checksum, décrit en termes clairs : opération effectuée, poids, modulo appliqué.  
- Le calcul pas à pas de la clé valide, montrant la valeur ASCII de chaque caractère du préfixe, la multiplication par le poids, la somme, le modulo et la conversion hexadécimale.  
- La description de la routine XOR : clé utilisée, message chiffré, résultat du déchiffrement.

### Livrable 4 — Preuve de résolution

Une capture ou un log démontrant que la clé trouvée fonctionne. Cela peut prendre l'une des formes suivantes :

- La sortie du programme exécuté dans la VM avec la clé correcte saisie en entrée, affichant le message de succès et le flag.  
- Un log GDB montrant un breakpoint sur la fonction de vérification, l'argument `rdi` contenant la clé, et la valeur de retour confirmant la validation.  
- Un script Frida ou pwntools qui envoie automatiquement la clé et capture la sortie.

---

## Critères de validation

| Critère | Seuil de réussite |  
|---------|-------------------|  
| Le rapport de détection cite au moins 4 indices convergents sur les 8 de la grille (section 29.1) | Obligatoire |  
| Le packer est correctement identifié comme UPX avec signatures altérées | Obligatoire |  
| Le fichier ELF reconstruit passe `readelf -h` et `readelf -l` sans erreur | Obligatoire |  
| Ghidra importe le fichier reconstruit et produit un désassemblage cohérent (pas de bruit sur `.text`) | Obligatoire |  
| La fonction de vérification est identifiée et son algorithme est correctement décrit | Obligatoire |  
| La clé valide `RE29-0337` est trouvée et validée (preuve fournie) | Obligatoire |  
| Le fichier reconstruit possède une SHT avec au moins `.text` et `.rodata` | Recommandé |  
| La routine XOR est identifiée et le message `SUCCESS!` est retrouvé | Recommandé |  
| Les constantes de la S-box AES sont repérées et mentionnées dans le rapport | Bonus |  
| Le script de reconstruction est automatisé (Python + LIEF ou équivalent) | Bonus |

---

## Indications méthodologiques

Sans donner la solution pas à pas (disponible dans `solutions/ch29-checkpoint-solution.md`), voici quelques repères pour guider la démarche :

- Commencer **systématiquement** par le triage complet (livrable 1) avant de tenter quoi que ce soit d'autre. La tentation de foncer vers GDB est forte, mais le rapport de détection est un livrable à part entière et les informations collectées (adresses des segments, entropie, permissions) seront directement utiles pour les étapes suivantes.

- Pour l'extraction, deux chemins sont valides : la restauration des magic bytes suivie de `upx -d` (approche statique) ou le dump mémoire via GDB (approche dynamique). Les deux approches sont acceptées. L'approche dynamique est plus formatrice car elle fonctionne sur n'importe quel packer, mais l'approche statique est parfaitement légitime ici puisque le packer est identifié.

- Pour la reconstruction, le script Python avec LIEF présenté en section 29.3 est un point de départ solide. L'adapter aux adresses spécifiques observées dans GDB suffit. Une reconstruction manuelle dans ImHex est également acceptée mais plus sujette aux erreurs de calcul d'offsets.

- Pour l'analyse dans Ghidra, la stratégie la plus efficace est de partir des chaînes de caractères : ouvrir la vue **Defined Strings**, repérer les messages utilisateur, puis remonter aux fonctions qui les référencent via les cross-references (XREF). Cela mène directement à `main`, d'où l'on descend vers les fonctions appelées.

- La clé de licence peut être trouvée par deux chemins indépendants : par **analyse statique** (lire le décompilé et calculer le checksum à la main ou avec un script) ou par **analyse dynamique** (poser un breakpoint sur la comparaison et observer la valeur attendue dans un registre). Les deux approches sont acceptées ; l'idéal est de faire les deux et de vérifier que les résultats concordent.

---

## Compétences mobilisées

Ce checkpoint fait la synthèse des compétences suivantes, acquises tout au long de la formation :

| Compétence | Chapitres de référence |  
|------------|----------------------|  
| Triage rapide d'un binaire inconnu | 5 (section 5.7) |  
| Lecture des headers et sections ELF | 2 (sections 2.3–2.4), 5 (section 5.2) |  
| Analyse d'entropie et recherche de signatures | 6 (sections 6.8–6.10), 29 (section 29.1) |  
| Débogage avec GDB (breakpoints matériels, dump mémoire) | 11 (sections 11.2–11.5), 12, 29 (section 29.2) |  
| Manipulation de structures ELF avec LIEF / ImHex | 6 (section 6.4), 35 (section 35.1), 29 (section 29.3) |  
| Désassemblage et décompilation avec Ghidra | 8 (sections 8.2–8.7) |  
| Identification de patterns crypto | 24 (section 24.1), annexe J |  
| Instrumentation dynamique avec Frida | 13 (sections 13.3–13.5) |

---

> 📌 **Rappel** — Le corrigé complet se trouve dans `solutions/ch29-checkpoint-solution.md`. Consultez-le uniquement après avoir produit vos propres livrables. Comparer votre démarche au corrigé est souvent plus instructif que le résultat final lui-même : il y a rarement un seul chemin correct en reverse engineering.

⏭️ [Partie VII — Bonus : RE sur Binaires .NET / C#](/partie-7-dotnet.md)
