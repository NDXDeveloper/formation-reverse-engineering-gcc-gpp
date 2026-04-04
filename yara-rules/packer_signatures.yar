/*
 * packer_signatures.yar — Détection de packers, protections et anomalies ELF
 * Formation Reverse Engineering — Applications compilées avec la chaîne GNU
 *
 * Ce fichier contient des règles YARA pour détecter :
 *   - Les binaires packés (UPX, et patterns génériques)
 *   - Les binaires strippés (absence de .symtab)
 *   - Les indicateurs d'obfuscation de flux de contrôle
 *   - Les techniques anti-debugging courantes
 *   - Les anomalies structurelles ELF suspectes
 *   - Les marqueurs spécifiques aux binaires d'entraînement (ch25, ch29)
 *
 * Usage :
 *   yara -r packer_signatures.yar binaries/
 *   yara -s -m packer_signatures.yar binaries/ch29-packed/
 *
 * Licence MIT — Usage strictement éducatif.
 */

import "elf"

/* ============================================================
 *  UPX — Ultimate Packer for eXecutables
 * ============================================================ */

rule UPX_Packed_ELF
{
    meta:
        description = "ELF binary packed with UPX"
        category    = "packer"
        packer      = "UPX"
        chapter     = "29"
        reference   = "https://upx.github.io/"
        unpacking   = "upx -d <binary>"

    strings:
        // Signature ASCII "UPX!" présente dans le stub de décompression
        $upx_magic = "UPX!"

        // Noms de sections UPX (remplacent les sections originales)
        $sect_upx0 = "UPX0"
        $sect_upx1 = "UPX1"
        $sect_upx2 = "UPX2"

        // Chaîne de version UPX (ex: "UPX 4.2.1")
        $upx_version = /UPX\x20[0-9]+\.[0-9]+/

        // En-tête UPX : magic suivi de la version et des infos de compression
        // Les 4 premiers octets du header UPX interne
        $upx_hdr = { 55 50 58 21 }     // "UPX!" en hex

        // Informations de copyright intégrées au stub
        $upx_copyright = "the UPX Team"

    condition:
        // ELF valide
        uint32(0) == 0x464C457F and     // \x7FELF
        // Au moins le magic + un nom de section UPX
        $upx_magic and
        (1 of ($sect_upx0, $sect_upx1, $sect_upx2))
}

rule UPX_Packed_Stripped_ELF
{
    meta:
        description = "ELF binary packed with UPX AND stripped (double protection)"
        category    = "packer"
        packer      = "UPX"
        chapter     = "29"
        difficulty  = "high"

    strings:
        $upx_magic = "UPX!"
        $sect_upx0 = "UPX0"
        $sect_upx1 = "UPX1"

    condition:
        uint32(0) == 0x464C457F and
        $upx_magic and
        ($sect_upx0 or $sect_upx1) and
        // Pas de section .symtab (indicateur de stripping)
        not for any i in (0..elf.number_of_sections - 1) : (
            elf.sections[i].name == ".symtab"
        )
}

/* ============================================================
 *  Détection générique de packing (heuristiques)
 * ============================================================ */

rule ELF_High_Entropy_Text
{
    meta:
        description = "ELF with suspicious .text section (high entropy suggests packing or encryption)"
        category    = "packer"
        note        = "Heuristique : une section .text compressée ou chiffrée a une entropie > 6.8"

    strings:
        // Séquences longues d'octets à haute entropie sont rares dans du code
        // légitime non optimisé. On cherche l'absence de patterns classiques
        // combinée à la présence de marqueurs ELF.

        // Prologue GCC standard (push rbp; mov rbp, rsp)
        $gcc_prologue = { 55 48 89 E5 }

        // nop sled (padding entre fonctions)
        $nop_sled = { 90 90 90 90 90 90 90 90 }

        // endbr64 (CET, présent dans les binaires GCC récents)
        $endbr64 = { F3 0F 1E FA }

    condition:
        uint32(0) == 0x464C457F and
        filesize < 10MB and
        // Un binaire GCC normal contient des prologues et du padding
        // Un binaire packé n'en contient pas (le code original est compressé)
        not $gcc_prologue and
        not $nop_sled and
        not $endbr64
}

rule ELF_Single_Load_Segment
{
    meta:
        description = "ELF with only one LOAD segment (typical of custom packers)"
        category    = "packer"
        note        = "Un ELF GCC normal a au moins 2 segments LOAD (RX + RW)"

    condition:
        uint32(0) == 0x464C457F and
        elf.number_of_segments > 0 and
        // Compter les segments LOAD
        for 1 i in (0..elf.number_of_segments - 1) : (
            elf.segments[i].type == elf.PT_LOAD
        ) and
        not for 2 i in (0..elf.number_of_segments - 1) : (
            elf.segments[i].type == elf.PT_LOAD
        )
}

rule ELF_Writable_Text
{
    meta:
        description = "ELF with writable .text section (self-modifying code or packer stub)"
        category    = "packer"
        note        = "Un .text légitime est R-X, jamais RWX"

    condition:
        uint32(0) == 0x464C457F and
        for any i in (0..elf.number_of_sections - 1) : (
            elf.sections[i].name == ".text" and
            // SHF_WRITE (0x1) activé
            elf.sections[i].flags & 0x1 != 0
        )
}

/* ============================================================
 *  Binaires strippés
 * ============================================================ */

rule ELF_Stripped
{
    meta:
        description = "ELF binary without .symtab (stripped of debug symbols)"
        category    = "protection"
        chapter     = "19"
        note        = "Stripping supprime .symtab et .strtab mais conserve .dynsym"

    condition:
        uint32(0) == 0x464C457F and
        // Aucune section nommée .symtab
        not for any i in (0..elf.number_of_sections - 1) : (
            elf.sections[i].name == ".symtab"
        )
}

rule ELF_Stripped_No_Debug
{
    meta:
        description = "ELF binary stripped AND without DWARF debug info"
        category    = "protection"
        chapter     = "19"

    strings:
        $debug_info = ".debug_info"
        $debug_abbrev = ".debug_abbrev"
        $debug_line = ".debug_line"

    condition:
        uint32(0) == 0x464C457F and
        not for any i in (0..elf.number_of_sections - 1) : (
            elf.sections[i].name == ".symtab"
        ) and
        not $debug_info and
        not $debug_abbrev and
        not $debug_line
}

/* ============================================================
 *  Techniques anti-debugging
 * ============================================================ */

rule Anti_Debug_Ptrace_Self
{
    meta:
        description = "Binary may use ptrace(PTRACE_TRACEME) as anti-debugging technique"
        category    = "anti_debug"
        chapter     = "19"
        technique   = "ptrace self-attach"
        reference   = "Section 19.7 — Techniques de détection de débogueur"

    strings:
        // ptrace est le syscall n°101 sur x86-64
        // PTRACE_TRACEME = 0
        //
        // Pattern 1 : appel via libc (call ptrace@plt)
        // On cherche la chaîne "ptrace" dans .dynstr (import dynamique)
        $ptrace_import = "ptrace"

        // Pattern 2 : invocation directe du syscall
        // mov rax, 101 (0x65) ; ... ; syscall
        $syscall_ptrace_a = { 48 C7 C0 65 00 00 00 }   // mov rax, 0x65
        $syscall_ptrace_b = { B8 65 00 00 00 }          // mov eax, 0x65

        // PTRACE_TRACEME = 0 dans rdi (premier argument)
        $traceme_rdi = { 48 31 FF }     // xor rdi, rdi (= 0)
        $traceme_rdi2 = { BF 00 00 00 00 }  // mov edi, 0

    condition:
        uint32(0) == 0x464C457F and
        (
            // Import de ptrace (utilisation via libc)
            $ptrace_import or
            // Syscall direct avec PTRACE_TRACEME
            (1 of ($syscall_ptrace_a, $syscall_ptrace_b) and
             1 of ($traceme_rdi, $traceme_rdi2))
        )
}

rule Anti_Debug_Proc_Status
{
    meta:
        description = "Binary reads /proc/self/status (TracerPid detection)"
        category    = "anti_debug"
        chapter     = "19"
        technique   = "TracerPid check via procfs"

    strings:
        $proc_status  = "/proc/self/status"
        $tracer_pid   = "TracerPid"
        $proc_self_fd = "/proc/self/fd"

    condition:
        uint32(0) == 0x464C457F and
        ($proc_status or $tracer_pid)
}

rule Anti_Debug_Timing_Check
{
    meta:
        description = "Binary may use timing-based anti-debugging (rdtsc or clock_gettime)"
        category    = "anti_debug"
        chapter     = "19"
        technique   = "Timing check"

    strings:
        // RDTSC instruction (0F 31) — lit le timestamp counter
        $rdtsc = { 0F 31 }

        // RDTSCP instruction (0F 01 F9) — variante sérialisante
        $rdtscp = { 0F 01 F9 }

        // Import de clock_gettime (alternative libc)
        $clock_gettime = "clock_gettime"

    condition:
        uint32(0) == 0x464C457F and
        (
            // Au moins 2 occurrences de RDTSC (avant et après le code mesuré)
            (#rdtsc >= 2) or
            (#rdtscp >= 2) or
            // clock_gettime importé (peut être légitime, mais à investiguer)
            $clock_gettime
        )
}

rule Anti_Debug_Int3_Scanning
{
    meta:
        description = "Binary may scan for software breakpoints (INT3 = 0xCC)"
        category    = "anti_debug"
        chapter     = "19"
        technique   = "Breakpoint detection via memory scanning"
        reference   = "Section 19.8 — Contre-mesures aux breakpoints"

    strings:
        // Comparaison avec 0xCC (INT3) — souvent via : cmp byte [reg], 0xCC
        $cmp_cc_a = { 80 38 CC }    // cmp byte [rax], 0xCC
        $cmp_cc_b = { 80 39 CC }    // cmp byte [rcx], 0xCC
        $cmp_cc_c = { 80 3B CC }    // cmp byte [rbx], 0xCC
        $cmp_cc_d = { 3C CC }       // cmp al, 0xCC

        // Recherche de 0xCC dans un buffer (movzx + cmp pattern)
        $scan_loop = { 0F B6 ?? 3D CC 00 00 00 }  // movzx + cmp eax, 0xCC

    condition:
        uint32(0) == 0x464C457F and
        2 of ($cmp_cc_a, $cmp_cc_b, $cmp_cc_c, $cmp_cc_d, $scan_loop)
}

/* ============================================================
 *  Obfuscation de flux de contrôle
 * ============================================================ */

rule OLLVM_Control_Flow_Flattening
{
    meta:
        description = "Potential OLLVM/Hikari control flow flattening (dispatcher pattern)"
        category    = "obfuscation"
        chapter     = "19"
        technique   = "Control Flow Flattening (CFF)"
        note        = "Heuristique : grand switch dans une boucle = dispatcher CFF"

    strings:
        // Pattern CFF typique : un grand nombre de cas dans un switch
        // se manifeste par de nombreuses comparaisons cmp + je consécutives
        //   cmp eax, IMM32 ; je TARGET
        // répété de nombreuses fois

        // cmp eax, imm32 ; je rel32
        $cmp_je = { 3D ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? }

        // Variante : cmp edx, imm32 ; je rel32
        $cmp_je_edx = { 81 FA ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? }

    condition:
        uint32(0) == 0x464C457F and
        // Un dispatcher CFF produit des dizaines de ces patterns consécutifs
        (#cmp_je > 20 or #cmp_je_edx > 20)
}

rule Bogus_Control_Flow
{
    meta:
        description = "Potential bogus control flow (opaque predicates)"
        category    = "obfuscation"
        chapter     = "19"
        technique   = "Bogus Control Flow / Opaque Predicates"

    strings:
        // Opaque predicates classiques OLLVM :
        // x * (x - 1) est toujours pair => test avec AND 1
        // Se manifeste par des séquences : imul + dec + and + test + jz/jnz
        // sur des valeurs globales lues depuis .bss/.data

        // Accès répétés à une même adresse globale suivi de calcul + saut
        // Pattern simplifié : mov reg, [rip+disp] ; ... ; test ; jz
        $opaque_load = { 8B 05 ?? ?? ?? ?? }    // mov eax, [rip+disp32]

        // Comparaison constante qui est toujours vraie/fausse
        $always_true  = { 83 F8 00 74 }         // cmp eax, 0 ; jz (après un calcul opaque)
        $always_false = { 83 F8 01 75 }          // cmp eax, 1 ; jnz

    condition:
        uint32(0) == 0x464C457F and
        #opaque_load > 30 and
        (1 of ($always_true, $always_false))
}

/* ============================================================
 *  Marqueurs spécifiques : formats et binaires d'entraînement
 * ============================================================ */

rule CFR_Format_Handler
{
    meta:
        description = "Binary that reads or writes CFR archives (Chapter 25)"
        category    = "training"
        chapter     = "25"

    strings:
        // Magics du format CFR
        $hdr_magic = "CFRM"
        $ftr_magic = "CRFE"

        // Clé XOR utilisée pour l'obfuscation des données
        $xor_key = { 5A 3C 96 F1 }

        // Noms de commandes dans le code
        $cmd_generate = "generate"
        $cmd_pack     = "pack"
        $cmd_validate = "validate"
        $cmd_unpack   = "unpack"

    condition:
        uint32(0) == 0x464C457F and
        $hdr_magic and $ftr_magic and
        (2 of ($cmd_generate, $cmd_pack, $cmd_validate, $cmd_unpack))
}

rule CFR_Archive_File
{
    meta:
        description = "CFR archive file (Custom Format Records, Chapter 25)"
        category    = "training"
        chapter     = "25"
        filetype    = "data"

    strings:
        $hdr_magic = "CFRM"
        $ftr_magic = "CRFE"

    condition:
        // Header magic au début, version 2
        $hdr_magic at 0 and
        uint16(4) == 0x0002 and
        // Vérifier que le nombre de records est raisonnable (< 1024)
        uint32(8) > 0 and uint32(8) < 1024 and
        // Footer magic quelque part à la fin
        (filesize > 44 and $ftr_magic in (filesize - 12 .. filesize))
}

/* ============================================================
 *  Détection de protections de compilation (GCC hardening)
 * ============================================================ */

rule ELF_Has_Stack_Canary
{
    meta:
        description = "ELF binary compiled with -fstack-protector (imports __stack_chk_fail)"
        category    = "protection"
        chapter     = "19"

    strings:
        $canary_func = "__stack_chk_fail"

    condition:
        uint32(0) == 0x464C457F and
        $canary_func
}

rule ELF_Full_RELRO
{
    meta:
        description = "ELF binary with Full RELRO (GOT is read-only after relocation)"
        category    = "protection"
        chapter     = "19"
        note        = "Checks for PT_GNU_RELRO segment + DT_BIND_NOW tag (x86-64)"

    strings:
        // DT_BIND_NOW : Elf64_Dyn avec d_tag = 24 (0x18), d_val = 0
        // 16 octets : tag (8 LE) + value (8 LE)
        $dt_bind_now_64 = { 18 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 }

    condition:
        uint32(0) == 0x464C457F and
        elf.machine == elf.EM_X86_64 and
        // Segment PT_GNU_RELRO présent
        for any i in (0..elf.number_of_segments - 1) : (
            elf.segments[i].type == elf.PT_GNU_RELRO
        ) and
        // Tag DT_BIND_NOW dans .dynamic
        $dt_bind_now_64
}

rule ELF_PIE_Executable
{
    meta:
        description = "Position-Independent Executable (PIE)"
        category    = "protection"
        chapter     = "19"

    condition:
        // Un PIE a le type ET_DYN (3) mais est un exécutable
        uint32(0) == 0x464C457F and
        elf.type == elf.ET_DYN and
        // Distinguer PIE d'une shared library : présence d'un interpréteur
        for any i in (0..elf.number_of_segments - 1) : (
            elf.segments[i].type == elf.PT_INTERP
        )
}

/* ============================================================
 *  Règle composite : binaire suspect (cumul d'indicateurs)
 * ============================================================ */

rule Suspicious_ELF_Binary
{
    meta:
        description = "ELF binary with multiple suspicious characteristics"
        category    = "triage"
        note        = "Combine plusieurs heuristiques — à investiguer manuellement"

    condition:
        uint32(0) == 0x464C457F and
        (
            // Packé ou obfusqué
            (UPX_Packed_ELF or ELF_Writable_Text or OLLVM_Control_Flow_Flattening)
            or
            // Anti-debug + strippé
            (ELF_Stripped and
             (Anti_Debug_Ptrace_Self or Anti_Debug_Proc_Status or Anti_Debug_Int3_Scanning))
            or
            // Packé + anti-debug
            (UPX_Packed_ELF and Anti_Debug_Ptrace_Self)
        )
}
