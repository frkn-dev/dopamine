#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QList>
#include <QStringList>
#include <QTimer>

#include "protocols/vpnprotocol.h"
#include "ui/models/clientManagementModel.h"
#include "ui/models/containers_model.h"
#include "ui/models/servers_model.h"
#include "vpnconnection.h"

class HealthCheckController;
class ApiConfigsController;

class ConnectionController : public QObject
{
    Q_OBJECT

public:
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool isConnectionInProgress READ isConnectionInProgress NOTIFY connectionStateChanged)
    Q_PROPERTY(QString connectionStateText READ connectionStateText NOTIFY connectionStateChanged)
    Q_PROPERTY(QString currentEndpoint READ currentEndpoint NOTIFY connectionStateChanged)

    explicit ConnectionController(const QSharedPointer<ServersModel> &serversModel, const QSharedPointer<ContainersModel> &containersModel,
                                  const QSharedPointer<ClientManagementModel> &clientManagementModel,
                                  const QSharedPointer<VpnConnection> &vpnConnection, const std::shared_ptr<Settings> &settings,
                                  QObject *parent = nullptr);

    ~ConnectionController() = default;

    // needed only for auto server selection; may stay null (auto falls back to default server)
    void setHealthCheckController(HealthCheckController *healthCheckController);
    // used by auto selection to install per-row configs from the gateway (they are
    // otherwise fetched lazily on the first manual connect)
    void setApiConfigsController(ApiConfigsController *apiConfigsController);

    bool isConnected() const;
    bool isConnectionInProgress() const;
    QString connectionStateText() const;
    QString currentEndpoint() const { return m_currentEndpoint; }

public slots:
    void toggleConnection();

    void openConnection();
    void closeConnection();

    ErrorCode getLastConnectionError();
    void onConnectionStateChanged(Vpn::ConnectionState state);

    void onCurrentContainerUpdated();

    void onTranslationsUpdated();

signals:
    void connectToVpn(int serverIndex, const ServerCredentials &credentials, DockerContainer container, const QJsonObject &vpnConfiguration);
    void disconnectFromVpn();
    void connectionStateChanged();

    void connectionErrorOccurred(ErrorCode errorCode);
    void reconnectWithUpdatedContainer(const QString &message);

    void connectButtonClicked();
    void preparingConfig();
    void prepareConfig();

private:
    Vpn::ConnectionState getCurrentConnectionState();

    void connectToServerIndex(int serverIndex);
    void connectToServerIndexWithIp(int serverIndex, const QString &ip);

    // --- multi-IP failover (node_ips) ---
    // a server may carry several equivalent entry addresses (node_ips from the API,
    // same node/keys/ports — only the entry address differs). Every connect starts
    // from a random address of a freshly shuffled pool; a hard failure or a
    // traffic-less "Connected" retries the next address from the pool.
    bool retryWithNextIp();
    void resetIpPool();

    QStringList m_ipPool;
    int m_ipPoolPos = 0;
    int m_ipPoolRow = -1;
    QString m_currentEndpoint; // entry address actually used by the running/last attempt
    bool m_connectionSwitching = false; // teardown states between (re)connects are not failures
    QTimer *m_ipTrafficTimer = nullptr;
    bool m_ipAwaitingTraffic = false; // connected to a multi-IP server, waiting for bytes
    quint64 m_ipTrafficBaseline = 0;
    static constexpr int kIpTrafficTimeoutMs = 8000;
    // ---

    // --- manual-connect watchdog ---
    // a blocked server produces no state events at all (handshake never gets
    // an answer) — without this the UI spins "Connecting..." forever. On
    // expiry: try the next pool address, otherwise fail loudly.
    QTimer *m_manualConnectTimer = nullptr;
    static constexpr int kManualConnectTimeoutMs = 20000;
    // ---

    // --- auto server selection (Settings::isAutoServerSelection) ---
    // protocol priority: AmneziaWgMobile -> AmneziaWg/WireGuard -> Hysteria2 ->
    // VLESS -> VlessXhttpCdn (last-resort fallback). A candidate with rtt <=
    // kAutoAcceptLatencyMs in the best available tier is taken immediately.
    // Candidates the probe did NOT confirm get a post-connect traffic check:
    // no bytes flowing within kAutoTrafficTimeoutMs = try the next one.
    struct AutoCandidate
    {
        int row;
        int tier;
        int latency; // >=0 rtt ms, -1 offline, -2 not probed
    };

    enum class AutoPhase { None, Probing, Connecting };

    void startAutoSelection();
    bool tryEarlyAutoDecision();
    void onAutoHealthUpdated();
    void onAutoProbeSettled();
    void beginAutoConnect();
    void connectCurrentAutoCandidate();
    void cancelAutoSelection();
    void finalizeAutoSuccess();

    QList<AutoCandidate> buildAutoCandidates() const;
    void installMissingConfigs();
    int tierForRow(int row) const;
    bool isVlessCdnRow(int row) const;
    // index into the ordered candidate list; 0 when nothing is probe-confirmed
    // (we still try in priority order — an "offline" badge is not a verdict)
    int pickAutoCandidate(const QList<AutoCandidate> &candidates) const;

    HealthCheckController *m_healthCheckController = nullptr;
    ApiConfigsController *m_apiConfigsController = nullptr;
    AutoPhase m_autoPhase = AutoPhase::None;
    QList<AutoCandidate> m_autoCandidates;
    int m_autoCandidatePos = 0;
    bool m_autoAdvancing = false; // teardown states between attempts are not a user cancel
    bool m_autoAwaitingTraffic = false; // connected to an unprobed server, waiting for bytes
    quint64 m_autoTrafficBaseline = 0;
    QTimer *m_autoProbeTimer;
    QTimer *m_autoAttemptTimer;

    static constexpr int kAutoAcceptLatencyMs = 80;
    static constexpr qint64 kAutoFreshProbeMs = 60000;
    static constexpr int kAutoProbeTimeoutMs = 10000;
    static constexpr int kAutoAttemptTimeoutMs = 15000;
    static constexpr int kAutoTrafficTimeoutMs = 8000;
    // ---

    void continueConnection();

    QSharedPointer<ServersModel> m_serversModel;
    QSharedPointer<ContainersModel> m_containersModel;
    QSharedPointer<ClientManagementModel> m_clientManagementModel;

    QSharedPointer<VpnConnection> m_vpnConnection;

    std::shared_ptr<Settings> m_settings;

    bool m_isConnected = false;
    bool m_isConnectionInProgress = false;
    QString m_connectionStateText = tr("Connect");

    Vpn::ConnectionState m_state;
};

#endif // CONNECTIONCONTROLLER_H
