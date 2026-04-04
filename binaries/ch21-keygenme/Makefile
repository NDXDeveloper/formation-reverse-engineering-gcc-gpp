# ═══════════════════════════════════════════════════════════════
#  ch21-keygenme — Makefile
#  Formation Reverse Engineering — Chapitre 21
#
# Makefile — Chapitre 21 : keygenme (5 variantes)
#
# Produit :
#   keygenme_O0         -O0 -g          (debug, non optimisé)
#   keygenme_O2         -O2 -g          (optimisé, avec symboles)
#   keygenme_O3         -O3 -g          (optimisé agressif, avec symboles)
#   keygenme_strip      -O0    strippé  (non optimisé, sans symboles)
#   keygenme_O2_strip   -O2    strippé  (optimisé, sans symboles)
#
# Usage :
#   make              → compile toutes les variantes
#   make clean        → supprime les binaires
#   make keygenme_O0  → compile une seule variante

# ═══════════════════════════════════════════════════════════════

CC      := gcc
SRC     := keygenme.c

# Flags communs : warnings stricts, standard C11
CFLAGS_COMMON := -Wall -Wextra -Wpedantic -std=c11

# ── Cibles ──────────────────────────────────────────────────

ALL := keygenme_O0 keygenme_O2 keygenme_O3 keygenme_strip keygenme_O2_strip

.PHONY: all clean info

all: $(ALL)
	@echo ""
	@echo "✅ 5 variantes compilées avec succès :"
	@ls -lh $(ALL)
	@echo ""

# ── Variante 1 : -O0 avec symboles (difficulté ⭐) ─────────

keygenme_O0: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -g -o $@ $<
	@echo "  [OK] $@ (-O0 -g)"

# ── Variante 2 : -O2 avec symboles (difficulté ⭐⭐) ───────

keygenme_O2: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O2 -g -o $@ $<
	@echo "  [OK] $@ (-O2 -g)"

# ── Variante 3 : -O3 avec symboles (difficulté ⭐⭐⭐) ─────

keygenme_O3: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O3 -g -o $@ $<
	@echo "  [OK] $@ (-O3 -g)"

# ── Variante 4 : -O0 strippé (difficulté ⭐⭐) ─────────────

keygenme_strip: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -o $@ $<
	strip --strip-all $@
	@echo "  [OK] $@ (-O0, stripped)"

# ── Variante 5 : -O2 strippé (difficulté ⭐⭐⭐⭐) ─────────

keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O2 -o $@ $<
	strip --strip-all $@
	@echo "  [OK] $@ (-O2, stripped)"

# ── Nettoyage ───────────────────────────────────────────────

clean:
	rm -f $(ALL)
	@echo "🧹 Binaires supprimés."

# ── Info : affiche les tailles et sections pour comparaison ─

info: all
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════╗"
	@echo "║          Comparaison des 5 variantes                    ║"
	@echo "╚══════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "── Tailles ──────────────────────────────────────────────"
	@for bin in $(ALL); do \
		printf "  %-22s %s\n" "$$bin" "$$(stat --format='%s octets' $$bin 2>/dev/null || stat -f '%z octets' $$bin 2>/dev/null)"; \
	done
	@echo ""
	@echo "── Sections .text ──────────────────────────────────────"
	@for bin in $(ALL); do \
		printf "  %-22s " "$$bin"; \
		readelf -S $$bin 2>/dev/null | grep '\.text' | awk '{print "taille:", $$6, "octets (hex)"}'; \
	done
	@echo ""
	@echo "── Symboles (nm) ───────────────────────────────────────"
	@for bin in $(ALL); do \
		count=$$(nm $$bin 2>/dev/null | wc -l); \
		printf "  %-22s %s symboles\n" "$$bin" "$$count"; \
	done
	@echo ""
	@echo "── file(1) ─────────────────────────────────────────────"
	@for bin in $(ALL); do \
		printf "  %-22s " "$$bin"; \
		file $$bin | cut -d: -f2; \
	done
	@echo ""
