#include <QNetworkInformation>
#include "connectionController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <cstdio>

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(MACOS_NE)
    #include <QGuiApplication>
#else
    #include <QApplication>
#endif

#ifdef Q_OS_WIN
    #include "platforms/windows/windowsutils.h"
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

void patchAddressInJsonObject(QJsonObject &obj, const QString &ip, int port = 0);

QString patchAddressInConfigString(const QString &configStr, const QString &ip, int port = 0)
{
    const QString trimmed = configStr.trimmed();
    if (trimmed.startsWith(QLatin1Char('{'))) {
        QJsonObject obj = QJsonDocument::fromJson(configStr.toUtf8()).object();
        if (obj.isEmpty()) {
            return configStr;
        }
        patchAddressInJsonObject(obj, ip, port);
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
    if (trimmed.contains(QStringLiteral("[Interface]"), Qt::CaseInsensitive)) {
        static const QRegularExpression endpointRe(
                QStringLiteral("(?m)^(\\s*Endpoint\\s*=\\s*)([^:\\s]+)(:\\d+\\s*)$"));
        // порт адреса из node_ports: подменяем host:port целиком
        if (port > 0) {
            return QString(configStr).replace(endpointRe,
                                             QStringLiteral("\\1") + ip + QStringLiteral(":") + QString::number(port));
        }
        return QString(configStr).replace(endpointRe, QStringLiteral("\\1") + ip + QStringLiteral("\\3"));
    }
    return configStr;
}

void patchAddressInJsonObject(QJsonObject &obj, const QString &ip, int port)
{
    if (obj.contains(QStringLiteral("hostName"))) {
        obj[QStringLiteral("hostName")] = ip;
    }
    if (port > 0 && obj.contains(QStringLiteral("port"))) {
        obj[QStringLiteral("port")] = port;
    }
    if (obj.contains(QStringLiteral("server"))) {
        // hysteria-style "host:port"
        const QString server = obj.value(QStringLiteral("server")).toString();
        const int colon = server.lastIndexOf(QLatin1Char(':'));
        const QString suffix = port > 0 ? (QStringLiteral(":") + QString::number(port))
                                        : (colon > 0 ? server.mid(colon) : QString());
        obj[QStringLiteral("server")] = ip + suffix;
    }
    QJsonArray outbounds = obj.value(QStringLiteral("outbounds")).toArray();
    for (int i = 0; i < outbounds.size(); ++i) {
        QJsonObject outbound = outbounds.at(i).toObject();
        QJsonObject settingsObj = outbound.value(QStringLiteral("settings")).toObject();
        bool touched = false;
        QJsonArray vnext = settingsObj.value(QStringLiteral("vnext")).toArray();
        for (int j = 0; j < vnext.size(); ++j) {
            QJsonObject v = vnext.at(j).toObject();
            if (v.contains(QStringLiteral("address"))) {
                v[QStringLiteral("address")] = ip;
                if (port > 0) {
                    v[QStringLiteral("port")] = port;
                }
                touched = true;
            }
            vnext.replace(j, v);
        }
        if (!vnext.isEmpty()) {
            settingsObj[QStringLiteral("vnext")] = vnext;
        }
        // hysteria2, legacy schema (what the backend stores): dial address lives in
        // settings.servers[].address. serverName/SNI must stay the original domain —
        // TLS verifies against it, not the dial address, so any node IP works
        QJsonArray servers = settingsObj.value(QStringLiteral("servers")).toArray();
        for (int j = 0; j < servers.size(); ++j) {
            QJsonObject s = servers.at(j).toObject();
            if (s.contains(QStringLiteral("address"))) {
                s[QStringLiteral("address")] = ip;
                if (port > 0 && s.contains(QStringLiteral("port"))) {
                    s[QStringLiteral("port")] = port;
                }
                touched = true;
            }
            servers.replace(j, s);
        }
        if (!servers.isEmpty()) {
            settingsObj[QStringLiteral("servers")] = servers;
        }
        // hysteria2, translated schema (protocol "hysteria"): host only, port stays
        if (settingsObj.contains(QStringLiteral("address"))) {
            settingsObj[QStringLiteral("address")] = ip;
            touched = true;
        }
        if (touched) {
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

void patchProtocolObject(QJsonObject &protocolObject, const QString &ip, int port)
{
    if (protocolObject.contains(QStringLiteral("hostName"))) {
        protocolObject[QStringLiteral("hostName")] = ip;
    }
    if (port > 0 && protocolObject.contains(QStringLiteral("port"))) {
        protocolObject[QStringLiteral("port")] = port;
    }
    const QString lastConfig = protocolObject.value(QStringLiteral("last_config")).toString();
    if (!lastConfig.isEmpty()) {
        protocolObject[QStringLiteral("last_config")] = patchAddressInConfigString(lastConfig, ip, port);
    }
    const QString configStr = protocolObject.value(QStringLiteral("config")).toString();
    if (!configStr.isEmpty()) {
        protocolObject[QStringLiteral("config")] = patchAddressInConfigString(configStr, ip, port);
    }
}

void patchContainerObject(QJsonObject &containerObject, const QString &ip, int port)
{
    // flat (CDN) form: the container object itself may hold last_config/config
    patchProtocolObject(containerObject, ip, port);
    // nested protocol objects ("amnezia-awg", "awg", "xray", ...)
    for (auto it = containerObject.begin(); it != containerObject.end(); ++it) {
        if (it.value().isObject()) {
            QJsonObject protocolObject = it.value().toObject();
            patchProtocolObject(protocolObject, ip, port);
            containerObject[it.key()] = protocolObject;
        }
    }
}

void patchServerConfigAddress(QJsonObject &serverConfig, const QString &ip, int port)
{
    serverConfig[QStringLiteral("hostName")] = ip;
    if (port > 0 && serverConfig.contains(QStringLiteral("port"))) {
        serverConfig[QStringLiteral("port")] = port;
    }
    QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();
    for (int i = 0; i < containers.size(); ++i) {
        QJsonObject containerObject = containers.at(i).toObject();
        patchContainerObject(containerObject, ip, port);
        containers.replace(i, containerObject);
    }
    if (!containers.isEmpty()) {
        serverConfig[QStringLiteral("containers")] = containers;
    }
}

} // namespace

ConnectionController::~ConnectionController() = default;

// диагностический лог тумблеров (определение ниже): нужен уже в конструкторе
static void appendEndpointLog(const QString &line);

// Перехват сообщений Qt (включая ошибки QML/JS) в нашу трассу: без этого
// исключение в обработчике молча убивает его, и на экране «ничего не нажимается».
static Settings *g_diagSettings = nullptr;
static void diagMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    static thread_local bool inside = false;
    Q_UNUSED(context);

    // Только предупреждения и ошибки: раньше сюда попадал и qDebug, включая
    // многокилобайтные дампы [AGW REQUEST] — каждая строка давала синхронную
    // запись plist, UI-поток захлёбывался и приложение показывало чёрный экран.
    if (type == QtDebugMsg || type == QtInfoMsg) {
        return;
    }

    fprintf(stderr, "%s\n", qPrintable(msg));

    if (!g_diagSettings || inside) {
        return;
    }
    inside = true;
    QString line = QStringLiteral("QT: ") + msg;
    if (line.size() > 400) {
        line = line.left(400);
    }
    g_diagSettings->appendDiagTrace(line);
    inside = false;

    if (type == QtFatalMsg) {
        abort();
    }
}

ConnectionController::ConnectionController(const QSharedPointer<ServersModel> &serversModel,
                                           const QSharedPointer<ContainersModel> &containersModel,
                                           const QSharedPointer<VpnConnection> &vpnConnection, const std::shared_ptr<Settings> &settings,
                                           QObject *parent)
    : QObject(parent),
      m_serversModel(serversModel),
      m_containersModel(containersModel),
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
        qDebug() << "[IPPOOL] connected but no traffic on row" << currentPoolRow() << ", trying next address";
        if (!retryWithNextIp()) {
            resetIpPool();
            emit disconnectFromVpn();
            failOrRetry(m_vpnConnection->lastError());
        }
    });

    m_manualConnectTimer = new QTimer(this);
    m_manualConnectTimer->setSingleShot(true);
    connect(m_manualConnectTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[CONNECT] manual attempt timed out, row" << currentPoolRow();
        const bool wasFastPath = m_fastPathDialActive;
        m_fastPathDialActive = false;
        Q_UNUSED(wasFastPath);
        if (retryWithNextIp()) {
            return; // connectToServerIndexWithIp restarts the watchdog
        }
        resetIpPool();
        m_manualConnectTimer->stop();
        recordAttempt(QStringLiteral("timeout"));
        if (tryAlgorithmAfterFastPath()) {
            return; // быстрый путь не ожил — считаем варианты по-настоящему
        }
        // Вместо «ошибка и стоп» — ждём сеть и пробуем сами, пока пользователь
        // не нажмёт «Отмена». Иначе пропавший на 20 минут интернет оставляет
        // приложение в красном состоянии, и человек думает, что сломался сервер.
        if (m_settings->isReconnectUntilCancelled()) {
            beginWaitingForNetwork();
            return;
        }
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        emit disconnectFromVpn();
        emit connectionErrorOccurred(ErrorCode::ServerConnectionTimeoutError);
        emit connectionStateChanged();
    });

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &ConnectionController::retryConnectionNow);

    // Сеть вернулась — не ждём очередного тика бэкоффа, а подключаемся сразу.
    if (QNetworkInformation::loadDefaultBackend()) {
        if (auto *networkInfo = QNetworkInformation::instance()) {
            connect(networkInfo, &QNetworkInformation::reachabilityChanged, this,
                    [this](QNetworkInformation::Reachability reachability) {
                        if (m_waitingForNetwork && reachability == QNetworkInformation::Reachability::Online) {
                            qDebug() << "[RETRY] network is back, reconnecting immediately";
                            retryConnectionNow();
                        }
                    });
        }
    }

#if defined(Q_OS_IOS) || defined(MACOS_NE) || defined(Q_OS_ANDROID)
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive || !m_isConnected) {
            return;
        }
        m_speedTimer.invalidate();
        if (m_pingSocket) {
            QTcpSocket *socket = m_pingSocket;
            m_pingSocket = nullptr;
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
        }
        if (m_pingTimer) {
            if (!m_pingTimer->isActive()) {
                m_pingTimer->start();
            }
            probeLivePing();
        }
    });
#endif

    // traffic proof: bytes must move. Used by auto selection for probe-unconfirmed
    // candidates and by the multi-IP failover for every multi-IP connect
    connect(m_vpnConnection.get(), &VpnConnection::bytesChanged, this, [this](quint64 receivedBytes, quint64 sentBytes) {
        m_rxSinceConnect += static_cast<qint64>(receivedBytes);
        m_txSinceConnect += static_cast<qint64>(sentBytes);
        if (m_settings) {
            m_settings->setLiveTraffic(m_rxSinceConnect, m_txSinceConnect);
        }
        if (!m_tunnelConfirmed && (receivedBytes > 0 || sentBytes > 0)) {
            m_tunnelConfirmed = true; // просто факт «данные пошли», UI не трогаем
        }
        if (m_postCheckPending && m_isConnected && m_txSinceConnect > 20000 && m_rxSinceConnect == 0) {
            // не перестраиваем коннект из обработчика счётчиков синхронно —
            // переносим на цикл событий, иначе UI ловит рекурсию/гонку
            QMetaObject::invokeMethod(this, [this]() { handleNoData(QStringLiteral("one-way")); },
                                      Qt::QueuedConnection);
        }
        // live speed meter for the server card: bytesChanged carries per-interval
        // deltas on all platforms, so speed = delta / elapsed
        if (m_isConnected) {
            const qint64 elapsedMs = m_speedTimer.isValid() ? m_speedTimer.restart() : 0;
            if (!m_speedTimer.isValid()) {
                m_speedTimer.start();
            }
            if (elapsedMs > 0 && elapsedMs <= 5000) {
                const quint64 downBps = receivedBytes * 1000 / static_cast<quint64>(elapsedMs);
                const quint64 upBps = sentBytes * 1000 / static_cast<quint64>(elapsedMs);
                constexpr quint64 kMaxBps = 10ull * 1024 * 1024 * 1024;
                if (downBps <= kMaxBps && upBps <= kMaxBps) {
                    const QString down = formatSpeed(static_cast<qint64>(downBps));
                    const QString up = formatSpeed(static_cast<qint64>(upBps));
                    if (down != m_downloadSpeed || up != m_uploadSpeed) {
                        m_downloadSpeed = down;
                        m_uploadSpeed = up;
                        emit speedChanged();
                    }
                }
            }
        }

        if (m_autoPhase == AutoPhase::Connecting && m_autoAwaitingTraffic) {
            // bytesChanged carries per-interval deltas on all platforms — any
            // non-zero receive is proof the tunnel moves traffic
            if (receivedBytes > 0) {
                qDebug() << "[AUTO] traffic confirmed on row" << m_autoCandidates.at(m_autoCandidatePos).row;
                finalizeAutoSuccess();
            }
            return;
        }
        if (m_autoPhase == AutoPhase::None && m_ipAwaitingTraffic) {
            if (receivedBytes > 0) {
                qDebug() << "[IPPOOL] traffic confirmed on row" << currentPoolRow();
                resetIpPool(); // success — the next connect starts from a fresh random pool
            }
        }
    });

    m_state = Vpn::ConnectionState::Disconnected;

    // Диагностика тумблеров — только по DOPAMINE_DIAG=1: она пишет в настройки
    // на каждое предупреждение Qt и нужна лишь при разборе проблем на устройстве.
    if (qEnvironmentVariableIsSet("DOPAMINE_DIAG")) {
        m_settings->setDiagBuildMarker(
            QStringLiteral("%1 %2").arg(QStringLiteral(APP_VERSION),
                                        QStringLiteral(__DATE__ " " __TIME__)));
        m_settings->clearDiagTrace();
        m_settings->appendDiagTrace(QStringLiteral("startup"));
        g_diagSettings = m_settings.get();
        qInstallMessageHandler(diagMessageHandler);
        m_settings->appendDiagTrace(QStringLiteral("meta: logEndpointEvent index=%1")
                                        .arg(metaObject()->indexOfMethod("logEndpointEvent(QString)")));
    } else {
        // обычный запуск: подчищаем диагностические ключи прошлых сборок
        m_settings->clearDiagTrace();
    }

    // Проверка хранилища переключателей без кликов: DOPAMINE_ENDPOINT_TEST=1
    // пишет тестовую запись, читает её обратно и убирает за собой.
    if (qEnvironmentVariableIsSet("DOPAMINE_ENDPOINT_TEST")) {
        QTimer::singleShot(5000, this, [this]() {
            // повторяем ровно тот путь, что делает карточка: выключаем первый
            // адрес пула у первого awg-сервера и читаем через ту же функцию
            int row = -1;
            QString ip;
            for (int i = 0; i < m_serversModel->getServersCount() && row < 0; ++i) {
                const QString protocol = m_serversModel->data(i, ServersModel::Roles::ServiceProtocolRole).toString();
                if (protocol != QStringLiteral("awg") && protocol != QStringLiteral("wireguard")) {
                    continue;
                }
                const QJsonArray nodeIps = m_serversModel->getServerConfig(i).value(QStringLiteral("node_ips")).toArray();
                if (nodeIps.isEmpty()) {
                    continue;
                }
                row = i;
                ip = nodeIps.first().toString();
            }
            if (row < 0 || ip.isEmpty()) {
                qWarning() << "[EPTEST] no awg server with node_ips";
                return;
            }

            qDebug() << "[EPTEST] row" << row << "ip" << ip;
            qDebug() << "[EPTEST] read before:" << poolDisabledAddressesFor(row);

            setPoolAddressEnabledFor(row, ip, false);
            const QStringList afterDisable = poolDisabledAddressesFor(row);
            qDebug() << "[EPTEST] read after disable:" << afterDisable;

            // DOPAMINE_ENDPOINT_TEST_KEEP=1 оставляет адрес выключенным:
            // так проверяем, что состояние реально легло в файл настроек
            if (qEnvironmentVariableIsSet("DOPAMINE_ENDPOINT_TEST_KEEP")) {
                qDebug() << "[EPTEST] keep disabled, skipping re-enable";
                return;
            }

            setPoolAddressEnabledFor(row, ip, true);
            qDebug() << "[EPTEST] read after enable:" << poolDisabledAddressesFor(row);
        });
    }
}

QString ConnectionController::formatSpeed(qint64 bytesPerSec)
{
    if (bytesPerSec < 1024) {
        return QStringLiteral("%1 B/s").arg(bytesPerSec);
    }
    if (bytesPerSec < 1024 * 1024) {
        return QStringLiteral("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB/s").arg(bytesPerSec / 1024.0 / 1024.0, 0, 'f', 1);
}

void ConnectionController::startLivePing()
{
    if (!m_pingTimer) {
        m_pingTimer = new QTimer(this);
        m_pingTimer->setInterval(kLivePingIntervalMs);
        connect(m_pingTimer, &QTimer::timeout, this, &ConnectionController::probeLivePing);
    }
    if (m_pingTimer->isActive()) {
        return;
    }
    m_pingTimer->start();
    probeLivePing();
}

void ConnectionController::stopLivePing()
{
    if (m_pingTimer) {
        m_pingTimer->stop();
    }
    if (m_pingSocket) {
        QTcpSocket *socket = m_pingSocket;
        m_pingSocket = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    if (!m_ping.isEmpty()) {
        m_ping.clear();
        emit pingChanged();
    }
}

// Туннель числится поднятым, но пробы через него не проходят. Не врём юзеру
// «Connected»: либо переподключаемся сами (если включён keep reconnecting),
// либо пишем, что интернета через VPN нет.
void ConnectionController::onLivePingFailed()
{
    if (++m_livePingFails < kLivePingFailThreshold) {
        return;
    }
    m_livePingFails = 0;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_autoRestartWindowStart == 0 || now - m_autoRestartWindowStart > kAutoRestartWindowMs) {
        m_autoRestartWindowStart = now;
        m_autoRestarts = 0;
    }

    const bool keepTrying = m_settings && m_settings->isReconnectUntilCancelled();
    if (!keepTrying) {
        if (!m_tunnelFlaggedDead) {
            qWarning() << "[LIVENESS] no traffic through the tunnel — reporting 'no internet'";
            m_tunnelFlaggedDead = true;
            m_connectionStateText = tr("Connected — no internet");
            emit connectionStateChanged();
        }
        return;
    }

    if (m_autoRestarts >= kAutoRestartMax) {
        if (!m_tunnelFlaggedDead) {
            qWarning() << "[LIVENESS] tunnel dead, auto-restart budget exhausted"
                       << m_autoRestarts << "in 10 min";
            m_tunnelFlaggedDead = true;
            m_connectionStateText = tr("Connected — no internet");
            emit connectionStateChanged();
        }
        return;
    }

    ++m_autoRestarts;
    m_tunnelFlaggedDead = true;
    m_connectionStateText = tr("Reconnecting…");
    emit connectionStateChanged();
    qWarning() << "[LIVENESS] tunnel is not passing traffic — restarting"
               << "(attempt" << m_autoRestarts << "of" << kAutoRestartMax << ")";

    // первый перезапуск — тот же адрес; если и он мёртв, уходим на следующий
    if (m_autoRestarts <= 1 || m_ipPool.isEmpty()) {
        connectToServerIndexWithIp(currentPoolRow() >= 0 ? currentPoolRow()
                                                         : m_serversModel->getProcessedServerIndex(),
                                   m_currentEndpoint, m_currentPort);
    } else if (!retryWithNextIp()) {
        connectToServerIndexWithIp(currentPoolRow() >= 0 ? currentPoolRow()
                                                         : m_serversModel->getProcessedServerIndex(),
                                   m_currentEndpoint, m_currentPort);
    }
}

void ConnectionController::probeLivePing()
{
    if (!m_isConnected || m_pingSocket) {
        return;
    }

    // Pre-connect health probes are stopped once the tunnel is up (their packets
    // would be captured by it). This measures the latency the session actually
    // has: a TCP handshake through the tunnel to a public anycast.
    static const QHostAddress hosts[] = {
        QHostAddress(QStringLiteral("1.1.1.1")),
        QHostAddress(QStringLiteral("8.8.8.8")),
        QHostAddress(QStringLiteral("1.0.0.1")),
        QHostAddress(QStringLiteral("9.9.9.9")),
    };
    static constexpr int hostCount = int(sizeof(hosts) / sizeof(hosts[0]));

    auto *socket = new QTcpSocket(this);
    m_pingSocket = socket;
    m_pingElapsed.start();

    connect(socket, &QTcpSocket::connected, this, [this, socket]() { finishLivePing(socket, true); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QTcpSocket::SocketError) {
        finishLivePing(socket, false);
    });
    QTimer::singleShot(kLivePingTimeoutMs, socket, [this, socket]() {
        if (m_pingSocket == socket && socket->state() != QAbstractSocket::ConnectedState) {
            finishLivePing(socket, false);
        }
    });

    socket->connectToHost(hosts[m_pingHostIndex % hostCount], 443);
}

void ConnectionController::finishLivePing(QTcpSocket *socket, bool ok)
{
    if (m_pingSocket != socket) {
        return;
    }

    const int ms = static_cast<int>(m_pingElapsed.elapsed());
    m_pingSocket = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();

    if (!ok || !m_isConnected) {
        if (m_isConnected) {
            m_pingHostIndex = (m_pingHostIndex + 1) % 4;
            onLivePingFailed();
        }
        return;
    }

    // проба прошла — туннель живой
    if (m_livePingFails != 0 || m_tunnelFlaggedDead) {
        m_livePingFails = 0;
        if (m_tunnelFlaggedDead) {
            m_tunnelFlaggedDead = false;
            m_connectionStateText = tr("Connected");
            emit connectionStateChanged();
            qDebug() << "[LIVENESS] tunnel is passing traffic again";
        }
    }

    const QString text = QString::number(qMax(ms, 1));
    if (text != m_ping) {
        m_ping = text;
        emit pingChanged();
    }
}

void ConnectionController::setHealthCheckController(HealthCheckController *healthCheckController)
{
    m_healthCheckController = healthCheckController;
}

void ConnectionController::setApiConfigsController(ApiConfigsController *apiConfigsController)
{
    m_apiConfigsController = apiConfigsController;
}

void ConnectionController::dbgMark(const QString &what)
{
    const qint64 ms = m_dbgClock.isValid() ? m_dbgClock.elapsed() : 0;
    qDebug().noquote() << "[TIMELINE]" << QStringLiteral("%1ms %2").arg(ms).arg(what);
}

void ConnectionController::openConnection()
{
    // Смена карточки в UI вызывает openConnection() напрямую (не через
    // toggleConnection) — и флаг «быстрый путь уже пробовали» оставался
    // поднятым с прошлой строки, поэтому новый пул каждый раз шёл полным
    // алгоритмом (долгий коннект). Сбрасываем здесь — это вход в новую попытку.
    m_dbgClock.start();
    dbgMark("openConnection");
    m_fastPathTried = false;
    m_fastPathConsumed = false;
    m_fastPathEndpoint.clear();
    m_fastPathPort = 0;

    // Плохая сеть: «Preparing»/«Probing» могут тянуться бесконечно. Через 7 с
    // бросаем всё лишнее и просто перебираем адреса по очереди.
    if (!m_connectWatchdog) {
        m_connectWatchdog = new QTimer(this);
        m_connectWatchdog->setSingleShot(true);
        connect(m_connectWatchdog, &QTimer::timeout, this, [this]() {
            // Сторож только для ДО-диалных фаз: валидация конфига (Preparing),
            // предконнектная проба и автовыбор. Уже идущий дозвон не трогаем —
            // иначе таймер каждые 7 с обрывал попытку и клиент не подключался
            // вообще (дозвон на плохой сети легально занимает до 20 с).
            const bool preparing = (m_state == Vpn::ConnectionState::Preparing);
            const bool probing = m_awaitConnectProbe || (m_autoPhase == AutoPhase::Probing);
            if (m_isConnected || m_isConnectionInProgress || m_ipPoolPos >= 0) {
                return;
            }
            if (!preparing && !probing) {
                return;
            }
            qWarning() << "[CONNECT] 7s without a connection — dropping probes/auto-select,"
                          " dialing pool addresses one by one";
            m_connectWatchdogFired = true;
            m_prepareBypass = true;
            if (m_awaitConnectProbe) {
                m_awaitConnectProbe = false;
                if (m_connectProbeConn) {
                    disconnect(m_connectProbeConn);
                    m_connectProbeConn = {};
                }
                if (m_connectProbeGuard) {
                    m_connectProbeGuard->stop();
                }
            }
            if (m_healthCheckController) {
                m_healthCheckController->stopProbe();
            }
            cancelAutoSelection();
            const int row = m_serversModel ? m_serversModel->getProcessedServerIndex() : -1;
            connectToServerIndex(row >= 0 ? row : m_serversModel->getDefaultServerIndex());
        });
    }
    if (!m_connectWatchdogFired) {
        m_connectWatchdog->start(kPrepareWatchdogMs);
    }

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    if (!Utils::processIsRunning(Utils::executable(SERVICE_NAME, false), true))
    {
#ifdef Q_OS_WIN
        // the service may be stopped (crash, upgrade) or missing entirely
        // (silent MSI failure without admin rights) — try to revive it with
        // elevation before erroring out
        if (WindowsUtils::ensureDopamineServiceRunning()) {
            qDebug() << "ConnectionController: service recovered, proceeding with connect";
        } else
#endif
        {
            emit connectionErrorOccurred(ErrorCode::DopamineServiceNotRunning);
            return;
        }
    }
#endif

    if (m_settings->isAutoServerSelection() && m_healthCheckController && !m_prepareBypass) {
        startAutoSelection();
        return;
    }

    connectToServerIndex(m_serversModel->getDefaultServerIndex());
}


// Нужен ли прогон проб перед коннектом: у строки нет ни одного свежего замера.
bool ConnectionController::connectNeedsProbe(int serverIndex)
{
    if (serverIndex < 0 || !m_serversModel) {
        return false;
    }
    const QJsonObject config = m_serversModel->getServerConfig(serverIndex);
    const QJsonArray nodeIps = config.value(QStringLiteral("node_ips")).toArray();
    if (nodeIps.isEmpty()) {
        return false; // одиночная строка: мерить нечего, идём в hostName
    }
    bool any = false;
    for (const QJsonValue &v : nodeIps) {
        const QString ip = v.toString();
        if (ip.isEmpty()) {
            continue;
        }
        any = true;
        const int port = ServersModel::portForNodeIp(config, ip, m_serversModel->serverPort(serverIndex));
        // Кэш больше не отменяет расчёт: единый сценарий — всегда считаем
        // лучший вариант (фиксированные 3 с), чтобы поведение было предсказуемым.
    }
    return any;
}

QString ConnectionController::rowMemoryKey(int serverIndex) const
{
    if (!m_serversModel || serverIndex < 0) {
        return QString();
    }
    const QJsonObject apiConfig = m_serversModel->getServerConfig(serverIndex)
                                          .value(QStringLiteral("api_config")).toObject();
    const QString uuid = apiConfig.value(QStringLiteral("connection_uuid")).toString();
    if (!uuid.isEmpty()) {
        return uuid;
    }
    const QString pool = apiConfig.value(QStringLiteral("pool")).toString();
    const QString proto = apiConfig.value(QStringLiteral("service_protocol")).toString();
    if (!pool.isEmpty()) {
        return pool + QStringLiteral("|") + proto;
    }
    return QStringLiteral("row:") + QString::number(serverIndex);
}

void ConnectionController::connectToServerIndex(int serverIndex)
{
    // Истёкшая подписка: нечего дозваниваться — говорим прямо, что оплата
    // кончилась, вместо «не удалось подключиться».
    // Смена карточки при живом туннеле: сначала останавливаем текущий туннель,
    // и только после Disconnected начинаем расчёт и дозвон в новую строку.
    if (m_isConnected && m_serversModel && serverIndex >= 0
        && rowMemoryKey(serverIndex) != m_lastConnectedRowKey) {
        qDebug() << "[SWITCH] card changed while connected — stopping tunnel first, row" << serverIndex;
        m_pendingSwitchRow = serverIndex;
        m_switchWaiting = true;
        m_isConnectionInProgress = true;
        m_connectionStateText = tr("Calculating the best server…");
        emit connectionStateChanged();
        emit disconnectFromVpn();
        return;
    }

    // Скоринг загрузки: обновляем фоном, коннект его не ждёт (нет данных — веса).
    if (m_apiConfigsController) {
        m_apiConfigsController->refreshNodeLoad(serverIndex);
    }

    if (m_serversModel && m_serversModel->isSubscriptionExpired(serverIndex)) {
        qWarning() << "[CONNECT] subscription expired for row" << serverIndex;
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Subscription expired");
        emit connectionErrorOccurred(ErrorCode::ApiSubscriptionExpiredError);
        emit connectionStateChanged();
        return;
    }

    // Первый коннект по строке: прогоняем рукопожатия, чтобы выбрать живой адрес,
    // а не случайный. Ждём не дольше 4 секунд — если проба не успела, идём с тем,
    // что есть (частичные результаты уже записаны).
    // БЫСТРЫЙ ПУТЬ. Юзеру не нужен наш алгоритм каждый раз: если в прошлый раз
    // мы успешно поднялись на конкретном адресе — сразу звоним туда. Пробы,
    // веса и прочая «магия» включаются только если этот адрес не ожил.
    // Быстрый путь — только для той же карточки (выкл/вкл, реконнект). При
    // смене карточки сразу идём в «Calculating the best server…»: иначе юзер видит
    // бессмысленную попытку в чужой/мёртвый адрес и не понимает, что происходит.
    const QString currentRowKey = rowMemoryKey(serverIndex);
    const bool sameCardAsConnected = !m_lastConnectedRowKey.isEmpty()
            && m_lastConnectedRowKey == currentRowKey;
    // Решение продукта: единый сценарий на каждый коннект —
    // «Calculating the best server…» (алгоритм, фиксированные 3 с) → «Connecting».
    // Быстрый путь по памяти карточки выключен: он давал разное поведение
    // в разных случаях и путал пользователя.
    static constexpr bool kUseFastPath = false;
    if (kUseFastPath && !m_fastPathTried && sameCardAsConnected) {
        m_fastPathTried = true;
        // Память конкретной строки (pool/RU, EU и т.д.): при переключении между
        // пулами глобальный lastConnectedEndpoint всегда указывает на последний
        // использованный, и предыдущий пул каждый раз шёл полным алгоритмом.
        const QString memoryKey = rowMemoryKey(serverIndex);
        const QJsonObject rememberedEntry = m_settings ? m_settings->lastGoodEndpointFor(memoryKey)
                                                       : QJsonObject();
        QString remembered = rememberedEntry.value(QStringLiteral("ip")).toString();
        int rememberedPort = rememberedEntry.value(QStringLiteral("port")).toInt();
        if (remembered.isEmpty() && m_settings) {
            // запасной вариант — глобальная память (первый запуск после обновления)
            remembered = m_settings->lastConnectedEndpoint();
            rememberedPort = m_settings->lastConnectedPort();
        }
        if (!remembered.isEmpty()) {
            const QStringList disabled = m_settings->poolDisabledEndpoints();
            if (!disabled.contains(remembered)) {
                m_fastPathEndpoint = remembered;
                // Порт берём из ТЕКУЩЕГО конфига строки, а не из памяти: там
                // мог остаться порт от прежней (ошибочной) раскладки, и дозвон
                // шёл не туда — туннель падал на старте (InternalError 101).
                const QJsonObject fastConfig = m_serversModel->getServerConfig(serverIndex);
                const int configPort = ServersModel::portForNodeIp(
                        fastConfig, remembered, m_serversModel->serverPort(serverIndex));
                m_fastPathPort = configPort > 0 ? configPort
                                                : (rememberedPort > 0 ? rememberedPort
                                                                      : m_serversModel->serverPort(serverIndex));
                qDebug().noquote() << "[FASTPATH] dialing last working endpoint"
                                   << remembered << ":" << m_fastPathPort;
                dbgMark(QStringLiteral("fastpath dial %1:%2").arg(remembered).arg(m_fastPathPort));
                m_fastPathDialActive = true;
                connectToServerIndexWithIp(serverIndex, remembered, m_fastPathPort);
                return;
            }
        }
    }

    if (m_awaitConnectProbe) {
        return; // уже ждём прогон для этой попытки
    }
    if (m_healthCheckController && !m_isConnected && !m_prepareBypass
        && connectNeedsProbe(serverIndex)) {
        m_awaitConnectProbe = true;
        m_awaitConnectRow = serverIndex;
        // Пока считаем варианты — говорим об этом прямо, вместо «Preparing»/
        // пустого Connecting.
        m_isConnectionInProgress = true;
        m_connectionStateText = tr("Calculating the best server…");
        emit connectionStateChanged();

        if (!m_connectProbeGuard) {
            m_connectProbeGuard = new QTimer(this);
            m_connectProbeGuard->setSingleShot(true);
        }
        disconnect(m_connectProbeGuard, nullptr, this, nullptr);
        connect(m_connectProbeGuard, &QTimer::timeout, this, [this]() {
            if (!m_awaitConnectProbe) {
                return;
            }
            m_awaitConnectProbe = false;
            if (m_connectProbeConn) {
                disconnect(m_connectProbeConn);
                m_connectProbeConn = {};
            }
            const int row = m_awaitConnectRow;
            m_awaitConnectRow = -1;
            qDebug() << "[PREPROBE] timeout, connecting with what we have, row" << row;
            // Без этого флага connectToServerIndex() снова уходил в ветку пробу и
            // ждал ещё 7 с — так набегало ~20 с «вечного CONNECTING» до первой
            // попытки дозвона. Один таймаут = идём по сохранённым данным.
            m_prepareBypass = true;
            connectToServerIndex(row);
        });
        // Пул считаем максимум 3 с — это осознанно выбранный бюджет, а не 7 с, как
        // из-за повторного входа в ветку ожидание доходило до ~20 с — юзер видел
        // «вечный CONNECTING». Частичные результаты проб всё равно используются.
        m_connectProbeGuard->start(3000);

        // порядок как везде: сначала запуск, потом подписка
        m_healthCheckController->startProbe(true);
        if (m_connectProbeConn) {
            disconnect(m_connectProbeConn);
            m_connectProbeConn = {};
        }
        m_connectProbeConn = connect(m_healthCheckController, &HealthCheckController::probingFinished, this, [this]() {
            if (!m_awaitConnectProbe) {
                return;
            }
            m_awaitConnectProbe = false;
            if (m_connectProbeGuard) {
                m_connectProbeGuard->stop();
            }
            if (m_connectProbeConn) {
                disconnect(m_connectProbeConn);
                m_connectProbeConn = {};
            }
            const int row = m_awaitConnectRow;
            m_awaitConnectRow = -1;
            qDebug() << "[PREPROBE] done, connecting row" << row;
            connectToServerIndex(row);
        });
        if (!m_healthCheckController->isProbing()) {
            // проба закончилась синхронно
            disconnect(m_connectProbeConn);
            m_connectProbeConn = {};
            m_awaitConnectProbe = false;
            if (m_connectProbeGuard) {
                m_connectProbeGuard->stop();
            }
            const int row = m_awaitConnectRow;
            m_awaitConnectRow = -1;
            connectToServerIndex(row);
        }
        return;
    }

    // multi-IP / multi-node failover: build a freshly shuffled pool of (row, ip)
    // pairs. A pooled API row already carries every pool address in node_ips, but
    // older/pinned gateways expose one node per row — so addresses are also
    // gathered from the sibling rows of the same pool. Every entry keeps its own
    // row: keys/ports must come from the row that owns the address.
    const auto subscriptionIdOf = [](const QJsonObject &config) {
        QJsonObject authData = config.value(QStringLiteral("api_config")).toObject().value(QStringLiteral("auth_data")).toObject();
        if (authData.isEmpty()) {
            authData = config.value(QStringLiteral("auth_data")).toObject();
        }
        const QString apiKey = authData.value(QStringLiteral("api_key")).toString();
        return apiKey.isEmpty() ? authData.value(QStringLiteral("id")).toString() : apiKey;
    };

    const QJsonObject serverConfig = m_serversModel->getServerConfig(serverIndex);
    const QString targetPool =
            serverConfig.value(QStringLiteral("displayInfo")).toObject().value(QStringLiteral("pool")).toString();
    const QString targetProtocol = m_serversModel->data(serverIndex, ServersModel::Roles::ServiceProtocolFilterRole).toString();
    const QString targetEnv = m_serversModel->data(serverIndex, ServersModel::Roles::ConnectionEnvRole).toString();
    const QString targetSubscription = subscriptionIdOf(serverConfig);
    const bool targetIsVlessCdn = isVlessCdnRow(serverIndex);

    QList<PoolEntry> pool;
    QSet<QString> seenIps; // the same pool address may repeat across collapsed rows
    m_ipFallbackPool.clear(); // прошлый прогон не должен подмешиваться
    m_ipFallbackPos = -1;

    // Пул адресов — только у ВЫБРАННОЙ строки. Если юзер выбрал карточку, он должен
    // получить именно её: раньше сюда попадали все строки того же pool, и выбранный
    // PL мог незаметно превратиться в другую ноду (BY) после шаффла.
    const int serverCount = m_serversModel->getServersCount();
    Q_UNUSED(serverCount);
    const int firstRow = serverIndex;
    const int lastRow = serverIndex;
    for (int row = firstRow; row <= lastRow; ++row) {
        const QJsonObject config = row == serverIndex ? serverConfig : m_serversModel->getServerConfig(row);

        if (!targetPool.isEmpty()) {
            // stay within one protocol/env/subscription: a pool may list the same
            // node under several protocol variants, but keys/ports differ
            const QString poolName =
                    config.value(QStringLiteral("displayInfo")).toObject().value(QStringLiteral("pool")).toString();
            if (poolName != targetPool
                || m_serversModel->data(row, ServersModel::Roles::ServiceProtocolFilterRole).toString() != targetProtocol
                || m_serversModel->data(row, ServersModel::Roles::ConnectionEnvRole).toString() != targetEnv
                || subscriptionIdOf(config) != targetSubscription || isVlessCdnRow(row) != targetIsVlessCdn) {
                continue;
            }
            // gateway rows may not have their container config installed yet —
            // there is nothing to connect with, so such a sibling is not a fallback
            if (!m_serversModel->serverHasUsableConfig(row)) {
                qDebug() << "[IPPOOL] skipping row" << row << "(no usable config yet)";
                continue;
            }
        }

        // CDN-fronted VLESS (xhttp over plain TLS) dials the CDN hostname, not a
        // node IP — node_ips must not be patched into its config
        if (targetIsVlessCdn) {
            pool.append({row, QString(), 0, 100});
            continue;
        }

        const QJsonArray nodeIps = config.value(QStringLiteral("node_ips")).toArray();
        bool rowHasIps = false;
        for (const QJsonValue &v : nodeIps) {
            const QString ip = v.toString();
            if (ip.isEmpty()) {
                continue;
            }
            rowHasIps = true;
            if (seenIps.contains(ip)) {
                continue; // already covered by an earlier row of the pool
            }
            seenIps.insert(ip);
            // порт этого адреса: node_ports[ip], иначе порт протокола строки
            pool.append({row, ip,
                         ServersModel::portForNodeIp(config, ip, m_serversModel->serverPort(row)),
                         ServersModel::weightForNodeIp(config, ip)});
        }
        // no entry addresses at all — this row is still reachable by its hostName
        if (!rowHasIps) {
            pool.append({row, QString()});
        }
    }

    // Пользователь может выключить отдельные адреса пула (тумблеры в карточке).
    // Фильтр применяем ВСЕГДА, включая одиночный адрес: раньше он работал только
    // при pool.size() > 1, и строка с одним адресом (или с hostName вместо
    // node_ips) подключалась к выключенному адресу.
    {
        const QStringList disabledEntries = m_settings->poolDisabledEndpoints();
        if (!disabledEntries.isEmpty() && !pool.isEmpty()) {
            QList<PoolEntry> filtered;
            for (const PoolEntry &entry : pool) {
                // у одиночной строки адрес живёт в hostName, а не в node_ips
                const QString address = entry.ip.isEmpty()
                        ? m_serversModel->getServerCredentials(entry.row).hostName
                        : entry.ip;
                if (!address.isEmpty() && disabledEntries.contains(address)) {
                    continue;
                }
                filtered.append(entry);
            }
            if (filtered.isEmpty()) {
                // выключены все — подключаться некуда, и делать вид нельзя
                qWarning() << "[IPPOOL] all addresses are switched off for row" << serverIndex;
                resetIpPool();
                m_isConnectionInProgress = false;
                m_connectionStateText = tr("All addresses are turned off");
                emit connectionErrorOccurred(ErrorCode::ServerConnectionTimeoutError);
                emit connectionStateChanged();
                return;
            }
            if (filtered.size() != pool.size()) {
                qDebug() << "[IPPOOL] user disabled" << (pool.size() - filtered.size())
                         << "address(es), using" << filtered.size();
                pool = filtered;
            }
        }
    }

    // Адреса, у которых handshake не ответил (свежая проба), не пробуем:
    // один IP ноды может быть заблокирован, пока другой работает. Если после
    // фильтра не осталось ничего — идём как раньше, по всему пулу.
    if (pool.size() > 1) {
        QList<PoolEntry> alive;
        QList<PoolEntry> droppedByProbe;
        for (const PoolEntry &entry : pool) {
            if (entry.ip.isEmpty()) {
                alive.append(entry);
                continue;
            }
            const int port = entry.port > 0 ? entry.port : m_serversModel->serverPort(entry.row);
            if (m_serversModel->isEndpointUsable(entry.ip, port)) {
                alive.append(entry);
            } else {
                droppedByProbe.append(entry);
            }
        }
        if (!alive.isEmpty() && alive.size() != pool.size()) {
            qDebug() << "[IPPOOL] probe says" << droppedByProbe.size()
                     << "address(es) unreachable, moved to fallback";
            pool = alive;
            m_ipFallbackPool = droppedByProbe; // второй круг, если живых не хватит
        }
    }

    // ВЫБОР АДРЕСА: вес из node_weights + пинг как вето (а не множитель).
    // 1) адрес с проваленной пробой уже отброшен выше;
    // 2) среди оставшихся вычисляем min RTT и выбрасываем адреса, которые хуже
    //    min+150 мс И при этом хуже 200 мс — «живой, но заведомо мёртвый по
    //    задержке» не должен тянуть подключение;
    // 3) дальше взвешенный случайный выбор: P(ip) = w / Σw;
    // 4) адреса без замеров участвуют по весу (данных нет — просто пробуем).
    if (pool.size() > 1) {
        struct Candidate
        {
            PoolEntry entry;
            int rtt = -1;      // -1 = данных нет
            int weight = 100;  // вес из node_weights
            double load = -1;  // загрузка ноды 0..1 (-1 = неизвестна)
            int weightEff = 100; // вес с учётом загрузки: w * (1 - load), не ниже 15% w
            bool overloaded = false; // load >= 0.9 — пускаем последними
        };
        QList<Candidate> candidates;
        int minRtt = INT_MAX;
        for (const PoolEntry &entry : pool) {
            Candidate candidate;
            candidate.entry = entry;
            candidate.weight = entry.ip.isEmpty() ? 100 : entry.weight;
            candidate.weightEff = candidate.weight;
            if (m_apiConfigsController && !entry.ip.isEmpty()) {
                const QJsonObject rowApiConfig = m_serversModel->getServerConfig(entry.row)
                                                         .value(QStringLiteral("api_config")).toObject();
                const QString nodeId = rowApiConfig.value(QStringLiteral("node_id")).toString();
                const double load = m_apiConfigsController->nodeLoadFor(nodeId);
                if (load >= 0.0) {
                    candidate.load = load;
                    // Полностью загруженная нода не исчезает, но падает до 15 % веса.
                    const double scaled = candidate.weight * (1.0 - load);
                    const double floor = qMax(1.0, candidate.weight * 0.15);
                    candidate.weightEff = qBound(1, static_cast<int>(qMax(scaled, floor)), candidate.weight);
                    candidate.overloaded = load >= 0.9;
                }
            }
            if (!entry.ip.isEmpty()) {
                const int port = entry.port > 0 ? entry.port : m_serversModel->serverPort(entry.row);
                const QVariantMap health = m_serversModel->endpointHealth(entry.ip, port);
                candidate.rtt = health.value(QStringLiteral("rtt")).toInt();
                if (candidate.rtt >= 0) {
                    minRtt = qMin(minRtt, candidate.rtt);
                }
            }
            candidates.append(candidate);
        }

        QList<Candidate> usable;
        for (const Candidate &candidate : candidates) {
            if (candidate.rtt >= 0 && minRtt != INT_MAX
                && candidate.rtt > minRtt + kPingVetoDeltaMs
                && candidate.rtt > kPingVetoAbsoluteMs) {
                qDebug() << "[IPPOOL] ping veto:" << candidate.entry.ip << candidate.rtt
                         << "ms vs best" << minRtt << "ms (weight" << candidate.weight << ")";
                continue;
            }
            usable.append(candidate);
        }
        if (usable.isEmpty()) {
            usable = candidates;
        }

        // ЛУЧШИЙ АДРЕС — ПЕРВЫМ. Раньше здесь был взвешенный случайный выбор,
        // и при смене карточки мы могли с первой попытки уйти в заведомо
        // медленный/мёртвый адрес и ждать таймаут. Вес остаётся вторым
        // критерием (для адресов без замеров) — распределение по весам
        // обеспечивает пул, а не порядок перебора для конкретного юзера.
        std::stable_sort(usable.begin(), usable.end(), [](const Candidate &a, const Candidate &b) {
            if (a.overloaded != b.overloaded) {
                return !a.overloaded; // загруженные (load >= 0.9) — последними
            }
            const bool aMeasured = a.rtt >= 0;
            const bool bMeasured = b.rtt >= 0;
            if (aMeasured != bMeasured) {
                return aMeasured; // с замером — раньше, чем без него
            }
            if (aMeasured && bMeasured && a.rtt != b.rtt) {
                return a.rtt < b.rtt; // лучший пинг — первым
            }
            return a.weightEff > b.weightEff; // равный пинг/нет данных — по весу с учётом загрузки
        });
        const int chosen = 0;

        qDebug().noquote() << "[IPPOOL] pick" << (usable.at(chosen).entry.ip.isEmpty()
                                                  ? QStringLiteral("hostName")
                                                  : usable.at(chosen).entry.ip)
                           << "weight" << usable.at(chosen).weight
                           << "weightEff" << usable.at(chosen).weightEff
                           << "load" << usable.at(chosen).load
                           << "rtt" << usable.at(chosen).rtt
                           << QStringLiteral("(candidates %1, best-first)").arg(candidates.size());

        QList<PoolEntry> ordered;
        ordered.append(usable.at(chosen).entry);
        QList<Candidate> rest = usable;
        rest.removeAt(chosen);
        std::stable_sort(rest.begin(), rest.end(), [](const Candidate &a, const Candidate &b) {
            if (a.overloaded != b.overloaded) {
                return !a.overloaded;
            }
            const bool aM = a.rtt >= 0;
            const bool bM = b.rtt >= 0;
            if (aM != bM) {
                return aM;
            }
            if (aM && bM && a.rtt != b.rtt) {
                return a.rtt < b.rtt;
            }
            return a.weightEff > b.weightEff;
        });
        for (const Candidate &candidate : rest) {
            ordered.append(candidate.entry);
        }
        // адреса, срезанные вето, всё равно пробуем последними
        for (const Candidate &candidate : candidates) {
            bool alreadyAdded = false;
            for (const PoolEntry &entry : ordered) {
                if (entry.row == candidate.entry.row && entry.ip == candidate.entry.ip) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                ordered.append(candidate.entry);
            }
        }
        pool = ordered;
    }

    // Один живой адрес + отброшенные пробой — это тоже пул: иначе терялся
    // второй круг (resetIpPool() стирал fallback) и живой адрес не пробовался.
    if (!pool.isEmpty() && (pool.size() > 1 || !m_ipFallbackPool.isEmpty())) {
        m_ipPool = pool;
        m_ipPoolPos = 0;
        m_ipFallbackPos = -1;
        qDebug() << "[IPPOOL] created pool with" << m_ipPool.size() << "entries for row" << serverIndex
                 << "fallback" << m_ipFallbackPool.size();
        connectToServerIndexWithIp(m_ipPool[0].row, m_ipPool[0].ip, m_ipPool[0].port);
        return;
    }

    resetIpPool();
    connectToServerIndexWithIp(serverIndex, QString());
}

void ConnectionController::connectToServerIndexWithIp(int serverIndex, const QString &ip, int port)
{
    m_userDisconnectRequested = false;

    QJsonObject serverConfig = m_serversModel->getServerConfig(serverIndex);

    DockerContainer container = qvariant_cast<DockerContainer>(m_serversModel->data(serverIndex, ServersModel::Roles::DefaultContainerRole));

    if (!m_containersModel->isSupportedByCurrentPlatform(container)) {
        emit connectionErrorOccurred(ErrorCode::NotSupportedOnThisPlatform);
        return;
    }

    // teardown states of a previous tunnel are neither a failure nor a user cancel
    m_connectionSwitching = true;

    // Прямой коннект по hostName (старт приложения, «подключиться к этому»)
    // шёл мимо пула — и выключенный в карточке адрес всё равно набирался.
    // Здесь единая точка: если адрес выключен юзером, берём первый разрешённый.
    QString dialAddress = ip;
    if (m_settings) {
        const QStringList disabled = m_settings->poolDisabledEndpoints();
        if (!disabled.isEmpty()) {
            const QStringList nodeIps = [&]() {
                QStringList list;
                for (const QJsonValue &v : serverConfig.value(QStringLiteral("node_ips")).toArray()) {
                    const QString value = v.toString();
                    if (!value.isEmpty() && !list.contains(value)) {
                        list.append(value);
                    }
                }
                return list;
            }();
            const QString hostName = serverConfig.value(QStringLiteral("hostName")).toString();
            const QString candidate = dialAddress.isEmpty() ? hostName : dialAddress;
            if (!candidate.isEmpty() && disabled.contains(candidate)) {
                QString replacement;
                for (const QString &nodeIp : nodeIps) {
                    if (!disabled.contains(nodeIp)) {
                        replacement = nodeIp;
                        break;
                    }
                }
                if (replacement.isEmpty()) {
                    qWarning() << "[IPPOOL] refusing to dial disabled address" << candidate;
                    m_isConnectionInProgress = false;
                    m_connectionStateText = tr("All addresses are turned off");
                    emit connectionErrorOccurred(ErrorCode::ServerConnectionTimeoutError);
                    emit connectionStateChanged();
                    return;
                }
                qDebug() << "[IPPOOL] dial address" << candidate << "is switched off, using" << replacement;
                dialAddress = replacement;
            }
        }
    }

    if (!dialAddress.isEmpty()) {
        // порт адреса (node_ports) правим вместе с host: иначе альтернативный
        // IP стучится на порт протокола, который провайдер может резать
        patchServerConfigAddress(serverConfig, dialAddress, port);
    }

    VpnConfigurationsController vpnConfigurationController(m_settings);

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
    if (!dialAddress.isEmpty()) {
        credentials.hostName = dialAddress;
    }
    const bool endpointChanged = m_currentEndpoint != credentials.hostName;
    const int previousPort = m_currentPort;
    // Store and publish the endpoint's own port before notifying QML. Otherwise
    // the card briefly shows the previous endpoint's port and persists it.
    m_currentPort = port > 0 ? port : m_serversModel->serverPort(serverIndex);
    if (endpointChanged) {
        m_currentEndpoint = credentials.hostName;
    }
    if (endpointChanged || previousPort != m_currentPort) {
        if (m_settings) {
            m_settings->setLastConnectedEndpoint(m_currentEndpoint);
            m_settings->setLastConnectedPort(m_currentPort);
        }
        emit connectionStateChanged();
    }
    m_attemptElapsed.start(); // отсчёт попытки для истории в отчёте
    m_postCheckPending = false; // новая попытка — прошлые счётчики не в счёт
    m_tunnelConfirmed = false;
    m_noDataReported = false;
    m_rxSinceConnect = 0;
    m_txSinceConnect = 0;

    auto dns = m_serversModel->getDnsPair(serverIndex);

    auto vpnConfiguration = vpnConfigurationController.createVpnConfiguration(dns, serverConfig, containerConfig, container);
    if (m_autoPhase == AutoPhase::None) {
        // manual connect: fail loudly instead of spinning forever on a dead server
        // Быстрый путь не должен держать юзера 20 секунд: не ответил за 8 — уходим
        // в полный алгоритм.
        m_manualConnectTimer->start((m_prepareBypass || m_fastPathDialActive)
                                            ? kFastPathAttemptTimeoutMs
                                            : kManualConnectTimeoutMs);
    }
    dbgMark(QStringLiteral("dial %1:%2 row=%3").arg(credentials.hostName).arg(m_currentPort).arg(serverIndex));
    emit connectToVpn(serverIndex, credentials, container, vpnConfiguration);
}

int ConnectionController::currentPoolRow() const
{
    return (m_ipPoolPos >= 0 && m_ipPoolPos < m_ipPool.size()) ? m_ipPool.at(m_ipPoolPos).row : -1;
}

void ConnectionController::resetIpPool()
{
    m_ipPool.clear();
    m_ipPoolPos = -1;
    m_ipFallbackPool.clear();
    m_ipFallbackPos = -1;
    m_ipAwaitingTraffic = false;
    if (m_ipTrafficTimer) {
        m_ipTrafficTimer->stop();
    }
}

// --- выбор адресов пула ------------------------------------------------------

QString ConnectionController::poolKeyForRow(int row) const
{
    if (!m_serversModel || row < 0 || row >= m_serversModel->getServersCount()) {
        return {};
    }
    const QJsonObject config = m_serversModel->getServerConfig(row);
    QJsonObject authData = config.value(QStringLiteral("api_config")).toObject()
                                   .value(QStringLiteral("auth_data")).toObject();
    if (authData.isEmpty()) {
        authData = config.value(QStringLiteral("auth_data")).toObject();
    }
    const QString apiKey = authData.value(QStringLiteral("api_key")).toString();
    QString subscription = apiKey.isEmpty() ? authData.value(QStringLiteral("id")).toString() : apiKey;
    if (subscription.isEmpty()) {
        // сервер без api_config (импорт вручную): ключ стабилен на устройство
        subscription = m_settings->getInstallationUuid(false);
    }
    return subscription;
}

QStringList ConnectionController::poolAddresses()
{
    if (!m_serversModel) {
        return {};
    }
    const int row = m_serversModel->getProcessedServerIndex();
    if (row < 0 || row >= m_serversModel->getServersCount()) {
        return {};
    }
    QStringList ips;
    for (const QJsonValue &value : m_serversModel->getServerConfig(row).value(QStringLiteral("node_ips")).toArray()) {
        const QString ip = value.toString();
        if (!ip.isEmpty() && !ips.contains(ip)) {
            ips.append(ip);
        }
    }
    return ips;
}

QStringList ConnectionController::poolDisabledAddressesFor(const int serverIndex)
{
    Q_UNUSED(serverIndex);
    // список не зависит от строки сервера: IP либо выключен юзером, либо нет
    const QStringList ips = m_settings->poolDisabledEndpoints();
    qDebug() << "[POOL] disabled endpoints:" << ips;
    return ips;
}

// Диагностика тумблеров: пишем в файл рядом с настройками приложения и
// дублируем в сам plist (Conf/diagTrace) — plist я читаю с устройства
// через devicectl, поэтому вижу, что реально произошло в UI.
static void appendEndpointLog(const QString &line)
{
    const QByteArray payload = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()
                               + " " + line.toUtf8() + "\n";

    const QStringList dirs = {
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
    };
    for (const QString &dir : dirs) {
        if (dir.isEmpty()) {
            continue;
        }
        QDir().mkpath(dir);
        QFile file(dir + QStringLiteral("/endpoints.log"));
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            file.write(payload);
        }
    }

    // кольцевой буфер в настройках: последние 40 строк, чтобы plist не пух
    QSettings settings;
    QStringList trace = settings.value("Conf/diagTrace").toStringList();
    trace.append(QDateTime::currentDateTime().toString("HH:mm:ss") + " " + line);
    while (trace.size() > 40) {
        trace.removeFirst();
    }
    settings.setValue("Conf/diagTrace", trace);
    settings.sync();
}

void ConnectionController::logEndpointEvent(const QString &message)
{
    const QString line = QStringLiteral("[UI] ") + message;
    appendEndpointLog(line);
    if (m_settings) {
        m_settings->appendDiagTrace(line);
    }
}

QStringList ConnectionController::poolDisabledAddresses()
{
    if (!m_serversModel) {
        return {};
    }
    return poolDisabledAddressesFor(m_serversModel->getProcessedServerIndex());
}

bool ConnectionController::isPoolAddressEnabled(const QString &ip)
{
    return !poolDisabledAddresses().contains(ip);
}

void ConnectionController::setPoolAddressEnabledFor(const int serverIndex, const QString &ip, bool enabled)
{
    if (ip.isEmpty()) {
        return;
    }
    Q_UNUSED(serverIndex);

    QStringList list = m_settings->poolDisabledEndpoints();
    if (enabled) {
        list.removeAll(ip);
    } else if (!list.contains(ip)) {
        list.append(ip);
    }
    m_settings->setPoolDisabledEndpoints(list);
    appendEndpointLog(QStringLiteral("[SET] ") + ip + (enabled ? " enabled" : " disabled")
                      + " now=[" + list.join(",") + "]");
    qDebug() << "[POOL] endpoint" << ip << (enabled ? "enabled" : "disabled") << "now:" << list;
}

void ConnectionController::setPoolAddressEnabled(const QString &ip, bool enabled)
{
    if (!m_serversModel) {
        return;
    }
    setPoolAddressEnabledFor(m_serversModel->getProcessedServerIndex(), ip, enabled);
}


// --- ожидание сети / бесконечный реконнект -----------------------------------

int ConnectionController::retryDelayMs() const
{
    switch (m_retryAttempt) {
    case 1: return 3000;
    case 2: return 5000;
    case 3: return 10000;
    case 4: return 20000;
    default: return 30000;
    }
}

void ConnectionController::beginWaitingForNetwork()
{
    m_waitingForNetwork = true;
    // кнопка остаётся в режиме «Отмена»: одно нажатие должно прекращать попытки
    m_isConnectionInProgress = true;
    ++m_retryAttempt;
    m_retryServerIndex = m_serversModel ? m_serversModel->getDefaultServerIndex() : -1;

    // Туннель опускаем: TUN без пира ничего не защищает, но мешает следующим попыткам.
    emit disconnectFromVpn();

    const bool networkUp = QNetworkInformation::instance()
            && QNetworkInformation::instance()->reachability() == QNetworkInformation::Reachability::Online;
    m_connectionStateText = networkUp ? tr("Server is not responding, retrying…")
                                      : tr("Waiting for network…");
    emit connectionStateChanged();

    if (m_retryTimer) {
        m_retryTimer->start(retryDelayMs());
    }
    qDebug() << "[RETRY] attempt" << m_retryAttempt << "in" << retryDelayMs()
             << "ms, network up:" << networkUp;
}

void ConnectionController::retryConnectionNow()
{
    if (!m_waitingForNetwork) {
        return;
    }
    if (m_retryTimer) {
        m_retryTimer->stop();
    }

    // Сети всё ещё нет — не долбим сокетами, ждём события reachability.
    if (QNetworkInformation::instance()
        && QNetworkInformation::instance()->reachability() == QNetworkInformation::Reachability::Disconnected) {
        if (m_retryTimer) {
            m_retryTimer->start(30000);
        }
        qDebug() << "[RETRY] still offline, next attempt in 30s";
        return;
    }

    m_connectionStateText = tr("Connecting…");
    emit connectionStateChanged();

    const int index = m_retryServerIndex >= 0 ? m_retryServerIndex
                                              : (m_serversModel ? m_serversModel->getDefaultServerIndex() : -1);
    if (index < 0) {
        resetRetryState();
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        emit connectionStateChanged();
        return;
    }
    connectToServerIndex(index);
}

void ConnectionController::recordAttempt(const QString &outcome, const QString &detail)
{
    if (!m_settings || m_currentEndpoint.isEmpty()) {
        return;
    }
    QJsonObject entry;
    entry.insert(QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    entry.insert(QStringLiteral("address"), m_currentEndpoint);
    // Порт берём тот, на который реально шли: у одноадресной строки пула нет,
    // и через currentPoolRow() в отчёт уходил 0 («не знаем») вместо 443/51820.
    int port = m_currentPort;
    if (port <= 0) {
        const int row = currentPoolRow();
        if (m_serversModel && row >= 0) {
            port = m_serversModel->serverPort(row);
        }
    }
    entry.insert(QStringLiteral("port"), port);
    if (m_serversModel) {
        const int row = currentPoolRow();
        if (row >= 0) {
            entry.insert(QStringLiteral("protocol"),
                         m_serversModel->data(row, ServersModel::ServiceProtocolRole).toString());
        }
    }
    entry.insert(QStringLiteral("outcome"), outcome);
    if (m_attemptElapsed.isValid()) {
        entry.insert(QStringLiteral("waited_ms"), static_cast<double>(m_attemptElapsed.elapsed()));
    }
    if (!detail.isEmpty()) {
        entry.insert(QStringLiteral("detail"), detail);
    }
    m_settings->addConnectAttempt(entry);
}

bool ConnectionController::retrySameAddressAfterFastFail()
{
    // Старт упал за доли секунды — до любого handshake. На iOS это почти всегда
    // гонка: новая сессия стартует, пока прошлая ещё останавливается, и NE
    // отдаёт общую ошибку (ErrorCode::InternalError, «101» в отчёте).
    // Перебор адресов тут вреден: живой адрес помечается мёртвым.
    if (!m_attemptElapsed.isValid() || m_attemptElapsed.elapsed() > 1500) {
        return false;
    }
    if (m_currentEndpoint.isEmpty() || m_fastFailAddress == m_currentEndpoint) {
        return false; // уже повторяли этот адрес — второй раз не ждём
    }
    m_fastFailAddress = m_currentEndpoint;

    const int poolRow = currentPoolRow();
    const int row = poolRow >= 0 ? poolRow : (m_serversModel ? m_serversModel->getProcessedServerIndex() : -1);
    if (row < 0) {
        return false;
    }
    const QString address = m_currentEndpoint;
    const int port = m_currentPort;
    // Повтор того же адреса оправдан, только если он единственный: иначе
    // быстрее перейти к следующему (лучшему из оставшихся), чем ждать ещё раз.
    if (m_ipPool.size() > 1) {
        return false;
    }
    qDebug() << "[IPPOOL] fast fail on" << address << "— retry same address once";
    m_connectionStateText = tr("Connecting...");
    emit connectionStateChanged();
    QTimer::singleShot(500, this, [this, row, address, port]() { connectToServerIndexWithIp(row, address, port); });
    return true;
}


// «Connected» не значит «данные идут»: handshake проходит за 148 байт, а пакеты
// данных провайдер может резать (MTU-дыра, DPI, односторонняя блокировка).
// Ловим две вещи: активно — DNS-запрос через туннель, пассивно — tx растёт, а rx нет.
void ConnectionController::startPostConnectCheck()
{
    if (!m_settings || !m_settings->isPostConnectCheckEnabled()) {
        return;
    }
    m_rxSinceConnect = 0;
    m_txSinceConnect = 0;
    m_noDataReported = false;
    m_postCheckPending = true;

    if (!m_postCheckTimer) {
        m_postCheckTimer = new QTimer(this);
        m_postCheckTimer->setSingleShot(true);
        connect(m_postCheckTimer, &QTimer::timeout, this, [this]() {
            if (!m_postCheckPending || !m_isConnected) {
                return;
            }
            // за окно отправили заметный объём, а не получили ничего
            if (m_txSinceConnect > 20000 && m_rxSinceConnect == 0) {
                handleNoData("one-way");
                return;
            }
            m_postCheckPending = false;
        });
    }
    m_postCheckTimer->start(30000);

    // Активная проверка: резолв имени через туннель. Если resolver молчит —
    // трафик внутрь уходит, а ответы не возвращаются.
    QHostInfo::lookupHost(QStringLiteral("api.frkn.org"), this, [this](const QHostInfo &info) {
        if (!m_isConnected || !m_postCheckPending) {
            return;
        }
        if (info.error() == QHostInfo::NoError) {
            confirmTunnel();
        } else {
            handleNoData(QStringLiteral("dns"));
        }
    });
}

// Первые реальные байты (или успешный DNS через туннель) — только теперь
// соединение считается рабочим.
void ConnectionController::confirmTunnel()
{
    if (m_tunnelConfirmed) {
        return;
    }
    m_tunnelConfirmed = true;
    m_postCheckPending = false;
    if (m_postCheckTimer) {
        m_postCheckTimer->stop();
    }
    qDebug() << "[TUNNEL] confirmed with data on" << m_currentEndpoint << m_currentPort;
    m_isConnected = true;
    m_isConnectionInProgress = false;
    m_connectionStateText = tr("Connected");
    emit connectionStateChanged();
}

void ConnectionController::handleNoData(const QString &reason)
{
    if (m_noDataReported) {
        return;
    }
    m_noDataReported = true;
    m_postCheckPending = false;
    if (m_postCheckTimer) {
        m_postCheckTimer->stop();
    }

    // ТОЛЬКО фиксируем факт: переподключать и рвать туннель из-за этого нельзя —
    // на iOS счётчики NE отдают нули и на рабочем канале, и юзер получал
    // бесконечный цикл «connecting → reconnecting».
    qWarning() << "[NODATA] suspected one-way traffic on" << m_currentEndpoint << m_currentPort
               << "reason:" << reason << "rx:" << m_rxSinceConnect << "tx:" << m_txSinceConnect;

    if (m_settings) {
        QJsonObject info;
        info.insert(QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        info.insert(QStringLiteral("address"), m_currentEndpoint);
        info.insert(QStringLiteral("port"), m_currentPort);
        info.insert(QStringLiteral("reason"), reason);
        info.insert(QStringLiteral("rx_bytes"), static_cast<double>(m_rxSinceConnect));
        info.insert(QStringLiteral("tx_bytes"), static_cast<double>(m_txSinceConnect));
        m_settings->setLastNoDataInfo(QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact)));
    }
}

// Единая точка сбоя коннекта. Пока «переподключаться до отмены» включено,
// любая неудача (туннель упал, все адреса пула кончились, время вышло) не
// показывает ошибку, а уводит в цикл ожидания сети — кнопка остаётся «Отмена».
// Быстрый путь не ожил: показываем юзеру, что теперь считаем варианты, и
// запускаем обычный алгоритм (пробы + веса + перебор адресов).
bool ConnectionController::tryAlgorithmAfterFastPath()
{
    if (!m_fastPathTried || m_fastPathConsumed || m_fastPathEndpoint.isEmpty()) {
        return false;
    }
    m_fastPathConsumed = true;
    resetIpPool();
    m_connectionStateText = tr("Calculating the best server…");
    emit connectionStateChanged();
    qWarning().noquote() << "[FASTPATH] endpoint did not answer, running pool algorithm";
    const int row = m_serversModel ? m_serversModel->getProcessedServerIndex() : -1;
    QTimer::singleShot(0, this, [this, row]() {
        connectToServerIndex(row >= 0 ? row : m_serversModel->getDefaultServerIndex());
    });
    return true;
}

void ConnectionController::failOrRetry(ErrorCode code)
{
    // Если не ожил адрес, который мы считали рабочим, — забываем его: следующая
    // попытка пойдёт полным алгоритмом, а не снова в тот же мёртвый порт.
    if (m_fastPathConsumed && m_settings && m_serversModel) {
        m_settings->clearLastGoodEndpointFor(
                rowMemoryKey(m_serversModel->getProcessedServerIndex()));
        m_fastPathConsumed = false;
    }

    if (m_settings && m_settings->isReconnectUntilCancelled() && !m_isConnected) {
        qDebug() << "[RETRY] connect failed, entering retry loop; code" << static_cast<int>(code);
        beginWaitingForNetwork();
        return;
    }
    emit connectionErrorOccurred(code);
}

void ConnectionController::resetRetryState()
{
    m_waitingForNetwork = false;
    m_retryAttempt = 0;
    if (m_retryTimer) {
        m_retryTimer->stop();
    }
}

bool ConnectionController::retryWithNextIp()
{
    if (m_ipPoolPos >= 0 && m_ipPoolPos + 1 < m_ipPool.size()) {
        m_ipPoolPos++;
    } else if (m_ipFallbackPos + 1 < m_ipFallbackPool.size()) {
        // Первый круг исчерпан — добираем адреса, отброшенные пробой:
        // проба могла соврать (порт, старый кэш), а живой адрес важнее.
        m_ipFallbackPos++;
        qDebug() << "[IPPOOL] fallback round, entry" << (m_ipFallbackPos + 1) << "/" << m_ipFallbackPool.size();
        const PoolEntry base = m_ipFallbackPool.at(m_ipFallbackPos);
        m_ipAwaitingTraffic = false;
        m_ipTrafficTimer->stop();
        connectToServerIndexWithIp(base.row, base.ip, base.port);
        if (m_autoPhase == AutoPhase::Connecting) {
            m_autoAdvancing = true;
            m_autoAttemptTimer->start(kAutoAttemptTimeoutMs);
        }
        m_connectionStateText = tr("Connecting...");
        emit connectionStateChanged();
        return true;
    } else {
        return false;
    }

    const PoolEntry &entry = m_ipPool.at(m_ipPoolPos);
    qDebug() << "[IPPOOL] entry" << (m_ipPoolPos + 1) << "/" << m_ipPool.size()
             << "row" << entry.row << "ip" << entry.ip;
    m_ipAwaitingTraffic = false;
    m_ipTrafficTimer->stop();
    connectToServerIndexWithIp(entry.row, entry.ip, entry.port);
    if (m_autoPhase == AutoPhase::Connecting) {
        // Connected cleared m_autoAdvancing — re-arm it, otherwise the old
        // tunnel's teardown Disconnected is misread as this attempt failing
        // and the candidate gets skipped mid-retry
        m_autoAdvancing = true;
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
    if (protocol == QStringLiteral("awg")) {
        return 1;
    }
    if (protocol == QStringLiteral("hysteria2")) {
        return 2;
    }
    if (protocol == QStringLiteral("wireguard")) {
        return 3;
    }
    if (protocol == QStringLiteral("vless")) {
        // VLESS goes (almost) last: a CDN/REALITY TCP probe answers fast even on
        // a node that proxies nothing, which used to outrank healthy AWG rows
        return isVlessCdnRow(row) ? 4 : 5;
    }
    return 6;
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

    // strict tier order (list is already sorted): a probe-confirmed candidate
    // may win only within the BEST tier present — a fast VLESS TCP probe must
    // not outrank unprobed (still probing) AWG rows; protocol preference beats
    // probe speed
    const int bestTier = candidates.first().tier;
    for (int i = 0; i < candidates.size(); ++i) {
        if (candidates.at(i).tier != bestTier) {
            break; // sorted by tier — nothing else in the best tier
        }
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
    m_connectionStateText = tr("Calculating the best server…");
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
    m_autoAdvancing = false;
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
        failOrRetry(m_vpnConnection->lastError());
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
    m_userDisconnectRequested = true;

    m_manualConnectTimer->stop();

    if (m_waitingForNetwork) {
        // пользователь нажал «Отмена» в режиме ожидания сети
        resetRetryState();
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        emit disconnectFromVpn();
        emit connectionStateChanged();
        return;
    }

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
    // «Preparing» — это валидация конфига через api. На плохой сети она может
    // висеть бесконечно: юзер видит вечное «Preparing» и ни одной попытки
    // подключения. Через kPrepareWatchdogMs бросаем валидацию и идём по
    // сохранённому конфигу, перебирая адреса пула по очереди.
    if (!m_prepareWatchdog) {
        m_prepareWatchdog = new QTimer(this);
        m_prepareWatchdog->setSingleShot(true);
        connect(m_prepareWatchdog, &QTimer::timeout, this, [this]() {
            if (m_isConnected || m_ipPoolPos >= 0) {
                return;
            }
            if (m_state != Vpn::ConnectionState::Preparing
                && m_connectionStateText != tr("Preparing...")) {
                return;
            }
            qDebug() << "[PREPARE] stuck in Preparing for" << kPrepareWatchdogMs
                     << "ms — bypassing config validation, dialing cached addresses";
            m_prepareBypass = true;
            openConnection();
        });
    }
    if (state == Vpn::ConnectionState::Preparing) {
        m_prepareWatchdog->start(kPrepareWatchdogMs);
    } else {
        m_prepareWatchdog->stop();
        if (state == Vpn::ConnectionState::Connected
            || state == Vpn::ConnectionState::Disconnected) {
            m_prepareBypass = false;
        }
    }

    if (state == Vpn::ConnectionState::Connected
        || state == Vpn::ConnectionState::Disconnected) {
        if (m_connectWatchdog) {
            m_connectWatchdog->stop();
        }
        m_connectWatchdogFired = false;
    }
    if (state == Vpn::ConnectionState::Connected) {
        m_livePingFails = 0;
        m_tunnelFlaggedDead = false;
        resetRetryState();
        // attempt resolved — a teardown after this point is a real failure
        m_autoAdvancing = false;
        m_connectionSwitching = false;
        m_fastFailAddress.clear(); // адрес доказал себя — следующий фейл снова даёт одну попытку
        dbgMark(QStringLiteral("CONNECTED %1:%2").arg(m_currentEndpoint).arg(m_currentPort));
        m_fastPathDialActive = false;
        if (m_serversModel) {
            const int row = currentPoolRow() >= 0 ? currentPoolRow()
                                                  : m_serversModel->getProcessedServerIndex();
            m_lastConnectedRowKey = rowMemoryKey(row);
        }
        if (m_settings) {
            const int connectedRow = currentPoolRow() >= 0 ? currentPoolRow()
                                                           : m_serversModel->getProcessedServerIndex();
            m_settings->setLastConnectedServerIndex(connectedRow);
            // Память именно этой строки: следующий Connect к ней пойдёт сразу
            // на рабочий адрес, без проб и расчётов.
            m_settings->setLastGoodEndpointFor(rowMemoryKey(connectedRow), m_currentEndpoint, m_currentPort);
        }
        QTimer::singleShot(2000, this, [this]() { if (m_isConnected) startPostConnectCheck(); });
        recordAttempt(QStringLiteral("ok"));

        // Событие успешного подключения на бэкенд: подписка + нода + протокол
        // (в auth_data/apiConfig) плюс реальный endpoint:port и support_tag.
        // Троттлинг: один и тот же адрес — не чаще раза в минуту.
        // Телеметрия выключена — событие не шлём вовсе (тот же тумблер, что у отчётов).
        if (m_apiConfigsController && m_serversModel && m_settings && m_settings->isTelemetryEnabled()) {
            const int row = currentPoolRow() >= 0 ? currentPoolRow()
                                                  : m_serversModel->getProcessedServerIndex();
            if (row >= 0 && m_serversModel->data(row, ServersModel::Roles::IsServerFromGatewayApiRole).toBool()) {
                const QString eventKey = m_currentEndpoint + QStringLiteral(":") + QString::number(m_currentPort);
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (eventKey != m_lastConnectEventKey || nowMs - m_lastConnectEventMs > 60000) {
                    m_lastConnectEventKey = eventKey;
                    m_lastConnectEventMs = nowMs;
                    m_apiConfigsController->notifyConnected(row, m_currentEndpoint, m_currentPort);
                }
            }
        }
    } else if (state == Vpn::ConnectionState::Connecting || state == Vpn::ConnectionState::Preparing) {
        // do NOT clear m_autoAdvancing here: the previous tunnel's teardown
        // Disconnected is still in flight (we stop it inside connectVpn) and must
        // not be read as "this auto attempt failed" — that race made auto
        // selection skip every candidate instantly when a tunnel was already up
        m_connectionSwitching = false;
    }

    if (m_autoPhase == AutoPhase::Connecting) {
        if (state == Vpn::ConnectionState::Connected) {
            m_autoAttemptTimer->stop();
            // "Connected" alone proves nothing for xray/hysteria: a broken entry
            // can still bring a tunnel up, so require real bytes before declaring
            // success. WG/AWG on Apple NE skip this — their Connected is already
            // handshake-gated (see the Connected case below).
            if (m_autoCandidatePos < m_autoCandidates.size()) {
                m_autoAwaitingTraffic = true;
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
        } else if (state == Vpn::ConnectionState::Reconnecting) {
            // The Android service recycles the tunnel by itself on network events
            // (NetworkState callback -> "Reconnect VPN"), and the WG status job
            // waits for a handshake with no timeout — when the network blocks it,
            // the tunnel wedges in RECONNECTING forever and no more events come.
            // Treat it as a failed attempt instead of spinning the UI forever.
            qDebug() << "[AUTO] candidate wedged in reconnecting, moving on";
            m_autoAttemptTimer->stop();
            m_autoAwaitingTraffic = false;
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
            // the wedged tunnel is still up — tear it down on the way out
            m_autoPhase = AutoPhase::None;
            m_autoAdvancing = false;
            m_autoCandidates.clear();
            m_isConnectionInProgress = false;
            m_connectionStateText = tr("Connect");
            m_currentEndpoint.clear();
            emit disconnectFromVpn();
            failOrRetry(m_vpnConnection->lastError());
            emit connectionStateChanged();
            return;
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
            m_autoAdvancing = false;
            m_autoCandidates.clear();
            m_isConnectionInProgress = false;
            m_connectionStateText = tr("Connect");
            m_currentEndpoint.clear();
            failOrRetry(m_vpnConnection->lastError());
            emit connectionStateChanged();
            return;
        }
    } else if (m_ipPoolPos >= 0) {
        // manual connect to a multi-IP server
        if (state == Vpn::ConnectionState::Connected) {
#if defined(Q_OS_IOS) || defined(MACOS_NE) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN)
            // WG/AWG "Connected" here is handshake-gated (see the Connected case
            // below) — the entry address already proved itself, so waiting for
            // idle-tunnel bytes on top would false-negative the same way.
            // Q_OS_MACOS: the legacy macOS build talks to the daemon, which also
            // emits connected only after lastHandshake — without it a working
            // pool address was dropped after 8s and the pool ran out into an
            // error instead of switching.
            const DockerContainer container = qvariant_cast<DockerContainer>(
                    m_serversModel->data(m_ipPool.at(m_ipPoolPos).row, ServersModel::Roles::DefaultContainerRole));
            // ВАЖНО: «Connected» от Network Extension на iOS/Android означает лишь
            // «utun поднят» — PacketTunnelProvider отдаёт completionHandler сразу
            // после старта wireguard-go, ДО рукопожатия. Мёртвый адрес поэтому
            // висит как «подключено» с нулевым приёмом, и перебор не запускается.
            // На десктопе демон отдаёт connected уже после lastHandshake — там
            // доверять можно.
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
            const bool handshakeGated = false;
#else
            const bool handshakeGated = container == DockerContainer::Awg || container == DockerContainer::Awg2
                    || container == DockerContainer::WireGuard;
#endif
            if (handshakeGated) {
                qDebug() << "[IPPOOL] handshake-confirmed connect on row" << m_ipPool.at(m_ipPoolPos).row;
                resetIpPool(); // success — the next connect starts from a fresh random pool
                // fall through to the common Connected handling in the switch below
            } else {
#endif
            // a blocked entry address can still bring the tunnel up — require real bytes
            m_ipAwaitingTraffic = true;
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
            // окно шире: AWG шлёт keepalive раз в 25 с, на коротком окне живой
            // адрес без активного трафика считался бы мёртвым
            m_ipTrafficTimer->start(kNeTrafficTimeoutMs);
#else
            m_ipTrafficTimer->start(kIpTrafficTimeoutMs);
#endif
#if defined(Q_OS_IOS) || defined(MACOS_NE) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN)
            }
#endif
        } else if (state == Vpn::ConnectionState::Reconnecting) {
            // same wedge as in auto selection: the Android service's self-reconnect
            // can park in RECONNECTING forever — cycle the entry address or fail loudly
            m_ipAwaitingTraffic = false;
            m_ipTrafficTimer->stop();
            if (retryWithNextIp()) {
                m_state = state;
                return;
            }
            resetIpPool();
            m_manualConnectTimer->stop();
            m_isConnectionInProgress = false;
            m_connectionStateText = tr("Connect");
            emit disconnectFromVpn();
            failOrRetry(ErrorCode::ServerConnectionTimeoutError);
            emit connectionStateChanged();
            return;
        } else if (state == Vpn::ConnectionState::Error || state == Vpn::ConnectionState::Unknown
                   || (state == Vpn::ConnectionState::Disconnected && !m_connectionSwitching)) {
            recordAttempt(QStringLiteral("error"), m_vpnConnection ? QString::number(static_cast<int>(m_vpnConnection->lastError())) : QString());
            // упало за доли секунды — это гонка со стартом/остановкой NE,
            // а не мёртвый адрес: повторяем тот же, прежде чем перебирать пул
            if (retrySameAddressAfterFastFail()) {
                m_state = state;
                return;
            }
            if (retryWithNextIp()) {
                m_state = state;
                return;
            }
            resetIpPool(); // pool exhausted — fall through to the normal error/UI path
            if (tryAlgorithmAfterFastPath()) {
                m_state = state;
                return;
            }
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
#if defined(Q_OS_IOS) || defined(MACOS_NE) || defined(Q_OS_MACOS) || defined(Q_OS_ANDROID) || defined(Q_OS_LINUX) || defined(Q_OS_WIN)
        if (m_autoPhase == AutoPhase::Connecting && m_autoAwaitingTraffic
            && m_autoCandidatePos < m_autoCandidates.size()) {
            // WG/AWG "Connected" is emitted only after a verified handshake
            // (Apple NE, Android status job, and the desktop daemon all wait
            // for lastHandshake) — that alone proves the path is bidirectional.
            // Waiting for user bytes on top false-negatives an idle-but-healthy
            // tunnel (keepalive is 25s, the traffic window is 8s) and burned the
            // whole window per candidate.
            const int row = m_autoCandidates.at(m_autoCandidatePos).row;
            const DockerContainer container = qvariant_cast<DockerContainer>(
                    m_serversModel->data(row, ServersModel::Roles::DefaultContainerRole));
            if (container == DockerContainer::Awg || container == DockerContainer::Awg2
                || container == DockerContainer::WireGuard) {
                qDebug() << "[AUTO] handshake-confirmed connect on row" << row << "- accepting without waiting for traffic";
                finalizeAutoSuccess();
            }
        }
#endif
        break;
    }
    case Vpn::ConnectionState::Connecting: {
        m_isConnectionInProgress = true;
        if (m_switchWaiting) {
            // Смена карточки: iOS успевает сообщить Connecting от старого
            // туннеля — юзеру в этот момент важно видеть расчёт, а не «Connecting».
            m_connectionStateText = tr("Calculating the best server…");
            emit connectionStateChanged();
        }
        break;
    }
    case Vpn::ConnectionState::Reconnecting: {
        m_isConnectionInProgress = true;
        if (m_switchWaiting || m_awaitConnectProbe || m_ipPoolPos >= 0) {
            // Пока считаем вариант или гасим старый туннель — это не реконнект.
            m_connectionStateText = tr("Calculating the best server…");
            emit connectionStateChanged();
            break;
        }
        m_connectionStateText = tr("Reconnecting...");
        // a wedged self-reconnect emits no further events — keep a watchdog
        // running so a manual connect fails loudly instead of spinning forever
        // (auto selection handles Reconnecting in the auto branch above)
        // ВАЖНО: если туннель уже Connected, «Reconnecting» от NE — это ожидание
        // рукопожатия, а не заевшая попытка. Раньше здесь перезапускался
        // 6-секундный сторож, он срабатывал, мы рвали живой туннель и звонили
        // заново — отсюда те самые ~7 секунд при смене пула.
        if (!m_isConnected && m_autoPhase == AutoPhase::None && m_ipPoolPos < 0
            && !m_manualConnectTimer->isActive()) {
            m_manualConnectTimer->start(m_prepareBypass ? kBypassAttemptTimeoutMs : kManualConnectTimeoutMs);
        }
        break;
    }
    case Vpn::ConnectionState::Disconnected: {
        m_livePingFails = 0;
        m_tunnelFlaggedDead = false;
        m_isConnectionInProgress = false;
        m_currentEndpoint.clear();
        if (m_switchWaiting) {
            // Старый туннель погашен — теперь честно считаем варианты и звоним.
            m_switchWaiting = false;
            const int row = m_pendingSwitchRow;
            m_pendingSwitchRow = -1;
            m_userDisconnectRequested = false;
            m_isConnectionInProgress = true;
            m_connectionStateText = tr("Calculating the best server…");
            emit connectionStateChanged();
            QTimer::singleShot(0, this, [this, row]() { connectToServerIndex(row); });
            break;
        }
        if (!m_connectionSwitching) {
            m_manualConnectTimer->stop();
        }
        if (m_waitingForNetwork) {
            // это наш же teardown перед повтором: кнопка остаётся «Отмена»,
            // текст «Waiting for network…» не сбрасываем в «Connect»
            break;
        }
        m_connectionStateText = tr("Connect");
        break;
    }
    case Vpn::ConnectionState::Disconnecting: {
        m_isConnectionInProgress = true;
        // Смена карточки: старый туннель гасим мы сами, и «Disconnecting…» в этот
        // момент только путает — юзер ждёт переключения, а не отключения.
        const bool ourAttempt = m_switchWaiting || m_awaitConnectProbe || m_ipPoolPos >= 0;
        m_connectionStateText = ourAttempt ? tr("Calculating the best server…")
                                           : tr("Disconnecting...");
        emit connectionStateChanged();
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
        failOrRetry(getLastConnectionError());
        break;
    }
    case Vpn::ConnectionState::Unknown: {
        m_isConnectionInProgress = false;
        m_connectionStateText = tr("Connect");
        m_manualConnectTimer->stop();
        failOrRetry(getLastConnectionError());
        break;
    }
    }
    if (m_isConnected) {
        startLivePing();
    } else {
        stopLivePing();
    }
    emit connectionStateChanged();
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
    // Новый тап Connect = новый цикл: снова сначала пробуем последний рабочий
    // адрес, потом алгоритм.
    m_fastPathTried = false;
    m_fastPathConsumed = false;
    m_fastPathEndpoint.clear();
    m_fastPathPort = 0;

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
