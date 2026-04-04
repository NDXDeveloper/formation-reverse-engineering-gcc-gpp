# ============================================================================
# Makefile — binaries/ch12-keygenme/
#
# Produit le crackme keygenme à différents niveaux d'optimisation,
# avec et sans symboles de débogage, pour servir de support au
# chapitres 12 de la formation Reverse Engineering.
#
# Variantes produites :
#   keygenme_O0          -O0 -g   (sans optim, avec symboles DWARF)
#   keygenme_O2          -O2 -g   (optimisé, avec symboles)
#   keygenme_O3          -O3 -g   (optimisé agressif, avec symboles)
#   keygenme_strip       -O0 -s   (sans optim, strippé)
#   keygenme_O2_strip    -O2 -s   (optimisé, strippé)
#
# Usage :
#   make                 — compile toutes les variantes
#   make keygenme_O0     — compile uniquement la variante -O0
#   make clean           — supprime tous les binaires
#   make info            — affiche file + checksec sur chaque variante
# ============================================================================

CC       := gcc
SRC      := keygenme.c
CFLAGS   := -Wall -Wextra

# Flags communs de sécurité (reproduisent le comportement par défaut de GCC
# sur les distributions récentes — utiles pour les chapitres sur checksec)
SECFLAGS := -fstack-protector-strong -pie -Wl,-z,relro,-z,now

# Liste de toutes les cibles
TARGETS  := keygenme_O0 \
            keygenme_O2 \
            keygenme_O3 \
            keygenme_strip \
            keygenme_O2_strip

# ──────────────────────────────────────────────
# Cible par défaut : tout compiler
# ──────────────────────────────────────────────
.PHONY: all clean info

all: $(TARGETS)

# ──────────────────────────────────────────────
# Variantes avec symboles de débogage (-g)
# ──────────────────────────────────────────────

# -O0 : aucune optimisation — le code assembleur correspond presque
# ligne à ligne au code C. Idéal pour les premiers chapitres (7, 12).
keygenme_O0: $(SRC)
	$(CC) $(CFLAGS) $(SECFLAGS) -O0 -g -o $@ $<
	@echo "[+] $@ compilé (-O0, symboles DWARF)"

# -O2 : optimisation standard — inlining, réorganisation de boucles,
# élimination de code mort. Le code assembleur diverge sensiblement
# du source C. Utilisé aux chapitres 7, 16 et 18.
keygenme_O2: $(SRC)
	$(CC) $(CFLAGS) $(SECFLAGS) -O2 -g -o $@ $<
	@echo "[+] $@ compilé (-O2, symboles DWARF)"

# -O3 : optimisation agressive — vectorisation, déroulage de boucles,
# inlining agressif. Le désassemblage peut être très différent du source.
keygenme_O3: $(SRC)
	$(CC) $(CFLAGS) $(SECFLAGS) -O3 -g -o $@ $<
	@echo "[+] $@ compilé (-O3, symboles DWARF)"

# ──────────────────────────────────────────────
# Variantes strippées (-s)
# ──────────────────────────────────────────────

# -O0 strippé : pas d'optimisation mais aucun symbole.
# Force l'analyste à retrouver main et check_password
# sans l'aide des noms de fonctions. Chapitre 11 (section 11.4).
keygenme_strip: $(SRC)
	$(CC) $(CFLAGS) $(SECFLAGS) -O0 -s -o $@ $<
	@echo "[+] $@ compilé (-O0, strippé)"

# -O2 strippé : optimisé ET sans symboles — la variante la plus
# difficile à reverser. Cible du checkpoint du chapitre 18
# (résolution avec angr en < 30 lignes Python).
keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS) $(SECFLAGS) -O2 -s -o $@ $<
	@echo "[+] $@ compilé (-O2, strippé)"

# ──────────────────────────────────────────────
# Nettoyage
# ──────────────────────────────────────────────
clean:
	rm -f $(TARGETS)
	@echo "[+] Binaires supprimés"

# ──────────────────────────────────────────────
# Info : afficher les métadonnées de chaque variante
# Utile pour vérifier les protections après compilation
# ──────────────────────────────────────────────
info: all
	@echo ""
	@echo "════════════════════════════════════════════"
	@echo " Métadonnées des variantes keygenme"
	@echo "════════════════════════════════════════════"
	@for bin in $(TARGETS); do \
		echo ""; \
		echo "── $$bin ──"; \
		file $$bin; \
		size $$bin; \
		if command -v checksec >/dev/null 2>&1; then \
			checksec --file=$$bin 2>/dev/null || true; \
		fi; \
		echo ""; \
	done
