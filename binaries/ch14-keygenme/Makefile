# ═══════════════════════════════════════════════════════════════
#  Makefile — ch14-keygenme
#  Crackme pour les chapitres 14 (profiling Callgrind)
#  et 18 (résolution angr).
#
#  Cibles :
#    make all       — toutes les variantes
#    make asan      — ASan + UBSan (chapitre 14)
#    make msan      — MSan / Clang (chapitre 14)
#    make clean     — nettoyage
# ═══════════════════════════════════════════════════════════════

CC      = gcc
CLANG   = clang
SRC     = keygenme.c
WARN    = -Wall -Wextra

TARGETS = keygenme_O0 keygenme_O2 keygenme_O3 keygenme_strip keygenme_O2_strip

.PHONY: all asan msan clean

all: $(TARGETS)

# ── Avec symboles ────────────────────────────────────────────────

keygenme_O0: $(SRC)
	$(CC) $(WARN) -O0 -g -o $@ $<

keygenme_O2: $(SRC)
	$(CC) $(WARN) -O2 -g -o $@ $<

keygenme_O3: $(SRC)
	$(CC) $(WARN) -O3 -g -o $@ $<

# ── Sans symboles (RE « conditions réelles ») ────────────────────

keygenme_strip: $(SRC)
	$(CC) $(WARN) -O0 -s -o $@ $<

keygenme_O2_strip: $(SRC)
	$(CC) $(WARN) -O2 -s -o $@ $<

# ── Sanitizers (chapitre 14) ─────────────────────────────────────

asan: $(SRC)
	$(CC) $(WARN) -O0 -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined \
		-o keygenme_asan $<
	@echo "→ keygenme_asan (ASan + UBSan)"
	@echo "  ./keygenme_asan ABCD-1234-EFGH-5678"

msan: $(SRC)
	$(CLANG) $(WARN) -O0 -g -fno-omit-frame-pointer \
		-fsanitize=memory -fsanitize-memory-track-origins=2 \
		-o keygenme_msan $<
	@echo "→ keygenme_msan (MSan, Clang)"

# ── Nettoyage ────────────────────────────────────────────────────

clean:
	rm -f $(TARGETS) keygenme_asan keygenme_msan
	rm -f callgrind.out.* asan.* msan.*
