#include "protocols/vpnprotocol.h"
#include "connectionReportController.h"

#include "dopamine_application.h"

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#endif

#include <QLocale>
#include <QStorageInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QJsonDocument>
#include <QHostAddress>
#include <QNetworkInterface>
#if !defined(Q_OS_IOS)
#include <QProcess>
#endif
#include <QRegularExpression>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

#include <algorithm>
#include <random>

#include "core/api/apiDefs.h"
#include "ui/controllers/healthCheckController.h"
#include "core/controllers/gatewayController.h"
#include "core/defs.h"
#include "constants.h"
#include "settings.h"
#include "ui/models/servers_model.h"

namespace
{
    constexpr int kTcpTimeoutMs = 3000;
    constexpr int kUdpTimeoutMs = 1500;
    constexpr int kIcmpWaitMs = 1200;
    constexpr int kDnsTimeoutMs = 2500;
    // Сколько ждём рукопожатия по всем адресам пула, прежде чем отдать отчёт
    // с тем, что успели (AWG-handshake с junk-параметрами идёт ~1-3 с на адрес).
    constexpr int kHandshakeWaitMs = 15000;

#if !defined(Q_OS_IOS)
    // Пингуем системным ping: на Windows/macOS он не требует прав и уже есть.
    // На iOS шелла нет вообще — там ICMP не делаем.
    QStringList pingArgs(const QString &host)
    {
#if defined(Q_OS_WIN)
        return { QStringLiteral("-n"), QStringLiteral("1"), QStringLiteral("-w"),
                 QString::number(kIcmpWaitMs), host };
#else
        return { QStringLiteral("-c"), QStringLiteral("1"), QStringLiteral("-W"),
                 QString::number(kIcmpWaitMs / 1000 + 1), host };
#endif
    }
#endif
}

ConnectionReportController::ConnectionReportController(const QSharedPointer<ServersModel> &serversModel,
                                                       const std::shared_ptr<Settings> &settings, QObject *parent)
    : QObject(parent), m_serversModel(serversModel), m_settings(settings)
{
}

void ConnectionReportController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

QString ConnectionReportController::targetHost() const
{
    return m_serversModel ? m_serversModel->getServerCredentials(reportIndex()).hostName : QString();
}

QString ConnectionReportController::protocolTag() const
{
    return m_serversModel ? m_serversModel->data(reportIndex(), ServersModel::ServiceProtocolRole).toString()
                          : QString();
}

QList<int> ConnectionReportController::targetPorts() const
{
    QList<int> ports;
    if (!m_serversModel) {
        return ports;
    }
    // Порт берём у протокола строки. Раньше здесь был creds.port, а он читается
    // с верхнего уровня сервера — у серверов из API его нет, получался 0, и в
    // отчёт всегда уходил захардкоженный 443 (проба в закрытый порт → ложный timeout).
    const int index = reportIndex();
    const int port = m_serversModel->serverPort(index);
    if (port > 0) {
        ports.append(port);
    }

    // 443 добавляем только TLS/TCP-протоколам: для AWG/WG/H2 (UDP) проба на 443
    // бессмысленна и даёт ложную тревогу «нода недоступна».
    const QString proto = protocolTag().toLower();
    const bool isUdp = proto == QStringLiteral("awg") || proto == QStringLiteral("awg-mobile")
            || proto == QStringLiteral("amneziawgmobile") || proto == QStringLiteral("wireguard")
            || proto == QStringLiteral("hysteria2");
    if (!isUdp && !ports.contains(443)) {
        ports.append(443);
    }
    return ports;
}

int ConnectionReportController::reportIndex() const
{
    if (!m_serversModel) {
        return -1;
    }
    return m_reportIndex >= 0 ? m_reportIndex : m_serversModel->getProcessedServerIndex();
}

void ConnectionReportController::resetReport()
{
    if (m_report.isEmpty() && m_reportText.isEmpty()) {
        return;
    }
    m_report = QJsonObject();
    m_reportText.clear();
    m_ports.clear();
    m_handshake = QJsonArray();
    m_resolvedAddress.clear();
    m_reportIndex = -1;
    setSendState(QStringLiteral("idle"));
    emit reportChanged();
}

void ConnectionReportController::collect(int serverIndex)
{
    // На поднятом туннеле проб нет — но данные самого туннеля собрать можно
    // и нужно: именно они отвечают на «подключено, а интернета нет».
    if (m_tunnelActive) {
        m_reportIndex = serverIndex >= 0 ? serverIndex : (m_serversModel ? m_serversModel->getProcessedServerIndex() : -1);
        collectTunnel();
        return;
    }

    // Отчёт всегда про конкретную строку: глобальный «processed» индекс
    // сбрасывается кнопкой Connect на сервер по умолчанию, и карточка RU
    // показывала отчёт по польской ноде.
    m_reportIndex = serverIndex >= 0 ? serverIndex : (m_serversModel ? m_serversModel->getProcessedServerIndex() : -1);

    if (m_busy || !m_settings || !m_settings->isTelemetryEnabled()) {
        return;
    }

    // Новый проход начинается с чистого состояния: иначе строка в карточке
    // висит с «Sent» и выглядит так, будто кнопка больше ничего не делает.
    setSendState(QStringLiteral("idle"));

    m_report = QJsonObject();
    m_reportText.clear();
    m_resolvedAddress.clear();
    m_resolvedIps.clear();
    m_icmpOk = false;
    m_icmpMs = -1;
    m_ports.clear();
    m_handshake = QJsonArray();
    m_portIndex = 0;
    m_host = targetHost();

    if (m_host.isEmpty()) {
        m_report.insert(QStringLiteral("error"), QStringLiteral("no current server"));
        finishReport();
        return;
    }

    for (int port : targetPorts()) {
        PortProbe probe;
        probe.port = port;
        m_ports.append(probe);
    }

    // Проверяем весь пул, а не один резолвнутый адрес: у ноды несколько IP,
    // и один может быть заблокирован, пока другой отвечает. Транспорт — по
    // протоколу: AWG/WG/H2 только UDP, VLESS только TCP (TCP на UDP-порт давал
    // таймаут в пустоту).
    if (m_serversModel) {
        const int index = reportIndex();
        const QString protocol = protocolTag();
        const bool udpTransport = transportIsUdp(protocol);
        const int protocolPort = m_serversModel->serverPort(index);

        QStringList poolAddresses;
        for (const QJsonValue &value : m_serversModel->getServerConfig(index)
                                               .value(QStringLiteral("node_ips")).toArray()) {
            const QString ip = value.toString();
            if (!ip.isEmpty() && !poolAddresses.contains(ip)) {
                poolAddresses.append(ip);
            }
        }

        if (!poolAddresses.isEmpty()) {
            QList<PortProbe> targets;
            for (const QString &ip : poolAddresses) {
                PortProbe probe;
                probe.address = ip;
                // Порт адреса: сперва node_ports, потом порт протокола ТОЙ строки,
                // которой этот адрес принадлежит. Раньше всем адресам пула
                // доставался порт открытой строки — проба писала результат на
                // 8443/51820, а отчёт искал 443 и показывал «unknown».
                probe.port = portForPoolAddress(ip, protocolPort);
                probe.udpTransport = udpTransport;
                targets.append(probe);
            }
            m_ports = targets;
        } else {
            for (PortProbe &probe : m_ports) {
                probe.udpTransport = udpTransport;
            }
        }
    }

    setBusy(true);

    // Для UDP-протоколов (AWG/WG/H2) мусорная UDP-проба бесполезна: сервер
    // молчит на случайные байты, и в отчёт уходило «no-answer» вместо вердикта.
    // Берём настоящий handshake из HealthCheckController — он же даёт пинг.
    const bool udpOnly = !m_ports.isEmpty() && m_ports.first().udpTransport;
    if (udpOnly && m_healthCheckController) {
        probeHandshakes();
        return;
    }

    resolveTarget();
}

void ConnectionReportController::resolveTarget()
{
    auto *guard = new QTimer(this);
    guard->setSingleShot(true);
    connect(guard, &QTimer::timeout, this, [this, guard]() {
        guard->deleteLater();
        // DNS не ответил — дальше пробовать нечего, но отчёт всё равно соберём.
        probeIcmp();
    });
    guard->start(kDnsTimeoutMs);

    QHostInfo::lookupHost(m_host, this, [this, guard](const QHostInfo &info) {
        if (guard->isActive()) {
            guard->stop();
            guard->deleteLater();
        }
        for (const auto &address : info.addresses()) {
            m_resolvedIps.append(address.toString());
        }
        if (!m_resolvedIps.isEmpty()) {
            m_resolvedAddress = m_resolvedIps.first();
        }
        probeIcmp();
    });
}

void ConnectionReportController::probeIcmp()
{
#if defined(Q_OS_IOS)
    // iOS: процесса ping нет, ICMP сделать нечем — сразу пробы портов.
    probeNextPort();
#else
    if (m_resolvedAddress.isEmpty()) {
        // Резолва нет — ICMP по имени бессмыслен, идём сразу к портам.
        probeNextPort();
        return;
    }

    auto *process = new QProcess(this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);

    auto done = [this, process, timer](bool ok) {
        if (timer->isActive()) {
            timer->stop();
        }
        timer->deleteLater();
        process->deleteLater();
        m_icmpOk = ok;
        probeNextPort();
    };

    connect(process, &QProcess::finished, this, [process, done](int, QProcess::ExitStatus status) {
        const QString out = QString::fromLocal8Bit(process->readAllStandardOutput());
        const bool ok = status == QProcess::NormalExit && out.contains(QStringLiteral("TTL="), Qt::CaseInsensitive);
        done(ok);
    });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError) { done(false); });
    connect(timer, &QTimer::timeout, this, [process, done]() {
        process->kill();
        done(false);
    });

    timer->start(kIcmpWaitMs + 1500);
    process->start(QStringLiteral("ping"), pingArgs(m_resolvedAddress));
#endif // Q_OS_IOS
}

void ConnectionReportController::probeNextPort()
{
    if (m_portIndex >= m_ports.size()) {
        finishReport();
        return;
    }

    PortProbe &probe = m_ports[m_portIndex];

    // UDP-протокол (AWG/WG/H2) по TCP не проверяем: сервис там не слушает,
    // и любой такой замер — это таймаут в пустоту, а не «нода недоступна».
    if (probe.udpTransport) {
        probe.tcpState = QStringLiteral("skipped");
        probeUdpForCurrentPort();
        return;
    }

    auto *socket = new QTcpSocket(this);
    auto *timer = new QTimer(this);
    auto *elapsed = new QElapsedTimer();
    elapsed->start();
    timer->setSingleShot(true);

    auto advance = [this, socket, timer, elapsed](const QString &state, qint64 ms) {
        PortProbe &current = m_ports[m_portIndex];
        current.tcpState = state;
        current.tcpMs = ms;
        if (timer->isActive()) {
            timer->stop();
        }
        timer->deleteLater();
        socket->deleteLater();
        delete elapsed;
        probeUdpForCurrentPort();
    };

    connect(socket, &QTcpSocket::connected, this, [advance, elapsed]() {
        advance(QStringLiteral("open"), elapsed->elapsed());
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [advance, elapsed](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::ConnectionRefusedError) {
            advance(QStringLiteral("refused"), elapsed->elapsed());
        } else if (error == QAbstractSocket::SocketTimeoutError) {
            advance(QStringLiteral("timeout"), -1);
        } else if (error == QAbstractSocket::HostNotFoundError) {
            advance(QStringLiteral("no-host"), -1);
        } else {
            advance(QStringLiteral("err"), -1);
        }
    });
    connect(timer, &QTimer::timeout, this, [advance]() {
        advance(QStringLiteral("timeout"), -1);
    });

    timer->start(kTcpTimeoutMs);
    const QString connectHost = !probe.address.isEmpty()
            ? probe.address
            : (m_resolvedAddress.isEmpty() ? m_host : m_resolvedAddress);
    socket->connectToHost(connectHost, static_cast<quint16>(probe.port));
}

void ConnectionReportController::probeUdpForCurrentPort()
{
    const int port = m_ports[m_portIndex].port;

    // TCP-протокол (VLESS) по UDP не проверяем — это заведомо «no-answer».
    if (!m_ports[m_portIndex].udpTransport) {
        m_ports[m_portIndex].udpState = QStringLiteral("skipped");
        ++m_portIndex;
        probeNextPort();
        return;
    }

    auto *socket = new QUdpSocket(this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);

    std::random_device device;
    QByteArray payload(32, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(device() & 0xFF);
    }

    // «answered» — сервис ответил; «closed» — ICMP port unreachable (хост жив, порт
    // закрыт); «no-answer» — тишина, что для AWG/WG/H2 нормально (молчат на мусор).
    auto finish = [this, socket, timer](const QString &state) {
        m_ports[m_portIndex].udpState = state;
        if (timer->isActive()) {
            timer->stop();
        }
        timer->deleteLater();
        socket->deleteLater();
        ++m_portIndex;
        probeNextPort();
    };

    connect(socket, &QUdpSocket::readyRead, this, [socket, finish]() {
        while (socket->hasPendingDatagrams()) {
            QByteArray buffer;
            buffer.resize(static_cast<int>(socket->pendingDatagramSize()));
            socket->readDatagram(buffer.data(), buffer.size());
        }
        finish(QStringLiteral("answered"));
    });
    connect(socket, &QUdpSocket::errorOccurred, this, [finish](QAbstractSocket::SocketError error) {
        finish(error == QAbstractSocket::ConnectionRefusedError ? QStringLiteral("closed")
                                                               : QStringLiteral("err"));
    });
    connect(timer, &QTimer::timeout, this, [finish]() { finish(QStringLiteral("no-answer")); });

    const QString host = !m_ports[m_portIndex].address.isEmpty()
            ? m_ports[m_portIndex].address
            : (m_resolvedAddress.isEmpty() ? m_host : m_resolvedAddress);
    socket->writeDatagram(payload, QHostAddress(host), static_cast<quint16>(port));
    timer->start(kUdpTimeoutMs);
}

QJsonObject ConnectionReportController::buildReport() const
{
    QJsonObject report;

    QJsonObject app;
    app.insert(QStringLiteral("version"), Constants::versionString());
    app.insert(QStringLiteral("platform"), QSysInfo::productType());
    app.insert(QStringLiteral("os"), QSysInfo::productVersion());
    if (m_settings) {
        app.insert(QStringLiteral("build"), m_settings->diagBuild());
    }
    report.insert(QStringLiteral("app"), app);

    QJsonObject target;
    target.insert(QStringLiteral("host"), m_host);
    target.insert(QStringLiteral("address"), m_resolvedAddress);
    target.insert(QStringLiteral("protocol"), protocolTag());
    if (!m_ports.isEmpty()) {
        target.insert(QStringLiteral("port"), m_ports.first().port);
    }
    if (m_serversModel) {
        target.insert(QStringLiteral("country"), m_serversModel->data(reportIndex(), ServersModel::CountryNameRole).toString());
        target.insert(QStringLiteral("country_code"), m_serversModel->data(reportIndex(), ServersModel::CountryCodeRole).toString());
    }
    report.insert(QStringLiteral("target"), target);

    if (m_settings) {
        report.insert(QStringLiteral("support_tag"), m_settings->getInstallationUuid(false));

        // MTU: что просит клиент, что реально применили и что отдаёт сервер.
        // Без этого в поддержке не понять, жалуется юзер на дефолт или на свой override.
        QJsonObject mtu;
        mtu.insert(QStringLiteral("override"), m_settings->mtuOverride());
        mtu.insert(QStringLiteral("applied"), m_settings->lastAppliedMtu());
        const int serverMtu = serverMtuValue();
        if (serverMtu > 0) {
            mtu.insert(QStringLiteral("server"), serverMtu);
        }
        report.insert(QStringLiteral("mtu"), mtu);

        // Конфиг, который юзер реально получил от API (хеш/ревизия/протокол)
        const QJsonObject config = configInfo();
        if (!config.isEmpty()) {
            report.insert(QStringLiteral("config"), config);
        }

        // Последние попытки подключения: по ним видно, перебирал ли юзер адреса
        // и на чём именно спотыкался, а не только «сейчас не работает».
        const QJsonArray attempts = m_settings->connectAttempts();
        if (!attempts.isEmpty()) {
            QJsonArray tail;
            const int from = qMax(0, attempts.size() - 5);
            for (int i = from; i < attempts.size(); ++i) {
                tail.append(attempts.at(i));
            }
            report.insert(QStringLiteral("attempts"), tail);
        }
    }

    const QJsonObject device = deviceInfo();
    if (!device.isEmpty()) {
        report.insert(QStringLiteral("device"), device);
    }
    const QJsonObject network = networkInfo();
    if (!network.isEmpty()) {
        report.insert(QStringLiteral("network"), network);
    }
    const QJsonObject settingsInfoJson = settingsInfo();
    if (!settingsInfoJson.isEmpty()) {
        report.insert(QStringLiteral("settings"), settingsInfoJson);
    }

    QJsonObject probe;
    QJsonObject dns;
    QJsonArray ips;
    for (const auto &ip : m_resolvedIps) {
        ips.append(ip);
    }
    dns.insert(QStringLiteral("ips"), ips);
    probe.insert(QStringLiteral("dns"), dns);

#if !defined(Q_OS_IOS)
    // На iOS ICMP не делаем (нет шелла), поэтому и поле не отправляем: иначе
    // сервер прочитает "ok: false" как «хост не пингуется» и неверно оценит блок.
    QJsonObject icmp;
    icmp.insert(QStringLiteral("ok"), m_icmpOk);
    probe.insert(QStringLiteral("icmp"), icmp);
#endif

    QJsonArray tcp;
    QJsonArray udp;
    for (const auto &port : m_ports) {
        // В отчёт попадает только тот транспорт, который реально проверяли:
        // AWG/WG/H2 — UDP (и то лишь когда handshake недоступен), VLESS — TCP.
        // Записи без состояния не пишем вовсе: «udp: unknown» по каждому адресу
        // был мусором, который ничего не значит.
        if (!port.udpTransport) {
            if (port.tcpState.isEmpty()) {
                continue;
            }
            QJsonObject t;
            if (!port.address.isEmpty()) {
                t.insert(QStringLiteral("address"), port.address);
            }
            t.insert(QStringLiteral("port"), port.port);
            t.insert(QStringLiteral("state"), port.tcpState);
            if (port.tcpMs >= 0) {
                t.insert(QStringLiteral("ms"), static_cast<double>(port.tcpMs));
            }
            tcp.append(t);
        } else {
            if (port.udpState.isEmpty()) {
                continue;
            }
            QJsonObject u;
            if (!port.address.isEmpty()) {
                u.insert(QStringLiteral("address"), port.address);
            }
            u.insert(QStringLiteral("port"), port.port);
            u.insert(QStringLiteral("state"), port.udpState);
            udp.append(u);
        }
    }
    probe.insert(QStringLiteral("tcp"), tcp);
    probe.insert(QStringLiteral("udp"), udp);
    if (!m_handshake.isEmpty()) {
        probe.insert(QStringLiteral("handshake"), m_handshake);
    }
    report.insert(QStringLiteral("probe"), probe);

    if (m_sessionConnectedAt.isValid() || m_sessionActive || !m_sessionSamples.isEmpty()) {
        report.insert(QStringLiteral("session"), buildSession());
    }

    report.insert(QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return report;
}

void ConnectionReportController::setHealthCheckController(HealthCheckController *controller)
{
    m_healthCheckController = controller;
}

void ConnectionReportController::probeHandshakes()
{
    m_handshake = QJsonArray();
    // флаг ставим до проверки контроллера: иначе finishHandshakeProbe() сразу
    // выйдет по гварду и отчёт не соберётся
    m_handshakeProbeActive = true;
    if (!m_healthCheckController) {
        finishHandshakeProbe();
        return;
    }

    // Ровно один активный проход. Всё состояние — члены класса, подписка снимается
    // в finishHandshakeProbe(): лямбда с захваченным таймером, оставленная висеть
    // на probingFinished, роняла приложение на следующей пробе.
    if (!m_handshakeTimer) {
        m_handshakeTimer = new QTimer(this);
        m_handshakeTimer->setSingleShot(true);
        connect(m_handshakeTimer, &QTimer::timeout, this, [this]() {
            finishHandshakeProbe();
        });
    }
    m_handshakeTimer->start(kHandshakeWaitMs);

    // ВАЖНО: сначала запуск, потом подписка. startProbe() внутри останавливает
    // прошлый проход, и это синхронно испускает probingFinished — подписавшись
    // заранее, отчёт закрывался мгновенно, до записи результатов, и все адреса
    // выходили «unknown» при живых пингах в карточке.
    m_healthCheckController->startProbe(true);

    if (m_handshakeConn) {
        disconnect(m_handshakeConn);
        m_handshakeConn = {};
    }
    m_handshakeConn = connect(m_healthCheckController, &HealthCheckController::probingFinished,
                              this, [this]() { finishHandshakeProbe(); });

    if (!m_healthCheckController->isProbing()) {
        // проба закончилась синхронно (или целей не было) — подписка уже не сработает
        finishHandshakeProbe();
    }
}

void ConnectionReportController::finishHandshakeProbe()
{
    if (!m_handshakeProbeActive) {
        return; // уже отдали отчёт или отчёт не ждал рукопожатий
    }
    m_handshakeProbeActive = false;
    if (m_handshakeTimer) {
        m_handshakeTimer->stop();
    }
    if (m_handshakeConn) {
        disconnect(m_handshakeConn);
        m_handshakeConn = {};
    }

    // Статус галочек в карточке: что юзер выключил сам и какой адрес реально
    // использовался. Иначе в поддержке не понять, «in use» это или «switched off».
    const QStringList disabledByUser = m_settings ? m_settings->poolDisabledEndpoints() : QStringList();
    const QString activeEndpoint = m_settings ? m_settings->lastConnectedEndpoint() : QString();

    for (const PortProbe &target : m_ports) {
        if (target.address.isEmpty()) {
            continue;
        }
        const QVariantMap health = m_serversModel->endpointHealth(target.address, target.port);
        const QString state = health.value(QStringLiteral("state")).toString();
        const int rtt = health.value(QStringLiteral("rtt")).toInt();

        QJsonObject entry;
        entry.insert(QStringLiteral("address"), target.address);
        entry.insert(QStringLiteral("port"), target.port);
        entry.insert(QStringLiteral("transport"), QStringLiteral("udp"));
        entry.insert(QStringLiteral("disabled_by_user"), disabledByUser.contains(target.address));
        entry.insert(QStringLiteral("in_use"), !activeEndpoint.isEmpty() && target.address == activeEndpoint);
        if (state == QStringLiteral("ok") && rtt >= 0) {
            entry.insert(QStringLiteral("state"), QStringLiteral("ok"));
            entry.insert(QStringLiteral("ms"), rtt);
        } else if (state == QStringLiteral("unreachable")) {
            entry.insert(QStringLiteral("state"), QStringLiteral("unreachable"));
        } else {
            // проба не успела или её нет — честно помечаем, а не выдумываем вердикт
            entry.insert(QStringLiteral("state"), QStringLiteral("unknown"));
        }
        m_handshake.append(entry);
    }

    // target.address: в handshake-пути DNS не резолвим, и адрес уходил пустым.
    // Ставим тот, что реально в работе, и его же первым — чтобы target.port
    // в отчёте совпадал с адресом, а не с произвольным из пула.
    if (!m_ports.isEmpty()) {
        int useIndex = 0;
        for (int i = 0; i < m_handshake.size(); ++i) {
            if (m_handshake.at(i).toObject().value(QStringLiteral("in_use")).toBool()) {
                useIndex = i;
                break;
            }
        }
        if (useIndex > 0 && useIndex < m_ports.size()) {
            m_ports.move(useIndex, 0);
        }
        m_resolvedAddress = m_ports.first().address;
    }

    finishReport();
}


// Отчёт по живому туннелю: без проб по адресам, только то, что видно изнутри —
// поднят ли канал на самом деле (DNS через туннель), MTU, адрес в туннеле,
// сколько байт прошло и был ли зафиксирован отказ «данные не идут».
void ConnectionReportController::collectTunnel()
{
    if (m_busy || !m_settings) {
        return;
    }
    setBusy(true);
    setSendState(QStringLiteral("idle"));

    QElapsedTimer *timer = new QElapsedTimer();
    timer->start();
    auto *guard = new QTimer(this);
    guard->setSingleShot(true);

    auto finished = std::make_shared<bool>(false);
    auto finishOnce = [this, finished, guard, timer](bool ok) {
        if (*finished) {
            return;
        }
        *finished = true;
        if (guard->isActive()) {
            guard->stop();
        }
        guard->deleteLater();
        m_reportId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_report = buildTunnelReport(ok, static_cast<int>(timer->elapsed()));
        m_scanPayload = QJsonObject();
        m_reportText = QString::fromUtf8(QJsonDocument(m_report).toJson(QJsonDocument::Indented));
        delete timer;
        setBusy(false);
        emit reportChanged();
        // см. finishReport(): отправка только по кнопке Send
    };

    connect(guard, &QTimer::timeout, this, [finishOnce]() { finishOnce(false); });
    guard->start(3000);

    // DNS через туннель: единственная проверка, которая имеет смысл на живом VPN
    QHostInfo::lookupHost(QStringLiteral("api.frkn.org"), this, [finishOnce](const QHostInfo &info) {
        finishOnce(info.error() == QHostInfo::NoError);
    });
}

QJsonObject ConnectionReportController::buildTunnelReport(bool dnsOk, int dnsMs) const
{
    QJsonObject report = minimalReport();
    report.insert(QStringLiteral("mode"), QStringLiteral("tunnel"));
    report.insert(QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QJsonObject session = buildSession();
    session.insert(QStringLiteral("dns_check"), dnsOk ? QStringLiteral("ok") : QStringLiteral("fail"));
    if (dnsOk) {
        session.insert(QStringLiteral("dns_ms"), dnsMs);
    }
    report.insert(QStringLiteral("session"), session);

    QJsonObject tunnel;
    if (m_settings) {
        tunnel.insert(QStringLiteral("endpoint"), m_settings->lastConnectedEndpoint());
        tunnel.insert(QStringLiteral("mtu_applied"), m_settings->lastAppliedMtu());
    }
    const QString tunnelIp = tunnelClientIp();
    if (!tunnelIp.isEmpty()) {
        tunnel.insert(QStringLiteral("client_ip"), tunnelIp);
    }
    if (m_serversModel && reportIndex() >= 0) {
        tunnel.insert(QStringLiteral("port"), m_serversModel->serverPort(reportIndex()));
    }
    if (m_settings) {
        // Сколько реально прошло через туннель: «подключено, но 0 байт» —
        // главный жалобный кейс, по одним RTT его не отличить.
        QJsonObject transport;
        transport.insert(QStringLiteral("rx_bytes"), static_cast<double>(m_settings->liveRxBytes()));
        transport.insert(QStringLiteral("tx_bytes"), static_cast<double>(m_settings->liveTxBytes()));
        tunnel.insert(QStringLiteral("transport_bytes"), transport);

        const QJsonObject prev = m_settings->prevSession();
        if (!prev.isEmpty()) {
            session.insert(QStringLiteral("prev"), prev);
            report.insert(QStringLiteral("session"), session);
        }
    }
    report.insert(QStringLiteral("tunnel"), tunnel);
    return report;
}

void ConnectionReportController::finishReport()
{
    m_reportId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_report = buildReport();
    m_scanPayload = QJsonObject();
    m_reportText = QString::fromUtf8(QJsonDocument(m_report).toJson(QJsonDocument::Indented));
    setBusy(false);
    emit reportChanged();
    // Отправку НЕ делаем автоматически: «проверить» и «отправить» — разные
    // действия, юзер сам жмёт Send (или Copy). Иначе половина отчётов уходила
    // без его решения, а половина — нет, и логика выглядела сломанной.
}

// ---------------------------------------------------------------------------
// Полный скан: каждый сервер из подписки, каждый его адрес, каждый порт.
// Запускать только при выключенном VPN — иначе пробы идут через туннель и меряют
// туннель, а не доступность сервера.
// ---------------------------------------------------------------------------

namespace
{
    constexpr int kScanConcurrency = 16;
    constexpr int kScanTcpTimeoutMs = 3000;
    constexpr int kScanUdpTimeoutMs = 700;
    constexpr int kScanMaxTasks = 512;
    constexpr int kScanMaxDurationMs = 60000;
    constexpr int kScanHandshakeWaitMs = 25000;
    constexpr int kScanDnsTimeoutMs = 4000;
}

bool ConnectionReportController::transportIsUdp(const QString &protocol)
{
    const QString p = protocol.toLower();
    return p.contains(QStringLiteral("awg")) || p.contains(QStringLiteral("wireguard"))
           || p.contains(QStringLiteral("hysteria"));
}

QStringList ConnectionReportController::nodeIpsForIndex(int index) const
{
    QStringList ips;
    if (!m_serversModel) {
        return ips;
    }
    const QStringList raw = m_serversModel->data(index, ServersModel::NodeIpsRole).toStringList();
    for (const QString &ip : raw) {
        if (!ip.isEmpty() && !ips.contains(ip)) {
            ips.append(ip);
        }
    }
    return ips;
}

void ConnectionReportController::collectAll()
{
    // Скан «все серверы» на живом туннеле тоже врёт: пробы идут внутрь VPN
    if (m_tunnelActive) {
        return;
    }

    if (m_busy || !m_serversModel || !m_settings || !m_settings->isTelemetryEnabled()) {
        return;
    }

    m_report = QJsonObject();
    m_reportText.clear();
    m_scanPayload = QJsonObject();
    m_scanTargets.clear();
    m_scanTasks.clear();
    m_scanResolveCursor = 0;
    m_scanResolvePending = 0;
    m_scanTaskCursor = 0;
    m_scanActiveTasks = 0;
    m_scanStarted = QDateTime::currentDateTimeUtc();
    m_scanId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_reportId = QUuid::createUuid().toString(QUuid::WithoutBraces); // новый отчёт — новый id
    m_scanHandshakeStarted = false;
    setProgress(0, 0);
    setSendState(QStringLiteral("idle"));
    setScanning(true);
    setBusy(true);

    const int count = m_serversModel->getServersCount();
    for (int i = 0; i < count; ++i) {
        const auto creds = m_serversModel->getServerCredentials(i);
        if (creds.hostName.isEmpty()) {
            continue;
        }
        ScanTarget target;
        target.host = creds.hostName;
        target.protocol = m_serversModel->data(i, ServersModel::ServiceProtocolRole).toString();
        // Порт протокола (у API-серверов он в контейнере, не на верхнем уровне)
        const int protoPort = m_serversModel->serverPort(i);
        if (protoPort > 0 && protoPort != 22) {
            target.ports.append(protoPort);
        }
        const QString proto = target.protocol.toLower();
        const bool isUdp = proto == QStringLiteral("awg") || proto == QStringLiteral("awg-mobile")
                || proto == QStringLiteral("amneziawgmobile") || proto == QStringLiteral("wireguard")
                || proto == QStringLiteral("hysteria2");
        if (!isUdp && !target.ports.contains(443)) {
            target.ports.append(443);
        }
        target.addresses = nodeIpsForIndex(i);
        target.row = i;
        target.udpProtocol = isUdp;
        m_scanTargets.append(target);
    }

    if (m_scanTargets.isEmpty()) {
        finishScan();
        return;
    }

    resolveNextScanHost();
}

void ConnectionReportController::resolveNextScanHost()
{
    while (m_scanResolveCursor < m_scanTargets.size()) {
        ScanTarget &target = m_scanTargets[m_scanResolveCursor];
        ++m_scanResolveCursor;

        // Хост может быть уже литеральным IP — тогда DNS не нужен.
        QHostAddress literal;
        if (literal.setAddress(target.host)) {
            if (!target.addresses.contains(target.host)) {
                target.addresses.append(target.host);
            }
            target.resolved = true;
            continue;
        }

        target.resolved = true;
        ++m_scanResolvePending;
        const int index = m_scanResolveCursor - 1;

        auto *guard = new QTimer(this);
        guard->setSingleShot(true);
        auto done = QSharedPointer<bool>::create(false);

        auto complete = [this, index, guard, done](const QStringList &ips) {
            if (*done) {
                return;
            }
            *done = true;
            guard->stop();
            guard->deleteLater();
            for (const QString &ip : ips) {
                if (!ip.isEmpty() && !m_scanTargets[index].addresses.contains(ip)) {
                    m_scanTargets[index].addresses.append(ip);
                }
            }
            --m_scanResolvePending;
            if (m_scanResolvePending == 0) {
                buildScanTasks();
            } else {
                resolveNextScanHost();
            }
        };

        connect(guard, &QTimer::timeout, this, [complete]() { complete({}); });
        guard->start(kScanDnsTimeoutMs);

        QHostInfo::lookupHost(target.host, this, [complete](const QHostInfo &info) {
            QStringList ips;
            for (const auto &address : info.addresses()) {
                ips.append(address.toString());
            }
            complete(ips);
        });

        // Следующий резолв запускаем сразу — DNS параллельно быстрее.
        continue;
    }

    if (m_scanResolvePending == 0) {
        buildScanTasks();
    }
}

void ConnectionReportController::buildScanTasks()
{
    m_scanTasks.clear();
    for (const ScanTarget &target : m_scanTargets) {
        for (const QString &address : target.addresses) {
            for (int port : target.ports) {
                if (m_scanTasks.size() >= kScanMaxTasks) {
                    break;
                }
                ScanTask task;
                task.host = target.host;
                task.address = address;
                task.port = static_cast<quint16>(port);
                task.protocol = target.protocol;
                task.transport = transportIsUdp(target.protocol) ? QStringLiteral("udp") : QStringLiteral("tcp");
                // UDP-протоколы (AWG/WG/H2) не сканируем сырым пакетом: сервер
                // на мусор молчит by design, вердикт даёт только рукопожатие —
                // его результаты подмешиваем в конце скана (startScanHandshake).
                if (task.transport == QStringLiteral("udp")) {
                    continue;
                }
                m_scanTasks.append(task);
            }
        }
    }

    setProgress(0, m_scanTasks.size());
    if (m_scanTasks.isEmpty()) {
        // все цели UDP (AWG/WG/H2): сырых задач нет, но вердикт даст рукопожатие
        scanDone();
        return;
    }
    startNextScanTasks();
}

void ConnectionReportController::startNextScanTasks()
{
    if (m_scanStarted.msecsTo(QDateTime::currentDateTimeUtc()) > kScanMaxDurationMs) {
        scanDone();
        return;
    }

    while (m_scanActiveTasks < kScanConcurrency && m_scanTaskCursor < m_scanTasks.size()) {
        const int index = m_scanTaskCursor++;
        ++m_scanActiveTasks;
        launchScanTask(index);
    }

    if (m_scanActiveTasks == 0 && m_scanTaskCursor >= m_scanTasks.size()) {
        scanDone();
    }
}

// Порт адреса пула: ищем строку, которой этот адрес принадлежит (node_ips),
// и берём порт её протокола; node_ports[ip] имеет приоритет.
int ConnectionReportController::portForPoolAddress(const QString &address, int fallbackPort) const
{
    if (!m_serversModel || address.isEmpty()) {
        return fallbackPort;
    }
    const int count = m_serversModel->getServersCount();
    for (int row = 0; row < count; ++row) {
        const QJsonObject config = m_serversModel->getServerConfig(row);
        const QJsonArray ips = config.value(QStringLiteral("node_ips")).toArray();
        for (const QJsonValue &value : ips) {
            if (value.toString() != address) {
                continue;
            }
            return ServersModel::portForNodeIp(config, address, m_serversModel->serverPort(row));
        }
    }
    return fallbackPort;
}

// Скан закончил TCP-часть. UDP-строки вердикта не имеют — прогоняем рукопожатие
// по всем серверам и только потом собираем отчёт.
void ConnectionReportController::scanDone()
{
    if (m_scanHandshakeStarted) {
        finishScan();
        return;
    }
    m_scanHandshakeStarted = true;

    const bool hasUdpTargets = std::any_of(m_scanTargets.cbegin(), m_scanTargets.cend(),
                                           [](const ScanTarget &target) { return target.udpProtocol; });
    if (!hasUdpTargets || !m_healthCheckController) {
        finishScan();
        return;
    }

    if (!m_scanHandshakeTimer) {
        m_scanHandshakeTimer = new QTimer(this);
        m_scanHandshakeTimer->setSingleShot(true);
        connect(m_scanHandshakeTimer, &QTimer::timeout, this, [this]() { finishScan(); });
    }

    // порядок как в probeHandshakes(): сначала запуск, потом подписка
    m_healthCheckController->startProbe(true);
    if (m_scanHandshakeConn) {
        disconnect(m_scanHandshakeConn);
        m_scanHandshakeConn = {};
    }
    m_scanHandshakeConn = connect(m_healthCheckController, &HealthCheckController::probingFinished,
                                  this, [this]() { finishScan(); });
    m_scanHandshakeTimer->start(kScanHandshakeWaitMs);
    if (!m_healthCheckController->isProbing()) {
        finishScan();
    }
}

void ConnectionReportController::completeScanTask(int index)
{
    Q_UNUSED(index);
    --m_scanActiveTasks;
    setProgress(m_progressDone + 1, m_progressTotal);
    startNextScanTasks();
}

void ConnectionReportController::launchScanTask(int index)
{
    ScanTask &task = m_scanTasks[index];

    if (task.transport == QStringLiteral("udp")) {
        auto *socket = new QUdpSocket(this);
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);

        std::random_device device;
        QByteArray payload(24, Qt::Uninitialized);
        for (int i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(device() & 0xFF);
        }

        auto done = [this, index, socket, timer](const QString &state) {
            m_scanTasks[index].udpState = state;
            if (timer->isActive()) {
                timer->stop();
            }
            timer->deleteLater();
            socket->deleteLater();
            completeScanTask(index);
        };

        connect(socket, &QUdpSocket::readyRead, this, [socket, done]() {
            while (socket->hasPendingDatagrams()) {
                QByteArray buffer;
                buffer.resize(static_cast<int>(socket->pendingDatagramSize()));
                socket->readDatagram(buffer.data(), buffer.size());
            }
            done(QStringLiteral("answered"));
        });
        connect(socket, &QUdpSocket::errorOccurred, this, [done](QAbstractSocket::SocketError error) {
            done(error == QAbstractSocket::ConnectionRefusedError ? QStringLiteral("closed") : QStringLiteral("err"));
        });
        connect(timer, &QTimer::timeout, this, [done]() { done(QStringLiteral("no-answer")); });

        socket->writeDatagram(payload, QHostAddress(task.address), task.port);
        timer->start(kScanUdpTimeoutMs);
        return;
    }

    auto *socket = new QTcpSocket(this);
    auto *timer = new QTimer(this);
    auto *elapsed = new QElapsedTimer();
    elapsed->start();
    timer->setSingleShot(true);

    auto done = [this, index, socket, timer, elapsed](const QString &state, int ms) {
        m_scanTasks[index].tcpState = state;
        m_scanTasks[index].rttMs = ms;
        if (timer->isActive()) {
            timer->stop();
        }
        timer->deleteLater();
        socket->deleteLater();
        delete elapsed;
        completeScanTask(index);
    };

    connect(socket, &QTcpSocket::connected, this, [done, elapsed]() {
        done(QStringLiteral("open"), static_cast<int>(elapsed->elapsed()));
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [done, elapsed](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::ConnectionRefusedError) {
            done(QStringLiteral("refused"), static_cast<int>(elapsed->elapsed()));
        } else if (error == QAbstractSocket::HostNotFoundError) {
            done(QStringLiteral("no-host"), -1);
        } else if (error == QAbstractSocket::SocketTimeoutError) {
            done(QStringLiteral("timeout"), -1);
        } else {
            done(QStringLiteral("err"), -1);
        }
    });
    connect(timer, &QTimer::timeout, this, [done]() { done(QStringLiteral("timeout"), -1); });

    timer->start(kScanTcpTimeoutMs);
    socket->connectToHost(task.address, task.port);
}

QJsonObject ConnectionReportController::buildScan() const
{
    QJsonObject scan;

    QJsonObject app;
    app.insert(QStringLiteral("version"), Constants::versionString());
    app.insert(QStringLiteral("platform"), QSysInfo::productType());
    app.insert(QStringLiteral("os"), QSysInfo::productVersion());
    scan.insert(QStringLiteral("app"), app);

    if (m_settings) {
        scan.insert(QStringLiteral("support_tag"), m_settings->getInstallationUuid(false));
    }
    scan.insert(QStringLiteral("mode"), QStringLiteral("full"));
    scan.insert(QStringLiteral("started_at"), m_scanStarted.toString(Qt::ISODate));
    scan.insert(QStringLiteral("duration_ms"), static_cast<double>(m_scanStarted.msecsTo(QDateTime::currentDateTimeUtc())));

    QJsonObject network = networkInfo();
    if (!network.contains(QStringLiteral("mobile"))) {
        network.insert(QStringLiteral("mobile"),
                       QSysInfo::productType().contains(QStringLiteral("android"))
                               || QSysInfo::productType().contains(QStringLiteral("ios")));
    }
    const QJsonObject device = deviceInfo();
    if (!device.isEmpty()) {
        scan.insert(QStringLiteral("device"), device);
    }
    scan.insert(QStringLiteral("network"), network);
    const QJsonObject scanSettings = settingsInfo();
    if (!scanSettings.isEmpty()) {
        scan.insert(QStringLiteral("settings"), scanSettings);
    }
    if (m_settings) {
        scan.insert(QStringLiteral("app_build"), m_settings->diagBuild());
    }

    QJsonArray results;

    // Статус галочек и адрес, к которому реально подключались — те же поля,
    // что в отчёте по одной строке, иначе скан и карточка говорят разное.
    const QStringList disabledByUser = m_settings ? m_settings->poolDisabledEndpoints() : QStringList();
    const QString activeEndpoint = m_settings ? m_settings->lastConnectedEndpoint() : QString();

    const auto fillEndpointFlags = [&](QJsonObject &item, const QString &address) {
        item.insert(QStringLiteral("disabled_by_user"), disabledByUser.contains(address));
        item.insert(QStringLiteral("in_use"), !activeEndpoint.isEmpty() && address == activeEndpoint);
    };

    for (const ScanTask &task : m_scanTasks) {
        QJsonObject item;
        item.insert(QStringLiteral("host"), task.host);
        item.insert(QStringLiteral("address"), task.address);
        item.insert(QStringLiteral("port"), task.port);
        item.insert(QStringLiteral("protocol"), task.protocol);
        item.insert(QStringLiteral("transport"), task.transport);
        if (task.transport == QStringLiteral("udp")) {
            item.insert(QStringLiteral("udp"), task.udpState.isEmpty() ? QStringLiteral("skipped") : task.udpState);
        } else {
            item.insert(QStringLiteral("tcp"), task.tcpState.isEmpty() ? QStringLiteral("skipped") : task.tcpState);
            if (task.rttMs >= 0) {
                item.insert(QStringLiteral("rtt_ms"), task.rttMs);
            }
        }
        if (!task.error.isEmpty()) {
            item.insert(QStringLiteral("error"), task.error);
        }
        fillEndpointFlags(item, task.address);
        results.append(item);
    }

    // UDP-протоколы: вердикт из рукопожатия (endpointHealth пишет его
    // HealthCheckController), а не из сырого UDP-пакета.
    for (const ScanTarget &target : m_scanTargets) {
        if (!target.udpProtocol || target.addresses.isEmpty()) {
            continue;
        }
        const int fallbackPort = target.ports.isEmpty() ? 0 : target.ports.first();
        const QJsonObject scanConfig = m_serversModel ? m_serversModel->getServerConfig(target.row) : QJsonObject();
        for (const QString &address : target.addresses) {
            const int port = ServersModel::portForNodeIp(scanConfig, address, fallbackPort);
            const QVariantMap health = m_serversModel
                    ? m_serversModel->endpointHealth(address, port)
                    : QVariantMap();
            QJsonObject item;
            item.insert(QStringLiteral("host"), target.host);
            item.insert(QStringLiteral("address"), address);
            item.insert(QStringLiteral("port"), port);
            item.insert(QStringLiteral("protocol"), target.protocol);
            item.insert(QStringLiteral("transport"), QStringLiteral("handshake"));
            item.insert(QStringLiteral("udp"),
                        health.value(QStringLiteral("state"), QStringLiteral("unknown")).toString());
            const int rtt = health.value(QStringLiteral("rtt"), -2).toInt();
            if (rtt >= 0) {
                item.insert(QStringLiteral("rtt_ms"), rtt);
            }
            const QString configHash = configHashForIndex(target.row);
            if (!configHash.isEmpty()) {
                item.insert(QStringLiteral("config_hash"), configHash);
            }
            fillEndpointFlags(item, address);
            results.append(item);
        }
    }

    scan.insert(QStringLiteral("results"), results);
    return scan;
}

void ConnectionReportController::finishScan()
{
    m_scanPayload = buildScan();
    if (m_reportId.isEmpty()) {
        m_reportId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_report = QJsonObject();
    m_reportText = QString::fromUtf8(QJsonDocument(m_scanPayload).toJson(QJsonDocument::Indented));
    m_scanning = false;
    emit scanningChanged();
    setBusy(false);
    emit reportChanged();

    if (m_settings && m_settings->isTelemetryEnabled()) {
        send();
    }
}

void ConnectionReportController::cancel()
{
    if (!m_busy) {
        return;
    }
    if (m_scanning) {
        m_scanTaskCursor = m_scanTasks.size();
        finishScan();
        return;
    }
    setBusy(false);
}

// ---------------------------------------------------------------------------
// Отправка на gateway
// ---------------------------------------------------------------------------

QJsonObject ConnectionReportController::authData() const
{
    if (!m_serversModel) {
        return {};
    }
    const int index = m_serversModel->getProcessedServerIndex();
    const QVariant cfg = m_serversModel->getServerData(index, QStringLiteral("apiConfig"));
    const QJsonObject apiConfig = cfg.toJsonObject();
    QJsonObject auth = apiConfig.value(QStringLiteral("auth_data")).toObject();
    if (auth.isEmpty()) {
        auth = cfg.toJsonObject().value(QStringLiteral("auth_data")).toObject();
    }
    return auth;
}

void ConnectionReportController::setFeedbackState(const QString &state, const QString &message)
{
    if (m_feedbackState == state && m_feedbackMessage == message) {
        return;
    }
    m_feedbackState = state;
    m_feedbackMessage = message;
    emit feedbackStateChanged();
}

// Краткий отчёт для фидбека: без проб и рукопожатий (юзер не просил ждать),
// но с тем, что нужно поддержке — версия, сеть, MTU, конфиг, целевой сервер.
QJsonObject ConnectionReportController::minimalReport() const
{
    QJsonObject report;

    QJsonObject app;
    app.insert(QStringLiteral("version"), Constants::versionString());
    app.insert(QStringLiteral("platform"), QSysInfo::productType().toLower());
    app.insert(QStringLiteral("os"), QSysInfo::productVersion());
    report.insert(QStringLiteral("app"), app);
    report.insert(QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (m_settings) {
        report.insert(QStringLiteral("support_tag"), m_settings->getInstallationUuid(false));

        QJsonObject mtu;
        mtu.insert(QStringLiteral("override"), m_settings->mtuOverride());
        mtu.insert(QStringLiteral("applied"), m_settings->lastAppliedMtu());
        const int serverMtu = serverMtuValue();
        if (serverMtu > 0) {
            mtu.insert(QStringLiteral("server"), serverMtu);
        }
        report.insert(QStringLiteral("mtu"), mtu);
    }

    const QJsonObject config = configInfo();
    if (!config.isEmpty()) {
        report.insert(QStringLiteral("config"), config);
    }
    const QJsonObject network = networkInfo();
    if (!network.isEmpty()) {
        report.insert(QStringLiteral("network"), network);
    }
    const QJsonObject device = deviceInfo();
    if (!device.isEmpty()) {
        report.insert(QStringLiteral("device"), device);
    }

    if (m_serversModel) {
        const int index = reportIndex();
        QJsonObject target;
        target.insert(QStringLiteral("host"), m_serversModel->getServerCredentials(reportIndex()).hostName);
        target.insert(QStringLiteral("port"), m_serversModel->serverPort(index));
        target.insert(QStringLiteral("protocol"), protocolTag());
        report.insert(QStringLiteral("target"), target);
    }

    return report;
}

void ConnectionReportController::sendFeedback(const QString &message, bool attachReport)
{
    if (!m_settings) {
        return;
    }
    if (m_feedbackState == QStringLiteral("sending")) {
        return;
    }

    // Контракт с бэкендом: 1…2000 символов после trim
    const QString text = message.trimmed().left(2000);
    if (text.isEmpty() && !attachReport) {
        setFeedbackState(QStringLiteral("error"), tr("Type a message first"));
        return;
    }

    const QJsonObject auth = authData();
    if (auth.isEmpty()) {
        setFeedbackState(QStringLiteral("error"), tr("Sign in to your subscription to send feedback"));
        return;
    }

    QJsonObject feedback;
    feedback.insert(QStringLiteral("feedback_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    feedback.insert(QStringLiteral("message"), text);
    feedback.insert(QStringLiteral("lang"), m_settings->getAppLanguage().name().left(2));
    feedback.insert(QStringLiteral("attach_report"), attachReport);

    QJsonObject payload;
    payload.insert(QStringLiteral("auth_data"), auth);
    payload.insert(QStringLiteral("report_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    // свежий отчёт из этой сессии, если он есть; иначе краткий (без проб)
    payload.insert(QStringLiteral("report"), (attachReport && hasReport()) ? m_report : minimalReport());
    payload.insert(QStringLiteral("feedback"), feedback);

    setFeedbackState(QStringLiteral("sending"));
    flushPendingFeedback(); // заодно добиваем то, что не ушло раньше
    postFeedbackPayload(payload);
}

void ConnectionReportController::postFeedbackPayload(const QJsonObject &payload)
{
    auto gatewayController = QSharedPointer<GatewayController>::create(
            m_settings->getGatewayEndpoint(), m_settings->isDevGatewayEnv(), apiDefs::requestTimeoutMsecs,
            m_settings->isStrictKillSwitchEnabled(), nullptr, m_settings->getGatewayEndpointFallback());

    auto future = gatewayController->postAsync(QStringLiteral("%1v1/report/block"), payload);
    future.then(this, [this, gatewayController, payload](QPair<ErrorCode, QByteArray> result) {
        const auto [errorCode, response] = result;
        if (errorCode != ErrorCode::NoError) {
            // нет сети — в очередь: отправим при следующей попытке или запуске
            QJsonArray pending = m_settings->pendingFeedback();
            pending.append(payload);
            m_settings->setPendingFeedback(pending);
            setFeedbackState(QStringLiteral("queued"), tr("No connection — we'll send it later"));
            return;
        }

        const QJsonObject body = QJsonDocument::fromJson(response).object();
        if (body.contains(QStringLiteral("status")) && body.value(QStringLiteral("status")).toInt() >= 400) {
            setFeedbackState(QStringLiteral("error"),
                             tr("Send failed (http %1)").arg(body.value(QStringLiteral("status")).toInt()));
            return;
        }
        setFeedbackState(QStringLiteral("sent"), tr("Thanks — we got your message"));
    });
}

void ConnectionReportController::flushPendingFeedback()
{
    if (!m_settings) {
        return;
    }
    QJsonArray pending = m_settings->pendingFeedback();
    if (pending.isEmpty()) {
        return;
    }
    // по одному за раз, самый старый первым; остальные — следующей попыткой
    const QJsonObject payload = pending.at(0).toObject();
    pending.removeFirst();
    m_settings->setPendingFeedback(pending);
    postFeedbackPayload(payload);
}

void ConnectionReportController::copyReport()
{
    if (!amnApp) {
        return;
    }
    if (m_reportText.isEmpty()) {
        setSendState(QStringLiteral("error"), QStringLiteral("nothing to copy"));
        return;
    }
    amnApp->getClipboard()->setText(m_reportText);
}

void ConnectionReportController::send()
{
    if (m_sendState == QStringLiteral("sending")) {
        return;
    }

    if (!m_settings || !m_settings->isTelemetryEnabled()) {
        setSendState(QStringLiteral("error"), QStringLiteral("telemetry is disabled"));
        return;
    }

    const QJsonObject auth = authData();
    if (auth.isEmpty()) {
        setSendState(QStringLiteral("error"), QStringLiteral("no subscription auth data"));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("auth_data"), auth);

    // Контракт с бэкендом: report_id — UUID, один на отчёт, повторяется при
    // переотправке и присутствует в ОБОИХ режимах. app — тоже на верхнем
    // уровне: бэкенд читает версию оттуда, а не только из вложенного объекта.
    if (m_reportId.isEmpty()) {
        m_reportId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    payload.insert(QStringLiteral("report_id"), m_reportId);

    QJsonObject appInfo;
    appInfo.insert(QStringLiteral("version"), Constants::versionString());
    appInfo.insert(QStringLiteral("platform"), QSysInfo::productType());
    appInfo.insert(QStringLiteral("os"), QSysInfo::productVersion());
    payload.insert(QStringLiteral("app"), appInfo);

    if (hasScan()) {
        payload.insert(QStringLiteral("scan_id"), m_scanId);
        payload.insert(QStringLiteral("scan"), m_scanPayload);
        sendPayload(QStringLiteral("%1v1/report/scan"), payload);
    } else if (hasReport()) {
        payload.insert(QStringLiteral("report"), m_report);
        sendPayload(QStringLiteral("%1v1/report/block"), payload);
    } else {
        setSendState(QStringLiteral("error"), QStringLiteral("nothing to send"));
    }
}

void ConnectionReportController::sendPayload(const QString &endpoint, const QJsonObject &payload)
{
    setSendState(QStringLiteral("sending"));

    auto gatewayController = QSharedPointer<GatewayController>::create(
            m_settings->getGatewayEndpoint(), m_settings->isDevGatewayEnv(), apiDefs::requestTimeoutMsecs,
            m_settings->isStrictKillSwitchEnabled(), nullptr, m_settings->getGatewayEndpointFallback());

    auto future = gatewayController->postAsync(endpoint, payload);
    future.then(this, [this, gatewayController](QPair<ErrorCode, QByteArray> result) {
        const auto [errorCode, response] = result;
        if (errorCode != ErrorCode::NoError) {
            setSendState(QStringLiteral("error"),
                         QStringLiteral("request failed (%1)").arg(static_cast<int>(errorCode)));
            return;
        }

        const QJsonObject body = QJsonDocument::fromJson(response).object();
        if (body.contains(QStringLiteral("status"))) {
            const int status = body.value(QStringLiteral("status")).toInt();
            if (status >= 400) {
                setSendState(QStringLiteral("error"), QStringLiteral("http %1").arg(status));
                return;
            }
        }
        setSendState(QStringLiteral("sent"), QStringLiteral("report accepted"));
    });
}

// ---------------------------------------------------------------------------
// Сведения об устройстве и сети
//
// Собираем только то, что не требует опасных разрешений и не является
// аппаратным идентификатором (никаких IMEI/IMSI/серийников/MAC и рекламных id):
// это важно и для сторов, и по смыслу — нам нужен «оператор + регион + система»,
// а не привязка к конкретному железу.
// ---------------------------------------------------------------------------

QJsonObject ConnectionReportController::deviceInfo() const
{
    QJsonObject device;
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    // Модель железа: "iPhone16,2", "Mac14,5" — по ней видно устройство без
    // сбора чего-либо персонального.
    char machine[128] = {0};
    size_t machineLen = sizeof(machine);
    if (sysctlbyname("hw.machine", machine, &machineLen, nullptr, 0) == 0) {
        device.insert(QStringLiteral("model"), QString::fromLatin1(machine));
    }
    char model[128] = {0};
    size_t modelLen = sizeof(model);
    if (sysctlbyname("hw.model", model, &modelLen, nullptr, 0) == 0) {
        device.insert(QStringLiteral("model_ident"), QString::fromLatin1(model));
    }
    quint64 memBytes = 0;
    size_t memLen = sizeof(memBytes);
    if (sysctlbyname("hw.memsize", &memBytes, &memLen, nullptr, 0) == 0 && memBytes > 0) {
        device.insert(QStringLiteral("total_ram_mb"), static_cast<int>(memBytes / (1024ull * 1024ull)));
    }
    device.insert(QStringLiteral("os_version"), QSysInfo::productVersion());
    device.insert(QStringLiteral("locale"), QLocale::system().name());
#endif
#ifdef Q_OS_ANDROID
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return device;
    }

    const auto readBuildString = [](const char *field) {
        const QJniObject value = QJniObject::getStaticObjectField("android/os/Build", field, "Ljava/lang/String;");
        return value.isValid() ? value.toString() : QString();
    };

    device.insert(QStringLiteral("manufacturer"), readBuildString("MANUFACTURER"));
    device.insert(QStringLiteral("model"), readBuildString("MODEL"));

    const QJniObject release =
            QJniObject::getStaticObjectField("android/os/Build$VERSION", "RELEASE", "Ljava/lang/String;");
    if (release.isValid()) {
        device.insert(QStringLiteral("os_version"), release.toString());
    }
    const jint sdk = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
    device.insert(QStringLiteral("sdk"), static_cast<int>(sdk));

    const QJniObject locale = QJniObject::callStaticObjectMethod("java/util/Locale", "getDefault",
                                                                "()Ljava/util/Locale;");
    if (locale.isValid()) {
        device.insert(QStringLiteral("locale"), locale.callObjectMethod<jstring>("toString").toString());
    }
#endif
    return device;
}

QJsonObject ConnectionReportController::settingsInfo() const
{
    QJsonObject info;
    if (!m_settings) {
        return info;
    }
    info.insert(QStringLiteral("mtu_override"), m_settings->mtuOverride());
    info.insert(QStringLiteral("reconnect_until_cancelled"), m_settings->isReconnectUntilCancelled());
    info.insert(QStringLiteral("post_connect_check"), m_settings->isPostConnectCheckEnabled());
    info.insert(QStringLiteral("experimental"), m_settings->isExperimentalFeaturesEnabled());
    info.insert(QStringLiteral("telemetry"), m_settings->isTelemetryEnabled());
    info.insert(QStringLiteral("kill_switch"), m_settings->isStrictKillSwitchEnabled());
    info.insert(QStringLiteral("app_language"), m_settings->getAppLanguage().name());
    return info;
}

QJsonObject ConnectionReportController::networkInfo() const
{
    QJsonObject network;
#ifdef Q_OS_ANDROID
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return network;
    }

    const QJniObject serviceName = QJniObject::fromString(QStringLiteral("phone"));
    const QJniObject telephony = context.callObjectMethod("getSystemService",
                                                         "(Ljava/lang/String;)Ljava/lang/Object;",
                                                         serviceName.object<jstring>());
    if (telephony.isValid()) {
        // Имя оператора и MCC/MNC доступны без READ_PHONE_STATE; на части прошивок
        // может прийти пустая строка — это нормально, не считаем ошибкой.
        network.insert(QStringLiteral("carrier"),
                       telephony.callObjectMethod<jstring>("getNetworkOperatorName").toString());
        const QString mccMnc = telephony.callObjectMethod<jstring>("getNetworkOperator").toString();
        if (mccMnc.size() >= 4) {
            network.insert(QStringLiteral("mcc"), mccMnc.left(3));
            network.insert(QStringLiteral("mnc"), mccMnc.mid(3));
        }
        network.insert(QStringLiteral("network_country"),
                       telephony.callObjectMethod<jstring>("getNetworkCountryIso").toString());
    }

    // Мобильная сеть или Wi-Fi: ConnectivityManager, разрешение ACCESS_NETWORK_STATE
    // (normal, без запроса у пользователя).
    const QJniObject connectivityName = QJniObject::fromString(QStringLiteral("connectivity"));
    const QJniObject connectivity = context.callObjectMethod("getSystemService",
                                                            "(Ljava/lang/String;)Ljava/lang/Object;",
                                                            connectivityName.object<jstring>());
    if (connectivity.isValid()) {
        const QJniObject activeNetwork = connectivity.callObjectMethod("getActiveNetwork",
                                                                      "()Landroid/net/Network;");
        if (activeNetwork.isValid()) {
            const QJniObject capabilities = connectivity.callObjectMethod(
                    "getNetworkCapabilities", "(Landroid/net/Network;)Landroid/net/NetworkCapabilities;",
                    activeNetwork.object());
            if (capabilities.isValid()) {
                constexpr jint transportCellular = 0; // NetworkCapabilities.TRANSPORT_CELLULAR
                const jboolean mobile = capabilities.callMethod<jboolean>("hasTransport", "(I)Z", transportCellular);
                network.insert(QStringLiteral("mobile"), static_cast<bool>(mobile));
            }
        }
    }
#endif

    // Клиентская часть: без неё в отчёте был только адрес ноды. Публичный IP
    // не отправляем — бэкенд и так видит его из самого запроса (и это честнее,
    // чем то, что скажет клиент); локальный адрес и интерфейс дают понять,
    // через что шёл трафик (Wi-Fi / сотовый / провод).
    //
    // Интерфейсы туннеля (utun/ipsec/ppp) пропускаем: при активном VPN первым
    // обычно всплывает именно адрес utun (10.x), и в отчёт уходил адрес туннеля
    // под видом «сети клиента».
    const auto isTunnelInterface = [](const QString &name) {
        return name.startsWith(QLatin1String("utun"), Qt::CaseInsensitive)
                || name.startsWith(QLatin1String("ipsec"), Qt::CaseInsensitive)
                || name.startsWith(QLatin1String("ppp"), Qt::CaseInsensitive)
                || name.startsWith(QLatin1String("tun"), Qt::CaseInsensitive)
                || name.startsWith(QLatin1String("tap"), Qt::CaseInsensitive);
    };

    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        if (isTunnelInterface(iface.name())) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol || addr.isLoopback()) {
                continue;
            }
            network.insert(QStringLiteral("local_ip"), addr.toString());
            network.insert(QStringLiteral("iface"), iface.humanReadableName());
            network.insert(QStringLiteral("iface_type"),
                           iface.type() == QNetworkInterface::Wifi ? QStringLiteral("wifi")
                           : iface.type() == QNetworkInterface::Ethernet ? QStringLiteral("ethernet")
                                                                         : QStringLiteral("other"));
            break;
        }
        if (network.contains(QStringLiteral("local_ip"))) {
            break;
        }
    }

    // IPv6: у половины «не подключается» виноват сломанный IPv6 у оператора
    bool hasIpv6 = false;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() == QAbstractSocket::IPv6Protocol
                && !addr.isLoopback()
                && !addr.isLinkLocal()) {
                hasIpv6 = true;
                break;
            }
        }
        if (hasIpv6) {
            break;
        }
    }
    network.insert(QStringLiteral("ipv6_available"), hasIpv6);

    // Свободное место: «не подключается» иногда = диск забит логами/кэшем
    const QStorageInfo storage = QStorageInfo::root();
    if (storage.isValid()) {
        network.insert(QStringLiteral("disk_free_mb"), static_cast<int>(storage.bytesAvailable() / (1024 * 1024)));
    }

    // Адрес в туннеле — тот же, что карточка показывает как «Tunnel IP»
    const QString tunnelIp = tunnelClientIp();
    if (!tunnelIp.isEmpty()) {
        network.insert(QStringLiteral("tunnel_ip"), tunnelIp);
    }

    return network;
}

// Адрес клиента внутри туннеля: last_config.client_ip, либо «Address = …»
// из wg-quick INI. Тот же источник, что у карточки (Tunnel IP).
QString ConnectionReportController::tunnelClientIp() const
{
    if (!m_serversModel) {
        return {};
    }
    const QJsonObject serverConfig = m_serversModel->getServerConfig(reportIndex());
    const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();

    const auto fromProtocolObject = [](const QJsonObject &protocolObject) -> QString {
        const QJsonObject lastConfig = QJsonDocument::fromJson(
                protocolObject.value(QStringLiteral("last_config")).toString().toUtf8()).object();

        QString ip = lastConfig.value(config_key::client_ip).toString();
        if (!ip.isEmpty()) {
            return ip;
        }
        ip = protocolObject.value(config_key::client_ip).toString();
        if (!ip.isEmpty()) {
            return ip;
        }

        QString ini = lastConfig.value(QStringLiteral("config")).toString();
        if (ini.isEmpty()) {
            ini = protocolObject.value(QStringLiteral("config")).toString();
        }
        for (const QString &line : ini.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (!trimmed.startsWith(QLatin1String("Address"))) {
                continue;
            }
            const QStringList parts = trimmed.split(QLatin1Char('='));
            if (parts.size() < 2) {
                continue;
            }
            // "10.166.13.138/32, fd00::1/128" — берём первый IPv4
            for (const QString &chunk : parts.at(1).split(QLatin1Char(','))) {
                QString candidate = chunk.trimmed();
                candidate = candidate.section(QLatin1Char('/'), 0, 0);
                if (candidate.contains(QLatin1Char('.'))) {
                    return candidate;
                }
            }
        }
        return {};
    };

    for (const QJsonValue &containerValue : containers) {
        const QJsonObject container = containerValue.toObject();
        const QString ip = fromProtocolObject(container);
        if (!ip.isEmpty()) {
            return ip;
        }
        for (auto it = container.begin(); it != container.end(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            const QString nested = fromProtocolObject(it.value().toObject());
            if (!nested.isEmpty()) {
                return nested;
            }
        }
    }
    return {};
}

// MTU, который отдаёт сервер для текущей строки (last_config.mtu или
// верхнеуровневый mtu в объекте протокола).
int ConnectionReportController::serverMtuValue() const
{
    if (!m_serversModel) {
        return 0;
    }
    const QJsonObject serverConfig = m_serversModel->getServerConfig(reportIndex());
    const QJsonArray containers = serverConfig.value(QStringLiteral("containers")).toArray();

    const auto mtuFromObject = [](const QJsonObject &protocolObject) -> int {
        const QJsonObject lastConfig = QJsonDocument::fromJson(
                protocolObject.value(QStringLiteral("last_config")).toString().toUtf8()).object();
        const QJsonValue fromLast = lastConfig.value(config_key::mtu);
        if (!fromLast.isUndefined() && !fromLast.isNull()) {
            const int value = fromLast.isString() ? fromLast.toString().toInt() : fromLast.toInt();
            if (value > 0) {
                return value;
            }
        }
        const QJsonValue direct = protocolObject.value(config_key::mtu);
        if (!direct.isUndefined() && !direct.isNull()) {
            const int value = direct.isString() ? direct.toString().toInt() : direct.toInt();
            if (value > 0) {
                return value;
            }
        }
        return 0;
    };

    for (const QJsonValue &containerValue : containers) {
        const QJsonObject container = containerValue.toObject();
        const int mtu = mtuFromObject(container);
        if (mtu > 0) {
            return mtu;
        }
        for (auto it = container.begin(); it != container.end(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            const int nested = mtuFromObject(it.value().toObject());
            if (nested > 0) {
                return nested;
            }
        }
    }
    return 0;
}

// Что за конфиг юзер реально получил от API: хеш, ревизия, протокол.
// Без этого невозможно отличить «у юзера старый конфиг» от «нода лежит».
QJsonObject ConnectionReportController::configInfo() const
{
    QJsonObject info;
    if (!m_serversModel) {
        return info;
    }

    const int index = reportIndex();
    const QJsonObject serverConfig = m_serversModel->getServerConfig(index);
    if (serverConfig.isEmpty()) {
        return info;
    }

    const QByteArray canonical = QJsonDocument(serverConfig).toJson(QJsonDocument::Compact);
    const QByteArray digest = QCryptographicHash::hash(canonical, QCryptographicHash::Sha256);
    info.insert(QStringLiteral("hash"), QString::fromLatin1(digest.toHex().left(16)));
    info.insert(QStringLiteral("size"), canonical.size());
    const QJsonObject apiConfig = serverConfig.value(QStringLiteral("api_config")).toObject();
    const QString rev = apiConfig.value(QStringLiteral("config_rev")).toString();
    if (!rev.isEmpty()) {
        info.insert(QStringLiteral("rev"), rev);
    }
    const QString apiProtocol = apiConfig.value(QStringLiteral("service_protocol")).toString();
    if (!apiProtocol.isEmpty()) {
        info.insert(QStringLiteral("api_protocol"), apiProtocol);
    }
    const QJsonValue apiProtocolVersion = apiConfig.value(QStringLiteral("service_protocol_version"));
    if (!apiProtocolVersion.isUndefined() && !apiProtocolVersion.isNull()) {
        info.insert(QStringLiteral("api_protocol_version"),
                    apiProtocolVersion.isString() ? apiProtocolVersion.toString()
                                                  : QString::number(apiProtocolVersion.toInt()));
    }

    return info;
}

// Короткий хеш конфига строки — для скана, где строк много.
QString ConnectionReportController::configHashForIndex(int index) const
{
    if (!m_serversModel || index < 0) {
        return {};
    }
    const QJsonObject serverConfig = m_serversModel->getServerConfig(index);
    if (serverConfig.isEmpty()) {
        return {};
    }
    const QByteArray canonical = QJsonDocument(serverConfig).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex().left(16));
}
// ---------------------------------------------------------------------------

QString ConnectionReportController::lastAutoReportAt() const
{
    if (!m_settings) {
        return {};
    }
    return m_settings->lastAutoReportAt();
}

void ConnectionReportController::handleConnectionState(int state, int errorCode, bool userRequested)
{
    if (state == static_cast<int>(Vpn::ConnectionState::Connecting)
        || state == static_cast<int>(Vpn::ConnectionState::Reconnecting)
        || state == static_cast<int>(Vpn::ConnectionState::Preparing)) {
        m_connectAttemptActive = true;
        return;
    }

    if (state == static_cast<int>(Vpn::ConnectionState::Connected)) {
        m_connectAttemptActive = false;
        return;
    }

    const bool hardError = (state == static_cast<int>(Vpn::ConnectionState::Error));
    const bool failedDisconnect = (state == static_cast<int>(Vpn::ConnectionState::Disconnected)
                                   && m_connectAttemptActive
                                   && errorCode != 0);
    // Туннель был поднят и отвалился сам (не наша кнопка, не ретрай-цикл):
    // самый частый жалобный кейс — «было подключено, интернет пропал».
    const bool droppedLiveSession = (state == static_cast<int>(Vpn::ConnectionState::Disconnected)
                                     && !userRequested
                                     && !m_connectAttemptActive
                                     && m_hadLiveSession
                                     && m_liveSessionUptimeSec >= 5);
    if (state == static_cast<int>(Vpn::ConnectionState::Disconnected)) {
        m_hadLiveSession = false; // один разрыв — один отчёт
    }
    if (state == static_cast<int>(Vpn::ConnectionState::Disconnected)) {
        m_connectAttemptActive = false;
    }

    if (!hardError && !failedDisconnect && !droppedLiveSession) {
        return; // обычное «выключил VPN» — отчёт не нужен
    }
    if (!m_settings || !m_settings->isTelemetryEnabled()) {
        return;
    }
    if (m_busy || m_scanning) {
        return; // идёт ручная проба/скан
    }

    // Не чаще одного отчёта в 10 минут: реконнект-цикл иначе завалит бэкенд.
    constexpr int kMinIntervalSec = 600;
    const QString last = m_settings->lastAutoReportAt();
    if (!last.isEmpty()) {
        const QDateTime lastAt = QDateTime::fromString(last, Qt::ISODate);
        if (lastAt.isValid() && lastAt.secsTo(QDateTime::currentDateTimeUtc()) < kMinIntervalSec) {
            return;
        }
    }

    qDebug().noquote() << "[TELEMETRY] connection failed (state" << state << "error" << errorCode
                       << "), collecting and sending auto report";
    m_settings->setLastAutoReportAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    emit lastAutoReportAtChanged();
    collect();
    // Собирали, но не отправляли: auto-отчёт молча лежал в памяти и умирал
    // при закрытии приложения. Теперь уходит сразу (тот же send, что у кнопки).
    send();
}

void ConnectionReportController::setConnected(bool connected)
{
    // Пробы на живом туннеле бессмысленны: трафик идёт внутрь VPN, и вердикт
    // «адрес доступен/нет» получается про туннель, а не про сеть юзера.
    m_tunnelActive = connected;

    // Настройка выключена — не собираем ничего, даже локально.
    if (!m_settings || !m_settings->isTelemetryEnabled()) {
        if (m_sessionActive) {
            m_sessionActive = false;
            if (m_sessionTimer) {
                m_sessionTimer->stop();
            }
        }
        return;
    }

    if (connected) {
        if (m_sessionActive) {
            return;
        }
        m_sessionActive = true;
        m_hadLiveSession = true;
        m_liveSessionUptimeSec = 0;
        m_sessionConnectedAt = QDateTime::currentDateTimeUtc();
        m_sessionSamples.clear();
        m_disconnectInfo = QJsonObject();

        if (!m_sessionTimer) {
            m_sessionTimer = new QTimer(this);
            m_sessionTimer->setInterval(60 * 1000);
            connect(m_sessionTimer, &QTimer::timeout, this, &ConnectionReportController::sampleSession);
        }
        m_sessionTimer->start();
        sampleSession();
        return;
    }

    if (!m_sessionActive) {
        return;
    }
    if (m_sessionActive && m_sessionConnectedAt.isValid()) {
        m_liveSessionUptimeSec = m_sessionConnectedAt.secsTo(QDateTime::currentDateTimeUtc());
        if (m_settings) {
            // prev_session: нужна в следующем отчёте — сколько прожила сессия и как умерла
            QJsonObject prev;
            prev.insert(QStringLiteral("connected_at"), m_sessionConnectedAt.toString(Qt::ISODate));
            prev.insert(QStringLiteral("uptime_s"), static_cast<double>(m_liveSessionUptimeSec));
            prev.insert(QStringLiteral("endpoint"), m_settings->lastConnectedEndpoint());
            prev.insert(QStringLiteral("reason"), QStringLiteral("disconnected"));
            m_settings->setPrevSession(prev);
        }
    }
    m_sessionActive = false;
    if (m_sessionTimer) {
        m_sessionTimer->stop();
    }

    QJsonObject disconnect;
    disconnect.insert(QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_disconnectInfo = disconnect;
}

void ConnectionReportController::sampleSession()
{
    const QString host = targetHost();
    if (host.isEmpty()) {
        return;
    }

    auto appendSample = [this](bool ok, int ms) {
        m_sessionSamples.append(ok ? ms : -1);
        while (m_sessionSamples.size() > 60) {
            m_sessionSamples.removeFirst();
        }
    };

#if defined(Q_OS_IOS)
    // iOS: ни ping, ни QProcess недоступны — RTT меряем TCP-хендшейком до порта сервера.
    const QList<int> ports = targetPorts();
    if (ports.isEmpty()) {
        return;
    }
    auto *tcpSocket = new QTcpSocket(this);
    auto *tcpTimer = new QTimer(this);
    auto *tcpElapsed = new QElapsedTimer();
    tcpElapsed->start();
    tcpTimer->setSingleShot(true);

    auto tcpDone = [appendSample, tcpSocket, tcpTimer, tcpElapsed](bool ok, int ms) {
        appendSample(ok, ms);
        if (tcpTimer->isActive()) {
            tcpTimer->stop();
        }
        tcpTimer->deleteLater();
        tcpSocket->deleteLater();
        delete tcpElapsed;
    };
    connect(tcpSocket, &QTcpSocket::connected, this, [tcpDone, tcpElapsed]() {
        tcpDone(true, static_cast<int>(tcpElapsed->elapsed()));
    });
    connect(tcpSocket, &QTcpSocket::errorOccurred, this,
            [tcpDone](QAbstractSocket::SocketError) { tcpDone(false, -1); });
    connect(tcpTimer, &QTimer::timeout, this, [tcpDone]() { tcpDone(false, -1); });

    tcpTimer->start(kTcpTimeoutMs);
    tcpSocket->connectToHost(host, static_cast<quint16>(ports.first()));
#else
    auto *process = new QProcess(this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);

    auto done = [appendSample, process, timer](bool ok, int ms) {
        if (timer->isActive()) {
            timer->stop();
        }
        timer->deleteLater();
        process->deleteLater();
        appendSample(ok, ms);
    };

    connect(process, &QProcess::finished, this, [process, done](int, QProcess::ExitStatus status) {
        const QString out = QString::fromLocal8Bit(process->readAllStandardOutput());
        const bool ok = status == QProcess::NormalExit
                        && out.contains(QStringLiteral("ttl="), Qt::CaseInsensitive);
        int ms = -1;
        if (ok) {
            // "time=12.3 ms" / "время=12мс" / "time<1ms"
            static const QRegularExpression re(QStringLiteral("([0-9]+(?:[.,][0-9]+)?)\\s*(?:ms|мс)"),
                                               QRegularExpression::CaseInsensitiveOption);
            const auto match = re.match(out);
            if (match.hasMatch()) {
                ms = qRound(match.captured(1).replace(',', '.').toDouble());
            } else {
                ms = 0;
            }
        }
        done(ok, ms);
    });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError) { done(false, -1); });
    connect(timer, &QTimer::timeout, this, [process, done]() {
        process->kill();
        done(false, -1);
    });

    timer->start(kIcmpWaitMs + 2000);
    process->start(QStringLiteral("ping"), pingArgs(host));
#endif // Q_OS_IOS
}

QJsonObject ConnectionReportController::buildSession() const
{
    QJsonObject session;
    session.insert(QStringLiteral("connected"), m_sessionActive);

    if (m_sessionConnectedAt.isValid()) {
        session.insert(QStringLiteral("connected_at"), m_sessionConnectedAt.toString(Qt::ISODate));
        // если сессия уже закрыта — считаем до дисконнекта, а не до момента отчёта
        QDateTime endAt = QDateTime::currentDateTimeUtc();
        const QString disconnectAt = m_disconnectInfo.value(QStringLiteral("at")).toString();
        if (!disconnectAt.isEmpty()) {
            const QDateTime parsed = QDateTime::fromString(disconnectAt, Qt::ISODate);
            if (parsed.isValid()) {
                endAt = parsed;
            }
        }
        const qint64 uptime = m_sessionConnectedAt.secsTo(endAt);
        session.insert(QStringLiteral("uptime_s"), static_cast<double>(uptime));
    }

    QList<int> good;
    int lost = 0;
    for (int sample : m_sessionSamples) {
        if (sample < 0) {
            ++lost;
        } else {
            good.append(sample);
        }
    }

    // 1 замер = 100% потерь — это ложная картина; считаем от 3 замеров
    if (m_sessionSamples.size() >= 3) {
        session.insert(QStringLiteral("loss_pct"),
                       static_cast<double>(lost) * 100.0 / static_cast<double>(m_sessionSamples.size()));
    }
    session.insert(QStringLiteral("samples_ok"), static_cast<int>(good.size()));
    session.insert(QStringLiteral("samples_failed"), lost);
    if (!good.isEmpty()) {
        std::sort(good.begin(), good.end());
        int sum = 0;
        for (int v : good) {
            sum += v;
        }
        session.insert(QStringLiteral("rtt_ms_avg"), sum / good.size());
        const int p95Index = qMin(good.size() - 1, static_cast<int>(good.size() * 0.95));
        session.insert(QStringLiteral("rtt_ms_p95"), good.at(p95Index));
    }

    // только успешные замеры: -1 в массиве читается как «задержка -1 мс»
    QJsonArray samples;
    for (int sample : m_sessionSamples) {
        if (sample >= 0) {
            samples.append(sample);
        }
    }
    session.insert(QStringLiteral("samples"), samples);

    if (!m_disconnectInfo.isEmpty()) {
        session.insert(QStringLiteral("disconnect"), m_disconnectInfo);
    }
    if (m_settings) {
        const QJsonObject noData = QJsonDocument::fromJson(m_settings->lastNoDataInfo().toUtf8()).object();
        if (!noData.isEmpty()) {
            session.insert(QStringLiteral("last_no_data"), noData);
        }
    }
    return session;
}

void ConnectionReportController::setScanning(bool scanning)
{
    if (m_scanning == scanning) {
        return;
    }
    m_scanning = scanning;
    emit scanningChanged();
}

void ConnectionReportController::setProgress(int done, int total)
{
    m_progressDone = done;
    m_progressTotal = total;
    emit progressChanged();
}

void ConnectionReportController::setSendState(const QString &state, const QString &message)
{
    m_sendState = state;
    m_sendMessage = message;
    emit sendStateChanged();
}
