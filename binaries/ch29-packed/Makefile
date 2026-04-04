# ============================================================================
# Makefile — Chapitre 29 : Détection de packing, unpack et reconstruction
# Formation Reverse Engineering — Chaîne GNU
#
# Cibles produites :
#   packed_sample_O0          → -O0 -g        (référence, avec symboles DWARF)
#   packed_sample_O0_strip    → -O0, strippé  (référence sans symboles)
#   packed_sample_O2          → -O2 -g        (optimisé, avec symboles)
#   packed_sample_O2_strip    → -O2, strippé  (optimisé sans symboles)
#   packed_sample_upx         → -O2, strippé, packé UPX standard
#   packed_sample_upx_tampered→ -O2, strippé, packé UPX avec magic altéré
#                                (résiste à `upx -d`, nécessite unpack dynamique)
#
# Usage :
#   make all          Compile toutes les variantes
#   make clean        Supprime les binaires générés
#   make check        Vérifie que UPX est installé
#   make compare      Affiche les tailles de toutes les variantes
#   make entropy      Affiche l'entropie des sections (nécessite binwalk)
#
# Prérequis :
#   - gcc
#   - strip   (binutils)
#   - upx     (apt install upx-ucl)
#   - python3 (pour l'altération des magic bytes UPX)
#   - binwalk (optionnel, pour `make entropy`)
#   - readelf (binutils, pour `make compare`)
# ============================================================================

CC        := gcc
CFLAGS_O0 := -O0 -g -Wall -Wextra -std=c11
CFLAGS_O2 := -O2 -g -Wall -Wextra -std=c11
LDFLAGS   := -no-pie
STRIP     := strip
UPX       := upx

SRC       := packed_sample.c

# Cibles principales
BIN_O0          := packed_sample_O0
BIN_O0_STRIP    := packed_sample_O0_strip
BIN_O2          := packed_sample_O2
BIN_O2_STRIP    := packed_sample_O2_strip
BIN_UPX         := packed_sample_upx
BIN_UPX_TAMPER  := packed_sample_upx_tampered

ALL_BINS := $(BIN_O0) $(BIN_O0_STRIP) $(BIN_O2) $(BIN_O2_STRIP) \
            $(BIN_UPX) $(BIN_UPX_TAMPER)

# ============================================================================
# Cibles
# ============================================================================

.PHONY: all clean check compare entropy

all: check $(ALL_BINS)
	@echo ""
	@echo "══════════════════════════════════════════════════════"
	@echo " Chapitre 29 — Tous les binaires générés avec succès"
	@echo "══════════════════════════════════════════════════════"
	@echo ""
	@$(MAKE) --no-print-directory compare

# --- Vérification des prérequis ---
check:
	@command -v $(CC) >/dev/null 2>&1 || \
		{ echo "[!] gcc introuvable. Installez build-essential."; exit 1; }
	@command -v $(UPX) >/dev/null 2>&1 || \
		{ echo "[!] upx introuvable. Installez : apt install upx-ucl"; exit 1; }
	@command -v $(STRIP) >/dev/null 2>&1 || \
		{ echo "[!] strip introuvable. Installez binutils."; exit 1; }
	@command -v python3 >/dev/null 2>&1 || \
		{ echo "[!] python3 introuvable. Installez python3."; exit 1; }
	@echo "[OK] Tous les outils nécessaires sont disponibles."

# --- Binaire O0 avec symboles (référence pour débogage) ---
$(BIN_O0): $(SRC)
	$(CC) $(CFLAGS_O0) $(LDFLAGS) -o $@ $<
	@echo "[+] $@ compilé (-O0 -g, avec symboles DWARF)"

# --- Binaire O0 strippé ---
$(BIN_O0_STRIP): $(BIN_O0)
	cp $< $@
	$(STRIP) --strip-all $@
	@echo "[+] $@ généré (-O0, strippé)"

# --- Binaire O2 avec symboles ---
$(BIN_O2): $(SRC)
	$(CC) $(CFLAGS_O2) $(LDFLAGS) -o $@ $<
	@echo "[+] $@ compilé (-O2 -g, avec symboles DWARF)"

# --- Binaire O2 strippé (base pour le packing) ---
$(BIN_O2_STRIP): $(BIN_O2)
	cp $< $@
	$(STRIP) --strip-all $@
	@echo "[+] $@ généré (-O2, strippé)"

# --- Packé UPX standard (décompressible avec `upx -d`) ---
$(BIN_UPX): $(BIN_O2_STRIP)
	cp $< $@
	$(UPX) --best --force $@ >/dev/null 2>&1
	@echo "[+] $@ généré (UPX standard, décompressible avec upx -d)"

# --- Packé UPX avec magic bytes altérés ---
#     On corrompt la signature "UPX!" (0x55505821) dans le binaire
#     pour empêcher `upx -d` de fonctionner. L'étudiant devra
#     utiliser l'approche dynamique (dump mémoire via GDB).
#
#     Méthode : remplacer les octets "UPX!" par "FKP!" à chaque
#     occurrence dans le fichier packé.
$(BIN_UPX_TAMPER): $(BIN_O2_STRIP)
	cp $< $@
	$(UPX) --best --force $@ >/dev/null 2>&1
	@# Altérer toutes les signatures UPX! → FKP! (0x464B5021)
	@python3 -c "\
import sys; \
data = open('$@','rb').read(); \
patched = data.replace(b'UPX!', b'FKP!'); \
patched = patched.replace(b'UPX0', b'XP_0'); \
patched = patched.replace(b'UPX1', b'XP_1'); \
patched = patched.replace(b'UPX2', b'XP_2'); \
open('$@','wb').write(patched); \
n = data.count(b'UPX!'); \
print(f'[+] {n} signature(s) UPX! altérée(s) dans $@')"
	@echo "[+] $@ généré (UPX altéré, résiste à upx -d)"

# --- Comparaison des tailles ---
compare:
	@echo ""
	@echo "┌─────────────────────────────────┬──────────┬─────────────────────┐"
	@echo "│ Binaire                         │ Taille   │ Type                │"
	@echo "├─────────────────────────────────┼──────────┼─────────────────────┤"
	@for bin in $(ALL_BINS); do \
		if [ -f "$$bin" ]; then \
			size=$$(stat -c%s "$$bin" 2>/dev/null || stat -f%z "$$bin" 2>/dev/null); \
			type=$$(file -b "$$bin" | cut -c1-19); \
			printf "│ %-31s │ %6s o │ %-19s │\n" "$$bin" "$$size" "$$type"; \
		fi; \
	done
	@echo "└─────────────────────────────────┴──────────┴─────────────────────┘"
	@echo ""
	@echo "Astuce : comparez les tailles UPX vs originales et lancez"
	@echo "         strings packed_sample_upx | wc -l"
	@echo "         vs"
	@echo "         strings packed_sample_O2_strip | wc -l"

# --- Analyse d'entropie (nécessite binwalk) ---
entropy:
	@command -v binwalk >/dev/null 2>&1 || \
		{ echo "[!] binwalk introuvable. Installez : apt install binwalk"; exit 1; }
	@echo ""
	@echo "═══ Entropie : binaire NON packé ═══"
	@binwalk -E $(BIN_O2_STRIP) 2>/dev/null || true
	@echo ""
	@echo "═══ Entropie : binaire packé UPX ═══"
	@binwalk -E $(BIN_UPX) 2>/dev/null || true
	@echo ""
	@echo "═══ Entropie : binaire packé UPX altéré ═══"
	@binwalk -E $(BIN_UPX_TAMPER) 2>/dev/null || true

# --- Nettoyage ---
clean:
	rm -f $(ALL_BINS)
	@echo "[OK] Binaires supprimés."
