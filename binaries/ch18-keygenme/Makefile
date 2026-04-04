# ============================================================================
# Makefile — keygenme (Chapitre 18 )
#
# Produit plusieurs variantes du même binaire pour illustrer l'impact
# des niveaux d'optimisation et du stripping sur le reverse engineering.
#
# Usage :
#   make            ← Compile toutes les variantes
#   make clean      ← Supprime les binaires générés
#   make keygenme_O0       ← Compile uniquement la variante -O0
#
# Licence MIT — Usage strictement éducatif.
# ============================================================================

CC      := gcc
SRC     := keygenme.c
CFLAGS  := -Wall -Wextra

# --------------------------------------------------------------------------
# Cibles
# --------------------------------------------------------------------------

# Variantes principales
TARGETS := keygenme_O0        \
           keygenme_O0_strip   \
           keygenme_O2        \
           keygenme_O2_strip   \
           keygenme_O3        \
           keygenme_O3_strip

.PHONY: all clean info

all: $(TARGETS)
	@echo ""
	@echo "  ✅  Toutes les variantes compilées avec succès."
	@echo "  Lancez 'make info' pour voir les détails."
	@echo ""

# --------------------------------------------------------------------------
# Avec symboles de debug (-g) — idéal pour apprendre
# --------------------------------------------------------------------------

keygenme_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

keygenme_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -o $@ $<

keygenme_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -g -o $@ $<

# --------------------------------------------------------------------------
# Sans symboles (strip) — conditions réalistes de RE
# --------------------------------------------------------------------------

keygenme_O0_strip: $(SRC)
	$(CC) $(CFLAGS) -O0 -s -o $@ $<

keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS) -O2 -s -o $@ $<

keygenme_O3_strip: $(SRC)
	$(CC) $(CFLAGS) -O3 -s -o $@ $<

# --------------------------------------------------------------------------
# Nettoyage
# --------------------------------------------------------------------------

clean:
	rm -f $(TARGETS)
	@echo "  🧹  Binaires supprimés."

# --------------------------------------------------------------------------
# Informations sur les binaires générés
# --------------------------------------------------------------------------

info: all
	@echo ""
	@echo "  ╔══════════════════════════════════════════════════════════════╗"
	@echo "  ║           keygenme — Variantes compilées                     ║"
	@echo "  ╠══════════════════════════════════════════════════════════════╣"
	@echo "  ║  Fichier             Optim   Symboles   Usage principal      ║"
	@echo "  ╠══════════════════════════════════════════════════════════════╣"
	@echo "  ║  keygenme_O0         -O0     oui        Apprentissage        ║"
	@echo "  ║  keygenme_O0_strip   -O0     non        RE sans symboles     ║"
	@echo "  ║  keygenme_O2         -O2     oui        Optimisations        ║"
	@echo "  ║  keygenme_O2_strip   -O2     non        Checkpoint ch.18     ║"
	@echo "  ║  keygenme_O3         -O3     oui        Vectorisation        ║"
	@echo "  ║  keygenme_O3_strip   -O3     non        Défi avancé          ║"
	@echo "  ╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "  Tailles :"
	@ls -lh $(TARGETS) | awk '{printf "    %-20s %s\n", $$NF, $$5}'
	@echo ""
