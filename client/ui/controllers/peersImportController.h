#ifndef PEERSIMPORTCONTROLLER_H
#define PEERSIMPORTCONTROLLER_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <memory>

class ServersModel;
class Settings;

namespace amnezia::peers
{
struct PoolCard;
} // namespace amnezia::peers

// Peers plane → server list.
//
// One row per pool: the api hands out one card per protocol
// (`<shard>:<pool>:<proto>`), and the list shows a single row per (env, pool) —
// the shard id is the env — with the protocol as a property of that row. The
// row is shaped exactly like a gateway config row, so the list, the flags, the
// pool grouping and the whole connect path keep working unchanged; the concrete
// card of the selected protocol stays in `peerCardId` (that is what
// POST /v1/peers receives) while every protocol of the pool travels in
// `peerVariants`, so the selector can switch the row in place.
//
// The difference from v1 is where the config comes from: the api hands out an
// address and a card, the device builds the tunnel itself (PeerConfigBuilder)
// and keeps the result in the row — that cached config is what allows the next
// connect to happen without the api.
//
// Nothing here blocks the UI thread: the network work runs in QtConcurrent and
// the model is touched back on the GUI thread.
class PeersImportController : public QObject
{
    Q_OBJECT

public:
    PeersImportController(QSharedPointer<ServersModel> serversModel, std::shared_ptr<Settings> settings,
                          QObject *parent = nullptr);

    // Rebuild the peer rows of `subscriptionId` from `POST /v1/locations`.
    // Rows whose card the api no longer offers are dropped; the others keep
    // their cached tunnel.
    //
    // `provision` runs one batched `POST /v1/peers/prepare` after the rows are
    // in place, so an imported subscription has a dialable config per pool
    // before the first connect. It is off for the startup/periodic reload: the
    // owner does not want api work on every app open, and an existing row
    // refreshes its own pool on connect (see ensureTunnelForRow).
    Q_INVOKABLE void refreshCards(const QString &subscriptionId, bool provision = false);

    // True when the last `refreshCards` of this subscription returned at least
    // one v2 card. The subscription is then served on the v2 plane and the
    // legacy per-protocol fetch must not run for it (see
    // ApiConfigsController::setLegacyFetchAllowed). An empty answer clears the
    // mark, so a subscription that stops serving cards falls back to v1.
    bool hasV2Cards(const QString &subscriptionId) const;

    // Reload one peers card: the cached tunnel config may be stale or plain
    // wrong, and dropping it is the only way to get a fresh one without
    // deleting the whole subscription. The card list is then re-read from
    // POST /v1/locations and the tunnel is rebuilt on the next connect.
    // A v1 config row has no card id — callers must keep it on the v1 path.
    Q_INVOKABLE void reloadCard(const int serverIndex);

    // True when the row already holds a tunnel config and the connect may go
    // straight to the dial. Otherwise the peer is created in the background and
    // `tunnelReady` is emitted for this row.
    bool ensureTunnelForRow(const int serverIndex);

    // Re-provision one pool of the row (the active variant) and invalidate what
    // the row cached. Used once after a failed handshake — never on a plain
    // connect, which dials whatever is cached without touching the network.
    // Public because the retry decision belongs to ConnectionController: it ends
    // in exactly one signal — `peersReprovisioned` (dial again once) or
    // `peersReprovisionFailed` (report the original error, no loop here).
    bool reprovisionPeerRow(const int serverIndex, bool afterFailure);

    // Provision one pool (every protocol card it serves) through a single
    // `POST /v1/peers/prepare` and store each resulting tunnel under its card
    // id, without re-pointing the row at another variant.
    //
    // The speed test calls this when the protocol the user picked has no config
    // yet: a handshake probe is answered only by a node that knows the peer, and
    // one call prepares every protocol card of the pool at once — so picking
    // AWG on a pool that was only ever dialled over hysteria2 becomes testable
    // without the user connecting first.
    Q_INVOKABLE void provisionPool(const QString &subscriptionId, const QString &poolKey);

    // Rebuild the row's cached tunnel config for one of the card's endpoints:
    // every node carries its own server key and AWG obfuscation, so the address
    // failover must switch the whole profile, not just host:port. Returns false
    // when the row is not a peer card or the address is not one of its nodes.
    bool retargetPeerConfig(QJsonObject &row, const QString &host, const int port = 0) const;

    // Drop the subscription from this device and revoke its tunnels on the api.
    //
    // Deleting the rows locally only is what makes users hit the limits: the
    // peer keeps its address and still counts against the three-per-device
    // budget, so after a few unlink/re-link cycles "cannot connect" starts and
    // support gets the tickets. A failed revoke is remembered and retried on the
    // next refresh instead of being lost.
    Q_INVOKABLE void forgetSubscription(const QString &subscriptionId);

signals:
    void cardsReady(int count);
    void cardsFailed(int httpStatus, const QString &error);

    // Completion of reloadCard(). Kept apart from cardsReady/cardsFailed so a
    // manual reload neither shows the "Servers added" banner nor reuses the
    // import messages; `error` is the api/builder text, empty on success.
    void cardReloadFinished(bool success, const QString &error);

    void tunnelReady(int serverIndex);
    void tunnelFailed(int serverIndex, int httpStatus, const QString &error);

    // Completion of provisionPool(): `ok` is false with the api/builder text when
    // no card of the pool could be prepared. Emitted from that call only.
    void poolProvisioned(const QString &subscriptionId, const QString &poolKey, bool ok, const QString &error);

    // The row's pool was re-provisioned after a failed connect: the caller dials
    // the row again, once. Emitted only from reprovisionPeerRow().
    void peersReprovisioned(int serverIndex);
    // The same attempt failed again — report the original error, no further
    // retries (`ok` is false only for the reprovision request itself).
    void peersReprovisionFailed(int serverIndex, const QString &error);

private:
    // Cards the api handed out per subscription on the last refresh. `> 0`
    // means the subscription is served on the v2 plane (see hasV2Cards).
    QHash<QString, int> m_cardsPerSubscription;

    void buildCardRows(const QString &subscriptionId, bool manualReload = false, bool provision = false);
    void createTunnel(const int serverIndex);
    // One batched `POST /v1/peers/prepare` for every pool of the subscription:
    // what an import/manual refresh runs once, storing each `prepared[].config`
    // under its card id (same shape a single-peer connect stores).
    void provisionPeerConfigs(const QString &subscriptionId);
    // Connect-time check of a `client_config` row: the config is a function of
    // the card's payload, so a container built from an older payload is
    // rebuilt locally (no api, no peer slot) before the dial.
    bool ensureClientConfigTunnel(const int serverIndex, const QJsonObject &row, const amnezia::peers::PoolCard &card);

    // (subscription_id, peer_id) pairs whose revocation must reach the api.
    void revokePeers(const QVector<QPair<QString, QString>> &peers);
    void queueRevokes(const QVector<QPair<QString, QString>> &peers);
    void flushQueuedRevokes();
    static QString peerIdOfRow(const QJsonObject &row);

    QSharedPointer<ServersModel> m_serversModel;
    std::shared_ptr<Settings> m_settings;
};

#endif // PEERSIMPORTCONTROLLER_H
