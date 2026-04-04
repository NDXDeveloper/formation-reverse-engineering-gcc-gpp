# ═══════════════════════════════════════════════════════════════
# Makefile — Chapitre 19 : Anti-reversing et protections compilateur
# Formation Reverse Engineering — Chaîne GNU
#
# Produit de nombreuses variantes d'un même binaire pour
# illustrer l'impact de chaque protection.
#
# Usage :
#   make all          — compile toutes les variantes
#   make anti_debug   — variantes anti-debug uniquement
#   make protections  — variantes protections compilateur
#   make packed       — variantes packées (nécessite upx)
#   make clean        — supprime tous les binaires
#   make list         — affiche la liste des cibles
#   make checksec     — lance checksec sur toutes les variantes
#
# ⚠️ Usage strictement éducatif — Licence MIT
# ═══════════════════════════════════════════════════════════════

CC       = gcc
CFLAGS   = -Wall -Wextra
LDFLAGS  =

# Répertoire de sortie
OUT      = build
$(shell mkdir -p $(OUT))

# ───────────────────────────────────────────
# Fichiers sources
# ───────────────────────────────────────────
SRC_ANTI   = anti_reverse.c
SRC_VULN   = vuln_demo.c

# ───────────────────────────────────────────
# Cibles : anti_reverse (crackme protégé)
# ───────────────────────────────────────────

# 1. Version de base avec symboles (pour comparer)
ANTI_TARGETS  = $(OUT)/anti_reverse_debug

# 2. Versions strippées (section 19.1)
ANTI_TARGETS += $(OUT)/anti_reverse_stripped

# 3. Versions avec protections sélectives (section 19.7–19.8)
ANTI_TARGETS += $(OUT)/anti_reverse_ptrace_only
ANTI_TARGETS += $(OUT)/anti_reverse_timing_only
ANTI_TARGETS += $(OUT)/anti_reverse_procfs_only
ANTI_TARGETS += $(OUT)/anti_reverse_int3_only
ANTI_TARGETS += $(OUT)/anti_reverse_all_checks

# 4. Versions à différents niveaux d'optimisation
ANTI_TARGETS += $(OUT)/anti_reverse_O0
ANTI_TARGETS += $(OUT)/anti_reverse_O2
ANTI_TARGETS += $(OUT)/anti_reverse_O3
ANTI_TARGETS += $(OUT)/anti_reverse_Os

# 5. Version packée UPX (section 19.2)
ANTI_PACKED   = $(OUT)/anti_reverse_upx

# ───────────────────────────────────────────
# Cibles : vuln_demo (protections compilateur)
# ───────────────────────────────────────────

# 6. Démonstration canary (section 19.5)
VULN_TARGETS  = $(OUT)/vuln_no_canary
VULN_TARGETS += $(OUT)/vuln_canary
VULN_TARGETS += $(OUT)/vuln_canary_all

# 7. Démonstration PIE (section 19.5)
VULN_TARGETS += $(OUT)/vuln_no_pie
VULN_TARGETS += $(OUT)/vuln_pie

# 8. Démonstration NX (section 19.5)
VULN_TARGETS += $(OUT)/vuln_execstack

# 9. Démonstration RELRO (section 19.6)
VULN_TARGETS += $(OUT)/vuln_no_relro
VULN_TARGETS += $(OUT)/vuln_partial_relro
VULN_TARGETS += $(OUT)/vuln_full_relro

# 10. Combinaisons (section 19.9 — audit checksec)
VULN_TARGETS += $(OUT)/vuln_max_protection
VULN_TARGETS += $(OUT)/vuln_min_protection

# ═══════════════════════════════════════════
# Règles principales
# ═══════════════════════════════════════════

.PHONY: all anti_debug protections packed clean list checksec

all: anti_debug protections packed
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo " Toutes les variantes compilées dans $(OUT)/"
	@echo "══════════════════════════════════════════"
	@echo ""

anti_debug: $(ANTI_TARGETS)
	@echo "[OK] Variantes anti-debug compilées."

protections: $(VULN_TARGETS)
	@echo "[OK] Variantes protections compilateur compilées."

packed: $(ANTI_PACKED)
	@echo "[OK] Variantes packées compilées."

# ═══════════════════════════════════════════
# anti_reverse — variantes
# ═══════════════════════════════════════════

# Macros de contrôle des protections anti-debug
DISABLE_ALL  = -DENABLE_PTRACE_CHECK=0 -DENABLE_PROCFS_CHECK=0 \
               -DENABLE_TIMING_CHECK=0 -DENABLE_INT3_SCAN=0   \
               -DENABLE_CHECKSUM=0
ENABLE_ONLY_PTRACE = $(DISABLE_ALL) -DENABLE_PTRACE_CHECK=1
ENABLE_ONLY_TIMING = $(DISABLE_ALL) -DENABLE_TIMING_CHECK=1
ENABLE_ONLY_PROCFS = $(DISABLE_ALL) -DENABLE_PROCFS_CHECK=1
ENABLE_ONLY_INT3   = $(DISABLE_ALL) -DENABLE_INT3_SCAN=1

# Version debug : symboles, pas d'opti, pas de protections anti-debug
$(OUT)/anti_reverse_debug: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O0 -g $(DISABLE_ALL) -o $@ $< -lrt

# Version strippée : O2, pas de symboles
$(OUT)/anti_reverse_stripped: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O2 $(DISABLE_ALL) -o $@ $< -lrt
	strip $@

# Protection ptrace uniquement
$(OUT)/anti_reverse_ptrace_only: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O0 -g $(ENABLE_ONLY_PTRACE) -o $@ $< -lrt

# Protection timing uniquement
$(OUT)/anti_reverse_timing_only: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O0 -g $(ENABLE_ONLY_TIMING) -o $@ $< -lrt

# Protection /proc/self/status uniquement
$(OUT)/anti_reverse_procfs_only: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O0 -g $(ENABLE_ONLY_PROCFS) -o $@ $< -lrt

# Protection int3 scan uniquement
$(OUT)/anti_reverse_int3_only: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O0 -g $(ENABLE_ONLY_INT3) -o $@ $< -lrt

# Toutes les protections anti-debug activées
$(OUT)/anti_reverse_all_checks: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O2 -o $@ $< -lrt
	strip $@

# Niveaux d'optimisation (sans anti-debug pour isoler l'effet)
$(OUT)/anti_reverse_O0: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O0 -g $(DISABLE_ALL) -o $@ $< -lrt

$(OUT)/anti_reverse_O2: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O2 -g $(DISABLE_ALL) -o $@ $< -lrt

$(OUT)/anti_reverse_O3: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O3 -g $(DISABLE_ALL) -o $@ $< -lrt

$(OUT)/anti_reverse_Os: $(SRC_ANTI)
	$(CC) $(CFLAGS) -Os -g $(DISABLE_ALL) -o $@ $< -lrt

# Version packée UPX (nécessite upx installé)
$(OUT)/anti_reverse_upx: $(SRC_ANTI)
	$(CC) $(CFLAGS) -O2 -no-pie $(DISABLE_ALL) -o $(OUT)/.anti_reverse_pre_upx $< -lrt
	strip $(OUT)/.anti_reverse_pre_upx
	@if command -v upx >/dev/null 2>&1; then \
		upx -9 -o $@ $(OUT)/.anti_reverse_pre_upx; \
	else \
		echo "[WARN] upx non trouvé — copie du binaire non packé"; \
		cp $(OUT)/.anti_reverse_pre_upx $@; \
	fi
	@rm -f $(OUT)/.anti_reverse_pre_upx

# ═══════════════════════════════════════════
# vuln_demo — variantes protections compilateur
# ═══════════════════════════════════════════

# Flags communs pour vuln_demo
VULN_BASE = $(CFLAGS) -O0 -g

# --- Stack canary (section 19.5) ---

$(OUT)/vuln_no_canary: $(SRC_VULN)
	$(CC) $(VULN_BASE) -fno-stack-protector -o $@ $<

$(OUT)/vuln_canary: $(SRC_VULN)
	$(CC) $(VULN_BASE) -fstack-protector -o $@ $<

$(OUT)/vuln_canary_all: $(SRC_VULN)
	$(CC) $(VULN_BASE) -fstack-protector-all -o $@ $<

# --- PIE / No-PIE (section 19.5) ---

$(OUT)/vuln_no_pie: $(SRC_VULN)
	$(CC) $(VULN_BASE) -no-pie -fno-pie -o $@ $<

$(OUT)/vuln_pie: $(SRC_VULN)
	$(CC) $(VULN_BASE) -pie -fPIE -o $@ $<

# --- NX / Execstack (section 19.5) ---

$(OUT)/vuln_execstack: $(SRC_VULN)
	$(CC) $(VULN_BASE) -z execstack -o $@ $<

# --- RELRO (section 19.6) ---

$(OUT)/vuln_no_relro: $(SRC_VULN)
	$(CC) $(VULN_BASE) -Wl,-z,norelro -no-pie -o $@ $<

$(OUT)/vuln_partial_relro: $(SRC_VULN)
	$(CC) $(VULN_BASE) -Wl,-z,relro -no-pie -o $@ $<

$(OUT)/vuln_full_relro: $(SRC_VULN)
	$(CC) $(VULN_BASE) -Wl,-z,relro,-z,now -no-pie -o $@ $<

# --- Combinaisons extrêmes (section 19.9) ---

# Protection maximale : tout activé
$(OUT)/vuln_max_protection: $(SRC_VULN)
	$(CC) $(CFLAGS) -O2 \
		-fstack-protector-all \
		-pie -fPIE \
		-Wl,-z,relro,-z,now \
		-D_FORTIFY_SOURCE=2 \
		-o $@ $<
	strip $@

# Protection minimale : tout désactivé
$(OUT)/vuln_min_protection: $(SRC_VULN)
	$(CC) $(CFLAGS) -O0 -g \
		-fno-stack-protector \
		-no-pie -fno-pie \
		-Wl,-z,norelro \
		-z execstack \
		-o $@ $<

# ═══════════════════════════════════════════
# Utilitaires
# ═══════════════════════════════════════════

# Lancer checksec sur toutes les variantes
checksec: all
	@echo ""
	@echo "═══════════════════════════════════════════════"
	@echo " Audit checksec de toutes les variantes"
	@echo "═══════════════════════════════════════════════"
	@echo ""
	@if command -v checksec >/dev/null 2>&1; then \
		for bin in $(OUT)/*; do \
			if [ -f "$$bin" ] && [ -x "$$bin" ]; then \
				echo "── $$bin ──"; \
				checksec --file="$$bin"; \
				echo ""; \
			fi; \
		done; \
	else \
		echo "[ERREUR] checksec n'est pas installé."; \
		echo "  → apt install checksec  (Debian/Ubuntu)"; \
		echo "  → pip install checksec  (Python)"; \
	fi

# Lister toutes les cibles
list:
	@echo ""
	@echo "anti_reverse (crackme protégé) :"
	@echo "  anti_reverse_debug          — symboles, O0, aucune protection anti-debug"
	@echo "  anti_reverse_stripped        — O2, strippé, aucune protection anti-debug"
	@echo "  anti_reverse_ptrace_only     — ptrace check uniquement"
	@echo "  anti_reverse_timing_only     — timing check uniquement"
	@echo "  anti_reverse_procfs_only     — /proc/self/status check uniquement"
	@echo "  anti_reverse_int3_only       — int3 scan uniquement"
	@echo "  anti_reverse_all_checks      — toutes les protections, strippé, O2"
	@echo "  anti_reverse_O0 / O2 / O3 / Os — niveaux d'optimisation"
	@echo "  anti_reverse_upx             — packé avec UPX"
	@echo ""
	@echo "vuln_demo (protections compilateur) :"
	@echo "  vuln_no_canary               — sans stack canary"
	@echo "  vuln_canary                  — avec -fstack-protector"
	@echo "  vuln_canary_all              — avec -fstack-protector-all"
	@echo "  vuln_no_pie                  — sans PIE"
	@echo "  vuln_pie                     — avec PIE"
	@echo "  vuln_execstack               — pile exécutable (NX désactivé)"
	@echo "  vuln_no_relro                — sans RELRO"
	@echo "  vuln_partial_relro           — Partial RELRO"
	@echo "  vuln_full_relro              — Full RELRO"
	@echo "  vuln_max_protection          — tout activé, strippé, O2"
	@echo "  vuln_min_protection          — tout désactivé, symboles, O0"
	@echo ""

clean:
	rm -rf $(OUT)
	@echo "[OK] Répertoire $(OUT)/ supprimé."
