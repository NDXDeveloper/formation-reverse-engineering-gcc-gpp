# ============================================================================
# Chapitre 16 — Comprendre les optimisations du compilateur
# Makefile — Génère toutes les variantes de binaires d'entraînement
# ============================================================================
#
# Usage :
#   make all          Compile tous les binaires à tous les niveaux
#   make clean        Supprime tous les binaires générés
#   make s16_1        Compile uniquement les binaires de la section 16.1
#   make s16_2        Compile uniquement les binaires de la section 16.2
#   make s16_3        Compile uniquement les binaires de la section 16.3
#   make s16_4        Compile uniquement les binaires de la section 16.4
#   make s16_5        Compile uniquement les binaires de la section 16.5 (LTO)
#   make s16_6        Compile uniquement les binaires de la section 16.6
#   make s16_7        Compile les binaires de 16.7 (GCC vs Clang — nécessite clang)
#
# Chaque binaire est produit en 6 variantes :
#   _O0          -O0 -g    (référence non optimisée, symboles DWARF)
#   _O1          -O1 -g    (optimisations conservatrices)
#   _O2          -O2 -g    (optimisations standard)
#   _O3          -O3 -g    (optimisations agressives)
#   _Os          -Os -g    (optimisation en taille)
#   _O2_strip    -O2 -s    (production réaliste : optimisé + strippé)
#
# La section 16.3 produit en plus une variante _O3_avx2 avec -mavx2.
# La section 16.7 produit des variantes _clang_O0 et _clang_O2.
# ============================================================================

CC      := gcc
CXX     := g++
CLANG   := clang
CFLAGS  := -Wall -Wextra -std=c11
CXXFLAGS:= -Wall -Wextra -std=c++17

# Répertoire de sortie
OUT     := build

# Niveaux d'optimisation (sauf strip, traité à part)
OPT_LEVELS := O0 O1 O2 O3 Os

# ============================================================================
# Sources par section
# ============================================================================

# 16.1 — Impact des niveaux d'optimisation
SRC_16_1 := opt_levels_demo.c

# 16.2 — Inlining
SRC_16_2 := inlining_demo.c

# 16.3 — Déroulage de boucles et vectorisation
SRC_16_3 := loop_unroll_vec.c

# 16.4 — Tail call optimization
SRC_16_4 := tail_call.c

# 16.5 — Link-Time Optimization (multi-fichiers)
SRC_16_5_MAIN := lto_main.c
SRC_16_5_MATH := lto_math.c
SRC_16_5_UTIL := lto_utils.c

# 16.6 — Patterns et idiomes GCC
SRC_16_6 := gcc_idioms.c

# 16.7 — GCC vs Clang (réutilise les sources existantes)
SRCS_16_7 := opt_levels_demo.c gcc_idioms.c

# ============================================================================
# Cibles par section (noms de base des binaires)
# ============================================================================

BIN_16_1 := opt_levels_demo
BIN_16_2 := inlining_demo
BIN_16_3 := loop_unroll_vec
BIN_16_4 := tail_call
BIN_16_5 := lto_demo
BIN_16_6 := gcc_idioms

# Liste des binaires mono-fichier utilisant les règles génériques.
# NOTE : BIN_16_3 est EXCLU car il a une variante spéciale _O3_avx2.
SINGLE_BINS := $(BIN_16_1) $(BIN_16_2) $(BIN_16_4) $(BIN_16_6)

# ============================================================================
# Génération des listes de cibles
# ============================================================================

# Tous les binaires mono-fichier (règles génériques) à tous les niveaux + strip
ALL_SINGLE := $(foreach bin,$(SINGLE_BINS),\
                $(foreach opt,$(OPT_LEVELS),$(OUT)/$(bin)_$(opt)) \
                $(OUT)/$(bin)_O2_strip)

# Section 16.3 — écrit à la main pour ajouter la variante _O3_avx2
ALL_16_3 := $(foreach opt,$(OPT_LEVELS),$(OUT)/$(BIN_16_3)_$(opt)) \
            $(OUT)/$(BIN_16_3)_O2_strip \
            $(OUT)/$(BIN_16_3)_O3_avx2

# Binaires LTO (section 16.5) — sans et avec -flto
ALL_LTO := $(foreach opt,$(OPT_LEVELS),$(OUT)/$(BIN_16_5)_$(opt)) \
           $(OUT)/$(BIN_16_5)_O2_strip \
           $(foreach opt,$(OPT_LEVELS),$(OUT)/$(BIN_16_5)_$(opt)_flto) \
           $(OUT)/$(BIN_16_5)_O2_flto_strip

# Section 16.7 — variantes Clang (optionnelles, voir cible s16_7)
ALL_CLANG := $(foreach src,$(SRCS_16_7),\
               $(OUT)/$(basename $(src))_clang_O0 \
               $(OUT)/$(basename $(src))_clang_O2)

# Tout sauf Clang (Clang est optionnel — ne bloque pas make all)
ALL_CORE := $(ALL_SINGLE) $(ALL_16_3) $(ALL_LTO)

# Tout y compris Clang
ALL := $(ALL_CORE) $(ALL_CLANG)

# ============================================================================
# Règles principales
# ============================================================================

.PHONY: all all_with_clang clean s16_1 s16_2 s16_3 s16_4 s16_5 s16_6 s16_7

all: $(ALL_CORE)
	@echo ""
	@echo "========================================"
	@echo " Binaires GCC du chapitre 16 compilés"
	@echo " dans $(OUT)/"
	@echo ""
	@echo " Pour les variantes Clang (section 16.7) :"
	@echo "   make s16_7"
	@echo "========================================"
	@echo ""
	@ls -lhS $(OUT)/

all_with_clang: $(ALL)
	@echo ""
	@echo "========================================"
	@echo " Tous les binaires (GCC + Clang)"
	@echo " compilés dans $(OUT)/"
	@echo "========================================"
	@echo ""
	@ls -lhS $(OUT)/

clean:
	rm -rf $(OUT)

$(OUT):
	mkdir -p $(OUT)

# ============================================================================
# Cibles par section (compilation sélective)
# ============================================================================

s16_1: $(foreach opt,$(OPT_LEVELS),$(OUT)/$(BIN_16_1)_$(opt)) \
       $(OUT)/$(BIN_16_1)_O2_strip

s16_2: $(foreach opt,$(OPT_LEVELS),$(OUT)/$(BIN_16_2)_$(opt)) \
       $(OUT)/$(BIN_16_2)_O2_strip

s16_3: $(ALL_16_3)

s16_4: $(foreach opt,$(OPT_LEVELS),$(OUT)/$(BIN_16_4)_$(opt)) \
       $(OUT)/$(BIN_16_4)_O2_strip

s16_5: $(ALL_LTO)

s16_6: $(foreach opt,$(OPT_LEVELS),$(OUT)/$(BIN_16_6)_$(opt)) \
       $(OUT)/$(BIN_16_6)_O2_strip

s16_7: $(ALL_CLANG)
	@echo ""
	@echo "=== Variantes Clang compilées ==="
	@ls -lh $(ALL_CLANG)

# ============================================================================
# Règles génériques — Binaires mono-fichier C
# ============================================================================

# Macro pour générer les 6 règles de compilation (O0/O1/O2/O3/Os + O2_strip)
# $(1) = nom de base du binaire, $(2) = fichier source
define SINGLE_C_RULES

$(OUT)/$(1)_O0: $(2) | $(OUT)
	$(CC) $(CFLAGS) -O0 -g -o $$@ $$<

$(OUT)/$(1)_O1: $(2) | $(OUT)
	$(CC) $(CFLAGS) -O1 -g -o $$@ $$<

$(OUT)/$(1)_O2: $(2) | $(OUT)
	$(CC) $(CFLAGS) -O2 -g -o $$@ $$<

$(OUT)/$(1)_O3: $(2) | $(OUT)
	$(CC) $(CFLAGS) -O3 -g -o $$@ $$<

$(OUT)/$(1)_Os: $(2) | $(OUT)
	$(CC) $(CFLAGS) -Os -g -o $$@ $$<

$(OUT)/$(1)_O2_strip: $(2) | $(OUT)
	$(CC) $(CFLAGS) -O2 -s -o $$@ $$<

endef

# Instanciation des règles pour les binaires mono-fichier génériques
$(eval $(call SINGLE_C_RULES,$(BIN_16_1),$(SRC_16_1)))
$(eval $(call SINGLE_C_RULES,$(BIN_16_2),$(SRC_16_2)))
$(eval $(call SINGLE_C_RULES,$(BIN_16_4),$(SRC_16_4)))
$(eval $(call SINGLE_C_RULES,$(BIN_16_6),$(SRC_16_6)))

# ============================================================================
# Règles spécifiques — Section 16.3 (loop_unroll_vec)
#
# Écrites manuellement pour ajouter la variante _O3_avx2 sans conflit
# de règle dupliquée avec le macro générique.
# ============================================================================

$(OUT)/$(BIN_16_3)_O0: $(SRC_16_3) | $(OUT)
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

$(OUT)/$(BIN_16_3)_O1: $(SRC_16_3) | $(OUT)
	$(CC) $(CFLAGS) -O1 -g -o $@ $<

$(OUT)/$(BIN_16_3)_O2: $(SRC_16_3) | $(OUT)
	$(CC) $(CFLAGS) -O2 -g -o $@ $<

$(OUT)/$(BIN_16_3)_O3: $(SRC_16_3) | $(OUT)
	$(CC) $(CFLAGS) -O3 -g -o $@ $<

$(OUT)/$(BIN_16_3)_Os: $(SRC_16_3) | $(OUT)
	$(CC) $(CFLAGS) -Os -g -o $@ $<

$(OUT)/$(BIN_16_3)_O2_strip: $(SRC_16_3) | $(OUT)
	$(CC) $(CFLAGS) -O2 -s -o $@ $<

# Variante spéciale : O3 + AVX2 (registres ymm 256 bits)
# Compare avec _O3 (SSE seulement) pour voir la différence de vectorisation.
$(OUT)/$(BIN_16_3)_O3_avx2: $(SRC_16_3) | $(OUT)
	$(CC) $(CFLAGS) -O3 -g -mavx2 -o $@ $<

# ============================================================================
# Règles spécifiques — Section 16.5 (LTO, multi-fichiers)
# ============================================================================

LTO_SRCS := $(SRC_16_5_MAIN) $(SRC_16_5_MATH) $(SRC_16_5_UTIL)
LTO_HDRS := lto_math.h lto_utils.h

# --- Sans LTO (compilation classique multi-fichiers) ---

$(OUT)/$(BIN_16_5)_O0: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O0 -g -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O1: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -g -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O2: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O2 -g -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O3: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O3 -g -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_Os: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -Os -g -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O2_strip: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O2 -s -o $@ $(LTO_SRCS) -lm

# --- Avec LTO (-flto) — le cœur de la section 16.5 ---

$(OUT)/$(BIN_16_5)_O0_flto: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O0 -g -flto -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O1_flto: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O1 -g -flto -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O2_flto: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O2 -g -flto -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O3_flto: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O3 -g -flto -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_Os_flto: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -Os -g -flto -o $@ $(LTO_SRCS) -lm

$(OUT)/$(BIN_16_5)_O2_flto_strip: $(LTO_SRCS) $(LTO_HDRS) | $(OUT)
	$(CC) $(CFLAGS) -O2 -s -flto -o $@ $(LTO_SRCS) -lm

# ============================================================================
# Règles spécifiques — Section 16.7 (GCC vs Clang)
#
# Compile les mêmes sources avec Clang pour comparer les patterns
# d'optimisation. Nécessite que clang soit installé.
# ============================================================================

# Macro pour les règles Clang
# $(1) = nom de base, $(2) = fichier source
define CLANG_RULES

$(OUT)/$(1)_clang_O0: $(2) | $(OUT)
	@command -v $(CLANG) >/dev/null 2>&1 || \
		{ echo "ERREUR: clang n'est pas installé. Installez-le pour la section 16.7."; exit 1; }
	$(CLANG) $(CFLAGS) -O0 -g -o $$@ $$<

$(OUT)/$(1)_clang_O2: $(2) | $(OUT)
	@command -v $(CLANG) >/dev/null 2>&1 || \
		{ echo "ERREUR: clang n'est pas installé. Installez-le pour la section 16.7."; exit 1; }
	$(CLANG) $(CFLAGS) -O2 -g -o $$@ $$<

endef

$(eval $(call CLANG_RULES,opt_levels_demo,opt_levels_demo.c))
$(eval $(call CLANG_RULES,gcc_idioms,gcc_idioms.c))

# ============================================================================
# Cibles utilitaires
# ============================================================================

.PHONY: list sizes disasm_compare lto_compare

# Lister tous les binaires attendus
list:
	@echo "=== Binaires GCC (make all) ==="
	@for f in $(sort $(ALL_CORE)); do echo "  $$f"; done
	@echo ""
	@echo "=== Binaires Clang (make s16_7) ==="
	@for f in $(sort $(ALL_CLANG)); do echo "  $$f"; done

# Comparer les tailles de tous les binaires compilés
sizes: all
	@echo ""
	@echo "=== Comparaison des tailles ==="
	@echo ""
	@printf "%-45s %10s\n" "Binaire" "Taille"
	@printf "%-45s %10s\n" "---------------------------------------------" "----------"
	@for f in $(sort $(ALL_CORE)); do \
		if [ -f "$$f" ]; then \
			printf "%-45s %10s\n" "$$(basename $$f)" \
				"$$(stat -c%s "$$f" 2>/dev/null || stat -f%z "$$f" 2>/dev/null)"; \
		fi; \
	done

# Comparer le désassemblage de main() entre O0 et O2 pour un binaire donné
# Usage : make disasm_compare BIN=opt_levels_demo
disasm_compare:
ifndef BIN
	@echo "Usage : make disasm_compare BIN=<nom_de_base>"
	@echo ""
	@echo "Exemples :"
	@echo "  make disasm_compare BIN=opt_levels_demo"
	@echo "  make disasm_compare BIN=inlining_demo"
	@echo "  make disasm_compare BIN=gcc_idioms"
	@echo "  make disasm_compare BIN=tail_call"
else
	@echo "=== $(BIN) en -O0 ==="
	@echo ""
	@objdump -d -M intel $(OUT)/$(BIN)_O0 | sed -n '/<main>:/,/^$$/p' | head -60
	@echo ""
	@echo "=== $(BIN) en -O2 ==="
	@echo ""
	@objdump -d -M intel $(OUT)/$(BIN)_O2 | sed -n '/<main>:/,/^$$/p' | head -60
endif

# Comparer les symboles avec et sans LTO
# Montre la disparition des fonctions inlinées cross-module
lto_compare: s16_5
	@echo ""
	@echo "=== Fonctions (T = text) SANS LTO (-O2) ==="
	@nm $(OUT)/$(BIN_16_5)_O2 2>/dev/null | grep ' T ' | sort
	@echo ""
	@echo "=== Fonctions (T = text) AVEC LTO (-O2 -flto) ==="
	@nm $(OUT)/$(BIN_16_5)_O2_flto 2>/dev/null | grep ' T ' | sort
	@echo ""
	@echo "=== Différence de taille ==="
	@printf "  Sans LTO : %s octets\n" \
		"$$(stat -c%s $(OUT)/$(BIN_16_5)_O2 2>/dev/null || stat -f%z $(OUT)/$(BIN_16_5)_O2 2>/dev/null)"
	@printf "  Avec LTO : %s octets\n" \
		"$$(stat -c%s $(OUT)/$(BIN_16_5)_O2_flto 2>/dev/null || stat -f%z $(OUT)/$(BIN_16_5)_O2_flto 2>/dev/null)"
