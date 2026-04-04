# ═══════════════════════════════════════════════════════════════
#  Makefile — binaries/ch15-fileformat/
#
#  Parseur de format custom CSTM pour le chapitre 15 .
#  Produit des variantes à différents niveaux d'optimisation,
#  avec et sans symboles, et des builds instrumentés pour le fuzzing.
#
#  Usage :
#    make              — compile toutes les variantes standard
#    make fuzz         — compile les variantes AFL++ et libFuzzer
#    make coverage     — compile le build gcov pour la couverture
#    make all          — compile tout (standard + fuzz + coverage)
#    make clean        — supprime tous les binaires et artefacts
# ═══════════════════════════════════════════════════════════════

# ── Compilateurs ──
CC       ?= gcc
AFL_CC   ?= afl-gcc
CLANG    ?= clang

# ── Source ──
SRC      = fileformat.c
HARNESS  = fuzz_fileformat.c

# ── Flags communs ──
CFLAGS_BASE   = -Wall -Wextra
LDFLAGS       =

# ═════════════════════════
#  Cibles standard (GCC)
# ═════════════════════════

TARGETS_STD = fileformat_O0 fileformat_O2 fileformat_O3 \
              fileformat_O0_strip fileformat_O2_strip

.PHONY: standard fuzz coverage all clean help

standard: $(TARGETS_STD)

fileformat_O0: $(SRC)
	$(CC) $(CFLAGS_BASE) -O0 -g -o $@ $< $(LDFLAGS)

fileformat_O2: $(SRC)
	$(CC) $(CFLAGS_BASE) -O2 -g -o $@ $< $(LDFLAGS)

fileformat_O3: $(SRC)
	$(CC) $(CFLAGS_BASE) -O3 -g -o $@ $< $(LDFLAGS)

fileformat_O0_strip: fileformat_O0
	cp $< $@
	strip $@

fileformat_O2_strip: fileformat_O2
	cp $< $@
	strip $@

# ═════════════════════════════════
#  Cibles fuzzing (AFL++ & Clang)
# ═════════════════════════════════

TARGETS_FUZZ = fileformat_afl fileformat_afl_asan

fuzz: $(TARGETS_FUZZ)

# AFL++ — instrumentation standard
fileformat_afl: $(SRC)
	$(AFL_CC) $(CFLAGS_BASE) -O0 -g -o $@ $< $(LDFLAGS)

# AFL++ — instrumentation + AddressSanitizer
fileformat_afl_asan: $(SRC)
	AFL_USE_ASAN=1 $(AFL_CC) $(CFLAGS_BASE) -O0 -g -o $@ $< $(LDFLAGS)

# libFuzzer (nécessite Clang et le harness fuzz_fileformat.c)
# -DFUZZ_TARGET exclut main() de fileformat.c et rend les fonctions non-static
fuzz_fileformat: $(SRC) $(HARNESS)
	$(CLANG) -DFUZZ_TARGET -fsanitize=fuzzer,address,undefined -g -O1 \
		$(CFLAGS_BASE) -o $@ $(HARNESS) $(SRC)

# ═══════════════════════════════
#  Cible couverture (gcov/lcov)
# ═══════════════════════════════

coverage: fileformat_gcov

fileformat_gcov: $(SRC)
	$(CC) --coverage $(CFLAGS_BASE) -O0 -g -o $@ $< $(LDFLAGS)

# ═════════════════
#  Cible globale
# ═════════════════

all: standard fuzz coverage
	@echo ""
	@echo "=== Tous les binaires compilés ==="
	@ls -la fileformat_* fuzz_fileformat 2>/dev/null || true

# ═════════════════
#  Nettoyage
# ═════════════════

clean:
	rm -f fileformat_O0 fileformat_O2 fileformat_O3
	rm -f fileformat_O0_strip fileformat_O2_strip
	rm -f fileformat_afl fileformat_afl_asan
	rm -f fileformat_gcov
	rm -f fuzz_fileformat
	rm -f *.o *.gcno *.gcda *.gcov *.info
	rm -rf cov_report/ cov_html/

# ═════════════════
#  Aide
# ═════════════════

help:
	@echo "Cibles disponibles :"
	@echo "  make              Variantes standard (O0, O2, O3, strip)"
	@echo "  make fuzz         Builds AFL++ (afl, afl+asan)"
	@echo "  make coverage     Build gcov pour lcov/genhtml"
	@echo "  make all          Tout compiler"
	@echo "  make clean        Supprimer les artefacts"
	@echo ""
	@echo "Variables surchargeables :"
	@echo "  CC=gcc            Compilateur C (défaut: gcc)"
	@echo "  AFL_CC=afl-gcc    Compilateur AFL++ (défaut: afl-gcc)"
	@echo "  CLANG=clang       Compilateur Clang pour libFuzzer"
