# ============================================================================
#  Formation Reverse Engineering — Chapitre 27
#  Makefile : compilation du sample ransomware pédagogique
# ============================================================================
#
#  ⚠️  Usage strictement éducatif — VM sandboxée obligatoire.
#
#  Cibles :
#    make all        → compile les 3 variantes
#    make debug      → ransomware_O0       (debug, symboles DWARF)
#    make optimized  → ransomware_O2       (optimisé, symboles)
#    make stripped   → ransomware_O2_strip (optimisé, sans symboles)
#    make testenv    → crée /tmp/test/ avec des fichiers de test
#    make clean      → supprime les binaires compilés
#    make reset      → supprime /tmp/test/ et le recrée propre
#
#  Dépendance : libssl-dev
#    sudo apt install libssl-dev
# ============================================================================

CC        := gcc
SRC       := ransomware_sample.c
LIBS      := -lssl -lcrypto
WARNINGS  := -Wall -Wextra -Wpedantic

# --- Flags par variante ---------------------------------------------------

CFLAGS_DBG   := $(WARNINGS) -O0 -g3 -ggdb -DDEBUG
CFLAGS_OPT   := $(WARNINGS) -O2 -g
CFLAGS_STRIP := $(WARNINGS) -O2 -s

# --- Noms des binaires de sortie -------------------------------------------

BIN_DBG   := ransomware_O0
BIN_OPT   := ransomware_O2
BIN_STRIP := ransomware_O2_strip

# --- Répertoire et fichiers de test ----------------------------------------

TEST_DIR    := /tmp/test

# ============================================================================
#  Cibles
# ============================================================================

.PHONY: all debug optimized stripped clean testenv reset help

all: debug optimized stripped
	@echo ""
	@echo "[✓] 3 variantes compilées :"
	@ls -lh $(BIN_DBG) $(BIN_OPT) $(BIN_STRIP)
	@echo ""
	@echo "Pensez à exécuter 'make testenv' pour préparer /tmp/test/"

# --- Variante debug (O0, symboles DWARF) -----------------------------------

debug: $(BIN_DBG)

$(BIN_DBG): $(SRC)
	$(CC) $(CFLAGS_DBG) -o $@ $< $(LIBS)
	@echo "[✓] $@ compilé (debug, symboles DWARF)"

# --- Variante optimisée (O2, symboles) -------------------------------------

optimized: $(BIN_OPT)

$(BIN_OPT): $(SRC)
	$(CC) $(CFLAGS_OPT) -o $@ $< $(LIBS)
	@echo "[✓] $@ compilé (optimisé, avec symboles)"

# --- Variante strippée (O2, sans symboles) ----------------------------------

stripped: $(BIN_STRIP)

$(BIN_STRIP): $(SRC)
	$(CC) $(CFLAGS_STRIP) -o $@ $< $(LIBS)
	strip --strip-all $@
	@echo "[✓] $@ compilé (optimisé, strippé)"

# --- Préparation de l'environnement de test ---------------------------------

testenv:
	@echo "[*] Création de $(TEST_DIR) avec des fichiers de test..."
	@mkdir -p $(TEST_DIR)
	@mkdir -p $(TEST_DIR)/sous-dossier
	@echo "Ceci est un document confidentiel très important." > $(TEST_DIR)/document.txt
	@echo "# Notes de réunion\n\n- Point 1\n- Point 2\n- Point 3" > $(TEST_DIR)/notes.md
	@echo "Contenu fictif d'un rapport PDF pour l'exercice." > $(TEST_DIR)/rapport.pdf
	@head -c 2048 /dev/urandom > $(TEST_DIR)/photo.jpg
	@echo "nom,prenom,salaire\nDupont,Jean,45000\nMartin,Claire,52000" > $(TEST_DIR)/budget.csv
	@echo "Fichier dans un sous-dossier pour tester la récursion." > $(TEST_DIR)/sous-dossier/nested.txt
	@echo ""
	@echo "[✓] Environnement de test prêt :"
	@find $(TEST_DIR) -type f | sort
	@echo ""
	@echo "⚠️  Prenez un snapshot de la VM MAINTENANT avant d'exécuter le sample."

# --- Reset : nettoie et recrée l'environnement de test ----------------------

reset:
	@echo "[*] Suppression de $(TEST_DIR)..."
	@rm -rf $(TEST_DIR)
	@$(MAKE) --no-print-directory testenv

# --- Nettoyage des binaires -------------------------------------------------

clean:
	rm -f $(BIN_DBG) $(BIN_OPT) $(BIN_STRIP)
	@echo "[✓] Binaires supprimés."

# --- Aide -------------------------------------------------------------------

help:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════╗"
	@echo "║  Chapitre 27 — Makefile du sample ransomware            ║"
	@echo "╠══════════════════════════════════════════════════════════╣"
	@echo "║  make all        Compile les 3 variantes                ║"
	@echo "║  make debug      ransomware_O0       (-O0 -g3)         ║"
	@echo "║  make optimized  ransomware_O2       (-O2 -g)          ║"
	@echo "║  make stripped   ransomware_O2_strip (-O2, strip)       ║"
	@echo "║  make testenv    Crée /tmp/test/ avec fichiers de test  ║"
	@echo "║  make reset      Nettoie et recrée /tmp/test/           ║"
	@echo "║  make clean      Supprime les binaires                  ║"
	@echo "║  make help       Affiche cette aide                     ║"
	@echo "╠══════════════════════════════════════════════════════════╣"
	@echo "║  ⚠️  VM sandboxée obligatoire pour l'exécution !        ║"
	@echo "║  Dépendance : sudo apt install libssl-dev               ║"
	@echo "╚══════════════════════════════════════════════════════════╝"
	@echo ""
