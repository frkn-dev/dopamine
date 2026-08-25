#ifndef SERVERSMODEL_H
#define SERVERSMODEL_H

#include <QAbstractListModel>

#include "core/controllers/serverController.h"
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

    // health probe result for a server (key = row index): >=0 latency ms,
    // -1 offline, missing = unknown
    void setHealthResult(int serverIndex, int latencyMs);
    void clearHealthResults();

    QJsonObject getServerConfig(const int serverIndex) const;

    void reloadDefaultServerContainerConfig();
    void updateContainerConfig(const int containerIndex, const QJsonObject config);
    void addContainerConfig(const int containerIndex, const QJsonObject config);

    void clearCachedProfile(const DockerContainer container);

    ErrorCode removeContainer(const QSharedPointer<ServerController> &serverController, const int containerIndex);
    ErrorCode removeAllContainers(const QSharedPointer<ServerController> &serverController);
    ErrorCode rebootServer(const QSharedPointer<ServerController> &serverController);

    void setDefaultContainer(const int serverIndex, const int containerIndex);

    QStringList getAllInstalledServicesName(const int serverIndex);

    void toggleAmneziaDns(bool enabled);
    QPair<QString, QString> getDnsPair(const int serverIndex);

    bool isServerFromApiAlreadyExists(const quint16 crc);
    bool isServerFromApiAlreadyExists(const QString &userCountryCode, const QString &serviceType, const QString &serviceProtocol);
    bool isServerFromApiAlreadyExists(const QString &connectionUuid);
    bool isServerFromApiAlreadyExists(const QString &name, const QString &description) const;
    bool hasServerWithVpnKey(const QString &vpnKey) const;

    // true only when the default container entry holds an actual protocol config;
    // HasInstalledContainers alone is not enough — gateway imports create the
    // container entry even when the config itself was never fetched
    bool serverHasUsableConfig(const int serverIndex) const;

    QVariant getDefaultServerData(const QString roleString);

    QVariant getProcessedServerData(const QString roleString);
    bool setProcessedServerData(const QString &roleString, const QVariant &value);

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

private:
    ServerCredentials serverCredentials(int index) const;

    void updateContainersModel();
    void updateDefaultServerContainersModel();

    QString getServerDescription(const QJsonObject &server, const int index) const;

    bool isAmneziaDnsContainerInstalled(const int serverIndex) const;

    bool serverHasInstalledContainers(const int serverIndex) const;

    QStringList availableProtocols() const;
    void recomputeAvailableProtocols();

    QStringList availableEnvs() const;
    void recomputeAvailableEnvs();

    QJsonArray m_servers;

    std::shared_ptr<Settings> m_settings;

    int m_defaultServerIndex;
    int m_processedServerIndex;

    bool m_isAmneziaDnsEnabled = m_settings->useAmneziaDns();

    GatewayStacks m_gatewayStacks;
    void recomputeGatewayStacks();

    QStringList m_availableProtocols;

    QStringList m_availableEnvs;

    QHash<int, int> m_healthResults;
};

#endif // SERVERSMODEL_H
