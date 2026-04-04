🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 30.4 — Inspecter un assembly avec `file`, `strings` et ImHex (headers PE/.NET)

> 📚 **Objectif de cette section** — Appliquer le workflow de triage rapide du chapitre 5 à un assembly .NET, en réutilisant les outils que vous connaissez déjà (`file`, `strings`, `xxd`, ImHex) et en les complétant par quelques commandes spécifiques au monde .NET.

---

## Le réflexe du triage : les mêmes 5 premières minutes

La section 5.7 a introduit un workflow de « triage rapide » : une routine systématique à appliquer dans les cinq premières minutes face à un binaire inconnu. L'objectif est de répondre aux questions fondamentales — quel type de fichier, quel architecture, quelles dépendances, quelles chaînes intéressantes, quelles protections — avant d'engager une analyse approfondie.

Ce workflow reste valable sur un assembly .NET. Les outils sont les mêmes ; ce qui change, c'est l'interprétation des résultats. Déroulons chaque étape sur un assembly fictif `CrackMe.exe` compilé en C# avec `dotnet build`.

## Étape 1 — `file` : identifier la nature du binaire

```
$ file CrackMe.exe
CrackMe.exe: PE32 executable (console) Intel 80386 Mono/.Net assembly, for MS Windows
```

Trois informations cruciales dans cette sortie :

**`PE32 executable`** — Le fichier est au format PE (Portable Executable). Ce n'est pas un ELF. Si vous êtes sous Linux et que vous avez l'habitude de voir `ELF 64-bit LSB executable`, cette sortie confirme immédiatement que vous n'êtes pas face à un binaire natif Linux classique.

**`Intel 80386`** — Ne vous laissez pas tromper par cette mention. Elle indique l'architecture déclarée dans le PE Header, pas l'architecture réelle d'exécution. Les assemblies .NET « Any CPU » (le mode par défaut) déclarent souvent `Intel 80386` (PE32) ou `x86-64` (PE32+) dans leur header, mais le bytecode CIL qu'ils contiennent est indépendant de l'architecture. C'est le JIT du CLR qui produit le code machine adapté à la plateforme cible au runtime.

**`Mono/.Net assembly`** — C'est le marqueur décisif. `file` a détecté la présence du CLR Header (Data Directory index 14 dans le PE Optional Header). Ce binaire contient du bytecode CIL et des metadata .NET. À partir de ce constat, vous savez que les outils d'analyse natifs (`objdump -d`, Ghidra en mode x86) ne seront pas pertinents — il faut basculer vers les outils .NET (ILSpy, dnSpy, `monodis`).

> ⚠️ **Cas particulier — l'exécutable hôte .NET 6+** : les projets .NET modernes (`dotnet publish`) produisent parfois un exécutable natif Linux (ELF) qui sert de lanceur pour le runtime. Dans ce cas, `file` affiche `ELF 64-bit LSB executable` — sans mention de .NET. Le vrai assembly se trouve dans un fichier `.dll` adjacent (par exemple `CrackMe.dll`). Si vous trouvez un ELF accompagné d'un `.dll` du même nom et d'un fichier `.runtimeconfig.json`, c'est un indice fort que vous êtes face à une application .NET avec un hôte natif. Le triage doit alors porter sur la `.dll`, pas sur l'ELF.

```
$ ls -la
-rwxr-xr-x  1 user user  143360  CrackMe          ← Hôte natif (ELF)
-rw-r--r--  1 user user    8704  CrackMe.dll       ← Assembly .NET (CIL)
-rw-r--r--  1 user user     253  CrackMe.runtimeconfig.json
-rw-r--r--  1 user user  143808  CrackMe.deps.json

$ file CrackMe
CrackMe: ELF 64-bit LSB pie executable, x86-64, ...

$ file CrackMe.dll
CrackMe.dll: PE32 executable (console) Intel 80386 Mono/.Net assembly, for MS Windows
```

## Étape 2 — `strings` : extraire les chaînes lisibles

```
$ strings CrackMe.exe
```

Sur un assembly .NET non obfusqué, `strings` produit une sortie considérablement plus riche que sur un binaire natif strippé. Voici les catégories de chaînes que vous allez rencontrer, dans l'ordre typique de leur apparition dans le fichier :

**Chaînes du DOS Stub** — Le classique `This program cannot be run in DOS mode` apparaît en début de fichier. Ignorez-le.

**Noms de types et de méthodes** — Proviennent du heap `#Strings` des metadata (section 30.2). Ce sont les identifiants du programme : noms de classes, de méthodes, de champs, de namespaces, de paramètres.

```
CrackMe  
Program  
Main  
LicenseChecker  
ValidateKey  
_secretSalt
System.Runtime  
System.Console  
WriteLine  
ReadLine  
```

Sur un binaire natif strippé, ces informations auraient été perdues. Ici, elles vous donnent immédiatement une vue d'ensemble de l'architecture du programme : une classe `Program` avec un `Main`, une classe `LicenseChecker` avec une méthode `ValidateKey` et un champ `_secretSalt`. En quelques secondes, vous avez une hypothèse de travail sur la logique du crackme.

**Chaînes utilisateur** — Proviennent du heap `#US` (User Strings). Ce sont les littéraux du code source : messages affichés, clés, URLs, formats.

```
Enter your license key:  
Invalid key. Try again.  
License activated successfully!  
SHA256  
HMAC  
```

Ces chaînes révèlent le comportement du programme de manière explicite. La présence de `SHA256` et `HMAC` indique l'utilisation de fonctions cryptographiques dans la vérification de licence — une information qu'il faudrait des heures pour extraire d'un binaire natif optimisé.

**Noms d'assemblies référencées** — Les noms des dépendances apparaissent en clair.

```
System.Runtime  
System.Console  
System.Security.Cryptography  
```

La présence de `System.Security.Cryptography` confirme l'hypothèse crypto.

**Marqueurs d'obfuscation éventuels** — C'est le moment d'appliquer l'étape 1 de la stratégie d'identification de la section 30.3.

```
$ strings CrackMe.exe | grep -iE "confuser|dotfuscator|smartassembly|preemptive|reactor"
```

Une sortie vide suggère un assembly non obfusqué (ou un obfuscateur qui ne laisse pas de marqueur textuel).

> 💡 **Astuce** — Sur un assembly .NET, pensez à utiliser `strings` avec l'option `-e l` (little-endian 16 bits) pour capturer les chaînes encodées en UTF-16LE du heap `#US`, que `strings` en mode par défaut (ASCII/UTF-8) pourrait manquer :  
>  
> ```  
> $ strings -e l CrackMe.exe  
> Enter your license key:  
> Invalid key. Try again.  
> License activated successfully!  
> ```

> 💡 **Parallèle ELF** — Sur un binaire natif, `strings` capture essentiellement le contenu de `.rodata` et les noms de symboles (si non strippé). Sur un assembly .NET, la sortie est systématiquement plus dense car les metadata ne peuvent pas être strippées — elles font partie intégrante du format.

## Étape 3 — `xxd` / `hexdump` : vérifier les magic bytes

Un coup d'œil rapide aux premiers octets confirme le format :

```
$ xxd CrackMe.exe | head -4
00000000: 4d5a 9000 0300 0000 0400 0000 ffff 0000  MZ..............
00000010: b800 0000 0000 0000 4000 0000 0000 0000  ........@.......
00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000030: 0000 0000 0000 0000 0000 0000 8000 0000  ................
```

Le magic `MZ` (`4D 5A`) à l'offset `0x00` confirme un PE. L'offset `0x3C` (ici la valeur `0x80`) pointe vers le PE Header :

```
$ xxd -s 0x80 -l 4 CrackMe.exe
00000080: 5045 0000                                PE..
```

La signature `PE\0\0` (`50 45 00 00`) est présente. Pour confirmer que c'est bien un assembly .NET et pas un PE natif, il faut vérifier le Data Directory à l'index 14 (CLI Header). Son offset dans le fichier dépend de la taille de l'Optional Header, mais le principe est simple : si ce Data Directory contient un RVA non nul, le fichier a un CLR Header.

C'est ici qu'ImHex devient plus confortable que `xxd` pour l'exploration manuelle.

## Étape 4 — ImHex : exploration structurée des headers

ImHex (chapitre 6) est l'outil idéal pour explorer visuellement la structure d'un assembly .NET. Chargez le fichier et utilisez les fonctionnalités suivantes :

### Navigation manuelle avec le Data Inspector

Placez le curseur à l'offset `0x00` et observez le Data Inspector d'ImHex interpréter les premiers octets comme un DOS Header. Naviguez jusqu'au PE Header (offset indiqué par `e_lfanew` à `0x3C`), puis parcourez le COFF Header et l'Optional Header.

Dans l'Optional Header, repérez le **tableau des Data Directories**. Le 15ᵉ entrée (index 14, « CLI Header ») est celle qui nous intéresse. Si elle contient un RVA et une taille non nuls, vous avez la confirmation définitive que le fichier est un assembly .NET — et le RVA vous indique où trouver le CLR Header dans le fichier.

### Localiser le magic BSJB

Le bloc de metadata est identifiable par son magic `BSJB` (`42 53 4A 42`). Utilisez la fonction de recherche hexadécimale d'ImHex (Ctrl+F → onglet Hex) pour trouver cette séquence :

```
Recherche : 42 53 4A 42  
Résultat  : trouvé à l'offset 0x0268 (exemple)  
```

À partir de ce magic, vous êtes au début du Metadata Root Header. Les octets suivants donnent la version du format metadata (typiquement `v4.0.30319` pour les assemblies .NET Framework, ou `v5.0`, `v6.0`, `v8.0` pour .NET moderne) :

```
Offset 0x0268: 42 53 4A 42    ← Magic "BSJB"  
Offset 0x026C: 01 00 01 00    ← Version majeure/mineure  
Offset 0x0270: 00 00 00 00    ← Réservé  
Offset 0x0274: 0C 00 00 00    ← Longueur de la chaîne de version (12)  
Offset 0x0278: 76 34 2E 30 2E 33 30 33 31 39 00 00  ← "v4.0.30319\0\0"  
```

Cette chaîne de version vous donne une indication sur le framework ciblé. Après la chaîne de version, le header liste les streams (nombre, offset relatif, taille, nom) — ce qui vous permet de localiser précisément chaque heap (`#~`, `#Strings`, `#US`, `#Blob`, `#GUID`) dans le fichier.

### Écrire un pattern `.hexpat` pour le CLR Header

Si vous souhaitez aller plus loin avec ImHex, vous pouvez écrire un pattern `.hexpat` qui parse automatiquement le CLR Header. Voici un pattern minimal couvrant les champs principaux :

```c
// Pattern ImHex pour le CLR Header (.NET COR20)
// Positionnez le curseur au début du CLR Header avant d'exécuter

struct CLR_Header {
    u32 cb;                    // Taille de la structure (72)
    u16 majorRuntimeVersion;
    u16 minorRuntimeVersion;
    u32 metadataRVA;
    u32 metadataSize;
    u32 flags;                 // 0x01 = ILONLY, 0x02 = 32BITREQUIRED, ...
    u32 entryPointToken;       // Token MethodDef de Main()
    u32 resourcesRVA;
    u32 resourcesSize;
    u32 strongNameRVA;
    u32 strongNameSize;
    u32 codeManagerTableRVA;
    u32 codeManagerTableSize;
    u32 vTableFixupsRVA;
    u32 vTableFixupsSize;
    u32 exportAddressTableRVA;
    u32 exportAddressTableSize;
    u32 managedNativeHeaderRVA;
    u32 managedNativeHeaderSize;
};

CLR_Header clr_header @ 0x0208;  // ← Ajustez cet offset selon votre binaire
```

Une fois le pattern appliqué, ImHex affiche chaque champ avec son nom, sa valeur et sa position — vous pouvez immédiatement lire le `flags` pour vérifier `ILONLY`, le `entryPointToken` pour identifier `Main()`, et les RVA des metadata et ressources.

> 💡 **Rappel** — La section 6.4 a montré comment écrire un pattern `.hexpat` pour un header ELF. La démarche est identique ici : définir une `struct` C-like qui reflète le layout binaire, puis l'ancrer à l'offset correct dans le fichier. La conversion RVA → offset fichier nécessite de consulter la table des sections du PE pour trouver le mapping — une étape que les décompilateurs .NET font automatiquement, mais qu'il est utile de savoir faire manuellement pour les cas atypiques.

### Visualiser l'entropie

La vue d'entropie d'ImHex (menu Analysis → Entropy) donne un aperçu rapide des zones chiffrées ou compressées. Sur un assembly .NET non obfusqué, l'entropie est relativement homogène et modérée (entre 4 et 6 bits/octet). Si vous observez des blocs d'entropie élevée (proche de 8 bits/octet), c'est un signe de :

- Chiffrement de chaînes (section 30.3) — bloc localisé dans la zone des ressources ou des metadata.  
- Packing de méthodes CIL — bloc couvrant une grande partie de la section `.text`.  
- Ressources chiffrées ou compressées — bloc dans la zone pointée par le champ `Resources` du CLR Header.

Comparez mentalement avec le profil d'entropie d'un binaire natif packé par UPX (section 29.1) : la signature est similaire — des zones à haute entropie indiquent du contenu transformé.

## Étape 5 — Outils spécifiques .NET : `monodis` et `dotnet`

Une fois la nature .NET confirmée par les étapes précédentes, des outils spécifiques complètent le triage.

### `monodis` — le « objdump » du monde .NET

`monodis` (fourni avec Mono) est l'équivalent fonctionnel d'`objdump` pour les assemblies .NET. Il désassemble le CIL et permet d'inspecter les metadata.

**Lister les types définis :**

```
$ monodis --typedef CrackMe.exe
Typedef Table
  1: (TypeDef) CrackMe.Program (Flags: 00100000)
  2: (TypeDef) CrackMe.LicenseChecker (Flags: 00100000)
```

**Lister les méthodes :**

```
$ monodis --method CrackMe.exe
Method Table
  1: void CrackMe.Program::Main(string[])  (param: 1, flags: 00000096, implflags: 0000)
  2: void CrackMe.Program::.ctor()  (param: 0, flags: 00001886, implflags: 0000)
  3: bool CrackMe.LicenseChecker::ValidateKey(string)  (param: 1, flags: 00000086, implflags: 0000)
  4: void CrackMe.LicenseChecker::.ctor()  (param: 0, flags: 00001886, implflags: 0000)
```

**Lister les assemblies référencées :**

```
$ monodis --assemblyref CrackMe.exe
AssemblyRef Table
  1: Version=8.0.0.0  Name=System.Runtime
  2: Version=8.0.0.0  Name=System.Console
  3: Version=8.0.0.0  Name=System.Security.Cryptography
```

**Désassembler le CIL complet :**

```
$ monodis CrackMe.exe > CrackMe.il
```

Le fichier `.il` résultant contient le désassemblage CIL de l'assembly entier, dans un format textuel lisible. C'est l'équivalent de `objdump -d` sur un ELF — utile pour une inspection rapide, mais un décompilateur (ILSpy, dnSpy) est préférable pour une analyse approfondie.

### `dotnet` CLI — informations de runtime

Si le SDK .NET est installé, la commande `dotnet` peut fournir des informations supplémentaires :

```
$ dotnet --list-runtimes
Microsoft.NETCore.App 8.0.4 [/usr/share/dotnet/shared/Microsoft.NETCore.App]
```

Cela vous indique quels runtimes sont disponibles sur la machine, ce qui est utile pour savoir si vous pouvez exécuter l'assembly cible (analyse dynamique, chapitre 32).

## Résumé du workflow de triage .NET

Voici la séquence complète, condensée en un aide-mémoire :

```
1.  file target.exe / target.dll
    → Confirmer "Mono/.Net assembly"
    → Si ELF : chercher une .dll adjacente + .runtimeconfig.json

2.  strings target.dll
    strings -e l target.dll
    → Noms de classes/méthodes (heap #Strings)
    → Chaînes utilisateur (heap #US)
    → Marqueurs d'obfuscation (grep confuser|dotfuscator|smart...)

3.  xxd target.dll | head
    → Confirmer magic MZ (4D 5A) + PE (50 45 00 00)
    → Chercher magic BSJB (42 53 4A 42) pour les metadata

4.  ImHex
    → Data Inspector sur les headers PE + CLR Header
    → Recherche du magic BSJB, lecture de la version metadata
    → Pattern .hexpat sur le CLR Header (flags, entryPointToken)
    → Vue entropie : détecter chiffrement/packing

5.  monodis --typedef --method --assemblyref target.dll
    → Inventaire des types, méthodes, dépendances
    → Désassemblage CIL si besoin (monodis target.dll)

6.  [Optionnel] de4dot --detect target.dll
    → Identification automatique de l'obfuscateur
```

À l'issue de ces étapes — qui prennent moins de cinq minutes — vous disposez d'un profil complet de l'assembly : son framework cible, ses types et méthodes, ses dépendances, la présence ou l'absence d'obfuscation, et une première idée de sa logique interne. Vous êtes prêt à passer à la décompilation (chapitre 31) ou à l'analyse dynamique (chapitre 32) en connaissance de cause.

---

> 📖 **À retenir** — Le triage d'un assembly .NET suit la même logique que celui d'un binaire ELF natif : identifier le format, extraire les chaînes, inspecter les headers, évaluer les protections. Les outils de base (`file`, `strings`, `xxd`, ImHex) fonctionnent directement ; `monodis` et `de4dot` les complètent pour les aspects spécifiques au CLR. Le point clé est la détection initiale avec `file` : la mention `Mono/.Net assembly` (ou la présence d'une `.dll` avec `.runtimeconfig.json` pour les déploiements .NET modernes) oriente immédiatement vers la boîte à outils .NET.

---


⏭️ [NativeAOT et ReadyToRun : quand le C# devient du code natif](/30-introduction-re-dotnet/05-nativeaot-readytorun.md)
