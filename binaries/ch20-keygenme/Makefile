# ═══════════════════════════════════════════════════════════════
#  ch20-keygenme — Makefile
#  Formation Reverse Engineering — Chapitre 20
#
# Produit 6 variantes du binaire :
#   keygenme_O0        — sans optimisation, sans debug
#   keygenme_O0_dbg    — sans optimisation, avec symboles DWARF
#   keygenme_O2        — optimisé -O2, avec symboles
#   keygenme_O2_strip  — optimisé -O2, strippé, non-PIE
#   keygenme_O3        — optimisé -O3, avec symboles
#   keygenme_O3_strip  — optimisé -O3, strippé, non-PIE
#
# Usage :
#   make            — compile toutes les variantes
#   make clean      — supprime les binaires

# ═══════════════════════════════════════════════════════════════

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11
SRC     = keygenme.c
BIN_DIR = .

TARGETS = keygenme_O0        \
          keygenme_O0_dbg     \
          keygenme_O2         \
          keygenme_O2_strip   \
          keygenme_O3         \
          keygenme_O3_strip

.PHONY: all clean

all: $(TARGETS)

# -O0 sans symboles de debug
keygenme_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -o $(BIN_DIR)/$@ $<

# -O0 avec symboles DWARF (debug)
keygenme_O0_dbg: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $(BIN_DIR)/$@ $<

# -O2 avec symboles
keygenme_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -o $(BIN_DIR)/$@ $<

# -O2 strippé (ni symboles ni debug)
keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS) -O2 -s -no-pie -o $(BIN_DIR)/$@ $<

# -O3 avec symboles
keygenme_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -o $(BIN_DIR)/$@ $<

# -O3 strippé
keygenme_O3_strip: $(SRC)
	$(CC) $(CFLAGS) -O3 -s -no-pie -o $(BIN_DIR)/$@ $<

clean:
	rm -f $(TARGETS)
