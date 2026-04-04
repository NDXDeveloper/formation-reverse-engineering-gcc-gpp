🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Chapitre 34 — Reverse Engineering de binaires Go

> 🐹 *Go produit des binaires ELF natifs liés statiquement, embarquant un runtime complet. Leur reverse engineering est déroutant au premier contact, mais les structures internes du langage — une fois comprises — deviennent paradoxalement un allié puissant pour l'analyste.*

---

## Pourquoi un chapitre dédié à Go ?

Go (parfois appelé Golang) est devenu un langage de prédilection pour le développement d'outils système, d'infrastructure cloud, de CLI et — il faut le reconnaître — de malwares modernes. Des projets comme Docker, Kubernetes, Terraform ou Caddy sont écrits en Go. Côté offensif, de nombreuses familles de malwares récentes (Bazar, Sunshuttle, Kaiji, certains ransomwares) ont adopté Go pour sa facilité de cross-compilation et la taille imposante de ses binaires, qui complique l'analyse.

En tant que reverse engineer, vous tomberez inévitablement sur un binaire Go. Et la première rencontre est souvent déconcertante : le binaire pèse plusieurs mégaoctets même pour un simple « Hello, World! », `objdump` affiche des milliers de fonctions inconnues, le décompilateur de Ghidra produit un pseudo-code difficilement lisible, et les conventions d'appel ne ressemblent pas à ce que vous connaissez du C ou du C++.

Ce chapitre vous donne les clés pour transformer cette complexité apparente en avantage.

---

## Ce qui rend les binaires Go si différents

### Un runtime embarqué complet

Contrairement à un programme C qui délègue l'essentiel au noyau et à la libc, un binaire Go embarque son propre runtime : un scheduler de goroutines, un ramasse-miettes (garbage collector), un allocateur mémoire, une gestion de pile extensible et l'ensemble de la bibliothèque standard utilisée. Un `main.main()` de trois lignes tire derrière lui des centaines de fonctions internes préfixées `runtime.`, `sync.`, `fmt.`, `os.`, etc. C'est ce qui explique la taille caractéristique des binaires Go — souvent entre 2 et 15 Mo pour un outil en ligne de commande — et le bruit considérable dans la liste des fonctions.

### Liaison statique par défaut

Par défaut, le compilateur Go produit un binaire lié statiquement. Il n'y a pas de dépendance sur `libc.so` ni sur d'autres bibliothèques partagées (sauf activation explicite de CGo). En conséquence, `ldd` vous répondra `not a dynamic executable`, et le binaire embarquera ses propres wrappers autour des appels système Linux via `runtime.syscall` ou `syscall.Syscall6`. Cette autonomie complique le triage initial : les outils comme `ltrace` ne captureront rien, et `strace` restera votre principal allié côté dynamique.

### Des conventions d'appel non standard

Jusqu'à Go 1.16 inclus, Go utilisait une convention d'appel entièrement basée sur la pile — tous les arguments et toutes les valeurs de retour transitaient par la stack, sans utiliser les registres `rdi`, `rsi`, `rdx` que vous attendriez dans du code suivant la System V AMD64 ABI. Depuis Go 1.17, une convention basée sur les registres a été introduite, plus proche (mais pas identique) à ce que fait le C. En pratique, vous rencontrerez les deux variantes selon la version du compilateur, ce qui affecte directement la façon dont vous lirez le désassemblage.

### Des métadonnées riches, même après stripping

C'est le paradoxe de Go : même un binaire strippé conserve des structures internes riches. La table `gopclntab` (Go PC-Line Table), les informations de type `runtime._type`, et les tables de modules permettent souvent de retrouver les noms de fonctions, les correspondances ligne-par-ligne et les définitions de types — des informations que le stripping d'un binaire C détruirait définitivement. Ces métadonnées, prévues pour le runtime (stack traces, garbage collector, reflection), deviennent un cadeau pour le reverse engineer qui sait où chercher.

---

## Prérequis pour ce chapitre

Ce chapitre suppose que vous êtes à l'aise avec les notions couvertes dans les parties précédentes, en particulier :

- le désassemblage et la navigation dans Ghidra (chapitre 8),  
- les bases de l'assembleur x86-64 et les conventions d'appel System V (chapitre 3),  
- l'utilisation de GDB pour l'analyse dynamique (chapitre 11),  
- les concepts de stripping et de reconstruction de symboles (chapitre 19).

Aucune connaissance préalable du langage Go n'est strictement nécessaire, mais une familiarité basique avec sa syntaxe (fonctions, goroutines, slices, interfaces) facilitera grandement la compréhension. Si vous n'avez jamais écrit de Go, un rapide parcours du *Tour of Go* (tour.golang.org) en une heure suffit pour acquérir le vocabulaire.

---

## Binaire d'entraînement

Le binaire utilisé tout au long de ce chapitre est `crackme_go`, dont les sources se trouvent dans `binaries/ch34-go/crackme_go/main.go`. Le `Makefile` associé produit plusieurs variantes :

| Variante | Description |  
|---|---|  
| `crackme_go` | Binaire standard avec symboles |  
| `crackme_go_strip` | Binaire strippé (`strip -s`) |  
| `crackme_go_upx` | Binaire strippé puis compressé avec UPX |  
| `crackme_go_nopie` | Binaire sans PIE (adresses fixes, facilite l'analyse statique) |  
| `crackme_go_race` | Binaire avec détecteur de data races (instrumentation runtime supplémentaire) |

Compilez l'ensemble avec `make` depuis le répertoire `binaries/ch34-go/`. Vous pouvez vérifier la version du compilateur Go embarquée dans le binaire avec `go version crackme_go` ou en cherchant la chaîne `go1.` dans la sortie de `strings`.

---

## Plan du chapitre

| Section | Thème |  
|---|---|  
| 34.1 | Spécificités du runtime Go : goroutines, scheduler, GC |  
| 34.2 | Convention d'appel Go (stack-based puis register-based depuis Go 1.17) |  
| 34.3 | Structures de données Go en mémoire : slices, maps, interfaces, channels |  
| 34.4 | Récupérer les noms de fonctions : `gopclntab` et `go_parser` pour Ghidra/IDA |  
| 34.5 | Strings en Go : structure `(ptr, len)` et implications pour `strings` |  
| 34.6 | Stripped Go binaries : retrouver les symboles via les structures internes |  
| 🎯 | **Checkpoint** : analyser un binaire Go strippé, retrouver les fonctions et reconstruire la logique |

---

## Conseils avant de commencer

**Ne paniquez pas devant le volume.** Un binaire Go typique contient des milliers de fonctions, mais l'immense majorité appartient au runtime et à la bibliothèque standard. Votre cible réelle — le code métier écrit par le développeur — se cache généralement dans le package `main` et quelques packages internes. Apprendre à filtrer le bruit pour se concentrer sur le signal est la compétence centrale de ce chapitre.

**Pensez en termes de packages, pas de fichiers.** L'unité d'organisation en Go est le package, et cela se reflète directement dans les noms de symboles : `main.checkLicense`, `crypto/aes.newCipher`, `net/http.(*Client).Do`. Cette convention de nommage hiérarchique — quand elle est accessible — est une mine d'or pour comprendre l'architecture d'un programme Go sans ses sources.

**Outillez-vous.** L'écosystème RE a rattrapé son retard sur Go. Des scripts comme `go_parser` pour IDA, le module `GoReSym` de Mandiant, ou les analyseurs intégrés aux dernières versions de Ghidra facilitent considérablement le travail. Nous les verrons en détail dans la section 34.4.

⏭️ [Spécificités du runtime Go : goroutines, scheduler, GC](/34-re-go/01-runtime-goroutines-gc.md)
