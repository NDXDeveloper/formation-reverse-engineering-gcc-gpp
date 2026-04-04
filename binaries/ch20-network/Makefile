# ═══════════════════════════════════════════════════════════════
#  ch20-network — Makefile
#  Formation Reverse Engineering — Chapitre 20
#
#  Produit les binaires client et serveur à plusieurs niveaux
#  d'optimisation, avec et sans symboles de débogage.
#
#  Usage :
#    make              — compile toutes les variantes
#    make clean        — supprime les binaires
# ═══════════════════════════════════════════════════════════════

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11
SRC_SRV = server.c
SRC_CLI = client.c
BIN_DIR = .

TARGETS = server_O0        \
          server_O0_dbg     \
          server_O2         \
          server_O2_strip   \
          client_O0         \
          client_O0_dbg     \
          client_O2         \
          client_O2_strip
# Note : les variantes *_strip sont compilées avec -no-pie
# pour des adresses fixes (0x400000+), plus adaptées à l'exercice de RE.

.PHONY: all clean

all: $(TARGETS)

# --- Serveur ---

server_O0: $(SRC_SRV) protocol.h
	$(CC) $(CFLAGS) -O0 -o $(BIN_DIR)/$@ $<

server_O0_dbg: $(SRC_SRV) protocol.h
	$(CC) $(CFLAGS) -O0 -g -o $(BIN_DIR)/$@ $<

server_O2: $(SRC_SRV) protocol.h
	$(CC) $(CFLAGS) -O2 -o $(BIN_DIR)/$@ $<

server_O2_strip: $(SRC_SRV) protocol.h
	$(CC) $(CFLAGS) -O2 -s -no-pie -o $(BIN_DIR)/$@ $<

# --- Client ---

client_O0: $(SRC_CLI) protocol.h
	$(CC) $(CFLAGS) -O0 -o $(BIN_DIR)/$@ $<

client_O0_dbg: $(SRC_CLI) protocol.h
	$(CC) $(CFLAGS) -O0 -g -o $(BIN_DIR)/$@ $<

client_O2: $(SRC_CLI) protocol.h
	$(CC) $(CFLAGS) -O2 -o $(BIN_DIR)/$@ $<

client_O2_strip: $(SRC_CLI) protocol.h
	$(CC) $(CFLAGS) -O2 -s -no-pie -o $(BIN_DIR)/$@ $<

clean:
	rm -f $(TARGETS)

# --- END Makefile 
