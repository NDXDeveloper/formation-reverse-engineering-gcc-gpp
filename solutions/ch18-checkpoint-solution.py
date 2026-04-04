#!/usr/bin/env python3
"""
solutions/ch18-checkpoint-solution.py
=====================================
Solution de référence — Checkpoint du chapitre 18

Résout keygenme_O2_strip avec angr en ≤ 30 lignes de code utiles.

Usage :
    cd binaries/ch18-keygenme/
    python3 ../../solutions/ch18-checkpoint-solution.py

Prérequis :
    - angr installé dans un virtualenv  (pip install angr)
    - keygenme_O2_strip compilé         (make keygenme_O2_strip)

Vérification :
    ./keygenme_O2_strip <serial_affiché>
    → doit afficher "Access Granted!"

Licence MIT — Usage strictement éducatif.
"""

# ============================================================================
#  SOLUTION PRINCIPALE — 20 lignes de code utiles
# ============================================================================

import angr
import claripy
import sys
import os

# --- Configuration -----------------------------------------------------------

BINARY = os.path.join(
    os.path.dirname(__file__), "..", "binaries", "ch18-keygenme", "keygenme_O2_strip"
)
# Fallback : si lancé directement depuis le dossier du binaire
if not os.path.isfile(BINARY):
    BINARY = "./keygenme_O2_strip"

SERIAL_LEN = 16  # 16 caractères hexadécimaux = 64 bits

# --- Résolution --------------------------------------------------------------

# 1. Charger le binaire (sans la libc pour éviter l'explosion des chemins)
proj = angr.Project(BINARY, auto_load_libs=False)                          #  1

# 2. Créer 16 caractères symboliques de 8 bits chacun
chars = [claripy.BVS(f"c{i}", 8) for i in range(SERIAL_LEN)]              #  2

# 3. Les concaténer en un seul bitvector de 128 bits
serial = claripy.Concat(*chars)                                            #  3

# 4. Créer l'état initial : le binaire reçoit le serial comme argv[1]
state = proj.factory.entry_state(args=[BINARY, serial])                    #  4

# 5. Contraindre chaque caractère au jeu hexadécimal [0-9A-Fa-f]
for c in chars:                                                            #  5
    state.solver.add(claripy.Or(                                           #  6
        claripy.And(c >= ord('0'), c <= ord('9')),                         #  7
        claripy.And(c >= ord('A'), c <= ord('F')),                         #  8
        claripy.And(c >= ord('a'), c <= ord('f'))                          #  9
    ))                                                                     # 10

# 6. Créer le SimulationManager et lancer l'exploration
simgr = proj.factory.simgr(state)                                         # 11
simgr.explore(                                                             # 12
    find=lambda s: b"Access Granted" in s.posix.dumps(1),                  # 13
    avoid=lambda s: b"Access Denied" in s.posix.dumps(1)                   # 14
)                                                                          # 15

# 7. Extraire et afficher la solution
if simgr.found:                                                            # 16
    found_state = simgr.found[0]                                           # 17
    solution = found_state.solver.eval(serial, cast_to=bytes)              # 18
    print(f"[+] Serial trouvé : {solution.decode()}")                      # 19
else:                                                                      # 20
    print("[-] Aucune solution trouvée.")                                  # 21
    print(f"    active={len(simgr.active)} "                               # 22
          f"deadended={len(simgr.deadended)} "                             # 23
          f"avoided={len(simgr.avoided)} "                                 # 24
          f"errored={len(simgr.errored)}")                                 # 25
    sys.exit(1)                                                            # 26

# ============================================================================
#  VÉRIFICATION AUTOMATIQUE (bonus — hors décompte des 30 lignes)
# ============================================================================

import subprocess

serial_str = solution.decode()
result = subprocess.run(
    [BINARY, serial_str],
    capture_output=True, text=True, timeout=5
)

if "Access Granted" in result.stdout:
    print(f"[✓] Vérifié : ./keygenme_O2_strip {serial_str}")
    print(f"    stdout → {result.stdout.strip()}")
else:
    print(f"[✗] ÉCHEC : le serial ne fonctionne pas sur le binaire réel.")
    print(f"    stdout → {result.stdout.strip()}")
    sys.exit(1)

# ============================================================================
#  BONUS : résolution alternative avec Z3 seul (hors décompte)
#
#  Montre que le même résultat peut être obtenu sans angr, en modélisant
#  les contraintes extraites manuellement depuis le décompileur Ghidra.
# ============================================================================

print("\n--- Résolution alternative avec Z3 ---\n")

from z3 import BitVec, BitVecVal, Solver, sat, LShR

def mix32_z3(v, seed):
    """Reproduction de mix32() en Z3 — extraite du pseudo-code Ghidra."""
    v = v ^ seed
    v = (LShR(v, 16) ^ v) * BitVecVal(0x45d9f3b, 32)
    v = (LShR(v, 16) ^ v) * BitVecVal(0x45d9f3b, 32)
    v = LShR(v, 16) ^ v
    return v

def feistel4_z3(high, low):
    """4 tours de Feistel — traduit du désassemblage."""
    seeds = [0x5a3ce7f1, 0x1f4b8c2d, 0xdead1337, 0x8badf00d]
    for seed in seeds:
        tmp = low
        low = high ^ mix32_z3(low, BitVecVal(seed, 32))
        high = tmp
    return high, low

# Inconnues : les deux moitiés 32 bits du serial AVANT transformation
high_in = BitVec("high_in", 32)
low_in  = BitVec("low_in", 32)

# Appliquer la transformation
high_out, low_out = feistel4_z3(high_in, low_in)

# Condition de succès extraite du binaire
z3_solver = Solver()
z3_solver.add(high_out == BitVecVal(0xa11c3514, 32))
z3_solver.add(low_out  == BitVecVal(0xf00dcafe, 32))

if z3_solver.check() == sat:
    m = z3_solver.model()
    h = m[high_in].as_long()
    l = m[low_in].as_long()
    z3_serial = f"{h:08x}{l:08x}"
    print(f"[+] Serial trouvé (Z3) : {z3_serial}")

    # Vérification
    result_z3 = subprocess.run(
        [BINARY, z3_serial],
        capture_output=True, text=True, timeout=5
    )
    if "Access Granted" in result_z3.stdout:
        print(f"[✓] Vérifié : ./keygenme_O2_strip {z3_serial}")
    else:
        print(f"[✗] ÉCHEC Z3 : le serial ne fonctionne pas.")
else:
    print("[-] Z3 : aucune solution (UNSAT)")
