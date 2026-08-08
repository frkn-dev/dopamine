#include "healthCheckController.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
// from libwg-go.a (api-probe.go): blocking WG/AWG handshake probe, returns RTT ms or -1
extern "C" int WgProbeRTT(const char *host, int port, const char *clientPrivKeyB64, const char *serverPubKeyB64,
                          const char *pskB64, const char *junkParamsJSON, int timeoutMs);
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

        // one probe per host:port — duplicate rows share the node
        const QString endpoint = QStringLiteral("%1:%2").arg(probeHost).arg(port);
        if (m_seenTcpEndpoints.contains(endpoint)) {
            continue;
        }
        m_seenTcpEndpoints.insert(endpoint);

        Target target;
        target.key = host;
        target.host = probeHost;
        target.port = port;
        target.httpsProbe = httpsProbe;
        target.sni = sni.isEmpty() ? probeHost : sni;
        target.path = path.isEmpty() ? QStringLiteral("/") : path;
        m_queue.append(target);
    }

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    // collect awg/wireguard targets: blocking handshake probe via libwg-go on worker threads
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

        // AWG junk params (plain WG has none -> "{}"): last_config keys are
        // the short wg-quick names already (Jc, Jmin, ..., H1..H4)
        QJsonObject junk;
        for (const char *key : { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4", "H1", "H2", "H3", "H4" }) {
            const QString value = lastConfig.value(QString::fromLatin1(key)).toString();
            if (!value.isEmpty()) {
                junk.insert(QString::fromLatin1(key), value);
            }
        }

        // fall back to junk params embedded in the wg-quick INI (some configs
        // carry them only there / last_config fields may be null)
        if (junk.isEmpty()) {
            static const QSet<QString> junkKeys = { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4",
                                                    "H1", "H2", "H3", "H4" };
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
        target.key = host;
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

        // one probe per host:port — duplicate entries (same node in several
        // server rows) racing handshakes with the same key get dropped by the
        // server and show up as random timeouts
        const QString endpoint = QStringLiteral("%1:%2").arg(host).arg(port);
        if (m_seenWgEndpoints.contains(endpoint)) {
            continue;
        }
        m_seenWgEndpoints.insert(endpoint);

        if (!junk.isEmpty()) {
            qDebug() << "[HEALTH] wg probe queued:" << host << port << "junk:" << target.junkParamsJson;
        }
        m_wgQueue.append(target);
    }
#endif

    startNext();
    startNextWg();
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

    m_serversModel->setHealthResult(target.key, latencyMs);

    startNext();
}

void HealthCheckController::startNextWg()
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    // few parallel WG probes: racing handshakes with the same key get dropped
    while (m_wgWatchers.size() < kWgMaxParallel && !m_wgQueue.isEmpty()) {
        const WgTarget target = m_wgQueue.takeFirst();

        QFutureWatcher<int> *watcher = new QFutureWatcher<int>(this);
        m_wgWatchers.insert(watcher, target);

        connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher]() {
            finishWatcher(watcher);
        });

        watcher->setFuture(QtConcurrent::run([target]() -> int {
            const QByteArray host = target.host.toUtf8();
            const QByteArray privKey = target.clientPrivKey.toUtf8();
            const QByteArray pubKey = target.serverPubKey.toUtf8();
            const QByteArray psk = target.psk.toUtf8();
            const QByteArray junk = target.junkParamsJson.toUtf8();
            return WgProbeRTT(host.constData(), static_cast<int>(target.port), privKey.constData(), pubKey.constData(),
                              psk.constData(), junk.constData(), kWgTimeoutMs);
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
    qDebug() << "[HEALTH] wg probe" << target.key << "rtt:" << rttMs << "attempt:" << target.attempts;
    watcher->deleteLater();

    // one retry on timeout — UDP probes on flaky links drop packets
    if (rttMs < 0 && target.attempts < 2) {
        target.attempts++;
        m_wgQueue.append(target);
    } else {
        m_serversModel->setHealthResult(target.key, rttMs);
    }

    startNextWg();
}
