#include <QDateTime>
#include "servers_model.h"

#include <QJsonObject>
#include <QLocale>
#include <QSet>

#include "core/api/apiDefs.h"
#include "core/networkUtilities.h"
#include "node_endpoints.h"
#include "core/peerConfigBuilder.h"

#if defined(Q_OS_IOS) || defined(MACOS_NE)
    #include <Dopamine-Swift.h>
#endif

#include "core/api/apiUtils.h"

namespace
{
    namespace configKey
    {
        constexpr char apiConfig[] = "api_config";
        constexpr char serviceInfo[] = "service_info";
        constexpr char availableCountries[] = "available_countries";
        constexpr char serverCountryCode[] = "server_country_code";
        constexpr char serverCountryName[] = "server_country_name";
        constexpr char userCountryCode[] = "user_country_code";
        constexpr char serviceType[] = "service_type";
        constexpr char serviceProtocol[] = "service_protocol";

        constexpr char publicKeyInfo[] = "public_key";
        constexpr char expiresAt[] = "expires_at";
    }

    QString normalizeVpnKey(const QString &vpnKey)
    {
        QString normalized = vpnKey.trimmed();
        if (normalized.startsWith(QStringLiteral("vpn://"), Qt::CaseInsensitive)) {
            normalized = normalized.mid(QStringLiteral("vpn://").size());
        }
        return normalized;
    }

    QString flagCountryCode(QString countryCode)
    {
        countryCode = countryCode.trimmed().toUpper();
        static const QHash<QString, QString> aliases {
            { QStringLiteral("SWE"), QStringLiteral("SE") },
            { QStringLiteral("HEL"), QStringLiteral("FI") },
            { QStringLiteral("UK"), QStringLiteral("GB") },
            { QStringLiteral("EST"), QStringLiteral("EE") },
            { QStringLiteral("TLL"), QStringLiteral("EE") },
            { QStringLiteral("EESTI"), QStringLiteral("EE") },
            { QStringLiteral("ESTONIA"), QStringLiteral("EE") },
            { QStringLiteral("TALLINN"), QStringLiteral("EE") },
        };
        return aliases.value(countryCode, countryCode);
    }

    // A stored server belongs to an FRKN subscription only when it came from the
    // gateway. Share imports (share_token / "shared" marker) and hand-added configs
    // are not subscriptions and must not show up in the subscription selector.
    QString subscriptionIdOfServer(const QJsonObject &server)
    {
        const QJsonObject apiConfig = server.value(configKey::apiConfig).toObject();
        if (apiConfig.value(QStringLiteral("shared")).toBool()) {
            return QString();
        }
        QJsonObject authData = apiConfig.value(QStringLiteral("auth_data")).toObject();
        if (authData.isEmpty()) {
            authData = server.value(QStringLiteral("auth_data")).toObject();
        }
        if (authData.contains(QStringLiteral("share_token"))) {
            return QString();
        }
        QString id = authData.value(apiDefs::key::apiKey).toString();
        if (id.isEmpty()) {
            id = authData.value(apiDefs::key::id).toString();
        }
        return id;
    }

    // ── Peers pool rows ────────────────────────────────────────────────────
    // The peers plane gives the list one row per (env, pool): `peerVariants`
    // carries the concrete card of every protocol the pool serves and
    // `peerCardId` the one that is selected. Everything else on the row (the
    // endpoint list, protocol, container, container key) describes that selected
    // variant, so the connect path, the probes and the platform layers keep
    // reading a row exactly as before.

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

    QJsonArray peerVariantsOf(const QJsonObject &row)
    {
        return row.value(QStringLiteral("peerVariants")).toArray();
    }

    // `<shard>:<pool>` of a peers card id (`<shard>:<pool>:<proto>`). The card
    // id is the only pool identity a row written before `peerPoolKey` existed
    // carries, and it still names its pool.
    QString poolKeyOfCardId(const QString &cardId)
    {
        const QString raw = cardId.trimmed();
        const int sep = raw.lastIndexOf(QLatin1Char(':'));
        return sep > 0 ? raw.left(sep) : QString();
    }

    // The pool a peer row belongs to, whatever the build that wrote it knew
    // about: the stored pool key, the selected card, or any protocol variant.
    QString peerPoolKeyOf(const QJsonObject &row)
    {
        const QString stored = row.value(QStringLiteral("peerPoolKey")).toString();
        if (!stored.isEmpty()) {
            return stored;
        }
        const QString fromCard = poolKeyOfCardId(row.value(QStringLiteral("peerCardId")).toString());
        if (!fromCard.isEmpty()) {
            return fromCard;
        }
        for (const QJsonValue &value : peerVariantsOf(row)) {
            const QString fromVariant = poolKeyOfCardId(
                    value.toObject().value(QStringLiteral("cardId")).toString());
            if (!fromVariant.isEmpty()) {
                return fromVariant;
            }
        }
        return QString();
    }

    // Settings key of the pool's protocol choice:
    // "<subscription>|<shard>:<pool>". Empty for v1 rows and for rows written by
    // a build that predates the pool variants.
    QString poolChoiceKey(const QJsonObject &row)
    {
        const QString poolKey = row.value(QStringLiteral("peerPoolKey")).toString();
        if (poolKey.isEmpty()) {
            return QString();
        }
        return row.value(QStringLiteral("peerSubscriptionId")).toString() + QLatin1Char('|') + poolKey;
    }

    // Re-point the row at one of its protocol variants: endpoints, protocol,
    // container and container key all follow the card, and the tunnel cached for
    // that card (if any) is put back so the switch costs no api call.
    void applyPeerVariant(QJsonObject &row, const QJsonObject &variant, const QJsonObject &cachedContainer)
    {
        const QString cardId = variant.value(QStringLiteral("cardId")).toString();
        const QString proto = variant.value(QStringLiteral("proto")).toString();
        const QString protocol = variant.value(QStringLiteral("protocol")).toString();
        const QString containerName = variant.value(QStringLiteral("container")).toString();
        // The key the platform layers read the protocol config by inside the
        // container ("xray" for a hysteria2 card, "awg"/"wireguard" otherwise).
        QString protocolKey = variant.value(QStringLiteral("protocolKey")).toString();
        if (protocolKey.isEmpty()) {
            protocolKey = protocol;
        }
        const QJsonArray nodes = variant.value(QStringLiteral("nodes")).toArray();

        if (!nodes.isEmpty()) {
            const QJsonObject first = nodes.at(0).toObject();
            const QString endpoint = first.value(QStringLiteral("endpoint")).toString();
            // A client_config node (hysteria2) states its dial target in the
            // payload: `server` may be the TLS domain while the endpoint is an
            // entry IP, and the port lives in the payload too.
            const QJsonObject payload = first.value(QStringLiteral("payload")).toObject();
            const QString payloadServer = payload.value(QStringLiteral("server")).toString().trimmed();
            const int payloadPort = payload.value(QStringLiteral("port")).toInt();
            row[config_key::hostName] =
                    payloadServer.isEmpty() ? endpointHostOf(endpoint) : payloadServer;
            row[config_key::port] = payloadPort > 0 ? payloadPort : endpointPortOf(endpoint);

            QStringList hosts;
            QJsonObject ports;
            QJsonObject weights;
            QJsonArray endpoints;
            for (const QJsonValue &value : nodes) {
                const QJsonObject cardNode = value.toObject();
                // A card node may carry its entry point in the ONE endpoint
                // shape (`endpoints[]`, with weight/label) next to the legacy
                // `endpoint: "ip:port"` string; prefer the list when present.
                const QJsonArray nodeEndpoints = cardNode.value(QStringLiteral("endpoints")).toArray();
                if (!nodeEndpoints.isEmpty()) {
                    for (const QJsonValue &endpointValue : nodeEndpoints) {
                        const QJsonObject entry = endpointValue.toObject();
                        const QString host = entry.value(QStringLiteral("ip")).toString().trimmed();
                        if (host.isEmpty() || hosts.contains(host)) {
                            continue;
                        }
                        QJsonObject record = entry;
                        record[QStringLiteral("ip")] = host;
                        endpoints.append(record);
                        hosts.append(host);
                        const int port = entry.value(QStringLiteral("port")).toInt();
                        if (port > 0) {
                            ports[host] = port;
                        }
                        const int weight = entry.value(QStringLiteral("weight")).toInt();
                        if (weight > 0 && weight != NodeEndpoints::kDefaultWeight) {
                            weights[host] = weight;
                        }
                    }
                    continue;
                }
                const QString endpointString = cardNode.value(QStringLiteral("endpoint")).toString();
                const QString host = endpointHostOf(endpointString);
                if (host.isEmpty() || hosts.contains(host)) {
                    continue;
                }
                hosts.append(host);
                const int port = endpointPortOf(endpointString);
                if (port > 0) {
                    ports[host] = port;
                }
            }
            row[QStringLiteral("node_ips")] = QJsonArray::fromStringList(hosts);
            row[QStringLiteral("node_ports")] = ports;
            if (!endpoints.isEmpty()) {
                row[QStringLiteral("endpoints")] = endpoints;
            } else {
                row.remove(QStringLiteral("endpoints"));
            }
            if (!weights.isEmpty()) {
                row[QStringLiteral("node_weights")] = weights;
            } else {
                row.remove(QStringLiteral("node_weights"));
            }

            QStringList dns;
            for (const QJsonValue &value : first.value(QStringLiteral("dns")).toArray()) {
                if (!value.toString().isEmpty()) {
                    dns.append(value.toString());
                }
            }
            if (dns.isEmpty()) {
                dns = QStringList { QStringLiteral("1.1.1.1"), QStringLiteral("1.0.0.1") };
            }
            row[config_key::dns1] = dns.value(0);
            row[config_key::dns2] = dns.value(1, dns.value(0));
        }
        row[QStringLiteral("peerNodes")] = nodes;
        row[QStringLiteral("peerCardId")] = cardId;

        QJsonObject apiConfig = row.value(configKey::apiConfig).toObject();
        apiConfig[configKey::serviceProtocol] = protocol;
        row[configKey::apiConfig] = apiConfig;

        QJsonObject display = row.value(QStringLiteral("displayInfo")).toObject();
        display[QStringLiteral("protocol")] = protocol;
        display[QStringLiteral("proto")] = proto;
        // The card kind travels with the row as well: the tunnel builder rebuilds
        // the card from displayInfo, and a `client_config` card (hysteria2) that
        // loses its kind is sent down the peer path, which asks for a
        // server_public_key it does not have ("card has no enabled endpoint with
        // a server key").
        const QString kind = variant.value(QStringLiteral("kind")).toString().trimmed();
        display[QStringLiteral("kind")] = !kind.isEmpty() ? kind
                : proto.contains(QLatin1String("hysteria"), Qt::CaseInsensitive) ? QStringLiteral("client_config")
                                                                                : QStringLiteral("peer");
        const QString country = variant.value(QStringLiteral("country")).toString();
        const QString countryName = variant.value(QStringLiteral("countryName")).toString();
        if (!country.isEmpty()) {
            display[QStringLiteral("countryCode")] = country;
        }
        if (!countryName.isEmpty()) {
            display[QStringLiteral("countryName")] = countryName;
        }
        row[QStringLiteral("displayInfo")] = display;

        QJsonObject container = cachedContainer;
        if (container.isEmpty()) {
            QJsonObject protoConfig;
            container[config_key::container] = containerName;
            container[protocol] = protoConfig;
            container[containerName] = protoConfig;
        } else {
            container[config_key::container] = containerName;
        }
        row[config_key::containers] = QJsonArray { container };
        row[config_key::defaultContainer] = containerName;
    }
}

ServersModel::ServersModel(std::shared_ptr<Settings> settings, QObject *parent) : m_settings(settings), QAbstractListModel(parent)
{
    m_isAmneziaDnsEnabled = m_settings->useAmneziaDns();

    connect(this, &ServersModel::defaultServerIndexChanged, this, &ServersModel::defaultServerNameChanged);

    connect(this, &ServersModel::defaultServerIndexChanged, this, [this](const int serverIndex) {
        // -1 means "no servers left" (last row removed): there is no default
        // container to read, and m_servers.at(-1) would assert.
        if (serverIndex < 0 || serverIndex >= m_servers.size()) {
            return;
        }
        auto defaultContainer =
                ContainerProps::containerFromString(m_servers.at(serverIndex).toObject().value(config_key::defaultContainer).toString());
        emit ServersModel::defaultServerDefaultContainerChanged(defaultContainer);
        emit ServersModel::defaultServerNameChanged();
        updateDefaultServerContainersModel();
    });

    connect(this, &ServersModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
        for (int i = topLeft.row(); i <= bottomRight.row(); ++i) {
            if (i == m_defaultServerIndex) {
                emit defaultServerNameChanged();
                emit defaultServerDescriptionChanged();
            }
        }
        emit processedServerChanged();
    });

    connect(this, &QAbstractItemModel::modelReset, this, &ServersModel::recomputeGatewayStacks);
}

int ServersModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return static_cast<int>(m_servers.size());
}

bool ServersModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_servers.size())) {
        return false;
    }

    QJsonObject server = m_servers.at(index.row()).toObject();
    const auto configVersion = server.value(config_key::configVersion).toInt();

    switch (role) {
    case NameRole: {
        if (configVersion) {
            server.insert(config_key::name, value.toString());
        } else {
            server.insert(config_key::description, value.toString());
        }
        server.insert(config_key::nameOverriddenByUser, true);
        m_settings->editServer(index.row(), server);
        m_servers.replace(index.row(), server);
        if (index.row() == m_defaultServerIndex) {
            emit defaultServerNameChanged();
        }
        break;
    }
    default: {
        return true;
    }
    }

    emit dataChanged(index, index);
    return true;
}

bool ServersModel::setData(const int index, const QVariant &value, int role)
{
    QModelIndex modelIndex = this->index(index);
    return setData(modelIndex, value, role);
}

QVariant ServersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_servers.size())) {
        return QVariant();
    }

    const QJsonObject server = m_servers.at(index.row()).toObject();
    const auto apiConfig = server.value(configKey::apiConfig).toObject();
    const auto configVersion = server.value(config_key::configVersion).toInt();
    switch (role) {
    case NameRole: {
        if (configVersion) {
            // Pool-based localization is resolved on every read, so a UI
            // language change only needs a dataChanged (no reload). A name the
            // user set always wins and is never translated.
            if (!server.value(config_key::nameOverriddenByUser).toBool()) {
                const QJsonObject displayInfo = server.value(QStringLiteral("displayInfo")).toObject();
                const QString localized = localizedPoolName(displayInfo.value(QStringLiteral("pool")).toString(),
                                                            displayInfo.value(QStringLiteral("poolTranslation")).toObject(),
                                                            m_settings->getAppLanguage());
                if (!localized.isEmpty()) {
                    return localized;
                }
            }
            return server.value(config_key::name).toString();
        }
        auto name = server.value(config_key::description).toString();
        if (name.isEmpty()) {
            return server.value(config_key::hostName).toString();
        }
        return name;
    }
    case ServerDescriptionRole: {
        return getServerDescription(server, index.row());
    }
    case HostNameRole: return server.value(config_key::hostName).toString();
    case EndpointHostRole: {
        const QString hostName = server.value(config_key::hostName).toString();
        if (!hostName.isEmpty()) {
            return hostName;
        }
        // Proxy rows (hysteria2, vless behind a CDN, vmess/trojan) carry no
        // top-level hostName: the dial target lives in the stored outbound, and
        // the row must still name the host we ping and hand to the tunnel.
        const QString hysteria = hysteriaDialHost(index.row());
        return hysteria.isEmpty() ? proxyDialHost(index.row()) : hysteria;
    }
    case EndpointDomainRole: {
        const QString domain = hysteriaDialDomain(index.row());
        // when the config already dials the domain itself the Domain row would
        // only repeat Endpoint, so it stays empty
        const QString hostName = server.value(config_key::hostName).toString();
        return domain == hostName ? QString() : domain;
    }
    case CredentialsRole: return QVariant::fromValue(serverCredentials(index.row()));
    case CredentialsLoginRole: return serverCredentials(index.row()).userName;
    case IsDefaultRole: return index.row() == m_defaultServerIndex;
    case IsCurrentlyProcessedRole: return index.row() == m_processedServerIndex;
    case HasWriteAccessRole: {
        auto credentials = serverCredentials(index.row());
        return (!credentials.userName.isEmpty() && !credentials.secretData.isEmpty());
    }
    case ContainsAmneziaDnsRole: {
        QString primaryDns = server.value(config_key::dns1).toString();
        return primaryDns == protocols::dns::amneziaDnsIp;
    }
    case DefaultContainerRole: {
        return ContainerProps::containerFromString(server.value(config_key::defaultContainer).toString());
    }
    case HasInstalledContainers: {
        return serverHasInstalledContainers(index.row());
    }
    case IsServerFromTelegramApiRole: {
        return server.value(config_key::configVersion).toInt() == apiDefs::ConfigSource::Telegram;
    }
    case IsServerFromGatewayApiRole: {
        return server.value(config_key::configVersion).toInt() == apiDefs::ConfigSource::AmneziaGateway;
    }
    case ApiConfigRole: {
        return apiConfig;
    }
    case IsCountrySelectionAvailableRole: {
        return !apiConfig.value(configKey::availableCountries).toArray().isEmpty();
    }
    case ApiAvailableCountriesRole: {
        return apiConfig.value(configKey::availableCountries).toArray();
    }
    case ApiServerCountryCodeRole: {
        return apiConfig.value(configKey::serverCountryCode).toString();
    }
    case ServiceProtocolRole: {
        auto protocol = apiConfig.value(configKey::serviceProtocol).toString();
        if (protocol.isEmpty()) {
            protocol = server.value(QStringLiteral("displayInfo")).toObject().value(QStringLiteral("protocol")).toString().toLower();
        }
        return canonicalServiceProtocol(protocol);
    }
    case ServiceProtocolFilterRole: {
        // raw backend tag (e.g. "AmneziaWgMobile") lowercased — protocol variants
        // get their own entry in the UI filter while ServiceProtocolRole stays
        // canonical for logic
        auto protocol = apiConfig.value(configKey::serviceProtocol).toString();
        if (protocol.isEmpty()) {
            protocol = server.value(QStringLiteral("displayInfo")).toObject().value(QStringLiteral("protocol")).toString();
        }
        if (protocol.isEmpty()) {
            // legacy self-hosted servers carry no api metadata — derive the tag
            // from the default container so the protocol filter doesn't hide them
            const auto container = ContainerProps::containerFromString(server.value(config_key::defaultContainer).toString());
            protocol = ContainerProps::containerTypeToProtocolString(container);
            if (protocol == QStringLiteral("none")) {
                protocol.clear();
            }
        }
        return protocol.toLower();
    }
    case ConnectionEnvRole: {
        const QString env = apiConfig.value(QStringLiteral("env")).toString();
        if (env.startsWith(QLatin1String("custompremium"))) {
            return QStringLiteral("premium");
        }
        if (env.startsWith(QLatin1String("custom"))) {
            return QStringLiteral("private");
        }
        return env;
    }
    case CountryCodeRole: {
        // Флаг = страна ноды. Поля пула (pool_country_code) не используем:
        // лишний слой, который ломался на каждой смене представительной ноды.
        auto countryCode = apiConfig.value(configKey::serverCountryCode).toString();
        if (countryCode.isEmpty()) {
            countryCode = apiConfig.value(configKey::userCountryCode).toString();
        }
        if (countryCode.isEmpty()) {
            countryCode = apiConfig.value(QStringLiteral("country_code")).toString();
        }
        if (countryCode.isEmpty()) {
            countryCode = server.value(QStringLiteral("displayInfo")).toObject()
                                .value(QStringLiteral("countryCode")).toString();
        }
        return flagCountryCode(countryCode);
    }
    case CountryNameRole: {
        // human-readable country without the protocol suffix — the server list
        // shows it under the name while the protocol lives in the filter dropdown
        auto countryName = apiConfig.value(configKey::serverCountryName).toString();
        if (countryName.isEmpty()) {
            countryName = apiConfig.value(QStringLiteral("country_name")).toString();
        }
        if (countryName.isEmpty()) {
            countryName = server.value(QStringLiteral("displayInfo")).toObject()
                                 .value(QStringLiteral("countryName")).toString();
        }
        return countryName;
    }
    case NodeIpsRole: {
        // all entry addresses of a multi-IP node (empty for single-address
        // nodes); `endpoints[]` first, the legacy node_ips as fallback
        return NodeEndpoints::ips(server);
    }
    case HealthPingRole: {
        // Proxy protocols (vless, hysteria2, any client_config card) cannot be
        // handshake-probed from the client: their latency is a server ping.
        const QString protocol = data(index, ServiceProtocolRole).toString();
        return protocol != QStringLiteral("awg") && protocol != QStringLiteral("wireguard");
    }
    case HealthLatencyRole: {
        // >=0 latency ms, -1 offline, -2 unknown (not probed yet); keyed by row
        // index — several servers can share a host on different ports (e.g. AWG
        // and its mobile variant), keying by host name mixed up their results.
        // The value also belongs to the protocol card it was measured for: a pool
        // row swaps cards in place behind the protocol pills, and the number of
        // the protocol we just left must not stay on screen.
        const auto it = m_healthResults.constFind(index.row());
        if (it == m_healthResults.constEnd()) {
            return -2;
        }
        const QString rowCard = server.value(QStringLiteral("peerCardId")).toString();
        if (it->cardId != rowCard) {
            return -2;
        }
        return it->latencyMs;
    }
    case HasAmneziaDns: {
        QString primaryDns = server.value(config_key::dns1).toString();
        return primaryDns == protocols::dns::amneziaDnsIp;
    }
    case IsAdVisibleRole: {
        return apiConfig.value(apiDefs::key::serviceInfo).toObject().value(apiDefs::key::isAdVisible).toBool(false);
    }
    case AdHeaderRole: {
        return apiConfig.value(apiDefs::key::serviceInfo).toObject().value(apiDefs::key::adHeader).toString();
    }
    case AdDescriptionRole: {
        return apiConfig.value(apiDefs::key::serviceInfo).toObject().value(apiDefs::key::adDescription).toString();
    }
    case AdEndpointRole: {
        return apiConfig.value(apiDefs::key::serviceInfo).toObject().value(apiDefs::key::adEndpoint).toString();
    }
    }

    return QVariant();
}

QVariant ServersModel::data(const int index, int role) const
{
    QModelIndex modelIndex = this->index(index);
    return data(modelIndex, role);
}

void ServersModel::resetModel()
{
    // Открытая карточка не должна «переезжать» на сервер по умолчанию при
    // каждом обновлении подписки: держим ту же строку, если она ещё существует.
    const int previousProcessed = m_processedServerIndex;
    bumpRowsGeneration();
    beginResetModel();
    m_healthResults.clear();
    loadEndpointHealthCache();
    m_servers = m_settings->serversArray();

    // Selection by identity, not by position: the rebuild may insert, drop or
    // reorder rows, and then the stored index silently points at another
    // server (the card "changes by itself" after a background refresh).
    const QString storedDefaultIdentity = m_settings->defaultServerIdentity();
    int resolvedDefault = rowIndexForDefaultIdentity();
    if (resolvedDefault < 0 && storedDefaultIdentity.isEmpty()) {
        // Upgrade from a build that stored only a position: adopt that row, so
        // the next refresh already restores by identity.
        const int storedIndex = m_settings->defaultServerIndex();
        if (storedIndex >= 0 && storedIndex < m_servers.size()) {
            resolvedDefault = storedIndex;
        }
    }
    // Восстанавливаем выбор юзера: сначала по идентичности (uuid+node),
    // индекс — только запасной вариант, он «плывёт» при обновлении подписки.
    const int restoredSelected = rowIndexForUserSelection();
    if (resolvedDefault < 0) {
        // The selected row is gone (grant revoked, subscription re-imported).
        // Never reuse its stale position — take the user's last pick, then the
        // row the open card was on, then the first one.
        resolvedDefault = restoredSelected;
        if (resolvedDefault < 0 && previousProcessed >= 0 && previousProcessed < m_servers.size()) {
            resolvedDefault = previousProcessed;
        }
        if (resolvedDefault < 0 && !m_servers.isEmpty()) {
            resolvedDefault = 0;
        }
    }
    m_defaultServerIndex = resolvedDefault;

    if (restoredSelected >= 0) {
        m_processedServerIndex = restoredSelected;
    } else if (previousProcessed >= 0 && previousProcessed < m_servers.size()) {
        m_processedServerIndex = previousProcessed;
    } else {
        m_processedServerIndex = m_defaultServerIndex;
    }

    // One-time migration from builds that kept the default row on index 0 while
    // the real selection lived in selectedServerIndex. Without this the first
    // frame shows FK and the subscription refresh later rewrites it to EU.
    // Only while no identity is stored — an identity is authoritative.
    const int lastConnected = m_settings->lastConnectedServerIndex();
    if (storedDefaultIdentity.isEmpty() && restoredSelected >= 0 && restoredSelected == lastConnected
        && m_defaultServerIndex != restoredSelected) {
        qDebug() << "[SELECTION] default reconciled on startup:"
                 << m_defaultServerIndex << "->" << restoredSelected;
        m_defaultServerIndex = restoredSelected;
    }
    // Store the position (compatibility key) together with the identity the row
    // carries, so the next reset can restore by it.
    persistDefaultSelection(m_defaultServerIndex);

    m_isAmneziaDnsEnabled = m_settings->useAmneziaDns();
    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
    emit defaultServerIndexChanged(m_defaultServerIndex);
    emit processedServerIndexChanged(m_processedServerIndex);
}

void ServersModel::setDefaultServerIndex(const int index)
{
    m_settings->setDefaultServer(index);
    if (index >= 0 && index < m_servers.size()) {
        m_settings->setDefaultServerIdentity(selectionIdentity(m_servers.at(index).toObject()));
    } else {
        m_settings->setDefaultServerIdentity(QString());
    }
    m_defaultServerIndex = m_settings->defaultServerIndex();
    emit defaultServerIndexChanged(m_defaultServerIndex);

    // Тап по строке в списке серверов идёт через defaultIndex, а не через
    // processedIndex. Раньше из-за этого «выбранный сервер» запоминался только
    // у карточки, и после перезапуска поднималась старая идентичность —
    // на экране оказывался случайный сервер (FK / EST1). Пишем выбор и здесь.
    if (index >= 0 && index < m_servers.size()) {
        if (index != m_processedServerIndex) {
            setProcessedServerIndex(index);
        }
    }
}

const int ServersModel::getDefaultServerIndex()
{
    return m_defaultServerIndex;
}

const QString ServersModel::getDefaultServerName()
{
    return qvariant_cast<QString>(data(m_defaultServerIndex, NameRole));
}

// short human-readable protocol label for the home card plaque (small gray
// text under the server name): honors raw backend variant tags (awg-mobile)
// and falls back to the container name for self-hosted configs
const QString ServersModel::getDefaultServerProtocolName()
{
    if (m_defaultServerIndex < 0 || m_defaultServerIndex >= m_servers.size()) {
        return {};
    }

    const QJsonObject serverConfig = m_servers.at(m_defaultServerIndex).toObject();
    const auto configVersion = serverConfig.value(config_key::configVersion).toInt();

    if (configVersion) {
        QString protocol = serverConfig.value(configKey::apiConfig).toObject().value(configKey::serviceProtocol).toString();
        if (protocol.isEmpty()) {
            protocol = serverConfig.value(QStringLiteral("displayInfo")).toObject().value(QStringLiteral("protocol")).toString();
        }
        const QString display = serviceProtocolDisplayName(protocol);
        if (display != protocol) {
            return display;
        }
        static const QHash<QString, QString> names = { { QStringLiteral("awg"), QStringLiteral("AmneziaWG") },
                                                       { QStringLiteral("vless"), QStringLiteral("VLESS") },
                                                       { QStringLiteral("hysteria2"), QStringLiteral("Hysteria2") },
                                                       { QStringLiteral("wireguard"), QStringLiteral("WireGuard") } };
        return names.value(protocol.toLower(), protocol.toUpper());
    }

    const auto container = ContainerProps::containerFromString(serverConfig.value(config_key::defaultContainer).toString());
    return ContainerProps::containerHumanNames().value(container);
}

QString ServersModel::getServerDescription(const QJsonObject &server, const int index) const
{
    const auto configVersion = server.value(config_key::configVersion).toInt();
    const auto apiConfig = server.value(configKey::apiConfig).toObject();

    QString description;

    if (configVersion && !apiConfig.value(configKey::serverCountryCode).toString().isEmpty()) {
        QString protocol = apiConfig.value(configKey::serviceProtocol).toString().toUpper();
        QString countryName = apiConfig.value(configKey::serverCountryName).toString();
        QString countryCode = apiConfig.value(configKey::serverCountryCode).toString().toUpper();
        if (!countryName.isEmpty() && !protocol.isEmpty()) {
            return QString("%1 · %2").arg(countryName, protocol);
        }
        if (!countryName.isEmpty()) {
            return countryName;
        }
        if (!protocol.isEmpty()) {
            return QString("%1 · %2").arg(countryCode, protocol);
        }
        return countryCode;
    } else if (configVersion) {
        return server.value(config_key::description).toString();
    } else if (data(index, HasWriteAccessRole).toBool()) {
        if (m_isAmneziaDnsEnabled && isAmneziaDnsContainerInstalled(index)) {
            description += "Dopamine DNS | ";
        }
    } else {
        if (data(index, HasAmneziaDns).toBool()) {
            description += "Dopamine DNS | ";
        }
    }
    return description;
}

const QString ServersModel::getDefaultServerDescriptionCollapsed()
{
    const QJsonObject serverConfig = m_servers.at(m_defaultServerIndex).toObject();
    const auto configVersion = serverConfig.value(config_key::configVersion).toInt();
    auto description = getServerDescription(serverConfig, m_defaultServerIndex);
    if (configVersion) {
        return description;
    }

    auto container = ContainerProps::containerFromString(serverConfig.value(config_key::defaultContainer).toString());
    QString protocolVersion;
    QString containerName = ContainerProps::containerHumanNames().value(container);

    if (ContainerProps::isAwgContainer(container)) {
        QJsonObject containerConfig = m_settings->containerConfig(m_defaultServerIndex, container);
        QJsonObject serverProtocolConfig = containerConfig.value(ContainerProps::containerTypeToProtocolString(container)).toObject();
        protocolVersion = ProtocolProps::getProtocolVersionString(serverProtocolConfig);

        auto isThirdPartyConfig = serverProtocolConfig.value(config_key::isThirdPartyConfig).toBool();
        if (container == DockerContainer::Awg && !isThirdPartyConfig) {
            containerName = "AmneziaWG Legacy";
        }
    }

    return description += containerName + protocolVersion + " | " + serverConfig.value(config_key::hostName).toString();
}

const QString ServersModel::getDefaultServerDescriptionExpanded()
{
    const QJsonObject server = m_servers.at(m_defaultServerIndex).toObject();
    const auto configVersion = server.value(config_key::configVersion).toInt();
    auto description = getServerDescription(server, m_defaultServerIndex);
    if (configVersion) {
        return description;
    }

    return description += server.value(config_key::hostName).toString();
}

const QString ServersModel::getDefaultServerHostName()
{
    if (m_defaultServerIndex < 0 || m_defaultServerIndex >= m_servers.count()) {
        return QString();
    }
    return m_servers.at(m_defaultServerIndex).toObject().value(config_key::hostName).toString();
}

const QStringList ServersModel::getDefaultServerNodeIps()
{
    if (m_defaultServerIndex < 0 || m_defaultServerIndex >= m_servers.count()) {
        return QStringList();
    }
    return NodeEndpoints::ips(m_servers.at(m_defaultServerIndex).toObject());
}

const int ServersModel::getServersCount()
{
    return m_servers.count();
}

bool ServersModel::hasServerWithWriteAccess()
{
    for (size_t i = 0; i < getServersCount(); i++) {
        if (qvariant_cast<bool>(data(i, HasWriteAccessRole))) {
            return true;
        }
    }
    return false;
}

void ServersModel::setProcessedServerIndex(const int index)
{
    if (index >= 0 && m_settings) {
        m_settings->setSelectedServerIndex(index); // запоминаем выбор юзера
        // и его идентичность: индекс меняется, пара uuid+node — нет
        const QJsonObject row = getServerConfig(index);
        const QJsonObject apiConfig = row.value(QStringLiteral("api_config")).toObject();
        m_settings->setSelectedIdentity(apiConfig.value(QStringLiteral("connection_uuid")).toString(),
                                        apiConfig.value(QStringLiteral("node_id")).toString(),
                                        apiConfig.value(QStringLiteral("pool")).toString(),
                                        apiConfig.value(QStringLiteral("service_protocol")).toString());
        // A peer row has no uuid: the card id is its identity.
        m_settings->setSelectedPeerCardId(row.value(QStringLiteral("peerCardId")).toString());
    }
    m_processedServerIndex = index;
    updateContainersModel();
    if (data(index, IsServerFromGatewayApiRole).toBool()) {
        if (data(index, IsCountrySelectionAvailableRole).toBool()) {
            emit updateApiCountryModel();
        }
        emit updateApiServicesModel();
    }
    emit processedServerIndexChanged(m_processedServerIndex);
}

int ServersModel::getProcessedServerIndex()
{
    return m_processedServerIndex;
}

const ServerCredentials ServersModel::getProcessedServerCredentials()
{
    return serverCredentials(m_processedServerIndex);
}

const ServerCredentials ServersModel::getServerCredentials(const int index)
{
    return serverCredentials(index);
}

bool ServersModel::isDefaultServerCurrentlyProcessed()
{
    return m_defaultServerIndex == m_processedServerIndex;
}

bool ServersModel::isDefaultServerFromApi()
{
    return data(m_defaultServerIndex, IsServerFromTelegramApiRole).toBool()
            || data(m_defaultServerIndex, IsServerFromGatewayApiRole).toBool();
}

bool ServersModel::isProcessedServerHasWriteAccess()
{
    return qvariant_cast<bool>(data(m_processedServerIndex, HasWriteAccessRole));
}

bool ServersModel::isDefaultServerHasWriteAccess()
{
    return qvariant_cast<bool>(data(m_defaultServerIndex, HasWriteAccessRole));
}

void ServersModel::addServer(const QJsonObject &server)
{
    bumpRowsGeneration();
    beginResetModel();
    // `endpoints[]` (the API's single endpoint shape) -> the legacy keys the
    // rest of the client reads. A payload without it is passed through
    // byte-identically, so an old config keeps behaving exactly as before.
    QJsonObject normalized = server;
    NodeEndpoints::applyLegacy(normalized);
    m_settings->addServer(normalized);
    m_servers = m_settings->serversArray();
    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::addServers(const QJsonArray &servers)
{
    bumpRowsGeneration();
    beginResetModel();
    QJsonArray normalized;
    for (const QJsonValue &value : servers) {
        QJsonObject server = value.toObject();
        NodeEndpoints::applyLegacy(server);
        normalized.append(server);
    }
    m_settings->addServers(normalized);
    m_servers = m_settings->serversArray();
    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::removeAllServers()
{
    bumpRowsGeneration();
    beginResetModel();
    m_healthResults.clear();
    m_settings->setServersArray(QJsonArray());
    m_servers = QJsonArray();
    setDefaultServerIndex(-1);
    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::editServer(const QJsonObject &server, const int serverIndex)
{
    QJsonObject normalized = server;
    NodeEndpoints::applyLegacy(normalized);
    m_settings->editServer(serverIndex, normalized);
    m_servers.replace(serverIndex, m_settings->serversArray().at(serverIndex));
    emit dataChanged(index(serverIndex, 0), index(serverIndex, 0));

    if (serverIndex == m_defaultServerIndex) {
        updateDefaultServerContainersModel();
    }
    updateContainersModel();

    if (serverIndex == m_defaultServerIndex) {
        auto defaultContainer = qvariant_cast<DockerContainer>(getDefaultServerData("defaultContainer"));
        emit defaultServerDefaultContainerChanged(defaultContainer);
        emit defaultServerNameChanged();
    }
    recomputeAvailableSubscriptions();
}

void ServersModel::removeServer()
{
    bumpRowsGeneration();
    beginResetModel();
    m_healthResults.clear();
    m_settings->removeServer(m_processedServerIndex);
    m_servers = m_settings->serversArray();

    if (m_settings->defaultServerIndex() == m_processedServerIndex) {
        setDefaultServerIndex(0);
    } else if (m_settings->defaultServerIndex() > m_processedServerIndex) {
        setDefaultServerIndex(m_settings->defaultServerIndex() - 1);
    }

    if (m_settings->serversCount() == 0) {
        setDefaultServerIndex(-1);
    }
    setProcessedServerIndex(m_defaultServerIndex);
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::removeServer(const int serverIndex)
{
    bumpRowsGeneration();
    beginResetModel();
    m_healthResults.clear();
    m_settings->removeServer(serverIndex);
    m_servers = m_settings->serversArray();

    if (m_settings->defaultServerIndex() == serverIndex) {
        setDefaultServerIndex(0);
    } else if (m_settings->defaultServerIndex() > serverIndex) {
        setDefaultServerIndex(m_settings->defaultServerIndex() - 1);
    }

    if (m_settings->serversCount() == 0) {
        setDefaultServerIndex(-1);
    }
    setProcessedServerIndex(m_defaultServerIndex);
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::removeSubscription(const QString &subscriptionId)
{
    if (subscriptionId.isEmpty() || m_settings->serversCount() == 0) {
        return;
    }

    bumpRowsGeneration();
    beginResetModel();
    m_healthResults.clear();

    int defaultIndex = m_settings->defaultServerIndex();
    for (int i = m_settings->serversCount() - 1; i >= 0; --i) {
        const QJsonObject server = m_settings->serversArray().at(i).toObject();
        if (subscriptionIdOfServer(server) != subscriptionId) {
            continue;
        }

        m_settings->removeServer(i);
        if (i == defaultIndex) {
            defaultIndex = -1;
        } else if (i < defaultIndex) {
            --defaultIndex;
        }
    }

    m_servers = m_settings->serversArray();

    if (m_servers.isEmpty()) {
        defaultIndex = -1;
    } else if (defaultIndex < 0 || defaultIndex >= m_servers.size()) {
        defaultIndex = 0;
    }

    setDefaultServerIndex(defaultIndex);
    setProcessedServerIndex(defaultIndex);
    recomputeAvailableSubscriptions();
    endResetModel();
}

QHash<int, QByteArray> ServersModel::roleNames() const
{
    QHash<int, QByteArray> roles;

    roles[NameRole] = "name";
    roles[ServerDescriptionRole] = "serverDescription";
    roles[CollapsedServerDescriptionRole] = "collapsedServerDescription";
    roles[ExpandedServerDescriptionRole] = "expandedServerDescription";

    roles[HostNameRole] = "hostName";
    roles[EndpointHostRole] = "endpointHost";
    roles[EndpointDomainRole] = "endpointDomain";

    roles[CredentialsRole] = "credentials";
    roles[CredentialsLoginRole] = "credentialsLogin";

    roles[IsDefaultRole] = "isDefault";
    roles[IsCurrentlyProcessedRole] = "isCurrentlyProcessed";

    roles[HasWriteAccessRole] = "hasWriteAccess";

    roles[ContainsAmneziaDnsRole] = "containsAmneziaDns";

    roles[DefaultContainerRole] = "defaultContainer";
    roles[HasInstalledContainers] = "hasInstalledContainers";

    roles[IsServerFromTelegramApiRole] = "isServerFromTelegramApi";
    roles[IsServerFromGatewayApiRole] = "isServerFromGatewayApi";
    roles[ApiConfigRole] = "apiConfig";
    roles[IsCountrySelectionAvailableRole] = "isCountrySelectionAvailable";
    roles[ApiAvailableCountriesRole] = "apiAvailableCountries";
    roles[ApiServerCountryCodeRole] = "apiServerCountryCode";

    roles[IsAdVisibleRole] = "isAdVisible";
    roles[AdHeaderRole] = "adHeader";
    roles[AdDescriptionRole] = "adDescription";
    roles[AdEndpointRole] = "adEndpoint";
    roles[ServiceProtocolRole] = "serviceProtocol";
    roles[ServiceProtocolFilterRole] = "serviceProtocolFilter";
    roles[ConnectionEnvRole] = "connectionEnv";
    roles[CountryCodeRole] = "countryCode";
    roles[CountryNameRole] = "countryName";
    roles[NodeIpsRole] = "nodeIps";
    roles[HealthPingRole] = "healthPing";
    roles[HealthLatencyRole] = "healthLatency";

    return roles;
}

ServerCredentials ServersModel::serverCredentials(int index) const
{
    ServerCredentials credentials;
    if (index < 0 || index >= m_servers.size()) {
        // A row number can arrive from a caller that captured it before a
        // refresh (report card, QML delegate): never read the array blind.
        return credentials;
    }
    const QJsonObject &s = m_servers.at(index).toObject();

    credentials.hostName = s.value(config_key::hostName).toString();
    credentials.userName = s.value(config_key::userName).toString();
    credentials.secretData = s.value(config_key::password).toString();
    credentials.port = s.value(config_key::port).toInt();

    return credentials;
}

void ServersModel::updateContainersModel()
{
    if (m_processedServerIndex < 0 || m_processedServerIndex >= m_servers.size()) {
        emit containersUpdated(QJsonArray());
        return;
    }
    auto containers = m_servers.at(m_processedServerIndex).toObject().value(config_key::containers).toArray();
    emit containersUpdated(containers);
}

void ServersModel::updateDefaultServerContainersModel()
{
    if (m_defaultServerIndex < 0 || m_defaultServerIndex >= m_servers.size()) {
        emit defaultServerContainersUpdated(QJsonArray());
        return;
    }
    auto containers = m_servers.at(m_defaultServerIndex).toObject().value(config_key::containers).toArray();
    emit defaultServerContainersUpdated(containers);
}

QJsonObject ServersModel::getServerConfig(const int serverIndex) const
{
    if (serverIndex < 0 || serverIndex >= m_servers.size()) {
        return {};
    }
    return m_servers.at(serverIndex).toObject();
}

QJsonObject ServersModel::serverRow(const int serverIndex) const
{
    if (serverIndex < 0 || serverIndex >= m_servers.size()) {
        return {};
    }
    return m_servers.at(serverIndex).toObject();
}

QString ServersModel::peerCardId(const int serverIndex) const
{
    return serverRow(serverIndex).value(QStringLiteral("peerCardId")).toString();
}

QString ServersModel::subscriptionIdForRow(const int serverIndex) const
{
    const QJsonObject row = serverRow(serverIndex);
    if (row.isEmpty()) {
        return {};
    }
    const QString stored = row.value(QStringLiteral("peerSubscriptionId")).toString();
    return stored.isEmpty() ? subscriptionIdOfServer(row) : stored;
}

// The stable identity of a row. Peer cards carry `peerCardId` (the api's
// `<shard>:<pool>:<proto>`), v1 configs the gateway `connection_uuid`. Prefixes
// keep the two key spaces apart. A row with neither cannot be restored by
// identity — callers fall back to the position, as before.
QString ServersModel::selectionIdentity(const QJsonObject &row)
{
    const QString cardId = row.value(QStringLiteral("peerCardId")).toString();
    if (!cardId.isEmpty()) {
        return QStringLiteral("peer:") + cardId;
    }
    const QString connectionUuid = row.value(configKey::apiConfig).toObject()
                                           .value(QStringLiteral("connection_uuid")).toString();
    if (!connectionUuid.isEmpty()) {
        return QStringLiteral("uuid:") + connectionUuid;
    }
    return {};
}

QString ServersModel::defaultServerIdentity() const
{
    return m_settings ? m_settings->defaultServerIdentity() : QString();
}

QString ServersModel::rowIdentity(const int serverIndex) const
{
    return selectionIdentity(serverRow(serverIndex));
}

int ServersModel::rowForIdentity(const QString &identity) const
{
    if (identity.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < m_servers.size(); ++i) {
        if (selectionIdentity(m_servers.at(i).toObject()) == identity) {
            return i;
        }
    }
    // A peer row's identity is its active card id, and the active card moves
    // when the pool's protocol changes (a stored choice the pool no longer
    // serves falls back, or the user switches protocols). The old card id still
    // names the same pool, so resolve by pool key instead of losing the row and
    // landing the card page on a stale position with no variants to show.
    const QString peerPrefix = QStringLiteral("peer:");
    if (identity.startsWith(peerPrefix)) {
        const QString poolKey = poolKeyOfCardId(identity.mid(peerPrefix.size()));
        if (!poolKey.isEmpty()) {
            for (int i = 0; i < m_servers.size(); ++i) {
                if (peerPoolKeyOf(m_servers.at(i).toObject()) == poolKey) {
                    return i;
                }
            }
        }
    }
    return -1;
}

int ServersModel::rowIndexForDefaultIdentity() const
{
    const QString identity = defaultServerIdentity();
    if (identity.isEmpty()) {
        return -1;
    }
    return rowForIdentity(identity);
}

int ServersModel::rowIndexForUserSelection() const
{
    if (!m_settings) {
        return -1;
    }

    // Peer cards first: their uuid is empty, and matching them by pool breaks
    // as soon as two shards carry the same pool name.
    const QString selCardId = m_settings->selectedPeerCardId();
    if (!selCardId.isEmpty()) {
        for (int i = 0; i < m_servers.size(); ++i) {
            if (m_servers.at(i).toObject().value(QStringLiteral("peerCardId")).toString() == selCardId) {
                return i;
            }
        }
    }

    // Восстанавливаем выбор юзера: сначала по идентичности (uuid+node),
    // индекс — только запасной вариант, он «плывёт» при обновлении подписки.
    const QString selUuid = m_settings->selectedConnectionUuid();
    const QString selNode = m_settings->selectedNodeId();
    if (!selUuid.isEmpty() && !selNode.isEmpty()) {
        for (int i = 0; i < m_servers.size(); ++i) {
            const QJsonObject apiConfig = m_servers.at(i).toObject()
                                                  .value(QStringLiteral("api_config")).toObject();
            if (apiConfig.value(QStringLiteral("connection_uuid")).toString() == selUuid
                && apiConfig.value(QStringLiteral("node_id")).toString() == selNode) {
                return i;
            }
        }
    }
    // Пул: connection_uuid стабилен (это conn_id из БД), а node_id — нет:
    // в строке пула стоит представительная нода, и она меняется, когда
    // меняется порядок обхода нод. Поэтому для пула пара uuid+node рвётся,
    // хотя само соединение то же — сверяем uuid + пул + протокол.
    const QString selPool = m_settings->selectedPool();
    const QString selProto = m_settings->selectedProtocol();
    if (!selUuid.isEmpty() && !selPool.isEmpty()) {
        for (int i = 0; i < m_servers.size(); ++i) {
            const QJsonObject apiConfig = m_servers.at(i).toObject()
                                                  .value(QStringLiteral("api_config")).toObject();
            if (apiConfig.value(QStringLiteral("connection_uuid")).toString() == selUuid
                && apiConfig.value(QStringLiteral("pool")).toString() == selPool
                && (selProto.isEmpty()
                    || apiConfig.value(QStringLiteral("service_protocol")).toString() == selProto)) {
                return i;
            }
        }
    }
    // Последний шанс для пула: uuid мог быть перевыпущен (переимпорт
    // подписки), но имя пула и протокол те же.
    if (!selPool.isEmpty()) {
        for (int i = 0; i < m_servers.size(); ++i) {
            const QJsonObject apiConfig = m_servers.at(i).toObject()
                                                  .value(QStringLiteral("api_config")).toObject();
            if (apiConfig.value(QStringLiteral("pool")).toString() == selPool
                && (selProto.isEmpty()
                    || apiConfig.value(QStringLiteral("service_protocol")).toString() == selProto)) {
                return i;
            }
        }
    }
    // Nothing identifiable is left: the stored position is the previous
    // behaviour, adopted only when the identities above did not resolve.
    const int selected = m_settings->selectedServerIndex();
    if (selected >= 0 && selected < m_servers.size()) {
        return selected;
    }
    return -1;
}

void ServersModel::persistDefaultSelection(const int index)
{
    if (index >= 0 && index < m_servers.size()) {
        m_settings->setDefaultServer(index);
        m_settings->setDefaultServerIdentity(selectionIdentity(m_servers.at(index).toObject()));
    } else {
        m_settings->setDefaultServer(-1);
        m_settings->setDefaultServerIdentity(QString());
    }
}

bool ServersModel::peerRowHasConfig(const int serverIndex) const
{
    const QJsonObject row = serverRow(serverIndex);
    const QString cardId = row.value(QStringLiteral("peerCardId")).toString();
    if (cardId.isEmpty()) {
        return false;
    }
    // A manual reload keeps the old tunnel on the row for display only
    // (invalidatePeerConfig). It must not count as the card's config, or the
    // next connect would reuse the tunnel the user just asked to drop.
    if (row.value(QStringLiteral("peerConfigStale")).toBool()) {
        return false;
    }
    const QJsonArray containers = row.value(config_key::containers).toArray();
    if (!containers.isEmpty()) {
        const QJsonObject container = containers.at(0).toObject();
        const QString name = container.value(config_key::container).toString();
        if (!container.value(name).toObject().value(config_key::config).toString().isEmpty()) {
            return true;
        }
    }
    // The active entry may be empty (the row was just switched to a protocol
    // whose tunnel exists only in the per-variant cache), so the cache decides.
    const QJsonObject cached = row.value(QStringLiteral("peerVariantConfigs")).toObject().value(cardId).toObject();
    const QString cachedName = cached.value(config_key::container).toString();
    return !cached.value(cachedName).toObject().value(config_key::config).toString().isEmpty();
}

void ServersModel::pruneAbsentRows(const QString &subscriptionId, const QStringList &keepConnectionUuids)
{
    qInfo() << "[PRUNE] asked for" << subscriptionId << "keep" << keepConnectionUuids.size();
    if (subscriptionId.isEmpty() || keepConnectionUuids.isEmpty()) {
        qInfo() << "[PRUNE] nothing to do (empty id or keep list)";
        return;
    }
    int candidates = 0;
    const int count = getServersCount();
    QJsonArray kept;
    const int oldDefault = m_settings->defaultServerIndex();
    int newDefault = -1;
    int removed = 0;
    for (int i = 0; i < count; ++i) {
        const QJsonObject row = getServerConfig(i);
        bool drop = false;
        const int version = row.value(config_key::configVersion).toInt();
        const QString rowSub = subscriptionIdForRow(i);
        if (version == apiDefs::ConfigSource::AmneziaGateway && rowSub == subscriptionId) {
            ++candidates;
            const QString uuid = row.value(QStringLiteral("apiConfig"))
                                         .toObject()
                                         .value(QStringLiteral("connection_uuid"))
                                         .toString();
            qInfo() << "[PRUNE] row" << i << "uuid" << uuid.left(8)
                    << "kept?" << keepConnectionUuids.contains(uuid);
            if (!uuid.isEmpty() && !keepConnectionUuids.contains(uuid)) {
                drop = true;
            }
        }
        if (drop) {
            ++removed;
            continue;
        }
        if (i == oldDefault) {
            newDefault = kept.size();
        }
        kept.append(row);
    }
    qInfo() << "[PRUNE] candidates" << candidates << "removed" << removed;
    if (removed == 0) {
        return;
    }
    qInfo() << "[SUBSCRIPTION] dropping" << removed << "rows the api no longer serves for" << subscriptionId;

    bumpRowsGeneration();
    beginResetModel();
    m_healthResults.clear();
    m_settings->setServersArray(kept);
    if (newDefault < 0) {
        newDefault = kept.isEmpty() ? -1 : 0;
    }
    m_settings->setDefaultServer(newDefault);
    m_servers = m_settings->serversArray();
    m_defaultServerIndex = newDefault;
    setProcessedServerIndex(newDefault >= 0 ? newDefault : 0);
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::dropPeerCards(const QString &subscriptionId)
{
    const QJsonArray all = m_settings->serversArray();
    QJsonArray kept;
    const int oldDefault = m_settings->defaultServerIndex();
    int newDefault = -1;
    int removed = 0;
    for (int i = 0; i < all.size(); ++i) {
        const QJsonObject row = all.at(i).toObject();
        if (row.value(QStringLiteral("peerSubscriptionId")).toString() == subscriptionId) {
            ++removed;
            continue;
        }
        if (i == oldDefault) {
            newDefault = kept.size();
        }
        kept.append(all.at(i));
    }
    if (removed == 0) {
        return;
    }
    qInfo() << "[PEERS] grant has no card protocols left, dropping" << removed
            << "pool rows of" << subscriptionId;

    bumpRowsGeneration();
    beginResetModel();
    m_healthResults.clear();
    m_settings->setServersArray(kept);
    if (newDefault < 0) {
        newDefault = kept.isEmpty() ? -1 : 0;
    }
    m_settings->setDefaultServer(newDefault);
    m_servers = m_settings->serversArray();
    m_defaultServerIndex = newDefault;
    setProcessedServerIndex(newDefault >= 0 ? newDefault : 0);
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::syncPeerCards(const QString &subscriptionId, const QJsonArray &rows)
{
    // The answer must cover the subscription it claims to refresh. A locations
    // call that timed out, hit an HTTP error or found no live grant comes back
    // with an empty card list — and the rows below are rebuilt from that list,
    // so a reset here deleted the user's whole subscription on a failed
    // «обновить». Keep the stored rows (and their cached tunnels) untouched in
    // that case: only the caller knows whether the empty answer was an error,
    // and either way it is not evidence that the pools are gone. Removing a
    // subscription stays the job of forgetSubscription().
    //
    // Manual check of the two states this guard owns (the model has no test
    // harness; keep it cheap to re-verify):
    //   * pools on refresh N, empty answer on refresh N+1 → every row, its
    //     cached tunnel containers and the selected card must still be there;
    //   * request fails (timeout/HTTP error) → syncPeerCards is not called at
    //     all (PeersImportController), so stored rows stay byte-identical.
    bool coversSubscription = false;
    for (const QJsonValue &value : rows) {
        if (value.toObject().value(QStringLiteral("peerSubscriptionId")).toString() == subscriptionId) {
            coversSubscription = true;
            break;
        }
    }
    if (!coversSubscription) {
        qWarning() << "[PEERS] refresh returned no pools for" << subscriptionId << ", rows kept";
        return;
    }

    // Keep the cached tunnel of every variant the api still offers: rebuilding it
    // would burn another peer of the device budget on every list refresh, and a
    // protocol switch back and forth must not mint a second tunnel either.
    // `display` holds the tunnel of every row as well — including one a manual
    // reload invalidated — so the card keeps its config text while the rows are
    // rebuilt; `cached` is the part the connect path may actually reuse.
    QHash<QString, QJsonObject> cached;
    QHash<QString, QJsonObject> display;
    for (const QJsonValue &value : m_servers) {
        const QJsonObject row = value.toObject();
        const QString activeCard = row.value(QStringLiteral("peerCardId")).toString();
        const QJsonArray containers = row.value(config_key::containers).toArray();
        if (!activeCard.isEmpty() && !containers.isEmpty()) {
            display.insert(activeCard, containers.at(0).toObject());
            if (!row.value(QStringLiteral("peerConfigStale")).toBool()) {
                cached.insert(activeCard, containers.at(0).toObject());
            }
        }
        const QJsonObject perVariant = row.value(QStringLiteral("peerVariantConfigs")).toObject();
        for (auto it = perVariant.constBegin(); it != perVariant.constEnd(); ++it) {
            if (!cached.contains(it.key())) {
                cached.insert(it.key(), it.value().toObject());
                if (!display.contains(it.key())) {
                    display.insert(it.key(), it.value().toObject());
                }
            }
        }
    }

    // Payload fingerprint of every client_config card as the stored rows know
    // it. A client_config tunnel is only valid for the payload it was built
    // from (the api rotates obfs/auth/sni/port while the card id stays), so the
    // refresh compares the fresh card with this and drops the cached tunnel when
    // they differ — even when the payload changed while the app was closed: the
    // map is persisted on the row.
    QHash<QString, QString> previousPayloadHashes;
    for (const QJsonValue &value : m_servers) {
        const QJsonObject row = value.toObject();
        const QJsonObject storedHashes = row.value(QStringLiteral("peerPayloadHashes")).toObject();
        for (auto it = storedHashes.constBegin(); it != storedHashes.constEnd(); ++it) {
            if (it.value().isString() && !it.value().toString().isEmpty()) {
                previousPayloadHashes.insert(it.key(), it.value().toString());
            }
        }
        // A row written before the map existed keeps the active card's hash only.
        const QString activeCard = row.value(QStringLiteral("peerCardId")).toString();
        const QString activeHash = row.value(QStringLiteral("peerPayloadHash")).toString();
        if (!activeCard.isEmpty() && !activeHash.isEmpty() && !previousPayloadHashes.contains(activeCard)) {
            previousPayloadHashes.insert(activeCard, activeHash);
        }
    }

    bumpRowsGeneration();
    beginResetModel();
    // The pools this answer covers. A row written by a build that predates
    // `peerSubscriptionId` cannot be matched by subscription, so it is matched
    // by pool instead: when the fresh set describes the same `<shard>:<pool>`,
    // the legacy row is that pool's stale copy and must be replaced by it, not
    // kept beside it ("Russia" twice, forever).
    QSet<QString> freshPoolKeys;
    for (const QJsonValue &value : rows) {
        const QString key = peerPoolKeyOf(value.toObject());
        if (!key.isEmpty()) {
            freshPoolKeys.insert(key);
        }
    }
    QJsonArray merged;
    for (const QJsonValue &value : m_servers) {
        const QJsonObject row = value.toObject();
        const QString cardId = row.value(QStringLiteral("peerCardId")).toString();
        if (cardId.isEmpty()) {
            merged.append(row); // a v1 config row is not ours to touch
            continue;
        }
        // Every row of the subscription being refreshed is rebuilt from the
        // answer, never merged with it. Keeping an old row because its card is
        // still handed out is what duplicated the list: the previous import
        // wrote one row per card, the pooled import writes one row per pool,
        // and both matched `offered` — so each refresh appended another copy of
        // the same pool ("Russia" twice, "Germany | Premium" twice). The row
        // that disappears here is a stale copy; its tunnel lives on in `cached`
        // and is re-attached below by card id. This replace is only reached for
        // an answer that carries the subscription's pools: the guard at the top
        // of this method returns before the reset otherwise, so an empty answer
        // can never delete these rows.
        const QString rowSubscription = row.value(QStringLiteral("peerSubscriptionId")).toString();
        if (rowSubscription == subscriptionId) {
            continue;
        }
        // A row with no subscription id was written before that field existed.
        // Its own subscription is unknown, but `auth_data` still names it when
        // the build stored one: only a row that cannot belong elsewhere is
        // dropped, and only for a pool this refresh hands out again. A legacy
        // row of an un-refreshed subscription (no fresh counterpart) stays.
        if (rowSubscription.isEmpty()) {
            const QString owner = subscriptionIdOfServer(row);
            if ((owner.isEmpty() || owner == subscriptionId)
                && freshPoolKeys.contains(peerPoolKeyOf(row))) {
                continue;
            }
        }
        // Another subscription's rows are rebuilt by that subscription's own
        // refresh.
        merged.append(row);
    }

    for (const QJsonValue &value : rows) {
        QJsonObject row = value.toObject();
        const QString cardId = row.value(QStringLiteral("peerCardId")).toString();
        // Per-protocol tunnel cache carves out of the row it replaces: the card
        // ids are stable, the row position is not.
        QJsonObject perVariant;
        QJsonObject payloadHashes;
        QSet<QString> freshVariantCards;
        QSet<QString> clientConfigCards;
        bool activeIsClientConfig = false;
        bool activePayloadChanged = false;
        for (const QJsonValue &variantValue : peerVariantsOf(row)) {
            const QJsonObject variant = variantValue.toObject();
            const QString variantCard = variant.value(QStringLiteral("cardId")).toString();
            freshVariantCards.insert(variantCard);
            // A client_config card (hysteria2) is built from the card itself:
            // its payload carries the auth token and the obfs PSK, which the api
            // may rotate. The freshly built config on this row is the truth, so
            // the cached one must not overwrite it.
            const bool clientConfig =
                    variant.value(QStringLiteral("kind")).toString() == QLatin1String("client_config");
            // Fingerprint of the payload this variant carries now, compared with
            // the one the stored row was written with. A client_config config is
            // a function of its payload: when the api drops the obfs, rotates the
            // auth token or moves sni/port, the cached tunnel is wrong and is
            // dropped below instead of being dialled.
            const QString freshHash = amnezia::peers::payloadFingerprint(variant.value(QStringLiteral("nodes")).toArray());
            if (!variantCard.isEmpty() && !freshHash.isEmpty()) {
                payloadHashes.insert(variantCard, freshHash);
            }
            const QString previousHash = previousPayloadHashes.value(variantCard);
            const bool payloadChanged = clientConfig && !previousHash.isEmpty() && previousHash != freshHash;
            if (variantCard == cardId) {
                if (clientConfig) {
                    activeIsClientConfig = true;
                    activePayloadChanged = payloadChanged;
                }
            }
            if (clientConfig) {
                clientConfigCards.insert(variantCard);
                if (payloadChanged) {
                    qDebug() << "[PEERS] client_config payload changed for" << variantCard
                             << previousHash.left(12) << "->" << freshHash.left(12)
                             << "— dropping the cached tunnel";
                }
            }
            if (variantCard.isEmpty() || !cached.contains(variantCard) || clientConfig) {
                continue;
            }
            const QJsonObject cachedContainer = cached.value(variantCard);
            if (!cachedContainer.isEmpty()) {
                perVariant.insert(variantCard, cachedContainer);
            }
        }
        // Nothing outside the fresh variant set may keep a cached tunnel on the
        // row: a protocol the pool no longer serves must not be dialled (or
        // displayed) from a leftover per-variant config.
        for (auto it = perVariant.begin(); it != perVariant.end();) {
            // A client_config card never keeps a cached tunnel either — not even
            // for a card the pool still serves: its config is rebuilt from the
            // current payload, so a leftover entry could only be stale.
            if (!freshVariantCards.contains(it.key()) || clientConfigCards.contains(it.key())) {
                qDebug() << "[PEERS] dropping stale variant config" << it.key()
                         << "of pool" << peerPoolKeyOf(row);
                it = perVariant.erase(it);
            } else {
                ++it;
            }
        }
        row[QStringLiteral("peerVariantConfigs")] = perVariant;
        // The fingerprints the row goes forward with, per card and for the active
        // one: persisted with the row, so a payload that changes while the app is
        // closed is still detected on the next start.
        row[QStringLiteral("peerPayloadHashes")] = payloadHashes;
        row[QStringLiteral("peerPayloadHash")] = payloadHashes.value(cardId).toString();
        if (activeIsClientConfig && activePayloadChanged) {
            // The card's dial parameters moved. The row's container was rebuilt
            // from the fresh card above, but the flag makes the connect re-verify
            // it (PeersImportController::ensureClientConfigTunnel) instead of
            // trusting any cache written by an older build.
            row[QStringLiteral("peerConfigStale")] = true;
        }
        if (!activeIsClientConfig && cached.contains(cardId) && !cached.value(cardId).isEmpty()) {
            row[config_key::containers] = QJsonArray { cached.value(cardId) };
        } else if (!activeIsClientConfig && display.contains(cardId) && !display.value(cardId).isEmpty()) {
            // The tunnel of this card was dropped by a manual reload: the fresh
            // row still carries it so the card shows its config, but flagged so
            // the next connect rebuilds the tunnel instead of reusing it.
            row[config_key::containers] = QJsonArray { display.value(cardId) };
            row[QStringLiteral("peerConfigStale")] = true;
        }
        merged.append(row);
    }

    // A stored selection can name a protocol card this answer no longer hands
    // out (the pool dropped that protocol and its row fell back to another
    // variant). Point it at the card the row now holds, so the home card and
    // the card page keep resolving the same pool instead of a vanished card.
    const QString selectedCard = m_settings->selectedPeerCardId();
    if (!selectedCard.isEmpty()) {
        bool alive = false;
        for (const QJsonValue &value : merged) {
            if (value.toObject().value(QStringLiteral("peerCardId")).toString() == selectedCard) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            const QString poolKey = poolKeyOfCardId(selectedCard);
            for (const QJsonValue &value : merged) {
                const QJsonObject row = value.toObject();
                if (poolKey.isEmpty() || peerPoolKeyOf(row) != poolKey
                    || row.value(QStringLiteral("peerSubscriptionId")).toString() != subscriptionId) {
                    continue;
                }
                const QString card = row.value(QStringLiteral("peerCardId")).toString();
                if (!card.isEmpty()) {
                    qDebug() << "[PEERS] selected card" << selectedCard << "gone — following pool" << poolKey
                             << "to" << card;
                    m_settings->setSelectedPeerCardId(card);
                }
                break;
            }
        }
    }

    m_settings->setServersArray(merged);
    m_servers = merged;

    // The row positions just changed. Re-resolve the selection by identity so
    // the home card and the list keep the same server the user picked; a stale
    // index used to silently land on whatever row took that place.
    const int previousDefault = m_defaultServerIndex;
    int restoredDefault = rowIndexForDefaultIdentity();
    if (restoredDefault < 0) {
        if (defaultServerIdentity().isEmpty()) {
            // No identity yet (upgrade) or nothing selected: keep the old
            // position only while it still exists.
            restoredDefault = (previousDefault >= 0 && previousDefault < m_servers.size())
                    ? previousDefault
                    : (m_servers.isEmpty() ? -1 : 0);
        } else {
            // The selected card is gone (grant revoked): follow the user's last
            // pick, never the vanished row's position.
            restoredDefault = rowIndexForUserSelection();
            if (restoredDefault < 0 && previousDefault >= 0 && previousDefault < m_servers.size()) {
                restoredDefault = previousDefault;
            }
            if (restoredDefault < 0 && !m_servers.isEmpty()) {
                restoredDefault = 0;
            }
        }
    }
    const bool defaultMoved = restoredDefault != m_defaultServerIndex;
    m_defaultServerIndex = restoredDefault;
    persistDefaultSelection(m_defaultServerIndex);

    const int previousProcessed = m_processedServerIndex;
    const int restoredProcessed = rowIndexForUserSelection();
    m_processedServerIndex = restoredProcessed >= 0          ? restoredProcessed
            : m_defaultServerIndex >= 0                      ? m_defaultServerIndex
                                                             : -1;
    const bool processedMoved = m_processedServerIndex != previousProcessed;

    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
    // A model reset alone does not re-evaluate Q_PROPERTY bindings: without
    // these the card kept reading the old index (or the old row's data).
    if (defaultMoved) {
        emit defaultServerIndexChanged(m_defaultServerIndex);
    }
    if (processedMoved) {
        emit processedServerIndexChanged(m_processedServerIndex);
    }
}

void ServersModel::setPeerConfig(const QString &cardId, const QJsonObject &protoConfig)
{
    for (int i = 0; i < m_servers.size(); ++i) {
        QJsonObject row = m_servers.at(i).toObject();
        // The row is one pool: the tunnel belongs to the card it was created
        // for, which is not necessarily the variant on screen any more.
        QString containerName;
        QString protocol;
        QString protocolKey;
        bool clientConfig = false;
        bool active = row.value(QStringLiteral("peerCardId")).toString() == cardId;
        for (const QJsonValue &value : peerVariantsOf(row)) {
            const QJsonObject variant = value.toObject();
            if (variant.value(QStringLiteral("cardId")).toString() != cardId) {
                continue;
            }
            containerName = variant.value(QStringLiteral("container")).toString();
            protocol = variant.value(QStringLiteral("protocol")).toString();
            // hysteria2 stores its config under the container's protocol alias
            // ("xray"), not under the service protocol name the UI uses.
            protocolKey = variant.value(QStringLiteral("protocolKey")).toString();
            clientConfig = variant.value(QStringLiteral("kind")).toString() == QLatin1String("client_config");
            break;
        }
        if (!active && containerName.isEmpty()) {
            continue; // not this row
        }
        if (containerName.isEmpty()) {
            // A row written before the pool variants existed.
            containerName = QStringLiteral("amnezia-awg");
            protocol = QStringLiteral("awg");
        }
        if (protocolKey.isEmpty()) {
            protocolKey = protocol;
        }

        QJsonObject container;
        container[config_key::container] = containerName;
        // Both spellings: the platform layers read the short alias, iOS also
        // looks the data up by the container name.
        container[protocolKey] = protoConfig;
        container[containerName] = protoConfig;

        QJsonObject perVariant = row.value(QStringLiteral("peerVariantConfigs")).toObject();
        if (protoConfig.isEmpty()) {
            perVariant.remove(cardId);
        } else if (clientConfig) {
            // A client_config tunnel lives in the row's containers only: its
            // config is rebuilt from the payload on every connect, so keeping a
            // per-variant copy would just be a second, staler cache.
            perVariant.remove(cardId);
        } else {
            perVariant.insert(cardId, container);
        }
        row[QStringLiteral("peerVariantConfigs")] = perVariant;

        // A `client_config` container states the payload it was built from. Keep
        // the row's fingerprint in step with it, so the next connect and the next
        // refresh compare against the payload that is actually dialled.
        const QString payloadHash = protoConfig.value(QStringLiteral("payloadHash")).toString();
        if (!payloadHash.isEmpty()) {
            QJsonObject payloadHashes = row.value(QStringLiteral("peerPayloadHashes")).toObject();
            payloadHashes.insert(cardId, payloadHash);
            row[QStringLiteral("peerPayloadHashes")] = payloadHashes;
            if (active) {
                row[QStringLiteral("peerPayloadHash")] = payloadHash;
            }
        }

        // Provisioning timestamp, mirrored on the row: the connect path reads the
        // config's age from there (a week-old tunnel is refreshed before it is
        // dialled) without unpacking the container.
        const QString createdAt = protoConfig.value(QStringLiteral("config_created_at")).toString();
        if (!createdAt.isEmpty()) {
            // Both spellings: the unprefixed key is the contract field support
            // greps for, the `peer` one is what the peers controller reads.
            row[QStringLiteral("config_created_at")] = createdAt;
            row[QStringLiteral("peerConfigCreatedAt")] = createdAt;
        }

        if (active) {
            row[config_key::containers] = QJsonArray { container };
            row[config_key::defaultContainer] = containerName;
            if (protoConfig.isEmpty()) {
                // Nothing to show any more: the snapshot of a previous tunnel
                // would otherwise be presented as this card's config.
                row[QStringLiteral("peerConfigStale")] = false;
            } else {
                row.remove(QStringLiteral("peerConfigStale"));
            }
        }
        m_settings->editServer(i, row);
        m_servers.replace(i, row);
        if (active) {
            emit dataChanged(index(i, 0), index(i, 0));
        }
        return;
    }
}

void ServersModel::invalidatePeerConfig(const QString &cardId)
{
    if (cardId.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_servers.size(); ++i) {
        QJsonObject row = m_servers.at(i).toObject();
        const bool active = row.value(QStringLiteral("peerCardId")).toString() == cardId;
        QJsonObject perVariant = row.value(QStringLiteral("peerVariantConfigs")).toObject();
        if (!active && !perVariant.contains(cardId)) {
            continue;
        }
        // Drop what the connect path would reuse (the per-protocol cache, and the
        // cache flag of the active card), but keep the tunnel the row displays:
        // the card must not fall back to an empty config until the next connect
        // builds the fresh one.
        perVariant.remove(cardId);
        row[QStringLiteral("peerVariantConfigs")] = perVariant;
        if (active) {
            row[QStringLiteral("peerConfigStale")] = true;
        }
        m_settings->editServer(i, row);
        m_servers.replace(i, row);
        if (active) {
            emit dataChanged(index(i, 0), index(i, 0));
        }
        return;
    }
}

void ServersModel::markPeerConnected(const QString &cardId)
{
    if (!m_settings || cardId.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_servers.size(); ++i) {
        QJsonObject row = m_servers.at(i).toObject();
        const bool active = row.value(QStringLiteral("peerCardId")).toString() == cardId;
        const QJsonObject perVariant = row.value(QStringLiteral("peerVariantConfigs")).toObject();
        if (!active && !perVariant.contains(cardId)) {
            continue;
        }
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        row[QStringLiteral("last_connected_at")] = now;
        row[QStringLiteral("peerLastConnectedAt")] = now;
        if (active) {
            // Mirror into the tunnel config as well: the container is what
            // survives a row rebuild (syncPeerCards carries it over), while the
            // row-level copy is rebuilt from the card.
            QJsonArray containers = row.value(config_key::containers).toArray();
            if (!containers.isEmpty()) {
                QJsonObject container = containers.at(0).toObject();
                // Both spellings of the protocol object (`xray` and the container
                // name, `awg`/`amnezia-awg`) carry the same tunnel: stamp every
                // entry that holds a config.
                for (auto it = container.begin(); it != container.end(); ++it) {
                    if (it.key() == QLatin1String(config_key::container) || !it.value().isObject()) {
                        continue;
                    }
                    QJsonObject proto = it.value().toObject();
                    if (proto.value(config_key::config).toString().isEmpty()) {
                        continue;
                    }
                    proto[QStringLiteral("last_connected_at")] = now;
                    container[it.key()] = proto;
                }
                containers.replace(0, container);
                row[config_key::containers] = containers;
            }
        }
        m_settings->editServer(i, row);
        m_servers.replace(i, row);
        if (active) {
            emit dataChanged(index(i, 0), index(i, 0));
        }
        return;
    }
}

bool ServersModel::setPeerPoolProtocol(const int serverIndex, const QString &cardId)
{
    if (!m_settings || cardId.isEmpty() || serverIndex < 0 || serverIndex >= m_servers.size()) {
        return false;
    }
    QJsonObject row = m_servers.at(serverIndex).toObject();
    const QString currentCard = row.value(QStringLiteral("peerCardId")).toString();
    if (currentCard.isEmpty() || currentCard == cardId) {
        return false;
    }

    QJsonObject target;
    QString currentKind;
    for (const QJsonValue &value : peerVariantsOf(row)) {
        const QJsonObject variant = value.toObject();
        const QString variantCard = variant.value(QStringLiteral("cardId")).toString();
        if (variantCard == cardId) {
            target = variant;
        }
        if (variantCard == currentCard) {
            currentKind = variant.value(QStringLiteral("kind")).toString();
        }
    }
    if (target.isEmpty()) {
        return false;
    }

    // Park the tunnel we already have for the current protocol before switching:
    // switching back must reuse it instead of spending another device slot. A
    // client_config card's tunnel is never parked — it is rebuilt from its
    // payload on every connect and its fingerprint lives inside the container.
    QJsonObject cached = row.value(QStringLiteral("peerVariantConfigs")).toObject();
    const QJsonArray containers = row.value(config_key::containers).toArray();
    if (!containers.isEmpty() && currentKind != QLatin1String("client_config")) {
        cached.insert(currentCard, containers.at(0).toObject());
    }
    row[QStringLiteral("peerVariantConfigs")] = cached;

    applyPeerVariant(row, target, cached.value(cardId).toObject());

    // The row's active fingerprint follows the variant on screen.
    const QJsonObject payloadHashes = row.value(QStringLiteral("peerPayloadHashes")).toObject();
    if (payloadHashes.value(cardId).isString()) {
        row[QStringLiteral("peerPayloadHash")] = payloadHashes.value(cardId).toString();
    } else {
        row.remove(QStringLiteral("peerPayloadHash"));
    }
    m_settings->editServer(serverIndex, row);
    m_servers.replace(serverIndex, row);

    // Remember the choice per pool so a refresh (and the next app start) keeps
    // the same protocol on the row.
    m_settings->setPeerPoolProtocol(poolChoiceKey(row), target.value(QStringLiteral("proto")).toString());

    // The row's identity is the card id: keep the stored selection pointing at
    // the card that is now on screen, not at the one we just left.
    if (m_settings->selectedPeerCardId() == currentCard) {
        m_settings->setSelectedPeerCardId(cardId);
    }
    if (serverIndex == m_defaultServerIndex) {
        persistDefaultSelection(serverIndex);
        emit defaultServerIndexChanged(m_defaultServerIndex);
    }
    if (serverIndex == m_processedServerIndex) {
        emit processedServerChanged();
    }

    // The number on screen belonged to the protocol we just left: drop it and let
    // the next measurement of this card fill the badge (the list re-probes right
    // after the switch).
    m_healthResults.remove(serverIndex);

    emit dataChanged(index(serverIndex, 0), index(serverIndex, 0));
    qDebug() << "[PEERS] pool row switched to" << cardId << "at row" << serverIndex;
    return true;
}

void ServersModel::applyPeerProtocolFilter(const QString &protocol)
{
    if (protocol.isEmpty() || protocol == QStringLiteral("all")) {
        return;
    }
    for (int i = 0; i < m_servers.size(); ++i) {
        const QJsonObject row = m_servers.at(i).toObject();
        const QString currentCard = row.value(QStringLiteral("peerCardId")).toString();
        if (currentCard.isEmpty()) {
            continue; // a v1 config row keeps its own protocol
        }
        QString wanted;
        for (const QJsonValue &value : peerVariantsOf(row)) {
            const QJsonObject variant = value.toObject();
            if (canonicalServiceProtocol(variant.value(QStringLiteral("protocol")).toString()) == protocol) {
                wanted = variant.value(QStringLiteral("cardId")).toString();
                break;
            }
        }
        // Pools without the protocol keep their row: the view filters them out.
        if (!wanted.isEmpty() && wanted != currentCard) {
            setPeerPoolProtocol(i, wanted);
        }
    }
}

QVariantMap ServersModel::getServerDisplayData(const int serverIndex) const
{
    if (serverIndex < 0 || serverIndex >= m_servers.size()) {
        return {};
    }

    const QJsonObject server = m_servers.at(serverIndex).toObject();
    const QJsonObject apiConfig = server.value(configKey::apiConfig).toObject();
    const QJsonObject displayInfo = server.value(QStringLiteral("displayInfo")).toObject();
    const QString pool = displayInfo.value(QStringLiteral("pool")).toString();
    const QString localizedPool = localizedPoolName(pool, displayInfo.value(QStringLiteral("poolTranslation")).toObject(),
                                                    m_settings->getAppLanguage());

    QJsonObject authData = apiConfig.value(QStringLiteral("auth_data")).toObject();
    if (authData.isEmpty()) {
        authData = server.value(QStringLiteral("auth_data")).toObject();
    }
    QString subscriptionId = authData.value(apiDefs::key::apiKey).toString();
    if (subscriptionId.isEmpty()) {
        subscriptionId = authData.value(apiDefs::key::id).toString();
    }

    // The list works with canonical protocols only: the raw backend tag
    // (ServiceProtocolFilterRole, e.g. `amneziawgmobile`) is kept for the
    // connection logic, but it must never reach the selector — legacy rows used
    // to inject a second entry for the same protocol.
    const QString rawProtocol = data(serverIndex, ServiceProtocolFilterRole).toString();
    const QString protocol = canonicalServiceProtocol(rawProtocol);

    // Protocol variants of a peers pool: the row is one pool, the protocol is a
    // property of the row. `cardId` is what POST /v1/peers receives.
    // `peerProtocols` carries every canonical protocol the pool serves (not just
    // the selected one): the list narrows pools with it, so filtering by
    // WireGuard keeps a pool that serves both even while its row shows AWG.
    QVariantList peerVariants;
    QVariantList peerProtocols;
    for (const QJsonValue &value : peerVariantsOf(server)) {
        const QJsonObject variant = value.toObject();
        const QString cardId = variant.value(QStringLiteral("cardId")).toString();
        if (cardId.isEmpty()) {
            continue;
        }
        const QString variantProtocol =
                canonicalServiceProtocol(variant.value(QStringLiteral("protocol")).toString());
        peerVariants.append(QVariantMap {
                { QStringLiteral("cardId"), cardId },
                { QStringLiteral("proto"), variant.value(QStringLiteral("proto")).toString() },
                { QStringLiteral("protocol"), variantProtocol },
        });
        if (!variantProtocol.isEmpty() && !peerProtocols.contains(variantProtocol)) {
            peerProtocols.append(variantProtocol);
        }
    }
    const QString cardId = server.value(QStringLiteral("peerCardId")).toString();

    const QString name = data(serverIndex, NameRole).toString();
    return {
        { QStringLiteral("sourceIndex"), serverIndex },
        { QStringLiteral("name"), name },
        { QStringLiteral("groupName"), localizedPool.isEmpty() ? name : localizedPool },
        { QStringLiteral("pool"), pool },
        { QStringLiteral("subscriptionId"), subscriptionId },
        { QStringLiteral("connectionUuid"), apiConfig.value(QStringLiteral("connection_uuid")).toString() },
        { QStringLiteral("protocol"), protocol },
        { QStringLiteral("protocolRaw"), rawProtocol },
        { QStringLiteral("env"), data(serverIndex, ConnectionEnvRole).toString() },
        { QStringLiteral("countryCode"), data(serverIndex, CountryCodeRole).toString() },
        { QStringLiteral("countryName"), data(serverIndex, CountryNameRole).toString() },
        { QStringLiteral("healthLatency"), data(serverIndex, HealthLatencyRole).toInt() },
        { QStringLiteral("healthPing"), data(serverIndex, HealthPingRole).toBool() },
        { QStringLiteral("isServerFromGatewayApi"), data(serverIndex, IsServerFromGatewayApiRole).toBool() },
        // Origin tag of a peers row: one row per (env, pool) with the protocol
        // as a property of the row. `peerCardId` alone decides it, so rows
        // written before the pool variants existed are still kept out of the
        // legacy v1 groups. v1 config rows carry neither and never merge.
        { QStringLiteral("isPeerPool"), !cardId.isEmpty() },
        { QStringLiteral("peerCardId"), cardId },
        // Subscription of the pool, so the list can tell which subscription a
        // pooled row belongs to without parsing auth_data. Rows written before
        // `peerSubscriptionId` existed fall back to `subscriptionId` (both are
        // the subscription token, `auth_data.api_key` is written for pool rows).
        { QStringLiteral("peerSubscriptionId"), server.value(QStringLiteral("peerSubscriptionId")).toString() },
        { QStringLiteral("poolKey"), server.value(QStringLiteral("peerPoolKey")).toString() },
        { QStringLiteral("peerVariants"), peerVariants },
        { QStringLiteral("peerProtocols"), peerProtocols },
        { QStringLiteral("serverDescription"), data(serverIndex, ServerDescriptionRole).toString() },
    };
}

void ServersModel::reloadDefaultServerContainerConfig()
{
    if (m_defaultServerIndex < 0 || m_defaultServerIndex >= m_servers.size()) {
        return;
    }
    QJsonObject server = m_servers.at(m_defaultServerIndex).toObject();
    auto container = ContainerProps::containerFromString(server.value(config_key::defaultContainer).toString());

    auto containers = server.value(config_key::containers).toArray();

    auto config = m_settings->containerConfig(m_defaultServerIndex, container);
    for (auto i = 0; i < containers.size(); i++) {
        auto c = ContainerProps::containerFromString(containers.at(i).toObject().value(config_key::container).toString());
        if (c == container) {
            containers.replace(i, config);
            break;
        }
    }

    server.insert(config_key::containers, containers);
    editServer(server, m_defaultServerIndex);
}

void ServersModel::updateContainerConfig(const int containerIndex, const QJsonObject config)
{
    auto container = static_cast<DockerContainer>(containerIndex);
    QJsonObject server = m_servers.at(m_processedServerIndex).toObject();

    auto containers = server.value(config_key::containers).toArray();
    for (auto i = 0; i < containers.size(); i++) {
        auto c = ContainerProps::containerFromString(containers.at(i).toObject().value(config_key::container).toString());
        if (c == container) {
            containers.replace(i, config);
            break;
        }
    }

    server.insert(config_key::containers, containers);
    editServer(server, m_processedServerIndex);
}

void ServersModel::addContainerConfig(const int containerIndex, const QJsonObject config)
{
    auto container = static_cast<DockerContainer>(containerIndex);
    QJsonObject server = m_servers.at(m_processedServerIndex).toObject();

    auto containers = server.value(config_key::containers).toArray();
    containers.push_back(config);

    server.insert(config_key::containers, containers);

    auto defaultContainer = server.value(config_key::defaultContainer).toString();
    if (ContainerProps::containerFromString(defaultContainer) == DockerContainer::None
        && ContainerProps::containerService(container) != ServiceType::Other && ContainerProps::isSupportedByCurrentPlatform(container)) {
        server.insert(config_key::defaultContainer, ContainerProps::containerToString(container));
    }

    editServer(server, m_processedServerIndex);
}

void ServersModel::setDefaultContainer(const int serverIndex, const int containerIndex)
{
    auto container = static_cast<DockerContainer>(containerIndex);
    QJsonObject s = m_servers.at(serverIndex).toObject();
    s.insert(config_key::defaultContainer, ContainerProps::containerToString(container));
    editServer(s, serverIndex); // check
}

const QString ServersModel::getDefaultServerDefaultContainerName()
{
    auto defaultContainer = qvariant_cast<DockerContainer>(getDefaultServerData("defaultContainer"));

    QString protocolVersion;
    QString containerName = ContainerProps::containerHumanNames().value(defaultContainer);

    if (ContainerProps::isAwgContainer(defaultContainer)) {
        QJsonObject containerConfig = m_settings->containerConfig(m_defaultServerIndex, defaultContainer);
        QJsonObject serverProtocolConfig = containerConfig.value(ContainerProps::containerTypeToProtocolString(defaultContainer)).toObject();
        protocolVersion = ProtocolProps::getProtocolVersionString(serverProtocolConfig);

        auto isThirdPartyConfig = serverProtocolConfig.value(config_key::isThirdPartyConfig).toBool();
        if (defaultContainer == DockerContainer::Awg && !isThirdPartyConfig) {
            containerName = "AmneziaWG Legacy";
        }
    }

    return containerName + protocolVersion;
}

void ServersModel::clearCachedProfile(const DockerContainer container)
{
    m_settings->clearLastConnectionConfig(m_processedServerIndex, container);
    m_servers.replace(m_processedServerIndex, m_settings->server(m_processedServerIndex));
    if (m_processedServerIndex == m_defaultServerIndex) {
        updateDefaultServerContainersModel();
    }
    updateContainersModel();
}

bool ServersModel::isAmneziaDnsContainerInstalled(const int serverIndex) const
{
    QJsonObject server = m_servers.at(serverIndex).toObject();
    auto containers = server.value(config_key::containers).toArray();
    for (auto it = containers.begin(); it != containers.end(); it++) {
        if (it->toObject().value(config_key::container).toString() == ContainerProps::containerToString(DockerContainer::Dns)) {
            return true;
        }
    }
    return false;
}

QPair<QString, QString> ServersModel::getDnsPair(int serverIndex)
{
    QPair<QString, QString> dns;
    if (serverIndex < 0 || serverIndex >= m_servers.size()) {
        return dns;
    }

    if (m_settings->isDnsOverrideEnabled()) {
        const QString primary = m_settings->primaryDns();
        const QString secondary = m_settings->secondaryDns();
        if (NetworkUtilities::checkIPv4Format(primary) && NetworkUtilities::checkIPv4Format(secondary)) {
            qDebug() << "VpnConfigurator::getDnsForConfig using client override" << primary << secondary;
            return { primary, secondary };
        }
        qWarning() << "VpnConfigurator::getDnsForConfig ignoring invalid client override";
    }

    // Use the row being connected, not whichever card happens to be processed.
    const QJsonObject &server = m_servers.at(serverIndex).toObject();
    const auto containers = server.value(config_key::containers).toArray();
    bool isDnsContainerInstalled = false;
    for (const QJsonValue &container : containers) {
        if (ContainerProps::containerFromString(container.toObject().value(config_key::container).toString()) == DockerContainer::Dns) {
            isDnsContainerInstalled = true;
        }
    }

    dns.first = server.value(config_key::dns1).toString();
    dns.second = server.value(config_key::dns2).toString();

    if (dns.first.isEmpty() || !NetworkUtilities::checkIPv4Format(dns.first)) {
        if (m_isAmneziaDnsEnabled && isDnsContainerInstalled) {
            dns.first = protocols::dns::amneziaDnsIp;
        } else
            dns.first = m_settings->primaryDns();
    }
    if (dns.second.isEmpty() || !NetworkUtilities::checkIPv4Format(dns.second)) {
        dns.second = m_settings->secondaryDns();
    }

    qDebug() << "VpnConfigurator::getDnsForConfig" << dns.first << dns.second;
    return dns;
}

QStringList ServersModel::getAllInstalledServicesName(const int serverIndex)
{
    QStringList servicesName;
    QJsonObject server = m_servers.at(serverIndex).toObject();
    const auto apiConfig = server.value(configKey::apiConfig).toObject();
    if (!apiConfig.isEmpty()) {
        const QString protocol = apiConfig.value(configKey::serviceProtocol).toString().toUpper();
        if (!protocol.isEmpty()) {
            servicesName.append(protocol);
        }
    } else {
        const auto containers = server.value(config_key::containers).toArray();
        for (auto it = containers.begin(); it != containers.end(); it++) {
            auto container = ContainerProps::containerFromString(it->toObject().value(config_key::container).toString());
            if (ContainerProps::containerService(container) == ServiceType::Other) {
                if (container == DockerContainer::Dns) {
                    servicesName.append("DNS");
                } else if (container == DockerContainer::Sftp) {
                    servicesName.append("SFTP");
                } else if (container == DockerContainer::TorWebSite) {
                    servicesName.append("TOR");
                } else if (container == DockerContainer::Socks5Proxy) {
                    servicesName.append("SOCKS5");
                }
            }
        }
    }
    servicesName.sort();
    return servicesName;
}

void ServersModel::toggleAmneziaDns(bool enabled)
{
    m_isAmneziaDnsEnabled = enabled;
    emit defaultServerDescriptionChanged();
}

bool ServersModel::isServerFromApiAlreadyExists(const quint16 crc)
{
    for (const auto &server : std::as_const(m_servers)) {
        if (static_cast<quint16>(server.toObject().value(config_key::crc).toInt()) == crc) {
            return true;
        }
    }
    return false;
}

bool ServersModel::isServerFromApiAlreadyExists(const QString &userCountryCode, const QString &serviceType, const QString &serviceProtocol,
                                                const QString &subscriptionId)
{
    for (const auto &server : std::as_const(m_servers)) {
        const auto serverObject = server.toObject();
        const auto apiConfig = serverObject.value(configKey::apiConfig).toObject();
        if (apiConfig.value(configKey::userCountryCode).toString() != userCountryCode
            || apiConfig.value(configKey::serviceType).toString() != serviceType
            || apiConfig.value(configKey::serviceProtocol).toString() != serviceProtocol) {
            continue;
        }
        if (!subscriptionId.isEmpty()) {
            // subscription-scoped: the same country/service/protocol in another
            // subscription is a different server, not a duplicate
            QJsonObject authData = apiConfig.value(QStringLiteral("auth_data")).toObject();
            if (authData.isEmpty()) {
                authData = serverObject.value(QStringLiteral("auth_data")).toObject();
            }
            QString existingId = authData.value(apiDefs::key::apiKey).toString();
            if (existingId.isEmpty()) {
                existingId = authData.value(apiDefs::key::id).toString();
            }
            if (existingId != subscriptionId) {
                continue;
            }
        }
        return true;
    }
    return false;
}

bool ServersModel::isServerFromApiAlreadyExists(const QString &connectionUuid)
{
    if (connectionUuid.isEmpty()) {
        return false;
    }
    for (const auto &server : std::as_const(m_servers)) {
        const auto apiConfig = server.toObject().value(configKey::apiConfig).toObject();
        if (apiConfig.value("connection_uuid").toString() == connectionUuid) {
            return true;
        }
    }
    return false;
}

bool ServersModel::isServerFromApiAlreadyExists(const QString &name, const QString &description) const
{
    for (const auto &server : std::as_const(m_servers)) {
        const auto serverObject = server.toObject();
        if (serverObject.value(config_key::name).toString() == name
            && serverObject.value(config_key::description).toString() == description) {
            return true;
        }
    }
    return false;
}

bool ServersModel::hasServerWithVpnKey(const QString &vpnKey) const
{
    const QString normalizedInput = normalizeVpnKey(vpnKey);
    if (normalizedInput.isEmpty()) {
        return false;
    }

    for (const auto &server : std::as_const(m_servers)) {
        const auto apiConfig = server.toObject().value(configKey::apiConfig).toObject();
        const QString existingKey = normalizeVpnKey(apiConfig.value(apiDefs::key::vpnKey).toString());
        if (!existingKey.isEmpty() && existingKey == normalizedInput) {
            return true;
        }
    }
    return false;
}

bool ServersModel::serverHasInstalledContainers(const int serverIndex) const
{
    QJsonObject server = m_servers.at(serverIndex).toObject();
    const auto containers = server.value(config_key::containers).toArray();
    for (auto it = containers.begin(); it != containers.end(); it++) {
        auto container = ContainerProps::containerFromString(it->toObject().value(config_key::container).toString());
        if (ContainerProps::containerService(container) == ServiceType::Vpn) {
            return true;
        }
        if (container == DockerContainer::SSXray) {
            return true;
        }
    }
    return false;
}

bool ServersModel::serverHasUsableConfig(const int serverIndex) const
{
    const QJsonObject server = m_servers.at(serverIndex).toObject();
    // the "container" field holds the container name ("amnezia-awg") — not the
    // protocol alias ("awg") that containerTypeToString returns
    const QString defaultContainerName = ContainerProps::containerToString(
            ContainerProps::containerFromString(server.value(config_key::defaultContainer).toString()));
    const auto containers = server.value(config_key::containers).toArray();
    for (const QJsonValue &containerValue : containers) {
        const QJsonObject containerObject = containerValue.toObject();
        if (containerObject.value(config_key::container).toString() != defaultContainerName) {
            continue;
        }
        // the entry exists — but does it hold an actual protocol config? Mirror the
        // extraction order of VpnConfigurationController::createVpnConfiguration:
        // a nested protocol object first, then the flat container fields (CDN form)
        for (auto it = containerObject.begin(); it != containerObject.end(); ++it) {
            const QJsonObject protocolObject = it.value().toObject();
            if (!protocolObject.value(config_key::last_config).toString().isEmpty()
                || !protocolObject.value(config_key::config).toString().isEmpty()) {
                return true;
            }
        }
        return !containerObject.value(config_key::last_config).toString().isEmpty()
                || !containerObject.value(config_key::config).toString().isEmpty();
    }
    return false;
}

QVariant ServersModel::getDefaultServerData(const QString roleString)
{
    auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); it++) {
        if (QString(it.value()) == roleString) {
            return data(m_defaultServerIndex, it.key());
        }
    }

    return {};
}

QVariant ServersModel::getProcessedServerData(const QString roleString)
{
    auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); it++) {
        if (QString(it.value()) == roleString) {
            return data(m_processedServerIndex, it.key());
        }
    }

    return {};
}

QVariant ServersModel::getServerData(const int serverIndex, const QString roleString)
{
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); it++) {
        if (QString(it.value()) == roleString) {
            return data(serverIndex, it.key());
        }
    }
    return {};
}

bool ServersModel::setProcessedServerData(const QString &roleString, const QVariant &value)
{
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); it++) {
        if (QString(it.value()) == roleString) {
            return setData(m_processedServerIndex, value, it.key());
        }
    }

    return false;
}

QString ServersModel::localizedPoolName(const QString &pool, const QJsonObject &translation, const QLocale &locale)
{
    QString code;
    switch (locale.language()) {
    case QLocale::Russian: code = QStringLiteral("ru"); break;
    case QLocale::Ukrainian: code = QStringLiteral("uk"); break;
    default: code = QStringLiteral("en"); break;
    }

    QString value = translation.value(code).toString().trimmed();
    if (value.isEmpty() && code == QLatin1String("uk")) {
        // `ua` is accepted as an alias of `uk`
        value = translation.value(QStringLiteral("ua")).toString().trimmed();
    }
    if (!value.isEmpty()) {
        return value;
    }

    return pool.trimmed();
}

void ServersModel::onTranslationsUpdated()
{
    if (m_servers.isEmpty()) {
        return;
    }
    emit dataChanged(index(0), index(static_cast<int>(m_servers.size()) - 1));
    emit defaultServerNameChanged();
}

bool ServersModel::isDefaultServerDefaultContainerHasSplitTunneling()
{
    auto server = m_servers.at(m_defaultServerIndex).toObject();
    auto defaultContainer = ContainerProps::containerFromString(server.value(config_key::defaultContainer).toString());

    auto containers = server.value(config_key::containers).toArray();
    for (auto i = 0; i < containers.size(); i++) {
        auto container = containers.at(i).toObject();
        if (container.value(config_key::container).toString() != ContainerProps::containerToString(defaultContainer)) {
            continue;
        }
        if (ContainerProps::isAwgContainer(defaultContainer) || defaultContainer == DockerContainer::WireGuard) {
            QJsonObject serverProtocolConfig = container.value(ContainerProps::containerTypeToProtocolString(defaultContainer)).toObject();
            QString clientProtocolConfigString = serverProtocolConfig.value(config_key::last_config).toString();
            QJsonObject clientProtocolConfig = QJsonDocument::fromJson(clientProtocolConfigString.toUtf8()).object();
            // full tunnel = AllowedIPs covers all IPv4 (0.0.0.0/0); anything
            // narrower is a split-tunnel config. IPv6 ("/0" or "::/0") is not
            // required — our API configs are IPv4-only by design
            return (clientProtocolConfigString.contains("AllowedIPs") && !clientProtocolConfigString.contains("0.0.0.0/0"))
                    || (!clientProtocolConfig.value(config_key::allowed_ips).toArray().isEmpty()
                        && !clientProtocolConfig.value(config_key::allowed_ips).toArray().contains("0.0.0.0/0"));
        }
    }
    return false;
}

bool ServersModel::isServerFromApi(const int serverIndex)
{
    return data(serverIndex, IsServerFromTelegramApiRole).toBool() || data(serverIndex, IsServerFromGatewayApiRole).toBool();
}

bool ServersModel::hasServersFromGatewayApi()
{
    return !m_gatewayStacks.isEmpty();
}

bool ServersModel::GatewayStacks::operator==(const GatewayStacks &other) const
{
    return userCountryCodes == other.userCountryCodes && serviceTypes == other.serviceTypes;
}

QJsonObject ServersModel::GatewayStacks::toJson() const
{
    QJsonObject obj;
    if (!userCountryCodes.isEmpty()) {
        obj.insert(configKey::userCountryCode, QJsonArray::fromStringList(userCountryCodes.values()));
    }
    if (!serviceTypes.isEmpty()) {
        obj.insert(configKey::serviceType, QJsonArray::fromStringList(serviceTypes.values()));
    }
    return obj;
}

void ServersModel::recomputeGatewayStacks()
{
    const bool wasEmpty = m_gatewayStacks.isEmpty();
    GatewayStacks computed;
    bool hasNewTags = false;

    for (int i = 0; i < m_servers.count(); ++i) {
        if (data(i, IsServerFromGatewayApiRole).toBool()) {
            const QJsonObject server = m_servers.at(i).toObject();
            const QJsonObject apiConfig = server.value(configKey::apiConfig).toObject();

            const QString userCountryCode = apiConfig.value(configKey::userCountryCode).toString();
            const QString serviceType = apiConfig.value(configKey::serviceType).toString();

            if (!userCountryCode.isEmpty()) {
                if (!m_gatewayStacks.userCountryCodes.contains(userCountryCode)) {
                    hasNewTags = true;
                }
                computed.userCountryCodes.insert(userCountryCode);
            }

            if (!serviceType.isEmpty()) {
                if (!m_gatewayStacks.serviceTypes.contains(serviceType)) {
                    hasNewTags = true;
                }
                computed.serviceTypes.insert(serviceType);
            }
        }
    }

    m_gatewayStacks = std::move(computed);
    if (hasNewTags) {
        emit gatewayStacksExpanded();
    }

    if (wasEmpty != m_gatewayStacks.isEmpty()) {
        emit hasServersFromGatewayApiChanged();
    }
}

bool ServersModel::isApiKeyExpired(const int serverIndex)
{
    auto serverConfig = m_servers.at(serverIndex).toObject();
    auto apiConfig = serverConfig.value(configKey::apiConfig).toObject();

    auto publicKeyInfo = apiConfig.value(configKey::publicKeyInfo).toObject();
    const QString expiresAt = publicKeyInfo.value(configKey::expiresAt).toString();
    if (expiresAt.isEmpty()) {
        publicKeyInfo.insert(configKey::expiresAt, QDateTime::currentDateTimeUtc().addDays(1).toString(Qt::ISODate));
        apiConfig.insert(configKey::publicKeyInfo, publicKeyInfo);
        serverConfig.insert(configKey::apiConfig, apiConfig);
        editServer(serverConfig, serverIndex);

        return false;
    }

    auto expiresAtDateTime = QDateTime::fromString(expiresAt, Qt::ISODate).toUTC();
    if (expiresAtDateTime < QDateTime::currentDateTimeUtc()) {
        return true;
    }
    return false;
}

void ServersModel::removeApiConfig(const int serverIndex)
{
    auto serverConfig = getServerConfig(serverIndex);

#if defined(Q_OS_IOS) || defined(MACOS_NE)
    QString vpncName = QString("%1 (%2) %3")
                               .arg(serverConfig[config_key::description].toString())
                               .arg(serverConfig[config_key::hostName].toString())
                               .arg(serverConfig[config_key::vpnproto].toString());

    Dopamine::removeVPNC(vpncName.toStdString());
#endif

    serverConfig.remove(config_key::dns1);
    serverConfig.remove(config_key::dns2);
    serverConfig.remove(config_key::containers);
    serverConfig.remove(config_key::hostName);

    auto apiConfig = serverConfig.value(configKey::apiConfig).toObject();
    apiConfig.remove(configKey::publicKeyInfo);
    serverConfig.insert(configKey::apiConfig, apiConfig);

    serverConfig.insert(config_key::defaultContainer, ContainerProps::containerToString(DockerContainer::None));

    editServer(serverConfig, serverIndex);
}

const QString ServersModel::getDefaultServerImagePathCollapsed()
{
    const auto server = m_servers.at(m_defaultServerIndex).toObject();
    const auto countryCode = data(m_defaultServerIndex, CountryCodeRole).toString();
    if (countryCode.isEmpty()) {
        return "";
    }
    return QString("qrc:/countriesFlags/images/flagKit/%1.svg").arg(countryCode);
}

bool ServersModel::processedServerIsPremium() const
{
    return apiUtils::isPremiumServer(getServerConfig(m_processedServerIndex));
}

bool ServersModel::isAdVisible()
{
    return data(m_defaultServerIndex, IsAdVisibleRole).toBool();
}

QString ServersModel::adHeader()
{
    return data(m_defaultServerIndex, AdHeaderRole).toString();
}

QString ServersModel::adDescription()
{
    return data(m_defaultServerIndex, AdDescriptionRole).toString();
}

QStringList ServersModel::availableProtocols() const
{
    return m_availableProtocols;
}

QStringList ServersModel::availableEnvs() const
{
    return m_availableEnvs;
}

QStringList ServersModel::availableSubscriptions() const
{
    return m_availableSubscriptions;
}

QStringList ServersModel::availableProtocolsForEnv(const QString &env, const QString &subscriptionId) const
{
    // the server list's sentinel for its "Other" bucket (ServersListView.qml)
    const bool otherBucket = subscriptionId == QStringLiteral("__other__");
    QSet<QString> protocols;
    for (int i = 0; i < m_servers.size(); ++i) {
        if (!env.isEmpty() && data(index(i), ConnectionEnvRole).toString() != env) {
            continue;
        }
        if (!subscriptionId.isEmpty()) {
            const QString rowSubscription = subscriptionIdOfServer(m_servers.at(i).toObject());
            // The sentinel is the list's "Other" bucket: servers outside every
            // known subscription (share imports, hand-added configs). The same
            // rule variantMatches() in ServersListView.qml uses for that bucket.
            const bool matches = otherBucket ? rowSubscription.isEmpty()
                                                    || !m_availableSubscriptions.contains(rowSubscription)
                                             : rowSubscription == subscriptionId;
            if (!matches) {
                continue;
            }
        }
        // Canonical only: the selector must not gain a second entry for the same
        // protocol because a legacy row carries the raw tag (`amneziawgmobile`).
        const QString protocol = canonicalServiceProtocol(
                data(index(i), ServiceProtocolFilterRole).toString());
        if (!protocol.isEmpty()) {
            protocols.insert(protocol);
        }
        // A pool serves the protocols of all its variants, not only the one its
        // row currently points at: reading just the active protocol hid plain
        // WireGuard from the selector entirely, because a pool that serves both
        // defaults to AmneziaWG.
        for (const QJsonValue &variant : peerVariantsOf(m_servers.at(i).toObject())) {
            const QString variantProtocol = canonicalServiceProtocol(
                    variant.toObject().value(QStringLiteral("protocol")).toString());
            if (!variantProtocol.isEmpty()) {
                protocols.insert(variantProtocol);
            }
        }
    }
    QStringList sorted = protocols.values();
    sorted.sort();
    return sorted;
}

void ServersModel::setHealthResult(int serverIndex, int latencyMs, const QString &cardId)
{
    // a probe run outlives a subscription refresh: its row indices belong to the
    // list it started on, and a stale one must not reach dataChanged()
    if (serverIndex < 0 || serverIndex >= m_servers.size()) {
        return;
    }
    // The result belongs to the card the probe was queued for, so a value that
    // arrives after the row's protocol pill was switched is dropped instead of
    // landing on the new protocol.
    const QString rowCard = peerCardId(serverIndex);
    const QString measuredCard = cardId.isNull() ? rowCard : cardId;
    if (measuredCard != rowCard) {
        return;
    }
    // multi-IP rows get one result per probed address — keep the best:
    // a live address means the server is reachable, and the lowest RTT wins
    const auto it = m_healthResults.constFind(serverIndex);
    if (it != m_healthResults.constEnd() && it->cardId == measuredCard) {
        const int prev = it->latencyMs;
        if ((prev >= 0 && (latencyMs < 0 || latencyMs >= prev)) || (prev < 0 && latencyMs < 0)) {
            return;
        }
    }
    m_healthResults.insert(serverIndex, HealthResult { measuredCard, latencyMs });
    const QModelIndex modelIndex = index(serverIndex);
    emit dataChanged(modelIndex, modelIndex, { HealthLatencyRole });
}

bool ServersModel::probeIsFresh() const
{
    if (!m_settings) {
        return false;
    }
    const qint64 last = m_settings->lastProbeRunMs();
    return last > 0 && QDateTime::currentMSecsSinceEpoch() - last < 60 * 1000;
}

void ServersModel::markProbeRun()
{
    if (m_settings) {
        m_settings->setLastProbeRunMs(QDateTime::currentMSecsSinceEpoch());
    }
}

void ServersModel::loadEndpointHealthCache()
{
    if (!m_settings) {
        return;
    }
    const QJsonObject cache = QJsonDocument::fromJson(m_settings->endpointHealthCache().toUtf8()).object();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const qint64 atMs = static_cast<qint64>(entry.value(QStringLiteral("at")).toDouble());
        if (atMs <= 0 || now - atMs > kEndpointHealthTtlMs) {
            continue; // старьё не тащим
        }
        EndpointHealth health;
        health.rttMs = entry.value(QStringLiteral("rtt")).toInt(-1);
        health.atMs = atMs;
        m_endpointHealth.insert(it.key(), health);
    }
    ++m_endpointHealthRevision;
    emit endpointHealthRevisionChanged();
    qDebug() << "[HEALTH] restored" << m_endpointHealth.size() << "cached results";
}

void ServersModel::saveEndpointHealthCache()
{
    if (!m_settings) {
        return;
    }
    QJsonObject cache;
    for (auto it = m_endpointHealth.constBegin(); it != m_endpointHealth.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("rtt"), it->rttMs);
        entry.insert(QStringLiteral("at"), static_cast<double>(it->atMs));
        cache.insert(it.key(), entry);
    }
    m_settings->setEndpointHealthCache(QString::fromUtf8(QJsonDocument(cache).toJson(QJsonDocument::Compact)));
}

void ServersModel::setEndpointHealthResult(const QString &host, const int port, int latencyMs, int attempts)
{
    if (host.isEmpty() || port <= 0) {
        return;
    }
    EndpointHealth entry;
    entry.rttMs = latencyMs;
    entry.atMs = QDateTime::currentMSecsSinceEpoch();
    entry.attempts = qMax(1, attempts);
    m_endpointHealth.insert(endpointKey(host, port), entry);
    ++m_endpointHealthRevision;
    emit endpointHealthRevisionChanged();
}

void ServersModel::clearEndpointHealthResults()
{
    m_endpointHealth.clear();
}

QVariantMap ServersModel::endpointHealth(const QString &host, const int port) const
{
    QVariantMap result;
    const auto it = m_endpointHealth.constFind(endpointKey(host, port));
    if (it == m_endpointHealth.constEnd()) {
        result.insert(QStringLiteral("rtt"), -2);
        result.insert(QStringLiteral("state"), QStringLiteral("unknown"));
        result.insert(QStringLiteral("ageSec"), -1);
        return result;
    }

    const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - it->atMs;
    const bool fresh = ageMs <= kEndpointHealthTtlMs;

    // результат старше TTL не считаем фактом: сеть меняется, а «мёртвый» адрес
    // не должен оставаться мёртвым навсегда
    QString state = QStringLiteral("unknown");
    if (fresh) {
        state = it->rttMs >= 0 ? QStringLiteral("ok") : QStringLiteral("unreachable");
    }

    result.insert(QStringLiteral("rttLast"), it->rttMs);
    result.insert(QStringLiteral("rtt"), fresh ? it->rttMs : -2);
    result.insert(QStringLiteral("state"), state);
    result.insert(QStringLiteral("ageSec"), static_cast<int>(ageMs / 1000));
    result.insert(QStringLiteral("attempts"), it->attempts);
    return result;
}

void ServersModel::clearEndpointHealth(const QString &host, const int port)
{
    if (m_endpointHealth.remove(endpointKey(host, port)) > 0) {
        qDebug() << "[HEALTH] endpoint verdict cleared by user:" << host << port;
    }
}

// hysteria2 dial target: the backend stores an xray-style outbound whose
// settings.servers[0] carries {address, port, serverName}; the translated form
// keeps settings.address/settings.port. Both shapes are handled here so the
// server card can show the same "Endpoint / Port" pair as the AWG rows.
QJsonObject ServersModel::hysteriaDialOutbound(const int index) const
{
    if (index < 0 || index >= m_servers.size()) {
        return {};
    }
    const QJsonObject config = getServerConfig(index);
    const QJsonArray containers = config.value(QStringLiteral("containers")).toArray();
    for (const QJsonValue &containerValue : containers) {
        const QJsonObject container = containerValue.toObject();
        const QJsonObject xray = container.value(QStringLiteral("xray")).toObject();
        QJsonObject root = QJsonDocument::fromJson(xray.value(QStringLiteral("config")).toString().toUtf8()).object();
        if (root.isEmpty()) {
            root = QJsonDocument::fromJson(container.value(QStringLiteral("config")).toString().toUtf8()).object();
        }
        const QJsonArray outbounds = root.value(QStringLiteral("outbounds")).toArray();
        for (const QJsonValue &outboundValue : outbounds) {
            const QJsonObject outbound = outboundValue.toObject();
            const QString protocol = outbound.value(QStringLiteral("protocol")).toString();
            if (protocol == QLatin1String("hysteria2") || protocol == QLatin1String("hysteria")) {
                return outbound;
            }
        }
    }
    return {};
}

int ServersModel::hysteriaDialPort(const int index) const
{
    const QJsonObject outbound = hysteriaDialOutbound(index);
    if (outbound.isEmpty()) {
        return 0;
    }
    const QJsonObject settings = outbound.value(QStringLiteral("settings")).toObject();
    const QJsonArray servers = settings.value(QStringLiteral("servers")).toArray();
    if (!servers.isEmpty()) {
        const QJsonValue portValue = servers.at(0).toObject().value(QStringLiteral("port"));
        const int port = portValue.isString() ? portValue.toString().toInt() : portValue.toInt();
        if (port > 0) {
            return port;
        }
    }
    const QJsonValue direct = settings.value(QStringLiteral("port"));
    return direct.isString() ? direct.toString().toInt() : direct.toInt();
}

QString ServersModel::hysteriaDialDomain(const int index) const
{
    const QJsonObject outbound = hysteriaDialOutbound(index);
    if (outbound.isEmpty()) {
        return {};
    }
    const QJsonArray servers = outbound.value(QStringLiteral("settings")).toObject()
                                       .value(QStringLiteral("servers")).toArray();
    if (!servers.isEmpty()) {
        const QString serverName = servers.at(0).toObject().value(QStringLiteral("serverName")).toString();
        if (!serverName.isEmpty()) {
            return serverName;
        }
    }
    return outbound.value(QStringLiteral("streamSettings")).toObject()
            .value(QStringLiteral("tlsSettings")).toObject()
            .value(QStringLiteral("serverName")).toString();
}

QJsonObject ServersModel::proxyDialOutbound(const int index) const
{
    if (index < 0 || index >= m_servers.size()) {
        return {};
    }
    const QJsonObject config = getServerConfig(index);
    const QJsonArray containers = config.value(QStringLiteral("containers")).toArray();
    for (const QJsonValue &containerValue : containers) {
        const QJsonObject container = containerValue.toObject();
        const QJsonObject xray = container.value(QStringLiteral("xray")).toObject();
        QJsonObject root = QJsonDocument::fromJson(xray.value(QStringLiteral("config")).toString().toUtf8()).object();
        if (root.isEmpty()) {
            root = QJsonDocument::fromJson(container.value(QStringLiteral("config")).toString().toUtf8()).object();
        }
        for (const QJsonValue &outboundValue : root.value(QStringLiteral("outbounds")).toArray()) {
            const QJsonObject outbound = outboundValue.toObject();
            const QJsonArray servers = outbound.value(QStringLiteral("settings")).toObject()
                                               .value(QStringLiteral("servers")).toArray();
            for (const QJsonValue &serverValue : servers) {
                const QJsonObject server = serverValue.toObject();
                if (!server.value(QStringLiteral("address")).toString().isEmpty()) {
                    return outbound;
                }
            }
        }
    }
    return {};
}

QString ServersModel::proxyDialHost(const int index) const
{
    const QJsonObject outbound = proxyDialOutbound(index);
    if (outbound.isEmpty()) {
        return {};
    }
    const QJsonObject server = outbound.value(QStringLiteral("settings")).toObject()
                                       .value(QStringLiteral("servers")).toArray()
                                       .at(0).toObject();
    // serverName is what TLS verifies (the domain in the config); address is the
    // dialled IP. A probe wants the name the user sees, exactly like hysteria.
    const QString serverName = server.value(QStringLiteral("serverName")).toString();
    if (!serverName.isEmpty()) {
        return serverName;
    }
    return server.value(QStringLiteral("address")).toString();
}

int ServersModel::endpointPort(const int serverIndex) const
{
    const QJsonObject server = getServerConfig(serverIndex);
    const int stored = server.value(config_key::port).toInt();
    if (stored > 0) {
        return stored;
    }
    const int hysteria = hysteriaDialPort(serverIndex);
    if (hysteria > 0) {
        return hysteria;
    }
    const QJsonObject outbound = proxyDialOutbound(serverIndex);
    if (!outbound.isEmpty()) {
        const QJsonValue portValue = outbound.value(QStringLiteral("settings")).toObject()
                                              .value(QStringLiteral("servers")).toArray()
                                              .at(0).toObject().value(QStringLiteral("port"));
        const int port = portValue.isString() ? portValue.toString().toInt() : portValue.toInt();
        if (port > 0) {
            return port;
        }
    }
    return 443;
}

QString ServersModel::hysteriaDialHost(const int index) const
{
    const QJsonObject outbound = hysteriaDialOutbound(index);
    if (outbound.isEmpty()) {
        return {};
    }
    const QJsonObject settings = outbound.value(QStringLiteral("settings")).toObject();
    const QJsonArray servers = settings.value(QStringLiteral("servers")).toArray();
    if (!servers.isEmpty()) {
        const QJsonObject server = servers.at(0).toObject();
        // serverName is the domain TLS verifies against; address is what gets
        // dialled (an IP for pool rows). The card names the domain.
        const QString serverName = server.value(QStringLiteral("serverName")).toString();
        if (!serverName.isEmpty()) {
            return serverName;
        }
        const QString address = server.value(QStringLiteral("address")).toString();
        if (!address.isEmpty()) {
            return address;
        }
    }
    const QString tlsName = outbound.value(QStringLiteral("streamSettings")).toObject()
                                    .value(QStringLiteral("tlsSettings")).toObject()
                                    .value(QStringLiteral("serverName")).toString();
    if (!tlsName.isEmpty()) {
        return tlsName;
    }
    QString address = settings.value(QStringLiteral("address")).toString();
    const int colon = address.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && address.mid(colon + 1).toInt() > 0) {
        address = address.left(colon);
    }
    return address;
}

int ServersModel::serverPort(int index) const
{
    if (index < 0 || index >= m_servers.size()) {
        return 0;
    }
    const QJsonObject config = getServerConfig(index);
    const int port = config.value(QStringLiteral("port")).toInt();
    if (port > 0) {
        return port;
    }
    // hysteria2 keeps its port in the outbound (settings.servers[].port), not in
    // the container's last_config — without this the card showed no port at all.
    if (data(index, ServiceProtocolFilterRole).toString() == QLatin1String("hysteria2")) {
        const int hysteriaPort = hysteriaDialPort(index);
        if (hysteriaPort > 0) {
            return hysteriaPort;
        }
    }
    // у серверов из API верхнеуровневого port нет — он живёт в контейнере.
    // Порядок важен: last_config.port — это порт, которым реально поднимается
    // туннель и по которому строятся ключи проб; если взять другой, карточка
    // будет спрашивать здоровье адреса по несуществующему ключу.
    for (const QJsonValue &containerValue : config.value(QStringLiteral("containers")).toArray()) {
        const QJsonObject container = containerValue.toObject();
        // Сначала контейнер, который реально используется этой строкой
        // (defaultContainer), и только потом остальные ключи. Иначе первый
        // попавшийся ключ контейнера отдавал чужой порт (например 8443 вместо
        // 51820), и туннель падал на старте с InternalError.
        const auto rowContainer = ContainerProps::containerFromString(
                config.value(config_key::defaultContainer).toString());
        const QString preferredKey = ContainerProps::containerTypeToString(rowContainer);
        if (!preferredKey.isEmpty()) {
            const QJsonObject preferred = container.value(preferredKey).toObject();
            if (!preferred.isEmpty()) {
                const QJsonObject lastConfig = QJsonDocument::fromJson(
                        preferred.value(QStringLiteral("last_config")).toString().toUtf8()).object();
                const QJsonValue portValue = lastConfig.value(QStringLiteral("port"));
                const int lastPort = portValue.isString() ? portValue.toString().toInt() : portValue.toInt();
                if (lastPort > 0) {
                    return lastPort;
                }
                const int direct = preferred.value(QStringLiteral("port")).toInt();
                if (direct > 0) {
                    return direct;
                }
            }
        }

        for (auto it = container.constBegin(); it != container.constEnd(); ++it) {
            const QString &name = it.key();
            const QJsonObject protocolConfig = it.value().toObject();
            if (protocolConfig.isEmpty() || name == QStringLiteral("container")) {
                continue;
            }
            const QJsonObject lastConfig = QJsonDocument::fromJson(protocolConfig
                                                                   .value(QStringLiteral("last_config"))
                                                                   .toString().toUtf8()).object();
            const QJsonValue portValue = lastConfig.value(QStringLiteral("port"));
            const int lastPort = portValue.isString() ? portValue.toString().toInt() : portValue.toInt();
            if (lastPort > 0) {
                return lastPort;
            }
            const int directPort = protocolConfig.value(QStringLiteral("port")).toInt();
            if (directPort > 0) {
                return directPort;
            }
        }
    }

    // VLESS/SS: порт лежит в outbound'ах xray-контейнера
    for (const QJsonValue &containerValue : config.value(QStringLiteral("containers")).toArray()) {
        const QJsonObject container = containerValue.toObject();
        const QJsonObject xray = container.value(QStringLiteral("xray")).toObject();
        if (xray.isEmpty()) {
            continue;
        }
        const QJsonObject root = QJsonDocument::fromJson(xray.value(QStringLiteral("config"))
                                                         .toString().toUtf8()).object();
        const QJsonArray outbounds = root.value(QStringLiteral("outbounds")).toArray();
        for (const QJsonValue &outboundValue : outbounds) {
            const QJsonObject outbound = outboundValue.toObject();
            const QJsonArray vnext = outbound.value(QStringLiteral("settings")).toObject()
                                            .value(QStringLiteral("vnext")).toArray();
            for (const QJsonValue &vnextValue : vnext) {
                const int port = vnextValue.toObject().value(QStringLiteral("port")).toInt();
                if (port > 0) {
                    return port;
                }
            }
            const int serverPortValue = outbound.value(QStringLiteral("settings")).toObject()
                                               .value(QStringLiteral("port")).toInt();
            if (serverPortValue > 0) {
                return serverPortValue;
            }
        }
    }
    return 0;
}

int ServersModel::getProcessedServerPort()
{
    return serverPort(m_processedServerIndex);
}

// node_ports[ip] — порт конкретного адреса пула. Нет записи (или поле не
// пришло) → порт протокола, то есть прежнее поведение.
int ServersModel::portForNodeIp(const QJsonObject &serverConfig, const QString &ip, int fallbackPort)
{
    // The single endpoint shape wins when the payload carries it; a row
    // without it keeps reading node_ports exactly as before.
    if (const int port = NodeEndpoints::portFor(serverConfig, ip); port > 0) {
        return port;
    }
    const QJsonValue raw = serverConfig.value(QStringLiteral("node_ports"));

    // Форма 1 (канон): карта "ip": port. Самодостаточна: не зависит от порядка.
    if (raw.isObject()) {
        const QJsonValue value = raw.toObject().value(ip);
        if (!value.isUndefined() && !value.isNull()) {
            const int port = value.isString() ? value.toString().toInt() : value.toInt();
            if (port > 0 && port <= 65535) {
                return port;
            }
        }
        return fallbackPort;
    }

    // Форма 2 (компактная): массив портов, выровненный по node_ips.
    // Принимаем и её, чтобы бэкенд мог отдавать без дублирования адресов.
    if (raw.isArray()) {
        const QJsonArray ports = raw.toArray();
        const QJsonArray ips = serverConfig.value(QStringLiteral("node_ips")).toArray();
        const int index = [&]() {
            for (int i = 0; i < ips.size(); ++i) {
                if (ips.at(i).toString() == ip) {
                    return i;
                }
            }
            return -1;
        }();
        if (index >= 0 && index < ports.size()) {
            const QJsonValue value = ports.at(index);
            const int port = value.isString() ? value.toString().toInt() : value.toInt();
            if (port > 0 && port <= 65535) {
                return port;
            }
        }
    }
    return fallbackPort;
}

int ServersModel::livePortForIp(const QString &ip, int currentPort) const
{
    if (ip.isEmpty()) {
        return 0;
    }
    // Если для текущего порта есть свежий положительный замер — он и правильный.
    if (currentPort > 0) {
        const auto cur = m_endpointHealth.constFind(endpointKey(ip, currentPort));
        if (cur != m_endpointHealth.constEnd() && cur->rttMs >= 0) {
            return 0;
        }
    }
    int bestPort = 0;
    int bestRtt = INT_MAX;
    const QString prefix = ip + QLatin1Char(':');
    for (auto it = m_endpointHealth.constBegin(); it != m_endpointHealth.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) {
            continue;
        }
        const bool ok = it->rttMs >= 0;
        const int port = it.key().mid(prefix.size()).toInt();
        if (ok && port > 0 && it->rttMs < bestRtt) {
            bestRtt = it->rttMs;
            bestPort = port;
        }
    }
    return bestPort;
}

QVariantMap ServersModel::endpointHealthFor(const int serverIndex, const QString &ip)
{
    const QJsonObject serverConfig = getServerConfig(serverIndex);
    const int port = portForNodeIp(serverConfig, ip, serverPort(serverIndex));
    QVariantMap health = endpointHealth(ip, port);
    if (health.value(QStringLiteral("ageSec")).toInt() < 0) {
        const QVariantMap byRowPort = endpointHealth(ip, serverPort(serverIndex));
        if (byRowPort.value(QStringLiteral("ageSec")).toInt() >= 0) {
            return byRowPort;
        }
    }
    if (health.value(QStringLiteral("ageSec")).toInt() >= 0) {
        return health;
    }

    // Последний шанс: у этого IP мог быть замер на другом порту (нода сменила
    // порт, строка переехала на другой коннект). Показывать «not measured»,
    // когда вердикт для адреса есть, — враньё; берём самый свежий из кэша.
    const QString prefix = ip + QStringLiteral(":");
    QString bestKey;
    qint64 bestAt = -1;
    for (auto it = m_endpointHealth.constBegin(); it != m_endpointHealth.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) {
            continue;
        }
        if (it->atMs > bestAt) {
            bestAt = it->atMs;
            bestKey = it.key();
        }
    }
    if (!bestKey.isEmpty()) {
        const EndpointHealth entry = m_endpointHealth.value(bestKey);
        const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - entry.atMs;
        const bool fresh = ageMs <= kEndpointHealthTtlMs;
        QVariantMap fallback;
        fallback.insert(QStringLiteral("rttLast"), entry.rttMs);
        fallback.insert(QStringLiteral("rtt"), fresh ? entry.rttMs : -2);
        fallback.insert(QStringLiteral("state"),
                        fresh ? (entry.rttMs >= 0 ? QStringLiteral("ok") : QStringLiteral("unreachable"))
                              : QStringLiteral("unknown"));
        fallback.insert(QStringLiteral("ageSec"), static_cast<int>(ageMs / 1000));
        fallback.insert(QStringLiteral("attempts"), entry.attempts);
        return fallback;
    }
    return health;
}

QVariantMap ServersModel::processedEndpointHealth(const QString &ip)
{
    QVariantMap health = endpointHealth(ip, getProcessedNodePort(ip));
    if (health.value(QStringLiteral("ageSec")).toInt() < 0) {
        const QVariantMap byRowPort = endpointHealth(ip, getProcessedServerPort());
        if (byRowPort.value(QStringLiteral("ageSec")).toInt() >= 0) {
            return byRowPort;
        }
    }
    return health;
}

int ServersModel::getProcessedNodePort(const QString &ip)
{
    const int fallback = serverPort(m_processedServerIndex);
    if (m_processedServerIndex < 0) {
        return fallback;
    }
    return portForNodeIp(getServerConfig(m_processedServerIndex), ip, fallback);
}

int ServersModel::weightForNodeIp(const QJsonObject &serverConfig, const QString &ip)
{
    // Same rule for weights: `endpoints[].weight` (default 100) wins, the
    // legacy map is the fallback. Both feed the picker identically.
    if (NodeEndpoints::present(serverConfig)) {
        return NodeEndpoints::weightFor(serverConfig, ip);
    }
    const QJsonValue raw = serverConfig.value(QStringLiteral("node_weights"));

    auto normalize = [](const QJsonValue &value) -> int {
        int weight = value.isString() ? value.toString().toInt() : value.toInt();
        // 0 и отрицательные = «вес не задан»: именно так ведёт себя нода старой
        // сборки, которая присылает 0 вместо отсутствия поля.
        return weight > 0 ? weight : 100;
    };

    if (raw.isObject()) {
        const QJsonValue value = raw.toObject().value(ip);
        if (!value.isUndefined() && !value.isNull()) {
            return normalize(value);
        }
        return 100;
    }
    // компактная форма: массив весов, выровненный по node_ips (как у node_ports)
    if (raw.isArray()) {
        const QJsonArray ips = serverConfig.value(QStringLiteral("node_ips")).toArray();
        const QJsonArray weights = raw.toArray();
        for (int i = 0; i < ips.size() && i < weights.size(); ++i) {
            if (ips.at(i).toString() == ip) {
                return normalize(weights.at(i));
            }
        }
        return 100;
    }
    return 100;
}

bool ServersModel::isSubscriptionExpired(int index) const
{
    if (index < 0 || index >= m_servers.size()) {
        return false;
    }
    const QJsonObject apiConfig = getServerConfig(index).value(QStringLiteral("api_config")).toObject();
    const QString endDate = apiConfig.value(QStringLiteral("subscription")).toObject()
                                     .value(QStringLiteral("end_date")).toString();
    if (endDate.isEmpty()) {
        return false;
    }
    QDateTime end = QDateTime::fromString(endDate, Qt::ISODate);
    if (!end.isValid()) {
        end = QDateTime::fromString(endDate, QStringLiteral("yyyy-MM-dd"));
    }
    if (!end.isValid()) {
        return false;
    }
    // дата в подписке — «до конца дня»: добавляем сутки, чтобы не блокировать
    // подключение в последний оплаченный день
    return end.addDays(1) < QDateTime::currentDateTimeUtc();
}

int ServersModel::endpointWeight(const QString &ip) const
{
    return weightForNodeIp(getServerConfig(m_processedServerIndex), ip);
}

bool ServersModel::isEndpointUnreachable(const QString &host, const int port) const
{
    return endpointHealth(host, port).value(QStringLiteral("state")).toString()
           == QStringLiteral("unreachable");
}

bool ServersModel::isEndpointUsable(const QString &host, const int port) const
{
    // всё, что не «свежий провал», считаем пригодным: непроверенный адрес
    // надо пробовать, иначе пул схлопнется в ноль при первом же сбое
    return !isEndpointUnreachable(host, port);
}

void ServersModel::clearHealthResults()
{
    if (m_healthResults.isEmpty()) {
        return;
    }
    m_healthResults.clear();
    if (!m_servers.isEmpty()) {
        emit dataChanged(index(0), index(m_servers.size() - 1), { HealthLatencyRole });
    }
}

void ServersModel::recomputeAvailableEnvs()
{
    QSet<QString> envs;
    for (int i = 0; i < m_servers.size(); ++i) {
        const QString env = data(index(i), ConnectionEnvRole).toString();
        if (!env.isEmpty()) {
            envs.insert(env);
        }
    }
    QStringList sorted = envs.values();
    sorted.sort();
    if (sorted != m_availableEnvs) {
        m_availableEnvs = sorted;
        emit availableEnvsChanged();
    }
}

void ServersModel::recomputeAvailableSubscriptions()
{
    // first-seen order: the server list numbers subscriptions by their insertion
    // order, so "Subscription 1" stays the first one the user added
    QStringList ids;
    for (int i = 0; i < m_servers.size(); ++i) {
        const QString id = subscriptionIdOfServer(m_servers.at(i).toObject());
        if (!id.isEmpty() && !ids.contains(id)) {
            ids.append(id);
        }
    }
    if (ids != m_availableSubscriptions) {
        m_availableSubscriptions = ids;
        emit availableSubscriptionsChanged();
    }
}

void ServersModel::recomputeAvailableProtocols()
{
    QSet<QString> protocols;
    for (int i = 0; i < m_servers.size(); ++i) {
        // Canonical tags: raw backend variants (`amneziawgmobile`) stay for the
        // connection logic and must never surface as their own protocol.
        const QString protocol = canonicalServiceProtocol(
                data(index(i), ServiceProtocolFilterRole).toString());
        if (!protocol.isEmpty()) {
            protocols.insert(protocol);
        }
    }
    QStringList sorted = protocols.values();
    sorted.sort();
    if (sorted != m_availableProtocols) {
        m_availableProtocols = sorted;
        emit availableProtocolsChanged();
    }
}
