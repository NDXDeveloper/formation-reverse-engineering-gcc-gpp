🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.2 — Packing avec UPX — détecter et décompresser

> 🎯 **Objectif** : Comprendre le fonctionnement interne d'un packer, savoir identifier un binaire packé avec UPX (et reconnaître les indices généraux de packing), maîtriser les techniques de décompression statique et dynamique.

---

## Qu'est-ce qu'un packer ?

Un packer est un outil qui transforme un binaire exécutable en un autre binaire exécutable. Le binaire produit contient une version compressée (et parfois chiffrée) du code original, accompagnée d'un petit « stub » de décompression. À l'exécution, le stub s'exécute en premier, décompresse le code original en mémoire, puis lui transfère le contrôle.

Du point de vue de l'utilisateur, le programme fonctionne normalement. Du point de vue de l'analyste, le fichier sur disque ne contient plus le code réel — uniquement du bruit compressé et un stub minimaliste. Le désassemblage du fichier packé ne montre que la routine de décompression, pas la logique du programme.

Les packers sont utilisés pour deux raisons distinctes :

- **Réduction de taille** — L'usage historique. UPX a été conçu dans les années 1990 quand l'espace disque et la bande passante étaient limités. Un binaire packé avec UPX pèse typiquement 40 à 60 % de sa taille originale.  
- **Obstruction de l'analyse** — L'usage qui nous intéresse ici. Même si UPX est trivial à décompresser, des packers plus sophistiqués (Themida, VMProtect, packers custom) ajoutent du chiffrement, de l'anti-debug et de la virtualisation de code pour résister activement à l'analyse.

## UPX : le packer de référence

UPX (Ultimate Packer for eXecutables) est le packer open source le plus répandu. Il supporte de nombreux formats (ELF, PE, Mach-O, etc.) et fonctionne sur les binaires compilés avec GCC sans modification du code source.

### Comment UPX transforme un ELF

Le processus de packing d'un binaire ELF par UPX suit ces étapes :

1. **Lecture du binaire original** — UPX parse les headers ELF, identifie les segments loadables (PT_LOAD) et les sections.

2. **Compression des segments** — Le contenu des segments (code `.text`, données `.data`, `.rodata`, etc.) est compressé avec un algorithme de type NRV (Not Really Vanished) ou LZMA. Le code original disparaît du fichier.

3. **Construction du stub** — UPX génère un petit programme en assembleur (quelques centaines d'octets) qui sera le nouveau point d'entrée. Ce stub sait décompresser les données et reconstruire l'image mémoire originale.

4. **Réécriture de l'ELF** — UPX produit un nouveau fichier ELF avec une structure simplifiée : les segments originaux sont remplacés par les données compressées et le stub de décompression. Les headers sont réécrits pour pointer vers le nouveau point d'entrée.

5. **Marquage** — UPX inscrit sa signature dans le binaire (les chaînes `UPX!` et les magic bytes associés) pour pouvoir le décompresser ultérieurement avec `upx -d`.

### Ce qui se passe à l'exécution

Quand le loader Linux charge le binaire packé :

1. Le kernel charge les segments du fichier packé en mémoire (comme pour tout ELF).  
2. L'exécution commence au point d'entrée, qui est le stub UPX.  
3. Le stub décompresse les données compressées dans les zones mémoire appropriées, reconstruisant les segments originaux.  
4. Le stub corrige les permissions mémoire des pages (code = RX, données = RW).  
5. Le stub saute au point d'entrée original (`_start` ou `__libc_start_main`), et l'exécution du programme réel commence.

Tout ce processus prend quelques millisecondes. L'utilisateur ne remarque rien.

## Détecter un binaire packé avec UPX

La détection se fait à plusieurs niveaux, du plus évident au plus subtil. Un analyste expérimenté repère un binaire packé en quelques secondes lors du triage initial (workflow du chapitre 5).

### La commande `file`

`file` reconnaît souvent UPX directement :

```bash
$ file anti_reverse_upx
anti_reverse_upx: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux),  
statically linked, no section header at file offset 0,  
missing section headers at 8408 with 0 entries  
```

Les indices ici sont multiples. Le binaire apparaît comme `statically linked` alors que l'original était dynamique. Les section headers sont absents (`no section header at file offset 0`). Pour un binaire qui devrait être un simple crackme, c'est anormal.

### La commande `strings`

C'est souvent le test le plus rapide :

```bash
$ strings anti_reverse_upx | grep -i upx
$Info: This file is packed with the UPX executable packer $
$Id: UPX 4.2.2 Copyright (C) 1996-2024 the UPX Team. All Rights Reserved. $
UPX!
```

UPX laisse sa signature en clair dans le binaire. Les chaînes `$Info:` et `UPX!` sont caractéristiques. Notez que cette signature peut être retirée manuellement (on y reviendra), mais dans sa forme standard, UPX s'identifie lui-même.

Au-delà de la signature UPX, comparez la sortie de `strings` entre le binaire original et le binaire packé :

```bash
$ strings anti_reverse_stripped | wc -l
87

$ strings anti_reverse_upx | wc -l
12
```

Une chute brutale du nombre de chaînes lisibles est un indicateur fort de packing. Les chaînes originales (`"Mot de passe"`, `"Accès autorisé"`, messages d'erreur) ont été compressées et ne sont plus visibles.

### L'entropie

L'entropie mesure le degré de « désordre » des octets dans un fichier. Du code machine compilé a une entropie typique entre 5.0 et 6.5 (sur une échelle de 0 à 8). Des données compressées ou chiffrées ont une entropie proche de 7.5 à 8.0 — les octets sont quasi-aléatoires.

On peut mesurer l'entropie avec `binwalk` :

```bash
$ binwalk -E anti_reverse_stripped

DECIMAL       HEXADECIMAL     ENTROPY
---------------------------------------------
0             0x0             Rising entropy edge (0.5 -> 6.1)

$ binwalk -E anti_reverse_upx

DECIMAL       HEXADECIMAL     ENTROPY
---------------------------------------------
0             0x0             Rising entropy edge (0.5 -> 7.8)
```

Une entropie globale supérieure à 7.0 sur la majorité du fichier indique presque certainement de la compression ou du chiffrement.

ImHex offre également une vue d'entropie graphique qui permet de visualiser les zones compressées d'un seul coup d'œil : le code normal produit un profil d'entropie irrégulier avec des creux (sections `.rodata`, padding), tandis qu'un binaire packé présente un plateau uniformément élevé.

### Les sections ELF

Un binaire ELF normal compilé avec GCC possède de nombreuses sections aux noms familiers : `.text`, `.data`, `.bss`, `.rodata`, `.plt`, `.got`, `.eh_frame`, etc. Un binaire packé par UPX présente une structure radicalement différente :

```bash
$ readelf -S anti_reverse_stripped | head -20
  [Nr] Name              Type             ...
  [ 1] .interp           PROGBITS         ...
  [ 2] .note.gnu.build-id NOTE            ...
  [ 3] .gnu.hash         GNU_HASH         ...
  [ 4] .dynsym           DYNSYM           ...
  ...
  [14] .text             PROGBITS         ...
  [15] .rodata           PROGBITS         ...
  ...
  (27 sections au total)

$ readelf -S anti_reverse_upx
There are no sections in this file.
```

L'absence totale de section headers est caractéristique du packing UPX sur ELF. UPX supprime la table des sections car elle n'est pas nécessaire à l'exécution (le loader utilise les program headers, pas les section headers). Cela rend les outils comme `objdump` inutilisables sur le fichier packé.

### Les program headers (segments)

En l'absence de sections, on examine les segments :

```bash
$ readelf -l anti_reverse_upx

Program Headers:
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg
  LOAD           0x000000 0x0000000000400000 0x0000000000400000 0x...    0x...    R E
  LOAD           0x...    0x0000000000600000 0x0000000000600000 0x...    0x...    RW
  GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW
```

Deux éléments à noter. D'abord, le nombre de segments est minimal (2 LOAD + GNU_STACK). Ensuite, le rapport `FileSiz / MemSiz` est très déséquilibré sur le premier segment LOAD : la taille en mémoire (`MemSiz`) est beaucoup plus grande que la taille sur disque (`FileSiz`). C'est logique — les données compressées sur disque doivent se décompresser dans un espace mémoire plus grand.

### `checksec`

L'outil `checksec` révèle aussi des anomalies :

```bash
$ checksec --file=anti_reverse_upx
    Arch:     amd64-64-little
    RELRO:    No RELRO
    Stack:    No canary found
    NX:       NX disabled
    PIE:      No PIE
```

Un binaire qui affiche `No RELRO`, `No canary`, `NX disabled` et `No PIE` simultanément est suspect. UPX a besoin de désactiver certaines protections pour pouvoir écrire le code décompressé en mémoire (il nécessite un segment exécutable et writable pendant la décompression). Le Makefile compile d'ailleurs avec `-no-pie` avant de packer, car UPX gère mal les binaires PIE dans certaines versions.

### Résumé des indicateurs de packing

| Indicateur | Binaire normal | Binaire packé UPX |  
|---|---|---|  
| `file` | `dynamically linked, not stripped` | `statically linked, missing section headers` |  
| `strings` | Nombreuses chaînes lisibles | Quasi aucune chaîne + signature `UPX!` |  
| Entropie | 5.0 – 6.5 | 7.5 – 8.0 |  
| Sections ELF | 25–30 sections nommées | Aucune section |  
| `MemSiz / FileSiz` | Ratio proche de 1 | Ratio élevé (×2 à ×4) |  
| `checksec` | Protections variées | Tout désactivé |

## Décompresser un binaire UPX

### Décompression statique : `upx -d`

UPX étant réversible par design, la décompression est triviale quand la signature est intacte :

```bash
$ upx -d anti_reverse_upx -o anti_reverse_unpacked
                       Ultimate Packer for eXecutables
                          Copyright (C) 1996 - 2024
UPX 4.2.2       Markus Oberhumer, Laszlo Molnar & John Reese

        File size         Ratio      Format      Name
   --------------------   ------   -----------   -----------
     14832 <-      6480   43.69%   linux/amd64   anti_reverse_unpacked

Unpacked 1 file.
```

Le binaire décompressé est fonctionnellement identique à l'original. On peut maintenant le désassembler, le charger dans Ghidra, poser des breakpoints — l'analyse reprend normalement.

### Quand `upx -d` échoue : signature altérée

Un auteur de malware ou un développeur cherchant à compliquer l'analyse peut modifier la signature UPX dans le binaire packé. Les magic bytes `UPX!` sont remplacés par des valeurs arbitraires, et la chaîne `$Info:` est effacée. Dans ce cas, `upx -d` refuse de décompresser :

```bash
$ upx -d binaire_modifie
upx: binaire_modifie: NotPackedException: not packed by UPX
```

Pour contourner cela, deux approches sont possibles.

**Restaurer la signature** — Les magic bytes UPX sont situés à des offsets connus dans le fichier. On peut les restaurer avec un éditeur hexadécimal (ImHex) en cherchant les patterns caractéristiques du stub UPX et en réécrivant les 4 octets `UPX!` (`55 50 58 21`) aux bons emplacements. Il y a généralement trois occurrences de la signature dans un binaire UPX.

**Décompression dynamique** — Au lieu de tenter de décompresser le fichier, on le laisse se décompresser lui-même en l'exécutant, puis on récupère le code en mémoire. C'est l'approche décrite dans la section suivante.

### Décompression dynamique : dump mémoire avec GDB

La décompression dynamique consiste à laisser le stub s'exécuter, attendre qu'il ait fini de décompresser le code, puis récupérer l'image mémoire contenant le programme original.

**Étape 1 — Trouver le point de saut vers le code original**

Le stub UPX se termine par un saut (souvent un `jmp` indirect ou un `call`) vers le point d'entrée du programme décompressé. On peut le repérer en désassemblant le stub :

```bash
$ objdump -d -M intel anti_reverse_upx | tail -20
```

Le stub est court (quelques centaines d'instructions). Le dernier saut inconditionnel à la fin du stub est généralement le transfert de contrôle vers le code original.

**Étape 2 — Poser un breakpoint et exécuter**

```
$ gdb ./anti_reverse_upx
(gdb) starti
(gdb) info proc mappings
```

On examine les mappings mémoire. Après avoir identifié la fin du stub, on pose un breakpoint juste après la décompression :

```
(gdb) break *0x<adresse_du_jmp_final>
(gdb) continue
```

Le stub s'exécute, décompresse tout, et s'arrête juste avant de sauter au code original.

**Étape 3 — Dumper la mémoire**

On utilise `dump memory` pour extraire les segments décompressés :

```
(gdb) info proc mappings
(gdb) dump memory code_dump.bin 0x400000 0x402000
(gdb) dump memory data_dump.bin 0x600000 0x601000
```

Le chapitre 29 détaille la reconstruction d'un ELF fonctionnel à partir de ces dumps mémoire.

**Alternative avec `/proc/pid/mem`**

Sans GDB, on peut aussi lire directement la mémoire du processus via `/proc/<pid>/mem` en combinaison avec `/proc/<pid>/maps` :

```bash
# Dans un terminal, lancer le binaire et le suspendre
$ ./anti_reverse_upx &
$ PID=$!
$ kill -STOP $PID

# Lire les mappings
$ cat /proc/$PID/maps

# Dumper un segment
$ dd if=/proc/$PID/mem bs=1 skip=$((0x400000)) count=$((0x2000)) \
     of=segment_dump.bin
```

## Au-delà d'UPX : reconnaître d'autres packers

UPX est le packer le plus courant et le plus facile à traiter. D'autres packers existent et sont significativement plus résistants à l'analyse. Ils seront développés dans le chapitre 29, mais voici les indices généraux qui trahissent un packing, quel que soit le packer :

- **Entropie élevée** (> 7.0) sur la majorité du fichier  
- **Très peu de chaînes lisibles** par rapport à la taille du binaire  
- **Noms de sections inhabituels** — Certains packers renomment les sections (`.UPX0`, `.UPX1` pour UPX sur PE, noms aléatoires pour d'autres packers)  
- **Point d'entrée qui pointe dans une section inhabituelle** — Par exemple dans `.data` ou dans une section inconnue plutôt que dans `.text`  
- **Ratio `MemSiz / FileSiz` anormal** dans les program headers  
- **Détection par signatures** — Les règles YARA (chapitre 6, section 6.10 et chapitre 35) permettent de détecter les packers connus par leurs patterns d'octets caractéristiques

La distinction fondamentale à retenir : le packing est une transformation réversible du binaire sur disque. Une fois en mémoire, le code original est toujours là, en clair, prêt à être analysé. C'est pourquoi la décompression dynamique (dump mémoire) fonctionne universellement, même quand la décompression statique échoue.

---


⏭️ [Obfuscation de flux de contrôle (Control Flow Flattening, bogus control flow)](/19-anti-reversing/03-obfuscation-flux-controle.md)
