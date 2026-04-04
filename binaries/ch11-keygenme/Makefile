# ══════════════════════════════════════════════════════════════════════
# binaries/ch11-keygenme/Makefile
# ══════════════════════════════════════════════════════════════════════
#
# Produit le binaire keygenme à différents niveaux d'optimisation,
# avec et sans symboles de débogage, avec et sans stripping.
#
# Ces variantes servent dès le chapitre 11 (GDB) et tout au long
# de la Partie V (cas pratiques).
#
# Variantes produites :
#
#   keygenme_O0            -O0 -g       Debug complet, aucune optimisation
#   keygenme_O0_strip      -O0 strippé  Sans symboles, aucune optimisation
#   keygenme_O2            -O2 -g       Optimisé avec symboles
#   keygenme_O2_strip      -O2 strippé  Optimisé sans symboles (cas réaliste)
#   keygenme_O3            -O3 -g       Optimisation agressive avec symboles
#   keygenme_O3_strip      -O3 strippé  Optimisation agressive sans symboles
#   keygenme_pie           -O2 -g -pie  PIE avec symboles
#   keygenme_pie_strip     -O2 -pie     PIE strippé (ASLR + pas de symboles)
#
# Usage :
#   make              Compile toutes les variantes
#   make clean        Supprime tous les binaires générés
#   make keygenme_O0  Compile une variante spécifique
#
# ══════════════════════════════════════════════════════════════════════

CC      = gcc
SRC     = keygenme.c
CFLAGS  = -Wall -Wextra

# ── Liste de toutes les cibles ──────────────────────────────────────

TARGETS = keygenme_O0       \
          keygenme_O0_strip  \
          keygenme_O2        \
          keygenme_O2_strip  \
          keygenme_O3        \
          keygenme_O3_strip  \
          keygenme_pie       \
          keygenme_pie_strip

.PHONY: all clean

all: $(TARGETS)
	@echo ""
	@echo "══════════════════════════════════════════════════════════"
	@echo "  Toutes les variantes de keygenme ont été compilées."
	@echo "══════════════════════════════════════════════════════════"
	@echo ""
	@ls -lh keygenme_* 2>/dev/null | awk '{printf "  %-24s %s\n", $$NF, $$5}'
	@echo ""

# ── Debug complet (-O0 -g) ──────────────────────────────────────────
# Aucune optimisation, symboles DWARF complets.
# Utilisé pour l'apprentissage de GDB (sections 11.1 à 11.3).

keygenme_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -no-pie -o $@ $<

# ── Sans optimisation, strippé ──────────────────────────────────────
# Code non optimisé mais sans symboles.
# Prologue/épilogue standard, mais pas de noms de fonctions.

keygenme_O0_strip: $(SRC)
	$(CC) $(CFLAGS) -O0 -no-pie -o $@ $<
	strip $@

# ── Optimisé avec symboles (-O2 -g) ────────────────────────────────
# Code optimisé avec symboles DWARF.
# Variables "optimized out", instructions réordonnées (section 11.1).

keygenme_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -no-pie -o $@ $<

# ── Optimisé et strippé (-O2, strip) ───────────────────────────────
# Le cas le plus réaliste : optimisé, sans aucun symbole.
# C'est la cible principale pour les techniques de la section 11.4.

keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS) -O2 -no-pie -o $@ $<
	strip $@

# ── Optimisation agressive avec symboles (-O3 -g) ──────────────────
# Inlining, vectorisation, déroulage de boucles (chapitre 16).

keygenme_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -g -no-pie -o $@ $<

# ── Optimisation agressive, strippé ─────────────────────────────────

keygenme_O3_strip: $(SRC)
	$(CC) $(CFLAGS) -O3 -no-pie -o $@ $<
	strip $@

# ── PIE avec symboles (-O2 -g -pie) ────────────────────────────────
# Position-Independent Executable. Adresses relatives.
# Utilisé pour le workflow PIE + ASLR (section 11.4).

keygenme_pie: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -pie -o $@ $<

# ── PIE strippé (-O2 -pie, strip) ──────────────────────────────────
# Le cas le plus difficile : PIE + ASLR + pas de symboles.
# Nécessite starti + calcul de base (section 11.4).

keygenme_pie_strip: $(SRC)
	$(CC) $(CFLAGS) -O2 -pie -o $@ $<
	strip $@

# ── Nettoyage ───────────────────────────────────────────────────────

clean:
	rm -f $(TARGETS)
	@echo "Nettoyage terminé."
