🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Corrigé — Checkpoint Chapitre 14

> ⚠️ **Spoilers** — Ce document contient la solution complète du checkpoint. Essayez de réaliser l'exercice par vous-même avant de consulter ce corrigé.

---

## Préparation de l'environnement

### Fichier de test et commandes de lancement

```bash
# Créer un fichier de test de taille connue (64 octets de texte ASCII)
python3 -c "print('A'*63)" > test_64.txt

# Créer un second fichier plus grand pour comparaison (512 octets)
python3 -c "print('B'*511)" > test_512.txt

# Désactiver l'ASLR pour des adresses stables entre les runs
# (nécessaire si le binaire est PIE)
sudo sysctl -w kernel.randomize_va_space=0
# OU, par commande, sans modifier le système :
# setarch x86_64 -R valgrind [options] ./ch14-crypto [args]
```

### Runs d'analyse

```bash
cd binaries/ch14-crypto/

# ── Run 1 : Memcheck (version -O0 avec symboles) ──
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --track-fds=yes \
    --verbose \
    --log-file=../../analysis/01_memcheck_64.txt \
    ./crypto_O0 encrypt ../../test_64.txt ../../out_64.enc S3cretP@ss

valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --log-file=../../analysis/01_memcheck_512.txt \
    ./crypto_O0 encrypt ../../test_512.txt ../../out_512.enc S3cretP@ss

# ── Run 2 : Callgrind ──
valgrind \
    --tool=callgrind \
    --callgrind-out-file=../../analysis/02_callgrind_64.out \
    --collect-jumps=yes \
    ./crypto_O0 encrypt ../../test_64.txt ../../out_64.enc S3cretP@ss

# ── Run 3 : ASan + UBSan (recompilation) ──
make clean  
CC=gcc CFLAGS="-fsanitize=address,undefined -g -O0 -fno-omit-frame-pointer" make  
ASAN_OPTIONS="halt_on_error=0:detect_leaks=1:log_path=../../analysis/03_asan" \  
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \  
    ./crypto_asan encrypt ../../test_64.txt ../../out_asan.enc S3cretP@ss

# ── Run 4 : MSan (Clang, si disponible) ──
clang -fsanitize=memory -fsanitize-memory-track-origins=2 -g -O0 \
    -o crypto_msan crypto.c -lm
MSAN_OPTIONS="halt_on_error=0:log_path=../../analysis/04_msan" \
    ./crypto_msan encrypt ../../test_64.txt ../../out_msan.enc S3cretP@ss
```

---

## Livrable 1 — Carte des allocations

### Extraction depuis Memcheck

Le `HEAP SUMMARY` du rapport Memcheck (input 64 octets) affiche :

```
==23456== HEAP SUMMARY:
==23456==     in use at exit: 1,376 bytes in 7 blocks
==23456==   total heap usage: 19 allocs, 15 frees, 6,208 bytes allocated
```

19 allocations, 15 libérations → 4 blocs du programme non libérés à la sortie (les 3 blocs restants sont des buffers internes de la libc, catégorisés « still reachable »). En croisant avec les fuites détaillées et les rapports ASan, on obtient la carte complète :

### Carte des allocations — `ch14-crypto` (input : 64 octets)

| ID | Taille (octets) | Fonction d'allocation | Nb allocs | Fonction de libération | Catégorie Memcheck | Hypothèse |  
|----|-----------------|----------------------|-----------|----------------------|-------------------|-----------|  
| A1 | 32 | `0x401B89` (`derive_key`) | 1 | — | still reachable | **Clé AES-256** (256 bits = 32 octets) |  
| A2 | 16 | `0x401B45` (`prepare_iv`) | 1 | — | still reachable | **IV AES-CBC/CTR** (128 bits = 16 octets = taille bloc AES) |  
| A3 | 4096 | `0x4019F0` (`init_io`) | 1 | `0x401F10` (`cleanup`) | — (libéré) | Buffer de lecture fichier (taille page) |  
| A4 | 128 | `0x401DA1` (`write_block`) | 5 | `0x401E80` (`write_block`) | — (libéré) | Buffer de sortie — header + ciphertext |  
| A5 | 240 | `0x401C30` (`init_aes_ctx`) | 1 | — | still reachable | **Contexte AES** (expanded key + état) |  
| A6 | 1024 | `0x4018A0` (`read_password`) | 1 | `0x4018F0` (`read_password`) | — (libéré) | Buffer temporaire de lecture mot de passe |  
| A7 | 64 | `0x401A20` (`generate_salt`) | 1 | — | definitely lost | **Sel de dérivation** (512 bits = taille bloc SHA-256) |  
| A8 | 32 | `0x401A60` (`derive_key`) | 1 | `0x401AE0` (`derive_key`) | — (libéré) | Buffer temporaire de dérivation (HMAC output) |

**Observations clés :**

- **4 blocs non libérés du programme** : A1 (clé), A2 (IV) et A5 (contexte) sont des structures crypto accessibles depuis des variables locales ou globales au moment de la sortie — Memcheck les catégorise comme « still reachable ». A7 (sel) est « definitely lost » car le pointeur est perdu au retour de `derive_key()` (variable locale). Ce sont les structures crypto qui persistent pendant toute la durée du chiffrement et que le développeur n'a pas nettoyées — en situation réelle, c'est une mauvaise pratique de sécurité (les clés restent en mémoire).  
- **A3 est de taille fixe (4096 octets)** quel que soit l'input → le buffer de lecture est une page, pas proportionnel au fichier.  
- **A4 est alloué et libéré 5 fois** pour un input de 64 octets → 5 blocs de chiffrement traités (voir explication ci-dessous).

### Pourquoi 5 appels à encrypt_block et non 4 ?

L'input fait 64 octets. Avec un bloc AES de 16 octets, cela donne 64 / 16 = 4 blocs de données. Cependant, le code ajoute un **bloc de padding PKCS7** lorsque la taille de l'input est un multiple exact de `BLOCK_SIZE` (condition visible dans `process_encrypt()`). Ce bloc supplémentaire de 16 octets de padding est chiffré comme les autres, ce qui porte le total à **5 appels** à `encrypt_block` et 5 allocations/libérations de A4.

C'est un détail que Callgrind révèle directement : le nombre d'appels à `encrypt_block` est 5, pas 4. Si on n'avait pas Callgrind, on pourrait aussi le déduire en comptant les allocations de A4 dans le rapport Memcheck (en analysant les piles d'appels de `malloc` dans `write_block`).

### Vérification par comparaison d'inputs

| Bloc | Input 64 oct. | Input 512 oct. | Conclusion |  
|------|--------------|----------------|------------|  
| A1 | 32 | 32 | Taille fixe → clé |  
| A2 | 16 | 16 | Taille fixe → IV |  
| A3 | 4096 | 4096 | Taille fixe → buffer page |  
| A4 | 128 × 5 allocs | 128 × 33 allocs | Taille fixe, nb allocs variable → bloc de sortie |  
| A5 | 240 | 240 | Taille fixe → contexte crypto |  
| A7 | 64 | 64 | Taille fixe → hash/sel |

Tous les blocs sont à taille fixe. Le seul élément qui varie est le **nombre d'allocations** de A4, qui suit le nombre de blocs de chiffrement (proportionnel à la taille du fichier). Pour 512 octets : 512/16 = 32 blocs data + 1 bloc padding = 33 appels — ce que Callgrind confirme.

---

## Livrable 2 — Identification des buffers de clés

### Buffer de clé — Bloc A1 (32 octets)

**Identification :**

| Source | Observation | Conclusion |  
|--------|-------------|------------|  
| Memcheck | Bloc de 32 octets, still reachable, alloué par `0x401B89` | Structure de 32 octets persistante |  
| Memcheck | « Uninitialised value was created by a heap allocation at 0x401B89 » puis « Uninitialised value was stored by 0x401AC0 » | Le bloc est alloué vide puis rempli par `0x401AC0` (dérivation) |  
| Callgrind | `0x401B89` appelée 1 fois, coût self 0.2% | Fonction d'allocation simple (pas de calcul) |  
| Callgrind | `0x401AC0` appelée 1 fois, coût self 8.3% | Fonction de dérivation (calcul significatif) |  
| ASan | Aucune erreur sur ce bloc | Accès toujours dans les limites |  
| UBSan | Signed integer overflow dans `0x401AC0` (« 2147483600 + 217 cannot be represented in type 'int' ») | Additions modulaires dans la dérivation → typique d'un hash |

**Conclusion — double confirmation :**
1. La taille de 32 octets = 256 bits correspond exactement à une clé AES-256 (Memcheck taille + connaissance crypto).  
2. La fonction qui remplit le bloc (`0x401AC0`) consomme 8.3% du CPU et déclenche des signed overflows typiques de hash → c'est une fonction de dérivation de clé de type PBKDF2 ou HKDF (Callgrind coût + UBSan overflow).

**Flux de la clé :**
```
argv[4] ("S3cretP@ss")
    │
    ▼
read_password (0x4018A0) — lit le mot de passe dans A6 (1024 oct.)
    │
    ▼
derive_key (0x401B89) — malloc(32) → A1
    │
    ├── derive_hash (0x401AC0) — SHA-256-like, écrit 32 octets dans A1
    │   └── [UBSan: signed overflow — additions modulaires]
    │   └── [Callgrind: 8.3% du total]
    │
    ▼
expand_key (0x401C12) — lit A1, écrit dans A5+20 (round keys)
    │
    ▼
encrypt_block (0x401C7E) — lit A5+20 à chaque appel (14 rounds)
    └── [Callgrind: 65% du total — hotspot crypto]
```

### Buffer IV — Bloc A2 (16 octets)

**Identification :**

| Source | Observation | Conclusion |  
|--------|-------------|------------|  
| Memcheck | Bloc de 16 octets, still reachable, alloué par `0x401B45` | Structure de 16 octets persistante |  
| Memcheck | « Conditional jump or move depends on uninitialised value(s) at 0x401C7E » — origine : allocation à `0x401B45` | Les 16 octets ne sont **pas tous initialisés** |  
| MSan | « Uninitialized value created by heap allocation at 0x401B45, stored at 0x401B70, used at 0x401C7E » | Flux : alloc → écriture partielle → utilisation dans `encrypt_block` |  
| Callgrind | `0x401B45` appelée 1 fois, coût self 0.1% | Allocation simple |  
| Callgrind | `0x401B70` appelée 1 fois, coût self 0.4% | Écriture de l'IV (peu de calcul) |

**Conclusion — double confirmation :**
1. Taille 16 octets = 128 bits = taille d'un bloc AES, correspondant à un IV pour les modes CBC ou CTR (Memcheck taille + connaissance crypto).  
2. Memcheck et MSan signalent tous deux que certains octets ne sont pas initialisés avant utilisation dans `encrypt_block` — cela confirme que c'est un buffer de données sensibles (l'IV) partiellement rempli (seuls 12 octets sur 16 sont écrits par `prepare_iv`), ce qui constitue par ailleurs une **vulnérabilité** : IV à entropie réduite (Memcheck uninitialised + MSan uninitialised).

> ⚠️ **Note sécurité** : un IV partiellement non initialisé est une faille cryptographique réelle. En mode CBC, cela réduit l'entropie de l'IV et peut compromettre la confidentialité. Ce constat issu de Memcheck/MSan serait une finding légitime dans un audit de sécurité.

### Contexte crypto — Bloc A5 (240 octets)

**Identification :**

| Source | Observation | Conclusion |  
|--------|-------------|------------|  
| Memcheck | Bloc de 240 octets, still reachable (via global `g_ctx`), alloué par `0x401C30` | Structure persistante globale |  
| ASan (frame layout) | Accès read size 4 à offset 0 | Premier champ : 4 octets (uint32_t, mode/algo) |  
| Memcheck | Write size 16 à offset 4 dans le bloc | Champ de 16 octets à l'offset 4 (copie de l'IV) |  
| Memcheck | Write size 32 à offset 20 — depuis `expand_key` | Début de la zone des round keys |  
| Callgrind | `encrypt_block` lit le bloc A5 à chaque itération, coût 65% | Le bloc contient les données lues en boucle serrée → état central du chiffrement |  
| MSan | Octets [236, 240) jamais initialisés | 4 derniers octets = padding d'alignement |

**Conclusion :** le bloc A5 est le **contexte AES complet** contenant le mode de chiffrement (4 octets), une copie de travail de l'IV / state CBC (16 octets), et les round keys expansées à partir de l'offset 20. La zone des round keys occupe [20, 236) = 216 octets, ce qui est cohérent avec 14 rounds de key schedule AES-256 produisant 15 × 16 = 240 octets de round keys — mais seuls 216 octets sont effectivement écrits selon Memcheck. Ce décalage suggère un layout interne légèrement différent (les round keys pourraient se chevaucher cycliquement via un modulo). Ce point devra être affiné dans Ghidra en examinant la boucle de `expand_key`.

---

## Livrable 3 — Graphe fonctionnel de la chaîne crypto

### Données Callgrind brutes (input 64 octets)

```
callgrind_annotate --inclusive=yes 02_callgrind_64.out
```

Résultat trié par coût inclusif :

```
Ir (inclusive)    Fonction
──────────────    ──────────────────────
  2,847,391      0x4012E8  [main]                  100.0%
  2,614,205      0x401DA1  [process_file]           91.8%
  1,851,230      0x401C7E  [encrypt_block]          65.0%
    498,712      0x401C12  [expand_key]             17.5%
    236,445      0x401AC0  [derive_hash]             8.3%
     67,891      0x401B89  [derive_key wrapper]      2.4%
     43,210      0x401E23  [write_block]             1.5%
     38,990      0x401C80  [read_block]              1.4%
     12,450      0x401B45  [prepare_iv]              0.4%
      8,230      0x4019F0  [init_io]                 0.3%
      5,100      0x401C30  [init_aes_ctx]            0.2%
      3,200      0x401F10  [cleanup]                 0.1%
```

### Graphe consolidé

```
main (0x4012E8) ──────────────────────────────────────────── Incl: 100%, Self: 1.5%
│
├──► init_io (0x4019F0) ─────────────────────────────────── Incl: 0.3%, Self: 0.3%
│    └── malloc(4096) → A3
│
├──► derive_key (0x401B89) ──────────────────────────────── Incl: 10.7%, Self: 0.1%
│    ├── malloc(32) → A1 (clé brute)
│    ├── malloc(64) → A7 (sel — definitely lost, pointeur perdu)
│    ├──► derive_hash (0x401AC0) ────────────────────────── Incl: 8.3%, Self: 8.3%
│    │    ├── [UBSan: signed overflow ×12 — rounds de hash]
│    │    ├── [Callgrind: boucle interne 64 itérations → SHA-256]
│    │    └── Écrit 32 octets dans A1
│    └── free(A8) (buffer temp HMAC)
│
├──► prepare_iv (0x401B45) ──────────────────────────────── Incl: 0.4%, Self: 0.4%
│    ├── malloc(16) → A2 (IV)
│    ├── Écrit 12 octets sur 16 (3 × memcpy de 4 oct.)
│    └── [MSan: 4 octets [12..15] non initialisés dans A2]
│
├──► init_aes_ctx (0x401C30) ────────────────────────────── Incl: 0.2%, Self: 0.2%
│    └── malloc(240) → A5 (contexte AES)
│
├──► expand_key (0x401C12) ──────────────────────────────── Incl: 17.5%, Self: 17.5%
│    ├── Lit A1 (clé 32 octets)
│    ├── Écrit dans A5+20 (round keys)
│    └── [Callgrind: boucle de 14 itérations → AES-256 key schedule]
│
├──► process_file (0x401DA1) ────────────────────────────── Incl: 91.8%, Self: 0.4%
│    │
│    │   ┌──── Boucle : 5 itérations (4 data + 1 padding) ──────────────┐
│    │   │                                                              │
│    ├───┤► read_block (0x401C80) ───────────────────────── Self: 1.4%
│    │   │   └── read() → remplit A3 (4096 oct.)
│    │   │
│    ├───┤► encrypt_block (0x401C7E) ─── ★ HOTSPOT ─────── Self: 65.0%
│    │   │   ├── Lit A5+20 (round keys) à chaque appel
│    │   │   ├── Lit A5+4 (iv_state) pour le chaînage CBC
│    │   │   ├── [Callgrind: boucle interne 14 itérations → 14 rounds]
│    │   │   ├── [Callgrind: sous-boucle 16 itérations → 16 octets]
│    │   │   ├── [UBSan: signed overflow dans les rounds]
│    │   │   └── [Memcheck: conditional jump on uninitialised A2]
│    │   │
│    ├───┤► write_block (0x401E23) ──────────────────────── Self: 1.5%
│    │   │   ├── malloc(128) → A4 (buffer sortie)
│    │   │   ├── write() → fichier de sortie
│    │   │   └── free(A4)
│    │   │
│    │   └──────────────────────────────────────────────────────────────┘
│    │
│    └── [Callgrind: encrypt_block appelée 5x pour 64 oct. input
│         → 4 blocs de 16 octets + 1 bloc padding PKCS7 = 5 appels]
│
└──► cleanup (0x401F10) ─────────────────────────────────── Incl: 0.1%, Self: 0.1%
     └── free(A3) — seul buffer explicitement libéré
     [A1, A2, A5 still reachable ; A7 definitely lost]
```

### Identification de l'algorithme

Les indices convergent vers **AES-256-CBC** :

| Indice | Source | Confirmation |  
|--------|--------|-------------|  
| Clé de 32 octets (256 bits) | Memcheck, bloc A1 | AES-**256** |  
| IV de 16 octets (128 bits) | Memcheck, bloc A2 | Mode à IV (CBC, CTR, OFB…) |  
| 14 itérations dans `expand_key` | Callgrind jumps | AES-256 = 14 rounds |  
| Sous-boucle de 16 dans `encrypt_block` | Callgrind | Bloc AES = 16 octets |  
| 5 appels à `encrypt_block` pour 64 oct. d'input | Callgrind calls | 64 / 16 = 4 blocs data + 1 padding PKCS7 |  
| Lecture de l'iv_state à chaque bloc dans `encrypt_block` | Memcheck + Callgrind | Mode **CBC** (chaînage par XOR avec le bloc précédent) |  
| Contexte de 240 octets (round keys + IV state) | Memcheck, bloc A5 | Expanded key AES-256 + métadonnées |

---

## Livrable 4 — Structures C reconstruites

### Structure `cipher_ctx` (Bloc A5, 240 octets)

```c
/*
 * Structure reconstruite : cipher_ctx
 * Taille totale : 240 octets (confirmé Memcheck : still reachable, 240 bytes)
 * Alloué par : init_aes_ctx (0x401C30)
 * Rempli par : main (mode + IV), expand_key (round keys)
 * Utilisé par : encrypt_block (0x401C7E) à chaque itération
 */
struct cipher_ctx {
    /* Offset 0, taille 4 — mode de chiffrement (ex: 1 = CBC, 2 = CTR)
     * Source : ASan read size 4 à offset 0 dans encrypt_block
     * Source : Callgrind — lu 1 fois par appel à encrypt_block (dispatch) */
    uint32_t  mode;

    /* Offset 4, taille 16 — copie de travail de l'IV / state CBC
     * Source : Memcheck write size 16 à offset 4 depuis prepare_iv/main
     * Source : Memcheck read size 16 à offset 4 dans encrypt_block
     * Note : mis à jour à chaque bloc (chaînage CBC — le state courant
     *        est le dernier ciphertext produit) */
    uint8_t   iv_state[16];

    /* Offset 20, taille 216 — round keys AES-256 (zone d'expansion)
     * Source : Memcheck write size 32 à offset 20 depuis expand_key (premiers 32 oct.)
     * Source : Callgrind — expand_key itère 14 fois (key schedule AES-256)
     * Source : encrypt_block lit cette zone à chaque round (14 accès par bloc)
     * Note : 14 rounds → 15 round keys × 16 octets = 240 octets théoriques,
     *        mais l'expansion utilise un index cyclique modulo 216.
     *        Point à vérifier dans Ghidra. */
    uint8_t   round_keys[216];

    /* Offset 236, taille 4 — padding / non utilisé
     * Source : MSan — octets [236, 240) jamais initialisés
     * Hypothèse : padding d'alignement imposé par malloc ou le développeur */
    uint8_t   _padding[4];

};  /* Total : 4 + 16 + 216 + 4 = 240 octets ✓ */
```

### Structure `output_block` (Bloc A4, 128 octets)

```c
/*
 * Structure reconstruite : output_block
 * Taille totale : 128 octets (confirmé Memcheck : alloc/free dans write_block)
 * Alloué et libéré par : write_block (0x401E23 / 0x401E80) — 1 alloc par bloc chiffré
 * Nombre d'instances par exécution : = nombre de blocs (5 pour input 64 oct.)
 */
struct output_block {
    /* Offset 0, taille 8 — header du bloc de sortie
     * Source : Memcheck — les 8 premiers octets sont toujours initialisés
     * Source : Memcheck syscall write — ces 8 octets sont écrits en premier
     * Hypothèse : taille du payload (uint32_t) + flags/padding (uint32_t) */
    uint32_t  payload_size;
    uint32_t  flags;

    /* Offset 8, taille 120 — payload chiffré
     * Source : Memcheck — « Syscall param write(buf) points to uninitialised
     *          byte(s), Address is 8 bytes inside block of size 128 »
     * Note : seuls les 16 premiers octets du payload sont du ciphertext réel.
     *        Les 104 octets restants ne sont jamais écrits par le programme
     *        (le fwrite envoie les 128 octets complets) → explique l'erreur
     *        Memcheck sur l'écriture de données non initialisées. */
    uint8_t   ciphertext[120];

};  /* Total : 4 + 4 + 120 = 128 octets ✓ */
```

### Type `raw_key_t` (Bloc A1, 32 octets)

```c
/*
 * Type reconstruit : raw_key_t
 * Taille : 32 octets (confirmé Memcheck : still reachable, 32 bytes)
 * Alloué par : derive_key (0x401B89)
 * Écrit par : derive_hash (0x401AC0) — dérivation SHA-256-like
 * Lu par : expand_key (0x401C12) — expansion en round keys
 * Jamais libéré — pointeur accessible dans le frame de main à la sortie
 */
typedef uint8_t raw_key_t[32];  /* Clé AES-256 dérivée du mot de passe */
```

### Type `iv_t` (Bloc A2, 16 octets)

```c
/*
 * Type reconstruit : iv_t
 * Taille : 16 octets (confirmé Memcheck : still reachable, 16 bytes)
 * Alloué par : prepare_iv (0x401B45)
 * Partiellement écrit par : 0x401B70 (12 octets sur 16 initialisés :
 *     3 × memcpy de 4 octets pour les offsets [0..3], [4..7], [8..11])
 * Copié dans : cipher_ctx.iv_state (offset 4 de A5)
 * Jamais libéré — pointeur accessible dans le frame de main à la sortie
 *
 * ⚠️ VULNÉRABILITÉ : 4 octets [12..15] de l'IV ne sont pas initialisés
 *    (confirmé Memcheck + MSan). Entropie réduite, IV partiellement
 *    prévisible — les octets non initialisés proviennent du tas et
 *    contiennent des résidus d'allocations précédentes.
 */
typedef uint8_t iv_t[16];  /* IV AES-CBC — WARNING: partiellement non initialisé */
```

---

## Synthèse — Flux de données sensibles (ACRF étape F)

```
                        ┌──────────────────────────┐
                        │   argv[4] = password     │
                        └────────────┬─────────────┘
                                     │
                                     ▼
                        ┌──────────────────────────┐
                        │  read_password (0x4018A0)│
                        │  A6 = malloc(1024)       │
                        │  → copie le password     │
                        │  → free(A6) après usage  │
                        └────────────┬─────────────┘
                                     │
                      ┌──────────────┴──────────────┐
                      ▼                             ▼
        ┌──────────────────────┐       ┌──────────────────────┐
        │ derive_key (0x401B89)│       │ prepare_iv (0x401B45)│
        │ A1 = malloc(32) clé  │       │ A2 = malloc(16) IV   │
        │ A7 = malloc(64) sel  │       │ 3 × memcpy(4) → 12/16│
        │ A8 = malloc(32) tmp  │       │ ⚠️ [12..15] non init.│
        └──────────┬───────────┘       └──────────┬───────────┘
                   │                              │
                   ▼                              │
        ┌──────────────────────┐                  │
        │derive_hash (0x401AC0)│                  │
        │ SHA-256-like         │                  │
        │ 64 rounds internes   │                  │
        │ Écrit 32 oct. → A1   │                  │
        │ [UBSan: overflow]    │                  │
        └──────────┬───────────┘                  │
                   │                              │
                   ▼                              ▼
        ┌──────────────────────────────────────────────┐
        │          init_aes_ctx (0x401C30)             │
        │          A5 = malloc(240) contexte           │
        │          A5.mode = CBC (offset 0, 4 oct.)    │
        │          A5.iv_state = copie de A2 (offset 4)│
        └──────────────────────┬───────────────────────┘
                               │
                               ▼
        ┌──────────────────────────────────────────────┐
        │          expand_key (0x401C12)               │
        │          Lit A1 (32 oct. — clé brute)        │
        │          14 rounds de key schedule           │
        │          Écrit round keys dans A5+20         │
        │          [Callgrind: 17.5%]                  │
        └──────────────────────┬───────────────────────┘
                               │
                               ▼
        ┌──────────────────────────────────────────────┐
        │          process_file (0x401DA1)             │
        │                                              │
        │    ┌── Boucle (5× pour 64 oct. input) ───┐   │
        │    │    4 blocs data + 1 bloc padding    │   │
        │    │                                     │   │
        │    │  read_block ── read(A3) ────────────│─┐ │
        │    │       │                             │ │ │
        │    │       ▼                             │ │ │
        │    │  encrypt_block ★ 65%               │ │ │
        │    │  │  Lit A5.round_keys (14 rounds)   │ │ │
        │    │  │  Lit A5.iv_state (chaînage CBC)  │ │ │
        │    │  │  XOR plaintext ⊕ iv_state        │ │ │
        │    │  │  14 rounds (sub + perm + XOR rk) │ │ │
        │    │  │  Maj A5.iv_state = ciphertext    │ │ │
        │    │  └──────────┬───────────────────────│─┘ │
        │    │             ▼                       │   │
        │    │  write_block (A4 = 128 oct.)        │   │
        │    │       │  header (8 oct.) + ct (16)  │   │
        │    │       │  ⚠️ 104 oct. non init écrits│   │
        │    │       └──► write() → output.enc     │   │
        │    │                                     │   │
        │    └─────────────────────────────────────┘   │
        └──────────────────────────────────────────────┘
                               │
                               ▼
        ┌──────────────────────────────────────────────┐
        │          cleanup (0x401F10)                  │
        │          free(A3) — seul buffer libéré       │
        │          A1, A2 still reachable (locals main)│
        │          A5 still reachable (global g_ctx)   │
        │          A7 definitely lost (ptr perdu)      │
        └──────────────────────────────────────────────┘
```

---

## Transfert vers Ghidra — renommages proposés

| Adresse | Nom proposé | Justification |  
|---------|-------------|---------------|  
| `0x4012E8` | `main` | Point d'entrée, coût inclusif 100%, coût self ~1.5% |  
| `0x4018A0` | `read_password` | Alloue/libère A6 (1024), lit depuis argv |  
| `0x4019F0` | `init_io` | Alloue A3 (4096), buffer de lecture fichier |  
| `0x401A20` | `generate_salt` | Alloue A7 (64), sel pour dérivation |  
| `0x401AC0` | `derive_hash_sha256` | Hotspot dérivation, 64 itérations internes, UBSan overflows |  
| `0x401B45` | `prepare_iv` | Alloue A2 (16), IV partiellement non init. (12/16 octets) |  
| `0x401B89` | `derive_key` | Orchestre la dérivation, alloue A1 (32) |  
| `0x401C12` | `aes256_expand_key` | Lit A1, écrit A5+20, 14 itérations key schedule |  
| `0x401C30` | `init_aes_ctx` | Alloue A5 (240), contexte AES global |  
| `0x401C7E` | `aes256_encrypt_block` | Hotspot 65%, 14 rounds × 16 oct., lit A5 |  
| `0x401C80` | `read_block` | Lit depuis le fichier source dans A3 |  
| `0x401DA1` | `process_file` | Orchestrateur boucle read/encrypt/write, 5 itérations |  
| `0x401E23` | `write_block` | Alloue A4 (128), écrit header + ciphertext |  
| `0x401F10` | `cleanup` | Libère A3 uniquement, ne libère pas les buffers crypto |

---

## Checklist d'auto-évaluation — résultat

- [x] **Au moins deux blocs crypto identifiés** — A1 (clé 32 oct.), A2 (IV 16 oct.), A5 (contexte 240 oct.), A7 (sel 64 oct.) = quatre blocs identifiés.  
- [x] **Flux de la clé tracé de bout en bout** — password → read_password → derive_key → derive_hash → A1 → expand_key → A5.round_keys → encrypt_block.  
- [x] **Graphe distinguant init / traitement / finalisation** — init (0x4018A0 → 0x401C30), traitement (0x401DA1 boucle de 5 blocs), finalisation (0x401F10).  
- [x] **Chaque champ justifié par au moins une source** — cipher_ctx : 4 champs, chacun avec 2+ sources (Memcheck + ASan ou Callgrind).  
- [x] **Deux inputs testés** — 64 octets et 512 octets, confirmant les tailles fixes et le scaling du nombre de blocs (5 vs 33).  
- [x] **Prêt à renommer dans Ghidra** — 14 fonctions avec noms et rôles identifiés.

---


⏭️
