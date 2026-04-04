# ============================================================================
# Makefile — keygenme (Formation Reverse Engineering)
#
# Produit le même binaire à différents niveaux d'optimisation,
# avec et sans symboles de débogage, pour illustrer l'impact
# des flags de compilation sur le reverse engineering.
#
# Variantes produites :
#   keygenme_O0          -O0 -g   (pas d'optim, symboles DWARF)
#   keygenme_O0_strip    -O0      (pas d'optim, strippé)
#   keygenme_O2          -O2 -g   (optim standard, symboles)
#   keygenme_O2_strip    -O2      (optim standard, strippé)
#   keygenme_O3          -O3 -g   (optim agressive, symboles)
#   keygenme_O3_strip    -O3      (optim agressive, strippé)
#
# Usage :
#   make            — compile toutes les variantes
#   make clean      — supprime les binaires
#   make info       — affiche taille et sections de chaque variante
# ============================================================================

CC      := gcc
SRC     := keygenme.c
CFLAGS  := -Wall -Wextra

# Toutes les variantes à produire
TARGETS := keygenme_O0      keygenme_O0_strip \
           keygenme_O2      keygenme_O2_strip \
           keygenme_O3      keygenme_O3_strip

# ── Cibles principales ──────────────────────────────────────────────────────

.PHONY: all clean info

all: $(TARGETS)

# ── Variantes avec symboles (-g) ────────────────────────────────────────────

keygenme_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

keygenme_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -o $@ $<

keygenme_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -g -o $@ $<

# ── Variantes strippées ─────────────────────────────────────────────────────
# On compile d'abord avec -g puis on strippe, pour pouvoir éventuellement
# garder un fichier .debug séparé (non fait ici, mais possible avec
# objcopy --only-keep-debug).

keygenme_O0_strip: $(SRC)
	$(CC) $(CFLAGS) -O0 -o $@ $<
	strip $@

keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS) -O2 -o $@ $<
	strip $@

keygenme_O3_strip: $(SRC)
	$(CC) $(CFLAGS) -O3 -o $@ $<
	strip $@

# ── Nettoyage ───────────────────────────────────────────────────────────────

clean:
	rm -f $(TARGETS)

# ── Info : comparaison rapide des variantes ─────────────────────────────────
# Affiche la taille de chaque binaire et le nombre de symboles visibles.
# Utile pour le checkpoint du chapitre 2 et pour comparer l'impact des flags.

info: all
	@echo ""
	@echo "══════════════════════════════════════════════════════════════"
	@echo "  Variante               Taille      Symboles (.symtab)"
	@echo "══════════════════════════════════════════════════════════════"
	@for bin in $(TARGETS); do \
		size=$$(stat --format='%s' $$bin 2>/dev/null || stat -f '%z' $$bin 2>/dev/null); \
		syms=$$(nm $$bin 2>/dev/null | wc -l || echo "0"); \
		printf "  %-24s %6s B    %s symboles\n" "$$bin" "$$size" "$$syms"; \
	done
	@echo "══════════════════════════════════════════════════════════════"
	@echo ""
