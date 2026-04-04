# ============================================================================
# Makefile — Chapitre 34 : Reverse Engineering de binaires Go
# Formation Reverse Engineering — Applications compilées avec la chaîne GNU
#
# Produit plusieurs variantes du crackme pour illustrer les défis du RE Go :
#   - crackme_go            : binaire standard avec symboles
#   - crackme_go_strip      : binaire strippé (strip -s)
#   - crackme_go_upx        : binaire strippé + compressé UPX
#   - crackme_go_nopie      : binaire sans PIE (facilite l'analyse statique)
#   - crackme_go_race       : binaire compilé avec -race (détecteur de data races)
#
# Usage :
#   make              — compile toutes les variantes
#   make clean        — supprime les binaires produits
#   make info         — affiche les tailles et métadonnées de chaque variante
#
# Prérequis :
#   - go >= 1.21       (vérifier avec : go version)
#   - strip            (GNU binutils)
#   - upx              (optionnel, pour la variante packée)
#
# Licence MIT — Usage strictement éducatif.
# ============================================================================

# ---------------------------------------------------------------------------
# Variables
# ---------------------------------------------------------------------------
GO         := go
GOFLAGS    :=
STRIP      := strip
UPX        := upx
SRC_DIR    := crackme_go
MODULE     := $(SRC_DIR)/main.go

OUT_DIR    := build
BIN        := $(OUT_DIR)/crackme_go
BIN_STRIP  := $(OUT_DIR)/crackme_go_strip
BIN_UPX    := $(OUT_DIR)/crackme_go_upx
BIN_NOPIE  := $(OUT_DIR)/crackme_go_nopie
BIN_RACE   := $(OUT_DIR)/crackme_go_race

# Vérification de la présence d'UPX (optionnel)
HAS_UPX    := $(shell command -v $(UPX) 2>/dev/null)

# Liste des cibles principales
TARGETS    := $(BIN) $(BIN_STRIP) $(BIN_NOPIE) $(BIN_RACE)
ifdef HAS_UPX
TARGETS    += $(BIN_UPX)
endif

# ---------------------------------------------------------------------------
# Cibles
# ---------------------------------------------------------------------------
.PHONY: all clean info check-go

all: check-go $(TARGETS)
	@echo ""
	@echo "══════════════════════════════════════════════════════════"
	@echo "  ✅  Chapitre 34 — Toutes les variantes compilées."
	@echo "══════════════════════════════════════════════════════════"
	@echo ""
	@$(MAKE) --no-print-directory info

# --- Vérification de l'environnement ----------------------------------------
check-go:
	@command -v $(GO) >/dev/null 2>&1 || \
		{ echo "❌ Erreur : 'go' introuvable. Installez Go >= 1.21."; exit 1; }
	@echo "[*] Go détecté : $$($(GO) version)"

# --- Binaire standard (symboles conservés) -----------------------------------
$(BIN): $(MODULE) | $(OUT_DIR)
	@echo "[*] Compilation : $@ (symboles conservés)"
	cd $(SRC_DIR) && $(GO) build $(GOFLAGS) -o ../$(BIN) .

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# --- Binaire strippé ---------------------------------------------------------
$(BIN_STRIP): $(BIN)
	@echo "[*] Stripping : $@"
	cp $(BIN) $(BIN_STRIP)
	$(STRIP) -s $(BIN_STRIP)

# --- Binaire strippé + UPX ---------------------------------------------------
$(BIN_UPX): $(BIN_STRIP)
ifdef HAS_UPX
	@echo "[*] Compression UPX : $@"
	cp $(BIN_STRIP) $(BIN_UPX)
	$(UPX) --best --lzma -q $(BIN_UPX) >/dev/null 2>&1 || \
		{ echo "⚠️  UPX a échoué (normal sur certains Go récents). Copie brute."; \
		  cp $(BIN_STRIP) $(BIN_UPX); }
else
	@echo "⚠️  UPX non installé — variante $@ ignorée."
endif

# --- Binaire sans PIE (adresses fixes, facilite l'analyse statique) ----------
$(BIN_NOPIE): $(MODULE)
	@echo "[*] Compilation : $@ (sans PIE)"
	cd $(SRC_DIR) && CGO_ENABLED=0 $(GO) build \
		-ldflags="-extldflags=-no-pie" \
		-o ../$(BIN_NOPIE) .

# --- Binaire avec détecteur de data races ------------------------------------
#     Illustre l'instrumentation runtime supplémentaire visible en RE.
$(BIN_RACE): $(MODULE)
	@echo "[*] Compilation : $@ (race detector activé)"
	cd $(SRC_DIR) && CGO_ENABLED=1 $(GO) build -race -o ../$(BIN_RACE) . 2>/dev/null || \
		{ echo "⚠️  -race nécessite CGO. Compilation sans -race comme fallback."; \
		  cd $(SRC_DIR) && $(GO) build -o ../$(BIN_RACE) .; }

# --- Informations sur les variantes -----------------------------------------
info:
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Variantes produites — Chapitre 34                         ║"
	@echo "╠════════════════════════════╦═══════════╦════════════════════╣"
	@echo "║ Fichier                    ║ Taille    ║ Type               ║"
	@echo "╠════════════════════════════╬═══════════╬════════════════════╣"
	@for f in $(BIN) $(BIN_STRIP) $(BIN_UPX) $(BIN_NOPIE) $(BIN_RACE); do \
		if [ -f "$$f" ]; then \
			size=$$(du -h "$$f" | cut -f1); \
			type=$$(file -b "$$f" | cut -d',' -f1); \
			printf "║ %-26s ║ %-9s ║ %-18s ║\n" "$$f" "$$size" "$$type"; \
		fi; \
	done
	@echo "╚════════════════════════════╩═══════════╩════════════════════╝"
	@echo ""
	@echo "💡 Astuce : comparez les tailles entre la version standard et strippée."
	@echo "   Puis lancez 'strings <binaire> | grep main\\.' pour voir l'effet"
	@echo "   du stripping sur les noms de fonctions Go."

# --- Nettoyage ---------------------------------------------------------------
clean:
	@echo "[*] Nettoyage des binaires du chapitre 34..."
	rm -rf $(OUT_DIR)
	@echo "[✓] Nettoyage terminé."
