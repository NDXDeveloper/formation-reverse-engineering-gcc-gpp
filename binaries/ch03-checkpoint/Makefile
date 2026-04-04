# ═══════════════════════════════════════════════════════════════
#  ch03-checkpoint — Makefile
#  Formation Reverse Engineering — Chapitre 3
#
# Makefile pour compiler count_lowercase.c à différents niveaux
# d'optimisation, avec et sans symboles.
#
# Cibles produites :
#   count_lowercase_O0          -O0 -g  (debug, avec frame pointer)
#   count_lowercase_O2          -O2 -g  (optimisé, frame pointer omis)
#   count_lowercase_O0_strip    -O0     (strippé, comme dans le checkpoint)
#
# Usage :
#   make all       Compile toutes les variantes
#   make clean     Supprime les binaires générés
#   make disass    Affiche le désassemblage de count_lowercase (variante -O0 strippée)
#   make compare   Affiche côte à côte le désassemblage -O0 vs -O2

# ═══════════════════════════════════════════════════════════════

CC      = gcc
SRC     = count_lowercase.c
NAME    = count_lowercase

# Flags communs
CFLAGS_COMMON = -Wall -Wextra -no-pie

# Variantes
BIN_O0       = $(NAME)_O0
BIN_O2       = $(NAME)_O2
BIN_O0_STRIP = $(NAME)_O0_strip

ALL_BINS = $(BIN_O0) $(BIN_O2) $(BIN_O0_STRIP)

.PHONY: all clean disass compare

all: $(ALL_BINS)

# -O0 avec symboles de débogage (pour étudier avec GDB)
$(BIN_O0): $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -g -o $@ $<

# -O2 avec symboles (pour comparer les optimisations)
$(BIN_O2): $(SRC)
	$(CC) $(CFLAGS_COMMON) -O2 -g -o $@ $<

# -O0 strippé (version du checkpoint — pas de symboles)
$(BIN_O0_STRIP): $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -o $@ $<
	strip $@

# Désassemble la fonction count_lowercase depuis la variante -O0 (avec symboles)
# puis affiche aussi la version strippée pour comparaison
disass: $(BIN_O0) $(BIN_O0_STRIP)
	@echo "═══════════════════════════════════════════════════"
	@echo " Désassemblage -O0 (avec symboles)"
	@echo "═══════════════════════════════════════════════════"
	objdump -d -M intel $(BIN_O0) | sed -n '/<count_lowercase>:/,/^$$/p'
	@echo ""
	@echo "═══════════════════════════════════════════════════"
	@echo " Désassemblage -O0 strippé (version checkpoint)"
	@echo "═══════════════════════════════════════════════════"
	@echo "(Utilisez objdump -d -M intel $(BIN_O0_STRIP) et cherchez"
	@echo " la fonction manuellement par son prologue push rbp)"

# Compare le désassemblage -O0 vs -O2 de count_lowercase
compare: $(BIN_O0) $(BIN_O2)
	@echo "═══════════════════════════════════════════════════"
	@echo " count_lowercase — GCC -O0"
	@echo "═══════════════════════════════════════════════════"
	objdump -d -M intel $(BIN_O0) | sed -n '/<count_lowercase>:/,/^$$/p'
	@echo ""
	@echo "═══════════════════════════════════════════════════"
	@echo " count_lowercase — GCC -O2"
	@echo "═══════════════════════════════════════════════════"
	objdump -d -M intel $(BIN_O2) | sed -n '/<count_lowercase>:/,/^$$/p'

clean:
	rm -f $(ALL_BINS)
