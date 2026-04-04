# ═══════════════════════════════════════════════════════════════
#  ch23-network — Makefile
#  Formation Reverse Engineering — Chapitre 23
#
#  Produit les binaires client et serveur à plusieurs niveaux
#  d'optimisation, avec et sans symboles de débogage.
#
#  Usage :
#    make              — compile toutes les variantes
#    make clean        — supprime les binaires
#    make run-server   — lance le serveur (variante -O0 -g)
#    make run-client   — lance le client  (variante -O0 -g)
#    make test         — lance serveur + client automatiquement
# ═══════════════════════════════════════════════════════════════

CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11
LDFLAGS  =

# Répertoire de sortie
OUTDIR   = build

# ───────────────────────────────────────────
#  Liste des cibles
# ───────────────────────────────────────────

SERVER_TARGETS = \
	$(OUTDIR)/server_O0        \
	$(OUTDIR)/server_O2        \
	$(OUTDIR)/server_O3        \
	$(OUTDIR)/server_Os        \
	$(OUTDIR)/server_strip     \
	$(OUTDIR)/server_O2_strip

CLIENT_TARGETS = \
	$(OUTDIR)/client_O0        \
	$(OUTDIR)/client_O2        \
	$(OUTDIR)/client_O3        \
	$(OUTDIR)/client_Os        \
	$(OUTDIR)/client_strip     \
	$(OUTDIR)/client_O2_strip

ALL_TARGETS = $(SERVER_TARGETS) $(CLIENT_TARGETS)

# ───────────────────────────────────────────
#  Cibles principales
# ───────────────────────────────────────────

.PHONY: all clean run-server run-client test help

all: $(OUTDIR) $(ALL_TARGETS)
	@echo ""
	@echo "══════════════════════════════════════════════"
	@echo "  Build complete. Binaries in $(OUTDIR)/"
	@echo "══════════════════════════════════════════════"
	@echo ""
	@ls -lh $(OUTDIR)/
	@echo ""

$(OUTDIR):
	mkdir -p $(OUTDIR)

clean:
	rm -rf $(OUTDIR)

help:
	@echo "Cibles disponibles :"
	@echo "  all          — compile toutes les variantes"
	@echo "  clean        — supprime le répertoire build/"
	@echo "  run-server   — lance server_O0 sur le port 4444"
	@echo "  run-client   — lance client_O0 vers 127.0.0.1:4444"
	@echo "  test         — lance serveur + client automatiquement"
	@echo ""
	@echo "Variantes produites pour chaque binaire :"
	@echo "  _O0         — -O0 -g  (debug, non optimisé)"
	@echo "  _O2         — -O2 -g  (optimisé, avec symboles)"
	@echo "  _O3         — -O3 -g  (optimisé agressif, avec symboles)"
	@echo "  _Os         — -Os -g  (optimisé en taille, avec symboles)"
	@echo "  _strip      — -O0, strippé (pas de symboles)"
	@echo "  _O2_strip   — -O2, strippé (optimisé + pas de symboles)"

# ───────────────────────────────────────────
#  Serveur — variantes
# ───────────────────────────────────────────

$(OUTDIR)/server_O0: server.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O0 -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/server_O2: server.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O2 -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/server_O3: server.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O3 -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/server_Os: server.c | $(OUTDIR)
	$(CC) $(CFLAGS) -Os -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/server_strip: server.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O0 -o $@ $< $(LDFLAGS)
	strip $@

$(OUTDIR)/server_O2_strip: server.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O2 -o $@ $< $(LDFLAGS)
	strip $@

# ───────────────────────────────────────────
#  Client — variantes
# ───────────────────────────────────────────

$(OUTDIR)/client_O0: client.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O0 -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/client_O2: client.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O2 -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/client_O3: client.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O3 -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/client_Os: client.c | $(OUTDIR)
	$(CC) $(CFLAGS) -Os -g -o $@ $< $(LDFLAGS)

$(OUTDIR)/client_strip: client.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O0 -o $@ $< $(LDFLAGS)
	strip $@

$(OUTDIR)/client_O2_strip: client.c | $(OUTDIR)
	$(CC) $(CFLAGS) -O2 -o $@ $< $(LDFLAGS)
	strip $@

# ───────────────────────────────────────────
#  Cibles utilitaires
# ───────────────────────────────────────────

run-server: $(OUTDIR)/server_O0
	@echo "[*] Starting server on port 4444..."
	./$(OUTDIR)/server_O0

run-client: $(OUTDIR)/client_O0
	@echo "[*] Connecting client to 127.0.0.1:4444..."
	./$(OUTDIR)/client_O0 127.0.0.1

# Lance le serveur en arrière-plan, exécute le client, puis tue le serveur
test: $(OUTDIR)/server_O0 $(OUTDIR)/client_O0
	@echo "══════════════════════════════════════════════"
	@echo "  Test automatique : serveur + client"
	@echo "══════════════════════════════════════════════"
	@echo ""
	@echo "[*] Starting server in background..."
	@./$(OUTDIR)/server_O0 &                                      \
	 SERVER_PID=$$!;                                               \
	 sleep 0.5;                                                    \
	 echo "[*] Running client...";                                 \
	 echo "";                                                      \
	 ./$(OUTDIR)/client_O0 127.0.0.1;                              \
	 CLIENT_RC=$$?;                                                \
	 echo "";                                                      \
	 echo "[*] Stopping server (PID $$SERVER_PID)...";             \
	 kill $$SERVER_PID 2>/dev/null;                                \
	 wait $$SERVER_PID 2>/dev/null;                                \
	 echo "";                                                      \
	 if [ $$CLIENT_RC -eq 0 ]; then                                \
	     echo "✅  Test PASSED — session completed successfully";  \
	 else                                                          \
	     echo "❌  Test FAILED — client exited with error";        \
	 fi
