#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════
# check_env.sh — Vérification de l'environnement de la formation
#                Reverse Engineering — Applications compilées (chaîne GNU)
#
# Usage : ./check_env.sh [--no-color] [--verbose]
#
# Licence MIT — Usage strictement éducatif et éthique.
# ═══════════════════════════════════════════════════════════════════════

set -uo pipefail

# ── Options CLI ──────────────────────────────────────────────────────

NO_COLOR=false
VERBOSE=false

for arg in "$@"; do
    case "$arg" in
        --no-color) NO_COLOR=true ;;
        --verbose)  VERBOSE=true ;;
        --help|-h)
            echo "Usage : $0 [--no-color] [--verbose]"
            echo "  --no-color   Désactive la coloration de la sortie"
            echo "  --verbose    Affiche les détails de chaque vérification"
            exit 0
            ;;
        *)
            echo "Option inconnue : $arg"
            echo "Usage : $0 [--no-color] [--verbose]"
            exit 1
            ;;
    esac
done

# ── Couleurs ─────────────────────────────────────────────────────────

if [ "$NO_COLOR" = true ] || [ ! -t 1 ]; then
    GREEN=""
    RED=""
    YELLOW=""
    CYAN=""
    BOLD=""
    DIM=""
    RESET=""
else
    GREEN="\033[0;32m"
    RED="\033[0;31m"
    YELLOW="\033[0;33m"
    CYAN="\033[0;36m"
    BOLD="\033[1m"
    DIM="\033[2m"
    RESET="\033[0m"
fi

# ── Compteurs ────────────────────────────────────────────────────────

PASS=0
FAIL=0
OPTIONAL_MISSING=0
WARNINGS=0
TOTAL=0

# ── Détection du répertoire du dépôt ─────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARIES_DIR="$SCRIPT_DIR/binaries"

# ── Fonctions utilitaires ────────────────────────────────────────────

print_header() {
    echo ""
    printf "  ${CYAN}${BOLD}── %s${RESET}\n" "$1"
    echo ""
}

print_banner() {
    echo ""
    echo -e "  ${BOLD}══════════════════════════════════════════════════════${RESET}"
    echo -e "  ${BOLD}  RE Lab — Vérification de l'environnement${RESET}"
    echo -e "  ${BOLD}══════════════════════════════════════════════════════${RESET}"
    echo ""
    echo -e "  ${DIM}Date    : $(date '+%Y-%m-%d %H:%M:%S')${RESET}"
    echo -e "  ${DIM}Système : $(uname -srm)${RESET}"
    echo -e "  ${DIM}Dépôt   : $SCRIPT_DIR${RESET}"
}

# Vérifie la présence d'une commande dans le PATH
# Arguments : nom_affiché  commande  [optional:true/false]
check_cmd() {
    local name="$1"
    local cmd="$2"
    local optional="${3:-false}"

    TOTAL=$((TOTAL + 1))

    if command -v "$cmd" &>/dev/null; then
        local version
        version=$( ("$cmd" --version 2>&1 || true) | head -1 | sed 's/^[^0-9]*//' | cut -d' ' -f1 )
        [ -z "$version" ] && version="(disponible)"
        printf "  ${GREEN}[✔]${RESET} %-20s %s\n" "$name" "$version"
        PASS=$((PASS + 1))
        return 0
    elif [ "$optional" = "true" ]; then
        printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}NON TROUVÉ (optionnel)${RESET}\n" "$name"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
        return 1
    else
        printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ\n" "$name"
        FAIL=$((FAIL + 1))
        return 1
    fi
}

# Vérifie la présence d'une commande ET sa version minimale
# Arguments : nom_affiché  commande  version_minimale  [optional:true/false]
check_cmd_version() {
    local name="$1"
    local cmd="$2"
    local min_version="$3"
    local optional="${4:-false}"

    TOTAL=$((TOTAL + 1))

    if ! command -v "$cmd" &>/dev/null; then
        if [ "$optional" = "true" ]; then
            printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}NON TROUVÉ (optionnel)${RESET}\n" "$name"
            OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
        else
            printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ\n" "$name"
            FAIL=$((FAIL + 1))
        fi
        return 1
    fi

    local version
    version=$( ("$cmd" --version 2>&1 || true) | head -1 | grep -oP '\d+\.\d+(\.\d+)?' | head -1 )
    [ -z "$version" ] && version="unknown"

    if version_gte "$version" "$min_version"; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(>= %s)${RESET}\n" "$name" "$version" "$min_version"
        PASS=$((PASS + 1))
        return 0
    else
        printf "  ${RED}[✗]${RESET} %-20s %s ${RED}(minimum requis : %s)${RESET}\n" "$name" "$version" "$min_version"
        FAIL=$((FAIL + 1))
        return 1
    fi
}

# Vérifie l'importabilité d'un paquet Python
# Arguments : nom_affiché  module_python  [optional:true/false]
check_python_pkg() {
    local name="$1"
    local module="$2"
    local optional="${3:-false}"

    TOTAL=$((TOTAL + 1))

    if ! command -v python3 &>/dev/null; then
        printf "  ${RED}[✗]${RESET} %-20s python3 non disponible\n" "$name"
        FAIL=$((FAIL + 1))
        return 1
    fi

    local version
    if version=$(python3 -c "
import importlib
m = importlib.import_module('$module')
v = getattr(m, '__version__', None)
if v is None:
    v = getattr(m, 'VERSION', None)
if v is None:
    v = 'OK'
print(v)
" 2>/dev/null); then
        printf "  ${GREEN}[✔]${RESET} %-20s %s\n" "$name" "$version"
        PASS=$((PASS + 1))
        return 0
    elif [ "$optional" = "true" ]; then
        printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}NON TROUVÉ (optionnel)${RESET}\n" "$name"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
        return 1
    else
        printf "  ${RED}[✗]${RESET} %-20s NON IMPORTABLE\n" "$name"
        FAIL=$((FAIL + 1))
        return 1
    fi
}

# Vérifie le nombre de binaires ELF dans un sous-dossier de binaries/
# Arguments : nom_du_dossier  nombre_attendu  [optional:true/false]
check_binaries_dir() {
    local dir="$1"
    local expected="$2"
    local optional="${3:-false}"

    TOTAL=$((TOTAL + 1))

    local full_path="$BINARIES_DIR/$dir"

    if [ ! -d "$full_path" ]; then
        if [ "$optional" = "true" ]; then
            printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}Répertoire absent (optionnel)${RESET}\n" "$dir"
            OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
        else
            printf "  ${RED}[✗]${RESET} %-20s Répertoire absent\n" "$dir"
            FAIL=$((FAIL + 1))
        fi
        return 1
    fi

    local count
    count=$(find "$full_path" -maxdepth 1 -type f -executable \
            -exec file {} \; 2>/dev/null | grep -c "ELF" || echo 0)

    if [ "$count" -ge "$expected" ]; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s binaire(s) ELF (attendu : %s)\n" "$dir" "$count" "$expected"
        PASS=$((PASS + 1))
        return 0
    elif [ "$optional" = "true" ]; then
        printf "  ${YELLOW}[~]${RESET} %-20s %s binaire(s) ELF (attendu : %s) ${DIM}— toolchain manquante ?${RESET}\n" "$dir" "$count" "$expected"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
        return 1
    else
        printf "  ${RED}[✗]${RESET} %-20s %s binaire(s) ELF (attendu : %s) ${DIM}— lancez make all${RESET}\n" "$dir" "$count" "$expected"
        FAIL=$((FAIL + 1))
        return 1
    fi
}

# Compare deux versions sémantiques (a >= b)
# Retourne 0 (vrai) si $1 >= $2
version_gte() {
    local v1="$1"
    local v2="$2"

    # Normalise à 3 composants
    local a1 a2 a3 b1 b2 b3
    IFS='.' read -r a1 a2 a3 <<< "$v1"
    IFS='.' read -r b1 b2 b3 <<< "$v2"
    a1=${a1:-0}; a2=${a2:-0}; a3=${a3:-0}
    b1=${b1:-0}; b2=${b2:-0}; b3=${b3:-0}

    if (( a1 > b1 )); then return 0; fi
    if (( a1 < b1 )); then return 1; fi
    if (( a2 > b2 )); then return 0; fi
    if (( a2 < b2 )); then return 1; fi
    if (( a3 >= b3 )); then return 0; fi
    return 1
}

# Vérifie qu'un fichier ou répertoire existe
# Arguments : nom_affiché  chemin  [optional:true/false]
check_path() {
    local name="$1"
    local path="$2"
    local optional="${3:-false}"

    TOTAL=$((TOTAL + 1))

    if [ -e "$path" ]; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s\n" "$name" "$path"
        PASS=$((PASS + 1))
        return 0
    elif [ "$optional" = "true" ]; then
        printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}%s non trouvé (optionnel)${RESET}\n" "$name" "$path"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
        return 1
    else
        printf "  ${RED}[✗]${RESET} %-20s %s non trouvé\n" "$name" "$path"
        FAIL=$((FAIL + 1))
        return 1
    fi
}

# Affiche un message verbose (uniquement si --verbose)
verbose() {
    if [ "$VERBOSE" = true ]; then
        echo -e "      ${DIM}↳ $1${RESET}"
    fi
}

# ═════════════════════════════════════════════════════════════════════
#  DÉBUT DES VÉRIFICATIONS
# ═════════════════════════════════════════════════════════════════════

print_banner

# ─────────────────────────────────────────────────────────────────────
#  VAGUE 1 — Socle système et langages
# ─────────────────────────────────────────────────────────────────────

print_header "Socle système et langages"

check_cmd_version "gcc" "gcc" "11.0"
verbose "Paquet : build-essential"

check_cmd_version "g++" "g++" "11.0"
verbose "Paquet : build-essential"

check_cmd "make" "make"
verbose "Paquet : build-essential"

check_cmd_version "python3" "python3" "3.10"
verbose "Paquet : python3"

# pip — extraction de version spécifique
TOTAL=$((TOTAL + 1))
if command -v pip &>/dev/null; then
    pip_version=$(pip --version 2>&1 | grep -oP '\d+\.\d+(\.\d+)?' | head -1)
    printf "  ${GREEN}[✔]${RESET} %-20s %s\n" "pip" "$pip_version"
    PASS=$((PASS + 1))
elif command -v pip3 &>/dev/null; then
    pip_version=$(pip3 --version 2>&1 | grep -oP '\d+\.\d+(\.\d+)?' | head -1)
    printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(via pip3)${RESET}\n" "pip" "$pip_version"
    PASS=$((PASS + 1))
else
    printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ\n" "pip"
    FAIL=$((FAIL + 1))
fi
verbose "Paquet : python3-pip"

# Java — version spécifique pour Ghidra
TOTAL=$((TOTAL + 1))
if command -v java &>/dev/null; then
    java_version=$(java -version 2>&1 | head -1 | grep -oP '\d+\.\d+\.\d+|\d+' | head -1)
    java_major=$(echo "$java_version" | cut -d. -f1)
    if [ "$java_major" -ge 17 ] 2>/dev/null; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(>= 17, requis par Ghidra)${RESET}\n" "java (JDK)" "$java_version"
        PASS=$((PASS + 1))
    else
        printf "  ${RED}[✗]${RESET} %-20s %s ${RED}(minimum requis : 17 pour Ghidra)${RESET}\n" "java (JDK)" "$java_version"
        FAIL=$((FAIL + 1))
    fi
else
    printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ ${DIM}(requis par Ghidra)${RESET}\n" "java (JDK)"
    FAIL=$((FAIL + 1))
fi
verbose "Paquet : openjdk-21-jdk"

check_cmd "git" "git"

# ─────────────────────────────────────────────────────────────────────
#  VAGUE 2 — Outils CLI d'inspection et de débogage
# ─────────────────────────────────────────────────────────────────────

print_header "Outils CLI d'inspection et débogage"

check_cmd_version "gdb" "gdb" "10.0"
verbose "Paquet : gdb"

check_cmd "strace" "strace"
check_cmd "ltrace" "ltrace"

check_cmd_version "valgrind" "valgrind" "3.18"
verbose "Paquet : valgrind"

# checksec — peut être un script système ou un paquet pip
TOTAL=$((TOTAL + 1))
if command -v checksec &>/dev/null; then
    checksec_v=$(checksec --version 2>&1 | grep -oP '\d+\.\d+(\.\d+)?' | head -1 || echo "(disponible)")
    [ -z "$checksec_v" ] && checksec_v="(disponible)"
    printf "  ${GREEN}[✔]${RESET} %-20s %s\n" "checksec" "$checksec_v"
    PASS=$((PASS + 1))
elif python3 -c "import checksec" &>/dev/null 2>&1; then
    printf "  ${GREEN}[✔]${RESET} %-20s ${DIM}(via pip checksec.py)${RESET}\n" "checksec"
    PASS=$((PASS + 1))
else
    printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ\n" "checksec"
    FAIL=$((FAIL + 1))
fi

check_cmd "yara" "yara"
check_cmd "file" "file"

# binutils — vérification groupée
for tool in strings readelf objdump nm c__filt strip objcopy; do
    actual_cmd="${tool/c__filt/c++filt}"
    check_cmd "$actual_cmd" "$actual_cmd"
done

check_cmd "nasm" "nasm"
check_cmd "binwalk" "binwalk"
check_cmd "xxd" "xxd"

# ─────────────────────────────────────────────────────────────────────
#  VAGUE 3 — Désassembleurs et éditeurs graphiques
# ─────────────────────────────────────────────────────────────────────

print_header "Désassembleurs et éditeurs graphiques"

# Ghidra — installé manuellement dans /opt
TOTAL=$((TOTAL + 1))
GHIDRA_FOUND=false
GHIDRA_PATHS=(
    "/opt/ghidra/ghidraRun"
    "/opt/ghidra*/ghidraRun"
    "$HOME/tools/ghidra*/ghidraRun"
    "$HOME/ghidra*/ghidraRun"
)

for pattern in "${GHIDRA_PATHS[@]}"; do
    # shellcheck disable=SC2086
    for candidate in $pattern; do
        if [ -x "$candidate" ] 2>/dev/null; then
            ghidra_dir=$(dirname "$candidate")
            # Tente d'extraire la version depuis le nom du dossier ou application.properties
            ghidra_version="(trouvé)"
            if [ -f "$ghidra_dir/application.properties" ]; then
                ghidra_version=$(grep "application.version" "$ghidra_dir/application.properties" 2>/dev/null \
                    | cut -d= -f2 | tr -d ' ' || echo "(trouvé)")
            fi
            printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(%s)${RESET}\n" "ghidra" "$ghidra_version" "$ghidra_dir"
            PASS=$((PASS + 1))
            GHIDRA_FOUND=true
            break 2
        fi
    done
done

if [ "$GHIDRA_FOUND" = false ]; then
    # Vérifie aussi via un alias
    if alias ghidra &>/dev/null 2>&1 || type ghidra &>/dev/null 2>&1; then
        printf "  ${GREEN}[✔]${RESET} %-20s ${DIM}(via alias)${RESET}\n" "ghidra"
        PASS=$((PASS + 1))
    else
        printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ ${DIM}(cherché dans /opt et ~/tools)${RESET}\n" "ghidra"
        FAIL=$((FAIL + 1))
    fi
fi

check_cmd "radare2" "r2"
verbose "Installé depuis source : github.com/radareorg/radare2"

# Cutter — peut être installé via apt ou AppImage
TOTAL=$((TOTAL + 1))
if command -v cutter &>/dev/null || command -v Cutter &>/dev/null; then
    printf "  ${GREEN}[✔]${RESET} %-20s (disponible)\n" "cutter"
    PASS=$((PASS + 1))
elif ls "$HOME/tools/Cutter"*.AppImage &>/dev/null 2>&1; then
    printf "  ${GREEN}[✔]${RESET} %-20s ${DIM}(AppImage dans ~/tools)${RESET}\n" "cutter"
    PASS=$((PASS + 1))
else
    printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}NON TROUVÉ (optionnel — GUI de Radare2)${RESET}\n" "cutter"
    OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
fi

# ImHex — peut être installé via apt, .deb, AppImage ou Flatpak
TOTAL=$((TOTAL + 1))
if command -v imhex &>/dev/null; then
    imhex_v=$(imhex --version 2>&1 | grep -oP '\d+\.\d+\.\d+' | head -1 || echo "(disponible)")
    [ -z "$imhex_v" ] && imhex_v="(disponible)"
    printf "  ${GREEN}[✔]${RESET} %-20s %s\n" "imhex" "$imhex_v"
    PASS=$((PASS + 1))
elif flatpak list 2>/dev/null | grep -qi imhex; then
    printf "  ${GREEN}[✔]${RESET} %-20s ${DIM}(Flatpak)${RESET}\n" "imhex"
    PASS=$((PASS + 1))
elif ls "$HOME/tools/"*[Ii]m[Hh]ex* &>/dev/null 2>&1; then
    printf "  ${GREEN}[✔]${RESET} %-20s ${DIM}(AppImage dans ~/tools)${RESET}\n" "imhex"
    PASS=$((PASS + 1))
else
    printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ\n" "imhex"
    FAIL=$((FAIL + 1))
fi

# IDA Free — optionnel
TOTAL=$((TOTAL + 1))
if command -v ida64 &>/dev/null || command -v idat64 &>/dev/null; then
    printf "  ${GREEN}[✔]${RESET} %-20s (disponible)\n" "ida-free"
    PASS=$((PASS + 1))
elif [ -x "$HOME/idafree"*/ida64 ] 2>/dev/null || [ -x "/opt/idafree"*/ida64 ] 2>/dev/null; then
    printf "  ${GREEN}[✔]${RESET} %-20s (disponible)\n" "ida-free"
    PASS=$((PASS + 1))
else
    printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}NON TROUVÉ (optionnel)${RESET}\n" "ida-free"
    OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
fi

# ─────────────────────────────────────────────────────────────────────
#  VAGUE 4 — Extensions GDB, frameworks dynamiques
# ─────────────────────────────────────────────────────────────────────

print_header "Extensions GDB et frameworks dynamiques"

# Extensions GDB — au moins une doit être configurée
TOTAL=$((TOTAL + 1))
GDB_EXT_FOUND=false
GDB_EXT_NAME=""

if [ -f "$HOME/.gdbinit" ]; then
    if grep -qiE "gef|GEF" "$HOME/.gdbinit" 2>/dev/null; then
        GDB_EXT_FOUND=true
        GDB_EXT_NAME="GEF"
    elif grep -qi "pwndbg" "$HOME/.gdbinit" 2>/dev/null; then
        GDB_EXT_FOUND=true
        GDB_EXT_NAME="pwndbg"
    elif grep -qi "peda" "$HOME/.gdbinit" 2>/dev/null; then
        GDB_EXT_FOUND=true
        GDB_EXT_NAME="PEDA"
    fi
fi

if [ "$GDB_EXT_FOUND" = true ]; then
    printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(chargé dans ~/.gdbinit)${RESET}\n" "gdb-extension" "$GDB_EXT_NAME"
    PASS=$((PASS + 1))
else
    printf "  ${RED}[✗]${RESET} %-20s Aucune extension détectée dans ~/.gdbinit ${DIM}(GEF/pwndbg/PEDA)${RESET}\n" "gdb-extension"
    FAIL=$((FAIL + 1))
fi

# Frida
TOTAL=$((TOTAL + 1))
if command -v frida &>/dev/null; then
    frida_v=$(frida --version 2>&1 | head -1 || echo "(disponible)")
    frida_major=$(echo "$frida_v" | cut -d. -f1)
    if [ "${frida_major:-0}" -ge 15 ] 2>/dev/null; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(>= 15.0)${RESET}\n" "frida" "$frida_v"
        PASS=$((PASS + 1))
    else
        printf "  ${RED}[✗]${RESET} %-20s %s ${RED}(minimum requis : 15.0)${RESET}\n" "frida" "$frida_v"
        FAIL=$((FAIL + 1))
    fi
else
    printf "  ${RED}[✗]${RESET} %-20s NON TROUVÉ ${DIM}(pip install frida-tools frida)${RESET}\n" "frida"
    FAIL=$((FAIL + 1))
fi

check_cmd "frida-trace" "frida-trace"

check_cmd "afl-fuzz" "afl-fuzz"
verbose "Paquet : afl++ ou compilation depuis source"

check_cmd "afl-gcc" "afl-gcc" true
verbose "Compilateur instrumenté AFL++"

# ─────────────────────────────────────────────────────────────────────
#  VAGUE 4 (suite) — Bibliothèques Python
# ─────────────────────────────────────────────────────────────────────

print_header "Bibliothèques Python (dans le venv)"

# Vérification du venv
TOTAL=$((TOTAL + 1))
if [ -n "${VIRTUAL_ENV:-}" ]; then
    venv_name=$(basename "$VIRTUAL_ENV")
    printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(%s)${RESET}\n" "venv actif" "$venv_name" "$VIRTUAL_ENV"
    PASS=$((PASS + 1))
else
    printf "  ${RED}[✗]${RESET} %-20s ${RED}Aucun venv détecté${RESET} ${DIM}— exécutez : source ~/re-venv/bin/activate${RESET}\n" "venv actif"
    FAIL=$((FAIL + 1))
    WARNINGS=$((WARNINGS + 1))
fi

# Vérification du PATH venv
TOTAL=$((TOTAL + 1))
if [ -n "${VIRTUAL_ENV:-}" ] && echo "$PATH" | grep -q "$VIRTUAL_ENV/bin"; then
    printf "  ${GREEN}[✔]${RESET} %-20s ${DIM}$VIRTUAL_ENV/bin en tête du PATH${RESET}\n" "PATH venv"
    PASS=$((PASS + 1))
else
    printf "  ${YELLOW}[~]${RESET} %-20s ${DIM}Le venv n'est pas en tête du PATH${RESET}\n" "PATH venv"
    OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
fi

check_python_pkg "pwntools"    "pwn"
check_python_pkg "angr"        "angr"
check_python_pkg "z3-solver"   "z3"
check_python_pkg "pyelftools"  "elftools"
check_python_pkg "lief"        "lief"
check_python_pkg "r2pipe"      "r2pipe"
check_python_pkg "yara-python" "yara"

check_python_pkg "capstone"    "capstone"   true
check_python_pkg "keystone"    "keystone"   true
check_python_pkg "unicorn"     "unicorn"    true

# ─────────────────────────────────────────────────────────────────────
#  VAGUE 5 — Outils complémentaires
# ─────────────────────────────────────────────────────────────────────

print_header "Outils complémentaires"

check_cmd "wireshark" "wireshark"
verbose "Paquet : wireshark — vérifiez que l'utilisateur est dans le groupe wireshark"

check_cmd "tcpdump" "tcpdump"
check_cmd "upx" "upx"
check_cmd "clang" "clang"

# BinDiff — optionnel
check_cmd "bindiff" "bindiff" true

# Outils de monitoring (Partie VI)
check_cmd "auditctl" "auditctl"
verbose "Paquet : auditd"

check_cmd "inotifywait" "inotifywait"
verbose "Paquet : inotify-tools"

check_cmd "sysdig" "sysdig" true

# KCachegrind — optionnel (GUI pour Callgrind)
check_cmd "kcachegrind" "kcachegrind" true

check_cmd "tmux" "tmux"

# ─────────────────────────────────────────────────────────────────────
#  Toolchains optionnelles (Parties VII et VIII)
# ─────────────────────────────────────────────────────────────────────

print_header "Toolchains optionnelles (Parties VII–VIII)"

check_cmd "rustc"  "rustc"  true
check_cmd "cargo"  "cargo"  true
check_cmd "go"     "go"     true
check_cmd "dotnet" "dotnet" true

# ─────────────────────────────────────────────────────────────────────
#  Binaires d'entraînement
# ─────────────────────────────────────────────────────────────────────

print_header "Binaires d'entraînement (binaries/)"

if [ ! -d "$BINARIES_DIR" ]; then
    TOTAL=$((TOTAL + 1))
    printf "  ${RED}[✗]${RESET} %-20s Répertoire binaries/ non trouvé dans %s\n" "binaries/" "$SCRIPT_DIR"
    FAIL=$((FAIL + 1))
else
    check_binaries_dir "ch21-keygenme"    5
    check_binaries_dir "ch22-oop"         6
    check_binaries_dir "ch23-network"     8
    check_binaries_dir "ch24-crypto"      4
    check_binaries_dir "ch25-fileformat"  4
    check_binaries_dir "ch27-ransomware"  4
    check_binaries_dir "ch28-dropper"     4
    check_binaries_dir "ch29-packed"      2
    check_binaries_dir "ch33-rust"        2 true
    check_binaries_dir "ch34-go"          2 true
fi

# ─────────────────────────────────────────────────────────────────────
#  Ressources du dépôt
# ─────────────────────────────────────────────────────────────────────

print_header "Ressources du dépôt"

check_path "scripts/"    "$SCRIPT_DIR/scripts"
check_path "hexpat/"     "$SCRIPT_DIR/hexpat"
check_path "yara-rules/" "$SCRIPT_DIR/yara-rules"
check_path "annexes/"    "$SCRIPT_DIR/annexes"
check_path "solutions/"  "$SCRIPT_DIR/solutions"

# ─────────────────────────────────────────────────────────────────────
#  Vérifications système
# ─────────────────────────────────────────────────────────────────────

print_header "Configuration système"

# core_pattern — nécessaire pour AFL++
TOTAL=$((TOTAL + 1))
if [ -f /proc/sys/kernel/core_pattern ]; then
    core_pattern=$(cat /proc/sys/kernel/core_pattern 2>/dev/null)
    if [ "$core_pattern" = "core" ]; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(compatible AFL++)${RESET}\n" "core_pattern" "$core_pattern"
        PASS=$((PASS + 1))
    else
        printf "  ${YELLOW}[~]${RESET} %-20s %s ${DIM}— AFL++ requiert 'core' (echo core | sudo tee /proc/sys/kernel/core_pattern)${RESET}\n" "core_pattern" "$core_pattern"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
    fi
else
    printf "  ${DIM}  %-20s (non applicable — /proc non disponible)${RESET}\n" "core_pattern"
    TOTAL=$((TOTAL - 1))
fi

# ptrace_scope — nécessaire pour Frida, GDB attach, strace
TOTAL=$((TOTAL + 1))
if [ -f /proc/sys/kernel/yama/ptrace_scope ]; then
    ptrace_scope=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null)
    if [ "$ptrace_scope" -le 1 ] 2>/dev/null; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s ${DIM}(0=all, 1=parent — OK)${RESET}\n" "ptrace_scope" "$ptrace_scope"
        PASS=$((PASS + 1))
    else
        printf "  ${YELLOW}[~]${RESET} %-20s %s ${DIM}— Frida/GDB attach peut nécessiter sudo ou : echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope${RESET}\n" "ptrace_scope" "$ptrace_scope"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
    fi
else
    printf "  ${DIM}  %-20s (non applicable — Yama non activé)${RESET}\n" "ptrace_scope"
    TOTAL=$((TOTAL - 1))
fi

# ASLR — information (pas un échec, juste informatif)
TOTAL=$((TOTAL + 1))
if [ -f /proc/sys/kernel/randomize_va_space ]; then
    aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)
    case "$aslr" in
        0) aslr_label="désactivé" ;;
        1) aslr_label="partiel (stack)" ;;
        2) aslr_label="complet" ;;
        *) aslr_label="inconnu ($aslr)" ;;
    esac
    printf "  ${GREEN}[✔]${RESET} %-20s %s — %s\n" "ASLR" "$aslr" "$aslr_label"
    PASS=$((PASS + 1))
else
    printf "  ${DIM}  %-20s (non applicable)${RESET}\n" "ASLR"
    TOTAL=$((TOTAL - 1))
fi

# Espace disque disponible
TOTAL=$((TOTAL + 1))
if command -v df &>/dev/null; then
    avail_kb=$(df --output=avail "$HOME" 2>/dev/null | tail -1 | tr -d ' ')
    avail_gb=$((avail_kb / 1024 / 1024))
    if [ "$avail_gb" -ge 15 ]; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s Go disponibles\n" "espace disque" "$avail_gb"
        PASS=$((PASS + 1))
    elif [ "$avail_gb" -ge 5 ]; then
        printf "  ${YELLOW}[~]${RESET} %-20s %s Go disponibles ${DIM}(15 Go+ recommandés)${RESET}\n" "espace disque" "$avail_gb"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
    else
        printf "  ${RED}[✗]${RESET} %-20s %s Go disponibles ${RED}(insuffisant — 15 Go+ recommandés)${RESET}\n" "espace disque" "$avail_gb"
        FAIL=$((FAIL + 1))
    fi
fi

# RAM totale
TOTAL=$((TOTAL + 1))
if [ -f /proc/meminfo ]; then
    mem_total_kb=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    mem_total_gb=$((mem_total_kb / 1024 / 1024))
    mem_total_mb=$((mem_total_kb / 1024))
    if [ "$mem_total_mb" -ge 7500 ]; then
        printf "  ${GREEN}[✔]${RESET} %-20s %s Go\n" "RAM totale" "$mem_total_gb"
        PASS=$((PASS + 1))
    elif [ "$mem_total_mb" -ge 3500 ]; then
        printf "  ${YELLOW}[~]${RESET} %-20s %s Go ${DIM}(8 Go recommandés)${RESET}\n" "RAM totale" "$mem_total_gb"
        OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
    else
        printf "  ${RED}[✗]${RESET} %-20s %s Go ${RED}(minimum 4 Go, 8 Go recommandés)${RESET}\n" "RAM totale" "$mem_total_gb"
        FAIL=$((FAIL + 1))
    fi
fi

# Architecture
TOTAL=$((TOTAL + 1))
arch=$(uname -m)
if [ "$arch" = "x86_64" ]; then
    printf "  ${GREEN}[✔]${RESET} %-20s %s\n" "architecture" "$arch"
    PASS=$((PASS + 1))
else
    printf "  ${YELLOW}[~]${RESET} %-20s %s ${DIM}(les binaires cibles sont x86-64 — émulation nécessaire)${RESET}\n" "architecture" "$arch"
    OPTIONAL_MISSING=$((OPTIONAL_MISSING + 1))
fi

# ═════════════════════════════════════════════════════════════════════
#  VERDICT FINAL
# ═════════════════════════════════════════════════════════════════════

echo ""
echo -e "  ${BOLD}══════════════════════════════════════════════════════${RESET}"
echo ""
printf "  ${BOLD}RÉSULTAT${RESET} : %s/%s vérifications réussies\n" "$PASS" "$TOTAL"
printf "             ${RED}%s${RESET} échec(s) critique(s)\n" "$FAIL"
printf "             ${YELLOW}%s${RESET} élément(s) optionnel(s) manquant(s)\n" "$OPTIONAL_MISSING"

if [ "$WARNINGS" -gt 0 ]; then
    printf "             ${YELLOW}%s${RESET} avertissement(s)\n" "$WARNINGS"
fi

echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "  ${GREEN}${BOLD}✔ Votre environnement est prêt pour la formation.${RESET}"
    if [ "$OPTIONAL_MISSING" -gt 0 ]; then
        echo -e "  ${DIM}  Les éléments optionnels manquants ne bloquent pas le parcours principal.${RESET}"
    fi
    echo ""
    echo -e "  ${BOLD}══════════════════════════════════════════════════════${RESET}"
    echo ""
    exit 0
else
    echo -e "  ${RED}${BOLD}✗ Des problèmes doivent être corrigés avant de continuer.${RESET}"
    echo ""
    echo -e "  ${DIM}  Corrigez les éléments marqués [✗], puis relancez :${RESET}"
    echo -e "  ${DIM}  ./check_env.sh${RESET}"
    echo ""
    echo -e "  ${BOLD}══════════════════════════════════════════════════════${RESET}"
    echo ""
    exit 1
fi
