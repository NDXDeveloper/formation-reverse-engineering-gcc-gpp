🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 35.5 — Intégration dans un pipeline CI/CD pour audit de régression binaire

> 🔧 **Outils couverts** : GitHub Actions, scripts shell, `lief`, `yara-python`, `readelf`, `checksec`  
> 📁 **Fichiers de référence** : `check_env.sh`, `yara-rules/*.yar`, `scripts/triage.py`  
> 🎯 **Objectif** : détecter automatiquement, à chaque commit, les régressions de sécurité et les anomalies dans les binaires produits par un projet

---

## Pourquoi auditer les binaires dans un pipeline

Tout au long de cette formation, nous avons analysé des binaires *après* leur production — en posture de reverse engineer, face à un artefact inconnu. Cette section renverse la perspective : on se place du côté du développeur ou de l'ingénieur sécurité qui *produit* les binaires, et on intègre les outils de RE dans le processus de build pour détecter les problèmes *avant* qu'un analyste externe ne les trouve.

Les régressions binaires sont des changements involontaires dans les propriétés d'un binaire entre deux versions. Elles passent inaperçues dans les tests fonctionnels classiques parce que le programme fonctionne correctement — c'est sa posture de sécurité ou sa surface d'attaque qui a changé. Quelques exemples concrets tirés de situations réelles :

Un développeur désactive temporairement `-fstack-protector` pour déboguer un crash obscur, oublie de le réactiver, et le binaire de production perd ses canaries de pile. Les tests unitaires passent. Le binaire est déployé. Six mois plus tard, un auditeur découvre l'absence de protection.

Une mise à jour de la chaîne de build passe de Full RELRO à Partial RELRO sans que personne ne le remarque — la table GOT redevient écrivable, ouvrant la voie à des attaques par réécriture de GOT.

Un binaire de release est livré avec les symboles de debug (`-g`) et sans stripping, exposant les noms de fonctions internes, les chemins de fichiers sources, et les noms de variables aux analystes adverses.

Une dépendance est ajoutée (`libcrypto`, `libcurl`) sans que l'équipe sécurité n'en soit informée — le binaire gagne une surface d'attaque supplémentaire.

Tous ces cas sont détectables automatiquement avec les outils vus dans ce chapitre. L'idée est de transformer les vérifications manuelles en assertions exécutées à chaque build, exactement comme on le fait pour les tests unitaires.

---

## Architecture du pipeline

Le pipeline d'audit binaire s'insère après l'étape de compilation et avant l'étape de déploiement. Il ne remplace pas les tests fonctionnels — il les complète avec des vérifications spécifiques aux propriétés binaires.

```
   ┌───────────┐     ┌──────────┐     ┌───────────────┐     ┌──────────┐
   │  Source   │────▶│  Build   │────▶│ Audit binaire │────▶│  Deploy  │
   │  (git)    │     │  (make)  │     │  (CI stage)   │     │          │
   └───────────┘     └──────────┘     └───────────────┘     └──────────┘
                                             │
                                      ┌──────┴──────┐
                                      │  Vérifier   │
                                      │  - checksec │
                                      │  - symboles │
                                      │  - deps     │
                                      │  - YARA     │
                                      │  - taille   │
                                      │  - entropie │
                                      └─────────────┘
```

Le stage d'audit produit un rapport JSON et un code de retour : 0 si toutes les vérifications passent, non-zéro si une régression est détectée. Le pipeline peut alors bloquer le déploiement ou simplement émettre un avertissement, selon la politique de l'équipe.

---

## Le script d'audit : `audit_binary.py`

Le cœur du pipeline est un script Python unique qui prend un binaire en entrée, exécute toutes les vérifications, et produit un rapport structuré. Il combine `lief` (section 35.1), `yara-python` (section 35.4), et des appels shell pour les outils qui n'ont pas de binding Python.

```python
#!/usr/bin/env python3
"""
audit_binary.py — Audit de sécurité automatisé d'un binaire ELF

Vérifie :
  1. Protections de compilation (PIE, NX, canary, RELRO)
  2. Présence/absence de symboles de debug
  3. Dépendances dynamiques (nouvelles libs, libs sensibles)
  4. Scan YARA (constantes crypto, signatures de packers)
  5. Entropie des sections (détection de packing)
  6. Taille du binaire (détection de bloat)

Usage :
  python3 audit_binary.py <binary> [--policy policy.json] [--output report.json]

Code de retour :
  0 = toutes les vérifications passent
  1 = au moins un FAIL
  2 = erreur d'exécution
"""

import argparse  
import json  
import sys  
import subprocess  
from pathlib import Path  

try:
    import lief
except ImportError:
    print("ERREUR : lief requis (pip install lief)", file=sys.stderr)
    sys.exit(2)

try:
    import yara
except ImportError:
    yara = None  # YARA optionnel, les checks YARA seront ignorés


# ── Politique par défaut ─────────────────────────────────────

DEFAULT_POLICY = {
    "require_pie":           True,
    "require_nx":            True,
    "require_canary":        True,
    "require_relro":         "full",     # "full", "partial", "none"
    "require_stripped":      True,
    "forbid_debug_symbols":  True,
    "max_size_bytes":        10_000_000,  # 10 Mo
    "entropy_threshold":     7.2,         # au-dessus = suspicion de packing
    "allowed_libraries":     [],          # vide = pas de restriction
    "forbidden_libraries":   [],          # ex: ["libasan.so"]
    "yara_rules_dir":        None,        # chemin vers les fichiers .yar
}


# ── Vérifications individuelles ──────────────────────────────

def check_protections(binary, policy):
    """Vérifie PIE, NX, canary, RELRO."""
    results = []

    # PIE
    if policy["require_pie"]:
        ok = binary.is_pie
        results.append({
            "check": "PIE",
            "status": "PASS" if ok else "FAIL",
            "detail": f"is_pie={binary.is_pie}",
        })

    # NX (No-Execute)
    if policy["require_nx"]:
        ok = binary.has_nx
        results.append({
            "check": "NX",
            "status": "PASS" if ok else "FAIL",
            "detail": f"has_nx={binary.has_nx}",
        })

    # Stack canary (détecté via import de __stack_chk_fail)
    if policy["require_canary"]:
        imports = {s.name for s in binary.imported_symbols if s.name}
        has_canary = "__stack_chk_fail" in imports
        results.append({
            "check": "Stack canary",
            "status": "PASS" if has_canary else "FAIL",
            "detail": f"__stack_chk_fail imported: {has_canary}",
        })

    # RELRO
    required = policy["require_relro"]
    if required != "none":
        has_relro = False
        has_bind_now = False
        for seg in binary.segments:
            if seg.type == lief.ELF.Segment.TYPE.GNU_RELRO:
                has_relro = True
        # Chercher BIND_NOW dans .dynamic
        try:
            for entry in binary.dynamic_entries:
                if entry.tag == lief.ELF.DynamicEntry.TAG.BIND_NOW:
                    has_bind_now = True
                if entry.tag == lief.ELF.DynamicEntry.TAG.FLAGS:
                    if entry.value & 0x08:  # DF_BIND_NOW
                        has_bind_now = True
        except Exception:
            pass

        if required == "full":
            ok = has_relro and has_bind_now
            detail = f"GNU_RELRO={has_relro}, BIND_NOW={has_bind_now}"
        else:  # partial
            ok = has_relro
            detail = f"GNU_RELRO={has_relro}"

        results.append({
            "check": f"RELRO ({required})",
            "status": "PASS" if ok else "FAIL",
            "detail": detail,
        })

    return results


def check_symbols(binary, policy):
    """Vérifie la présence/absence de symboles et infos de debug."""
    results = []

    static_syms = list(binary.static_symbols)
    has_symtab = len(static_syms) > 0

    # Debug sections
    debug_sections = [s.name for s in binary.sections
                      if s.name.startswith(".debug_")]

    if policy["require_stripped"]:
        results.append({
            "check": "Stripped (.symtab)",
            "status": "FAIL" if has_symtab else "PASS",
            "detail": f"{len(static_syms)} static symbols found",
        })

    if policy["forbid_debug_symbols"]:
        ok = len(debug_sections) == 0
        results.append({
            "check": "No debug sections",
            "status": "PASS" if ok else "FAIL",
            "detail": f"debug sections: {debug_sections}" if debug_sections
                      else "none",
        })

    return results


def check_libraries(binary, policy):
    """Vérifie les dépendances dynamiques."""
    results = []
    libs = list(binary.libraries)

    # Bibliothèques interdites (ex: libasan = sanitizer oublié en prod)
    forbidden = policy.get("forbidden_libraries", [])
    found_forbidden = [lib for lib in libs
                       if any(f in lib for f in forbidden)]
    if forbidden:
        results.append({
            "check": "No forbidden libraries",
            "status": "FAIL" if found_forbidden else "PASS",
            "detail": f"forbidden found: {found_forbidden}" if found_forbidden
                      else f"libs: {libs}",
        })

    # Bibliothèques autorisées (whitelist stricte)
    allowed = policy.get("allowed_libraries", [])
    if allowed:
        unexpected = [lib for lib in libs
                      if not any(a in lib for a in allowed)]
        results.append({
            "check": "Only allowed libraries",
            "status": "FAIL" if unexpected else "PASS",
            "detail": f"unexpected: {unexpected}" if unexpected
                      else f"libs: {libs}",
        })

    # Toujours lister les dépendances dans le rapport (informatif)
    results.append({
        "check": "Library inventory",
        "status": "INFO",
        "detail": f"{len(libs)} libraries: {libs}",
    })

    return results


def check_entropy(binary, policy):
    """Détecte les sections à entropie anormalement élevée."""
    results = []
    threshold = policy["entropy_threshold"]
    high_entropy = []

    for section in binary.sections:
        if section.size == 0:
            continue
        e = section.entropy
        if e > threshold:
            high_entropy.append(f"{section.name} ({e:.2f})")

    results.append({
        "check": f"Entropy < {threshold}",
        "status": "FAIL" if high_entropy else "PASS",
        "detail": f"high entropy: {high_entropy}" if high_entropy
                  else "all sections normal",
    })

    return results


def check_size(binary_path, policy):
    """Vérifie que la taille du binaire reste dans les limites."""
    max_size = policy["max_size_bytes"]
    actual = Path(binary_path).stat().st_size

    return [{
        "check": f"Size < {max_size // 1_000_000}MB",
        "status": "FAIL" if actual > max_size else "PASS",
        "detail": f"{actual:,} bytes",
    }]


def check_yara(binary_path, policy):
    """Exécute les règles YARA si configurées."""
    rules_dir = policy.get("yara_rules_dir")
    if not rules_dir or yara is None:
        return []

    rules_dir = Path(rules_dir)
    if not rules_dir.is_dir():
        return [{"check": "YARA scan", "status": "SKIP",
                 "detail": f"rules dir not found: {rules_dir}"}]

    rule_files = {}
    for i, path in enumerate(sorted(rules_dir.glob("*.yar"))):
        rule_files[f"ns_{i}"] = str(path)

    if not rule_files:
        return [{"check": "YARA scan", "status": "SKIP",
                 "detail": "no .yar files found"}]

    try:
        rules = yara.compile(filepaths=rule_files)
        matches = rules.match(str(binary_path))
    except yara.Error as e:
        return [{"check": "YARA scan", "status": "ERROR",
                 "detail": str(e)}]

    match_names = [m.rule for m in matches]
    return [{
        "check": "YARA scan",
        "status": "INFO",
        "detail": f"{len(match_names)} matches: {match_names}"
                  if match_names else "no matches",
    }]


# ── Orchestration ────────────────────────────────────────────

def audit(binary_path, policy):
    """Exécute toutes les vérifications et retourne le rapport."""
    binary = lief.parse(str(binary_path))
    if binary is None:
        return {"error": f"Cannot parse {binary_path}"}, 2

    results = []
    results.extend(check_protections(binary, policy))
    results.extend(check_symbols(binary, policy))
    results.extend(check_libraries(binary, policy))
    results.extend(check_entropy(binary, policy))
    results.extend(check_size(binary_path, policy))
    results.extend(check_yara(binary_path, policy))

    fails = [r for r in results if r["status"] == "FAIL"]

    report = {
        "binary": str(binary_path),
        "total_checks": len([r for r in results if r["status"] != "INFO"]),
        "passed": len([r for r in results if r["status"] == "PASS"]),
        "failed": len(fails),
        "results": results,
    }

    exit_code = 1 if fails else 0
    return report, exit_code


# ── Point d'entrée ───────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Audit binaire ELF")
    parser.add_argument("binary", help="Chemin vers le binaire à auditer")
    parser.add_argument("--policy", help="Fichier de politique JSON",
                        default=None)
    parser.add_argument("--output", help="Fichier de sortie JSON",
                        default=None)
    args = parser.parse_args()

    # Charger la politique
    policy = dict(DEFAULT_POLICY)
    if args.policy:
        with open(args.policy) as f:
            overrides = json.load(f)
        policy.update(overrides)

    # Exécuter l'audit
    report, code = audit(args.binary, policy)

    # Affichage console
    if "error" in report:
        print(f"ERREUR : {report['error']}", file=sys.stderr)
    else:
        for r in report["results"]:
            icon = {"PASS": "✅", "FAIL": "❌", "INFO": "ℹ️",
                    "SKIP": "⏭️", "ERROR": "⚠️"}.get(r["status"], "?")
            print(f"  {icon} {r['check']:<30} {r['detail']}")
        print()
        print(f"  Résultat : {report['passed']}/{report['total_checks']} "
              f"checks passed"
              + (f" — {report['failed']} FAIL(s)" if report['failed'] else ""))

    # Sortie JSON
    if args.output:
        with open(args.output, "w") as f:
            json.dump(report, f, indent=2)

    sys.exit(code)
```

### Fichier de politique

La politique d'audit est externalisée dans un fichier JSON, ce qui permet d'adapter les règles sans modifier le script. Chaque projet peut avoir sa propre politique :

```json
{
    "require_pie": true,
    "require_nx": true,
    "require_canary": true,
    "require_relro": "full",
    "require_stripped": true,
    "forbid_debug_symbols": true,
    "max_size_bytes": 5000000,
    "entropy_threshold": 7.2,
    "forbidden_libraries": [
        "libasan.so",
        "libtsan.so",
        "libubsan.so",
        "libmsan.so"
    ],
    "allowed_libraries": [
        "libc.so",
        "libpthread.so",
        "libm.so",
        "libdl.so",
        "librt.so",
        "ld-linux"
    ],
    "yara_rules_dir": "yara-rules/"
}
```

Les quatre bibliothèques interdites (`libasan`, `libtsan`, `libubsan`, `libmsan`) sont les sanitizers GCC/Clang — compilés avec `-fsanitize=address|thread|undefined|memory`. Leur présence dans un binaire de production est un indicateur fiable de mauvaise hygiène de build. La whitelist `allowed_libraries` restreint les dépendances à un noyau minimal ; toute bibliothèque non listée provoquera un FAIL. Pour un binaire comme `crypto_O0` qui dépend de `libcrypto.so`, il faudra ajouter `libcrypto.so` à la whitelist dans la politique du projet.

---

## Pipeline GitHub Actions

Le script d'audit s'intègre dans un workflow GitHub Actions qui se déclenche à chaque push ou pull request.

```yaml
# .github/workflows/binary-audit.yml
name: Binary Security Audit

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

jobs:
  build-and-audit:
    runs-on: ubuntu-latest

    steps:
      # ── Récupération du code ──────────────────────────────
      - name: Checkout
        uses: actions/checkout@v4

      # ── Installation des dépendances ──────────────────────
      - name: Install system dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y gcc g++ make libssl-dev yara
          # checksec (script bash)
          wget -q https://raw.githubusercontent.com/slimm609/checksec.sh/main/checksec \
               -O /usr/local/bin/checksec
          chmod +x /usr/local/bin/checksec

      - name: Install Python dependencies
        run: |
          pip install lief yara-python

      # ── Compilation des binaires ──────────────────────────
      - name: Build all binaries
        run: |
          cd binaries && make all

      # ── Audit de chaque binaire cible ─────────────────────
      - name: Audit keygenme (release variant)
        run: |
          python3 scripts/audit_binary.py \
              binaries/ch21-keygenme/keygenme_O2_strip \
              --policy policies/keygenme_policy.json \
              --output reports/keygenme_audit.json

      - name: Audit crypto (release variant)
        run: |
          python3 scripts/audit_binary.py \
              binaries/ch24-crypto/crypto_O2_strip \
              --policy policies/crypto_policy.json \
              --output reports/crypto_audit.json

      - name: Audit fileformat (release variant)
        run: |
          python3 scripts/audit_binary.py \
              binaries/ch25-fileformat/fileformat_O2_strip \
              --policy policies/default_policy.json \
              --output reports/fileformat_audit.json

      # ── Scan YARA global ──────────────────────────────────
      - name: YARA scan (all binaries)
        run: |
          yara -r yara-rules/crypto_constants.yar binaries/ > reports/yara_crypto.txt || true
          yara -r yara-rules/packer_signatures.yar binaries/ > reports/yara_packers.txt || true
          echo "=== Crypto constants ===" && cat reports/yara_crypto.txt
          echo "=== Packer signatures ===" && cat reports/yara_packers.txt

      # ── Publication des rapports ──────────────────────────
      - name: Upload audit reports
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: binary-audit-reports
          path: reports/
          retention-days: 30

      # ── Vérification checksec (informatif) ────────────────
      - name: checksec summary
        if: always()
        run: |
          echo "=== checksec — keygenme variants ==="
          for bin in binaries/ch21-keygenme/keygenme_*; do
            echo "--- $bin ---"
            checksec --file="$bin" || true
          done
```

### Décomposition des étapes

**Build** — Le pipeline compile tous les binaires avec `make all`. Cela garantit que l'audit porte sur des binaires produits à partir du code source actuel, pas sur des artefacts cachés.

**Audit ciblé** — Chaque binaire de release (variante `-O2` strippée) est audité individuellement avec sa propre politique. Le script `audit_binary.py` retourne un code non-zéro en cas de FAIL, ce qui fait échouer le step GitHub Actions et bloque la pull request.

**YARA global** — Un scan de l'ensemble du répertoire `binaries/` avec les règles YARA détecte les patterns au niveau du corpus. Le `|| true` empêche le pipeline d'échouer sur le scan YARA (qui est informatif, pas bloquant).

**Rapports** — Tous les fichiers JSON et texte sont publiés comme artefacts du workflow, téléchargeables pendant 30 jours. Cela constitue une trace d'audit horodatée.

---

## Détection de régressions par comparaison

L'audit d'un seul build est utile, mais la vraie puissance du pipeline apparaît quand on compare deux builds successifs. La comparaison permet de détecter les *changements* — une protection qui disparaît, une bibliothèque qui apparaît, une section dont l'entropie augmente brusquement.

```python
#!/usr/bin/env python3
"""
diff_audits.py — Compare deux rapports d'audit et signale les régressions.

Usage : python3 diff_audits.py <old_report.json> <new_report.json>

Code de retour :
  0 = pas de régression
  1 = au moins une régression détectée
"""

import json  
import sys  

def load(path):
    with open(path) as f:
        return json.load(f)

def diff_reports(old, new):
    regressions = []

    old_results = {r["check"]: r for r in old["results"]}
    new_results = {r["check"]: r for r in new["results"]}

    for check, new_r in new_results.items():
        old_r = old_results.get(check)

        if old_r is None:
            continue  # Nouveau check, pas de comparaison possible

        # Régression : PASS -> FAIL
        if old_r["status"] == "PASS" and new_r["status"] == "FAIL":
            regressions.append({
                "check": check,
                "was": old_r["detail"],
                "now": new_r["detail"],
            })

    return regressions

if __name__ == "__main__":
    old = load(sys.argv[1])
    new = load(sys.argv[2])

    regressions = diff_reports(old, new)

    if regressions:
        print(f"❌ {len(regressions)} régression(s) détectée(s) :\n")
        for r in regressions:
            print(f"  [{r['check']}]")
            print(f"    Avant : {r['was']}")
            print(f"    Après : {r['now']}")
            print()
        sys.exit(1)
    else:
        print("✅ Aucune régression détectée.")
        sys.exit(0)
```

Dans le pipeline, on conserve le rapport du dernier build validé (branche `main`) et on le compare avec celui du build courant :

```yaml
      - name: Download baseline report
        uses: actions/download-artifact@v4
        with:
          name: binary-audit-reports
          path: baseline/
        continue-on-error: true   # Pas de baseline au premier run

      - name: Check for regressions
        if: hashFiles('baseline/keygenme_audit.json') != ''
        run: |
          python3 scripts/diff_audits.py \
              baseline/keygenme_audit.json \
              reports/keygenme_audit.json
```

Un changement de `PASS` → `FAIL` sur n'importe quel check (canary disparu, symboles de debug réapparus, bibliothèque interdite ajoutée) bloquera la PR avec un message clair indiquant ce qui a régressé et ce qui était attendu.

---

## Variantes de déploiement

### GitLab CI

Le même script s'adapte à GitLab CI avec une syntaxe légèrement différente :

```yaml
# .gitlab-ci.yml
binary-audit:
  stage: test
  image: ubuntu:22.04
  before_script:
    - apt-get update && apt-get install -y gcc make libssl-dev python3-pip yara
    - pip3 install lief yara-python
  script:
    - cd binaries && make all && cd ..
    - python3 scripts/audit_binary.py binaries/ch21-keygenme/keygenme_O2_strip
        --policy policies/keygenme_policy.json
        --output reports/keygenme_audit.json
  artifacts:
    paths:
      - reports/
    expire_in: 30 days
```

### Script local (pre-commit hook)

Pour les développeurs qui veulent vérifier avant de pousser, un hook Git `pre-commit` exécute l'audit localement :

```bash
#!/bin/bash
# .git/hooks/pre-commit

echo "=== Audit binaire pre-commit ==="

# Recompiler les binaires modifiés
make -C binaries all 2>/dev/null

# Auditer les variantes de release
for bin in binaries/ch21-keygenme/keygenme_O2_strip \
           binaries/ch24-crypto/crypto_O2_strip \
           binaries/ch25-fileformat/fileformat_O2_strip; do
    if [ -f "$bin" ]; then
        python3 scripts/audit_binary.py "$bin" --policy policies/default_policy.json
        if [ $? -ne 0 ]; then
            echo ""
            echo "❌ Audit échoué pour $bin — commit bloqué."
            echo "   Corrigez les problèmes puis réessayez."
            exit 1
        fi
    fi
done

echo "✅ Tous les audits passent."
```

---

## Exemples de régressions détectables

Pour rendre concret ce que le pipeline attrape, voici les régressions typiques appliquées à nos binaires d'entraînement et la réponse attendue du pipeline.

| Scénario | Modification | Détection |  
|---|---|---|  
| Canary désactivé | Retirer `-fstack-protector` du Makefile | `check_protections` → FAIL (pas de `__stack_chk_fail`) |  
| Symboles en production | Oublier `strip` dans la cible release | `check_symbols` → FAIL (`.symtab` présent) |  
| Debug info en production | Laisser `-g` dans les `CFLAGS` release | `check_symbols` → FAIL (sections `.debug_*`) |  
| Sanitizer oublié | `-fsanitize=address` dans le build release | `check_libraries` → FAIL (`libasan.so` interdit) |  
| RELRO réduit | Passer de `-Wl,-z,relro,-z,now` à `-Wl,-z,relro` | `check_protections` → FAIL (BIND_NOW absent) |  
| Nouvelle dépendance | Ajouter `libcurl` sans mise à jour de la politique | `check_libraries` → FAIL (lib hors whitelist) |  
| Binaire packé par erreur | `upx` appliqué en CI | `check_entropy` → FAIL (entropie > 7.2) |  
| PIE désactivé | `-no-pie` ajouté au linker | `check_protections` → FAIL (`is_pie=False`) |

Chaque ligne de ce tableau correspond à une erreur réelle observée dans des projets de production. Le pipeline les détecte toutes automatiquement, sans intervention humaine.

---

## Limites et extensions

Le pipeline présenté ici est un socle minimal. Voici les extensions les plus utiles en pratique, que l'on n'implémente pas ici mais qui se greffent naturellement sur l'architecture.

**Comparaison de taille de `.text`** — Au-delà de la taille totale du binaire, surveiller la taille de `.text` entre deux builds détecte les ajouts de code inattendus (code mort, fonctions de debug oubliées). Un seuil de variation de 5 % au-dessus de la moyenne récente est un bon point de départ.

**Analyse des symboles exportés** — Pour les bibliothèques partagées (`.so`), vérifier que la surface d'API exportée ne change pas involontairement. Un symbole nouvellement exporté est un point d'entrée supplémentaire pour un attaquant.

**Vérification de la chaîne de compilation** — Le section `.comment` d'un ELF contient la version du compilateur (ex : `GCC: (Ubuntu 13.2.0-23ubuntu4) 13.2.0`). Surveiller que cette chaîne ne change pas entre les builds garantit la reproductibilité.

**Intégration avec Ghidra headless** — Pour les projets sensibles, déclencher une analyse Ghidra automatique (section 35.2) sur les binaires de release et comparer le graphe de fonctions entre deux versions. C'est plus lourd, mais détecte des changements structurels invisibles aux vérifications superficielles.

---


⏭️ [Construire son propre toolkit RE : organiser ses scripts et snippets](/35-automatisation-scripting/06-construire-toolkit.md)
