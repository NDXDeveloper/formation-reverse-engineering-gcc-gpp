# ============================================================================
# Formation Reverse Engineering — Chapitre 33
# Makefile pour le crackme Rust
#
# Produit 3 variantes du binaire :
#   1. crackme_rust_debug      — non optimisé, symboles DWARF complets
#   2. crackme_rust_release    — optimisé -O3, symboles conservés
#   3. crackme_rust_strip      — optimisé -O3, strippé, LTO, panic=abort
#
# Prérequis :
#   - rustc / cargo (https://rustup.rs)
#   - Le linker GNU ld (installé avec gcc/binutils)
#
# Vérification rapide :
#   $ rustc --version
#   $ cargo --version
#   $ ld --version
# ============================================================================

CARGO       := cargo
STRIP       := strip
READELF     := readelf
FILE_CMD    := file

CRATE_DIR   := crackme_rust
TARGET_DIR  := $(CRATE_DIR)/target

# Noms des binaires de sortie (copiés à la racine du dossier ch33)
OUT_DEBUG   := crackme_rust_debug
OUT_RELEASE := crackme_rust_release
OUT_STRIP   := crackme_rust_strip

# ── Cibles principales ─────────────────────────────────────────

.PHONY: all debug release release-strip clean info help

all: debug release release-strip info

# ── Debug : opt-level 0, symboles complets ─────────────────────

debug:
	@echo "══════════════════════════════════════════════"
	@echo "  [1/3] Compilation DEBUG (opt-level 0, symboles DWARF)"
	@echo "══════════════════════════════════════════════"
	cd $(CRATE_DIR) && $(CARGO) build 2>&1
	cp $(TARGET_DIR)/debug/crackme_rust ./$(OUT_DEBUG)
	@echo "[OK] $(OUT_DEBUG) généré."
	@echo ""

# ── Release : opt-level 3, symboles conservés ──────────────────

release:
	@echo "══════════════════════════════════════════════"
	@echo "  [2/3] Compilation RELEASE (opt-level 3, symboles)"
	@echo "══════════════════════════════════════════════"
	cd $(CRATE_DIR) && $(CARGO) build --release 2>&1
	cp $(TARGET_DIR)/release/crackme_rust ./$(OUT_RELEASE)
	@echo "[OK] $(OUT_RELEASE) généré."
	@echo ""

# ── Release strippé : optimisé, LTO, panic=abort, strip ───────

release-strip:
	@echo "══════════════════════════════════════════════"
	@echo "  [3/3] Compilation RELEASE STRIPPÉ (LTO, panic=abort, strip)"
	@echo "══════════════════════════════════════════════"
	cd $(CRATE_DIR) && $(CARGO) build --profile release-strip 2>&1
	cp $(TARGET_DIR)/release-strip/crackme_rust ./$(OUT_STRIP)
	@# Double strip par sécurité (certaines versions de cargo strip incomplètement)
	$(STRIP) --strip-all ./$(OUT_STRIP) 2>/dev/null || true
	@echo "[OK] $(OUT_STRIP) généré."
	@echo ""

# ── Informations sur les binaires produits ─────────────────────

info: 
	@echo "══════════════════════════════════════════════"
	@echo "  Résumé des binaires générés"
	@echo "══════════════════════════════════════════════"
	@echo ""
	@for bin in $(OUT_DEBUG) $(OUT_RELEASE) $(OUT_STRIP); do \
		if [ -f $$bin ]; then \
			echo "── $$bin ──"; \
			echo "  Taille : $$(du -h $$bin | cut -f1)"; \
			echo "  Type   : $$($(FILE_CMD) $$bin | cut -d: -f2-)"; \
			echo "  Sections : $$($(READELF) -S $$bin 2>/dev/null | grep -c '\\[')"; \
			echo "  Symboles : $$($(READELF) -s $$bin 2>/dev/null | grep -c 'FUNC')"; \
			echo ""; \
		fi \
	done
	@echo "══════════════════════════════════════════════"
	@echo "  Comparez les tailles et le nombre de symboles"
	@echo "  entre les 3 variantes — c'est le point de départ"
	@echo "  du chapitre 33."
	@echo "══════════════════════════════════════════════"

# ── Nettoyage ──────────────────────────────────────────────────

clean:
	cd $(CRATE_DIR) && $(CARGO) clean 2>/dev/null || true
	rm -f $(OUT_DEBUG) $(OUT_RELEASE) $(OUT_STRIP)
	@echo "[OK] Nettoyage terminé."

# ── Aide ───────────────────────────────────────────────────────

help:
	@echo "Cibles disponibles :"
	@echo "  make all            — Compile les 3 variantes + affiche le résumé"
	@echo "  make debug          — Compile en debug (opt-level 0, DWARF)"
	@echo "  make release        — Compile en release (opt-level 3, symboles)"
	@echo "  make release-strip  — Compile en release strippé (LTO, strip)"
	@echo "  make info           — Affiche les infos des binaires existants"
	@echo "  make clean          — Supprime tous les binaires et le cache cargo"
	@echo "  make help           — Affiche cette aide"
