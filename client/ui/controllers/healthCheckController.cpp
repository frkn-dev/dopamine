#include "healthCheckController.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <atomic>

#include "core/api/apiUtils.h"

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
// from libwg-go.a (api-probe.go): blocking WG/AWG handshake probe, returns RTT ms or -1
extern "C" int WgProbeRTT(const char *host, int port, const char *clientPrivKeyB64, const char *serverPubKeyB64,
                          const char *pskB64, const char *junkParamsJSON, int timeoutMs);
// from libwg-go.a (api-xray.go): full-path probe — starts a real xray instance with
// the given config and measures an HTTP request through it; returns C string
// "delayMs:error" (empty error on success), caller frees
extern "C" char *LibXrayPing(const char *datDir, const char *configPath, int timeoutSec, const char *url,
                             const char *proxy);
#elif defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>

#include "core/wgHandshakeProbe.h"
#include "platforms/android/android_utils.h"

// same Go API as LibXrayPing on Apple platforms, via the Java binding packaged
// in the APK (client/android/xray/libXray/libxray.aar); returns "delayMs:error"
static QString libXrayPingAndroid(const QString &configPath, int timeoutSec, const QString &url, const QString &proxy)
{
    // the gomobile runtime needs an app context before any Go call (Xray.kt does
    // the same in the vpn service process; the health probe runs in the app process)
    static bool seqInitialized = false;
    if (!seqInitialized) {
        QJniObject::callStaticMethod<void>("go/Seq", "setContext", "(Landroid/content/Context;)V",
                                           AndroidUtils::getActivity().object());
        seqInitialized = true;
    }
    const QJniObject reply = QJniObject::callStaticObjectMethod(
            "org/amnezia/vpn/protocol/xray/libXray/LibXray", "ping",
            "(Ljava/lang/String;Ljava/lang/String;JLjava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            QJniObject::fromString(QString()).object<jstring>(), QJniObject::fromString(configPath).object<jstring>(),
            static_cast<jlong>(timeoutSec), QJniObject::fromString(url).object<jstring>(),
            QJniObject::fromString(proxy).object<jstring>());
    QJniEnvironment env;
    if (env.checkAndClearExceptions()) {
        qWarning() << "[HEALTH] h2 probe: LibXray.ping failed (JNI exception)";
        return QString();
    }
    return reply.toString();
}
#endif

HealthCheckController::HealthCheckController(const QSharedPointer<ServersModel> &serversModel, QObject *parent)
    : QObject(parent), m_serversModel(serversModel)
{
}

void HealthCheckController::startProbe(bool force)
{
    if (m_serversModel.isNull()) {
        return;
    }

    // throttle: one run per kThrottleMs (manual refresh bypasses)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_lastRunMsecs != 0 && now - m_lastRunMsecs < kThrottleMs) {
        return;
    }
    m_lastRunMsecs = now;

    stopProbe();
    // clear displayed values: badges reappear only as fresh results arrive
    m_serversModel->clearHealthResults();

    // collect targets: vless (TCP) servers only; everything else stays n/a
    const int count = m_serversModel->getServersCount();
    for (int i = 0; i < count; ++i) {
        const QString protocol = m_serversModel->data(i, ServersModel::Roles::ServiceProtocolRole).toString();
        if (protocol != QStringLiteral("vless")) {
            continue;
        }

        const QJsonObject serverConfig = m_serversModel->getServerConfig(i);
        const QString host = serverConfig.value(QStringLiteral("hostName")).toString();
        if (host.isEmpty()) {
            continue;
        }

        // port and address from the xray outbound inside the stored config —
        // for xhttp-cdn the client connects to the CDN front (vnext address),
        // not to hostName (the origin, which is usually firewalled), so probing
        // hostName would mark a healthy server offline and vice versa
        quint16 port = 0;
        QString connectAddress;
        QString sni;
        QString path;
        bool httpsProbe = false;
        const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
        if (!containers.isEmpty()) {
            const QJsonObject containerObject = containers.at(0).toObject();

            auto endpointFromOutbound = [&](const QJsonObject &outbound) -> quint16 {
                const QJsonArray vnext = outbound.value(QStringLiteral("settings")).toObject()
                                                 .value(QStringLiteral("vnext")).toArray();
                if (vnext.isEmpty()) {
                    return 0;
                }
                const QJsonObject vnextObject = vnext.at(0).toObject();
                connectAddress = vnextObject.value(QStringLiteral("address")).toString();

                // xhttp over plain TLS = CDN-fronted server: probe through the CDN with a
                // real HTTPS request, the edge TCP/TLS alone says nothing about the origin
                const QJsonObject streamSettings = outbound.value(QStringLiteral("streamSettings")).toObject();
                if (streamSettings.value(QStringLiteral("security")).toString() == QStringLiteral("tls")
                    && streamSettings.value(QStringLiteral("network")).toString() == QStringLiteral("xhttp")) {
                    httpsProbe = true;
                    sni = streamSettings.value(QStringLiteral("tlsSettings")).toObject()
                                  .value(QStringLiteral("serverName")).toString();
                    path = streamSettings.value(QStringLiteral("xhttpSettings")).toObject()
                                   .value(QStringLiteral("path")).toString();
                }
                return static_cast<quint16>(vnextObject.value(QStringLiteral("port")).toInt());
            };

            // standard form: containers[0].xray.config = {"outbounds":[...]}
            const QJsonObject xray = containerObject.value(QStringLiteral("xray")).toObject();
            QJsonObject configRoot = QJsonDocument::fromJson(xray.value(QStringLiteral("config")).toString().toUtf8()).object();

            // xhttp-cdn form: containers[0].config holds the outbound (or outbounds) directly
            if (configRoot.isEmpty()) {
                configRoot = QJsonDocument::fromJson(containerObject.value(QStringLiteral("config")).toString().toUtf8()).object();
            }

            const QJsonArray outbounds = configRoot.value(QStringLiteral("outbounds")).toArray();
            if (!outbounds.isEmpty()) {
                port = endpointFromOutbound(outbounds.at(0).toObject());
            } else if (!configRoot.isEmpty()) {
                // single outbound object instead of an outbounds array
                port = endpointFromOutbound(configRoot);
            }
        }
        if (port == 0) {
            continue;
        }
        const QString probeHost = connectAddress.isEmpty() ? host : connectAddress;

        // one probe per host:port — duplicate rows share the node; every sharing
        // row gets the result (rows can share a host on different ports, e.g.
        // AWG and its mobile variant, so results are applied per row, not per host)
        const QString endpoint = QStringLiteral("%1:%2").arg(probeHost).arg(port);
        const auto seen = m_seenTcpEndpoints.constFind(endpoint);
        if (seen != m_seenTcpEndpoints.constEnd()) {
            m_queue[seen.value()].rows.append(i);
            continue;
        }
        m_seenTcpEndpoints.insert(endpoint, m_queue.size());

        Target target;
        target.rows.append(i);
        target.host = probeHost;
        target.port = port;
        target.httpsProbe = httpsProbe;
        target.sni = sni.isEmpty() ? probeHost : sni;
        target.path = path.isEmpty() ? QStringLiteral("/") : path;
        m_queue.append(target);
    }

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID)
    // collect awg/wireguard targets: blocking handshake probe on worker threads
    // (libwg-go on Apple platforms, core/wgHandshakeProbe on Android)
    for (int i = 0; i < count; ++i) {
        const QString protocol = m_serversModel->data(i, ServersModel::Roles::ServiceProtocolRole).toString();
        if (protocol != QStringLiteral("awg") && protocol != QStringLiteral("wireguard")) {
            continue;
        }

        const QJsonObject serverConfig = m_serversModel->getServerConfig(i);
        const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
        if (containers.isEmpty()) {
            continue;
        }
        const QJsonObject containerObject = containers.at(0).toObject();
        const QString containerName = protocol == QStringLiteral("awg") ? QStringLiteral("amnezia-awg")
                                                                        : QStringLiteral("amnezia-wireguard");
        const QJsonObject lastConfig = QJsonDocument::fromJson(
                containerObject.value(containerName).toObject().value(QStringLiteral("last_config")).toString().toUtf8())
                                               .object();
        if (lastConfig.isEmpty()) {
            continue;
        }

        QString host = serverConfig.value(QStringLiteral("hostName")).toString();
        if (host.isEmpty()) {
            host = lastConfig.value(QStringLiteral("hostName")).toString();
        }
        const QJsonValue portValue = lastConfig.value(QStringLiteral("port"));
        const quint16 port = static_cast<quint16>(portValue.isString() ? portValue.toString().toUShort()
                                                                       : portValue.toInt());
        if (host.isEmpty() || port == 0) {
            continue;
        }

        // one probe per host:port — duplicate entries (same node in several
        // server rows) racing handshakes with the same key get dropped by the
        // server and show up as random timeouts; sharing rows get the result
        // applied per row (rows can share a host on different ports)
        const QString endpoint = QStringLiteral("%1:%2").arg(host).arg(port);
        const auto seen = m_seenWgEndpoints.constFind(endpoint);
        if (seen != m_seenWgEndpoints.constEnd()) {
            m_wgQueue[seen.value()].rows.append(i);
            continue;
        }

        // AWG junk params (plain WG has none -> "{}"): last_config keys are
        // the short wg-quick names already (Jc, Jmin, ..., H1..H4, I1..I5)
        QJsonObject junk;
        for (const char *key : { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4", "H1", "H2", "H3", "H4",
                                 "I1", "I2", "I3", "I4", "I5" }) {
            const QString value = lastConfig.value(QString::fromLatin1(key)).toString();
            if (!value.isEmpty()) {
                junk.insert(QString::fromLatin1(key), value);
            }
        }

        // fall back to junk params embedded in the wg-quick INI (some configs
        // carry them only there / last_config fields may be null)
        if (junk.isEmpty()) {
            static const QSet<QString> junkKeys = { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4",
                                                    "H1", "H2", "H3", "H4", "I1", "I2", "I3", "I4", "I5" };
            const QString ini = containerObject.value(containerName).toObject().value(QStringLiteral("config")).toString();
            for (const QString &line : ini.split(QLatin1Char('\n'))) {
                const auto parts = line.split(QLatin1Char('='), Qt::SkipEmptyParts);
                if (parts.size() != 2) {
                    continue;
                }
                const QString key = parts[0].trimmed();
                if (junkKeys.contains(key)) {
                    const QString value = parts[1].trimmed();
                    if (!value.isEmpty()) {
                        junk.insert(key, value);
                    }
                }
            }
        }

        WgTarget target;
        target.rows.append(i);
        target.host = host;
        target.port = port;
        target.clientPrivKey = lastConfig.value(QStringLiteral("client_priv_key")).toString();
        target.serverPubKey = lastConfig.value(QStringLiteral("server_pub_key")).toString();
        target.psk = lastConfig.value(QStringLiteral("psk_key")).toString();
        target.junkParamsJson = QString::fromUtf8(QJsonDocument(junk).toJson(QJsonDocument::Compact));
        if (target.clientPrivKey.isEmpty() || target.serverPubKey.isEmpty()) {
            qWarning() << "[HEALTH] skip wg probe (no keys):" << host << "priv empty:" << target.clientPrivKey.isEmpty()
                       << "pub empty:" << target.serverPubKey.isEmpty();
            continue;
        }

        m_seenWgEndpoints.insert(endpoint, m_wgQueue.size());

        if (!junk.isEmpty()) {
            qDebug() << "[HEALTH] wg probe queued:" << host << port << "junk:" << target.junkParamsJson;
        }
        m_wgQueue.append(target);
    }

    // collect hysteria2 targets: a bare QUIC version-negotiation probe is unreliable
    // (live servers may ignore it), so H2 gets a full-path probe via LibXrayPing —
    // real handshake + auth + HTTP request through the server
    for (int i = 0; i < count; ++i) {
        const QString protocol = m_serversModel->data(i, ServersModel::Roles::ServiceProtocolRole).toString();
        if (protocol != QStringLiteral("hysteria2")) {
            continue;
        }

        const QJsonObject serverConfig = m_serversModel->getServerConfig(i);
        const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
        if (containers.isEmpty()) {
            continue;
        }
        const QJsonObject containerObject = containers.at(0).toObject();
        const QJsonObject xray = containerObject.value(QStringLiteral("xray")).toObject();
        QJsonObject configRoot = QJsonDocument::fromJson(xray.value(QStringLiteral("config")).toString().toUtf8()).object();
        if (configRoot.isEmpty()) {
            configRoot = QJsonDocument::fromJson(containerObject.value(QStringLiteral("config")).toString().toUtf8()).object();
        }
        const QJsonArray outbounds = configRoot.value(QStringLiteral("outbounds")).toArray();
        if (outbounds.isEmpty()) {
            continue;
        }
        const QJsonObject outbound = outbounds.at(0).toObject();
        const QJsonArray servers = outbound.value(QStringLiteral("settings")).toObject()
                                           .value(QStringLiteral("servers")).toArray();
        if (servers.isEmpty()) {
            continue;
        }
        const QJsonObject server = servers.at(0).toObject();
        const QString host = server.value(QStringLiteral("address")).toString();
        const quint16 port = static_cast<quint16>(server.value(QStringLiteral("port")).toInt());
        if (host.isEmpty() || port == 0) {
            continue;
        }

        // one probe per host:port, result applied to every sharing row
        const QString endpoint = QStringLiteral("%1:%2").arg(host).arg(port);
        const auto seen = m_seenH2Endpoints.constFind(endpoint);
        if (seen != m_seenH2Endpoints.constEnd()) {
            m_h2Queue[seen.value()].rows.append(i);
            continue;
        }
        m_seenH2Endpoints.insert(endpoint, m_h2Queue.size());

        H2Target target;
        target.rows.append(i);
        target.host = host;
        target.port = port;
        target.outbound = outbound;
        m_h2Queue.append(target);
    }
#endif

    startNext();
    startNextWg();
    startNextH2();

    // covers the "no targets at all" case — waiters must not hang
    m_probeActive = true;
    maybeFinishProbe();
}

void HealthCheckController::maybeFinishProbe()
{
    if (!m_probeActive) {
        return;
    }
    if (!m_queue.isEmpty() || !m_socketTargets.isEmpty() || !m_wgQueue.isEmpty() || !m_wgWatchers.isEmpty()
        || !m_h2Queue.isEmpty() || !m_h2Watchers.isEmpty()) {
        return;
    }
    m_probeActive = false;
    emit probingFinished();
}

void HealthCheckController::stopProbe()
{
    for (QTcpSocket *socket : m_socketTargets.keys()) {
        socket->abort();
        socket->deleteLater();
    }
    m_socketTargets.clear();
    m_timers.clear();
    m_attempts.clear();
    m_queue.clear();
    m_seenTcpEndpoints.clear();
    m_seenWgEndpoints.clear();

    // in-flight Go probes cannot be cancelled; detach the watchers and let
    // them finish harmlessly on their worker threads
    for (QFutureWatcher<int> *watcher : m_wgWatchers.keys()) {
        disconnect(watcher, nullptr, this, nullptr);
        watcher->deleteLater();
    }
    m_wgWatchers.clear();
    m_wgQueue.clear();

    // in-flight H2 pings run a full xray instance and cannot be cancelled cheaply;
    // detach the watchers and let them finish harmlessly
    for (QFutureWatcher<int> *watcher : m_h2Watchers.keys()) {
        disconnect(watcher, nullptr, this, nullptr);
        watcher->deleteLater();
    }
    m_h2Watchers.clear();
    m_h2Queue.clear();
    m_seenH2Endpoints.clear();

    // wake up anyone waiting on this run (e.g. auto server selection)
    if (m_probeActive) {
        m_probeActive = false;
        emit probingFinished();
    }
}

void HealthCheckController::startNext()
{
    while (m_socketTargets.size() < kMaxParallel && !m_queue.isEmpty()) {
        const Target target = m_queue.takeFirst();
        const int timeoutMs = target.httpsProbe ? kHttpsTimeoutMs : kTimeoutMs;

        QTcpSocket *socket = target.httpsProbe ? static_cast<QTcpSocket *>(new QSslSocket(this)) : new QTcpSocket(this);
        m_socketTargets.insert(socket, target);
        m_attempts.insert(socket, 1);
        m_timers.insert(socket, QElapsedTimer());
        m_timers[socket].start();

        if (target.httpsProbe) {
            QSslSocket *sslSocket = static_cast<QSslSocket *>(socket);
            // TLS up -> ask the xhttp endpoint for anything; the response status tells
            // whether the ORIGIN behind the CDN is alive, not just the CDN edge
            connect(sslSocket, &QSslSocket::encrypted, this, [this, sslSocket]() {
                const Target t = m_socketTargets.value(sslSocket);
                sslSocket->write("GET " + t.path.toUtf8() + " HTTP/1.1\r\nHost: " + t.sni.toUtf8()
                                 + "\r\nUser-Agent: Dopamine-HealthCheck\r\nConnection: close\r\n\r\n");
            });
            connect(sslSocket, &QSslSocket::readyRead, this, [this, sslSocket]() {
                if (!m_socketTargets.contains(sslSocket)) {
                    return;
                }
                const QByteArray head = sslSocket->peek(64);
                const int space = head.indexOf(' ');
                if (space < 0 || head.size() < space + 4) {
                    return; // status line not fully here yet
                }
                const int status = head.mid(space + 1, 3).toInt();
                if (status <= 0) {
                    return;
                }
                const qint64 elapsed = m_timers.value(sslSocket).elapsed();
                // any non-5xx answer (404/405/200...) means the origin responded;
                // 5xx is the CDN edge reporting a dead/hung origin
                finishSocket(sslSocket, status < 500 ? static_cast<int>(elapsed) : -1);
            });
        } else {
            connect(socket, &QTcpSocket::connected, this, [this, socket]() {
                const qint64 elapsed = m_timers.value(socket).elapsed();
                finishSocket(socket, static_cast<int>(elapsed));
            });
        }
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, timeoutMs](QTcpSocket::SocketError) {
            // flaky networks / DPI: one retry before declaring the server offline
            if (m_attempts.value(socket) < 2) {
                const Target target = m_socketTargets.value(socket);
                const int attempt = m_attempts.value(socket) + 1;
                m_attempts.insert(socket, attempt);
                m_timers[socket].restart();
                socket->abort();
                if (target.httpsProbe) {
                    static_cast<QSslSocket *>(socket)->connectToHostEncrypted(target.host, target.port, target.sni);
                } else {
                    socket->connectToHost(target.host, target.port);
                }
                QTimer::singleShot(timeoutMs, socket, [this, socket, attempt]() {
                    if (m_socketTargets.contains(socket) && m_attempts.value(socket) == attempt) {
                        finishSocket(socket, -1);
                    }
                });
            } else {
                finishSocket(socket, -1);
            }
        });

        if (target.httpsProbe) {
            static_cast<QSslSocket *>(socket)->connectToHostEncrypted(target.host, target.port, target.sni);
        } else {
            socket->connectToHost(target.host, target.port);
        }

        QTimer::singleShot(timeoutMs, socket, [this, socket]() {
            if (m_socketTargets.contains(socket) && m_attempts.value(socket) == 1) {
                finishSocket(socket, -1);
            }
        });
    }
}

void HealthCheckController::finishSocket(QTcpSocket *socket, int latencyMs)
{
    if (!m_socketTargets.contains(socket)) {
        return;
    }
    const Target target = m_socketTargets.take(socket);
    m_timers.remove(socket);
    m_attempts.remove(socket);
    socket->deleteLater();

    for (const int row : target.rows) {
        m_serversModel->setHealthResult(row, latencyMs);
    }

    startNext();
    maybeFinishProbe();
}

void HealthCheckController::startNextWg()
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID)
    // few parallel WG probes: racing handshakes with the same key get dropped
    while (m_wgWatchers.size() < kWgMaxParallel && !m_wgQueue.isEmpty()) {
        const WgTarget target = m_wgQueue.takeFirst();

        QFutureWatcher<int> *watcher = new QFutureWatcher<int>(this);
        m_wgWatchers.insert(watcher, target);

        connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher]() {
            finishWatcher(watcher);
        });

        watcher->setFuture(QtConcurrent::run([target]() -> int {
#if defined(Q_OS_ANDROID)
            const QJsonObject junk = QJsonDocument::fromJson(target.junkParamsJson.toUtf8()).object();
            return wgProbeHandshakeRTT(target.host, target.port, target.clientPrivKey, target.serverPubKey,
                                       target.psk, junk, kWgTimeoutMs);
#else
            const QByteArray host = target.host.toUtf8();
            const QByteArray privKey = target.clientPrivKey.toUtf8();
            const QByteArray pubKey = target.serverPubKey.toUtf8();
            const QByteArray psk = target.psk.toUtf8();
            const QByteArray junk = target.junkParamsJson.toUtf8();
            return WgProbeRTT(host.constData(), static_cast<int>(target.port), privKey.constData(), pubKey.constData(),
                              psk.constData(), junk.constData(), kWgTimeoutMs);
#endif
        }));
    }
#endif
}

void HealthCheckController::finishWatcher(QFutureWatcher<int> *watcher)
{
    if (!m_wgWatchers.contains(watcher)) {
        return;
    }
    WgTarget target = m_wgWatchers.take(watcher);
    const int rttMs = watcher->result();
    qDebug() << "[HEALTH] wg probe" << target.host << "rtt:" << rttMs << "attempt:" << target.attempts;
    watcher->deleteLater();

    // one retry on timeout — UDP probes on flaky links drop packets
    if (rttMs < 0 && target.attempts < 2) {
        target.attempts++;
        m_wgQueue.append(target);
    } else {
        for (const int row : target.rows) {
            m_serversModel->setHealthResult(row, rttMs);
        }
    }

    startNextWg();
    maybeFinishProbe();
}

void HealthCheckController::startNextH2()
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID)
    // strictly one at a time: each ping spins up a full xray instance and
    // libxray keeps global state
    if (!m_h2Watchers.isEmpty() || m_h2Queue.isEmpty()) {
        return;
    }
    const H2Target target = m_h2Queue.takeFirst();

    QFutureWatcher<int> *watcher = new QFutureWatcher<int>(this);
    m_h2Watchers.insert(watcher, target);
    connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher]() { finishH2Watcher(watcher); });

    watcher->setFuture(QtConcurrent::run([target]() -> int {
        static std::atomic<int> probePort { 19081 };
        const int localPort = probePort.fetch_add(1);

        QJsonObject inbound;
        inbound[QStringLiteral("listen")] = QStringLiteral("127.0.0.1");
        inbound[QStringLiteral("port")] = localPort;
        inbound[QStringLiteral("protocol")] = QStringLiteral("socks");
        inbound[QStringLiteral("settings")] = QJsonObject { { QStringLiteral("auth"), QStringLiteral("noauth") },
                                                            { QStringLiteral("udp"), true } };

        QJsonObject probeConfig;
        probeConfig[QStringLiteral("inbounds")] = QJsonArray { inbound };
        probeConfig[QStringLiteral("outbounds")] =
                QJsonArray { apiUtils::translateLegacyHysteria2Outbound(target.outbound) };

        QTemporaryFile file(QDir::tempPath() + QStringLiteral("/dopamine-h2probe-XXXXXX.json"));
        if (!file.open()) {
            return -1;
        }
        file.write(QJsonDocument(probeConfig).toJson(QJsonDocument::Compact));
        file.flush();

        const QByteArray proxy = QStringLiteral("socks5://127.0.0.1:%1").arg(localPort).toUtf8();
#if defined(Q_OS_ANDROID)
        const QString result = libXrayPingAndroid(file.fileName(), kH2TimeoutSec,
                                                  QStringLiteral("https://www.google.com/generate_204"),
                                                  QString::fromUtf8(proxy));
#else
        char *reply = LibXrayPing(nullptr, file.fileName().toUtf8().constData(), kH2TimeoutSec,
                                  "https://www.google.com/generate_204", proxy.constData());
        if (!reply) {
            return -1;
        }
        const QString result = QString::fromUtf8(reply);
        free(reply);
#endif
        if (result.isEmpty()) {
            return -1;
        }

        // libxray answers "delayMs:error"; delays >= 10000 are its failure markers
        const int delay = result.section(QLatin1Char(':'), 0, 0).toInt();
        const bool failed = !result.section(QLatin1Char(':'), 1).isEmpty();
        if (failed || delay <= 0 || delay >= 10000) {
            return -1;
        }
        return delay;
    }));
#endif
}

void HealthCheckController::finishH2Watcher(QFutureWatcher<int> *watcher)
{
    if (!m_h2Watchers.contains(watcher)) {
        return;
    }
    const H2Target target = m_h2Watchers.take(watcher);
    const int delayMs = watcher->result();
    qDebug() << "[HEALTH] h2 probe" << target.host << "delay:" << delayMs;
    watcher->deleteLater();

    for (const int row : target.rows) {
        m_serversModel->setHealthResult(row, delayMs);
    }

    startNextH2();
    maybeFinishProbe();
}
