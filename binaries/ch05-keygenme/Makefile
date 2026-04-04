# ═══════════════════════════════════════════════════════════════
#  ch05-keygenme — Makefile
#  Formation Reverse Engineering — Chapitre 5
#
# Formation Reverse Engineering — Chapitre 5
# Compile keygenme.c en plusieurs variantes pour couvrir les différents
# scénarios d'analyse du tutoriel.
#
# Usage :
#   make              → compile toutes les variantes
#   make keygenme_O0  → compile une variante spécifique
#   make clean        → supprime les binaires compilés
#   make verify       → affiche les propriétés de chaque variante
#
# Variantes produites :
#   keygenme_O0           -O0 -g        (aucune optimisation, symboles debug)
#   keygenme_O1           -O1 -g        (optimisation légère, symboles debug)
#   keygenme_O2           -O2 -g        (optimisation standard, symboles debug)
#   keygenme_O3           -O3 -g        (optimisation agressive, symboles debug)
#   keygenme_Os           -Os -g        (optimisation taille, symboles debug)
#   keygenme_strip        -O0           (sans optimisation, strippé)
#   keygenme_O2_strip     -O2           (optimisé + strippé = le plus réaliste)
#   keygenme_O3_strip     -O3           (optimisation agressive + strippé)
#   keygenme_nopie        -O0 -g -no-pie (sans PIE, pour comparaison checksec)
#   keygenme_nocanary     -O0 -g -fno-stack-protector (sans canary)

# ═══════════════════════════════════════════════════════════════

CC      = gcc
SRC     = keygenme.c

# Flags communs à toutes les variantes
CFLAGS_COMMON = -Wall -Wextra

# Flags de sécurité (Full RELRO + canary + PIE)
SECURITY_FLAGS = -fstack-protector-strong -pie -Wl,-z,relro,-z,now

# ──────────────────────────────────────────────
# Variantes avec symboles (non strippées)
# Utilisées pour l'apprentissage (chapitre 5)
# ──────────────────────────────────────────────

keygenme_O0: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -g $(SECURITY_FLAGS) -o $@ $<

keygenme_O1: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O1 -g $(SECURITY_FLAGS) -o $@ $<

keygenme_O2: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O2 -g $(SECURITY_FLAGS) -o $@ $<

keygenme_O3: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O3 -g $(SECURITY_FLAGS) -o $@ $<

keygenme_Os: $(SRC)
	$(CC) $(CFLAGS_COMMON) -Os -g $(SECURITY_FLAGS) -o $@ $<

# ──────────────────────────────────────────────
# Variantes strippées (sans symboles)
# Conditions réalistes (chapitre 5)
# ──────────────────────────────────────────────

keygenme_strip: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 $(SECURITY_FLAGS) -o $@ $<
	strip $@

keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O2 $(SECURITY_FLAGS) -o $@ $<
	strip $@

keygenme_O3_strip: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O3 $(SECURITY_FLAGS) -o $@ $<
	strip $@

# ──────────────────────────────────────────────
# Variantes avec protections réduites
# Pour comparaison avec checksec (chapitre 5 section 5.6)
# ──────────────────────────────────────────────

keygenme_nopie: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -g -fstack-protector-strong -no-pie -Wl,-z,relro,-z,now -o $@ $<

keygenme_nocanary: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -g -fno-stack-protector -pie -Wl,-z,relro,-z,now -o $@ $<

# ──────────────────────────────────────────────
# Variantes pour le diffing 
# keygenme_v1 et keygenme_v2 ont une légère différence dans check_license
# ──────────────────────────────────────────────

keygenme_v1: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O1 -g $(SECURITY_FLAGS) -DVERSION=1 -o $@ $<

keygenme_v2: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O1 -g $(SECURITY_FLAGS) -DVERSION=2 -DPATCH_EXTRA_CHECK -o $@ $<

# ──────────────────────────────────────────────
# Cibles utilitaires
# ──────────────────────────────────────────────

# Toutes les variantes principales
all: keygenme_O0 keygenme_O1 keygenme_O2 keygenme_O3 keygenme_Os \
     keygenme_strip keygenme_O2_strip keygenme_O3_strip \
     keygenme_nopie keygenme_nocanary \
     keygenme_v1 keygenme_v2

# Vérification rapide : affiche file + checksec pour chaque binaire
verify: all
	@echo "═══════════════════════════════════════════════"
	@echo "  Vérification des variantes keygenme"
	@echo "═══════════════════════════════════════════════"
	@for bin in keygenme_O0 keygenme_O2 keygenme_O2_strip \
	            keygenme_nopie keygenme_nocanary; do \
		echo ""; \
		echo "--- $$bin ---"; \
		file $$bin; \
		checksec --file=$$bin 2>/dev/null || echo "(checksec non disponible)"; \
	done

# Tailles comparées
sizes: all
	@echo "═══════════════════════════════════════════════"
	@echo "  Tailles des variantes"
	@echo "═══════════════════════════════════════════════"
	@ls -lhS keygenme_* | awk '{print $$5, $$9}'

clean:
	rm -f keygenme_O0 keygenme_O1 keygenme_O2 keygenme_O3 keygenme_Os \
	      keygenme_strip keygenme_O2_strip keygenme_O3_strip \
	      keygenme_nopie keygenme_nocanary \
	      keygenme_v1 keygenme_v2

.PHONY: all clean verify sizes 
