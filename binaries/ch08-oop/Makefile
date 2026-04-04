# ═══════════════════════════════════════════════════════════════
#  ch08-oop — Makefile
#  Formation Reverse Engineering — Chapitre 8
#
# Makefile produisant les variantes du binaire ch08-oop à différents niveaux
# d'optimisation, avec et sans symboles.
#
# Usage :
#   make all       — compile toutes les variantes
#   make clean     — supprime les binaires générés
#   make check     — compile et exécute la variante O0 (test rapide)
#
# Variantes produites :
#   ch08-oop_O0           -O0 -g     (debug, symboles DWARF, pas d'optimisation)
#   ch08-oop_O0_strip     -O0        (sans symboles, sans optimisation)
#   ch08-oop_O2           -O2 -g     (optimisé, avec symboles)
#   ch08-oop_O2_strip     -O2        (optimisé, sans symboles — cas réaliste)
#   ch08-oop_O3           -O3 -g     (optimisation agressive, avec symboles)

# ═══════════════════════════════════════════════════════════════

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra
SRC      := oop.cpp
STRIP    := strip

# Nom de base des binaires
BIN_O0       := ch08-oop_O0
BIN_O0_STRIP := ch08-oop_O0_strip
BIN_O2       := ch08-oop_O2
BIN_O2_STRIP := ch08-oop_O2_strip
BIN_O3       := ch08-oop_O3

ALL_BINS := $(BIN_O0) $(BIN_O0_STRIP) $(BIN_O2) $(BIN_O2_STRIP) $(BIN_O3)

# ============================================================================
# Cibles principales
# ============================================================================

.PHONY: all clean check

all: $(ALL_BINS)
	@echo ""
	@echo "=== Toutes les variantes compilées ==="
	@ls -lh $(ALL_BINS)
	@echo ""
	@echo "Comparaison des tailles :"
	@for bin in $(ALL_BINS); do \
		printf "  %-25s %s\n" "$$bin" "$$(stat --printf='%s octets' $$bin 2>/dev/null || stat -f '%z octets' $$bin 2>/dev/null)"; \
	done

clean:
	rm -f $(ALL_BINS)

check: $(BIN_O0)
	@echo "=== Exécution de $(BIN_O0) ==="
	./$(BIN_O0)

# ============================================================================
# Variantes de compilation
# ============================================================================

# -O0 -g : aucune optimisation, symboles DWARF complets.
# C'est la variante la plus facile à analyser dans Ghidra.
# Le code assembleur suit fidèlement la structure du source C++.
$(BIN_O0): $(SRC)
	$(CXX) $(CXXFLAGS) -O0 -g -o $@ $<

# -O0 strippé : même code que O0, mais sans la table .symtab ni DWARF.
# Les noms de fonctions locales disparaissent. Les imports dynamiques
# et le RTTI restent présents (le RTTI est nécessaire au runtime C++).
$(BIN_O0_STRIP): $(SRC)
	$(CXX) $(CXXFLAGS) -O0 -o $@ $<
	$(STRIP) $@

# -O2 -g : optimisations standard activées, symboles conservés.
# Le code est réorganisé : fonctions courtes inlinées, boucles déroulées,
# variables en registres. Utile pour observer l'impact des optimisations
# tout en gardant les noms de fonctions comme repère.
$(BIN_O2): $(SRC)
	$(CXX) $(CXXFLAGS) -O2 -g -o $@ $<

# -O2 strippé : c'est le cas le plus réaliste en conditions de RE.
# Les binaires distribués (release builds) sont typiquement compilés
# avec -O2 et strippés. C'est la variante la plus difficile à analyser.
$(BIN_O2_STRIP): $(SRC)
	$(CXX) $(CXXFLAGS) -O2 -o $@ $<
	$(STRIP) $@

# -O3 -g : optimisation agressive. Peut produire de la vectorisation SIMD,
# du déroulage de boucles agressif et de l'inlining très étendu.
# Les symboles sont conservés pour faciliter la comparaison avec -O0 et -O2.
$(BIN_O3): $(SRC)
	$(CXX) $(CXXFLAGS) -O3 -g -o $@ $<
