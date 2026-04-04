#!/usr/bin/env python3
"""
batch_analyze.py — Analyse batch de binaires via Ghidra headless
Formation Reverse Engineering — Applications compilées avec la chaîne GNU

Wrapper Python autour de `analyzeHeadless` qui orchestre :
  1. Import et auto-analyse d'un répertoire de binaires
  2. Exécution de scripts Ghidra post-analyse (extraction de fonctions,
     décompilation, scan de constantes crypto)
  3. Consolidation des rapports JSON produits par les scripts

Ce script remplace le pipeline shell `batch_ghidra.sh` de la section 35.2
par une version Python plus portable et plus facile à intégrer dans un
pipeline CI/CD (section 35.5).

Prérequis :
  - Ghidra installé, variable GHIDRA_HOME définie
  - Scripts Ghidra dans le répertoire spécifié par --scripts
  - Java 17+ (requis par Ghidra 10+)

Usage :
  python3 batch_analyze.py binaries/ch21-keygenme/
  python3 batch_analyze.py binaries/ --scripts ghidra_scripts/ --output reports/
  python3 batch_analyze.py binaries/ --timeout 300 --max-cpu 4

Licence MIT — Usage strictement éducatif.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


# ═══════════════════════════════════════════════════════════════
#  Localisation de Ghidra
# ═══════════════════════════════════════════════════════════════

def find_headless():
    """Localise le script analyzeHeadless de Ghidra."""
    # 1. Variable d'environnement GHIDRA_HOME
    ghidra_home = os.environ.get("GHIDRA_HOME")
    if ghidra_home:
        candidate = Path(ghidra_home) / "support" / "analyzeHeadless"
        if candidate.is_file():
            return str(candidate)

    # 2. Alias/commande dans le PATH
    which = shutil.which("analyzeHeadless")
    if which:
        return which

    # 3. Emplacements courants
    for path in [
        "/opt/ghidra/support/analyzeHeadless",
        "/usr/local/ghidra/support/analyzeHeadless",
        Path.home() / "ghidra" / "support" / "analyzeHeadless",
    ]:
        if Path(path).is_file():
            return str(path)

    return None


# ═══════════════════════════════════════════════════════════════
#  Détection des ELF
# ═══════════════════════════════════════════════════════════════

def find_elfs(directory):
    """Trouve les fichiers ELF dans un répertoire (récursif)."""
    elfs = []
    for path in sorted(Path(directory).rglob("*")):
        if not path.is_file():
            continue
        try:
            with open(path, "rb") as f:
                if f.read(4) == b"\x7fELF":
                    elfs.append(path)
        except (OSError, PermissionError):
            continue
    return elfs


# ═══════════════════════════════════════════════════════════════
#  Exécution de analyzeHeadless
# ═══════════════════════════════════════════════════════════════

def run_headless(headless_path, project_dir, project_name, args_list,
                 env_extra=None, verbose=False):
    """Lance analyzeHeadless avec les arguments donnés.

    Retourne (code_retour, stdout, stderr).
    """
    cmd = [headless_path, str(project_dir), project_name] + args_list

    env = os.environ.copy()
    if env_extra:
        env.update(env_extra)

    if verbose:
        print(f"  CMD: {' '.join(cmd[:6])}...", file=sys.stderr)

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        env=env,
        timeout=3600,  # 1h max global
    )

    return result.returncode, result.stdout, result.stderr


# ═══════════════════════════════════════════════════════════════
#  Phases du pipeline
# ═══════════════════════════════════════════════════════════════

def phase_import(headless, project_dir, project_name, binaries_dir,
                 timeout_per_file, max_cpu, verbose):
    """Phase 1 : import et auto-analyse de tous les binaires."""
    print("[*] Phase 1 : Import et analyse...", file=sys.stderr)

    args = [
        "-import", str(binaries_dir),
        "-recursive",
        "-overwrite",
        "-analysisTimeoutPerFile", str(timeout_per_file),
    ]
    if max_cpu:
        args += ["-max-cpu", str(max_cpu)]

    code, stdout, stderr = run_headless(
        headless, project_dir, project_name, args, verbose=verbose
    )

    if code != 0:
        print(f"  AVERTISSEMENT : analyzeHeadless retour {code}",
              file=sys.stderr)
        if verbose:
            # Afficher les dernières lignes d'erreur
            for line in stderr.splitlines()[-10:]:
                print(f"    {line}", file=sys.stderr)

    # Compter les binaires importés (heuristique sur la sortie)
    imported = stdout.count("Import succeeded")
    print(f"  [{imported} binaire(s) importé(s)]", file=sys.stderr)
    return code


def phase_script(headless, project_dir, project_name, script_path,
                 script_args, output_dir, verbose):
    """Phase N : exécuter un script post-analyse sur tous les binaires."""
    script_name = Path(script_path).name
    print(f"[*] Exécution de {script_name}...", file=sys.stderr)

    args = [
        "-process",           # traiter tous les binaires du projet
        "-noanalysis",        # ne pas relancer l'auto-analyse
        "-postScript", str(script_path),
    ]
    if script_args:
        args.extend(script_args)

    env_extra = {"GHIDRA_OUTPUT": str(output_dir)}

    code, stdout, stderr = run_headless(
        headless, project_dir, project_name, args,
        env_extra=env_extra, verbose=verbose,
    )

    if code != 0 and verbose:
        for line in stderr.splitlines()[-5:]:
            print(f"    {line}", file=sys.stderr)

    return code


# ═══════════════════════════════════════════════════════════════
#  Consolidation des rapports
# ═══════════════════════════════════════════════════════════════

def merge_reports(output_dir):
    """Fusionne les JSON produits par les scripts Ghidra."""
    report = {}

    for json_path in sorted(Path(output_dir).glob("*.json")):
        try:
            with open(json_path) as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError) as e:
            print(f"  AVERTISSEMENT : {json_path.name} illisible ({e})",
                  file=sys.stderr)
            continue

        # Identifier le type de rapport par son contenu
        binary_name = data.get("binary", json_path.stem)

        report.setdefault(binary_name, {})

        if "functions" in data:
            report[binary_name]["functions"] = data
        elif "findings" in data:
            report[binary_name]["crypto"] = data
        else:
            # Rapport générique
            report[binary_name][json_path.stem] = data

    return report


def build_final_report(merged, binaries_dir, output_dir):
    """Construit le rapport final consolidé."""
    summary = []
    for name, data in sorted(merged.items()):
        func_count = data.get("functions", {}).get("count", 0)
        crypto_count = len(data.get("crypto", {}).get("findings", []))
        summary.append({
            "binary":              name,
            "functions_detected":  func_count,
            "crypto_constants":    crypto_count,
        })

    return {
        "metadata": {
            "timestamp":      time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "binaries_dir":   str(Path(binaries_dir).resolve()),
            "tool":           "batch_analyze.py (Ghidra headless)",
        },
        "summary": summary,
        "details": merged,
    }


# ═══════════════════════════════════════════════════════════════
#  Point d'entrée
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="Analyse batch de binaires via Ghidra headless",
        epilog="Formation RE — Chapitre 35",
    )
    parser.add_argument(
        "binaries_dir",
        help="Répertoire contenant les binaires à analyser",
    )
    parser.add_argument(
        "--scripts", "-s",
        help="Répertoire contenant les scripts Ghidra "
             "(défaut: static/ghidra/ ou ghidra_scripts/)",
        default=None,
    )
    parser.add_argument(
        "--output", "-o",
        help="Répertoire de sortie pour les rapports "
             "(défaut: /tmp/ghidra_batch_output/)",
        default=None,
    )
    parser.add_argument(
        "--timeout", "-t",
        help="Timeout d'analyse par fichier en secondes (défaut: 300)",
        type=int, default=300,
    )
    parser.add_argument(
        "--max-cpu",
        help="Nombre de threads pour l'analyse (défaut: auto)",
        type=int, default=None,
    )
    parser.add_argument(
        "--keep-project",
        help="Ne pas supprimer le projet Ghidra temporaire",
        action="store_true",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Afficher la progression détaillée",
    )
    args = parser.parse_args()

    # ── Vérifications ──

    headless = find_headless()
    if headless is None:
        print("ERREUR : analyzeHeadless non trouvé.", file=sys.stderr)
        print("  Définissez GHIDRA_HOME ou ajoutez Ghidra au PATH.",
              file=sys.stderr)
        sys.exit(2)
    print(f"[*] Ghidra headless : {headless}", file=sys.stderr)

    binaries_dir = Path(args.binaries_dir)
    if not binaries_dir.is_dir():
        print(f"ERREUR : '{binaries_dir}' n'est pas un répertoire",
              file=sys.stderr)
        sys.exit(2)

    elfs = find_elfs(binaries_dir)
    if not elfs:
        print(f"Aucun ELF trouvé dans {binaries_dir}", file=sys.stderr)
        sys.exit(0)
    print(f"[*] {len(elfs)} binaire(s) ELF détecté(s)", file=sys.stderr)

    # ── Répertoire des scripts Ghidra ──

    scripts_dir = None
    if args.scripts:
        scripts_dir = Path(args.scripts)
    else:
        # Chercher dans les emplacements conventionnels
        for candidate in ["static/ghidra", "ghidra_scripts", "scripts/ghidra"]:
            if Path(candidate).is_dir():
                scripts_dir = Path(candidate)
                break

    scripts = []
    if scripts_dir and scripts_dir.is_dir():
        scripts = sorted(scripts_dir.glob("*.py"))
        print(f"[*] {len(scripts)} script(s) Ghidra dans {scripts_dir}",
              file=sys.stderr)
    else:
        print("[*] Aucun répertoire de scripts Ghidra trouvé — "
              "import et analyse uniquement", file=sys.stderr)

    # ── Répertoires de travail ──

    output_dir = Path(args.output) if args.output else Path(
        tempfile.mkdtemp(prefix="ghidra_batch_output_")
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    project_dir = Path(tempfile.mkdtemp(prefix="ghidra_batch_project_"))
    project_name = "batch"

    print(f"[*] Projet Ghidra : {project_dir}/{project_name}", file=sys.stderr)
    print(f"[*] Sortie         : {output_dir}", file=sys.stderr)

    # ── Phase 1 : Import ──

    phase_import(
        headless, project_dir, project_name,
        binaries_dir, args.timeout, args.max_cpu, args.verbose,
    )

    # ── Phases 2..N : Scripts ──

    for script_path in scripts:
        phase_script(
            headless, project_dir, project_name,
            script_path, [], output_dir, args.verbose,
        )

    # ── Consolidation ──

    print("[*] Consolidation des rapports...", file=sys.stderr)
    merged = merge_reports(output_dir)
    final = build_final_report(merged, binaries_dir, output_dir)

    report_path = output_dir / "report.json"
    with open(report_path, "w") as f:
        json.dump(final, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # ── Résumé ──

    print(f"\n{'='*50}", file=sys.stderr)
    print(f"  Rapport : {report_path}", file=sys.stderr)
    print(f"  Binaires analysés : {len(elfs)}", file=sys.stderr)
    for s in final["summary"]:
        flags = []
        if s["crypto_constants"] > 0:
            flags.append(f"CRYPTO({s['crypto_constants']})")
        flag_str = f"  [{', '.join(flags)}]" if flags else ""
        print(f"    {s['binary']:<30} "
              f"{s['functions_detected']:>4} functions{flag_str}",
              file=sys.stderr)
    print(f"{'='*50}\n", file=sys.stderr)

    # ── Nettoyage ──

    if not args.keep_project:
        shutil.rmtree(project_dir, ignore_errors=True)
        if args.verbose:
            print(f"[*] Projet temporaire supprimé", file=sys.stderr)

    # Sortie JSON sur stdout aussi
    print(json.dumps(final, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
