# ═══════════════════════════════════════════════════════════════
#  ch32-dotnet — Makefile
#  Formation Reverse Engineering — Chapitre 32
#
# Prérequis :
#   - gcc
#   - dotnet SDK 8.0+
#
# Cibles :
#   make all     — compile la bibliothèque native + l'application .NET
#   make native  — compile uniquement libnative_check.so
#   make dotnet  — compile uniquement l'application .NET
#   make run     — lance l'application
#   make clean   — nettoie tous les artefacts de compilation

# ═══════════════════════════════════════════════════════════════

.PHONY: all native dotnet run clean

NATIVE_SRC   = native/native_check.c
NATIVE_LIB   = native/libnative_check.so
DOTNET_PROJ  = LicenseChecker/LicenseChecker.csproj
DOTNET_OUT   = LicenseChecker/bin/Release/net8.0/linux-x64

CC      = gcc
CFLAGS  = -shared -fPIC -O2 -Wall -Wextra

all: native dotnet
	@echo ""
	@echo "══════════════════════════════════════════════════════════"
	@echo " ✅ Build complet."
	@echo " Binaire .NET : $(DOTNET_OUT)/LicenseChecker"
	@echo " Lib native   : $(NATIVE_LIB)"
	@echo "══════════════════════════════════════════════════════════"
	@echo ""
	@echo " Lancer : make run"
	@echo " Ou     : cd $(DOTNET_OUT) && LD_LIBRARY_PATH=. ./LicenseChecker"
	@echo ""

native: $(NATIVE_LIB)

$(NATIVE_LIB): $(NATIVE_SRC)
	@mkdir -p native
	$(CC) $(CFLAGS) -o $@ $<
	@echo "[OK] libnative_check.so compilée"

dotnet: native
	dotnet publish $(DOTNET_PROJ) \
		-c Release \
		-r linux-x64 \
		--self-contained false \
		-o $(DOTNET_OUT)
	cp $(NATIVE_LIB) $(DOTNET_OUT)/
	@echo "[OK] Application .NET compilée"

run: all
	@echo "── Lancement de LicenseChecker ──"
	@cd $(DOTNET_OUT) && LD_LIBRARY_PATH=. ./LicenseChecker

clean:
	rm -f $(NATIVE_LIB)
	rm -rf LicenseChecker/bin/ LicenseChecker/obj/
	@echo "[OK] Nettoyage effectué"
