#ifndef HEALTHCHECKCONTROLLER_H
#define HEALTHCHECKCONTROLLER_H

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QTcpSocket>

#include "ui/models/servers_model.h"

// Server health probe (see frkn-docs/server-healthcheck-research.md):
// TCP connect + RTT for TCP-based servers (VLESS). WG/AWG servers are
// probed with a real handshake via WgProbeRTT (libwg-go) on worker threads.
// Hysteria2 stays n/a until its handshake probe lands.
class HealthCheckController : public QObject
{
    Q_OBJECT
public:
    explicit HealthCheckController(const QSharedPointer<ServersModel> &serversModel, QObject *parent = nullptr);

    Q_INVOKABLE void startProbe(bool force = false);
    Q_INVOKABLE void stopProbe();

private:
    struct Target
    {
        QList<int> rows; // server indices sharing this endpoint
        QString host;
        quint16 port;

        // xhttp+tls (CDN) servers get a real HTTPS probe instead of a bare TCP
        // connect: the CDN edge is almost always up, so TCP says nothing about
        // the origin behind it
        QString sni;
        QString path;
        bool httpsProbe = false;
    };

    struct WgTarget
    {
        QList<int> rows; // server indices sharing this endpoint
        QString host;
        quint16 port;
        QString clientPrivKey;
        QString serverPubKey;
        QString psk;
        QString junkParamsJson;
        int attempts = 1; // lives on the target, not the watcher — retries requeue the target
    };

    struct H2Target
    {
        QList<int> rows; // server indices sharing this endpoint
        QString host;
        quint16 port;
        QJsonObject outbound; // legacy hysteria2 outbound from the stored config
    };

    void startNext();
    void finishSocket(QTcpSocket *socket, int latencyMs);

    void startNextWg();
    void finishWatcher(QFutureWatcher<int> *watcher);

    // hysteria2: full-path probe via LibXrayPing (real handshake+auth+HTTP through
    // the server) — a bare QUIC version-negotiation probe is unreliable (some live
    // servers ignore it). Runs strictly one at a time: libxray holds global state.
    void startNextH2();
    void finishH2Watcher(QFutureWatcher<int> *watcher);

    QSharedPointer<ServersModel> m_serversModel;

    QList<Target> m_queue;
    QHash<QTcpSocket *, Target> m_socketTargets;
    QHash<QTcpSocket *, QElapsedTimer> m_timers;
    QHash<QTcpSocket *, int> m_attempts;
    QHash<QString, int> m_seenTcpEndpoints; // endpoint -> position in m_queue

    QList<WgTarget> m_wgQueue;
    QHash<QFutureWatcher<int> *, WgTarget> m_wgWatchers;
    QHash<QString, int> m_seenWgEndpoints; // endpoint -> position in m_wgQueue

    QList<H2Target> m_h2Queue;
    QHash<QFutureWatcher<int> *, H2Target> m_h2Watchers;
    QHash<QString, int> m_seenH2Endpoints; // endpoint -> position in m_h2Queue

    qint64 m_lastRunMsecs = 0;

    static constexpr int kMaxParallel = 8;
    static constexpr int kWgMaxParallel = 3;
    static constexpr int kTimeoutMs = 2500;
    static constexpr int kHttpsTimeoutMs = 4000;
    static constexpr int kWgTimeoutMs = 4000;
    static constexpr int kH2TimeoutSec = 6;
    static constexpr qint64 kThrottleMs = 30000;
};

#endif // HEALTHCHECKCONTROLLER_H
