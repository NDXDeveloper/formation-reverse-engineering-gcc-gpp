🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 21 — Reverse d'un programme C simple (keygenme)

> 🎯 **Objectif du chapitre** : mener de bout en bout le reverse engineering d'un crackme écrit en C pur, compilé avec GCC à différents niveaux d'optimisation. À l'issue de ce chapitre, vous serez capable de localiser une routine de vérification, comprendre sa logique, la contourner par patching binaire, la résoudre automatiquement par exécution symbolique, et produire un keygen fonctionnel.

---

## Contexte

Ce chapitre est le premier des cas pratiques de la **Partie V**. Il mobilise l'ensemble des compétences acquises dans les parties précédentes — analyse statique (Partie II), analyse dynamique (Partie III) et techniques avancées (Partie IV) — sur une cible volontairement accessible : un programme C simple qui demande une clé de licence à l'utilisateur et vérifie sa validité.

Le binaire `keygenme` est un scénario classique du monde des CTF et de l'apprentissage du RE. L'utilisateur lance le programme, saisit une chaîne de caractères, et le programme répond par un message de succès ou d'échec. Derrière cette mécanique en apparence triviale se cache une opportunité pédagogique riche : chaque étape du reverse — du triage initial à l'écriture du keygen — illustre une compétence fondamentale que vous retrouverez sur des cibles bien plus complexes.

## Pourquoi commencer par un keygenme ?

Le keygenme est au reverse engineer ce que le « Hello, World! » est au développeur : un exercice canonique qui permet de valider les bases dans un cadre maîtrisé. Sa simplicité apparente est trompeuse, car il force à enchaîner méthodiquement toutes les phases d'une analyse complète :

- **Reconnaissance** : que contient ce binaire ? Quel compilateur, quelles protections, quelles chaînes de caractères révélatrices ?  
- **Analyse statique** : où se situe la logique de vérification ? Quelles transformations subit l'entrée utilisateur ? Quel est le prédicat de succès ?  
- **Analyse dynamique** : peut-on observer la comparaison en temps réel ? Quelles valeurs transitent dans les registres au moment critique ?  
- **Exploitation** : peut-on contourner la vérification (patching) ou la résoudre (keygen, exécution symbolique) ?

Un analyste qui maîtrise ce workflow sur un keygenme `-O0` avec symboles est prêt à affronter le même binaire en `-O2` strippé, puis des cibles de complexité croissante.

## Le binaire cible

Le dossier `binaries/ch21-keygenme/` contient le source `keygenme.c` et un `Makefile` qui produit **cinq variantes** du même programme :

| Variante | Optimisation | Symboles | Difficulté |  
|---|---|---|---|  
| `keygenme_O0` | `-O0` | oui | ⭐ |  
| `keygenme_O2` | `-O2` | oui | ⭐⭐ |  
| `keygenme_O3` | `-O3` | oui | ⭐⭐⭐ |  
| `keygenme_strip` | `-O0` | non (`strip`) | ⭐⭐ |  
| `keygenme_O2_strip` | `-O2` | non (`strip`) | ⭐⭐⭐⭐ |

Cette progression permet d'observer concrètement l'impact des flags de compilation étudiés au chapitre 2 (`-O0` à `-O3`, `-g`, `-s`) et des techniques anti-reversing vues au chapitre 19 (stripping). On recommande de commencer l'analyse avec `keygenme_O0` pour acquérir la compréhension de la logique, puis de vérifier que l'on retrouve les mêmes conclusions sur les variantes optimisées et strippées.

## Ce que vous allez apprendre

Ce chapitre couvre la totalité du cycle de reverse engineering appliqué à une cible C compilée avec GCC :

- **Section 21.1** — Triage rapide du binaire : `file`, `strings`, `readelf`, inspection des sections. Vous appliquerez le workflow des 5 premières minutes vu au chapitre 5.  
- **Section 21.2** — Inventaire des protections avec `checksec` : PIE, NX, canary, RELRO. Vous saurez ce que le compilateur a activé et ce que cela implique pour l'analyse.  
- **Section 21.3** — Localisation de la routine de vérification par approche top-down dans Ghidra, en partant de `main()` et en suivant les cross-references vers la fonction critique.  
- **Section 21.4** — Compréhension des sauts conditionnels (`jz`/`jnz`) qui séparent le chemin « clé valide » du chemin « clé invalide ». C'est le cœur de tout crackme.  
- **Section 21.5** — Analyse dynamique avec GDB : poser un breakpoint sur la comparaison, observer les registres, capturer la clé attendue en mémoire.  
- **Section 21.6** — Patching binaire avec ImHex : inverser un octet de saut conditionnel pour que le programme accepte n'importe quelle clé. Une modification chirurgicale de un ou deux octets.  
- **Section 21.7** — Résolution automatique avec angr : écrire un script d'exécution symbolique qui trouve la bonne clé sans comprendre manuellement l'algorithme.  
- **Section 21.8** — Écriture d'un keygen en Python avec `pwntools` : reproduire l'algorithme de validation pour générer des clés valides à la demande.

## Prérequis

Avant d'aborder ce chapitre, assurez-vous d'être à l'aise avec :

- Le **triage rapide** d'un binaire ELF (chapitre 5)  
- La **lecture d'un désassemblage** x86-64 et les conventions d'appel System V (chapitre 3)  
- Les **bases de Ghidra** : navigation dans le CodeBrowser, renommage, XREF (chapitre 8)  
- Les **commandes fondamentales de GDB** : `break`, `run`, `x`, `info registers` (chapitre 11)  
- La notion de **patching binaire** et l'utilisation d'ImHex (chapitre 6)  
- Les **principes de l'exécution symbolique** avec angr (chapitre 18)  
- L'utilisation de **pwntools** pour interagir avec un binaire (chapitre 11, section 9)

Si l'un de ces points vous semble flou, n'hésitez pas à revenir sur le chapitre correspondant. Chaque section de ce chapitre 21 indiquera les rappels nécessaires.

## Méthodologie suivie

L'analyse de ce chapitre suit un parcours délibérément linéaire, du plus passif au plus intrusif :

```
Triage (passif)
  └─→ Protections (passif)
        └─→ Analyse statique dans Ghidra (passif)
              └─→ Analyse dynamique dans GDB (actif, non destructif)
                    └─→ Patching binaire (actif, destructif)
                          └─→ Résolution automatique avec angr (actif)
                                └─→ Keygen (produit final)
```

Cette progression n'est pas arbitraire. En RE, on commence toujours par ce qui ne modifie pas la cible et ne risque pas de fausser l'analyse. Le triage et l'analyse statique permettent de formuler des hypothèses ; l'analyse dynamique les confirme ; le patching et le keygen exploitent la compréhension acquise.

## Conventions pour ce chapitre

- Les commandes shell sont préfixées par `$`, les commandes GDB par `(gdb)`.  
- Sauf mention contraire, les exemples utilisent la variante `keygenme_O0` (la plus lisible).  
- La syntaxe assembleur est **Intel** (`-M intel` pour `objdump`, option par défaut dans Ghidra).  
- Les adresses affichées peuvent différer des vôtres si PIE est activé et ASLR non désactivé — c'est normal, les offsets relatifs restent identiques.

---

## Plan du chapitre

- 21.1 — [Analyse statique complète du binaire (triage, strings, sections)](/21-keygenme/01-analyse-statique.md)  
- 21.2 — [Inventaire des protections avec `checksec`](/21-keygenme/02-checksec-protections.md)  
- 21.3 — [Localisation de la routine de vérification (approche top-down)](/21-keygenme/03-localisation-routine.md)  
- 21.4 — [Comprendre les sauts conditionnels (`jz`/`jnz`) dans le contexte du crackme](/21-keygenme/04-sauts-conditionnels-crackme.md)  
- 21.5 — [Analyse dynamique : tracer la comparaison avec GDB](/21-keygenme/05-analyse-dynamique-gdb.md)  
- 21.6 — [Patching binaire : inverser un saut directement dans le binaire (avec ImHex)](/21-keygenme/06-patching-imhex.md)  
- 21.7 — [Résolution automatique avec angr](/21-keygenme/07-resolution-angr.md)  
- 21.8 — [Écriture d'un keygen en Python avec `pwntools`](/21-keygenme/08-keygen-pwntools.md)  
- 🎯 Checkpoint — [Produire un keygen fonctionnel pour les 3 variantes du binaire](/21-keygenme/checkpoint.md)

⏭️ [Analyse statique complète du binaire (triage, strings, sections)](/21-keygenme/01-analyse-statique.md)
