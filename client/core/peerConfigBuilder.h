#ifndef PEERCONFIGBUILDER_H
#define PEERCONFIGBUILDER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "core/controllers/peersController.h"

namespace amnezia::peers
{

// Builds the tunnel config of one peer from what the api handed out.
//
// The card carries everything the config needs per node (endpoint, server key,
// MTU, DNS, keepalive and the obfuscation profile), because pools of one shard
// may differ: never rebuild a config from a card other than the one it was
// created with.
class PeerConfigBuilder
{
public:
    // `privateKey` and `address` come from the device's own keypair and the
    // address the api assigned. AmneziaWG gets the obfuscation profile, plain
    // WireGuard never does.
    static QString build(const PoolCard &card, const PoolNode &node, const QString &privateKey, const QString &address);

    // Tunnel config of a `client_config` card (hysteria2), built from the
    // node's `payload` alone: no peer, no address, no device key.
    //
    // The result is an xray config (`{"outbounds":[...]}`) whose outbound is in
    // the same legacy hysteria2 schema the v1 api hands out (protocol
    // "hysteria2", `settings.servers[]`, `network: "udp"`), which is exactly
    // what the existing transport already consumes: the desktop path converts
    // it with `apiUtils::translateLegacyHysteria2Outbound`
    // (vpnConfigurationController) and the iOS path does the same in
    // IosController::setupXray. No second protocol implementation.
    // Empty when the payload cannot be dialled (missing auth/server/port) or
    // when the card is a client_config of a protocol this build cannot build.
    static QString buildClientConfig(const PoolCard &card, const PoolNode &node);

    // Only the fields the builder understands: the profile is flattened into INI
    // keys the amneziawg tool accepts (Jc/Jmin/Jmax/S1/S2/H1..H4/I1..I5).
    static QString obfuscationBlock(const PoolNode &node);

    // The api serializes the profile in lower_snake_case (`jc`, `jmin`, `h1`,
    // `random_trailers`), while every config consumer in the app — the desktop
    // daemon, the iOS extension and the INI — reads the awg-quick spelling
    // (`Jc`, `Jmin`, `H1`, `RandomTrailers`). Empty for unknown keys.
    static QString canonicalObfuscationKey(const QString &apiKey);

    // The obfuscation profile in the app's own spelling, ready to be merged
    // into the protocol JSON of a peer card: a JSON keyed by `jc` leaves the
    // daemon with the default magic headers, and the node then drops every
    // packet.
    static QJsonObject obfuscationData(const PoolNode &node);
};

// Fingerprint of the dial parameters of a `client_config` card.
//
// A client_config card is a function of its `nodes[].payload`: the payload
// carries the auth token, the dial server/port, the SNI and the hysteria2 obfs
// PSK, any of which the api may change between two refreshes while the card id
// stays the same. The fingerprint (SHA-256 over the compact JSON of every
// node's payload, in node order) is what the row stores next to its cached
// tunnel, so a config produced from an older payload is recognisable — even
// after an app restart — and is rebuilt instead of dialled.
//
// Empty when no node carries a payload: a `peer` card keeps a constant
// fingerprint, so its tunnel cache is never invalidated by this.
QString payloadFingerprint(const QVector<PoolNode> &nodes);

// Same fingerprint, read off the JSON a row stores (`peerNodes` of the row, or
// `nodes` of one `peerVariants` entry).
QString payloadFingerprint(const QJsonArray &nodeJson);

} // namespace amnezia::peers

#endif // PEERCONFIGBUILDER_H
