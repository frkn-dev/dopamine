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
    // list filter never offers protocols the selected env doesn't have
    QStringList availableProtocolsForEnv(const QString &env) const;
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
    void editServer(const QJsonObject &server, const int serverIndex);
    void removeServer();
    void removeServer(const int serverIndex);
    // Removes every server row that belongs to one FRKN subscription. The
    // subscription itself stays on the account, only this device forgets it.
    Q_INVOKABLE void removeSubscription(const QString &subscriptionId);

    // health probe result for a server (key = row index): >=0 latency ms,
    // -1 offline, missing = unknown
    void setHealthResult(int serverIndex, int latencyMs);
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
    Q_INVOKABLE bool isEndpointUsable(const QString &host, const int port) const;
    // true while a fresh (<= 5 min) handshake failure is recorded
    Q_INVOKABLE bool isEndpointUnreachable(const QString &host, const int port) const;

    QJsonObject getServerConfig(const int serverIndex) const;
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

    bool isAmneziaDnsContainerInstalled(const int serverIndex) const;

    bool serverHasInstalledContainers(const int serverIndex) const;

    QStringList availableProtocols() const;
    void recomputeAvailableProtocols();

    void recomputeAvailableEnvs();
    void recomputeAvailableSubscriptions();

    QJsonArray m_servers;

    std::shared_ptr<Settings> m_settings;

    int m_defaultServerIndex = -1;
    int m_processedServerIndex = -1;

    bool m_isAmneziaDnsEnabled = m_settings->useAmneziaDns();

    GatewayStacks m_gatewayStacks;
    void recomputeGatewayStacks();

    QStringList m_availableProtocols;

    QStringList m_availableEnvs;

    QStringList m_availableSubscriptions;

    QHash<int, int> m_healthResults;

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
