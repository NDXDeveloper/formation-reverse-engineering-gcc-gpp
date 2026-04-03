🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 27.7 — Rédiger un rapport d'analyse type (IOC, comportement, recommandations)

> 🎯 **Objectif de cette section** : formaliser l'intégralité de l'analyse menée depuis la section 27.2 dans un **rapport structuré**, tel qu'un analyste en réponse à incident le produirait pour son équipe, sa direction ou un client. Le rapport est le livrable final de toute analyse de malware — c'est lui qui transforme un travail technique individuel en information actionnable par une organisation.  
>  
> Cette section présente d'abord les principes de rédaction d'un rapport d'analyse, puis fournit un **template complet** rempli avec les résultats de notre analyse du sample Ch27.

---

## Pourquoi rédiger un rapport

L'analyse technique la plus brillante n'a aucune valeur si elle reste dans la tête de l'analyste ou dispersée dans des notes informelles. Le rapport remplit trois fonctions distinctes selon son audience :

**Pour l'équipe technique (SOC, CERT, IR)** — Le rapport fournit les IOC nécessaires pour détecter le sample sur d'autres machines, les règles YARA déployables immédiatement, et le déchiffreur permettant de restaurer les fichiers. C'est un outil opérationnel.

**Pour la direction et les décideurs** — Le résumé exécutif traduit l'analyse technique en impact business : combien de fichiers affectés, les données sont-elles récupérables, quelle est la sophistication de l'attaquant, faut-il payer la rançon. C'est un outil de décision.

**Pour la communauté (CERT nationaux, partenaires sectoriels, bases de threat intelligence)** — Les IOC, les règles YARA et la description comportementale alimentent les bases de signatures partagées. C'est un outil de défense collective.

Un bon rapport doit servir ces trois audiences dans un même document, grâce à une structure en couches : résumé exécutif (non-technique) en tête, détails techniques en corps, annexes techniques en queue.

---

## Principes de rédaction

### Clarté et précision

Chaque affirmation du rapport doit être étayée par une preuve identifiable. Évitez les formulations vagues comme « le malware semble utiliser du chiffrement ». Préférez « le binaire appelle `EVP_aes_256_cbc()` (XREF à l'offset `0x001013c5`), confirmé par capture du registre `rcx` lors du breakpoint GDB sur `EVP_EncryptInit_ex` ». La précision permet à un pair de **reproduire** chaque conclusion.

### Séparation faits / interprétations

Distinguez toujours ce que vous avez **observé** (fait) de ce que vous en **déduisez** (interprétation). Par exemple : « Le binaire ne contient aucun appel à `socket`, `connect` ou `send` dans sa table de symboles dynamiques (fait). Cela suggère l'absence de communication réseau (interprétation), confirmée par une trace `strace` muette sur les syscalls réseau (fait complémentaire). » Cette rigueur protège le rapport contre les contestations et guide le lecteur dans son propre raisonnement.

### Niveaux de confiance

Attribuez un niveau de confiance à chaque conclusion. La terminologie standardisée dans le renseignement sur les menaces utilise une échelle comme celle-ci :

| Niveau | Signification | Exemple dans notre analyse |  
|---|---|---|  
| **Confirmé** | Observé directement, reproductible | « La clé AES est `REVERSE_ENGINEERING_IS_FUN_2025!` » |  
| **Très probable** | Multiples indices convergents, pas d'observation directe d'une alternative | « Le sample ne communique pas sur le réseau » |  
| **Probable** | Indices solides mais scénarios alternatifs possibles | « Le sample ne cible que `/tmp/test/` » (pourrait exister un mode alternatif) |  
| **Possible** | Hypothèse plausible, indices limités | « L'auteur est un étudiant en sécurité (contexte pédagogique) » |

### Structure progressive

Le rapport se lit de haut en bas, du plus synthétique au plus détaillé. Un lecteur pressé s'arrête au résumé exécutif. Un analyste technique descend jusqu'aux annexes. Personne ne devrait être obligé de tout lire pour trouver l'information dont il a besoin.

---

## Template de rapport — rempli avec notre analyse

Ce qui suit est un rapport complet, rédigé comme si nous étions un analyste rendant compte d'un incident réel. Les étudiants peuvent l'utiliser comme template pour leurs propres analyses futures en remplaçant le contenu spécifique au Ch27 par leurs propres résultats.

---

> # RAPPORT D'ANALYSE DE MALWARE  
>  
> | Champ | Valeur |  
> |---|---|  
> | **Référence** | MAL-2025-CH27-001 |  
> | **Classification** | TLP:WHITE — Diffusion libre |  
> | **Date d'analyse** | [Date du jour] |  
> | **Analyste** | [Votre nom / pseudo] |  
> | **Outil principal** | Ghidra 11.x, GDB 14.x, Frida 16.x, ImHex 1.3x |  
> | **Environnement** | Ubuntu 24.04 LTS, VM QEMU/KVM isolée, réseau déconnecté |  
> | **Révision** | 1.0 |

---

> ## 1. Résumé exécutif  
>  
> Le sample analysé est un **ransomware ELF Linux** ciblant le répertoire `/tmp/test/`. Il chiffre récursivement les fichiers avec l'algorithme **AES-256-CBC** via la bibliothèque OpenSSL, remplace les originaux par des fichiers portant l'extension `.locked`, puis dépose une note de rançon.  
>  
> **Conclusion principale : les fichiers sont intégralement récupérables.** La clé de chiffrement est codée en dur dans le binaire et a été extraite par analyse statique et dynamique. Un outil de déchiffrement fonctionnel a été produit et validé.  
>  
> Le sample ne présente aucune communication réseau, aucun mécanisme de persistance, et aucune technique d'évasion ou d'anti-analyse. Sa sophistication est **faible**. Il ne constitue pas une menace avancée, mais son comportement de chiffrement destructeur le rend dangereux pour les données non sauvegardées dans le périmètre ciblé.  
>  
> **Recommandation immédiate** : déployer le déchiffreur (annexe C) sur les systèmes affectés et scanner l'infrastructure avec les règles YARA fournies (annexe D) pour identifier d'éventuelles autres machines compromises.

---

> ## 2. Identification du sample  
>  
> | Propriété | Valeur |  
> |---|---|  
> | **Nom de fichier** | `ransomware_O2_strip` |  
> | **Type** | ELF 64-bit LSB PIE executable, x86-64, dynamically linked, stripped |  
> | **SHA-256** | `[insérer le hash — sha256sum ransomware_O2_strip]` |  
> | **SHA-1** | `[insérer]` |  
> | **MD5** | `[insérer]` |  
> | **Taille** | [insérer] octets |  
> | **Compilateur** | GCC (GNU Compiler Collection), toolchain Linux standard |  
> | **Dépendances** | `libssl.so.3`, `libcrypto.so.3`, `libc.so.6` |  
> | **Protections binaires** | PIE activé, NX activé, Stack canary présent, Partial RELRO |  
> | **Première soumission VT** | N/A (sample pédagogique, non soumis) |

---

> ## 3. Indicateurs de compromission (IOC)  
>  
> ### 3.1 IOC basés sur le fichier (file-based)  
>  
> | Type | Valeur | Contexte |  
> |---|---|---|  
> | SHA-256 | `[hash du binaire]` | Hash du sample analysé |  
> | Nom de fichier | `README_LOCKED.txt` | Ransom note déposée par le sample |  
> | Extension | `.locked` | Extension ajoutée aux fichiers chiffrés |  
> | Magic bytes (binaire) | `7F 45 4C 46` (ELF) | Header du sample |  
> | Magic bytes (fichiers produits) | `52 57 41 52 45 32 37 00` (`RWARE27\0`) | Header des fichiers chiffrés |  
> | Chaîne caractéristique | `REVERSE_ENGINEERING_IS_FUN_2025!` | Clé AES en `.rodata` |  
> | Chaîne caractéristique | `VOS FICHIERS ONT ETE CHIFFRES` | Fragment de la ransom note |  
>  
> ### 3.2 IOC basés sur le comportement (behavioral)  
>  
> | Type | Valeur | Contexte |  
> |---|---|---|  
> | Répertoire ciblé | `/tmp/test/` | Unique répertoire parcouru |  
> | Opération fichier | Parcours récursif (`opendir`/`readdir`/`stat`) | Énumération de l'arborescence |  
> | Opération fichier | Lecture intégrale (`fopen`/`fread`) → écriture `.locked` → suppression original (`unlink`) | Cycle chiffrement/remplacement |  
> | API cryptographique | `EVP_EncryptInit_ex`, `EVP_EncryptUpdate`, `EVP_EncryptFinal_ex` | Chiffrement AES-256-CBC via OpenSSL |  
> | Absence réseau | Aucun appel `socket`/`connect`/`send`/`recv` | Pas d'exfiltration, pas de C2 |  
> | Absence persistance | Aucune écriture dans crontab, systemd, `.bashrc`, `/etc/init.d` | Exécution unique |  
>  
> ### 3.3 IOC basés sur le réseau (network-based)  
>  
> Aucun. Le sample n'effectue aucune communication réseau. Cette absence a été confirmée par analyse statique (pas de symboles réseau dans `.dynsym`) et dynamique (`strace -e trace=network` muet).

---

> ## 4. Analyse comportementale détaillée  
>  
> ### 4.1 Flux d'exécution  
>  
> Le programme suit un flux linéaire en cinq phases :  
>  
> **Phase 1 — Initialisation.** Le sample affiche une bannière sur `stdout` puis vérifie l'existence du répertoire `/tmp/test/` via un appel `stat()`. Si le répertoire n'existe pas, le programme s'arrête avec un message d'erreur. Cette vérification constitue un garde-fou involontaire : le sample ne peut pas chiffrer de fichiers en dehors de cette arborescence.  
>  
> **Phase 2 — Énumération.** La fonction de parcours récursif (`traverse_directory`) utilise `opendir`/`readdir` pour lister le contenu de `/tmp/test/` et de ses sous-répertoires. Chaque entrée est filtrée : les fichiers `.locked` (déjà chiffrés) et `README_LOCKED.txt` (ransom note) sont ignorés. Les répertoires déclenchent un appel récursif. Les fichiers réguliers sont transmis à la routine de chiffrement.  
>  
> **Phase 3 — Chiffrement.** Pour chaque fichier ciblé, la fonction `encrypt_file` exécute la séquence suivante :  
>  
> 1. Lecture intégrale du fichier en mémoire (`fopen` / `fseek` / `ftell` / `fread`).  
> 2. Allocation d'un buffer de sortie de taille `file_size + 16` (padding AES).  
> 3. Chiffrement via l'API EVP d'OpenSSL : `EVP_EncryptInit_ex` (AES-256-CBC avec clé et IV statiques) → `EVP_EncryptUpdate` → `EVP_EncryptFinal_ex`.  
> 4. Écriture du fichier `.locked` : header de 16 octets (magic `RWARE27\0` + taille originale en `uint64_t` LE) suivi du ciphertext.  
> 5. Suppression du fichier original via `unlink()`.  
>  
> **Phase 4 — Ransom note.** Après le chiffrement de tous les fichiers, la fonction `drop_ransom_note` écrit le fichier `README_LOCKED.txt` à la racine de `/tmp/test/`. Le texte mentionne l'algorithme utilisé (AES-256-CBC) et fournit un indice sur la localisation de la clé.  
>  
> **Phase 5 — Terminaison.** Le sample affiche le nombre de fichiers chiffrés et se termine normalement (`exit(0)`). Aucun mécanisme de persistance n'est mis en place.  
>  
> ### 4.2 Analyse cryptographique  
>  
> | Paramètre | Valeur | Méthode de confirmation |  
> |---|---|---|  
> | Algorithme | AES-256-CBC | XREF vers `EVP_aes_256_cbc()` (Ghidra) + appel observé (GDB/Frida) |  
> | Clé | `52455645...30323521` (ASCII: `REVERSE_ENGINEERING_IS_FUN_2025!`) | Argument `rcx` de `EVP_EncryptInit_ex` (GDB/Frida) |  
> | IV | `DEADBEEFCAFEBABE13374242FEEDFACE` | Argument `r8` de `EVP_EncryptInit_ex` (GDB/Frida) |  
> | Padding | PKCS#7 (défaut OpenSSL) | Taille ciphertext = ⌈taille_plaintext / 16⌉ × 16 |  
> | Rotation de clé | Aucune | 6 appels capturés, clé et IV identiques à chaque appel |  
> | Dérivation de clé | Aucune | Clé utilisée brute depuis `.rodata`, pas d'appel à PBKDF2/scrypt |  
>  
> **Évaluation de la robustesse cryptographique : FAIBLE.** La clé est statique, en clair dans le binaire, et identique pour toutes les victimes potentielles. L'IV est statique et réutilisé pour chaque fichier, ce qui permet une analyse de patterns sur les premiers blocs chiffrés. La récupération des données est triviale pour quiconque possède le binaire.  
>  
> ### 4.3 Format des fichiers chiffrés  
>  
> ```  
> Offset    Taille    Type           Contenu  
> ─────────────────────────────────────────────────────  
> 0x00      8         char[8]        Magic : "RWARE27\0"  
> 0x08      8         uint64_t LE    Taille du fichier original  
> 0x10      variable  byte[]         Ciphertext AES-256-CBC (padding PKCS#7 inclus)  
> ```  
>  
> Un pattern ImHex (`.hexpat`) et une règle YARA de détection sont fournis en annexes.  
>  
> ### 4.4 Techniques d'évasion et anti-analyse  
>  
> **Aucune technique d'évasion n'a été identifiée.** Le sample ne détecte pas les débogueurs (`ptrace`, timing checks), ne vérifie pas son environnement d'exécution (VM detection), n'utilise pas de packing ni d'obfuscation de flux de contrôle. Le binaire est strippé (symboles internes supprimés), mais les symboles dynamiques restent intacts, ce qui constitue une protection minimale.

---

> ## 5. Évaluation de la menace  
>  
> ### 5.1 Classification  
>  
> | Critère | Évaluation |  
> |---|---|  
> | **Type** | Ransomware — chiffrement de fichiers |  
> | **Famille** | Inconnue (sample unique, contexte pédagogique) |  
> | **Cible** | Linux x86-64, répertoire `/tmp/test/` |  
> | **Sophistication** | Faible |  
> | **Impact potentiel** | Modéré (destruction de données dans le périmètre ciblé) |  
> | **Récupérabilité** | Totale (clé extraite, déchiffreur fonctionnel) |  
>  
> ### 5.2 Matrice ATT&CK  
>  
> | Tactique | Technique | ID | Détails |  
> |---|---|---|---|  
> | Execution | User Execution | T1204 | Le sample doit être exécuté manuellement |  
> | Discovery | File and Directory Discovery | T1083 | Parcours récursif de `/tmp/test/` |  
> | Impact | Data Encrypted for Impact | T1486 | Chiffrement AES-256-CBC des fichiers |  
> | Impact | Data Destruction | T1485 | Suppression des fichiers originaux (`unlink`) |  
>  
> Le faible nombre de techniques employées confirme la sophistication limitée du sample. On note l'absence de techniques de Defense Evasion (T1027 Obfuscated Files, T1140 Deobfuscate/Decode), de Persistence (T1053 Scheduled Task, T1543 Create System Service), et de Command and Control.

---

> ## 6. Recommandations  
>  
> ### 6.1 Actions immédiates (réponse à incident)  
>  
> **R1 — Déployer le déchiffreur.** Exécuter `decryptor.py` (annexe C) sur tous les systèmes affectés pour restaurer les fichiers. Valider l'intégrité par comparaison de hash SHA-256 avec les sauvegardes disponibles.  
>  
> **R2 — Scanner l'infrastructure.** Déployer les règles YARA (annexe D) sur les endpoints via l'EDR en place pour identifier d'éventuelles autres machines compromises. Scanner avec la règle `ransomware_ch27_locked_file` pour inventorier les fichiers chiffrés, et avec `ransomware_ch27_exact` pour localiser le binaire.  
>  
> **R3 — Préserver les preuves.** Avant toute restauration, capturer une image forensique des systèmes affectés (disque + mémoire). Conserver le binaire, les fichiers `.locked` et la ransom note comme pièces à conviction.  
>  
> **R4 — Identifier le vecteur d'infection.** L'analyse du sample ne révèle pas comment il a été déposé sur le système. Examiner les logs d'accès SSH, les historiques bash, les journaux d'authentification et les éventuels téléchargements récents pour reconstituer la chaîne d'attaque initiale.  
>  
> ### 6.2 Actions de remédiation (court terme)  
>  
> **R5 — Réinitialiser les accès.** Changer les mots de passe et clés SSH des comptes ayant accès aux systèmes affectés. Révoquer toute session active suspecte.  
>  
> **R6 — Auditer les permissions.** Vérifier que les répertoires sensibles ne sont pas en écriture pour des comptes non privilégiés. Appliquer le principe du moindre privilège.  
>  
> **R7 — Mettre à jour les signatures.** Intégrer les IOC (section 3) et les règles YARA (annexe D) dans les outils de détection (EDR, SIEM, antivirus) de l'organisation.  
>  
> ### 6.3 Actions de durcissement (long terme)  
>  
> **R8 — Sauvegardes.** Vérifier que des sauvegardes hors-ligne (air-gapped) existent pour les données critiques. Tester régulièrement la procédure de restauration. Une sauvegarde non testée n'est pas une sauvegarde.  
>  
> **R9 — Monitoring filesystem.** Déployer une surveillance des opérations de fichiers massives anormales : création rapide de nombreux fichiers avec une extension inhabituelle, suppression en série, pics d'utilisation CPU liés à du chiffrement. Des outils comme `auditd`, `inotifywait` ou les règles Sysmon (sur les endpoints Windows/Linux) peuvent détecter ce pattern.  
>  
> **R10 — Segmentation.** Isoler les partitions de données sensibles avec des montages en lecture seule lorsque l'écriture n'est pas nécessaire. Limiter l'accès aux répertoires par des ACL granulaires.

---

> ## 7. Annexes  
>  
> ### Annexe A — Graphe d'appels  
>  
> *(Insérer le graphe d'appels reconstruit dans Ghidra — section 27.3)*  
>  
> ### Annexe B — Pattern ImHex pour le format `.locked`  
>  
> *(Insérer le pattern `.hexpat` complet — section 27.3, Partie B)*  
>  
> ### Annexe C — Déchiffreur Python  
>  
> *(Insérer ou référencer `solutions/ch27-checkpoint-decryptor.py` — section 27.6)*  
>  
> Usage :  
> ```bash  
> pip install cryptography  
> python3 decryptor.py /tmp/test/          # déchiffre tout le répertoire  
> python3 decryptor.py fichier.txt.locked  # déchiffre un fichier unique  
> python3 decryptor.py --dry-run           # simule sans écrire  
> ```  
>  
> ### Annexe D — Règles YARA  
>  
> *(Insérer le fichier `yara-rules/ransomware_ch27.yar` — section 27.4)*  
>  
> Trois règles :  
> - `ransomware_ch27_exact` — Détection exacte du sample  
> - `ransomware_ch27_generic` — Détection de variantes par pattern comportemental  
> - `ransomware_ch27_locked_file` — Détection des fichiers chiffrés produits  
>  
> ### Annexe E — Traces d'exécution  
>  
> *(Insérer les extraits pertinents des logs GDB, Frida et strace — section 27.5)*  
>  
> ### Annexe F — Chronologie de l'analyse  
>  
> | Étape | Durée estimée | Outils | Résultat clé |  
> |---|---|---|---|  
> | Triage rapide | 10 min | `file`, `strings`, `checksec`, `ldd`, `readelf` | Hypothèses H1–H8 formulées |  
> | Analyse statique | 45 min | Ghidra, ImHex | Graphe d'appels complet, clé/IV localisés, format `.locked` cartographié |  
> | Règles YARA | 20 min | ImHex, YARA | 3 règles de détection opérationnelles |  
> | Analyse dynamique | 30 min | GDB, Frida, `strace` | Confirmation définitive clé/IV/comportement |  
> | Déchiffreur | 30 min | Python, `cryptography` | Script fonctionnel, validation par hash |  
> | Rapport | 30 min | — | Ce document |  
> | **Total** | **~2h45** | | |

---

## Conseils pour vos futurs rapports

### Adapter le niveau de détail à l'audience

Le template ci-dessus est volontairement complet. En pratique, adaptez la granularité au contexte. Un rapport interne pour l'équipe IR d'une entreprise peut être plus technique et moins formel. Un rapport destiné à un CERT national ou à un partage sectoriel (ISAC) sera plus structuré et suivra les conventions de la communauté (STIX/TAXII pour les IOC, TLP pour la classification, MITRE ATT&CK pour la taxonomie).

### Utiliser un système de référencement cohérent

Numérotez vos IOC, vos recommandations et vos annexes. Quand le rapport est discuté en réunion, pouvoir dire « voir IOC-3 » ou « appliquer R7 » est infiniment plus efficace que « la règle YARA dont je parlais tout à l'heure ».

### Inclure les limitations et les questions ouvertes

Un rapport honnête mentionne ce qu'il ne sait pas. Dans notre cas : le vecteur d'infection initial est inconnu (R4), et nous ne pouvons pas exclure l'existence de variantes avec des clés différentes (la règle YARA générique couvre ce risque, mais sans certitude). Mentionner ces zones d'ombre renforce la crédibilité du rapport plutôt que de l'affaiblir.

### Versionner le rapport

Utilisez un numéro de révision et une date. L'analyse peut évoluer : une variante du sample pourrait apparaître, un nouveau vecteur d'infection pourrait être identifié, ou une erreur pourrait être corrigée. Un rapport versionné permet de tracer ces évolutions sans ambiguïté.

### Archiver le dossier complet

Le rapport seul n'est pas suffisant. Archivez l'ensemble du dossier d'analyse dans un répertoire structuré :

```
analyse-MAL-2025-CH27-001/
├── rapport.md                    ← Le rapport final
├── sample/
│   ├── ransomware_O2_strip       ← Le binaire analysé
│   └── sha256sums.txt            ← Hashes de référence
├── ghidra/
│   └── Ch27-Ransomware.gpr       ← Projet Ghidra avec renommages
├── imhex/
│   ├── locked_format.hexpat       ← Pattern du format .locked
│   └── crypto_constants.hexpat    ← Pattern des constantes dans l'ELF
├── yara/
│   └── ransomware_ch27.yar        ← Règles YARA
├── scripts/
│   ├── decryptor.py               ← Déchiffreur
│   ├── extract_crypto_params.py   ← Script GDB Python
│   ├── hook_evp.js                ← Script Frida
│   └── hook_evp_full.js           ← Script Frida étendu
├── traces/
│   ├── strace_output.txt          ← Trace strace complète
│   ├── frida_log.txt              ← Sortie Frida
│   └── crypto_params.json         ← Export GDB des paramètres crypto
└── locked_samples/
    ├── document.txt.locked        ← Échantillons de fichiers chiffrés
    └── budget.csv.locked
```

Cette arborescence permet à un analyste de reprendre le dossier des mois plus tard, ou à un pair de reproduire et vérifier chaque conclusion du rapport.

---

## Ce que ce rapport vous apprend sur la discipline

Au-delà du contenu technique, la rédaction de ce rapport illustre plusieurs aspects fondamentaux du métier d'analyste :

**La traçabilité est non négociable.** Chaque affirmation renvoie à une preuve. Chaque preuve est reproductible. Chaque outil et sa version sont documentés. Cette rigueur protège l'analyste et l'organisation — juridiquement en cas de litige, techniquement en cas de remise en question.

**L'analyse n'est terminée que lorsque le rapport est écrit.** La tentation est forte de s'arrêter une fois le déchiffreur fonctionnel. Mais sans rapport, les IOC ne sont pas partagés, les recommandations ne sont pas émises, et la connaissance reste individuelle. Le rapport est ce qui transforme une compétence technique en valeur organisationnelle.

**Le rapport est un livrable vivant.** Il sera relu, critiqué, complété, mis à jour. Écrivez-le pour votre futur vous autant que pour vos lecteurs immédiats. Dans six mois, quand une variante apparaîtra, vous serez reconnaissant d'avoir documenté chaque décision et chaque observation.

⏭️ [🎯 Checkpoint : déchiffrer les fichiers et produire un rapport complet](/27-ransomware/checkpoint.md)
