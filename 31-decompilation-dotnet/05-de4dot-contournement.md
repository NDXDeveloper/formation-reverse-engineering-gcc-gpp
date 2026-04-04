🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 31.5 — Décompiler malgré l'obfuscation : de4dot et techniques de contournement

> 📦 **Chapitre 31 — Décompilation d'assemblies .NET**  
> 

---

## Le problème

Les sections précédentes ont montré que la décompilation .NET produit du code C# de haute fidélité — noms de types, signatures de méthodes, hiérarchies d'héritage, tout y est. Cette transparence est une aubaine pour le reverse engineer, mais un cauchemar pour le développeur qui souhaite protéger sa propriété intellectuelle. La réponse de l'industrie est l'**obfuscation** : un ensemble de transformations appliquées au bytecode CIL après compilation pour rendre la décompilation aussi difficile et improductive que possible.

Le chapitre 30, section 30.3, a présenté les obfuscateurs courants et leurs catégories de transformations. Cette section passe à la pratique : comment **identifier** l'obfuscateur utilisé, comment **inverser** ses transformations avec de4dot, et que faire quand l'outil automatique ne suffit pas.

L'analogie avec le monde natif est directe. L'obfuscation .NET joue le même rôle que le stripping, le packing et l'obfuscation de flux de contrôle traités au chapitre 19 — les techniques diffèrent car le support est du bytecode et non du code machine, mais la philosophie du contournement est identique : identifier la protection, comprendre la transformation, l'inverser.

---

## Rappel : les couches d'obfuscation

Avant de contourner une protection, il faut savoir ce qu'on affronte. Les obfuscateurs .NET appliquent typiquement plusieurs couches de transformations, chacune ciblant un aspect différent de la lisibilité du code décompilé.

### Renommage des symboles

La transformation la plus répandue et la plus immédiatement visible. L'obfuscateur remplace les noms de classes, méthodes, champs, propriétés, paramètres et namespaces par des identifiants générés — des séquences aléatoires (`a`, `b`, `c0d`), des caractères Unicode non imprimables, ou des noms identiques dans des contextes différents pour créer de la confusion. Le bytecode CIL reste fonctionnellement identique, mais le code C# décompilé devient :

```csharp
// Avant obfuscation
public class LicenseValidator
{
    private readonly byte[] _expectedHash;
    
    public bool Validate(string licenseKey)
    {
        byte[] hash = ComputeSHA256(licenseKey);
        return CompareBytes(hash, _expectedHash);
    }
}

// Après renommage (exemple ConfuserEx)
public class \u0002
{
    private readonly byte[] \u0003;
    
    public bool \u0002(string \u0002)
    {
        byte[] array = \u0005.\u0002(\u0002);
        return \u0005.\u0003(array, this.\u0003);
    }
}
```

Le code est fonctionnellement identique, mais vous avez perdu toute l'information sémantique qui rendait le RE .NET si confortable. Vous êtes ramené à une situation comparable au RE d'un binaire ELF strippé — sauf que la structure du code (branchements, appels, types) reste visible.

### Chiffrement des chaînes de caractères

L'obfuscateur remplace chaque chaîne littérale par un appel à une fonction de déchiffrement. Au lieu de `"Invalid license key"` dans le code, vous trouvez un appel comme `\u0008.\u0002(182)` qui déchiffre la chaîne à l'exécution depuis un tableau d'octets embarqué dans l'assembly. Cela neutralise la commande `strings` et la recherche de chaînes dans ILSpy/dnSpy — votre premier réflexe de triage devient inopérant.

### Obfuscation du flux de contrôle

L'obfuscateur restructure le graphe de contrôle des méthodes pour le rendre incompréhensible. Les techniques courantes sont :

- **Control flow flattening** (aplatissement) : tous les blocs de base d'une méthode sont placés dans un `switch` géant piloté par une variable d'état. Le flux linéaire original (bloc A → bloc B → bloc C) devient une boucle `while(true) { switch(state) { ... } }` où l'ordre des `case` ne correspond pas à l'ordre logique. C'est le même principe que l'obfuscation de flux de contrôle vue au chapitre 19, section 19.3, mais appliquée au CIL plutôt qu'au code machine.  
- **Bogus control flow** : insertion de branchements conditionnels dont la condition est toujours vraie (ou toujours fausse), reliés à du code mort. Le décompilateur produit des `if` qui ne s'exécutent jamais, noyant la logique réelle.  
- **Proxy calls** : les appels de méthodes sont redirigés à travers des méthodes « proxy » générées, ajoutant une couche d'indirection qui masque la cible réelle de l'appel.

### Protection anti-tampering et anti-débogage

Certains obfuscateurs ajoutent des mécanismes actifs de protection :

- **Vérification d'intégrité** : au démarrage, l'assembly calcule un hash de son propre bytecode et le compare à une valeur attendue. Toute modification (patching) est détectée et provoque un crash ou un comportement altéré.  
- **Détection de débogueur** : vérification de `System.Diagnostics.Debugger.IsAttached`, appels à `Debugger.IsLogging()`, mesures de timing pour détecter les pauses de débogage.  
- **Packing du bytecode** : le CIL est compressé ou chiffré et n'est décompressé qu'au moment du chargement par le runtime, via un module de bootstrap natif ou un resolver d'assembly custom.

### Transformations des métadonnées

L'obfuscateur peut manipuler les tables de métadonnées de manières qui perturbent les décompilateurs sans invalider l'assembly aux yeux du runtime :

- Insertion d'entrées invalides ou redondantes dans les metadata tables.  
- Création de types ou de méthodes « fantômes » jamais appelés.  
- Modification des flags d'accès (rendre `public` des méthodes `private` et vice versa) tout en s'appuyant sur le comportement réel du runtime.

---

## Identifier l'obfuscateur

La première étape face à un assembly obfusqué est d'identifier quel obfuscateur a été utilisé. Chacun laisse des traces caractéristiques — des signatures aussi reconnaissables que les magic bytes d'un format de fichier.

### Signatures dans les attributs

La plupart des obfuscateurs injectent un attribut d'assembly qui indique leur nom et leur version. C'est le cas de Dotfuscator, SmartAssembly et de nombreux outils commerciaux. Ouvrez l'assembly dans ILSpy et inspectez les attributs au niveau de l'assembly (nœud racine > Properties ou Attributes). Vous pourriez trouver des entrées comme :

- `[assembly: Dotfuscated]`  
- `[assembly: SmartAssembly.Attributes.PoweredBy]`  
- `[assembly: ConfusedBy("ConfuserEx vX.Y.Z")]`  
- `[assembly: Obfuscation(...)]` avec des paramètres spécifiques à un outil.

Ces attributs ne sont pas toujours présents — un obfuscateur bien configuré peut les supprimer — mais ils constituent un point de vérification rapide.

### Patterns de renommage

Chaque obfuscateur a un style de renommage caractéristique :

- **ConfuserEx** : caractères Unicode non imprimables (`\u0001`, `\u0002`…), souvent identiques dans différents contextes (plusieurs méthodes nommées `\u0002` dans la même classe, distinguées par leur signature).  
- **Dotfuscator** : noms courts en minuscules (`a`, `b`, `c`, `a0`, `b0`…), préservation partielle des namespaces publics si configuré en « library mode ».  
- **SmartAssembly** : noms générés sous forme de chaînes aléatoires plus longues, parfois avec des tirets ou des points.  
- **Crypto Obfuscator** : utilisation intensive de caractères Unicode exotiques, parfois des caractères de contrôle qui perturbent l'affichage dans certains éditeurs.  
- **.NET Reactor** : noms contenant des caractères spéciaux (espaces, caractères null), création de types avec des noms identiques dans des namespaces différents.

### Structure du code de déchiffrement de chaînes

Si les chaînes sont chiffrées, examinez les méthodes statiques appelées en remplacement des chaînes littérales. Leur structure (nombre de paramètres, type de retour `string`, présence d'un tableau `byte[]` statique, algorithme de déchiffrement) est caractéristique de chaque obfuscateur.

### Détection automatique par de4dot

de4dot lui-même inclut un mécanisme de détection. Quand vous lui soumettez un assembly, il affiche l'obfuscateur identifié avant de procéder à la déobfuscation :

```
de4dot v3.1.41592.3141 Copyright (C) 2011-2015 de4dot@gmail.com  
Detected ConfuserEx v1.0.0 (Max settings)  
```

C'est souvent la manière la plus rapide d'obtenir l'information.

---

## de4dot : déobfuscation automatique

### Présentation

de4dot est un déobfuscateur .NET open source (GPL v3) créé par **0xd4d** — le même auteur que dnSpy. C'est l'outil de référence pour le nettoyage automatique d'assemblies .NET obfusqués. Il fonctionne en analysant le bytecode CIL de l'assembly, en identifiant les patterns d'obfuscation, et en appliquant les transformations inverses pour restaurer un assembly lisible.

de4dot supporte une liste étendue d'obfuscateurs, parmi lesquels :

- Agile.NET (CliSecure)  
- Babel .NET  
- CodeFort  
- CodeVeil  
- CodeWall  
- Confuser / ConfuserEx  
- CryptoObfuscator  
- DeepSea  
- Dotfuscator  
- .NET Reactor  
- Eazfuscator.NET  
- GoliathNET  
- ILProtector  
- MaxtoCode  
- MPRESS  
- Rummage  
- Skater .NET  
- SmartAssembly  
- Spices.Net  
- Xenocode

Pour chaque obfuscateur supporté, de4dot connaît les patterns de transformation et sait les inverser — du moins pour les versions qu'il a été entraîné à reconnaître. Les versions récentes d'obfuscateurs commerciaux peuvent avoir changé leurs patterns, ce qui nécessite parfois des ajustements manuels.

### Installation

de4dot est un outil en ligne de commande. Téléchargez-le depuis son dépôt GitHub et décompressez l'archive. L'exécutable est `de4dot.exe` (Windows) ou exécutable via `dotnet de4dot.dll` sur les plateformes supportant .NET.

> ⚠️ **Note sur la maintenance** : le dépôt original de de4dot, comme celui de dnSpy, n'est plus activement maintenu. Des forks communautaires existent avec des corrections mineures et un support étendu. Pour les assemblies protégés par des versions récentes d'obfuscateurs, il est possible que de4dot ne parvienne pas à inverser toutes les transformations — d'où l'importance des techniques manuelles décrites plus loin dans cette section.

### Usage de base

L'utilisation la plus simple de de4dot consiste à lui fournir un assembly et le laisser détecter et contourner l'obfuscation automatiquement :

```bash
# Déobfuscation automatique — de4dot détecte l'obfuscateur
de4dot ObfuscatedApp.exe

# Le résultat est écrit dans ObfuscatedApp-cleaned.exe
```

de4dot crée un nouvel assembly avec le suffixe `-cleaned` par défaut. L'assembly original n'est pas modifié. Le fichier nettoyé peut ensuite être ouvert normalement dans ILSpy, dnSpy ou dotPeek.

### Spécifier l'obfuscateur

Si de4dot ne détecte pas automatiquement l'obfuscateur, ou s'il détecte le mauvais, vous pouvez le forcer avec le paramètre `-p` :

```bash
# Forcer la détection comme ConfuserEx
de4dot ObfuscatedApp.exe -p cr

# Forcer la détection comme SmartAssembly
de4dot ObfuscatedApp.exe -p sa

# Forcer la détection comme .NET Reactor
de4dot ObfuscatedApp.exe -p dr
```

Les codes courts (`cr`, `sa`, `dr`, `df` pour Dotfuscator, `el` pour Eazfuscator, etc.) sont listés dans la documentation de de4dot et dans l'aide en ligne (`de4dot --help`).

### Traitement par lots

de4dot peut traiter plusieurs assemblies simultanément — utile quand une application est composée de l'exécutable principal et de plusieurs DLL, tous obfusqués avec le même outil :

```bash
# Traiter tous les assemblies d'un répertoire
de4dot -r C:\path\to\app\ -ro C:\path\to\output\
```

Le paramètre `-r` (recursive) analyse le répertoire, et `-ro` spécifie le répertoire de sortie.

### Renommage intelligent

de4dot ne se contente pas de déchiffrer les chaînes et de simplifier le flux de contrôle. Il tente aussi de **restaurer des noms lisibles** pour les symboles renommés. Comme les noms originaux sont définitivement perdus, de4dot génère des noms descriptifs basés sur le contexte :

- Les classes sont renommées selon leur namespace et leur position (`Class0`, `Class1`…).  
- Les méthodes sont renommées selon leur signature (`method_0`, `method_1`…) ou leur rôle quand il est détectable (`get_Property0`, `set_Property0` pour les accesseurs).  
- Les champs sont renommés selon leur type (`string_0`, `int_0`, `byte_array_0`…).

Ces noms ne sont pas les noms originaux, mais ils sont infiniment plus lisibles que `\u0002` ou `a0b`. Combinés avec l'analyse du flux de données (que fait cette variable ? d'où vient-elle ?), ils suffisent généralement à reconstruire la logique.

Vous pouvez contrôler le comportement de renommage :

```bash
# Désactiver le renommage (garder les noms obfusqués tels quels)
de4dot ObfuscatedApp.exe --dont-rename

# Renommer uniquement les types ayant des noms invalides (Unicode non imprimable)
de4dot ObfuscatedApp.exe --only-rename-invalid
```

L'option `--dont-rename` est utile quand vous voulez conserver les noms d'origine pour les corréler avec des traces de débogage ou des logs.

---

## Ce que de4dot fait — et ne fait pas

Pour calibrer vos attentes, voici ce que de4dot gère bien et ce qui échappe à ses capacités.

### Ce que de4dot réussit généralement

**Déchiffrement des chaînes.** C'est la transformation la plus couramment inversée. de4dot identifie la méthode de déchiffrement, l'exécute (en chargeant l'assembly dans un AppDomain isolé), récupère les chaînes en clair et les réinjecte comme littéraux dans le CIL. Après traitement, vos recherches de chaînes dans ILSpy fonctionnent à nouveau normalement.

**Restauration des proxy calls.** Les méthodes proxy insérées par l'obfuscateur sont supprimées et les appels sont redirigés vers leurs cibles réelles. Le graphe d'appels redevient lisible.

**Suppression du code mort.** Les branchements bogus et le code mort inséré par l'obfuscateur sont identifiés et éliminés.

**Nettoyage des métadonnées.** Les entrées de métadonnées invalides ou parasites sont supprimées, et les flags d'accès sont corrigés quand c'est possible.

### Ce que de4dot réussit partiellement

**Control flow flattening.** de4dot parvient à défaire l'aplatissement de flux de contrôle dans de nombreux cas, mais les obfuscateurs récents utilisent des techniques de plus en plus sophistiquées (variables d'état dynamiques, calcul de la prochaine cible à l'exécution) qui peuvent résister à l'analyse statique de de4dot. Le résultat est parfois un code partiellement déplié — certaines méthodes sont restaurées, d'autres conservent la structure `switch/while`.

**Renommage.** Les noms générés sont fonctionnels mais ne reconstituent pas les noms originaux. Vous devrez renommer manuellement les symboles critiques au fur et à mesure de votre compréhension du code.

### Ce que de4dot ne fait pas

**Obfuscateurs inconnus.** Si l'assembly est protégé par un obfuscateur que de4dot ne reconnaît pas (outil commercial récent, protection custom), l'outil n'applique aucune transformation significative. Vous devez alors recourir aux techniques manuelles.

**Packing natif.** Si l'obfuscateur a encapsulé l'assembly .NET dans un loader natif (certaines configurations de .NET Reactor, Themida, VMProtect avec support .NET), de4dot ne peut pas traiter le fichier car il ne voit pas l'assembly .NET — il faut d'abord extraire l'assembly du packer natif, une opération qui relève des techniques du chapitre 29.

**Virtualisation du CIL.** Certains obfuscateurs haut de gamme (KoiVM pour ConfuserEx, Agile.NET VM mode) convertissent le bytecode CIL en un bytecode custom interprété par une machine virtuelle embarquée dans l'assembly. de4dot ne peut pas inverser cette transformation car le jeu d'instructions est propriétaire et change entre les versions. C'est l'équivalent .NET de la virtualisation de code machine (VMProtect, Themida) mentionnée au chapitre 19 — et c'est la protection la plus difficile à contourner.

---

## Techniques manuelles de contournement

Quand de4dot ne suffit pas — obfuscateur non reconnu, version trop récente, virtualisation partielle — il faut passer aux techniques manuelles. L'objectif n'est pas nécessairement de restaurer l'assembly entier, mais de rendre lisibles les parties qui vous intéressent.

### Déchiffrement de chaînes par exécution dynamique

Si de4dot ne parvient pas à déchiffrer les chaînes automatiquement, vous pouvez le faire manuellement en utilisant le débogueur de dnSpy.

La stratégie est simple : identifiez dans le code décompilé les appels à la méthode de déchiffrement de chaînes (typiquement une méthode statique acceptant un entier ou un tableau d'octets et retournant un `string`). Posez un breakpoint **après** l'appel, sur la ligne qui utilise la chaîne déchiffrée. Lancez l'application dans dnSpy. À chaque pause, la variable contenant la chaîne déchiffrée est visible dans le panneau *Locals* ou évaluable dans *Watch*.

Pour automatiser ce processus, vous pouvez écrire un petit programme C# qui charge l'assembly obfusqué par réflexion et appelle la méthode de déchiffrement avec tous les arguments possibles :

```csharp
// Principe du déchiffrement par réflexion
// (le code exact dépend de la signature de la méthode de déchiffrement)
var asm = Assembly.LoadFrom("ObfuscatedApp.exe");  
var decryptMethod = asm.GetType("\u0008").GetMethod("\u0002",  
    BindingFlags.Static | BindingFlags.NonPublic);

for (int i = 0; i < 500; i++)
{
    try
    {
        string result = (string)decryptMethod.Invoke(null, new object[] { i });
        Console.WriteLine($"[{i}] = \"{result}\"");
    }
    catch { }
}
```

Cette technique exploite le fait que la méthode de déchiffrement fait partie de l'assembly et peut être invoquée directement — l'obfuscateur a chiffré les chaînes, mais le code de déchiffrement est là, dans l'assembly, prêt à être utilisé contre lui-même.

### Défaire le control flow flattening manuellement

Face à une méthode aplatie que de4dot n'a pas su restaurer, deux approches sont possibles.

**Approche statique.** Ouvrez la méthode dans la vue IL d'ILSpy ou dnSpy. Identifiez la variable d'état (généralement un `int` local chargé au début de chaque itération de la boucle `while`). Tracez manuellement les valeurs de cette variable pour reconstruire l'ordre réel des blocs. Sur papier ou dans un éditeur, réordonnez les blocs en suivant les transitions d'état. C'est un travail fastidieux mais mécanique — exactement comme la reconstruction de flux de contrôle aplati sur un binaire natif (chapitre 19, section 19.3).

**Approche dynamique.** Utilisez le débogueur de dnSpy pour exécuter la méthode pas à pas. Notez la séquence des `case` exécutés — c'est l'ordre réel des blocs. Pour chaque `case`, notez ce qu'il fait (appel de méthode, affectation de variable, branchement conditionnel). Après quelques exécutions avec des entrées différentes, vous aurez une cartographie complète du flux de contrôle réel.

### Hooking avec Frida pour contourner la protection

Les techniques Frida du chapitre 13 s'appliquent directement aux assemblies .NET via **frida-clr** (détaillé au chapitre 32, section 32.2). Vous pouvez hooker les méthodes critiques sans modifier l'assembly :

- Hooker la méthode de déchiffrement de chaînes pour loguer automatiquement chaque chaîne déchiffrée au moment de son utilisation.  
- Hooker une méthode de validation pour forcer sa valeur de retour à `true`.  
- Hooker les méthodes anti-débogage (`Debugger.get_IsAttached`) pour retourner `false`.

L'avantage de Frida est qu'il opère au runtime sans modifier le fichier sur disque — les vérifications d'intégrité (anti-tampering) ne sont donc pas déclenchées.

### Nettoyage dans dnSpy par édition IL

Pour les cas où l'obfuscation est localisée à quelques méthodes critiques, il peut être plus rapide de nettoyer directement dans dnSpy plutôt que de chercher un outil automatique.

Ouvrez la méthode dans l'éditeur IL de dnSpy (`Edit IL Instructions`). Identifiez et supprimez :

- Les instructions `nop` superflues.  
- Les branchements vers des branchements (chaînes de `br`).  
- Les blocs de code mort (jamais atteints par le flux de contrôle).  
- Les variables locales jamais lues.

Remplacez les constructions aplaties par des branchements directs quand la cible est évidente. Cette approche est chirurgicale — vous ne nettoyez que ce dont vous avez besoin, ce qui est souvent suffisant pour comprendre la logique d'une ou deux méthodes clés.

### Écrire un déobfuscateur custom avec Mono.Cecil

Pour des besoins récurrents (analyser plusieurs versions du même logiciel protégé par le même obfuscateur), il peut être rentable d'écrire votre propre déobfuscateur en utilisant **Mono.Cecil**, une bibliothèque .NET de manipulation d'assemblies au niveau IL :

```csharp
// Principe d'un déobfuscateur de chaînes custom avec Mono.Cecil
var module = ModuleDefinition.ReadModule("ObfuscatedApp.exe");

foreach (var type in module.Types)
{
    foreach (var method in type.Methods)
    {
        if (!method.HasBody) continue;
        
        var il = method.Body.GetILProcessor();
        var instructions = method.Body.Instructions;
        
        for (int i = 0; i < instructions.Count - 1; i++)
        {
            // Chercher le pattern : ldc.i4 N → call DecryptString
            if (instructions[i].OpCode == OpCodes.Ldc_I4 &&
                instructions[i + 1].OpCode == OpCodes.Call &&
                IsDecryptMethod(instructions[i + 1].Operand))
            {
                int token = (int)instructions[i].Operand;
                string decrypted = DecryptString(token);
                
                // Remplacer par la chaîne en clair
                instructions[i].OpCode = OpCodes.Nop;
                instructions[i + 1].OpCode = OpCodes.Ldstr;
                instructions[i + 1].Operand = decrypted;
            }
        }
    }
}

module.Write("ObfuscatedApp-cleaned.exe");
```

Ce code est un squelette — l'implémentation réelle de `IsDecryptMethod` et `DecryptString` dépend de l'obfuscateur ciblé. Mais le principe est toujours le même : parcourir les instructions IL, détecter les patterns d'obfuscation, et les remplacer par leur équivalent en clair. C'est l'approche que de4dot utilise en interne, adaptée à votre cas spécifique.

---

## Workflow complet face à un assembly obfusqué

Pour synthétiser les techniques de cette section, voici le workflow recommandé face à un assembly .NET obfusqué.

### Étape 1 — Identification

Ouvrez l'assembly dans ILSpy. Notez les symptômes d'obfuscation : noms illisibles, chaînes absentes, flux de contrôle anormal. Vérifiez les attributs d'assembly. Lancez de4dot en mode détection (`de4dot --detect-only ObfuscatedApp.exe`).

### Étape 2 — Déobfuscation automatique

Lancez de4dot sur l'assembly. Ouvrez le résultat `-cleaned` dans ILSpy et évaluez la qualité du nettoyage : les chaînes sont-elles restaurées ? Le flux de contrôle est-il lisible ? Les noms sont-ils exploitables ? Si le résultat est satisfaisant, passez à l'analyse normale avec les workflows des sections 31.1 à 31.3.

### Étape 3 — Nettoyage complémentaire

Si de4dot n'a résolu qu'une partie du problème, identifiez les couches résiduelles. Appliquez les techniques manuelles appropriées : déchiffrement de chaînes par réflexion ou dnSpy, reconstruction du flux de contrôle par analyse dynamique, édition IL ciblée.

### Étape 4 — Analyse dynamique

Pour les parties qui résistent à toute déobfuscation statique, basculez en analyse dynamique pure. Utilisez dnSpy pour tracer l'exécution des méthodes obfusquées pas à pas. Les noms sont illisibles mais les **valeurs** à l'exécution ne mentent pas : vous voyez les chaînes déchiffrées, les résultats de calculs, les branchements effectivement pris. Complétez avec Frida (chapitre 32, section 32.2) pour hooker les méthodes critiques et loguer leur comportement.

### Étape 5 — Renommage progressif

Au fur et à mesure de votre compréhension, renommez les symboles dans ILSpy ou dnSpy. Chaque méthode comprise et renommée de manière descriptive (`DecryptString`, `ValidateLicense`, `CheckExpiry`) rend les méthodes voisines plus faciles à comprendre par effet de contexte. C'est exactement le même processus de renommage progressif que dans Ghidra sur un binaire natif (chapitre 8, section 8.4) — la patience et la rigueur font la différence.

---

## Contre-mesures aux protections anti-analyse

### Anti-tampering

Si l'assembly obfusqué vérifie sa propre intégrité au démarrage, toute modification (y compris celles de de4dot) provoque un crash. Deux stratégies :

**Neutralisation avant déobfuscation.** Identifiez la méthode de vérification d'intégrité (souvent appelée dans le constructeur de module `.cctor` ou dans un handler de l'événement `AppDomain.AssemblyLoad`). Utilisez dnSpy pour la localiser et la neutraliser (remplacer le corps par un `ret` en IL) *avant* de passer l'assembly à de4dot.

**Contournement dynamique.** Utilisez Frida ou un hook `LD_PRELOAD`-style (.NET equivalent : hook de méthode au runtime) pour neutraliser la vérification sans modifier le fichier. L'assembly sur disque reste intact, la vérification d'intégrité passe, mais votre hook force le résultat à « valide ».

### Anti-débogage

Les mécanismes de détection de débogueur .NET sont plus simples à contourner que leurs équivalents natifs (chapitre 19, section 19.7) car ils passent par des API .NET bien connues :

- `System.Diagnostics.Debugger.IsAttached` — hookable via Frida, ou contournable en modifiant la propriété par réflexion.  
- `Debugger.IsLogging()` — même approche.  
- Timing checks (`Stopwatch`, `DateTime.Now`) — identifiables dans le code décompilé et neutralisables par nop-out des vérifications.  
- `Environment.GetEnvironmentVariable("COR_ENABLE_PROFILING")` — neutralisable en définissant la variable à `0`.

Dans dnSpy, la méthode la plus directe est d'identifier ces vérifications dans le code décompilé et de les neutraliser par édition IL avant de lancer la session de débogage.

---

## Résumé

L'obfuscation est le seul véritable obstacle entre un reverse engineer et le code source d'une application .NET. de4dot automatise le contournement des obfuscateurs courants et constitue toujours la première étape à tenter. Quand l'automatisation ne suffit pas, les techniques manuelles — déchiffrement par réflexion, analyse dynamique avec dnSpy, hooking Frida, édition IL ciblée, scripting Mono.Cecil — prennent le relais. La clé est de combiner analyse statique et dynamique, en exploitant le fait que l'obfuscation masque la lisibilité mais ne modifie pas le comportement : ce que le code fait à l'exécution reste observable, quel que soit le degré d'obfuscation appliqué.

---


⏭️ [Chapitre 32 — Analyse dynamique et hooking .NET](/32-analyse-dynamique-dotnet/README.md)
