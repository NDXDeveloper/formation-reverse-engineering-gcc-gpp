🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 12.1 — Installation et comparaison des trois extensions

> **Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg**  
> **Partie III — Analyse Dynamique**

---

## Le mécanisme commun : les fichiers d'initialisation GDB

Avant d'installer quoi que ce soit, il est utile de comprendre comment ces extensions se greffent sur GDB. Le principe est le même pour les trois : GDB, au démarrage, exécute automatiquement les commandes présentes dans le fichier `~/.gdbinit`. Ce fichier agit comme un script de configuration. Les extensions y ajoutent une directive `source` qui charge leur code Python, lequel enregistre de nouvelles commandes, remplace certains comportements par défaut et installe des hooks qui s'exécutent à chaque arrêt du programme.

```bash
# Exemple typique d'un ~/.gdbinit après installation d'une extension
source /opt/pwndbg/gdbinit.py
```

Cette mécanique implique une contrainte importante : **une seule extension peut être active à la fois**. Si le fichier `~/.gdbinit` source à la fois GEF et pwndbg, les commandes homonymes entreront en conflit et le comportement sera imprévisible. Nous verrons en fin de section comment basculer proprement entre les trois.

---

## Installation de PEDA

PEDA est la plus simple à installer. Le dépôt est cloné, puis une ligne est ajoutée au fichier d'initialisation de GDB.

```bash
git clone https://github.com/longld/peda.git ~/peda  
echo "source ~/peda/peda.py" >> ~/.gdbinit  
```

Aucune dépendance Python externe n'est requise : PEDA n'utilise que la bibliothèque standard de Python et l'API GDB intégrée. C'est d'ailleurs l'un de ses atouts historiques — elle fonctionne sur des systèmes minimalistes sans pip ni virtualenv.

Pour vérifier l'installation, il suffit de lancer GDB :

```bash
gdb -q
```

Le prompt doit afficher `gdb-peda$` au lieu du `(gdb)` habituel. Si ce n'est pas le cas, vérifier que le chemin dans `~/.gdbinit` pointe bien vers le fichier `peda.py` et que la version de Python embarquée dans GDB est compatible (Python 3 dans les versions récentes de GDB).

PEDA ne reçoit plus de mises à jour fréquentes. Le dernier commit significatif sur le dépôt principal remonte à plusieurs années. Des forks communautaires existent (notamment `peda-arm` pour l'architecture ARM), mais pour un usage sur x86-64 avec des fonctionnalités modernes, GEF et pwndbg sont aujourd'hui préférables.

---

## Installation de GEF

GEF se distingue par sa distribution en un seul fichier Python. L'installation canonique passe par un script qui télécharge ce fichier et configure `~/.gdbinit` :

```bash
bash -c "$(curl -fsSL https://gef.blah.cat/sh)"
```

Pour ceux qui préfèrent une installation manuelle (ce qui est toujours une bonne habitude en sécurité — inspecter un script avant de l'exécuter) :

```bash
curl -fsSL https://gef.blah.cat/py -o ~/.gdbinit-gef.py  
echo "source ~/.gdbinit-gef.py" >> ~/.gdbinit  
```

GEF n'a aucune dépendance obligatoire au-delà de GDB compilé avec le support Python 3. Cependant, certaines commandes optionnelles gagnent en fonctionnalité avec des paquets supplémentaires. GEF propose une commande intégrée pour vérifier et installer ces extras :

```bash
# Depuis le prompt GEF dans GDB :
gef➤ pip install ropper keystone-engine
```

Ou directement depuis le shell, avec le gestionnaire de paquets système pour les outils externes :

```bash
# Dépendances optionnelles pour des commandes avancées
sudo apt install ropper  
pip install capstone unicorn keystone-engine  
```

Ces paquets activent respectivement la recherche de gadgets ROP (ropper), le désassemblage avancé (capstone), l'émulation d'instructions (unicorn) et l'assemblage en ligne (keystone). Sans eux, GEF fonctionne parfaitement pour le débogage quotidien — ces extras ne sont nécessaires que pour des usages spécifiques comme l'exploitation.

Au lancement de GDB, le prompt affiche `gef➤` et une bannière indiquant la version de GEF ainsi que le nombre de commandes chargées.

---

## Installation de pwndbg

pwndbg a l'installation la plus lourde des trois, mais le processus reste automatisé :

```bash
git clone https://github.com/pwndbg/pwndbg.git ~/pwndbg  
cd ~/pwndbg  
./setup.sh
```

Le script `setup.sh` crée un environnement virtuel Python, installe les dépendances (parmi lesquelles `capstone`, `unicorn`, `pycparser`, `psutil` et d'autres), puis ajoute la ligne `source` appropriée dans `~/.gdbinit`. Sur les distributions basées sur Debian/Ubuntu, il installe également les paquets système nécessaires via `apt`.

L'installation prend sensiblement plus de temps que pour les deux autres extensions, et l'empreinte disque est plus importante en raison de l'environnement virtuel et des bibliothèques compilées. C'est le prix à payer pour la richesse fonctionnelle de pwndbg, notamment ses capacités d'analyse de la heap qui reposent sur un parsing fin des structures internes de la glibc.

Après installation, le lancement de GDB affiche le prompt `pwndbg>` accompagné d'une bannière colorée.

> 💡 **Note pour les utilisateurs d'Arch Linux, Fedora ou d'autres distributions** : le script `setup.sh` détecte la distribution et adapte les commandes d'installation de paquets. En cas de problème, consulter le `README.md` du dépôt pwndbg qui documente les cas particuliers.

---

## Basculer entre les extensions

Puisqu'une seule extension peut être chargée à la fois via `~/.gdbinit`, il faut un mécanisme pour basculer. L'approche la plus propre consiste à créer un fichier d'initialisation dédié par extension, puis à utiliser des alias shell.

Commencer par créer trois fichiers séparés :

```bash
# ~/.gdbinit-peda
source ~/peda/peda.py

# ~/.gdbinit-gef
source ~/.gdbinit-gef.py

# ~/.gdbinit-pwndbg
source ~/pwndbg/gdbinit.py
```

Puis définir des alias dans `~/.bashrc` ou `~/.zshrc` :

```bash
alias gdb-peda='gdb -ix ~/.gdbinit-peda'  
alias gdb-gef='gdb -ix ~/.gdbinit-gef'  
alias gdb-pwndbg='gdb -ix ~/.gdbinit-pwndbg'  
```

Le flag `-ix` indique à GDB d'utiliser le fichier spécifié comme fichier d'initialisation *à la place* du `~/.gdbinit` par défaut. Après un `source ~/.bashrc`, les trois commandes sont disponibles :

```bash
gdb-gef -q ./keygenme_O0       # Lance GDB avec GEF  
gdb-pwndbg -q ./keygenme_O0    # Lance GDB avec pwndbg  
gdb-peda -q ./keygenme_O0      # Lance GDB avec PEDA  
```

Le fichier `~/.gdbinit` principal peut alors contenir l'extension que l'on utilise le plus souvent par défaut (par exemple GEF pour sa légèreté), tout en laissant la possibilité de basculer ponctuellement via les alias.

> 💡 **Astuce** : si le script `check_env.sh` de la formation vérifie la présence d'une extension GDB, il testera l'existence de ces fichiers d'initialisation. S'assurer que les trois sont bien en place après cette étape.

---

## Comparaison des trois extensions

### Philosophie et architecture

PEDA a ouvert la voie avec un principe simple : afficher un contexte riche à chaque arrêt. Son code est monolithique — un seul fichier Python d'environ 4 000 lignes qui enregistre toutes les commandes. Cette architecture rend le code facile à lire pour comprendre comment étendre GDB, mais difficile à maintenir et à faire évoluer.

GEF a repris cette philosophie du fichier unique en la poussant plus loin : le code est plus modulaire en interne (chaque commande est une classe Python distincte), mais le tout est distribué dans un seul fichier. L'idée directrice est « zéro dépendance obligatoire » — on peut `scp` le fichier sur une machine distante et avoir immédiatement un GDB augmenté. GEF met également l'accent sur le support multi-architecture : ARM, AArch64, MIPS, SPARC, PowerPC et RISC-V sont pris en charge nativement, ce qui le rend précieux pour le reverse engineering de firmware ou d'embarqué.

pwndbg adopte une architecture éclatée en de nombreux modules Python organisés en paquets. Cette structure favorise la contribution communautaire et l'ajout de fonctionnalités complexes, comme le parsing de la heap glibc qui nécessite à lui seul plusieurs centaines de lignes de code structuré. La contrepartie est l'impossibilité de fonctionner sans ses dépendances — on ne copie pas pwndbg sur un serveur distant aussi facilement que GEF.

### Affichage du contexte

Les trois extensions affichent un contexte similaire à chaque arrêt, mais avec des différences de présentation et de contenu.

PEDA affiche trois panneaux : registres, code désassemblé et pile. La coloration est fonctionnelle mais basique. Les pointeurs sont déréférencés sur un niveau — on voit la valeur pointée, mais pas les chaînes de déréférencement profondes.

GEF structure son contexte en sections configurables : `registers`, `stack`, `code`, `threads`, `trace`, et optionnellement `extra` (qui peut afficher la source C si les symboles DWARF sont présents). Chaque section peut être activée, désactivée ou réordonnée via la commande `gef config`. Le déréférencement des pointeurs est récursif, et les chaînes de caractères détectées sont affichées en clair à côté des adresses.

pwndbg propose l'affichage le plus riche par défaut. Son contexte inclut les registres avec mise en évidence des modifications depuis le dernier arrêt (la valeur précédente est affichée en gris, la nouvelle en couleur vive), le désassemblage avec coloration syntaxique avancée, la pile avec déréférencement récursif (commande `telescope` intégrée), et un panneau de backtrace. pwndbg détecte et affiche également les arguments des fonctions de la libc lors d'un `call` : par exemple, à un `call malloc`, il indique la taille demandée extraite du registre `rdi`.

### Commandes spécifiques notables

Certaines commandes n'existent que dans une extension ou y sont significativement plus développées.

pwndbg excelle dans l'analyse de la heap avec `vis_heap_chunks` (représentation visuelle des chunks malloc), `bins` (état des bins de la glibc : fastbins, tcache, unsorted, small, large), `top_chunk`, `arena` et `mp_`. Ces commandes sont absentes de PEDA et présentes sous une forme plus limitée dans GEF (via la commande `heap` et ses sous-commandes).

GEF propose `pattern create` / `pattern search` pour la génération et la recherche de motifs De Bruijn (utiles pour calculer les offsets lors de débordements de buffer), `xinfo` pour obtenir toutes les informations sur une adresse (section, permissions, mapping), et `highlight` pour coloriser dynamiquement des motifs dans la sortie de GDB. GEF dispose également d'un système de commandes `gef config` qui permet de personnaliser finement chaque aspect de l'affichage sans modifier le code source.

PEDA fournit `checksec` (vérification des protections du binaire en cours de débogage), `procinfo` (informations sur le processus), et `elfheader` / `elfsymbol` pour inspecter les structures ELF depuis le débogueur. Ces commandes existent aussi dans GEF et pwndbg, mais PEDA les a popularisées.

### Tableau récapitulatif

| Critère | PEDA | GEF | pwndbg |  
|---|---|---|---|  
| **Fichier unique** | Oui | Oui | Non (multi-modules) |  
| **Dépendances obligatoires** | Aucune | Aucune | Plusieurs (capstone, unicorn…) |  
| **Multi-architecture** | x86, x86-64 | x86, x86-64, ARM, AArch64, MIPS, SPARC, PPC, RISC-V | x86, x86-64, ARM, AArch64, MIPS |  
| **Analyse de heap glibc** | Basique | Intermédiaire | Avancée (vis_heap_chunks, bins, tcache…) |  
| **Recherche de gadgets ROP** | `ropgadget` | Via ropper intégré | `rop` intégrée |  
| **Déréférencement récursif** | 1 niveau | Récursif | Récursif (telescope) |  
| **Coloration des modifications de registres** | Non | Oui | Oui (avec valeur précédente) |  
| **Configuration fine** | Limitée | `gef config` (très granulaire) | `config` / `themefile` |  
| **Facilité de déploiement distant** | Excellente | Excellente | Moyenne |  
| **Maintenance active (2024+)** | Faible | Active | Très active |  
| **Communauté / contributeurs** | Réduite | Moyenne | Large |

### Quel outil choisir ?

Le choix dépend du contexte d'utilisation.

Pour un **débogage quotidien sur machine locale** avec un focus sur le reverse engineering de binaires ELF x86-64, pwndbg offre l'expérience la plus complète. Ses commandes de heap et sa communauté active en font l'extension de référence pour quiconque travaille sur l'exploitation ou l'analyse de malware.

Pour un **débogage distant** via `gdbserver` sur une cible à laquelle on accède par SSH, ou pour un **environnement embarqué / multi-architecture**, GEF est le choix pragmatique. Un `scp` du fichier Python suffit, et le support natif d'architectures variées évite de jongler avec des plugins additionnels.

Pour **apprendre comment les extensions fonctionnent en interne**, le code de PEDA reste le plus lisible et le plus pédagogique. Son architecture simple en fait un bon point de départ pour quiconque souhaite écrire ses propres commandes GDB en Python.

Dans la pratique, beaucoup de reverse engineers installent les trois et basculent via les alias décrits plus haut, en fonction de la tâche du moment. C'est l'approche que nous adopterons dans cette formation : GEF comme extension par défaut pour sa polyvalence, pwndbg quand l'analyse de heap ou la recherche de gadgets ROP l'exige.

---

## Vérification de l'installation

Pour confirmer que les trois extensions sont correctement installées et que le mécanisme de bascule fonctionne, exécuter les commandes suivantes :

```bash
# Vérifier GEF
gdb-gef -q -batch -ex "gef help" 2>/dev/null | head -5

# Vérifier pwndbg
gdb-pwndbg -q -batch -ex "pwndbg" 2>/dev/null | head -5

# Vérifier PEDA
gdb-peda -q -batch -ex "peda help" 2>/dev/null | head -5
```

Chaque commande doit afficher la liste des commandes propres à l'extension correspondante sans erreur Python. Si une erreur `ModuleNotFoundError` apparaît pour pwndbg, relancer `~/pwndbg/setup.sh` pour réinstaller les dépendances manquantes. Si GDB affiche `No module named 'gef'`, vérifier que le chemin dans `~/.gdbinit-gef` pointe bien vers le fichier téléchargé.

Le script `check_env.sh` fourni avec la formation inclut ces vérifications. Après cette section, lancer :

```bash
./check_env.sh --chapter 12
```

Les trois extensions doivent apparaître au vert.

---


⏭️ [Visualisation de la stack et des registres en temps réel](/12-gdb-extensions/02-visualisation-stack-registres.md)
