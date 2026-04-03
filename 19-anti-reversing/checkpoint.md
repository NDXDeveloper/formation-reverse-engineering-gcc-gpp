🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 19

## Objectif

Identifier **toutes** les protections du binaire `anti_reverse_all_checks`, les contourner une par une, et retrouver le mot de passe permettant d'obtenir le flag.

Ce checkpoint mobilise l'ensemble des compétences du chapitre :

| Section | Compétence évaluée |  
|---|---|  
| 19.1 | Détecter le stripping et adapter sa stratégie |  
| 19.2 | Détecter un éventuel packing et décompresser |  
| 19.5–19.6 | Identifier les protections compilateur (canary, NX, PIE, RELRO) |  
| 19.7 | Identifier et contourner les détections de débogueur |  
| 19.8 | Identifier et contourner les contre-mesures aux breakpoints |  
| 19.9 | Appliquer le workflow de triage systématique avec `checksec` |

---

## Binaire cible

```
binaries/ch19-anti-reversing/build/anti_reverse_all_checks
```

Ce binaire a été compilé avec `make anti_debug`. Il combine toutes les protections applicatives implémentées dans `anti_reverse.c`, est compilé en `-O2` et a été strippé. C'est la variante la plus résistante du chapitre.

Vous ne devez **pas** consulter le code source `anti_reverse.c` pour résoudre ce checkpoint. L'objectif est de retrouver les protections et le mot de passe par l'analyse seule.

---

## Travail demandé

### Phase 1 — Triage et fiche de protections

Appliquer le workflow de triage de la section 19.9 sur `anti_reverse_all_checks` :

1. Lancer `file` sur le binaire. Noter le format, l'architecture, le linkage, le statut du stripping.  
2. Lancer `checksec`. Noter chaque ligne : RELRO, canary, NX, PIE.  
3. Inspecter les imports dynamiques avec `nm -D`. Lister les fonctions suspectes qui trahissent des protections anti-RE.  
4. Lancer `strings` et `strings | wc -l`. Chercher des signatures de packer, des chemins procfs, des chaînes de la logique métier.  
5. Vérifier l'entropie avec `binwalk -E` et la structure des sections avec `readelf -S`.

Produire une **fiche de protections** synthétique (texte libre, quelques lignes) qui liste :

- Toutes les protections compilateur détectées  
- Toutes les protections applicatives suspectées  
- La stratégie d'analyse envisagée (quels outils, quels contournements)

### Phase 2 — Contournement des protections anti-debug

En utilisant les techniques de votre choix (GDB, Frida, `LD_PRELOAD`, patching, ou une combinaison) :

1. Identifier chaque check anti-debug présent dans le binaire. Pour chacun, noter l'adresse (ou l'offset) de la fonction et la technique utilisée.  
2. Contourner chaque check individuellement. Documenter la méthode choisie.  
3. Atteindre le prompt `"Mot de passe : "` — preuve que tous les checks ont été passés.

**Indice** : les variantes à protection unique (`anti_reverse_ptrace_only`, `anti_reverse_timing_only`, `anti_reverse_procfs_only`, `anti_reverse_int3_only`) peuvent servir de bac à sable pour tester chaque contournement en isolation avant de les combiner.

### Phase 3 — Extraction du mot de passe

Une fois les protections anti-debug contournées :

1. Localiser la routine de vérification du mot de passe dans le désassemblage (Ghidra, objdump, ou directement dans GDB).  
2. Comprendre le mécanisme d'encodage/décodage du mot de passe attendu.  
3. Extraire le mot de passe par l'une des méthodes suivantes :  
   - Analyse statique : reconstruire l'algorithme de décodage et le reproduire  
   - Analyse dynamique : poser un breakpoint (hardware !) sur la comparaison et lire la valeur attendue en mémoire  
   - Scripting : écrire un script qui automatise l'extraction

4. Valider le mot de passe : le programme doit afficher le flag `CTF{...}`.

---

## Livrables

Le checkpoint est validé quand vous disposez de :

1. **La fiche de protections** — Le résumé complet des protections identifiées lors du triage.  
2. **Le journal de contournement** — Pour chaque protection anti-debug, la technique utilisée pour la neutraliser (quelques lignes par protection suffisent : nom de la protection, adresse/offset, méthode de contournement, commande ou script utilisé).  
3. **Le mot de passe et le flag** — La preuve que le binaire a été entièrement résolu.  
4. **Optionnel** — Un script (GDB Python, Frida JS, ou shell) qui automatise le contournement complet et l'extraction du mot de passe en une seule exécution.

---

## Critères de réussite

| Critère | Attendu |  
|---|---|  
| Triage complet | Les 5 étapes du workflow ont été appliquées |  
| Protections compilateur | RELRO, canary, NX, PIE correctement identifiés |  
| Protections applicatives | Toutes les techniques anti-debug listées et localisées |  
| Contournement | Chaque check est neutralisé par une méthode documentée |  
| Mot de passe | Le flag est obtenu par soumission du bon mot de passe |

---

## Conseils

- Commencez par le triage. Ne touchez pas à GDB avant d'avoir une fiche de protections complète.  
- Les imports dynamiques (`nm -D`) racontent la moitié de l'histoire, même sur un binaire strippé.  
- Face à plusieurs checks combinés, un script Frida unique qui hooke toutes les fonctions suspectes est souvent plus rapide que des contournements individuels dans GDB.  
- Le mot de passe n'est pas stocké en clair dans le binaire. Cherchez un encodage réversible.  
- Les hardware breakpoints sont vos alliés sur ce binaire. Utilisez `hbreak` dans GDB.  
- En cas de blocage, la variante `anti_reverse_debug` (avec symboles, sans protections anti-debug) permet de comprendre la structure du programme avant de s'attaquer à la version durcie.

---


⏭️ [Chapitre 20 — Décompilation et reconstruction du code source](/20-decompilation/README.md)
