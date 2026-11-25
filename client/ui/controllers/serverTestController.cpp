#include "serverTestController.h"

#include "ui/models/node_endpoints.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cstdlib>

#include "core/wgHandshakeProbe.h"
#include "core/wgJunkParams.h"
#include "protocols/protocols_defs.h"
#include "ui/controllers/peersImportController.h"
#include "version.h"

namespace
{
// Sequential samples per endpoint, not a burst: every sample creates session
// state on the server and handshakes racing with the same key get dropped,
// which would read as random loss.
constexpr int kSamplesPerEndpoint = 3;
// the whole download is repeated three times and averaged: one read measures TCP
// ramp-up as much as the node
constexpr int kSpeedRounds = 3;
// one hanging probe must not stall the run: 3 tries * 2.5 s plus slack
constexpr int kHandshakeWatchdogMs = 12000;
// a sibling address is a guess: one quick try is enough
constexpr int kFallbackTimeoutMs = 1500;
// An address that missed twice is dead for this run: three full 4 s timeouts per
// address made a pass over a whole pool outlast the app's own auto-connect.
constexpr int kTimeoutsToGiveUp = 2;
constexpr int kMaxParallelEndpoints = 5;
constexpr int kSampleGapMs = 150;
constexpr int kHandshakeTimeoutMs = 2500;

// Bench files live on the node itself (same layout as status.frkn.org): HTTPS
// only, and the host comes from the api per node — a central proxy would measure
// the proxy's uplink instead of the node's path.
constexpr const char *kBenchPingPath = "/speedtest/ping.txt";
constexpr const char *kBenchSmallPath = "/speedtest/1mb.test";
constexpr const char *kBenchBigPath = "/speedtest/10mb.test";
constexpr int kBenchPingTimeoutMs = 4000;
constexpr int kBenchSmallTimeoutMs = 6000;
constexpr int kBenchBigTimeoutMs = 12000;
// Only escalate to 10 MB when 1 MB arrived fast enough for the bigger sample to
// be more accurate instead of just slower.
constexpr qint64 kBigFileThresholdMs = 900;

// Writes one line to the verification sink when DOPAMINE_SERVERTEST is set:
// the app's own log only exists when the user enabled log saving, and a
// headless (session 0 / offscreen) run has nowhere else to report to.
void selftestLog(const QString &line)
{
    qDebug().noquote() << line;
    QString path = qEnvironmentVariable("DOPAMINE_SERVERTEST_OUT");
    if (path.isEmpty()) {
        path = QDir(QDir::tempPath()).filePath(QStringLiteral("dopamine-servertest.log"));
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << line << "\n";
        stream.flush();
    }
}

// Bench host of a node: the first label of its hostname (eu01.frkn.org ->
// eu01.bench.beacon.frkn.org). Only pool nodes get a name, so this cannot
// collide across the fleet.
QString benchLabel(const QString &hostname)
{
    return hostname.split(QLatin1Char('.')).first();
}

// Short reason for the UI: "not measured" alone hid whether the endpoint was
// unreachable, had no bench file, or failed TLS.
QString speedErrorCode(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("dns");
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("refused");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("timeout");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("tls");
    case QNetworkReply::ContentNotFoundError:
        return QStringLiteral("404");
    case QNetworkReply::ContentAccessDenied:
        return QStringLiteral("403");
    default:
        return QStringLiteral("error");
    }
}

int medianOf(QList<int> values)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](int v) { return v < 0; }), values.end());
    if (values.isEmpty()) {
        return -1;
    }
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    return values.size() % 2 == 0 ? (values[mid - 1] + values[mid]) / 2 : values[mid];
}

double medianOf(QList<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    return values.size() % 2 == 0 ? (values[mid - 1] + values[mid]) / 2.0 : values[mid];
}

// Subscription of a row, resolved exactly the way the server list resolves it
// (ServersListView.pooledProtocolsBySubscription: `peerSubscriptionId ||
// subscriptionId`). A peers row names its subscription in `peerSubscriptionId`,
// a v1 config through `auth_data.api_key`, which the display map already folds
// into `subscriptionId`. Reading `subscriptionId` alone dropped every pooled row
// of a v2 subscription once a concrete subscription was selected, and the pass
// found no targets at all.
QString rowSubscriptionOf(const QVariantMap &display)
{
    const QString peer = display.value(QStringLiteral("peerSubscriptionId")).toString();
    return peer.isEmpty() ? display.value(QStringLiteral("subscriptionId")).toString() : peer;
}

// Protocol a row dials, in the canonical form the list's filter compares
// ("awg" / "wireguard" / "hysteria2"). A pooled row is one pool whose protocol
// is a property of the row: it is the variant `peerCardId` points at, so the
// raw tag of a variant that is not on screen cannot decide whether the row is
// tested. A v1 row keeps its backend tag, as before.
QString rowProtocolOf(const QVariantMap &display)
{
    if (display.value(QStringLiteral("isPeerPool")).toBool()) {
        const QString cardId = display.value(QStringLiteral("peerCardId")).toString();
        for (const QVariant &entry : display.value(QStringLiteral("peerVariants")).toList()) {
            const QVariantMap variant = entry.toMap();
            if (!cardId.isEmpty() && variant.value(QStringLiteral("cardId")).toString() == cardId) {
                const QString protocol = variant.value(QStringLiteral("protocol")).toString();
                if (!protocol.isEmpty()) {
                    return protocol;
                }
            }
        }
    }
    QString raw = display.value(QStringLiteral("protocolRaw")).toString();
    if (raw.isEmpty()) {
        raw = display.value(QStringLiteral("protocol")).toString();
    }
    return raw;
}

QString canonicalProtocolOf(const QString &protocol)
{
    return amnezia::canonicalServiceProtocol(protocol).toLower();
}

// Pool name of a row: `pool` is the name the list and the pool filter show.
// A pooled row also carries `poolKey` ("<shard>:<pool>"), which names the same
// pool when the stored displayInfo lagged behind.
QString rowPoolOf(const QVariantMap &display)
{
    const QString pool = display.value(QStringLiteral("pool")).toString();
    if (!pool.isEmpty()) {
        return pool;
    }
    const QString poolKey = display.value(QStringLiteral("poolKey")).toString();
    const int sep = poolKey.indexOf(QLatin1Char(':'));
    return sep >= 0 ? poolKey.mid(sep + 1) : poolKey;
}

// "<host>:<port>" as the peers payload spells an endpoint.
QString endpointHostOf(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    return colon > 0 ? endpoint.left(colon) : endpoint;
}

int endpointPortOf(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon >= endpoint.size() - 1) {
        return 0;
    }
    bool ok = false;
    const int port = endpoint.mid(colon + 1).toInt(&ok);
    return ok ? port : 0;
}

// `peerVariants` entry of a pool row that serves `canonicalProtocol`, with the
// nodes and the card id of that protocol. The row is one pool and the protocol is
// a property of it, so the endpoint list and the card that has to be provisioned
// come from the variant the user picked — not from the one the servers list
// currently points the row at.
QJsonObject poolVariantOf(const QJsonObject &serverConfig, const QString &canonicalProtocol)
{
    const QJsonArray variants = serverConfig.value(QStringLiteral("peerVariants")).toArray();
    for (const QJsonValue &value : variants) {
        const QJsonObject variant = value.toObject();
        if (variant.value(QStringLiteral("cardId")).toString().isEmpty()) {
            continue;
        }
        if (canonicalProtocolOf(variant.value(QStringLiteral("protocol")).toString()) == canonicalProtocol) {
            return variant;
        }
    }
    return {};
}

// "<shard>:<pool>" of a peers card id ("<shard>:<pool>:<proto>"): the handle
// `POST /v1/peers/prepare` takes.
QString poolKeyOfCardId(const QString &cardId)
{
    const QString raw = cardId.trimmed();
    const int sep = raw.lastIndexOf(QLatin1Char(':'));
    return sep > 0 ? raw.left(sep) : QString();
}

// Tunnel the row already holds for one of its cards: the per-card cache first
// (`peerVariantConfigs`, where setPeerConfig stores every prepared card), then the
// row's active container when the card is the one it points at.
QJsonObject variantContainerOf(const QJsonObject &serverConfig, const QString &cardId)
{
    if (cardId.isEmpty()) {
        return {};
    }
    const QJsonObject cached = serverConfig.value(QStringLiteral("peerVariantConfigs")).toObject();
    const QJsonObject container = cached.value(cardId).toObject();
    if (!container.isEmpty()) {
        return container;
    }
    if (serverConfig.value(QStringLiteral("peerCardId")).toString() == cardId) {
        const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
        if (!containers.isEmpty()) {
            return containers.at(0).toObject();
        }
    }
    return {};
}

// `last_config` of a container plus the wg-quick INI it was built from: the
// handshake probe needs the key material and the obfuscation of the same config,
// and the config is not always stored under the canonical container name (a
// mobile AWG row keeps it under "awg").
struct ContainerConfig
{
    QJsonObject lastConfig;
    QString ini;
};

ContainerConfig containerConfigOf(const QJsonObject &container)
{
    ContainerConfig out;
    for (auto it = container.constBegin(); it != container.constEnd(); ++it) {
        if (it.key() == QLatin1String("container") || !it.value().isObject()) {
            continue;
        }
        const QJsonObject proto = it.value().toObject();
        const QJsonObject parsed =
                QJsonDocument::fromJson(proto.value(QStringLiteral("last_config")).toString().toUtf8()).object();
        if (parsed.isEmpty()) {
            continue;
        }
        out.lastConfig = parsed;
        out.ini = proto.value(QStringLiteral("config")).toString();
        return out;
    }
    return out;
}

// AWG and WireGuard answer a Noise handshake, so they get the handshake phase.
// Everything else the pool serves (hysteria2, vless, vless_xhttp_cdn, any
// `client_config` card) is a proxy: no handshake exists for it, the download is
// the whole measurement.
bool isProxyProtocol(const QString &protocol, const QJsonObject &variant)
{
    if (variant.value(QStringLiteral("kind")).toString().compare(QLatin1String("client_config"),
                                                               Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QString canonical = canonicalProtocolOf(protocol);
    return canonical != QStringLiteral("awg") && canonical != QStringLiteral("wireguard");
}

// Order of the chooser: the pool's default variant first (AWG), then WireGuard,
// then the proxy protocols.
int protocolRank(const QString &protocol)
{
    const QString canonical = canonicalProtocolOf(protocol);
    if (canonical == QStringLiteral("awg")) {
        return 0;
    }
    if (canonical == QStringLiteral("wireguard")) {
        return 1;
    }
    return 2;
}

} // namespace

ServerTestController::ServerTestController(const QSharedPointer<ServersModel> &serversModel,
                                           std::shared_ptr<Settings> settings, QObject *parent)
    : QObject(parent), m_serversModel(serversModel), m_settings(std::move(settings))
{
    // the bench file is a probe of the node itself; going through a system proxy
    // (macOS PAC/WPAD) would measure the proxy instead, and resolving that proxy
    // is what froze the UI in issue #18
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(200);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ServerTestController::resultsChanged);
    // Bench downloads must describe the node, not whatever proxy the user has
    // configured system-wide.
    m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    m_userAgent = QStringLiteral("Dopamine/%1 server-test").arg(QStringLiteral(APP_VERSION));
    // Compile-time tag: tells us which binary is actually on the device when a
    // string change "does not appear".
    m_buildTag = QStringLiteral(__DATE__ " " __TIME__);

    // Headless pass for verification on a device: DOPAMINE_SERVERTEST=1 runs the
    // pool test as soon as no tunnel is up, and reports through qDebug. The speed
    // pass can be skipped with DOPAMINE_SERVERTEST_NOSPEED=1.
    if (qEnvironmentVariableIsSet("DOPAMINE_SERVERTEST")) {
        const bool withSpeed = !qEnvironmentVariableIsSet("DOPAMINE_SERVERTEST_NOSPEED");
        selftestLog(QStringLiteral("[SERVERTEST] controller ready (build %1), waiting for the tunnel to be off").arg(m_buildTag));
        // Prove what the running binary actually carries: the embedded page source
        // and what the PageServerTest translation context returns for the title.
        {
            QFile page(QStringLiteral(":/ui/qml/Pages2/PageServerTest.qml"));
            if (page.open(QIODevice::ReadOnly)) {
                const QString source = QString::fromUtf8(page.readAll());
                selftestLog(QStringLiteral("[SERVERTEST] embedded qml %1 bytes, hasSpeedTest=%2")
                                    .arg(source.size())
                                    .arg(source.contains(QStringLiteral("Speed Test"))));
            } else {
                selftestLog(QStringLiteral("[SERVERTEST] embedded qml NOT FOUND in resources"));
            }
            selftestLog(QStringLiteral("[SERVERTEST] tr(PageServerTest, 'Speed Test') = '%1'")
                                .arg(QCoreApplication::translate("PageServerTest", "Speed Test")));
        }
        // Verification on a device: the pass otherwise opens on the default pool
        // and its default protocol, so a proxy protocol (no handshake at all)
        // could not be exercised there without a tap in the UI.
        const QString pool = qEnvironmentVariable("DOPAMINE_SERVERTEST_POOL");
        if (!pool.isEmpty()) {
            m_poolFilter = pool;
        }
        const QString protocol = qEnvironmentVariable("DOPAMINE_SERVERTEST_PROTOCOL");
        if (!protocol.isEmpty()) {
            m_protocolFilter = protocol;
        }
        runWhenTunnelOff(withSpeed);
    }
}

void ServerTestController::setPoolFilter(const QString &pool)
{
    if (m_poolFilter == pool) {
        return;
    }
    m_poolFilter = pool;
    reloadTargets();
}

void ServerTestController::setProtocolFilter(const QString &protocol)
{
    if (m_protocolFilter == protocol) {
        return;
    }
    m_protocolFilter = protocol;
    m_provisionTried = false;
    reloadTargets();
}

void ServerTestController::setPeersImportController(PeersImportController *peers)
{
    m_peers = peers;
    if (!m_peers) {
        return;
    }
    connect(m_peers, &PeersImportController::poolProvisioned, this, &ServerTestController::onPoolProvisioned,
            Qt::UniqueConnection);
}

void ServerTestController::selectProtocol(const QString &protocol)
{
    // "Pick a protocol" means "measure this one": the pick narrows the pass, and
    // a protocol the pool serves but has no config for is provisioned first —
    // otherwise the row could only ever be downloaded, never handshaked, and the
    // chooser would be limited to the variants that already had a peer.
    if (protocol.isEmpty()) {
        return;
    }
    const bool wasRunning = m_running;
    if (wasRunning) {
        stop();
    }
    m_protocolFilter = protocol;
    m_provisionTried = false;
    m_targets.clear();
    m_allProtocols.clear();
    collectTargets();
    emit filtersChanged();
    if (requestProvision(wasRunning)) {
        return;
    }
    if (wasRunning) {
        runPoolTest(m_includeSpeed);
    }
}

void ServerTestController::reloadTargets()
{
    if (m_running) {
        stop();
    }
    invalidateProbes();
    m_targets.clear();
    m_allPools.clear();
    m_allProtocols.clear();
    m_provisionTried = false;
    fetchBenchMap([this]() {
        collectTargets();
        emit filtersChanged();
        emit resultsChanged();
    });
}

QString ServerTestController::subscriptionTail(const QString &subscriptionId)
{
    // Same shape the server list prints («Subscription 1a2b3c4d»): the last
    // eight characters of the id, dashes stripped. A full token says nothing
    // and would not fit a row.
    QString cleaned = subscriptionId;
    cleaned.remove(QLatin1Char('-'));
    return cleaned.size() > 8 ? cleaned.right(8) : cleaned;
}

void ServerTestController::fetchBenchMap(std::function<void()> then)
{
    if (m_benchMapFetched) {
        then();
        return;
    }
    QNetworkRequest request{ QUrl(QStringLiteral("https://api.frkn.org/status")) };
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setTransferTimeout(6000);

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, then]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            selftestLog(QStringLiteral("[SERVERTEST] /status failed: %1").arg(reply->errorString()));
            then();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray nodes = root.value(QStringLiteral("response")).toArray();
        for (const QJsonValue &value : nodes) {
            const QJsonObject node = value.toObject();
            const QString address = node.value(QStringLiteral("address")).toString();
            if (address.isEmpty()) {
                continue;
            }
            // /status lists the same address twice: once as a pool node, once as a
            // plain node (with a different, often useless label — an address or a
            // protocol name). Only the pool row describes the node we test, so it
            // wins; the other one only fills gaps.
            const QString nodePool = node.value(QStringLiteral("pool")).toString();
            const bool isPoolRow = !nodePool.isEmpty();
            const bool havePoolRow = m_nodePools.contains(address) && !m_nodePools.value(address).isEmpty();
            if (!isPoolRow && havePoolRow) {
                continue;
            }

            m_benchUrls.insert(address, node.value(QStringLiteral("speedtest_url")).toString());
            m_nodeOnline.insert(address, node.value(QStringLiteral("status")).toString() == QStringLiteral("Online"));
            m_nodePools.insert(address, nodePool);
            const QString nodeLabel = node.value(QStringLiteral("label")).toString();
            if (!nodeLabel.isEmpty()) {
                m_nodeLabels.insert(address, nodeLabel);
            }
            const QString statusHostname = node.value(QStringLiteral("hostname")).toString();
            if (!statusHostname.isEmpty()) {
                m_nodeHostnames.insert(address, statusHostname);
            }
        }
        m_benchMapFetched = true;
        selftestLog(QStringLiteral("[SERVERTEST] /status: %1 nodes, %2 with a bench url")
                            .arg(nodes.size())
                            .arg([this]() {
                                int n = 0;
                                for (const QString &url : m_benchUrls) {
                                    if (!url.isEmpty()) {
                                        ++n;
                                    }
                                }
                                return n;
                            }()));
        then();
    });
}

void ServerTestController::prepareTargets()
{
    // The page can be opened again after the subscription was changed in the
    // servers list: the list built for the previous selection would then still
    // show servers of a subscription the user no longer has in front of him.
    QString current = m_settings ? m_settings->serversSubscriptionFilter() : QString();
    if (current == QStringLiteral("all")) {
        current.clear();
    }
    if (current != m_subscriptionFilter) {
        reloadTargets();
        return;
    }
    if (m_running || !m_targets.isEmpty()) {
        return;
    }
    fetchBenchMap([this]() {
        if (!m_targets.isEmpty()) {
            return;
        }
        collectTargets();
        emit filtersChanged();
        emit resultsChanged();
        // The pool may serve a protocol no card of it was provisioned for yet:
        // prepare it now, so Measure handshakes instead of falling back to a
        // download-only row.
        requestProvision(false);
    });
}

QVariantList ServerTestController::results() const
{
    // One row per node: a pooled node can be listed by several addresses and
    // protocol variants, and showing each as its own "server" multiplied the same
    // machine across the list ("три ноды 177 в EU").
    QVariantList rows;
    QStringList order;
    QHash<QString, QList<int>> byNode;
    // "All subscriptions" is the only case where a row's origin is ambiguous.
    const bool allSubscriptions = m_subscriptionFilter.isEmpty();
    for (int i = 0; i < m_targets.size(); ++i) {
        const QString key = m_targets.at(i).hostname;
        if (!byNode.contains(key)) {
            order.append(key);
        }
        byNode[key].append(i);
    }

    for (const QString &hostname : order) {
        const QList<int> indices = byNode.value(hostname);
        // the address that actually answered best is the one the user cares about
        int best = indices.first();
        for (const int i : indices) {
            const bool measured = m_targets.at(i).medianRtt >= 0;
            const bool bestMeasured = m_targets.at(best).medianRtt >= 0;
            if (measured && (!bestMeasured || m_targets.at(i).medianRtt < m_targets.at(best).medianRtt)) {
                best = i;
            }
        }

        const Target &node = m_targets.at(best);
        QStringList protocols;
        // Two subscriptions can sell the same pool: when the pass covers all of
        // them the row has to name where the server came from, otherwise the
        // list looks like one subscription's servers.
        QStringList subscriptionLabels;
        double mbps = 0.0;
        QString speedErrorCode;
        bool measured = false;
        for (const int i : indices) {
            const Target &target = m_targets.at(i);
            if (!target.protocol.isEmpty() && !protocols.contains(target.protocol)) {
                protocols.append(target.protocol.toUpper());
            }
            if (!target.subscriptionLabel.isEmpty() && !subscriptionLabels.contains(target.subscriptionLabel)) {
                subscriptionLabels.append(target.subscriptionLabel);
            }
            mbps = std::max(mbps, target.mbps);
            if (speedErrorCode.isEmpty() && !target.speedErrorCode.isEmpty()) {
                speedErrorCode = target.speedErrorCode;
            }
            // A download-only row has no handshake by design: its speed sample is
            // the measurement, and leaving it out read as "nothing was tested".
            measured = measured || !target.rtts.isEmpty() || (target.benchOnly && target.mbps > 0.0);
        }
        // Proxy protocol: no handshake exists for it, so the row must not read as
        // "no answer" — the download is the whole verdict and the page says so.
        const bool speedOnly = std::all_of(indices.begin(), indices.end(), [this](int i) {
            return m_targets.at(i).proxyOnly;
        });

        QVariantMap row;
        row[QStringLiteral("pool")] = node.pool;
        row[QStringLiteral("label")] = node.label.isEmpty() ? hostname : node.label;
        row[QStringLiteral("listName")] = node.listName;
        row[QStringLiteral("hostname")] = hostname;
        row[QStringLiteral("address")] = node.address;
        row[QStringLiteral("port")] = node.port;
        row[QStringLiteral("country")] = node.country;
        row[QStringLiteral("protocol")] = protocols.join(QStringLiteral(" · "));
        // "" when the pass was narrowed to one subscription: the page then shows
        // no per-row subscription at all.
        row[QStringLiteral("subscription")] = allSubscriptions ? subscriptionLabels.join(QStringLiteral(" · "))
                                                               : QString();
        row[QStringLiteral("addresses")] = indices.size();
        row[QStringLiteral("rtt")] = node.rtts.isEmpty() ? -2 : node.medianRtt;
        row[QStringLiteral("jitter")] = node.jitterMs;
        row[QStringLiteral("loss")] = node.rtts.size() - node.okSamples;
        row[QStringLiteral("samples")] = node.rtts.size();
        row[QStringLiteral("mbps")] = mbps;
        row[QStringLiteral("speedError")] = node.speedError;
        row[QStringLiteral("speedErrorCode")] = speedErrorCode;
        row[QStringLiteral("speedOnly")] = speedOnly;
        row[QStringLiteral("done")] = measured;
        rows.append(row);
    }
    return rows;
}

int ServerTestController::measuredCount() const
{
    QSet<QString> measured;
    for (const Target &target : m_targets) {
        // a download-only row has no handshake by design: its speed sample is
        // the measurement, and leaving it out read as "nothing was tested"
        if (!target.rtts.isEmpty() || (target.benchOnly && target.mbps > 0.0)) {
            measured.insert(target.hostname);
        }
    }
    return measured.size();
}

int ServerTestController::targetCount() const
{
    QSet<QString> nodes;
    for (const Target &target : m_targets) {
        nodes.insert(target.hostname);
    }
    return nodes.size();
}

void ServerTestController::setTunnelActive(bool active)
{
    m_tunnelActive = active;
    if (!active && m_pendingRun && !m_running) {
        qDebug() << "[SERVERTEST] tunnel off, starting the queued pass";
        m_pendingRun = false;
        runPoolTest(m_pendingSpeed);
        return;
    }
    if (active && m_running) {
        // in-flight probes would be routed into the tunnel and report nonsense
        m_status = QStringLiteral("stoppedTunnel");
        stop();
    }
}

void ServerTestController::runWhenTunnelOff(bool includeSpeed)
{
    m_pendingSpeed = includeSpeed;
    if (!m_tunnelActive) {
        m_pendingRun = false;
        runPoolTest(includeSpeed);
        return;
    }
    // The tunnel is up: remember the request and start the moment it drops.
    m_pendingRun = true;
    m_status = QStringLiteral("waitingForTunnelOff");
    selftestLog(QStringLiteral("[SERVERTEST] queued: waiting for the tunnel to go off"));
    emit resultsChanged();
}

void ServerTestController::runPoolTest(bool includeSpeed)
{
    selftestLog(QStringLiteral("[SERVERTEST] run requested: tunnel=%1 running=%2 sub=[%3] filters=[%4|%5]")
                        .arg(m_tunnelActive)
                        .arg(m_running)
                        .arg(m_subscriptionFilter.isEmpty() ? QStringLiteral("*") : m_subscriptionFilter,
                             m_poolFilter.isEmpty() ? QStringLiteral("*") : m_poolFilter,
                             m_protocolFilter.isEmpty() ? QStringLiteral("*") : m_protocolFilter));
    if (m_running) {
        selftestLog(QStringLiteral("[SERVERTEST] already running"));
        return;
    }
    if (m_tunnelActive) {
        selftestLog(QStringLiteral("[SERVERTEST] skipped: tunnel is active (probes would go through the exit)"));
        return;
    }
    if (m_serversModel.isNull()) {
        return;
    }

    m_targets.clear();
    m_handshakeQueue.clear();
    invalidateProbes();
    m_speedByNode.clear();
    m_handshakeInFlight = 0;
    m_speedIndex = 0;
    m_speedRx = 0;
    m_includeSpeed = includeSpeed;
    // An explicit press may retry a prepare that failed on an earlier attempt; the
    // pass itself asks at most once (see onPoolProvisioned for the failed case).
    m_provisionTried = false;
    m_running = true;
    m_pendingRun = false; // a released pass must not be started twice
    m_status.clear();
    emit runningChanged();
    emit resultsChanged();

    // The bench address comes from the api, so the pass waits for that map before
    // it can decide which rows get a speed sample.
    fetchBenchMap([this, includeSpeed]() {
        if (!m_running) {
            return;
        }
        collectTargets();
        if (m_targets.isEmpty()) {
            selftestLog(QStringLiteral("[SERVERTEST] no pool targets in this subscription"));
            finishPass();
            return;
        }
        // A protocol the pool serves but that has no config yet is prepared first:
        // a node answers a handshake only for a peer it knows, so probing before
        // the prepare would read as "no answer" for a healthy node.
        if (requestProvision(true)) {
            return; // resumes in onPoolProvisioned()
        }
        beginMeasurement(includeSpeed);
    });
}

void ServerTestController::beginMeasurement(bool includeSpeed)
{
    m_status.clear();
    m_handshakeQueue.clear();
    const bool proxy = !m_targets.isEmpty() && m_targets.first().proxyOnly;
    if (proxy) {
        // No handshake exists for a proxy protocol: the download is the whole
        // test, logged once per pass with the protocol the user picked.
        selftestLog(QStringLiteral("[SERVERTEST] %1: proxy — speed only").arg(m_protocolFilter));
    }
    for (int i = 0; i < m_targets.size(); ++i) {
        // A proxy protocol and a pool row without a built tunnel have no key
        // material: there is nothing to handshake with, but the bench file is
        // public — the download pass still runs for them.
        if (m_targets.at(i).benchOnly) {
            selftestLog(QStringLiteral("[SERVERTEST] %1: %2, download only")
                                .arg(m_targets.at(i).label.isEmpty() ? m_targets.at(i).hostname
                                                                     : m_targets.at(i).label,
                                     m_targets.at(i).proxyOnly ? QStringLiteral("proxy")
                                                               : QStringLiteral("no config")));
            continue;
        }
        m_handshakeQueue.append(i);
    }
    selftestLog(QStringLiteral("[SERVERTEST] start: %1 endpoints, protocol %2, speed pass %3")
                        .arg(m_targets.size())
                        .arg(m_protocolFilter)
                        .arg(includeSpeed ? QStringLiteral("on") : QStringLiteral("off")));
    pump();
}

// A pool row tested through a protocol the pool serves but that was never
// provisioned has no key material: the node would drop every handshake. One
// `POST /v1/peers/prepare` for that pool stores a config per protocol card, which
// is exactly what the pass needs — and what makes picking a protocol the row had
// no peer for work at all.
bool ServerTestController::requestProvision(bool thenRun)
{
    if (!m_peers || m_provisionTried || m_provisionPending) {
        return false;
    }
    QString subscriptionId;
    QString poolKey;
    for (const Target &target : m_targets) {
        if (target.needsProvision && !target.provisionSubscription.isEmpty()
            && !target.provisionPoolKey.isEmpty()) {
            subscriptionId = target.provisionSubscription;
            poolKey = target.provisionPoolKey;
            break;
        }
    }
    if (subscriptionId.isEmpty() || poolKey.isEmpty()) {
        return false;
    }
    m_provisionTried = true;
    m_provisionPending = true;
    m_runAfterProvision = thenRun;
    m_status = QStringLiteral("provisioning");
    selftestLog(QStringLiteral("[SERVERTEST] %1: no config for %2 yet, preparing through /v1/peers")
                        .arg(m_protocolFilter, poolKey));
    emit resultsChanged();
    m_peers->provisionPool(subscriptionId, poolKey);
    return true;
}

void ServerTestController::onPoolProvisioned(const QString &subscriptionId, const QString &poolKey, bool ok,
                                             const QString &error)
{
    Q_UNUSED(subscriptionId)
    m_provisionPending = false;
    selftestLog(QStringLiteral("[SERVERTEST] prepare %1: %2")
                        .arg(poolKey, ok ? QStringLiteral("ok") : error));
    const bool thenRun = m_runAfterProvision;
    m_runAfterProvision = false;
    // The configs are stored per card now: rebuild the targets, so the handshake
    // half sees the keys the api just handed out.
    collectTargets();
    emit filtersChanged();
    emit resultsChanged();
    if (!thenRun) {
        if (m_status == QStringLiteral("provisioning")) {
            m_status.clear();
            emit resultsChanged();
        }
        return;
    }
    if (!ok) {
        // The api declined every card of the pool: the rows stay download-only and
        // another press of Measure is what asks again. Retrying here would hammer
        // the api with a request it already refused.
        m_status.clear();
        if (!m_running) {
            emit resultsChanged();
            return;
        }
        if (m_targets.isEmpty()) {
            selftestLog(QStringLiteral("[SERVERTEST] no pool targets in this subscription"));
            finishPass();
            return;
        }
        beginMeasurement(m_includeSpeed);
        return;
    }
    if (m_running) {
        if (m_targets.isEmpty()) {
            selftestLog(QStringLiteral("[SERVERTEST] no pool targets in this subscription"));
            finishPass();
            return;
        }
        beginMeasurement(m_includeSpeed);
        return;
    }
    runPoolTest(m_includeSpeed);
}

void ServerTestController::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    m_benchMapFetched = false;
    m_handshakeQueue.clear();
    invalidateProbes();
    m_handshakeInFlight = 0;
    m_speedIndex = m_targets.size();
    if (m_speedReply) {
        QNetworkReply *reply = m_speedReply;
        m_speedReply = nullptr;
        reply->abort();
    }
    qDebug() << "[SERVERTEST] stopped";
    // A prepare still in flight must not restart the pass the user just stopped.
    m_runAfterProvision = false;
    m_provisionPending = false;
    emit runningChanged();
    emit resultsChanged();
}

void ServerTestController::collectTargets()
{
    // One call does the whole selection: scope -> pools -> protocols of the
    // selected pool -> defaults -> targets. The old split (run the pass, patch
    // the default pool/protocol in between, run it again) left `m_allProtocols`
    // holding the union of every pool's protocols, which is exactly what made the
    // chooser offer protocols the selected pool does not have.
    collectTargetsPass();
    emit resultsChanged();
}

void ServerTestController::collectTargetsPass()
{
    // a pass rebuilds the list from scratch: without this the second pass (the one
    // that runs after the default pool/protocol is picked) appended its rows to the
    // unfiltered first pass and every pool showed up at once
    invalidateProbes();
    m_targets.clear();
    m_allPools.clear();
    m_allProtocols.clear();

    // Scope of the pass: exactly the rows the server list shows for its current
    // subscription selection. "All" (or nothing) keeps every row — they all
    // belong to the account — while a concrete subscription narrows the pass to
    // what the user picked and therefore can actually dial.
    m_subscriptionFilter = m_settings ? m_settings->serversSubscriptionFilter() : QString();
    if (m_subscriptionFilter == QStringLiteral("all")) {
        m_subscriptionFilter.clear();
    }
    const QStringList subscriptions = m_serversModel->availableSubscriptions();
    // Endpoints the user switched off in the server card: the connect path never
    // dials them, so the test must not count them as dead either.
    const QStringList disabledEndpoints = m_settings ? m_settings->poolDisabledEndpoints() : QStringList();

    const int count = m_serversModel->getServersCount();

    QVector<QVariantMap> inScope;
    inScope.reserve(count);
    // Every row dropped from the pass says why: "no targets" on the device then
    // names the rule that removed the last row instead of leaving it to guess.
    const auto skipRow = [](int row, const QString &protocol, const QString &reason) {
        selftestLog(QStringLiteral("[SERVERTEST] skip row %1 (%2): %3")
                            .arg(row)
                            .arg(protocol.isEmpty() ? QStringLiteral("?") : protocol, reason));
    };
    for (int row = 0; row < count; ++row) {
        const QVariantMap display = m_serversModel->getServerDisplayData(row);
        if (display.isEmpty()) {
            continue;
        }
        // Same rule as ServersListView.variantMatches(): the list matches rows by
        // their subscription, and "__other__" is its bucket for rows of none.
        // A pooled row carries its subscription in `peerSubscriptionId`, so both
        // the row scope and every downstream key use the resolved id.
        const QString subscriptionId = rowSubscriptionOf(display);
        if (!m_subscriptionFilter.isEmpty()) {
            if (m_subscriptionFilter == QStringLiteral("__other__")) {
                if (subscriptions.contains(subscriptionId)) {
                    skipRow(row, rowProtocolOf(display), QStringLiteral("not in scope (known subscription)"));
                    continue;
                }
            } else if (subscriptionId != m_subscriptionFilter) {
                skipRow(row, rowProtocolOf(display), QStringLiteral("not in scope (subscription)"));
                continue;
            }
        }
        inScope.append(display);
    }
    selftestLog(QStringLiteral("[SERVERTEST] scope: %1 of %2 rows (subscription %3)")
                        .arg(inScope.size())
                        .arg(count)
                        .arg(m_subscriptionFilter.isEmpty() ? QStringLiteral("all") : subscriptionTail(m_subscriptionFilter)));

    // ── Pools ─────────────────────────────────────────────────────────────
    // A pool is what the page lets the user pick, so the list is built before
    // any protocol decision. Rows without one (premium slots, hand-added
    // configs) have no bench endpoint and are not part of the screen.
    for (const QVariantMap &display : inScope) {
        const QString pool = rowPoolOf(display);
        if (!pool.isEmpty() && !m_allPools.contains(pool)) {
            m_allPools.append(pool);
        }
    }
    QString pool = m_allPools.contains(m_poolFilter) ? m_poolFilter : QString();
    if (pool.isEmpty() && !m_allPools.isEmpty()) {
        // the screen opens on the pool most people use: short name, no spaces
        const QString defaultPool = QStringLiteral("eu");
        pool = m_allPools.contains(defaultPool) ? defaultPool : m_allPools.first();
    }

    // ── Protocols of that pool ────────────────────────────────────────────
    // Every protocol the pool's cards cover, not only the one its row currently
    // dials: a pool that serves WireGuard and hysteria2 is testable over both even
    // when only the AWG card was ever provisioned. The variants are the api's
    // cards for this subscription, so this list is the grant; a protocol that
    // only appears on another pool is never offered here.
    QSet<QString> served;
    // The grant the pool is sold under, resolved per (env, subscription) pair: two
    // contracts can sell a pool of the same name, and a protocol only one of them
    // grants must stay in the list.
    QSet<QString> granted;
    QSet<QString> grantedPairs;
    for (const QVariantMap &display : inScope) {
        if (rowPoolOf(display) != pool) {
            continue;
        }
        const QString rowSubscription = rowSubscriptionOf(display);
        const QString rowEnv = display.value(QStringLiteral("env")).toString();
        const QString pair = rowEnv + QLatin1Char('|') + rowSubscription;
        if (!grantedPairs.contains(pair)) {
            grantedPairs.insert(pair);
            for (const QString &p : m_serversModel->availableProtocolsForEnv(rowEnv, rowSubscription)) {
                granted.insert(p);
            }
        }
        if (!display.value(QStringLiteral("isPeerPool")).toBool()) {
            // a v1 row is one protocol; the raw tag keeps the plain and the
            // mobile AmneziaWG rows apart, as the server list filters them
            const QString raw = rowProtocolOf(display);
            if (!raw.isEmpty()) {
                served.insert(raw);
            }
            continue;
        }
        for (const QVariant &entry : display.value(QStringLiteral("peerVariants")).toList()) {
            const QString variant = entry.toMap().value(QStringLiteral("protocol")).toString();
            if (!variant.isEmpty()) {
                served.insert(variant);
            }
        }
    }
    QStringList protocols;
    for (const QString &candidate : served) {
        // A variant the subscription no longer grants is not testable: the api
        // would refuse the prepare and the row would read as "no answer".
        if (!granted.isEmpty() && !granted.contains(canonicalProtocolOf(candidate))) {
            continue;
        }
        protocols.append(candidate);
    }
    std::stable_sort(protocols.begin(), protocols.end(), [](const QString &a, const QString &b) {
        return protocolRank(a) < protocolRank(b);
    });
    m_allProtocols = protocols;

    const QString protocol = m_allProtocols.contains(m_protocolFilter)
            ? m_protocolFilter
            : (m_allProtocols.isEmpty() ? QString() : m_allProtocols.first());
    const bool selectionChanged = pool != m_poolFilter || protocol != m_protocolFilter;
    m_poolFilter = pool;
    m_protocolFilter = protocol;
    if (selectionChanged) {
        emit filtersChanged();
    }
    if (m_poolFilter.isEmpty() || m_protocolFilter.isEmpty()) {
        emit resultsChanged();
        return;
    }

    const QString selectedCanonical = canonicalProtocolOf(m_protocolFilter);
    // Handshake only for the protocols that have one. Everything else the pool
    // serves is a proxy: the download is the whole measurement, and no handshake
    // verdict is invented for it.
    const bool poolIsProxy =
            selectedCanonical != QStringLiteral("awg") && selectedCanonical != QStringLiteral("wireguard");
    // What the chooser offers and what the pass measures: one line per build, so
    // a device log answers "why is this protocol missing" without a screenshot.
    selftestLog(QStringLiteral("[SERVERTEST] pool %1: protocols [%2], testing %3%4")
                        .arg(pool, m_allProtocols.join(QStringLiteral(", ")), m_protocolFilter,
                             poolIsProxy ? QStringLiteral(" (proxy — speed only)") : QString()));

    QSet<QString> seenEndpoints;
    QHash<QString, int> sameSubnet; // published address -> target index

    for (const QVariantMap &display : inScope) {
        const int row = display.value(QStringLiteral("sourceIndex")).toInt();
        const QString subscriptionId = rowSubscriptionOf(display);
        const QString subscriptionLabel = subscriptionTail(subscriptionId);
        // A peers row is one row per (env, pool): its protocol and endpoints are
        // those of the variant the user picked, so it yields targets like any
        // row — never one per protocol it also serves.
        const bool isPoolRow = display.value(QStringLiteral("isPeerPool")).toBool();

        // A pooled row's protocol is the variant `peerCardId` points at (canonical,
        // as the filter sees it); a v1 row keeps its backend tag.
        const QString rowProtocol = rowProtocolOf(display);
        const QString canonicalProtocol = canonicalProtocolOf(rowProtocol);
        const QString rowPool = rowPoolOf(display);
        if (rowPool.isEmpty()) {
            // A premium or selfhosted card has no pool, but it still has a
            // bench endpoint (the node serves it like any other). Group it by
            // its env, so the picker offers it next to the pools.
            const QString env = display.value(QStringLiteral("env")).toString();
            if (env.isEmpty()) {
                skipRow(row, rowProtocol, QStringLiteral("no pool"));
                continue;
            }
            // A row without a pool is its own group: the env names it.
            const_cast<QString&>(rowPool) = env;
            if (!m_allPools.contains(env)) {
                m_allPools.append(env);
            }
        }
        if (rowPool != m_poolFilter) {
            skipRow(row, rowProtocol, QStringLiteral("not in scope (pool %1)").arg(rowPool));
            continue;
        }
        // A pool row is tested through the selected variant, so its own active
        // protocol must not decide the scope: that is what hid every protocol but
        // the one peer/config the row already carried.
        if (!isPoolRow && canonicalProtocol != selectedCanonical) {
            skipRow(row, rowProtocol, QStringLiteral("not in scope (protocol %1)").arg(rowProtocol));
            continue;
        }

        const QJsonObject serverConfig = m_serversModel->getServerConfig(row);
        const QJsonObject displayInfo = serverConfig.value(QStringLiteral("displayInfo")).toObject();

        // The variant this row is tested through: it carries the endpoints, the
        // card to provision and the kind that decides whether a handshake exists.
        QJsonObject variant;
        QString cardId;
        bool rowProxy = poolIsProxy;
        if (isPoolRow) {
            variant = poolVariantOf(serverConfig, selectedCanonical);
            if (variant.isEmpty()) {
                skipRow(row, rowProtocol, QStringLiteral("pool does not serve %1").arg(m_protocolFilter));
                continue;
            }
            cardId = variant.value(QStringLiteral("cardId")).toString();
            rowProxy = isProxyProtocol(m_protocolFilter, variant);
        }

        // Config of the variant, when one was provisioned. A pooled row has none
        // until the first connect or the prepare the pick triggers, so the
        // resolution runs before the pool branch instead of skipping the row.
        ContainerConfig tunnel;
        if (isPoolRow) {
            tunnel = containerConfigOf(variantContainerOf(serverConfig, cardId));
        } else {
            const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
            if (!containers.isEmpty()) {
                tunnel = containerConfigOf(containers.at(0).toObject());
            }
        }
        const QString clientPrivKey = tunnel.lastConfig.value(QStringLiteral("client_priv_key")).toString();
        const QString serverPubKey = tunnel.lastConfig.value(QStringLiteral("server_pub_key")).toString();

        if (isPoolRow) {
            // A pool row is measurable before the first connect: the bench file
            // is public and needs no tunnel, only the card's endpoints and the
            // node's bench url. Keys exist after the card was provisioned and
            // decide whether there is a handshake sample; without them the row is
            // download-only and asks for a prepare instead of disappearing.
            const QString country = m_serversModel->data(row, ServersModel::Roles::CountryNameRole).toString();
            const QJsonArray nodes = variant.value(QStringLiteral("nodes")).toArray();
            QHash<QString, QJsonObject> nodeByHost;
            QStringList endpoints;
            for (const QJsonValue &value : nodes) {
                const QJsonObject node = value.toObject();
                const QString endpoint = node.value(QStringLiteral("endpoint")).toString();
                const QString host = endpointHostOf(endpoint);
                if (host.isEmpty() || nodeByHost.contains(host)) {
                    continue;
                }
                nodeByHost.insert(host, node);
                endpoints.append(endpoint);
            }
            if (endpoints.isEmpty()) {
                // rows written before `peerNodes` existed state the same list as
                // bare addresses; the port then comes from the row
                for (const QString &ip : NodeEndpoints::ips(serverConfig)) {
                    if (!ip.isEmpty() && !nodeByHost.contains(ip)) {
                        nodeByHost.insert(ip, QJsonObject());
                        endpoints.append(ip);
                    }
                }
            }
            QString rowBench = displayInfo.value(QStringLiteral("speedtest_url")).toString();
            if (rowBench.isEmpty()) {
                rowBench = serverConfig.value(QStringLiteral("speedtest_url")).toString();
            }
            while (rowBench.endsWith(QLatin1Char('/'))) {
                rowBench.chop(1);
            }
            const int rowPort = m_serversModel->serverPort(row);
            // Where the prepare request goes: the row's own pool handle, or the
            // card id without its protocol segment.
            const QString rowPoolKey = serverConfig.value(QStringLiteral("peerPoolKey")).toString().isEmpty()
                    ? poolKeyOfCardId(cardId)
                    : serverConfig.value(QStringLiteral("peerPoolKey")).toString();
            QString firstBench;
            int built = 0;
            for (const QString &endpoint : endpoints) {
                const QString address = endpointHostOf(endpoint);
                if (disabledEndpoints.contains(address)) {
                    selftestLog(QStringLiteral("[SERVERTEST] skip %1: switched off in the card").arg(address));
                    continue;
                }
                int endpointPort = endpointPortOf(endpoint);
                if (endpointPort == 0) {
                    endpointPort = rowPort;
                }
                const QString key = QStringLiteral("%1:%2").arg(address).arg(endpointPort);
                if (seenEndpoints.contains(key)) {
                    continue;
                }
                seenEndpoints.insert(key);

                QString bench = rowBench;
                if (bench.isEmpty()) {
                    bench = m_benchUrls.value(address);
                }
                if (bench.isEmpty()) {
                    const QJsonObject node = nodeByHost.value(address);
                    bench = node.value(QStringLiteral("bench")).toString();
                    if (bench.isEmpty()) {
                        bench = node.value(QStringLiteral("speedtest_url")).toString();
                    }
                }
                while (bench.endsWith(QLatin1Char('/'))) {
                    bench.chop(1);
                }
                if (bench.isEmpty()) {
                    // nothing to download: the row keeps its handshake (if any)
                    selftestLog(QStringLiteral("[SERVERTEST] no bench url for %1").arg(address));
                } else if (firstBench.isEmpty()) {
                    firstBench = bench;
                }
                const bool knownNode = m_nodeHostnames.contains(address);
                const QString nodeHostname = knownNode ? m_nodeHostnames.value(address) : address;
                const QString statusLabel = m_nodeLabels.value(address);
                const QString statusPool = m_nodePools.value(address);
                Target target;
                target.row = row;
                target.pool = pool;
                target.country = country;
                target.protocol = m_protocolFilter;
                target.listName = display.value(QStringLiteral("name")).toString();
                target.subscriptionId = subscriptionId;
                target.subscriptionLabel = subscriptionLabel;
                target.hostname = nodeHostname;
                target.address = address;
                target.port = static_cast<quint16>(endpointPort);
                target.label = (!statusLabel.isEmpty() && statusLabel != statusPool) ? statusLabel : nodeHostname;
                target.benchBase = bench;
                target.onlineKnown = m_nodeOnline.contains(address);
                target.online = m_nodeOnline.value(address, true);
                target.proxyOnly = rowProxy;
                target.clientPrivKey = clientPrivKey;
                target.serverPubKey = serverPubKey;
                target.psk = tunnel.lastConfig.value(QStringLiteral("psk_key")).toString();
                target.junkJson = tunnel.lastConfig.isEmpty()
                        ? QString()
                        : QString::fromUtf8(QJsonDocument(wgJunkParamsFromConfig(tunnel.lastConfig, tunnel.ini))
                                                    .toJson(QJsonDocument::Compact));
                if (rowProxy) {
                    // no handshake exists for a proxy protocol: the download is
                    // the measurement, never a "no answer" verdict
                    target.benchOnly = true;
                    target.needsProvision = false;
                } else {
                    // no keys = this card was never provisioned: download only,
                    // and the pass asks for a prepare first
                    const bool haveKeys = !clientPrivKey.isEmpty() && !serverPubKey.isEmpty();
                    target.benchOnly = !haveKeys;
                    target.needsProvision = !haveKeys;
                    if (!haveKeys) {
                        target.provisionSubscription = subscriptionId;
                        target.provisionPoolKey = rowPoolKey;
                    }
                }
                m_targets.append(target);
                ++built;
            }
            selftestLog(QStringLiteral("[SERVERTEST] pool %1: %2 endpoints, bench %3")
                                .arg(pool)
                                .arg(built)
                                .arg(firstBench.isEmpty() ? QStringLiteral("none") : firstBench));
            if (built == 0) {
                skipRow(row, rowProtocol, QStringLiteral("no endpoint"));
            }
            continue;
        }

        // ── v1 config row ─────────────────────────────────────────────────
        const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
        const bool haveConfig = !tunnel.lastConfig.isEmpty();
        if ((!haveConfig || containers.isEmpty()) && !poolIsProxy) {
            // no dialable config on this row
            skipRow(row, rowProtocol, QStringLiteral("no config"));
            continue;
        }
        if (!poolIsProxy && (clientPrivKey.isEmpty() || serverPubKey.isEmpty())) {
            skipRow(row, rowProtocol, QStringLiteral("no keys"));
            continue;
        }

        QString hostname = serverConfig.value(QStringLiteral("hostName")).toString();
        if (hostname.isEmpty()) {
            hostname = tunnel.lastConfig.value(QStringLiteral("hostName")).toString();
        }
        if (hostname.isEmpty()) {
            skipRow(row, rowProtocol, QStringLiteral("no endpoint"));
            continue;
        }

        const QJsonValue portValue = tunnel.lastConfig.value(QStringLiteral("port"));
        int port = portValue.isString() ? portValue.toString().toInt() : portValue.toInt();
        if (port == 0) {
            port = m_serversModel->serverPort(row);
        }
        if (port == 0) {
            skipRow(row, rowProtocol, QStringLiteral("no port"));
            continue;
        }

        const QString junkJson = haveConfig
                ? QString::fromUtf8(QJsonDocument(wgJunkParamsFromConfig(tunnel.lastConfig, tunnel.ini))
                                            .toJson(QJsonDocument::Compact))
                : QString();

        // Bench origin comes from the api (/status -> speedtest_url) or from the
        // subscription config once /v1/services carries it. The old
        // "<node label>.bench.beacon.frkn.org" scheme is dead (NXDOMAIN), so no
        // local derivation is attempted: no URL means "no test file".
        QString benchBase = displayInfo.value(QStringLiteral("speedtest_url")).toString();
        if (benchBase.isEmpty()) {
            benchBase = serverConfig.value(QStringLiteral("speedtest_url")).toString();
        }
        while (benchBase.endsWith(QLatin1Char('/'))) {
            benchBase.chop(1);
        }
        const QString psk = tunnel.lastConfig.value(QStringLiteral("psk_key")).toString();
        const QString country = m_serversModel->data(row, ServersModel::Roles::CountryNameRole).toString();
        QString label = displayInfo.value(QStringLiteral("connectionLabel")).toString();
        const int labelSep = label.indexOf(QStringLiteral(" · "));
        if (labelSep > 0) {
            label = label.left(labelSep);
        }
        label = label.trimmed();
        if (label == rowPool) {
            label.clear();
        }
        // a dotted quad is not a name: the api puts an address into
        // connection_label for pooled rows
        if (!label.isEmpty() && label.at(0).isDigit() && label.contains(QLatin1Char('.'))) {
            label.clear();
        }
        if (label.isEmpty()) {
            label = hostname;
        }

        // `endpoints[]` when the payload has it, node_ips otherwise.
        QStringList addresses = NodeEndpoints::ips(serverConfig);
        addresses.removeDuplicates();
        if (addresses.isEmpty()) {
            addresses.append(hostname);
        }
        // An address the user switched off is not part of what he has: the card
        // never dials it, so it is not probed and not reported as dead.
        for (int i = addresses.size() - 1; i >= 0; --i) {
            if (disabledEndpoints.contains(addresses.at(i))) {
                selftestLog(QStringLiteral("[SERVERTEST] skip %1: switched off in the card").arg(addresses.at(i)));
                addresses.removeAt(i);
            }
        }
        if (addresses.isEmpty()) {
            skipRow(row, rowProtocol, QStringLiteral("no address"));
            continue;
        }

        // /status knows the fleet as nodes: one hostname and one address each, with
        // the display name the status page shows. The subscription, by contrast,
        // puts the addresses of SEVERAL nodes into a single pool row, and its own
        // labels are the pool name or a bare address. So the row unit here is the
        // /status node, matched by address: one line per node, named like the page.
        for (const QString &address : addresses) {
            // The api lists one address per node; the subscription often carries
            // more (extra ips of the same box). Such an address has no name and no
            // bench, and turning it into a row would put bare ips back on screen —
            // the list is names only, so it is skipped.
            const bool knownNode = m_nodeHostnames.contains(address);
            if (!knownNode) {
                selftestLog(QStringLiteral("[SERVERTEST] skip %1: no /status node").arg(address));
                continue;
            }
            const quint16 hostPort = static_cast<quint16>(ServersModel::portForNodeIp(serverConfig, address, port));
            const QString key = QStringLiteral("%1:%2").arg(address).arg(hostPort);
            if (seenEndpoints.contains(key)) {
                continue;
            }
            seenEndpoints.insert(key);

            const QString nodeHostname = m_nodeHostnames.value(address);
            const QString statusLabel = m_nodeLabels.value(address);
            const QString statusPool = m_nodePools.value(address);

            Target target;
            target.row = row;
            target.pool = pool;
            target.country = country;
            target.protocol = rowProtocol;
            target.listName = display.value(QStringLiteral("name")).toString();
            target.subscriptionId = subscriptionId;
            target.subscriptionLabel = subscriptionLabel;
            target.hostname = nodeHostname;
            target.address = address;
            target.port = hostPort;
            target.label = (!statusLabel.isEmpty() && statusLabel != statusPool) ? statusLabel : nodeHostname;
            QString perAddress = knownNode ? benchBase : QString();
            if (perAddress.isEmpty()) {
                perAddress = m_benchUrls.value(address);
            }
            while (perAddress.endsWith(QLatin1Char('/'))) {
                perAddress.chop(1);
            }
            target.benchBase = perAddress;
            target.onlineKnown = m_nodeOnline.contains(address);
            target.online = m_nodeOnline.value(address, true);
            // the handshake needs the key material of the very row that owns the
            // node: without it every probe is dropped by the server ("no answer")
            target.clientPrivKey = clientPrivKey;
            target.serverPubKey = serverPubKey;
            target.psk = psk;
            target.junkJson = junkJson;
            target.proxyOnly = poolIsProxy;
            target.benchOnly = poolIsProxy;
            m_targets.append(target);
            sameSubnet.insert(address, m_targets.size() - 1);
        }

        // A node may be published with an ip that no longer answers while the row
        // still carries a live sibling in the same /24 (EE .48/.49 and PL .155/.156
        // are exactly that). The sibling becomes a fallback of that node instead of
        // a nameless row of its own.
        for (const QString &address : addresses) {
            if (m_nodeHostnames.contains(address) || disabledEndpoints.contains(address)) {
                continue;
            }
            const QString prefix = address.left(address.lastIndexOf(QLatin1Char('.')));
            for (auto it = sameSubnet.constBegin(); it != sameSubnet.constEnd(); ++it) {
                if (it.key().left(it.key().lastIndexOf(QLatin1Char('.'))) == prefix) {
                    Target &node = m_targets[it.value()];
                    // a pool row can carry a dozen addresses of the same /24 (premium
                    // ips and old ones included): only the nearest two are worth a try,
                    // otherwise one dead node costs the whole run's time budget
                    const int delta = std::abs(address.section(QLatin1Char('.'), -1).toInt()
                                               - it.key().section(QLatin1Char('.'), -1).toInt());
                    node.fallbackAddresses.append(QStringLiteral("%1|%2").arg(delta, 3, 10, QLatin1Char('0')).arg(address));
                    break;
                }
            }
        }
    }

    // keep only the two closest siblings per node, peeled of the sort prefix
    for (Target &target : m_targets) {
        std::sort(target.fallbackAddresses.begin(), target.fallbackAddresses.end());
        while (target.fallbackAddresses.size() > 2) {
            target.fallbackAddresses.removeLast();
        }
        for (QString &entry : target.fallbackAddresses) {
            const QString address = entry.section(QLatin1Char('|'), 1);
            selftestLog(QStringLiteral("[SERVERTEST] fallback %1 for node %2").arg(address, target.address));
            entry = address;
        }
    }

    emit resultsChanged();
}

void ServerTestController::pump()
{
    if (!m_running) {
        return;
    }
    if (m_handshakeInFlight > 0 || !m_handshakeQueue.isEmpty()) {
        startHandshakes();
        return;
    }
    if (m_includeSpeed && m_speedIndex < m_targets.size()) {
        measureNextSpeed();
        return;
    }
    finishPass();
}

void ServerTestController::startHandshakes()
{
    for (const Target &target : m_targets) {
        if (target.benchOnly) {
            continue; // download-only row: no key material by design
        }
        if (target.address.isEmpty() || target.port == 0) {
            selftestLog(QStringLiteral("[SERVERTEST] WARN target without address: %1 port=%2")
                                .arg(target.hostname)
                                .arg(target.port));
            break;
        }
        if (target.clientPrivKey.isEmpty() || target.serverPubKey.isEmpty()) {
            selftestLog(QStringLiteral("[SERVERTEST] WARN target without keys: %1").arg(target.hostname));
            break;
        }
    }
    while (m_handshakeInFlight < kMaxParallelEndpoints && !m_handshakeQueue.isEmpty()) {
        const int index = m_handshakeQueue.takeFirst();
        const Target target = m_targets.at(index);
        ++m_handshakeInFlight;

        const quint64 token = ++m_probeSeq;
        m_probeToken[index] = token;
        auto *watcher = new QFutureWatcher<ProbeResult>(this);
        connect(watcher, &QFutureWatcher<ProbeResult>::finished, this, [this, watcher, index, token]() {
            watcher->deleteLater();
            if (index < 0 || index >= m_targets.size() || m_probeToken.value(index) != token) {
                return; // the watchdog already moved the run past this node, or the list was rebuilt
            }
            m_probeToken[index] = 0;
            const ProbeResult result = watcher->result();
            --m_handshakeInFlight;
            onHandshakeFinished(index, result);
            pump();
        });
        // a socket that neither answers nor errors would keep the spinner forever
        QTimer::singleShot(kHandshakeWatchdogMs, this, [this, index, token]() {
            // m_targets is addressed by position: a rebuild (reloadTargets, a new
            // pass) empties it while the timer is still pending, and indexing a
            // short list here wrote past it into freed heap — the next QList<int>
            // growth on that garbage header aborted in qBadAlloc
            if (index < 0 || index >= m_targets.size() || m_probeToken.value(index) != token) {
                return;
            }
            m_probeToken[index] = 0;
            Target &stuck = m_targets[index];
            selftestLog(QStringLiteral("[SERVERTEST] watchdog: %1 (%2:%3) silent for %4 ms, counted as no answer")
                                .arg(stuck.label)
                                .arg(stuck.address)
                                .arg(stuck.port)
                                .arg(kHandshakeWatchdogMs));
            --m_handshakeInFlight;
            onHandshakeFinished(index, ProbeResult{ stuck.address, QList<int>() });
            pump();
        });
        selftestLog(QStringLiteral("[SERVERTEST] probe %1:%2%3")
                            .arg(target.address)
                            .arg(target.port)
                            .arg(target.fallbackAddresses.isEmpty()
                                 ? QString()
                                 : QStringLiteral(" (+%1 fallback)").arg(target.fallbackAddresses.size())));

        watcher->setFuture(QtConcurrent::run([target]() -> ProbeResult {
            QStringList candidates = QStringList() << target.address;
            candidates.append(target.fallbackAddresses);
            const QJsonObject junk = QJsonDocument::fromJson(target.junkJson.toUtf8()).object();

            QList<int> lastSamples;
            for (int candidate = 0; candidate < candidates.size(); ++candidate) {
                const QString address = candidates.at(candidate);
                // the published address gets the full three tries; a sibling gets
                // one short one, so a dead node cannot eat the run's time budget
                const int tries = candidate == 0 ? kSamplesPerEndpoint : 1;
                const int timeout = candidate == 0 ? kHandshakeTimeoutMs : kFallbackTimeoutMs;
                QList<int> samples;
                int timeouts = 0;
                for (int sample = 0; sample < tries; ++sample) {
                    if (sample > 0) {
                        QThread::msleep(kSampleGapMs);
                    }
                    const int rtt = wgProbeHandshakeRTT(address, target.port, target.clientPrivKey,
                                                        target.serverPubKey, target.psk, junk, timeout);
                    samples.append(rtt);
                    timeouts = rtt < 0 ? timeouts + 1 : 0;
                    if (timeouts >= kTimeoutsToGiveUp) {
                        break;
                    }
                }
                lastSamples = samples;
                for (const int rtt : samples) {
                    if (rtt >= 0) {
                        return ProbeResult{address, samples};
                    }
                }
            }
            return ProbeResult{target.address, lastSamples};
        }));
    }
}

void ServerTestController::onHandshakeFinished(int index, const ProbeResult &result)
{
    if (index < 0 || index >= m_targets.size()) {
        return; // the pass was rebuilt or stopped while this probe was in flight
    }
    Target &target = m_targets[index];
    const QList<int> &rtts = result.rtts;
    if (!result.address.isEmpty() && result.address != target.address) {
        // the published ip was dead, a sibling answered: the row keeps the api
        // name and shows the numbers of the address that actually works
        selftestLog(QStringLiteral("[SERVERTEST] fallback %1 -> %2").arg(target.address, result.address));
        target.address = result.address;
    }
    target.rtts = rtts;
    target.okSamples = 0;
    int best = -1;
    int worst = -1;
    for (const int rtt : rtts) {
        if (rtt < 0) {
            continue;
        }
        ++target.okSamples;
        best = (best < 0 || rtt < best) ? rtt : best;
        worst = std::max(worst, rtt);
    }
    int sumRtt = 0;
    int okCount = 0;
    for (const int rtt : rtts) {
        if (rtt >= 0) {
            sumRtt += rtt;
            ++okCount;
        }
    }
    target.medianRtt = okCount > 0 ? sumRtt / okCount : -1; // average of the answers
    scheduleRefresh();
    // spread of the answers, not of the misses: a timeout is not jitter
    target.jitterMs = (best >= 0 && worst >= best) ? worst - best : 0;

    QStringList samples;
    for (const int rtt : rtts) {
        samples.append(rtt < 0 ? QStringLiteral("x") : QString::number(rtt));
    }
    selftestLog(QStringLiteral("[SERVERTEST] hs %1:%2 %3 samples=[%4] med=%5 jit=%6 loss=%7/%8")
                         .arg(target.address)
                         .arg(target.port)
                         .arg(target.label)
                         .arg(samples.join(QLatin1Char(',')))
                         .arg(target.medianRtt)
                         .arg(target.jitterMs)
                         .arg(target.rtts.size() - target.okSamples)
                         .arg(target.rtts.size()));
}

void ServerTestController::invalidateProbes()
{
    // A handshake is addressed by its position in m_targets, and both the
    // finished handler and the watchdog fire later — after the list may have
    // been emptied by a stop or a new pass. Bumping the token makes every
    // callback of the old list bail out instead of indexing past its end.
    ++m_probeSeq;
    m_probeToken.clear();
}

void ServerTestController::measureNextSpeed()
{
    while (m_speedIndex < m_targets.size()) {
        Target &target = m_targets[m_speedIndex];
        // the api said the node is down: measuring would only burn the timeout
        if (target.onlineKnown && !target.online) {
            target.speedErrorCode = QStringLiteral("offline");
            ++m_speedIndex;
            continue;
        }
        if (target.benchBase.isEmpty()) {
            target.speedErrorCode = QStringLiteral("no_test");
            selftestLog(QStringLiteral("[SERVERTEST] skip row %1 (%2): no bench url for %3")
                                .arg(target.row)
                                .arg(target.protocol, target.address));
            ++m_speedIndex;
            continue;
        }
        // only nodes that answered a handshake get a throughput sample: on a dead
        // path the download would spend its whole timeout to say nothing new.
        // A download-only row has no handshake to wait for — its bench file is
        // public, so the sample is the only measurement it can produce.
        if ((target.okSamples > 0 || target.benchOnly) && !target.hostname.isEmpty()) {
            const QString node = benchLabel(target.hostname);
            const auto known = m_speedByNode.constFind(node);
            if (known != m_speedByNode.constEnd()) {
                // the same node already measured through another address
                target.mbps = known->mbps;
                target.speedBytes = known->bytes;
                target.speedMs = known->ms;
                target.speedError = known->error;
                ++m_speedIndex;
                continue;
            }
            m_speedStage = SpeedWarm;
            m_speedByIp = false;
            target.speedRound = 0;
            target.speedSamples.clear();
            requestBench(m_speedIndex, kBenchPingPath, kBenchPingTimeoutMs);
            return;
        }
        if (target.okSamples > 0) {
            target.speedError = QStringLiteral("no node hostname");
        }
        ++m_speedIndex;
    }
    finishPass();
}

void ServerTestController::requestBench(int index, const char *path, int timeoutMs)
{
    const Target &target = m_targets.at(index);
    QString base = target.benchBase;
    if (m_speedByIp && !target.address.isEmpty()) {
        // the bench hostname resolved to the address that no longer answers, so the
        // sample goes straight to the ip that passed the handshake
        const QUrl benchUrl(base);
        base = QStringLiteral("https://%1:%2").arg(target.address).arg(benchUrl.port(19443));
    }
    const QString url = QStringLiteral("%1%2?cb=%3")
                                .arg(base, QString::fromLatin1(path))
                                .arg(QDateTime::currentMSecsSinceEpoch());

    QNetworkRequest request{ QUrl(url) };
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setTransferTimeout(timeoutMs);
    if (m_speedByIp) {
        // an ip is not what the certificate is issued for; the file is a public
        // speed sample, nothing is authenticated by it
        QSslConfiguration ssl = request.sslConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        request.setSslConfiguration(ssl);
    }

    m_speedRx = 0;
    m_speedReply = m_network.get(request);
    if (!m_speedTimer.isValid() || m_speedStage != SpeedWarm) {
        m_speedTimer.start();
    }

    // count bytes as they arrive: a 10 MB body does not need to be held in RAM
    connect(m_speedReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_speedReply) {
            m_speedRx += m_speedReply->readAll().size();
        }
    });
    connect(m_speedReply, &QNetworkReply::finished, this, [this]() { onBenchFinished(); });
}

void ServerTestController::onBenchFinished()
{
    QNetworkReply *reply = m_speedReply;
    if (!reply) {
        return;
    }
    m_speedReply = nullptr;
    if (!m_running || m_speedIndex >= m_targets.size()) {
        // stopped mid-flight: the reply is aborted, there is nothing to record
        reply->deleteLater();
        return;
    }
    m_speedRx += reply->readAll().size(); // tail bytes, if any are still buffered
    const qint64 elapsedMs = m_speedTimer.isValid() ? m_speedTimer.elapsed() : -1;
    const bool ok = reply->error() == QNetworkReply::NoError;
    const QString errorString = ok ? QString() : reply->errorString();
    const qint64 received = m_speedRx;
    reply->deleteLater();

    Target &target = m_targets[m_speedIndex];
    const QString shortName = benchLabel(target.hostname);

    if (!ok && !m_speedByIp && !target.address.isEmpty()) {
        // the bench is reached by name and the name may still point at the dead ip
        const char *path = m_speedStage == SpeedWarm ? kBenchPingPath
                : (m_speedStage == SpeedBig ? kBenchBigPath : kBenchSmallPath);
        const int timeout = m_speedStage == SpeedWarm ? kBenchPingTimeoutMs
                : (m_speedStage == SpeedBig ? kBenchBigTimeoutMs : kBenchSmallTimeoutMs);
        selftestLog(QStringLiteral("[SERVERTEST] speed %1: %2, retrying by ip %3")
                            .arg(shortName, errorString, target.address));
        m_speedByIp = true;
        requestBench(m_speedIndex, path, timeout);
        return;
    }

    if (!ok) {
        if (m_speedStage == SpeedBig && target.mbps > 0.0) {
            // keep the 1 MB number rather than losing the node entirely
            selftestLog(QStringLiteral("[SERVERTEST] speed %1 10MB failed (%2), keeping 1MB sample")
                                .arg(shortName, errorString));
            advanceSpeedPass();
            return;
        }
        target.speedError = errorString;
        selftestLog(QStringLiteral("[SERVERTEST] speed %1 failed: %2").arg(shortName, errorString));
        advanceSpeedPass();
        return;
    }

    if (m_speedStage == SpeedWarm) {
        // DNS + TCP + TLS happened here, so the timed request below reuses the
        // connection instead of spending 100+ ms of its window on a handshake.
        // The stage must move first: requestBench restarts the clock for every
        // stage except the warm-up.
        m_speedStage = SpeedSmall;
        requestBench(m_speedIndex, kBenchSmallPath, kBenchSmallTimeoutMs);
        return;
    }

    if (elapsedMs <= 0) {
        target.speedError = QStringLiteral("bad timing");
        advanceSpeedPass();
        return;
    }

    const double sample = static_cast<double>(received) * 8.0 / (static_cast<double>(elapsedMs) * 1000.0);
    target.speedSamples.append(sample);
    double sum = 0.0;
    for (const double value : target.speedSamples) {
        sum += value;
    }
    target.mbps = sum / static_cast<double>(target.speedSamples.size());
    target.speedBytes = received;
    target.speedMs = elapsedMs;
    emit resultsChanged();

    if (m_speedStage == SpeedSmall && target.speedRound == 0 && elapsedMs < kBigFileThresholdMs) {
        m_speedStage = SpeedBig;
        requestBench(m_speedIndex, kBenchBigPath, kBenchBigTimeoutMs);
        return;
    }
    if (target.speedRound + 1 < kSpeedRounds) {
        ++target.speedRound;
        m_speedStage = SpeedWarm;
        m_speedByIp = false;
        requestBench(m_speedIndex, kBenchPingPath, kBenchPingTimeoutMs);
        return;
    }
    advanceSpeedPass();
}

void ServerTestController::advanceSpeedPass()
{
    if (m_speedIndex < m_targets.size()) {
        const Target &target = m_targets.at(m_speedIndex);
        if (!target.hostname.isEmpty() && (target.mbps > 0.0 || !target.speedError.isEmpty())) {
            SpeedResult result;
            result.mbps = target.mbps;
            result.bytes = target.speedBytes;
            result.ms = target.speedMs;
            result.error = target.speedError;
            m_speedByNode.insert(benchLabel(target.hostname), result);
        }
    }
    ++m_speedIndex;
    pump();
}

void ServerTestController::finishPass()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    m_benchMapFetched = false;
    if (m_status.isEmpty()) {
        m_status = QStringLiteral("done");
    }
    emit runningChanged();
    emit resultsChanged();
    const QString text = report();
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        selftestLog(line);
    }
    emit finished(text);
}

QString ServerTestController::report() const
{
    QStringList lines;
    lines << QStringLiteral("[SERVERTEST] --- results ---");

    QMap<QString, QList<const Target *>> byPool;
    // With every subscription in one pass, the same pool name can come from two
    // contracts: the section title says which one, so the report cannot be read
    // as one subscription's servers.
    const bool allSubscriptions = m_subscriptionFilter.isEmpty();
    for (const Target &target : m_targets) {
        const QString key = (allSubscriptions && !target.subscriptionLabel.isEmpty())
                ? target.subscriptionLabel + QStringLiteral(" · ") + target.pool
                : target.pool;
        byPool[key].append(&target);
    }

    for (auto it = byPool.constBegin(); it != byPool.constEnd(); ++it) {
        for (const Target *target : it.value()) {
            const QString benchOrigin = target->benchBase;
            QString handshake;
            if (target->proxyOnly) {
                // No handshake exists for a proxy protocol: the download below is
                // the whole measurement, and the report says so instead of a fake
                // "no answer".
                handshake = QStringLiteral("hs skipped (proxy: speed only)");
            } else if (target->benchOnly) {
                handshake = QStringLiteral("hs skipped (no config)");
            } else if (target->medianRtt < 0) {
                handshake = QStringLiteral("hs no answer (0/%1)").arg(kSamplesPerEndpoint);
            } else {
                handshake = QStringLiteral("hs %1 ms (jit %2, loss %3/%4)")
                                    .arg(target->medianRtt)
                                    .arg(target->jitterMs)
                                    .arg(kSamplesPerEndpoint - target->okSamples)
                                    .arg(kSamplesPerEndpoint);
            }

            QString speed;
            if (target->mbps > 0.0) {
                speed = QStringLiteral("down %1 Mbps (%2 MB, %3 s)")
                                .arg(target->mbps, 0, 'f', 1)
                                .arg(target->speedBytes / (1024.0 * 1024.0), 0, 'f', 0)
                                .arg(target->speedMs / 1000.0, 0, 'f', 2);
            } else if (!target->speedError.isEmpty()) {
                speed = QStringLiteral("down: %1").arg(target->speedError);
            } else {
                speed = QStringLiteral("down: skipped");
            }

            lines << QStringLiteral("[SERVERTEST] %1 · %2 · %3:%4 · %5 · %6 · bench %7")
                             .arg(it.key(),
                                  target->label.isEmpty() ? target->hostname : target->label,
                                  target->address,
                                  QString::number(target->port),
                                  handshake,
                                  speed,
                                  benchOrigin);
        }

        QList<int> rtts;
        QList<double> speeds;
        int reachable = 0;
        bool proxyPool = true;
        for (const Target *target : it.value()) {
            proxyPool = proxyPool && target->proxyOnly;
            if (target->medianRtt >= 0) {
                ++reachable;
                rtts.append(target->medianRtt);
            }
            if (target->mbps > 0.0) {
                speeds.append(target->mbps);
            }
        }
        const QString medianHs = rtts.isEmpty() ? QStringLiteral("n/a")
                                                : QString::number(medianOf(rtts));
        const QString medianDown = speeds.isEmpty() ? QStringLiteral("n/a")
                                                    : QString::number(medianOf(speeds), 'f', 1);
        if (proxyPool) {
            // A proxy pool has no handshake at all: "reachable 0/N" would read as
            // a pool where every node is dead.
            lines << QStringLiteral("[SERVERTEST] pool %1: proxy protocol, handshake n/a, median down %2 Mbps "
                                    "(speed measured %3/%4)")
                             .arg(it.key())
                             .arg(medianDown)
                             .arg(speeds.size())
                             .arg(it.value().size());
            continue;
        }
        lines << QStringLiteral("[SERVERTEST] pool %1: reachable %2/%3, median hs %4 ms, median down %5 Mbps "
                                "(speed measured %6/%3)")
                         .arg(it.key())
                         .arg(reachable)
                         .arg(it.value().size())
                         .arg(medianHs)
                         .arg(medianDown)
                         .arg(speeds.size());
    }

    return lines.join(QLatin1Char('\n'));
}

QString ServerTestController::reportJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("app"), QStringLiteral("Dopamine"));
    root.insert(QStringLiteral("build"), buildTag());
    root.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QJsonObject filters;
    filters.insert(QStringLiteral("pool"), m_poolFilter);
    filters.insert(QStringLiteral("protocol"), m_protocolFilter);
    // "" means every subscription of the account; otherwise the pass was limited
    // to the one selected in the servers list.
    filters.insert(QStringLiteral("subscription"), m_subscriptionFilter);
    root.insert(QStringLiteral("filters"), filters);
    root.insert(QStringLiteral("targets"), m_targets.size());
    root.insert(QStringLiteral("measured"), measuredCount());

    QJsonArray nodes;
    for (const Target &target : m_targets) {
        QJsonObject node;
        node.insert(QStringLiteral("pool"), target.pool);
        node.insert(QStringLiteral("name"), target.label);
        node.insert(QStringLiteral("hostname"), target.hostname);
        node.insert(QStringLiteral("address"), target.address);
        node.insert(QStringLiteral("port"), target.port);
        node.insert(QStringLiteral("protocol"), target.protocol);
        if (!target.subscriptionId.isEmpty()) {
            node.insert(QStringLiteral("subscription"), target.subscriptionId);
        }
        node.insert(QStringLiteral("status"), !target.onlineKnown ? QStringLiteral("unknown")
                                                                 : (target.online ? QStringLiteral("Online")
                                                                                  : QStringLiteral("Offline")));
        // A proxy protocol has no handshake by design: `handshakeMs` is -1 for it
        // and this flag says why, so the report cannot be read as "every node
        // silent".
        node.insert(QStringLiteral("proxyOnly"), target.proxyOnly);
        node.insert(QStringLiteral("handshakeMs"), target.medianRtt);
        node.insert(QStringLiteral("jitterMs"), target.jitterMs);
        node.insert(QStringLiteral("loss"),
                    QStringLiteral("%1/%2").arg(target.rtts.size() - target.okSamples).arg(target.rtts.size()));
        if (target.speedMs > 0 || target.speedBytes > 0) {
            node.insert(QStringLiteral("mbps"), QString::number(target.mbps, 'f', 2).toDouble());
            node.insert(QStringLiteral("bytes"), target.speedBytes);
            node.insert(QStringLiteral("ms"), target.speedMs);
        }
        if (!target.speedErrorCode.isEmpty()) {
            node.insert(QStringLiteral("speedError"), target.speedErrorCode);
        }
        node.insert(QStringLiteral("bench"), target.benchBase);
        nodes.append(node);
    }
    root.insert(QStringLiteral("nodes"), nodes);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ServerTestController::scheduleRefresh()
{
    // the whole model is rebuilt on resultsChanged; doing it once per finished
    // handshake (five in flight) made the list jump while the user scrolled
    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    }
}
