🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Partie VII — Bonus : RE sur Binaires .NET / C#

Le C# et l'écosystème .NET sont omniprésents en entreprise — applications métier, outils internes, services Windows, jeux Unity. En tant que reverseur de binaires natifs, vous croiserez inévitablement des assemblies .NET : soit comme cibles directes, soit comme composants d'une architecture mixte où le C# appelle du code natif via P/Invoke. Et avec l'arrivée de NativeAOT et ReadyToRun, la frontière entre bytecode managé et code natif se brouille — un même binaire peut contenir les deux. Cette partie vous donne les clés pour aborder le RE .NET avec confiance, en capitalisant sur tout ce que vous savez déjà du RE natif.

---

## 🎯 Objectifs de cette partie

À l'issue de ces trois chapitres, vous serez capable de :

1. **Comprendre la structure d'un assembly .NET** : metadata, headers PE, sections CIL, et ce qui le distingue d'un binaire natif ELF ou PE classique.  
2. **Décompiler un assembly C# avec ILSpy, dnSpy et dotPeek**, obtenir un code source quasi-identique à l'original, et choisir le bon outil selon le contexte (décompilation seule, débogage intégré, navigation de code).  
3. **Contourner les obfuscateurs .NET courants** (ConfuserEx, Dotfuscator, SmartAssembly) avec de4dot et des techniques manuelles, pour retrouver un code lisible.  
4. **Déboguer et instrumenter un assembly .NET à la volée** : poser des breakpoints sur du C# décompilé dans dnSpy, hooker des méthodes .NET avec `frida-clr`, intercepter les appels P/Invoke vers des bibliothèques natives, et patcher l'IL directement.  
5. **Identifier quand un binaire .NET devient du code natif** (NativeAOT, ReadyToRun) et adapter votre approche — savoir quand basculer vers les outils de RE natif vus dans les Parties II-IV.

---

## 📋 Chapitres

| N° | Titre | Description | Lien |  
|----|-------|-------------|------|  
| 30 | Introduction au RE .NET | Bytecode CIL vs code natif x86-64, structure d'un assembly .NET (metadata, PE headers, sections CIL), obfuscateurs courants (ConfuserEx, Dotfuscator, SmartAssembly), NativeAOT et ReadyToRun. | [Chapitre 30](/30-introduction-re-dotnet/README.md) |  
| 31 | Décompilation d'assemblies .NET | ILSpy (open source), dnSpy/dnSpyEx (décompilation + débogage), dotPeek (JetBrains), comparatif des trois outils, de4dot et techniques de contournement d'obfuscation. | [Chapitre 31](/31-decompilation-dotnet/README.md) |  
| 32 | Analyse dynamique et hooking .NET | Débogage dans dnSpy sans les sources, hooking de méthodes .NET avec Frida (`frida-clr`), interception d'appels P/Invoke (pont .NET → natif), patching de l'IL avec dnSpy, cas pratique de contournement d'une vérification de licence C#. | [Chapitre 32](/32-analyse-dynamique-dotnet/README.md) |

---

## 🔄 Pont avec le RE natif

Si vous avez suivi les Parties I à IV, vous avez déjà les réflexes fondamentaux — le RE .NET les transpose dans un monde plus confortable. Le bytecode CIL est un assembleur de haut niveau : là où le x86-64 vous donne des `mov` et des `lea`, le CIL manipule directement des objets, des appels de méthodes typés et une pile d'évaluation explicite. Conséquence directe : la décompilation .NET produit un code source quasi-identique à l'original, là où Ghidra vous donne un pseudo-C approximatif. Le vrai point de jonction entre les deux mondes est P/Invoke : quand un programme C# appelle une DLL native compilée avec GCC, vous retrouvez exactement les techniques de RE natif des Parties II-IV pour analyser cette DLL, et les techniques .NET de cette partie pour comprendre comment elle est appelée.

---

## 🛠️ Outils couverts

- **ILSpy** — décompilateur C# open source, référence pour la décompilation d'assemblies .NET.  
- **dnSpy / dnSpyEx** — décompilateur avec débogueur intégré : breakpoints, inspection de variables et patching IL directement sur le C# décompilé.  
- **dotPeek** (JetBrains) — décompilateur avec navigation avancée et export de sources, intégré à l'écosystème JetBrains.  
- **de4dot** — désobfuscateur .NET automatique, supporte ConfuserEx, Dotfuscator, SmartAssembly et d'autres.  
- **Frida (`frida-clr`)** — instrumentation dynamique du runtime .NET (CLR), hooking de méthodes C# à la volée.  
- **ImHex** — inspection des headers PE/.NET et des sections CIL au niveau hexadécimal.  
- **`file` / `strings`** — premiers réflexes de triage, applicables aussi aux assemblies .NET.

---

## ⏱️ Durée estimée

**~10-14 heures** pour un praticien du RE natif ayant des bases en C#.

Le chapitre 30 (introduction, ~2-3h) pose le cadre conceptuel et les différences avec le natif. Le chapitre 31 (décompilation, ~3-4h) est le plus outillé : vous prendrez en main trois décompilateurs et apprendrez à contourner l'obfuscation. Le chapitre 32 (analyse dynamique, ~4-5h) culmine avec un cas pratique complet de contournement de licence — le pendant .NET du keygenme du chapitre 21.

Si vous n'avez pas de background C#, ajoutez ~3-4h pour vous familiariser avec la syntaxe et les concepts de base du langage (classes, propriétés, delegates, LINQ). La connaissance du C++ acquise dans les parties précédentes accélère considérablement cette prise en main.

---

## 📌 Prérequis

**Obligatoires :**

- Avoir complété au minimum les chapitres 1 et 2 de la **[Partie I](/partie-1-fondamentaux.md)** (concepts de RE, chaîne de compilation, formats binaires) — vous devez comprendre ce qu'est un binaire et comment il est produit.  
- Avoir des **bases en C#** : syntaxe, classes, héritage, interfaces, propriétés. Pas besoin d'être un expert — vous lirez du code décompilé, pas vous n'en écrirez des milliers de lignes.

**Recommandé :**

- Avoir complété les **[Partie II](/partie-2-analyse-statique.md)** et **[Partie III](/partie-3-analyse-dynamique.md)** — les concepts de désassemblage, décompilation et hooking sont les mêmes, seuls les outils changent.  
- Avoir complété le chapitre 13 (Frida) — `frida-clr` utilise la même API et les mêmes principes que le hooking natif.

---

## ⬅️ Partie précédente

← [**Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**](/partie-6-malware.md)

## ➡️ Partie suivante

Dernier module bonus : le Reverse Engineering de binaires Rust et Go — deux langages qui produisent des ELF natifs via la toolchain GNU mais dont les conventions, le name mangling et les structures internes posent des défis spécifiques.

→ [**Partie VIII — Bonus : RE de Binaires Rust et Go**](/partie-8-rust-go.md)

⏭️ [Chapitre 30 — Introduction au RE .NET](/30-introduction-re-dotnet/README.md)
