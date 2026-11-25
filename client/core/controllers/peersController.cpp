#include "core/controllers/peersController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace amnezia::peers
{
namespace
{
QString str(const QJsonObject &obj, const char *key, const QString &def = QString())
{
    const auto value = obj.value(QString::fromLatin1(key));
    return value.isString() ? value.toString() : def;
}

QStringList strings(const QJsonValue &value)
{
    QStringList out;
    for (const auto &item : value.toArray()) {
        if (item.isString()) {
            out.append(item.toString());
        }
    }
    return out;
}

QDateTime dateTime(const QJsonObject &obj, const char *key)
{
    const auto raw = str(obj, key);
    if (raw.isEmpty()) {
        return {};
    }
    return QDateTime::fromString(raw, Qt::ISODateWithMs);
}

PoolNode parseNode(const QJsonObject &obj)
{
    PoolNode node;
    node.endpoint = str(obj, "endpoint");
    node.serverPublicKey = str(obj, "server_public_key");
    node.mtu = obj.value("mtu").toInt(1280);
    node.dns = strings(obj.value("dns"));
    node.keepalive = obj.value("keepalive").toInt(25);
    if (obj.value("obfuscation").isObject()) {
        node.obfuscation = obj.value("obfuscation").toObject();
        node.hasObfuscation = true;
    }
    // Client parameters of a `client_config` card (hysteria2): opaque here, the
    // tunnel builder reads it. Kept as the api sent it so the config can be
    // rebuilt later without another /v1/locations round trip.
    if (obj.value("payload").isObject()) {
        node.payload = obj.value("payload").toObject();
    }
    return node;
}

PoolCard parseCard(const QJsonObject &obj)
{
    PoolCard card;
    card.cardId = str(obj, "card_id");
    card.pool = str(obj, "pool");
    card.country = str(obj, "country");
    card.countryName = str(obj, "country_name");
    card.proto = str(obj, "proto");
    // Absent `kind` is `peer`: every card an older api (and every peer
    // protocol) hands out keeps the POST /v1/peers flow.
    card.kind = str(obj, "kind", "peer").trimmed().toLower();
    card.poolTranslation = obj.value("pool_translation").toObject();
    for (const auto &node : obj.value("nodes").toArray()) {
        if (node.isObject()) {
            card.nodes.append(parseNode(node.toObject()));
        }
    }
    return card;
}

AccessInfo parseAccess(const QJsonObject &obj)
{
    AccessInfo info;
    info.plan = str(obj, "plan");
    info.planTitle = str(obj, "plan_title");
    info.protos = strings(obj.value("protos"));
    info.expiresAt = dateTime(obj, "expires_at");
    info.trafficLimit = static_cast<qint64>(obj.value("traffic_limit").toDouble(-1));
    info.usedBytes = static_cast<qint64>(obj.value("used_bytes").toDouble(0));
    return info;
}

PeerInfo parsePeer(const QJsonObject &obj)
{
    PeerInfo peer;
    peer.peerId = str(obj, "peer_id");
    peer.deviceId = str(obj, "device_id");
    peer.proto = str(obj, "proto");
    peer.address = str(obj, "address");
    peer.label = str(obj, "label");
    peer.revokeReason = str(obj, "revoke_reason");
    peer.createdAt = dateTime(obj, "created_at");
    peer.revokedAt = dateTime(obj, "revoked_at");
    return peer;
}

DeviceInfo parseDevice(const QJsonObject &obj)
{
    DeviceInfo device;
    device.id = str(obj, "id");
    device.deviceUid = str(obj, "device_uid");
    device.deviceName = str(obj, "device_name");
    device.supportTag = str(obj, "support_tag");
    device.peers = obj.value("peers").toInt(0);
    return device;
}
} // namespace

PeersController::PeersController(GatewayController *gateway, QObject *parent)
    : QObject(parent), m_gateway(gateway)
{
}

ErrorCode PeersController::call(const QString &endpoint, const QJsonObject &payload, QJsonObject &reply)
{
    QByteArray body;
    m_httpStatus = 0;
    m_lastError.clear();

    const ErrorCode code = m_gateway->postWithStatus(endpoint, payload, body, m_httpStatus);
    qDebug() << "[PEERS] call" << endpoint << "->" << int(code) << "http" << m_httpStatus
             << "bytes" << body.size();
    if (code != ErrorCode::NoError) {
        m_lastError = QString::fromUtf8(body);
        return code;
    }

    QJsonParseError parseError{};
    const auto doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = QString::fromUtf8(body.left(300));
        return ErrorCode::ApiConfigEmptyError;
    }
    // api.frkn.org wraps every answer in {status, message, response}: callers here
    // read the payload keys directly (pools/access/peer), so unwrap it first —
    // without this the fields never matched and the cards stayed invisible.
    QJsonObject object = doc.object();
    if (object.value(QStringLiteral("response")).isObject()) {
        object = object.value(QStringLiteral("response")).toObject();
    }
    reply = object;
    return ErrorCode::NoError;
}

ErrorCode PeersController::locations(const QString &subscriptionId, QVector<PoolCard> &cards, QVector<AccessInfo> &access)
{
    QJsonObject reply;
    const ErrorCode code = call("%1v1/locations", { { "subscription_id", subscriptionId } }, reply);
    if (code != ErrorCode::NoError) {
        return code;
    }
    cards.clear();
    access.clear();
    for (const auto &item : reply.value("pools").toArray()) {
        if (item.isObject()) {
            auto card = parseCard(item.toObject());
            if (!card.cardId.isEmpty()) {
                cards.append(card);
            }
        }
    }
    for (const auto &item : reply.value("access").toArray()) {
        if (item.isObject()) {
            access.append(parseAccess(item.toObject()));
        }
    }
    return ErrorCode::NoError;
}

ErrorCode PeersController::peers(const QString &subscriptionId, const QString &deviceUid, QVector<PeerInfo> &peers)
{
    QJsonObject payload{ { "subscription_id", subscriptionId } };
    if (!deviceUid.isEmpty()) {
        payload.insert("device_uid", deviceUid);
    }
    QJsonObject reply;
    const ErrorCode code = call("%1v1/peers/list", payload, reply);
    if (code != ErrorCode::NoError) {
        return code;
    }
    peers.clear();
    for (const auto &item : reply.value("peers").toArray()) {
        if (item.isObject()) {
            peers.append(parsePeer(item.toObject()));
        }
    }
    return ErrorCode::NoError;
}

ErrorCode PeersController::devices(const QString &subscriptionId, QVector<DeviceInfo> &devices)
{
    QJsonObject reply;
    const ErrorCode code = call("%1v1/devices/list", { { "subscription_id", subscriptionId } }, reply);
    if (code != ErrorCode::NoError) {
        return code;
    }
    devices.clear();
    for (const auto &item : reply.value("devices").toArray()) {
        if (item.isObject()) {
            devices.append(parseDevice(item.toObject()));
        }
    }
    return ErrorCode::NoError;
}

ErrorCode PeersController::createPeer(const QString &subscriptionId, const QString &deviceUid, const QString &cardId,
                                      const QString &publicKey, const QString &label, PeerInfo &peer)
{
    QJsonObject payload{ { "subscription_id", subscriptionId }, { "device_uid", deviceUid }, { "card_id", cardId } };
    if (!publicKey.isEmpty()) {
        payload.insert("public_key", publicKey);
    }
    if (!label.isEmpty()) {
        payload.insert("label", label);
    }

    QJsonObject reply;
    const ErrorCode code = call("%1v1/peers", payload, reply);
    if (code != ErrorCode::NoError) {
        return code;
    }
    if (reply.value("peer").isObject()) {
        peer = parsePeer(reply.value("peer").toObject());
    }
    return ErrorCode::NoError;
}

ErrorCode PeersController::prepare(const QString &subscriptionId, const QString &deviceUid,
                                   const QString &publicKey, const QString &deviceName, const QString &platform,
                                   const QStringList &pools, QVector<PreparedPeer> &prepared,
                                   QVector<SkippedPool> &skipped)
{
    QJsonObject payload{ { "subscription_id", subscriptionId },
                         { "device_uid", deviceUid },
                         { "public_key", publicKey } };
    if (!deviceName.isEmpty()) {
        payload.insert("device_name", deviceName);
    }
    if (!platform.isEmpty()) {
        payload.insert("platform", platform);
    }
    // The card is a handle, not an env: a requested pool is `<shard>:<pool>` —
    // the card id without its protocol segment. No `pools` means every peer
    // pool of the grant.
    if (!pools.isEmpty()) {
        QJsonArray handles;
        for (const auto &pool : pools) {
            if (!pool.trimmed().isEmpty()) {
                handles.append(pool.trimmed());
            }
        }
        if (!handles.isEmpty()) {
            payload.insert("pools", handles);
        }
    }

    QJsonObject reply;
    const ErrorCode code = call("%1v1/peers/prepare", payload, reply);
    if (code != ErrorCode::NoError) {
        return code;
    }

    prepared.clear();
    skipped.clear();
    for (const auto &item : reply.value("prepared").toArray()) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject obj = item.toObject();
        PreparedPeer peer;
        peer.cardId = str(obj, "card_id");
        peer.peerId = str(obj, "peer_id");
        peer.address = str(obj, "address");
        // `config` is the card itself (`endpoint`, `server_public_key`, `mtu`,
        // `dns`, `keepalive`, `obfuscation`, `payload`): the same parser as
        // /v1/locations, so the tunnel builder reads one shape everywhere.
        if (obj.value("config").isObject()) {
            peer.card = parseCard(obj.value("config").toObject());
        }
        if (peer.card.cardId.isEmpty()) {
            peer.card.cardId = peer.cardId; // keep the handle dialable even so
        }
        // No address means there is nothing to build a peer tunnel from — the
        // api only omits it on a malformed entry; log-and-skip at the caller.
        prepared.append(peer);
    }
    for (const auto &item : reply.value("skipped").toArray()) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject obj = item.toObject();
        skipped.append({ str(obj, "card_id"), str(obj, "reason") });
    }
    return ErrorCode::NoError;
}

ErrorCode PeersController::deletePeer(const QString &subscriptionId, const QString &peerId)
{
    QJsonObject reply;
    return call("%1v1/peers/delete", { { "subscription_id", subscriptionId }, { "peer_id", peerId } }, reply);
}

ErrorCode PeersController::renameDevice(const QString &subscriptionId, const QString &deviceId, const QString &name)
{
    QJsonObject reply;
    return call("%1v1/devices/update", { { "subscription_id", subscriptionId }, { "device_id", deviceId }, { "device_name", name } },
                reply);
}

ErrorCode PeersController::deleteDevice(const QString &subscriptionId, const QString &deviceId)
{
    QJsonObject reply;
    return call("%1v1/devices/delete", { { "subscription_id", subscriptionId }, { "device_id", deviceId } }, reply);
}

ErrorCode PeersController::supportTag(const QString &subscriptionId, const QString &deviceUid, QString &tag)
{
    QJsonObject reply;
    const ErrorCode code = call("%1v1/support", { { "subscription_id", subscriptionId }, { "device_uid", deviceUid } }, reply);
    if (code != ErrorCode::NoError) {
        return code;
    }
    tag = str(reply, "support_tag");
    return ErrorCode::NoError;
}

} // namespace amnezia::peers
