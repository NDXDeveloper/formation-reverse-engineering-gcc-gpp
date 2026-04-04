// crackme_go — Binaire d'entraînement pour le Chapitre 34
// Formation Reverse Engineering — Applications compilées avec la chaîne GNU
//
// Objectif pédagogique :
//   Ce crackme illustre les structures internes de Go visibles en RE :
//   - Interfaces et dispatch dynamique (section 34.3)
//   - Goroutines et channels (section 34.1)
//   - Slices, maps et strings Go (sections 34.3 / 34.5)
//   - Convention d'appel Go (section 34.2)
//   - Noms de fonctions dans gopclntab (section 34.4)
//
// Usage : ./crackme_go <LICENSE_KEY>
// Format attendu : XXXX-XXXX-XXXX-XXXX (hex uppercase)
//
// Licence MIT — Usage strictement éducatif.

package main

import (
	"fmt"
	"os"
	"strings"
	"sync"
)

// ---------------------------------------------------------------------------
// Constantes et table de référence
// ---------------------------------------------------------------------------

// magic est utilisé comme seed XOR pour la validation.
// En RE, cette constante est repérable via strings ou .rodata.
var magic = [4]byte{0xDE, 0xAD, 0xC0, 0xDE}

// expectedSums est la table de checksums attendue pour chaque groupe.
// Chaque groupe fait 2 octets ; après XOR avec magic[0] et magic[1],
// la somme doit correspondre à la valeur ci-dessous.
// L'analyste devra extraire ces valeurs pour écrire un keygen.
var expectedSums = map[int]uint16{
	0: 0x010E, // 270
	1: 0x0122, // 290
	2: 0x0136, // 310
	3: 0x013E, // 318
}

// ---------------------------------------------------------------------------
// Interface Validator — illustre le dispatch virtuel Go (itab)
// ---------------------------------------------------------------------------

// Validator est une interface avec une seule méthode.
// En assembleur, l'appel passera par une itab (interface table).
type Validator interface {
	Validate(group []byte, index int) bool
}

// ---------------------------------------------------------------------------
// ChecksumValidator — vérifie la somme des octets XORés d'un groupe
// ---------------------------------------------------------------------------

// ChecksumValidator implémente Validator.
type ChecksumValidator struct {
	ExpectedSums map[int]uint16
}

// Validate additionne les octets du groupe XORés avec magic,
// puis compare avec la somme attendue.
func (cv *ChecksumValidator) Validate(group []byte, index int) bool {
	var sum uint16
	for i, b := range group {
		xored := b ^ magic[i%len(magic)]
		sum += uint16(xored)
	}
	expected, ok := cv.ExpectedSums[index]
	if !ok {
		return false
	}
	return sum == expected
}

// ---------------------------------------------------------------------------
// Parsing de la clé
// ---------------------------------------------------------------------------

// parseKey vérifie le format XXXX-XXXX-XXXX-XXXX et retourne 4 groupes
// de 2 octets chacun (chaque "XXXX" = 2 octets hex).
func parseKey(key string) ([][2]byte, error) {
	parts := strings.Split(key, "-")
	if len(parts) != 4 {
		return nil, fmt.Errorf("format invalide : attendu 4 groupes séparés par '-'")
	}

	groups := make([][2]byte, 4)
	for i, part := range parts {
		if len(part) != 4 {
			return nil, fmt.Errorf("groupe %d : attendu 4 caractères hex, reçu %d", i+1, len(part))
		}
		for j := 0; j < 2; j++ {
			hi := hexVal(part[j*2])
			lo := hexVal(part[j*2+1])
			if hi < 0 || lo < 0 {
				return nil, fmt.Errorf("groupe %d : caractère non-hex détecté", i+1)
			}
			groups[i][j] = byte(hi<<4) | byte(lo)
		}
	}
	return groups, nil
}

// hexVal convertit un caractère hex ASCII en valeur 0-15.
// Retourne -1 si le caractère n'est pas hexadécimal valide.
func hexVal(c byte) int {
	switch {
	case c >= '0' && c <= '9':
		return int(c - '0')
	case c >= 'A' && c <= 'F':
		return int(c-'A') + 10
	case c >= 'a' && c <= 'f':
		return int(c-'a') + 10
	default:
		return -1
	}
}

// ---------------------------------------------------------------------------
// Validation concurrente — illustre goroutines + channels + WaitGroup
// ---------------------------------------------------------------------------

// validationResult transporte le résultat d'une goroutine de validation.
type validationResult struct {
	Index int
	OK    bool
}

// validateGroups lance une goroutine par groupe pour la validation checksum.
// Utilise un channel pour collecter les résultats et un WaitGroup pour
// la synchronisation — patterns classiques visibles en RE Go.
func validateGroups(groups [][2]byte, v Validator) bool {
	ch := make(chan validationResult, len(groups))
	var wg sync.WaitGroup

	for i, g := range groups {
		wg.Add(1)
		go func(idx int, data [2]byte) {
			defer wg.Done()
			ok := v.Validate(data[:], idx)
			ch <- validationResult{Index: idx, OK: ok}
		}(i, g)
	}

	// Fermer le channel une fois toutes les goroutines terminées.
	go func() {
		wg.Wait()
		close(ch)
	}()

	for res := range ch {
		if !res.OK {
			return false
		}
	}
	return true
}

// ---------------------------------------------------------------------------
// Validation croisée — relation entre tous les groupes
// ---------------------------------------------------------------------------

// validateCross vérifie que le XOR global de tous les octets de la clé
// produit la valeur attendue (0x42). C'est un contrôle d'intégrité global.
func validateCross(groups [][2]byte) bool {
	var globalXOR byte
	for _, g := range groups {
		for _, b := range g {
			globalXOR ^= b
		}
	}
	return globalXOR == 0x42
}

// ---------------------------------------------------------------------------
// Vérification de l'ordre des groupes
// ---------------------------------------------------------------------------

// validateOrder s'assure que le premier octet de chaque groupe
// est strictement croissant. Cette contrainte impose un ordre
// sur les groupes et réduit l'espace de clés valides.
func validateOrder(groups [][2]byte) bool {
	prev := -1
	for _, g := range groups {
		val := int(g[0])
		if val <= prev {
			return false
		}
		prev = val
	}
	return true
}

// ---------------------------------------------------------------------------
// Point d'entrée
// ---------------------------------------------------------------------------

func main() {
	banner := `
   ╔══════════════════════════════════════════╗
   ║   crackme_go — Chapitre 34              ║
   ║   Formation Reverse Engineering GNU      ║
   ╚══════════════════════════════════════════╝`
	fmt.Println(banner)

	if len(os.Args) != 2 {
		fmt.Fprintf(os.Stderr, "\nUsage : %s <LICENSE_KEY>\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Format : XXXX-XXXX-XXXX-XXXX (hex uppercase)\n\n")
		os.Exit(1)
	}

	key := os.Args[1]
	groups, err := parseKey(key)
	if err != nil {
		fmt.Fprintf(os.Stderr, "\n[ERREUR] %s\n\n", err)
		os.Exit(1)
	}

	fmt.Printf("\n[*] Vérification de la clé : %s\n", key)

	// Étape 1 — Validation checksum par groupe (via interface + goroutines)
	cv := &ChecksumValidator{ExpectedSums: expectedSums}
	if !validateGroups(groups, cv) {
		fmt.Println("[✗] Échec : checksum de groupe invalide.")
		os.Exit(1)
	}
	fmt.Println("[✓] Checksums de groupes valides.")

	// Étape 2 — Validation de l'ordre croissant
	if !validateOrder(groups) {
		fmt.Println("[✗] Échec : contrainte d'ordre non respectée.")
		os.Exit(1)
	}
	fmt.Println("[✓] Contrainte d'ordre respectée.")

	// Étape 3 — Validation croisée XOR globale
	if !validateCross(groups) {
		fmt.Println("[✗] Échec : vérification croisée échouée.")
		os.Exit(1)
	}
	fmt.Println("[✓] Vérification croisée OK.")

	// Succès
	fmt.Println("\n══════════════════════════════════════")
	fmt.Println("  🎉  Clé valide ! Bravo, reverser !  ")
	fmt.Println("══════════════════════════════════════\n")
}
