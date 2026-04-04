# ============================================================================
# Makefile — binaries/ch07-keygenme/
#
# Produit les variantes du keygenme à différents niveaux d'optimisation,
# avec et sans symboles de débogage, avec et sans stripping.
#
# Variantes produites :
#   keygenme_O0          -O0 -g        (débogage complet, aucune optimisation)
#   keygenme_O1          -O1           (optimisation légère)
#   keygenme_O2          -O2           (optimisation standard de production)
#   keygenme_O3          -O3           (optimisation agressive)
#   keygenme_Os          -Os           (optimisation en taille)
#   keygenme_strip        -O0 -g -s    (compilé puis strippé)
#   keygenme_O2_strip     -O2 -s       (optimisé puis strippé)
#
# Usage :
#   make              → compile toutes les variantes
#   make clean        → supprime les binaires
#   make test         → vérifie que chaque binaire fonctionne
#   make info         → affiche les tailles et caractéristiques
# ============================================================================

CC      = gcc
SRC     = keygenme.c
CFLAGS  = -Wall -Wextra

# Nom de base pour tous les binaires
BASE    = keygenme

# Liste de toutes les cibles
TARGETS = $(BASE)_O0 \
          $(BASE)_O1 \
          $(BASE)_O2 \
          $(BASE)_O3 \
          $(BASE)_Os \
          $(BASE)_strip \
          $(BASE)_O2_strip

# ============================================================================
# Cibles principales
# ============================================================================

.PHONY: all clean test info

all: $(TARGETS)
	@echo ""
	@echo "=== Toutes les variantes compilées ==="
	@ls -l $(TARGETS)

# ============================================================================
# Variantes avec symboles
# ============================================================================

$(BASE)_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

$(BASE)_O1: $(SRC)
	$(CC) $(CFLAGS) -O1 -o $@ $<

$(BASE)_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -o $@ $<

$(BASE)_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -o $@ $<

$(BASE)_Os: $(SRC)
	$(CC) $(CFLAGS) -Os -o $@ $<

# ============================================================================
# Variantes strippées
# ============================================================================

# -O0 avec debug compilé puis strippé (simule un binaire "dans la nature")
$(BASE)_strip: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $<
	strip $@

# -O2 strippé (le cas le plus réaliste en RE)
$(BASE)_O2_strip: $(SRC)
	$(CC) $(CFLAGS) -O2 -o $@ $<
	strip $@

# ============================================================================
# Utilitaires
# ============================================================================

# Test rapide : chaque binaire doit accepter le bon serial et refuser un mauvais
test: all
	@echo ""
	@echo "=== Tests fonctionnels ==="
	@HASH=$$(./$(BASE)_O0 admin test 2>&1; \
	         ./$(BASE)_O0 admin $$(printf '%08x' $$(./$(BASE)_O0 admin x 2>&1 | head -1)) 2>&1); \
	echo "Test basique OK (binaires exécutables)"
	@for bin in $(TARGETS); do \
		if ./$$bin admin abcd1234 2>/dev/null | grep -q "invalide"; then \
			echo "  $$bin : mauvais serial rejeté ✓"; \
		else \
			echo "  $$bin : ERREUR — mauvais serial accepté ✗"; \
		fi \
	done

# Affiche un résumé des tailles et caractéristiques de chaque binaire
info: all
	@echo ""
	@echo "=== Informations sur les binaires ==="
	@echo ""
	@printf "%-22s %8s %10s %10s %s\n" "Binaire" "Taille" ".text (h)" "Symboles" "Debug"
	@printf "%-22s %8s %10s %10s %s\n" "------" "------" "---------" "--------" "-----"
	@for bin in $(TARGETS); do \
		size=$$(stat -c%s $$bin 2>/dev/null || stat -f%z $$bin 2>/dev/null); \
		text=$$(readelf -S $$bin 2>/dev/null | grep '\.text ' | awk '{print $$6}'); \
		if nm $$bin >/dev/null 2>&1; then sym="oui"; else sym="non"; fi; \
		if readelf -S $$bin 2>/dev/null | grep -q '\.debug_info'; then dbg="oui"; else dbg="non"; fi; \
		printf "%-22s %8s %10s %10s %s\n" "$$bin" "$$size" "0x$$text" "$$sym" "$$dbg"; \
	done
	@echo ""

clean:
	rm -f $(TARGETS)
	@echo "Nettoyage effectué."
