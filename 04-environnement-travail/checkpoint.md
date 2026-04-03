🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 🎯 Checkpoint — Chapitre 4

> **Objectif** : valider que votre environnement de reverse engineering est complet, fonctionnel et prêt à accueillir les 32 chapitres suivants. Ce checkpoint est le dernier verrou avant d'entrer dans le vif du sujet.

---

## Ce que vous devez avoir à ce stade

Si vous avez suivi les sections 4.1 à 4.7, votre environnement se compose de :

- une **VM** Ubuntu 24.04 LTS (ou Debian/Kali) en x86-64, avec au moins 4 Go de RAM et 60 Go de disque ;  
- **deux interfaces réseau** configurées : NAT (pour Internet) et host-only (pour l'isolation) ;  
- l'ensemble des **outils RE** installés (vagues 1 à 5 de la section 4.2) ;  
- un **environnement virtuel Python** (`re-venv`) contenant angr, pwntools, Frida, pyelftools, LIEF, Z3 et les autres bibliothèques ;  
- une **extension GDB** active (GEF, pwndbg ou PEDA) ;  
- tous les **binaires d'entraînement** compilés via `make all` dans `binaries/` ;  
- un **snapshot `tools-ready`** de la VM dans cet état.

---

## Validation : exécuter `check_env.sh`

Activez votre environnement virtuel Python, placez-vous à la racine du dépôt et lancez le script :

```bash
[vm] source ~/re-venv/bin/activate
[vm] cd ~/formation-re
[vm] chmod +x check_env.sh
[vm] ./check_env.sh
```

### Résultat attendu

Le script doit se terminer par le message :

```
  ✔ Votre environnement est prêt pour la formation.
```

avec **zéro échec critique** (`0 échec(s) critique(s)`).

Les éléments marqués `[~]` (optionnels manquants) sont acceptables — ils concernent des outils requis uniquement pour les parties bonus (VII, VIII) ou des composants non bloquants comme `kcachegrind`, `sysdig` ou `bindiff`. Vous pourrez les installer plus tard si nécessaire.

### Si des échecs critiques apparaissent

Chaque ligne `[✗]` dans la sortie du script identifie un outil manquant ou mal configuré. La section 4.7 détaille les causes et les corrections pour chaque cas courant. Le cycle est simple :

1. Identifiez les lignes `[✗]`.  
2. Appliquez la correction (généralement un `apt install` ou un `pip install`).  
3. Relancez `./check_env.sh`.  
4. Répétez jusqu'à zéro échec critique.

---

## Critères de réussite

| Critère | Attendu |  
|---|---|  
| Échecs critiques (`[✗]`) | **0** |  
| Socle système (gcc, g++, python3, java, gdb) | Tous `[✔]` |  
| Désassembleurs (Ghidra, Radare2, ImHex) | Tous `[✔]` |  
| Extension GDB (GEF, pwndbg ou PEDA) | Au moins une `[✔]` |  
| Environnement virtuel Python actif | `[✔]` |  
| Bibliothèques Python (angr, pwntools, frida, z3) | Toutes `[✔]` |  
| Binaires d'entraînement C/C++ (ch21–ch29) | Tous `[✔]` avec le bon nombre |  
| Configuration système (ptrace_scope, espace disque, RAM) | Pas de `[✗]` |  
| Code de sortie du script | `0` (succès) |

---

## Conserver une trace

Gardez un log daté de la sortie du script. Il servira de référence en cas de problème futur (mise à jour système, restauration de snapshot, migration de VM) :

```bash
[vm] ./check_env.sh | tee ~/check_env_$(date +%Y%m%d).log
```

---

## Prendre le snapshot de référence

Si ce n'est pas encore fait, c'est le moment de prendre (ou mettre à jour) le snapshot `tools-ready` de votre VM. Cet instantané représente l'état de référence de la formation : système installé, outils configurés, binaires compilés, tout au vert.

**VirtualBox** : Machine → Prendre un instantané → `tools-ready`

**QEMU/KVM** :
```bash
[hôte] virsh snapshot-create-as RE-Lab tools-ready --description "Chapitre 4 checkpoint OK"
```

**UTM** : icône appareil photo → `tools-ready`

> 💡 Si un snapshot `tools-ready` existait déjà, supprimez-le et recréez-le pour qu'il reflète l'état le plus récent.

---

## Et ensuite ?

Votre laboratoire est opérationnel. Vous disposez de tous les outils, de tous les binaires cibles et d'un environnement isolé et reproductible.

La **Partie II — Analyse Statique** commence au chapitre 5 avec les outils d'inspection binaire de base. Vous y mettrez immédiatement en pratique l'environnement construit ici : `file`, `strings`, `readelf`, `objdump`, `checksec` — tous vérifiés et prêts à l'emploi par `check_env.sh`.


⏭️ [Partie II — Analyse Statique](/partie-2-analyse-statique.md)
