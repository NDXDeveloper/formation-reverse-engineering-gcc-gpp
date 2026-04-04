# Makefile — binaries/
#
# Formation Reverse Engineering — Applications compilées avec la chaîne GNU
# Makefile racine qui compile tous les binaires d'entraînement.
#
# Usage :
#   make          → compile tous les binaires de tous les chapitres
#   make ch05     → compile uniquement les binaires du chapitre 5
#   make ch21     → compile uniquement les binaires du chapitre 21
#   make clean    → nettoie tout
#   make list     → affiche tous les binaires produits
#
# Chaque sous-dossier contient son propre Makefile avec ses variantes.

# ──────────────────────────────────────────────
# Liste des sous-dossiers par chapitre
# ──────────────────────────────────────────────

SUBDIRS = \
	ch02-hello \
	ch03-checkpoint \
	ch05-keygenme \
	ch05-mystery_bin \
	ch06-fileformat \
	ch07-keygenme \
	ch08-oop \
	ch09-keygenme \
	ch10-keygenme \
	ch11-keygenme \
	ch12-keygenme \
	ch13-keygenme \
	ch13-network \
	ch13-oop \
	ch14-crypto \
	ch14-fileformat \
	ch14-keygenme \
	ch15-fileformat \
	ch15-keygenme \
	ch16-optimisations \
	ch17-oop \
	ch18-keygenme \
	ch19-anti-reversing \
	ch20-keygenme \
	ch20-network \
	ch20-oop \
	ch21-keygenme \
	ch22-oop \
	ch23-network \
	ch24-crypto \
	ch25-fileformat \
	ch27-ransomware \
	ch28-dropper \
	ch29-packed \
	ch32-dotnet \
	ch33-rust \
	ch34-go

# ──────────────────────────────────────────────
# Cible par défaut : tout compiler
# ──────────────────────────────────────────────

all:
	@echo "═══════════════════════════════════════════════════════"
	@echo "  Compilation de tous les binaires d'entraînement"
	@echo "═══════════════════════════════════════════════════════"
	@for dir in $(SUBDIRS); do \
		if [ -d "$$dir" ] && [ -f "$$dir/Makefile" ]; then \
			echo ""; \
			echo "──── $$dir ────"; \
			$(MAKE) -C $$dir all || echo "[!] Échec dans $$dir (ignoré)"; \
		else \
			echo "[!] $$dir : répertoire ou Makefile absent (ignoré)"; \
		fi; \
	done
	@echo ""
	@echo "═══════════════════════════════════════════════════════"
	@echo "  Compilation terminée."
	@echo "═══════════════════════════════════════════════════════"

# ──────────────────────────────────────────────
# Cibles par chapitre (raccourcis)
# ──────────────────────────────────────────────

ch02:
	$(MAKE) -C ch02-hello all

ch03:
	$(MAKE) -C ch03-checkpoint all

ch05:
	$(MAKE) -C ch05-keygenme all
	$(MAKE) -C ch05-mystery_bin all

ch06:
	$(MAKE) -C ch06-fileformat all

ch07:
	$(MAKE) -C ch07-keygenme all

ch08:
	$(MAKE) -C ch08-oop all

ch09:
	$(MAKE) -C ch09-keygenme all

ch10:
	$(MAKE) -C ch10-keygenme all

ch11:
	$(MAKE) -C ch11-keygenme all

ch12:
	$(MAKE) -C ch12-keygenme all

ch13:
	$(MAKE) -C ch13-keygenme all
	$(MAKE) -C ch13-network all
	$(MAKE) -C ch13-oop all

ch14:
	$(MAKE) -C ch14-crypto all
	$(MAKE) -C ch14-fileformat all
	$(MAKE) -C ch14-keygenme all

ch15:
	$(MAKE) -C ch15-fileformat all
	$(MAKE) -C ch15-keygenme all

ch16:
	$(MAKE) -C ch16-optimisations all

ch17:
	$(MAKE) -C ch17-oop all

ch18:
	$(MAKE) -C ch18-keygenme all

ch19:
	$(MAKE) -C ch19-anti-reversing all

ch20:
	$(MAKE) -C ch20-keygenme all
	$(MAKE) -C ch20-network all
	$(MAKE) -C ch20-oop all

ch21:
	$(MAKE) -C ch21-keygenme all

ch22:
	$(MAKE) -C ch22-oop all

ch23:
	$(MAKE) -C ch23-network all

ch24:
	$(MAKE) -C ch24-crypto all

ch25:
	$(MAKE) -C ch25-fileformat all

ch27:
	@echo "⚠️  ch27-ransomware : sandbox uniquement !"
	$(MAKE) -C ch27-ransomware all

ch28:
	@echo "⚠️  ch28-dropper : sandbox uniquement !"
	$(MAKE) -C ch28-dropper all

ch29:
	$(MAKE) -C ch29-packed all

ch32:
	$(MAKE) -C ch32-dotnet all

ch33:
	$(MAKE) -C ch33-rust all

ch34:
	$(MAKE) -C ch34-go all

# ──────────────────────────────────────────────
# Nettoyage
# ──────────────────────────────────────────────

clean:
	@for dir in $(SUBDIRS); do \
		if [ -d "$$dir" ] && [ -f "$$dir/Makefile" ]; then \
			$(MAKE) -C $$dir clean; \
		fi; \
	done
	@echo "Nettoyage terminé."

# ──────────────────────────────────────────────
# Lister tous les binaires produits
# ──────────────────────────────────────────────

list: all
	@echo ""
	@echo "═══════════════════════════════════════════════════════"
	@echo "  Binaires produits :"
	@echo "═══════════════════════════════════════════════════════"
	@find . -maxdepth 3 -type f -executable ! -name "*.sh" ! -name "Makefile" | sort

.PHONY: all clean list \
	ch02 ch03 ch05 ch06 ch07 ch08 ch09 ch10 ch11 ch12 ch13 ch14 ch15 ch16 \
	ch17 ch18 ch19 ch20 ch21 ch22 ch23 ch24 ch25 ch27 ch28 ch29 ch32 ch33 ch34
