# ═══════════════════════════════════════════════════════════════
#  Makefile — binaries/ch15-keygenme/
#
#  Keygenme d'entraînement pour le chapitre 15 .
#  Produit des variantes à différents niveaux d'optimisation,
#  avec et sans symboles, et des builds instrumentés pour le fuzzing.
#
#  Usage :
#    make              — compile toutes les variantes standard
#    make fuzz         — compile les variantes AFL++
#    make all          — compile tout (standard + fuzz)
#    make clean        — supprime tous les binaires et artefacts
#    make test         — lance un test rapide avec une clé valide
# ═══════════════════════════════════════════════════════════════

# ── Compilateurs ──
CC       ?= gcc
AFL_CC   ?= afl-gcc

# ── Source ──
SRC      = keygenme.c

# ── Flags communs ──
CFLAGS_BASE = -Wall -Wextra

# ═════════════════════════
#  Cibles standard (GCC)
# ═════════════════════════

TARGETS_STD = keygenme_O0 keygenme_O2 keygenme_O3 \
              keygenme_O0_strip keygenme_O2_strip

.PHONY: standard fuzz all clean help test

standard: $(TARGETS_STD)

keygenme_O0: $(SRC)
	$(CC) $(CFLAGS_BASE) -O0 -g -o $@ $<

keygenme_O2: $(SRC)
	$(CC) $(CFLAGS_BASE) -O2 -g -o $@ $<

keygenme_O3: $(SRC)
	$(CC) $(CFLAGS_BASE) -O3 -g -o $@ $<

keygenme_O0_strip: keygenme_O0
	cp $< $@
	strip $@

keygenme_O2_strip: keygenme_O2
	cp $< $@
	strip $@

# ══════════════════════════════
#  Cibles fuzzing (AFL++)
# ══════════════════════════════

TARGETS_FUZZ = keygenme_afl keygenme_afl_asan

fuzz: $(TARGETS_FUZZ)

# AFL++ — instrumentation standard
keygenme_afl: $(SRC)
	$(AFL_CC) $(CFLAGS_BASE) -O0 -g -o $@ $<

# AFL++ — instrumentation + AddressSanitizer
keygenme_afl_asan: $(SRC)
	AFL_USE_ASAN=1 $(AFL_CC) $(CFLAGS_BASE) -O0 -g -o $@ $<

# ═════════════════
#  Cible globale
# ═════════════════

all: standard fuzz
	@echo ""
	@echo "=== Tous les binaires compilés ==="
	@ls -la keygenme_* 2>/dev/null || true

# ═════════════════
#  Test rapide
# ═════════════════

# Clé valide calculée :
#   g1 = 0x1234
#   g2 = g1 XOR 0xBEEF = 0x1234 XOR 0xBEEF = 0xACDB
#   g3 = (g1 + g2) & 0xFFFF = (0x1234 + 0xACDB) & 0xFFFF = 0xBF0F
#   → RENG-1234-ACDB-BF0F

TEST_KEY = RENG-1234-ACDB-BF0F

test: keygenme_O0
	@echo "=== Test avec clé valide : $(TEST_KEY) ==="
	@./keygenme_O0 "$(TEST_KEY)" && echo "[PASS]" || echo "[FAIL]"
	@echo ""
	@echo "=== Test avec clé invalide : RENG-0000-0000-0000 ==="
	@./keygenme_O0 "RENG-0000-0000-0000" && echo "[UNEXPECTED PASS]" || echo "[PASS — rejeté comme attendu]"
	@echo ""
	@echo "=== Test avec entrée trop courte : AAAA ==="
	@./keygenme_O0 "AAAA" && echo "[UNEXPECTED PASS]" || echo "[PASS — rejeté comme attendu]"

# ═════════════════
#  Nettoyage
# ═════════════════

clean:
	rm -f keygenme_O0 keygenme_O2 keygenme_O3
	rm -f keygenme_O0_strip keygenme_O2_strip
	rm -f keygenme_afl keygenme_afl_asan
	rm -f *.o

# ═════════════════
#  Aide
# ═════════════════

help:
	@echo "Cibles disponibles :"
	@echo "  make              Variantes standard (O0, O2, O3, strip)"
	@echo "  make fuzz         Builds AFL++ (afl, afl+asan)"
	@echo "  make all          Tout compiler"
	@echo "  make test         Lancer les tests avec clés valide/invalide"
	@echo "  make clean        Supprimer les artefacts"
	@echo ""
	@echo "Variables surchargeables :"
	@echo "  CC=gcc            Compilateur C (défaut: gcc)"
	@echo "  AFL_CC=afl-gcc    Compilateur AFL++ (défaut: afl-gcc)"
	@echo ""
	@echo "Format de clé : RENG-XXXX-XXXX-XXXX"
	@echo "  Groupe 1 XOR Groupe 2 = 0xBEEF"
	@echo "  Groupe 3 = (Groupe 1 + Groupe 2) & 0xFFFF"
	@echo "  Groupe 1 != 0x0000"
