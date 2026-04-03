🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 27.4 — Identifier les règles YARA correspondantes depuis ImHex

> 🎯 **Objectif de cette section** : transformer les observations accumulées lors du triage (27.2) et de l'analyse statique (27.3) en **règles YARA** — des signatures formelles capables de détecter automatiquement ce sample (ou des variantes similaires) dans un corpus de fichiers. Nous utiliserons ImHex comme support visuel pour identifier les patterns binaires les plus discriminants.  
>  
> Cette section fait le lien entre l'analyse de malware et la détection opérationnelle. Les règles YARA produites ici sont du type de celles qu'un analyste partagerait dans un rapport d'incident pour permettre aux défenseurs de scanner leurs systèmes.

---

## YARA en 30 secondes

YARA est un outil de pattern matching sur fichiers binaires. Une règle YARA se compose de trois blocs :

- **`meta`** — Métadonnées descriptives (auteur, date, description). Aucun impact sur la détection, mais essentielles pour la traçabilité.  
- **`strings`** — Les patterns à rechercher : chaînes ASCII/Unicode, séquences hexadécimales, expressions régulières.  
- **`condition`** — La logique booléenne qui combine les patterns pour déclencher (ou non) la détection.

Une règle bien écrite est **suffisamment spécifique** pour éviter les faux positifs (détecter un binaire légitime utilisant OpenSSL) tout en étant **suffisamment générale** pour résister aux variations mineures (recompilation avec des flags différents, légère modification du code source).

---

## Stratégie : quels patterns sont discriminants ?

Tous les éléments identifiés dans les sections précédentes ne se valent pas comme signatures de détection. Le tableau suivant classe nos observations par **pouvoir discriminant** :

| Élément | Spécificité | Stabilité face aux variations | Pertinence YARA |  
|---|---|---|---|  
| Clé AES `REVERSE_ENGINEERING...` | Très haute — unique à ce sample | Faible — premier élément qu'un auteur modifierait | Bonne pour cette variante exacte |  
| IV `DEADBEEF CAFEBABE...` | Haute — combinaison spécifique | Faible — facilement modifiable | Bonne pour cette variante exacte |  
| Magic header `RWARE27` | Haute — identifiant du format | Moyenne — l'auteur pourrait le changer, mais c'est structurel | Excellente pour les fichiers produits |  
| Chaînes de la ransom note | Moyenne — texte spécifique | Faible — texte facilement réécrit | Complémentaire |  
| Pattern d'appels EVP (Init+Update+Final) | Faible seule — tout programme OpenSSL l'utilise | Haute — imposé par l'API | Utile seulement combinée à d'autres critères |  
| Combinaison chemin `/tmp/test` + `.locked` + `unlink` | Haute en combinaison | Moyenne | Bonne signature comportementale |

La stratégie optimale consiste à écrire **plusieurs règles** de granularité différente : une règle précise qui cible exactement cette variante, une règle plus générique qui survivrait à des modifications mineures, et une règle ciblant les fichiers produits (les `.locked`).

---

## Identifier les patterns dans ImHex

Avant d'écrire les règles, retournons dans ImHex pour extraire les séquences hexadécimales exactes qui serviront de signatures. L'avantage d'ImHex sur un simple `hexdump` est la possibilité de **sélectionner visuellement** les octets, de les copier en notation YARA, et de vérifier leur unicité dans le fichier.

### La clé AES dans le binaire ELF

Naviguez vers le bookmark `AES-256 Key` créé en section 27.3. Sélectionnez les 32 octets et copiez-les en hexadécimal (*Edit → Copy As → Hex String*) :

```
52 45 56 45 52 53 45 5F 45 4E 47 49 4E 45 45 52
49 4E 47 5F 49 53 5F 46 55 4E 5F 32 30 32 35 21
```

Cette séquence est notre signature la plus spécifique pour le binaire. Elle identifie non seulement le sample, mais précisément la clé utilisée — information critique dans un contexte de réponse à incident, car elle signifie que les fichiers chiffrés par cette variante sont déchiffrables.

### L'IV dans le binaire ELF

Naviguez vers le bookmark `AES IV`. Les 16 octets :

```
DE AD BE EF CA FE BA BE 13 37 42 42 FE ED FA CE
```

Individuellement, des sous-séquences comme `DEADBEEF` ou `CAFEBABE` sont trop courantes pour servir de signature (elles apparaissent dans de nombreux programmes et formats). Mais les 16 octets pris ensemble forment une combinaison suffisamment unique.

### Le magic header dans un fichier `.locked`

Ouvrez un fichier `.locked` dans ImHex et sélectionnez les 8 premiers octets :

```
52 57 41 52 45 32 37 00
```

C'est le magic `RWARE27\0`. Cette signature est idéale pour scanner un système de fichiers à la recherche de fichiers déjà chiffrés par le sample — un besoin fréquent lors de la phase d'évaluation d'impact d'un incident.

### Proximité clé/IV dans `.rodata`

Un point subtil mais important : dans notre binaire, la clé et l'IV sont **consécutifs en mémoire** (ou séparés par un padding d'alignement de quelques octets au maximum). Cette proximité est caractéristique de deux variables `static const` déclarées l'une après l'autre dans le code source. En YARA, nous pouvons exploiter cette adjacence avec l'opérateur de distance `(offset_clé, offset_iv)` ou plus simplement en cherchant la clé suivie de l'IV dans une fenêtre étroite.

Dans ImHex, mesurez la distance entre le dernier octet de la clé et le premier octet de l'IV. Si la distance est de 0 (consécutifs), nous pouvons les combiner en une seule chaîne hex de 48 octets. Si un gap d'alignement existe (typiquement 0 à 16 octets de padding), nous utiliserons un wildcard YARA.

---

## Règle 1 — Détection exacte du sample (`ransomware_ch27_exact`)

Cette première règle cible précisément notre sample. Elle combine la clé AES, l'IV, et des chaînes comportementales spécifiques. Un seul faux positif est quasiment impossible.

```yara
rule ransomware_ch27_exact
{
    meta:
        description = "Détecte le sample ransomware pédagogique du Chapitre 27"
        author      = "Formation RE"
        date        = "2025-01-01"
        hash        = "<insérer SHA-256 de ransomware_O2_strip>"
        reference   = "Formation Reverse Engineering — Chapitre 27"
        tlp         = "WHITE"

    strings:
        // Clé AES-256 hardcodée (32 octets)
        $aes_key = {
            52 45 56 45 52 53 45 5F
            45 4E 47 49 4E 45 45 52
            49 4E 47 5F 49 53 5F 46
            55 4E 5F 32 30 32 35 21
        }

        // IV AES (16 octets)
        $aes_iv = {
            DE AD BE EF CA FE BA BE
            13 37 42 42 FE ED FA CE
        }

        // Chaînes comportementales
        $target_dir   = "/tmp/test"            ascii
        $locked_ext   = ".locked"              ascii
        $ransom_note  = "README_LOCKED.txt"    ascii
        $magic_header = "RWARE27"              ascii

        // Fragment de la ransom note
        $note_text = "VOS FICHIERS ONT ETE CHIFFRES" ascii

    condition:
        uint32(0) == 0x464C457F     // magic ELF ("\x7FELF")
        and $aes_key
        and $aes_iv
        and 3 of ($target_dir, $locked_ext, $ransom_note, $magic_header, $note_text)
}
```

Décortiquons les choix de conception :

**`uint32(0) == 0x464C457F`** — Cette condition vérifie que les 4 premiers octets du fichier correspondent au magic number ELF (`\x7FELF` en little-endian). C'est un filtre de type de fichier qui évite de matcher sur un fichier texte contenant par hasard la chaîne `REVERSE_ENGINEERING_IS_FUN_2025!`.

**`$aes_key` et `$aes_iv`** — Les deux constantes crypto sont exigées simultanément. Trouver l'une sans l'autre dans un binaire ELF serait une coïncidence extraordinaire, mais les exiger ensemble élimine tout doute.

**`3 of (...)`** — Parmi les 5 chaînes comportementales, au moins 3 doivent être présentes. Cette souplesse absorbe les petites variations : si un attaquant modifie le texte de la ransom note mais conserve le répertoire cible et l'extension, la règle matche toujours.

### Test de la règle

```bash
# Scanner le sample
$ yara ransomware_ch27.yar ransomware_O2_strip
ransomware_ch27_exact ransomware_O2_strip

# Scanner un binaire légitime pour vérifier l'absence de faux positif
$ yara ransomware_ch27.yar /usr/bin/openssl
# (aucune sortie → pas de faux positif)
```

---

## Règle 2 — Détection générique par comportement (`ransomware_ch27_generic`)

La règle exacte est fragile : si l'attaquant change la clé, l'IV, ou le texte de la note, elle ne matche plus. Une deuxième règle, plus générique, capture le **pattern comportemental** — les caractéristiques structurelles qui survivraient à une recompilation avec des constantes différentes.

```yara
rule ransomware_ch27_generic
{
    meta:
        description = "Détecte des variantes du ransomware Ch27 par pattern comportemental"
        author      = "Formation RE"
        date        = "2025-01-01"
        reference   = "Formation Reverse Engineering — Chapitre 27"

    strings:
        // API OpenSSL EVP — chiffrement (pas déchiffrement)
        $evp_init   = "EVP_EncryptInit_ex"   ascii
        $evp_update = "EVP_EncryptUpdate"    ascii
        $evp_final  = "EVP_EncryptFinal_ex"  ascii
        $evp_aes    = "EVP_aes_256_cbc"      ascii

        // Fonctions de parcours de système de fichiers
        $fs_opendir = "opendir"   ascii
        $fs_readdir = "readdir"   ascii
        $fs_unlink  = "unlink"    ascii
        $fs_stat    = "stat"      ascii

        // Extension de fichier chiffré
        $locked_ext = ".locked" ascii

        // Magic header du format de sortie
        $magic = "RWARE27" ascii

    condition:
        uint32(0) == 0x464C457F                      // fichier ELF
        and filesize < 500KB                          // sample compact
        and 3 of ($evp_init, $evp_update, $evp_final, $evp_aes)  // API crypto
        and 3 of ($fs_opendir, $fs_readdir, $fs_unlink, $fs_stat) // parcours FS
        and ($locked_ext or $magic)                   // marqueur ransomware
}
```

Cette règle ne contient **aucune constante spécifique au sample** (ni clé, ni IV, ni texte de ransom note). Elle repose sur la convergence de trois familles d'indicateurs :

1. **Usage de l'API EVP d'OpenSSL en mode chiffrement** — La présence de `EVP_Encrypt*` dans la table `.dynsym` est vérifiable même sur un binaire strippé.  
2. **Parcours récursif du système de fichiers avec suppression** — La combinaison `opendir` + `readdir` + `stat` + `unlink` dans un même binaire décrit un programme qui énumère des fichiers et en supprime.  
3. **Marqueur de sortie** — L'extension `.locked` ou le magic `RWARE27` identifie la finalité ransomware.

La condition `filesize < 500KB` est un garde-fou pragmatique : notre sample est petit (quelques dizaines de Ko). Cela exclut les gros binaires légitimes qui pourraient accidentellement combiner OpenSSL et des fonctions de manipulation de fichiers (un backup tool, par exemple). Cette limite devra être ajustée si le sample grossit (ajout de bibliothèques statiques, par exemple).

> ⚠️ **Risque de faux positifs** : cette règle générique est plus agressive. Un outil légitime de chiffrement de fichiers utilisant OpenSSL, parcourant des répertoires et supprimant les originaux après chiffrement (un backup chiffré, par exemple) pourrait théoriquement matcher. Le marqueur `.locked` ou `RWARE27` est ce qui réduit ce risque. En environnement de production, cette règle serait classée comme *hunting rule* (règle de chasse) plutôt que *detection rule* (règle de détection ferme) — elle signale un fichier qui **mérite investigation**, pas un fichier nécessairement malveillant.

---

## Règle 3 — Détection des fichiers chiffrés (`ransomware_ch27_locked_file`)

Cette troisième règle ne cible pas le binaire malveillant, mais les **fichiers produits** par celui-ci. Son utilité est différente : elle permet de scanner un système de fichiers compromis pour identifier tous les fichiers affectés et évaluer l'étendue des dégâts.

```yara
rule ransomware_ch27_locked_file
{
    meta:
        description = "Identifie les fichiers chiffrés par le ransomware Ch27 (format .locked)"
        author      = "Formation RE"
        date        = "2025-01-01"
        filetype    = "locked"
        reference   = "Formation Reverse Engineering — Chapitre 27"

    strings:
        $magic = { 52 57 41 52 45 32 37 00 }  // "RWARE27\0"

    condition:
        $magic at 0                             // magic en tout début de fichier
        and filesize > 16                       // au minimum header (16) + 1 bloc chiffré
        and filesize < 100MB                    // borne supérieure raisonnable
}
```

La condition `$magic at 0` impose que la signature `RWARE27\0` soit exactement au début du fichier (offset 0), pas simplement quelque part à l'intérieur. C'est un critère fort : il élimine les faux positifs où la chaîne `RWARE27` apparaîtrait par hasard au milieu d'un fichier texte ou d'un binaire.

La condition `filesize > 16` exclut les fichiers trop petits pour contenir un header complet (16 octets) plus au moins un bloc de données chiffrées. La borne supérieure `filesize < 100MB` est un garde-fou de performance : scanner des fichiers de plusieurs gigaoctets pour un magic de 8 octets est coûteux et le sample lit les fichiers intégralement en mémoire, ce qui limite de facto la taille des fichiers qu'il peut chiffrer.

### Usage en réponse à incident

```bash
# Scanner récursivement un système de fichiers pour trouver les fichiers chiffrés
$ yara -r ransomware_ch27.yar /tmp/test/
ransomware_ch27_locked_file /tmp/test/document.txt.locked  
ransomware_ch27_locked_file /tmp/test/notes.md.locked  
ransomware_ch27_locked_file /tmp/test/budget.csv.locked  
ransomware_ch27_locked_file /tmp/test/sous-dossier/nested.txt.locked  
...

# Compter le nombre de fichiers affectés
$ yara -r ransomware_ch27.yar /tmp/test/ | wc -l
```

Ce type de scan est une étape standard dans la phase d'évaluation d'impact d'un incident ransomware. Le résultat alimente directement le rapport (section 27.7).

---

## Assembler les règles dans un fichier unique

En pratique, les trois règles sont regroupées dans un seul fichier `.yar` :

```yara
/*
 * Formation Reverse Engineering — Chapitre 27
 * Règles YARA pour le sample ransomware pédagogique
 *
 * Fichier : yara-rules/ransomware_ch27.yar
 *
 * Règle 1 : ransomware_ch27_exact        → détection exacte du sample
 * Règle 2 : ransomware_ch27_generic      → détection de variantes par comportement
 * Règle 3 : ransomware_ch27_locked_file  → détection des fichiers chiffrés produits
 */

import "elf"

rule ransomware_ch27_exact { ... }  
rule ransomware_ch27_generic { ... }  
rule ransomware_ch27_locked_file { ... }  
```

> 💡 L'import `elf` est optionnel mais permet d'accéder à des métadonnées ELF dans les conditions (par exemple `elf.type == elf.ET_DYN` pour cibler uniquement les PIE executables). Nous ne l'utilisons pas dans nos règles pour rester compatibles avec un YARA minimal, mais il est courant dans les règles de production.

---

## Le pont entre ImHex et YARA

Un aspect sous-estimé d'ImHex est sa capacité à **exécuter des règles YARA directement** sur le fichier ouvert (*View → YARA*). Cela crée un workflow itératif particulièrement efficace :

1. **Observer dans ImHex** — Repérer visuellement un pattern intéressant (une séquence d'octets, une constante, un magic).  
2. **Copier en hexadécimal** — Sélectionner les octets et les copier en format hex.  
3. **Écrire la règle YARA** — Intégrer la séquence dans une règle avec les conditions appropriées.  
4. **Tester dans ImHex** — Charger la règle dans le panneau YARA d'ImHex et vérifier qu'elle matche sur le fichier ouvert.  
5. **Affiner** — Si la règle est trop large (faux positifs sur d'autres fichiers) ou trop étroite (ne matche pas sur les variantes), ajuster les patterns et conditions.

Ce cycle observation → formalisation → test → affinement est au cœur de la création de signatures de qualité. ImHex sert de banc d'essai visuel avant de déployer les règles sur un scanner YARA en production.

---

## Considérations sur la robustesse des règles

### Ce qui casse une règle YARA

Les modifications suivantes du sample invalideraient certaines de nos règles :

| Modification | Règle 1 (exacte) | Règle 2 (générique) | Règle 3 (fichiers) |  
|---|---|---|---|  
| Changement de clé AES | ❌ Cassée | ✅ Résiste | ✅ Résiste |  
| Changement d'IV | ❌ Cassée | ✅ Résiste | ✅ Résiste |  
| Réécriture de la ransom note | ⚠️ Affaiblie (3 of 5) | ✅ Résiste | ✅ Résiste |  
| Changement d'extension (`.encrypted` au lieu de `.locked`) | ⚠️ Affaiblie | ⚠️ Affaiblie | ✅ Résiste |  
| Changement du magic header (`RWARE28`) | ⚠️ Affaiblie | ⚠️ Affaiblie | ❌ Cassée |  
| Passage à AES-128-CTR au lieu d'AES-256-CBC | ⚠️ Affaiblie | ⚠️ Affaiblie | ✅ Résiste |  
| Implémentation AES custom (sans OpenSSL) | ✅ Résiste (clé en `.rodata`) | ❌ Cassée | ✅ Résiste |  
| Linkage statique d'OpenSSL | ✅ Résiste | ❌ Cassée (plus dans `.dynsym`) | ✅ Résiste |  
| Packing UPX du binaire | ❌ Cassée | ❌ Cassée | ✅ Résiste |

Ce tableau illustre pourquoi on écrit **plusieurs règles de granularité différente**. La règle exacte est la plus précise mais la plus fragile. La règle générique survit à plus de variations mais risque des faux positifs. La règle sur les fichiers produits est la plus résiliente car elle cible la sortie, pas le code — mais elle ne détecte le sample qu'après qu'il a déjà causé des dégâts.

### Complémentarité avec d'autres signatures

Les règles YARA ne sont qu'une pièce du dispositif de détection. En environnement opérationnel, elles seraient complétées par :

- **Hashes** (SHA-256, SHA-1, MD5) — Identifient exactement un fichier, zéro tolérance aux variations. Le hash de `ransomware_O2_strip` (calculé au triage, section 27.2) est l'IOC le plus simple et le plus fiable pour cette variante précise.  
- **Signatures réseau** (Snort/Suricata) — Non applicables ici (pas de communication réseau), mais essentielles pour le dropper du [Chapitre 28](/28-dropper/README.md).  
- **Signatures comportementales** (Sysmon, auditd) — Détectent le comportement au runtime : création massive de fichiers `.locked`, suppression de fichiers dans `/tmp/test`, invocation de fonctions OpenSSL. Complémentaires aux règles YARA statiques.

---

## Résumé des livrables

À l'issue de cette section, vous avez produit trois artefacts concrets :

1. **`ransomware_ch27_exact`** — Règle de détection exacte, exploitable immédiatement sur un scanner de fichiers pour identifier ce sample précis et confirmer que la clé est récupérable.  
2. **`ransomware_ch27_generic`** — Règle de chasse (hunting), utilisable pour détecter des variantes recompilées du même sample avec des constantes modifiées.  
3. **`ransomware_ch27_locked_file`** — Règle d'évaluation d'impact, permettant de scanner un système de fichiers pour inventorier les fichiers chiffrés.

Ces règles alimenteront la section IOC du rapport d'analyse (section 27.7). Elles sont également archivées dans le dépôt sous `yara-rules/ransomware_ch27.yar` pour référence.

⏭️ [Analyse dynamique : GDB + Frida (extraire la clé en mémoire)](/27-ransomware/05-analyse-dynamique-gdb-frida.md)
