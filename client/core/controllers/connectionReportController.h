#ifndef CONNECTIONREPORTCONTROLLER_H
#define CONNECTIONREPORTCONTROLLER_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <memory>

class QTcpSocket;
class QUdpSocket;
class QTimer;
class Settings;
class ServersModel;

// Собирает отчёт «у меня не работает» — либо по текущему серверу, либо срез по всем
// серверам, доступным пользователю (каждый адрес каждого сервера).
//
// Зачем: жалоба «не открывается айпи X» с одного устройства ничего не доказывает —
// блок у оператора выглядит так же, как мёртвая нода или кривой MTU
// (frkn-docs/block-telemetry-research.md). Отчёт + наша проба с полигона дают
// различие. Клиентская часть здесь намеренно простая: только факты о сети.
class ConnectionReportController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int progressDone READ progressDone NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY progressChanged)
    Q_PROPERTY(bool hasReport READ hasReport NOTIFY reportChanged)
    Q_PROPERTY(bool hasScan READ hasScan NOTIFY reportChanged)
    Q_PROPERTY(QString reportText READ reportText NOTIFY reportChanged)
    Q_PROPERTY(QString sendState READ sendState NOTIFY sendStateChanged)
    Q_PROPERTY(QString sendMessage READ sendMessage NOTIFY sendStateChanged)
    Q_PROPERTY(QString lastAutoReportAt READ lastAutoReportAt NOTIFY lastAutoReportAtChanged)

public:
    explicit ConnectionReportController(const QSharedPointer<ServersModel> &serversModel,
                                        const std::shared_ptr<Settings> &settings, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    bool scanning() const { return m_scanning; }
    int progressDone() const { return m_progressDone; }
    int progressTotal() const { return m_progressTotal; }
    bool hasReport() const { return !m_report.isEmpty(); }
    int reportIndex() const;
    int m_reportIndex = -1;
    bool hasScan() const { return !m_scanPayload.isEmpty(); }
    QString reportText() const { return m_reportText; }
    QString sendState() const { return m_sendState; }
    QString sendMessage() const { return m_sendMessage; }

    // Пробы по текущему серверу, отчёт из одного адреса (async, ~3-8 с).
    Q_INVOKABLE void collect(int serverIndex = -1);

    // Сброс отчёта: карточка другого сервера не должна показывать (и копировать)
    // отчёт, собранный для предыдущего — иначе юзер уносит в поддержку чужие данные.
    Q_INVOKABLE void resetReport();

    // Для какой строки собран текущий отчёт (-1 = строки нет)
    Q_PROPERTY(int reportIndex READ reportIndex NOTIFY reportChanged)

    // Полный скан: все серверы из подписки, все их адреса, все порты.
    Q_INVOKABLE void collectAll();

    Q_INVOKABLE void cancel();

    // Отправка последнего собранного отчёта/скана на gateway.
    Q_INVOKABLE void send();
    // положить текущий отчёт/скан в буфер — чтобы юзер мог прислать его в чат
    Q_INVOKABLE void copyReport();

    // Обратная связь из приложения: текст уходит тем же каналом, что отчётен
    // (v1/report/block), с блоком feedback. attachReport — приложить отчёт
    // (свежий из сессии, иначе краткий без проб).
    Q_INVOKABLE void sendFeedback(const QString &message, bool attachReport);
    QString feedbackState() const { return m_feedbackState; }
    QString feedbackMessage() const { return m_feedbackMessage; }
    Q_PROPERTY(QString feedbackState READ feedbackState NOTIFY feedbackStateChanged)
    Q_PROPERTY(QString feedbackMessage READ feedbackMessage NOTIFY feedbackStateChanged)

    // Автосбор по активному коннекту: пока туннель поднят, раз в минуту пишем RTT
    // до текущего сервера (это метрика качества канала, а не доступности).
    Q_INVOKABLE void setConnected(bool connected);

    // Автоотчёт при неудачном подключении: юзер не жмёт кнопку, клиент сам
    // делает пробу и отправляет её. Не чаще одного раза в 10 минут.
    // userRequested = туннель сняли мы сами (кнопка юзера), а не сбой
    void handleConnectionState(int state, int errorCode, bool userRequested = false);

    // Рукопожатие вместо мусорной UDP-пробы: отчёт берёт результаты
    // HealthCheckController (AWG/WG — настоящий handshake, H2/VLESS — свой путь).
    void setHealthCheckController(class HealthCheckController *controller);

    QString lastAutoReportAt() const;

signals:
    void busyChanged();
    void scanningChanged();
    void progressChanged();
    void reportChanged();
    void sendStateChanged();
    void lastAutoReportAtChanged();
    void feedbackStateChanged();

private:
    struct PortProbe
    {
        int port = 0;
        QString address;      // конкретный адрес пула, который проверяем
        bool udpTransport = false; // AWG/WG/H2 — только UDP, VLESS — только TCP
        QString tcpState;     // open | refused | timeout | err
        qint64 tcpMs = -1;
        QString udpState;     // answered | closed | no-answer | err
    };

    struct ScanTarget
    {
        QString host;
        QString protocol;
        QList<int> ports;
        QStringList addresses;
        bool resolved = false;
        bool udpProtocol = false;
        int row = -1;
    };

    struct ScanTask
    {
        QString host;
        QString address;
        quint16 port = 0;
        QString protocol;
        QString transport; // tcp | udp
        QString tcpState;  // open | refused | timeout | no-host | err | skipped
        QString udpState;  // answered | closed | no-answer | err | skipped
        int rttMs = -1;
        QString error;
    };

    void setBusy(bool busy);
    void setScanning(bool scanning);
    void setProgress(int done, int total);
    void setSendState(const QString &state, const QString &message = QString());

    void resolveTarget();
    void probeIcmp();
    void probeNextPort();
    void probeUdpForCurrentPort();
    // Рукопожатие по всем адресам пула через HealthCheckController
    void probeHandshakes();
    void finishHandshakeProbe();
    void finishReport();

    QJsonObject buildReport() const;
    QString targetHost() const;
    QList<int> targetPorts() const;
    QString protocolTag() const;

    // --- полный скан ---
    void resolveNextScanHost();
    void buildScanTasks();
    void startNextScanTasks();
    void launchScanTask(int index);
    void completeScanTask(int index);
    void finishScan();
    QJsonObject buildScan() const;
    QStringList nodeIpsForIndex(int index) const;
    static bool transportIsUdp(const QString &protocol);

    // --- сведения об устройстве и сети (Android; на других платформах пусто) ---
    QJsonObject deviceInfo() const;
    QJsonObject networkInfo() const;
    QJsonObject settingsInfo() const;
    QString tunnelClientIp() const;
    int serverMtuValue() const;
    QJsonObject configInfo() const;
    void collectTunnel();
    QJsonObject buildTunnelReport(bool dnsOk, int dnsMs) const;
    QString configHashForIndex(int index) const;
    int portForPoolAddress(const QString &address, int fallbackPort) const;
    void scanDone();

    // --- обратная связь ---
    void setFeedbackState(const QString &state, const QString &message = QString());
    QJsonObject minimalReport() const;
    void postFeedbackPayload(const QJsonObject &payload);
    void flushPendingFeedback();
    QString m_feedbackState{QStringLiteral("idle")};
    QString m_feedbackMessage;
    bool m_tunnelActive = false; // туннель поднят — пробы не запускаем

    // --- сессия ---
    void sampleSession();
    QJsonObject buildSession() const;

    // --- отправка ---
    void sendPayload(const QString &endpoint, const QJsonObject &payload);
    QJsonObject authData() const;

    QSharedPointer<ServersModel> m_serversModel;
    class HealthCheckController *m_healthCheckController = nullptr;
    QJsonArray m_handshake; // результаты рукопожатий по адресам пула
    // состояние ожидания рукопожатия: подписка и таймер живут как члены,
    // чтобы их гарантированно снять после отчёта (висячая лямбда с таймером
    // роняла приложение на следующем проходе пробы)
    bool m_handshakeProbeActive = false;
    QTimer *m_handshakeTimer = nullptr;
    QMetaObject::Connection m_handshakeConn;
    std::shared_ptr<Settings> m_settings;

    bool m_busy = false;
    QJsonObject m_report;
    QString m_reportText;
    QString m_reportId;

    // состояние одной пробы (текущий сервер)
    QString m_host;
    QString m_resolvedAddress;
    QStringList m_resolvedIps;
    bool m_icmpOk = false;
    qint64 m_icmpMs = -1;
    QList<PortProbe> m_ports;
    int m_portIndex = 0;
    QTimer *m_stageTimer = nullptr;

    // полный скан
    bool m_scanning = false;
    bool m_scanHandshakeStarted = false;
    QTimer *m_scanHandshakeTimer = nullptr;
    QMetaObject::Connection m_scanHandshakeConn;
    int m_progressDone = 0;
    int m_progressTotal = 0;
    QString m_scanId;
    QDateTime m_scanStarted;
    QList<ScanTarget> m_scanTargets;
    QList<ScanTask> m_scanTasks;
    int m_scanResolveCursor = 0;
    int m_scanResolvePending = 0;
    int m_scanTaskCursor = 0;
    int m_scanActiveTasks = 0;
    QJsonObject m_scanPayload;

    // автосбор по активному коннекту
    QTimer *m_sessionTimer = nullptr;
    QList<int> m_sessionSamples;   // мс, -1 = потеря
    QDateTime m_sessionConnectedAt;
    bool m_sessionActive = false;
    // живая сессия оборвалась не по нашей воле — повод для автоотчёта
    bool m_hadLiveSession = false;
    qint64 m_liveSessionUptimeSec = 0;
    // автоотчёт: помним, что шла попытка подключения, чтобы отличить провал
    // от обычного «выключил VPN»
    bool m_connectAttemptActive = false;
    QJsonObject m_disconnectInfo;

    // отправка
    QString m_sendState = QStringLiteral("idle"); // idle | sending | sent | error
    QString m_sendMessage;
};

#endif // CONNECTIONREPORTCONTROLLER_H
