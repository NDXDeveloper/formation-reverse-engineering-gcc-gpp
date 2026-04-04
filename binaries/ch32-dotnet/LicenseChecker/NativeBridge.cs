using System;
using System.Runtime.InteropServices;

namespace LicenseChecker
{
    /// <summary>
    /// Pont P/Invoke vers libnative_check.so (compilée avec GCC).
    ///
    /// Points d'intérêt RE :
    ///   - §32.3 : ces appels traversent la frontière managé → natif.
    ///     Interceptables avec Frida côté natif (Interceptor.attach)
    ///     ou en hookant les wrappers C# côté CLR (frida-clr).
    ///   - La bibliothèque est chargée paresseusement au premier appel.
    ///   - LD_LIBRARY_PATH doit pointer vers le répertoire de la .so.
    /// </summary>
    internal static class NativeBridge
    {
        private const string LibName = "libnative_check.so";

        /// <summary>
        /// Calcule un hash FNV-1a salé sur un buffer (username UTF-8).
        /// Utilisé pour valider le segment B.
        /// </summary>
        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl,
                   EntryPoint = "compute_native_hash")]
        public static extern uint ComputeNativeHash(byte[] data, int length);

        /// <summary>
        /// Calcule un checksum combinatoire à partir des segments A, B, C.
        /// Utilisé pour la partie native du segment D.
        /// </summary>
        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl,
                   EntryPoint = "compute_checksum")]
        public static extern uint ComputeChecksum(uint segA, uint segB, uint segC);

        /// <summary>
        /// Vérification d'intégrité complète côté natif.
        /// Non utilisée dans le flux principal — présente comme cible
        /// d'exercice pour le hooking Frida (§32.2 / §32.3).
        /// </summary>
        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl,
                   EntryPoint = "verify_integrity")]
        public static extern int VerifyIntegrity(
            [MarshalAs(UnmanagedType.LPStr)] string username,
            uint segA, uint segB, uint segC, uint segD);
    }
}
