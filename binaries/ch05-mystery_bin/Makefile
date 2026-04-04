# ═══════════════════════════════════════════════════════════════
#  ch05-mystery_bin — Makefile
#  Formation Reverse Engineering — Chapitre 5
#
# Formation Reverse Engineering — Chapitre 5
# Compile mystery_bin en plusieurs variantes pour le checkpoint.
#
# Usage :
#   make              → compile toutes les variantes
#   make mystery_bin  → compile la variante par défaut (checkpoint)
#   make clean        → supprime les binaires compilés
#
# La variante par défaut du checkpoint est compilée avec -O1 et sans strip,
# ce qui offre un bon équilibre : les symboles sont présents (le triage est
# riche), mais le code a subi quelques optimisations (pas trivial).

# ═══════════════════════════════════════════════════════════════

CC      = gcc
CFLAGS_COMMON = -Wall -Wextra

# Source
SRC = mystery_bin.c

# ──────────────────────────────────────────────
# Variante par défaut : celle utilisée dans le checkpoint
# -O1          : optimisation légère (quelques inlinings, code lisible)
# -g           : symboles DWARF (readelf -S montrera .debug_*)
# -fstack-protector-strong : canary activé (visible dans checksec/nm)
# -pie         : PIE activé (défaut GCC, explicité ici pour la clarté)
# -Wl,-z,relro,-z,now : Full RELRO
# ──────────────────────────────────────────────
mystery_bin: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O1 -g \
		-fstack-protector-strong \
		-pie \
		-Wl,-z,relro,-z,now \
		-o $@ $<

# ──────────────────────────────────────────────
# Variantes supplémentaires (pour exploration libre)
# ──────────────────────────────────────────────

# Sans optimisation, avec symboles complets
mystery_bin_O0: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -g \
		-fstack-protector-strong \
		-pie \
		-Wl,-z,relro,-z,now \
		-o $@ $<

# Optimisé -O2, strippé (difficulté accrue)
mystery_bin_O2_strip: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O2 \
		-fstack-protector-strong \
		-pie \
		-Wl,-z,relro,-z,now \
		-o $@ $<
	strip $@

# Sans canary et sans PIE (pour comparaison checksec)
mystery_bin_no_protections: $(SRC)
	$(CC) $(CFLAGS_COMMON) -O0 -g \
		-fno-stack-protector \
		-no-pie \
		-Wl,-z,norelro \
		-o $@ $<

# ──────────────────────────────────────────────
# Cibles utilitaires
# ──────────────────────────────────────────────

all: mystery_bin mystery_bin_O0 mystery_bin_O2_strip mystery_bin_no_protections

clean:
	rm -f mystery_bin mystery_bin_O0 mystery_bin_O2_strip mystery_bin_no_protections

.PHONY: all clean
