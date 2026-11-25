#ifndef SERVERSMODEL_H
#define SERVERSMODEL_H

#include <QAbstractListModel>

#include "settings.h"

class ServersModel : public QAbstractListModel
{
    Q_OBJECT
public:
    struct GatewayStacks
    {
        QSet<QString> userCountryCodes;
        QSet<QString> serviceTypes;

        bool isEmpty() const { return userCountryCodes.isEmpty() && serviceTypes.isEmpty(); }
        bool operator==(const GatewayStacks &other) const;
        QJsonObject toJson() const;
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        ServerDescriptionRole,
        CollapsedServerDescriptionRole,
        ExpandedServerDescriptionRole,
        HostNameRole,

        CredentialsRole,
        CredentialsLoginRole,

        IsDefaultRole,
        IsCurrentlyProcessedRole,

        HasWriteAccessRole,

        ContainsAmneziaDnsRole,

        DefaultContainerRole,

        HasInstalledContainers,

        IsServerFromTelegramApiRole,
        IsServerFromGatewayApiRole,
        ApiConfigRole,
        IsCountrySelectionAvailableRole,
        ApiAvailableCountriesRole,
        ApiServerCountryCodeRole,
        IsAdVisibleRole,
        AdHeaderRole,
        AdDescriptionRole,
        AdEndpointRole,

        HasAmneziaDns,
        ServiceProtocolRole,
        // raw backend protocol tag lowercased (e.g. "amneziawgmobile") — protocol
        // variants get their own entry in the UI filter while ServiceProtocolRole
        // stays canonical for logic
        ServiceProtocolFilterRole,
        ConnectionEnvRole,
        CountryCodeRole,
        CountryNameRole,
        NodeIpsRole,
        // dial address for protocols whose config keeps it inside the stored
        // xray-style blob (hysteria2): hostName stays what the tunnel dials,
        // this falls back to the config's serverName/address for the card.
        EndpointHostRole,
        // TLS domain a hysteria2 row verifies against (its outbound serverName):
        // the dial address can be an IP, and the card should name the domain too.
        EndpointDomainRole,
        // true when the row's latency is a server ping (proxy protocols: the
        // tunnel itself cannot be handshake-probed from the client)
        HealthPingRole,
        HealthLatencyRole
    };

    ServersModel(std::shared_ptr<Settings> settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    bool setData(const int index, const QVariant &value, int role = Qt::EditRole);
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant data(const int index, int role = Qt::DisplayRole) const;

    void resetModel();

    GatewayStacks gatewayStacks() const { return m_gatewayStacks; }

    Q_PROPERTY(int defaultIndex READ getDefaultServerIndex WRITE setDefaultServerIndex NOTIFY defaultServerIndexChanged)
    Q_PROPERTY(QString defaultServerName READ getDefaultServerName NOTIFY defaultServerNameChanged)
    Q_PROPERTY(QString defaultServerProtocolName READ getDefaultServerProtocolName NOTIFY defaultServerIndexChanged)
    Q_PROPERTY(QString defaultServerDefaultContainerName READ getDefaultServerDefaultContainerName NOTIFY defaultServerDefaultContainerChanged)
    Q_PROPERTY(QString defaultServerDescriptionCollapsed READ getDefaultServerDescriptionCollapsed NOTIFY defaultServerDefaultContainerChanged)
    Q_PROPERTY(QString defaultServerImagePathCollapsed READ getDefaultServerImagePathCollapsed NOTIFY defaultServerDefaultContainerChanged)
    Q_PROPERTY(QString defaultServerDescriptionExpanded READ getDefaultServerDescriptionExpanded NOTIFY defaultServerDefaultContainerChanged)
    Q_PROPERTY(bool isDefaultServerDefaultContainerHasSplitTunneling READ isDefaultServerDefaultContainerHasSplitTunneling NOTIFY
                       defaultServerDefaultContainerChanged)
    Q_PROPERTY(bool isDefaultServerFromApi READ isDefaultServerFromApi NOTIFY defaultServerIndexChanged)
    Q_PROPERTY(QString defaultServerHostName READ getDefaultServerHostName NOTIFY defaultServerIndexChanged)
    Q_PROPERTY(QStringList defaultServerNodeIps READ getDefaultServerNodeIps NOTIFY defaultServerIndexChanged)

    Q_PROPERTY(bool hasServersFromGatewayApi READ hasServersFromGatewayApi NOTIFY hasServersFromGatewayApiChanged)
    Q_PROPERTY(QStringList availableProtocols READ availableProtocols NOTIFY availableProtocolsChanged)
    Q_PROPERTY(QStringList availableEnvs READ availableEnvs NOTIFY availableEnvsChanged)
    Q_PROPERTY(QStringList availableSubscriptions READ availableSubscriptions NOTIFY availableSubscriptionsChanged)
    // Счётчик обновлений замеров адресов: QML-биндинг должен пересчитаться,
    // когда пришли новые результаты или восстановился кэш (иначе карточка
    // держит старое «not checked», хотя замер уже есть).
    Q_PROPERTY(int endpointHealthRevision READ endpointHealthRevision NOTIFY endpointHealthRevisionChanged)

    Q_PROPERTY(int processedIndex READ getProcessedServerIndex WRITE setProcessedServerIndex NOTIFY processedServerIndexChanged)
    Q_PROPERTY(bool processedServerIsPremium READ processedServerIsPremium NOTIFY processedServerChanged)

    Q_PROPERTY(bool isAdVisible READ isAdVisible NOTIFY defaultServerIndexChanged)
    Q_PROPERTY(QString adHeader READ adHeader NOTIFY defaultServerIndexChanged)
    Q_PROPERTY(QString adDescription READ adDescription NOTIFY defaultServerIndexChanged)

    bool processedServerIsPremium() const;

public slots:
    void setDefaultServerIndex(const int index);
    const int getDefaultServerIndex();
    const QString getDefaultServerName();
    const QString getDefaultServerProtocolName();
    const QString getDefaultServerDescriptionCollapsed();
    const QString getDefaultServerImagePathCollapsed();
    const QString getDefaultServerDescriptionExpanded();
    const QString getDefaultServerDefaultContainerName();
    bool isDefaultServerCurrentlyProcessed();
    bool isDefaultServerFromApi();
    const QString getDefaultServerHostName();
    const QStringList getDefaultServerNodeIps();
    // protocols present on servers of the given env ("" = all envs) — the server
    // list filter never offers protocols the selected env doesn't have.
    // subscriptionId ("" = any) narrows the scan to one subscription, so with a
    // subscription selected the list does not offer protocols that only other
    // subscriptions (or legacy non-subscription configs) carry. The list's
    // "Other" bucket sentinel ("__other__") means "rows without a subscription".
    QStringList availableProtocolsForEnv(const QString &env, const QString &subscriptionId = QString()) const;
    QStringList availableEnvs() const;
    // distinct FRKN subscriptions present among stored servers, in first-seen
    // order (the server list numbers them "Subscription 1/2/...")
    QStringList availableSubscriptions() const;

    bool isProcessedServerHasWriteAccess();
    bool isDefaultServerHasWriteAccess();
    bool hasServerWithWriteAccess();

    bool hasServersFromGatewayApi();

    const int getServersCount();

    void setProcessedServerIndex(const int index);
    int getProcessedServerIndex();

    const ServerCredentials getProcessedServerCredentials();
    const ServerCredentials getServerCredentials(const int index);

    void addServer(const QJsonObject &server);
    void addServers(const QJsonArray &servers);
    void removeAllServers();

    // Peers plane: one row per (env, pool). The row keeps the concrete card id
    // of the selected protocol in `peerCardId` (that is what POST /v1/peers
    // receives) and every protocol the pool serves in `peerVariants`; rows
    // carry `peerCardId`, so the connect path can tell them from v1 configs,
    // and keep their tunnel config once it exists — that cached config is the
    // reason a second connect needs no api call.
    //
    // `rows` replaces every peer row of `subscriptionId`: a pool the api no
    // longer offers loses its row, a known pool keeps the cached tunnel of each
    // of its protocols.
    void syncPeerCards(const QString &subscriptionId, const QJsonArray &rows);

    // The api answered for this subscription and offered NO peer cards: the
    // shard's grant no longer carries a card protocol (v1 -> v2 -> v1, a plan
    // downgrade). Unlike syncPeerCards(empty), this answer is authoritative, so
    // the stored pool rows must actually go — otherwise the old pool list stays
    // on screen forever and the client looks like it ignores the migration.
    void dropPeerCards(const QString &subscriptionId);

    // Remove stored rows of THIS subscription that the api no longer returns.
    // Guards, in order: only rows that came from our gateway api (a manually
    // added config or another provider never matches), only rows that carry a
    // connection uuid, and only when `keepConnectionUuids` is non-empty — an
    // empty answer means "nothing fetched", not "everything deleted".
    void pruneAbsentRows(const QString &subscriptionId, const QStringList &keepConnectionUuids);

    // Rows whose connection the api no longer returns are gone for good: keep
    // the ones present in `keepConnectionUuids`, drop the rest of THIS
    // subscription (peer rows have no connection uuid and are not touched).
    // QML: the server card uses this to tell a peers row from a v1 config row
    // (empty = v1) and to pick the peer reload path.
    Q_INVOKABLE QString peerCardId(const int serverIndex) const;
    // The subscription a row belongs to (peers rows and v1 rows alike). A peer
    // row written before `peerSubscriptionId` existed still names it through
    // `auth_data.api_key`; the card reload needs it to rebuild that row.
    Q_INVOKABLE QString subscriptionIdForRow(const int serverIndex) const;

    // Switch a pool row to another of its protocol variants. The row keeps its
    // position and its cached tunnel per protocol; the choice is written to the
    // settings so a refresh and the next app start show the same protocol.
    // Returns false when the row has no such variant (v1 row, unknown card).
    Q_INVOKABLE bool setPeerPoolProtocol(const int serverIndex, const QString &cardId);
    // TCP port the row's config dials (443 when the row has no port of its own).
    Q_INVOKABLE int endpointPort(const int serverIndex) const;
    // The list's protocol filter: every pool row that serves `protocol` is
    // switched to it, so the row the user sees is the row that gets dialed.
    // Pools without the protocol stay as they are (the view filters them out).
    Q_INVOKABLE void applyPeerProtocolFilter(const QString &protocol);

    // Stable identity of a row: `peer:<peerCardId>` for a peer card,
    // `uuid:<connection_uuid>` for a v1 config, empty when the row carries
    // neither. The selection is stored by this key because row positions drift
    // on every model reset (subscription import appends rows, syncPeerCards
    // rebuilds the peer rows), which silently moved the chosen card.
    static QString selectionIdentity(const QJsonObject &row);
    // Stored default-row identity, as written by setDefaultServerIndex().
    QString defaultServerIdentity() const;
    // The stored identity resolved to a row index; -1 when unknown or when the
    // row no longer exists.
    int rowIndexForDefaultIdentity() const;
    // Same key, for a live row (empty when the row carries neither id).
    Q_INVOKABLE QString rowIdentity(const int serverIndex) const;
    // Row index that currently carries `identity`; -1 when the row is gone.
    // Use this instead of a row number captured before a model refresh: a
    // subscription import or a peers sync rebuilds the list in place, and the
    // captured number then names another server.
    Q_INVOKABLE int rowForIdentity(const QString &identity) const;
    // Bumped whenever the row list is rebuilt. An index captured in an older
    // generation points into a layout that no longer exists.
    quint64 rowsGeneration() const { return m_rowsGeneration; }
    bool peerRowHasConfig(const int serverIndex) const;
    void setPeerConfig(const QString &cardId, const QJsonObject &protoConfig);
    // Drops the cached tunnel of one card so the next connect rebuilds it, but
    // keeps the tunnel the row already carries as a display-only snapshot
    // (`peerConfigStale`): the card keeps showing its config right after a
    // manual reload instead of an empty popup, and peerRowHasConfig() ignores
    // the snapshot, so the rebuild still happens.
    void invalidatePeerConfig(const QString &cardId);
    // Record a successful connect on the row of `cardId` (`peerLastConnectedAt`,
    // mirrored into the tunnel config). Support reads it next to the config's own
    // provisioning time; nothing in the connect path depends on it.
    void markPeerConnected(const QString &cardId);
    void editServer(const QJsonObject &server, const int serverIndex);
    void removeServer();
    void removeServer(const int serverIndex);
    // Removes every server row that belongs to one FRKN subscription. The
    // subscription itself stays on the account, only this device forgets it.
    Q_INVOKABLE void removeSubscription(const QString &subscriptionId);

    // health result for a server row: >=0 latency ms, -1 offline, missing = unknown.
    // The value belongs to the row AND the protocol card it was measured for — a
    // pool row swaps cards in place, and the previous protocol's number must not
    // survive the switch. `cardId` is the card the probe was queued for; empty
    // means "whatever the row carries now".
    void setHealthResult(int serverIndex, int latencyMs, const QString &cardId = QString());
    void clearHealthResults();

    // Per-address probe results (key = "host:port"). Needed to pick the address
    // inside a node and to show the number next to each address.
    // latencyMs: >= 0 real handshake RTT, -1 no handshake (unreachable).
    void setEndpointHealthResult(const QString &host, const int port, int latencyMs, int attempts = 1);
    // Перезапуск приложения не должен обнулять замеры: свежие результаты
    // (< 1 мин) берём из диска, а не меряем заново.
    void loadEndpointHealthCache();
    // Свежесть последнего прогона проб (для «не дрочить проверку»)
    bool probeIsFresh() const;
    void markProbeRun();
    void saveEndpointHealthCache();
    void clearEndpointHealthResults();

    // { rtt: int, state: "ok"|"unreachable"|"unknown", ageSec: int }
    Q_INVOKABLE QVariantMap endpointHealth(const QString &host, const int port) const;
    Q_INVOKABLE void clearEndpointHealth(const QString &host, const int port);
    // порт текущего сервера (для ключа проб по адресам)
    Q_INVOKABLE int getProcessedServerPort();
    Q_INVOKABLE int getProcessedNodePort(const QString &ip);

    // Замер по адресу текущей строки: пробуем порт адреса, затем порт строки.
    // Проба и карточка считают порт из разных мест (контейнер vs node_ports),
    // и без этого второго шанса цифры не находились — в карточке был прочерк.
    Q_INVOKABLE QVariantMap processedEndpointHealth(const QString &ip);
    // Тот же health, но для конкретной строки: карточка может показывать строку,
    // которая не совпадает с processedIndex, и тогда порты берутся чужие —
    // адрес находится «неизмеренным», хотя замер есть.
    Q_INVOKABLE QVariantMap endpointHealthFor(const int serverIndex, const QString &ip);
    // Порт, на котором этот IP реально отвечал (по кэшу замеров). Нужен, когда
    // конфиг не несёт node_ports и порт протокола угадывается неверно.
    int livePortForIp(const QString &ip, int currentPort) const;

    // Порт конкретного адреса пула: node_ports[ip], иначе порт протокола.
    // Общий для коннекта, проб и отчёта — иначе карточка и подключение
    // разойдутся по портам.
    static int portForNodeIp(const QJsonObject &serverConfig, const QString &ip, int fallbackPort);
    // Вес адреса из node_weights (карта ip → вес). Дефолт 100; 0/отрицательное
    // считается «не задан» → 100 (так ведёт себя старая нода, присылающая 0).
    static int weightForNodeIp(const QJsonObject &serverConfig, const QString &ip);
    // тот же вес, но для текущей обрабатываемой строки (карточка/коннект)
    int endpointWeight(const QString &ip) const;
    int endpointHealthRevision() const { return m_endpointHealthRevision; }
    // Подписка строки истекла? Читаем api_config.subscription.end_date (ISO).
    bool isSubscriptionExpired(int index) const;
    // Порт протокола для строки: верхнеуровневый port, иначе порт контейнера
    // (у серверов из API верхнего порта нет вообще).
    int serverPort(int index) const;
    QString hysteriaDialHost(const int index) const;
    // First outbound that names a real server (address + port) whatever its
    // protocol — vless, vmess, trojan, hysteria2. The ping probe needs the
    // dial target of the row, not the row's label.
    QJsonObject proxyDialOutbound(const int index) const;
    QString proxyDialHost(const int index) const;
    QString hysteriaDialDomain(const int index) const;
    int hysteriaDialPort(const int index) const;
    QJsonObject hysteriaDialOutbound(const int index) const;
    Q_INVOKABLE bool isEndpointUsable(const QString &host, const int port) const;
    // true while a fresh (<= 5 min) handshake failure is recorded
    Q_INVOKABLE bool isEndpointUnreachable(const QString &host, const int port) const;

    QJsonObject getServerConfig(const int serverIndex) const;
    QJsonObject serverRow(const int serverIndex) const;
    QVariantMap getServerDisplayData(const int serverIndex) const;

    void reloadDefaultServerContainerConfig();
    void updateContainerConfig(const int containerIndex, const QJsonObject config);
    void addContainerConfig(const int containerIndex, const QJsonObject config);

    void clearCachedProfile(const DockerContainer container);

    void setDefaultContainer(const int serverIndex, const int containerIndex);

    QStringList getAllInstalledServicesName(const int serverIndex);

    void toggleAmneziaDns(bool enabled);
    QPair<QString, QString> getDnsPair(const int serverIndex);

    bool isServerFromApiAlreadyExists(const quint16 crc);
    bool isServerFromApiAlreadyExists(const QString &userCountryCode, const QString &serviceType, const QString &serviceProtocol,
                                      const QString &subscriptionId = QString());
    bool isServerFromApiAlreadyExists(const QString &connectionUuid);
    bool isServerFromApiAlreadyExists(const QString &name, const QString &description) const;
    bool hasServerWithVpnKey(const QString &vpnKey) const;

    // true only when the default container entry holds an actual protocol config;
    // HasInstalledContainers alone is not enough — gateway imports create the
    // container entry even when the config itself was never fetched
    bool serverHasUsableConfig(const int serverIndex) const;

    QVariant getDefaultServerData(const QString roleString);

    QVariant getProcessedServerData(const QString roleString);
    QVariant getServerData(const int serverIndex, const QString roleString);
    bool setProcessedServerData(const QString &roleString, const QVariant &value);

    // Localized title for a node pool/group: current UI language translation
    // -> raw pool -> "" (so the caller keeps the old name). A missing
    // translation falls back to the pool itself; `ua` is accepted as `uk`.
    // `pool`/`translation` come from /v1/services (`pool`, `pool_translation`).
    static QString localizedPoolName(const QString &pool, const QJsonObject &translation, const QLocale &locale);

    // Re-reads pool-based names after a UI language change. NameRole is computed
    // dynamically, so this only needs to notify the views; user renames are kept.
    void onTranslationsUpdated();

    bool isDefaultServerDefaultContainerHasSplitTunneling();

    bool isServerFromApi(const int serverIndex);
    bool isApiKeyExpired(const int serverIndex);
    void removeApiConfig(const int serverIndex);

    bool isAdVisible();
    QString adHeader();
    QString adDescription();
    
protected:
    QHash<int, QByteArray> roleNames() const override;

signals:
    void endpointHealthRevisionChanged();
    void processedServerIndexChanged(const int index);
    // emitted when the processed server index or processed server data is changed
    void processedServerChanged();

    void defaultServerIndexChanged(const int index);
    void defaultServerNameChanged();
    void defaultServerDescriptionChanged();

    void containersUpdated(const QJsonArray &containers);
    void defaultServerContainersUpdated(const QJsonArray &containers);
    void defaultServerDefaultContainerChanged(const int containerIndex);

    void updateApiCountryModel();
    void updateApiServicesModel();

    void hasServersFromGatewayApiChanged();
    void gatewayStacksExpanded();
    void availableProtocolsChanged();
    void availableEnvsChanged();
    void availableSubscriptionsChanged();

private:
    ServerCredentials serverCredentials(int index) const;

    void updateContainersModel();
    void updateDefaultServerContainersModel();

    QString getServerDescription(const QJsonObject &server, const int index) const;

    // User-selected row (the open card) resolved by identity: peer card id,
    // uuid+node, uuid+pool+protocol, pool+protocol, then the stored index.
    // -1 when nothing matches.
    int rowIndexForUserSelection() const;
    // Writes `Servers/defaultServerIndex` (still the compatibility key) and the
    // matching identity, so the next reset can restore by identity.
    void persistDefaultSelection(const int index);

    bool isAmneziaDnsContainerInstalled(const int serverIndex) const;

    bool serverHasInstalledContainers(const int serverIndex) const;

    QStringList availableProtocols() const;
    void recomputeAvailableProtocols();

    void recomputeAvailableEnvs();
    void recomputeAvailableSubscriptions();

    QJsonArray m_servers;

    // Row layout generation: incremented before every rebuild of m_servers so
    // that work started on one layout (health probes, auto-select candidates)
    // can tell that its row numbers no longer describe the current list.
    quint64 m_rowsGeneration = 0;
    void bumpRowsGeneration() { ++m_rowsGeneration; }

    std::shared_ptr<Settings> m_settings;

    int m_defaultServerIndex = -1;
    int m_processedServerIndex = -1;

    bool m_isAmneziaDnsEnabled = m_settings->useAmneziaDns();

    GatewayStacks m_gatewayStacks;
    void recomputeGatewayStacks();

    QStringList m_availableProtocols;

    QStringList m_availableEnvs;

    QStringList m_availableSubscriptions;

    struct HealthResult
    {
        QString cardId; // protocol card the value was measured for (empty: v1 row)
        int latencyMs = -2;
    };

    QHash<int, HealthResult> m_healthResults;

    static constexpr qint64 kEndpointHealthTtlMs = 5 * 60 * 1000;

    struct EndpointHealth
    {
        int rttMs = -1;
        qint64 atMs = 0;
        int attempts = 1; // сколько раз стучались (1 = с первой попытки)
    };
    QHash<QString, EndpointHealth> m_endpointHealth;
    int m_endpointHealthRevision = 0;
    static QString endpointKey(const QString &host, const int port)
    {
        return host + QLatin1Char(':') + QString::number(port);
    }
};

#endif // SERVERSMODEL_H
