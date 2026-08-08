#ifndef HEALTHCHECKCONTROLLER_H
#define HEALTHCHECKCONTROLLER_H

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
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
        QString key;
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
        QString key;
        QString host;
        quint16 port;
        QString clientPrivKey;
        QString serverPubKey;
        QString psk;
        QString junkParamsJson;
        int attempts = 1; // lives on the target, not the watcher — retries requeue the target
    };

    void startNext();
    void finishSocket(QTcpSocket *socket, int latencyMs);

    void startNextWg();
    void finishWatcher(QFutureWatcher<int> *watcher);

    QSharedPointer<ServersModel> m_serversModel;

    QList<Target> m_queue;
    QHash<QTcpSocket *, Target> m_socketTargets;
    QHash<QTcpSocket *, QElapsedTimer> m_timers;
    QHash<QTcpSocket *, int> m_attempts;
    QSet<QString> m_seenTcpEndpoints;

    QList<WgTarget> m_wgQueue;
    QHash<QFutureWatcher<int> *, WgTarget> m_wgWatchers;
    QSet<QString> m_seenWgEndpoints;

    qint64 m_lastRunMsecs = 0;

    static constexpr int kMaxParallel = 8;
    static constexpr int kWgMaxParallel = 3;
    static constexpr int kTimeoutMs = 2500;
    static constexpr int kHttpsTimeoutMs = 4000;
    static constexpr int kWgTimeoutMs = 4000;
    static constexpr qint64 kThrottleMs = 30000;
};

#endif // HEALTHCHECKCONTROLLER_H
