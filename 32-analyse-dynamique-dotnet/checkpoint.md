🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Patcher et keygenner l'application .NET fournie

> 📁 **Cible** : `binaries/ch32-dotnet/` (application `LicenseChecker` + bibliothèque `libnative_check.so`)  
> 📖 **Sections couvertes** : [32.1](/32-analyse-dynamique-dotnet/01-debug-dnspy-sans-sources.md) à [32.5](/32-analyse-dynamique-dotnet/05-cas-pratique-licence-csharp.md)  
> 📝 **Corrigé** : [`solutions/ch32-checkpoint-solution.md`](/solutions/ch32-checkpoint-solution.md)

---

## Contexte

Ce checkpoint valide les compétences acquises au cours du chapitre 32. Vous allez travailler sur l'application `LicenseChecker` — un assembly .NET accompagné d'une bibliothèque native compilée avec GCC — et produire trois livrables qui couvrent l'ensemble des techniques vues : débogage sans sources, hooking CLR et natif, interception P/Invoke, et patching IL.

Vous disposez uniquement des fichiers compilés présents dans `binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64/`. Vous ne devez pas consulter les fichiers sources (`.cs`, `.c`) pendant le checkpoint — ils serviront uniquement pour vérifier vos résultats après coup.

---

## Livrable 1 — Script Frida de capture

Écrire un script Frida unique (`capture.js`) qui, lorsqu'il est attaché au processus `LicenseChecker`, intercepte simultanément les méthodes managées et les fonctions natives impliquées dans la validation, et affiche dans la console les informations suivantes pour chaque tentative de validation :

- Le nom d'utilisateur et la clé fournis en entrée.  
- La valeur attendue de chaque segment (A, B, C, D) telle que calculée par l'application.  
- L'identification de la source de chaque valeur : calculée côté CLR ou côté natif.  
- La clé valide complète, reconstituée à partir des quatre segments capturés.

Le script doit gérer le chargement paresseux de `libnative_check.so` (la bibliothèque n'est pas chargée au démarrage du processus) et doit fonctionner en mode `spawn` comme en mode `attach`.

**Critères de validation** : lancer `LicenseChecker` avec le script actif, entrer un username quelconque avec une clé bidon, et vérifier que la clé affichée par le script est acceptée lors d'une exécution ultérieure sans Frida.

---

## Livrable 2 — Keygen Python autonome

Écrire un script Python (`keygen.py`) qui prend un nom d'utilisateur en argument et produit une clé de licence valide, sans recourir à aucun outil externe (ni Frida, ni dnSpy, ni l'application elle-même).

Le script doit réimplémenter intégralement le schéma de validation — les quatre segments — en se basant sur votre analyse statique (dnSpy pour le côté managé, Ghidra/objdump pour le côté natif). Portez une attention particulière aux points suivants :

- Les salts utilisés côté C# et côté natif ne sont pas identiques. Votre keygen doit utiliser les bons salts pour les bons segments.  
- Les opérations arithmétiques sont masquées sur 16 bits à chaque étape. Un oubli de masquage produit des résultats incorrects sur certains usernames.  
- La conversion du username en minuscules précède le calcul des hash des deux côtés.

**Critères de validation** : le keygen doit produire une clé acceptée par l'application originale (non patchée) pour au moins cinq usernames différents de votre choix, y compris un username contenant des caractères non-ASCII (par exemple `café`, `müller` ou `naïve`).

---

## Livrable 3 — Assembly patché

Produire une version modifiée de `LicenseChecker.dll` qui accepte n'importe quelle clé de licence pour n'importe quel nom d'utilisateur, sans nécessiter Frida, sans nécessiter `libnative_check.so`, et sans afficher de message d'erreur.

Deux variantes sont attendues :

**Variante A — Patch C#.** Réécrire le corps de la méthode `Validate()` en C# via l'éditeur de dnSpy pour qu'elle retourne toujours un résultat positif. Sauvegarder sous le nom `LicenseChecker_patch_csharp.dll`.

**Variante B — Patch IL minimal.** Sans réécrire la méthode entière, modifier uniquement les instructions IL nécessaires pour neutraliser les quatre comparaisons de segments. La logique de calcul (hash, XOR, checksums) doit rester intacte et s'exécuter normalement — seuls les résultats des comparaisons sont ignorés. Sauvegarder sous le nom `LicenseChecker_patch_il.dll`.

**Critères de validation** : chaque variante doit être acceptée par le runtime .NET sans `InvalidProgramException`, doit afficher « Licence valide » pour toute combinaison username/clé, et doit fonctionner même si `libnative_check.so` est absente du répertoire (pour la variante A ; la variante B peut échouer sur les segments B/D si la bibliothèque est absente, ce qui est acceptable tant que le résultat final est « Licence valide »).

---

## Rapport d'accompagnement

Chaque livrable doit être accompagné d'une brève documentation (quelques paragraphes dans un fichier `rapport.md`) décrivant :

- La démarche suivie pour chaque livrable : quels outils, dans quel ordre, quelles découvertes à chaque étape.  
- Le schéma de validation reconstitué, avec le rôle de chaque segment et les constantes clés (salts, seeds, primes, constantes multiplicatives).  
- Pour le patch IL (variante B) : la liste des instructions modifiées, avec leur offset, l'opcode original et l'opcode de remplacement, et la justification de chaque changement (en particulier la gestion de la pile IL).  
- Les difficultés rencontrées et comment elles ont été résolues.

---

## Grille d'auto-évaluation

| Critère | Acquis | Points |  
|---|---|---|  
| Le script Frida capture les 4 segments et affiche la clé valide | ☐ | /20 |  
| Le script Frida gère le chargement paresseux de la bibliothèque native | ☐ | /5 |  
| Le keygen Python produit des clés valides pour 5+ usernames | ☐ | /20 |  
| Le keygen gère correctement les usernames non-ASCII | ☐ | /5 |  
| Le patch C# (variante A) fonctionne sans la bibliothèque native | ☐ | /10 |  
| Le patch IL (variante B) neutralise les 4 checks sans casser la pile IL | ☐ | /15 |  
| Le patch IL conserve la logique de calcul intacte (seuls les sauts sont modifiés) | ☐ | /10 |  
| Le rapport documente le schéma de validation avec les deux salts | ☐ | /10 |  
| Le rapport liste les instructions IL modifiées avec justification | ☐ | /5 |  
| **Total** | | **/100** |

---

## Conseils avant de commencer

**Commencer par la reconnaissance.** Ouvrez l'assembly dans dnSpy et prenez le temps de lire le code décompilé en entier avant de lancer un outil dynamique. La structure de `LicenseValidator` est linéaire et les noms de méthodes sont parlants — la lecture statique vous donnera une carte mentale du flux qui guidera toute la suite.

**Valider chaque segment indépendamment.** Avant d'écrire le keygen complet, vérifiez chaque segment séparément. Utilisez la fenêtre Immediate de dnSpy pour appeler `ComputeUserHash("test")` et comparez avec votre implémentation Python. Corrigez les écarts avant de passer au segment suivant.

**Sauvegarder l'assembly original.** Avant tout patching, faites une copie de `LicenseChecker.dll`. Un patch IL incorrect peut rendre l'assembly non exécutable, et il est plus rapide de repartir de l'original que de tenter de réparer un patch cassé.

**Travailler itérativement sur le patch IL.** Patcherz un seul check à la fois, sauvegardez, testez, puis passez au suivant. Si une `InvalidProgramException` apparaît, vous saurez immédiatement quel patch l'a causée.

---


⏭️ [Partie VIII — Bonus : RE de binaires Rust et Go](/partie-8-rust-go.md)
