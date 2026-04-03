🔝 Retour au [Sommaire](/SOMMAIRE.md)

# 18.5 — Limites : explosion de chemins, boucles, appels système

> **Chapitre 18 — Exécution symbolique et solveurs de contraintes**  
> Partie IV — Techniques Avancées de RE

---

## L'exécution symbolique n'est pas magique

Les sections précédentes ont montré la puissance de l'exécution symbolique : résoudre un crackme en quelques lignes de Python, sans comprendre la logique du programme. Mais cette puissance a un prix. L'exécution symbolique se heurte à des limites fondamentales — pas des bugs d'implémentation qu'une future version d'angr corrigera, mais des obstacles théoriques inhérents à l'approche elle-même.

Comprendre ces limites est essentiel pour deux raisons. D'abord, cela vous évitera de perdre des heures à attendre qu'un script angr termine alors qu'il ne terminera jamais. Ensuite, cela vous guidera vers la bonne stratégie : savoir quand l'exécution symbolique est l'outil adapté, quand il faut la combiner avec d'autres techniques, et quand il faut l'abandonner au profit d'une approche purement manuelle.

---

## Limite n°1 — L'explosion des chemins (path explosion)

C'est l'ennemi principal de l'exécution symbolique et la raison pour laquelle elle ne peut pas « tout résoudre automatiquement ».

### Le problème

Chaque branchement conditionnel dont la condition dépend d'une valeur symbolique **double** le nombre d'états actifs. Avec *n* branchements successifs, le nombre d'états peut atteindre **2ⁿ**. Un programme réaliste contient des centaines, voire des milliers de branchements.

Prenons un exemple concret. Imaginez une fonction qui valide un mot de passe caractère par caractère :

```c
int check_password(const char *input) {
    if (input[0] != 'S') return 0;
    if (input[1] != 'e') return 0;
    if (input[2] != 'c') return 0;
    if (input[3] != 'r') return 0;
    if (input[4] != 'e') return 0;
    if (input[5] != 't') return 0;
    if (input[6] != '!') return 0;
    return 1;
}
```

Sept branchements, 2⁷ = 128 chemins théoriques. Ici c'est gérable. Mais considérez une validation plus réaliste qui itère sur une chaîne de 64 caractères avec des opérations de transformation à chaque itération — le nombre de chemins explose bien au-delà de ce qu'un ordinateur peut explorer.

### Pourquoi ça explose en pratique

Le problème n'est pas seulement le nombre de branchements dans la fonction cible. C'est le nombre de branchements **total** sur le chemin d'exécution, incluant :

- Le code d'initialisation de la libc (`__libc_start_main`, constructeurs).  
- Les SimProcedures qui modélisent des fonctions de bibliothèque — même simplifiées, elles contiennent des branchements internes.  
- Les fonctions appelées par la fonction cible (vérification de format, allocation mémoire…).  
- Les branchements implicites ajoutés par les optimisations du compilateur (branchements pour la vectorisation, tests de taille de boucle…).

Un programme qui semble simple en C peut produire un arbre d'exécution de plusieurs millions de nœuds une fois compilé et simulé dans angr.

### Symptômes

Vous reconnaîtrez l'explosion des chemins à ces signes :

- Le nombre d'états dans `simgr.active` croît indéfiniment (des centaines, puis des milliers, puis des dizaines de milliers).  
- La consommation mémoire grimpe de manière continue (chaque état transporte ses propres registres, sa mémoire et ses contraintes).  
- Le script tourne depuis plusieurs minutes sans que `simgr.found` ne reçoive d'état.  
- Les appels au solveur Z3 deviennent de plus en plus lents car les contraintes accumulées se complexifient.

### Stratégies de mitigation

**Élaguer avec `avoid`** — Chaque adresse dans la liste `avoid` supprime immédiatement tous les états qui l'atteignent. Plus vous fournissez d'adresses à éviter, plus vous élaguez l'arbre tôt. En pratique, identifiez non seulement le message d'échec final, mais aussi les retours anticipés (les `return 0` en milieu de fonction) :

```python
simgr.explore(
    find=addr_success,
    avoid=[
        addr_fail_strlen,      # return 0 si mauvaise longueur
        addr_fail_format,      # return 0 si caractère invalide
        addr_fail_compare,     # return 0 si comparaison échoue
        addr_fail_final        # puts("Access Denied.")
    ]
)
```

**Contraindre les entrées** — Plus les variables symboliques sont contraintes, moins le solveur autorise de chemins. Contraindre chaque caractère à être un chiffre hexadécimal élimine 94% de l'espace ASCII à chaque branchement qui dépend de la valeur d'un caractère.

**Démarrer plus tard** — Au lieu de partir de `_start` ou de `main`, démarrez à l'entrée de la fonction de vérification avec `blank_state` (comme vu en section 18.3). Vous éliminez tous les branchements du code d'initialisation et de parsing des arguments.

**Limiter la profondeur** — `LengthLimiter` arrête les états qui ont traversé trop de blocs de base :

```python
simgr.use_technique(angr.exploration_techniques.LengthLimiter(
    max_length=2000
))
```

**Fusionner les états (Veritesting)** — Quand deux chemins divergent puis reconvergent (un pattern très courant avec les `if/else`), Veritesting peut les fusionner en un seul état avec des contraintes conditionnelles, réduisant le nombre total d'états :

```python
simgr.use_technique(angr.exploration_techniques.Veritesting())
```

Veritesting ne fonctionne pas dans tous les cas et peut parfois ralentir l'exploration au lieu de l'accélérer. C'est un outil à essayer empiriquement.

---

## Limite n°2 — Les boucles dépendant d'entrées symboliques

### Le problème

Considérez cette boucle :

```c
int count = 0;  
for (int i = 0; i < input_length; i++) {  
    if (input[i] == 'X')
        count++;
}
if (count == 3)
    success();
```

Si `input_length` est une valeur **concrète** (par exemple 16), la boucle est déroulée 16 fois et chaque itération ajoute un branchement (le `if`). C'est gérable — 2¹⁶ = 65536 chemins au maximum.

Mais si `input_length` est **symbolique** (le moteur ne sait pas combien de fois itérer), angr ne peut pas déterminer quand arrêter le déroulage. Il peut théoriquement dérouler la boucle indéfiniment, créant de nouveaux états à chaque itération.

Même avec une borne concrète, les boucles restent problématiques si elles contiennent des branchements. Une boucle de 100 itérations avec un `if` symbolique par itération produit potentiellement 2¹⁰⁰ chemins — un nombre qui dépasse le nombre d'atomes dans l'univers observable.

### Symptômes

- Le nombre d'états actifs croît linéairement et indéfiniment (les états se multiplient à chaque itération de la boucle).  
- Vous voyez les mêmes adresses revenir en boucle dans les états actifs quand vous les inspectez.  
- L'exploration ne progresse jamais vers la suite de la fonction.

### Stratégies de mitigation

**LoopSeer** — C'est la technique d'exploration conçue spécifiquement pour ce problème. Elle détecte les boucles dans le graphe de flux de contrôle et limite le nombre d'itérations :

```python
cfg = proj.analyses.CFGFast()  
simgr.use_technique(angr.exploration_techniques.LoopSeer(  
    cfg=cfg,
    bound=10       # Maximum 10 itérations par boucle
))
```

Le choix de la borne est un compromis. Trop faible, et vous ratez la solution si elle nécessite plus d'itérations. Trop élevée, et l'explosion des chemins revient. En pratique, commencez avec une borne basse (5–10) et augmentez si la solution n'est pas trouvée.

**Hooker la boucle** — Si vous comprenez ce que fait la boucle grâce à Ghidra (par exemple, elle calcule un checksum sur l'entrée), vous pouvez la remplacer par une SimProcedure qui modélise directement le résultat :

```python
class ChecksumHook(angr.SimProcedure):
    def run(self, buf_ptr, length):
        # Modéliser le résultat comme une valeur symbolique
        # avec les contraintes appropriées
        result = self.state.solver.BVS("checksum", 32)
        return result

proj.hook(addr_debut_boucle, ChecksumHook(), length=taille_boucle_en_octets)
```

Le paramètre `length` indique combien d'octets de code machine le hook remplace — angr sautera ces octets et reprendra l'exécution après.

**Passer à Z3** — Quand la boucle est trop complexe pour angr mais que vous avez compris sa logique dans Ghidra, modélisez-la directement en Z3 avec un déroulage Python (comme vu en section 18.4). Python peut dérouler une boucle de 10 000 itérations en quelques millisecondes pour construire l'expression Z3 correspondante.

---

## Limite n°3 — Les appels système et l'environnement

### Le problème

Un programme réel ne vit pas en vase clos. Il interagit avec le système d'exploitation via des appels système : lire des fichiers, ouvrir des sockets, obtenir l'heure, allouer de la mémoire, créer des processus… Chacune de ces interactions pose un problème pour l'exécution symbolique :

- **Le résultat est imprévisible.** Que retourne `read(fd, buf, 16)` ? Cela dépend du contenu du fichier, qui n'existe pas dans l'environnement simulé d'angr.  
- **L'appel peut avoir des effets de bord.** `fork()` crée un nouveau processus. `mmap()` modifie l'espace d'adressage. `ioctl()` modifie l'état d'un périphérique.  
- **Le résultat peut être source de branchements.** Si le programme vérifie la valeur de retour de `open()` et se comporte différemment selon le succès ou l'échec, c'est un branchement supplémentaire à explorer.

angr gère ce problème de deux manières :

1. **SimProcedures** pour les fonctions de la libc courantes (`open`, `read`, `write`, `close`, `malloc`, `free`, `printf`, `strcmp`…). Ces modèles sont fonctionnels mais simplifiés — par exemple, `open` dans angr ouvre un fichier simulé en mémoire, pas un vrai fichier.

2. **SimOS** pour les appels système directs (`syscall`). angr modélise un sous-ensemble des syscalls Linux, mais beaucoup restent non supportés.

### Ce qui fonctionne bien

- Les fonctions de manipulation de chaînes (`strlen`, `strcmp`, `strcpy`, `memcpy`, `memcmp`…) sont bien modélisées et raisonnent correctement sur des arguments symboliques.  
- `malloc`/`free` fonctionnent avec un allocateur simplifié.  
- `printf`/`puts`/`fprintf` écrivent dans un flux simulé (accessible via `state.posix.dumps(fd)`).  
- Les entrées depuis `argv` et `stdin` sont bien supportées.

### Ce qui pose problème

**Fichiers et I/O** — Si le programme lit une clé depuis un fichier (`fopen`/`fread`), angr doit savoir quoi retourner. Vous pouvez pré-charger un fichier simulé :

```python
# Simuler le contenu du fichier "config.dat"
content = claripy.BVS("file_content", 128)  # 16 octets symboliques

simfile = angr.SimFile("config.dat", content=content)  
state.fs.insert("config.dat", simfile)  
```

**Réseau** — Les sockets (`socket`, `connect`, `send`, `recv`) ne sont que partiellement supportées. Si le binaire communique avec un serveur pour la validation (chapitre 23), angr ne pourra pas simuler la communication. Solution : hooker les fonctions réseau pour retourner des données symboliques ou des valeurs concrètes prédéfinies.

**Threads et processus** — `fork`, `pthread_create`, signaux… ne sont pas supportés de manière fiable. Si le binaire est multi-threadé, angr n'est généralement pas l'outil adapté.

**Appels système rares** — `ioctl`, `mmap` avec des flags exotiques, `prctl`, `seccomp`… Chaque syscall non supporté provoque une erreur ou un comportement imprédictible. Les états qui les atteignent se retrouvent dans `simgr.errored`.

**Temps et aléatoire** — `time()`, `gettimeofday()`, `rand()`, `/dev/urandom`… Si le binaire utilise le temps comme seed (vérification anti-debug par timing, chapitre 19), le résultat sera un bitvector symbolique non contraint — ce qui peut fonctionner ou non selon la suite du programme. `rand()` est modélisé par une SimProcedure qui retourne une valeur symbolique.

### Stratégie générale

Face à un binaire qui interagit fortement avec son environnement, la meilleure approche est de **réduire le périmètre** de l'exécution symbolique. Au lieu de simuler le programme entier, isolez la fonction de vérification et alimentez-la avec des entrées symboliques via `blank_state` (section 18.3). Toute la logique d'I/O, de réseau et de système se retrouve hors du périmètre d'analyse.

---

## Limite n°4 — La complexité des expressions symboliques

### Le problème

À mesure que l'exécution symbolique progresse, les expressions symboliques associées aux registres et à la mémoire deviennent de plus en plus complexes. Une valeur qui commence comme le simple symbole **α** peut, après quelques dizaines d'instructions, devenir une expression de plusieurs milliers de nœuds dans l'arbre syntaxique interne de claripy.

Chaque fois que le moteur doit bifurquer (évaluer si une condition symbolique peut être vraie **et** fausse), il interroge Z3 avec l'expression courante plus toutes les contraintes de chemin accumulées. Si l'expression est énorme, Z3 peut mettre des secondes — voire des minutes — à répondre pour **chaque** bifurcation.

### Symptômes

- L'exploration ralentit progressivement même si le nombre d'états actifs reste stable.  
- Les appels au solveur (visibles en activant le logging de claripy) prennent de plus en plus de temps.  
- La consommation mémoire augmente à cause de la taille des expressions stockées dans chaque état.

### Stratégies de mitigation

**Concrétiser les valeurs non pertinentes** — Si certaines variables n'influencent pas la condition de succès, vous pouvez les fixer à des valeurs concrètes pour simplifier les expressions. Par exemple, si le programme lit un fichier de configuration qui n'a rien à voir avec la validation du serial, simulez-le avec un contenu concret plutôt que symbolique.

**Activer la simplification agressive** — angr simplifie les expressions par défaut, mais vous pouvez forcer des passes supplémentaires :

```python
state.options.add(angr.options.OPTIMIZE_IR)
```

**Utiliser le mode unicorn** — Pour les portions de code purement concrètes (aucune valeur symbolique impliquée), angr peut basculer sur le moteur d'émulation unicorn, beaucoup plus rapide :

```python
state.options.add(angr.options.UNICORN)
```

Attention : unicorn ne peut pas exécuter du code qui manipule des valeurs symboliques. angr bascule automatiquement entre SimEngine (symbolique) et unicorn (concret) selon le contexte.

---

## Limite n°5 — La mémoire symbolique et les pointeurs symboliques

### Le problème

Quand le programme effectue un accès mémoire avec un pointeur dont la valeur est symbolique (par exemple `array[x]` où `x` est symbolique), angr doit considérer **toutes** les adresses possibles que ce pointeur pourrait prendre. C'est le problème de l'accès mémoire symbolique.

```c
char table[256] = { ... };  
char result = table[user_input[0]];  // user_input[0] est symbolique  
```

L'index `user_input[0]` peut valoir n'importe quel octet de 0 à 255. Le résultat `result` dépend donc de 256 cas possibles. angr doit soit bifurquer en 256 états (un par valeur possible de l'index), soit construire une expression symbolique géante de type `If(index == 0, table[0], If(index == 1, table[1], ...))`.

### La double peine

Les accès mémoire symboliques combinent deux problèmes :

1. **Explosion des états** si angr bifurque pour chaque valeur possible du pointeur.  
2. **Expressions géantes** si angr utilise des `If` imbriqués pour représenter toutes les possibilités.

Dans les deux cas, une seule instruction machine (`movzx eax, byte [rsi + rax]`) peut suffire à faire dérailler l'exploration.

### Stratégies de mitigation

**Contraindre l'index** — Si vous savez que l'index est limité (par exemple, un caractère ASCII imprimable entre 0x20 et 0x7E), ajoutez cette contrainte. Cela réduit le nombre de cas de 256 à 95.

**Hooker l'accès** — Si l'accès indexé fait partie d'une fonction connue (table de substitution, S-box), remplacez la fonction entière par une SimProcedure ou passez à Z3 comme vu en section 18.4.

**Concretization strategies** — angr propose des stratégies de concrétisation pour les adresses symboliques. Par défaut, il utilise une stratégie « any » qui choisit une valeur concrète possible et continue. Vous pouvez configurer des stratégies plus élaborées :

```python
# Concrétiser les lectures mémoire symboliques en choisissant
# une valeur parmi les possibles (perte de complétude)
state.memory.read_strategies = [
    angr.concretization_strategies.SimConcretizationStrategyRange(128)
]
```

---

## Limite n°6 — Les fonctions cryptographiques et le hashing

### Le problème

Les fonctions de hachage cryptographiques (SHA-256, MD5, bcrypt…) sont conçues pour être des fonctions à sens unique : il est computationnellement infaisable d'inverser le résultat pour trouver l'entrée. Cette propriété résiste aussi aux solveurs SMT.

Si un binaire fait :

```c
if (sha256(input) == expected_hash)
    success();
```

L'exécution symbolique propagera les expressions à travers les centaines d'opérations de SHA-256, produisant une expression symbolique d'une complexité colossale. Quand le solveur devra satisfaire `expression_sha256(α) == expected_hash`, il sera confronté au même problème que celui de l'inversion cryptographique — et il échouera, soit par timeout, soit par épuisement mémoire.

### Pourquoi notre keygenme fonctionne et pas SHA-256

Notre keygenme utilise un réseau de Feistel avec une fonction `mix32` qui ressemble superficiellement à du hashing. Pourquoi Z3 le résout-il en millisecondes ?

La différence est la **taille** et la **structure** du problème :

- `mix32` effectue environ 10 opérations sur des bitvectors de 32 bits. SHA-256 en effectue plusieurs milliers sur des bitvectors de 32 bits avec des substitutions non linéaires.  
- Le Feistel à 4 tours produit une expression symbolique de quelques centaines de nœuds. SHA-256 en produit des centaines de milliers.  
- Les opérations de `mix32` (XOR, shift, multiplication) restent dans le domaine des bitvectors linéaires ou quasi-linéaires. SHA-256 utilise des opérations non linéaires spécifiquement choisies pour résister à la résolution algébrique.

### Stratégies de contournement

**Extraire la clé à l'exécution** — Plutôt que d'inverser le hash, utilisez GDB ou Frida (chapitre 13) pour intercepter la valeur **avant** qu'elle ne soit hashée. C'est l'approche du chapitre 24 (reverse de binaires avec chiffrement).

**Hooker la fonction de hachage** — Remplacez `sha256` par une SimProcedure qui retourne une valeur symbolique non contrainte. L'exécution symbolique contournera le hash et résoudra les contraintes **autour** de l'appel au hash. Vous obtiendrez le résultat attendu du hash (la valeur qui doit entrer dans le hash pour que le programme réussisse), mais pas l'entrée qui produit ce résultat :

```python
class SkipSHA256(angr.SimProcedure):
    def run(self, input_ptr, input_len, output_ptr):
        # Écrire un résultat symbolique de 256 bits
        result = self.state.solver.BVS("sha256_out", 256)
        self.state.memory.store(output_ptr, result)

proj.hook_symbol("SHA256", SkipSHA256())
```

**Combiner avec le bruteforce** — Si le hash porte sur une petite entrée (un PIN à 4 chiffres, un token court), le bruteforce classique peut être plus efficace que l'exécution symbolique. Parfois, la meilleure solution est la plus simple.

---

## Tableau récapitulatif des limites et remèdes

| Limite | Cause | Symptôme principal | Remèdes |  
|---|---|---|---|  
| **Explosion des chemins** | Trop de branchements symboliques | Milliers d'états actifs, mémoire qui grimpe | `avoid` agressif, contraindre les entrées, `blank_state`, Veritesting |  
| **Boucles symboliques** | Boucle dont la borne ou le corps dépend d'une valeur symbolique | États qui ne progressent jamais au-delà de la boucle | `LoopSeer`, hooker la boucle, passer à Z3 |  
| **Appels système** | Interactions avec l'OS non modélisées | États dans `errored`, comportement incorrect | SimProcedures custom, `SimFile`, réduire le périmètre |  
| **Expressions complexes** | Accumulation d'opérations symboliques | Ralentissement progressif du solveur | Concrétiser les valeurs non pertinentes, mode unicorn, `OPTIMIZE_IR` |  
| **Pointeurs symboliques** | Accès mémoire avec index symbolique | Explosion d'états ou expressions `If` géantes | Contraindre l'index, hooker l'accès, stratégies de concrétisation |  
| **Crypto / hashing** | Fonctions conçues pour résister à l'inversion | Timeout du solveur | Extraire les valeurs à l'exécution (GDB/Frida), hooker le hash, bruteforce |

---

## Savoir abandonner : quand l'exécution symbolique n'est pas la réponse

Il existe des catégories entières de binaires pour lesquelles l'exécution symbolique n'est pas l'outil adapté :

**Binaires massivement multi-threadés** — angr ne modélise pas le scheduling des threads. Les race conditions et les synchronisations (mutex, semaphores) sont invisibles.

**Code auto-modifiant** — Si le binaire modifie ses propres instructions en mémoire à l'exécution (technique anti-reversing, chapitre 19), angr exécute le code original, pas le code modifié. Il faudra d'abord unpacker le binaire (chapitre 29).

**Binaires avec obfuscation de flux de contrôle** — Le Control Flow Flattening (chapitre 19) remplace les branchements normaux par un dispatcher central avec une variable d'état. L'exécution symbolique voit des dizaines de branches à chaque itération du dispatcher, ce qui provoque une explosion des chemins immédiate. Des outils spécialisés de désobfuscation sont nécessaires en amont.

**Logique dominée par les I/O** — Un serveur web, un client réseau, une application GUI… Si la logique intéressante est intimement mêlée aux entrées/sorties, l'exécution symbolique passera son temps à modéliser des interactions système au lieu de résoudre des contraintes utiles. Frida (chapitre 13) est souvent plus adapté dans ce cas.

**Grandes bases de code** — Un binaire de plusieurs mégaoctets avec des centaines de fonctions n'est pas un bon candidat pour l'exécution symbolique globale. Isolez la fonction d'intérêt et travaillez sur ce fragment.

La capacité à reconnaître ces situations rapidement et à choisir un autre outil est une compétence tout aussi importante que la maîtrise d'angr.

---

## Diagnostiquer un script angr bloqué

Quand votre script tourne depuis trop longtemps, voici une checklist de diagnostic à suivre dans l'ordre :

**1. Inspecter les stashes**

```python
print(f"active:    {len(simgr.active)}")  
print(f"found:     {len(simgr.found)}")  
print(f"avoided:   {len(simgr.avoided)}")  
print(f"deadended: {len(simgr.deadended)}")  
print(f"errored:   {len(simgr.errored)}")  
```

- `active` en croissance continue → explosion des chemins.  
- `errored` non vide → appel système ou instruction non supportée.  
- `deadended` plein mais `found` vide → la cible n'est jamais atteinte, vérifiez l'adresse `find`.

**2. Examiner où se trouvent les états actifs**

```python
for s in simgr.active[:10]:
    print(hex(s.addr))
```

Si tous les états sont à des adresses proches, ils sont probablement coincés dans une boucle. Si les adresses sont très dispersées, c'est une explosion de chemins classique.

**3. Examiner les erreurs**

```python
for err in simgr.errored:
    print(err)
```

Les erreurs vous indiqueront souvent la cause exacte : syscall non supporté, accès mémoire impossible, division par zéro symbolique…

**4. Activer le logging**

```python
import logging  
logging.getLogger("angr").setLevel(logging.INFO)  
# Ou pour le solveur :
logging.getLogger("claripy").setLevel(logging.DEBUG)
```

Le logging verbose d'angr montre chaque bloc de base exécuté, chaque bifurcation et chaque appel au solveur. C'est bavard mais diagnostique.

**5. Limiter le temps**

Plutôt que d'attendre indéfiniment, fixez une limite de temps raisonnable et analysez l'état de l'exploration à l'expiration :

```python
import signal

def timeout_handler(signum, frame):
    raise TimeoutError("Exploration trop longue")

signal.signal(signal.SIGALRM, timeout_handler)  
signal.alarm(120)  # 2 minutes maximum  

try:
    simgr.explore(find=target, avoid=bad)
except TimeoutError:
    print("Timeout atteint. État de l'exploration :")
    print(f"  active: {len(simgr.active)}")
    print(f"  found:  {len(simgr.found)}")
    # Analyser les états actifs pour comprendre le blocage
```

---

## Points clés à retenir

- L'**explosion des chemins** est la limite fondamentale de l'exécution symbolique : chaque branchement symbolique peut doubler le nombre d'états. Les remèdes sont l'élagage (`avoid`), les contraintes sur les entrées, le démarrage tardif (`blank_state`), et les techniques d'exploration (Veritesting, LoopSeer).

- Les **boucles symboliques** sont un cas particulier de l'explosion des chemins. `LoopSeer` avec une borne raisonnable est la première chose à essayer. Hooker la boucle ou passer à Z3 sont les alternatives.

- Les **appels système** et interactions avec l'environnement sont partiellement modélisés. Réduire le périmètre d'analyse à la fonction cible est la stratégie la plus fiable.

- Les **fonctions cryptographiques** résistent à l'inversion symbolique par conception. L'extraction dynamique des valeurs (GDB/Frida) est souvent la seule option viable.

- Savoir **diagnostiquer** un script bloqué (inspecter les stashes, examiner les adresses des états actifs, lire les erreurs) est aussi important que savoir écrire le script.

- Savoir **abandonner** l'exécution symbolique au profit d'un autre outil (Frida, GDB, analyse manuelle) est une compétence à part entière. L'exécution symbolique est un outil dans votre boîte à outils, pas une solution universelle.

---

> Dans la section suivante (18.6), nous verrons comment **combiner** l'exécution symbolique avec le reverse engineering manuel pour obtenir le meilleur des deux mondes — la puissance du solveur et l'intuition de l'analyste.

⏭️ [Combinaison avec le RE manuel : quand utiliser l'exécution symbolique](/18-execution-symbolique/06-combinaison-re-manuel.md)
