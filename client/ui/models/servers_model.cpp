#include <QDateTime>
#include "servers_model.h"

#include <QJsonObject>
#include <QLocale>
#include <QSet>

#include "core/api/apiDefs.h"
#include "core/networkUtilities.h"

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
}

ServersModel::ServersModel(std::shared_ptr<Settings> settings, QObject *parent) : m_settings(settings), QAbstractListModel(parent)
{
    m_isAmneziaDnsEnabled = m_settings->useAmneziaDns();

    connect(this, &ServersModel::defaultServerIndexChanged, this, &ServersModel::defaultServerNameChanged);

    connect(this, &ServersModel::defaultServerIndexChanged, this, [this](const int serverIndex) {
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
        // all entry addresses of a multi-IP node (empty for single-address nodes)
        QStringList ips;
        const QJsonArray nodeIps = server.value(QStringLiteral("node_ips")).toArray();
        for (const QJsonValue &v : nodeIps) {
            const QString ip = v.toString();
            if (!ip.isEmpty()) {
                ips.append(ip);
            }
        }
        return ips;
    }
    case HealthLatencyRole: {
        // >=0 latency ms, -1 offline, -2 unknown (not probed yet); keyed by row
        // index — several servers can share a host on different ports (e.g. AWG
        // and its mobile variant), keying by host name mixed up their results
        return m_healthResults.value(index.row(), -2);
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
    beginResetModel();
    m_healthResults.clear();
    loadEndpointHealthCache();
    m_servers = m_settings->serversArray();
    m_defaultServerIndex = m_settings->defaultServerIndex();
    // Восстанавливаем выбор юзера: сначала по идентичности (uuid+node),
    // индекс — только запасной вариант, он «плывёт» при обновлении подписки.
    int restoredSelected = -1;
    if (m_settings) {
        const QString selUuid = m_settings->selectedConnectionUuid();
        const QString selNode = m_settings->selectedNodeId();
        if (!selUuid.isEmpty() && !selNode.isEmpty()) {
            for (int i = 0; i < m_servers.size(); ++i) {
                const QJsonObject apiConfig = m_servers.at(i).toObject()
                                                      .value(QStringLiteral("api_config")).toObject();
                if (apiConfig.value(QStringLiteral("connection_uuid")).toString() == selUuid
                    && apiConfig.value(QStringLiteral("node_id")).toString() == selNode) {
                    restoredSelected = i;
                    break;
                }
            }
        }
        // Пул: connection_uuid стабилен (это conn_id из БД), а node_id — нет:
        // в строке пула стоит представительная нода, и она меняется, когда
        // меняется порядок обхода нод. Поэтому для пула пара uuid+node рвётся,
        // хотя само соединение то же — сверяем uuid + пул + протокол.
        if (restoredSelected < 0) {
            const QString selUuid2 = m_settings->selectedConnectionUuid();
            const QString selPool2 = m_settings->selectedPool();
            const QString selProto2 = m_settings->selectedProtocol();
            if (!selUuid2.isEmpty() && !selPool2.isEmpty()) {
                for (int i = 0; i < m_servers.size(); ++i) {
                    const QJsonObject apiConfig = m_servers.at(i).toObject()
                                                          .value(QStringLiteral("api_config")).toObject();
                    if (apiConfig.value(QStringLiteral("connection_uuid")).toString() == selUuid2
                        && apiConfig.value(QStringLiteral("pool")).toString() == selPool2
                        && (selProto2.isEmpty()
                            || apiConfig.value(QStringLiteral("service_protocol")).toString() == selProto2)) {
                        restoredSelected = i;
                        break;
                    }
                }
            }
        }
        // Последний шанс для пула: uuid мог быть перевыпущен (переимпорт
        // подписки), но имя пула и протокол те же.
        if (restoredSelected < 0) {
            const QString selPool = m_settings->selectedPool();
            const QString selProto = m_settings->selectedProtocol();
            if (!selPool.isEmpty()) {
                for (int i = 0; i < m_servers.size(); ++i) {
                    const QJsonObject apiConfig = m_servers.at(i).toObject()
                                                          .value(QStringLiteral("api_config")).toObject();
                    if (apiConfig.value(QStringLiteral("pool")).toString() == selPool
                        && (selProto.isEmpty()
                            || apiConfig.value(QStringLiteral("service_protocol")).toString() == selProto)) {
                        restoredSelected = i;
                        break;
                    }
                }
            }
        }
        if (restoredSelected < 0) {
            const int selected = m_settings->selectedServerIndex();
            if (selected >= 0 && selected < m_servers.size()) {
                restoredSelected = selected;
            }
        }
    }
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
    const int lastConnected = m_settings->lastConnectedServerIndex();
    if (restoredSelected >= 0 && restoredSelected == lastConnected
        && m_defaultServerIndex != restoredSelected) {
        qDebug() << "[SELECTION] default reconciled on startup:"
                 << m_defaultServerIndex << "->" << restoredSelected;
        m_defaultServerIndex = restoredSelected;
        m_settings->setDefaultServer(restoredSelected);
    }

    m_isAmneziaDnsEnabled = m_settings->useAmneziaDns();
    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
    emit defaultServerIndexChanged(m_defaultServerIndex);
}

void ServersModel::setDefaultServerIndex(const int index)
{
    m_settings->setDefaultServer(index);
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
    QStringList ips;
    const QJsonArray nodeIps = m_servers.at(m_defaultServerIndex).toObject().value(QStringLiteral("node_ips")).toArray();
    for (const QJsonValue &v : nodeIps) {
        const QString ip = v.toString();
        if (!ip.isEmpty()) {
            ips.append(ip);
        }
    }
    return ips;
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
        const QJsonObject apiConfig = getServerConfig(index).value(QStringLiteral("api_config")).toObject();
        m_settings->setSelectedIdentity(apiConfig.value(QStringLiteral("connection_uuid")).toString(),
                                        apiConfig.value(QStringLiteral("node_id")).toString(),
                                        apiConfig.value(QStringLiteral("pool")).toString(),
                                        apiConfig.value(QStringLiteral("service_protocol")).toString());
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
    beginResetModel();
    m_settings->addServer(server);
    m_servers = m_settings->serversArray();
    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::addServers(const QJsonArray &servers)
{
    beginResetModel();
    m_settings->addServers(servers);
    m_servers = m_settings->serversArray();
    recomputeAvailableProtocols();
    recomputeAvailableEnvs();
    recomputeAvailableSubscriptions();
    endResetModel();
}

void ServersModel::removeAllServers()
{
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
    m_settings->editServer(serverIndex, server);
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

QHash<int, QByteArray> ServersModel::roleNames() const
{
    QHash<int, QByteArray> roles;

    roles[NameRole] = "name";
    roles[ServerDescriptionRole] = "serverDescription";
    roles[CollapsedServerDescriptionRole] = "collapsedServerDescription";
    roles[ExpandedServerDescriptionRole] = "expandedServerDescription";

    roles[HostNameRole] = "hostName";

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
    roles[HealthLatencyRole] = "healthLatency";

    return roles;
}

ServerCredentials ServersModel::serverCredentials(int index) const
{
    const QJsonObject &s = m_servers.at(index).toObject();

    ServerCredentials credentials;
    credentials.hostName = s.value(config_key::hostName).toString();
    credentials.userName = s.value(config_key::userName).toString();
    credentials.secretData = s.value(config_key::password).toString();
    credentials.port = s.value(config_key::port).toInt();

    return credentials;
}

void ServersModel::updateContainersModel()
{
    auto containers = m_servers.at(m_processedServerIndex).toObject().value(config_key::containers).toArray();
    emit containersUpdated(containers);
}

void ServersModel::updateDefaultServerContainersModel()
{
    auto containers = m_servers.at(m_defaultServerIndex).toObject().value(config_key::containers).toArray();
    emit defaultServerContainersUpdated(containers);
}

QJsonObject ServersModel::getServerConfig(const int serverIndex) const
{
    return m_servers.at(serverIndex).toObject();
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

    const QString name = data(serverIndex, NameRole).toString();
    return {
        { QStringLiteral("sourceIndex"), serverIndex },
        { QStringLiteral("name"), name },
        { QStringLiteral("groupName"), localizedPool.isEmpty() ? name : localizedPool },
        { QStringLiteral("pool"), pool },
        { QStringLiteral("subscriptionId"), subscriptionId },
        { QStringLiteral("connectionUuid"), apiConfig.value(QStringLiteral("connection_uuid")).toString() },
        { QStringLiteral("protocol"), data(serverIndex, ServiceProtocolFilterRole).toString() },
        { QStringLiteral("env"), data(serverIndex, ConnectionEnvRole).toString() },
        { QStringLiteral("countryCode"), data(serverIndex, CountryCodeRole).toString() },
        { QStringLiteral("countryName"), data(serverIndex, CountryNameRole).toString() },
        { QStringLiteral("healthLatency"), data(serverIndex, HealthLatencyRole).toInt() },
        { QStringLiteral("isServerFromGatewayApi"), data(serverIndex, IsServerFromGatewayApiRole).toBool() },
        { QStringLiteral("serverDescription"), data(serverIndex, ServerDescriptionRole).toString() },
    };
}

void ServersModel::reloadDefaultServerContainerConfig()
{
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

QStringList ServersModel::availableProtocolsForEnv(const QString &env) const
{
    QSet<QString> protocols;
    for (int i = 0; i < m_servers.size(); ++i) {
        if (!env.isEmpty() && data(index(i), ConnectionEnvRole).toString() != env) {
            continue;
        }
        const QString protocol = data(index(i), ServiceProtocolFilterRole).toString();
        if (!protocol.isEmpty()) {
            protocols.insert(protocol);
        }
    }
    QStringList sorted = protocols.values();
    sorted.sort();
    return sorted;
}

void ServersModel::setHealthResult(int serverIndex, int latencyMs)
{
    // multi-IP rows get one result per probed address — keep the best:
    // a live address means the server is reachable, and the lowest RTT wins
    const auto it = m_healthResults.constFind(serverIndex);
    if (it != m_healthResults.constEnd()) {
        const int prev = it.value();
        if ((prev >= 0 && (latencyMs < 0 || latencyMs >= prev)) || (prev < 0 && latencyMs < 0)) {
            return;
        }
    }
    m_healthResults.insert(serverIndex, latencyMs);
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
        const QString protocol = data(index(i), ServiceProtocolFilterRole).toString();
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
