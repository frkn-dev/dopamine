#ifndef SERVERTESTCONTROLLER_H
#define SERVERTESTCONTROLLER_H

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QTimer>
#include <QString>

#include <functional>

#include "ui/models/servers_model.h"

class QNetworkReply;
class PeersImportController;

// "Test servers" pass over FRKN pool nodes: a real AWG handshake per node
// address (Noise IK over UDP — the path the tunnel actually uses) plus a
// throughput sample from the node's own bench file (the same endpoints
// status.frkn.org measures, so the numbers stay comparable to the web page).
//
// The pass tests one pool over one protocol. A proxy protocol (hysteria2,
// vless, anything a `client_config` card builds) has no Noise handshake to
// measure, so it is measured by its download alone — no handshake verdict is
// invented for it.
//
// Scope is rows with a non-empty displayInfo.pool; premium slots carry neither a
// pool nor a bench endpoint, so they are skipped instead of being reported as
// "no answer".
class ServerTestController : public QObject
{
    Q_OBJECT
public:
    explicit ServerTestController(const QSharedPointer<ServersModel> &serversModel, std::shared_ptr<Settings> settings,
                                  QObject *parent = nullptr);

    // Full pass: every handshake sample first, throughput after. A download in
    // flight inflates the RTT of anything measured next to it, so the two passes
    // never overlap.
    Q_INVOKABLE void runPoolTest(bool includeSpeed = true);
    // Start as soon as no tunnel is up: the app reconnects on launch, so a pass
    // pressed while connected would otherwise be dropped without any feedback.
    Q_INVOKABLE void runWhenTunnelOff(bool includeSpeed = true);
    Q_INVOKABLE void stop();
    // the whole run as one JSON blob: pasted into a chat it says more than a
    // screenshot of ten rows
    Q_INVOKABLE QString reportJson() const;
    bool isRunning() const { return m_running; }

    // Rows for the UI before (and during) a pass: one entry per probed address.
    Q_INVOKABLE void prepareTargets();
    // Same idea as the server list: test one pool and/or one protocol at a time.
    // Pools/protocols come from the subscription rows, not from the api.
    Q_INVOKABLE void setPoolFilter(const QString &pool);
    Q_INVOKABLE void setProtocolFilter(const QString &protocol);
    // The chooser's pick: narrow the pass to `protocol` and, when that protocol
    // has no peer config yet, provision it (`POST /v1/peers/prepare`) before the
    // test. A pass that was running restarts on the new protocol once its config
    // is there, because "pick a protocol" means "measure this one".
    Q_INVOKABLE void selectProtocol(const QString &protocol);
    QStringList availablePools() const { return m_allPools; }
    QStringList availableProtocols() const { return m_allProtocols; }
    QString poolFilter() const { return m_poolFilter; }
    QString protocolFilter() const { return m_protocolFilter; }
    // True when the selected pool serves more than one protocol. The page hides
    // the chooser otherwise: a single choice is not a choice.
    bool hasProtocolChoice() const { return m_allProtocols.size() > 1; }
    // Provisioning entry point (see selectProtocol): optional, so the controller
    // still builds where the peers plane does not exist.
    void setPeersImportController(PeersImportController *peers);
    // Subscription selection this pass is limited to, mirrored from the servers
    // list ("" = all subscriptions). Read by the page to say what is tested.
    QString subscriptionFilter() const { return m_subscriptionFilter; }
    // { pool, label, hostname, address, port, country, rtt, jitter, loss,
    //   samples, mbps, speedError, done }
    QVariantList results() const;
    int measuredCount() const;
    int targetCount() const;
    QString status() const { return m_status; }
    QString buildTag() const { return m_buildTag; }

    // A handshake probe roams the peer endpoint on the server side, so a pass
    // must not run while a tunnel is up. The caller keeps this flag current.
    void setTunnelActive(bool active);

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int measuredCount READ measuredCount NOTIFY resultsChanged)
    Q_PROPERTY(int targetCount READ targetCount NOTIFY resultsChanged)
    Q_PROPERTY(QString status READ status NOTIFY resultsChanged)
    Q_PROPERTY(QString buildTag READ buildTag CONSTANT)
    Q_PROPERTY(QStringList availablePools READ availablePools NOTIFY filtersChanged)
    Q_PROPERTY(QStringList availableProtocols READ availableProtocols NOTIFY filtersChanged)
    Q_PROPERTY(bool hasProtocolChoice READ hasProtocolChoice NOTIFY filtersChanged)
    Q_PROPERTY(QString poolFilter READ poolFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString protocolFilter READ protocolFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString subscriptionFilter READ subscriptionFilter NOTIFY filtersChanged)

signals:
    void runningChanged();
    void resultsChanged();
    void filtersChanged();
    void finished(const QString &report);

private:
    enum SpeedStage
    {
        SpeedWarm,
        SpeedSmall,
        SpeedBig
    };

    // Throughput is a property of the node (one bench host per node hostname),
    // not of a single ip: a node listing three addresses must not be downloaded
    // three times, and all three rows must show the same number.
    struct SpeedResult
    {
        double mbps = 0.0;
        QList<double> speedSamples; // one entry per round, averaged into mbps
        int speedRound = 0;
        qint64 bytes = 0;
        qint64 ms = 0;
        QString error;
    };

    struct Target
    {
        int row = -1;
        QString pool;
        QString country;
        QString protocol; // backend protocol tag, e.g. amneziawgmobile
        QString label;
        QString listName; // the same row's name in the server list
        // Which subscription the row belongs to: two subscriptions can sell the
        // same pool, so results and the report have to say where a server came
        // from when the pass covers all of them.
        QString subscriptionId;
        QString subscriptionLabel; // short tail of the id, as the server list shows it
        QString hostname; // node hostname — the default bench origin derives from it
        QString benchBase; // https://host:port used for the speed sample
        bool onlineKnown = false; // api reported a status for this node
        bool online = true;
        QString address;  // the ip actually probed (node_ips entry)
        // the api can publish a stale ip for a node (EE/PL did exactly that):
        // sibling addresses of the same /24 from the subscription are tried when
        // the published one does not answer
        QStringList fallbackAddresses;
        quint16 port = 0;
        QString clientPrivKey;
        QString serverPubKey;
        QString psk;
        QString junkJson;
        // A pooled row whose tunnel was never built has no keys: it cannot be
        // handshake-probed, but its bench file is public, so it still gets a
        // download sample instead of being dropped from the list.
        bool benchOnly = false;
        // The protocol is a proxy (hysteria2 / vless / any `client_config` card):
        // there is no Noise handshake to measure, so the download is the whole
        // test and no handshake verdict is shown for the row.
        bool proxyOnly = false;
        // The variant this row is tested through has no peer config yet: the app
        // provisions it (`POST /v1/peers/prepare`) before the handshake, because
        // a node answers only a peer it knows.
        bool needsProvision = false;
        QString provisionSubscription; // subscription id of the row needing prepare
        QString provisionPoolKey;      // "<shard>:<pool>" handle for prepare

        QList<int> rtts; // -1 = no answer
        int okSamples = 0;
        int medianRtt = -1;
        int jitterMs = 0;
        double mbps = 0.0;
        QList<double> speedSamples; // one entry per round, averaged into mbps
        int speedRound = 0;
        qint64 speedBytes = 0;
        qint64 speedMs = 0;
        QString speedError;
        QString speedErrorCode; // dns / refused / timeout / tls / 404 / 403 / error
    };

    void collectTargets();
    void collectTargetsPass();
    // Provisioning gate: when a target of the selected protocol has no peer
    // config yet, ask the peers controller to prepare its pool and return true —
    // the caller resumes in onPoolProvisioned(). `thenRun`: continue/start the
    // pass once the config is stored.
    bool requestProvision(bool thenRun);
    void onPoolProvisioned(const QString &subscriptionId, const QString &poolKey, bool ok, const QString &error);
    // Handshake queue + speed pass after the target list is final.
    void beginMeasurement(bool includeSpeed);
    // Short tail of a subscription id, the same label the server list shows
    // («Subscription 1a2b3c4d»), so the two screens name a subscription alike.
    static QString subscriptionTail(const QString &subscriptionId);
    void scheduleRefresh();
    void reloadTargets();
    // api.frkn.org/status carries the bench base URL per node: the zone no longer
    // names benches after the node hostname, so nothing can be derived locally.
    void fetchBenchMap(std::function<void()> then);
    void pump();
    void startHandshakes();
    struct ProbeResult
    {
        QString address;
        QList<int> rtts;
    };
    void onHandshakeFinished(int index, const ProbeResult &result);
    // drops the in-flight tokens: callbacks of a pass whose target list was
    // rebuilt or emptied must not index m_targets any more
    void invalidateProbes();

    void measureNextSpeed();
    void requestBench(int index, const char *path, int timeoutMs);
    void onBenchFinished();
    void advanceSpeedPass();

    void finishPass();
    QString report() const;

    QSharedPointer<ServersModel> m_serversModel;
    std::shared_ptr<Settings> m_settings;
    // Peers plane, optional: only provisioning needs it.
    PeersImportController *m_peers = nullptr;
    bool m_provisionPending = false;
    // One prepare per page/protocol selection: a pool the api refuses would
    // otherwise be asked again on every recollect.
    bool m_provisionTried = false;
    bool m_runAfterProvision = false;
    QList<Target> m_targets;
    QTimer m_refreshTimer;
    bool m_speedByIp = false;
    QHash<int, quint64> m_probeToken; // handshake index -> in-flight token
    quint64 m_probeSeq = 0;
    QList<int> m_handshakeQueue;
    QHash<QString, SpeedResult> m_speedByNode; // node label -> measured throughput
    QHash<QString, QString> m_benchUrls;       // node address -> bench base URL ("" = no bench)
    QHash<QString, bool> m_nodeOnline;         // node address -> api status Online
    QHash<QString, QString> m_nodeLabels;      // node address -> /status label (same name the web page shows)
    QHash<QString, QString> m_nodePools;       // node address -> pool title as /status spells it
    QHash<QString, QString> m_nodeHostnames;   // node address -> node hostname as /status spells it
    bool m_benchMapFetched = false;
    QStringList m_allPools;
    QStringList m_allProtocols;
    QString m_poolFilter;
    QString m_protocolFilter;
    // Subscription the pass is limited to ("" = every subscription the account
    // has). Mirrored from the servers list, never set by this screen.
    QString m_subscriptionFilter;
    QNetworkAccessManager m_network;
    QNetworkReply *m_speedReply = nullptr;
    QElapsedTimer m_speedTimer;

    int m_handshakeInFlight = 0;
    int m_speedIndex = 0;
    SpeedStage m_speedStage = SpeedWarm;
    qint64 m_speedRx = 0;
    bool m_running = false;
    bool m_includeSpeed = true;
    QString m_status; // why the pass ended, shown in the UI
    bool m_pendingRun = false;
    bool m_pendingSpeed = true;
    bool m_tunnelActive = false;
    QString m_userAgent;
    QString m_buildTag;
};

#endif // SERVERTESTCONTROLLER_H
