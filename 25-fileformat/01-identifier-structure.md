🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 25.1 — Identifier la structure générale avec `file`, `strings` et `binwalk`

> 🎯 **Objectif de cette section** : établir un premier portrait du format de fichier inconnu en moins de dix minutes, sans ouvrir de désassembleur. À la fin, vous aurez identifié les magic bytes, les chaînes visibles, les zones d'entropie, et formulé vos premières hypothèses sur l'organisation des données.

---

## Poser le contexte

Imaginons la situation suivante : on dispose du binaire `fileformat_O2_strip` (compilé en `-O2`, strippé) et de trois fichiers `.cfr` produits par ce binaire — `demo.cfr`, `packed_noxor.cfr` et `packed_xor.cfr`. On ne sait rien du format. L'extension `.cfr` ne correspond à aucun format connu. Notre mission commence.

La tentation est grande d'ouvrir immédiatement Ghidra pour analyser le parseur. Résistez. Le reverse d'un format de fichier est **plus efficace quand on commence par les données elles-mêmes** plutôt que par le code. Observer les octets bruts avec des outils simples permet de formuler des hypothèses concrètes *avant* de plonger dans le désassemblage. On arrive ensuite dans Ghidra avec des questions précises plutôt qu'une exploration à l'aveugle.

Les trois outils de cette première passe — `file`, `strings` et `binwalk` — sont volontairement primitifs. C'est leur force : ils n'ont pas besoin de comprendre le format pour révéler des informations utiles.

---

## Étape 1 — `file` : tenter l'identification automatique

La commande `file` identifie un fichier en examinant ses premiers octets et en les comparant à une base de signatures (les « magic patterns » définis dans `/usr/share/misc/magic`). Pour un format custom inconnu, `file` ne trouvera rien de pertinent — et c'est justement une information.

```bash
$ file demo.cfr
demo.cfr: data
```

Le verdict `data` signifie que `file` n'a reconnu aucune signature connue. C'est le résultat attendu pour un format propriétaire. Si `file` avait retourné quelque chose de plus précis (un PNG, un ZIP, un ELF…), cela aurait signifié soit que le format encapsule un format connu, soit que le fichier n'est pas ce qu'on croit.

Vérifions aussi les deux autres archives :

```bash
$ file packed_noxor.cfr packed_xor.cfr
packed_noxor.cfr: data  
packed_xor.cfr:   data  
```

Même résultat. On note au passage que les trois fichiers portent la même extension mais pourraient avoir des variations internes. Gardons cela en tête.

> 💡 **Réflexe utile** : lancer `file` aussi sur le binaire lui-même pour confirmer sa nature.  
>  
> ```bash  
> $ file fileformat_O2_strip  
> fileformat_O2_strip: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
>                      dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
>                      BuildID[sha1]=..., for GNU/Linux 3.2.0, stripped  
> ```  
>  
> On confirme un ELF x86-64 dynamiquement linké et strippé — cohérent avec nos paramètres de compilation.

---

## Étape 2 — `xxd` : observer les premiers octets

Avant de passer à `strings`, prenons l'habitude de regarder les tout premiers octets du fichier. C'est là que se trouvent presque toujours les magic bytes d'un format.

```bash
$ xxd demo.cfr | head -4
00000000: 4346 524d 0200 0200 0400 0000 xxxx xxxx  CFRM............
00000010: xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx  ................
00000020: 01xx xxxx xxxx xxxx xxxx xxxx 6772 6565  ............gree
00000030: 7469 6e67 2e74 7874 xxxx xxxx xxxx xxxx  ting.txt........
```

*(Les `xx` représentent des octets variables — timestamps, CRC, etc.)*

Dès les quatre premiers octets, on lit les caractères ASCII **`CFRM`**. C'est notre première découverte majeure : le format utilise un magic number de 4 octets en début de fichier. Notons-le immédiatement.

On observe aussi que dès l'offset `0x20` environ, on commence à voir des noms de fichiers lisibles (`greeting.txt`). Cela suggère que le header est relativement court (une trentaine d'octets ?) et que les données utiles commencent rapidement après.

Regardons aussi la fin du fichier :

```bash
$ xxd demo.cfr | tail -4
```

Si le format possède un footer, on devrait y trouver un second magic. Cherchons :

```bash
$ xxd demo.cfr | grep -i "crfe\|cfrm"
```

Si on repère la chaîne `CRFE` vers la fin du fichier, c'est un indice fort de la présence d'un footer avec son propre magic.

---

## Étape 3 — `strings` : extraire les chaînes lisibles

La commande `strings` extrait toutes les séquences d'octets imprimables d'une longueur minimale (4 par défaut). Sur un format de fichier, elle révèle les noms, les métadonnées textuelles, et parfois des indices structurels.

### Sur les fichiers de données

```bash
$ strings demo.cfr
CFRM  
greeting.txt  
Hello from the CFR archive format!  
This is a sample text record.  
data.bin  
version.meta  
format=CFR  
version=2  
author=student  
notes.txt  
This archive was generated for Chapter 25 of the RE training.  
Your mission: reverse-engineer this format completely.  
CRFE  
```

Cette sortie est une mine d'or. On y apprend que :

- **`CFRM`** apparaît en début de fichier (magic header confirmé).  
- **`CRFE`** apparaît en fin de fichier (magic footer probable).  
- Le format contient des **noms de fichiers** (`greeting.txt`, `data.bin`, `version.meta`, `notes.txt`), ce qui confirme qu'il s'agit d'un format d'archive.  
- Des contenus textuels sont lisibles en clair — au moins pour certains enregistrements.  
- Des paires clé-valeur apparaissent (`format=CFR`, `version=2`, `author=student`), suggérant un type d'enregistrement de métadonnées distinct.

Maintenant comparons avec l'archive XOR :

```bash
$ strings packed_xor.cfr
CFRM  
test.txt  
info.meta  
fake.bin  
CRFE  
```

Observation importante : les **noms de fichiers** restent lisibles, mais les **contenus** ont disparu de la sortie `strings`. Cela signifie que les données (payload) sont transformées d'une manière qui casse les chaînes ASCII, alors que les noms ne le sont pas. Hypothèse : une forme d'obfuscation ou de compression est appliquée sélectivement sur les données, mais pas sur les noms.

Vérifions avec l'archive non-XOR :

```bash
$ strings packed_noxor.cfr
CFRM  
test.txt  
This is a plain text test file for packing.  
info.meta  
chapter=25  
topic=fileformat  
fake.bin  
CRFE  
```

Ici les contenus textuels sont lisibles. La différence entre `packed_xor.cfr` et `packed_noxor.cfr` confirme l'hypothèse : une transformation optionnelle est appliquée aux données. Le fait qu'elle soit « optionnelle » suggère l'existence d'un flag dans le header qui contrôle ce comportement.

### Sur le binaire lui-même

Lancer `strings` sur le binaire exécutable est tout aussi révélateur :

```bash
$ strings fileformat_O2_strip | head -60
```

On y trouvera typiquement :

- Les chaînes d'erreur et d'usage : `"Usage:"`, `"generate"`, `"pack"`, `"list"`, `"read"`, `"unpack"`, `"validate"` — ce qui révèle les sous-commandes supportées par le binaire.  
- Les chaînes de format : `"CFRM"`, `"CRFE"`, `"TEXT"`, `"BINARY"`, `"META"` — ce qui confirme nos observations sur les magics et révèle les types d'enregistrements.  
- Des messages de diagnostic : `"Invalid magic"`, `"Header CRC"`, `"Record %u"`, `"CRC-16"` — qui trahissent la présence de mécanismes de vérification d'intégrité (CRC-32 pour le header, CRC-16 par enregistrement).  
- Des chaînes liées à des champs : `"Author"`, `"Flags"`, `"Version"`, `"Records"` — indices sur la structure du header.

Filtrons les chaînes les plus intéressantes :

```bash
$ strings fileformat_O2_strip | grep -iE "crc|magic|record|header|footer|flag|xor"
```

Chaque chaîne trouvée ici est un fil à tirer lors de l'analyse dans Ghidra. Le message `"Invalid magic: expected CFRM"` par exemple, une fois localisé dans le désassemblage, nous mènera directement à la fonction de parsing du header.

> 📝 **À retenir** : `strings` sur le binaire est souvent *plus* informatif que `strings` sur les données elles-mêmes, car le binaire contient les messages d'erreur qui décrivent les contraintes du format.

---

## Étape 4 — `binwalk` : scanner les structures connues et l'entropie

`binwalk` est un outil d'analyse de firmware qui excelle à détecter des formats imbriqués (archives dans archives, filesystems, images compressées) et à calculer l'entropie par blocs. Même quand il ne reconnaît rien, son analyse d'entropie reste précieuse.

### Scan de signatures

```bash
$ binwalk demo.cfr

DECIMAL       HEXADECIMAL     DESCRIPTION
--------------------------------------------------
```

Aucune signature reconnue — c'est attendu pour un format entièrement custom. Si `binwalk` avait détecté un en-tête gzip, un filesystem JFFS2, ou une image PNG à un certain offset, cela aurait signifié que le format encapsule des données dans un format standard.

### Analyse d'entropie

C'est ici que `binwalk` devient vraiment utile pour le reverse de formats :

```bash
$ binwalk -E demo.cfr
```

Cette commande produit un graphique (ou une sortie textuelle) de l'entropie par blocs. L'entropie mesure le « désordre » des octets sur une échelle de 0 (parfaitement uniforme, par ex. que des zéros) à 1 (aléatoire pur, par ex. données chiffrées ou compressées).

Pour notre archive `demo.cfr` :
- Le **header** (premiers ~32 octets) aura une entropie modérée — un mélange d'octets structurés (magic ASCII, compteurs, CRC) et de padding.  
- Les **enregistrements textuels** auront une entropie typique de texte ASCII (~0.4–0.6).  
- L'**enregistrement binaire** (`data.bin`) aura une entropie plus élevée.

Comparons maintenant les trois archives :

```bash
$ binwalk -E packed_noxor.cfr
$ binwalk -E packed_xor.cfr
```

La différence entre ces deux fichiers sera visuellement parlante. L'archive XOR montrera une entropie significativement plus élevée sur les zones de données, alors que les zones de noms (non transformées) garderont la même entropie. Cette différence d'entropie entre noms et données est un indice supplémentaire que la transformation ne s'applique qu'aux payloads.

> 💡 **Interprétation de l'entropie** :  
>  
> | Entropie     | Interprétation typique |  
> |---|---|  
> | 0.0 – 0.2    | Padding, zones de zéros, données très structurées |  
> | 0.3 – 0.6    | Texte ASCII, code exécutable, données structurées |  
> | 0.7 – 0.85   | Données transformées (XOR simple, encodage basique) |  
> | 0.85 – 1.0   | Données chiffrées ou compressées (AES, gzip, zlib…) |  
>  
> Le XOR à clé courte (4 octets dans notre cas) élève l'entropie du texte mais ne l'amène généralement pas au niveau du chiffrement fort. Observer une entropie autour de 0.7–0.8 sur une zone qui devrait contenir du texte est un indice classique de XOR.

### Scan de chaînes brutes avec offset

`binwalk` peut aussi extraire les chaînes avec leurs offsets, de façon similaire à `strings` mais avec un formatage orienté analyse :

```bash
$ binwalk -R "CFRM" demo.cfr
$ binwalk -R "CRFE" demo.cfr
```

Ces recherches de motifs bruts confirment les positions exactes des magic bytes dans le fichier. On vérifie ainsi que `CFRM` n'apparaît qu'une seule fois (en offset 0) et que `CRFE` n'apparaît qu'en fin de fichier (footer).

---

## Étape 5 — Premiers calculs sur la taille

Avant de refermer cette phase de reconnaissance, notons les tailles des fichiers :

```bash
$ ls -la samples/*.cfr
-rw-r--r-- 1 user user  364 ... demo.cfr
-rw-r--r-- 1 user user  204 ... packed_noxor.cfr
-rw-r--r-- 1 user user  204 ... packed_xor.cfr
```

Les deux archives `packed_*.cfr` ont la même taille — logique, elles contiennent les mêmes fichiers, seule la transformation des données diffère. Le XOR ne change pas la taille (c'est une transformation octet par octet), ce qui exclut la compression comme explication de la transformation.

On peut aussi commencer à estimer la taille du header. Si le magic fait 4 octets et que les premières données lisibles apparaissent vers l'offset `0x20` (32 en décimal), le header fait probablement 32 octets. C'est une taille ronde et classique pour un header de format binaire.

De même, si `CRFE` est suivi de quelques octets avant la fin du fichier, on peut estimer la taille du footer.

---

## Synthèse : ce qu'on sait après 10 minutes

Récapitulons les hypothèses formulées à ce stade, sans avoir touché un désassembleur :

| Découverte | Source | Confiance |  
|---|---|---|  
| Magic header : `CFRM` (4 octets à l'offset 0) | `xxd`, `strings` | Certaine |  
| Magic footer : `CRFE` (fin de fichier) | `strings`, `binwalk -R` | Forte |  
| Le format est une archive (contient des fichiers nommés) | `strings` | Certaine |  
| Taille probable du header : ~32 octets | `xxd` | Hypothèse |  
| Au moins 3 types de contenu : texte, binaire, métadonnées | `strings` sur le binaire | Forte |  
| Transformation optionnelle des données (XOR probable) | Comparaison `strings` + entropie | Forte |  
| La transformation ne modifie pas la taille | `ls -la` | Certaine |  
| Les noms ne sont pas transformés | `strings` sur l'archive XOR | Certaine |  
| Mécanismes d'intégrité : CRC-32 (header) + CRC-16 (records) | `strings` sur le binaire | Forte |  
| Un flag dans le header contrôle la transformation | Existence de deux variantes | Hypothèse |

C'est un point de départ solide. On sait ce qu'on cherche, on a des mots-clés pour naviguer dans le désassemblage (`CFRM`, `Invalid magic`, `CRC`), et on a des hypothèses testables.

---

## Créer un carnet de notes structuré

Avant de passer à la cartographie hexadécimale (section 25.2), ouvrez un fichier de notes dédié. Un bon format de travail :

```markdown
# Reverse CFR Format — Notes

## Magic bytes
- Header: 0x4346524D ("CFRM") @ offset 0x00
- Footer: 0x43524645 ("CRFE") @ fin de fichier

## Tailles estimées
- Header: ~32 octets (à confirmer)
- Footer: ~12 octets (à confirmer)

## Types d'enregistrements
- TEXT, BINARY, META (strings du binaire)

## Transformation
- Optionnelle (flag), probablement XOR
- S'applique aux données, pas aux noms
- Ne change pas la taille → pas de compression

## Intégrité
- CRC-32 pour le header
- CRC-16 par enregistrement
- CRC global possible (footer ?)

## Questions ouvertes
- [ ] Structure exacte du header (quels champs, quel ordre ?)
- [ ] Où est le flag XOR dans le header ?
- [ ] Comment est structuré un enregistrement ? (header + nom + données + CRC ?)
- [ ] Quelle variante de CRC-16 ? (polynôme, valeur initiale)
- [ ] Quelle est la clé XOR ?
- [ ] Que contient le footer exactement ?
- [ ] Le champ "reserved" visible dans les messages du binaire sert-il à quelque chose ?
```

Ce carnet de notes va évoluer tout au long des sections suivantes. Chaque hypothèse sera confirmée ou corrigée, chaque question résolue et cochée.

---


⏭️ [Cartographier les champs avec ImHex et un pattern `.hexpat` itératif](/25-fileformat/02-cartographier-imhex-hexpat.md)
