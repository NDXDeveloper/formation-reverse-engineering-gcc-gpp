/**
 * solutions/ch13-checkpoint-solution.js
 * 
 * Agent Frida — Checkpoint Chapitre 13
 * Logue tous les appels à send() avec leurs buffers,
 * et intercepte connect() pour contextualiser les connexions.
 *
 * Usage CLI :
 *   frida -f ./client_O0 -l ch13-checkpoint-solution.js --no-pause
 *
 * Usage Python :
 *   Voir ch13-checkpoint-solution.py (client d'orchestration)
 */
'use strict';

// ═══════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════

const CONFIG = {
    // Filtrage par file descriptor (-1 = désactivé, tout capturer)
    filterFd: -1,

    // Taille maximale du hexdump affiché en console
    hexdumpMaxBytes: 128,

    // Hooker aussi write() sur les fd de sockets connus
    hookWrite: true,
};

// ═══════════════════════════════════════════════════════════
// ÉTAT GLOBAL
// ═══════════════════════════════════════════════════════════

const startTime = Date.now();
let sequenceNumber = 0;

// Table des connexions connues : fd → { ip, port }
const connections = new Map();

// Table des fd de sockets connus (pour filtrer write() sur sockets)
const knownSocketFds = new Set();

// ═══════════════════════════════════════════════════════════
// UTILITAIRES
// ═══════════════════════════════════════════════════════════

/**
 * Retourne le nombre de millisecondes écoulées depuis le
 * début du traçage.
 */
function elapsed() {
    return Date.now() - startTime;
}

/**
 * Parse une structure sockaddr_in pointée par `addrPtr`.
 * Retourne { family, ip, port } ou null si la famille
 * n'est pas AF_INET (2).
 */
function parseSockaddrIn(addrPtr) {
    try {
        const family = addrPtr.readU16();
        if (family !== 2) {  // AF_INET
            return { family, ip: null, port: null };
        }

        // sin_port : 2 octets en network byte order (big-endian)
        const portHi = addrPtr.add(2).readU8();
        const portLo = addrPtr.add(3).readU8();
        const port = (portHi << 8) | portLo;

        // sin_addr : 4 octets
        const ip = [
            addrPtr.add(4).readU8(),
            addrPtr.add(5).readU8(),
            addrPtr.add(6).readU8(),
            addrPtr.add(7).readU8()
        ].join('.');

        return { family, ip, port };
    } catch (e) {
        return null;
    }
}

/**
 * Vérifie si un fd doit être loggué selon la configuration
 * de filtrage.
 */
function shouldLog(fd) {
    if (CONFIG.filterFd === -1) return true;
    return fd === CONFIG.filterFd;
}

// ═══════════════════════════════════════════════════════════
// HOOK : connect()
// ═══════════════════════════════════════════════════════════

Interceptor.attach(Module.findExportByName(null, "connect"), {
    onEnter(args) {
        try {
            this.fd = args[0].toInt32();
            this.addr = parseSockaddrIn(args[1]);
        } catch (e) {
            this.addr = null;
        }
    },
    onLeave(retval) {
        try {
            const result = retval.toInt32();

            if (this.addr && this.addr.ip) {
                // Enregistrer la connexion
                connections.set(this.fd, {
                    ip: this.addr.ip,
                    port: this.addr.port
                });
                knownSocketFds.add(this.fd);

                const status = result === 0 ? 'OK' : `ERREUR (${result})`;
                const msg = `[CONNECT] fd ${this.fd} → ${this.addr.ip}:${this.addr.port} — ${status}`;
                console.log(`\n${msg}`);

                send({
                    event: 'connect',
                    timestamp_ms: elapsed(),
                    fd: this.fd,
                    ip: this.addr.ip,
                    port: this.addr.port,
                    result: result
                });
            } else if (this.addr) {
                // Famille non AF_INET (AF_UNIX, AF_INET6...)
                console.log(`[CONNECT] fd ${this.fd} — famille ${this.addr.family} (non IPv4)`);
            }
        } catch (e) {
            console.log(`[!] Erreur dans connect/onLeave : ${e.message}`);
        }
    }
});

// ═══════════════════════════════════════════════════════════
// HOOK : send()
// ═══════════════════════════════════════════════════════════

Interceptor.attach(Module.findExportByName(null, "send"), {
    onEnter(args) {
        try {
            this.fd = args[0].toInt32();
            this.buf = args[1];
            this.len = args[2].toInt32();
            this.flags = args[3].toInt32();
            this.shouldLog = shouldLog(this.fd);
        } catch (e) {
            this.shouldLog = false;
            console.log(`[!] Erreur dans send/onEnter : ${e.message}`);
        }
    },
    onLeave(retval) {
        if (!this.shouldLog) return;

        try {
            const bytesSent = retval.toInt32();

            // Incrémenter le numéro de séquence
            sequenceNumber++;
            const seq = sequenceNumber;
            const ts = elapsed();

            // Préparer les métadonnées
            const conn = connections.get(this.fd);
            const meta = {
                event: 'send',
                seq: seq,
                timestamp_ms: ts,
                fd: this.fd,
                requested_len: this.len,
                bytes_sent: bytesSent,
                flags: this.flags,
                dest_ip: conn ? conn.ip : null,
                dest_port: conn ? conn.port : null,
            };

            // Si send() a échoué, logger l'erreur mais ne pas lire le buffer
            if (bytesSent < 0) {
                console.log(`\n[SEND #${seq}] fd ${this.fd} — ERREUR (retour ${bytesSent})`);
                send(meta);
                return;
            }

            // Lire le buffer : au maximum len octets, au maximum bytesSent
            const readSize = Math.min(this.len, bytesSent);
            const bufferData = this.buf.readByteArray(readSize);

            // Affichage console
            const destStr = conn ? ` → ${conn.ip}:${conn.port}` : '';
            console.log(`\n[SEND #${seq}] +${ts}ms | fd ${this.fd}${destStr} | ${bytesSent}/${this.len} octets`);
            console.log(hexdump(this.buf, {
                length: Math.min(readSize, CONFIG.hexdumpMaxBytes)
            }));

            // Envoi vers Python : JSON + buffer binaire brut
            send(meta, bufferData);

        } catch (e) {
            console.log(`[!] Erreur dans send/onLeave : ${e.message}`);
        }
    }
});

// ═══════════════════════════════════════════════════════════
// HOOK OPTIONNEL : write() (pour les sockets)
// ═══════════════════════════════════════════════════════════

if (CONFIG.hookWrite) {
    Interceptor.attach(Module.findExportByName(null, "write"), {
        onEnter(args) {
            try {
                this.fd = args[0].toInt32();
                this.buf = args[1];
                this.len = args[2].toInt32();
                // Ne logger que les write() sur des fd de sockets connus
                this.shouldLog = knownSocketFds.has(this.fd) && shouldLog(this.fd);
            } catch (e) {
                this.shouldLog = false;
            }
        },
        onLeave(retval) {
            if (!this.shouldLog) return;

            try {
                const bytesWritten = retval.toInt32();
                if (bytesWritten < 0) return;

                sequenceNumber++;
                const seq = sequenceNumber;
                const ts = elapsed();
                const readSize = Math.min(this.len, bytesWritten);
                const bufferData = this.buf.readByteArray(readSize);

                const conn = connections.get(this.fd);
                const destStr = conn ? ` → ${conn.ip}:${conn.port}` : '';

                console.log(`\n[WRITE #${seq}] +${ts}ms | fd ${this.fd}${destStr} | ${bytesWritten}/${this.len} octets`);
                console.log(hexdump(this.buf, {
                    length: Math.min(readSize, CONFIG.hexdumpMaxBytes)
                }));

                send({
                    event: 'write',
                    seq: seq,
                    timestamp_ms: ts,
                    fd: this.fd,
                    requested_len: this.len,
                    bytes_sent: bytesWritten,
                    flags: 0,
                    dest_ip: conn ? conn.ip : null,
                    dest_port: conn ? conn.port : null,
                }, bufferData);

            } catch (e) {
                console.log(`[!] Erreur dans write/onLeave : ${e.message}`);
            }
        }
    });
}

// ═══════════════════════════════════════════════════════════
// HOOK : close() — nettoyer les tables de suivi
// ═══════════════════════════════════════════════════════════

Interceptor.attach(Module.findExportByName(null, "close"), {
    onEnter(args) {
        try {
            const fd = args[0].toInt32();
            if (connections.has(fd)) {
                const conn = connections.get(fd);
                console.log(`\n[CLOSE] fd ${fd} (${conn.ip}:${conn.port})`);
                send({
                    event: 'close',
                    timestamp_ms: elapsed(),
                    fd: fd,
                    ip: conn.ip,
                    port: conn.port
                });
                connections.delete(fd);
                knownSocketFds.delete(fd);
            }
        } catch (e) {
            // Ignorer silencieusement — close() est appelé très souvent
        }
    }
});

// ═══════════════════════════════════════════════════════════
// MESSAGE DE DÉMARRAGE
// ═══════════════════════════════════════════════════════════

console.log('═══════════════════════════════════════════════');
console.log('  Frida send() logger — Checkpoint Ch.13');
console.log(`  Filtrage fd : ${CONFIG.filterFd === -1 ? 'désactivé (tout capturer)' : 'fd ' + CONFIG.filterFd}`);
console.log(`  Hook write() : ${CONFIG.hookWrite ? 'oui' : 'non'}`);
console.log('  Ctrl+C pour arrêter');
console.log('═══════════════════════════════════════════════\n');
