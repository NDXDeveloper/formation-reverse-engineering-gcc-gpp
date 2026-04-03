🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Écrire un script GDB Python qui dumpe automatiquement les arguments de chaque appel à `strcmp`

> **Chapitre 11 — Débogage avec GDB**  
> **Partie III — Analyse Dynamique**

---

## Objectif

Ce checkpoint valide les compétences acquises au fil du chapitre 11 en les mobilisant dans un script unique. L'objectif est de produire un script GDB Python (`strcmp_dump.py`) qui, une fois chargé dans GDB, intercepte **chaque appel à `strcmp`** dans le binaire cible, affiche les deux chaînes comparées de manière lisible, accumule les résultats, et produit un **rapport récapitulatif** à la fin de l'exécution.

Ce type de script est un outil de RE quotidien : sur un binaire strippé, poser un breakpoint scripté sur `strcmp` (ou `memcmp`, `strncmp`) est souvent le moyen le plus rapide de découvrir ce que le programme compare — et donc d'extraire des clés, des mots de passe, des tokens ou des valeurs attendues.

## Compétences mobilisées

| Section | Compétence utilisée |  
|---|---|  
| 11.1 | Comprendre pourquoi le script fonctionne aussi sur un binaire strippé (`.dynsym` préservée) |  
| 11.2 | Breakpoints sur des fonctions de bibliothèque (`break strcmp`) |  
| 11.3 | Lecture des registres `rdi`/`rsi` (arguments System V AMD64), lecture mémoire avec interprétation |  
| 11.4 | Fonctionnement sans symboles — `strcmp` est résolu via la PLT |  
| 11.5 | Breakpoint avec logique conditionnelle (filtrage du bruit) |  
| 11.8 | API Python : `gdb.Breakpoint`, `read_register()`, `read_memory()`, `gdb.events.exited` |  
| 11.9 | Optionnel : lancer le binaire via pwntools et charger le script |

## Binaire cible

Le script doit fonctionner sur `keygenme_O0` (avec symboles) **et** `keygenme_O2_strip` (strippé et optimisé). Les deux utilisent `strcmp` pour comparer la clé utilisateur avec la clé attendue.

```bash
$ cd binaries/ch11-keygenme/
$ make    # Génère keygenme_O0, keygenme_O2, keygenme_O2_strip, etc.
```

## Conception du script

### Architecture générale

Le script repose sur trois composants :

1. **Un breakpoint scripté** (`StrcmpDumper`) qui sous-classe `gdb.Breakpoint`, se pose sur `strcmp`, et implémente `stop()` pour capturer les arguments à chaque appel.  
2. **Un handler d'événement** connecté à `gdb.events.exited` qui affiche le rapport récapitulatif quand le programme se termine.  
3. **Une logique de lecture mémoire robuste** qui gère les adresses invalides, les chaînes non imprimables et les buffers anormalement longs.

### Les défis techniques

Plusieurs problèmes doivent être anticipés :

**Chaînes invalides ou très longues.** Le registre `rdi` ou `rsi` peut contenir une adresse invalide, un pointeur `NULL`, ou pointer vers un buffer de plusieurs mégaoctets. Le script doit lire prudemment, avec une taille maximale et un `try/except` sur `gdb.MemoryError`.

**Appels parasites.** La glibc elle-même appelle `strcmp` pendant l'initialisation du programme (résolution de locales, chargement de bibliothèques). Ces appels n'ont rien à voir avec la logique du binaire. Le script doit les enregistrer sans polluer l'affichage principal — on les filtrera dans le rapport.

**Double encodage.** Les chaînes comparées peuvent contenir des caractères non-ASCII (octets bruts, séquences UTF-8 partielles). Le décodage doit utiliser `errors='replace'` pour ne jamais planter.

**Performance.** Sur un programme qui appelle `strcmp` des centaines de fois, chaque déclenchement provoque un aller-retour ptrace. Le script doit être le plus léger possible dans `stop()` — accumuler les données et reporter l'analyse au rapport final.

## Script complet : `strcmp_dump.py`

```python
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
```

## Utilisation

### Session de base

```bash
$ gdb -q ./keygenme_O0
(gdb) source strcmp_dump.py
[strcmp_dump] Breakpoint installé sur strcmp.
[strcmp_dump] Script chargé. Lancez le programme avec 'run'.
[strcmp_dump] Config: SHOW_LIVE=True, FILTER_EMPTY=True, MAX_STRING_LEN=256
(gdb) run
Enter your key: TEST-KEY-123
[strcmp #1] "TEST-KEY-123" vs "VALID-KEY-2025"  (caller: check_key @ 0x401189)  ✗
Wrong key!
[Inferior 1 (process 56789) exited with code 01]

======================================================================
  RAPPORT strcmp_dump — 1 appels capturés (3 total, 2 filtrés)
======================================================================

  Appels enregistrés :
     #  Match  Appelant              Chaîne 1                        Chaîne 2
     —  —————  ————————————————————  ——————————————————————————————  ——————————————————————————————
     1      ✗  check_key             TEST-KEY-123                    VALID-KEY-2025

  Chaînes uniques observées (2) :
    • TEST-KEY-123
    • VALID-KEY-2025

  Sites d'appel uniques (1) :
    0x401189 (check_key) — 1 appel(s)

======================================================================
```

La clé attendue (`VALID-KEY-2025`) apparaît directement dans le rapport. Sur un vrai crackme, c'est souvent tout ce qu'il faut.

### Sur le binaire strippé

```bash
$ gdb -q ./keygenme_O2_strip
(gdb) source strcmp_dump.py
[strcmp_dump] Breakpoint installé sur strcmp.
(gdb) run
Enter your key: AAAA
[strcmp #1] "AAAA" vs "VALID-KEY-2025"  (caller: ?? @ 0x401175)  ✗
Wrong key!
```

Le script fonctionne de manière identique. Le nom de l'appelant est `??` (pas de symboles), mais l'adresse `0x401175` peut être corrélée avec le désassemblage dans Ghidra. La chaîne attendue est toujours capturée.

### Avec pwntools

```python
#!/usr/bin/env python3
"""Lancer le keygenme sous GDB avec le script strcmp_dump."""
from pwn import *

context.terminal = ['tmux', 'splitw', '-h']

p = gdb.debug("./keygenme_O2_strip", '''
    source strcmp_dump.py
    continue
''')

p.sendlineafter(b"Enter your key: ", b"TEST")  
p.interactive()  
```

Le script `strcmp_dump.py` est chargé dans la session GDB ouverte par pwntools. Le rapport s'affiche dans le panneau GDB quand le programme se termine.

## Critères de validation

Le checkpoint est réussi si le script :

| Critère | Vérifié par |  
|---|---|  
| Se charge sans erreur dans GDB | `source strcmp_dump.py` ne produit pas de traceback Python |  
| Intercepte les appels à `strcmp` | Les appels sont affichés en temps réel pendant l'exécution |  
| Affiche les deux chaînes comparées | Les colonnes « Chaîne 1 » et « Chaîne 2 » sont remplies |  
| Identifie l'appelant | L'adresse (et le nom si disponible) du site d'appel est affiché |  
| Gère les adresses invalides | Pas de crash si un registre contient une adresse non mappée |  
| Produit un rapport à la fin | Le bloc récapitulatif s'affiche quand le programme se termine |  
| Fonctionne sur binaire strippé | Le script produit les mêmes résultats sur `keygenme_O2_strip` |  
| Est rechargeable | Un second `source strcmp_dump.py` ne crée pas de breakpoints en double |

## Pistes d'extension

Une fois le script de base fonctionnel, voici des améliorations possibles pour aller plus loin :

- **Supporter `strncmp` et `memcmp`** en ajoutant des breakpoints supplémentaires. Pour `memcmp`, le troisième argument (`rdx`) donne la taille à comparer — il faut lire exactement `rdx` octets au lieu de chercher un null terminator.  
- **Exporter en JSON** avec `json.dump()` dans le handler `on_exit`, pour une analyse ultérieure dans un script Python externe ou un notebook.  
- **Ajouter un filtre par appelant** : ne capturer que les `strcmp` appelés depuis le code du binaire (adresses dans `.text`) et ignorer ceux appelés depuis la glibc.  
- **Capturer la valeur de retour** en posant un second breakpoint sur l'instruction `ret` de `strcmp`, ou en utilisant un `FinishBreakpoint` (sous-classe de `gdb.Breakpoint` qui se déclenche au retour d'une fonction).

---

> **Validation :** si le script affiche la chaîne attendue (`VALID-KEY-2025` ou équivalent) lors de l'exécution du keygenme avec une clé quelconque, le checkpoint est réussi. Vous disposez maintenant d'un outil de traçage réutilisable pour tout binaire utilisant `strcmp` — un outil qui vous servira dès le chapitre 21 et tout au long de la Partie V.

⏭️ [Chapitre 12 — GDB amélioré : PEDA, GEF, pwndbg](/12-gdb-extensions/README.md)
