🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution du Checkpoint — Chapitre 4

> **Exercice** : Exécuter `check_env.sh` et obtenir zéro échec critique.

---

## Procédure

```bash
# 1. Activer l'environnement virtuel Python
source ~/re-venv/bin/activate

# 2. Se placer à la racine du dépôt
cd ~/formation-re

# 3. Rendre le script exécutable
chmod +x check_env.sh

# 4. Exécuter le script
./check_env.sh
```

---

## Résultat attendu

Le script doit se terminer par :

```
  RÉSULTAT : XX/XX vérifications réussies
             0 échec(s) critique(s)
             N élément(s) optionnel(s) manquant(s)

  ✔ Votre environnement est prêt pour la formation.
```

Le code de sortie doit être `0` :

```bash
echo $?
# 0
```

---

## Critères de validation

| Catégorie | Éléments vérifiés | Résultat attendu |  
|---|---|---|  
| **Socle système** | gcc, g++, make, python3, java (JRE), gdb | Tous `[✔]` |  
| **Binutils** | objdump, readelf, nm, strings, c++filt, strip, ldd | Tous `[✔]` |  
| **Traçage** | strace, ltrace | Tous `[✔]` |  
| **Désassembleurs** | Ghidra (analyzeHeadless), Radare2 (r2) | Tous `[✔]` |  
| **Hex editor** | ImHex | `[✔]` |  
| **Extension GDB** | GEF, pwndbg ou PEDA | Au moins un `[✔]` |  
| **Python libs** | angr, pwntools, frida, z3, pyelftools, lief, r2pipe, yara | Tous `[✔]` |  
| **Fuzzing** | AFL++ (afl-fuzz) | `[✔]` |  
| **Analyse mémoire** | Valgrind | `[✔]` |  
| **Détection** | checksec, YARA | Tous `[✔]` |  
| **Système** | Architecture x86-64, espace disque >= 15 Go, RAM >= 4 Go | Pas de `[✗]` |  
| **Binaires** | Sous-dossiers ch* compilés via `make all` | Comptage correct |

---

## Résolution des échecs courants

### `[✗]` sur un outil en ligne de commande

```bash
# Identifier le paquet manquant
apt search <outil>

# Installer
sudo apt install -y <paquet>
```

### `[✗]` sur un paquet Python

```bash
# S'assurer que le venv est activé
source ~/re-venv/bin/activate

# Installer le paquet manquant
pip install <paquet>
```

### `[✗]` sur Ghidra

Ghidra n'est pas dans les dépôts APT. Vérifiez que le dossier d'installation est dans votre `PATH` :

```bash
echo $PATH | tr ':' '\n' | grep -i ghidra
# Si vide, ajoutez à ~/.bashrc :
export PATH="$PATH:/opt/ghidra/support"
```

### `[✗]` sur les binaires d'entraînement

```bash
cd ~/formation-re/binaries  
make all  
```

### `[✗]` sur l'extension GDB

Vérifiez que votre `~/.gdbinit` charge l'extension :

```bash
cat ~/.gdbinit | grep -E 'gef|pwndbg|peda'
```

Si aucune ligne n'apparaît, ajoutez celle de votre extension choisie (une seule) :

```bash
# Pour GEF :
echo 'source ~/.gdbinit-gef.py' >> ~/.gdbinit

# Pour pwndbg :
echo 'source ~/pwndbg/gdbinit.py' >> ~/.gdbinit
```

---

## Conserver le log

```bash
./check_env.sh | tee ~/check_env_$(date +%Y%m%d).log
```

Ce log sert de référence en cas de problème futur (mise à jour système, restauration de snapshot).

---

## Prendre le snapshot

Après validation (0 échec critique), prenez le snapshot de référence :

**VirtualBox** : Machine → Prendre un instantané → `tools-ready`

**QEMU/KVM** :
```bash
virsh snapshot-create-as RE-Lab tools-ready --description "Chapitre 4 checkpoint OK"
```

Ce snapshot représente l'état de référence de la formation : système installé, outils configurés, binaires compilés, tout au vert.

---

⏭️ [Partie II — Analyse Statique](/partie-2-analyse-statique.md)
