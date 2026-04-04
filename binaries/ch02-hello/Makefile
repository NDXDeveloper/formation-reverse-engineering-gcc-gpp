# ============================================================================
# Makefile — Chapitre 2 : La chaîne de compilation GNU
#
# Produit toutes les variantes du binaire hello utilisées dans le chapitre.
# Usage :
#   make              Compile toutes les variantes
#   make intermediates Génère les fichiers intermédiaires (.i, .s, .o)
#   make compare      Affiche un tableau comparatif des tailles
#   make clean        Supprime tous les fichiers générés
#
# Licence MIT — Ce contenu est strictement éducatif et éthique.
# ============================================================================

CC      = gcc
SRC     = hello.c
CFLAGS  = -Wall -Wextra

# ────────────────────────────────────────────────────────────
# Cibles principales
# ────────────────────────────────────────────────────────────

# Toutes les variantes binaires produites par ce Makefile
BINS = hello_O0 hello_O1 hello_O2 hello_O3 hello_Os \
       hello_debug hello_debug_O2 \
       hello_stripped hello_release \
       hello_nopie hello_static \
       hello_partial_relro hello_fullrelro \
       hello_canary hello_hardened

# Fichiers intermédiaires (section 2.2)
INTERMEDIATES = hello.i hello.s hello.o \
                hello_O0.s hello_O1.s hello_O2.s hello_O3.s hello_Os.s \
                hello_intel.s

.PHONY: all intermediates compare clean help

all: $(BINS)
	@echo ""
	@echo "✅ Toutes les variantes compilées avec succès."
	@echo "   Lancez 'make compare' pour voir le tableau comparatif."

# ────────────────────────────────────────────────────────────
# Section 2.2 — Fichiers intermédiaires
# ────────────────────────────────────────────────────────────

intermediates: $(INTERMEDIATES)
	@echo ""
	@echo "✅ Fichiers intermédiaires générés :"
	@ls -lh hello.i hello.s hello.o 2>/dev/null
	@echo ""
	@echo "   hello.i  → Sortie du préprocesseur (gcc -E)"
	@echo "   hello.s  → Sortie du compilateur, syntaxe AT&T (gcc -S)"
	@echo "   hello.o  → Fichier objet ELF relocatable (gcc -c)"
	@echo ""
	@echo "   Fichiers .s par niveau d'optimisation :"
	@ls -lh hello_O0.s hello_O1.s hello_O2.s hello_O3.s hello_Os.s 2>/dev/null
	@echo ""
	@echo "   hello_intel.s → Syntaxe Intel (gcc -S -masm=intel)"

# Préprocesseur seul (section 2.1, 2.2)
hello.i: $(SRC)
	$(CC) -E $(SRC) -o $@

# Compilation seule — assembleur AT&T (section 2.1, 2.2)
hello.s: $(SRC)
	$(CC) -S -O0 $(SRC) -o $@

# Compilation seule — syntaxe Intel (section 2.2, 7.2)
hello_intel.s: $(SRC)
	$(CC) -S -O0 -masm=intel $(SRC) -o $@

# Assemblage seul — fichier objet (section 2.2)
hello.o: $(SRC)
	$(CC) -c -O0 $(SRC) -o $@

# Assembleur à chaque niveau d'optimisation (section 2.2, 2.5)
hello_O0.s: $(SRC)
	$(CC) -S -O0 $(SRC) -o $@

hello_O1.s: $(SRC)
	$(CC) -S -O1 $(SRC) -o $@

hello_O2.s: $(SRC)
	$(CC) -S -O2 $(SRC) -o $@

hello_O3.s: $(SRC)
	$(CC) -S -O3 $(SRC) -o $@

hello_Os.s: $(SRC)
	$(CC) -S -Os $(SRC) -o $@

# ────────────────────────────────────────────────────────────
# Section 2.5 — Niveaux d'optimisation
# ────────────────────────────────────────────────────────────

hello_O0: $(SRC)
	$(CC) $(CFLAGS) -O0 -o $@ $(SRC)

hello_O1: $(SRC)
	$(CC) $(CFLAGS) -O1 -o $@ $(SRC)

hello_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -o $@ $(SRC)

hello_O3: $(SRC)
	$(CC) $(CFLAGS) -O3 -o $@ $(SRC)

hello_Os: $(SRC)
	$(CC) $(CFLAGS) -Os -o $@ $(SRC)

# ────────────────────────────────────────────────────────────
# Section 2.5, 2.6 — Debug et stripping
# ────────────────────────────────────────────────────────────

# Debug complet : aucune optimisation + DWARF (section 2.6)
hello_debug: $(SRC)
	$(CC) $(CFLAGS) -O0 -g -o $@ $(SRC)

# Debug + optimisation : DWARF présent mais variables parfois "optimized out"
hello_debug_O2: $(SRC)
	$(CC) $(CFLAGS) -O2 -g -o $@ $(SRC)

# Strippé à la compilation (section 2.5)
hello_stripped: $(SRC)
	$(CC) $(CFLAGS) -O0 -s -o $@ $(SRC)

# Build release typique : optimisé + strippé (checkpoint)
hello_release: $(SRC)
	$(CC) $(CFLAGS) -O2 -s -o $@ $(SRC)

# ────────────────────────────────────────────────────────────
# Section 2.5, 2.8 — PIE et positionnement du code
# ────────────────────────────────────────────────────────────

# Non-PIE : adresses fixes, pas d'ASLR pour le binaire (section 2.8)
hello_nopie: $(SRC)
	$(CC) $(CFLAGS) -O0 -no-pie -o $@ $(SRC)

# Statiquement lié : pas de PLT/GOT, pas de .so (section 2.7, 2.9)
hello_static: $(SRC)
	$(CC) $(CFLAGS) -O0 -static -o $@ $(SRC)

# ────────────────────────────────────────────────────────────
# Section 2.9 — RELRO et protections
# ────────────────────────────────────────────────────────────

# Partial RELRO (défaut, explicité ici pour la clarté)
hello_partial_relro: $(SRC)
	$(CC) $(CFLAGS) -O0 -Wl,-z,relro -o $@ $(SRC)

# Full RELRO : GOT en lecture seule après résolution (section 2.9)
hello_fullrelro: $(SRC)
	$(CC) $(CFLAGS) -O0 -Wl,-z,relro,-z,now -o $@ $(SRC)

# Stack canary explicite (section 2.5)
hello_canary: $(SRC)
	$(CC) $(CFLAGS) -O0 -fstack-protector-all -o $@ $(SRC)

# Build « hardened » : toutes les protections (section 2.5, 2.8, 2.9)
hello_hardened: $(SRC)
	$(CC) $(CFLAGS) -O2 -s -pie \
		-fstack-protector-strong \
		-D_FORTIFY_SOURCE=2 \
		-Wl,-z,relro,-z,now \
		-o $@ $(SRC)

# ────────────────────────────────────────────────────────────
# Utilitaires
# ────────────────────────────────────────────────────────────

# Tableau comparatif des tailles et propriétés
compare: $(BINS)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════════╗"
	@echo "║           Comparaison des variantes — Chapitre 2               ║"
	@echo "╠═══════════════════════╦════════╦═════════╦══════════╦══════════╣"
	@echo "║ Variante              ║ Taille ║ Strippé ║ Sections ║ .debug_* ║"
	@echo "╠═══════════════════════╬════════╬═════════╬══════════╬══════════╣"
	@for bin in $(BINS); do \
		size=$$(du -h $$bin | cut -f1); \
		stripped=$$(file $$bin | grep -q 'not stripped' && echo "non" || echo "OUI"); \
		nsections=$$(readelf -S $$bin 2>/dev/null | grep -c '\['); \
		ndebug=$$(readelf -S $$bin 2>/dev/null | grep -c '\.debug_'); \
		printf "║ %-21s ║ %6s ║ %7s ║ %8s ║ %8s ║\n" \
			"$$bin" "$$size" "$$stripped" "$$nsections" "$$ndebug"; \
	done
	@echo "╚═══════════════════════╩════════╩═════════╩══════════╩══════════╝"
	@echo ""

clean:
	rm -f $(BINS) $(INTERMEDIATES)
	rm -f hello hello.i hello.s hello.o
	rm -f *.txt
	@echo "🧹 Nettoyage terminé."

help:
	@echo ""
	@echo "Cibles disponibles :"
	@echo "  make              Compile toutes les variantes binaires"
	@echo "  make intermediates Génère les fichiers intermédiaires (.i, .s, .o)"
	@echo "  make compare      Affiche un tableau comparatif des variantes"
	@echo "  make hello_debug  Compile une variante spécifique"
	@echo "  make clean        Supprime tous les fichiers générés"
	@echo "  make help         Affiche cette aide"
	@echo ""
	@echo "Variantes disponibles :"
	@echo "  hello_O0 hello_O1 hello_O2 hello_O3 hello_Os"
	@echo "  hello_debug hello_debug_O2"
	@echo "  hello_stripped hello_release"
	@echo "  hello_nopie hello_static"
	@echo "  hello_partial_relro hello_fullrelro"
	@echo "  hello_canary hello_hardened"
	@echo ""
