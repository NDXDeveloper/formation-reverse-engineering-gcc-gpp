🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 31.2 — dnSpy / dnSpyEx — décompilation + débogage intégré (breakpoints sur C# décompilé)

> 📦 **Chapitre 31 — Décompilation d'assemblies .NET**  
> 

---

## Présentation

dnSpy est bien plus qu'un décompilateur : c'est un **environnement de reverse engineering .NET complet** qui réunit décompilation, débogage, édition d'IL et inspection mémoire dans une seule interface. Là où ILSpy (section 31.1) se limite à l'analyse statique, dnSpy permet de poser un breakpoint directement sur une ligne de code C# décompilé, de lancer l'exécution, d'inspecter les variables locales et de modifier des valeurs en live — le tout sans disposer du code source ni des fichiers PDB.

Pour un reverse engineer habitué au workflow GDB sur binaires natifs (chapitres 11–12), dnSpy représente un changement de confort radical. Imaginez pouvoir déboguer un binaire ELF strippé avec le confort de Visual Studio, en voyant le code C au lieu de l'assembleur : c'est exactement ce que dnSpy offre pour le monde .NET.

### dnSpy vs dnSpyEx

Le projet dnSpy original, créé par **0xd4d** (également auteur de de4dot), a été archivé par son auteur en décembre 2020. Le dépôt GitHub reste accessible mais ne reçoit plus de mises à jour. Le fork communautaire **dnSpyEx** a pris le relais et assure la maintenance active :

- Support des runtimes .NET 6, 7, 8 et 9.  
- Corrections de bugs du décompilateur et du débogueur.  
- Compatibilité avec les dernières constructions C# (required members, collection expressions, primary constructors).

Dans la suite de cette section, le nom « dnSpy » désigne **dnSpyEx** sauf indication contraire. C'est la version que vous devez installer.

---

## Installation

### Téléchargement

Rendez-vous sur le dépôt GitHub de dnSpyEx (`dnSpyEx/dnSpy`) et téléchargez la release correspondant à votre architecture dans l'onglet *Releases*. Deux builds sont proposés :

- **`dnSpy-net-win64.zip`** : version ciblant .NET (recommandée) — supporte le débogage d'applications .NET Framework *et* .NET 6+.  
- **`dnSpy-netframework.zip`** : version ciblant .NET Framework 4.7.2 — nécessaire uniquement si vous devez déboguer des scénarios très spécifiques au runtime legacy.

Décompressez l'archive et lancez `dnSpy.exe`. Aucune installation n'est requise — l'outil est entièrement portable.

### Limitation de plateforme

dnSpy est une application **Windows uniquement**. Son débogueur repose sur les API de débogage CLR de Windows (`ICorDebug`, `DbgShim`), qui n'ont pas d'équivalent sous Linux ou macOS. Si vous travaillez dans une VM Linux, deux options s'offrent à vous :

- Utiliser dnSpy dans une **VM Windows dédiée** (ou en dual-boot).  
- Utiliser ILSpy (section 31.1) pour la décompilation statique sous Linux, et réserver dnSpy pour les sessions de débogage dynamique sous Windows.

Cette contrainte est l'une des raisons pour lesquelles il est important de maîtriser plusieurs outils (cf. comparatif en section 31.4).

---

## Tour de l'interface

L'interface de dnSpy ressemble volontairement à Visual Studio, ce qui la rend immédiatement familière aux développeurs C#. Elle s'organise en plusieurs zones.

### L'arbre des assemblies (panneau gauche)

Fonctionnellement identique à celui d'ILSpy : hiérarchie de namespaces, types, méthodes, propriétés et champs. La navigation par clic et le repliement des nœuds fonctionnent de la même manière. Les assemblies se chargent par glisser-déposer ou via `File > Open`.

Une différence notable : dnSpy charge automatiquement les **assemblies du GAC** (Global Assembly Cache) et les assemblies du runtime .NET installé, ce qui permet de naviguer dans les implémentations internes du framework (classes BCL, runtime, etc.) sans les charger manuellement.

### Le panneau de code (zone centrale)

Le code C# décompilé s'affiche ici avec coloration syntaxique complète. Comme dans ILSpy, un clic sur un type ou un membre navigue vers sa définition. Mais dnSpy ajoute deux capacités visuelles absentes d'ILSpy :

- **La marge de breakpoints** : une colonne grise à gauche du code, dans laquelle vous pouvez cliquer pour poser ou retirer un breakpoint (point rouge). Exactement comme dans Visual Studio.  
- **La surbrillance de la ligne courante** : pendant le débogage, la ligne en cours d'exécution est surlignée en jaune, avec une flèche dans la marge.

### Panneaux de débogage (zone inférieure)

Ces panneaux ne sont visibles que pendant une session de débogage active :

- **Locals** : variables locales de la méthode courante, avec leurs valeurs actuelles, types et possibilité d'expansion pour les objets complexes.  
- **Watch** : expressions personnalisées évaluées à chaque pas de débogage.  
- **Call Stack** : pile d'appels complète, avec navigation vers chaque frame.  
- **Threads** : liste des threads du processus, avec possibilité de basculer entre eux.  
- **Modules** : tous les assemblies chargés par le processus, avec leurs adresses de base — utile pour identifier le chargement dynamique de plugins.  
- **Breakpoints** : liste centralisée de tous les breakpoints posés, avec activation/désactivation individuelle.  
- **Output** : messages de débogage, exceptions, sorties de `Console.WriteLine`.

### Le menu contextuel (clic droit)

Le menu contextuel de dnSpy est beaucoup plus riche que celui d'ILSpy et reflète sa double nature décompilateur/débogueur :

- **Go to Definition** / **Analyze** : navigation et références croisées, comme dans ILSpy.  
- **Edit Method (C#)** : ouvre un éditeur permettant de modifier le code C# d'une méthode, que dnSpy recompile en IL et injecte dans l'assembly.  
- **Edit IL Instructions** : édition directe des opcodes CIL.  
- **Edit Method Body** : modification du corps d'une méthode au niveau IL, avec visualisation des instructions sous forme de tableau.  
- **Set Next Statement** : pendant le débogage, déplace le pointeur d'exécution vers une autre ligne — indispensable pour sauter une vérification.

---

## Le débogueur : la fonctionnalité phare

Le débogueur intégré de dnSpy est ce qui le distingue fondamentalement de tous les autres outils .NET. Il permet de déboguer n'importe quel assembly .NET **sans code source et sans PDB**, en posant des breakpoints directement sur le code C# décompilé.

### Lancer une session de débogage

dnSpy propose deux modes de démarrage, analogues aux modes `run` et `attach` de GDB (chapitre 11) :

**Mode « Start »** (`Debug > Start`) : dnSpy lance le processus cible et attache le débogueur dès le démarrage. Vous pouvez configurer les arguments de ligne de commande, le répertoire de travail et les variables d'environnement. Ce mode est idéal pour les applications console et les outils en ligne de commande.

**Mode « Attach »** (`Debug > Attach to Process`) : dnSpy s'attache à un processus .NET déjà en cours d'exécution. La liste affiche tous les processus .NET détectés sur la machine, avec leur PID, nom et version du runtime. Ce mode est utile pour les services Windows, les applications ASP.NET hébergées par IIS, ou tout processus que vous ne contrôlez pas au démarrage.

Pour chaque mode, vous devez sélectionner le moteur de débogage approprié :

- **.NET Framework** : pour les applications ciblant le runtime classique (versions 2.0 à 4.8).  
- **.NET** : pour les applications .NET 6+, .NET 8, etc.  
- **Unity** : pour les jeux et applications Unity (qui utilisent Mono ou IL2CPP — dans le cas IL2CPP, le débogueur .NET ne fonctionne pas car le CIL a été compilé en natif).

### Breakpoints sur code décompilé

C'est la capacité la plus impressionnante de dnSpy. Le workflow est le suivant :

1. Naviguez dans l'arbre jusqu'à la méthode qui vous intéresse.  
2. Cliquez dans la marge grise à gauche de la ligne où vous voulez vous arrêter. Un point rouge apparaît.  
3. Lancez le débogage (`F5`).  
4. Quand l'exécution atteint cette ligne, le processus se met en pause. La ligne est surlignée en jaune.  
5. Inspectez les variables locales dans le panneau *Locals*, évaluez des expressions dans *Watch*, examinez la pile d'appels.

Ce mécanisme fonctionne parce que dnSpy effectue un mapping entre les instructions IL et les lignes du code C# décompilé. Quand vous posez un breakpoint sur une ligne C#, dnSpy détermine l'offset IL correspondant et pose un breakpoint CLR à cet offset. Le mapping n'est pas toujours parfait — il arrive qu'un breakpoint se décale d'une ligne ou deux, surtout dans du code optimisé — mais il est fiable dans la très grande majorité des cas.

### Breakpoints conditionnels

Comme les breakpoints conditionnels de GDB (chapitre 11, section 11.5), dnSpy permet de conditionner l'arrêt à une expression booléenne. Faites un clic droit sur un breakpoint posé, puis *Settings*. Vous pouvez spécifier :

- **Condition** : une expression C# évaluée dans le contexte de la méthode. Par exemple : `password.Length > 8` ou `userId == 42`.  
- **Hit Count** : arrêt uniquement au N-ième passage (« when hit count equals 5 », « when hit count is a multiple of 100 »).  
- **Filter** : arrêt uniquement sur un thread spécifique ou un processus donné.  
- **Action** : au lieu de mettre en pause, consigner un message dans la fenêtre *Output* (tracepoint) — utile pour tracer des appels sans interrompre l'exécution.

Les breakpoints conditionnels sont particulièrement puissants pour le RE de routines de validation. Imaginez une méthode `CheckSerial(string serial)` appelée en boucle sur plusieurs clés : un breakpoint conditionné à `serial.StartsWith("PRO-")` vous permet d'isoler précisément les appels qui vous intéressent.

### Inspection de la mémoire et des objets

Pendant une pause de débogage, le panneau *Locals* affiche l'état complet des variables locales et des paramètres de la méthode courante. Pour les types complexes, vous pouvez expanser l'objet pour voir ses champs, et continuer récursivement. La fenêtre *Watch* accepte n'importe quelle expression C# valide dans le contexte courant :

- `this.config.LicenseKey` — accéder à un champ privé de l'objet courant.  
- `System.Text.Encoding.UTF8.GetString(buffer)` — convertir un tableau d'octets en chaîne lisible.  
- `BitConverter.ToString(hashBytes).Replace("-", "")` — formater un hash en hexadécimal.  
- `((MyDerivedClass)baseRef).SecretField` — caster pour accéder à un champ d'un type dérivé.

C'est l'équivalent de la commande `print` de GDB (chapitre 11, section 11.2), mais avec la puissance d'évaluation du runtime .NET complet. Vous pouvez appeler des méthodes, instancier des objets, et évaluer des expressions arbitraires — un confort sans équivalent dans le monde natif.

Pour l'inspection mémoire de bas niveau, `Debug > Windows > Memory` ouvre un visualiseur hexadécimal du processus cible, similaire à la commande `x` de GDB (chapitre 11, section 11.3). Vous pouvez naviguer par adresse ou par expression (nom de variable, pointeur).

### Pas à pas

Les commandes de pas à pas sont les mêmes que dans tout débogueur :

| Commande | Raccourci | Équivalent GDB | Comportement |  
|---|---|---|---|  
| **Step Into** | `F11` | `step` | Entre dans la méthode appelée |  
| **Step Over** | `F10` | `next` | Exécute la ligne sans entrer dans les appels |  
| **Step Out** | `Shift+F11` | `finish` | Continue jusqu'au retour de la méthode courante |  
| **Continue** | `F5` | `continue` | Reprend l'exécution jusqu'au prochain breakpoint |  
| **Run to Cursor** | `Ctrl+F10` | `advance` | Continue jusqu'à la ligne où se trouve le curseur |  
| **Set Next Statement** | — | `set $rip` | Déplace le pointeur d'exécution (sauter du code) |

**Set Next Statement** mérite une attention particulière. Cette commande permet de déplacer le pointeur d'exécution vers n'importe quelle ligne de la méthode courante, sans exécuter les lignes intermédiaires. En RE, c'est un outil de contournement immédiat : si une vérification de licence se trouve en ligne 15 et que le code « licence valide » commence en ligne 20, vous pouvez littéralement sauter la vérification en déplaçant l'exécution de la ligne 14 à la ligne 20. C'est l'équivalent de la modification de `$rip` dans GDB, mais avec la sécurité du mapping C# — vous voyez exactement où vous sautez.

> ⚠️ **Attention** : Set Next Statement peut provoquer un état incohérent si vous sautez des initialisations de variables ou des blocs `try/finally`. Utilisez-le avec discernement.

---

## Édition d'assemblies

La seconde fonctionnalité distinctive de dnSpy est la possibilité de **modifier un assembly** et de sauvegarder les changements. C'est le patching binaire (chapitre 21, section 21.6) adapté au monde .NET.

### Édition en C#

Faites un clic droit sur une méthode, puis `Edit Method (C#)`. dnSpy ouvre un éditeur de code dans le panneau central, pré-rempli avec le code décompilé de la méthode. Vous pouvez modifier le code C# librement :

```csharp
// Avant modification (code original décompilé)
public bool ValidateLicense(string key)
{
    byte[] hash = ComputeHash(key);
    return CompareBytes(hash, this.expectedHash);
}

// Après modification (contournement)
public bool ValidateLicense(string key)
{
    return true;
}
```

Cliquez sur `Compile`. dnSpy invoque Roslyn (le compilateur C#) pour transformer votre code modifié en IL, puis remplace le corps de la méthode dans l'assembly en mémoire. Si la compilation échoue (erreur de syntaxe, type non résolu), les erreurs sont affichées dans un panneau en bas de l'éditeur, comme dans Visual Studio.

L'édition C# a quelques contraintes à garder en tête :

- Vous ne pouvez modifier qu'**une méthode à la fois** (pas de refactoring multi-fichiers).  
- Les **types et signatures** de la méthode ne peuvent pas être changés (vous ne pouvez pas ajouter un paramètre ou changer le type de retour).  
- Le compilateur intégré a besoin de **résoudre tous les types** référencés — si un assembly de dépendance manque, la compilation échouera.

### Édition IL directe

Pour des modifications plus chirurgicales, `Edit IL Instructions` affiche le corps de la méthode sous forme de tableau d'instructions CIL, avec pour chaque instruction son offset, son opcode et son opérande. Vous pouvez :

- Modifier un opcode (transformer un `brfalse` en `brtrue` pour inverser un branchement — exactement comme inverser un `jz`/`jnz` sur un binaire natif au chapitre 21, section 21.4).  
- Supprimer des instructions (les remplacer par des `nop`).  
- Insérer de nouvelles instructions.  
- Modifier les opérandes (changer la cible d'un saut, remplacer une constante).

L'édition IL est plus puissante que l'édition C# car elle n'est pas limitée par ce que le compilateur Roslyn peut produire. Certaines transformations (modification de la table d'exceptions, ajout de handlers `fault`, manipulation des contraintes de génériques) ne sont possibles qu'au niveau IL.

### Sauvegarder les modifications

Après avoir effectué vos modifications (en C# ou en IL), sauvegardez avec `File > Save Module` ou `File > Save All`. dnSpy écrit un nouvel assembly sur disque avec vos changements intégrés. L'assembly original n'est pas modifié — dnSpy crée un nouveau fichier (ou vous demande de confirmer l'écrasement).

> 💡 **Bonne pratique** : travaillez toujours sur une copie de l'assembly et conservez l'original intact. Nommez vos versions modifiées de manière explicite (`MonApp_patched_v1.exe`, `MonApp_license_bypass.exe`) pour garder une trace de vos modifications.

> ⚠️ Si l'assembly est **signé** (strong name), la modification invalide la signature. L'application ou le runtime refuseront de charger l'assembly modifié. La section 32.4 du chapitre suivant traite des techniques pour contourner cette vérification.

---

## Fonctionnalités complémentaires

### Analyze (références croisées)

Comme dans ILSpy, `Ctrl+R` sur un symbole ouvre un panneau *Analyzer* qui liste les utilisations : « Used By », « Uses », « Instantiated By », « Assigned By », « Read By ». L'implémentation est fonctionnellement équivalente à celle d'ILSpy — le moteur de décompilation de dnSpy est d'ailleurs un fork ancien du moteur d'ILSpy, bien que les deux aient divergé depuis.

### Recherche avancée

`Ctrl+Shift+K` ouvre une boîte de recherche globale qui supporte les mêmes catégories que celle d'ILSpy (types, méthodes, champs, chaînes littérales), avec en plus la possibilité de chercher dans les **assemblies du runtime** chargés automatiquement. Cela permet de trouver des utilisations de types framework peu courants qui pourraient révéler des fonctionnalités cachées.

### Hex Editor intégré

`Ctrl+X` sur une méthode ou un champ ouvre un éditeur hexadécimal positionné à l'offset correspondant dans le fichier PE. Cet éditeur est plus rudimentaire qu'ImHex (chapitre 6), mais il permet des corrections rapides sans quitter dnSpy. Il est particulièrement utile pour modifier des constantes (chaînes hardcodées, octets de configuration) directement dans le binaire.

### Onglets multiples et sessions

dnSpy supporte les onglets multiples : vous pouvez ouvrir plusieurs méthodes ou types dans des onglets séparés et basculer entre eux. Combiné au système de bookmarks (`Ctrl+K`), cela permet de garder en vue simultanément la routine de validation, la fonction de hachage qu'elle appelle, et la structure de données qu'elle manipule.

---

## Workflow de débogage RE avec dnSpy

Pour illustrer concrètement l'utilisation de dnSpy en reverse engineering, voici un workflow type face à une application .NET dont vous voulez comprendre la logique de validation.

### Phase 1 — Reconnaissance statique

Chargez l'assembly et parcourez l'arbre. Identifiez les namespaces liés à la validation (`Licensing`, `Security`, `Auth`…). Utilisez la recherche de chaînes pour localiser les messages d'erreur (« Invalid license key », « Trial expired », etc.). Remontez les références croisées depuis ces chaînes jusqu'aux méthodes qui les utilisent. À ce stade, le workflow est identique à celui d'ILSpy.

### Phase 2 — Pose des breakpoints stratégiques

Vous avez identifié la méthode candidate, par exemple `LicenseManager.ValidateKey(string)`. Posez un breakpoint à l'entrée de cette méthode (première ligne du corps). Si la méthode est longue, posez des breakpoints supplémentaires aux points de branchement critiques — les `if`, les `return`, les appels à des fonctions crypto.

### Phase 3 — Exécution et observation

Lancez le débogage (`F5`). Interagissez avec l'application normalement — entrez un numéro de licence quelconque et validez. L'exécution s'arrête à votre breakpoint. Vous voyez maintenant :

- La **valeur exacte** du paramètre `key` que vous avez saisi, dans le panneau *Locals*.  
- Les **variables locales** de la méthode au fur et à mesure que vous avancez en pas à pas.  
- Les **valeurs de retour** des sous-méthodes appelées (hash calculé, résultat de comparaison, etc.).

Avancez en pas à pas (`F10` / `F11`) à travers la logique de validation. Notez à chaque étape les transformations appliquées à votre entrée : quelle fonction de hachage est utilisée ? Quel est le hash attendu ? Y a-t-il un sel ? La vérification est-elle symétrique (comparaison directe) ou asymétrique (vérification de signature) ?

### Phase 4 — Extraction d'informations

Grâce au panneau *Watch*, vous pouvez évaluer des expressions à chaque pause pour extraire les données critiques :

- L'argument exact passé à une fonction de comparaison.  
- Le contenu d'un tableau d'octets utilisé comme clé de déchiffrement.  
- La chaîne attendue avant hachage.  
- Le résultat intermédiaire d'un calcul cryptographique.

Ces informations alimentent directement l'écriture d'un keygen (chapitre 21, section 21.8) ou la compréhension d'un schéma de chiffrement (chapitre 24).

### Phase 5 — Contournement rapide (optionnel)

Si l'objectif immédiat est de contourner la vérification (dans un contexte de CTF ou d'audit autorisé), vous pouvez utiliser **Set Next Statement** pour sauter la vérification en temps réel, ou **Edit Method** pour modifier le code et sauvegarder une version patchée de l'assembly.

---

## Forces et limites de dnSpy

### Forces

- **Débogueur sur code décompilé** : la fonctionnalité phare, sans équivalent dans l'écosystème .NET libre. Transforme le RE .NET en une expérience proche du débogage avec sources.  
- **Édition C# et IL** : permet le patching direct d'assemblies, depuis une simple modification de constante jusqu'au remplacement complet d'une méthode.  
- **Interface familière** : le layout Visual Studio réduit la courbe d'apprentissage pour les développeurs C#.  
- **Support multi-runtime** : .NET Framework, .NET 6+, Unity (Mono) — couvre l'essentiel des applications .NET rencontrées en pratique.  
- **Breakpoints conditionnels et tracepoints** : permettent une analyse dynamique fine sans noyer l'analyste sous les pauses.  
- **Gratuit et open source** (GPL v3) via le fork dnSpyEx.

### Limites

- **Windows uniquement** : le débogueur repose sur les API CLR natives de Windows. Aucun port Linux n'est prévu.  
- **Fork community-driven** : le rythme de développement de dnSpyEx est irrégulier, dépendant des contributions bénévoles. Certaines fonctionnalités peuvent présenter des régressions entre versions.  
- **Qualité de décompilation** : le moteur de décompilation de dnSpy, bien que bon, a divergé de celui d'ILSpy et peut produire des résultats légèrement différents (et parfois moins fidèles) sur les constructions C# les plus récentes. ILSpy bénéficie d'un développement plus actif de son moteur de décompilation.  
- **Pas d'export de projet** : contrairement à ILSpy, dnSpy ne propose pas d'export en projet `.csproj` complet. Vous pouvez copier le code méthode par méthode, mais pas exporter l'arborescence entière en un clic.  
- **Assemblies obfusqués** : comme ILSpy, dnSpy affiche fidèlement un assembly obfusqué, mais le résultat reste illisible. Le débogueur fonctionne sur du code obfusqué, ce qui aide, mais ne remplace pas une passe de déobfuscation préalable (section 31.5).  
- **Pas de support IL2CPP** : les jeux Unity compilés en IL2CPP (le CIL est converti en C++ puis compilé en natif) ne sont pas déboguables par dnSpy. Ils requièrent des outils natifs comme Ghidra (chapitre 8) ou des outils spécialisés comme Il2CppDumper.

---

## Résumé

dnSpy (via le fork dnSpyEx) est l'outil de prédilection pour l'**analyse dynamique** d'assemblies .NET. Sa capacité unique de débogage sur code décompilé — breakpoints, pas à pas, inspection de variables, évaluation d'expressions — en fait le compagnon indispensable d'ILSpy. Là où ILSpy excelle en lecture et export de code, dnSpy excelle en observation et modification du comportement à l'exécution. En pratique, un reverse engineer .NET efficace utilise les deux : ILSpy pour la reconnaissance statique et l'export, dnSpy pour le débogage et le patching. La section 31.4 formalisera cette complémentarité dans un comparatif détaillé.

---


⏭️ [dotPeek (JetBrains) — navigation et export de sources](/31-decompilation-dotnet/03-dotpeek.md)
