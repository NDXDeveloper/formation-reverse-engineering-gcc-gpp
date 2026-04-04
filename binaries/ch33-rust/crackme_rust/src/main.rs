// ============================================================================
// Formation Reverse Engineering — Chapitre 33
// crackme_rust : binaire d'entraînement Rust
//
// Ce crackme illustre les patterns Rust visibles en RE :
//   - Option<T> / Result<T, E> et unwrap / match
//   - Enums avec données (tagged unions)
//   - Trait objects (dyn Trait) et vtables de trait
//   - Gestion des String / &str (fat pointers, pas de null terminator)
//   - Panics et messages d'erreur
//   - Itérateurs et closures
//
// Usage : ./crackme_rust <username> <serial>
// ============================================================================

use std::env;
use std::fmt;
use std::process;

// ---------------------------------------------------------------------------
// Constantes — repérables avec `strings` ou dans .rodata
// ---------------------------------------------------------------------------
const MAGIC: u32 = 0xDEAD_C0DE;
const VERSION: &str = "RustCrackMe-v3.3";
const SERIAL_PREFIX: &str = "RUST-";
const SERIAL_PARTS: usize = 4;
const SEPARATOR: char = '-';

// ---------------------------------------------------------------------------
// Enum avec données — produit un tag + payload en mémoire (tagged union)
// Reconnaissable en RE par les comparaisons sur le discriminant
// ---------------------------------------------------------------------------
#[derive(Debug, Clone)]
enum LicenseType {
    Trial { days_left: u32 },
    Standard { seats: u32 },
    Enterprise { seats: u32, support: bool },
}

impl LicenseType {
    /// Le `match` exhaustif sur cet enum génère une table de sauts
    /// ou une cascade de comparaisons selon le niveau d'optimisation.
    fn max_features(&self) -> u32 {
        match self {
            LicenseType::Trial { days_left } => {
                if *days_left > 0 { 5 } else { 0 }
            }
            LicenseType::Standard { seats } => 10 + seats,
            LicenseType::Enterprise { seats, support } => {
                let base = 50 + seats * 2;
                if *support { base + 100 } else { base }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Trait + implémentation — le trait object (dyn Validator) utilise un fat
// pointer (data_ptr, vtable_ptr) différent des vtables C++
// ---------------------------------------------------------------------------
trait Validator {
    fn name(&self) -> &str;
    fn validate(&self, input: &str) -> Result<(), String>;
}

/// Vérifie que le serial commence par le bon préfixe
struct PrefixValidator;

impl Validator for PrefixValidator {
    fn name(&self) -> &str {
        "PrefixCheck"
    }

    fn validate(&self, input: &str) -> Result<(), String> {
        if input.starts_with(SERIAL_PREFIX) {
            Ok(())
        } else {
            Err(format!(
                "Le serial doit commencer par '{}'",
                SERIAL_PREFIX
            ))
        }
    }
}

/// Vérifie le format : RUST-XXXX-XXXX-XXXX (4 groupes après le préfixe)
struct FormatValidator;

impl Validator for FormatValidator {
    fn name(&self) -> &str {
        "FormatCheck"
    }

    fn validate(&self, input: &str) -> Result<(), String> {
        let body = input.strip_prefix(SERIAL_PREFIX).unwrap_or(input);
        let parts: Vec<&str> = body.split(SEPARATOR).collect();

        if parts.len() != SERIAL_PARTS {
            return Err(format!(
                "Attendu {} groupes après le préfixe, trouvé {}",
                SERIAL_PARTS,
                parts.len()
            ));
        }

        // Chaque groupe doit faire exactement 4 caractères hexadécimaux
        for (i, part) in parts.iter().enumerate() {
            if part.len() != 4 {
                return Err(format!("Groupe {} : longueur invalide ({})", i + 1, part.len()));
            }
            if !part.chars().all(|c| c.is_ascii_hexdigit()) {
                return Err(format!("Groupe {} : caractères non hexadécimaux", i + 1));
            }
        }

        Ok(())
    }
}

/// Vérifie la cohérence mathématique du serial par rapport au username
struct ChecksumValidator {
    expected_checksum: u32,
}

impl ChecksumValidator {
    fn new(username: &str) -> Self {
        // Algorithme de dérivation de la checksum depuis le username
        // En RE, il faut reconstruire cette logique pour écrire un keygen
        let mut hash: u32 = MAGIC;
        for (i, byte) in username.bytes().enumerate() {
            hash = hash.wrapping_mul(31).wrapping_add(byte as u32);
            hash ^= (i as u32).wrapping_shl(((byte & 0x0F) + 1) as u32);
            hash = hash.rotate_left(7);
        }
        // Mélange final
        hash ^= hash >> 16;
        hash = hash.wrapping_mul(0x45D9_F3B1);
        hash ^= hash >> 13;
        hash = hash.wrapping_mul(0x1DA2_85FC);
        hash ^= hash >> 16;

        Self {
            expected_checksum: hash,
        }
    }
}

impl Validator for ChecksumValidator {
    fn name(&self) -> &str {
        "ChecksumCheck"
    }

    fn validate(&self, input: &str) -> Result<(), String> {
        let body = input
            .strip_prefix(SERIAL_PREFIX)
            .ok_or_else(|| String::from("Préfixe manquant"))?;

        // Parser les 4 groupes hexadécimaux en un u128, puis extraire les 32 bits bas
        let combined: Option<u128> = body
            .split(SEPARATOR)
            .map(|part| u32::from_str_radix(part, 16).ok().map(|v| v as u128))
            .try_fold(0u128, |acc, maybe_val| {
                maybe_val.map(|v| (acc << 16) | v)
            });

        match combined {
            Some(value) => {
                let serial_checksum = (value & 0xFFFF_FFFF) as u32;
                if serial_checksum == self.expected_checksum {
                    Ok(())
                } else {
                    Err(format!(
                        "Checksum invalide : attendu 0x{:08X}, obtenu 0x{:08X}",
                        self.expected_checksum, serial_checksum
                    ))
                }
            }
            // Ce None déclenche un chemin d'erreur reconnaissable en RE
            // (comparaison du discriminant de Option puis branchement)
            None => Err(String::from("Impossible de parser les groupes hexadécimaux")),
        }
    }
}

// ---------------------------------------------------------------------------
// Struct contenant un Vec<Box<dyn Validator>> — dispatch dynamique
// En RE : chaque appel passe par le vtable pointer du trait object
// ---------------------------------------------------------------------------
struct ValidationPipeline {
    validators: Vec<Box<dyn Validator>>,
}

impl ValidationPipeline {
    fn new() -> Self {
        Self {
            validators: Vec::new(),
        }
    }

    fn add(&mut self, v: Box<dyn Validator>) {
        self.validators.push(v);
    }

    /// Exécute tous les validateurs en séquence.
    /// Utilise un itérateur + closure — pattern fréquent en Rust.
    fn run(&self, serial: &str) -> Result<(), ValidationError> {
        for (idx, validator) in self.validators.iter().enumerate() {
            // Appel via le fat pointer (vtable dispatch)
            validator.validate(serial).map_err(|msg| ValidationError {
                step: idx + 1,
                validator_name: validator.name().to_string(),
                message: msg,
            })?;
        }
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Struct d'erreur custom — implémente Display et Debug
// Les messages formatés se retrouvent dans les chaînes du binaire
// ---------------------------------------------------------------------------
#[derive(Debug)]
struct ValidationError {
    step: usize,
    validator_name: String,
    message: String,
}

impl fmt::Display for ValidationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "[Étape {}/{}] {} : {}",
            self.step, SERIAL_PARTS, self.validator_name, self.message
        )
    }
}

// ---------------------------------------------------------------------------
// Fonctions utilitaires — illustrent des patterns Rust variés
// ---------------------------------------------------------------------------

/// Détermine le type de licence à partir du premier groupe du serial.
/// Le `match` sur une plage de valeurs génère des comparaisons bornées en asm.
fn determine_license(serial: &str) -> Option<LicenseType> {
    let body = serial.strip_prefix(SERIAL_PREFIX)?;
    let first_group = body.split(SEPARATOR).next()?;
    let value = u16::from_str_radix(first_group, 16).ok()?;

    // Pattern matching sur des ranges — génère des cmp/jae/jbe en asm
    let license = match value {
        0x0000..=0x00FF => LicenseType::Trial { days_left: 30 },
        0x0100..=0x0FFF => LicenseType::Standard {
            seats: (value / 256) as u32,
        },
        0x1000..=0xFFFF => LicenseType::Enterprise {
            seats: (value / 512) as u32,
            support: (value & 1) == 1,
        },
    };

    Some(license)
}

/// Affiche une bannière — les littéraux &str sont des fat pointers (ptr, len)
fn print_banner() {
    println!("╔══════════════════════════════════════════╗");
    println!("║      {} — Crackme      ║", VERSION);
    println!("║  Formation Reverse Engineering (Ch.33)   ║");
    println!("╚══════════════════════════════════════════╝");
    println!();
}

/// Affiche l'usage et quitte — le process::exit génère un appel
/// reconnaissable (souvent inliné en syscall exit_group)
fn usage_and_exit() -> ! {
    eprintln!("Usage : crackme_rust <username> <serial>");
    eprintln!("  username : votre nom d'utilisateur (non vide)");
    eprintln!(
        "  serial   : au format {0}XXXX{1}XXXX{1}XXXX{1}XXXX",
        SERIAL_PREFIX, SEPARATOR
    );
    process::exit(1);
}

// ---------------------------------------------------------------------------
// Point d'entrée
// ---------------------------------------------------------------------------
fn main() {
    print_banner();

    // Collecte des arguments — Vec<String> avec fat pointers
    let args: Vec<String> = env::args().collect();

    if args.len() != 3 {
        usage_and_exit();
    }

    let username = &args[1];
    let serial = &args[2];

    // Validation du username — unwrap_or / is_empty patterns
    if username.is_empty() {
        eprintln!("Erreur : le username ne peut pas être vide.");
        process::exit(1);
    }

    println!("[*] Username : {}", username);
    println!("[*] Serial   : {}", serial);
    println!();

    // Construction du pipeline de validation avec dispatch dynamique
    let mut pipeline = ValidationPipeline::new();
    pipeline.add(Box::new(PrefixValidator));
    pipeline.add(Box::new(FormatValidator));
    pipeline.add(Box::new(ChecksumValidator::new(username)));

    // Exécution — le `match` sur Result<(), ValidationError> est le point
    // central que l'analyste RE doit identifier
    match pipeline.run(serial) {
        Ok(()) => {
            println!("[+] Serial VALIDE !");
            println!();

            // Détermination du type de licence — chaîne d'Option avec ?
            match determine_license(serial) {
                Some(license) => {
                    let features = license.max_features();
                    println!("[+] Type de licence : {:?}", license);
                    println!("[+] Fonctionnalités débloquées : {}", features);
                }
                // Ce None ne devrait jamais arriver si le serial est valide
                // mais le compilateur génère quand même le code de ce bras
                None => {
                    eprintln!("[-] Impossible de déterminer le type de licence.");
                }
            }

            println!();
            println!("Félicitations, vous avez résolu le crackme !");
        }
        Err(e) => {
            eprintln!("[-] Validation échouée : {}", e);
            println!();
            println!("Indice : analysez l'algorithme de checksum dans ChecksumValidator.");
            process::exit(1);
        }
    }
}
