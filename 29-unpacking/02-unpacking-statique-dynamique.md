🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 29.2 — Unpacking statique (UPX) et dynamique (dump mémoire avec GDB)

> 🎯 **Objectif de cette section** — Maîtriser les deux grandes familles de techniques d'unpacking : l'approche **statique**, qui exploite l'outil du packer lui-même ou reproduit son algorithme hors exécution, et l'approche **dynamique**, qui laisse le stub faire le travail puis capture le résultat en mémoire. On appliquera les deux sur nos binaires `packed_sample_upx` (UPX standard) et `packed_sample_upx_tampered` (UPX altéré).

---

## Deux philosophies, un même objectif

L'unpacking consiste à récupérer le code original du programme tel qu'il existe en mémoire après décompression. Deux chemins mènent à ce résultat :

- **L'unpacking statique** travaille sur le fichier, sans jamais l'exécuter. On utilise soit la commande de décompression fournie par le packer (quand elle existe et fonctionne), soit un script qui réimplémente l'algorithme de décompression en analysant le format du fichier packé. L'avantage est la sécurité — aucun code potentiellement malveillant ne s'exécute sur la machine. L'inconvénient est que cette approche ne fonctionne que si l'on connaît (ou peut identifier) le packer et son format.

- **L'unpacking dynamique** exécute le binaire dans un environnement contrôlé et intercepte l'état de la mémoire une fois le stub terminé. Le stub fait tout le travail de décompression ; l'analyste n'a qu'à capturer le résultat au bon moment. L'avantage est l'universalité — la technique fonctionne quel que soit le packer, y compris les packers custom ou volontairement altérés. L'inconvénient est que le binaire s'exécute réellement, d'où la nécessité absolue de travailler dans une VM sandboxée (chapitre 26).

Dans la pratique, on tente toujours l'approche statique en premier. Si elle échoue (packer inconnu, signatures altérées, packer custom), on bascule sur l'approche dynamique.

---

## Partie A — Unpacking statique avec UPX

### Le cas simple : `upx -d`

UPX fournit nativement une option de décompression. C'est le scénario le plus favorable :

```
$ cp packed_sample_upx packed_sample_upx_restored
$ upx -d packed_sample_upx_restored
                       Ultimate Packer for eXecutables
                          Copyright (C) 1996 - 2024
UPX 4.2.2       Markus Oberhumer, Laszlo Molnar & John Reese

        File size         Ratio      Format      Name
   --------------------   ------   -----------   -----------
     18472 <-      7152   38.72%   linux/amd64   packed_sample_upx_restored

Unpacked 1 file.
```

UPX lit ses propres métadonnées de compression (le header `l_info`/`p_info` repéré grâce au magic `UPX!`), décompresse les données, reconstruit les sections originales et restaure le point d'entrée. Le binaire résultant est fonctionnellement identique au binaire pré-packing.

On peut vérifier immédiatement le résultat :

```
$ strings packed_sample_upx_restored | grep FLAG
FLAG{unp4ck3d_and_r3c0nstruct3d}

$ readelf -S packed_sample_upx_restored | head -5
There are 27 section headers, starting at offset 0x3a08:
```

Les chaînes sont de retour, les 27 sections standard sont restaurées, et `checksec` affiche à nouveau les protections du binaire original. L'unpacking statique avec UPX est aussi simple que cela — quand les signatures sont intactes.

### Quand `upx -d` échoue

Tentons la même commande sur notre variante altérée :

```
$ cp packed_sample_upx_tampered test_restore
$ upx -d test_restore
                       Ultimate Packer for eXecutables
                          Copyright (C) 1996 - 2024
UPX 4.2.2       Markus Oberhumer, Laszlo Molnar & John Reese

        File size         Ratio      Format      Name
   --------------------   ------   -----------   -----------
upx: test_restore: NotPackedException: not packed by UPX

Unpacked 0 files.
```

UPX recherche son magic `UPX!` pour localiser ses structures internes. Comme nous avons remplacé `UPX!` par `FKP!` dans le Makefile, UPX ne reconnaît plus son propre format et refuse la décompression. C'est une technique triviale à mettre en œuvre (quelques octets modifiés) mais suffisante pour bloquer l'outil automatique.

### Contournement : restaurer les signatures manuellement

Avant de passer à l'approche dynamique, on peut tenter une réparation statique. Si l'on sait que le binaire a été packé avec UPX puis altéré, il suffit de restaurer les magic bytes originaux. On connaît les substitutions effectuées par notre Makefile (`FKP!` → `UPX!`, `XP_0` → `UPX0`, etc.), mais en situation réelle, il faut les deviner.

La démarche consiste à ouvrir le binaire dans ImHex et à rechercher les patterns candidats. On sait que le magic UPX se trouve généralement vers la fin du fichier et que les noms de sections sont dans la table des section headers. En recherchant `FKP!` dans ImHex :

```
$ python3 -c "
data = open('packed_sample_upx_tampered','rb').read()  
idx = data.find(b'FKP!')  
print(f'FKP! trouvé à l\'offset 0x{idx:X}')  
"
FKP! trouvé à l'offset 0x1BD0
```

On remplace `FKP!` par `UPX!` (et les noms de sections si nécessaire), on sauvegarde, et on retente `upx -d`. Cette approche fonctionne pour UPX car le format de compression sous-jacent n'a pas changé — seules les signatures de reconnaissance ont été altérées.

Cependant, cette méthode a des limites évidentes. Si le packer a modifié non seulement les signatures mais aussi la structure des métadonnées, ou s'il s'agit d'un packer entièrement custom, la réparation statique est impossible. C'est là qu'intervient l'approche dynamique.

---

## Partie B — Unpacking dynamique avec GDB

L'unpacking dynamique repose sur un principe fondamental : **quel que soit le packer, le code original finit par exister en clair dans la mémoire du processus**. Le stub décompresse le code, puis transfère le contrôle au point d'entrée original (Original Entry Point, ou **OEP**). Si l'on parvient à interrompre l'exécution juste après ce transfert, on peut dumper les régions mémoire contenant le code décompressé.

### Étape 1 — Comprendre le flux d'exécution du stub

Avant de poser des breakpoints, il est utile de comprendre ce que fait le stub UPX à haut niveau. La séquence typique est la suivante :

1. Le loader Linux mappe les segments du fichier packé en mémoire.  
2. L'exécution commence au point d'entrée du stub (`e_entry` dans le header ELF).  
3. Le stub décompresse les données du segment compressé vers le segment de destination (le segment `LOAD` avec le grand `MemSiz`).  
4. Le stub restaure les imports dynamiques si nécessaire (résolution des symboles PLT/GOT).  
5. Le stub effectue un saut (`jmp` ou `call` suivi de `ret`) vers l'OEP — le vrai point d'entrée du programme original.  
6. Le programme original s'exécute normalement.

L'objectif est d'intercepter l'exécution entre les étapes 5 et 6.

### Étape 2 — Identifier l'OEP avec GDB

Lançons le binaire packé sous GDB (avec GEF ou pwndbg installé) :

```
$ gdb -q ./packed_sample_upx_tampered
```

On commence par examiner le point d'entrée du fichier :

```
gef➤ info file  
Entry point: 0x4013e8  
```

C'est le point d'entrée du **stub**, pas du programme original. Posons un breakpoint dessus et lançons l'exécution :

```
gef➤ break *0x4013e8  
gef➤ run  
```

À ce stade, on est au tout début du stub de décompression. L'approche pour trouver l'OEP varie selon le packer, mais pour UPX, plusieurs techniques sont éprouvées.

#### Technique A — Chercher le dernier `jmp` du stub

Le stub UPX se termine invariablement par un saut inconditionnel vers l'OEP. Ce `jmp` est le dernier transfert de contrôle avant que le programme original ne prenne la main. On peut le repérer en exécutant le stub pas à pas avec `stepi`, mais c'est fastidieux. Une approche plus efficace consiste à poser un **breakpoint matériel en exécution** sur une adresse mémoire que l'on sait appartenir au code décompressé.

#### Technique B — Breakpoint sur le segment de destination

Avec `readelf -l`, on a noté que le segment de destination (celui avec le grand `MemSiz`) commence à une certaine adresse. Le code original sera décompressé vers cette zone. On pose un breakpoint matériel en exécution sur les premiers octets de cette zone :

```
gef➤ hbreak *0x401000  
gef➤ continue  
```

Le breakpoint matériel (`hbreak` et non `break`) est essentiel ici. Un breakpoint logiciel (`break`) écrit une instruction `int3` à l'adresse cible, mais cette zone mémoire va être écrasée par le stub au moment de la décompression, ce qui effacerait le breakpoint. Un breakpoint matériel utilise les registres de débogage du processeur (DR0–DR3) et survit aux écritures mémoire.

Lorsque le breakpoint est atteint, on se trouve aux premières instructions du code original décompressé. Le `rip` pointe vers l'OEP :

```
gef➤ info registers rip  
rip    0x401000  
```

#### Technique C — Breakpoint sur `__libc_start_main`

Si le programme original est dynamiquement lié à la libc (ce qui n'est pas le cas avec UPX standard, mais peut l'être avec d'autres packers), on peut poser un breakpoint sur `__libc_start_main`. Cette fonction est appelée par le code de démarrage CRT (`_start`), et son premier argument (dans `rdi` selon la convention System V AMD64) est l'adresse de `main()`. En inspectant `rdi` au moment de l'appel, on obtient directement l'adresse de `main` dans le code décompressé.

#### Technique D — Catch syscall execve

Certains packers (pas UPX, mais des packers plus complexes) décompressent le binaire dans un fichier temporaire puis l'exécutent via `execve`. Dans ce cas, un `catch syscall execve` dans GDB interceptera le moment du lancement du « vrai » binaire.

### Étape 3 — Mapper les régions mémoire décompressées

Une fois à l'OEP, on examine la carte mémoire du processus :

```
gef➤ vmmap
```

Avec GEF ou pwndbg, `vmmap` affiche toutes les régions mémoire du processus avec leurs permissions et la source du mapping (fichier ou anonyme). On repère les régions qui contiennent le code et les données décompressées. Typiquement :

```
Start              End                Perm  Name
0x0000000000400000 0x0000000000401000 r--p  packed_sample_upx_tampered
0x0000000000401000 0x0000000000404000 r-xp  packed_sample_upx_tampered
0x0000000000404000 0x0000000000405000 r--p  packed_sample_upx_tampered
0x0000000000405000 0x0000000000406000 rw-p  packed_sample_upx_tampered
```

Les régions pertinentes sont celles marquées `r-xp` (code exécutable décompressé) et `rw-p` (données décompressées). On note les adresses de début et de fin de chaque région.

> 💡 **Astuce GEF** — La commande `xinfo <adresse>` donne des détails sur la région mémoire contenant une adresse donnée. Utile pour vérifier qu'une adresse appartient bien au code décompressé et non au stub.

### Étape 4 — Dumper la mémoire

GDB permet de sauvegarder des régions de mémoire dans des fichiers avec la commande `dump` :

```
gef➤ dump binary memory code.bin 0x401000 0x404000  
gef➤ dump binary memory data_ro.bin 0x404000 0x405000  
gef➤ dump binary memory data_rw.bin 0x405000 0x406000  
```

Chaque commande crée un fichier binaire contenant une copie exacte de la mémoire entre les deux adresses spécifiées. Ces fichiers contiennent le code machine décompressé et les données du programme original.

On peut aussi dumper l'intégralité de l'espace mémoire mappé en une seule commande avec pwndbg :

```
pwndbg> dumpmem /tmp/full_dump/ --writable --executable
```

Ou utiliser la méthode `/proc` depuis un second terminal (en connaissant le PID du processus arrêté sous GDB) :

```
$ cat /proc/<pid>/maps
$ dd if=/proc/<pid>/mem bs=1 skip=$((0x401000)) count=$((0x3000)) \
     of=code.bin 2>/dev/null
```

### Étape 5 — Vérification immédiate du dump

Avant de passer à la reconstruction (section 29.3), on vérifie que le dump contient bien le code original :

```
$ strings code.bin | grep FLAG
FLAG{unp4ck3d_and_r3c0nstruct3d}

$ strings code.bin | grep "Entrez votre"
[*] Entrez votre clé de licence (format RE29-XXXX) :
```

Si les chaînes caractéristiques du programme sont présentes dans le dump, l'extraction a réussi. On peut aussi vérifier la présence d'opcodes cohérents au début du dump :

```
$ objdump -b binary -m i386:x86-64 -D code.bin | head -20
```

On devrait reconnaître des instructions x86-64 valides — des prologues de fonctions (`push rbp` / `mov rbp, rsp` en `-O0`, ou `sub rsp, ...` en `-O2`), des appels (`call`), etc. — et non le bruit aléatoire qu'on obtiendrait si le dump avait été fait trop tôt (avant la fin de la décompression) ou aux mauvaises adresses.

---

## Quand l'unpacking dynamique se complique

La procédure décrite ci-dessus fonctionne directement pour UPX et la majorité des packers simples. Certains packers plus avancés ajoutent des obstacles que l'on rencontrera dans des analyses réelles :

### Décompression en plusieurs passes

Certains packers chaînent plusieurs couches de compression ou de chiffrement. Le stub de premier niveau décompresse un stub de second niveau, qui décompresse lui-même le code original. Dans ce cas, le breakpoint matériel sur le segment de destination s'active trop tôt — on tombe sur le stub intermédiaire, pas sur le code final. La solution est d'itérer : une fois le premier breakpoint atteint, on examine le code, on détermine que ce n'est pas encore le programme original, et on pose un nouveau breakpoint sur le prochain saut de transfert.

### Anti-debugging dans le stub

Le stub peut inclure des vérifications anti-débogage (chapitre 19, section 19.7) : appel à `ptrace(PTRACE_TRACEME)` pour détecter GDB, vérification de `/proc/self/status` pour la présence d'un traceur, ou mesures de timing pour détecter le single-stepping. Les contournements vus au chapitre 19 s'appliquent directement ici. Avec GDB, on peut utiliser `catch syscall ptrace` pour intercepter l'appel et forcer sa valeur de retour à 0 avec `set $rax = 0` avant de continuer.

### Réécriture des permissions mémoire

Des packers sophistiqués utilisent `mprotect` pour changer les permissions des pages mémoire au fil de la décompression : la zone de destination est d'abord `RW-` (pour y écrire le code décompressé), puis basculée en `R-X` (pour l'exécuter), et enfin la zone du stub est remise en `---` (inaccessible) pour effacer ses traces. Si l'on dump trop tard, le stub peut avoir déjà effacé certaines informations utiles. Un `catch syscall mprotect` dans GDB permet de suivre ces transitions.

### Code original relogé

Si le binaire original était compilé en PIE (`-pie`), les adresses dans le code décompressé sont relatives et doivent être relogées. Le stub gère normalement cette relocation avant le transfert de contrôle, mais le dump mémoire contiendra des adresses absolues correspondant à l'emplacement choisi par le loader pour cette exécution particulière. La reconstruction (section 29.3) devra en tenir compte.

---

## Résumé : arbre de décision pour l'unpacking

La logique de décision suit un chemin simple :

```
Le binaire est packé (confirmé en 29.1)
│
├─ Le packer est identifié (UPX, Ezuri…)
│  │
│  ├─ L'outil de décompression fonctionne ?
│  │  ├─ OUI → upx -d (ou équivalent) → terminé
│  │  └─ NON → Signatures altérées ?
│  │     ├─ OUI → Tenter réparation des magic bytes → réessayer
│  │     └─ NON → Passer au dynamique
│  │
│  └─ Pas d'outil de décompression disponible → Passer au dynamique
│
└─ Packer inconnu / custom → Passer directement au dynamique
   │
   ├─ 1. Lancer sous GDB dans la VM sandboxée
   ├─ 2. Trouver l'OEP (hbreak segment destination / dernier jmp)
   ├─ 3. Mapper les régions mémoire (vmmap)
   ├─ 4. Dumper code + données (dump binary memory)
   └─ 5. Vérifier le dump (strings, objdump -D)
```

Les dumps bruts obtenus à l'étape 5 ne sont pas encore des fichiers ELF exploitables par Ghidra ou radare2. La transformation de ces dumps en un ELF analysable est l'objet de la section suivante (29.3 — Reconstruire l'ELF original).

---

> 📌 **Point clé à retenir** — L'unpacking statique est toujours préférable quand il est possible : plus rapide, plus sûr, et le résultat est souvent un ELF complet. L'unpacking dynamique est la solution universelle mais produit des dumps bruts qui nécessitent un travail de reconstruction. En pratique, les deux approches sont complémentaires : on tente le statique, on valide (ou on corrige) avec le dynamique.

⏭️ [Reconstruire l'ELF original : fixer les headers, sections et entry point](/29-unpacking/03-reconstruire-elf.md)
