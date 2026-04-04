using System;
using System.Linq;
using System.Text;

namespace LicenseChecker
{
    /// <summary>
    /// Résultat de la validation de licence.
    /// </summary>
    public class ValidationResult
    {
        public bool   IsValid        { get; set; }
        public string FailureReason  { get; set; } = "";
        public string LicenseLevel   { get; set; } = "Standard";
        public string ExpirationInfo { get; set; } = "Perpétuelle";
    }

    /// <summary>
    /// Moteur de validation de licence.
    ///
    /// Schéma de la clé : AAAA-BBBB-CCCC-DDDD
    ///
    ///   Segment AAAA : dérivé du nom d'utilisateur (hash FNV-1a, C# pur)
    ///   Segment BBBB : validé via P/Invoke (libnative_check.so)
    ///   Segment CCCC : XOR croisé des segments A et B avec rotation + mélange
    ///   Segment DDDD : checksum final combinant partie managée et native
    ///
    /// Points d'intérêt pour le RE :
    ///   - §32.1 : breakpoint sur Validate(), suivre le flux étape par étape
    ///   - §32.2 : hooker ComputeUserHash / ComputeCrossXor avec Frida CLR
    ///   - §32.3 : intercepter les appels P/Invoke vers libnative_check.so
    ///   - §32.4 : patcher les sauts conditionnels dans Validate()
    /// </summary>
    public class LicenseValidator
    {
        // ── Constantes internes (découvrables via strings / décompilation) ──

        private static readonly byte[] MagicSalt =
            { 0x52, 0x45, 0x56, 0x33, 0x52, 0x53, 0x45, 0x21 };
        // ASCII : "REV3RSE!"

        private const uint HashSeed  = 0x811C9DC5;  // FNV-1a offset basis (32-bit)
        private const uint HashPrime = 0x01000193;   // FNV-1a prime

        // ══════════════════════════════════════════════════════════════
        //  Point d'entrée principal
        // ══════════════════════════════════════════════════════════════

        public ValidationResult Validate(string username, string licenseKey)
        {
            var result = new ValidationResult();

            // ── Étape 1 — Vérification du format ──
            if (!ValidateStructure(licenseKey, out string[] segments))
            {
                result.IsValid       = false;
                result.FailureReason = "Format invalide. Attendu : XXXX-XXXX-XXXX-XXXX (hex)";
                return result;
            }

            // ── Étape 2 — Segment A : hash du username (C# pur) ──
            uint expectedA = ComputeUserHash(username);
            uint actualA   = Convert.ToUInt32(segments[0], 16);

            if (actualA != expectedA)
            {
                result.IsValid       = false;
                result.FailureReason = "Segment 1 invalide (lié au nom d'utilisateur).";
                return result;
            }

            // ── Étape 3 — Segment B : vérification native (P/Invoke) ──
            uint actualB   = Convert.ToUInt32(segments[1], 16);
            bool segBValid = CheckSegmentB(username, actualB);

            if (!segBValid)
            {
                result.IsValid       = false;
                result.FailureReason = "Segment 2 invalide (vérification native).";
                return result;
            }

            // ── Étape 4 — Segment C : XOR croisé (C# pur) ──
            uint expectedC = ComputeCrossXor(actualA, actualB);
            uint actualC   = Convert.ToUInt32(segments[2], 16);

            if (actualC != expectedC)
            {
                result.IsValid       = false;
                result.FailureReason = "Segment 3 invalide (contrôle croisé).";
                return result;
            }

            // ── Étape 5 — Segment D : checksum final (natif + managé) ──
            uint expectedD = ComputeFinalChecksum(actualA, actualB, actualC, username);
            uint actualD   = Convert.ToUInt32(segments[3], 16);

            if (actualD != expectedD)
            {
                result.IsValid       = false;
                result.FailureReason = "Segment 4 invalide (checksum final).";
                return result;
            }

            // ── Succès ──
            result.IsValid        = true;
            result.LicenseLevel   = DeriveLicenseLevel(actualA);
            result.ExpirationInfo = "Perpétuelle";
            return result;
        }

        // ══════════════════════════════════════════════════════════════
        //  Méthodes internes
        // ══════════════════════════════════════════════════════════════

        /// <summary>
        /// Vérifie que la clé a le format XXXX-XXXX-XXXX-XXXX (hex).
        /// Cible de patching IL : inverser le retour pour bypasser le format check.
        /// </summary>
        private bool ValidateStructure(string key, out string[] segments)
        {
            segments = Array.Empty<string>();

            if (string.IsNullOrWhiteSpace(key))
                return false;

            string[] parts = key.Trim().ToUpperInvariant().Split('-');

            if (parts.Length != 4)
                return false;

            foreach (string part in parts)
            {
                if (part.Length != 4)
                    return false;

                if (!part.All(c => "0123456789ABCDEF".Contains(c)))
                    return false;
            }

            segments = parts;
            return true;
        }

        /// <summary>
        /// Hash FNV-1a du username salé avec MagicSalt, replié sur 16 bits.
        /// Produit le segment A attendu de la clé.
        /// </summary>
        private uint ComputeUserHash(string username)
        {
            byte[] usernameBytes = Encoding.UTF8.GetBytes(
                username.ToLowerInvariant());

            // Concaténer username + salt
            byte[] salted = new byte[usernameBytes.Length + MagicSalt.Length];
            Array.Copy(usernameBytes, 0, salted, 0, usernameBytes.Length);
            Array.Copy(MagicSalt, 0, salted, usernameBytes.Length, MagicSalt.Length);

            // FNV-1a 32-bit
            uint hash = HashSeed;
            for (int i = 0; i < salted.Length; i++)
            {
                hash ^= salted[i];
                hash *= HashPrime;
            }

            // Fold-XOR 32→16 bits
            uint folded = (hash >> 16) ^ (hash & 0xFFFF);
            return folded & 0xFFFF;
        }

        /// <summary>
        /// Valide le segment B via la bibliothèque native (P/Invoke).
        /// Le hash est calculé côté C avec un salt différent ("NATIVERE").
        /// </summary>
        private bool CheckSegmentB(string username, uint segmentB)
        {
            try
            {
                byte[] data = Encoding.UTF8.GetBytes(
                    username.ToLowerInvariant());
                uint expected = NativeBridge.ComputeNativeHash(data, data.Length);
                expected = expected & 0xFFFF;
                return segmentB == expected;
            }
            catch (DllNotFoundException)
            {
                Console.WriteLine(
                    "  [!] libnative_check.so introuvable. "
                  + "Vérifiez LD_LIBRARY_PATH.");
                return false;
            }
            catch (EntryPointNotFoundException ex)
            {
                Console.WriteLine(
                    $"  [!] Fonction native introuvable : {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Calcule le segment C par XOR croisé avec rotation et mélange.
        /// Algorithme :
        ///   1. Rotation à gauche de segA (5 bits, sur 16 bits)
        ///   2. XOR avec segB
        ///   3. Multiplication par 0x9E37, masquée sur 16 bits
        ///   4. XOR final avec 0xA5A5
        /// </summary>
        private uint ComputeCrossXor(uint segA, uint segB)
        {
            // Rotation à gauche de 5 bits sur 16 bits
            uint rotA   = ((segA << 5) | (segA >> 11)) & 0xFFFF;
            uint result = rotA ^ segB;

            // Mélange multiplicatif
            result = (result * 0x9E37) & 0xFFFF;
            result ^= 0xA5A5;

            return result & 0xFFFF;
        }

        /// <summary>
        /// Calcule le segment D : checksum final combinant partie managée
        /// (somme des 3 segments) et partie native (compute_checksum via P/Invoke).
        /// Le résultat est le XOR des deux parties, sur 16 bits.
        /// </summary>
        private uint ComputeFinalChecksum(
            uint segA, uint segB, uint segC, string username)
        {
            // Partie managée : somme des trois segments
            uint managed = (segA + segB + segC) & 0xFFFF;

            // Partie native : checksum via P/Invoke
            try
            {
                uint nativePart = NativeBridge.ComputeChecksum(segA, segB, segC);
                nativePart = nativePart & 0xFFFF;

                // Combinaison finale
                return (managed ^ nativePart) & 0xFFFF;
            }
            catch
            {
                return 0xDEAD;
            }
        }

        /// <summary>
        /// Détermine le niveau de licence (cosmétique) à partir du segment A.
        /// </summary>
        private string DeriveLicenseLevel(uint segA)
        {
            return (segA % 3) switch
            {
                0 => "Professional",
                1 => "Enterprise",
                _ => "Standard"
            };
        }
    }
} 
