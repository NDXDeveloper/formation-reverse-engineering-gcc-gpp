🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 6.9 — Intégration avec le désassembleur intégré d'ImHex

> 🎯 **Objectif de cette section** : Utiliser le désassembleur intégré d'ImHex pour inspecter ponctuellement du code machine sans quitter l'éditeur hexadécimal, comprendre ses capacités et ses limites par rapport aux désassembleurs dédiés (objdump, Ghidra, radare2), et savoir dans quels cas de figure il apporte un gain de temps réel.

> 📦 **Binaire de test** : n'importe quel ELF de `binaries/` — par exemple `binaries/ch21-keygenme/keygenme_O0`

---

## Le besoin : voir les instructions sans changer d'outil

Au cours d'une analyse dans ImHex, vous rencontrez régulièrement des situations où la vue hexadécimale brute ne suffit pas. Vous avez localisé une séquence d'opcodes intéressante en section 6.8 — un `E8` suivi de 4 octets, un `0F 05`, un prologue `55 48 89 E5` — et vous voulez vérifier ce que ces octets signifient en tant qu'instructions assembleur. Ou bien vous explorez la section `.text` à un offset donné et vous voulez comprendre le flux de code autour de cet offset.

La solution classique est de basculer vers un autre outil : ouvrir un terminal et lancer `objdump -d`, ou importer le binaire dans Ghidra. Mais ce changement de contexte a un coût. Il faut retrouver le même offset dans l'autre outil, synchroniser mentalement les deux vues, et vous perdez le contact visuel avec les données hexadécimales environnantes. Pour une vérification ponctuelle, c'est disproportionné.

Le désassembleur intégré d'ImHex résout ce problème. Sans quitter l'éditeur, sans ouvrir un autre programme, vous pouvez désassembler une plage d'octets et voir les mnémoniques assembleur correspondants — directement à côté de la vue hexadécimale.

---

## Architecture technique : Capstone sous le capot

Le désassembleur d'ImHex repose sur la bibliothèque **Capstone**, un framework de désassemblage multi-architecture open source. Capstone est un désassembleur **linéaire** (linear sweep) : il lit les octets séquentiellement à partir d'un point de départ donné et décode chaque instruction l'une après l'autre, sans analyser le flux de contrôle.

Capstone supporte de nombreuses architectures, et ImHex en hérite la polyvalence : x86, x86-64, ARM, ARM64 (AArch64), MIPS, PowerPC, RISC-V, entre autres. Dans le cadre de cette formation, nous travaillons exclusivement en x86-64, mais si vous analysez un jour un firmware ARM embarqué dans un binaire ELF, le désassembleur d'ImHex le gère nativement.

---

## Accéder au désassembleur

Le désassembleur est accessible via le menu **View → Disassembler** (le nom exact peut varier légèrement selon la version d'ImHex). Un panneau s'ouvre avec plusieurs paramètres de configuration.

### Configuration requise

Avant de lancer le désassemblage, vous devez spécifier :

**L'architecture.** Sélectionnez `x86-64` (parfois libellé `X86` avec le mode `64-bit`) pour les binaires ELF que nous analysons. Un mauvais choix d'architecture produit un désassemblage incohérent — les octets seront décodés comme des instructions d'une autre architecture, avec des mnémoniques qui n'ont aucun rapport avec le code réel.

**L'offset de début.** L'adresse dans le fichier à partir de laquelle ImHex commence à décoder les instructions. C'est typiquement le début de la section `.text` — vous pouvez récupérer cet offset depuis votre pattern ELF (le champ `sh_offset` du Section Header dont le type est `SHT_PROGBITS` et les flags incluent `execinstr`), ou depuis `readelf -S`.

**La taille (ou l'offset de fin).** Le nombre d'octets à désassembler. Spécifiez la taille de la section `.text` (champ `sh_size` du Section Header correspondant) pour désassembler tout le code, ou une taille plus petite si vous ne vous intéressez qu'à une zone précise.

**L'adresse de base.** L'adresse virtuelle correspondant à l'offset de début. Cette valeur est importante pour que les adresses affichées dans le désassemblage correspondent aux adresses virtuelles que vous voyez dans `readelf`, Ghidra ou GDB. Pour `.text`, c'est le champ `sh_addr` du Section Header. Si vous ne la spécifiez pas (ou la laissez à 0), les adresses affichées seront des offsets relatifs plutôt que des adresses virtuelles — ce qui reste lisible, mais complique la corrélation avec d'autres outils.

### Lancer le désassemblage

Une fois les paramètres renseignés, cliquez sur le bouton de lancement (souvent **Disassemble** ou une icône ▶). ImHex décode les octets et affiche le résultat dans le panneau sous forme de listing : chaque ligne montre l'adresse, les octets bruts de l'instruction et le mnémonique assembleur avec ses opérandes.

```
0x00401040    55                  push   rbp
0x00401041    48 89 E5            mov    rbp, rsp
0x00401044    48 83 EC 20         sub    rsp, 0x20
0x00401048    89 7D EC            mov    dword [rbp-0x14], edi
0x0040104B    48 89 75 E0         mov    qword [rbp-0x20], rsi
0x0040104F    ...
```

La syntaxe utilisée est celle de **Intel** par défaut dans Capstone — la même que celle que nous utilisons dans cette formation (opérande destination à gauche, source à droite). Si vous préférez la syntaxe AT&T, une option de configuration permet de basculer.

---

## Navigation synchronisée avec la vue hexadécimale

L'intérêt majeur du désassembleur intégré par rapport à un outil externe est la **synchronisation bidirectionnelle** avec la vue hexadécimale.

**Du hex vers le désassemblage.** Quand vous cliquez sur un octet dans la vue hexadécimale (à condition qu'il se trouve dans la plage désassemblée), l'instruction correspondante est mise en surbrillance dans le panneau désassembleur. Vous voyez immédiatement à quelle instruction appartient l'octet que vous inspectez.

**Du désassemblage vers le hex.** Inversement, quand vous cliquez sur une instruction dans le panneau désassembleur, la vue hexadécimale saute aux octets correspondants et les sélectionne. Vous voyez les octets bruts de l'instruction surlignés dans leur contexte hexadécimal.

Cette synchronisation est précieuse dans plusieurs situations :

- Vous avez trouvé un opcode suspect lors d'une recherche hexadécimale (section 6.8). Vous cliquez dessus dans la vue hex et le désassembleur vous montre l'instruction complète — est-ce un vrai `syscall`, ou des octets `0F 05` qui font partie d'un immédiat dans une instruction plus longue ?  
- Vous lisez le désassemblage et vous repérez un `call` vers une adresse intéressante. Vous cliquez sur l'instruction et la vue hex vous montre les octets exacts — utile si vous prévoyez de patcher cet appel.  
- Vous voulez modifier un saut conditionnel. Le désassembleur vous montre l'instruction `jz` (opcode `74 XX`) ou `jnz` (opcode `75 XX`). Vous cliquez dessus, la vue hex sélectionne l'opcode, et vous pouvez le modifier sur place.

---

## Cas d'usage pratiques

### Vérifier un résultat de recherche d'opcodes

En section 6.8, nous avons cherché le motif `0F 05` pour localiser les `syscall`. Supposons que la recherche retourne trois occurrences. Pour chaque occurrence :

1. Cliquez sur le résultat dans la vue hexadécimale.  
2. Le désassembleur met en surbrillance l'instruction correspondante.  
3. Si l'instruction affichée est bien `syscall`, c'est un vrai hit.  
4. Si le désassembleur montre une autre instruction dont les octets contiennent `0F 05` comme partie d'un opérande, c'est un faux positif.

Ce processus de vérification prend quelques secondes par occurrence — bien plus rapide que d'ouvrir `objdump` et de chercher l'offset manuellement.

### Inspecter le code autour d'une chaîne référencée

Vous avez trouvé la chaîne `"Access denied"` dans `.rodata` via la recherche de chaînes. Vous voulez savoir quel code la référence. La démarche :

1. Notez l'adresse virtuelle de la chaîne (visible dans votre pattern ELF ou via `readelf`).  
2. Cherchez cette adresse dans la vue hexadécimale de `.text` : un `lea` (`48 8D 05 ...` ou `48 8D 3D ...`) suivi de l'offset relatif vers la chaîne.  
3. Cliquez sur le résultat et le désassembleur vous montre l'instruction `lea rdi, [rip+0x...]` — le chargement de l'adresse de la chaîne comme premier argument d'un `call` (probablement `printf` ou `puts`).

Cette technique est un raccourci pour les **cross-references** (XREF) que nous verrons dans Ghidra au chapitre 8. Elle est moins systématique (vous cherchez manuellement l'adresse) mais ne nécessite aucun import dans un outil externe.

### Préparer un patch binaire

Vous savez, grâce à l'analyse dynamique avec GDB (chapitre 11) ou à la lecture du désassemblage dans Ghidra (chapitre 8), qu'une instruction `jz` (saut si zéro) à un certain offset conditionne l'acceptation ou le refus d'un serial. Vous voulez la patcher en `jnz` (saut si non zéro).

1. Naviguez à l'offset dans la vue hexadécimale.  
2. Le désassembleur confirme que c'est bien un `jz` — l'opcode `74` suivi d'un déplacement relatif d'un octet.  
3. Modifiez directement l'octet `74` en `75` dans la vue hex.  
4. Le désassembleur se met à jour et affiche maintenant `jnz` — confirmation visuelle que le patch est correct.  
5. Sauvegardez le fichier modifié.

Ce workflow de patching dans ImHex sera détaillé au chapitre 21. Le désassembleur intégré est ce qui rend l'opération sûre : vous voyez l'instruction avant et après la modification, sans quitter l'éditeur.

---

## Limites du désassembleur intégré

Le désassembleur d'ImHex est un outil de **vérification ponctuelle**, pas un outil d'analyse complète. Comprendre ses limites est essentiel pour ne pas lui demander ce qu'il ne peut pas fournir.

### Désassemblage linéaire, pas récursif

Capstone utilise un algorithme de **linear sweep** : il décode les octets séquentiellement à partir du point de départ, instruction après instruction. Il ne suit pas les sauts, ne résout pas les appels, et ne sait pas distinguer le code des données embarquées dans `.text` (jump tables, constantes littérales insérées par le compilateur).

Conséquence : si une zone de données se trouve au milieu de `.text` (ce qui arrive avec GCC quand il insère des literal pools ou des tables d'adresses pour les `switch`), le désassembleur les interprétera comme des instructions — produisant des mnémoniques absurdes. Ghidra, IDA et radare2 utilisent des algorithmes **récursifs** (recursive descent) qui suivent le flux de contrôle et évitent ce piège. Le désassembleur d'ImHex ne le peut pas.

### Pas de graphe de flux de contrôle

ImHex ne construit pas de CFG (Control Flow Graph). Vous ne verrez pas de diagramme de blocs basiques, pas de flèches montrant les branchements, pas de coloration des boucles et des conditions. Le résultat est un listing linéaire brut — fonctionnel pour lire quelques instructions, mais inadapté pour comprendre la logique d'une fonction complexe.

### Pas de résolution de symboles

Le désassembleur affiche les adresses brutes dans les opérandes. Un `call 0x401120` ne vous dit pas que c'est un appel à `printf@plt` — vous devez faire la correspondance vous-même en consultant la table des symboles (via `nm`, `readelf -s`, ou votre pattern `.hexpat`). Ghidra et IDA résolvent automatiquement ces symboles et annotent le listing.

### Pas de décompilation

ImHex ne produit pas de pseudo-code C. Le désassembleur s'arrête au niveau des mnémoniques assembleur. Si vous avez besoin de voir le code décompilé, Ghidra (chapitre 8) est l'outil approprié.

### Pas de mise à jour dynamique continue

Le panneau désassembleur ne se rafraîchit pas automatiquement lorsque vous modifiez des octets dans la vue hexadécimale. Après un patch, vous devez relancer le désassemblage pour voir le résultat mis à jour. C'est une friction mineure dans un workflow de patching, mais à garder en tête.

---

## Quand utiliser le désassembleur d'ImHex vs un outil dédié

Le tableau suivant résume les situations où le désassembleur intégré est le bon choix, et celles où un outil dédié est préférable.

| Situation | ImHex | Outil dédié |  
|---|---|---|  
| Vérifier qu'un octet trouvé par recherche est bien un opcode | ✅ Immédiat, pas de changement de contexte | Disproportionné |  
| Confirmer l'instruction avant/après un patch | ✅ Synchronisé avec la vue hex | Possible mais plus lent |  
| Inspecter 5–10 instructions autour d'un offset précis | ✅ Rapide et suffisant | Possible mais plus lent |  
| Comprendre la logique d'une fonction de 50+ lignes | ❌ Listing linéaire trop limité | ✅ Ghidra/IDA avec CFG |  
| Suivre les appels et les cross-references | ❌ Pas de résolution de symboles | ✅ Ghidra/IDA/radare2 |  
| Analyser un binaire complet (toutes les fonctions) | ❌ Pas conçu pour ça | ✅ Désassembleur dédié |  
| Examiner un binaire ARM/MIPS/RISC-V | ✅ Capstone multi-arch | ✅ Aussi supporté par les dédiés |

La règle est simple : si vous avez besoin de voir **quelques instructions** pour vérifier ou préparer une action dans ImHex, utilisez le désassembleur intégré. Si vous avez besoin de **comprendre un algorithme**, utilisez Ghidra ou radare2.

---

## Résumé

Le désassembleur intégré d'ImHex, basé sur Capstone, transforme les octets de la section `.text` en instructions assembleur lisibles directement dans l'éditeur hexadécimal. Sa force réside dans la synchronisation bidirectionnelle avec la vue hex — un clic dans l'un met à jour l'autre — qui accélère la vérification de résultats de recherche, la préparation de patches et l'inspection ponctuelle de code. Ses limites sont celles d'un désassembleur linéaire sans analyse de flux : pas de CFG, pas de résolution de symboles, pas de décompilation, pas de gestion des données embarquées dans le code. C'est un outil de proximité, pas un substitut aux désassembleurs des chapitres 7–9 — et c'est précisément ce positionnement qui le rend si utile dans le workflow quotidien d'ImHex.

---


⏭️ [Appliquer des règles YARA depuis ImHex (pont vers l'analyse malware)](/06-imhex/10-regles-yara.md)
