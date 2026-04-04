🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Solution — Checkpoint Chapitre 9

## Comparatif Ghidra vs Radare2/Cutter sur `keygenme_O2_strip`

> ⚠️ **Spoiler** — Ce document est le corrigé du checkpoint du chapitre 9. Essayez de réaliser l'exercice par vous-même avant de consulter cette solution.  
>  
> Cette solution utilise la combinaison **Ghidra + Cutter (Radare2)** comme paire d'outils. Les observations seraient similaires avec d'autres combinaisons ; les points de divergence spécifiques changeraient, mais la méthodologie reste la même.

---

## 1. Reconnaissance de fonctions

### Ghidra

Après import et auto-analyse (options par défaut, « Analyze All »), Ghidra détecte **9 fonctions** dans le binaire :

| Adresse | Nom attribué par Ghidra |  
|---|---|  
| `0x00401050` | `entry` |  
| `0x00401080` | `FUN_00401080` |  
| `0x004010b0` | `FUN_004010b0` |  
| `0x004010f0` | `FUN_004010f0` |  
| `0x00401110` | `FUN_00401110` |  
| `0x00401120` | `FUN_00401120` |  
| `0x00401160` | `main` |  
| `0x004011d0` | `FUN_004011d0` |  
| `0x004011e0` | `FUN_004011e0` |

Ghidra identifie `main` automatiquement en analysant le premier argument passé à `__libc_start_main` dans `entry`. Les fonctions `FUN_00401080` à `FUN_00401110` correspondent aux fonctions d'infrastructure GCC (`deregister_tm_clones`, `register_tm_clones`, `__do_global_dtors_aux`, `frame_dummy`). `FUN_004011d0` et `FUN_004011e0` sont `__libc_csu_fini` et `__libc_csu_init`.

### Cutter (Radare2)

Après ouverture avec analyse `aaaa`, Cutter détecte **9 fonctions** :

| Adresse | Nom attribué par Cutter |  
|---|---|  
| `0x00401050` | `entry0` |  
| `0x00401080` | `sym.deregister_tm_clones` |  
| `0x004010b0` | `sym.register_tm_clones` |  
| `0x004010f0` | `sym.__do_global_dtors_aux` |  
| `0x00401110` | `sym.frame_dummy` |  
| `0x00401120` | `fcn.00401120` |  
| `0x00401160` | `main` |  
| `0x004011d0` | `sym.__libc_csu_fini` |  
| `0x004011e0` | `sym.__libc_csu_init` |

### Observations

Le nombre total de fonctions détectées est **identique** (9 fonctions). C'est attendu sur un petit binaire proprement compilé par GCC : les points d'entrée sont clairement définis et les prologues de fonctions sont standards.

La différence la plus visible est le **nommage**. Radare2 reconnaît et nomme automatiquement les fonctions d'infrastructure GCC (`deregister_tm_clones`, `register_tm_clones`, `__do_global_dtors_aux`, `frame_dummy`, `__libc_csu_init`, `__libc_csu_fini`), alors que Ghidra les laisse en `FUN_*` générique. Cela s'explique par les heuristiques de nommage de `r2` qui identifient ces fonctions par leur pattern d'octets et leur position relative dans la section `.text`. C'est un avantage ergonomique de Radare2 sur ce type de binaire : le triage initial est plus rapide car les fonctions d'infrastructure sont immédiatement reconnaissables et peuvent être ignorées.

Les deux outils identifient `main` correctement via le même mécanisme (analyse du premier argument de `__libc_start_main`).

La fonction applicative à `0x00401120` est nommée `FUN_00401120` par Ghidra et `fcn.00401120` par Cutter — ni l'un ni l'autre ne peut lui donner un nom significatif sans symboles. C'est la fonction `transform_key` du code source original (qu'on peut vérifier en ouvrant la version `keygenme_O0` avec symboles).

## 2. Décompilé de la fonction de vérification

La fonction principale de vérification est `main` à `0x00401160`. Voici les pseudo-codes produits par chaque outil.

### Ghidra

```c
undefined8 main(void)
{
  int iVar1;
  char local_1c [20];
  
  puts("Enter key: ");
  __isoc99_scanf("%25s", local_1c);
  FUN_00401120(local_1c);
  iVar1 = strcmp(local_1c, "s3cr3t_k3y");
  if (iVar1 == 0) {
    puts("Access granted");
  }
  else {
    puts("Wrong key");
  }
  return 0;
}
```

### Cutter (décompileur r2ghidra / Ghidra intégré)

```c
int32_t main(void)
{
  int32_t iVar1;
  char s [20];

  puts("Enter key: ");
  __isoc99_scanf("%25s", s);
  fcn.00401120(s);
  iVar1 = strcmp(s, "s3cr3t_k3y");
  if (iVar1 == 0) {
    puts("Access granted");
  } else {
    puts("Wrong key");
  }
  return 0;
}
```

> 💡 **Note** : Cutter utilise le plugin `r2ghidra` (ou `rz-ghidra`) qui embarque le même moteur de décompilation que Ghidra. Les résultats sont donc structurellement très similaires. Pour une comparaison plus contrastée, on pourrait utiliser le décompileur natif de `r2` (`pdc`) ou Binary Ninja Cloud. L'exemple reste instructif car les différences, même mineures, illustrent l'impact de l'intégration.

### Comparaison ligne par ligne

**Lisibilité.** Les deux pseudo-codes sont très proches et immédiatement compréhensibles. La logique du crackme est limpide dans les deux cas : lire une entrée, la transformer, la comparer à une valeur attendue.

**Types de retour.** Ghidra infère `undefined8` pour le retour de `main`, ce qui est son type par défaut pour les valeurs 64 bits non résolues. Cutter affiche `int32_t`, ce qui est plus proche de la réalité (le `return 0` en fin de fonction correspond à `eax = 0`, soit un retour 32 bits). Avantage Cutter sur ce point, bien que la différence soit cosmétique.

**Noms de variables.** Ghidra nomme le buffer `local_1c` (offset par rapport au frame pointer), Cutter le nomme `s`. Le nom `s` est plus lisible mais moins informatif sur l'emplacement en mémoire. Ghidra nomme le résultat de `strcmp` `iVar1`, Cutter également `iVar1` — même convention car même moteur de décompilation sous-jacent.

**Structure de contrôle.** Les deux outils reconstruisent un `if/else` propre, sans `goto`. La condition (`iVar1 == 0`) est identique. La correspondance directe avec le `jne` à `0x0040119a` dans le désassemblage est correcte dans les deux cas.

**Appel à la fonction de transformation.** Ghidra affiche `FUN_00401120(local_1c)` tandis que Cutter affiche `fcn.00401120(s)`. La sémantique est identique, seuls les noms diffèrent. Les deux outils détectent correctement que cette fonction prend le buffer en paramètre (passage par `rdi` selon la convention System V).

**Chaîne de comparaison.** Les deux outils extraient correctement la chaîne `"s3cr3t_k3y"` passée à `strcmp`. Cette chaîne est la « clé attendue » du crackme (après transformation de l'entrée par `FUN_00401120`/`fcn.00401120`).

**Erreurs ou artefacts.** Aucun artefact ni erreur notable dans l'un ou l'autre outil sur cette fonction. La simplicité du code `-O2` pour `main` (pas d'inlining complexe, pas de vectorisation) explique cette convergence.

### Divergence sur `FUN_00401120` / `fcn.00401120`

La comparaison est plus intéressante sur la fonction `transform_key` à `0x00401120`, car le compilateur y a appliqué des optimisations (`-O2`).

**Ghidra** produit une boucle `while` avec des opérations XOR et des décalages sur les caractères du buffer. La structure est correcte mais les expressions intermédiaires utilisent des casts `(int)(char)` verbeux.

**Cutter (r2ghidra)** produit un résultat quasi-identique, avec de légères variations dans l'ordre des sous-expressions et dans le nommage des temporaires. Ceci confirme que les deux utilisent le même moteur.

Pour une vraie divergence de décompilation, il faudrait comparer Ghidra avec le **décompileur natif de `r2`** (`pdc`) ou avec **Binary Ninja HLIL** :

- `pdc` (Radare2 natif) produit un pseudo-code nettement plus rudimentaire sur cette fonction : pas de reconstruction de boucle `while`, conditions exprimées sous forme de `goto`, variables nommées par leur registre source (`rdi`, `rsi`). La structure de la transformation est beaucoup plus difficile à lire.  
- Binary Ninja HLIL (si utilisé comme second outil) tend à mieux simplifier les expressions arithmétiques de la boucle de transformation, produisant un code plus compact.

## 3. Cross-references et navigation

### Ghidra

1. Ouvrir la fenêtre *Defined Strings* (menu *Window → Defined Strings*).  
2. Taper « granted » dans la barre de filtre — la chaîne `"Access granted"` apparaît à `0x0040200e`.  
3. Double-cliquer navigue vers l'adresse en `.rodata`.  
4. Clic droit sur la chaîne → *References → Show References to Address* (ou raccourci selon la version).  
5. Une seule XREF apparaît : `main` à `0x0040119c` (instruction `LEA RDI, [.rodata:"Access granted"]`).  
6. Double-cliquer sur la XREF navigue vers l'instruction dans `main`.  
7. Bascule vers la vue décompilée via le panneau Decompiler.

**Total : 4 actions** (filtre chaînes → double-clic → XREF → double-clic). La navigation est fluide, les panneaux restent synchronisés.

### Cutter

1. Ouvrir le widget *Strings* (panneau latéral ou menu *Windows → Strings*).  
2. Taper « granted » dans le filtre — la chaîne apparaît à `0x0040200e`.  
3. Double-cliquer navigue vers l'adresse dans la vue désassemblage.  
4. Clic droit → *Show X-Refs* (ou touche `X`).  
5. Une XREF apparaît : `main+0x3c` à `0x0040119c`.  
6. Double-cliquer navigue vers l'instruction.  
7. Le widget décompileur (r2ghidra) se synchronise automatiquement.

**Total : 4 actions** identiques. L'ergonomie est comparable. Cutter affiche la XREF avec le format `main+0x3c` (offset relatif au début de la fonction), ce qui est légèrement plus informatif que l'adresse absolue seule affichée par Ghidra.

### Variante CLI (Radare2 pur)

En CLI, le même workflow se résume à 5 commandes comme détaillé en section 9.3 :

```
iz~granted → s 0x0040200e → axt → s main → pdf
```

La vitesse brute en CLI est supérieure (pas de navigation dans les menus), mais l'absence de synchronisation visuelle entre vues oblige à enchaîner mentalement les résultats.

## 4. Annotations et renommages

### Ghidra

- **Renommer `main`** — déjà nommé automatiquement, pas de changement nécessaire.  
- **Renommer `FUN_00401120`** — clic droit dans le Listing ou le Symbol Tree → *Rename Function* → saisir `transform_key` → Entrée. Le nom se propage immédiatement dans le désassemblage, le décompileur, et le Symbol Tree.  
- **Renommer `local_1c`** dans le décompileur — clic droit sur `local_1c` → *Rename Variable* → saisir `user_input`. Le pseudo-code met à jour toutes les occurrences dans la fonction. Le Listing (vue assembleur) affiche un commentaire reflétant le nouveau nom.  
- **Renommer `iVar1`** → `cmp_result`. Même procédure, propagation immédiate.  
- **Persistance** — les annotations sont sauvegardées automatiquement dans le fichier projet Ghidra (`.gpr` / `.rep`). Après fermeture et réouverture, tous les renommages sont conservés.

### Cutter

- **Renommer `fcn.00401120`** — clic droit dans le panneau Functions → *Rename* → saisir `transform_key`. Propagation immédiate dans le désassemblage et les XREF. Le décompileur met à jour l'appel.  
- **Renommer `s`** dans le décompileur — clic droit → *Rename* → `user_input`. La propagation dans le pseudo-code est immédiate.  
- **Renommer `iVar1`** → `cmp_result`. Même procédure.  
- **Persistance** — Cutter propose de sauvegarder le projet (*File → Save Project*). Après réouverture, les annotations sont conservées.

### Observations

L'expérience est très similaire dans les deux outils. Les deux supportent le renommage depuis la vue décompilée, ce qui est le workflow le plus naturel (on renomme ce qu'on comprend, au moment où on le comprend). La propagation est immédiate dans les deux cas.

Une différence mineure : dans Ghidra, le raccourci clavier `L` permet de renommer directement depuis le Listing sans passer par un menu contextuel, ce qui est légèrement plus rapide. Dans Cutter, la touche `N` (héritée de `r2`) joue un rôle similaire en mode désassemblage, mais pas dans le panneau de décompilation où le clic droit est nécessaire.

## 5. Synthèse et préférence argumentée

### Bilan sur `keygenme_O2_strip`

Sur ce binaire précis (petit, ELF x86-64, GCC `-O2`, strippé mais sans obfuscation), les deux outils produisent des résultats d'analyse très proches. Le nombre de fonctions détectées est identique, la qualité du décompilé est comparable (d'autant que Cutter utilise le même moteur Ghidra), et les opérations de navigation et d'annotation sont fonctionnellement équivalentes.

### Avantages observés de Ghidra

- L'interface décompileur est nativement intégrée et ne dépend d'aucun plugin additionnel.  
- Le panneau Symbol Tree offre une vue hiérarchique complète (fonctions, labels, classes, namespaces) absente de Cutter.  
- La documentation officielle et les tutoriels disponibles sont plus nombreux et plus structurés.  
- La possibilité de définir des structures de données et de les appliquer au binaire (menu *Data Type Manager*) est plus avancée que dans Cutter.

### Avantages observés de Cutter / Radare2

- Le nommage automatique des fonctions d'infrastructure GCC accélère le triage initial : on identifie immédiatement les fonctions à ignorer.  
- Le Dashboard offre un résumé visuel instantané (architecture, protections, entropie, hashes) sans ouvrir de fenêtres supplémentaires.  
- La console `r2` intégrée permet de basculer vers le CLI pour les opérations rapides (recherche, filtrage, export JSON) sans quitter l'interface.  
- La commande `pds` (résumé appels + chaînes) n'a pas d'équivalent direct dans Ghidra et permet un triage de fonction en une commande.

### Recommandation personnelle (à adapter)

Pour un binaire de cette taille et de cette complexité, **Ghidra seul suffit largement**. L'apport de Cutter/Radare2 se manifeste surtout dans deux situations :

- Quand on a besoin d'un **second avis** sur un décompilé suspect — même si dans ce cas précis, Cutter utilisant le même décompileur Ghidra, il faudrait plutôt utiliser Binary Ninja Cloud ou le `pdc` natif de `r2` pour obtenir une perspective réellement différente.  
- Quand on veut **scripter ou automatiser** une partie de l'analyse — `r2pipe` et le mode CLI sont nettement plus légers que le scripting Ghidra pour des tâches ponctuelles.

Sur un binaire plus gros, plus obfusqué, ou nécessitant un traitement batch, la combinaison Ghidra (décompilation) + Radare2 (scripting) deviendrait un avantage réel.

---

> ✅ Cette solution est un exemple de rapport attendu. Votre propre rapport peut diverger sur les observations spécifiques (surtout si vous avez choisi IDA Free ou Binary Ninja Cloud comme second outil), sur les préférences exprimées, et sur le niveau de détail. L'essentiel est d'avoir manipulé les deux outils et d'avoir formulé des observations concrètes et argumentées.  
>  
> 

⏭️
