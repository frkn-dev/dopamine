#ifndef PEERSCONTROLLER_H
#define PEERSCONTROLLER_H

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/controllers/gatewayController.h"
#include "core/defs.h"

// Peers plane: locations, tunnels, devices and the support tag.
//
// The shard is a server-side detail: a card is handed out as `card_id`
// ("<shard>:<pool>:<proto>") and echoed back when a tunnel is created, so no env
// or raw plan name ever reaches the UI — only `planTitle` and `poolTranslation`.
namespace amnezia::peers
{

struct PoolNode
{
    QString endpoint;
    QString serverPublicKey;
    int mtu = 1280;
    QStringList dns;
    int keepalive = 25;
    QJsonObject obfuscation;
    bool hasObfuscation = false;
    // Client parameters of a `client_config` card (hysteria2 today):
    // {server, port, sni, auth, insecure, obfs}. Empty on a peer card, which
    // carries a server key instead of a payload.
    QJsonObject payload;
};

struct PoolCard
{
    QString cardId;
    QString pool;
    // Country of the card under the flag: the api sends the pool's own country
    // (`pool_country_*`, falling back to its first node), never the hostname.
    QString country;
    QString countryName;
    QString proto;
    // `peer` or `client_config`; absent in the JSON of an older api means
    // `peer`. A client_config card carries everything the client needs in
    // `nodes[].payload` and must never reach POST /v1/peers.
    QString kind;
    QJsonObject poolTranslation;
    QVector<PoolNode> nodes;
};

struct AccessInfo
{
    QString plan;
    QString planTitle;
    QStringList protos;
    QDateTime expiresAt;
    qint64 trafficLimit = -1;
    qint64 usedBytes = 0;
};

struct PeerInfo
{
    QString peerId;
    QString deviceId;
    QString proto;
    QString address;
    QString label;
    QString revokeReason;
    QDateTime createdAt;
    QDateTime revokedAt;
};

struct DeviceInfo
{
    QString id;
    QString deviceUid;
    QString deviceName;
    QString supportTag;
    int peers = 0;
};

// One tunnel of `POST /v1/peers/prepare`: the address the api allocated plus the
// card (`prepared[].config`) the client builds the tunnel from — the same card
// object `/v1/locations` hands out, so the app parses one shape everywhere.
struct PreparedPeer
{
    QString cardId;
    QString peerId;
    QString address;
    PoolCard card;
};

// A pool the batch did not provision and the stable code that says why
// (`peers_limit`, `proto_not_granted`, `card_kind_not_peer`, `db`, …). Not an
// error: the api answers 200 and the client logs the reason at debug level.
struct SkippedPool
{
    QString cardId;
    QString reason;
};

class PeersController : public QObject
{
    Q_OBJECT

public:
    explicit PeersController(GatewayController *gateway, QObject *parent = nullptr);

    // GET-equivalents, POST over the AGW envelope (ids stay in the body).
    ErrorCode locations(const QString &subscriptionId, QVector<PoolCard> &cards, QVector<AccessInfo> &access);
    ErrorCode peers(const QString &subscriptionId, const QString &deviceUid, QVector<PeerInfo> &peers);
    ErrorCode devices(const QString &subscriptionId, QVector<DeviceInfo> &devices);
    ErrorCode supportTag(const QString &subscriptionId, const QString &deviceUid, QString &tag);

    // POST /v1/peers: the api picks the address, the client brings the public key.
    ErrorCode createPeer(const QString &subscriptionId, const QString &deviceUid, const QString &cardId,
                         const QString &publicKey, const QString &label, PeerInfo &peer);

    // POST /v1/peers/prepare: every peer pool of the grant (or just `pools`,
    // handles of the form `<shard>:<pool>`) provisioned in one call, so the
    // import has dialable configs before the first connect.
    //
    // Idempotent server-side by `(env, pool, public_key)`: a repeated call
    // returns the live peer instead of allocating another one, which is what
    // makes it safe to run on every import and refresh. A pool that cannot be
    // provisioned lands in `skipped` with a stable reason and never fails the
    // call — the caller logs it and moves on.
    ErrorCode prepare(const QString &subscriptionId, const QString &deviceUid, const QString &publicKey,
                      const QString &deviceName, const QString &platform, const QStringList &pools,
                      QVector<PreparedPeer> &prepared, QVector<SkippedPool> &skipped);
    ErrorCode deletePeer(const QString &subscriptionId, const QString &peerId);
    ErrorCode renameDevice(const QString &subscriptionId, const QString &deviceId, const QString &name);
    ErrorCode deleteDevice(const QString &subscriptionId, const QString &deviceId);

    // HTTP status of the last call: the peers flow branches on 402/403/409/503.
    int lastHttpStatus() const { return m_httpStatus; }
    QString lastError() const { return m_lastError; }

private:
    ErrorCode call(const QString &endpoint, const QJsonObject &payload, QJsonObject &reply);

    GatewayController *m_gateway = nullptr;
    int m_httpStatus = 0;
    QString m_lastError;
};

} // namespace amnezia::peers

#endif // PEERSCONTROLLER_H
