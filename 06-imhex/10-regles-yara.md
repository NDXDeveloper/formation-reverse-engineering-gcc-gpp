🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.10 — Appliquer des règles YARA depuis ImHex (pont vers l'analyse malware)

> 🎯 **Objectif de cette section** : Comprendre ce que sont les règles YARA, pourquoi elles sont un outil fondamental de l'analyse de binaires, et comment les appliquer directement depuis ImHex pour détecter des patterns connus — constantes cryptographiques, signatures de packers, indicateurs de comportement suspect — sans quitter l'éditeur hexadécimal.

> 📁 **Fichiers utilisés** : `yara-rules/crypto_constants.yar`, `yara-rules/packer_signatures.yar`  
> 📦 **Binaire de test** : `binaries/ch24-crypto/crypto_O0` ou `binaries/ch29-packed/packed_sample`

---

## YARA en quelques mots

YARA est un outil créé par Victor Alvarez (VirusTotal) qui permet de décrire des **patterns binaires et textuels** sous forme de règles, puis de scanner des fichiers pour détecter la présence de ces patterns. Le nom est parfois interprété comme « Yet Another Recursive Acronym », mais YARA est surtout connu pour son usage pratique : c'est le langage de signatures de facto dans l'industrie de l'analyse malware.

Une règle YARA est conceptuellement simple : elle décrit « ce à quoi ressemble » un type de fichier, un malware, un packer ou un algorithme, en combinant des séquences d'octets, des chaînes de caractères et des conditions logiques. Si le fichier scanné correspond à la description, la règle « matche ».

Voici un exemple minimal :

```yara
rule detect_elf {
    meta:
        description = "Détecte un fichier ELF"
        author      = "Formation RE"

    strings:
        $magic = { 7F 45 4C 46 }

    condition:
        $magic at 0
}
```

Cette règle déclare un pattern hexadécimal `$magic` (le magic number ELF) et une condition : le pattern doit se trouver à l'offset 0 du fichier. Tout fichier ELF déclenche cette règle.

---

## Pourquoi YARA dans un éditeur hexadécimal ?

YARA s'utilise habituellement en ligne de commande (`yara rules.yar fichier`) ou dans des plateformes d'analyse automatisée (VirusTotal, CAPE Sandbox, Cuckoo). L'intégration dans ImHex apporte un avantage spécifique : la **localisation visuelle** des matchs.

Quand vous lancez `yara` en CLI, il vous dit « la règle X matche le fichier Y » — une réponse binaire oui/non, éventuellement accompagnée de l'offset du match. Quand vous lancez YARA depuis ImHex, les octets qui ont déclenché le match sont **surlignés directement dans la vue hexadécimale**. Vous voyez exactement où se trouve la séquence détectée, dans quel contexte elle apparaît (quelle section, quels octets l'entourent), et vous pouvez immédiatement inspecter la zone avec le Data Inspector, le désassembleur intégré ou un pattern `.hexpat`.

Cette combinaison « détection + localisation + inspection » en un seul outil est ce qui fait la valeur de l'intégration YARA dans ImHex. Elle transforme un scan qui serait autrement abstrait en un acte d'exploration visuelle.

---

## Anatomie d'une règle YARA

Avant d'utiliser YARA dans ImHex, prenons le temps de comprendre la structure d'une règle. Nous n'avons pas besoin de devenir des experts YARA à ce stade — le chapitre 35 y reviendra en profondeur pour la rédaction de règles avancées — mais il faut savoir lire et adapter une règle existante.

### La section `meta`

La section `meta` contient des métadonnées descriptives qui n'affectent pas le matching. C'est de la documentation intégrée à la règle :

```yara
meta:
    description = "Détecte la S-box AES dans un binaire"
    author      = "Formation RE"
    date        = "2025-03-15"
    reference   = "FIPS 197, Table S-box"
```

Les champs `description`, `author` et `date` sont conventionnels. Le champ `reference` pointe vers la source de la signature — indispensable pour que quiconque lisant la règle comprenne pourquoi ces octets sont significatifs.

### La section `strings`

La section `strings` déclare les patterns à rechercher. YARA supporte trois types de patterns.

**Chaînes hexadécimales** — des séquences d'octets bruts, exactement comme dans la recherche hex d'ImHex :

```yara
strings:
    $aes_sbox = { 63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76 }
```

Les wildcards sont supportés avec `??` pour un octet quelconque, et les alternatives avec `(XX | YY)` :

```yara
strings:
    $call_pattern = { E8 ?? ?? ?? ?? }           // call rel32
    $jmp_or_call  = { (E8 | E9) ?? ?? ?? ?? }    // call ou jmp rel32
```

**Chaînes textuelles** — des chaînes ASCII avec des modificateurs optionnels :

```yara
strings:
    $str_passwd  = "password" nocase    // insensible à la casse
    $str_wide    = "config" wide        // UTF-16 little-endian
    $str_both    = "secret" ascii wide  // cherche les deux encodages
```

Le modificateur `nocase` est particulièrement utile pour les chaînes qui peuvent apparaître en casse variable. Le modificateur `wide` gère les chaînes UTF-16 que nous avons évoquées en section 6.8.

**Expressions régulières** — délimitées par des slashs :

```yara
strings:
    $base64_block = /[A-Za-z0-9+\/]{20,}={0,2}/
```

### La section `condition`

La section `condition` est une expression booléenne qui détermine quand la règle matche. Elle peut combiner les patterns déclarés avec des opérateurs logiques et des fonctions :

```yara
condition:
    $aes_sbox                         // le pattern existe quelque part dans le fichier
```

```yara
condition:
    $aes_sbox and $str_passwd         // les deux patterns sont présents
```

```yara
condition:
    any of ($str_*)                   // au moins un des patterns $str_... est présent
```

```yara
condition:
    #call_pattern > 50                // plus de 50 occurrences de call rel32
```

```yara
condition:
    $magic at 0 and filesize < 1MB    // magic à l'offset 0, fichier < 1 Mo
```

La richesse du langage de conditions permet d'écrire des règles très précises qui minimisent les faux positifs. Une règle qui cherche la S-box AES **et** une chaîne `"AES"` ou `"encrypt"` est plus fiable qu'une règle qui cherche uniquement la S-box (qui pourrait apparaître dans un compresseur ou dans des données aléatoires).

---

## Utiliser YARA dans ImHex

### Accéder à la vue YARA

Ouvrez la vue YARA via **View → YARA**. Le panneau se divise en deux zones : une zone de sélection de fichier de règles (`.yar` ou `.yara`) et une zone d'affichage des résultats.

### Charger un fichier de règles

Cliquez sur le bouton de chargement et naviguez jusqu'à votre fichier de règles. Vous pouvez charger les fichiers fournis avec la formation :

- `yara-rules/crypto_constants.yar` — détecte les constantes cryptographiques courantes (AES, SHA-256, MD5, ChaCha20, etc.)  
- `yara-rules/packer_signatures.yar` — détecte les signatures de packers connus (UPX, ASPack, etc.)

Vous pouvez aussi charger des règles YARA publiques provenant de dépôts communautaires reconnus.

### Lancer le scan

Une fois les règles chargées, cliquez sur le bouton de scan (souvent **Match** ou une icône ▶). ImHex scanne le fichier ouvert contre toutes les règles du fichier chargé et affiche les résultats dans le panneau : le nom de chaque règle qui matche, accompagné des offsets des patterns détectés.

### Localiser les matchs

Cliquez sur un résultat dans la liste. ImHex **saute à l'offset** correspondant dans la vue hexadécimale et surligne les octets qui ont déclenché le match. Vous pouvez alors :

- Inspecter la zone avec le **Data Inspector** pour interpréter les valeurs.  
- Vérifier dans le **désassembleur intégré** si les octets se trouvent dans du code ou dans des données.  
- Créer un **bookmark** (section 6.6) pour documenter la découverte et la conserver dans votre projet.  
- Charger un **pattern `.hexpat`** pour parser la structure autour du match.

Ce workflow « scan → localiser → inspecter → documenter » est la chaîne de valeur complète de l'intégration YARA dans ImHex.

---

## Règles YARA utiles pour le RE de binaires GCC

Voici une sélection de règles adaptées au contexte de cette formation. Elles ciblent des patterns que l'on rencontre fréquemment dans les binaires ELF compilés avec GCC.

### Détection de constantes cryptographiques

```yara
rule crypto_aes_sbox {
    meta:
        description = "Détecte la S-box AES (première ligne, 16 octets)"
        reference   = "FIPS 197"
    strings:
        $sbox = { 63 7C 77 7B F2 6B 6F C5 30 01 67 2B FE D7 AB 76 }
    condition:
        $sbox
}

rule crypto_sha256_init {
    meta:
        description = "Détecte les valeurs initiales SHA-256 (H0-H3, little-endian)"
        reference   = "FIPS 180-4"
    strings:
        $h_init = { 67 E6 09 6A 85 AE 67 BB 72 F3 6E 3C 3A F5 4F A5 }
    condition:
        $h_init
}

rule crypto_chacha20_constant {
    meta:
        description = "Détecte la constante ChaCha20 'expand 32-byte k'"
    strings:
        $const = "expand 32-byte k"
    condition:
        $const
}
```

Ces règles correspondent aux constantes que nous avons listées en section 6.8. Leur avantage par rapport à une recherche hexadécimale manuelle est qu'elles peuvent être lancées **toutes en même temps** en un seul scan. Plutôt que de chercher séquentiellement chaque constante, vous chargez un fichier de règles et ImHex vous dit en une passe quels algorithmes sont présents.

### Détection du packer UPX

```yara
rule packer_upx {
    meta:
        description = "Détecte la signature UPX dans un binaire ELF"
    strings:
        $upx_magic  = "UPX!"
        $upx_header = { 55 50 58 21 }
        $upx_info   = "This file is packed with the UPX"
    condition:
        any of them
}
```

UPX laisse des marqueurs identifiables dans les binaires qu'il compresse. Si cette règle matche, vous savez que le binaire est packé avec UPX et que vous pouvez le décompresser avec `upx -d` avant de l'analyser (chapitre 19 et 29).

### Détection de techniques anti-débogage

```yara
rule anti_debug_ptrace {
    meta:
        description = "Détecte un appel potentiel à ptrace (anti-debug)"
    strings:
        $ptrace_str = "ptrace" nocase
        $syscall    = { 0F 05 }
    condition:
        $ptrace_str and #syscall > 0
}
```

Cette règle combine une chaîne textuelle (`"ptrace"`) et la présence d'au moins un `syscall`. La conjonction des deux est un indicateur (pas une preuve) que le binaire utilise `ptrace(PTRACE_TRACEME)` pour détecter un débogueur — technique anti-reversing que nous étudierons au chapitre 19.

### Indicateurs de comportement réseau

```yara
rule network_indicators {
    meta:
        description = "Détecte des chaînes liées à des communications réseau"
    strings:
        $connect     = "connect" nocase
        $socket      = "socket" nocase
        $http_get    = "GET / HTTP" nocase
        $http_post   = "POST " nocase
        $user_agent  = "User-Agent:" nocase
    condition:
        3 of them
}
```

Si trois de ces cinq chaînes sont présentes, le binaire fait probablement des communications réseau — potentiellement un client HTTP embarqué ou un module de communication C2 (chapitres 23 et 28).

---

## Écrire ses propres règles : bonnes pratiques

Au fil de vos analyses, vous accumulerez des découvertes spécifiques — une séquence d'opcodes caractéristique d'un compilateur, un magic number propre à un format de fichier propriétaire, un pattern d'obfuscation récurrent. Capturer ces découvertes sous forme de règles YARA est un investissement qui rentabilise chaque analyse future.

### Commencer par des patterns hexadécimaux précis

Les règles les plus fiables reposent sur des séquences d'octets suffisamment longues et spécifiques pour éviter les faux positifs. Une règle qui cherche `{ FF FF }` matchera dans presque tous les fichiers. Une règle qui cherche les 16 premiers octets de la S-box AES ne matchera que dans les binaires qui embarquent AES.

La longueur minimale recommandée pour un pattern hexadécimal est **8 octets** pour un pattern isolé, ou **4 octets** si la condition combine plusieurs patterns. En dessous, le risque de faux positifs est trop élevé.

### Combiner plusieurs indicateurs dans la condition

Une règle avec un seul pattern est fragile. Combiner plusieurs indicateurs dans la condition renforce la spécificité :

```yara
rule custom_file_format_v2 {
    strings:
        $magic   = { 43 55 53 54 }     // "CUST"
        $version = { 02 00 }           // version 2
        $table   = "ENTRY_TABLE"
    condition:
        $magic at 0 and $version at 4 and $table
}
```

Cette règle ne matche que les fichiers qui commencent par `CUST`, ont la version 2 à l'offset 4, **et** contiennent la chaîne `ENTRY_TABLE` quelque part. Trois conditions combinées éliminent presque tout risque de faux positif.

### Documenter systématiquement avec `meta`

Chaque règle doit porter une `description` qui explique ce qu'elle détecte, un `author`, et idéalement une `reference` vers la documentation ou l'analyse qui a motivé sa création. Six mois après avoir écrit la règle, ces métadonnées seront votre seul souvenir de pourquoi ces octets sont significatifs.

### Organiser les règles par thématique

Le dossier `yara-rules/` de la formation illustre une organisation par thème : un fichier pour les constantes cryptographiques, un autre pour les signatures de packers. Vous pouvez étendre cette structure avec vos propres fichiers :

```
yara-rules/
├── crypto_constants.yar      # AES, SHA, MD5, ChaCha20...
├── packer_signatures.yar     # UPX, ASPack, custom packers
├── anti_debug.yar            # ptrace, timing checks, /proc/self
├── network_indicators.yar    # sockets, HTTP, DNS, C2 patterns
└── custom_formats.yar        # formats propriétaires rencontrés
```

Chaque fichier peut être chargé indépendamment dans ImHex selon le contexte de l'analyse.

---

## YARA dans ImHex vs YARA en CLI : complémentarité

L'intégration YARA d'ImHex ne remplace pas l'outil en ligne de commande. Les deux ont des usages complémentaires.

| Critère | YARA dans ImHex | YARA en CLI |  
|---|---|---|  
| Scan d'un seul fichier avec inspection visuelle | ✅ Optimal — localisation et contexte immédiat | Résultat textuel, pas de visualisation |  
| Scan d'un répertoire de fichiers (batch) | ❌ Un fichier à la fois | ✅ `yara -r rules.yar dossier/` |  
| Intégration dans un pipeline automatisé | ❌ Interface graphique | ✅ Scriptable, intégrable en CI/CD |  
| Développement et test de règles | ✅ Feedback visuel immédiat sur les matchs | Fonctionnel mais moins intuitif |  
| Scan de samples malware en sandbox | Possible | ✅ Plus courant (pas de GUI en sandbox) |

En pratique, vous utiliserez ImHex pour **développer et tester** vos règles (le feedback visuel immédiat accélère l'itération) et la CLI pour **déployer** ces règles sur des collections de fichiers ou dans des pipelines automatisés (chapitre 35).

---

## Pont vers la partie VI : l'analyse malware

Cette section est volontairement positionnée comme un **pont** vers la partie VI de la formation (Analyse de Code Malveillant). Les règles YARA que nous avons présentées ici — constantes crypto, signatures de packers, indicateurs réseau, techniques anti-débogage — sont exactement les outils que les analystes malware utilisent pour le triage de samples suspects.

Au chapitre 27 (ransomware), nous utiliserons les règles `crypto_constants.yar` pour identifier les algorithmes de chiffrement embarqués dans le sample. Au chapitre 28 (dropper), les règles `network_indicators.yar` nous aideront à repérer les chaînes liées au protocole C2. Au chapitre 29 (unpacking), `packer_signatures.yar` détectera le packer utilisé et nous orientera vers la bonne stratégie de décompression.

Tous ces usages partiront du même workflow : charger les règles dans ImHex, lancer le scan, localiser les matchs, inspecter et documenter. La compétence que vous construisez dans cette section est directement transférable.

---

## Résumé

YARA est un langage de signatures binaires et textuelles qui permet de détecter des patterns connus dans un fichier. Son intégration dans ImHex ajoute une dimension visuelle au scan : les matchs sont localisés et surlignés dans la vue hexadécimale, ce qui permet une inspection immédiate dans le contexte des données environnantes. Les règles s'organisent en trois sections — `meta` (documentation), `strings` (patterns hex, textuels ou regex) et `condition` (logique de matching) — et peuvent être combinées pour une détection précise. Dans le contexte de cette formation, les règles YARA servent à détecter les constantes cryptographiques, les signatures de packers, les techniques anti-débogage et les indicateurs réseau. Cette compétence sera mobilisée directement dans les cas pratiques de la partie V et l'analyse malware de la partie VI.

---


⏭️ [Cas pratique : cartographier un format de fichier custom avec `.hexpat`](/06-imhex/11-cas-pratique-format-custom.md)
