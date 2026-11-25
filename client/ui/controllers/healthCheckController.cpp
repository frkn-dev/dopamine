#include "healthCheckController.h"

#include "ui/models/servers_model.h"

#include <QDateTime>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <QTcpSocket>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <cstring>

#if !defined(Q_OS_WIN)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

// Internet checksum of an ICMPv4 message (RFC 1071).
quint16 icmpChecksum(const quint8 *data, const int len)
{
    quint32 sum = 0;
    for (int i = 0; i + 1 < len; i += 2) {
        sum += static_cast<quint16>((data[i] << 8) | data[i + 1]);
    }
    if (len & 1) {
        sum += static_cast<quint16>(data[len - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return static_cast<quint16>(~sum);
}

// Unprivileged ICMPv4 echo (SOCK_DGRAM + IPPROTO_ICMP): no raw socket, so it
// works inside the app sandbox on iOS and for a normal user elsewhere. Returns
// -1 when the platform has no ping socket (Windows), the packet never leaves or
// nothing answers within the timeout — the caller then falls back to TCP.
int icmpEchoRttMs(const QHostAddress &address, const int timeoutMs)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(address);
    Q_UNUSED(timeoutMs);
    return -1;
#else
    if (address.protocol() != QAbstractSocket::IPv4Protocol) {
        return -1;
    }
    const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in destination {};
    destination.sin_family = AF_INET;
    destination.sin_port = 0;
    destination.sin_addr.s_addr = htonl(address.toIPv4Address());

    // echo request built by hand: `struct icmphdr` does not exist on Darwin and
    // the reply is read the same way, so only the fixed byte layout is relied on
    quint8 packet[32] = {};
    packet[0] = 8; // echo request
    packet[1] = 0; // code
    const quint16 id = static_cast<quint16>(QRandomGenerator::global()->bounded(0x10000));
    const quint16 seq = 1;
    memcpy(packet + 4, &id, sizeof(id));
    memcpy(packet + 6, &seq, sizeof(seq));
    packet[8] = 0x21; // payload lets the kernel size the reply it builds
    packet[9] = 0x9f;
    packet[10] = 0x4c;
    packet[11] = 0x11;
    const quint16 checksum = icmpChecksum(packet, sizeof(packet));
    memcpy(packet + 2, &checksum, sizeof(checksum));

    QElapsedTimer timer;
    timer.start();
    const auto sent = ::sendto(fd, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr *>(&destination),
                               sizeof(destination));
    if (sent <= 0) {
        ::close(fd);
        return -1;
    }

    int rttMs = -1;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (true) {
        const qint64 left = deadline - QDateTime::currentMSecsSinceEpoch();
        if (left <= 0) {
            break;
        }
        pollfd descriptor {};
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        const int ready = ::poll(&descriptor, 1, static_cast<int>(left));
        if (ready <= 0) {
            break; // timeout (or a signal): no usable reply
        }
        // a ping socket also reports the destination unreachable this way
        if ((descriptor.revents & POLLERR) != 0 || (descriptor.revents & POLLIN) == 0) {
            break;
        }
        quint8 reply[512];
        const auto got = ::recv(fd, reply, sizeof(reply), 0);
        if (got < 8) {
            continue;
        }
        // Darwin hands the reply over with the IPv4 header still in front, Linux
        // ping sockets strip it — detected by the version/IHL nibble.
        const int offset = (reply[0] >> 4) == 4 ? (reply[0] & 0x0f) * 4 : 0;
        // type 0 / code 0 = echo reply. The kernel may rewrite the identifier of
        // a ping socket, so the id is not compared.
        if (got >= offset + 8 && reply[offset] == 0 && reply[offset + 1] == 0) {
            rttMs = static_cast<int>(timer.elapsed());
            break;
        }
    }
    ::close(fd);
    return rttMs;
#endif
}

// TCP-connect RTT to the node itself: the fallback when ICMP is unavailable or
// unanswered. Any port that answers proves the host is reachable; 443 is what a
// proxy node serves anyway.
int tcpConnectRttMs(const QHostAddress &address, const quint16 port, const int timeoutMs)
{
    QTcpSocket socket;
    // the probe must not consult the system proxy: a PAC configuration makes
    // connectToHost() resolve it synchronously on the calling thread
    socket.setProxy(QNetworkProxy::NoProxy);
    QElapsedTimer timer;
    timer.start();
    socket.connectToHost(address, port);
    if (!socket.waitForConnected(timeoutMs)) {
        socket.abort();
        return -1;
    }
    const int rttMs = static_cast<int>(timer.elapsed());
    socket.abort();
    return rttMs;
}

// Node address for a ping: IP literals go straight through, names are resolved
// on the worker thread (IPv4 first, ICMP needs it).
QHostAddress resolveProbeAddress(const QString &host)
{
    const QHostAddress literal(host);
    if (!literal.isNull()) {
        return literal;
    }
    const QHostInfo info = QHostInfo::fromName(host);
    const QList<QHostAddress> addresses = info.addresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            return address;
        }
    }
    return addresses.isEmpty() ? QHostAddress() : addresses.first();
}

} // namespace

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID) || defined(Q_OS_WIN) || defined(Q_OS_LINUX)
#include "core/wgHandshakeProbe.h"
#endif

HealthCheckController::HealthCheckController(const QSharedPointer<ServersModel> &serversModel, QObject *parent)
    : QObject(parent), m_serversModel(serversModel)
{
    // A run measures the row numbers it queued. A subscription refresh or a peers
    // sync rebuilds those rows in place (ServersModel bumps its row generation),
    // so a result still in flight would be written into another server's row —
    // that is exactly the "a probe outlived a refresh" class of bug that crashed
    // on iOS (qBadAlloc). Stop the run instead: everything waiting on it
    // (auto-select decision, pre-connect probe) rebuilds from the fresh list.
    if (m_serversModel) {
        connect(m_serversModel.get(), &QAbstractItemModel::modelReset, this, &HealthCheckController::stopProbe);
    }

    // Проверка качества замеров без кликов: DOPAMINE_PROBE_SELFTEST=1 запускает
    // самопроверку handshake-пробы сразу после старта и печатает [SELFTEST] в лог.
    if (qEnvironmentVariableIsSet("DOPAMINE_PROBE_SELFTEST")) {
        QTimer::singleShot(3000, this, [this]() { runProbeSelfTest(); });
    }
}

// Junk-параметры AWG: их надо отправлять перед handshake, иначе сервер молчит.
// Собираем из last_config (long/short имена) и добираем из INI, если поля пустые.
static QJsonObject wgJunkParams(const QJsonObject &lastConfig, const QString &ini)
{
    auto jsonText = [](const QJsonValue &value) {
        if (value.isString()) {
            return value.toString().trimmed();
        }
        if (value.isDouble()) {
            return QString::number(static_cast<qint64>(value.toDouble()));
        }
        return QString();
    };
    QJsonObject params = lastConfig;
    const QList<QPair<QString, QString>> aliases = {
        { QStringLiteral("junkPacketCount"), QStringLiteral("Jc") },
        { QStringLiteral("junkPacketMinSize"), QStringLiteral("Jmin") },
        { QStringLiteral("junkPacketMaxSize"), QStringLiteral("Jmax") },
        { QStringLiteral("initPacketJunkSize"), QStringLiteral("S1") },
        { QStringLiteral("responsePacketJunkSize"), QStringLiteral("S2") },
        { QStringLiteral("cookieReplyPacketJunkSize"), QStringLiteral("S3") },
        { QStringLiteral("transportPacketJunkSize"), QStringLiteral("S4") },
        { QStringLiteral("initPacketMagicHeader"), QStringLiteral("H1") },
        { QStringLiteral("responsePacketMagicHeader"), QStringLiteral("H2") },
        { QStringLiteral("underloadPacketMagicHeader"), QStringLiteral("H3") },
        { QStringLiteral("transportPacketMagicHeader"), QStringLiteral("H4") },
        { QStringLiteral("specialJunk1"), QStringLiteral("I1") },
        { QStringLiteral("specialJunk2"), QStringLiteral("I2") },
        { QStringLiteral("specialJunk3"), QStringLiteral("I3") },
        { QStringLiteral("specialJunk4"), QStringLiteral("I4") },
        { QStringLiteral("specialJunk5"), QStringLiteral("I5") },
        { QStringLiteral("headerProtectionKey"), QStringLiteral("HeaderProtectionKey") },
    };
    for (const auto &alias : aliases) {
        if (!jsonText(params.value(alias.second)).isEmpty()) {
            continue;
        }
        const QString value = jsonText(params.value(alias.first));
        if (!value.isEmpty()) {
            params.insert(alias.second, value);
        }
    }
    QJsonObject junk;
    for (const char *key : { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4", "H1", "H2", "H3", "H4",
                             "I1", "I2", "I3", "I4", "I5", "HeaderProtectionKey" }) {
        const QString value = jsonText(params.value(QString::fromLatin1(key)));
        if (!value.isEmpty()) {
            junk.insert(QString::fromLatin1(key), value);
        }
    }

    // fill gaps from junk params embedded in the wg-quick INI (some configs
    // carry them only there / last_config fields may be null)
    static const QSet<QString> junkKeys = { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4",
                                            "H1", "H2", "H3", "H4", "I1", "I2", "I3", "I4", "I5",
                                            "HeaderProtectionKey" };
    for (const QString &line : ini.split(QLatin1Char('\n'))) {
        const auto parts = line.split(QLatin1Char('='), Qt::SkipEmptyParts);
        if (parts.size() != 2) {
            continue;
        }
        const QString key = parts[0].trimmed();
        if (junkKeys.contains(key) && !junk.contains(key)) {
            const QString value = parts[1].trimmed();
            if (!value.isEmpty()) {
                junk.insert(key, value);
            }
        }
    }

    return junk;
}

// A peers card groups nodes that each carry their own server key and AWG junk,
// while the cached tunnel config only holds the primary node's profile. Probing
// another node's address with the primary key would always look dead, so the
// probe takes the node's own profile when the row has one.
static QString peersNodeHost(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    return colon > 0 ? endpoint.left(colon) : endpoint;
}

static QJsonObject peersNodeJunk(const QJsonObject &obfuscation)
{
    // The same short keys wgJunkParams produces, with the first letter
    // capitalized (the api sends them lower-case).
    static const QStringList known = { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4", "H1", "H2", "H3",
                                       "H4", "I1", "I2", "I3", "I4", "I5", "HeaderProtectionKey" };
    QJsonObject junk;
    for (auto it = obfuscation.constBegin(); it != obfuscation.constEnd(); ++it) {
        QString key = it.key();
        if (key.isEmpty()) {
            continue;
        }
        key[0] = key[0].toUpper();
        if (!known.contains(key)) {
            continue;
        }
        const QString value = it.value().isDouble()
                ? QString::number(static_cast<qint64>(it.value().toDouble()))
                : it.value().toString();
        if (!value.isEmpty()) {
            junk.insert(key, value);
        }
    }
    return junk;
}

// m_seen*Endpoints caches the position of a target inside its queue. The
// position only holds while the queue keeps its order: startNext*() pops from
// the front, and a re-entrant run clears the queue altogether. So a cached
// position has to be bounds-checked before use — an out-of-range [] reads and
// writes past the list, and appending to the garbage QList<int> header found
// there throws std::bad_alloc (SIGABRT in qBadAlloc, seen on iOS).
template<typename TargetType>
static bool appendRowToQueuedTarget(QList<TargetType> &queue, const QHash<QString, int> &positions,
                                    const QString &endpoint, int row, const QString &cardId)
{
    const auto it = positions.constFind(endpoint);
    if (it == positions.constEnd() || it.value() < 0 || it.value() >= queue.size()) {
        return false;
    }
    queue[it.value()].rows.append(row);
    queue[it.value()].cardIds.append(cardId);
    return true;
}

void HealthCheckController::startProbe(bool force)
{
    // stopProbe() below clears the queues together with their position maps, and
    // both it (probingFinished) and clearHealthResults() (dataChanged) can call
    // back into QML, which may re-enter this function. A nested run then drains
    // queue through startNext*() while the outer loop still resolves addresses
    // through the position maps: a stale index writes into freed heap and the
    // next rows.append() on that garbage header aborts. A run already being
    // filled covers the same servers, so the nested call is simply dropped.
    if (m_startingProbe) {
        qDebug() << "[HEALTH] probe skipped: run already being started";
        return;
    }
    QScopedValueRollback<bool> startGuard(m_startingProbe, true);

    // Глобальное «не чаще раза в минуту» здесь было ошибкой: оно блокировало
    // пробы и для тех адресов, по которым замера вообще нет (карточка пула
    // показывала «no data»). Свежесть проверяет вызывающий — по каждому адресу
    // отдельно (кэш 5 мин), плюс ниже остаётся общий throttle.

    if (m_tunnelActive) {
        // на живом туннеле не меряем: результат врёт и портит выбор адресов
        qDebug() << "[HEALTH] probe skipped: tunnel is active";
        return;
    }

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

    // collect ping targets: proxy protocols (vless, hysteria2, any client_config
    // card) cannot be handshake-probed from the client, so they get a server ping
    // on the node's own address. One probe per address answers every protocol of
    // the pool, and each row keeps the value for the card it shows.
    const int count = m_serversModel->getServersCount();
    for (int i = 0; i < count; ++i) {
        if (!m_serversModel->data(i, ServersModel::Roles::HealthPingRole).toBool()) {
            continue;
        }
        const QString host = m_serversModel->data(i, ServersModel::Roles::EndpointHostRole).toString();
        if (host.isEmpty()) {
            continue;
        }

        // multi-IP nodes: ping every entry address — the badge is the best result
        QStringList probeHosts = m_serversModel->data(i, ServersModel::Roles::NodeIpsRole).toStringList();
        probeHosts.removeDuplicates();
        if (probeHosts.isEmpty()) {
            probeHosts.append(host);
        }

        const QString protocol = m_serversModel->data(i, ServersModel::Roles::ServiceProtocolRole).toString();
        const QString cardId = m_serversModel->peerCardId(i);
        for (const QString &probeHost : probeHosts) {
            if (appendRowToQueuedTarget(m_pingQueue, m_seenPingEndpoints, probeHost, i, cardId)) {
                continue;
            }
            m_seenPingEndpoints.insert(probeHost, m_pingQueue.size());

            PingTarget target;
            target.rows.append(i);
            target.cardIds.append(cardId);
            target.host = probeHost;
            target.proto = protocol;
            target.port = static_cast<quint16>(m_serversModel->endpointPort(i));
            target.udpOnly = protocol == QLatin1String("hysteria2");
            m_pingQueue.append(target);
        }
    }

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID) || defined(Q_OS_WIN) || defined(Q_OS_LINUX)
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
        // the result belongs to the card on screen: the pool may show another
        // protocol by the time the handshake answers
        const QString cardId = m_serversModel->peerCardId(i);
        const QJsonValue portValue = lastConfig.value(QStringLiteral("port"));
        const quint16 port = static_cast<quint16>(portValue.isString() ? portValue.toString().toUShort()
                                                                       : portValue.toInt());
        if (host.isEmpty() || port == 0) {
            continue;
        }

        const QString ini = containerObject.value(containerName).toObject().value(QStringLiteral("config")).toString();
        const QJsonObject junk = wgJunkParams(lastConfig, ini);
        const QString clientPrivKey = lastConfig.value(QStringLiteral("client_priv_key")).toString();
        const QString serverPubKey = lastConfig.value(QStringLiteral("server_pub_key")).toString();
        if (clientPrivKey.isEmpty() || serverPubKey.isEmpty()) {
            qWarning() << "[HEALTH] skip wg probe (no keys):" << host << "priv empty:" << clientPrivKey.isEmpty()
                       << "pub empty:" << serverPubKey.isEmpty();
            continue;
        }

        // multi-IP nodes: probe every entry address — same port, keys and junk
        // per the node_ips contract; the row badge is the best result
        QStringList probeHosts;
        const QJsonArray nodeIps = serverConfig.value(QStringLiteral("node_ips")).toArray();
        for (const QJsonValue &v : nodeIps) {
            const QString ip = v.toString();
            if (!ip.isEmpty()) {
                probeHosts.append(ip);
            }
        }
        probeHosts.removeDuplicates();
        if (probeHosts.isEmpty()) {
            probeHosts.append(host);
        }

        for (const QString &probeHost : probeHosts) {
            const quint16 hostPort = static_cast<quint16>(
                    ServersModel::portForNodeIp(serverConfig, probeHost, port));
            // per-node profile of a peers card (empty for v1 rows)
            QJsonObject probeJunk = junk;
            QString probeServerKey = serverPubKey;
            for (const QJsonValue &nodeValue : serverConfig.value(QStringLiteral("peerNodes")).toArray()) {
                const QJsonObject node = nodeValue.toObject();
                if (peersNodeHost(node.value(QStringLiteral("endpoint")).toString()) != probeHost) {
                    continue;
                }
                const QString key = node.value(QStringLiteral("serverPublicKey")).toString();
                if (!key.isEmpty()) {
                    probeServerKey = key;
                }
                const QJsonObject nodeJunk = peersNodeJunk(node.value(QStringLiteral("obfuscation")).toObject());
                if (!nodeJunk.isEmpty()) {
                    probeJunk = nodeJunk;
                }
                break;
            }
            // one probe per host:port — duplicate entries (same node in several
            // server rows) racing handshakes with the same key get dropped by the
            // server and show up as random timeouts; sharing rows get the result
            // applied per row (rows can share a host on different ports)
            const QString endpoint = QStringLiteral("%1:%2").arg(probeHost).arg(hostPort);
            if (appendRowToQueuedTarget(m_wgQueue, m_seenWgEndpoints, endpoint, i, cardId)) {
                continue;
            }

            WgTarget target;
            target.rows.append(i);
            target.cardIds.append(cardId);
            target.host = probeHost;
            target.port = hostPort;
            target.clientPrivKey = clientPrivKey;
            target.serverPubKey = probeServerKey;
            target.psk = lastConfig.value(QStringLiteral("psk_key")).toString();
            target.junkParamsJson = QString::fromUtf8(QJsonDocument(probeJunk).toJson(QJsonDocument::Compact));

            m_seenWgEndpoints.insert(endpoint, m_wgQueue.size());

            if (!probeJunk.isEmpty()) {
                qDebug() << "[HEALTH] wg probe queued:" << probeHost << port << "junk:" << target.junkParamsJson;
            }
            m_wgQueue.append(target);
        }
    }
#endif

    startNextPing();
    startNextWg();

    // covers the "no targets at all" case — waiters must not hang
    m_probeActive = true;
    emit probingChanged();
    maybeFinishProbe();
}

void HealthCheckController::maybeFinishProbe()
{
    if (!m_probeActive) {
        return;
    }
    if (!m_pingQueue.isEmpty() || !m_pingWatchers.isEmpty() || !m_wgQueue.isEmpty()
        || !m_wgWatchers.isEmpty()) {
        return;
    }
    m_probeActive = false;
    if (m_serversModel) {
        m_serversModel->saveEndpointHealthCache(); // переживёт перезапуск
        m_serversModel->markProbeRun();
    }
    emit probingChanged();
    emit probingFinished();
}

void HealthCheckController::stopProbe()
{
    // in-flight pings finish on their worker threads; detach and drop the results
    for (QFutureWatcher<PingResult> *watcher : m_pingWatchers.keys()) {
        disconnect(watcher, nullptr, this, nullptr);
        watcher->deleteLater();
    }
    m_pingWatchers.clear();
    m_pingQueue.clear();
    m_seenPingEndpoints.clear();
    m_seenWgEndpoints.clear();

    // in-flight Go probes cannot be cancelled; detach the watchers and let
    // them finish harmlessly on their worker threads
    for (QFutureWatcher<int> *watcher : m_wgWatchers.keys()) {
        disconnect(watcher, nullptr, this, nullptr);
        watcher->deleteLater();
    }
    m_wgWatchers.clear();
    m_wgQueue.clear();

    // прогон мог прерваться (уход в фон/дисконнект) — сохраняем то, что успели
    if (m_serversModel) {
        m_serversModel->saveEndpointHealthCache();
        m_serversModel->markProbeRun();
    }

    // wake up anyone waiting on this run (e.g. auto server selection)
    if (m_probeActive) {
        m_probeActive = false;
        emit probingChanged();
        emit probingFinished();
    }
}

void HealthCheckController::startNextPing()
{
    while (m_pingWatchers.size() < kMaxParallel && !m_pingQueue.isEmpty()) {
        const PingTarget target = m_pingQueue.takeFirst();

        auto *watcher = new QFutureWatcher<PingResult>(this);
        m_pingWatchers.insert(watcher, target);
        connect(watcher, &QFutureWatcher<PingResult>::finished, this,
                [this, watcher]() { finishPingWatcher(watcher); });

        const QString host = target.host;
        const int icmpTimeoutMs = kPingIcmpTimeoutMs;
        const int tcpTimeoutMs = kPingTcpTimeoutMs;
        const quint16 tcpPort = target.port > 0 ? target.port : kPingTcpPort;
        const bool udpOnly = target.udpOnly;
        watcher->setFuture(QtConcurrent::run([host, icmpTimeoutMs, tcpTimeoutMs, tcpPort, udpOnly]() {
            PingResult result;
            const QHostAddress address = resolveProbeAddress(host);
            if (address.isNull()) {
                return result;
            }
            // real ICMP first: it measures the node itself and needs no open
            // port. No ping socket or no answer -> TCP connect on 443.
            const int icmpMs = icmpEchoRttMs(address, icmpTimeoutMs);
            if (icmpMs >= 0) {
                result.rttMs = icmpMs;
                result.viaIcmp = true;
                return result;
            }
            // A QUIC/UDP proxy port never completes a TCP handshake: the TCP
            // fallback would report "no answer" for a perfectly live node.
            if (udpOnly) {
                return result;
            }
            result.rttMs = tcpConnectRttMs(address, tcpPort, tcpTimeoutMs);
            return result;
        }));
    }
}

void HealthCheckController::finishPingWatcher(QFutureWatcher<PingResult> *watcher)
{
    if (!m_pingWatchers.contains(watcher)) {
        return;
    }
    const PingTarget target = m_pingWatchers.take(watcher);
    const PingResult result = watcher->result();
    watcher->deleteLater();

    qDebug().noquote() << QStringLiteral("[HEALTH] %1: proxy ping via %2 %3")
                                  .arg(target.proto.isEmpty() ? QStringLiteral("proxy") : target.proto,
                                       result.viaIcmp ? QStringLiteral("icmp") : QStringLiteral("tcp"),
                                       result.rttMs >= 0 ? QStringLiteral("%1 ms").arg(result.rttMs)
                                                         : QStringLiteral("no reply"));

    for (int i = 0; i < target.rows.size(); ++i) {
        m_serversModel->setHealthResult(target.rows.at(i), result.rttMs,
                                        i < target.cardIds.size() ? target.cardIds.at(i) : QString());
    }

    startNextPing();
    maybeFinishProbe();
}


// Самопроверка пробы: показывает, что мы меряем настоящий handshake, а не эхо
// чего-то другого. Три контроля на каждый адрес:
//   1) настоящая проба 3 раза — значения должны быть близкими и конечными;
//   2) проба со случайным server_pub_key — сервер НЕ должен ответить (иначе
//      «ответ» приходит не от handshake);
//   3) проба на закрытый порт — тоже обязана молчать.
// Пишет строки [SELFTEST] в лог; вызывается из диагностики.
void HealthCheckController::runProbeSelfTest()
{
    // Проходим по ВСЕМ awg/wg-серверам подписки: если проба молчит там, где
    // приложение реально подключается, — виновата проба, а не нода.
    struct SelfTestTarget
    {
        QString host;
        quint16 port = 0;
        QString clientPrivKey;
        QString serverPubKey;
        QString psk;
        QString junkJson;
        QStringList endpoints;
    };

    QList<SelfTestTarget> targets;
    const int count = m_serversModel->getServersCount();
    for (int i = 0; i < count && targets.size() < 12; ++i) {
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
        const QJsonObject container = containerObject.value(containerName).toObject();
        const QJsonObject lastConfig =
                QJsonDocument::fromJson(container.value(QStringLiteral("last_config")).toString().toUtf8()).object();
        if (lastConfig.isEmpty()) {
            continue;
        }

        SelfTestTarget target;
        target.host = serverConfig.value(QStringLiteral("hostName")).toString();
        if (target.host.isEmpty()) {
            target.host = lastConfig.value(QStringLiteral("hostName")).toString();
        }
        const QJsonValue portValue = lastConfig.value(QStringLiteral("port"));
        target.port = static_cast<quint16>(portValue.isString() ? portValue.toString().toUShort()
                                                                : portValue.toInt());
        target.clientPrivKey = lastConfig.value(QStringLiteral("client_priv_key")).toString();
        target.serverPubKey = lastConfig.value(QStringLiteral("server_pub_key")).toString();
        target.psk = lastConfig.value(QStringLiteral("psk_key")).toString();
        if (target.host.isEmpty() || target.port == 0 || target.clientPrivKey.isEmpty()
            || target.serverPubKey.isEmpty()) {
            continue;
        }
        target.junkJson = QString::fromUtf8(
                QJsonDocument(wgJunkParams(lastConfig, container.value(QStringLiteral("config")).toString()))
                        .toJson(QJsonDocument::Compact));

        for (const QJsonValue &v : serverConfig.value(QStringLiteral("node_ips")).toArray()) {
            const QString ip = v.toString();
            if (!ip.isEmpty() && !target.endpoints.contains(ip)) {
                target.endpoints.append(ip);
            }
        }
        if (target.endpoints.isEmpty()) {
            target.endpoints.append(target.host);
        }
        targets.append(target);
    }

    if (targets.isEmpty()) {
        qWarning() << "[SELFTEST] no awg server with keys configured";
        return;
    }

    QtConcurrent::run([targets]() {
        for (const SelfTestTarget &target : targets) {
            const QJsonObject junkParams = QJsonDocument::fromJson(target.junkJson.toUtf8()).object();

            QByteArray randomPub(32, Qt::Uninitialized);
            for (int i = 0; i < randomPub.size(); ++i) {
                randomPub[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
            }
            const QString badPubKey = QString::fromLatin1(randomPub.toBase64());

            for (const QString &endpoint : target.endpoints) {
                QList<int> real;
                for (int i = 0; i < 3; ++i) {
                    real.append(wgProbeHandshakeRTT(endpoint, target.port, target.clientPrivKey,
                                                    target.serverPubKey, target.psk, junkParams, kWgTimeoutMs));
                }
                const int badKey = wgProbeHandshakeRTT(endpoint, target.port, target.clientPrivKey, badPubKey,
                                                       target.psk, junkParams, kWgTimeoutMs);
                const int closedPort = wgProbeHandshakeRTT(endpoint, 1, target.clientPrivKey,
                                                           target.serverPubKey, target.psk, junkParams,
                                                           kWgTimeoutMs);

                int okCount = 0;
                int best = -1;
                for (const int rtt : real) {
                    if (rtt >= 0) {
                        ++okCount;
                        best = (best < 0 || rtt < best) ? rtt : best;
                    }
                }

                const bool verdict = (okCount > 0 && badKey < 0 && closedPort < 0);
                qDebug().noquote() << "[SELFTEST]" << endpoint << ":" << target.port
                                   << "real=" << real << "badkey=" << badKey << "closedport=" << closedPort
                                   << "best=" << best << "verdict=" << (verdict ? "OK" : "CHECK")
                                   << "junk=" << (target.junkJson.size() > 2 ? "yes" : "no");
            }
        }
        qDebug().noquote() << "[SELFTEST] finished";
    });
}

void HealthCheckController::startNextWg()
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID) || defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // few parallel WG probes: racing handshakes with the same key get dropped
    while (m_wgWatchers.size() < kWgMaxParallel && !m_wgQueue.isEmpty()) {
        const WgTarget target = m_wgQueue.takeFirst();

        QFutureWatcher<int> *watcher = new QFutureWatcher<int>(this);
        m_wgWatchers.insert(watcher, target);

        connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher]() {
            finishWatcher(watcher);
        });

        watcher->setFuture(QtConcurrent::run([target]() -> int {
            const QJsonObject junk = QJsonDocument::fromJson(target.junkParamsJson.toUtf8()).object();
            return wgProbeHandshakeRTT(target.host, target.port, target.clientPrivKey, target.serverPubKey,
                                       target.psk, junk, kWgTimeoutMs);
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
        for (int i = 0; i < target.rows.size(); ++i) {
            m_serversModel->setHealthResult(target.rows.at(i), rttMs,
                                            i < target.cardIds.size() ? target.cardIds.at(i) : QString());
        }
        // результат фиксируем на конкретный адрес: в пуле у ноды несколько IP,
        // и один может быть заблокирован, пока другой отвечает
        m_serversModel->setEndpointHealthResult(target.host, target.port, rttMs, target.attempts + 1);
    }

    startNextWg();
    maybeFinishProbe();
}
