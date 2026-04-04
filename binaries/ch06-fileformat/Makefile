# ============================================================
# Makefile — binaries/ch06-fileformat/
# Formation Reverse Engineering — Chapitre 6
#
# Produit le binaire fileformat à différents niveaux
# d'optimisation, avec et sans symboles, puis génère
# les fichiers de données sample.cdb et sample.pkt.
#
# Cibles principales :
#   make all       — compile tout + génère les samples
#   make binaries  — compile uniquement les binaires
#   make samples   — génère les fichiers .cdb et .pkt
#   make clean     — supprime les binaires et les samples
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic
SRC     = fileformat.c

# --- Cibles binaires ---

TARGETS = fileformat_O0 \
          fileformat_O0_strip \
          fileformat_O2 \
          fileformat_O2_strip \
          fileformat_O3

# --- Fichiers de données générés ---

SAMPLES = sample.cdb sample.pkt

# ============================================================
#  Règles principales
# ============================================================

.PHONY: all binaries samples clean

all: binaries samples

binaries: $(TARGETS)

samples: sample.cdb sample.pkt

# ============================================================
#  Compilation à différents niveaux d'optimisation
# ============================================================

# -O0 avec symboles de débogage (cible principale pour le RE)
fileformat_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $<

# -O0 sans symboles (strippé)
fileformat_O0_strip: fileformat_O0
	cp fileformat_O0 $@
	strip $@

# -O2 avec symboles
fileformat_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -o $@ $<

# -O2 sans symboles (strippé)
fileformat_O2_strip: fileformat_O2
	cp fileformat_O2 $@
	strip $@

# -O3 avec symboles
fileformat_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -g -o $@ $<

# ============================================================
#  Génération des fichiers de données
# ============================================================

# On utilise la version -O0 pour générer les samples.
# Les fichiers produits sont identiques quel que soit le
# niveau d'optimisation du binaire — c'est le format de
# sortie qui nous intéresse, pas les performances.

sample.cdb sample.pkt: fileformat_O0
	./fileformat_O0 --output-dir .

# ============================================================
#  Nettoyage
# ============================================================

clean:
	rm -f $(TARGETS) $(SAMPLES)
