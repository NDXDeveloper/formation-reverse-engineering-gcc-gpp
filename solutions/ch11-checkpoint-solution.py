#!/usr/bin/env python3
"""
strcmp_dump.py — Script GDB Python pour tracer les appels à strcmp.

Usage dans GDB :
    (gdb) source strcmp_dump.py
    (gdb) run
    
Le script :
  1. Pose un breakpoint silencieux sur strcmp.
  2. À chaque appel, lit les deux chaînes (rdi, rsi) et les enregistre.
  3. À la fin du programme, affiche un rapport récapitulatif.
"""
import gdb

# ──────────────────────────────────────────────
# Configuration
# ──────────────────────────────────────────────
MAX_STRING_LEN = 256        # Taille maximale lue par chaîne
SHOW_LIVE = True            # Afficher chaque appel en temps réel
FILTER_EMPTY = True         # Ignorer les appels où une chaîne est vide

# ──────────────────────────────────────────────
# Utilitaires
# ──────────────────────────────────────────────
def safe_read_string(inferior, addr, max_len=MAX_STRING_LEN):
    """Lit une chaîne C null-terminated à l'adresse donnée.
    
    Retourne la chaîne décodée, ou None si l'adresse est invalide.
    """
    if addr == 0:
        return None
    
    try:
        raw = bytes(inferior.read_memory(addr, max_len))
    except gdb.MemoryError:
        return None
    
    # Extraire jusqu'au premier null byte
    null_pos = raw.find(b'\x00')
    if null_pos != -1:
        raw = raw[:null_pos]
    
    # Décoder en UTF-8 avec remplacement des octets invalides
    return raw.decode('utf-8', errors='replace')


def is_printable_string(s):
    """Vérifie si une chaîne est raisonnablement imprimable."""
    if not s:
        return False
    printable_ratio = sum(1 for c in s if c.isprintable() or c in '\t\n\r') / len(s)
    return printable_ratio > 0.8


def format_string(s, max_display=80):
    """Formate une chaîne pour l'affichage, avec troncature si nécessaire."""
    if s is None:
        return "<NULL>"
    if len(s) == 0:
        return "<empty>"
    
    # Échapper les caractères de contrôle
    display = repr(s)[1:-1]     # Utiliser repr() et retirer les quotes
    
    if len(display) > max_display:
        return display[:max_display - 3] + "..."
    return display

# ──────────────────────────────────────────────
# Breakpoint scripté
# ──────────────────────────────────────────────
class StrcmpDumper(gdb.Breakpoint):
    """Breakpoint sur strcmp qui capture les arguments à chaque appel."""
    
    def __init__(self):
        # Nettoyer les instances précédentes (idempotence au rechargement)
        for bp in gdb.breakpoints() or []:
            if hasattr(bp, '_is_strcmp_dumper'):
                bp.delete()
        
        super().__init__("strcmp", type=gdb.BP_BREAKPOINT)
        self._is_strcmp_dumper = True    # Marqueur pour le nettoyage
        self.silent = True               # Supprimer le message GDB standard
        self.calls = []                  # Liste de tous les appels capturés
        self.call_count = 0
        
        gdb.write("[strcmp_dump] Breakpoint installé sur strcmp.\n")
    
    def stop(self):
        """Appelé à chaque déclenchement. Retourne False pour ne pas arrêter."""
        self.call_count += 1
        
        frame = gdb.selected_frame()
        inferior = gdb.selected_inferior()
        
        # Lire les adresses des deux arguments
        rdi = int(frame.read_register("rdi"))
        rsi = int(frame.read_register("rsi"))
        
        # Lire les chaînes
        s1 = safe_read_string(inferior, rdi)
        s2 = safe_read_string(inferior, rsi)
        
        # Filtrer les appels avec une chaîne vide si configuré
        if FILTER_EMPTY and (s1 is None or s2 is None or s1 == "" or s2 == ""):
            return False
        
        # Identifier l'appelant (adresse de retour)
        caller_frame = frame.older()
        caller_pc = caller_frame.pc() if caller_frame else 0
        caller_name = caller_frame.name() if caller_frame and caller_frame.name() else "??"
        
        # Enregistrer l'appel
        entry = {
            "num": self.call_count,
            "s1": s1,
            "s2": s2,
            "s1_addr": rdi,
            "s2_addr": rsi,
            "caller_pc": caller_pc,
            "caller_name": caller_name,
            "match": s1 == s2 if (s1 is not None and s2 is not None) else None
        }
        self.calls.append(entry)
        
        # Affichage en temps réel
        if SHOW_LIVE:
            match_indicator = ""
            if entry["match"] is True:
                match_indicator = "  ✓ MATCH"
            elif entry["match"] is False:
                match_indicator = "  ✗"
            
            gdb.write(
                f"[strcmp #{self.call_count}] "
                f"\"{format_string(s1)}\" vs \"{format_string(s2)}\""
                f"  (caller: {caller_name} @ {caller_pc:#x})"
                f"{match_indicator}\n"
            )
        
        return False    # Ne jamais arrêter — continuer l'exécution


# ──────────────────────────────────────────────
# Rapport de fin d'exécution
# ──────────────────────────────────────────────
def on_exit(event):
    """Affiche le rapport récapitulatif quand le programme se termine."""
    if not hasattr(StrcmpDumper, '_instance'):
        return
    
    dumper = StrcmpDumper._instance
    calls = dumper.calls
    
    gdb.write(f"\n{'=' * 70}\n")
    gdb.write(f"  RAPPORT strcmp_dump — {len(calls)} appels capturés "
              f"({dumper.call_count} total, {dumper.call_count - len(calls)} filtrés)\n")
    gdb.write(f"{'=' * 70}\n\n")
    
    if not calls:
        gdb.write("  Aucun appel à strcmp capturé.\n")
        return
    
    # Section 1 : tous les appels
    gdb.write("  Appels enregistrés :\n")
    gdb.write(f"  {'#':>4}  {'Match':>5}  {'Appelant':<20}  {'Chaîne 1':<30}  {'Chaîne 2':<30}\n")
    gdb.write(f"  {'—'*4}  {'—'*5}  {'—'*20}  {'—'*30}  {'—'*30}\n")
    
    for e in calls:
        match = " ✓" if e["match"] else " ✗" if e["match"] is False else " ?"
        caller = f"{e['caller_name']}"[:20]
        gdb.write(
            f"  {e['num']:>4}  {match:>5}  {caller:<20}  "
            f"{format_string(e['s1'], 30):<30}  "
            f"{format_string(e['s2'], 30):<30}\n"
        )
    
    # Section 2 : appels avec match (potentiellement intéressants)
    matches = [e for e in calls if e["match"] is True]
    if matches:
        gdb.write(f"\n  Correspondances exactes ({len(matches)}) :\n")
        for e in matches:
            gdb.write(f"    strcmp #{e['num']}: \"{format_string(e['s1'])}\" "
                      f"(caller: {e['caller_name']} @ {e['caller_pc']:#x})\n")
    
    # Section 3 : chaînes uniques vues (déduplication)
    all_strings = set()
    for e in calls:
        if e["s1"] and is_printable_string(e["s1"]):
            all_strings.add(e["s1"])
        if e["s2"] and is_printable_string(e["s2"]):
            all_strings.add(e["s2"])
    
    if all_strings:
        gdb.write(f"\n  Chaînes uniques observées ({len(all_strings)}) :\n")
        for s in sorted(all_strings):
            gdb.write(f"    • {format_string(s)}\n")
    
    # Section 4 : appelants uniques
    callers = {}
    for e in calls:
        key = e["caller_pc"]
        if key not in callers:
            callers[key] = {"name": e["caller_name"], "count": 0}
        callers[key]["count"] += 1
    
    gdb.write(f"\n  Sites d'appel uniques ({len(callers)}) :\n")
    for pc, info in sorted(callers.items(), key=lambda x: -x[1]["count"]):
        gdb.write(f"    {pc:#x} ({info['name']}) — {info['count']} appel(s)\n")
    
    gdb.write(f"\n{'=' * 70}\n")


# ──────────────────────────────────────────────
# Initialisation
# ──────────────────────────────────────────────
def init():
    """Point d'entrée du script."""
    # Créer le breakpoint
    dumper = StrcmpDumper()
    StrcmpDumper._instance = dumper
    
    # S'abonner à l'événement de fin de programme
    # (déconnecter d'abord pour éviter les doublons au rechargement)
    try:
        gdb.events.exited.disconnect(on_exit)
    except ValueError:
        pass    # Pas encore connecté
    gdb.events.exited.connect(on_exit)
    
    gdb.write("[strcmp_dump] Script chargé. Lancez le programme avec 'run'.\n")
    gdb.write(f"[strcmp_dump] Config: SHOW_LIVE={SHOW_LIVE}, "
              f"FILTER_EMPTY={FILTER_EMPTY}, "
              f"MAX_STRING_LEN={MAX_STRING_LEN}\n")

init()
