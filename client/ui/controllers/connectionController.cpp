#include "connectionController.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(MACOS_NE)
    #include <QGuiApplication>
#else
    #include <QApplication>
#endif

#include "utilities.h"
#include "core/controllers/vpnConfigurationController.h"
#include "ui/controllers/api/apiConfigsController.h"
#include "healthCheckController.h"
#include "version.h"

namespace {

// multi-IP failover: rewrite the entry address everywhere it is embedded in a
// server/container config — top-level hostName, INI "Endpoint =", xray outbounds,
// hysteria-style "server" — keeping every other setting untouched

void patchAddressInJsonObject(QJsonObject &obj, const QString &ip);

QString patchAddressInConfigString(const QString &configStr, const QString &ip)
{
    if (configStr.contains(QStringLiteral("[Interface]"))) {
        // wg-quick/AWG INI: rewrite only the host part of "Endpoint = host:port"
        static const QRegularExpression endpointRe(QStringLiteral("(?m)^(\\s*Endpoint\\s*=\\s*)[^:\\s]+(:\\d+\\s*)$"));
        return QString(configStr).replace(endpointRe, QStringLiteral("\\1") + ip + QStringLiteral("\\2"));
    }
    QJsonObject obj = QJsonDocument::fromJson(configStr.toUtf8()).object();
    if (obj.isEmpty()) {
        return configStr;
    }
    patchAddressInJsonObject(obj, ip);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void patchAddressInJsonObject(QJsonObject &obj, const QString &ip)
{
    if (obj.contains(QStringLiteral("hostName"))) {
        obj[QStringLiteral("hostName")] = ip;
    }
    if (obj.contains(QStringLiteral("server"))) {
        // hysteria-style "host:port"
        const QString server = obj.value(QStringLiteral("server")).toString();
        const int colon = server.lastIndexOf(QLatin1Char(':'));
        obj[QStringLiteral("server")] = colon > 0 ? ip + server.mid(colon) : ip;
    }
    QJsonArray outbounds = obj.value(QStringLiteral("outbounds")).toArray();
    for (int i = 0; i < outbounds.size(); ++i) {
        QJsonObject outbound = outbounds.at(i).toObject();
        QJsonObject settingsObj = outbound.value(QStringLiteral("settings")).toObject();
        QJsonArray vnext = settingsObj.value(QStringLiteral("vnext")).toArray();
        for (int j = 0; j < vnext.size(); ++j) {
            QJsonObject v = vnext.at(j).toObject();
            if (v.contains(QStringLiteral("address"))) {
                v[QStringLiteral("address")] = ip;
            }
            vnext.replace(j, v);
        }
        if (!vnext.isEmpty()) {
            settingsObj[QStringLiteral("vnext")] = vnext;
            outbound[QStringLiteral("settings")] = settingsObj;
            outbounds.replace(i, outbound);
        }
    }
    if (!outbounds.isEmpty()) {
        obj[QStringLiteral("outbounds")] = outbounds;
    }
    // nested config string (e.g. the INI persisted inside last_config)
    const QString nestedConfig = obj.value(QStringLiteral("config")).toString();
    if (!nestedConfig.isEmpty()) {
        obj[QStringLiteral("config")] = patchAddressInConfigString(nestedConfig, ip);
    }
}

void patchProtocolObject(QJsonObject &protocolObject, const QString &ip)
{
    if (protocolObject.contains(QStringLiteral("hostName"))) {
        protocolObject[QStringLiteral("hostName")] = ip;
    }
    const QString lastConfig = protocolObject.value(QStringLiteral("last_config")).toString();
    if (!lastConfig.isEmpty()) {
        protocolObject[QStringLiteral("last_config")] = patchAddressInConfigString(lastConfig, ip);
    }
    const QString configStr = protocolObject.value(QStringLiteral("config")).toString();
    if (!configStr.isEmpty()) {
        protocolObject[QStringLiteral("config")] = patchAddressInConfigString(configStr, ip);
    }
}

void patchContainerObject(QJsonObject &containerObject, const QString &ip)
{
    // flat (CDN) form: the container object itself may hold last_config/config
    patchProtocolObject(containerObject, ip);
    // nested protocol objects ("amnezia-awg", "awg", "xray", ...)
    for (auto it = containerObject.begin(); it != containerObject.end(); ++it) {
        if (it.value().isObject()) {
            QJsonObject protocolObject = it.value().toObject();
            patchProtocolObject(protocolObject, ip);
            containerObject[it.key()] = protocolObject;
        }
    }
}

void patchServerConfigAddress(QJsonObject &serverConfig, const QString &ip)
{
    serverConfig[QStringLiteral("hostName")] = ip;
    QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
    for (int i = 0; i < containers.size(); ++i) {
        QJsonObject containerObject = containers.at(i).toObject();
        patchContainerObject(containerObject, ip);
        containers.replace(i, containerObject);
    }
    if (!containers.isEmpty()) {
        serverConfig[QStringLiteral("containers")] = containers;
    }
}

} // namespace

ConnectionController::ConnectionController(const QSharedPointer<ServersModel> &serversModel,
                                           const QSharedPointer<ContainersModel> &containersModel,
                                           const QSharedPointer<ClientManagementModel> &clientManagementModel,
                                           const QSharedPointer<VpnConnection> &vpnConnection, const std::shared_ptr<Settings> &settings,
                                           QObject *parent)
    : QObject(parent),
      m_serversModel(serversModel),
      m_containersModel(containersModel),
      m_clientManagementModel(clientManagementModel),
      m_vpnConnection(vpnConnection),
      m_settings(settings)
{
    connect(m_vpnConnection.get(), &VpnConnection::connectionStateChanged, this, &ConnectionController::onConnectionStateChanged);
    connect(this, &ConnectionController::connectToVpn, m_vpnConnection.get(), &VpnConnection::connectToVpn, Qt::QueuedConnection);
    connect(this, &ConnectionController::disconnectFromVpn, m_vpnConnection.get(), &VpnConnection::disconnectFromVpn, Qt::QueuedConnection);

    connect(this, &ConnectionController::connectButtonClicked, this, &ConnectionController::toggleConnection, Qt::QueuedConnection);

    m_autoProbeTimer = new QTimer(this);
    m_autoProbeTimer->setSingleShot(true);
    connect(m_autoProbeTimer, &QTimer::timeout, this, &ConnectionController::onAutoProbeSettled);

    m_autoAttemptTimer = new QTimer(this);
    m_autoAttemptTimer->setSingleShot(true);
    connect(m_autoAttemptTimer, &QTimer::timeout, this, [this]() {
        if (m_autoPhase != AutoPhase::Connecting) {
            return;
        }
        // same candidate, untried addresses left — retry the next one first
        if (retryWithNextIp()) {
            m_autoAwaitingTraffic = false;
            return;
        }
        qDebug() << "[AUTO] attempt timed out" << (m_autoAwaitingTraffic ? "(no traffic)" : "") << ", trying next candidate";
        m_autoCandidatePos++;
        connectCurrentAutoCandidate();
    });

    m_ipTrafficTimer = new QTimer(this);
    m_ipTrafficTimer->setSingleShot(true);
    connect(m_ipTrafficTimer, &QTimer::timeout, this, [this]() {
        if (!m_ipAwaitingTraffic) {
            return;
        }
        m_ipAwaitingTraffic = false;
        qDebug() << "[IPPOOL] connected but no traffic on row" << m_ipPoolRow << ", trying next address";
        if (!retryWithNextIp()) {
            resetIpPool();
            emit disconnectFromVpn();
            emit connectionErrorOccurred(m_vpnConnection->lastError());
        }
    });

    m_manualConnectTimer = new QTimer(this);
    m_manualConnectTimer->setSingleShot(true);
    connect(m_manualConnectTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[CONNECT] manual attempt timed out, row" << m_ipPoolRow;
        if (retryWithNextIp()) {
            return; // connectToServerIndexWithIp restarts the watchdog
        }
        resetIpPool();
        m_manualConnectTimer->stop();
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        emit disconnectFromVpn();
        emit connectionErrorOccurred(ErrorCode::ServerConnectionTimeoutError);
        emit connectionStateChanged();
    });

    // traffic proof: bytes must move. Used by auto selection for probe-unconfirmed
    // candidates and by the multi-IP failover for every multi-IP connect
    connect(m_vpnConnection.get(), &VpnConnection::bytesChanged, this, [this](quint64 receivedBytes, quint64) {
        if (m_autoPhase == AutoPhase::Connecting && m_autoAwaitingTraffic) {
            if (m_autoTrafficBaseline == ~0ULL) {
                m_autoTrafficBaseline = receivedBytes; // first sample = baseline
                return;
            }
            if (receivedBytes > m_autoTrafficBaseline) {
                qDebug() << "[AUTO] traffic confirmed on row" << m_autoCandidates.at(m_autoCandidatePos).row;
                finalizeAutoSuccess();
            }
            return;
        }
        if (m_autoPhase == AutoPhase::None && m_ipAwaitingTraffic) {
            if (m_ipTrafficBaseline == ~0ULL) {
                m_ipTrafficBaseline = receivedBytes; // first sample = baseline
                return;
            }
            if (receivedBytes > m_ipTrafficBaseline) {
                qDebug() << "[IPPOOL] traffic confirmed on row" << m_ipPoolRow;
                resetIpPool(); // success — the next connect starts from a fresh random pool
            }
        }
    });

    m_state = Vpn::ConnectionState::Disconnected;
}

void ConnectionController::setHealthCheckController(HealthCheckController *healthCheckController)
{
    m_healthCheckController = healthCheckController;
}

void ConnectionController::setApiConfigsController(ApiConfigsController *apiConfigsController)
{
    m_apiConfigsController = apiConfigsController;
}

void ConnectionController::openConnection()
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    if (!Utils::processIsRunning(Utils::executable(SERVICE_NAME, false), true))
    {
        emit connectionErrorOccurred(ErrorCode::AmneziaServiceNotRunning);
        return;
    }
#endif

    if (m_settings->isAutoServerSelection() && m_healthCheckController) {
        startAutoSelection();
        return;
    }

    connectToServerIndex(m_serversModel->getDefaultServerIndex());
}

void ConnectionController::connectToServerIndex(int serverIndex)
{
    // multi-IP nodes: build a freshly shuffled address pool per connect — each
    // attempt starts from a random entry address, retries walk the rest
    const QJsonArray nodeIps = m_serversModel->getServerConfig(serverIndex).value(QStringLiteral("node_ips")).toArray();
    QStringList ips;
    for (const QJsonValue &v : nodeIps) {
        const QString ip = v.toString();
        if (!ip.isEmpty()) {
            ips.append(ip);
        }
    }
    ips.removeDuplicates();

    if (ips.size() > 1) {
        m_ipPool = ips;
        for (int i = m_ipPool.size() - 1; i > 0; --i) {
            m_ipPool.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
        }
        m_ipPoolPos = 0;
        m_ipPoolRow = serverIndex;
        qDebug() << "[IPPOOL] row" << serverIndex << "address pool:" << m_ipPool;
        connectToServerIndexWithIp(serverIndex, m_ipPool.first());
        return;
    }

    resetIpPool();
    connectToServerIndexWithIp(serverIndex, QString());
}

void ConnectionController::connectToServerIndexWithIp(int serverIndex, const QString &ip)
{
    QJsonObject serverConfig = m_serversModel->getServerConfig(serverIndex);

    DockerContainer container = qvariant_cast<DockerContainer>(m_serversModel->data(serverIndex, ServersModel::Roles::DefaultContainerRole));

    if (!m_containersModel->isSupportedByCurrentPlatform(container)) {
        emit connectionErrorOccurred(ErrorCode::NotSupportedOnThisPlatform);
        return;
    }

    // teardown states of a previous tunnel are neither a failure nor a user cancel
    m_connectionSwitching = true;

    if (!ip.isEmpty()) {
        patchServerConfigAddress(serverConfig, ip);
    }

    QSharedPointer<ServerController> serverController(new ServerController(m_settings));
    VpnConfigurationsController vpnConfigurationController(m_settings, serverController);

    QJsonObject containerConfig;
    {
        // Always prefer the container entry from the target server's own config —
        // m_containersModel may still hold the PREVIOUS server's container when
        // switching servers (produced "France" name with the old server's config
        // body: connected to France in the UI, traffic exits the old server).
        // NB: the "container" field holds the container name ("amnezia-awg"),
        // not the protocol alias ("awg") returned by containerTypeToString
        const QString containerName = ContainerProps::containerToString(container);
        const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
        for (const QJsonValue &v : containers) {
            const QJsonObject entry = v.toObject();
            if (entry.value(QStringLiteral("container")).toString() == containerName) {
                containerConfig = entry;
                break;
            }
        }
    }
    if (containerConfig.isEmpty()) {
        containerConfig = m_containersModel->getContainerConfig(container);
    }

    ServerCredentials credentials = m_serversModel->getServerCredentials(serverIndex);
    if (!ip.isEmpty()) {
        credentials.hostName = ip;
    }
    if (m_currentEndpoint != credentials.hostName) {
        m_currentEndpoint = credentials.hostName;
        emit connectionStateChanged();
    }

    auto dns = m_serversModel->getDnsPair(serverIndex);

    auto vpnConfiguration = vpnConfigurationController.createVpnConfiguration(dns, serverConfig, containerConfig, container);
    if (m_autoPhase == AutoPhase::None) {
        // manual connect: fail loudly instead of spinning forever on a dead server
        m_manualConnectTimer->start(kManualConnectTimeoutMs);
    }
    emit connectToVpn(serverIndex, credentials, container, vpnConfiguration);
}

void ConnectionController::resetIpPool()
{
    m_ipPool.clear();
    m_ipPoolPos = 0;
    m_ipPoolRow = -1;
    m_ipAwaitingTraffic = false;
    if (m_ipTrafficTimer) {
        m_ipTrafficTimer->stop();
    }
}

bool ConnectionController::retryWithNextIp()
{
    if (m_ipPoolRow < 0 || m_ipPoolPos + 1 >= m_ipPool.size()) {
        return false;
    }
    m_ipPoolPos++;
    qDebug() << "[IPPOOL] row" << m_ipPoolRow << "failed, trying next address" << m_ipPool.at(m_ipPoolPos);
    m_ipAwaitingTraffic = false;
    m_ipTrafficTimer->stop();
    connectToServerIndexWithIp(m_ipPoolRow, m_ipPool.at(m_ipPoolPos));
    if (m_autoPhase == AutoPhase::Connecting) {
        m_autoAttemptTimer->start(kAutoAttemptTimeoutMs);
    }
    m_connectionStateText = tr("Connecting...");
    emit connectionStateChanged();
    return true;
}

// --- auto server selection ---

int ConnectionController::tierForRow(int row) const
{
    const QString filterProtocol = m_serversModel->data(row, ServersModel::Roles::ServiceProtocolFilterRole).toString().toLower();
    if (filterProtocol == QStringLiteral("awg-mobile") || filterProtocol == QStringLiteral("amneziawgmobile")) {
        return 0; // AmneziaWgMobile first
    }
    const QString protocol = m_serversModel->data(row, ServersModel::Roles::ServiceProtocolRole).toString();
    if (protocol == QStringLiteral("awg") || protocol == QStringLiteral("wireguard")) {
        return 1;
    }
    if (protocol == QStringLiteral("hysteria2")) {
        return 2;
    }
    if (protocol == QStringLiteral("vless")) {
        return isVlessCdnRow(row) ? 4 : 3; // CDN-fronted vless is the last resort
    }
    return 5;
}

bool ConnectionController::isVlessCdnRow(int row) const
{
    // xhttp over plain TLS = CDN-fronted server (same shape as in HealthCheckController)
    const QJsonObject serverConfig = m_serversModel->getServerConfig(row);
    const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
    if (containers.isEmpty()) {
        return false;
    }
    const QJsonObject containerObject = containers.at(0).toObject();
    QJsonObject configRoot = QJsonDocument::fromJson(
            containerObject.value(QStringLiteral("xray")).toObject().value(QStringLiteral("config")).toString().toUtf8())
                                     .object();
    if (configRoot.isEmpty()) {
        configRoot = QJsonDocument::fromJson(containerObject.value(QStringLiteral("config")).toString().toUtf8()).object();
    }
    const QJsonArray outbounds = configRoot.value(QStringLiteral("outbounds")).toArray();
    const QJsonObject outbound = outbounds.isEmpty() ? configRoot : outbounds.at(0).toObject();
    const QJsonObject streamSettings = outbound.value(QStringLiteral("streamSettings")).toObject();
    return streamSettings.value(QStringLiteral("security")).toString() == QStringLiteral("tls")
            && streamSettings.value(QStringLiteral("network")).toString() == QStringLiteral("xhttp");
}

QList<ConnectionController::AutoCandidate> ConnectionController::buildAutoCandidates() const
{
    QList<AutoCandidate> candidates;
    const QString envFilter = m_settings->serversEnvFilter();
    const int count = m_serversModel->getServersCount();
    for (int i = 0; i < count; ++i) {
        if (!envFilter.isEmpty() && envFilter != QStringLiteral("all")
            && m_serversModel->data(i, ServersModel::Roles::ConnectionEnvRole).toString() != envFilter) {
            continue;
        }
        DockerContainer container = qvariant_cast<DockerContainer>(m_serversModel->data(i, ServersModel::Roles::DefaultContainerRole));
        if (!m_containersModel->isSupportedByCurrentPlatform(container)) {
            continue;
        }
        AutoCandidate candidate;
        candidate.row = i;
        candidate.tier = tierForRow(i);
        candidate.latency = m_serversModel->data(i, ServersModel::Roles::HealthLatencyRole).toInt();
        candidates.append(candidate);
    }

    // tier asc; inside a tier: probe-confirmed by rtt asc, then unprobed, then offline
    const auto latencyRank = [](int latency) { return latency >= 0 ? 0 : (latency == -2 ? 1 : 2); };
    std::sort(candidates.begin(), candidates.end(), [&](const AutoCandidate &a, const AutoCandidate &b) {
        if (a.tier != b.tier) {
            return a.tier < b.tier;
        }
        if (latencyRank(a.latency) != latencyRank(b.latency)) {
            return latencyRank(a.latency) < latencyRank(b.latency);
        }
        if (a.latency >= 0 && b.latency >= 0 && a.latency != b.latency) {
            return a.latency < b.latency;
        }
        return a.row < b.row;
    });
    return candidates;
}

int ConnectionController::pickAutoCandidate(const QList<AutoCandidate> &candidates) const
{
    if (candidates.isEmpty()) {
        return -1;
    }

    // strict tier order (list is already sorted): take the best tier's first
    // probe-confirmed candidate with an acceptable rtt
    for (int i = 0; i < candidates.size(); ++i) {
        if (candidates.at(i).latency >= 0 && candidates.at(i).latency <= kAutoAcceptLatencyMs) {
            return i;
        }
    }

    // nothing fast anywhere: first usable (alive-but-slow or unprobed) in tier order
    for (int i = 0; i < candidates.size(); ++i) {
        if (candidates.at(i).latency != -1) {
            return i;
        }
    }

    // everything probed offline — badges lie sometimes, try in priority order
    return 0;
}

void ConnectionController::installMissingConfigs()
{
    if (!m_apiConfigsController) {
        return;
    }
    // gateway-issued rows get their container config only on the first manual
    // connect (ApiConfigsController::isConfigValid) — until then there is nothing
    // to probe (no WG keys) and nothing to connect with. Install them up front.
    const QString envFilter = m_settings->serversEnvFilter();
    const int count = m_serversModel->getServersCount();
    for (int i = 0; i < count; ++i) {
        if (!envFilter.isEmpty() && envFilter != QStringLiteral("all")
            && m_serversModel->data(i, ServersModel::Roles::ConnectionEnvRole).toString() != envFilter) {
            continue;
        }
        if (!m_serversModel->data(i, ServersModel::Roles::IsServerFromGatewayApiRole).toBool()) {
            continue;
        }
        DockerContainer container = qvariant_cast<DockerContainer>(m_serversModel->data(i, ServersModel::Roles::DefaultContainerRole));
        if (!m_containersModel->isSupportedByCurrentPlatform(container)) {
            continue;
        }
        if (m_serversModel->serverHasUsableConfig(i)) {
            continue;
        }
        qDebug() << "[AUTO] installing config for row" << i;
        m_apiConfigsController->updateServiceFromGateway(i, "", "", false, true);
    }
}

void ConnectionController::startAutoSelection()
{
    cancelAutoSelection();
    m_autoPhase = AutoPhase::Probing;
    m_connectionStateText = tr("Searching for the best server...");
    emit connectionStateChanged();

    installMissingConfigs();

    // fresh probe results are already there (the server list was open recently) —
    // decide without another probe round
    const qint64 lastProbe = m_healthCheckController->lastProbeMsecs();
    const bool fresh = lastProbe != 0 && QDateTime::currentMSecsSinceEpoch() - lastProbe < kAutoFreshProbeMs
            && !m_healthCheckController->isProbing();
    if (fresh && tryEarlyAutoDecision()) {
        return;
    }

    m_healthCheckController->startProbe(true);

    // connect AFTER startProbe: its internal stopProbe() may synchronously emit
    // probingFinished for a previous run — that one is not ours to settle on
    connect(m_healthCheckController, &HealthCheckController::probingFinished, this,
            &ConnectionController::onAutoProbeSettled, Qt::UniqueConnection);
    connect(m_serversModel.get(), &ServersModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &,
                                                                           const QVector<int> &roles) {
        if (roles.isEmpty() || roles.contains(ServersModel::Roles::HealthLatencyRole)) {
            onAutoHealthUpdated();
        }
    });

    if (!m_healthCheckController->isProbing()) {
        // no targets at all — the run finished synchronously inside startProbe
        onAutoProbeSettled();
        return;
    }
    m_autoProbeTimer->start(kAutoProbeTimeoutMs);
}

bool ConnectionController::tryEarlyAutoDecision()
{
    const QList<AutoCandidate> candidates = buildAutoCandidates();
    if (candidates.isEmpty()) {
        return false;
    }
    const int pick = pickAutoCandidate(candidates);
    if (pick < 0) {
        return false;
    }
    // early exit only for a probe-confirmed fast server in the BEST tier present —
    // otherwise keep waiting: a higher-priority protocol may still get its result
    if (candidates.at(pick).latency < 0 || candidates.at(pick).latency > kAutoAcceptLatencyMs
        || candidates.at(pick).tier != candidates.first().tier) {
        return false;
    }
    qDebug() << "[AUTO] early pick: row" << candidates.at(pick).row << "rtt" << candidates.at(pick).latency;
    m_autoCandidates = candidates;
    m_autoCandidates.move(pick, 0);
    beginAutoConnect();
    return true;
}

void ConnectionController::onAutoHealthUpdated()
{
    if (m_autoPhase != AutoPhase::Probing) {
        return;
    }
    tryEarlyAutoDecision();
}

void ConnectionController::onAutoProbeSettled()
{
    if (m_autoPhase != AutoPhase::Probing) {
        return;
    }
    m_autoProbeTimer->stop();
    m_autoCandidates = buildAutoCandidates();
    const int pick = pickAutoCandidate(m_autoCandidates);
    if (pick > 0) {
        m_autoCandidates.move(pick, 0);
    }
    beginAutoConnect();
}

void ConnectionController::beginAutoConnect()
{
    disconnect(m_healthCheckController, &HealthCheckController::probingFinished, this,
               &ConnectionController::onAutoProbeSettled);
    disconnect(m_serversModel.get(), &ServersModel::dataChanged, this, nullptr);
    m_autoProbeTimer->stop();
    m_healthCheckController->stopProbe();

    if (m_autoCandidates.isEmpty()) {
        qWarning() << "[AUTO] no candidates, falling back to the selected server";
        m_autoPhase = AutoPhase::None;
        connectToServerIndex(m_serversModel->getDefaultServerIndex());
        return;
    }

    m_autoPhase = AutoPhase::Connecting;
    m_autoCandidatePos = 0;
    connectCurrentAutoCandidate();
}

void ConnectionController::finalizeAutoSuccess()
{
    // the winning candidate becomes the selected server
    m_autoAttemptTimer->stop();
    m_autoAwaitingTraffic = false;
    if (m_autoCandidatePos < m_autoCandidates.size()) {
        m_serversModel->setDefaultServerIndex(m_autoCandidates.at(m_autoCandidatePos).row);
    }
    m_autoPhase = AutoPhase::None;
    m_autoCandidates.clear();
    resetIpPool();
    // isConnectionInProgress just flipped to false — QML must know, otherwise
    // the connect-button spinner keeps spinning forever under the "Connected" text
    emit connectionStateChanged();
}

void ConnectionController::connectCurrentAutoCandidate()
{
    if (m_autoCandidatePos >= m_autoCandidates.size()) {
        qWarning() << "[AUTO] all candidates failed";
        m_autoPhase = AutoPhase::None;
        m_autoAdvancing = false;
        m_autoAwaitingTraffic = false;
        emit disconnectFromVpn();
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        emit connectionErrorOccurred(m_vpnConnection->lastError());
        emit connectionStateChanged();
        return;
    }

    const AutoCandidate candidate = m_autoCandidates.at(m_autoCandidatePos);
    qDebug() << "[AUTO] trying row" << candidate.row << "tier" << candidate.tier << "rtt" << candidate.latency;
    m_autoAdvancing = true;
    m_autoAwaitingTraffic = false;
    connectToServerIndex(candidate.row);
    m_autoAttemptTimer->start(kAutoAttemptTimeoutMs);
}

void ConnectionController::cancelAutoSelection()
{
    m_autoPhase = AutoPhase::None;
    m_autoAdvancing = false;
    m_autoAwaitingTraffic = false;
    m_autoCandidates.clear();
    m_autoCandidatePos = 0;
    if (m_healthCheckController) {
        disconnect(m_healthCheckController, &HealthCheckController::probingFinished, this,
                   &ConnectionController::onAutoProbeSettled);
    }
    disconnect(m_serversModel.get(), &ServersModel::dataChanged, this, nullptr);
    if (m_autoProbeTimer) {
        m_autoProbeTimer->stop();
    }
    if (m_autoAttemptTimer) {
        m_autoAttemptTimer->stop();
    }
}

// ---

void ConnectionController::closeConnection()
{
    m_manualConnectTimer->stop();
    if (m_autoPhase != AutoPhase::None) {
        // cancelled while searching / trying candidates
        cancelAutoSelection();
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        emit connectionStateChanged();
    }
    resetIpPool(); // user cancel kills any pending multi-IP retry as well
    emit disconnectFromVpn();
}

ErrorCode ConnectionController::getLastConnectionError()
{
    return m_vpnConnection->lastError();
}

void ConnectionController::onConnectionStateChanged(Vpn::ConnectionState state)
{
    if (state == Vpn::ConnectionState::Connecting || state == Vpn::ConnectionState::Preparing
        || state == Vpn::ConnectionState::Connected) {
        m_autoAdvancing = false;
        m_connectionSwitching = false;
    }

    if (m_autoPhase == AutoPhase::Connecting) {
        if (state == Vpn::ConnectionState::Connected) {
            m_autoAttemptTimer->stop();
            // "Connected" alone proves nothing: a broken entry can still bring a
            // WG-style tunnel up, and a successful junk probe does not guarantee
            // traffic flows (seen in the wild: rtt 22ms, handshake ok, zero bytes).
            // Require real bytes for EVERY candidate before declaring success.
            if (m_autoCandidatePos < m_autoCandidates.size()) {
                m_autoAwaitingTraffic = true;
                m_autoTrafficBaseline = ~0ULL;
                m_autoAttemptTimer->start(kAutoTrafficTimeoutMs);
            } else {
                finalizeAutoSuccess();
            }
        } else if (state == Vpn::ConnectionState::Error || state == Vpn::ConnectionState::Unknown) {
            m_autoAttemptTimer->stop();
            if (retryWithNextIp()) {
                // same candidate, next entry address
                m_state = state;
                return;
            }
            m_autoCandidatePos++;
            if (m_autoCandidatePos < m_autoCandidates.size()) {
                // candidate failed quietly — try the next one, no error UI yet
                m_state = state;
                connectCurrentAutoCandidate();
                return;
            }
            m_autoPhase = AutoPhase::None; // out of candidates: report the error below
        } else if (state == Vpn::ConnectionState::Disconnected && !m_autoAdvancing) {
            // iOS tears a broken tunnel down as Connecting -> Disconnecting -> Disconnected
            // (PacketTunnelProviderError) without ever reporting Vpn::Error — treat it as a
            // failed attempt and advance to the next candidate. A real user cancel goes
            // through closeConnection(), which resets the phase before Disconnected arrives.
            m_autoAttemptTimer->stop();
            if (retryWithNextIp()) {
                m_state = state;
                return;
            }
            m_autoCandidatePos++;
            if (m_autoCandidatePos < m_autoCandidates.size()) {
                m_state = state;
                connectCurrentAutoCandidate();
                return;
            }
            m_autoPhase = AutoPhase::None;
            m_autoCandidates.clear();
            m_isConnectionInProgress = false;
            m_connectionStateText = tr("Connect");
            m_currentEndpoint.clear();
            emit connectionErrorOccurred(m_vpnConnection->lastError());
            emit connectionStateChanged();
            return;
        }
    } else if (m_ipPoolRow >= 0) {
        // manual connect to a multi-IP server
        if (state == Vpn::ConnectionState::Connected) {
            // a blocked entry address can still bring the tunnel up — require real bytes
            m_ipAwaitingTraffic = true;
            m_ipTrafficBaseline = ~0ULL;
            m_ipTrafficTimer->start(kIpTrafficTimeoutMs);
        } else if (state == Vpn::ConnectionState::Error || state == Vpn::ConnectionState::Unknown
                   || (state == Vpn::ConnectionState::Disconnected && !m_connectionSwitching)) {
            if (retryWithNextIp()) {
                m_state = state;
                return;
            }
            resetIpPool(); // pool exhausted — fall through to the normal error/UI path
        }
    }

    m_state = state;

    m_isConnected = false;
    m_connectionStateText = tr("Connecting...");
    switch (state) {
    case Vpn::ConnectionState::Connected: {
        m_isConnectionInProgress = false;
        m_isConnected = true;
        m_connectionStateText = tr("Connected");
        m_manualConnectTimer->stop(); // the traffic watchdogs take it from here
        break;
    }
    case Vpn::ConnectionState::Connecting: {
        m_isConnectionInProgress = true;
        break;
    }
    case Vpn::ConnectionState::Reconnecting: {
        m_isConnectionInProgress = true;
        m_connectionStateText = tr("Reconnecting...");
        break;
    }
    case Vpn::ConnectionState::Disconnected: {
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        m_currentEndpoint.clear();
        if (!m_connectionSwitching) {
            m_manualConnectTimer->stop();
        }
        break;
    }
    case Vpn::ConnectionState::Disconnecting: {
        m_isConnectionInProgress = true;
        m_connectionStateText = tr("Disconnecting...");
        break;
    }
    case Vpn::ConnectionState::Preparing: {
        m_isConnectionInProgress = true;
        m_connectionStateText = tr("Preparing...");
        break;
    }
    case Vpn::ConnectionState::Error: {
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        m_manualConnectTimer->stop();
        emit connectionErrorOccurred(getLastConnectionError());
        break;
    }
    case Vpn::ConnectionState::Unknown: {
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        m_manualConnectTimer->stop();
        emit connectionErrorOccurred(getLastConnectionError());
        break;
    }
    }
    emit connectionStateChanged();
}

void ConnectionController::onCurrentContainerUpdated()
{
    if (m_isConnected || m_isConnectionInProgress) {
        emit reconnectWithUpdatedContainer(tr("Settings updated successfully, reconnnection..."));
        openConnection();
    } else {
        emit reconnectWithUpdatedContainer(tr("Settings updated successfully"));
    }
}

void ConnectionController::onTranslationsUpdated()
{
    // get translated text of current state
    onConnectionStateChanged(getCurrentConnectionState());
}

Vpn::ConnectionState ConnectionController::getCurrentConnectionState()
{
    return m_state;
}

QString ConnectionController::connectionStateText() const
{
    return m_connectionStateText;
}

void ConnectionController::toggleConnection()
{
    if (m_state == Vpn::ConnectionState::Preparing) {
        emit preparingConfig();
        return;
    }

    if (isConnectionInProgress()) {
        closeConnection();
    } else if (isConnected()) {
        closeConnection();
    } else {
        emit prepareConfig();
    }
}

bool ConnectionController::isConnectionInProgress() const
{
    return m_isConnectionInProgress || m_autoPhase != AutoPhase::None;
}

bool ConnectionController::isConnected() const
{
    return m_isConnected;
}
