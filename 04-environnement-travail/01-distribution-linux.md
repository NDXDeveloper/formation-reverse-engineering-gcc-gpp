🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 4.1 — Distribution Linux recommandée (Ubuntu/Debian/Kali)

> 🎯 **Objectif de cette section** : choisir la distribution Linux qui servira de base à votre VM de reverse engineering, en comprenant les critères qui motivent ce choix.

---

## Pourquoi Linux ?

La quasi-totalité des outils de reverse engineering que nous utiliserons dans cette formation sont nativement développés pour Linux ou y trouvent leur meilleur support. GDB, Radare2, Frida, AFL++, angr, pwntools, les binutils GNU — tous sont des projets nés dans l'écosystème Unix/Linux. Même Ghidra, qui est multiplateforme, s'intègre plus naturellement dans un workflow Linux où l'on passe constamment du terminal au désassembleur.

Au-delà des outils, les **cibles** de cette formation sont des binaires ELF compilés avec GCC/G++. Travailler sous Linux signifie pouvoir les exécuter, les déboguer et les instrumenter directement, sans couche d'émulation ni de compatibilité.

Enfin, la maîtrise de la ligne de commande Linux est une compétence fondamentale en RE. Les analystes passent une part significative de leur temps dans un terminal — pour lancer des scripts, inspecter la mémoire d'un processus, capturer du trafic réseau ou automatiser des tâches répétitives.

---

## Les trois candidates

Trois distributions reviennent systématiquement dans les environnements de RE et de sécurité offensive. Toutes trois appartiennent à la famille Debian et partagent le gestionnaire de paquets `apt`, ce qui facilite la transposition des instructions d'installation d'une distribution à l'autre.

### Ubuntu LTS — le choix par défaut de cette formation

Ubuntu LTS (Long Term Support) est la distribution que nous recommandons comme base de travail. Voici pourquoi :

**Disponibilité des paquets.** Les dépôts Ubuntu contiennent la grande majorité des outils dont nous avons besoin, soit directement dans les dépôts officiels, soit via des PPA maintenus par la communauté. Pour les outils non empaquetés (Ghidra, ImHex, GEF…), l'installation manuelle est systématiquement documentée sur Ubuntu en priorité par les mainteneurs de ces projets.

**Stabilité et cycle de support.** Une version LTS est supportée cinq ans. Cela signifie que les instructions d'installation de ce tutoriel resteront valides longtemps sans nécessiter de mises à jour. À l'heure de la rédaction, **Ubuntu 24.04 LTS (Noble Numbat)** est la version recommandée.

**Documentation et communauté.** Lorsqu'un problème survient — une dépendance manquante, un conflit de versions, un outil qui refuse de se lancer — la probabilité de trouver une solution sur un forum, un issue GitHub ou Stack Overflow est maximale avec Ubuntu. C'est un avantage pragmatique qu'il ne faut pas sous-estimer.

**Légèreté possible.** Pour une VM dédiée au RE, il n'est pas nécessaire d'installer un environnement de bureau lourd. Ubuntu Server (sans GUI) ou Ubuntu avec un bureau minimal (Xfce, LXQt) permet de limiter la consommation de RAM et de disque, en ne lançant une interface graphique que pour les outils qui l'exigent (Ghidra, ImHex, Cutter).

> 📌 **Version recommandée** : Ubuntu 24.04 LTS, architecture **amd64** (x86-64).  
> Si vous utilisez un Mac Apple Silicon, la section 4.3 détaille la marche à suivre avec UTM et l'émulation x86-64.

### Debian stable — l'alternative minimaliste

Debian stable est le socle sur lequel Ubuntu est construit. Elle offre une stabilité encore supérieure, avec des paquets rigoureusement testés. En contrepartie, les versions des logiciels dans les dépôts stables sont souvent plus anciennes que dans Ubuntu.

**Quand choisir Debian plutôt qu'Ubuntu ?**

- Vous préférez un système épuré sans les ajouts spécifiques à Canonical (snap, etc.).  
- Vous êtes déjà à l'aise avec Debian et ne voulez pas changer vos habitudes.  
- Vous montez un lab sur une machine physique à ressources très limitées.

En pratique, toutes les commandes `apt install` de cette formation fonctionnent aussi bien sur Debian que sur Ubuntu. Les rares divergences (noms de paquets, versions disponibles) sont signalées lorsqu'elles se présentent.

### Kali Linux — la distribution spécialisée sécurité

Kali Linux est la distribution la plus connue dans le domaine de la sécurité offensive. Elle est maintenue par OffSec (anciennement Offensive Security) et intègre des centaines d'outils préinstallés, dont beaucoup sont pertinents pour le RE : GDB, Radare2, Ghidra, binutils, pwntools, checksec, strace, ltrace, Wireshark, et bien d'autres.

**Avantages pour cette formation :**

- De nombreux outils sont **déjà installés** dès le premier démarrage. Cela réduit considérablement le temps de mise en place.  
- Kali est livrée avec des **configurations pré-optimisées** pour le travail en sécurité (permissions, chemins, aliases).  
- Les images VM officielles (pour VirtualBox, VMware, QEMU) sont directement téléchargeables et prêtes à l'emploi.

**Inconvénients à connaître :**

- Kali est une distribution de type **rolling release**. Les paquets sont mis à jour fréquemment, ce qui peut occasionnellement casser une dépendance ou modifier un comportement attendu. C'est l'opposé de la stabilité recherchée avec Ubuntu LTS.  
- Le système est conçu pour être utilisé en tant que **root** par défaut (même si les versions récentes créent un utilisateur non-root `kali`). Cette philosophie, adaptée au pentest ponctuel, est discutable pour un environnement d'apprentissage quotidien.  
- La **quantité d'outils préinstallés** est à double tranchant : l'empreinte disque et mémoire est plus élevée, et il est facile de se perdre parmi des centaines d'outils dont la plupart ne concernent pas le RE.  
- Lorsqu'un outil n'est pas dans les dépôts Kali, l'installation manuelle est parfois moins bien documentée que sur Ubuntu.

> 💡 **Notre recommandation** : si vous utilisez déjà Kali au quotidien et que vous y êtes à l'aise, vous pouvez tout à fait suivre cette formation dessus. Sinon, partez sur Ubuntu LTS — vous aurez un environnement plus prévisible et vous n'installerez que ce dont vous avez réellement besoin.

---

## Tableau récapitulatif

| Critère | Ubuntu LTS | Debian stable | Kali Linux |  
|---|---|---|---|  
| **Modèle de release** | LTS (5 ans de support) | Stable (~2 ans entre versions) | Rolling release |  
| **Outils RE préinstallés** | Peu (installation manuelle) | Très peu | Beaucoup |  
| **Fraîcheur des paquets** | Récente | Conservatrice | Très récente |  
| **Taille de l'image de base** | ~3 Go (Desktop), ~1 Go (Server) | ~600 Mo (netinst) | ~4 Go (image VM) |  
| **RAM minimale pour le RE** | 4 Go (8 Go recommandés) | 2 Go (8 Go recommandés) | 4 Go (8 Go recommandés) |  
| **Documentation communautaire** | Abondante | Abondante | Bonne (orientée pentest) |  
| **Risque de casse après `apt upgrade`** | Faible | Très faible | Modéré |  
| **Idéal pour** | Suivre cette formation | Minimalistes, machines légères | Utilisateurs Kali existants |

---

## Et les autres distributions ?

**Fedora / Arch / openSUSE** sont des distributions tout à fait capables d'accueillir un environnement RE. Si vous en maîtrisez une, vous saurez adapter les commandes d'installation (`dnf`, `pacman`, `zypper`). Cependant, cette formation ne fournit pas d'instructions spécifiques pour ces distributions. En cas de problème lié au packaging, vous serez livré à vous-même.

**WSL2 (Windows Subsystem for Linux)** est une option tentante pour les utilisateurs Windows qui ne souhaitent pas créer de VM. WSL2 fonctionne pour une partie des outils (GDB, binutils, compilation, scripts Python), mais présente des limitations importantes pour le RE :

- L'absence d'interface graphique native rend l'utilisation de Ghidra, ImHex ou Cutter plus laborieuse (il faut configurer un serveur X ou utiliser WSLg).  
- L'instrumentation bas niveau (Frida, ptrace avancé, certaines fonctionnalités de Valgrind) peut se comporter différemment ou ne pas fonctionner du tout, car le noyau WSL2 n'est pas un noyau Linux standard.  
- L'isolation est faible : WSL2 partage le système de fichiers et le réseau de l'hôte Windows, ce qui pose problème pour les chapitres d'analyse de malware (Partie VI).

Pour ces raisons, nous déconseillons WSL2 comme environnement principal pour cette formation. Si vous êtes sous Windows, la VM reste la voie recommandée.

---

## Quelle ISO télécharger ?

Si vous suivez notre recommandation (Ubuntu 24.04 LTS), voici les deux options :

- **Ubuntu Desktop 24.04 LTS** — si vous souhaitez un environnement graphique complet (GNOME) pour utiliser confortablement Ghidra, ImHex et Cutter. C'est le choix le plus simple pour débuter.  
  → Téléchargement : [ubuntu.com/download/desktop](https://ubuntu.com/download/desktop)

- **Ubuntu Server 24.04 LTS** — si vous préférez un système léger et installer un bureau minimal par la suite (`sudo apt install xfce4 xfce4-goodies`). Meilleur pour les machines avec peu de RAM.  
  → Téléchargement : [ubuntu.com/download/server](https://ubuntu.com/download/server)

> ⚠️ **Architecture** : téléchargez impérativement la version **amd64** (x86-64). Les binaires d'entraînement de cette formation sont compilés pour cette architecture. Une image ARM64 (même sur un Mac Apple Silicon via UTM) nécessitera une émulation x86-64 décrite en section 4.3.

---

## Résumé

- **Choix par défaut** : Ubuntu 24.04 LTS (amd64), en version Desktop ou Server selon vos ressources.  
- **Alternative acceptable** : Debian stable si vous la maîtrisez, Kali si vous l'utilisez déjà.  
- **À éviter pour cette formation** : WSL2 (limitations d'isolation et de compatibilité), distributions non-Debian (pas de support documenté ici).  
- Le choix de la distribution n'est pas critique — tous les outils s'installent sur les trois candidates. Ce qui compte, c'est d'avoir un environnement **stable, isolé et reproductible**.

---


⏭️ [Installation et configuration des outils essentiels (liste versionnée)](/04-environnement-travail/02-installation-outils.md)
