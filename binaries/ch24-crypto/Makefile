# ============================================================================
# Makefile — Chapitre 24 : Reverse d'un binaire avec chiffrement
# Formation Reverse Engineering — Applications compilées avec la chaîne GNU
#
# Cibles :
#   crypto_O0          Pas d'optimisation, symboles de debug
#   crypto_O2          Optimisé -O2, symboles de debug
#   crypto_O3          Optimisé -O3, symboles de debug
#   crypto_O2_strip    Optimisé -O2, strippé (pas de symboles)
#   crypto_static      Lié statiquement à libcrypto (pour exercice FLIRT)
#   secret.txt.enc     Fichier chiffré généré à partir de secret.txt
#
# Prérequis : gcc, libssl-dev (ou libssl-devel)
#   Ubuntu/Debian : sudo apt install libssl-dev
#   Fedora/RHEL   : sudo dnf install openssl-devel
# ============================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra
LDFLAGS  = -lcrypto
SRC      = crypto.c
TARGETS  = crypto_O0 crypto_O2 crypto_O3 crypto_O2_strip crypto_static

.PHONY: all clean test generate_plaintext

# --- Cible par défaut : tout compiler + générer secret.enc ----------------
all: $(TARGETS) secret.txt.enc secret.enc

# --- Variantes de compilation ---------------------------------------------

crypto_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (debug, no optimization)"

crypto_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (optimized -O2, with symbols)"

crypto_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -g -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (optimized -O3, with symbols)"

crypto_O2_strip: crypto_O2
	cp crypto_O2 $@
	strip $@
	@echo "[OK] $@ (optimized -O2, stripped)"

crypto_static: $(SRC)
	$(CC) $(CFLAGS) -O2 -static -o $@ $< $(LDFLAGS)
	@echo "[OK] $@ (static link — for FLIRT/signature exercises)"

# --- Fichier plaintext de test --------------------------------------------

secret.txt:
	@echo "=== FICHIER CONFIDENTIEL ===" > $@
	@echo "" >> $@
	@echo "Projet : Operation Midnight Sun" >> $@
	@echo "Classification : TOP SECRET" >> $@
	@echo "" >> $@
	@echo "Points de rendez-vous :" >> $@
	@echo "  - Alpha : 48.8566 N, 2.3522 E (Paris)" >> $@
	@echo "  - Bravo : 51.5074 N, 0.1278 W (London)" >> $@
	@echo "  - Charlie : 40.7128 N, 74.0060 W (New York)" >> $@
	@echo "" >> $@
	@echo "Code d'activation : SIERRA-TANGO-4729-FOXTROT" >> $@
	@echo "" >> $@
	@echo "Ce fichier s'autodetruira dans 5 secondes..." >> $@
	@echo "(non, pas vraiment, c'est juste un exercice)" >> $@
	@echo "[OK] secret.txt generated"

# --- Génération du fichier chiffré ----------------------------------------

secret.txt.enc: crypto_O0 secret.txt
	./crypto_O0 secret.txt
	@echo "[OK] secret.txt.enc generated (encrypted with crypto_O0)"

# Alias court pour les sections .md qui référencent "secret.enc"
secret.enc: secret.txt.enc
	cp secret.txt.enc secret.enc
	@echo "[OK] secret.enc (copy of secret.txt.enc)"

# --- Test rapide : vérifier que tous les binaires fonctionnent ------------

test: all
	@echo ""
	@echo "=== Vérification des binaires ==="
	@file crypto_O0 crypto_O2 crypto_O3 crypto_O2_strip crypto_static
	@echo ""
	@echo "=== Symboles dans crypto_O0 vs crypto_O2_strip ==="
	@echo "crypto_O0:       $$(nm crypto_O0 2>/dev/null | wc -l) symbols"
	@echo "crypto_O2_strip: $$(nm crypto_O2_strip 2>/dev/null | wc -l) symbols"
	@echo ""
	@echo "=== Dépendances dynamiques ==="
	@ldd crypto_O0 2>/dev/null || true
	@echo ""
	@echo "=== crypto_static (pas de dépendances dynamiques) ==="
	@ldd crypto_static 2>/dev/null || echo "(statically linked)"
	@echo ""
	@echo "=== Tailles des binaires ==="
	@ls -lh crypto_O0 crypto_O2 crypto_O3 crypto_O2_strip crypto_static
	@echo ""
	@echo "=== Vérification de secret.txt.enc ==="
	@xxd secret.txt.enc | head -4
	@echo "[OK] All checks passed."

# --- Nettoyage ------------------------------------------------------------

clean:
	rm -f $(TARGETS) secret.txt secret.txt.enc secret.enc
	@echo "[OK] Cleaned."
