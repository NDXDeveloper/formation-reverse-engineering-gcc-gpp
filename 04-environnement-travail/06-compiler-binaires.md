🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 4.6 — Compiler tous les binaires d'entraînement en une commande (`make all`)

> 🎯 **Objectif de cette section** : compiler l'ensemble des binaires d'entraînement de la formation à partir des sources fournies, vérifier que la compilation s'est déroulée correctement, et comprendre ce que chaque cible produit.

---

## Prérequis

Avant de lancer la compilation, assurez-vous que :

- Les outils de la **vague 1** (section 4.2) sont installés — en particulier `gcc`, `g++` et `make`.  
- Le dépôt de la formation est cloné dans votre VM (section 4.5).  
- Votre environnement virtuel Python est activé (`source ~/re-venv/bin/activate`) — certains Makefile invoquent des outils Python pour des étapes de post-traitement.

Vérification rapide :

```bash
[vm] gcc --version && g++ --version && make --version
```

Si l'une de ces commandes échoue, revenez à la section 4.2.

---

## Compilation complète : `make all`

Placez-vous à la racine du répertoire `binaries/` et lancez la compilation :

```bash
[vm] cd ~/formation-re/binaries
[vm] make all
```

Le Makefile racine itère sur chaque sous-dossier et y exécute `make all`. Vous verrez défiler les lignes de compilation, groupées par chapitre :

```
=== Compilation de ch21-keygenme ===
gcc -Wall -Wextra -O0 -g -o keygenme_O0 keygenme.c  
gcc -Wall -Wextra -O2 -g -o keygenme_O2 keygenme.c  
gcc -Wall -Wextra -O3 -g -o keygenme_O3 keygenme.c  
cp keygenme_O0 keygenme_O0_strip  
strip keygenme_O0_strip  
cp keygenme_O2 keygenme_O2_strip  
strip keygenme_O2_strip  
=== Compilation de ch22-oop ===
g++ -Wall -Wextra -O0 -g -o oop_O0 oop.cpp  
g++ -Wall -Wextra -O2 -g -o oop_O2 oop.cpp  
...
```

La compilation de l'ensemble prend généralement **moins d'une minute** sur une VM correctement dimensionnée (4 vCPU, 8 Go RAM). Sur un Mac Apple Silicon via UTM en émulation x86-64, comptez 2 à 4 minutes.

---

## Ce que `make all` produit

Après une compilation réussie, chaque sous-dossier de `binaries/` contient ses sources d'origine **plus** les binaires générés. Voici l'inventaire complet :

### Binaires C (GCC)

| Sous-dossier | Binaires produits | Remarques |  
|---|---|---|  
| `ch21-keygenme/` | `keygenme_O0`, `keygenme_O2`, `keygenme_O3`, `keygenme_O0_strip`, `keygenme_O2_strip` | Crackme — 5 variantes |  
| `ch23-network/` | `client_O0`, `client_O2`, `server_O0`, `server_O2` + variantes strippées | Client et serveur séparés |  
| `ch24-crypto/` | `crypto_O0`, `crypto_O2` + variantes strippées | Lié avec `-lcrypto` (OpenSSL) |  
| `ch25-fileformat/` | `fileformat_O0`, `fileformat_O2` + variantes strippées | Parseur de format custom |  
| `ch27-ransomware/` | `ransomware_O0`, `ransomware_O2` + variantes strippées | ⚠️ Sandbox uniquement |  
| `ch28-dropper/` | `dropper_O0`, `dropper_O2` + variantes strippées | ⚠️ Sandbox uniquement |  
| `ch29-packed/` | `packed_O0`, `packed_O0_upx` | Binaire original + version packée UPX |

### Binaires C++ (G++)

| Sous-dossier | Binaires produits | Remarques |  
|---|---|---|  
| `ch22-oop/` | `oop_O0`, `oop_O2`, `oop_O3` + variantes strippées | Application orientée objet avec vtables |

### Binaires Rust et Go (optionnels)

| Sous-dossier | Binaires produits | Remarques |  
|---|---|---|  
| `ch33-rust/` | `crackme_rust`, `crackme_rust_strip` | Requiert `rustc`/`cargo` |  
| `ch34-go/` | `crackme_go`, `crackme_go_strip` | Requiert `go` |

> 💡 Au total, `make all` produit environ **35 à 40 binaires** selon les toolchains disponibles. C'est l'intégralité du matériel pratique de la formation.

---

## Cas particuliers de compilation

### `ch24-crypto` — dépendance OpenSSL

Le binaire du chapitre 24 utilise les fonctions cryptographiques d'OpenSSL. Le Makefile le lie avec `-lcrypto` :

```makefile
$(NAME)_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $< -lcrypto
```

Si la compilation échoue avec une erreur du type `cannot find -lcrypto`, installez les headers de développement OpenSSL :

```bash
[vm] sudo apt install -y libssl-dev
```

Ce paquet devrait déjà être installé si vous avez suivi la section 4.2.

### `ch29-packed` — packing avec UPX

Le Makefile du chapitre 29 compile d'abord le binaire normalement, puis le compresse avec UPX pour produire la variante packée :

```makefile
$(NAME)_O0_upx: $(NAME)_O0
	cp $< $@
	upx --best $@
```

Si UPX n'est pas installé, cette cible échouera. Les autres cibles du même sous-dossier (non packées) seront produites normalement.

```bash
[vm] sudo apt install -y upx-ucl    # si pas déjà installé
```

### `ch33-rust` — compilation Rust

Le Makefile du chapitre 33 invoque `cargo build` :

```makefile
all:
	cd crackme_rust && cargo build --release
	cp crackme_rust/target/release/crackme_rust .
	cp crackme_rust .
	strip -o crackme_rust_strip crackme_rust
```

Si Rust n'est pas installé, vous verrez :

```
make[1]: cargo: No such file or directory
```

C'est attendu si vous avez choisi de ne pas installer Rust (outil optionnel en section 4.2). Les chapitres 33 sont des bonus (Partie VIII) et ne sont pas requis pour le parcours principal.

### `ch34-go` — compilation Go

Même logique pour Go :

```makefile
all:
	cd crackme_go && go build -o ../crackme_go .
	strip -o crackme_go_strip crackme_go
```

Sans la toolchain Go installée, cette cible échoue sans affecter le reste.

---

## Vérifier la compilation

### Vérification rapide par comptage

Un moyen rapide de vérifier que tout s'est bien passé est de compter les binaires ELF produits :

```bash
[vm] cd ~/formation-re/binaries
[vm] find . -type f -executable | xargs file | grep "ELF" | wc -l
```

Le résultat attendu est d'environ **35–40** (selon les toolchains optionnelles). Si le nombre est nettement inférieur, relisez la sortie de `make all` pour identifier les erreurs.

### Vérification détaillée par sous-dossier

Pour inspecter les binaires d'un sous-dossier spécifique :

```bash
[vm] ls -lh ch21-keygenme/
```

Sortie attendue :

```
-rw-r--r-- 1 re re  2.1K  keygenme.c
-rw-r--r-- 1 re re   487  Makefile
-rwxr-xr-x 1 re re  18K   keygenme_O0
-rwxr-xr-x 1 re re  17K   keygenme_O2
-rwxr-xr-x 1 re re  17K   keygenme_O3
-rwxr-xr-x 1 re re  15K   keygenme_O0_strip
-rwxr-xr-x 1 re re  15K   keygenme_O2_strip
```

Quelques observations qui confirment que la compilation est correcte :

- Les binaires non strippés (`_O0`, `_O2`, `_O3`) sont **plus gros** que les variantes strippées — les informations DWARF et la table de symboles occupent de l'espace.  
- Le binaire `-O0` est généralement un peu **plus gros** que le `-O2` — les fonctions ne sont pas inlinées, le code n'est pas factorisé. Ce n'est pas toujours le cas (les optimisations peuvent aussi dérouler des boucles et augmenter la taille), mais la tendance est là.  
- Tous les fichiers portent le bit exécutable (`-rwxr-xr-x`).

### Vérification avec `file`

Confirmez que chaque binaire est bien un ELF x86-64 :

```bash
[vm] file ch21-keygenme/keygenme_O0
```

Sortie attendue :

```
keygenme_O0: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, with debug_info, not stripped  
```

Points à vérifier :

- `ELF 64-bit` — c'est bien un binaire 64 bits.  
- `x86-64` — architecture correcte.  
- `with debug_info, not stripped` — les symboles DWARF sont présents (variante non strippée).

Pour une variante strippée :

```bash
[vm] file ch21-keygenme/keygenme_O0_strip
```

```
keygenme_O0_strip: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),  
dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2,  
BuildID[sha1]=..., for GNU/Linux 3.2.0, stripped  
```

Notez la différence : `stripped` au lieu de `with debug_info, not stripped`. C'est exactement le comportement attendu.

### Vérification avec `readelf`

Pour aller plus loin et vérifier la présence des sections de débogage :

```bash
[vm] readelf -S ch21-keygenme/keygenme_O0 | grep debug
```

Sortie attendue (extraits) :

```
  [29] .debug_aranges    PROGBITS  ...
  [30] .debug_info       PROGBITS  ...
  [31] .debug_abbrev     PROGBITS  ...
  [32] .debug_line       PROGBITS  ...
  [33] .debug_str        PROGBITS  ...
```

Sur la variante strippée, cette commande ne retourne rien — c'est la confirmation que `strip` a bien supprimé les informations de débogage.

> 📌 Ces commandes (`file`, `readelf`, `strings`, `objdump`) seront détaillées dans le chapitre 5. Pour l'instant, elles servent uniquement à valider la compilation. Si les sorties correspondent à ce qui est décrit ci-dessus, tout est en ordre.

---

## Recompiler un sous-dossier spécifique

Vous n'avez pas besoin de recompiler tout le dépôt à chaque fois. Pour ne recompiler que les binaires d'un chapitre donné :

```bash
[vm] cd ~/formation-re/binaries/ch21-keygenme
[vm] make clean    # supprime les binaires existants
[vm] make all      # recompile toutes les variantes
```

C'est utile lorsque vous modifiez une source pour expérimenter (par exemple, ajouter un `printf` pour comprendre un comportement) et que vous voulez recompiler rapidement.

---

## Nettoyer les binaires

Pour supprimer tous les binaires compilés et revenir à un état « sources uniquement » :

```bash
[vm] cd ~/formation-re/binaries
[vm] make clean
```

Après cette commande, chaque sous-dossier ne contient plus que ses fichiers sources et son Makefile. Un nouveau `make all` reproduira exactement les mêmes binaires.

> 💡 **Quand nettoyer ?** Après une mise à jour de GCC (`apt upgrade` qui installe une nouvelle version), il est recommandé de faire un `make clean && make all` pour que tous les binaires soient recompilés avec la même version du compilateur. Cela évite des incohérences si vous comparez des binaires entre eux (chapitre 10 — diffing).

---

## Compiler avec un compilateur ou des flags différents

Les Makefile utilisent des variables (`CC`, `CXX`, `CFLAGS`) que vous pouvez surcharger depuis la ligne de commande. Par exemple, pour compiler avec Clang plutôt que GCC :

```bash
[vm] cd ~/formation-re/binaries/ch21-keygenme
[vm] make clean
[vm] make all CC=clang
```

Ou pour ajouter un flag spécifique, comme la protection de la pile :

```bash
[vm] make all CFLAGS="-Wall -Wextra -fstack-protector-strong"
```

Cette flexibilité est exploitée dans le chapitre 16 (comparaison GCC vs Clang, section 16.7) et le chapitre 19 (activation/désactivation des protections compilateur, section 19.5). Pouvoir recompiler les mêmes sources avec des options différentes et observer l'impact sur le binaire est un exercice fondamental en RE.

> ⚠️ **Attention** : surcharger `CFLAGS` remplace la valeur définie dans le Makefile (qui inclut typiquement `-Wall -Wextra`). Si vous ajoutez des flags, pensez à inclure les flags de base. Alternativement, utilisez la variable `EXTRA_CFLAGS` si le Makefile la supporte :  
> ```bash  
> [vm] make all EXTRA_CFLAGS="-fstack-protector-strong"  
> ```

---

## Snapshot post-compilation

Une fois tous les binaires compilés et vérifiés, c'est le bon moment pour prendre un nouveau snapshot de votre VM :

```bash
# VirtualBox : Machine → Prendre un instantané → "tools-ready"
# QEMU/KVM :
[hôte] virsh snapshot-create-as RE-Lab tools-ready --description "Outils installés, binaires compilés"
# UTM : icône appareil photo → "tools-ready"
```

Ce snapshot `tools-ready` représente l'état de référence pour l'ensemble de la formation. Tous les outils sont installés, tous les binaires sont compilés, l'environnement est prêt. Si une manipulation future corrompt votre VM, vous pourrez revenir ici en quelques secondes.

---

## Résumé

- `make all` depuis `binaries/` compile toutes les variantes de tous les binaires d'entraînement en une seule commande. La compilation prend moins d'une minute.  
- Chaque source produit **plusieurs binaires** : différents niveaux d'optimisation (`-O0`, `-O2`, `-O3`), avec ou sans symboles, avec ou sans stripping, et parfois avec packing (UPX).  
- Les cibles Rust et Go sont **optionnelles** — leur échec n'affecte pas les binaires C/C++.  
- Vérifiez la compilation avec `find` + `file` + `readelf` pour confirmer le type, l'architecture et la présence des symboles de débogage.  
- Les Makefile acceptent des **surcharges de variables** (`CC=clang`, `CFLAGS=...`) pour expérimenter avec d'autres compilateurs ou options.  
- Prenez un **snapshot `tools-ready`** après la compilation — c'est votre état de référence pour toute la formation.

---


⏭️ [Vérifier l'installation : script `check_env.sh` fourni](/04-environnement-travail/07-verifier-installation.md)
