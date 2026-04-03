🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 19 — Anti-reversing et protections compilateur

> 🔒 **Objectif** : Comprendre, identifier et contourner les mécanismes qui rendent le reverse engineering plus difficile — qu'ils soient appliqués volontairement par le développeur, automatiquement par le compilateur, ou ajoutés par un outil tiers.

---

## Pourquoi ce chapitre existe

Jusqu'ici, les binaires que nous avons analysés étaient relativement coopératifs. Même compilés avec des optimisations élevées, ils ne cherchaient pas activement à nous empêcher de les comprendre. Dans la réalité, c'est rarement le cas.

Les développeurs de logiciels commerciaux, les auteurs de malware et les concepteurs de challenges CTF emploient tous — pour des raisons très différentes — des techniques destinées à ralentir, tromper ou bloquer l'analyste. Ces techniques se répartissent en plusieurs catégories :

- **La suppression d'information** : retirer les symboles, les informations de débogage, tout ce qui facilite la lecture du binaire. C'est la première barrière, la plus simple et la plus répandue.  
- **La compression et le packing** : transformer le binaire en une enveloppe compressée qui se décompresse en mémoire à l'exécution. Le fichier sur disque ne ressemble plus du tout au code réel.  
- **L'obfuscation du flux de contrôle** : réécrire la structure logique du programme pour que le graphe de contrôle devienne un labyrinthe incompréhensible, sans modifier le comportement fonctionnel.  
- **Les protections mémoire du compilateur et du système** : stack canaries, ASLR, PIE, NX, RELRO — des mécanismes qui ne visent pas directement le reverse engineer mais qui compliquent significativement l'exploitation et l'analyse dynamique.  
- **La détection active de l'analyste** : vérifications anti-débogueur, détection de breakpoints, contrôles d'intégrité — le binaire se défend en temps réel contre toute tentative d'observation.

Comprendre ces protections est une compétence fondamentale. Non seulement pour les contourner quand l'analyse le justifie (audit de sécurité, analyse de malware, interopérabilité), mais aussi pour les reconnaître immédiatement lors du triage et adapter sa stratégie en conséquence. Un analyste qui ne sait pas identifier un binaire packé perdra des heures à tenter de désassembler du code compressé. Un analyste qui ne reconnaît pas un contrôle anti-`ptrace` se demandera pourquoi GDB refuse de s'attacher au processus.

## Ce que vous allez apprendre

Ce chapitre couvre les protections que vous rencontrerez le plus fréquemment sur des binaires ELF compilés avec GCC/G++, dans un ordre progressif :

1. **Stripping** — La suppression des symboles avec `strip`, comment la détecter, et ce qu'on perd (et ce qu'on ne perd pas).  
2. **Packing avec UPX** — Le packer le plus courant, comment il fonctionne, comment le détecter et comment décompresser le binaire original.  
3. **Obfuscation du flux de contrôle** — Le Control Flow Flattening, le bogus control flow, et les transformations qui rendent le graphe de fonctions illisible.  
4. **Obfuscation via LLVM** — Les passes d'obfuscation comme Hikari et O-LLVM, qui opèrent au niveau du compilateur et produisent des patterns reconnaissables.  
5. **Protections compilateur et système** — Stack canaries (`-fstack-protector`), ASLR, PIE, NX : leur fonctionnement interne et leur impact concret sur l'analyse.  
6. **RELRO** — La distinction entre Partial et Full RELRO, et ce que cela implique pour la table GOT/PLT et les possibilités de patching dynamique.  
7. **Détection de débogueur** — Les techniques classiques (`ptrace`, timing checks, lecture de `/proc/self/status`) et comment les neutraliser.  
8. **Contre-mesures aux breakpoints** — Le self-modifying code, le scanning d'instructions `int3`, et les protections qui ciblent directement les outils d'analyse dynamique.  
9. **Audit complet avec `checksec`** — La démarche systématique pour inventorier toutes les protections d'un binaire avant de commencer l'analyse.

## Prérequis

Ce chapitre s'appuie sur l'ensemble des compétences acquises dans les parties précédentes. Avant de le commencer, vous devez être à l'aise avec :

- Le désassemblage et la lecture de code assembleur x86-64 (Partie I, Chapitre 3)  
- Les outils d'inspection binaire : `readelf`, `objdump`, `checksec`, `file`, `strings` (Chapitre 5)  
- L'analyse statique avec Ghidra (Chapitre 8)  
- Le débogage avec GDB et ses extensions (Chapitres 11–12)  
- L'instrumentation dynamique avec Frida (Chapitre 13)  
- La structure d'un binaire ELF : sections, segments, PLT/GOT (Chapitre 2)

## Philosophie de l'approche

Chaque section suit le même schéma en trois temps :

1. **Comprendre** — Comment la protection fonctionne techniquement, ce qu'elle modifie dans le binaire ou dans l'environnement d'exécution.  
2. **Détecter** — Les signatures, les indicateurs et les outils qui permettent d'identifier rapidement la présence de cette protection.  
3. **Contourner** — Les techniques et les outils pour neutraliser la protection et poursuivre l'analyse.

Cette approche n'est pas un encouragement au piratage. Rappelons que le cadre légal et éthique du reverse engineering a été posé au Chapitre 1 (section 1.2). Les techniques présentées ici sont celles utilisées quotidiennement par les analystes en sécurité, les chercheurs de vulnérabilités et les participants de CTF.

## Binaires d'entraînement

Les sources et le Makefile de ce chapitre se trouvent dans `binaries/ch19-anti-reversing/`. Le Makefile produit **22 variantes** dans `binaries/ch19-anti-reversing/build/`, chacune isolant une protection spécifique :

- **`anti_reverse.c`** — Un crackme protégé par plusieurs couches anti-debug (ptrace, timing, /proc, int3 scan, checksum), activables individuellement via des macros de compilation. Le mot de passe est stocké encodé en XOR.  
- **`vuln_demo.c`** — Un programme volontairement vulnérable (buffer overflow) compilé avec différentes combinaisons de protections compilateur (canary, PIE, NX, RELRO) pour observer leur effet concret.

```bash
cd binaries/ch19-anti-reversing/  
make all        # compile les 22 variantes  
make list       # affiche la description de chaque cible  
make checksec   # lance checksec sur toutes les variantes  
```

Le checkpoint final vous demandera d'identifier toutes les protections du binaire `anti_reverse_all_checks` et de les contourner une par une pour retrouver le mot de passe.

## Plan du chapitre

| Section | Sujet |  
|---|---|  
| 19.1 | Stripping (`strip`) et détection |  
| 19.2 | Packing avec UPX — détecter et décompresser |  
| 19.3 | Obfuscation de flux de contrôle (Control Flow Flattening, bogus control flow) |  
| 19.4 | Obfuscation via LLVM (Hikari, O-LLVM) — reconnaître les patterns |  
| 19.5 | Stack canaries (`-fstack-protector`), ASLR, PIE, NX |  
| 19.6 | RELRO : Partial vs Full et impact sur la table GOT/PLT |  
| 19.7 | Techniques de détection de débogueur (`ptrace`, timing checks, `/proc/self/status`) |  
| 19.8 | Contre-mesures aux breakpoints (self-modifying code, int3 scanning) |  
| 19.9 | Inspecter l'ensemble des protections avec `checksec` avant toute analyse |

---


⏭️ [Stripping (`strip`) et détection](/19-anti-reversing/01-stripping-detection.md)
