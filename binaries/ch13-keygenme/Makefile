# ═══════════════════════════════════════════════════════════════
#  ch13-keygenme — Makefile
#  Formation Reverse Engineering — Chapitre 13
#
# Compile le crackme keygenme à différents niveaux d'optimisation,
# avec et sans symboles de débogage.
#
# Cibles :
#   make all       — compile toutes les variantes
#   make debug     — compile uniquement keygenme_O0
#   make clean     — supprime tous les binaires générés
#
# Variantes produites :
#   keygenme_O0          -O0 -g          (aucune optimisation, symboles DWARF)
#   keygenme_O2          -O2 -g          (optimisé, symboles)
#   keygenme_O3          -O3 -g          (optimisations agressives, symboles)
#   keygenme_strip       -O0 -s          (aucune optimisation, strippé)
#   keygenme_O2_strip    -O2 -s          (optimisé + strippé)
#
# Le binaire keygenme_O0 est la cible principale pour le chapitre
# d'apprentissage 13. Les variantes optimisées et
# strippées servent aux chapitres avancés (16, 18, 19).

# ═══════════════════════════════════════════════════════════════

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11
LDFLAGS :=

SRC     := keygenme.c

# ═══════════════════════════════════════════════
# Liste des cibles
# ═══════════════════════════════════════════════

ALL_BINS := keygenme_O0 keygenme_O2 keygenme_O3 keygenme_strip keygenme_O2_strip

# ═══════════════════════════════════════════════
# Cibles principales
# ═══════════════════════════════════════════════

.PHONY: all debug clean info

all: $(ALL_BINS)
	@echo ""
	@echo "✅ Toutes les variantes compilées :"
	@ls -lh $(ALL_BINS)
	@echo ""
	@echo "📊 Comparaison des tailles :"
	@printf "  %-20s %s\n" "keygenme_O0" "$$(stat -c%s keygenme_O0 2>/dev/null || stat -f%z keygenme_O0) octets"
	@printf "  %-20s %s\n" "keygenme_O2" "$$(stat -c%s keygenme_O2 2>/dev/null || stat -f%z keygenme_O2) octets"
	@printf "  %-20s %s\n" "keygenme_O3" "$$(stat -c%s keygenme_O3 2>/dev/null || stat -f%z keygenme_O3) octets"
	@printf "  %-20s %s\n" "keygenme_strip" "$$(stat -c%s keygenme_strip 2>/dev/null || stat -f%z keygenme_strip) octets"
	@printf "  %-20s %s\n" "keygenme_O2_strip" "$$(stat -c%s keygenme_O2_strip 2>/dev/null || stat -f%z keygenme_O2_strip) octets"
	@echo ""

debug: keygenme_O0
	@echo "✅ Variante debug compilée : keygenme_O0"

clean:
	rm -f $(ALL_BINS)
	@echo "🧹 Nettoyé."

info:
	@echo "Clé valide : GCC-RE-2024-XPRO"
	@echo ""
	@echo "Vérifications dans l'ordre :"
	@echo "  1. Longueur == 16"
	@echo "  2. Format XXX-XX-XXXX-XXXX (tirets positions 3, 6, 11)"
	@echo "  3. Préfixe \"GCC\""
	@echo "  4. Checksum additif == 0x03DB"
	@echo "  5. strcmp avec clé de référence (XOR-décodée)"

# ═══════════════════════════════════════════════
# Variantes de compilation
# ═══════════════════════════════════════════════

# -O0 -g : aucune optimisation, symboles de débogage complets.
# C'est la cible principale pour l'apprentissage du RE.
# Le code assembleur suit fidèlement la structure du C source :
# pas d'inlining, pas de réordonnancement, pile classique
# avec prologue/épilogue rbp.
keygenme_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $< $(LDFLAGS)

# -O2 -g : optimisations standard de GCC.
# Inlining partiel, réordonnancement d'instructions,
# élimination de code mort, tail calls possibles.
# Les symboles DWARF sont présents mais les correspondances
# source↔assembleur sont moins directes.
keygenme_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -o $@ $< $(LDFLAGS)

# -O3 -g : optimisations agressives.
# Inlining plus agressif, vectorisation, déroulage de boucles.
# Certaines fonctions peuvent disparaître (inlinées dans main).
keygenme_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -g -o $@ $< $(LDFLAGS)

# -O0 -s : sans optimisation mais strippé.
# Les noms de fonctions (validate_key, check_length, etc.)
# disparaissent de la table de symboles. L'assembleur reste
# lisible (pas d'optimisation) mais il faut retrouver les
# fonctions par analyse statique.
keygenme_strip: $(SRC)
	$(CC) $(CFLAGS) -O0 -s -o $@ $< $(LDFLAGS)

# -O2 -s : optimisé ET strippé.
# Le cas le plus difficile : code réorganisé par les
# optimisations, fonctions potentiellement inlinées,
# et aucun symbole pour se repérer. C'est la cible
# du checkpoint du chapitre 18 (exécution symbolique
# avec angr).
keygenme_O2_strip: $(SRC)
	$(CC) $(CFLAGS) -O2 -s -o $@ $< $(LDFLAGS)
