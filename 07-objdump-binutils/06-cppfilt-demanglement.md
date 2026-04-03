🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 7.6 — `c++filt` — démanglement des symboles C++

> 🔧 **Outils utilisés** : `c++filt`, `nm`, `objdump`, `readelf`  
> 📦 **Binaires** : `oop` (répertoire `binaries/ch22-oop/`)  
> 📝 **Syntaxe** : Intel (via `-M intel`)

---

## Pourquoi une section dédiée à `c++filt` ?

La section 7.5 a introduit le *name mangling* et montré que l'option `-C` d'`objdump` et de `nm` le décode automatiquement. Alors pourquoi consacrer une section entière à `c++filt` ?

Parce que `-C` ne couvre pas tous les scénarios. Vous rencontrerez des noms manglés **en dehors** de `objdump` et `nm` : dans des logs de crash, dans la sortie de `readelf`, dans des rapports de Valgrind, dans des traces `strace`/`ltrace`, dans des fichiers texte exportés par Ghidra, dans des sorties de scripts Python, dans des messages d'erreur du linker, ou tout simplement copiés-collés dans un terminal. `c++filt` est l'outil dédié au démanglement autonome : il prend un nom manglé en entrée et produit sa version lisible en sortie, indépendamment de tout autre outil.

C'est aussi un outil que vous pouvez intégrer dans des **pipelines shell**, ce qui le rend indispensable dès que vous scriptez vos analyses.

---

## Utilisation de base

### Mode argument direct

La forme la plus simple : passer un ou plusieurs noms manglés comme arguments.

```bash
$ c++filt _ZN6Animal5speakEv
Animal::speak()

$ c++filt _ZN3DogC1ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi
Dog::Dog(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, int)
```

Vous pouvez passer plusieurs symboles d'un coup :

```bash
$ c++filt _ZN6Animal5speakEv _ZN3Cat5speakEv _ZN3Dog5speakEv
Animal::speak()  
Cat::speak()  
Dog::speak()  
```

Chaque nom est démanglé indépendamment, un par ligne.

### Mode filtre (stdin)

C'est le mode le plus puissant. `c++filt` lit son entrée standard ligne par ligne et remplace **chaque nom manglé** qu'il trouve dans le texte, en laissant le reste intact. Cela signifie que vous pouvez le brancher en fin de n'importe quel pipeline sans perturber le formatage de la sortie :

```bash
$ nm binaries/ch22-oop/oop | c++filt
```

Le résultat est identique à `nm -C`, mais la mécanique est différente : ici, c'est `nm` qui produit sa sortie brute (avec noms manglés), et `c++filt` qui la transforme en aval. Cette approche est plus flexible car elle fonctionne avec **n'importe quelle source de texte**, pas seulement les outils qui supportent `-C`.

Le filtre est intelligent : il reconnaît les noms manglés au milieu d'une ligne de texte et ne touche pas au reste. Par exemple :

```bash
$ echo "La fonction _ZN6Animal5speakEv est appelée ici" | c++filt
La fonction Animal::speak() est appelée ici
```

Le texte environnant est préservé, seul le nom manglé est remplacé. C'est ce qui rend `c++filt` utilisable sur des sorties arbitraires : logs, rapports d'erreur, exports de désassembleurs, commentaires, etc.

---

## Intégration dans les pipelines d'analyse

### Avec `readelf`

Contrairement à `objdump` et `nm`, certaines versions de `readelf` ne disposent pas d'option `-C`. Le pipe vers `c++filt` résout le problème :

```bash
# Symboles dynamiques démanglés
$ readelf --dyn-syms binaries/ch22-oop/oop | c++filt

# Table des symboles complète démanglée
$ readelf -s binaries/ch22-oop/oop | c++filt
```

### Avec `objdump` (quand `-C` ne suffit pas)

Parfois, vous voulez post-traiter un listing `objdump` déjà sauvegardé dans un fichier. Plutôt que de le régénérer avec `-C`, filtrez le fichier existant :

```bash
$ c++filt < oop_disasm.asm > oop_disasm_demangled.asm
```

### Avec `addr2line`

L'outil `addr2line`, qui convertit des adresses en noms de fichiers et numéros de ligne, produit des noms manglés par défaut. Le pipe vers `c++filt` rend la sortie lisible :

```bash
$ addr2line -f -e binaries/ch22-oop/oop 0x1234 | c++filt
Animal::speak()
/home/user/oop.cpp:42
```

(`addr2line` accepte aussi l'option `-C` directement, mais la version pipe fonctionne avec toutes les versions.)

### Avec des logs de crash et backtraces

Les backtraces produites par `glibc` (via `backtrace_symbols()`), par GDB, ou par les gestionnaires de crash contiennent souvent des noms manglés :

```
#3  0x00005555555552a0 in _ZN3Dog5speakEv ()
#4  0x0000555555555380 in _ZN6AnimalC2ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi ()
```

Passez la backtrace entière dans `c++filt` :

```bash
$ cat crash.log | c++filt
#3  0x00005555555552a0 in Dog::speak() ()
#4  0x0000555555555380 in Animal::Animal(std::__cxx11::basic_string<...>, int) ()
```

La backtrace devient instantanément compréhensible.

### Avec `grep` pour filtrer puis démangler

L'ordre du pipeline compte. Si vous cherchez un symbole manglé spécifique, filtrez **avant** de démangler (la recherche sur le nom manglé est plus précise) :

```bash
# Chercher toutes les méthodes de la classe Animal, puis démangler
$ nm binaries/ch22-oop/oop | grep '_ZN6Animal' | c++filt
0000000000001234 T Animal::speak()
0000000000001380 T Animal::Animal(std::__cxx11::basic_string<...>, int)
00000000000013f0 T Animal::~Animal()
```

Si vous cherchez par nom démanglé (par exemple toutes les fonctions contenant `speak`), démanglez **avant** de filtrer :

```bash
# Démangler puis chercher par nom lisible
$ nm binaries/ch22-oop/oop | c++filt | grep 'speak'
0000000000001234 T Animal::speak()
00000000000012a0 T Dog::speak()
0000000000001310 T Cat::speak()
```

La nuance est subtile mais importante dans les pipelines complexes.

---

## Options de `c++filt`

`c++filt` dispose de quelques options utiles au-delà du comportement par défaut.

### `-t` : démangler les types individuels

Par défaut, `c++filt` s'attend à des noms de fonctions ou de variables manglés (commençant par `_Z`). L'option `-t` lui permet de démangler aussi les **encodages de types isolés**, comme ceux qu'on trouve dans les signatures de fonctions :

```bash
$ c++filt -t i
int

$ c++filt -t PKc
char const*

$ c++filt -t NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >
```

C'est très utile quand vous décomposez manuellement un nom manglé et que vous voulez vérifier votre interprétation d'un fragment de type.

### `-p` : ne pas afficher les types des paramètres

L'option `-p` (*no-params*) produit un démanglement simplifié qui omet les types des paramètres :

```bash
$ c++filt -p _ZN6AnimalC2ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi
Animal::Animal(...)

# Comparaison sans -p :
$ c++filt _ZN6AnimalC2ENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEi
Animal::Animal(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, int)
```

Quand les types de la STL rendent les noms illisiblement longs, `-p` offre une vue d'ensemble plus claire. Vous perdez l'information de surcharge (vous ne distinguez plus les constructeurs qui diffèrent par leurs paramètres), mais vous gagnez en lisibilité.

### `-s` : spécifier le schéma de mangling

Par défaut, `c++filt` utilise le schéma de la plateforme (Itanium ABI sous Linux/GCC). Si vous travaillez sur un binaire provenant d'un autre compilateur, vous pouvez forcer un schéma différent :

```bash
# Itanium ABI (GCC, Clang) — défaut sous Linux
$ c++filt -s gnu-v3 _ZN6Animal5speakEv

# Ancien schéma GCC 2.x (très rare aujourd'hui)
$ c++filt -s gnu _ZN6Animal5speakEv
```

En pratique, vous n'aurez presque jamais besoin de changer le schéma sous Linux. Cette option devient pertinente si vous analysez un binaire compilé avec un compilateur exotique ou très ancien.

> 💡 **Note sur MSVC** : le compilateur de Microsoft utilise son propre schéma de *name mangling*, totalement différent de l'Itanium ABI. Les noms manglés MSVC commencent par `?` (par exemple `?speak@Animal@@UEAAXXZ`). `c++filt` ne supporte **pas** le démanglement MSVC. Pour les binaires Windows, utilisez `undname.exe` (outil Microsoft), `llvm-undname` (projet LLVM), ou le démangleur intégré de Ghidra/IDA. Ce cas reste hors du périmètre de ce tutoriel, centré sur la chaîne GNU, mais mérite d'être mentionné pour éviter toute confusion si vous croisez des symboles `?...@@...`.

---

## Construire un inventaire de classes avec `nm` + `c++filt`

En combinant les outils vus jusqu'ici, vous pouvez extraire rapidement la structure objet d'un binaire C++. Voici un mini-workflow scriptable :

### Lister toutes les classes

```bash
$ nm -C binaries/ch22-oop/oop | grep ' T ' | awk -F'::' 'NF>1 {print $1}' | \
    sed 's/^.* //' | sort -u
Animal  
Cat  
Dog  
```

Le `grep ' T '` filtre les fonctions définies dans `.text`. Le `awk` extrait ce qui précède le `::`. Le `sed` retire l'adresse et le type de symbole. Le `sort -u` déduplique.

### Lister les méthodes d'une classe donnée

```bash
$ nm -C binaries/ch22-oop/oop | grep ' T .*Animal::'
0000000000001234 T Animal::speak()
0000000000001380 T Animal::Animal(std::__cxx11::basic_string<...>, int)
0000000000001390 T Animal::Animal(std::__cxx11::basic_string<...>, int)
00000000000013f0 T Animal::~Animal()
0000000000001410 T Animal::~Animal()
```

Les doublons apparents (`Animal::Animal` deux fois, `~Animal` deux fois) correspondent aux variantes `C1`/`C2` et `D1`/`D2` mentionnées en section 7.5.

### Lister les vtables et le RTTI

```bash
$ nm -C binaries/ch22-oop/oop | grep -E 'vtable|typeinfo'
0000000000003d00 V vtable for Animal
0000000000003d28 V vtable for Cat
0000000000003d50 V vtable for Dog
0000000000002050 V typeinfo for Animal
0000000000002068 V typeinfo for Cat
0000000000002080 V typeinfo for Dog
0000000000002040 V typeinfo name for Animal
0000000000002048 V typeinfo name for Cat
0000000000002050 V typeinfo name for Dog
```

Avec le démanglement, `_ZTV` devient `vtable for ...`, `_ZTI` devient `typeinfo for ...`, et `_ZTS` devient `typeinfo name for ...`. Le listing est directement lisible.

### Générer un résumé structuré

Combinons le tout dans un script one-liner qui produit un inventaire hiérarchique :

```bash
$ nm -C binaries/ch22-oop/oop | grep ' T ' | grep '::' | \
    awk -F'::' '{class=$1; sub(/^.* /, "", class); method=$2; print class " → " method}' | \
    sort
Animal → Animal(std::__cxx11::basic_string<...>, int)  
Animal → speak()  
Animal → ~Animal()  
Cat → Cat(std::__cxx11::basic_string<...>, int)  
Cat → speak()  
Cat → ~Cat()  
Dog → Dog(std::__cxx11::basic_string<...>, int)  
Dog → speak()  
Dog → ~Dog()  
```

En quelques secondes, vous avez une vue d'ensemble de la hiérarchie de classes : trois classes (`Animal`, `Cat`, `Dog`), chacune avec un constructeur, un destructeur, et une méthode `speak()` — probablement virtuelle vu qu'elle existe dans chaque classe. Ce résumé, produit avant même d'ouvrir un désassembleur graphique, oriente déjà votre analyse.

---

## Les limites du démanglement

Le démanglement n'est pas une solution miracle. Voici les cas où il ne vous aidera pas :

**Binaire strippé sans symboles dynamiques C++.** Si le binaire est lié statiquement (pas de `libstdc++.so`) et complètement strippé, il n'y a plus aucun nom manglé à démangler. `c++filt` n'a rien sur quoi travailler. Dans ce cas, il faut recourir aux techniques du chapitre 17 : reconstruire les classes depuis les vtables, les patterns d'allocation (`operator new` suivi d'un constructeur), et le layout mémoire des objets.

**Templates profondément imbriqués.** Le démanglement produit des noms techniquement corrects mais parfois illisibles à cause de la verbosité des types template de la STL :

```bash
$ c++filt _ZNSt8__detail9_Map_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS6_iESaIS9_ENS_10_Select1stESt8equal_toIS6_ESt4hashIS6_ENSA_18_Mod_range_hashingENSA_20_Default_ranged_hashENSA_20_Prime_rehash_policyENSA_17_Hashtable_traitsILb1ELb0ELb1EEELb1EEixERS8_
```

Le résultat démanglé fait plusieurs lignes et mentionne une demi-douzaine de types template. En pratique, la réaction appropriée est de reconnaître qu'il s'agit d'une méthode interne de `std::unordered_map` et de passer à la suite. L'option `-p` aide, mais ne résout pas totalement le problème. L'habitude et la connaissance de la STL sont vos meilleurs alliés.

**Symboles obfusqués volontairement.** Certains obfuscateurs post-compilation remplacent les noms de symboles par des identifiants aléatoires qui ne suivent pas le schéma Itanium. `c++filt` les laisse inchangés puisqu'ils ne commencent pas par `_Z`. Il n'y a pas de parade automatique — c'est l'objectif même de l'obfuscation.

**Fonctions `extern "C"` dans du C++.** Les fonctions déclarées `extern "C"` en C++ ne sont **pas** manglées — elles utilisent le nom C brut, sans préfixe `_Z`. C'est volontaire (pour permettre l'interopérabilité C/C++), mais cela signifie que ces fonctions ne sont pas identifiables comme du C++ par leur nom seul. Si un binaire C++ expose une API en `extern "C"`, les fonctions de l'API ressembleront à du C dans la table des symboles.

---

## Résumé

`c++filt` est l'outil de démanglement autonome de la suite Binutils. Il fonctionne en mode argument direct (passer un symbole sur la ligne de commande) ou en mode filtre (lire stdin et remplacer les noms manglés dans le texte). Son mode filtre le rend indispensable dans les pipelines shell : combiné avec `nm`, `readelf`, `addr2line`, ou des fichiers de logs, il transforme des identifiants cryptiques en noms C++ lisibles. L'option `-t` décode les encodages de types isolés, et `-p` produit un démanglement simplifié sans les types de paramètres. Associé à `nm -C` et à quelques commandes `grep`/`awk`, il permet de dresser rapidement l'inventaire des classes, méthodes, vtables et RTTI d'un binaire C++. Ses limites apparaissent sur les binaires strippés sans symboles résiduels, sur les templates profondément imbriqués (verbosité), et face à l'obfuscation délibérée.

---


⏭️ [Limitations d'`objdump` : pourquoi un vrai désassembleur est nécessaire](/07-objdump-binutils/07-limitations-objdump.md)
