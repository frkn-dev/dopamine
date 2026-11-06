#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QList>
#include <QStringList>
#include <QTimer>
#include <QElapsedTimer>

#include "protocols/vpnprotocol.h"
#include "ui/models/containers_model.h"
#include "ui/models/servers_model.h"
#include "vpnconnection.h"

class HealthCheckController;
class ApiConfigsController;
class QTcpSocket;

class ConnectionController : public QObject
{
    Q_OBJECT

public:
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool isConnectionInProgress READ isConnectionInProgress NOTIFY connectionStateChanged)
    Q_PROPERTY(QString connectionStateText READ connectionStateText NOTIFY connectionStateChanged)
    Q_PROPERTY(QString currentEndpoint READ currentEndpoint NOTIFY connectionStateChanged)
    Q_PROPERTY(QString downloadSpeed READ downloadSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString uploadSpeed READ uploadSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString ping READ ping NOTIFY pingChanged)
    Q_PROPERTY(bool isWaitingForNetwork READ isWaitingForNetwork NOTIFY connectionStateChanged)

    explicit ConnectionController(const QSharedPointer<ServersModel> &serversModel, const QSharedPointer<ContainersModel> &containersModel,
                                  const QSharedPointer<VpnConnection> &vpnConnection, const std::shared_ptr<Settings> &settings,
                                  QObject *parent = nullptr);

    ~ConnectionController() override;

    // needed only for auto server selection; may stay null (auto falls back to default server)
    void setHealthCheckController(HealthCheckController *healthCheckController);
    // used by auto selection to install per-row configs from the gateway (they are
    // otherwise fetched lazily on the first manual connect)
    void setApiConfigsController(ApiConfigsController *apiConfigsController);

    bool isConnected() const;
    bool isConnectionInProgress() const;
    QString connectionStateText() const;
    QString currentEndpoint() const { return m_currentEndpoint; }
    // байты, прошедшие через туннель в этой сессии (для отчёта)
    qint64 sessionRxBytes() const { return m_rxSinceConnect; }
    qint64 sessionTxBytes() const { return m_txSinceConnect; }
    // порт, на который реально шли (для карточки: «Port in use»)
    int currentPort() const { return m_currentPort; }
    QString downloadSpeed() const { return m_downloadSpeed; }
    QString uploadSpeed() const { return m_uploadSpeed; }
    QString ping() const { return m_ping; }
    // true пока мы в цикле «сеть пропала — ждём и переподключаемся сами»
    bool isWaitingForNetwork() const { return m_waitingForNetwork; }

public slots:
    void toggleConnection();

    void openConnection();
    void closeConnection();

    ErrorCode getLastConnectionError();
    void onConnectionStateChanged(Vpn::ConnectionState state);

    void onTranslationsUpdated();

    // true = последний разрыв туннеля был нашим действием по кнопке юзера
    bool userDisconnectRequested() const { return m_userDisconnectRequested; }

signals:
    void connectToVpn(int serverIndex, const ServerCredentials &credentials, DockerContainer container, const QJsonObject &vpnConfiguration);
    void disconnectFromVpn();

    void connectionStateChanged();

    void connectionErrorOccurred(ErrorCode errorCode);

    void connectButtonClicked();
    void preparingConfig();
    void prepareConfig();
    void speedChanged();
    void pingChanged();

private:
    Vpn::ConnectionState getCurrentConnectionState();

    void connectToServerIndex(int serverIndex);
    void connectToServerIndexWithIp(int serverIndex, const QString &ip, int port = 0);

    // --- выбор адресов пула (пользователь может выключить отдельные IP) ---
    Q_INVOKABLE QStringList poolAddresses();
    // адреса текущего пула, которые пользователь выключил (для биндинга тумблеров)
    Q_INVOKABLE QStringList poolDisabledAddresses();
    Q_INVOKABLE bool isPoolAddressEnabled(const QString &ip);
    Q_INVOKABLE void setPoolAddressEnabled(const QString &ip, bool enabled);
    // то же, но с явным индексом строки сервера: карточка знает, какой сервер
    // показывает, а processedIndex к моменту создания страницы может быть ещё -1
    Q_INVOKABLE QStringList poolDisabledAddressesFor(const int serverIndex);
    Q_INVOKABLE void setPoolAddressEnabledFor(const int serverIndex, const QString &ip, bool enabled);
    // диагностический лог тумблеров: пишем в файл, его можно забрать с устройства
    Q_INVOKABLE void logEndpointEvent(const QString &message);
    QString poolKeyForRow(int row) const;

    // --- ожидание сети / бесконечный реконнект ---
    // Пока m_waitingForNetwork, приложение не показывает ошибку: держит кнопку в
    // состоянии «Отмена» и повторяет попытки с бэкоффом, пока сеть не вернётся.
    void beginWaitingForNetwork();
    void retryConnectionNow();
    // Единая точка сбоя коннекта: при включённом «переподключаться до отмены»
    // уводит в цикл ожидания сети вместо показа ошибки.
    void failOrRetry(ErrorCode code);
    void resetRetryState();
    void recordAttempt(const QString &outcome, const QString &detail = QString());
    // Быстрый фейл старта туннеля (<1.5 c) — это гонка с остановкой прошлой
    // сессии, а не мёртвый адрес: повторяем тот же адрес, а не перебираем пул.
    bool retrySameAddressAfterFastFail();

    // «Подключено, но данные не идут»: пассивный триггер (tx есть, rx нет)
    // + активная проверка DNS через туннель сразу после Connected.
    void startPostConnectCheck();
    void handleNoData(const QString &reason);
    QString m_fastFailAddress;
    bool m_postCheckPending = false;
    bool m_tunnelConfirmed = false; // данные реально пошли (байты/DNS)
    void confirmTunnel();
    bool m_noDataReported = false;
    qint64 m_rxSinceConnect = 0;
    qint64 m_txSinceConnect = 0;
    QTimer *m_postCheckTimer = nullptr;
    int retryDelayMs() const;

    QTimer *m_retryTimer = nullptr;
    int m_retryAttempt = 0;
    bool m_waitingForNetwork = false;
    int m_retryServerIndex = -1;

    // --- multi-IP failover (node_ips) ---
    // a server may carry several equivalent entry addresses (node_ips from the API,
    // same node/keys/ports — only the entry address differs). Every connect starts
    // from a random address of a freshly shuffled pool; a hard failure or a
    // traffic-less "Connected" retries the next address from the pool.
    // A pool may span several servers (N servers × M addresses) — the retry walks
    // all (row, ip) pairs in random order to spread load across the pool.
    bool retryWithNextIp();
    void resetIpPool();
    // row of the pool entry currently being attempted (-1 when no pool is active)
    int currentPoolRow() const;

    struct PoolEntry
    {
        int row;
        QString ip; // empty = dial the row's own hostName (CDN front / single address)
        int port = 0; // 0 = порт протокола; иначе node_ports[ip]
        int weight = 100; // node_weights[ip]; дефолт 100, 0/минус → 100
    };
    QList<PoolEntry> m_ipPool;
    int m_ipPoolPos = -1;
    // Адреса, отброшенные пробой/фильтром. Их пробуем ВТОРЫМ кругом: вердикт
    // пробы может быть ошибочным (не тот порт, устаревший кэш), а живой адрес
    // юзеру нужнее, чем наша уверенность.
    QList<PoolEntry> m_ipFallbackPool;
    int m_ipFallbackPos = -1;
    QString m_currentEndpoint; // entry address actually used by the running/last attempt
    int m_currentPort = 0; // порт, на который реально шли (0 = не знаем)
    // ВРЕМЕННО: таймлайн попытки для разбора «долгого коннекта» по логам с устройства.
    QElapsedTimer m_dbgClock;
    void dbgMark(const QString &what);
    QElapsedTimer m_attemptElapsed; // сколько шла текущая попытка
    bool m_connectionSwitching = false; // teardown states between (re)connects are not failures
    QTimer *m_ipTrafficTimer = nullptr;
    bool m_ipAwaitingTraffic = false; // connected to a multi-IP server, waiting for bytes

    // live speed meter (server card): computed from bytesChanged deltas
    QString m_downloadSpeed;
    QString m_uploadSpeed;
    QElapsedTimer m_speedTimer;
    static QString formatSpeed(qint64 bytesPerSec);

    // live ping (server card), TCP handshake RTT through the tunnel
    void startLivePing();
    void stopLivePing();
    void probeLivePing();
    void onLivePingFailed();
    void finishLivePing(QTcpSocket *socket, bool ok);

    QString m_ping;
    QTimer *m_pingTimer = nullptr;
    QTcpSocket *m_pingSocket = nullptr;
    QElapsedTimer m_pingElapsed;
    int m_pingHostIndex = 0;
    static constexpr int kLivePingIntervalMs = 3000;
    static constexpr int kLivePingTimeoutMs = 4000;
    // Туннель «поднят», но данные не идут: считаем подряд идущие неудачные
    // пробы (по 3 с) и через ~30 с либо перезапускаем, либо честно пишем,
    // что через VPN интернета нет.
    static constexpr int kLivePingFailThreshold = 9;
    static constexpr int kAutoRestartWindowMs = 10 * 60 * 1000;
    static constexpr int kAutoRestartMax = 3;
    static constexpr int kIpTrafficTimeoutMs = 8000;
    // iOS/Android: NE отдаёт Connected до рукопожатия, а AWG keepalive — раз в 25 с.
    // Окно должно быть больше keepalive, иначе живой адрес сочтут мёртвым.
    static constexpr int kNeTrafficTimeoutMs = 30000;
    // ---

    // --- manual-connect watchdog ---
    // a blocked server produces no state events at all (handshake never gets
    // an answer) — without this the UI spins "Connecting..." forever. On
    // expiry: try the next pool address, otherwise fail loudly.
    QTimer *m_manualConnectTimer = nullptr;
    static constexpr int kManualConnectTimeoutMs = 20000;
    // «Preparing» дольше этого = валидация конфига зависла (плохая сеть):
    // не ждём api, идём по сохранённому конфигу и перебираем адреса.
    static constexpr int kPrepareWatchdogMs = 7000;
    // в этом режиме на адрес даём меньше времени, чтобы успеть перебрать пул
    static constexpr int kBypassAttemptTimeoutMs = 8000;
    // Быстрый путь: не ответил за 6 с — уходим в алгоритм, не держим 20 с.
    static constexpr int kFastPathAttemptTimeoutMs = 6000;
    // Пинг — только вето: адрес живой, но хуже лучшего на 150 мс и при этом
    // медленнее 200 мс — не участвует в выборе (веса его не спасают).
    static constexpr int kPingVetoDeltaMs = 150;
    static constexpr int kPingVetoAbsoluteMs = 200;
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
    // Перед первым коннектом по строке без замеров — быстрый прогон проб,
    // чтобы не уходить случайным адресом в мёртвый (первый коннект чуть медленнее,
    // зато не 30 секунд до переключения).
    bool m_awaitConnectProbe = false;
    int m_awaitConnectRow = -1;
    QTimer *m_connectProbeGuard = nullptr;
    QMetaObject::Connection m_connectProbeConn;
    bool connectNeedsProbe(int serverIndex);
    ApiConfigsController *m_apiConfigsController = nullptr;
    AutoPhase m_autoPhase = AutoPhase::None;
    QList<AutoCandidate> m_autoCandidates;
    int m_autoCandidatePos = 0;
    bool m_autoAdvancing = false; // teardown states between attempts are not a user cancel
    bool m_autoAwaitingTraffic = false; // connected to an unprobed server, waiting for bytes
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

    QSharedPointer<VpnConnection> m_vpnConnection;

    std::shared_ptr<Settings> m_settings;

    // true = туннель снимаем мы сами по действию юзера, а не из-за сбоя:
    // нужно, чтобы автоотчёт не уходил на нормальное «выключил VPN».
    bool m_userDisconnectRequested = false;
    QTimer *m_prepareWatchdog = nullptr;
    // Если за 7 с после тапа Connect не подключились — бросаем пробы и
    // автовыбор, идём по адресам по очереди (плохая сеть = вечный probing).
    QTimer *m_connectWatchdog = nullptr;
    bool m_connectWatchdogFired = false;
    bool m_prepareBypass = false; // готовимся по кэшу, без запросов к api
    // последний connect-event: не спамим при реконнектах одного и того же адреса
    qint64 m_lastConnectEventMs = 0;
    QString m_lastConnectEventKey;
    bool m_isConnected = false;
    bool m_isConnectionInProgress = false;
    QString m_connectionStateText = tr("Connect");
    int m_livePingFails = 0;
    int m_autoRestarts = 0;
    qint64 m_autoRestartWindowStart = 0;
    bool m_tunnelFlaggedDead = false;
    // Быстрый путь: сначала пробуем адрес, на котором в прошлый раз поднялось
    // соединение; алгоритм (пробы + веса) включается только если он не ожил.
    bool m_fastPathTried = false;
    bool m_fastPathConsumed = false;
    // Именно «сейчас идёт дозвон быстрого пути»: пока он жив, таймаут попытки
    // короткий (6 с), иначе мёртвый адрес держал юзера все 20 секунд.
    bool m_fastPathDialActive = false;
    // Смена карточки на живом туннеле: гасим старый туннель и только потом
    // дозваниваемся в новый — иначе iOS рисует Connecting → Disconnecting →
    // Connecting, и юзер не понимает, что происходит.
    int m_pendingSwitchRow = -1;
    bool m_switchWaiting = false;
    // Идентичность строки, на которой мы реально поднялись в этой сессии.
    // Быстрый путь имеет смысл только для неё: при смене карточки юзер должен
    // видеть «Calculating best options…», а не непонятный дозвон в пустоту.
    QString m_lastConnectedRowKey;
    QString m_fastPathEndpoint;
    int m_fastPathPort = 0;
    bool tryAlgorithmAfterFastPath();
    // Идентичность строки для «памяти успешного адреса» (uuid → pool|proto).
    QString rowMemoryKey(int serverIndex) const;

    Vpn::ConnectionState m_state;
};

#endif // CONNECTIONCONTROLLER_H
