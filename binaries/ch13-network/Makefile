# ═══════════════════════════════════════════════════════════════
#  ch13-network — Makefile
#  Formation Reverse Engineering — Chapitre 13
#
# Compile le client et le serveur GCRP à différents niveaux
# d'optimisation, avec et sans symboles.
#
# Cibles :
#   make all       — compile toutes les variantes
#   make debug     — compile uniquement les variantes -O0 -g
#   make clean     — supprime tous les binaires générés
#
# Variantes produites pour chaque binaire :
#   *_O0          -O0 -g          (sans optimisations, avec symboles DWARF)
#   *_O2          -O2 -g          (optimisé, avec symboles)
#   *_O3          -O3 -g          (optimisations agressives, avec symboles)
#   *_strip       -O0 -s          (sans optimisations, strippé)
#   *_O2_strip    -O2 -s          (optimisé + strippé)

# ═══════════════════════════════════════════════════════════════

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11
LDFLAGS :=

# Fichiers sources
SERVER_SRC := server.c
CLIENT_SRC := client.c
HEADER     := protocol.h

# ═══════════════════════════════════════════════
# Liste des cibles
# ═══════════════════════════════════════════════

SERVER_BINS := server_O0 server_O2 server_O3 server_strip server_O2_strip
CLIENT_BINS := client_O0 client_O2 client_O3 client_strip client_O2_strip

ALL_BINS := $(SERVER_BINS) $(CLIENT_BINS)

# ═══════════════════════════════════════════════
# Cibles principales
# ═══════════════════════════════════════════════

.PHONY: all debug clean

all: $(ALL_BINS)
	@echo ""
	@echo "✅ Toutes les variantes compilées :"
	@ls -lh server_O0 server_O2 server_strip client_O0 client_O2 client_strip
	@echo ""

debug: server_O0 client_O0
	@echo "✅ Variantes debug compilées : server_O0, client_O0"

clean:
	rm -f $(ALL_BINS)
	@echo "🧹 Nettoyé."

# ═══════════════════════════════════════════════
# Serveur — variantes
# ═══════════════════════════════════════════════

server_O0: $(SERVER_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O0 -g -o $@ $(SERVER_SRC) $(LDFLAGS)

server_O2: $(SERVER_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O2 -g -o $@ $(SERVER_SRC) $(LDFLAGS)

server_O3: $(SERVER_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O3 -g -o $@ $(SERVER_SRC) $(LDFLAGS)

server_strip: $(SERVER_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O0 -s -o $@ $(SERVER_SRC) $(LDFLAGS)

server_O2_strip: $(SERVER_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O2 -s -o $@ $(SERVER_SRC) $(LDFLAGS)

# ═══════════════════════════════════════════════
# Client — variantes
# ═══════════════════════════════════════════════

client_O0: $(CLIENT_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O0 -g -o $@ $(CLIENT_SRC) $(LDFLAGS)

client_O2: $(CLIENT_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O2 -g -o $@ $(CLIENT_SRC) $(LDFLAGS)

client_O3: $(CLIENT_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O3 -g -o $@ $(CLIENT_SRC) $(LDFLAGS)

client_strip: $(CLIENT_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O0 -s -o $@ $(CLIENT_SRC) $(LDFLAGS)

client_O2_strip: $(CLIENT_SRC) $(HEADER)
	$(CC) $(CFLAGS) -O2 -s -o $@ $(CLIENT_SRC) $(LDFLAGS)
