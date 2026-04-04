/*
 * anti_reverse.c — Binaire d'entraînement Chapitre 19
 * Formation Reverse Engineering — Chaîne GNU
 *
 * Ce programme implémente un crackme protégé par plusieurs couches
 * de techniques anti-reverse engineering :
 *
 *   1. Détection de débogueur via ptrace (section 19.7)
 *   2. Détection de débogueur via /proc/self/status (section 19.7)
 *   3. Timing check pour détecter le single-stepping (section 19.7)
 *   4. Scanning d'instructions int3 (0xCC) dans son propre code (section 19.8)
 *   5. Vérification d'intégrité (checksum) sur la section .text (section 19.8)
 *   6. Compilable avec diverses protections : canary, PIE, RELRO (sections 19.5–19.6)
 *   7. Strippable et packable via le Makefile (sections 19.1–19.2)
 *
 * Le secret : le mot de passe est dérivé d'un XOR sur une chaîne encodée.
 *
 * Compilation : voir le Makefile associé.
 *
 * ⚠️ Usage strictement éducatif — Licence MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <signal.h>

/* ═══════════════════════════════════════════════════════
 * CONFIGURATION — activer/désactiver chaque protection
 * via des -D au moment de la compilation (voir Makefile)
 * ═══════════════════════════════════════════════════════ */

/* Valeurs par défaut si non définies par le Makefile */
#ifndef ENABLE_PTRACE_CHECK
#define ENABLE_PTRACE_CHECK 1
#endif

#ifndef ENABLE_PROCFS_CHECK
#define ENABLE_PROCFS_CHECK 1
#endif

#ifndef ENABLE_TIMING_CHECK
#define ENABLE_TIMING_CHECK 1
#endif

#ifndef ENABLE_INT3_SCAN
#define ENABLE_INT3_SCAN 1
#endif

#ifndef ENABLE_CHECKSUM
#define ENABLE_CHECKSUM 1
#endif

/* ═══════════════════════════════════════════
 * Données encodées — le mot de passe "caché"
 * ═══════════════════════════════════════════ */

/*
 * Le mot de passe en clair est : "R3vers3!"
 * Il est stocké XORé avec la clé 0x5A pour ne pas
 * apparaître directement dans `strings`.
 */
static const uint8_t encoded_pass[] = {
    0x08, /* 'R' ^ 0x5A */
    0x69, /* '3' ^ 0x5A */
    0x2C, /* 'v' ^ 0x5A */
    0x3F, /* 'e' ^ 0x5A */
    0x28, /* 'r' ^ 0x5A */
    0x29, /* 's' ^ 0x5A */
    0x69, /* '3' ^ 0x5A */
    0x73, /* '!' ^ 0x5A */
};

#define PASS_LEN (sizeof(encoded_pass))
#define XOR_KEY  0x5A

/* ═══════════════════════════════════════════
 * Messages d'erreur volontairement vagues
 * ═══════════════════════════════════════════ */
static const char *msg_env_error  = "Erreur : environnement non conforme.\n";
static const char *msg_integrity  = "Erreur : intégrité compromise.\n";

/* ═══════════════════════════════════════════
 * PROTECTION 1 — Détection ptrace
 * Section 19.7
 *
 * Principe : un processus ne peut être ptracé que par
 * un seul parent. Si PTRACE_TRACEME échoue, c'est qu'un
 * débogueur est déjà attaché.
 * ═══════════════════════════════════════════ */
static int check_ptrace(void)
{
#if ENABLE_PTRACE_CHECK
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
        return 1; /* débogueur détecté */
    }
    /* Détacher immédiatement pour ne pas bloquer les
     * éventuels fork() ultérieurs */
    ptrace(PTRACE_DETACH, 0, NULL, NULL);
#endif
    return 0;
}

/* ═══════════════════════════════════════════
 * PROTECTION 2 — Lecture de /proc/self/status
 * Section 19.7
 *
 * Principe : le champ TracerPid dans /proc/self/status
 * indique le PID du processus qui nous trace. S'il est
 * non nul, un débogueur est attaché.
 * ═══════════════════════════════════════════ */
static int check_procfs(void)
{
#if ENABLE_PROCFS_CHECK
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp)
        return 0; /* pas de procfs = on laisse passer */

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            long pid = strtol(line + 10, NULL, 10);
            fclose(fp);
            return (pid != 0) ? 1 : 0;
        }
    }
    fclose(fp);
#endif
    return 0;
}

/* ═══════════════════════════════════════════
 * PROTECTION 3 — Timing check
 * Section 19.7
 *
 * Principe : mesurer le temps d'exécution d'un bloc
 * trivial. Sous débogueur (single-step), ce temps
 * explose. Seuil : 50 ms pour une opération instantanée.
 * ═══════════════════════════════════════════ */
static int check_timing(void)
{
#if ENABLE_TIMING_CHECK
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* Bloc trivial — devrait prendre < 1 ms */
    volatile int dummy = 0;
    for (int i = 0; i < 1000; i++) {
        dummy += i;
    }

    clock_gettime(CLOCK_MONOTONIC, &t2);

    long elapsed_ms = (t2.tv_sec - t1.tv_sec) * 1000 +
                      (t2.tv_nsec - t1.tv_nsec) / 1000000;

    if (elapsed_ms > 50) {
        return 1; /* exécution anormalement lente */
    }
#endif
    return 0;
}

/* ═══════════════════════════════════════════
 * PROTECTION 4 — Scanning int3 (0xCC)
 * Section 19.8
 *
 * Principe : quand GDB pose un software breakpoint,
 * il écrit l'opcode 0xCC (int3) au début de l'instruction
 * ciblée. On scanne notre propre code pour détecter
 * ces octets.
 *
 * Note : on scanne la fonction verify_password car c'est
 * la cible la plus probable d'un breakpoint.
 * ═══════════════════════════════════════════ */

/* Forward declaration — définie plus bas */
static int verify_password(const char *input);

static int scan_int3(void)
{
#if ENABLE_INT3_SCAN
    const uint8_t *fn_ptr = (const uint8_t *)verify_password;

    /*
     * Scanner les 128 premiers octets de verify_password.
     * On cherche 0xCC qui ne fait pas partie du code légitime.
     * Attention : 0xCC peut apparaître légitimement dans des
     * opérandes, mais rarement en début d'instruction dans
     * du code GCC typique.
     */
    for (int i = 0; i < 128; i++) {
        if (fn_ptr[i] == 0xCC) {
            return 1; /* breakpoint détecté */
        }
    }
#endif
    return 0;
}

/* ═══════════════════════════════════════════
 * PROTECTION 5 — Checksum de code
 * Section 19.8
 *
 * Principe : calculer un hash simple sur les octets
 * de verify_password. Si le code a été patché (par ex.
 * un NOP sur un jz/jnz), le checksum change.
 *
 * Le checksum attendu est calculé à la première exécution
 * non-déboguée et hardcodé. En pratique, le Makefile
 * pourrait injecter la bonne valeur ; ici on utilise
 * un calcul dynamique simplifié pour la démo.
 * ═══════════════════════════════════════════ */

/* Taille du bloc à vérifier */
#define CHECKSUM_LEN 64

static uint32_t compute_checksum(const uint8_t *ptr, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (sum << 3) | (sum >> 29); /* rotation */
        sum ^= ptr[i];
    }
    return sum;
}

/*
 * Le checksum attendu est stocké ici.
 * Valeur 0 = désactivé (première compilation).
 * Le script post-build du Makefile peut patcher cette valeur.
 */
static volatile uint32_t expected_checksum = 0;

static int check_code_integrity(void)
{
#if ENABLE_CHECKSUM
    if (expected_checksum == 0)
        return 0; /* checksum non initialisé, on skip */

    uint32_t actual = compute_checksum(
        (const uint8_t *)verify_password, CHECKSUM_LEN);

    if (actual != expected_checksum) {
        return 1; /* code modifié */
    }
#endif
    return 0;
}

/* ═══════════════════════════════════════════
 * Routine de vérification du mot de passe
 *
 * C'est la cible principale du reverse engineer.
 * Le mot de passe est décodé en mémoire par XOR,
 * comparé caractère par caractère (pas de strcmp
 * pour éviter un hook trivial).
 * ═══════════════════════════════════════════ */
static int verify_password(const char *input)
{
    if (strlen(input) != PASS_LEN)
        return 0;

    /* Décodage du mot de passe en mémoire */
    char decoded[PASS_LEN + 1];
    for (size_t i = 0; i < PASS_LEN; i++) {
        decoded[i] = (char)(encoded_pass[i] ^ XOR_KEY);
    }
    decoded[PASS_LEN] = '\0';

    /* Comparaison caractère par caractère
     * (évite de pouvoir hook strcmp/memcmp) */
    int result = 1;
    for (size_t i = 0; i < PASS_LEN; i++) {
        if (input[i] != decoded[i]) {
            result = 0;
            /* On ne sort PAS immédiatement pour éviter
             * un timing side-channel (toujours parcourir
             * toute la chaîne) */
        }
    }

    /* Nettoyage du mot de passe décodé en mémoire */
    explicit_bzero(decoded, sizeof(decoded));

    return result;
}

/* ═══════════════════════════════════════════
 * Handler SIGTRAP — protection bonus
 * Section 19.8
 *
 * Si quelqu'un envoie un SIGTRAP (ou si int3 est
 * exécuté hors débogueur), on le catch au lieu de
 * crasher, ce qui perturbe le débogueur.
 * ═══════════════════════════════════════════ */
static volatile int trap_detected = 0;

static void sigtrap_handler(int sig)
{
    (void)sig;
    trap_detected = 1;
}

/* ═══════════════════════════════════════════
 * Point d'entrée
 * ═══════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    /* Installation du handler SIGTRAP */
    signal(SIGTRAP, sigtrap_handler);

    /* ── Couche 1 : détection ptrace ── */
    if (check_ptrace()) {
        fprintf(stderr, "%s", msg_env_error);
        return 1;
    }

    /* ── Couche 2 : détection /proc/self/status ── */
    if (check_procfs()) {
        fprintf(stderr, "%s", msg_env_error);
        return 1;
    }

    /* ── Couche 3 : timing check ── */
    if (check_timing()) {
        fprintf(stderr, "%s", msg_env_error);
        return 1;
    }

    /* ── Couche 4 : scan int3 ── */
    if (scan_int3()) {
        fprintf(stderr, "%s", msg_integrity);
        return 1;
    }

    /* ── Couche 5 : intégrité du code ── */
    if (check_code_integrity()) {
        fprintf(stderr, "%s", msg_integrity);
        return 1;
    }

    /* ── Logique principale ── */
    printf("=== Crackme Chapitre 19 ===\n");
    printf("Mot de passe : ");
    fflush(stdout);

    char input[256];
    if (!fgets(input, sizeof(input), stdin)) {
        return 1;
    }

    /* Retirer le newline */
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }

    if (verify_password(input)) {
        printf(">>> Accès autorisé. Bravo !\n");
        printf(">>> Flag : CTF{ant1_r3v3rs3_byp4ss3d}\n");
        return 0;
    } else {
        printf(">>> Mot de passe incorrect.\n");
        return 1;
    }
} 
