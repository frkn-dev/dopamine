#ifndef HEALTHCHECKCONTROLLER_H
#define HEALTHCHECKCONTROLLER_H

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QSharedPointer>

#include "ui/models/servers_model.h"

// Server health probe.
//
//   * AWG/WG rows keep the handshake probe: a real handshake on worker threads
//     (WgProbeRTT via libwg-go on Apple, core/wgHandshakeProbe on Android and
//     Windows). That is the honest "is there a tunnel" signal for them.
//   * Proxy protocols (vless, hysteria2, any client_config card) cannot be
//     handshake-probed from the client, so they get a server ping instead:
//     unprivileged ICMP echo (SOCK_DGRAM + IPPROTO_ICMP), and a TCP-connect RTT
//     to port 443 when the socket or the reply is unavailable. The address is
//     the node's, the same one for every protocol of the pool.
class HealthCheckController : public QObject
{
    Q_OBJECT
public:
    explicit HealthCheckController(const QSharedPointer<ServersModel> &serversModel, QObject *parent = nullptr);

    Q_INVOKABLE void startProbe(bool force = false);

    // Пока туннель поднят, пробы бессмысленны: трафик идёт внутрь VPN, и вердикт
    // «нода недоступна» получается ложным (ровно это ловил юзер: на туннеле
    // 80% нод «no handshake», после отключения — доступны).
    void setTunnelActive(bool active) { m_tunnelActive = active; }
    Q_INVOKABLE void stopProbe();

    // диагностика: убедиться, что handshake-проба меряет именно handshake
    Q_INVOKABLE void runProbeSelfTest();

    // true while a probe run has unfinished targets; probingFinished fires once
    // when the last target of the run resolves (also on an external stopProbe)
    bool isProbing() const { return m_probeActive; }
    bool m_tunnelActive = false;
    Q_PROPERTY(bool probing READ isProbing NOTIFY probingChanged)
    qint64 lastProbeMsecs() const { return m_lastRunMsecs; }

signals:
    void probingFinished();
    void probingChanged();

private:
    struct WgTarget
    {
        QList<int> rows; // server indices sharing this endpoint
        QList<QString> cardIds; // protocol card each row showed when the run was queued
        QString host;
        quint16 port;
        QString clientPrivKey;
        QString serverPubKey;
        QString psk;
        QString junkParamsJson;
        int attempts = 1; // lives on the target, not the watcher — retries requeue the target
    };

    // One ping per node address. `proto` is only carried for the log line: the
    // address answers for every protocol of the pool.
    struct PingTarget
    {
        QList<int> rows;
        QList<QString> cardIds; // protocol card each row showed when the run was queued
        QString host;
        QString proto;
        // Dial port of the row and whether the protocol speaks UDP only
        // (hysteria2). A TCP probe to a QUIC port can never answer: those rows
        // get ICMP or nothing, instead of a misleading "offline".
        quint16 port = 443;
        bool udpOnly = false;
    };

    struct PingResult
    {
        int rttMs = -1;
        bool viaIcmp = false;
    };

    void startNextWg();
    void finishWatcher(QFutureWatcher<int> *watcher);

    void startNextPing();
    void finishPingWatcher(QFutureWatcher<PingResult> *watcher);

    // emits probingFinished once every queue and in-flight probe of the run is done
    void maybeFinishProbe();

    QSharedPointer<ServersModel> m_serversModel;

    QList<PingTarget> m_pingQueue;
    QHash<QFutureWatcher<PingResult> *, PingTarget> m_pingWatchers;
    QHash<QString, int> m_seenPingEndpoints; // address -> position in m_pingQueue

    QList<WgTarget> m_wgQueue;
    QHash<QFutureWatcher<int> *, WgTarget> m_wgWatchers;
    QHash<QString, int> m_seenWgEndpoints; // endpoint -> position in m_wgQueue

    qint64 m_lastRunMsecs = 0;
    bool m_probeActive = false;
    // startProbe() runs stopProbe() and clearHealthResults() before it fills the
    // queues, and both can re-enter it from QML; a nested run would leave the
    // outer loop indexing a drained m_queue (qBadAlloc crash on iOS).
    bool m_startingProbe = false;

    static constexpr int kMaxParallel = 8;
    static constexpr int kWgMaxParallel = 3;
    static constexpr int kPingIcmpTimeoutMs = 1500;
    static constexpr int kPingTcpTimeoutMs = 2500;
    static constexpr quint16 kPingTcpPort = 443;
    static constexpr int kWgTimeoutMs = 4000;
    static constexpr qint64 kThrottleMs = 30000;
};

#endif // HEALTHCHECKCONTROLLER_H
