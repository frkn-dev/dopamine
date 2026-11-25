#include "ui/controllers/peersImportController.h"

#include <QDateTime>
#include <QDebug>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>

#include "containers/containers_defs.h"
#include "core/api/apiDefs.h"
#include "core/controllers/gatewayController.h"
#include "core/controllers/peersController.h"
#include "core/defs.h"
#include "core/deviceIdentity.h"
#include "core/peerConfigBuilder.h"
#include "protocols/protocols_defs.h"
#include "secure_qsettings.h"
#include "settings.h"
#include "ui/models/servers_model.h"
#include "version.h" // ORGANIZATION_NAME / APPLICATION_NAME: same store as the app

namespace
{
using namespace amnezia::peers;

constexpr char kPeerCardId[] = "peerCardId";
constexpr char kPeerSubscriptionId[] = "peerSubscriptionId";
constexpr char kDisplayInfo[] = "displayInfo";
// One row per pool: `peerPoolKey` is its identity ("<shard>:<pool>"), and
// `peerVariants` holds the concrete card of every protocol the pool serves, so
// the row can switch protocol in place without another /v1/locations call.
constexpr char kPeerPoolKey[] = "peerPoolKey";
constexpr char kPeerVariants[] = "peerVariants";
constexpr char kPendingRevoke[] = "peers/pending_revoke";
// When the row's peer config was provisioned (ISO-8601 UTC), stored inside the
// protocol object so it travels with the config it describes.
constexpr char kConfigCreatedAt[] = "config_created_at";
// A peer config older than this is refreshed on the next connect: the shard
// address or the card's parameters may have moved and a week-old tunnel is not
// worth dialling blind.

// Revocations that did not reach the api (offline, 5xx) wait here, in the same
// Keychain-backed store as everything else, and are retried on the next refresh.
SecureQSettings &revokeQueue()
{
    static SecureQSettings settings(ORGANIZATION_NAME, APPLICATION_NAME);
    return settings;
}

QVector<QPair<QString, QString>> loadQueuedRevokes()
{
    QVector<QPair<QString, QString>> out;
    const auto raw = revokeQueue().value(QLatin1String(kPendingRevoke)).toString();
    for (const QJsonValue &value : QJsonDocument::fromJson(raw.toUtf8()).array()) {
        const QJsonObject entry = value.toObject();
        const QString sub = entry.value(QStringLiteral("subscription_id")).toString();
        const QString peer = entry.value(QStringLiteral("peer_id")).toString();
        if (!sub.isEmpty() && !peer.isEmpty()) {
            out.append({ sub, peer });
        }
    }
    return out;
}

void storeQueuedRevokes(const QVector<QPair<QString, QString>> &peers)
{
    QJsonArray array;
    for (const auto &pair : peers) {
        array.append(QJsonObject { { QStringLiteral("subscription_id"), pair.first },
                                   { QStringLiteral("peer_id"), pair.second } });
    }
    auto &settings = revokeQueue();
    settings.setValue(QLatin1String(kPendingRevoke),
                      QString(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.sync();
}

QString hostOf(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    return colon > 0 ? endpoint.left(colon) : endpoint;
}

int portOf(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon >= endpoint.size() - 1) {
        return 0;
    }
    bool ok = false;
    const int port = endpoint.mid(colon + 1).toInt(&ok);
    return ok ? port : 0;
}

QJsonObject nodeToJson(const PoolNode &node)
{
    QJsonObject out;
    out[QStringLiteral("endpoint")] = node.endpoint;
    out[QStringLiteral("serverPublicKey")] = node.serverPublicKey;
    out[QStringLiteral("mtu")] = node.mtu;
    out[QStringLiteral("dns")] = QJsonArray::fromStringList(node.dns);
    out[QStringLiteral("keepalive")] = node.keepalive;
    out[QStringLiteral("obfuscation")] = node.obfuscation;
    out[QStringLiteral("hasObfuscation")] = node.hasObfuscation;
    // A `client_config` node's dial parameters travel with the row, so the
    // tunnel of such a card can be rebuilt from the row alone — no /v1/locations
    // and, more importantly, no POST /v1/peers.
    if (!node.payload.isEmpty()) {
        out[QStringLiteral("payload")] = node.payload;
    }
    return out;
}

PoolNode nodeFromJson(const QJsonObject &object)
{
    PoolNode node;
    node.endpoint = object.value(QStringLiteral("endpoint")).toString();
    node.serverPublicKey = object.value(QStringLiteral("serverPublicKey")).toString();
    node.mtu = object.value(QStringLiteral("mtu")).toInt(1280);
    for (const QJsonValue &dns : object.value(QStringLiteral("dns")).toArray()) {
        node.dns.append(dns.toString());
    }
    node.keepalive = object.value(QStringLiteral("keepalive")).toInt(25);
    node.obfuscation = object.value(QStringLiteral("obfuscation")).toObject();
    node.hasObfuscation = object.value(QStringLiteral("hasObfuscation")).toBool(!node.obfuscation.isEmpty());
    node.payload = object.value(QStringLiteral("payload")).toObject();
    return node;
}

// Complete protocol config for one node of a card: the INI the platform layers
// parse plus the JSON fields they read. Used both when the tunnel is created and
// when the connect switches to another endpoint of the card, so both paths
// produce exactly the same shape.
QJsonObject protoForNode(const PoolCard &card, const PoolNode &node, const QString &privateKey,
                         const QString &publicKey, const QString &peerAddress, const QString &peerId)
{
    QJsonObject proto;
    proto[config_key::config] = PeerConfigBuilder::build(card, node, privateKey, peerAddress);
    proto[config_key::client_priv_key] = privateKey;
    proto[config_key::client_pub_key] = publicKey;
    proto[config_key::client_ip] = peerAddress;
    proto[config_key::server_pub_key] = node.serverPublicKey;
    proto[config_key::hostName] = hostOf(node.endpoint);
    proto[config_key::port] = portOf(node.endpoint);
    // MTU and keepalive stay strings: the desktop daemon and the iOS extension
    // read them as strings, the same way a v1 gateway config carries them.
    proto[config_key::mtu] = QString::number(node.mtu);
    proto[config_key::allowed_ips] = QJsonArray { QStringLiteral("0.0.0.0/0") };
    proto[config_key::persistent_keep_alive] = QString::number(node.keepalive > 0 ? node.keepalive : 25);
    // Junk parameters go in under the names the daemon and the iOS extension
    // read (`Jc`, `H1`, `RandomTrailers`), not the api's lower_snake_case ones
    // (`jc`, `h1`, `random_trailers`): keyed by `jc` the config carried no
    // obfuscation at all, so the client handshaked with the default magic
    // headers and the node silently dropped every packet — the tunnel never
    // reached Connected and passed no traffic.
    const QJsonObject obfuscation = PeerConfigBuilder::obfuscationData(node);
    for (auto it = obfuscation.constBegin(); it != obfuscation.constEnd(); ++it) {
        proto[it.key()] = it.value();
    }
    if (!peerId.isEmpty()) {
        proto[QStringLiteral("peer_id")] = peerId;
    }
    // When this config was provisioned. A peer tunnel older than a week is
    // refreshed on the next connect (PeersImportController::ensureTunnelForRow):
    // the address may have been recycled and the card's parameters may have
    // moved, so dialling a stale tunnel blind is what leaves the user staring at
    // a spinner.
    proto[QLatin1String(kConfigCreatedAt)] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    // iOS reads the JSON blob and uses its `config` field as the INI source.
    proto[config_key::last_config] = QString(QJsonDocument(proto).toJson(QJsonDocument::Compact));
    return proto;
}

// `kind` of a card, as /v1/locations states it: absent (or anything unknown)
// is a peer card, which keeps the POST /v1/peers flow.
bool isClientConfigCard(const PoolCard &card)
{
    return card.kind.compare(QLatin1String("client_config"), Qt::CaseInsensitive) == 0;
}

// What a `client_config` node is dialled at. The payload is authoritative when
// it names a server: the card's endpoint can be an entry IP while TLS is
// verified against `sni`, and the port lives in the payload too.
QString dialHostOfNode(const PoolNode &node)
{
    const QString server = node.payload.value(QStringLiteral("server")).toString().trimmed();
    return server.isEmpty() ? hostOf(node.endpoint) : server;
}

int dialPortOfNode(const PoolNode &node)
{
    const int port = node.payload.value(QStringLiteral("port")).toInt();
    return port > 0 ? port : portOf(node.endpoint);
}

// Ready-to-store protocol config of a `client_config` node, shaped exactly like
// the one the v1 import writes for a hysteria2 server: the xray container holds
// `config`/`last_config` with the outbound JSON. No peer id, no assigned
// address, no device key — the node terminates the tunnel itself.
QJsonObject clientConfigForNode(const PoolCard &card, const PoolNode &node)
{
    QJsonObject proto;
    const QString config = PeerConfigBuilder::buildClientConfig(card, node);
    if (config.isEmpty()) {
        return proto;
    }
    proto[config_key::config] = config;
    proto[config_key::last_config] = config;
    proto[config_key::isThirdPartyConfig] = true;
    // The payload this config was built from. Stored inside the container, so a
    // cached tunnel built from an older card is recognisable on the next
    // connect — after a refresh and after a restart alike — and is rebuilt
    // instead of dialled.
    proto[QStringLiteral("payloadHash")] = payloadFingerprint(card.nodes);
    return proto;
}

// `kind` of a card, derived from its proto when the row does not state one. A
// row written before the active card's kind was kept in step (or switched by a
// build that only rewrote cardId) carries the kind of the variant the pool was
// imported with; defaulting that to "peer" sent a hysteria2 card down the peer
// path, where it failed for lack of a server_public_key. Never guess `peer` for
// a hysteria proto — hysteria2 is a `client_config` protocol, and its payload is
// the only thing the builder needs.
QString kindForProto(const QString &proto, const QString &stated)
{
    const QString kind = stated.trimmed().toLower();
    if (!kind.isEmpty()) {
        return kind;
    }
    const QString tag = proto.trimmed().toLower();
    if (tag.contains(QLatin1String("hysteria")) || tag.contains(QLatin1String("vless"))) {
        return QStringLiteral("client_config");
    }
    if (tag == QLatin1String("amnezia_peer") || tag == QLatin1String("wireguard_peer")) {
        return QStringLiteral("peer");
    }
    // Unknown proto and no stated kind: say so instead of pretending it is a
    // peer card; the caller logs it.
    return QString();
}

PoolCard cardFromRow(const QJsonObject &row)
{
    PoolCard card;
    card.cardId = row.value(QLatin1String(kPeerCardId)).toString();
    const QJsonObject display = row.value(QLatin1String(kDisplayInfo)).toObject();
    card.pool = display.value(QStringLiteral("pool")).toString();
    card.poolTranslation = display.value(QStringLiteral("poolTranslation")).toObject();
    // The card id ends with the proto (`<shard>:<pool>:<proto>`), so a row whose
    // displayInfo lagged behind the active card still names it.
    QString proto = display.value(QStringLiteral("proto")).toString().trimmed();
    if (proto.isEmpty()) {
        const int sep = card.cardId.lastIndexOf(QLatin1Char(':'));
        if (sep >= 0) {
            proto = card.cardId.mid(sep + 1).trimmed();
        }
    }
    card.proto = proto.isEmpty() ? QStringLiteral("amnezia_peer") : proto;
    card.kind = kindForProto(card.proto, display.value(QStringLiteral("kind")).toString());
    if (card.kind.isEmpty()) {
        qWarning() << "[PEERS] card" << card.cardId << "states no kind and its proto"
                   << card.proto << "is unknown; taking the peer path";
    }
    for (const QJsonValue &node : row.value(QStringLiteral("peerNodes")).toArray()) {
        card.nodes.append(nodeFromJson(node.toObject()));
    }
    return card;
}

// Protocol tag of a card: the api's own `proto` field when present, otherwise
// the last segment of `card_id` (`<shard>:<pool>:<proto>`).
QString protoOfCard(const PoolCard &card)
{
    if (!card.proto.trimmed().isEmpty()) {
        return card.proto.trimmed().toLower();
    }
    const int sep = card.cardId.lastIndexOf(QLatin1Char(':'));
    return sep >= 0 ? card.cardId.mid(sep + 1).trimmed().toLower() : QString();
}

// Container a card's protocol belongs to. A `wireguard_peer` card is plain
// WireGuard: labelled AmneziaWG (the hardcoded DockerContainer::Awg this used to
// be) every pool serving both protocols showed two "Russia" rows with the same
// protocol name, and the row handed the daemon an amnezia-awg container for a
// WireGuard node.
//
// hysteria2 is a client_config protocol that runs on the xray transport the app
// already ships (amnezia-xray + tun2socks + LibXrayPing), so it maps to
// DockerContainer::Xray. Sending it to Awg — the old fallback for every
// non-wireguard tag — handed the daemon an amnezia-awg container holding an xray
// config, and the tunnel could never start. Everything else in the peers plane
// is still AmneziaWG.
DockerContainer containerForProto(const QString &proto)
{
    if (proto.contains(QLatin1String("wireguard")) || proto.startsWith(QLatin1String("wg"))) {
        return DockerContainer::WireGuard;
    }
    if (proto.contains(QLatin1String("hysteria")) || proto.contains(QLatin1String("vless"))) {
        return DockerContainer::Xray;
    }
    return DockerContainer::Awg;
}

// Container name the row stores ("amnezia-awg" / "amnezia-wireguard" /
// "amnezia-xray").
QString containerNameForProto(const QString &proto)
{
    return ContainerProps::containerToString(containerForProto(proto));
}

// Canonical service protocol of the row ("awg" / "wireguard" / "hysteria2") —
// this is what the list's protocol selector and filter work with, and what the
// health check dispatches on.
//
// hysteria2 keeps its own name instead of the container's protocol alias
// ("xray"): the UI pill, the latency probe (HealthCheckController's hysteria2
// branch) and the auto-selection tier all key on "hysteria2", while the container
// still is amnezia-xray.
QString protocolForProto(const QString &proto)
{
    if (proto.contains(QLatin1String("hysteria"))) {
        return QStringLiteral("hysteria2");
    }
    // VLESS keeps its own canonical tag instead of the container's protocol
    // alias: the pill, the filter, the latency probe (HealthCheckController's
    // vless branch) and the connect path all key on "vless", while the container
    // still is amnezia-xray.
    if (proto.contains(QLatin1String("vless"))) {
        return QStringLiteral("vless");
    }
    return ContainerProps::containerTypeToProtocolString(containerForProto(proto));
}

// Short key the platform layers read the protocol config by ("awg" /
// "wireguard" / "xray"); the container name is the second key iOS looks the data
// up by.
QString protocolKeyForProto(const QString &proto)
{
    return ContainerProps::containerTypeToString(containerForProto(proto));
}

int protocolRank(const QString &proto)
{
    const DockerContainer container = containerForProto(proto);
    if (container == DockerContainer::Awg) {
        return 0;
    }
    if (container == DockerContainer::WireGuard) {
        return 1;
    }
    // Xray-family protocols (hysteria2) come after the WG ones: a pool that
    // serves both keeps AmneziaWG as its default variant.
    return 2;
}

// One entry of the row's `peerVariants`: everything needed to switch the row to
// this protocol without another /v1/locations call (the concrete card id, its
// endpoints, country and translations).
QJsonObject variantJson(const PoolCard &card)
{
    const QString proto = protoOfCard(card);
    QJsonObject out;
    out[QStringLiteral("cardId")] = card.cardId;
    out[QStringLiteral("proto")] = proto;
    // `protocol` is what the UI pill, the filter and the health check see
    // ("hysteria2"); `protocolKey` is the key the platform layers read the
    // protocol config by inside the container ("xray" for hysteria2). They
    // differ for every xray-family card, so the row needs both.
    out[QStringLiteral("protocol")] = protocolForProto(proto);
    out[QStringLiteral("protocolKey")] = protocolKeyForProto(proto);
    out[QStringLiteral("container")] = containerNameForProto(proto);
    // Derived from the proto when the card states none, so a hysteria2 variant
    // stays `client_config` all the way through the row.
    const QString kind = kindForProto(proto, card.kind);
    out[QStringLiteral("kind")] = kind.isEmpty() ? QStringLiteral("peer") : kind;
    out[QStringLiteral("country")] = card.country;
    out[QStringLiteral("countryName")] = card.countryName;
    QJsonArray nodes;
    for (const auto &node : card.nodes) {
        nodes.append(nodeToJson(node));
    }
    out[QStringLiteral("nodes")] = nodes;
    // Fingerprint of the payload these nodes carry (empty for a peer card). The
    // row keeps it next to the cached tunnel of this card, so a refresh can tell
    // a rotated payload — obfs dropped, auth/sni/port changed — from the card
    // the tunnel was built for.
    out[QStringLiteral("payloadHash")] = payloadFingerprint(nodes);
    return out;
}

// Cards of one pool, grouped by (env, pool). /v1/locations deliberately carries
// no env — the shard id is the env and the client only ever echoes the card
// back — so the identity is the first two segments of `card_id`
// (`<shard>:<pool>:<proto>`). Grouping by pool name alone would merge two
// shards that happen to sell a pool of the same name.
struct PoolGroup
{
    QString key;  // "<shard>:<pool>" — the row's pool identity
    QString pool;
    QVector<PoolCard> cards;
};

QVector<PoolGroup> groupCardsByPool(const QVector<PoolCard> &cards)
{
    QVector<PoolGroup> groups;
    QHash<QString, int> indexByKey;
    for (const auto &card : cards) {
        QString key = card.cardId;
        const int lastSep = key.lastIndexOf(QLatin1Char(':'));
        if (lastSep > 0) {
            key = key.left(lastSep); // drop the protocol segment
        }
        if (key.isEmpty()) {
            key = card.pool;
        }
        if (key.isEmpty()) {
            continue;
        }
        const auto it = indexByKey.constFind(key);
        if (it != indexByKey.constEnd()) {
            groups[it.value()].cards.append(card);
            continue;
        }
        PoolGroup group;
        group.key = key;
        group.pool = card.pool;
        group.cards.append(card);
        indexByKey.insert(key, groups.size());
        groups.append(group);
    }
    // AmneziaWG first: it is the pool's default variant unless the user picked
    // the other protocol (the choice is remembered per pool in the settings).
    for (auto &group : groups) {
        std::stable_sort(group.cards.begin(), group.cards.end(), [](const PoolCard &a, const PoolCard &b) {
            return protocolRank(protoOfCard(a)) < protocolRank(protoOfCard(b));
        });
    }
    return groups;
}

// The card the row currently points at: the protocol the user picked for this
// pool, or AmneziaWG / the first protocol the pool offers. A stored choice whose
// card the api no longer hands out (WireGuard removed from the pool) is dropped
// by the caller before this runs, so the row falls back to the remaining
// variant instead of leaving the row broken or the choice stored forever.
const PoolCard *activeCardOf(const PoolGroup &group, const QString &storedProto)
{
    if (!storedProto.isEmpty()) {
        for (const auto &card : group.cards) {
            if (protoOfCard(card) == storedProto) {
                return &card;
            }
        }
    }
    return group.cards.isEmpty() ? nullptr : &group.cards.first();
}

// One row per pool, shaped exactly like a v1 gateway config row. The concrete
// card of the selected protocol stays in `peerCardId` (that is what POST
// /v1/peers receives), the other protocols of the pool travel in
// `peerVariants`, and the row's protocol/container/container key are derived
// from the card's own `proto` — never assumed to be AmneziaWG.
QJsonObject buildPoolRow(const QString &subscriptionId, const PoolGroup &group, const PoolCard &card,
                         const QJsonArray &variants)
{
    const QString proto = protoOfCard(card);
    const QString protocol = protocolForProto(proto);
    const QString containerName = containerNameForProto(proto);
    const QString protocolKey = protocolKeyForProto(proto);

    QJsonObject row;
    row[config_key::name] = group.pool;
    // The dial target: a client_config node states it in its payload (`server`
    // may be a domain while the endpoint is an entry IP), a peer node dials its
    // endpoint.
    row[config_key::hostName] = dialHostOfNode(card.nodes.first());
    // The port rides in the endpoint; iOS and the card both read
    // a top-level `port`, and the tunnel config keeps the same
    // value inside its INI.
    row[config_key::port] = dialPortOfNode(card.nodes.first());

    // The endpoint list is the same v1 structure a gateway row
    // carries: `node_ips` (every entry address) plus `node_ports`
    // (ip -> port). Everything downstream — the per-address ping,
    // the enable/disable toggles and the address failover on
    // connect — reads only these, so a peers card with several
    // nodes gets the same machinery for free. hostName/port above
    // stay the primary endpoint for the iOS extension and the
    // tunnel builder.
    QStringList endpointHosts;
    QJsonObject endpointPorts;
    for (const auto &node : card.nodes) {
        const QString host = hostOf(node.endpoint);
        const int port = portOf(node.endpoint);
        if (host.isEmpty() || endpointHosts.contains(host)) {
            continue;
        }
        endpointHosts.append(host);
        if (port > 0) {
            endpointPorts[host] = port;
        }
    }
    row[QStringLiteral("node_ips")] = QJsonArray::fromStringList(endpointHosts);
    row[QStringLiteral("node_ports")] = endpointPorts;
    // DNS of the tunnel: the card's own resolvers, or the app's
    // defaults when the node declares none. Both platforms read
    // dns1/dns2 from the row (iOS hands them to the extension).
    const QStringList dns = card.nodes.first().dns.isEmpty()
                                    ? QStringList { QStringLiteral("1.1.1.1"), QStringLiteral("1.0.0.1") }
                                    : card.nodes.first().dns;
    row[config_key::dns1] = dns.value(0);
    row[config_key::dns2] = dns.value(1, dns.value(0));
    row[config_key::configVersion] = apiDefs::ConfigSource::AmneziaGateway;
    row[QLatin1String(kPeerCardId)] = card.cardId;
    row[QLatin1String(kPeerSubscriptionId)] = subscriptionId;
    // Pool identity (env:pool) and every protocol the pool serves. The row is
    // one pool; the protocol is a property of the row, switched in place.
    row[QLatin1String(kPeerPoolKey)] = group.key;
    row[QLatin1String(kPeerVariants)] = variants;

    // `api_config.auth_data.api_key` is what the list groups by and
    // what the subscription filter and removeSubscription() read.
    // Without it the card lands in a group with no subscription and
    // a saved subscription filter hides it entirely.
    QJsonObject authData;
    authData[QStringLiteral("api_key")] = subscriptionId;
    QJsonObject apiConfig;
    apiConfig[QStringLiteral("auth_data")] = authData;
    apiConfig[QStringLiteral("pool")] = group.pool;
    apiConfig[QStringLiteral("service_protocol")] = protocol;
    row[QStringLiteral("api_config")] = apiConfig;

    QJsonObject display;
    display[QStringLiteral("pool")] = group.pool;
    display[QStringLiteral("poolTranslation")] = card.poolTranslation;
    display[QStringLiteral("countryCode")] = card.country;
    display[QStringLiteral("countryName")] = card.countryName;
    display[QStringLiteral("protocol")] = protocol;
    display[QStringLiteral("proto")] = proto;
    // Card kind, so the row can rebuild the card exactly as the api described
    // it (`client_config` cards must never be sent to POST /v1/peers).
    display[QStringLiteral("kind")] =
            card.kind.isEmpty() ? QStringLiteral("peer") : card.kind;
    // Origin tag: a v2 pool row must never be merged with a legacy v1 config
    // that happens to carry the same pool name.
    display[QStringLiteral("peerPool")] = true;
    display[QStringLiteral("poolKey")] = group.key;
    row[QLatin1String(kDisplayInfo)] = display;

    QJsonArray nodes;
    for (const auto &node : card.nodes) {
        nodes.append(nodeToJson(node));
    }
    row[QStringLiteral("peerNodes")] = nodes;

    // Payload fingerprint per card of the pool (and the active card's one on the
    // row, for callers that only look at the row). Persisted with the row, so a
    // payload that changed while the app was closed is still detected on the
    // next start instead of dialling the tunnel built from the old card.
    QJsonObject payloadHashes;
    for (const QJsonValue &variantValue : variants) {
        const QJsonObject variant = variantValue.toObject();
        const QString variantCard = variant.value(QStringLiteral("cardId")).toString();
        const QString variantHash = variant.value(QStringLiteral("payloadHash")).toString();
        if (!variantCard.isEmpty() && !variantHash.isEmpty()) {
            payloadHashes.insert(variantCard, variantHash);
        }
    }
    row[QStringLiteral("peerPayloadHashes")] = payloadHashes;
    row[QStringLiteral("peerPayloadHash")] = payloadHashes.value(card.cardId).toString();

    // The container exists from the first display (the list shows
    // the protocol). A peer card's config appears only after the tunnel is
    // created — see ensureTunnelForRow — because it needs the address the api
    // assigns to this device. A client_config card needs nothing from the api
    // (its payload is enough), so the config is ready here: the connect dials
    // at once and the latency probe works before the first connect.
    QJsonObject protoConfig = isClientConfigCard(card)
            ? clientConfigForNode(card, card.nodes.first())
            : QJsonObject();
    QJsonObject container;
    container[config_key::container] = containerName;
    // Both spellings: the platform layers read the short alias, iOS also looks
    // the data up by the container name.
    container[protocolKey] = protoConfig;
    container[containerName] = protoConfig;
    row[config_key::containers] = QJsonArray { container };
    row[config_key::defaultContainer] = containerName;
    return row;
}

/// One `/v1/locations` answer. `hasGrant` is what tells an authoritative
/// "no cards for you" (the grant exists, its protocol list carries no card
/// protocol) from a transport failure or a subscription the api knows nothing
/// about — the two cases must not be treated the same by the model.
struct LocationsResult
{
    QVector<PoolCard> cards;
    int httpStatus = 0;
    QString error;
    bool hasGrant = false;
    /// The grant still names a card protocol, so an empty card list means "no
    /// live node right now" — the stored rows must survive that.
    bool grantHasCardProto = false;
};

amnezia::ErrorCode buildLocations(Settings &settings, const QString &subscriptionId, LocationsResult &out)
{
    auto gateway = QSharedPointer<GatewayController>::create(
            settings.getGatewayEndpoint(), settings.isDevGatewayEnv(), apiDefs::requestTimeoutMsecs, false);
    PeersController peers(gateway.get());
    QVector<AccessInfo> access;
    const auto code = peers.locations(subscriptionId, out.cards, access);
    out.hasGrant = !access.isEmpty();
    for (const auto &grant : access) {
        for (const QString &proto : grant.protos) {
            if (proto == QLatin1String("amnezia_peer") || proto == QLatin1String("wireguard_peer")
                || proto == QLatin1String("hysteria2") || proto == QLatin1String("vless_xhttp_cdn")) {
                out.grantHasCardProto = true;
            }
        }
    }
    out.httpStatus = peers.lastHttpStatus();
    out.error = peers.lastError();
    return code;
}

struct TunnelResult
{
    QString cardId;
    QString peerId;
    QString address;
    QString ini;
    QString privateKey;
    QString publicKey;
    // Ready-to-store protocol config (INI + the JSON fields the platform layers
    // read). Built on the worker thread with the rest of the tunnel.
    QJsonObject proto;
    int httpStatus = 0;
    QString error;
    bool ok = false;
};

TunnelResult buildTunnel(Settings &settings, const QString &subscriptionId, const PoolCard &card,
                         const QString &deviceUid, const QString &privateKey, const QString &publicKey,
                         const QStringList &disabledHosts)
{
    TunnelResult out;
    out.cardId = card.cardId;
    out.privateKey = privateKey;
    out.publicKey = publicKey;

    if (card.nodes.isEmpty()) {
        out.error = QStringLiteral("card without nodes");
        return out;
    }

    // A `client_config` card is not a peer. Its payload already carries every
    // dial parameter, so this path never calls POST /v1/peers (which answers
    // `400 card_kind_not_peer` for such a card anyway) and never spends one of
    // the device's tunnels. Rebuilt here — not only at row build — because a
    // manual reload drops the cached config and the next connect must recover
    // without the api.
    if (isClientConfigCard(card)) {
        // Says which branch the builder took, so an on-device log proves the row
        // reached this path instead of POST /v1/peers.
        qDebug().noquote() << QStringLiteral("[PEERS] client_config tunnel: %1 proto=%2")
                                      .arg(card.cardId, protoOfCard(card));
        const PoolNode *target = nullptr;
        for (const auto &candidate : card.nodes) {
            if (candidate.payload.isEmpty() || hostOf(candidate.endpoint).isEmpty()) {
                continue;
            }
            if (disabledHosts.contains(hostOf(candidate.endpoint))) {
                continue;
            }
            target = &candidate;
            break;
        }
        if (!target) {
            out.error = QStringLiteral("client_config card has no enabled node");
            return out;
        }
        out.proto = clientConfigForNode(card, *target);
        out.ini = out.proto.value(config_key::config).toString();
        out.ok = !out.ini.isEmpty();
        if (!out.ok) {
            out.error = QStringLiteral("client_config payload is not dialable");
        }
        return out;
    }

    // A card can hold several nodes (and a node several addresses). The row's
    // primary endpoint is the first node, but a node the user switched off in
    // the card must not be dialed: start from the first enabled one instead of
    // failing the connect. Checked before createPeer so a doomed card does not
    // burn one of the three device peers.
    const PoolNode *node = nullptr;
    for (const auto &candidate : card.nodes) {
        if (candidate.serverPublicKey.isEmpty() || hostOf(candidate.endpoint).isEmpty()
            || portOf(candidate.endpoint) <= 0) {
            continue;
        }
        if (disabledHosts.contains(hostOf(candidate.endpoint))) {
            continue;
        }
        node = &candidate;
        break;
    }
    if (!node) {
        // A cached tunnel without a key or an endpoint would be used by every
        // later connect and never work — fail the first one instead.
        out.error = card.nodes.size() > 1 || !disabledHosts.isEmpty()
                ? QStringLiteral("card has no enabled endpoint with a server key")
                : QStringLiteral("card node without endpoint or server key");
        return out;
    }

    auto gateway = QSharedPointer<GatewayController>::create(
            settings.getGatewayEndpoint(), settings.isDevGatewayEnv(), apiDefs::requestTimeoutMsecs, false);
    PeersController peers(gateway.get());

    PeerInfo peer;
    // The other branch: a peer card still mints its tunnel through POST
    // /v1/peers. Logged so a device log shows which path the row took.
    qDebug().noquote() << QStringLiteral("[PEERS] create peer for %1 proto=%2")
                                  .arg(card.cardId, protoOfCard(card));
    const auto code = peers.createPeer(subscriptionId, deviceUid, card.cardId, publicKey, QString(), peer);
    out.httpStatus = peers.lastHttpStatus();
    out.error = peers.lastError();
    if (code != amnezia::ErrorCode::NoError || peer.address.isEmpty()) {
        return out;
    }

    out.peerId = peer.peerId;
    out.address = peer.address;
    out.proto = protoForNode(card, *node, privateKey, publicKey, peer.address, peer.peerId);
    out.ini = out.proto.value(config_key::config).toString();
    out.ok = !out.ini.isEmpty();
    return out;
}
} // namespace

PeersImportController::PeersImportController(QSharedPointer<ServersModel> serversModel,
                                             std::shared_ptr<Settings> settings, QObject *parent)
    : QObject(parent), m_serversModel(std::move(serversModel)), m_settings(std::move(settings))
{
}

void PeersImportController::refreshCards(const QString &subscriptionId, bool provision)
{
    if (subscriptionId.isEmpty() || !m_serversModel || !m_settings) {
        return;
    }

    flushQueuedRevokes();
    buildCardRows(subscriptionId, false, provision);
}

bool PeersImportController::hasV2Cards(const QString &subscriptionId) const
{
    return m_cardsPerSubscription.value(subscriptionId, 0) > 0;
}

void PeersImportController::reloadCard(const int serverIndex)
{
    if (!m_serversModel) {
        return;
    }

    QJsonObject row = m_serversModel->serverRow(serverIndex);
    const QString cardId = row.value(QLatin1String(kPeerCardId)).toString();
    QString subscriptionId = row.value(QLatin1String(kPeerSubscriptionId)).toString();
    if (subscriptionId.isEmpty()) {
        // A row written before `peerSubscriptionId` existed still names its
        // subscription through `auth_data.api_key`, which is all the reload
        // needs to rebuild it (and to give it the pool's protocol variants).
        subscriptionId = m_serversModel->subscriptionIdForRow(serverIndex);
    }
    if (cardId.isEmpty() || subscriptionId.isEmpty()) {
        qWarning() << "[PEERS] reloadCard: row without card or subscription" << serverIndex;
        emit cardReloadFinished(false, QString());
        return;
    }

    // Dropping the cached tunnel is the point of the reload: the row keeps its
    // card (addresses, pool, flags), and the next connect rebuilds the tunnel
    // from the fresh /v1/locations data instead of reusing a stale config. The
    // tunnel itself stays on the row as a display snapshot — wiping it here left
    // the open card with an empty «Configuration Files» popup until the user
    // reconnected and then reopened the card.
    m_serversModel->invalidatePeerConfig(cardId);
    flushQueuedRevokes();
    buildCardRows(subscriptionId, true);
}

void PeersImportController::buildCardRows(const QString &subscriptionId, bool manualReload, bool provision)
{
    QVector<PoolCard> cards;
    int httpStatus = 0;
    QString error;
    auto settings = m_settings;
    if (settings) {
        settings->rememberSubscription(subscriptionId);
    }

    auto *watcher = new QFutureWatcher<LocationsResult>(this);
    connect(watcher, &QFutureWatcher<LocationsResult>::finished, this,
            [this, watcher, subscriptionId, manualReload, provision]() {
                const auto result = watcher->result();
                watcher->deleteLater();

                const auto &cards = result.cards;
                const int httpStatus = result.httpStatus;
                qDebug() << "[PEERS] locations answer: cards" << cards.size()
                         << "http" << result.httpStatus << result.error;
                if (cards.isEmpty()) {
                    // An empty list is a legitimate answer: no grant on this shard,
                    // a v1-only subscription, or every node offline. Only a real
                    // transport/server error deserves a message — the startup reload
                    // asks for the stored (old) subscription id and used to raise a
                    // bogus "Could not load servers" on every launch.
                    const bool httpOk = httpStatus >= 200 && httpStatus < 400;
                    if (!httpOk) {
                        // A failed request proves nothing about the pools: touching
                        // the model here wiped the user's subscription on a timeout.
                        // Leave it byte-identical and surface the error only.
                        if (manualReload) {
                            emit cardReloadFinished(false, result.error);
                        } else {
                            emit cardsFailed(httpStatus, result.error);
                        }
                        return;
                    }
                    // An empty answer NEVER deletes anything. This used to call
                    // dropPeerCards when the grant carried no card protocol, and
                    // that wipe cost the user his whole server list when the api
                    // answered "grant, but no cards" for any reason. Stored rows
                    // are removed only by an explicit "remove subscription".
                    m_serversModel->syncPeerCards(subscriptionId, QJsonArray());
                    // No cards = the subscription is still on the v1 plane (or
                    // lost its grant): the legacy fetch keeps running for it.
                    m_cardsPerSubscription.insert(subscriptionId, 0);
                    if (manualReload) {
                        emit cardReloadFinished(true, QString());
                    } else {
                        emit cardsReady(0);
                    }
                    return;
                }

                QJsonArray rows;
                // One card per protocol in the answer, one row per pool in the
                // list: "Russia" is one row with a protocol choice, not AWG and
                // WireGuard side by side.
                const auto groups = groupCardsByPool(cards);
                for (const auto &group : groups) {
                    // `peerVariants` is the pool's fresh card set — one entry per
                    // protocol the pool serves, built every refresh from the api
                    // answer, never from the row that happened to be stored. A
                    // card whose node list is empty in this answer is still a
                    // served protocol: leaving it out is what left a two-protocol
                    // pool with a single variant and no protocol picker.
                    QJsonArray variants;
                    QStringList servedProtos;
                    for (const auto &card : group.cards) {
                        variants.append(variantJson(card));
                        const QString proto = protoOfCard(card);
                        if (!proto.isEmpty() && !servedProtos.contains(proto)) {
                            servedProtos.append(proto);
                        }
                    }

                    // Only a card with endpoints can be dialled, so the row's
                    // active variant is chosen from those.
                    QVector<PoolCard> usable;
                    for (const auto &card : group.cards) {
                        if (!card.nodes.isEmpty()) {
                            usable.append(card);
                        }
                    }
                    if (usable.isEmpty()) {
                        continue;
                    }
                    PoolGroup withNodes = group;
                    withNodes.cards = usable;

                    // The protocol the user picked for this pool, remembered
                    // across refreshes. Stale state is healed here, not merely
                    // ignored: a choice the pool no longer serves (WireGuard on
                    // a pool that now serves only AmneziaWG) is dropped from the
                    // settings and the row falls back to the pool's default —
                    // the first variant by protocolRank (AmneziaWG).
                    const QString poolKey = subscriptionId + QLatin1Char('|') + group.key;
                    QString storedProto = m_settings ? m_settings->peerPoolProtocol(poolKey) : QString();
                    if (!storedProto.isEmpty() && !servedProtos.contains(storedProto)) {
                        qDebug() << "[PEERS] pool" << group.key << "no longer serves" << storedProto
                                 << "— dropping the stored choice; served:" << servedProtos;
                        if (m_settings) {
                            m_settings->removePeerPoolProtocol(poolKey);
                        }
                        storedProto.clear();
                    }
                    const PoolCard *active = activeCardOf(withNodes, storedProto);
                    if (!active) {
                        continue;
                    }
                    qDebug() << "[PEERS] pool" << group.key << "served" << servedProtos
                             << "variants" << variants.size()
                             << "active" << protoOfCard(*active)
                             << "stored" << (storedProto.isEmpty() ? QStringLiteral("<default>") : storedProto);
                    rows.append(buildPoolRow(subscriptionId, withNodes, *active, variants));
                }

                qDebug() << "[PEERS] cards for" << subscriptionId << ":" << cards.size()
                         << "cards," << groups.size() << "pools," << rows.size() << "rows";
                m_serversModel->syncPeerCards(subscriptionId, rows);
                // Cards landed: this subscription is served on the v2 plane, so
                // the legacy per-protocol fetch must not run for it any more.
                m_cardsPerSubscription.insert(subscriptionId, rows.size());
                if (manualReload) {
                    emit cardReloadFinished(true, QString());
                } else {
                    emit cardsReady(rows.size());
                }
                // Import / manual refresh: provision the pools once, so every
                // row has a dialable config and the speed test has something to
                // dial before the first connect. Never on the periodic reload —
                // the owner does not want api work on every app open.
                if (provision && !rows.isEmpty()) {
                    provisionPeerConfigs(subscriptionId);
                }
            });

    watcher->setFuture(QtConcurrent::run([settings, subscriptionId]() {
        LocationsResult result;
        buildLocations(*settings, subscriptionId, result);
        return result;
    }));
}

namespace
{
// Pool handle of a card (`<shard>:<pool>`, the card id without its protocol
// segment) — the form `POST /v1/peers/prepare` takes. The row's `peerPoolKey`
// is authoritative when it has one.
QString poolKeyOfCard(const PoolCard &card, const QJsonObject &row)
{
    const QString stored = row.value(QLatin1String(kPeerPoolKey)).toString();
    if (!stored.isEmpty()) {
        return stored;
    }
    const int sep = card.cardId.lastIndexOf(QLatin1Char(':'));
    return sep > 0 ? card.cardId.left(sep) : QString();
}

// Node a peer tunnel of this card is dialled at: the first one with a server key
// and an address the user has not switched off.
const PoolNode *dialableNodeOf(const PoolCard &card, const QStringList &disabledHosts)
{
    for (const auto &candidate : card.nodes) {
        if (candidate.serverPublicKey.isEmpty() || hostOf(candidate.endpoint).isEmpty()
            || portOf(candidate.endpoint) <= 0) {
            continue;
        }
        if (disabledHosts.contains(hostOf(candidate.endpoint))) {
            continue;
        }
        return &candidate;
    }
    return nullptr;
}

struct PreparedTunnel
{
    QString cardId;
    QString error;
    QJsonObject proto;
};

struct ProvisionResult
{
    QVector<PreparedTunnel> tunnels;
    QVector<SkippedPool> skipped;
    int httpStatus = 0;
    QString error;
    amnezia::ErrorCode code = amnezia::ErrorCode::NoError;
};

// Worker: one batched `POST /v1/peers/prepare`, plus the ready-to-store protocol
// config of every prepared card, built exactly the way a single-peer connect
// builds it (device key + the address the api allocated).
ProvisionResult provisionTunnels(Settings &settings, const QString &subscriptionId, const QString &deviceUid,
                                 const QString &deviceName, const QString &platform, const QString &privateKey,
                                 const QString &publicKey, const QStringList &pools, const QStringList &disabledHosts)
{
    ProvisionResult out;
    auto gateway = QSharedPointer<GatewayController>::create(
            settings.getGatewayEndpoint(), settings.isDevGatewayEnv(), apiDefs::requestTimeoutMsecs, false);
    PeersController peers(gateway.get());
    QVector<PreparedPeer> prepared;
    out.code = peers.prepare(subscriptionId, deviceUid, publicKey, deviceName, platform, pools, prepared, out.skipped);
    out.httpStatus = peers.lastHttpStatus();
    out.error = peers.lastError();
    if (out.code != amnezia::ErrorCode::NoError) {
        return out;
    }

    for (const auto &peer : prepared) {
        PreparedTunnel tunnel;
        tunnel.cardId = peer.cardId.isEmpty() ? peer.card.cardId : peer.cardId;
        if (isClientConfigCard(peer.card)) {
            // Never prepared (the api answers `card_kind_not_peer`): the row
            // builds its config from the payload it already holds.
            continue;
        }
        if (peer.card.nodes.isEmpty()) {
            tunnel.error = QStringLiteral("prepared card without nodes");
        } else if (const PoolNode *node = dialableNodeOf(peer.card, disabledHosts)) {
            tunnel.proto = protoForNode(peer.card, *node, privateKey, publicKey, peer.address, peer.peerId);
            if (tunnel.proto.value(config_key::config).toString().isEmpty()) {
                tunnel.error = QStringLiteral("empty tunnel config");
            }
        } else {
            tunnel.error = QStringLiteral("card has no enabled endpoint with a server key");
        }
        out.tunnels.append(tunnel);
    }
    return out;
}

struct ReprovisionResult
{
    QString cardId;
    QJsonObject proto;
    int httpStatus = 0;
    QString error;
    bool ok = false;
};

// Worker: refresh one pool's peer for the row's active card — `prepare` with the
// single `<shard>:<pool>` handle, falling back to `POST /v1/peers` when the
// batch did not hand this card back. Both are idempotent for the device key, so
// the refresh never burns a second tunnel slot.
ReprovisionResult reprovisionTunnel(Settings &settings, const QString &subscriptionId, const PoolCard &rowCard,
                                    const QString &poolKey, const QString &deviceUid, const QString &deviceName,
                                    const QString &platform, const QString &privateKey, const QString &publicKey,
                                    const QStringList &disabledHosts)
{
    ReprovisionResult out;
    out.cardId = rowCard.cardId;
    auto gateway = QSharedPointer<GatewayController>::create(
            settings.getGatewayEndpoint(), settings.isDevGatewayEnv(), apiDefs::requestTimeoutMsecs, false);
    PeersController peers(gateway.get());

    QStringList pools;
    if (!poolKey.isEmpty()) {
        pools.append(poolKey);
    }
    QVector<PreparedPeer> prepared;
    QVector<SkippedPool> skipped;
    const auto code = peers.prepare(subscriptionId, deviceUid, publicKey, deviceName, platform, pools, prepared,
                                    skipped);
    out.httpStatus = peers.lastHttpStatus();
    out.error = peers.lastError();
    for (const auto &skip : skipped) {
        qDebug() << "[PEERS] prepare skipped" << skip.cardId << skip.reason;
    }

    const PoolCard *card = nullptr;
    QString address;
    QString peerId;
    if (code == amnezia::ErrorCode::NoError) {
        for (const auto &peer : prepared) {
            // The handle names one card per protocol, so a prepared entry of the
            // same pool but another protocol is not this row's tunnel.
            const bool sameCard = peer.cardId == rowCard.cardId || peer.card.cardId == rowCard.cardId;
            const bool sameVariant = !poolKey.isEmpty()
                    && poolKeyOfCard(peer.card, QJsonObject()) == poolKey
                    && protoOfCard(peer.card) == protoOfCard(rowCard);
            if (sameCard || sameVariant) {
                card = &peer.card;
                address = peer.address;
                peerId = peer.peerId;
                break;
            }
        }
    }
    if (!card) {
        PeerInfo peer;
        const auto createCode = peers.createPeer(subscriptionId, deviceUid, rowCard.cardId, publicKey, QString(), peer);
        out.httpStatus = peers.lastHttpStatus();
        if (createCode != amnezia::ErrorCode::NoError || peer.address.isEmpty()) {
            out.error = peers.lastError();
            if (out.error.isEmpty()) {
                out.error = QStringLiteral("peer was not created");
            }
            return out;
        }
        card = &rowCard;
        address = peer.address;
        peerId = peer.peerId;
    }

    if (card->nodes.isEmpty()) {
        out.error = QStringLiteral("card without nodes");
        return out;
    }
    const PoolNode *node = dialableNodeOf(*card, disabledHosts);
    if (!node) {
        out.error = QStringLiteral("card has no enabled endpoint with a server key");
        return out;
    }
    out.proto = protoForNode(*card, *node, privateKey, publicKey, address, peerId);
    out.ok = !out.proto.value(config_key::config).toString().isEmpty();
    if (!out.ok) {
        out.error = QStringLiteral("empty tunnel config");
    }
    return out;
}
} // namespace

// One batched `POST /v1/peers/prepare` for every peer pool of the subscription:
// the import / manual refresh moment. Each `prepared[].config` is stored under
// its card id the same way a single-peer connect stores it, so the rows are
// dialable and the speed test has configs before the first connect. A pool the
// api skipped is logged at debug and never surfaced: it is not an error, and
// the connect path re-provisions that pool lazily anyway.
void PeersImportController::provisionPeerConfigs(const QString &subscriptionId)
{
    if (!m_serversModel || !m_settings || subscriptionId.isEmpty()) {
        return;
    }
    const QString deviceUid = DeviceIdentity::deviceUid();
    const QString privateKey = DeviceIdentity::privateKey();
    const QString publicKey = DeviceIdentity::publicKey();
    if (deviceUid.isEmpty() || privateKey.isEmpty() || publicKey.isEmpty()) {
        qDebug() << "[PEERS] prepare skipped for" << subscriptionId << ": no device identity yet";
        return;
    }
    const QString deviceName = DeviceIdentity::deviceName();
    const QString platform = QSysInfo::productType().toLower();
    const QStringList disabledHosts = m_settings->poolDisabledEndpoints();
    auto settings = m_settings;

    auto *watcher = new QFutureWatcher<ProvisionResult>(this);
    connect(watcher, &QFutureWatcher<ProvisionResult>::finished, this,
            [this, watcher, subscriptionId]() {
                const ProvisionResult result = watcher->result();
                watcher->deleteLater();
                if (result.code != amnezia::ErrorCode::NoError) {
                    // Non-fatal by design: the first connect creates the peer it
                    // needs, so a failed batch only costs that lazy call.
                    qWarning() << "[PEERS] prepare failed for" << subscriptionId << result.httpStatus
                               << result.error;
                    return;
                }
                int stored = 0;
                for (const auto &tunnel : result.tunnels) {
                    if (tunnel.proto.isEmpty()) {
                        qDebug() << "[PEERS] prepare: card" << tunnel.cardId << "not dialable:"
                                 << tunnel.error;
                        continue;
                    }
                    m_serversModel->setPeerConfig(tunnel.cardId, tunnel.proto);
                    ++stored;
                }
                for (const auto &skip : result.skipped) {
                    qDebug() << "[PEERS] prepare skipped" << skip.cardId << skip.reason;
                }
                qDebug() << "[PEERS] prepare for" << subscriptionId << ": stored" << stored << "of"
                         << result.tunnels.size() << "pools, skipped" << result.skipped.size();
            });

    watcher->setFuture(QtConcurrent::run([settings, subscriptionId, deviceUid, deviceName, platform, privateKey,
                                          publicKey, disabledHosts]() {
        // No `pools`: every peer pool of the grant.
        return provisionTunnels(*settings, subscriptionId, deviceUid, deviceName, platform, privateKey, publicKey,
                                QStringList(), disabledHosts);
    }));
}

// One pool, every protocol card of it, in a single `POST /v1/peers/prepare`;
// each prepared tunnel is stored under its own card id. Nothing here touches the
// row's active variant: switching protocols on screen stays the server list's
// business, while the speed test only needs a peer the node will answer.
void PeersImportController::provisionPool(const QString &subscriptionId, const QString &poolKey)
{
    if (!m_serversModel || !m_settings || subscriptionId.isEmpty() || poolKey.isEmpty()) {
        emit poolProvisioned(subscriptionId, poolKey, false, QStringLiteral("no pool to prepare"));
        return;
    }
    const QString deviceUid = DeviceIdentity::deviceUid();
    const QString privateKey = DeviceIdentity::privateKey();
    const QString publicKey = DeviceIdentity::publicKey();
    if (deviceUid.isEmpty() || privateKey.isEmpty() || publicKey.isEmpty()) {
        emit poolProvisioned(subscriptionId, poolKey, false, QStringLiteral("no device identity"));
        return;
    }
    const QString deviceName = DeviceIdentity::deviceName();
    const QString platform = QSysInfo::productType().toLower();
    const QStringList disabledHosts = m_settings->poolDisabledEndpoints();
    auto settings = m_settings;
    QStringList pools;
    pools.append(poolKey);

    auto *watcher = new QFutureWatcher<ProvisionResult>(this);
    connect(watcher, &QFutureWatcher<ProvisionResult>::finished, this,
            [this, watcher, subscriptionId, poolKey]() {
                const ProvisionResult result = watcher->result();
                watcher->deleteLater();
                if (result.code != amnezia::ErrorCode::NoError) {
                    qWarning() << "[PEERS] prepare pool" << poolKey << "failed" << result.httpStatus
                               << result.error;
                    emit poolProvisioned(subscriptionId, poolKey, false, result.error);
                    return;
                }
                int stored = 0;
                for (const auto &tunnel : result.tunnels) {
                    if (tunnel.proto.isEmpty()) {
                        qDebug() << "[PEERS] prepare pool" << poolKey << ": card" << tunnel.cardId
                                 << "not dialable:" << tunnel.error;
                        continue;
                    }
                    m_serversModel->setPeerConfig(tunnel.cardId, tunnel.proto);
                    ++stored;
                }
                for (const auto &skip : result.skipped) {
                    qDebug() << "[PEERS] prepare pool" << poolKey << "skipped" << skip.cardId << skip.reason;
                }
                qDebug() << "[PEERS] prepare pool" << poolKey << ": stored" << stored << "of"
                         << result.tunnels.size() << "cards";
                const bool ok = stored > 0 || !result.tunnels.isEmpty();
                emit poolProvisioned(subscriptionId, poolKey, ok,
                                     ok ? QString() : QStringLiteral("no dialable card"));
            });

    watcher->setFuture(QtConcurrent::run([settings, subscriptionId, deviceUid, deviceName, platform, privateKey,
                                          publicKey, disabledHosts, pools]() {
        return provisionTunnels(*settings, subscriptionId, deviceUid, deviceName, platform, privateKey, publicKey,
                                pools, disabledHosts);
    }));
}

// Refresh one pool of the row and store the fresh config.
//
// One caller: ConnectionController after a failed handshake (`afterFailure`),
// resuming through peersReprovisioned. The attempt ends in exactly one signal —
// the retry is the caller's decision, not a loop here.
bool PeersImportController::reprovisionPeerRow(const int serverIndex, bool afterFailure)
{
    if (!m_serversModel || !m_settings) {
        return false;
    }
    const QJsonObject row = m_serversModel->serverRow(serverIndex);
    const QString cardId = row.value(QLatin1String(kPeerCardId)).toString();
    QString subscriptionId = row.value(QLatin1String(kPeerSubscriptionId)).toString();
    if (subscriptionId.isEmpty()) {
        subscriptionId = m_serversModel->subscriptionIdForRow(serverIndex);
    }
    if (cardId.isEmpty() || subscriptionId.isEmpty()) {
        return false;
    }
    const PoolCard card = cardFromRow(row);
    if (isClientConfigCard(card)) {
        // Nothing on the shard is registered for a client_config card: it is
        // rebuilt from its payload, never re-provisioned. The connect paths that
        // own it (ensureClientConfigTunnel, payload fingerprint) stay in charge.
        return false;
    }
    const QString poolKey = poolKeyOfCard(card, row);

    const QString deviceUid = DeviceIdentity::deviceUid();
    const QString privateKey = DeviceIdentity::privateKey();
    const QString publicKey = DeviceIdentity::publicKey();
    if (deviceUid.isEmpty() || privateKey.isEmpty() || publicKey.isEmpty()) {
        qWarning() << "[PEERS] cannot reprovision" << cardId << ": no device key";
        return false;
    }
    const QString deviceName = DeviceIdentity::deviceName();
    const QString platform = QSysInfo::productType().toLower();
    const QStringList disabledHosts = m_settings->poolDisabledEndpoints();
    auto settings = m_settings;

    // The old tunnel must not be reused: its address or parameters are what is
    // being replaced.
    m_serversModel->invalidatePeerConfig(cardId);

    auto *watcher = new QFutureWatcher<ReprovisionResult>(this);
    connect(watcher, &QFutureWatcher<ReprovisionResult>::finished, this,
            [this, watcher, serverIndex, afterFailure, cardId]() {
                const ReprovisionResult result = watcher->result();
                watcher->deleteLater();
                if (!result.ok) {
                    qWarning() << "[PEERS] reprovision of" << cardId << "failed:" << result.httpStatus
                               << result.error;
                    if (afterFailure) {
                        emit peersReprovisionFailed(serverIndex, result.error);
                    } else {
                        emit tunnelFailed(serverIndex, result.httpStatus, result.error);
                    }
                    return;
                }
                m_serversModel->setPeerConfig(cardId, result.proto);
                if (afterFailure) {
                    emit peersReprovisioned(serverIndex);
                } else {
                    emit tunnelReady(serverIndex);
                }
            });

    watcher->setFuture(QtConcurrent::run([settings, subscriptionId, card, poolKey, deviceUid, deviceName, platform,
                                          privateKey, publicKey, disabledHosts]() {
        return reprovisionTunnel(*settings, subscriptionId, card, poolKey, deviceUid, deviceName, platform,
                                 privateKey, publicKey, disabledHosts);
    }));
    return true;
}

QString PeersImportController::peerIdOfRow(const QJsonObject &row)
{
    const QJsonArray containers = row.value(config_key::containers).toArray();
    if (containers.isEmpty()) {
        return {};
    }
    const QJsonObject container = containers.at(0).toObject();
    const QString name = container.value(config_key::container).toString();
    return container.value(name).toObject().value(QStringLiteral("peer_id")).toString();
}

void PeersImportController::forgetSubscription(const QString &subscriptionId)
{
    if (!m_serversModel || subscriptionId.isEmpty()) {
        return;
    }

    QVector<QPair<QString, QString>> doomed;
    for (int i = 0; i < m_serversModel->rowCount(); ++i) {
        const QJsonObject row = m_serversModel->serverRow(i);
        if (row.value(QLatin1String(kPeerSubscriptionId)).toString() != subscriptionId) {
            continue;
        }
        const QString peerId = peerIdOfRow(row);
        if (!peerId.isEmpty()) {
            doomed.append({ subscriptionId, peerId });
        }
    }

    if (!doomed.isEmpty()) {
        revokePeers(doomed);
    }
    // The rows go now; the revocation finishes in the background and is queued
    // for a retry if the network is down.
    m_serversModel->removeSubscription(subscriptionId);
}

void PeersImportController::revokePeers(const QVector<QPair<QString, QString>> &peers)
{
    if (peers.isEmpty() || !m_settings) {
        return;
    }
    auto settings = m_settings;

    auto *watcher = new QFutureWatcher<QVector<QPair<QString, QString>>>(this);
    connect(watcher, &QFutureWatcher<QVector<QPair<QString, QString>>>::finished, this, [this, watcher]() {
        const auto failed = watcher->result();
        watcher->deleteLater();
        if (!failed.isEmpty()) {
            qWarning() << "[PEERS] revoke failed for" << failed.size() << "tunnel(s), queued for retry";
            queueRevokes(failed);
        }
    });

    watcher->setFuture(QtConcurrent::run([settings, peers]() {
        QVector<QPair<QString, QString>> failed;
        auto gateway = QSharedPointer<GatewayController>::create(
                settings->getGatewayEndpoint(), settings->isDevGatewayEnv(),
                apiDefs::requestTimeoutMsecs, false);
        PeersController controller(gateway.get());
        for (const auto &pair : peers) {
            if (controller.deletePeer(pair.first, pair.second) != amnezia::ErrorCode::NoError) {
                failed.append(pair);
            }
        }
        return failed;
    }));
}

void PeersImportController::queueRevokes(const QVector<QPair<QString, QString>> &peers)
{
    auto queued = loadQueuedRevokes();
    for (const auto &pair : peers) {
        if (!queued.contains(pair)) {
            queued.append(pair);
        }
    }
    storeQueuedRevokes(queued);
}

void PeersImportController::flushQueuedRevokes()
{
    auto queued = loadQueuedRevokes();
    if (queued.isEmpty()) {
        return;
    }
    // Clear first: revokePeers re-queues whatever still fails, so a crash in
    // between cannot turn the queue into a permanent retry storm.
    storeQueuedRevokes({});
    revokePeers(queued);
}

bool PeersImportController::ensureTunnelForRow(const int serverIndex)
{
    if (!m_serversModel) {
        return true;
    }
    const QJsonObject row = m_serversModel->serverRow(serverIndex);
    const QString cardId = row.value(QLatin1String(kPeerCardId)).toString();
    if (cardId.isEmpty()) {
        return true; // not a peer row: the v1 flow owns it
    }

    // A `client_config` card is a function of its payload and its config is
    // built locally (no api, no peer slot), so the connect path never trusts a
    // cached container: a payload that moved since the tunnel was built is
    // rebuilt here, synchronously, before the dial.
    const PoolCard card = cardFromRow(row);
    if (isClientConfigCard(card)) {
        return ensureClientConfigTunnel(serverIndex, row, card);
    }

    if (m_serversModel->peerRowHasConfig(serverIndex)) {
        // The cached config is dialled as is — no network call on connect. A
        // config that no longer works shows up as a failed handshake, and that
        // failure is what reprovisions the pool once and retries (failOrRetry in
        // ConnectionController), not the age of the config.
        return true;
    }

    createTunnel(serverIndex);
    return false;
}

// Rebuild a `client_config` row whose cached container was produced by a
// different payload than the card states now (obfs dropped, auth/sni/port
// rotated — or a container written by a build that kept no fingerprint at all).
// Returns true when the row is dialable; false hands the row to createTunnel,
// which reports why the card cannot be built.
bool PeersImportController::ensureClientConfigTunnel(const int serverIndex, const QJsonObject &row, const PoolCard &card)
{
    const QString cardId = card.cardId;
    const QString freshHash = payloadFingerprint(card.nodes);

    // The container the dial would use: the row's first entry, whichever key the
    // protocol object sits under (`xray` and the container name are both
    // written), with the flat CDN form as a fallback.
    QJsonObject container;
    const QJsonArray containers = row.value(config_key::containers).toArray();
    if (!containers.isEmpty()) {
        container = containers.at(0).toObject();
    }
    QJsonObject proto = container.value(container.value(config_key::container).toString()).toObject();
    if (proto.value(config_key::config).toString().isEmpty()) {
        for (auto it = container.constBegin(); it != container.constEnd(); ++it) {
            if (it.key() == QLatin1String(config_key::container)) {
                continue;
            }
            const QJsonObject candidate = it.value().toObject();
            if (!candidate.value(config_key::config).toString().isEmpty()) {
                proto = candidate;
                break;
            }
        }
    }

    const QString cachedConfig = proto.value(config_key::config).toString();
    const QString cachedHash = proto.value(QStringLiteral("payloadHash")).toString();
    // A row flagged stale was invalidated on purpose (manual reload, refresh saw
    // a new payload): rebuild even when the fingerprint happens to match.
    const bool invalidated = row.value(QStringLiteral("peerConfigStale")).toBool();

    if (!cachedConfig.isEmpty() && !invalidated && !freshHash.isEmpty() && cachedHash == freshHash) {
        return true; // the cached container was built from this exact payload
    }

    const QStringList disabledHosts = m_settings ? m_settings->poolDisabledEndpoints() : QStringList();
    const PoolNode *target = nullptr;
    for (const auto &candidate : card.nodes) {
        if (candidate.payload.isEmpty() || hostOf(candidate.endpoint).isEmpty()) {
            continue;
        }
        if (disabledHosts.contains(hostOf(candidate.endpoint))) {
            continue;
        }
        target = &candidate;
        break;
    }
    if (!target) {
        qWarning() << "[PEERS] client_config card" << cardId << "has no enabled node to rebuild from";
        createTunnel(serverIndex);
        return false;
    }

    const QJsonObject built = clientConfigForNode(card, *target);
    if (built.value(config_key::config).toString().isEmpty()) {
        createTunnel(serverIndex);
        return false;
    }

    m_serversModel->setPeerConfig(cardId, built);
    if (!cachedConfig.isEmpty() && cachedHash != freshHash) {
        // Says the payload itself moved, not just that the container was
        // missing: a device log line proves the stale tunnel was dropped.
        qDebug().noquote() << QStringLiteral("[PEERS] client_config config rebuilt (payload changed) %1").arg(cardId);
    } else {
        qDebug().noquote() << QStringLiteral("[PEERS] client_config config rebuilt %1").arg(cardId);
    }
    return true;
}

void PeersImportController::createTunnel(const int serverIndex)
{
    const QJsonObject row = m_serversModel->serverRow(serverIndex);
    const QString cardId = row.value(QLatin1String(kPeerCardId)).toString();
    const QString subscriptionId = row.value(QLatin1String(kPeerSubscriptionId)).toString();
    if (cardId.isEmpty() || subscriptionId.isEmpty()) {
        emit tunnelFailed(serverIndex, 0, QStringLiteral("peers: row without card or subscription"));
        return;
    }

    // A client_config card needs neither a device key nor a device identity:
    // nothing on the shard is registered for it. A peer card does, and a
    // missing key must fail with text rather than a tunnel that cannot come up.
    const PoolCard card = cardFromRow(row);
    QString deviceUid;
    QString privateKey;
    QString publicKey;
    if (!isClientConfigCard(card)) {
        // Keychain access stays on this thread; only the network work moves away.
        deviceUid = DeviceIdentity::deviceUid();
        privateKey = DeviceIdentity::privateKey();
        publicKey = DeviceIdentity::publicKey();
        if (privateKey.isEmpty() || publicKey.isEmpty()) {
            emit tunnelFailed(serverIndex, 0, QStringLiteral("peers: cannot create the device key"));
            return;
        }
    }

    auto settings = m_settings;
    // Endpoint switches live in the shared settings store; read them here, on
    // the GUI thread, and hand the worker a plain list.
    const QStringList disabledHosts = m_settings ? m_settings->poolDisabledEndpoints() : QStringList();

    auto *watcher = new QFutureWatcher<TunnelResult>(this);
    connect(watcher, &QFutureWatcher<TunnelResult>::finished, this, [this, watcher, serverIndex, cardId]() {
        const TunnelResult result = watcher->result();
        watcher->deleteLater();

        if (!result.ok) {
            qWarning() << "[PEERS] cannot create tunnel" << cardId << result.httpStatus << result.error;
            emit tunnelFailed(serverIndex, result.httpStatus, result.error);
            return;
        }

        m_serversModel->setPeerConfig(cardId, result.proto);
        emit tunnelReady(serverIndex);
    });

    watcher->setFuture(QtConcurrent::run([settings, subscriptionId, card, deviceUid, privateKey, publicKey,
                                          disabledHosts]() {
        return buildTunnel(*settings, subscriptionId, card, deviceUid, privateKey, publicKey, disabledHosts);
    }));
}

bool PeersImportController::retargetPeerConfig(QJsonObject &row, const QString &host, const int port) const
{
    if (host.isEmpty() || row.value(QLatin1String(kPeerCardId)).toString().isEmpty()) {
        return false;
    }

    const PoolCard card = cardFromRow(row);
    // A client_config row has nothing to rebuild: its tunnel is a single xray
    // outbound, and the failover has already moved the dial address inside it
    // (ConnectionController::patchServerConfigAddress rewrites
    // `settings.servers[].address` and keeps the SNI, which is what TLS verifies).
    // Rebuilding from the payload here would undo that patch.
    if (isClientConfigCard(card)) {
        return false;
    }
    const PoolNode *target = nullptr;
    for (const auto &node : card.nodes) {
        if (hostOf(node.endpoint) != host) {
            continue;
        }
        // Two nodes may share an address on different ports; the port decides.
        if (port > 0 && portOf(node.endpoint) > 0 && portOf(node.endpoint) != port) {
            continue;
        }
        target = &node;
        break;
    }
    if (!target || target->serverPublicKey.isEmpty()) {
        return false;
    }

    QJsonArray containers = row.value(config_key::containers).toArray();
    if (containers.isEmpty()) {
        return false;
    }
    QJsonObject container = containers.at(0).toObject();
    QString name = container.value(config_key::container).toString();
    if (name.isEmpty()) {
        name = containerNameForProto(protoOfCard(card));
    }
    const QJsonObject cached = container.value(name).toObject();
    if (cached.isEmpty()) {
        return false;
    }
    const QString privateKey = cached.value(config_key::client_priv_key).toString();
    const QString publicKey = cached.value(config_key::client_pub_key).toString();
    const QString peerAddress = cached.value(config_key::client_ip).toString();
    if (privateKey.isEmpty() || peerAddress.isEmpty()) {
        return false;
    }

    // Each node carries its own server key and AWG junk: the cached tunnel was
    // built for the primary node, so dialing another endpoint of the card needs
    // a fresh protocol config, not just a patched host:port.
    const QJsonObject proto = protoForNode(card, *target, privateKey, publicKey, peerAddress,
                                           cached.value(QStringLiteral("peer_id")).toString());
    if (proto.value(config_key::config).toString().isEmpty()) {
        return false;
    }
    // Rewrite the entry under the card's own container and short protocol key
    // (`wireguard` for a wireguard_peer card, not the old hardcoded `awg`).
    QJsonObject entry;
    entry[config_key::container] = name;
    entry[protocolKeyForProto(protoOfCard(card))] = proto;
    entry[name] = proto;
    row[config_key::containers] = QJsonArray { entry };
    return true;
}

