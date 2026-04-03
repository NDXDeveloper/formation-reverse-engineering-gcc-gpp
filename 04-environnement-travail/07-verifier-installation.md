🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 4.7 — Vérifier l'installation : script `check_env.sh` fourni

> 🎯 **Objectif de cette section** : exécuter le script de vérification `check_env.sh` qui contrôle automatiquement que tous les outils, dépendances et binaires d'entraînement sont correctement installés et configurés. Comprendre la sortie du script et savoir résoudre les éventuels échecs.

---

## Pourquoi un script de vérification ?

L'installation de l'environnement RE implique une trentaine d'outils répartis entre paquets système, installations manuelles, paquets pip et téléchargements directs (sections 4.2 à 4.6). Même en suivant les instructions à la lettre, il est courant qu'un outil ait été oublié, qu'une version soit incorrecte ou qu'une variable d'environnement manque.

Plutôt que de découvrir ces problèmes au milieu du chapitre 11 quand GEF refuse de se charger, ou au chapitre 15 quand AFL++ n'est pas dans le `PATH`, le script `check_env.sh` vérifie tout d'un coup, produit un rapport clair, et vous dit exactement quoi corriger.

C'est aussi un filet de sécurité après une mise à jour système (`apt upgrade`) qui pourrait avoir modifié des versions ou supprimé des dépendances.

---

## Exécuter le script

Le script se trouve à la racine du dépôt :

```bash
[vm] cd ~/formation-re
[vm] chmod +x check_env.sh
[vm] ./check_env.sh
```

> 💡 Le script ne nécessite pas les droits root. Il se contente de vérifier la présence et les versions des outils — il ne modifie rien sur le système.

---

## Lire la sortie

Le script organise ses vérifications en **catégories** correspondant aux vagues d'installation de la section 4.2. Chaque vérification produit une ligne avec un indicateur visuel :

```
══════════════════════════════════════════════════════
  RE Lab — Vérification de l'environnement
══════════════════════════════════════════════════════

── Socle système ──────────────────────────────────────
  [✔] gcc             13.2.0
  [✔] g++             13.2.0
  [✔] make            4.3
  [✔] python3         3.12.3
  [✔] pip             24.0
  [✔] java            openjdk 21.0.3
  [✔] git             2.43.0

── Outils CLI d'inspection et débogage ────────────────
  [✔] gdb             15.0.50
  [✔] strace          6.8
  [✔] ltrace          0.7.3
  [✔] valgrind        3.22.0
  [✔] checksec        (disponible)
  [✔] yara            4.3.2
  [✔] file            5.45
  [✔] strings         (binutils) 2.42
  [✔] readelf         (binutils) 2.42
  [✔] objdump         (binutils) 2.42
  [✔] nm              (binutils) 2.42
  [✔] c++filt         (binutils) 2.42
  [✔] nasm            2.16.03
  [✔] binwalk         2.3.4

── Désassembleurs et éditeurs ─────────────────────────
  [✔] ghidra          11.3 (/opt/ghidra)
  [✔] radare2         5.9.6
  [✔] imhex           1.37.4
  [✗] ida-free        NON TROUVÉ (optionnel)

── Frameworks dynamiques ──────────────────────────────
  [✔] gdb-gef         GEF chargé dans ~/.gdbinit
  [✔] frida           16.2.1
  [✔] pwntools        4.13.0
  [✔] afl-fuzz        4.21c
  [✔] angr            9.2.108
  [✔] z3              4.13.0

── Bibliothèques Python ───────────────────────────────
  [✔] pyelftools      0.31
  [✔] lief            0.15.1
  [✔] r2pipe          1.9.0
  [✔] yara-python     4.3.1

── Outils complémentaires ─────────────────────────────
  [✔] wireshark       4.2.5
  [✔] tcpdump         4.99.4
  [✔] upx             4.2.2
  [✗] bindiff         NON TROUVÉ (optionnel)
  [✔] clang           18.1.3
  [✔] auditd          (disponible)
  [✔] inotifywait     (disponible)

── Toolchains optionnelles ────────────────────────────
  [~] rustc            NON TROUVÉ (optionnel — Partie VIII)
  [~] cargo            NON TROUVÉ (optionnel — Partie VIII)
  [~] go               NON TROUVÉ (optionnel — Partie VIII)
  [~] dotnet           NON TROUVÉ (optionnel — Partie VII)

── Binaires d'entraînement ────────────────────────────
  [✔] ch21-keygenme    5 binaires trouvés (attendu : 5)
  [✔] ch22-oop         6 binaires trouvés (attendu : 6)
  [✔] ch23-network     8 binaires trouvés (attendu : 8)
  [✔] ch24-crypto      4 binaires trouvés (attendu : 4)
  [✔] ch25-fileformat  4 binaires trouvés (attendu : 4)
  [✔] ch27-ransomware  4 binaires trouvés (attendu : 4)
  [✔] ch28-dropper     4 binaires trouvés (attendu : 4)
  [✔] ch29-packed      2 binaires trouvés (attendu : 2)
  [~] ch33-rust        0 binaires trouvés (attendu : 2) — toolchain manquante
  [~] ch34-go          0 binaires trouvés (attendu : 2) — toolchain manquante

── Environnement Python ───────────────────────────────
  [✔] venv actif       re-venv
  [✔] PATH venv        /home/re/re-venv/bin en tête du PATH

══════════════════════════════════════════════════════

  RÉSULTAT : 42/46 vérifications réussies
             0 échec(s) critique(s)
             4 élément(s) optionnel(s) manquant(s)

  ✔ Votre environnement est prêt pour la formation.

══════════════════════════════════════════════════════
```

### Les trois indicateurs

| Indicateur | Signification | Action requise |  
|---|---|---|  
| `[✔]` | L'outil est installé et fonctionnel | Aucune — tout va bien |  
| `[✗]` | L'outil est manquant ou défaillant | **Critique** si l'outil n'est pas marqué « optionnel » — à corriger avant de continuer |  
| `[~]` | L'outil est manquant mais **optionnel** | Aucune action immédiate — vous l'installerez si vous abordez les chapitres concernés |

### Le verdict final

Le script produit un résumé en trois lignes :

- **Vérifications réussies** — le nombre total de `[✔]`.  
- **Échecs critiques** — le nombre de `[✗]` sur des outils non optionnels. Si ce nombre est supérieur à zéro, vous devez corriger les problèmes avant de continuer.  
- **Éléments optionnels manquants** — le nombre de `[~]`. Zéro action requise sauf si vous comptez aborder les parties correspondantes.

Le message final est soit `✔ Votre environnement est prêt pour la formation.` (aucun échec critique), soit `✗ Des problèmes doivent être corrigés avant de continuer.` (au moins un échec critique).

---

## Comprendre ce que le script vérifie

Le script effectue cinq catégories de vérifications :

### 1. Présence des exécutables

Pour chaque outil, le script utilise `command -v` (ou `which`) pour vérifier que la commande existe dans le `PATH` :

```bash
if command -v gcc &>/dev/null; then
    version=$(gcc --version | head -1 | grep -oP '\d+\.\d+\.\d+')
    echo "  [✔] gcc             $version"
else
    echo "  [✗] gcc             NON TROUVÉ"
    CRITICAL_FAIL=$((CRITICAL_FAIL + 1))
fi
```

Pour les outils installés hors du `PATH` standard (Ghidra dans `/opt/ghidra`, Cutter en AppImage dans `~/tools/`), le script vérifie des chemins connus ou des alias définis dans `~/.bashrc`.

### 2. Versions minimales

Pour certains outils critiques, le script ne se contente pas de vérifier la présence — il contrôle aussi que la version est suffisante. Par exemple, Ghidra 11.x exige Java 17+. Si Java 11 est installé, Ghidra se lancera mais plantera à l'analyse. Le script détecte cette situation :

```
  [✗] java            11.0.22 (minimum requis : 17)
```

Les versions minimales vérifiées sont :

| Outil | Version minimale | Raison |  
|---|---|---|  
| Java (JDK) | 17 | Requis par Ghidra 11.x |  
| Python 3 | 3.10 | Compatibilité angr, pwntools |  
| GDB | 10.0 | Support des commandes utilisées par GEF/pwndbg |  
| Frida | 15.0 | API de hooking stable |

### 3. Extensions GDB

Le script inspecte le contenu de `~/.gdbinit` pour déterminer quelle extension GDB est configurée :

```bash
if grep -q "gef" ~/.gdbinit 2>/dev/null; then
    echo "  [✔] gdb-gef         GEF chargé dans ~/.gdbinit"
elif grep -q "pwndbg" ~/.gdbinit 2>/dev/null; then
    echo "  [✔] gdb-pwndbg      pwndbg chargé dans ~/.gdbinit"
elif grep -q "peda" ~/.gdbinit 2>/dev/null; then
    echo "  [✔] gdb-peda        PEDA chargé dans ~/.gdbinit"
else
    echo "  [✗] gdb-extension   Aucune extension configurée dans ~/.gdbinit"
fi
```

Au moins une des trois extensions doit être configurée. Laquelle est à votre discrétion (section 4.2 recommande GEF).

### 4. Paquets Python dans le venv

Le script vérifie que l'environnement virtuel `re-venv` est activé et que les paquets Python nécessaires sont importables :

```bash
python3 -c "import angr" 2>/dev/null && echo "  [✔] angr" || echo "  [✗] angr"
```

Si le venv n'est pas activé, les paquets pip installés dans `re-venv` ne seront pas visibles. Le script le détecte et affiche un avertissement explicite :

```
  [✗] venv actif       Aucun venv détecté — activez re-venv avant de relancer
```

### 5. Binaires d'entraînement

Pour chaque sous-dossier de `binaries/`, le script compte les fichiers exécutables ELF présents et compare au nombre attendu :

```bash
count=$(find binaries/ch21-keygenme -maxdepth 1 -type f -executable \
        -exec file {} \; | grep -c "ELF")
expected=5  
if [ "$count" -eq "$expected" ]; then  
    echo "  [✔] ch21-keygenme    $count binaires trouvés (attendu : $expected)"
else
    echo "  [✗] ch21-keygenme    $count binaires trouvés (attendu : $expected)"
fi
```

Si le nombre est inférieur au nombre attendu, cela signifie que `make all` n'a pas été exécuté ou qu'une compilation a échoué.

---

## Résoudre les échecs courants

### `[✗] gcc — NON TROUVÉ`

Le paquet `build-essential` n'est pas installé :

```bash
[vm] sudo apt install -y build-essential
```

### `[✗] ghidra — NON TROUVÉ`

Ghidra est installé dans `/opt/ghidra` mais l'alias n'est pas défini, ou le chemin est différent. Vérifiez :

```bash
[vm] ls /opt/ghidra*/ghidraRun
```

Si le fichier existe, ajoutez l'alias :

```bash
[vm] echo 'alias ghidra="/opt/ghidra/ghidraRun"' >> ~/.bashrc
[vm] source ~/.bashrc
```

Si le fichier n'existe pas, Ghidra n'a pas été installé — reprenez la procédure de la section 4.2.

### `[✗] java — 11.0.22 (minimum requis : 17)`

Une version trop ancienne de Java est installée. Installez OpenJDK 21 :

```bash
[vm] sudo apt install -y openjdk-21-jdk
```

Si plusieurs versions de Java coexistent, sélectionnez la bonne avec `update-alternatives` :

```bash
[vm] sudo update-alternatives --config java
```

Choisissez l'entrée correspondant à OpenJDK 21.

### `[✗] gdb-extension — Aucune extension configurée`

Aucune extension GDB n'est sourcée dans `~/.gdbinit`. Installez GEF (ou pwndbg, ou PEDA) en suivant les instructions de la section 4.2 :

```bash
[vm] bash -c "$(curl -fsSL https://gef.blah.cat/sh)"
```

### `[✗] venv actif — Aucun venv détecté`

L'environnement virtuel Python n'est pas activé. Activez-le :

```bash
[vm] source ~/re-venv/bin/activate
```

Si le dossier `~/re-venv` n'existe pas, créez-le :

```bash
[vm] python3 -m venv ~/re-venv
[vm] source ~/re-venv/bin/activate
```

Puis réinstallez les paquets pip nécessaires (section 4.2, vagues 4–5).

### `[✗] angr — NON TROUVÉ` (ou autre paquet pip)

Le paquet n'est pas installé dans le venv actif :

```bash
[vm] pip install angr
```

Si l'installation échoue, vérifiez que les dépendances système sont présentes :

```bash
[vm] sudo apt install -y python3-dev libffi-dev
[vm] pip install angr
```

### `[✗] ch24-crypto — 0 binaires trouvés (attendu : 4)`

La compilation du chapitre 24 a échoué, probablement à cause de la dépendance OpenSSL manquante :

```bash
[vm] sudo apt install -y libssl-dev
[vm] cd ~/formation-re/binaries/ch24-crypto
[vm] make clean && make all
```

### `[~] rustc — NON TROUVÉ (optionnel)`

C'est un outil optionnel. Si vous comptez aborder la Partie VIII (RE de binaires Rust), installez la toolchain :

```bash
[vm] curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
[vm] source ~/.cargo/env
```

Sinon, ignorez cet avertissement.

---

## Relancer le script après corrections

Après avoir corrigé les problèmes identifiés, relancez le script pour vérifier que tout est résolu :

```bash
[vm] ./check_env.sh
```

Répétez le cycle « corriger → relancer » jusqu'à obtenir zéro échec critique. Le script est idempotent — vous pouvez le lancer autant de fois que nécessaire sans effet de bord.

---

## Utiliser le script comme diagnostic

Le script n'est pas réservé à la mise en place initiale. Vous pouvez le relancer à tout moment de la formation pour diagnostiquer un problème :

- **Après une mise à jour système** (`apt upgrade`) — pour vérifier qu'aucun outil n'a été cassé ou supprimé.  
- **Après la restauration d'un snapshot** — pour confirmer que le snapshot contient bien tous les outils.  
- **En cas de comportement inattendu d'un outil** — pour vérifier que c'est bien la bonne version qui est dans le `PATH`.  
- **Sur une nouvelle VM** — si vous recréez votre environnement depuis zéro, le script sert de checklist automatisée.

> 💡 **Astuce** : redirigez la sortie vers un fichier pour garder une trace de l'état de votre environnement à un instant donné :  
> ```bash  
> [vm] ./check_env.sh | tee ~/check_env_$(date +%Y%m%d).log  
> ```

---

## Structure interne du script

Pour les curieux, voici la logique générale de `check_env.sh`. Comprendre sa structure vous sera utile lorsque vous écrirez vos propres scripts d'automatisation (chapitre 35).

```bash
#!/usr/bin/env bash
set -euo pipefail

# Compteurs
PASS=0  
FAIL=0  
OPTIONAL=0  

# Fonctions utilitaires
check_cmd() {
    local name="$1"
    local cmd="$2"
    local optional="${3:-false}"
    
    if command -v "$cmd" &>/dev/null; then
        local version
        version=$("$cmd" --version 2>&1 | head -1 || echo "disponible")
        printf "  [✔] %-16s %s\n" "$name" "$version"
        PASS=$((PASS + 1))
    elif [ "$optional" = "true" ]; then
        printf "  [~] %-16s NON TROUVÉ (optionnel)\n" "$name"
        OPTIONAL=$((OPTIONAL + 1))
    else
        printf "  [✗] %-16s NON TROUVÉ\n" "$name"
        FAIL=$((FAIL + 1))
    fi
}

check_python_pkg() {
    local name="$1"
    local optional="${2:-false}"
    
    if python3 -c "import $name" &>/dev/null; then
        local version
        version=$(python3 -c "import $name; print(getattr($name, '__version__', 'OK'))" 2>/dev/null)
        printf "  [✔] %-16s %s\n" "$name" "$version"
        PASS=$((PASS + 1))
    elif [ "$optional" = "true" ]; then
        printf "  [~] %-16s NON TROUVÉ (optionnel)\n" "$name"
        OPTIONAL=$((OPTIONAL + 1))
    else
        printf "  [✗] %-16s NON TROUVÉ\n" "$name"
        FAIL=$((FAIL + 1))
    fi
}

check_binaries_dir() {
    local dir="$1"
    local expected="$2"
    local count
    count=$(find "binaries/$dir" -maxdepth 1 -type f -executable \
            -exec file {} \; 2>/dev/null | grep -c "ELF" || echo 0)
    
    if [ "$count" -ge "$expected" ]; then
        printf "  [✔] %-16s %s binaires trouvés (attendu : %s)\n" "$dir" "$count" "$expected"
        PASS=$((PASS + 1))
    else
        printf "  [✗] %-16s %s binaires trouvés (attendu : %s)\n" "$dir" "$count" "$expected"
        FAIL=$((FAIL + 1))
    fi
}

# ── Vérifications ──
# ... appels à check_cmd, check_python_pkg, check_binaries_dir ...

# ── Verdict ──
echo ""  
echo "  RÉSULTAT : $PASS/$((PASS + FAIL + OPTIONAL)) vérifications réussies"  
echo "             $FAIL échec(s) critique(s)"  
echo "             $OPTIONAL élément(s) optionnel(s) manquant(s)"  

if [ "$FAIL" -eq 0 ]; then
    echo ""
    echo "  ✔ Votre environnement est prêt pour la formation."
else
    echo ""
    echo "  ✗ Des problèmes doivent être corrigés avant de continuer."
    exit 1
fi
```

Le script utilise `set -euo pipefail` pour un comportement strict (arrêt sur erreur non gérée), mais chaque vérification individuelle capture ses propres erreurs pour pouvoir continuer l'inspection même si un outil est absent.

Les trois fonctions utilitaires (`check_cmd`, `check_python_pkg`, `check_binaries_dir`) encapsulent les motifs de vérification récurrents. C'est un bon exemple de factorisation qu'on retrouvera dans les scripts du chapitre 35.

> 📌 Le script complet est dans le dépôt à la racine (`check_env.sh`). Ce qui est montré ici est une version simplifiée pour illustrer la logique. Le script réel inclut des vérifications supplémentaires (versions minimales, `.gdbinit`, état du venv, droits sur `/proc/sys/kernel/core_pattern` pour AFL++).

---

## Résumé

- Le script `check_env.sh` est votre **outil de diagnostic central**. Exécutez-le après l'installation initiale, après chaque mise à jour système, et à chaque restauration de snapshot.  
- Il vérifie la présence, les versions et la configuration de tous les outils, paquets Python, extensions GDB et binaires d'entraînement.  
- Les résultats sont classés en trois catégories : `[✔]` (OK), `[✗]` (échec critique à corriger), `[~]` (optionnel manquant).  
- **Zéro échec critique** est le prérequis pour passer à la suite de la formation.  
- Le script est lui-même un exemple de bonnes pratiques de scripting Bash que vous retrouverez et approfondirez au chapitre 35.

---


⏭️ [🎯 Checkpoint : exécuter `check_env.sh` — tous les outils doivent être au vert](/04-environnement-travail/checkpoint.md)
