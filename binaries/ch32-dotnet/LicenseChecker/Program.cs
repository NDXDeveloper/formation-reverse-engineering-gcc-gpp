using System;

namespace LicenseChecker
{
    /// <summary>
    /// Point d'entrée — Application de vérification de licence.
    /// Cible d'entraînement pour le Chapitre 32 (analyse dynamique .NET).
    ///
    /// Format de clé attendu : XXXX-XXXX-XXXX-XXXX (caractères hexadécimaux)
    /// La validation combine des vérifications C# et des appels P/Invoke
    /// vers libnative_check.so (compilée avec GCC).
    /// </summary>
    class Program
    {
        private const string Banner = @"
    ╔══════════════════════════════════════════╗
    ║   LicenseChecker v3.2.1 — Ch.32 RE Lab  ║
    ║   © 2025 Formation RE GCC/G++           ║
    ╚══════════════════════════════════════════╝";

        static int Main(string[] args)
        {
            Console.WriteLine(Banner);
            Console.WriteLine();

            string username;
            string licenseKey;

            if (args.Length >= 2)
            {
                username   = args[0];
                licenseKey = args[1];
            }
            else
            {
                Console.Write("  Nom d'utilisateur : ");
                username = Console.ReadLine() ?? "";

                Console.Write("  Clé de licence    : ");
                licenseKey = Console.ReadLine() ?? "";
            }

            Console.WriteLine();

            if (string.IsNullOrWhiteSpace(username))
            {
                Console.WriteLine("  [!] Le nom d'utilisateur ne peut pas être vide.");
                return 1;
            }

            var validator = new LicenseValidator();
            var result    = validator.Validate(username, licenseKey);

            if (result.IsValid)
            {
                Console.WriteLine("  ╔═══════════════════════════════════╗");
                Console.WriteLine("  ║  ✅  Licence valide ! Bienvenue. ║");
                Console.WriteLine("  ╚═══════════════════════════════════╝");
                Console.WriteLine();
                Console.WriteLine($"  Utilisateur : {username}");
                Console.WriteLine($"  Niveau      : {result.LicenseLevel}");
                Console.WriteLine($"  Expiration  : {result.ExpirationInfo}");
                return 0;
            }
            else
            {
                Console.WriteLine("  ╔═══════════════════════════════════════╗");
                Console.WriteLine("  ║  ❌  Licence invalide.               ║");
                Console.WriteLine("  ╚═══════════════════════════════════════╝");
                Console.WriteLine();
                Console.WriteLine($"  Raison : {result.FailureReason}");
                return 1;
            }
        }
    }
}
