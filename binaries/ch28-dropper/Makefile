# ═══════════════════════════════════════════════════════════════
#  Makefile — Chapitre 28 : Dropper ELF avec communication réseau
# ═══════════════════════════════════════════════════════════════
#
#  ⚠️  Les binaires produits sont STRICTEMENT ÉDUCATIFS.
#      Ne les exécutez QUE dans une VM sandboxée isolée.
#
#  Cibles :
#    make all          → Compile toutes les variantes
#    make dropper_O0   → -O0 -g  (symboles, pas d'optimisation)
#    make dropper_O2   → -O2 -g  (optimisé, avec symboles)
#    make dropper_O2_strip → -O2, strippé (réaliste)
#    make clean        → Supprime les binaires
#
# ═══════════════════════════════════════════════════════════════

CC      = gcc
SRC     = dropper_sample.c

# Flags communs
CFLAGS_COMMON = -Wall -Wextra -std=c11

# Flags de sécurité réalistes (pour que checksec montre des protections)
CFLAGS_SEC    = -fstack-protector-strong -D_FORTIFY_SOURCE=2
LDFLAGS_SEC   = -Wl,-z,relro,-z,now -pie

# ─── Variantes ────────────────────────────────────────────────

# Variante 1 : debug, aucune optimisation, symboles DWARF complets
#   → Idéale pour l'apprentissage et le suivi pas-à-pas dans GDB
CFLAGS_O0     = $(CFLAGS_COMMON) -O0 -g -gdwarf-4 $(CFLAGS_SEC) -fPIE
LDFLAGS_O0    = $(LDFLAGS_SEC)

# Variante 2 : optimisé -O2, symboles DWARF conservés
#   → Observer l'impact des optimisations tout en gardant les noms
CFLAGS_O2     = $(CFLAGS_COMMON) -O2 -g -gdwarf-4 $(CFLAGS_SEC) -fPIE
LDFLAGS_O2    = $(LDFLAGS_SEC)

# Variante 3 : optimisé -O2, strippé (pas de symboles)
#   → Conditions réalistes d'analyse d'un vrai sample
CFLAGS_O2S    = $(CFLAGS_COMMON) -O2 $(CFLAGS_SEC) -fPIE
LDFLAGS_O2S   = $(LDFLAGS_SEC) -s

# ─── Cibles ───────────────────────────────────────────────────

.PHONY: all clean info

all: dropper_O0 dropper_O2 dropper_O2_strip

dropper_O0: $(SRC)
	$(CC) $(CFLAGS_O0) $(LDFLAGS_O0) -o $@ $<
	@echo "[+] Built $@ (debug, -O0, with symbols)"
	@ls -lh $@
	@file $@

dropper_O2: $(SRC)
	$(CC) $(CFLAGS_O2) $(LDFLAGS_O2) -o $@ $<
	@echo "[+] Built $@ (optimized, -O2, with symbols)"
	@ls -lh $@
	@file $@

dropper_O2_strip: $(SRC)
	$(CC) $(CFLAGS_O2S) $(LDFLAGS_O2S) -o $@ $<
	@echo "[+] Built $@ (optimized, -O2, stripped)"
	@ls -lh $@
	@file $@

# ─── Utilitaires ──────────────────────────────────────────────

# Affiche un résumé des binaires produits (taille, sections, protections)
info: all
	@echo ""
	@echo "══════════════════════════════════════════════════"
	@echo "  Résumé des binaires produits"
	@echo "══════════════════════════════════════════════════"
	@echo ""
	@for bin in dropper_O0 dropper_O2 dropper_O2_strip; do \
		echo "--- $$bin ---"; \
		file $$bin; \
		size $$bin; \
		echo "Sections:"; \
		readelf -S $$bin 2>/dev/null | grep -E '\.text|\.data|\.bss|\.rodata|\.plt|\.got' || true; \
		if command -v checksec >/dev/null 2>&1; then \
			checksec --file=$$bin 2>/dev/null || true; \
		fi; \
		echo ""; \
	done

clean:
	rm -f dropper_O0 dropper_O2 dropper_O2_strip
	@echo "[*] Cleaned all binaries"
