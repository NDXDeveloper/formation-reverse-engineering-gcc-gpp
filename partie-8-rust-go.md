🔝 Retour au [Sommaire](/SOMMAIRE.md)

# Partie VIII — Bonus : RE de Binaires Rust et Go

Rust et Go s'imposent dans des domaines où le C/C++ régnait : outils système, infrastructure réseau, CLI, malware, et de plus en plus de challenges CTF. Les deux produisent des binaires ELF natifs — souvent via le linker GNU — mais leur RE ne ressemble en rien à celui du C/C++. Un binaire Rust de 500 lignes pèse plusieurs mégaoctets à cause du linking statique de la stdlib. Un binaire Go embarque son propre runtime (scheduler, garbage collector) et utilise une convention d'appel atypique. Les noms de fonctions, la représentation des strings, les structures de données en mémoire — tout est différent. Cette partie vous apprend à reconnaître ces spécificités et à adapter vos outils en conséquence.

---

## 🎯 Objectifs de cette partie

À l'issue de ces deux chapitres, vous serez capable de :

1. **Identifier un binaire Rust ou Go** dès le triage initial et adapter immédiatement votre stratégie d'analyse (outils, signatures, hypothèses sur le layout mémoire).  
2. **Reverser un binaire Rust** : décoder le name mangling, reconnaître les patterns `Option`/`Result`/`match`/`panic`, comprendre la représentation mémoire des strings (`&str` vs `String`, pas de null terminator), et filtrer le bruit de la stdlib linkée statiquement avec `cargo-bloat` et les signatures Ghidra.  
3. **Reverser un binaire Go** : naviguer dans le runtime (goroutines, scheduler, GC), comprendre la convention d'appel (stack-based puis register-based depuis Go 1.17), interpréter les structures internes (`slice`, `map`, `interface`, `channel`), et retrouver les noms de fonctions via `gopclntab` même sur un binaire strippé.  
4. **Appliquer les outils de RE natif** (Ghidra, GDB, Frida) sur des binaires Rust et Go en sachant où ils atteignent leurs limites et quels plugins ou scripts spécialisés comblent ces lacunes.  
5. **Comparer les patterns assembleur** produits par GCC (C/C++), rustc et le compilateur Go pour un même algorithme, et tirer parti de ces différences lors de l'analyse.

---

## 📋 Chapitres

| N° | Titre | Langage | Défis spécifiques | Lien |  
|----|-------|---------|-------------------|------|  
| 33 | RE de binaires Rust | Rust | Name mangling distinct du C++, patterns `Option`/`Result`/`match`/`panic` omniprésents, strings sans null terminator, stdlib linkée statiquement (~4 Mo de bruit), signatures Ghidra dédiées. | [Chapitre 33](/33-re-rust/README.md) |  
| 34 | RE de binaires Go | Go | Runtime embarqué (goroutines, GC, scheduler), convention d'appel non standard, strings `(ptr, len)`, structures internes (`slice`, `map`, `interface`), table `gopclntab` pour retrouver les symboles sur binaires strippés. | [Chapitre 34](/34-re-go/README.md) |

---

## 🆚 Rust vs Go vs C++ en RE

| Critère | C/C++ (GCC) | Rust (rustc) | Go |  
|---------|-------------|-------------|-----|  
| **Name mangling** | Itanium ABI (`_ZN...`), démanglé par `c++filt` | Format propre (`_ZN...` + hash), démanglé par `rustfilt` | Pas de mangling classique — noms lisibles avec chemin complet (`main.processData`) |  
| **Convention d'appel** | System V AMD64 (registres `rdi`, `rsi`, `rdx`…) | System V AMD64 (identique au C) | Stack-based (≤1.16), register-based custom (≥1.17) — ni System V ni Windows |  
| **Strings** | Null-terminated (`char*`) | `&str` = `(ptr, len)`, pas de `\0` ; `String` = `(ptr, len, capacity)` | `(ptr, len)`, pas de `\0` — similaire à Rust |  
| **Linking** | Dynamique par défaut (libc, libstdc++) | Statique par défaut (stdlib entière embarquée) | Statique par défaut (runtime + stdlib embarqués) |  
| **Taille binaire (hello world)** | ~16 Ko (dynamique) | ~4 Mo (statique, stdlib incluse) | ~2 Mo (statique, runtime inclus) |  
| **Outils spécialisés** | `c++filt`, Ghidra, IDA (support natif) | `rustfilt`, `cargo-bloat`, signatures Ghidra Rust | `go_parser` (Ghidra/IDA), `gopclntab`, `redress` |

Le point central : Ghidra, GDB et Frida fonctionnent sur les trois, mais leur decompiler produit des résultats très bruités sur Rust et Go sans signatures adaptées. L'effort de cette partie porte sur la reconnaissance des patterns spécifiques à chaque langage et l'utilisation des bons plugins pour filtrer le bruit.

---

## ⏱️ Durée estimée

**~8-12 heures** pour un praticien du RE natif C/C++.

Le chapitre 33 (Rust, ~4-6h) est plus dense car les patterns Rust en assembleur sont verbeux — la gestion systématique des `Result`/`Option` et les panics génèrent beaucoup de code que vous apprendrez à filtrer. Le chapitre 34 (Go, ~4-6h) demande un effort d'adaptation sur la convention d'appel et les structures internes, mais la table `gopclntab` facilite considérablement le travail sur les binaires strippés — un luxe que ni le C ni le Rust n'offrent.

Si vous n'avez jamais écrit de Rust ou de Go, prévoyez ~2h supplémentaires par langage pour lire un tutoriel d'introduction à la syntaxe. Vous n'avez pas besoin de maîtriser ces langages — juste de reconnaître leurs constructions de base quand vous les croisez dans le decompiler.

---

## 📌 Prérequis

**Obligatoires :**

- Avoir complété la **[Partie I](/partie-1-fondamentaux.md)** (format ELF, assembleur x86-64, conventions d'appel) et la **[Partie II](/partie-2-analyse-statique.md)** (Ghidra, `objdump`, `readelf`, `strings`).  
- Savoir naviguer dans Ghidra : import, decompiler, XREF, renommage de fonctions.

**Recommandé :**

- Avoir complété le chapitre 16 (optimisations compilateur) et le chapitre 17 (RE C++ avec GCC) de la **[Partie IV](/partie-4-techniques-avancees.md)** — la comparaison avec les patterns C++ est un fil rouge de cette partie.  
- Avoir complété au moins un cas pratique de la **[Partie V](/partie-5-cas-pratiques.md)** pour avoir le réflexe du workflow complet triage → analyse statique → analyse dynamique.  
- Une familiarité de base avec la syntaxe Rust et/ou Go. Pas besoin de savoir écrire du code — juste de reconnaître un `match`, un `Result<T, E>`, une goroutine ou un `defer` quand vous les voyez.

---

## ⬅️ Partie précédente

← [**Partie VII — Bonus : RE sur Binaires .NET / C#**](/partie-7-dotnet.md)

## ➡️ Partie suivante

Pour clôturer la formation : automatisation de vos workflows RE (scripts Python, Ghidra headless, YARA, pipelines CI/CD) et ressources pour continuer à progresser (CTF, lectures, certifications, communautés).

→ [**Partie IX — Ressources & Automatisation**](/partie-9-ressources.md)

⏭️ [Chapitre 33 — Reverse Engineering de binaires Rust](/33-re-rust/README.md)
