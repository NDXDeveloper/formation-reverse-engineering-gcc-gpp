# ═══════════════════════════════════════════════════════════════
#  Makefile — ch14-fileformat
#  Parseur de format custom .cdat pour le chapitre 14 (sanitizers)
#
#  Cibles :
#    make all       — toutes les variantes
#    make asan      — ASan + UBSan (chapitre 14 )
#    make msan      — MSan / Clang (chapitre 14)
#    make afl       — instrumentation AFL++ 
#    make sample    — crée un fichier .cdat de test
#    make clean     — nettoyage
# ═══════════════════════════════════════════════════════════════

CC      = gcc
CLANG   = clang
AFL_CC  = afl-gcc
SRC     = fileformat.c
WARN    = -Wall -Wextra

TARGETS = fileformat_O0 fileformat_O2 fileformat_O3 \
          fileformat_strip fileformat_O2_strip

.PHONY: all asan msan afl sample clean

all: $(TARGETS)

# ── Avec symboles ────────────────────────────────────────────────

fileformat_O0: $(SRC)
	$(CC) $(WARN) -O0 -g -o $@ $<

fileformat_O2: $(SRC)
	$(CC) $(WARN) -O2 -g -o $@ $<

fileformat_O3: $(SRC)
	$(CC) $(WARN) -O3 -g -o $@ $<

# ── Sans symboles ────────────────────────────────────────────────

fileformat_strip: $(SRC)
	$(CC) $(WARN) -O0 -s -o $@ $<

fileformat_O2_strip: $(SRC)
	$(CC) $(WARN) -O2 -s -o $@ $<

# ── Sanitizers (chapitre 14) ─────────────────────────────────────

asan: $(SRC)
	$(CC) $(WARN) -O0 -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined \
		-o fileformat_asan $<
	@echo "→ fileformat_asan (ASan + UBSan)"
	@echo "  Créer un sample d'abord : ./fileformat_asan create test.cdat"
	@echo "  Puis parser :            ./fileformat_asan parse test.cdat"

msan: $(SRC)
	$(CLANG) $(WARN) -O0 -g -fno-omit-frame-pointer \
		-fsanitize=memory -fsanitize-memory-track-origins=2 \
		-o fileformat_msan $<
	@echo "→ fileformat_msan (MSan, Clang)"

# ── AFL++  ─────────────────────────────────────────

afl: $(SRC)
	$(AFL_CC) $(WARN) -O2 -g \
		-fsanitize=address,undefined \
		-o fileformat_afl $<
	@echo "→ fileformat_afl (AFL++ instrumented + ASan)"
	@echo "  mkdir -p afl_in afl_out"
	@echo "  ./fileformat_O0 create afl_in/seed.cdat"
	@echo "  afl-fuzz -i afl_in -o afl_out -- ./fileformat_afl parse @@"

# ── Fichier de test ──────────────────────────────────────────────

sample: fileformat_O0
	./fileformat_O0 create sample.cdat
	@echo "→ sample.cdat créé (pour ImHex, Valgrind, fuzzing…)"

# ── Nettoyage ────────────────────────────────────────────────────

clean:
	rm -f $(TARGETS) fileformat_asan fileformat_msan fileformat_afl
	rm -f *.cdat
	rm -f callgrind.out.* asan.* msan.*
	rm -rf afl_in afl_out
