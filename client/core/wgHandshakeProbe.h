#ifndef WGHANDSHAKEPROBE_H
#define WGHANDSHAKEPROBE_H

#include <QJsonObject>
#include <QString>

// Blocking WireGuard/AmneziaWG health probe: performs a real Noise_IK handshake
// initiation over UDP (OpenSSL primitives) and measures the RTT to the server's
// handshake response. AWG junk obfuscation (Jc/Jmin/Jmax, S1..S3, H1..H3,
// I1..I5) is supported; plain WireGuard is the same path with empty junkParams.
//
// Runs on a worker thread (blocking socket with recv timeout).
// Returns the RTT in milliseconds, or -1 on timeout/error.
int wgProbeHandshakeRTT(const QString &host, quint16 port,
                        const QString &clientPrivKeyB64, const QString &serverPubKeyB64,
                        const QString &pskB64, const QJsonObject &junkParams, int timeoutMs);

#endif // WGHANDSHAKEPROBE_H
