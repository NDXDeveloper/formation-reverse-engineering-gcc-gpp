# ============================================================================
# Makefile — Chapitre 10 : Diffing de binaires
# Formation Reverse Engineering — Applications compilées avec la chaîne GNU
#
# Produit les binaires keygenme_v1 et keygenme_v2 à différents niveaux
# d'optimisation, avec et sans symboles de débogage, avec et sans stripping.
#
# Usage :
#   make              Compile toutes les variantes
#   make clean        Supprime tous les binaires générés
#   make v1           Compile uniquement les variantes v1
#   make v2           Compile uniquement les variantes v2
#   make diff-quick   Lance un triage rapide avec radiff2
#
# Licence MIT — Usage strictement éducatif et éthique.
# ============================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra

# Variantes de compilation
# -O0 -g   : sans optimisation, avec symboles (idéal pour le débogage/RE)
# -O0      : sans optimisation, sans symboles DWARF
# -O2      : optimisé, avec symboles
# -O2 -s   : optimisé, strippé (cas réaliste d'un binaire de production)
# -O3      : optimisation agressive, avec symboles

# --- Cibles principales ---------------------------------------------------

all: v1 v2

v1: keygenme_v1_O0_dbg \
    keygenme_v1_O0 \
    keygenme_v1_O2_dbg \
    keygenme_v1_O2_strip \
    keygenme_v1_O3_dbg

v2: keygenme_v2_O0_dbg \
    keygenme_v2_O0 \
    keygenme_v2_O2_dbg \
    keygenme_v2_O2_strip \
    keygenme_v2_O3_dbg

# --- Alias de commodité pour le checkpoint du chapitre 10 ------------------
# Les noms "keygenme_v1" et "keygenme_v2" utilisés dans le chapitre
# pointent vers les variantes -O0 avec symboles (les plus lisibles).

keygenme_v1: keygenme_v1_O0_dbg
	cp $< $@

keygenme_v2: keygenme_v2_O0_dbg
	cp $< $@

checkpoint: keygenme_v1 keygenme_v2
	@echo "=== Binaires du checkpoint prêts ==="
	@echo "  keygenme_v1  (v1, -O0, symboles)"
	@echo "  keygenme_v2  (v2, -O0, symboles)"
	@echo ""
	@echo "Lancez votre analyse avec :"
	@echo "  radiff2 -s keygenme_v1 keygenme_v2"

# --- Version 1 (vulnérable) -----------------------------------------------

keygenme_v1_O0_dbg: keygenme_v1.c
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

keygenme_v1_O0: keygenme_v1.c
	$(CC) $(CFLAGS) -O0 -o $@ $<

keygenme_v1_O2_dbg: keygenme_v1.c
	$(CC) $(CFLAGS) -O2 -g -o $@ $<

keygenme_v1_O2_strip: keygenme_v1.c
	$(CC) $(CFLAGS) -O2 -s -o $@ $<

keygenme_v1_O3_dbg: keygenme_v1.c
	$(CC) $(CFLAGS) -O3 -g -o $@ $<

# --- Version 2 (corrigée) -------------------------------------------------

keygenme_v2_O0_dbg: keygenme_v2.c
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

keygenme_v2_O0: keygenme_v2.c
	$(CC) $(CFLAGS) -O0 -o $@ $<

keygenme_v2_O2_dbg: keygenme_v2.c
	$(CC) $(CFLAGS) -O2 -g -o $@ $<

keygenme_v2_O2_strip: keygenme_v2.c
	$(CC) $(CFLAGS) -O2 -s -o $@ $<

keygenme_v2_O3_dbg: keygenme_v2.c
	$(CC) $(CFLAGS) -O3 -g -o $@ $<

# --- Utilitaires -----------------------------------------------------------

# Triage rapide : lance radiff2 sur les variantes checkpoint
diff-quick: keygenme_v1 keygenme_v2
	@echo "=== Triage radiff2 -s ==="
	radiff2 -s keygenme_v1 keygenme_v2
	@echo ""
	@echo "=== Fonctions modifiées (radiff2 -AC) ==="
	radiff2 -AC keygenme_v1 keygenme_v2

# Compare les variantes strippées -O2 (scénario réaliste)
diff-stripped: keygenme_v1_O2_strip keygenme_v2_O2_strip
	@echo "=== Triage radiff2 -s (strippé, -O2) ==="
	radiff2 -s keygenme_v1_O2_strip keygenme_v2_O2_strip
	@echo ""
	@echo "=== Fonctions modifiées ==="
	radiff2 -AC keygenme_v1_O2_strip keygenme_v2_O2_strip

# Affiche les tailles de tous les binaires générés
sizes: all
	@echo "=== Tailles des binaires ==="
	@ls -lhS keygenme_v* 2>/dev/null | awk '{print $$5, $$9}'

clean:
	rm -f keygenme_v1 keygenme_v2
	rm -f keygenme_v1_O0_dbg keygenme_v1_O0
	rm -f keygenme_v1_O2_dbg keygenme_v1_O2_strip
	rm -f keygenme_v1_O3_dbg
	rm -f keygenme_v2_O0_dbg keygenme_v2_O0
	rm -f keygenme_v2_O2_dbg keygenme_v2_O2_strip
	rm -f keygenme_v2_O3_dbg

.PHONY: all v1 v2 checkpoint clean diff-quick diff-stripped sizes
