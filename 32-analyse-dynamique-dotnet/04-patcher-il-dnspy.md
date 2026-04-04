🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 32.4 — Patcher un assembly .NET à la volée (modifier l'IL avec dnSpy)

> 📁 **Fichiers utilisés** : `binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64/LicenseChecker.dll`  
> 🔧 **Outils** : dnSpy (dnSpyEx), ILSpy (vérification)  
> 📖 **Prérequis** : [Section 32.1](/32-analyse-dynamique-dotnet/01-debug-dnspy-sans-sources.md), notions de bytecode CIL ([Chapitre 30](/30-introduction-re-dotnet/README.md))

---

## Du patch temporaire au patch permanent

Les sections précédentes ont présenté des techniques d'intervention à l'exécution : modifier une variable dans le débogueur (§32.1), remplacer une implémentation de méthode avec Frida (§32.2), intercepter un appel P/Invoke pour altérer sa valeur de retour (§32.3). Toutes ces interventions sont éphémères — elles n'affectent que l'instance en cours du programme et disparaissent dès qu'on le relance.

Le patching IL est fondamentalement différent. Il modifie le fichier de l'assembly sur disque. Le résultat est une version altérée et permanente du programme, qui se comporte différemment à chaque exécution future sans nécessiter aucun outil externe. C'est l'équivalent .NET du patching binaire vu au chapitre 21 (où on inversait un `jz` en `jnz` dans un ELF avec ImHex), mais avec un niveau de confort sans commune mesure.

Dans le monde natif, patcher un binaire x86-64 impose de manipuler des opcodes bruts, de respecter les contraintes d'alignement, de s'assurer que l'instruction de remplacement a exactement la même taille (ou de combler avec des `nop`), et de gérer les éventuelles relocations. Dans le monde .NET, dnSpy offre trois niveaux d'édition : la réécriture directe en C# (dnSpy recompile en IL pour vous), l'édition d'instructions IL individuelles (l'équivalent du patching opcode par opcode), et l'édition des métadonnées (renommer un type, changer une visibilité, modifier un attribut). Chacun de ces niveaux a ses usages et ses limites.

## Le bytecode CIL : rappel essentiel

Avant de patcher, il faut comprendre ce qu'on modifie. Le Common Intermediate Language (CIL, anciennement MSIL) est le bytecode dans lequel le compilateur C# traduit le code source. C'est un langage à pile — les opérandes sont empilées, les opérations consomment le sommet de la pile et empilent leur résultat. Quelques instructions clés pour le patching :

| Instruction IL | Effet | Équivalent conceptuel x86 |  
|---|---|---|  
| `ldc.i4.0` | Empile l'entier 0 | `mov eax, 0` |  
| `ldc.i4.1` | Empile l'entier 1 | `mov eax, 1` |  
| `ldarg.0` | Empile le premier argument (ou `this`) | Lecture depuis le registre d'argument |  
| `ldloc.0` | Empile la variable locale 0 | Lecture depuis la pile |  
| `stloc.0` | Dépile vers la variable locale 0 | Écriture sur la pile |  
| `call` | Appelle une méthode | `call <adresse>` |  
| `callvirt` | Appel virtuel (via vtable) | `call [vtable+offset]` |  
| `ret` | Retourne (la valeur au sommet de la pile) | `ret` |  
| `br` | Saut inconditionnel | `jmp` |  
| `brfalse` / `brtrue` | Saut conditionnel si 0 / non-0 | `jz` / `jnz` |  
| `ceq` | Compare les deux valeurs au sommet, empile 1 si égales | `cmp` + `sete` |  
| `nop` | Ne fait rien | `nop` |  
| `pop` | Dépile et jette la valeur au sommet | — |

Contrairement à x86 où les instructions ont des tailles variables (1 à 15 octets), les instructions IL ont des tailles beaucoup plus prévisibles : un octet pour l'opcode de base, un ou deux octets pour les opcodes étendus (préfixés par `0xFE`), plus les opérandes éventuels. Cette régularité rend le patching IL nettement moins risqué que le patching x86.

Un autre point crucial : le CIL est vérifié par le runtime. Avant d'exécuter une méthode, le JIT vérifie que la pile IL est cohérente à chaque instruction — chaque chemin d'exécution doit laisser la pile dans un état correct. Un patch IL mal construit (par exemple, une pile qui déborde ou qui est vide au moment d'un `ret` qui attend une valeur) sera rejeté par le vérificateur avec une `InvalidProgramException`. C'est une contrainte absente du monde natif, où un opcode invalide produit simplement un crash.

## Édition en C# : le mode le plus confortable

dnSpy permet d'éditer le corps d'une méthode directement en C#. Le workflow est d'une simplicité remarquable : on fait un clic droit sur la méthode dans le code décompilé, on choisit **Edit Method (C#)**, un éditeur de code s'ouvre avec le C# décompilé, on modifie ce qu'on veut, et on clique sur **Compile**. dnSpy recompile le C# en IL et remplace le corps de la méthode dans l'assembly.

### Bypass par réécriture de `Validate()`

L'approche la plus directe pour neutraliser la vérification de licence est de réécrire entièrement la méthode `Validate()` :

On navigue vers `LicenseValidator.Validate()` dans dnSpy. Clic droit → **Edit Method (C#)**. L'éditeur s'ouvre avec le code décompilé. On remplace le corps entier par :

```csharp
public ValidationResult Validate(string username, string licenseKey)
{
    return new ValidationResult
    {
        IsValid        = true,
        FailureReason  = "",
        LicenseLevel   = "Enterprise",
        ExpirationInfo = "Perpétuelle"
    };
}
```

On clique sur **Compile**. Si la compilation réussit (pas d'erreur dans le panneau inférieur), la méthode est remplacée. Le code décompilé dans le panneau principal reflète maintenant la nouvelle version. Toute la logique de validation — hash FNV-1a, appels P/Invoke, XOR croisé, checksum — a disparu. La méthode retourne toujours `true`, quel que soit l'input.

> ⚠️ **Attention** : la modification n'est pas encore sauvegardée sur disque. Elle existe uniquement dans la mémoire de dnSpy. Pour persister le patch, il faut utiliser **File → Save Module** (ou Ctrl+Shift+S). dnSpy écrit alors la version modifiée de l'assembly sur disque. On peut sauvegarder par-dessus l'original ou sous un nouveau nom — la deuxième option est préférable pour pouvoir comparer et revenir en arrière.

### Limites de l'édition C#

L'édition en C# est puissante mais pas toujours applicable. dnSpy utilise son propre compilateur C# interne, qui ne supporte pas toujours toutes les constructions syntaxiques de la version de C# utilisée par le projet original. Si le code décompilé contient des patterns que le compilateur interne ne gère pas — certaines formes de pattern matching, des fonctionnalités C# 12+, des types `ref struct` — la compilation échouera.

Dans ce cas, on dispose de deux recours : simplifier le code C# pour contourner les limitations du compilateur interne, ou descendre au niveau IL.

Par ailleurs, l'édition en C# recompile la méthode entière. Si on ne veut modifier qu'une seule instruction (par exemple, inverser un saut conditionnel), cette approche est un peu disproportionnée. L'édition IL directe est alors plus appropriée.

## Édition d'instructions IL : le patch chirurgical

Pour les modifications ciblées, dnSpy offre un éditeur d'instructions IL. On y accède par clic droit sur la méthode → **Edit IL Instructions**. Une fenêtre s'ouvre avec la liste séquentielle des instructions IL de la méthode, chacune avec son offset, son opcode et ses opérandes.

### Inverser un saut conditionnel

Prenons un cas concret. Dans la méthode `Validate()`, après le calcul du segment A, le code IL contient une séquence de ce type :

```
IL_0040: ldloc.s  actualA         // empile actualA  
IL_0042: ldloc.s  expectedA       // empile expectedA  
IL_0044: beq      IL_0060         // si égaux, sauter à l'étape suivante  
IL_0049: ldloc.0                  // sinon : charger result  
IL_004A: ldc.i4.0                 //         empiler false  
IL_004B: callvirt set_IsValid     //         result.IsValid = false  
...                                //         (assigner FailureReason)
IL_005E: ldloc.0                  //         charger result  
IL_005F: ret                      //         retourner (échec)  
IL_0060: ...                      // suite de la validation  
```

L'instruction `beq IL_0060` (branch if equal) est le point de décision. Si `actualA == expectedA`, on saute à l'étape suivante ; sinon, on tombe dans le bloc d'échec. Pour bypasser ce check, on a plusieurs options.

**Option 1 : transformer `beq` en `br`.** On remplace le saut conditionnel par un saut inconditionnel. Quelle que soit la comparaison, l'exécution saute toujours à `IL_0060`. Dans l'éditeur IL de dnSpy, on sélectionne l'instruction `beq`, on change l'opcode en `br` (branch unconditional), et on garde la même cible de saut. Le check du segment A est neutralisé.

**Option 2 : remplacer le bloc par des `nop`.** On sélectionne toutes les instructions du bloc d'échec (de `IL_0049` à `IL_005F`) et on les remplace par des `nop`. Attention : il faut aussi gérer la pile IL. Si `beq` consomme deux valeurs mais que le code de remplacement n'en consomme pas, le vérificateur IL rejettera le résultat. La solution est de remplacer `beq IL_0060` par `pop` + `pop` + `br IL_0060` — on dépile les deux valeurs qui étaient destinées à la comparaison, puis on saute inconditionnellement.

**Option 3 : forcer le résultat de la comparaison.** Juste avant le `beq`, on insère des instructions qui remplacent les valeurs au sommet de la pile par deux valeurs identiques. Par exemple, on remplace `ldloc.s actualA` par `ldloc.s expectedA` — les deux valeurs empilées sont alors identiques, et le `beq` est toujours pris. Cette approche a l'avantage de ne pas modifier la structure du flux de contrôle.

### Appliquer le patch à chaque étape de validation

La méthode `Validate()` contient quatre points de comparaison (segments A, B, C, D), chacun suivi d'un bloc d'échec qui assigne `IsValid = false` et retourne. Pour bypasser toute la validation, on doit patcher les quatre sauts conditionnels.

En pratique, on identifie chaque `beq` (ou `bne.un`, selon l'optimisation du compilateur — `bne.un` signifie « branch if not equal, unsigned ») et on le transforme en `br` vers la suite du flux normal, ou en `nop` avec gestion de la pile.

> 💡 **Astuce** : dans l'éditeur IL de dnSpy, les cibles de saut sont affichées comme des références cliquables. En survolant un `beq IL_0060`, on voit immédiatement où le saut mène. C'est l'équivalent des cross-references (XREF) dans Ghidra, mais au niveau IL.

### Précautions avec la pile IL

Le piège principal du patching IL est la cohérence de la pile. Chaque chemin d'exécution dans une méthode doit terminer avec la pile dans un état attendu. Voici les erreurs les plus fréquentes et comment les éviter.

**Valeur orpheline sur la pile.** Si on supprime une instruction qui consommait une valeur (par exemple, un `call` qui prenait un argument), la valeur reste sur la pile et le vérificateur la détecte. Solution : ajouter un `pop` pour la consommer.

**Pile vide au moment du `ret`.** Si la méthode a un type de retour (par exemple, `ValidationResult`), le `ret` attend une valeur au sommet de la pile. Si on a retiré des instructions qui produisaient cette valeur, il faut en ajouter une. Pour un type référence, `ldnull` empile une référence nulle ; pour un booléen, `ldc.i4.1` empile `true`.

**Profondeur de pile incohérente entre branches.** Si deux branches convergent vers le même point mais laissent la pile à des profondeurs différentes, le vérificateur rejette le code. C'est le cas le plus délicat à gérer manuellement. L'édition en C# évite ce problème car le compilateur interne gère la pile automatiquement.

dnSpy affiche une erreur dans l'éditeur IL si la pile est incohérente, avec un message indiquant l'instruction problématique. C'est un retour immédiat qui permet de corriger avant de sauvegarder.

## Édition des métadonnées

Au-delà du code IL, dnSpy permet de modifier les métadonnées de l'assembly : noms de types, visibilités, attributs, signatures de méthodes, valeurs de constantes. Cette capacité ouvre des possibilités de patching supplémentaires.

### Changer la visibilité d'une méthode

Dans notre `LicenseChecker`, la classe `NativeBridge` est marquée `internal` — elle n'est pas accessible depuis un assembly externe. Si on voulait écrire un programme C# qui appelle `NativeBridge.ComputeNativeHash()` directement, on ne pourrait pas sans modifier cette visibilité.

Dans dnSpy : clic droit sur la classe `NativeBridge` → **Edit Type**. On change le modificateur d'accès de `internal` à `public`. De même, on peut changer la visibilité des méthodes privées (`ComputeUserHash`, `ComputeCrossXor`, etc.) en `public`. Après sauvegarde, ces méthodes sont appelables depuis n'importe quel code externe — ce qui facilite l'écriture d'un keygen en C# qui réutilise directement les fonctions du programme original.

### Modifier une constante

Les champs `const` et `static readonly` apparaissent dans les métadonnées. On peut les éditer dans dnSpy via clic droit → **Edit Field**. Par exemple, on pourrait modifier la valeur de `HashSeed` ou le contenu de `MagicSalt` pour altérer le comportement des algorithmes de hash sans toucher au code IL.

### Supprimer un attribut

Si l'assembly utilise un attribut `[Obfuscation]` ou un mécanisme de vérification d'intégrité basé sur un attribut custom, on peut simplement supprimer l'attribut dans l'éditeur de métadonnées. dnSpy le permet via l'édition du type ou de la méthode portant l'attribut.

## Sauvegarder et vérifier le patch

Une fois les modifications effectuées, on sauvegarde l'assembly via **File → Save Module** (Ctrl+Shift+S). dnSpy propose plusieurs options :

**Save Module.** Écrit le fichier modifié. On peut choisir un chemin de sortie différent de l'original (recommandé).

**Save All.** Si on a modifié plusieurs assemblies dans la même session, cette option les sauvegarde tous.

Après la sauvegarde, il est essentiel de **vérifier** que le patch fonctionne correctement.

Première vérification : on lance l'application patchée et on teste avec un username et une clé arbitraires. Si le bypass est correct, la validation réussit.

Deuxième vérification : on réouvre l'assembly patché dans dnSpy (ou dans ILSpy, pour un second avis) et on inspecte les méthodes modifiées. Le code décompilé doit refléter les changements attendus.

Troisième vérification : on s'assure que l'application fonctionne normalement en dehors du chemin patché. Un patch trop agressif (par exemple, une pile IL incohérente dans un chemin rarement emprunté) peut provoquer des crashs dans des situations inattendues.

```bash
# Test du patch
cd binaries/ch32-dotnet/LicenseChecker/bin/Release/net8.0/linux-x64

# Sauvegarder l'original
cp LicenseChecker.dll LicenseChecker.dll.bak

# Copier la version patchée (si sauvegardée sous un autre nom)
cp LicenseChecker_patched.dll LicenseChecker.dll

# Lancer et tester
LD_LIBRARY_PATH=. ./LicenseChecker
# Entrer un username quelconque et une clé bidon
# → Devrait afficher "Licence valide !"
```

## Protection contre le patching : les signatures fortes

Dans le monde .NET, un assembly peut être signé avec une clé cryptographique forte (strong name). La signature couvre l'intégralité du contenu de l'assembly — si on modifie un seul octet, la signature devient invalide. Le CLR vérifie cette signature au chargement et refuse les assemblies dont la signature est corrompue.

Notre `LicenseChecker` n'est pas signé (pour simplifier l'exercice). Mais en situation réelle, on rencontre fréquemment des assemblies signés. dnSpy gère cette situation : quand on sauvegarde un assembly modifié, il propose l'option **Remove Strong Name Signature**. Si on coche cette option, la signature est supprimée de l'assembly patché.

La suppression de la signature pose un problème en cascade : les autres assemblies qui référencent l'assembly modifié par son strong name ne le trouveront plus. Il faut alors patcher ces références aussi, ou désactiver la vérification de strong name dans la configuration du runtime (.NET Framework : `<runtime><bypassTrustedAppStrongNames>` ; .NET Core : généralement pas vérifié par défaut).

Certains obfuscateurs ajoutent des vérifications d'intégrité supplémentaires en code — ils calculent un hash de l'assembly au démarrage et le comparent à une valeur hardcodée. Ces vérifications sont des méthodes C# ordinaires, détectables par analyse statique dans dnSpy et neutralisables par les mêmes techniques de patching IL.

## Comparaison avec le patching natif

| Aspect | Patching x86-64 (ch. 21, ImHex) | Patching IL (.NET, dnSpy) |  
|---|---|---|  
| Niveau d'abstraction | Opcodes machine bruts | Bytecode typé avec métadonnées |  
| Contrainte de taille | L'instruction de remplacement doit tenir dans le même espace | Flexible — dnSpy réorganise le bytecode |  
| Vérification | Aucune (un opcode invalide = crash au runtime) | Vérificateur IL du CLR (rejet avant exécution) |  
| Risque de corruption | Élevé (alignement, relocations, cross-references) | Faible (dnSpy gère la cohérence) |  
| Édition haut niveau | Impossible (pas de « recompilation C → x86 » intégrée) | Édition en C# avec recompilation automatique |  
| Signatures | Pas de mécanisme standard (mais checksums custom) | Strong name signing (supprimable) |  
| Outils nécessaires | Éditeur hex + connaissance des opcodes | dnSpy uniquement |  
| Réversibilité | Difficile sans backup | Facile (dnSpy peut ré-éditer indéfiniment) |

L'avantage décisif du patching IL est la possibilité d'éditer en C#. Là où le patching x86 demande de jongler avec des opcodes hexadécimaux et de compter les octets, dnSpy offre un environnement où on modifie du code lisible et où le compilateur interne s'occupe de la traduction vers le bytecode. C'est un changement qualitatif qui rend le patching .NET accessible même sans expertise en assembleur.

## Les trois stratégies de patching en résumé

Selon le contexte et l'objectif, on choisit entre les trois niveaux d'édition :

**L'édition C#** est le premier choix quand on veut réécrire une logique entière — remplacer un corps de méthode, ajouter un chemin de code, modifier un algorithme. Elle est intuitive, sûre (le compilateur vérifie la cohérence), et ne requiert aucune connaissance du bytecode IL. Sa limitation est le support partiel des constructions C# modernes par le compilateur interne de dnSpy.

**L'édition IL** est le choix pour les patchs chirurgicaux — inverser un saut, remplacer une constante, neutraliser un appel. Elle demande de comprendre le modèle à pile du CIL et de maintenir la cohérence de la pile. C'est l'analogue direct du patching d'opcodes x86, mais avec un filet de sécurité (le vérificateur IL de dnSpy).

**L'édition de métadonnées** est le choix pour les modifications structurelles — changer la visibilité d'un type ou d'une méthode, renommer un symbole, supprimer un attribut, modifier une constante. Elle ne touche pas au code IL mais peut avoir des effets profonds sur le comportement de l'application.

En pratique, un patch complet combine souvent les trois niveaux. On commence par l'édition C# pour les modifications majeures, on affine avec l'édition IL pour les ajustements fins, et on adapte les métadonnées si nécessaire.

---


⏭️ [Cas pratique : contourner une vérification de licence C#](/32-analyse-dynamique-dotnet/05-cas-pratique-licence-csharp.md)
