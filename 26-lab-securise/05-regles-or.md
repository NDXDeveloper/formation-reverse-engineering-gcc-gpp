🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 26.5 — Règles d'or : ne jamais exécuter hors sandbox, ne jamais connecter au réseau réel

> **Chapitre 26 — Mise en place d'un lab d'analyse sécurisé**  
> **Partie VI — Analyse de Code Malveillant (Environnement Contrôlé)**

---

## Pourquoi des règles non négociables

Les sections précédentes ont construit un lab techniquement solide : VM dédiée, bridge isolé, monitoring pré-déployé, snapshots systématiques. Mais l'infrastructure ne protège que si elle est **utilisée correctement à chaque session, sans exception**. L'histoire de l'analyse de malware — en laboratoire académique comme en environnement professionnel — est jalonnée d'incidents causés non pas par une faille technique, mais par un raccourci humain. Un analyste pressé qui exécute « juste un test rapide » sur l'hôte. Un stagiaire qui branche la VM sur le NAT pour télécharger un outil en oubliant qu'un sample est déjà chargé. Un chercheur qui partage un répertoire entre l'hôte et la VM pour « gagner du temps ».

Ces règles d'or n'existent pas parce que la technique est insuffisante. Elles existent parce que la technique est inutile si la discipline ne suit pas. Elles sont volontairement absolues, sans nuance, sans « sauf si ». Ce caractère catégorique est intentionnel : dans le feu de l'action, face à un binaire qu'on a hâte de comprendre, la tentation du raccourci est réelle. Une règle à interprétation variable devient une règle qu'on contourne. Une règle absolue reste un mur.

---

## Règle n°1 — Ne jamais exécuter un sample hors de la VM d'analyse

C'est la règle fondatrice, celle dont toutes les autres découlent. Un sample — quel qu'il soit, quelle que soit la confiance qu'on lui accorde — ne s'exécute **jamais** sur la machine hôte, jamais sur la VM de travail du chapitre 4, jamais sur aucune machine qui n'est pas la VM d'analyse dédiée construite dans ce chapitre.

### Ce que « exécuter » signifie

La définition est large et intentionnellement conservatrice :

- **Lancer directement le binaire** (`./sample`) — le cas évident.  
- **Le charger dans un débogueur et le laisser tourner** (`gdb ./sample` suivi de `run`) — GDB lance le processus, le code s'exécute.  
- **L'ouvrir avec un outil qui l'exécute implicitement** — certains outils de profiling ou de couverture de code exécutent le binaire en interne.  
- **Le passer à un script d'automatisation** — un script `pwntools` qui appelle `process('./sample')` exécute le binaire.  
- **Le fuzzer** — AFL++ et libFuzzer exécutent le binaire des milliers de fois. Le faire hors VM revient à exécuter le sample hors VM des milliers de fois.

### Ce qui n'est PAS une exécution et peut se faire sur l'hôte

L'analyse statique ne nécessite aucune exécution. Les opérations suivantes sont sûres sur l'hôte :

- Charger le binaire dans **Ghidra** (Ghidra analyse le fichier, il ne l'exécute pas).  
- Lancer `file`, `strings`, `readelf`, `objdump`, `nm` sur le fichier.  
- Ouvrir le binaire dans **ImHex** pour l'analyse hexadécimale.  
- Calculer son hash (`sha256sum`).  
- Appliquer des règles **YARA** sur le fichier.  
- Examiner le binaire avec `binwalk`.

La frontière est nette : si l'outil lit le fichier comme une suite d'octets, c'est de l'analyse statique. Si l'outil donne le contrôle au code contenu dans le fichier, c'est de l'exécution.

> 💡 **Cas particulier des archives et documents piégés** — Si vous analysez un jour des samples réels au-delà de cette formation, méfiez-vous des fichiers qui ne sont pas des binaires ELF mais qui exploitent des vulnérabilités dans les logiciels qui les ouvrent (PDF piégés, documents Office malveillants, images forgées). Dans ces cas, même « ouvrir » le fichier dans le logiciel ciblé constitue une exécution potentielle. Ce scénario ne concerne pas cette formation (nos samples sont des binaires ELF), mais le réflexe doit être acquis.

### Pourquoi cette règle est absolue

« Je sais que ce sample est inoffensif, c'est moi qui l'ai compilé. » Cette phrase est le préambule de la majorité des incidents en lab. Trois raisons justifient l'absence d'exception :

Premièrement, le **réflexe musculaire**. Si vous prenez l'habitude d'exécuter des samples « de confiance » sur l'hôte, le geste devient automatique. Le jour où vous manipulez un sample dont vous n'êtes plus certain de l'innocuité, le réflexe prend le dessus avant la réflexion.

Deuxièmement, la **certitude est une illusion**. Même un sample que vous avez compilé vous-même peut avoir un comportement inattendu. Un buffer overflow dans votre propre code, un comportement dépendant de l'environnement, une bibliothèque tierce qui fait un appel réseau non documenté. La VM est là pour absorber les imprévus.

Troisièmement, la **crédibilité professionnelle**. Si vous travaillez un jour dans une équipe d'analyse de menaces, vos collègues et votre hiérarchie s'attendent à ce que vous appliquiez cette règle sans réfléchir. La découvrir sous pression n'est pas le bon moment.

---

## Règle n°2 — Ne jamais connecter la VM au réseau réel quand un sample est présent

La formulation est précise : « quand un sample est présent ». La VM peut — et doit — avoir un accès réseau pendant l'installation et la maintenance (mises à jour, installation de paquets). Mais dès qu'un fichier suspect se trouve quelque part dans la VM (même s'il n'a pas encore été exécuté), le réseau doit être isolé.

### Ce que « réseau réel » signifie

- Le réseau **NAT par défaut** de libvirt (`virbr0` / `default`) — il donne accès à Internet via l'hôte.  
- Un réseau **bridgé sur l'interface physique** de l'hôte — la VM obtient une adresse sur le LAN réel.  
- Un réseau **host-only avec forwarding IP activé** sur l'hôte — les paquets peuvent atteindre le LAN via l'hôte.  
- Toute configuration où `ping 8.8.8.8` **réussit** depuis la VM.

Le réseau `isolated-malware` (`br-malware`) n'est **pas** un réseau réel au sens de cette règle. Il est conçu pour capturer le trafic sans le laisser sortir.

### Le scénario type de la violation

Le scénario classique se déroule ainsi :

1. L'analyste termine une session. Il restaure le snapshot `clean-base`.  
2. Il remet la VM sur le réseau `default` (NAT) pour installer un outil qu'il avait oublié.  
3. Il copie un nouveau sample dans la VM pour la prochaine session.  
4. Il oublie de rebasculer sur le réseau isolé.  
5. Il exécute le sample. Le sample contacte son C2 réel, exfiltre les données qu'il trouve, ou scanne le LAN.

L'étape critique est l'étape 4 — un oubli de deux secondes qui peut avoir des conséquences significatives. La parade est procédurale : **toujours vérifier l'isolation réseau avant d'exécuter un sample**, et ne jamais copier un sample dans la VM tant que le réseau n'est pas isolé.

### Vérification systématique

Avant chaque exécution, exécutez cette séquence dans la VM :

```bash
# Vérification en 3 points
echo "=== Vérification isolation réseau ==="

# 1. Pas de route par défaut
if ip route | grep -q "^default"; then
    echo "[FAIL] Route par défaut détectée — NE PAS EXÉCUTER"
    ip route
    exit 1
else
    echo "[OK] Aucune route par défaut"
fi

# 2. Pas d'accès Internet
if ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1; then
    echo "[FAIL] Internet accessible — NE PAS EXÉCUTER"
    exit 1
else
    echo "[OK] Internet inaccessible"
fi

# 3. Résolution DNS publique impossible
if host example.com >/dev/null 2>&1; then
    echo "[WARN] DNS public résout — vérifier si INetSim est actif intentionnellement"
else
    echo "[OK] DNS public inaccessible"
fi

echo "=== Isolation confirmée ==="
```

Intégrez cette vérification dans le script `start_monitoring.sh` de la section 26.3, en tout premier — avant le lancement des outils de monitoring. Si la vérification échoue, le script s'arrête et rien ne s'exécute.

---

## Règle n°3 — Toujours prendre un snapshot avant d'exécuter un sample

Cette règle garantit la réversibilité. Sans snapshot pré-exécution, les effets du sample sur la VM sont **permanents** (jusqu'à la restauration du snapshot `clean-base`, qui efface aussi votre configuration de session).

Le snapshot pré-exécution capture l'état exact de la VM au moment où tout est prêt : sample copié, monitoring lancé, réseau isolé. Si l'exécution du sample provoque un crash système, un kernel panic, ou un état irrécupérable, vous pouvez revenir à cet instant précis sans perdre votre préparation.

### Convention de nommage

Un snapshot n'est utile que s'il est identifiable. Adoptez une convention stricte :

```
pre-exec-<nom-du-sample>-<YYYYMMDD>-<HHMM>
```

Exemples :

```
pre-exec-ch27-ransomware-20250615-1432  
pre-exec-ch28-dropper-20250616-0915  
pre-exec-ch29-packed-upx-20250616-1100  
```

Évitez les noms génériques (`test`, `snapshot1`, `avant-exec`). Dans trois semaines, quand vous chercherez un snapshot spécifique parmi une dizaine, le nom descriptif vous fera gagner un temps considérable.

---

## Règle n°4 — Ne jamais partager de ressources entre l'hôte et la VM d'analyse

Cette règle découle du principe du moindre privilège (section 26.1), mais elle mérite d'être énoncée comme règle à part entière en raison de la fréquence des violations.

### Dossiers partagés

Les dossiers partagés (VirtualBox Shared Folders, `virtio-9p` dans QEMU, VMware Shared Folders) créent un accès direct du système invité vers le système de fichiers de l'hôte. Un malware qui parcourt les points de montage de la VM trouvera ce dossier partagé et pourra lire, modifier ou chiffrer les fichiers de l'hôte.

La solution est de ne jamais configurer de dossier partagé sur la VM d'analyse. Le transfert de fichiers se fait exclusivement par `scp` via le réseau host-only ou le bridge isolé, ce qui donne un contrôle total sur ce qui est transféré et dans quelle direction.

### Presse-papier partagé

Le presse-papier partagé est bidirectionnel. Si vous copiez un chemin de fichier ou une commande sur l'hôte, le sample peut le lire dans la VM. Inversement, un malware qui place du contenu dans le presse-papier de la VM (technique utilisée par les clipboard stealers) peut contaminer le presse-papier de l'hôte.

Vérifiez que `spice-vdagent` n'est pas installé dans la VM :

```bash
dpkg -l | grep spice-vdagent
# Si présent :
sudo apt remove --purge spice-vdagent
```

### Périphériques USB

Le passthrough USB (redirection d'un périphérique USB physique vers la VM) expose le firmware du périphérique aux actions du malware. Certains malwares ciblent spécifiquement les périphériques USB pour se propager (les clés USB infectées restent un vecteur d'attaque réel). Ne redirigez jamais de périphérique USB vers la VM d'analyse.

### Glisser-déposer (Drag and Drop)

Certains hyperviseurs proposent le glisser-déposer de fichiers entre l'hôte et la VM. Cette fonctionnalité implique un canal de communication entre les deux systèmes qui constitue une surface d'attaque. Désactivez-la.

---

## Règle n°5 — Toujours hasher les samples avant et après manipulation

Le hash cryptographique (SHA-256) est l'empreinte digitale du sample. Il remplit deux fonctions essentielles.

### Identification univoque

Dans un rapport d'analyse, le hash identifie le sample de manière univoque. Dire « j'ai analysé le ransomware du chapitre 27 » est ambigu — il peut y avoir plusieurs versions, des recompilations, des modifications. Dire « j'ai analysé le binaire SHA-256 `a1b2c3d4...` » est univoque et vérifiable par quiconque possède le même fichier.

### Détection de modification accidentelle

Si le hash du sample change entre le moment où vous l'avez copié dans la VM et le moment où vous lancez l'analyse, le fichier a été modifié — peut-être par une erreur de transfert, peut-être par un autre processus dans la VM. Analyser un binaire modifié accidentellement produit des résultats incorrects sans qu'on s'en rende compte.

### En pratique

```bash
# Sur l'hôte, avant le transfert
sha256sum ransomware_sample
# a1b2c3d4e5f6... ransomware_sample

# Dans la VM, après le transfert
sha256sum ~/malware-samples/ransomware_sample
# a1b2c3d4e5f6... (doit être identique)
```

Consignez le hash dans vos notes de session. Si vous rédigez un rapport (chapitre 27, section 27.7), le hash figure dans la section « Identification du sample ».

---

## Règle n°6 — Collecter les artefacts avant le rollback

Le rollback d'un snapshot est irréversible : tout ce qui existait dans la VM après le snapshot est **détruit**. Les logs `auditd`, les captures `inotifywait`, les fichiers modifiés par le sample, les dumps mémoire de GDB — tout disparaît.

Avant chaque rollback, transférez systématiquement les artefacts vers l'hôte :

```bash
# Depuis l'hôte
ANALYSIS_DIR="./analyses/ch27-ransomware-$(date +%Y%m%d-%H%M%S)"  
mkdir -p "$ANALYSIS_DIR"  

scp analyst@10.66.66.100:~/captures/* "$ANALYSIS_DIR/"  
scp analyst@10.66.66.100:/var/log/audit/audit.log "$ANALYSIS_DIR/"  

# Optionnel : récupérer les fichiers modifiés par le sample
scp -r analyst@10.66.66.100:/tmp/test/ "$ANALYSIS_DIR/modified_files/"
```

Vérifiez que les fichiers sont bien sur l'hôte avant de lancer le rollback. La commande `ls -la "$ANALYSIS_DIR/"` doit montrer des fichiers non vides.

Le script `cleanup_analysis.sh` de la section 26.2 automatise cette collecte, mais une vérification visuelle avant le rollback reste une bonne habitude.

---

## Règle n°7 — Documenter chaque session d'analyse

L'analyse de malware n'est pas un acte solitaire et éphémère. Vous relirez vos propres notes dans une semaine. Un collègue devra peut-être reproduire votre analyse. Un rapport devra être rédigé. Sans documentation, l'analyse n'a produit que des souvenirs — et les souvenirs sont infidèles.

### Ce que chaque session doit consigner

Chaque session d'analyse devrait produire un fichier de notes (même bref) contenant :

- **Date et heure** de début et fin de la session.  
- **Hash SHA-256** du sample analysé.  
- **Nom du snapshot** utilisé comme base et nom du snapshot pré-exécution.  
- **Configuration réseau** : isolation totale ou semi-isolée (INetSim, faux C2).  
- **Outils de monitoring actifs** : lesquels, avec quels paramètres.  
- **Commandes d'exécution** : comment le sample a été lancé, sous quel utilisateur, avec quels arguments.  
- **Observations chronologiques** : ce qui s'est passé, dans quel ordre, ce qui était inattendu.  
- **Artefacts collectés** : liste des fichiers récupérés et leur emplacement sur l'hôte.  
- **Questions ouvertes** : ce qu'il reste à investiguer lors de la prochaine session.

Un template minimaliste :

```markdown
# Session d'analyse — [nom du sample]

- **Date** : 2025-06-15, 14:30 – 15:45
- **Sample** : ransomware_sample (SHA-256 : a1b2c3d4...)
- **Snapshot base** : clean-base
- **Snapshot pré-exec** : pre-exec-ch27-ransomware-20250615-1430
- **Réseau** : isolation totale (pas d'INetSim)
- **Monitoring** : auditd + inotifywait + tcpdump (hôte)

## Observations

- T+0s : lancement sous sample-runner
- T+1s : ouverture de /tmp/test/ (inotifywait)
- T+2s : lecture séquentielle de tous les fichiers .txt
- T+3s : création de fichiers .enc correspondants
- T+5s : suppression des originaux
- T+6s : création de RANSOM_NOTE.txt
- T+7s : le processus se termine (exit 0)
- Aucune activité réseau détectée (tcpdump : 0 paquet hors ARP/DHCP)

## Artefacts

- analyses/ch27-ransomware-20250615-1430/capture.pcap
- analyses/ch27-ransomware-20250615-1430/audit.log
- analyses/ch27-ransomware-20250615-1430/inotify.log
- analyses/ch27-ransomware-20250615-1430/modified_files/

## Questions ouvertes

- L'algorithme de chiffrement est-il AES-CBC ou AES-CTR ?
- La clé est-elle hardcodée ou dérivée d'un input ?
```

---

## Règle n°8 — Ne jamais analyser un sample sur une machine contenant des données sensibles

Cette règle concerne davantage le contexte professionnel que cette formation, mais le principe doit être ancré dès maintenant.

La VM d'analyse est isolée, mais la machine hôte ne l'est pas forcément. Si votre hôte contient des données professionnelles confidentielles, des clés SSH vers des serveurs de production, des tokens d'API, des bases de données clients — et qu'un sample réussit à s'échapper de la VM (scénario rare mais pas impossible), ces données sont exposées.

Dans un cadre professionnel, la machine d'analyse est idéalement une machine dédiée, déconnectée du réseau d'entreprise, sans accès aux ressources internes. Dans le cadre de cette formation, vos samples sont créés par vos soins et le risque d'évasion est négligeable, mais gardez le réflexe : ne stockez pas sur la machine hôte de votre lab des éléments dont la compromission aurait des conséquences graves.

---

## Checklist pré-exécution

Toutes les règles ci-dessus se résument en une checklist à parcourir mentalement (ou physiquement, en la cochant) avant chaque exécution de sample. Imprimez-la, collez-la à côté de votre écran, intégrez-la dans vos scripts.

```
┌─────────────────────────────────────────────────────────┐
│              CHECKLIST PRÉ-EXÉCUTION                    │
│                                                         │
│  □  Le sample est dans la VM d'analyse                  │
│     (PAS sur l'hôte, PAS dans une autre VM)             │
│                                                         │
│  □  La VM est sur le réseau isolé (br-malware)          │
│     → ping 8.8.8.8 échoue                               │
│     → pas de route par défaut (ip route)                │
│                                                         │
│  □  Un snapshot pré-exécution a été pris                │
│     → nom descriptif avec date                          │
│                                                         │
│  □  Le monitoring est actif                             │
│     → tcpdump sur l'hôte                                │
│     → auditd chargé dans la VM                          │
│     → inotifywait lancé dans la VM                      │
│                                                         │
│  □  Aucun dossier partagé, USB, clipboard               │
│     entre l'hôte et la VM                               │
│                                                         │
│  □  Le hash du sample a été vérifié                     │
│                                                         │
│  □  Les notes de session sont ouvertes                  │
│     (prêtes à consigner les observations)               │
│                                                         │
│  Tout est coché ? → Vous pouvez exécuter.               │
│  Un point manque ? → Corrigez AVANT d'exécuter.         │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## Script de vérification automatisée

Pour les analystes qui préfèrent l'automatisation à la discipline pure, voici un script qui vérifie programmatiquement les points critiques de la checklist. Ce script s'exécute **dans la VM** juste avant le lancement du sample :

```bash
#!/bin/bash
# preflight_check.sh — Vérification pré-exécution dans la VM
# Usage : source preflight_check.sh (doit être sourcé, pas exécuté,
#         pour pouvoir interrompre la session shell en cas d'échec)

PASS=0  
FAIL=0  

check() {
    local description="$1"
    local result="$2"
    if [ "$result" -eq 0 ]; then
        echo "[OK]   $description"
        ((PASS++))
    else
        echo "[FAIL] $description"
        ((FAIL++))
    fi
}

echo "========================================"  
echo "  VÉRIFICATION PRÉ-EXÉCUTION"  
echo "  $(date)"  
echo "========================================"  
echo ""  

# 1. Pas de route par défaut
ip route | grep -q "^default"  
check "Aucune route par défaut" "$?"  
# Ici $? vaut 0 si grep a trouvé (= mauvais), 1 sinon (= bon)
# Inversons la logique :
ip route | grep -q "^default"  
DEFAULT_ROUTE=$?  
check "Aucune route par défaut" "$((1 - DEFAULT_ROUTE))"  

# 2. Internet inaccessible
ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1  
INTERNET=$?  
check "Internet inaccessible (ping 8.8.8.8)" "$INTERNET"  

# 3. Pas de dossier partagé monté
mount | grep -qiE '(vboxsf|vmhgfs|9p|shared)'  
SHARED=$?  
check "Aucun dossier partagé monté" "$((1 - SHARED))"  

# 4. spice-vdagent absent (clipboard)
pgrep -x spice-vdagent >/dev/null 2>&1  
CLIPBOARD=$?  
check "Clipboard partagé inactif (spice-vdagent)" "$((1 - CLIPBOARD))"  

# 5. auditd actif
systemctl is-active --quiet auditd  
AUDITD=$?  
check "auditd actif" "$((1 - AUDITD))"  

# 6. Règles auditd chargées
RULES_COUNT=$(sudo auditctl -l 2>/dev/null | grep -c -v "No rules")
[ "$RULES_COUNT" -gt 0 ] 2>/dev/null
check "Règles auditd chargées ($RULES_COUNT règles)" "$?"

echo ""  
echo "========================================"  
echo "  Résultat : $PASS OK, $FAIL FAIL"  
echo "========================================"  

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "  ⛔ AU MOINS UN CHECK A ÉCHOUÉ"
    echo "     Corrigez les problèmes avant d'exécuter le sample."
    echo ""
else
    echo ""
    echo "  ✅ TOUS LES CHECKS SONT PASSÉS"
    echo "     Vous pouvez exécuter le sample."
    echo ""
fi
```

> 💡 Ce script est un filet de sécurité, pas un substitut au jugement. Il vérifie les points techniques automatisables, mais ne peut pas vérifier que vous avez pris vos notes, que vous avez bien hashé le sample, ou que votre snapshot est correctement nommé. La discipline humaine reste le dernier rempart.

---

## Quand les règles semblent excessives

À ce stade de la formation, certaines de ces règles peuvent sembler disproportionnées par rapport aux samples que nous manipulons. Après tout, ce sont des binaires que nous avons compilés nous-mêmes à partir de sources fournies. Le risque réel est minime.

Cette objection est parfaitement légitime, et pourtant la réponse reste la même : appliquez les règles quand même. Voici pourquoi.

L'objectif de cette formation n'est pas seulement de vous apprendre à analyser les samples fournis. C'est de vous donner les compétences et les réflexes pour analyser **n'importe quel** binaire, y compris ceux que vous rencontrerez plus tard dans un contexte professionnel ou un CTF avancé — des binaires dont vous ne connaîtrez ni la source, ni l'auteur, ni les capacités. Les réflexes se construisent par la répétition dans un environnement sûr, pas par la théorie. Chaque session d'analyse sur nos samples pédagogiques est une répétition qui ancre le geste. Quand vous ferez face à un vrai échantillon inconnu, la checklist sera un automatisme, pas une corvée à apprendre sous pression.

C'est exactement le même principe que les pilotes d'avion qui déroulent leur checklist pré-vol à chaque décollage, y compris quand ils volent depuis 20 ans et connaissent l'appareil par cœur. Ce n'est pas une question de compétence — c'est une question de fiabilité.

---

> 📌 **À retenir** — Les huit règles de cette section ne sont pas des recommandations graduelles. Ce sont des invariants. L'infrastructure la plus sophistiquée ne protège rien si l'analyste prend un raccourci. Snapshot avant, isolation vérifiée, monitoring actif, artefacts collectés, notes prises — à chaque session, sans exception. La checklist pré-exécution est votre dernière ligne de défense entre une analyse réussie et un incident évitable.

⏭️ [🎯 Checkpoint : déployer le lab et vérifier l'isolation réseau](/26-lab-securise/checkpoint.md)
