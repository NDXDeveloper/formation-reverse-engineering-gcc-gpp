🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Tracer l'exécution complète de `keygenme_O0` avec GEF, capturer le moment de la comparaison

> **Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg**  
> **Partie III — Analyse Dynamique**

---

## Objectif

Ce checkpoint valide la maîtrise des trois compétences centrales du chapitre : savoir lire le contexte automatique d'une extension GDB, utiliser les commandes spécifiques pour inspecter l'état du programme, et combiner ces outils pour localiser une logique de vérification dans un binaire. Le scénario est volontairement simple — un binaire compilé sans optimisation et avec symboles — afin de se concentrer sur la prise en main des extensions plutôt que sur la difficulté du reverse engineering lui-même.

Le binaire `keygenme_O0` est un programme qui demande un mot de passe à l'utilisateur et affiche un message de succès ou d'échec. L'objectif est de tracer son exécution avec GEF, identifier la fonction de comparaison, capturer les deux chaînes comparées et comprendre le flux de contrôle qui mène à l'acceptation ou au rejet du mot de passe.

---

## Prérequis

- GEF installé et fonctionnel (section 12.1), vérifiable avec `gdb-gef -q -batch -ex "gef help"`  
- Le binaire `keygenme_O0` compilé et présent dans `binaries/ch12-keygenme/`  
- Un terminal d'au moins 120 colonnes et 40 lignes pour un affichage confortable du contexte

```bash
cd binaries/ch12-keygenme/  
make keygenme_O0  
file keygenme_O0  
```

La commande `file` doit confirmer un binaire ELF 64-bit, non strippé (« not stripped »). La présence des symboles est essentielle pour ce checkpoint — les variantes strippées seront abordées dans les chapitres ultérieurs.

---

## Étape 1 — Reconnaissance initiale depuis GEF

Lancer GDB avec GEF sur le binaire :

```bash
gdb-gef -q ./keygenme_O0
```

La première action à l'intérieur de GEF est de vérifier les protections du binaire. Cela établit le contexte de sécurité avant toute analyse dynamique :

```
gef➤ checksec
```

La sortie indique l'état de NX, PIE, RELRO, des canaries et de Fortify. Pour un binaire compilé avec les options par défaut de GCC récent, on s'attend à voir PIE activé, NX activé, et probablement des canaries. Ces informations ne sont pas immédiatement nécessaires pour tracer la comparaison, mais elles font partie de la routine de triage que tout reverse engineer doit automatiser (section 5.7).

Ensuite, consulter les fonctions disponibles dans le binaire :

```
gef➤ info functions
```

Puisque le binaire n'est pas strippé, GDB liste toutes les fonctions avec leurs noms. On cherche les fonctions du programme (pas celles de la libc) : `main`, et potentiellement des fonctions auxiliaires comme `check_password`, `verify`, `validate` ou similaire. La présence de `strcmp`, `strncmp` ou `memcmp` dans les imports (visibles via la table PLT) est un indice fort sur le mécanisme de comparaison.

```
gef➤ got
```

La commande `got` de GEF liste les fonctions importées. Si `strcmp@plt` apparaît, on sait que le programme utilise une comparaison de chaînes standard — c'est notre cible principale.

---

## Étape 2 — Poser les breakpoints stratégiques

Deux breakpoints sont nécessaires pour cette analyse : un sur `main` pour observer l'initialisation, et un sur la fonction de comparaison pour capturer le moment critique.

```
gef➤ break main  
gef➤ break strcmp  
```

Si l'étape 1 a révélé une fonction de vérification dédiée (par exemple `check_password`), poser un breakpoint supplémentaire dessus :

```
gef➤ break check_password
```

Lancer le programme :

```
gef➤ run
```

GDB s'arrête sur `main`. Le contexte GEF s'affiche automatiquement : registres, désassemblage et pile. C'est le premier contact avec le contexte en situation réelle.

---

## Étape 3 — Lire le contexte à l'entrée de `main`

À l'arrêt sur `main`, le contexte GEF affiche plusieurs informations exploitables immédiatement.

**Section registres.** `RDI` contient `argc` (le nombre d'arguments en ligne de commande) et `RSI` contient `argv` (le pointeur vers le tableau d'arguments). Avec le déréférencement récursif de GEF, `RSI` montre la chaîne du chemin de l'exécutable. Ces valeurs confirment que le programme a été lancé correctement.

**Section code.** Le désassemblage montre le prologue de `main` (`push rbp ; mov rbp, rsp ; sub rsp, ...`) suivi des premières instructions de la fonction. En parcourant visuellement les instructions affichées, on peut repérer les appels (`call`) vers les fonctions d'entrée/sortie (`puts@plt`, `printf@plt`, `scanf@plt`, `fgets@plt`) et la comparaison (`strcmp@plt`).

**Section pile.** À l'entrée de `main`, la pile contient l'adresse de retour vers `__libc_start_call_main` et les données du setup initial. GEF déréférence l'adresse de retour et affiche le nom de la fonction appelante, confirmant le chemin normal d'entrée dans le programme.

Utiliser `xinfo` pour vérifier la nature d'une adresse intéressante vue dans les registres :

```
gef➤ xinfo $rsi
```

La sortie confirme que l'adresse pointe vers la pile (région `[stack]`) et correspond à `argv`.

---

## Étape 4 — Avancer jusqu'à la comparaison

Continuer l'exécution pour atteindre le point de comparaison :

```
gef➤ continue
```

Le programme affiche son prompt et attend une entrée. Taper un mot de passe quelconque — par exemple `test123` — et valider. GDB s'arrête sur le breakpoint `strcmp`.

C'est le moment central du checkpoint. Le contexte GEF affiche automatiquement l'état complet du programme au point de comparaison.

---

## Étape 5 — Capturer les arguments de la comparaison

À l'arrêt sur `strcmp`, les registres `RDI` et `RSI` contiennent les deux arguments de la comparaison (convention System V AMD64 : premier argument dans `RDI`, deuxième dans `RSI`).

Le contexte GEF affiche directement les valeurs déréférencées :

```
$rdi   : 0x00007fffffffe0b0  →  "test123"
$rsi   : 0x0000555555556004  →  "s3cr3t_k3y"
```

L'entrée utilisateur (`test123`) est dans `RDI`, et la chaîne attendue (`s3cr3t_k3y`) est dans `RSI` — ou l'inverse, selon l'implémentation. Le contexte de GEF rend ces deux valeurs lisibles sans aucune commande supplémentaire.

Pour confirmer et approfondir l'inspection, utiliser `xinfo` sur chaque adresse :

```
gef➤ xinfo $rdi  
gef➤ xinfo $rsi  
```

L'adresse pointée par `RDI` devrait se trouver dans la pile (`[stack]`) — c'est le buffer où l'entrée utilisateur a été stockée. L'adresse pointée par `RSI` devrait se trouver dans la section `.rodata` du binaire — c'est une constante compilée dans le programme. Cette distinction est significative : elle confirme que le mot de passe attendu est une chaîne statique intégrée au binaire, et non une valeur calculée dynamiquement.

Pour vérifier l'intégralité des deux chaînes au-delà de ce que le contexte affiche :

```
gef➤ dereference $rdi 4  
gef➤ dereference $rsi 4  
```

La commande `dereference` (l'équivalent GEF de `telescope` dans pwndbg) affiche plusieurs mots mémoire à partir de l'adresse, avec déréférencement récursif. Cela permet de voir si les chaînes sont plus longues que ce que le résumé du contexte montre.

---

## Étape 6 — Comprendre le flux de contrôle post-comparaison

Après avoir capturé les arguments, il faut comprendre ce que le programme fait du résultat de `strcmp`. Placer un breakpoint temporaire sur l'instruction qui suit le `call strcmp` dans la fonction appelante :

```
gef➤ finish
```

`finish` exécute jusqu'au retour de `strcmp`. Le contexte GEF s'affiche à nouveau, cette fois dans la fonction appelante, juste après le `call`. Le registre `RAX` contient la valeur de retour de `strcmp` : 0 si les chaînes sont identiques, une valeur non nulle sinon.

Le désassemblage dans la section code du contexte montre les instructions qui suivent. Le pattern typique est :

```nasm
call   strcmp@plt  
test   eax, eax        ; teste si EAX == 0  
jne    0x555555551xyz   ; saute si différent de zéro (échec)  
; ... code de succès ...
```

Ou la variante inverse :

```nasm
call   strcmp@plt  
test   eax, eax  
je     0x555555551xyz   ; saute si égal à zéro (succès)  
; ... code d'échec ...
```

Le contexte GEF montre l'instruction `test eax, eax` avec l'état des flags résultant. Si l'entrée ne correspondait pas au mot de passe attendu, `EAX` est non nul, le Zero Flag (`ZF`) est à 0, et le `jne` sera pris (ou le `je` ne sera pas pris). La section de désassemblage de GEF permet de suivre ce raisonnement visuellement.

Avancer d'une instruction pour exécuter le `test` :

```
gef➤ stepi
```

Le nouveau contexte montre les flags mis à jour. En consultant la section registres, on voit si `ZF` est actif ou non. C'est le flag qui détermine le résultat de la vérification.

Avancer encore d'une instruction pour atteindre le saut conditionnel :

```
gef➤ stepi
```

Le contexte montre l'instruction de saut. Pour vérifier le chemin que le programme va prendre, on peut utiliser la prédiction de GEF — la branche cible est annotée si les informations sont suffisantes — ou simplement lire l'état de `ZF` et appliquer la règle du saut (`jne` saute si `ZF == 0`, `je` saute si `ZF == 1`).

---

## Étape 7 — Forcer le chemin de succès

Pour confirmer la compréhension du flux de contrôle, on peut forcer le programme à emprunter le chemin de succès en modifiant le Zero Flag :

```
gef➤ edit-flags +zero       # si le jne doit être évité (ZF=1 → jne NOT TAKEN)
```

Ou inversement :

```
gef➤ edit-flags -zero       # si le je doit être forcé à ne pas sauter
```

Après la modification, continuer l'exécution :

```
gef➤ continue
```

Le programme devrait afficher le message de succès, confirmant que la compréhension du flux est correcte. Cette technique de modification de flags en live est un outil de validation fondamental en reverse engineering : elle permet de tester une hypothèse sur la logique du programme sans modifier le binaire sur disque.

---

## Étape 8 — Vérification croisée avec pwndbg

Pour consolider la maîtrise de la bascule entre extensions, relancer l'analyse avec pwndbg et observer les différences de présentation :

```bash
gdb-pwndbg -q ./keygenme_O0
```

```
pwndbg> break strcmp  
pwndbg> run  
Entrez le mot de passe : test123  
```

À l'arrêt sur `strcmp`, comparer le contexte pwndbg avec celui de GEF. Les différences notables à observer sont l'annotation des arguments de `strcmp` dans la section DISASM (pwndbg affiche les arguments déduits directement dans le désassemblage), la prédiction `TAKEN` / `NOT TAKEN` sur le saut conditionnel qui suit, et la présentation des registres modifiés avec l'ancienne valeur en grisé.

Utiliser la commande de navigation sémantique de pwndbg pour atteindre le saut conditionnel directement :

```
pwndbg> finish  
pwndbg> nextjmp  
```

`nextjmp` avance l'exécution jusqu'au prochain saut, ce qui atteint directement le `jne` ou `je` qui nous intéresse, sans avoir à compter les `stepi`. Le contexte pwndbg affiche alors la prédiction de branchement, confirmant visuellement le chemin que le programme va emprunter.

---

## Ce que ce checkpoint valide

En complétant ces étapes, les compétences suivantes sont acquises.

**Lecture du contexte automatique** — savoir identifier immédiatement les registres d'arguments (`RDI`, `RSI`) lors d'un arrêt sur un `call`, repérer les adresses de retour sur la pile et comprendre la coloration des modifications de registres.

**Utilisation des commandes spécifiques** — `checksec` pour le triage, `got` pour les imports, `xinfo` pour qualifier une adresse, `dereference` pour le déréférencement récursif, `edit-flags` pour la modification de flags en live.

**Navigation dans le flux de contrôle** — combiner `break`, `continue`, `finish`, `stepi` et `nextjmp` pour atteindre précisément le point d'intérêt dans l'exécution, comprendre le lien entre `test`/`cmp`, les flags et les sauts conditionnels.

**Bascule entre extensions** — vérifier qu'un même résultat (les arguments de `strcmp`) est accessible avec GEF et pwndbg, et apprécier les différences de présentation et de commandes.

Ces compétences seront mobilisées intensivement dans les cas pratiques de la Partie V (chapitres 21 à 25) et dans l'analyse de malware de la Partie VI (chapitres 27 à 29).

---


⏭️ [Chapitre 13 — Instrumentation dynamique avec Frida](/13-frida/README.md)
