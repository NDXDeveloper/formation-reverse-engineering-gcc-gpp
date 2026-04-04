# ==========================================================================
# Makefile — Chapitre 22 : Reverse d'une application C++ orientée objet
#
# Formation Reverse Engineering — Licence MIT
#
# Cibles produites :
#   oop_O0           → -O0 -g          (debug, correspondance directe)
#   oop_O2           → -O2 -g          (optimisé avec symboles)
#   oop_O2_strip     → -O2 -s          (optimisé sans symboles — réaliste)
#   plugins/plugin_alpha.so  → -O2 -g  (plugin ROT13)
#   plugins/plugin_beta.so   → -O2 -g  (plugin XOR)
#
# Usage :
#   make all         → compile tout
#   make run         → compile et exécute oop_O0 avec les plugins
#   make run-strip   → exécute la version strippée
#   make clean       → supprime les binaires et le répertoire plugins/
# ==========================================================================

CXX      := g++
CXXFLAGS := -Wall -Wextra -std=c++17

# Flags communs pour les plugins (.so)
PLUGIN_FLAGS := -shared -fPIC -O2 -g

# Texte de test par défaut
TEST_TEXT := "Hello World 42 from Reverse Engineering"

# ---- Cibles principales ----

.PHONY: all clean run run-strip dirs

all: dirs oop_O0 oop_O2 oop_O2_strip plugins/plugin_alpha.so plugins/plugin_beta.so

dirs:
	@mkdir -p plugins

# ---- Application principale ----

oop_O0: oop.cpp processor.h
	$(CXX) $(CXXFLAGS) -O0 -g -rdynamic -o $@ $< -ldl

oop_O2: oop.cpp processor.h
	$(CXX) $(CXXFLAGS) -O2 -g -rdynamic -o $@ $< -ldl

oop_O2_strip: oop.cpp processor.h
	$(CXX) $(CXXFLAGS) -O2 -s -rdynamic -o $@ $< -ldl

# Note : -rdynamic est nécessaire pour que les symboles de l'exécutable
# soient visibles par les plugins chargés via dlopen (résolution RTTI).

# ---- Plugins (.so) ----

plugins/plugin_alpha.so: plugin_alpha.cpp processor.h | dirs
	$(CXX) $(CXXFLAGS) $(PLUGIN_FLAGS) -o $@ $<

plugins/plugin_beta.so: plugin_beta.cpp processor.h | dirs
	$(CXX) $(CXXFLAGS) $(PLUGIN_FLAGS) -o $@ $<

# ---- Exécution de test ----

run: all
	@echo "=========================================="
	@echo " Running oop_O0 with plugins"
	@echo "=========================================="
	./oop_O0 -p ./plugins $(TEST_TEXT)

run-strip: all
	@echo "=========================================="
	@echo " Running oop_O2_strip with plugins"
	@echo "=========================================="
	./oop_O2_strip -p ./plugins $(TEST_TEXT)

# ---- Cibles utilitaires pour le RE ----

.PHONY: symbols vtables sections

# Affiche les symboles démanglés de l'exécutable
symbols: oop_O0
	@echo "=== Symboles de oop_O0 (démanglés) ==="
	nm -C oop_O0 | grep -E '(Processor|Pipeline|Upper|Reverse)' | sort

# Affiche les vtables dans .rodata
vtables: oop_O0
	@echo "=== Vtables (objdump -t filtré) ==="
	objdump -t -C oop_O0 | grep -i 'vtable'

# Affiche les sections ELF
sections: oop_O0
	@echo "=== Sections ELF ==="
	readelf -S oop_O0

# ---- Nettoyage ----

clean:
	rm -f oop_O0 oop_O2 oop_O2_strip
	rm -rf plugins/
