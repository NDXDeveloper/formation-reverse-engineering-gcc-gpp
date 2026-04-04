# ═══════════════════════════════════════════════════════════════
#  ch20-oop — Makefile
#  Formation Reverse Engineering — Chapitre 13
#
# Produit 6 variantes du binaire :
#   oop_O0        — sans optimisation, sans debug
#   oop_O0_dbg    — sans optimisation, avec symboles DWARF
#   oop_O2        — optimisé -O2, avec symboles
#   oop_O2_strip  — optimisé -O2, strippé
#   oop_O3        — optimisé -O3, avec symboles
#   oop_O3_strip  — optimisé -O3, strippé
#
# Usage :
#   make            — compile toutes les variantes
#   make clean      — supprime les binaires

# ═══════════════════════════════════════════════════════════════

CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17
LDFLAGS  = -ldl
SRC      = oop.cpp
BIN_DIR  = .

TARGETS = oop_O0        \
          oop_O0_dbg     \
          oop_O2         \
          oop_O2_strip   \
          oop_O3         \
          oop_O3_strip

.PHONY: all clean

all: $(TARGETS)

oop_O0: $(SRC)
	$(CXX) $(CXXFLAGS) -O0 -o $(BIN_DIR)/$@ $< $(LDFLAGS)

oop_O0_dbg: $(SRC)
	$(CXX) $(CXXFLAGS) -O0 -g -o $(BIN_DIR)/$@ $< $(LDFLAGS)

oop_O2: $(SRC)
	$(CXX) $(CXXFLAGS) -O2 -o $(BIN_DIR)/$@ $< $(LDFLAGS)

oop_O2_strip: $(SRC)
	$(CXX) $(CXXFLAGS) -O2 -s -o $(BIN_DIR)/$@ $< $(LDFLAGS)

oop_O3: $(SRC)
	$(CXX) $(CXXFLAGS) -O3 -o $(BIN_DIR)/$@ $< $(LDFLAGS)

oop_O3_strip: $(SRC)
	$(CXX) $(CXXFLAGS) -O3 -s -o $(BIN_DIR)/$@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)
