# ============================================================================
# Formation Reverse Engineering — Chapitre 17 
# Makefile pour le binaire d'entraînement C++ orienté objet
#
# Produit plusieurs variantes du même source pour permettre la comparaison
# en analyse statique et dynamique :
#
#   oop_O0          → -O0 -g     (non optimisé, symboles complets)
#   oop_O0_strip    → -O0 -s     (non optimisé, strippé)
#   oop_O2          → -O2 -g     (optimisé, symboles complets)
#   oop_O2_strip    → -O2 -s     (optimisé, strippé)
#   oop_O3          → -O3 -g     (optimisation agressive, symboles)
#   oop_O3_strip    → -O3 -s     (optimisation agressive, strippé)
#
# Usage :
#   make all        → compile toutes les variantes
#   make clean      → supprime tous les binaires
#   make oop_O0     → compile uniquement la variante -O0 -g
#
# ============================================================================

CXX      := g++
CXXSTD   := -std=c++17
WARNINGS := -Wall -Wextra -Wpedantic
SRC      := oop.cpp
LDFLAGS  := -lm

# Flags communs à toutes les variantes
# -rdynamic : exporte les symboles dynamiques (utile pour RTTI et dlopen)
# On n'utilise PAS -fno-rtti pour garder la RTTI activée (exercice chapitre 17.3)
COMMON   := $(CXXSTD) $(WARNINGS) -rdynamic

# ---- Préfixe des binaires ---------------------------------------------------
# Les binaires produits sont préfixés par le numéro du chapitre.
PREFIX := ch17-oop

# ---- Cibles ----------------------------------------------------------------

.PHONY: all clean info

all: $(PREFIX)_O0 $(PREFIX)_O0_strip $(PREFIX)_O2 $(PREFIX)_O2_strip $(PREFIX)_O3 $(PREFIX)_O3_strip

# --- Variantes non optimisées (-O0) ---

$(PREFIX)_O0: $(SRC)
	$(CXX) $(COMMON) -O0 -g -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (non optimisé, symboles complets)"

$(PREFIX)_O0_strip: $(SRC)
	$(CXX) $(COMMON) -O0 -s -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (non optimisé, strippé)"

# --- Variantes optimisées (-O2) ---

$(PREFIX)_O2: $(SRC)
	$(CXX) $(COMMON) -O2 -g -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (optimisé -O2, symboles complets)"

$(PREFIX)_O2_strip: $(SRC)
	$(CXX) $(COMMON) -O2 -s -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (optimisé -O2, strippé)"

# --- Variantes très optimisées (-O3) ---

$(PREFIX)_O3: $(SRC)
	$(CXX) $(COMMON) -O3 -g -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (optimisé -O3, symboles complets)"

$(PREFIX)_O3_strip: $(SRC)
	$(CXX) $(COMMON) -O3 -s -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (optimisé -O3, strippé)"

# ---- Nettoyage --------------------------------------------------------------

clean:
	rm -f $(PREFIX)_O0 $(PREFIX)_O0_strip $(PREFIX)_O2 $(PREFIX)_O2_strip $(PREFIX)_O3 $(PREFIX)_O3_strip
	@echo "[CLEAN] Tous les binaires supprimés."

# ---- Infos utiles pour le RE -------------------------------------------------

BINS_ALL   := $(PREFIX)_O0 $(PREFIX)_O0_strip $(PREFIX)_O2 $(PREFIX)_O2_strip $(PREFIX)_O3 $(PREFIX)_O3_strip
BINS_SYM   := $(PREFIX)_O0 $(PREFIX)_O2 $(PREFIX)_O3
BINS_STRIP := $(PREFIX)_O0_strip $(PREFIX)_O2_strip $(PREFIX)_O3_strip

info: all
	@echo ""
	@echo "====== Comparaison des tailles ======"
	@ls -lh $(BINS_ALL)
	@echo ""
	@echo "====== Sections .text (taille du code) ======"
	@for bin in $(BINS_ALL); do \
		printf "%-24s " "$bin:"; \
		size --format=SysV "$bin" 2>/dev/null | grep '\.text' | awk '{print $2 " bytes"}'; \
	done
	@echo ""
	@echo "====== Nombre de symboles ======"
	@for bin in $(BINS_SYM); do \
		printf "%-24s " "$bin:"; \
		nm "$bin" 2>/dev/null | wc -l; \
	done
	@for bin in $(BINS_STRIP); do \
		printf "%-24s " "$bin:"; \
		echo "(strippé — nm ne trouve rien ou presque)"; \
	done
	@echo ""
	@echo "====== Protections (checksec) ======"
	@if command -v checksec > /dev/null 2>&1; then \
		for bin in $(PREFIX)_O0 $(PREFIX)_O2_strip; do \
			echo "--- $bin ---"; \
			checksec --file="$bin" 2>/dev/null || checksec "$bin" 2>/dev/null; \
		done; \
	else \
		echo "checksec non installé — voir chapitre 4"; \
	fi
