🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 27.3 — Analyse statique : Ghidra + ImHex (repérer les constantes AES, flux de chiffrement)

> 🔍 **Objectif de cette section** : passer du triage (hypothèses) à l'analyse approfondie (certitudes). Nous allons importer le sample dans Ghidra pour reconstruire le flux de chiffrement fonction par fonction, puis utiliser ImHex pour cartographier les constantes cryptographiques dans le binaire et analyser le format des fichiers `.locked` produits.  
>  
> Nous travaillons sur la variante **`ransomware_O2_strip`** (optimisée, sans symboles). Les captures et adresses peuvent varier selon votre environnement, mais les patterns restent identiques.

---

## Partie A — Analyse dans Ghidra

### Import et analyse automatique

Lancez Ghidra, créez un projet dédié (par exemple `Ch27-Ransomware`), puis importez `ransomware_O2_strip` via *File → Import File*. Ghidra détecte automatiquement le format ELF x86-64. Acceptez les options par défaut et lancez l'analyse automatique (*Auto Analyze*) en cochant au minimum :

- **Decompiler Parameter ID** — reconstitution des paramètres de fonctions  
- **Aggressive Instruction Finder** — important pour un binaire strippé  
- **Function Start Search** — heuristiques de détection de prologues de fonctions  
- **Shared Return Calls** — détection de fonctions partageant un épilogue (courant en `-O2`)

L'analyse prend quelques secondes sur un sample de cette taille. Une fois terminée, le CodeBrowser s'ouvre avec le listing désassemblé à gauche et le décompilateur à droite.

### Orientation dans un binaire strippé

Sans symboles, Ghidra attribue des noms génériques aux fonctions internes : `FUN_00101340`, `FUN_001014a0`, etc. Seules les fonctions importées depuis les bibliothèques partagées conservent leur nom (`EVP_EncryptInit_ex`, `opendir`, `fopen`...) car elles font partie de la table `.dynsym`.

Notre stratégie d'exploration repose donc sur ces **points d'ancrage nommés**. Nous partirons des fonctions OpenSSL — dont nous connaissons l'existence grâce au triage — pour remonter vers les fonctions internes qui les appellent.

### Localiser les appels à l'API EVP d'OpenSSL

Ouvrez la fenêtre *Symbol Tree* (à gauche) et dépliez la section *Imports* ou *External Functions*. Vous y trouverez les fonctions OpenSSL importées. Alternativement, utilisez le filtre de la fenêtre *Symbol Table* (*Window → Symbol Table*) et tapez `EVP` :

```
EVP_CIPHER_CTX_new  
EVP_CIPHER_CTX_free  
EVP_EncryptInit_ex  
EVP_EncryptUpdate  
EVP_EncryptFinal_ex  
EVP_aes_256_cbc  
```

La présence de `EVP_aes_256_cbc` est une confirmation directe de l'algorithme : cette fonction retourne un pointeur vers la structure décrivant AES-256-CBC dans OpenSSL. Son appel est l'équivalent en code de la déclaration « j'utilise AES-256 en mode CBC ».

### Remonter depuis `EVP_EncryptInit_ex` par cross-reference

Faites un clic droit sur `EVP_EncryptInit_ex` dans le Symbol Tree, puis *References → Show References to*. Ghidra affiche la liste des endroits du code qui appellent cette fonction. Sur notre sample, il n'y aura qu'**un seul appel** — c'est la fonction interne qui encapsule le chiffrement. Double-cliquez sur la référence pour naviguer vers le site d'appel.

Vous atterrissez dans une fonction que Ghidra nomme quelque chose comme `FUN_001013a0`. Le décompilateur affiche un pseudo-code C ressemblant à ceci (noms génériques, types approximatifs) :

```c
undefined8 FUN_001013a0(uchar *param_1, int param_2, uchar *param_3, int *param_4)
{
    EVP_CIPHER_CTX *ctx;
    int local_len;
    
    ctx = EVP_CIPHER_CTX_new();
    if (ctx == (EVP_CIPHER_CTX *)0x0) {
        fprintf(stderr, "[!] EVP_CIPHER_CTX_new failed\n");
        return 0xffffffff;  // return -1
    }
    
    iVar1 = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), (ENGINE *)0x0,
                                &DAT_00104020,   // ← clé AES
                                &DAT_00104040);  // ← IV
    if (iVar1 != 1) { ... }
    
    iVar1 = EVP_EncryptUpdate(ctx, param_3, &local_len, param_1, param_2);
    if (iVar1 != 1) { ... }
    *param_4 = local_len;
    
    iVar1 = EVP_EncryptFinal_ex(ctx, param_3 + local_len, &local_len);
    if (iVar1 != 1) { ... }
    *param_4 = *param_4 + local_len;
    
    EVP_CIPHER_CTX_free(ctx);
    return 0;  // succès
}
```

> 💡 Le pseudo-code ci-dessus est une reconstitution simplifiée. Le décompilateur Ghidra produira un résultat légèrement différent selon la version et les options d'analyse, mais la structure — `Init`, `Update`, `Final` — sera toujours reconnaissable.

#### Identification de la clé et de l'IV

Les deux paramètres cruciaux de `EVP_EncryptInit_ex` sont le 4ᵉ argument (la clé) et le 5ᵉ argument (l'IV). Dans le pseudo-code décompilé, ils apparaissent comme des références à des adresses dans la section `.rodata` : `DAT_00104020` et `DAT_00104040` (les adresses exactes varient).

Double-cliquez sur `DAT_00104020` pour naviguer vers cette adresse dans le listing. Vous verrez 32 octets consécutifs :

```
00104020  52 45 56 45 52 53 45 5f  45 4e 47 49 4e 45 45 52   REVERSE_ENGINEER
00104030  49 4e 47 5f 49 53 5f 46  55 4e 5f 32 30 32 35 21   ING_IS_FUN_2025!
```

C'est la **clé AES-256** identifiée lors du triage. L'hypothèse H3 est maintenant **confirmée** : cette adresse mémoire est bien celle passée à `EVP_EncryptInit_ex` comme paramètre de clé.

Naviguez ensuite vers `DAT_00104040` :

```
00104040  de ad be ef ca fe ba be  13 37 42 42 fe ed fa ce   ................
```

Les 16 octets de l'**IV** — hypothèse H4 confirmée.

#### Renommage et annotation

Même si nous travaillons sur un binaire strippé, rien ne nous empêche d'enrichir l'analyse dans Ghidra. Renommons les éléments identifiés pour faciliter la lecture :

| Élément original | Nouveau nom | Raison |  
|---|---|---|  
| `FUN_001013a0` | `aes256cbc_encrypt` | Encapsule le chiffrement EVP |  
| `DAT_00104020` | `AES_KEY` | Clé de 32 octets passée à `EncryptInit` |  
| `DAT_00104040` | `AES_IV` | IV de 16 octets passé à `EncryptInit` |

Pour renommer, cliquez sur le nom puis appuyez sur `L` (ou clic droit → *Rename*). Ajoutez un commentaire avec `;` (pré-commentaire) ou `Ctrl+;` (post-commentaire) pour noter vos observations. Par exemple, sur l'adresse de la clé : « AES-256 key: REVERSE_ENGINEERING_IS_FUN_2025! — confirmé via XREF vers EVP_EncryptInit_ex ».

### Remonter vers la fonction de chiffrement de fichier

Maintenant que `aes256cbc_encrypt` est identifiée, cherchons qui l'appelle. Clic droit sur le nom de la fonction → *References → Show References to*. L'unique appelant est la fonction que nous renommerons `encrypt_file`.

Le décompilateur de cette fonction révèle le flux suivant (pseudo-code nettoyé) :

```c
int encrypt_file(char *input_path)
{
    FILE *fp_in = fopen(input_path, "rb");
    // ... vérifications d'erreur ...
    
    fseek(fp_in, 0, SEEK_END);
    long file_size = ftell(fp_in);
    fseek(fp_in, 0, SEEK_SET);
    
    uchar *plaintext = malloc(file_size);
    fread(plaintext, 1, file_size, fp_in);
    fclose(fp_in);
    
    uchar *ciphertext = malloc(file_size + 16);  // + EVP_MAX_BLOCK_LENGTH
    int ciphertext_len;
    aes256cbc_encrypt(plaintext, file_size, ciphertext, &ciphertext_len);
    free(plaintext);
    
    // Construction du chemin de sortie : input_path + ".locked"
    snprintf(out_path, 4096, "%s.locked", input_path);
    
    FILE *fp_out = fopen(out_path, "wb");
    fwrite("RWARE27\0", 1, 8, fp_out);           // magic header
    fwrite(&file_size, sizeof(uint64_t), 1, fp_out);  // taille originale
    fwrite(ciphertext, 1, ciphertext_len, fp_out);     // données chiffrées
    fclose(fp_out);
    free(ciphertext);
    
    unlink(input_path);  // suppression de l'original
    return 0;
}
```

Plusieurs éléments se dégagent :

1. **Lecture intégrale en mémoire** — Le fichier entier est chargé via `fread` avant d'être chiffré en un seul appel à `aes256cbc_encrypt`. Pas de chiffrement par blocs itératif depuis le fichier.  
2. **Header du fichier `.locked`** — Trois écritures successives avec `fwrite` : magic (`RWARE27\0`, 8 octets), taille originale (`uint64_t`, 8 octets), puis les données chiffrées. Le header fait donc **16 octets** au total.  
3. **Suppression de l'original** — L'appel à `unlink` après la fermeture du fichier de sortie confirme le comportement destructeur.  
4. **Concaténation du chemin** — `snprintf` avec `"%s.locked"` confirme l'extension ajoutée.

Renommons cette fonction `encrypt_file` et ajoutons un commentaire récapitulatif en en-tête.

### Remonter vers le parcours de répertoire

En remontant encore d'un cran par XREF depuis `encrypt_file`, on atteint la fonction de parcours récursif. Le décompilateur montre une boucle typique `opendir` / `readdir` / `closedir` :

```c
void traverse_directory(char *dir_path, int *count)
{
    DIR *d = opendir(dir_path);
    // ...
    while ((entry = readdir(d)) != NULL) {
        // strcmp(entry->d_name, ".") et strcmp(entry->d_name, "..")
        // ... filtrage should_skip() ...
        
        stat(full_path, &st);
        
        if (S_ISDIR(st.st_mode)) {
            traverse_directory(full_path, count);  // appel récursif
        } else if (S_ISREG(st.st_mode)) {
            encrypt_file(full_path);
            *count = *count + 1;
        }
    }
    closedir(d);
}
```

La récursion est identifiable par le fait que la fonction s'appelle elle-même (XREF circulaire). Les appels à `stat` suivis de tests sur les bits de mode (`S_ISDIR`, `S_ISREG`) sont un pattern classique de parcours de système de fichiers en C.

On repère également un appel à une petite fonction de filtrage (que nous renommerons `should_skip`) qui effectue des comparaisons de chaînes avec `.locked` et `README_LOCKED.txt` pour éviter de rechiffrer des fichiers déjà traités ou la ransom note.

### Remonter jusqu'à `main`

Le dernier étage est la fonction `main`, accessible par XREF depuis `traverse_directory`. Dans un binaire PIE strippé, Ghidra identifie souvent `main` automatiquement grâce à la convention d'appel de `__libc_start_main` (dont le premier argument est le pointeur vers `main`). Si ce n'est pas le cas, cherchez `__libc_start_main` dans les imports et suivez son XREF pour identifier la fonction passée en premier argument — c'est `main`.

Le pseudo-code de `main` révèle la séquence globale :

```c
int main(int argc, char **argv)
{
    print_banner();
    
    // stat() sur "/tmp/test" → vérifie que le répertoire existe
    if (stat("/tmp/test", &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[!] Repertoire cible absent : /tmp/test\n");
        return 1;
    }
    
    int count = 0;
    traverse_directory("/tmp/test", &count);
    
    if (count > 0) {
        drop_ransom_note("/tmp/test");
        printf("[*] %d fichier(s) chiffre(s).\n", count);
    }
    
    return 0;
}
```

### Graphe d'appels reconstruit

À ce stade, nous pouvons dresser le graphe d'appels complet du sample. Dans Ghidra, ouvrez *Window → Function Call Graph* pour une visualisation automatique, ou reconstituez-le manuellement :

```
main()
 ├── print_banner()
 │     └── printf()
 ├── stat()                          [vérifie /tmp/test]
 ├── traverse_directory()
 │     ├── opendir()
 │     ├── readdir()                 [boucle]
 │     ├── stat()                    [type fichier/répertoire]
 │     ├── should_skip()
 │     │     ├── strlen()
 │     │     └── strcmp()            [".locked", "README_LOCKED.txt"]
 │     ├── traverse_directory()      [récursion sur sous-répertoires]
 │     ├── encrypt_file()
 │     │     ├── fopen() / fseek() / ftell() / fread() / fclose()
 │     │     ├── malloc() / free()
 │     │     ├── aes256cbc_encrypt()
 │     │     │     ├── EVP_CIPHER_CTX_new()
 │     │     │     ├── EVP_aes_256_cbc()
 │     │     │     ├── EVP_EncryptInit_ex()   [AES_KEY, AES_IV]
 │     │     │     ├── EVP_EncryptUpdate()
 │     │     │     ├── EVP_EncryptFinal_ex()
 │     │     │     └── EVP_CIPHER_CTX_free()
 │     │     ├── snprintf()          ["%s.locked"]
 │     │     ├── fopen() / fwrite() / fclose()
 │     │     └── unlink()            [suppression original]
 │     └── closedir()
 └── drop_ransom_note()
       ├── snprintf()                [chemin de la note]
       ├── fopen()
       ├── fputs()                   [contenu de la note]
       └── fclose()
```

Ce graphe est le résultat central de l'analyse statique dans Ghidra. Il capture l'intégralité du comportement du programme dans une structure lisible. Exportez-le dans vos notes de travail.

### Récapitulatif des renommages dans Ghidra

À la fin de l'analyse Ghidra, votre Symbol Tree devrait contenir les renommages suivants :

| Adresse (exemple) | Nom Ghidra original | Nom attribué | Rôle |  
|---|---|---|---|  
| `0x001011e0` | `FUN_001011e0` | `print_banner` | Affiche la bannière d'exécution |  
| `0x00101240` | `FUN_00101240` | `should_skip` | Filtre `.locked` et la ransom note |  
| `0x001012b0` | `FUN_001012b0` | `traverse_directory` | Parcours récursif de `/tmp/test` |  
| `0x001013a0` | `FUN_001013a0` | `aes256cbc_encrypt` | Chiffrement AES-256-CBC via EVP |  
| `0x00101480` | `FUN_00101480` | `encrypt_file` | Lit, chiffre, écrit `.locked`, supprime |  
| `0x00101620` | `FUN_00101620` | `drop_ransom_note` | Écrit `README_LOCKED.txt` |  
| `0x00101690` | `FUN_00101690` | `main` | Point d'entrée logique |  
| `0x00104020` | `DAT_00104020` | `AES_KEY` | Clé AES-256 (32 octets) |  
| `0x00104040` | `DAT_00104040` | `AES_IV` | IV AES (16 octets) |

> 💡 Les adresses sont indicatives et varieront selon votre compilation. L'important est le **processus** : partir des imports nommés, remonter par XREF, renommer et annoter au fur et à mesure.

---

## Partie B — Analyse dans ImHex

ImHex intervient à deux niveaux dans cette analyse : l'examen du **binaire ELF** lui-même (pour visualiser les constantes crypto en contexte) et l'examen des **fichiers `.locked` produits** (pour cartographier le format de sortie).

### Localiser les constantes crypto dans le binaire ELF

Ouvrez `ransomware_O2_strip` dans ImHex. Nous allons localiser les constantes cryptographiques que Ghidra nous a identifiées, mais cette fois dans leur contexte hexadécimal brut.

#### Recherche de la clé AES

Utilisez *Edit → Find → Hex Pattern* et cherchez la séquence hexadécimale correspondant au début de la clé :

```
52 45 56 45 52 53 45 5F
```

ImHex surligne l'occurrence. Sélectionnez les 32 octets à partir de cette position et créez un **bookmark** (*Edit → Bookmark Selection*) nommé `AES-256 Key`. Dans le panneau Data Inspector à droite, le type `ASCII String` affiche `REVERSE_ENGINEERING_IS_FUN_2025!`.

#### Recherche de l'IV

Cherchez ensuite la séquence :

```
DE AD BE EF CA FE BA BE
```

Les 16 octets à partir de cette position constituent l'IV. Créez un second bookmark nommé `AES IV`. Notez la proximité en mémoire entre la clé et l'IV : ils sont typiquement consécutifs ou très proches dans `.rodata`, car ils sont déclarés comme des variables `static const` adjacentes dans le code source.

#### Visualisation avec un pattern `.hexpat` minimal

Pour rendre cette inspection reproductible, nous pouvons écrire un mini-pattern ImHex qui identifie ces constantes dans la section `.rodata`. Ce n'est pas un pattern du format `.locked` (celui-ci vient ensuite) mais un outil de repérage dans le binaire ELF :

```hexpat
// Pattern ImHex : repérage des constantes crypto dans le binaire ELF
// Usage : placer le curseur au début de la clé repérée par recherche hex

struct CryptoConstants {
    char aes_key[32]   [[comment("AES-256 Key"), color("FF6B6B")]];
    char aes_iv[16]    [[comment("AES IV (CBC)"), color("4ECDC4")]];
};

CryptoConstants constants @ 0x____;  // Remplacez par l'offset trouvé
```

Remplacez `0x____` par l'offset du premier octet de la clé dans votre binaire. Ce pattern colorisera la clé en rouge et l'IV en turquoise, rendant leur localisation immédiatement visuelle.

> ⚠️ Cet offset est un **offset fichier** (file offset), pas une adresse virtuelle. ImHex travaille sur le fichier brut, pas sur l'image mémoire. Si Ghidra vous donne une adresse virtuelle, utilisez *Navigation → Go To* dans Ghidra en mode « File Offset » pour obtenir la correspondance, ou calculez-la via les headers de segment ELF (`readelf -l`).

### Cartographier le format des fichiers `.locked`

C'est ici qu'ImHex révèle toute sa puissance. Exécutez le sample dans votre VM sandboxée (après snapshot), puis ouvrez l'un des fichiers `.locked` produits dans ImHex.

#### Observation brute

Les premiers octets du fichier `.locked` se présentent ainsi :

```
Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F
00000000  52 57 41 52 45 32 37 00  xx xx xx xx xx xx xx xx   RWARE27.........
00000010  [données chiffrées ..................................]
```

On reconnaît immédiatement le magic `RWARE27\0` sur les 8 premiers octets. Les 8 octets suivants (offsets `0x08` à `0x0F`) sont la taille originale du fichier en `uint64_t` little-endian. À partir de l'offset `0x10`, c'est le flux chiffré AES-256-CBC.

#### Pattern `.hexpat` pour le format `.locked`

Écrivons un pattern complet qui structure cette vue :

```hexpat
/*!
 * Pattern ImHex — Format de fichier .locked (Chapitre 27)
 * Formation Reverse Engineering
 *
 * Structure :
 *   [0x00 - 0x07]  Magic header "RWARE27\0"
 *   [0x08 - 0x0F]  Taille originale du fichier (uint64_t LE)
 *   [0x10 - EOF ]  Données chiffrées (AES-256-CBC, padding PKCS#7)
 */

#pragma endian little

// --- Magic header ---
struct MagicHeader {
    char signature[7]  [[comment("Identifiant du format")]];
    u8   null_term     [[comment("Null terminator")]];
} [[color("FF6B6B"), name("Magic Header")]];

// --- Metadata ---
struct FileMetadata {
    u64 original_size  [[comment("Taille du fichier avant chiffrement")]];
} [[color("4ECDC4"), name("File Metadata")]];

// --- Données chiffrées ---
struct EncryptedPayload {
    // Taille = taille du fichier .locked - 16 octets de header
    u8 data[std::mem::size() - 16]  [[comment("AES-256-CBC ciphertext + PKCS#7 padding")]];
} [[color("FFE66D"), name("Encrypted Data")]];

// --- Structure principale ---
struct LockedFile {
    MagicHeader   header;
    FileMetadata  metadata;
    EncryptedPayload payload;
};

LockedFile file @ 0x00;
```

Chargez ce pattern dans ImHex via *File → Load Pattern* (ou collez-le dans le Pattern Editor). Le fichier `.locked` s'affiche maintenant avec trois zones colorées distinctes :

- **Rouge** — Le header magique `RWARE27\0`  
- **Turquoise** — La taille originale (lisible directement dans le Data Inspector comme un entier 64 bits)  
- **Jaune** — Le payload chiffré

Cette visualisation confirme la structure déduite depuis Ghidra et fournit un outil réutilisable pour inspecter n'importe quel fichier produit par le sample.

#### Vérifications croisées

Quelques vérifications manuelles renforcent la confiance dans l'analyse :

**Taille du payload vs. taille originale** — Le payload chiffré doit être légèrement plus grand que la taille originale, à cause du padding PKCS#7. Plus précisément, sa taille doit être le premier multiple de 16 supérieur ou égal à la taille originale. Si le fichier original faisait 45 octets, le payload chiffré fera 48 octets (3 octets de padding ajoutés pour atteindre 3 × 16). Si le fichier original faisait exactement 32 octets, le payload fera 48 octets (16 octets de padding ajoutés — un bloc complet, car PKCS#7 ajoute toujours du padding).

**Comparaison entre deux fichiers `.locked`** — Utilisez la fonction *Diff* d'ImHex (*File → Open Diff*) pour comparer deux fichiers chiffrés. Les headers magiques seront identiques, les tailles originales différeront, et les payloads seront complètement différents (l'AES en mode CBC avec le même IV produit des sorties différentes dès que les entrées diffèrent, même d'un seul bit).

**Entropie** — Le panneau *View → Information → Entropy* d'ImHex affiche une courbe d'entropie par blocs. Le header (16 octets) aura une entropie modérée (texte ASCII + petit entier), tandis que le payload chiffré présentera une **entropie proche de 1.0** (maximale), caractéristique de données chiffrées ou compressées. Ce profil d'entropie — faible au début, maximale ensuite — est une signature visuelle typique d'un format chiffré avec header en clair.

---

## Synthèse : mise à jour du tableau d'hypothèses

L'analyse statique approfondie permet de promouvoir plusieurs hypothèses en **faits confirmés** et d'en ajouter de nouvelles :

| # | Hypothèse (section 27.2) | Statut après analyse statique | Preuve |  
|---|---|---|---|  
| H1 | Ransomware ciblant `/tmp/test/` | **Confirmée** | Chaîne passée à `stat()` dans `main()`, parcours récursif via `opendir`/`readdir` |  
| H2 | Algorithme AES-256-CBC | **Confirmée** | Appel à `EVP_aes_256_cbc()` dans `aes256cbc_encrypt` |  
| H3 | Clé = `REVERSE_ENGINEERING_IS_FUN_2025!` | **Confirmée** | Adresse passée en 4ᵉ argument de `EVP_EncryptInit_ex`, 32 octets en `.rodata` |  
| H4 | IV = `DEADBEEF CAFEBABE 1337 4242 FEEDFACE` | **Confirmée** | Adresse passée en 5ᵉ argument de `EVP_EncryptInit_ex`, 16 octets en `.rodata` |  
| H5 | Header `RWARE27` dans fichiers `.locked` | **Confirmée** | `fwrite` de 8 octets dans `encrypt_file`, vérifié dans ImHex |  
| H6 | Parcours récursif + suppression des originaux | **Confirmée** | Appel récursif identifié + `unlink()` dans `encrypt_file` |  
| H7 | Pas de communication réseau | **Renforcée** | Aucun appel à `socket`, `connect`, `send`, `recv` dans le graphe d'appels |  
| H8 | Pas d'anti-debug | **Renforcée** | Aucun appel à `ptrace`, pas de lecture de `/proc/self/status` |

Nouvelles observations :

| # | Observation | Source |  
|---|---|---|  
| N1 | Le fichier est lu intégralement en mémoire avant chiffrement | `fseek`/`ftell`/`fread` dans `encrypt_file` |  
| N2 | La taille originale est stockée dans le header `.locked` (offset `0x08`, `uint64_t` LE) | `fwrite` dans `encrypt_file` + pattern ImHex |  
| N3 | Le header `.locked` fait exactement 16 octets (8 magic + 8 taille) | Analyse ImHex |  
| N4 | Les fichiers `.locked` et `README_LOCKED.txt` sont exclus du chiffrement | Fonction `should_skip` avec `strcmp` |  
| N5 | Clé et IV sont statiques (pas de `RAND_bytes`, pas de dérivation) | Aucun appel à des fonctions de génération aléatoire dans le graphe |

---

## Ce que l'analyse statique ne confirme pas définitivement

Malgré la richesse des résultats, l'analyse statique a ses limites inhérentes :

- **Le code est-il réellement exécuté tel quel ?** — Le décompilateur montre le code *tel qu'il pourrait* s'exécuter, mais des conditions de branchement pourraient orienter l'exécution vers des chemins alternatifs non observés. Seule l'exécution contrôlée le confirmera.  
- **La clé identifiée dans `.rodata` est-elle celle effectivement utilisée au runtime ?** — Bien que le XREF soit direct, un binaire plus sophistiqué pourrait copier la clé, la transformer, ou charger une clé différente selon des conditions runtime. L'analyse dynamique (section 27.5) lèvera cette dernière incertitude en capturant les arguments réels au moment de l'appel.  
- **Le comportement est-il complet ?** — Le graphe d'appels montre le flux principal, mais du code mort ou des branches rarement atteintes pourraient exister (fonctions de debug résiduelles, chemins d'erreur complexes). Un passage en analyse dynamique avec couverture de code (`Frida Stalker` ou `gcov`) pourrait compléter le tableau.

Ces questions seront adressées dans la section 27.5, où nous poserons des breakpoints sur les fonctions critiques et observerons le comportement réel du programme en exécution.

⏭️ [Identifier les règles YARA correspondantes depuis ImHex](/27-ransomware/04-regles-yara.md)
