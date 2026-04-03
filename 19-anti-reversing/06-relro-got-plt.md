🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 19.6 — RELRO : Partial vs Full et impact sur la table GOT/PLT

> 🎯 **Objectif** : Comprendre les deux niveaux de RELRO (Partial et Full), leur mécanisme interne au niveau des segments ELF et des permissions mémoire, leur impact sur la résolution dynamique des symboles (lazy binding vs immediate binding), et les conséquences concrètes pour l'analyste — en particulier sur les possibilités de hooking et de patching via la GOT.

---

## Rappel : PLT et GOT dans la résolution dynamique

Avant d'aborder RELRO, un rappel rapide du mécanisme PLT/GOT est nécessaire. Ce sujet a été couvert en détail au chapitre 2 (section 2.9), mais les points clés sont indispensables pour comprendre ce que RELRO protège.

Quand un binaire ELF dynamiquement linké appelle une fonction de bibliothèque partagée (par exemple `printf`), l'appel ne va pas directement dans la libc. Il passe par deux structures intermédiaires :

- **PLT (Procedure Linkage Table)** — Un tableau de petits stubs en `.text` (code exécutable, lecture seule). Chaque stub contient un saut indirect vers une adresse stockée dans la GOT.  
- **GOT (Global Offset Table)** — Un tableau de pointeurs en `.got.plt` (données, initialement en lecture-écriture). Chaque entrée contient l'adresse réelle de la fonction cible en mémoire.

Le flux d'un appel à `printf` ressemble à ceci :

```
code utilisateur          PLT                     GOT                libc
      │                    │                       │                  │
      ├─ call printf@plt ─►│                       │                  │
      │                    ├── jmp [GOT+offset] ──►│                  │
      │                    │                       ├── 0x7f...4520 ──►│ printf()
      │                    │                       │                  │
```

### Lazy binding : le comportement par défaut

Par défaut, la GOT n'est pas remplie au chargement du programme. Au premier appel à `printf`, l'entrée GOT correspondante ne contient pas encore l'adresse de `printf` dans la libc — elle contient l'adresse d'une routine du résolveur dynamique (`_dl_runtime_resolve`). Ce résolveur cherche l'adresse réelle de `printf`, l'écrit dans la GOT, puis saute vers `printf`. Les appels suivants trouvent la bonne adresse directement dans la GOT et ne passent plus par le résolveur.

C'est le **lazy binding** : la résolution est différée au premier appel. L'avantage est un démarrage plus rapide (on ne résout que les fonctions effectivement appelées). L'inconvénient est que la GOT doit rester **en écriture** pendant toute l'exécution du programme, puisque le résolveur doit pouvoir y écrire les adresses au fil des appels.

Et c'est précisément cette GOT en écriture qui constitue une surface d'attaque.

## L'attaque GOT overwrite

Si un attaquant dispose d'une primitive d'écriture arbitraire (via un buffer overflow, un format string, un use-after-free…), il peut écraser une entrée de la GOT. Par exemple, remplacer l'adresse de `printf` dans la GOT par l'adresse de `system`. Au prochain appel à `printf("ls")` dans le code, c'est en réalité `system("ls")` qui s'exécute.

C'est une technique d'exploitation classique, élégante et puissante. RELRO a été conçu pour la neutraliser.

## Partial RELRO

### Le mécanisme

Partial RELRO est le niveau de protection par défaut sur la plupart des distributions Linux modernes. Il est activé par le flag de linker `-z relro` (souvent passé automatiquement par GCC).

Partial RELRO applique deux modifications au binaire :

**1. Réorganisation des sections ELF** — Le linker réordonne les sections pour que la GOT (`.got`) et les autres structures de données critiques du linker dynamique (`.dynamic`, `.init_array`, `.fini_array`) soient placées **avant** les données du programme (`.data`, `.bss`) dans le layout mémoire. L'objectif est que si un buffer overflow dans `.bss` ou `.data` déborde vers les adresses basses, il n'atteigne pas les structures du linker.

**2. Marquage en lecture seule des sections non-PLT** — Après le chargement et la relocation initiale, le loader marque certaines sections comme read-only via `mprotect`. Cela inclut la section `.got` (qui contient les pointeurs vers les variables globales des bibliothèques) et les headers de la section `.dynamic`. Mais — et c'est le point crucial — la section `.got.plt` reste en écriture.

La section `.got.plt` est celle qui contient les adresses des fonctions résolues par lazy binding. Partial RELRO la laisse writable parce que le résolveur dynamique doit pouvoir y écrire les adresses au fur et à mesure des appels.

### Ce que Partial RELRO protège et ne protège pas

| Protégé (read-only après chargement) | Non protégé (reste writable) |  
|---|---|  
| `.got` (variables globales) | `.got.plt` (fonctions lazy-bound) |  
| `.dynamic` | |  
| `.init_array` / `.fini_array` | |  
| Headers ELF relokalisés | |

En résumé : Partial RELRO empêche l'écrasement des métadonnées du linker, mais laisse la porte ouverte au GOT overwrite classique sur les pointeurs de fonctions.

### Observer Partial RELRO

```bash
$ readelf -l build/vuln_partial_relro | grep GNU_RELRO
  GNU_RELRO      0x002e00 0x0000000000403e00 0x0000000000403e00
                 0x000200 0x000200  R    0x1
```

La présence du segment `GNU_RELRO` dans les program headers confirme que RELRO est actif. Ce segment définit la plage d'adresses qui sera marquée read-only par le loader.

On peut vérifier que `.got.plt` est en dehors de cette plage :

```bash
$ readelf -S build/vuln_partial_relro | grep -E '\.got|\.dynamic'
  [21] .dynamic          DYNAMIC   0000000000403e00  ...
  [22] .got              PROGBITS  0000000000403fe0  ...
  [23] .got.plt          PROGBITS  0000000000404000  ...
```

La section `.dynamic` et `.got` sont à `0x403e00` et `0x403fe0`, couvertes par le segment `GNU_RELRO` qui commence à `0x403e00`. Mais `.got.plt` à `0x404000` est au-delà de la plage protégée — elle reste en écriture.

## Full RELRO

### Le mécanisme

Full RELRO est activé par les flags de linker `-z relro -z now`. Le flag `-z now` est la clé : il demande au loader de résoudre **tous** les symboles immédiatement au chargement, au lieu d'utiliser le lazy binding.

Le processus au chargement devient :

1. Le loader (`ld.so`) charge le binaire et ses bibliothèques en mémoire.  
2. **Toutes** les fonctions importées sont résolues immédiatement : le loader parcourt la table `.dynsym`, cherche chaque symbole dans les bibliothèques, et écrit l'adresse réelle dans la GOT.  
3. Une fois toutes les résolutions terminées, le loader appelle `mprotect` sur **l'intégralité** de la GOT (y compris `.got.plt`) pour la marquer en lecture seule.  
4. L'exécution du programme commence avec une GOT entièrement remplie et entièrement verrouillée.

Après cette initialisation, aucune écriture dans la GOT n'est possible. Toute tentative (par le programme, un attaquant, ou un outil d'analyse) provoque un segmentation fault.

### Ce qui change par rapport à Partial RELRO

| Aspect | Partial RELRO | Full RELRO |  
|---|---|---|  
| Résolution des symboles | Lazy binding (à la demande) | Immediate binding (au chargement) |  
| `.got.plt` après init | Read-Write | Read-Only |  
| GOT overwrite possible | Oui | Non |  
| Temps de démarrage | Plus rapide (résolution différée) | Plus lent (tout résolu d'un coup) |  
| Segment GNU_RELRO | Couvre `.got`, `.dynamic` | Couvre `.got`, `.got.plt`, `.dynamic` |

### Observer Full RELRO

```bash
$ readelf -l build/vuln_full_relro | grep GNU_RELRO
  GNU_RELRO      0x002de0 0x0000000000403de0 0x0000000000403de0
                 0x000220 0x000220  R    0x1
```

La plage couverte par `GNU_RELRO` est plus grande qu'en Partial RELRO — elle englobe maintenant `.got.plt`.

On peut aussi vérifier la présence du flag `BIND_NOW` dans la section `.dynamic` :

```bash
$ readelf -d build/vuln_full_relro | grep -E 'BIND_NOW|FLAGS'
 0x0000000000000018 (BIND_NOW)
 0x000000006ffffffb (FLAGS_1)            Flags: NOW
```

La présence de `BIND_NOW` ou du flag `NOW` dans `FLAGS_1` confirme l'immediate binding. C'est l'indicateur fiable de Full RELRO, car c'est ce flag qui déclenche le verrouillage complet par le loader.

### Détection avec `checksec`

```bash
$ checksec --file=build/vuln_no_relro
    RELRO:    No RELRO

$ checksec --file=build/vuln_partial_relro
    RELRO:    Partial RELRO

$ checksec --file=build/vuln_full_relro
    RELRO:    Full RELRO
```

Les trois niveaux sont clairement distingués par `checksec`.

## No RELRO

Pour mémoire, il est possible de compiler sans aucun RELRO :

```bash
gcc -Wl,-z,norelro -o vuln_no_relro vuln_demo.c
```

Sans RELRO, toutes les sections de données — `.got`, `.got.plt`, `.dynamic`, `.init_array`, `.fini_array` — restent en lecture-écriture pendant toute l'exécution. Le segment `GNU_RELRO` est absent des program headers. C'est la configuration la moins sûre et elle ne devrait jamais être utilisée en production.

Pour l'analyste, un binaire sans RELRO est le plus facile à manipuler dynamiquement : toutes les tables sont modifiables en mémoire.

## Impact sur le reverse engineering

RELRO a des implications différentes selon qu'on fait de l'analyse statique, de l'analyse dynamique, ou du hooking.

### Analyse statique : aucun impact

RELRO ne modifie pas le code machine ni la structure logique du programme. Les instructions sont les mêmes, les fonctions sont les mêmes, les chaînes sont les mêmes. Un analyste qui travaille exclusivement dans Ghidra ne verra aucune différence entre un binaire Partial RELRO et Full RELRO.

La seule nuance est que Full RELRO supprime le mécanisme de lazy binding, ce qui signifie que les stubs PLT sont légèrement simplifiés : ils ne contiennent plus le code de fallback vers le résolveur dynamique. Mais cette différence est mineure dans le désassemblage.

### Analyse dynamique : impact modéré

La principale conséquence de Full RELRO pour l'analyste dynamique est l'impossibilité de patcher la GOT en mémoire. Plusieurs techniques sont affectées.

**GOT hooking impossible sous Full RELRO** — Une technique classique d'instrumentation consiste à remplacer une entrée GOT par l'adresse d'une fonction custom (un hook). Par exemple, remplacer l'entrée GOT de `strcmp` par un wrapper qui logue les arguments avant d'appeler le vrai `strcmp`. Sous Full RELRO, l'écriture dans la GOT provoque un crash.

**`LD_PRELOAD` fonctionne toujours** — Le mécanisme `LD_PRELOAD` (chapitre 22, section 22.4) n'utilise pas la GOT pour le hooking. Il agit au niveau du résolveur de symboles, en injectant une bibliothèque prioritaire dans l'ordre de résolution. `LD_PRELOAD` fonctionne identiquement quel que soit le niveau de RELRO.

**Frida fonctionne toujours** — Frida (chapitre 13) utilise plusieurs mécanismes d'interception qui ne dépendent pas de l'écriture dans la GOT. Le mode `Interceptor.attach` réécrit les premières instructions de la fonction cible (inline hooking), pas la GOT. RELRO ne bloque pas Frida.

**GDB peut contourner les permissions** — GDB peut écrire dans des pages read-only via `ptrace(PTRACE_POKEDATA)`, qui court-circuite les protections mémoire du processus cible. Un analyste utilisant GDB peut donc modifier la GOT même sous Full RELRO, bien que ce ne soit pas une technique courante pour le RE pur.

### Récapitulatif pour l'analyste

| Technique | No RELRO | Partial RELRO | Full RELRO |  
|---|---|---|---|  
| GOT overwrite en mémoire | Possible | Possible (`.got.plt`) | Bloqué (`SIGSEGV`) |  
| `LD_PRELOAD` hooking | Fonctionne | Fonctionne | Fonctionne |  
| Frida `Interceptor.attach` | Fonctionne | Fonctionne | Fonctionne |  
| GDB `set` sur la GOT | Fonctionne | Fonctionne | Possible via `ptrace` |  
| Écriture `.dynamic` | Possible | Bloqué | Bloqué |  
| Écriture `.init_array` | Possible | Bloqué | Bloqué |

## RELRO et l'écosystème des distributions

Depuis plusieurs années, les grandes distributions Linux ont convergé vers des paramètres par défaut qui activent au minimum Partial RELRO :

- **Debian / Ubuntu** — Partial RELRO par défaut. Full RELRO activé sur les paquets sensibles (daemons réseau, binaires setuid).  
- **Fedora / RHEL** — Full RELRO par défaut (`-Wl,-z,relro,-z,now` dans les flags de compilation des paquets).  
- **Arch Linux** — Full RELRO par défaut via `makepkg.conf`.  
- **Hardened Gentoo** — Full RELRO systématique.

En conséquence, la majorité des binaires que vous rencontrerez « dans la nature » auront au moins Partial RELRO. Les binaires avec Full RELRO sont de plus en plus courants. Les binaires sans RELRO sont rares et méritent attention — c'est soit un binaire ancien, soit un choix délibéré (binaire embarqué, binaire packé, challenge CTF).

## Relier RELRO aux autres protections

RELRO complète la triade de protections abordée dans la section précédente :

- **Canary** empêche l'écrasement de l'adresse de retour sur la pile.  
- **NX** empêche l'exécution de code injecté.  
- **ASLR + PIE** empêche la prédiction des adresses.  
- **Full RELRO** empêche la modification de la GOT.

Ensemble, ces protections ferment les vecteurs d'exploitation classiques un par un. Pour l'analyste RE, l'essentiel est de savoir les identifier rapidement (un seul `checksec` suffit) et de comprendre lesquelles affectent sa stratégie d'analyse — sans les confondre avec des protections anti-RE à proprement parler.

---


⏭️ [Techniques de détection de débogueur (`ptrace`, timing checks, `/proc/self/status`)](/19-anti-reversing/07-detection-debogueur.md)
