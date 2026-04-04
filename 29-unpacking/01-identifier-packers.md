🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 29.1 — Identifier UPX et packers custom avec `checksec` + ImHex + entropie

> 🎯 **Objectif de cette section** — Avant de tenter un quelconque unpacking, il faut d'abord **confirmer** que le binaire est packé et, si possible, **identifier le packer** utilisé. Cette section présente une méthodologie systématique en cinq indices convergents, que l'on appliquera sur les variantes de `packed_sample` compilées au chapitre.

---

## Le problème : comment savoir qu'un binaire est packé ?

Un binaire packé ne porte aucune étiquette qui dit « je suis compressé ». Il reste un fichier ELF parfaitement valide : le noyau Linux peut le charger et l'exécuter sans problème. Le stub de décompression est du code machine tout à fait normal. C'est seulement en examinant les **propriétés statistiques** du fichier, la **structure de ses sections**, et le **comportement de ses métadonnées** qu'on peut déduire que le code visible n'est pas le code réel.

L'identification repose sur la convergence de plusieurs indices. Aucun indice pris isolément n'est suffisant — un binaire légitime peut avoir une section à haute entropie (données compressées embarquées), des noms de sections inhabituels (linker custom), ou peu de chaînes lisibles (binaire Rust ou Go). C'est la **combinaison** de ces signaux qui permet de conclure avec confiance.

---

## Indice 1 — Le triage rapide avec `file` et `strings`

La première commande à exécuter face à tout binaire inconnu reste `file`. Sur un binaire packé UPX, la sortie est souvent révélatrice :

```
$ file packed_sample_upx
packed_sample_upx: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux),  
statically linked, no section header  
```

Deux éléments doivent attirer l'attention. Premièrement, la mention **`statically linked`** alors que le programme original utilise des fonctions de la libc (`printf`, `fgets`, `strcmp`…) — un binaire GCC standard est presque toujours dynamiquement lié. UPX produit un exécutable autonome qui embarque le stub et les données compressées, sans dépendance externe. Deuxièmement, la mention **`no section header`** (ou un nombre de sections anormalement bas) : UPX supprime la table des sections, qui n'est pas nécessaire au chargement par le noyau mais qui est indispensable aux désassembleurs.

Enchaînons immédiatement avec `strings` :

```
$ strings packed_sample_O2_strip | wc -l
87

$ strings packed_sample_upx | wc -l
9
```

Le binaire non packé contient des dizaines de chaînes lisibles : le banner, le flag, les messages d'erreur, les noms de fonctions libc. Le binaire packé n'en contient presque aucune — les seules chaînes visibles sont celles du stub UPX lui-même. Si `strings` ne renvoie qu'une poignée de résultats sur un binaire censé interagir avec l'utilisateur (affichage de texte, lecture d'input), c'est un signal fort de packing.

> 💡 **Attention au piège inverse** — Un binaire Go ou Rust strippé peut aussi produire très peu de chaînes avec `strings`, sans pour autant être packé. C'est pourquoi on ne s'arrête jamais à un seul indice.

---

## Indice 2 — Analyse des sections et segments avec `readelf`

La structure interne d'un ELF packé diffère radicalement de celle d'un binaire classique. Examinons les **program headers** (segments) et les **section headers** :

```
$ readelf -l packed_sample_upx
```

Sur un binaire UPX typique, on observe généralement deux ou trois segments `LOAD` avec des caractéristiques inhabituelles :

- Un premier segment `LOAD` avec les flags **`RWE`** (Read-Write-Execute). Dans un binaire normal, les segments de code sont `RE` (Read-Execute) et les segments de données sont `RW` (Read-Write). Un segment simultanément inscriptible et exécutable est un marqueur quasi certain de packing ou de self-modifying code : le stub a besoin d'écrire le code décompressé dans une zone qu'il va ensuite exécuter.  
- Un **ratio de taille** anormal entre la taille sur disque (`FileSiz`) et la taille en mémoire (`MemSiz`). Un segment dont le `MemSiz` est très supérieur au `FileSiz` indique que le loader devra allouer beaucoup plus de mémoire que ce qui est présent dans le fichier — c'est exactement ce qui se passe quand le stub va y décompresser le code original.

Côté sections :

```
$ readelf -S packed_sample_upx
```

Si la table des sections existe encore (UPX la supprime souvent, mais certains packers la conservent), on peut observer des **noms de sections non standard**. UPX utilise historiquement `UPX0`, `UPX1`, `UPX2` comme noms de sections. Sur notre variante `packed_sample_upx_tampered`, ces noms ont été altérés en `XP_0`, `XP_1`, `XP_2` — mais le fait même que les noms ne correspondent à aucune convention ELF habituelle (`.text`, `.data`, `.rodata`…) reste un indice.

Pour comparaison, le binaire non packé présente une structure de sections parfaitement classique :

```
$ readelf -S packed_sample_O2_strip | grep -c "\."
27
```

Vingt-sept sections avec des noms standard (`.text`, `.rodata`, `.data`, `.bss`, `.plt`, `.got`, `.init`, `.fini`…) contre deux ou trois sections aux noms exotiques dans le binaire packé.

---

## Indice 3 — `checksec` et les protections binaires

L'outil `checksec` (inclus dans `pwntools` et disponible en standalone) affiche les protections de sécurité d'un binaire. Sur un binaire packé, le résultat est souvent caractéristique :

```
$ checksec --file=packed_sample_O2_strip
    Arch:     amd64-64-little
    RELRO:    Partial RELRO
    Stack:    Canary found
    NX:       NX enabled
    PIE:      No PIE

$ checksec --file=packed_sample_upx
    Arch:     amd64-64-little
    RELRO:    No RELRO
    Stack:    No canary found
    NX:       NX disabled
    PIE:      No PIE
```

La disparition simultanée de toutes les protections est un signal d'alarme. Voici pourquoi chaque changement est significatif :

- **NX disabled** — Le bit NX (No-eXecute) empêche l'exécution de code dans les segments de données. UPX a besoin de décompresser du code dans une zone mémoire initialement marquée comme données, puis de l'exécuter. Pour que cela fonctionne, la protection NX doit être désactivée (ou le stub doit utiliser `mprotect` pour changer les permissions, ce que certains packers plus sophistiqués font).  
- **No canary found** — Les stack canaries sont insérés par GCC à la compilation (`-fstack-protector`). Comme le binaire visible est le stub du packer (pas le code compilé par GCC), les canaries du code original ne sont pas détectables.  
- **No RELRO** — La protection RELRO (Relocation Read-Only) concerne la table GOT. Puisque le binaire packé n'a pas de GOT conventionnelle (le stub gère lui-même la résolution des imports après décompression), RELRO est absent.

En résumé : un binaire qui était compilé avec les protections standard de GCC et qui les perd toutes d'un coup est très probablement passé par un packer.

---

## Indice 4 — Analyse d'entropie

L'entropie est la mesure la plus fiable pour détecter le packing. Le concept est simple : l'entropie de Shannon mesure le degré de « désordre » d'une séquence d'octets, sur une échelle de 0.0 (séquence parfaitement uniforme, par exemple que des zéros) à 8.0 (séquence statistiquement indistinguable de données aléatoires).

Le code machine x86-64 a typiquement une entropie comprise entre **5.0 et 6.5** : il contient des patterns répétitifs (prologues de fonctions, instructions courantes) mais assez de variété pour ne pas être trivial. Les données compressées (et a fortiori chiffrées) ont une entropie comprise entre **7.5 et 8.0** : la compression élimine toute redondance, ce qui fait ressembler les données à du bruit aléatoire.

### Méthode 1 — `binwalk -E`

L'outil `binwalk` avec l'option `-E` (entropy) produit un graphe d'entropie par blocs sur l'ensemble du fichier :

```
$ binwalk -E packed_sample_O2_strip
```

Sur un binaire non packé, le graphe montre des variations : entropie moyenne sur `.text`, plus basse sur `.rodata` (chaînes de caractères lisibles), très basse sur `.bss` (zéros). Sur un binaire packé, le graphe montre un **plateau quasi uniforme à une entropie supérieure à 7.5** sur la majorité du fichier, avec éventuellement une petite zone à entropie plus basse correspondant au stub de décompression.

### Méthode 2 — Script Python avec `math.log2`

Pour une analyse plus fine (par section ou par segment), on peut calculer l'entropie manuellement :

```python
import math  
from collections import Counter  

def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = Counter(data)
    length = len(data)
    return -sum(
        (c / length) * math.log2(c / length)
        for c in counts.values()
    )
```

En appliquant cette fonction aux données de chaque segment `LOAD` (extraites avec `readelf` + `dd` ou avec `pyelftools`), on obtient des valeurs numériques précises. Les seuils empiriques à retenir sont les suivants : en dessous de 6.0, les données sont probablement du code machine ou des données en clair ; entre 6.0 et 7.0, il peut s'agir de code optimisé ou de données structurées ; au-dessus de 7.5, il s'agit presque certainement de données compressées ou chiffrées.

### Méthode 3 — ImHex (analyse visuelle)

ImHex offre une vue d'entropie intégrée accessible via **View → Data Information**. Cette vue affiche un histogramme de la distribution des octets et une courbe d'entropie par blocs directement superposée à la vue hexadécimale. C'est l'approche la plus intuitive : on **voit** littéralement les zones compressées (couleur uniforme, distribution plate des octets) se distinguer des zones de code ou de données (distribution irrégulière, pics sur certaines valeurs d'octets).

Dans ImHex, la distribution des octets d'un binaire non packé présente des pics caractéristiques : le `0x00` (padding, fin de chaînes) domine largement, suivi de quelques opcodes fréquents. Sur un binaire packé, la distribution est presque plate — chaque valeur d'octet de `0x00` à `0xFF` apparaît avec une fréquence quasi identique. Cette « platitude » est le signe visuel le plus immédiat du packing.

---

## Indice 5 — Signatures spécifiques du packer

Certains packers laissent des signatures identifiables dans le binaire, même sans analyser le code du stub.

### Signatures UPX

UPX est le packer le plus courant et le plus facile à identifier. Il laisse plusieurs marqueurs :

- La chaîne ASCII **`UPX!`** (magic bytes `0x55 0x50 0x58 0x21`) présente dans les métadonnées de compression, généralement vers la fin du fichier. On peut la rechercher avec `grep` en mode binaire ou dans ImHex avec une recherche hexadécimale.  
- Les noms de sections **`UPX0`**, **`UPX1`**, **`UPX2`** si la table des sections est conservée.  
- La chaîne **`$Info: This file is packed with the UPX executable packer`** parfois présente dans les versions d'UPX qui n'ont pas été invoquées avec `--no-banner`.  
- La structure du **header de compression UPX** à un offset fixe par rapport à la fin du fichier (`p_info`, `l_info`, `p_blocksize`), documentée dans le code source d'UPX.

```
$ grep -c "UPX!" packed_sample_upx
1

$ grep -c "UPX!" packed_sample_upx_tampered
0
```

Sur notre variante `packed_sample_upx_tampered`, les signatures ont été volontairement remplacées (`UPX!` → `FKP!`, `UPX0` → `XP_0`…). C'est une technique courante dans les malwares qui utilisent UPX mais veulent empêcher la décompression automatique avec `upx -d`. Cependant, le **code du stub** reste celui d'UPX : les séquences d'instructions de décompression (boucle de copie, gestion des runs de zéros) sont reconnaissables à l'analyse du point d'entrée.

### Signatures d'autres packers ELF

Au-delà d'UPX, d'autres packers laissent des traces identifiables :

- **Ezuri** — Packer Go ciblant les binaires ELF Linux, répandu dans les botnets IoT. Le stub est un programme Go compilé statiquement, ce qui produit un binaire volumineux (plusieurs Mo) avec les structures internes Go (`gopclntab`, runtime). La présence d'un runtime Go dans un binaire qui n'est pas censé être écrit en Go est un signal fort.  
- **Midgetpack** — Utilise des sections ELF avec des noms aléatoires et un stub écrit en assembleur. L'entropie reste le meilleur indicateur.  
- **Packers custom** — Aucune signature connue. L'identification repose uniquement sur les indices structurels (entropie, segments RWE, ratio `FileSiz`/`MemSiz`, absence de sections standard) et sur l'analyse du code au point d'entrée.

### Règles YARA pour la détection automatisée

On peut formaliser la détection de packers sous forme de règles YARA, comme vu au chapitre 6 (section 6.10). Une règle simple pour UPX standard pourrait cibler le magic `UPX!` combiné à la présence de segments `RWE`. Pour les packers custom, les règles porteront sur des critères statistiques (entropie calculée par le module `math` de YARA) plutôt que sur des signatures exactes.

Le fichier `yara-rules/packer_signatures.yar` du dépôt contient un jeu de règles prêtes à l'emploi pour les packers les plus courants.

---

## Synthèse : la grille de détection

Pour conclure cette section, voici la grille de décision que l'on appliquera systématiquement face à un binaire suspect. On considère que le binaire est probablement packé si **trois indices ou plus** convergent :

| # | Indice | Outil | Seuil / Signal |  
|---|--------|-------|----------------|  
| 1 | Très peu de chaînes lisibles | `strings \| wc -l` | Moins de ~15 pour un binaire interactif |  
| 2 | Sections absentes ou noms non standard | `readelf -S` | Noms hors convention ELF (`.text`, `.data`…) |  
| 3 | Segment LOAD avec flags RWE | `readelf -l` | Flags simultanés Read+Write+Execute |  
| 4 | Ratio `MemSiz` ≫ `FileSiz` sur un segment | `readelf -l` | Rapport supérieur à ~3× |  
| 5 | Protections absentes (NX off, no canary, no RELRO) | `checksec` | Perte de toutes les protections GCC |  
| 6 | Entropie globale > 7.5 | `binwalk -E` / ImHex | Plateau d'entropie haute sur le fichier |  
| 7 | Distribution des octets quasi uniforme | ImHex (Data Information) | Histogramme plat de `0x00` à `0xFF` |  
| 8 | Signature connue du packer | `strings` / ImHex / YARA | `UPX!`, noms de sections, magic bytes |

Lorsque la convergence est établie et le packer identifié (ou non), on passe à l'étape suivante : le décompression effective du binaire, qui fera l'objet de la section 29.2.

---

> 📌 **Point clé à retenir** — L'identification du packing est un diagnostic différentiel : on accumule des indices indépendants et on conclut par convergence. Un seul indice ne suffit jamais. L'entropie est l'indicateur le plus fiable, mais il doit toujours être corroboré par l'analyse structurelle (sections, segments, protections) pour éviter les faux positifs.

⏭️ [Unpacking statique (UPX) et dynamique (dump mémoire avec GDB)](/29-unpacking/02-unpacking-statique-dynamique.md)
