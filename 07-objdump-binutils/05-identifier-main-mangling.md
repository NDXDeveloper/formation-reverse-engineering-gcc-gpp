🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 7.5 — Identifier `main()` et les fonctions C++ (name mangling)

> 🔧 **Outils utilisés** : `objdump`, `readelf`, `nm`, `c++filt`, `strings`  
> 📦 **Binaires** : `keygenme_O0`, `keygenme_strip` (répertoire `binaries/ch07-keygenme/`) et `oop` (répertoire `binaries/ch22-oop/`)  
> 📝 **Syntaxe** : Intel (via `-M intel`)

---

## Trouver `main()` : le point de départ de toute analyse

La première question que se pose un reverse engineer face à un binaire inconnu est presque toujours : **où est `main()` ?** C'est là que commence la logique du programme — le code qui précède `main()` est de la mécanique d'initialisation du *runtime* C, rarement intéressant pour l'analyse. Trouver `main()` vous place directement au cœur du sujet.

Sur un binaire non strippé, c'est trivial :

```bash
$ objdump -d -M intel keygenme_O0 | grep '<main>'
00000000000011e2 <main>:
```

Il suffit de chercher le label. Mais sur un binaire strippé, `main` a disparu des symboles. Il faut le retrouver autrement.

---

## Méthode 1 : remonter depuis `_start` via `__libc_start_main`

Le point d'entrée réel d'un binaire ELF n'est pas `main()` — c'est `_start`. Même sur un binaire strippé, le point d'entrée est inscrit dans le header ELF et `readelf` le révèle :

```bash
$ readelf -h keygenme_strip | grep "Entry point"
  Entry point address:               0x1060
```

Désassemblons à cette adresse :

```bash
$ objdump -d -M intel --start-address=0x1060 --stop-address=0x1090 keygenme_strip
```

Vous trouverez la routine `_start`, générée par le *runtime* C (crt1.o). Sa structure est standardisée :

```asm
0000000000001060 <.text>:
    1060:       f3 0f 1e fa             endbr64
    1064:       31 ed                   xor    ebp, ebp
    1066:       49 89 d1                mov    r9, rdx
    1069:       5e                      pop    rsi
    106a:       48 89 e2                mov    rdx, rsp
    106d:       48 83 e4 f0             and    rsp, 0xfffffffffffffff0
    1071:       50                      push   rax
    1072:       54                      push   rsp
    1073:       45 31 c0                xor    r8d, r8d
    1076:       31 c9                   xor    ecx, ecx
    1078:       48 8d 3d 63 01 00 00    lea    rdi, [rip+0x163]
    107f:       ff 15 53 2f 00 00       call   QWORD PTR [rip+0x2f53]
    1085:       f4                      hlt
```

La clé est l'instruction `lea rdi, [rip+0x163]` à l'adresse `0x1078`. Elle charge dans `rdi` l'adresse de… `main()`. En effet, la convention du *runtime* C sous Linux est d'appeler `__libc_start_main` avec `main` comme premier argument (dans `rdi`). Le `call` qui suit (vers `__libc_start_main@plt` via la GOT) reçoit donc l'adresse de `main` dans `rdi`.

Calculons l'adresse : le `lea` est à `0x1078`, il fait 7 octets (donc l'instruction suivante est à `0x107f`), et le déplacement RIP-relatif est `0x163`. L'adresse de `main` est donc :

```
0x107f + 0x163 = 0x11e2
```

Vérifions sur la version non strippée : `main` est bien à `0x11e2`. La méthode fonctionne.

> 💡 **Cette technique est universelle** pour les binaires ELF liés dynamiquement avec la glibc. Le pattern peut varier légèrement selon la version du *runtime* C et les options de compilation (PIE vs non-PIE), mais le principe reste le même : `_start` charge l'adresse de `main` dans `rdi` avant d'appeler `__libc_start_main`. Cherchez le `lea rdi, [rip+...]` dans les premières instructions de `_start`.

### Automatiser avec un one-liner

Pour les pressés, voici une extraction rapide :

```bash
# Afficher les premières instructions à partir du point d'entrée
entry=$(readelf -h keygenme_strip | grep "Entry point" | awk '{print $4}')  
objdump -d -M intel --start-address=$entry --stop-address=$((entry + 0x40)) keygenme_strip  
```

Cherchez le `lea rdi` dans la sortie — l'adresse cible est votre `main`.

---

## Méthode 2 : chercher les chaînes caractéristiques

C'est une approche plus pragmatique, qui ne nécessite aucune connaissance du *runtime* C. La plupart des programmes affichent quelque chose à l'utilisateur : un message de bienvenue, un prompt, un usage, ou un message d'erreur. Ces chaînes se trouvent dans `.rodata` et sont référencées par le code via des `lea` RIP-relatifs.

```bash
$ strings keygenme_strip
...
Usage: %s <serial>  
Serial valide !  
Serial invalide.  
...
```

La chaîne `"Usage: %s <serial>"` est probablement utilisée dans `main()`, car c'est elle qui gère les arguments de la ligne de commande. Trouvons où elle est référencée :

```bash
$ objdump -d -M intel keygenme_strip | grep -B2 -A2 "Usage"
```

Cela ne fonctionnera pas directement — `objdump -d` n'affiche pas les chaînes en clair dans le désassemblage. Mais on peut procéder autrement : trouver l'adresse de la chaîne dans `.rodata`, puis chercher cette adresse dans le code.

```bash
# Trouver l'offset de la chaîne
$ objdump -s -j .rodata keygenme_strip | grep "Usage"
 2004 55736167 653a2025 73203c73 65726961  Usage: %s <seria
```

La chaîne est à l'adresse `0x2004`. Dans le code, elle sera chargée via un `lea` RIP-relatif. Cherchons dans le désassemblage les accès à `.rodata` suivis d'un `call` vers `printf@plt` ou `puts@plt` :

```bash
$ objdump -d -M intel keygenme_strip | grep -A3 "lea.*rdi"
```

Parmi les résultats, identifiez le `lea rdi, [rip+...]` dont le calcul d'adresse pointe vers `0x2004`. L'instruction qui contient ce `lea` se trouve dans `main()` — ou dans une fonction appelée directement par `main()` pour afficher l'usage.

Cette méthode demande un peu plus de travail, mais elle fonctionne même quand le binaire a un point d'entrée non standard ou quand `_start` a été personnalisé.

---

## Méthode 3 : identifier `main` par sa signature comportementale

Si les deux méthodes précédentes échouent ou si vous voulez confirmer votre trouvaille, `main()` a des caractéristiques reconnaissables :

**Elle reçoit `argc` et `argv`.** En convention System V, `main(int argc, char **argv)` reçoit `argc` dans `edi` et `argv` dans `rsi`. Vous verrez souvent une comparaison de `edi` avec une constante dans les premières instructions (vérification du nombre d'arguments) :

```asm
; Vérification argc == 2
cmp    edi, 0x2  
jne    <branche_usage>  
```

**Elle appelle des fonctions d'I/O.** La fonction `main()` d'un programme interactif appelle typiquement `printf`, `puts`, `scanf`, `fgets`, ou similaire dans les premières dizaines d'instructions. Si une fonction non identifiée fait un `call printf@plt` et un `cmp edi, 2` au début, c'est très probablement `main`.

**Elle se termine par un code de retour.** Avant son `ret`, `main()` charge généralement un petit entier dans `eax` : `0` pour le succès, `1` pour l'erreur. Souvent, les deux chemins de retour sont visibles :

```asm
; Chemin succès
xor    eax, eax                ; return 0  
jmp    .epilogue  

; Chemin erreur
mov    eax, 0x1                ; return 1

.epilogue:
leave  
ret  
```

---

## Les fonctions du *runtime* C : le bruit de fond

En cherchant `main()`, vous tomberez inévitablement sur de nombreuses fonctions qui ne vous intéressent pas. Savoir les reconnaître vous fera gagner du temps.

Sur un binaire ELF typique lié dynamiquement avec la glibc, la section `.text` contient, en plus de votre code, plusieurs fonctions injectées par le *runtime* C et le linker. Elles apparaissent systématiquement, même dans un `hello world` :

| Fonction | Rôle | Comment la reconnaître |  
|---|---|---|  
| `_start` | Point d'entrée ELF, appelle `__libc_start_main` | À l'adresse du *entry point*, contient `xor ebp, ebp` |  
| `_init` | Initialisation globale (section `.init`) | Désassemblée dans `Disassembly of section .init` |  
| `_fini` | Finalisation (section `.fini`) | Désassemblée dans `Disassembly of section .fini` |  
| `__do_global_dtors_aux` | Destruction des objets globaux C++ | Contient un accès à un flag `completed.0` en `.bss` |  
| `frame_dummy` | Prépare le cadre pour les destructeurs | Très courte, juste avant vos fonctions |  
| `register_tm_clones` | Gestion de la mémoire transactionnelle | Contient des accès à `__TMC_END__` |  
| `deregister_tm_clones` | Idem | Symétrique de la précédente |  
| Stubs PLT | Trampolines vers les fonctions dynamiques | Section `.plt`, identifiables par le `jmp QWORD PTR [rip+...]` |

Ces fonctions forment un « bruit de fond » qui encadre votre code. En `-O0`, votre première fonction utilisateur apparaît après `frame_dummy`. En pratique, si vous voyez `_start`, `deregister_tm_clones`, `register_tm_clones`, `__do_global_dtors_aux`, `frame_dummy`, puis un `endbr64`/`push rbp` — la fonction qui suit `frame_dummy` est votre première fonction utilisateur.

Sur un binaire strippé, ces fonctions de *runtime* sont toujours présentes mais non étiquetées. Vous les reconnaîtrez à leur position (tout début de `.text`), à leur brièveté, et au fait qu'elles ne font rien de « logique » du point de vue du programme. Sautez-les et concentrez-vous sur les fonctions qui viennent après.

---

## Le *name mangling* C++ : quand les noms deviennent cryptiques

Passons maintenant aux binaires C++. Désassemblons le binaire OOP du chapitre 22 :

```bash
$ objdump -d -M intel binaries/ch22-oop/oop | less
```

Vous découvrez des noms de fonctions comme :

```
0000000000001234 <_ZN6Animal5speakEv>:
00000000000012a0 <_ZN3Dog5speakEv>:
0000000000001310 <_ZN3Cat5speakEv>:
0000000000001380 <_ZN6AnimalC2ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi>:
0000000000001450 <_ZN3DogC1ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi>:
```

Ces noms ne sont pas du bruit aléatoire — c'est du **name mangling** (décoration de noms), un encodage systématique que le compilateur C++ utilise pour transformer les noms de fonctions en identifiants uniques.

### Pourquoi le name mangling existe

En C, chaque fonction a un nom unique dans le binaire : `compute_hash` en C devient `compute_hash` dans la table des symboles (parfois préfixé d'un `_` selon la plateforme). Il n'y a pas d'ambiguïté.

En C++, la surcharge de fonctions (*overloading*) permet d'avoir plusieurs fonctions avec le même nom mais des paramètres différents. Les méthodes appartiennent à des classes et des namespaces. Les templates génèrent des spécialisations multiples. Le linker, lui, travaille avec des noms plats — il a besoin d'un identifiant unique pour chaque fonction. Le *name mangling* encode toute l'information nécessaire (namespace, classe, nom de méthode, types des paramètres) dans un seul identifiant textuel.

### L'Itanium ABI : le standard sous GCC et Clang

GCC et Clang suivent l'**Itanium C++ ABI** pour le mangling. Les noms manglés commencent toujours par `_Z`, ce qui les rend immédiatement identifiables. La structure suit un schéma hiérarchique :

```
_Z <longueur><namespace/classe> <longueur><nom_méthode> <types_paramètres>
```

Décodons `_ZN6Animal5speakEv` morceau par morceau :

| Fragment | Signification |  
|---|---|  
| `_Z` | Préfixe de nom manglé C++ |  
| `N` | Début d'un nom qualifié (*nested name*) |  
| `6Animal` | Nom de 6 caractères : `Animal` |  
| `5speak` | Nom de 5 caractères : `speak` |  
| `E` | Fin du nom qualifié |  
| `v` | Type du paramètre : `void` (aucun paramètre) |

Le résultat décodé est : `Animal::speak()`.

Décodons un cas plus complexe — `_ZN6AnimalC2ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi` :

| Fragment | Signification |  
|---|---|  
| `_Z` | Préfixe manglé |  
| `N` | Début nom qualifié |  
| `6Animal` | Classe `Animal` |  
| `C2` | Constructeur de base (*base object constructor*) |  
| `E` | Fin du nom qualifié |  
| `NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE` | `std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>` — c'est-à-dire `std::string` |  
| `i` | `int` |

Le résultat décodé est : `Animal::Animal(std::string, int)` — le constructeur de `Animal` prenant un `std::string` et un `int`.

### Les encodages de types courants

Voici les codes de types que vous rencontrerez le plus souvent dans les noms manglés :

| Code | Type C++ |  
|---|---|  
| `v` | `void` |  
| `b` | `bool` |  
| `c` | `char` |  
| `i` | `int` |  
| `j` | `unsigned int` |  
| `l` | `long` |  
| `m` | `unsigned long` |  
| `f` | `float` |  
| `d` | `double` |  
| `PKc` | `const char*` (P = pointeur, K = const, c = char) |  
| `Ri` | `int&` (R = référence) |

### Codes spéciaux pour constructeurs et destructeurs

| Code | Signification |  
|---|---|  
| `C1` | Constructeur complet (*complete object constructor*) |  
| `C2` | Constructeur de base (*base object constructor*) |  
| `D0` | Destructeur de suppression (*deleting destructor*) |  
| `D1` | Destructeur complet (*complete object destructor*) |  
| `D2` | Destructeur de base (*base object destructor*) |

La distinction entre `C1`/`C2` et `D0`/`D1`/`D2` est liée à l'héritage virtuel. En pratique, pour la plupart des classes simples, GCC génère un `C1` qui appelle `C2`, et les deux ont un contenu quasiment identique. En RE, vous pouvez souvent les traiter comme le même constructeur.

### Opérateurs surchargés

Les opérateurs C++ ont des encodages spécifiques :

| Code | Opérateur |  
|---|---|  
| `ls` | `operator<<` (insertion de flux) |  
| `rs` | `operator>>` (extraction de flux) |  
| `pl` | `operator+` |  
| `eq` | `operator==` |  
| `cl` | `operator()` (appel de fonction) |  
| `ix` | `operator[]` (indexation) |

Vous les rencontrerez fréquemment dans les binaires C++ qui utilisent les flux (`std::cout << ...`) ou les conteneurs STL.

---

## Démanglement rapide dans `objdump`

Plutôt que de décoder manuellement chaque symbole, `objdump` offre une option intégrée :

```bash
objdump -d -M intel -C binaries/ch22-oop/oop | less
```

L'option **`-C`** (ou `--demangle`) active le démanglement automatique. Les labels deviennent lisibles :

```
0000000000001234 <Animal::speak()>:
00000000000012a0 <Dog::speak()>:
0000000000001310 <Cat::speak()>:
0000000000001380 <Animal::Animal(std::__cxx11::basic_string<...>, int)>:
```

La différence de lisibilité est spectaculaire. C'est une option à activer **systématiquement** sur les binaires C++ :

```bash
# Alias recommandé pour le travail quotidien sur du C++
alias objdump='objdump -M intel -C'
```

L'option `-C` fonctionne aussi avec `nm` :

```bash
$ nm -C binaries/ch22-oop/oop | grep ' T '
0000000000001234 T Animal::speak()
00000000000012a0 T Dog::speak()
0000000000001310 T Cat::speak()
...
```

Et avec `readelf` (via l'option `--demangle` ou `-C` selon les versions).

---

## Identifier les composants C++ dans un listing `objdump`

Même sans démangler, certains patterns dans les noms manglés vous donnent des indices immédiats sur la nature du code :

### Repérer les classes

Cherchez les préfixes communs dans les noms manglés :

```bash
$ nm binaries/ch22-oop/oop | grep '_ZN' | sed 's/_ZN\([0-9]*\)\(.*\)/\2/' | sort -u
```

Ou plus simplement, avec démanglement :

```bash
$ nm -C binaries/ch22-oop/oop | grep '::' | awk -F'::' '{print $1}' | sort -u
```

Cela vous donne la liste des classes (et namespaces) présents dans le binaire.

### Repérer les vtables et le RTTI

Les vtables apparaissent dans les symboles sous le mangling `_ZTV` :

```bash
$ nm binaries/ch22-oop/oop | grep '_ZTV'
0000000000003d00 V _ZTV6Animal
0000000000003d28 V _ZTV3Cat
0000000000003d50 V _ZTV3Dog
```

Le RTTI (*Run-Time Type Information*) apparaît sous `_ZTI` (typeinfo) et `_ZTS` (typeinfo name) :

```bash
$ nm binaries/ch22-oop/oop | grep '_ZTI\|_ZTS'
0000000000002050 V _ZTI6Animal
0000000000002068 V _ZTI3Cat
0000000000002080 V _ZTI3Dog
0000000000002040 V _ZTS6Animal
0000000000002048 V _ZTS3Cat
0000000000002050 V _ZTS3Dog
```

Ces informations sont précieuses : même sur un binaire strippé partiellement (où `.symtab` a disparu mais les symboles dynamiques restent), les vtables et le RTTI peuvent subsister si les classes sont exportées ou utilisées avec `dynamic_cast`. Les chaînes RTTI (`_ZTS`) contiennent les noms des classes en clair dans `.rodata` :

```bash
$ strings binaries/ch22-oop/oop | grep -E '^[0-9]+[A-Z]'
6Animal
3Cat
3Dog
```

Ces chaînes (format `<longueur><nom>`) sont les noms de types du RTTI. Même sur un binaire complètement strippé, `strings` peut les révéler — c'est un filon inattendu pour retrouver les noms de classes.

### Repérer les constructeurs et destructeurs

Les motifs `C1`, `C2` (constructeurs) et `D0`, `D1`, `D2` (destructeurs) dans les noms manglés sont des cibles prioritaires en RE :

```bash
# Constructeurs
$ nm binaries/ch22-oop/oop | grep '_ZN.*C[12]E'

# Destructeurs
$ nm binaries/ch22-oop/oop | grep '_ZN.*D[012]E'
```

Analyser un constructeur vous révèle l'initialisation des membres de la classe (et donc leur type et leur offset dans la structure). Analyser un destructeur vous montre les ressources libérées (et donc ce que la classe possède).

### Repérer les fonctions de la STL

Les noms manglés contenant `St` (pour `std::`) ou `NSt` (pour un nom qualifié dans `std::`) proviennent de la bibliothèque standard C++. Vous verrez beaucoup de :

- `_ZNSt7__cxx1112basic_stringI...` → méthodes de `std::string`  
- `_ZNSt6vectorI...` → méthodes de `std::vector`  
- `_ZNSolsI...` → `std::ostream::operator<<`  
- `_ZStlsI...` → `operator<<` free-function pour les flux

En RE, ces fonctions STL sont généralement du « bruit » que vous voulez traverser rapidement pour atteindre la logique métier. Les reconnaître au premier coup d'œil (même sous forme manglée) vous fait gagner beaucoup de temps.

---

## Le cas du binaire C++ strippé

Sur un binaire C++ strippé, les noms manglés de `.symtab` ont disparu. Cependant :

**Les symboles dynamiques survivent.** Si le binaire est lié dynamiquement à `libstdc++`, les appels aux fonctions de la STL restent visibles dans `.dynsym` et dans la PLT :

```bash
$ nm -D binaries/ch22-oop/oop_strip | c++filt
                 U std::basic_ostream<char, std::char_traits<char> >& std::operator<< <...>(...)
                 U operator new(unsigned long)
                 U operator delete(void*)
                 U __cxa_atexit
                 U __cxa_begin_catch
                 U __cxa_end_catch
```

Les symboles `__cxa_*` sont caractéristiques du *runtime* C++ et confirment que le binaire est du C++ — même si aucun nom de classe ou de méthode utilisateur n'est visible. Ces indicateurs vous permettent d'adapter votre approche avant même de commencer le désassemblage : vous savez que vous aurez affaire à des vtables, du dispatch virtuel, des exceptions, et potentiellement de la STL.

**Les chaînes RTTI peuvent survivre au stripping.** Comme mentionné plus haut, les noms de types utilisés par `dynamic_cast` et `typeid` sont des chaînes stockées dans `.rodata`. Elles ne font pas partie de `.symtab` et survivent donc à `strip`. Sur un binaire C++ strippé, lancez `strings` et cherchez les patterns `<nombre><Nom>` :

```bash
$ strings binaries/ch22-oop/oop_strip | grep -E '^[0-9]+[A-Z][a-z]'
6Animal
3Cat
3Dog
```

Vous venez de retrouver les noms de classes sans aucun symbole. Ce n'est pas garanti (si le programme est compilé avec `-fno-rtti`, ces chaînes n'existent pas), mais c'est suffisamment fréquent pour mériter d'être systématiquement vérifié.

**Les vtables restent dans la section `.data.rel.ro` ou `.rodata`.** Même strippées de leurs noms, les vtables sont des tableaux de pointeurs de fonctions situés dans les sections de données en lecture seule. On les repère en cherchant des séquences de pointeurs qui pointent vers la section `.text`. Le chapitre 17 (section 17.2) couvre la reconstruction de vtables en détail — pour l'instant, retenez que leur présence trahit du polymorphisme C++.

---

## Reconnaître un binaire C++ sans symboles : les indices convergents

Comment savoir qu'un binaire strippé est du C++ et non du C pur ? Voici une checklist rapide de signes révélateurs, même en l'absence totale de symboles :

1. **Appels à `__cxa_*` dans la PLT** : `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch`, `__cxa_atexit` — ces fonctions font partie du *runtime* C++ et n'existent pas dans un programme C pur.

2. **Appels à `operator new` et `operator delete` dans la PLT** : allocation C++ (au lieu de `malloc`/`free`).

3. **Chaînes RTTI dans `.rodata`** : format `<longueur><NomDeClasse>`, comme `6Animal`, `3Dog`.

4. **Sections `.gcc_except_table` et `.eh_frame`** : gestion des exceptions C++. La section `.eh_frame` existe aussi en C (pour le stack unwinding), mais `.gcc_except_table` est spécifique au C++.

5. **Liens vers `libstdc++.so`** : vérifiable avec `ldd` ou `readelf -d`.

6. **Appels indirects via des tableaux de pointeurs en `.data.rel.ro`** : pattern caractéristique du dispatch virtuel (appel à travers la vtable).

7. **Fonctions appelées juste après un chargement de pointeur depuis un objet** : le pattern `mov rax, QWORD PTR [rdi]` suivi de `call QWORD PTR [rax+N]` est un appel de méthode virtuelle. Le premier `mov` charge le vptr de l'objet, le second appelle la N-ième entrée de la vtable.

Aucun de ces indices n'est une preuve absolue pris isolément, mais leur combinaison est quasi-certaine. Si vous voyez des `__cxa_*`, des `operator new`, des chaînes RTTI et des appels indirects via vtable, vous avez affaire à du C++ — adaptez votre stratégie d'analyse en conséquence.

---

## Résumé

Trouver `main()` sur un binaire strippé repose sur trois techniques complémentaires : remonter depuis le point d'entrée `_start` en lisant le `lea rdi` qui charge l'adresse de `main` avant l'appel à `__libc_start_main` ; chercher les chaînes caractéristiques dans `.rodata` (messages d'usage, prompts) et tracer leur référence dans le code ; identifier la signature comportementale de `main` (vérification d'`argc`, appels d'I/O, codes de retour). Pour les binaires C++, le *name mangling* Itanium encode les noms de classes, de méthodes et les types de paramètres dans des identifiants commençant par `_Z`, décodables automatiquement avec l'option `-C` d'`objdump` ou `nm`. Les préfixes `_ZTV` (vtables), `_ZTI`/`_ZTS` (RTTI), et les codes `C1`/`C2`/`D0`/`D1`/`D2` (constructeurs/destructeurs) sont des repères essentiels. Même sur un binaire C++ strippé, les symboles dynamiques (`__cxa_*`, `operator new`), les chaînes RTTI, et les patterns de dispatch virtuel permettent d'identifier le langage et de commencer à reconstruire la hiérarchie de classes.

---


⏭️ [`c++filt` — démanglement des symboles C++](/07-objdump-binutils/06-cppfilt-demanglement.md)
