# ═══════════════════════════════════════════════════════════════
#  Makefile — ch14-crypto
#  Produit le binaire de chiffrement à différents niveaux
#  d'optimisation, avec et sans symboles, pour l'analyse RE.
#
#  Cibles principales :
#    make all       — compile toutes les variantes
#    make asan      — compile avec ASan + UBSan (chapitre 14)
#    make msan      — compile avec MSan via Clang (chapitre 14)
#    make clean     — nettoie tous les artefacts
#
#  Les variantes strippées (-s) n'ont aucun symbole : c'est sur
#  celles-ci que l'étudiant pratiquera le RE « en conditions réelles ».
# ═══════════════════════════════════════════════════════════════

CC      = gcc
CLANG   = clang
SRC     = crypto.c
WARN    = -Wall -Wextra

# ── Cibles standard ──────────────────────────────────────────────

TARGETS = crypto_O0 crypto_O2 crypto_O3 crypto_strip crypto_O2_strip

.PHONY: all asan msan clean

all: $(TARGETS)

# Avec symboles de débogage (-g) — pour Valgrind, GDB, Callgrind
crypto_O0: $(SRC)
	$(CC) $(WARN) -O0 -g -o $@ $<

crypto_O2: $(SRC)
	$(CC) $(WARN) -O2 -g -o $@ $<

crypto_O3: $(SRC)
	$(CC) $(WARN) -O3 -g -o $@ $<

# Sans symboles (-s) — binaires « réalistes » pour le RE
crypto_strip: $(SRC)
	$(CC) $(WARN) -O0 -s -o $@ $<

crypto_O2_strip: $(SRC)
	$(CC) $(WARN) -O2 -s -o $@ $<

# ── Cibles sanitizers (chapitre 14) ──────────────────────────────

# ASan + UBSan — détection d'erreurs mémoire et comportements indéfinis
asan: $(SRC)
	$(CC) $(WARN) -O0 -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined \
		-o crypto_asan $<
	@echo "→ Compilé: crypto_asan (ASan + UBSan)"
	@echo "  Lancer avec:"
	@echo "  ASAN_OPTIONS=\"halt_on_error=0:detect_leaks=1\" \\"
	@echo "  UBSAN_OPTIONS=\"print_stacktrace=1:halt_on_error=0\" \\"
	@echo "    ./crypto_asan encrypt input.txt output.enc password"

# MSan — détection de lectures non initialisées (Clang uniquement)
msan: $(SRC)
	$(CLANG) $(WARN) -O0 -g -fno-omit-frame-pointer \
		-fsanitize=memory -fsanitize-memory-track-origins=2 \
		-o crypto_msan $<
	@echo "→ Compilé: crypto_msan (MSan, Clang)"
	@echo "  Lancer avec:"
	@echo "  MSAN_OPTIONS=\"halt_on_error=0\" \\"
	@echo "    ./crypto_msan encrypt input.txt output.enc password"

# ── Nettoyage ────────────────────────────────────────────────────

clean:
	rm -f $(TARGETS) crypto_asan crypto_msan
	rm -f *.out *.enc *.txt.dec
	rm -f callgrind.out.* asan.* msan.*
